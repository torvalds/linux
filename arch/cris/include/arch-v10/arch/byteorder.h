#ifndef _CRIS_ARCH_BYTEORDER_H
#define _CRIS_ARCH_BYTEORDER_H

#include <asm/types.h>
#include <linux/compiler.h>

/* we just define these two (as we can do the swap in a single
 * asm instruction in CRIS) and the arch-independent files will put
 * them together into ntohl etc.
 */

static inline __attribute_const__ __u32 ___arch__swab32(__u32 x)
{
	__asm__ ("swapwb %0" : "=r" (x) : "0" (x));
  
	return(x);
}

static inline __attribute_const__ __u16 ___arch__swab16(__u16 x)
{
	__asm__ ("swapb %0" : "=r" (x) : "0" (x));
	
	return(x);
}

#endif
