//===- ELFStub.cpp --------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===-----------------------------------------------------------------------===/

#include "llvm/TextAPI/ELF/ELFStub.h"

using namespace llvm;
using namespace llvm::elfabi;

ELFStub::ELFStub(ELFStub const &Stub) {
  TbeVersion = Stub.TbeVersion;
  Arch = Stub.Arch;
  SoName = Stub.SoName;
  NeededLibs = Stub.NeededLibs;
  Symbols = Stub.Symbols;
}

ELFStub::ELFStub(ELFStub &&Stub) {
  TbeVersion = std::move(Stub.TbeVersion);
  Arch = std::move(Stub.Arch);
  SoName = std::move(Stub.SoName);
  NeededLibs = std::move(Stub.NeededLibs);
  Symbols = std::move(Stub.Symbols);
}
