//===-- Debugger.cpp --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/Debugger.h"

#include "lldb/Breakpoint/Breakpoint.h"
#include "lldb/Core/FormatEntity.h"
#include "lldb/Core/Mangled.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/StreamAsynchronousIO.h"
#include "lldb/Core/StreamFile.h"
#include "lldb/DataFormatters/DataVisualization.h"
#include "lldb/Expression/REPL.h"
#include "lldb/Host/File.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Host/Terminal.h"
#include "lldb/Host/ThreadLauncher.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/OptionValue.h"
#include "lldb/Interpreter/OptionValueProperties.h"
#include "lldb/Interpreter/OptionValueSInt64.h"
#include "lldb/Interpreter/OptionValueString.h"
#include "lldb/Interpreter/Property.h"
#include "lldb/Interpreter/ScriptInterpreter.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Target/Language.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/StructuredDataPlugin.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/TargetList.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadList.h"
#include "lldb/Utility/AnsiTerminal.h"
#include "lldb/Utility/Event.h"
#include "lldb/Utility/Listener.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Reproducer.h"
#include "lldb/Utility/State.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StreamCallback.h"
#include "lldb/Utility/StreamString.h"

#if defined(_WIN32)
#include "lldb/Host/windows/PosixApi.h"
#include "lldb/Host/windows/windows.h"
#endif

#include "llvm/ADT/None.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/raw_ostream.h"

#include <list>
#include <memory>
#include <mutex>
#include <set>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <system_error>

namespace lldb_private {
class Address;
}

using namespace lldb;
using namespace lldb_private;

static lldb::user_id_t g_unique_id = 1;
static size_t g_debugger_event_thread_stack_bytes = 8 * 1024 * 1024;

#pragma mark Static Functions

typedef std::vector<DebuggerSP> DebuggerList;
static std::recursive_mutex *g_debugger_list_mutex_ptr =
    nullptr; // NOTE: intentional leak to avoid issues with C++ destructor chain
static DebuggerList *g_debugger_list_ptr =
    nullptr; // NOTE: intentional leak to avoid issues with C++ destructor chain

static constexpr OptionEnumValueElement g_show_disassembly_enum_values[] = {
    {Debugger::eStopDisassemblyTypeNever, "never",
     "Never show disassembly when displaying a stop context."},
    {Debugger::eStopDisassemblyTypeNoDebugInfo, "no-debuginfo",
     "Show disassembly when there is no debug information."},
    {Debugger::eStopDisassemblyTypeNoSource, "no-source",
     "Show disassembly when there is no source information, or the source file "
     "is missing when displaying a stop context."},
    {Debugger::eStopDisassemblyTypeAlways, "always",
     "Always show disassembly when displaying a stop context."} };

static constexpr OptionEnumValueElement g_language_enumerators[] = {
    {eScriptLanguageNone, "none", "Disable scripting languages."},
    {eScriptLanguagePython, "python",
     "Select python as the default scripting language."},
    {eScriptLanguageDefault, "default",
     "Select the lldb default as the default scripting language."} };

#define MODULE_WITH_FUNC                                                       \
  "{ "                                                                         \
  "${module.file.basename}{`${function.name-with-args}"                        \
  "{${frame.no-debug}${function.pc-offset}}}}"

#define MODULE_WITH_FUNC_NO_ARGS                                               \
  "{ "                                                                         \
  "${module.file.basename}{`${function.name-without-args}"                     \
  "{${frame.no-debug}${function.pc-offset}}}}"

#define FILE_AND_LINE                                                          \
  "{ at ${line.file.basename}:${line.number}{:${line.column}}}"
#define IS_OPTIMIZED "{${function.is-optimized} [opt]}"

#define IS_ARTIFICIAL "{${frame.is-artificial} [artificial]}"

#define DEFAULT_THREAD_FORMAT                                                  \
  "thread #${thread.index}: tid = ${thread.id%tid}"                            \
  "{, ${frame.pc}}" MODULE_WITH_FUNC FILE_AND_LINE                             \
  "{, name = '${thread.name}'}"                                                \
  "{, queue = '${thread.queue}'}"                                              \
  "{, activity = '${thread.info.activity.name}'}"                              \
  "{, ${thread.info.trace_messages} messages}"                                 \
  "{, stop reason = ${thread.stop-reason}}"                                    \
  "{\\nReturn value: ${thread.return-value}}"                                  \
  "{\\nCompleted expression: ${thread.completed-expression}}"                  \
  "\\n"

#define DEFAULT_THREAD_STOP_FORMAT                                             \
  "thread #${thread.index}{, name = '${thread.name}'}"                         \
  "{, queue = '${thread.queue}'}"                                              \
  "{, activity = '${thread.info.activity.name}'}"                              \
  "{, ${thread.info.trace_messages} messages}"                                 \
  "{, stop reason = ${thread.stop-reason}}"                                    \
  "{\\nReturn value: ${thread.return-value}}"                                  \
  "{\\nCompleted expression: ${thread.completed-expression}}"                  \
  "\\n"

#define DEFAULT_FRAME_FORMAT                                                   \
  "frame #${frame.index}: ${frame.pc}" MODULE_WITH_FUNC FILE_AND_LINE          \
      IS_OPTIMIZED IS_ARTIFICIAL "\\n"

#define DEFAULT_FRAME_FORMAT_NO_ARGS                                           \
  "frame #${frame.index}: ${frame.pc}" MODULE_WITH_FUNC_NO_ARGS FILE_AND_LINE  \
      IS_OPTIMIZED IS_ARTIFICIAL "\\n"

// Three parts to this disassembly format specification:
//   1. If this is a new function/symbol (no previous symbol/function), print
//      dylib`funcname:\n
//   2. If this is a symbol context change (different from previous
//   symbol/function), print
//      dylib`funcname:\n
//   3. print
//      address <+offset>:
#define DEFAULT_DISASSEMBLY_FORMAT                                             \
  "{${function.initial-function}{${module.file.basename}`}{${function.name-"   \
  "without-args}}:\\n}{${function.changed}\\n{${module.file.basename}`}{${"    \
  "function.name-without-args}}:\\n}{${current-pc-arrow} "                     \
  "}${addr-file-or-load}{ "                                                    \
  "<${function.concrete-only-addr-offset-no-padding}>}: "

// gdb's disassembly format can be emulated with ${current-pc-arrow}${addr-
// file-or-load}{ <${function.name-without-args}${function.concrete-only-addr-
// offset-no-padding}>}:

// lldb's original format for disassembly would look like this format string -
// {${function.initial-function}{${module.file.basename}`}{${function.name-
// without-
// args}}:\n}{${function.changed}\n{${module.file.basename}`}{${function.name-
// without-args}}:\n}{${current-pc-arrow} }{${addr-file-or-load}}:

static constexpr OptionEnumValueElement s_stop_show_column_values[] = {
    {eStopShowColumnAnsiOrCaret, "ansi-or-caret",
     "Highlight the stop column with ANSI terminal codes when color/ANSI mode "
     "is enabled; otherwise, fall back to using a text-only caret (^) as if "
     "\"caret-only\" mode was selected."},
    {eStopShowColumnAnsi, "ansi", "Highlight the stop column with ANSI "
                                  "terminal codes when running LLDB with "
                                  "color/ANSI enabled."},
    {eStopShowColumnCaret, "caret",
     "Highlight the stop column with a caret character (^) underneath the stop "
     "column. This method introduces a new line in source listings that "
     "display thread stop locations."},
    {eStopShowColumnNone, "none", "Do not highlight the stop column."}};

