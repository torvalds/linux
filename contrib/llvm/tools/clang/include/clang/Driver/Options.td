//===--- Options.td - Options for clang -----------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the options accepted by clang.
//
//===----------------------------------------------------------------------===//

// Include the common option parsing interfaces.
include "llvm/Option/OptParser.td"

/////////
// Flags

// DriverOption - The option is a "driver" option, and should not be forwarded
// to other tools.
def DriverOption : OptionFlag;

// LinkerInput - The option is a linker input.
def LinkerInput : OptionFlag;

// NoArgumentUnused - Don't report argument unused warnings for this option; this
// is useful for options like -static or -dynamic which a user may always end up
// passing, even if the platform defaults to (or only supports) that option.
def NoArgumentUnused : OptionFlag;

// Unsupported - The option is unsupported, and the driver will reject command
// lines that use it.
def Unsupported : OptionFlag;

// Ignored - The option is unsupported, and the driver will silently ignore it.
def Ignored : OptionFlag;

// CoreOption - This is considered a "core" Clang option, available in both
// clang and clang-cl modes.
def CoreOption : OptionFlag;

// CLOption - This is a cl.exe compatibility option. Options with this flag
// are made available when the driver is running in CL compatibility mode.
def CLOption : OptionFlag;

// CC1Option - This option should be accepted by clang -cc1.
def CC1Option : OptionFlag;

// CC1AsOption - This option should be accepted by clang -cc1as.
def CC1AsOption : OptionFlag;

// NoDriverOption - This option should not be accepted by the driver.
def NoDriverOption : OptionFlag;

// A short name to show in documentation. The name will be interpreted as rST.
class DocName<string name> { string DocName = name; }

// A brief description to show in documentation, interpreted as rST.
class DocBrief<code descr> { code DocBrief = descr; }

// Indicates that this group should be flattened into its parent when generating
// documentation.
class DocFlatten { bit DocFlatten = 1; }

// Indicates that this warning is ignored, but accepted with a warning for
// GCC compatibility.
class IgnoredGCCCompat : Flags<[HelpHidden]> {}

/////////
// Groups

def Action_Group : OptionGroup<"<action group>">, DocName<"Actions">,
                   DocBrief<[{The action to perform on the input.}]>;

// Meta-group for options which are only used for compilation,
// and not linking etc.
def CompileOnly_Group : OptionGroup<"<CompileOnly group>">,
                        DocName<"Compilation flags">, DocBrief<[{
Flags controlling the behavior of Clang during compilation. These flags have
no effect during actions that do not perform compilation.}]>;

def Preprocessor_Group : OptionGroup<"<Preprocessor group>">,
                         Group<CompileOnly_Group>,
                         DocName<"Preprocessor flags">, DocBrief<[{
Flags controlling the behavior of the Clang preprocessor.}]>;

