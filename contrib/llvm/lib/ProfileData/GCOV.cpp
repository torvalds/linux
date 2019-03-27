//===- GCOV.cpp - LLVM coverage tool --------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// GCOV implements the interface to read and write coverage files that use
// 'gcov' format.
//
//===----------------------------------------------------------------------===//

#include "llvm/ProfileData/GCOV.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <system_error>

using namespace llvm;

//===----------------------------------------------------------------------===//
// GCOVFile implementation.

/// readGCNO - Read GCNO buffer.
bool GCOVFile::readGCNO(GCOVBuffer &Buffer) {
  if (!Buffer.readGCNOFormat())
    return false;
  if (!Buffer.readGCOVVersion(Version))
    return false;

  if (!Buffer.readInt(Checksum))
    return false;
  while (true) {
    if (!Buffer.readFunctionTag())
      break;
    auto GFun = make_unique<GCOVFunction>(*this);
    if (!GFun->readGCNO(Buffer, Version))
      return false;
    Functions.push_back(std::move(GFun));
  }

  GCNOInitialized = true;
  return true;
}

/// readGCDA - Read GCDA buffer. It is required that readGCDA() can only be
/// called after readGCNO().
bool GCOVFile::readGCDA(GCOVBuffer &Buffer) {
  assert(GCNOInitialized && "readGCDA() can only be called after readGCNO()");
  if (!Buffer.readGCDAFormat())
    return false;
  GCOV::GCOVVersion GCDAVersion;
  if (!Buffer.readGCOVVersion(GCDAVersion))
    return false;
  if (Version != GCDAVersion) {
    errs() << "GCOV versions do not match.\n";
    return false;
  }

  uint32_t GCDAChecksum;
  if (!Buffer.readInt(GCDAChecksum))
    return false;
  if (Checksum != GCDAChecksum) {
    errs() << "File checksums do not match: " << Checksum
           << " != " << GCDAChecksum << ".\n";
    return false;
  }
  for (size_t i = 0, e = Functions.size(); i < e; ++i) {
    if (!Buffer.readFunctionTag()) {
      errs() << "Unexpected number of functions.\n";
      return false;
    }
    if (!Functions[i]->readGCDA(Buffer, Version))
      return false;
  }
  if (Buffer.readObjectTag()) {
    uint32_t Length;
    uint32_t Dummy;
    if (!Buffer.readInt(Length))
      return false;
    if (!Buffer.readInt(Dummy))
      return false; // checksum
    if (!Buffer.readInt(Dummy))
      return false; // num
    if (!Buffer.readInt(RunCount))
      return false;
    Buffer.advanceCursor(Length - 3);
  }
  while (Buffer.readProgramTag()) {
    uint32_t Length;
    if (!Buffer.readInt(Length))
      return false;
    Buffer.advanceCursor(Length);
    ++ProgramCount;
  }

  return true;
}

