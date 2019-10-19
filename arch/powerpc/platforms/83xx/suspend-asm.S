/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Enter and leave deep sleep state on MPC83xx
 *
 * Copyright (c) 2006-2008 Freescale Semiconductor, Inc.
 * Author: Scott Wood <scottwood@freescale.com>
 */

#include <asm/page.h>
#include <asm/ppc_asm.h>
#include <asm/reg.h>
#include <asm/asm-offsets.h>

#define SS_MEMSAVE	0x00 /* First 8 bytes of RAM */
#define SS_HID		0x08 /* 3 HIDs */
#define SS_IABR		0x14 /* 2 IABRs */
#define SS_IBCR		0x1c
#define SS_DABR		0x20 /* 2 DABRs */
#define SS_DBCR		0x28
#define SS_SP		0x2c
#define SS_SR		0x30 /* 16 segment registers */
#define SS_R2		0x70
#define SS_MSR		0x74
#define SS_SDR1		0x78
#define SS_LR		0x7c
#define SS_SPRG		0x80 /* 8 SPRGs */
#define SS_DBAT		0xa0 /* 8 DBATs */
#define SS_IBAT		0xe0 /* 8 IBATs */
#define SS_TB		0x120
#define SS_CR		0x128
#define SS_GPREG	0x12c /* r12-r31 */
#define STATE_SAVE_SIZE 0x17c

	.section .data
	.align	5

mpc83xx_sleep_save_area:
	.space	STATE_SAVE_SIZE
immrbase:
	.long	0

	.section .text
	.align	5

	/* r3 = physical address of IMMR */
