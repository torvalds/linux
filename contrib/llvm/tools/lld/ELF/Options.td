include "llvm/Option/OptParser.td"

// For options whose names are multiple letters, either one dash or
// two can precede the option name except those that start with 'o'.
class F<string name>: Flag<["--", "-"], name>;
class J<string name>: Joined<["--", "-"], name>;

multiclass Eq<string name, string help> {
  def NAME: Separate<["--", "-"], name>;
  def NAME # _eq: Joined<["--", "-"], name # "=">, Alias<!cast<Separate>(NAME)>,
    HelpText<help>;
}

multiclass B<string name, string help1, string help2> {
  def NAME: Flag<["--", "-"], name>, HelpText<help1>;
  def no_ # NAME: Flag<["--", "-"], "no-" # name>, HelpText<help2>;
}

defm auxiliary: Eq<"auxiliary", "Set DT_AUXILIARY field to the specified name">;

def Bsymbolic: F<"Bsymbolic">, HelpText<"Bind defined symbols locally">;

def Bsymbolic_functions: F<"Bsymbolic-functions">,
  HelpText<"Bind defined function symbols locally">;

def Bdynamic: F<"Bdynamic">, HelpText<"Link against shared libraries (default)">;

def Bstatic: F<"Bstatic">, HelpText<"Do not link against shared libraries">;

def build_id: F<"build-id">, HelpText<"Alias for --build-id=fast">;

def build_id_eq: J<"build-id=">, HelpText<"Generate build ID note">,
  MetaVarName<"[fast,md5,sha1,uuid,0x<hexstring>]">;

defm check_sections: B<"check-sections",
    "Check section addresses for overlaps (default)",
    "Do not check section addresses for overlaps">;

defm compress_debug_sections:
  Eq<"compress-debug-sections", "Compress DWARF debug sections">,
  MetaVarName<"[none,zlib]">;

defm defsym: Eq<"defsym", "Define a symbol alias">, MetaVarName<"<symbol>=<value>">;

defm split_stack_adjust_size
    : Eq<"split-stack-adjust-size",
         "Specify adjustment to stack size when a split-stack function calls a "
         "non-split-stack function">,
      MetaVarName<"<value>">;

defm library_path:
  Eq<"library-path", "Add a directory to the library search path">, MetaVarName<"<dir>">;

def O: JoinedOrSeparate<["-"], "O">, HelpText<"Optimize output file size">;

defm Tbss: Eq<"Tbss", "Same as --section-start with .bss as the sectionname">;

defm Tdata: Eq<"Tdata", "Same as --section-start with .data as the sectionname">;

defm Ttext: Eq<"Ttext", "Same as --section-start with .text as the sectionname">;

defm allow_multiple_definition: B<"allow-multiple-definition",
    "Allow multiple definitions",
    "Do not allow multiple definitions (default)">;

defm allow_shlib_undefined: B<"allow-shlib-undefined",
    "Allow unresolved references in shared libraries (default when linking a shared library)",
    "Do not allow unresolved references in shared libraries (default when linking an executable)">;

defm apply_dynamic_relocs: B<"apply-dynamic-relocs",
    "Apply link-time values for dynamic relocations",
    "Do not apply link-time values for dynamic relocations (default)">;

defm as_needed: B<"as-needed",
    "Only set DT_NEEDED for shared libraries if used",
    "Always set DT_NEEDED for shared libraries (default)">;

defm call_graph_ordering_file:
  Eq<"call-graph-ordering-file", "Layout sections to optimize the given callgraph">;

defm call_graph_profile_sort: B<"call-graph-profile-sort",
    "Reorder sections with call graph profile (default)",
    "Do not reorder sections with call graph profile">;

// -chroot doesn't have a help text because it is an internal option.
def chroot: Separate<["--", "-"], "chroot">;

def color_diagnostics: F<"color-diagnostics">,
  HelpText<"Alias for --color-diagnostics=always">;

def color_diagnostics_eq: J<"color-diagnostics=">,
  HelpText<"Use colors in diagnostics">,
  MetaVarName<"[auto,always,never]">;

defm cref: B<"cref",
    "Output cross reference table",
    "Do not output cross reference table">;

defm define_common: B<"define-common",
    "Assign space to common symbols",
    "Do not assign space to common symbols">;

