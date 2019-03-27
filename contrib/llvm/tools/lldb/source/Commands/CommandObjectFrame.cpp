//===-- CommandObjectFrame.cpp ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <string>

#include "CommandObjectFrame.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/StreamFile.h"
#include "lldb/Core/Value.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectVariable.h"
#include "lldb/DataFormatters/DataVisualization.h"
#include "lldb/DataFormatters/ValueObjectPrinter.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/OptionParser.h"
#include "lldb/Host/StringConvert.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionGroupFormat.h"
#include "lldb/Interpreter/OptionGroupValueObjectDisplay.h"
#include "lldb/Interpreter/OptionGroupVariable.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/Variable.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/StackFrameRecognizer.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/Timer.h"

using namespace lldb;
using namespace lldb_private;

#pragma mark CommandObjectFrameDiagnose

//-------------------------------------------------------------------------
// CommandObjectFrameInfo
//-------------------------------------------------------------------------

//-------------------------------------------------------------------------
// CommandObjectFrameDiagnose
//-------------------------------------------------------------------------

static constexpr OptionDefinition g_frame_diag_options[] = {
    // clang-format off
  { LLDB_OPT_SET_1, false, "register", 'r', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeRegisterName,    "A register to diagnose." },
  { LLDB_OPT_SET_1, false, "address",  'a', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeAddress,         "An address to diagnose." },
  { LLDB_OPT_SET_1, false, "offset",   'o', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeOffset,          "An optional offset.  Requires --register." }
    // clang-format on
};

class CommandObjectFrameDiagnose : public CommandObjectParsed {
public:
  class CommandOptions : public Options {
  public:
    CommandOptions() : Options() { OptionParsingStarting(nullptr); }

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;
      switch (short_option) {
      case 'r':
        reg = ConstString(option_arg);
        break;

      case 'a': {
        address.emplace();
        if (option_arg.getAsInteger(0, *address)) {
          address.reset();
          error.SetErrorStringWithFormat("invalid address argument '%s'",
                                         option_arg.str().c_str());
        }
      } break;

      case 'o': {
        offset.emplace();
        if (option_arg.getAsInteger(0, *offset)) {
          offset.reset();
          error.SetErrorStringWithFormat("invalid offset argument '%s'",
                                         option_arg.str().c_str());
        }
      } break;

      default:
        error.SetErrorStringWithFormat("invalid short option character '%c'",
                                       short_option);
        break;
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      address.reset();
      reg.reset();
      offset.reset();
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_frame_diag_options);
    }

    // Options.
    llvm::Optional<lldb::addr_t> address;
    llvm::Optional<ConstString> reg;
    llvm::Optional<int64_t> offset;
  };

  CommandObjectFrameDiagnose(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "frame diagnose",
                            "Try to determine what path path the current stop "
                            "location used to get to a register or address",
                            nullptr,
                            eCommandRequiresThread | eCommandTryTargetAPILock |
                                eCommandProcessMustBeLaunched |
                                eCommandProcessMustBePaused),
        m_options() {
    CommandArgumentEntry arg;
    CommandArgumentData index_arg;

    // Define the first (and only) variant of this arg.
    index_arg.arg_type = eArgTypeFrameIndex;
    index_arg.arg_repetition = eArgRepeatOptional;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg.push_back(index_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg);
  }

  ~CommandObjectFrameDiagnose() override = default;

  Options *GetOptions() override { return &m_options; }

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    Thread *thread = m_exe_ctx.GetThreadPtr();
    StackFrameSP frame_sp = thread->GetSelectedFrame();

    ValueObjectSP valobj_sp;

    if (m_options.address.hasValue()) {
      if (m_options.reg.hasValue() || m_options.offset.hasValue()) {
        result.AppendError(
            "`frame diagnose --address` is incompatible with other arguments.");
        result.SetStatus(eReturnStatusFailed);
        return false;
      }
      valobj_sp = frame_sp->GuessValueForAddress(m_options.address.getValue());
    } else if (m_options.reg.hasValue()) {
      valobj_sp = frame_sp->GuessValueForRegisterAndOffset(
          m_options.reg.getValue(), m_options.offset.getValueOr(0));
    } else {
      StopInfoSP stop_info_sp = thread->GetStopInfo();
      if (!stop_info_sp) {
        result.AppendError("No arguments provided, and no stop info.");
        result.SetStatus(eReturnStatusFailed);
        return false;
      }

      valobj_sp = StopInfo::GetCrashingDereference(stop_info_sp);
    }

    if (!valobj_sp) {
      result.AppendError("No diagnosis available.");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }


    DumpValueObjectOptions::DeclPrintingHelper helper = [&valobj_sp](
        ConstString type, ConstString var, const DumpValueObjectOptions &opts,
        Stream &stream) -> bool {
      const ValueObject::GetExpressionPathFormat format = ValueObject::
          GetExpressionPathFormat::eGetExpressionPathFormatHonorPointers;
      const bool qualify_cxx_base_classes = false;
      valobj_sp->GetExpressionPath(stream, qualify_cxx_base_classes, format);
      stream.PutCString(" =");
      return true;
    };

    DumpValueObjectOptions options;
    options.SetDeclPrintingHelper(helper);
    ValueObjectPrinter printer(valobj_sp.get(), &result.GetOutputStream(),
                               options);
    printer.PrintValueObject();

    return true;
  }

