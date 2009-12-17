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

#define SHADOW_SLB_ESID(num)	(SLBSHADOW_SAVEAREA + (num * 0x10))
#define SHADOW_SLB_VSID(num)	(SLBSHADOW_SAVEAREA + (num * 0x10) + 0x8)
#define UNBOLT_SLB_ENTRY(num) \
	ld	r9, SHADOW_SLB_ESID(num)(r12); \
	/* Invalid? Skip. */; \
	rldicl. r0, r9, 37, 63; \
	beq	slb_entry_skip_ ## num; \
	xoris	r9, r9, SLB_ESID_V@h; \
	std	r9, SHADOW_SLB_ESID(num)(r12); \
  slb_entry_skip_ ## num:

#define REBOLT_SLB_ENTRY(num) \
	ld	r10, SHADOW_SLB_ESID(num)(r11); \
	cmpdi	r10, 0; \
	beq	slb_exit_skip_1; \
	oris	r10, r10, SLB_ESID_V@h; \
	ld	r9, SHADOW_SLB_VSID(num)(r11); \
	slbmte	r9, r10; \
	std	r10, SHADOW_SLB_ESID(num)(r11); \
slb_exit_skip_ ## num:

/******************************************************************************
 *                                                                            *
 *                               Entry code                                   *
 *                                                                            *
 *****************************************************************************/

.global kvmppc_handler_trampoline_enter
kvmppc_handler_trampoline_enter:

	/* Required state:
	 *
	 * MSR = ~IR|DR
	 * R13 = PACA
	 * R9 = guest IP
	 * R10 = guest MSR
	 * R11 = free
	 * R12 = free
	 * PACA[PACA_EXMC + EX_R9] = guest R9
	 * PACA[PACA_EXMC + EX_R10] = guest R10
	 * PACA[PACA_EXMC + EX_R11] = guest R11
	 * PACA[PACA_EXMC + EX_R12] = guest R12
	 * PACA[PACA_EXMC + EX_R13] = guest R13
	 * PACA[PACA_EXMC + EX_CCR] = guest CR
	 * PACA[PACA_EXMC + EX_R3] = guest XER
	 */

	mtsrr0	r9
	mtsrr1	r10

	mtspr	SPRN_SPRG_SCRATCH0, r0

	/* Remove LPAR shadow entries */

#if SLB_NUM_BOLTED == 3

	ld	r12, PACA_SLBSHADOWPTR(r13)

	/* Save off the first entry so we can slbie it later */
	ld	r10, SHADOW_SLB_ESID(0)(r12)
	ld	r11, SHADOW_SLB_VSID(0)(r12)

	/* Remove bolted entries */
	UNBOLT_SLB_ENTRY(0)
	UNBOLT_SLB_ENTRY(1)
	UNBOLT_SLB_ENTRY(2)
	
#else
#error unknown number of bolted entries
#endif

	/* Flush SLB */

	slbia

	/* r0 = esid & ESID_MASK */
	rldicr  r10, r10, 0, 35
	/* r0 |= CLASS_BIT(VSID) */
	rldic   r12, r11, 56 - 36, 36
	or      r10, r10, r12
	slbie	r10

	isync

	/* Fill SLB with our shadow */

	lbz	r12, PACA_KVM_SLB_MAX(r13)
	mulli	r12, r12, 16
	addi	r12, r12, PACA_KVM_SLB
	add	r12, r12, r13

	/* for (r11 = kvm_slb; r11 < kvm_slb + kvm_slb_size; r11+=slb_entry) */
	li	r11, PACA_KVM_SLB
	add	r11, r11, r13

slb_loop_enter:

	ld	r10, 0(r11)

	rldicl. r0, r10, 37, 63
	beq	slb_loop_enter_skip

	ld	r9, 8(r11)
	slbmte	r9, r10

slb_loop_enter_skip:
	addi	r11, r11, 16
	cmpd	cr0, r11, r12
	blt	slb_loop_enter

