include "llvm/Option/OptParser.td"

def m: JoinedOrSeparate<["-"], "m">, HelpText<"Set target machine">;
def m_long : JoinedOrSeparate<["--"], "machine">, Alias<m>;

def l: JoinedOrSeparate<["-"], "l">, HelpText<"Generate an import lib">;
def l_long : JoinedOrSeparate<["--"], "output-lib">, Alias<l>;

def D: JoinedOrSeparate<["-"], "D">, HelpText<"Specify the input DLL Name">;
def D_long : JoinedOrSeparate<["--"], "dllname">, Alias<D>;

def d: JoinedOrSeparate<["-"], "d">, HelpText<"Input .def File">;
def d_long : JoinedOrSeparate<["--"], "input-def">, Alias<d>;

def k: Flag<["-"], "k">, HelpText<"Kill @n Symbol from export">;
def k_alias: Flag<["--"], "kill-at">, Alias<k>;

//==============================================================================
// The flags below do nothing. They are defined only for dlltool compatibility.
//==============================================================================

def S: JoinedOrSeparate<["-"], "S">, HelpText<"Assembler">;
def S_alias: JoinedOrSeparate<["--"], "as">, Alias<S>;

def f: JoinedOrSeparate<["-"], "f">, HelpText<"Assembler Flags">;
def f_alias: JoinedOrSeparate<["--"], "as-flags">, Alias<f>;
