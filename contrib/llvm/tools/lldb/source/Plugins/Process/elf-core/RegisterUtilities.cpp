//===-- RegisterUtilities.cpp -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Plugins/Process/elf-core/RegisterUtilities.h"
#include "llvm/ADT/STLExtras.h"

using namespace lldb_private;

static llvm::Optional<uint32_t>
getNoteType(const llvm::Triple &Triple,
            llvm::ArrayRef<RegsetDesc> RegsetDescs) {
  for (const auto &Entry : RegsetDescs) {
    if (Entry.OS != Triple.getOS())
      continue;
    if (Entry.Arch != llvm::Triple::UnknownArch &&
        Entry.Arch != Triple.getArch())
      continue;
    return Entry.Note;
  }
  return llvm::None;
}

DataExtractor lldb_private::getRegset(llvm::ArrayRef<CoreNote> Notes,
                                      const llvm::Triple &Triple,
                                      llvm::ArrayRef<RegsetDesc> RegsetDescs) {
  auto TypeOr = getNoteType(Triple, RegsetDescs);
  if (!TypeOr)
    return DataExtractor();
  uint32_t Type = *TypeOr;
  auto Iter = llvm::find_if(
      Notes, [Type](const CoreNote &Note) { return Note.info.n_type == Type; });
  return Iter == Notes.end() ? DataExtractor() : Iter->data;
}
