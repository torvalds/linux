//===-- CommandObjectExpression.cpp -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"

#include "CommandObjectExpression.h"
#include "Plugins/ExpressionParser/Clang/ClangExpressionVariable.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Value.h"
#include "lldb/Core/ValueObjectVariable.h"
#include "lldb/DataFormatters/ValueObjectPrinter.h"
#include "lldb/Expression/DWARFExpression.h"
#include "lldb/Expression/REPL.h"
#include "lldb/Expression/UserExpression.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/OptionParser.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/Variable.h"
#include "lldb/Target/Language.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"

using namespace lldb;
using namespace lldb_private;

CommandObjectExpression::CommandOptions::CommandOptions() : OptionGroup() {}

CommandObjectExpression::CommandOptions::~CommandOptions() = default;

static constexpr OptionEnumValueElement g_description_verbosity_type[] = {
    {eLanguageRuntimeDescriptionDisplayVerbosityCompact, "compact",
     "Only show the description string"},
    {eLanguageRuntimeDescriptionDisplayVerbosityFull, "full",
     "Show the full output, including persistent variable's name and type"} };

static constexpr OptionEnumValues DescriptionVerbosityTypes() {
  return OptionEnumValues(g_description_verbosity_type);
}

static constexpr OptionDefinition g_expression_options[] = {
    // clang-format off
  {LLDB_OPT_SET_1 | LLDB_OPT_SET_2, false, "all-threads",           'a', OptionParser::eRequiredArgument, nullptr, {},                          0, eArgTypeBoolean,              "Should we run all threads if the execution doesn't complete on one thread."},
  {LLDB_OPT_SET_1 | LLDB_OPT_SET_2, false, "ignore-breakpoints",    'i', OptionParser::eRequiredArgument, nullptr, {},                          0, eArgTypeBoolean,              "Ignore breakpoint hits while running expressions"},
  {LLDB_OPT_SET_1 | LLDB_OPT_SET_2, false, "timeout",               't', OptionParser::eRequiredArgument, nullptr, {},                          0, eArgTypeUnsignedInteger,      "Timeout value (in microseconds) for running the expression."},
  {LLDB_OPT_SET_1 | LLDB_OPT_SET_2, false, "unwind-on-error",       'u', OptionParser::eRequiredArgument, nullptr, {},                          0, eArgTypeBoolean,              "Clean up program state if the expression causes a crash, or raises a signal.  "
                                                                                                                                                                                  "Note, unlike gdb hitting a breakpoint is controlled by another option (-i)."},
  {LLDB_OPT_SET_1 | LLDB_OPT_SET_2, false, "debug",                 'g', OptionParser::eNoArgument,       nullptr, {},                          0, eArgTypeNone,                 "When specified, debug the JIT code by setting a breakpoint on the first instruction "
                                                                                                                                                                                  "and forcing breakpoints to not be ignored (-i0) and no unwinding to happen on error (-u0)."},
  {LLDB_OPT_SET_1 | LLDB_OPT_SET_2, false, "language",              'l', OptionParser::eRequiredArgument, nullptr, {},                          0, eArgTypeLanguage,             "Specifies the Language to use when parsing the expression.  If not set the target.language "
                                                                                                                                                                                  "setting is used." },
  {LLDB_OPT_SET_1 | LLDB_OPT_SET_2, false, "apply-fixits",          'X', OptionParser::eRequiredArgument, nullptr, {},                          0, eArgTypeLanguage,             "If true, simple fix-it hints will be automatically applied to the expression." },
  {LLDB_OPT_SET_1,                  false, "description-verbosity", 'v', OptionParser::eOptionalArgument, nullptr, DescriptionVerbosityTypes(), 0, eArgTypeDescriptionVerbosity, "How verbose should the output of this expression be, if the object description is asked for."},
  {LLDB_OPT_SET_1 | LLDB_OPT_SET_2, false, "top-level",             'p', OptionParser::eNoArgument,       nullptr, {},                          0, eArgTypeNone,                 "Interpret the expression as a complete translation unit, without injecting it into the local "
                                                                                                                                                                                  "context.  Allows declaration of persistent, top-level entities without a $ prefix."},
  {LLDB_OPT_SET_1 | LLDB_OPT_SET_2, false, "allow-jit",             'j', OptionParser::eRequiredArgument, nullptr, {},                          0, eArgTypeBoolean,              "Controls whether the expression can fall back to being JITted if it's not supported by "
                                                                                                                                                                                  "the interpreter (defaults to true)."}
    // clang-format on
};