defm demangle: B<"demangle",
    "Demangle symbol names (default)",
    "Do not demangle symbol names">;

def disable_new_dtags: F<"disable-new-dtags">,
  HelpText<"Disable new dynamic tags">;

def discard_all: F<"discard-all">, HelpText<"Delete all local symbols">;

def discard_locals: F<"discard-locals">,
  HelpText<"Delete temporary local symbols">;

def discard_none: F<"discard-none">,
  HelpText<"Keep all symbols in the symbol table">;

defm dynamic_linker: Eq<"dynamic-linker", "Which dynamic linker to use">;

defm dynamic_list: Eq<"dynamic-list", "Read a list of dynamic symbols">;

defm eh_frame_hdr: B<"eh-frame-hdr",
    "Request creation of .eh_frame_hdr section and PT_GNU_EH_FRAME segment header",
    "Do not create .eh_frame_hdr section">;

def emit_relocs: F<"emit-relocs">, HelpText<"Generate relocations in output">;

def enable_new_dtags: F<"enable-new-dtags">,
  HelpText<"Enable new dynamic tags (default)">;

def end_group: F<"end-group">,
  HelpText<"Ignored for compatibility with GNU unless you pass --warn-backrefs">;

def end_lib: F<"end-lib">,
  HelpText<"End a grouping of objects that should be treated as if they were together in an archive">;

defm entry: Eq<"entry", "Name of entry point symbol">,
  MetaVarName<"<entry>">;

defm error_limit:
  Eq<"error-limit", "Maximum number of errors to emit before stopping (0 = no limit)">;

def error_unresolved_symbols: F<"error-unresolved-symbols">,
  HelpText<"Report unresolved symbols as errors">;

defm exclude_libs: Eq<"exclude-libs", "Exclude static libraries from automatic export">;

defm execute_only: B<"execute-only",
    "Mark executable sections unreadable",
    "Mark executable sections readable (default)">;

defm export_dynamic: B<"export-dynamic",
    "Put symbols in the dynamic symbol table",
    "Do not put symbols in the dynamic symbol table (default)">;

defm export_dynamic_symbol:
  Eq<"export-dynamic-symbol", "Put a symbol in the dynamic symbol table">;

defm fatal_warnings: B<"fatal-warnings",
    "Treat warnings as errors",
    "Do not treat warnings as errors (default)">;

defm filter: Eq<"filter", "Set DT_FILTER field to the specified name">;

defm fini: Eq<"fini", "Specify a finalizer function">, MetaVarName<"<symbol>">;

def fix_cortex_a53_843419: F<"fix-cortex-a53-843419">,
  HelpText<"Apply fixes for AArch64 Cortex-A53 erratum 843419">;

defm format: Eq<"format", "Change the input format of the inputs following this option">,
  MetaVarName<"[default,elf,binary]">;

defm gc_sections: B<"gc-sections",
    "Enable garbage collection of unused sections",
    "Disable garbage collection of unused sections (default)">;

defm gdb_index: B<"gdb-index",
    "Generate .gdb_index section",
    "Do not generate .gdb_index section (default)">;

defm gnu_unique: B<"gnu-unique",
  "Enable STB_GNU_UNIQUE symbol binding (default)",
  "Disable STB_GNU_UNIQUE symbol binding">;

defm hash_style: Eq<"hash-style", "Specify hash style (sysv, gnu or both)">;

def help: F<"help">, HelpText<"Print option help">;

def icf_all: F<"icf=all">, HelpText<"Enable identical code folding">;

def icf_safe: F<"icf=safe">, HelpText<"Enable safe identical code folding">;

def icf_none: F<"icf=none">, HelpText<"Disable identical code folding (default)">;

def ignore_function_address_equality: F<"ignore-function-address-equality">,
  HelpText<"lld can break the address equality of functions">;

def ignore_data_address_equality: F<"ignore-data-address-equality">,
  HelpText<"lld can break the address equality of data">;

defm image_base: Eq<"image-base", "Set the base address">;

defm init: Eq<"init", "Specify an initializer function">,
  MetaVarName<"<symbol>">;

defm just_symbols: Eq<"just-symbols", "Just link symbols">;

defm keep_unique: Eq<"keep-unique", "Do not fold this symbol during ICF">;

defm library: Eq<"library", "Root name of library to use">,
  MetaVarName<"<libName>">;

