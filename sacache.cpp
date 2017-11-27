//
//  main.cpp
//  SA Cache
//
//  Created by Max Nedorezov on 11/23/17.
//  Copyright © 2017 Max Nedorezov. All rights reserved.
//

#include <iostream>
#include <fstream>
#include <iomanip>
#include <sstream>

using namespace std;

enum Hit { miss = 0, hit = 1 };
enum Operation { Read = 0x00, Write = 0xFF};

class CacheLine
{
    int cacheLine[4];

public:
    bool dirty;
    int tag;
    int lastUsed;

    void writeDataToOffset(int data, int offset)
    {
        dirty = true;
        cacheLine[offset] = data;
    }

    void writeLine(int data[4])
    {
        for (int i = 0; i < 4; i++)
            cacheLine[i] = data[i];
    }

    int readDataAtOffset(int offset, int &dirtyLine)
    {
        dirtyLine = dirty;
        return cacheLine[offset];
    }

    void readLine(int data[6]) // data stores the old tag in the second to last position and dirty bit in the last position
    {
        for (int i = 0; i < 4; i++)
            data[i] = cacheLine[i];

        data[4] = tag;
        data[5] = dirty;
    }
};

struct InputInfo
{
    InputInfo(string strAddress, string strOperation, string strData)
    {
        int address;
        stringstream ss;
        ss << hex << strAddress;
        ss >> address;

        tag = (address & 0xFFC0) >> 6; // isolate tag and shift right 6 bits
        setNumber = (address & 0x003C) >> 2;
        offset = address & 0x0003;

        int op;
        stringstream ss2;
        ss2 << hex << strOperation;
        ss2 >> op;
        if (op != 0) operation = Write; else operation = Read;

        stringstream ss3;
        ss3 << hex << strData;
        ss3 >> data;
    }

    int tag;
    int setNumber;
    int offset;

    Operation operation;
    int data;
};

class Set
{
    CacheLine line[8];

    Hit lineExistsInSet(int tag, int &position)
    {
        for (int i = 0; i < 8; i++) {
            if (line[i].tag == tag) {
                position = i;
                return hit;
            }
        }

        return miss;
    }

    int positionOfLRULine()
    {
        int currentLRU = 0;
        for (int i = 0; i < 8; i++)
            if (line[i].lastUsed > line[currentLRU].lastUsed)
                currentLRU = i;

        return currentLRU;
    }

    void updateLRUExceptCurrent(int currentLine)
    {
        line[currentLine].lastUsed = 0;
        for (int i = 0; i < 8; i++)
            if (i != currentLine)
                line[i].lastUsed++;
    }

public:

    Hit tryToWriteDataToOffsetWithTag(int data, int offset, int newTag, int evictedData[6]) // evictedData stores the old tag in the second to last position
    {
        int positionOfTag = 0;
        Hit isHit = lineExistsInSet(newTag, positionOfTag);

        if (isHit == hit) {
            updateLRUExceptCurrent(positionOfTag);
            line[positionOfTag].writeDataToOffset(data, offset);
            return hit;
        }

        int LRULine = positionOfLRULine();
        line[LRULine].readLine(evictedData);

        return miss;
    }

    void updateCacheLineWithDataFromRAM(int newDataFromRam[4], int newTag, int &newData, int offset, Operation operation)
    {
        int LRULine = positionOfLRULine();

        line[LRULine].tag = newTag;

        line[LRULine].writeLine(newDataFromRam);

        if (operation == Write)
            line[LRULine].writeDataToOffset(newData, offset);
        else {
            int dirty = 0; // not needed here but whatever...
            newData = line[LRULine].readDataAtOffset(offset, dirty);
        }

        updateLRUExceptCurrent(LRULine);
    }

    Hit readDataAtOffsetWithTag(int &data, int offset, int dataTag, int evictedData[6]) // evictedData stores the old tag in the second to last position and dirty bit in the last position
    {
        int positionOfTag = 0;
        Hit isHit = lineExistsInSet(dataTag, positionOfTag);

        if (isHit == hit) {
            updateLRUExceptCurrent(positionOfTag);
            data = line[positionOfTag].readDataAtOffset(offset, evictedData[5]); // evictedData[5] stores the dirty bit
            return hit;
        }

        int LRULine = positionOfLRULine();
        line[LRULine].readLine(evictedData);
        line[LRULine].dirty = false;

        return miss;
    }
};

void manipulateCache(InputInfo inputInfo, ofstream &outputFile);
int RAMAddress(int tag, int lineNumber);
void storeAndFetchDataFromRAM(int oldTag, int lineNumber, int evictedData[4], int newDataFromRAM[4], int newTag);

int RAM[65536] = { 0 };
Set cache[16];

int main(int argc, char **argv)
{
    string fileName;
    fileName = argv[1];

    ifstream inputFile;
    inputFile.open(fileName.c_str());

    ofstream outputFile;
    outputFile.open("sa-out.txt");

    string address, operation, data;
    while (inputFile >> address && inputFile >> operation && inputFile >> data)
    {
        InputInfo inputInfo = InputInfo(address, operation, data);
        manipulateCache(inputInfo, outputFile);
    }

    inputFile.close();
    outputFile.close();

    return 0;
}

void manipulateCache(InputInfo inputInfo, ofstream &outputFile)
{
    Set cacheSet = cache[inputInfo.setNumber];

    int evictedData[6] = { 0 };
    int newDataFromRAM[5] = { 0 };

    if (inputInfo.operation == Write)
    {
        Hit hit = cacheSet.tryToWriteDataToOffsetWithTag(inputInfo.data, inputInfo.offset, inputInfo.tag, evictedData);

        if (hit == miss) {
            storeAndFetchDataFromRAM(evictedData[4], inputInfo.setNumber, evictedData, newDataFromRAM, inputInfo.tag);
            cacheSet.updateCacheLineWithDataFromRAM(newDataFromRAM, inputInfo.tag, inputInfo.data, inputInfo.offset, Write);
        }

    } else {

        int readData = 0;
        Hit hit = cacheSet.readDataAtOffsetWithTag(readData, inputInfo.offset, inputInfo.tag, evictedData);

        if (hit == miss) {
            storeAndFetchDataFromRAM(evictedData[4], inputInfo.setNumber, evictedData, newDataFromRAM, inputInfo.tag);
            cacheSet.updateCacheLineWithDataFromRAM(newDataFromRAM, inputInfo.tag, readData, inputInfo.offset, Read);
        }

        outputFile << hex << setfill('0') << setw(2) << uppercase << readData << " " << hit << " " << evictedData[5] << endl; // evictedData[5] is the dirty bit
    }

    cache[inputInfo.setNumber] = cacheSet; // save the current working cache set back to the global cache
}

// probably a good idea to split this into 2 functions but whatever...
void storeAndFetchDataFromRAM(int oldTag, int setNumber, int evictedData[4], int newDataFromRAM[4], int newTag)
{
    // store the evicted data in the RAM
    int evictedDataRAMAddress = RAMAddress(oldTag, setNumber);  // evictedData[4] is the old tag
    for (int i = evictedDataRAMAddress; i < evictedDataRAMAddress + 4; i++)
        RAM[i] = evictedData[i - evictedDataRAMAddress];

    // fetch data from RAM into cache and update cache with new data
    int newRAMAddress = RAMAddress(newTag, setNumber);

    for (int i = 0; i < 4; i++)
        newDataFromRAM[i] = RAM[newRAMAddress + i];
}

int RAMAddress(int tag, int setNumber) // assume offset is 0 because looking for a whole cache line
{
    int address = (tag << 6) | (setNumber << 2);
    return address;
}
