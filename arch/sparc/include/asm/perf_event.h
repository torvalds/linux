#ifndef __ASM_SPARC_PERF_EVENT_H
#define __ASM_SPARC_PERF_EVENT_H

extern void set_perf_event_pending(void);

#define	PERF_EVENT_INDEX_OFFSET	0

#ifdef CONFIG_PERF_EVENTS
#include <asm/ptrace.h>

extern void init_hw_perf_events(void);

extern void
__perf_arch_fetch_caller_regs(struct pt_regs *regs, unsigned long ip, int skip);

#define perf_arch_fetch_caller_regs(pt_regs, ip)	\
	__perf_arch_fetch_caller_regs(pt_regs, ip, 1);
#else
static inline void init_hw_perf_events(void)	{ }
#endif

#endif
