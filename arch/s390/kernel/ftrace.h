/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _FTRACE_H
#define _FTRACE_H

#include <asm/types.h>

struct ftrace_hotpatch_trampoline {
	u16 brasl_opc;
	s32 brasl_disp;
	s16: 16;
	u64 rest_of_intercepted_function;
	u64 interceptor;
} __packed;

extern struct ftrace_hotpatch_trampoline __ftrace_hotpatch_trampolines_start[];
extern struct ftrace_hotpatch_trampoline __ftrace_hotpatch_trampolines_end[];
extern const char ftrace_shared_hotpatch_trampoline_br[];
extern const char ftrace_shared_hotpatch_trampoline_br_end[];
extern const char ftrace_shared_hotpatch_trampoline_exrl[];
extern const char ftrace_shared_hotpatch_trampoline_exrl_end[];
extern const char ftrace_plt_template[];
extern const char ftrace_plt_template_end[];

#endif /* _FTRACE_H */
