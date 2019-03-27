//===-- xray_interface_internal.h -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a dynamic runtime instrumentation system.
//
// Implementation of the API functions. See also include/xray/xray_interface.h.
//
//===----------------------------------------------------------------------===//
#ifndef XRAY_INTERFACE_INTERNAL_H
#define XRAY_INTERFACE_INTERNAL_H

#include "sanitizer_common/sanitizer_platform.h"
#include "xray/xray_interface.h"
#include <cstddef>
#include <cstdint>

extern "C" {

struct XRaySledEntry {
#if SANITIZER_WORDSIZE == 64
  uint64_t Address;
  uint64_t Function;
  unsigned char Kind;
  unsigned char AlwaysInstrument;
  unsigned char Version;
  unsigned char Padding[13]; // Need 32 bytes
#elif SANITIZER_WORDSIZE == 32
  uint32_t Address;
  uint32_t Function;
  unsigned char Kind;
  unsigned char AlwaysInstrument;
  unsigned char Version;
  unsigned char Padding[5]; // Need 16 bytes
#else
#error "Unsupported word size."
#endif
};

struct XRayFunctionSledIndex {
  const XRaySledEntry *Begin;
  const XRaySledEntry *End;
};
}

namespace __xray {

struct XRaySledMap {
  const XRaySledEntry *Sleds;
  size_t Entries;
  const XRayFunctionSledIndex *SledsIndex;
  size_t Functions;
};

bool patchFunctionEntry(bool Enable, uint32_t FuncId, const XRaySledEntry &Sled,
                        void (*Trampoline)());
bool patchFunctionExit(bool Enable, uint32_t FuncId, const XRaySledEntry &Sled);
bool patchFunctionTailExit(bool Enable, uint32_t FuncId,
                           const XRaySledEntry &Sled);
bool patchCustomEvent(bool Enable, uint32_t FuncId, const XRaySledEntry &Sled);
bool patchTypedEvent(bool Enable, uint32_t FuncId, const XRaySledEntry &Sled);

} // namespace __xray

extern "C" {
// The following functions have to be defined in assembler, on a per-platform
// basis. See xray_trampoline_*.S files for implementations.
extern void __xray_FunctionEntry();
extern void __xray_FunctionExit();
extern void __xray_FunctionTailExit();
extern void __xray_ArgLoggerEntry();
extern void __xray_CustomEvent();
extern void __xray_TypedEvent();
}

#endif
