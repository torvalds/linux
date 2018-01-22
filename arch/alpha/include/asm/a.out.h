/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ALPHA_A_OUT_H__
#define __ALPHA_A_OUT_H__

#include <uapi/asm/a.out.h>


/* Assume that start addresses below 4G belong to a TASO application.
   Unfortunately, there is no proper bit in the exec header to check.
   Worse, we have to notice the start address before swapping to use
   /sbin/loader, which of course is _not_ a TASO application.  */
#define SET_AOUT_PERSONALITY(BFPM, EX) \
	set_personality (((BFPM->taso || EX.ah.entry < 0x100000000L \
			   ? ADDR_LIMIT_32BIT : 0) | PER_OSF4))

#endif /* __A_OUT_GNU_H__ */
