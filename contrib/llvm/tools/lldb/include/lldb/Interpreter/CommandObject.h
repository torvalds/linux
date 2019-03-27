//===-- CommandObject.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CommandObject_h_
#define liblldb_CommandObject_h_

#include <map>
#include <string>
#include <vector>

#include "lldb/Utility/Flags.h"

#include "lldb/Interpreter/CommandCompletions.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/CompletionRequest.h"
#include "lldb/Utility/StringList.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

// This function really deals with CommandObjectLists, but we didn't make a
// CommandObjectList class, so I'm sticking it here.  But we really should have
// such a class.  Anyway, it looks up the commands in the map that match the
// partial string cmd_str, inserts the matches into matches, and returns the
// number added.

template <typename ValueType>
int AddNamesMatchingPartialString(
    const std::map<std::string, ValueType> &in_map, llvm::StringRef cmd_str,
    StringList &matches, StringList *descriptions = nullptr) {
  int number_added = 0;

  const bool add_all = cmd_str.empty();

  for (auto iter = in_map.begin(), end = in_map.end(); iter != end; iter++) {
    if (add_all || (iter->first.find(cmd_str, 0) == 0)) {
      ++number_added;
      matches.AppendString(iter->first.c_str());
      if (descriptions)
        descriptions->AppendString(iter->second->GetHelp());
    }
  }

  return number_added;
}

template <typename ValueType>
size_t FindLongestCommandWord(std::map<std::string, ValueType> &dict) {
  auto end = dict.end();
  size_t max_len = 0;

  for (auto pos = dict.begin(); pos != end; ++pos) {
    size_t len = pos->first.size();
    if (max_len < len)
      max_len = len;
  }
  return max_len;
}

class CommandObject {
public:
  typedef llvm::StringRef(ArgumentHelpCallbackFunction)();

  struct ArgumentHelpCallback {
    ArgumentHelpCallbackFunction *help_callback;
    bool self_formatting;

    llvm::StringRef operator()() const { return (*help_callback)(); }

    explicit operator bool() const { return (help_callback != nullptr); }
  };

  struct ArgumentTableEntry // Entries in the main argument information table
  {
    lldb::CommandArgumentType arg_type;
    const char *arg_name;
    CommandCompletions::CommonCompletionTypes completion_type;
    ArgumentHelpCallback help_function;
    const char *help_text;
  };

  struct CommandArgumentData // Used to build individual command argument lists
  {
    lldb::CommandArgumentType arg_type;
    ArgumentRepetitionType arg_repetition;
    uint32_t arg_opt_set_association; // This arg might be associated only with
                                      // some particular option set(s).
    CommandArgumentData()
        : arg_type(lldb::eArgTypeNone), arg_repetition(eArgRepeatPlain),
          arg_opt_set_association(LLDB_OPT_SET_ALL) // By default, the arg
                                                    // associates to all option
                                                    // sets.
    {}
  };

  typedef std::vector<CommandArgumentData>
      CommandArgumentEntry; // Used to build individual command argument lists

  static ArgumentTableEntry g_arguments_data
      [lldb::eArgTypeLastArg]; // Main argument information table

  typedef std::map<std::string, lldb::CommandObjectSP> CommandMap;

  CommandObject(CommandInterpreter &interpreter, llvm::StringRef name,
    llvm::StringRef help = "", llvm::StringRef syntax = "",
                uint32_t flags = 0);

  virtual ~CommandObject();

  static const char *
  GetArgumentTypeAsCString(const lldb::CommandArgumentType arg_type);

  static const char *
  GetArgumentDescriptionAsCString(const lldb::CommandArgumentType arg_type);

  CommandInterpreter &GetCommandInterpreter() { return m_interpreter; }

  virtual llvm::StringRef GetHelp();

  virtual llvm::StringRef GetHelpLong();

  virtual llvm::StringRef GetSyntax();

  llvm::StringRef GetCommandName() const;

  virtual void SetHelp(llvm::StringRef str);

  virtual void SetHelpLong(llvm::StringRef str);

  void SetSyntax(llvm::StringRef str);

  // override this to return true if you want to enable the user to delete the
  // Command object from the Command dictionary (aliases have their own
  // deletion scheme, so they do not need to care about this)
  virtual bool IsRemovable() const { return false; }

  virtual bool IsMultiwordObject() { return false; }

  virtual CommandObjectMultiword *GetAsMultiwordCommand() { return nullptr; }

  virtual bool IsAlias() { return false; }

  // override this to return true if your command is somehow a "dash-dash" form
  // of some other command (e.g. po is expr -O --); this is a powerful hint to
  // the help system that one cannot pass options to this command
  virtual bool IsDashDashCommand() { return false; }

  virtual lldb::CommandObjectSP GetSubcommandSP(llvm::StringRef sub_cmd,
                                                StringList *matches = nullptr) {
    return lldb::CommandObjectSP();
  }

