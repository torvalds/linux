//===-- CodeGen/RuntimeLibcalls.h - Runtime Library Calls -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the enum representing the list of runtime library calls
// the backend may emit during code generation, and also some helper functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_RUNTIMELIBCALLS_H
#define LLVM_CODEGEN_RUNTIMELIBCALLS_H

#include "llvm/CodeGen/ValueTypes.h"

namespace llvm {
namespace RTLIB {
  /// RTLIB::Libcall enum - This enum defines all of the runtime library calls
  /// the backend can emit.  The various long double types cannot be merged,
  /// because 80-bit library functions use "xf" and 128-bit use "tf".
  ///
  /// When adding PPCF128 functions here, note that their names generally need
  /// to be overridden for Darwin with the xxx$LDBL128 form.  See
  /// PPCISelLowering.cpp.
  ///
  enum Libcall {
#define HANDLE_LIBCALL(code, name) code,
    #include "llvm/IR/RuntimeLibcalls.def"
#undef HANDLE_LIBCALL
  };

  /// getFPEXT - Return the FPEXT_*_* value for the given types, or
  /// UNKNOWN_LIBCALL if there is none.
  Libcall getFPEXT(EVT OpVT, EVT RetVT);

  /// getFPROUND - Return the FPROUND_*_* value for the given types, or
  /// UNKNOWN_LIBCALL if there is none.
  Libcall getFPROUND(EVT OpVT, EVT RetVT);

  /// getFPTOSINT - Return the FPTOSINT_*_* value for the given types, or
  /// UNKNOWN_LIBCALL if there is none.
  Libcall getFPTOSINT(EVT OpVT, EVT RetVT);

  /// getFPTOUINT - Return the FPTOUINT_*_* value for the given types, or
  /// UNKNOWN_LIBCALL if there is none.
  Libcall getFPTOUINT(EVT OpVT, EVT RetVT);

  /// getSINTTOFP - Return the SINTTOFP_*_* value for the given types, or
  /// UNKNOWN_LIBCALL if there is none.
  Libcall getSINTTOFP(EVT OpVT, EVT RetVT);

  /// getUINTTOFP - Return the UINTTOFP_*_* value for the given types, or
  /// UNKNOWN_LIBCALL if there is none.
  Libcall getUINTTOFP(EVT OpVT, EVT RetVT);

  /// Return the SYNC_FETCH_AND_* value for the given opcode and type, or
  /// UNKNOWN_LIBCALL if there is none.
  Libcall getSYNC(unsigned Opc, MVT VT);

  /// getMEMCPY_ELEMENT_UNORDERED_ATOMIC - Return
  /// MEMCPY_ELEMENT_UNORDERED_ATOMIC_* value for the given element size or
  /// UNKNOW_LIBCALL if there is none.
  Libcall getMEMCPY_ELEMENT_UNORDERED_ATOMIC(uint64_t ElementSize);

  /// getMEMMOVE_ELEMENT_UNORDERED_ATOMIC - Return
  /// MEMMOVE_ELEMENT_UNORDERED_ATOMIC_* value for the given element size or
  /// UNKNOW_LIBCALL if there is none.
  Libcall getMEMMOVE_ELEMENT_UNORDERED_ATOMIC(uint64_t ElementSize);

  /// getMEMSET_ELEMENT_UNORDERED_ATOMIC - Return
  /// MEMSET_ELEMENT_UNORDERED_ATOMIC_* value for the given element size or
  /// UNKNOW_LIBCALL if there is none.
  Libcall getMEMSET_ELEMENT_UNORDERED_ATOMIC(uint64_t ElementSize);

}
}

#endif
