//===-- Disassembler.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/Disassembler.h"

#include "lldb/Core/AddressRange.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/EmulateInstruction.h"
#include "lldb/Core/Mangled.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/SourceManager.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Interpreter/OptionValue.h"
#include "lldb/Interpreter/OptionValueArray.h"
#include "lldb/Interpreter/OptionValueDictionary.h"
#include "lldb/Interpreter/OptionValueRegex.h"
#include "lldb/Interpreter/OptionValueString.h"
#include "lldb/Interpreter/OptionValueUInt64.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/Timer.h"
#include "lldb/lldb-private-enumerations.h"
#include "lldb/lldb-private-interfaces.h"
#include "lldb/lldb-private-types.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Support/Compiler.h"

#include <cstdint>
#include <cstring>
#include <utility>

#include <assert.h>

#define DEFAULT_DISASM_BYTE_SIZE 32

using namespace lldb;
using namespace lldb_private;

DisassemblerSP Disassembler::FindPlugin(const ArchSpec &arch,
                                        const char *flavor,
                                        const char *plugin_name) {
  static Timer::Category func_cat(LLVM_PRETTY_FUNCTION);
  Timer scoped_timer(func_cat,
                     "Disassembler::FindPlugin (arch = %s, plugin_name = %s)",
                     arch.GetArchitectureName(), plugin_name);

  DisassemblerCreateInstance create_callback = nullptr;

  if (plugin_name) {
    ConstString const_plugin_name(plugin_name);
    create_callback = PluginManager::GetDisassemblerCreateCallbackForPluginName(
        const_plugin_name);
    if (create_callback) {
      DisassemblerSP disassembler_sp(create_callback(arch, flavor));

      if (disassembler_sp)
        return disassembler_sp;
    }
  } else {
    for (uint32_t idx = 0;
         (create_callback = PluginManager::GetDisassemblerCreateCallbackAtIndex(
              idx)) != nullptr;
         ++idx) {
      DisassemblerSP disassembler_sp(create_callback(arch, flavor));

      if (disassembler_sp)
        return disassembler_sp;
    }
  }
  return DisassemblerSP();
}

DisassemblerSP Disassembler::FindPluginForTarget(const TargetSP target_sp,
                                                 const ArchSpec &arch,
                                                 const char *flavor,
                                                 const char *plugin_name) {
  if (target_sp && flavor == nullptr) {
    // FIXME - we don't have the mechanism in place to do per-architecture
    // settings.  But since we know that for now we only support flavors on x86
    // & x86_64,
    if (arch.GetTriple().getArch() == llvm::Triple::x86 ||
        arch.GetTriple().getArch() == llvm::Triple::x86_64)
      flavor = target_sp->GetDisassemblyFlavor();
  }
  return FindPlugin(arch, flavor, plugin_name);
}

static void ResolveAddress(const ExecutionContext &exe_ctx, const Address &addr,
                           Address &resolved_addr) {
  if (!addr.IsSectionOffset()) {
    // If we weren't passed in a section offset address range, try and resolve
    // it to something
    Target *target = exe_ctx.GetTargetPtr();
    if (target) {
      bool is_resolved =
          target->GetSectionLoadList().IsEmpty() ?
              target->GetImages().ResolveFileAddress(addr.GetOffset(),
                                                     resolved_addr) :
              target->GetSectionLoadList().ResolveLoadAddress(addr.GetOffset(),
                                                              resolved_addr);

      // We weren't able to resolve the address, just treat it as a raw address
      if (is_resolved && resolved_addr.IsValid())
        return;
    }
  }
  resolved_addr = addr;
}

size_t Disassembler::Disassemble(Debugger &debugger, const ArchSpec &arch,
                                 const char *plugin_name, const char *flavor,
                                 const ExecutionContext &exe_ctx,
                                 SymbolContextList &sc_list,
                                 uint32_t num_instructions,
                                 bool mixed_source_and_assembly,
                                 uint32_t num_mixed_context_lines,
                                 uint32_t options, Stream &strm) {
  size_t success_count = 0;
  const size_t count = sc_list.GetSize();
  SymbolContext sc;
  AddressRange range;
  const uint32_t scope =
      eSymbolContextBlock | eSymbolContextFunction | eSymbolContextSymbol;
  const bool use_inline_block_range = true;
  for (size_t i = 0; i < count; ++i) {
    if (!sc_list.GetContextAtIndex(i, sc))
      break;
    for (uint32_t range_idx = 0;
         sc.GetAddressRange(scope, range_idx, use_inline_block_range, range);
         ++range_idx) {
      if (Disassemble(debugger, arch, plugin_name, flavor, exe_ctx, range,
                      num_instructions, mixed_source_and_assembly,
                      num_mixed_context_lines, options, strm)) {
        ++success_count;
        strm.EOL();
      }
    }
  }
  return success_count;
}

bool Disassembler::Disassemble(Debugger &debugger, const ArchSpec &arch,
                               const char *plugin_name, const char *flavor,
                               const ExecutionContext &exe_ctx,
                               const ConstString &name, Module *module,
                               uint32_t num_instructions,
                               bool mixed_source_and_assembly,
                               uint32_t num_mixed_context_lines,
                               uint32_t options, Stream &strm) {
  SymbolContextList sc_list;
  if (name) {
    const bool include_symbols = true;
    const bool include_inlines = true;
    if (module) {
      module->FindFunctions(name, nullptr, eFunctionNameTypeAuto,
                            include_symbols, include_inlines, true, sc_list);
    } else if (exe_ctx.GetTargetPtr()) {
      exe_ctx.GetTargetPtr()->GetImages().FindFunctions(
          name, eFunctionNameTypeAuto, include_symbols, include_inlines, false,
          sc_list);
    }
  }

  if (sc_list.GetSize()) {
    return Disassemble(debugger, arch, plugin_name, flavor, exe_ctx, sc_list,
                       num_instructions, mixed_source_and_assembly,
                       num_mixed_context_lines, options, strm);
  }
  return false;
}

lldb::DisassemblerSP Disassembler::DisassembleRange(
    const ArchSpec &arch, const char *plugin_name, const char *flavor,
    const ExecutionContext &exe_ctx, const AddressRange &range,
    bool prefer_file_cache) {
  lldb::DisassemblerSP disasm_sp;
  if (range.GetByteSize() > 0 && range.GetBaseAddress().IsValid()) {
    disasm_sp = Disassembler::FindPluginForTarget(exe_ctx.GetTargetSP(), arch,
                                                  flavor, plugin_name);

    if (disasm_sp) {
      size_t bytes_disassembled = disasm_sp->ParseInstructions(
          &exe_ctx, range, nullptr, prefer_file_cache);
      if (bytes_disassembled == 0)
        disasm_sp.reset();
    }
  }
  return disasm_sp;
}

