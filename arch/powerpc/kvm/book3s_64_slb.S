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
	beq	slb_exit_skip_ ## num; \
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
	 * R1 = host R1
	 * R2 = host R2
	 * R9 = guest IP
	 * R10 = guest MSR
	 * all other GPRS = free
	 * PACA[KVM_CR] = guest CR
	 * PACA[KVM_XER] = guest XER
	 */

	mtsrr0	r9
	mtsrr1	r10

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

	ld	r0, (PACA_KVM_R0)(r13)
	ld	r1, (PACA_KVM_R1)(r13)
	ld	r2, (PACA_KVM_R2)(r13)
	ld	r3, (PACA_KVM_R3)(r13)
	ld	r4, (PACA_KVM_R4)(r13)
	ld	r5, (PACA_KVM_R5)(r13)
	ld	r6, (PACA_KVM_R6)(r13)
	ld	r7, (PACA_KVM_R7)(r13)
	ld	r8, (PACA_KVM_R8)(r13)
	ld	r9, (PACA_KVM_R9)(r13)
	ld	r10, (PACA_KVM_R10)(r13)
	ld	r12, (PACA_KVM_R12)(r13)

	lwz	r11, (PACA_KVM_CR)(r13)
	mtcr	r11

	ld	r11, (PACA_KVM_XER)(r13)
	mtxer	r11

	ld	r11, (PACA_KVM_R11)(r13)
	ld	r13, (PACA_KVM_R13)(r13)

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
	 * SPRG_SCRATCH0     = guest R13
	 * R12               = exit handler id
	 * R13               = PACA
	 * PACA.KVM.SCRATCH0 = guest R12
	 * PACA.KVM.SCRATCH1 = guest CR
	 *
	 */

	/* Save registers */

	std	r0, PACA_KVM_R0(r13)
	std	r1, PACA_KVM_R1(r13)
	std	r2, PACA_KVM_R2(r13)
	std	r3, PACA_KVM_R3(r13)
	std	r4, PACA_KVM_R4(r13)
	std	r5, PACA_KVM_R5(r13)
	std	r6, PACA_KVM_R6(r13)
	std	r7, PACA_KVM_R7(r13)
	std	r8, PACA_KVM_R8(r13)
	std	r9, PACA_KVM_R9(r13)
	std	r10, PACA_KVM_R10(r13)
	std	r11, PACA_KVM_R11(r13)

	/* Restore R1/R2 so we can handle faults */
	ld	r1, PACA_KVM_HOST_R1(r13)
	ld	r2, PACA_KVM_HOST_R2(r13)

	/* Save guest PC and MSR in GPRs */
	mfsrr0	r3
	mfsrr1	r4

	/* Get scratch'ed off registers */
	mfspr	r9, SPRN_SPRG_SCRATCH0
	std	r9, PACA_KVM_R13(r13)

	ld	r8, PACA_KVM_SCRATCH0(r13)
	std	r8, PACA_KVM_R12(r13)

	lwz	r7, PACA_KVM_SCRATCH1(r13)
	stw	r7, PACA_KVM_CR(r13)

	/* Save more register state  */

	mfxer	r6
	stw	r6, PACA_KVM_XER(r13)

	mfdar	r5
	mfdsisr	r6

	/* Unset guest state */
	li	r9, 0
	stb	r9, PACA_KVM_IN_GUEST(r13)

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
	/* XXX implement PACA_KVM_IN_GUEST=2 path to safely jump over this */
	lwz	r0, 0(r3)
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

	/* Register usage at this point:
	 *
	 * R0         = guest last inst
	 * R1         = host R1
	 * R2         = host R2
	 * R3         = guest PC
	 * R4         = guest MSR
	 * R5         = guest DAR
	 * R6         = guest DSISR
	 * R12        = exit handler id
	 * R13        = PACA
	 * PACA.KVM.* = guest *
	 *
	 */

	/* RFI into the highmem handler */
	mfmsr	r7
	ori	r7, r7, MSR_IR|MSR_DR|MSR_RI	/* Enable paging */
	mtsrr1	r7
	ld	r8, PACA_KVM_VMHANDLER(r13)	/* Highmem handler address */
	mtsrr0	r8

	RFI
kvmppc_handler_trampoline_exit_end:

