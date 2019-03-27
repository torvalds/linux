//===-- CommandObjectType.cpp -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "CommandObjectType.h"

#include <algorithm>
#include <cctype>
#include <functional>

#include "lldb/Core/Debugger.h"
#include "lldb/Core/IOHandler.h"
#include "lldb/DataFormatters/DataVisualization.h"
#include "lldb/Host/OptionParser.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandObject.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Interpreter/OptionGroupFormat.h"
#include "lldb/Interpreter/OptionValueBoolean.h"
#include "lldb/Interpreter/OptionValueLanguage.h"
#include "lldb/Interpreter/OptionValueString.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Target/Language.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadList.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/Utility/State.h"
#include "lldb/Utility/StringList.h"

#include "llvm/ADT/STLExtras.h"

using namespace lldb;
using namespace lldb_private;

class ScriptAddOptions {
public:
  TypeSummaryImpl::Flags m_flags;
  StringList m_target_types;
  bool m_regex;
  ConstString m_name;
  std::string m_category;

  ScriptAddOptions(const TypeSummaryImpl::Flags &flags, bool regx,
                   const ConstString &name, std::string catg)
      : m_flags(flags), m_regex(regx), m_name(name), m_category(catg) {}

  typedef std::shared_ptr<ScriptAddOptions> SharedPointer;
};

class SynthAddOptions {
public:
  bool m_skip_pointers;
  bool m_skip_references;
  bool m_cascade;
  bool m_regex;
  StringList m_target_types;
  std::string m_category;

  SynthAddOptions(bool sptr, bool sref, bool casc, bool regx, std::string catg)
      : m_skip_pointers(sptr), m_skip_references(sref), m_cascade(casc),
        m_regex(regx), m_target_types(), m_category(catg) {}

  typedef std::shared_ptr<SynthAddOptions> SharedPointer;
};

static bool WarnOnPotentialUnquotedUnsignedType(Args &command,
                                                CommandReturnObject &result) {
  if (command.empty())
    return false;

  for (auto entry : llvm::enumerate(command.entries().drop_back())) {
    if (entry.value().ref != "unsigned")
      continue;
    auto next = command.entries()[entry.index() + 1].ref;
    if (next == "int" || next == "short" || next == "char" || next == "long") {
      result.AppendWarningWithFormat(
          "unsigned %s being treated as two types. if you meant the combined "
          "type "
          "name use  quotes, as in \"unsigned %s\"\n",
          next.str().c_str(), next.str().c_str());
      return true;
    }
  }
  return false;
}

static constexpr OptionDefinition g_type_summary_add_options[] = {
    // clang-format off
  { LLDB_OPT_SET_ALL,                false, "category",        'w', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeName,           "Add this to the given category instead of the default one." },
  { LLDB_OPT_SET_ALL,                false, "cascade",         'C', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeBoolean,        "If true, cascade through typedef chains." },
  { LLDB_OPT_SET_ALL,                false, "no-value",        'v', OptionParser::eNoArgument,       nullptr, {}, 0, eArgTypeNone,           "Don't show the value, just show the summary, for this type." },
  { LLDB_OPT_SET_ALL,                false, "skip-pointers",   'p', OptionParser::eNoArgument,       nullptr, {}, 0, eArgTypeNone,           "Don't use this format for pointers-to-type objects." },
  { LLDB_OPT_SET_ALL,                false, "skip-references", 'r', OptionParser::eNoArgument,       nullptr, {}, 0, eArgTypeNone,           "Don't use this format for references-to-type objects." },
  { LLDB_OPT_SET_ALL,                false, "regex",           'x', OptionParser::eNoArgument,       nullptr, {}, 0, eArgTypeNone,           "Type names are actually regular expressions." },
  { LLDB_OPT_SET_1,                  true,  "inline-children", 'c', OptionParser::eNoArgument,       nullptr, {}, 0, eArgTypeNone,           "If true, inline all child values into summary string." },
  { LLDB_OPT_SET_1,                  false, "omit-names",      'O', OptionParser::eNoArgument,       nullptr, {}, 0, eArgTypeNone,           "If true, omit value names in the summary display." },
  { LLDB_OPT_SET_2,                  true,  "summary-string",  's', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeSummaryString,  "Summary string used to display text and object contents." },
  { LLDB_OPT_SET_3,                  false, "python-script",   'o', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypePythonScript,   "Give a one-liner Python script as part of the command." },
  { LLDB_OPT_SET_3,                  false, "python-function", 'F', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypePythonFunction, "Give the name of a Python function to use for this type." },
  { LLDB_OPT_SET_3,                  false, "input-python",    'P', OptionParser::eNoArgument,       nullptr, {}, 0, eArgTypeNone,           "Input Python code to use for this type manually." },
  { LLDB_OPT_SET_2 | LLDB_OPT_SET_3, false, "expand",          'e', OptionParser::eNoArgument,       nullptr, {}, 0, eArgTypeNone,           "Expand aggregate data types to show children on separate lines." },
  { LLDB_OPT_SET_2 | LLDB_OPT_SET_3, false, "hide-empty",      'h', OptionParser::eNoArgument,       nullptr, {}, 0, eArgTypeNone,           "Do not expand aggregate data types with no children." },
  { LLDB_OPT_SET_2 | LLDB_OPT_SET_3, false, "name",            'n', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeName,           "A name for this summary string." }
    // clang-format on
};

class CommandObjectTypeSummaryAdd : public CommandObjectParsed,
                                    public IOHandlerDelegateMultiline {
private:
  class CommandOptions : public Options {
  public:
    CommandOptions(CommandInterpreter &interpreter) : Options() {}

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override;

    void OptionParsingStarting(ExecutionContext *execution_context) override;

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_type_summary_add_options);
    }

    // Instance variables to hold the values for command options.

    TypeSummaryImpl::Flags m_flags;
    bool m_regex;
    std::string m_format_string;
    ConstString m_name;
    std::string m_python_script;
    std::string m_python_function;
    bool m_is_add_script;
    std::string m_category;
  };

  CommandOptions m_options;

  Options *GetOptions() override { return &m_options; }

  bool Execute_ScriptSummary(Args &command, CommandReturnObject &result);

  bool Execute_StringSummary(Args &command, CommandReturnObject &result);

public:
  enum SummaryFormatType { eRegularSummary, eRegexSummary, eNamedSummary };

  CommandObjectTypeSummaryAdd(CommandInterpreter &interpreter);

  ~CommandObjectTypeSummaryAdd() override = default;

  void IOHandlerActivated(IOHandler &io_handler) override {
    static const char *g_summary_addreader_instructions =
        "Enter your Python command(s). Type 'DONE' to end.\n"
        "def function (valobj,internal_dict):\n"
        "     \"\"\"valobj: an SBValue which you want to provide a summary "
        "for\n"
        "        internal_dict: an LLDB support object not to be used\"\"\"\n";

    StreamFileSP output_sp(io_handler.GetOutputStreamFile());
    if (output_sp) {
      output_sp->PutCString(g_summary_addreader_instructions);
      output_sp->Flush();
    }
  }

  void IOHandlerInputComplete(IOHandler &io_handler,
                              std::string &data) override {
    StreamFileSP error_sp = io_handler.GetErrorStreamFile();

#ifndef LLDB_DISABLE_PYTHON
    ScriptInterpreter *interpreter = m_interpreter.GetScriptInterpreter();
    if (interpreter) {
      StringList lines;
      lines.SplitIntoLines(data);
      if (lines.GetSize() > 0) {
        ScriptAddOptions *options_ptr =
            ((ScriptAddOptions *)io_handler.GetUserData());
        if (options_ptr) {
          ScriptAddOptions::SharedPointer options(
              options_ptr); // this will ensure that we get rid of the pointer
                            // when going out of scope

          ScriptInterpreter *interpreter = m_interpreter.GetScriptInterpreter();
          if (interpreter) {
            std::string funct_name_str;
            if (interpreter->GenerateTypeScriptFunction(lines,
                                                        funct_name_str)) {
              if (funct_name_str.empty()) {
                error_sp->Printf("unable to obtain a valid function name from "
                                 "the script interpreter.\n");
                error_sp->Flush();
              } else {
                // now I have a valid function name, let's add this as script
                // for every type in the list

                TypeSummaryImplSP script_format;
                script_format.reset(new ScriptSummaryFormat(
                    options->m_flags, funct_name_str.c_str(),
                    lines.CopyList("    ").c_str()));

                Status error;

                for (size_t i = 0; i < options->m_target_types.GetSize(); i++) {
                  const char *type_name =
                      options->m_target_types.GetStringAtIndex(i);
                  CommandObjectTypeSummaryAdd::AddSummary(
                      ConstString(type_name), script_format,
                      (options->m_regex
                           ? CommandObjectTypeSummaryAdd::eRegexSummary
                           : CommandObjectTypeSummaryAdd::eRegularSummary),
                      options->m_category, &error);
                  if (error.Fail()) {
                    error_sp->Printf("error: %s", error.AsCString());
                    error_sp->Flush();
                  }
                }

                if (options->m_name) {
                  CommandObjectTypeSummaryAdd::AddSummary(
                      options->m_name, script_format,
                      CommandObjectTypeSummaryAdd::eNamedSummary,
                      options->m_category, &error);
                  if (error.Fail()) {
                    CommandObjectTypeSummaryAdd::AddSummary(
                        options->m_name, script_format,
                        CommandObjectTypeSummaryAdd::eNamedSummary,
                        options->m_category, &error);
                    if (error.Fail()) {
                      error_sp->Printf("error: %s", error.AsCString());
                      error_sp->Flush();
                    }
                  } else {
                    error_sp->Printf("error: %s", error.AsCString());
                    error_sp->Flush();
                  }
                } else {
                  if (error.AsCString()) {
                    error_sp->Printf("error: %s", error.AsCString());
                    error_sp->Flush();
                  }
                }
              }
            } else {
              error_sp->Printf("error: unable to generate a function.\n");
              error_sp->Flush();
            }
          } else {
            error_sp->Printf("error: no script interpreter.\n");
            error_sp->Flush();
          }
        } else {
          error_sp->Printf("error: internal synchronization information "
                           "missing or invalid.\n");
          error_sp->Flush();
        }
      } else {
        error_sp->Printf("error: empty function, didn't add python command.\n");
        error_sp->Flush();
      }
    } else {
      error_sp->Printf(
          "error: script interpreter missing, didn't add python command.\n");
      error_sp->Flush();
    }
#endif // LLDB_DISABLE_PYTHON
    io_handler.SetIsDone(true);
  }

  static bool AddSummary(ConstString type_name, lldb::TypeSummaryImplSP entry,
                         SummaryFormatType type, std::string category,
                         Status *error = nullptr);

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override;
};

static const char *g_synth_addreader_instructions =
    "Enter your Python command(s). Type 'DONE' to end.\n"
    "You must define a Python class with these methods:\n"
    "    def __init__(self, valobj, dict):\n"
    "    def num_children(self):\n"
    "    def get_child_at_index(self, index):\n"
    "    def get_child_index(self, name):\n"
    "    def update(self):\n"
    "        '''Optional'''\n"
    "class synthProvider:\n";

static constexpr OptionDefinition g_type_synth_add_options[] = {
    // clang-format off
  { LLDB_OPT_SET_ALL, false, "cascade",         'C', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeBoolean,     "If true, cascade through typedef chains." },
  { LLDB_OPT_SET_ALL, false, "skip-pointers",   'p', OptionParser::eNoArgument,       nullptr, {}, 0, eArgTypeNone,        "Don't use this format for pointers-to-type objects." },
  { LLDB_OPT_SET_ALL, false, "skip-references", 'r', OptionParser::eNoArgument,       nullptr, {}, 0, eArgTypeNone,        "Don't use this format for references-to-type objects." },
  { LLDB_OPT_SET_ALL, false, "category",        'w', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeName,        "Add this to the given category instead of the default one." },
  { LLDB_OPT_SET_2,   false, "python-class",    'l', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypePythonClass, "Use this Python class to produce synthetic children." },
  { LLDB_OPT_SET_3,   false, "input-python",    'P', OptionParser::eNoArgument,       nullptr, {}, 0, eArgTypeNone,        "Type Python code to generate a class that provides synthetic children." },
  { LLDB_OPT_SET_ALL, false, "regex",           'x', OptionParser::eNoArgument,       nullptr, {}, 0, eArgTypeNone,        "Type names are actually regular expressions." }
    // clang-format on
};

