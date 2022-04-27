// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 - Google Inc
 * Author: Andrew Scull <ascull@google.com>
 */

#include <hyp/adjust_pc.h>

#include <asm/pgtable-types.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_host.h>
#include <asm/kvm_hyp.h>
#include <asm/kvm_mmu.h>

#include <nvhe/mem_protect.h>
#include <nvhe/mm.h>
#include <nvhe/pkvm.h>
#include <nvhe/trap_handler.h>

#include <linux/irqchip/arm-gic-v3.h>

#include "../../sys_regs.h"

/*
 * Host FPSIMD state. Written to when the guest accesses its own FPSIMD state,
 * and read when the guest state is live and we need to switch back to the host.
 *
 * Only valid when (fp_state == FP_STATE_GUEST_OWNED) in the hyp vCPU structure.
 */
static DEFINE_PER_CPU(struct user_fpsimd_state, loaded_host_fpsimd_state);

DEFINE_PER_CPU(struct kvm_nvhe_init_params, kvm_init_params);

void __kvm_hyp_host_forward_smc(struct kvm_cpu_context *host_ctxt);

typedef void (*hyp_entry_exit_handler_fn)(struct pkvm_hyp_vcpu *);

static void handle_pvm_entry_wfx(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	if (vcpu_get_flag(hyp_vcpu->host_vcpu, INCREMENT_PC)) {
		vcpu_clear_flag(&hyp_vcpu->vcpu, PC_UPDATE_REQ);
		kvm_incr_pc(&hyp_vcpu->vcpu);
	}
}

static void handle_pvm_entry_sys64(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;

	/* Exceptions have priority on anything else */
	if (vcpu_get_flag(host_vcpu, PENDING_EXCEPTION)) {
		/* Exceptions caused by this should be undef exceptions. */
		u32 esr = (ESR_ELx_EC_UNKNOWN << ESR_ELx_EC_SHIFT);

		__vcpu_sys_reg(&hyp_vcpu->vcpu, ESR_EL1) = esr;
		kvm_pend_exception(&hyp_vcpu->vcpu, EXCEPT_AA64_EL1_SYNC);
		return;
	}

	if (vcpu_get_flag(host_vcpu, INCREMENT_PC)) {
		vcpu_clear_flag(&hyp_vcpu->vcpu, PC_UPDATE_REQ);
		kvm_incr_pc(&hyp_vcpu->vcpu);
	}

	if (!esr_sys64_to_params(hyp_vcpu->vcpu.arch.fault.esr_el2).is_write) {
		/* r0 as transfer register between the guest and the host. */
		u64 rt_val = READ_ONCE(host_vcpu->arch.ctxt.regs.regs[0]);
		int rt = kvm_vcpu_sys_get_rt(&hyp_vcpu->vcpu);

		vcpu_set_reg(&hyp_vcpu->vcpu, rt, rt_val);
	}
}

static void handle_pvm_entry_iabt(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	unsigned long cpsr = *vcpu_cpsr(&hyp_vcpu->vcpu);
	u32 esr = ESR_ELx_IL;

	if (!vcpu_get_flag(hyp_vcpu->host_vcpu, PENDING_EXCEPTION))
		return;

	/*
	 * If the host wants to inject an exception, get syndrom and
	 * fault address.
	 */
	if ((cpsr & PSR_MODE_MASK) == PSR_MODE_EL0t)
		esr |= (ESR_ELx_EC_IABT_LOW << ESR_ELx_EC_SHIFT);
	else
		esr |= (ESR_ELx_EC_IABT_CUR << ESR_ELx_EC_SHIFT);

	esr |= ESR_ELx_FSC_EXTABT;

	__vcpu_sys_reg(&hyp_vcpu->vcpu, ESR_EL1) = esr;
	__vcpu_sys_reg(&hyp_vcpu->vcpu, FAR_EL1) =
		kvm_vcpu_get_hfar(&hyp_vcpu->vcpu);

	/* Tell the run loop that we want to inject something */
	kvm_pend_exception(&hyp_vcpu->vcpu, EXCEPT_AA64_EL1_SYNC);
}

