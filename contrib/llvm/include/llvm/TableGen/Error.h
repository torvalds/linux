//===- llvm/TableGen/Error.h - tblgen error handling helpers ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains error handling helper routines to pretty-print diagnostic
// messages from tblgen.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TABLEGEN_ERROR_H
#define LLVM_TABLEGEN_ERROR_H

#include "llvm/Support/SourceMgr.h"

namespace llvm {

void PrintNote(ArrayRef<SMLoc> NoteLoc, const Twine &Msg);

void PrintWarning(ArrayRef<SMLoc> WarningLoc, const Twine &Msg);
void PrintWarning(const char *Loc, const Twine &Msg);
void PrintWarning(const Twine &Msg);

void PrintError(ArrayRef<SMLoc> ErrorLoc, const Twine &Msg);
void PrintError(const char *Loc, const Twine &Msg);
void PrintError(const Twine &Msg);

LLVM_ATTRIBUTE_NORETURN void PrintFatalError(const Twine &Msg);
LLVM_ATTRIBUTE_NORETURN void PrintFatalError(ArrayRef<SMLoc> ErrorLoc,
                                             const Twine &Msg);

extern SourceMgr SrcMgr;
extern unsigned ErrorsPrinted;

} // end namespace "llvm"

#endif
