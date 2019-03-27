//==- DIAEnumDebugStreams.h - DIA Debug Stream Enumerator impl ---*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_DIA_DIAENUMDEBUGSTREAMS_H
#define LLVM_DEBUGINFO_PDB_DIA_DIAENUMDEBUGSTREAMS_H

#include "DIASupport.h"
#include "llvm/DebugInfo/PDB/IPDBDataStream.h"
#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"

namespace llvm {
namespace pdb {

class IPDBDataStream;

class DIAEnumDebugStreams : public IPDBEnumChildren<IPDBDataStream> {
public:
  explicit DIAEnumDebugStreams(CComPtr<IDiaEnumDebugStreams> DiaEnumerator);

  uint32_t getChildCount() const override;
  ChildTypePtr getChildAtIndex(uint32_t Index) const override;
  ChildTypePtr getNext() override;
  void reset() override;

private:
  CComPtr<IDiaEnumDebugStreams> Enumerator;
};
}
}

#endif
