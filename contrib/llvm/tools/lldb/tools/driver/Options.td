include "llvm/Option/OptParser.td"

class F<string name>: Flag<["--", "-"], name>;
class S<string name>: Separate<["--", "-"], name>;
class R<list<string> prefixes, string name>
  : Option<prefixes, name, KIND_REMAINING_ARGS>;

// Attaching options.
def grp_attach : OptionGroup<"attaching">, HelpText<"ATTACHING">;

def attach_name: Separate<["--", "-"], "attach-name">,
  MetaVarName<"<name>">,
  HelpText<"Tells the debugger to attach to a process with the given name.">,
  Group<grp_attach>;
def: Separate<["-"], "n">,
  Alias<attach_name>,
  HelpText<"Alias for --attach-name">,
  Group<grp_attach>;

def wait_for: F<"wait-for">,
  HelpText<"Tells the debugger to wait for a process with the given pid or name to launch before attaching.">,
  Group<grp_attach>;
def: Flag<["-"], "w">,
  Alias<wait_for>,
  HelpText<"Alias for --wait-for">,
  Group<grp_attach>;

def attach_pid: Separate<["--", "-"], "attach-pid">,
  MetaVarName<"<pid>">,
  HelpText<"Tells the debugger to attach to a process with the given pid.">,
  Group<grp_attach>;
def: Separate<["-"], "p">,
  Alias<attach_pid>,
  HelpText<"Alias for --attach-pid">,
  Group<grp_attach>;


// Scripting options.
def grp_scripting : OptionGroup<"scripting">, HelpText<"SCRIPTING">;

def python_path: F<"python-path">,
  HelpText<"Prints out the path to the lldb.py file for this version of lldb.">,
  Group<grp_scripting>;
def: Flag<["-"], "P">,
  Alias<python_path>,
  HelpText<"Alias for --python-path">,
  Group<grp_scripting>;

def script_language: Separate<["--", "-"], "script-language">,
  MetaVarName<"<language>">,
  HelpText<"Tells the debugger to use the specified scripting language for user-defined scripts.">,
  Group<grp_scripting>;
def: Separate<["-"], "l">,
  Alias<script_language>,
  HelpText<"Alias for --script-language">,
  Group<grp_scripting>;

// Repl options.
def grp_repl : OptionGroup<"repl">, HelpText<"REPL">;

def repl: Flag<["--", "-"], "repl">,
  HelpText<"Runs lldb in REPL mode with a stub process.">,
  Group<grp_repl>;
def: Flag<["-"], "r">,
  Alias<repl>,
  HelpText<"Alias for --repl">,
  Group<grp_repl>;
def repl_: Joined<["--", "-"], "repl=">,
  MetaVarName<"<flags>">,
  HelpText<"Runs lldb in REPL mode with a stub process with the given flags.">,
  Group<grp_repl>;
def: Joined<["-"], "r=">,
  MetaVarName<"<flags>">,
  Alias<repl_>,
  HelpText<"Alias for --repl=<flags>">,
  Group<grp_repl>;

def repl_language: Separate<["--", "-"], "repl-language">,
  MetaVarName<"<language>">,
  HelpText<"Chooses the language for the REPL.">,
  Group<grp_repl>;
def: Separate<["-"], "R">,
  Alias<repl_language>,
  HelpText<"Alias for --repl-language">,
  Group<grp_repl>;


// Command options.
def grp_command : OptionGroup<"command">, HelpText<"COMMANDS">;

def no_lldbinit: F<"no-lldbinit">,
  HelpText<"Do not automatically parse any '.lldbinit' files.">,
  Group<grp_command>;
def: Flag<["-"], "x">,
  Alias<no_lldbinit>,
  HelpText<"Alias for --no-lldbinit">,
  Group<grp_command>;

def batch: F<"batch">,
  HelpText<"Tells the debugger to run the commands from -s, -S, -o & -O, and then quit.">,
  Group<grp_command>;
def: Flag<["-"], "b">,
  Alias<batch>,
  HelpText<"Alias for --batch">,
  Group<grp_command>;

def source_quietly: F<"source-quietly">,
  HelpText<"Tells the debugger to execute this one-line lldb command before any file has been loaded.">,
  Group<grp_command>;
def: Flag<["-"], "Q">,
  Alias<source_quietly>,
  HelpText<"Alias for --source-quietly">,
  Group<grp_command>;