_GLOBAL(mpc83xx_enter_deep_sleep)
	lis	r4, immrbase@ha
	stw	r3, immrbase@l(r4)

	/* The first 2 words of memory are used to communicate with the
	 * bootloader, to tell it how to resume.
	 *
	 * The first word is the magic number 0xf5153ae5, and the second
	 * is the pointer to mpc83xx_deep_resume.
	 *
	 * The original content of these two words is saved in SS_MEMSAVE.
	 */

	lis	r3, mpc83xx_sleep_save_area@h
	ori	r3, r3, mpc83xx_sleep_save_area@l

	lis	r4, KERNELBASE@h
	lwz	r5, 0(r4)
	lwz	r6, 4(r4)

	stw	r5, SS_MEMSAVE+0(r3)
	stw	r6, SS_MEMSAVE+4(r3)

	mfspr	r5, SPRN_HID0
	mfspr	r6, SPRN_HID1
	mfspr	r7, SPRN_HID2

	stw	r5, SS_HID+0(r3)
	stw	r6, SS_HID+4(r3)
	stw	r7, SS_HID+8(r3)

	mfspr	r4, SPRN_IABR
	mfspr	r5, SPRN_IABR2
	mfspr	r6, SPRN_IBCR
	mfspr	r7, SPRN_DABR
	mfspr	r8, SPRN_DABR2
	mfspr	r9, SPRN_DBCR

	stw	r4, SS_IABR+0(r3)
	stw	r5, SS_IABR+4(r3)
	stw	r6, SS_IBCR(r3)
	stw	r7, SS_DABR+0(r3)
	stw	r8, SS_DABR+4(r3)
	stw	r9, SS_DBCR(r3)

	mfspr	r4, SPRN_SPRG0
	mfspr	r5, SPRN_SPRG1
	mfspr	r6, SPRN_SPRG2
	mfspr	r7, SPRN_SPRG3
	mfsdr1	r8

	stw	r4, SS_SPRG+0(r3)
	stw	r5, SS_SPRG+4(r3)
	stw	r6, SS_SPRG+8(r3)
	stw	r7, SS_SPRG+12(r3)
	stw	r8, SS_SDR1(r3)

	mfspr	r4, SPRN_SPRG4
	mfspr	r5, SPRN_SPRG5
	mfspr	r6, SPRN_SPRG6
	mfspr	r7, SPRN_SPRG7

	stw	r4, SS_SPRG+16(r3)
	stw	r5, SS_SPRG+20(r3)
	stw	r6, SS_SPRG+24(r3)
	stw	r7, SS_SPRG+28(r3)

	mfspr	r4, SPRN_DBAT0U
	mfspr	r5, SPRN_DBAT0L
	mfspr	r6, SPRN_DBAT1U
	mfspr	r7, SPRN_DBAT1L

	stw	r4, SS_DBAT+0x00(r3)
	stw	r5, SS_DBAT+0x04(r3)
	stw	r6, SS_DBAT+0x08(r3)
	stw	r7, SS_DBAT+0x0c(r3)

	mfspr	r4, SPRN_DBAT2U
	mfspr	r5, SPRN_DBAT2L
	mfspr	r6, SPRN_DBAT3U
	mfspr	r7, SPRN_DBAT3L

	stw	r4, SS_DBAT+0x10(r3)
	stw	r5, SS_DBAT+0x14(r3)
	stw	r6, SS_DBAT+0x18(r3)
	stw	r7, SS_DBAT+0x1c(r3)

	mfspr	r4, SPRN_DBAT4U
	mfspr	r5, SPRN_DBAT4L
	mfspr	r6, SPRN_DBAT5U
	mfspr	r7, SPRN_DBAT5L

	stw	r4, SS_DBAT+0x20(r3)
	stw	r5, SS_DBAT+0x24(r3)
	stw	r6, SS_DBAT+0x28(r3)
	stw	r7, SS_DBAT+0x2c(r3)

	mfspr	r4, SPRN_DBAT6U
	mfspr	r5, SPRN_DBAT6L
	mfspr	r6, SPRN_DBAT7U
	mfspr	r7, SPRN_DBAT7L

	stw	r4, SS_DBAT+0x30(r3)
	stw	r5, SS_DBAT+0x34(r3)
	stw	r6, SS_DBAT+0x38(r3)
	stw	r7, SS_DBAT+0x3c(r3)

	mfspr	r4, SPRN_IBAT0U
	mfspr	r5, SPRN_IBAT0L
	mfspr	r6, SPRN_IBAT1U
	mfspr	r7, SPRN_IBAT1L

	stw	r4, SS_IBAT+0x00(r3)
	stw	r5, SS_IBAT+0x04(r3)
	stw	r6, SS_IBAT+0x08(r3)
	stw	r7, SS_IBAT+0x0c(r3)

	mfspr	r4, SPRN_IBAT2U
	mfspr	r5, SPRN_IBAT2L
	mfspr	r6, SPRN_IBAT3U
	mfspr	r7, SPRN_IBAT3L

	stw	r4, SS_IBAT+0x10(r3)
	stw	r5, SS_IBAT+0x14(r3)
	stw	r6, SS_IBAT+0x18(r3)
	stw	r7, SS_IBAT+0x1c(r3)

	mfspr	r4, SPRN_IBAT4U
	mfspr	r5, SPRN_IBAT4L
	mfspr	r6, SPRN_IBAT5U
	mfspr	r7, SPRN_IBAT5L

	stw	r4, SS_IBAT+0x20(r3)
	stw	r5, SS_IBAT+0x24(r3)
	stw	r6, SS_IBAT+0x28(r3)
	stw	r7, SS_IBAT+0x2c(r3)

	mfspr	r4, SPRN_IBAT6U
	mfspr	r5, SPRN_IBAT6L
	mfspr	r6, SPRN_IBAT7U
	mfspr	r7, SPRN_IBAT7L

	stw	r4, SS_IBAT+0x30(r3)
	stw	r5, SS_IBAT+0x34(r3)
	stw	r6, SS_IBAT+0x38(r3)
	stw	r7, SS_IBAT+0x3c(r3)

	mfmsr	r4
	mflr	r5
	mfcr	r6

	stw	r4, SS_MSR(r3)
	stw	r5, SS_LR(r3)
	stw	r6, SS_CR(r3)
	stw	r1, SS_SP(r3)
	stw	r2, SS_R2(r3)

1:	mftbu	r4
	mftb	r5
	mftbu	r6
	cmpw	r4, r6
	bne	1b

	stw	r4, SS_TB+0(r3)
	stw	r5, SS_TB+4(r3)

	stmw	r12, SS_GPREG(r3)

	li	r4, 0
	addi	r6, r3, SS_SR-4
