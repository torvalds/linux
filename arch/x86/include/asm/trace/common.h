#ifndef _ASM_TRACE_COMMON_H
#define _ASM_TRACE_COMMON_H

#ifdef CONFIG_TRACING
DECLARE_STATIC_KEY_FALSE(trace_pagefault_key);
#define trace_pagefault_enabled()			\
	static_branch_unlikely(&trace_pagefault_key)
#else
static inline bool trace_pagefault_enabled(void) { return false; }
#endif

#endif
