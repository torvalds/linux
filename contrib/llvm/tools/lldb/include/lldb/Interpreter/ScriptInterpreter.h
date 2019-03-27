//===-- ScriptInterpreter.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ScriptInterpreter_h_
#define liblldb_ScriptInterpreter_h_

#include "lldb/lldb-private.h"

#include "lldb/Breakpoint/BreakpointOptions.h"
#include "lldb/Core/PluginInterface.h"
#include "lldb/Core/SearchFilter.h"
#include "lldb/Utility/Broadcaster.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StructuredData.h"

#include "lldb/Host/PseudoTerminal.h"

namespace lldb_private {

class ScriptInterpreterLocker {
public:
  ScriptInterpreterLocker() = default;

  virtual ~ScriptInterpreterLocker() = default;

private:
  DISALLOW_COPY_AND_ASSIGN(ScriptInterpreterLocker);
};

class ScriptInterpreter : public PluginInterface {
public:
  typedef enum {
    eScriptReturnTypeCharPtr,
    eScriptReturnTypeBool,
    eScriptReturnTypeShortInt,
    eScriptReturnTypeShortIntUnsigned,
    eScriptReturnTypeInt,
    eScriptReturnTypeIntUnsigned,
    eScriptReturnTypeLongInt,
    eScriptReturnTypeLongIntUnsigned,
    eScriptReturnTypeLongLong,
    eScriptReturnTypeLongLongUnsigned,
    eScriptReturnTypeFloat,
    eScriptReturnTypeDouble,
    eScriptReturnTypeChar,
    eScriptReturnTypeCharStrOrNone,
    eScriptReturnTypeOpaqueObject
  } ScriptReturnType;

  ScriptInterpreter(CommandInterpreter &interpreter,
                    lldb::ScriptLanguage script_lang);

  ~ScriptInterpreter() override;

  struct ExecuteScriptOptions {
  public:
    ExecuteScriptOptions()
        : m_enable_io(true), m_set_lldb_globals(true), m_maskout_errors(true) {}

    bool GetEnableIO() const { return m_enable_io; }

    bool GetSetLLDBGlobals() const { return m_set_lldb_globals; }

    bool GetMaskoutErrors() const { return m_maskout_errors; }

    ExecuteScriptOptions &SetEnableIO(bool enable) {
      m_enable_io = enable;
      return *this;
    }

    ExecuteScriptOptions &SetSetLLDBGlobals(bool set) {
      m_set_lldb_globals = set;
      return *this;
    }

    ExecuteScriptOptions &SetMaskoutErrors(bool maskout) {
      m_maskout_errors = maskout;
      return *this;
    }

  private:
    bool m_enable_io;
    bool m_set_lldb_globals;
    bool m_maskout_errors;
  };

  virtual bool Interrupt() { return false; }

  virtual bool ExecuteOneLine(
      llvm::StringRef command, CommandReturnObject *result,
      const ExecuteScriptOptions &options = ExecuteScriptOptions()) = 0;

  virtual void ExecuteInterpreterLoop() = 0;

  virtual bool ExecuteOneLineWithReturn(
      llvm::StringRef in_string, ScriptReturnType return_type, void *ret_value,
      const ExecuteScriptOptions &options = ExecuteScriptOptions()) {
    return true;
  }

  virtual Status ExecuteMultipleLines(
      const char *in_string,
      const ExecuteScriptOptions &options = ExecuteScriptOptions()) {
    Status error;
    error.SetErrorString("not implemented");
    return error;
  }

  virtual Status
  ExportFunctionDefinitionToInterpreter(StringList &function_def) {
    Status error;
    error.SetErrorString("not implemented");
    return error;
  }

  virtual Status GenerateBreakpointCommandCallbackData(StringList &input,
                                                       std::string &output) {
    Status error;
    error.SetErrorString("not implemented");
    return error;
  }

  virtual bool GenerateWatchpointCommandCallbackData(StringList &input,
                                                     std::string &output) {
    return false;
  }

  virtual bool GenerateTypeScriptFunction(const char *oneliner,
                                          std::string &output,
                                          const void *name_token = nullptr) {
    return false;
  }

  virtual bool GenerateTypeScriptFunction(StringList &input,
                                          std::string &output,
                                          const void *name_token = nullptr) {
    return false;
  }

  virtual bool GenerateScriptAliasFunction(StringList &input,
                                           std::string &output) {
    return false;
  }

  virtual bool GenerateTypeSynthClass(StringList &input, std::string &output,
                                      const void *name_token = nullptr) {
    return false;
  }

  virtual bool GenerateTypeSynthClass(const char *oneliner, std::string &output,
                                      const void *name_token = nullptr) {
    return false;
  }

  virtual StructuredData::ObjectSP
  CreateSyntheticScriptedProvider(const char *class_name,
                                  lldb::ValueObjectSP valobj) {
    return StructuredData::ObjectSP();
  }

  virtual StructuredData::GenericSP
  CreateScriptCommandObject(const char *class_name) {
    return StructuredData::GenericSP();
  }