1:	mfsrin	r5, r4
	stwu	r5, 4(r6)
	addis	r4, r4, 0x1000
	cmpwi	r4, 0
	bne	1b

	/* Disable machine checks and critical exceptions */
	mfmsr	r4
	rlwinm	r4, r4, 0, ~MSR_CE
	rlwinm	r4, r4, 0, ~MSR_ME
	mtmsr	r4
	isync

#define TMP_VIRT_IMMR		0xf0000000
#define DEFAULT_IMMR_VALUE	0xff400000
#define IMMRBAR_BASE		0x0000

	lis	r4, immrbase@ha
	lwz	r4, immrbase@l(r4)

	/* Use DBAT0 to address the current IMMR space */

	ori	r4, r4, 0x002a
	mtspr	SPRN_DBAT0L, r4
	lis	r8, TMP_VIRT_IMMR@h
	ori	r4, r8, 0x001e	/* 1 MByte accessible from Kernel Space only */
	mtspr	SPRN_DBAT0U, r4
	isync

	/* Use DBAT1 to address the original IMMR space */

	lis	r4, DEFAULT_IMMR_VALUE@h
	ori	r4, r4, 0x002a
	mtspr	SPRN_DBAT1L, r4
	lis	r9, (TMP_VIRT_IMMR + 0x01000000)@h
	ori	r4, r9, 0x001e	/* 1 MByte accessible from Kernel Space only */
	mtspr	SPRN_DBAT1U, r4
	isync

	/* Use DBAT2 to address the beginning of RAM.  This isn't done
	 * using the normal virtual mapping, because with page debugging
	 * enabled it will be read-only.
	 */

	li	r4, 0x0002
	mtspr	SPRN_DBAT2L, r4
	lis	r4, KERNELBASE@h
	ori	r4, r4, 0x001e	/* 1 MByte accessible from Kernel Space only */
	mtspr	SPRN_DBAT2U, r4
	isync

	/* Flush the cache with our BAT, as there will be TLB misses
	 * otherwise if page debugging is enabled, and these misses
	 * will disturb the PLRU algorithm.
	 */

	bl	__flush_disable_L1

	/* Keep the i-cache enabled, so the hack below for low-boot
	 * flash will work.
	 */
	mfspr	r3, SPRN_HID0
	ori	r3, r3, HID0_ICE
	mtspr	SPRN_HID0, r3
	isync

	lis	r6, 0xf515
	ori	r6, r6, 0x3ae5

	lis	r7, mpc83xx_deep_resume@h
	ori	r7, r7, mpc83xx_deep_resume@l
	tophys(r7, r7)

	lis	r5, KERNELBASE@h
	stw	r6, 0(r5)
	stw	r7, 4(r5)

	/* Reset BARs */

	li	r4, 0
	stw	r4, 0x0024(r8)
	stw	r4, 0x002c(r8)
	stw	r4, 0x0034(r8)
	stw	r4, 0x003c(r8)
	stw	r4, 0x0064(r8)
	stw	r4, 0x006c(r8)

	/* Rev 1 of the 8313 has problems with wakeup events that are
	 * pending during the transition to deep sleep state (such as if
	 * the PCI host sets the state to D3 and then D0 in rapid
	 * succession).  This check shrinks the race window somewhat.
	 *
	 * See erratum PCI23, though the problem is not limited
	 * to PCI.
	 */

	lwz	r3, 0x0b04(r8)
	andi.	r3, r3, 1
	bne-	mpc83xx_deep_resume

	/* Move IMMR back to the default location, following the
	 * procedure specified in the MPC8313 manual.
	 */
	lwz	r4, IMMRBAR_BASE(r8)
	isync
	lis	r4, DEFAULT_IMMR_VALUE@h
	stw	r4, IMMRBAR_BASE(r8)
	lis	r4, KERNELBASE@h
	lwz	r4, 0(r4)
	isync
	lwz	r4, IMMRBAR_BASE(r9)
	mr	r8, r9
	isync

	/* Check the Reset Configuration Word to see whether flash needs
	 * to be mapped at a low address or a high address.
	 */

	lwz	r4, 0x0904(r8)
	andis.	r4, r4, 0x0400
	li	r4, 0
	beq	boot_low
	lis	r4, 0xff80