def one_line_on_crash: Separate<["--", "-"], "one-line-on-crash">,
  MetaVarName<"<command>">,
  HelpText<"When in batch mode, tells the debugger to source this file of lldb commands if the target crashes.">,
  Group<grp_command>;
def: Separate<["-"], "k">,
  Alias<one_line_on_crash>,
  HelpText<"Alias for --one-line-on-crash">,
  Group<grp_command>;

def source_on_crash: Separate<["--", "-"], "source-on-crash">,
  MetaVarName<"<file>">,
  HelpText<"When in batch mode, tells the debugger to source this file of lldb commands if the target crashes.">,
  Group<grp_command>;
def: Separate<["-"], "K">,
  Alias<source_on_crash>,
  HelpText<"Alias for --source-on-crash">,
  Group<grp_command>;

def source: Separate<["--", "-"], "source">,
  MetaVarName<"<file>">,
  HelpText<"Tells the debugger to read in and execute the lldb commands in the given file, after any file has been loaded.">,
  Group<grp_command>;
def: Separate<["-"], "s">,
  Alias<source>,
  HelpText<"Alias for --source">,
  Group<grp_command>;

def source_before_file: Separate<["--", "-"], "source-before-file">,
  MetaVarName<"<file>">,
  HelpText<"Tells the debugger to read in and execute the lldb commands in the given file, before any file has been loaded.">,
  Group<grp_command>;
def: Separate<["-"], "S">,
  Alias<source_before_file>,
  HelpText<"Alias for --source-before-file">,
  Group<grp_command>;

def one_line: Separate<["--", "-"], "one-line">,
  MetaVarName<"<command>">,
  HelpText<"Tells the debugger to execute this one-line lldb command after any file provided on the command line has been loaded.">,
  Group<grp_command>;
def: Separate<["-"], "o">,
  Alias<one_line>,
  HelpText<"Alias for --one-line">,
  Group<grp_command>;

def one_line_before_file: Separate<["--", "-"], "one-line-before-file">,
  MetaVarName<"<command>">,
  HelpText<"Tells the debugger to execute this one-line lldb command before any file provided on the command line has been loaded.">,
  Group<grp_command>;
def: Separate<["-"], "O">,
  Alias<one_line_before_file>,
  HelpText<"Alias for --one-line-before-file">,
  Group<grp_command>;


// General options.
def version: F<"version">,
  HelpText<"Prints out the current version number of the LLDB debugger.">;
def: Flag<["-"], "v">,
  Alias<version>,
  HelpText<"Alias for --version">;

def help: F<"help">,
  HelpText<"Prints out the usage information for the LLDB debugger.">;
def: Flag<["-"], "h">,
  Alias<help>,
  HelpText<"Alias for --help">;

def core: Separate<["--", "-"], "core">,
  MetaVarName<"<filename>">,
  HelpText<"Tells the debugger to use the full path to <filename> as the core file.">;
def: Separate<["-"], "c">,
  Alias<core>,
  HelpText<"Alias for --core">;

def editor: F<"editor">,
  HelpText<"Tells the debugger to open source files using the host's \"external editor\" mechanism.">;
def: Flag<["-"], "e">,
  Alias<editor>,
  HelpText<"Alias for --editor">;

def no_use_colors: F<"no-use-colors">,
  HelpText<"Do not use colors.">;
def: Flag<["-"], "X">,
  Alias<no_use_colors>,
  HelpText<"Alias for --no-use-color">;

def file: Separate<["--", "-"], "file">,
  MetaVarName<"<filename>">,
  HelpText<"Tells the debugger to use the file <filename> as the program to be debugged.">;
def: Separate<["-"], "f">,
  Alias<file>,
  HelpText<"Alias for --file">;

def arch: Separate<["--", "-"], "arch">,
  MetaVarName<"<architecture>">,
  HelpText<"Tells the debugger to use the specified architecture when starting and running the program.">;
def: Separate<["-"], "a">,
  Alias<arch>,
  HelpText<"Alias for --arch">;

def debug: F<"debug">,
  HelpText<"Tells the debugger to print out extra information for debugging itself.">;
def: Flag<["-"], "d">,
  Alias<debug>,
  HelpText<"Alias for --debug">;

def capture: Separate<["--", "-"], "capture">,
  MetaVarName<"<filename>">,
  HelpText<"Tells the debugger to capture a reproducer to <filename>.">;
def replay: Separate<["--", "-"], "replay">,
  MetaVarName<"<filename>">,
  HelpText<"Tells the debugger to replay a reproducer from <filename>.">;

def REM : R<["--"], "">;