static void handle_pvm_entry_dabt(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;
	bool pc_update;

	/* Exceptions have priority over anything else */
	if (vcpu_get_flag(host_vcpu, PENDING_EXCEPTION)) {
		unsigned long cpsr = *vcpu_cpsr(&hyp_vcpu->vcpu);
		u32 esr = ESR_ELx_IL;

		if ((cpsr & PSR_MODE_MASK) == PSR_MODE_EL0t)
			esr |= (ESR_ELx_EC_DABT_LOW << ESR_ELx_EC_SHIFT);
		else
			esr |= (ESR_ELx_EC_DABT_CUR << ESR_ELx_EC_SHIFT);

		esr |= ESR_ELx_FSC_EXTABT;

		__vcpu_sys_reg(&hyp_vcpu->vcpu, ESR_EL1) = esr;
		__vcpu_sys_reg(&hyp_vcpu->vcpu, FAR_EL1) =
			kvm_vcpu_get_hfar(&hyp_vcpu->vcpu);

		/* Tell the run loop that we want to inject something */
		kvm_pend_exception(&hyp_vcpu->vcpu, EXCEPT_AA64_EL1_SYNC);

		/* Cancel potential in-flight MMIO */
		hyp_vcpu->vcpu.mmio_needed = false;
		return;
	}

	/* Handle PC increment on MMIO */
	pc_update = (hyp_vcpu->vcpu.mmio_needed &&
		     vcpu_get_flag(host_vcpu, INCREMENT_PC));
	if (pc_update) {
		vcpu_clear_flag(&hyp_vcpu->vcpu, PC_UPDATE_REQ);
		kvm_incr_pc(&hyp_vcpu->vcpu);
	}

	/* If we were doing an MMIO read access, update the register*/
	if (pc_update && !kvm_vcpu_dabt_iswrite(&hyp_vcpu->vcpu)) {
		/* r0 as transfer register between the guest and the host. */
		u64 rd_val = READ_ONCE(host_vcpu->arch.ctxt.regs.regs[0]);
		int rd = kvm_vcpu_dabt_get_rd(&hyp_vcpu->vcpu);

		vcpu_set_reg(&hyp_vcpu->vcpu, rd, rd_val);
	}

	hyp_vcpu->vcpu.mmio_needed = false;
}

static void handle_pvm_exit_wfx(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	WRITE_ONCE(hyp_vcpu->host_vcpu->arch.ctxt.regs.pstate,
		   hyp_vcpu->vcpu.arch.ctxt.regs.pstate & PSR_MODE_MASK);
	WRITE_ONCE(hyp_vcpu->host_vcpu->arch.fault.esr_el2,
		   hyp_vcpu->vcpu.arch.fault.esr_el2);
}

static void handle_pvm_exit_sys64(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;
	u32 esr_el2 = hyp_vcpu->vcpu.arch.fault.esr_el2;

	/* r0 as transfer register between the guest and the host. */
	WRITE_ONCE(host_vcpu->arch.fault.esr_el2,
		   esr_el2 & ~ESR_ELx_SYS64_ISS_RT_MASK);

	/* The mode is required for the host to emulate some sysregs */
	WRITE_ONCE(host_vcpu->arch.ctxt.regs.pstate,
		   hyp_vcpu->vcpu.arch.ctxt.regs.pstate & PSR_MODE_MASK);

	if (esr_sys64_to_params(esr_el2).is_write) {
		int rt = kvm_vcpu_sys_get_rt(&hyp_vcpu->vcpu);
		u64 rt_val = vcpu_get_reg(&hyp_vcpu->vcpu, rt);

		WRITE_ONCE(host_vcpu->arch.ctxt.regs.regs[0], rt_val);
	}
}

static void handle_pvm_exit_iabt(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	WRITE_ONCE(hyp_vcpu->host_vcpu->arch.fault.esr_el2,
		   hyp_vcpu->vcpu.arch.fault.esr_el2);
	WRITE_ONCE(hyp_vcpu->host_vcpu->arch.fault.hpfar_el2,
		   hyp_vcpu->vcpu.arch.fault.hpfar_el2);
}

static void handle_pvm_exit_dabt(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;

	/*
	 * For now, we treat all data aborts as MMIO since we have no knowledge
	 * of the memslot configuration at EL2.
	 */
	hyp_vcpu->vcpu.mmio_needed = true;

	if (hyp_vcpu->vcpu.mmio_needed) {
		/* r0 as transfer register between the guest and the host. */
		WRITE_ONCE(host_vcpu->arch.fault.esr_el2,
			   hyp_vcpu->vcpu.arch.fault.esr_el2 & ~ESR_ELx_SRT_MASK);

		if (kvm_vcpu_dabt_iswrite(&hyp_vcpu->vcpu)) {
			int rt = kvm_vcpu_dabt_get_rd(&hyp_vcpu->vcpu);
			u64 rt_val = vcpu_get_reg(&hyp_vcpu->vcpu, rt);

			WRITE_ONCE(host_vcpu->arch.ctxt.regs.regs[0], rt_val);
		}
	} else {
		WRITE_ONCE(host_vcpu->arch.fault.esr_el2,
			   hyp_vcpu->vcpu.arch.fault.esr_el2 & ~ESR_ELx_ISV);
	}

	WRITE_ONCE(host_vcpu->arch.ctxt.regs.pstate,
		   hyp_vcpu->vcpu.arch.ctxt.regs.pstate & PSR_MODE_MASK);
	WRITE_ONCE(host_vcpu->arch.fault.far_el2,
		   hyp_vcpu->vcpu.arch.fault.far_el2 & GENMASK(11, 0));
	WRITE_ONCE(host_vcpu->arch.fault.hpfar_el2,
		   hyp_vcpu->vcpu.arch.fault.hpfar_el2);
	WRITE_ONCE(__vcpu_sys_reg(host_vcpu, SCTLR_EL1),
		   __vcpu_sys_reg(&hyp_vcpu->vcpu, SCTLR_EL1) &
			(SCTLR_ELx_EE | SCTLR_EL1_E0E));
}

static void handle_vm_entry_generic(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	vcpu_copy_flag(&hyp_vcpu->vcpu, hyp_vcpu->host_vcpu, PC_UPDATE_REQ);
}

