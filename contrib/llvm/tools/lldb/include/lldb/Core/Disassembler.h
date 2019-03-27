//===-- Disassembler.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Disassembler_h_
#define liblldb_Disassembler_h_

#include "lldb/Core/Address.h"
#include "lldb/Core/EmulateInstruction.h"
#include "lldb/Core/FormatEntity.h"
#include "lldb/Core/Opcode.h"
#include "lldb/Core/PluginInterface.h"
#include "lldb/Interpreter/OptionValue.h"
#include "lldb/Symbol/LineEntry.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-private-enumerations.h"
#include "lldb/lldb-types.h"

#include "llvm/ADT/StringRef.h"

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

namespace lldb_private {
class AddressRange;
}
namespace lldb_private {
class DataExtractor;
}
namespace lldb_private {
class Debugger;
}
namespace lldb_private {
class Disassembler;
}
namespace lldb_private {
class Module;
}
namespace lldb_private {
class Stream;
}
namespace lldb_private {
class SymbolContext;
}
namespace lldb_private {
class SymbolContextList;
}
namespace lldb_private {
class Target;
}
namespace lldb_private {
struct RegisterInfo;
}
namespace llvm {
template <typename T> class SmallVectorImpl;
}

namespace lldb_private {

class Instruction {
public:
  Instruction(const Address &address,
              AddressClass addr_class = AddressClass::eInvalid);

  virtual ~Instruction();

  const Address &GetAddress() const { return m_address; }

  const char *GetMnemonic(const ExecutionContext *exe_ctx) {
    CalculateMnemonicOperandsAndCommentIfNeeded(exe_ctx);
    return m_opcode_name.c_str();
  }

  const char *GetOperands(const ExecutionContext *exe_ctx) {
    CalculateMnemonicOperandsAndCommentIfNeeded(exe_ctx);
    return m_mnemonics.c_str();
  }

  const char *GetComment(const ExecutionContext *exe_ctx) {
    CalculateMnemonicOperandsAndCommentIfNeeded(exe_ctx);
    return m_comment.c_str();
  }

  virtual void
  CalculateMnemonicOperandsAndComment(const ExecutionContext *exe_ctx) = 0;

  AddressClass GetAddressClass();

  void SetAddress(const Address &addr) {
    // Invalidate the address class to lazily discover it if we need to.
    m_address_class = AddressClass::eInvalid;
    m_address = addr;
  }

  //------------------------------------------------------------------
  /// Dump the text representation of this Instruction to a Stream
  ///
  /// Print the (optional) address, (optional) bytes, opcode,
  /// operands, and instruction comments to a stream.
  ///
  /// @param[in] s
  ///     The Stream to add the text to.
  ///
  /// @param[in] show_address
  ///     Whether the address (using disassembly_addr_format_spec formatting)
  ///     should be printed.
  ///
  /// @param[in] show_bytes
  ///     Whether the bytes of the assembly instruction should be printed.
  ///
  /// @param[in] max_opcode_byte_size
  ///     The size (in bytes) of the largest instruction in the list that
  ///     we are printing (for text justification/alignment purposes)
  ///     Only needed if show_bytes is true.
  ///
  /// @param[in] exe_ctx
  ///     The current execution context, if available.  May be used in
  ///     the assembling of the operands+comments for this instruction.
  ///     Pass NULL if not applicable.
  ///
  /// @param[in] sym_ctx
  ///     The SymbolContext for this instruction.
  ///     Pass NULL if not available/computed.
  ///     Only needed if show_address is true.
  ///
  /// @param[in] prev_sym_ctx
  ///     The SymbolContext for the previous instruction.  Depending on
  ///     the disassembly address format specification, a change in
  ///     Symbol / Function may mean that a line is printed with the new
  ///     symbol/function name.
  ///     Pass NULL if unavailable, or if this is the first instruction of
  ///     the InstructionList.
  ///     Only needed if show_address is true.
  ///
  /// @param[in] disassembly_addr_format
  ///     The format specification for how addresses are printed.
  ///     Only needed if show_address is true.
  ///
  /// @param[in] max_address_text_size
  ///     The length of the longest address string at the start of the
  ///     disassembly line that will be printed (the
  ///     Debugger::FormatDisassemblerAddress() string)
  ///     so this method can properly align the instruction opcodes.
  ///     May be 0 to indicate no indentation/alignment of the opcodes.
  //------------------------------------------------------------------
  virtual void Dump(Stream *s, uint32_t max_opcode_byte_size, bool show_address,
                    bool show_bytes, const ExecutionContext *exe_ctx,
                    const SymbolContext *sym_ctx,
                    const SymbolContext *prev_sym_ctx,
                    const FormatEntity::Entry *disassembly_addr_format,
                    size_t max_address_text_size);