protected:
  CommandOptions m_options;
};

#pragma mark CommandObjectFrameInfo

//-------------------------------------------------------------------------
// CommandObjectFrameInfo
//-------------------------------------------------------------------------

class CommandObjectFrameInfo : public CommandObjectParsed {
public:
  CommandObjectFrameInfo(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "frame info", "List information about the current "
                                       "stack frame in the current thread.",
            "frame info",
            eCommandRequiresFrame | eCommandTryTargetAPILock |
                eCommandProcessMustBeLaunched | eCommandProcessMustBePaused) {}

  ~CommandObjectFrameInfo() override = default;

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    m_exe_ctx.GetFrameRef().DumpUsingSettingsFormat(&result.GetOutputStream());
    result.SetStatus(eReturnStatusSuccessFinishResult);
    return result.Succeeded();
  }
};

#pragma mark CommandObjectFrameSelect

//-------------------------------------------------------------------------
// CommandObjectFrameSelect
//-------------------------------------------------------------------------

static OptionDefinition g_frame_select_options[] = {
    // clang-format off
  { LLDB_OPT_SET_1, false, "relative", 'r', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeOffset, "A relative frame index offset from the current frame index." },
    // clang-format on
};

class CommandObjectFrameSelect : public CommandObjectParsed {
public:
  class CommandOptions : public Options {
  public:
    CommandOptions() : Options() { OptionParsingStarting(nullptr); }

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;
      switch (short_option) {
      case 'r':
        if (option_arg.getAsInteger(0, relative_frame_offset)) {
          relative_frame_offset = INT32_MIN;
          error.SetErrorStringWithFormat("invalid frame offset argument '%s'",
                                         option_arg.str().c_str());
        }
        break;

      default:
        error.SetErrorStringWithFormat("invalid short option character '%c'",
                                       short_option);
        break;
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      relative_frame_offset = INT32_MIN;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_frame_select_options);
    }

