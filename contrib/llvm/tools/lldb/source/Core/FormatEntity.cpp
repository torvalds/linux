//===-- FormatEntity.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/FormatEntity.h"

#include "lldb/Core/Address.h"
#include "lldb/Core/AddressRange.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/DumpRegisterValue.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectVariable.h"
#include "lldb/DataFormatters/DataVisualization.h"
#include "lldb/DataFormatters/FormatClasses.h"
#include "lldb/DataFormatters/FormatManager.h"
#include "lldb/DataFormatters/TypeSummary.h"
#include "lldb/Expression/ExpressionVariable.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Symbol/Block.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/LineEntry.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/ExecutionContextScope.h"
#include "lldb/Target/Language.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/AnsiTerminal.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Logging.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/SharingPtr.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/StringList.h"
#include "lldb/Utility/StructuredData.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-forward.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Support/Compiler.h"

#include <ctype.h>
#include <inttypes.h>
#include <memory>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <type_traits>
#include <utility>

namespace lldb_private {
class ScriptInterpreter;
}
namespace lldb_private {
struct RegisterInfo;
}

using namespace lldb;
using namespace lldb_private;

enum FileKind { FileError = 0, Basename, Dirname, Fullpath };

#define ENTRY(n, t, f)                                                         \
  {                                                                            \
    n, nullptr, FormatEntity::Entry::Type::t,                                  \
        FormatEntity::Entry::FormatType::f, 0, 0, nullptr, false               \
  }
#define ENTRY_VALUE(n, t, f, v)                                                \
  {                                                                            \
    n, nullptr, FormatEntity::Entry::Type::t,                                  \
        FormatEntity::Entry::FormatType::f, v, 0, nullptr, false               \
  }
#define ENTRY_CHILDREN(n, t, f, c)                                             \
  {                                                                            \
    n, nullptr, FormatEntity::Entry::Type::t,                                  \
        FormatEntity::Entry::FormatType::f, 0,                                 \
        static_cast<uint32_t>(llvm::array_lengthof(c)), c, false               \
  }
#define ENTRY_CHILDREN_KEEP_SEP(n, t, f, c)                                    \
  {                                                                            \
    n, nullptr, FormatEntity::Entry::Type::t,                                  \
        FormatEntity::Entry::FormatType::f, 0,                                 \
        static_cast<uint32_t>(llvm::array_lengthof(c)), c, true                \
  }
#define ENTRY_STRING(n, s)                                                     \
  {                                                                            \
    n, s, FormatEntity::Entry::Type::InsertString,                             \
        FormatEntity::Entry::FormatType::None, 0, 0, nullptr, false            \
  }
static FormatEntity::Entry::Definition g_string_entry[] = {
    ENTRY("*", ParentString, None)};

static FormatEntity::Entry::Definition g_addr_entries[] = {
    ENTRY("load", AddressLoad, UInt64), ENTRY("file", AddressFile, UInt64),
    ENTRY("load", AddressLoadOrFile, UInt64),
};

static FormatEntity::Entry::Definition g_file_child_entries[] = {
    ENTRY_VALUE("basename", ParentNumber, CString, FileKind::Basename),
    ENTRY_VALUE("dirname", ParentNumber, CString, FileKind::Dirname),
    ENTRY_VALUE("fullpath", ParentNumber, CString, FileKind::Fullpath)};

static FormatEntity::Entry::Definition g_frame_child_entries[] = {
    ENTRY("index", FrameIndex, UInt32),
    ENTRY("pc", FrameRegisterPC, UInt64),
    ENTRY("fp", FrameRegisterFP, UInt64),
    ENTRY("sp", FrameRegisterSP, UInt64),
    ENTRY("flags", FrameRegisterFlags, UInt64),
    ENTRY("no-debug", FrameNoDebug, None),
    ENTRY_CHILDREN("reg", FrameRegisterByName, UInt64, g_string_entry),
    ENTRY("is-artificial", FrameIsArtificial, UInt32),
};

static FormatEntity::Entry::Definition g_function_child_entries[] = {
    ENTRY("id", FunctionID, UInt64), ENTRY("name", FunctionName, CString),
    ENTRY("name-without-args", FunctionNameNoArgs, CString),
    ENTRY("name-with-args", FunctionNameWithArgs, CString),
    ENTRY("addr-offset", FunctionAddrOffset, UInt64),
    ENTRY("concrete-only-addr-offset-no-padding", FunctionAddrOffsetConcrete,
          UInt64),
    ENTRY("line-offset", FunctionLineOffset, UInt64),
    ENTRY("pc-offset", FunctionPCOffset, UInt64),
    ENTRY("initial-function", FunctionInitial, None),
    ENTRY("changed", FunctionChanged, None),
    ENTRY("is-optimized", FunctionIsOptimized, None)};

static FormatEntity::Entry::Definition g_line_child_entries[] = {
    ENTRY_CHILDREN("file", LineEntryFile, None, g_file_child_entries),
    ENTRY("number", LineEntryLineNumber, UInt32),
    ENTRY("column", LineEntryColumn, UInt32),
    ENTRY("start-addr", LineEntryStartAddress, UInt64),
    ENTRY("end-addr", LineEntryEndAddress, UInt64),
};

static FormatEntity::Entry::Definition g_module_child_entries[] = {
    ENTRY_CHILDREN("file", ModuleFile, None, g_file_child_entries),
};

static FormatEntity::Entry::Definition g_process_child_entries[] = {
    ENTRY("id", ProcessID, UInt64),
    ENTRY_VALUE("name", ProcessFile, CString, FileKind::Basename),
    ENTRY_CHILDREN("file", ProcessFile, None, g_file_child_entries),
};

static FormatEntity::Entry::Definition g_svar_child_entries[] = {
    ENTRY("*", ParentString, None)};

static FormatEntity::Entry::Definition g_var_child_entries[] = {
    ENTRY("*", ParentString, None)};

static FormatEntity::Entry::Definition g_thread_child_entries[] = {
    ENTRY("id", ThreadID, UInt64),
    ENTRY("protocol_id", ThreadProtocolID, UInt64),
    ENTRY("index", ThreadIndexID, UInt32),
    ENTRY_CHILDREN("info", ThreadInfo, None, g_string_entry),
    ENTRY("queue", ThreadQueue, CString),
    ENTRY("name", ThreadName, CString),
    ENTRY("stop-reason", ThreadStopReason, CString),
    ENTRY("return-value", ThreadReturnValue, CString),
    ENTRY("completed-expression", ThreadCompletedExpression, CString),
};

static FormatEntity::Entry::Definition g_target_child_entries[] = {
    ENTRY("arch", TargetArch, CString),
};

#define _TO_STR2(_val) #_val
#define _TO_STR(_val) _TO_STR2(_val)

static FormatEntity::Entry::Definition g_ansi_fg_entries[] = {
    ENTRY_STRING("black",
                 ANSI_ESC_START _TO_STR(ANSI_FG_COLOR_BLACK) ANSI_ESC_END),
    ENTRY_STRING("red", ANSI_ESC_START _TO_STR(ANSI_FG_COLOR_RED) ANSI_ESC_END),
    ENTRY_STRING("green",
                 ANSI_ESC_START _TO_STR(ANSI_FG_COLOR_GREEN) ANSI_ESC_END),
    ENTRY_STRING("yellow",
                 ANSI_ESC_START _TO_STR(ANSI_FG_COLOR_YELLOW) ANSI_ESC_END),
    ENTRY_STRING("blue",
                 ANSI_ESC_START _TO_STR(ANSI_FG_COLOR_BLUE) ANSI_ESC_END),
    ENTRY_STRING("purple",
                 ANSI_ESC_START _TO_STR(ANSI_FG_COLOR_PURPLE) ANSI_ESC_END),
    ENTRY_STRING("cyan",
                 ANSI_ESC_START _TO_STR(ANSI_FG_COLOR_CYAN) ANSI_ESC_END),
    ENTRY_STRING("white",
                 ANSI_ESC_START _TO_STR(ANSI_FG_COLOR_WHITE) ANSI_ESC_END),
};

static FormatEntity::Entry::Definition g_ansi_bg_entries[] = {
    ENTRY_STRING("black",
                 ANSI_ESC_START _TO_STR(ANSI_BG_COLOR_BLACK) ANSI_ESC_END),
    ENTRY_STRING("red", ANSI_ESC_START _TO_STR(ANSI_BG_COLOR_RED) ANSI_ESC_END),
    ENTRY_STRING("green",
                 ANSI_ESC_START _TO_STR(ANSI_BG_COLOR_GREEN) ANSI_ESC_END),
    ENTRY_STRING("yellow",
                 ANSI_ESC_START _TO_STR(ANSI_BG_COLOR_YELLOW) ANSI_ESC_END),
    ENTRY_STRING("blue",
                 ANSI_ESC_START _TO_STR(ANSI_BG_COLOR_BLUE) ANSI_ESC_END),
    ENTRY_STRING("purple",
                 ANSI_ESC_START _TO_STR(ANSI_BG_COLOR_PURPLE) ANSI_ESC_END),
    ENTRY_STRING("cyan",
                 ANSI_ESC_START _TO_STR(ANSI_BG_COLOR_CYAN) ANSI_ESC_END),
    ENTRY_STRING("white",
                 ANSI_ESC_START _TO_STR(ANSI_BG_COLOR_WHITE) ANSI_ESC_END),
};

static FormatEntity::Entry::Definition g_ansi_entries[] = {
    ENTRY_CHILDREN("fg", Invalid, None, g_ansi_fg_entries),
    ENTRY_CHILDREN("bg", Invalid, None, g_ansi_bg_entries),
    ENTRY_STRING("normal",
                 ANSI_ESC_START _TO_STR(ANSI_CTRL_NORMAL) ANSI_ESC_END),
    ENTRY_STRING("bold", ANSI_ESC_START _TO_STR(ANSI_CTRL_BOLD) ANSI_ESC_END),
    ENTRY_STRING("faint", ANSI_ESC_START _TO_STR(ANSI_CTRL_FAINT) ANSI_ESC_END),
    ENTRY_STRING("italic",
                 ANSI_ESC_START _TO_STR(ANSI_CTRL_ITALIC) ANSI_ESC_END),
    ENTRY_STRING("underline",
                 ANSI_ESC_START _TO_STR(ANSI_CTRL_UNDERLINE) ANSI_ESC_END),
    ENTRY_STRING("slow-blink",
                 ANSI_ESC_START _TO_STR(ANSI_CTRL_SLOW_BLINK) ANSI_ESC_END),
    ENTRY_STRING("fast-blink",
                 ANSI_ESC_START _TO_STR(ANSI_CTRL_FAST_BLINK) ANSI_ESC_END),
    ENTRY_STRING("negative",
                 ANSI_ESC_START _TO_STR(ANSI_CTRL_IMAGE_NEGATIVE) ANSI_ESC_END),
    ENTRY_STRING("conceal",
                 ANSI_ESC_START _TO_STR(ANSI_CTRL_CONCEAL) ANSI_ESC_END),
    ENTRY_STRING("crossed-out",
                 ANSI_ESC_START _TO_STR(ANSI_CTRL_CROSSED_OUT) ANSI_ESC_END),
};