lldb::DisassemblerSP
Disassembler::DisassembleBytes(const ArchSpec &arch, const char *plugin_name,
                               const char *flavor, const Address &start,
                               const void *src, size_t src_len,
                               uint32_t num_instructions, bool data_from_file) {
  lldb::DisassemblerSP disasm_sp;

  if (src) {
    disasm_sp = Disassembler::FindPlugin(arch, flavor, plugin_name);

    if (disasm_sp) {
      DataExtractor data(src, src_len, arch.GetByteOrder(),
                         arch.GetAddressByteSize());

      (void)disasm_sp->DecodeInstructions(start, data, 0, num_instructions,
                                          false, data_from_file);
    }
  }

  return disasm_sp;
}

bool Disassembler::Disassemble(Debugger &debugger, const ArchSpec &arch,
                               const char *plugin_name, const char *flavor,
                               const ExecutionContext &exe_ctx,
                               const AddressRange &disasm_range,
                               uint32_t num_instructions,
                               bool mixed_source_and_assembly,
                               uint32_t num_mixed_context_lines,
                               uint32_t options, Stream &strm) {
  if (disasm_range.GetByteSize()) {
    lldb::DisassemblerSP disasm_sp(Disassembler::FindPluginForTarget(
        exe_ctx.GetTargetSP(), arch, flavor, plugin_name));

    if (disasm_sp) {
      AddressRange range;
      ResolveAddress(exe_ctx, disasm_range.GetBaseAddress(),
                     range.GetBaseAddress());
      range.SetByteSize(disasm_range.GetByteSize());
      const bool prefer_file_cache = false;
      size_t bytes_disassembled = disasm_sp->ParseInstructions(
          &exe_ctx, range, &strm, prefer_file_cache);
      if (bytes_disassembled == 0)
        return false;

      return PrintInstructions(disasm_sp.get(), debugger, arch, exe_ctx,
                               num_instructions, mixed_source_and_assembly,
                               num_mixed_context_lines, options, strm);
    }
  }
  return false;
}

bool Disassembler::Disassemble(Debugger &debugger, const ArchSpec &arch,
                               const char *plugin_name, const char *flavor,
                               const ExecutionContext &exe_ctx,
                               const Address &start_address,
                               uint32_t num_instructions,
                               bool mixed_source_and_assembly,
                               uint32_t num_mixed_context_lines,
                               uint32_t options, Stream &strm) {
  if (num_instructions > 0) {
    lldb::DisassemblerSP disasm_sp(Disassembler::FindPluginForTarget(
        exe_ctx.GetTargetSP(), arch, flavor, plugin_name));
    if (disasm_sp) {
      Address addr;
      ResolveAddress(exe_ctx, start_address, addr);
      const bool prefer_file_cache = false;
      size_t bytes_disassembled = disasm_sp->ParseInstructions(
          &exe_ctx, addr, num_instructions, prefer_file_cache);
      if (bytes_disassembled == 0)
        return false;
      return PrintInstructions(disasm_sp.get(), debugger, arch, exe_ctx,
                               num_instructions, mixed_source_and_assembly,
                               num_mixed_context_lines, options, strm);
    }
  }
  return false;
}

Disassembler::SourceLine
Disassembler::GetFunctionDeclLineEntry(const SymbolContext &sc) {
  SourceLine decl_line;
  if (sc.function && sc.line_entry.IsValid()) {
    LineEntry prologue_end_line = sc.line_entry;
    FileSpec func_decl_file;
    uint32_t func_decl_line;
    sc.function->GetStartLineSourceInfo(func_decl_file, func_decl_line);
    if (func_decl_file == prologue_end_line.file ||
        func_decl_file == prologue_end_line.original_file) {
      decl_line.file = func_decl_file;
      decl_line.line = func_decl_line;
      // TODO do we care about column on these entries?  If so, we need to
      // plumb that through GetStartLineSourceInfo.
      decl_line.column = 0;
    }
  }
  return decl_line;
}

void Disassembler::AddLineToSourceLineTables(
    SourceLine &line,
    std::map<FileSpec, std::set<uint32_t>> &source_lines_seen) {
  if (line.IsValid()) {
    auto source_lines_seen_pos = source_lines_seen.find(line.file);
    if (source_lines_seen_pos == source_lines_seen.end()) {
      std::set<uint32_t> lines;
      lines.insert(line.line);
      source_lines_seen.emplace(line.file, lines);
    } else {
      source_lines_seen_pos->second.insert(line.line);
    }
  }
}

bool Disassembler::ElideMixedSourceAndDisassemblyLine(
    const ExecutionContext &exe_ctx, const SymbolContext &sc,
    SourceLine &line) {

  // TODO: should we also check target.process.thread.step-avoid-libraries ?

  const RegularExpression *avoid_regex = nullptr;

  // Skip any line #0 entries - they are implementation details
  if (line.line == 0)
    return false;

  ThreadSP thread_sp = exe_ctx.GetThreadSP();
  if (thread_sp) {
    avoid_regex = thread_sp->GetSymbolsToAvoidRegexp();
  } else {
    TargetSP target_sp = exe_ctx.GetTargetSP();
    if (target_sp) {
      Status error;
      OptionValueSP value_sp = target_sp->GetDebugger().GetPropertyValue(
          &exe_ctx, "target.process.thread.step-avoid-regexp", false, error);
      if (value_sp && value_sp->GetType() == OptionValue::eTypeRegex) {
        OptionValueRegex *re = value_sp->GetAsRegex();
        if (re) {
          avoid_regex = re->GetCurrentValue();
        }
      }
    }
  }
  if (avoid_regex && sc.symbol != nullptr) {
    const char *function_name =
        sc.GetFunctionName(Mangled::ePreferDemangledWithoutArguments)
            .GetCString();
    if (function_name) {
      RegularExpression::Match regex_match(1);
      if (avoid_regex->Execute(function_name, &regex_match)) {
        // skip this source line
        return true;
      }
    }
  }
  // don't skip this source line
  return false;
}

