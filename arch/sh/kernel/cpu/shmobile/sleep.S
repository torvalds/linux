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

/*
 * Kernel mode register usage, see entry.S:
 *	k0	scratch
 *	k1	scratch
 *	k4	scratch
 */
#define k0	r0
#define k1	r1
#define k4	r4

/* manage self-refresh and enter standby mode.
 * this code will be copied to on-chip memory and executed from there.
 */

	.balign 	4096,0,4096
ENTRY(sh_mobile_standby)

	/* save original vbr */
	stc	vbr, r1
	mova	saved_vbr, r0
	mov.l	r1, @r0

	/* point vbr to our on-chip memory page */
	ldc	r5, vbr

	/* save return address */
	mova	saved_spc, r0
	sts	pr, r5
	mov.l	r5, @r0

	/* save sr */
	mova	saved_sr, r0
	stc	sr, r5
	mov.l	r5, @r0

	/* save mode flags */
	mova	saved_mode, r0
	mov.l	r4, @r0

	/* put mode flags in r0 */
	mov	r4, r0

	tst	#SUSP_SH_SF, r0
	bt	skip_set_sf
#ifdef CONFIG_CPU_SUBTYPE_SH7724
	/* DBSC: put memory in self-refresh mode */
	mov.l	dben_reg, r4
	mov.l	dben_data0, r1
	mov.l	r1, @r4

	mov.l	dbrfpdn0_reg, r4
	mov.l	dbrfpdn0_data0, r1
	mov.l	r1, @r4

	mov.l	dbcmdcnt_reg, r4
	mov.l	dbcmdcnt_data0, r1
	mov.l	r1, @r4

	mov.l	dbcmdcnt_reg, r4
	mov.l	dbcmdcnt_data1, r1
	mov.l	r1, @r4

	mov.l	dbrfpdn0_reg, r4
	mov.l	dbrfpdn0_data1, r1
	mov.l	r1, @r4
#else
	/* SBSC: disable power down and put in self-refresh mode */
	mov.l	1f, r4
	mov.l	2f, r1
	mov.l	@r4, r2
	or	r1, r2
	mov.l   3f, r3
	and	r3, r2
	mov.l	r2, @r4
#endif

skip_set_sf:
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
	bt	force_sleep

	/* set mode to "u-standby mode" */
	bra	do_sleep
	 mov	#0x10, r1

force_sleep:

	/* set mode to "sleep mode" */
	mov	#0x00, r1

do_sleep:
	/* setup and enter selected standby mode */
	mov.l	5f, r4
	mov.l	r1, @r4
again:
	sleep
	bra	again
	 nop

restore_jump_vbr:
	/* setup spc with return address to c code */
	mov.l	saved_spc, k0
	ldc	k0, spc

	/* restore vbr */
	mov.l	saved_vbr, k0
	ldc	k0, vbr

	/* setup ssr with saved sr */
	mov.l	saved_sr, k0
	ldc	k0, ssr

	/* get mode flags */
	mov.l	saved_mode, k0

done_sleep:
	/* reset standby mode to sleep mode */
	mov.l	5f, k4
	mov	#0x00, k1
	mov.l	k1, @k4

	tst	#SUSP_SH_SF, k0
	bt	skip_restore_sf

#ifdef CONFIG_CPU_SUBTYPE_SH7724
	/* DBSC: put memory in auto-refresh mode */
	mov.l	dbrfpdn0_reg, k4
	mov.l	dbrfpdn0_data0, k1
	mov.l	k1, @k4

	nop /* sleep 140 ns */
	nop
	nop
	nop

	mov.l	dbcmdcnt_reg, k4
	mov.l	dbcmdcnt_data0, k1
	mov.l	k1, @k4

	mov.l	dbcmdcnt_reg, k4
	mov.l	dbcmdcnt_data1, k1
	mov.l	k1, @k4

	mov.l	dben_reg, k4
	mov.l	dben_data1, k1
	mov.l	k1, @k4

	mov.l	dbrfpdn0_reg, k4
	mov.l	dbrfpdn0_data2, k1
	mov.l	k1, @k4
#else
	/* SBSC: set auto-refresh mode */
	mov.l	1f, k4
	mov.l	@k4, k0
	mov.l   4f, k1
	and	k1, k0
	mov.l	k0, @k4
	mov.l	6f, k4
	mov.l	8f, k0
	mov.l	@k4, k1
	mov	#-1, k4
	add	k4, k1
	or	k1, k0
	mov.l	7f, k1
	mov.l	k0, @k1
#endif
skip_restore_sf:
	/* jump to vbr vector */
	mov.l	saved_vbr, k0
	mov.l	offset_vbr, k4
	add	k4, k0
	jmp	@k0
	 nop

	.balign 4
saved_mode:	.long	0
saved_spc:	.long	0
saved_sr:	.long	0
saved_vbr:	.long	0
offset_vbr:	.long	0x600
#ifdef CONFIG_CPU_SUBTYPE_SH7724
dben_reg:	.long	0xfd000010 /* DBEN */
dben_data0:	.long	0
dben_data1:	.long	1
dbrfpdn0_reg:	.long	0xfd000040 /* DBRFPDN0 */
dbrfpdn0_data0:	.long	0
dbrfpdn0_data1:	.long	1
dbrfpdn0_data2:	.long	0x00010000
dbcmdcnt_reg:	.long	0xfd000014 /* DBCMDCNT */
dbcmdcnt_data0:	.long	2
dbcmdcnt_data1:	.long	4
#else
1:	.long	0xfe400008 /* SDCR0 */
2:	.long	0x00000400
3:	.long	0xffff7fff
4:	.long	0xfffffbff
#endif
5:	.long	0xa4150020 /* STBCR */
6:	.long   0xfe40001c /* RTCOR */
7:	.long   0xfe400018 /* RTCNT */
8:	.long   0xa55a0000


/* interrupt vector @ 0x600 */
	.balign 	0x400,0,0x400
	.long	0xdeadbeef
	.balign 	0x200,0,0x200
	bra	restore_jump_vbr
	 nop
sh_mobile_standby_end:

ENTRY(sh_mobile_standby_size)
	.long sh_mobile_standby_end - sh_mobile_standby
