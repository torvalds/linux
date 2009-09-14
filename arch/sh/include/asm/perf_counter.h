#ifndef __ASM_SH_PERF_COUNTER_H
#define __ASM_SH_PERF_COUNTER_H

/* SH only supports software counters through this interface. */
static inline void set_perf_counter_pending(void) {}

#define PERF_COUNTER_INDEX_OFFSET	0

#endif /* __ASM_SH_PERF_COUNTER_H */