bool Disassembler::PrintInstructions(Disassembler *disasm_ptr,
                                     Debugger &debugger, const ArchSpec &arch,
                                     const ExecutionContext &exe_ctx,
                                     uint32_t num_instructions,
                                     bool mixed_source_and_assembly,
                                     uint32_t num_mixed_context_lines,
                                     uint32_t options, Stream &strm) {
  // We got some things disassembled...
  size_t num_instructions_found = disasm_ptr->GetInstructionList().GetSize();

  if (num_instructions > 0 && num_instructions < num_instructions_found)
    num_instructions_found = num_instructions;

  const uint32_t max_opcode_byte_size =
      disasm_ptr->GetInstructionList().GetMaxOpcocdeByteSize();
  SymbolContext sc;
  SymbolContext prev_sc;
  AddressRange current_source_line_range;
  const Address *pc_addr_ptr = nullptr;
  StackFrame *frame = exe_ctx.GetFramePtr();

  TargetSP target_sp(exe_ctx.GetTargetSP());
  SourceManager &source_manager =
      target_sp ? target_sp->GetSourceManager() : debugger.GetSourceManager();

  if (frame) {
    pc_addr_ptr = &frame->GetFrameCodeAddress();
  }
  const uint32_t scope =
      eSymbolContextLineEntry | eSymbolContextFunction | eSymbolContextSymbol;
  const bool use_inline_block_range = false;

  const FormatEntity::Entry *disassembly_format = nullptr;
  FormatEntity::Entry format;
  if (exe_ctx.HasTargetScope()) {
    disassembly_format =
        exe_ctx.GetTargetRef().GetDebugger().GetDisassemblyFormat();
  } else {
    FormatEntity::Parse("${addr}: ", format);
    disassembly_format = &format;
  }

  // First pass: step through the list of instructions, find how long the
  // initial addresses strings are, insert padding in the second pass so the
  // opcodes all line up nicely.

  // Also build up the source line mapping if this is mixed source & assembly
  // mode. Calculate the source line for each assembly instruction (eliding
  // inlined functions which the user wants to skip).

  std::map<FileSpec, std::set<uint32_t>> source_lines_seen;
  Symbol *previous_symbol = nullptr;

  size_t address_text_size = 0;
  for (size_t i = 0; i < num_instructions_found; ++i) {
    Instruction *inst =
        disasm_ptr->GetInstructionList().GetInstructionAtIndex(i).get();
    if (inst) {
      const Address &addr = inst->GetAddress();
      ModuleSP module_sp(addr.GetModule());
      if (module_sp) {
        const SymbolContextItem resolve_mask = eSymbolContextFunction |
                                               eSymbolContextSymbol |
                                               eSymbolContextLineEntry;
        uint32_t resolved_mask =
            module_sp->ResolveSymbolContextForAddress(addr, resolve_mask, sc);
        if (resolved_mask) {
          StreamString strmstr;
          Debugger::FormatDisassemblerAddress(disassembly_format, &sc, nullptr,
                                              &exe_ctx, &addr, strmstr);
          size_t cur_line = strmstr.GetSizeOfLastLine();
          if (cur_line > address_text_size)
            address_text_size = cur_line;

          // Add entries to our "source_lines_seen" map+set which list which
          // sources lines occur in this disassembly session.  We will print
          // lines of context around a source line, but we don't want to print
          // a source line that has a line table entry of its own - we'll leave
          // that source line to be printed when it actually occurs in the
          // disassembly.

          if (mixed_source_and_assembly && sc.line_entry.IsValid()) {
            if (sc.symbol != previous_symbol) {
              SourceLine decl_line = GetFunctionDeclLineEntry(sc);
              if (!ElideMixedSourceAndDisassemblyLine(exe_ctx, sc, decl_line))
                AddLineToSourceLineTables(decl_line, source_lines_seen);
            }
            if (sc.line_entry.IsValid()) {
              SourceLine this_line;
              this_line.file = sc.line_entry.file;
              this_line.line = sc.line_entry.line;
              this_line.column = sc.line_entry.column;
              if (!ElideMixedSourceAndDisassemblyLine(exe_ctx, sc, this_line))
                AddLineToSourceLineTables(this_line, source_lines_seen);
            }
          }
        }
        sc.Clear(false);
      }
    }
  }

  previous_symbol = nullptr;
  SourceLine previous_line;
  for (size_t i = 0; i < num_instructions_found; ++i) {
    Instruction *inst =
        disasm_ptr->GetInstructionList().GetInstructionAtIndex(i).get();

    if (inst) {
      const Address &addr = inst->GetAddress();
      const bool inst_is_at_pc = pc_addr_ptr && addr == *pc_addr_ptr;
      SourceLinesToDisplay source_lines_to_display;

      prev_sc = sc;

      ModuleSP module_sp(addr.GetModule());
      if (module_sp) {
        uint32_t resolved_mask = module_sp->ResolveSymbolContextForAddress(
            addr, eSymbolContextEverything, sc);
        if (resolved_mask) {
          if (mixed_source_and_assembly) {

            // If we've started a new function (non-inlined), print all of the
            // source lines from the function declaration until the first line
            // table entry - typically the opening curly brace of the function.
            if (previous_symbol != sc.symbol) {
              // The default disassembly format puts an extra blank line
              // between functions - so when we're displaying the source
              // context for a function, we don't want to add a blank line
              // after the source context or we'll end up with two of them.
              if (previous_symbol != nullptr)
                source_lines_to_display.print_source_context_end_eol = false;

              previous_symbol = sc.symbol;
              if (sc.function && sc.line_entry.IsValid()) {
                LineEntry prologue_end_line = sc.line_entry;
                if (!ElideMixedSourceAndDisassemblyLine(exe_ctx, sc,
                                                        prologue_end_line)) {
                  FileSpec func_decl_file;
                  uint32_t func_decl_line;
                  sc.function->GetStartLineSourceInfo(func_decl_file,
                                                      func_decl_line);
                  if (func_decl_file == prologue_end_line.file ||
                      func_decl_file == prologue_end_line.original_file) {
                    // Add all the lines between the function declaration and
                    // the first non-prologue source line to the list of lines
                    // to print.
                    for (uint32_t lineno = func_decl_line;
                         lineno <= prologue_end_line.line; lineno++) {
                      SourceLine this_line;
                      this_line.file = func_decl_file;
                      this_line.line = lineno;
                      source_lines_to_display.lines.push_back(this_line);
                    }
                    // Mark the last line as the "current" one.  Usually this
                    // is the open curly brace.
                    if (source_lines_to_display.lines.size() > 0)
                      source_lines_to_display.current_source_line =
                          source_lines_to_display.lines.size() - 1;
                  }
                }
              }
              sc.GetAddressRange(scope, 0, use_inline_block_range,
                                 current_source_line_range);
            }

            // If we've left a previous source line's address range, print a
            // new source line
            if (!current_source_line_range.ContainsFileAddress(addr)) {
              sc.GetAddressRange(scope, 0, use_inline_block_range,
                                 current_source_line_range);

              if (sc != prev_sc && sc.comp_unit && sc.line_entry.IsValid()) {
                SourceLine this_line;
                this_line.file = sc.line_entry.file;
                this_line.line = sc.line_entry.line;

                if (!ElideMixedSourceAndDisassemblyLine(exe_ctx, sc,
                                                        this_line)) {
                  // Only print this source line if it is different from the
                  // last source line we printed.  There may have been inlined
                  // functions between these lines that we elided, resulting in
                  // the same line being printed twice in a row for a
                  // contiguous block of assembly instructions.
                  if (this_line != previous_line) {

                    std::vector<uint32_t> previous_lines;
                    for (uint32_t i = 0;
                         i < num_mixed_context_lines &&
                         (this_line.line - num_mixed_context_lines) > 0;
                         i++) {
                      uint32_t line =
                          this_line.line - num_mixed_context_lines + i;
                      auto pos = source_lines_seen.find(this_line.file);
                      if (pos != source_lines_seen.end()) {
                        if (pos->second.count(line) == 1) {
                          previous_lines.clear();
                        } else {
                          previous_lines.push_back(line);
                        }
                      }
                    }
                    for (size_t i = 0; i < previous_lines.size(); i++) {
                      SourceLine previous_line;
                      previous_line.file = this_line.file;
                      previous_line.line = previous_lines[i];
                      auto pos = source_lines_seen.find(previous_line.file);
                      if (pos != source_lines_seen.end()) {
                        pos->second.insert(previous_line.line);
                      }
                      source_lines_to_display.lines.push_back(previous_line);
                    }

                    source_lines_to_display.lines.push_back(this_line);
                    source_lines_to_display.current_source_line =
                        source_lines_to_display.lines.size() - 1;

                    for (uint32_t i = 0; i < num_mixed_context_lines; i++) {
                      SourceLine next_line;
                      next_line.file = this_line.file;
                      next_line.line = this_line.line + i + 1;
                      auto pos = source_lines_seen.find(next_line.file);
                      if (pos != source_lines_seen.end()) {
                        if (pos->second.count(next_line.line) == 1)
                          break;
                        pos->second.insert(next_line.line);
                      }
                      source_lines_to_display.lines.push_back(next_line);
                    }
                  }
                  previous_line = this_line;
                }
              }
            }
          }
        } else {
          sc.Clear(true);
        }
      }

      if (source_lines_to_display.lines.size() > 0) {
        strm.EOL();
        for (size_t idx = 0; idx < source_lines_to_display.lines.size();
             idx++) {
          SourceLine ln = source_lines_to_display.lines[idx];
          const char *line_highlight = "";
          if (inst_is_at_pc && (options & eOptionMarkPCSourceLine)) {
            line_highlight = "->";
          } else if (idx == source_lines_to_display.current_source_line) {
            line_highlight = "**";
          }
          source_manager.DisplaySourceLinesWithLineNumbers(
              ln.file, ln.line, ln.column, 0, 0, line_highlight, &strm);
        }
        if (source_lines_to_display.print_source_context_end_eol)
          strm.EOL();
      }

      const bool show_bytes = (options & eOptionShowBytes) != 0;
      inst->Dump(&strm, max_opcode_byte_size, true, show_bytes, &exe_ctx, &sc,
                 &prev_sc, nullptr, address_text_size);
      strm.EOL();
    } else {
      break;
    }
  }

  return true;
}