  virtual bool DoesBranch() = 0;

  virtual bool HasDelaySlot();

  bool CanSetBreakpoint ();

  virtual size_t Decode(const Disassembler &disassembler,
                        const DataExtractor &data,
                        lldb::offset_t data_offset) = 0;

  virtual void SetDescription(llvm::StringRef) {
  } // May be overridden in sub-classes that have descriptions.

  lldb::OptionValueSP ReadArray(FILE *in_file, Stream *out_stream,
                                OptionValue::Type data_type);

  lldb::OptionValueSP ReadDictionary(FILE *in_file, Stream *out_stream);

  bool DumpEmulation(const ArchSpec &arch);

  virtual bool TestEmulation(Stream *stream, const char *test_file_name);

  bool Emulate(const ArchSpec &arch, uint32_t evaluate_options, void *baton,
               EmulateInstruction::ReadMemoryCallback read_mem_callback,
               EmulateInstruction::WriteMemoryCallback write_mem_calback,
               EmulateInstruction::ReadRegisterCallback read_reg_callback,
               EmulateInstruction::WriteRegisterCallback write_reg_callback);

  const Opcode &GetOpcode() const { return m_opcode; }

  uint32_t GetData(DataExtractor &data);

  struct Operand {
    enum class Type {
      Invalid = 0,
      Register,
      Immediate,
      Dereference,
      Sum,
      Product
    } m_type = Type::Invalid;
    std::vector<Operand> m_children;
    lldb::addr_t m_immediate = 0;
    ConstString m_register;
    bool m_negative = false;
    bool m_clobbered = false;

    bool IsValid() { return m_type != Type::Invalid; }

    static Operand BuildRegister(ConstString &r);
    static Operand BuildImmediate(lldb::addr_t imm, bool neg);
    static Operand BuildImmediate(int64_t imm);
    static Operand BuildDereference(const Operand &ref);
    static Operand BuildSum(const Operand &lhs, const Operand &rhs);
    static Operand BuildProduct(const Operand &lhs, const Operand &rhs);
  };

  virtual bool ParseOperands(llvm::SmallVectorImpl<Operand> &operands) {
    return false;
  }

  virtual bool IsCall() { return false; }

protected:
  Address m_address; // The section offset address of this instruction
                     // We include an address class in the Instruction class to
                     // allow the instruction specify the
                     // AddressClass::eCodeAlternateISA (currently used for
                     // thumb), and also to specify data (AddressClass::eData).
                     // The usual value will be AddressClass::eCode, but often
                     // when disassembling memory, you might run into data.
                     // This can help us to disassemble appropriately.
private:
  AddressClass m_address_class; // Use GetAddressClass () accessor function!

protected:
  Opcode m_opcode; // The opcode for this instruction
  std::string m_opcode_name;
  std::string m_mnemonics;
  std::string m_comment;
  bool m_calculated_strings;

  void
  CalculateMnemonicOperandsAndCommentIfNeeded(const ExecutionContext *exe_ctx) {
    if (!m_calculated_strings) {
      m_calculated_strings = true;
      CalculateMnemonicOperandsAndComment(exe_ctx);
    }
  }
};

namespace OperandMatchers {
std::function<bool(const Instruction::Operand &)>
MatchBinaryOp(std::function<bool(const Instruction::Operand &)> base,
              std::function<bool(const Instruction::Operand &)> left,
              std::function<bool(const Instruction::Operand &)> right);

std::function<bool(const Instruction::Operand &)>
MatchUnaryOp(std::function<bool(const Instruction::Operand &)> base,
             std::function<bool(const Instruction::Operand &)> child);

std::function<bool(const Instruction::Operand &)>
MatchRegOp(const RegisterInfo &info);

std::function<bool(const Instruction::Operand &)> FetchRegOp(ConstString &reg);

std::function<bool(const Instruction::Operand &)> MatchImmOp(int64_t imm);

std::function<bool(const Instruction::Operand &)> FetchImmOp(int64_t &imm);

std::function<bool(const Instruction::Operand &)>
MatchOpType(Instruction::Operand::Type type);
}

class InstructionList {
public:
  InstructionList();
  ~InstructionList();

  size_t GetSize() const;

  uint32_t GetMaxOpcocdeByteSize() const;

  lldb::InstructionSP GetInstructionAtIndex(size_t idx) const;

  uint32_t GetIndexOfNextBranchInstruction(uint32_t start,
                                           Target &target) const;

  uint32_t GetIndexOfInstructionAtLoadAddress(lldb::addr_t load_addr,
                                              Target &target);

