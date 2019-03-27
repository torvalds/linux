//===- DIADataStream.cpp - DIA implementation of IPDBDataStream -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/DIA/DIADataStream.h"
#include "llvm/DebugInfo/PDB/DIA/DIAUtils.h"

using namespace llvm;
using namespace llvm::pdb;

DIADataStream::DIADataStream(CComPtr<IDiaEnumDebugStreamData> DiaStreamData)
    : StreamData(DiaStreamData) {}

uint32_t DIADataStream::getRecordCount() const {
  LONG Count = 0;
  return (S_OK == StreamData->get_Count(&Count)) ? Count : 0;
}

std::string DIADataStream::getName() const {
  return invokeBstrMethod(*StreamData, &IDiaEnumDebugStreamData::get_name);
}

llvm::Optional<DIADataStream::RecordType>
DIADataStream::getItemAtIndex(uint32_t Index) const {
  RecordType Record;
  DWORD RecordSize = 0;
  StreamData->Item(Index, 0, &RecordSize, nullptr);
  if (RecordSize == 0)
    return llvm::Optional<RecordType>();

  Record.resize(RecordSize);
  if (S_OK != StreamData->Item(Index, RecordSize, &RecordSize, &Record[0]))
    return llvm::Optional<RecordType>();
  return Record;
}

bool DIADataStream::getNext(RecordType &Record) {
  Record.clear();
  DWORD RecordSize = 0;
  ULONG CountFetched = 0;
  StreamData->Next(1, 0, &RecordSize, nullptr, &CountFetched);
  if (RecordSize == 0)
    return false;

  Record.resize(RecordSize);
  if (S_OK ==
      StreamData->Next(1, RecordSize, &RecordSize, &Record[0], &CountFetched))
    return false;
  return true;
}

void DIADataStream::reset() { StreamData->Reset(); }
