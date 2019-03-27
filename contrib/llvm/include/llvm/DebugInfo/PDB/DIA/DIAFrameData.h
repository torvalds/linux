//===- DIAFrameData.h - DIA Impl. of IPDBFrameData ---------------- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_DIA_DIAFRAMEDATA_H
#define LLVM_DEBUGINFO_PDB_DIA_DIAFRAMEDATA_H

#include "DIASupport.h"
#include "llvm/DebugInfo/PDB/IPDBFrameData.h"

namespace llvm {
namespace pdb {

class DIASession;

class DIAFrameData : public IPDBFrameData {
public:
  explicit DIAFrameData(CComPtr<IDiaFrameData> DiaFrameData);

  uint32_t getAddressOffset() const override;
  uint32_t getAddressSection() const override;
  uint32_t getLengthBlock() const override;
  std::string getProgram() const override;
  uint32_t getRelativeVirtualAddress() const override;
  uint64_t getVirtualAddress() const override;

private:
  CComPtr<IDiaFrameData> FrameData;
};

} // namespace pdb
} // namespace llvm

#endif
