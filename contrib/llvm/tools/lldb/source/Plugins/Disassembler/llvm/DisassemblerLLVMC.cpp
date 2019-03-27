//===-- DisassemblerLLVMC.cpp -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "DisassemblerLLVMC.h"

#include "llvm-c/Disassembler.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCDisassembler/MCExternalSymbolizer.h"
#include "llvm/MC/MCDisassembler/MCRelocationInfo.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"

#include "lldb/Core/Address.h"
#include "lldb/Core/Module.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

class DisassemblerLLVMC::MCDisasmInstance {
public:
  static std::unique_ptr<MCDisasmInstance>
  Create(const char *triple, const char *cpu, const char *features_str,
         unsigned flavor, DisassemblerLLVMC &owner);

  ~MCDisasmInstance() = default;

  uint64_t GetMCInst(const uint8_t *opcode_data, size_t opcode_data_len,
                     lldb::addr_t pc, llvm::MCInst &mc_inst) const;
  void PrintMCInst(llvm::MCInst &mc_inst, std::string &inst_string,
                   std::string &comments_string);
  void SetStyle(bool use_hex_immed, HexImmediateStyle hex_style);
  bool CanBranch(llvm::MCInst &mc_inst) const;
  bool HasDelaySlot(llvm::MCInst &mc_inst) const;
  bool IsCall(llvm::MCInst &mc_inst) const;

private:
  MCDisasmInstance(std::unique_ptr<llvm::MCInstrInfo> &&instr_info_up,
                   std::unique_ptr<llvm::MCRegisterInfo> &&reg_info_up,
                   std::unique_ptr<llvm::MCSubtargetInfo> &&subtarget_info_up,
                   std::unique_ptr<llvm::MCAsmInfo> &&asm_info_up,
                   std::unique_ptr<llvm::MCContext> &&context_up,
                   std::unique_ptr<llvm::MCDisassembler> &&disasm_up,
                   std::unique_ptr<llvm::MCInstPrinter> &&instr_printer_up);

  std::unique_ptr<llvm::MCInstrInfo> m_instr_info_up;
  std::unique_ptr<llvm::MCRegisterInfo> m_reg_info_up;
  std::unique_ptr<llvm::MCSubtargetInfo> m_subtarget_info_up;
  std::unique_ptr<llvm::MCAsmInfo> m_asm_info_up;
  std::unique_ptr<llvm::MCContext> m_context_up;
  std::unique_ptr<llvm::MCDisassembler> m_disasm_up;
  std::unique_ptr<llvm::MCInstPrinter> m_instr_printer_up;
};

class InstructionLLVMC : public lldb_private::Instruction {
public:
  InstructionLLVMC(DisassemblerLLVMC &disasm,
                   const lldb_private::Address &address,
                   AddressClass addr_class)
      : Instruction(address, addr_class),
        m_disasm_wp(std::static_pointer_cast<DisassemblerLLVMC>(
            disasm.shared_from_this())),
        m_does_branch(eLazyBoolCalculate), m_has_delay_slot(eLazyBoolCalculate),
        m_is_call(eLazyBoolCalculate), m_is_valid(false),
        m_using_file_addr(false) {}

  ~InstructionLLVMC() override = default;

  bool DoesBranch() override {
    if (m_does_branch == eLazyBoolCalculate) {
      DisassemblerScope disasm(*this);
      if (disasm) {
        DataExtractor data;
        if (m_opcode.GetData(data)) {
          bool is_alternate_isa;
          lldb::addr_t pc = m_address.GetFileAddress();

          DisassemblerLLVMC::MCDisasmInstance *mc_disasm_ptr =
              GetDisasmToUse(is_alternate_isa, disasm);
          const uint8_t *opcode_data = data.GetDataStart();
          const size_t opcode_data_len = data.GetByteSize();
          llvm::MCInst inst;
          const size_t inst_size =
              mc_disasm_ptr->GetMCInst(opcode_data, opcode_data_len, pc, inst);
          // Be conservative, if we didn't understand the instruction, say it
          // might branch...
          if (inst_size == 0)
            m_does_branch = eLazyBoolYes;
          else {
            const bool can_branch = mc_disasm_ptr->CanBranch(inst);
            if (can_branch)
              m_does_branch = eLazyBoolYes;
            else
              m_does_branch = eLazyBoolNo;
          }
        }
      }
    }
    return m_does_branch == eLazyBoolYes;
  }

  bool HasDelaySlot() override {
    if (m_has_delay_slot == eLazyBoolCalculate) {
      DisassemblerScope disasm(*this);
      if (disasm) {
        DataExtractor data;
        if (m_opcode.GetData(data)) {
          bool is_alternate_isa;
          lldb::addr_t pc = m_address.GetFileAddress();

          DisassemblerLLVMC::MCDisasmInstance *mc_disasm_ptr =
              GetDisasmToUse(is_alternate_isa, disasm);
          const uint8_t *opcode_data = data.GetDataStart();
          const size_t opcode_data_len = data.GetByteSize();
          llvm::MCInst inst;
          const size_t inst_size =
              mc_disasm_ptr->GetMCInst(opcode_data, opcode_data_len, pc, inst);
          // if we didn't understand the instruction, say it doesn't have a
          // delay slot...
          if (inst_size == 0)
            m_has_delay_slot = eLazyBoolNo;
          else {
            const bool has_delay_slot = mc_disasm_ptr->HasDelaySlot(inst);
            if (has_delay_slot)
              m_has_delay_slot = eLazyBoolYes;
            else
              m_has_delay_slot = eLazyBoolNo;
          }
        }
      }
    }
    return m_has_delay_slot == eLazyBoolYes;
  }

  DisassemblerLLVMC::MCDisasmInstance *GetDisasmToUse(bool &is_alternate_isa) {
    DisassemblerScope disasm(*this);
    return GetDisasmToUse(is_alternate_isa, disasm);
  }