static void handle_vm_exit_generic(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	WRITE_ONCE(hyp_vcpu->host_vcpu->arch.fault.esr_el2,
		   hyp_vcpu->vcpu.arch.fault.esr_el2);
}

static void handle_vm_exit_abt(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;

	WRITE_ONCE(host_vcpu->arch.fault.esr_el2,
		   hyp_vcpu->vcpu.arch.fault.esr_el2);
	WRITE_ONCE(host_vcpu->arch.fault.far_el2,
		   hyp_vcpu->vcpu.arch.fault.far_el2);
	WRITE_ONCE(host_vcpu->arch.fault.hpfar_el2,
		   hyp_vcpu->vcpu.arch.fault.hpfar_el2);
	WRITE_ONCE(host_vcpu->arch.fault.disr_el1,
		   hyp_vcpu->vcpu.arch.fault.disr_el1);
}

static const hyp_entry_exit_handler_fn entry_hyp_pvm_handlers[] = {
	[0 ... ESR_ELx_EC_MAX]		= NULL,
	[ESR_ELx_EC_WFx]		= handle_pvm_entry_wfx,
	[ESR_ELx_EC_SYS64]		= handle_pvm_entry_sys64,
	[ESR_ELx_EC_IABT_LOW]		= handle_pvm_entry_iabt,
	[ESR_ELx_EC_DABT_LOW]		= handle_pvm_entry_dabt,
};

static const hyp_entry_exit_handler_fn exit_hyp_pvm_handlers[] = {
	[0 ... ESR_ELx_EC_MAX]		= NULL,
	[ESR_ELx_EC_WFx]		= handle_pvm_exit_wfx,
	[ESR_ELx_EC_SYS64]		= handle_pvm_exit_sys64,
	[ESR_ELx_EC_IABT_LOW]		= handle_pvm_exit_iabt,
	[ESR_ELx_EC_DABT_LOW]		= handle_pvm_exit_dabt,
};

static const hyp_entry_exit_handler_fn entry_hyp_vm_handlers[] = {
	[0 ... ESR_ELx_EC_MAX]		= handle_vm_entry_generic,
};

static const hyp_entry_exit_handler_fn exit_hyp_vm_handlers[] = {
	[0 ... ESR_ELx_EC_MAX]		= handle_vm_exit_generic,
	[ESR_ELx_EC_IABT_LOW]		= handle_vm_exit_abt,
	[ESR_ELx_EC_DABT_LOW]		= handle_vm_exit_abt,
};

static void flush_hyp_vgic_state(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;
	struct vgic_v3_cpu_if *host_cpu_if, *hyp_cpu_if;
	unsigned int used_lrs, max_lrs, i;

	host_cpu_if	= &host_vcpu->arch.vgic_cpu.vgic_v3;
	hyp_cpu_if	= &hyp_vcpu->vcpu.arch.vgic_cpu.vgic_v3;

	max_lrs		= (read_gicreg(ICH_VTR_EL2) & 0xf) + 1;
	used_lrs	= READ_ONCE(host_cpu_if->used_lrs);
	used_lrs	= min(used_lrs, max_lrs);

	hyp_cpu_if->vgic_hcr	= READ_ONCE(host_cpu_if->vgic_hcr);
	/* Should be a one-off */
	hyp_cpu_if->vgic_sre	= (ICC_SRE_EL1_DIB |
				   ICC_SRE_EL1_DFB |
				   ICC_SRE_EL1_SRE);
	hyp_cpu_if->used_lrs	= used_lrs;

	for (i = 0; i < used_lrs; i++)
		hyp_cpu_if->vgic_lr[i] = READ_ONCE(host_cpu_if->vgic_lr[i]);
}

static void sync_hyp_vgic_state(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;
	struct vgic_v3_cpu_if *host_cpu_if, *hyp_cpu_if;
	unsigned int i;

	host_cpu_if	= &host_vcpu->arch.vgic_cpu.vgic_v3;
	hyp_cpu_if	= &hyp_vcpu->vcpu.arch.vgic_cpu.vgic_v3;

	WRITE_ONCE(host_cpu_if->vgic_hcr, hyp_cpu_if->vgic_hcr);

	for (i = 0; i < hyp_cpu_if->used_lrs; i++)
		WRITE_ONCE(host_cpu_if->vgic_lr[i], hyp_cpu_if->vgic_lr[i]);
}

static void flush_hyp_timer_state(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	if (!pkvm_hyp_vcpu_is_protected(hyp_vcpu))
		return;

	/*
	 * A hyp vcpu has no offset, and sees vtime == ptime. The
	 * ptimer is fully emulated by EL1 and cannot be trusted.
	 */
	write_sysreg(0, cntvoff_el2);
	isb();
	write_sysreg_el0(__vcpu_sys_reg(&hyp_vcpu->vcpu, CNTV_CVAL_EL0),
			 SYS_CNTV_CVAL);
	write_sysreg_el0(__vcpu_sys_reg(&hyp_vcpu->vcpu, CNTV_CTL_EL0),
			 SYS_CNTV_CTL);
}

