/**
	Multithreaded file read and write with text edition.

	@author  Mateusz Łyszkiewicz
	@version 1.0
	@since   2020 - 04 - 20
*/

#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <chrono>
#include <thread>
#include <mutex>
#include <vector>
#include <array>
#include <deque>
#include <map>

constexpr bool DEBUG = true;



bool fileChanger(std::filesystem::path& fileName, std::vector<std::array<std::string, 2>>& wordsToChange);

int main() {

	namespace fs = std::filesystem;

	std::ios::sync_with_stdio(false);
	std::locale loc("en_US.UTF-8");
	std::cin.imbue(loc);

	std::cout <<
		"Folder with input files is named: \"inputFiles\".\n"
		"Put the words to change it in all entry files.\n"
		"To end of entering data, put \"--\".\n\n"
		"Example entry names: \"word1 -> word2\".\n\n";

	std::string inputLine;
	std::string wordToChange;
	std::vector<std::array<std::string, 2>> wordsToChange;

	for (int i = 0; i < 100; i++) {
		std::getline(std::cin, inputLine);
		if (inputLine.find("--", 0) != std::string::npos) {
			std::cout << std::endl;
			break;
		}

		inputLine.erase(remove_if(inputLine.begin(), inputLine.end(), isspace), inputLine.end());
		size_t position = inputLine.find("->", 0);
		if (position < inputLine.size()) {
			std::array<std::string, 2> words;
			words[0] = inputLine.substr(0, position);
			words[1] = inputLine.substr(position + (size_t)2, inputLine.back());
			wordsToChange.push_back(words);
		}
		else
			std::cout << "Input data is incorrect!\n";
	}

	std::string inputFolder = "inputFiles";
	fs::create_directory(inputFolder);
	std::multimap<int, fs::path> filesList;

	for (auto& p : fs::directory_iterator(inputFolder))
		filesList.insert(std::pair<int, fs::path>(fs::file_size(p.path()), p.path()));

	if (!filesList.size())
		std::cout << "\"inputFolder\" is empty!\n\n";

	auto start = std::chrono::steady_clock::now();

	int numberOfThreads = std::thread::hardware_concurrency();
	int maximumThreads = numberOfThreads >> 1;
	std::deque<std::thread> threads;

	for (std::multimap<int, fs::path>::iterator it = filesList.begin(); it != filesList.end(); ++it) {

		if constexpr (DEBUG) {
			static int counter = 0;
			std::cout << "< DEBUG MODE > Thread " << counter++ << " has started..." << std::endl;
		}

		std::thread th{ [&, it] {
			fileChanger(it->second, wordsToChange);
		} };
		threads.push_back(std::move(th));

		if (threads.size() == maximumThreads) {
			threads.front().join();
			threads.pop_front();
		}
	}

	for (auto& thread : threads) {
		if (thread.joinable())
			thread.join();
	}
	threads.clear();
	if constexpr (DEBUG)
		std::cout << "< DEBUG MODE > All threads have been finished." << std::endl;

	auto end = std::chrono::steady_clock::now();
	std::chrono::duration<double> time = end - start;
	std::cout << "Time: " << time.count() << " sec.\n";
}

