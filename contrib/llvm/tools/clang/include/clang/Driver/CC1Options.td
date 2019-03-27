//===--- CC1Options.td - Options for clang -cc1 ---------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the options accepted by clang -cc1 and clang -cc1as.
//
//===----------------------------------------------------------------------===//

let Flags = [CC1Option, NoDriverOption] in {

//===----------------------------------------------------------------------===//
// Target Options
//===----------------------------------------------------------------------===//

let Flags = [CC1Option, CC1AsOption, NoDriverOption] in {

def target_cpu : Separate<["-"], "target-cpu">,
  HelpText<"Target a specific cpu type">;
def target_feature : Separate<["-"], "target-feature">,
  HelpText<"Target specific attributes">;
def triple : Separate<["-"], "triple">,
  HelpText<"Specify target triple (e.g. i686-apple-darwin9)">;
def target_abi : Separate<["-"], "target-abi">,
  HelpText<"Target a particular ABI type">;
def target_sdk_version_EQ : Joined<["-"], "target-sdk-version=">,
  HelpText<"The version of target SDK used for compilation">;

}

def target_linker_version : Separate<["-"], "target-linker-version">,
  HelpText<"Target linker version">;
def triple_EQ : Joined<["-"], "triple=">, Alias<triple>;
def mfpmath : Separate<["-"], "mfpmath">,
  HelpText<"Which unit to use for fp math">;

def fpadding_on_unsigned_fixed_point : Flag<["-"], "fpadding-on-unsigned-fixed-point">,
  HelpText<"Force each unsigned fixed point type to have an extra bit of padding to align their scales with those of signed fixed point types">;
def fno_padding_on_unsigned_fixed_point : Flag<["-"], "fno-padding-on-unsigned-fixed-point">;

//===----------------------------------------------------------------------===//
// Analyzer Options
//===----------------------------------------------------------------------===//

def analysis_UnoptimizedCFG : Flag<["-"], "unoptimized-cfg">,
  HelpText<"Generate unoptimized CFGs for all analyses">;
def analysis_CFGAddImplicitDtors : Flag<["-"], "cfg-add-implicit-dtors">,
  HelpText<"Add C++ implicit destructors to CFGs for all analyses">;

def analyzer_store : Separate<["-"], "analyzer-store">,
  HelpText<"Source Code Analysis - Abstract Memory Store Models">;
def analyzer_store_EQ : Joined<["-"], "analyzer-store=">, Alias<analyzer_store>;

def analyzer_constraints : Separate<["-"], "analyzer-constraints">,
  HelpText<"Source Code Analysis - Symbolic Constraint Engines">;
def analyzer_constraints_EQ : Joined<["-"], "analyzer-constraints=">,
  Alias<analyzer_constraints>;

def analyzer_output : Separate<["-"], "analyzer-output">,
  HelpText<"Source Code Analysis - Output Options">;
def analyzer_output_EQ : Joined<["-"], "analyzer-output=">,
  Alias<analyzer_output>;

def analyzer_purge : Separate<["-"], "analyzer-purge">,
  HelpText<"Source Code Analysis - Dead Symbol Removal Frequency">;
def analyzer_purge_EQ : Joined<["-"], "analyzer-purge=">, Alias<analyzer_purge>;

def analyzer_opt_analyze_headers : Flag<["-"], "analyzer-opt-analyze-headers">,
  HelpText<"Force the static analyzer to analyze functions defined in header files">;
def analyzer_opt_analyze_nested_blocks : Flag<["-"], "analyzer-opt-analyze-nested-blocks">,
  HelpText<"Analyze the definitions of blocks in addition to functions">;
def analyzer_display_progress : Flag<["-"], "analyzer-display-progress">,
  HelpText<"Emit verbose output about the analyzer's progress">;
def analyze_function : Separate<["-"], "analyze-function">,
  HelpText<"Run analysis on specific function (for C++ include parameters in name)">;
def analyze_function_EQ : Joined<["-"], "analyze-function=">, Alias<analyze_function>;
def trim_egraph : Flag<["-"], "trim-egraph">,
  HelpText<"Only show error-related paths in the analysis graph">;
def analyzer_viz_egraph_graphviz : Flag<["-"], "analyzer-viz-egraph-graphviz">,
  HelpText<"Display exploded graph using GraphViz">;
def analyzer_dump_egraph : Separate<["-"], "analyzer-dump-egraph">,
  HelpText<"Dump exploded graph to the specified file">;
def analyzer_dump_egraph_EQ : Joined<["-"], "analyzer-dump-egraph=">, Alias<analyzer_dump_egraph>;

def analyzer_inline_max_stack_depth : Separate<["-"], "analyzer-inline-max-stack-depth">,
  HelpText<"Bound on stack depth while inlining (4 by default)">;
def analyzer_inline_max_stack_depth_EQ : Joined<["-"], "analyzer-inline-max-stack-depth=">,
  Alias<analyzer_inline_max_stack_depth>;

def analyzer_inlining_mode : Separate<["-"], "analyzer-inlining-mode">,
  HelpText<"Specify the function selection heuristic used during inlining">;
def analyzer_inlining_mode_EQ : Joined<["-"], "analyzer-inlining-mode=">, Alias<analyzer_inlining_mode>;

def analyzer_disable_retry_exhausted : Flag<["-"], "analyzer-disable-retry-exhausted">,
  HelpText<"Do not re-analyze paths leading to exhausted nodes with a different strategy (may decrease code coverage)">;

def analyzer_max_loop : Separate<["-"], "analyzer-max-loop">,
  HelpText<"The maximum number of times the analyzer will go through a loop">;
def analyzer_stats : Flag<["-"], "analyzer-stats">,
  HelpText<"Print internal analyzer statistics.">;

def analyzer_checker : Separate<["-"], "analyzer-checker">,
  HelpText<"Choose analyzer checkers to enable">,
  ValuesCode<[{
    const char *Values =
    #define GET_CHECKERS
    #define CHECKER(FULLNAME, CLASS, HT, DOC_URI)  FULLNAME ","
    #include "clang/StaticAnalyzer/Checkers/Checkers.inc"
    #undef GET_CHECKERS
    #define GET_PACKAGES
    #define PACKAGE(FULLNAME)  FULLNAME ","
    #include "clang/StaticAnalyzer/Checkers/Checkers.inc"
    #undef GET_PACKAGES
    ;
  }]>;
def analyzer_checker_EQ : Joined<["-"], "analyzer-checker=">,
  Alias<analyzer_checker>;

def analyzer_disable_checker : Separate<["-"], "analyzer-disable-checker">,
  HelpText<"Choose analyzer checkers to disable">;
def analyzer_disable_checker_EQ : Joined<["-"], "analyzer-disable-checker=">,
  Alias<analyzer_disable_checker>;

def analyzer_disable_all_checks : Flag<["-"], "analyzer-disable-all-checks">,
  HelpText<"Disable all static analyzer checks">;

def analyzer_checker_help : Flag<["-"], "analyzer-checker-help">,
  HelpText<"Display the list of analyzer checkers that are available">;

def analyzer_config_help : Flag<["-"], "analyzer-config-help">,
  HelpText<"Display the list of -analyzer-config options">;

def analyzer_list_enabled_checkers : Flag<["-"], "analyzer-list-enabled-checkers">,
  HelpText<"Display the list of enabled analyzer checkers">;

def analyzer_config : Separate<["-"], "analyzer-config">,
  HelpText<"Choose analyzer options to enable">;

def analyzer_config_compatibility_mode : Separate<["-"], "analyzer-config-compatibility-mode">,
  HelpText<"Don't emit errors on invalid analyzer-config inputs">;

def analyzer_config_compatibility_mode_EQ : Joined<["-"], "analyzer-config-compatibility-mode=">,
  Alias<analyzer_config_compatibility_mode>;

//===----------------------------------------------------------------------===//
// Migrator Options
//===----------------------------------------------------------------------===//
def migrator_no_nsalloc_error : Flag<["-"], "no-ns-alloc-error">,
  HelpText<"Do not error on use of NSAllocateCollectable/NSReallocateCollectable">;

def migrator_no_finalize_removal : Flag<["-"], "no-finalize-removal">,
  HelpText<"Do not remove finalize method in gc mode">;

//===----------------------------------------------------------------------===//
// CodeGen Options
//===----------------------------------------------------------------------===//

let Flags = [CC1Option, CC1AsOption, NoDriverOption] in {
def debug_info_kind_EQ : Joined<["-"], "debug-info-kind=">;
def debug_info_macro : Flag<["-"], "debug-info-macro">,
  HelpText<"Emit macro debug information">;
def default_function_attr : Separate<["-"], "default-function-attr">,
  HelpText<"Apply given attribute to all functions">;
def dwarf_version_EQ : Joined<["-"], "dwarf-version=">;
def debugger_tuning_EQ : Joined<["-"], "debugger-tuning=">;
def fdebug_compilation_dir : Separate<["-"], "fdebug-compilation-dir">,
  HelpText<"The compilation directory to embed in the debug info.">;
def dwarf_debug_flags : Separate<["-"], "dwarf-debug-flags">,
  HelpText<"The string to embed in the Dwarf debug flags record.">;
def record_command_line : Separate<["-"], "record-command-line">,
  HelpText<"The string to embed in the .LLVM.command.line section.">;
def compress_debug_sections : Flag<["-", "--"], "compress-debug-sections">,
    HelpText<"DWARF debug sections compression">;
def compress_debug_sections_EQ : Joined<["-"], "compress-debug-sections=">,
    HelpText<"DWARF debug sections compression type">;
def mno_exec_stack : Flag<["-"], "mnoexecstack">,
  HelpText<"Mark the file as not needing an executable stack">;
def massembler_fatal_warnings : Flag<["-"], "massembler-fatal-warnings">,
  HelpText<"Make assembler warnings fatal">;
def mrelax_relocations : Flag<["--"], "mrelax-relocations">,
    HelpText<"Use relaxable elf relocations">;
def msave_temp_labels : Flag<["-"], "msave-temp-labels">,
  HelpText<"Save temporary labels in the symbol table. "
           "Note this may change .s semantics and shouldn't generally be used "
           "on compiler-generated code.">;
def mrelocation_model : Separate<["-"], "mrelocation-model">,
  HelpText<"The relocation model to use">, Values<"static,pic,ropi,rwpi,ropi-rwpi,dynamic-no-pic">;
def fno_math_builtin : Flag<["-"], "fno-math-builtin">,
  HelpText<"Disable implicit builtin knowledge of math functions">;
}

def disable_llvm_verifier : Flag<["-"], "disable-llvm-verifier">,
  HelpText<"Don't run the LLVM IR verifier pass">;
def disable_llvm_passes : Flag<["-"], "disable-llvm-passes">,
  HelpText<"Use together with -emit-llvm to get pristine LLVM IR from the "
           "frontend by not running any LLVM passes at all">;
def disable_llvm_optzns : Flag<["-"], "disable-llvm-optzns">,
  Alias<disable_llvm_passes>;
def disable_lifetimemarkers : Flag<["-"], "disable-lifetime-markers">,
  HelpText<"Disable lifetime-markers emission even when optimizations are "
           "enabled">;
def disable_O0_optnone : Flag<["-"], "disable-O0-optnone">,
  HelpText<"Disable adding the optnone attribute to functions at O0">;
def disable_red_zone : Flag<["-"], "disable-red-zone">,
  HelpText<"Do not emit code that uses the red zone.">;
def dwarf_column_info : Flag<["-"], "dwarf-column-info">,
  HelpText<"Turn on column location information.">;
def split_dwarf : Flag<["-"], "split-dwarf">,
  HelpText<"Split out the dwarf .dwo sections">;
def dwarf_ext_refs : Flag<["-"], "dwarf-ext-refs">,
  HelpText<"Generate debug info with external references to clang modules"
           " or precompiled headers">;
def dwarf_explicit_import : Flag<["-"], "dwarf-explicit-import">,
  HelpText<"Generate explicit import from anonymous namespace to containing"
           " scope">;
def debug_forward_template_params : Flag<["-"], "debug-forward-template-params">,
  HelpText<"Emit complete descriptions of template parameters in forward"
           " declarations">;
def fforbid_guard_variables : Flag<["-"], "fforbid-guard-variables">,
  HelpText<"Emit an error if a C++ static local initializer would need a guard variable">;
def no_implicit_float : Flag<["-"], "no-implicit-float">,
  HelpText<"Don't generate implicit floating point instructions">;
def fdump_vtable_layouts : Flag<["-"], "fdump-vtable-layouts">,
  HelpText<"Dump the layouts of all vtables that will be emitted in a translation unit">;
def fmerge_functions : Flag<["-"], "fmerge-functions">,
  HelpText<"Permit merging of identical functions when optimizing.">;
def femit_coverage_notes : Flag<["-"], "femit-coverage-notes">,
  HelpText<"Emit a gcov coverage notes file when compiling.">;
def femit_coverage_data: Flag<["-"], "femit-coverage-data">,
  HelpText<"Instrument the program to emit gcov coverage data when run.">;
def coverage_data_file : Separate<["-"], "coverage-data-file">,
  HelpText<"Emit coverage data to this filename.">;
def coverage_data_file_EQ : Joined<["-"], "coverage-data-file=">,
  Alias<coverage_data_file>;
def coverage_notes_file : Separate<["-"], "coverage-notes-file">,
  HelpText<"Emit coverage notes to this filename.">;
def coverage_notes_file_EQ : Joined<["-"], "coverage-notes-file=">,
  Alias<coverage_notes_file>;
def coverage_cfg_checksum : Flag<["-"], "coverage-cfg-checksum">,
  HelpText<"Emit CFG checksum for functions in .gcno files.">;
def coverage_no_function_names_in_data : Flag<["-"], "coverage-no-function-names-in-data">,
  HelpText<"Emit function names in .gcda files.">;
def coverage_exit_block_before_body : Flag<["-"], "coverage-exit-block-before-body">,
  HelpText<"Emit the exit block before the body blocks in .gcno files.">;
def coverage_version_EQ : Joined<["-"], "coverage-version=">,
  HelpText<"Four-byte version string for gcov files.">;
def test_coverage : Flag<["-"], "test-coverage">,
  HelpText<"Do not generate coverage files or remove coverage changes from IR">;
def dump_coverage_mapping : Flag<["-"], "dump-coverage-mapping">,
  HelpText<"Dump the coverage mapping records, for testing">;
def fuse_register_sized_bitfield_access: Flag<["-"], "fuse-register-sized-bitfield-access">,
  HelpText<"Use register sized accesses to bit-fields, when possible.">;
def relaxed_aliasing : Flag<["-"], "relaxed-aliasing">,
  HelpText<"Turn off Type Based Alias Analysis">;
def no_struct_path_tbaa : Flag<["-"], "no-struct-path-tbaa">,
  HelpText<"Turn off struct-path aware Type Based Alias Analysis">;
def new_struct_path_tbaa : Flag<["-"], "new-struct-path-tbaa">,
  HelpText<"Enable enhanced struct-path aware Type Based Alias Analysis">;
def masm_verbose : Flag<["-"], "masm-verbose">,
  HelpText<"Generate verbose assembly output">;
def mcode_model : Separate<["-"], "mcode-model">,
  HelpText<"The code model to use">, Values<"tiny,small,kernel,medium,large">;
def mdebug_pass : Separate<["-"], "mdebug-pass">,
  HelpText<"Enable additional debug output">;
def mdisable_fp_elim : Flag<["-"], "mdisable-fp-elim">,
  HelpText<"Disable frame pointer elimination optimization">;
def mdisable_tail_calls : Flag<["-"], "mdisable-tail-calls">,
  HelpText<"Disable tail call optimization, keeping the call stack accurate">;
def menable_no_infinities : Flag<["-"], "menable-no-infs">,
  HelpText<"Allow optimization to assume there are no infinities.">;
def menable_no_nans : Flag<["-"], "menable-no-nans">,
  HelpText<"Allow optimization to assume there are no NaNs.">;
def menable_unsafe_fp_math : Flag<["-"], "menable-unsafe-fp-math">,
  HelpText<"Allow unsafe floating-point math optimizations which may decrease "
           "precision">;
def mreassociate : Flag<["-"], "mreassociate">,
  HelpText<"Allow reassociation transformations for floating-point instructions">;
def mfloat_abi : Separate<["-"], "mfloat-abi">,
  HelpText<"The float ABI to use">;
def mtp : Separate<["-"], "mtp">,
  HelpText<"Mode for reading thread pointer">;
def mlimit_float_precision : Separate<["-"], "mlimit-float-precision">,
  HelpText<"Limit float precision to the given value">;
def split_stacks : Flag<["-"], "split-stacks">,
  HelpText<"Try to use a split stack if possible.">;
def mno_zero_initialized_in_bss : Flag<["-"], "mno-zero-initialized-in-bss">,
  HelpText<"Do not put zero initialized data in the BSS">;
def mregparm : Separate<["-"], "mregparm">,
  HelpText<"Limit the number of registers available for integer arguments">;
def munwind_tables : Flag<["-"], "munwind-tables">,
  HelpText<"Generate unwinding tables for all functions">;
def mconstructor_aliases : Flag<["-"], "mconstructor-aliases">,
  HelpText<"Emit complete constructors and destructors as aliases when possible">;
def mlink_bitcode_file : Separate<["-"], "mlink-bitcode-file">,
  HelpText<"Link the given bitcode file before performing optimizations.">;
def mlink_builtin_bitcode : Separate<["-"], "mlink-builtin-bitcode">,
  HelpText<"Link and internalize needed symbols from the given bitcode file "
           "before performing optimizations.">;
def mlink_cuda_bitcode : Separate<["-"], "mlink-cuda-bitcode">,
  Alias<mlink_builtin_bitcode>;
def vectorize_loops : Flag<["-"], "vectorize-loops">,
  HelpText<"Run the Loop vectorization passes">;
def vectorize_slp : Flag<["-"], "vectorize-slp">,
  HelpText<"Run the SLP vectorization passes">;
def dependent_lib : Joined<["--"], "dependent-lib=">,
  HelpText<"Add dependent library">;
def linker_option : Joined<["--"], "linker-option=">,
  HelpText<"Add linker option">;
def fsanitize_coverage_type : Joined<["-"], "fsanitize-coverage-type=">,
                              HelpText<"Sanitizer coverage type">;
def fsanitize_coverage_indirect_calls
    : Flag<["-"], "fsanitize-coverage-indirect-calls">,
      HelpText<"Enable sanitizer coverage for indirect calls">;
def fsanitize_coverage_trace_bb
    : Flag<["-"], "fsanitize-coverage-trace-bb">,
      HelpText<"Enable basic block tracing in sanitizer coverage">;
def fsanitize_coverage_trace_cmp
    : Flag<["-"], "fsanitize-coverage-trace-cmp">,
      HelpText<"Enable cmp instruction tracing in sanitizer coverage">;
def fsanitize_coverage_trace_div
    : Flag<["-"], "fsanitize-coverage-trace-div">,
      HelpText<"Enable div instruction tracing in sanitizer coverage">;
def fsanitize_coverage_trace_gep
    : Flag<["-"], "fsanitize-coverage-trace-gep">,
      HelpText<"Enable gep instruction tracing in sanitizer coverage">;
def fsanitize_coverage_8bit_counters
    : Flag<["-"], "fsanitize-coverage-8bit-counters">,
      HelpText<"Enable frequency counters in sanitizer coverage">;
def fsanitize_coverage_inline_8bit_counters
    : Flag<["-"], "fsanitize-coverage-inline-8bit-counters">,
      HelpText<"Enable inline 8-bit counters in sanitizer coverage">;
def fsanitize_coverage_pc_table
    : Flag<["-"], "fsanitize-coverage-pc-table">,
      HelpText<"Create a table of coverage-instrumented PCs">;
def fsanitize_coverage_trace_pc
    : Flag<["-"], "fsanitize-coverage-trace-pc">,
      HelpText<"Enable PC tracing in sanitizer coverage">;
def fsanitize_coverage_trace_pc_guard
    : Flag<["-"], "fsanitize-coverage-trace-pc-guard">,
      HelpText<"Enable PC tracing with guard in sanitizer coverage">;
def fsanitize_coverage_no_prune
    : Flag<["-"], "fsanitize-coverage-no-prune">,
      HelpText<"Disable coverage pruning (i.e. instrument all blocks/edges)">;
def fsanitize_coverage_stack_depth
    : Flag<["-"], "fsanitize-coverage-stack-depth">,
      HelpText<"Enable max stack depth tracing">;
def fprofile_instrument_EQ : Joined<["-"], "fprofile-instrument=">,
    HelpText<"Enable PGO instrumentation. The accepted value is clang, llvm, "
             "or none">, Values<"none,clang,llvm">;
def fprofile_instrument_path_EQ : Joined<["-"], "fprofile-instrument-path=">,
    HelpText<"Generate instrumented code to collect execution counts into "
             "<file> (overridden by LLVM_PROFILE_FILE env var)">;
def fprofile_instrument_use_path_EQ :
    Joined<["-"], "fprofile-instrument-use-path=">,
    HelpText<"Specify the profile path in PGO use compilation">;
def flto_visibility_public_std:
    Flag<["-"], "flto-visibility-public-std">,
    HelpText<"Use public LTO visibility for classes in std and stdext namespaces">;
def flto_unit: Flag<["-"], "flto-unit">,
    HelpText<"Emit IR to support LTO unit features (CFI, whole program vtable opt)">;
def fno_lto_unit: Flag<["-"], "fno-lto-unit">;
def fthin_link_bitcode_EQ : Joined<["-"], "fthin-link-bitcode=">,
    HelpText<"Write minimized bitcode to <file> for the ThinLTO thin link only">;
def fdebug_pass_manager : Flag<["-"], "fdebug-pass-manager">,
    HelpText<"Prints debug information for the new pass manager">;
def fno_debug_pass_manager : Flag<["-"], "fno-debug-pass-manager">,
    HelpText<"Disables debug printing for the new pass manager">;
// The driver option takes the key as a parameter to the -msign-return-address=
// and -mbranch-protection= options, but CC1 has a separate option so we
// don't have to parse the parameter twice.
def msign_return_address_key_EQ : Joined<["-"], "msign-return-address-key=">,
    Values<"a_key,b_key">;
def mbranch_target_enforce : Flag<["-"], "mbranch-target-enforce">;
def fno_dllexport_inlines : Flag<["-"], "fno-dllexport-inlines">;

//===----------------------------------------------------------------------===//
// Dependency Output Options
//===----------------------------------------------------------------------===//

def sys_header_deps : Flag<["-"], "sys-header-deps">,
  HelpText<"Include system headers in dependency output">;
def module_file_deps : Flag<["-"], "module-file-deps">,
  HelpText<"Include module files in dependency output">;
def header_include_file : Separate<["-"], "header-include-file">,
  HelpText<"Filename (or -) to write header include output to">;
def show_includes : Flag<["--"], "show-includes">,
  HelpText<"Print cl.exe style /showIncludes to stdout">;

//===----------------------------------------------------------------------===//
// Diagnostic Options
//===----------------------------------------------------------------------===//

def diagnostic_log_file : Separate<["-"], "diagnostic-log-file">,
  HelpText<"Filename (or -) to log diagnostics to">;
def diagnostic_serialized_file : Separate<["-"], "serialize-diagnostic-file">,
  MetaVarName<"<filename>">,
  HelpText<"File for serializing diagnostics in a binary format">;

def fdiagnostics_format : Separate<["-"], "fdiagnostics-format">,
  HelpText<"Change diagnostic formatting to match IDE and command line tools">, Values<"clang,msvc,msvc-fallback,vi">;
def fdiagnostics_show_category : Separate<["-"], "fdiagnostics-show-category">,
  HelpText<"Print diagnostic category">, Values<"none,id,name">;
def fno_diagnostics_use_presumed_location : Flag<["-"], "fno-diagnostics-use-presumed-location">,
  HelpText<"Ignore #line directives when displaying diagnostic locations">;
def ftabstop : Separate<["-"], "ftabstop">, MetaVarName<"<N>">,
  HelpText<"Set the tab stop distance.">;
def ferror_limit : Separate<["-"], "ferror-limit">, MetaVarName<"<N>">,
  HelpText<"Set the maximum number of errors to emit before stopping (0 = no limit).">;
def fmacro_backtrace_limit : Separate<["-"], "fmacro-backtrace-limit">, MetaVarName<"<N>">,
  HelpText<"Set the maximum number of entries to print in a macro expansion backtrace (0 = no limit).">;
def ftemplate_backtrace_limit : Separate<["-"], "ftemplate-backtrace-limit">, MetaVarName<"<N>">,
  HelpText<"Set the maximum number of entries to print in a template instantiation backtrace (0 = no limit).">;
def fconstexpr_backtrace_limit : Separate<["-"], "fconstexpr-backtrace-limit">, MetaVarName<"<N>">,
  HelpText<"Set the maximum number of entries to print in a constexpr evaluation backtrace (0 = no limit).">;
def fspell_checking_limit : Separate<["-"], "fspell-checking-limit">, MetaVarName<"<N>">,
  HelpText<"Set the maximum number of times to perform spell checking on unrecognized identifiers (0 = no limit).">;
def fcaret_diagnostics_max_lines :
  Separate<["-"], "fcaret-diagnostics-max-lines">, MetaVarName<"<N>">,
  HelpText<"Set the maximum number of source lines to show in a caret diagnostic">;
def fmessage_length : Separate<["-"], "fmessage-length">, MetaVarName<"<N>">,
  HelpText<"Format message diagnostics so that they fit within N columns or fewer, when possible.">;
def verify_EQ : CommaJoined<["-"], "verify=">,
  MetaVarName<"<prefixes>">,
  HelpText<"Verify diagnostic output using comment directives that start with"
           " prefixes in the comma-separated sequence <prefixes>">;
def verify : Flag<["-"], "verify">,
  HelpText<"Equivalent to -verify=expected">;
def verify_ignore_unexpected : Flag<["-"], "verify-ignore-unexpected">,
  HelpText<"Ignore unexpected diagnostic messages">;
def verify_ignore_unexpected_EQ : CommaJoined<["-"], "verify-ignore-unexpected=">,
  HelpText<"Ignore unexpected diagnostic messages">;
def Wno_rewrite_macros : Flag<["-"], "Wno-rewrite-macros">,
  HelpText<"Silence ObjC rewriting warnings">;

//===----------------------------------------------------------------------===//
// Frontend Options
//===----------------------------------------------------------------------===//

// This isn't normally used, it is just here so we can parse a
// CompilerInvocation out of a driver-derived argument vector.
def cc1 : Flag<["-"], "cc1">;
def cc1as : Flag<["-"], "cc1as">;

def ast_merge : Separate<["-"], "ast-merge">,
  MetaVarName<"<ast file>">,
  HelpText<"Merge the given AST file into the translation unit being compiled.">;
def aux_triple : Separate<["-"], "aux-triple">,
  HelpText<"Auxiliary target triple.">;
def code_completion_at : Separate<["-"], "code-completion-at">,
  MetaVarName<"<file>:<line>:<column>">,
  HelpText<"Dump code-completion information at a location">;
def remap_file : Separate<["-"], "remap-file">,
  MetaVarName<"<from>;<to>">,
  HelpText<"Replace the contents of the <from> file with the contents of the <to> file">;
def code_completion_at_EQ : Joined<["-"], "code-completion-at=">,
  Alias<code_completion_at>;
def code_completion_macros : Flag<["-"], "code-completion-macros">,
  HelpText<"Include macros in code-completion results">;
def code_completion_patterns : Flag<["-"], "code-completion-patterns">,
  HelpText<"Include code patterns in code-completion results">;
def no_code_completion_globals : Flag<["-"], "no-code-completion-globals">,
  HelpText<"Do not include global declarations in code-completion results.">;
def no_code_completion_ns_level_decls : Flag<["-"], "no-code-completion-ns-level-decls">,
  HelpText<"Do not include declarations inside namespaces (incl. global namespace) in the code-completion results.">;
def code_completion_brief_comments : Flag<["-"], "code-completion-brief-comments">,
  HelpText<"Include brief documentation comments in code-completion results.">;
def code_completion_with_fixits : Flag<["-"], "code-completion-with-fixits">,
  HelpText<"Include code completion results which require small fix-its.">;
def disable_free : Flag<["-"], "disable-free">,
  HelpText<"Disable freeing of memory on exit">;
def discard_value_names : Flag<["-"], "discard-value-names">,
  HelpText<"Discard value names in LLVM IR">;
def load : Separate<["-"], "load">, MetaVarName<"<dsopath>">,
  HelpText<"Load the named plugin (dynamic shared object)">;
def plugin : Separate<["-"], "plugin">, MetaVarName<"<name>">,
  HelpText<"Use the named plugin action instead of the default action (use \"help\" to list available options)">;
def plugin_arg : JoinedAndSeparate<["-"], "plugin-arg-">,
    MetaVarName<"<name> <arg>">,
    HelpText<"Pass <arg> to plugin <name>">;
def add_plugin : Separate<["-"], "add-plugin">, MetaVarName<"<name>">,
  HelpText<"Use the named plugin action in addition to the default action">;
def ast_dump_filter : Separate<["-"], "ast-dump-filter">,
  MetaVarName<"<dump_filter>">,
  HelpText<"Use with -ast-dump or -ast-print to dump/print only AST declaration"
           " nodes having a certain substring in a qualified name. Use"
           " -ast-list to list all filterable declaration node names.">;
def fno_modules_global_index : Flag<["-"], "fno-modules-global-index">,
  HelpText<"Do not automatically generate or update the global module index">;
def fno_modules_error_recovery : Flag<["-"], "fno-modules-error-recovery">,
  HelpText<"Do not automatically import modules for error recovery">;
def fmodule_map_file_home_is_cwd : Flag<["-"], "fmodule-map-file-home-is-cwd">,
  HelpText<"Use the current working directory as the home directory of "
           "module maps specified by -fmodule-map-file=<FILE>">;
def fmodule_feature : Separate<["-"], "fmodule-feature">,
  MetaVarName<"<feature>">,
  HelpText<"Enable <feature> in module map requires declarations">;
def fmodules_embed_file_EQ : Joined<["-"], "fmodules-embed-file=">,
  MetaVarName<"<file>">,
  HelpText<"Embed the contents of the specified file into the module file "
           "being compiled.">;
def fmodules_embed_all_files : Joined<["-"], "fmodules-embed-all-files">,
  HelpText<"Embed the contents of all files read by this compilation into "
           "the produced module file.">;
def fmodules_local_submodule_visibility :
  Flag<["-"], "fmodules-local-submodule-visibility">,
  HelpText<"Enforce name visibility rules across submodules of the same "
           "top-level module.">;
def fmodules_codegen :
  Flag<["-"], "fmodules-codegen">,
  HelpText<"Generate code for uses of this module that assumes an explicit "
           "object file will be built for the module">;
def fmodules_debuginfo :
  Flag<["-"], "fmodules-debuginfo">,
  HelpText<"Generate debug info for types in an object file built from this "
           "module and do not generate them elsewhere">;
def fmodule_format_EQ : Joined<["-"], "fmodule-format=">,
  HelpText<"Select the container format for clang modules and PCH. "
           "Supported options are 'raw' and 'obj'.">;
def ftest_module_file_extension_EQ :
  Joined<["-"], "ftest-module-file-extension=">,
  HelpText<"introduce a module file extension for testing purposes. "
           "The argument is parsed as blockname:major:minor:hashed:user info">;
def fconcepts_ts : Flag<["-"], "fconcepts-ts">,
  HelpText<"Enable C++ Extensions for Concepts.">;

let Group = Action_Group in {

def Eonly : Flag<["-"], "Eonly">,
  HelpText<"Just run preprocessor, no output (for timings)">;
def dump_raw_tokens : Flag<["-"], "dump-raw-tokens">,
  HelpText<"Lex file in raw mode and dump raw tokens">;
def analyze : Flag<["-"], "analyze">,
  HelpText<"Run static analysis engine">;
def dump_tokens : Flag<["-"], "dump-tokens">,
  HelpText<"Run preprocessor, dump internal rep of tokens">;
def init_only : Flag<["-"], "init-only">,
  HelpText<"Only execute frontend initialization">;
def fixit : Flag<["-"], "fixit">,
  HelpText<"Apply fix-it advice to the input source">;
def fixit_EQ : Joined<["-"], "fixit=">,
  HelpText<"Apply fix-it advice creating a file with the given suffix">;
def print_preamble : Flag<["-"], "print-preamble">,
  HelpText<"Print the \"preamble\" of a file, which is a candidate for implicit"
           " precompiled headers.">;
def emit_html : Flag<["-"], "emit-html">,
  HelpText<"Output input source as HTML">;
def ast_print : Flag<["-"], "ast-print">,
  HelpText<"Build ASTs and then pretty-print them">;
def ast_list : Flag<["-"], "ast-list">,
  HelpText<"Build ASTs and print the list of declaration node qualified names">;
def ast_dump : Flag<["-"], "ast-dump">,
  HelpText<"Build ASTs and then debug dump them">;
def ast_dump_all : Flag<["-"], "ast-dump-all">,
  HelpText<"Build ASTs and then debug dump them, forcing deserialization">;
def templight_dump : Flag<["-"], "templight-dump">,
  HelpText<"Dump templight information to stdout">;
def ast_dump_lookups : Flag<["-"], "ast-dump-lookups">,
  HelpText<"Build ASTs and then debug dump their name lookup tables">;
def ast_view : Flag<["-"], "ast-view">,
  HelpText<"Build ASTs and view them with GraphViz">;
def emit_module : Flag<["-"], "emit-module">,
  HelpText<"Generate pre-compiled module file from a module map">;
def emit_module_interface : Flag<["-"], "emit-module-interface">,
  HelpText<"Generate pre-compiled module file from a C++ module interface">;
def emit_header_module : Flag<["-"], "emit-header-module">,
  HelpText<"Generate pre-compiled module file from a set of header files">;
def emit_pch : Flag<["-"], "emit-pch">,
  HelpText<"Generate pre-compiled header file">;
def emit_llvm_bc : Flag<["-"], "emit-llvm-bc">,
  HelpText<"Build ASTs then convert to LLVM, emit .bc file">;
def emit_llvm_only : Flag<["-"], "emit-llvm-only">,
  HelpText<"Build ASTs and convert to LLVM, discarding output">;
def emit_codegen_only : Flag<["-"], "emit-codegen-only">,
  HelpText<"Generate machine code, but discard output">;
def emit_obj : Flag<["-"], "emit-obj">,
  HelpText<"Emit native object files">;
def rewrite_test : Flag<["-"], "rewrite-test">,
  HelpText<"Rewriter playground">;
def rewrite_macros : Flag<["-"], "rewrite-macros">,
  HelpText<"Expand macros without full preprocessing">;
def migrate : Flag<["-"], "migrate">,
  HelpText<"Migrate source code">;
def compiler_options_dump : Flag<["-"], "compiler-options-dump">,
  HelpText<"Dump the compiler configuration options">;
}

def emit_llvm_uselists : Flag<["-"], "emit-llvm-uselists">,
  HelpText<"Preserve order of LLVM use-lists when serializing">;
def no_emit_llvm_uselists : Flag<["-"], "no-emit-llvm-uselists">,
  HelpText<"Don't preserve order of LLVM use-lists when serializing">;

def mt_migrate_directory : Separate<["-"], "mt-migrate-directory">,
  HelpText<"Directory for temporary files produced during ARC or ObjC migration">;
def arcmt_check : Flag<["-"], "arcmt-check">,
  HelpText<"Check for ARC migration issues that need manual handling">;
def arcmt_modify : Flag<["-"], "arcmt-modify">,
  HelpText<"Apply modifications to files to conform to ARC">;
def arcmt_migrate : Flag<["-"], "arcmt-migrate">,
  HelpText<"Apply modifications and produces temporary files that conform to ARC">;

def opt_record_file : Separate<["-"], "opt-record-file">,
  HelpText<"File name to use for YAML optimization record output">;

def print_stats : Flag<["-"], "print-stats">,
  HelpText<"Print performance metrics and statistics">;
def stats_file : Joined<["-"], "stats-file=">,
  HelpText<"Filename to write statistics to">;
def fdump_record_layouts : Flag<["-"], "fdump-record-layouts">,
  HelpText<"Dump record layout information">;
def fdump_record_layouts_simple : Flag<["-"], "fdump-record-layouts-simple">,
  HelpText<"Dump record layout information in a simple form used for testing">;
def fix_what_you_can : Flag<["-"], "fix-what-you-can">,
  HelpText<"Apply fix-it advice even in the presence of unfixable errors">;
def fix_only_warnings : Flag<["-"], "fix-only-warnings">,
  HelpText<"Apply fix-it advice only for warnings, not errors">;
def fixit_recompile : Flag<["-"], "fixit-recompile">,
  HelpText<"Apply fix-it changes and recompile">;
def fixit_to_temp : Flag<["-"], "fixit-to-temporary">,
  HelpText<"Apply fix-it changes to temporary files">;

def foverride_record_layout_EQ : Joined<["-"], "foverride-record-layout=">,
  HelpText<"Override record layouts with those in the given file">;
def pch_through_header_EQ : Joined<["-"], "pch-through-header=">,
  HelpText<"Stop PCH generation after including this file.  When using a PCH, "
           "skip tokens until after this file is included.">;
def pch_through_hdrstop_create : Flag<["-"], "pch-through-hdrstop-create">,
  HelpText<"When creating a PCH, stop PCH generation after #pragma hdrstop.">;
def pch_through_hdrstop_use : Flag<["-"], "pch-through-hdrstop-use">,
  HelpText<"When using a PCH, skip tokens until after a #pragma hdrstop.">;
def fno_pch_timestamp : Flag<["-"], "fno-pch-timestamp">,
  HelpText<"Disable inclusion of timestamp in precompiled headers">;
def building_pch_with_obj : Flag<["-"], "building-pch-with-obj">,
  HelpText<"This compilation is part of building a PCH with corresponding object file.">;

def aligned_alloc_unavailable : Flag<["-"], "faligned-alloc-unavailable">,
  HelpText<"Aligned allocation/deallocation functions are unavailable">;

//===----------------------------------------------------------------------===//
// Language Options
//===----------------------------------------------------------------------===//

let Flags = [CC1Option, CC1AsOption, NoDriverOption] in {

def version : Flag<["-"], "version">,
  HelpText<"Print the compiler version">;
def main_file_name : Separate<["-"], "main-file-name">,
  HelpText<"Main file name to use for debug info">;
def split_dwarf_file : Separate<["-"], "split-dwarf-file">,
  HelpText<"File name to use for split dwarf debug info output">;

}

def fblocks_runtime_optional : Flag<["-"], "fblocks-runtime-optional">,
  HelpText<"Weakly link in the blocks runtime">;
def fexternc_nounwind : Flag<["-"], "fexternc-nounwind">,
  HelpText<"Assume all functions with C linkage do not unwind">;
def enable_split_dwarf : Flag<["-"], "enable-split-dwarf">,
  HelpText<"Use DWARF fission in 'split' mode">;
def enable_split_dwarf_EQ : Joined<["-"], "enable-split-dwarf=">,
  HelpText<"Set DWARF fission mode to either 'split' or 'single'">, Values<"split,single">;
def fno_wchar : Flag<["-"], "fno-wchar">,
  HelpText<"Disable C++ builtin type wchar_t">;
def fconstant_string_class : Separate<["-"], "fconstant-string-class">,
  MetaVarName<"<class name>">,
  HelpText<"Specify the class to use for constant Objective-C string objects.">;
def fobjc_arc_cxxlib_EQ : Joined<["-"], "fobjc-arc-cxxlib=">,
  HelpText<"Objective-C++ Automatic Reference Counting standard library kind">, Values<"libc++,libstdc++,none">;
def fobjc_runtime_has_weak : Flag<["-"], "fobjc-runtime-has-weak">,
  HelpText<"The target Objective-C runtime supports ARC weak operations">;
def fobjc_dispatch_method_EQ : Joined<["-"], "fobjc-dispatch-method=">,
  HelpText<"Objective-C dispatch method to use">, Values<"legacy,non-legacy,mixed">;
def disable_objc_default_synthesize_properties : Flag<["-"], "disable-objc-default-synthesize-properties">,
  HelpText<"disable the default synthesis of Objective-C properties">;
def fencode_extended_block_signature : Flag<["-"], "fencode-extended-block-signature">,
  HelpText<"enable extended encoding of block type signature">;
def function_alignment : Separate<["-"], "function-alignment">,
    HelpText<"default alignment for functions">;
def pic_level : Separate<["-"], "pic-level">,
  HelpText<"Value for __PIC__">;
def pic_is_pie : Flag<["-"], "pic-is-pie">,
  HelpText<"File is for a position independent executable">;
def fno_validate_pch : Flag<["-"], "fno-validate-pch">,
  HelpText<"Disable validation of precompiled headers">;
def fallow_pch_with_errors : Flag<["-"], "fallow-pch-with-compiler-errors">,
  HelpText<"Accept a PCH file that was created with compiler errors">;
def dump_deserialized_pch_decls : Flag<["-"], "dump-deserialized-decls">,
  HelpText<"Dump declarations that are deserialized from PCH, for testing">;
def error_on_deserialized_pch_decl : Separate<["-"], "error-on-deserialized-decl">,
  HelpText<"Emit error if a specific declaration is deserialized from PCH, for testing">;
def error_on_deserialized_pch_decl_EQ : Joined<["-"], "error-on-deserialized-decl=">,
  Alias<error_on_deserialized_pch_decl>;
def static_define : Flag<["-"], "static-define">,
  HelpText<"Should __STATIC__ be defined">;
def stack_protector : Separate<["-"], "stack-protector">,
  HelpText<"Enable stack protectors">;
def stack_protector_buffer_size : Separate<["-"], "stack-protector-buffer-size">,
  HelpText<"Lower bound for a buffer to be considered for stack protection">;
def fvisibility : Separate<["-"], "fvisibility">,
  HelpText<"Default type and symbol visibility">;
def ftype_visibility : Separate<["-"], "ftype-visibility">,
  HelpText<"Default type visibility">;
def ftemplate_depth : Separate<["-"], "ftemplate-depth">,
  HelpText<"Maximum depth of recursive template instantiation">;
def foperator_arrow_depth : Separate<["-"], "foperator-arrow-depth">,
  HelpText<"Maximum number of 'operator->'s to call for a member access">;
def fconstexpr_depth : Separate<["-"], "fconstexpr-depth">,
  HelpText<"Maximum depth of recursive constexpr function calls">;
def fconstexpr_steps : Separate<["-"], "fconstexpr-steps">,
  HelpText<"Maximum number of steps in constexpr function evaluation">;
def fbracket_depth : Separate<["-"], "fbracket-depth">,
  HelpText<"Maximum nesting level for parentheses, brackets, and braces">;
def fconst_strings : Flag<["-"], "fconst-strings">,
  HelpText<"Use a const qualified type for string literals in C and ObjC">;
def fno_const_strings : Flag<["-"], "fno-const-strings">,
  HelpText<"Don't use a const qualified type for string literals in C and ObjC">;
def fno_bitfield_type_align : Flag<["-"], "fno-bitfield-type-align">,
  HelpText<"Ignore bit-field types when aligning structures">;
def ffake_address_space_map : Flag<["-"], "ffake-address-space-map">,
  HelpText<"Use a fake address space map; OpenCL testing purposes only">;
def faddress_space_map_mangling_EQ : Joined<["-"], "faddress-space-map-mangling=">, MetaVarName<"<yes|no|target>">,
  HelpText<"Set the mode for address space map based mangling; OpenCL testing purposes only">;
def funknown_anytype : Flag<["-"], "funknown-anytype">,
  HelpText<"Enable parser support for the __unknown_anytype type; for testing purposes only">;
def fdebugger_support : Flag<["-"], "fdebugger-support">,
  HelpText<"Enable special debugger support behavior">;
def fdebugger_cast_result_to_id : Flag<["-"], "fdebugger-cast-result-to-id">,
  HelpText<"Enable casting unknown expression results to id">;
def fdebugger_objc_literal : Flag<["-"], "fdebugger-objc-literal">,
  HelpText<"Enable special debugger support for Objective-C subscripting and literals">;
def fdeprecated_macro : Flag<["-"], "fdeprecated-macro">,
  HelpText<"Defines the __DEPRECATED macro">;
def fno_deprecated_macro : Flag<["-"], "fno-deprecated-macro">,
  HelpText<"Undefines the __DEPRECATED macro">;
def fobjc_subscripting_legacy_runtime : Flag<["-"], "fobjc-subscripting-legacy-runtime">,
  HelpText<"Allow Objective-C array and dictionary subscripting in legacy runtime">;
def vtordisp_mode_EQ : Joined<["-"], "vtordisp-mode=">,
  HelpText<"Control vtordisp placement on win32 targets">;
def fnative_half_type: Flag<["-"], "fnative-half-type">,
  HelpText<"Use the native half type for __fp16 instead of promoting to float">;
def fnative_half_arguments_and_returns : Flag<["-"], "fnative-half-arguments-and-returns">,
  HelpText<"Use the native __fp16 type for arguments and returns (and skip ABI-specific lowering)">;
def fallow_half_arguments_and_returns : Flag<["-"], "fallow-half-arguments-and-returns">,
  HelpText<"Allow function arguments and returns of type half">;
def fdefault_calling_conv_EQ : Joined<["-"], "fdefault-calling-conv=">,
  HelpText<"Set default calling convention">, Values<"cdecl,fastcall,stdcall,vectorcall,regcall">;
def finclude_default_header : Flag<["-"], "finclude-default-header">,
  HelpText<"Include the default header file for OpenCL">;
def fpreserve_vec3_type : Flag<["-"], "fpreserve-vec3-type">,
  HelpText<"Preserve 3-component vector type">;
def fwchar_type_EQ : Joined<["-"], "fwchar-type=">,
  HelpText<"Select underlying type for wchar_t">, Values<"char,short,int">;
def fsigned_wchar : Flag<["-"], "fsigned-wchar">,
  HelpText<"Use a signed type for wchar_t">;
def fno_signed_wchar : Flag<["-"], "fno-signed-wchar">,
  HelpText<"Use an unsigned type for wchar_t">;

// FIXME: Remove these entirely once functionality/tests have been excised.
def fobjc_gc_only : Flag<["-"], "fobjc-gc-only">, Group<f_Group>,
  HelpText<"Use GC exclusively for Objective-C related memory management">;
def fobjc_gc : Flag<["-"], "fobjc-gc">, Group<f_Group>,
  HelpText<"Enable Objective-C garbage collection">;

//===----------------------------------------------------------------------===//
// Header Search Options
//===----------------------------------------------------------------------===//

def nostdsysteminc : Flag<["-"], "nostdsysteminc">,
  HelpText<"Disable standard system #include directories">;
def fdisable_module_hash : Flag<["-"], "fdisable-module-hash">,
  HelpText<"Disable the module hash">;
def fmodules_hash_content : Flag<["-"], "fmodules-hash-content">,
  HelpText<"Enable hashing the content of a module file">;
def c_isystem : JoinedOrSeparate<["-"], "c-isystem">, MetaVarName<"<directory>">,
  HelpText<"Add directory to the C SYSTEM include search path">;
def objc_isystem : JoinedOrSeparate<["-"], "objc-isystem">,
  MetaVarName<"<directory>">,
  HelpText<"Add directory to the ObjC SYSTEM include search path">;
def objcxx_isystem : JoinedOrSeparate<["-"], "objcxx-isystem">,
  MetaVarName<"<directory>">,
  HelpText<"Add directory to the ObjC++ SYSTEM include search path">;
def internal_isystem : JoinedOrSeparate<["-"], "internal-isystem">,
  MetaVarName<"<directory>">,
  HelpText<"Add directory to the internal system include search path; these "
           "are assumed to not be user-provided and are used to model system "
           "and standard headers' paths.">;
def internal_externc_isystem : JoinedOrSeparate<["-"], "internal-externc-isystem">,
  MetaVarName<"<directory>">,
  HelpText<"Add directory to the internal system include search path with "
           "implicit extern \"C\" semantics; these are assumed to not be "
           "user-provided and are used to model system and standard headers' "
           "paths.">;

//===----------------------------------------------------------------------===//
// Preprocessor Options
//===----------------------------------------------------------------------===//

def chain_include : Separate<["-"], "chain-include">, MetaVarName<"<file>">,
  HelpText<"Include and chain a header file after turning it into PCH">;
def preamble_bytes_EQ : Joined<["-"], "preamble-bytes=">,
  HelpText<"Assume that the precompiled header is a precompiled preamble "
           "covering the first N bytes of the main file">;
def detailed_preprocessing_record : Flag<["-"], "detailed-preprocessing-record">,
  HelpText<"include a detailed record of preprocessing actions">;

//===----------------------------------------------------------------------===//
// OpenCL Options
//===----------------------------------------------------------------------===//

def cl_ext_EQ : CommaJoined<["-"], "cl-ext=">,
  HelpText<"OpenCL only. Enable or disable OpenCL extensions. The argument is a comma-separated sequence of one or more extension names, each prefixed by '+' or '-'.">;

//===----------------------------------------------------------------------===//
// CUDA Options
//===----------------------------------------------------------------------===//

def fcuda_is_device : Flag<["-"], "fcuda-is-device">,
  HelpText<"Generate code for CUDA device">;
def fcuda_include_gpubinary : Separate<["-"], "fcuda-include-gpubinary">,
  HelpText<"Incorporate CUDA device-side binary into host object file.">;
def fcuda_allow_variadic_functions : Flag<["-"], "fcuda-allow-variadic-functions">,
  HelpText<"Allow variadic functions in CUDA device code.">;
def fno_cuda_host_device_constexpr : Flag<["-"], "fno-cuda-host-device-constexpr">,
  HelpText<"Don't treat unattributed constexpr functions as __host__ __device__.">;

//===----------------------------------------------------------------------===//
// OpenMP Options
//===----------------------------------------------------------------------===//

def fopenmp_is_device : Flag<["-"], "fopenmp-is-device">,
  HelpText<"Generate code only for an OpenMP target device.">;
def fopenmp_host_ir_file_path : Separate<["-"], "fopenmp-host-ir-file-path">,
  HelpText<"Path to the IR file produced by the frontend for the host.">;

} // let Flags = [CC1Option]


//===----------------------------------------------------------------------===//
// cc1as-only Options
//===----------------------------------------------------------------------===//

let Flags = [CC1AsOption, NoDriverOption] in {

// Language Options
def n : Flag<["-"], "n">,
  HelpText<"Don't automatically start assembly file with a text section">;

// Frontend Options
def filetype : Separate<["-"], "filetype">,
    HelpText<"Specify the output file type ('asm', 'null', or 'obj')">;

// Transliterate Options
def output_asm_variant : Separate<["-"], "output-asm-variant">,
    HelpText<"Select the asm variant index to use for output">;
def show_encoding : Flag<["-"], "show-encoding">,
    HelpText<"Show instruction encoding information in transliterate mode">;
def show_inst : Flag<["-"], "show-inst">,
    HelpText<"Show internal instruction representation in transliterate mode">;

// Assemble Options
def dwarf_debug_producer : Separate<["-"], "dwarf-debug-producer">,
  HelpText<"The string to embed in the Dwarf debug AT_producer record.">;

def defsym : Separate<["-"], "defsym">,
  HelpText<"Define a value for a symbol">;
} // let Flags = [CC1AsOption]