static constexpr PropertyDefinition g_properties[] = {
    {"auto-confirm", OptionValue::eTypeBoolean, true, false, nullptr, {},
     "If true all confirmation prompts will receive their default reply."},
    {"disassembly-format", OptionValue::eTypeFormatEntity, true, 0,
     DEFAULT_DISASSEMBLY_FORMAT, {},
     "The default disassembly format "
     "string to use when disassembling "
     "instruction sequences."},
    {"frame-format", OptionValue::eTypeFormatEntity, true, 0,
     DEFAULT_FRAME_FORMAT, {},
     "The default frame format string to use "
     "when displaying stack frame information "
     "for threads."},
    {"notify-void", OptionValue::eTypeBoolean, true, false, nullptr, {},
     "Notify the user explicitly if an expression returns void (default: "
     "false)."},
    {"prompt", OptionValue::eTypeString, true,
     OptionValueString::eOptionEncodeCharacterEscapeSequences, "(lldb) ", {},
     "The debugger command line prompt displayed for the user."},
    {"script-lang", OptionValue::eTypeEnum, true, eScriptLanguagePython,
     nullptr, OptionEnumValues(g_language_enumerators),
     "The script language to be used for evaluating user-written scripts."},
    {"stop-disassembly-count", OptionValue::eTypeSInt64, true, 4, nullptr, {},
     "The number of disassembly lines to show when displaying a "
     "stopped context."},
    {"stop-disassembly-display", OptionValue::eTypeEnum, true,
     Debugger::eStopDisassemblyTypeNoDebugInfo, nullptr,
     OptionEnumValues(g_show_disassembly_enum_values),
     "Control when to display disassembly when displaying a stopped context."},
    {"stop-line-count-after", OptionValue::eTypeSInt64, true, 3, nullptr, {},
     "The number of sources lines to display that come after the "
     "current source line when displaying a stopped context."},
    {"stop-line-count-before", OptionValue::eTypeSInt64, true, 3, nullptr, {},
     "The number of sources lines to display that come before the "
     "current source line when displaying a stopped context."},
    {"highlight-source", OptionValue::eTypeBoolean, true, true, nullptr, {},
     "If true, LLDB will highlight the displayed source code."},
    {"stop-show-column", OptionValue::eTypeEnum, false,
     eStopShowColumnAnsiOrCaret, nullptr, OptionEnumValues(s_stop_show_column_values),
     "If true, LLDB will use the column information from the debug info to "
     "mark the current position when displaying a stopped context."},
    {"stop-show-column-ansi-prefix", OptionValue::eTypeString, true, 0,
     "${ansi.underline}", {},
     "When displaying the column marker in a color-enabled (i.e. ANSI) "
     "terminal, use the ANSI terminal code specified in this format at the "
     "immediately before the column to be marked."},
    {"stop-show-column-ansi-suffix", OptionValue::eTypeString, true, 0,
     "${ansi.normal}", {},
     "When displaying the column marker in a color-enabled (i.e. ANSI) "
     "terminal, use the ANSI terminal code specified in this format "
     "immediately after the column to be marked."},
    {"term-width", OptionValue::eTypeSInt64, true, 80, nullptr, {},
     "The maximum number of columns to use for displaying text."},
    {"thread-format", OptionValue::eTypeFormatEntity, true, 0,
     DEFAULT_THREAD_FORMAT, {},
     "The default thread format string to use "
     "when displaying thread information."},
    {"thread-stop-format", OptionValue::eTypeFormatEntity, true, 0,
     DEFAULT_THREAD_STOP_FORMAT, {},
     "The default thread format  "
     "string to use when displaying thread "
     "information as part of the stop display."},
    {"use-external-editor", OptionValue::eTypeBoolean, true, false, nullptr, {},
     "Whether to use an external editor or not."},
    {"use-color", OptionValue::eTypeBoolean, true, true, nullptr, {},
     "Whether to use Ansi color codes or not."},
    {"auto-one-line-summaries", OptionValue::eTypeBoolean, true, true, nullptr,
     {},
     "If true, LLDB will automatically display small structs in "
     "one-liner format (default: true)."},
    {"auto-indent", OptionValue::eTypeBoolean, true, true, nullptr, {},
     "If true, LLDB will auto indent/outdent code. Currently only supported in "
     "the REPL (default: true)."},
    {"print-decls", OptionValue::eTypeBoolean, true, true, nullptr, {},
     "If true, LLDB will print the values of variables declared in an "
     "expression. Currently only supported in the REPL (default: true)."},
    {"tab-size", OptionValue::eTypeUInt64, true, 4, nullptr, {},
     "The tab size to use when indenting code in multi-line input mode "
     "(default: 4)."},
    {"escape-non-printables", OptionValue::eTypeBoolean, true, true, nullptr,
     {},
     "If true, LLDB will automatically escape non-printable and "
     "escape characters when formatting strings."},
    {"frame-format-unique", OptionValue::eTypeFormatEntity, true, 0,
     DEFAULT_FRAME_FORMAT_NO_ARGS, {},
     "The default frame format string to use when displaying stack frame"
     "information for threads from thread backtrace unique."}};

enum {
  ePropertyAutoConfirm = 0,
  ePropertyDisassemblyFormat,
  ePropertyFrameFormat,
  ePropertyNotiftVoid,
  ePropertyPrompt,
  ePropertyScriptLanguage,
  ePropertyStopDisassemblyCount,
  ePropertyStopDisassemblyDisplay,
  ePropertyStopLineCountAfter,
  ePropertyStopLineCountBefore,
  ePropertyHighlightSource,
  ePropertyStopShowColumn,
  ePropertyStopShowColumnAnsiPrefix,
  ePropertyStopShowColumnAnsiSuffix,
  ePropertyTerminalWidth,
  ePropertyThreadFormat,
  ePropertyThreadStopFormat,
  ePropertyUseExternalEditor,
  ePropertyUseColor,
  ePropertyAutoOneLineSummaries,
  ePropertyAutoIndent,
  ePropertyPrintDecls,
  ePropertyTabSize,
  ePropertyEscapeNonPrintables,
  ePropertyFrameFormatUnique,
};

LoadPluginCallbackType Debugger::g_load_plugin_callback = nullptr;

Status Debugger::SetPropertyValue(const ExecutionContext *exe_ctx,
                                  VarSetOperationType op,
                                  llvm::StringRef property_path,
                                  llvm::StringRef value) {
  bool is_load_script = (property_path == "target.load-script-from-symbol-file");
  bool is_escape_non_printables = (property_path == "escape-non-printables");
  TargetSP target_sp;
  LoadScriptFromSymFile load_script_old_value;
  if (is_load_script && exe_ctx->GetTargetSP()) {
    target_sp = exe_ctx->GetTargetSP();
    load_script_old_value =
        target_sp->TargetProperties::GetLoadScriptFromSymbolFile();
  }
  Status error(Properties::SetPropertyValue(exe_ctx, op, property_path, value));
  if (error.Success()) {
    // FIXME it would be nice to have "on-change" callbacks for properties
    if (property_path == g_properties[ePropertyPrompt].name) {
      llvm::StringRef new_prompt = GetPrompt();
      std::string str = lldb_utility::ansi::FormatAnsiTerminalCodes(
          new_prompt, GetUseColor());
      if (str.length())
        new_prompt = str;
      GetCommandInterpreter().UpdatePrompt(new_prompt);
      auto bytes = llvm::make_unique<EventDataBytes>(new_prompt);
      auto prompt_change_event_sp = std::make_shared<Event>(
          CommandInterpreter::eBroadcastBitResetPrompt, bytes.release());
      GetCommandInterpreter().BroadcastEvent(prompt_change_event_sp);
    } else if (property_path == g_properties[ePropertyUseColor].name) {
      // use-color changed. Ping the prompt so it can reset the ansi terminal
      // codes.
      SetPrompt(GetPrompt());
    } else if (is_load_script && target_sp &&
               load_script_old_value == eLoadScriptFromSymFileWarn) {
      if (target_sp->TargetProperties::GetLoadScriptFromSymbolFile() ==
          eLoadScriptFromSymFileTrue) {
        std::list<Status> errors;
        StreamString feedback_stream;
        if (!target_sp->LoadScriptingResources(errors, &feedback_stream)) {
          StreamFileSP stream_sp(GetErrorFile());
          if (stream_sp) {
            for (auto error : errors) {
              stream_sp->Printf("%s\n", error.AsCString());
            }
            if (feedback_stream.GetSize())
              stream_sp->PutCString(feedback_stream.GetString());
          }
        }
      }
    } else if (is_escape_non_printables) {
      DataVisualization::ForceUpdate();
    }
  }
  return error;
}

bool Debugger::GetAutoConfirm() const {
  const uint32_t idx = ePropertyAutoConfirm;
  return m_collection_sp->GetPropertyAtIndexAsBoolean(
      nullptr, idx, g_properties[idx].default_uint_value != 0);
}

const FormatEntity::Entry *Debugger::GetDisassemblyFormat() const {
  const uint32_t idx = ePropertyDisassemblyFormat;
  return m_collection_sp->GetPropertyAtIndexAsFormatEntity(nullptr, idx);
}

const FormatEntity::Entry *Debugger::GetFrameFormat() const {
  const uint32_t idx = ePropertyFrameFormat;
  return m_collection_sp->GetPropertyAtIndexAsFormatEntity(nullptr, idx);
}

const FormatEntity::Entry *Debugger::GetFrameFormatUnique() const {
  const uint32_t idx = ePropertyFrameFormatUnique;
  return m_collection_sp->GetPropertyAtIndexAsFormatEntity(nullptr, idx);
}

