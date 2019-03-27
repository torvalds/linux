//===-- Debugger.h ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Debugger_h_
#define liblldb_Debugger_h_

#include <stdint.h>

#include <memory>
#include <vector>

#include "lldb/Core/FormatEntity.h"
#include "lldb/Core/IOHandler.h"
#include "lldb/Core/SourceManager.h"
#include "lldb/Core/UserSettingsController.h"
#include "lldb/Host/HostThread.h"
#include "lldb/Host/Terminal.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Platform.h"
#include "lldb/Target/TargetList.h"
#include "lldb/Utility/Broadcaster.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/UserID.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-private-enumerations.h"
#include "lldb/lldb-private-types.h"
#include "lldb/lldb-types.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/Threading.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>

namespace lldb_private {
class Address;
}
namespace lldb_private {
class CommandInterpreter;
}
namespace lldb_private {
class Process;
}
namespace lldb_private {
class Stream;
}
namespace lldb_private {
class SymbolContext;
}
namespace lldb_private {
class Target;
}
namespace llvm {
class raw_ostream;
}

namespace lldb_private {

//----------------------------------------------------------------------
/// @class Debugger Debugger.h "lldb/Core/Debugger.h"
/// A class to manage flag bits.
///
/// Provides a global root objects for the debugger core.
//----------------------------------------------------------------------

class Debugger : public std::enable_shared_from_this<Debugger>,
                 public UserID,
                 public Properties {
  friend class SourceManager; // For GetSourceFileCache.

public:
  ~Debugger() override;

  static lldb::DebuggerSP
  CreateInstance(lldb::LogOutputCallback log_callback = nullptr,
                 void *baton = nullptr);

  static lldb::TargetSP FindTargetWithProcessID(lldb::pid_t pid);

  static lldb::TargetSP FindTargetWithProcess(Process *process);

  static void Initialize(LoadPluginCallbackType load_plugin_callback);

  static void Terminate();

  static void SettingsInitialize();

  static void SettingsTerminate();

  static void Destroy(lldb::DebuggerSP &debugger_sp);

  static lldb::DebuggerSP FindDebuggerWithID(lldb::user_id_t id);

  static lldb::DebuggerSP
  FindDebuggerWithInstanceName(const ConstString &instance_name);

  static size_t GetNumDebuggers();

  static lldb::DebuggerSP GetDebuggerAtIndex(size_t index);

  static bool FormatDisassemblerAddress(const FormatEntity::Entry *format,
                                        const SymbolContext *sc,
                                        const SymbolContext *prev_sc,
                                        const ExecutionContext *exe_ctx,
                                        const Address *addr, Stream &s);

  void Clear();

  bool GetAsyncExecution();

  void SetAsyncExecution(bool async);

  lldb::StreamFileSP GetInputFile() { return m_input_file_sp; }

  lldb::StreamFileSP GetOutputFile() { return m_output_file_sp; }

  lldb::StreamFileSP GetErrorFile() { return m_error_file_sp; }

  void SetInputFileHandle(FILE *fh, bool tranfer_ownership);

  void SetOutputFileHandle(FILE *fh, bool tranfer_ownership);

  void SetErrorFileHandle(FILE *fh, bool tranfer_ownership);

  void SaveInputTerminalState();

  void RestoreInputTerminalState();

  lldb::StreamSP GetAsyncOutputStream();

  lldb::StreamSP GetAsyncErrorStream();

  CommandInterpreter &GetCommandInterpreter() {
    assert(m_command_interpreter_ap.get());
    return *m_command_interpreter_ap;
  }

  lldb::ListenerSP GetListener() { return m_listener_sp; }

  // This returns the Debugger's scratch source manager.  It won't be able to
  // look up files in debug information, but it can look up files by absolute
  // path and display them to you. To get the target's source manager, call
  // GetSourceManager on the target instead.
  SourceManager &GetSourceManager();

  lldb::TargetSP GetSelectedTarget() {
    return m_target_list.GetSelectedTarget();
  }

  ExecutionContext GetSelectedExecutionContext();
  //------------------------------------------------------------------
  /// Get accessor for the target list.
  ///
  /// The target list is part of the global debugger object. This the single
  /// debugger shared instance to control where targets get created and to
  /// allow for tracking and searching for targets based on certain criteria.
  ///
  /// @return
  ///     A global shared target list.
  //------------------------------------------------------------------
  TargetList &GetTargetList() { return m_target_list; }

  PlatformList &GetPlatformList() { return m_platform_list; }

  void DispatchInputInterrupt();

  void DispatchInputEndOfFile();

  //------------------------------------------------------------------
  // If any of the streams are not set, set them to the in/out/err stream of
  // the top most input reader to ensure they at least have something
  //------------------------------------------------------------------
  void AdoptTopIOHandlerFilesIfInvalid(lldb::StreamFileSP &in,
                                       lldb::StreamFileSP &out,
                                       lldb::StreamFileSP &err);

  void PushIOHandler(const lldb::IOHandlerSP &reader_sp,
                     bool cancel_top_handler = true);

  bool PopIOHandler(const lldb::IOHandlerSP &reader_sp);

  // Synchronously run an input reader until it is done
  void RunIOHandler(const lldb::IOHandlerSP &reader_sp);

  bool IsTopIOHandler(const lldb::IOHandlerSP &reader_sp);

  bool CheckTopIOHandlerTypes(IOHandler::Type top_type,
                              IOHandler::Type second_top_type);

  void PrintAsync(const char *s, size_t len, bool is_stdout);

  ConstString GetTopIOHandlerControlSequence(char ch);

  const char *GetIOHandlerCommandPrefix();

  const char *GetIOHandlerHelpPrologue();

  void ClearIOHandlers();

  bool GetCloseInputOnEOF() const;

  void SetCloseInputOnEOF(bool b);

  bool EnableLog(llvm::StringRef channel,
                 llvm::ArrayRef<const char *> categories,
                 llvm::StringRef log_file, uint32_t log_options,
                 llvm::raw_ostream &error_stream);

  void SetLoggingCallback(lldb::LogOutputCallback log_callback, void *baton);

  //----------------------------------------------------------------------
  // Properties Functions
  //----------------------------------------------------------------------
  enum StopDisassemblyType {
    eStopDisassemblyTypeNever = 0,
    eStopDisassemblyTypeNoDebugInfo,
    eStopDisassemblyTypeNoSource,
    eStopDisassemblyTypeAlways
  };

  Status SetPropertyValue(const ExecutionContext *exe_ctx,
                          VarSetOperationType op, llvm::StringRef property_path,
                          llvm::StringRef value) override;

  bool GetAutoConfirm() const;

  const FormatEntity::Entry *GetDisassemblyFormat() const;

  const FormatEntity::Entry *GetFrameFormat() const;

  const FormatEntity::Entry *GetFrameFormatUnique() const;

  const FormatEntity::Entry *GetThreadFormat() const;

  const FormatEntity::Entry *GetThreadStopFormat() const;

  lldb::ScriptLanguage GetScriptLanguage() const;

  bool SetScriptLanguage(lldb::ScriptLanguage script_lang);

  uint32_t GetTerminalWidth() const;

  bool SetTerminalWidth(uint32_t term_width);

  llvm::StringRef GetPrompt() const;

  void SetPrompt(llvm::StringRef p);
  void SetPrompt(const char *) = delete;

  llvm::StringRef GetReproducerPath() const;

  bool GetUseExternalEditor() const;

  bool SetUseExternalEditor(bool use_external_editor_p);

  bool GetUseColor() const;

  bool SetUseColor(bool use_color);

  bool GetHighlightSource() const;

  lldb::StopShowColumn GetStopShowColumn() const;

  llvm::StringRef GetStopShowColumnAnsiPrefix() const;

  llvm::StringRef GetStopShowColumnAnsiSuffix() const;

  uint32_t GetStopSourceLineCount(bool before) const;

  StopDisassemblyType GetStopDisassemblyDisplay() const;

  uint32_t GetDisassemblyLineCount() const;

  bool GetAutoOneLineSummaries() const;

  bool GetAutoIndent() const;

  bool SetAutoIndent(bool b);

  bool GetPrintDecls() const;

  bool SetPrintDecls(bool b);

  uint32_t GetTabSize() const;

  bool SetTabSize(uint32_t tab_size);

  bool GetEscapeNonPrintables() const;

  bool GetNotifyVoid() const;

  const ConstString &GetInstanceName() { return m_instance_name; }

  bool LoadPlugin(const FileSpec &spec, Status &error);

  void ExecuteIOHandlers();

  bool IsForwardingEvents();

  void EnableForwardEvents(const lldb::ListenerSP &listener_sp);

  void CancelForwardEvents(const lldb::ListenerSP &listener_sp);

  bool IsHandlingEvents() const { return m_event_handler_thread.IsJoinable(); }

  Status RunREPL(lldb::LanguageType language, const char *repl_options);

  // This is for use in the command interpreter, when you either want the
  // selected target, or if no target is present you want to prime the dummy
  // target with entities that will be copied over to new targets.
  Target *GetSelectedOrDummyTarget(bool prefer_dummy = false);
  Target *GetDummyTarget();

  lldb::BroadcasterManagerSP GetBroadcasterManager() {
    return m_broadcaster_manager_sp;
  }

protected:
  friend class CommandInterpreter;
  friend class REPL;

  bool StartEventHandlerThread();

  void StopEventHandlerThread();

  static lldb::thread_result_t EventHandlerThread(lldb::thread_arg_t arg);

  bool HasIOHandlerThread();

  bool StartIOHandlerThread();

  void StopIOHandlerThread();

  void JoinIOHandlerThread();

  static lldb::thread_result_t IOHandlerThread(lldb::thread_arg_t arg);

  void DefaultEventHandler();

  void HandleBreakpointEvent(const lldb::EventSP &event_sp);

  void HandleProcessEvent(const lldb::EventSP &event_sp);

  void HandleThreadEvent(const lldb::EventSP &event_sp);

  size_t GetProcessSTDOUT(Process *process, Stream *stream);

  size_t GetProcessSTDERR(Process *process, Stream *stream);

  SourceManager::SourceFileCache &GetSourceFileCache() {
    return m_source_file_cache;
  }

  void InstanceInitialize();

  lldb::StreamFileSP m_input_file_sp;
  lldb::StreamFileSP m_output_file_sp;
  lldb::StreamFileSP m_error_file_sp;

  lldb::BroadcasterManagerSP m_broadcaster_manager_sp; // The debugger acts as a
                                                       // broadcaster manager of
                                                       // last resort.
  // It needs to get constructed before the target_list or any other member
  // that might want to broadcast through the debugger.

  TerminalState m_terminal_state;
  TargetList m_target_list;

  PlatformList m_platform_list;
  lldb::ListenerSP m_listener_sp;
  std::unique_ptr<SourceManager> m_source_manager_ap; // This is a scratch
                                                      // source manager that we
                                                      // return if we have no
                                                      // targets.
  SourceManager::SourceFileCache m_source_file_cache; // All the source managers
                                                      // for targets created in
                                                      // this debugger used this
                                                      // shared
                                                      // source file cache.
  std::unique_ptr<CommandInterpreter> m_command_interpreter_ap;

  IOHandlerStack m_input_reader_stack;
  llvm::StringMap<std::weak_ptr<llvm::raw_ostream>> m_log_streams;
  std::shared_ptr<llvm::raw_ostream> m_log_callback_stream_sp;
  ConstString m_instance_name;
  static LoadPluginCallbackType g_load_plugin_callback;
  typedef std::vector<llvm::sys::DynamicLibrary> LoadedPluginsList;
  LoadedPluginsList m_loaded_plugins;
  HostThread m_event_handler_thread;
  HostThread m_io_handler_thread;
  Broadcaster m_sync_broadcaster;
  lldb::ListenerSP m_forward_listener_sp;
  llvm::once_flag m_clear_once;

  //----------------------------------------------------------------------
  // Events for m_sync_broadcaster
  //----------------------------------------------------------------------
  enum {
    eBroadcastBitEventThreadIsListening = (1 << 0),
  };

private:
  // Use Debugger::CreateInstance() to get a shared pointer to a new debugger
  // object
  Debugger(lldb::LogOutputCallback m_log_callback, void *baton);

  DISALLOW_COPY_AND_ASSIGN(Debugger);
};

} // namespace lldb_private

#endif // liblldb_Debugger_h_