class CommandObjectTypeSynthAdd : public CommandObjectParsed,
                                  public IOHandlerDelegateMultiline {
private:
  class CommandOptions : public Options {
  public:
    CommandOptions() : Options() {}

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;
      bool success;

      switch (short_option) {
      case 'C':
        m_cascade = OptionArgParser::ToBoolean(option_arg, true, &success);
        if (!success)
          error.SetErrorStringWithFormat("invalid value for cascade: %s",
                                         option_arg.str().c_str());
        break;
      case 'P':
        handwrite_python = true;
        break;
      case 'l':
        m_class_name = std::string(option_arg);
        is_class_based = true;
        break;
      case 'p':
        m_skip_pointers = true;
        break;
      case 'r':
        m_skip_references = true;
        break;
      case 'w':
        m_category = std::string(option_arg);
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
      m_cascade = true;
      m_class_name = "";
      m_skip_pointers = false;
      m_skip_references = false;
      m_category = "default";
      is_class_based = false;
      handwrite_python = false;
      m_regex = false;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_type_synth_add_options);
    }

    // Instance variables to hold the values for command options.

    bool m_cascade;
    bool m_skip_references;
    bool m_skip_pointers;
    std::string m_class_name;
    bool m_input_python;
    std::string m_category;
    bool is_class_based;
    bool handwrite_python;
    bool m_regex;
  };

  CommandOptions m_options;

  Options *GetOptions() override { return &m_options; }

  bool Execute_HandwritePython(Args &command, CommandReturnObject &result);

  bool Execute_PythonClass(Args &command, CommandReturnObject &result);

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    WarnOnPotentialUnquotedUnsignedType(command, result);

    if (m_options.handwrite_python)
      return Execute_HandwritePython(command, result);
    else if (m_options.is_class_based)
      return Execute_PythonClass(command, result);
    else {
      result.AppendError("must either provide a children list, a Python class "
                         "name, or use -P and type a Python class "
                         "line-by-line");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }
  }

  void IOHandlerActivated(IOHandler &io_handler) override {
    StreamFileSP output_sp(io_handler.GetOutputStreamFile());
    if (output_sp) {
      output_sp->PutCString(g_synth_addreader_instructions);
      output_sp->Flush();
    }
  }

  void IOHandlerInputComplete(IOHandler &io_handler,
                              std::string &data) override {
    StreamFileSP error_sp = io_handler.GetErrorStreamFile();

#ifndef LLDB_DISABLE_PYTHON
    ScriptInterpreter *interpreter = m_interpreter.GetScriptInterpreter();
    if (interpreter) {
      StringList lines;
      lines.SplitIntoLines(data);
      if (lines.GetSize() > 0) {
        SynthAddOptions *options_ptr =
            ((SynthAddOptions *)io_handler.GetUserData());
        if (options_ptr) {
          SynthAddOptions::SharedPointer options(
              options_ptr); // this will ensure that we get rid of the pointer
                            // when going out of scope

          ScriptInterpreter *interpreter = m_interpreter.GetScriptInterpreter();
          if (interpreter) {
            std::string class_name_str;
            if (interpreter->GenerateTypeSynthClass(lines, class_name_str)) {
              if (class_name_str.empty()) {
                error_sp->Printf(
                    "error: unable to obtain a proper name for the class.\n");
                error_sp->Flush();
              } else {
                // everything should be fine now, let's add the synth provider
                // class

                SyntheticChildrenSP synth_provider;
                synth_provider.reset(new ScriptedSyntheticChildren(
                    SyntheticChildren::Flags()
                        .SetCascades(options->m_cascade)
                        .SetSkipPointers(options->m_skip_pointers)
                        .SetSkipReferences(options->m_skip_references),
                    class_name_str.c_str()));

                lldb::TypeCategoryImplSP category;
                DataVisualization::Categories::GetCategory(
                    ConstString(options->m_category.c_str()), category);

                Status error;

                for (size_t i = 0; i < options->m_target_types.GetSize(); i++) {
                  const char *type_name =
                      options->m_target_types.GetStringAtIndex(i);
                  ConstString const_type_name(type_name);
                  if (const_type_name) {
                    if (!CommandObjectTypeSynthAdd::AddSynth(
                            const_type_name, synth_provider,
                            options->m_regex
                                ? CommandObjectTypeSynthAdd::eRegexSynth
                                : CommandObjectTypeSynthAdd::eRegularSynth,
                            options->m_category, &error)) {
                      error_sp->Printf("error: %s\n", error.AsCString());
                      error_sp->Flush();
                      break;
                    }
                  } else {
                    error_sp->Printf("error: invalid type name.\n");
                    error_sp->Flush();
                    break;
                  }
                }
              }
            } else {
              error_sp->Printf("error: unable to generate a class.\n");
              error_sp->Flush();
            }
          } else {
            error_sp->Printf("error: no script interpreter.\n");
            error_sp->Flush();
          }
        } else {
          error_sp->Printf("error: internal synchronization data missing.\n");
          error_sp->Flush();
        }
      } else {
        error_sp->Printf("error: empty function, didn't add python command.\n");
        error_sp->Flush();
      }
    } else {
      error_sp->Printf(
          "error: script interpreter missing, didn't add python command.\n");
      error_sp->Flush();
    }

#endif // LLDB_DISABLE_PYTHON
    io_handler.SetIsDone(true);
  }

public:
  enum SynthFormatType { eRegularSynth, eRegexSynth };

  CommandObjectTypeSynthAdd(CommandInterpreter &interpreter);

  ~CommandObjectTypeSynthAdd() override = default;

  static bool AddSynth(ConstString type_name, lldb::SyntheticChildrenSP entry,
                       SynthFormatType type, std::string category_name,
                       Status *error);
};

//-------------------------------------------------------------------------
// CommandObjectTypeFormatAdd
//-------------------------------------------------------------------------

static constexpr OptionDefinition g_type_format_add_options[] = {
    // clang-format off
  { LLDB_OPT_SET_ALL, false, "category",        'w', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeName,    "Add this to the given category instead of the default one." },
  { LLDB_OPT_SET_ALL, false, "cascade",         'C', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeBoolean, "If true, cascade through typedef chains." },
  { LLDB_OPT_SET_ALL, false, "skip-pointers",   'p', OptionParser::eNoArgument,       nullptr, {}, 0, eArgTypeNone,    "Don't use this format for pointers-to-type objects." },
  { LLDB_OPT_SET_ALL, false, "skip-references", 'r', OptionParser::eNoArgument,       nullptr, {}, 0, eArgTypeNone,    "Don't use this format for references-to-type objects." },
  { LLDB_OPT_SET_ALL, false, "regex",           'x', OptionParser::eNoArgument,       nullptr, {}, 0, eArgTypeNone,    "Type names are actually regular expressions." },
  { LLDB_OPT_SET_2,   false, "type",            't', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeName,    "Format variables as if they were of this type." }
    // clang-format on
};

class CommandObjectTypeFormatAdd : public CommandObjectParsed {
private:
  class CommandOptions : public OptionGroup {
  public:
    CommandOptions() : OptionGroup() {}

    ~CommandOptions() override = default;

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_type_format_add_options);
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_cascade = true;
      m_skip_pointers = false;
      m_skip_references = false;
      m_regex = false;
      m_category.assign("default");
      m_custom_type_name.clear();
    }

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_value,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option =
          g_type_format_add_options[option_idx].short_option;
      bool success;

      switch (short_option) {
      case 'C':
        m_cascade = OptionArgParser::ToBoolean(option_value, true, &success);
        if (!success)
          error.SetErrorStringWithFormat("invalid value for cascade: %s",
                                         option_value.str().c_str());
        break;
      case 'p':
        m_skip_pointers = true;
        break;
      case 'w':
        m_category.assign(option_value);
        break;
      case 'r':
        m_skip_references = true;
        break;
      case 'x':
        m_regex = true;
        break;
      case 't':
        m_custom_type_name.assign(option_value);
        break;
      default:
        error.SetErrorStringWithFormat("unrecognized option '%c'",
                                       short_option);
        break;
      }

      return error;
    }

    // Instance variables to hold the values for command options.

    bool m_cascade;
    bool m_skip_references;
    bool m_skip_pointers;
    bool m_regex;
    std::string m_category;
    std::string m_custom_type_name;
  };

  OptionGroupOptions m_option_group;
  OptionGroupFormat m_format_options;
  CommandOptions m_command_options;

  Options *GetOptions() override { return &m_option_group; }

public:
  CommandObjectTypeFormatAdd(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "type format add",
                            "Add a new formatting style for a type.", nullptr),
        m_option_group(), m_format_options(eFormatInvalid),
        m_command_options() {
    CommandArgumentEntry type_arg;
    CommandArgumentData type_style_arg;

    type_style_arg.arg_type = eArgTypeName;
    type_style_arg.arg_repetition = eArgRepeatPlus;

    type_arg.push_back(type_style_arg);

    m_arguments.push_back(type_arg);

    SetHelpLong(
        R"(
The following examples of 'type format add' refer to this code snippet for context:

    typedef int Aint;
    typedef float Afloat;
    typedef Aint Bint;
    typedef Afloat Bfloat;

    Aint ix = 5;
    Bint iy = 5;

    Afloat fx = 3.14;
    BFloat fy = 3.14;

Adding default formatting:

(lldb) type format add -f hex AInt
(lldb) frame variable iy

)"
        "    Produces hexadecimal display of iy, because no formatter is available for Bint and \
the one for Aint is used instead."
        R"(

To prevent this use the cascade option '-C no' to prevent evaluation of typedef chains:


(lldb) type format add -f hex -C no AInt

Similar reasoning applies to this:

(lldb) type format add -f hex -C no float -p

)"
        "    All float values and float references are now formatted as hexadecimal, but not \
pointers to floats.  Nor will it change the default display for Afloat and Bfloat objects.");

    // Add the "--format" to all options groups
    m_option_group.Append(&m_format_options,
                          OptionGroupFormat::OPTION_GROUP_FORMAT,
                          LLDB_OPT_SET_1);
    m_option_group.Append(&m_command_options);
    m_option_group.Finalize();
  }

  ~CommandObjectTypeFormatAdd() override = default;

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();

    if (argc < 1) {
      result.AppendErrorWithFormat("%s takes one or more args.\n",
                                   m_cmd_name.c_str());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    const Format format = m_format_options.GetFormat();
    if (format == eFormatInvalid &&
        m_command_options.m_custom_type_name.empty()) {
      result.AppendErrorWithFormat("%s needs a valid format.\n",
                                   m_cmd_name.c_str());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    TypeFormatImplSP entry;

    if (m_command_options.m_custom_type_name.empty())
      entry.reset(new TypeFormatImpl_Format(
          format, TypeFormatImpl::Flags()
                      .SetCascades(m_command_options.m_cascade)
                      .SetSkipPointers(m_command_options.m_skip_pointers)
                      .SetSkipReferences(m_command_options.m_skip_references)));
    else
      entry.reset(new TypeFormatImpl_EnumType(
          ConstString(m_command_options.m_custom_type_name.c_str()),
          TypeFormatImpl::Flags()
              .SetCascades(m_command_options.m_cascade)
              .SetSkipPointers(m_command_options.m_skip_pointers)
              .SetSkipReferences(m_command_options.m_skip_references)));

    // now I have a valid format, let's add it to every type

    TypeCategoryImplSP category_sp;
    DataVisualization::Categories::GetCategory(
        ConstString(m_command_options.m_category), category_sp);
    if (!category_sp)
      return false;

    WarnOnPotentialUnquotedUnsignedType(command, result);

    for (auto &arg_entry : command.entries()) {
      if (arg_entry.ref.empty()) {
        result.AppendError("empty typenames not allowed");
        result.SetStatus(eReturnStatusFailed);
        return false;
      }

      ConstString typeCS(arg_entry.ref);
      if (m_command_options.m_regex) {
        RegularExpressionSP typeRX(new RegularExpression());
        if (!typeRX->Compile(arg_entry.ref)) {
          result.AppendError(
              "regex format error (maybe this is not really a regex?)");
          result.SetStatus(eReturnStatusFailed);
          return false;
        }
        category_sp->GetRegexTypeSummariesContainer()->Delete(typeCS);
        category_sp->GetRegexTypeFormatsContainer()->Add(typeRX, entry);
      } else
        category_sp->GetTypeFormatsContainer()->Add(typeCS, entry);
    }

    result.SetStatus(eReturnStatusSuccessFinishNoResult);
    return result.Succeeded();
  }
};

