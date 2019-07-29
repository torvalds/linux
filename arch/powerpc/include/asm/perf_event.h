/*
 * Performance event support - hardware-specific disambiguation
 *
 * For now this is a compile-time decision, but eventually it should be
 * runtime.  This would allow multiplatform perf event support for e300 (fsl
 * embedded perf counters) plus server/classic, and would accommodate
 * devices other than the core which provide their own performance counters.
 *
 * Copyright 2010 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifdef CONFIG_PPC_PERF_CTRS
#include <asm/perf_event_server.h>
#endif

#ifdef CONFIG_FSL_EMB_PERF_EVENT
#include <asm/perf_event_fsl_emb.h>
#endif

#ifdef CONFIG_PERF_EVENTS
#include <asm/ptrace.h>
#include <asm/reg.h>

#define perf_arch_bpf_user_pt_regs(regs) &regs->user_regs

/*
 * Overload regs->result to specify whether we should use the MSR (result
 * is zero) or the SIAR (result is non zero).
 */
#define perf_arch_fetch_caller_regs(regs, __ip)			\
	do {							\
		(regs)->result = 0;				\
		(regs)->nip = __ip;				\
		(regs)->gpr[1] = current_stack_pointer();	\
		asm volatile("mfmsr %0" : "=r" ((regs)->msr));	\
	} while (0)
#endif