  size_t Decode(const lldb_private::Disassembler &disassembler,
                const lldb_private::DataExtractor &data,
                lldb::offset_t data_offset) override {
    // All we have to do is read the opcode which can be easy for some
    // architectures
    bool got_op = false;
    DisassemblerScope disasm(*this);
    if (disasm) {
      const ArchSpec &arch = disasm->GetArchitecture();
      const lldb::ByteOrder byte_order = data.GetByteOrder();

      const uint32_t min_op_byte_size = arch.GetMinimumOpcodeByteSize();
      const uint32_t max_op_byte_size = arch.GetMaximumOpcodeByteSize();
      if (min_op_byte_size == max_op_byte_size) {
        // Fixed size instructions, just read that amount of data.
        if (!data.ValidOffsetForDataOfSize(data_offset, min_op_byte_size))
          return false;

        switch (min_op_byte_size) {
        case 1:
          m_opcode.SetOpcode8(data.GetU8(&data_offset), byte_order);
          got_op = true;
          break;

        case 2:
          m_opcode.SetOpcode16(data.GetU16(&data_offset), byte_order);
          got_op = true;
          break;

        case 4:
          m_opcode.SetOpcode32(data.GetU32(&data_offset), byte_order);
          got_op = true;
          break;

        case 8:
          m_opcode.SetOpcode64(data.GetU64(&data_offset), byte_order);
          got_op = true;
          break;

        default:
          m_opcode.SetOpcodeBytes(data.PeekData(data_offset, min_op_byte_size),
                                  min_op_byte_size);
          got_op = true;
          break;
        }
      }
      if (!got_op) {
        bool is_alternate_isa = false;
        DisassemblerLLVMC::MCDisasmInstance *mc_disasm_ptr =
            GetDisasmToUse(is_alternate_isa, disasm);

        const llvm::Triple::ArchType machine = arch.GetMachine();
        if (machine == llvm::Triple::arm || machine == llvm::Triple::thumb) {
          if (machine == llvm::Triple::thumb || is_alternate_isa) {
            uint32_t thumb_opcode = data.GetU16(&data_offset);
            if ((thumb_opcode & 0xe000) != 0xe000 ||
                ((thumb_opcode & 0x1800u) == 0)) {
              m_opcode.SetOpcode16(thumb_opcode, byte_order);
              m_is_valid = true;
            } else {
              thumb_opcode <<= 16;
              thumb_opcode |= data.GetU16(&data_offset);
              m_opcode.SetOpcode16_2(thumb_opcode, byte_order);
              m_is_valid = true;
            }
          } else {
            m_opcode.SetOpcode32(data.GetU32(&data_offset), byte_order);
            m_is_valid = true;
          }
        } else {
          // The opcode isn't evenly sized, so we need to actually use the llvm
          // disassembler to parse it and get the size.
          uint8_t *opcode_data =
              const_cast<uint8_t *>(data.PeekData(data_offset, 1));
          const size_t opcode_data_len = data.BytesLeft(data_offset);
          const addr_t pc = m_address.GetFileAddress();
          llvm::MCInst inst;

          const size_t inst_size =
              mc_disasm_ptr->GetMCInst(opcode_data, opcode_data_len, pc, inst);
          if (inst_size == 0)
            m_opcode.Clear();
          else {
            m_opcode.SetOpcodeBytes(opcode_data, inst_size);
            m_is_valid = true;
          }
        }
      }
      return m_opcode.GetByteSize();
    }
    return 0;
  }

  void AppendComment(std::string &description) {
    if (m_comment.empty())
      m_comment.swap(description);
    else {
      m_comment.append(", ");
      m_comment.append(description);
    }
  }

  void CalculateMnemonicOperandsAndComment(
      const lldb_private::ExecutionContext *exe_ctx) override {
    DataExtractor data;
    const AddressClass address_class = GetAddressClass();

    if (m_opcode.GetData(data)) {
      std::string out_string;
      std::string comment_string;

      DisassemblerScope disasm(*this, exe_ctx);
      if (disasm) {
        DisassemblerLLVMC::MCDisasmInstance *mc_disasm_ptr;

        if (address_class == AddressClass::eCodeAlternateISA)
          mc_disasm_ptr = disasm->m_alternate_disasm_up.get();
        else
          mc_disasm_ptr = disasm->m_disasm_up.get();

        lldb::addr_t pc = m_address.GetFileAddress();
        m_using_file_addr = true;

        const bool data_from_file = disasm->m_data_from_file;
        bool use_hex_immediates = true;
        Disassembler::HexImmediateStyle hex_style = Disassembler::eHexStyleC;

        if (exe_ctx) {
          Target *target = exe_ctx->GetTargetPtr();
          if (target) {
            use_hex_immediates = target->GetUseHexImmediates();
            hex_style = target->GetHexImmediateStyle();

            if (!data_from_file) {
              const lldb::addr_t load_addr = m_address.GetLoadAddress(target);
              if (load_addr != LLDB_INVALID_ADDRESS) {
                pc = load_addr;
                m_using_file_addr = false;
              }
            }
          }
        }

        const uint8_t *opcode_data = data.GetDataStart();
        const size_t opcode_data_len = data.GetByteSize();
        llvm::MCInst inst;
        size_t inst_size =
            mc_disasm_ptr->GetMCInst(opcode_data, opcode_data_len, pc, inst);

        if (inst_size > 0) {
          mc_disasm_ptr->SetStyle(use_hex_immediates, hex_style);
          mc_disasm_ptr->PrintMCInst(inst, out_string, comment_string);

          if (!comment_string.empty()) {
            AppendComment(comment_string);
          }
        }

        if (inst_size == 0) {
          m_comment.assign("unknown opcode");
          inst_size = m_opcode.GetByteSize();
          StreamString mnemonic_strm;
          lldb::offset_t offset = 0;
          lldb::ByteOrder byte_order = data.GetByteOrder();
          switch (inst_size) {
          case 1: {
            const uint8_t uval8 = data.GetU8(&offset);
            m_opcode.SetOpcode8(uval8, byte_order);
            m_opcode_name.assign(".byte");
            mnemonic_strm.Printf("0x%2.2x", uval8);
          } break;
          case 2: {
            const uint16_t uval16 = data.GetU16(&offset);
            m_opcode.SetOpcode16(uval16, byte_order);
            m_opcode_name.assign(".short");
            mnemonic_strm.Printf("0x%4.4x", uval16);
          } break;
          case 4: {
            const uint32_t uval32 = data.GetU32(&offset);
            m_opcode.SetOpcode32(uval32, byte_order);
            m_opcode_name.assign(".long");
            mnemonic_strm.Printf("0x%8.8x", uval32);
          } break;
          case 8: {
            const uint64_t uval64 = data.GetU64(&offset);
            m_opcode.SetOpcode64(uval64, byte_order);
            m_opcode_name.assign(".quad");
            mnemonic_strm.Printf("0x%16.16" PRIx64, uval64);
          } break;
          default:
            if (inst_size == 0)
              return;
            else {
              const uint8_t *bytes = data.PeekData(offset, inst_size);
              if (bytes == NULL)
                return;
              m_opcode_name.assign(".byte");
              m_opcode.SetOpcodeBytes(bytes, inst_size);
              mnemonic_strm.Printf("0x%2.2x", bytes[0]);
              for (uint32_t i = 1; i < inst_size; ++i)
                mnemonic_strm.Printf(" 0x%2.2x", bytes[i]);
            }
            break;
          }
          m_mnemonics = mnemonic_strm.GetString();
          return;
        } else {
          if (m_does_branch == eLazyBoolCalculate) {
            const bool can_branch = mc_disasm_ptr->CanBranch(inst);
            if (can_branch)
              m_does_branch = eLazyBoolYes;
            else
              m_does_branch = eLazyBoolNo;
          }
        }

        static RegularExpression s_regex(
            llvm::StringRef("[ \t]*([^ ^\t]+)[ \t]*([^ ^\t].*)?"));

        RegularExpression::Match matches(3);

        if (s_regex.Execute(out_string, &matches)) {
          matches.GetMatchAtIndex(out_string.c_str(), 1, m_opcode_name);
          matches.GetMatchAtIndex(out_string.c_str(), 2, m_mnemonics);
        }
      }
    }
  }