bool Debugger::GetNotifyVoid() const {
  const uint32_t idx = ePropertyNotiftVoid;
  return m_collection_sp->GetPropertyAtIndexAsBoolean(
      nullptr, idx, g_properties[idx].default_uint_value != 0);
}

llvm::StringRef Debugger::GetPrompt() const {
  const uint32_t idx = ePropertyPrompt;
  return m_collection_sp->GetPropertyAtIndexAsString(
      nullptr, idx, g_properties[idx].default_cstr_value);
}

void Debugger::SetPrompt(llvm::StringRef p) {
  const uint32_t idx = ePropertyPrompt;
  m_collection_sp->SetPropertyAtIndexAsString(nullptr, idx, p);
  llvm::StringRef new_prompt = GetPrompt();
  std::string str =
      lldb_utility::ansi::FormatAnsiTerminalCodes(new_prompt, GetUseColor());
  if (str.length())
    new_prompt = str;
  GetCommandInterpreter().UpdatePrompt(new_prompt);
}

llvm::StringRef Debugger::GetReproducerPath() const {
  auto &r = repro::Reproducer::Instance();
  return r.GetReproducerPath().GetCString();
}

const FormatEntity::Entry *Debugger::GetThreadFormat() const {
  const uint32_t idx = ePropertyThreadFormat;
  return m_collection_sp->GetPropertyAtIndexAsFormatEntity(nullptr, idx);
}

const FormatEntity::Entry *Debugger::GetThreadStopFormat() const {
  const uint32_t idx = ePropertyThreadStopFormat;
  return m_collection_sp->GetPropertyAtIndexAsFormatEntity(nullptr, idx);
}

lldb::ScriptLanguage Debugger::GetScriptLanguage() const {
  const uint32_t idx = ePropertyScriptLanguage;
  return (lldb::ScriptLanguage)m_collection_sp->GetPropertyAtIndexAsEnumeration(
      nullptr, idx, g_properties[idx].default_uint_value);
}

bool Debugger::SetScriptLanguage(lldb::ScriptLanguage script_lang) {
  const uint32_t idx = ePropertyScriptLanguage;
  return m_collection_sp->SetPropertyAtIndexAsEnumeration(nullptr, idx,
                                                          script_lang);
}

uint32_t Debugger::GetTerminalWidth() const {
  const uint32_t idx = ePropertyTerminalWidth;
  return m_collection_sp->GetPropertyAtIndexAsSInt64(
      nullptr, idx, g_properties[idx].default_uint_value);
}

bool Debugger::SetTerminalWidth(uint32_t term_width) {
  const uint32_t idx = ePropertyTerminalWidth;
  return m_collection_sp->SetPropertyAtIndexAsSInt64(nullptr, idx, term_width);
}

bool Debugger::GetUseExternalEditor() const {
  const uint32_t idx = ePropertyUseExternalEditor;
  return m_collection_sp->GetPropertyAtIndexAsBoolean(
      nullptr, idx, g_properties[idx].default_uint_value != 0);
}

bool Debugger::SetUseExternalEditor(bool b) {
  const uint32_t idx = ePropertyUseExternalEditor;
  return m_collection_sp->SetPropertyAtIndexAsBoolean(nullptr, idx, b);
}

bool Debugger::GetUseColor() const {
  const uint32_t idx = ePropertyUseColor;
  return m_collection_sp->GetPropertyAtIndexAsBoolean(
      nullptr, idx, g_properties[idx].default_uint_value != 0);
}

bool Debugger::SetUseColor(bool b) {
  const uint32_t idx = ePropertyUseColor;
  bool ret = m_collection_sp->SetPropertyAtIndexAsBoolean(nullptr, idx, b);
  SetPrompt(GetPrompt());
  return ret;
}

bool Debugger::GetHighlightSource() const {
  const uint32_t idx = ePropertyHighlightSource;
  return m_collection_sp->GetPropertyAtIndexAsBoolean(
      nullptr, idx, g_properties[idx].default_uint_value);
}

StopShowColumn Debugger::GetStopShowColumn() const {
  const uint32_t idx = ePropertyStopShowColumn;
  return (lldb::StopShowColumn)m_collection_sp->GetPropertyAtIndexAsEnumeration(
      nullptr, idx, g_properties[idx].default_uint_value);
}

llvm::StringRef Debugger::GetStopShowColumnAnsiPrefix() const {
  const uint32_t idx = ePropertyStopShowColumnAnsiPrefix;
  return m_collection_sp->GetPropertyAtIndexAsString(nullptr, idx, "");
}

llvm::StringRef Debugger::GetStopShowColumnAnsiSuffix() const {
  const uint32_t idx = ePropertyStopShowColumnAnsiSuffix;
  return m_collection_sp->GetPropertyAtIndexAsString(nullptr, idx, "");
}

uint32_t Debugger::GetStopSourceLineCount(bool before) const {
  const uint32_t idx =
      before ? ePropertyStopLineCountBefore : ePropertyStopLineCountAfter;
  return m_collection_sp->GetPropertyAtIndexAsSInt64(
      nullptr, idx, g_properties[idx].default_uint_value);
}

Debugger::StopDisassemblyType Debugger::GetStopDisassemblyDisplay() const {
  const uint32_t idx = ePropertyStopDisassemblyDisplay;
  return (Debugger::StopDisassemblyType)
      m_collection_sp->GetPropertyAtIndexAsEnumeration(
          nullptr, idx, g_properties[idx].default_uint_value);
}

uint32_t Debugger::GetDisassemblyLineCount() const {
  const uint32_t idx = ePropertyStopDisassemblyCount;
  return m_collection_sp->GetPropertyAtIndexAsSInt64(
      nullptr, idx, g_properties[idx].default_uint_value);
}

bool Debugger::GetAutoOneLineSummaries() const {
  const uint32_t idx = ePropertyAutoOneLineSummaries;
  return m_collection_sp->GetPropertyAtIndexAsBoolean(nullptr, idx, true);
}

bool Debugger::GetEscapeNonPrintables() const {
  const uint32_t idx = ePropertyEscapeNonPrintables;
  return m_collection_sp->GetPropertyAtIndexAsBoolean(nullptr, idx, true);
}

bool Debugger::GetAutoIndent() const {
  const uint32_t idx = ePropertyAutoIndent;
  return m_collection_sp->GetPropertyAtIndexAsBoolean(nullptr, idx, true);
}

bool Debugger::SetAutoIndent(bool b) {
  const uint32_t idx = ePropertyAutoIndent;
  return m_collection_sp->SetPropertyAtIndexAsBoolean(nullptr, idx, b);
}

bool Debugger::GetPrintDecls() const {
  const uint32_t idx = ePropertyPrintDecls;
  return m_collection_sp->GetPropertyAtIndexAsBoolean(nullptr, idx, true);
}

bool Debugger::SetPrintDecls(bool b) {
  const uint32_t idx = ePropertyPrintDecls;
  return m_collection_sp->SetPropertyAtIndexAsBoolean(nullptr, idx, b);
}

uint32_t Debugger::GetTabSize() const {
  const uint32_t idx = ePropertyTabSize;
  return m_collection_sp->GetPropertyAtIndexAsUInt64(
      nullptr, idx, g_properties[idx].default_uint_value);
}

bool Debugger::SetTabSize(uint32_t tab_size) {
  const uint32_t idx = ePropertyTabSize;
  return m_collection_sp->SetPropertyAtIndexAsUInt64(nullptr, idx, tab_size);
}

#pragma mark Debugger

// const DebuggerPropertiesSP &
// Debugger::GetSettings() const
//{
//    return m_properties_sp;
//}
//

void Debugger::Initialize(LoadPluginCallbackType load_plugin_callback) {
  assert(g_debugger_list_ptr == nullptr &&
         "Debugger::Initialize called more than once!");
  g_debugger_list_mutex_ptr = new std::recursive_mutex();
  g_debugger_list_ptr = new DebuggerList();
  g_load_plugin_callback = load_plugin_callback;
}

void Debugger::Terminate() {
  assert(g_debugger_list_ptr &&
         "Debugger::Terminate called without a matching Debugger::Initialize!");

  if (g_debugger_list_ptr && g_debugger_list_mutex_ptr) {
    // Clear our master list of debugger objects
    {
      std::lock_guard<std::recursive_mutex> guard(*g_debugger_list_mutex_ptr);
      for (const auto &debugger : *g_debugger_list_ptr)
        debugger->Clear();
      g_debugger_list_ptr->clear();
    }
  }
}

void Debugger::SettingsInitialize() { Target::SettingsInitialize(); }

void Debugger::SettingsTerminate() { Target::SettingsTerminate(); }