bool fileChanger(std::filesystem::path& file, std::vector<std::array<std::string, 2>>& wordsToChange) {

	namespace fs = std::filesystem;

	bool finish = false;

	do {

		std::string fileName;
		fileName = file.filename().u8string();

		std::string outputFolder = "outputFiles";
		std::string outputFileName;

		fs::create_directory(outputFolder);
		outputFileName.append(outputFolder);
		outputFileName.append("/");
		outputFileName.append(fileName, 0, fileName.length() - 4);
		outputFileName.append("_new.txt");

		std::ifstream inFile(file.u8string(), std::ios::in | std::ios::binary);
		std::ofstream outFile(outputFileName, std::ios::out | std::ios::binary);

		if (!inFile.good()) {
			std::cout << "Can't open " << fileName << " file.\n";
			break;
		}

		if (!outFile.good()) {
			std::cout << "Can't create " << fileName << " file.\n";
			break;
		}

		// std::getline is much slower than read on Windows
		//std::string line;
		//while(std::getline(inFile, line));

		std::filebuf* pbuf = inFile.rdbuf();
		std::size_t fileSize = pbuf->pubseekoff(0, inFile.end, inFile.in);
		pbuf->pubseekpos(0, inFile.in);

		constexpr std::size_t kB = 1024;
		constexpr std::size_t MB = 1024 * kB;
		constexpr std::size_t bufferSize = 5 * MB;
		int readOffset = 1 * kB;
		std::size_t remainingFileSize = fileSize;

		std::string buffer[2];
		bool bufferReady[2]{ false, false };
		bool readFinish = false;

		std::condition_variable cv;

		std::thread writeThread{
		  [&] {

		  std::mutex localMutex;
		  int writeOffset = readOffset / 2;
		  bool firstWrite = true;

		  while (true) {

			  std::unique_lock<std::mutex> internalLock(localMutex);
			  cv.wait(internalLock, [&] {
				  return bufferReady[0] || bufferReady[1] || readFinish;
				  });

			  if (readFinish && !bufferReady[0] && !bufferReady[1])
				  break;
			  else if (bufferReady[0]) {
				  if (firstWrite)
					outFile.write(buffer[0].data(), buffer[0].size());
				  else {
					  outFile.seekp(-writeOffset, std::ios_base::cur);
					  outFile.write(buffer[0].data() + writeOffset, buffer[0].size() - writeOffset);
				  }
					bufferReady[0] = false;
				  firstWrite = false;
			  }
			  else if (bufferReady[1]) {
				  outFile.seekp(-writeOffset, std::ios_base::cur);
				  outFile.write(buffer[1].data() + writeOffset, buffer[1].size() - writeOffset);
				  bufferReady[1] = false;
			  }
			  cv.notify_one();
		  }
		} };

		std::string* actualBuffer = &buffer[0];
		std::mutex localMutex;
		bool firstRead = true;
		int numbOfCopiedChars = bufferSize;

		while (numbOfCopiedChars == bufferSize) {

			if (remainingFileSize >= bufferSize) {
				remainingFileSize -= bufferSize - readOffset;
				actualBuffer->reserve(bufferSize);
				actualBuffer->resize(bufferSize);
			}
			else {
				actualBuffer->reserve(remainingFileSize);
				actualBuffer->resize(remainingFileSize);
				remainingFileSize = 0;
			}

			if (firstRead)
				firstRead = false;
			else
				inFile.seekg(-readOffset, std::ios_base::cur);
			numbOfCopiedChars = pbuf->sgetn(actualBuffer->data(), bufferSize);

			std::string wordToChange;
			std::string newWord;

			for (auto& words : wordsToChange) {

				wordToChange = words[0];
				newWord = words[1];

				for (auto position = actualBuffer->find(wordToChange, 0); position != std::string::npos; position = actualBuffer->find(wordToChange, position + newWord.length()))
					actualBuffer->replace(position, wordToChange.length(), newWord);
			}

			{
				std::unique_lock<std::mutex> internalLock(localMutex);
				cv.wait(internalLock, [&] {
					return !(bufferReady[0] && bufferReady[1]);
					});
			}

			if (actualBuffer == &buffer[0]) {
				bufferReady[0] = true;
				actualBuffer = &buffer[1];
			}
			else if (actualBuffer == &buffer[1]) {
				bufferReady[1] = true;
				actualBuffer = &buffer[0];
			}

			cv.notify_one();
		}

		readFinish = true;
		cv.notify_one();

		writeThread.join();

		if (inFile.good() && outFile.good()) {
			std::cout << "File: " << fileName << " is ready.\n";
			finish = true;
		}
		else {
			std::cout << "File: " << fileName << " is fail.\n";
			finish = false;
			break;
		}

	} while (false);

	return finish;
}