void GCOVFile::print(raw_ostream &OS) const {
  for (const auto &FPtr : Functions)
    FPtr->print(OS);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
/// dump - Dump GCOVFile content to dbgs() for debugging purposes.
LLVM_DUMP_METHOD void GCOVFile::dump() const { print(dbgs()); }
#endif

/// collectLineCounts - Collect line counts. This must be used after
/// reading .gcno and .gcda files.
void GCOVFile::collectLineCounts(FileInfo &FI) {
  for (const auto &FPtr : Functions)
    FPtr->collectLineCounts(FI);
  FI.setRunCount(RunCount);
  FI.setProgramCount(ProgramCount);
}

//===----------------------------------------------------------------------===//
// GCOVFunction implementation.

/// readGCNO - Read a function from the GCNO buffer. Return false if an error
/// occurs.
bool GCOVFunction::readGCNO(GCOVBuffer &Buff, GCOV::GCOVVersion Version) {
  uint32_t Dummy;
  if (!Buff.readInt(Dummy))
    return false; // Function header length
  if (!Buff.readInt(Ident))
    return false;
  if (!Buff.readInt(Checksum))
    return false;
  if (Version != GCOV::V402) {
    uint32_t CfgChecksum;
    if (!Buff.readInt(CfgChecksum))
      return false;
    if (Parent.getChecksum() != CfgChecksum) {
      errs() << "File checksums do not match: " << Parent.getChecksum()
             << " != " << CfgChecksum << " in (" << Name << ").\n";
      return false;
    }
  }
  if (!Buff.readString(Name))
    return false;
  if (!Buff.readString(Filename))
    return false;
  if (!Buff.readInt(LineNumber))
    return false;

  // read blocks.
  if (!Buff.readBlockTag()) {
    errs() << "Block tag not found.\n";
    return false;
  }
  uint32_t BlockCount;
  if (!Buff.readInt(BlockCount))
    return false;
  for (uint32_t i = 0, e = BlockCount; i != e; ++i) {
    if (!Buff.readInt(Dummy))
      return false; // Block flags;
    Blocks.push_back(make_unique<GCOVBlock>(*this, i));
  }

  // read edges.
  while (Buff.readEdgeTag()) {
    uint32_t EdgeCount;
    if (!Buff.readInt(EdgeCount))
      return false;
    EdgeCount = (EdgeCount - 1) / 2;
    uint32_t BlockNo;
    if (!Buff.readInt(BlockNo))
      return false;
    if (BlockNo >= BlockCount) {
      errs() << "Unexpected block number: " << BlockNo << " (in " << Name
             << ").\n";
      return false;
    }
    for (uint32_t i = 0, e = EdgeCount; i != e; ++i) {
      uint32_t Dst;
      if (!Buff.readInt(Dst))
        return false;
      Edges.push_back(make_unique<GCOVEdge>(*Blocks[BlockNo], *Blocks[Dst]));
      GCOVEdge *Edge = Edges.back().get();
      Blocks[BlockNo]->addDstEdge(Edge);
      Blocks[Dst]->addSrcEdge(Edge);
      if (!Buff.readInt(Dummy))
        return false; // Edge flag
    }
  }

  // read line table.
  while (Buff.readLineTag()) {
    uint32_t LineTableLength;
    // Read the length of this line table.
    if (!Buff.readInt(LineTableLength))
      return false;
    uint32_t EndPos = Buff.getCursor() + LineTableLength * 4;
    uint32_t BlockNo;
    // Read the block number this table is associated with.
    if (!Buff.readInt(BlockNo))
      return false;
    if (BlockNo >= BlockCount) {
      errs() << "Unexpected block number: " << BlockNo << " (in " << Name
             << ").\n";
      return false;
    }
    GCOVBlock &Block = *Blocks[BlockNo];
    // Read the word that pads the beginning of the line table. This may be a
    // flag of some sort, but seems to always be zero.
    if (!Buff.readInt(Dummy))
      return false;

    // Line information starts here and continues up until the last word.
    if (Buff.getCursor() != (EndPos - sizeof(uint32_t))) {
      StringRef F;
      // Read the source file name.
      if (!Buff.readString(F))
        return false;
      if (Filename != F) {
        errs() << "Multiple sources for a single basic block: " << Filename
               << " != " << F << " (in " << Name << ").\n";
        return false;
      }
      // Read lines up to, but not including, the null terminator.
      while (Buff.getCursor() < (EndPos - 2 * sizeof(uint32_t))) {
        uint32_t Line;
        if (!Buff.readInt(Line))
          return false;
        // Line 0 means this instruction was injected by the compiler. Skip it.
        if (!Line)
          continue;
        Block.addLine(Line);
      }
      // Read the null terminator.
      if (!Buff.readInt(Dummy))
        return false;
    }
    // The last word is either a flag or padding, it isn't clear which. Skip
    // over it.
    if (!Buff.readInt(Dummy))
      return false;
  }
  return true;
}

/// readGCDA - Read a function from the GCDA buffer. Return false if an error
/// occurs.
bool GCOVFunction::readGCDA(GCOVBuffer &Buff, GCOV::GCOVVersion Version) {
  uint32_t HeaderLength;
  if (!Buff.readInt(HeaderLength))
    return false; // Function header length

  uint64_t EndPos = Buff.getCursor() + HeaderLength * sizeof(uint32_t);

  uint32_t GCDAIdent;
  if (!Buff.readInt(GCDAIdent))
    return false;
  if (Ident != GCDAIdent) {
    errs() << "Function identifiers do not match: " << Ident
           << " != " << GCDAIdent << " (in " << Name << ").\n";
    return false;
  }

  uint32_t GCDAChecksum;
  if (!Buff.readInt(GCDAChecksum))
    return false;
  if (Checksum != GCDAChecksum) {
    errs() << "Function checksums do not match: " << Checksum
           << " != " << GCDAChecksum << " (in " << Name << ").\n";
    return false;
  }

  uint32_t CfgChecksum;
  if (Version != GCOV::V402) {
    if (!Buff.readInt(CfgChecksum))
      return false;
    if (Parent.getChecksum() != CfgChecksum) {
      errs() << "File checksums do not match: " << Parent.getChecksum()
             << " != " << CfgChecksum << " (in " << Name << ").\n";
      return false;
    }
  }

  if (Buff.getCursor() < EndPos) {
    StringRef GCDAName;
    if (!Buff.readString(GCDAName))
      return false;
    if (Name != GCDAName) {
      errs() << "Function names do not match: " << Name << " != " << GCDAName
             << ".\n";
      return false;
    }
  }

  if (!Buff.readArcTag()) {
    errs() << "Arc tag not found (in " << Name << ").\n";
    return false;
  }

  uint32_t Count;
  if (!Buff.readInt(Count))
    return false;
  Count /= 2;

  // This for loop adds the counts for each block. A second nested loop is
  // required to combine the edge counts that are contained in the GCDA file.
  for (uint32_t BlockNo = 0; Count > 0; ++BlockNo) {
    // The last block is always reserved for exit block
    if (BlockNo >= Blocks.size()) {
      errs() << "Unexpected number of edges (in " << Name << ").\n";
      return false;
    }
    if (BlockNo == Blocks.size() - 1)
      errs() << "(" << Name << ") has arcs from exit block.\n";
    GCOVBlock &Block = *Blocks[BlockNo];
    for (size_t EdgeNo = 0, End = Block.getNumDstEdges(); EdgeNo < End;
         ++EdgeNo) {
      if (Count == 0) {
        errs() << "Unexpected number of edges (in " << Name << ").\n";
        return false;
      }
      uint64_t ArcCount;
      if (!Buff.readInt64(ArcCount))
        return false;
      Block.addCount(EdgeNo, ArcCount);
      --Count;
    }
    Block.sortDstEdges();
  }
  return true;
}

/// getEntryCount - Get the number of times the function was called by
/// retrieving the entry block's count.
uint64_t GCOVFunction::getEntryCount() const {
  return Blocks.front()->getCount();
}

/// getExitCount - Get the number of times the function returned by retrieving
/// the exit block's count.
uint64_t GCOVFunction::getExitCount() const {
  return Blocks.back()->getCount();
}

void GCOVFunction::print(raw_ostream &OS) const {
  OS << "===== " << Name << " (" << Ident << ") @ " << Filename << ":"
     << LineNumber << "\n";
  for (const auto &Block : Blocks)
    Block->print(OS);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
/// dump - Dump GCOVFunction content to dbgs() for debugging purposes.
LLVM_DUMP_METHOD void GCOVFunction::dump() const { print(dbgs()); }
#endif

/// collectLineCounts - Collect line counts. This must be used after
/// reading .gcno and .gcda files.
void GCOVFunction::collectLineCounts(FileInfo &FI) {
  // If the line number is zero, this is a function that doesn't actually appear
  // in the source file, so there isn't anything we can do with it.
  if (LineNumber == 0)
    return;

  for (const auto &Block : Blocks)
    Block->collectLineCounts(FI);
  FI.addFunctionLine(Filename, LineNumber, this);
}

//===----------------------------------------------------------------------===//
// GCOVBlock implementation.

/// ~GCOVBlock - Delete GCOVBlock and its content.
GCOVBlock::~GCOVBlock() {
  SrcEdges.clear();
  DstEdges.clear();
  Lines.clear();
}

/// addCount - Add to block counter while storing the edge count. If the
/// destination has no outgoing edges, also update that block's count too.
void GCOVBlock::addCount(size_t DstEdgeNo, uint64_t N) {
  assert(DstEdgeNo < DstEdges.size()); // up to caller to ensure EdgeNo is valid
  DstEdges[DstEdgeNo]->Count = N;
  Counter += N;
  if (!DstEdges[DstEdgeNo]->Dst.getNumDstEdges())
    DstEdges[DstEdgeNo]->Dst.Counter += N;
}

/// sortDstEdges - Sort destination edges by block number, nop if already
/// sorted. This is required for printing branch info in the correct order.
void GCOVBlock::sortDstEdges() {
  if (!DstEdgesAreSorted) {
    SortDstEdgesFunctor SortEdges;
    std::stable_sort(DstEdges.begin(), DstEdges.end(), SortEdges);
  }
}

/// collectLineCounts - Collect line counts. This must be used after
/// reading .gcno and .gcda files.
void GCOVBlock::collectLineCounts(FileInfo &FI) {
  for (uint32_t N : Lines)
    FI.addBlockLine(Parent.getFilename(), N, this);
}

void GCOVBlock::print(raw_ostream &OS) const {
  OS << "Block : " << Number << " Counter : " << Counter << "\n";
  if (!SrcEdges.empty()) {
    OS << "\tSource Edges : ";
    for (const GCOVEdge *Edge : SrcEdges)
      OS << Edge->Src.Number << " (" << Edge->Count << "), ";
    OS << "\n";
  }
  if (!DstEdges.empty()) {
    OS << "\tDestination Edges : ";
    for (const GCOVEdge *Edge : DstEdges)
      OS << Edge->Dst.Number << " (" << Edge->Count << "), ";
    OS << "\n";
  }
  if (!Lines.empty()) {
    OS << "\tLines : ";
    for (uint32_t N : Lines)
      OS << (N) << ",";
    OS << "\n";
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
/// dump - Dump GCOVBlock content to dbgs() for debugging purposes.
LLVM_DUMP_METHOD void GCOVBlock::dump() const { print(dbgs()); }
#endif

//===----------------------------------------------------------------------===//
// Cycles detection
//
// The algorithm in GCC is based on the algorihtm by Hawick & James:
//   "Enumerating Circuits and Loops in Graphs with Self-Arcs and Multiple-Arcs"
//   http://complexity.massey.ac.nz/cstn/013/cstn-013.pdf.

/// Get the count for the detected cycle.
uint64_t GCOVBlock::getCycleCount(const Edges &Path) {
  uint64_t CycleCount = std::numeric_limits<uint64_t>::max();
  for (auto E : Path) {
    CycleCount = std::min(E->CyclesCount, CycleCount);
  }
  for (auto E : Path) {
    E->CyclesCount -= CycleCount;
  }
  return CycleCount;
}

/// Unblock a vertex previously marked as blocked.
void GCOVBlock::unblock(const GCOVBlock *U, BlockVector &Blocked,
                        BlockVectorLists &BlockLists) {
  auto it = find(Blocked, U);
  if (it == Blocked.end()) {
    return;
  }

  const size_t index = it - Blocked.begin();
  Blocked.erase(it);

  const BlockVector ToUnblock(BlockLists[index]);
  BlockLists.erase(BlockLists.begin() + index);
  for (auto GB : ToUnblock) {
    GCOVBlock::unblock(GB, Blocked, BlockLists);
  }
}

bool GCOVBlock::lookForCircuit(const GCOVBlock *V, const GCOVBlock *Start,
                               Edges &Path, BlockVector &Blocked,
                               BlockVectorLists &BlockLists,
                               const BlockVector &Blocks, uint64_t &Count) {
  Blocked.push_back(V);
  BlockLists.emplace_back(BlockVector());
  bool FoundCircuit = false;

  for (auto E : V->dsts()) {
    const GCOVBlock *W = &E->Dst;
    if (W < Start || find(Blocks, W) == Blocks.end()) {
      continue;
    }

    Path.push_back(E);

    if (W == Start) {
      // We've a cycle.
      Count += GCOVBlock::getCycleCount(Path);
      FoundCircuit = true;
    } else if (find(Blocked, W) == Blocked.end() && // W is not blocked.
               GCOVBlock::lookForCircuit(W, Start, Path, Blocked, BlockLists,
                                         Blocks, Count)) {
      FoundCircuit = true;
    }

    Path.pop_back();
  }

  if (FoundCircuit) {
    GCOVBlock::unblock(V, Blocked, BlockLists);
  } else {
    for (auto E : V->dsts()) {
      const GCOVBlock *W = &E->Dst;
      if (W < Start || find(Blocks, W) == Blocks.end()) {
        continue;
      }
      const size_t index = find(Blocked, W) - Blocked.begin();
      BlockVector &List = BlockLists[index];
      if (find(List, V) == List.end()) {
        List.push_back(V);
      }
    }
  }

  return FoundCircuit;
}

/// Get the count for the list of blocks which lie on the same line.
void GCOVBlock::getCyclesCount(const BlockVector &Blocks, uint64_t &Count) {
  for (auto Block : Blocks) {
    Edges Path;
    BlockVector Blocked;
    BlockVectorLists BlockLists;

    GCOVBlock::lookForCircuit(Block, Block, Path, Blocked, BlockLists, Blocks,
                              Count);
  }
}

/// Get the count for the list of blocks which lie on the same line.
uint64_t GCOVBlock::getLineCount(const BlockVector &Blocks) {
  uint64_t Count = 0;

  for (auto Block : Blocks) {
    if (Block->getNumSrcEdges() == 0) {
      // The block has no predecessors and a non-null counter
      // (can be the case with entry block in functions).
      Count += Block->getCount();
    } else {
      // Add counts from predecessors that are not on the same line.
      for (auto E : Block->srcs()) {
        const GCOVBlock *W = &E->Src;
        if (find(Blocks, W) == Blocks.end()) {
          Count += E->Count;
        }
      }
    }
    for (auto E : Block->dsts()) {
      E->CyclesCount = E->Count;
    }
  }

  GCOVBlock::getCyclesCount(Blocks, Count);

  return Count;
}

//===----------------------------------------------------------------------===//
// FileInfo implementation.

// Safe integer division, returns 0 if numerator is 0.
static uint32_t safeDiv(uint64_t Numerator, uint64_t Divisor) {
  if (!Numerator)
    return 0;
  return Numerator / Divisor;
}

// This custom division function mimics gcov's branch ouputs:
//   - Round to closest whole number
//   - Only output 0% or 100% if it's exactly that value
static uint32_t branchDiv(uint64_t Numerator, uint64_t Divisor) {
  if (!Numerator)
    return 0;
  if (Numerator == Divisor)
    return 100;

  uint8_t Res = (Numerator * 100 + Divisor / 2) / Divisor;
  if (Res == 0)
    return 1;
  if (Res == 100)
    return 99;
  return Res;
}

namespace {
struct formatBranchInfo {
  formatBranchInfo(const GCOV::Options &Options, uint64_t Count, uint64_t Total)
      : Options(Options), Count(Count), Total(Total) {}

  void print(raw_ostream &OS) const {
    if (!Total)
      OS << "never executed";
    else if (Options.BranchCount)
      OS << "taken " << Count;
    else
      OS << "taken " << branchDiv(Count, Total) << "%";
  }

  const GCOV::Options &Options;
  uint64_t Count;
  uint64_t Total;
};

static raw_ostream &operator<<(raw_ostream &OS, const formatBranchInfo &FBI) {
  FBI.print(OS);
  return OS;
}

class LineConsumer {
  std::unique_ptr<MemoryBuffer> Buffer;
  StringRef Remaining;

public:
  LineConsumer(StringRef Filename) {
    ErrorOr<std::unique_ptr<MemoryBuffer>> BufferOrErr =
        MemoryBuffer::getFileOrSTDIN(Filename);
    if (std::error_code EC = BufferOrErr.getError()) {
      errs() << Filename << ": " << EC.message() << "\n";
      Remaining = "";
    } else {
      Buffer = std::move(BufferOrErr.get());
      Remaining = Buffer->getBuffer();
    }
  }
  bool empty() { return Remaining.empty(); }
  void printNext(raw_ostream &OS, uint32_t LineNum) {
    StringRef Line;
    if (empty())
      Line = "/*EOF*/";
    else
      std::tie(Line, Remaining) = Remaining.split("\n");
    OS << format("%5u:", LineNum) << Line << "\n";
  }
};
} // end anonymous namespace

/// Convert a path to a gcov filename. If PreservePaths is true, this
/// translates "/" to "#", ".." to "^", and drops ".", to match gcov.
static std::string mangleCoveragePath(StringRef Filename, bool PreservePaths) {
  if (!PreservePaths)
    return sys::path::filename(Filename).str();

  // This behaviour is defined by gcov in terms of text replacements, so it's
  // not likely to do anything useful on filesystems with different textual
  // conventions.
  llvm::SmallString<256> Result("");
  StringRef::iterator I, S, E;
  for (I = S = Filename.begin(), E = Filename.end(); I != E; ++I) {
    if (*I != '/')
      continue;

    if (I - S == 1 && *S == '.') {
      // ".", the current directory, is skipped.
    } else if (I - S == 2 && *S == '.' && *(S + 1) == '.') {
      // "..", the parent directory, is replaced with "^".
      Result.append("^#");
    } else {
      if (S < I)
        // Leave other components intact,
        Result.append(S, I);
      // And separate with "#".
      Result.push_back('#');
    }
    S = I + 1;
  }

  if (S < I)
    Result.append(S, I);
  return Result.str();
}

std::string FileInfo::getCoveragePath(StringRef Filename,
                                      StringRef MainFilename) {
  if (Options.NoOutput)
    // This is probably a bug in gcov, but when -n is specified, paths aren't
    // mangled at all, and the -l and -p options are ignored. Here, we do the
    // same.
    return Filename;

  std::string CoveragePath;
  if (Options.LongFileNames && !Filename.equals(MainFilename))
    CoveragePath =
        mangleCoveragePath(MainFilename, Options.PreservePaths) + "##";
  CoveragePath += mangleCoveragePath(Filename, Options.PreservePaths) + ".gcov";
  return CoveragePath;
}

std::unique_ptr<raw_ostream>
FileInfo::openCoveragePath(StringRef CoveragePath) {
  if (Options.NoOutput)
    return llvm::make_unique<raw_null_ostream>();

  std::error_code EC;
  auto OS =
      llvm::make_unique<raw_fd_ostream>(CoveragePath, EC, sys::fs::F_Text);
  if (EC) {
    errs() << EC.message() << "\n";
    return llvm::make_unique<raw_null_ostream>();
  }
  return std::move(OS);
}

/// print -  Print source files with collected line count information.
void FileInfo::print(raw_ostream &InfoOS, StringRef MainFilename,
                     StringRef GCNOFile, StringRef GCDAFile) {
  SmallVector<StringRef, 4> Filenames;
  for (const auto &LI : LineInfo)
    Filenames.push_back(LI.first());
  llvm::sort(Filenames);

  for (StringRef Filename : Filenames) {
    auto AllLines = LineConsumer(Filename);

    std::string CoveragePath = getCoveragePath(Filename, MainFilename);
    std::unique_ptr<raw_ostream> CovStream = openCoveragePath(CoveragePath);
    raw_ostream &CovOS = *CovStream;

    CovOS << "        -:    0:Source:" << Filename << "\n";
    CovOS << "        -:    0:Graph:" << GCNOFile << "\n";
    CovOS << "        -:    0:Data:" << GCDAFile << "\n";
    CovOS << "        -:    0:Runs:" << RunCount << "\n";
    CovOS << "        -:    0:Programs:" << ProgramCount << "\n";

    const LineData &Line = LineInfo[Filename];
    GCOVCoverage FileCoverage(Filename);
    for (uint32_t LineIndex = 0; LineIndex < Line.LastLine || !AllLines.empty();
         ++LineIndex) {
      if (Options.BranchInfo) {
        FunctionLines::const_iterator FuncsIt = Line.Functions.find(LineIndex);
        if (FuncsIt != Line.Functions.end())
          printFunctionSummary(CovOS, FuncsIt->second);
      }

      BlockLines::const_iterator BlocksIt = Line.Blocks.find(LineIndex);
      if (BlocksIt == Line.Blocks.end()) {
        // No basic blocks are on this line. Not an executable line of code.
        CovOS << "        -:";
        AllLines.printNext(CovOS, LineIndex + 1);
      } else {
        const BlockVector &Blocks = BlocksIt->second;

        // Add up the block counts to form line counts.
        DenseMap<const GCOVFunction *, bool> LineExecs;
        for (const GCOVBlock *Block : Blocks) {
          if (Options.FuncCoverage) {
            // This is a slightly convoluted way to most accurately gather line
            // statistics for functions. Basically what is happening is that we
            // don't want to count a single line with multiple blocks more than
            // once. However, we also don't simply want to give the total line
            // count to every function that starts on the line. Thus, what is
            // happening here are two things:
            // 1) Ensure that the number of logical lines is only incremented
            //    once per function.
            // 2) If there are multiple blocks on the same line, ensure that the
            //    number of lines executed is incremented as long as at least
            //    one of the blocks are executed.
            const GCOVFunction *Function = &Block->getParent();
            if (FuncCoverages.find(Function) == FuncCoverages.end()) {
              std::pair<const GCOVFunction *, GCOVCoverage> KeyValue(
                  Function, GCOVCoverage(Function->getName()));
              FuncCoverages.insert(KeyValue);
            }
            GCOVCoverage &FuncCoverage = FuncCoverages.find(Function)->second;

            if (LineExecs.find(Function) == LineExecs.end()) {
              if (Block->getCount()) {
                ++FuncCoverage.LinesExec;
                LineExecs[Function] = true;
              } else {
                LineExecs[Function] = false;
              }
              ++FuncCoverage.LogicalLines;
            } else if (!LineExecs[Function] && Block->getCount()) {
              ++FuncCoverage.LinesExec;
              LineExecs[Function] = true;
            }
          }
        }

        const uint64_t LineCount = GCOVBlock::getLineCount(Blocks);
        if (LineCount == 0)
          CovOS << "    #####:";
        else {
          CovOS << format("%9" PRIu64 ":", LineCount);
          ++FileCoverage.LinesExec;
        }
        ++FileCoverage.LogicalLines;

        AllLines.printNext(CovOS, LineIndex + 1);

        uint32_t BlockNo = 0;
        uint32_t EdgeNo = 0;
        for (const GCOVBlock *Block : Blocks) {
          // Only print block and branch information at the end of the block.
          if (Block->getLastLine() != LineIndex + 1)
            continue;
          if (Options.AllBlocks)
            printBlockInfo(CovOS, *Block, LineIndex, BlockNo);
          if (Options.BranchInfo) {
            size_t NumEdges = Block->getNumDstEdges();
            if (NumEdges > 1)
              printBranchInfo(CovOS, *Block, FileCoverage, EdgeNo);
            else if (Options.UncondBranch && NumEdges == 1)
              printUncondBranchInfo(CovOS, EdgeNo,
                                    (*Block->dst_begin())->Count);
          }
        }
      }
    }
    FileCoverages.push_back(std::make_pair(CoveragePath, FileCoverage));
  }

  // FIXME: There is no way to detect calls given current instrumentation.
  if (Options.FuncCoverage)
    printFuncCoverage(InfoOS);
  printFileCoverage(InfoOS);
}

/// printFunctionSummary - Print function and block summary.
void FileInfo::printFunctionSummary(raw_ostream &OS,
                                    const FunctionVector &Funcs) const {
  for (const GCOVFunction *Func : Funcs) {
    uint64_t EntryCount = Func->getEntryCount();
    uint32_t BlocksExec = 0;
    for (const GCOVBlock &Block : Func->blocks())
      if (Block.getNumDstEdges() && Block.getCount())
        ++BlocksExec;

    OS << "function " << Func->getName() << " called " << EntryCount
       << " returned " << safeDiv(Func->getExitCount() * 100, EntryCount)
       << "% blocks executed "
       << safeDiv(BlocksExec * 100, Func->getNumBlocks() - 1) << "%\n";
  }
}

/// printBlockInfo - Output counts for each block.
void FileInfo::printBlockInfo(raw_ostream &OS, const GCOVBlock &Block,
                              uint32_t LineIndex, uint32_t &BlockNo) const {
  if (Block.getCount() == 0)
    OS << "    $$$$$:";
  else
    OS << format("%9" PRIu64 ":", Block.getCount());
  OS << format("%5u-block %2u\n", LineIndex + 1, BlockNo++);
}

/// printBranchInfo - Print conditional branch probabilities.
void FileInfo::printBranchInfo(raw_ostream &OS, const GCOVBlock &Block,
                               GCOVCoverage &Coverage, uint32_t &EdgeNo) {
  SmallVector<uint64_t, 16> BranchCounts;
  uint64_t TotalCounts = 0;
  for (const GCOVEdge *Edge : Block.dsts()) {
    BranchCounts.push_back(Edge->Count);
    TotalCounts += Edge->Count;
    if (Block.getCount())
      ++Coverage.BranchesExec;
    if (Edge->Count)
      ++Coverage.BranchesTaken;
    ++Coverage.Branches;

    if (Options.FuncCoverage) {
      const GCOVFunction *Function = &Block.getParent();
      GCOVCoverage &FuncCoverage = FuncCoverages.find(Function)->second;
      if (Block.getCount())
        ++FuncCoverage.BranchesExec;
      if (Edge->Count)
        ++FuncCoverage.BranchesTaken;
      ++FuncCoverage.Branches;
    }
  }

  for (uint64_t N : BranchCounts)
    OS << format("branch %2u ", EdgeNo++)
       << formatBranchInfo(Options, N, TotalCounts) << "\n";
}

/// printUncondBranchInfo - Print unconditional branch probabilities.
void FileInfo::printUncondBranchInfo(raw_ostream &OS, uint32_t &EdgeNo,
                                     uint64_t Count) const {
  OS << format("unconditional %2u ", EdgeNo++)
     << formatBranchInfo(Options, Count, Count) << "\n";
}

// printCoverage - Print generic coverage info used by both printFuncCoverage
// and printFileCoverage.
void FileInfo::printCoverage(raw_ostream &OS,
                             const GCOVCoverage &Coverage) const {
  OS << format("Lines executed:%.2f%% of %u\n",
               double(Coverage.LinesExec) * 100 / Coverage.LogicalLines,
               Coverage.LogicalLines);
  if (Options.BranchInfo) {
    if (Coverage.Branches) {
      OS << format("Branches executed:%.2f%% of %u\n",
                   double(Coverage.BranchesExec) * 100 / Coverage.Branches,
                   Coverage.Branches);
      OS << format("Taken at least once:%.2f%% of %u\n",
                   double(Coverage.BranchesTaken) * 100 / Coverage.Branches,
                   Coverage.Branches);
    } else {
      OS << "No branches\n";
    }
    OS << "No calls\n"; // to be consistent with gcov
  }
}

// printFuncCoverage - Print per-function coverage info.
void FileInfo::printFuncCoverage(raw_ostream &OS) const {
  for (const auto &FC : FuncCoverages) {
    const GCOVCoverage &Coverage = FC.second;
    OS << "Function '" << Coverage.Name << "'\n";
    printCoverage(OS, Coverage);
    OS << "\n";
  }
}

// printFileCoverage - Print per-file coverage info.
void FileInfo::printFileCoverage(raw_ostream &OS) const {
  for (const auto &FC : FileCoverages) {
    const std::string &Filename = FC.first;
    const GCOVCoverage &Coverage = FC.second;
    OS << "File '" << Coverage.Name << "'\n";
    printCoverage(OS, Coverage);
    if (!Options.NoOutput)
      OS << Coverage.Name << ":creating '" << Filename << "'\n";
    OS << "\n";
  }
}
