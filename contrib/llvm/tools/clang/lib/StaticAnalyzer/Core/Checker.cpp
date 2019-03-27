//== Checker.cpp - Registration mechanism for checkers -----------*- C++ -*--=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines Checker, used to create and register checkers.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/Checker.h"

using namespace clang;
using namespace ento;

int ImplicitNullDerefEvent::Tag;

StringRef CheckerBase::getTagDescription() const {
  return getCheckName().getName();
}

CheckName CheckerBase::getCheckName() const { return Name; }

CheckerProgramPointTag::CheckerProgramPointTag(StringRef CheckerName,
                                               StringRef Msg)
  : SimpleProgramPointTag(CheckerName, Msg) {}

CheckerProgramPointTag::CheckerProgramPointTag(const CheckerBase *Checker,
                                               StringRef Msg)
  : SimpleProgramPointTag(Checker->getCheckName().getName(), Msg) {}

raw_ostream& clang::ento::operator<<(raw_ostream &Out,
                                     const CheckerBase &Checker) {
  Out << Checker.getCheckName().getName();
  return Out;
}
