//===- llvm/Support/Unix/COM.inc - Unix COM Implementation -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Unix portion of COM support.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//=== WARNING: Implementation here must contain only generic UNIX code that
//===          is guaranteed to work on *all* UNIX variants.
//===----------------------------------------------------------------------===//

namespace llvm {
namespace sys {

InitializeCOMRAII::InitializeCOMRAII(COMThreadingMode Threading,
                                     bool SpeedOverMemory) {}

InitializeCOMRAII::~InitializeCOMRAII() {}
}
}
