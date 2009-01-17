#ifndef _ASM_X86_SWAB_H
#define _ASM_X86_SWAB_H

#include <asm/types.h>
#include <linux/compiler.h>

static inline __attribute_const__ __u32 __arch_swab32(__u32 val)
{
#ifdef __i386__
# ifdef CONFIG_X86_BSWAP
	asm("bswap %0" : "=r" (val) : "0" (val));
# else
	asm("xchgb %b0,%h0\n\t"	/* swap lower bytes	*/
	    "rorl $16,%0\n\t"	/* swap words		*/
	    "xchgb %b0,%h0"	/* swap higher bytes	*/
	    : "=q" (val)
	    : "0" (val));
# endif

#else /* __i386__ */
	asm("bswapl %0"
	    : "=r" (val)
	    : "0" (val));
#endif
	return val;
}
#define __arch_swab32 __arch_swab32

static inline __attribute_const__ __u64 __arch_swab64(__u64 val)
{
#ifdef __i386__
	union {
		struct {
			__u32 a;
			__u32 b;
		} s;
		__u64 u;
	} v;
	v.u = val;
# ifdef CONFIG_X86_BSWAP
	asm("bswapl %0 ; bswapl %1 ; xchgl %0,%1"
	    : "=r" (v.s.a), "=r" (v.s.b)
	    : "0" (v.s.a), "1" (v.s.b));
# else
	v.s.a = __arch_swab32(v.s.a);
	v.s.b = __arch_swab32(v.s.b);
	asm("xchgl %0,%1"
	    : "=r" (v.s.a), "=r" (v.s.b)
	    : "0" (v.s.a), "1" (v.s.b));
# endif
	return v.u;
#else /* __i386__ */
	asm("bswapq %0"
	    : "=r" (val)
	    : "0" (val));
	return val;
#endif
}
#define __arch_swab64 __arch_swab64

#endif /* _ASM_X86_SWAB_H */