Status CommandObjectExpression::CommandOptions::SetOptionValue(
    uint32_t option_idx, llvm::StringRef option_arg,
    ExecutionContext *execution_context) {
  Status error;

  const int short_option = GetDefinitions()[option_idx].short_option;

  switch (short_option) {
  case 'l':
    language = Language::GetLanguageTypeFromString(option_arg);
    if (language == eLanguageTypeUnknown)
      error.SetErrorStringWithFormat(
          "unknown language type: '%s' for expression",
          option_arg.str().c_str());
    break;

  case 'a': {
    bool success;
    bool result;
    result = OptionArgParser::ToBoolean(option_arg, true, &success);
    if (!success)
      error.SetErrorStringWithFormat(
          "invalid all-threads value setting: \"%s\"",
          option_arg.str().c_str());
    else
      try_all_threads = result;
  } break;

  case 'i': {
    bool success;
    bool tmp_value = OptionArgParser::ToBoolean(option_arg, true, &success);
    if (success)
      ignore_breakpoints = tmp_value;
    else
      error.SetErrorStringWithFormat(
          "could not convert \"%s\" to a boolean value.",
          option_arg.str().c_str());
    break;
  }

  case 'j': {
    bool success;
    bool tmp_value = OptionArgParser::ToBoolean(option_arg, true, &success);
    if (success)
      allow_jit = tmp_value;
    else
      error.SetErrorStringWithFormat(
          "could not convert \"%s\" to a boolean value.",
          option_arg.str().c_str());
    break;
  }

  case 't':
    if (option_arg.getAsInteger(0, timeout)) {
      timeout = 0;
      error.SetErrorStringWithFormat("invalid timeout setting \"%s\"",
                                     option_arg.str().c_str());
    }
    break;

  case 'u': {
    bool success;
    bool tmp_value = OptionArgParser::ToBoolean(option_arg, true, &success);
    if (success)
      unwind_on_error = tmp_value;
    else
      error.SetErrorStringWithFormat(
          "could not convert \"%s\" to a boolean value.",
          option_arg.str().c_str());
    break;
  }

  case 'v':
    if (option_arg.empty()) {
      m_verbosity = eLanguageRuntimeDescriptionDisplayVerbosityFull;
      break;
    }
    m_verbosity = (LanguageRuntimeDescriptionDisplayVerbosity)
        OptionArgParser::ToOptionEnum(
            option_arg, GetDefinitions()[option_idx].enum_values, 0, error);
    if (!error.Success())
      error.SetErrorStringWithFormat(
          "unrecognized value for description-verbosity '%s'",
          option_arg.str().c_str());
    break;

  case 'g':
    debug = true;
    unwind_on_error = false;
    ignore_breakpoints = false;
    break;

  case 'p':
    top_level = true;
    break;

  case 'X': {
    bool success;
    bool tmp_value = OptionArgParser::ToBoolean(option_arg, true, &success);
    if (success)
      auto_apply_fixits = tmp_value ? eLazyBoolYes : eLazyBoolNo;
    else
      error.SetErrorStringWithFormat(
          "could not convert \"%s\" to a boolean value.",
          option_arg.str().c_str());
    break;
  }

  default:
    error.SetErrorStringWithFormat("invalid short option character '%c'",
                                   short_option);
    break;
  }

  return error;
}

void CommandObjectExpression::CommandOptions::OptionParsingStarting(
    ExecutionContext *execution_context) {
  auto process_sp =
      execution_context ? execution_context->GetProcessSP() : ProcessSP();
  if (process_sp) {
    ignore_breakpoints = process_sp->GetIgnoreBreakpointsInExpressions();
    unwind_on_error = process_sp->GetUnwindOnErrorInExpressions();
  } else {
    ignore_breakpoints = true;
    unwind_on_error = true;
  }

  show_summary = true;
  try_all_threads = true;
  timeout = 0;
  debug = false;
  language = eLanguageTypeUnknown;
  m_verbosity = eLanguageRuntimeDescriptionDisplayVerbosityCompact;
  auto_apply_fixits = eLazyBoolCalculate;
  top_level = false;
  allow_jit = true;
}