bool Debugger::LoadPlugin(const FileSpec &spec, Status &error) {
  if (g_load_plugin_callback) {
    llvm::sys::DynamicLibrary dynlib =
        g_load_plugin_callback(shared_from_this(), spec, error);
    if (dynlib.isValid()) {
      m_loaded_plugins.push_back(dynlib);
      return true;
    }
  } else {
    // The g_load_plugin_callback is registered in SBDebugger::Initialize() and
    // if the public API layer isn't available (code is linking against all of
    // the internal LLDB static libraries), then we can't load plugins
    error.SetErrorString("Public API layer is not available");
  }
  return false;
}

static FileSystem::EnumerateDirectoryResult
LoadPluginCallback(void *baton, llvm::sys::fs::file_type ft,
                   llvm::StringRef path) {
  Status error;

  static ConstString g_dylibext(".dylib");
  static ConstString g_solibext(".so");

  if (!baton)
    return FileSystem::eEnumerateDirectoryResultQuit;

  Debugger *debugger = (Debugger *)baton;

  namespace fs = llvm::sys::fs;
  // If we have a regular file, a symbolic link or unknown file type, try and
  // process the file. We must handle unknown as sometimes the directory
  // enumeration might be enumerating a file system that doesn't have correct
  // file type information.
  if (ft == fs::file_type::regular_file || ft == fs::file_type::symlink_file ||
      ft == fs::file_type::type_unknown) {
    FileSpec plugin_file_spec(path);
    FileSystem::Instance().Resolve(plugin_file_spec);

    if (plugin_file_spec.GetFileNameExtension() != g_dylibext &&
        plugin_file_spec.GetFileNameExtension() != g_solibext) {
      return FileSystem::eEnumerateDirectoryResultNext;
    }

    Status plugin_load_error;
    debugger->LoadPlugin(plugin_file_spec, plugin_load_error);

    return FileSystem::eEnumerateDirectoryResultNext;
  } else if (ft == fs::file_type::directory_file ||
             ft == fs::file_type::symlink_file ||
             ft == fs::file_type::type_unknown) {
    // Try and recurse into anything that a directory or symbolic link. We must
    // also do this for unknown as sometimes the directory enumeration might be
    // enumerating a file system that doesn't have correct file type
    // information.
    return FileSystem::eEnumerateDirectoryResultEnter;
  }

  return FileSystem::eEnumerateDirectoryResultNext;
}

void Debugger::InstanceInitialize() {
  const bool find_directories = true;
  const bool find_files = true;
  const bool find_other = true;
  char dir_path[PATH_MAX];
  if (FileSpec dir_spec = HostInfo::GetSystemPluginDir()) {
    if (FileSystem::Instance().Exists(dir_spec) &&
        dir_spec.GetPath(dir_path, sizeof(dir_path))) {
      FileSystem::Instance().EnumerateDirectory(dir_path, find_directories,
                                                find_files, find_other,
                                                LoadPluginCallback, this);
    }
  }

  if (FileSpec dir_spec = HostInfo::GetUserPluginDir()) {
    if (FileSystem::Instance().Exists(dir_spec) &&
        dir_spec.GetPath(dir_path, sizeof(dir_path))) {
      FileSystem::Instance().EnumerateDirectory(dir_path, find_directories,
                                                find_files, find_other,
                                                LoadPluginCallback, this);
    }
  }

  PluginManager::DebuggerInitialize(*this);
}

DebuggerSP Debugger::CreateInstance(lldb::LogOutputCallback log_callback,
                                    void *baton) {
  DebuggerSP debugger_sp(new Debugger(log_callback, baton));
  if (g_debugger_list_ptr && g_debugger_list_mutex_ptr) {
    std::lock_guard<std::recursive_mutex> guard(*g_debugger_list_mutex_ptr);
    g_debugger_list_ptr->push_back(debugger_sp);
  }
  debugger_sp->InstanceInitialize();
  return debugger_sp;
}

void Debugger::Destroy(DebuggerSP &debugger_sp) {
  if (!debugger_sp)
    return;

  debugger_sp->Clear();

  if (g_debugger_list_ptr && g_debugger_list_mutex_ptr) {
    std::lock_guard<std::recursive_mutex> guard(*g_debugger_list_mutex_ptr);
    DebuggerList::iterator pos, end = g_debugger_list_ptr->end();
    for (pos = g_debugger_list_ptr->begin(); pos != end; ++pos) {
      if ((*pos).get() == debugger_sp.get()) {
        g_debugger_list_ptr->erase(pos);
        return;
      }
    }
  }
}

DebuggerSP
Debugger::FindDebuggerWithInstanceName(const ConstString &instance_name) {
  DebuggerSP debugger_sp;
  if (g_debugger_list_ptr && g_debugger_list_mutex_ptr) {
    std::lock_guard<std::recursive_mutex> guard(*g_debugger_list_mutex_ptr);
    DebuggerList::iterator pos, end = g_debugger_list_ptr->end();
    for (pos = g_debugger_list_ptr->begin(); pos != end; ++pos) {
      if ((*pos)->m_instance_name == instance_name) {
        debugger_sp = *pos;
        break;
      }
    }
  }
  return debugger_sp;
}

TargetSP Debugger::FindTargetWithProcessID(lldb::pid_t pid) {
  TargetSP target_sp;
  if (g_debugger_list_ptr && g_debugger_list_mutex_ptr) {
    std::lock_guard<std::recursive_mutex> guard(*g_debugger_list_mutex_ptr);
    DebuggerList::iterator pos, end = g_debugger_list_ptr->end();
    for (pos = g_debugger_list_ptr->begin(); pos != end; ++pos) {
      target_sp = (*pos)->GetTargetList().FindTargetWithProcessID(pid);
      if (target_sp)
        break;
    }
  }
  return target_sp;
}

TargetSP Debugger::FindTargetWithProcess(Process *process) {
  TargetSP target_sp;
  if (g_debugger_list_ptr && g_debugger_list_mutex_ptr) {
    std::lock_guard<std::recursive_mutex> guard(*g_debugger_list_mutex_ptr);
    DebuggerList::iterator pos, end = g_debugger_list_ptr->end();
    for (pos = g_debugger_list_ptr->begin(); pos != end; ++pos) {
      target_sp = (*pos)->GetTargetList().FindTargetWithProcess(process);
      if (target_sp)
        break;
    }
  }
  return target_sp;
}

Debugger::Debugger(lldb::LogOutputCallback log_callback, void *baton)
    : UserID(g_unique_id++),
      Properties(std::make_shared<OptionValueProperties>()),
      m_input_file_sp(std::make_shared<StreamFile>(stdin, false)),
      m_output_file_sp(std::make_shared<StreamFile>(stdout, false)),
      m_error_file_sp(std::make_shared<StreamFile>(stderr, false)),
      m_broadcaster_manager_sp(BroadcasterManager::MakeBroadcasterManager()),
      m_terminal_state(), m_target_list(*this), m_platform_list(),
      m_listener_sp(Listener::MakeListener("lldb.Debugger")),
      m_source_manager_ap(), m_source_file_cache(),
      m_command_interpreter_ap(llvm::make_unique<CommandInterpreter>(
          *this, eScriptLanguageDefault, false)),
      m_input_reader_stack(), m_instance_name(), m_loaded_plugins(),
      m_event_handler_thread(), m_io_handler_thread(),
      m_sync_broadcaster(nullptr, "lldb.debugger.sync"),
      m_forward_listener_sp(), m_clear_once() {
  char instance_cstr[256];
  snprintf(instance_cstr, sizeof(instance_cstr), "debugger_%d", (int)GetID());
  m_instance_name.SetCString(instance_cstr);
  if (log_callback)
    m_log_callback_stream_sp =
        std::make_shared<StreamCallback>(log_callback, baton);
  m_command_interpreter_ap->Initialize();
  // Always add our default platform to the platform list
  PlatformSP default_platform_sp(Platform::GetHostPlatform());
  assert(default_platform_sp);
  m_platform_list.Append(default_platform_sp, true);

  m_collection_sp->Initialize(g_properties);
  m_collection_sp->AppendProperty(
      ConstString("target"),
      ConstString("Settings specify to debugging targets."), true,
      Target::GetGlobalProperties()->GetValueProperties());
  m_collection_sp->AppendProperty(
      ConstString("platform"), ConstString("Platform settings."), true,
      Platform::GetGlobalPlatformProperties()->GetValueProperties());
  m_collection_sp->AppendProperty(
      ConstString("symbols"), ConstString("Symbol lookup and cache settings."),
      true, ModuleList::GetGlobalModuleListProperties().GetValueProperties());
  if (m_command_interpreter_ap) {
    m_collection_sp->AppendProperty(
        ConstString("interpreter"),
        ConstString("Settings specify to the debugger's command interpreter."),
        true, m_command_interpreter_ap->GetValueProperties());
  }
  OptionValueSInt64 *term_width =
      m_collection_sp->GetPropertyAtIndexAsOptionValueSInt64(
          nullptr, ePropertyTerminalWidth);
  term_width->SetMinimumValue(10);
  term_width->SetMaximumValue(1024);

  // Turn off use-color if this is a dumb terminal.
  const char *term = getenv("TERM");
  if (term && !strcmp(term, "dumb"))
    SetUseColor(false);
  // Turn off use-color if we don't write to a terminal with color support.
  if (!m_output_file_sp->GetFile().GetIsTerminalWithColors())
    SetUseColor(false);

#if defined(_WIN32) && defined(ENABLE_VIRTUAL_TERMINAL_PROCESSING)
  // Enabling use of ANSI color codes because LLDB is using them to highlight
  // text.
  llvm::sys::Process::UseANSIEscapeCodes(true);
#endif
}

