include "llvm/Option/OptParser.td"


// output kinds
def grp_kind : OptionGroup<"outs">, HelpText<"OUTPUT KIND">;
def relocatable : Flag<["-"], "r">,
     HelpText<"Create relocatable object file">, Group<grp_kind>;
def static : Flag<["-"], "static">,
     HelpText<"Create static executable">, Group<grp_kind>;
def dynamic : Flag<["-"], "dynamic">,
     HelpText<"Create dynamic executable (default)">,Group<grp_kind>;
def dylib : Flag<["-"], "dylib">,
     HelpText<"Create dynamic library">, Group<grp_kind>;
def bundle : Flag<["-"], "bundle">,
     HelpText<"Create dynamic bundle">, Group<grp_kind>;
def execute : Flag<["-"], "execute">,
     HelpText<"Create main executable (default)">, Group<grp_kind>;
def preload : Flag<["-"], "preload">,
     HelpText<"Create binary for use with embedded systems">, Group<grp_kind>;

// optimizations
def grp_opts : OptionGroup<"opts">, HelpText<"OPTIMIZATIONS">;
def dead_strip : Flag<["-"], "dead_strip">,
     HelpText<"Remove unreference code and data">, Group<grp_opts>;
def macosx_version_min : Separate<["-"], "macosx_version_min">,
     MetaVarName<"<version>">,
     HelpText<"Minimum Mac OS X version">, Group<grp_opts>;
def ios_version_min : Separate<["-"], "ios_version_min">,
     MetaVarName<"<version>">,
     HelpText<"Minimum iOS version">, Group<grp_opts>;
def iphoneos_version_min : Separate<["-"], "iphoneos_version_min">,
     Alias<ios_version_min>;
def ios_simulator_version_min : Separate<["-"], "ios_simulator_version_min">,
     MetaVarName<"<version>">,
     HelpText<"Minimum iOS simulator version">, Group<grp_opts>;
def sdk_version : Separate<["-"], "sdk_version">,
     MetaVarName<"<version>">,
     HelpText<"SDK version">, Group<grp_opts>;
def source_version : Separate<["-"], "source_version">,
     MetaVarName<"<version>">,
     HelpText<"Source version">, Group<grp_opts>;
def version_load_command : Flag<["-"], "version_load_command">,
     HelpText<"Force generation of a version load command">, Group<grp_opts>;
def no_version_load_command : Flag<["-"], "no_version_load_command">,
     HelpText<"Disable generation of a version load command">, Group<grp_opts>;
def function_starts : Flag<["-"], "function_starts">,
     HelpText<"Force generation of a function starts load command">,
     Group<grp_opts>;
def no_function_starts : Flag<["-"], "no_function_starts">,
     HelpText<"Disable generation of a function starts load command">,
     Group<grp_opts>;
def data_in_code_info : Flag<["-"], "data_in_code_info">,
     HelpText<"Force generation of a data in code load command">,
     Group<grp_opts>;
def no_data_in_code_info : Flag<["-"], "no_data_in_code_info">,
     HelpText<"Disable generation of a data in code load command">,
     Group<grp_opts>;
def mllvm : Separate<["-"], "mllvm">,
     MetaVarName<"<option>">,
     HelpText<"Options to pass to LLVM during LTO">, Group<grp_opts>;
def exported_symbols_list : Separate<["-"], "exported_symbols_list">,
     MetaVarName<"<file-path>">,
     HelpText<"Restricts which symbols will be exported">, Group<grp_opts>;
def exported_symbol : Separate<["-"], "exported_symbol">,
     MetaVarName<"<symbol>">,
     HelpText<"Restricts which symbols will be exported">, Group<grp_opts>;
def unexported_symbols_list : Separate<["-"], "unexported_symbols_list">,
     MetaVarName<"<file-path>">,
     HelpText<"Lists symbols that should not be exported">, Group<grp_opts>;
def unexported_symbol : Separate<["-"], "unexported_symbol">,
     MetaVarName<"<symbol>">,
     HelpText<"A symbol which should not be exported">, Group<grp_opts>;
def keep_private_externs : Flag<["-"], "keep_private_externs">,
     HelpText<"Private extern (hidden) symbols should not be transformed "
              "into local symbols">, Group<grp_opts>;
