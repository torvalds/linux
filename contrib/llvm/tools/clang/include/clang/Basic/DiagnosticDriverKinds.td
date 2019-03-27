//==--- DiagnosticDriverKinds.td - libdriver diagnostics ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

let Component = "Driver" in {

def err_drv_no_such_file : Error<"no such file or directory: '%0'">;
def err_drv_unsupported_opt : Error<"unsupported option '%0'">;
def err_drv_unsupported_opt_with_suggestion
  : Error<"unsupported option '%0', did you mean '%1'?">;
def err_drv_unsupported_opt_for_target : Error<
  "unsupported option '%0' for target '%1'">;
def err_drv_unsupported_option_argument : Error<
  "unsupported argument '%1' to option '%0'">;
def err_drv_unknown_stdin_type : Error<
  "-E or -x required when input is from standard input">;
def err_drv_unknown_stdin_type_clang_cl : Error<
  "use /Tc or /Tp to set input type for standard input">;
def err_drv_unknown_language : Error<"language not recognized: '%0'">;
def err_drv_invalid_arch_name : Error<
  "invalid arch name '%0'">;
def err_drv_invalid_riscv_arch_name : Error<
  "invalid arch name '%0', %1">;
def err_drv_invalid_riscv_ext_arch_name : Error<
  "invalid arch name '%0', %1 '%2'">;
def err_drv_cuda_bad_gpu_arch : Error<"Unsupported CUDA gpu architecture: %0">;
def err_drv_no_cuda_installation : Error<
  "cannot find CUDA installation.  Provide its path via --cuda-path, or pass "
  "-nocudainc to build without CUDA includes.">;
def err_drv_no_cuda_libdevice : Error<
  "cannot find libdevice for %0. Provide path to different CUDA installation "
  "via --cuda-path, or pass -nocudalib to build without linking with libdevice.">;
def err_drv_cuda_version_unsupported : Error<
  "GPU arch %0 is supported by CUDA versions between %1 and %2 (inclusive), "
  "but installation at %3 is %4.  Use --cuda-path to specify a different CUDA "
  "install, pass a different GPU arch with --cuda-gpu-arch, or pass "
  "--no-cuda-version-check.">;
def err_drv_cuda_host_arch : Error<"unsupported architecture '%0' for host compilation.">;
def err_drv_mix_cuda_hip : Error<"Mixed Cuda and HIP compilation is not supported.">;
def err_drv_invalid_thread_model_for_target : Error<
  "invalid thread model '%0' in '%1' for this target">;
def err_drv_invalid_linker_name : Error<
  "invalid linker name in argument '%0'">;
def err_drv_invalid_pgo_instrumentor : Error<
  "invalid PGO instrumentor in argument '%0'">;
def err_drv_invalid_rtlib_name : Error<
  "invalid runtime library name in argument '%0'">;
def err_drv_unsupported_rtlib_for_platform : Error<
  "unsupported runtime library '%0' for platform '%1'">;
def err_drv_invalid_stdlib_name : Error<
  "invalid library name in argument '%0'">;
def err_drv_invalid_output_with_multiple_archs : Error<
  "cannot use '%0' output with multiple -arch options">;
def err_drv_no_input_files : Error<"no input files">;
def err_drv_use_of_Z_option : Error<
  "unsupported use of internal gcc -Z option '%0'">;
def err_drv_output_argument_with_multiple_files : Error<
  "cannot specify -o when generating multiple output files">;
def err_drv_out_file_argument_with_multiple_sources : Error<
  "cannot specify '%0%1' when compiling multiple source files">;
def err_no_external_assembler : Error<
  "there is no external assembler that can be used on this platform">;
def err_drv_unable_to_remove_file : Error<
  "unable to remove file: %0">;
def err_drv_command_failure : Error<
  "unable to execute command: %0">;
def err_drv_invalid_darwin_version : Error<
  "invalid Darwin version number: %0">;
def err_drv_missing_argument : Error<
  "argument to '%0' is missing (expected %1 value%s1)">;
def err_drv_invalid_Xarch_argument_with_args : Error<
  "invalid Xarch argument: '%0', options requiring arguments are unsupported">;
def err_drv_invalid_Xarch_argument_isdriver : Error<
  "invalid Xarch argument: '%0', cannot change driver behavior inside Xarch argument">;
def err_drv_Xopenmp_target_missing_triple : Error<
  "cannot deduce implicit triple value for -Xopenmp-target, specify triple using -Xopenmp-target=<triple>">;
def err_drv_invalid_Xopenmp_target_with_args : Error<
  "invalid -Xopenmp-target argument: '%0', options requiring arguments are unsupported">;
def err_drv_argument_only_allowed_with : Error<
  "invalid argument '%0' only allowed with '%1'">;
def err_drv_argument_not_allowed_with : Error<
  "invalid argument '%0' not allowed with '%1'">;
def err_drv_invalid_version_number : Error<
  "invalid version number in '%0'">;
def err_drv_no_linker_llvm_support : Error<
  "'%0': unable to pass LLVM bit-code files to linker">;
def err_drv_no_ast_support : Error<
  "'%0': unable to use AST files with this tool">;
def err_drv_no_module_support : Error<
  "'%0': unable to use module files with this tool">;
def err_drv_clang_unsupported : Error<
  "the clang compiler does not support '%0'">;
def err_drv_clang_unsupported_opt_cxx_darwin_i386 : Error<
  "the clang compiler does not support '%0' for C++ on Darwin/i386">;
def err_drv_clang_unsupported_opt_faltivec : Error<
  "the clang compiler does not support '%0', %1">;
def err_drv_command_failed : Error<
  "%0 command failed with exit code %1 (use -v to see invocation)">;
def err_drv_compilationdatabase : Error<
  "compilation database '%0' could not be opened: %1">;
def err_drv_command_signalled : Error<
  "%0 command failed due to signal (use -v to see invocation)">;
def err_drv_force_crash : Error<
  "failing because %select{environment variable 'FORCE_CLANG_DIAGNOSTICS_CRASH' is set|'-gen-reproducer' is used}0">;
def err_drv_invalid_mfloat_abi : Error<
  "invalid float ABI '%0'">;
def err_drv_invalid_mtp : Error<
  "invalid thread pointer reading mode '%0'">;
def err_drv_missing_arg_mtp : Error<
  "missing argument to '%0'">;
def err_drv_invalid_libcxx_deployment : Error<
  "invalid deployment target for -stdlib=libc++ (requires %0 or later)">;
def err_drv_invalid_argument_to_fdebug_prefix_map : Error<
  "invalid argument '%0' to -fdebug-prefix-map">;
def err_drv_malformed_sanitizer_blacklist : Error<
  "malformed sanitizer blacklist: '%0'">;
def err_drv_duplicate_config : Error<
  "no more than one option '--config' is allowed">;
def err_drv_config_file_not_exist : Error<
  "configuration file '%0' does not exist">;
def err_drv_config_file_not_found : Error<
  "configuration file '%0' cannot be found">;
def note_drv_config_file_searched_in : Note<
  "was searched for in the directory: %0">;
def err_drv_cannot_read_config_file : Error<
  "cannot read configuration file '%0'">;
def err_drv_nested_config_file: Error<
  "option '--config' is not allowed inside configuration file">;

def err_target_unsupported_arch
  : Error<"the target architecture '%0' is not supported by the target '%1'">;
def err_cpu_unsupported_isa
  : Error<"CPU '%0' does not support '%1' execution mode">;
def err_arch_unsupported_isa
  : Error<"Architecture '%0' does not support '%1' execution mode">;

def err_drv_I_dash_not_supported : Error<
  "'%0' not supported, please use -iquote instead">;
def err_drv_unknown_argument : Error<"unknown argument: '%0'">;
def err_drv_unknown_argument_with_suggestion
  : Error<"unknown argument '%0', did you mean '%1'?">;
def warn_drv_unknown_argument_clang_cl : Warning<
  "unknown argument ignored in clang-cl: '%0'">,
  InGroup<UnknownArgument>;
def warn_drv_unknown_argument_clang_cl_with_suggestion : Warning<
  "unknown argument ignored in clang-cl '%0' (did you mean '%1'?)">,
  InGroup<UnknownArgument>;

def warn_drv_ycyu_different_arg_clang_cl : Warning<
  "support for '/Yc' and '/Yu' with different filenames not implemented yet; flags ignored">,
  InGroup<ClangClPch>;
def warn_drv_ycyu_no_fi_arg_clang_cl : Warning<
  "support for '%0' without a corresponding /FI flag not implemented yet; flag ignored">,
  InGroup<ClangClPch>;
def warn_drv_yc_multiple_inputs_clang_cl : Warning<
  "support for '/Yc' with more than one source file not implemented yet; flag ignored">,
  InGroup<ClangClPch>;

def err_drv_dllexport_inlines_and_fallback : Error<
  "option '/Zc:dllexportInlines-' is ABI-changing and not compatible with '/fallback'">;

def err_drv_invalid_value : Error<"invalid value '%1' in '%0'">;
def err_drv_invalid_int_value : Error<"invalid integral value '%1' in '%0'">;
def err_drv_invalid_remap_file : Error<
    "invalid option '%0' not of the form <from-file>;<to-file>">;
def err_drv_invalid_gcc_output_type : Error<
    "invalid output type '%0' for use with gcc tool">;
def err_drv_cc_print_options_failure : Error<
    "unable to open CC_PRINT_OPTIONS file: %0">;
def err_drv_lto_without_lld : Error<"LTO requires -fuse-ld=lld">;
def err_drv_preamble_format : Error<
    "incorrect format for -preamble-bytes=N,END">;
def warn_invalid_ios_deployment_target : Warning<
  "invalid iOS deployment version '%0', iOS 10 is the maximum deployment "
  "target for 32-bit targets">, InGroup<InvalidIOSDeploymentTarget>,
  DefaultError;
def err_drv_conflicting_deployment_targets : Error<
  "conflicting deployment targets, both '%0' and '%1' are present in environment">;
def err_arc_unsupported_on_runtime : Error<
  "-fobjc-arc is not supported on platforms using the legacy runtime">;
def err_arc_unsupported_on_toolchain : Error< // feel free to generalize this
  "-fobjc-arc is not supported on versions of OS X prior to 10.6">;
def err_objc_weak_with_gc : Error<
  "-fobjc-weak is not supported in Objective-C garbage collection">;
def err_objc_weak_unsupported : Error<
  "-fobjc-weak is not supported on the current deployment target">;
def err_drv_mg_requires_m_or_mm : Error<
  "option '-MG' requires '-M' or '-MM'">;
def err_drv_unknown_objc_runtime : Error<
  "unknown or ill-formed Objective-C runtime '%0'">;
def err_drv_invalid_cf_runtime_abi
  : Error<"invalid CoreFoundation Runtime ABI '%0'; must be one of "
          "'objc', 'standalone', 'swift', 'swift-5.0', 'swift-4.2', 'swift-4.1'">;
def err_drv_gnustep_objc_runtime_incompatible_binary : Error<
  "GNUstep Objective-C runtime version %0 incompatible with target binary format">;
def err_drv_emit_llvm_link : Error<
   "-emit-llvm cannot be used when linking">;
def err_drv_optimization_remark_pattern : Error<
  "%0 in '%1'">;
def err_drv_no_neon_modifier : Error<"[no]neon is not accepted as modifier, please use [no]simd instead">;
def err_drv_invalid_omp_target : Error<"OpenMP target is invalid: '%0'">;
def err_drv_omp_host_ir_file_not_found : Error<
  "The provided host compiler IR file '%0' is required to generate code for OpenMP target regions but cannot be found.">;
def err_drv_omp_host_target_not_supported : Error<
  "The target '%0' is not a supported OpenMP host target.">;
def err_drv_expecting_fopenmp_with_fopenmp_targets : Error<
  "The option -fopenmp-targets must be used in conjunction with a -fopenmp option compatible with offloading, please use -fopenmp=libomp or -fopenmp=libiomp5.">;
def warn_drv_omp_offload_target_duplicate : Warning<
  "The OpenMP offloading target '%0' is similar to target '%1' already specified - will be ignored.">,
  InGroup<OpenMPTarget>;
def warn_drv_omp_offload_target_missingbcruntime : Warning<
  "No library '%0' found in the default clang lib directory or in LIBRARY_PATH. Expect degraded performance due to no inlining of runtime functions on target devices.">,
  InGroup<OpenMPTarget>;
def err_drv_unsupported_embed_bitcode
    : Error<"%0 is not supported with -fembed-bitcode">;
def err_drv_bitcode_unsupported_on_toolchain : Error<
  "-fembed-bitcode is not supported on versions of iOS prior to 6.0">;

def warn_O4_is_O3 : Warning<"-O4 is equivalent to -O3">, InGroup<Deprecated>;
def warn_drv_optimization_value : Warning<"optimization level '%0' is not supported; using '%1%2' instead">,
  InGroup<InvalidCommandLineArgument>;
def warn_ignored_gcc_optimization : Warning<"optimization flag '%0' is not supported">,
  InGroup<IgnoredOptimizationArgument>;
def warn_ignored_clang_option : Warning<"the flag '%0' has been deprecated and will be ignored">,
  InGroup<UnusedCommandLineArgument>;
def warn_drv_unsupported_opt_for_target : Warning<
  "optimization flag '%0' is not supported for target '%1'">,
  InGroup<IgnoredOptimizationArgument>;
def warn_drv_unsupported_debug_info_opt_for_target : Warning<
  "debug information option '%0' is not supported for target '%1'">,
  InGroup<UnsupportedTargetOpt>;
def warn_c_kext : Warning<
  "ignoring -fapple-kext which is valid for C++ and Objective-C++ only">;
def warn_drv_input_file_unused : Warning<
  "%0: '%1' input unused%select{ when '%3' is present|}2">,
  InGroup<UnusedCommandLineArgument>;
def warn_drv_input_file_unused_by_cpp : Warning<
  "%0: '%1' input unused in cpp mode">,
  InGroup<UnusedCommandLineArgument>;
def warn_drv_preprocessed_input_file_unused : Warning<
  "%0: previously preprocessed input%select{ unused when '%2' is present|}1">,
  InGroup<UnusedCommandLineArgument>;
def warn_drv_unused_argument : Warning<
  "argument unused during compilation: '%0'">,
  InGroup<UnusedCommandLineArgument>;
def warn_drv_empty_joined_argument : Warning<
  "joined argument expects additional value: '%0'">,
  InGroup<UnusedCommandLineArgument>;
def warn_drv_diagnostics_hotness_requires_pgo : Warning<
  "argument '%0' requires profile-guided optimization information">,
  InGroup<UnusedCommandLineArgument>;
def warn_drv_clang_unsupported : Warning<
  "the clang compiler does not support '%0'">;
def warn_drv_deprecated_arg : Warning<
  "argument '%0' is deprecated, use '%1' instead">, InGroup<Deprecated>;
def warn_drv_assuming_mfloat_abi_is : Warning<
  "unknown platform, assuming -mfloat-abi=%0">;
def warn_ignoring_ftabstop_value : Warning<
  "ignoring invalid -ftabstop value '%0', using default value %1">;
def warn_drv_overriding_flag_option : Warning<
  "overriding '%0' option with '%1'">,
  InGroup<DiagGroup<"overriding-t-option">>;
def warn_drv_treating_input_as_cxx : Warning<
  "treating '%0' input as '%1' when in C++ mode, this behavior is deprecated">,
  InGroup<Deprecated>;
def warn_drv_pch_not_first_include : Warning<
  "precompiled header '%0' was ignored because '%1' is not first '-include'">;
def warn_missing_sysroot : Warning<"no such sysroot directory: '%0'">,
  InGroup<DiagGroup<"missing-sysroot">>;
def warn_incompatible_sysroot : Warning<"using sysroot for '%0' but targeting '%1'">,
  InGroup<DiagGroup<"incompatible-sysroot">>;
def warn_debug_compression_unavailable : Warning<"cannot compress debug sections (zlib not installed)">,
  InGroup<DiagGroup<"debug-compression-unavailable">>;
def warn_drv_disabling_vptr_no_rtti_default : Warning<
  "implicitly disabling vptr sanitizer because rtti wasn't enabled">,
  InGroup<AutoDisableVptrSanitizer>;
def warn_drv_object_size_disabled_O0 : Warning<
  "the object size sanitizer has no effect at -O0, but is explicitly enabled: %0">,
  InGroup<InvalidCommandLineArgument>, DefaultWarnNoWerror;
def err_invalid_branch_protection: Error <
  "invalid branch protection option '%0' in '%1'">;

def note_drv_command_failed_diag_msg : Note<
  "diagnostic msg: %0">;
def note_drv_t_option_is_global : Note<
  "The last /TC or /TP option takes precedence over earlier instances">;
def note_drv_address_sanitizer_debug_runtime : Note<
  "AddressSanitizer doesn't support linking with debug runtime libraries yet">;
def note_drv_use_standard : Note<"use '%0'"
  "%select{| or '%3'|, '%3', or '%4'|, '%3', '%4', or '%5'}2 "
  "for '%1' standard">;

def err_analyzer_config_no_value : Error<
  "analyzer-config option '%0' has a key but no value">;
def err_analyzer_config_multiple_values : Error<
  "analyzer-config option '%0' should contain only one '='">;
def err_analyzer_config_invalid_input : Error<
  "invalid input for analyzer-config option '%0', that expects %1 value">;
def err_analyzer_config_unknown : Error<"unknown analyzer-config '%0'">;

def err_drv_invalid_hvx_length : Error<
  "-mhvx-length is not supported without a -mhvx/-mhvx= flag">;
def warn_drv_vectorize_needs_hvx : Warning<
  "auto-vectorization requires HVX, use -mhvx to enable it">,
  InGroup<OptionIgnored>;

def err_drv_module_header_wrong_kind : Error<
  "header file '%0' input type '%1' does not match type of prior input "
  "in module compilation; use '-x %2' to override">;
def err_drv_modules_validate_once_requires_timestamp : Error<
  "option '-fmodules-validate-once-per-build-session' requires "
  "'-fbuild-session-timestamp=<seconds since Epoch>' or '-fbuild-session-file=<file>'">;

def err_test_module_file_extension_format : Error<
  "-ftest-module-file-extension argument '%0' is not of the required form "
  "'blockname:major:minor:hashed:user info'">;

def warn_drv_invoking_fallback : Warning<"falling back to %0">,
  InGroup<Fallback>;

def warn_slash_u_filename : Warning<"'/U%0' treated as the '/U' option">,
  InGroup<DiagGroup<"slash-u-filename">>;
def note_use_dashdash : Note<"Use '--' to treat subsequent arguments as filenames">;

def err_drv_ropi_rwpi_incompatible_with_pic : Error<
  "embedded and GOT-based position independence are incompatible">;
def err_drv_ropi_incompatible_with_cxx : Error<
  "ROPI is not compatible with c++">;

def warn_target_unsupported_nan2008 : Warning<
  "ignoring '-mnan=2008' option because the '%0' architecture does not support it">,
  InGroup<UnsupportedNan>;
def warn_target_unsupported_nanlegacy : Warning<
  "ignoring '-mnan=legacy' option because the '%0' architecture does not support it">,
  InGroup<UnsupportedNan>;
def warn_target_unsupported_abslegacy : Warning<
  "ignoring '-mabs=legacy' option because the '%0' architecture does not support it">,
  InGroup<UnsupportedAbs>;
def warn_target_unsupported_abs2008 : Warning<
  "ignoring '-mabs=2008' option because the '%0' architecture does not support it">,
  InGroup<UnsupportedAbs>;
def warn_target_unsupported_compact_branches : Warning<
  "ignoring '-mcompact-branches=' option because the '%0' architecture does not"
  " support it">, InGroup<UnsupportedCB>;
def warn_drv_unsupported_gpopt : Warning<
  "ignoring '-mgpopt' option as it cannot be used with %select{|the implicit"
  " usage of }0-mabicalls">,
  InGroup<UnsupportedGPOpt>;
def warn_drv_unsupported_longcalls : Warning<
  "ignoring '-mlong-calls' option as it is not currently supported with "
  "%select{|the implicit usage of }0-mabicalls">,
  InGroup<OptionIgnored>;
def warn_drv_unsupported_pic_with_mabicalls : Warning<
  "ignoring '%0' option as it cannot be used with "
  "%select{implicit usage of|}1 -mabicalls and the N64 ABI">,
  InGroup<OptionIgnored>;
def err_drv_unsupported_noabicalls_pic : Error<
  "position-independent code requires '-mabicalls'">;
def err_drv_unsupported_indirect_jump_opt : Error<
  "'-mindirect-jump=%0' is unsupported with the '%1' architecture">;
def err_drv_unknown_indirect_jump_opt : Error<
  "unknown '-mindirect-jump=' option '%0'">;

def warn_drv_unable_to_find_directory_expected : Warning<
  "unable to find %0 directory, expected to be in '%1'">,
  InGroup<InvalidOrNonExistentDirectory>, DefaultIgnore;

def warn_drv_ps4_force_pic : Warning<
  "option '%0' was ignored by the PS4 toolchain, using '-fPIC'">,
  InGroup<OptionIgnored>;

def warn_drv_ps4_sdk_dir : Warning<
  "environment variable SCE_ORBIS_SDK_DIR is set, but points to invalid or nonexistent directory '%0'">,
  InGroup<InvalidOrNonExistentDirectory>;

def err_drv_unsupported_linker : Error<"unsupported value '%0' for -linker option">;
def err_drv_defsym_invalid_format : Error<"defsym must be of the form: sym=value: %0">;
def err_drv_defsym_invalid_symval : Error<"Value is not an integer: %0">;
def warn_drv_msvc_not_found : Warning<
  "unable to find a Visual Studio installation; "
  "try running Clang from a developer command prompt">,
  InGroup<DiagGroup<"msvc-not-found">>;

def warn_drv_fine_grained_bitfield_accesses_ignored : Warning<
  "option '-ffine-grained-bitfield-accesses' cannot be enabled together with a sanitizer; flag ignored">,
  InGroup<OptionIgnored>;

def note_drv_verify_prefix_spelling : Note<
  "-verify prefixes must start with a letter and contain only alphanumeric"
  " characters, hyphens, and underscores">;

def warn_drv_experimental_isel_incomplete : Warning<
  "-fexperimental-isel support for the '%0' architecture is incomplete">,
  InGroup<ExperimentalISel>;

def warn_drv_experimental_isel_incomplete_opt : Warning<
  "-fexperimental-isel support is incomplete for this architecture at the current optimization level">,
  InGroup<ExperimentalISel>;

def warn_drv_moutline_unsupported_opt : Warning<
  "The '%0' architecture does not support -moutline; flag ignored">,
  InGroup<OptionIgnored>;

def warn_drv_darwin_sdk_invalid_settings : Warning<
  "SDK settings were ignored as 'SDKSettings.json' could not be parsed">,
  InGroup<DiagGroup<"darwin-sdk-settings">>;

def err_drv_trivial_auto_var_init_zero_disabled : Error<
  "-ftrivial-auto-var-init=zero hasn't been enabled. Enable it at your own peril for benchmarking purpose only with "
  "-enable-trivial-auto-var-init-zero-knowing-it-will-be-removed-from-clang">;

def warn_drv_msp430_hwmult_unsupported : Warning<"the given MCU does not "
  "support hardware multiply, but -mhwmult is set to %0.">,
   InGroup<InvalidCommandLineArgument>;
def warn_drv_msp430_hwmult_mismatch : Warning<"the given MCU supports %0 "
  "hardware multiply, but -mhwmult is set to %1.">,
   InGroup<InvalidCommandLineArgument>;
def warn_drv_msp430_hwmult_no_device : Warning<"no MCU device specified, but "
  "'-mhwmult' is set to 'auto', assuming no hardware multiply. Use -mmcu to "
  "specify a MSP430 device, or -mhwmult to set hardware multiply type "
  "explicitly.">, InGroup<InvalidCommandLineArgument>;

}