def IncludePath_Group : OptionGroup<"<I/i group>">, Group<Preprocessor_Group>,
                        DocName<"Include path management">,
                        DocBrief<[{
Flags controlling how ``#include``\s are resolved to files.}]>;

def I_Group : OptionGroup<"<I group>">, Group<IncludePath_Group>, DocFlatten;
def i_Group : OptionGroup<"<i group>">, Group<IncludePath_Group>, DocFlatten;
def clang_i_Group : OptionGroup<"<clang i group>">, Group<i_Group>, DocFlatten;

def M_Group : OptionGroup<"<M group>">, Group<Preprocessor_Group>,
              DocName<"Dependency file generation">, DocBrief<[{
Flags controlling generation of a dependency file for ``make``-like build
systems.}]>;

def d_Group : OptionGroup<"<d group>">, Group<Preprocessor_Group>,
              DocName<"Dumping preprocessor state">, DocBrief<[{
Flags allowing the state of the preprocessor to be dumped in various ways.}]>;

def Diag_Group : OptionGroup<"<W/R group>">, Group<CompileOnly_Group>,
                 DocName<"Diagnostic flags">, DocBrief<[{
Flags controlling which warnings, errors, and remarks Clang will generate.
See the :doc:`full list of warning and remark flags <DiagnosticsReference>`.}]>;

def R_Group : OptionGroup<"<R group>">, Group<Diag_Group>, DocFlatten;
def R_value_Group : OptionGroup<"<R (with value) group>">, Group<R_Group>,
                    DocFlatten;
def W_Group : OptionGroup<"<W group>">, Group<Diag_Group>, DocFlatten;
def W_value_Group : OptionGroup<"<W (with value) group>">, Group<W_Group>,
                    DocFlatten;

def f_Group : OptionGroup<"<f group>">, Group<CompileOnly_Group>,
              DocName<"Target-independent compilation options">;

def f_clang_Group : OptionGroup<"<f (clang-only) group>">,
                    Group<CompileOnly_Group>, DocFlatten;
def pedantic_Group : OptionGroup<"<pedantic group>">, Group<f_Group>,
                     DocFlatten;
def opencl_Group : OptionGroup<"<opencl group>">, Group<f_Group>,
                   DocName<"OpenCL flags">;

def m_Group : OptionGroup<"<m group>">, Group<CompileOnly_Group>,
              DocName<"Target-dependent compilation options">;

// Feature groups - these take command line options that correspond directly to
// target specific features and can be translated directly from command line
// options.
def m_aarch64_Features_Group : OptionGroup<"<aarch64 features group>">,
                               Group<m_Group>, DocName<"AARCH64">;
def m_amdgpu_Features_Group : OptionGroup<"<amdgpu features group>">,
                              Group<m_Group>, DocName<"AMDGPU">;
def m_arm_Features_Group : OptionGroup<"<arm features group>">,
                           Group<m_Group>, DocName<"ARM">;
def m_hexagon_Features_Group : OptionGroup<"<hexagon features group>">,
                               Group<m_Group>, DocName<"Hexagon">;
// The features added by this group will not be added to target features.
// These are explicitly handled.
def m_hexagon_Features_HVX_Group : OptionGroup<"<hexagon features group>">,
                                   Group<m_Group>, DocName<"Hexagon">;
def m_mips_Features_Group : OptionGroup<"<mips features group>">,
                            Group<m_Group>, DocName<"MIPS">;
def m_ppc_Features_Group : OptionGroup<"<ppc features group>">,
                           Group<m_Group>, DocName<"PowerPC">;
def m_wasm_Features_Group : OptionGroup<"<wasm features group>">,
                            Group<m_Group>, DocName<"WebAssembly">;
def m_x86_Features_Group : OptionGroup<"<x86 features group>">,
                           Group<m_Group>, Flags<[CoreOption]>, DocName<"X86">;
def m_riscv_Features_Group : OptionGroup<"<riscv features group>">,
                             Group<m_Group>, DocName<"RISCV">;

def m_libc_Group : OptionGroup<"<m libc group>">, Group<m_mips_Features_Group>,
                   Flags<[HelpHidden]>;

def O_Group : OptionGroup<"<O group>">, Group<CompileOnly_Group>,
              DocName<"Optimization level">, DocBrief<[{
Flags controlling how much optimization should be performed.}]>;

def DebugInfo_Group : OptionGroup<"<g group>">, Group<CompileOnly_Group>,
                      DocName<"Debug information generation">, DocBrief<[{
Flags controlling how much and what kind of debug information should be
generated.}]>;

def g_Group : OptionGroup<"<g group>">, Group<DebugInfo_Group>,
              DocName<"Kind and level of debug information">;
def gN_Group : OptionGroup<"<gN group>">, Group<g_Group>,
               DocName<"Debug level">;
def ggdbN_Group : OptionGroup<"<ggdbN group>">, Group<gN_Group>, DocFlatten;
def gTune_Group : OptionGroup<"<gTune group>">, Group<g_Group>,
                  DocName<"Debugger to tune debug information for">;
def g_flags_Group : OptionGroup<"<g flags group>">, Group<DebugInfo_Group>,
                    DocName<"Debug information flags">;

def StaticAnalyzer_Group : OptionGroup<"<Static analyzer group>">,
                           DocName<"Static analyzer flags">, DocBrief<[{
Flags controlling the behavior of the Clang Static Analyzer.}]>;

// gfortran options that we recognize in the driver and pass along when
// invoking GCC to compile Fortran code.
def gfortran_Group : OptionGroup<"<gfortran group>">,
                     DocName<"Fortran compilation flags">, DocBrief<[{
Flags that will be passed onto the ``gfortran`` compiler when Clang is given
a Fortran input.}]>;

def Link_Group : OptionGroup<"<T/e/s/t/u group>">, DocName<"Linker flags">,
                 DocBrief<[{Flags that are passed on to the linker}]>;
def T_Group : OptionGroup<"<T group>">, Group<Link_Group>, DocFlatten;
def u_Group : OptionGroup<"<u group>">, Group<Link_Group>, DocFlatten;

def reserved_lib_Group : OptionGroup<"<reserved libs group>">,
                         Flags<[Unsupported]>;

// Temporary groups for clang options which we know we don't support,
// but don't want to verbosely warn the user about.
def clang_ignored_f_Group : OptionGroup<"<clang ignored f group>">,
  Group<f_Group>, Flags<[Ignored]>;
def clang_ignored_m_Group : OptionGroup<"<clang ignored m group>">,
  Group<m_Group>, Flags<[Ignored]>;

// Group for clang options in the process of deprecation.
// Please include the version that deprecated the flag as comment to allow
// easier garbage collection.
def clang_ignored_legacy_options_Group : OptionGroup<"<clang legacy flags>">,
  Group<f_Group>, Flags<[Ignored]>;

// Retired with clang-5.0
def : Flag<["-"], "fslp-vectorize-aggressive">, Group<clang_ignored_legacy_options_Group>;
def : Flag<["-"], "fno-slp-vectorize-aggressive">, Group<clang_ignored_legacy_options_Group>;

// Group that ignores all gcc optimizations that won't be implemented
def clang_ignored_gcc_optimization_f_Group : OptionGroup<
  "<clang_ignored_gcc_optimization_f_Group>">, Group<f_Group>, Flags<[Ignored]>;

/////////
// Options

// The internal option ID must be a valid C++ identifier and results in a
// clang::driver::options::OPT_XX enum constant for XX.
//
// We want to unambiguously be able to refer to options from the driver source
// code, for this reason the option name is mangled into an ID. This mangling
// isn't guaranteed to have an inverse, but for practical purposes it does.
//
// The mangling scheme is to ignore the leading '-', and perform the following
// substitutions:
//   _ => __
//   - => _
//   / => _SLASH
//   # => _HASH
//   ? => _QUESTION
//   , => _COMMA
//   = => _EQ
//   C++ => CXX
//   . => _

// Developer Driver Options

def internal_Group : OptionGroup<"<clang internal options>">, Flags<[HelpHidden]>;
def internal_driver_Group : OptionGroup<"<clang driver internal options>">,
  Group<internal_Group>, HelpText<"DRIVER OPTIONS">;
def internal_debug_Group :
  OptionGroup<"<clang debug/development internal options>">,
  Group<internal_Group>, HelpText<"DEBUG/DEVELOPMENT OPTIONS">;

class InternalDriverOpt : Group<internal_driver_Group>,
  Flags<[DriverOption, HelpHidden]>;
def driver_mode : Joined<["--"], "driver-mode=">, Group<internal_driver_Group>,
  Flags<[CoreOption, DriverOption, HelpHidden]>,
  HelpText<"Set the driver mode to either 'gcc', 'g++', 'cpp', or 'cl'">;
def rsp_quoting : Joined<["--"], "rsp-quoting=">, Group<internal_driver_Group>,
  Flags<[CoreOption, DriverOption, HelpHidden]>,
  HelpText<"Set the rsp quoting to either 'posix', or 'windows'">;
def ccc_gcc_name : Separate<["-"], "ccc-gcc-name">, InternalDriverOpt,
  HelpText<"Name for native GCC compiler">,
  MetaVarName<"<gcc-path>">;

class InternalDebugOpt : Group<internal_debug_Group>,
  Flags<[DriverOption, HelpHidden, CoreOption]>;
def ccc_install_dir : Separate<["-"], "ccc-install-dir">, InternalDebugOpt,
  HelpText<"Simulate installation in the given directory">;
def ccc_print_phases : Flag<["-"], "ccc-print-phases">, InternalDebugOpt,
  HelpText<"Dump list of actions to perform">;
def ccc_print_bindings : Flag<["-"], "ccc-print-bindings">, InternalDebugOpt,
  HelpText<"Show bindings of tools to actions">;

def ccc_arcmt_check : Flag<["-"], "ccc-arcmt-check">, InternalDriverOpt,
  HelpText<"Check for ARC migration issues that need manual handling">;
def ccc_arcmt_modify : Flag<["-"], "ccc-arcmt-modify">, InternalDriverOpt,
  HelpText<"Apply modifications to files to conform to ARC">;
def ccc_arcmt_migrate : Separate<["-"], "ccc-arcmt-migrate">, InternalDriverOpt,
  HelpText<"Apply modifications and produces temporary files that conform to ARC">;
def arcmt_migrate_report_output : Separate<["-"], "arcmt-migrate-report-output">,
  HelpText<"Output path for the plist report">,  Flags<[CC1Option]>;
def arcmt_migrate_emit_arc_errors : Flag<["-"], "arcmt-migrate-emit-errors">,
  HelpText<"Emit ARC errors even if the migrator can fix them">,
  Flags<[CC1Option]>;
def gen_reproducer: Flag<["-"], "gen-reproducer">, InternalDebugOpt,
  HelpText<"Auto-generates preprocessed source files and a reproduction script">;

def _migrate : Flag<["--"], "migrate">, Flags<[DriverOption]>,
  HelpText<"Run the migrator">;
def ccc_objcmt_migrate : Separate<["-"], "ccc-objcmt-migrate">,
  InternalDriverOpt,
  HelpText<"Apply modifications and produces temporary files to migrate to "
   "modern ObjC syntax">;
def objcmt_migrate_literals : Flag<["-"], "objcmt-migrate-literals">, Flags<[CC1Option]>,
  HelpText<"Enable migration to modern ObjC literals">;
def objcmt_migrate_subscripting : Flag<["-"], "objcmt-migrate-subscripting">, Flags<[CC1Option]>,
  HelpText<"Enable migration to modern ObjC subscripting">;
def objcmt_migrate_property : Flag<["-"], "objcmt-migrate-property">, Flags<[CC1Option]>,
  HelpText<"Enable migration to modern ObjC property">;
def objcmt_migrate_all : Flag<["-"], "objcmt-migrate-all">, Flags<[CC1Option]>,
  HelpText<"Enable migration to modern ObjC">;
def objcmt_migrate_readonly_property : Flag<["-"], "objcmt-migrate-readonly-property">, Flags<[CC1Option]>,
  HelpText<"Enable migration to modern ObjC readonly property">;
def objcmt_migrate_readwrite_property : Flag<["-"], "objcmt-migrate-readwrite-property">, Flags<[CC1Option]>,
  HelpText<"Enable migration to modern ObjC readwrite property">;
def objcmt_migrate_property_dot_syntax : Flag<["-"], "objcmt-migrate-property-dot-syntax">, Flags<[CC1Option]>,
  HelpText<"Enable migration of setter/getter messages to property-dot syntax">;
def objcmt_migrate_annotation : Flag<["-"], "objcmt-migrate-annotation">, Flags<[CC1Option]>,
  HelpText<"Enable migration to property and method annotations">;
def objcmt_migrate_instancetype : Flag<["-"], "objcmt-migrate-instancetype">, Flags<[CC1Option]>,
  HelpText<"Enable migration to infer instancetype for method result type">;
def objcmt_migrate_nsmacros : Flag<["-"], "objcmt-migrate-ns-macros">, Flags<[CC1Option]>,
  HelpText<"Enable migration to NS_ENUM/NS_OPTIONS macros">;
def objcmt_migrate_protocol_conformance : Flag<["-"], "objcmt-migrate-protocol-conformance">, Flags<[CC1Option]>,
  HelpText<"Enable migration to add protocol conformance on classes">;
def objcmt_atomic_property : Flag<["-"], "objcmt-atomic-property">, Flags<[CC1Option]>,
  HelpText<"Make migration to 'atomic' properties">;
def objcmt_returns_innerpointer_property : Flag<["-"], "objcmt-returns-innerpointer-property">, Flags<[CC1Option]>,
  HelpText<"Enable migration to annotate property with NS_RETURNS_INNER_POINTER">;
def objcmt_ns_nonatomic_iosonly: Flag<["-"], "objcmt-ns-nonatomic-iosonly">, Flags<[CC1Option]>,
  HelpText<"Enable migration to use NS_NONATOMIC_IOSONLY macro for setting property's 'atomic' attribute">;
def objcmt_migrate_designated_init : Flag<["-"], "objcmt-migrate-designated-init">, Flags<[CC1Option]>,
  HelpText<"Enable migration to infer NS_DESIGNATED_INITIALIZER for initializer methods">;
def objcmt_whitelist_dir_path: Joined<["-"], "objcmt-whitelist-dir-path=">, Flags<[CC1Option]>,
  HelpText<"Only modify files with a filename contained in the provided directory path">;
// The misspelt "white-list" [sic] alias is due for removal.
def : Joined<["-"], "objcmt-white-list-dir-path=">, Flags<[CC1Option]>,
    Alias<objcmt_whitelist_dir_path>;

// Make sure all other -ccc- options are rejected.
def ccc_ : Joined<["-"], "ccc-">, Group<internal_Group>, Flags<[Unsupported]>;

// Standard Options

def _HASH_HASH_HASH : Flag<["-"], "###">, Flags<[DriverOption, CoreOption]>,
    HelpText<"Print (but do not run) the commands to run for this compilation">;
def _DASH_DASH : Option<["--"], "", KIND_REMAINING_ARGS>,
    Flags<[DriverOption, CoreOption]>;
def A : JoinedOrSeparate<["-"], "A">, Flags<[RenderJoined]>, Group<gfortran_Group>;
def B : JoinedOrSeparate<["-"], "B">, MetaVarName<"<dir>">,
    HelpText<"Add <dir> to search path for binaries and object files used implicitly">;
def CC : Flag<["-"], "CC">, Flags<[CC1Option]>, Group<Preprocessor_Group>,
    HelpText<"Include comments from within macros in preprocessed output">;
def C : Flag<["-"], "C">, Flags<[CC1Option]>, Group<Preprocessor_Group>,
    HelpText<"Include comments in preprocessed output">;
def D : JoinedOrSeparate<["-"], "D">, Group<Preprocessor_Group>,
    Flags<[CC1Option]>, MetaVarName<"<macro>=<value>">,
    HelpText<"Define <macro> to <value> (or 1 if <value> omitted)">;
def E : Flag<["-"], "E">, Flags<[DriverOption,CC1Option]>, Group<Action_Group>,
    HelpText<"Only run the preprocessor">;
def F : JoinedOrSeparate<["-"], "F">, Flags<[RenderJoined,CC1Option]>,
    HelpText<"Add directory to framework include search path">;
def G : JoinedOrSeparate<["-"], "G">, Flags<[DriverOption]>, Group<m_Group>,
    MetaVarName<"<size>">, HelpText<"Put objects of at most <size> bytes "
    "into small data section (MIPS / Hexagon)">;
def G_EQ : Joined<["-"], "G=">, Flags<[DriverOption]>, Group<m_Group>, Alias<G>;
def H : Flag<["-"], "H">, Flags<[CC1Option]>, Group<Preprocessor_Group>,
    HelpText<"Show header includes and nesting depth">;
def I_ : Flag<["-"], "I-">, Group<I_Group>,
    HelpText<"Restrict all prior -I flags to double-quoted inclusion and "
             "remove current directory from include path">;
def I : JoinedOrSeparate<["-"], "I">, Group<I_Group>,
    Flags<[CC1Option,CC1AsOption]>, MetaVarName<"<dir>">,
    HelpText<"Add directory to include search path">;
def L : JoinedOrSeparate<["-"], "L">, Flags<[RenderJoined]>, Group<Link_Group>,
    MetaVarName<"<dir>">, HelpText<"Add directory to library search path">;
def MD : Flag<["-"], "MD">, Group<M_Group>,
    HelpText<"Write a depfile containing user and system headers">;
def MMD : Flag<["-"], "MMD">, Group<M_Group>,
    HelpText<"Write a depfile containing user headers">;
def M : Flag<["-"], "M">, Group<M_Group>,
    HelpText<"Like -MD, but also implies -E and writes to stdout by default">;
def MM : Flag<["-"], "MM">, Group<M_Group>,
    HelpText<"Like -MMD, but also implies -E and writes to stdout by default">;
def MF : JoinedOrSeparate<["-"], "MF">, Group<M_Group>,
    HelpText<"Write depfile output from -MMD, -MD, -MM, or -M to <file>">,
    MetaVarName<"<file>">;
def MG : Flag<["-"], "MG">, Group<M_Group>, Flags<[CC1Option]>,
    HelpText<"Add missing headers to depfile">;
def MJ : JoinedOrSeparate<["-"], "MJ">, Group<M_Group>,
    HelpText<"Write a compilation database entry per input">;
def MP : Flag<["-"], "MP">, Group<M_Group>, Flags<[CC1Option]>,
    HelpText<"Create phony target for each dependency (other than main file)">;
def MQ : JoinedOrSeparate<["-"], "MQ">, Group<M_Group>, Flags<[CC1Option]>,
    HelpText<"Specify name of main file output to quote in depfile">;
def MT : JoinedOrSeparate<["-"], "MT">, Group<M_Group>, Flags<[CC1Option]>,
    HelpText<"Specify name of main file output in depfile">;
def MV : Flag<["-"], "MV">, Group<M_Group>, Flags<[CC1Option]>,
    HelpText<"Use NMake/Jom format for the depfile">;
def Mach : Flag<["-"], "Mach">, Group<Link_Group>;
def O0 : Flag<["-"], "O0">, Group<O_Group>, Flags<[CC1Option, HelpHidden]>;
def O4 : Flag<["-"], "O4">, Group<O_Group>, Flags<[CC1Option, HelpHidden]>;
def ObjCXX : Flag<["-"], "ObjC++">, Flags<[DriverOption]>,
  HelpText<"Treat source input files as Objective-C++ inputs">;
def ObjC : Flag<["-"], "ObjC">, Flags<[DriverOption]>,
  HelpText<"Treat source input files as Objective-C inputs">;
def O : Joined<["-"], "O">, Group<O_Group>, Flags<[CC1Option]>;
def O_flag : Flag<["-"], "O">, Flags<[CC1Option]>, Alias<O>, AliasArgs<["2"]>;
def Ofast : Joined<["-"], "Ofast">, Group<O_Group>, Flags<[CC1Option]>;
def P : Flag<["-"], "P">, Flags<[CC1Option]>, Group<Preprocessor_Group>,
  HelpText<"Disable linemarker output in -E mode">;
def Qy : Flag<["-"], "Qy">, Flags<[CC1Option]>,
  HelpText<"Emit metadata containing compiler name and version">;
def Qn : Flag<["-"], "Qn">, Flags<[CC1Option]>,
  HelpText<"Do not emit metadata containing compiler name and version">;
def : Flag<["-"], "fident">, Group<f_Group>, Alias<Qy>, Flags<[CC1Option]>;
def : Flag<["-"], "fno-ident">, Group<f_Group>, Alias<Qn>, Flags<[CC1Option]>;
def Qunused_arguments : Flag<["-"], "Qunused-arguments">, Flags<[DriverOption, CoreOption]>,
  HelpText<"Don't emit warning for unused driver arguments">;
def Q : Flag<["-"], "Q">, IgnoredGCCCompat;
def Rpass_EQ : Joined<["-"], "Rpass=">, Group<R_value_Group>, Flags<[CC1Option]>,
  HelpText<"Report transformations performed by optimization passes whose "
           "name matches the given POSIX regular expression">;
def Rpass_missed_EQ : Joined<["-"], "Rpass-missed=">, Group<R_value_Group>,
  Flags<[CC1Option]>,
  HelpText<"Report missed transformations by optimization passes whose "
           "name matches the given POSIX regular expression">;
def Rpass_analysis_EQ : Joined<["-"], "Rpass-analysis=">, Group<R_value_Group>,
  Flags<[CC1Option]>,
  HelpText<"Report transformation analysis from optimization passes whose "
           "name matches the given POSIX regular expression">;
def R_Joined : Joined<["-"], "R">, Group<R_Group>, Flags<[CC1Option, CoreOption]>,
  MetaVarName<"<remark>">, HelpText<"Enable the specified remark">;
def S : Flag<["-"], "S">, Flags<[DriverOption,CC1Option]>, Group<Action_Group>,
  HelpText<"Only run preprocess and compilation steps">;
def Tbss : JoinedOrSeparate<["-"], "Tbss">, Group<T_Group>,
  MetaVarName<"<addr>">, HelpText<"Set starting address of BSS to <addr>">;
def Tdata : JoinedOrSeparate<["-"], "Tdata">, Group<T_Group>,
  MetaVarName<"<addr>">, HelpText<"Set starting address of DATA to <addr>">;
def Ttext : JoinedOrSeparate<["-"], "Ttext">, Group<T_Group>,
  MetaVarName<"<addr>">, HelpText<"Set starting address of TEXT to <addr>">;
def T : JoinedOrSeparate<["-"], "T">, Group<T_Group>,
  MetaVarName<"<script>">, HelpText<"Specify <script> as linker script">;
def U : JoinedOrSeparate<["-"], "U">, Group<Preprocessor_Group>,
  Flags<[CC1Option]>, MetaVarName<"<macro>">, HelpText<"Undefine macro <macro>">;
def V : JoinedOrSeparate<["-"], "V">, Flags<[DriverOption, Unsupported]>;
def Wa_COMMA : CommaJoined<["-"], "Wa,">,
  HelpText<"Pass the comma separated arguments in <arg> to the assembler">,
  MetaVarName<"<arg>">;
def Wall : Flag<["-"], "Wall">, Group<W_Group>, Flags<[CC1Option, HelpHidden]>;
def WCL4 : Flag<["-"], "WCL4">, Group<W_Group>, Flags<[CC1Option, HelpHidden]>;
def Wdeprecated : Flag<["-"], "Wdeprecated">, Group<W_Group>, Flags<[CC1Option]>,
  HelpText<"Enable warnings for deprecated constructs and define __DEPRECATED">;
def Wno_deprecated : Flag<["-"], "Wno-deprecated">, Group<W_Group>, Flags<[CC1Option]>;
def Wl_COMMA : CommaJoined<["-"], "Wl,">, Flags<[LinkerInput, RenderAsInput]>,
  HelpText<"Pass the comma separated arguments in <arg> to the linker">,
  MetaVarName<"<arg>">, Group<Link_Group>;
// FIXME: This is broken; these should not be Joined arguments.
def Wno_nonportable_cfstrings : Joined<["-"], "Wno-nonportable-cfstrings">, Group<W_Group>,
  Flags<[CC1Option]>;
def Wnonportable_cfstrings : Joined<["-"], "Wnonportable-cfstrings">, Group<W_Group>,
  Flags<[CC1Option]>;
def Wp_COMMA : CommaJoined<["-"], "Wp,">,
  HelpText<"Pass the comma separated arguments in <arg> to the preprocessor">,
  MetaVarName<"<arg>">, Group<Preprocessor_Group>;
def Wwrite_strings : Flag<["-"], "Wwrite-strings">, Group<W_Group>, Flags<[CC1Option, HelpHidden]>;
def Wno_write_strings : Flag<["-"], "Wno-write-strings">, Group<W_Group>, Flags<[CC1Option, HelpHidden]>;
def W_Joined : Joined<["-"], "W">, Group<W_Group>, Flags<[CC1Option, CoreOption]>,
  MetaVarName<"<warning>">, HelpText<"Enable the specified warning">;
def Xanalyzer : Separate<["-"], "Xanalyzer">,
  HelpText<"Pass <arg> to the static analyzer">, MetaVarName<"<arg>">,
  Group<StaticAnalyzer_Group>;
def Xarch__ : JoinedAndSeparate<["-"], "Xarch_">, Flags<[DriverOption]>;
def Xassembler : Separate<["-"], "Xassembler">,
  HelpText<"Pass <arg> to the assembler">, MetaVarName<"<arg>">,
  Group<CompileOnly_Group>;
def Xclang : Separate<["-"], "Xclang">,
  HelpText<"Pass <arg> to the clang compiler">, MetaVarName<"<arg>">,
  Flags<[DriverOption, CoreOption]>, Group<CompileOnly_Group>;
def Xcuda_fatbinary : Separate<["-"], "Xcuda-fatbinary">,
  HelpText<"Pass <arg> to fatbinary invocation">, MetaVarName<"<arg>">;
def Xcuda_ptxas : Separate<["-"], "Xcuda-ptxas">,
  HelpText<"Pass <arg> to the ptxas assembler">, MetaVarName<"<arg>">;
def Xopenmp_target : Separate<["-"], "Xopenmp-target">,
  HelpText<"Pass <arg> to the target offloading toolchain.">, MetaVarName<"<arg>">;
def Xopenmp_target_EQ : JoinedAndSeparate<["-"], "Xopenmp-target=">,
  HelpText<"Pass <arg> to the target offloading toolchain identified by <triple>.">,
  MetaVarName<"<triple> <arg>">;
def z : Separate<["-"], "z">, Flags<[LinkerInput, RenderAsInput]>,
  HelpText<"Pass -z <arg> to the linker">, MetaVarName<"<arg>">,
  Group<Link_Group>;
def Xlinker : Separate<["-"], "Xlinker">, Flags<[LinkerInput, RenderAsInput]>,
  HelpText<"Pass <arg> to the linker">, MetaVarName<"<arg>">,
  Group<Link_Group>;
def Xpreprocessor : Separate<["-"], "Xpreprocessor">, Group<Preprocessor_Group>,
  HelpText<"Pass <arg> to the preprocessor">, MetaVarName<"<arg>">;
def X_Flag : Flag<["-"], "X">, Group<Link_Group>;
def X_Joined : Joined<["-"], "X">, IgnoredGCCCompat;
def Z_Flag : Flag<["-"], "Z">, Group<Link_Group>;
// FIXME: All we do with this is reject it. Remove.
def Z_Joined : Joined<["-"], "Z">;
def all__load : Flag<["-"], "all_load">;
def allowable__client : Separate<["-"], "allowable_client">;
def ansi : Flag<["-", "--"], "ansi">;
def arch__errors__fatal : Flag<["-"], "arch_errors_fatal">;
def arch : Separate<["-"], "arch">, Flags<[DriverOption]>;
def arch__only : Separate<["-"], "arch_only">;
def a : Joined<["-"], "a">;
def autocomplete : Joined<["--"], "autocomplete=">;
def bind__at__load : Flag<["-"], "bind_at_load">;
def bundle__loader : Separate<["-"], "bundle_loader">;
def bundle : Flag<["-"], "bundle">;
def b : JoinedOrSeparate<["-"], "b">, Flags<[Unsupported]>;
def cfguard : Flag<["-"], "cfguard">, Flags<[CC1Option]>,
  HelpText<"Emit tables required for Windows Control Flow Guard.">;
def cl_opt_disable : Flag<["-"], "cl-opt-disable">, Group<opencl_Group>, Flags<[CC1Option]>,
  HelpText<"OpenCL only. This option disables all optimizations. By default optimizations are enabled.">;
def cl_strict_aliasing : Flag<["-"], "cl-strict-aliasing">, Group<opencl_Group>, Flags<[CC1Option]>,
  HelpText<"OpenCL only. This option is added for compatibility with OpenCL 1.0.">;
def cl_single_precision_constant : Flag<["-"], "cl-single-precision-constant">, Group<opencl_Group>, Flags<[CC1Option]>,
  HelpText<"OpenCL only. Treat double precision floating-point constant as single precision constant.">;
def cl_finite_math_only : Flag<["-"], "cl-finite-math-only">, Group<opencl_Group>, Flags<[CC1Option]>,
  HelpText<"OpenCL only. Allow floating-point optimizations that assume arguments and results are not NaNs or +-Inf.">;
def cl_kernel_arg_info : Flag<["-"], "cl-kernel-arg-info">, Group<opencl_Group>, Flags<[CC1Option]>,
  HelpText<"OpenCL only. Generate kernel argument metadata.">;
def cl_unsafe_math_optimizations : Flag<["-"], "cl-unsafe-math-optimizations">, Group<opencl_Group>, Flags<[CC1Option]>,
  HelpText<"OpenCL only. Allow unsafe floating-point optimizations.  Also implies -cl-no-signed-zeros and -cl-mad-enable.">;
def cl_fast_relaxed_math : Flag<["-"], "cl-fast-relaxed-math">, Group<opencl_Group>, Flags<[CC1Option]>,
  HelpText<"OpenCL only. Sets -cl-finite-math-only and -cl-unsafe-math-optimizations, and defines __FAST_RELAXED_MATH__.">;
def cl_mad_enable : Flag<["-"], "cl-mad-enable">, Group<opencl_Group>, Flags<[CC1Option]>,
  HelpText<"OpenCL only. Allow use of less precise MAD computations in the generated binary.">;
def cl_no_signed_zeros : Flag<["-"], "cl-no-signed-zeros">, Group<opencl_Group>, Flags<[CC1Option]>,
  HelpText<"OpenCL only. Allow use of less precise no signed zeros computations in the generated binary.">;
def cl_std_EQ : Joined<["-"], "cl-std=">, Group<opencl_Group>, Flags<[CC1Option]>,
  HelpText<"OpenCL language standard to compile for.">, Values<"cl,CL,cl1.1,CL1.1,cl1.2,CL1.2,cl2.0,CL2.0,c++">;
def cl_denorms_are_zero : Flag<["-"], "cl-denorms-are-zero">, Group<opencl_Group>, Flags<[CC1Option]>,
  HelpText<"OpenCL only. Allow denormals to be flushed to zero.">;
def cl_fp32_correctly_rounded_divide_sqrt : Flag<["-"], "cl-fp32-correctly-rounded-divide-sqrt">, Group<opencl_Group>, Flags<[CC1Option]>,
  HelpText<"OpenCL only. Specify that single precision floating-point divide and sqrt used in the program source are correctly rounded.">;
def cl_uniform_work_group_size : Flag<["-"], "cl-uniform-work-group-size">, Group<opencl_Group>, Flags<[CC1Option]>,
  HelpText<"OpenCL only. Defines that the global work-size be a multiple of the work-group size specified to clEnqueueNDRangeKernel">;
def client__name : JoinedOrSeparate<["-"], "client_name">;
def combine : Flag<["-", "--"], "combine">, Flags<[DriverOption, Unsupported]>;
def compatibility__version : JoinedOrSeparate<["-"], "compatibility_version">;
def config : Separate<["--"], "config">, Flags<[DriverOption]>,
  HelpText<"Specifies configuration file">;
def config_system_dir_EQ : Joined<["--"], "config-system-dir=">, Flags<[DriverOption, HelpHidden]>,
  HelpText<"System directory for configuration files">;
def config_user_dir_EQ : Joined<["--"], "config-user-dir=">, Flags<[DriverOption, HelpHidden]>,
  HelpText<"User directory for configuration files">;
def coverage : Flag<["-", "--"], "coverage">, Flags<[CoreOption]>;
def cpp_precomp : Flag<["-"], "cpp-precomp">, Group<clang_ignored_f_Group>;
def current__version : JoinedOrSeparate<["-"], "current_version">;
def cxx_isystem : JoinedOrSeparate<["-"], "cxx-isystem">, Group<clang_i_Group>,
  HelpText<"Add directory to the C++ SYSTEM include search path">, Flags<[CC1Option]>,
  MetaVarName<"<directory>">;
def c : Flag<["-"], "c">, Flags<[DriverOption]>, Group<Action_Group>,
  HelpText<"Only run preprocess, compile, and assemble steps">;
def cuda_device_only : Flag<["--"], "cuda-device-only">,
  HelpText<"Compile CUDA code for device only">;
def cuda_host_only : Flag<["--"], "cuda-host-only">,
  HelpText<"Compile CUDA code for host only.  Has no effect on non-CUDA "
           "compilations.">;
def cuda_compile_host_device : Flag<["--"], "cuda-compile-host-device">,
  HelpText<"Compile CUDA code for both host and device (default).  Has no "
           "effect on non-CUDA compilations.">;
def cuda_include_ptx_EQ : Joined<["--"], "cuda-include-ptx=">, Flags<[DriverOption]>,
  HelpText<"Include PTX for the following GPU architecture (e.g. sm_35) or 'all'. May be specified more than once.">;
def no_cuda_include_ptx_EQ : Joined<["--"], "no-cuda-include-ptx=">, Flags<[DriverOption]>,
  HelpText<"Do not include PTX for the following GPU architecture (e.g. sm_35) or 'all'. May be specified more than once.">;
def cuda_gpu_arch_EQ : Joined<["--"], "cuda-gpu-arch=">, Flags<[DriverOption]>,
  HelpText<"CUDA GPU architecture (e.g. sm_35).  May be specified more than once.">;
def hip_link : Flag<["--"], "hip-link">,
  HelpText<"Link clang-offload-bundler bundles for HIP">;
def no_cuda_gpu_arch_EQ : Joined<["--"], "no-cuda-gpu-arch=">, Flags<[DriverOption]>,
  HelpText<"Remove GPU architecture (e.g. sm_35) from the list of GPUs to compile for. "
           "'all' resets the list to its default value.">;
def cuda_noopt_device_debug : Flag<["--"], "cuda-noopt-device-debug">,
  HelpText<"Enable device-side debug info generation. Disables ptxas optimizations.">;
def no_cuda_version_check : Flag<["--"], "no-cuda-version-check">,
  HelpText<"Don't error out if the detected version of the CUDA install is "
           "too low for the requested CUDA gpu architecture.">;
def no_cuda_noopt_device_debug : Flag<["--"], "no-cuda-noopt-device-debug">;
def cuda_path_EQ : Joined<["--"], "cuda-path=">, Group<i_Group>,
  HelpText<"CUDA installation path">;
def cuda_path_ignore_env : Flag<["--"], "cuda-path-ignore-env">, Group<i_Group>,
  HelpText<"Ignore environment variables to detect CUDA installation">;
def ptxas_path_EQ : Joined<["--"], "ptxas-path=">, Group<i_Group>,
  HelpText<"Path to ptxas (used for compiling CUDA code)">;
def fcuda_flush_denormals_to_zero : Flag<["-"], "fcuda-flush-denormals-to-zero">,
  Flags<[CC1Option]>, HelpText<"Flush denormal floating point values to zero in CUDA device mode.">;
def fno_cuda_flush_denormals_to_zero : Flag<["-"], "fno-cuda-flush-denormals-to-zero">;
def fcuda_approx_transcendentals : Flag<["-"], "fcuda-approx-transcendentals">,
  Flags<[CC1Option]>, HelpText<"Use approximate transcendental functions">;
def fno_cuda_approx_transcendentals : Flag<["-"], "fno-cuda-approx-transcendentals">;
def fgpu_rdc : Flag<["-"], "fgpu-rdc">, Flags<[CC1Option]>,
  HelpText<"Generate relocatable device code, also known as separate compilation mode.">;
def fno_gpu_rdc : Flag<["-"], "fno-gpu-rdc">;
def : Flag<["-"], "fcuda-rdc">, Alias<fgpu_rdc>;
def : Flag<["-"], "fno-cuda-rdc">, Alias<fno_gpu_rdc>;
def fcuda_short_ptr : Flag<["-"], "fcuda-short-ptr">, Flags<[CC1Option]>,
  HelpText<"Use 32-bit pointers for accessing const/local/shared address spaces.">;
def fno_cuda_short_ptr : Flag<["-"], "fno-cuda-short-ptr">;
def hip_device_lib_path_EQ : Joined<["--"], "hip-device-lib-path=">, Group<Link_Group>,
  HelpText<"HIP device library path">;
def hip_device_lib_EQ : Joined<["--"], "hip-device-lib=">, Group<Link_Group>,
  HelpText<"HIP device library">;
def fhip_dump_offload_linker_script : Flag<["-"], "fhip-dump-offload-linker-script">,
  Group<f_Group>, Flags<[NoArgumentUnused, HelpHidden]>;
def libomptarget_nvptx_path_EQ : Joined<["--"], "libomptarget-nvptx-path=">, Group<i_Group>,
  HelpText<"Path to libomptarget-nvptx libraries">;
def dA : Flag<["-"], "dA">, Group<d_Group>;
def dD : Flag<["-"], "dD">, Group<d_Group>, Flags<[CC1Option]>,
  HelpText<"Print macro definitions in -E mode in addition to normal output">;
def dI : Flag<["-"], "dI">, Group<d_Group>, Flags<[CC1Option]>,
  HelpText<"Print include directives in -E mode in addition to normal output">;
def dM : Flag<["-"], "dM">, Group<d_Group>, Flags<[CC1Option]>,
  HelpText<"Print macro definitions in -E mode instead of normal output">;
def dead__strip : Flag<["-"], "dead_strip">;
def dependency_file : Separate<["-"], "dependency-file">, Flags<[CC1Option]>,
  HelpText<"Filename (or -) to write dependency output to">;
def dependency_dot : Separate<["-"], "dependency-dot">, Flags<[CC1Option]>,
  HelpText<"Filename to write DOT-formatted header dependencies to">;
def module_dependency_dir : Separate<["-"], "module-dependency-dir">,
  Flags<[CC1Option]>, HelpText<"Directory to dump module dependencies to">;
def dumpmachine : Flag<["-"], "dumpmachine">;
def dumpspecs : Flag<["-"], "dumpspecs">, Flags<[Unsupported]>;
def dumpversion : Flag<["-"], "dumpversion">;
def dylib__file : Separate<["-"], "dylib_file">;
def dylinker__install__name : JoinedOrSeparate<["-"], "dylinker_install_name">;
def dylinker : Flag<["-"], "dylinker">;
def dynamiclib : Flag<["-"], "dynamiclib">;
def dynamic : Flag<["-"], "dynamic">, Flags<[NoArgumentUnused]>;
def d_Flag : Flag<["-"], "d">, Group<d_Group>;
def d_Joined : Joined<["-"], "d">, Group<d_Group>;
def emit_ast : Flag<["-"], "emit-ast">,
  HelpText<"Emit Clang AST files for source inputs">;
def emit_llvm : Flag<["-"], "emit-llvm">, Flags<[CC1Option]>, Group<Action_Group>,
  HelpText<"Use the LLVM representation for assembler and object files">;
def exported__symbols__list : Separate<["-"], "exported_symbols_list">;
def e : JoinedOrSeparate<["-"], "e">, Group<Link_Group>;
def fPIC : Flag<["-"], "fPIC">, Group<f_Group>;
def fno_PIC : Flag<["-"], "fno-PIC">, Group<f_Group>;
def fPIE : Flag<["-"], "fPIE">, Group<f_Group>;
def fno_PIE : Flag<["-"], "fno-PIE">, Group<f_Group>;
def faccess_control : Flag<["-"], "faccess-control">, Group<f_Group>;
def falign_functions : Flag<["-"], "falign-functions">, Group<f_Group>;
def falign_functions_EQ : Joined<["-"], "falign-functions=">, Group<f_Group>;
def fno_align_functions: Flag<["-"], "fno-align-functions">, Group<f_Group>;
def fallow_unsupported : Flag<["-"], "fallow-unsupported">, Group<f_Group>;
def fapple_kext : Flag<["-"], "fapple-kext">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Use Apple's kernel extensions ABI">;
def fapple_pragma_pack : Flag<["-"], "fapple-pragma-pack">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Enable Apple gcc-compatible #pragma pack handling">;
def shared_libsan : Flag<["-"], "shared-libsan">,
  HelpText<"Dynamically link the sanitizer runtime">;
def static_libsan : Flag<["-"], "static-libsan">,
  HelpText<"Statically link the sanitizer runtime">;
def : Flag<["-"], "shared-libasan">, Alias<shared_libsan>;
def fasm : Flag<["-"], "fasm">, Group<f_Group>;

def fasm_blocks : Flag<["-"], "fasm-blocks">, Group<f_Group>, Flags<[CC1Option]>;
def fno_asm_blocks : Flag<["-"], "fno-asm-blocks">, Group<f_Group>;

def fassume_sane_operator_new : Flag<["-"], "fassume-sane-operator-new">, Group<f_Group>;
def fastcp : Flag<["-"], "fastcp">, Group<f_Group>;
def fastf : Flag<["-"], "fastf">, Group<f_Group>;
def fast : Flag<["-"], "fast">, Group<f_Group>;
def fasynchronous_unwind_tables : Flag<["-"], "fasynchronous-unwind-tables">, Group<f_Group>;

def fdouble_square_bracket_attributes : Flag<[ "-" ], "fdouble-square-bracket-attributes">,
  Group<f_Group>, Flags<[DriverOption, CC1Option]>,
  HelpText<"Enable '[[]]' attributes in all C and C++ language modes">;
def fno_double_square_bracket_attributes : Flag<[ "-" ], "fno-double-square-bracket-attributes">,
  Group<f_Group>, Flags<[DriverOption, CC1Option]>,
  HelpText<"Disable '[[]]' attributes in all C and C++ language modes">;

def fautolink : Flag <["-"], "fautolink">, Group<f_Group>;
def fno_autolink : Flag <["-"], "fno-autolink">, Group<f_Group>,
  Flags<[DriverOption, CC1Option]>,
  HelpText<"Disable generation of linker directives for automatic library linking">;

// C++ Coroutines TS
def fcoroutines_ts : Flag <["-"], "fcoroutines-ts">, Group<f_Group>,
  Flags<[DriverOption, CC1Option]>,
  HelpText<"Enable support for the C++ Coroutines TS">;
def fno_coroutines_ts : Flag <["-"], "fno-coroutines-ts">, Group<f_Group>,
  Flags<[DriverOption]>;

def fembed_bitcode_EQ : Joined<["-"], "fembed-bitcode=">,
    Group<f_Group>, Flags<[DriverOption, CC1Option, CC1AsOption]>, MetaVarName<"<option>">,
    HelpText<"Embed LLVM bitcode (option: off, all, bitcode, marker)">;
def fembed_bitcode : Flag<["-"], "fembed-bitcode">, Group<f_Group>,
  Alias<fembed_bitcode_EQ>, AliasArgs<["all"]>,
  HelpText<"Embed LLVM IR bitcode as data">;
def fembed_bitcode_marker : Flag<["-"], "fembed-bitcode-marker">,
  Alias<fembed_bitcode_EQ>, AliasArgs<["marker"]>,
  HelpText<"Embed placeholder LLVM IR data as a marker">;
def fgnu_inline_asm : Flag<["-"], "fgnu-inline-asm">, Group<f_Group>, Flags<[DriverOption]>;
def fno_gnu_inline_asm : Flag<["-"], "fno-gnu-inline-asm">, Group<f_Group>,
  Flags<[DriverOption, CC1Option]>,
  HelpText<"Disable GNU style inline asm">;

def fprofile_sample_use : Flag<["-"], "fprofile-sample-use">, Group<f_Group>,
    Flags<[CoreOption]>;
def fno_profile_sample_use : Flag<["-"], "fno-profile-sample-use">, Group<f_Group>,
    Flags<[CoreOption]>;
def fprofile_sample_use_EQ : Joined<["-"], "fprofile-sample-use=">,
    Group<f_Group>, Flags<[DriverOption, CC1Option]>,
    HelpText<"Enable sample-based profile guided optimizations">;
def fprofile_sample_accurate : Flag<["-"], "fprofile-sample-accurate">,
    Group<f_Group>, Flags<[DriverOption, CC1Option]>,
    HelpText<"Specifies that the sample profile is accurate">,
    DocBrief<[{Specifies that the sample profile is accurate. If the sample
               profile is accurate, callsites without profile samples are marked
               as cold. Otherwise, treat callsites without profile samples as if
               we have no profile}]>;
def fno_profile_sample_accurate : Flag<["-"], "fno-profile-sample-accurate">,
  Group<f_Group>, Flags<[DriverOption]>;
def fauto_profile : Flag<["-"], "fauto-profile">, Group<f_Group>,
    Alias<fprofile_sample_use>;
def fno_auto_profile : Flag<["-"], "fno-auto-profile">, Group<f_Group>,
    Alias<fno_profile_sample_use>;
def fauto_profile_EQ : Joined<["-"], "fauto-profile=">,
    Alias<fprofile_sample_use_EQ>;
def fauto_profile_accurate : Flag<["-"], "fauto-profile-accurate">,
    Group<f_Group>, Alias<fprofile_sample_accurate>;
def fno_auto_profile_accurate : Flag<["-"], "fno-auto-profile-accurate">,
    Group<f_Group>, Alias<fno_profile_sample_accurate>;
def fdebug_info_for_profiling : Flag<["-"], "fdebug-info-for-profiling">, Group<f_Group>,
    Flags<[CC1Option]>,
    HelpText<"Emit extra debug info to make sample profile more accurate.">;
def fno_debug_info_for_profiling : Flag<["-"], "fno-debug-info-for-profiling">, Group<f_Group>,
    Flags<[DriverOption]>,
    HelpText<"Do not emit extra debug info for sample profiler.">;
def fprofile_instr_generate : Flag<["-"], "fprofile-instr-generate">,
    Group<f_Group>, Flags<[CoreOption]>,
    HelpText<"Generate instrumented code to collect execution counts into default.profraw file (overridden by '=' form of option or LLVM_PROFILE_FILE env var)">;
def fprofile_instr_generate_EQ : Joined<["-"], "fprofile-instr-generate=">,
    Group<f_Group>, Flags<[CoreOption]>, MetaVarName<"<file>">,
    HelpText<"Generate instrumented code to collect execution counts into <file> (overridden by LLVM_PROFILE_FILE env var)">;
def fprofile_instr_use : Flag<["-"], "fprofile-instr-use">, Group<f_Group>,
    Flags<[CoreOption]>;
def fprofile_instr_use_EQ : Joined<["-"], "fprofile-instr-use=">,
    Group<f_Group>, Flags<[CoreOption]>,
    HelpText<"Use instrumentation data for profile-guided optimization">;
def fprofile_remapping_file_EQ : Joined<["-"], "fprofile-remapping-file=">,
    Group<f_Group>, Flags<[CC1Option, CoreOption]>, MetaVarName<"<file>">,
    HelpText<"Use the remappings described in <file> to match the profile data against names in the program">;
def fprofile_remapping_file : Separate<["-"], "fprofile-remapping-file">,
    Group<f_Group>, Flags<[CoreOption]>, Alias<fprofile_remapping_file_EQ>;
def fcoverage_mapping : Flag<["-"], "fcoverage-mapping">,
    Group<f_Group>, Flags<[CC1Option, CoreOption]>,
    HelpText<"Generate coverage mapping to enable code coverage analysis">;
def fno_coverage_mapping : Flag<["-"], "fno-coverage-mapping">,
    Group<f_Group>, Flags<[DriverOption, CoreOption]>,
    HelpText<"Disable code coverage analysis">;
def fprofile_generate : Flag<["-"], "fprofile-generate">,
    Group<f_Group>, Flags<[DriverOption]>,
    HelpText<"Generate instrumented code to collect execution counts into default.profraw (overridden by LLVM_PROFILE_FILE env var)">;
def fprofile_generate_EQ : Joined<["-"], "fprofile-generate=">,
    Group<f_Group>, Flags<[DriverOption]>, MetaVarName<"<directory>">,
    HelpText<"Generate instrumented code to collect execution counts into <directory>/default.profraw (overridden by LLVM_PROFILE_FILE env var)">;
def fprofile_use : Flag<["-"], "fprofile-use">, Group<f_Group>,
    Alias<fprofile_instr_use>;
def fprofile_use_EQ : Joined<["-"], "fprofile-use=">,
    Group<f_Group>, Flags<[DriverOption]>, MetaVarName<"<pathname>">,
    HelpText<"Use instrumentation data for profile-guided optimization. If pathname is a directory, it reads from <pathname>/default.profdata. Otherwise, it reads from file <pathname>.">;
def fno_profile_instr_generate : Flag<["-"], "fno-profile-instr-generate">,
    Group<f_Group>, Flags<[DriverOption]>,
    HelpText<"Disable generation of profile instrumentation.">;
def fno_profile_generate : Flag<["-"], "fno-profile-generate">,
    Group<f_Group>, Flags<[DriverOption]>,
    HelpText<"Disable generation of profile instrumentation.">;
def fno_profile_instr_use : Flag<["-"], "fno-profile-instr-use">,
    Group<f_Group>, Flags<[DriverOption]>,
    HelpText<"Disable using instrumentation data for profile-guided optimization">;
def fno_profile_use : Flag<["-"], "fno-profile-use">,
    Alias<fno_profile_instr_use>;
def fprofile_filter_files_EQ : Joined<["-"], "fprofile-filter-files=">,
    Group<f_Group>, Flags<[CC1Option, CoreOption]>,
    HelpText<"Instrument only functions from files where names match any regex separated by a semi-colon">;
def fprofile_exclude_files_EQ : Joined<["-"], "fprofile-exclude-files=">,
    Group<f_Group>, Flags<[CC1Option, CoreOption]>,
    HelpText<"Instrument only functions from files where names don't match all the regexes separated by a semi-colon">;

def faddrsig : Flag<["-"], "faddrsig">, Group<f_Group>, Flags<[CoreOption, CC1Option]>,
  HelpText<"Emit an address-significance table">;
def fno_addrsig : Flag<["-"], "fno-addrsig">, Group<f_Group>, Flags<[CoreOption]>,
  HelpText<"Don't emit an address-significance table">;
def fblocks : Flag<["-"], "fblocks">, Group<f_Group>, Flags<[CoreOption, CC1Option]>,
  HelpText<"Enable the 'blocks' language feature">;
def fbootclasspath_EQ : Joined<["-"], "fbootclasspath=">, Group<f_Group>;
def fborland_extensions : Flag<["-"], "fborland-extensions">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Accept non-standard constructs supported by the Borland compiler">;
def fbuiltin : Flag<["-"], "fbuiltin">, Group<f_Group>, Flags<[CoreOption]>;
def fbuiltin_module_map : Flag <["-"], "fbuiltin-module-map">, Group<f_Group>,
  Flags<[DriverOption]>, HelpText<"Load the clang builtins module map file.">;
def fcaret_diagnostics : Flag<["-"], "fcaret-diagnostics">, Group<f_Group>;
def fclang_abi_compat_EQ : Joined<["-"], "fclang-abi-compat=">, Group<f_clang_Group>,
  Flags<[CC1Option]>, MetaVarName<"<version>">, Values<"<major>.<minor>,latest">,
  HelpText<"Attempt to match the ABI of Clang <version>">;
def fclasspath_EQ : Joined<["-"], "fclasspath=">, Group<f_Group>;
def fcolor_diagnostics : Flag<["-"], "fcolor-diagnostics">, Group<f_Group>,
  Flags<[CoreOption, CC1Option]>, HelpText<"Use colors in diagnostics">;
def fdiagnostics_color : Flag<["-"], "fdiagnostics-color">, Group<f_Group>,
  Flags<[CoreOption, DriverOption]>;
def fdiagnostics_color_EQ : Joined<["-"], "fdiagnostics-color=">, Group<f_Group>;
def fansi_escape_codes : Flag<["-"], "fansi-escape-codes">, Group<f_Group>,
  Flags<[CoreOption, CC1Option]>, HelpText<"Use ANSI escape codes for diagnostics">;
def fcomment_block_commands : CommaJoined<["-"], "fcomment-block-commands=">, Group<f_clang_Group>, Flags<[CC1Option]>,
  HelpText<"Treat each comma separated argument in <arg> as a documentation comment block command">,
  MetaVarName<"<arg>">;
def fparse_all_comments : Flag<["-"], "fparse-all-comments">, Group<f_clang_Group>, Flags<[CC1Option]>;
def frecord_command_line : Flag<["-"], "frecord-command-line">,
  Group<f_clang_Group>;
def fno_record_command_line : Flag<["-"], "fno-record-command-line">,
  Group<f_clang_Group>;
def : Flag<["-"], "frecord-gcc-switches">, Alias<frecord_command_line>;
def : Flag<["-"], "fno-record-gcc-switches">, Alias<fno_record_command_line>;
def fcommon : Flag<["-"], "fcommon">, Group<f_Group>;
def fcompile_resource_EQ : Joined<["-"], "fcompile-resource=">, Group<f_Group>;
def fcomplete_member_pointers : Flag<["-"], "fcomplete-member-pointers">, Group<f_clang_Group>,
   Flags<[CoreOption, CC1Option]>,
   HelpText<"Require member pointer base types to be complete if they would be significant under the Microsoft ABI">;
def fno_complete_member_pointers : Flag<["-"], "fno-complete-member-pointers">, Group<f_clang_Group>,
   Flags<[CoreOption]>,
   HelpText<"Do not require member pointer base types to be complete if they would be significant under the Microsoft ABI">;
def fcf_runtime_abi_EQ : Joined<["-"], "fcf-runtime-abi=">, Group<f_Group>,
    Flags<[CC1Option]>;
def fconstant_cfstrings : Flag<["-"], "fconstant-cfstrings">, Group<f_Group>;
def fconstant_string_class_EQ : Joined<["-"], "fconstant-string-class=">, Group<f_Group>;
def fconstexpr_depth_EQ : Joined<["-"], "fconstexpr-depth=">, Group<f_Group>;
def fconstexpr_steps_EQ : Joined<["-"], "fconstexpr-steps=">, Group<f_Group>;
def fconstexpr_backtrace_limit_EQ : Joined<["-"], "fconstexpr-backtrace-limit=">,
                                    Group<f_Group>;
def fno_crash_diagnostics : Flag<["-"], "fno-crash-diagnostics">, Group<f_clang_Group>, Flags<[NoArgumentUnused, CoreOption]>,
  HelpText<"Disable auto-generation of preprocessed source files and a script for reproduction during a clang crash">;
def fcrash_diagnostics_dir : Joined<["-"], "fcrash-diagnostics-dir=">, Group<f_clang_Group>, Flags<[NoArgumentUnused, CoreOption]>;
def fcreate_profile : Flag<["-"], "fcreate-profile">, Group<f_Group>;
def fcxx_exceptions: Flag<["-"], "fcxx-exceptions">, Group<f_Group>,
  HelpText<"Enable C++ exceptions">, Flags<[CC1Option]>;
def fcxx_modules : Flag <["-"], "fcxx-modules">, Group<f_Group>,
  Flags<[DriverOption]>;
def fdebug_pass_arguments : Flag<["-"], "fdebug-pass-arguments">, Group<f_Group>;
def fdebug_pass_structure : Flag<["-"], "fdebug-pass-structure">, Group<f_Group>;
def fdepfile_entry : Joined<["-"], "fdepfile-entry=">,
    Group<f_clang_Group>, Flags<[CC1Option]>;
def fdiagnostics_fixit_info : Flag<["-"], "fdiagnostics-fixit-info">, Group<f_clang_Group>;
def fdiagnostics_parseable_fixits : Flag<["-"], "fdiagnostics-parseable-fixits">, Group<f_clang_Group>,
    Flags<[CoreOption, CC1Option]>, HelpText<"Print fix-its in machine parseable form">;
def fdiagnostics_print_source_range_info : Flag<["-"], "fdiagnostics-print-source-range-info">,
    Group<f_clang_Group>,  Flags<[CC1Option]>,
    HelpText<"Print source range spans in numeric form">;
def fdiagnostics_show_hotness : Flag<["-"], "fdiagnostics-show-hotness">, Group<f_Group>,
    Flags<[CC1Option]>, HelpText<"Enable profile hotness information in diagnostic line">;
def fdiagnostics_hotness_threshold_EQ : Joined<["-"], "fdiagnostics-hotness-threshold=">,
    Group<f_Group>, Flags<[CC1Option]>, MetaVarName<"<number>">,
    HelpText<"Prevent optimization remarks from being output if they do not have at least this profile count">;
def fdiagnostics_show_option : Flag<["-"], "fdiagnostics-show-option">, Group<f_Group>,
    Flags<[CC1Option]>, HelpText<"Print option name with mappable diagnostics">;
def fdiagnostics_show_note_include_stack : Flag<["-"], "fdiagnostics-show-note-include-stack">,
    Group<f_Group>,  Flags<[CC1Option]>, HelpText<"Display include stacks for diagnostic notes">;
def fdiagnostics_format_EQ : Joined<["-"], "fdiagnostics-format=">, Group<f_clang_Group>;
def fdiagnostics_show_category_EQ : Joined<["-"], "fdiagnostics-show-category=">, Group<f_clang_Group>;
def fdiagnostics_show_template_tree : Flag<["-"], "fdiagnostics-show-template-tree">,
    Group<f_Group>, Flags<[CC1Option]>,
    HelpText<"Print a template comparison tree for differing templates">;
def fdeclspec : Flag<["-"], "fdeclspec">, Group<f_clang_Group>,
  HelpText<"Allow __declspec as a keyword">, Flags<[CC1Option]>;
def fdiscard_value_names : Flag<["-"], "fdiscard-value-names">, Group<f_clang_Group>,
  HelpText<"Discard value names in LLVM IR">, Flags<[DriverOption]>;
def fno_discard_value_names : Flag<["-"], "fno-discard-value-names">, Group<f_clang_Group>,
  HelpText<"Do not discard value names in LLVM IR">, Flags<[DriverOption]>;
def fdollars_in_identifiers : Flag<["-"], "fdollars-in-identifiers">, Group<f_Group>,
  HelpText<"Allow '$' in identifiers">, Flags<[CC1Option]>;
def fdwarf2_cfi_asm : Flag<["-"], "fdwarf2-cfi-asm">, Group<clang_ignored_f_Group>;
def fno_dwarf2_cfi_asm : Flag<["-"], "fno-dwarf2-cfi-asm">, Group<clang_ignored_f_Group>;
def fdwarf_directory_asm : Flag<["-"], "fdwarf-directory-asm">, Group<f_Group>;
def fno_dwarf_directory_asm : Flag<["-"], "fno-dwarf-directory-asm">, Group<f_Group>, Flags<[CC1Option]>;
def felide_constructors : Flag<["-"], "felide-constructors">, Group<f_Group>;
def fno_elide_type : Flag<["-"], "fno-elide-type">, Group<f_Group>,
    Flags<[CC1Option]>,
    HelpText<"Do not elide types when printing diagnostics">;
def feliminate_unused_debug_symbols : Flag<["-"], "feliminate-unused-debug-symbols">, Group<f_Group>;
def femit_all_decls : Flag<["-"], "femit-all-decls">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Emit all declarations, even if unused">;
def femulated_tls : Flag<["-"], "femulated-tls">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Use emutls functions to access thread_local variables">;
def fno_emulated_tls : Flag<["-"], "fno-emulated-tls">, Group<f_Group>, Flags<[CC1Option]>;
def fencoding_EQ : Joined<["-"], "fencoding=">, Group<f_Group>;
def ferror_limit_EQ : Joined<["-"], "ferror-limit=">, Group<f_Group>, Flags<[CoreOption]>;
def fexceptions : Flag<["-"], "fexceptions">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Enable support for exception handling">;
def fdwarf_exceptions : Flag<["-"], "fdwarf-exceptions">, Group<f_Group>,
  Flags<[CC1Option]>, HelpText<"Use DWARF style exceptions">;
def fsjlj_exceptions : Flag<["-"], "fsjlj-exceptions">, Group<f_Group>,
  Flags<[CC1Option]>, HelpText<"Use SjLj style exceptions">;
def fseh_exceptions : Flag<["-"], "fseh-exceptions">, Group<f_Group>,
  Flags<[CC1Option]>, HelpText<"Use SEH style exceptions">;
def fexcess_precision_EQ : Joined<["-"], "fexcess-precision=">,
    Group<clang_ignored_gcc_optimization_f_Group>;
def : Flag<["-"], "fexpensive-optimizations">, Group<clang_ignored_gcc_optimization_f_Group>;
def : Flag<["-"], "fno-expensive-optimizations">, Group<clang_ignored_gcc_optimization_f_Group>;
def fextdirs_EQ : Joined<["-"], "fextdirs=">, Group<f_Group>;
def : Flag<["-"], "fdefer-pop">, Group<clang_ignored_gcc_optimization_f_Group>;
def : Flag<["-"], "fno-defer-pop">, Group<clang_ignored_gcc_optimization_f_Group>;
def : Flag<["-"], "fextended-identifiers">, Group<clang_ignored_f_Group>;
def : Flag<["-"], "fno-extended-identifiers">, Group<f_Group>, Flags<[Unsupported]>;
def fhosted : Flag<["-"], "fhosted">, Group<f_Group>;
def fdenormal_fp_math_EQ : Joined<["-"], "fdenormal-fp-math=">, Group<f_Group>, Flags<[CC1Option]>;
def ffast_math : Flag<["-"], "ffast-math">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Allow aggressive, lossy floating-point optimizations">;
def fno_fast_math : Flag<["-"], "fno-fast-math">, Group<f_Group>;
def fmath_errno : Flag<["-"], "fmath-errno">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Require math functions to indicate errors by setting errno">;
def fno_math_errno : Flag<["-"], "fno-math-errno">, Group<f_Group>;
def fbracket_depth_EQ : Joined<["-"], "fbracket-depth=">, Group<f_Group>;
def fsignaling_math : Flag<["-"], "fsignaling-math">, Group<f_Group>;
def fno_signaling_math : Flag<["-"], "fno-signaling-math">, Group<f_Group>;
def fjump_tables : Flag<["-"], "fjump-tables">, Group<f_Group>;
def fno_jump_tables : Flag<["-"], "fno-jump-tables">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Do not use jump tables for lowering switches">;
def fforce_enable_int128 : Flag<["-"], "fforce-enable-int128">,
  Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Enable support for int128_t type">;
def fno_force_enable_int128 : Flag<["-"], "fno-force-enable-int128">,
  Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Disable support for int128_t type">;
def fkeep_static_consts : Flag<["-"], "fkeep-static-consts">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Keep static const variables even if unused">;
def ffixed_point : Flag<["-"], "ffixed-point">, Group<f_Group>,
                   Flags<[CC1Option]>, HelpText<"Enable fixed point types">;
def fno_fixed_point : Flag<["-"], "fno-fixed-point">, Group<f_Group>,
                      HelpText<"Disable fixed point types">;
def fcxx_static_destructors : Flag<["-"], "fc++-static-destructors">,
  Group<f_Group>,
  HelpText<"Enable C++ static destructor registration (the default)">;
def fno_cxx_static_destructors : Flag<["-"], "fno-c++-static-destructors">,
  Group<f_Group>,
  Flags<[CC1Option]>,
  HelpText<"Disable C++ static destructor registration">;

// Begin sanitizer flags. These should all be core options exposed in all driver
// modes.
let Flags = [CC1Option, CoreOption] in {

def fsanitize_EQ : CommaJoined<["-"], "fsanitize=">, Group<f_clang_Group>,
                   MetaVarName<"<check>">,
                   HelpText<"Turn on runtime checks for various forms of undefined "
                            "or suspicious behavior. See user manual for available checks">;
def fno_sanitize_EQ : CommaJoined<["-"], "fno-sanitize=">, Group<f_clang_Group>,
                      Flags<[CoreOption, DriverOption]>;
def fsanitize_blacklist : Joined<["-"], "fsanitize-blacklist=">,
                          Group<f_clang_Group>,
                          HelpText<"Path to blacklist file for sanitizers">;
def fno_sanitize_blacklist : Flag<["-"], "fno-sanitize-blacklist">,
                             Group<f_clang_Group>,
                             HelpText<"Don't use blacklist file for sanitizers">;
def fsanitize_coverage
    : CommaJoined<["-"], "fsanitize-coverage=">,
      Group<f_clang_Group>,
      HelpText<"Specify the type of coverage instrumentation for Sanitizers">;
def fno_sanitize_coverage
    : CommaJoined<["-"], "fno-sanitize-coverage=">,
      Group<f_clang_Group>, Flags<[CoreOption, DriverOption]>,
      HelpText<"Disable specified features of coverage instrumentation for "
               "Sanitizers">, Values<"func,bb,edge,indirect-calls,trace-bb,trace-cmp,trace-div,trace-gep,8bit-counters,trace-pc,trace-pc-guard,no-prune,inline-8bit-counters">;
def fsanitize_memory_track_origins_EQ : Joined<["-"], "fsanitize-memory-track-origins=">,
                                        Group<f_clang_Group>,
                                        HelpText<"Enable origins tracking in MemorySanitizer">;
def fsanitize_memory_track_origins : Flag<["-"], "fsanitize-memory-track-origins">,
                                     Group<f_clang_Group>,
                                     HelpText<"Enable origins tracking in MemorySanitizer">;
def fno_sanitize_memory_track_origins : Flag<["-"], "fno-sanitize-memory-track-origins">,
                                        Group<f_clang_Group>,
                                        Flags<[CoreOption, DriverOption]>,
                                        HelpText<"Disable origins tracking in MemorySanitizer">;
def fsanitize_memory_use_after_dtor : Flag<["-"], "fsanitize-memory-use-after-dtor">,
                                     Group<f_clang_Group>,
                                     HelpText<"Enable use-after-destroy detection in MemorySanitizer">;
def fno_sanitize_memory_use_after_dtor : Flag<["-"], "fno-sanitize-memory-use-after-dtor">,
                                     Group<f_clang_Group>,
                                     HelpText<"Disable use-after-destroy detection in MemorySanitizer">;
def fsanitize_address_field_padding : Joined<["-"], "fsanitize-address-field-padding=">,
                                        Group<f_clang_Group>,
                                        HelpText<"Level of field padding for AddressSanitizer">;
def fsanitize_address_use_after_scope : Flag<["-"], "fsanitize-address-use-after-scope">,
                                        Group<f_clang_Group>,
                                        HelpText<"Enable use-after-scope detection in AddressSanitizer">;
def fno_sanitize_address_use_after_scope : Flag<["-"], "fno-sanitize-address-use-after-scope">,
                                           Group<f_clang_Group>,
                                           Flags<[CoreOption, DriverOption]>,
                                           HelpText<"Disable use-after-scope detection in AddressSanitizer">;
def fsanitize_address_poison_custom_array_cookie
    : Flag<[ "-" ], "fsanitize-address-poison-custom-array-cookie">,
      Group<f_clang_Group>,
      HelpText<"Enable poisoning array cookies when using custom operator new[] in AddressSanitizer">;
def fno_sanitize_address_poison_custom_array_cookie
    : Flag<[ "-" ], "fno-sanitize-address-poison-custom-array-cookie">,
      Group<f_clang_Group>,
      HelpText<"Disable poisoning array cookies when using custom operator new[] in AddressSanitizer">;
def fsanitize_address_globals_dead_stripping : Flag<["-"], "fsanitize-address-globals-dead-stripping">,
                                        Group<f_clang_Group>,
                                        HelpText<"Enable linker dead stripping of globals in AddressSanitizer">;
def fsanitize_address_use_odr_indicator
    : Flag<["-"], "fsanitize-address-use-odr-indicator">,
      Group<f_clang_Group>,
      HelpText<"Enable ODR indicator globals to avoid false ODR violation reports in partially sanitized programs at the cost of an increase in binary size">;
def fno_sanitize_address_use_odr_indicator
    : Flag<["-"], "fno-sanitize-address-use-odr-indicator">,
      Group<f_clang_Group>,
      HelpText<"Disable ODR indicator globals">;
def fsanitize_hwaddress_abi_EQ
    : Joined<["-"], "fsanitize-hwaddress-abi=">,
      Group<f_clang_Group>,
      HelpText<"Select the HWAddressSanitizer ABI to target (interceptor or platform, default interceptor)">;
def fsanitize_recover : Flag<["-"], "fsanitize-recover">, Group<f_clang_Group>;
def fno_sanitize_recover : Flag<["-"], "fno-sanitize-recover">,
                           Flags<[CoreOption, DriverOption]>,
                           Group<f_clang_Group>;
def fsanitize_recover_EQ : CommaJoined<["-"], "fsanitize-recover=">,
                           Group<f_clang_Group>,
                           HelpText<"Enable recovery for specified sanitizers">;
def fno_sanitize_recover_EQ
    : CommaJoined<["-"], "fno-sanitize-recover=">,
      Group<f_clang_Group>,
      Flags<[CoreOption, DriverOption]>,
      HelpText<"Disable recovery for specified sanitizers">;
def fsanitize_trap_EQ : CommaJoined<["-"], "fsanitize-trap=">, Group<f_clang_Group>,
                        HelpText<"Enable trapping for specified sanitizers">;
def fno_sanitize_trap_EQ : CommaJoined<["-"], "fno-sanitize-trap=">, Group<f_clang_Group>,
                           Flags<[CoreOption, DriverOption]>,
                           HelpText<"Disable trapping for specified sanitizers">;
def fsanitize_undefined_trap_on_error : Flag<["-"], "fsanitize-undefined-trap-on-error">,
                                        Group<f_clang_Group>;
def fno_sanitize_undefined_trap_on_error : Flag<["-"], "fno-sanitize-undefined-trap-on-error">,
                                           Group<f_clang_Group>;
def fsanitize_minimal_runtime : Flag<["-"], "fsanitize-minimal-runtime">,
                                        Group<f_clang_Group>;
def fno_sanitize_minimal_runtime : Flag<["-"], "fno-sanitize-minimal-runtime">,
                                        Group<f_clang_Group>;
def fsanitize_link_cxx_runtime : Flag<["-"], "fsanitize-link-c++-runtime">,
                                 Group<f_clang_Group>;
def fsanitize_cfi_cross_dso : Flag<["-"], "fsanitize-cfi-cross-dso">,
                              Group<f_clang_Group>,
                              HelpText<"Enable control flow integrity (CFI) checks for cross-DSO calls.">;
def fno_sanitize_cfi_cross_dso : Flag<["-"], "fno-sanitize-cfi-cross-dso">,
                                 Flags<[CoreOption, DriverOption]>,
                                 Group<f_clang_Group>,
                                 HelpText<"Disable control flow integrity (CFI) checks for cross-DSO calls.">;
def fsanitize_cfi_icall_generalize_pointers : Flag<["-"], "fsanitize-cfi-icall-generalize-pointers">,
                                              Group<f_clang_Group>,
                                              HelpText<"Generalize pointers in CFI indirect call type signature checks">;
def fsanitize_stats : Flag<["-"], "fsanitize-stats">,
                              Group<f_clang_Group>,
                              HelpText<"Enable sanitizer statistics gathering.">;
def fno_sanitize_stats : Flag<["-"], "fno-sanitize-stats">,
                                 Group<f_clang_Group>,
                                 Flags<[CoreOption, DriverOption]>,
                                 HelpText<"Disable sanitizer statistics gathering.">;
def fsanitize_thread_memory_access : Flag<["-"], "fsanitize-thread-memory-access">,
                                     Group<f_clang_Group>,
                                     HelpText<"Enable memory access instrumentation in ThreadSanitizer (default)">;
def fno_sanitize_thread_memory_access : Flag<["-"], "fno-sanitize-thread-memory-access">,
                                        Group<f_clang_Group>,
                                        Flags<[CoreOption, DriverOption]>,
                                        HelpText<"Disable memory access instrumentation in ThreadSanitizer">;
def fsanitize_thread_func_entry_exit : Flag<["-"], "fsanitize-thread-func-entry-exit">,
                                       Group<f_clang_Group>,
                                       HelpText<"Enable function entry/exit instrumentation in ThreadSanitizer (default)">;
def fno_sanitize_thread_func_entry_exit : Flag<["-"], "fno-sanitize-thread-func-entry-exit">,
                                          Group<f_clang_Group>,
                                          Flags<[CoreOption, DriverOption]>,
                                          HelpText<"Disable function entry/exit instrumentation in ThreadSanitizer">;
def fsanitize_thread_atomics : Flag<["-"], "fsanitize-thread-atomics">,
                               Group<f_clang_Group>,
                               HelpText<"Enable atomic operations instrumentation in ThreadSanitizer (default)">;
def fno_sanitize_thread_atomics : Flag<["-"], "fno-sanitize-thread-atomics">,
                                  Group<f_clang_Group>,
                                  Flags<[CoreOption, DriverOption]>,
                                  HelpText<"Disable atomic operations instrumentation in ThreadSanitizer">;
def fsanitize_undefined_strip_path_components_EQ : Joined<["-"], "fsanitize-undefined-strip-path-components=">,
  Group<f_clang_Group>, MetaVarName<"<number>">,
  HelpText<"Strip (or keep only, if negative) a given number of path components "
           "when emitting check metadata.">;

} // end -f[no-]sanitize* flags

def funsafe_math_optimizations : Flag<["-"], "funsafe-math-optimizations">,
  Group<f_Group>;
def fno_unsafe_math_optimizations : Flag<["-"], "fno-unsafe-math-optimizations">,
  Group<f_Group>;
def fassociative_math : Flag<["-"], "fassociative-math">, Group<f_Group>;
def fno_associative_math : Flag<["-"], "fno-associative-math">, Group<f_Group>;
def freciprocal_math :
  Flag<["-"], "freciprocal-math">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Allow division operations to be reassociated">;
def fno_reciprocal_math : Flag<["-"], "fno-reciprocal-math">, Group<f_Group>;
def ffinite_math_only : Flag<["-"], "ffinite-math-only">, Group<f_Group>, Flags<[CC1Option]>;
def fno_finite_math_only : Flag<["-"], "fno-finite-math-only">, Group<f_Group>;
def fsigned_zeros : Flag<["-"], "fsigned-zeros">, Group<f_Group>;
def fno_signed_zeros :
  Flag<["-"], "fno-signed-zeros">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Allow optimizations that ignore the sign of floating point zeros">;
def fhonor_nans : Flag<["-"], "fhonor-nans">, Group<f_Group>;
def fno_honor_nans : Flag<["-"], "fno-honor-nans">, Group<f_Group>;
def fhonor_infinities : Flag<["-"], "fhonor-infinities">, Group<f_Group>;
def fno_honor_infinities : Flag<["-"], "fno-honor-infinities">, Group<f_Group>;
// This option was originally misspelt "infinites" [sic].
def : Flag<["-"], "fhonor-infinites">, Alias<fhonor_infinities>;
def : Flag<["-"], "fno-honor-infinites">, Alias<fno_honor_infinities>;
def ftrapping_math : Flag<["-"], "ftrapping-math">, Group<f_Group>, Flags<[CC1Option]>;
def fno_trapping_math : Flag<["-"], "fno-trapping-math">, Group<f_Group>, Flags<[CC1Option]>;
def ffp_contract : Joined<["-"], "ffp-contract=">, Group<f_Group>,
  Flags<[CC1Option]>, HelpText<"Form fused FP ops (e.g. FMAs): fast (everywhere)"
  " | on (according to FP_CONTRACT pragma, default) | off (never fuse)">, Values<"fast,on,off">;

def fstrict_float_cast_overflow : Flag<["-"],
  "fstrict-float-cast-overflow">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Assume that overflowing float-to-int casts are undefined (default)">;
def fno_strict_float_cast_overflow : Flag<["-"],
  "fno-strict-float-cast-overflow">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Relax language rules and try to match the behavior of the target's native float-to-int conversion instructions">;

def ffor_scope : Flag<["-"], "ffor-scope">, Group<f_Group>;
def fno_for_scope : Flag<["-"], "fno-for-scope">, Group<f_Group>;

def frewrite_includes : Flag<["-"], "frewrite-includes">, Group<f_Group>,
  Flags<[CC1Option]>;
def fno_rewrite_includes : Flag<["-"], "fno-rewrite-includes">, Group<f_Group>;

def frewrite_imports : Flag<["-"], "frewrite-imports">, Group<f_Group>,
  Flags<[CC1Option]>;
def fno_rewrite_imports : Flag<["-"], "fno-rewrite-imports">, Group<f_Group>;

def fdelete_null_pointer_checks : Flag<["-"],
  "fdelete-null-pointer-checks">, Group<f_Group>,
  HelpText<"Treat usage of null pointers as undefined behavior.">;
def fno_delete_null_pointer_checks : Flag<["-"],
  "fno-delete-null-pointer-checks">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Do not treat usage of null pointers as undefined behavior.">;

def frewrite_map_file : Separate<["-"], "frewrite-map-file">,
                        Group<f_Group>,
                        Flags<[ DriverOption, CC1Option ]>;
def frewrite_map_file_EQ : Joined<["-"], "frewrite-map-file=">,
                           Group<f_Group>,
                           Flags<[DriverOption]>;

def fuse_line_directives : Flag<["-"], "fuse-line-directives">, Group<f_Group>,
  Flags<[CC1Option]>;
def fno_use_line_directives : Flag<["-"], "fno-use-line-directives">, Group<f_Group>;

def ffreestanding : Flag<["-"], "ffreestanding">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Assert that the compilation takes place in a freestanding environment">;
def fgnu_keywords : Flag<["-"], "fgnu-keywords">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Allow GNU-extension keywords regardless of language standard">;
def fgnu89_inline : Flag<["-"], "fgnu89-inline">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Use the gnu89 inline semantics">;
def fno_gnu89_inline : Flag<["-"], "fno-gnu89-inline">, Group<f_Group>;
def fgnu_runtime : Flag<["-"], "fgnu-runtime">, Group<f_Group>,
  HelpText<"Generate output compatible with the standard GNU Objective-C runtime">;
def fheinous_gnu_extensions : Flag<["-"], "fheinous-gnu-extensions">, Flags<[CC1Option]>;
def filelist : Separate<["-"], "filelist">, Flags<[LinkerInput]>,
               Group<Link_Group>;
def : Flag<["-"], "findirect-virtual-calls">, Alias<fapple_kext>;
def finline_functions : Flag<["-"], "finline-functions">, Group<f_clang_Group>, Flags<[CC1Option]>,
  HelpText<"Inline suitable functions">;
def finline_hint_functions: Flag<["-"], "finline-hint-functions">, Group<f_clang_Group>, Flags<[CC1Option]>,
  HelpText<"Inline functions which are (explicitly or implicitly) marked inline">;
def finline : Flag<["-"], "finline">, Group<clang_ignored_f_Group>;
def fexperimental_isel : Flag<["-"], "fexperimental-isel">, Group<f_clang_Group>,
  HelpText<"Enables the experimental global instruction selector">;
def fexperimental_new_pass_manager : Flag<["-"], "fexperimental-new-pass-manager">,
  Group<f_clang_Group>, Flags<[CC1Option]>,
  HelpText<"Enables an experimental new pass manager in LLVM.">;
def finput_charset_EQ : Joined<["-"], "finput-charset=">, Group<f_Group>;
def fexec_charset_EQ : Joined<["-"], "fexec-charset=">, Group<f_Group>;
def finstrument_functions : Flag<["-"], "finstrument-functions">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Generate calls to instrument function entry and exit">;
def finstrument_functions_after_inlining : Flag<["-"], "finstrument-functions-after-inlining">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Like -finstrument-functions, but insert the calls after inlining">;
def finstrument_function_entry_bare : Flag<["-"], "finstrument-function-entry-bare">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Instrument function entry only, after inlining, without arguments to the instrumentation call">;
def fcf_protection_EQ : Joined<["-"], "fcf-protection=">, Flags<[CoreOption, CC1Option]>, Group<f_Group>,
  HelpText<"Instrument control-flow architecture protection. Options: return, branch, full, none.">, Values<"return,branch,full,none">;
def fcf_protection : Flag<["-"], "fcf-protection">, Group<f_Group>, Flags<[CoreOption, CC1Option]>,
  Alias<fcf_protection_EQ>, AliasArgs<["full"]>,
  HelpText<"Enable cf-protection in 'full' mode">;

def fxray_instrument : Flag<["-"], "fxray-instrument">, Group<f_Group>,
  Flags<[CC1Option]>,
  HelpText<"Generate XRay instrumentation sleds on function entry and exit">;
def fnoxray_instrument : Flag<["-"], "fno-xray-instrument">, Group<f_Group>,
  Flags<[CC1Option]>;

def fxray_instruction_threshold_EQ :
  JoinedOrSeparate<["-"], "fxray-instruction-threshold=">,
  Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Sets the minimum function size to instrument with XRay">;
def fxray_instruction_threshold_ :
  JoinedOrSeparate<["-"], "fxray-instruction-threshold">,
  Group<f_Group>, Flags<[CC1Option]>;

def fxray_always_instrument :
  JoinedOrSeparate<["-"], "fxray-always-instrument=">,
  Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"DEPRECATED: Filename defining the whitelist for imbuing the 'always instrument' XRay attribute.">;
def fxray_never_instrument :
  JoinedOrSeparate<["-"], "fxray-never-instrument=">,
  Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"DEPRECATED: Filename defining the whitelist for imbuing the 'never instrument' XRay attribute.">;
def fxray_attr_list :
  JoinedOrSeparate<["-"], "fxray-attr-list=">,
  Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Filename defining the list of functions/types for imbuing XRay attributes.">;
def fxray_modes :
  JoinedOrSeparate<["-"], "fxray-modes=">,
  Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"List of modes to link in by default into XRay instrumented binaries.">;

def fxray_always_emit_customevents : Flag<["-"], "fxray-always-emit-customevents">, Group<f_Group>,
  Flags<[CC1Option]>,
  HelpText<"Determine whether to always emit __xray_customevent(...) calls even if the function it appears in is not always instrumented.">;
def fnoxray_always_emit_customevents : Flag<["-"], "fno-xray-always-emit-customevents">, Group<f_Group>,
  Flags<[CC1Option]>;

def fxray_always_emit_typedevents : Flag<["-"], "fxray-always-emit-typedevents">, Group<f_Group>,
  Flags<[CC1Option]>,
  HelpText<"Determine whether to always emit __xray_typedevent(...) calls even if the function it appears in is not always instrumented.">;
def fnoxray_always_emit_typedevents : Flag<["-"], "fno-xray-always-emit-typedevents">, Group<f_Group>,
  Flags<[CC1Option]>;

def fxray_link_deps : Flag<["-"], "fxray-link-deps">, Group<f_Group>,
  Flags<[CC1Option]>,
  HelpText<"Tells clang to add the link dependencies for XRay.">;
def fnoxray_link_deps : Flag<["-"], "fnoxray-link-deps">, Group<f_Group>,
  Flags<[CC1Option]>;

def fxray_instrumentation_bundle :
  JoinedOrSeparate<["-"], "fxray-instrumentation-bundle=">,
  Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Select which XRay instrumentation points to emit. Options: all, none, function, custom. Default is 'all'.">;

def ffine_grained_bitfield_accesses : Flag<["-"],
  "ffine-grained-bitfield-accesses">, Group<f_clang_Group>, Flags<[CC1Option]>,
  HelpText<"Use separate accesses for consecutive bitfield runs with legal widths and alignments.">;
def fno_fine_grained_bitfield_accesses : Flag<["-"],
  "fno-fine-grained-bitfield-accesses">, Group<f_clang_Group>, Flags<[CC1Option]>,
  HelpText<"Use large-integer access for consecutive bitfield runs.">;

def flat__namespace : Flag<["-"], "flat_namespace">;
def flax_vector_conversions : Flag<["-"], "flax-vector-conversions">, Group<f_Group>;
def flimited_precision_EQ : Joined<["-"], "flimited-precision=">, Group<f_Group>;
def flto_EQ : Joined<["-"], "flto=">, Flags<[CoreOption, CC1Option]>, Group<f_Group>,
  HelpText<"Set LTO mode to either 'full' or 'thin'">, Values<"thin,full">;
def flto : Flag<["-"], "flto">, Flags<[CoreOption, CC1Option]>, Group<f_Group>,
  HelpText<"Enable LTO in 'full' mode">;
def fno_lto : Flag<["-"], "fno-lto">, Group<f_Group>,
  HelpText<"Disable LTO mode (default)">;
def flto_jobs_EQ : Joined<["-"], "flto-jobs=">,
  Flags<[CC1Option]>, Group<f_Group>,
  HelpText<"Controls the backend parallelism of -flto=thin (default "
           "of 0 means the number of threads will be derived from "
           "the number of CPUs detected)">;
def fthinlto_index_EQ : Joined<["-"], "fthinlto-index=">,
  Flags<[CC1Option]>, Group<f_Group>,
  HelpText<"Perform ThinLTO importing using provided function summary index">;
def fmacro_backtrace_limit_EQ : Joined<["-"], "fmacro-backtrace-limit=">,
                                Group<f_Group>, Flags<[DriverOption, CoreOption]>;
def fmerge_all_constants : Flag<["-"], "fmerge-all-constants">, Group<f_Group>,
  Flags<[CC1Option, CoreOption]>, HelpText<"Allow merging of constants">;
def fmessage_length_EQ : Joined<["-"], "fmessage-length=">, Group<f_Group>;
def fms_extensions : Flag<["-"], "fms-extensions">, Group<f_Group>, Flags<[CC1Option, CoreOption]>,
  HelpText<"Accept some non-standard constructs supported by the Microsoft compiler">;
def fms_compatibility : Flag<["-"], "fms-compatibility">, Group<f_Group>, Flags<[CC1Option, CoreOption]>,
  HelpText<"Enable full Microsoft Visual C++ compatibility">;
def fms_volatile : Joined<["-"], "fms-volatile">, Group<f_Group>, Flags<[CC1Option]>;
def fmsc_version : Joined<["-"], "fmsc-version=">, Group<f_Group>, Flags<[DriverOption, CoreOption]>,
  HelpText<"Microsoft compiler version number to report in _MSC_VER (0 = don't define it (default))">;
def fms_compatibility_version
    : Joined<["-"], "fms-compatibility-version=">,
      Group<f_Group>,
      Flags<[ CC1Option, CoreOption ]>,
      HelpText<"Dot-separated value representing the Microsoft compiler "
               "version number to report in _MSC_VER (0 = don't define it "
               "(default))">;
def fdelayed_template_parsing : Flag<["-"], "fdelayed-template-parsing">, Group<f_Group>,
  HelpText<"Parse templated function definitions at the end of the "
           "translation unit">,  Flags<[CC1Option, CoreOption]>;
def fms_memptr_rep_EQ : Joined<["-"], "fms-memptr-rep=">, Group<f_Group>, Flags<[CC1Option]>;
def fmodules_cache_path : Joined<["-"], "fmodules-cache-path=">, Group<i_Group>,
  Flags<[DriverOption, CC1Option]>, MetaVarName<"<directory>">,
  HelpText<"Specify the module cache path">;
def fmodules_user_build_path : Separate<["-"], "fmodules-user-build-path">, Group<i_Group>,
  Flags<[DriverOption, CC1Option]>, MetaVarName<"<directory>">,
  HelpText<"Specify the module user build path">;
def fprebuilt_module_path : Joined<["-"], "fprebuilt-module-path=">, Group<i_Group>,
  Flags<[DriverOption, CC1Option]>, MetaVarName<"<directory>">,
  HelpText<"Specify the prebuilt module path">;
def fmodules_prune_interval : Joined<["-"], "fmodules-prune-interval=">, Group<i_Group>,
  Flags<[CC1Option]>, MetaVarName<"<seconds>">,
  HelpText<"Specify the interval (in seconds) between attempts to prune the module cache">;
def fmodules_prune_after : Joined<["-"], "fmodules-prune-after=">, Group<i_Group>,
  Flags<[CC1Option]>, MetaVarName<"<seconds>">,
  HelpText<"Specify the interval (in seconds) after which a module file will be considered unused">;
def fmodules_search_all : Flag <["-"], "fmodules-search-all">, Group<f_Group>,
  Flags<[DriverOption, CC1Option]>,
  HelpText<"Search even non-imported modules to resolve references">;
def fbuild_session_timestamp : Joined<["-"], "fbuild-session-timestamp=">,
  Group<i_Group>, Flags<[CC1Option]>, MetaVarName<"<time since Epoch in seconds>">,
  HelpText<"Time when the current build session started">;
def fbuild_session_file : Joined<["-"], "fbuild-session-file=">,
  Group<i_Group>, MetaVarName<"<file>">,
  HelpText<"Use the last modification time of <file> as the build session timestamp">;
def fmodules_validate_once_per_build_session : Flag<["-"], "fmodules-validate-once-per-build-session">,
  Group<i_Group>, Flags<[CC1Option]>,
  HelpText<"Don't verify input files for the modules if the module has been "
           "successfully validated or loaded during this build session">;
def fmodules_disable_diagnostic_validation : Flag<["-"], "fmodules-disable-diagnostic-validation">,
  Group<i_Group>, Flags<[CC1Option]>,
  HelpText<"Disable validation of the diagnostic options when loading the module">;
def fmodules_validate_system_headers : Flag<["-"], "fmodules-validate-system-headers">,
  Group<i_Group>, Flags<[CC1Option]>,
  HelpText<"Validate the system headers that a module depends on when loading the module">;
def fno_modules_validate_system_headers : Flag<["-"], "fno-modules-validate-system-headers">,
  Group<i_Group>, Flags<[DriverOption]>;
def fmodules : Flag <["-"], "fmodules">, Group<f_Group>,
  Flags<[DriverOption, CC1Option]>,
  HelpText<"Enable the 'modules' language feature">;
def fimplicit_module_maps : Flag <["-"], "fimplicit-module-maps">, Group<f_Group>,
  Flags<[DriverOption, CC1Option]>,
  HelpText<"Implicitly search the file system for module map files.">;
def fmodules_ts : Flag <["-"], "fmodules-ts">, Group<f_Group>,
  Flags<[CC1Option]>, HelpText<"Enable support for the C++ Modules TS">;
def fmodule_maps : Flag <["-"], "fmodule-maps">, Alias<fimplicit_module_maps>;
def fmodule_name_EQ : Joined<["-"], "fmodule-name=">, Group<f_Group>,
  Flags<[DriverOption,CC1Option]>, MetaVarName<"<name>">,
  HelpText<"Specify the name of the module to build">;
def fmodule_name : Separate<["-"], "fmodule-name">, Alias<fmodule_name_EQ>;
def fmodule_implementation_of : Separate<["-"], "fmodule-implementation-of">,
  Flags<[CC1Option]>, Alias<fmodule_name_EQ>;
def fmodule_map_file : Joined<["-"], "fmodule-map-file=">,
  Group<f_Group>, Flags<[DriverOption,CC1Option]>, MetaVarName<"<file>">,
  HelpText<"Load this module map file">;
def fmodule_file : Joined<["-"], "fmodule-file=">,
  Group<i_Group>, Flags<[DriverOption,CC1Option]>, MetaVarName<"[<name>=]<file>">,
  HelpText<"Specify the mapping of module name to precompiled module file, or load a module file if name is omitted.">;
def fmodules_ignore_macro : Joined<["-"], "fmodules-ignore-macro=">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Ignore the definition of the given macro when building and loading modules">;
def fmodules_decluse : Flag <["-"], "fmodules-decluse">, Group<f_Group>,
  Flags<[DriverOption,CC1Option]>,
  HelpText<"Require declaration of modules used within a module">;
def fmodules_strict_decluse : Flag <["-"], "fmodules-strict-decluse">, Group<f_Group>,
  Flags<[DriverOption,CC1Option]>,
  HelpText<"Like -fmodules-decluse but requires all headers to be in modules">;
def fno_modules_search_all : Flag <["-"], "fno-modules-search-all">, Group<f_Group>,
  Flags<[DriverOption, CC1Option]>;
def fno_implicit_modules :
  Flag <["-"], "fno-implicit-modules">,
  Group<f_Group>, Flags<[DriverOption, CC1Option]>;
def fretain_comments_from_system_headers : Flag<["-"], "fretain-comments-from-system-headers">, Group<f_Group>, Flags<[CC1Option]>;

def fmudflapth : Flag<["-"], "fmudflapth">, Group<f_Group>;
def fmudflap : Flag<["-"], "fmudflap">, Group<f_Group>;
def fnested_functions : Flag<["-"], "fnested-functions">, Group<f_Group>;
def fnext_runtime : Flag<["-"], "fnext-runtime">, Group<f_Group>;
def fno_access_control : Flag<["-"], "fno-access-control">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Disable C++ access control">;
def fno_apple_pragma_pack : Flag<["-"], "fno-apple-pragma-pack">, Group<f_Group>;
def fno_asm : Flag<["-"], "fno-asm">, Group<f_Group>;
def fno_asynchronous_unwind_tables : Flag<["-"], "fno-asynchronous-unwind-tables">, Group<f_Group>;
def fno_assume_sane_operator_new : Flag<["-"], "fno-assume-sane-operator-new">, Group<f_Group>,
  HelpText<"Don't assume that C++'s global operator new can't alias any pointer">,
  Flags<[CC1Option]>;
def fno_blocks : Flag<["-"], "fno-blocks">, Group<f_Group>, Flags<[CoreOption]>;
def fno_borland_extensions : Flag<["-"], "fno-borland-extensions">, Group<f_Group>;
def fno_builtin : Flag<["-"], "fno-builtin">, Group<f_Group>, Flags<[CC1Option, CoreOption]>,
  HelpText<"Disable implicit builtin knowledge of functions">;
def fno_builtin_ : Joined<["-"], "fno-builtin-">, Group<f_Group>, Flags<[CC1Option, CoreOption]>,
  HelpText<"Disable implicit builtin knowledge of a specific function">;
def fno_caret_diagnostics : Flag<["-"], "fno-caret-diagnostics">, Group<f_Group>,
 Flags<[CC1Option]>;
def fno_color_diagnostics : Flag<["-"], "fno-color-diagnostics">, Group<f_Group>,
  Flags<[CoreOption, CC1Option]>;
def fno_diagnostics_color : Flag<["-"], "fno-diagnostics-color">, Group<f_Group>,
  Flags<[CoreOption, DriverOption]>;
def fno_common : Flag<["-"], "fno-common">, Group<f_Group>, Flags<[CC1Option]>,
    HelpText<"Compile common globals like normal definitions">;
def fno_constant_cfstrings : Flag<["-"], "fno-constant-cfstrings">, Group<f_Group>,
  Flags<[CC1Option]>,
  HelpText<"Disable creation of CodeFoundation-type constant strings">;
def fno_cxx_exceptions: Flag<["-"], "fno-cxx-exceptions">, Group<f_Group>;
def fno_cxx_modules : Flag <["-"], "fno-cxx-modules">, Group<f_Group>,
  Flags<[DriverOption]>;
def fno_diagnostics_fixit_info : Flag<["-"], "fno-diagnostics-fixit-info">, Group<f_Group>,
  Flags<[CC1Option]>, HelpText<"Do not include fixit information in diagnostics">;
def fno_diagnostics_show_hotness : Flag<["-"], "fno-diagnostics-show-hotness">, Group<f_Group>;
def fno_diagnostics_show_option : Flag<["-"], "fno-diagnostics-show-option">, Group<f_Group>;
def fno_diagnostics_show_note_include_stack : Flag<["-"], "fno-diagnostics-show-note-include-stack">,
    Flags<[CC1Option]>, Group<f_Group>;
def fdigraphs : Flag<["-"], "fdigraphs">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Enable alternative token representations '<:', ':>', '<%', '%>', '%:', '%:%:' (default)">;
def fno_digraphs : Flag<["-"], "fno-digraphs">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Disallow alternative token representations '<:', ':>', '<%', '%>', '%:', '%:%:'">;
def fno_declspec : Flag<["-"], "fno-declspec">, Group<f_clang_Group>,
  HelpText<"Disallow __declspec as a keyword">, Flags<[CC1Option]>;
def fno_dollars_in_identifiers : Flag<["-"], "fno-dollars-in-identifiers">, Group<f_Group>,
  HelpText<"Disallow '$' in identifiers">, Flags<[CC1Option]>;
def fno_elide_constructors : Flag<["-"], "fno-elide-constructors">, Group<f_Group>,
  HelpText<"Disable C++ copy constructor elision">, Flags<[CC1Option]>;
def fno_eliminate_unused_debug_symbols : Flag<["-"], "fno-eliminate-unused-debug-symbols">, Group<f_Group>;
def fno_exceptions : Flag<["-"], "fno-exceptions">, Group<f_Group>;
def fno_gnu_keywords : Flag<["-"], "fno-gnu-keywords">, Group<f_Group>, Flags<[CC1Option]>;
def fno_inline_functions : Flag<["-"], "fno-inline-functions">, Group<f_clang_Group>, Flags<[CC1Option]>;
def fno_inline : Flag<["-"], "fno-inline">, Group<f_clang_Group>, Flags<[CC1Option]>;
def fno_experimental_isel : Flag<["-"], "fno-experimental-isel">, Group<f_clang_Group>,
  HelpText<"Disables the experimental global instruction selector">;
def fno_experimental_new_pass_manager : Flag<["-"], "fno-experimental-new-pass-manager">,
  Group<f_clang_Group>, Flags<[CC1Option]>,
  HelpText<"Disables an experimental new pass manager in LLVM.">;
def fveclib : Joined<["-"], "fveclib=">, Group<f_Group>, Flags<[CC1Option]>,
    HelpText<"Use the given vector functions library">, Values<"Accelerate,SVML,none">;
def fno_lax_vector_conversions : Flag<["-"], "fno-lax-vector-conversions">, Group<f_Group>,
  HelpText<"Disallow implicit conversions between vectors with a different number of elements or different element types">, Flags<[CC1Option]>;
def fno_merge_all_constants : Flag<["-"], "fno-merge-all-constants">, Group<f_Group>,
  HelpText<"Disallow merging of constants">;
def fno_modules : Flag <["-"], "fno-modules">, Group<f_Group>,
  Flags<[DriverOption]>;
def fno_implicit_module_maps : Flag <["-"], "fno-implicit-module-maps">, Group<f_Group>,
  Flags<[DriverOption]>;
def fno_module_maps : Flag <["-"], "fno-module-maps">, Alias<fno_implicit_module_maps>;
def fno_modules_decluse : Flag <["-"], "fno-modules-decluse">, Group<f_Group>,
  Flags<[DriverOption]>;
def fno_modules_strict_decluse : Flag <["-"], "fno-strict-modules-decluse">, Group<f_Group>,
  Flags<[DriverOption]>;
def fimplicit_modules : Flag <["-"], "fimplicit-modules">, Group<f_Group>,
  Flags<[DriverOption]>;
def fmodule_file_deps : Flag <["-"], "fmodule-file-deps">, Group<f_Group>,
  Flags<[DriverOption]>;
def fno_module_file_deps : Flag <["-"], "fno-module-file-deps">, Group<f_Group>,
  Flags<[DriverOption]>;
def fno_ms_extensions : Flag<["-"], "fno-ms-extensions">, Group<f_Group>,
  Flags<[CoreOption]>;
def fno_ms_compatibility : Flag<["-"], "fno-ms-compatibility">, Group<f_Group>,
  Flags<[CoreOption]>;
def fno_delayed_template_parsing : Flag<["-"], "fno-delayed-template-parsing">, Group<f_Group>,
  HelpText<"Disable delayed template parsing">,
  Flags<[DriverOption, CoreOption]>;
def fno_objc_exceptions: Flag<["-"], "fno-objc-exceptions">, Group<f_Group>;
def fno_objc_legacy_dispatch : Flag<["-"], "fno-objc-legacy-dispatch">, Group<f_Group>;
def fno_objc_weak : Flag<["-"], "fno-objc-weak">, Group<f_Group>, Flags<[CC1Option]>;
def fno_omit_frame_pointer : Flag<["-"], "fno-omit-frame-pointer">, Group<f_Group>;
def fno_operator_names : Flag<["-"], "fno-operator-names">, Group<f_Group>,
  HelpText<"Do not treat C++ operator name keywords as synonyms for operators">,
  Flags<[CC1Option]>;
def fno_pascal_strings : Flag<["-"], "fno-pascal-strings">, Group<f_Group>;
def fno_rtti : Flag<["-"], "fno-rtti">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Disable generation of rtti information">;
def fno_rtti_data : Flag<["-"], "fno-rtti-data">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Control emission of RTTI data">;
def fno_short_enums : Flag<["-"], "fno-short-enums">, Group<f_Group>;
def fno_show_column : Flag<["-"], "fno-show-column">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Do not include column number on diagnostics">;
def fno_show_source_location : Flag<["-"], "fno-show-source-location">, Group<f_Group>,
  Flags<[CC1Option]>, HelpText<"Do not include source location information with diagnostics">;
def fdiagnostics_absolute_paths : Flag<["-"], "fdiagnostics-absolute-paths">, Group<f_Group>,
  Flags<[CC1Option, CoreOption]>, HelpText<"Print absolute paths in diagnostics">;
def fno_spell_checking : Flag<["-"], "fno-spell-checking">, Group<f_Group>,
  Flags<[CC1Option]>, HelpText<"Disable spell-checking">;
def fno_stack_protector : Flag<["-"], "fno-stack-protector">, Group<f_Group>,
  HelpText<"Disable the use of stack protectors">;
def fno_strict_aliasing : Flag<["-"], "fno-strict-aliasing">, Group<f_Group>,
  Flags<[DriverOption, CoreOption]>;
def fstruct_path_tbaa : Flag<["-"], "fstruct-path-tbaa">, Group<f_Group>;
def fno_struct_path_tbaa : Flag<["-"], "fno-struct-path-tbaa">, Group<f_Group>;
def fno_strict_enums : Flag<["-"], "fno-strict-enums">, Group<f_Group>;
def fno_strict_vtable_pointers: Flag<["-"], "fno-strict-vtable-pointers">,
  Group<f_Group>;
def fno_strict_overflow : Flag<["-"], "fno-strict-overflow">, Group<f_Group>;
def fno_threadsafe_statics : Flag<["-"], "fno-threadsafe-statics">, Group<f_Group>,
  Flags<[CC1Option]>, HelpText<"Do not emit code to make initialization of local statics thread safe">;
def fno_use_cxa_atexit : Flag<["-"], "fno-use-cxa-atexit">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Don't use __cxa_atexit for calling destructors">;
def fno_register_global_dtors_with_atexit : Flag<["-"], "fno-register-global-dtors-with-atexit">, Group<f_Group>,
  HelpText<"Don't use atexit or __cxa_atexit to register global destructors">;
def fno_use_init_array : Flag<["-"], "fno-use-init-array">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Don't use .init_array instead of .ctors">;
def fno_unit_at_a_time : Flag<["-"], "fno-unit-at-a-time">, Group<f_Group>;
def fno_unwind_tables : Flag<["-"], "fno-unwind-tables">, Group<f_Group>;
def fno_verbose_asm : Flag<["-"], "fno-verbose-asm">, Group<f_Group>;
def fno_working_directory : Flag<["-"], "fno-working-directory">, Group<f_Group>;
def fno_wrapv : Flag<["-"], "fno-wrapv">, Group<f_Group>;
def fno_zero_initialized_in_bss : Flag<["-"], "fno-zero-initialized-in-bss">, Group<f_Group>;
def fobjc_arc : Flag<["-"], "fobjc-arc">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Synthesize retain and release calls for Objective-C pointers">;
def fno_objc_arc : Flag<["-"], "fno-objc-arc">, Group<f_Group>;
def fobjc_convert_messages_to_runtime_calls :
  Flag<["-"], "fobjc-convert-messages-to-runtime-calls">, Group<f_Group>;
def fno_objc_convert_messages_to_runtime_calls :
  Flag<["-"], "fno-objc-convert-messages-to-runtime-calls">, Group<f_Group>, Flags<[CC1Option]>;
def fobjc_arc_exceptions : Flag<["-"], "fobjc-arc-exceptions">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Use EH-safe code when synthesizing retains and releases in -fobjc-arc">;
def fno_objc_arc_exceptions : Flag<["-"], "fno-objc-arc-exceptions">, Group<f_Group>;
def fobjc_atdefs : Flag<["-"], "fobjc-atdefs">, Group<clang_ignored_f_Group>;
def fobjc_call_cxx_cdtors : Flag<["-"], "fobjc-call-cxx-cdtors">, Group<clang_ignored_f_Group>;
def fobjc_exceptions: Flag<["-"], "fobjc-exceptions">, Group<f_Group>,
  HelpText<"Enable Objective-C exceptions">, Flags<[CC1Option]>;
def fapplication_extension : Flag<["-"], "fapplication-extension">,
  Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Restrict code to those available for App Extensions">;
def fno_application_extension : Flag<["-"], "fno-application-extension">,
  Group<f_Group>;
def frelaxed_template_template_args : Flag<["-"], "frelaxed-template-template-args">,
  Flags<[CC1Option]>, HelpText<"Enable C++17 relaxed template template argument matching">,
  Group<f_Group>;
def fno_relaxed_template_template_args : Flag<["-"], "fno-relaxed-template-template-args">,
  Group<f_Group>;
def fsized_deallocation : Flag<["-"], "fsized-deallocation">, Flags<[CC1Option]>,
  HelpText<"Enable C++14 sized global deallocation functions">, Group<f_Group>;
def fno_sized_deallocation: Flag<["-"], "fno-sized-deallocation">, Group<f_Group>;
def faligned_allocation : Flag<["-"], "faligned-allocation">, Flags<[CC1Option]>,
  HelpText<"Enable C++17 aligned allocation functions">, Group<f_Group>;
def fno_aligned_allocation: Flag<["-"], "fno-aligned-allocation">,
  Group<f_Group>, Flags<[CC1Option]>;
def fnew_alignment_EQ : Joined<["-"], "fnew-alignment=">,
  HelpText<"Specifies the largest alignment guaranteed by '::operator new(size_t)'">,
  MetaVarName<"<align>">, Group<f_Group>, Flags<[CC1Option]>;
def : Separate<["-"], "fnew-alignment">, Alias<fnew_alignment_EQ>;
def : Flag<["-"], "faligned-new">, Alias<faligned_allocation>;
def : Flag<["-"], "fno-aligned-new">, Alias<fno_aligned_allocation>;
def faligned_new_EQ : Joined<["-"], "faligned-new=">;

def fobjc_legacy_dispatch : Flag<["-"], "fobjc-legacy-dispatch">, Group<f_Group>;
def fobjc_new_property : Flag<["-"], "fobjc-new-property">, Group<clang_ignored_f_Group>;
def fobjc_infer_related_result_type : Flag<["-"], "fobjc-infer-related-result-type">,
                                      Group<f_Group>;
def fno_objc_infer_related_result_type : Flag<["-"],
  "fno-objc-infer-related-result-type">, Group<f_Group>,
  HelpText<
    "do not infer Objective-C related result type based on method family">,
  Flags<[CC1Option]>;
def fobjc_link_runtime: Flag<["-"], "fobjc-link-runtime">, Group<f_Group>;
def fobjc_weak : Flag<["-"], "fobjc-weak">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Enable ARC-style weak references in Objective-C">;

// Objective-C ABI options.
def fobjc_runtime_EQ : Joined<["-"], "fobjc-runtime=">, Group<f_Group>, Flags<[CC1Option, CoreOption]>,
  HelpText<"Specify the target Objective-C runtime kind and version">;
def fobjc_abi_version_EQ : Joined<["-"], "fobjc-abi-version=">, Group<f_Group>;
def fobjc_nonfragile_abi_version_EQ : Joined<["-"], "fobjc-nonfragile-abi-version=">, Group<f_Group>;
def fobjc_nonfragile_abi : Flag<["-"], "fobjc-nonfragile-abi">, Group<f_Group>;
def fno_objc_nonfragile_abi : Flag<["-"], "fno-objc-nonfragile-abi">, Group<f_Group>;

def fobjc_sender_dependent_dispatch : Flag<["-"], "fobjc-sender-dependent-dispatch">, Group<f_Group>;
def fomit_frame_pointer : Flag<["-"], "fomit-frame-pointer">, Group<f_Group>;
def fopenmp : Flag<["-"], "fopenmp">, Group<f_Group>, Flags<[CC1Option, NoArgumentUnused]>,
  HelpText<"Parse OpenMP pragmas and generate parallel code.">;
def fno_openmp : Flag<["-"], "fno-openmp">, Group<f_Group>, Flags<[NoArgumentUnused]>;
def fopenmp_version_EQ : Joined<["-"], "fopenmp-version=">, Group<f_Group>, Flags<[CC1Option, NoArgumentUnused]>;
def fopenmp_EQ : Joined<["-"], "fopenmp=">, Group<f_Group>;
def fopenmp_use_tls : Flag<["-"], "fopenmp-use-tls">, Group<f_Group>,
  Flags<[NoArgumentUnused, HelpHidden]>;
def fnoopenmp_use_tls : Flag<["-"], "fnoopenmp-use-tls">, Group<f_Group>,
  Flags<[CC1Option, NoArgumentUnused, HelpHidden]>;
def fopenmp_targets_EQ : CommaJoined<["-"], "fopenmp-targets=">, Flags<[DriverOption, CC1Option]>,
  HelpText<"Specify comma-separated list of triples OpenMP offloading targets to be supported">;
def fopenmp_dump_offload_linker_script : Flag<["-"], "fopenmp-dump-offload-linker-script">,
  Group<f_Group>, Flags<[NoArgumentUnused, HelpHidden]>;
def fopenmp_relocatable_target : Flag<["-"], "fopenmp-relocatable-target">,
  Group<f_Group>, Flags<[CC1Option, NoArgumentUnused, HelpHidden]>;
def fnoopenmp_relocatable_target : Flag<["-"], "fnoopenmp-relocatable-target">,
  Group<f_Group>, Flags<[CC1Option, NoArgumentUnused, HelpHidden]>;
def fopenmp_simd : Flag<["-"], "fopenmp-simd">, Group<f_Group>, Flags<[CC1Option, NoArgumentUnused]>,
  HelpText<"Emit OpenMP code only for SIMD-based constructs.">;
def fno_openmp_simd : Flag<["-"], "fno-openmp-simd">, Group<f_Group>, Flags<[CC1Option, NoArgumentUnused]>;
def fopenmp_cuda_mode : Flag<["-"], "fopenmp-cuda-mode">, Group<f_Group>,
  Flags<[CC1Option, NoArgumentUnused, HelpHidden]>;
def fno_openmp_cuda_mode : Flag<["-"], "fno-openmp-cuda-mode">, Group<f_Group>,
  Flags<[NoArgumentUnused, HelpHidden]>;
def fopenmp_cuda_force_full_runtime : Flag<["-"], "fopenmp-cuda-force-full-runtime">, Group<f_Group>,
  Flags<[CC1Option, NoArgumentUnused, HelpHidden]>;
def fno_openmp_cuda_force_full_runtime : Flag<["-"], "fno-openmp-cuda-force-full-runtime">, Group<f_Group>,
  Flags<[NoArgumentUnused, HelpHidden]>;
def fopenmp_cuda_number_of_sm_EQ : Joined<["-"], "fopenmp-cuda-number-of-sm=">, Group<f_Group>,
  Flags<[CC1Option, NoArgumentUnused, HelpHidden]>;
def fopenmp_cuda_blocks_per_sm_EQ : Joined<["-"], "fopenmp-cuda-blocks-per-sm=">, Group<f_Group>,
  Flags<[CC1Option, NoArgumentUnused, HelpHidden]>;
def fopenmp_optimistic_collapse : Flag<["-"], "fopenmp-optimistic-collapse">, Group<f_Group>,
  Flags<[CC1Option, NoArgumentUnused, HelpHidden]>;
def fno_openmp_optimistic_collapse : Flag<["-"], "fno-openmp-optimistic-collapse">, Group<f_Group>,
  Flags<[NoArgumentUnused, HelpHidden]>;
def fno_optimize_sibling_calls : Flag<["-"], "fno-optimize-sibling-calls">, Group<f_Group>;
def foptimize_sibling_calls : Flag<["-"], "foptimize-sibling-calls">, Group<f_Group>;
def fno_escaping_block_tail_calls : Flag<["-"], "fno-escaping-block-tail-calls">, Group<f_Group>, Flags<[CC1Option]>;
def fescaping_block_tail_calls : Flag<["-"], "fescaping-block-tail-calls">, Group<f_Group>;
def force__cpusubtype__ALL : Flag<["-"], "force_cpusubtype_ALL">;
def force__flat__namespace : Flag<["-"], "force_flat_namespace">;
def force__load : Separate<["-"], "force_load">;
def force_addr : Joined<["-"], "fforce-addr">, Group<clang_ignored_f_Group>;
def foutput_class_dir_EQ : Joined<["-"], "foutput-class-dir=">, Group<f_Group>;
def fpack_struct : Flag<["-"], "fpack-struct">, Group<f_Group>;
def fno_pack_struct : Flag<["-"], "fno-pack-struct">, Group<f_Group>;
def fpack_struct_EQ : Joined<["-"], "fpack-struct=">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Specify the default maximum struct packing alignment">;
def fmax_type_align_EQ : Joined<["-"], "fmax-type-align=">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Specify the maximum alignment to enforce on pointers lacking an explicit alignment">;
def fno_max_type_align : Flag<["-"], "fno-max-type-align">, Group<f_Group>;
def fpascal_strings : Flag<["-"], "fpascal-strings">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Recognize and construct Pascal-style string literals">;
def fpcc_struct_return : Flag<["-"], "fpcc-struct-return">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Override the default ABI to return all structs on the stack">;
def fpch_preprocess : Flag<["-"], "fpch-preprocess">, Group<f_Group>;
def fpic : Flag<["-"], "fpic">, Group<f_Group>;
def fno_pic : Flag<["-"], "fno-pic">, Group<f_Group>;
def fpie : Flag<["-"], "fpie">, Group<f_Group>;
def fno_pie : Flag<["-"], "fno-pie">, Group<f_Group>;
def fplt : Flag<["-"], "fplt">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Use the PLT to make function calls">;
def fno_plt : Flag<["-"], "fno-plt">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Do not use the PLT to make function calls">;
def fropi : Flag<["-"], "fropi">, Group<f_Group>;
def fno_ropi : Flag<["-"], "fno-ropi">, Group<f_Group>;
def frwpi : Flag<["-"], "frwpi">, Group<f_Group>;
def fno_rwpi : Flag<["-"], "fno-rwpi">, Group<f_Group>;
def fplugin_EQ : Joined<["-"], "fplugin=">, Group<f_Group>, Flags<[DriverOption]>, MetaVarName<"<dsopath>">,
  HelpText<"Load the named plugin (dynamic shared object)">;
def fpreserve_as_comments : Flag<["-"], "fpreserve-as-comments">, Group<f_Group>;
def fno_preserve_as_comments : Flag<["-"], "fno-preserve-as-comments">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Do not preserve comments in inline assembly">;
def fprofile_arcs : Flag<["-"], "fprofile-arcs">, Group<f_Group>;
def fno_profile_arcs : Flag<["-"], "fno-profile-arcs">, Group<f_Group>;
def framework : Separate<["-"], "framework">, Flags<[LinkerInput]>;
def frandom_seed_EQ : Joined<["-"], "frandom-seed=">, Group<clang_ignored_f_Group>;
def freg_struct_return : Flag<["-"], "freg-struct-return">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Override the default ABI to return small structs in registers">;
def frtti : Flag<["-"], "frtti">, Group<f_Group>;
def : Flag<["-"], "fsched-interblock">, Group<clang_ignored_f_Group>;
def fshort_enums : Flag<["-"], "fshort-enums">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Allocate to an enum type only as many bytes as it needs for the declared range of possible values">;
def fchar8__t : Flag<["-"], "fchar8_t">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Enable C++ builtin type char8_t">;
def fno_char8__t : Flag<["-"], "fno-char8_t">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Disable C++ builtin type char8_t">;
def fshort_wchar : Flag<["-"], "fshort-wchar">, Group<f_Group>,
  HelpText<"Force wchar_t to be a short unsigned int">;
def fno_short_wchar : Flag<["-"], "fno-short-wchar">, Group<f_Group>,
  HelpText<"Force wchar_t to be an unsigned int">;
def fshow_overloads_EQ : Joined<["-"], "fshow-overloads=">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Which overload candidates to show when overload resolution fails: "
           "best|all; defaults to all">, Values<"best,all">;
def fshow_column : Flag<["-"], "fshow-column">, Group<f_Group>, Flags<[CC1Option]>;
def fshow_source_location : Flag<["-"], "fshow-source-location">, Group<f_Group>;
def fspell_checking : Flag<["-"], "fspell-checking">, Group<f_Group>;
def fspell_checking_limit_EQ : Joined<["-"], "fspell-checking-limit=">, Group<f_Group>;
def fsigned_bitfields : Flag<["-"], "fsigned-bitfields">, Group<f_Group>;
def fsigned_char : Flag<["-"], "fsigned-char">, Group<f_Group>;
def fno_signed_char : Flag<["-"], "fno-signed-char">, Group<f_Group>,
    Flags<[CC1Option]>, HelpText<"Char is unsigned">;
def fsplit_stack : Flag<["-"], "fsplit-stack">, Group<f_Group>;
def fstack_protector_all : Flag<["-"], "fstack-protector-all">, Group<f_Group>,
  HelpText<"Enable stack protectors for all functions">;
def fstack_protector_strong : Flag<["-"], "fstack-protector-strong">, Group<f_Group>,
  HelpText<"Enable stack protectors for some functions vulnerable to stack smashing. "
           "Compared to -fstack-protector, this uses a stronger heuristic "
           "that includes functions containing arrays of any size (and any type), "
           "as well as any calls to alloca or the taking of an address from a local variable">;
def fstack_protector : Flag<["-"], "fstack-protector">, Group<f_Group>,
  HelpText<"Enable stack protectors for some functions vulnerable to stack smashing. "
           "This uses a loose heuristic which considers functions vulnerable "
           "if they contain a char (or 8bit integer) array or constant sized calls to "
           "alloca, which are of greater size than ssp-buffer-size (default: 8 bytes). "
           "All variable sized calls to alloca are considered vulnerable">;
def ftrivial_auto_var_init : Joined<["-"], "ftrivial-auto-var-init=">, Group<f_Group>,
  Flags<[CC1Option]>, HelpText<"Initialize trivial automatic stack variables: uninitialized (default)"
  " | pattern">, Values<"uninitialized,pattern">;
def enable_trivial_var_init_zero : Joined<["-"], "enable-trivial-auto-var-init-zero-knowing-it-will-be-removed-from-clang">,
  Flags<[CC1Option]>,
  HelpText<"Trivial automatic variable initialization to zero is only here for benchmarks, it'll eventually be removed, and I'm OK with that because I'm only using it to benchmark">;
def fstandalone_debug : Flag<["-"], "fstandalone-debug">, Group<f_Group>, Flags<[CoreOption]>,
  HelpText<"Emit full debug info for all types used by the program">;
def fno_standalone_debug : Flag<["-"], "fno-standalone-debug">, Group<f_Group>, Flags<[CoreOption]>,
  HelpText<"Limit debug information produced to reduce size of debug binary">;
def flimit_debug_info : Flag<["-"], "flimit-debug-info">, Flags<[CoreOption]>, Alias<fno_standalone_debug>;
def fno_limit_debug_info : Flag<["-"], "fno-limit-debug-info">, Flags<[CoreOption]>, Alias<fstandalone_debug>;
def fdebug_macro : Flag<["-"], "fdebug-macro">, Group<f_Group>, Flags<[CoreOption]>,
  HelpText<"Emit macro debug information">;
def fno_debug_macro : Flag<["-"], "fno-debug-macro">, Group<f_Group>, Flags<[CoreOption]>,
  HelpText<"Do not emit macro debug information">;
def fstrict_aliasing : Flag<["-"], "fstrict-aliasing">, Group<f_Group>,
  Flags<[DriverOption, CoreOption]>;
def fstrict_enums : Flag<["-"], "fstrict-enums">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Enable optimizations based on the strict definition of an enum's "
           "value range">;
def fstrict_vtable_pointers: Flag<["-"], "fstrict-vtable-pointers">,
  Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Enable optimizations based on the strict rules for overwriting "
             "polymorphic C++ objects">;
def fstrict_overflow : Flag<["-"], "fstrict-overflow">, Group<f_Group>;
def fsyntax_only : Flag<["-"], "fsyntax-only">,
  Flags<[DriverOption,CoreOption,CC1Option]>, Group<Action_Group>;
def ftabstop_EQ : Joined<["-"], "ftabstop=">, Group<f_Group>;
def ftemplate_depth_EQ : Joined<["-"], "ftemplate-depth=">, Group<f_Group>;
def ftemplate_depth_ : Joined<["-"], "ftemplate-depth-">, Group<f_Group>;
def ftemplate_backtrace_limit_EQ : Joined<["-"], "ftemplate-backtrace-limit=">,
                                   Group<f_Group>;
def foperator_arrow_depth_EQ : Joined<["-"], "foperator-arrow-depth=">,
                               Group<f_Group>;

def fsave_optimization_record : Flag<["-"], "fsave-optimization-record">,
  Group<f_Group>, HelpText<"Generate a YAML optimization record file">;
def fno_save_optimization_record : Flag<["-"], "fno-save-optimization-record">,
  Group<f_Group>, Flags<[NoArgumentUnused]>;
def foptimization_record_file_EQ : Joined<["-"], "foptimization-record-file=">,
  Group<f_Group>,
  HelpText<"Specify the file name of any generated YAML optimization record">;

def ftest_coverage : Flag<["-"], "ftest-coverage">, Group<f_Group>;
def fvectorize : Flag<["-"], "fvectorize">, Group<f_Group>,
  HelpText<"Enable the loop vectorization passes">;
def fno_vectorize : Flag<["-"], "fno-vectorize">, Group<f_Group>;
def : Flag<["-"], "ftree-vectorize">, Alias<fvectorize>;
def : Flag<["-"], "fno-tree-vectorize">, Alias<fno_vectorize>;
def fslp_vectorize : Flag<["-"], "fslp-vectorize">, Group<f_Group>,
  HelpText<"Enable the superword-level parallelism vectorization passes">;
def fno_slp_vectorize : Flag<["-"], "fno-slp-vectorize">, Group<f_Group>;
def : Flag<["-"], "ftree-slp-vectorize">, Alias<fslp_vectorize>;
def : Flag<["-"], "fno-tree-slp-vectorize">, Alias<fno_slp_vectorize>;
def Wlarge_by_value_copy_def : Flag<["-"], "Wlarge-by-value-copy">,
  HelpText<"Warn if a function definition returns or accepts an object larger "
           "in bytes than a given value">, Flags<[HelpHidden]>;
def Wlarge_by_value_copy_EQ : Joined<["-"], "Wlarge-by-value-copy=">, Flags<[CC1Option]>;

// These "special" warning flags are effectively processed as f_Group flags by the driver:
// Just silence warnings about -Wlarger-than for now.
def Wlarger_than_EQ : Joined<["-"], "Wlarger-than=">, Group<clang_ignored_f_Group>;
def Wlarger_than_ : Joined<["-"], "Wlarger-than-">, Alias<Wlarger_than_EQ>;
def Wframe_larger_than_EQ : Joined<["-"], "Wframe-larger-than=">, Group<f_Group>, Flags<[DriverOption]>;

def : Flag<["-"], "fterminated-vtables">, Alias<fapple_kext>;
def fthreadsafe_statics : Flag<["-"], "fthreadsafe-statics">, Group<f_Group>;
def ftime_report : Flag<["-"], "ftime-report">, Group<f_Group>, Flags<[CC1Option]>;
def ftlsmodel_EQ : Joined<["-"], "ftls-model=">, Group<f_Group>, Flags<[CC1Option]>;
def ftrapv : Flag<["-"], "ftrapv">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Trap on integer overflow">;
def ftrapv_handler_EQ : Joined<["-"], "ftrapv-handler=">, Group<f_Group>,
  MetaVarName<"<function name>">,
  HelpText<"Specify the function to be called on overflow">;
def ftrapv_handler : Separate<["-"], "ftrapv-handler">, Group<f_Group>, Flags<[CC1Option]>;
def ftrap_function_EQ : Joined<["-"], "ftrap-function=">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Issue call to specified function rather than a trap instruction">;
def funit_at_a_time : Flag<["-"], "funit-at-a-time">, Group<f_Group>;
def funroll_loops : Flag<["-"], "funroll-loops">, Group<f_Group>,
  HelpText<"Turn on loop unroller">, Flags<[CC1Option]>;
def fno_unroll_loops : Flag<["-"], "fno-unroll-loops">, Group<f_Group>,
  HelpText<"Turn off loop unroller">, Flags<[CC1Option]>;
def freroll_loops : Flag<["-"], "freroll-loops">, Group<f_Group>,
  HelpText<"Turn on loop reroller">, Flags<[CC1Option]>;
def fno_reroll_loops : Flag<["-"], "fno-reroll-loops">, Group<f_Group>,
  HelpText<"Turn off loop reroller">;
def ftrigraphs : Flag<["-"], "ftrigraphs">, Group<f_Group>,
  HelpText<"Process trigraph sequences">, Flags<[CC1Option]>;
def fno_trigraphs : Flag<["-"], "fno-trigraphs">, Group<f_Group>,
  HelpText<"Do not process trigraph sequences">, Flags<[CC1Option]>;
def funsigned_bitfields : Flag<["-"], "funsigned-bitfields">, Group<f_Group>;
def funsigned_char : Flag<["-"], "funsigned-char">, Group<f_Group>;
def fno_unsigned_char : Flag<["-"], "fno-unsigned-char">;
def funwind_tables : Flag<["-"], "funwind-tables">, Group<f_Group>;
def fuse_cxa_atexit : Flag<["-"], "fuse-cxa-atexit">, Group<f_Group>;
def fregister_global_dtors_with_atexit : Flag<["-"], "fregister-global-dtors-with-atexit">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Use atexit or __cxa_atexit to register global destructors">;
def fuse_init_array : Flag<["-"], "fuse-init-array">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Use .init_array instead of .ctors">;
def fno_var_tracking : Flag<["-"], "fno-var-tracking">, Group<clang_ignored_f_Group>;
def fverbose_asm : Flag<["-"], "fverbose-asm">, Group<f_Group>;
def fvisibility_EQ : Joined<["-"], "fvisibility=">, Group<f_Group>,
  HelpText<"Set the default symbol visibility for all global declarations">, Values<"hidden,default">;
def fvisibility_inlines_hidden : Flag<["-"], "fvisibility-inlines-hidden">, Group<f_Group>,
  HelpText<"Give inline C++ member functions hidden visibility by default">,
  Flags<[CC1Option]>;
def fvisibility_ms_compat : Flag<["-"], "fvisibility-ms-compat">, Group<f_Group>,
  HelpText<"Give global types 'default' visibility and global functions and "
           "variables 'hidden' visibility by default">;
def fvisibility_global_new_delete_hidden : Flag<["-"], "fvisibility-global-new-delete-hidden">, Group<f_Group>,
  HelpText<"Give global C++ operator new and delete declarations hidden visibility">, Flags<[CC1Option]>;
def fwhole_program_vtables : Flag<["-"], "fwhole-program-vtables">, Group<f_Group>,
  Flags<[CoreOption, CC1Option]>,
  HelpText<"Enables whole-program vtable optimization. Requires -flto">;
def fno_whole_program_vtables : Flag<["-"], "fno-whole-program-vtables">, Group<f_Group>,
  Flags<[CoreOption]>;
def fsplit_lto_unit : Flag<["-"], "fsplit-lto-unit">, Group<f_Group>,
  Flags<[CoreOption, CC1Option]>,
  HelpText<"Enables splitting of the LTO unit.">;
def fno_split_lto_unit : Flag<["-"], "fno-split-lto-unit">, Group<f_Group>,
  Flags<[CoreOption]>;
def fforce_emit_vtables : Flag<["-"], "fforce-emit-vtables">, Group<f_Group>,
    Flags<[CC1Option]>,
    HelpText<"Emits more virtual tables to improve devirtualization">;
def fno_force_emit_vtables : Flag<["-"], "fno-force-emit-vtables">, Group<f_Group>,
  Flags<[CoreOption]>;
def fwrapv : Flag<["-"], "fwrapv">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Treat signed integer overflow as two's complement">;
def fwritable_strings : Flag<["-"], "fwritable-strings">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Store string literals as writable data">;
def fzero_initialized_in_bss : Flag<["-"], "fzero-initialized-in-bss">, Group<f_Group>;
def ffunction_sections : Flag<["-"], "ffunction-sections">, Group<f_Group>,
  Flags<[CC1Option]>,
  HelpText<"Place each function in its own section (ELF Only)">;
def fno_function_sections : Flag<["-"], "fno-function-sections">,
  Group<f_Group>, Flags<[CC1Option]>;
def fdata_sections : Flag <["-"], "fdata-sections">, Group<f_Group>,
 Flags<[CC1Option]>, HelpText<"Place each data in its own section (ELF Only)">;
def fno_data_sections : Flag <["-"], "fno-data-sections">, Group<f_Group>,
  Flags<[CC1Option]>;
def fstack_size_section : Flag<["-"], "fstack-size-section">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Emit section containing metadata on function stack sizes">;
def fno_stack_size_section : Flag<["-"], "fno-stack-size-section">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Don't emit section containing metadata on function stack sizes">;

def funique_section_names : Flag <["-"], "funique-section-names">,
  Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Use unique names for text and data sections (ELF Only)">;
def fno_unique_section_names : Flag <["-"], "fno-unique-section-names">,
  Group<f_Group>, Flags<[CC1Option]>;

def fstrict_return : Flag<["-"], "fstrict-return">, Group<f_Group>,
  Flags<[CC1Option]>,
  HelpText<"Always treat control flow paths that fall off the end of a "
           "non-void function as unreachable">;
def fno_strict_return : Flag<["-"], "fno-strict-return">, Group<f_Group>,
  Flags<[CC1Option]>;

def fallow_editor_placeholders : Flag<["-"], "fallow-editor-placeholders">,
  Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Treat editor placeholders as valid source code">;
def fno_allow_editor_placeholders : Flag<["-"],
  "fno-allow-editor-placeholders">, Group<f_Group>;

def fdebug_types_section: Flag <["-"], "fdebug-types-section">, Group<f_Group>,
  Flags<[CC1Option]>, HelpText<"Place debug types in their own section (ELF Only)">;
def fno_debug_types_section: Flag<["-"], "fno-debug-types-section">, Group<f_Group>,
  Flags<[CC1Option]>;
def fdebug_ranges_base_address: Flag <["-"], "fdebug-ranges-base-address">, Group<f_Group>,
  Flags<[CC1Option]>, HelpText<"Use DWARF base address selection entries in debug_ranges">;
def fno_debug_ranges_base_address: Flag <["-"], "fno-debug-ranges-base-address">, Group<f_Group>,
  Flags<[CC1Option]>;
def fsplit_dwarf_inlining: Flag <["-"], "fsplit-dwarf-inlining">, Group<f_Group>,
  Flags<[CC1Option]>, HelpText<"Provide minimal debug info in the object/executable to facilitate online symbolication/stack traces in the absence of .dwo/.dwp files when using Split DWARF">;
def fno_split_dwarf_inlining: Flag<["-"], "fno-split-dwarf-inlining">, Group<f_Group>,
  Flags<[CC1Option]>;
def fdebug_prefix_map_EQ
  : Joined<["-"], "fdebug-prefix-map=">, Group<f_Group>,
    Flags<[CC1Option,CC1AsOption]>,
    HelpText<"remap file source paths in debug info">;
def g_Flag : Flag<["-"], "g">, Group<g_Group>,
  HelpText<"Generate source-level debug information">;
def gline_tables_only : Flag<["-"], "gline-tables-only">, Group<gN_Group>,
  Flags<[CoreOption]>, HelpText<"Emit debug line number tables only">;
def gline_directives_only : Flag<["-"], "gline-directives-only">, Group<gN_Group>,
  Flags<[CoreOption]>, HelpText<"Emit debug line info directives only">;
def gmlt : Flag<["-"], "gmlt">, Alias<gline_tables_only>;
def g0 : Flag<["-"], "g0">, Group<gN_Group>;
def g1 : Flag<["-"], "g1">, Group<gN_Group>, Alias<gline_tables_only>;
def g2 : Flag<["-"], "g2">, Group<gN_Group>;
def g3 : Flag<["-"], "g3">, Group<gN_Group>;
def ggdb : Flag<["-"], "ggdb">, Group<gTune_Group>;
def ggdb0 : Flag<["-"], "ggdb0">, Group<ggdbN_Group>;
def ggdb1 : Flag<["-"], "ggdb1">, Group<ggdbN_Group>;
def ggdb2 : Flag<["-"], "ggdb2">, Group<ggdbN_Group>;
def ggdb3 : Flag<["-"], "ggdb3">, Group<ggdbN_Group>;
def glldb : Flag<["-"], "glldb">, Group<gTune_Group>;
def gsce : Flag<["-"], "gsce">, Group<gTune_Group>;
def gdwarf_2 : Flag<["-"], "gdwarf-2">, Group<g_Group>,
  HelpText<"Generate source-level debug information with dwarf version 2">;
def gdwarf_3 : Flag<["-"], "gdwarf-3">, Group<g_Group>,
  HelpText<"Generate source-level debug information with dwarf version 3">;
def gdwarf_4 : Flag<["-"], "gdwarf-4">, Group<g_Group>,
  HelpText<"Generate source-level debug information with dwarf version 4">;
def gdwarf_5 : Flag<["-"], "gdwarf-5">, Group<g_Group>,
  HelpText<"Generate source-level debug information with dwarf version 5">;

def gcodeview : Flag<["-"], "gcodeview">,
  HelpText<"Generate CodeView debug information">,
  Flags<[CC1Option, CC1AsOption, CoreOption]>;
def gcodeview_ghash : Flag<["-"], "gcodeview-ghash">,
  HelpText<"Emit type record hashes in a .debug$H section">,
  Flags<[CC1Option, CoreOption]>;
def gno_codeview_ghash : Flag<["-"], "gno-codeview-ghash">, Flags<[CoreOption]>;

// Equivalent to our default dwarf version. Forces usual dwarf emission when
// CodeView is enabled.
def gdwarf : Flag<["-"], "gdwarf">, Alias<gdwarf_4>, Flags<[CoreOption]>;

def gfull : Flag<["-"], "gfull">, Group<g_Group>;
def gused : Flag<["-"], "gused">, Group<g_Group>;
def gstabs : Joined<["-"], "gstabs">, Group<g_Group>, Flags<[Unsupported]>;
def gcoff : Joined<["-"], "gcoff">, Group<g_Group>, Flags<[Unsupported]>;
def gxcoff : Joined<["-"], "gxcoff">, Group<g_Group>, Flags<[Unsupported]>;
def gvms : Joined<["-"], "gvms">, Group<g_Group>, Flags<[Unsupported]>;
def gtoggle : Flag<["-"], "gtoggle">, Group<g_flags_Group>, Flags<[Unsupported]>;
def grecord_command_line : Flag<["-"], "grecord-command-line">,
  Group<g_flags_Group>;
def gno_record_command_line : Flag<["-"], "gno-record-command-line">,
  Group<g_flags_Group>;
def : Flag<["-"], "grecord-gcc-switches">, Alias<grecord_command_line>;
def : Flag<["-"], "gno-record-gcc-switches">, Alias<gno_record_command_line>;
def gstrict_dwarf : Flag<["-"], "gstrict-dwarf">, Group<g_flags_Group>;
def gno_strict_dwarf : Flag<["-"], "gno-strict-dwarf">, Group<g_flags_Group>;
def gcolumn_info : Flag<["-"], "gcolumn-info">, Group<g_flags_Group>, Flags<[CoreOption]>;
def gno_column_info : Flag<["-"], "gno-column-info">, Group<g_flags_Group>, Flags<[CoreOption]>;
def gsplit_dwarf : Flag<["-"], "gsplit-dwarf">, Group<g_flags_Group>;
def gsplit_dwarf_EQ : Joined<["-"], "gsplit-dwarf=">, Group<g_flags_Group>,
  HelpText<"Set DWARF fission mode to either 'split' or 'single'">,
  Values<"split,single">;
def ggnu_pubnames : Flag<["-"], "ggnu-pubnames">, Group<g_flags_Group>, Flags<[CC1Option]>;
def gno_gnu_pubnames : Flag<["-"], "gno-gnu-pubnames">, Group<g_flags_Group>, Flags<[CC1Option]>;
def gpubnames : Flag<["-"], "gpubnames">, Group<g_flags_Group>, Flags<[CC1Option]>;
def gno_pubnames : Flag<["-"], "gno-pubnames">, Group<g_flags_Group>, Flags<[CC1Option]>;
def gdwarf_aranges : Flag<["-"], "gdwarf-aranges">, Group<g_flags_Group>;
def gmodules : Flag <["-"], "gmodules">, Group<gN_Group>,
  HelpText<"Generate debug info with external references to clang modules"
           " or precompiled headers">;
def gz : Flag<["-"], "gz">, Group<g_flags_Group>,
    HelpText<"DWARF debug sections compression type">;
def gz_EQ : Joined<["-"], "gz=">, Group<g_flags_Group>,
    HelpText<"DWARF debug sections compression type">;
def gembed_source : Flag<["-"], "gembed-source">, Group<g_flags_Group>, Flags<[CC1Option]>,
    HelpText<"Embed source text in DWARF debug sections">;
def gno_embed_source : Flag<["-"], "gno-embed-source">, Group<g_flags_Group>,
    Flags<[DriverOption]>,
    HelpText<"Restore the default behavior of not embedding source text in DWARF debug sections">;
def headerpad__max__install__names : Joined<["-"], "headerpad_max_install_names">;
def help : Flag<["-", "--"], "help">, Flags<[CC1Option,CC1AsOption]>,
  HelpText<"Display available options">;
def index_header_map : Flag<["-"], "index-header-map">, Flags<[CC1Option]>,
  HelpText<"Make the next included directory (-I or -F) an indexer header map">;
def idirafter : JoinedOrSeparate<["-"], "idirafter">, Group<clang_i_Group>, Flags<[CC1Option]>,
  HelpText<"Add directory to AFTER include search path">;
def iframework : JoinedOrSeparate<["-"], "iframework">, Group<clang_i_Group>, Flags<[CC1Option]>,
  HelpText<"Add directory to SYSTEM framework search path">;
def iframeworkwithsysroot : JoinedOrSeparate<["-"], "iframeworkwithsysroot">,
  Group<clang_i_Group>,
  HelpText<"Add directory to SYSTEM framework search path, "
           "absolute paths are relative to -isysroot">,
  MetaVarName<"<directory>">, Flags<[CC1Option]>;
def imacros : JoinedOrSeparate<["-", "--"], "imacros">, Group<clang_i_Group>, Flags<[CC1Option]>,
  HelpText<"Include macros from file before parsing">, MetaVarName<"<file>">;
def image__base : Separate<["-"], "image_base">;
def include_ : JoinedOrSeparate<["-", "--"], "include">, Group<clang_i_Group>, EnumName<"include">,
    MetaVarName<"<file>">, HelpText<"Include file before parsing">, Flags<[CC1Option]>;
def include_pch : Separate<["-"], "include-pch">, Group<clang_i_Group>, Flags<[CC1Option]>,
  HelpText<"Include precompiled header file">, MetaVarName<"<file>">;
def relocatable_pch : Flag<["-", "--"], "relocatable-pch">, Flags<[CC1Option]>,
  HelpText<"Whether to build a relocatable precompiled header">;
def verify_pch : Flag<["-"], "verify-pch">, Group<Action_Group>, Flags<[CC1Option]>,
  HelpText<"Load and verify that a pre-compiled header file is not stale">;
def init : Separate<["-"], "init">;
def install__name : Separate<["-"], "install_name">;
def iprefix : JoinedOrSeparate<["-"], "iprefix">, Group<clang_i_Group>, Flags<[CC1Option]>,
  HelpText<"Set the -iwithprefix/-iwithprefixbefore prefix">, MetaVarName<"<dir>">;
def iquote : JoinedOrSeparate<["-"], "iquote">, Group<clang_i_Group>, Flags<[CC1Option]>,
  HelpText<"Add directory to QUOTE include search path">, MetaVarName<"<directory>">;
def isysroot : JoinedOrSeparate<["-"], "isysroot">, Group<clang_i_Group>, Flags<[CC1Option]>,
  HelpText<"Set the system root directory (usually /)">, MetaVarName<"<dir>">;
def isystem : JoinedOrSeparate<["-"], "isystem">, Group<clang_i_Group>,
  Flags<[CC1Option]>,
  HelpText<"Add directory to SYSTEM include search path">, MetaVarName<"<directory>">;
def isystem_after : JoinedOrSeparate<["-"], "isystem-after">,
  Group<clang_i_Group>, Flags<[DriverOption]>, MetaVarName<"<directory>">,
  HelpText<"Add directory to end of the SYSTEM include search path">;
def iwithprefixbefore : JoinedOrSeparate<["-"], "iwithprefixbefore">, Group<clang_i_Group>,
  HelpText<"Set directory to include search path with prefix">, MetaVarName<"<dir>">,
  Flags<[CC1Option]>;
def iwithprefix : JoinedOrSeparate<["-"], "iwithprefix">, Group<clang_i_Group>, Flags<[CC1Option]>,
  HelpText<"Set directory to SYSTEM include search path with prefix">, MetaVarName<"<dir>">;
def iwithsysroot : JoinedOrSeparate<["-"], "iwithsysroot">, Group<clang_i_Group>,
  HelpText<"Add directory to SYSTEM include search path, "
           "absolute paths are relative to -isysroot">, MetaVarName<"<directory>">,
  Flags<[CC1Option]>;
def ivfsoverlay : JoinedOrSeparate<["-"], "ivfsoverlay">, Group<clang_i_Group>, Flags<[CC1Option]>,
  HelpText<"Overlay the virtual filesystem described by file over the real file system">;
def imultilib : Separate<["-"], "imultilib">, Group<gfortran_Group>;
def keep__private__externs : Flag<["-"], "keep_private_externs">;
def l : JoinedOrSeparate<["-"], "l">, Flags<[LinkerInput, RenderJoined]>,
        Group<Link_Group>;
def lazy__framework : Separate<["-"], "lazy_framework">, Flags<[LinkerInput]>;
def lazy__library : Separate<["-"], "lazy_library">, Flags<[LinkerInput]>;
def mlittle_endian : Flag<["-"], "mlittle-endian">, Flags<[DriverOption]>;
def EL : Flag<["-"], "EL">, Alias<mlittle_endian>;
def mbig_endian : Flag<["-"], "mbig-endian">, Flags<[DriverOption]>;
def EB : Flag<["-"], "EB">, Alias<mbig_endian>;
def m16 : Flag<["-"], "m16">, Group<m_Group>, Flags<[DriverOption, CoreOption]>;
def m32 : Flag<["-"], "m32">, Group<m_Group>, Flags<[DriverOption, CoreOption]>;
def mqdsp6_compat : Flag<["-"], "mqdsp6-compat">, Group<m_Group>, Flags<[DriverOption,CC1Option]>,
  HelpText<"Enable hexagon-qdsp6 backward compatibility">;
def m64 : Flag<["-"], "m64">, Group<m_Group>, Flags<[DriverOption, CoreOption]>;
def mx32 : Flag<["-"], "mx32">, Group<m_Group>, Flags<[DriverOption, CoreOption]>;
def mabi_EQ : Joined<["-"], "mabi=">, Group<m_Group>;
def miamcu : Flag<["-"], "miamcu">, Group<m_Group>, Flags<[DriverOption, CoreOption]>,
  HelpText<"Use Intel MCU ABI">;
def mno_iamcu : Flag<["-"], "mno-iamcu">, Group<m_Group>, Flags<[DriverOption, CoreOption]>;
def malign_functions_EQ : Joined<["-"], "malign-functions=">, Group<clang_ignored_m_Group>;
def malign_loops_EQ : Joined<["-"], "malign-loops=">, Group<clang_ignored_m_Group>;
def malign_jumps_EQ : Joined<["-"], "malign-jumps=">, Group<clang_ignored_m_Group>;
def mfancy_math_387 : Flag<["-"], "mfancy-math-387">, Group<clang_ignored_m_Group>;
def mlong_calls : Flag<["-"], "mlong-calls">, Group<m_Group>,
  HelpText<"Generate branches with extended addressability, usually via indirect jumps.">;
def mno_long_calls : Flag<["-"], "mno-long-calls">, Group<m_Group>,
  HelpText<"Restore the default behaviour of not generating long calls">;
def mexecute_only : Flag<["-"], "mexecute-only">, Group<m_arm_Features_Group>,
  HelpText<"Disallow generation of data access to code sections (ARM only)">;
def mno_execute_only : Flag<["-"], "mno-execute-only">, Group<m_arm_Features_Group>,
  HelpText<"Allow generation of data access to code sections (ARM only)">;
def mtp_mode_EQ : Joined<["-"], "mtp=">, Group<m_arm_Features_Group>, Values<"soft, cp15">,
  HelpText<"Read thread pointer from coprocessor register (ARM only)">;
def mpure_code : Flag<["-"], "mpure-code">, Alias<mexecute_only>; // Alias for GCC compatibility
def mno_pure_code : Flag<["-"], "mno-pure-code">, Alias<mno_execute_only>;
def mtvos_version_min_EQ : Joined<["-"], "mtvos-version-min=">, Group<m_Group>;
def mappletvos_version_min_EQ : Joined<["-"], "mappletvos-version-min=">, Alias<mtvos_version_min_EQ>;
def mtvos_simulator_version_min_EQ : Joined<["-"], "mtvos-simulator-version-min=">;
def mappletvsimulator_version_min_EQ : Joined<["-"], "mappletvsimulator-version-min=">, Alias<mtvos_simulator_version_min_EQ>;
def mwatchos_version_min_EQ : Joined<["-"], "mwatchos-version-min=">, Group<m_Group>;
def mwatchos_simulator_version_min_EQ : Joined<["-"], "mwatchos-simulator-version-min=">;
def mwatchsimulator_version_min_EQ : Joined<["-"], "mwatchsimulator-version-min=">, Alias<mwatchos_simulator_version_min_EQ>;
def march_EQ : Joined<["-"], "march=">, Group<m_Group>, Flags<[CoreOption]>;
def masm_EQ : Joined<["-"], "masm=">, Group<m_Group>, Flags<[DriverOption]>;
def mcmodel_EQ : Joined<["-"], "mcmodel=">, Group<m_Group>;
def mimplicit_it_EQ : Joined<["-"], "mimplicit-it=">, Group<m_Group>;
def mdefault_build_attributes : Joined<["-"], "mdefault-build-attributes">, Group<m_Group>;
def mno_default_build_attributes : Joined<["-"], "mno-default-build-attributes">, Group<m_Group>;
def mconstant_cfstrings : Flag<["-"], "mconstant-cfstrings">, Group<clang_ignored_m_Group>;
def mconsole : Joined<["-"], "mconsole">, Group<m_Group>, Flags<[DriverOption]>;
def mwindows : Joined<["-"], "mwindows">, Group<m_Group>, Flags<[DriverOption]>;
def mdll : Joined<["-"], "mdll">, Group<m_Group>, Flags<[DriverOption]>;
def municode : Joined<["-"], "municode">, Group<m_Group>, Flags<[DriverOption]>;
def mthreads : Joined<["-"], "mthreads">, Group<m_Group>, Flags<[DriverOption]>;
def mcpu_EQ : Joined<["-"], "mcpu=">, Group<m_Group>;
def mmcu_EQ : Joined<["-"], "mmcu=">, Group<m_Group>;
def mdynamic_no_pic : Joined<["-"], "mdynamic-no-pic">, Group<m_Group>;
def mfix_and_continue : Flag<["-"], "mfix-and-continue">, Group<clang_ignored_m_Group>;
def mieee_fp : Flag<["-"], "mieee-fp">, Group<clang_ignored_m_Group>;
def minline_all_stringops : Flag<["-"], "minline-all-stringops">, Group<clang_ignored_m_Group>;
def mno_inline_all_stringops : Flag<["-"], "mno-inline-all-stringops">, Group<clang_ignored_m_Group>;
def malign_double : Flag<["-"], "malign-double">, Group<m_Group>, Flags<[CC1Option]>,
  HelpText<"Align doubles to two words in structs (x86 only)">;
def mfloat_abi_EQ : Joined<["-"], "mfloat-abi=">, Group<m_Group>, Values<"soft,softfp,hard">;
def mfpmath_EQ : Joined<["-"], "mfpmath=">, Group<m_Group>;
def mfpu_EQ : Joined<["-"], "mfpu=">, Group<m_Group>;
def mhwdiv_EQ : Joined<["-"], "mhwdiv=">, Group<m_Group>;
def mhwmult_EQ : Joined<["-"], "mhwmult=">, Group<m_Group>;
def mglobal_merge : Flag<["-"], "mglobal-merge">, Group<m_Group>, Flags<[CC1Option]>,
  HelpText<"Enable merging of globals">;
def mhard_float : Flag<["-"], "mhard-float">, Group<m_Group>;
def miphoneos_version_min_EQ : Joined<["-"], "miphoneos-version-min=">, Group<m_Group>;
def mios_version_min_EQ : Joined<["-"], "mios-version-min=">,
  Alias<miphoneos_version_min_EQ>, HelpText<"Set iOS deployment target">;
def mios_simulator_version_min_EQ : Joined<["-"], "mios-simulator-version-min=">;
def miphonesimulator_version_min_EQ : Joined<["-"], "miphonesimulator-version-min=">, Alias<mios_simulator_version_min_EQ>;
def mkernel : Flag<["-"], "mkernel">, Group<m_Group>;
def mlinker_version_EQ : Joined<["-"], "mlinker-version=">,
  Flags<[DriverOption]>;
def mllvm : Separate<["-"], "mllvm">, Flags<[CC1Option,CC1AsOption,CoreOption]>,
  HelpText<"Additional arguments to forward to LLVM's option processing">;
def mmacosx_version_min_EQ : Joined<["-"], "mmacosx-version-min=">,
  Group<m_Group>, HelpText<"Set Mac OS X deployment target">;
def mmacos_version_min_EQ : Joined<["-"], "mmacos-version-min=">,
  Group<m_Group>, Alias<mmacosx_version_min_EQ>;
def mms_bitfields : Flag<["-"], "mms-bitfields">, Group<m_Group>, Flags<[CC1Option]>,
  HelpText<"Set the default structure layout to be compatible with the Microsoft compiler standard">;
def moutline : Flag<["-"], "moutline">, Group<f_clang_Group>, Flags<[CC1Option]>,
    HelpText<"Enable function outlining (AArch64 only)">;
def mno_outline : Flag<["-"], "mno-outline">, Group<f_clang_Group>, Flags<[CC1Option]>,
    HelpText<"Disable function outlining (AArch64 only)">;
def mno_ms_bitfields : Flag<["-"], "mno-ms-bitfields">, Group<m_Group>,
  HelpText<"Do not set the default structure layout to be compatible with the Microsoft compiler standard">;
def mstackrealign : Flag<["-"], "mstackrealign">, Group<m_Group>, Flags<[CC1Option]>,
  HelpText<"Force realign the stack at entry to every function">;
def mstack_alignment : Joined<["-"], "mstack-alignment=">, Group<m_Group>, Flags<[CC1Option]>,
  HelpText<"Set the stack alignment">;
def mstack_probe_size : Joined<["-"], "mstack-probe-size=">, Group<m_Group>, Flags<[CC1Option]>,
  HelpText<"Set the stack probe size">;
def mstack_arg_probe : Flag<["-"], "mstack-arg-probe">, Group<m_Group>,
  HelpText<"Enable stack probes">;
def mno_stack_arg_probe : Flag<["-"], "mno-stack-arg-probe">, Group<m_Group>, Flags<[CC1Option]>,
  HelpText<"Disable stack probes which are enabled by default">;
def mthread_model : Separate<["-"], "mthread-model">, Group<m_Group>, Flags<[CC1Option]>,
  HelpText<"The thread model to use, e.g. posix, single (posix by default)">, Values<"posix,single">;
def meabi : Separate<["-"], "meabi">, Group<m_Group>, Flags<[CC1Option]>,
  HelpText<"Set EABI type, e.g. 4, 5 or gnu (default depends on triple)">, Values<"default,4,5,gnu">;

def mno_constant_cfstrings : Flag<["-"], "mno-constant-cfstrings">, Group<m_Group>;
def mno_global_merge : Flag<["-"], "mno-global-merge">, Group<m_Group>, Flags<[CC1Option]>,
  HelpText<"Disable merging of globals">;
def mno_pascal_strings : Flag<["-"], "mno-pascal-strings">,
  Alias<fno_pascal_strings>;
def mno_red_zone : Flag<["-"], "mno-red-zone">, Group<m_Group>;
def mno_tls_direct_seg_refs : Flag<["-"], "mno-tls-direct-seg-refs">, Group<m_Group>, Flags<[CC1Option]>,
  HelpText<"Disable direct TLS access through segment registers">;
def mno_relax_all : Flag<["-"], "mno-relax-all">, Group<m_Group>;
def mno_rtd: Flag<["-"], "mno-rtd">, Group<m_Group>;
def mno_soft_float : Flag<["-"], "mno-soft-float">, Group<m_Group>;
def mno_stackrealign : Flag<["-"], "mno-stackrealign">, Group<m_Group>;

def mretpoline : Flag<["-"], "mretpoline">, Group<m_Group>, Flags<[CoreOption,DriverOption]>;
def mno_retpoline : Flag<["-"], "mno-retpoline">, Group<m_Group>, Flags<[CoreOption,DriverOption]>;
def mspeculative_load_hardening : Flag<["-"], "mspeculative-load-hardening">,
  Group<m_Group>, Flags<[CoreOption,CC1Option]>;
def mno_speculative_load_hardening : Flag<["-"], "mno-speculative-load-hardening">,
  Group<m_Group>, Flags<[CoreOption]>;

def mrelax : Flag<["-"], "mrelax">, Group<m_riscv_Features_Group>,
  HelpText<"Enable linker relaxation">;
def mno_relax : Flag<["-"], "mno-relax">, Group<m_riscv_Features_Group>,
  HelpText<"Disable linker relaxation">;

def munaligned_access : Flag<["-"], "munaligned-access">, Group<m_arm_Features_Group>,
  HelpText<"Allow memory accesses to be unaligned (AArch32/AArch64 only)">;
def mno_unaligned_access : Flag<["-"], "mno-unaligned-access">, Group<m_arm_Features_Group>,
  HelpText<"Force all memory accesses to be aligned (AArch32/AArch64 only)">;
def mstrict_align : Flag<["-"], "mstrict-align">, Alias<mno_unaligned_access>, Flags<[CC1Option,HelpHidden]>,
  HelpText<"Force all memory accesses to be aligned (same as mno-unaligned-access)">;
def mno_thumb : Flag<["-"], "mno-thumb">, Group<m_arm_Features_Group>;
def mrestrict_it: Flag<["-"], "mrestrict-it">, Group<m_arm_Features_Group>,
  HelpText<"Disallow generation of deprecated IT blocks for ARMv8. It is on by default for ARMv8 Thumb mode.">;
def mno_restrict_it: Flag<["-"], "mno-restrict-it">, Group<m_arm_Features_Group>,
  HelpText<"Allow generation of deprecated IT blocks for ARMv8. It is off by default for ARMv8 Thumb mode">;
def marm : Flag<["-"], "marm">, Alias<mno_thumb>;
def ffixed_r9 : Flag<["-"], "ffixed-r9">, Group<m_arm_Features_Group>,
  HelpText<"Reserve the r9 register (ARM only)">;
def mno_movt : Flag<["-"], "mno-movt">, Group<m_arm_Features_Group>,
  HelpText<"Disallow use of movt/movw pairs (ARM only)">;
def mcrc : Flag<["-"], "mcrc">, Group<m_Group>,
  HelpText<"Allow use of CRC instructions (ARM/Mips only)">;
def mnocrc : Flag<["-"], "mnocrc">, Group<m_arm_Features_Group>,
  HelpText<"Disallow use of CRC instructions (ARM only)">;
def mno_neg_immediates: Flag<["-"], "mno-neg-immediates">, Group<m_arm_Features_Group>,
  HelpText<"Disallow converting instructions with negative immediates to their negation or inversion.">;

def mgeneral_regs_only : Flag<["-"], "mgeneral-regs-only">, Group<m_aarch64_Features_Group>,
  HelpText<"Generate code which only uses the general purpose registers (AArch64 only)">;
def mfix_cortex_a53_835769 : Flag<["-"], "mfix-cortex-a53-835769">,
  Group<m_aarch64_Features_Group>,
  HelpText<"Workaround Cortex-A53 erratum 835769 (AArch64 only)">;
def mno_fix_cortex_a53_835769 : Flag<["-"], "mno-fix-cortex-a53-835769">,
  Group<m_aarch64_Features_Group>,
  HelpText<"Don't workaround Cortex-A53 erratum 835769 (AArch64 only)">;
foreach i = {1-7,18,20} in
  def ffixed_x#i : Flag<["-"], "ffixed-x"#i>, Group<m_aarch64_Features_Group>,
    HelpText<"Reserve the "#i#" register (AArch64 only)">;

foreach i = {8-15,18} in
  def fcall_saved_x#i : Flag<["-"], "fcall-saved-x"#i>, Group<m_aarch64_Features_Group>,
    HelpText<"Make the x"#i#" register call-saved (AArch64 only)">;

def msign_return_address_EQ : Joined<["-"], "msign-return-address=">,
  Flags<[CC1Option]>, Group<m_Group>, Values<"none,all,non-leaf">,
  HelpText<"Select return address signing scope">;
def mbranch_protection_EQ : Joined<["-"], "mbranch-protection=">,
  HelpText<"Enforce targets of indirect branches and function returns">;

def msimd128 : Flag<["-"], "msimd128">, Group<m_wasm_Features_Group>;
def munimplemented_simd128 : Flag<["-"], "munimplemented-simd128">, Group<m_wasm_Features_Group>;
def mno_unimplemented_simd128 : Flag<["-"], "mno-unimplemented-simd128">, Group<m_wasm_Features_Group>;
def mno_simd128 : Flag<["-"], "mno-simd128">, Group<m_wasm_Features_Group>;
def mnontrapping_fptoint : Flag<["-"], "mnontrapping-fptoint">, Group<m_wasm_Features_Group>;
def mno_nontrapping_fptoint : Flag<["-"], "mno-nontrapping-fptoint">, Group<m_wasm_Features_Group>;
def msign_ext : Flag<["-"], "msign-ext">, Group<m_wasm_Features_Group>;
def mno_sign_ext : Flag<["-"], "mno-sign-ext">, Group<m_wasm_Features_Group>;
def mexception_handing : Flag<["-"], "mexception-handling">, Group<m_wasm_Features_Group>;
def mno_exception_handing : Flag<["-"], "mno-exception-handling">, Group<m_wasm_Features_Group>;

def mamdgpu_debugger_abi : Joined<["-"], "mamdgpu-debugger-abi=">,
  Flags<[HelpHidden]>,
  Group<m_Group>,
  HelpText<"Generate additional code for specified <version> of debugger ABI (AMDGPU only)">,
  MetaVarName<"<version>">;

def mcode_object_v3 : Flag<["-"], "mcode-object-v3">, Group<m_amdgpu_Features_Group>,
  HelpText<"Enable code object v3 (AMDGPU only)">;
def mno_code_object_v3 : Flag<["-"], "mno-code-object-v3">, Group<m_amdgpu_Features_Group>,
  HelpText<"Disable code object v3 (AMDGPU only)">;
def mxnack : Flag<["-"], "mxnack">, Group<m_amdgpu_Features_Group>,
  HelpText<"Enable XNACK (AMDGPU only)">;
def mno_xnack : Flag<["-"], "mno-xnack">, Group<m_amdgpu_Features_Group>,
  HelpText<"Disable XNACK (AMDGPU only)">;
def msram_ecc : Flag<["-"], "msram-ecc">, Group<m_amdgpu_Features_Group>,
  HelpText<"Enable SRAM ECC (AMDGPU only)">;
def mno_sram_ecc : Flag<["-"], "mno-sram-ecc">, Group<m_amdgpu_Features_Group>,
  HelpText<"Disable SRAM ECC (AMDGPU only)">;

def faltivec : Flag<["-"], "faltivec">, Group<f_Group>, Flags<[DriverOption]>;
def fno_altivec : Flag<["-"], "fno-altivec">, Group<f_Group>, Flags<[DriverOption]>;
def maltivec : Flag<["-"], "maltivec">, Group<m_ppc_Features_Group>;
def mno_altivec : Flag<["-"], "mno-altivec">, Group<m_ppc_Features_Group>;
def mvsx : Flag<["-"], "mvsx">, Group<m_ppc_Features_Group>;
def mno_vsx : Flag<["-"], "mno-vsx">, Group<m_ppc_Features_Group>;
def msecure_plt : Flag<["-"], "msecure-plt">, Group<m_ppc_Features_Group>;
def mpower8_vector : Flag<["-"], "mpower8-vector">,
    Group<m_ppc_Features_Group>;
def mno_power8_vector : Flag<["-"], "mno-power8-vector">,
    Group<m_ppc_Features_Group>;
def mpower9_vector : Flag<["-"], "mpower9-vector">,
    Group<m_ppc_Features_Group>;
def mno_power9_vector : Flag<["-"], "mno-power9-vector">,
    Group<m_ppc_Features_Group>;
def mpower8_crypto : Flag<["-"], "mcrypto">,
    Group<m_ppc_Features_Group>;
def mnopower8_crypto : Flag<["-"], "mno-crypto">,
    Group<m_ppc_Features_Group>;
def mdirect_move : Flag<["-"], "mdirect-move">,
    Group<m_ppc_Features_Group>;
def mnodirect_move : Flag<["-"], "mno-direct-move">,
    Group<m_ppc_Features_Group>;
def mhtm : Flag<["-"], "mhtm">, Group<m_ppc_Features_Group>;
def mno_htm : Flag<["-"], "mno-htm">, Group<m_ppc_Features_Group>;
def mfprnd : Flag<["-"], "mfprnd">, Group<m_ppc_Features_Group>;
def mno_fprnd : Flag<["-"], "mno-fprnd">, Group<m_ppc_Features_Group>;
def mcmpb : Flag<["-"], "mcmpb">, Group<m_ppc_Features_Group>;
def mno_cmpb : Flag<["-"], "mno-cmpb">, Group<m_ppc_Features_Group>;
def misel : Flag<["-"], "misel">, Group<m_ppc_Features_Group>;
def mno_isel : Flag<["-"], "mno-isel">, Group<m_ppc_Features_Group>;
def mmfocrf : Flag<["-"], "mmfocrf">, Group<m_ppc_Features_Group>;
def mmfcrf : Flag<["-"], "mmfcrf">, Alias<mmfocrf>;
def mno_mfocrf : Flag<["-"], "mno-mfocrf">, Group<m_ppc_Features_Group>;
def mno_mfcrf : Flag<["-"], "mno-mfcrf">, Alias<mno_mfocrf>;
def mpopcntd : Flag<["-"], "mpopcntd">, Group<m_ppc_Features_Group>;
def mno_popcntd : Flag<["-"], "mno-popcntd">, Group<m_ppc_Features_Group>;
def mqpx : Flag<["-"], "mqpx">, Group<m_ppc_Features_Group>;
def mno_qpx : Flag<["-"], "mno-qpx">, Group<m_ppc_Features_Group>;
def mcrbits : Flag<["-"], "mcrbits">, Group<m_ppc_Features_Group>;
def mno_crbits : Flag<["-"], "mno-crbits">, Group<m_ppc_Features_Group>;
def minvariant_function_descriptors :
  Flag<["-"], "minvariant-function-descriptors">, Group<m_ppc_Features_Group>;
def mno_invariant_function_descriptors :
  Flag<["-"], "mno-invariant-function-descriptors">,
  Group<m_ppc_Features_Group>;
def mfloat128: Flag<["-"], "mfloat128">,
    Group<m_ppc_Features_Group>;
def mno_float128 : Flag<["-"], "mno-float128">,
    Group<m_ppc_Features_Group>;
def mlongcall: Flag<["-"], "mlongcall">,
    Group<m_ppc_Features_Group>;
def mno_longcall : Flag<["-"], "mno-longcall">,
    Group<m_ppc_Features_Group>;

def mvx : Flag<["-"], "mvx">, Group<m_Group>;
def mno_vx : Flag<["-"], "mno-vx">, Group<m_Group>;

def fzvector : Flag<["-"], "fzvector">, Group<f_Group>, Flags<[CC1Option]>,
  HelpText<"Enable System z vector language extension">;
def fno_zvector : Flag<["-"], "fno-zvector">, Group<f_Group>,
  Flags<[CC1Option]>;
def mzvector : Flag<["-"], "mzvector">, Alias<fzvector>;
def mno_zvector : Flag<["-"], "mno-zvector">, Alias<fno_zvector>;

def mbackchain : Flag<["-"], "mbackchain">, Group<m_Group>, Flags<[DriverOption,CC1Option]>,
  HelpText<"Link stack frames through backchain on System Z">;
def mno_backchain : Flag<["-"], "mno-backchain">, Group<m_Group>, Flags<[DriverOption,CC1Option]>;

def mno_warn_nonportable_cfstrings : Flag<["-"], "mno-warn-nonportable-cfstrings">, Group<m_Group>;
def mno_omit_leaf_frame_pointer : Flag<["-"], "mno-omit-leaf-frame-pointer">, Group<m_Group>;
def momit_leaf_frame_pointer : Flag<["-"], "momit-leaf-frame-pointer">, Group<m_Group>,
  HelpText<"Omit frame pointer setup for leaf functions">, Flags<[CC1Option]>;
def moslib_EQ : Joined<["-"], "moslib=">, Group<m_Group>;
def mpascal_strings : Flag<["-"], "mpascal-strings">, Alias<fpascal_strings>;
def mred_zone : Flag<["-"], "mred-zone">, Group<m_Group>;
def mtls_direct_seg_refs : Flag<["-"], "mtls-direct-seg-refs">, Group<m_Group>,
  HelpText<"Enable direct TLS access through segment registers (default)">;
def mregparm_EQ : Joined<["-"], "mregparm=">, Group<m_Group>;
def mrelax_all : Flag<["-"], "mrelax-all">, Group<m_Group>, Flags<[CC1Option,CC1AsOption]>,
  HelpText<"(integrated-as) Relax all machine instructions">;
def mincremental_linker_compatible : Flag<["-"], "mincremental-linker-compatible">, Group<m_Group>,
  Flags<[CC1Option,CC1AsOption]>,
  HelpText<"(integrated-as) Emit an object file which can be used with an incremental linker">;
def mno_incremental_linker_compatible : Flag<["-"], "mno-incremental-linker-compatible">, Group<m_Group>,
  HelpText<"(integrated-as) Emit an object file which cannot be used with an incremental linker">;
def mrtd : Flag<["-"], "mrtd">, Group<m_Group>, Flags<[CC1Option]>,
  HelpText<"Make StdCall calling convention the default">;
def msmall_data_threshold_EQ : Joined <["-"], "msmall-data-threshold=">,
  Group<m_Group>, Alias<G>;
def msoft_float : Flag<["-"], "msoft-float">, Group<m_Group>, Flags<[CC1Option]>,
  HelpText<"Use software floating point">;
def mno_implicit_float : Flag<["-"], "mno-implicit-float">, Group<m_Group>,
  HelpText<"Don't generate implicit floating point instructions">;
def mimplicit_float : Flag<["-"], "mimplicit-float">, Group<m_Group>;
def mrecip : Flag<["-"], "mrecip">, Group<m_Group>;
def mrecip_EQ : CommaJoined<["-"], "mrecip=">, Group<m_Group>, Flags<[CC1Option]>;
def mprefer_vector_width_EQ : Joined<["-"], "mprefer-vector-width=">, Group<m_Group>, Flags<[CC1Option]>,
  HelpText<"Specifies preferred vector width for auto-vectorization. Defaults to 'none' which allows target specific decisions.">;
def mpie_copy_relocations : Flag<["-"], "mpie-copy-relocations">, Group<m_Group>,
  Flags<[CC1Option]>,
  HelpText<"Use copy relocations support for PIE builds">;
def mno_pie_copy_relocations : Flag<["-"], "mno-pie-copy-relocations">, Group<m_Group>;
def mfentry : Flag<["-"], "mfentry">, HelpText<"Insert calls to fentry at function entry (x86 only)">,
  Flags<[CC1Option]>, Group<m_Group>;
def mips16 : Flag<["-"], "mips16">, Group<m_mips_Features_Group>;
def mno_mips16 : Flag<["-"], "mno-mips16">, Group<m_mips_Features_Group>;
def mmicromips : Flag<["-"], "mmicromips">, Group<m_mips_Features_Group>;
def mno_micromips : Flag<["-"], "mno-micromips">, Group<m_mips_Features_Group>;
def mxgot : Flag<["-"], "mxgot">, Group<m_mips_Features_Group>;
def mno_xgot : Flag<["-"], "mno-xgot">, Group<m_mips_Features_Group>;
def mldc1_sdc1 : Flag<["-"], "mldc1-sdc1">, Group<m_mips_Features_Group>;
def mno_ldc1_sdc1 : Flag<["-"], "mno-ldc1-sdc1">, Group<m_mips_Features_Group>;
def mcheck_zero_division : Flag<["-"], "mcheck-zero-division">,
                           Group<m_mips_Features_Group>;
def mno_check_zero_division : Flag<["-"], "mno-check-zero-division">,
                              Group<m_mips_Features_Group>;
def mcompact_branches_EQ : Joined<["-"], "mcompact-branches=">,
                           Group<m_mips_Features_Group>;
def mbranch_likely : Flag<["-"], "mbranch-likely">, Group<m_Group>,
  IgnoredGCCCompat;
def mno_branch_likely : Flag<["-"], "mno-branch-likely">, Group<m_Group>,
  IgnoredGCCCompat;
def mindirect_jump_EQ : Joined<["-"], "mindirect-jump=">,
  Group<m_mips_Features_Group>,
  HelpText<"Change indirect jump instructions to inhibit speculation">;
def mdsp : Flag<["-"], "mdsp">, Group<m_mips_Features_Group>;
def mno_dsp : Flag<["-"], "mno-dsp">, Group<m_mips_Features_Group>;
def mdspr2 : Flag<["-"], "mdspr2">, Group<m_mips_Features_Group>;
def mno_dspr2 : Flag<["-"], "mno-dspr2">, Group<m_mips_Features_Group>;
def msingle_float : Flag<["-"], "msingle-float">, Group<m_mips_Features_Group>;
def mdouble_float : Flag<["-"], "mdouble-float">, Group<m_mips_Features_Group>;
def mmadd4 : Flag<["-"], "mmadd4">, Group<m_mips_Features_Group>,
  HelpText<"Enable the generation of 4-operand madd.s, madd.d and related instructions.">;
def mno_madd4 : Flag<["-"], "mno-madd4">, Group<m_mips_Features_Group>,
  HelpText<"Disable the generation of 4-operand madd.s, madd.d and related instructions.">;
def mmsa : Flag<["-"], "mmsa">, Group<m_mips_Features_Group>,
  HelpText<"Enable MSA ASE (MIPS only)">;
def mno_msa : Flag<["-"], "mno-msa">, Group<m_mips_Features_Group>,
  HelpText<"Disable MSA ASE (MIPS only)">;
def mmt : Flag<["-"], "mmt">, Group<m_mips_Features_Group>,
  HelpText<"Enable MT ASE (MIPS only)">;
def mno_mt : Flag<["-"], "mno-mt">, Group<m_mips_Features_Group>,
  HelpText<"Disable MT ASE (MIPS only)">;
def mfp64 : Flag<["-"], "mfp64">, Group<m_mips_Features_Group>,
  HelpText<"Use 64-bit floating point registers (MIPS only)">;
def mfp32 : Flag<["-"], "mfp32">, Group<m_mips_Features_Group>,
  HelpText<"Use 32-bit floating point registers (MIPS only)">;
def mgpopt : Flag<["-"], "mgpopt">, Group<m_mips_Features_Group>,
  HelpText<"Use GP relative accesses for symbols known to be in a small"
           " data section (MIPS)">;
def mno_gpopt : Flag<["-"], "mno-gpopt">, Group<m_mips_Features_Group>,
  HelpText<"Do not use GP relative accesses for symbols known to be in a small"
           " data section (MIPS)">;
def mlocal_sdata : Flag<["-"], "mlocal-sdata">,
  Group<m_mips_Features_Group>,
  HelpText<"Extend the -G behaviour to object local data (MIPS)">;
def mno_local_sdata : Flag<["-"], "mno-local-sdata">,
  Group<m_mips_Features_Group>,
  HelpText<"Do not extend the -G behaviour to object local data (MIPS)">;
def mextern_sdata : Flag<["-"], "mextern-sdata">,
  Group<m_mips_Features_Group>,
  HelpText<"Assume that externally defined data is in the small data if it"
           " meets the -G <size> threshold (MIPS)">;
def mno_extern_sdata : Flag<["-"], "mno-extern-sdata">,
  Group<m_mips_Features_Group>,
  HelpText<"Do not assume that externally defined data is in the small data if"
           " it meets the -G <size> threshold (MIPS)">;
def membedded_data : Flag<["-"], "membedded-data">,
  Group<m_mips_Features_Group>,
  HelpText<"Place constants in the .rodata section instead of the .sdata "
           "section even if they meet the -G <size> threshold (MIPS)">;
def mno_embedded_data : Flag<["-"], "mno-embedded-data">,
  Group<m_mips_Features_Group>,
  HelpText<"Do not place constants in the .rodata section instead of the "
           ".sdata if they meet the -G <size> threshold (MIPS)">;
def mnan_EQ : Joined<["-"], "mnan=">, Group<m_mips_Features_Group>;
def mabs_EQ : Joined<["-"], "mabs=">, Group<m_mips_Features_Group>;
def mabicalls : Flag<["-"], "mabicalls">, Group<m_mips_Features_Group>,
  HelpText<"Enable SVR4-style position-independent code (Mips only)">;
def mno_abicalls : Flag<["-"], "mno-abicalls">, Group<m_mips_Features_Group>,
  HelpText<"Disable SVR4-style position-independent code (Mips only)">;
def mno_crc : Flag<["-"], "mno-crc">, Group<m_mips_Features_Group>,
  HelpText<"Disallow use of CRC instructions (Mips only)">;
def mvirt : Flag<["-"], "mvirt">, Group<m_mips_Features_Group>;
def mno_virt : Flag<["-"], "mno-virt">, Group<m_mips_Features_Group>;
def mginv : Flag<["-"], "mginv">, Group<m_mips_Features_Group>;
def mno_ginv : Flag<["-"], "mno-ginv">, Group<m_mips_Features_Group>;
def mips1 : Flag<["-"], "mips1">,
  Alias<march_EQ>, AliasArgs<["mips1"]>, Group<m_mips_Features_Group>,
  HelpText<"Equivalent to -march=mips1">, Flags<[HelpHidden]>;
def mips2 : Flag<["-"], "mips2">,
  Alias<march_EQ>, AliasArgs<["mips2"]>, Group<m_mips_Features_Group>,
  HelpText<"Equivalent to -march=mips2">, Flags<[HelpHidden]>;
def mips3 : Flag<["-"], "mips3">,
  Alias<march_EQ>, AliasArgs<["mips3"]>, Group<m_mips_Features_Group>,
  HelpText<"Equivalent to -march=mips3">, Flags<[HelpHidden]>;
def mips4 : Flag<["-"], "mips4">,
  Alias<march_EQ>, AliasArgs<["mips4"]>, Group<m_mips_Features_Group>,
  HelpText<"Equivalent to -march=mips4">, Flags<[HelpHidden]>;
def mips5 : Flag<["-"], "mips5">,
  Alias<march_EQ>, AliasArgs<["mips5"]>, Group<m_mips_Features_Group>,
  HelpText<"Equivalent to -march=mips5">, Flags<[HelpHidden]>;
def mips32 : Flag<["-"], "mips32">,
  Alias<march_EQ>, AliasArgs<["mips32"]>, Group<m_mips_Features_Group>,
  HelpText<"Equivalent to -march=mips32">, Flags<[HelpHidden]>;
def mips32r2 : Flag<["-"], "mips32r2">,
  Alias<march_EQ>, AliasArgs<["mips32r2"]>, Group<m_mips_Features_Group>,
  HelpText<"Equivalent to -march=mips32r2">, Flags<[HelpHidden]>;
def mips32r3 : Flag<["-"], "mips32r3">,
  Alias<march_EQ>, AliasArgs<["mips32r3"]>, Group<m_mips_Features_Group>,
  HelpText<"Equivalent to -march=mips32r3">, Flags<[HelpHidden]>;
def mips32r5 : Flag<["-"], "mips32r5">,
  Alias<march_EQ>, AliasArgs<["mips32r5"]>, Group<m_mips_Features_Group>,
  HelpText<"Equivalent to -march=mips32r5">, Flags<[HelpHidden]>;
def mips32r6 : Flag<["-"], "mips32r6">,
  Alias<march_EQ>, AliasArgs<["mips32r6"]>, Group<m_mips_Features_Group>,
  HelpText<"Equivalent to -march=mips32r6">, Flags<[HelpHidden]>;
def mips64 : Flag<["-"], "mips64">,
  Alias<march_EQ>, AliasArgs<["mips64"]>, Group<m_mips_Features_Group>,
  HelpText<"Equivalent to -march=mips64">, Flags<[HelpHidden]>;
def mips64r2 : Flag<["-"], "mips64r2">,
  Alias<march_EQ>, AliasArgs<["mips64r2"]>, Group<m_mips_Features_Group>,
  HelpText<"Equivalent to -march=mips64r2">, Flags<[HelpHidden]>;
def mips64r3 : Flag<["-"], "mips64r3">,
  Alias<march_EQ>, AliasArgs<["mips64r3"]>, Group<m_mips_Features_Group>,
  HelpText<"Equivalent to -march=mips64r3">, Flags<[HelpHidden]>;
def mips64r5 : Flag<["-"], "mips64r5">,
  Alias<march_EQ>, AliasArgs<["mips64r5"]>, Group<m_mips_Features_Group>,
  HelpText<"Equivalent to -march=mips64r5">, Flags<[HelpHidden]>;
def mips64r6 : Flag<["-"], "mips64r6">,
  Alias<march_EQ>, AliasArgs<["mips64r6"]>, Group<m_mips_Features_Group>,
  HelpText<"Equivalent to -march=mips64r6">, Flags<[HelpHidden]>;
def mfpxx : Flag<["-"], "mfpxx">, Group<m_mips_Features_Group>,
  HelpText<"Avoid FPU mode dependent operations when used with the O32 ABI">,
  Flags<[HelpHidden]>;
def modd_spreg : Flag<["-"], "modd-spreg">, Group<m_mips_Features_Group>,
  HelpText<"Enable odd single-precision floating point registers">,
  Flags<[HelpHidden]>;
def mno_odd_spreg : Flag<["-"], "mno-odd-spreg">, Group<m_mips_Features_Group>,
  HelpText<"Disable odd single-precision floating point registers">,
  Flags<[HelpHidden]>;
def mrelax_pic_calls : Flag<["-"], "mrelax-pic-calls">,
  Group<m_mips_Features_Group>,
  HelpText<"Try turning PIC calls (j{al}r{c} $25) into direct calls "
  "(MIPS only)">, Flags<[HelpHidden]>;
def mno_relax_pic_calls : Flag<["-"], "mno-relax-pic-calls">,
  Group<m_mips_Features_Group>,
  HelpText<"Do not try turning PIC calls (j{al}r{c} $25) into direct calls "
  "(MIPS only)">, Flags<[HelpHidden]>;
def mglibc : Flag<["-"], "mglibc">, Group<m_libc_Group>, Flags<[HelpHidden]>;
def muclibc : Flag<["-"], "muclibc">, Group<m_libc_Group>, Flags<[HelpHidden]>;
def module_file_info : Flag<["-"], "module-file-info">, Flags<[DriverOption,CC1Option]>, Group<Action_Group>,
  HelpText<"Provide information about a particular module file">;
def mthumb : Flag<["-"], "mthumb">, Group<m_Group>;
def mtune_EQ : Joined<["-"], "mtune=">, Group<m_Group>;
def multi__module : Flag<["-"], "multi_module">;
def multiply__defined__unused : Separate<["-"], "multiply_defined_unused">;
def multiply__defined : Separate<["-"], "multiply_defined">;
def mwarn_nonportable_cfstrings : Flag<["-"], "mwarn-nonportable-cfstrings">, Group<m_Group>;
def no_canonical_prefixes : Flag<["-"], "no-canonical-prefixes">, Flags<[HelpHidden, CoreOption]>,
  HelpText<"Use relative instead of canonical paths">;
def no_cpp_precomp : Flag<["-"], "no-cpp-precomp">, Group<clang_ignored_f_Group>;
def no_integrated_cpp : Flag<["-", "--"], "no-integrated-cpp">, Flags<[DriverOption]>;
def no_pedantic : Flag<["-", "--"], "no-pedantic">, Group<pedantic_Group>;
def no__dead__strip__inits__and__terms : Flag<["-"], "no_dead_strip_inits_and_terms">;
def nobuiltininc : Flag<["-"], "nobuiltininc">, Flags<[CC1Option, CoreOption]>,
  HelpText<"Disable builtin #include directories">;
def nocudainc : Flag<["-"], "nocudainc">;
def nocudalib : Flag<["-"], "nocudalib">;
def nodefaultlibs : Flag<["-"], "nodefaultlibs">;
def nofixprebinding : Flag<["-"], "nofixprebinding">;
def nolibc : Flag<["-"], "nolibc">;
def nomultidefs : Flag<["-"], "nomultidefs">;
def nopie : Flag<["-"], "nopie">;
def no_pie : Flag<["-"], "no-pie">, Alias<nopie>;
def noprebind : Flag<["-"], "noprebind">;
def noseglinkedit : Flag<["-"], "noseglinkedit">;
def nostartfiles : Flag<["-"], "nostartfiles">;
def nostdinc : Flag<["-"], "nostdinc">, Flags<[CoreOption]>;
def nostdlibinc : Flag<["-"], "nostdlibinc">;
def nostdincxx : Flag<["-"], "nostdinc++">, Flags<[CC1Option]>,
  HelpText<"Disable standard #include directories for the C++ standard library">;
def nostdlib : Flag<["-"], "nostdlib">;
def nostdlibxx : Flag<["-"], "nostdlib++">;
def object : Flag<["-"], "object">;
def o : JoinedOrSeparate<["-"], "o">, Flags<[DriverOption, RenderAsInput, CC1Option, CC1AsOption]>,
  HelpText<"Write output to <file>">, MetaVarName<"<file>">;
def pagezero__size : JoinedOrSeparate<["-"], "pagezero_size">;
def pass_exit_codes : Flag<["-", "--"], "pass-exit-codes">, Flags<[Unsupported]>;
def pedantic_errors : Flag<["-", "--"], "pedantic-errors">, Group<pedantic_Group>, Flags<[CC1Option]>;
def pedantic : Flag<["-", "--"], "pedantic">, Group<pedantic_Group>, Flags<[CC1Option]>;
def pg : Flag<["-"], "pg">, HelpText<"Enable mcount instrumentation">, Flags<[CC1Option]>;
def pipe : Flag<["-", "--"], "pipe">,
  HelpText<"Use pipes between commands, when possible">;
def prebind__all__twolevel__modules : Flag<["-"], "prebind_all_twolevel_modules">;
def prebind : Flag<["-"], "prebind">;
def preload : Flag<["-"], "preload">;
def print_file_name_EQ : Joined<["-", "--"], "print-file-name=">,
  HelpText<"Print the full library path of <file>">, MetaVarName<"<file>">;
def print_ivar_layout : Flag<["-"], "print-ivar-layout">, Flags<[CC1Option]>,
  HelpText<"Enable Objective-C Ivar layout bitmap print trace">;
def print_libgcc_file_name : Flag<["-", "--"], "print-libgcc-file-name">,
  HelpText<"Print the library path for the currently used compiler runtime "
           "library (\"libgcc.a\" or \"libclang_rt.builtins.*.a\")">;
def print_multi_directory : Flag<["-", "--"], "print-multi-directory">;
def print_multi_lib : Flag<["-", "--"], "print-multi-lib">;
def print_multi_os_directory : Flag<["-", "--"], "print-multi-os-directory">,
  Flags<[Unsupported]>;
def print_target_triple : Flag<["-", "--"], "print-target-triple">,
  HelpText<"Print the normalized target triple">;
def print_effective_triple : Flag<["-", "--"], "print-effective-triple">,
  HelpText<"Print the effective target triple">;
def print_prog_name_EQ : Joined<["-", "--"], "print-prog-name=">,
  HelpText<"Print the full program path of <name>">, MetaVarName<"<name>">;
def print_resource_dir : Flag<["-", "--"], "print-resource-dir">,
  HelpText<"Print the resource directory pathname">;
def print_search_dirs : Flag<["-", "--"], "print-search-dirs">,
  HelpText<"Print the paths used for finding libraries and programs">;
def private__bundle : Flag<["-"], "private_bundle">;
def pthreads : Flag<["-"], "pthreads">;
def pthread : Flag<["-"], "pthread">, Flags<[CC1Option]>,
  HelpText<"Support POSIX threads in generated code">;
def no_pthread : Flag<["-"], "no-pthread">, Flags<[CC1Option]>;
def p : Flag<["-"], "p">;
def pie : Flag<["-"], "pie">;
def read__only__relocs : Separate<["-"], "read_only_relocs">;
def remap : Flag<["-"], "remap">;
def rewrite_objc : Flag<["-"], "rewrite-objc">, Flags<[DriverOption,CC1Option]>,
  HelpText<"Rewrite Objective-C source to C++">, Group<Action_Group>;
def rewrite_legacy_objc : Flag<["-"], "rewrite-legacy-objc">, Flags<[DriverOption]>,
  HelpText<"Rewrite Legacy Objective-C source to C++">;
def rdynamic : Flag<["-"], "rdynamic">;
def resource_dir : Separate<["-"], "resource-dir">,
  Flags<[DriverOption, CC1Option, CoreOption, HelpHidden]>,
  HelpText<"The directory which holds the compiler resource files">;
def resource_dir_EQ : Joined<["-"], "resource-dir=">, Flags<[DriverOption, CoreOption]>,
  Alias<resource_dir>;
def rpath : Separate<["-"], "rpath">, Flags<[LinkerInput]>, Group<Link_Group>;
def rtlib_EQ : Joined<["-", "--"], "rtlib=">,
  HelpText<"Compiler runtime library to use">;
def frtlib_add_rpath: Flag<["-"], "frtlib-add-rpath">, Flags<[NoArgumentUnused]>,
  HelpText<"Add -rpath with architecture-specific resource directory to the linker flags">;
def fno_rtlib_add_rpath: Flag<["-"], "fno-rtlib-add-rpath">, Flags<[NoArgumentUnused]>,
  HelpText<"Do not add -rpath with architecture-specific resource directory to the linker flags">;
def r : Flag<["-"], "r">, Flags<[LinkerInput,NoArgumentUnused]>,
        Group<Link_Group>;
def save_temps_EQ : Joined<["-", "--"], "save-temps=">, Flags<[CC1Option, DriverOption]>,
  HelpText<"Save intermediate compilation results.">;
def save_temps : Flag<["-", "--"], "save-temps">, Flags<[DriverOption]>,
  Alias<save_temps_EQ>, AliasArgs<["cwd"]>,
  HelpText<"Save intermediate compilation results">;
def save_stats_EQ : Joined<["-", "--"], "save-stats=">, Flags<[DriverOption]>,
  HelpText<"Save llvm statistics.">;
def save_stats : Flag<["-", "--"], "save-stats">, Flags<[DriverOption]>,
  Alias<save_stats_EQ>, AliasArgs<["cwd"]>,
  HelpText<"Save llvm statistics.">;
def via_file_asm : Flag<["-", "--"], "via-file-asm">, InternalDebugOpt,
  HelpText<"Write assembly to file for input to assemble jobs">;
def sectalign : MultiArg<["-"], "sectalign", 3>;
def sectcreate : MultiArg<["-"], "sectcreate", 3>;
def sectobjectsymbols : MultiArg<["-"], "sectobjectsymbols", 2>;
def sectorder : MultiArg<["-"], "sectorder", 3>;
def seg1addr : JoinedOrSeparate<["-"], "seg1addr">;
def seg__addr__table__filename : Separate<["-"], "seg_addr_table_filename">;
def seg__addr__table : Separate<["-"], "seg_addr_table">;
def segaddr : MultiArg<["-"], "segaddr", 2>;
def segcreate : MultiArg<["-"], "segcreate", 3>;
def seglinkedit : Flag<["-"], "seglinkedit">;
def segprot : MultiArg<["-"], "segprot", 3>;
def segs__read__only__addr : Separate<["-"], "segs_read_only_addr">;
def segs__read__write__addr : Separate<["-"], "segs_read_write_addr">;
def segs__read__ : Joined<["-"], "segs_read_">;
def shared_libgcc : Flag<["-"], "shared-libgcc">;
def shared : Flag<["-", "--"], "shared">;
def single__module : Flag<["-"], "single_module">;
def specs_EQ : Joined<["-", "--"], "specs=">;
def specs : Separate<["-", "--"], "specs">, Flags<[Unsupported]>;
def static_libgcc : Flag<["-"], "static-libgcc">;
def static_libstdcxx : Flag<["-"], "static-libstdc++">;
def static : Flag<["-", "--"], "static">, Flags<[NoArgumentUnused]>;
def std_default_EQ : Joined<["-"], "std-default=">;
def std_EQ : Joined<["-", "--"], "std=">, Flags<[CC1Option]>,
  Group<CompileOnly_Group>, HelpText<"Language standard to compile for">,
  ValuesCode<[{
    const char *Values =
    #define LANGSTANDARD(id, name, lang, desc, features) name ","
    #define LANGSTANDARD_ALIAS(id, alias) alias ","
    #include "clang/Frontend/LangStandards.def"
    ;
  }]>;
def stdlib_EQ : Joined<["-", "--"], "stdlib=">, Flags<[CC1Option]>,
  HelpText<"C++ standard library to use">, Values<"libc++,libstdc++,platform">;
def sub__library : JoinedOrSeparate<["-"], "sub_library">;
def sub__umbrella : JoinedOrSeparate<["-"], "sub_umbrella">;
def system_header_prefix : Joined<["--"], "system-header-prefix=">,
  Group<clang_i_Group>, Flags<[CC1Option]>, MetaVarName<"<prefix>">,
  HelpText<"Treat all #include paths starting with <prefix> as including a "
           "system header.">;
def : Separate<["--"], "system-header-prefix">, Alias<system_header_prefix>;
def no_system_header_prefix : Joined<["--"], "no-system-header-prefix=">,
  Group<clang_i_Group>, Flags<[CC1Option]>, MetaVarName<"<prefix>">,
  HelpText<"Treat all #include paths starting with <prefix> as not including a "
           "system header.">;
def : Separate<["--"], "no-system-header-prefix">, Alias<no_system_header_prefix>;
def s : Flag<["-"], "s">, Group<Link_Group>;
def target : Joined<["--"], "target=">, Flags<[DriverOption, CoreOption]>,
  HelpText<"Generate code for the given target">;
def gcc_toolchain : Joined<["--"], "gcc-toolchain=">, Flags<[DriverOption]>,
  HelpText<"Use the gcc toolchain at the given directory">;
def time : Flag<["-"], "time">,
  HelpText<"Time individual commands">;
def traditional_cpp : Flag<["-", "--"], "traditional-cpp">, Flags<[CC1Option]>,
  HelpText<"Enable some traditional CPP emulation">;
def traditional : Flag<["-", "--"], "traditional">;
def trigraphs : Flag<["-", "--"], "trigraphs">, Alias<ftrigraphs>,
  HelpText<"Process trigraph sequences">;
def twolevel__namespace__hints : Flag<["-"], "twolevel_namespace_hints">;
def twolevel__namespace : Flag<["-"], "twolevel_namespace">;
def t : Flag<["-"], "t">, Group<Link_Group>;
def umbrella : Separate<["-"], "umbrella">;
def undefined : JoinedOrSeparate<["-"], "undefined">, Group<u_Group>;
def undef : Flag<["-"], "undef">, Group<u_Group>, Flags<[CC1Option]>,
  HelpText<"undef all system defines">;
def unexported__symbols__list : Separate<["-"], "unexported_symbols_list">;
def u : JoinedOrSeparate<["-"], "u">, Group<u_Group>;
def v : Flag<["-"], "v">, Flags<[CC1Option, CoreOption]>,
  HelpText<"Show commands to run and use verbose output">;
def verify_debug_info : Flag<["--"], "verify-debug-info">, Flags<[DriverOption]>,
  HelpText<"Verify the binary representation of debug output">;
def weak_l : Joined<["-"], "weak-l">, Flags<[LinkerInput]>;
def weak__framework : Separate<["-"], "weak_framework">, Flags<[LinkerInput]>;
def weak__library : Separate<["-"], "weak_library">, Flags<[LinkerInput]>;
def weak__reference__mismatches : Separate<["-"], "weak_reference_mismatches">;
def whatsloaded : Flag<["-"], "whatsloaded">;
def whyload : Flag<["-"], "whyload">;
def w : Flag<["-"], "w">, HelpText<"Suppress all warnings">, Flags<[CC1Option]>;
def x : JoinedOrSeparate<["-"], "x">, Flags<[DriverOption,CC1Option]>,
  HelpText<"Treat subsequent input files as having type <language>">,
  MetaVarName<"<language>">;
def y : Joined<["-"], "y">;

def fintegrated_as : Flag<["-"], "fintegrated-as">, Flags<[DriverOption]>,
                     Group<f_Group>, HelpText<"Enable the integrated assembler">;
def fno_integrated_as : Flag<["-"], "fno-integrated-as">,
                        Flags<[CC1Option, DriverOption]>, Group<f_Group>,
                        HelpText<"Disable the integrated assembler">;
def : Flag<["-"], "integrated-as">, Alias<fintegrated_as>, Flags<[DriverOption]>;
def : Flag<["-"], "no-integrated-as">, Alias<fno_integrated_as>,
      Flags<[CC1Option, DriverOption]>;

def working_directory : JoinedOrSeparate<["-"], "working-directory">, Flags<[CC1Option]>,
  HelpText<"Resolve file paths relative to the specified directory">;
def working_directory_EQ : Joined<["-"], "working-directory=">, Flags<[CC1Option]>,
  Alias<working_directory>;

// Double dash options, which are usually an alias for one of the previous
// options.

def _mhwdiv_EQ : Joined<["--"], "mhwdiv=">, Alias<mhwdiv_EQ>;
def _mhwdiv : Separate<["--"], "mhwdiv">, Alias<mhwdiv_EQ>;
def _CLASSPATH_EQ : Joined<["--"], "CLASSPATH=">, Alias<fclasspath_EQ>;
def _CLASSPATH : Separate<["--"], "CLASSPATH">, Alias<fclasspath_EQ>;
def _all_warnings : Flag<["--"], "all-warnings">, Alias<Wall>;
def _analyze_auto : Flag<["--"], "analyze-auto">, Flags<[DriverOption]>;
def _analyzer_no_default_checks : Flag<["--"], "analyzer-no-default-checks">, Flags<[DriverOption]>;
def _analyzer_output : JoinedOrSeparate<["--"], "analyzer-output">, Flags<[DriverOption]>,
  HelpText<"Static analyzer report output format (html|plist|plist-multi-file|plist-html|text).">;
def _analyze : Flag<["--"], "analyze">, Flags<[DriverOption, CoreOption]>,
  HelpText<"Run the static analyzer">;
def _assemble : Flag<["--"], "assemble">, Alias<S>;
def _assert_EQ : Joined<["--"], "assert=">, Alias<A>;
def _assert : Separate<["--"], "assert">, Alias<A>;
def _bootclasspath_EQ : Joined<["--"], "bootclasspath=">, Alias<fbootclasspath_EQ>;
def _bootclasspath : Separate<["--"], "bootclasspath">, Alias<fbootclasspath_EQ>;
def _classpath_EQ : Joined<["--"], "classpath=">, Alias<fclasspath_EQ>;
def _classpath : Separate<["--"], "classpath">, Alias<fclasspath_EQ>;
def _comments_in_macros : Flag<["--"], "comments-in-macros">, Alias<CC>;
def _comments : Flag<["--"], "comments">, Alias<C>;
def _compile : Flag<["--"], "compile">, Alias<c>;
def _constant_cfstrings : Flag<["--"], "constant-cfstrings">;
def _debug_EQ : Joined<["--"], "debug=">, Alias<g_Flag>;
def _debug : Flag<["--"], "debug">, Alias<g_Flag>;
def _define_macro_EQ : Joined<["--"], "define-macro=">, Alias<D>;
def _define_macro : Separate<["--"], "define-macro">, Alias<D>;
def _dependencies : Flag<["--"], "dependencies">, Alias<M>;
def _dyld_prefix_EQ : Joined<["--"], "dyld-prefix=">;
def _dyld_prefix : Separate<["--"], "dyld-prefix">, Alias<_dyld_prefix_EQ>;
def _encoding_EQ : Joined<["--"], "encoding=">, Alias<fencoding_EQ>;
def _encoding : Separate<["--"], "encoding">, Alias<fencoding_EQ>;
def _entry : Flag<["--"], "entry">, Alias<e>;
def _extdirs_EQ : Joined<["--"], "extdirs=">, Alias<fextdirs_EQ>;
def _extdirs : Separate<["--"], "extdirs">, Alias<fextdirs_EQ>;
def _extra_warnings : Flag<["--"], "extra-warnings">, Alias<W_Joined>;
def _for_linker_EQ : Joined<["--"], "for-linker=">, Alias<Xlinker>;
def _for_linker : Separate<["--"], "for-linker">, Alias<Xlinker>;
def _force_link_EQ : Joined<["--"], "force-link=">, Alias<u>;
def _force_link : Separate<["--"], "force-link">, Alias<u>;
def _help_hidden : Flag<["--"], "help-hidden">,
  HelpText<"Display help for hidden options">;
def _imacros_EQ : Joined<["--"], "imacros=">, Alias<imacros>;
def _include_barrier : Flag<["--"], "include-barrier">, Alias<I_>;
def _include_directory_after_EQ : Joined<["--"], "include-directory-after=">, Alias<idirafter>;
def _include_directory_after : Separate<["--"], "include-directory-after">, Alias<idirafter>;
def _include_directory_EQ : Joined<["--"], "include-directory=">, Alias<I>;
def _include_directory : Separate<["--"], "include-directory">, Alias<I>;
def _include_prefix_EQ : Joined<["--"], "include-prefix=">, Alias<iprefix>;
def _include_prefix : Separate<["--"], "include-prefix">, Alias<iprefix>;
def _include_with_prefix_after_EQ : Joined<["--"], "include-with-prefix-after=">, Alias<iwithprefix>;
def _include_with_prefix_after : Separate<["--"], "include-with-prefix-after">, Alias<iwithprefix>;
def _include_with_prefix_before_EQ : Joined<["--"], "include-with-prefix-before=">, Alias<iwithprefixbefore>;
def _include_with_prefix_before : Separate<["--"], "include-with-prefix-before">, Alias<iwithprefixbefore>;
def _include_with_prefix_EQ : Joined<["--"], "include-with-prefix=">, Alias<iwithprefix>;
def _include_with_prefix : Separate<["--"], "include-with-prefix">, Alias<iwithprefix>;
def _include_EQ : Joined<["--"], "include=">, Alias<include_>;
def _language_EQ : Joined<["--"], "language=">, Alias<x>;
def _language : Separate<["--"], "language">, Alias<x>;
def _library_directory_EQ : Joined<["--"], "library-directory=">, Alias<L>;
def _library_directory : Separate<["--"], "library-directory">, Alias<L>;
def _no_line_commands : Flag<["--"], "no-line-commands">, Alias<P>;
def _no_standard_includes : Flag<["--"], "no-standard-includes">, Alias<nostdinc>;
def _no_standard_libraries : Flag<["--"], "no-standard-libraries">, Alias<nostdlib>;
def _no_undefined : Flag<["--"], "no-undefined">, Flags<[LinkerInput]>;
def _no_warnings : Flag<["--"], "no-warnings">, Alias<w>;
def _optimize_EQ : Joined<["--"], "optimize=">, Alias<O>;
def _optimize : Flag<["--"], "optimize">, Alias<O>;
def _output_class_directory_EQ : Joined<["--"], "output-class-directory=">, Alias<foutput_class_dir_EQ>;
def _output_class_directory : Separate<["--"], "output-class-directory">, Alias<foutput_class_dir_EQ>;
def _output_EQ : Joined<["--"], "output=">, Alias<o>;
def _output : Separate<["--"], "output">, Alias<o>;
def _param : Separate<["--"], "param">, Group<CompileOnly_Group>;
def _param_EQ : Joined<["--"], "param=">, Alias<_param>;
def _precompile : Flag<["--"], "precompile">, Flags<[DriverOption]>,
  Group<Action_Group>, HelpText<"Only precompile the input">;
def _prefix_EQ : Joined<["--"], "prefix=">, Alias<B>;
def _prefix : Separate<["--"], "prefix">, Alias<B>;
def _preprocess : Flag<["--"], "preprocess">, Alias<E>;
def _print_diagnostic_categories : Flag<["--"], "print-diagnostic-categories">;
def _print_file_name : Separate<["--"], "print-file-name">, Alias<print_file_name_EQ>;
def _print_missing_file_dependencies : Flag<["--"], "print-missing-file-dependencies">, Alias<MG>;
def _print_prog_name : Separate<["--"], "print-prog-name">, Alias<print_prog_name_EQ>;
def _profile_blocks : Flag<["--"], "profile-blocks">, Alias<a>;
def _profile : Flag<["--"], "profile">, Alias<p>;
def _resource_EQ : Joined<["--"], "resource=">, Alias<fcompile_resource_EQ>;
def _resource : Separate<["--"], "resource">, Alias<fcompile_resource_EQ>;
def _rtlib : Separate<["--"], "rtlib">, Alias<rtlib_EQ>;
def _serialize_diags : Separate<["-", "--"], "serialize-diagnostics">, Flags<[DriverOption]>,
  HelpText<"Serialize compiler diagnostics to a file">;
// We give --version different semantics from -version.
def _version : Flag<["--"], "version">, Flags<[CoreOption, CC1Option]>,
  HelpText<"Print version information">;
def _signed_char : Flag<["--"], "signed-char">, Alias<fsigned_char>;
def _std : Separate<["--"], "std">, Alias<std_EQ>;
def _stdlib : Separate<["--"], "stdlib">, Alias<stdlib_EQ>;
def _sysroot_EQ : Joined<["--"], "sysroot=">;
def _sysroot : Separate<["--"], "sysroot">, Alias<_sysroot_EQ>;
def _target_help : Flag<["--"], "target-help">;
def _trace_includes : Flag<["--"], "trace-includes">, Alias<H>;
def _undefine_macro_EQ : Joined<["--"], "undefine-macro=">, Alias<U>;
def _undefine_macro : Separate<["--"], "undefine-macro">, Alias<U>;
def _unsigned_char : Flag<["--"], "unsigned-char">, Alias<funsigned_char>;
def _user_dependencies : Flag<["--"], "user-dependencies">, Alias<MM>;
def _verbose : Flag<["--"], "verbose">, Alias<v>;
def _warn__EQ : Joined<["--"], "warn-=">, Alias<W_Joined>;
def _warn_ : Joined<["--"], "warn-">, Alias<W_Joined>;
def _write_dependencies : Flag<["--"], "write-dependencies">, Alias<MD>;
def _write_user_dependencies : Flag<["--"], "write-user-dependencies">, Alias<MMD>;
def _ : Joined<["--"], "">, Flags<[Unsupported]>;

// Hexagon feature flags.
def mieee_rnd_near : Flag<["-"], "mieee-rnd-near">,
  Group<m_hexagon_Features_Group>;
def mv5 : Flag<["-"], "mv5">, Group<m_hexagon_Features_Group>, Alias<mcpu_EQ>,
  AliasArgs<["hexagonv5"]>;
def mv55 : Flag<["-"], "mv55">, Group<m_hexagon_Features_Group>,
  Alias<mcpu_EQ>, AliasArgs<["hexagonv55"]>;
def mv60 : Flag<["-"], "mv60">, Group<m_hexagon_Features_Group>,
  Alias<mcpu_EQ>, AliasArgs<["hexagonv60"]>;
def mv62 : Flag<["-"], "mv62">, Group<m_hexagon_Features_Group>,
  Alias<mcpu_EQ>, AliasArgs<["hexagonv62"]>;
def mv65 : Flag<["-"], "mv65">, Group<m_hexagon_Features_Group>,
  Alias<mcpu_EQ>, AliasArgs<["hexagonv65"]>;
def mv66 : Flag<["-"], "mv66">, Group<m_hexagon_Features_Group>,
  Alias<mcpu_EQ>, AliasArgs<["hexagonv66"]>;
def mhexagon_hvx : Flag<["-"], "mhvx">, Group<m_hexagon_Features_HVX_Group>,
  HelpText<"Enable Hexagon Vector eXtensions">;
def mhexagon_hvx_EQ : Joined<["-"], "mhvx=">,
  Group<m_hexagon_Features_HVX_Group>,
  HelpText<"Enable Hexagon Vector eXtensions">;
def mno_hexagon_hvx : Flag<["-"], "mno-hvx">,
  Group<m_hexagon_Features_HVX_Group>,
  HelpText<"Disable Hexagon Vector eXtensions">;
def mhexagon_hvx_length_EQ : Joined<["-"], "mhvx-length=">,
  Group<m_hexagon_Features_HVX_Group>, HelpText<"Set Hexagon Vector Length">,
  Values<"64B,128B">;
def ffixed_r19: Flag<["-"], "ffixed-r19">,
  HelpText<"Reserve register r19 (Hexagon only)">;
def mmemops : Flag<["-"], "mmemops">, Group<m_hexagon_Features_Group>,
  Flags<[CC1Option]>, HelpText<"Enable generation of memop instructions">;
def mno_memops : Flag<["-"], "mno-memops">, Group<m_hexagon_Features_Group>,
  Flags<[CC1Option]>, HelpText<"Disable generation of memop instructions">;
def mpackets : Flag<["-"], "mpackets">, Group<m_hexagon_Features_Group>,
  Flags<[CC1Option]>, HelpText<"Enable generation of instruction packets">;
def mno_packets : Flag<["-"], "mno-packets">, Group<m_hexagon_Features_Group>,
  Flags<[CC1Option]>, HelpText<"Disable generation of instruction packets">;
def mnvj : Flag<["-"], "mnvj">, Group<m_hexagon_Features_Group>,
  Flags<[CC1Option]>, HelpText<"Enable generation of new-value jumps">;
def mno_nvj : Flag<["-"], "mno-nvj">, Group<m_hexagon_Features_Group>,
  Flags<[CC1Option]>, HelpText<"Disable generation of new-value jumps">;
def mnvs : Flag<["-"], "mnvs">, Group<m_hexagon_Features_Group>,
  Flags<[CC1Option]>, HelpText<"Enable generation of new-value stores">;
def mno_nvs : Flag<["-"], "mno-nvs">, Group<m_hexagon_Features_Group>,
  Flags<[CC1Option]>, HelpText<"Disable generation of new-value stores">;


// X86 feature flags
def mx87 : Flag<["-"], "mx87">, Group<m_x86_Features_Group>;
def mno_x87 : Flag<["-"], "mno-x87">, Group<m_x86_Features_Group>;
def m80387 : Flag<["-"], "m80387">, Alias<mx87>;
def mno_80387 : Flag<["-"], "mno-80387">, Alias<mno_x87>;
def mmmx : Flag<["-"], "mmmx">, Group<m_x86_Features_Group>;
def mno_mmx : Flag<["-"], "mno-mmx">, Group<m_x86_Features_Group>;
def m3dnow : Flag<["-"], "m3dnow">, Group<m_x86_Features_Group>;
def mno_3dnow : Flag<["-"], "mno-3dnow">, Group<m_x86_Features_Group>;
def m3dnowa : Flag<["-"], "m3dnowa">, Group<m_x86_Features_Group>;
def mno_3dnowa : Flag<["-"], "mno-3dnowa">, Group<m_x86_Features_Group>;
def msse : Flag<["-"], "msse">, Group<m_x86_Features_Group>;
def mno_sse : Flag<["-"], "mno-sse">, Group<m_x86_Features_Group>;
def msse2 : Flag<["-"], "msse2">, Group<m_x86_Features_Group>;
def mno_sse2 : Flag<["-"], "mno-sse2">, Group<m_x86_Features_Group>;
def msse3 : Flag<["-"], "msse3">, Group<m_x86_Features_Group>;
def mno_sse3 : Flag<["-"], "mno-sse3">, Group<m_x86_Features_Group>;
def mssse3 : Flag<["-"], "mssse3">, Group<m_x86_Features_Group>;
def mno_ssse3 : Flag<["-"], "mno-ssse3">, Group<m_x86_Features_Group>;
def msse4_1 : Flag<["-"], "msse4.1">, Group<m_x86_Features_Group>;
def mno_sse4_1 : Flag<["-"], "mno-sse4.1">, Group<m_x86_Features_Group>;
def msse4_2 : Flag<["-"], "msse4.2">, Group<m_x86_Features_Group>;
def mno_sse4_2 : Flag<["-"], "mno-sse4.2">, Group<m_x86_Features_Group>;
def msse4 : Flag<["-"], "msse4">, Alias<msse4_2>;
// -mno-sse4 turns off sse4.1 which has the effect of turning off everything
// later than 4.1. -msse4 turns on 4.2 which has the effect of turning on
// everything earlier than 4.2.
def mno_sse4 : Flag<["-"], "mno-sse4">, Alias<mno_sse4_1>;
def msse4a : Flag<["-"], "msse4a">, Group<m_x86_Features_Group>;
def mno_sse4a : Flag<["-"], "mno-sse4a">, Group<m_x86_Features_Group>;
def mavx : Flag<["-"], "mavx">, Group<m_x86_Features_Group>;
def mno_avx : Flag<["-"], "mno-avx">, Group<m_x86_Features_Group>;
def mavx2 : Flag<["-"], "mavx2">, Group<m_x86_Features_Group>;
def mno_avx2 : Flag<["-"], "mno-avx2">, Group<m_x86_Features_Group>;
def mavx512f : Flag<["-"], "mavx512f">, Group<m_x86_Features_Group>;
def mno_avx512f : Flag<["-"], "mno-avx512f">, Group<m_x86_Features_Group>;
def mavx512bitalg : Flag<["-"], "mavx512bitalg">, Group<m_x86_Features_Group>;
def mno_avx512bitalg : Flag<["-"], "mno-avx512bitalg">, Group<m_x86_Features_Group>;
def mavx512bw : Flag<["-"], "mavx512bw">, Group<m_x86_Features_Group>;
def mno_avx512bw : Flag<["-"], "mno-avx512bw">, Group<m_x86_Features_Group>;
def mavx512cd : Flag<["-"], "mavx512cd">, Group<m_x86_Features_Group>;
def mno_avx512cd : Flag<["-"], "mno-avx512cd">, Group<m_x86_Features_Group>;
def mavx512dq : Flag<["-"], "mavx512dq">, Group<m_x86_Features_Group>;
def mno_avx512dq : Flag<["-"], "mno-avx512dq">, Group<m_x86_Features_Group>;
def mavx512er : Flag<["-"], "mavx512er">, Group<m_x86_Features_Group>;
def mno_avx512er : Flag<["-"], "mno-avx512er">, Group<m_x86_Features_Group>;
def mavx512ifma : Flag<["-"], "mavx512ifma">, Group<m_x86_Features_Group>;
def mno_avx512ifma : Flag<["-"], "mno-avx512ifma">, Group<m_x86_Features_Group>;
def mavx512pf : Flag<["-"], "mavx512pf">, Group<m_x86_Features_Group>;
def mno_avx512pf : Flag<["-"], "mno-avx512pf">, Group<m_x86_Features_Group>;
def mavx512vbmi : Flag<["-"], "mavx512vbmi">, Group<m_x86_Features_Group>;
def mno_avx512vbmi : Flag<["-"], "mno-avx512vbmi">, Group<m_x86_Features_Group>;
def mavx512vbmi2 : Flag<["-"], "mavx512vbmi2">, Group<m_x86_Features_Group>;
def mno_avx512vbmi2 : Flag<["-"], "mno-avx512vbmi2">, Group<m_x86_Features_Group>;
def mavx512vl : Flag<["-"], "mavx512vl">, Group<m_x86_Features_Group>;
def mno_avx512vl : Flag<["-"], "mno-avx512vl">, Group<m_x86_Features_Group>;
def mavx512vnni : Flag<["-"], "mavx512vnni">, Group<m_x86_Features_Group>;
def mno_avx512vnni : Flag<["-"], "mno-avx512vnni">, Group<m_x86_Features_Group>;
def mavx512vpopcntdq : Flag<["-"], "mavx512vpopcntdq">, Group<m_x86_Features_Group>;
def mno_avx512vpopcntdq : Flag<["-"], "mno-avx512vpopcntdq">, Group<m_x86_Features_Group>;
def madx : Flag<["-"], "madx">, Group<m_x86_Features_Group>;
def mno_adx : Flag<["-"], "mno-adx">, Group<m_x86_Features_Group>;
def maes : Flag<["-"], "maes">, Group<m_x86_Features_Group>;
def mno_aes : Flag<["-"], "mno-aes">, Group<m_x86_Features_Group>;
def mbmi : Flag<["-"], "mbmi">, Group<m_x86_Features_Group>;
def mno_bmi : Flag<["-"], "mno-bmi">, Group<m_x86_Features_Group>;
def mbmi2 : Flag<["-"], "mbmi2">, Group<m_x86_Features_Group>;
def mno_bmi2 : Flag<["-"], "mno-bmi2">, Group<m_x86_Features_Group>;
def mcldemote : Flag<["-"], "mcldemote">, Group<m_x86_Features_Group>;
def mno_cldemote : Flag<["-"], "mno-cldemote">, Group<m_x86_Features_Group>;
def mclflushopt : Flag<["-"], "mclflushopt">, Group<m_x86_Features_Group>;
def mno_clflushopt : Flag<["-"], "mno-clflushopt">, Group<m_x86_Features_Group>;
def mclwb : Flag<["-"], "mclwb">, Group<m_x86_Features_Group>;
def mno_clwb : Flag<["-"], "mno-clwb">, Group<m_x86_Features_Group>;
def mwbnoinvd : Flag<["-"], "mwbnoinvd">, Group<m_x86_Features_Group>;
def mno_wbnoinvd : Flag<["-"], "mno-wbnoinvd">, Group<m_x86_Features_Group>;
def mclzero : Flag<["-"], "mclzero">, Group<m_x86_Features_Group>;
def mno_clzero : Flag<["-"], "mno-clzero">, Group<m_x86_Features_Group>;
def mcx16 : Flag<["-"], "mcx16">, Group<m_x86_Features_Group>;
def mno_cx16 : Flag<["-"], "mno-cx16">, Group<m_x86_Features_Group>;
def mf16c : Flag<["-"], "mf16c">, Group<m_x86_Features_Group>;
def mno_f16c : Flag<["-"], "mno-f16c">, Group<m_x86_Features_Group>;
def mfma : Flag<["-"], "mfma">, Group<m_x86_Features_Group>;
def mno_fma : Flag<["-"], "mno-fma">, Group<m_x86_Features_Group>;
def mfma4 : Flag<["-"], "mfma4">, Group<m_x86_Features_Group>;
def mno_fma4 : Flag<["-"], "mno-fma4">, Group<m_x86_Features_Group>;
def mfsgsbase : Flag<["-"], "mfsgsbase">, Group<m_x86_Features_Group>;
def mno_fsgsbase : Flag<["-"], "mno-fsgsbase">, Group<m_x86_Features_Group>;
def mfxsr : Flag<["-"], "mfxsr">, Group<m_x86_Features_Group>;
def mno_fxsr : Flag<["-"], "mno-fxsr">, Group<m_x86_Features_Group>;
def minvpcid : Flag<["-"], "minvpcid">, Group<m_x86_Features_Group>;
def mno_invpcid : Flag<["-"], "mno-invpcid">, Group<m_x86_Features_Group>;
def mgfni : Flag<["-"], "mgfni">, Group<m_x86_Features_Group>;
def mno_gfni : Flag<["-"], "mno-gfni">, Group<m_x86_Features_Group>;
def mlwp : Flag<["-"], "mlwp">, Group<m_x86_Features_Group>;
def mno_lwp : Flag<["-"], "mno-lwp">, Group<m_x86_Features_Group>;
def mlzcnt : Flag<["-"], "mlzcnt">, Group<m_x86_Features_Group>;
def mno_lzcnt : Flag<["-"], "mno-lzcnt">, Group<m_x86_Features_Group>;
def mmovbe : Flag<["-"], "mmovbe">, Group<m_x86_Features_Group>;
def mno_movbe : Flag<["-"], "mno-movbe">, Group<m_x86_Features_Group>;
def mmovdiri : Flag<["-"], "mmovdiri">, Group<m_x86_Features_Group>;
def mno_movdiri : Flag<["-"], "mno-movdiri">, Group<m_x86_Features_Group>;
def mmovdir64b : Flag<["-"], "mmovdir64b">, Group<m_x86_Features_Group>;
def mno_movdir64b : Flag<["-"], "mno-movdir64b">, Group<m_x86_Features_Group>;
def mmpx : Flag<["-"], "mmpx">, Group<m_x86_Features_Group>;
def mno_mpx : Flag<["-"], "mno-mpx">, Group<m_x86_Features_Group>;
def mmwaitx : Flag<["-"], "mmwaitx">, Group<m_x86_Features_Group>;
def mno_mwaitx : Flag<["-"], "mno-mwaitx">, Group<m_x86_Features_Group>;
def mpku : Flag<["-"], "mpku">, Group<m_x86_Features_Group>;
def mno_pku : Flag<["-"], "mno-pku">, Group<m_x86_Features_Group>;
def mpclmul : Flag<["-"], "mpclmul">, Group<m_x86_Features_Group>;
def mno_pclmul : Flag<["-"], "mno-pclmul">, Group<m_x86_Features_Group>;
def mpconfig : Flag<["-"], "mpconfig">, Group<m_x86_Features_Group>;
def mno_pconfig : Flag<["-"], "mno-pconfig">, Group<m_x86_Features_Group>;
def mpopcnt : Flag<["-"], "mpopcnt">, Group<m_x86_Features_Group>;
def mno_popcnt : Flag<["-"], "mno-popcnt">, Group<m_x86_Features_Group>;
def mprefetchwt1 : Flag<["-"], "mprefetchwt1">, Group<m_x86_Features_Group>;
def mno_prefetchwt1 : Flag<["-"], "mno-prefetchwt1">, Group<m_x86_Features_Group>;
def mprfchw : Flag<["-"], "mprfchw">, Group<m_x86_Features_Group>;
def mno_prfchw : Flag<["-"], "mno-prfchw">, Group<m_x86_Features_Group>;
def mptwrite : Flag<["-"], "mptwrite">, Group<m_x86_Features_Group>;
def mno_ptwrite : Flag<["-"], "mno-ptwrite">, Group<m_x86_Features_Group>;
def mrdpid : Flag<["-"], "mrdpid">, Group<m_x86_Features_Group>;
def mno_rdpid : Flag<["-"], "mno-rdpid">, Group<m_x86_Features_Group>;
def mrdrnd : Flag<["-"], "mrdrnd">, Group<m_x86_Features_Group>;
def mno_rdrnd : Flag<["-"], "mno-rdrnd">, Group<m_x86_Features_Group>;
def mrtm : Flag<["-"], "mrtm">, Group<m_x86_Features_Group>;
def mno_rtm : Flag<["-"], "mno-rtm">, Group<m_x86_Features_Group>;
def mrdseed : Flag<["-"], "mrdseed">, Group<m_x86_Features_Group>;
def mno_rdseed : Flag<["-"], "mno-rdseed">, Group<m_x86_Features_Group>;
def msahf : Flag<["-"], "msahf">, Group<m_x86_Features_Group>;
def mno_sahf : Flag<["-"], "mno-sahf">, Group<m_x86_Features_Group>;
def msgx : Flag<["-"], "msgx">, Group<m_x86_Features_Group>;
def mno_sgx : Flag<["-"], "mno-sgx">, Group<m_x86_Features_Group>;
def msha : Flag<["-"], "msha">, Group<m_x86_Features_Group>;
def mno_sha : Flag<["-"], "mno-sha">, Group<m_x86_Features_Group>;
def mtbm : Flag<["-"], "mtbm">, Group<m_x86_Features_Group>;
def mno_tbm : Flag<["-"], "mno-tbm">, Group<m_x86_Features_Group>;
def mvaes : Flag<["-"], "mvaes">, Group<m_x86_Features_Group>;
def mno_vaes : Flag<["-"], "mno-vaes">, Group<m_x86_Features_Group>;
def mvpclmulqdq : Flag<["-"], "mvpclmulqdq">, Group<m_x86_Features_Group>;
def mno_vpclmulqdq : Flag<["-"], "mno-vpclmulqdq">, Group<m_x86_Features_Group>;
def mwaitpkg : Flag<["-"], "mwaitpkg">, Group<m_x86_Features_Group>;
def mno_waitpkg : Flag<["-"], "mno-waitpkg">, Group<m_x86_Features_Group>;
def mxop : Flag<["-"], "mxop">, Group<m_x86_Features_Group>;
def mno_xop : Flag<["-"], "mno-xop">, Group<m_x86_Features_Group>;
def mxsave : Flag<["-"], "mxsave">, Group<m_x86_Features_Group>;
def mno_xsave : Flag<["-"], "mno-xsave">, Group<m_x86_Features_Group>;
def mxsavec : Flag<["-"], "mxsavec">, Group<m_x86_Features_Group>;
def mno_xsavec : Flag<["-"], "mno-xsavec">, Group<m_x86_Features_Group>;
def mxsaveopt : Flag<["-"], "mxsaveopt">, Group<m_x86_Features_Group>;
def mno_xsaveopt : Flag<["-"], "mno-xsaveopt">, Group<m_x86_Features_Group>;
def mxsaves : Flag<["-"], "mxsaves">, Group<m_x86_Features_Group>;
def mno_xsaves : Flag<["-"], "mno-xsaves">, Group<m_x86_Features_Group>;
def mshstk : Flag<["-"], "mshstk">, Group<m_x86_Features_Group>;
def mno_shstk : Flag<["-"], "mno-shstk">, Group<m_x86_Features_Group>;
def mretpoline_external_thunk : Flag<["-"], "mretpoline-external-thunk">, Group<m_x86_Features_Group>;
def mno_retpoline_external_thunk : Flag<["-"], "mno-retpoline-external-thunk">, Group<m_x86_Features_Group>;

// These are legacy user-facing driver-level option spellings. They are always
// aliases for options that are spelled using the more common Unix / GNU flag
// style of double-dash and equals-joined flags.
def gcc_toolchain_legacy_spelling : Separate<["-"], "gcc-toolchain">, Alias<gcc_toolchain>;
def target_legacy_spelling : Separate<["-"], "target">, Alias<target>;

// Special internal option to handle -Xlinker --no-demangle.
def Z_Xlinker__no_demangle : Flag<["-"], "Z-Xlinker-no-demangle">,
    Flags<[Unsupported, NoArgumentUnused]>;

// Special internal option to allow forwarding arbitrary arguments to linker.
def Zlinker_input : Separate<["-"], "Zlinker-input">,
    Flags<[Unsupported, NoArgumentUnused]>;

// Reserved library options.
def Z_reserved_lib_stdcxx : Flag<["-"], "Z-reserved-lib-stdc++">,
    Flags<[LinkerInput, NoArgumentUnused, Unsupported]>, Group<reserved_lib_Group>;
def Z_reserved_lib_cckext : Flag<["-"], "Z-reserved-lib-cckext">,
    Flags<[LinkerInput, NoArgumentUnused, Unsupported]>, Group<reserved_lib_Group>;

// Ignored options
// FIXME: multiclasess produce suffixes, not prefixes. This is fine for now
// since it is only used in ignored options.
multiclass BooleanFFlag<string name> {
  def _f : Flag<["-"], "f"#name>;
  def _fno : Flag<["-"], "fno-"#name>;
}

defm : BooleanFFlag<"keep-inline-functions">, Group<clang_ignored_gcc_optimization_f_Group>;

def fprofile_dir : Joined<["-"], "fprofile-dir=">, Group<f_Group>;

def fuse_ld_EQ : Joined<["-"], "fuse-ld=">, Group<f_Group>, Flags<[CoreOption]>;

defm align_labels : BooleanFFlag<"align-labels">, Group<clang_ignored_gcc_optimization_f_Group>;
def falign_labels_EQ : Joined<["-"], "falign-labels=">, Group<clang_ignored_gcc_optimization_f_Group>;
defm align_loops : BooleanFFlag<"align-loops">, Group<clang_ignored_gcc_optimization_f_Group>;
def falign_loops_EQ : Joined<["-"], "falign-loops=">, Group<clang_ignored_gcc_optimization_f_Group>;
defm align_jumps : BooleanFFlag<"align-jumps">, Group<clang_ignored_gcc_optimization_f_Group>;
def falign_jumps_EQ : Joined<["-"], "falign-jumps=">, Group<clang_ignored_gcc_optimization_f_Group>;

// FIXME: This option should be supported and wired up to our diognostics, but
// ignore it for now to avoid breaking builds that use it.
def fdiagnostics_show_location_EQ : Joined<["-"], "fdiagnostics-show-location=">, Group<clang_ignored_f_Group>;

defm fcheck_new : BooleanFFlag<"check-new">, Group<clang_ignored_f_Group>;
defm caller_saves : BooleanFFlag<"caller-saves">, Group<clang_ignored_gcc_optimization_f_Group>;
defm reorder_blocks : BooleanFFlag<"reorder-blocks">, Group<clang_ignored_gcc_optimization_f_Group>;
defm eliminate_unused_debug_types : BooleanFFlag<"eliminate-unused-debug-types">, Group<clang_ignored_f_Group>;
defm branch_count_reg : BooleanFFlag<"branch-count-reg">, Group<clang_ignored_gcc_optimization_f_Group>;
defm default_inline : BooleanFFlag<"default-inline">, Group<clang_ignored_gcc_optimization_f_Group>;
defm fat_lto_objects : BooleanFFlag<"fat-lto-objects">, Group<clang_ignored_gcc_optimization_f_Group>;
defm float_store : BooleanFFlag<"float-store">, Group<clang_ignored_gcc_optimization_f_Group>;
defm friend_injection : BooleanFFlag<"friend-injection">, Group<clang_ignored_f_Group>;
defm function_attribute_list : BooleanFFlag<"function-attribute-list">, Group<clang_ignored_f_Group>;
defm gcse : BooleanFFlag<"gcse">, Group<clang_ignored_gcc_optimization_f_Group>;
defm gcse_after_reload: BooleanFFlag<"gcse-after-reload">, Group<clang_ignored_gcc_optimization_f_Group>;
defm gcse_las: BooleanFFlag<"gcse-las">, Group<clang_ignored_gcc_optimization_f_Group>;
defm gcse_sm: BooleanFFlag<"gcse-sm">, Group<clang_ignored_gcc_optimization_f_Group>;
defm gnu : BooleanFFlag<"gnu">, Group<clang_ignored_f_Group>;
defm implicit_templates : BooleanFFlag<"implicit-templates">, Group<clang_ignored_f_Group>;
defm implement_inlines : BooleanFFlag<"implement-inlines">, Group<clang_ignored_f_Group>;
defm merge_constants : BooleanFFlag<"merge-constants">, Group<clang_ignored_gcc_optimization_f_Group>;
defm modulo_sched : BooleanFFlag<"modulo-sched">, Group<clang_ignored_gcc_optimization_f_Group>;
defm modulo_sched_allow_regmoves : BooleanFFlag<"modulo-sched-allow-regmoves">,
    Group<clang_ignored_gcc_optimization_f_Group>;
defm inline_functions_called_once : BooleanFFlag<"inline-functions-called-once">,
    Group<clang_ignored_gcc_optimization_f_Group>;
def finline_limit_EQ : Joined<["-"], "finline-limit=">, Group<clang_ignored_gcc_optimization_f_Group>;
defm finline_limit : BooleanFFlag<"inline-limit">, Group<clang_ignored_gcc_optimization_f_Group>;
defm inline_small_functions : BooleanFFlag<"inline-small-functions">,
    Group<clang_ignored_gcc_optimization_f_Group>;
defm ipa_cp : BooleanFFlag<"ipa-cp">,
    Group<clang_ignored_gcc_optimization_f_Group>;
defm ivopts : BooleanFFlag<"ivopts">, Group<clang_ignored_gcc_optimization_f_Group>;
defm non_call_exceptions : BooleanFFlag<"non-call-exceptions">, Group<clang_ignored_f_Group>;
defm peel_loops : BooleanFFlag<"peel-loops">, Group<clang_ignored_gcc_optimization_f_Group>;
defm permissive : BooleanFFlag<"permissive">, Group<clang_ignored_f_Group>;
defm prefetch_loop_arrays : BooleanFFlag<"prefetch-loop-arrays">, Group<clang_ignored_gcc_optimization_f_Group>;
defm printf : BooleanFFlag<"printf">, Group<clang_ignored_f_Group>;
defm profile : BooleanFFlag<"profile">, Group<clang_ignored_f_Group>;
defm profile_correction : BooleanFFlag<"profile-correction">, Group<clang_ignored_gcc_optimization_f_Group>;
defm profile_generate_sampling : BooleanFFlag<"profile-generate-sampling">, Group<clang_ignored_f_Group>;
defm profile_reusedist : BooleanFFlag<"profile-reusedist">, Group<clang_ignored_f_Group>;
defm profile_values : BooleanFFlag<"profile-values">, Group<clang_ignored_gcc_optimization_f_Group>;
defm regs_graph : BooleanFFlag<"regs-graph">, Group<clang_ignored_f_Group>;
defm rename_registers : BooleanFFlag<"rename-registers">, Group<clang_ignored_gcc_optimization_f_Group>;
defm ripa : BooleanFFlag<"ripa">, Group<clang_ignored_f_Group>;
defm rounding_math : BooleanFFlag<"rounding-math">, Group<clang_ignored_gcc_optimization_f_Group>;
defm schedule_insns : BooleanFFlag<"schedule-insns">, Group<clang_ignored_gcc_optimization_f_Group>;
defm schedule_insns2 : BooleanFFlag<"schedule-insns2">, Group<clang_ignored_gcc_optimization_f_Group>;
defm see : BooleanFFlag<"see">, Group<clang_ignored_f_Group>;
defm signaling_nans : BooleanFFlag<"signaling-nans">, Group<clang_ignored_gcc_optimization_f_Group>;
defm single_precision_constant : BooleanFFlag<"single-precision-constant">,
    Group<clang_ignored_gcc_optimization_f_Group>;
defm spec_constr_count : BooleanFFlag<"spec-constr-count">, Group<clang_ignored_f_Group>;
defm stack_check : BooleanFFlag<"stack-check">, Group<clang_ignored_f_Group>;
defm strength_reduce :
    BooleanFFlag<"strength-reduce">, Group<clang_ignored_gcc_optimization_f_Group>;
defm tls_model : BooleanFFlag<"tls-model">, Group<clang_ignored_f_Group>;
defm tracer : BooleanFFlag<"tracer">, Group<clang_ignored_gcc_optimization_f_Group>;
defm tree_dce : BooleanFFlag<"tree-dce">, Group<clang_ignored_gcc_optimization_f_Group>;
defm tree_loop_im : BooleanFFlag<"tree_loop_im">,  Group<clang_ignored_gcc_optimization_f_Group>;
defm tree_loop_ivcanon : BooleanFFlag<"tree_loop_ivcanon">,  Group<clang_ignored_gcc_optimization_f_Group>;
defm tree_loop_linear : BooleanFFlag<"tree_loop_linear">,  Group<clang_ignored_gcc_optimization_f_Group>;
defm tree_salias : BooleanFFlag<"tree-salias">, Group<clang_ignored_f_Group>;
defm tree_ter : BooleanFFlag<"tree-ter">, Group<clang_ignored_gcc_optimization_f_Group>;
defm tree_vectorizer_verbose : BooleanFFlag<"tree-vectorizer-verbose">, Group<clang_ignored_f_Group>;
defm tree_vrp : BooleanFFlag<"tree-vrp">, Group<clang_ignored_gcc_optimization_f_Group>;
defm unroll_all_loops : BooleanFFlag<"unroll-all-loops">, Group<clang_ignored_gcc_optimization_f_Group>;
defm unsafe_loop_optimizations : BooleanFFlag<"unsafe-loop-optimizations">,
    Group<clang_ignored_gcc_optimization_f_Group>;
defm unswitch_loops : BooleanFFlag<"unswitch-loops">, Group<clang_ignored_gcc_optimization_f_Group>;
defm use_linker_plugin : BooleanFFlag<"use-linker-plugin">, Group<clang_ignored_gcc_optimization_f_Group>;
defm vect_cost_model : BooleanFFlag<"vect-cost-model">, Group<clang_ignored_gcc_optimization_f_Group>;
defm variable_expansion_in_unroller : BooleanFFlag<"variable-expansion-in-unroller">,
    Group<clang_ignored_gcc_optimization_f_Group>;
defm web : BooleanFFlag<"web">, Group<clang_ignored_gcc_optimization_f_Group>;
defm whole_program : BooleanFFlag<"whole-program">, Group<clang_ignored_gcc_optimization_f_Group>;
defm devirtualize : BooleanFFlag<"devirtualize">, Group<clang_ignored_gcc_optimization_f_Group>;
defm devirtualize_speculatively : BooleanFFlag<"devirtualize-speculatively">,
    Group<clang_ignored_gcc_optimization_f_Group>;

// Generic gfortran options.
def A_DASH : Joined<["-"], "A-">, Group<gfortran_Group>;
def J : JoinedOrSeparate<["-"], "J">, Flags<[RenderJoined]>, Group<gfortran_Group>;
def cpp : Flag<["-"], "cpp">, Group<gfortran_Group>;
def nocpp : Flag<["-"], "nocpp">, Group<gfortran_Group>;
def static_libgfortran : Flag<["-"], "static-libgfortran">, Group<gfortran_Group>;

// "f" options with values for gfortran.
def fblas_matmul_limit_EQ : Joined<["-"], "fblas-matmul-limit=">, Group<gfortran_Group>;
def fcheck_EQ : Joined<["-"], "fcheck=">, Group<gfortran_Group>;
def fcoarray_EQ : Joined<["-"], "fcoarray=">, Group<gfortran_Group>;
def fconvert_EQ : Joined<["-"], "fconvert=">, Group<gfortran_Group>;
def ffixed_line_length_VALUE : Joined<["-"], "ffixed-line-length-">, Group<gfortran_Group>;
def ffpe_trap_EQ : Joined<["-"], "ffpe-trap=">, Group<gfortran_Group>;
def ffree_line_length_VALUE : Joined<["-"], "ffree-line-length-">, Group<gfortran_Group>;
def finit_character_EQ : Joined<["-"], "finit-character=">, Group<gfortran_Group>;
def finit_integer_EQ : Joined<["-"], "finit-integer=">, Group<gfortran_Group>;
def finit_logical_EQ : Joined<["-"], "finit-logical=">, Group<gfortran_Group>;
def finit_real_EQ : Joined<["-"], "finit-real=">, Group<gfortran_Group>;
def fmax_array_constructor_EQ : Joined<["-"], "fmax-array-constructor=">, Group<gfortran_Group>;
def fmax_errors_EQ : Joined<["-"], "fmax-errors=">, Group<gfortran_Group>;
def fmax_stack_var_size_EQ : Joined<["-"], "fmax-stack-var-size=">, Group<gfortran_Group>;
def fmax_subrecord_length_EQ : Joined<["-"], "fmax-subrecord-length=">, Group<gfortran_Group>;
def frecord_marker_EQ : Joined<["-"], "frecord-marker=">, Group<gfortran_Group>;

// "f" flags for gfortran.
defm aggressive_function_elimination : BooleanFFlag<"aggressive-function-elimination">, Group<gfortran_Group>;
defm align_commons : BooleanFFlag<"align-commons">, Group<gfortran_Group>;
defm all_intrinsics : BooleanFFlag<"all-intrinsics">, Group<gfortran_Group>;
defm automatic : BooleanFFlag<"automatic">, Group<gfortran_Group>;
defm backslash : BooleanFFlag<"backslash">, Group<gfortran_Group>;
defm backtrace : BooleanFFlag<"backtrace">, Group<gfortran_Group>;
defm bounds_check : BooleanFFlag<"bounds-check">, Group<gfortran_Group>;
defm check_array_temporaries : BooleanFFlag<"check-array-temporaries">, Group<gfortran_Group>;
defm cray_pointer : BooleanFFlag<"cray-pointer">, Group<gfortran_Group>;
defm d_lines_as_code : BooleanFFlag<"d-lines-as-code">, Group<gfortran_Group>;
defm d_lines_as_comments : BooleanFFlag<"d-lines-as-comments">, Group<gfortran_Group>;
defm default_double_8 : BooleanFFlag<"default-double-8">, Group<gfortran_Group>;
defm default_integer_8 : BooleanFFlag<"default-integer-8">, Group<gfortran_Group>;
defm default_real_8 : BooleanFFlag<"default-real-8">, Group<gfortran_Group>;
defm dollar_ok : BooleanFFlag<"dollar-ok">, Group<gfortran_Group>;
defm dump_fortran_optimized : BooleanFFlag<"dump-fortran-optimized">, Group<gfortran_Group>;
defm dump_fortran_original : BooleanFFlag<"dump-fortran-original">, Group<gfortran_Group>;
defm dump_parse_tree : BooleanFFlag<"dump-parse-tree">, Group<gfortran_Group>;
defm external_blas : BooleanFFlag<"external-blas">, Group<gfortran_Group>;
defm f2c : BooleanFFlag<"f2c">, Group<gfortran_Group>;
defm fixed_form : BooleanFFlag<"fixed-form">, Group<gfortran_Group>;
defm free_form : BooleanFFlag<"free-form">, Group<gfortran_Group>;
defm frontend_optimize : BooleanFFlag<"frontend-optimize">, Group<gfortran_Group>;
defm implicit_none : BooleanFFlag<"implicit-none">, Group<gfortran_Group>;
defm init_local_zero : BooleanFFlag<"init-local-zero">, Group<gfortran_Group>;
defm integer_4_integer_8 : BooleanFFlag<"integer-4-integer-8">, Group<gfortran_Group>;
defm intrinsic_modules_path : BooleanFFlag<"intrinsic-modules-path">, Group<gfortran_Group>;
defm max_identifier_length : BooleanFFlag<"max-identifier-length">, Group<gfortran_Group>;
defm module_private : BooleanFFlag<"module-private">, Group<gfortran_Group>;
defm pack_derived : BooleanFFlag<"pack-derived">, Group<gfortran_Group>;
defm protect_parens : BooleanFFlag<"protect-parens">, Group<gfortran_Group>;
defm range_check : BooleanFFlag<"range-check">, Group<gfortran_Group>;
defm real_4_real_10 : BooleanFFlag<"real-4-real-10">, Group<gfortran_Group>;
defm real_4_real_16 : BooleanFFlag<"real-4-real-16">, Group<gfortran_Group>;
defm real_4_real_8 : BooleanFFlag<"real-4-real-8">, Group<gfortran_Group>;
defm real_8_real_10 : BooleanFFlag<"real-8-real-10">, Group<gfortran_Group>;
defm real_8_real_16 : BooleanFFlag<"real-8-real-16">, Group<gfortran_Group>;
defm real_8_real_4 : BooleanFFlag<"real-8-real-4">, Group<gfortran_Group>;
defm realloc_lhs : BooleanFFlag<"realloc-lhs">, Group<gfortran_Group>;
defm recursive : BooleanFFlag<"recursive">, Group<gfortran_Group>;
defm repack_arrays : BooleanFFlag<"repack-arrays">, Group<gfortran_Group>;
defm second_underscore : BooleanFFlag<"second-underscore">, Group<gfortran_Group>;
defm sign_zero : BooleanFFlag<"sign-zero">, Group<gfortran_Group>;
defm stack_arrays : BooleanFFlag<"stack-arrays">, Group<gfortran_Group>;
defm underscoring : BooleanFFlag<"underscoring">, Group<gfortran_Group>;
defm whole_file : BooleanFFlag<"whole-file">, Group<gfortran_Group>;


include "CC1Options.td"

include "CLCompatOptions.td"
