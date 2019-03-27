include "llvm/Option/OptParser.td"

// link.exe accepts options starting with either a dash or a slash.

// Flag that takes no arguments.
class F<string name> : Flag<["/", "-", "-?"], name>;

// Flag that takes one argument after ":".
class P<string name, string help> :
      Joined<["/", "-", "-?"], name#":">, HelpText<help>;

// Boolean flag which can be suffixed by ":no". Using it unsuffixed turns the
// flag on and using it suffixed by ":no" turns it off.
multiclass B<string name, string help_on, string help_off> {
  def "" : F<name>, HelpText<help_on>;
  def _no : F<name#":no">, HelpText<help_off>;
}

def align   : P<"align", "Section alignment">;
def aligncomm : P<"aligncomm", "Set common symbol alignment">;
def alternatename : P<"alternatename", "Define weak alias">;
def base    : P<"base", "Base address of the program">;
def color_diagnostics: Flag<["--"], "color-diagnostics">,
  HelpText<"Use colors in diagnostics">;
def color_diagnostics_eq: Joined<["--"], "color-diagnostics=">,
  HelpText<"Use colors in diagnostics; one of 'always', 'never', 'auto'">;
def defaultlib : P<"defaultlib", "Add the library to the list of input files">;
def delayload : P<"delayload", "Delay loaded DLL name">;
def entry   : P<"entry", "Name of entry point symbol">;
def errorlimit : P<"errorlimit",
    "Maximum number of errors to emit before stopping (0 = no limit)">;
def export  : P<"export", "Export a function">;
// No help text because /failifmismatch is not intended to be used by the user.
def failifmismatch : P<"failifmismatch", "">;
def guard   : P<"guard", "Control flow guard">;
def heap    : P<"heap", "Size of the heap">;
def ignore : P<"ignore", "Specify warning codes to ignore">;
def implib  : P<"implib", "Import library name">;
def lib : F<"lib">,
    HelpText<"Act like lib.exe; must be first argument if present">;
def libpath : P<"libpath", "Additional library search path">;
def linkrepro : P<"linkrepro", "Dump linker invocation and input files for debugging">;
def lldltocache : P<"lldltocache", "Path to ThinLTO cached object file directory">;
def lldltocachepolicy : P<"lldltocachepolicy", "Pruning policy for the ThinLTO cache">;
def lldsavetemps : F<"lldsavetemps">,
    HelpText<"Save temporary files instead of deleting them">;
def machine : P<"machine", "Specify target platform">;
def merge   : P<"merge", "Combine sections">;
def mllvm   : P<"mllvm", "Options to pass to LLVM">;
def nodefaultlib : P<"nodefaultlib", "Remove a default library">;
def opt     : P<"opt", "Control optimizations">;
def order   : P<"order", "Put functions in order">;
def out     : P<"out", "Path to file to write output">;
def natvis : P<"natvis", "Path to natvis file to embed in the PDB">;
def no_color_diagnostics: F<"no-color-diagnostics">,
  HelpText<"Do not use colors in diagnostics">;
def pdb : P<"pdb", "PDB file path">;
def pdbaltpath : P<"pdbaltpath", "PDB file path to embed in the image">;
def section : P<"section", "Specify section attributes">;
def stack   : P<"stack", "Size of the stack">;
def stub    : P<"stub", "Specify DOS stub file">;
def subsystem : P<"subsystem", "Specify subsystem">;
def timestamp : P<"timestamp", "Specify the PE header timestamp">;
def version : P<"version", "Specify a version number in the PE header">;
def wholearchive_file : P<"wholearchive", "Include all object files from this archive">;

def disallowlib : Joined<["/", "-", "-?"], "disallowlib:">, Alias<nodefaultlib>;

def manifest : F<"manifest">, HelpText<"Create .manifest file">;
def manifest_colon : P<
    "manifest",
    "NO disables manifest output; EMBED[,ID=#] embeds manifest as resource in the image">;
def manifestuac : P<"manifestuac", "User access control">;
def manifestfile : P<"manifestfile", "Manifest output path, with /manifest">;
def manifestdependency : P<
    "manifestdependency",
    "Attributes for <dependency> element in manifest file; implies /manifest">;
def manifestinput : P<
    "manifestinput",
    "Additional manifest inputs; only valid with /manifest:embed">;

// We cannot use multiclass P because class name "incl" is different
// from its command line option name. We do this because "include" is
// a reserved keyword in tablegen.
def incl : Joined<["/", "-"], "include:">,
    HelpText<"Force symbol to be added to symbol table as undefined one">;

// "def" is also a keyword.
def deffile : Joined<["/", "-"], "def:">,
    HelpText<"Use module-definition file">;