Debugger::~Debugger() { Clear(); }

void Debugger::Clear() {
  //----------------------------------------------------------------------
  // Make sure we call this function only once. With the C++ global destructor
  // chain having a list of debuggers and with code that can be running on
  // other threads, we need to ensure this doesn't happen multiple times.
  //
  // The following functions call Debugger::Clear():
  //     Debugger::~Debugger();
  //     static void Debugger::Destroy(lldb::DebuggerSP &debugger_sp);
  //     static void Debugger::Terminate();
  //----------------------------------------------------------------------
  llvm::call_once(m_clear_once, [this]() {
    ClearIOHandlers();
    StopIOHandlerThread();
    StopEventHandlerThread();
    m_listener_sp->Clear();
    int num_targets = m_target_list.GetNumTargets();
    for (int i = 0; i < num_targets; i++) {
      TargetSP target_sp(m_target_list.GetTargetAtIndex(i));
      if (target_sp) {
        ProcessSP process_sp(target_sp->GetProcessSP());
        if (process_sp)
          process_sp->Finalize();
        target_sp->Destroy();
      }
    }
    m_broadcaster_manager_sp->Clear();

    // Close the input file _before_ we close the input read communications
    // class as it does NOT own the input file, our m_input_file does.
    m_terminal_state.Clear();
    if (m_input_file_sp)
      m_input_file_sp->GetFile().Close();

    m_command_interpreter_ap->Clear();
  });
}

bool Debugger::GetCloseInputOnEOF() const {
  //    return m_input_comm.GetCloseOnEOF();
  return false;
}

void Debugger::SetCloseInputOnEOF(bool b) {
  //    m_input_comm.SetCloseOnEOF(b);
}

bool Debugger::GetAsyncExecution() {
  return !m_command_interpreter_ap->GetSynchronous();
}

void Debugger::SetAsyncExecution(bool async_execution) {
  m_command_interpreter_ap->SetSynchronous(!async_execution);
}

void Debugger::SetInputFileHandle(FILE *fh, bool tranfer_ownership) {
  if (m_input_file_sp)
    m_input_file_sp->GetFile().SetStream(fh, tranfer_ownership);
  else
    m_input_file_sp = std::make_shared<StreamFile>(fh, tranfer_ownership);

  File &in_file = m_input_file_sp->GetFile();
  if (!in_file.IsValid())
    in_file.SetStream(stdin, true);

  // Save away the terminal state if that is relevant, so that we can restore
  // it in RestoreInputState.
  SaveInputTerminalState();
}

void Debugger::SetOutputFileHandle(FILE *fh, bool tranfer_ownership) {
  if (m_output_file_sp)
    m_output_file_sp->GetFile().SetStream(fh, tranfer_ownership);
  else
    m_output_file_sp = std::make_shared<StreamFile>(fh, tranfer_ownership);

  File &out_file = m_output_file_sp->GetFile();
  if (!out_file.IsValid())
    out_file.SetStream(stdout, false);

  // do not create the ScriptInterpreter just for setting the output file
  // handle as the constructor will know how to do the right thing on its own
  const bool can_create = false;
  ScriptInterpreter *script_interpreter =
      GetCommandInterpreter().GetScriptInterpreter(can_create);
  if (script_interpreter)
    script_interpreter->ResetOutputFileHandle(fh);
}

void Debugger::SetErrorFileHandle(FILE *fh, bool tranfer_ownership) {
  if (m_error_file_sp)
    m_error_file_sp->GetFile().SetStream(fh, tranfer_ownership);
  else
    m_error_file_sp = std::make_shared<StreamFile>(fh, tranfer_ownership);

  File &err_file = m_error_file_sp->GetFile();
  if (!err_file.IsValid())
    err_file.SetStream(stderr, false);
}

void Debugger::SaveInputTerminalState() {
  if (m_input_file_sp) {
    File &in_file = m_input_file_sp->GetFile();
    if (in_file.GetDescriptor() != File::kInvalidDescriptor)
      m_terminal_state.Save(in_file.GetDescriptor(), true);
  }
}

void Debugger::RestoreInputTerminalState() { m_terminal_state.Restore(); }

ExecutionContext Debugger::GetSelectedExecutionContext() {
  ExecutionContext exe_ctx;
  TargetSP target_sp(GetSelectedTarget());
  exe_ctx.SetTargetSP(target_sp);

  if (target_sp) {
    ProcessSP process_sp(target_sp->GetProcessSP());
    exe_ctx.SetProcessSP(process_sp);
    if (process_sp && !process_sp->IsRunning()) {
      ThreadSP thread_sp(process_sp->GetThreadList().GetSelectedThread());
      if (thread_sp) {
        exe_ctx.SetThreadSP(thread_sp);
        exe_ctx.SetFrameSP(thread_sp->GetSelectedFrame());
        if (exe_ctx.GetFramePtr() == nullptr)
          exe_ctx.SetFrameSP(thread_sp->GetStackFrameAtIndex(0));
      }
    }
  }
  return exe_ctx;
}

void Debugger::DispatchInputInterrupt() {
  std::lock_guard<std::recursive_mutex> guard(m_input_reader_stack.GetMutex());
  IOHandlerSP reader_sp(m_input_reader_stack.Top());
  if (reader_sp)
    reader_sp->Interrupt();
}

void Debugger::DispatchInputEndOfFile() {
  std::lock_guard<std::recursive_mutex> guard(m_input_reader_stack.GetMutex());
  IOHandlerSP reader_sp(m_input_reader_stack.Top());
  if (reader_sp)
    reader_sp->GotEOF();
}

void Debugger::ClearIOHandlers() {
  // The bottom input reader should be the main debugger input reader.  We do
  // not want to close that one here.
  std::lock_guard<std::recursive_mutex> guard(m_input_reader_stack.GetMutex());
  while (m_input_reader_stack.GetSize() > 1) {
    IOHandlerSP reader_sp(m_input_reader_stack.Top());
    if (reader_sp)
      PopIOHandler(reader_sp);
  }
}

void Debugger::ExecuteIOHandlers() {
  while (true) {
    IOHandlerSP reader_sp(m_input_reader_stack.Top());
    if (!reader_sp)
      break;

    reader_sp->Run();

    // Remove all input readers that are done from the top of the stack
    while (true) {
      IOHandlerSP top_reader_sp = m_input_reader_stack.Top();
      if (top_reader_sp && top_reader_sp->GetIsDone())
        PopIOHandler(top_reader_sp);
      else
        break;
    }
  }
  ClearIOHandlers();
}

bool Debugger::IsTopIOHandler(const lldb::IOHandlerSP &reader_sp) {
  return m_input_reader_stack.IsTop(reader_sp);
}

bool Debugger::CheckTopIOHandlerTypes(IOHandler::Type top_type,
                                      IOHandler::Type second_top_type) {
  return m_input_reader_stack.CheckTopIOHandlerTypes(top_type, second_top_type);
}

void Debugger::PrintAsync(const char *s, size_t len, bool is_stdout) {
  lldb::StreamFileSP stream = is_stdout ? GetOutputFile() : GetErrorFile();
  m_input_reader_stack.PrintAsync(stream.get(), s, len);
}

ConstString Debugger::GetTopIOHandlerControlSequence(char ch) {
  return m_input_reader_stack.GetTopIOHandlerControlSequence(ch);
}

const char *Debugger::GetIOHandlerCommandPrefix() {
  return m_input_reader_stack.GetTopIOHandlerCommandPrefix();
}

const char *Debugger::GetIOHandlerHelpPrologue() {
  return m_input_reader_stack.GetTopIOHandlerHelpPrologue();
}

