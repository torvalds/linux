//===-- SBCommandInterpreter.h ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBCommandInterpreter_h_
#define LLDB_SBCommandInterpreter_h_

#include <memory>

#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBDefines.h"

namespace lldb {

class LLDB_API SBCommandInterpreterRunOptions {
  friend class SBDebugger;
  friend class SBCommandInterpreter;

public:
  SBCommandInterpreterRunOptions();
  ~SBCommandInterpreterRunOptions();

  bool GetStopOnContinue() const;

  void SetStopOnContinue(bool);

  bool GetStopOnError() const;

  void SetStopOnError(bool);

  bool GetStopOnCrash() const;

  void SetStopOnCrash(bool);

  bool GetEchoCommands() const;

  void SetEchoCommands(bool);

  bool GetEchoCommentCommands() const;

  void SetEchoCommentCommands(bool echo);

  bool GetPrintResults() const;

  void SetPrintResults(bool);

  bool GetAddToHistory() const;

  void SetAddToHistory(bool);

private:
  lldb_private::CommandInterpreterRunOptions *get() const;

  lldb_private::CommandInterpreterRunOptions &ref() const;

  // This is set in the constructor and will always be valid.
  mutable std::unique_ptr<lldb_private::CommandInterpreterRunOptions>
      m_opaque_up;
};

class SBCommandInterpreter {
public:
  enum {
    eBroadcastBitThreadShouldExit = (1 << 0),
    eBroadcastBitResetPrompt = (1 << 1),
    eBroadcastBitQuitCommandReceived = (1 << 2), // User entered quit
    eBroadcastBitAsynchronousOutputData = (1 << 3),
    eBroadcastBitAsynchronousErrorData = (1 << 4)
  };

  SBCommandInterpreter(const lldb::SBCommandInterpreter &rhs);

  ~SBCommandInterpreter();

  const lldb::SBCommandInterpreter &
  operator=(const lldb::SBCommandInterpreter &rhs);

  static const char *
  GetArgumentTypeAsCString(const lldb::CommandArgumentType arg_type);

  static const char *
  GetArgumentDescriptionAsCString(const lldb::CommandArgumentType arg_type);

  static bool EventIsCommandInterpreterEvent(const lldb::SBEvent &event);

  bool IsValid() const;

  bool CommandExists(const char *cmd);

  bool AliasExists(const char *cmd);

  lldb::SBBroadcaster GetBroadcaster();

  static const char *GetBroadcasterClass();

  bool HasCommands();

  bool HasAliases();

  bool HasAliasOptions();

  lldb::SBProcess GetProcess();

  lldb::SBDebugger GetDebugger();

  lldb::SBCommand AddMultiwordCommand(const char *name, const char *help);

  lldb::SBCommand AddCommand(const char *name,
                             lldb::SBCommandPluginInterface *impl,
                             const char *help);

  lldb::SBCommand AddCommand(const char *name,
                             lldb::SBCommandPluginInterface *impl,
                             const char *help, const char *syntax);

  void SourceInitFileInHomeDirectory(lldb::SBCommandReturnObject &result);

  void
  SourceInitFileInCurrentWorkingDirectory(lldb::SBCommandReturnObject &result);

  lldb::ReturnStatus HandleCommand(const char *command_line,
                                   lldb::SBCommandReturnObject &result,
                                   bool add_to_history = false);

  lldb::ReturnStatus HandleCommand(const char *command_line,
                                   SBExecutionContext &exe_ctx,
                                   SBCommandReturnObject &result,
                                   bool add_to_history = false);

  void HandleCommandsFromFile(lldb::SBFileSpec &file,
                              lldb::SBExecutionContext &override_context,
                              lldb::SBCommandInterpreterRunOptions &options,
                              lldb::SBCommandReturnObject result);

  // The pointer based interface is not useful in SWIG, since the cursor &
  // last_char arguments are string pointers INTO current_line and you can't do
  // that in a scripting language interface in general...

  // In either case, the way this works is that the you give it a line and
  // cursor position in the line.  The function will return the number of
  // completions.  The matches list will contain number_of_completions + 1
  // elements.  The first element is the common substring after the cursor
  // position for all the matches.  The rest of the elements are the matches.
  // The first element is useful if you are emulating the common shell behavior
  // where the tab completes to the string that is common among all the
  // matches, then you should first check if the first element is non-empty,
  // and if so just insert it and move the cursor to the end of the insertion.
  // The next tab will return an empty common substring, and a list of choices
  // (if any), at which point you should display the choices and let the user
  // type further to disambiguate.

  int HandleCompletion(const char *current_line, const char *cursor,
                       const char *last_char, int match_start_point,
                       int max_return_elements, lldb::SBStringList &matches);

