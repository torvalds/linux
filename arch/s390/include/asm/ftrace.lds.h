/* SPDX-License-Identifier: GPL-2.0 */
#ifndef DIV_ROUND_UP
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#endif

#define SIZEOF_MCOUNT_LOC_ENTRY 8
#define SIZEOF_FTRACE_HOTPATCH_TRAMPOLINE 24
#define FTRACE_HOTPATCH_TRAMPOLINES_SIZE(n)				       \
	DIV_ROUND_UP(SIZEOF_FTRACE_HOTPATCH_TRAMPOLINE * (n),		       \
		     SIZEOF_MCOUNT_LOC_ENTRY)

#ifdef CONFIG_FUNCTION_TRACER
#define FTRACE_HOTPATCH_TRAMPOLINES_TEXT				       \
	. = ALIGN(8);							       \
	__ftrace_hotpatch_trampolines_start = .;			       \
	. = . + FTRACE_HOTPATCH_TRAMPOLINES_SIZE(__stop_mcount_loc -	       \
						 __start_mcount_loc);	       \
	__ftrace_hotpatch_trampolines_end = .;
#else
#define FTRACE_HOTPATCH_TRAMPOLINES_TEXT
#endif