    int32_t relative_frame_offset;
  };

  CommandObjectFrameSelect(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "frame select", "Select the current stack frame by "
                                         "index from within the current thread "
                                         "(see 'thread backtrace'.)",
            nullptr,
            eCommandRequiresThread | eCommandTryTargetAPILock |
                eCommandProcessMustBeLaunched | eCommandProcessMustBePaused),
        m_options() {
    CommandArgumentEntry arg;
    CommandArgumentData index_arg;

    // Define the first (and only) variant of this arg.
    index_arg.arg_type = eArgTypeFrameIndex;
    index_arg.arg_repetition = eArgRepeatOptional;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg.push_back(index_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg);
  }

  ~CommandObjectFrameSelect() override = default;

  Options *GetOptions() override { return &m_options; }

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    // No need to check "thread" for validity as eCommandRequiresThread ensures
    // it is valid
    Thread *thread = m_exe_ctx.GetThreadPtr();

    uint32_t frame_idx = UINT32_MAX;
    if (m_options.relative_frame_offset != INT32_MIN) {
      // The one and only argument is a signed relative frame index
      frame_idx = thread->GetSelectedFrameIndex();
      if (frame_idx == UINT32_MAX)
        frame_idx = 0;

      if (m_options.relative_frame_offset < 0) {
        if (static_cast<int32_t>(frame_idx) >= -m_options.relative_frame_offset)
          frame_idx += m_options.relative_frame_offset;
        else {
          if (frame_idx == 0) {
            // If you are already at the bottom of the stack, then just warn
            // and don't reset the frame.
            result.AppendError("Already at the bottom of the stack.");
            result.SetStatus(eReturnStatusFailed);
            return false;
          } else
            frame_idx = 0;
        }
      } else if (m_options.relative_frame_offset > 0) {
        // I don't want "up 20" where "20" takes you past the top of the stack
        // to produce
        // an error, but rather to just go to the top.  So I have to count the
        // stack here...
        const uint32_t num_frames = thread->GetStackFrameCount();
        if (static_cast<int32_t>(num_frames - frame_idx) >
            m_options.relative_frame_offset)
          frame_idx += m_options.relative_frame_offset;
        else {
          if (frame_idx == num_frames - 1) {
            // If we are already at the top of the stack, just warn and don't
            // reset the frame.
            result.AppendError("Already at the top of the stack.");
            result.SetStatus(eReturnStatusFailed);
            return false;
          } else
            frame_idx = num_frames - 1;
        }
      }
    } else {
      if (command.GetArgumentCount() > 1) {
        result.AppendErrorWithFormat(
            "too many arguments; expected frame-index, saw '%s'.\n",
            command[0].c_str());
        m_options.GenerateOptionUsage(
            result.GetErrorStream(), this,
            GetCommandInterpreter().GetDebugger().GetTerminalWidth());
        return false;
      }

      if (command.GetArgumentCount() == 1) {
        if (command[0].ref.getAsInteger(0, frame_idx)) {
          result.AppendErrorWithFormat("invalid frame index argument '%s'.",
                                       command[0].c_str());
          result.SetStatus(eReturnStatusFailed);
          return false;
        }
      } else if (command.GetArgumentCount() == 0) {
        frame_idx = thread->GetSelectedFrameIndex();
        if (frame_idx == UINT32_MAX) {
          frame_idx = 0;
        }
      }
    }

    bool success = thread->SetSelectedFrameByIndexNoisily(
        frame_idx, result.GetOutputStream());
    if (success) {
      m_exe_ctx.SetFrameSP(thread->GetSelectedFrame());
      result.SetStatus(eReturnStatusSuccessFinishResult);
    } else {
      result.AppendErrorWithFormat("Frame index (%u) out of range.\n",
                                   frame_idx);
      result.SetStatus(eReturnStatusFailed);
    }

    return result.Succeeded();
  }

protected:
  CommandOptions m_options;
};

