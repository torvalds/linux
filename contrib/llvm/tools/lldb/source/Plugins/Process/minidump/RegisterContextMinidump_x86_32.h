//===-- RegisterContextMinidump_x86_32.h ------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_RegisterContextMinidump_x86_32_h_
#define liblldb_RegisterContextMinidump_x86_32_h_

#include "MinidumpTypes.h"

#include "Plugins/Process/Utility/RegisterInfoInterface.h"
#include "Plugins/Process/Utility/lldb-x86-register-enums.h"

#include "lldb/Target/RegisterContext.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/Support/Endian.h"

// C includes
// C++ includes

namespace lldb_private {

namespace minidump {

// This function receives an ArrayRef pointing to the bytes of the Minidump
// register context and returns a DataBuffer that's ordered by the offsets
// specified in the RegisterInfoInterface argument
// This way we can reuse the already existing register contexts
lldb::DataBufferSP
ConvertMinidumpContext_x86_32(llvm::ArrayRef<uint8_t> source_data,
                              RegisterInfoInterface *target_reg_interface);

// Reference: see breakpad/crashpad source or WinNT.h
struct MinidumpFloatingSaveAreaX86 {
  llvm::support::ulittle32_t control_word;
  llvm::support::ulittle32_t status_word;
  llvm::support::ulittle32_t tag_word;
  llvm::support::ulittle32_t error_offset;
  llvm::support::ulittle32_t error_selector;
  llvm::support::ulittle32_t data_offset;
  llvm::support::ulittle32_t data_selector;

  enum {
    RegisterAreaSize = 80,
  };
  // register_area contains eight 80-bit (x87 "long double") quantities for
  // floating-point registers %st0 (%mm0) through %st7 (%mm7).
  uint8_t register_area[RegisterAreaSize];
  llvm::support::ulittle32_t cr0_npx_state;
};

struct MinidumpContext_x86_32 {
  // The context_flags field determines which parts
  // of the structure are populated (have valid values)
  llvm::support::ulittle32_t context_flags;

  // The next 6 registers are included with
  // MinidumpContext_x86_32_Flags::DebugRegisters
  llvm::support::ulittle32_t dr0;
  llvm::support::ulittle32_t dr1;
  llvm::support::ulittle32_t dr2;
  llvm::support::ulittle32_t dr3;
  llvm::support::ulittle32_t dr6;
  llvm::support::ulittle32_t dr7;

  // The next field is included with
  // MinidumpContext_x86_32_Flags::FloatingPoint
  MinidumpFloatingSaveAreaX86 float_save;

  // The next 4 registers are included with
  // MinidumpContext_x86_32_Flags::Segments
  llvm::support::ulittle32_t gs;
  llvm::support::ulittle32_t fs;
  llvm::support::ulittle32_t es;
  llvm::support::ulittle32_t ds;

  // The next 6 registers are included with
  // MinidumpContext_x86_32_Flags::Integer
  llvm::support::ulittle32_t edi;
  llvm::support::ulittle32_t esi;
  llvm::support::ulittle32_t ebx;
  llvm::support::ulittle32_t edx;
  llvm::support::ulittle32_t ecx;
  llvm::support::ulittle32_t eax;

  // The next 6 registers are included with
  // MinidumpContext_x86_32_Flags::Control
  llvm::support::ulittle32_t ebp;
  llvm::support::ulittle32_t eip;
  llvm::support::ulittle32_t cs;     // WinNT.h says "must be sanitized"
  llvm::support::ulittle32_t eflags; // WinNT.h says "must be sanitized"
  llvm::support::ulittle32_t esp;
  llvm::support::ulittle32_t ss;

  // The next field is included with
  // MinidumpContext_x86_32_Flags::ExtendedRegisters
  // It contains vector (MMX/SSE) registers.  It it laid out in the
  // format used by the fxsave and fsrstor instructions, so it includes
  // a copy of the x87 floating-point registers as well.  See FXSAVE in
  // "Intel Architecture Software Developer's Manual, Volume 2."
  enum {
    ExtendedRegistersSize = 512,
  };
  uint8_t extended_registers[ExtendedRegistersSize];
};

LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();

// For context_flags. These values indicate the type of
// context stored in the structure. The high 24 bits identify the CPU, the
// low 8 bits identify the type of context saved.
enum class MinidumpContext_x86_32_Flags : uint32_t {
  x86_32_Flag = 0x00010000, // CONTEXT_i386, CONTEXT_i486
  Control = x86_32_Flag | 0x00000001,
  Integer = x86_32_Flag | 0x00000002,
  Segments = x86_32_Flag | 0x00000004,
  FloatingPoint = x86_32_Flag | 0x00000008,
  DebugRegisters = x86_32_Flag | 0x00000010,
  ExtendedRegisters = x86_32_Flag | 0x00000020,
  XState = x86_32_Flag | 0x00000040,

  Full = Control | Integer | Segments,
  All = Full | FloatingPoint | DebugRegisters | ExtendedRegisters,

  LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ All)
};

} // end namespace minidump
} // end namespace lldb_private
#endif // liblldb_RegisterContextMinidump_x86_32_h_
