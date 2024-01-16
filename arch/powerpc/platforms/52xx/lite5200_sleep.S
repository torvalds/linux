/* SPDX-License-Identifier: GPL-2.0 */
#include <asm/reg.h>
#include <asm/ppc_asm.h>
#include <asm/processor.h>
#include <asm/cache.h>


#define SDRAM_CTRL	0x104
#define SC_MODE_EN	(1<<31)
#define SC_CKE		(1<<30)
#define SC_REF_EN	(1<<28)
#define SC_SOFT_PRE	(1<<1)

#define GPIOW_GPIOE	0xc00
#define GPIOW_DDR	0xc08
#define GPIOW_DVO	0xc0c

#define CDM_CE		0x214
#define CDM_SDRAM	(1<<3)


/* helpers... beware: r10 and r4 are overwritten */
#define SAVE_SPRN(reg, addr)		\
	mfspr	r10, SPRN_##reg;	\
	stw	r10, ((addr)*4)(r4);

#define LOAD_SPRN(reg, addr)		\
	lwz	r10, ((addr)*4)(r4);	\
	mtspr	SPRN_##reg, r10;	\
	sync;				\
	isync;


	.data
registers:
	.space 0x5c*4
	.text

/* ---------------------------------------------------------------------- */
/* low-power mode with help of M68HLC908QT1 */

	.globl lite5200_low_power
lite5200_low_power:

	mr	r7, r3	/* save SRAM va */
	mr	r8, r4	/* save MBAR va */

	/* setup wakeup address for u-boot at physical location 0x0 */
	lis	r3, CONFIG_KERNEL_START@h
	lis	r4, lite5200_wakeup@h
	ori	r4, r4, lite5200_wakeup@l
	sub	r4, r4, r3
	stw	r4, 0(r3)


	/*
	 * save stuff BDI overwrites
	 * 0xf0 (0xe0->0x100 gets overwritten when BDI connected;
	 *   even when CONFIG_BDI_SWITCH is disabled and MMU XLAT commented; heisenbug?))
	 * WARNING: self-refresh doesn't seem to work when BDI2000 is connected,
	 *   possibly because BDI sets SDRAM registers before wakeup code does
	 */
	lis	r4, registers@h
	ori	r4, r4, registers@l
	lwz	r10, 0xf0(r3)
	stw	r10, (0x1d*4)(r4)

	/* save registers to r4 [destroys r10] */
	SAVE_SPRN(LR, 0x1c)
	bl	save_regs

	/* flush caches [destroys r3, r4] */
	bl	flush_data_cache


	/* copy code to sram */
	mr	r4, r7
	li	r3, (sram_code_end - sram_code)/4
	mtctr	r3
	lis	r3, sram_code@h
	ori	r3, r3, sram_code@l
1:
	lwz	r5, 0(r3)
	stw	r5, 0(r4)
	addi	r3, r3, 4
	addi	r4, r4, 4
	bdnz	1b

	/* get tb_ticks_per_usec */
	lis	r3, tb_ticks_per_usec@h
	lwz	r11, tb_ticks_per_usec@l(r3)

	/* disable I and D caches */
	mfspr	r3, SPRN_HID0
	ori	r3, r3, HID0_ICE | HID0_DCE
	xori	r3, r3, HID0_ICE | HID0_DCE
	sync; isync;
	mtspr	SPRN_HID0, r3
	sync; isync;

	/* jump to sram */
	mtlr	r7
	blrl
	/* doesn't return */