  bool IsValid() const { return m_is_valid; }

  bool UsingFileAddress() const { return m_using_file_addr; }
  size_t GetByteSize() const { return m_opcode.GetByteSize(); }

  /// Grants exclusive access to the disassembler and initializes it with the
  /// given InstructionLLVMC and an optional ExecutionContext.
  class DisassemblerScope {
    std::shared_ptr<DisassemblerLLVMC> m_disasm;

  public:
    explicit DisassemblerScope(
        InstructionLLVMC &i,
        const lldb_private::ExecutionContext *exe_ctx = nullptr)
        : m_disasm(i.m_disasm_wp.lock()) {
      m_disasm->m_mutex.lock();
      m_disasm->m_inst = &i;
      m_disasm->m_exe_ctx = exe_ctx;
    }
    ~DisassemblerScope() { m_disasm->m_mutex.unlock(); }

    /// Evaluates to true if this scope contains a valid disassembler.
    operator bool() const { return static_cast<bool>(m_disasm); }

    std::shared_ptr<DisassemblerLLVMC> operator->() { return m_disasm; }
  };

  static llvm::StringRef::const_iterator
  ConsumeWhitespace(llvm::StringRef::const_iterator osi,
                    llvm::StringRef::const_iterator ose) {
    while (osi != ose) {
      switch (*osi) {
      default:
        return osi;
      case ' ':
      case '\t':
        break;
      }
      ++osi;
    }

    return osi;
  }

  static std::pair<bool, llvm::StringRef::const_iterator>
  ConsumeChar(llvm::StringRef::const_iterator osi, const char c,
              llvm::StringRef::const_iterator ose) {
    bool found = false;

    osi = ConsumeWhitespace(osi, ose);
    if (osi != ose && *osi == c) {
      found = true;
      ++osi;
    }

    return std::make_pair(found, osi);
  }

  static std::pair<Operand, llvm::StringRef::const_iterator>
  ParseRegisterName(llvm::StringRef::const_iterator osi,
                    llvm::StringRef::const_iterator ose) {
    Operand ret;
    ret.m_type = Operand::Type::Register;
    std::string str;

    osi = ConsumeWhitespace(osi, ose);

    while (osi != ose) {
      if (*osi >= '0' && *osi <= '9') {
        if (str.empty()) {
          return std::make_pair(Operand(), osi);
        } else {
          str.push_back(*osi);
        }
      } else if (*osi >= 'a' && *osi <= 'z') {
        str.push_back(*osi);
      } else {
        switch (*osi) {
        default:
          if (str.empty()) {
            return std::make_pair(Operand(), osi);
          } else {
            ret.m_register = ConstString(str);
            return std::make_pair(ret, osi);
          }
        case '%':
          if (!str.empty()) {
            return std::make_pair(Operand(), osi);
          }
          break;
        }
      }
      ++osi;
    }

    ret.m_register = ConstString(str);
    return std::make_pair(ret, osi);
  }

  static std::pair<Operand, llvm::StringRef::const_iterator>
  ParseImmediate(llvm::StringRef::const_iterator osi,
                 llvm::StringRef::const_iterator ose) {
    Operand ret;
    ret.m_type = Operand::Type::Immediate;
    std::string str;
    bool is_hex = false;

    osi = ConsumeWhitespace(osi, ose);

    while (osi != ose) {
      if (*osi >= '0' && *osi <= '9') {
        str.push_back(*osi);
      } else if (*osi >= 'a' && *osi <= 'f') {
        if (is_hex) {
          str.push_back(*osi);
        } else {
          return std::make_pair(Operand(), osi);
        }
      } else {
        switch (*osi) {
        default:
          if (str.empty()) {
            return std::make_pair(Operand(), osi);
          } else {
            ret.m_immediate = strtoull(str.c_str(), nullptr, 0);
            return std::make_pair(ret, osi);
          }
        case 'x':
          if (!str.compare("0")) {
            is_hex = true;
            str.push_back(*osi);
          } else {
            return std::make_pair(Operand(), osi);
          }
          break;
        case '#':
        case '$':
          if (!str.empty()) {
            return std::make_pair(Operand(), osi);
          }
          break;
        case '-':
          if (str.empty()) {
            ret.m_negative = true;
          } else {
            return std::make_pair(Operand(), osi);
          }
        }
      }
      ++osi;
    }

    ret.m_immediate = strtoull(str.c_str(), nullptr, 0);
    return std::make_pair(ret, osi);
  }

