#ifndef _PARISC_BYTEORDER_H
#define _PARISC_BYTEORDER_H

#include <asm/types.h>
#include <linux/compiler.h>

#ifdef __GNUC__

static __inline__ __attribute_const__ __u16 ___arch__swab16(__u16 x)
{
	__asm__("dep %0, 15, 8, %0\n\t"		/* deposit 00ab -> 0bab */
		"shd %%r0, %0, 8, %0"		/* shift 000000ab -> 00ba */
		: "=r" (x)
		: "0" (x));
	return x;
}

static __inline__ __attribute_const__ __u32 ___arch__swab24(__u32 x)
{
	__asm__("shd %0, %0, 8, %0\n\t"		/* shift xabcxabc -> cxab */
		"dep %0, 15, 8, %0\n\t"		/* deposit cxab -> cbab */
		"shd %%r0, %0, 8, %0"		/* shift 0000cbab -> 0cba */
		: "=r" (x)
		: "0" (x));
	return x;
}

static __inline__ __attribute_const__ __u32 ___arch__swab32(__u32 x)
{
	unsigned int temp;
	__asm__("shd %0, %0, 16, %1\n\t"	/* shift abcdabcd -> cdab */
		"dep %1, 15, 8, %1\n\t"		/* deposit cdab -> cbab */
		"shd %0, %1, 8, %0"		/* shift abcdcbab -> dcba */
		: "=r" (x), "=&r" (temp)
		: "0" (x));
	return x;
}


#if BITS_PER_LONG > 32
/*
** From "PA-RISC 2.0 Architecture", HP Professional Books.
** See Appendix I page 8 , "Endian Byte Swapping".
**
** Pretty cool algorithm: (* == zero'd bits)
**      PERMH   01234567 -> 67452301 into %0
**      HSHL    67452301 -> 7*5*3*1* into %1
**      HSHR    67452301 -> *6*4*2*0 into %0
**      OR      %0 | %1  -> 76543210 into %0 (all done!)
*/
static __inline__ __attribute_const__ __u64 ___arch__swab64(__u64 x) {
	__u64 temp;
	__asm__("permh,3210 %0, %0\n\t"
		"hshl %0, 8, %1\n\t"
		"hshr,u %0, 8, %0\n\t"
		"or %1, %0, %0"
		: "=r" (x), "=&r" (temp)
		: "0" (x));
	return x;
}
#define __arch__swab64(x) ___arch__swab64(x)
#define __BYTEORDER_HAS_U64__
#elif !defined(__STRICT_ANSI__)
static __inline__ __attribute_const__ __u64 ___arch__swab64(__u64 x)
{
	__u32 t1 = ___arch__swab32((__u32) x);
	__u32 t2 = ___arch__swab32((__u32) (x >> 32));
	return (((__u64) t1 << 32) | t2);
}
#define __arch__swab64(x) ___arch__swab64(x)
#define __BYTEORDER_HAS_U64__
#endif

#define __arch__swab16(x) ___arch__swab16(x)
#define __arch__swab24(x) ___arch__swab24(x)
#define __arch__swab32(x) ___arch__swab32(x)

#endif /* __GNUC__ */

#include <linux/byteorder/big_endian.h>

#endif /* _PARISC_BYTEORDER_H */