#pragma mark CommandObjectFrameVariable
//----------------------------------------------------------------------
// List images with associated information
//----------------------------------------------------------------------
class CommandObjectFrameVariable : public CommandObjectParsed {
public:
  CommandObjectFrameVariable(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "frame variable",
            "Show variables for the current stack frame. Defaults to all "
            "arguments and local variables in scope. Names of argument, "
            "local, file static and file global variables can be specified. "
            "Children of aggregate variables can be specified such as "
            "'var->child.x'.  The -> and [] operators in 'frame variable' do "
            "not invoke operator overloads if they exist, but directly access "
            "the specified element.  If you want to trigger operator overloads "
            "use the expression command to print the variable instead."
            "\nIt is worth noting that except for overloaded "
            "operators, when printing local variables 'expr local_var' and "
            "'frame var local_var' produce the same "
            "results.  However, 'frame variable' is more efficient, since it "
            "uses debug information and memory reads directly, rather than "
            "parsing and evaluating an expression, which may even involve "
            "JITing and running code in the target program.",
            nullptr, eCommandRequiresFrame | eCommandTryTargetAPILock |
                         eCommandProcessMustBeLaunched |
                         eCommandProcessMustBePaused | eCommandRequiresProcess),
        m_option_group(),
        m_option_variable(
            true), // Include the frame specific options by passing "true"
        m_option_format(eFormatDefault),
        m_varobj_options() {
    CommandArgumentEntry arg;
    CommandArgumentData var_name_arg;

    // Define the first (and only) variant of this arg.
    var_name_arg.arg_type = eArgTypeVarName;
    var_name_arg.arg_repetition = eArgRepeatStar;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg.push_back(var_name_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg);

    m_option_group.Append(&m_option_variable, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Append(&m_option_format,
                          OptionGroupFormat::OPTION_GROUP_FORMAT |
                              OptionGroupFormat::OPTION_GROUP_GDB_FMT,
                          LLDB_OPT_SET_1);
    m_option_group.Append(&m_varobj_options, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Finalize();
  }

  ~CommandObjectFrameVariable() override = default;

  Options *GetOptions() override { return &m_option_group; }

  int HandleArgumentCompletion(
      CompletionRequest &request,
      OptionElementVector &opt_element_vector) override {
    // Arguments are the standard source file completer.
    CommandCompletions::InvokeCommonCompletionCallbacks(
        GetCommandInterpreter(), CommandCompletions::eVariablePathCompletion,
        request, nullptr);
    return request.GetNumberOfMatches();
  }

protected:
  llvm::StringRef GetScopeString(VariableSP var_sp) {
    if (!var_sp)
      return llvm::StringRef::withNullAsEmpty(nullptr);

    switch (var_sp->GetScope()) {
    case eValueTypeVariableGlobal:
      return "GLOBAL: ";
    case eValueTypeVariableStatic:
      return "STATIC: ";
    case eValueTypeVariableArgument:
      return "ARG: ";
    case eValueTypeVariableLocal:
      return "LOCAL: ";
    case eValueTypeVariableThreadLocal:
      return "THREAD: ";
    default:
      break;
    }

    return llvm::StringRef::withNullAsEmpty(nullptr);
  }

  bool DoExecute(Args &command, CommandReturnObject &result) override {
    // No need to check "frame" for validity as eCommandRequiresFrame ensures
    // it is valid
    StackFrame *frame = m_exe_ctx.GetFramePtr();

    Stream &s = result.GetOutputStream();

    // Be careful about the stack frame, if any summary formatter runs code, it
    // might clear the StackFrameList for the thread.  So hold onto a shared
    // pointer to the frame so it stays alive.

    VariableList *variable_list =
        frame->GetVariableList(m_option_variable.show_globals);

    VariableSP var_sp;
    ValueObjectSP valobj_sp;

    TypeSummaryImplSP summary_format_sp;
    if (!m_option_variable.summary.IsCurrentValueEmpty())
      DataVisualization::NamedSummaryFormats::GetSummaryFormat(
          ConstString(m_option_variable.summary.GetCurrentValue()),
          summary_format_sp);
    else if (!m_option_variable.summary_string.IsCurrentValueEmpty())
      summary_format_sp.reset(new StringSummaryFormat(
          TypeSummaryImpl::Flags(),
          m_option_variable.summary_string.GetCurrentValue()));

    DumpValueObjectOptions options(m_varobj_options.GetAsDumpOptions(
        eLanguageRuntimeDescriptionDisplayVerbosityFull, eFormatDefault,
        summary_format_sp));

    const SymbolContext &sym_ctx =
        frame->GetSymbolContext(eSymbolContextFunction);
    if (sym_ctx.function && sym_ctx.function->IsTopLevelFunction())
      m_option_variable.show_globals = true;

    if (variable_list) {
      const Format format = m_option_format.GetFormat();
      options.SetFormat(format);

      if (!command.empty()) {
        VariableList regex_var_list;

        // If we have any args to the variable command, we will make variable
        // objects from them...
        for (auto &entry : command) {
          if (m_option_variable.use_regex) {
            const size_t regex_start_index = regex_var_list.GetSize();
            llvm::StringRef name_str = entry.ref;
            RegularExpression regex(name_str);
            if (regex.Compile(name_str)) {
              size_t num_matches = 0;
              const size_t num_new_regex_vars =
                  variable_list->AppendVariablesIfUnique(regex, regex_var_list,
                                                         num_matches);
              if (num_new_regex_vars > 0) {
                for (size_t regex_idx = regex_start_index,
                            end_index = regex_var_list.GetSize();
                     regex_idx < end_index; ++regex_idx) {
                  var_sp = regex_var_list.GetVariableAtIndex(regex_idx);
                  if (var_sp) {
                    valobj_sp = frame->GetValueObjectForFrameVariable(
                        var_sp, m_varobj_options.use_dynamic);
                    if (valobj_sp) {
                      std::string scope_string;
                      if (m_option_variable.show_scope)
                        scope_string = GetScopeString(var_sp).str();

                      if (!scope_string.empty())
                        s.PutCString(scope_string);

                      if (m_option_variable.show_decl &&
                          var_sp->GetDeclaration().GetFile()) {
                        bool show_fullpaths = false;
                        bool show_module = true;
                        if (var_sp->DumpDeclaration(&s, show_fullpaths,
                                                    show_module))
                          s.PutCString(": ");
                      }
                      valobj_sp->Dump(result.GetOutputStream(), options);
                    }
                  }
                }
              } else if (num_matches == 0) {
                result.GetErrorStream().Printf("error: no variables matched "
                                               "the regular expression '%s'.\n",
                                               entry.c_str());
              }
            } else {
              char regex_error[1024];
              if (regex.GetErrorAsCString(regex_error, sizeof(regex_error)))
                result.GetErrorStream().Printf("error: %s\n", regex_error);
              else
                result.GetErrorStream().Printf(
                    "error: unknown regex error when compiling '%s'\n",
                    entry.c_str());
            }
          } else // No regex, either exact variable names or variable
                 // expressions.
          {
            Status error;
            uint32_t expr_path_options =
                StackFrame::eExpressionPathOptionCheckPtrVsMember |
                StackFrame::eExpressionPathOptionsAllowDirectIVarAccess |
                StackFrame::eExpressionPathOptionsInspectAnonymousUnions;
            lldb::VariableSP var_sp;
            valobj_sp = frame->GetValueForVariableExpressionPath(
                entry.ref, m_varobj_options.use_dynamic, expr_path_options,
                var_sp, error);
            if (valobj_sp) {
              std::string scope_string;
              if (m_option_variable.show_scope)
                scope_string = GetScopeString(var_sp).str();

              if (!scope_string.empty())
                s.PutCString(scope_string);
              if (m_option_variable.show_decl && var_sp &&
                  var_sp->GetDeclaration().GetFile()) {
                var_sp->GetDeclaration().DumpStopContext(&s, false);
                s.PutCString(": ");
              }

              options.SetFormat(format);
              options.SetVariableFormatDisplayLanguage(
                  valobj_sp->GetPreferredDisplayLanguage());

              Stream &output_stream = result.GetOutputStream();
              options.SetRootValueObjectName(
                  valobj_sp->GetParent() ? entry.c_str() : nullptr);
              valobj_sp->Dump(output_stream, options);
            } else {
              const char *error_cstr = error.AsCString(nullptr);
              if (error_cstr)
                result.GetErrorStream().Printf("error: %s\n", error_cstr);
              else
                result.GetErrorStream().Printf("error: unable to find any "
                                               "variable expression path that "
                                               "matches '%s'.\n",
                                               entry.c_str());
            }
          }
        }
      } else // No command arg specified.  Use variable_list, instead.
      {
        const size_t num_variables = variable_list->GetSize();
        if (num_variables > 0) {
          for (size_t i = 0; i < num_variables; i++) {
            var_sp = variable_list->GetVariableAtIndex(i);
            switch (var_sp->GetScope()) {
            case eValueTypeVariableGlobal:
              if (!m_option_variable.show_globals)
                continue;
              break;
            case eValueTypeVariableStatic:
              if (!m_option_variable.show_globals)
                continue;
              break;
            case eValueTypeVariableArgument:
              if (!m_option_variable.show_args)
                continue;
              break;
            case eValueTypeVariableLocal:
              if (!m_option_variable.show_locals)
                continue;
              break;
            default:
              continue;
              break;
            }
            std::string scope_string;
            if (m_option_variable.show_scope)
              scope_string = GetScopeString(var_sp).str();

            // Use the variable object code to make sure we are using the same
            // APIs as the public API will be using...
            valobj_sp = frame->GetValueObjectForFrameVariable(
                var_sp, m_varobj_options.use_dynamic);
            if (valobj_sp) {
              // When dumping all variables, don't print any variables that are
              // not in scope to avoid extra unneeded output
              if (valobj_sp->IsInScope()) {
                if (!valobj_sp->GetTargetSP()
                         ->GetDisplayRuntimeSupportValues() &&
                    valobj_sp->IsRuntimeSupportValue())
                  continue;

                if (!scope_string.empty())
                  s.PutCString(scope_string);

                if (m_option_variable.show_decl &&
                    var_sp->GetDeclaration().GetFile()) {
                  var_sp->GetDeclaration().DumpStopContext(&s, false);
                  s.PutCString(": ");
                }

                options.SetFormat(format);
                options.SetVariableFormatDisplayLanguage(
                    valobj_sp->GetPreferredDisplayLanguage());
                options.SetRootValueObjectName(
                    var_sp ? var_sp->GetName().AsCString() : nullptr);
                valobj_sp->Dump(result.GetOutputStream(), options);
              }
            }
          }
        }
      }
      result.SetStatus(eReturnStatusSuccessFinishResult);
    }

    if (m_option_variable.show_recognized_args) {
      auto recognized_frame = frame->GetRecognizedFrame();
      if (recognized_frame) {
        ValueObjectListSP recognized_arg_list =
            recognized_frame->GetRecognizedArguments();
        if (recognized_arg_list) {
          for (auto &rec_value_sp : recognized_arg_list->GetObjects()) {
            options.SetFormat(m_option_format.GetFormat());
            options.SetVariableFormatDisplayLanguage(
                rec_value_sp->GetPreferredDisplayLanguage());
            options.SetRootValueObjectName(rec_value_sp->GetName().AsCString());
            rec_value_sp->Dump(result.GetOutputStream(), options);
          }
        }
      }
    }

    if (m_interpreter.TruncationWarningNecessary()) {
      result.GetOutputStream().Printf(m_interpreter.TruncationWarningText(),
                                      m_cmd_name.c_str());
      m_interpreter.TruncationWarningGiven();
    }

    // Increment statistics.
    bool res = result.Succeeded();
    Target *target = GetSelectedOrDummyTarget();
    if (res)
      target->IncrementStats(StatisticKind::FrameVarSuccess);
    else
      target->IncrementStats(StatisticKind::FrameVarFailure);
    return res;
  }

protected:
  OptionGroupOptions m_option_group;
  OptionGroupVariable m_option_variable;
  OptionGroupFormat m_option_format;
  OptionGroupValueObjectDisplay m_varobj_options;
};

#pragma mark CommandObjectFrameRecognizer

static OptionDefinition g_frame_recognizer_add_options[] = {
    // clang-format off
  { LLDB_OPT_SET_ALL, false, "shlib",         's', OptionParser::eRequiredArgument, nullptr, {}, CommandCompletions::eModuleCompletion, eArgTypeShlibName,   "Name of the module or shared library that this recognizer applies to." },
  { LLDB_OPT_SET_ALL, false, "function",      'n', OptionParser::eRequiredArgument, nullptr, {}, CommandCompletions::eSymbolCompletion, eArgTypeName,        "Name of the function that this recognizer applies to." },
  { LLDB_OPT_SET_2,   false, "python-class",  'l', OptionParser::eRequiredArgument, nullptr, {}, 0,                                     eArgTypePythonClass, "Give the name of a Python class to use for this frame recognizer." },
  { LLDB_OPT_SET_ALL, false, "regex",         'x', OptionParser::eNoArgument,       nullptr, {}, 0,                                     eArgTypeNone,        "Function name and module name are actually regular expressions." }
    // clang-format on
};

class CommandObjectFrameRecognizerAdd : public CommandObjectParsed {
private:
  class CommandOptions : public Options {
  public:
    CommandOptions() : Options() {}
    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'l':
        m_class_name = std::string(option_arg);
        break;
      case 's':
        m_module = std::string(option_arg);
        break;
      case 'n':
        m_function = std::string(option_arg);
        break;
      case 'x':
        m_regex = true;
        break;
      default:
        error.SetErrorStringWithFormat("unrecognized option '%c'",
                                       short_option);
        break;
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_module = "";
      m_function = "";
      m_class_name = "";
      m_regex = false;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_frame_recognizer_add_options);
    }

    // Instance variables to hold the values for command options.
    std::string m_class_name;
    std::string m_module;
    std::string m_function;
    bool m_regex;
  };

