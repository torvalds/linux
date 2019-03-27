//=-- ubsan_signals_standalone.h
//------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Installs signal handlers and related interceptors for UBSan standalone.
//
//===----------------------------------------------------------------------===//

#ifndef UBSAN_SIGNALS_STANDALONE_H
#define UBSAN_SIGNALS_STANDALONE_H

namespace __ubsan {

// Initializes signal handlers and interceptors.
void InitializeDeadlySignals();

} // namespace __ubsan

#endif // UBSAN_SIGNALS_STANDALONE_H
