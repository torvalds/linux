//===- ArchiveWriter.h - ar archive file format writer ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Declares the writeArchive function for writing an archive file.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_ARCHIVEWRITER_H
#define LLVM_OBJECT_ARCHIVEWRITER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Object/Archive.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"

namespace llvm {

struct NewArchiveMember {
  std::unique_ptr<MemoryBuffer> Buf;
  StringRef MemberName;
  sys::TimePoint<std::chrono::seconds> ModTime;
  unsigned UID = 0, GID = 0, Perms = 0644;

  bool IsNew = false;
  NewArchiveMember() = default;
  NewArchiveMember(MemoryBufferRef BufRef);

  static Expected<NewArchiveMember>
  getOldMember(const object::Archive::Child &OldMember, bool Deterministic);

  static Expected<NewArchiveMember> getFile(StringRef FileName,
                                            bool Deterministic);
};

Error writeArchive(StringRef ArcName, ArrayRef<NewArchiveMember> NewMembers,
                   bool WriteSymtab, object::Archive::Kind Kind,
                   bool Deterministic, bool Thin,
                   std::unique_ptr<MemoryBuffer> OldArchiveBuf = nullptr);
}

#endif