  CommandOptions m_options;

  Options *GetOptions() override { return &m_options; }

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override;

public:
  CommandObjectFrameRecognizerAdd(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "frame recognizer add",
                            "Add a new frame recognizer.", nullptr),
        m_options() {
    SetHelpLong(R"(
Frame recognizers allow for retrieving information about special frames based on
ABI, arguments or other special properties of that frame, even without source
code or debug info. Currently, one use case is to extract function arguments
that would otherwise be unaccesible, or augment existing arguments.

Adding a custom frame recognizer is possible by implementing a Python class
and using the 'frame recognizer add' command. The Python class should have a
'get_recognized_arguments' method and it will receive an argument of type
lldb.SBFrame representing the current frame that we are trying to recognize.
The method should return a (possibly empty) list of lldb.SBValue objects that
represent the recognized arguments.

An example of a recognizer that retrieves the file descriptor values from libc
functions 'read', 'write' and 'close' follows:

  class LibcFdRecognizer(object):
    def get_recognized_arguments(self, frame):
      if frame.name in ["read", "write", "close"]:
        fd = frame.EvaluateExpression("$arg1").unsigned
        value = lldb.target.CreateValueFromExpression("fd", "(int)%d" % fd)
        return [value]
      return []

The file containing this implementation can be imported via 'command script
import' and then we can register this recognizer with 'frame recognizer add'.
It's important to restrict the recognizer to the libc library (which is
libsystem_kernel.dylib on macOS) to avoid matching functions with the same name
in other modules:

(lldb) command script import .../fd_recognizer.py
(lldb) frame recognizer add -l fd_recognizer.LibcFdRecognizer -n read -s libsystem_kernel.dylib

When the program is stopped at the beginning of the 'read' function in libc, we
can view the recognizer arguments in 'frame variable':

(lldb) b read
(lldb) r
Process 1234 stopped
* thread #1, queue = 'com.apple.main-thread', stop reason = breakpoint 1.3
    frame #0: 0x00007fff06013ca0 libsystem_kernel.dylib`read
(lldb) frame variable
(int) fd = 3

    )");
  }
  ~CommandObjectFrameRecognizerAdd() override = default;
};