  virtual StructuredData::GenericSP
  CreateFrameRecognizer(const char *class_name) {
    return StructuredData::GenericSP();
  }

  virtual lldb::ValueObjectListSP GetRecognizedArguments(
      const StructuredData::ObjectSP &implementor,
      lldb::StackFrameSP frame_sp) {
    return lldb::ValueObjectListSP();
  }

  virtual StructuredData::GenericSP
  OSPlugin_CreatePluginObject(const char *class_name,
                              lldb::ProcessSP process_sp) {
    return StructuredData::GenericSP();
  }

  virtual StructuredData::DictionarySP
  OSPlugin_RegisterInfo(StructuredData::ObjectSP os_plugin_object_sp) {
    return StructuredData::DictionarySP();
  }

  virtual StructuredData::ArraySP
  OSPlugin_ThreadsInfo(StructuredData::ObjectSP os_plugin_object_sp) {
    return StructuredData::ArraySP();
  }

  virtual StructuredData::StringSP
  OSPlugin_RegisterContextData(StructuredData::ObjectSP os_plugin_object_sp,
                               lldb::tid_t thread_id) {
    return StructuredData::StringSP();
  }

  virtual StructuredData::DictionarySP
  OSPlugin_CreateThread(StructuredData::ObjectSP os_plugin_object_sp,
                        lldb::tid_t tid, lldb::addr_t context) {
    return StructuredData::DictionarySP();
  }

  virtual StructuredData::ObjectSP
  CreateScriptedThreadPlan(const char *class_name,
                           lldb::ThreadPlanSP thread_plan_sp) {
    return StructuredData::ObjectSP();
  }

  virtual bool
  ScriptedThreadPlanExplainsStop(StructuredData::ObjectSP implementor_sp,
                                 Event *event, bool &script_error) {
    script_error = true;
    return true;
  }

  virtual bool
  ScriptedThreadPlanShouldStop(StructuredData::ObjectSP implementor_sp,
                               Event *event, bool &script_error) {
    script_error = true;
    return true;
  }

  virtual bool
  ScriptedThreadPlanIsStale(StructuredData::ObjectSP implementor_sp,
                            bool &script_error) {
    script_error = true;
    return true;
  }

  virtual lldb::StateType
  ScriptedThreadPlanGetRunState(StructuredData::ObjectSP implementor_sp,
                                bool &script_error) {
    script_error = true;
    return lldb::eStateStepping;
  }

  virtual StructuredData::GenericSP
  CreateScriptedBreakpointResolver(const char *class_name,
                                   StructuredDataImpl *args_data,
                                   lldb::BreakpointSP &bkpt_sp) {
    return StructuredData::GenericSP();
  }
  
  virtual bool
  ScriptedBreakpointResolverSearchCallback(StructuredData::GenericSP implementor_sp,
                                           SymbolContext *sym_ctx)
  {
    return false;
  }

  virtual lldb::SearchDepth
  ScriptedBreakpointResolverSearchDepth(StructuredData::GenericSP implementor_sp)
  {
    return lldb::eSearchDepthModule;
  }

  virtual StructuredData::ObjectSP
  LoadPluginModule(const FileSpec &file_spec, lldb_private::Status &error) {
    return StructuredData::ObjectSP();
  }

  virtual StructuredData::DictionarySP
  GetDynamicSettings(StructuredData::ObjectSP plugin_module_sp, Target *target,
                     const char *setting_name, lldb_private::Status &error) {
    return StructuredData::DictionarySP();
  }

  virtual Status GenerateFunction(const char *signature,
                                  const StringList &input) {
    Status error;
    error.SetErrorString("unimplemented");
    return error;
  }

  virtual void CollectDataForBreakpointCommandCallback(
      std::vector<BreakpointOptions *> &options, CommandReturnObject &result);

  virtual void
  CollectDataForWatchpointCommandCallback(WatchpointOptions *wp_options,
                                          CommandReturnObject &result);

  /// Set the specified text as the callback for the breakpoint.
  Status
  SetBreakpointCommandCallback(std::vector<BreakpointOptions *> &bp_options_vec,
                               const char *callback_text);

  virtual Status SetBreakpointCommandCallback(BreakpointOptions *bp_options,
                                              const char *callback_text) {
    Status error;
    error.SetErrorString("unimplemented");
    return error;
  }

  /// This one is for deserialization:
  virtual Status SetBreakpointCommandCallback(
      BreakpointOptions *bp_options,
      std::unique_ptr<BreakpointOptions::CommandData> &data_up) {
    Status error;
    error.SetErrorString("unimplemented");
    return error;
  }

  void SetBreakpointCommandCallbackFunction(
      std::vector<BreakpointOptions *> &bp_options_vec,
      const char *function_name);

  /// Set a one-liner as the callback for the breakpoint.
  virtual void
  SetBreakpointCommandCallbackFunction(BreakpointOptions *bp_options,
                                       const char *function_name) {}