static constexpr OptionDefinition g_type_formatter_delete_options[] = {
    // clang-format off
  { LLDB_OPT_SET_1, false, "all",      'a', OptionParser::eNoArgument,       nullptr, {}, 0, eArgTypeNone,     "Delete from every category." },
  { LLDB_OPT_SET_2, false, "category", 'w', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeName,     "Delete from given category." },
  { LLDB_OPT_SET_3, false, "language", 'l', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeLanguage, "Delete from given language's category." }
    // clang-format on
};

class CommandObjectTypeFormatterDelete : public CommandObjectParsed {
protected:
  class CommandOptions : public Options {
  public:
    CommandOptions() : Options() {}

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'a':
        m_delete_all = true;
        break;
      case 'w':
        m_category = std::string(option_arg);
        break;
      case 'l':
        m_language = Language::GetLanguageTypeFromString(option_arg);
        break;
      default:
        error.SetErrorStringWithFormat("unrecognized option '%c'",
                                       short_option);
        break;
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_delete_all = false;
      m_category = "default";
      m_language = lldb::eLanguageTypeUnknown;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_type_formatter_delete_options);
    }

    // Instance variables to hold the values for command options.

    bool m_delete_all;
    std::string m_category;
    lldb::LanguageType m_language;
  };

  CommandOptions m_options;
  uint32_t m_formatter_kind_mask;

  Options *GetOptions() override { return &m_options; }

public:
  CommandObjectTypeFormatterDelete(CommandInterpreter &interpreter,
                                   uint32_t formatter_kind_mask,
                                   const char *name, const char *help)
      : CommandObjectParsed(interpreter, name, help, nullptr), m_options(),
        m_formatter_kind_mask(formatter_kind_mask) {
    CommandArgumentEntry type_arg;
    CommandArgumentData type_style_arg;

    type_style_arg.arg_type = eArgTypeName;
    type_style_arg.arg_repetition = eArgRepeatPlain;

    type_arg.push_back(type_style_arg);

    m_arguments.push_back(type_arg);
  }

  ~CommandObjectTypeFormatterDelete() override = default;

protected:
  virtual bool FormatterSpecificDeletion(ConstString typeCS) { return false; }

  bool DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();

    if (argc != 1) {
      result.AppendErrorWithFormat("%s takes 1 arg.\n", m_cmd_name.c_str());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    const char *typeA = command.GetArgumentAtIndex(0);
    ConstString typeCS(typeA);

    if (!typeCS) {
      result.AppendError("empty typenames not allowed");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    if (m_options.m_delete_all) {
      DataVisualization::Categories::ForEach(
          [this, typeCS](const lldb::TypeCategoryImplSP &category_sp) -> bool {
            category_sp->Delete(typeCS, m_formatter_kind_mask);
            return true;
          });
      result.SetStatus(eReturnStatusSuccessFinishNoResult);
      return result.Succeeded();
    }

    bool delete_category = false;
    bool extra_deletion = false;

    if (m_options.m_language != lldb::eLanguageTypeUnknown) {
      lldb::TypeCategoryImplSP category;
      DataVisualization::Categories::GetCategory(m_options.m_language,
                                                 category);
      if (category)
        delete_category = category->Delete(typeCS, m_formatter_kind_mask);
      extra_deletion = FormatterSpecificDeletion(typeCS);
    } else {
      lldb::TypeCategoryImplSP category;
      DataVisualization::Categories::GetCategory(
          ConstString(m_options.m_category.c_str()), category);
      if (category)
        delete_category = category->Delete(typeCS, m_formatter_kind_mask);
      extra_deletion = FormatterSpecificDeletion(typeCS);
    }

    if (delete_category || extra_deletion) {
      result.SetStatus(eReturnStatusSuccessFinishNoResult);
      return result.Succeeded();
    } else {
      result.AppendErrorWithFormat("no custom formatter for %s.\n", typeA);
      result.SetStatus(eReturnStatusFailed);
      return false;
    }
  }
};

static constexpr OptionDefinition g_type_formatter_clear_options[] = {
    // clang-format off
  { LLDB_OPT_SET_ALL, false, "all", 'a', OptionParser::eNoArgument, nullptr, {}, 0, eArgTypeNone, "Clear every category." }
    // clang-format on
};

class CommandObjectTypeFormatterClear : public CommandObjectParsed {
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
      case 'a':
        m_delete_all = true;
        break;
      default:
        error.SetErrorStringWithFormat("unrecognized option '%c'",
                                       short_option);
        break;
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_delete_all = false;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_type_formatter_clear_options);
    }

    // Instance variables to hold the values for command options.
    bool m_delete_all;
  };

  CommandOptions m_options;
  uint32_t m_formatter_kind_mask;

  Options *GetOptions() override { return &m_options; }

public:
  CommandObjectTypeFormatterClear(CommandInterpreter &interpreter,
                                  uint32_t formatter_kind_mask,
                                  const char *name, const char *help)
      : CommandObjectParsed(interpreter, name, help, nullptr), m_options(),
        m_formatter_kind_mask(formatter_kind_mask) {}

  ~CommandObjectTypeFormatterClear() override = default;

protected:
  virtual void FormatterSpecificDeletion() {}

  bool DoExecute(Args &command, CommandReturnObject &result) override {
    if (m_options.m_delete_all) {
      DataVisualization::Categories::ForEach(
          [this](const TypeCategoryImplSP &category_sp) -> bool {
            category_sp->Clear(m_formatter_kind_mask);
            return true;
          });
    } else {
      lldb::TypeCategoryImplSP category;
      if (command.GetArgumentCount() > 0) {
        const char *cat_name = command.GetArgumentAtIndex(0);
        ConstString cat_nameCS(cat_name);
        DataVisualization::Categories::GetCategory(cat_nameCS, category);
      } else {
        DataVisualization::Categories::GetCategory(ConstString(nullptr),
                                                   category);
      }
      category->Clear(m_formatter_kind_mask);
    }

    FormatterSpecificDeletion();

    result.SetStatus(eReturnStatusSuccessFinishResult);
    return result.Succeeded();
  }
};

//-------------------------------------------------------------------------
// CommandObjectTypeFormatDelete
//-------------------------------------------------------------------------

class CommandObjectTypeFormatDelete : public CommandObjectTypeFormatterDelete {
public:
  CommandObjectTypeFormatDelete(CommandInterpreter &interpreter)
      : CommandObjectTypeFormatterDelete(
            interpreter,
            eFormatCategoryItemValue | eFormatCategoryItemRegexValue,
            "type format delete",
            "Delete an existing formatting style for a type.") {}

  ~CommandObjectTypeFormatDelete() override = default;
};

//-------------------------------------------------------------------------
// CommandObjectTypeFormatClear
//-------------------------------------------------------------------------

class CommandObjectTypeFormatClear : public CommandObjectTypeFormatterClear {
public:
  CommandObjectTypeFormatClear(CommandInterpreter &interpreter)
      : CommandObjectTypeFormatterClear(
            interpreter,
            eFormatCategoryItemValue | eFormatCategoryItemRegexValue,
            "type format clear", "Delete all existing format styles.") {}
};


static constexpr OptionDefinition g_type_formatter_list_options[] = {
  // clang-format off
  {LLDB_OPT_SET_1, false, "category-regex", 'w', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeName,     "Only show categories matching this filter."},
  {LLDB_OPT_SET_2, false, "language",       'l', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeLanguage, "Only show the category for a specific language."}
  // clang-format on
};

template <typename FormatterType>
class CommandObjectTypeFormatterList : public CommandObjectParsed {
  typedef typename FormatterType::SharedPointer FormatterSharedPointer;

  class CommandOptions : public Options {
  public:
    CommandOptions()
        : Options(), m_category_regex("", ""),
          m_category_language(lldb::eLanguageTypeUnknown,
                              lldb::eLanguageTypeUnknown) {}

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;
      switch (short_option) {
      case 'w':
        m_category_regex.SetCurrentValue(option_arg);
        m_category_regex.SetOptionWasSet();
        break;
      case 'l':
        error = m_category_language.SetValueFromString(option_arg);
        if (error.Success())
          m_category_language.SetOptionWasSet();
        break;
      default:
        error.SetErrorStringWithFormat("unrecognized option '%c'",
                                       short_option);
        break;
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_category_regex.Clear();
      m_category_language.Clear();
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_type_formatter_list_options);
    }

    // Instance variables to hold the values for command options.

    OptionValueString m_category_regex;
    OptionValueLanguage m_category_language;
  };

  CommandOptions m_options;

  Options *GetOptions() override { return &m_options; }

public:
  CommandObjectTypeFormatterList(CommandInterpreter &interpreter,
                                 const char *name, const char *help)
      : CommandObjectParsed(interpreter, name, help, nullptr), m_options() {
    CommandArgumentEntry type_arg;
    CommandArgumentData type_style_arg;

    type_style_arg.arg_type = eArgTypeName;
    type_style_arg.arg_repetition = eArgRepeatOptional;

    type_arg.push_back(type_style_arg);

    m_arguments.push_back(type_arg);
  }

  ~CommandObjectTypeFormatterList() override = default;

protected:
  virtual bool FormatterSpecificList(CommandReturnObject &result) {
    return false;
  }

  bool DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();

    std::unique_ptr<RegularExpression> category_regex;
    std::unique_ptr<RegularExpression> formatter_regex;

    if (m_options.m_category_regex.OptionWasSet()) {
      category_regex.reset(new RegularExpression());
      if (!category_regex->Compile(
              m_options.m_category_regex.GetCurrentValueAsRef())) {
        result.AppendErrorWithFormat(
            "syntax error in category regular expression '%s'",
            m_options.m_category_regex.GetCurrentValueAsRef().str().c_str());
        result.SetStatus(eReturnStatusFailed);
        return false;
      }
    }

    if (argc == 1) {
      const char *arg = command.GetArgumentAtIndex(0);
      formatter_regex.reset(new RegularExpression());
      if (!formatter_regex->Compile(llvm::StringRef::withNullAsEmpty(arg))) {
        result.AppendErrorWithFormat("syntax error in regular expression '%s'",
                                     arg);
        result.SetStatus(eReturnStatusFailed);
        return false;
      }
    }

    bool any_printed = false;

    auto category_closure = [&result, &formatter_regex, &any_printed](
        const lldb::TypeCategoryImplSP &category) -> void {
      result.GetOutputStream().Printf(
          "-----------------------\nCategory: %s%s\n-----------------------\n",
          category->GetName(), category->IsEnabled() ? "" : " (disabled)");

      TypeCategoryImpl::ForEachCallbacks<FormatterType> foreach;
      foreach
        .SetExact([&result, &formatter_regex, &any_printed](
                      ConstString name,
                      const FormatterSharedPointer &format_sp) -> bool {
          if (formatter_regex) {
            bool escape = true;
            if (name.GetStringRef() == formatter_regex->GetText()) {
              escape = false;
            } else if (formatter_regex->Execute(name.GetStringRef())) {
              escape = false;
            }

            if (escape)
              return true;
          }

          any_printed = true;
          result.GetOutputStream().Printf("%s: %s\n", name.AsCString(),
                                          format_sp->GetDescription().c_str());
          return true;
        });

      foreach
        .SetWithRegex([&result, &formatter_regex, &any_printed](
                          RegularExpressionSP regex_sp,
                          const FormatterSharedPointer &format_sp) -> bool {
          if (formatter_regex) {
            bool escape = true;
            if (regex_sp->GetText() == formatter_regex->GetText()) {
              escape = false;
            } else if (formatter_regex->Execute(regex_sp->GetText())) {
              escape = false;
            }

            if (escape)
              return true;
          }

          any_printed = true;
          result.GetOutputStream().Printf("%s: %s\n",
                                          regex_sp->GetText().str().c_str(),
                                          format_sp->GetDescription().c_str());
          return true;
        });

      category->ForEach(foreach);
    };

    if (m_options.m_category_language.OptionWasSet()) {
      lldb::TypeCategoryImplSP category_sp;
      DataVisualization::Categories::GetCategory(
          m_options.m_category_language.GetCurrentValue(), category_sp);
      if (category_sp)
        category_closure(category_sp);
    } else {
      DataVisualization::Categories::ForEach(
          [&category_regex, &category_closure](
              const lldb::TypeCategoryImplSP &category) -> bool {
            if (category_regex) {
              bool escape = true;
              if (category->GetName() == category_regex->GetText()) {
                escape = false;
              } else if (category_regex->Execute(
                             llvm::StringRef::withNullAsEmpty(
                                 category->GetName()))) {
                escape = false;
              }

              if (escape)
                return true;
            }

            category_closure(category);

            return true;
          });

      any_printed = FormatterSpecificList(result) | any_printed;
    }

    if (any_printed)
      result.SetStatus(eReturnStatusSuccessFinishResult);
    else {
      result.GetOutputStream().PutCString("no matching results found.\n");
      result.SetStatus(eReturnStatusSuccessFinishNoResult);
    }
    return result.Succeeded();
  }
};

