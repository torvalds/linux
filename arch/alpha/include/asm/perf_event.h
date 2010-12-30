#ifndef __ASM_ALPHA_PERF_EVENT_H
#define __ASM_ALPHA_PERF_EVENT_H

#ifdef CONFIG_PERF_EVENTS
extern void init_hw_perf_events(void);
#else
static inline void init_hw_perf_events(void)    { }
#endif

#endif /* __ASM_ALPHA_PERF_EVENT_H */