  virtual CommandObject *GetSubcommandObject(llvm::StringRef sub_cmd,
                                             StringList *matches = nullptr) {
    return nullptr;
  }

  virtual void AproposAllSubCommands(llvm::StringRef prefix,
                                     llvm::StringRef search_word,
                                     StringList &commands_found,
                                     StringList &commands_help) {}

  void FormatLongHelpText(Stream &output_strm, llvm::StringRef long_help);

  void GenerateHelpText(CommandReturnObject &result);

  virtual void GenerateHelpText(Stream &result);

  // this is needed in order to allow the SBCommand class to transparently try
  // and load subcommands - it will fail on anything but a multiword command,
  // but it avoids us doing type checkings and casts
  virtual bool LoadSubCommand(llvm::StringRef cmd_name,
                              const lldb::CommandObjectSP &command_obj) {
    return false;
  }

  virtual bool WantsRawCommandString() = 0;

  // By default, WantsCompletion = !WantsRawCommandString. Subclasses who want
  // raw command string but desire, for example, argument completion should
  // override this method to return true.
  virtual bool WantsCompletion() { return !WantsRawCommandString(); }

  virtual Options *GetOptions();

  static const ArgumentTableEntry *GetArgumentTable();

  static lldb::CommandArgumentType LookupArgumentName(llvm::StringRef arg_name);

  static const ArgumentTableEntry *
  FindArgumentDataByType(lldb::CommandArgumentType arg_type);

  int GetNumArgumentEntries();

  CommandArgumentEntry *GetArgumentEntryAtIndex(int idx);

  static void GetArgumentHelp(Stream &str, lldb::CommandArgumentType arg_type,
                              CommandInterpreter &interpreter);

  static const char *GetArgumentName(lldb::CommandArgumentType arg_type);

  // Generates a nicely formatted command args string for help command output.
  // By default, all possible args are taken into account, for example, '<expr
  // | variable-name>'.  This can be refined by passing a second arg specifying
  // which option set(s) we are interested, which could then, for example,
  // produce either '<expr>' or '<variable-name>'.
  void GetFormattedCommandArguments(Stream &str,
                                    uint32_t opt_set_mask = LLDB_OPT_SET_ALL);

  bool IsPairType(ArgumentRepetitionType arg_repeat_type);

  bool ParseOptions(Args &args, CommandReturnObject &result);

  void SetCommandName(llvm::StringRef name);

  //------------------------------------------------------------------
  /// This default version handles calling option argument completions and then
  /// calls HandleArgumentCompletion if the cursor is on an argument, not an
  /// option. Don't override this method, override HandleArgumentCompletion
  /// instead unless you have special reasons.
  ///
  /// @param[in/out] request
  ///    The completion request that needs to be answered.
  ///
  /// FIXME: This is the wrong return value, since we also need to make a
  /// distinction between
  /// total number of matches, and the window the user wants returned.
  ///
  /// @return
  ///     \btrue if we were in an option, \bfalse otherwise.
  //------------------------------------------------------------------
  virtual int HandleCompletion(CompletionRequest &request);

  //------------------------------------------------------------------
  /// The input array contains a parsed version of the line.  The insertion
  /// point is given by cursor_index (the index in input of the word containing
  /// the cursor) and cursor_char_position (the position of the cursor in that
  /// word.)
  /// We've constructed the map of options and their arguments as well if that
  /// is helpful for the completion.
  ///
  /// @param[in/out] request
  ///    The completion request that needs to be answered.
  ///
  /// FIXME: This is the wrong return value, since we also need to make a
  /// distinction between
  /// total number of matches, and the window the user wants returned.
  ///
  /// @return
  ///     The number of completions.
  //------------------------------------------------------------------
  virtual int
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) {
    return 0;
  }

  bool HelpTextContainsWord(llvm::StringRef search_word,
                            bool search_short_help = true,
                            bool search_long_help = true,
                            bool search_syntax = true,
                            bool search_options = true);

  //------------------------------------------------------------------
  /// The flags accessor.
  ///
  /// @return
  ///     A reference to the Flags member variable.
  //------------------------------------------------------------------
  Flags &GetFlags() { return m_flags; }

  //------------------------------------------------------------------
  /// The flags const accessor.
  ///
  /// @return
  ///     A const reference to the Flags member variable.
  //------------------------------------------------------------------
  const Flags &GetFlags() const { return m_flags; }

  //------------------------------------------------------------------
  /// Get the command that appropriate for a "repeat" of the current command.
  ///
  /// @param[in] current_command_line
  ///    The complete current command line.
  ///
  /// @return
  ///     nullptr if there is no special repeat command - it will use the
  ///     current command line.
  ///     Otherwise a pointer to the command to be repeated.
  ///     If the returned string is the empty string, the command won't be
  ///     repeated.
  //------------------------------------------------------------------
  virtual const char *GetRepeatCommand(Args &current_command_args,
                                       uint32_t index) {
    return nullptr;
  }

  bool HasOverrideCallback() const {
    return m_command_override_callback ||
           m_deprecated_command_override_callback;
  }

  void SetOverrideCallback(lldb::CommandOverrideCallback callback,
                           void *baton) {
    m_deprecated_command_override_callback = callback;
    m_command_override_baton = baton;
  }

  void SetOverrideCallback(lldb::CommandOverrideCallbackWithResult callback,
                           void *baton) {
    m_command_override_callback = callback;
    m_command_override_baton = baton;
  }

  bool InvokeOverrideCallback(const char **argv, CommandReturnObject &result) {
    if (m_command_override_callback)
      return m_command_override_callback(m_command_override_baton, argv,
                                         result);
    else if (m_deprecated_command_override_callback)
      return m_deprecated_command_override_callback(m_command_override_baton,
                                                    argv);
    else
      return false;
  }

  virtual bool Execute(const char *args_string,
                       CommandReturnObject &result) = 0;

