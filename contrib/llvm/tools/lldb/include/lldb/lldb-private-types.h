//===-- lldb-private-types.h ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_lldb_private_types_h_
#define liblldb_lldb_private_types_h_

#if defined(__cplusplus)

#include "lldb/lldb-private.h"

#include "llvm/ADT/ArrayRef.h"

namespace llvm {
namespace sys {
class DynamicLibrary;
}
}

namespace lldb_private {
class Platform;
class ExecutionContext;

typedef llvm::sys::DynamicLibrary (*LoadPluginCallbackType)(
    const lldb::DebuggerSP &debugger_sp, const FileSpec &spec, Status &error);

//----------------------------------------------------------------------
// Every register is described in detail including its name, alternate name
// (optional), encoding, size in bytes and the default display format.
//----------------------------------------------------------------------
struct RegisterInfo {
  const char *name;     // Name of this register, can't be NULL
  const char *alt_name; // Alternate name of this register, can be NULL
  uint32_t byte_size;   // Size in bytes of the register
  uint32_t byte_offset; // The byte offset in the register context data where
                        // this register's value is found.
  // This is optional, and can be 0 if a particular RegisterContext does not
  // need to address its registers by byte offset.
  lldb::Encoding encoding;                 // Encoding of the register bits
  lldb::Format format;                     // Default display format
  uint32_t kinds[lldb::kNumRegisterKinds]; // Holds all of the various register
                                           // numbers for all register kinds
  uint32_t *value_regs;                    // List of registers (terminated with
                        // LLDB_INVALID_REGNUM).  If this value is not null,
                        // all registers in this list will be read first, at
                        // which point the value for this register will be
                        // valid.  For example, the value list for ah would be
                        // eax (x86) or rax (x64).
  uint32_t *invalidate_regs; // List of registers (terminated with
                             // LLDB_INVALID_REGNUM).  If this value is not
                             // null, all registers in this list will be
                             // invalidated when the value of this register
                             // changes.  For example, the invalidate list for
                             // eax would be rax ax, ah, and al.
  const uint8_t *dynamic_size_dwarf_expr_bytes; // A DWARF expression that when
                                                // evaluated gives
  // the byte size of this register.
  size_t dynamic_size_dwarf_len; // The length of the DWARF expression in bytes
                                 // in the dynamic_size_dwarf_expr_bytes
                                 // member.

  llvm::ArrayRef<uint8_t> data(const uint8_t *context_base) const {
    return llvm::ArrayRef<uint8_t>(context_base + byte_offset, byte_size);
  }

  llvm::MutableArrayRef<uint8_t> mutable_data(uint8_t *context_base) const {
    return llvm::MutableArrayRef<uint8_t>(context_base + byte_offset,
                                          byte_size);
  }
};

//----------------------------------------------------------------------
// Registers are grouped into register sets
//----------------------------------------------------------------------
struct RegisterSet {
  const char *name;          // Name of this register set
  const char *short_name;    // A short name for this register set
  size_t num_registers;      // The number of registers in REGISTERS array below
  const uint32_t *registers; // An array of register indices in this set.  The
                             // values in this array are
  // *indices* (not register numbers) into a particular RegisterContext's
  // register array.  For example, if eax is defined at index 4 for a
  // particular RegisterContext, eax would be included in this RegisterSet by
  // adding the value 4.  Not by adding the value lldb_eax_i386.
};

struct OptionEnumValueElement {
  int64_t value;
  const char *string_value;
  const char *usage;
};

using OptionEnumValues = llvm::ArrayRef<OptionEnumValueElement>;

struct OptionValidator {
  virtual ~OptionValidator() {}
  virtual bool IsValid(Platform &platform,
                       const ExecutionContext &target) const = 0;
  virtual const char *ShortConditionString() const = 0;
  virtual const char *LongConditionString() const = 0;
};

struct OptionDefinition {
  uint32_t usage_mask; // Used to mark options that can be used together.  If (1
                       // << n & usage_mask) != 0
                       // then this option belongs to option set n.
  bool required;       // This option is required (in the current usage level)
  const char *long_option; // Full name for this option.
  int short_option;        // Single character for this option.
  int option_has_arg; // no_argument, required_argument or optional_argument
  OptionValidator *validator; // If non-NULL, option is valid iff
                              // |validator->IsValid()|, otherwise always valid.
  OptionEnumValues enum_values; // If not empty, an array of enum values.
  uint32_t completion_type; // Cookie the option class can use to do define the
                            // argument completion.
  lldb::CommandArgumentType argument_type; // Type of argument this option takes
  const char *usage_text; // Full text explaining what this options does and
                          // what (if any) argument to
                          // pass it.
};

typedef struct type128 { uint64_t x[2]; } type128;
typedef struct type256 { uint64_t x[4]; } type256;

} // namespace lldb_private

#endif // #if defined(__cplusplus)

#endif // liblldb_lldb_private_types_h_
