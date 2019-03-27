//===-- xray_defs.h ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Common definitions useful for XRay sources.
//
//===----------------------------------------------------------------------===//
#ifndef XRAY_XRAY_DEFS_H
#define XRAY_XRAY_DEFS_H

#if XRAY_SUPPORTED
#define XRAY_NEVER_INSTRUMENT __attribute__((xray_never_instrument))
#else
#define XRAY_NEVER_INSTRUMENT
#endif

#if SANITIZER_NETBSD
// NetBSD: thread_local is not aligned properly, and the code relying
// on it segfaults
#define XRAY_TLS_ALIGNAS(x)
#define XRAY_HAS_TLS_ALIGNAS 0
#else
#define XRAY_TLS_ALIGNAS(x) alignas(x)
#define XRAY_HAS_TLS_ALIGNAS 1
#endif

#endif  // XRAY_XRAY_DEFS_H