static FormatEntity::Entry::Definition g_script_child_entries[] = {
    ENTRY("frame", ScriptFrame, None),
    ENTRY("process", ScriptProcess, None),
    ENTRY("target", ScriptTarget, None),
    ENTRY("thread", ScriptThread, None),
    ENTRY("var", ScriptVariable, None),
    ENTRY("svar", ScriptVariableSynthetic, None),
    ENTRY("thread", ScriptThread, None),
};

static FormatEntity::Entry::Definition g_top_level_entries[] = {
    ENTRY_CHILDREN("addr", AddressLoadOrFile, UInt64, g_addr_entries),
    ENTRY("addr-file-or-load", AddressLoadOrFile, UInt64),
    ENTRY_CHILDREN("ansi", Invalid, None, g_ansi_entries),
    ENTRY("current-pc-arrow", CurrentPCArrow, CString),
    ENTRY_CHILDREN("file", File, CString, g_file_child_entries),
    ENTRY("language", Lang, CString),
    ENTRY_CHILDREN("frame", Invalid, None, g_frame_child_entries),
    ENTRY_CHILDREN("function", Invalid, None, g_function_child_entries),
    ENTRY_CHILDREN("line", Invalid, None, g_line_child_entries),
    ENTRY_CHILDREN("module", Invalid, None, g_module_child_entries),
    ENTRY_CHILDREN("process", Invalid, None, g_process_child_entries),
    ENTRY_CHILDREN("script", Invalid, None, g_script_child_entries),
    ENTRY_CHILDREN_KEEP_SEP("svar", VariableSynthetic, None,
                            g_svar_child_entries),
    ENTRY_CHILDREN("thread", Invalid, None, g_thread_child_entries),
    ENTRY_CHILDREN("target", Invalid, None, g_target_child_entries),
    ENTRY_CHILDREN_KEEP_SEP("var", Variable, None, g_var_child_entries),
};

static FormatEntity::Entry::Definition g_root =
    ENTRY_CHILDREN("<root>", Root, None, g_top_level_entries);

FormatEntity::Entry::Entry(llvm::StringRef s)
    : string(s.data(), s.size()), printf_format(), children(),
      definition(nullptr), type(Type::String), fmt(lldb::eFormatDefault),
      number(0), deref(false) {}

FormatEntity::Entry::Entry(char ch)
    : string(1, ch), printf_format(), children(), definition(nullptr),
      type(Type::String), fmt(lldb::eFormatDefault), number(0), deref(false) {}

void FormatEntity::Entry::AppendChar(char ch) {
  if (children.empty() || children.back().type != Entry::Type::String)
    children.push_back(Entry(ch));
  else
    children.back().string.append(1, ch);
}

void FormatEntity::Entry::AppendText(const llvm::StringRef &s) {
  if (children.empty() || children.back().type != Entry::Type::String)
    children.push_back(Entry(s));
  else
    children.back().string.append(s.data(), s.size());
}

void FormatEntity::Entry::AppendText(const char *cstr) {
  return AppendText(llvm::StringRef(cstr));
}

Status FormatEntity::Parse(const llvm::StringRef &format_str, Entry &entry) {
  entry.Clear();
  entry.type = Entry::Type::Root;
  llvm::StringRef modifiable_format(format_str);
  return ParseInternal(modifiable_format, entry, 0);
}

#define ENUM_TO_CSTR(eee)                                                      \
  case FormatEntity::Entry::Type::eee:                                         \
    return #eee

const char *FormatEntity::Entry::TypeToCString(Type t) {
  switch (t) {
    ENUM_TO_CSTR(Invalid);
    ENUM_TO_CSTR(ParentNumber);
    ENUM_TO_CSTR(ParentString);
    ENUM_TO_CSTR(InsertString);
    ENUM_TO_CSTR(Root);
    ENUM_TO_CSTR(String);
    ENUM_TO_CSTR(Scope);
    ENUM_TO_CSTR(Variable);
    ENUM_TO_CSTR(VariableSynthetic);
    ENUM_TO_CSTR(ScriptVariable);
    ENUM_TO_CSTR(ScriptVariableSynthetic);
    ENUM_TO_CSTR(AddressLoad);
    ENUM_TO_CSTR(AddressFile);
    ENUM_TO_CSTR(AddressLoadOrFile);
    ENUM_TO_CSTR(ProcessID);
    ENUM_TO_CSTR(ProcessFile);
    ENUM_TO_CSTR(ScriptProcess);
    ENUM_TO_CSTR(ThreadID);
    ENUM_TO_CSTR(ThreadProtocolID);
    ENUM_TO_CSTR(ThreadIndexID);
    ENUM_TO_CSTR(ThreadName);
    ENUM_TO_CSTR(ThreadQueue);
    ENUM_TO_CSTR(ThreadStopReason);
    ENUM_TO_CSTR(ThreadReturnValue);
    ENUM_TO_CSTR(ThreadCompletedExpression);
    ENUM_TO_CSTR(ScriptThread);
    ENUM_TO_CSTR(ThreadInfo);
    ENUM_TO_CSTR(TargetArch);
    ENUM_TO_CSTR(ScriptTarget);
    ENUM_TO_CSTR(ModuleFile);
    ENUM_TO_CSTR(File);
    ENUM_TO_CSTR(Lang);
    ENUM_TO_CSTR(FrameIndex);
    ENUM_TO_CSTR(FrameNoDebug);
    ENUM_TO_CSTR(FrameRegisterPC);
    ENUM_TO_CSTR(FrameRegisterSP);
    ENUM_TO_CSTR(FrameRegisterFP);
    ENUM_TO_CSTR(FrameRegisterFlags);
    ENUM_TO_CSTR(FrameRegisterByName);
    ENUM_TO_CSTR(FrameIsArtificial);
    ENUM_TO_CSTR(ScriptFrame);
    ENUM_TO_CSTR(FunctionID);
    ENUM_TO_CSTR(FunctionDidChange);
    ENUM_TO_CSTR(FunctionInitialFunction);
    ENUM_TO_CSTR(FunctionName);
    ENUM_TO_CSTR(FunctionNameWithArgs);
    ENUM_TO_CSTR(FunctionNameNoArgs);
    ENUM_TO_CSTR(FunctionAddrOffset);
    ENUM_TO_CSTR(FunctionAddrOffsetConcrete);
    ENUM_TO_CSTR(FunctionLineOffset);
    ENUM_TO_CSTR(FunctionPCOffset);
    ENUM_TO_CSTR(FunctionInitial);
    ENUM_TO_CSTR(FunctionChanged);
    ENUM_TO_CSTR(FunctionIsOptimized);
    ENUM_TO_CSTR(LineEntryFile);
    ENUM_TO_CSTR(LineEntryLineNumber);
    ENUM_TO_CSTR(LineEntryColumn);
    ENUM_TO_CSTR(LineEntryStartAddress);
    ENUM_TO_CSTR(LineEntryEndAddress);
    ENUM_TO_CSTR(CurrentPCArrow);
  }
  return "???";
}

#undef ENUM_TO_CSTR

void FormatEntity::Entry::Dump(Stream &s, int depth) const {
  s.Printf("%*.*s%-20s: ", depth * 2, depth * 2, "", TypeToCString(type));
  if (fmt != eFormatDefault)
    s.Printf("lldb-format = %s, ", FormatManager::GetFormatAsCString(fmt));
  if (!string.empty())
    s.Printf("string = \"%s\"", string.c_str());
  if (!printf_format.empty())
    s.Printf("printf_format = \"%s\"", printf_format.c_str());
  if (number != 0)
    s.Printf("number = %" PRIu64 " (0x%" PRIx64 "), ", number, number);
  if (deref)
    s.Printf("deref = true, ");
  s.EOL();
  for (const auto &child : children) {
    child.Dump(s, depth + 1);
  }
}

template <typename T>
static bool RunScriptFormatKeyword(Stream &s, const SymbolContext *sc,
                                   const ExecutionContext *exe_ctx, T t,
                                   const char *script_function_name) {
  Target *target = Target::GetTargetFromContexts(exe_ctx, sc);

  if (target) {
    ScriptInterpreter *script_interpreter =
        target->GetDebugger().GetCommandInterpreter().GetScriptInterpreter();
    if (script_interpreter) {
      Status error;
      std::string script_output;

      if (script_interpreter->RunScriptFormatKeyword(script_function_name, t,
                                                     script_output, error) &&
          error.Success()) {
        s.Printf("%s", script_output.c_str());
        return true;
      } else {
        s.Printf("<error: %s>", error.AsCString());
      }
    }
  }
  return false;
}

static bool DumpAddress(Stream &s, const SymbolContext *sc,
                        const ExecutionContext *exe_ctx, const Address &addr,
                        bool print_file_addr_or_load_addr) {
  Target *target = Target::GetTargetFromContexts(exe_ctx, sc);
  addr_t vaddr = LLDB_INVALID_ADDRESS;
  if (exe_ctx && !target->GetSectionLoadList().IsEmpty())
    vaddr = addr.GetLoadAddress(target);
  if (vaddr == LLDB_INVALID_ADDRESS)
    vaddr = addr.GetFileAddress();

  if (vaddr != LLDB_INVALID_ADDRESS) {
    int addr_width = 0;
    if (exe_ctx && target) {
      addr_width = target->GetArchitecture().GetAddressByteSize() * 2;
    }
    if (addr_width == 0)
      addr_width = 16;
    if (print_file_addr_or_load_addr) {
      ExecutionContextScope *exe_scope = nullptr;
      if (exe_ctx)
        exe_scope = exe_ctx->GetBestExecutionContextScope();
      addr.Dump(&s, exe_scope, Address::DumpStyleLoadAddress,
                Address::DumpStyleModuleWithFileAddress, 0);
    } else {
      s.Printf("0x%*.*" PRIx64, addr_width, addr_width, vaddr);
    }
    return true;
  }
  return false;
}