//-------------------------------------------------------------------------
// CommandObjectTypeFormatList
//-------------------------------------------------------------------------

class CommandObjectTypeFormatList
    : public CommandObjectTypeFormatterList<TypeFormatImpl> {
public:
  CommandObjectTypeFormatList(CommandInterpreter &interpreter)
      : CommandObjectTypeFormatterList(interpreter, "type format list",
                                       "Show a list of current formats.") {}
};

#ifndef LLDB_DISABLE_PYTHON

//-------------------------------------------------------------------------
// CommandObjectTypeSummaryAdd
//-------------------------------------------------------------------------

#endif // LLDB_DISABLE_PYTHON

Status CommandObjectTypeSummaryAdd::CommandOptions::SetOptionValue(
    uint32_t option_idx, llvm::StringRef option_arg,
    ExecutionContext *execution_context) {
  Status error;
  const int short_option = m_getopt_table[option_idx].val;
  bool success;

  switch (short_option) {
  case 'C':
    m_flags.SetCascades(OptionArgParser::ToBoolean(option_arg, true, &success));
    if (!success)
      error.SetErrorStringWithFormat("invalid value for cascade: %s",
                                     option_arg.str().c_str());
    break;
  case 'e':
    m_flags.SetDontShowChildren(false);
    break;
  case 'h':
    m_flags.SetHideEmptyAggregates(true);
    break;
  case 'v':
    m_flags.SetDontShowValue(true);
    break;
  case 'c':
    m_flags.SetShowMembersOneLiner(true);
    break;
  case 's':
    m_format_string = std::string(option_arg);
    break;
  case 'p':
    m_flags.SetSkipPointers(true);
    break;
  case 'r':
    m_flags.SetSkipReferences(true);
    break;
  case 'x':
    m_regex = true;
    break;
  case 'n':
    m_name.SetString(option_arg);
    break;
  case 'o':
    m_python_script = option_arg;
    m_is_add_script = true;
    break;
  case 'F':
    m_python_function = option_arg;
    m_is_add_script = true;
    break;
  case 'P':
    m_is_add_script = true;
    break;
  case 'w':
    m_category = std::string(option_arg);
    break;
  case 'O':
    m_flags.SetHideItemNames(true);
    break;
  default:
    error.SetErrorStringWithFormat("unrecognized option '%c'", short_option);
    break;
  }

  return error;
}

void CommandObjectTypeSummaryAdd::CommandOptions::OptionParsingStarting(
    ExecutionContext *execution_context) {
  m_flags.Clear().SetCascades().SetDontShowChildren().SetDontShowValue(false);
  m_flags.SetShowMembersOneLiner(false)
      .SetSkipPointers(false)
      .SetSkipReferences(false)
      .SetHideItemNames(false);

  m_regex = false;
  m_name.Clear();
  m_python_script = "";
  m_python_function = "";
  m_format_string = "";
  m_is_add_script = false;
  m_category = "default";
}

#ifndef LLDB_DISABLE_PYTHON

bool CommandObjectTypeSummaryAdd::Execute_ScriptSummary(
    Args &command, CommandReturnObject &result) {
  const size_t argc = command.GetArgumentCount();

  if (argc < 1 && !m_options.m_name) {
    result.AppendErrorWithFormat("%s takes one or more args.\n",
                                 m_cmd_name.c_str());
    result.SetStatus(eReturnStatusFailed);
    return false;
  }

  TypeSummaryImplSP script_format;

  if (!m_options.m_python_function
           .empty()) // we have a Python function ready to use
  {
    const char *funct_name = m_options.m_python_function.c_str();
    if (!funct_name || !funct_name[0]) {
      result.AppendError("function name empty.\n");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    std::string code =
        ("    " + m_options.m_python_function + "(valobj,internal_dict)");

    script_format.reset(
        new ScriptSummaryFormat(m_options.m_flags, funct_name, code.c_str()));

    ScriptInterpreter *interpreter = m_interpreter.GetScriptInterpreter();

    if (interpreter && !interpreter->CheckObjectExists(funct_name))
      result.AppendWarningWithFormat(
          "The provided function \"%s\" does not exist - "
          "please define it before attempting to use this summary.\n",
          funct_name);
  } else if (!m_options.m_python_script
                  .empty()) // we have a quick 1-line script, just use it
  {
    ScriptInterpreter *interpreter = m_interpreter.GetScriptInterpreter();
    if (!interpreter) {
      result.AppendError("script interpreter missing - unable to generate "
                         "function wrapper.\n");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }
    StringList funct_sl;
    funct_sl << m_options.m_python_script.c_str();
    std::string funct_name_str;
    if (!interpreter->GenerateTypeScriptFunction(funct_sl, funct_name_str)) {
      result.AppendError("unable to generate function wrapper.\n");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }
    if (funct_name_str.empty()) {
      result.AppendError(
          "script interpreter failed to generate a valid function name.\n");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    std::string code = "    " + m_options.m_python_script;

    script_format.reset(new ScriptSummaryFormat(
        m_options.m_flags, funct_name_str.c_str(), code.c_str()));
  } else {
    // Use an IOHandler to grab Python code from the user
    ScriptAddOptions *options =
        new ScriptAddOptions(m_options.m_flags, m_options.m_regex,
                             m_options.m_name, m_options.m_category);

    for (auto &entry : command.entries()) {
      if (entry.ref.empty()) {
        result.AppendError("empty typenames not allowed");
        result.SetStatus(eReturnStatusFailed);
        return false;
      }

      options->m_target_types << entry.ref;
    }

    m_interpreter.GetPythonCommandsFromIOHandler(
        "    ",   // Prompt
        *this,    // IOHandlerDelegate
        true,     // Run IOHandler in async mode
        options); // Baton for the "io_handler" that will be passed back into
                  // our IOHandlerDelegate functions
    result.SetStatus(eReturnStatusSuccessFinishNoResult);

    return result.Succeeded();
  }

  // if I am here, script_format must point to something good, so I can add
  // that as a script summary to all interested parties

  Status error;

  for (auto &entry : command.entries()) {
    CommandObjectTypeSummaryAdd::AddSummary(
        ConstString(entry.ref), script_format,
        (m_options.m_regex ? eRegexSummary : eRegularSummary),
        m_options.m_category, &error);
    if (error.Fail()) {
      result.AppendError(error.AsCString());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }
  }

  if (m_options.m_name) {
    AddSummary(m_options.m_name, script_format, eNamedSummary,
               m_options.m_category, &error);
    if (error.Fail()) {
      result.AppendError(error.AsCString());
      result.AppendError("added to types, but not given a name");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }
  }

  return result.Succeeded();
}

#endif // LLDB_DISABLE_PYTHON

bool CommandObjectTypeSummaryAdd::Execute_StringSummary(
    Args &command, CommandReturnObject &result) {
  const size_t argc = command.GetArgumentCount();

  if (argc < 1 && !m_options.m_name) {
    result.AppendErrorWithFormat("%s takes one or more args.\n",
                                 m_cmd_name.c_str());
    result.SetStatus(eReturnStatusFailed);
    return false;
  }

  if (!m_options.m_flags.GetShowMembersOneLiner() &&
      m_options.m_format_string.empty()) {
    result.AppendError("empty summary strings not allowed");
    result.SetStatus(eReturnStatusFailed);
    return false;
  }

  const char *format_cstr = (m_options.m_flags.GetShowMembersOneLiner()
                                 ? ""
                                 : m_options.m_format_string.c_str());

  // ${var%S} is an endless recursion, prevent it
  if (strcmp(format_cstr, "${var%S}") == 0) {
    result.AppendError("recursive summary not allowed");
    result.SetStatus(eReturnStatusFailed);
    return false;
  }

  std::unique_ptr<StringSummaryFormat> string_format(
      new StringSummaryFormat(m_options.m_flags, format_cstr));
  if (!string_format) {
    result.AppendError("summary creation failed");
    result.SetStatus(eReturnStatusFailed);
    return false;
  }
  if (string_format->m_error.Fail()) {
    result.AppendErrorWithFormat("syntax error: %s",
                                 string_format->m_error.AsCString("<unknown>"));
    result.SetStatus(eReturnStatusFailed);
    return false;
  }
  lldb::TypeSummaryImplSP entry(string_format.release());

  // now I have a valid format, let's add it to every type
  Status error;
  for (auto &arg_entry : command.entries()) {
    if (arg_entry.ref.empty()) {
      result.AppendError("empty typenames not allowed");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }
    ConstString typeCS(arg_entry.ref);

    AddSummary(typeCS, entry,
               (m_options.m_regex ? eRegexSummary : eRegularSummary),
               m_options.m_category, &error);

    if (error.Fail()) {
      result.AppendError(error.AsCString());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }
  }

  if (m_options.m_name) {
    AddSummary(m_options.m_name, entry, eNamedSummary, m_options.m_category,
               &error);
    if (error.Fail()) {
      result.AppendError(error.AsCString());
      result.AppendError("added to types, but not given a name");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }
  }

  result.SetStatus(eReturnStatusSuccessFinishNoResult);
  return result.Succeeded();
}

CommandObjectTypeSummaryAdd::CommandObjectTypeSummaryAdd(
    CommandInterpreter &interpreter)
    : CommandObjectParsed(interpreter, "type summary add",
                          "Add a new summary style for a type.", nullptr),
      IOHandlerDelegateMultiline("DONE"), m_options(interpreter) {
  CommandArgumentEntry type_arg;
  CommandArgumentData type_style_arg;

  type_style_arg.arg_type = eArgTypeName;
  type_style_arg.arg_repetition = eArgRepeatPlus;

  type_arg.push_back(type_style_arg);

  m_arguments.push_back(type_arg);

  SetHelpLong(
      R"(
The following examples of 'type summary add' refer to this code snippet for context:

    struct JustADemo
    {
        int* ptr;
        float value;
        JustADemo(int p = 1, float v = 0.1) : ptr(new int(p)), value(v) {}
    };
    JustADemo demo_instance(42, 3.14);

    typedef JustADemo NewDemo;
    NewDemo new_demo_instance(42, 3.14);

(lldb) type summary add --summary-string "the answer is ${*var.ptr}" JustADemo

    Subsequently displaying demo_instance with 'frame variable' or 'expression' will display "the answer is 42"

(lldb) type summary add --summary-string "the answer is ${*var.ptr}, and the question is ${var.value}" JustADemo

    Subsequently displaying demo_instance with 'frame variable' or 'expression' will display "the answer is 42 and the question is 3.14"

)"
      "Alternatively, you could define formatting for all pointers to integers and \
rely on that when formatting JustADemo to obtain the same result:"
      R"(

(lldb) type summary add --summary-string "${var%V} -> ${*var}" "int *"
(lldb) type summary add --summary-string "the answer is ${var.ptr}, and the question is ${var.value}" JustADemo

)"
      "Type summaries are automatically applied to derived typedefs, so the examples \
above apply to both JustADemo and NewDemo.  The cascade option can be used to \
suppress this behavior:"
      R"(

(lldb) type summary add --summary-string "${var.ptr}, ${var.value},{${var.byte}}" JustADemo -C no

    The summary will now be used for values of JustADemo but not NewDemo.

)"
      "By default summaries are shown for pointers and references to values of the \
specified type.  To suppress formatting for pointers use the -p option, or apply \
the corresponding -r option to suppress formatting for references:"
      R"(

(lldb) type summary add -p -r --summary-string "${var.ptr}, ${var.value},{${var.byte}}" JustADemo

)"
      "One-line summaries including all fields in a type can be inferred without supplying an \
explicit summary string by passing the -c option:"
      R"(