static void sync_hyp_timer_state(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	if (!pkvm_hyp_vcpu_is_protected(hyp_vcpu))
		return;

	/*
	 * Preserve the vtimer state so that it is always correct,
	 * even if the host tries to make a mess.
	 */
	__vcpu_sys_reg(&hyp_vcpu->vcpu, CNTV_CVAL_EL0) =
		read_sysreg_el0(SYS_CNTV_CVAL);
	__vcpu_sys_reg(&hyp_vcpu->vcpu, CNTV_CTL_EL0) =
		read_sysreg_el0(SYS_CNTV_CTL);
}

static void __copy_vcpu_state(const struct kvm_vcpu *from_vcpu,
			      struct kvm_vcpu *to_vcpu)
{
	int i;

	to_vcpu->arch.ctxt.regs		= from_vcpu->arch.ctxt.regs;
	to_vcpu->arch.ctxt.spsr_abt	= from_vcpu->arch.ctxt.spsr_abt;
	to_vcpu->arch.ctxt.spsr_und	= from_vcpu->arch.ctxt.spsr_und;
	to_vcpu->arch.ctxt.spsr_irq	= from_vcpu->arch.ctxt.spsr_irq;
	to_vcpu->arch.ctxt.spsr_fiq	= from_vcpu->arch.ctxt.spsr_fiq;

	/*
	 * Copy the sysregs, but don't mess with the timer state which
	 * is directly handled by EL1 and is expected to be preserved.
	 */
	for (i = 1; i < NR_SYS_REGS; i++) {
		if (i >= CNTVOFF_EL2 && i <= CNTP_CTL_EL0)
			continue;
		to_vcpu->arch.ctxt.sys_regs[i] = from_vcpu->arch.ctxt.sys_regs[i];
	}
}

static void __sync_hyp_vcpu(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	__copy_vcpu_state(&hyp_vcpu->vcpu, hyp_vcpu->host_vcpu);
}

static void __flush_hyp_vcpu(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	__copy_vcpu_state(hyp_vcpu->host_vcpu, &hyp_vcpu->vcpu);
}

static void flush_hyp_vcpu(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;
	hyp_entry_exit_handler_fn ec_handler;
	u8 esr_ec;

	/*
	 * If we deal with a non-protected guest and the state is potentially
	 * dirty (from a host perspective), copy the state back into the hyp
	 * vcpu.
	 */
	if (!pkvm_hyp_vcpu_is_protected(hyp_vcpu)) {
		if (vcpu_get_flag(host_vcpu, PKVM_HOST_STATE_DIRTY))
			__flush_hyp_vcpu(hyp_vcpu);

		hyp_vcpu->vcpu.arch.sve_state = kern_hyp_va(host_vcpu->arch.sve_state);
		hyp_vcpu->vcpu.arch.sve_max_vl = host_vcpu->arch.sve_max_vl;

		hyp_vcpu->vcpu.arch.hcr_el2 = HCR_GUEST_FLAGS & ~(HCR_RW | HCR_TWI | HCR_TWE);
		hyp_vcpu->vcpu.arch.hcr_el2 |= READ_ONCE(host_vcpu->arch.hcr_el2);

		hyp_vcpu->vcpu.arch.mdcr_el2 = host_vcpu->arch.mdcr_el2;
		hyp_vcpu->vcpu.arch.debug_ptr = kern_hyp_va(host_vcpu->arch.debug_ptr);
	}

	hyp_vcpu->vcpu.arch.vsesr_el2 = host_vcpu->arch.vsesr_el2;

	flush_hyp_vgic_state(hyp_vcpu);
	flush_hyp_timer_state(hyp_vcpu);

	switch (ARM_EXCEPTION_CODE(hyp_vcpu->exit_code)) {
	case ARM_EXCEPTION_IRQ:
	case ARM_EXCEPTION_EL1_SERROR:
	case ARM_EXCEPTION_IL:
		break;
	case ARM_EXCEPTION_TRAP:
		esr_ec = ESR_ELx_EC(kvm_vcpu_get_esr(&hyp_vcpu->vcpu));

		if (pkvm_hyp_vcpu_is_protected(hyp_vcpu))
			ec_handler = entry_hyp_pvm_handlers[esr_ec];
		else
			ec_handler = entry_hyp_vm_handlers[esr_ec];

		if (ec_handler)
			ec_handler(hyp_vcpu);
		break;
	default:
		BUG();
	}

	hyp_vcpu->exit_code = 0;
}

static void sync_hyp_vcpu(struct pkvm_hyp_vcpu *hyp_vcpu, u32 exit_reason)
{
	struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;
	hyp_entry_exit_handler_fn ec_handler;
	u8 esr_ec;

	/*
	 * Don't sync the vcpu GPR/sysreg state after a run. Instead,
	 * leave it in the hyp vCPU until someone actually requires it.
	 */
	sync_hyp_vgic_state(hyp_vcpu);
	sync_hyp_timer_state(hyp_vcpu);

	switch (ARM_EXCEPTION_CODE(exit_reason)) {
	case ARM_EXCEPTION_IRQ:
		break;
	case ARM_EXCEPTION_TRAP:
		esr_ec = ESR_ELx_EC(kvm_vcpu_get_esr(&hyp_vcpu->vcpu));

		if (pkvm_hyp_vcpu_is_protected(hyp_vcpu))
			ec_handler = exit_hyp_pvm_handlers[esr_ec];
		else
			ec_handler = exit_hyp_vm_handlers[esr_ec];

		if (ec_handler)
			ec_handler(hyp_vcpu);
		break;
	case ARM_EXCEPTION_EL1_SERROR:
	case ARM_EXCEPTION_IL:
		break;
	default:
		BUG();
	}

	vcpu_clear_flag(host_vcpu, PC_UPDATE_REQ);
	hyp_vcpu->exit_code = exit_reason;
}

