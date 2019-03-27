//===-- CommandObjectSettings.cpp -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "CommandObjectSettings.h"

#include "llvm/ADT/StringRef.h"

#include "lldb/Host/OptionParser.h"
#include "lldb/Interpreter/CommandCompletions.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionValueProperties.h"

using namespace lldb;
using namespace lldb_private;

//-------------------------------------------------------------------------
// CommandObjectSettingsSet
//-------------------------------------------------------------------------

static constexpr OptionDefinition g_settings_set_options[] = {
    // clang-format off
  { LLDB_OPT_SET_2, false, "global", 'g', OptionParser::eNoArgument, nullptr, {}, 0, eArgTypeNone, "Apply the new value to the global default value." },
  { LLDB_OPT_SET_2, false, "force",  'f', OptionParser::eNoArgument, nullptr, {}, 0, eArgTypeNone, "Force an empty value to be accepted as the default." }
    // clang-format on
};

class CommandObjectSettingsSet : public CommandObjectRaw {
public:
  CommandObjectSettingsSet(CommandInterpreter &interpreter)
      : CommandObjectRaw(interpreter, "settings set",
                         "Set the value of the specified debugger setting."),
        m_options() {
    CommandArgumentEntry arg1;
    CommandArgumentEntry arg2;
    CommandArgumentData var_name_arg;
    CommandArgumentData value_arg;

    // Define the first (and only) variant of this arg.
    var_name_arg.arg_type = eArgTypeSettingVariableName;
    var_name_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg1.push_back(var_name_arg);

    // Define the first (and only) variant of this arg.
    value_arg.arg_type = eArgTypeValue;
    value_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg2.push_back(value_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg1);
    m_arguments.push_back(arg2);

    SetHelpLong(
        "\nWhen setting a dictionary or array variable, you can set multiple entries \
at once by giving the values to the set command.  For example:"
        R"(

(lldb) settings set target.run-args value1 value2 value3
(lldb) settings set target.env-vars MYPATH=~/.:/usr/bin  SOME_ENV_VAR=12345

(lldb) settings show target.run-args
  [0]: 'value1'
  [1]: 'value2'
  [3]: 'value3'
(lldb) settings show target.env-vars
  'MYPATH=~/.:/usr/bin'
  'SOME_ENV_VAR=12345'

)"
        "Warning:  The 'set' command re-sets the entire array or dictionary.  If you \
just want to add, remove or update individual values (or add something to \
the end), use one of the other settings sub-commands: append, replace, \
insert-before or insert-after.");
  }

  ~CommandObjectSettingsSet() override = default;

  // Overrides base class's behavior where WantsCompletion =
  // !WantsRawCommandString.
  bool WantsCompletion() override { return true; }

  Options *GetOptions() override { return &m_options; }

  class CommandOptions : public Options {
  public:
    CommandOptions() : Options(), m_global(false) {}

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'f':
        m_force = true;
        break;
      case 'g':
        m_global = true;
        break;
      default:
        error.SetErrorStringWithFormat("unrecognized options '%c'",
                                       short_option);
        break;
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_global = false;
      m_force = false;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_settings_set_options);
    }

    // Instance variables to hold the values for command options.
    bool m_global;
    bool m_force;
  };

  int HandleArgumentCompletion(
      CompletionRequest &request,
      OptionElementVector &opt_element_vector) override {

    const size_t argc = request.GetParsedLine().GetArgumentCount();
    const char *arg = nullptr;
    int setting_var_idx;
    for (setting_var_idx = 0; setting_var_idx < static_cast<int>(argc);
         ++setting_var_idx) {
      arg = request.GetParsedLine().GetArgumentAtIndex(setting_var_idx);
      if (arg && arg[0] != '-')
        break; // We found our setting variable name index
    }
    if (request.GetCursorIndex() == setting_var_idx) {
      // Attempting to complete setting variable name
      CommandCompletions::InvokeCommonCompletionCallbacks(
          GetCommandInterpreter(), CommandCompletions::eSettingsNameCompletion,
          request, nullptr);
    } else {
      arg =
          request.GetParsedLine().GetArgumentAtIndex(request.GetCursorIndex());

      if (arg) {
        if (arg[0] == '-') {
          // Complete option name
        } else {
          // Complete setting value
          const char *setting_var_name =
              request.GetParsedLine().GetArgumentAtIndex(setting_var_idx);
          Status error;
          lldb::OptionValueSP value_sp(
              m_interpreter.GetDebugger().GetPropertyValue(
                  &m_exe_ctx, setting_var_name, false, error));
          if (value_sp) {
            value_sp->AutoComplete(m_interpreter, request);
          }
        }
      }
    }
    return request.GetNumberOfMatches();
  }