boot_low:
	stw	r4, 0x0020(r8)
	lis	r7, 0x8000
	ori	r7, r7, 0x0016

	mfspr	r5, SPRN_HID0
	rlwinm	r5, r5, 0, ~(HID0_DOZE | HID0_NAP)
	oris	r5, r5, HID0_SLEEP@h
	mtspr	SPRN_HID0, r5
	isync

	mfmsr	r5
	oris	r5, r5, MSR_POW@h

	/* Enable the flash mapping at the appropriate address.  This
	 * mapping will override the RAM mapping if booting low, so there's
	 * no need to disable the latter.  This must be done inside the same
	 * cache line as setting MSR_POW, so that no instruction fetches
	 * from RAM happen after the flash mapping is turned on.
	 */

	.align	5
	stw	r7, 0x0024(r8)
	sync
	isync
	mtmsr	r5
	isync
1:	b	1b

mpc83xx_deep_resume:
	lis	r4, 1f@h
	ori	r4, r4, 1f@l
	tophys(r4, r4)
	mtsrr0	r4

	mfmsr	r4
	rlwinm	r4, r4, 0, ~(MSR_IR | MSR_DR)
	mtsrr1	r4

	rfi

1:	tlbia
	bl	__inval_enable_L1

	lis	r3, mpc83xx_sleep_save_area@h
	ori	r3, r3, mpc83xx_sleep_save_area@l
	tophys(r3, r3)

	lwz	r5, SS_MEMSAVE+0(r3)
	lwz	r6, SS_MEMSAVE+4(r3)

	stw	r5, 0(0)
	stw	r6, 4(0)

	lwz	r5, SS_HID+0(r3)
	lwz	r6, SS_HID+4(r3)
	lwz	r7, SS_HID+8(r3)

	mtspr	SPRN_HID0, r5
	mtspr	SPRN_HID1, r6
	mtspr	SPRN_HID2, r7

	lwz	r4, SS_IABR+0(r3)
	lwz	r5, SS_IABR+4(r3)
	lwz	r6, SS_IBCR(r3)
	lwz	r7, SS_DABR+0(r3)
	lwz	r8, SS_DABR+4(r3)
	lwz	r9, SS_DBCR(r3)

	mtspr	SPRN_IABR, r4
	mtspr	SPRN_IABR2, r5
	mtspr	SPRN_IBCR, r6
	mtspr	SPRN_DABR, r7
	mtspr	SPRN_DABR2, r8
	mtspr	SPRN_DBCR, r9

	li	r4, 0
	addi	r6, r3, SS_SR-4