bool Disassembler::Disassemble(Debugger &debugger, const ArchSpec &arch,
                               const char *plugin_name, const char *flavor,
                               const ExecutionContext &exe_ctx,
                               uint32_t num_instructions,
                               bool mixed_source_and_assembly,
                               uint32_t num_mixed_context_lines,
                               uint32_t options, Stream &strm) {
  AddressRange range;
  StackFrame *frame = exe_ctx.GetFramePtr();
  if (frame) {
    SymbolContext sc(
        frame->GetSymbolContext(eSymbolContextFunction | eSymbolContextSymbol));
    if (sc.function) {
      range = sc.function->GetAddressRange();
    } else if (sc.symbol && sc.symbol->ValueIsAddress()) {
      range.GetBaseAddress() = sc.symbol->GetAddressRef();
      range.SetByteSize(sc.symbol->GetByteSize());
    } else {
      range.GetBaseAddress() = frame->GetFrameCodeAddress();
    }

    if (range.GetBaseAddress().IsValid() && range.GetByteSize() == 0)
      range.SetByteSize(DEFAULT_DISASM_BYTE_SIZE);
  }

  return Disassemble(debugger, arch, plugin_name, flavor, exe_ctx, range,
                     num_instructions, mixed_source_and_assembly,
                     num_mixed_context_lines, options, strm);
}

Instruction::Instruction(const Address &address, AddressClass addr_class)
    : m_address(address), m_address_class(addr_class), m_opcode(),
      m_calculated_strings(false) {}

Instruction::~Instruction() = default;

AddressClass Instruction::GetAddressClass() {
  if (m_address_class == AddressClass::eInvalid)
    m_address_class = m_address.GetAddressClass();
  return m_address_class;
}

