//===-- sanitizer_flags.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes common flags available in all sanitizers.
//
//===----------------------------------------------------------------------===//

#ifndef COMMON_FLAG
#error "Define COMMON_FLAG prior to including this file!"
#endif

// COMMON_FLAG(Type, Name, DefaultValue, Description)
// Supported types: bool, const char *, int, uptr.
// Default value must be a compile-time constant.
// Description must be a string literal.

COMMON_FLAG(
    bool, symbolize, true,
    "If set, use the online symbolizer from common sanitizer runtime to turn "
    "virtual addresses to file/line locations.")
COMMON_FLAG(
    const char *, external_symbolizer_path, nullptr,
    "Path to external symbolizer. If empty, the tool will search $PATH for "
    "the symbolizer.")
COMMON_FLAG(
    bool, allow_addr2line, false,
    "If set, allows online symbolizer to run addr2line binary to symbolize "
    "stack traces (addr2line will only be used if llvm-symbolizer binary is "
    "unavailable.")
COMMON_FLAG(const char *, strip_path_prefix, "",
            "Strips this prefix from file paths in error reports.")
COMMON_FLAG(bool, fast_unwind_on_check, false,
            "If available, use the fast frame-pointer-based unwinder on "
            "internal CHECK failures.")
COMMON_FLAG(bool, fast_unwind_on_fatal, false,
            "If available, use the fast frame-pointer-based unwinder on fatal "
            "errors.")
COMMON_FLAG(bool, fast_unwind_on_malloc, true,
            "If available, use the fast frame-pointer-based unwinder on "
            "malloc/free.")
COMMON_FLAG(bool, handle_ioctl, false, "Intercept and handle ioctl requests.")
COMMON_FLAG(int, malloc_context_size, 1,
            "Max number of stack frames kept for each allocation/deallocation.")
COMMON_FLAG(
    const char *, log_path, "stderr",
    "Write logs to \"log_path.pid\". The special values are \"stdout\" and "
    "\"stderr\". The default is \"stderr\".")
COMMON_FLAG(
    bool, log_exe_name, false,
    "Mention name of executable when reporting error and "
    "append executable name to logs (as in \"log_path.exe_name.pid\").")
COMMON_FLAG(
    bool, log_to_syslog, (bool)SANITIZER_ANDROID || (bool)SANITIZER_MAC,
    "Write all sanitizer output to syslog in addition to other means of "
    "logging.")
COMMON_FLAG(
    int, verbosity, 0,
    "Verbosity level (0 - silent, 1 - a bit of output, 2+ - more output).")
COMMON_FLAG(bool, strip_env, 1,
            "Whether to remove the sanitizer from DYLD_INSERT_LIBRARIES to "
            "avoid passing it to children. Default is true.")
COMMON_FLAG(bool, detect_leaks, !SANITIZER_MAC, "Enable memory leak detection.")
COMMON_FLAG(
    bool, leak_check_at_exit, true,
    "Invoke leak checking in an atexit handler. Has no effect if "
    "detect_leaks=false, or if __lsan_do_leak_check() is called before the "
    "handler has a chance to run.")
COMMON_FLAG(bool, allocator_may_return_null, false,
            "If false, the allocator will crash instead of returning 0 on "
            "out-of-memory.")
COMMON_FLAG(bool, print_summary, true,
            "If false, disable printing error summaries in addition to error "
            "reports.")
COMMON_FLAG(int, print_module_map, 0,
            "OS X only (0 - don't print, 1 - print only once before process "
            "exits, 2 - print after each report).")
COMMON_FLAG(bool, check_printf, true, "Check printf arguments.")
#define COMMON_FLAG_HANDLE_SIGNAL_HELP(signal) \
    "Controls custom tool's " #signal " handler (0 - do not registers the " \
    "handler, 1 - register the handler and allow user to set own, " \
    "2 - registers the handler and block user from changing it). "
COMMON_FLAG(HandleSignalMode, handle_segv, kHandleSignalYes,
            COMMON_FLAG_HANDLE_SIGNAL_HELP(SIGSEGV))
COMMON_FLAG(HandleSignalMode, handle_sigbus, kHandleSignalYes,
            COMMON_FLAG_HANDLE_SIGNAL_HELP(SIGBUS))
COMMON_FLAG(HandleSignalMode, handle_abort, kHandleSignalNo,
            COMMON_FLAG_HANDLE_SIGNAL_HELP(SIGABRT))
