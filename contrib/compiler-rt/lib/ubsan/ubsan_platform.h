//===-- ubsan_platform.h ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Defines the platforms which UBSan is supported at.
//
//===----------------------------------------------------------------------===//
#ifndef UBSAN_PLATFORM_H
#define UBSAN_PLATFORM_H

// Other platforms should be easy to add, and probably work as-is.
#if defined(__linux__) || defined(__FreeBSD__) || defined(__APPLE__) ||        \
    defined(__NetBSD__) || defined(__OpenBSD__) || \
    (defined(__sun__) && defined(__svr4__)) || \
    defined(_WIN32) || defined(__Fuchsia__) || defined(__rtems__)
# define CAN_SANITIZE_UB 1
#else
# define CAN_SANITIZE_UB 0
#endif

#endif