void Instruction::Dump(lldb_private::Stream *s, uint32_t max_opcode_byte_size,
                       bool show_address, bool show_bytes,
                       const ExecutionContext *exe_ctx,
                       const SymbolContext *sym_ctx,
                       const SymbolContext *prev_sym_ctx,
                       const FormatEntity::Entry *disassembly_addr_format,
                       size_t max_address_text_size) {
  size_t opcode_column_width = 7;
  const size_t operand_column_width = 25;

  CalculateMnemonicOperandsAndCommentIfNeeded(exe_ctx);

  StreamString ss;

  if (show_address) {
    Debugger::FormatDisassemblerAddress(disassembly_addr_format, sym_ctx,
                                        prev_sym_ctx, exe_ctx, &m_address, ss);
    ss.FillLastLineToColumn(max_address_text_size, ' ');
  }

  if (show_bytes) {
    if (m_opcode.GetType() == Opcode::eTypeBytes) {
      // x86_64 and i386 are the only ones that use bytes right now so pad out
      // the byte dump to be able to always show 15 bytes (3 chars each) plus a
      // space
      if (max_opcode_byte_size > 0)
        m_opcode.Dump(&ss, max_opcode_byte_size * 3 + 1);
      else
        m_opcode.Dump(&ss, 15 * 3 + 1);
    } else {
      // Else, we have ARM or MIPS which can show up to a uint32_t 0x00000000
      // (10 spaces) plus two for padding...
      if (max_opcode_byte_size > 0)
        m_opcode.Dump(&ss, max_opcode_byte_size * 3 + 1);
      else
        m_opcode.Dump(&ss, 12);
    }
  }

  const size_t opcode_pos = ss.GetSizeOfLastLine();

  // The default opcode size of 7 characters is plenty for most architectures
  // but some like arm can pull out the occasional vqrshrun.s16.  We won't get
  // consistent column spacing in these cases, unfortunately.
  if (m_opcode_name.length() >= opcode_column_width) {
    opcode_column_width = m_opcode_name.length() + 1;
  }

  ss.PutCString(m_opcode_name);
  ss.FillLastLineToColumn(opcode_pos + opcode_column_width, ' ');
  ss.PutCString(m_mnemonics);

  if (!m_comment.empty()) {
    ss.FillLastLineToColumn(
        opcode_pos + opcode_column_width + operand_column_width, ' ');
    ss.PutCString(" ; ");
    ss.PutCString(m_comment);
  }
  s->PutCString(ss.GetString());
}

bool Instruction::DumpEmulation(const ArchSpec &arch) {
  std::unique_ptr<EmulateInstruction> insn_emulator_ap(
      EmulateInstruction::FindPlugin(arch, eInstructionTypeAny, nullptr));
  if (insn_emulator_ap) {
    insn_emulator_ap->SetInstruction(GetOpcode(), GetAddress(), nullptr);
    return insn_emulator_ap->EvaluateInstruction(0);
  }

  return false;
}

bool Instruction::CanSetBreakpoint () {
  return !HasDelaySlot();
}

bool Instruction::HasDelaySlot() {
  // Default is false.
  return false;
}

OptionValueSP Instruction::ReadArray(FILE *in_file, Stream *out_stream,
                                     OptionValue::Type data_type) {
  bool done = false;
  char buffer[1024];

  auto option_value_sp = std::make_shared<OptionValueArray>(1u << data_type);

  int idx = 0;
  while (!done) {
    if (!fgets(buffer, 1023, in_file)) {
      out_stream->Printf(
          "Instruction::ReadArray:  Error reading file (fgets).\n");
      option_value_sp.reset();
      return option_value_sp;
    }

    std::string line(buffer);

    size_t len = line.size();
    if (line[len - 1] == '\n') {
      line[len - 1] = '\0';
      line.resize(len - 1);
    }

    if ((line.size() == 1) && line[0] == ']') {
      done = true;
      line.clear();
    }

    if (!line.empty()) {
      std::string value;
      static RegularExpression g_reg_exp(
          llvm::StringRef("^[ \t]*([^ \t]+)[ \t]*$"));
      RegularExpression::Match regex_match(1);
      bool reg_exp_success = g_reg_exp.Execute(line, &regex_match);
      if (reg_exp_success)
        regex_match.GetMatchAtIndex(line.c_str(), 1, value);
      else
        value = line;

      OptionValueSP data_value_sp;
      switch (data_type) {
      case OptionValue::eTypeUInt64:
        data_value_sp = std::make_shared<OptionValueUInt64>(0, 0);
        data_value_sp->SetValueFromString(value);
        break;
      // Other types can be added later as needed.
      default:
        data_value_sp = std::make_shared<OptionValueString>(value.c_str(), "");
        break;
      }

      option_value_sp->GetAsArray()->InsertValue(idx, data_value_sp);
      ++idx;
    }
  }

  return option_value_sp;
}

OptionValueSP Instruction::ReadDictionary(FILE *in_file, Stream *out_stream) {
  bool done = false;
  char buffer[1024];

  auto option_value_sp = std::make_shared<OptionValueDictionary>();
  static ConstString encoding_key("data_encoding");
  OptionValue::Type data_type = OptionValue::eTypeInvalid;

  while (!done) {
    // Read the next line in the file
    if (!fgets(buffer, 1023, in_file)) {
      out_stream->Printf(
          "Instruction::ReadDictionary: Error reading file (fgets).\n");
      option_value_sp.reset();
      return option_value_sp;
    }

    // Check to see if the line contains the end-of-dictionary marker ("}")
    std::string line(buffer);

    size_t len = line.size();
    if (line[len - 1] == '\n') {
      line[len - 1] = '\0';
      line.resize(len - 1);
    }

    if ((line.size() == 1) && (line[0] == '}')) {
      done = true;
      line.clear();
    }

    // Try to find a key-value pair in the current line and add it to the
    // dictionary.
    if (!line.empty()) {
      static RegularExpression g_reg_exp(llvm::StringRef(
          "^[ \t]*([a-zA-Z_][a-zA-Z0-9_]*)[ \t]*=[ \t]*(.*)[ \t]*$"));
      RegularExpression::Match regex_match(2);

      bool reg_exp_success = g_reg_exp.Execute(line, &regex_match);
      std::string key;
      std::string value;
      if (reg_exp_success) {
        regex_match.GetMatchAtIndex(line.c_str(), 1, key);
        regex_match.GetMatchAtIndex(line.c_str(), 2, value);
      } else {
        out_stream->Printf("Instruction::ReadDictionary: Failure executing "
                           "regular expression.\n");
        option_value_sp.reset();
        return option_value_sp;
      }

      ConstString const_key(key.c_str());
      // Check value to see if it's the start of an array or dictionary.

      lldb::OptionValueSP value_sp;
      assert(value.empty() == false);
      assert(key.empty() == false);

      if (value[0] == '{') {
        assert(value.size() == 1);
        // value is a dictionary
        value_sp = ReadDictionary(in_file, out_stream);
        if (!value_sp) {
          option_value_sp.reset();
          return option_value_sp;
        }
      } else if (value[0] == '[') {
        assert(value.size() == 1);
        // value is an array
        value_sp = ReadArray(in_file, out_stream, data_type);
        if (!value_sp) {
          option_value_sp.reset();
          return option_value_sp;
        }
        // We've used the data_type to read an array; re-set the type to
        // Invalid
        data_type = OptionValue::eTypeInvalid;
      } else if ((value[0] == '0') && (value[1] == 'x')) {
        value_sp = std::make_shared<OptionValueUInt64>(0, 0);
        value_sp->SetValueFromString(value);
      } else {
        size_t len = value.size();
        if ((value[0] == '"') && (value[len - 1] == '"'))
          value = value.substr(1, len - 2);
        value_sp = std::make_shared<OptionValueString>(value.c_str(), "");
      }

      if (const_key == encoding_key) {
        // A 'data_encoding=..." is NOT a normal key-value pair; it is meta-data
        // indicating the
        // data type of an upcoming array (usually the next bit of data to be
        // read in).
        if (strcmp(value.c_str(), "uint32_t") == 0)
          data_type = OptionValue::eTypeUInt64;
      } else
        option_value_sp->GetAsDictionary()->SetValueForKey(const_key, value_sp,
                                                           false);
    }
  }

  return option_value_sp;
}

