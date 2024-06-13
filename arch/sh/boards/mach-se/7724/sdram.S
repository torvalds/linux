/* SPDX-License-Identifier: GPL-2.0
 *
 * MS7724SE sdram self/auto-refresh setup code
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
ENTRY(ms7724se_sdram_enter_start)

	/* DBSC: put memory in self-refresh mode */

	ED 0xFD000010, 0x00000000 /* DBEN */
	ED 0xFD000040, 0x00000000 /* DBRFPDN0 */
	ED 0xFD000014, 0x00000002 /* DBCMDCNT (PALL) */
	ED 0xFD000014, 0x00000004 /* DBCMDCNT (REF) */
	ED 0xFD000040, 0x00000001 /* DBRFPDN0 */

	rts
	 nop

ENTRY(ms7724se_sdram_enter_end)

	.balign 4
ENTRY(ms7724se_sdram_leave_start)

	/* DBSC: put memory in auto-refresh mode */

	mov.l	@(SH_SLEEP_MODE, r5), r0
	tst	#SUSP_SH_RSTANDBY, r0
	bf	resume_rstandby

	ED 0xFD000040, 0x00000000 /* DBRFPDN0 */
	WAIT 1
	ED 0xFD000014, 0x00000002 /* DBCMDCNT (PALL) */
	ED 0xFD000014, 0x00000004 /* DBCMDCNT (REF) */
	ED 0xFD000010, 0x00000001 /* DBEN */
	ED 0xFD000040, 0x00010000 /* DBRFPDN0 */

	rts
	 nop

resume_rstandby:

	/* CPG: setup clocks before restarting external memory */

	ED 0xA4150024, 0x00004000 /* PLLCR */

	mov.l	FRQCRA,r0
	mov.l	@r0,r3
	mov.l	KICK,r1
	or	r1, r3
	mov.l	r3, @r0

	mov.l	LSTATS,r0
	mov	#1,r1
WAIT_LSTATS:
	mov.l	@r0,r3
	tst	r1,r3
	bf	WAIT_LSTATS

	/* DBSC: re-initialize and put in auto-refresh */

	ED 0xFD000108, 0x00000181 /* DBPDCNT0 */
	ED 0xFD000020, 0x015B0002 /* DBCONF */
	ED 0xFD000030, 0x03071502 /* DBTR0 */
	ED 0xFD000034, 0x02020102 /* DBTR1 */
	ED 0xFD000038, 0x01090405 /* DBTR2 */
	ED 0xFD00003C, 0x00000002 /* DBTR3 */
	ED 0xFD000008, 0x00000005 /* DBKIND */
	ED 0xFD000040, 0x00000001 /* DBRFPDN0 */
	ED 0xFD000040, 0x00000000 /* DBRFPDN0 */
	ED 0xFD000018, 0x00000001 /* DBCKECNT */

	mov	#100,r0
WAIT_400NS:
	dt	r0
	bf	WAIT_400NS

	ED 0xFD000014, 0x00000002 /* DBCMDCNT (PALL) */
	ED 0xFD000060, 0x00020000 /* DBMRCNT (EMR2) */
	ED 0xFD000060, 0x00030000 /* DBMRCNT (EMR3) */
	ED 0xFD000060, 0x00010004 /* DBMRCNT (EMR) */
	ED 0xFD000060, 0x00000532 /* DBMRCNT (MRS) */
	ED 0xFD000014, 0x00000002 /* DBCMDCNT (PALL) */
	ED 0xFD000014, 0x00000004 /* DBCMDCNT (REF) */
	ED 0xFD000014, 0x00000004 /* DBCMDCNT (REF) */
	ED 0xFD000060, 0x00000432 /* DBMRCNT (MRS) */
	ED 0xFD000060, 0x000103c0 /* DBMRCNT (EMR) */
	ED 0xFD000060, 0x00010040 /* DBMRCNT (EMR) */

	mov	#100,r0
WAIT_400NS_2:
	dt	r0
	bf	WAIT_400NS_2

	ED 0xFD000010, 0x00000001 /* DBEN */
	ED 0xFD000044, 0x0000050f /* DBRFPDN1 */
	ED 0xFD000048, 0x236800e6 /* DBRFPDN2 */

	mov.l	DUMMY,r0
	mov.l	@r0, r1 /* force single dummy read */

	ED 0xFD000014, 0x00000002 /* DBCMDCNT (PALL) */
	ED 0xFD000014, 0x00000004 /* DBCMDCNT (REF) */
	ED 0xFD000108, 0x00000080 /* DBPDCNT0 */
	ED 0xFD000040, 0x00010000 /* DBRFPDN0 */

	rts
	 nop

	.balign 4
DUMMY:	.long	0xac400000
FRQCRA:	.long	0xa4150000
KICK:	.long	0x80000000
LSTATS:	.long	0xa4150060

ENTRY(ms7724se_sdram_leave_end)