1:	lwzu	r5, 4(r6)
	mtsrin	r5, r4
	addis	r4, r4, 0x1000
	cmpwi	r4, 0
	bne	1b

	lwz	r4, SS_DBAT+0x00(r3)
	lwz	r5, SS_DBAT+0x04(r3)
	lwz	r6, SS_DBAT+0x08(r3)
	lwz	r7, SS_DBAT+0x0c(r3)

	mtspr	SPRN_DBAT0U, r4
	mtspr	SPRN_DBAT0L, r5
	mtspr	SPRN_DBAT1U, r6
	mtspr	SPRN_DBAT1L, r7

	lwz	r4, SS_DBAT+0x10(r3)
	lwz	r5, SS_DBAT+0x14(r3)
	lwz	r6, SS_DBAT+0x18(r3)
	lwz	r7, SS_DBAT+0x1c(r3)

	mtspr	SPRN_DBAT2U, r4
	mtspr	SPRN_DBAT2L, r5
	mtspr	SPRN_DBAT3U, r6
	mtspr	SPRN_DBAT3L, r7

	lwz	r4, SS_DBAT+0x20(r3)
	lwz	r5, SS_DBAT+0x24(r3)
	lwz	r6, SS_DBAT+0x28(r3)
	lwz	r7, SS_DBAT+0x2c(r3)

	mtspr	SPRN_DBAT4U, r4
	mtspr	SPRN_DBAT4L, r5
	mtspr	SPRN_DBAT5U, r6
	mtspr	SPRN_DBAT5L, r7

	lwz	r4, SS_DBAT+0x30(r3)
	lwz	r5, SS_DBAT+0x34(r3)
	lwz	r6, SS_DBAT+0x38(r3)
	lwz	r7, SS_DBAT+0x3c(r3)

	mtspr	SPRN_DBAT6U, r4
	mtspr	SPRN_DBAT6L, r5
	mtspr	SPRN_DBAT7U, r6
	mtspr	SPRN_DBAT7L, r7

	lwz	r4, SS_IBAT+0x00(r3)
	lwz	r5, SS_IBAT+0x04(r3)
	lwz	r6, SS_IBAT+0x08(r3)
	lwz	r7, SS_IBAT+0x0c(r3)

	mtspr	SPRN_IBAT0U, r4
	mtspr	SPRN_IBAT0L, r5
	mtspr	SPRN_IBAT1U, r6
	mtspr	SPRN_IBAT1L, r7

	lwz	r4, SS_IBAT+0x10(r3)
	lwz	r5, SS_IBAT+0x14(r3)
	lwz	r6, SS_IBAT+0x18(r3)
	lwz	r7, SS_IBAT+0x1c(r3)

	mtspr	SPRN_IBAT2U, r4
	mtspr	SPRN_IBAT2L, r5
	mtspr	SPRN_IBAT3U, r6
	mtspr	SPRN_IBAT3L, r7

	lwz	r4, SS_IBAT+0x20(r3)
	lwz	r5, SS_IBAT+0x24(r3)
	lwz	r6, SS_IBAT+0x28(r3)
	lwz	r7, SS_IBAT+0x2c(r3)

	mtspr	SPRN_IBAT4U, r4
	mtspr	SPRN_IBAT4L, r5
	mtspr	SPRN_IBAT5U, r6
	mtspr	SPRN_IBAT5L, r7

	lwz	r4, SS_IBAT+0x30(r3)
	lwz	r5, SS_IBAT+0x34(r3)
	lwz	r6, SS_IBAT+0x38(r3)
	lwz	r7, SS_IBAT+0x3c(r3)

	mtspr	SPRN_IBAT6U, r4
	mtspr	SPRN_IBAT6L, r5
	mtspr	SPRN_IBAT7U, r6
	mtspr	SPRN_IBAT7L, r7

	lwz	r4, SS_SPRG+16(r3)
	lwz	r5, SS_SPRG+20(r3)
	lwz	r6, SS_SPRG+24(r3)
	lwz	r7, SS_SPRG+28(r3)

	mtspr	SPRN_SPRG4, r4
	mtspr	SPRN_SPRG5, r5
	mtspr	SPRN_SPRG6, r6
	mtspr	SPRN_SPRG7, r7

	lwz	r4, SS_SPRG+0(r3)
	lwz	r5, SS_SPRG+4(r3)
	lwz	r6, SS_SPRG+8(r3)
	lwz	r7, SS_SPRG+12(r3)
	lwz	r8, SS_SDR1(r3)

	mtspr	SPRN_SPRG0, r4
	mtspr	SPRN_SPRG1, r5
	mtspr	SPRN_SPRG2, r6
	mtspr	SPRN_SPRG3, r7
	mtsdr1	r8

	lwz	r4, SS_MSR(r3)
	lwz	r5, SS_LR(r3)
	lwz	r6, SS_CR(r3)
	lwz	r1, SS_SP(r3)
	lwz	r2, SS_R2(r3)

	mtsrr1	r4
	mtsrr0	r5
	mtcr	r6

	li	r4, 0
	mtspr	SPRN_TBWL, r4

	lwz	r4, SS_TB+0(r3)
	lwz	r5, SS_TB+4(r3)

	mtspr	SPRN_TBWU, r4
	mtspr	SPRN_TBWL, r5

	lmw	r12, SS_GPREG(r3)

	/* Kick decrementer */
	li	r0, 1
	mtdec	r0

	rfi
