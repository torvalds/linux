// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 - Google Inc
 * Author: Andrew Scull <ascull@google.com>
 */

#include <hyp/adjust_pc.h>
#include <hyp/switch.h>

#include <asm/pgtable-types.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_host.h>
#include <asm/kvm_hyp.h>
#include <asm/kvm_mmu.h>

#include <nvhe/ffa.h>
#include <nvhe/mem_protect.h>
#include <nvhe/mm.h>
#include <nvhe/pkvm.h>
#include <nvhe/trap_handler.h>

DEFINE_PER_CPU(struct kvm_nvhe_init_params, kvm_init_params);

void __kvm_hyp_host_forward_smc(struct kvm_cpu_context *host_ctxt);

static void __hyp_sve_save_guest(struct kvm_vcpu *vcpu)
{
	__vcpu_sys_reg(vcpu, ZCR_EL1) = read_sysreg_el1(SYS_ZCR);
	/*
	 * On saving/restoring guest sve state, always use the maximum VL for
	 * the guest. The layout of the data when saving the sve state depends
	 * on the VL, so use a consistent (i.e., the maximum) guest VL.
	 */
	sve_cond_update_zcr_vq(vcpu_sve_max_vq(vcpu) - 1, SYS_ZCR_EL2);
	__sve_save_state(vcpu_sve_pffr(vcpu), &vcpu->arch.ctxt.fp_regs.fpsr, true);
	write_sysreg_s(sve_vq_from_vl(kvm_host_sve_max_vl) - 1, SYS_ZCR_EL2);
}

static void __hyp_sve_restore_host(void)
{
	struct cpu_sve_state *sve_state = *host_data_ptr(sve_state);

	/*
	 * On saving/restoring host sve state, always use the maximum VL for
	 * the host. The layout of the data when saving the sve state depends
	 * on the VL, so use a consistent (i.e., the maximum) host VL.
	 *
	 * Note that this constrains the PE to the maximum shared VL
	 * that was discovered, if we wish to use larger VLs this will
	 * need to be revisited.
	 */
	write_sysreg_s(sve_vq_from_vl(kvm_host_sve_max_vl) - 1, SYS_ZCR_EL2);
	__sve_restore_state(sve_state->sve_regs + sve_ffr_offset(kvm_host_sve_max_vl),
			    &sve_state->fpsr,
			    true);
	write_sysreg_el1(sve_state->zcr_el1, SYS_ZCR);
}

static void fpsimd_sve_flush(void)
{
	*host_data_ptr(fp_owner) = FP_STATE_HOST_OWNED;
}

static void fpsimd_sve_sync(struct kvm_vcpu *vcpu)
{
	bool has_fpmr;

	if (!guest_owns_fp_regs())
		return;

	cpacr_clear_set(0, CPACR_EL1_FPEN | CPACR_EL1_ZEN);
	isb();

	if (vcpu_has_sve(vcpu))
		__hyp_sve_save_guest(vcpu);
	else
		__fpsimd_save_state(&vcpu->arch.ctxt.fp_regs);

	has_fpmr = kvm_has_fpmr(kern_hyp_va(vcpu->kvm));
	if (has_fpmr)
		__vcpu_sys_reg(vcpu, FPMR) = read_sysreg_s(SYS_FPMR);

	if (system_supports_sve())
		__hyp_sve_restore_host();
	else
		__fpsimd_restore_state(host_data_ptr(host_ctxt.fp_regs));

	if (has_fpmr)
		write_sysreg_s(*host_data_ptr(fpmr), SYS_FPMR);

	*host_data_ptr(fp_owner) = FP_STATE_HOST_OWNED;
}

static void flush_debug_state(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;

	hyp_vcpu->vcpu.arch.debug_owner = host_vcpu->arch.debug_owner;

	if (kvm_guest_owns_debug_regs(&hyp_vcpu->vcpu))
		hyp_vcpu->vcpu.arch.vcpu_debug_state = host_vcpu->arch.vcpu_debug_state;
	else if (kvm_host_owns_debug_regs(&hyp_vcpu->vcpu))
		hyp_vcpu->vcpu.arch.external_debug_state = host_vcpu->arch.external_debug_state;
}

static void sync_debug_state(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;

	if (kvm_guest_owns_debug_regs(&hyp_vcpu->vcpu))
		host_vcpu->arch.vcpu_debug_state = hyp_vcpu->vcpu.arch.vcpu_debug_state;
	else if (kvm_host_owns_debug_regs(&hyp_vcpu->vcpu))
		host_vcpu->arch.external_debug_state = hyp_vcpu->vcpu.arch.external_debug_state;
}

