#ifndef _ASM_X86_PM_TRACE_H
#define _ASM_X86_PM_TRACE_H

#include <asm/asm.h>

#define TRACE_RESUME(user)					\
do {								\
	if (pm_trace_enabled) {					\
		const void *tracedata;				\
		asm volatile(_ASM_MOV " $1f,%0\n"		\
			     ".section .tracedata,\"a\"\n"	\
			     "1:\t.word %c1\n\t"		\
			     _ASM_PTR " %c2\n"			\
			     ".previous"			\
			     :"=r" (tracedata)			\
			     : "i" (__LINE__), "i" (__FILE__));	\
		generate_pm_trace(tracedata, user);		\
	}							\
} while (0)

#define TRACE_SUSPEND(user)	TRACE_RESUME(user)

#endif /* _ASM_X86_PM_TRACE_H */