static void __hyp_sve_save_guest(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *vcpu = &hyp_vcpu->vcpu;

	__sve_save_state(vcpu_sve_pffr(vcpu), &vcpu->arch.ctxt.fp_regs.fpsr);
	__vcpu_sys_reg(vcpu, ZCR_EL1) = read_sysreg_el1(SYS_ZCR);
	sve_cond_update_zcr_vq(vcpu_sve_max_vq(vcpu) - 1, SYS_ZCR_EL1);
}

static void fpsimd_host_restore(void)
{
	sysreg_clear_set(cptr_el2, CPTR_EL2_TZ | CPTR_EL2_TFP, 0);
	isb();

	if (unlikely(is_protected_kvm_enabled())) {
		struct pkvm_hyp_vcpu *hyp_vcpu = pkvm_get_loaded_hyp_vcpu();
		struct user_fpsimd_state *host_fpsimd_state;

		host_fpsimd_state = this_cpu_ptr(&loaded_host_fpsimd_state);

		if (vcpu_has_sve(&hyp_vcpu->vcpu))
			__hyp_sve_save_guest(hyp_vcpu);
		else
			__fpsimd_save_state(&hyp_vcpu->vcpu.arch.ctxt.fp_regs);

		__fpsimd_restore_state(host_fpsimd_state);

		hyp_vcpu->vcpu.arch.fp_state = FP_STATE_HOST_OWNED;
	}

	if (system_supports_sve())
		sve_cond_update_zcr_vq(ZCR_ELx_LEN_MASK, SYS_ZCR_EL2);
}

static void handle___pkvm_vcpu_load(struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(pkvm_handle_t, handle, host_ctxt, 1);
	DECLARE_REG(unsigned int, vcpu_idx, host_ctxt, 2);
	DECLARE_REG(u64, hcr_el2, host_ctxt, 3);
	struct pkvm_hyp_vcpu *hyp_vcpu;
	int *last_ran;

	if (!is_protected_kvm_enabled())
		return;

	hyp_vcpu = pkvm_load_hyp_vcpu(handle, vcpu_idx);
	if (!hyp_vcpu)
		return;

	/*
	 * Guarantee that both TLBs and I-cache are private to each vcpu. If a
	 * vcpu from the same VM has previously run on the same physical CPU,
	 * nuke the relevant contexts.
	 */
	last_ran = &hyp_vcpu->vcpu.arch.hw_mmu->last_vcpu_ran[hyp_smp_processor_id()];
	if (*last_ran != hyp_vcpu->vcpu.vcpu_id) {
		__kvm_flush_cpu_context(hyp_vcpu->vcpu.arch.hw_mmu);
		*last_ran = hyp_vcpu->vcpu.vcpu_id;
	}

	hyp_vcpu->vcpu.arch.host_fpsimd_state = this_cpu_ptr(&loaded_host_fpsimd_state);
	hyp_vcpu->vcpu.arch.fp_state = FP_STATE_HOST_OWNED;

	if (pkvm_hyp_vcpu_is_protected(hyp_vcpu)) {
		/* Propagate WFx trapping flags, trap ptrauth */
		hyp_vcpu->vcpu.arch.hcr_el2 &= ~(HCR_TWE | HCR_TWI |
						     HCR_API | HCR_APK);
		hyp_vcpu->vcpu.arch.hcr_el2 |= hcr_el2 & (HCR_TWE | HCR_TWI);
	}
}

static void handle___pkvm_vcpu_put(struct kvm_cpu_context *host_ctxt)
{
	struct pkvm_hyp_vcpu *hyp_vcpu;

	if (!is_protected_kvm_enabled())
		return;

	hyp_vcpu = pkvm_get_loaded_hyp_vcpu();
	if (hyp_vcpu) {
		struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;

		if (hyp_vcpu->vcpu.arch.fp_state == FP_STATE_GUEST_OWNED)
			fpsimd_host_restore();

		if (!pkvm_hyp_vcpu_is_protected(hyp_vcpu) &&
		    !vcpu_get_flag(host_vcpu, PKVM_HOST_STATE_DIRTY)) {
			__sync_hyp_vcpu(hyp_vcpu);
		}

		pkvm_put_hyp_vcpu(hyp_vcpu);
	}
}

static void handle___pkvm_vcpu_sync_state(struct kvm_cpu_context *host_ctxt)
{
	struct pkvm_hyp_vcpu *hyp_vcpu;

	if (!is_protected_kvm_enabled())
		return;

	hyp_vcpu = pkvm_get_loaded_hyp_vcpu();
	if (!hyp_vcpu || pkvm_hyp_vcpu_is_protected(hyp_vcpu))
		return;

	if (hyp_vcpu->vcpu.arch.fp_state == FP_STATE_GUEST_OWNED)
		fpsimd_host_restore();

	__sync_hyp_vcpu(hyp_vcpu);
}