def m: JoinedOrSeparate<["-"], "m">, HelpText<"Set target emulation">;

defm Map: Eq<"Map", "Print a link map to the specified file">;

defm merge_exidx_entries: B<"merge-exidx-entries",
    "Enable merging .ARM.exidx entries (default)",
    "Disable merging .ARM.exidx entries">;

def nostdlib: F<"nostdlib">,
  HelpText<"Only search directories specified on the command line">;

def no_color_diagnostics: F<"no-color-diagnostics">,
  HelpText<"Do not use colors in diagnostics">;

def no_dynamic_linker: F<"no-dynamic-linker">,
  HelpText<"Inhibit output of .interp section">;

def noinhibit_exec: F<"noinhibit-exec">,
  HelpText<"Retain the executable output file whenever it is still usable">;

def no_omagic: F<"no-omagic">, MetaVarName<"<magic>">,
  HelpText<"Do not set the text data sections to be writable">;

def no_rosegment: F<"no-rosegment">,
  HelpText<"Do not put read-only non-executable sections in their own segment">;

def no_undefined: F<"no-undefined">,
  HelpText<"Report unresolved symbols even if the linker is creating a shared library">;

def o: JoinedOrSeparate<["-"], "o">, MetaVarName<"<path>">,
  HelpText<"Path to file to write output">;

def oformat: Separate<["--"], "oformat">, MetaVarName<"<format>">,
  HelpText<"Specify the binary format for the output object file">;

def omagic: Flag<["--"], "omagic">, MetaVarName<"<magic>">,
  HelpText<"Set the text and data sections to be readable and writable">;

defm orphan_handling:
  Eq<"orphan-handling", "Control how orphan sections are handled when linker script used">;

defm pack_dyn_relocs:
  Eq<"pack-dyn-relocs", "Pack dynamic relocations in the given format">,
  MetaVarName<"[none,android,relr,android+relr]">;

defm use_android_relr_tags: B<"use-android-relr-tags",
    "Use SHT_ANDROID_RELR / DT_ANDROID_RELR* tags instead of SHT_RELR / DT_RELR*",
    "Use SHT_RELR / DT_RELR* tags (default)">;

def pic_veneer: F<"pic-veneer">,
  HelpText<"Always generate position independent thunks (veneers)">;

defm pie: B<"pie",
    "Create a position independent executable",
    "Do not create a position independent executable (default)">;

defm print_gc_sections: B<"print-gc-sections",
    "List removed unused sections",
    "Do not list removed unused sections (default)">;

defm print_icf_sections: B<"print-icf-sections",
    "List identical folded sections",
    "Do not list identical folded sections (default)">;

def pop_state: F<"pop-state">,
  HelpText<"Undo the effect of -push-state">;

def push_state: F<"push-state">,
  HelpText<"Save the current state of -as-needed, -static and -whole-archive">;

def print_map: F<"print-map">,
  HelpText<"Print a link map to the standard output">;

defm reproduce: Eq<"reproduce", "Dump linker invocation and input files for debugging">;

defm rpath: Eq<"rpath", "Add a DT_RUNPATH to the output">;

def relocatable: F<"relocatable">, HelpText<"Create relocatable object file">;

defm retain_symbols_file:
  Eq<"retain-symbols-file", "Retain only the symbols listed in the file">,
  MetaVarName<"<file>">;

defm script: Eq<"script", "Read linker script">;

defm section_start: Eq<"section-start", "Set address of section">,
  MetaVarName<"<address>">;

def shared: F<"shared">, HelpText<"Build a shared object">;

defm soname: Eq<"soname", "Set DT_SONAME">;

defm sort_section:
  Eq<"sort-section", "Specifies sections sorting rule when linkerscript is used">;

def start_group: F<"start-group">,
  HelpText<"Ignored for compatibility with GNU unless you pass --warn-backrefs">;

def start_lib: F<"start-lib">,
  HelpText<"Start a grouping of objects that should be treated as if they were together in an archive">;

def strip_all: F<"strip-all">, HelpText<"Strip all symbols">;

def strip_debug: F<"strip-debug">, HelpText<"Strip debugging information">;

defm symbol_ordering_file:
  Eq<"symbol-ordering-file", "Layout sections to place symbols in the order specified by symbol ordering file">;