void Debugger::RunIOHandler(const IOHandlerSP &reader_sp) {
  PushIOHandler(reader_sp);

  IOHandlerSP top_reader_sp = reader_sp;
  while (top_reader_sp) {
    top_reader_sp->Run();

    if (top_reader_sp.get() == reader_sp.get()) {
      if (PopIOHandler(reader_sp))
        break;
    }

    while (true) {
      top_reader_sp = m_input_reader_stack.Top();
      if (top_reader_sp && top_reader_sp->GetIsDone())
        PopIOHandler(top_reader_sp);
      else
        break;
    }
  }
}

void Debugger::AdoptTopIOHandlerFilesIfInvalid(StreamFileSP &in,
                                               StreamFileSP &out,
                                               StreamFileSP &err) {
  // Before an IOHandler runs, it must have in/out/err streams. This function
  // is called when one ore more of the streams are nullptr. We use the top
  // input reader's in/out/err streams, or fall back to the debugger file
  // handles, or we fall back onto stdin/stdout/stderr as a last resort.

  std::lock_guard<std::recursive_mutex> guard(m_input_reader_stack.GetMutex());
  IOHandlerSP top_reader_sp(m_input_reader_stack.Top());
  // If no STDIN has been set, then set it appropriately
  if (!in) {
    if (top_reader_sp)
      in = top_reader_sp->GetInputStreamFile();
    else
      in = GetInputFile();

    // If there is nothing, use stdin
    if (!in)
      in = std::make_shared<StreamFile>(stdin, false);
  }
  // If no STDOUT has been set, then set it appropriately
  if (!out) {
    if (top_reader_sp)
      out = top_reader_sp->GetOutputStreamFile();
    else
      out = GetOutputFile();

    // If there is nothing, use stdout
    if (!out)
      out = std::make_shared<StreamFile>(stdout, false);
  }
  // If no STDERR has been set, then set it appropriately
  if (!err) {
    if (top_reader_sp)
      err = top_reader_sp->GetErrorStreamFile();
    else
      err = GetErrorFile();

    // If there is nothing, use stderr
    if (!err)
      err = std::make_shared<StreamFile>(stdout, false);
  }
}

void Debugger::PushIOHandler(const IOHandlerSP &reader_sp,
                             bool cancel_top_handler) {
  if (!reader_sp)
    return;

  std::lock_guard<std::recursive_mutex> guard(m_input_reader_stack.GetMutex());

  // Get the current top input reader...
  IOHandlerSP top_reader_sp(m_input_reader_stack.Top());

  // Don't push the same IO handler twice...
  if (reader_sp == top_reader_sp)
    return;

  // Push our new input reader
  m_input_reader_stack.Push(reader_sp);
  reader_sp->Activate();

  // Interrupt the top input reader to it will exit its Run() function and let
  // this new input reader take over
  if (top_reader_sp) {
    top_reader_sp->Deactivate();
    if (cancel_top_handler)
      top_reader_sp->Cancel();
  }
}

bool Debugger::PopIOHandler(const IOHandlerSP &pop_reader_sp) {
  if (!pop_reader_sp)
    return false;

  std::lock_guard<std::recursive_mutex> guard(m_input_reader_stack.GetMutex());

  // The reader on the stop of the stack is done, so let the next read on the
  // stack refresh its prompt and if there is one...
  if (m_input_reader_stack.IsEmpty())
    return false;

  IOHandlerSP reader_sp(m_input_reader_stack.Top());

  if (pop_reader_sp != reader_sp)
    return false;

  reader_sp->Deactivate();
  reader_sp->Cancel();
  m_input_reader_stack.Pop();

  reader_sp = m_input_reader_stack.Top();
  if (reader_sp)
    reader_sp->Activate();

  return true;
}

StreamSP Debugger::GetAsyncOutputStream() {
  return std::make_shared<StreamAsynchronousIO>(*this, true);
}

StreamSP Debugger::GetAsyncErrorStream() {
  return std::make_shared<StreamAsynchronousIO>(*this, false);
}

size_t Debugger::GetNumDebuggers() {
  if (g_debugger_list_ptr && g_debugger_list_mutex_ptr) {
    std::lock_guard<std::recursive_mutex> guard(*g_debugger_list_mutex_ptr);
    return g_debugger_list_ptr->size();
  }
  return 0;
}

lldb::DebuggerSP Debugger::GetDebuggerAtIndex(size_t index) {
  DebuggerSP debugger_sp;

  if (g_debugger_list_ptr && g_debugger_list_mutex_ptr) {
    std::lock_guard<std::recursive_mutex> guard(*g_debugger_list_mutex_ptr);
    if (index < g_debugger_list_ptr->size())
      debugger_sp = g_debugger_list_ptr->at(index);
  }

  return debugger_sp;
}

DebuggerSP Debugger::FindDebuggerWithID(lldb::user_id_t id) {
  DebuggerSP debugger_sp;

  if (g_debugger_list_ptr && g_debugger_list_mutex_ptr) {
    std::lock_guard<std::recursive_mutex> guard(*g_debugger_list_mutex_ptr);
    DebuggerList::iterator pos, end = g_debugger_list_ptr->end();
    for (pos = g_debugger_list_ptr->begin(); pos != end; ++pos) {
      if ((*pos)->GetID() == id) {
        debugger_sp = *pos;
        break;
      }
    }
  }
  return debugger_sp;
}

bool Debugger::FormatDisassemblerAddress(const FormatEntity::Entry *format,
                                         const SymbolContext *sc,
                                         const SymbolContext *prev_sc,
                                         const ExecutionContext *exe_ctx,
                                         const Address *addr, Stream &s) {
  FormatEntity::Entry format_entry;

  if (format == nullptr) {
    if (exe_ctx != nullptr && exe_ctx->HasTargetScope())
      format = exe_ctx->GetTargetRef().GetDebugger().GetDisassemblyFormat();
    if (format == nullptr) {
      FormatEntity::Parse("${addr}: ", format_entry);
      format = &format_entry;
    }
  }
  bool function_changed = false;
  bool initial_function = false;
  if (prev_sc && (prev_sc->function || prev_sc->symbol)) {
    if (sc && (sc->function || sc->symbol)) {
      if (prev_sc->symbol && sc->symbol) {
        if (!sc->symbol->Compare(prev_sc->symbol->GetName(),
                                 prev_sc->symbol->GetType())) {
          function_changed = true;
        }
      } else if (prev_sc->function && sc->function) {
        if (prev_sc->function->GetMangled() != sc->function->GetMangled()) {
          function_changed = true;
        }
      }
    }
  }
  // The first context on a list of instructions will have a prev_sc that has
  // no Function or Symbol -- if SymbolContext had an IsValid() method, it
  // would return false.  But we do get a prev_sc pointer.
  if ((sc && (sc->function || sc->symbol)) && prev_sc &&
      (prev_sc->function == nullptr && prev_sc->symbol == nullptr)) {
    initial_function = true;
  }
  return FormatEntity::Format(*format, s, sc, exe_ctx, addr, nullptr,
                              function_changed, initial_function);
}

void Debugger::SetLoggingCallback(lldb::LogOutputCallback log_callback,
                                  void *baton) {
  // For simplicity's sake, I am not going to deal with how to close down any
  // open logging streams, I just redirect everything from here on out to the
  // callback.
  m_log_callback_stream_sp =
      std::make_shared<StreamCallback>(log_callback, baton);
}

bool Debugger::EnableLog(llvm::StringRef channel,
                         llvm::ArrayRef<const char *> categories,
                         llvm::StringRef log_file, uint32_t log_options,
                         llvm::raw_ostream &error_stream) {
  const bool should_close = true;
  const bool unbuffered = true;

  std::shared_ptr<llvm::raw_ostream> log_stream_sp;
  if (m_log_callback_stream_sp) {
    log_stream_sp = m_log_callback_stream_sp;
    // For now when using the callback mode you always get thread & timestamp.
    log_options |=
        LLDB_LOG_OPTION_PREPEND_TIMESTAMP | LLDB_LOG_OPTION_PREPEND_THREAD_NAME;
  } else if (log_file.empty()) {
    log_stream_sp = std::make_shared<llvm::raw_fd_ostream>(
        GetOutputFile()->GetFile().GetDescriptor(), !should_close, unbuffered);
  } else {
    auto pos = m_log_streams.find(log_file);
    if (pos != m_log_streams.end())
      log_stream_sp = pos->second.lock();
    if (!log_stream_sp) {
      llvm::sys::fs::OpenFlags flags = llvm::sys::fs::F_Text;
      if (log_options & LLDB_LOG_OPTION_APPEND)
        flags |= llvm::sys::fs::F_Append;
      int FD;
      if (std::error_code ec = llvm::sys::fs::openFileForWrite(
              log_file, FD, llvm::sys::fs::CD_CreateAlways, flags)) {
        error_stream << "Unable to open log file: " << ec.message();
        return false;
      }
      log_stream_sp =
          std::make_shared<llvm::raw_fd_ostream>(FD, should_close, unbuffered);
      m_log_streams[log_file] = log_stream_sp;
    }
  }
  assert(log_stream_sp);

  if (log_options == 0)
    log_options =
        LLDB_LOG_OPTION_PREPEND_THREAD_NAME | LLDB_LOG_OPTION_THREADSAFE;

  return Log::EnableLogChannel(log_stream_sp, log_options, channel, categories,
                               error_stream);
}