static struct kvm_vcpu *__get_host_hyp_vcpus(struct kvm_vcpu *arg,
					     struct pkvm_hyp_vcpu **hyp_vcpup)
{
	struct kvm_vcpu *host_vcpu = kern_hyp_va(arg);
	struct pkvm_hyp_vcpu *hyp_vcpu = NULL;

	if (unlikely(is_protected_kvm_enabled())) {
		hyp_vcpu = pkvm_get_loaded_hyp_vcpu();

		if (!hyp_vcpu || hyp_vcpu->host_vcpu != host_vcpu) {
			hyp_vcpu = NULL;
			host_vcpu = NULL;
		}
	}

	*hyp_vcpup = hyp_vcpu;
	return host_vcpu;
}

#define get_host_hyp_vcpus(ctxt, regnr, hyp_vcpup)			\
	({								\
		DECLARE_REG(struct kvm_vcpu *, __vcpu, ctxt, regnr);	\
		__get_host_hyp_vcpus(__vcpu, hyp_vcpup);		\
	})

static void handle___kvm_vcpu_run(struct kvm_cpu_context *host_ctxt)
{
	struct pkvm_hyp_vcpu *hyp_vcpu;
	struct kvm_vcpu *host_vcpu;
	int ret;

	host_vcpu = get_host_hyp_vcpus(host_ctxt, 1, &hyp_vcpu);
	if (!host_vcpu) {
		ret = -EINVAL;
		goto out;
	}

	if (unlikely(hyp_vcpu)) {
		flush_hyp_vcpu(hyp_vcpu);

		ret = __kvm_vcpu_run(&hyp_vcpu->vcpu);

		sync_hyp_vcpu(hyp_vcpu, ret);

		if (hyp_vcpu->vcpu.arch.fp_state == FP_STATE_GUEST_OWNED) {
			/*
			 * The guest has used the FP, trap all accesses
			 * from the host (both FP and SVE).
			 */
			u64 reg = CPTR_EL2_TFP;

			if (system_supports_sve())
				reg |= CPTR_EL2_TZ;

			sysreg_clear_set(cptr_el2, 0, reg);
		}
	} else {
		/* The host is fully trusted, run its vCPU directly. */
		ret = __kvm_vcpu_run(host_vcpu);
	}
out:
	cpu_reg(host_ctxt, 1) =  ret;
}

static int pkvm_refill_memcache(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct pkvm_hyp_vm *hyp_vm = pkvm_hyp_vcpu_to_hyp_vm(hyp_vcpu);
	u64 nr_pages = VTCR_EL2_LVLS(hyp_vm->kvm.arch.vtcr) - 1;
	struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;

	return refill_memcache(&hyp_vcpu->vcpu.arch.pkvm_memcache, nr_pages,
			       &host_vcpu->arch.pkvm_memcache);
}

static void handle___pkvm_host_map_guest(struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(u64, pfn, host_ctxt, 1);
	DECLARE_REG(u64, gfn, host_ctxt, 2);
	struct pkvm_hyp_vcpu *hyp_vcpu;
	int ret = -EINVAL;

	if (!is_protected_kvm_enabled())
		goto out;

	hyp_vcpu = pkvm_get_loaded_hyp_vcpu();
	if (!hyp_vcpu)
		goto out;

	/* Top-up our per-vcpu memcache from the host's */
	ret = pkvm_refill_memcache(hyp_vcpu);
	if (ret)
		goto out;

	if (pkvm_hyp_vcpu_is_protected(hyp_vcpu))
		ret = __pkvm_host_donate_guest(pfn, gfn, hyp_vcpu);
	else
		ret = __pkvm_host_share_guest(pfn, gfn, hyp_vcpu);
out:
	cpu_reg(host_ctxt, 1) =  ret;
}

static void handle___kvm_adjust_pc(struct kvm_cpu_context *host_ctxt)
{
	struct pkvm_hyp_vcpu *hyp_vcpu;
	struct kvm_vcpu *host_vcpu;

	host_vcpu = get_host_hyp_vcpus(host_ctxt, 1, &hyp_vcpu);
	if (!host_vcpu)
		return;

	if (hyp_vcpu) {
		/* This only applies to non-protected VMs */
		if (pkvm_hyp_vcpu_is_protected(hyp_vcpu))
			return;

		__kvm_adjust_pc(&hyp_vcpu->vcpu);
	} else {
		__kvm_adjust_pc(host_vcpu);
	}
}

static void handle___kvm_flush_vm_context(struct kvm_cpu_context *host_ctxt)
{
	__kvm_flush_vm_context();
}

static void handle___kvm_tlb_flush_vmid_ipa(struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(struct kvm_s2_mmu *, mmu, host_ctxt, 1);
	DECLARE_REG(phys_addr_t, ipa, host_ctxt, 2);
	DECLARE_REG(int, level, host_ctxt, 3);

	__kvm_tlb_flush_vmid_ipa(kern_hyp_va(mmu), ipa, level);
}

static void handle___kvm_tlb_flush_vmid(struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(struct kvm_s2_mmu *, mmu, host_ctxt, 1);

	__kvm_tlb_flush_vmid(kern_hyp_va(mmu));
}