bool Instruction::TestEmulation(Stream *out_stream, const char *file_name) {
  if (!out_stream)
    return false;

  if (!file_name) {
    out_stream->Printf("Instruction::TestEmulation:  Missing file_name.");
    return false;
  }
  FILE *test_file = FileSystem::Instance().Fopen(file_name, "r");
  if (!test_file) {
    out_stream->Printf(
        "Instruction::TestEmulation: Attempt to open test file failed.");
    return false;
  }

  char buffer[256];
  if (!fgets(buffer, 255, test_file)) {
    out_stream->Printf(
        "Instruction::TestEmulation: Error reading first line of test file.\n");
    fclose(test_file);
    return false;
  }

  if (strncmp(buffer, "InstructionEmulationState={", 27) != 0) {
    out_stream->Printf("Instructin::TestEmulation: Test file does not contain "
                       "emulation state dictionary\n");
    fclose(test_file);
    return false;
  }

  // Read all the test information from the test file into an
  // OptionValueDictionary.

  OptionValueSP data_dictionary_sp(ReadDictionary(test_file, out_stream));
  if (!data_dictionary_sp) {
    out_stream->Printf(
        "Instruction::TestEmulation:  Error reading Dictionary Object.\n");
    fclose(test_file);
    return false;
  }

  fclose(test_file);

  OptionValueDictionary *data_dictionary =
      data_dictionary_sp->GetAsDictionary();
  static ConstString description_key("assembly_string");
  static ConstString triple_key("triple");

  OptionValueSP value_sp = data_dictionary->GetValueForKey(description_key);

  if (!value_sp) {
    out_stream->Printf("Instruction::TestEmulation:  Test file does not "
                       "contain description string.\n");
    return false;
  }

  SetDescription(value_sp->GetStringValue());

  value_sp = data_dictionary->GetValueForKey(triple_key);
  if (!value_sp) {
    out_stream->Printf(
        "Instruction::TestEmulation: Test file does not contain triple.\n");
    return false;
  }

  ArchSpec arch;
  arch.SetTriple(llvm::Triple(value_sp->GetStringValue()));

  bool success = false;
  std::unique_ptr<EmulateInstruction> insn_emulator_ap(
      EmulateInstruction::FindPlugin(arch, eInstructionTypeAny, nullptr));
  if (insn_emulator_ap)
    success =
        insn_emulator_ap->TestEmulation(out_stream, arch, data_dictionary);

  if (success)
    out_stream->Printf("Emulation test succeeded.");
  else
    out_stream->Printf("Emulation test failed.");

  return success;
}

bool Instruction::Emulate(
    const ArchSpec &arch, uint32_t evaluate_options, void *baton,
    EmulateInstruction::ReadMemoryCallback read_mem_callback,
    EmulateInstruction::WriteMemoryCallback write_mem_callback,
    EmulateInstruction::ReadRegisterCallback read_reg_callback,
    EmulateInstruction::WriteRegisterCallback write_reg_callback) {
  std::unique_ptr<EmulateInstruction> insn_emulator_ap(
      EmulateInstruction::FindPlugin(arch, eInstructionTypeAny, nullptr));
  if (insn_emulator_ap) {
    insn_emulator_ap->SetBaton(baton);
    insn_emulator_ap->SetCallbacks(read_mem_callback, write_mem_callback,
                                   read_reg_callback, write_reg_callback);
    insn_emulator_ap->SetInstruction(GetOpcode(), GetAddress(), nullptr);
    return insn_emulator_ap->EvaluateInstruction(evaluate_options);
  }

  return false;
}

uint32_t Instruction::GetData(DataExtractor &data) {
  return m_opcode.GetData(data);
}

InstructionList::InstructionList() : m_instructions() {}

InstructionList::~InstructionList() = default;

size_t InstructionList::GetSize() const { return m_instructions.size(); }

uint32_t InstructionList::GetMaxOpcocdeByteSize() const {
  uint32_t max_inst_size = 0;
  collection::const_iterator pos, end;
  for (pos = m_instructions.begin(), end = m_instructions.end(); pos != end;
       ++pos) {
    uint32_t inst_size = (*pos)->GetOpcode().GetByteSize();
    if (max_inst_size < inst_size)
      max_inst_size = inst_size;
  }
  return max_inst_size;
}

InstructionSP InstructionList::GetInstructionAtIndex(size_t idx) const {
  InstructionSP inst_sp;
  if (idx < m_instructions.size())
    inst_sp = m_instructions[idx];
  return inst_sp;
}

void InstructionList::Dump(Stream *s, bool show_address, bool show_bytes,
                           const ExecutionContext *exe_ctx) {
  const uint32_t max_opcode_byte_size = GetMaxOpcocdeByteSize();
  collection::const_iterator pos, begin, end;

  const FormatEntity::Entry *disassembly_format = nullptr;
  FormatEntity::Entry format;
  if (exe_ctx && exe_ctx->HasTargetScope()) {
    disassembly_format =
        exe_ctx->GetTargetRef().GetDebugger().GetDisassemblyFormat();
  } else {
    FormatEntity::Parse("${addr}: ", format);
    disassembly_format = &format;
  }

  for (begin = m_instructions.begin(), end = m_instructions.end(), pos = begin;
       pos != end; ++pos) {
    if (pos != begin)
      s->EOL();
    (*pos)->Dump(s, max_opcode_byte_size, show_address, show_bytes, exe_ctx,
                 nullptr, nullptr, disassembly_format, 0);
  }
}