static bool DumpAddressOffsetFromFunction(Stream &s, const SymbolContext *sc,
                                          const ExecutionContext *exe_ctx,
                                          const Address &format_addr,
                                          bool concrete_only, bool no_padding,
                                          bool print_zero_offsets) {
  if (format_addr.IsValid()) {
    Address func_addr;

    if (sc) {
      if (sc->function) {
        func_addr = sc->function->GetAddressRange().GetBaseAddress();
        if (sc->block && !concrete_only) {
          // Check to make sure we aren't in an inline function. If we are, use
          // the inline block range that contains "format_addr" since blocks
          // can be discontiguous.
          Block *inline_block = sc->block->GetContainingInlinedBlock();
          AddressRange inline_range;
          if (inline_block &&
              inline_block->GetRangeContainingAddress(format_addr,
                                                      inline_range))
            func_addr = inline_range.GetBaseAddress();
        }
      } else if (sc->symbol && sc->symbol->ValueIsAddress())
        func_addr = sc->symbol->GetAddressRef();
    }

    if (func_addr.IsValid()) {
      const char *addr_offset_padding = no_padding ? "" : " ";

      if (func_addr.GetSection() == format_addr.GetSection()) {
        addr_t func_file_addr = func_addr.GetFileAddress();
        addr_t addr_file_addr = format_addr.GetFileAddress();
        if (addr_file_addr > func_file_addr ||
            (addr_file_addr == func_file_addr && print_zero_offsets)) {
          s.Printf("%s+%s%" PRIu64, addr_offset_padding, addr_offset_padding,
                   addr_file_addr - func_file_addr);
        } else if (addr_file_addr < func_file_addr) {
          s.Printf("%s-%s%" PRIu64, addr_offset_padding, addr_offset_padding,
                   func_file_addr - addr_file_addr);
        }
        return true;
      } else {
        Target *target = Target::GetTargetFromContexts(exe_ctx, sc);
        if (target) {
          addr_t func_load_addr = func_addr.GetLoadAddress(target);
          addr_t addr_load_addr = format_addr.GetLoadAddress(target);
          if (addr_load_addr > func_load_addr ||
              (addr_load_addr == func_load_addr && print_zero_offsets)) {
            s.Printf("%s+%s%" PRIu64, addr_offset_padding, addr_offset_padding,
                     addr_load_addr - func_load_addr);
          } else if (addr_load_addr < func_load_addr) {
            s.Printf("%s-%s%" PRIu64, addr_offset_padding, addr_offset_padding,
                     func_load_addr - addr_load_addr);
          }
          return true;
        }
      }
    }
  }
  return false;
}

static bool ScanBracketedRange(llvm::StringRef subpath,
                               size_t &close_bracket_index,
                               const char *&var_name_final_if_array_range,
                               int64_t &index_lower, int64_t &index_higher) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_DATAFORMATTERS));
  close_bracket_index = llvm::StringRef::npos;
  const size_t open_bracket_index = subpath.find('[');
  if (open_bracket_index == llvm::StringRef::npos) {
    if (log)
      log->Printf("[ScanBracketedRange] no bracketed range, skipping entirely");
    return false;
  }

  close_bracket_index = subpath.find(']', open_bracket_index + 1);

  if (close_bracket_index == llvm::StringRef::npos) {
    if (log)
      log->Printf("[ScanBracketedRange] no bracketed range, skipping entirely");
    return false;
  } else {
    var_name_final_if_array_range = subpath.data() + open_bracket_index;

    if (close_bracket_index - open_bracket_index == 1) {
      if (log)
        log->Printf(
            "[ScanBracketedRange] '[]' detected.. going from 0 to end of data");
      index_lower = 0;
    } else {
      const size_t separator_index = subpath.find('-', open_bracket_index + 1);

      if (separator_index == llvm::StringRef::npos) {
        const char *index_lower_cstr = subpath.data() + open_bracket_index + 1;
        index_lower = ::strtoul(index_lower_cstr, nullptr, 0);
        index_higher = index_lower;
        if (log)
          log->Printf("[ScanBracketedRange] [%" PRId64
                      "] detected, high index is same",
                      index_lower);
      } else {
        const char *index_lower_cstr = subpath.data() + open_bracket_index + 1;
        const char *index_higher_cstr = subpath.data() + separator_index + 1;
        index_lower = ::strtoul(index_lower_cstr, nullptr, 0);
        index_higher = ::strtoul(index_higher_cstr, nullptr, 0);
        if (log)
          log->Printf("[ScanBracketedRange] [%" PRId64 "-%" PRId64 "] detected",
                      index_lower, index_higher);
      }
      if (index_lower > index_higher && index_higher > 0) {
        if (log)
          log->Printf("[ScanBracketedRange] swapping indices");
        const int64_t temp = index_lower;
        index_lower = index_higher;
        index_higher = temp;
      }
    }
  }
  return true;
}

static bool DumpFile(Stream &s, const FileSpec &file, FileKind file_kind) {
  switch (file_kind) {
  case FileKind::FileError:
    break;

  case FileKind::Basename:
    if (file.GetFilename()) {
      s << file.GetFilename();
      return true;
    }
    break;

  case FileKind::Dirname:
    if (file.GetDirectory()) {
      s << file.GetDirectory();
      return true;
    }
    break;

  case FileKind::Fullpath:
    if (file) {
      s << file;
      return true;
    }
    break;
  }
  return false;
}

static bool DumpRegister(Stream &s, StackFrame *frame, RegisterKind reg_kind,
                         uint32_t reg_num, Format format)

{
  if (frame) {
    RegisterContext *reg_ctx = frame->GetRegisterContext().get();

    if (reg_ctx) {
      const uint32_t lldb_reg_num =
          reg_ctx->ConvertRegisterKindToRegisterNumber(reg_kind, reg_num);
      if (lldb_reg_num != LLDB_INVALID_REGNUM) {
        const RegisterInfo *reg_info =
            reg_ctx->GetRegisterInfoAtIndex(lldb_reg_num);
        if (reg_info) {
          RegisterValue reg_value;
          if (reg_ctx->ReadRegister(reg_info, reg_value)) {
            DumpRegisterValue(reg_value, &s, reg_info, false, false, format);
            return true;
          }
        }
      }
    }
  }
  return false;
}

static ValueObjectSP ExpandIndexedExpression(ValueObject *valobj, size_t index,
                                             StackFrame *frame,
                                             bool deref_pointer) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_DATAFORMATTERS));
  const char *ptr_deref_format = "[%d]";
  std::string ptr_deref_buffer(10, 0);
  ::sprintf(&ptr_deref_buffer[0], ptr_deref_format, index);
  if (log)
    log->Printf("[ExpandIndexedExpression] name to deref: %s",
                ptr_deref_buffer.c_str());
  ValueObject::GetValueForExpressionPathOptions options;
  ValueObject::ExpressionPathEndResultType final_value_type;
  ValueObject::ExpressionPathScanEndReason reason_to_stop;
  ValueObject::ExpressionPathAftermath what_next =
      (deref_pointer ? ValueObject::eExpressionPathAftermathDereference
                     : ValueObject::eExpressionPathAftermathNothing);
  ValueObjectSP item = valobj->GetValueForExpressionPath(
      ptr_deref_buffer.c_str(), &reason_to_stop, &final_value_type, options,
      &what_next);
  if (!item) {
    if (log)
      log->Printf("[ExpandIndexedExpression] ERROR: why stopping = %d,"
                  " final_value_type %d",
                  reason_to_stop, final_value_type);
  } else {
    if (log)
      log->Printf("[ExpandIndexedExpression] ALL RIGHT: why stopping = %d,"
                  " final_value_type %d",
                  reason_to_stop, final_value_type);
  }
  return item;
}

static char ConvertValueObjectStyleToChar(
    ValueObject::ValueObjectRepresentationStyle style) {
  switch (style) {
  case ValueObject::eValueObjectRepresentationStyleLanguageSpecific:
    return '@';
  case ValueObject::eValueObjectRepresentationStyleValue:
    return 'V';
  case ValueObject::eValueObjectRepresentationStyleLocation:
    return 'L';
  case ValueObject::eValueObjectRepresentationStyleSummary:
    return 'S';
  case ValueObject::eValueObjectRepresentationStyleChildrenCount:
    return '#';
  case ValueObject::eValueObjectRepresentationStyleType:
    return 'T';
  case ValueObject::eValueObjectRepresentationStyleName:
    return 'N';
  case ValueObject::eValueObjectRepresentationStyleExpressionPath:
    return '>';
  }
  return '\0';
}

