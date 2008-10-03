#ifndef _M68KNOMMU_BYTEORDER_H
#define _M68KNOMMU_BYTEORDER_H

#include <linux/types.h>

#if defined(__GNUC__) && !defined(__STRICT_ANSI__) || defined(__KERNEL__)
#  define __BYTEORDER_HAS_U64__
#  define __SWAB_64_THRU_32__
#endif

#if defined (__mcfisaaplus__) || defined (__mcfisac__)
static inline __attribute_const__ __u32 ___arch__swab32(__u32 val)
{
	asm(
			"byterev %0"
			: "=d" (val)
			: "0" (val)
	   );
	return val;
}

#define __arch__swab32(x) ___arch__swab32(x)
#endif

#include <linux/byteorder/big_endian.h>

#endif /* _M68KNOMMU_BYTEORDER_H */
