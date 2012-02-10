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
#include <asm/regdef.h>
#include <asm/stackframe.h>

	.extern __flush_cache_all

	.text
	.set noreorder
	.set noat
	.align	5


/* preparatory stuff */
.macro	SETUP_SLEEP
	subu	sp, PT_SIZE
	sw	$1, PT_R1(sp)
	sw	$2, PT_R2(sp)
	sw	$3, PT_R3(sp)
	sw	$4, PT_R4(sp)
	sw	$5, PT_R5(sp)
	sw	$6, PT_R6(sp)
	sw	$7, PT_R7(sp)
	sw	$16, PT_R16(sp)
	sw	$17, PT_R17(sp)
	sw	$18, PT_R18(sp)
	sw	$19, PT_R19(sp)
	sw	$20, PT_R20(sp)
	sw	$21, PT_R21(sp)
	sw	$22, PT_R22(sp)
	sw	$23, PT_R23(sp)
	sw	$26, PT_R26(sp)
	sw	$27, PT_R27(sp)
	sw	$28, PT_R28(sp)
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

	/* flush caches to make sure context is in memory */
	la	t1, __flush_cache_all
	lw	t0, 0(t1)
	jalr	t0
	 nop

	/* Now set up the scratch registers so the boot rom will
	 * return to this point upon wakeup.
	 * sys_scratch0 : SP
	 * sys_scratch1 : RA
	 */
	lui	t3, 0xb190		/* sys_xxx */
	sw	sp, 0x0018(t3)
	la	k0, alchemy_sleep_wakeup	/* resume path */
	sw	k0, 0x001c(t3)
.endm

.macro	DO_SLEEP
	/* put power supply and processor to sleep */
	sw	zero, 0x0078(t3)	/* sys_slppwr */
	sync
	sw	zero, 0x007c(t3)	/* sys_sleep */
	sync
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
.endm

/* sleep code for Au1000/Au1100/Au1500 memory controller type */
LEAF(alchemy_sleep_au1000)

	SETUP_SLEEP

	/* cache following instructions, as memory gets put to sleep */
	la	t0, 1f
	.set	mips3
	cache	0x14, 0(t0)
	cache	0x14, 32(t0)
	cache	0x14, 64(t0)
	cache	0x14, 96(t0)
	.set	mips0

1:	lui 	a0, 0xb400		/* mem_xxx */
	sw	zero, 0x001c(a0) 	/* Precharge */
	sync
	sw	zero, 0x0020(a0)	/* Auto Refresh */
	sync
	sw	zero, 0x0030(a0)  	/* Sleep */
	sync

	DO_SLEEP

END(alchemy_sleep_au1000)

/* sleep code for Au1550/Au1200 memory controller type */
LEAF(alchemy_sleep_au1550)

	SETUP_SLEEP

	/* cache following instructions, as memory gets put to sleep */
	la	t0, 1f
	.set	mips3
	cache	0x14, 0(t0)
	cache	0x14, 32(t0)
	cache	0x14, 64(t0)
	cache	0x14, 96(t0)
	.set	mips0

1:	lui 	a0, 0xb400		/* mem_xxx */
	sw	zero, 0x08c0(a0) 	/* Precharge */
	sync
	sw	zero, 0x08d0(a0)	/* Self Refresh */
	sync

	/* wait for sdram to enter self-refresh mode */
	lui 	t0, 0x0100
2:	lw 	t1, 0x0850(a0)		/* mem_sdstat */
	and	t2, t1, t0
	beq	t2, zero, 2b
	 nop

	/* disable SDRAM clocks */
	lui	t0, 0xcfff
	ori	t0, t0, 0xffff
	lw 	t1, 0x0840(a0)		/* mem_sdconfiga */
	and 	t1, t0, t1		/* clear CE[1:0] */
	sw 	t1, 0x0840(a0)		/* mem_sdconfiga */
	sync

	DO_SLEEP

END(alchemy_sleep_au1550)