static bool DumpValue(Stream &s, const SymbolContext *sc,
                      const ExecutionContext *exe_ctx,
                      const FormatEntity::Entry &entry, ValueObject *valobj) {
  if (valobj == nullptr)
    return false;

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_DATAFORMATTERS));
  Format custom_format = eFormatInvalid;
  ValueObject::ValueObjectRepresentationStyle val_obj_display =
      entry.string.empty()
          ? ValueObject::eValueObjectRepresentationStyleValue
          : ValueObject::eValueObjectRepresentationStyleSummary;

  bool do_deref_pointer = entry.deref;
  bool is_script = false;
  switch (entry.type) {
  case FormatEntity::Entry::Type::ScriptVariable:
    is_script = true;
    break;

  case FormatEntity::Entry::Type::Variable:
    custom_format = entry.fmt;
    val_obj_display = (ValueObject::ValueObjectRepresentationStyle)entry.number;
    break;

  case FormatEntity::Entry::Type::ScriptVariableSynthetic:
    is_script = true;
    LLVM_FALLTHROUGH;
  case FormatEntity::Entry::Type::VariableSynthetic:
    custom_format = entry.fmt;
    val_obj_display = (ValueObject::ValueObjectRepresentationStyle)entry.number;
    if (!valobj->IsSynthetic()) {
      valobj = valobj->GetSyntheticValue().get();
      if (valobj == nullptr)
        return false;
    }
    break;

  default:
    return false;
  }

  if (valobj == nullptr)
    return false;

  ValueObject::ExpressionPathAftermath what_next =
      (do_deref_pointer ? ValueObject::eExpressionPathAftermathDereference
                        : ValueObject::eExpressionPathAftermathNothing);
  ValueObject::GetValueForExpressionPathOptions options;
  options.DontCheckDotVsArrowSyntax()
      .DoAllowBitfieldSyntax()
      .DoAllowFragileIVar()
      .SetSyntheticChildrenTraversal(
          ValueObject::GetValueForExpressionPathOptions::
              SyntheticChildrenTraversal::Both);
  ValueObject *target = nullptr;
  const char *var_name_final_if_array_range = nullptr;
  size_t close_bracket_index = llvm::StringRef::npos;
  int64_t index_lower = -1;
  int64_t index_higher = -1;
  bool is_array_range = false;
  bool was_plain_var = false;
  bool was_var_format = false;
  bool was_var_indexed = false;
  ValueObject::ExpressionPathScanEndReason reason_to_stop =
      ValueObject::eExpressionPathScanEndReasonEndOfString;
  ValueObject::ExpressionPathEndResultType final_value_type =
      ValueObject::eExpressionPathEndResultTypePlain;

  if (is_script) {
    return RunScriptFormatKeyword(s, sc, exe_ctx, valobj, entry.string.c_str());
  }

  llvm::StringRef subpath(entry.string);
  // simplest case ${var}, just print valobj's value
  if (entry.string.empty()) {
    if (entry.printf_format.empty() && entry.fmt == eFormatDefault &&
        entry.number == ValueObject::eValueObjectRepresentationStyleValue)
      was_plain_var = true;
    else
      was_var_format = true;
    target = valobj;
  } else // this is ${var.something} or multiple .something nested
  {
    if (entry.string[0] == '[')
      was_var_indexed = true;
    ScanBracketedRange(subpath, close_bracket_index,
                       var_name_final_if_array_range, index_lower,
                       index_higher);

    Status error;

    const std::string &expr_path = entry.string;

    if (log)
      log->Printf("[Debugger::FormatPrompt] symbol to expand: %s",
                  expr_path.c_str());

    target =
        valobj
            ->GetValueForExpressionPath(expr_path.c_str(), &reason_to_stop,
                                        &final_value_type, options, &what_next)
            .get();

    if (!target) {
      if (log)
        log->Printf("[Debugger::FormatPrompt] ERROR: why stopping = %d,"
                    " final_value_type %d",
                    reason_to_stop, final_value_type);
      return false;
    } else {
      if (log)
        log->Printf("[Debugger::FormatPrompt] ALL RIGHT: why stopping = %d,"
                    " final_value_type %d",
                    reason_to_stop, final_value_type);
      target = target
                   ->GetQualifiedRepresentationIfAvailable(
                       target->GetDynamicValueType(), true)
                   .get();
    }
  }

  is_array_range =
      (final_value_type ==
           ValueObject::eExpressionPathEndResultTypeBoundedRange ||
       final_value_type ==
           ValueObject::eExpressionPathEndResultTypeUnboundedRange);

  do_deref_pointer =
      (what_next == ValueObject::eExpressionPathAftermathDereference);

  if (do_deref_pointer && !is_array_range) {
    // I have not deref-ed yet, let's do it
    // this happens when we are not going through
    // GetValueForVariableExpressionPath to get to the target ValueObject
    Status error;
    target = target->Dereference(error).get();
    if (error.Fail()) {
      if (log)
        log->Printf("[Debugger::FormatPrompt] ERROR: %s\n",
                    error.AsCString("unknown"));
      return false;
    }
    do_deref_pointer = false;
  }

  if (!target) {
    if (log)
      log->Printf("[Debugger::FormatPrompt] could not calculate target for "
                  "prompt expression");
    return false;
  }

  // we do not want to use the summary for a bitfield of type T:n if we were
  // originally dealing with just a T - that would get us into an endless
  // recursion
  if (target->IsBitfield() && was_var_indexed) {
    // TODO: check for a (T:n)-specific summary - we should still obey that
    StreamString bitfield_name;
    bitfield_name.Printf("%s:%d", target->GetTypeName().AsCString(),
                         target->GetBitfieldBitSize());
    auto type_sp = std::make_shared<TypeNameSpecifierImpl>(
        bitfield_name.GetString(), false);
    if (val_obj_display ==
            ValueObject::eValueObjectRepresentationStyleSummary &&
        !DataVisualization::GetSummaryForType(type_sp))
      val_obj_display = ValueObject::eValueObjectRepresentationStyleValue;
  }

  // TODO use flags for these
  const uint32_t type_info_flags =
      target->GetCompilerType().GetTypeInfo(nullptr);
  bool is_array = (type_info_flags & eTypeIsArray) != 0;
  bool is_pointer = (type_info_flags & eTypeIsPointer) != 0;
  bool is_aggregate = target->GetCompilerType().IsAggregateType();

  if ((is_array || is_pointer) && (!is_array_range) &&
      val_obj_display ==
          ValueObject::eValueObjectRepresentationStyleValue) // this should be
                                                             // wrong, but there
                                                             // are some
                                                             // exceptions
  {
    StreamString str_temp;
    if (log)
      log->Printf(
          "[Debugger::FormatPrompt] I am into array || pointer && !range");

    if (target->HasSpecialPrintableRepresentation(val_obj_display,
                                                  custom_format)) {
      // try to use the special cases
      bool success = target->DumpPrintableRepresentation(
          str_temp, val_obj_display, custom_format);
      if (log)
        log->Printf("[Debugger::FormatPrompt] special cases did%s match",
                    success ? "" : "n't");

      // should not happen
      if (success)
        s << str_temp.GetString();
      return true;
    } else {
      if (was_plain_var) // if ${var}
      {
        s << target->GetTypeName() << " @ " << target->GetLocationAsCString();
      } else if (is_pointer) // if pointer, value is the address stored
      {
        target->DumpPrintableRepresentation(
            s, val_obj_display, custom_format,
            ValueObject::PrintableRepresentationSpecialCases::eDisable);
      }
      return true;
    }
  }

  // if directly trying to print ${var}, and this is an aggregate, display a
  // nice type @ location message
  if (is_aggregate && was_plain_var) {
    s << target->GetTypeName() << " @ " << target->GetLocationAsCString();
    return true;
  }

  // if directly trying to print ${var%V}, and this is an aggregate, do not let
  // the user do it
  if (is_aggregate &&
      ((was_var_format &&
        val_obj_display ==
            ValueObject::eValueObjectRepresentationStyleValue))) {
    s << "<invalid use of aggregate type>";
    return true;
  }

  if (!is_array_range) {
    if (log)
      log->Printf("[Debugger::FormatPrompt] dumping ordinary printable output");
    return target->DumpPrintableRepresentation(s, val_obj_display,
                                               custom_format);
  } else {
    if (log)
      log->Printf("[Debugger::FormatPrompt] checking if I can handle as array");
    if (!is_array && !is_pointer)
      return false;
    if (log)
      log->Printf("[Debugger::FormatPrompt] handle as array");
    StreamString special_directions_stream;
    llvm::StringRef special_directions;
    if (close_bracket_index != llvm::StringRef::npos &&
        subpath.size() > close_bracket_index) {
      ConstString additional_data(subpath.drop_front(close_bracket_index + 1));
      special_directions_stream.Printf("${%svar%s", do_deref_pointer ? "*" : "",
                                       additional_data.GetCString());

      if (entry.fmt != eFormatDefault) {
        const char format_char =
            FormatManager::GetFormatAsFormatChar(entry.fmt);
        if (format_char != '\0')
          special_directions_stream.Printf("%%%c", format_char);
        else {
          const char *format_cstr =
              FormatManager::GetFormatAsCString(entry.fmt);
          special_directions_stream.Printf("%%%s", format_cstr);
        }
      } else if (entry.number != 0) {
        const char style_char = ConvertValueObjectStyleToChar(
            (ValueObject::ValueObjectRepresentationStyle)entry.number);
        if (style_char)
          special_directions_stream.Printf("%%%c", style_char);
      }
      special_directions_stream.PutChar('}');
      special_directions =
          llvm::StringRef(special_directions_stream.GetString());
    }

    // let us display items index_lower thru index_higher of this array
    s.PutChar('[');

    if (index_higher < 0)
      index_higher = valobj->GetNumChildren() - 1;

    uint32_t max_num_children =
        target->GetTargetSP()->GetMaximumNumberOfChildrenToDisplay();

    bool success = true;
    for (int64_t index = index_lower; index <= index_higher; ++index) {
      ValueObject *item =
          ExpandIndexedExpression(target, index, exe_ctx->GetFramePtr(), false)
              .get();

      if (!item) {
        if (log)
          log->Printf("[Debugger::FormatPrompt] ERROR in getting child item at "
                      "index %" PRId64,
                      index);
      } else {
        if (log)
          log->Printf(
              "[Debugger::FormatPrompt] special_directions for child item: %s",
              special_directions.data() ? special_directions.data() : "");
      }

      if (special_directions.empty()) {
        success &= item->DumpPrintableRepresentation(s, val_obj_display,
                                                     custom_format);
      } else {
        success &= FormatEntity::FormatStringRef(
            special_directions, s, sc, exe_ctx, nullptr, item, false, false);
      }

      if (--max_num_children == 0) {
        s.PutCString(", ...");
        break;
      }

      if (index < index_higher)
        s.PutChar(',');
    }
    s.PutChar(']');
    return success;
  }
}

static bool DumpRegister(Stream &s, StackFrame *frame, const char *reg_name,
                         Format format) {
  if (frame) {
    RegisterContext *reg_ctx = frame->GetRegisterContext().get();

    if (reg_ctx) {
      const RegisterInfo *reg_info = reg_ctx->GetRegisterInfoByName(reg_name);
      if (reg_info) {
        RegisterValue reg_value;
        if (reg_ctx->ReadRegister(reg_info, reg_value)) {
          DumpRegisterValue(reg_value, &s, reg_info, false, false, format);
          return true;
        }
      }
    }
  }
  return false;
}

static bool FormatThreadExtendedInfoRecurse(
    const FormatEntity::Entry &entry,
    const StructuredData::ObjectSP &thread_info_dictionary,
    const SymbolContext *sc, const ExecutionContext *exe_ctx, Stream &s) {
  llvm::StringRef path(entry.string);

  StructuredData::ObjectSP value =
      thread_info_dictionary->GetObjectForDotSeparatedPath(path);

  if (value) {
    if (value->GetType() == eStructuredDataTypeInteger) {
      const char *token_format = "0x%4.4" PRIx64;
      if (!entry.printf_format.empty())
        token_format = entry.printf_format.c_str();
      s.Printf(token_format, value->GetAsInteger()->GetValue());
      return true;
    } else if (value->GetType() == eStructuredDataTypeFloat) {
      s.Printf("%f", value->GetAsFloat()->GetValue());
      return true;
    } else if (value->GetType() == eStructuredDataTypeString) {
      s.Format("{0}", value->GetAsString()->GetValue());
      return true;
    } else if (value->GetType() == eStructuredDataTypeArray) {
      if (value->GetAsArray()->GetSize() > 0) {
        s.Printf("%zu", value->GetAsArray()->GetSize());
        return true;
      }
    } else if (value->GetType() == eStructuredDataTypeDictionary) {
      s.Printf("%zu",
               value->GetAsDictionary()->GetKeys()->GetAsArray()->GetSize());
      return true;
    }
  }

  return false;
}

static inline bool IsToken(const char *var_name_begin, const char *var) {
  return (::strncmp(var_name_begin, var, strlen(var)) == 0);
}

