include "llvm/Option/OptParser.td"

// lib.exe accepts options starting with either a dash or a slash.

// Flag that takes no arguments.
class F<string name> : Flag<["/", "-", "-?"], name>;

// Flag that takes one argument after ":".
class P<string name, string help> :
      Joined<["/", "-", "-?"], name#":">, HelpText<help>;

def libpath: P<"libpath", "Object file search path">;
def out    : P<"out", "Path to file to write output">;

def llvmlibthin : F<"llvmlibthin">,
    HelpText<"Make .lib point to .obj files instead of copying their contents">;

def help : F<"help">;
def help_q : Flag<["/?", "-?"], "">, Alias<help>;

//==============================================================================
// The flags below do nothing. They are defined only for lib.exe compatibility.
//==============================================================================

class QF<string name> : Joined<["/", "-", "-?"], name#":">;

def ignore : QF<"ignore">;
def machine: QF<"machine">;
def nologo : F<"nologo">;