sram_code:
	/* self refresh */
	lwz	r4, SDRAM_CTRL(r8)

	/* send NOP (precharge) */
	oris	r4, r4, SC_MODE_EN@h	/* mode_en */
	stw	r4, SDRAM_CTRL(r8)
	sync

	ori	r4, r4, SC_SOFT_PRE	/* soft_pre */
	stw	r4, SDRAM_CTRL(r8)
	sync
	xori	r4, r4, SC_SOFT_PRE

	xoris	r4, r4, SC_MODE_EN@h	/* !mode_en */
	stw	r4, SDRAM_CTRL(r8)
	sync

	/* delay (for NOP to finish) */
	li	r12, 1
	bl	udelay

	/*
	 * mode_en must not be set when enabling self-refresh
	 * send AR with CKE low (self-refresh)
	 */
	oris	r4, r4, (SC_REF_EN | SC_CKE)@h
	xoris	r4, r4, (SC_CKE)@h	/* ref_en !cke */
	stw	r4, SDRAM_CTRL(r8)
	sync

	/* delay (after !CKE there should be two cycles) */
	li	r12, 1
	bl	udelay

	/* disable clock */
	lwz	r4, CDM_CE(r8)
	ori	r4, r4, CDM_SDRAM
	xori	r4, r4, CDM_SDRAM
	stw	r4, CDM_CE(r8)
	sync

	/* delay a bit */
	li	r12, 1
	bl	udelay


	/* turn off with QT chip */
	li	r4, 0x02
	stb	r4, GPIOW_GPIOE(r8)	/* enable gpio_wkup1 */
	sync

	stb	r4, GPIOW_DVO(r8)	/* "output" high */
	sync
	stb	r4, GPIOW_DDR(r8)	/* output */
	sync
	stb	r4, GPIOW_DVO(r8)	/* output high */
	sync

	/* 10uS delay */
	li	r12, 10
	bl	udelay

	/* turn off */
	li	r4, 0
	stb	r4, GPIOW_DVO(r8)	/* output low */
	sync

	/* wait until we're offline */
  1:
	b	1b


	/* local udelay in sram is needed */
  udelay: /* r11 - tb_ticks_per_usec, r12 - usecs, overwrites r13 */
	mullw	r12, r12, r11
	mftb	r13	/* start */
	add	r12, r13, r12 /* end */
    1:
	mftb	r13	/* current */
	cmp	cr0, r13, r12
	blt	1b
	blr

sram_code_end:



/* uboot jumps here on resume */
lite5200_wakeup:
	bl	restore_regs


	/* HIDs, MSR */
	LOAD_SPRN(HID1, 0x19)
	LOAD_SPRN(HID2, 0x1a)


	/* address translation is tricky (see turn_on_mmu) */
	mfmsr	r10
	ori	r10, r10, MSR_DR | MSR_IR


	mtspr	SPRN_SRR1, r10
	lis	r10, mmu_on@h
	ori	r10, r10, mmu_on@l
	mtspr	SPRN_SRR0, r10
	sync
	rfi
mmu_on:
	/* kernel offset (r4 is still set from restore_registers) */
	addis	r4, r4, CONFIG_KERNEL_START@h


	/* restore MSR */
	lwz	r10, (4*0x1b)(r4)
	mtmsr	r10
	sync; isync;

	/* invalidate caches */
	mfspr	r10, SPRN_HID0
	ori	r5, r10, HID0_ICFI | HID0_DCI
	mtspr	SPRN_HID0, r5	/* invalidate caches */
	sync; isync;
	mtspr	SPRN_HID0, r10
	sync; isync;

	/* enable caches */
	lwz	r10, (4*0x18)(r4)
	mtspr	SPRN_HID0, r10	/* restore (enable caches, DPM) */
	/* ^ this has to be after address translation set in MSR */
	sync
	isync


	/* restore 0xf0 (BDI2000) */
	lis	r3, CONFIG_KERNEL_START@h
	lwz	r10, (0x1d*4)(r4)
	stw	r10, 0xf0(r3)

	LOAD_SPRN(LR, 0x1c)


	blr
_ASM_NOKPROBE_SYMBOL(lite5200_wakeup)


/* ---------------------------------------------------------------------- */
/* boring code: helpers */

