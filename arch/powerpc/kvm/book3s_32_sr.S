/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
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

	/* 'current->mm' needs to be in r4 */
	tophys(r4, r2)
	lwz	r4, MM(r4)
	tophys(r4, r4)
	/* This only clobbers r0, r3, r4 and r5 */
	bl	switch_mmu_context

.endm