SourceManager &Debugger::GetSourceManager() {
  if (!m_source_manager_ap)
    m_source_manager_ap = llvm::make_unique<SourceManager>(shared_from_this());
  return *m_source_manager_ap;
}

// This function handles events that were broadcast by the process.
void Debugger::HandleBreakpointEvent(const EventSP &event_sp) {
  using namespace lldb;
  const uint32_t event_type =
      Breakpoint::BreakpointEventData::GetBreakpointEventTypeFromEvent(
          event_sp);

  //    if (event_type & eBreakpointEventTypeAdded
  //        || event_type & eBreakpointEventTypeRemoved
  //        || event_type & eBreakpointEventTypeEnabled
  //        || event_type & eBreakpointEventTypeDisabled
  //        || event_type & eBreakpointEventTypeCommandChanged
  //        || event_type & eBreakpointEventTypeConditionChanged
  //        || event_type & eBreakpointEventTypeIgnoreChanged
  //        || event_type & eBreakpointEventTypeLocationsResolved)
  //    {
  //        // Don't do anything about these events, since the breakpoint
  //        commands already echo these actions.
  //    }
  //
  if (event_type & eBreakpointEventTypeLocationsAdded) {
    uint32_t num_new_locations =
        Breakpoint::BreakpointEventData::GetNumBreakpointLocationsFromEvent(
            event_sp);
    if (num_new_locations > 0) {
      BreakpointSP breakpoint =
          Breakpoint::BreakpointEventData::GetBreakpointFromEvent(event_sp);
      StreamSP output_sp(GetAsyncOutputStream());
      if (output_sp) {
        output_sp->Printf("%d location%s added to breakpoint %d\n",
                          num_new_locations, num_new_locations == 1 ? "" : "s",
                          breakpoint->GetID());
        output_sp->Flush();
      }
    }
  }
  //    else if (event_type & eBreakpointEventTypeLocationsRemoved)
  //    {
  //        // These locations just get disabled, not sure it is worth spamming
  //        folks about this on the command line.
  //    }
  //    else if (event_type & eBreakpointEventTypeLocationsResolved)
  //    {
  //        // This might be an interesting thing to note, but I'm going to
  //        leave it quiet for now, it just looked noisy.
  //    }
}

size_t Debugger::GetProcessSTDOUT(Process *process, Stream *stream) {
  size_t total_bytes = 0;
  if (stream == nullptr)
    stream = GetOutputFile().get();

  if (stream) {
    //  The process has stuff waiting for stdout; get it and write it out to the
    //  appropriate place.
    if (process == nullptr) {
      TargetSP target_sp = GetTargetList().GetSelectedTarget();
      if (target_sp)
        process = target_sp->GetProcessSP().get();
    }
    if (process) {
      Status error;
      size_t len;
      char stdio_buffer[1024];
      while ((len = process->GetSTDOUT(stdio_buffer, sizeof(stdio_buffer),
                                       error)) > 0) {
        stream->Write(stdio_buffer, len);
        total_bytes += len;
      }
    }
    stream->Flush();
  }
  return total_bytes;
}

size_t Debugger::GetProcessSTDERR(Process *process, Stream *stream) {
  size_t total_bytes = 0;
  if (stream == nullptr)
    stream = GetOutputFile().get();

  if (stream) {
    //  The process has stuff waiting for stderr; get it and write it out to the
    //  appropriate place.
    if (process == nullptr) {
      TargetSP target_sp = GetTargetList().GetSelectedTarget();
      if (target_sp)
        process = target_sp->GetProcessSP().get();
    }
    if (process) {
      Status error;
      size_t len;
      char stdio_buffer[1024];
      while ((len = process->GetSTDERR(stdio_buffer, sizeof(stdio_buffer),
                                       error)) > 0) {
        stream->Write(stdio_buffer, len);
        total_bytes += len;
      }
    }
    stream->Flush();
  }
  return total_bytes;
}

// This function handles events that were broadcast by the process.
void Debugger::HandleProcessEvent(const EventSP &event_sp) {
  using namespace lldb;
  const uint32_t event_type = event_sp->GetType();
  ProcessSP process_sp =
      (event_type == Process::eBroadcastBitStructuredData)
          ? EventDataStructuredData::GetProcessFromEvent(event_sp.get())
          : Process::ProcessEventData::GetProcessFromEvent(event_sp.get());

  StreamSP output_stream_sp = GetAsyncOutputStream();
  StreamSP error_stream_sp = GetAsyncErrorStream();
  const bool gui_enabled = IsForwardingEvents();

  if (!gui_enabled) {
    bool pop_process_io_handler = false;
    assert(process_sp);

    bool state_is_stopped = false;
    const bool got_state_changed =
        (event_type & Process::eBroadcastBitStateChanged) != 0;
    const bool got_stdout = (event_type & Process::eBroadcastBitSTDOUT) != 0;
    const bool got_stderr = (event_type & Process::eBroadcastBitSTDERR) != 0;
    const bool got_structured_data =
        (event_type & Process::eBroadcastBitStructuredData) != 0;

    if (got_state_changed) {
      StateType event_state =
          Process::ProcessEventData::GetStateFromEvent(event_sp.get());
      state_is_stopped = StateIsStoppedState(event_state, false);
    }

    // Display running state changes first before any STDIO
    if (got_state_changed && !state_is_stopped) {
      Process::HandleProcessStateChangedEvent(event_sp, output_stream_sp.get(),
                                              pop_process_io_handler);
    }

    // Now display and STDOUT
    if (got_stdout || got_state_changed) {
      GetProcessSTDOUT(process_sp.get(), output_stream_sp.get());
    }

    // Now display and STDERR
    if (got_stderr || got_state_changed) {
      GetProcessSTDERR(process_sp.get(), error_stream_sp.get());
    }

    // Give structured data events an opportunity to display.
    if (got_structured_data) {
      StructuredDataPluginSP plugin_sp =
          EventDataStructuredData::GetPluginFromEvent(event_sp.get());
      if (plugin_sp) {
        auto structured_data_sp =
            EventDataStructuredData::GetObjectFromEvent(event_sp.get());
        if (output_stream_sp) {
          StreamString content_stream;
          Status error =
              plugin_sp->GetDescription(structured_data_sp, content_stream);
          if (error.Success()) {
            if (!content_stream.GetString().empty()) {
              // Add newline.
              content_stream.PutChar('\n');
              content_stream.Flush();

              // Print it.
              output_stream_sp->PutCString(content_stream.GetString());
            }
          } else {
            error_stream_sp->Printf("Failed to print structured "
                                    "data with plugin %s: %s",
                                    plugin_sp->GetPluginName().AsCString(),
                                    error.AsCString());
          }
        }
      }
    }

    // Now display any stopped state changes after any STDIO
    if (got_state_changed && state_is_stopped) {
      Process::HandleProcessStateChangedEvent(event_sp, output_stream_sp.get(),
                                              pop_process_io_handler);
    }

    output_stream_sp->Flush();
    error_stream_sp->Flush();

    if (pop_process_io_handler)
      process_sp->PopProcessIOHandler();
  }
}

void Debugger::HandleThreadEvent(const EventSP &event_sp) {
  // At present the only thread event we handle is the Frame Changed event, and
  // all we do for that is just reprint the thread status for that thread.
  using namespace lldb;
  const uint32_t event_type = event_sp->GetType();
  const bool stop_format = true;
  if (event_type == Thread::eBroadcastBitStackChanged ||
      event_type == Thread::eBroadcastBitThreadSelected) {
    ThreadSP thread_sp(
        Thread::ThreadEventData::GetThreadFromEvent(event_sp.get()));
    if (thread_sp) {
      thread_sp->GetStatus(*GetAsyncOutputStream(), 0, 1, 1, stop_format);
    }
  }
}

bool Debugger::IsForwardingEvents() { return (bool)m_forward_listener_sp; }