def order_file : Separate<["-"], "order_file">,
     MetaVarName<"<file-path>">,
     HelpText<"re-order and move specified symbols to start of their section">,
     Group<grp_opts>;
def flat_namespace : Flag<["-"], "flat_namespace">,
     HelpText<"Resolves symbols in any (transitively) linked dynamic libraries. "
              "Source libraries are not recorded: dyld will re-search all "
              "images at runtime and use the first definition found.">,
     Group<grp_opts>;
def twolevel_namespace : Flag<["-"], "twolevel_namespace">,
     HelpText<"Resolves symbols in listed libraries only. Source libraries are "
              "recorded in the symbol table.">,
     Group<grp_opts>;
def undefined : Separate<["-"], "undefined">,
                MetaVarName<"<undefined>">,
                HelpText<"Determines how undefined symbols are handled.">,
                Group<grp_opts>;
def no_objc_category_merging : Flag<["-"], "no_objc_category_merging">,
     HelpText<"Disables the optimisation which merges Objective-C categories "
              "on a class in to the class itself.">,
     Group<grp_opts>;

// main executable options
def grp_main : OptionGroup<"opts">, HelpText<"MAIN EXECUTABLE OPTIONS">;
def entry : Separate<["-"], "e">,
     MetaVarName<"<entry-name>">,
     HelpText<"entry symbol name">,Group<grp_main>;
def pie : Flag<["-"], "pie">,
     HelpText<"Create Position Independent Executable (for ASLR)">,
     Group<grp_main>;
def no_pie : Flag<["-"], "no_pie">,
     HelpText<"Do not create Position Independent Executable">,
     Group<grp_main>;
def stack_size : Separate<["-"], "stack_size">,
     HelpText<"Specifies the maximum stack size for the main thread in a program. "
              "Must be a page-size multiple. (default=8Mb)">,
     Group<grp_main>;
def export_dynamic : Flag<["-"], "export_dynamic">,
     HelpText<"Preserves all global symbols in main executables during LTO">,
     Group<grp_main>;

// dylib executable options
def grp_dylib : OptionGroup<"opts">, HelpText<"DYLIB EXECUTABLE OPTIONS">;
def install_name : Separate<["-"], "install_name">,
     MetaVarName<"<path>">,
     HelpText<"The dylib's install name">, Group<grp_dylib>;
def mark_dead_strippable_dylib : Flag<["-"], "mark_dead_strippable_dylib">,
     HelpText<"Marks the dylib as having no side effects during initialization">,
     Group<grp_dylib>;
def compatibility_version : Separate<["-"], "compatibility_version">,
     MetaVarName<"<version>">,
     HelpText<"The dylib's compatibility version">, Group<grp_dylib>;
def current_version : Separate<["-"], "current_version">,
     MetaVarName<"<version>">,
     HelpText<"The dylib's current version">, Group<grp_dylib>;

// dylib executable options - compatibility aliases
def dylib_install_name : Separate<["-"], "dylib_install_name">,
     Alias<install_name>;
def dylib_compatibility_version : Separate<["-"], "dylib_compatibility_version">,
     MetaVarName<"<version>">, Alias<compatibility_version>;
def dylib_current_version : Separate<["-"], "dylib_current_version">,
     MetaVarName<"<version>">, Alias<current_version>;

// bundle executable options
def grp_bundle : OptionGroup<"opts">, HelpText<"BUNDLE EXECUTABLE OPTIONS">;
def bundle_loader : Separate<["-"], "bundle_loader">,
     MetaVarName<"<path>">,
     HelpText<"The executable that will be loading this Mach-O bundle">,
     Group<grp_bundle>;

// library options
def grp_libs : OptionGroup<"libs">, HelpText<"LIBRARY OPTIONS">;
def L : JoinedOrSeparate<["-"], "L">,
     MetaVarName<"<dir>">,
     HelpText<"Add directory to library search path">, Group<grp_libs>;
def F : JoinedOrSeparate<["-"], "F">,
     MetaVarName<"<dir>">,
     HelpText<"Add directory to framework search path">, Group<grp_libs>;
def Z : Flag<["-"], "Z">,
     HelpText<"Do not search standard directories for libraries or frameworks">;
def all_load : Flag<["-"], "all_load">,
     HelpText<"Forces all members of all static libraries to be loaded">,
     Group<grp_libs>;