  int HandleCompletion(const char *current_line, uint32_t cursor_pos,
                       int match_start_point, int max_return_elements,
                       lldb::SBStringList &matches);

  // Same as HandleCompletion, but also fills out `descriptions` with
  // descriptions for each match.
  int HandleCompletionWithDescriptions(
      const char *current_line, const char *cursor, const char *last_char,
      int match_start_point, int max_return_elements,
      lldb::SBStringList &matches, lldb::SBStringList &descriptions);

  int HandleCompletionWithDescriptions(const char *current_line,
                                       uint32_t cursor_pos,
                                       int match_start_point,
                                       int max_return_elements,
                                       lldb::SBStringList &matches,
                                       lldb::SBStringList &descriptions);

  bool WasInterrupted() const;

  // Catch commands before they execute by registering a callback that will get
  // called when the command gets executed. This allows GUI or command line
  // interfaces to intercept a command and stop it from happening
  bool SetCommandOverrideCallback(const char *command_name,
                                  lldb::CommandOverrideCallback callback,
                                  void *baton);

  SBCommandInterpreter(
      lldb_private::CommandInterpreter *interpreter_ptr =
          nullptr); // Access using SBDebugger::GetCommandInterpreter();

  //----------------------------------------------------------------------
  /// Return true if the command interpreter is the active IO handler.
  ///
  /// This indicates that any input coming into the debugger handles will
  /// go to the command interpreter and will result in LLDB command line
  /// commands being executed.
  //----------------------------------------------------------------------
  bool IsActive();

  //----------------------------------------------------------------------
  /// Get the string that needs to be written to the debugger stdin file
  /// handle when a control character is typed.
  ///
  /// Some GUI programs will intercept "control + char" sequences and want
  /// to have them do what normally would happen when using a real
  /// terminal, so this function allows GUI programs to emulate this
  /// functionality.
  ///
  /// @param[in] ch
  ///     The character that was typed along with the control key
  ///
  /// @return
  ///     The string that should be written into the file handle that is
  ///     feeding the input stream for the debugger, or nullptr if there is
  ///     no string for this control key.
  //----------------------------------------------------------------------
  const char *GetIOHandlerControlSequence(char ch);

  bool GetPromptOnQuit();

  void SetPromptOnQuit(bool b);

  //----------------------------------------------------------------------
  /// Sets whether the command interpreter should allow custom exit codes
  /// for the 'quit' command.
  //----------------------------------------------------------------------
  void AllowExitCodeOnQuit(bool allow);

  //----------------------------------------------------------------------
  /// Returns true if the user has called the 'quit' command with a custom exit
  /// code.
  //----------------------------------------------------------------------
  bool HasCustomQuitExitCode();

  //----------------------------------------------------------------------
  /// Returns the exit code that the user has specified when running the
  /// 'quit' command. Returns 0 if the user hasn't called 'quit' at all or
  /// without a custom exit code.
  //----------------------------------------------------------------------
  int GetQuitStatus();

  //----------------------------------------------------------------------
  /// Resolve the command just as HandleCommand would, expanding abbreviations
  /// and aliases.  If successful, result->GetOutput has the full expansion.
  //----------------------------------------------------------------------
  void ResolveCommand(const char *command_line, SBCommandReturnObject &result);

protected:
  lldb_private::CommandInterpreter &ref();

  lldb_private::CommandInterpreter *get();

  void reset(lldb_private::CommandInterpreter *);

private:
  friend class SBDebugger;

  static void InitializeSWIG();

  lldb_private::CommandInterpreter *m_opaque_ptr;
};

class SBCommandPluginInterface {
public:
  virtual ~SBCommandPluginInterface() = default;

  virtual bool DoExecute(lldb::SBDebugger /*debugger*/, char ** /*command*/,
                         lldb::SBCommandReturnObject & /*result*/) {
    return false;
  }
};

class SBCommand {
public:
  SBCommand();

  bool IsValid();

  const char *GetName();

  const char *GetHelp();

  const char *GetHelpLong();

  void SetHelp(const char *);

  void SetHelpLong(const char *);

  uint32_t GetFlags();

  void SetFlags(uint32_t flags);

  lldb::SBCommand AddMultiwordCommand(const char *name,
                                      const char *help = nullptr);

  lldb::SBCommand AddCommand(const char *name,
                             lldb::SBCommandPluginInterface *impl,
                             const char *help = nullptr);

  lldb::SBCommand AddCommand(const char *name,
                             lldb::SBCommandPluginInterface *impl,
                             const char *help, const char *syntax);

private:
  friend class SBDebugger;
  friend class SBCommandInterpreter;

  SBCommand(lldb::CommandObjectSP cmd_sp);

  lldb::CommandObjectSP m_opaque_sp;
};

} // namespace lldb

#endif // LLDB_SBCommandInterpreter_h_
