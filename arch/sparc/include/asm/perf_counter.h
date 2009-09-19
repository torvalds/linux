#ifndef __ASM_SPARC_PERF_COUNTER_H
#define __ASM_SPARC_PERF_COUNTER_H

extern void set_perf_counter_pending(void);

#define	PERF_COUNTER_INDEX_OFFSET	0

#ifdef CONFIG_PERF_COUNTERS
extern void init_hw_perf_counters(void);
#else
static inline void init_hw_perf_counters(void)	{ }
#endif

#endif
