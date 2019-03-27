//===-- llvm-symbolizer.cpp - Simple addr2line-like symbolizer ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This utility works much like "addr2line". It is able of transforming
// tuples (module name, module offset) to code locations (function name,
// file, line number, column number). It is targeted for compiler-rt tools
// (especially AddressSanitizer and ThreadSanitizer) that can use it
// to symbolize stack traces in their error reports.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/Symbolize/DIPrinter.h"
#include "llvm/DebugInfo/Symbolize/Symbolize.h"
#include "llvm/Support/COM.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdio>
#include <cstring>
#include <string>

using namespace llvm;
using namespace symbolize;

static cl::opt<bool>
ClUseSymbolTable("use-symbol-table", cl::init(true),
                 cl::desc("Prefer names in symbol table to names "
                          "in debug info"));

static cl::opt<FunctionNameKind> ClPrintFunctions(
    "functions", cl::init(FunctionNameKind::LinkageName),
    cl::desc("Print function name for a given address:"),
    cl::values(clEnumValN(FunctionNameKind::None, "none", "omit function name"),
               clEnumValN(FunctionNameKind::ShortName, "short",
                          "print short function name"),
               clEnumValN(FunctionNameKind::LinkageName, "linkage",
                          "print function linkage name")));

static cl::opt<bool>
    ClUseRelativeAddress("relative-address", cl::init(false),
                         cl::desc("Interpret addresses as relative addresses"),
                         cl::ReallyHidden);

static cl::opt<bool>
    ClPrintInlining("inlining", cl::init(true),
                    cl::desc("Print all inlined frames for a given address"));

// -demangle, -C
static cl::opt<bool>
ClDemangle("demangle", cl::init(true), cl::desc("Demangle function names"));
static cl::alias
ClDemangleShort("C", cl::desc("Alias for -demangle"),
                cl::NotHidden, cl::aliasopt(ClDemangle));

static cl::opt<std::string> ClDefaultArch("default-arch", cl::init(""),
                                          cl::desc("Default architecture "
                                                   "(for multi-arch objects)"));

// -obj, -exe, -e
static cl::opt<std::string>
ClBinaryName("obj", cl::init(""),
             cl::desc("Path to object file to be symbolized (if not provided, "
                      "object file should be specified for each input line)"));
static cl::alias
ClBinaryNameAliasExe("exe", cl::desc("Alias for -obj"),
                     cl::NotHidden, cl::aliasopt(ClBinaryName));
static cl::alias
ClBinaryNameAliasE("e", cl::desc("Alias for -obj"),
                   cl::NotHidden, cl::aliasopt(ClBinaryName));


static cl::opt<std::string>
    ClDwpName("dwp", cl::init(""),
              cl::desc("Path to DWP file to be use for any split CUs"));

static cl::list<std::string>
ClDsymHint("dsym-hint", cl::ZeroOrMore,
           cl::desc("Path to .dSYM bundles to search for debug info for the "
                    "object files"));

// -print-address, -addresses, -a
static cl::opt<bool>
ClPrintAddress("print-address", cl::init(false),
               cl::desc("Show address before line information"));
static cl::alias
ClPrintAddressAliasAddresses("addresses", cl::desc("Alias for -print-address"),
                             cl::NotHidden, cl::aliasopt(ClPrintAddress));
static cl::alias
ClPrintAddressAliasA("a", cl::desc("Alias for -print-address"),
                     cl::NotHidden, cl::aliasopt(ClPrintAddress));

// -pretty-print, -p
static cl::opt<bool>
    ClPrettyPrint("pretty-print", cl::init(false),
                  cl::desc("Make the output more human friendly"));
static cl::alias ClPrettyPrintShort("p", cl::desc("Alias for -pretty-print"),
                                    cl::NotHidden,
                                    cl::aliasopt(ClPrettyPrint));

static cl::opt<int> ClPrintSourceContextLines(
    "print-source-context-lines", cl::init(0),
    cl::desc("Print N number of source file context"));

static cl::opt<bool> ClVerbose("verbose", cl::init(false),
                               cl::desc("Print verbose line info"));

static cl::list<std::string> ClInputAddresses(cl::Positional,
                                              cl::desc("<input addresses>..."),
                                              cl::ZeroOrMore);

