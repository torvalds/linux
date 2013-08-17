/*
 * KFR2R09 sdram self/auto-refresh setup code
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
#include <asm/romimage-macros.h>

/* code to enter and leave self-refresh. must be self-contained.
 * this code will be copied to on-chip memory and executed from there.
 */
	.balign 4
ENTRY(kfr2r09_sdram_enter_start)

	/* DBSC: put memory in self-refresh mode */

	ED 0xFD000010, 0x00000000 /* DBEN */
	ED 0xFD000040, 0x00000000 /* DBRFPDN0 */
	ED 0xFD000014, 0x00000002 /* DBCMDCNT (PALL) */
	ED 0xFD000014, 0x00000004 /* DBCMDCNT (REF) */
	ED 0xFD000040, 0x00000001 /* DBRFPDN0 */

	rts
	 nop

ENTRY(kfr2r09_sdram_enter_end)

	.balign 4
ENTRY(kfr2r09_sdram_leave_start)

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

	/* DBSC: re-initialize and put in auto-refresh */

	ED 0xFD000108, 0x40000301 /* DBPDCNT0 */
	ED 0xFD000020, 0x011B0002 /* DBCONF */
	ED 0xFD000030, 0x03060E02 /* DBTR0 */
	ED 0xFD000034, 0x01020102 /* DBTR1 */
	ED 0xFD000038, 0x01090406 /* DBTR2 */
	ED 0xFD000008, 0x00000004 /* DBKIND */
	ED 0xFD000040, 0x00000001 /* DBRFPDN0 */
	ED 0xFD000040, 0x00000000 /* DBRFPDN0 */
	ED 0xFD000018, 0x00000001 /* DBCKECNT */
	WAIT 1
	ED 0xFD000010, 0x00000001 /* DBEN */
	ED 0xFD000044, 0x000004AF /* DBRFPDN1 */
	ED 0xFD000048, 0x20CF0037 /* DBRFPDN2 */
	ED 0xFD000014, 0x00000004 /* DBCMDCNT (REF) */
	ED 0xFD000108, 0x40000300 /* DBPDCNT0 */
	ED 0xFD000040, 0x00010000 /* DBRFPDN0 */

	rts
	 nop

ENTRY(kfr2r09_sdram_leave_end)
