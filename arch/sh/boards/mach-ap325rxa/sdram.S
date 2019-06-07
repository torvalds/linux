/* SPDX-License-Identifier: GPL-2.0
 *
 * AP325RXA sdram self/auto-refresh setup code
 *
 *  Copyright (C) 2009 Magnus Damm
 */

#include <linux/sys.h>
#include <linux/errno.h>
#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/suspend.h>
#include <asm/romimage-macros.h>

/* code to enter and leave self-refresh. must be self-contained.
 * this code will be copied to on-chip memory and executed from there.
 */
	.balign 4
ENTRY(ap325rxa_sdram_enter_start)

	/* SBSC: disable power down and put in self-refresh mode */
	mov.l	1f, r4
	mov.l	2f, r1
	mov.l	@r4, r2
	or	r1, r2
	mov.l   3f, r3
	and	r3, r2
	mov.l	r2, @r4

	rts
	 nop

	.balign 4
1:	.long	0xfe400008 /* SDCR0 */
2:	.long	0x00000400
3:	.long	0xffff7fff
ENTRY(ap325rxa_sdram_enter_end)

	.balign 4
ENTRY(ap325rxa_sdram_leave_start)

	/* SBSC: set auto-refresh mode */
	mov.l	1f, r4
	mov.l	@r4, r0
	mov.l   4f, r1
	and	r1, r0
	mov.l	r0, @r4
	mov.l	6f, r4
	mov.l	8f, r0
	mov.l	@r4, r1
	mov	#-1, r4
	add	r4, r1
	or	r1, r0
	mov.l	7f, r1
	mov.l	r0, @r1

	rts
	 nop

	.balign 4
1:	.long	0xfe400008 /* SDCR0 */
4:	.long	0xfffffbff
6:	.long   0xfe40001c /* RTCOR */
7:	.long   0xfe400018 /* RTCNT */
8:	.long   0xa55a0000
ENTRY(ap325rxa_sdram_leave_end)