(lldb) type summary add -c JustADemo
(lldb) frame variable demo_instance
(ptr=<address>, value=3.14)

)"
      "Type summaries normally suppress the nested display of individual fields.  To \
supply a summary to supplement the default structure add the -e option:"
      R"(

(lldb) type summary add -e --summary-string "*ptr = ${*var.ptr}" JustADemo

)"
      "Now when displaying JustADemo values the int* is displayed, followed by the \
standard LLDB sequence of children, one per line:"
      R"(

*ptr = 42 {
  ptr = <address>
  value = 3.14
}

)"
      "You can also add summaries written in Python.  These scripts use lldb public API to \
gather information from your variables and produce a meaningful summary.  To start a \
multi-line script use the -P option.  The function declaration will be displayed along with \
a comment describing the two arguments.  End your script with the  word 'DONE' on a line by \
itself:"
      R"(

(lldb) type summary add JustADemo -P
def function (valobj,internal_dict):
"""valobj: an SBValue which you want to provide a summary for
internal_dict: an LLDB support object not to be used"""
    value = valobj.GetChildMemberWithName('value');
    return 'My value is ' + value.GetValue();
    DONE

Alternatively, the -o option can be used when providing a simple one-line Python script:

(lldb) type summary add JustADemo -o "value = valobj.GetChildMemberWithName('value'); return 'My value is ' + value.GetValue();")");
}

bool CommandObjectTypeSummaryAdd::DoExecute(Args &command,
                                            CommandReturnObject &result) {
  WarnOnPotentialUnquotedUnsignedType(command, result);

  if (m_options.m_is_add_script) {
#ifndef LLDB_DISABLE_PYTHON
    return Execute_ScriptSummary(command, result);
#else
    result.AppendError("python is disabled");
    result.SetStatus(eReturnStatusFailed);
    return false;
#endif // LLDB_DISABLE_PYTHON
  }

  return Execute_StringSummary(command, result);
}

static bool FixArrayTypeNameWithRegex(ConstString &type_name) {
  llvm::StringRef type_name_ref(type_name.GetStringRef());

  if (type_name_ref.endswith("[]")) {
    std::string type_name_str(type_name.GetCString());
    type_name_str.resize(type_name_str.length() - 2);
    if (type_name_str.back() != ' ')
      type_name_str.append(" \\[[0-9]+\\]");
    else
      type_name_str.append("\\[[0-9]+\\]");
    type_name.SetCString(type_name_str.c_str());
    return true;
  }
  return false;
}

bool CommandObjectTypeSummaryAdd::AddSummary(ConstString type_name,
                                             TypeSummaryImplSP entry,
                                             SummaryFormatType type,
                                             std::string category_name,
                                             Status *error) {
  lldb::TypeCategoryImplSP category;
  DataVisualization::Categories::GetCategory(ConstString(category_name.c_str()),
                                             category);

  if (type == eRegularSummary) {
    if (FixArrayTypeNameWithRegex(type_name))
      type = eRegexSummary;
  }

  if (type == eRegexSummary) {
    RegularExpressionSP typeRX(new RegularExpression());
    if (!typeRX->Compile(type_name.GetStringRef())) {
      if (error)
        error->SetErrorString(
            "regex format error (maybe this is not really a regex?)");
      return false;
    }

    category->GetRegexTypeSummariesContainer()->Delete(type_name);
    category->GetRegexTypeSummariesContainer()->Add(typeRX, entry);

    return true;
  } else if (type == eNamedSummary) {
    // system named summaries do not exist (yet?)
    DataVisualization::NamedSummaryFormats::Add(type_name, entry);
    return true;
  } else {
    category->GetTypeSummariesContainer()->Add(type_name, entry);
    return true;
  }
}

//-------------------------------------------------------------------------
// CommandObjectTypeSummaryDelete
//-------------------------------------------------------------------------

class CommandObjectTypeSummaryDelete : public CommandObjectTypeFormatterDelete {
public:
  CommandObjectTypeSummaryDelete(CommandInterpreter &interpreter)
      : CommandObjectTypeFormatterDelete(
            interpreter,
            eFormatCategoryItemSummary | eFormatCategoryItemRegexSummary,
            "type summary delete", "Delete an existing summary for a type.") {}

  ~CommandObjectTypeSummaryDelete() override = default;

protected:
  bool FormatterSpecificDeletion(ConstString typeCS) override {
    if (m_options.m_language != lldb::eLanguageTypeUnknown)
      return false;
    return DataVisualization::NamedSummaryFormats::Delete(typeCS);
  }
};

class CommandObjectTypeSummaryClear : public CommandObjectTypeFormatterClear {
public:
  CommandObjectTypeSummaryClear(CommandInterpreter &interpreter)
      : CommandObjectTypeFormatterClear(
            interpreter,
            eFormatCategoryItemSummary | eFormatCategoryItemRegexSummary,
            "type summary clear", "Delete all existing summaries.") {}

protected:
  void FormatterSpecificDeletion() override {
    DataVisualization::NamedSummaryFormats::Clear();
  }
};

//-------------------------------------------------------------------------
// CommandObjectTypeSummaryList
//-------------------------------------------------------------------------

class CommandObjectTypeSummaryList
    : public CommandObjectTypeFormatterList<TypeSummaryImpl> {
public:
  CommandObjectTypeSummaryList(CommandInterpreter &interpreter)
      : CommandObjectTypeFormatterList(interpreter, "type summary list",
                                       "Show a list of current summaries.") {}

protected:
  bool FormatterSpecificList(CommandReturnObject &result) override {
    if (DataVisualization::NamedSummaryFormats::GetCount() > 0) {
      result.GetOutputStream().Printf("Named summaries:\n");
      DataVisualization::NamedSummaryFormats::ForEach(
          [&result](ConstString name,
                    const TypeSummaryImplSP &summary_sp) -> bool {
            result.GetOutputStream().Printf(
                "%s: %s\n", name.AsCString(),
                summary_sp->GetDescription().c_str());
            return true;
          });
      return true;
    }
    return false;
  }
};

//-------------------------------------------------------------------------
// CommandObjectTypeCategoryDefine
//-------------------------------------------------------------------------

static constexpr OptionDefinition g_type_category_define_options[] = {
    // clang-format off
  { LLDB_OPT_SET_ALL, false, "enabled",  'e', OptionParser::eNoArgument,       nullptr, {}, 0, eArgTypeNone,     "If specified, this category will be created enabled." },
  { LLDB_OPT_SET_ALL, false, "language", 'l', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeLanguage, "Specify the language that this category is supported for." }
    // clang-format on
};

class CommandObjectTypeCategoryDefine : public CommandObjectParsed {
  class CommandOptions : public Options {
  public:
    CommandOptions()
        : Options(), m_define_enabled(false, false),
          m_cate_language(eLanguageTypeUnknown, eLanguageTypeUnknown) {}

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'e':
        m_define_enabled.SetValueFromString(llvm::StringRef("true"));
        break;
      case 'l':
        error = m_cate_language.SetValueFromString(option_arg);
        break;
      default:
        error.SetErrorStringWithFormat("unrecognized option '%c'",
                                       short_option);
        break;
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_define_enabled.Clear();
      m_cate_language.Clear();
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_type_category_define_options);
    }

    // Instance variables to hold the values for command options.

    OptionValueBoolean m_define_enabled;
    OptionValueLanguage m_cate_language;
  };

  CommandOptions m_options;

  Options *GetOptions() override { return &m_options; }

public:
  CommandObjectTypeCategoryDefine(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "type category define",
                            "Define a new category as a source of formatters.",
                            nullptr),
        m_options() {
    CommandArgumentEntry type_arg;
    CommandArgumentData type_style_arg;

    type_style_arg.arg_type = eArgTypeName;
    type_style_arg.arg_repetition = eArgRepeatPlus;

    type_arg.push_back(type_style_arg);

    m_arguments.push_back(type_arg);
  }

  ~CommandObjectTypeCategoryDefine() override = default;

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();

    if (argc < 1) {
      result.AppendErrorWithFormat("%s takes 1 or more args.\n",
                                   m_cmd_name.c_str());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    for (auto &entry : command.entries()) {
      TypeCategoryImplSP category_sp;
      if (DataVisualization::Categories::GetCategory(ConstString(entry.ref),
                                                     category_sp) &&
          category_sp) {
        category_sp->AddLanguage(m_options.m_cate_language.GetCurrentValue());
        if (m_options.m_define_enabled.GetCurrentValue())
          DataVisualization::Categories::Enable(category_sp,
                                                TypeCategoryMap::Default);
      }
    }

    result.SetStatus(eReturnStatusSuccessFinishResult);
    return result.Succeeded();
  }
};

//-------------------------------------------------------------------------
// CommandObjectTypeCategoryEnable
//-------------------------------------------------------------------------

static constexpr OptionDefinition g_type_category_enable_options[] = {
    // clang-format off
  { LLDB_OPT_SET_ALL, false, "language", 'l', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeLanguage, "Enable the category for this language." },
    // clang-format on
};

class CommandObjectTypeCategoryEnable : public CommandObjectParsed {
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
        if (!option_arg.empty()) {
          m_language = Language::GetLanguageTypeFromString(option_arg);
          if (m_language == lldb::eLanguageTypeUnknown)
            error.SetErrorStringWithFormat("unrecognized language '%s'",
                                           option_arg.str().c_str());
        }
        break;
      default:
        error.SetErrorStringWithFormat("unrecognized option '%c'",
                                       short_option);
        break;
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_language = lldb::eLanguageTypeUnknown;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_type_category_enable_options);
    }

    // Instance variables to hold the values for command options.

    lldb::LanguageType m_language;
  };

  CommandOptions m_options;

  Options *GetOptions() override { return &m_options; }

public:
  CommandObjectTypeCategoryEnable(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "type category enable",
                            "Enable a category as a source of formatters.",
                            nullptr),
        m_options() {
    CommandArgumentEntry type_arg;
    CommandArgumentData type_style_arg;

    type_style_arg.arg_type = eArgTypeName;
    type_style_arg.arg_repetition = eArgRepeatPlus;

    type_arg.push_back(type_style_arg);

    m_arguments.push_back(type_arg);
  }

  ~CommandObjectTypeCategoryEnable() override = default;

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();

    if (argc < 1 && m_options.m_language == lldb::eLanguageTypeUnknown) {
      result.AppendErrorWithFormat("%s takes arguments and/or a language",
                                   m_cmd_name.c_str());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    if (argc == 1 && strcmp(command.GetArgumentAtIndex(0), "*") == 0) {
      DataVisualization::Categories::EnableStar();
    } else if (argc > 0) {
      for (int i = argc - 1; i >= 0; i--) {
        const char *typeA = command.GetArgumentAtIndex(i);
        ConstString typeCS(typeA);

        if (!typeCS) {
          result.AppendError("empty category name not allowed");
          result.SetStatus(eReturnStatusFailed);
          return false;
        }
        DataVisualization::Categories::Enable(typeCS);
        lldb::TypeCategoryImplSP cate;
        if (DataVisualization::Categories::GetCategory(typeCS, cate) && cate) {
          if (cate->GetCount() == 0) {
            result.AppendWarning("empty category enabled (typo?)");
          }
        }
      }
    }

    if (m_options.m_language != lldb::eLanguageTypeUnknown)
      DataVisualization::Categories::Enable(m_options.m_language);

    result.SetStatus(eReturnStatusSuccessFinishResult);
    return result.Succeeded();
  }
};

//-------------------------------------------------------------------------
// CommandObjectTypeCategoryDelete
//-------------------------------------------------------------------------

class CommandObjectTypeCategoryDelete : public CommandObjectParsed {
public:
  CommandObjectTypeCategoryDelete(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "type category delete",
                            "Delete a category and all associated formatters.",
                            nullptr) {
    CommandArgumentEntry type_arg;
    CommandArgumentData type_style_arg;

    type_style_arg.arg_type = eArgTypeName;
    type_style_arg.arg_repetition = eArgRepeatPlus;

    type_arg.push_back(type_style_arg);

    m_arguments.push_back(type_arg);
  }