defm sysroot: Eq<"sysroot", "Set the system root">;

def target1_rel: F<"target1-rel">, HelpText<"Interpret R_ARM_TARGET1 as R_ARM_REL32">;

def target1_abs: F<"target1-abs">, HelpText<"Interpret R_ARM_TARGET1 as R_ARM_ABS32 (default)">;

defm target2:
  Eq<"target2", "Interpret R_ARM_TARGET2 as <type>, where <type> is one of rel, abs, or got-rel">,
  MetaVarName<"<type>">;

defm threads: B<"threads",
    "Run the linker multi-threaded (default)",
    "Do not run the linker multi-threaded">;

defm toc_optimize : B<"toc-optimize",
    "(PowerPC64) Enable TOC related optimizations (default)",
    "(PowerPC64) Disable TOC related optimizations">;

def trace: F<"trace">, HelpText<"Print the names of the input files">;

defm trace_symbol: Eq<"trace-symbol", "Trace references to symbols">;

defm undefined: Eq<"undefined", "Force undefined symbol during linking">,
  MetaVarName<"<symbol>">;

defm unresolved_symbols:
  Eq<"unresolved-symbols", "Determine how to handle unresolved symbols">;

defm undefined_version: B<"undefined-version",
  "Allow unused version in version script (default)",
  "Report version scripts that refer undefined symbols">;

defm rsp_quoting: Eq<"rsp-quoting", "Quoting style for response files">,
  MetaVarName<"[posix,windows]">;

def v: Flag<["-"], "v">, HelpText<"Display the version number">;

def verbose: F<"verbose">, HelpText<"Verbose mode">;

def version: F<"version">, HelpText<"Display the version number and exit">;

defm version_script: Eq<"version-script", "Read a version script">;

defm warn_backrefs: B<"warn-backrefs",
    "Warn about backward symbol references to fetch archive members",
    "Do not warn about backward symbol references to fetch archive members (default)">;

defm warn_common: B<"warn-common",
    "Warn about duplicate common symbols",
    "Do not warn about duplicate common symbols (default)">;

defm warn_ifunc_textrel: B<"warn-ifunc-textrel",
    "Warn about using ifunc symbols with text relocations",
    "Do not warn about using ifunc symbols with text relocations (default)">;

defm warn_symbol_ordering: B<"warn-symbol-ordering",
    "Warn about problems with the symbol ordering file (default)",
    "Do not warn about problems with the symbol ordering file">;

def warn_unresolved_symbols: F<"warn-unresolved-symbols">,
  HelpText<"Report unresolved symbols as warnings">;

defm whole_archive: B<"whole-archive",
    "Force load of all members in a static library",
    "Do not force load of all members in a static library (default)">;

defm wrap: Eq<"wrap", "Use wrapper functions for symbol">,
  MetaVarName<"<symbol>=<symbol>">;

def z: JoinedOrSeparate<["-"], "z">, MetaVarName<"<option>">,
  HelpText<"Linker option extensions">;