protected:
  bool DoExecute(llvm::StringRef command,
                 CommandReturnObject &result) override {
    Args cmd_args(command);

    // Process possible options.
    if (!ParseOptions(cmd_args, result))
      return false;

    const size_t min_argc = m_options.m_force ? 1 : 2;
    const size_t argc = cmd_args.GetArgumentCount();

    if ((argc < min_argc) && (!m_options.m_global)) {
      result.AppendError("'settings set' takes more arguments");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    const char *var_name = cmd_args.GetArgumentAtIndex(0);
    if ((var_name == nullptr) || (var_name[0] == '\0')) {
      result.AppendError(
          "'settings set' command requires a valid variable name");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    // A missing value corresponds to clearing the setting when "force" is
    // specified.
    if (argc == 1 && m_options.m_force) {
      Status error(m_interpreter.GetDebugger().SetPropertyValue(
          &m_exe_ctx, eVarSetOperationClear, var_name, llvm::StringRef()));
      if (error.Fail()) {
        result.AppendError(error.AsCString());
        result.SetStatus(eReturnStatusFailed);
        return false;
      }
      return result.Succeeded();
    }

    // Split the raw command into var_name and value pair.
    llvm::StringRef raw_str(command);
    std::string var_value_string = raw_str.split(var_name).second.str();
    const char *var_value_cstr =
        Args::StripSpaces(var_value_string, true, false, false);

    Status error;
    if (m_options.m_global) {
      error = m_interpreter.GetDebugger().SetPropertyValue(
          nullptr, eVarSetOperationAssign, var_name, var_value_cstr);
    }

    if (error.Success()) {
      // FIXME this is the same issue as the one in commands script import
      // we could be setting target.load-script-from-symbol-file which would
      // cause Python scripts to be loaded, which could run LLDB commands (e.g.
      // settings set target.process.python-os-plugin-path) and cause a crash
      // if we did not clear the command's exe_ctx first
      ExecutionContext exe_ctx(m_exe_ctx);
      m_exe_ctx.Clear();
      error = m_interpreter.GetDebugger().SetPropertyValue(
          &exe_ctx, eVarSetOperationAssign, var_name, var_value_cstr);
    }

    if (error.Fail()) {
      result.AppendError(error.AsCString());
      result.SetStatus(eReturnStatusFailed);
      return false;
    } else {
      result.SetStatus(eReturnStatusSuccessFinishResult);
    }

    return result.Succeeded();
  }

private:
  CommandOptions m_options;
};

//-------------------------------------------------------------------------
// CommandObjectSettingsShow -- Show current values
//-------------------------------------------------------------------------

class CommandObjectSettingsShow : public CommandObjectParsed {
public:
  CommandObjectSettingsShow(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "settings show",
                            "Show matching debugger settings and their current "
                            "values.  Defaults to showing all settings.",
                            nullptr) {
    CommandArgumentEntry arg1;
    CommandArgumentData var_name_arg;

    // Define the first (and only) variant of this arg.
    var_name_arg.arg_type = eArgTypeSettingVariableName;
    var_name_arg.arg_repetition = eArgRepeatOptional;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg1.push_back(var_name_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg1);
  }

  ~CommandObjectSettingsShow() override = default;

  int HandleArgumentCompletion(
      CompletionRequest &request,
      OptionElementVector &opt_element_vector) override {
    CommandCompletions::InvokeCommonCompletionCallbacks(
        GetCommandInterpreter(), CommandCompletions::eSettingsNameCompletion,
        request, nullptr);
    return request.GetNumberOfMatches();
  }

protected:
  bool DoExecute(Args &args, CommandReturnObject &result) override {
    result.SetStatus(eReturnStatusSuccessFinishResult);

    if (!args.empty()) {
      for (const auto &arg : args) {
        Status error(m_interpreter.GetDebugger().DumpPropertyValue(
            &m_exe_ctx, result.GetOutputStream(), arg.ref,
            OptionValue::eDumpGroupValue));
        if (error.Success()) {
          result.GetOutputStream().EOL();
        } else {
          result.AppendError(error.AsCString());
          result.SetStatus(eReturnStatusFailed);
        }
      }
    } else {
      m_interpreter.GetDebugger().DumpAllPropertyValues(
          &m_exe_ctx, result.GetOutputStream(), OptionValue::eDumpGroupValue);
    }

    return result.Succeeded();
  }
};

//-------------------------------------------------------------------------
// CommandObjectSettingsWrite -- Write settings to file
//-------------------------------------------------------------------------

static constexpr OptionDefinition g_settings_write_options[] = {
    // clang-format off
  { LLDB_OPT_SET_ALL, true,  "file",  'f', OptionParser::eRequiredArgument, nullptr, {}, CommandCompletions::eDiskFileCompletion, eArgTypeFilename,    "The file into which to write the settings." },
  { LLDB_OPT_SET_ALL, false, "append",'a', OptionParser::eNoArgument,       nullptr, {}, 0,                                       eArgTypeNone,        "Append to saved settings file if it exists."},
    // clang-format on
};

class CommandObjectSettingsWrite : public CommandObjectParsed {
public:
  CommandObjectSettingsWrite(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "settings export",
            "Write matching debugger settings and their "
            "current values to a file that can be read in with "
            "\"settings read\". Defaults to writing all settings.",
            nullptr),
        m_options() {
    CommandArgumentEntry arg1;
    CommandArgumentData var_name_arg;

    // Define the first (and only) variant of this arg.
    var_name_arg.arg_type = eArgTypeSettingVariableName;
    var_name_arg.arg_repetition = eArgRepeatOptional;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg1.push_back(var_name_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg1);
  }

  ~CommandObjectSettingsWrite() override = default;

  Options *GetOptions() override { return &m_options; }

  class CommandOptions : public Options {
  public:
    CommandOptions() : Options() {}

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'f':
        m_filename.assign(option_arg);
        break;
      case 'a':
        m_append = true;
        break;
      default:
        error.SetErrorStringWithFormat("unrecognized option '%c'",
                                       short_option);
        break;
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_filename.clear();
      m_append = false;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_settings_write_options);
    }

    // Instance variables to hold the values for command options.
    std::string m_filename;
    bool m_append = false;
  };