  ~CommandObjectTypeCategoryDelete() override = default;

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();

    if (argc < 1) {
      result.AppendErrorWithFormat("%s takes 1 or more arg.\n",
                                   m_cmd_name.c_str());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    bool success = true;

    // the order is not relevant here
    for (int i = argc - 1; i >= 0; i--) {
      const char *typeA = command.GetArgumentAtIndex(i);
      ConstString typeCS(typeA);

      if (!typeCS) {
        result.AppendError("empty category name not allowed");
        result.SetStatus(eReturnStatusFailed);
        return false;
      }
      if (!DataVisualization::Categories::Delete(typeCS))
        success = false; // keep deleting even if we hit an error
    }
    if (success) {
      result.SetStatus(eReturnStatusSuccessFinishResult);
      return result.Succeeded();
    } else {
      result.AppendError("cannot delete one or more categories\n");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }
  }
};

//-------------------------------------------------------------------------
// CommandObjectTypeCategoryDisable
//-------------------------------------------------------------------------

OptionDefinition constexpr g_type_category_disable_options[] = {
    // clang-format off
  { LLDB_OPT_SET_ALL, false, "language", 'l', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeLanguage, "Enable the category for this language." }
    // clang-format on
};

class CommandObjectTypeCategoryDisable : public CommandObjectParsed {
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
        if (!option_arg.empty()) {
          m_language = Language::GetLanguageTypeFromString(option_arg);
          if (m_language == lldb::eLanguageTypeUnknown)
            error.SetErrorStringWithFormat("unrecognized language '%s'",
                                           option_arg.str().c_str());
        }
        break;
      default:
        error.SetErrorStringWithFormat("unrecognized option '%c'",
                                       short_option);
        break;
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_language = lldb::eLanguageTypeUnknown;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_type_category_disable_options);
    }

    // Instance variables to hold the values for command options.

    lldb::LanguageType m_language;
  };

  CommandOptions m_options;

  Options *GetOptions() override { return &m_options; }

public:
  CommandObjectTypeCategoryDisable(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "type category disable",
                            "Disable a category as a source of formatters.",
                            nullptr),
        m_options() {
    CommandArgumentEntry type_arg;
    CommandArgumentData type_style_arg;

    type_style_arg.arg_type = eArgTypeName;
    type_style_arg.arg_repetition = eArgRepeatPlus;

    type_arg.push_back(type_style_arg);

    m_arguments.push_back(type_arg);
  }

  ~CommandObjectTypeCategoryDisable() override = default;

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();

    if (argc < 1 && m_options.m_language == lldb::eLanguageTypeUnknown) {
      result.AppendErrorWithFormat("%s takes arguments and/or a language",
                                   m_cmd_name.c_str());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    if (argc == 1 && strcmp(command.GetArgumentAtIndex(0), "*") == 0) {
      DataVisualization::Categories::DisableStar();
    } else if (argc > 0) {
      // the order is not relevant here
      for (int i = argc - 1; i >= 0; i--) {
        const char *typeA = command.GetArgumentAtIndex(i);
        ConstString typeCS(typeA);

        if (!typeCS) {
          result.AppendError("empty category name not allowed");
          result.SetStatus(eReturnStatusFailed);
          return false;
        }
        DataVisualization::Categories::Disable(typeCS);
      }
    }

    if (m_options.m_language != lldb::eLanguageTypeUnknown)
      DataVisualization::Categories::Disable(m_options.m_language);

    result.SetStatus(eReturnStatusSuccessFinishResult);
    return result.Succeeded();
  }
};

//-------------------------------------------------------------------------
// CommandObjectTypeCategoryList
//-------------------------------------------------------------------------

class CommandObjectTypeCategoryList : public CommandObjectParsed {
public:
  CommandObjectTypeCategoryList(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "type category list",
                            "Provide a list of all existing categories.",
                            nullptr) {
    CommandArgumentEntry type_arg;
    CommandArgumentData type_style_arg;

    type_style_arg.arg_type = eArgTypeName;
    type_style_arg.arg_repetition = eArgRepeatOptional;

    type_arg.push_back(type_style_arg);

    m_arguments.push_back(type_arg);
  }

  ~CommandObjectTypeCategoryList() override = default;

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();

    std::unique_ptr<RegularExpression> regex;

    if (argc == 1) {
      regex.reset(new RegularExpression());
      const char *arg = command.GetArgumentAtIndex(0);
      if (!regex->Compile(llvm::StringRef::withNullAsEmpty(arg))) {
        result.AppendErrorWithFormat(
            "syntax error in category regular expression '%s'", arg);
        result.SetStatus(eReturnStatusFailed);
        return false;
      }
    } else if (argc != 0) {
      result.AppendErrorWithFormat("%s takes 0 or one arg.\n",
                                   m_cmd_name.c_str());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    DataVisualization::Categories::ForEach(
        [&regex, &result](const lldb::TypeCategoryImplSP &category_sp) -> bool {
          if (regex) {
            bool escape = true;
            if (regex->GetText() == category_sp->GetName()) {
              escape = false;
            } else if (regex->Execute(llvm::StringRef::withNullAsEmpty(
                           category_sp->GetName()))) {
              escape = false;
            }

            if (escape)
              return true;
          }

          result.GetOutputStream().Printf(
              "Category: %s\n", category_sp->GetDescription().c_str());

          return true;
        });

    result.SetStatus(eReturnStatusSuccessFinishResult);
    return result.Succeeded();
  }
};

//-------------------------------------------------------------------------
// CommandObjectTypeFilterList
//-------------------------------------------------------------------------

class CommandObjectTypeFilterList
    : public CommandObjectTypeFormatterList<TypeFilterImpl> {
public:
  CommandObjectTypeFilterList(CommandInterpreter &interpreter)
      : CommandObjectTypeFormatterList(interpreter, "type filter list",
                                       "Show a list of current filters.") {}
};

#ifndef LLDB_DISABLE_PYTHON

//-------------------------------------------------------------------------
// CommandObjectTypeSynthList
//-------------------------------------------------------------------------

class CommandObjectTypeSynthList
    : public CommandObjectTypeFormatterList<SyntheticChildren> {
public:
  CommandObjectTypeSynthList(CommandInterpreter &interpreter)
      : CommandObjectTypeFormatterList(
            interpreter, "type synthetic list",
            "Show a list of current synthetic providers.") {}
};

#endif // LLDB_DISABLE_PYTHON

//-------------------------------------------------------------------------
// CommandObjectTypeFilterDelete
//-------------------------------------------------------------------------

class CommandObjectTypeFilterDelete : public CommandObjectTypeFormatterDelete {
public:
  CommandObjectTypeFilterDelete(CommandInterpreter &interpreter)
      : CommandObjectTypeFormatterDelete(
            interpreter,
            eFormatCategoryItemFilter | eFormatCategoryItemRegexFilter,
            "type filter delete", "Delete an existing filter for a type.") {}

  ~CommandObjectTypeFilterDelete() override = default;
};

#ifndef LLDB_DISABLE_PYTHON

//-------------------------------------------------------------------------
// CommandObjectTypeSynthDelete
//-------------------------------------------------------------------------

class CommandObjectTypeSynthDelete : public CommandObjectTypeFormatterDelete {
public:
  CommandObjectTypeSynthDelete(CommandInterpreter &interpreter)
      : CommandObjectTypeFormatterDelete(
            interpreter,
            eFormatCategoryItemSynth | eFormatCategoryItemRegexSynth,
            "type synthetic delete",
            "Delete an existing synthetic provider for a type.") {}

  ~CommandObjectTypeSynthDelete() override = default;
};

#endif // LLDB_DISABLE_PYTHON

//-------------------------------------------------------------------------
// CommandObjectTypeFilterClear
//-------------------------------------------------------------------------

class CommandObjectTypeFilterClear : public CommandObjectTypeFormatterClear {
public:
  CommandObjectTypeFilterClear(CommandInterpreter &interpreter)
      : CommandObjectTypeFormatterClear(
            interpreter,
            eFormatCategoryItemFilter | eFormatCategoryItemRegexFilter,
            "type filter clear", "Delete all existing filter.") {}
};

#ifndef LLDB_DISABLE_PYTHON
//-------------------------------------------------------------------------
// CommandObjectTypeSynthClear
//-------------------------------------------------------------------------

class CommandObjectTypeSynthClear : public CommandObjectTypeFormatterClear {
public:
  CommandObjectTypeSynthClear(CommandInterpreter &interpreter)
      : CommandObjectTypeFormatterClear(
            interpreter,
            eFormatCategoryItemSynth | eFormatCategoryItemRegexSynth,
            "type synthetic clear",
            "Delete all existing synthetic providers.") {}
};