  // -0x5(%rax,%rax,2)
  static std::pair<Operand, llvm::StringRef::const_iterator>
  ParseIntelIndexedAccess(llvm::StringRef::const_iterator osi,
                          llvm::StringRef::const_iterator ose) {
    std::pair<Operand, llvm::StringRef::const_iterator> offset_and_iterator =
        ParseImmediate(osi, ose);
    if (offset_and_iterator.first.IsValid()) {
      osi = offset_and_iterator.second;
    }

    bool found = false;
    std::tie(found, osi) = ConsumeChar(osi, '(', ose);
    if (!found) {
      return std::make_pair(Operand(), osi);
    }

    std::pair<Operand, llvm::StringRef::const_iterator> base_and_iterator =
        ParseRegisterName(osi, ose);
    if (base_and_iterator.first.IsValid()) {
      osi = base_and_iterator.second;
    } else {
      return std::make_pair(Operand(), osi);
    }

    std::tie(found, osi) = ConsumeChar(osi, ',', ose);
    if (!found) {
      return std::make_pair(Operand(), osi);
    }

    std::pair<Operand, llvm::StringRef::const_iterator> index_and_iterator =
        ParseRegisterName(osi, ose);
    if (index_and_iterator.first.IsValid()) {
      osi = index_and_iterator.second;
    } else {
      return std::make_pair(Operand(), osi);
    }

    std::tie(found, osi) = ConsumeChar(osi, ',', ose);
    if (!found) {
      return std::make_pair(Operand(), osi);
    }

    std::pair<Operand, llvm::StringRef::const_iterator>
        multiplier_and_iterator = ParseImmediate(osi, ose);
    if (index_and_iterator.first.IsValid()) {
      osi = index_and_iterator.second;
    } else {
      return std::make_pair(Operand(), osi);
    }

    std::tie(found, osi) = ConsumeChar(osi, ')', ose);
    if (!found) {
      return std::make_pair(Operand(), osi);
    }

    Operand product;
    product.m_type = Operand::Type::Product;
    product.m_children.push_back(index_and_iterator.first);
    product.m_children.push_back(multiplier_and_iterator.first);

    Operand index;
    index.m_type = Operand::Type::Sum;
    index.m_children.push_back(base_and_iterator.first);
    index.m_children.push_back(product);

    if (offset_and_iterator.first.IsValid()) {
      Operand offset;
      offset.m_type = Operand::Type::Sum;
      offset.m_children.push_back(offset_and_iterator.first);
      offset.m_children.push_back(index);

      Operand deref;
      deref.m_type = Operand::Type::Dereference;
      deref.m_children.push_back(offset);
      return std::make_pair(deref, osi);
    } else {
      Operand deref;
      deref.m_type = Operand::Type::Dereference;
      deref.m_children.push_back(index);
      return std::make_pair(deref, osi);
    }
  }

  // -0x10(%rbp)
  static std::pair<Operand, llvm::StringRef::const_iterator>
  ParseIntelDerefAccess(llvm::StringRef::const_iterator osi,
                        llvm::StringRef::const_iterator ose) {
    std::pair<Operand, llvm::StringRef::const_iterator> offset_and_iterator =
        ParseImmediate(osi, ose);
    if (offset_and_iterator.first.IsValid()) {
      osi = offset_and_iterator.second;
    }

    bool found = false;
    std::tie(found, osi) = ConsumeChar(osi, '(', ose);
    if (!found) {
      return std::make_pair(Operand(), osi);
    }

    std::pair<Operand, llvm::StringRef::const_iterator> base_and_iterator =
        ParseRegisterName(osi, ose);
    if (base_and_iterator.first.IsValid()) {
      osi = base_and_iterator.second;
    } else {
      return std::make_pair(Operand(), osi);
    }

    std::tie(found, osi) = ConsumeChar(osi, ')', ose);
    if (!found) {
      return std::make_pair(Operand(), osi);
    }

    if (offset_and_iterator.first.IsValid()) {
      Operand offset;
      offset.m_type = Operand::Type::Sum;
      offset.m_children.push_back(offset_and_iterator.first);
      offset.m_children.push_back(base_and_iterator.first);

      Operand deref;
      deref.m_type = Operand::Type::Dereference;
      deref.m_children.push_back(offset);
      return std::make_pair(deref, osi);
    } else {
      Operand deref;
      deref.m_type = Operand::Type::Dereference;
      deref.m_children.push_back(base_and_iterator.first);
      return std::make_pair(deref, osi);
    }
  }

  // [sp, #8]!
  static std::pair<Operand, llvm::StringRef::const_iterator>
  ParseARMOffsetAccess(llvm::StringRef::const_iterator osi,
                       llvm::StringRef::const_iterator ose) {
    bool found = false;
    std::tie(found, osi) = ConsumeChar(osi, '[', ose);
    if (!found) {
      return std::make_pair(Operand(), osi);
    }

    std::pair<Operand, llvm::StringRef::const_iterator> base_and_iterator =
        ParseRegisterName(osi, ose);
    if (base_and_iterator.first.IsValid()) {
      osi = base_and_iterator.second;
    } else {
      return std::make_pair(Operand(), osi);
    }

    std::tie(found, osi) = ConsumeChar(osi, ',', ose);
    if (!found) {
      return std::make_pair(Operand(), osi);
    }

    std::pair<Operand, llvm::StringRef::const_iterator> offset_and_iterator =
        ParseImmediate(osi, ose);
    if (offset_and_iterator.first.IsValid()) {
      osi = offset_and_iterator.second;
    }

    std::tie(found, osi) = ConsumeChar(osi, ']', ose);
    if (!found) {
      return std::make_pair(Operand(), osi);
    }

    Operand offset;
    offset.m_type = Operand::Type::Sum;
    offset.m_children.push_back(offset_and_iterator.first);
    offset.m_children.push_back(base_and_iterator.first);

    Operand deref;
    deref.m_type = Operand::Type::Dereference;
    deref.m_children.push_back(offset);
    return std::make_pair(deref, osi);
  }

  // [sp]
  static std::pair<Operand, llvm::StringRef::const_iterator>
  ParseARMDerefAccess(llvm::StringRef::const_iterator osi,
                      llvm::StringRef::const_iterator ose) {
    bool found = false;
    std::tie(found, osi) = ConsumeChar(osi, '[', ose);
    if (!found) {
      return std::make_pair(Operand(), osi);
    }

    std::pair<Operand, llvm::StringRef::const_iterator> base_and_iterator =
        ParseRegisterName(osi, ose);
    if (base_and_iterator.first.IsValid()) {
      osi = base_and_iterator.second;
    } else {
      return std::make_pair(Operand(), osi);
    }

    std::tie(found, osi) = ConsumeChar(osi, ']', ose);
    if (!found) {
      return std::make_pair(Operand(), osi);
    }

    Operand deref;
    deref.m_type = Operand::Type::Dereference;
    deref.m_children.push_back(base_and_iterator.first);
    return std::make_pair(deref, osi);
  }

