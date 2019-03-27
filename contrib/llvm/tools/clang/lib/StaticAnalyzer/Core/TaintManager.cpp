//== TaintManager.cpp ------------------------------------------ -*- C++ -*--=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/PathSensitive/TaintManager.h"

using namespace clang;
using namespace ento;

void *ProgramStateTrait<TaintMap>::GDMIndex() {
  static int index = 0;
  return &index;
}

void *ProgramStateTrait<DerivedSymTaint>::GDMIndex() {
  static int index;
  return &index;
}