  /// Set a one-liner as the callback for the watchpoint.
  virtual void SetWatchpointCommandCallback(WatchpointOptions *wp_options,
                                            const char *oneliner) {}

  virtual bool GetScriptedSummary(const char *function_name,
                                  lldb::ValueObjectSP valobj,
                                  StructuredData::ObjectSP &callee_wrapper_sp,
                                  const TypeSummaryOptions &options,
                                  std::string &retval) {
    return false;
  }

  virtual void Clear() {
    // Clean up any ref counts to SBObjects that might be in global variables
  }

  virtual size_t
  CalculateNumChildren(const StructuredData::ObjectSP &implementor,
                       uint32_t max) {
    return 0;
  }

  virtual lldb::ValueObjectSP
  GetChildAtIndex(const StructuredData::ObjectSP &implementor, uint32_t idx) {
    return lldb::ValueObjectSP();
  }

  virtual int
  GetIndexOfChildWithName(const StructuredData::ObjectSP &implementor,
                          const char *child_name) {
    return UINT32_MAX;
  }

  virtual bool
  UpdateSynthProviderInstance(const StructuredData::ObjectSP &implementor) {
    return false;
  }

  virtual bool MightHaveChildrenSynthProviderInstance(
      const StructuredData::ObjectSP &implementor) {
    return true;
  }

  virtual lldb::ValueObjectSP
  GetSyntheticValue(const StructuredData::ObjectSP &implementor) {
    return nullptr;
  }

  virtual ConstString
  GetSyntheticTypeName(const StructuredData::ObjectSP &implementor) {
    return ConstString();
  }

  virtual bool
  RunScriptBasedCommand(const char *impl_function, llvm::StringRef args,
                        ScriptedCommandSynchronicity synchronicity,
                        lldb_private::CommandReturnObject &cmd_retobj,
                        Status &error,
                        const lldb_private::ExecutionContext &exe_ctx) {
    return false;
  }

  virtual bool RunScriptBasedCommand(
      StructuredData::GenericSP impl_obj_sp, llvm::StringRef args,
      ScriptedCommandSynchronicity synchronicity,
      lldb_private::CommandReturnObject &cmd_retobj, Status &error,
      const lldb_private::ExecutionContext &exe_ctx) {
    return false;
  }

  virtual bool RunScriptFormatKeyword(const char *impl_function,
                                      Process *process, std::string &output,
                                      Status &error) {
    error.SetErrorString("unimplemented");
    return false;
  }

  virtual bool RunScriptFormatKeyword(const char *impl_function, Thread *thread,
                                      std::string &output, Status &error) {
    error.SetErrorString("unimplemented");
    return false;
  }

  virtual bool RunScriptFormatKeyword(const char *impl_function, Target *target,
                                      std::string &output, Status &error) {
    error.SetErrorString("unimplemented");
    return false;
  }

  virtual bool RunScriptFormatKeyword(const char *impl_function,
                                      StackFrame *frame, std::string &output,
                                      Status &error) {
    error.SetErrorString("unimplemented");
    return false;
  }

  virtual bool RunScriptFormatKeyword(const char *impl_function,
                                      ValueObject *value, std::string &output,
                                      Status &error) {
    error.SetErrorString("unimplemented");
    return false;
  }

  virtual bool GetDocumentationForItem(const char *item, std::string &dest) {
    dest.clear();
    return false;
  }

  virtual bool
  GetShortHelpForCommandObject(StructuredData::GenericSP cmd_obj_sp,
                               std::string &dest) {
    dest.clear();
    return false;
  }

  virtual uint32_t
  GetFlagsForCommandObject(StructuredData::GenericSP cmd_obj_sp) {
    return 0;
  }

  virtual bool GetLongHelpForCommandObject(StructuredData::GenericSP cmd_obj_sp,
                                           std::string &dest) {
    dest.clear();
    return false;
  }

  virtual bool CheckObjectExists(const char *name) { return false; }

  virtual bool
  LoadScriptingModule(const char *filename, bool can_reload, bool init_session,
                      lldb_private::Status &error,
                      StructuredData::ObjectSP *module_sp = nullptr) {
    error.SetErrorString("loading unimplemented");
    return false;
  }

  virtual bool IsReservedWord(const char *word) { return false; }

  virtual std::unique_ptr<ScriptInterpreterLocker> AcquireInterpreterLock();

  const char *GetScriptInterpreterPtyName();

  int GetMasterFileDescriptor();

  CommandInterpreter &GetCommandInterpreter();

  static std::string LanguageToString(lldb::ScriptLanguage language);

  static lldb::ScriptLanguage StringToLanguage(const llvm::StringRef &string);

  virtual void ResetOutputFileHandle(FILE *new_fh) {} // By default, do nothing.

  lldb::ScriptLanguage GetLanguage() { return m_script_lang; }

protected:
  CommandInterpreter &m_interpreter;
  lldb::ScriptLanguage m_script_lang;
};

} // namespace lldb_private

#endif // liblldb_ScriptInterpreter_h_