  static void DumpOperand(const Operand &op, Stream &s) {
    switch (op.m_type) {
    case Operand::Type::Dereference:
      s.PutCString("*");
      DumpOperand(op.m_children[0], s);
      break;
    case Operand::Type::Immediate:
      if (op.m_negative) {
        s.PutCString("-");
      }
      s.PutCString(llvm::to_string(op.m_immediate));
      break;
    case Operand::Type::Invalid:
      s.PutCString("Invalid");
      break;
    case Operand::Type::Product:
      s.PutCString("(");
      DumpOperand(op.m_children[0], s);
      s.PutCString("*");
      DumpOperand(op.m_children[1], s);
      s.PutCString(")");
      break;
    case Operand::Type::Register:
      s.PutCString(op.m_register.AsCString());
      break;
    case Operand::Type::Sum:
      s.PutCString("(");
      DumpOperand(op.m_children[0], s);
      s.PutCString("+");
      DumpOperand(op.m_children[1], s);
      s.PutCString(")");
      break;
    }
  }

  bool ParseOperands(
      llvm::SmallVectorImpl<Instruction::Operand> &operands) override {
    const char *operands_string = GetOperands(nullptr);

    if (!operands_string) {
      return false;
    }

    llvm::StringRef operands_ref(operands_string);

    llvm::StringRef::const_iterator osi = operands_ref.begin();
    llvm::StringRef::const_iterator ose = operands_ref.end();

    while (osi != ose) {
      Operand operand;
      llvm::StringRef::const_iterator iter;

      if ((std::tie(operand, iter) = ParseIntelIndexedAccess(osi, ose),
           operand.IsValid()) ||
          (std::tie(operand, iter) = ParseIntelDerefAccess(osi, ose),
           operand.IsValid()) ||
          (std::tie(operand, iter) = ParseARMOffsetAccess(osi, ose),
           operand.IsValid()) ||
          (std::tie(operand, iter) = ParseARMDerefAccess(osi, ose),
           operand.IsValid()) ||
          (std::tie(operand, iter) = ParseRegisterName(osi, ose),
           operand.IsValid()) ||
          (std::tie(operand, iter) = ParseImmediate(osi, ose),
           operand.IsValid())) {
        osi = iter;
        operands.push_back(operand);
      } else {
        return false;
      }

      std::pair<bool, llvm::StringRef::const_iterator> found_and_iter =
          ConsumeChar(osi, ',', ose);
      if (found_and_iter.first) {
        osi = found_and_iter.second;
      }

      osi = ConsumeWhitespace(osi, ose);
    }

    DisassemblerSP disasm_sp = m_disasm_wp.lock();

    if (disasm_sp && operands.size() > 1) {
      // TODO tie this into the MC Disassembler's notion of clobbers.
      switch (disasm_sp->GetArchitecture().GetMachine()) {
      default:
        break;
      case llvm::Triple::x86:
      case llvm::Triple::x86_64:
        operands[operands.size() - 1].m_clobbered = true;
        break;
      case llvm::Triple::arm:
        operands[0].m_clobbered = true;
        break;
      }
    }

    if (Log *log =
            lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS)) {
      StreamString ss;

      ss.Printf("[%s] expands to %zu operands:\n", operands_string,
                operands.size());
      for (const Operand &operand : operands) {
        ss.PutCString("  ");
        DumpOperand(operand, ss);
        ss.PutCString("\n");
      }

      log->PutString(ss.GetString());
    }

    return true;
  }

  bool IsCall() override {
    if (m_is_call == eLazyBoolCalculate) {
      DisassemblerScope disasm(*this);
      if (disasm) {
        DataExtractor data;
        if (m_opcode.GetData(data)) {
          bool is_alternate_isa;
          lldb::addr_t pc = m_address.GetFileAddress();

          DisassemblerLLVMC::MCDisasmInstance *mc_disasm_ptr =
              GetDisasmToUse(is_alternate_isa, disasm);
          const uint8_t *opcode_data = data.GetDataStart();
          const size_t opcode_data_len = data.GetByteSize();
          llvm::MCInst inst;
          const size_t inst_size =
              mc_disasm_ptr->GetMCInst(opcode_data, opcode_data_len, pc, inst);
          if (inst_size == 0) {
            m_is_call = eLazyBoolNo;
          } else {
            if (mc_disasm_ptr->IsCall(inst))
              m_is_call = eLazyBoolYes;
            else
              m_is_call = eLazyBoolNo;
          }
        }
      }
    }
    return m_is_call == eLazyBoolYes;
  }

protected:
  std::weak_ptr<DisassemblerLLVMC> m_disasm_wp;
  LazyBool m_does_branch;
  LazyBool m_has_delay_slot;
  LazyBool m_is_call;
  bool m_is_valid;
  bool m_using_file_addr;

private:
  DisassemblerLLVMC::MCDisasmInstance *
  GetDisasmToUse(bool &is_alternate_isa, DisassemblerScope &disasm) {
    is_alternate_isa = false;
    if (disasm) {
      if (disasm->m_alternate_disasm_up) {
        const AddressClass address_class = GetAddressClass();

        if (address_class == AddressClass::eCodeAlternateISA) {
          is_alternate_isa = true;
          return disasm->m_alternate_disasm_up.get();
        }
      }
      return disasm->m_disasm_up.get();
    }
    return nullptr;
  }
};

