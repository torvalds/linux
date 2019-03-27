//===- MapFile.cpp --------------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the -Map option. It shows lists in order and
// hierarchically the output sections, input sections, input files and
// symbol:
//
//   Address  Size     Align Out     In      Symbol
//   00201000 00000015     4 .text
//   00201000 0000000e     4         test.o:(.text)
//   0020100e 00000000     0                 local
//   00201005 00000000     0                 f(int)
//
//===----------------------------------------------------------------------===//

#include "MapFile.h"
#include "InputFiles.h"
#include "LinkerScript.h"
#include "OutputSections.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "lld/Common/Strings.h"
#include "lld/Common/Threads.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::object;

using namespace lld;
using namespace lld::elf;

typedef DenseMap<const SectionBase *, SmallVector<Defined *, 4>> SymbolMapTy;

static const std::string Indent8 = "        ";          // 8 spaces
static const std::string Indent16 = "                "; // 16 spaces

// Print out the first three columns of a line.
static void writeHeader(raw_ostream &OS, uint64_t VMA, uint64_t LMA,
                        uint64_t Size, uint64_t Align) {
  if (Config->Is64)
    OS << format("%16llx %16llx %8llx %5lld ", VMA, LMA, Size, Align);
  else
    OS << format("%8llx %8llx %8llx %5lld ", VMA, LMA, Size, Align);
}

// Returns a list of all symbols that we want to print out.
static std::vector<Defined *> getSymbols() {
  std::vector<Defined *> V;
  for (InputFile *File : ObjectFiles)
    for (Symbol *B : File->getSymbols())
      if (auto *DR = dyn_cast<Defined>(B))
        if (!DR->isSection() && DR->Section && DR->Section->Live &&
            (DR->File == File || DR->NeedsPltAddr || DR->Section->Bss))
          V.push_back(DR);
  return V;
}

// Returns a map from sections to their symbols.
static SymbolMapTy getSectionSyms(ArrayRef<Defined *> Syms) {
  SymbolMapTy Ret;
  for (Defined *DR : Syms)
    Ret[DR->Section].push_back(DR);

  // Sort symbols by address. We want to print out symbols in the
  // order in the output file rather than the order they appeared
  // in the input files.
  for (auto &It : Ret) {
    SmallVectorImpl<Defined *> &V = It.second;
    std::stable_sort(V.begin(), V.end(), [](Defined *A, Defined *B) {
      return A->getVA() < B->getVA();
    });
  }
  return Ret;
}

// Construct a map from symbols to their stringified representations.
// Demangling symbols (which is what toString() does) is slow, so
// we do that in batch using parallel-for.
static DenseMap<Symbol *, std::string>
getSymbolStrings(ArrayRef<Defined *> Syms) {
  std::vector<std::string> Str(Syms.size());
  parallelForEachN(0, Syms.size(), [&](size_t I) {
    raw_string_ostream OS(Str[I]);
    OutputSection *OSec = Syms[I]->getOutputSection();
    uint64_t VMA = Syms[I]->getVA();
    uint64_t LMA = OSec ? OSec->getLMA() + VMA - OSec->getVA(0) : 0;
    writeHeader(OS, VMA, LMA, Syms[I]->getSize(), 1);
    OS << Indent16 << toString(*Syms[I]);
  });

  DenseMap<Symbol *, std::string> Ret;
  for (size_t I = 0, E = Syms.size(); I < E; ++I)
    Ret[Syms[I]] = std::move(Str[I]);
  return Ret;
}

// Print .eh_frame contents. Since the section consists of EhSectionPieces,
// we need a specialized printer for that section.
//
// .eh_frame tend to contain a lot of section pieces that are contiguous
// both in input file and output file. Such pieces are squashed before
// being displayed to make output compact.
static void printEhFrame(raw_ostream &OS, OutputSection *OSec) {
  std::vector<EhSectionPiece> Pieces;

  auto Add = [&](const EhSectionPiece &P) {
    // If P is adjacent to Last, squash the two.
    if (!Pieces.empty()) {
      EhSectionPiece &Last = Pieces.back();
      if (Last.Sec == P.Sec && Last.InputOff + Last.Size == P.InputOff &&
          Last.OutputOff + Last.Size == P.OutputOff) {
        Last.Size += P.Size;
        return;
      }
    }
    Pieces.push_back(P);
  };

  // Gather section pieces.
  for (const CieRecord *Rec : In.EhFrame->getCieRecords()) {
    Add(*Rec->Cie);
    for (const EhSectionPiece *Fde : Rec->Fdes)
      Add(*Fde);
  }

  // Print out section pieces.
  for (EhSectionPiece &P : Pieces) {
    writeHeader(OS, OSec->Addr + P.OutputOff, OSec->getLMA() + P.OutputOff,
                P.Size, 1);
    OS << Indent8 << toString(P.Sec->File) << ":(" << P.Sec->Name << "+0x"
       << Twine::utohexstr(P.InputOff) + ")\n";
  }
}

