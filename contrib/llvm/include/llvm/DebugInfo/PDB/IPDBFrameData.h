//===- IPDBFrameData.h - base interface for frame data ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_IPDBFRAMEDATA_H
#define LLVM_DEBUGINFO_PDB_IPDBFRAMEDATA_H

#include <cstdint>
#include <string>

namespace llvm {
namespace pdb {

/// IPDBFrameData defines an interface used to represent a frame data of some
/// code block.
class IPDBFrameData {
public:
  virtual ~IPDBFrameData();

  virtual uint32_t getAddressOffset() const = 0;
  virtual uint32_t getAddressSection() const = 0;
  virtual uint32_t getLengthBlock() const = 0;
  virtual std::string getProgram() const = 0;
  virtual uint32_t getRelativeVirtualAddress() const = 0;
  virtual uint64_t getVirtualAddress() const = 0;
};

} // namespace pdb
} // namespace llvm

#endif