/* sleepcode for Au1300 memory controller type */
LEAF(alchemy_sleep_au1300)

	SETUP_SLEEP

	/* cache following instructions, as memory gets put to sleep */
	la	t0, 2f
	la	t1, 4f
	subu	t2, t1, t0

	.set	mips3

1:	cache	0x14, 0(t0)
	subu	t2, t2, 32
	bgez	t2, 1b
	 addu	t0, t0, 32

	.set	mips0

2:	lui	a0, 0xb400		/* mem_xxx */

	/* disable all ports in mem_sdportcfga */
	sw	zero, 0x868(a0)		/* mem_sdportcfga */
	sync

	/* disable ODT */
	li	t0, 0x03010000
	sw	t0, 0x08d8(a0)		/* mem_sdcmd0 */
	sw	t0, 0x08dc(a0)		/* mem_sdcmd1 */
	sync

	/* precharge */
	li	t0, 0x23000400
	sw	t0, 0x08dc(a0)		/* mem_sdcmd1 */
	sw	t0, 0x08d8(a0)		/* mem_sdcmd0 */
	sync

	/* auto refresh */
	sw	zero, 0x08c8(a0)	/* mem_sdautoref */
	sync

	/* block access to the DDR */
	lw	t0, 0x0848(a0)		/* mem_sdconfigb */
	li	t1, (1 << 7 | 0x3F)
	or	t0, t0, t1
	sw	t0, 0x0848(a0)		/* mem_sdconfigb */
	sync

	/* issue the Self Refresh command */
	li	t0, 0x10000000
	sw	t0, 0x08dc(a0)		/* mem_sdcmd1 */
	sw	t0, 0x08d8(a0)		/* mem_sdcmd0 */
	sync

	/* wait for sdram to enter self-refresh mode */
	lui	t0, 0x0300
3:	lw	t1, 0x0850(a0)		/* mem_sdstat */
	and	t2, t1, t0
	bne	t2, t0, 3b
	 nop

	/* disable SDRAM clocks */
	li	t0, ~(3<<28)
	lw	t1, 0x0840(a0)		/* mem_sdconfiga */
	and	t1, t1, t0		/* clear CE[1:0] */
	sw	t1, 0x0840(a0)		/* mem_sdconfiga */
	sync

	DO_SLEEP
4:

END(alchemy_sleep_au1300)


	/* This is where we return upon wakeup.
	 * Reload all of the registers and return.
	 */
LEAF(alchemy_sleep_wakeup)
	lw	k0, 0x20(sp)
	mtc0	k0, CP0_STATUS
	lw	k0, 0x1c(sp)
	mtc0	k0, CP0_CONTEXT
	lw	k0, 0x18(sp)
	mtc0	k0, CP0_PAGEMASK
	lw	k0, 0x14(sp)
	mtc0	k0, CP0_CONFIG

	/* We need to catch the early Alchemy SOCs with
	 * the write-only Config[OD] bit and set it back to one...
	 */
	jal	au1x00_fixup_config_od
	 nop
	lw	$1, PT_R1(sp)
	lw	$2, PT_R2(sp)
	lw	$3, PT_R3(sp)
	lw	$4, PT_R4(sp)
	lw	$5, PT_R5(sp)
	lw	$6, PT_R6(sp)
	lw	$7, PT_R7(sp)
	lw	$16, PT_R16(sp)
	lw	$17, PT_R17(sp)
	lw	$18, PT_R18(sp)
	lw	$19, PT_R19(sp)
	lw	$20, PT_R20(sp)
	lw	$21, PT_R21(sp)
	lw	$22, PT_R22(sp)
	lw	$23, PT_R23(sp)
	lw	$26, PT_R26(sp)
	lw	$27, PT_R27(sp)
	lw	$28, PT_R28(sp)
	lw	$30, PT_R30(sp)
	lw	$31, PT_R31(sp)
	jr	ra
	 addiu	sp, PT_SIZE
END(alchemy_sleep_wakeup)
