// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#ifndef __ARM64_KVM_HYP_SWITCH_H__
#define __ARM64_KVM_HYP_SWITCH_H__

#include <hyp/adjust_pc.h>
#include <hyp/fault.h>

#include <linux/arm-smccc.h>
#include <linux/kvm_host.h>
#include <linux/types.h>
#include <linux/jump_label.h>
#include <uapi/linux/psci.h>

#include <kvm/arm_psci.h>

#include <asm/barrier.h>
#include <asm/cpufeature.h>
#include <asm/extable.h>
#include <asm/kprobes.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_hyp.h>
#include <asm/kvm_mmu.h>
#include <asm/kvm_nested.h>
#include <asm/fpsimd.h>
#include <asm/debug-monitors.h>
#include <asm/processor.h>
#include <asm/traps.h>

struct kvm_exception_table_entry {
	int insn, fixup;
};

extern struct kvm_exception_table_entry __start___kvm_ex_table;
extern struct kvm_exception_table_entry __stop___kvm_ex_table;

/* Save the 32-bit only FPSIMD system register state */
static inline void __fpsimd_save_fpexc32(struct kvm_vcpu *vcpu)
{
	if (!vcpu_el1_is_32bit(vcpu))
		return;

	__vcpu_assign_sys_reg(vcpu, FPEXC32_EL2, read_sysreg(fpexc32_el2));
}

static inline void __activate_traps_fpsimd32(struct kvm_vcpu *vcpu)
{
	/*
	 * We are about to set CPTR_EL2.TFP to trap all floating point
	 * register accesses to EL2, however, the ARM ARM clearly states that
	 * traps are only taken to EL2 if the operation would not otherwise
	 * trap to EL1.  Therefore, always make sure that for 32-bit guests,
	 * we set FPEXC.EN to prevent traps to EL1, when setting the TFP bit.
	 * If FP/ASIMD is not implemented, FPEXC is UNDEFINED and any access to
	 * it will cause an exception.
	 */
	if (vcpu_el1_is_32bit(vcpu) && system_supports_fpsimd()) {
		write_sysreg(1 << 30, fpexc32_el2);
		isb();
	}
}

static inline void __activate_cptr_traps_nvhe(struct kvm_vcpu *vcpu)
{
	u64 val = CPTR_NVHE_EL2_RES1 | CPTR_EL2_TAM | CPTR_EL2_TTA;

	/*
	 * Always trap SME since it's not supported in KVM.
	 * TSM is RES1 if SME isn't implemented.
	 */
	val |= CPTR_EL2_TSM;

	if (!vcpu_has_sve(vcpu) || !guest_owns_fp_regs())
		val |= CPTR_EL2_TZ;

	if (!guest_owns_fp_regs())
		val |= CPTR_EL2_TFP;

	write_sysreg(val, cptr_el2);
}

static inline void __activate_cptr_traps_vhe(struct kvm_vcpu *vcpu)
{
	/*
	 * With VHE (HCR.E2H == 1), accesses to CPACR_EL1 are routed to
	 * CPTR_EL2. In general, CPACR_EL1 has the same layout as CPTR_EL2,
	 * except for some missing controls, such as TAM.
	 * In this case, CPTR_EL2.TAM has the same position with or without
	 * VHE (HCR.E2H == 1) which allows us to use here the CPTR_EL2.TAM
	 * shift value for trapping the AMU accesses.
	 */
	u64 val = CPTR_EL2_TAM | CPACR_EL1_TTA;
	u64 cptr;

	if (guest_owns_fp_regs()) {
		val |= CPACR_EL1_FPEN;
		if (vcpu_has_sve(vcpu))
			val |= CPACR_EL1_ZEN;
	}

	if (!vcpu_has_nv(vcpu))
		goto write;

	/*
	 * The architecture is a bit crap (what a surprise): an EL2 guest
	 * writing to CPTR_EL2 via CPACR_EL1 can't set any of TCPAC or TTA,
	 * as they are RES0 in the guest's view. To work around it, trap the
	 * sucker using the very same bit it can't set...
	 */
	if (vcpu_el2_e2h_is_set(vcpu) && is_hyp_ctxt(vcpu))
		val |= CPTR_EL2_TCPAC;

	/*
	 * Layer the guest hypervisor's trap configuration on top of our own if
	 * we're in a nested context.
	 */
	if (is_hyp_ctxt(vcpu))
		goto write;

	cptr = vcpu_sanitised_cptr_el2(vcpu);

	/*
	 * Pay attention, there's some interesting detail here.
	 *
	 * The CPTR_EL2.xEN fields are 2 bits wide, although there are only two
	 * meaningful trap states when HCR_EL2.TGE = 0 (running a nested guest):
	 *
	 *  - CPTR_EL2.xEN = x0, traps are enabled
	 *  - CPTR_EL2.xEN = x1, traps are disabled
	 *
	 * In other words, bit[0] determines if guest accesses trap or not. In
	 * the interest of simplicity, clear the entire field if the guest
	 * hypervisor has traps enabled to dispel any illusion of something more
	 * complicated taking place.
	 */
	if (!(SYS_FIELD_GET(CPACR_EL1, FPEN, cptr) & BIT(0)))
		val &= ~CPACR_EL1_FPEN;
	if (!(SYS_FIELD_GET(CPACR_EL1, ZEN, cptr) & BIT(0)))
		val &= ~CPACR_EL1_ZEN;

	if (kvm_has_feat(vcpu->kvm, ID_AA64MMFR3_EL1, S2POE, IMP))
		val |= cptr & CPACR_EL1_E0POE;

	val |= cptr & CPTR_EL2_TCPAC;

write:
	write_sysreg(val, cpacr_el1);
}