bool CommandObjectFrameRecognizerAdd::DoExecute(Args &command,
                                                CommandReturnObject &result) {
#ifndef LLDB_DISABLE_PYTHON
  if (m_options.m_class_name.empty()) {
    result.AppendErrorWithFormat(
        "%s needs a Python class name (-l argument).\n", m_cmd_name.c_str());
    result.SetStatus(eReturnStatusFailed);
    return false;
  }

  if (m_options.m_module.empty()) {
    result.AppendErrorWithFormat("%s needs a module name (-s argument).\n",
                                 m_cmd_name.c_str());
    result.SetStatus(eReturnStatusFailed);
    return false;
  }

  if (m_options.m_function.empty()) {
    result.AppendErrorWithFormat("%s needs a function name (-n argument).\n",
                                 m_cmd_name.c_str());
    result.SetStatus(eReturnStatusFailed);
    return false;
  }

  ScriptInterpreter *interpreter = m_interpreter.GetScriptInterpreter();

  if (interpreter &&
      !interpreter->CheckObjectExists(m_options.m_class_name.c_str())) {
    result.AppendWarning(
        "The provided class does not exist - please define it "
        "before attempting to use this frame recognizer");
  }

  StackFrameRecognizerSP recognizer_sp =
      StackFrameRecognizerSP(new ScriptedStackFrameRecognizer(
          interpreter, m_options.m_class_name.c_str()));
  if (m_options.m_regex) {
    auto module =
        RegularExpressionSP(new RegularExpression(m_options.m_module));
    auto func =
        RegularExpressionSP(new RegularExpression(m_options.m_function));
    StackFrameRecognizerManager::AddRecognizer(recognizer_sp, module, func);
  } else {
    auto module = ConstString(m_options.m_module);
    auto func = ConstString(m_options.m_function);
    StackFrameRecognizerManager::AddRecognizer(recognizer_sp, module, func);
  }
#endif

  result.SetStatus(eReturnStatusSuccessFinishNoResult);
  return result.Succeeded();
}

