#ifndef _CRIS_ARCH_SWAB_H
#define _CRIS_ARCH_SWAB_H

#include <asm/types.h>
#include <linux/compiler.h>

#define __SWAB_64_THRU_32__

/* we just define these two (as we can do the swap in a single
 * asm instruction in CRIS) and the arch-independent files will put
 * them together into ntohl etc.
 */

static inline __attribute_const__ __u32 __arch_swab32(__u32 x)
{
	__asm__ ("swapwb %0" : "=r" (x) : "0" (x));

	return(x);
}
#define __arch_swab32 __arch_swab32

static inline __attribute_const__ __u16 __arch_swab16(__u16 x)
{
	__asm__ ("swapb %0" : "=r" (x) : "0" (x));

	return(x);
}
#define __arch_swab16 __arch_swab16

#endif