slb_do_enter:

	/* Enter guest */

	mfspr	r0, SPRN_SPRG_SCRATCH0

	ld	r9, (PACA_EXMC+EX_R9)(r13)
	ld	r10, (PACA_EXMC+EX_R10)(r13)
	ld	r12, (PACA_EXMC+EX_R12)(r13)

	lwz	r11, (PACA_EXMC+EX_CCR)(r13)
	mtcr	r11

	ld	r11, (PACA_EXMC+EX_R3)(r13)
	mtxer	r11

	ld	r11, (PACA_EXMC+EX_R11)(r13)
	ld	r13, (PACA_EXMC+EX_R13)(r13)

	RFI
kvmppc_handler_trampoline_enter_end:



/******************************************************************************
 *                                                                            *
 *                               Exit code                                    *
 *                                                                            *
 *****************************************************************************/

.global kvmppc_handler_trampoline_exit
kvmppc_handler_trampoline_exit:

	/* Register usage at this point:
	 *
	 * SPRG_SCRATCH0 = guest R13
	 * R01           = host R1
	 * R02           = host R2
	 * R10           = guest PC
	 * R11           = guest MSR
	 * R12           = exit handler id
	 * R13           = PACA
	 * PACA.exmc.CCR  = guest CR
	 * PACA.exmc.R9  = guest R1
	 * PACA.exmc.R10 = guest R10
	 * PACA.exmc.R11 = guest R11
	 * PACA.exmc.R12 = guest R12
	 * PACA.exmc.R13 = guest R2
	 *
	 */

	/* Save registers */

	std	r0, (PACA_EXMC+EX_SRR0)(r13)
	std	r9, (PACA_EXMC+EX_R3)(r13)
	std	r10, (PACA_EXMC+EX_LR)(r13)
	std	r11, (PACA_EXMC+EX_DAR)(r13)

	/*
	 * In order for us to easily get the last instruction,
	 * we got the #vmexit at, we exploit the fact that the
	 * virtual layout is still the same here, so we can just
	 * ld from the guest's PC address
	 */

	/* We only load the last instruction when it's safe */
	cmpwi	r12, BOOK3S_INTERRUPT_DATA_STORAGE
	beq	ld_last_inst
	cmpwi	r12, BOOK3S_INTERRUPT_PROGRAM
	beq	ld_last_inst

	b	no_ld_last_inst

ld_last_inst:
	/* Save off the guest instruction we're at */
	/*    1) enable paging for data */
	mfmsr	r9
	ori	r11, r9, MSR_DR			/* Enable paging for data */
	mtmsr	r11
	/*    2) fetch the instruction */
	lwz	r0, 0(r10)
	/*    3) disable paging again */
	mtmsr	r9

no_ld_last_inst:

	/* Restore bolted entries from the shadow and fix it along the way */

	/* We don't store anything in entry 0, so we don't need to take care of it */
	slbia
	isync

#if SLB_NUM_BOLTED == 3

	ld	r11, PACA_SLBSHADOWPTR(r13)

	REBOLT_SLB_ENTRY(0)
	REBOLT_SLB_ENTRY(1)
	REBOLT_SLB_ENTRY(2)
	
#else
#error unknown number of bolted entries
#endif

slb_do_exit:

	/* Restore registers */

	ld	r11, (PACA_EXMC+EX_DAR)(r13)
	ld	r10, (PACA_EXMC+EX_LR)(r13)
	ld	r9, (PACA_EXMC+EX_R3)(r13)

	/* Save last inst */
	stw	r0, (PACA_EXMC+EX_LR)(r13)

	/* Save DAR and DSISR before going to paged mode */
	mfdar	r0
	std	r0, (PACA_EXMC+EX_DAR)(r13)
	mfdsisr	r0
	stw	r0, (PACA_EXMC+EX_DSISR)(r13)

	/* RFI into the highmem handler */
	mfmsr	r0
	ori	r0, r0, MSR_IR|MSR_DR|MSR_RI	/* Enable paging */
	mtsrr1	r0
	ld	r0, PACASAVEDMSR(r13)		/* Highmem handler address */
	mtsrr0	r0

	mfspr	r0, SPRN_SPRG_SCRATCH0

	RFI
kvmppc_handler_trampoline_exit_end:

