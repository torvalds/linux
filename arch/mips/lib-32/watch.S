/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Kernel debug stuff to use the Watch registers.
 * Useful to find stack overflows, dangling pointers etc.
 *
 * Copyright (C) 1995, 1996, 1999 by Ralf Baechle
 */
#include <asm/asm.h>
#include <asm/mipsregs.h>
#include <asm/regdef.h>

		.set	noreorder
/*
 * Parameter: a0 - logic address to watch
 *                 Currently only KSEG0 addresses are allowed!
 *            a1 - set bit #1 to trap on load references
 *                     bit #0 to trap on store references
 * Results  : none
 */
		LEAF(__watch_set)
		li	t0, 0x80000000
		subu	a0, t0
		ori	a0, 7
		xori	a0, 7
		or	a0, a1
		mtc0	a0, CP0_WATCHLO
		sw	a0, watch_savelo

		jr	ra
		 mtc0	zero, CP0_WATCHHI
		END(__watch_set)

/*
 * Parameter: none
 * Results  : none
 */
		LEAF(__watch_clear)
		jr	ra
		 mtc0	zero, CP0_WATCHLO
		END(__watch_clear)

/*
 * Parameter: none
 * Results  : none
 */
		LEAF(__watch_reenable)
		lw	t0, watch_savelo
		jr	ra
		 mtc0	t0, CP0_WATCHLO
		END(__watch_reenable)

/*
 * Saved value of the c0_watchlo register for watch_reenable()
 */
		.data
watch_savelo:	.word	0
		.text