COMMON_FLAG(HandleSignalMode, handle_sigill, kHandleSignalNo,
            COMMON_FLAG_HANDLE_SIGNAL_HELP(SIGILL))
COMMON_FLAG(HandleSignalMode, handle_sigtrap, kHandleSignalNo,
            COMMON_FLAG_HANDLE_SIGNAL_HELP(SIGTRAP))
COMMON_FLAG(HandleSignalMode, handle_sigfpe, kHandleSignalYes,
            COMMON_FLAG_HANDLE_SIGNAL_HELP(SIGFPE))
#undef COMMON_FLAG_HANDLE_SIGNAL_HELP
COMMON_FLAG(bool, allow_user_segv_handler, true,
            "Deprecated. True has no effect, use handle_sigbus=1. If false, "
            "handle_*=1 will be upgraded to handle_*=2.")
COMMON_FLAG(bool, use_sigaltstack, true,
            "If set, uses alternate stack for signal handling.")
COMMON_FLAG(bool, detect_deadlocks, true,
            "If set, deadlock detection is enabled.")
COMMON_FLAG(
    uptr, clear_shadow_mmap_threshold, 64 * 1024,
    "Large shadow regions are zero-filled using mmap(NORESERVE) instead of "
    "memset(). This is the threshold size in bytes.")
COMMON_FLAG(const char *, color, "auto",
            "Colorize reports: (always|never|auto).")
COMMON_FLAG(
    bool, legacy_pthread_cond, false,
    "Enables support for dynamic libraries linked with libpthread 2.2.5.")
COMMON_FLAG(bool, intercept_tls_get_addr, false, "Intercept __tls_get_addr.")
COMMON_FLAG(bool, help, false, "Print the flag descriptions.")
COMMON_FLAG(uptr, mmap_limit_mb, 0,
            "Limit the amount of mmap-ed memory (excluding shadow) in Mb; "
            "not a user-facing flag, used mosly for testing the tools")
COMMON_FLAG(uptr, hard_rss_limit_mb, 0,
            "Hard RSS limit in Mb."
            " If non-zero, a background thread is spawned at startup"
            " which periodically reads RSS and aborts the process if the"
            " limit is reached")
COMMON_FLAG(uptr, soft_rss_limit_mb, 0,
            "Soft RSS limit in Mb."
            " If non-zero, a background thread is spawned at startup"
            " which periodically reads RSS. If the limit is reached"
            " all subsequent malloc/new calls will fail or return NULL"
            " (depending on the value of allocator_may_return_null)"
            " until the RSS goes below the soft limit."
            " This limit does not affect memory allocations other than"
            " malloc/new.")
COMMON_FLAG(bool, heap_profile, false, "Experimental heap profiler, asan-only")
COMMON_FLAG(s32, allocator_release_to_os_interval_ms,
            ((bool)SANITIZER_FUCHSIA || (bool)SANITIZER_WINDOWS) ? -1 : 5000,
            "Only affects a 64-bit allocator. If set, tries to release unused "
            "memory to the OS, but not more often than this interval (in "
            "milliseconds). Negative values mean do not attempt to release "
            "memory to the OS.\n")
COMMON_FLAG(bool, can_use_proc_maps_statm, true,
            "If false, do not attempt to read /proc/maps/statm."
            " Mostly useful for testing sanitizers.")
COMMON_FLAG(
    bool, coverage, false,
    "If set, coverage information will be dumped at program shutdown (if the "
    "coverage instrumentation was enabled at compile time).")
COMMON_FLAG(const char *, coverage_dir, ".",
            "Target directory for coverage dumps. Defaults to the current "
            "directory.")
COMMON_FLAG(bool, full_address_space, false,
            "Sanitize complete address space; "
            "by default kernel area on 32-bit platforms will not be sanitized")
COMMON_FLAG(bool, print_suppressions, true,
            "Print matched suppressions at exit.")
COMMON_FLAG(
    bool, disable_coredump, (SANITIZER_WORDSIZE == 64) && !SANITIZER_GO,
    "Disable core dumping. By default, disable_coredump=1 on 64-bit to avoid"
    " dumping a 16T+ core file. Ignored on OSes that don't dump core by"
    " default and for sanitizers that don't reserve lots of virtual memory.")
COMMON_FLAG(bool, use_madv_dontdump, true,
          "If set, instructs kernel to not store the (huge) shadow "
          "in core file.")
