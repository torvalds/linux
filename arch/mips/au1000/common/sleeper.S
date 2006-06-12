/*
 * Copyright 2002 Embedded Edge, LLC
 * Author: dan@embeddededge.com
 *
 * Sleep helper for Au1xxx sleep mode.
 *
 * This program is free software; you can redistribute	it and/or modify it
 * under  the terms of	the GNU General	 Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <asm/asm.h>
#include <asm/mipsregs.h>
#include <asm/addrspace.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>

	.text
	.set	macro
	.set	noat
	.align	5

/* Save all of the processor general registers and go to sleep.
 * A wakeup condition will get us back here to restore the registers.
 */
LEAF(save_and_sleep)

	subu	sp, PT_SIZE
	sw	$1, PT_R1(sp)
	sw	$2, PT_R2(sp)
	sw	$3, PT_R3(sp)
	sw	$4, PT_R4(sp)
	sw	$5, PT_R5(sp)
	sw	$6, PT_R6(sp)
	sw	$7, PT_R7(sp)
	sw	$8, PT_R8(sp)
	sw	$9, PT_R9(sp)
	sw	$10, PT_R10(sp)
	sw	$11, PT_R11(sp)
	sw	$12, PT_R12(sp)
	sw	$13, PT_R13(sp)
	sw	$14, PT_R14(sp)
	sw	$15, PT_R15(sp)
	sw	$16, PT_R16(sp)
	sw	$17, PT_R17(sp)
	sw	$18, PT_R18(sp)
	sw	$19, PT_R19(sp)
	sw	$20, PT_R20(sp)
	sw	$21, PT_R21(sp)
	sw	$22, PT_R22(sp)
	sw	$23, PT_R23(sp)
	sw	$24, PT_R24(sp)
	sw	$25, PT_R25(sp)
	sw	$26, PT_R26(sp)
	sw	$27, PT_R27(sp)
	sw	$28, PT_R28(sp)
	sw	$29, PT_R29(sp)
	sw	$30, PT_R30(sp)
	sw	$31, PT_R31(sp)
	mfc0	k0, CP0_STATUS
	sw	k0, 0x20(sp)
	mfc0	k0, CP0_CONTEXT
	sw	k0, 0x1c(sp)
	mfc0	k0, CP0_PAGEMASK
	sw	k0, 0x18(sp)
	mfc0	k0, CP0_CONFIG
	sw	k0, 0x14(sp)

	/* Now set up the scratch registers so the boot rom will
	 * return to this point upon wakeup.
	 */
	la	k0, 1f
	lui	k1, 0xb190
	ori	k1, 0x18
	sw	sp, 0(k1)
	ori 	k1, 0x1c
	sw	k0, 0(k1)

/* Put SDRAM into self refresh.  Preload instructions into cache,
 * issue a precharge, then auto refresh, then sleep commands to it.
 */
 	la	t0, sdsleep
	.set	mips3
 	cache	0x14, 0(t0)
 	cache	0x14, 32(t0)
 	cache	0x14, 64(t0)
 	cache	0x14, 96(t0)
	.set	mips0

sdsleep:
	lui 	k0, 0xb400
	sw	zero, 0x001c(k0)	/* Precharge */
	sw	zero, 0x0020(k0)	/* Auto refresh */
	sw	zero, 0x0030(k0)	/* SDRAM sleep */
	sync

	lui 	k1, 0xb190
	sw	zero, 0x0078(k1)	/* get ready  to sleep */
	sync
	sw	zero, 0x007c(k1)	/* Put processor to sleep */
	sync

	/* This is where we return upon wakeup.
	 * Reload all of the registers and return.
	 */
1:	nop
	lw	k0, 0x20(sp)
	mtc0	k0, CP0_STATUS
	lw	k0, 0x1c(sp)
	mtc0	k0, CP0_CONTEXT
	lw	k0, 0x18(sp)
	mtc0	k0, CP0_PAGEMASK
	lw	k0, 0x14(sp)
	mtc0	k0, CP0_CONFIG

	/* We need to catch the ealry Alchemy SOCs with
	 * the write-only Config[OD] bit and set it back to one...
	 */
	jal	au1x00_fixup_config_od
	lw	$1, PT_R1(sp)
	lw	$2, PT_R2(sp)
	lw	$3, PT_R3(sp)
	lw	$4, PT_R4(sp)
	lw	$5, PT_R5(sp)
	lw	$6, PT_R6(sp)
	lw	$7, PT_R7(sp)
	lw	$8, PT_R8(sp)
	lw	$9, PT_R9(sp)
	lw	$10, PT_R10(sp)
	lw	$11, PT_R11(sp)
	lw	$12, PT_R12(sp)
	lw	$13, PT_R13(sp)
	lw	$14, PT_R14(sp)
	lw	$15, PT_R15(sp)
	lw	$16, PT_R16(sp)
	lw	$17, PT_R17(sp)
	lw	$18, PT_R18(sp)
	lw	$19, PT_R19(sp)
	lw	$20, PT_R20(sp)
	lw	$21, PT_R21(sp)
	lw	$22, PT_R22(sp)
	lw	$23, PT_R23(sp)
	lw	$24, PT_R24(sp)
	lw	$25, PT_R25(sp)
	lw	$26, PT_R26(sp)
	lw	$27, PT_R27(sp)
	lw	$28, PT_R28(sp)
	lw	$29, PT_R29(sp)
	lw	$30, PT_R30(sp)
	lw	$31, PT_R31(sp)
	addiu	sp, PT_SIZE

	jr	ra
END(save_and_sleep)
