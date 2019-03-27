//===-- sancov_flags.inc ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Sanitizer Coverage runtime flags.
//
//===----------------------------------------------------------------------===//
#ifndef SANCOV_FLAG
#error "Defnine SANCOV_FLAG prior to including this file!"
#endif

SANCOV_FLAG(bool, symbolize, true,
            "If set, converage information will be symbolized by sancov tool "
            "after dumping.")

SANCOV_FLAG(bool, help, false, "Print flags help.")
