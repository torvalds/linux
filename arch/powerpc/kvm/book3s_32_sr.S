/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Copyright SUSE Linux Products GmbH 2009
 *
 * Authors: Alexander Graf <agraf@suse.de>
 */

/******************************************************************************
 *                                                                            *
 *                               Entry code                                   *
 *                                                                            *
 *****************************************************************************/

.macro LOAD_GUEST_SEGMENTS

	/* Required state:
	 *
	 * MSR = ~IR|DR
	 * R1 = host R1
	 * R2 = host R2
	 * R3 = shadow vcpu
	 * all other volatile GPRS = free except R4, R6
	 * SVCPU[CR]  = guest CR
	 * SVCPU[XER] = guest XER
	 * SVCPU[CTR] = guest CTR
	 * SVCPU[LR]  = guest LR
	 */

#define XCHG_SR(n)	lwz	r9, (SVCPU_SR+(n*4))(r3);  \
			mtsr	n, r9

	XCHG_SR(0)
	XCHG_SR(1)
	XCHG_SR(2)
	XCHG_SR(3)
	XCHG_SR(4)
	XCHG_SR(5)
	XCHG_SR(6)
	XCHG_SR(7)
	XCHG_SR(8)
	XCHG_SR(9)
	XCHG_SR(10)
	XCHG_SR(11)
	XCHG_SR(12)
	XCHG_SR(13)
	XCHG_SR(14)
	XCHG_SR(15)

	/* Clear BATs. */

#define KVM_KILL_BAT(n, reg)		\
        mtspr   SPRN_IBAT##n##U,reg;	\
        mtspr   SPRN_IBAT##n##L,reg;	\
        mtspr   SPRN_DBAT##n##U,reg;	\
        mtspr   SPRN_DBAT##n##L,reg;	\

        li	r9, 0
	KVM_KILL_BAT(0, r9)
	KVM_KILL_BAT(1, r9)
	KVM_KILL_BAT(2, r9)
	KVM_KILL_BAT(3, r9)

.endm

/******************************************************************************
 *                                                                            *
 *                               Exit code                                    *
 *                                                                            *
 *****************************************************************************/

.macro LOAD_HOST_SEGMENTS

	/* Register usage at this point:
	 *
	 * R1         = host R1
	 * R2         = host R2
	 * R12        = exit handler id
	 * R13        = shadow vcpu - SHADOW_VCPU_OFF
	 * SVCPU.*    = guest *
	 * SVCPU[CR]  = guest CR
	 * SVCPU[XER] = guest XER
	 * SVCPU[CTR] = guest CTR
	 * SVCPU[LR]  = guest LR
	 *
	 */

	/* Restore BATs */

	/* We only overwrite the upper part, so we only restoree
	   the upper part. */
#define KVM_LOAD_BAT(n, reg, RA, RB)	\
	lwz	RA,(n*16)+0(reg);	\
	lwz	RB,(n*16)+4(reg);	\
	mtspr	SPRN_IBAT##n##U,RA;	\
	mtspr	SPRN_IBAT##n##L,RB;	\
	lwz	RA,(n*16)+8(reg);	\
	lwz	RB,(n*16)+12(reg);	\
	mtspr	SPRN_DBAT##n##U,RA;	\
	mtspr	SPRN_DBAT##n##L,RB;	\

	lis     r9, BATS@ha
	addi    r9, r9, BATS@l
	tophys(r9, r9)
	KVM_LOAD_BAT(0, r9, r10, r11)
	KVM_LOAD_BAT(1, r9, r10, r11)
	KVM_LOAD_BAT(2, r9, r10, r11)
	KVM_LOAD_BAT(3, r9, r10, r11)

	/* Restore Segment Registers */

	/* 0xc - 0xf */

        li      r0, 4
        mtctr   r0
	LOAD_REG_IMMEDIATE(r3, 0x20000000 | (0x111 * 0xc))
        lis     r4, 0xc000
3:      mtsrin  r3, r4
        addi    r3, r3, 0x111     /* increment VSID */
        addis   r4, r4, 0x1000    /* address of next segment */
        bdnz    3b

	/* 0x0 - 0xb */

	/* switch_mmu_context() needs paging, let's enable it */
	mfmsr   r9
	ori     r11, r9, MSR_DR
	mtmsr   r11
	sync

	/* switch_mmu_context() clobbers r12, rescue it */
	SAVE_GPR(12, r1)

	/* Calling switch_mmu_context(<inv>, current->mm, <inv>); */
	lwz	r4, MM(r2)
	bl	switch_mmu_context

	/* restore r12 */
	REST_GPR(12, r1)

	/* Disable paging again */
	mfmsr   r9
	li      r6, MSR_DR
	andc    r9, r9, r6
	mtmsr	r9
	sync

.endm