llvm::ArrayRef<OptionDefinition>
CommandObjectExpression::CommandOptions::GetDefinitions() {
  return llvm::makeArrayRef(g_expression_options);
}

CommandObjectExpression::CommandObjectExpression(
    CommandInterpreter &interpreter)
    : CommandObjectRaw(
          interpreter, "expression", "Evaluate an expression on the current "
                                     "thread.  Displays any returned value "
                                     "with LLDB's default formatting.",
          "", eCommandProcessMustBePaused | eCommandTryTargetAPILock),
      IOHandlerDelegate(IOHandlerDelegate::Completion::Expression),
      m_option_group(), m_format_options(eFormatDefault),
      m_repl_option(LLDB_OPT_SET_1, false, "repl", 'r', "Drop into REPL", false,
                    true),
      m_command_options(), m_expr_line_count(0), m_expr_lines() {
  SetHelpLong(
      R"(
Single and multi-line expressions:

)"
      "    The expression provided on the command line must be a complete expression \
with no newlines.  To evaluate a multi-line expression, \
hit a return after an empty expression, and lldb will enter the multi-line expression editor. \
Hit return on an empty line to end the multi-line expression."
      
      R"(

Timeouts:

)"
      "    If the expression can be evaluated statically (without running code) then it will be.  \
Otherwise, by default the expression will run on the current thread with a short timeout: \
currently .25 seconds.  If it doesn't return in that time, the evaluation will be interrupted \
and resumed with all threads running.  You can use the -a option to disable retrying on all \
threads.  You can use the -t option to set a shorter timeout."
      R"(

User defined variables:

)"
      "    You can define your own variables for convenience or to be used in subsequent expressions.  \
You define them the same way you would define variables in C.  If the first character of \
your user defined variable is a $, then the variable's value will be available in future \
expressions, otherwise it will just be available in the current expression."
      R"(

Continuing evaluation after a breakpoint:

)"
      "    If the \"-i false\" option is used, and execution is interrupted by a breakpoint hit, once \
you are done with your investigation, you can either remove the expression execution frames \
from the stack with \"thread return -x\" or if you are still interested in the expression result \
you can issue the \"continue\" command and the expression evaluation will complete and the \
expression result will be available using the \"thread.completed-expression\" key in the thread \
format."

      R"(

Examples:

    expr my_struct->a = my_array[3]
    expr -f bin -- (index * 8) + 5
    expr unsigned int $foo = 5
    expr char c[] = \"foo\"; c[0])");

  CommandArgumentEntry arg;
  CommandArgumentData expression_arg;

  // Define the first (and only) variant of this arg.
  expression_arg.arg_type = eArgTypeExpression;
  expression_arg.arg_repetition = eArgRepeatPlain;

  // There is only one variant this argument could be; put it into the argument
  // entry.
  arg.push_back(expression_arg);

  // Push the data for the first argument into the m_arguments vector.
  m_arguments.push_back(arg);

  // Add the "--format" and "--gdb-format"
  m_option_group.Append(&m_format_options,
                        OptionGroupFormat::OPTION_GROUP_FORMAT |
                            OptionGroupFormat::OPTION_GROUP_GDB_FMT,
                        LLDB_OPT_SET_1);
  m_option_group.Append(&m_command_options);
  m_option_group.Append(&m_varobj_options, LLDB_OPT_SET_ALL,
                        LLDB_OPT_SET_1 | LLDB_OPT_SET_2);
  m_option_group.Append(&m_repl_option, LLDB_OPT_SET_ALL, LLDB_OPT_SET_3);
  m_option_group.Finalize();
}

CommandObjectExpression::~CommandObjectExpression() = default;

Options *CommandObjectExpression::GetOptions() { return &m_option_group; }