template<typename T>
static bool error(Expected<T> &ResOrErr) {
  if (ResOrErr)
    return false;
  logAllUnhandledErrors(ResOrErr.takeError(), errs(),
                        "LLVMSymbolizer: error reading file: ");
  return true;
}

static bool parseCommand(StringRef InputString, bool &IsData,
                         std::string &ModuleName, uint64_t &ModuleOffset) {
  const char kDelimiters[] = " \n\r";
  ModuleName = "";
  if (InputString.consume_front("CODE ")) {
    IsData = false;
  } else if (InputString.consume_front("DATA ")) {
    IsData = true;
  } else {
    // If no cmd, assume it's CODE.
    IsData = false;
  }
  const char *pos = InputString.data();
  // Skip delimiters and parse input filename (if needed).
  if (ClBinaryName.empty()) {
    pos += strspn(pos, kDelimiters);
    if (*pos == '"' || *pos == '\'') {
      char quote = *pos;
      pos++;
      const char *end = strchr(pos, quote);
      if (!end)
        return false;
      ModuleName = std::string(pos, end - pos);
      pos = end + 1;
    } else {
      int name_length = strcspn(pos, kDelimiters);
      ModuleName = std::string(pos, name_length);
      pos += name_length;
    }
  } else {
    ModuleName = ClBinaryName;
  }
  // Skip delimiters and parse module offset.
  pos += strspn(pos, kDelimiters);
  int offset_length = strcspn(pos, kDelimiters);
  return !StringRef(pos, offset_length).getAsInteger(0, ModuleOffset);
}

static void symbolizeInput(StringRef InputString, LLVMSymbolizer &Symbolizer,
                           DIPrinter &Printer) {
  bool IsData = false;
  std::string ModuleName;
  uint64_t ModuleOffset = 0;
  if (!parseCommand(StringRef(InputString), IsData, ModuleName, ModuleOffset)) {
    outs() << InputString;
    return;
  }

  if (ClPrintAddress) {
    outs() << "0x";
    outs().write_hex(ModuleOffset);
    StringRef Delimiter = ClPrettyPrint ? ": " : "\n";
    outs() << Delimiter;
  }
  if (IsData) {
    auto ResOrErr = Symbolizer.symbolizeData(ModuleName, ModuleOffset);
    Printer << (error(ResOrErr) ? DIGlobal() : ResOrErr.get());
  } else if (ClPrintInlining) {
    auto ResOrErr =
        Symbolizer.symbolizeInlinedCode(ModuleName, ModuleOffset, ClDwpName);
    Printer << (error(ResOrErr) ? DIInliningInfo() : ResOrErr.get());
  } else {
    auto ResOrErr =
        Symbolizer.symbolizeCode(ModuleName, ModuleOffset, ClDwpName);
    Printer << (error(ResOrErr) ? DILineInfo() : ResOrErr.get());
  }
  outs() << "\n";
  outs().flush();
}

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  llvm::sys::InitializeCOMRAII COM(llvm::sys::COMThreadingMode::MultiThreaded);

  cl::ParseCommandLineOptions(argc, argv, "llvm-symbolizer\n");
  LLVMSymbolizer::Options Opts(ClPrintFunctions, ClUseSymbolTable, ClDemangle,
                               ClUseRelativeAddress, ClDefaultArch);

  for (const auto &hint : ClDsymHint) {
    if (sys::path::extension(hint) == ".dSYM") {
      Opts.DsymHints.push_back(hint);
    } else {
      errs() << "Warning: invalid dSYM hint: \"" << hint <<
                "\" (must have the '.dSYM' extension).\n";
    }
  }
  LLVMSymbolizer Symbolizer(Opts);

  DIPrinter Printer(outs(), ClPrintFunctions != FunctionNameKind::None,
                    ClPrettyPrint, ClPrintSourceContextLines, ClVerbose);

  if (ClInputAddresses.empty()) {
    const int kMaxInputStringLength = 1024;
    char InputString[kMaxInputStringLength];

    while (fgets(InputString, sizeof(InputString), stdin))
      symbolizeInput(InputString, Symbolizer, Printer);
  } else {
    for (StringRef Address : ClInputAddresses)
      symbolizeInput(Address, Symbolizer, Printer);
  }

  return 0;
}
