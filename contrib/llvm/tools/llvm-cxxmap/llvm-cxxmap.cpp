//===- llvm-cxxmap.cpp ----------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// llvm-cxxmap computes a correspondence between old symbol names and new
// symbol names based on a symbol equivalence file.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SymbolRemappingReader.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

cl::opt<std::string> OldSymbolFile(cl::Positional, cl::Required,
                                   cl::desc("<symbol-file>"));
cl::opt<std::string> NewSymbolFile(cl::Positional, cl::Required,
                                   cl::desc("<symbol-file>"));
cl::opt<std::string> RemappingFile("remapping-file", cl::Required,
                                   cl::desc("Remapping file"));
cl::alias RemappingFileA("r", cl::aliasopt(RemappingFile));
cl::opt<std::string> OutputFilename("output", cl::value_desc("output"),
                                    cl::init("-"), cl::desc("Output file"));
cl::alias OutputFilenameA("o", cl::aliasopt(OutputFilename));

cl::opt<bool> WarnAmbiguous(
    "Wambiguous",
    cl::desc("Warn on equivalent symbols in the output symbol list"));
cl::opt<bool> WarnIncomplete(
    "Wincomplete",
    cl::desc("Warn on input symbols missing from output symbol list"));

static void warn(Twine Message, Twine Whence = "",
                 std::string Hint = "") {
  WithColor::warning();
  std::string WhenceStr = Whence.str();
  if (!WhenceStr.empty())
    errs() << WhenceStr << ": ";
  errs() << Message << "\n";
  if (!Hint.empty())
    WithColor::note() << Hint << "\n";
}

static void exitWithError(Twine Message, Twine Whence = "",
                          std::string Hint = "") {
  WithColor::error();
  std::string WhenceStr = Whence.str();
  if (!WhenceStr.empty())
    errs() << WhenceStr << ": ";
  errs() << Message << "\n";
  if (!Hint.empty())
    WithColor::note() << Hint << "\n";
  ::exit(1);
}

static void exitWithError(Error E, StringRef Whence = "") {
  exitWithError(toString(std::move(E)), Whence);
}

static void exitWithErrorCode(std::error_code EC, StringRef Whence = "") {
  exitWithError(EC.message(), Whence);
}

static void remapSymbols(MemoryBuffer &OldSymbolFile,
                         MemoryBuffer &NewSymbolFile,
                         MemoryBuffer &RemappingFile,
                         raw_ostream &Out) {
  // Load the remapping file and prepare to canonicalize symbols.
  SymbolRemappingReader Reader;
  if (Error E = Reader.read(RemappingFile))
    exitWithError(std::move(E));

  // Canonicalize the new symbols.
  DenseMap<SymbolRemappingReader::Key, StringRef> MappedNames;
  DenseSet<StringRef> UnparseableSymbols;
  for (line_iterator LineIt(NewSymbolFile, /*SkipBlanks=*/true, '#');
       !LineIt.is_at_eof(); ++LineIt) {
    StringRef Symbol = *LineIt;

    auto K = Reader.insert(Symbol);
    if (!K) {
      UnparseableSymbols.insert(Symbol);
      continue;
    }

    auto ItAndIsNew = MappedNames.insert({K, Symbol});
    if (WarnAmbiguous && !ItAndIsNew.second &&
        ItAndIsNew.first->second != Symbol) {
      warn("symbol " + Symbol + " is equivalent to earlier symbol " +
               ItAndIsNew.first->second,
           NewSymbolFile.getBufferIdentifier() + ":" +
               Twine(LineIt.line_number()),
           "later symbol will not be the target of any remappings");
    }
  }

  // Figure out which new symbol each old symbol is equivalent to.
  for (line_iterator LineIt(OldSymbolFile, /*SkipBlanks=*/true, '#');
       !LineIt.is_at_eof(); ++LineIt) {
    StringRef Symbol = *LineIt;

    auto K = Reader.lookup(Symbol);
    StringRef NewSymbol = MappedNames.lookup(K);

    if (NewSymbol.empty()) {
      if (WarnIncomplete && !UnparseableSymbols.count(Symbol)) {
        warn("no new symbol matches old symbol " + Symbol,
             OldSymbolFile.getBufferIdentifier() + ":" +
                 Twine(LineIt.line_number()));
      }
      continue;
    }

    Out << Symbol << " " << NewSymbol << "\n";
  }
}

int main(int argc, const char *argv[]) {
  InitLLVM X(argc, argv);

  cl::ParseCommandLineOptions(argc, argv, "LLVM C++ mangled name remapper\n");

  auto OldSymbolBufOrError = MemoryBuffer::getFileOrSTDIN(OldSymbolFile);
  if (!OldSymbolBufOrError)
    exitWithErrorCode(OldSymbolBufOrError.getError(), OldSymbolFile);

  auto NewSymbolBufOrError = MemoryBuffer::getFileOrSTDIN(NewSymbolFile);
  if (!NewSymbolBufOrError)
    exitWithErrorCode(NewSymbolBufOrError.getError(), NewSymbolFile);

  auto RemappingBufOrError = MemoryBuffer::getFileOrSTDIN(RemappingFile);
  if (!RemappingBufOrError)
    exitWithErrorCode(RemappingBufOrError.getError(), RemappingFile);

  std::error_code EC;
  raw_fd_ostream OS(OutputFilename.data(), EC, sys::fs::F_Text);
  if (EC)
    exitWithErrorCode(EC, OutputFilename);

  remapSymbols(*OldSymbolBufOrError.get(), *NewSymbolBufOrError.get(),
               *RemappingBufOrError.get(), OS);
}