std::unique_ptr<DisassemblerLLVMC::MCDisasmInstance>
DisassemblerLLVMC::MCDisasmInstance::Create(const char *triple, const char *cpu,
                                            const char *features_str,
                                            unsigned flavor,
                                            DisassemblerLLVMC &owner) {
  using Instance = std::unique_ptr<DisassemblerLLVMC::MCDisasmInstance>;

  std::string Status;
  const llvm::Target *curr_target =
      llvm::TargetRegistry::lookupTarget(triple, Status);
  if (!curr_target)
    return Instance();

  std::unique_ptr<llvm::MCInstrInfo> instr_info_up(
      curr_target->createMCInstrInfo());
  if (!instr_info_up)
    return Instance();

  std::unique_ptr<llvm::MCRegisterInfo> reg_info_up(
      curr_target->createMCRegInfo(triple));
  if (!reg_info_up)
    return Instance();

  std::unique_ptr<llvm::MCSubtargetInfo> subtarget_info_up(
      curr_target->createMCSubtargetInfo(triple, cpu, features_str));
  if (!subtarget_info_up)
    return Instance();

  std::unique_ptr<llvm::MCAsmInfo> asm_info_up(
      curr_target->createMCAsmInfo(*reg_info_up, triple));
  if (!asm_info_up)
    return Instance();

  std::unique_ptr<llvm::MCContext> context_up(
      new llvm::MCContext(asm_info_up.get(), reg_info_up.get(), 0));
  if (!context_up)
    return Instance();

  std::unique_ptr<llvm::MCDisassembler> disasm_up(
      curr_target->createMCDisassembler(*subtarget_info_up, *context_up));
  if (!disasm_up)
    return Instance();

  std::unique_ptr<llvm::MCRelocationInfo> rel_info_up(
      curr_target->createMCRelocationInfo(triple, *context_up));
  if (!rel_info_up)
    return Instance();

  std::unique_ptr<llvm::MCSymbolizer> symbolizer_up(
      curr_target->createMCSymbolizer(
          triple, nullptr, DisassemblerLLVMC::SymbolLookupCallback, &owner,
          context_up.get(), std::move(rel_info_up)));
  disasm_up->setSymbolizer(std::move(symbolizer_up));

  unsigned asm_printer_variant =
      flavor == ~0U ? asm_info_up->getAssemblerDialect() : flavor;

  std::unique_ptr<llvm::MCInstPrinter> instr_printer_up(
      curr_target->createMCInstPrinter(llvm::Triple{triple},
                                       asm_printer_variant, *asm_info_up,
                                       *instr_info_up, *reg_info_up));
  if (!instr_printer_up)
    return Instance();

  return Instance(
      new MCDisasmInstance(std::move(instr_info_up), std::move(reg_info_up),
                           std::move(subtarget_info_up), std::move(asm_info_up),
                           std::move(context_up), std::move(disasm_up),
                           std::move(instr_printer_up)));
}

DisassemblerLLVMC::MCDisasmInstance::MCDisasmInstance(
    std::unique_ptr<llvm::MCInstrInfo> &&instr_info_up,
    std::unique_ptr<llvm::MCRegisterInfo> &&reg_info_up,
    std::unique_ptr<llvm::MCSubtargetInfo> &&subtarget_info_up,
    std::unique_ptr<llvm::MCAsmInfo> &&asm_info_up,
    std::unique_ptr<llvm::MCContext> &&context_up,
    std::unique_ptr<llvm::MCDisassembler> &&disasm_up,
    std::unique_ptr<llvm::MCInstPrinter> &&instr_printer_up)
    : m_instr_info_up(std::move(instr_info_up)),
      m_reg_info_up(std::move(reg_info_up)),
      m_subtarget_info_up(std::move(subtarget_info_up)),
      m_asm_info_up(std::move(asm_info_up)),
      m_context_up(std::move(context_up)), m_disasm_up(std::move(disasm_up)),
      m_instr_printer_up(std::move(instr_printer_up)) {
  assert(m_instr_info_up && m_reg_info_up && m_subtarget_info_up &&
         m_asm_info_up && m_context_up && m_disasm_up && m_instr_printer_up);
}

uint64_t DisassemblerLLVMC::MCDisasmInstance::GetMCInst(
    const uint8_t *opcode_data, size_t opcode_data_len, lldb::addr_t pc,
    llvm::MCInst &mc_inst) const {
  llvm::ArrayRef<uint8_t> data(opcode_data, opcode_data_len);
  llvm::MCDisassembler::DecodeStatus status;

  uint64_t new_inst_size;
  status = m_disasm_up->getInstruction(mc_inst, new_inst_size, data, pc,
                                       llvm::nulls(), llvm::nulls());
  if (status == llvm::MCDisassembler::Success)
    return new_inst_size;
  else
    return 0;
}

void DisassemblerLLVMC::MCDisasmInstance::PrintMCInst(
    llvm::MCInst &mc_inst, std::string &inst_string,
    std::string &comments_string) {
  llvm::raw_string_ostream inst_stream(inst_string);
  llvm::raw_string_ostream comments_stream(comments_string);

  m_instr_printer_up->setCommentStream(comments_stream);
  m_instr_printer_up->printInst(&mc_inst, inst_stream, llvm::StringRef(),
                                *m_subtarget_info_up);
  m_instr_printer_up->setCommentStream(llvm::nulls());
  comments_stream.flush();

  static std::string g_newlines("\r\n");

  for (size_t newline_pos = 0;
       (newline_pos = comments_string.find_first_of(g_newlines, newline_pos)) !=
       comments_string.npos;
       /**/) {
    comments_string.replace(comments_string.begin() + newline_pos,
                            comments_string.begin() + newline_pos + 1, 1, ' ');
  }
}

void DisassemblerLLVMC::MCDisasmInstance::SetStyle(
    bool use_hex_immed, HexImmediateStyle hex_style) {
  m_instr_printer_up->setPrintImmHex(use_hex_immed);
  switch (hex_style) {
  case eHexStyleC:
    m_instr_printer_up->setPrintHexStyle(llvm::HexStyle::C);
    break;
  case eHexStyleAsm:
    m_instr_printer_up->setPrintHexStyle(llvm::HexStyle::Asm);
    break;
  }
}

bool DisassemblerLLVMC::MCDisasmInstance::CanBranch(
    llvm::MCInst &mc_inst) const {
  return m_instr_info_up->get(mc_inst.getOpcode())
      .mayAffectControlFlow(mc_inst, *m_reg_info_up);
}

bool DisassemblerLLVMC::MCDisasmInstance::HasDelaySlot(
    llvm::MCInst &mc_inst) const {
  return m_instr_info_up->get(mc_inst.getOpcode()).hasDelaySlot();
}

bool DisassemblerLLVMC::MCDisasmInstance::IsCall(llvm::MCInst &mc_inst) const {
  return m_instr_info_up->get(mc_inst.getOpcode()).isCall();
}