def debug : F<"debug">, HelpText<"Embed a symbol table in the image">;
def debug_opt : P<"debug", "Embed a symbol table in the image with option">;
def debugtype : P<"debugtype", "Debug Info Options">;
def dll : F<"dll">, HelpText<"Create a DLL">;
def driver : P<"driver", "Generate a Windows NT Kernel Mode Driver">;
def nodefaultlib_all : F<"nodefaultlib">,
    HelpText<"Remove all default libraries">;
def noentry : F<"noentry">,
    HelpText<"Don't add reference to DllMainCRTStartup; only valid with /dll">;
def profile : F<"profile">;
def repro : F<"Brepro">,
    HelpText<"Use a hash of the executable as the PE header timestamp">;
def swaprun_cd : F<"swaprun:cd">;
def swaprun_net : F<"swaprun:net">;
def verbose : F<"verbose">;
def wholearchive_flag : F<"wholearchive">;

def force : F<"force">,
    HelpText<"Allow undefined and multiply defined symbols when creating executables">;
def force_unresolved : F<"force:unresolved">,
    HelpText<"Allow undefined symbols when creating executables">;
def force_multiple : F<"force:multiple">,
    HelpText<"Allow multiply defined symbols when creating executables">;
defm WX : B<"WX", "Treat warnings as errors", "Don't treat warnings as errors">;

defm allowbind : B<"allowbind", "Enable DLL binding (default)",
                   "Disable DLL binding">;
defm allowisolation : B<"allowisolation", "Enable DLL isolation (default)",
                        "Disable DLL isolation">;
defm appcontainer : B<"appcontainer",
                      "Image can only be run in an app container",
                      "Image can run outside an app container (default)">;
defm dynamicbase : B<"dynamicbase", "Enable ASLR (default unless /fixed)",
                     "Disable ASLR (default when /fixed)">;
defm fixed : B<"fixed", "Disable base relocations",
               "Enable base relocations (default)">;
defm highentropyva : B<"highentropyva",
                       "Enable 64-bit ASLR (default on 64-bit)",
                       "Disable 64-bit ASLR">;
defm incremental : B<"incremental",
                     "Keep original import library if contents are unchanged",
                     "Overwrite import library even if contents are unchanged">;
defm integritycheck : B<"integritycheck",
                        "Set FORCE_INTEGRITY bit in PE header",
                        "No effect (default)">;
defm largeaddressaware : B<"largeaddressaware",
                           "Enable large addresses (default on 64-bit)",
                           "Disable large addresses (default on 32-bit)">;
defm nxcompat : B<"nxcompat", "Enable data execution prevention (default)",
                  "Disable data execution provention">;
defm safeseh : B<"safeseh",
                 "Produce an image with Safe Exception Handler (only for x86)",
                 "Don't produce an image with Safe Exception Handler">;
defm tsaware  : B<"tsaware",
                  "Create Terminal Server aware executable (default)",
                  "Create non-Terminal Server aware executable">;

def help : F<"help">;
def help_q : Flag<["/?", "-?"], "">, Alias<help>;

// LLD extensions
def export_all_symbols : F<"export-all-symbols">;
def kill_at : F<"kill-at">;
def lldmingw : F<"lldmingw">;
def output_def : Joined<["/", "-"], "output-def:">;
def pdb_source_path : P<"pdbsourcepath",
                        "Base path used to make relative source file path absolute in PDB">;
def rsp_quoting : Joined<["--"], "rsp-quoting=">,
  HelpText<"Quoting style for response files, 'windows' (default) or 'posix'">;
def dash_dash_version : Flag<["--"], "version">,
  HelpText<"Print version information">;

// Flags for debugging
def lldmap : F<"lldmap">;
def lldmap_file : Joined<["/", "-"], "lldmap:">;
def show_timing : F<"time">;

//==============================================================================
// The flags below do nothing. They are defined only for link.exe compatibility.
//==============================================================================

class QF<string name> : Joined<["/", "-", "-?"], name#":">;

multiclass QB<string name> {
  def "" : F<name>;
  def _no : F<name#":no">;
}

def functionpadmin : F<"functionpadmin">;
def ignoreidl : F<"ignoreidl">;
def nologo : F<"nologo">;
def throwingnew : F<"throwingnew">;
def editandcontinue : F<"editandcontinue">;
def fastfail : F<"fastfail">;

def delay : QF<"delay">;
def errorreport : QF<"errorreport">;
def idlout : QF<"idlout">;
def maxilksize : QF<"maxilksize">;
def tlbid : QF<"tlbid">;
def tlbout : QF<"tlbout">;
def verbose_all : QF<"verbose">;
def guardsym : QF<"guardsym">;
