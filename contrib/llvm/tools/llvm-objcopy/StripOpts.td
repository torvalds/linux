include "llvm/Option/OptParser.td"

multiclass Eq<string name, string help> {
  def NAME : Separate<["--", "-"], name>;
  def NAME #_eq : Joined<["--", "-"], name #"=">,
                  Alias<!cast<Separate>(NAME)>,
                  HelpText<help>;
}

def help : Flag<["-", "--"], "help">;

def enable_deterministic_archives
    : Flag<["-", "--"], "enable-deterministic-archives">,
      HelpText<"Enable deterministic mode when stripping archives (use zero "
               "for UIDs, GIDs, and timestamps).">;
def D : Flag<["-"], "D">,
        Alias<enable_deterministic_archives>,
        HelpText<"Alias for --enable-deterministic-archives">;

def disable_deterministic_archives
    : Flag<["-", "--"], "disable-deterministic-archives">,
      HelpText<"Disable deterministic mode when stripping archives (use real "
               "values for UIDs, GIDs, and timestamps).">;
def U : Flag<["-"], "U">,
        Alias<disable_deterministic_archives>,
        HelpText<"Alias for --disable-deterministic-archives">;

defm output : Eq<"o", "Write output to <file>">, MetaVarName<"output">;

def preserve_dates : Flag<["-", "--"], "preserve-dates">,
                     HelpText<"Preserve access and modification timestamps">;
def p : Flag<["-"], "p">, Alias<preserve_dates>;

def strip_all
    : Flag<["-", "--"], "strip-all">,
      HelpText<
          "Remove non-allocated sections other than .gnu.warning* sections">;
def s : Flag<["-"], "s">, Alias<strip_all>;

def strip_all_gnu : Flag<["-", "--"], "strip-all-gnu">,
                    HelpText<"Compatible with GNU strip's --strip-all">;
def strip_debug : Flag<["-", "--"], "strip-debug">,
                  HelpText<"Remove debugging symbols only">;
def d : Flag<["-"], "d">, Alias<strip_debug>;
def g : Flag<["-"], "g">, Alias<strip_debug>;
def S : Flag<["-"], "S">, Alias<strip_debug>;
def strip_unneeded : Flag<["-", "--"], "strip-unneeded">,
                     HelpText<"Remove all symbols not needed by relocations">;

defm remove_section : Eq<"remove-section", "Remove <section>">,
                      MetaVarName<"section">;
def R : JoinedOrSeparate<["-"], "R">, Alias<remove_section>;

defm keep_section : Eq<"keep-section", "Keep <section>">,
                    MetaVarName<"section">;
defm keep_symbol : Eq<"keep-symbol", "Do not remove symbol <symbol>">,
                   MetaVarName<"symbol">;
def K : JoinedOrSeparate<["-"], "K">, Alias<keep_symbol>;

def discard_all
    : Flag<["-", "--"], "discard-all">,
      HelpText<"Remove all local symbols except file and section symbols">;
def x : Flag<["-"], "x">, Alias<discard_all>;

def version : Flag<["-", "--"], "version">,
              HelpText<"Print the version and exit.">;
def V : Flag<["-"], "V">, Alias<version>;
