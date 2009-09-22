#ifndef __ASM_SPARC_PERF_EVENT_H
#define __ASM_SPARC_PERF_EVENT_H

extern void set_perf_event_pending(void);

#define	PERF_EVENT_INDEX_OFFSET	0

#ifdef CONFIG_PERF_EVENTS
extern void init_hw_perf_events(void);
#else
static inline void init_hw_perf_events(void)	{ }
#endif

#endif