bool FormatEntity::FormatStringRef(const llvm::StringRef &format_str, Stream &s,
                                   const SymbolContext *sc,
                                   const ExecutionContext *exe_ctx,
                                   const Address *addr, ValueObject *valobj,
                                   bool function_changed,
                                   bool initial_function) {
  if (!format_str.empty()) {
    FormatEntity::Entry root;
    Status error = FormatEntity::Parse(format_str, root);
    if (error.Success()) {
      return FormatEntity::Format(root, s, sc, exe_ctx, addr, valobj,
                                  function_changed, initial_function);
    }
  }
  return false;
}

bool FormatEntity::FormatCString(const char *format, Stream &s,
                                 const SymbolContext *sc,
                                 const ExecutionContext *exe_ctx,
                                 const Address *addr, ValueObject *valobj,
                                 bool function_changed, bool initial_function) {
  if (format && format[0]) {
    FormatEntity::Entry root;
    llvm::StringRef format_str(format);
    Status error = FormatEntity::Parse(format_str, root);
    if (error.Success()) {
      return FormatEntity::Format(root, s, sc, exe_ctx, addr, valobj,
                                  function_changed, initial_function);
    }
  }
  return false;
}

bool FormatEntity::Format(const Entry &entry, Stream &s,
                          const SymbolContext *sc,
                          const ExecutionContext *exe_ctx, const Address *addr,
                          ValueObject *valobj, bool function_changed,
                          bool initial_function) {
  switch (entry.type) {
  case Entry::Type::Invalid:
  case Entry::Type::ParentNumber: // Only used for
                                  // FormatEntity::Entry::Definition encoding
  case Entry::Type::ParentString: // Only used for
                                  // FormatEntity::Entry::Definition encoding
  case Entry::Type::InsertString: // Only used for
                                  // FormatEntity::Entry::Definition encoding
    return false;

  case Entry::Type::Root:
    for (const auto &child : entry.children) {
      if (!Format(child, s, sc, exe_ctx, addr, valobj, function_changed,
                  initial_function)) {
        return false; // If any item of root fails, then the formatting fails
      }
    }
    return true; // Only return true if all items succeeded

  case Entry::Type::String:
    s.PutCString(entry.string);
    return true;

  case Entry::Type::Scope: {
    StreamString scope_stream;
    bool success = false;
    for (const auto &child : entry.children) {
      success = Format(child, scope_stream, sc, exe_ctx, addr, valobj,
                       function_changed, initial_function);
      if (!success)
        break;
    }
    // Only if all items in a scope succeed, then do we print the output into
    // the main stream
    if (success)
      s.Write(scope_stream.GetString().data(), scope_stream.GetString().size());
  }
    return true; // Scopes always successfully print themselves

  case Entry::Type::Variable:
  case Entry::Type::VariableSynthetic:
  case Entry::Type::ScriptVariable:
  case Entry::Type::ScriptVariableSynthetic:
    return DumpValue(s, sc, exe_ctx, entry, valobj);

  case Entry::Type::AddressFile:
  case Entry::Type::AddressLoad:
  case Entry::Type::AddressLoadOrFile:
    return (addr != nullptr && addr->IsValid() &&
            DumpAddress(s, sc, exe_ctx, *addr,
                        entry.type == Entry::Type::AddressLoadOrFile));

  case Entry::Type::ProcessID:
    if (exe_ctx) {
      Process *process = exe_ctx->GetProcessPtr();
      if (process) {
        const char *format = "%" PRIu64;
        if (!entry.printf_format.empty())
          format = entry.printf_format.c_str();
        s.Printf(format, process->GetID());
        return true;
      }
    }
    return false;

  case Entry::Type::ProcessFile:
    if (exe_ctx) {
      Process *process = exe_ctx->GetProcessPtr();
      if (process) {
        Module *exe_module = process->GetTarget().GetExecutableModulePointer();
        if (exe_module) {
          if (DumpFile(s, exe_module->GetFileSpec(), (FileKind)entry.number))
            return true;
        }
      }
    }
    return false;

  case Entry::Type::ScriptProcess:
    if (exe_ctx) {
      Process *process = exe_ctx->GetProcessPtr();
      if (process)
        return RunScriptFormatKeyword(s, sc, exe_ctx, process,
                                      entry.string.c_str());
    }
    return false;

  case Entry::Type::ThreadID:
    if (exe_ctx) {
      Thread *thread = exe_ctx->GetThreadPtr();
      if (thread) {
        const char *format = "0x%4.4" PRIx64;
        if (!entry.printf_format.empty()) {
          // Watch for the special "tid" format...
          if (entry.printf_format == "tid") {
            // TODO(zturner): Rather than hardcoding this to be platform
            // specific, it should be controlled by a setting and the default
            // value of the setting can be different depending on the platform.
            Target &target = thread->GetProcess()->GetTarget();
            ArchSpec arch(target.GetArchitecture());
            llvm::Triple::OSType ostype = arch.IsValid()
                                              ? arch.GetTriple().getOS()
                                              : llvm::Triple::UnknownOS;
            if ((ostype == llvm::Triple::FreeBSD) ||
                (ostype == llvm::Triple::Linux) ||
                (ostype == llvm::Triple::NetBSD)) {
              format = "%" PRIu64;
            }
          } else {
            format = entry.printf_format.c_str();
          }
        }
        s.Printf(format, thread->GetID());
        return true;
      }
    }
    return false;

  case Entry::Type::ThreadProtocolID:
    if (exe_ctx) {
      Thread *thread = exe_ctx->GetThreadPtr();
      if (thread) {
        const char *format = "0x%4.4" PRIx64;
        if (!entry.printf_format.empty())
          format = entry.printf_format.c_str();
        s.Printf(format, thread->GetProtocolID());
        return true;
      }
    }
    return false;

  case Entry::Type::ThreadIndexID:
    if (exe_ctx) {
      Thread *thread = exe_ctx->GetThreadPtr();
      if (thread) {
        const char *format = "%" PRIu32;
        if (!entry.printf_format.empty())
          format = entry.printf_format.c_str();
        s.Printf(format, thread->GetIndexID());
        return true;
      }
    }
    return false;

  case Entry::Type::ThreadName:
    if (exe_ctx) {
      Thread *thread = exe_ctx->GetThreadPtr();
      if (thread) {
        const char *cstr = thread->GetName();
        if (cstr && cstr[0]) {
          s.PutCString(cstr);
          return true;
        }
      }
    }
    return false;

  case Entry::Type::ThreadQueue:
    if (exe_ctx) {
      Thread *thread = exe_ctx->GetThreadPtr();
      if (thread) {
        const char *cstr = thread->GetQueueName();
        if (cstr && cstr[0]) {
          s.PutCString(cstr);
          return true;
        }
      }
    }
    return false;

  case Entry::Type::ThreadStopReason:
    if (exe_ctx) {
      Thread *thread = exe_ctx->GetThreadPtr();
      if (thread) {
        StopInfoSP stop_info_sp = thread->GetStopInfo();
        if (stop_info_sp && stop_info_sp->IsValid()) {
          const char *cstr = stop_info_sp->GetDescription();
          if (cstr && cstr[0]) {
            s.PutCString(cstr);
            return true;
          }
        }
      }
    }
    return false;

  case Entry::Type::ThreadReturnValue:
    if (exe_ctx) {
      Thread *thread = exe_ctx->GetThreadPtr();
      if (thread) {
        StopInfoSP stop_info_sp = thread->GetStopInfo();
        if (stop_info_sp && stop_info_sp->IsValid()) {
          ValueObjectSP return_valobj_sp =
              StopInfo::GetReturnValueObject(stop_info_sp);
          if (return_valobj_sp) {
            return_valobj_sp->Dump(s);
            return true;
          }
        }
      }
    }
    return false;

  case Entry::Type::ThreadCompletedExpression:
    if (exe_ctx) {
      Thread *thread = exe_ctx->GetThreadPtr();
      if (thread) {
        StopInfoSP stop_info_sp = thread->GetStopInfo();
        if (stop_info_sp && stop_info_sp->IsValid()) {
          ExpressionVariableSP expression_var_sp =
              StopInfo::GetExpressionVariable(stop_info_sp);
          if (expression_var_sp && expression_var_sp->GetValueObject()) {
            expression_var_sp->GetValueObject()->Dump(s);
            return true;
          }
        }
      }
    }
    return false;

  case Entry::Type::ScriptThread:
    if (exe_ctx) {
      Thread *thread = exe_ctx->GetThreadPtr();
      if (thread)
        return RunScriptFormatKeyword(s, sc, exe_ctx, thread,
                                      entry.string.c_str());
    }
    return false;

  case Entry::Type::ThreadInfo:
    if (exe_ctx) {
      Thread *thread = exe_ctx->GetThreadPtr();
      if (thread) {
        StructuredData::ObjectSP object_sp = thread->GetExtendedInfo();
        if (object_sp &&
            object_sp->GetType() == eStructuredDataTypeDictionary) {
          if (FormatThreadExtendedInfoRecurse(entry, object_sp, sc, exe_ctx, s))
            return true;
        }
      }
    }
    return false;

  case Entry::Type::TargetArch:
    if (exe_ctx) {
      Target *target = exe_ctx->GetTargetPtr();
      if (target) {
        const ArchSpec &arch = target->GetArchitecture();
        if (arch.IsValid()) {
          s.PutCString(arch.GetArchitectureName());
          return true;
        }
      }
    }
    return false;

  case Entry::Type::ScriptTarget:
    if (exe_ctx) {
      Target *target = exe_ctx->GetTargetPtr();
      if (target)
        return RunScriptFormatKeyword(s, sc, exe_ctx, target,
                                      entry.string.c_str());
    }
    return false;

  case Entry::Type::ModuleFile:
    if (sc) {
      Module *module = sc->module_sp.get();
      if (module) {
        if (DumpFile(s, module->GetFileSpec(), (FileKind)entry.number))
          return true;
      }
    }
    return false;

  case Entry::Type::File:
    if (sc) {
      CompileUnit *cu = sc->comp_unit;
      if (cu) {
        // CompileUnit is a FileSpec
        if (DumpFile(s, *cu, (FileKind)entry.number))
          return true;
      }
    }
    return false;

  case Entry::Type::Lang:
    if (sc) {
      CompileUnit *cu = sc->comp_unit;
      if (cu) {
        const char *lang_name =
            Language::GetNameForLanguageType(cu->GetLanguage());
        if (lang_name) {
          s.PutCString(lang_name);
          return true;
        }
      }
    }
    return false;

  case Entry::Type::FrameIndex:
    if (exe_ctx) {
      StackFrame *frame = exe_ctx->GetFramePtr();
      if (frame) {
        const char *format = "%" PRIu32;
        if (!entry.printf_format.empty())
          format = entry.printf_format.c_str();
        s.Printf(format, frame->GetFrameIndex());
        return true;
      }
    }
    return false;

  case Entry::Type::FrameRegisterPC:
    if (exe_ctx) {
      StackFrame *frame = exe_ctx->GetFramePtr();
      if (frame) {
        const Address &pc_addr = frame->GetFrameCodeAddress();
        if (pc_addr.IsValid()) {
          if (DumpAddress(s, sc, exe_ctx, pc_addr, false))
            return true;
        }
      }
    }
    return false;

  case Entry::Type::FrameRegisterSP:
    if (exe_ctx) {
      StackFrame *frame = exe_ctx->GetFramePtr();
      if (frame) {
        if (DumpRegister(s, frame, eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP,
                         (lldb::Format)entry.number))
          return true;
      }
    }
    return false;

  case Entry::Type::FrameRegisterFP:
    if (exe_ctx) {
      StackFrame *frame = exe_ctx->GetFramePtr();
      if (frame) {
        if (DumpRegister(s, frame, eRegisterKindGeneric, LLDB_REGNUM_GENERIC_FP,
                         (lldb::Format)entry.number))
          return true;
      }
    }
    return false;

  case Entry::Type::FrameRegisterFlags:
    if (exe_ctx) {
      StackFrame *frame = exe_ctx->GetFramePtr();
      if (frame) {
        if (DumpRegister(s, frame, eRegisterKindGeneric,
                         LLDB_REGNUM_GENERIC_FLAGS, (lldb::Format)entry.number))
          return true;
      }
    }
    return false;

  case Entry::Type::FrameNoDebug:
    if (exe_ctx) {
      StackFrame *frame = exe_ctx->GetFramePtr();
      if (frame) {
        return !frame->HasDebugInformation();
      }
    }
    return true;

  case Entry::Type::FrameRegisterByName:
    if (exe_ctx) {
      StackFrame *frame = exe_ctx->GetFramePtr();
      if (frame) {
        if (DumpRegister(s, frame, entry.string.c_str(),
                         (lldb::Format)entry.number))
          return true;
      }
    }
    return false;

  case Entry::Type::FrameIsArtificial: {
    if (exe_ctx)
      if (StackFrame *frame = exe_ctx->GetFramePtr())
        return frame->IsArtificial();
    return false;
  }

  case Entry::Type::ScriptFrame:
    if (exe_ctx) {
      StackFrame *frame = exe_ctx->GetFramePtr();
      if (frame)
        return RunScriptFormatKeyword(s, sc, exe_ctx, frame,
                                      entry.string.c_str());
    }
    return false;

  case Entry::Type::FunctionID:
    if (sc) {
      if (sc->function) {
        s.Printf("function{0x%8.8" PRIx64 "}", sc->function->GetID());
        return true;
      } else if (sc->symbol) {
        s.Printf("symbol[%u]", sc->symbol->GetID());
        return true;
      }
    }
    return false;

  case Entry::Type::FunctionDidChange:
    return function_changed;

  case Entry::Type::FunctionInitialFunction:
    return initial_function;

  case Entry::Type::FunctionName: {
    Language *language_plugin = nullptr;
    bool language_plugin_handled = false;
    StreamString ss;
    if (sc->function)
      language_plugin = Language::FindPlugin(sc->function->GetLanguage());
    else if (sc->symbol)
      language_plugin = Language::FindPlugin(sc->symbol->GetLanguage());
    if (language_plugin) {
      language_plugin_handled = language_plugin->GetFunctionDisplayName(
          sc, exe_ctx, Language::FunctionNameRepresentation::eName, ss);
    }
    if (language_plugin_handled) {
      s << ss.GetString();
      return true;
    } else {
      const char *name = nullptr;
      if (sc->function)
        name = sc->function->GetName().AsCString(nullptr);
      else if (sc->symbol)
        name = sc->symbol->GetName().AsCString(nullptr);
      if (name) {
        s.PutCString(name);

        if (sc->block) {
          Block *inline_block = sc->block->GetContainingInlinedBlock();
          if (inline_block) {
            const InlineFunctionInfo *inline_info =
                sc->block->GetInlinedFunctionInfo();
            if (inline_info) {
              s.PutCString(" [inlined] ");
              inline_info->GetName(sc->function->GetLanguage()).Dump(&s);
            }
          }
        }
        return true;
      }
    }
  }
    return false;

  case Entry::Type::FunctionNameNoArgs: {
    Language *language_plugin = nullptr;
    bool language_plugin_handled = false;
    StreamString ss;
    if (sc->function)
      language_plugin = Language::FindPlugin(sc->function->GetLanguage());
    else if (sc->symbol)
      language_plugin = Language::FindPlugin(sc->symbol->GetLanguage());
    if (language_plugin) {
      language_plugin_handled = language_plugin->GetFunctionDisplayName(
          sc, exe_ctx, Language::FunctionNameRepresentation::eNameWithNoArgs,
          ss);
    }
    if (language_plugin_handled) {
      s << ss.GetString();
      return true;
    } else {
      ConstString name;
      if (sc->function)
        name = sc->function->GetNameNoArguments();
      else if (sc->symbol)
        name = sc->symbol->GetNameNoArguments();
      if (name) {
        s.PutCString(name.GetCString());
        return true;
      }
    }
  }
    return false;

  case Entry::Type::FunctionNameWithArgs: {
    Language *language_plugin = nullptr;
    bool language_plugin_handled = false;
    StreamString ss;
    if (sc->function)
      language_plugin = Language::FindPlugin(sc->function->GetLanguage());
    else if (sc->symbol)
      language_plugin = Language::FindPlugin(sc->symbol->GetLanguage());
    if (language_plugin) {
      language_plugin_handled = language_plugin->GetFunctionDisplayName(
          sc, exe_ctx, Language::FunctionNameRepresentation::eNameWithArgs, ss);
    }
    if (language_plugin_handled) {
      s << ss.GetString();
      return true;
    } else {
      // Print the function name with arguments in it
      if (sc->function) {
        ExecutionContextScope *exe_scope =
            exe_ctx ? exe_ctx->GetBestExecutionContextScope() : nullptr;
        const char *cstr = sc->function->GetName().AsCString(nullptr);
        if (cstr) {
          const InlineFunctionInfo *inline_info = nullptr;
          VariableListSP variable_list_sp;
          bool get_function_vars = true;
          if (sc->block) {
            Block *inline_block = sc->block->GetContainingInlinedBlock();

            if (inline_block) {
              get_function_vars = false;
              inline_info = sc->block->GetInlinedFunctionInfo();
              if (inline_info)
                variable_list_sp = inline_block->GetBlockVariableList(true);
            }
          }

          if (get_function_vars) {
            variable_list_sp =
                sc->function->GetBlock(true).GetBlockVariableList(true);
          }

          if (inline_info) {
            s.PutCString(cstr);
            s.PutCString(" [inlined] ");
            cstr =
                inline_info->GetName(sc->function->GetLanguage()).GetCString();
          }

          VariableList args;
          if (variable_list_sp)
            variable_list_sp->AppendVariablesWithScope(
                eValueTypeVariableArgument, args);
          if (args.GetSize() > 0) {
            const char *open_paren = strchr(cstr, '(');
            const char *close_paren = nullptr;
            const char *generic = strchr(cstr, '<');
            // if before the arguments list begins there is a template sign
            // then scan to the end of the generic args before you try to find
            // the arguments list
            if (generic && open_paren && generic < open_paren) {
              int generic_depth = 1;
              ++generic;
              for (; *generic && generic_depth > 0; generic++) {
                if (*generic == '<')
                  generic_depth++;
                if (*generic == '>')
                  generic_depth--;
              }
              if (*generic)
                open_paren = strchr(generic, '(');
              else
                open_paren = nullptr;
            }
            if (open_paren) {
              if (IsToken(open_paren, "(anonymous namespace)")) {
                open_paren =
                    strchr(open_paren + strlen("(anonymous namespace)"), '(');
                if (open_paren)
                  close_paren = strchr(open_paren, ')');
              } else
                close_paren = strchr(open_paren, ')');
            }

            if (open_paren)
              s.Write(cstr, open_paren - cstr + 1);
            else {
              s.PutCString(cstr);
              s.PutChar('(');
            }
            const size_t num_args = args.GetSize();
            for (size_t arg_idx = 0; arg_idx < num_args; ++arg_idx) {
              std::string buffer;

              VariableSP var_sp(args.GetVariableAtIndex(arg_idx));
              ValueObjectSP var_value_sp(
                  ValueObjectVariable::Create(exe_scope, var_sp));
              StreamString ss;
              llvm::StringRef var_representation;
              const char *var_name = var_value_sp->GetName().GetCString();
              if (var_value_sp->GetCompilerType().IsValid()) {
                if (var_value_sp && exe_scope->CalculateTarget())
                  var_value_sp =
                      var_value_sp->GetQualifiedRepresentationIfAvailable(
                          exe_scope->CalculateTarget()
                              ->TargetProperties::GetPreferDynamicValue(),
                          exe_scope->CalculateTarget()
                              ->TargetProperties::GetEnableSyntheticValue());
                if (var_value_sp->GetCompilerType().IsAggregateType() &&
                    DataVisualization::ShouldPrintAsOneLiner(*var_value_sp)) {
                  static StringSummaryFormat format(
                      TypeSummaryImpl::Flags()
                          .SetHideItemNames(false)
                          .SetShowMembersOneLiner(true),
                      "");
                  format.FormatObject(var_value_sp.get(), buffer,
                                      TypeSummaryOptions());
                  var_representation = buffer;
                } else
                  var_value_sp->DumpPrintableRepresentation(
                      ss, ValueObject::ValueObjectRepresentationStyle::
                              eValueObjectRepresentationStyleSummary,
                      eFormatDefault,
                      ValueObject::PrintableRepresentationSpecialCases::eAllow,
                      false);
              }

              if (!ss.GetString().empty())
                var_representation = ss.GetString();
              if (arg_idx > 0)
                s.PutCString(", ");
              if (var_value_sp->GetError().Success()) {
                if (!var_representation.empty())
                  s.Printf("%s=%s", var_name, var_representation.str().c_str());
                else
                  s.Printf("%s=%s at %s", var_name,
                           var_value_sp->GetTypeName().GetCString(),
                           var_value_sp->GetLocationAsCString());
              } else
                s.Printf("%s=<unavailable>", var_name);
            }

            if (close_paren)
              s.PutCString(close_paren);
            else
              s.PutChar(')');

          } else {
            s.PutCString(cstr);
          }
          return true;
        }
      } else if (sc->symbol) {
        const char *cstr = sc->symbol->GetName().AsCString(nullptr);
        if (cstr) {
          s.PutCString(cstr);
          return true;
        }
      }
    }
  }
    return false;

  case Entry::Type::FunctionAddrOffset:
    if (addr) {
      if (DumpAddressOffsetFromFunction(s, sc, exe_ctx, *addr, false, false,
                                        false))
        return true;
    }
    return false;

  case Entry::Type::FunctionAddrOffsetConcrete:
    if (addr) {
      if (DumpAddressOffsetFromFunction(s, sc, exe_ctx, *addr, true, true,
                                        true))
        return true;
    }
    return false;

  case Entry::Type::FunctionLineOffset:
    return (DumpAddressOffsetFromFunction(s, sc, exe_ctx,
                                          sc->line_entry.range.GetBaseAddress(),
                                          false, false, false));

  case Entry::Type::FunctionPCOffset:
    if (exe_ctx) {
      StackFrame *frame = exe_ctx->GetFramePtr();
      if (frame) {
        if (DumpAddressOffsetFromFunction(s, sc, exe_ctx,
                                          frame->GetFrameCodeAddress(), false,
                                          false, false))
          return true;
      }
    }
    return false;

  case Entry::Type::FunctionChanged:
    return function_changed;

  case Entry::Type::FunctionIsOptimized: {
    bool is_optimized = false;
    if (sc->function && sc->function->GetIsOptimized()) {
      is_optimized = true;
    }
    return is_optimized;
  }

  case Entry::Type::FunctionInitial:
    return initial_function;

  case Entry::Type::LineEntryFile:
    if (sc && sc->line_entry.IsValid()) {
      Module *module = sc->module_sp.get();
      if (module) {
        if (DumpFile(s, sc->line_entry.file, (FileKind)entry.number))
          return true;
      }
    }
    return false;

  case Entry::Type::LineEntryLineNumber:
    if (sc && sc->line_entry.IsValid()) {
      const char *format = "%" PRIu32;
      if (!entry.printf_format.empty())
        format = entry.printf_format.c_str();
      s.Printf(format, sc->line_entry.line);
      return true;
    }
    return false;

  case Entry::Type::LineEntryColumn:
    if (sc && sc->line_entry.IsValid() && sc->line_entry.column) {
      const char *format = "%" PRIu32;
      if (!entry.printf_format.empty())
        format = entry.printf_format.c_str();
      s.Printf(format, sc->line_entry.column);
      return true;
    }
    return false;

  case Entry::Type::LineEntryStartAddress:
  case Entry::Type::LineEntryEndAddress:
    if (sc && sc->line_entry.range.GetBaseAddress().IsValid()) {
      Address addr = sc->line_entry.range.GetBaseAddress();

      if (entry.type == Entry::Type::LineEntryEndAddress)
        addr.Slide(sc->line_entry.range.GetByteSize());
      if (DumpAddress(s, sc, exe_ctx, addr, false))
        return true;
    }
    return false;

  case Entry::Type::CurrentPCArrow:
    if (addr && exe_ctx && exe_ctx->GetFramePtr()) {
      RegisterContextSP reg_ctx =
          exe_ctx->GetFramePtr()->GetRegisterContextSP();
      if (reg_ctx) {
        addr_t pc_loadaddr = reg_ctx->GetPC();
        if (pc_loadaddr != LLDB_INVALID_ADDRESS) {
          Address pc;
          pc.SetLoadAddress(pc_loadaddr, exe_ctx->GetTargetPtr());
          if (pc == *addr) {
            s.Printf("-> ");
            return true;
          }
        }
      }
      s.Printf("   ");
      return true;
    }
    return false;
  }
  return false;
}

