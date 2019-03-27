//===- MapFile.cpp --------------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the /lldmap option. It shows lists in order and
// hierarchically the output sections, input sections, input files and
// symbol:
//
//   Address  Size     Align Out     File    Symbol
//   00201000 00000015     4 .text
//   00201000 0000000e     4         test.o:(.text)
//   0020100e 00000000     0                 local
//   00201005 00000000     0                 f(int)
//
//===----------------------------------------------------------------------===//

#include "MapFile.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "Writer.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::object;

using namespace lld;
using namespace lld::coff;

typedef DenseMap<const SectionChunk *, SmallVector<DefinedRegular *, 4>>
    SymbolMapTy;

static const std::string Indent8 = "        ";          // 8 spaces
static const std::string Indent16 = "                "; // 16 spaces

// Print out the first three columns of a line.
static void writeHeader(raw_ostream &OS, uint64_t Addr, uint64_t Size,
                        uint64_t Align) {
  OS << format("%08llx %08llx %5lld ", Addr, Size, Align);
}

// Returns a list of all symbols that we want to print out.
static std::vector<DefinedRegular *> getSymbols() {
  std::vector<DefinedRegular *> V;
  for (ObjFile *File : ObjFile::Instances)
    for (Symbol *B : File->getSymbols())
      if (auto *Sym = dyn_cast_or_null<DefinedRegular>(B))
        if (Sym && !Sym->getCOFFSymbol().isSectionDefinition())
          V.push_back(Sym);
  return V;
}

// Returns a map from sections to their symbols.
static SymbolMapTy getSectionSyms(ArrayRef<DefinedRegular *> Syms) {
  SymbolMapTy Ret;
  for (DefinedRegular *S : Syms)
    Ret[S->getChunk()].push_back(S);

  // Sort symbols by address.
  for (auto &It : Ret) {
    SmallVectorImpl<DefinedRegular *> &V = It.second;
    std::sort(V.begin(), V.end(), [](DefinedRegular *A, DefinedRegular *B) {
      return A->getRVA() < B->getRVA();
    });
  }
  return Ret;
}

// Construct a map from symbols to their stringified representations.
static DenseMap<DefinedRegular *, std::string>
getSymbolStrings(ArrayRef<DefinedRegular *> Syms) {
  std::vector<std::string> Str(Syms.size());
  for_each_n(parallel::par, (size_t)0, Syms.size(), [&](size_t I) {
    raw_string_ostream OS(Str[I]);
    writeHeader(OS, Syms[I]->getRVA(), 0, 0);
    OS << Indent16 << toString(*Syms[I]);
  });

  DenseMap<DefinedRegular *, std::string> Ret;
  for (size_t I = 0, E = Syms.size(); I < E; ++I)
    Ret[Syms[I]] = std::move(Str[I]);
  return Ret;
}

void coff::writeMapFile(ArrayRef<OutputSection *> OutputSections) {
  if (Config->MapFile.empty())
    return;

  std::error_code EC;
  raw_fd_ostream OS(Config->MapFile, EC, sys::fs::F_None);
  if (EC)
    fatal("cannot open " + Config->MapFile + ": " + EC.message());

  // Collect symbol info that we want to print out.
  std::vector<DefinedRegular *> Syms = getSymbols();
  SymbolMapTy SectionSyms = getSectionSyms(Syms);
  DenseMap<DefinedRegular *, std::string> SymStr = getSymbolStrings(Syms);

  // Print out the header line.
  OS << "Address  Size     Align Out     In      Symbol\n";

  // Print out file contents.
  for (OutputSection *Sec : OutputSections) {
    writeHeader(OS, Sec->getRVA(), Sec->getVirtualSize(), /*Align=*/PageSize);
    OS << Sec->Name << '\n';

    for (Chunk *C : Sec->Chunks) {
      auto *SC = dyn_cast<SectionChunk>(C);
      if (!SC)
        continue;

      writeHeader(OS, SC->getRVA(), SC->getSize(), SC->Alignment);
      OS << Indent8 << SC->File->getName() << ":(" << SC->getSectionName()
         << ")\n";
      for (DefinedRegular *Sym : SectionSyms[SC])
        OS << SymStr[Sym] << '\n';
    }
  }
}
