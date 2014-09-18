#ifndef _ALPHA_SWAB_H
#define _ALPHA_SWAB_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <asm/compiler.h>

#ifdef __GNUC__

static inline __attribute_const__ __u32 __arch_swab32(__u32 x)
{
	/*
	 * Unfortunately, we can't use the 6 instruction sequence
	 * on ev6 since the latency of the UNPKBW is 3, which is
	 * pretty hard to hide.  Just in case a future implementation
	 * has a lower latency, here's the sequence (also by Mike Burrows)
	 *
	 * UNPKBW a0, v0       v0: 00AA00BB00CC00DD
	 * SLL v0, 24, a0      a0: BB00CC00DD000000
	 * BIS v0, a0, a0      a0: BBAACCBBDDCC00DD
	 * EXTWL a0, 6, v0     v0: 000000000000BBAA
	 * ZAP a0, 0xf3, a0    a0: 00000000DDCC0000
	 * ADDL a0, v0, v0     v0: ssssssssDDCCBBAA
	 */

	__u64 t0, t1, t2, t3;

	t0 = __kernel_inslh(x, 7);	/* t0 : 0000000000AABBCC */
	t1 = __kernel_inswl(x, 3);	/* t1 : 000000CCDD000000 */
	t1 |= t0;			/* t1 : 000000CCDDAABBCC */
	t2 = t1 >> 16;			/* t2 : 0000000000CCDDAA */
	t0 = t1 & 0xFF00FF00;		/* t0 : 00000000DD00BB00 */
	t3 = t2 & 0x00FF00FF;		/* t3 : 0000000000CC00AA */
	t1 = t0 + t3;			/* t1 : ssssssssDDCCBBAA */

	return t1;
}
#define __arch_swab32 __arch_swab32

#endif /* __GNUC__ */

#endif /* _ALPHA_SWAB_H */