int CommandObjectExpression::HandleCompletion(CompletionRequest &request) {
  EvaluateExpressionOptions options;
  options.SetCoerceToId(m_varobj_options.use_objc);
  options.SetLanguage(m_command_options.language);
  options.SetExecutionPolicy(lldb_private::eExecutionPolicyNever);
  options.SetAutoApplyFixIts(false);
  options.SetGenerateDebugInfo(false);

  // We need a valid execution context with a frame pointer for this
  // completion, so if we don't have one we should try to make a valid
  // execution context.
  if (m_interpreter.GetExecutionContext().GetFramePtr() == nullptr)
    m_interpreter.UpdateExecutionContext(nullptr);

  // This didn't work, so let's get out before we start doing things that
  // expect a valid frame pointer.
  if (m_interpreter.GetExecutionContext().GetFramePtr() == nullptr)
    return 0;

  ExecutionContext exe_ctx(m_interpreter.GetExecutionContext());

  Target *target = exe_ctx.GetTargetPtr();

  if (!target)
    target = GetDummyTarget();

  if (!target)
    return 0;

  unsigned cursor_pos = request.GetRawCursorPos();
  llvm::StringRef code = request.GetRawLine();

  const std::size_t original_code_size = code.size();

  // Remove the first token which is 'expr' or some alias/abbreviation of that.
  code = llvm::getToken(code).second.ltrim();
  OptionsWithRaw args(code);
  code = args.GetRawPart();

  // The position where the expression starts in the command line.
  assert(original_code_size >= code.size());
  std::size_t raw_start = original_code_size - code.size();

  // Check if the cursor is actually in the expression string, and if not, we
  // exit.
  // FIXME: We should complete the options here.
  if (cursor_pos < raw_start)
    return 0;

  // Make the cursor_pos again relative to the start of the code string.
  assert(cursor_pos >= raw_start);
  cursor_pos -= raw_start;

  auto language = exe_ctx.GetFrameRef().GetLanguage();

  Status error;
  lldb::UserExpressionSP expr(target->GetUserExpressionForLanguage(
      code, llvm::StringRef(), language, UserExpression::eResultTypeAny,
      options, error));
  if (error.Fail())
    return 0;

  expr->Complete(exe_ctx, request, cursor_pos);
  return request.GetNumberOfMatches();
}

static lldb_private::Status
CanBeUsedForElementCountPrinting(ValueObject &valobj) {
  CompilerType type(valobj.GetCompilerType());
  CompilerType pointee;
  if (!type.IsPointerType(&pointee))
    return Status("as it does not refer to a pointer");
  if (pointee.IsVoidType())
    return Status("as it refers to a pointer to void");
  return Status();
}

