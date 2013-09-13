#ifndef _ASM_IA64_SWAB_H
#define _ASM_IA64_SWAB_H

/*
 * Modified 1998, 1999
 *	David Mosberger-Tang <davidm@hpl.hp.com>, Hewlett-Packard Co.
 */

#include <linux/types.h>
#include <asm/intrinsics.h>
#include <linux/compiler.h>

static __inline__ __attribute_const__ __u64 __arch_swab64(__u64 x)
{
	__u64 result;

	result = ia64_mux1(x, ia64_mux1_rev);
	return result;
}
#define __arch_swab64 __arch_swab64

static __inline__ __attribute_const__ __u32 __arch_swab32(__u32 x)
{
	return __arch_swab64(x) >> 32;
}
#define __arch_swab32 __arch_swab32

static __inline__ __attribute_const__ __u16 __arch_swab16(__u16 x)
{
	return __arch_swab64(x) >> 48;
}
#define __arch_swab16 __arch_swab16

#endif /* _ASM_IA64_SWAB_H */