static void handle___kvm_flush_cpu_context(struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(struct kvm_s2_mmu *, mmu, host_ctxt, 1);

	__kvm_flush_cpu_context(kern_hyp_va(mmu));
}

static void handle___kvm_timer_set_cntvoff(struct kvm_cpu_context *host_ctxt)
{
	__kvm_timer_set_cntvoff(cpu_reg(host_ctxt, 1));
}

static void handle___kvm_enable_ssbs(struct kvm_cpu_context *host_ctxt)
{
	u64 tmp;

	tmp = read_sysreg_el2(SYS_SCTLR);
	tmp |= SCTLR_ELx_DSSBS;
	write_sysreg_el2(tmp, SYS_SCTLR);
}

static void handle___vgic_v3_get_gic_config(struct kvm_cpu_context *host_ctxt)
{
	cpu_reg(host_ctxt, 1) = __vgic_v3_get_gic_config();
}

static void handle___vgic_v3_init_lrs(struct kvm_cpu_context *host_ctxt)
{
	__vgic_v3_init_lrs();
}

static void handle___kvm_get_mdcr_el2(struct kvm_cpu_context *host_ctxt)
{
	cpu_reg(host_ctxt, 1) = __kvm_get_mdcr_el2();
}

static void handle___vgic_v3_save_vmcr_aprs(struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(struct vgic_v3_cpu_if *, cpu_if, host_ctxt, 1);

	__vgic_v3_save_vmcr_aprs(kern_hyp_va(cpu_if));
}

static void handle___vgic_v3_restore_vmcr_aprs(struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(struct vgic_v3_cpu_if *, cpu_if, host_ctxt, 1);

	__vgic_v3_restore_vmcr_aprs(kern_hyp_va(cpu_if));
}

static void handle___pkvm_init(struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(phys_addr_t, phys, host_ctxt, 1);
	DECLARE_REG(unsigned long, size, host_ctxt, 2);
	DECLARE_REG(unsigned long, nr_cpus, host_ctxt, 3);
	DECLARE_REG(unsigned long *, per_cpu_base, host_ctxt, 4);
	DECLARE_REG(u32, hyp_va_bits, host_ctxt, 5);

	/*
	 * __pkvm_init() will return only if an error occurred, otherwise it
	 * will tail-call in __pkvm_init_finalise() which will have to deal
	 * with the host context directly.
	 */
	cpu_reg(host_ctxt, 1) = __pkvm_init(phys, size, nr_cpus, per_cpu_base,
					    hyp_va_bits);
}

static void handle___pkvm_cpu_set_vector(struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(enum arm64_hyp_spectre_vector, slot, host_ctxt, 1);

	cpu_reg(host_ctxt, 1) = pkvm_cpu_set_vector(slot);
}

static void handle___pkvm_host_share_hyp(struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(u64, pfn, host_ctxt, 1);

	cpu_reg(host_ctxt, 1) = __pkvm_host_share_hyp(pfn);
}

static void handle___pkvm_host_unshare_hyp(struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(u64, pfn, host_ctxt, 1);

	cpu_reg(host_ctxt, 1) = __pkvm_host_unshare_hyp(pfn);
}

static void handle___pkvm_host_reclaim_page(struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(u64, pfn, host_ctxt, 1);

	cpu_reg(host_ctxt, 1) = __pkvm_host_reclaim_page(pfn);
}

static void handle___pkvm_create_private_mapping(struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(phys_addr_t, phys, host_ctxt, 1);
	DECLARE_REG(size_t, size, host_ctxt, 2);
	DECLARE_REG(enum kvm_pgtable_prot, prot, host_ctxt, 3);

	/*
	 * __pkvm_create_private_mapping() populates a pointer with the
	 * hypervisor start address of the allocation.
	 *
	 * However, handle___pkvm_create_private_mapping() hypercall crosses the
	 * EL1/EL2 boundary so the pointer would not be valid in this context.
	 *
	 * Instead pass the allocation address as the return value (or return
	 * ERR_PTR() on failure).
	 */
	unsigned long haddr;
	int err = __pkvm_create_private_mapping(phys, size, prot, &haddr);

	if (err)
		haddr = (unsigned long)ERR_PTR(err);

	cpu_reg(host_ctxt, 1) = haddr;
}

static void handle___pkvm_prot_finalize(struct kvm_cpu_context *host_ctxt)
{
	cpu_reg(host_ctxt, 1) = __pkvm_prot_finalize();
}

static void handle___pkvm_init_vm(struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(struct kvm *, host_kvm, host_ctxt, 1);
	DECLARE_REG(unsigned long, vm_hva, host_ctxt, 2);
	DECLARE_REG(unsigned long, pgd_hva, host_ctxt, 3);
	DECLARE_REG(unsigned long, last_ran_hva, host_ctxt, 4);

	host_kvm = kern_hyp_va(host_kvm);
	cpu_reg(host_ctxt, 1) = __pkvm_init_vm(host_kvm, vm_hva, pgd_hva,
					       last_ran_hva);
}