static void flush_hyp_vcpu(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;

	fpsimd_sve_flush();
	flush_debug_state(hyp_vcpu);

	hyp_vcpu->vcpu.arch.ctxt	= host_vcpu->arch.ctxt;

	hyp_vcpu->vcpu.arch.mdcr_el2	= host_vcpu->arch.mdcr_el2;
	hyp_vcpu->vcpu.arch.hcr_el2 &= ~(HCR_TWI | HCR_TWE);
	hyp_vcpu->vcpu.arch.hcr_el2 |= READ_ONCE(host_vcpu->arch.hcr_el2) &
						 (HCR_TWI | HCR_TWE);

	hyp_vcpu->vcpu.arch.iflags	= host_vcpu->arch.iflags;

	hyp_vcpu->vcpu.arch.vsesr_el2	= host_vcpu->arch.vsesr_el2;

	hyp_vcpu->vcpu.arch.vgic_cpu.vgic_v3 = host_vcpu->arch.vgic_cpu.vgic_v3;
}

static void sync_hyp_vcpu(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;
	struct vgic_v3_cpu_if *hyp_cpu_if = &hyp_vcpu->vcpu.arch.vgic_cpu.vgic_v3;
	struct vgic_v3_cpu_if *host_cpu_if = &host_vcpu->arch.vgic_cpu.vgic_v3;
	unsigned int i;

	fpsimd_sve_sync(&hyp_vcpu->vcpu);
	sync_debug_state(hyp_vcpu);

	host_vcpu->arch.ctxt		= hyp_vcpu->vcpu.arch.ctxt;

	host_vcpu->arch.hcr_el2		= hyp_vcpu->vcpu.arch.hcr_el2;

	host_vcpu->arch.fault		= hyp_vcpu->vcpu.arch.fault;

	host_vcpu->arch.iflags		= hyp_vcpu->vcpu.arch.iflags;

	host_cpu_if->vgic_hcr		= hyp_cpu_if->vgic_hcr;
	for (i = 0; i < hyp_cpu_if->used_lrs; ++i)
		host_cpu_if->vgic_lr[i] = hyp_cpu_if->vgic_lr[i];
}

static void handle___pkvm_vcpu_load(struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(pkvm_handle_t, handle, host_ctxt, 1);
	DECLARE_REG(unsigned int, vcpu_idx, host_ctxt, 2);
	DECLARE_REG(u64, hcr_el2, host_ctxt, 3);
	struct pkvm_hyp_vcpu *hyp_vcpu;

	if (!is_protected_kvm_enabled())
		return;

	hyp_vcpu = pkvm_load_hyp_vcpu(handle, vcpu_idx);
	if (!hyp_vcpu)
		return;

	if (pkvm_hyp_vcpu_is_protected(hyp_vcpu)) {
		/* Propagate WFx trapping flags */
		hyp_vcpu->vcpu.arch.hcr_el2 &= ~(HCR_TWE | HCR_TWI);
		hyp_vcpu->vcpu.arch.hcr_el2 |= hcr_el2 & (HCR_TWE | HCR_TWI);
	}
}

static void handle___pkvm_vcpu_put(struct kvm_cpu_context *host_ctxt)
{
	struct pkvm_hyp_vcpu *hyp_vcpu;

	if (!is_protected_kvm_enabled())
		return;

	hyp_vcpu = pkvm_get_loaded_hyp_vcpu();
	if (hyp_vcpu)
		pkvm_put_hyp_vcpu(hyp_vcpu);
}

static void handle___kvm_vcpu_run(struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(struct kvm_vcpu *, host_vcpu, host_ctxt, 1);
	int ret;

	if (unlikely(is_protected_kvm_enabled())) {
		struct pkvm_hyp_vcpu *hyp_vcpu = pkvm_get_loaded_hyp_vcpu();

		/*
		 * KVM (and pKVM) doesn't support SME guests for now, and
		 * ensures that SME features aren't enabled in pstate when
		 * loading a vcpu. Therefore, if SME features enabled the host
		 * is misbehaving.
		 */
		if (unlikely(system_supports_sme() && read_sysreg_s(SYS_SVCR))) {
			ret = -EINVAL;
			goto out;
		}

		if (!hyp_vcpu) {
			ret = -EINVAL;
			goto out;
		}

		flush_hyp_vcpu(hyp_vcpu);

		ret = __kvm_vcpu_run(&hyp_vcpu->vcpu);

		sync_hyp_vcpu(hyp_vcpu);
	} else {
		struct kvm_vcpu *vcpu = kern_hyp_va(host_vcpu);

		/* The host is fully trusted, run its vCPU directly. */
		fpsimd_lazy_switch_to_guest(vcpu);
		ret = __kvm_vcpu_run(vcpu);
		fpsimd_lazy_switch_to_host(vcpu);
	}
out:
	cpu_reg(host_ctxt, 1) =  ret;
}

