/*
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003  Paul Mundt
 * Copyright (C) 2004  Richard Curnow
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_SH_BL_BIT_64_H
#define __ASM_SH_BL_BIT_64_H

#include <asm/processor.h>

#define SR_BL_LL	0x0000000010000000LL

static inline void set_bl_bit(void)
{
	unsigned long long __dummy0, __dummy1 = SR_BL_LL;

	__asm__ __volatile__("getcon	" __SR ", %0\n\t"
			     "or	%0, %1, %0\n\t"
			     "putcon	%0, " __SR "\n\t"
			     : "=&r" (__dummy0)
			     : "r" (__dummy1));

}

static inline void clear_bl_bit(void)
{
	unsigned long long __dummy0, __dummy1 = ~SR_BL_LL;

	__asm__ __volatile__("getcon	" __SR ", %0\n\t"
			     "and	%0, %1, %0\n\t"
			     "putcon	%0, " __SR "\n\t"
			     : "=&r" (__dummy0)
			     : "r" (__dummy1));
}

#endif /* __ASM_SH_BL_BIT_64_H */
