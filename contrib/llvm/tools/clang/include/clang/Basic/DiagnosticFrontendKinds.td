//==--- DiagnosticFrontendKinds.td - frontend diagnostics -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

class BackendInfo : CatBackend, ShowInSystemHeader;

let Component = "Frontend" in {

def err_fe_error_opening : Error<"error opening '%0': %1">;
def err_fe_error_reading : Error<"error reading '%0'">;
def err_fe_error_reading_stdin : Error<"error reading stdin: %0">;
def err_fe_error_backend : Error<"error in backend: %0">, DefaultFatal;

def err_fe_inline_asm : Error<"%0">, CatInlineAsm;
def warn_fe_inline_asm : Warning<"%0">, CatInlineAsm, InGroup<BackendInlineAsm>;
def note_fe_inline_asm : Note<"%0">, CatInlineAsm;
def note_fe_inline_asm_here : Note<"instantiated into assembly here">;
def err_fe_cannot_link_module : Error<"cannot link module '%0': %1">,
  DefaultFatal;

def warn_fe_frame_larger_than : Warning<"stack frame size of %0 bytes in %q1">,
    BackendInfo, InGroup<BackendFrameLargerThanEQ>;
def warn_fe_backend_frame_larger_than: Warning<"%0">,
    BackendInfo, InGroup<BackendFrameLargerThanEQ>;
def err_fe_backend_frame_larger_than: Error<"%0">, BackendInfo;
def note_fe_backend_frame_larger_than: Note<"%0">, BackendInfo;

def warn_fe_backend_plugin: Warning<"%0">, BackendInfo, InGroup<BackendPlugin>;
def err_fe_backend_plugin: Error<"%0">, BackendInfo;
def remark_fe_backend_plugin: Remark<"%0">, BackendInfo, InGroup<RemarkBackendPlugin>;
def note_fe_backend_plugin: Note<"%0">, BackendInfo;

def warn_fe_override_module : Warning<
    "overriding the module target triple with %0">,
    InGroup<DiagGroup<"override-module">>;

def remark_fe_backend_optimization_remark : Remark<"%0">, BackendInfo,
    InGroup<BackendOptimizationRemark>;
def remark_fe_backend_optimization_remark_missed : Remark<"%0">, BackendInfo,
    InGroup<BackendOptimizationRemarkMissed>;
def remark_fe_backend_optimization_remark_analysis : Remark<"%0">, BackendInfo,
    InGroup<BackendOptimizationRemarkAnalysis>;
def remark_fe_backend_optimization_remark_analysis_fpcommute : Remark<"%0; "
    "allow reordering by specifying '#pragma clang loop vectorize(enable)' "
    "before the loop or by providing the compiler option '-ffast-math'.">,
    BackendInfo, InGroup<BackendOptimizationRemarkAnalysis>;
def remark_fe_backend_optimization_remark_analysis_aliasing : Remark<"%0; "
    "allow reordering by specifying '#pragma clang loop vectorize(enable)' "
    "before the loop. If the arrays will always be independent specify "
    "'#pragma clang loop vectorize(assume_safety)' before the loop or provide "
    "the '__restrict__' qualifier with the independent array arguments. "
    "Erroneous results will occur if these options are incorrectly applied!">,
    BackendInfo, InGroup<BackendOptimizationRemarkAnalysis>;
def warn_fe_backend_optimization_failure : Warning<"%0">, BackendInfo,
    InGroup<BackendOptimizationFailure>, DefaultWarn;
def note_fe_backend_invalid_loc : Note<"could "
  "not determine the original source location for %0:%1:%2">, BackendInfo;

def err_fe_backend_unsupported : Error<"%0">, BackendInfo;

def err_fe_invalid_code_complete_file : Error<
    "cannot locate code-completion file %0">, DefaultFatal;
def err_fe_stdout_binary : Error<"unable to change standard output to binary">,
  DefaultFatal;
def err_fe_dependency_file_requires_MT : Error<
    "-dependency-file requires at least one -MT or -MQ option">;
def err_fe_invalid_plugin_name : Error<
    "unable to find plugin '%0'">;
def err_fe_expected_compiler_job : Error<
    "unable to handle compilation, expected exactly one compiler job in '%0'">;
def err_fe_expected_clang_command : Error<
    "expected a clang compiler command">;
def err_fe_remap_missing_to_file : Error<
    "could not remap file '%0' to the contents of file '%1'">, DefaultFatal;
def err_fe_remap_missing_from_file : Error<
    "could not remap from missing file '%0'">, DefaultFatal;
def err_fe_unable_to_load_pch : Error<
    "unable to load PCH file">;
def err_fe_unable_to_load_plugin : Error<
    "unable to load plugin '%0': '%1'">;
def err_fe_unable_to_create_target : Error<
    "unable to create target: '%0'">;
def err_fe_unable_to_interface_with_target : Error<
    "unable to interface with target machine">;
def err_fe_unable_to_open_output : Error<
    "unable to open output file '%0': '%1'">;
def warn_fe_macro_contains_embedded_newline : Warning<
    "macro '%0' contains embedded newline; text after the newline is ignored">;
def warn_fe_cc_print_header_failure : Warning<
    "unable to open CC_PRINT_HEADERS file: %0 (using stderr)">;
def warn_fe_cc_log_diagnostics_failure : Warning<
    "unable to open CC_LOG_DIAGNOSTICS file: %0 (using stderr)">;
def warn_fe_unable_to_open_stats_file : Warning<
    "unable to open statistics output file '%0': '%1'">,
    InGroup<DiagGroup<"unable-to-open-stats-file">>;
def err_fe_no_pch_in_dir : Error<
    "no suitable precompiled header file found in directory '%0'">;
def err_fe_action_not_available : Error<
    "action %0 not compiled in">;
def err_fe_invalid_alignment : Error<
    "invalid value '%1' in '%0'; alignment must be a power of 2">;
def err_fe_invalid_wchar_type
    : Error<"invalid wchar_t type '%0'; must be one of 'char', 'short', 'int'">;
def err_fe_invalid_exception_model
   : Error<"invalid exception model '%0' for target '%1'">;

def warn_fe_serialized_diag_merge_failure : Warning<
    "unable to merge a subprocess's serialized diagnostics">,
    InGroup<SerializedDiagnostics>;
def warn_fe_serialized_diag_failure : Warning<
    "unable to open file %0 for serializing diagnostics (%1)">,
    InGroup<SerializedDiagnostics>;

def err_verify_missing_line : Error<
    "missing or invalid line number following '@' in expected %0">;
def err_verify_missing_file : Error<
    "file '%0' could not be located in expected %1">;
def err_verify_invalid_range : Error<
    "invalid range following '-' in expected %0">;
def err_verify_missing_start : Error<
    "cannot find start ('{{') of expected %0">;
def err_verify_missing_end : Error<
    "cannot find end ('}}') of expected %0">;
def err_verify_invalid_content : Error<
    "invalid expected %0: %1">;
def err_verify_missing_regex : Error<
    "cannot find start of regex ('{{') in %0">;
def err_verify_inconsistent_diags : Error<
    "'%0' diagnostics %select{expected|seen}1 but not %select{seen|expected}1: "
    "%2">;
def err_verify_invalid_no_diags : Error<
    "%select{expected|'expected-no-diagnostics'}0 directive cannot follow "
    "%select{'expected-no-diagnostics' directive|other expected directives}0">;
def err_verify_no_directives : Error<
    "no expected directives found: consider use of 'expected-no-diagnostics'">;

def note_fixit_applied : Note<"FIX-IT applied suggested code changes">;
def note_fixit_in_macro : Note<
    "FIX-IT unable to apply suggested code changes in a macro">;
def note_fixit_failed : Note<
    "FIX-IT unable to apply suggested code changes">;
def note_fixit_unfixed_error : Note<"FIX-IT detected an error it cannot fix">;
def warn_fixit_no_changes : Note<
    "FIX-IT detected errors it could not fix; no output will be generated">;

// PCH reader
def err_relocatable_without_isysroot : Error<
    "must specify system root with -isysroot when building a relocatable "
    "PCH file">;

def warn_unknown_diag_option : Warning<
    "unknown %select{warning|remark}0 option '%1'%select{|; did you mean '%3'?}2">,
    InGroup<UnknownWarningOption>;
def warn_unknown_warning_specifier : Warning<
    "unknown %0 warning specifier: '%1'">,
    InGroup<UnknownWarningOption>;

def warn_incompatible_analyzer_plugin_api : Warning<
    "checker plugin '%0' is not compatible with this version of the analyzer">,
    InGroup<DiagGroup<"analyzer-incompatible-plugin"> >;
def note_incompatible_analyzer_plugin_api : Note<
    "current API version is '%0', but plugin was compiled with version '%1'">;

def err_module_build_requires_fmodules : Error<
  "module compilation requires '-fmodules'">;
def err_module_interface_requires_modules_ts : Error<
  "module interface compilation requires '-fmodules-ts'">;
def err_header_module_requires_modules : Error<
  "header module compilation requires '-fmodules' or '-fmodules-ts'">;
def warn_module_config_mismatch : Warning<
  "module file %0 cannot be loaded due to a configuration mismatch with the current "
  "compilation">, InGroup<DiagGroup<"module-file-config-mismatch">>, DefaultError;
def err_module_map_not_found : Error<"module map file '%0' not found">,
  DefaultFatal;
def err_missing_module_name : Error<
  "no module name provided; specify one with -fmodule-name=">,
  DefaultFatal;
def err_missing_module : Error<
  "no module named '%0' declared in module map file '%1'">, DefaultFatal;
def err_no_submodule : Error<"no submodule named %0 in module '%1'">;
def err_no_submodule_suggest : Error<
  "no submodule named %0 in module '%1'; did you mean '%2'?">;
def warn_no_priv_submodule_use_toplevel : Warning<
  "no submodule named %0 in module '%1'; using top level '%2'">,
  InGroup<PrivateModule>;
def note_private_top_level_defined : Note<
  "module defined here">;
def warn_missing_submodule : Warning<"missing submodule '%0'">,
  InGroup<IncompleteUmbrella>;
def note_module_import_here : Note<"module imported here">;
def err_module_cannot_create_includes : Error<
  "cannot create includes file for module %0: %1">;
def warn_module_config_macro_undef : Warning<
  "%select{definition|#undef}0 of configuration macro '%1' has no effect on "
  "the import of '%2'; pass '%select{-D%1=...|-U%1}0' on the command line "
  "to configure the module">,
  InGroup<ConfigMacros>;
def note_module_def_undef_here : Note<
  "macro was %select{defined|#undef'd}0 here">;
def remark_module_build : Remark<"building module '%0' as '%1'">,
  InGroup<ModuleBuild>;
def remark_module_build_done : Remark<"finished building module '%0'">,
  InGroup<ModuleBuild>;
def err_modules_embed_file_not_found :
  Error<"file '%0' specified by '-fmodules-embed-file=' not found">,
  DefaultFatal;
def err_module_header_file_not_found :
  Error<"module header file '%0' not found">, DefaultFatal;
def err_module_header_file_invalid :
  Error<"unexpected module header file input '%0'">, DefaultFatal;

def err_test_module_file_extension_version : Error<
  "test module file extension '%0' has different version (%1.%2) than expected "
  "(%3.%4)">;

def err_missing_vfs_overlay_file : Error<
  "virtual filesystem overlay file '%0' not found">, DefaultFatal;
def err_invalid_vfs_overlay : Error<
  "invalid virtual filesystem overlay file '%0'">, DefaultFatal;

def warn_option_invalid_ocl_version : Warning<
  "OpenCL version %0 does not support the option '%1'">, InGroup<Deprecated>;

def warn_stdlibcxx_not_found : Warning<
  "include path for stdlibc++ headers not found; pass '-stdlib=libc++' on the "
  "command line to use the libc++ standard library instead">,
  InGroup<DiagGroup<"stdlibcxx-not-found">>;

def err_builtin_needs_feature : Error<"%0 needs target feature %1">;
def err_function_needs_feature : Error<
  "always_inline function %1 requires target feature '%2', but would "
  "be inlined into function %0 that is compiled without support for '%2'">;

def err_alias_to_undefined : Error<
  "%select{alias|ifunc}0 must point to a defined "
  "%select{variable or |}1function">;
def warn_alias_to_weak_alias : Warning<
  "%select{alias|ifunc}2 will always resolve to %0 even if weak definition of "
  "%1 is overridden">,
  InGroup<IgnoredAttributes>;
def err_duplicate_mangled_name : Error<
  "definition with same mangled name '%0' as another definition">;
def err_cyclic_alias : Error<
  "%select{alias|ifunc}0 definition is part of a cycle">;
def err_ifunc_resolver_return : Error<
  "ifunc resolver function must return a pointer">;

def warn_atomic_op_misaligned : Warning<
  "%select{large|misaligned}0 atomic operation may incur "
  "significant performance penalty">, InGroup<DiagGroup<"atomic-alignment">>;

def warn_alias_with_section : Warning<
  "%select{alias|ifunc}1 will not be in section '%0' but in the same section "
  "as the %select{aliasee|resolver}2">,
  InGroup<IgnoredAttributes>;

let CategoryName = "Instrumentation Issue" in {
def warn_profile_data_out_of_date : Warning<
  "profile data may be out of date: of %0 function%s0, %1 %plural{1:has|:have}1"
  " mismatched data that will be ignored">,
  InGroup<ProfileInstrOutOfDate>;
def warn_profile_data_missing : Warning<
  "profile data may be incomplete: of %0 function%s0, %1 %plural{1:has|:have}1"
  " no data">,
  InGroup<ProfileInstrMissing>,
  DefaultIgnore;
def warn_profile_data_unprofiled : Warning<
  "no profile data available for file \"%0\"">,
  InGroup<ProfileInstrUnprofiled>;

} // end of instrumentation issue category

}