protected:
  bool DoExecute(Args &args, CommandReturnObject &result) override {
    FileSpec file_spec(m_options.m_filename);
    FileSystem::Instance().Resolve(file_spec);
    std::string path(file_spec.GetPath());
    uint32_t options = File::OpenOptions::eOpenOptionWrite |
                       File::OpenOptions::eOpenOptionCanCreate;
    if (m_options.m_append)
      options |= File::OpenOptions::eOpenOptionAppend;
    else
      options |= File::OpenOptions::eOpenOptionTruncate;

    StreamFile out_file(path.c_str(), options,
                        lldb::eFilePermissionsFileDefault);

    if (!out_file.GetFile().IsValid()) {
      result.AppendErrorWithFormat("%s: unable to write to file", path.c_str());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    // Exporting should not be context sensitive.
    ExecutionContext clean_ctx;

    if (args.empty()) {
      m_interpreter.GetDebugger().DumpAllPropertyValues(
          &clean_ctx, out_file, OptionValue::eDumpGroupExport);
      return result.Succeeded();
    }

    for (const auto &arg : args) {
      Status error(m_interpreter.GetDebugger().DumpPropertyValue(
          &clean_ctx, out_file, arg.ref, OptionValue::eDumpGroupExport));
      if (!error.Success()) {
        result.AppendError(error.AsCString());
        result.SetStatus(eReturnStatusFailed);
      }
    }

    return result.Succeeded();
  }

private:
  CommandOptions m_options;
};

//-------------------------------------------------------------------------
// CommandObjectSettingsRead -- Read settings from file
//-------------------------------------------------------------------------

static constexpr OptionDefinition g_settings_read_options[] = {
    // clang-format off
  {LLDB_OPT_SET_ALL, true, "file",'f', OptionParser::eRequiredArgument, nullptr, {}, CommandCompletions::eDiskFileCompletion, eArgTypeFilename,       "The file from which to read the breakpoints." },
    // clang-format on
};

class CommandObjectSettingsRead : public CommandObjectParsed {
public:
  CommandObjectSettingsRead(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "settings read",
            "Read settings previously saved to a file with \"settings write\".",
            nullptr),
        m_options() {}

