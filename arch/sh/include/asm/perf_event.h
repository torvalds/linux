#ifndef __ASM_SH_PERF_EVENT_H
#define __ASM_SH_PERF_EVENT_H

/* SH only supports software events through this interface. */
static inline void set_perf_event_pending(void) {}

#define PERF_EVENT_INDEX_OFFSET	0

#endif /* __ASM_SH_PERF_EVENT_H */
