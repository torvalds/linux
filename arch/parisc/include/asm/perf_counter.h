#ifndef __ASM_PARISC_PERF_COUNTER_H
#define __ASM_PARISC_PERF_COUNTER_H

/* parisc only supports software counters through this interface. */
static inline void set_perf_counter_pending(void) { }

#endif /* __ASM_PARISC_PERF_COUNTER_H */