bool CommandObjectTypeSynthAdd::Execute_HandwritePython(
    Args &command, CommandReturnObject &result) {
  SynthAddOptions *options = new SynthAddOptions(
      m_options.m_skip_pointers, m_options.m_skip_references,
      m_options.m_cascade, m_options.m_regex, m_options.m_category);

  for (auto &entry : command.entries()) {
    if (entry.ref.empty()) {
      result.AppendError("empty typenames not allowed");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    options->m_target_types << entry.ref;
  }

  m_interpreter.GetPythonCommandsFromIOHandler(
      "    ",   // Prompt
      *this,    // IOHandlerDelegate
      true,     // Run IOHandler in async mode
      options); // Baton for the "io_handler" that will be passed back into our
                // IOHandlerDelegate functions
  result.SetStatus(eReturnStatusSuccessFinishNoResult);
  return result.Succeeded();
}

bool CommandObjectTypeSynthAdd::Execute_PythonClass(
    Args &command, CommandReturnObject &result) {
  const size_t argc = command.GetArgumentCount();

  if (argc < 1) {
    result.AppendErrorWithFormat("%s takes one or more args.\n",
                                 m_cmd_name.c_str());
    result.SetStatus(eReturnStatusFailed);
    return false;
  }

  if (m_options.m_class_name.empty() && !m_options.m_input_python) {
    result.AppendErrorWithFormat("%s needs either a Python class name or -P to "
                                 "directly input Python code.\n",
                                 m_cmd_name.c_str());
    result.SetStatus(eReturnStatusFailed);
    return false;
  }

  SyntheticChildrenSP entry;

  ScriptedSyntheticChildren *impl = new ScriptedSyntheticChildren(
      SyntheticChildren::Flags()
          .SetCascades(m_options.m_cascade)
          .SetSkipPointers(m_options.m_skip_pointers)
          .SetSkipReferences(m_options.m_skip_references),
      m_options.m_class_name.c_str());

  entry.reset(impl);

  ScriptInterpreter *interpreter = m_interpreter.GetScriptInterpreter();

  if (interpreter &&
      !interpreter->CheckObjectExists(impl->GetPythonClassName()))
    result.AppendWarning("The provided class does not exist - please define it "
                         "before attempting to use this synthetic provider");

  // now I have a valid provider, let's add it to every type

  lldb::TypeCategoryImplSP category;
  DataVisualization::Categories::GetCategory(
      ConstString(m_options.m_category.c_str()), category);

  Status error;

  for (auto &arg_entry : command.entries()) {
    if (arg_entry.ref.empty()) {
      result.AppendError("empty typenames not allowed");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    ConstString typeCS(arg_entry.ref);
    if (!AddSynth(typeCS, entry,
                  m_options.m_regex ? eRegexSynth : eRegularSynth,
                  m_options.m_category, &error)) {
      result.AppendError(error.AsCString());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }
  }

  result.SetStatus(eReturnStatusSuccessFinishNoResult);
  return result.Succeeded();
}

CommandObjectTypeSynthAdd::CommandObjectTypeSynthAdd(
    CommandInterpreter &interpreter)
    : CommandObjectParsed(interpreter, "type synthetic add",
                          "Add a new synthetic provider for a type.", nullptr),
      IOHandlerDelegateMultiline("DONE"), m_options() {
  CommandArgumentEntry type_arg;
  CommandArgumentData type_style_arg;

  type_style_arg.arg_type = eArgTypeName;
  type_style_arg.arg_repetition = eArgRepeatPlus;

  type_arg.push_back(type_style_arg);

  m_arguments.push_back(type_arg);
}

bool CommandObjectTypeSynthAdd::AddSynth(ConstString type_name,
                                         SyntheticChildrenSP entry,
                                         SynthFormatType type,
                                         std::string category_name,
                                         Status *error) {
  lldb::TypeCategoryImplSP category;
  DataVisualization::Categories::GetCategory(ConstString(category_name.c_str()),
                                             category);

  if (type == eRegularSynth) {
    if (FixArrayTypeNameWithRegex(type_name))
      type = eRegexSynth;
  }

  if (category->AnyMatches(type_name, eFormatCategoryItemFilter |
                                          eFormatCategoryItemRegexFilter,
                           false)) {
    if (error)
      error->SetErrorStringWithFormat("cannot add synthetic for type %s when "
                                      "filter is defined in same category!",
                                      type_name.AsCString());
    return false;
  }

  if (type == eRegexSynth) {
    RegularExpressionSP typeRX(new RegularExpression());
    if (!typeRX->Compile(type_name.GetStringRef())) {
      if (error)
        error->SetErrorString(
            "regex format error (maybe this is not really a regex?)");
      return false;
    }

    category->GetRegexTypeSyntheticsContainer()->Delete(type_name);
    category->GetRegexTypeSyntheticsContainer()->Add(typeRX, entry);

    return true;
  } else {
    category->GetTypeSyntheticsContainer()->Add(type_name, entry);
    return true;
  }
}

#endif // LLDB_DISABLE_PYTHON

static constexpr OptionDefinition g_type_filter_add_options[] = {
    // clang-format off
  { LLDB_OPT_SET_ALL, false, "cascade",         'C', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeBoolean,        "If true, cascade through typedef chains." },
  { LLDB_OPT_SET_ALL, false, "skip-pointers",   'p', OptionParser::eNoArgument,       nullptr, {}, 0, eArgTypeNone,           "Don't use this format for pointers-to-type objects." },
  { LLDB_OPT_SET_ALL, false, "skip-references", 'r', OptionParser::eNoArgument,       nullptr, {}, 0, eArgTypeNone,           "Don't use this format for references-to-type objects." },
  { LLDB_OPT_SET_ALL, false, "category",        'w', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeName,           "Add this to the given category instead of the default one." },
  { LLDB_OPT_SET_ALL, false, "child",           'c', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeExpressionPath, "Include this expression path in the synthetic view." },
  { LLDB_OPT_SET_ALL, false, "regex",           'x', OptionParser::eNoArgument,       nullptr, {}, 0, eArgTypeNone,           "Type names are actually regular expressions." }
    // clang-format on
};

class CommandObjectTypeFilterAdd : public CommandObjectParsed {
private:
  class CommandOptions : public Options {
    typedef std::vector<std::string> option_vector;

  public:
    CommandOptions() : Options() {}

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;
      bool success;

      switch (short_option) {
      case 'C':
        m_cascade = OptionArgParser::ToBoolean(option_arg, true, &success);
        if (!success)
          error.SetErrorStringWithFormat("invalid value for cascade: %s",
                                         option_arg.str().c_str());
        break;
      case 'c':
        m_expr_paths.push_back(option_arg);
        has_child_list = true;
        break;
      case 'p':
        m_skip_pointers = true;
        break;
      case 'r':
        m_skip_references = true;
        break;
      case 'w':
        m_category = std::string(option_arg);
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
      m_cascade = true;
      m_skip_pointers = false;
      m_skip_references = false;
      m_category = "default";
      m_expr_paths.clear();
      has_child_list = false;
      m_regex = false;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_type_filter_add_options);
    }

    // Instance variables to hold the values for command options.

    bool m_cascade;
    bool m_skip_references;
    bool m_skip_pointers;
    bool m_input_python;
    option_vector m_expr_paths;
    std::string m_category;
    bool has_child_list;
    bool m_regex;

    typedef option_vector::iterator ExpressionPathsIterator;
  };

  CommandOptions m_options;

  Options *GetOptions() override { return &m_options; }

  enum FilterFormatType { eRegularFilter, eRegexFilter };

  bool AddFilter(ConstString type_name, TypeFilterImplSP entry,
                 FilterFormatType type, std::string category_name,
                 Status *error) {
    lldb::TypeCategoryImplSP category;
    DataVisualization::Categories::GetCategory(
        ConstString(category_name.c_str()), category);

    if (type == eRegularFilter) {
      if (FixArrayTypeNameWithRegex(type_name))
        type = eRegexFilter;
    }

    if (category->AnyMatches(type_name, eFormatCategoryItemSynth |
                                            eFormatCategoryItemRegexSynth,
                             false)) {
      if (error)
        error->SetErrorStringWithFormat("cannot add filter for type %s when "
                                        "synthetic is defined in same "
                                        "category!",
                                        type_name.AsCString());
      return false;
    }

    if (type == eRegexFilter) {
      RegularExpressionSP typeRX(new RegularExpression());
      if (!typeRX->Compile(type_name.GetStringRef())) {
        if (error)
          error->SetErrorString(
              "regex format error (maybe this is not really a regex?)");
        return false;
      }

      category->GetRegexTypeFiltersContainer()->Delete(type_name);
      category->GetRegexTypeFiltersContainer()->Add(typeRX, entry);

      return true;
    } else {
      category->GetTypeFiltersContainer()->Add(type_name, entry);
      return true;
    }
  }

public:
  CommandObjectTypeFilterAdd(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "type filter add",
                            "Add a new filter for a type.", nullptr),
        m_options() {
    CommandArgumentEntry type_arg;
    CommandArgumentData type_style_arg;

    type_style_arg.arg_type = eArgTypeName;
    type_style_arg.arg_repetition = eArgRepeatPlus;

    type_arg.push_back(type_style_arg);

    m_arguments.push_back(type_arg);

    SetHelpLong(
        R"(
The following examples of 'type filter add' refer to this code snippet for context:

    class Foo {
        int a;
        int b;
        int c;
        int d;
        int e;
        int f;
        int g;
        int h;
        int i;
    }
    Foo my_foo;

Adding a simple filter:

(lldb) type filter add --child a --child g Foo
(lldb) frame variable my_foo

)"
        "Produces output where only a and g are displayed.  Other children of my_foo \
(b, c, d, e, f, h and i) are available by asking for them explicitly:"
        R"(

(lldb) frame variable my_foo.b my_foo.c my_foo.i

)"
        "The formatting option --raw on frame variable bypasses the filter, showing \
all children of my_foo as if no filter was defined:"
        R"(

(lldb) frame variable my_foo --raw)");
  }

  ~CommandObjectTypeFilterAdd() override = default;

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();

    if (argc < 1) {
      result.AppendErrorWithFormat("%s takes one or more args.\n",
                                   m_cmd_name.c_str());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    if (m_options.m_expr_paths.empty()) {
      result.AppendErrorWithFormat("%s needs one or more children.\n",
                                   m_cmd_name.c_str());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    TypeFilterImplSP entry(new TypeFilterImpl(
        SyntheticChildren::Flags()
            .SetCascades(m_options.m_cascade)
            .SetSkipPointers(m_options.m_skip_pointers)
            .SetSkipReferences(m_options.m_skip_references)));

    // go through the expression paths
    CommandOptions::ExpressionPathsIterator begin,
        end = m_options.m_expr_paths.end();

    for (begin = m_options.m_expr_paths.begin(); begin != end; begin++)
      entry->AddExpressionPath(*begin);

    // now I have a valid provider, let's add it to every type

    lldb::TypeCategoryImplSP category;
    DataVisualization::Categories::GetCategory(
        ConstString(m_options.m_category.c_str()), category);

    Status error;

    WarnOnPotentialUnquotedUnsignedType(command, result);

    for (auto &arg_entry : command.entries()) {
      if (arg_entry.ref.empty()) {
        result.AppendError("empty typenames not allowed");
        result.SetStatus(eReturnStatusFailed);
        return false;
      }

      ConstString typeCS(arg_entry.ref);
      if (!AddFilter(typeCS, entry,
                     m_options.m_regex ? eRegexFilter : eRegularFilter,
                     m_options.m_category, &error)) {
        result.AppendError(error.AsCString());
        result.SetStatus(eReturnStatusFailed);
        return false;
      }
    }

    result.SetStatus(eReturnStatusSuccessFinishNoResult);
    return result.Succeeded();
  }
};

//----------------------------------------------------------------------
// "type lookup"
//----------------------------------------------------------------------
static constexpr OptionDefinition g_type_lookup_options[] = {
    // clang-format off
  { LLDB_OPT_SET_ALL, false, "show-help", 'h', OptionParser::eNoArgument,       nullptr, {}, 0, eArgTypeNone,     "Display available help for types" },
  { LLDB_OPT_SET_ALL, false, "language",  'l', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeLanguage, "Which language's types should the search scope be" }
    // clang-format on
};

class CommandObjectTypeLookup : public CommandObjectRaw {
protected:
  // this function is allowed to do a more aggressive job at guessing languages
  // than the expression parser is comfortable with - so leave the original
  // call alone and add one that is specific to type lookup
  lldb::LanguageType GuessLanguage(StackFrame *frame) {
    lldb::LanguageType lang_type = lldb::eLanguageTypeUnknown;

    if (!frame)
      return lang_type;

    lang_type = frame->GuessLanguage();
    if (lang_type != lldb::eLanguageTypeUnknown)
      return lang_type;

    Symbol *s = frame->GetSymbolContext(eSymbolContextSymbol).symbol;
    if (s)
      lang_type = s->GetMangled().GuessLanguage();

    return lang_type;
  }

  class CommandOptions : public OptionGroup {
  public:
    CommandOptions()
        : OptionGroup(), m_show_help(false), m_language(eLanguageTypeUnknown) {}

    ~CommandOptions() override = default;

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_type_lookup_options);
    }

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_value,
                          ExecutionContext *execution_context) override {
      Status error;

      const int short_option = g_type_lookup_options[option_idx].short_option;

      switch (short_option) {
      case 'h':
        m_show_help = true;
        break;

      case 'l':
        m_language = Language::GetLanguageTypeFromString(option_value);
        break;

      default:
        error.SetErrorStringWithFormat("invalid short option character '%c'",
                                       short_option);
        break;
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_show_help = false;
      m_language = eLanguageTypeUnknown;
    }

    // Options table: Required for subclasses of Options.

    bool m_show_help;
    lldb::LanguageType m_language;
  };

  OptionGroupOptions m_option_group;
  CommandOptions m_command_options;