// Aliases
def: Separate<["-"], "f">, Alias<auxiliary>, HelpText<"Alias for --auxiliary">;
def: F<"call_shared">, Alias<Bdynamic>, HelpText<"Alias for --Bdynamic">;
def: F<"dy">, Alias<Bdynamic>, HelpText<"Alias for --Bdynamic">;
def: F<"dn">, Alias<Bstatic>, HelpText<"Alias for --Bstatic">;
def: F<"non_shared">, Alias<Bstatic>, HelpText<"Alias for --Bstatic">;
def: F<"static">, Alias<Bstatic>, HelpText<"Alias for --Bstatic">;
def: Flag<["-"], "d">, Alias<define_common>, HelpText<"Alias for --define-common">;
def: F<"dc">, Alias<define_common>, HelpText<"Alias for --define-common">;
def: F<"dp">, Alias<define_common>, HelpText<"Alias for --define-common">;
def: Flag<["-"], "x">, Alias<discard_all>, HelpText<"Alias for --discard-all">;
def: Flag<["-"], "X">, Alias<discard_locals>, HelpText<"Alias for --discard-locals">;
def: Flag<["-"], "q">, Alias<emit_relocs>, HelpText<"Alias for --emit-relocs">;
def: Flag<["-"], ")">, Alias<end_group>, HelpText<"Alias for --end-group">;
def: JoinedOrSeparate<["-"], "e">, Alias<entry>, HelpText<"Alias for --entry">;
def: Flag<["-"], "E">, Alias<export_dynamic>, HelpText<"Alias for --export-dynamic">;
def: Separate<["-"], "F">, Alias<filter>, HelpText<"Alias for --filter">;
def: Separate<["-"], "b">, Alias<format>, HelpText<"Alias for --format">;
def: JoinedOrSeparate<["-"], "l">, Alias<library>, HelpText<"Alias for --library">;
def: JoinedOrSeparate<["-"], "L">, Alias<library_path>, HelpText<"Alias for --library-path">;
def: F<"no-pic-executable">, Alias<no_pie>, HelpText<"Alias for --no-pie">;
def: Flag<["-"], "N">, Alias<omagic>, HelpText<"Alias for --omagic">;
def: Joined<["--"], "output=">, Alias<o>, HelpText<"Alias for -o">;
def: Separate<["--"], "output">, Alias<o>, HelpText<"Alias for -o">;
def: F<"pic-executable">, Alias<pie>, HelpText<"Alias for --pie">;
def: Flag<["-"], "M">, Alias<print_map>, HelpText<"Alias for --print-map">;
def: Flag<["-"], "r">, Alias<relocatable>, HelpText<"Alias for --relocatable">;
def: JoinedOrSeparate<["-"], "R">, Alias<rpath>, HelpText<"Alias for --rpath">;
def: JoinedOrSeparate<["-"], "T">, Alias<script>, HelpText<"Alias for --script">;
def: F<"Bshareable">, Alias<shared>, HelpText<"Alias for --shared">;
def: JoinedOrSeparate<["-"], "h">, Alias<soname>, HelpText<"Alias for --soname">;
def: Flag<["-"], "(">, Alias<start_group>, HelpText<"Alias for --start-group">;
def: Flag<["-"], "s">, Alias<strip_all>, HelpText<"Alias for --strip-all">;
def: Flag<["-"], "S">, Alias<strip_debug>, HelpText<"Alias for --strip-debug">;
def: Flag<["-"], "t">, Alias<trace>, HelpText<"Alias for --trace">;
def: JoinedOrSeparate<["-"], "y">, Alias<trace_symbol>, HelpText<"Alias for --trace-symbol">;
def: Separate<["-", "--"], "Ttext-segment">, Alias<Ttext>, HelpText<"Alias for --Ttext">;
def: Joined<["-", "--"], "Ttext-segment=">, Alias<Ttext>, HelpText<"Alias for --Ttext">;
def: JoinedOrSeparate<["-"], "u">, Alias<undefined>, HelpText<"Alias for --undefined">;
def: Flag<["-"], "V">, Alias<version>, HelpText<"Alias for --version">;

// LTO-related options.
def lto_aa_pipeline: J<"lto-aa-pipeline=">,
  HelpText<"AA pipeline to run during LTO. Used in conjunction with -lto-newpm-passes">;
def lto_debug_pass_manager: F<"lto-debug-pass-manager">,
  HelpText<"Debug new pass manager">;
def lto_new_pass_manager: F<"lto-new-pass-manager">,
  HelpText<"Use new pass manager">;
def lto_newpm_passes: J<"lto-newpm-passes=">,
  HelpText<"Passes to run during LTO">;
def lto_O: J<"lto-O">, MetaVarName<"<opt-level>">,
  HelpText<"Optimization level for LTO">;
def lto_partitions: J<"lto-partitions=">,
  HelpText<"Number of LTO codegen partitions">;
def lto_sample_profile: J<"lto-sample-profile=">,
  HelpText<"Sample profile file path">;
def disable_verify: F<"disable-verify">;
defm mllvm: Eq<"mllvm", "Additional arguments to forward to LLVM's option processing">;
def opt_remarks_filename: Separate<["--"], "opt-remarks-filename">,
  HelpText<"YAML output file for optimization remarks">;
def opt_remarks_with_hotness: Flag<["--"], "opt-remarks-with-hotness">,
  HelpText<"Include hotness information in the optimization remarks file">;
defm plugin_opt: Eq<"plugin-opt", "specifies LTO options for compatibility with GNU linkers">;
def save_temps: F<"save-temps">;
def thinlto_cache_dir: J<"thinlto-cache-dir=">,
  HelpText<"Path to ThinLTO cached object file directory">;
