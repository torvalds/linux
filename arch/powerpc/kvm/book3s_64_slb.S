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

#define SHADOW_SLB_ENTRY_LEN	0x10
#define OFFSET_ESID(x)		(SHADOW_SLB_ENTRY_LEN * x)
#define OFFSET_VSID(x)		((SHADOW_SLB_ENTRY_LEN * x) + 8)

/******************************************************************************
 *                                                                            *
 *                               Entry code                                   *
 *                                                                            *
 *****************************************************************************/

.macro LOAD_GUEST_SEGMENTS

	/* Required state:
	 *
	 * MSR = ~IR|DR
	 * R13 = PACA
	 * R1 = host R1
	 * R2 = host R2
	 * R3 = shadow vcpu
	 * all other volatile GPRS = free except R4, R6
	 * SVCPU[CR]  = guest CR
	 * SVCPU[XER] = guest XER
	 * SVCPU[CTR] = guest CTR
	 * SVCPU[LR]  = guest LR
	 */

BEGIN_FW_FTR_SECTION

	/* Declare SLB shadow as 0 entries big */

	ld	r11, PACA_SLBSHADOWPTR(r13)
	li	r8, 0
	stb	r8, 3(r11)

END_FW_FTR_SECTION_IFSET(FW_FEATURE_LPAR)

	/* Flush SLB */

	li	r10, 0
	slbmte	r10, r10
	slbia

	/* Fill SLB with our shadow */

	lbz	r12, SVCPU_SLB_MAX(r3)
	mulli	r12, r12, 16
	addi	r12, r12, SVCPU_SLB
	add	r12, r12, r3

	/* for (r11 = kvm_slb; r11 < kvm_slb + kvm_slb_size; r11+=slb_entry) */
	li	r11, SVCPU_SLB
	add	r11, r11, r3

slb_loop_enter:

	ld	r10, 0(r11)

	andis.	r9, r10, SLB_ESID_V@h
	beq	slb_loop_enter_skip

	ld	r9, 8(r11)
	slbmte	r9, r10

slb_loop_enter_skip:
	addi	r11, r11, 16
	cmpd	cr0, r11, r12
	blt	slb_loop_enter

slb_do_enter:

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
	 * R13        = shadow vcpu - SHADOW_VCPU_OFF [=PACA on PPC64]
	 * SVCPU.*    = guest *
	 * SVCPU[CR]  = guest CR
	 * SVCPU[XER] = guest XER
	 * SVCPU[CTR] = guest CTR
	 * SVCPU[LR]  = guest LR
	 *
	 */

	/* Remove all SLB entries that are in use. */

	li	r0, 0
	slbmte	r0, r0
	slbia

	/* Restore bolted entries from the shadow */

	ld	r11, PACA_SLBSHADOWPTR(r13)

BEGIN_FW_FTR_SECTION

	/* Declare SLB shadow as SLB_NUM_BOLTED entries big */

	li	r8, SLB_NUM_BOLTED
	stb	r8, 3(r11)

END_FW_FTR_SECTION_IFSET(FW_FEATURE_LPAR)

	/* Manually load all entries from shadow SLB */

	li	r8, SLBSHADOW_SAVEAREA
	li	r7, SLBSHADOW_SAVEAREA + 8

	.rept	SLB_NUM_BOLTED
	LDX_BE	r10, r11, r8
	cmpdi	r10, 0
	beq	1f
	LDX_BE	r9, r11, r7
	slbmte	r9, r10
1:	addi	r7, r7, SHADOW_SLB_ENTRY_LEN
	addi	r8, r8, SHADOW_SLB_ENTRY_LEN
	.endr

	isync
	sync

slb_do_exit:

.endm