class CommandObjectFrameRecognizerClear : public CommandObjectParsed {
public:
  CommandObjectFrameRecognizerClear(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "frame recognizer clear",
                           "Delete all frame recognizers.", nullptr) {}

  ~CommandObjectFrameRecognizerClear() override = default;

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    StackFrameRecognizerManager::RemoveAllRecognizers();
    result.SetStatus(eReturnStatusSuccessFinishResult);
    return result.Succeeded();
  }
};

class CommandObjectFrameRecognizerDelete : public CommandObjectParsed {
 public:
  CommandObjectFrameRecognizerDelete(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "frame recognizer delete",
                            "Delete an existing frame recognizer.", nullptr) {}

  ~CommandObjectFrameRecognizerDelete() override = default;

 protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    if (command.GetArgumentCount() == 0) {
      if (!m_interpreter.Confirm(
              "About to delete all frame recognizers, do you want to do that?",
              true)) {
        result.AppendMessage("Operation cancelled...");
        result.SetStatus(eReturnStatusFailed);
        return false;
      }

      StackFrameRecognizerManager::RemoveAllRecognizers();
      result.SetStatus(eReturnStatusSuccessFinishResult);
      return result.Succeeded();
    }

    if (command.GetArgumentCount() != 1) {
      result.AppendErrorWithFormat("'%s' takes zero or one arguments.\n",
                                   m_cmd_name.c_str());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    uint32_t recognizer_id =
        StringConvert::ToUInt32(command.GetArgumentAtIndex(0), 0, 0);

    StackFrameRecognizerManager::RemoveRecognizerWithID(recognizer_id);
    result.SetStatus(eReturnStatusSuccessFinishResult);
    return result.Succeeded();
  }
};

class CommandObjectFrameRecognizerList : public CommandObjectParsed {
 public:
  CommandObjectFrameRecognizerList(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "frame recognizer list",
                            "Show a list of active frame recognizers.",
                            nullptr) {}

  ~CommandObjectFrameRecognizerList() override = default;

 protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    bool any_printed = false;
    StackFrameRecognizerManager::ForEach(
        [&result, &any_printed](uint32_t recognizer_id, std::string name,
                                std::string function, std::string symbol,
                                bool regexp) {
          if (name == "") name = "(internal)";
          result.GetOutputStream().Printf(
              "%d: %s, module %s, function %s%s\n", recognizer_id, name.c_str(),
              function.c_str(), symbol.c_str(), regexp ? " (regexp)" : "");
          any_printed = true;
        });

