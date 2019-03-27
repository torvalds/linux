//===- CTagsEmitter.cpp - Generate ctags-compatible index ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This tablegen backend emits an index of definitions in ctags(1) format.
// A helper script, utils/TableGen/tdtags, provides an easier-to-use
// interface; run 'tdtags -H' for documentation.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include <algorithm>
#include <string>
#include <vector>
using namespace llvm;

#define DEBUG_TYPE "ctags-emitter"

namespace {

class Tag {
private:
  const std::string *Id;
  SMLoc Loc;
public:
  Tag(const std::string &Name, const SMLoc Location)
      : Id(&Name), Loc(Location) {}
  int operator<(const Tag &B) const { return *Id < *B.Id; }
  void emit(raw_ostream &OS) const {
    const MemoryBuffer *CurMB =
        SrcMgr.getMemoryBuffer(SrcMgr.FindBufferContainingLoc(Loc));
    auto BufferName = CurMB->getBufferIdentifier();
    std::pair<unsigned, unsigned> LineAndColumn = SrcMgr.getLineAndColumn(Loc);
    OS << *Id << "\t" << BufferName << "\t" << LineAndColumn.first << "\n";
  }
};

class CTagsEmitter {
private:
  RecordKeeper &Records;
public:
  CTagsEmitter(RecordKeeper &R) : Records(R) {}

  void run(raw_ostream &OS);

private:
  static SMLoc locate(const Record *R);
};

} // End anonymous namespace.

SMLoc CTagsEmitter::locate(const Record *R) {
  ArrayRef<SMLoc> Locs = R->getLoc();
  return !Locs.empty() ? Locs.front() : SMLoc();
}

void CTagsEmitter::run(raw_ostream &OS) {
  const auto &Classes = Records.getClasses();
  const auto &Defs = Records.getDefs();
  std::vector<Tag> Tags;
  // Collect tags.
  Tags.reserve(Classes.size() + Defs.size());
  for (const auto &C : Classes)
    Tags.push_back(Tag(C.first, locate(C.second.get())));
  for (const auto &D : Defs)
    Tags.push_back(Tag(D.first, locate(D.second.get())));
  // Emit tags.
  llvm::sort(Tags);
  OS << "!_TAG_FILE_FORMAT\t1\t/original ctags format/\n";
  OS << "!_TAG_FILE_SORTED\t1\t/0=unsorted, 1=sorted, 2=foldcase/\n";
  for (const Tag &T : Tags)
    T.emit(OS);
}

namespace llvm {

void EmitCTags(RecordKeeper &RK, raw_ostream &OS) { CTagsEmitter(RK).run(OS); }

} // End llvm namespace.