static void handle___pkvm_init_vcpu(struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(pkvm_handle_t, handle, host_ctxt, 1);
	DECLARE_REG(struct kvm_vcpu *, host_vcpu, host_ctxt, 2);
	DECLARE_REG(unsigned long, vcpu_hva, host_ctxt, 3);

	host_vcpu = kern_hyp_va(host_vcpu);
	cpu_reg(host_ctxt, 1) = __pkvm_init_vcpu(handle, host_vcpu, vcpu_hva);
}

static void handle___pkvm_teardown_vm(struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(pkvm_handle_t, handle, host_ctxt, 1);

	cpu_reg(host_ctxt, 1) = __pkvm_teardown_vm(handle);
}

typedef void (*hcall_t)(struct kvm_cpu_context *);

#define HANDLE_FUNC(x)	[__KVM_HOST_SMCCC_FUNC_##x] = (hcall_t)handle_##x

static const hcall_t host_hcall[] = {
	/* ___kvm_hyp_init */
	HANDLE_FUNC(__kvm_get_mdcr_el2),
	HANDLE_FUNC(__pkvm_init),
	HANDLE_FUNC(__pkvm_create_private_mapping),
	HANDLE_FUNC(__pkvm_cpu_set_vector),
	HANDLE_FUNC(__kvm_enable_ssbs),
	HANDLE_FUNC(__vgic_v3_init_lrs),
	HANDLE_FUNC(__vgic_v3_get_gic_config),
	HANDLE_FUNC(__pkvm_prot_finalize),

	HANDLE_FUNC(__pkvm_host_share_hyp),
	HANDLE_FUNC(__pkvm_host_unshare_hyp),
	HANDLE_FUNC(__pkvm_host_reclaim_page),
	HANDLE_FUNC(__pkvm_host_map_guest),
	HANDLE_FUNC(__kvm_adjust_pc),
	HANDLE_FUNC(__kvm_vcpu_run),
	HANDLE_FUNC(__kvm_flush_vm_context),
	HANDLE_FUNC(__kvm_tlb_flush_vmid_ipa),
	HANDLE_FUNC(__kvm_tlb_flush_vmid),
	HANDLE_FUNC(__kvm_flush_cpu_context),
	HANDLE_FUNC(__kvm_timer_set_cntvoff),
	HANDLE_FUNC(__vgic_v3_save_vmcr_aprs),
	HANDLE_FUNC(__vgic_v3_restore_vmcr_aprs),
	HANDLE_FUNC(__pkvm_init_vm),
	HANDLE_FUNC(__pkvm_init_vcpu),
	HANDLE_FUNC(__pkvm_teardown_vm),
	HANDLE_FUNC(__pkvm_vcpu_load),
	HANDLE_FUNC(__pkvm_vcpu_put),
	HANDLE_FUNC(__pkvm_vcpu_sync_state),
};

static void handle_host_hcall(struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(unsigned long, id, host_ctxt, 0);
	unsigned long hcall_min = 0;
	hcall_t hfn;

	/*
	 * If pKVM has been initialised then reject any calls to the
	 * early "privileged" hypercalls. Note that we cannot reject
	 * calls to __pkvm_prot_finalize for two reasons: (1) The static
	 * key used to determine initialisation must be toggled prior to
	 * finalisation and (2) finalisation is performed on a per-CPU
	 * basis. This is all fine, however, since __pkvm_prot_finalize
	 * returns -EPERM after the first call for a given CPU.
	 */
	if (static_branch_unlikely(&kvm_protected_mode_initialized))
		hcall_min = __KVM_HOST_SMCCC_FUNC___pkvm_prot_finalize;

	id -= KVM_HOST_SMCCC_ID(0);

	if (unlikely(id < hcall_min || id >= ARRAY_SIZE(host_hcall)))
		goto inval;

	hfn = host_hcall[id];
	if (unlikely(!hfn))
		goto inval;

	cpu_reg(host_ctxt, 0) = SMCCC_RET_SUCCESS;
	hfn(host_ctxt);

	return;
inval:
	cpu_reg(host_ctxt, 0) = SMCCC_RET_NOT_SUPPORTED;
}

static void default_host_smc_handler(struct kvm_cpu_context *host_ctxt)
{
	__kvm_hyp_host_forward_smc(host_ctxt);
}

static void handle_host_smc(struct kvm_cpu_context *host_ctxt)
{
	bool handled;

	handled = kvm_host_psci_handler(host_ctxt);
	if (!handled)
		default_host_smc_handler(host_ctxt);

	/* SMC was trapped, move ELR past the current PC. */
	kvm_skip_host_instr();
}

void handle_trap(struct kvm_cpu_context *host_ctxt)
{
	u64 esr = read_sysreg_el2(SYS_ESR);

	switch (ESR_ELx_EC(esr)) {
	case ESR_ELx_EC_HVC64:
		handle_host_hcall(host_ctxt);
		break;
	case ESR_ELx_EC_SMC64:
		handle_host_smc(host_ctxt);
		break;
	case ESR_ELx_EC_FP_ASIMD:
	case ESR_ELx_EC_SVE:
		fpsimd_host_restore();
		break;
	case ESR_ELx_EC_IABT_LOW:
	case ESR_ELx_EC_DABT_LOW:
		handle_host_mem_abort(host_ctxt);
		break;
	default:
		BUG();
	}
}