static inline void __activate_cptr_traps(struct kvm_vcpu *vcpu)
{
	if (!guest_owns_fp_regs())
		__activate_traps_fpsimd32(vcpu);

	if (has_vhe() || has_hvhe())
		__activate_cptr_traps_vhe(vcpu);
	else
		__activate_cptr_traps_nvhe(vcpu);
}

static inline void __deactivate_cptr_traps_nvhe(struct kvm_vcpu *vcpu)
{
	u64 val = CPTR_NVHE_EL2_RES1;

	if (!cpus_have_final_cap(ARM64_SVE))
		val |= CPTR_EL2_TZ;
	if (!cpus_have_final_cap(ARM64_SME))
		val |= CPTR_EL2_TSM;

	write_sysreg(val, cptr_el2);
}

static inline void __deactivate_cptr_traps_vhe(struct kvm_vcpu *vcpu)
{
	u64 val = CPACR_EL1_FPEN;

	if (cpus_have_final_cap(ARM64_SVE))
		val |= CPACR_EL1_ZEN;
	if (cpus_have_final_cap(ARM64_SME))
		val |= CPACR_EL1_SMEN;

	write_sysreg(val, cpacr_el1);
}

static inline void __deactivate_cptr_traps(struct kvm_vcpu *vcpu)
{
	if (has_vhe() || has_hvhe())
		__deactivate_cptr_traps_vhe(vcpu);
	else
		__deactivate_cptr_traps_nvhe(vcpu);
}

#define reg_to_fgt_masks(reg)						\
	({								\
		struct fgt_masks *m;					\
		switch(reg) {						\
		case HFGRTR_EL2:					\
			m = &hfgrtr_masks;				\
			break;						\
		case HFGWTR_EL2:					\
			m = &hfgwtr_masks;				\
			break;						\
		case HFGITR_EL2:					\
			m = &hfgitr_masks;				\
			break;						\
		case HDFGRTR_EL2:					\
			m = &hdfgrtr_masks;				\
			break;						\
		case HDFGWTR_EL2:					\
			m = &hdfgwtr_masks;				\
			break;						\
		case HAFGRTR_EL2:					\
			m = &hafgrtr_masks;				\
			break;						\
		case HFGRTR2_EL2:					\
			m = &hfgrtr2_masks;				\
			break;						\
		case HFGWTR2_EL2:					\
			m = &hfgwtr2_masks;				\
			break;						\
		case HFGITR2_EL2:					\
			m = &hfgitr2_masks;				\
			break;						\
		case HDFGRTR2_EL2:					\
			m = &hdfgrtr2_masks;				\
			break;						\
		case HDFGWTR2_EL2:					\
			m = &hdfgwtr2_masks;				\
			break;						\
		default:						\
			BUILD_BUG_ON(1);				\
		}							\
									\
		m;							\
	})

#define compute_clr_set(vcpu, reg, clr, set)				\
	do {								\
		u64 hfg = __vcpu_sys_reg(vcpu, reg);			\
		struct fgt_masks *m = reg_to_fgt_masks(reg);		\
		set |= hfg & m->mask;					\
		clr |= ~hfg & m->nmask;					\
	} while(0)

#define reg_to_fgt_group_id(reg)					\
	({								\
		enum fgt_group_id id;					\
		switch(reg) {						\
		case HFGRTR_EL2:					\
		case HFGWTR_EL2:					\
			id = HFGRTR_GROUP;				\
			break;						\
		case HFGITR_EL2:					\
			id = HFGITR_GROUP;				\
			break;						\
		case HDFGRTR_EL2:					\
		case HDFGWTR_EL2:					\
			id = HDFGRTR_GROUP;				\
			break;						\
		case HAFGRTR_EL2:					\
			id = HAFGRTR_GROUP;				\
			break;						\
		case HFGRTR2_EL2:					\
		case HFGWTR2_EL2:					\
			id = HFGRTR2_GROUP;				\
			break;						\
		case HFGITR2_EL2:					\
			id = HFGITR2_GROUP;				\
			break;						\
		case HDFGRTR2_EL2:					\
		case HDFGWTR2_EL2:					\
			id = HDFGRTR2_GROUP;				\
			break;						\
		default:						\
			BUILD_BUG_ON(1);				\
		}							\
									\
		id;							\
	})

#define compute_undef_clr_set(vcpu, kvm, reg, clr, set)			\
	do {								\
		u64 hfg = kvm->arch.fgu[reg_to_fgt_group_id(reg)];	\
		struct fgt_masks *m = reg_to_fgt_masks(reg);		\
		set |= hfg & m->mask;					\
		clr |= hfg & m->nmask;					\
	} while(0)

