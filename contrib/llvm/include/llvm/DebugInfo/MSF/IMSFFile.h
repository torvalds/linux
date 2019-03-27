//===- IMSFFile.h - Abstract base class for an MSF file ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_MSF_IMSFFILE_H
#define LLVM_DEBUGINFO_MSF_IMSFFILE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <cstdint>

namespace llvm {
namespace msf {

class IMSFFile {
public:
  virtual ~IMSFFile() = default;

  virtual uint32_t getBlockSize() const = 0;
  virtual uint32_t getBlockCount() const = 0;

  virtual uint32_t getNumStreams() const = 0;
  virtual uint32_t getStreamByteSize(uint32_t StreamIndex) const = 0;
  virtual ArrayRef<support::ulittle32_t>
  getStreamBlockList(uint32_t StreamIndex) const = 0;

  virtual Expected<ArrayRef<uint8_t>> getBlockData(uint32_t BlockIndex,
                                                   uint32_t NumBytes) const = 0;
  virtual Error setBlockData(uint32_t BlockIndex, uint32_t Offset,
                             ArrayRef<uint8_t> Data) const = 0;
};

} // end namespace msf
} // end namespace llvm

#endif // LLVM_DEBUGINFO_MSF_IMSFFILE_H
