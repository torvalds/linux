//===-- llvm/Support/Win64EH.h ---Win64 EH Constants-------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains constants and structures used for implementing
// exception handling on Win64 platforms. For more information, see
// http://msdn.microsoft.com/en-us/library/1eyas8tf.aspx
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_WIN64EH_H
#define LLVM_SUPPORT_WIN64EH_H

#include "llvm/Support/DataTypes.h"
#include "llvm/Support/Endian.h"

namespace llvm {
namespace Win64EH {

/// UnwindOpcodes - Enumeration whose values specify a single operation in
/// the prolog of a function.
enum UnwindOpcodes {
  UOP_PushNonVol = 0,
  UOP_AllocLarge,
  UOP_AllocSmall,
  UOP_SetFPReg,
  UOP_SaveNonVol,
  UOP_SaveNonVolBig,
  UOP_SaveXMM128 = 8,
  UOP_SaveXMM128Big,
  UOP_PushMachFrame,
  // The following set of unwind opcodes is for ARM64.  They are documented at
  // https://docs.microsoft.com/en-us/cpp/build/arm64-exception-handling
  UOP_AllocMedium,
  UOP_SaveFPLRX,
  UOP_SaveFPLR,
  UOP_SaveReg,
  UOP_SaveRegX,
  UOP_SaveRegP,
  UOP_SaveRegPX,
  UOP_SaveFReg,
  UOP_SaveFRegX,
  UOP_SaveFRegP,
  UOP_SaveFRegPX,
  UOP_SetFP,
  UOP_AddFP,
  UOP_Nop,
  UOP_End
};

/// UnwindCode - This union describes a single operation in a function prolog,
/// or part thereof.
union UnwindCode {
  struct {
    uint8_t CodeOffset;
    uint8_t UnwindOpAndOpInfo;
  } u;
  support::ulittle16_t FrameOffset;

  uint8_t getUnwindOp() const {
    return u.UnwindOpAndOpInfo & 0x0F;
  }
  uint8_t getOpInfo() const {
    return (u.UnwindOpAndOpInfo >> 4) & 0x0F;
  }
};

enum {
  /// UNW_ExceptionHandler - Specifies that this function has an exception
  /// handler.
  UNW_ExceptionHandler = 0x01,
  /// UNW_TerminateHandler - Specifies that this function has a termination
  /// handler.
  UNW_TerminateHandler = 0x02,
  /// UNW_ChainInfo - Specifies that this UnwindInfo structure is chained to
  /// another one.
  UNW_ChainInfo = 0x04
};

/// RuntimeFunction - An entry in the table of functions with unwind info.
struct RuntimeFunction {
  support::ulittle32_t StartAddress;
  support::ulittle32_t EndAddress;
  support::ulittle32_t UnwindInfoOffset;
};

/// UnwindInfo - An entry in the exception table.
struct UnwindInfo {
  uint8_t VersionAndFlags;
  uint8_t PrologSize;
  uint8_t NumCodes;
  uint8_t FrameRegisterAndOffset;
  UnwindCode UnwindCodes[1];

  uint8_t getVersion() const {
    return VersionAndFlags & 0x07;
  }
  uint8_t getFlags() const {
    return (VersionAndFlags >> 3) & 0x1f;
  }
  uint8_t getFrameRegister() const {
    return FrameRegisterAndOffset & 0x0f;
  }
  uint8_t getFrameOffset() const {
    return (FrameRegisterAndOffset >> 4) & 0x0f;
  }

  // The data after unwindCodes depends on flags.
  // If UNW_ExceptionHandler or UNW_TerminateHandler is set then follows
  // the address of the language-specific exception handler.
  // If UNW_ChainInfo is set then follows a RuntimeFunction which defines
  // the chained unwind info.
  // For more information please see MSDN at:
  // http://msdn.microsoft.com/en-us/library/ddssxxy8.aspx

  /// Return pointer to language specific data part of UnwindInfo.
  void *getLanguageSpecificData() {
    return reinterpret_cast<void *>(&UnwindCodes[(NumCodes+1) & ~1]);
  }

  /// Return pointer to language specific data part of UnwindInfo.
  const void *getLanguageSpecificData() const {
    return reinterpret_cast<const void *>(&UnwindCodes[(NumCodes + 1) & ~1]);
  }

  /// Return image-relative offset of language-specific exception handler.
  uint32_t getLanguageSpecificHandlerOffset() const {
    return *reinterpret_cast<const support::ulittle32_t *>(
               getLanguageSpecificData());
  }

  /// Set image-relative offset of language-specific exception handler.
  void setLanguageSpecificHandlerOffset(uint32_t offset) {
    *reinterpret_cast<support::ulittle32_t *>(getLanguageSpecificData()) =
        offset;
  }

  /// Return pointer to exception-specific data.
  void *getExceptionData() {
    return reinterpret_cast<void *>(reinterpret_cast<uint32_t *>(
                                                  getLanguageSpecificData())+1);
  }

  /// Return pointer to chained unwind info.
  RuntimeFunction *getChainedFunctionEntry() {
    return reinterpret_cast<RuntimeFunction *>(getLanguageSpecificData());
  }

  /// Return pointer to chained unwind info.
  const RuntimeFunction *getChainedFunctionEntry() const {
    return reinterpret_cast<const RuntimeFunction *>(getLanguageSpecificData());
  }
};


} // End of namespace Win64EH
} // End of namespace llvm

#endif