void elf::writeMapFile() {
  if (Config->MapFile.empty())
    return;

  // Open a map file for writing.
  std::error_code EC;
  raw_fd_ostream OS(Config->MapFile, EC, sys::fs::F_None);
  if (EC) {
    error("cannot open " + Config->MapFile + ": " + EC.message());
    return;
  }

  // Collect symbol info that we want to print out.
  std::vector<Defined *> Syms = getSymbols();
  SymbolMapTy SectionSyms = getSectionSyms(Syms);
  DenseMap<Symbol *, std::string> SymStr = getSymbolStrings(Syms);

  // Print out the header line.
  int W = Config->Is64 ? 16 : 8;
  OS << right_justify("VMA", W) << ' ' << right_justify("LMA", W)
     << "     Size Align Out     In      Symbol\n";

  OutputSection* OSec = nullptr;
  for (BaseCommand *Base : Script->SectionCommands) {
    if (auto *Cmd = dyn_cast<SymbolAssignment>(Base)) {
      if (Cmd->Provide && !Cmd->Sym)
        continue;
      uint64_t LMA = OSec ? OSec->getLMA() + Cmd->Addr - OSec->getVA(0) : 0;
      writeHeader(OS, Cmd->Addr, LMA, Cmd->Size, 1);
      OS << Cmd->CommandString << '\n';
      continue;
    }

    OSec = cast<OutputSection>(Base);
    writeHeader(OS, OSec->Addr, OSec->getLMA(), OSec->Size, OSec->Alignment);
    OS << OSec->Name << '\n';

    // Dump symbols for each input section.
    for (BaseCommand *Base : OSec->SectionCommands) {
      if (auto *ISD = dyn_cast<InputSectionDescription>(Base)) {
        for (InputSection *IS : ISD->Sections) {
          if (IS == In.EhFrame) {
            printEhFrame(OS, OSec);
            continue;
          }

          writeHeader(OS, IS->getVA(0), OSec->getLMA() + IS->getOffset(0),
                      IS->getSize(), IS->Alignment);
          OS << Indent8 << toString(IS) << '\n';
          for (Symbol *Sym : SectionSyms[IS])
            OS << SymStr[Sym] << '\n';
        }
        continue;
      }

      if (auto *Cmd = dyn_cast<ByteCommand>(Base)) {
        writeHeader(OS, OSec->Addr + Cmd->Offset, OSec->getLMA() + Cmd->Offset,
                    Cmd->Size, 1);
        OS << Indent8 << Cmd->CommandString << '\n';
        continue;
      }

      if (auto *Cmd = dyn_cast<SymbolAssignment>(Base)) {
        if (Cmd->Provide && !Cmd->Sym)
          continue;
        writeHeader(OS, Cmd->Addr, OSec->getLMA() + Cmd->Addr - OSec->getVA(0),
                    Cmd->Size, 1);
        OS << Indent8 << Cmd->CommandString << '\n';
        continue;
      }
    }
  }
}

static void print(StringRef A, StringRef B) {
  outs() << left_justify(A, 49) << " " << B << "\n";
}

// Output a cross reference table to stdout. This is for --cref.
//
// For each global symbol, we print out a file that defines the symbol
// followed by files that uses that symbol. Here is an example.
//
//     strlen     /lib/x86_64-linux-gnu/libc.so.6
//                tools/lld/tools/lld/CMakeFiles/lld.dir/lld.cpp.o
//                lib/libLLVMSupport.a(PrettyStackTrace.cpp.o)
//
// In this case, strlen is defined by libc.so.6 and used by other two
// files.
void elf::writeCrossReferenceTable() {
  if (!Config->Cref)
    return;

  // Collect symbols and files.
  MapVector<Symbol *, SetVector<InputFile *>> Map;
  for (InputFile *File : ObjectFiles) {
    for (Symbol *Sym : File->getSymbols()) {
      if (isa<SharedSymbol>(Sym))
        Map[Sym].insert(File);
      if (auto *D = dyn_cast<Defined>(Sym))
        if (!D->isLocal() && (!D->Section || D->Section->Live))
          Map[D].insert(File);
    }
  }

  // Print out a header.
  outs() << "Cross Reference Table\n\n";
  print("Symbol", "File");

  // Print out a table.
  for (auto KV : Map) {
    Symbol *Sym = KV.first;
    SetVector<InputFile *> &Files = KV.second;

    print(toString(*Sym), toString(Sym->File));
    for (InputFile *File : Files)
      if (File != Sym->File)
        print("", toString(File));
  }
}
