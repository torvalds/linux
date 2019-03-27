//==--- InterCheckerAPI.h ---------------------------------------*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// This file allows introduction of checker dependencies. It contains APIs for
// inter-checker communications.
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_INTERCHECKERAPI_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_INTERCHECKERAPI_H
namespace clang {
class CheckerManager;

namespace ento {

/// Register the checker which evaluates CString API calls.
void registerCStringCheckerBasic(CheckerManager &Mgr);

/// Register the part of MallocChecker connected to InnerPointerChecker.
void registerInnerPointerCheckerAux(CheckerManager &Mgr);

}}
#endif /* INTERCHECKERAPI_H_ */