defm thinlto_cache_policy: Eq<"thinlto-cache-policy", "Pruning policy for the ThinLTO cache">;
def thinlto_jobs: J<"thinlto-jobs=">, HelpText<"Number of ThinLTO jobs">;

def: J<"plugin-opt=O">, Alias<lto_O>, HelpText<"Alias for -lto-O">;
def: F<"plugin-opt=debug-pass-manager">,
  Alias<lto_debug_pass_manager>, HelpText<"Alias for -lto-debug-pass-manager">;
def: F<"plugin-opt=disable-verify">, Alias<disable_verify>, HelpText<"Alias for -disable-verify">;
def plugin_opt_dwo_dir_eq: J<"plugin-opt=dwo_dir=">,
  HelpText<"Directory to store .dwo files when LTO and debug fission are used">;
def plugin_opt_emit_llvm: F<"plugin-opt=emit-llvm">;
def: J<"plugin-opt=jobs=">, Alias<thinlto_jobs>, HelpText<"Alias for -thinlto-jobs">;
def: J<"plugin-opt=lto-partitions=">, Alias<lto_partitions>, HelpText<"Alias for -lto-partitions">;
def plugin_opt_mcpu_eq: J<"plugin-opt=mcpu=">;
def: F<"plugin-opt=new-pass-manager">,
  Alias<lto_new_pass_manager>, HelpText<"Alias for -lto-new-pass-manager">;
def plugin_opt_obj_path_eq: J<"plugin-opt=obj-path=">;
def: J<"plugin-opt=sample-profile=">,
  Alias<lto_sample_profile>, HelpText<"Alias for -lto-sample-profile">;
def: F<"plugin-opt=save-temps">, Alias<save_temps>, HelpText<"Alias for -save-temps">;
def plugin_opt_thinlto_emit_imports_files: F<"plugin-opt=thinlto-emit-imports-files">;
def plugin_opt_thinlto_index_only: F<"plugin-opt=thinlto-index-only">;
def plugin_opt_thinlto_index_only_eq: J<"plugin-opt=thinlto-index-only=">;
def plugin_opt_thinlto_object_suffix_replace_eq: J<"plugin-opt=thinlto-object-suffix-replace=">;
def plugin_opt_thinlto_prefix_replace_eq: J<"plugin-opt=thinlto-prefix-replace=">;

// Ignore LTO plugin-related options.
// clang -flto passes -plugin and -plugin-opt to the linker. This is required
// for ld.gold and ld.bfd to get LTO working. But it's not for lld which doesn't
// rely on a plugin. Instead of detecting which linker is used on clang side we
// just ignore the option on lld side as it's easier. In fact, the linker could
// be called 'ld' and understanding which linker is used would require parsing of
// --version output.
defm plugin: Eq<"plugin", "Ignored for compatibility with GNU linkers">;

def plugin_opt_fresolution_eq: J<"plugin-opt=-fresolution=">;
def plugin_opt_pass_through_eq: J<"plugin-opt=-pass-through=">;
def plugin_opt_thinlto: J<"plugin-opt=thinlto">;
def plugin_opt_slash: J<"plugin-opt=/">;

// Options listed below are silently ignored for now for compatibility.
def: F<"detect-odr-violations">;
def: Flag<["-"], "g">;
def: F<"long-plt">;
def: F<"no-add-needed">;
def: F<"no-copy-dt-needed-entries">;
def: F<"no-ctors-in-init-array">;
def: F<"no-keep-memory">;
def: F<"no-mmap-output-file">;
def: F<"no-warn-mismatch">;
def: Separate<["--", "-"], "rpath-link">;
def: J<"rpath-link=">;
def: F<"sort-common">;
def: F<"stats">;
def: F<"warn-execstack">;
def: F<"warn-once">;
def: F<"warn-shared-textrel">;
def: F<"EB">;
def: F<"EL">;
def: JoinedOrSeparate<["-"], "G">;
def: F<"Qy">;

// Hidden option used for testing MIPS multi-GOT implementation.
defm mips_got_size:
  Eq<"mips-got-size", "Max size of a single MIPS GOT. 0x10000 by default.">,
  Flags<[HelpHidden]>;