#define update_fgt_traps_cs(hctxt, vcpu, kvm, reg, clr, set)		\
	do {								\
		struct fgt_masks *m = reg_to_fgt_masks(reg);		\
		u64 c = clr, s = set;					\
		u64 val;						\
									\
		ctxt_sys_reg(hctxt, reg) = read_sysreg_s(SYS_ ## reg);	\
		if (is_nested_ctxt(vcpu))				\
			compute_clr_set(vcpu, reg, c, s);		\
									\
		compute_undef_clr_set(vcpu, kvm, reg, c, s);		\
									\
		val = m->nmask;						\
		val |= s;						\
		val &= ~c;						\
		write_sysreg_s(val, SYS_ ## reg);			\
	} while(0)

#define update_fgt_traps(hctxt, vcpu, kvm, reg)		\
	update_fgt_traps_cs(hctxt, vcpu, kvm, reg, 0, 0)

static inline bool cpu_has_amu(void)
{
       u64 pfr0 = read_sysreg_s(SYS_ID_AA64PFR0_EL1);

       return cpuid_feature_extract_unsigned_field(pfr0,
               ID_AA64PFR0_EL1_AMU_SHIFT);
}

static inline void __activate_traps_hfgxtr(struct kvm_vcpu *vcpu)
{
	struct kvm_cpu_context *hctxt = host_data_ptr(host_ctxt);
	struct kvm *kvm = kern_hyp_va(vcpu->kvm);

	if (!cpus_have_final_cap(ARM64_HAS_FGT))
		return;

	update_fgt_traps(hctxt, vcpu, kvm, HFGRTR_EL2);
	update_fgt_traps_cs(hctxt, vcpu, kvm, HFGWTR_EL2, 0,
			    cpus_have_final_cap(ARM64_WORKAROUND_AMPERE_AC03_CPU_38) ?
			    HFGWTR_EL2_TCR_EL1_MASK : 0);
	update_fgt_traps(hctxt, vcpu, kvm, HFGITR_EL2);
	update_fgt_traps(hctxt, vcpu, kvm, HDFGRTR_EL2);
	update_fgt_traps(hctxt, vcpu, kvm, HDFGWTR_EL2);

	if (cpu_has_amu())
		update_fgt_traps(hctxt, vcpu, kvm, HAFGRTR_EL2);

	if (!cpus_have_final_cap(ARM64_HAS_FGT2))
	    return;

	update_fgt_traps(hctxt, vcpu, kvm, HFGRTR2_EL2);
	update_fgt_traps(hctxt, vcpu, kvm, HFGWTR2_EL2);
	update_fgt_traps(hctxt, vcpu, kvm, HFGITR2_EL2);
	update_fgt_traps(hctxt, vcpu, kvm, HDFGRTR2_EL2);
	update_fgt_traps(hctxt, vcpu, kvm, HDFGWTR2_EL2);
}

#define __deactivate_fgt(htcxt, vcpu, reg)				\
	do {								\
		write_sysreg_s(ctxt_sys_reg(hctxt, reg),		\
			       SYS_ ## reg);				\
	} while(0)

static inline void __deactivate_traps_hfgxtr(struct kvm_vcpu *vcpu)
{
	struct kvm_cpu_context *hctxt = host_data_ptr(host_ctxt);

	if (!cpus_have_final_cap(ARM64_HAS_FGT))
		return;

	__deactivate_fgt(hctxt, vcpu, HFGRTR_EL2);
	__deactivate_fgt(hctxt, vcpu, HFGWTR_EL2);
	__deactivate_fgt(hctxt, vcpu, HFGITR_EL2);
	__deactivate_fgt(hctxt, vcpu, HDFGRTR_EL2);
	__deactivate_fgt(hctxt, vcpu, HDFGWTR_EL2);

	if (cpu_has_amu())
		__deactivate_fgt(hctxt, vcpu, HAFGRTR_EL2);

	if (!cpus_have_final_cap(ARM64_HAS_FGT2))
	    return;

	__deactivate_fgt(hctxt, vcpu, HFGRTR2_EL2);
	__deactivate_fgt(hctxt, vcpu, HFGWTR2_EL2);
	__deactivate_fgt(hctxt, vcpu, HFGITR2_EL2);
	__deactivate_fgt(hctxt, vcpu, HDFGRTR2_EL2);
	__deactivate_fgt(hctxt, vcpu, HDFGWTR2_EL2);
}

static inline void  __activate_traps_mpam(struct kvm_vcpu *vcpu)
{
	u64 r = MPAM2_EL2_TRAPMPAM0EL1 | MPAM2_EL2_TRAPMPAM1EL1;

	if (!system_supports_mpam())
		return;

	/* trap guest access to MPAMIDR_EL1 */
	if (system_supports_mpam_hcr()) {
		write_sysreg_s(MPAMHCR_EL2_TRAP_MPAMIDR_EL1, SYS_MPAMHCR_EL2);
	} else {
		/* From v1.1 TIDR can trap MPAMIDR, set it unconditionally */
		r |= MPAM2_EL2_TIDR;
	}

	write_sysreg_s(r, SYS_MPAM2_EL2);
}

static inline void __deactivate_traps_mpam(void)
{
	if (!system_supports_mpam())
		return;

	write_sysreg_s(0, SYS_MPAM2_EL2);

	if (system_supports_mpam_hcr())
		write_sysreg_s(MPAMHCR_HOST_FLAGS, SYS_MPAMHCR_EL2);
}

static inline void __activate_traps_common(struct kvm_vcpu *vcpu)
{
	struct kvm_cpu_context *hctxt = host_data_ptr(host_ctxt);

	/* Trap on AArch32 cp15 c15 (impdef sysregs) accesses (EL1 or EL0) */
	write_sysreg(1 << 15, hstr_el2);

	/*
	 * Make sure we trap PMU access from EL0 to EL2. Also sanitize
	 * PMSELR_EL0 to make sure it never contains the cycle
	 * counter, which could make a PMXEVCNTR_EL0 access UNDEF at
	 * EL1 instead of being trapped to EL2.
	 */
	if (system_supports_pmuv3()) {
		write_sysreg(0, pmselr_el0);

		ctxt_sys_reg(hctxt, PMUSERENR_EL0) = read_sysreg(pmuserenr_el0);
		write_sysreg(ARMV8_PMU_USERENR_MASK, pmuserenr_el0);
		vcpu_set_flag(vcpu, PMUSERENR_ON_CPU);
	}

	*host_data_ptr(host_debug_state.mdcr_el2) = read_sysreg(mdcr_el2);
	write_sysreg(vcpu->arch.mdcr_el2, mdcr_el2);

	if (cpus_have_final_cap(ARM64_HAS_HCX)) {
		u64 hcrx = vcpu->arch.hcrx_el2;
		if (is_nested_ctxt(vcpu)) {
			u64 val = __vcpu_sys_reg(vcpu, HCRX_EL2);
			hcrx |= val & __HCRX_EL2_MASK;
			hcrx &= ~(~val & __HCRX_EL2_nMASK);
		}

		ctxt_sys_reg(hctxt, HCRX_EL2) = read_sysreg_s(SYS_HCRX_EL2);
		write_sysreg_s(hcrx, SYS_HCRX_EL2);
	}

	__activate_traps_hfgxtr(vcpu);
	__activate_traps_mpam(vcpu);
}

static inline void __deactivate_traps_common(struct kvm_vcpu *vcpu)
{
	struct kvm_cpu_context *hctxt = host_data_ptr(host_ctxt);

	write_sysreg(*host_data_ptr(host_debug_state.mdcr_el2), mdcr_el2);

	write_sysreg(0, hstr_el2);
	if (system_supports_pmuv3()) {
		write_sysreg(ctxt_sys_reg(hctxt, PMUSERENR_EL0), pmuserenr_el0);
		vcpu_clear_flag(vcpu, PMUSERENR_ON_CPU);
	}

	if (cpus_have_final_cap(ARM64_HAS_HCX))
		write_sysreg_s(ctxt_sys_reg(hctxt, HCRX_EL2), SYS_HCRX_EL2);

	__deactivate_traps_hfgxtr(vcpu);
	__deactivate_traps_mpam();
}

static inline void ___activate_traps(struct kvm_vcpu *vcpu, u64 hcr)
{
	if (cpus_have_final_cap(ARM64_WORKAROUND_CAVIUM_TX2_219_TVM))
		hcr |= HCR_TVM;

	write_sysreg_hcr(hcr);

	if (cpus_have_final_cap(ARM64_HAS_RAS_EXTN) && (hcr & HCR_VSE)) {
		u64 vsesr;

		/*
		 * When HCR_EL2.AMO is set, physical SErrors are taken to EL2
		 * and vSError injection is enabled for EL1. Conveniently, for
		 * NV this means that it is never the case where a 'physical'
		 * SError (injected by KVM or userspace) and vSError are
		 * deliverable to the same context.
		 *
		 * As such, we can trivially select between the host or guest's
		 * VSESR_EL2. Except for the case that FEAT_RAS hasn't been
		 * exposed to the guest, where ESR propagation in hardware
		 * occurs unconditionally.
		 *
		 * Paper over the architectural wart and use an IMPLEMENTATION
		 * DEFINED ESR value in case FEAT_RAS is hidden from the guest.
		 */
		if (!vserror_state_is_nested(vcpu))
			vsesr = vcpu->arch.vsesr_el2;
		else if (kvm_has_ras(kern_hyp_va(vcpu->kvm)))
			vsesr = __vcpu_sys_reg(vcpu, VSESR_EL2);
		else
			vsesr = ESR_ELx_ISV;

		write_sysreg_s(vsesr, SYS_VSESR_EL2);
	}
}

static inline void ___deactivate_traps(struct kvm_vcpu *vcpu)
{
	u64 *hcr;

	if (vserror_state_is_nested(vcpu))
		hcr = __ctxt_sys_reg(&vcpu->arch.ctxt, HCR_EL2);
	else
		hcr = &vcpu->arch.hcr_el2;

	/*
	 * If we pended a virtual abort, preserve it until it gets
	 * cleared. See D1.14.3 (Virtual Interrupts) for details, but
	 * the crucial bit is "On taking a vSError interrupt,
	 * HCR_EL2.VSE is cleared to 0."
	 *
	 * Additionally, when in a nested context we need to propagate the
	 * updated state to the guest hypervisor's HCR_EL2.
	 */
	if (*hcr & HCR_VSE) {
		*hcr &= ~HCR_VSE;
		*hcr |= read_sysreg(hcr_el2) & HCR_VSE;
	}
}

static inline bool __populate_fault_info(struct kvm_vcpu *vcpu)
{
	return __get_fault_info(vcpu->arch.fault.esr_el2, &vcpu->arch.fault);
}

static inline bool kvm_hyp_handle_mops(struct kvm_vcpu *vcpu, u64 *exit_code)
{
	*vcpu_pc(vcpu) = read_sysreg_el2(SYS_ELR);
	arm64_mops_reset_regs(vcpu_gp_regs(vcpu), vcpu->arch.fault.esr_el2);
	write_sysreg_el2(*vcpu_pc(vcpu), SYS_ELR);

	/*
	 * Finish potential single step before executing the prologue
	 * instruction.
	 */
	*vcpu_cpsr(vcpu) &= ~DBG_SPSR_SS;
	write_sysreg_el2(*vcpu_cpsr(vcpu), SYS_SPSR);

	return true;
}

static inline void __hyp_sve_restore_guest(struct kvm_vcpu *vcpu)
{
	/*
	 * The vCPU's saved SVE state layout always matches the max VL of the
	 * vCPU. Start off with the max VL so we can load the SVE state.
	 */
	sve_cond_update_zcr_vq(vcpu_sve_max_vq(vcpu) - 1, SYS_ZCR_EL2);
	__sve_restore_state(vcpu_sve_pffr(vcpu),
			    &vcpu->arch.ctxt.fp_regs.fpsr,
			    true);

	/*
	 * The effective VL for a VM could differ from the max VL when running a
	 * nested guest, as the guest hypervisor could select a smaller VL. Slap
	 * that into hardware before wrapping up.
	 */
	if (is_nested_ctxt(vcpu))
		sve_cond_update_zcr_vq(__vcpu_sys_reg(vcpu, ZCR_EL2), SYS_ZCR_EL2);

	write_sysreg_el1(__vcpu_sys_reg(vcpu, vcpu_sve_zcr_elx(vcpu)), SYS_ZCR);
}

static inline void __hyp_sve_save_host(void)
{
	struct cpu_sve_state *sve_state = *host_data_ptr(sve_state);

	sve_state->zcr_el1 = read_sysreg_el1(SYS_ZCR);
	write_sysreg_s(sve_vq_from_vl(kvm_host_sve_max_vl) - 1, SYS_ZCR_EL2);
	__sve_save_state(sve_state->sve_regs + sve_ffr_offset(kvm_host_sve_max_vl),
			 &sve_state->fpsr,
			 true);
}

static inline void fpsimd_lazy_switch_to_guest(struct kvm_vcpu *vcpu)
{
	u64 zcr_el1, zcr_el2;

	if (!guest_owns_fp_regs())
		return;

	if (vcpu_has_sve(vcpu)) {
		/* A guest hypervisor may restrict the effective max VL. */
		if (is_nested_ctxt(vcpu))
			zcr_el2 = __vcpu_sys_reg(vcpu, ZCR_EL2);
		else
			zcr_el2 = vcpu_sve_max_vq(vcpu) - 1;

		write_sysreg_el2(zcr_el2, SYS_ZCR);

		zcr_el1 = __vcpu_sys_reg(vcpu, vcpu_sve_zcr_elx(vcpu));
		write_sysreg_el1(zcr_el1, SYS_ZCR);
	}
}

static inline void fpsimd_lazy_switch_to_host(struct kvm_vcpu *vcpu)
{
	u64 zcr_el1, zcr_el2;

	if (!guest_owns_fp_regs())
		return;

	/*
	 * When the guest owns the FP regs, we know that guest+hyp traps for
	 * any FPSIMD/SVE/SME features exposed to the guest have been disabled
	 * by either fpsimd_lazy_switch_to_guest() or kvm_hyp_handle_fpsimd()
	 * prior to __guest_entry(). As __guest_entry() guarantees a context
	 * synchronization event, we don't need an ISB here to avoid taking
	 * traps for anything that was exposed to the guest.
	 */
	if (vcpu_has_sve(vcpu)) {
		zcr_el1 = read_sysreg_el1(SYS_ZCR);
		__vcpu_assign_sys_reg(vcpu, vcpu_sve_zcr_elx(vcpu), zcr_el1);

		/*
		 * The guest's state is always saved using the guest's max VL.
		 * Ensure that the host has the guest's max VL active such that
		 * the host can save the guest's state lazily, but don't
		 * artificially restrict the host to the guest's max VL.
		 */
		if (has_vhe()) {
			zcr_el2 = vcpu_sve_max_vq(vcpu) - 1;
			write_sysreg_el2(zcr_el2, SYS_ZCR);
		} else {
			zcr_el2 = sve_vq_from_vl(kvm_host_sve_max_vl) - 1;
			write_sysreg_el2(zcr_el2, SYS_ZCR);

			zcr_el1 = vcpu_sve_max_vq(vcpu) - 1;
			write_sysreg_el1(zcr_el1, SYS_ZCR);
		}
	}
}

static void kvm_hyp_save_fpsimd_host(struct kvm_vcpu *vcpu)
{
	/*
	 * Non-protected kvm relies on the host restoring its sve state.
	 * Protected kvm restores the host's sve state as not to reveal that
	 * fpsimd was used by a guest nor leak upper sve bits.
	 */
	if (system_supports_sve()) {
		__hyp_sve_save_host();
	} else {
		__fpsimd_save_state(host_data_ptr(host_ctxt.fp_regs));
	}

	if (kvm_has_fpmr(kern_hyp_va(vcpu->kvm)))
		*host_data_ptr(fpmr) = read_sysreg_s(SYS_FPMR);
}


/*
 * We trap the first access to the FP/SIMD to save the host context and
 * restore the guest context lazily.
 * If FP/SIMD is not implemented, handle the trap and inject an undefined
 * instruction exception to the guest. Similarly for trapped SVE accesses.
 */
static inline bool kvm_hyp_handle_fpsimd(struct kvm_vcpu *vcpu, u64 *exit_code)
{
	bool sve_guest;
	u8 esr_ec;

	if (!system_supports_fpsimd())
		return false;

	sve_guest = vcpu_has_sve(vcpu);
	esr_ec = kvm_vcpu_trap_get_class(vcpu);

	/* Only handle traps the vCPU can support here: */
	switch (esr_ec) {
	case ESR_ELx_EC_FP_ASIMD:
		/* Forward traps to the guest hypervisor as required */
		if (guest_hyp_fpsimd_traps_enabled(vcpu))
			return false;
		break;
	case ESR_ELx_EC_SYS64:
		if (WARN_ON_ONCE(!is_hyp_ctxt(vcpu)))
			return false;
		fallthrough;
	case ESR_ELx_EC_SVE:
		if (!sve_guest)
			return false;
		if (guest_hyp_sve_traps_enabled(vcpu))
			return false;
		break;
	default:
		return false;
	}

	/* Valid trap.  Switch the context: */

	/* First disable enough traps to allow us to update the registers */
	__deactivate_cptr_traps(vcpu);
	isb();

	/* Write out the host state if it's in the registers */
	if (is_protected_kvm_enabled() && host_owns_fp_regs())
		kvm_hyp_save_fpsimd_host(vcpu);

	/* Restore the guest state */
	if (sve_guest)
		__hyp_sve_restore_guest(vcpu);
	else
		__fpsimd_restore_state(&vcpu->arch.ctxt.fp_regs);

	if (kvm_has_fpmr(kern_hyp_va(vcpu->kvm)))
		write_sysreg_s(__vcpu_sys_reg(vcpu, FPMR), SYS_FPMR);

	/* Skip restoring fpexc32 for AArch64 guests */
	if (!(read_sysreg(hcr_el2) & HCR_RW))
		write_sysreg(__vcpu_sys_reg(vcpu, FPEXC32_EL2), fpexc32_el2);

	*host_data_ptr(fp_owner) = FP_STATE_GUEST_OWNED;

	/*
	 * Re-enable traps necessary for the current state of the guest, e.g.
	 * those enabled by a guest hypervisor. The ERET to the guest will
	 * provide the necessary context synchronization.
	 */
	__activate_cptr_traps(vcpu);

	return true;
}

static inline bool handle_tx2_tvm(struct kvm_vcpu *vcpu)
{
	u32 sysreg = esr_sys64_to_sysreg(kvm_vcpu_get_esr(vcpu));
	int rt = kvm_vcpu_sys_get_rt(vcpu);
	u64 val = vcpu_get_reg(vcpu, rt);

	/*
	 * The normal sysreg handling code expects to see the traps,
	 * let's not do anything here.
	 */
	if (vcpu->arch.hcr_el2 & HCR_TVM)
		return false;

	switch (sysreg) {
	case SYS_SCTLR_EL1:
		write_sysreg_el1(val, SYS_SCTLR);
		break;
	case SYS_TTBR0_EL1:
		write_sysreg_el1(val, SYS_TTBR0);
		break;
	case SYS_TTBR1_EL1:
		write_sysreg_el1(val, SYS_TTBR1);
		break;
	case SYS_TCR_EL1:
		write_sysreg_el1(val, SYS_TCR);
		break;
	case SYS_ESR_EL1:
		write_sysreg_el1(val, SYS_ESR);
		break;
	case SYS_FAR_EL1:
		write_sysreg_el1(val, SYS_FAR);
		break;
	case SYS_AFSR0_EL1:
		write_sysreg_el1(val, SYS_AFSR0);
		break;
	case SYS_AFSR1_EL1:
		write_sysreg_el1(val, SYS_AFSR1);
		break;
	case SYS_MAIR_EL1:
		write_sysreg_el1(val, SYS_MAIR);
		break;
	case SYS_AMAIR_EL1:
		write_sysreg_el1(val, SYS_AMAIR);
		break;
	case SYS_CONTEXTIDR_EL1:
		write_sysreg_el1(val, SYS_CONTEXTIDR);
		break;
	default:
		return false;
	}

	__kvm_skip_instr(vcpu);
	return true;
}

/* Open-coded version of timer_get_offset() to allow for kern_hyp_va() */
static inline u64 hyp_timer_get_offset(struct arch_timer_context *ctxt)
{
	u64 offset = 0;

	if (ctxt->offset.vm_offset)
		offset += *kern_hyp_va(ctxt->offset.vm_offset);
	if (ctxt->offset.vcpu_offset)
		offset += *kern_hyp_va(ctxt->offset.vcpu_offset);

	return offset;
}

static inline u64 compute_counter_value(struct arch_timer_context *ctxt)
{
	return arch_timer_read_cntpct_el0() - hyp_timer_get_offset(ctxt);
}

static bool kvm_handle_cntxct(struct kvm_vcpu *vcpu)
{
	struct arch_timer_context *ctxt;
	u32 sysreg;
	u64 val;

	/*
	 * We only get here for 64bit guests, 32bit guests will hit
	 * the long and winding road all the way to the standard
	 * handling. Yes, it sucks to be irrelevant.
	 *
	 * Also, we only deal with non-hypervisor context here (either
	 * an EL1 guest, or a non-HYP context of an EL2 guest).
	 */
	if (is_hyp_ctxt(vcpu))
		return false;

	sysreg = esr_sys64_to_sysreg(kvm_vcpu_get_esr(vcpu));

	switch (sysreg) {
	case SYS_CNTPCT_EL0:
	case SYS_CNTPCTSS_EL0:
		if (vcpu_has_nv(vcpu)) {
			/* Check for guest hypervisor trapping */
			val = __vcpu_sys_reg(vcpu, CNTHCTL_EL2);
			if (!vcpu_el2_e2h_is_set(vcpu))
				val = (val & CNTHCTL_EL1PCTEN) << 10;

			if (!(val & (CNTHCTL_EL1PCTEN << 10)))
				return false;
		}

		ctxt = vcpu_ptimer(vcpu);
		break;
	case SYS_CNTVCT_EL0:
	case SYS_CNTVCTSS_EL0:
		if (vcpu_has_nv(vcpu)) {
			/* Check for guest hypervisor trapping */
			val = __vcpu_sys_reg(vcpu, CNTHCTL_EL2);

			if (val & CNTHCTL_EL1TVCT)
				return false;
		}

		ctxt = vcpu_vtimer(vcpu);
		break;
	default:
		return false;
	}

	val = compute_counter_value(ctxt);

	vcpu_set_reg(vcpu, kvm_vcpu_sys_get_rt(vcpu), val);
	__kvm_skip_instr(vcpu);
	return true;
}

static bool handle_ampere1_tcr(struct kvm_vcpu *vcpu)
{
	u32 sysreg = esr_sys64_to_sysreg(kvm_vcpu_get_esr(vcpu));
	int rt = kvm_vcpu_sys_get_rt(vcpu);
	u64 val = vcpu_get_reg(vcpu, rt);

	if (sysreg != SYS_TCR_EL1)
		return false;

	/*
	 * Affected parts do not advertise support for hardware Access Flag /
	 * Dirty state management in ID_AA64MMFR1_EL1.HAFDBS, but the underlying
	 * control bits are still functional. The architecture requires these be
	 * RES0 on systems that do not implement FEAT_HAFDBS.
	 *
	 * Uphold the requirements of the architecture by masking guest writes
	 * to TCR_EL1.{HA,HD} here.
	 */
	val &= ~(TCR_HD | TCR_HA);
	write_sysreg_el1(val, SYS_TCR);
	__kvm_skip_instr(vcpu);
	return true;
}

static inline bool kvm_hyp_handle_sysreg(struct kvm_vcpu *vcpu, u64 *exit_code)
{
	if (cpus_have_final_cap(ARM64_WORKAROUND_CAVIUM_TX2_219_TVM) &&
	    handle_tx2_tvm(vcpu))
		return true;

	if (cpus_have_final_cap(ARM64_WORKAROUND_AMPERE_AC03_CPU_38) &&
	    handle_ampere1_tcr(vcpu))
		return true;

	if (static_branch_unlikely(&vgic_v3_cpuif_trap) &&
	    __vgic_v3_perform_cpuif_access(vcpu) == 1)
		return true;

	if (kvm_handle_cntxct(vcpu))
		return true;

	return false;
}

static inline bool kvm_hyp_handle_cp15_32(struct kvm_vcpu *vcpu, u64 *exit_code)
{
	if (static_branch_unlikely(&vgic_v3_cpuif_trap) &&
	    __vgic_v3_perform_cpuif_access(vcpu) == 1)
		return true;

	return false;
}

static inline bool kvm_hyp_handle_memory_fault(struct kvm_vcpu *vcpu,
					       u64 *exit_code)
{
	if (!__populate_fault_info(vcpu))
		return true;

	return false;
}
#define kvm_hyp_handle_iabt_low		kvm_hyp_handle_memory_fault
#define kvm_hyp_handle_watchpt_low	kvm_hyp_handle_memory_fault

static inline bool kvm_hyp_handle_dabt_low(struct kvm_vcpu *vcpu, u64 *exit_code)
{
	if (kvm_hyp_handle_memory_fault(vcpu, exit_code))
		return true;

	if (static_branch_unlikely(&vgic_v2_cpuif_trap)) {
		bool valid;

		valid = kvm_vcpu_trap_is_translation_fault(vcpu) &&
			kvm_vcpu_dabt_isvalid(vcpu) &&
			!kvm_vcpu_abt_issea(vcpu) &&
			!kvm_vcpu_abt_iss1tw(vcpu);

		if (valid) {
			int ret = __vgic_v2_perform_cpuif_access(vcpu);

			if (ret == 1)
				return true;

			/* Promote an illegal access to an SError.*/
			if (ret == -1)
				*exit_code = ARM_EXCEPTION_EL1_SERROR;
		}
	}

	return false;
}

typedef bool (*exit_handler_fn)(struct kvm_vcpu *, u64 *);

/*
 * Allow the hypervisor to handle the exit with an exit handler if it has one.
 *
 * Returns true if the hypervisor handled the exit, and control should go back
 * to the guest, or false if it hasn't.
 */
static inline bool kvm_hyp_handle_exit(struct kvm_vcpu *vcpu, u64 *exit_code,
				       const exit_handler_fn *handlers)
{
	exit_handler_fn fn = handlers[kvm_vcpu_trap_get_class(vcpu)];
	if (fn)
		return fn(vcpu, exit_code);

	return false;
}

static inline void synchronize_vcpu_pstate(struct kvm_vcpu *vcpu, u64 *exit_code)
{
	/*
	 * Check for the conditions of Cortex-A510's #2077057. When these occur
	 * SPSR_EL2 can't be trusted, but isn't needed either as it is
	 * unchanged from the value in vcpu_gp_regs(vcpu)->pstate.
	 * Are we single-stepping the guest, and took a PAC exception from the
	 * active-not-pending state?
	 */
	if (cpus_have_final_cap(ARM64_WORKAROUND_2077057)		&&
	    vcpu->guest_debug & KVM_GUESTDBG_SINGLESTEP			&&
	    *vcpu_cpsr(vcpu) & DBG_SPSR_SS				&&
	    ESR_ELx_EC(read_sysreg_el2(SYS_ESR)) == ESR_ELx_EC_PAC)
		write_sysreg_el2(*vcpu_cpsr(vcpu), SYS_SPSR);

	vcpu->arch.ctxt.regs.pstate = read_sysreg_el2(SYS_SPSR);
}

/*
 * Return true when we were able to fixup the guest exit and should return to
 * the guest, false when we should restore the host state and return to the
 * main run loop.
 */
static inline bool __fixup_guest_exit(struct kvm_vcpu *vcpu, u64 *exit_code,
				      const exit_handler_fn *handlers)
{
	if (ARM_EXCEPTION_CODE(*exit_code) != ARM_EXCEPTION_IRQ)
		vcpu->arch.fault.esr_el2 = read_sysreg_el2(SYS_ESR);

	if (ARM_SERROR_PENDING(*exit_code) &&
	    ARM_EXCEPTION_CODE(*exit_code) != ARM_EXCEPTION_IRQ) {
		u8 esr_ec = kvm_vcpu_trap_get_class(vcpu);

		/*
		 * HVC already have an adjusted PC, which we need to
		 * correct in order to return to after having injected
		 * the SError.
		 *
		 * SMC, on the other hand, is *trapped*, meaning its
		 * preferred return address is the SMC itself.
		 */
		if (esr_ec == ESR_ELx_EC_HVC32 || esr_ec == ESR_ELx_EC_HVC64)
			write_sysreg_el2(read_sysreg_el2(SYS_ELR) - 4, SYS_ELR);
	}

	/*
	 * We're using the raw exception code in order to only process
	 * the trap if no SError is pending. We will come back to the
	 * same PC once the SError has been injected, and replay the
	 * trapping instruction.
	 */
	if (*exit_code != ARM_EXCEPTION_TRAP)
		goto exit;

	/* Check if there's an exit handler and allow it to handle the exit. */
	if (kvm_hyp_handle_exit(vcpu, exit_code, handlers))
		goto guest;
exit:
	/* Return to the host kernel and handle the exit */
	return false;

guest:
	/* Re-enter the guest */
	asm(ALTERNATIVE("nop", "dmb sy", ARM64_WORKAROUND_1508412));
	return true;
}

static inline void __kvm_unexpected_el2_exception(void)
{
	extern char __guest_exit_restore_elr_and_panic[];
	unsigned long addr, fixup;
	struct kvm_exception_table_entry *entry, *end;
	unsigned long elr_el2 = read_sysreg(elr_el2);

	entry = &__start___kvm_ex_table;
	end = &__stop___kvm_ex_table;

	while (entry < end) {
		addr = (unsigned long)&entry->insn + entry->insn;
		fixup = (unsigned long)&entry->fixup + entry->fixup;

		if (addr != elr_el2) {
			entry++;
			continue;
		}

		write_sysreg(fixup, elr_el2);
		return;
	}

	/* Trigger a panic after restoring the hyp context. */
	this_cpu_ptr(&kvm_hyp_ctxt)->sys_regs[ELR_EL2] = elr_el2;
	write_sysreg(__guest_exit_restore_elr_and_panic, elr_el2);
}

#endif /* __ARM64_KVM_HYP_SWITCH_H__ */
