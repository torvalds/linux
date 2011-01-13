/*
 * asm-offsets.c Generate definitions needed by assembly language modules.
 * This code generates raw asm output which is post-processed
 * to extract and format the required data.
 *
 * Anthony Xu    <anthony.xu@intel.com>
 * Xiantao Zhang <xiantao.zhang@intel.com>
 * Copyright (c) 2007 Intel Corporation  KVM support.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 */

#include <linux/kvm_host.h>
#include <linux/kbuild.h>

#include "vcpu.h"

void foo(void)
{
	DEFINE(VMM_TASK_SIZE, sizeof(struct kvm_vcpu));
	DEFINE(VMM_PT_REGS_SIZE, sizeof(struct kvm_pt_regs));

	BLANK();

	DEFINE(VMM_VCPU_META_RR0_OFFSET,
			offsetof(struct kvm_vcpu, arch.metaphysical_rr0));
	DEFINE(VMM_VCPU_META_SAVED_RR0_OFFSET,
			offsetof(struct kvm_vcpu,
				arch.metaphysical_saved_rr0));
	DEFINE(VMM_VCPU_VRR0_OFFSET,
			offsetof(struct kvm_vcpu, arch.vrr[0]));
	DEFINE(VMM_VPD_IRR0_OFFSET,
			offsetof(struct vpd, irr[0]));
	DEFINE(VMM_VCPU_ITC_CHECK_OFFSET,
			offsetof(struct kvm_vcpu, arch.itc_check));
	DEFINE(VMM_VCPU_IRQ_CHECK_OFFSET,
			offsetof(struct kvm_vcpu, arch.irq_check));
	DEFINE(VMM_VPD_VHPI_OFFSET,
			offsetof(struct vpd, vhpi));
	DEFINE(VMM_VCPU_VSA_BASE_OFFSET,
			offsetof(struct kvm_vcpu, arch.vsa_base));
	DEFINE(VMM_VCPU_VPD_OFFSET,
			offsetof(struct kvm_vcpu, arch.vpd));
	DEFINE(VMM_VCPU_IRQ_CHECK,
			offsetof(struct kvm_vcpu, arch.irq_check));
	DEFINE(VMM_VCPU_TIMER_PENDING,
			offsetof(struct kvm_vcpu, arch.timer_pending));
	DEFINE(VMM_VCPU_META_SAVED_RR0_OFFSET,
			offsetof(struct kvm_vcpu, arch.metaphysical_saved_rr0));
	DEFINE(VMM_VCPU_MODE_FLAGS_OFFSET,
			offsetof(struct kvm_vcpu, arch.mode_flags));
	DEFINE(VMM_VCPU_ITC_OFS_OFFSET,
			offsetof(struct kvm_vcpu, arch.itc_offset));
	DEFINE(VMM_VCPU_LAST_ITC_OFFSET,
			offsetof(struct kvm_vcpu, arch.last_itc));
	DEFINE(VMM_VCPU_SAVED_GP_OFFSET,
			offsetof(struct kvm_vcpu, arch.saved_gp));

	BLANK();

	DEFINE(VMM_PT_REGS_B6_OFFSET,
				offsetof(struct kvm_pt_regs, b6));
	DEFINE(VMM_PT_REGS_B7_OFFSET,
				offsetof(struct kvm_pt_regs, b7));
	DEFINE(VMM_PT_REGS_AR_CSD_OFFSET,
				offsetof(struct kvm_pt_regs, ar_csd));
	DEFINE(VMM_PT_REGS_AR_SSD_OFFSET,
				offsetof(struct kvm_pt_regs, ar_ssd));
	DEFINE(VMM_PT_REGS_R8_OFFSET,
				offsetof(struct kvm_pt_regs, r8));
	DEFINE(VMM_PT_REGS_R9_OFFSET,
				offsetof(struct kvm_pt_regs, r9));
	DEFINE(VMM_PT_REGS_R10_OFFSET,
				offsetof(struct kvm_pt_regs, r10));
	DEFINE(VMM_PT_REGS_R11_OFFSET,
				offsetof(struct kvm_pt_regs, r11));
	DEFINE(VMM_PT_REGS_CR_IPSR_OFFSET,
				offsetof(struct kvm_pt_regs, cr_ipsr));
	DEFINE(VMM_PT_REGS_CR_IIP_OFFSET,
				offsetof(struct kvm_pt_regs, cr_iip));
	DEFINE(VMM_PT_REGS_CR_IFS_OFFSET,
				offsetof(struct kvm_pt_regs, cr_ifs));
	DEFINE(VMM_PT_REGS_AR_UNAT_OFFSET,
				offsetof(struct kvm_pt_regs, ar_unat));
	DEFINE(VMM_PT_REGS_AR_PFS_OFFSET,
				offsetof(struct kvm_pt_regs, ar_pfs));
	DEFINE(VMM_PT_REGS_AR_RSC_OFFSET,
				offsetof(struct kvm_pt_regs, ar_rsc));
	DEFINE(VMM_PT_REGS_AR_RNAT_OFFSET,
				offsetof(struct kvm_pt_regs, ar_rnat));

	DEFINE(VMM_PT_REGS_AR_BSPSTORE_OFFSET,
				offsetof(struct kvm_pt_regs, ar_bspstore));
	DEFINE(VMM_PT_REGS_PR_OFFSET,
				offsetof(struct kvm_pt_regs, pr));
	DEFINE(VMM_PT_REGS_B0_OFFSET,
				offsetof(struct kvm_pt_regs, b0));
	DEFINE(VMM_PT_REGS_LOADRS_OFFSET,
				offsetof(struct kvm_pt_regs, loadrs));
	DEFINE(VMM_PT_REGS_R1_OFFSET,
				offsetof(struct kvm_pt_regs, r1));
	DEFINE(VMM_PT_REGS_R12_OFFSET,
				offsetof(struct kvm_pt_regs, r12));
	DEFINE(VMM_PT_REGS_R13_OFFSET,
				offsetof(struct kvm_pt_regs, r13));
	DEFINE(VMM_PT_REGS_AR_FPSR_OFFSET,
				offsetof(struct kvm_pt_regs, ar_fpsr));
	DEFINE(VMM_PT_REGS_R15_OFFSET,
				offsetof(struct kvm_pt_regs, r15));
	DEFINE(VMM_PT_REGS_R14_OFFSET,
				offsetof(struct kvm_pt_regs, r14));
	DEFINE(VMM_PT_REGS_R2_OFFSET,
				offsetof(struct kvm_pt_regs, r2));
	DEFINE(VMM_PT_REGS_R3_OFFSET,
				offsetof(struct kvm_pt_regs, r3));
	DEFINE(VMM_PT_REGS_R16_OFFSET,
				offsetof(struct kvm_pt_regs, r16));
	DEFINE(VMM_PT_REGS_R17_OFFSET,
				offsetof(struct kvm_pt_regs, r17));
	DEFINE(VMM_PT_REGS_R18_OFFSET,
				offsetof(struct kvm_pt_regs, r18));
	DEFINE(VMM_PT_REGS_R19_OFFSET,
				offsetof(struct kvm_pt_regs, r19));
	DEFINE(VMM_PT_REGS_R20_OFFSET,
				offsetof(struct kvm_pt_regs, r20));
	DEFINE(VMM_PT_REGS_R21_OFFSET,
				offsetof(struct kvm_pt_regs, r21));
	DEFINE(VMM_PT_REGS_R22_OFFSET,
				offsetof(struct kvm_pt_regs, r22));
	DEFINE(VMM_PT_REGS_R23_OFFSET,
				offsetof(struct kvm_pt_regs, r23));
	DEFINE(VMM_PT_REGS_R24_OFFSET,
				offsetof(struct kvm_pt_regs, r24));
	DEFINE(VMM_PT_REGS_R25_OFFSET,
				offsetof(struct kvm_pt_regs, r25));
	DEFINE(VMM_PT_REGS_R26_OFFSET,
				offsetof(struct kvm_pt_regs, r26));
	DEFINE(VMM_PT_REGS_R27_OFFSET,
				offsetof(struct kvm_pt_regs, r27));
	DEFINE(VMM_PT_REGS_R28_OFFSET,
				offsetof(struct kvm_pt_regs, r28));
	DEFINE(VMM_PT_REGS_R29_OFFSET,
				offsetof(struct kvm_pt_regs, r29));
	DEFINE(VMM_PT_REGS_R30_OFFSET,
				offsetof(struct kvm_pt_regs, r30));
	DEFINE(VMM_PT_REGS_R31_OFFSET,
				offsetof(struct kvm_pt_regs, r31));
	DEFINE(VMM_PT_REGS_AR_CCV_OFFSET,
				offsetof(struct kvm_pt_regs, ar_ccv));
	DEFINE(VMM_PT_REGS_F6_OFFSET,
				offsetof(struct kvm_pt_regs, f6));
	DEFINE(VMM_PT_REGS_F7_OFFSET,
				offsetof(struct kvm_pt_regs, f7));
	DEFINE(VMM_PT_REGS_F8_OFFSET,
				offsetof(struct kvm_pt_regs, f8));
	DEFINE(VMM_PT_REGS_F9_OFFSET,
				offsetof(struct kvm_pt_regs, f9));
	DEFINE(VMM_PT_REGS_F10_OFFSET,
				offsetof(struct kvm_pt_regs, f10));
	DEFINE(VMM_PT_REGS_F11_OFFSET,
				offsetof(struct kvm_pt_regs, f11));
	DEFINE(VMM_PT_REGS_R4_OFFSET,
				offsetof(struct kvm_pt_regs, r4));
	DEFINE(VMM_PT_REGS_R5_OFFSET,
				offsetof(struct kvm_pt_regs, r5));
	DEFINE(VMM_PT_REGS_R6_OFFSET,
				offsetof(struct kvm_pt_regs, r6));
	DEFINE(VMM_PT_REGS_R7_OFFSET,
				offsetof(struct kvm_pt_regs, r7));
	DEFINE(VMM_PT_REGS_EML_UNAT_OFFSET,
				offsetof(struct kvm_pt_regs, eml_unat));
	DEFINE(VMM_VCPU_IIPA_OFFSET,
				offsetof(struct kvm_vcpu, arch.cr_iipa));
	DEFINE(VMM_VCPU_OPCODE_OFFSET,
				offsetof(struct kvm_vcpu, arch.opcode));
	DEFINE(VMM_VCPU_CAUSE_OFFSET, offsetof(struct kvm_vcpu, arch.cause));
	DEFINE(VMM_VCPU_ISR_OFFSET,
				offsetof(struct kvm_vcpu, arch.cr_isr));
	DEFINE(VMM_PT_REGS_R16_SLOT,
				(((offsetof(struct kvm_pt_regs, r16)
				- sizeof(struct kvm_pt_regs)) >> 3) & 0x3f));
	DEFINE(VMM_VCPU_MODE_FLAGS_OFFSET,
				offsetof(struct kvm_vcpu, arch.mode_flags));
	DEFINE(VMM_VCPU_GP_OFFSET, offsetof(struct kvm_vcpu, arch.__gp));
	BLANK();

	DEFINE(VMM_VPD_BASE_OFFSET, offsetof(struct kvm_vcpu, arch.vpd));
	DEFINE(VMM_VPD_VIFS_OFFSET, offsetof(struct vpd, ifs));
	DEFINE(VMM_VLSAPIC_INSVC_BASE_OFFSET,
			offsetof(struct kvm_vcpu, arch.insvc[0]));
	DEFINE(VMM_VPD_VPTA_OFFSET, offsetof(struct vpd, pta));
	DEFINE(VMM_VPD_VPSR_OFFSET, offsetof(struct vpd, vpsr));

	DEFINE(VMM_CTX_R4_OFFSET, offsetof(union context, gr[4]));
	DEFINE(VMM_CTX_R5_OFFSET, offsetof(union context, gr[5]));
	DEFINE(VMM_CTX_R12_OFFSET, offsetof(union context, gr[12]));
	DEFINE(VMM_CTX_R13_OFFSET, offsetof(union context, gr[13]));
	DEFINE(VMM_CTX_KR0_OFFSET, offsetof(union context, ar[0]));
	DEFINE(VMM_CTX_KR1_OFFSET, offsetof(union context, ar[1]));
	DEFINE(VMM_CTX_B0_OFFSET, offsetof(union context, br[0]));
	DEFINE(VMM_CTX_B1_OFFSET, offsetof(union context, br[1]));
	DEFINE(VMM_CTX_B2_OFFSET, offsetof(union context, br[2]));
	DEFINE(VMM_CTX_RR0_OFFSET, offsetof(union context, rr[0]));
	DEFINE(VMM_CTX_RSC_OFFSET, offsetof(union context, ar[16]));
	DEFINE(VMM_CTX_BSPSTORE_OFFSET, offsetof(union context, ar[18]));
	DEFINE(VMM_CTX_RNAT_OFFSET, offsetof(union context, ar[19]));
	DEFINE(VMM_CTX_FCR_OFFSET, offsetof(union context, ar[21]));
	DEFINE(VMM_CTX_EFLAG_OFFSET, offsetof(union context, ar[24]));
	DEFINE(VMM_CTX_CFLG_OFFSET, offsetof(union context, ar[27]));
	DEFINE(VMM_CTX_FSR_OFFSET, offsetof(union context, ar[28]));
	DEFINE(VMM_CTX_FIR_OFFSET, offsetof(union context, ar[29]));
	DEFINE(VMM_CTX_FDR_OFFSET, offsetof(union context, ar[30]));
	DEFINE(VMM_CTX_UNAT_OFFSET, offsetof(union context, ar[36]));
	DEFINE(VMM_CTX_FPSR_OFFSET, offsetof(union context, ar[40]));
	DEFINE(VMM_CTX_PFS_OFFSET, offsetof(union context, ar[64]));
	DEFINE(VMM_CTX_LC_OFFSET, offsetof(union context, ar[65]));
	DEFINE(VMM_CTX_DCR_OFFSET, offsetof(union context, cr[0]));
	DEFINE(VMM_CTX_IVA_OFFSET, offsetof(union context, cr[2]));
	DEFINE(VMM_CTX_PTA_OFFSET, offsetof(union context, cr[8]));
	DEFINE(VMM_CTX_IBR0_OFFSET, offsetof(union context, ibr[0]));
	DEFINE(VMM_CTX_DBR0_OFFSET, offsetof(union context, dbr[0]));
	DEFINE(VMM_CTX_F2_OFFSET, offsetof(union context, fr[2]));
	DEFINE(VMM_CTX_F3_OFFSET, offsetof(union context, fr[3]));
	DEFINE(VMM_CTX_F32_OFFSET, offsetof(union context, fr[32]));
	DEFINE(VMM_CTX_F33_OFFSET, offsetof(union context, fr[33]));
	DEFINE(VMM_CTX_PKR0_OFFSET, offsetof(union context, pkr[0]));
	DEFINE(VMM_CTX_PSR_OFFSET, offsetof(union context, psr));
	BLANK();
}