DisassemblerLLVMC::DisassemblerLLVMC(const ArchSpec &arch,
                                     const char *flavor_string)
    : Disassembler(arch, flavor_string), m_exe_ctx(NULL), m_inst(NULL),
      m_data_from_file(false) {
  if (!FlavorValidForArchSpec(arch, m_flavor.c_str())) {
    m_flavor.assign("default");
  }

  unsigned flavor = ~0U;
  llvm::Triple triple = arch.GetTriple();

  // So far the only supported flavor is "intel" on x86.  The base class will
  // set this correctly coming in.
  if (triple.getArch() == llvm::Triple::x86 ||
      triple.getArch() == llvm::Triple::x86_64) {
    if (m_flavor == "intel") {
      flavor = 1;
    } else if (m_flavor == "att") {
      flavor = 0;
    }
  }

  ArchSpec thumb_arch(arch);
  if (triple.getArch() == llvm::Triple::arm) {
    std::string thumb_arch_name(thumb_arch.GetTriple().getArchName().str());
    // Replace "arm" with "thumb" so we get all thumb variants correct
    if (thumb_arch_name.size() > 3) {
      thumb_arch_name.erase(0, 3);
      thumb_arch_name.insert(0, "thumb");
    } else {
      thumb_arch_name = "thumbv8.2a";
    }
    thumb_arch.GetTriple().setArchName(llvm::StringRef(thumb_arch_name));
  }

  // If no sub architecture specified then use the most recent arm architecture
  // so the disassembler will return all instruction. Without it we will see a
  // lot of unknow opcode in case the code uses instructions which are not
  // available in the oldest arm version (used when no sub architecture is
  // specified)
  if (triple.getArch() == llvm::Triple::arm &&
      triple.getSubArch() == llvm::Triple::NoSubArch)
    triple.setArchName("armv8.2a");

  std::string features_str = "";
  const char *triple_str = triple.getTriple().c_str();

  // ARM Cortex M0-M7 devices only execute thumb instructions
  if (arch.IsAlwaysThumbInstructions()) {
    triple_str = thumb_arch.GetTriple().getTriple().c_str();
    features_str += "+fp-armv8,";
  }

  const char *cpu = "";

  switch (arch.GetCore()) {
  case ArchSpec::eCore_mips32:
  case ArchSpec::eCore_mips32el:
    cpu = "mips32";
    break;
  case ArchSpec::eCore_mips32r2:
  case ArchSpec::eCore_mips32r2el:
    cpu = "mips32r2";
    break;
  case ArchSpec::eCore_mips32r3:
  case ArchSpec::eCore_mips32r3el:
    cpu = "mips32r3";
    break;
  case ArchSpec::eCore_mips32r5:
  case ArchSpec::eCore_mips32r5el:
    cpu = "mips32r5";
    break;
  case ArchSpec::eCore_mips32r6:
  case ArchSpec::eCore_mips32r6el:
    cpu = "mips32r6";
    break;
  case ArchSpec::eCore_mips64:
  case ArchSpec::eCore_mips64el:
    cpu = "mips64";
    break;
  case ArchSpec::eCore_mips64r2:
  case ArchSpec::eCore_mips64r2el:
    cpu = "mips64r2";
    break;
  case ArchSpec::eCore_mips64r3:
  case ArchSpec::eCore_mips64r3el:
    cpu = "mips64r3";
    break;
  case ArchSpec::eCore_mips64r5:
  case ArchSpec::eCore_mips64r5el:
    cpu = "mips64r5";
    break;
  case ArchSpec::eCore_mips64r6:
  case ArchSpec::eCore_mips64r6el:
    cpu = "mips64r6";
    break;
  default:
    cpu = "";
    break;
  }

  if (triple.getArch() == llvm::Triple::mips ||
      triple.getArch() == llvm::Triple::mipsel ||
      triple.getArch() == llvm::Triple::mips64 ||
      triple.getArch() == llvm::Triple::mips64el) {
    uint32_t arch_flags = arch.GetFlags();
    if (arch_flags & ArchSpec::eMIPSAse_msa)
      features_str += "+msa,";
    if (arch_flags & ArchSpec::eMIPSAse_dsp)
      features_str += "+dsp,";
    if (arch_flags & ArchSpec::eMIPSAse_dspr2)
      features_str += "+dspr2,";
  }

  // If any AArch64 variant, enable the ARMv8.2 ISA extensions so we can
  // disassemble newer instructions.
  if (triple.getArch() == llvm::Triple::aarch64)
    features_str += "+v8.2a";

  // We use m_disasm_ap.get() to tell whether we are valid or not, so if this
  // isn't good for some reason, we won't be valid and FindPlugin will fail and
  // we won't get used.
  m_disasm_up = MCDisasmInstance::Create(triple_str, cpu, features_str.c_str(),
                                         flavor, *this);

  llvm::Triple::ArchType llvm_arch = triple.getArch();

  // For arm CPUs that can execute arm or thumb instructions, also create a
  // thumb instruction disassembler.
  if (llvm_arch == llvm::Triple::arm) {
    std::string thumb_triple(thumb_arch.GetTriple().getTriple());
    m_alternate_disasm_up =
        MCDisasmInstance::Create(thumb_triple.c_str(), "", features_str.c_str(), 
                                 flavor, *this);
    if (!m_alternate_disasm_up)
      m_disasm_up.reset();

  } else if (llvm_arch == llvm::Triple::mips ||
             llvm_arch == llvm::Triple::mipsel ||
             llvm_arch == llvm::Triple::mips64 ||
             llvm_arch == llvm::Triple::mips64el) {
    /* Create alternate disassembler for MIPS16 and microMIPS */
    uint32_t arch_flags = arch.GetFlags();
    if (arch_flags & ArchSpec::eMIPSAse_mips16)
      features_str += "+mips16,";
    else if (arch_flags & ArchSpec::eMIPSAse_micromips)
      features_str += "+micromips,";

    m_alternate_disasm_up = MCDisasmInstance::Create(
        triple_str, cpu, features_str.c_str(), flavor, *this);
    if (!m_alternate_disasm_up)
      m_disasm_up.reset();
  }
}

DisassemblerLLVMC::~DisassemblerLLVMC() = default;

Disassembler *DisassemblerLLVMC::CreateInstance(const ArchSpec &arch,
                                                const char *flavor) {
  if (arch.GetTriple().getArch() != llvm::Triple::UnknownArch) {
    std::unique_ptr<DisassemblerLLVMC> disasm_ap(
        new DisassemblerLLVMC(arch, flavor));

    if (disasm_ap.get() && disasm_ap->IsValid())
      return disasm_ap.release();
  }
  return NULL;
}

size_t DisassemblerLLVMC::DecodeInstructions(const Address &base_addr,
                                             const DataExtractor &data,
                                             lldb::offset_t data_offset,
                                             size_t num_instructions,
                                             bool append, bool data_from_file) {
  if (!append)
    m_instruction_list.Clear();

  if (!IsValid())
    return 0;

  m_data_from_file = data_from_file;
  uint32_t data_cursor = data_offset;
  const size_t data_byte_size = data.GetByteSize();
  uint32_t instructions_parsed = 0;
  Address inst_addr(base_addr);

  while (data_cursor < data_byte_size &&
         instructions_parsed < num_instructions) {

    AddressClass address_class = AddressClass::eCode;

    if (m_alternate_disasm_up)
      address_class = inst_addr.GetAddressClass();

    InstructionSP inst_sp(
        new InstructionLLVMC(*this, inst_addr, address_class));

    if (!inst_sp)
      break;

    uint32_t inst_size = inst_sp->Decode(*this, data, data_cursor);

    if (inst_size == 0)
      break;

    m_instruction_list.Append(inst_sp);
    data_cursor += inst_size;
    inst_addr.Slide(inst_size);
    instructions_parsed++;
  }

  return data_cursor - data_offset;
}

void DisassemblerLLVMC::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                "Disassembler that uses LLVM MC to disassemble "
                                "i386, x86_64, ARM, and ARM64.",
                                CreateInstance);

  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmParsers();
  llvm::InitializeAllDisassemblers();
}

void DisassemblerLLVMC::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

ConstString DisassemblerLLVMC::GetPluginNameStatic() {
  static ConstString g_name("llvm-mc");
  return g_name;
}

int DisassemblerLLVMC::OpInfoCallback(void *disassembler, uint64_t pc,
                                      uint64_t offset, uint64_t size,
                                      int tag_type, void *tag_bug) {
  return static_cast<DisassemblerLLVMC *>(disassembler)
      ->OpInfo(pc, offset, size, tag_type, tag_bug);
}

const char *DisassemblerLLVMC::SymbolLookupCallback(void *disassembler,
                                                    uint64_t value,
                                                    uint64_t *type, uint64_t pc,
                                                    const char **name) {
  return static_cast<DisassemblerLLVMC *>(disassembler)
      ->SymbolLookup(value, type, pc, name);
}

bool DisassemblerLLVMC::FlavorValidForArchSpec(
    const lldb_private::ArchSpec &arch, const char *flavor) {
  llvm::Triple triple = arch.GetTriple();
  if (flavor == NULL || strcmp(flavor, "default") == 0)
    return true;

  if (triple.getArch() == llvm::Triple::x86 ||
      triple.getArch() == llvm::Triple::x86_64) {
    return strcmp(flavor, "intel") == 0 || strcmp(flavor, "att") == 0;
  } else
    return false;
}

bool DisassemblerLLVMC::IsValid() const { return m_disasm_up.operator bool(); }

int DisassemblerLLVMC::OpInfo(uint64_t PC, uint64_t Offset, uint64_t Size,
                              int tag_type, void *tag_bug) {
  switch (tag_type) {
  default:
    break;
  case 1:
    memset(tag_bug, 0, sizeof(::LLVMOpInfo1));
    break;
  }
  return 0;
}

const char *DisassemblerLLVMC::SymbolLookup(uint64_t value, uint64_t *type_ptr,
                                            uint64_t pc, const char **name) {
  if (*type_ptr) {
    if (m_exe_ctx && m_inst) {
      // std::string remove_this_prior_to_checkin;
      Target *target = m_exe_ctx ? m_exe_ctx->GetTargetPtr() : NULL;
      Address value_so_addr;
      Address pc_so_addr;
      if (m_inst->UsingFileAddress()) {
        ModuleSP module_sp(m_inst->GetAddress().GetModule());
        if (module_sp) {
          module_sp->ResolveFileAddress(value, value_so_addr);
          module_sp->ResolveFileAddress(pc, pc_so_addr);
        }
      } else if (target && !target->GetSectionLoadList().IsEmpty()) {
        target->GetSectionLoadList().ResolveLoadAddress(value, value_so_addr);
        target->GetSectionLoadList().ResolveLoadAddress(pc, pc_so_addr);
      }

      SymbolContext sym_ctx;
      const SymbolContextItem resolve_scope =
          eSymbolContextFunction | eSymbolContextSymbol;
      if (pc_so_addr.IsValid() && pc_so_addr.GetModule()) {
        pc_so_addr.GetModule()->ResolveSymbolContextForAddress(
            pc_so_addr, resolve_scope, sym_ctx);
      }

      if (value_so_addr.IsValid() && value_so_addr.GetSection()) {
        StreamString ss;

        bool format_omitting_current_func_name = false;
        if (sym_ctx.symbol || sym_ctx.function) {
          AddressRange range;
          if (sym_ctx.GetAddressRange(resolve_scope, 0, false, range) &&
              range.GetBaseAddress().IsValid() &&
              range.ContainsLoadAddress(value_so_addr, target)) {
            format_omitting_current_func_name = true;
          }
        }

        // If the "value" address (the target address we're symbolicating) is
        // inside the same SymbolContext as the current instruction pc
        // (pc_so_addr), don't print the full function name - just print it
        // with DumpStyleNoFunctionName style, e.g. "<+36>".
        if (format_omitting_current_func_name) {
          value_so_addr.Dump(&ss, target, Address::DumpStyleNoFunctionName,
                             Address::DumpStyleSectionNameOffset);
        } else {
          value_so_addr.Dump(
              &ss, target,
              Address::DumpStyleResolvedDescriptionNoFunctionArguments,
              Address::DumpStyleSectionNameOffset);
        }

        if (!ss.GetString().empty()) {
          // If Address::Dump returned a multi-line description, most commonly
          // seen when we have multiple levels of inlined functions at an
          // address, only show the first line.
          std::string str = ss.GetString();
          size_t first_eol_char = str.find_first_of("\r\n");
          if (first_eol_char != std::string::npos) {
            str.erase(first_eol_char);
          }
          m_inst->AppendComment(str);
        }
      }
    }
  }

  *type_ptr = LLVMDisassembler_ReferenceType_InOut_None;
  *name = NULL;
  return NULL;
}

//------------------------------------------------------------------
// PluginInterface protocol
//------------------------------------------------------------------
ConstString DisassemblerLLVMC::GetPluginName() { return GetPluginNameStatic(); }

uint32_t DisassemblerLLVMC::GetPluginVersion() { return 1; }
