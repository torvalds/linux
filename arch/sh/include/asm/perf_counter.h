#ifndef __ASM_SH_PERF_COUNTER_H
#define __ASM_SH_PERF_COUNTER_H

/* SH only supports software counters through this interface. */
#define set_perf_counter_pending()	do { } while (0)

#endif /* __ASM_SH_PERF_COUNTER_H */
