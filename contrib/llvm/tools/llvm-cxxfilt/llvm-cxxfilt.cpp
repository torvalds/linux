//===-- llvm-c++filt.cpp --------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Demangle/Demangle.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdlib>
#include <iostream>

using namespace llvm;

enum Style {
  Auto,  ///< auto-detect mangling
  GNU,   ///< GNU
  Lucid, ///< Lucid compiler (lcc)
  ARM,
  HP,    ///< HP compiler (xCC)
  EDG,   ///< EDG compiler
  GNUv3, ///< GNU C++ v3 ABI
  Java,  ///< Java (gcj)
  GNAT   ///< ADA copiler (gnat)
};
static cl::opt<Style>
    Format("format", cl::desc("decoration style"),
           cl::values(clEnumValN(Auto, "auto", "auto-detect style"),
                      clEnumValN(GNU, "gnu", "GNU (itanium) style")),
           cl::init(Auto));
static cl::alias FormatShort("s", cl::desc("alias for --format"),
                             cl::aliasopt(Format));

static cl::opt<bool> StripUnderscore("strip-underscore",
                                     cl::desc("strip the leading underscore"),
                                     cl::init(false));
static cl::alias StripUnderscoreShort("_",
                                      cl::desc("alias for --strip-underscore"),
                                      cl::aliasopt(StripUnderscore));

static cl::opt<bool>
    Types("types",
          cl::desc("attempt to demangle types as well as function names"),
          cl::init(false));
static cl::alias TypesShort("t", cl::desc("alias for --types"),
                            cl::aliasopt(Types));

static cl::list<std::string>
Decorated(cl::Positional, cl::desc("<mangled>"), cl::ZeroOrMore);

static void demangle(llvm::raw_ostream &OS, const std::string &Mangled) {
  int Status;

  const char *Decorated = Mangled.c_str();
  if (StripUnderscore)
    if (Decorated[0] == '_')
      ++Decorated;
  size_t DecoratedLength = strlen(Decorated);

  char *Undecorated = nullptr;

  if (Types || ((DecoratedLength >= 2 && strncmp(Decorated, "_Z", 2) == 0) ||
                (DecoratedLength >= 4 && strncmp(Decorated, "___Z", 4) == 0)))
    Undecorated = itaniumDemangle(Decorated, nullptr, nullptr, &Status);

  if (!Undecorated &&
      (DecoratedLength > 6 && strncmp(Decorated, "__imp_", 6) == 0)) {
    OS << "import thunk for ";
    Undecorated = itaniumDemangle(Decorated + 6, nullptr, nullptr, &Status);
  }

  OS << (Undecorated ? Undecorated : Mangled) << '\n';
  OS.flush();

  free(Undecorated);
}

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  cl::ParseCommandLineOptions(argc, argv, "llvm symbol undecoration tool\n");

  if (Decorated.empty())
    for (std::string Mangled; std::getline(std::cin, Mangled);)
      demangle(llvm::outs(), Mangled);
  else
    for (const auto &Symbol : Decorated)
      demangle(llvm::outs(), Symbol);

  return EXIT_SUCCESS;
}