bool CommandObjectExpression::EvaluateExpression(llvm::StringRef expr,
                                                 Stream *output_stream,
                                                 Stream *error_stream,
                                                 CommandReturnObject *result) {
  // Don't use m_exe_ctx as this might be called asynchronously after the
  // command object DoExecute has finished when doing multi-line expression
  // that use an input reader...
  ExecutionContext exe_ctx(m_interpreter.GetExecutionContext());

  Target *target = exe_ctx.GetTargetPtr();

  if (!target)
    target = GetDummyTarget();

  if (target) {
    lldb::ValueObjectSP result_valobj_sp;
    bool keep_in_memory = true;
    StackFrame *frame = exe_ctx.GetFramePtr();

    EvaluateExpressionOptions options;
    options.SetCoerceToId(m_varobj_options.use_objc);
    options.SetUnwindOnError(m_command_options.unwind_on_error);
    options.SetIgnoreBreakpoints(m_command_options.ignore_breakpoints);
    options.SetKeepInMemory(keep_in_memory);
    options.SetUseDynamic(m_varobj_options.use_dynamic);
    options.SetTryAllThreads(m_command_options.try_all_threads);
    options.SetDebug(m_command_options.debug);
    options.SetLanguage(m_command_options.language);
    options.SetExecutionPolicy(
        m_command_options.allow_jit
            ? EvaluateExpressionOptions::default_execution_policy
            : lldb_private::eExecutionPolicyNever);

    bool auto_apply_fixits;
    if (m_command_options.auto_apply_fixits == eLazyBoolCalculate)
      auto_apply_fixits = target->GetEnableAutoApplyFixIts();
    else
      auto_apply_fixits = m_command_options.auto_apply_fixits == eLazyBoolYes;

    options.SetAutoApplyFixIts(auto_apply_fixits);

    if (m_command_options.top_level)
      options.SetExecutionPolicy(eExecutionPolicyTopLevel);

    // If there is any chance we are going to stop and want to see what went
    // wrong with our expression, we should generate debug info
    if (!m_command_options.ignore_breakpoints ||
        !m_command_options.unwind_on_error)
      options.SetGenerateDebugInfo(true);

    if (m_command_options.timeout > 0)
      options.SetTimeout(std::chrono::microseconds(m_command_options.timeout));
    else
      options.SetTimeout(llvm::None);

    ExpressionResults success = target->EvaluateExpression(
        expr, frame, result_valobj_sp, options, &m_fixed_expression);

    // We only tell you about the FixIt if we applied it.  The compiler errors
    // will suggest the FixIt if it parsed.
    if (error_stream && !m_fixed_expression.empty() &&
        target->GetEnableNotifyAboutFixIts()) {
      if (success == eExpressionCompleted)
        error_stream->Printf(
            "  Fix-it applied, fixed expression was: \n    %s\n",
            m_fixed_expression.c_str());
    }

    if (result_valobj_sp) {
      Format format = m_format_options.GetFormat();

      if (result_valobj_sp->GetError().Success()) {
        if (format != eFormatVoid) {
          if (format != eFormatDefault)
            result_valobj_sp->SetFormat(format);

          if (m_varobj_options.elem_count > 0) {
            Status error(CanBeUsedForElementCountPrinting(*result_valobj_sp));
            if (error.Fail()) {
              result->AppendErrorWithFormat(
                  "expression cannot be used with --element-count %s\n",
                  error.AsCString(""));
              result->SetStatus(eReturnStatusFailed);
              return false;
            }
          }

          DumpValueObjectOptions options(m_varobj_options.GetAsDumpOptions(
              m_command_options.m_verbosity, format));
          options.SetVariableFormatDisplayLanguage(
              result_valobj_sp->GetPreferredDisplayLanguage());

          result_valobj_sp->Dump(*output_stream, options);

          if (result)
            result->SetStatus(eReturnStatusSuccessFinishResult);
        }
      } else {
        if (result_valobj_sp->GetError().GetError() ==
            UserExpression::kNoResult) {
          if (format != eFormatVoid &&
              m_interpreter.GetDebugger().GetNotifyVoid()) {
            error_stream->PutCString("(void)\n");
          }

          if (result)
            result->SetStatus(eReturnStatusSuccessFinishResult);
        } else {
          const char *error_cstr = result_valobj_sp->GetError().AsCString();
          if (error_cstr && error_cstr[0]) {
            const size_t error_cstr_len = strlen(error_cstr);
            const bool ends_with_newline =
                error_cstr[error_cstr_len - 1] == '\n';
            if (strstr(error_cstr, "error:") != error_cstr)
              error_stream->PutCString("error: ");
            error_stream->Write(error_cstr, error_cstr_len);
            if (!ends_with_newline)
              error_stream->EOL();
          } else {
            error_stream->PutCString("error: unknown error\n");
          }

          if (result)
            result->SetStatus(eReturnStatusFailed);
        }
      }
    }
  } else {
    error_stream->Printf("error: invalid execution context for expression\n");
    return false;
  }

  return true;
}

void CommandObjectExpression::IOHandlerInputComplete(IOHandler &io_handler,
                                                     std::string &line) {
  io_handler.SetIsDone(true);
  //    StreamSP output_stream =
  //    io_handler.GetDebugger().GetAsyncOutputStream();
  //    StreamSP error_stream = io_handler.GetDebugger().GetAsyncErrorStream();
  StreamFileSP output_sp(io_handler.GetOutputStreamFile());
  StreamFileSP error_sp(io_handler.GetErrorStreamFile());

  EvaluateExpression(line.c_str(), output_sp.get(), error_sp.get());
  if (output_sp)
    output_sp->Flush();
  if (error_sp)
    error_sp->Flush();
}

bool CommandObjectExpression::IOHandlerIsInputComplete(IOHandler &io_handler,
                                                       StringList &lines) {
  // An empty lines is used to indicate the end of input
  const size_t num_lines = lines.GetSize();
  if (num_lines > 0 && lines[num_lines - 1].empty()) {
    // Remove the last empty line from "lines" so it doesn't appear in our
    // resulting input and return true to indicate we are done getting lines
    lines.PopBack();
    return true;
  }
  return false;
}

void CommandObjectExpression::GetMultilineExpression() {
  m_expr_lines.clear();
  m_expr_line_count = 0;

  Debugger &debugger = GetCommandInterpreter().GetDebugger();
  bool color_prompt = debugger.GetUseColor();
  const bool multiple_lines = true; // Get multiple lines
  IOHandlerSP io_handler_sp(
      new IOHandlerEditline(debugger, IOHandler::Type::Expression,
                            "lldb-expr", // Name of input reader for history
                            llvm::StringRef(), // No prompt
                            llvm::StringRef(), // Continuation prompt
                            multiple_lines, color_prompt,
                            1, // Show line numbers starting at 1
                            *this));

  StreamFileSP output_sp(io_handler_sp->GetOutputStreamFile());
  if (output_sp) {
    output_sp->PutCString(
        "Enter expressions, then terminate with an empty line to evaluate:\n");
    output_sp->Flush();
  }
  debugger.PushIOHandler(io_handler_sp);
}