static int pkvm_refill_memcache(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;

	return refill_memcache(&hyp_vcpu->vcpu.arch.pkvm_memcache,
			       host_vcpu->arch.pkvm_memcache.nr_pages,
			       &host_vcpu->arch.pkvm_memcache);
}

static void handle___pkvm_host_share_guest(struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(u64, pfn, host_ctxt, 1);
	DECLARE_REG(u64, gfn, host_ctxt, 2);
	DECLARE_REG(enum kvm_pgtable_prot, prot, host_ctxt, 3);
	struct pkvm_hyp_vcpu *hyp_vcpu;
	int ret = -EINVAL;

	if (!is_protected_kvm_enabled())
		goto out;

	hyp_vcpu = pkvm_get_loaded_hyp_vcpu();
	if (!hyp_vcpu || pkvm_hyp_vcpu_is_protected(hyp_vcpu))
		goto out;

	ret = pkvm_refill_memcache(hyp_vcpu);
	if (ret)
		goto out;

	ret = __pkvm_host_share_guest(pfn, gfn, hyp_vcpu, prot);
out:
	cpu_reg(host_ctxt, 1) =  ret;
}

static void handle___pkvm_host_unshare_guest(struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(pkvm_handle_t, handle, host_ctxt, 1);
	DECLARE_REG(u64, gfn, host_ctxt, 2);
	struct pkvm_hyp_vm *hyp_vm;
	int ret = -EINVAL;

	if (!is_protected_kvm_enabled())
		goto out;

	hyp_vm = get_np_pkvm_hyp_vm(handle);
	if (!hyp_vm)
		goto out;

	ret = __pkvm_host_unshare_guest(gfn, hyp_vm);
	put_pkvm_hyp_vm(hyp_vm);
out:
	cpu_reg(host_ctxt, 1) =  ret;
}

static void handle___pkvm_host_relax_perms_guest(struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(u64, gfn, host_ctxt, 1);
	DECLARE_REG(enum kvm_pgtable_prot, prot, host_ctxt, 2);
	struct pkvm_hyp_vcpu *hyp_vcpu;
	int ret = -EINVAL;

	if (!is_protected_kvm_enabled())
		goto out;

	hyp_vcpu = pkvm_get_loaded_hyp_vcpu();
	if (!hyp_vcpu || pkvm_hyp_vcpu_is_protected(hyp_vcpu))
		goto out;

	ret = __pkvm_host_relax_perms_guest(gfn, hyp_vcpu, prot);
out:
	cpu_reg(host_ctxt, 1) = ret;
}

static void handle___pkvm_host_wrprotect_guest(struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(pkvm_handle_t, handle, host_ctxt, 1);
	DECLARE_REG(u64, gfn, host_ctxt, 2);
	struct pkvm_hyp_vm *hyp_vm;
	int ret = -EINVAL;

	if (!is_protected_kvm_enabled())
		goto out;

	hyp_vm = get_np_pkvm_hyp_vm(handle);
	if (!hyp_vm)
		goto out;

	ret = __pkvm_host_wrprotect_guest(gfn, hyp_vm);
	put_pkvm_hyp_vm(hyp_vm);
out:
	cpu_reg(host_ctxt, 1) = ret;
}

static void handle___pkvm_host_test_clear_young_guest(struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(pkvm_handle_t, handle, host_ctxt, 1);
	DECLARE_REG(u64, gfn, host_ctxt, 2);
	DECLARE_REG(bool, mkold, host_ctxt, 3);
	struct pkvm_hyp_vm *hyp_vm;
	int ret = -EINVAL;

	if (!is_protected_kvm_enabled())
		goto out;

	hyp_vm = get_np_pkvm_hyp_vm(handle);
	if (!hyp_vm)
		goto out;

	ret = __pkvm_host_test_clear_young_guest(gfn, mkold, hyp_vm);
	put_pkvm_hyp_vm(hyp_vm);
out:
	cpu_reg(host_ctxt, 1) = ret;
}

static void handle___pkvm_host_mkyoung_guest(struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(u64, gfn, host_ctxt, 1);
	struct pkvm_hyp_vcpu *hyp_vcpu;
	int ret = -EINVAL;

	if (!is_protected_kvm_enabled())
		goto out;

	hyp_vcpu = pkvm_get_loaded_hyp_vcpu();
	if (!hyp_vcpu || pkvm_hyp_vcpu_is_protected(hyp_vcpu))
		goto out;

	ret = __pkvm_host_mkyoung_guest(gfn, hyp_vcpu);
out:
	cpu_reg(host_ctxt, 1) =  ret;
}

