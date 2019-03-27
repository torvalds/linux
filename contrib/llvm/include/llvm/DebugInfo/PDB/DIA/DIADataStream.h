//===- DIADataStream.h - DIA implementation of IPDBDataStream ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_DIA_DIADATASTREAM_H
#define LLVM_DEBUGINFO_PDB_DIA_DIADATASTREAM_H

#include "DIASupport.h"
#include "llvm/DebugInfo/PDB/IPDBDataStream.h"

namespace llvm {
namespace pdb {
class DIADataStream : public IPDBDataStream {
public:
  explicit DIADataStream(CComPtr<IDiaEnumDebugStreamData> DiaStreamData);

  uint32_t getRecordCount() const override;
  std::string getName() const override;
  llvm::Optional<RecordType> getItemAtIndex(uint32_t Index) const override;
  bool getNext(RecordType &Record) override;
  void reset() override;

private:
  CComPtr<IDiaEnumDebugStreamData> StreamData;
};
}
}

#endif
