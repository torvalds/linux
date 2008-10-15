/*
 * AVR32 endian-conversion functions.
 */
#ifndef __ASM_AVR32_BYTEORDER_H
#define __ASM_AVR32_BYTEORDER_H

#include <asm/types.h>
#include <linux/compiler.h>

#define __BIG_ENDIAN
#define __SWAB_64_THRU_32__

#ifdef __CHECKER__
extern unsigned long __builtin_bswap_32(unsigned long x);
extern unsigned short __builtin_bswap_16(unsigned short x);
#endif

/*
 * avr32-linux-gcc versions earlier than 4.2 improperly sign-extends
 * the result.
 */
#if !(__GNUC__ == 4 && __GNUC_MINOR__ < 2)
static inline __attribute_const__ __u16 __arch_swab16(__u16 val)
{
	return __builtin_bswap_16(val);
}
#define __arch_swab16 __arch_swab16

static inline __attribute_const__ __u32 __arch_swab32(__u32 val)
{
	return __builtin_bswap_32(val);
}
#define __arch_swab32 __arch_swab32
#endif

#include <linux/byteorder.h>
#endif /* __ASM_AVR32_BYTEORDER_H */