static void handle___kvm_adjust_pc(struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(struct kvm_vcpu *, vcpu, host_ctxt, 1);

	__kvm_adjust_pc(kern_hyp_va(vcpu));
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

static void handle___kvm_tlb_flush_vmid_ipa_nsh(struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(struct kvm_s2_mmu *, mmu, host_ctxt, 1);
	DECLARE_REG(phys_addr_t, ipa, host_ctxt, 2);
	DECLARE_REG(int, level, host_ctxt, 3);

	__kvm_tlb_flush_vmid_ipa_nsh(kern_hyp_va(mmu), ipa, level);
}

static void
handle___kvm_tlb_flush_vmid_range(struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(struct kvm_s2_mmu *, mmu, host_ctxt, 1);
	DECLARE_REG(phys_addr_t, start, host_ctxt, 2);
	DECLARE_REG(unsigned long, pages, host_ctxt, 3);

	__kvm_tlb_flush_vmid_range(kern_hyp_va(mmu), start, pages);
}

static void handle___kvm_tlb_flush_vmid(struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(struct kvm_s2_mmu *, mmu, host_ctxt, 1);

	__kvm_tlb_flush_vmid(kern_hyp_va(mmu));
}

static void handle___pkvm_tlb_flush_vmid(struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(pkvm_handle_t, handle, host_ctxt, 1);
	struct pkvm_hyp_vm *hyp_vm;

	if (!is_protected_kvm_enabled())
		return;

	hyp_vm = get_np_pkvm_hyp_vm(handle);
	if (!hyp_vm)
		return;

	__kvm_tlb_flush_vmid(&hyp_vm->kvm.arch.mmu);
	put_pkvm_hyp_vm(hyp_vm);
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

	host_kvm = kern_hyp_va(host_kvm);
	cpu_reg(host_ctxt, 1) = __pkvm_init_vm(host_kvm, vm_hva, pgd_hva);
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
	HANDLE_FUNC(__pkvm_init),
	HANDLE_FUNC(__pkvm_create_private_mapping),
	HANDLE_FUNC(__pkvm_cpu_set_vector),
	HANDLE_FUNC(__kvm_enable_ssbs),
	HANDLE_FUNC(__vgic_v3_init_lrs),
	HANDLE_FUNC(__vgic_v3_get_gic_config),
	HANDLE_FUNC(__pkvm_prot_finalize),

	HANDLE_FUNC(__pkvm_host_share_hyp),
	HANDLE_FUNC(__pkvm_host_unshare_hyp),
	HANDLE_FUNC(__pkvm_host_share_guest),
	HANDLE_FUNC(__pkvm_host_unshare_guest),
	HANDLE_FUNC(__pkvm_host_relax_perms_guest),
	HANDLE_FUNC(__pkvm_host_wrprotect_guest),
	HANDLE_FUNC(__pkvm_host_test_clear_young_guest),
	HANDLE_FUNC(__pkvm_host_mkyoung_guest),
	HANDLE_FUNC(__kvm_adjust_pc),
	HANDLE_FUNC(__kvm_vcpu_run),
	HANDLE_FUNC(__kvm_flush_vm_context),
	HANDLE_FUNC(__kvm_tlb_flush_vmid_ipa),
	HANDLE_FUNC(__kvm_tlb_flush_vmid_ipa_nsh),
	HANDLE_FUNC(__kvm_tlb_flush_vmid),
	HANDLE_FUNC(__kvm_tlb_flush_vmid_range),
	HANDLE_FUNC(__kvm_flush_cpu_context),
	HANDLE_FUNC(__kvm_timer_set_cntvoff),
	HANDLE_FUNC(__vgic_v3_save_vmcr_aprs),
	HANDLE_FUNC(__vgic_v3_restore_vmcr_aprs),
	HANDLE_FUNC(__pkvm_init_vm),
	HANDLE_FUNC(__pkvm_init_vcpu),
	HANDLE_FUNC(__pkvm_teardown_vm),
	HANDLE_FUNC(__pkvm_vcpu_load),
	HANDLE_FUNC(__pkvm_vcpu_put),
	HANDLE_FUNC(__pkvm_tlb_flush_vmid),
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

	id &= ~ARM_SMCCC_CALL_HINTS;
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
	DECLARE_REG(u64, func_id, host_ctxt, 0);
	bool handled;

	func_id &= ~ARM_SMCCC_CALL_HINTS;

	handled = kvm_host_psci_handler(host_ctxt, func_id);
	if (!handled)
		handled = kvm_host_ffa_handler(host_ctxt, func_id);
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
	case ESR_ELx_EC_IABT_LOW:
	case ESR_ELx_EC_DABT_LOW:
		handle_host_mem_abort(host_ctxt);
		break;
	default:
		BUG();
	}
}