void Debugger::EnableForwardEvents(const ListenerSP &listener_sp) {
  m_forward_listener_sp = listener_sp;
}

void Debugger::CancelForwardEvents(const ListenerSP &listener_sp) {
  m_forward_listener_sp.reset();
}

void Debugger::DefaultEventHandler() {
  ListenerSP listener_sp(GetListener());
  ConstString broadcaster_class_target(Target::GetStaticBroadcasterClass());
  ConstString broadcaster_class_process(Process::GetStaticBroadcasterClass());
  ConstString broadcaster_class_thread(Thread::GetStaticBroadcasterClass());
  BroadcastEventSpec target_event_spec(broadcaster_class_target,
                                       Target::eBroadcastBitBreakpointChanged);

  BroadcastEventSpec process_event_spec(
      broadcaster_class_process,
      Process::eBroadcastBitStateChanged | Process::eBroadcastBitSTDOUT |
          Process::eBroadcastBitSTDERR | Process::eBroadcastBitStructuredData);

  BroadcastEventSpec thread_event_spec(broadcaster_class_thread,
                                       Thread::eBroadcastBitStackChanged |
                                           Thread::eBroadcastBitThreadSelected);

  listener_sp->StartListeningForEventSpec(m_broadcaster_manager_sp,
                                          target_event_spec);
  listener_sp->StartListeningForEventSpec(m_broadcaster_manager_sp,
                                          process_event_spec);
  listener_sp->StartListeningForEventSpec(m_broadcaster_manager_sp,
                                          thread_event_spec);
  listener_sp->StartListeningForEvents(
      m_command_interpreter_ap.get(),
      CommandInterpreter::eBroadcastBitQuitCommandReceived |
          CommandInterpreter::eBroadcastBitAsynchronousOutputData |
          CommandInterpreter::eBroadcastBitAsynchronousErrorData);

  // Let the thread that spawned us know that we have started up and that we
  // are now listening to all required events so no events get missed
  m_sync_broadcaster.BroadcastEvent(eBroadcastBitEventThreadIsListening);

  bool done = false;
  while (!done) {
    EventSP event_sp;
    if (listener_sp->GetEvent(event_sp, llvm::None)) {
      if (event_sp) {
        Broadcaster *broadcaster = event_sp->GetBroadcaster();
        if (broadcaster) {
          uint32_t event_type = event_sp->GetType();
          ConstString broadcaster_class(broadcaster->GetBroadcasterClass());
          if (broadcaster_class == broadcaster_class_process) {
            HandleProcessEvent(event_sp);
          } else if (broadcaster_class == broadcaster_class_target) {
            if (Breakpoint::BreakpointEventData::GetEventDataFromEvent(
                    event_sp.get())) {
              HandleBreakpointEvent(event_sp);
            }
          } else if (broadcaster_class == broadcaster_class_thread) {
            HandleThreadEvent(event_sp);
          } else if (broadcaster == m_command_interpreter_ap.get()) {
            if (event_type &
                CommandInterpreter::eBroadcastBitQuitCommandReceived) {
              done = true;
            } else if (event_type &
                       CommandInterpreter::eBroadcastBitAsynchronousErrorData) {
              const char *data = reinterpret_cast<const char *>(
                  EventDataBytes::GetBytesFromEvent(event_sp.get()));
              if (data && data[0]) {
                StreamSP error_sp(GetAsyncErrorStream());
                if (error_sp) {
                  error_sp->PutCString(data);
                  error_sp->Flush();
                }
              }
            } else if (event_type & CommandInterpreter::
                                        eBroadcastBitAsynchronousOutputData) {
              const char *data = reinterpret_cast<const char *>(
                  EventDataBytes::GetBytesFromEvent(event_sp.get()));
              if (data && data[0]) {
                StreamSP output_sp(GetAsyncOutputStream());
                if (output_sp) {
                  output_sp->PutCString(data);
                  output_sp->Flush();
                }
              }
            }
          }
        }

        if (m_forward_listener_sp)
          m_forward_listener_sp->AddEvent(event_sp);
      }
    }
  }
}

lldb::thread_result_t Debugger::EventHandlerThread(lldb::thread_arg_t arg) {
  ((Debugger *)arg)->DefaultEventHandler();
  return NULL;
}

bool Debugger::StartEventHandlerThread() {
  if (!m_event_handler_thread.IsJoinable()) {
    // We must synchronize with the DefaultEventHandler() thread to ensure it
    // is up and running and listening to events before we return from this
    // function. We do this by listening to events for the
    // eBroadcastBitEventThreadIsListening from the m_sync_broadcaster
    ConstString full_name("lldb.debugger.event-handler");
    ListenerSP listener_sp(Listener::MakeListener(full_name.AsCString()));
    listener_sp->StartListeningForEvents(&m_sync_broadcaster,
                                         eBroadcastBitEventThreadIsListening);

    auto thread_name =
        full_name.GetLength() < llvm::get_max_thread_name_length() ?
        full_name.AsCString() : "dbg.evt-handler";

    // Use larger 8MB stack for this thread
    m_event_handler_thread = ThreadLauncher::LaunchThread(thread_name,
        EventHandlerThread, this, nullptr, g_debugger_event_thread_stack_bytes);

    // Make sure DefaultEventHandler() is running and listening to events
    // before we return from this function. We are only listening for events of
    // type eBroadcastBitEventThreadIsListening so we don't need to check the
    // event, we just need to wait an infinite amount of time for it (nullptr
    // timeout as the first parameter)
    lldb::EventSP event_sp;
    listener_sp->GetEvent(event_sp, llvm::None);
  }
  return m_event_handler_thread.IsJoinable();
}

void Debugger::StopEventHandlerThread() {
  if (m_event_handler_thread.IsJoinable()) {
    GetCommandInterpreter().BroadcastEvent(
        CommandInterpreter::eBroadcastBitQuitCommandReceived);
    m_event_handler_thread.Join(nullptr);
  }
}

lldb::thread_result_t Debugger::IOHandlerThread(lldb::thread_arg_t arg) {
  Debugger *debugger = (Debugger *)arg;
  debugger->ExecuteIOHandlers();
  debugger->StopEventHandlerThread();
  return NULL;
}

bool Debugger::HasIOHandlerThread() { return m_io_handler_thread.IsJoinable(); }

bool Debugger::StartIOHandlerThread() {
  if (!m_io_handler_thread.IsJoinable())
    m_io_handler_thread = ThreadLauncher::LaunchThread(
        "lldb.debugger.io-handler", IOHandlerThread, this, nullptr,
        8 * 1024 * 1024); // Use larger 8MB stack for this thread
  return m_io_handler_thread.IsJoinable();
}

void Debugger::StopIOHandlerThread() {
  if (m_io_handler_thread.IsJoinable()) {
    if (m_input_file_sp)
      m_input_file_sp->GetFile().Close();
    m_io_handler_thread.Join(nullptr);
  }
}

void Debugger::JoinIOHandlerThread() {
  if (HasIOHandlerThread()) {
    thread_result_t result;
    m_io_handler_thread.Join(&result);
    m_io_handler_thread = LLDB_INVALID_HOST_THREAD;
  }
}

Target *Debugger::GetDummyTarget() {
  return m_target_list.GetDummyTarget(*this).get();
}

Target *Debugger::GetSelectedOrDummyTarget(bool prefer_dummy) {
  Target *target = nullptr;
  if (!prefer_dummy) {
    target = m_target_list.GetSelectedTarget().get();
    if (target)
      return target;
  }

  return GetDummyTarget();
}

Status Debugger::RunREPL(LanguageType language, const char *repl_options) {
  Status err;
  FileSpec repl_executable;

  if (language == eLanguageTypeUnknown) {
    std::set<LanguageType> repl_languages;

    Language::GetLanguagesSupportingREPLs(repl_languages);

    if (repl_languages.size() == 1) {
      language = *repl_languages.begin();
    } else if (repl_languages.empty()) {
      err.SetErrorStringWithFormat(
          "LLDB isn't configured with REPL support for any languages.");
      return err;
    } else {
      err.SetErrorStringWithFormat(
          "Multiple possible REPL languages.  Please specify a language.");
      return err;
    }
  }

  Target *const target =
      nullptr; // passing in an empty target means the REPL must create one

  REPLSP repl_sp(REPL::Create(err, language, this, target, repl_options));

  if (!err.Success()) {
    return err;
  }

  if (!repl_sp) {
    err.SetErrorStringWithFormat("couldn't find a REPL for %s",
                                 Language::GetNameForLanguageType(language));
    return err;
  }

  repl_sp->SetCompilerOptions(repl_options);
  repl_sp->RunLoop();

  return err;
}