def force_load : Separate<["-"], "force_load">,
     MetaVarName<"<library-path>">,
     HelpText<"Forces all members of specified static libraries to be loaded">,
     Group<grp_libs>;
def syslibroot : Separate<["-"], "syslibroot">, MetaVarName<"<dir>">,
     HelpText<"Add path to SDK to all absolute library search paths">,
     Group<grp_libs>;

// Input options
def l : Joined<["-"], "l">,
     MetaVarName<"<libname>">,
     HelpText<"Base name of library searched for in -L directories">;
def upward_l : Joined<["-"], "upward-l">,
     MetaVarName<"<libname>">,
     HelpText<"Base name of upward library searched for in -L directories">;
def framework : Separate<["-"], "framework">,
     MetaVarName<"<name>">,
     HelpText<"Base name of framework searched for in -F directories">;
def upward_framework : Separate<["-"], "upward_framework">,
     MetaVarName<"<name>">,
     HelpText<"Base name of upward framework searched for in -F directories">;
def upward_library : Separate<["-"], "upward_library">,
     MetaVarName<"<path>">,
     HelpText<"path to upward dylib to link with">;
def filelist : Separate<["-"], "filelist">,
     MetaVarName<"<path>">,
     HelpText<"file containing paths to input files">;


// test case options
def print_atoms : Flag<["-"], "print_atoms">,
     HelpText<"Emit output as yaml atoms">;
def test_file_usage : Flag<["-"], "test_file_usage">,
     HelpText<"Only files specified by -file_exists are considered to exist. "
              "Print which files would be used">;
def path_exists : Separate<["-"], "path_exists">,
     MetaVarName<"<path>">,
     HelpText<"Used with -test_file_usage to declare a path">;


// general options
def output : Separate<["-"], "o">,
     MetaVarName<"<path>">,
     HelpText<"Output file path">;
def arch : Separate<["-"], "arch">,
     MetaVarName<"<arch-name>">,
     HelpText<"Architecture to link">;
def sectalign : MultiArg<["-"], "sectalign", 3>,
     MetaVarName<"<segname> <sectname> <alignment>">,
     HelpText<"Alignment for segment/section">;
def sectcreate : MultiArg<["-"], "sectcreate", 3>,
     MetaVarName<"<segname> <sectname> <file>">,
     HelpText<"Create section <segname>/<sectname> from contents of <file>">;
def image_base : Separate<["-"], "image_base">;
def seg1addr : Separate<["-"], "seg1addr">, Alias<image_base>;
def demangle : Flag<["-"], "demangle">,
     HelpText<"Demangles symbol names in errors and warnings">;
def dependency_info : Separate<["-"], "dependency_info">,
     MetaVarName<"<file>">,
     HelpText<"Write binary list of files used during link">;
def S : Flag<["-"], "S">,
     HelpText<"Remove debug information (STABS or DWARF) from the output file">;
def rpath : Separate<["-"], "rpath">,
     MetaVarName<"<path>">,
     HelpText<"Add path to the runpath search path list for image being created">;

def t : Flag<["-"], "t">,
     HelpText<"Print the names of the input files as ld processes them">;
def v : Flag<["-"], "v">,
     HelpText<"Print linker information">;
def error_limit : Separate<["-", "--"], "error-limit">,
     MetaVarName<"<number>">,
     HelpText<"Maximum number of errors to emit before stopping (0 = no limit)">;

// Ignored options
def lto_library : Separate<["-"], "lto_library">,
    MetaVarName<"<path>">,
    HelpText<"Ignored for compatibility with other linkers">;

// Obsolete options
def grp_obsolete : OptionGroup<"obsolete">, HelpText<"OBSOLETE OPTIONS">;
def single_module : Flag<["-"], "single_module">,
     HelpText<"Default for dylibs">, Group<grp_obsolete>;
def multi_module : Flag<["-"], "multi_module">,
     HelpText<"Unsupported way to build dylibs">, Group<grp_obsolete>;
def objc_gc_compaction : Flag<["-"], "objc_gc_compaction">,
     HelpText<"Unsupported ObjC GC option">, Group<grp_obsolete>;
def objc_gc : Flag<["-"], "objc_gc">,
     HelpText<"Unsupported ObjC GC option">, Group<grp_obsolete>;
def objc_gc_only : Flag<["-"], "objc_gc_only">,
     HelpText<"Unsupported ObjC GC option">, Group<grp_obsolete>;