COMMON_FLAG(bool, symbolize_inline_frames, true,
            "Print inlined frames in stacktraces. Defaults to true.")
COMMON_FLAG(bool, symbolize_vs_style, false,
            "Print file locations in Visual Studio style (e.g: "
            " file(10,42): ...")
COMMON_FLAG(int, dedup_token_length, 0,
            "If positive, after printing a stack trace also print a short "
            "string token based on this number of frames that will simplify "
            "deduplication of the reports. "
            "Example: 'DEDUP_TOKEN: foo-bar-main'. Default is 0.")
COMMON_FLAG(const char *, stack_trace_format, "DEFAULT",
            "Format string used to render stack frames. "
            "See sanitizer_stacktrace_printer.h for the format description. "
            "Use DEFAULT to get default format.")
COMMON_FLAG(bool, no_huge_pages_for_shadow, true,
            "If true, the shadow is not allowed to use huge pages. ")
COMMON_FLAG(bool, strict_string_checks, false,
            "If set check that string arguments are properly null-terminated")
COMMON_FLAG(bool, intercept_strstr, true,
            "If set, uses custom wrappers for strstr and strcasestr functions "
            "to find more errors.")
COMMON_FLAG(bool, intercept_strspn, true,
            "If set, uses custom wrappers for strspn and strcspn function "
            "to find more errors.")
COMMON_FLAG(bool, intercept_strtok, true,
            "If set, uses a custom wrapper for the strtok function "
            "to find more errors.")
COMMON_FLAG(bool, intercept_strpbrk, true,
            "If set, uses custom wrappers for strpbrk function "
            "to find more errors.")
COMMON_FLAG(bool, intercept_strlen, true,
            "If set, uses custom wrappers for strlen and strnlen functions "
            "to find more errors.")
COMMON_FLAG(bool, intercept_strndup, true,
            "If set, uses custom wrappers for strndup functions "
            "to find more errors.")
COMMON_FLAG(bool, intercept_strchr, true,
            "If set, uses custom wrappers for strchr, strchrnul, and strrchr "
            "functions to find more errors.")
COMMON_FLAG(bool, intercept_memcmp, true,
            "If set, uses custom wrappers for memcmp function "
            "to find more errors.")
COMMON_FLAG(bool, strict_memcmp, true,
          "If true, assume that memcmp(p1, p2, n) always reads n bytes before "
          "comparing p1 and p2.")
COMMON_FLAG(bool, intercept_memmem, true,
            "If set, uses a wrapper for memmem() to find more errors.")
COMMON_FLAG(bool, intercept_intrin, true,
            "If set, uses custom wrappers for memset/memcpy/memmove "
            "intrinsics to find more errors.")
COMMON_FLAG(bool, intercept_stat, true,
            "If set, uses custom wrappers for *stat functions "
            "to find more errors.")
COMMON_FLAG(bool, intercept_send, true,
            "If set, uses custom wrappers for send* functions "
            "to find more errors.")
COMMON_FLAG(bool, decorate_proc_maps, false, "If set, decorate sanitizer "
                                             "mappings in /proc/self/maps with "
                                             "user-readable names")
COMMON_FLAG(int, exitcode, 1, "Override the program exit status if the tool "
                              "found an error")
COMMON_FLAG(
    bool, abort_on_error, (bool)SANITIZER_ANDROID || (bool)SANITIZER_MAC,
    "If set, the tool calls abort() instead of _exit() after printing the "
    "error report.")
COMMON_FLAG(bool, suppress_equal_pcs, true,
            "Deduplicate multiple reports for single source location in "
            "halt_on_error=false mode (asan only).")
COMMON_FLAG(bool, print_cmdline, false, "Print command line on crash "
            "(asan only).")
COMMON_FLAG(bool, html_cov_report, false, "Generate html coverage report.")
COMMON_FLAG(const char *, sancov_path, "sancov", "Sancov tool location.")
COMMON_FLAG(bool, dump_instruction_bytes, false,
          "If true, dump 16 bytes starting at the instruction that caused SEGV")
COMMON_FLAG(bool, dump_registers, true,
          "If true, dump values of CPU registers when SEGV happens. Only "
          "available on OS X for now.")
COMMON_FLAG(bool, detect_write_exec, false,
          "If true, triggers warning when writable-executable pages requests "
          "are being made")
COMMON_FLAG(bool, test_only_emulate_no_memorymap, false,
            "TEST ONLY fail to read memory mappings to emulate sanitized "
            "\"init\"")
