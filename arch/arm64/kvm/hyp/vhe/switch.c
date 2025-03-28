// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#include <hyp/switch.h>

#include <linux/arm-smccc.h>
#include <linux/kvm_host.h>
#include <linux/types.h>
#include <linux/jump_label.h>
#include <linux/percpu.h>
#include <uapi/linux/psci.h>

#include <kvm/arm_psci.h>

#include <asm/barrier.h>
#include <asm/cpufeature.h>
#include <asm/kprobes.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_hyp.h>
#include <asm/kvm_mmu.h>
#include <asm/fpsimd.h>
#include <asm/debug-monitors.h>
#include <asm/processor.h>
#include <asm/thread_info.h>
#include <asm/vectors.h>

/* VHE specific context */
DEFINE_PER_CPU(struct kvm_host_data, kvm_host_data);
DEFINE_PER_CPU(struct kvm_cpu_context, kvm_hyp_ctxt);
DEFINE_PER_CPU(unsigned long, kvm_hyp_vector);

/*
 * HCR_EL2 bits that the NV guest can freely change (no RES0/RES1
 * semantics, irrespective of the configuration), but that cannot be
 * applied to the actual HW as things would otherwise break badly.
 *
 * - TGE: we want the guest to use EL1, which is incompatible with
 *   this bit being set
 *
 * - API/APK: they are already accounted for by vcpu_load(), and can
 *   only take effect across a load/put cycle (such as ERET)
 */
#define NV_HCR_GUEST_EXCLUDE	(HCR_TGE | HCR_API | HCR_APK)

static u64 __compute_hcr(struct kvm_vcpu *vcpu)
{
	u64 hcr = vcpu->arch.hcr_el2;

	if (!vcpu_has_nv(vcpu))
		return hcr;

	if (is_hyp_ctxt(vcpu)) {
		hcr |= HCR_NV | HCR_NV2 | HCR_AT | HCR_TTLB;

		if (!vcpu_el2_e2h_is_set(vcpu))
			hcr |= HCR_NV1;

		write_sysreg_s(vcpu->arch.ctxt.vncr_array, SYS_VNCR_EL2);
	}

	return hcr | (__vcpu_sys_reg(vcpu, HCR_EL2) & ~NV_HCR_GUEST_EXCLUDE);
}