  uint32_t GetIndexOfInstructionAtAddress(const Address &addr);

  void Clear();

  void Append(lldb::InstructionSP &inst_sp);

  void Dump(Stream *s, bool show_address, bool show_bytes,
            const ExecutionContext *exe_ctx);

private:
  typedef std::vector<lldb::InstructionSP> collection;
  typedef collection::iterator iterator;
  typedef collection::const_iterator const_iterator;

  collection m_instructions;
};

class PseudoInstruction : public Instruction {
public:
  PseudoInstruction();

  ~PseudoInstruction() override;

  bool DoesBranch() override;

  bool HasDelaySlot() override;

  void CalculateMnemonicOperandsAndComment(
      const ExecutionContext *exe_ctx) override {
    // TODO: fill this in and put opcode name into Instruction::m_opcode_name,
    // mnemonic into Instruction::m_mnemonics, and any comment into
    // Instruction::m_comment
  }

  size_t Decode(const Disassembler &disassembler, const DataExtractor &data,
                lldb::offset_t data_offset) override;

  void SetOpcode(size_t opcode_size, void *opcode_data);

  void SetDescription(llvm::StringRef description) override;

protected:
  std::string m_description;

  DISALLOW_COPY_AND_ASSIGN(PseudoInstruction);
};

class Disassembler : public std::enable_shared_from_this<Disassembler>,
                     public PluginInterface {
public:
  enum {
    eOptionNone = 0u,
    eOptionShowBytes = (1u << 0),
    eOptionRawOuput = (1u << 1),
    eOptionMarkPCSourceLine = (1u << 2), // Mark the source line that contains
                                         // the current PC (mixed mode only)
    eOptionMarkPCAddress =
        (1u << 3) // Mark the disassembly line the contains the PC
  };

  enum HexImmediateStyle {
    eHexStyleC,
    eHexStyleAsm,
  };

  // FindPlugin should be lax about the flavor string (it is too annoying to
  // have various internal uses of the disassembler fail because the global
  // flavor string gets set wrong. Instead, if you get a flavor string you
  // don't understand, use the default.  Folks who care to check can use the
  // FlavorValidForArchSpec method on the disassembler they got back.
  static lldb::DisassemblerSP
  FindPlugin(const ArchSpec &arch, const char *flavor, const char *plugin_name);

  // This version will use the value in the Target settings if flavor is NULL;
  static lldb::DisassemblerSP
  FindPluginForTarget(const lldb::TargetSP target_sp, const ArchSpec &arch,
                      const char *flavor, const char *plugin_name);

  static lldb::DisassemblerSP
  DisassembleRange(const ArchSpec &arch, const char *plugin_name,
                   const char *flavor, const ExecutionContext &exe_ctx,
                   const AddressRange &disasm_range, bool prefer_file_cache);

  static lldb::DisassemblerSP
  DisassembleBytes(const ArchSpec &arch, const char *plugin_name,
                   const char *flavor, const Address &start, const void *bytes,
                   size_t length, uint32_t max_num_instructions,
                   bool data_from_file);

  static bool Disassemble(Debugger &debugger, const ArchSpec &arch,
                          const char *plugin_name, const char *flavor,
                          const ExecutionContext &exe_ctx,
                          const AddressRange &range, uint32_t num_instructions,
                          bool mixed_source_and_assembly,
                          uint32_t num_mixed_context_lines, uint32_t options,
                          Stream &strm);

  static bool Disassemble(Debugger &debugger, const ArchSpec &arch,
                          const char *plugin_name, const char *flavor,
                          const ExecutionContext &exe_ctx, const Address &start,
                          uint32_t num_instructions,
                          bool mixed_source_and_assembly,
                          uint32_t num_mixed_context_lines, uint32_t options,
                          Stream &strm);

  static size_t
  Disassemble(Debugger &debugger, const ArchSpec &arch, const char *plugin_name,
              const char *flavor, const ExecutionContext &exe_ctx,
              SymbolContextList &sc_list, uint32_t num_instructions,
              bool mixed_source_and_assembly, uint32_t num_mixed_context_lines,
              uint32_t options, Stream &strm);

  static bool
  Disassemble(Debugger &debugger, const ArchSpec &arch, const char *plugin_name,
              const char *flavor, const ExecutionContext &exe_ctx,
              const ConstString &name, Module *module,
              uint32_t num_instructions, bool mixed_source_and_assembly,
              uint32_t num_mixed_context_lines, uint32_t options, Stream &strm);

  static bool
  Disassemble(Debugger &debugger, const ArchSpec &arch, const char *plugin_name,
              const char *flavor, const ExecutionContext &exe_ctx,
              uint32_t num_instructions, bool mixed_source_and_assembly,
              uint32_t num_mixed_context_lines, uint32_t options, Stream &strm);

  //------------------------------------------------------------------
  // Constructors and Destructors
  //------------------------------------------------------------------
  Disassembler(const ArchSpec &arch, const char *flavor);
  ~Disassembler() override;

  typedef const char *(*SummaryCallback)(const Instruction &inst,
                                         ExecutionContext *exe_context,
                                         void *user_data);

  static bool PrintInstructions(Disassembler *disasm_ptr, Debugger &debugger,
                                const ArchSpec &arch,
                                const ExecutionContext &exe_ctx,
                                uint32_t num_instructions,
                                bool mixed_source_and_assembly,
                                uint32_t num_mixed_context_lines,
                                uint32_t options, Stream &strm);

  size_t ParseInstructions(const ExecutionContext *exe_ctx,
                           const AddressRange &range, Stream *error_strm_ptr,
                           bool prefer_file_cache);

  size_t ParseInstructions(const ExecutionContext *exe_ctx,
                           const Address &range, uint32_t num_instructions,
                           bool prefer_file_cache);

  virtual size_t DecodeInstructions(const Address &base_addr,
                                    const DataExtractor &data,
                                    lldb::offset_t data_offset,
                                    size_t num_instructions, bool append,
                                    bool data_from_file) = 0;

  InstructionList &GetInstructionList();

  const InstructionList &GetInstructionList() const;

  const ArchSpec &GetArchitecture() const { return m_arch; }

  const char *GetFlavor() const { return m_flavor.c_str(); }

  virtual bool FlavorValidForArchSpec(const lldb_private::ArchSpec &arch,
                                      const char *flavor) = 0;

protected:
  // SourceLine and SourceLinesToDisplay structures are only used in the mixed
  // source and assembly display methods internal to this class.

  struct SourceLine {
    FileSpec file;
    uint32_t line;
    uint32_t column;

    SourceLine() : file(), line(LLDB_INVALID_LINE_NUMBER), column(0) {}

    bool operator==(const SourceLine &rhs) const {
      return file == rhs.file && line == rhs.line && rhs.column == column;
    }

    bool operator!=(const SourceLine &rhs) const {
      return file != rhs.file || line != rhs.line || column != rhs.column;
    }

    bool IsValid() const { return line != LLDB_INVALID_LINE_NUMBER; }
  };

  struct SourceLinesToDisplay {
    std::vector<SourceLine> lines;

    // index of the "current" source line, if we want to highlight that when
    // displaying the source lines.  (as opposed to the surrounding source
    // lines provided to give context)
    size_t current_source_line;

    // Whether to print a blank line at the end of the source lines.
    bool print_source_context_end_eol;

    SourceLinesToDisplay()
        : lines(), current_source_line(-1), print_source_context_end_eol(true) {
    }
  };

  // Get the function's declaration line number, hopefully a line number
  // earlier than the opening curly brace at the start of the function body.
  static SourceLine GetFunctionDeclLineEntry(const SymbolContext &sc);

  // Add the provided SourceLine to the map of filenames-to-source-lines-seen.
  static void AddLineToSourceLineTables(
      SourceLine &line,
      std::map<FileSpec, std::set<uint32_t>> &source_lines_seen);

  // Given a source line, determine if we should print it when we're doing
  // mixed source & assembly output. We're currently using the
  // target.process.thread.step-avoid-regexp setting (which is used for
  // stepping over inlined STL functions by default) to determine what source
  // lines to avoid showing.
  //
  // Returns true if this source line should be elided (if the source line
  // should not be displayed).
  static bool
  ElideMixedSourceAndDisassemblyLine(const ExecutionContext &exe_ctx,
                                     const SymbolContext &sc, SourceLine &line);

  static bool
  ElideMixedSourceAndDisassemblyLine(const ExecutionContext &exe_ctx,
                                     const SymbolContext &sc, LineEntry &line) {
    SourceLine sl;
    sl.file = line.file;
    sl.line = line.line;
    sl.column = line.column;
    return ElideMixedSourceAndDisassemblyLine(exe_ctx, sc, sl);
  };

  //------------------------------------------------------------------
  // Classes that inherit from Disassembler can see and modify these
  //------------------------------------------------------------------
  ArchSpec m_arch;
  InstructionList m_instruction_list;
  lldb::addr_t m_base_addr;
  std::string m_flavor;

private:
  //------------------------------------------------------------------
  // For Disassembler only
  //------------------------------------------------------------------
  DISALLOW_COPY_AND_ASSIGN(Disassembler);
};

} // namespace lldb_private

#endif // liblldb_Disassembler_h_