  ~CommandObjectSettingsRead() override = default;

  Options *GetOptions() override { return &m_options; }

  class CommandOptions : public Options {
  public:
    CommandOptions() : Options() {}

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'f':
        m_filename.assign(option_arg);
        break;
      default:
        error.SetErrorStringWithFormat("unrecognized option '%c'",
                                       short_option);
        break;
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_filename.clear();
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_settings_read_options);
    }

    // Instance variables to hold the values for command options.
    std::string m_filename;
  };

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    FileSpec file(m_options.m_filename);
    FileSystem::Instance().Resolve(file);
    ExecutionContext clean_ctx;
    CommandInterpreterRunOptions options;
    options.SetAddToHistory(false);
    options.SetEchoCommands(false);
    options.SetPrintResults(true);
    options.SetStopOnError(false);
    m_interpreter.HandleCommandsFromFile(file, &clean_ctx, options, result);
    return result.Succeeded();
  }

private:
  CommandOptions m_options;
};

//-------------------------------------------------------------------------
// CommandObjectSettingsList -- List settable variables
//-------------------------------------------------------------------------

class CommandObjectSettingsList : public CommandObjectParsed {
public:
  CommandObjectSettingsList(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "settings list",
                            "List and describe matching debugger settings.  "
                            "Defaults to all listing all settings.",
                            nullptr) {
    CommandArgumentEntry arg;
    CommandArgumentData var_name_arg;
    CommandArgumentData prefix_name_arg;

    // Define the first variant of this arg.
    var_name_arg.arg_type = eArgTypeSettingVariableName;
    var_name_arg.arg_repetition = eArgRepeatOptional;

    // Define the second variant of this arg.
    prefix_name_arg.arg_type = eArgTypeSettingPrefix;
    prefix_name_arg.arg_repetition = eArgRepeatOptional;

    arg.push_back(var_name_arg);
    arg.push_back(prefix_name_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg);
  }

  ~CommandObjectSettingsList() override = default;

  int HandleArgumentCompletion(
      CompletionRequest &request,
      OptionElementVector &opt_element_vector) override {
    CommandCompletions::InvokeCommonCompletionCallbacks(
        GetCommandInterpreter(), CommandCompletions::eSettingsNameCompletion,
        request, nullptr);
    return request.GetNumberOfMatches();
  }

protected:
  bool DoExecute(Args &args, CommandReturnObject &result) override {
    result.SetStatus(eReturnStatusSuccessFinishResult);

    const bool will_modify = false;
    const size_t argc = args.GetArgumentCount();
    if (argc > 0) {
      const bool dump_qualified_name = true;

      // TODO: Convert to StringRef based enumeration.  Requires converting
      // GetPropertyAtPath first.
      for (size_t i = 0; i < argc; ++i) {
        const char *property_path = args.GetArgumentAtIndex(i);

        const Property *property =
            m_interpreter.GetDebugger().GetValueProperties()->GetPropertyAtPath(
                &m_exe_ctx, will_modify, property_path);

        if (property) {
          property->DumpDescription(m_interpreter, result.GetOutputStream(), 0,
                                    dump_qualified_name);
        } else {
          result.AppendErrorWithFormat("invalid property path '%s'",
                                       property_path);
          result.SetStatus(eReturnStatusFailed);
        }
      }
    } else {
      m_interpreter.GetDebugger().DumpAllDescriptions(m_interpreter,
                                                      result.GetOutputStream());
    }

    return result.Succeeded();
  }
};

//-------------------------------------------------------------------------
// CommandObjectSettingsRemove
//-------------------------------------------------------------------------

class CommandObjectSettingsRemove : public CommandObjectRaw {
public:
  CommandObjectSettingsRemove(CommandInterpreter &interpreter)
      : CommandObjectRaw(interpreter, "settings remove",
                         "Remove a value from a setting, specified by array "
                         "index or dictionary key.") {
    CommandArgumentEntry arg1;
    CommandArgumentEntry arg2;
    CommandArgumentData var_name_arg;
    CommandArgumentData index_arg;
    CommandArgumentData key_arg;

    // Define the first (and only) variant of this arg.
    var_name_arg.arg_type = eArgTypeSettingVariableName;
    var_name_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg1.push_back(var_name_arg);

    // Define the first variant of this arg.
    index_arg.arg_type = eArgTypeSettingIndex;
    index_arg.arg_repetition = eArgRepeatPlain;

    // Define the second variant of this arg.
    key_arg.arg_type = eArgTypeSettingKey;
    key_arg.arg_repetition = eArgRepeatPlain;

    // Push both variants into this arg
    arg2.push_back(index_arg);
    arg2.push_back(key_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg1);
    m_arguments.push_back(arg2);
  }