bool CommandObjectExpression::DoExecute(llvm::StringRef command,
                                        CommandReturnObject &result) {
  m_fixed_expression.clear();
  auto exe_ctx = GetCommandInterpreter().GetExecutionContext();
  m_option_group.NotifyOptionParsingStarting(&exe_ctx);

  if (command.empty()) {
    GetMultilineExpression();
    return result.Succeeded();
  }

  OptionsWithRaw args(command);
  llvm::StringRef expr = args.GetRawPart();

  if (args.HasArgs()) {
    if (!ParseOptionsAndNotify(args.GetArgs(), result, m_option_group, exe_ctx))
      return false;

    if (m_repl_option.GetOptionValue().GetCurrentValue()) {
      Target *target = m_interpreter.GetExecutionContext().GetTargetPtr();
      if (target) {
        // Drop into REPL
        m_expr_lines.clear();
        m_expr_line_count = 0;

        Debugger &debugger = target->GetDebugger();

        // Check if the LLDB command interpreter is sitting on top of a REPL
        // that launched it...
        if (debugger.CheckTopIOHandlerTypes(IOHandler::Type::CommandInterpreter,
                                            IOHandler::Type::REPL)) {
          // the LLDB command interpreter is sitting on top of a REPL that
          // launched it, so just say the command interpreter is done and
          // fall back to the existing REPL
          m_interpreter.GetIOHandler(false)->SetIsDone(true);
        } else {
          // We are launching the REPL on top of the current LLDB command
          // interpreter, so just push one
          bool initialize = false;
          Status repl_error;
          REPLSP repl_sp(target->GetREPL(repl_error, m_command_options.language,
                                         nullptr, false));

          if (!repl_sp) {
            initialize = true;
            repl_sp = target->GetREPL(repl_error, m_command_options.language,
                                      nullptr, true);
            if (!repl_error.Success()) {
              result.SetError(repl_error);
              return result.Succeeded();
            }
          }

          if (repl_sp) {
            if (initialize) {
              repl_sp->SetCommandOptions(m_command_options);
              repl_sp->SetFormatOptions(m_format_options);
              repl_sp->SetValueObjectDisplayOptions(m_varobj_options);
            }

            IOHandlerSP io_handler_sp(repl_sp->GetIOHandler());

            io_handler_sp->SetIsDone(false);

            debugger.PushIOHandler(io_handler_sp);
          } else {
            repl_error.SetErrorStringWithFormat(
                "Couldn't create a REPL for %s",
                Language::GetNameForLanguageType(m_command_options.language));
            result.SetError(repl_error);
            return result.Succeeded();
          }
        }
      }
    }
    // No expression following options
    else if (expr.empty()) {
      GetMultilineExpression();
      return result.Succeeded();
    }
  }

  Target *target = GetSelectedOrDummyTarget();
  if (EvaluateExpression(expr, &(result.GetOutputStream()),
                         &(result.GetErrorStream()), &result)) {

    if (!m_fixed_expression.empty() && target->GetEnableNotifyAboutFixIts()) {
      CommandHistory &history = m_interpreter.GetCommandHistory();
      // FIXME: Can we figure out what the user actually typed (e.g. some alias
      // for expr???)
      // If we can it would be nice to show that.
      std::string fixed_command("expression ");
      if (args.HasArgs()) {
        // Add in any options that might have been in the original command:
        fixed_command.append(args.GetArgStringWithDelimiter());
        fixed_command.append(m_fixed_expression);
      } else
        fixed_command.append(m_fixed_expression);
      history.AppendString(fixed_command);
    }
    // Increment statistics to record this expression evaluation success.
    target->IncrementStats(StatisticKind::ExpressionSuccessful);
    return true;
  }

  // Increment statistics to record this expression evaluation failure.
  target->IncrementStats(StatisticKind::ExpressionFailure);
  result.SetStatus(eReturnStatusFailed);
  return false;
}