public:
  CommandObjectTypeLookup(CommandInterpreter &interpreter)
      : CommandObjectRaw(interpreter, "type lookup",
                         "Lookup types and declarations in the current target, "
                         "following language-specific naming conventions.",
                         "type lookup <type-specifier>",
                         eCommandRequiresTarget),
        m_option_group(), m_command_options() {
    m_option_group.Append(&m_command_options);
    m_option_group.Finalize();
  }

  ~CommandObjectTypeLookup() override = default;

  Options *GetOptions() override { return &m_option_group; }

  llvm::StringRef GetHelpLong() override {
    if (!m_cmd_help_long.empty())
      return m_cmd_help_long;

    StreamString stream;
    // FIXME: hardcoding languages is not good
    lldb::LanguageType languages[] = {eLanguageTypeObjC,
                                      eLanguageTypeC_plus_plus};

    for (const auto lang_type : languages) {
      if (auto language = Language::FindPlugin(lang_type)) {
        if (const char *help = language->GetLanguageSpecificTypeLookupHelp()) {
          stream.Printf("%s\n", help);
        }
      }
    }

    m_cmd_help_long = stream.GetString();
    return m_cmd_help_long;
  }

  bool DoExecute(llvm::StringRef raw_command_line,
                 CommandReturnObject &result) override {
    if (raw_command_line.empty()) {
      result.SetError(
          "type lookup cannot be invoked without a type name as argument");
      return false;
    }

    auto exe_ctx = GetCommandInterpreter().GetExecutionContext();
    m_option_group.NotifyOptionParsingStarting(&exe_ctx);

    OptionsWithRaw args(raw_command_line);
    const char *name_of_type = args.GetRawPart().c_str();

    if (args.HasArgs())
      if (!ParseOptionsAndNotify(args.GetArgs(), result, m_option_group,
                                 exe_ctx))
        return false;

    // TargetSP
    // target_sp(GetCommandInterpreter().GetDebugger().GetSelectedTarget());
    // const bool fill_all_in = true;
    // ExecutionContext exe_ctx(target_sp.get(), fill_all_in);
    ExecutionContextScope *best_scope = exe_ctx.GetBestExecutionContextScope();

    bool any_found = false;

    std::vector<Language *> languages;

    bool is_global_search = false;
    LanguageType guessed_language = lldb::eLanguageTypeUnknown;

    if ((is_global_search =
             (m_command_options.m_language == eLanguageTypeUnknown))) {
      // FIXME: hardcoding languages is not good
      languages.push_back(Language::FindPlugin(eLanguageTypeObjC));
      languages.push_back(Language::FindPlugin(eLanguageTypeC_plus_plus));
    } else {
      languages.push_back(Language::FindPlugin(m_command_options.m_language));
    }

    // This is not the most efficient way to do this, but we support very few
    // languages so the cost of the sort is going to be dwarfed by the actual
    // lookup anyway
    if (StackFrame *frame = m_exe_ctx.GetFramePtr()) {
      guessed_language = GuessLanguage(frame);
      if (guessed_language != eLanguageTypeUnknown) {
        llvm::sort(
            languages.begin(), languages.end(),
            [guessed_language](Language *lang1, Language *lang2) -> bool {
              if (!lang1 || !lang2)
                return false;
              LanguageType lt1 = lang1->GetLanguageType();
              LanguageType lt2 = lang2->GetLanguageType();
              if (lt1 == guessed_language)
                return true; // make the selected frame's language come first
              if (lt2 == guessed_language)
                return false; // make the selected frame's language come first
              return (lt1 < lt2); // normal comparison otherwise
            });
      }
    }

    bool is_first_language = true;

    for (Language *language : languages) {
      if (!language)
        continue;

      if (auto scavenger = language->GetTypeScavenger()) {
        Language::TypeScavenger::ResultSet search_results;
        if (scavenger->Find(best_scope, name_of_type, search_results) > 0) {
          for (const auto &search_result : search_results) {
            if (search_result && search_result->IsValid()) {
              any_found = true;
              search_result->DumpToStream(result.GetOutputStream(),
                                          this->m_command_options.m_show_help);
            }
          }
        }
      }
      // this is "type lookup SomeName" and we did find a match, so get out
      if (any_found && is_global_search)
        break;
      else if (is_first_language && is_global_search &&
               guessed_language != lldb::eLanguageTypeUnknown) {
        is_first_language = false;
        result.GetOutputStream().Printf(
            "no type was found in the current language %s matching '%s'; "
            "performing a global search across all languages\n",
            Language::GetNameForLanguageType(guessed_language), name_of_type);
      }
    }

    if (!any_found)
      result.AppendMessageWithFormat("no type was found matching '%s'\n",
                                     name_of_type);

    result.SetStatus(any_found ? lldb::eReturnStatusSuccessFinishResult
                               : lldb::eReturnStatusSuccessFinishNoResult);
    return true;
  }
};

template <typename FormatterType>
class CommandObjectFormatterInfo : public CommandObjectRaw {
public:
  typedef std::function<typename FormatterType::SharedPointer(ValueObject &)>
      DiscoveryFunction;
  CommandObjectFormatterInfo(CommandInterpreter &interpreter,
                             const char *formatter_name,
                             DiscoveryFunction discovery_func)
      : CommandObjectRaw(interpreter, "", "", "",
                         eCommandRequiresFrame),
        m_formatter_name(formatter_name ? formatter_name : ""),
        m_discovery_function(discovery_func) {
    StreamString name;
    name.Printf("type %s info", formatter_name);
    SetCommandName(name.GetString());
    StreamString help;
    help.Printf("This command evaluates the provided expression and shows "
                "which %s is applied to the resulting value (if any).",
                formatter_name);
    SetHelp(help.GetString());
    StreamString syntax;
    syntax.Printf("type %s info <expr>", formatter_name);
    SetSyntax(syntax.GetString());
  }

  ~CommandObjectFormatterInfo() override = default;

protected:
  bool DoExecute(llvm::StringRef command,
                 CommandReturnObject &result) override {
    TargetSP target_sp = m_interpreter.GetDebugger().GetSelectedTarget();
    Thread *thread = GetDefaultThread();
    if (!thread) {
      result.AppendError("no default thread");
      result.SetStatus(lldb::eReturnStatusFailed);
      return false;
    }

    StackFrameSP frame_sp = thread->GetSelectedFrame();
    ValueObjectSP result_valobj_sp;
    EvaluateExpressionOptions options;
    lldb::ExpressionResults expr_result = target_sp->EvaluateExpression(
        command, frame_sp.get(), result_valobj_sp, options);
    if (expr_result == eExpressionCompleted && result_valobj_sp) {
      result_valobj_sp =
          result_valobj_sp->GetQualifiedRepresentationIfAvailable(
              target_sp->GetPreferDynamicValue(),
              target_sp->GetEnableSyntheticValue());
      typename FormatterType::SharedPointer formatter_sp =
          m_discovery_function(*result_valobj_sp);
      if (formatter_sp) {
        std::string description(formatter_sp->GetDescription());
        result.GetOutputStream()
            << m_formatter_name << " applied to ("
            << result_valobj_sp->GetDisplayTypeName().AsCString("<unknown>")
            << ") " << command << " is: " << description << "\n";
        result.SetStatus(lldb::eReturnStatusSuccessFinishResult);
      } else {
        result.GetOutputStream()
            << "no " << m_formatter_name << " applies to ("
            << result_valobj_sp->GetDisplayTypeName().AsCString("<unknown>")
            << ") " << command << "\n";
        result.SetStatus(lldb::eReturnStatusSuccessFinishNoResult);
      }
      return true;
    } else {
      result.AppendError("failed to evaluate expression");
      result.SetStatus(lldb::eReturnStatusFailed);
      return false;
    }
  }

private:
  std::string m_formatter_name;
  DiscoveryFunction m_discovery_function;
};

class CommandObjectTypeFormat : public CommandObjectMultiword {
public:
  CommandObjectTypeFormat(CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "type format",
            "Commands for customizing value display formats.",
            "type format [<sub-command-options>] ") {
    LoadSubCommand(
        "add", CommandObjectSP(new CommandObjectTypeFormatAdd(interpreter)));
    LoadSubCommand("clear", CommandObjectSP(
                                new CommandObjectTypeFormatClear(interpreter)));
    LoadSubCommand("delete", CommandObjectSP(new CommandObjectTypeFormatDelete(
                                 interpreter)));
    LoadSubCommand(
        "list", CommandObjectSP(new CommandObjectTypeFormatList(interpreter)));
    LoadSubCommand(
        "info", CommandObjectSP(new CommandObjectFormatterInfo<TypeFormatImpl>(
                    interpreter, "format",
                    [](ValueObject &valobj) -> TypeFormatImpl::SharedPointer {
                      return valobj.GetValueFormat();
                    })));
  }

  ~CommandObjectTypeFormat() override = default;
};

#ifndef LLDB_DISABLE_PYTHON

class CommandObjectTypeSynth : public CommandObjectMultiword {
public:
  CommandObjectTypeSynth(CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "type synthetic",
            "Commands for operating on synthetic type representations.",
            "type synthetic [<sub-command-options>] ") {
    LoadSubCommand("add",
                   CommandObjectSP(new CommandObjectTypeSynthAdd(interpreter)));
    LoadSubCommand(
        "clear", CommandObjectSP(new CommandObjectTypeSynthClear(interpreter)));
    LoadSubCommand("delete", CommandObjectSP(new CommandObjectTypeSynthDelete(
                                 interpreter)));
    LoadSubCommand(
        "list", CommandObjectSP(new CommandObjectTypeSynthList(interpreter)));
    LoadSubCommand(
        "info",
        CommandObjectSP(new CommandObjectFormatterInfo<SyntheticChildren>(
            interpreter, "synthetic",
            [](ValueObject &valobj) -> SyntheticChildren::SharedPointer {
              return valobj.GetSyntheticChildren();
            })));
  }

  ~CommandObjectTypeSynth() override = default;
};

#endif // LLDB_DISABLE_PYTHON

class CommandObjectTypeFilter : public CommandObjectMultiword {
public:
  CommandObjectTypeFilter(CommandInterpreter &interpreter)
      : CommandObjectMultiword(interpreter, "type filter",
                               "Commands for operating on type filters.",
                               "type synthetic [<sub-command-options>] ") {
    LoadSubCommand(
        "add", CommandObjectSP(new CommandObjectTypeFilterAdd(interpreter)));
    LoadSubCommand("clear", CommandObjectSP(
                                new CommandObjectTypeFilterClear(interpreter)));
    LoadSubCommand("delete", CommandObjectSP(new CommandObjectTypeFilterDelete(
                                 interpreter)));
    LoadSubCommand(
        "list", CommandObjectSP(new CommandObjectTypeFilterList(interpreter)));
  }

  ~CommandObjectTypeFilter() override = default;
};

class CommandObjectTypeCategory : public CommandObjectMultiword {
public:
  CommandObjectTypeCategory(CommandInterpreter &interpreter)
      : CommandObjectMultiword(interpreter, "type category",
                               "Commands for operating on type categories.",
                               "type category [<sub-command-options>] ") {
    LoadSubCommand(
        "define",
        CommandObjectSP(new CommandObjectTypeCategoryDefine(interpreter)));
    LoadSubCommand(
        "enable",
        CommandObjectSP(new CommandObjectTypeCategoryEnable(interpreter)));
    LoadSubCommand(
        "disable",
        CommandObjectSP(new CommandObjectTypeCategoryDisable(interpreter)));
    LoadSubCommand(
        "delete",
        CommandObjectSP(new CommandObjectTypeCategoryDelete(interpreter)));
    LoadSubCommand("list", CommandObjectSP(
                               new CommandObjectTypeCategoryList(interpreter)));
  }

  ~CommandObjectTypeCategory() override = default;
};

class CommandObjectTypeSummary : public CommandObjectMultiword {
public:
  CommandObjectTypeSummary(CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "type summary",
            "Commands for editing variable summary display options.",
            "type summary [<sub-command-options>] ") {
    LoadSubCommand(
        "add", CommandObjectSP(new CommandObjectTypeSummaryAdd(interpreter)));
    LoadSubCommand("clear", CommandObjectSP(new CommandObjectTypeSummaryClear(
                                interpreter)));
    LoadSubCommand("delete", CommandObjectSP(new CommandObjectTypeSummaryDelete(
                                 interpreter)));
    LoadSubCommand(
        "list", CommandObjectSP(new CommandObjectTypeSummaryList(interpreter)));
    LoadSubCommand(
        "info", CommandObjectSP(new CommandObjectFormatterInfo<TypeSummaryImpl>(
                    interpreter, "summary",
                    [](ValueObject &valobj) -> TypeSummaryImpl::SharedPointer {
                      return valobj.GetSummaryFormat();
                    })));
  }

  ~CommandObjectTypeSummary() override = default;
};

//-------------------------------------------------------------------------
// CommandObjectType
//-------------------------------------------------------------------------

CommandObjectType::CommandObjectType(CommandInterpreter &interpreter)
    : CommandObjectMultiword(interpreter, "type",
                             "Commands for operating on the type system.",
                             "type [<sub-command-options>]") {
  LoadSubCommand("category",
                 CommandObjectSP(new CommandObjectTypeCategory(interpreter)));
  LoadSubCommand("filter",
                 CommandObjectSP(new CommandObjectTypeFilter(interpreter)));
  LoadSubCommand("format",
                 CommandObjectSP(new CommandObjectTypeFormat(interpreter)));
  LoadSubCommand("summary",
                 CommandObjectSP(new CommandObjectTypeSummary(interpreter)));
#ifndef LLDB_DISABLE_PYTHON
  LoadSubCommand("synthetic",
                 CommandObjectSP(new CommandObjectTypeSynth(interpreter)));
#endif // LLDB_DISABLE_PYTHON
  LoadSubCommand("lookup",
                 CommandObjectSP(new CommandObjectTypeLookup(interpreter)));
}

CommandObjectType::~CommandObjectType() = default;