protected:
  bool ParseOptionsAndNotify(Args &args, CommandReturnObject &result,
                             OptionGroupOptions &group_options,
                             ExecutionContext &exe_ctx);

  virtual const char *GetInvalidTargetDescription() {
    return "invalid target, create a target using the 'target create' command";
  }

  virtual const char *GetInvalidProcessDescription() {
    return "invalid process";
  }

  virtual const char *GetInvalidThreadDescription() { return "invalid thread"; }

  virtual const char *GetInvalidFrameDescription() { return "invalid frame"; }

  virtual const char *GetInvalidRegContextDescription() {
    return "invalid frame, no registers";
  }

  // This is for use in the command interpreter, when you either want the
  // selected target, or if no target is present you want to prime the dummy
  // target with entities that will be copied over to new targets.
  Target *GetSelectedOrDummyTarget(bool prefer_dummy = false);
  Target *GetDummyTarget();

  // If a command needs to use the "current" thread, use this call. Command
  // objects will have an ExecutionContext to use, and that may or may not have
  // a thread in it.  If it does, you should use that by default, if not, then
  // use the ExecutionContext's target's selected thread, etc... This call
  // insulates you from the details of this calculation.
  Thread *GetDefaultThread();

  //------------------------------------------------------------------
  /// Check the command to make sure anything required by this
  /// command is available.
  ///
  /// @param[out] result
  ///     A command result object, if it is not okay to run the command
  ///     this will be filled in with a suitable error.
  ///
  /// @return
  ///     \b true if it is okay to run this command, \b false otherwise.
  //------------------------------------------------------------------
  bool CheckRequirements(CommandReturnObject &result);

  void Cleanup();

  CommandInterpreter &m_interpreter;
  ExecutionContext m_exe_ctx;
  std::unique_lock<std::recursive_mutex> m_api_locker;
  std::string m_cmd_name;
  std::string m_cmd_help_short;
  std::string m_cmd_help_long;
  std::string m_cmd_syntax;
  Flags m_flags;
  std::vector<CommandArgumentEntry> m_arguments;
  lldb::CommandOverrideCallback m_deprecated_command_override_callback;
  lldb::CommandOverrideCallbackWithResult m_command_override_callback;
  void *m_command_override_baton;

  // Helper function to populate IDs or ID ranges as the command argument data
  // to the specified command argument entry.
  static void AddIDsArgumentData(CommandArgumentEntry &arg,
                                 lldb::CommandArgumentType ID,
                                 lldb::CommandArgumentType IDRange);
};

class CommandObjectParsed : public CommandObject {
public:
  CommandObjectParsed(CommandInterpreter &interpreter, const char *name,
                      const char *help = nullptr, const char *syntax = nullptr,
                      uint32_t flags = 0)
      : CommandObject(interpreter, name, help, syntax, flags) {}

  ~CommandObjectParsed() override = default;

  bool Execute(const char *args_string, CommandReturnObject &result) override;

protected:
  virtual bool DoExecute(Args &command, CommandReturnObject &result) = 0;

  bool WantsRawCommandString() override { return false; }
};

class CommandObjectRaw : public CommandObject {
public:
  CommandObjectRaw(CommandInterpreter &interpreter, llvm::StringRef name,
    llvm::StringRef help = "", llvm::StringRef syntax = "",
                   uint32_t flags = 0)
      : CommandObject(interpreter, name, help, syntax, flags) {}

  ~CommandObjectRaw() override = default;

  bool Execute(const char *args_string, CommandReturnObject &result) override;

protected:
  virtual bool DoExecute(llvm::StringRef command,
                         CommandReturnObject &result) = 0;

  bool WantsRawCommandString() override { return true; }
};

} // namespace lldb_private

#endif // liblldb_CommandObject_h_