static bool DumpCommaSeparatedChildEntryNames(
    Stream &s, const FormatEntity::Entry::Definition *parent) {
  if (parent->children) {
    const size_t n = parent->num_children;
    for (size_t i = 0; i < n; ++i) {
      if (i > 0)
        s.PutCString(", ");
      s.Printf("\"%s\"", parent->children[i].name);
    }
    return true;
  }
  return false;
}

static Status ParseEntry(const llvm::StringRef &format_str,
                         const FormatEntity::Entry::Definition *parent,
                         FormatEntity::Entry &entry) {
  Status error;

  const size_t sep_pos = format_str.find_first_of(".[:");
  const char sep_char =
      (sep_pos == llvm::StringRef::npos) ? '\0' : format_str[sep_pos];
  llvm::StringRef key = format_str.substr(0, sep_pos);

  const size_t n = parent->num_children;
  for (size_t i = 0; i < n; ++i) {
    const FormatEntity::Entry::Definition *entry_def = parent->children + i;
    if (key.equals(entry_def->name) || entry_def->name[0] == '*') {
      llvm::StringRef value;
      if (sep_char)
        value =
            format_str.substr(sep_pos + (entry_def->keep_separator ? 0 : 1));
      switch (entry_def->type) {
      case FormatEntity::Entry::Type::ParentString:
        entry.string = format_str.str();
        return error; // Success

      case FormatEntity::Entry::Type::ParentNumber:
        entry.number = entry_def->data;
        return error; // Success

      case FormatEntity::Entry::Type::InsertString:
        entry.type = entry_def->type;
        entry.string = entry_def->string;
        return error; // Success

      default:
        entry.type = entry_def->type;
        break;
      }

      if (value.empty()) {
        if (entry_def->type == FormatEntity::Entry::Type::Invalid) {
          if (entry_def->children) {
            StreamString error_strm;
            error_strm.Printf("'%s' can't be specified on its own, you must "
                              "access one of its children: ",
                              entry_def->name);
            DumpCommaSeparatedChildEntryNames(error_strm, entry_def);
            error.SetErrorStringWithFormat("%s", error_strm.GetData());
          } else if (sep_char == ':') {
            // Any value whose separator is a with a ':' means this value has a
            // string argument that needs to be stored in the entry (like
            // "${script.var:}"). In this case the string value is the empty
            // string which is ok.
          } else {
            error.SetErrorStringWithFormat("%s", "invalid entry definitions");
          }
        }
      } else {
        if (entry_def->children) {
          error = ParseEntry(value, entry_def, entry);
        } else if (sep_char == ':') {
          // Any value whose separator is a with a ':' means this value has a
          // string argument that needs to be stored in the entry (like
          // "${script.var:modulename.function}")
          entry.string = value.str();
        } else {
          error.SetErrorStringWithFormat(
              "'%s' followed by '%s' but it has no children", key.str().c_str(),
              value.str().c_str());
        }
      }
      return error;
    }
  }
  StreamString error_strm;
  if (parent->type == FormatEntity::Entry::Type::Root)
    error_strm.Printf(
        "invalid top level item '%s'. Valid top level items are: ",
        key.str().c_str());
  else
    error_strm.Printf("invalid member '%s' in '%s'. Valid members are: ",
                      key.str().c_str(), parent->name);
  DumpCommaSeparatedChildEntryNames(error_strm, parent);
  error.SetErrorStringWithFormat("%s", error_strm.GetData());
  return error;
}

