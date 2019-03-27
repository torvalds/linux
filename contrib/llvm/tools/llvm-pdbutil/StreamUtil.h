//===- Streamutil.h - PDB stream utilities ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVMPDBDUMP_STREAMUTIL_H
#define LLVM_TOOLS_LLVMPDBDUMP_STREAMUTIL_H

#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

#include <string>

namespace llvm {
namespace pdb {
class PDBFile;
enum class StreamPurpose {
  NamedStream,
  ModuleStream,
  Symbols,
  PDB,
  DBI,
  TPI,
  IPI,
  GlobalHash,
  PublicHash,
  TpiHash,
  IpiHash,
  Other
};

struct StreamInfo {
public:
  StreamInfo() {}

  uint32_t getModuleIndex() const { return *ModuleIndex; }
  StreamPurpose getPurpose() const { return Purpose; }
  StringRef getShortName() const { return Name; }
  uint32_t getStreamIndex() const { return StreamIndex; }
  std::string getLongName() const;

  static StreamInfo createStream(StreamPurpose Purpose, StringRef Name,
                                 uint32_t StreamIndex);
  static StreamInfo createModuleStream(StringRef Module, uint32_t StreamIndex,
                                       uint32_t Modi);

private:
  StreamPurpose Purpose;
  uint32_t StreamIndex;
  std::string Name;
  Optional<uint32_t> ModuleIndex;
};

void discoverStreamPurposes(PDBFile &File,
                            SmallVectorImpl<StreamInfo> &Streams);
}
}

#endif
