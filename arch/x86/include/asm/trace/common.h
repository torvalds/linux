#ifndef _ASM_TRACE_COMMON_H
#define _ASM_TRACE_COMMON_H

extern int trace_irq_vector_regfunc(void);
extern void trace_irq_vector_unregfunc(void);

#ifdef CONFIG_TRACING
DECLARE_STATIC_KEY_FALSE(trace_irqvectors_key);
#define trace_irqvectors_enabled()			\
	static_branch_unlikely(&trace_irqvectors_key)
#else
static inline bool trace_irqvectors_enabled(void) { return false; }
#endif

#endif