static const FormatEntity::Entry::Definition *
FindEntry(const llvm::StringRef &format_str,
          const FormatEntity::Entry::Definition *parent,
          llvm::StringRef &remainder) {
  Status error;

  std::pair<llvm::StringRef, llvm::StringRef> p = format_str.split('.');
  const size_t n = parent->num_children;
  for (size_t i = 0; i < n; ++i) {
    const FormatEntity::Entry::Definition *entry_def = parent->children + i;
    if (p.first.equals(entry_def->name) || entry_def->name[0] == '*') {
      if (p.second.empty()) {
        if (format_str.back() == '.')
          remainder = format_str.drop_front(format_str.size() - 1);
        else
          remainder = llvm::StringRef(); // Exact match
        return entry_def;
      } else {
        if (entry_def->children) {
          return FindEntry(p.second, entry_def, remainder);
        } else {
          remainder = p.second;
          return entry_def;
        }
      }
    }
  }
  remainder = format_str;
  return parent;
}

Status FormatEntity::ParseInternal(llvm::StringRef &format, Entry &parent_entry,
                                   uint32_t depth) {
  Status error;
  while (!format.empty() && error.Success()) {
    const size_t non_special_chars = format.find_first_of("${}\\");

    if (non_special_chars == llvm::StringRef::npos) {
      // No special characters, just string bytes so add them and we are done
      parent_entry.AppendText(format);
      return error;
    }

    if (non_special_chars > 0) {
      // We have a special character, so add all characters before these as a
      // plain string
      parent_entry.AppendText(format.substr(0, non_special_chars));
      format = format.drop_front(non_special_chars);
    }

    switch (format[0]) {
    case '\0':
      return error;

    case '{': {
      format = format.drop_front(); // Skip the '{'
      Entry scope_entry(Entry::Type::Scope);
      error = FormatEntity::ParseInternal(format, scope_entry, depth + 1);
      if (error.Fail())
        return error;
      parent_entry.AppendEntry(std::move(scope_entry));
    } break;

    case '}':
      if (depth == 0)
        error.SetErrorString("unmatched '}' character");
      else
        format =
            format
                .drop_front(); // Skip the '}' as we are at the end of the scope
      return error;

    case '\\': {
      format = format.drop_front(); // Skip the '\' character
      if (format.empty()) {
        error.SetErrorString(
            "'\\' character was not followed by another character");
        return error;
      }

      const char desens_char = format[0];
      format = format.drop_front(); // Skip the desensitized char character
      switch (desens_char) {
      case 'a':
        parent_entry.AppendChar('\a');
        break;
      case 'b':
        parent_entry.AppendChar('\b');
        break;
      case 'f':
        parent_entry.AppendChar('\f');
        break;
      case 'n':
        parent_entry.AppendChar('\n');
        break;
      case 'r':
        parent_entry.AppendChar('\r');
        break;
      case 't':
        parent_entry.AppendChar('\t');
        break;
      case 'v':
        parent_entry.AppendChar('\v');
        break;
      case '\'':
        parent_entry.AppendChar('\'');
        break;
      case '\\':
        parent_entry.AppendChar('\\');
        break;
      case '0':
        // 1 to 3 octal chars
        {
          // Make a string that can hold onto the initial zero char, up to 3
          // octal digits, and a terminating NULL.
          char oct_str[5] = {0, 0, 0, 0, 0};

          int i;
          for (i = 0; (format[i] >= '0' && format[i] <= '7') && i < 4; ++i)
            oct_str[i] = format[i];

          // We don't want to consume the last octal character since the main
          // for loop will do this for us, so we advance p by one less than i
          // (even if i is zero)
          format = format.drop_front(i);
          unsigned long octal_value = ::strtoul(oct_str, nullptr, 8);
          if (octal_value <= UINT8_MAX) {
            parent_entry.AppendChar((char)octal_value);
          } else {
            error.SetErrorString("octal number is larger than a single byte");
            return error;
          }
        }
        break;

      case 'x':
        // hex number in the format
        if (isxdigit(format[0])) {
          // Make a string that can hold onto two hex chars plus a
          // NULL terminator
          char hex_str[3] = {0, 0, 0};
          hex_str[0] = format[0];

          format = format.drop_front();

          if (isxdigit(format[0])) {
            hex_str[1] = format[0];
            format = format.drop_front();
          }

          unsigned long hex_value = strtoul(hex_str, nullptr, 16);
          if (hex_value <= UINT8_MAX) {
            parent_entry.AppendChar((char)hex_value);
          } else {
            error.SetErrorString("hex number is larger than a single byte");
            return error;
          }
        } else {
          parent_entry.AppendChar(desens_char);
        }
        break;

      default:
        // Just desensitize any other character by just printing what came
        // after the '\'
        parent_entry.AppendChar(desens_char);
        break;
      }
    } break;

    case '$':
      if (format.size() == 1) {
        // '$' at the end of a format string, just print the '$'
        parent_entry.AppendText("$");
      } else {
        format = format.drop_front(); // Skip the '$'

        if (format[0] == '{') {
          format = format.drop_front(); // Skip the '{'

          llvm::StringRef variable, variable_format;
          error = FormatEntity::ExtractVariableInfo(format, variable,
                                                    variable_format);
          if (error.Fail())
            return error;
          bool verify_is_thread_id = false;
          Entry entry;
          if (!variable_format.empty()) {
            entry.printf_format = variable_format.str();

            // If the format contains a '%' we are going to assume this is a
            // printf style format. So if you want to format your thread ID
            // using "0x%llx" you can use: ${thread.id%0x%llx}
            //
            // If there is no '%' in the format, then it is assumed to be a
            // LLDB format name, or one of the extended formats specified in
            // the switch statement below.

            if (entry.printf_format.find('%') == std::string::npos) {
              bool clear_printf = false;

              if (FormatManager::GetFormatFromCString(
                      entry.printf_format.c_str(), false, entry.fmt)) {
                // We have an LLDB format, so clear the printf format
                clear_printf = true;
              } else if (entry.printf_format.size() == 1) {
                switch (entry.printf_format[0]) {
                case '@': // if this is an @ sign, print ObjC description
                  entry.number = ValueObject::
                      eValueObjectRepresentationStyleLanguageSpecific;
                  clear_printf = true;
                  break;
                case 'V': // if this is a V, print the value using the default
                          // format
                  entry.number =
                      ValueObject::eValueObjectRepresentationStyleValue;
                  clear_printf = true;
                  break;
                case 'L': // if this is an L, print the location of the value
                  entry.number =
                      ValueObject::eValueObjectRepresentationStyleLocation;
                  clear_printf = true;
                  break;
                case 'S': // if this is an S, print the summary after all
                  entry.number =
                      ValueObject::eValueObjectRepresentationStyleSummary;
                  clear_printf = true;
                  break;
                case '#': // if this is a '#', print the number of children
                  entry.number =
                      ValueObject::eValueObjectRepresentationStyleChildrenCount;
                  clear_printf = true;
                  break;
                case 'T': // if this is a 'T', print the type
                  entry.number =
                      ValueObject::eValueObjectRepresentationStyleType;
                  clear_printf = true;
                  break;
                case 'N': // if this is a 'N', print the name
                  entry.number =
                      ValueObject::eValueObjectRepresentationStyleName;
                  clear_printf = true;
                  break;
                case '>': // if this is a '>', print the expression path
                  entry.number = ValueObject::
                      eValueObjectRepresentationStyleExpressionPath;
                  clear_printf = true;
                  break;
                default:
                  error.SetErrorStringWithFormat("invalid format: '%s'",
                                                 entry.printf_format.c_str());
                  return error;
                }
              } else if (FormatManager::GetFormatFromCString(
                             entry.printf_format.c_str(), true, entry.fmt)) {
                clear_printf = true;
              } else if (entry.printf_format == "tid") {
                verify_is_thread_id = true;
              } else {
                error.SetErrorStringWithFormat("invalid format: '%s'",
                                               entry.printf_format.c_str());
                return error;
              }

              // Our format string turned out to not be a printf style format
              // so lets clear the string
              if (clear_printf)
                entry.printf_format.clear();
            }
          }

          // Check for dereferences
          if (variable[0] == '*') {
            entry.deref = true;
            variable = variable.drop_front();
          }

          error = ParseEntry(variable, &g_root, entry);
          if (error.Fail())
            return error;

          if (verify_is_thread_id) {
            if (entry.type != Entry::Type::ThreadID &&
                entry.type != Entry::Type::ThreadProtocolID) {
              error.SetErrorString("the 'tid' format can only be used on "
                                   "${thread.id} and ${thread.protocol_id}");
            }
          }

          switch (entry.type) {
          case Entry::Type::Variable:
          case Entry::Type::VariableSynthetic:
            if (entry.number == 0) {
              if (entry.string.empty())
                entry.number =
                    ValueObject::eValueObjectRepresentationStyleValue;
              else
                entry.number =
                    ValueObject::eValueObjectRepresentationStyleSummary;
            }
            break;
          default:
            // Make sure someone didn't try to dereference anything but ${var}
            // or ${svar}
            if (entry.deref) {
              error.SetErrorStringWithFormat(
                  "${%s} can't be dereferenced, only ${var} and ${svar} can.",
                  variable.str().c_str());
              return error;
            }
          }
          // Check if this entry just wants to insert a constant string value
          // into the parent_entry, if so, insert the string with AppendText,
          // else append the entry to the parent_entry.
          if (entry.type == Entry::Type::InsertString)
            parent_entry.AppendText(entry.string.c_str());
          else
            parent_entry.AppendEntry(std::move(entry));
        }
      }
      break;
    }
  }
  return error;
}