/* save registers */
#define SAVE_BAT(n, addr)		\
	SAVE_SPRN(DBAT##n##L, addr);	\
	SAVE_SPRN(DBAT##n##U, addr+1);	\
	SAVE_SPRN(IBAT##n##L, addr+2);	\
	SAVE_SPRN(IBAT##n##U, addr+3);

#define SAVE_SR(n, addr)		\
	mfsr	r10, n;			\
	stw	r10, ((addr)*4)(r4);

#define SAVE_4SR(n, addr)	\
	SAVE_SR(n, addr);	\
	SAVE_SR(n+1, addr+1);	\
	SAVE_SR(n+2, addr+2);	\
	SAVE_SR(n+3, addr+3);

save_regs:
	stw	r0, 0(r4)
	stw	r1, 0x4(r4)
	stw	r2, 0x8(r4)
	stmw	r11, 0xc(r4) /* 0xc -> 0x5f, (0x18*4-1) */

	SAVE_SPRN(HID0, 0x18)
	SAVE_SPRN(HID1, 0x19)
	SAVE_SPRN(HID2, 0x1a)
	mfmsr	r10
	stw	r10, (4*0x1b)(r4)
	/*SAVE_SPRN(LR, 0x1c) have to save it before the call */
	/* 0x1d reserved by 0xf0 */
	SAVE_SPRN(RPA,   0x1e)
	SAVE_SPRN(SDR1,  0x1f)

	/* save MMU regs */
	SAVE_BAT(0, 0x20)
	SAVE_BAT(1, 0x24)
	SAVE_BAT(2, 0x28)
	SAVE_BAT(3, 0x2c)
	SAVE_BAT(4, 0x30)
	SAVE_BAT(5, 0x34)
	SAVE_BAT(6, 0x38)
	SAVE_BAT(7, 0x3c)

	SAVE_4SR(0, 0x40)
	SAVE_4SR(4, 0x44)
	SAVE_4SR(8, 0x48)
	SAVE_4SR(12, 0x4c)

	SAVE_SPRN(SPRG0, 0x50)
	SAVE_SPRN(SPRG1, 0x51)
	SAVE_SPRN(SPRG2, 0x52)
	SAVE_SPRN(SPRG3, 0x53)
	SAVE_SPRN(SPRG4, 0x54)
	SAVE_SPRN(SPRG5, 0x55)
	SAVE_SPRN(SPRG6, 0x56)
	SAVE_SPRN(SPRG7, 0x57)

	SAVE_SPRN(IABR,  0x58)
	SAVE_SPRN(DABR,  0x59)
	SAVE_SPRN(TBRL,  0x5a)
	SAVE_SPRN(TBRU,  0x5b)

	blr


/* restore registers */
#define LOAD_BAT(n, addr)		\
	LOAD_SPRN(DBAT##n##L, addr);	\
	LOAD_SPRN(DBAT##n##U, addr+1);	\
	LOAD_SPRN(IBAT##n##L, addr+2);	\
	LOAD_SPRN(IBAT##n##U, addr+3);

#define LOAD_SR(n, addr)		\
	lwz	r10, ((addr)*4)(r4);	\
	mtsr	n, r10;

#define LOAD_4SR(n, addr)	\
	LOAD_SR(n, addr);	\
	LOAD_SR(n+1, addr+1);	\
	LOAD_SR(n+2, addr+2);	\
	LOAD_SR(n+3, addr+3);

restore_regs:
	lis	r4, registers@h
	ori	r4, r4, registers@l

	/* MMU is not up yet */
	subis	r4, r4, CONFIG_KERNEL_START@h

	lwz	r0, 0(r4)
	lwz	r1, 0x4(r4)
	lwz	r2, 0x8(r4)
	lmw	r11, 0xc(r4)

	/*
	 * these are a bit tricky
	 *
	 * 0x18 - HID0
	 * 0x19 - HID1
	 * 0x1a - HID2
	 * 0x1b - MSR
	 * 0x1c - LR
	 * 0x1d - reserved by 0xf0 (BDI2000)
	 */
	LOAD_SPRN(RPA,   0x1e);
	LOAD_SPRN(SDR1,  0x1f);

	/* restore MMU regs */
	LOAD_BAT(0, 0x20)
	LOAD_BAT(1, 0x24)
	LOAD_BAT(2, 0x28)
	LOAD_BAT(3, 0x2c)
	LOAD_BAT(4, 0x30)
	LOAD_BAT(5, 0x34)
	LOAD_BAT(6, 0x38)
	LOAD_BAT(7, 0x3c)

	LOAD_4SR(0, 0x40)
	LOAD_4SR(4, 0x44)
	LOAD_4SR(8, 0x48)
	LOAD_4SR(12, 0x4c)

	/* rest of regs */
	LOAD_SPRN(SPRG0, 0x50);
	LOAD_SPRN(SPRG1, 0x51);
	LOAD_SPRN(SPRG2, 0x52);
	LOAD_SPRN(SPRG3, 0x53);
	LOAD_SPRN(SPRG4, 0x54);
	LOAD_SPRN(SPRG5, 0x55);
	LOAD_SPRN(SPRG6, 0x56);
	LOAD_SPRN(SPRG7, 0x57);

	LOAD_SPRN(IABR,  0x58);
	LOAD_SPRN(DABR,  0x59);
	LOAD_SPRN(TBWL,  0x5a);	/* these two have separate R/W regs */
	LOAD_SPRN(TBWU,  0x5b);

	blr
_ASM_NOKPROBE_SYMBOL(restore_regs)



/* cache flushing code. copied from arch/ppc/boot/util.S */
#define NUM_CACHE_LINES (128*8)

/*
 * Flush data cache
 * Do this by just reading lots of stuff into the cache.
 */
flush_data_cache:
	lis	r3,CONFIG_KERNEL_START@h
	ori	r3,r3,CONFIG_KERNEL_START@l
	li	r4,NUM_CACHE_LINES
	mtctr	r4
1:
	lwz	r4,0(r3)
	addi	r3,r3,L1_CACHE_BYTES	/* Next line, please */
	bdnz	1b
	blr
