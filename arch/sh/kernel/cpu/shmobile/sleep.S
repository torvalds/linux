/*
 * arch/sh/kernel/cpu/sh4a/sleep-sh_mobile.S
 *
 * Sleep mode and Standby modes support for SuperH Mobile
 *
 *  Copyright (C) 2009 Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/sys.h>
#include <linux/errno.h>
#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/suspend.h>

/* manage self-refresh and enter standby mode.
 * this code will be copied to on-chip memory and executed from there.
 */

	.balign 	4096,0,4096
ENTRY(sh_mobile_standby)
	mov	r4, r0

	tst	#SUSP_SH_SF, r0
	bt	skip_set_sf

	/* SDRAM: disable power down and put in self-refresh mode */
	mov.l	1f, r4
	mov.l	2f, r1
	mov.l	@r4, r2
	or	r1, r2
	mov.l   3f, r3
	and	r3, r2
	mov.l	r2, @r4

skip_set_sf:
	tst	#SUSP_SH_SLEEP, r0
	bt	test_standby

	/* set mode to "sleep mode" */
	bra	do_sleep
	 mov	#0x00, r1

test_standby:
	tst	#SUSP_SH_STANDBY, r0
	bt	test_rstandby

	/* set mode to "software standby mode" */
	bra	do_sleep
	 mov	#0x80, r1

test_rstandby:
	tst	#SUSP_SH_RSTANDBY, r0
	bt	test_ustandby

	/* set mode to "r-standby mode" */
	bra	do_sleep
	 mov	#0x20, r1

test_ustandby:
	tst	#SUSP_SH_USTANDBY, r0
	bt	done_sleep

	/* set mode to "u-standby mode" */
	mov	#0x10, r1

	/* fall-through */

do_sleep:
	/* setup and enter selected standby mode */
	mov.l	5f, r4
	mov.l	r1, @r4
	sleep

done_sleep:
	/* reset standby mode to sleep mode */
	mov.l	5f, r4
	mov	#0x00, r1
	mov.l	r1, @r4

	tst	#SUSP_SH_SF, r0
	bt	skip_restore_sf

	/* SDRAM: set auto-refresh mode */
	mov.l	1f, r4
	mov.l	@r4, r2
	mov.l   4f, r3
	and	r3, r2
	mov.l	r2, @r4
	mov.l	6f, r4
	mov.l	7f, r1
	mov.l	8f, r2
	mov.l	@r4, r3
	mov	#-1, r4
	add	r4, r3
	or	r2, r3
	mov.l	r3, @r1
skip_restore_sf:
	rts
	 nop

	.balign 4
1:	.long	0xfe400008 /* SDCR0 */
2:	.long	0x00000400
3:	.long	0xffff7fff
4:	.long	0xfffffbff
5:	.long	0xa4150020 /* STBCR */
6:	.long   0xfe40001c /* RTCOR */
7:	.long   0xfe400018 /* RTCNT */
8:	.long   0xa55a0000

/* interrupt vector @ 0x600 */
	.balign 	0x400,0,0x400
	.long	0xdeadbeef
	.balign 	0x200,0,0x200
	/* sh7722 will end up here in sleep mode */
	rte
	 nop
sh_mobile_standby_end:

ENTRY(sh_mobile_standby_size)
	.long sh_mobile_standby_end - sh_mobile_standby
