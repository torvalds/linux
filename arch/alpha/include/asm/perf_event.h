#ifndef __ASM_ALPHA_PERF_EVENT_H
#define __ASM_ALPHA_PERF_EVENT_H

/*
 * Software events have no trap frame, so perf_fetch_caller_regs() fills
 * in a zeroed pt_regs. A zero ps already means kernel mode; only pc is
 * missing.
 */
#define perf_arch_fetch_caller_regs(regs, __ip)	((regs)->pc = (__ip))

#endif /* __ASM_ALPHA_PERF_EVENT_H */