  ~CommandObjectSettingsRemove() override = default;

  int HandleArgumentCompletion(
      CompletionRequest &request,
      OptionElementVector &opt_element_vector) override {
    if (request.GetCursorIndex() < 2)
      CommandCompletions::InvokeCommonCompletionCallbacks(
          GetCommandInterpreter(), CommandCompletions::eSettingsNameCompletion,
          request, nullptr);
    return request.GetNumberOfMatches();
  }

protected:
  bool DoExecute(llvm::StringRef command,
                 CommandReturnObject &result) override {
    result.SetStatus(eReturnStatusSuccessFinishNoResult);

    Args cmd_args(command);

    // Process possible options.
    if (!ParseOptions(cmd_args, result))
      return false;

    const size_t argc = cmd_args.GetArgumentCount();
    if (argc == 0) {
      result.AppendError("'settings set' takes an array or dictionary item, or "
                         "an array followed by one or more indexes, or a "
                         "dictionary followed by one or more key names to "
                         "remove");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    const char *var_name = cmd_args.GetArgumentAtIndex(0);
    if ((var_name == nullptr) || (var_name[0] == '\0')) {
      result.AppendError(
          "'settings set' command requires a valid variable name");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    // Split the raw command into var_name and value pair.
    llvm::StringRef raw_str(command);
    std::string var_value_string = raw_str.split(var_name).second.str();
    const char *var_value_cstr =
        Args::StripSpaces(var_value_string, true, true, false);

    Status error(m_interpreter.GetDebugger().SetPropertyValue(
        &m_exe_ctx, eVarSetOperationRemove, var_name, var_value_cstr));
    if (error.Fail()) {
      result.AppendError(error.AsCString());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    return result.Succeeded();
  }
};

//-------------------------------------------------------------------------
// CommandObjectSettingsReplace
//-------------------------------------------------------------------------

class CommandObjectSettingsReplace : public CommandObjectRaw {
public:
  CommandObjectSettingsReplace(CommandInterpreter &interpreter)
      : CommandObjectRaw(interpreter, "settings replace",
                         "Replace the debugger setting value specified by "
                         "array index or dictionary key.") {
    CommandArgumentEntry arg1;
    CommandArgumentEntry arg2;
    CommandArgumentEntry arg3;
    CommandArgumentData var_name_arg;
    CommandArgumentData index_arg;
    CommandArgumentData key_arg;
    CommandArgumentData value_arg;

    // Define the first (and only) variant of this arg.
    var_name_arg.arg_type = eArgTypeSettingVariableName;
    var_name_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg1.push_back(var_name_arg);

    // Define the first (variant of this arg.
    index_arg.arg_type = eArgTypeSettingIndex;
    index_arg.arg_repetition = eArgRepeatPlain;

    // Define the second (variant of this arg.
    key_arg.arg_type = eArgTypeSettingKey;
    key_arg.arg_repetition = eArgRepeatPlain;

    // Put both variants into this arg
    arg2.push_back(index_arg);
    arg2.push_back(key_arg);

    // Define the first (and only) variant of this arg.
    value_arg.arg_type = eArgTypeValue;
    value_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg3.push_back(value_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg1);
    m_arguments.push_back(arg2);
    m_arguments.push_back(arg3);
  }

  ~CommandObjectSettingsReplace() override = default;

  // Overrides base class's behavior where WantsCompletion =
  // !WantsRawCommandString.
  bool WantsCompletion() override { return true; }

  int HandleArgumentCompletion(
      CompletionRequest &request,
      OptionElementVector &opt_element_vector) override {
    // Attempting to complete variable name
    if (request.GetCursorIndex() < 2)
      CommandCompletions::InvokeCommonCompletionCallbacks(
          GetCommandInterpreter(), CommandCompletions::eSettingsNameCompletion,
          request, nullptr);

    return request.GetNumberOfMatches();
  }

protected:
  bool DoExecute(llvm::StringRef command,
                 CommandReturnObject &result) override {
    result.SetStatus(eReturnStatusSuccessFinishNoResult);

    Args cmd_args(command);
    const char *var_name = cmd_args.GetArgumentAtIndex(0);
    if ((var_name == nullptr) || (var_name[0] == '\0')) {
      result.AppendError("'settings replace' command requires a valid variable "
                         "name; No value supplied");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    // Split the raw command into var_name, index_value, and value triple.
    llvm::StringRef raw_str(command);
    std::string var_value_string = raw_str.split(var_name).second.str();
    const char *var_value_cstr =
        Args::StripSpaces(var_value_string, true, true, false);

    Status error(m_interpreter.GetDebugger().SetPropertyValue(
        &m_exe_ctx, eVarSetOperationReplace, var_name, var_value_cstr));
    if (error.Fail()) {
      result.AppendError(error.AsCString());
      result.SetStatus(eReturnStatusFailed);
      return false;
    } else {
      result.SetStatus(eReturnStatusSuccessFinishNoResult);
    }

    return result.Succeeded();
  }
};

//-------------------------------------------------------------------------
// CommandObjectSettingsInsertBefore
//-------------------------------------------------------------------------

class CommandObjectSettingsInsertBefore : public CommandObjectRaw {
public:
  CommandObjectSettingsInsertBefore(CommandInterpreter &interpreter)
      : CommandObjectRaw(interpreter, "settings insert-before",
                         "Insert one or more values into an debugger array "
                         "setting immediately before the specified element "
                         "index.") {
    CommandArgumentEntry arg1;
    CommandArgumentEntry arg2;
    CommandArgumentEntry arg3;
    CommandArgumentData var_name_arg;
    CommandArgumentData index_arg;
    CommandArgumentData value_arg;

    // Define the first (and only) variant of this arg.
    var_name_arg.arg_type = eArgTypeSettingVariableName;
    var_name_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg1.push_back(var_name_arg);

    // Define the first (variant of this arg.
    index_arg.arg_type = eArgTypeSettingIndex;
    index_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg2.push_back(index_arg);

    // Define the first (and only) variant of this arg.
    value_arg.arg_type = eArgTypeValue;
    value_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg3.push_back(value_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg1);
    m_arguments.push_back(arg2);
    m_arguments.push_back(arg3);
  }

  ~CommandObjectSettingsInsertBefore() override = default;

  // Overrides base class's behavior where WantsCompletion =
  // !WantsRawCommandString.
  bool WantsCompletion() override { return true; }

  int HandleArgumentCompletion(
      CompletionRequest &request,
      OptionElementVector &opt_element_vector) override {
    // Attempting to complete variable name
    if (request.GetCursorIndex() < 2)
      CommandCompletions::InvokeCommonCompletionCallbacks(
          GetCommandInterpreter(), CommandCompletions::eSettingsNameCompletion,
          request, nullptr);

    return request.GetNumberOfMatches();
  }

protected:
  bool DoExecute(llvm::StringRef command,
                 CommandReturnObject &result) override {
    result.SetStatus(eReturnStatusSuccessFinishNoResult);

    Args cmd_args(command);
    const size_t argc = cmd_args.GetArgumentCount();

    if (argc < 3) {
      result.AppendError("'settings insert-before' takes more arguments");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    const char *var_name = cmd_args.GetArgumentAtIndex(0);
    if ((var_name == nullptr) || (var_name[0] == '\0')) {
      result.AppendError("'settings insert-before' command requires a valid "
                         "variable name; No value supplied");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    // Split the raw command into var_name, index_value, and value triple.
    llvm::StringRef raw_str(command);
    std::string var_value_string = raw_str.split(var_name).second.str();
    const char *var_value_cstr =
        Args::StripSpaces(var_value_string, true, true, false);

    Status error(m_interpreter.GetDebugger().SetPropertyValue(
        &m_exe_ctx, eVarSetOperationInsertBefore, var_name, var_value_cstr));
    if (error.Fail()) {
      result.AppendError(error.AsCString());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    return result.Succeeded();
  }
};

//-------------------------------------------------------------------------
// CommandObjectSettingInsertAfter
//-------------------------------------------------------------------------

class CommandObjectSettingsInsertAfter : public CommandObjectRaw {
public:
  CommandObjectSettingsInsertAfter(CommandInterpreter &interpreter)
      : CommandObjectRaw(interpreter, "settings insert-after",
                         "Insert one or more values into a debugger array "
                         "settings after the specified element index.") {
    CommandArgumentEntry arg1;
    CommandArgumentEntry arg2;
    CommandArgumentEntry arg3;
    CommandArgumentData var_name_arg;
    CommandArgumentData index_arg;
    CommandArgumentData value_arg;

    // Define the first (and only) variant of this arg.
    var_name_arg.arg_type = eArgTypeSettingVariableName;
    var_name_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg1.push_back(var_name_arg);

    // Define the first (variant of this arg.
    index_arg.arg_type = eArgTypeSettingIndex;
    index_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg2.push_back(index_arg);

    // Define the first (and only) variant of this arg.
    value_arg.arg_type = eArgTypeValue;
    value_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg3.push_back(value_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg1);
    m_arguments.push_back(arg2);
    m_arguments.push_back(arg3);
  }

  ~CommandObjectSettingsInsertAfter() override = default;

  // Overrides base class's behavior where WantsCompletion =
  // !WantsRawCommandString.
  bool WantsCompletion() override { return true; }

  int HandleArgumentCompletion(
      CompletionRequest &request,
      OptionElementVector &opt_element_vector) override {
    // Attempting to complete variable name
    if (request.GetCursorIndex() < 2)
      CommandCompletions::InvokeCommonCompletionCallbacks(
          GetCommandInterpreter(), CommandCompletions::eSettingsNameCompletion,
          request, nullptr);

    return request.GetNumberOfMatches();
  }

protected:
  bool DoExecute(llvm::StringRef command,
                 CommandReturnObject &result) override {
    result.SetStatus(eReturnStatusSuccessFinishNoResult);

    Args cmd_args(command);
    const size_t argc = cmd_args.GetArgumentCount();

    if (argc < 3) {
      result.AppendError("'settings insert-after' takes more arguments");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    const char *var_name = cmd_args.GetArgumentAtIndex(0);
    if ((var_name == nullptr) || (var_name[0] == '\0')) {
      result.AppendError("'settings insert-after' command requires a valid "
                         "variable name; No value supplied");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    // Split the raw command into var_name, index_value, and value triple.
    llvm::StringRef raw_str(command);
    std::string var_value_string = raw_str.split(var_name).second.str();
    const char *var_value_cstr =
        Args::StripSpaces(var_value_string, true, true, false);

    Status error(m_interpreter.GetDebugger().SetPropertyValue(
        &m_exe_ctx, eVarSetOperationInsertAfter, var_name, var_value_cstr));
    if (error.Fail()) {
      result.AppendError(error.AsCString());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    return result.Succeeded();
  }
};

//-------------------------------------------------------------------------
// CommandObjectSettingsAppend
//-------------------------------------------------------------------------

class CommandObjectSettingsAppend : public CommandObjectRaw {
public:
  CommandObjectSettingsAppend(CommandInterpreter &interpreter)
      : CommandObjectRaw(interpreter, "settings append",
                         "Append one or more values to a debugger array, "
                         "dictionary, or string setting.") {
    CommandArgumentEntry arg1;
    CommandArgumentEntry arg2;
    CommandArgumentData var_name_arg;
    CommandArgumentData value_arg;

    // Define the first (and only) variant of this arg.
    var_name_arg.arg_type = eArgTypeSettingVariableName;
    var_name_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg1.push_back(var_name_arg);

    // Define the first (and only) variant of this arg.
    value_arg.arg_type = eArgTypeValue;
    value_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg2.push_back(value_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg1);
    m_arguments.push_back(arg2);
  }

  ~CommandObjectSettingsAppend() override = default;

  // Overrides base class's behavior where WantsCompletion =
  // !WantsRawCommandString.
  bool WantsCompletion() override { return true; }

  int HandleArgumentCompletion(
      CompletionRequest &request,
      OptionElementVector &opt_element_vector) override {
    // Attempting to complete variable name
    if (request.GetCursorIndex() < 2)
      CommandCompletions::InvokeCommonCompletionCallbacks(
          GetCommandInterpreter(), CommandCompletions::eSettingsNameCompletion,
          request, nullptr);

    return request.GetNumberOfMatches();
  }

protected:
  bool DoExecute(llvm::StringRef command,
                 CommandReturnObject &result) override {
    result.SetStatus(eReturnStatusSuccessFinishNoResult);
    Args cmd_args(command);
    const size_t argc = cmd_args.GetArgumentCount();

    if (argc < 2) {
      result.AppendError("'settings append' takes more arguments");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    const char *var_name = cmd_args.GetArgumentAtIndex(0);
    if ((var_name == nullptr) || (var_name[0] == '\0')) {
      result.AppendError("'settings append' command requires a valid variable "
                         "name; No value supplied");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    // Do not perform cmd_args.Shift() since StringRef is manipulating the raw
    // character string later on.

    // Split the raw command into var_name and value pair.
    llvm::StringRef raw_str(command);
    std::string var_value_string = raw_str.split(var_name).second.str();
    const char *var_value_cstr =
        Args::StripSpaces(var_value_string, true, true, false);

    Status error(m_interpreter.GetDebugger().SetPropertyValue(
        &m_exe_ctx, eVarSetOperationAppend, var_name, var_value_cstr));
    if (error.Fail()) {
      result.AppendError(error.AsCString());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    return result.Succeeded();
  }
};

//-------------------------------------------------------------------------
// CommandObjectSettingsClear
//-------------------------------------------------------------------------

class CommandObjectSettingsClear : public CommandObjectParsed {
public:
  CommandObjectSettingsClear(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "settings clear",
            "Clear a debugger setting array, dictionary, or string.", nullptr) {
    CommandArgumentEntry arg;
    CommandArgumentData var_name_arg;

    // Define the first (and only) variant of this arg.
    var_name_arg.arg_type = eArgTypeSettingVariableName;
    var_name_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg.push_back(var_name_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg);
  }

  ~CommandObjectSettingsClear() override = default;

  int HandleArgumentCompletion(
      CompletionRequest &request,
      OptionElementVector &opt_element_vector) override {
    // Attempting to complete variable name
    if (request.GetCursorIndex() < 2)
      CommandCompletions::InvokeCommonCompletionCallbacks(
          GetCommandInterpreter(), CommandCompletions::eSettingsNameCompletion,
          request, nullptr);

    return request.GetNumberOfMatches();
  }

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    result.SetStatus(eReturnStatusSuccessFinishNoResult);
    const size_t argc = command.GetArgumentCount();

    if (argc != 1) {
      result.AppendError("'settings clear' takes exactly one argument");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    const char *var_name = command.GetArgumentAtIndex(0);
    if ((var_name == nullptr) || (var_name[0] == '\0')) {
      result.AppendError("'settings clear' command requires a valid variable "
                         "name; No value supplied");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    Status error(m_interpreter.GetDebugger().SetPropertyValue(
        &m_exe_ctx, eVarSetOperationClear, var_name, llvm::StringRef()));
    if (error.Fail()) {
      result.AppendError(error.AsCString());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    return result.Succeeded();
  }
};

//-------------------------------------------------------------------------
// CommandObjectMultiwordSettings
//-------------------------------------------------------------------------

CommandObjectMultiwordSettings::CommandObjectMultiwordSettings(
    CommandInterpreter &interpreter)
    : CommandObjectMultiword(interpreter, "settings",
                             "Commands for managing LLDB settings.",
                             "settings <subcommand> [<command-options>]") {
  LoadSubCommand("set",
                 CommandObjectSP(new CommandObjectSettingsSet(interpreter)));
  LoadSubCommand("show",
                 CommandObjectSP(new CommandObjectSettingsShow(interpreter)));
  LoadSubCommand("list",
                 CommandObjectSP(new CommandObjectSettingsList(interpreter)));
  LoadSubCommand("remove",
                 CommandObjectSP(new CommandObjectSettingsRemove(interpreter)));
  LoadSubCommand("replace", CommandObjectSP(
                                new CommandObjectSettingsReplace(interpreter)));
  LoadSubCommand(
      "insert-before",
      CommandObjectSP(new CommandObjectSettingsInsertBefore(interpreter)));
  LoadSubCommand(
      "insert-after",
      CommandObjectSP(new CommandObjectSettingsInsertAfter(interpreter)));
  LoadSubCommand("append",
                 CommandObjectSP(new CommandObjectSettingsAppend(interpreter)));
  LoadSubCommand("clear",
                 CommandObjectSP(new CommandObjectSettingsClear(interpreter)));
  LoadSubCommand("write",
                 CommandObjectSP(new CommandObjectSettingsWrite(interpreter)));
  LoadSubCommand("read",
                 CommandObjectSP(new CommandObjectSettingsRead(interpreter)));
}

CommandObjectMultiwordSettings::~CommandObjectMultiwordSettings() = default;