Status FormatEntity::ExtractVariableInfo(llvm::StringRef &format_str,
                                         llvm::StringRef &variable_name,
                                         llvm::StringRef &variable_format) {
  Status error;
  variable_name = llvm::StringRef();
  variable_format = llvm::StringRef();

  const size_t paren_pos = format_str.find('}');
  if (paren_pos != llvm::StringRef::npos) {
    const size_t percent_pos = format_str.find('%');
    if (percent_pos < paren_pos) {
      if (percent_pos > 0) {
        if (percent_pos > 1)
          variable_name = format_str.substr(0, percent_pos);
        variable_format =
            format_str.substr(percent_pos + 1, paren_pos - (percent_pos + 1));
      }
    } else {
      variable_name = format_str.substr(0, paren_pos);
    }
    // Strip off elements and the formatting and the trailing '}'
    format_str = format_str.substr(paren_pos + 1);
  } else {
    error.SetErrorStringWithFormat(
        "missing terminating '}' character for '${%s'",
        format_str.str().c_str());
  }
  return error;
}

bool FormatEntity::FormatFileSpec(const FileSpec &file_spec, Stream &s,
                                  llvm::StringRef variable_name,
                                  llvm::StringRef variable_format) {
  if (variable_name.empty() || variable_name.equals(".fullpath")) {
    file_spec.Dump(&s);
    return true;
  } else if (variable_name.equals(".basename")) {
    s.PutCString(file_spec.GetFilename().AsCString(""));
    return true;
  } else if (variable_name.equals(".dirname")) {
    s.PutCString(file_spec.GetFilename().AsCString(""));
    return true;
  }
  return false;
}

static std::string MakeMatch(const llvm::StringRef &prefix,
                             const char *suffix) {
  std::string match(prefix.str());
  match.append(suffix);
  return match;
}

static void AddMatches(const FormatEntity::Entry::Definition *def,
                       const llvm::StringRef &prefix,
                       const llvm::StringRef &match_prefix,
                       StringList &matches) {
  const size_t n = def->num_children;
  if (n > 0) {
    for (size_t i = 0; i < n; ++i) {
      std::string match = prefix.str();
      if (match_prefix.empty())
        matches.AppendString(MakeMatch(prefix, def->children[i].name));
      else if (strncmp(def->children[i].name, match_prefix.data(),
                       match_prefix.size()) == 0)
        matches.AppendString(
            MakeMatch(prefix, def->children[i].name + match_prefix.size()));
    }
  }
}

size_t FormatEntity::AutoComplete(CompletionRequest &request) {
  llvm::StringRef str = request.GetCursorArgumentPrefix().str();

  request.SetWordComplete(false);
  str = str.drop_front(request.GetMatchStartPoint());

  const size_t dollar_pos = str.rfind('$');
  if (dollar_pos == llvm::StringRef::npos)
    return 0;

  // Hitting TAB after $ at the end of the string add a "{"
  if (dollar_pos == str.size() - 1) {
    std::string match = str.str();
    match.append("{");
    request.AddCompletion(match);
    return 1;
  }

  if (str[dollar_pos + 1] != '{')
    return 0;

  const size_t close_pos = str.find('}', dollar_pos + 2);
  if (close_pos != llvm::StringRef::npos)
    return 0;

  const size_t format_pos = str.find('%', dollar_pos + 2);
  if (format_pos != llvm::StringRef::npos)
    return 0;

  llvm::StringRef partial_variable(str.substr(dollar_pos + 2));
  if (partial_variable.empty()) {
    // Suggest all top level entites as we are just past "${"
    StringList new_matches;
    AddMatches(&g_root, str, llvm::StringRef(), new_matches);
    request.AddCompletions(new_matches);
    return request.GetNumberOfMatches();
  }

  // We have a partially specified variable, find it
  llvm::StringRef remainder;
  const FormatEntity::Entry::Definition *entry_def =
      FindEntry(partial_variable, &g_root, remainder);
  if (!entry_def)
    return 0;

  const size_t n = entry_def->num_children;

  if (remainder.empty()) {
    // Exact match
    if (n > 0) {
      // "${thread.info" <TAB>
      request.AddCompletion(MakeMatch(str, "."));
    } else {
      // "${thread.id" <TAB>
      request.AddCompletion(MakeMatch(str, "}"));
      request.SetWordComplete(true);
    }
  } else if (remainder.equals(".")) {
    // "${thread." <TAB>
    StringList new_matches;
    AddMatches(entry_def, str, llvm::StringRef(), new_matches);
    request.AddCompletions(new_matches);
  } else {
    // We have a partial match
    // "${thre" <TAB>
    StringList new_matches;
    AddMatches(entry_def, str, remainder, new_matches);
    request.AddCompletions(new_matches);
  }
  return request.GetNumberOfMatches();
}
