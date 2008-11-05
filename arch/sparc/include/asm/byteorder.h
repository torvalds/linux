#ifndef _SPARC_BYTEORDER_H
#define _SPARC_BYTEORDER_H

#include <asm/types.h>
#include <asm/asi.h>

#define __BIG_ENDIAN

#ifdef CONFIG_SPARC32
#define __SWAB_64_THRU_32__
#endif

#ifdef CONFIG_SPARC64
static inline __u16 __arch_swab16p(const __u16 *addr)
{
	__u16 ret;

	__asm__ __volatile__ ("lduha [%1] %2, %0"
			      : "=r" (ret)
			      : "r" (addr), "i" (ASI_PL));
	return ret;
}
#define __arch_swab16p __arch_swab16p

static inline __u32 __arch_swab32p(const __u32 *addr)
{
	__u32 ret;

	__asm__ __volatile__ ("lduwa [%1] %2, %0"
			      : "=r" (ret)
			      : "r" (addr), "i" (ASI_PL));
	return ret;
}
#define __arch_swab32p __arch_swab32p

static inline __u64 __arch_swab64p(const __u64 *addr)
{
	__u64 ret;

	__asm__ __volatile__ ("ldxa [%1] %2, %0"
			      : "=r" (ret)
			      : "r" (addr), "i" (ASI_PL));
	return ret;
}
#define __arch_swab64p __arch_swab64p

#endif /* CONFIG_SPARC64 */

#include <linux/byteorder.h>

#endif /* _SPARC_BYTEORDER_H */