    if (any_printed)
      result.SetStatus(eReturnStatusSuccessFinishResult);
    else {
      result.GetOutputStream().PutCString("no matching results found.\n");
      result.SetStatus(eReturnStatusSuccessFinishNoResult);
    }
    return result.Succeeded();
  }
};

class CommandObjectFrameRecognizerInfo : public CommandObjectParsed {
 public:
  CommandObjectFrameRecognizerInfo(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "frame recognizer info",
            "Show which frame recognizer is applied a stack frame (if any).",
            nullptr) {
    CommandArgumentEntry arg;
    CommandArgumentData index_arg;

    // Define the first (and only) variant of this arg.
    index_arg.arg_type = eArgTypeFrameIndex;
    index_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg.push_back(index_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg);
  }

  ~CommandObjectFrameRecognizerInfo() override = default;

 protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    Process *process = m_exe_ctx.GetProcessPtr();
    if (process == nullptr) {
      result.AppendError("no process");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }
    Thread *thread = m_exe_ctx.GetThreadPtr();
    if (thread == nullptr) {
      result.AppendError("no thread");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }
    if (command.GetArgumentCount() != 1) {
      result.AppendErrorWithFormat(
          "'%s' takes exactly one frame index argument.\n", m_cmd_name.c_str());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    uint32_t frame_index =
        StringConvert::ToUInt32(command.GetArgumentAtIndex(0), 0, 0);
    StackFrameSP frame_sp = thread->GetStackFrameAtIndex(frame_index);
    if (!frame_sp) {
      result.AppendErrorWithFormat("no frame with index %u", frame_index);
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    auto recognizer =
        StackFrameRecognizerManager::GetRecognizerForFrame(frame_sp);

    Stream &output_stream = result.GetOutputStream();
    output_stream.Printf("frame %d ", frame_index);
    if (recognizer) {
      output_stream << "is recognized by ";
      output_stream << recognizer->GetName();
    } else {
      output_stream << "not recognized by any recognizer";
    }
    output_stream.EOL();
    result.SetStatus(eReturnStatusSuccessFinishResult);
    return result.Succeeded();
  }
};

class CommandObjectFrameRecognizer : public CommandObjectMultiword {
 public:
  CommandObjectFrameRecognizer(CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "frame recognizer",
            "Commands for editing and viewing frame recognizers.",
            "frame recognizer [<sub-command-options>] ") {
    LoadSubCommand(
        "add",
        CommandObjectSP(new CommandObjectFrameRecognizerAdd(interpreter)));
    LoadSubCommand(
        "clear",
        CommandObjectSP(new CommandObjectFrameRecognizerClear(interpreter)));
    LoadSubCommand(
        "delete",
        CommandObjectSP(new CommandObjectFrameRecognizerDelete(interpreter)));
    LoadSubCommand(
        "list",
        CommandObjectSP(new CommandObjectFrameRecognizerList(interpreter)));
    LoadSubCommand(
        "info",
        CommandObjectSP(new CommandObjectFrameRecognizerInfo(interpreter)));
  }

  ~CommandObjectFrameRecognizer() override = default;
};

#pragma mark CommandObjectMultiwordFrame

//-------------------------------------------------------------------------
// CommandObjectMultiwordFrame
//-------------------------------------------------------------------------

CommandObjectMultiwordFrame::CommandObjectMultiwordFrame(
    CommandInterpreter &interpreter)
    : CommandObjectMultiword(interpreter, "frame", "Commands for selecting and "
                                                   "examing the current "
                                                   "thread's stack frames.",
                             "frame <subcommand> [<subcommand-options>]") {
  LoadSubCommand("diagnose",
                 CommandObjectSP(new CommandObjectFrameDiagnose(interpreter)));
  LoadSubCommand("info",
                 CommandObjectSP(new CommandObjectFrameInfo(interpreter)));
  LoadSubCommand("select",
                 CommandObjectSP(new CommandObjectFrameSelect(interpreter)));
  LoadSubCommand("variable",
                 CommandObjectSP(new CommandObjectFrameVariable(interpreter)));
#ifndef LLDB_DISABLE_PYTHON
  LoadSubCommand(
      "recognizer",
      CommandObjectSP(new CommandObjectFrameRecognizer(interpreter)));
#endif
}

CommandObjectMultiwordFrame::~CommandObjectMultiwordFrame() = default;