void InstructionList::Clear() { m_instructions.clear(); }

void InstructionList::Append(lldb::InstructionSP &inst_sp) {
  if (inst_sp)
    m_instructions.push_back(inst_sp);
}

uint32_t
InstructionList::GetIndexOfNextBranchInstruction(uint32_t start,
                                                 Target &target) const {
  size_t num_instructions = m_instructions.size();

  uint32_t next_branch = UINT32_MAX;
  size_t i;
  for (i = start; i < num_instructions; i++) {
    if (m_instructions[i]->DoesBranch()) {
      next_branch = i;
      break;
    }
  }

  // Hexagon needs the first instruction of the packet with the branch. Go
  // backwards until we find an instruction marked end-of-packet, or until we
  // hit start.
  if (target.GetArchitecture().GetTriple().getArch() == llvm::Triple::hexagon) {
    // If we didn't find a branch, find the last packet start.
    if (next_branch == UINT32_MAX) {
      i = num_instructions - 1;
    }

    while (i > start) {
      --i;

      Status error;
      uint32_t inst_bytes;
      bool prefer_file_cache = false; // Read from process if process is running
      lldb::addr_t load_addr = LLDB_INVALID_ADDRESS;
      target.ReadMemory(m_instructions[i]->GetAddress(), prefer_file_cache,
                        &inst_bytes, sizeof(inst_bytes), error, &load_addr);
      // If we have an error reading memory, return start
      if (!error.Success())
        return start;
      // check if this is the last instruction in a packet bits 15:14 will be
      // 11b or 00b for a duplex
      if (((inst_bytes & 0xC000) == 0xC000) ||
          ((inst_bytes & 0xC000) == 0x0000)) {
        // instruction after this should be the start of next packet
        next_branch = i + 1;
        break;
      }
    }

    if (next_branch == UINT32_MAX) {
      // We couldn't find the previous packet, so return start
      next_branch = start;
    }
  }
  return next_branch;
}

uint32_t
InstructionList::GetIndexOfInstructionAtAddress(const Address &address) {
  size_t num_instructions = m_instructions.size();
  uint32_t index = UINT32_MAX;
  for (size_t i = 0; i < num_instructions; i++) {
    if (m_instructions[i]->GetAddress() == address) {
      index = i;
      break;
    }
  }
  return index;
}

uint32_t
InstructionList::GetIndexOfInstructionAtLoadAddress(lldb::addr_t load_addr,
                                                    Target &target) {
  Address address;
  address.SetLoadAddress(load_addr, &target);
  return GetIndexOfInstructionAtAddress(address);
}

size_t Disassembler::ParseInstructions(const ExecutionContext *exe_ctx,
                                       const AddressRange &range,
                                       Stream *error_strm_ptr,
                                       bool prefer_file_cache) {
  if (exe_ctx) {
    Target *target = exe_ctx->GetTargetPtr();
    const addr_t byte_size = range.GetByteSize();
    if (target == nullptr || byte_size == 0 ||
        !range.GetBaseAddress().IsValid())
      return 0;

    auto data_sp = std::make_shared<DataBufferHeap>(byte_size, '\0');

    Status error;
    lldb::addr_t load_addr = LLDB_INVALID_ADDRESS;
    const size_t bytes_read = target->ReadMemory(
        range.GetBaseAddress(), prefer_file_cache, data_sp->GetBytes(),
        data_sp->GetByteSize(), error, &load_addr);

    if (bytes_read > 0) {
      if (bytes_read != data_sp->GetByteSize())
        data_sp->SetByteSize(bytes_read);
      DataExtractor data(data_sp, m_arch.GetByteOrder(),
                         m_arch.GetAddressByteSize());
      const bool data_from_file = load_addr == LLDB_INVALID_ADDRESS;
      return DecodeInstructions(range.GetBaseAddress(), data, 0, UINT32_MAX,
                                false, data_from_file);
    } else if (error_strm_ptr) {
      const char *error_cstr = error.AsCString();
      if (error_cstr) {
        error_strm_ptr->Printf("error: %s\n", error_cstr);
      }
    }
  } else if (error_strm_ptr) {
    error_strm_ptr->PutCString("error: invalid execution context\n");
  }
  return 0;
}

size_t Disassembler::ParseInstructions(const ExecutionContext *exe_ctx,
                                       const Address &start,
                                       uint32_t num_instructions,
                                       bool prefer_file_cache) {
  m_instruction_list.Clear();

  if (exe_ctx == nullptr || num_instructions == 0 || !start.IsValid())
    return 0;

  Target *target = exe_ctx->GetTargetPtr();
  // Calculate the max buffer size we will need in order to disassemble
  const addr_t byte_size = num_instructions * m_arch.GetMaximumOpcodeByteSize();

  if (target == nullptr || byte_size == 0)
    return 0;

  DataBufferHeap *heap_buffer = new DataBufferHeap(byte_size, '\0');
  DataBufferSP data_sp(heap_buffer);

  Status error;
  lldb::addr_t load_addr = LLDB_INVALID_ADDRESS;
  const size_t bytes_read =
      target->ReadMemory(start, prefer_file_cache, heap_buffer->GetBytes(),
                         byte_size, error, &load_addr);

  const bool data_from_file = load_addr == LLDB_INVALID_ADDRESS;

  if (bytes_read == 0)
    return 0;
  DataExtractor data(data_sp, m_arch.GetByteOrder(),
                     m_arch.GetAddressByteSize());

  const bool append_instructions = true;
  DecodeInstructions(start, data, 0, num_instructions, append_instructions,
                     data_from_file);

  return m_instruction_list.GetSize();
}

//----------------------------------------------------------------------
// Disassembler copy constructor
//----------------------------------------------------------------------
Disassembler::Disassembler(const ArchSpec &arch, const char *flavor)
    : m_arch(arch), m_instruction_list(), m_base_addr(LLDB_INVALID_ADDRESS),
      m_flavor() {
  if (flavor == nullptr)
    m_flavor.assign("default");
  else
    m_flavor.assign(flavor);

  // If this is an arm variant that can only include thumb (T16, T32)
  // instructions, force the arch triple to be "thumbv.." instead of "armv..."
  if (arch.IsAlwaysThumbInstructions()) {
    std::string thumb_arch_name(arch.GetTriple().getArchName().str());
    // Replace "arm" with "thumb" so we get all thumb variants correct
    if (thumb_arch_name.size() > 3) {
      thumb_arch_name.erase(0, 3);
      thumb_arch_name.insert(0, "thumb");
    }
    m_arch.SetTriple(thumb_arch_name.c_str());
  }
}