static void __activate_cptr_traps(struct kvm_vcpu *vcpu)
{
	u64 cptr;

	/*
	 * With VHE (HCR.E2H == 1), accesses to CPACR_EL1 are routed to
	 * CPTR_EL2. In general, CPACR_EL1 has the same layout as CPTR_EL2,
	 * except for some missing controls, such as TAM.
	 * In this case, CPTR_EL2.TAM has the same position with or without
	 * VHE (HCR.E2H == 1) which allows us to use here the CPTR_EL2.TAM
	 * shift value for trapping the AMU accesses.
	 */
	u64 val = CPACR_EL1_TTA | CPTR_EL2_TAM;

	if (guest_owns_fp_regs()) {
		val |= CPACR_EL1_FPEN;
		if (vcpu_has_sve(vcpu))
			val |= CPACR_EL1_ZEN;
	} else {
		__activate_traps_fpsimd32(vcpu);
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

static void __deactivate_cptr_traps(struct kvm_vcpu *vcpu)
{
	u64 val = CPACR_EL1_FPEN | CPACR_EL1_ZEN_EL1EN;

	if (cpus_have_final_cap(ARM64_SME))
		val |= CPACR_EL1_SMEN_EL1EN;

	write_sysreg(val, cpacr_el1);
}

static void __activate_traps(struct kvm_vcpu *vcpu)
{
	u64 val;

	___activate_traps(vcpu, __compute_hcr(vcpu));

	if (has_cntpoff()) {
		struct timer_map map;

		get_timer_map(vcpu, &map);

		/*
		 * We're entrering the guest. Reload the correct
		 * values from memory now that TGE is clear.
		 */
		if (map.direct_ptimer == vcpu_ptimer(vcpu))
			val = __vcpu_sys_reg(vcpu, CNTP_CVAL_EL0);
		if (map.direct_ptimer == vcpu_hptimer(vcpu))
			val = __vcpu_sys_reg(vcpu, CNTHP_CVAL_EL2);

		if (map.direct_ptimer) {
			write_sysreg_el0(val, SYS_CNTP_CVAL);
			isb();
		}
	}

	__activate_cptr_traps(vcpu);

	write_sysreg(__this_cpu_read(kvm_hyp_vector), vbar_el1);
}
NOKPROBE_SYMBOL(__activate_traps);

static void __deactivate_traps(struct kvm_vcpu *vcpu)
{
	const char *host_vectors = vectors;

	___deactivate_traps(vcpu);

	write_sysreg(HCR_HOST_VHE_FLAGS, hcr_el2);

	if (has_cntpoff()) {
		struct timer_map map;
		u64 val, offset;

		get_timer_map(vcpu, &map);

		/*
		 * We're exiting the guest. Save the latest CVAL value
		 * to memory and apply the offset now that TGE is set.
		 */
		val = read_sysreg_el0(SYS_CNTP_CVAL);
		if (map.direct_ptimer == vcpu_ptimer(vcpu))
			__vcpu_sys_reg(vcpu, CNTP_CVAL_EL0) = val;
		if (map.direct_ptimer == vcpu_hptimer(vcpu))
			__vcpu_sys_reg(vcpu, CNTHP_CVAL_EL2) = val;

		offset = read_sysreg_s(SYS_CNTPOFF_EL2);

		if (map.direct_ptimer && offset) {
			write_sysreg_el0(val + offset, SYS_CNTP_CVAL);
			isb();
		}
	}

	/*
	 * ARM errata 1165522 and 1530923 require the actual execution of the
	 * above before we can switch to the EL2/EL0 translation regime used by
	 * the host.
	 */
	asm(ALTERNATIVE("nop", "isb", ARM64_WORKAROUND_SPECULATIVE_AT));

	__deactivate_cptr_traps(vcpu);

	if (!arm64_kernel_unmapped_at_el0())
		host_vectors = __this_cpu_read(this_cpu_vector);
	write_sysreg(host_vectors, vbar_el1);
}
NOKPROBE_SYMBOL(__deactivate_traps);

/*
 * Disable IRQs in __vcpu_{load,put}_{activate,deactivate}_traps() to
 * prevent a race condition between context switching of PMUSERENR_EL0
 * in __{activate,deactivate}_traps_common() and IPIs that attempts to
 * update PMUSERENR_EL0. See also kvm_set_pmuserenr().
 */
static void __vcpu_load_activate_traps(struct kvm_vcpu *vcpu)
{
	unsigned long flags;

	local_irq_save(flags);
	__activate_traps_common(vcpu);
	local_irq_restore(flags);
}

static void __vcpu_put_deactivate_traps(struct kvm_vcpu *vcpu)
{
	unsigned long flags;

	local_irq_save(flags);
	__deactivate_traps_common(vcpu);
	local_irq_restore(flags);
}

void kvm_vcpu_load_vhe(struct kvm_vcpu *vcpu)
{
	host_data_ptr(host_ctxt)->__hyp_running_vcpu = vcpu;

	__vcpu_load_switch_sysregs(vcpu);
	__vcpu_load_activate_traps(vcpu);
	__load_stage2(vcpu->arch.hw_mmu, vcpu->arch.hw_mmu->arch);
}

void kvm_vcpu_put_vhe(struct kvm_vcpu *vcpu)
{
	__vcpu_put_deactivate_traps(vcpu);
	__vcpu_put_switch_sysregs(vcpu);

	host_data_ptr(host_ctxt)->__hyp_running_vcpu = NULL;
}

static u64 compute_emulated_cntx_ctl_el0(struct kvm_vcpu *vcpu,
					 enum vcpu_sysreg reg)
{
	unsigned long ctl;
	u64 cval, cnt;
	bool stat;

	switch (reg) {
	case CNTP_CTL_EL0:
		cval = __vcpu_sys_reg(vcpu, CNTP_CVAL_EL0);
		ctl  = __vcpu_sys_reg(vcpu, CNTP_CTL_EL0);
		cnt  = compute_counter_value(vcpu_ptimer(vcpu));
		break;
	case CNTV_CTL_EL0:
		cval = __vcpu_sys_reg(vcpu, CNTV_CVAL_EL0);
		ctl  = __vcpu_sys_reg(vcpu, CNTV_CTL_EL0);
		cnt  = compute_counter_value(vcpu_vtimer(vcpu));
		break;
	default:
		BUG();
	}

	stat = cval <= cnt;
	__assign_bit(__ffs(ARCH_TIMER_CTRL_IT_STAT), &ctl, stat);

	return ctl;
}

static bool kvm_hyp_handle_timer(struct kvm_vcpu *vcpu, u64 *exit_code)
{
	u64 esr, val;

	/*
	 * Having FEAT_ECV allows for a better quality of timer emulation.
	 * However, this comes at a huge cost in terms of traps. Try and
	 * satisfy the reads from guest's hypervisor context without
	 * returning to the kernel if we can.
	 */
	if (!is_hyp_ctxt(vcpu))
		return false;

	esr = kvm_vcpu_get_esr(vcpu);
	if ((esr & ESR_ELx_SYS64_ISS_DIR_MASK) != ESR_ELx_SYS64_ISS_DIR_READ)
		return false;

	switch (esr_sys64_to_sysreg(esr)) {
	case SYS_CNTP_CTL_EL02:
		val = compute_emulated_cntx_ctl_el0(vcpu, CNTP_CTL_EL0);
		break;
	case SYS_CNTP_CTL_EL0:
		if (vcpu_el2_e2h_is_set(vcpu))
			val = read_sysreg_el0(SYS_CNTP_CTL);
		else
			val = compute_emulated_cntx_ctl_el0(vcpu, CNTP_CTL_EL0);
		break;
	case SYS_CNTP_CVAL_EL02:
		val = __vcpu_sys_reg(vcpu, CNTP_CVAL_EL0);
		break;
	case SYS_CNTP_CVAL_EL0:
		if (vcpu_el2_e2h_is_set(vcpu)) {
			val = read_sysreg_el0(SYS_CNTP_CVAL);

			if (!has_cntpoff())
				val -= timer_get_offset(vcpu_hptimer(vcpu));
		} else {
			val = __vcpu_sys_reg(vcpu, CNTP_CVAL_EL0);
		}
		break;
	case SYS_CNTPCT_EL0:
	case SYS_CNTPCTSS_EL0:
		val = compute_counter_value(vcpu_hptimer(vcpu));
		break;
	case SYS_CNTV_CTL_EL02:
		val = compute_emulated_cntx_ctl_el0(vcpu, CNTV_CTL_EL0);
		break;
	case SYS_CNTV_CTL_EL0:
		if (vcpu_el2_e2h_is_set(vcpu))
			val = read_sysreg_el0(SYS_CNTV_CTL);
		else
			val = compute_emulated_cntx_ctl_el0(vcpu, CNTV_CTL_EL0);
		break;
	case SYS_CNTV_CVAL_EL02:
		val = __vcpu_sys_reg(vcpu, CNTV_CVAL_EL0);
		break;
	case SYS_CNTV_CVAL_EL0:
		if (vcpu_el2_e2h_is_set(vcpu))
			val = read_sysreg_el0(SYS_CNTV_CVAL);
		else
			val = __vcpu_sys_reg(vcpu, CNTV_CVAL_EL0);
		break;
	case SYS_CNTVCT_EL0:
	case SYS_CNTVCTSS_EL0:
		val = compute_counter_value(vcpu_hvtimer(vcpu));
		break;
	default:
		return false;
	}

	vcpu_set_reg(vcpu, kvm_vcpu_sys_get_rt(vcpu), val);
	__kvm_skip_instr(vcpu);

	return true;
}

static bool kvm_hyp_handle_eret(struct kvm_vcpu *vcpu, u64 *exit_code)
{
	u64 esr = kvm_vcpu_get_esr(vcpu);
	u64 spsr, elr, mode;

	/*
	 * Going through the whole put/load motions is a waste of time
	 * if this is a VHE guest hypervisor returning to its own
	 * userspace, or the hypervisor performing a local exception
	 * return. No need to save/restore registers, no need to
	 * switch S2 MMU. Just do the canonical ERET.
	 *
	 * Unless the trap has to be forwarded further down the line,
	 * of course...
	 */
	if ((__vcpu_sys_reg(vcpu, HCR_EL2) & HCR_NV) ||
	    (__vcpu_sys_reg(vcpu, HFGITR_EL2) & HFGITR_EL2_ERET))
		return false;

	spsr = read_sysreg_el1(SYS_SPSR);
	mode = spsr & (PSR_MODE_MASK | PSR_MODE32_BIT);

	switch (mode) {
	case PSR_MODE_EL0t:
		if (!(vcpu_el2_e2h_is_set(vcpu) && vcpu_el2_tge_is_set(vcpu)))
			return false;
		break;
	case PSR_MODE_EL2t:
		mode = PSR_MODE_EL1t;
		break;
	case PSR_MODE_EL2h:
		mode = PSR_MODE_EL1h;
		break;
	default:
		return false;
	}

	/* If ERETAx fails, take the slow path */
	if (esr_iss_is_eretax(esr)) {
		if (!(vcpu_has_ptrauth(vcpu) && kvm_auth_eretax(vcpu, &elr)))
			return false;
	} else {
		elr = read_sysreg_el1(SYS_ELR);
	}

	spsr = (spsr & ~(PSR_MODE_MASK | PSR_MODE32_BIT)) | mode;

	write_sysreg_el2(spsr, SYS_SPSR);
	write_sysreg_el2(elr, SYS_ELR);

	return true;
}

static bool kvm_hyp_handle_tlbi_el2(struct kvm_vcpu *vcpu, u64 *exit_code)
{
	int ret = -EINVAL;
	u32 instr;
	u64 val;

	/*
	 * Ideally, we would never trap on EL2 S1 TLB invalidations using
	 * the EL1 instructions when the guest's HCR_EL2.{E2H,TGE}=={1,1}.
	 * But "thanks" to FEAT_NV2, we don't trap writes to HCR_EL2,
	 * meaning that we can't track changes to the virtual TGE bit. So we
	 * have to leave HCR_EL2.TTLB set on the host. Oopsie...
	 *
	 * Try and handle these invalidation as quickly as possible, without
	 * fully exiting. Note that we don't need to consider any forwarding
	 * here, as having E2H+TGE set is the very definition of being
	 * InHost.
	 *
	 * For the lesser hypervisors out there that have failed to get on
	 * with the VHE program, we can also handle the nVHE style of EL2
	 * invalidation.
	 */
	if (!(is_hyp_ctxt(vcpu)))
		return false;

	instr = esr_sys64_to_sysreg(kvm_vcpu_get_esr(vcpu));
	val = vcpu_get_reg(vcpu, kvm_vcpu_sys_get_rt(vcpu));

	if ((kvm_supported_tlbi_s1e1_op(vcpu, instr) &&
	     vcpu_el2_e2h_is_set(vcpu) && vcpu_el2_tge_is_set(vcpu)) ||
	    kvm_supported_tlbi_s1e2_op (vcpu, instr))
		ret = __kvm_tlbi_s1e2(NULL, val, instr);

	if (ret)
		return false;

	__kvm_skip_instr(vcpu);

	return true;
}

static bool kvm_hyp_handle_cpacr_el1(struct kvm_vcpu *vcpu, u64 *exit_code)
{
	u64 esr = kvm_vcpu_get_esr(vcpu);
	int rt;

	if (!is_hyp_ctxt(vcpu) || esr_sys64_to_sysreg(esr) != SYS_CPACR_EL1)
		return false;

	rt = kvm_vcpu_sys_get_rt(vcpu);

	if ((esr & ESR_ELx_SYS64_ISS_DIR_MASK) == ESR_ELx_SYS64_ISS_DIR_READ) {
		vcpu_set_reg(vcpu, rt, __vcpu_sys_reg(vcpu, CPTR_EL2));
	} else {
		vcpu_write_sys_reg(vcpu, vcpu_get_reg(vcpu, rt), CPTR_EL2);
		__activate_cptr_traps(vcpu);
	}

	__kvm_skip_instr(vcpu);

	return true;
}

static bool kvm_hyp_handle_zcr_el2(struct kvm_vcpu *vcpu, u64 *exit_code)
{
	u32 sysreg = esr_sys64_to_sysreg(kvm_vcpu_get_esr(vcpu));

	if (!vcpu_has_nv(vcpu))
		return false;

	if (sysreg != SYS_ZCR_EL2)
		return false;

	if (guest_owns_fp_regs())
		return false;

	/*
	 * ZCR_EL2 traps are handled in the slow path, with the expectation
	 * that the guest's FP context has already been loaded onto the CPU.
	 *
	 * Load the guest's FP context and unconditionally forward to the
	 * slow path for handling (i.e. return false).
	 */
	kvm_hyp_handle_fpsimd(vcpu, exit_code);
	return false;
}

static bool kvm_hyp_handle_sysreg_vhe(struct kvm_vcpu *vcpu, u64 *exit_code)
{
	if (kvm_hyp_handle_tlbi_el2(vcpu, exit_code))
		return true;

	if (kvm_hyp_handle_timer(vcpu, exit_code))
		return true;

	if (kvm_hyp_handle_cpacr_el1(vcpu, exit_code))
		return true;

	if (kvm_hyp_handle_zcr_el2(vcpu, exit_code))
		return true;

	return kvm_hyp_handle_sysreg(vcpu, exit_code);
}

static bool kvm_hyp_handle_impdef(struct kvm_vcpu *vcpu, u64 *exit_code)
{
	u64 iss;

	if (!cpus_have_final_cap(ARM64_WORKAROUND_PMUV3_IMPDEF_TRAPS))
		return false;

	/*
	 * Compute a synthetic ESR for a sysreg trap. Conveniently, AFSR1_EL2
	 * is populated with a correct ISS for a sysreg trap. These fruity
	 * parts are 64bit only, so unconditionally set IL.
	 */
	iss = ESR_ELx_ISS(read_sysreg_s(SYS_AFSR1_EL2));
	vcpu->arch.fault.esr_el2 = FIELD_PREP(ESR_ELx_EC_MASK, ESR_ELx_EC_SYS64) |
				   FIELD_PREP(ESR_ELx_ISS_MASK, iss) |
				   ESR_ELx_IL;
	return false;
}

static const exit_handler_fn hyp_exit_handlers[] = {
	[0 ... ESR_ELx_EC_MAX]		= NULL,
	[ESR_ELx_EC_CP15_32]		= kvm_hyp_handle_cp15_32,
	[ESR_ELx_EC_SYS64]		= kvm_hyp_handle_sysreg_vhe,
	[ESR_ELx_EC_SVE]		= kvm_hyp_handle_fpsimd,
	[ESR_ELx_EC_FP_ASIMD]		= kvm_hyp_handle_fpsimd,
	[ESR_ELx_EC_IABT_LOW]		= kvm_hyp_handle_iabt_low,
	[ESR_ELx_EC_DABT_LOW]		= kvm_hyp_handle_dabt_low,
	[ESR_ELx_EC_WATCHPT_LOW]	= kvm_hyp_handle_watchpt_low,
	[ESR_ELx_EC_ERET]		= kvm_hyp_handle_eret,
	[ESR_ELx_EC_MOPS]		= kvm_hyp_handle_mops,

	/* Apple shenanigans */
	[0x3F]				= kvm_hyp_handle_impdef,
};

static inline bool fixup_guest_exit(struct kvm_vcpu *vcpu, u64 *exit_code)
{
	synchronize_vcpu_pstate(vcpu, exit_code);

	/*
	 * If we were in HYP context on entry, adjust the PSTATE view
	 * so that the usual helpers work correctly.
	 */
	if (vcpu_has_nv(vcpu) && (read_sysreg(hcr_el2) & HCR_NV)) {
		u64 mode = *vcpu_cpsr(vcpu) & (PSR_MODE_MASK | PSR_MODE32_BIT);

		switch (mode) {
		case PSR_MODE_EL1t:
			mode = PSR_MODE_EL2t;
			break;
		case PSR_MODE_EL1h:
			mode = PSR_MODE_EL2h;
			break;
		}

		*vcpu_cpsr(vcpu) &= ~(PSR_MODE_MASK | PSR_MODE32_BIT);
		*vcpu_cpsr(vcpu) |= mode;
	}

	return __fixup_guest_exit(vcpu, exit_code, hyp_exit_handlers);
}

/* Switch to the guest for VHE systems running in EL2 */
static int __kvm_vcpu_run_vhe(struct kvm_vcpu *vcpu)
{
	struct kvm_cpu_context *host_ctxt;
	struct kvm_cpu_context *guest_ctxt;
	u64 exit_code;

	host_ctxt = host_data_ptr(host_ctxt);
	guest_ctxt = &vcpu->arch.ctxt;

	sysreg_save_host_state_vhe(host_ctxt);

	fpsimd_lazy_switch_to_guest(vcpu);

	/*
	 * Note that ARM erratum 1165522 requires us to configure both stage 1
	 * and stage 2 translation for the guest context before we clear
	 * HCR_EL2.TGE. The stage 1 and stage 2 guest context has already been
	 * loaded on the CPU in kvm_vcpu_load_vhe().
	 */
	__activate_traps(vcpu);

	__kvm_adjust_pc(vcpu);

	sysreg_restore_guest_state_vhe(guest_ctxt);
	__debug_switch_to_guest(vcpu);

	do {
		/* Jump in the fire! */
		exit_code = __guest_enter(vcpu);

		/* And we're baaack! */
	} while (fixup_guest_exit(vcpu, &exit_code));

	sysreg_save_guest_state_vhe(guest_ctxt);

	__deactivate_traps(vcpu);

	fpsimd_lazy_switch_to_host(vcpu);

	sysreg_restore_host_state_vhe(host_ctxt);

	if (guest_owns_fp_regs())
		__fpsimd_save_fpexc32(vcpu);

	__debug_switch_to_host(vcpu);

	return exit_code;
}
NOKPROBE_SYMBOL(__kvm_vcpu_run_vhe);

int __kvm_vcpu_run(struct kvm_vcpu *vcpu)
{
	int ret;

	local_daif_mask();

	/*
	 * Having IRQs masked via PMR when entering the guest means the GIC
	 * will not signal the CPU of interrupts of lower priority, and the
	 * only way to get out will be via guest exceptions.
	 * Naturally, we want to avoid this.
	 *
	 * local_daif_mask() already sets GIC_PRIO_PSR_I_SET, we just need a
	 * dsb to ensure the redistributor is forwards EL2 IRQs to the CPU.
	 */
	pmr_sync();

	ret = __kvm_vcpu_run_vhe(vcpu);

	/*
	 * local_daif_restore() takes care to properly restore PSTATE.DAIF
	 * and the GIC PMR if the host is using IRQ priorities.
	 */
	local_daif_restore(DAIF_PROCCTX_NOIRQ);

	/*
	 * When we exit from the guest we change a number of CPU configuration
	 * parameters, such as traps.  We rely on the isb() in kvm_call_hyp*()
	 * to make sure these changes take effect before running the host or
	 * additional guests.
	 */
	return ret;
}

static void __noreturn __hyp_call_panic(u64 spsr, u64 elr, u64 par)
{
	struct kvm_cpu_context *host_ctxt;
	struct kvm_vcpu *vcpu;

	host_ctxt = host_data_ptr(host_ctxt);
	vcpu = host_ctxt->__hyp_running_vcpu;

	__deactivate_traps(vcpu);
	sysreg_restore_host_state_vhe(host_ctxt);

	panic("HYP panic:\nPS:%08llx PC:%016llx ESR:%08llx\nFAR:%016llx HPFAR:%016llx PAR:%016llx\nVCPU:%p\n",
	      spsr, elr,
	      read_sysreg_el2(SYS_ESR), read_sysreg_el2(SYS_FAR),
	      read_sysreg(hpfar_el2), par, vcpu);
}
NOKPROBE_SYMBOL(__hyp_call_panic);

void __noreturn hyp_panic(void)
{
	u64 spsr = read_sysreg_el2(SYS_SPSR);
	u64 elr = read_sysreg_el2(SYS_ELR);
	u64 par = read_sysreg_par();

	__hyp_call_panic(spsr, elr, par);
}

asmlinkage void kvm_unexpected_el2_exception(void)
{
	__kvm_unexpected_el2_exception();
}
