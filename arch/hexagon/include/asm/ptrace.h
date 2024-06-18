/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Ptrace definitions for the Hexagon architecture
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_HEXAGON_PTRACE_H
#define _ASM_HEXAGON_PTRACE_H

#include <uapi/asm/ptrace.h>

/* kprobe-based event tracer support */
extern int regs_query_register_offset(const char *name);
extern const char *regs_query_register_name(unsigned int offset);

#define current_pt_regs() \
	((struct pt_regs *) \
	 ((unsigned long)current_thread_info() + THREAD_SIZE) - 1)

#if CONFIG_HEXAGON_ARCH_VERSION >= 4
#define arch_has_single_step()	(1)
#endif

#endif