Disassembler::~Disassembler() = default;

InstructionList &Disassembler::GetInstructionList() {
  return m_instruction_list;
}

const InstructionList &Disassembler::GetInstructionList() const {
  return m_instruction_list;
}

//----------------------------------------------------------------------
// Class PseudoInstruction
//----------------------------------------------------------------------

PseudoInstruction::PseudoInstruction()
    : Instruction(Address(), AddressClass::eUnknown), m_description() {}

PseudoInstruction::~PseudoInstruction() = default;

bool PseudoInstruction::DoesBranch() {
  // This is NOT a valid question for a pseudo instruction.
  return false;
}

bool PseudoInstruction::HasDelaySlot() {
  // This is NOT a valid question for a pseudo instruction.
  return false;
}

size_t PseudoInstruction::Decode(const lldb_private::Disassembler &disassembler,
                                 const lldb_private::DataExtractor &data,
                                 lldb::offset_t data_offset) {
  return m_opcode.GetByteSize();
}

void PseudoInstruction::SetOpcode(size_t opcode_size, void *opcode_data) {
  if (!opcode_data)
    return;

  switch (opcode_size) {
  case 8: {
    uint8_t value8 = *((uint8_t *)opcode_data);
    m_opcode.SetOpcode8(value8, eByteOrderInvalid);
    break;
  }
  case 16: {
    uint16_t value16 = *((uint16_t *)opcode_data);
    m_opcode.SetOpcode16(value16, eByteOrderInvalid);
    break;
  }
  case 32: {
    uint32_t value32 = *((uint32_t *)opcode_data);
    m_opcode.SetOpcode32(value32, eByteOrderInvalid);
    break;
  }
  case 64: {
    uint64_t value64 = *((uint64_t *)opcode_data);
    m_opcode.SetOpcode64(value64, eByteOrderInvalid);
    break;
  }
  default:
    break;
  }
}

void PseudoInstruction::SetDescription(llvm::StringRef description) {
  m_description = description;
}

Instruction::Operand Instruction::Operand::BuildRegister(ConstString &r) {
  Operand ret;
  ret.m_type = Type::Register;
  ret.m_register = r;
  return ret;
}

Instruction::Operand Instruction::Operand::BuildImmediate(lldb::addr_t imm,
                                                          bool neg) {
  Operand ret;
  ret.m_type = Type::Immediate;
  ret.m_immediate = imm;
  ret.m_negative = neg;
  return ret;
}

Instruction::Operand Instruction::Operand::BuildImmediate(int64_t imm) {
  Operand ret;
  ret.m_type = Type::Immediate;
  if (imm < 0) {
    ret.m_immediate = -imm;
    ret.m_negative = true;
  } else {
    ret.m_immediate = imm;
    ret.m_negative = false;
  }
  return ret;
}

Instruction::Operand
Instruction::Operand::BuildDereference(const Operand &ref) {
  Operand ret;
  ret.m_type = Type::Dereference;
  ret.m_children = {ref};
  return ret;
}

Instruction::Operand Instruction::Operand::BuildSum(const Operand &lhs,
                                                    const Operand &rhs) {
  Operand ret;
  ret.m_type = Type::Sum;
  ret.m_children = {lhs, rhs};
  return ret;
}

Instruction::Operand Instruction::Operand::BuildProduct(const Operand &lhs,
                                                        const Operand &rhs) {
  Operand ret;
  ret.m_type = Type::Product;
  ret.m_children = {lhs, rhs};
  return ret;
}

std::function<bool(const Instruction::Operand &)>
lldb_private::OperandMatchers::MatchBinaryOp(
    std::function<bool(const Instruction::Operand &)> base,
    std::function<bool(const Instruction::Operand &)> left,
    std::function<bool(const Instruction::Operand &)> right) {
  return [base, left, right](const Instruction::Operand &op) -> bool {
    return (base(op) && op.m_children.size() == 2 &&
            ((left(op.m_children[0]) && right(op.m_children[1])) ||
             (left(op.m_children[1]) && right(op.m_children[0]))));
  };
}

std::function<bool(const Instruction::Operand &)>
lldb_private::OperandMatchers::MatchUnaryOp(
    std::function<bool(const Instruction::Operand &)> base,
    std::function<bool(const Instruction::Operand &)> child) {
  return [base, child](const Instruction::Operand &op) -> bool {
    return (base(op) && op.m_children.size() == 1 && child(op.m_children[0]));
  };
}

std::function<bool(const Instruction::Operand &)>
lldb_private::OperandMatchers::MatchRegOp(const RegisterInfo &info) {
  return [&info](const Instruction::Operand &op) {
    return (op.m_type == Instruction::Operand::Type::Register &&
            (op.m_register == ConstString(info.name) ||
             op.m_register == ConstString(info.alt_name)));
  };
}

std::function<bool(const Instruction::Operand &)>
lldb_private::OperandMatchers::FetchRegOp(ConstString &reg) {
  return [&reg](const Instruction::Operand &op) {
    if (op.m_type != Instruction::Operand::Type::Register) {
      return false;
    }
    reg = op.m_register;
    return true;
  };
}

std::function<bool(const Instruction::Operand &)>
lldb_private::OperandMatchers::MatchImmOp(int64_t imm) {
  return [imm](const Instruction::Operand &op) {
    return (op.m_type == Instruction::Operand::Type::Immediate &&
            ((op.m_negative && op.m_immediate == (uint64_t)-imm) ||
             (!op.m_negative && op.m_immediate == (uint64_t)imm)));
  };
}

std::function<bool(const Instruction::Operand &)>
lldb_private::OperandMatchers::FetchImmOp(int64_t &imm) {
  return [&imm](const Instruction::Operand &op) {
    if (op.m_type != Instruction::Operand::Type::Immediate) {
      return false;
    }
    if (op.m_negative) {
      imm = -((int64_t)op.m_immediate);
    } else {
      imm = ((int64_t)op.m_immediate);
    }
    return true;
  };
}

std::function<bool(const Instruction::Operand &)>
lldb_private::OperandMatchers::MatchOpType(Instruction::Operand::Type type) {
  return [type](const Instruction::Operand &op) { return op.m_type == type; };
}
