#ifndef __ASM_SH_CMPXCHG_CAS_H
#define __ASM_SH_CMPXCHG_CAS_H

static inline unsigned long
__cmpxchg_u32(volatile u32 *m, unsigned long old, unsigned long new)
{
	__asm__ __volatile__("cas.l %1,%0,@r0"
		: "+r"(new)
		: "r"(old), "z"(m)
		: "t", "memory" );
	return new;
}

static inline unsigned long xchg_u32(volatile u32 *m, unsigned long val)
{
	unsigned long old;
	do old = *m;
	while (__cmpxchg_u32(m, old, val) != old);
	return old;
}

#include <asm/cmpxchg-xchg.h>

#endif /* __ASM_SH_CMPXCHG_CAS_H */
