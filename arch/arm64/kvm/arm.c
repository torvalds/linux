// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Author: Christoffer Dall <c.dall@virtualopensystems.com>
 */

#include <linux/bug.h>
#include <linux/cpu_pm.h>
#include <linux/entry-kvm.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/kvm_host.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/mman.h>
#include <linux/sched.h>
#include <linux/kvm.h>
#include <linux/kvm_irqfd.h>
#include <linux/irqbypass.h>
#include <linux/sched/stat.h>
#include <linux/psci.h>
#include <trace/events/kvm.h>

#define CREATE_TRACE_POINTS
#include "trace_arm.h"

#include <linux/uaccess.h>
#include <asm/ptrace.h>
#include <asm/mman.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>
#include <asm/cpufeature.h>
#include <asm/virt.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_mmu.h>
#include <asm/kvm_nested.h>
#include <asm/kvm_pkvm.h>
#include <asm/kvm_ptrauth.h>
#include <asm/sections.h>

#include <kvm/arm_hypercalls.h>
#include <kvm/arm_pmu.h>
#include <kvm/arm_psci.h>

#include "sys_regs.h"

static enum kvm_mode kvm_mode = KVM_MODE_DEFAULT;

enum kvm_wfx_trap_policy {
	KVM_WFX_NOTRAP_SINGLE_TASK, /* Default option */
	KVM_WFX_NOTRAP,
	KVM_WFX_TRAP,
};

static enum kvm_wfx_trap_policy kvm_wfi_trap_policy __read_mostly = KVM_WFX_NOTRAP_SINGLE_TASK;
static enum kvm_wfx_trap_policy kvm_wfe_trap_policy __read_mostly = KVM_WFX_NOTRAP_SINGLE_TASK;

DECLARE_KVM_HYP_PER_CPU(unsigned long, kvm_hyp_vector);

DEFINE_PER_CPU(unsigned long, kvm_arm_hyp_stack_base);
DECLARE_KVM_NVHE_PER_CPU(struct kvm_nvhe_init_params, kvm_init_params);

DECLARE_KVM_NVHE_PER_CPU(struct kvm_cpu_context, kvm_hyp_ctxt);

static bool vgic_present, kvm_arm_initialised;

static DEFINE_PER_CPU(unsigned char, kvm_hyp_initialized);

bool is_kvm_arm_initialised(void)
{
	return kvm_arm_initialised;
}

int kvm_arch_vcpu_should_kick(struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_exiting_guest_mode(vcpu) == IN_GUEST_MODE;
}

int kvm_vm_ioctl_enable_cap(struct kvm *kvm,
			    struct kvm_enable_cap *cap)
{
	int r = -EINVAL;

	if (cap->flags)
		return -EINVAL;

	if (kvm_vm_is_protected(kvm) && !kvm_pvm_ext_allowed(cap->cap))
		return -EINVAL;

	switch (cap->cap) {
	case KVM_CAP_ARM_NISV_TO_USER:
		r = 0;
		set_bit(KVM_ARCH_FLAG_RETURN_NISV_IO_ABORT_TO_USER,
			&kvm->arch.flags);
		break;
	case KVM_CAP_ARM_MTE:
		mutex_lock(&kvm->lock);
		if (system_supports_mte() && !kvm->created_vcpus) {
			r = 0;
			set_bit(KVM_ARCH_FLAG_MTE_ENABLED, &kvm->arch.flags);
		}
		mutex_unlock(&kvm->lock);
		break;
	case KVM_CAP_ARM_SYSTEM_SUSPEND:
		r = 0;
		set_bit(KVM_ARCH_FLAG_SYSTEM_SUSPEND_ENABLED, &kvm->arch.flags);
		break;
	case KVM_CAP_ARM_EAGER_SPLIT_CHUNK_SIZE:
		mutex_lock(&kvm->slots_lock);
		/*
		 * To keep things simple, allow changing the chunk
		 * size only when no memory slots have been created.
		 */
		if (kvm_are_all_memslots_empty(kvm)) {
			u64 new_cap = cap->args[0];

			if (!new_cap || kvm_is_block_size_supported(new_cap)) {
				r = 0;
				kvm->arch.mmu.split_page_chunk_size = new_cap;
			}
		}
		mutex_unlock(&kvm->slots_lock);
		break;
	case KVM_CAP_ARM_WRITABLE_IMP_ID_REGS:
		mutex_lock(&kvm->lock);
		if (!kvm->created_vcpus) {
			r = 0;
			set_bit(KVM_ARCH_FLAG_WRITABLE_IMP_ID_REGS, &kvm->arch.flags);
		}
		mutex_unlock(&kvm->lock);
		break;
	default:
		break;
	}

	return r;
}

static int kvm_arm_default_max_vcpus(void)
{
	return vgic_present ? kvm_vgic_get_max_vcpus() : KVM_MAX_VCPUS;
}

/**
 * kvm_arch_init_vm - initializes a VM data structure
 * @kvm:	pointer to the KVM struct
 * @type:	kvm device type
 */
int kvm_arch_init_vm(struct kvm *kvm, unsigned long type)
{
	int ret;

	mutex_init(&kvm->arch.config_lock);

#ifdef CONFIG_LOCKDEP
	/* Clue in lockdep that the config_lock must be taken inside kvm->lock */
	mutex_lock(&kvm->lock);
	mutex_lock(&kvm->arch.config_lock);
	mutex_unlock(&kvm->arch.config_lock);
	mutex_unlock(&kvm->lock);
#endif

	kvm_init_nested(kvm);

	ret = kvm_share_hyp(kvm, kvm + 1);
	if (ret)
		return ret;

	ret = pkvm_init_host_vm(kvm);
	if (ret)
		goto err_unshare_kvm;

	if (!zalloc_cpumask_var(&kvm->arch.supported_cpus, GFP_KERNEL_ACCOUNT)) {
		ret = -ENOMEM;
		goto err_unshare_kvm;
	}
	cpumask_copy(kvm->arch.supported_cpus, cpu_possible_mask);

	ret = kvm_init_stage2_mmu(kvm, &kvm->arch.mmu, type);
	if (ret)
		goto err_free_cpumask;

	kvm_vgic_early_init(kvm);

	kvm_timer_init_vm(kvm);

	/* The maximum number of VCPUs is limited by the host's GIC model */
	kvm->max_vcpus = kvm_arm_default_max_vcpus();

	kvm_arm_init_hypercalls(kvm);

	bitmap_zero(kvm->arch.vcpu_features, KVM_VCPU_MAX_FEATURES);

	return 0;

err_free_cpumask:
	free_cpumask_var(kvm->arch.supported_cpus);
err_unshare_kvm:
	kvm_unshare_hyp(kvm, kvm + 1);
	return ret;
}

vm_fault_t kvm_arch_vcpu_fault(struct kvm_vcpu *vcpu, struct vm_fault *vmf)
{
	return VM_FAULT_SIGBUS;
}

void kvm_arch_create_vm_debugfs(struct kvm *kvm)
{
	kvm_sys_regs_create_debugfs(kvm);
	kvm_s2_ptdump_create_debugfs(kvm);
}

static void kvm_destroy_mpidr_data(struct kvm *kvm)
{
	struct kvm_mpidr_data *data;

	mutex_lock(&kvm->arch.config_lock);

	data = rcu_dereference_protected(kvm->arch.mpidr_data,
					 lockdep_is_held(&kvm->arch.config_lock));
	if (data) {
		rcu_assign_pointer(kvm->arch.mpidr_data, NULL);
		synchronize_rcu();
		kfree(data);
	}

	mutex_unlock(&kvm->arch.config_lock);
}

/**
 * kvm_arch_destroy_vm - destroy the VM data structure
 * @kvm:	pointer to the KVM struct
 */
void kvm_arch_destroy_vm(struct kvm *kvm)
{
	bitmap_free(kvm->arch.pmu_filter);
	free_cpumask_var(kvm->arch.supported_cpus);

	kvm_vgic_destroy(kvm);

	if (is_protected_kvm_enabled())
		pkvm_destroy_hyp_vm(kvm);

	kvm_destroy_mpidr_data(kvm);

	kfree(kvm->arch.sysreg_masks);
	kvm_destroy_vcpus(kvm);

	kvm_unshare_hyp(kvm, kvm + 1);

	kvm_arm_teardown_hypercalls(kvm);
}

static bool kvm_has_full_ptr_auth(void)
{
	bool apa, gpa, api, gpi, apa3, gpa3;
	u64 isar1, isar2, val;

	/*
	 * Check that:
	 *
	 * - both Address and Generic auth are implemented for a given
         *   algorithm (Q5, IMPDEF or Q3)
	 * - only a single algorithm is implemented.
	 */
	if (!system_has_full_ptr_auth())
		return false;

	isar1 = read_sanitised_ftr_reg(SYS_ID_AA64ISAR1_EL1);
	isar2 = read_sanitised_ftr_reg(SYS_ID_AA64ISAR2_EL1);

	apa = !!FIELD_GET(ID_AA64ISAR1_EL1_APA_MASK, isar1);
	val = FIELD_GET(ID_AA64ISAR1_EL1_GPA_MASK, isar1);
	gpa = (val == ID_AA64ISAR1_EL1_GPA_IMP);

	api = !!FIELD_GET(ID_AA64ISAR1_EL1_API_MASK, isar1);
	val = FIELD_GET(ID_AA64ISAR1_EL1_GPI_MASK, isar1);
	gpi = (val == ID_AA64ISAR1_EL1_GPI_IMP);

	apa3 = !!FIELD_GET(ID_AA64ISAR2_EL1_APA3_MASK, isar2);
	val  = FIELD_GET(ID_AA64ISAR2_EL1_GPA3_MASK, isar2);
	gpa3 = (val == ID_AA64ISAR2_EL1_GPA3_IMP);

	return (apa == gpa && api == gpi && apa3 == gpa3 &&
		(apa + api + apa3) == 1);
}

int kvm_vm_ioctl_check_extension(struct kvm *kvm, long ext)
{
	int r;

	if (kvm && kvm_vm_is_protected(kvm) && !kvm_pvm_ext_allowed(ext))
		return 0;

	switch (ext) {
	case KVM_CAP_IRQCHIP:
		r = vgic_present;
		break;
	case KVM_CAP_IOEVENTFD:
	case KVM_CAP_USER_MEMORY:
	case KVM_CAP_SYNC_MMU:
	case KVM_CAP_DESTROY_MEMORY_REGION_WORKS:
	case KVM_CAP_ONE_REG:
	case KVM_CAP_ARM_PSCI:
	case KVM_CAP_ARM_PSCI_0_2:
	case KVM_CAP_READONLY_MEM:
	case KVM_CAP_MP_STATE:
	case KVM_CAP_IMMEDIATE_EXIT:
	case KVM_CAP_VCPU_EVENTS:
	case KVM_CAP_ARM_IRQ_LINE_LAYOUT_2:
	case KVM_CAP_ARM_NISV_TO_USER:
	case KVM_CAP_ARM_INJECT_EXT_DABT:
	case KVM_CAP_SET_GUEST_DEBUG:
	case KVM_CAP_VCPU_ATTRIBUTES:
	case KVM_CAP_PTP_KVM:
	case KVM_CAP_ARM_SYSTEM_SUSPEND:
	case KVM_CAP_IRQFD_RESAMPLE:
	case KVM_CAP_COUNTER_OFFSET:
	case KVM_CAP_ARM_WRITABLE_IMP_ID_REGS:
		r = 1;
		break;
	case KVM_CAP_SET_GUEST_DEBUG2:
		return KVM_GUESTDBG_VALID_MASK;
	case KVM_CAP_ARM_SET_DEVICE_ADDR:
		r = 1;
		break;
	case KVM_CAP_NR_VCPUS:
		/*
		 * ARM64 treats KVM_CAP_NR_CPUS differently from all other
		 * architectures, as it does not always bound it to
		 * KVM_CAP_MAX_VCPUS. It should not matter much because
		 * this is just an advisory value.
		 */
		r = min_t(unsigned int, num_online_cpus(),
			  kvm_arm_default_max_vcpus());
		break;
	case KVM_CAP_MAX_VCPUS:
	case KVM_CAP_MAX_VCPU_ID:
		if (kvm)
			r = kvm->max_vcpus;
		else
			r = kvm_arm_default_max_vcpus();
		break;
	case KVM_CAP_MSI_DEVID:
		if (!kvm)
			r = -EINVAL;
		else
			r = kvm->arch.vgic.msis_require_devid;
		break;
	case KVM_CAP_ARM_USER_IRQ:
		/*
		 * 1: EL1_VTIMER, EL1_PTIMER, and PMU.
		 * (bump this number if adding more devices)
		 */
		r = 1;
		break;
	case KVM_CAP_ARM_MTE:
		r = system_supports_mte();
		break;
	case KVM_CAP_STEAL_TIME:
		r = kvm_arm_pvtime_supported();
		break;
	case KVM_CAP_ARM_EL1_32BIT:
		r = cpus_have_final_cap(ARM64_HAS_32BIT_EL1);
		break;
	case KVM_CAP_ARM_EL2:
		r = cpus_have_final_cap(ARM64_HAS_NESTED_VIRT);
		break;
	case KVM_CAP_ARM_EL2_E2H0:
		r = cpus_have_final_cap(ARM64_HAS_HCR_NV1);
		break;
	case KVM_CAP_GUEST_DEBUG_HW_BPS:
		r = get_num_brps();
		break;
	case KVM_CAP_GUEST_DEBUG_HW_WPS:
		r = get_num_wrps();
		break;
	case KVM_CAP_ARM_PMU_V3:
		r = kvm_supports_guest_pmuv3();
		break;
	case KVM_CAP_ARM_INJECT_SERROR_ESR:
		r = cpus_have_final_cap(ARM64_HAS_RAS_EXTN);
		break;
	case KVM_CAP_ARM_VM_IPA_SIZE:
		r = get_kvm_ipa_limit();
		break;
	case KVM_CAP_ARM_SVE:
		r = system_supports_sve();
		break;
	case KVM_CAP_ARM_PTRAUTH_ADDRESS:
	case KVM_CAP_ARM_PTRAUTH_GENERIC:
		r = kvm_has_full_ptr_auth();
		break;
	case KVM_CAP_ARM_EAGER_SPLIT_CHUNK_SIZE:
		if (kvm)
			r = kvm->arch.mmu.split_page_chunk_size;
		else
			r = KVM_ARM_EAGER_SPLIT_CHUNK_SIZE_DEFAULT;
		break;
	case KVM_CAP_ARM_SUPPORTED_BLOCK_SIZES:
		r = kvm_supported_block_sizes();
		break;
	case KVM_CAP_ARM_SUPPORTED_REG_MASK_RANGES:
		r = BIT(0);
		break;
	default:
		r = 0;
	}

	return r;
}

long kvm_arch_dev_ioctl(struct file *filp,
			unsigned int ioctl, unsigned long arg)
{
	return -EINVAL;
}

struct kvm *kvm_arch_alloc_vm(void)
{
	size_t sz = sizeof(struct kvm);

	if (!has_vhe())
		return kzalloc(sz, GFP_KERNEL_ACCOUNT);

	return __vmalloc(sz, GFP_KERNEL_ACCOUNT | __GFP_HIGHMEM | __GFP_ZERO);
}

int kvm_arch_vcpu_precreate(struct kvm *kvm, unsigned int id)
{
	if (irqchip_in_kernel(kvm) && vgic_initialized(kvm))
		return -EBUSY;

	if (id >= kvm->max_vcpus)
		return -EINVAL;

	return 0;
}

int kvm_arch_vcpu_create(struct kvm_vcpu *vcpu)
{
	int err;

	spin_lock_init(&vcpu->arch.mp_state_lock);

#ifdef CONFIG_LOCKDEP
	/* Inform lockdep that the config_lock is acquired after vcpu->mutex */
	mutex_lock(&vcpu->mutex);
	mutex_lock(&vcpu->kvm->arch.config_lock);
	mutex_unlock(&vcpu->kvm->arch.config_lock);
	mutex_unlock(&vcpu->mutex);
#endif

	/* Force users to call KVM_ARM_VCPU_INIT */
	vcpu_clear_flag(vcpu, VCPU_INITIALIZED);

	vcpu->arch.mmu_page_cache.gfp_zero = __GFP_ZERO;

	/* Set up the timer */
	kvm_timer_vcpu_init(vcpu);

	kvm_pmu_vcpu_init(vcpu);

	kvm_arm_pvtime_vcpu_init(&vcpu->arch);

	vcpu->arch.hw_mmu = &vcpu->kvm->arch.mmu;

	/*
	 * This vCPU may have been created after mpidr_data was initialized.
	 * Throw out the pre-computed mappings if that is the case which forces
	 * KVM to fall back to iteratively searching the vCPUs.
	 */
	kvm_destroy_mpidr_data(vcpu->kvm);

	err = kvm_vgic_vcpu_init(vcpu);
	if (err)
		return err;

	err = kvm_share_hyp(vcpu, vcpu + 1);
	if (err)
		kvm_vgic_vcpu_destroy(vcpu);

	return err;
}

void kvm_arch_vcpu_postcreate(struct kvm_vcpu *vcpu)
{
}

void kvm_arch_vcpu_destroy(struct kvm_vcpu *vcpu)
{
	if (!is_protected_kvm_enabled())
		kvm_mmu_free_memory_cache(&vcpu->arch.mmu_page_cache);
	else
		free_hyp_memcache(&vcpu->arch.pkvm_memcache);
	kvm_timer_vcpu_terminate(vcpu);
	kvm_pmu_vcpu_destroy(vcpu);
	kvm_vgic_vcpu_destroy(vcpu);
	kvm_arm_vcpu_destroy(vcpu);
}

void kvm_arch_vcpu_blocking(struct kvm_vcpu *vcpu)
{

}

void kvm_arch_vcpu_unblocking(struct kvm_vcpu *vcpu)
{

}

static void vcpu_set_pauth_traps(struct kvm_vcpu *vcpu)
{
	if (vcpu_has_ptrauth(vcpu) && !is_protected_kvm_enabled()) {
		/*
		 * Either we're running an L2 guest, and the API/APK bits come
		 * from L1's HCR_EL2, or API/APK are both set.
		 */
		if (unlikely(vcpu_has_nv(vcpu) && !is_hyp_ctxt(vcpu))) {
			u64 val;

			val = __vcpu_sys_reg(vcpu, HCR_EL2);
			val &= (HCR_API | HCR_APK);
			vcpu->arch.hcr_el2 &= ~(HCR_API | HCR_APK);
			vcpu->arch.hcr_el2 |= val;
		} else {
			vcpu->arch.hcr_el2 |= (HCR_API | HCR_APK);
		}

		/*
		 * Save the host keys if there is any chance for the guest
		 * to use pauth, as the entry code will reload the guest
		 * keys in that case.
		 */
		if (vcpu->arch.hcr_el2 & (HCR_API | HCR_APK)) {
			struct kvm_cpu_context *ctxt;

			ctxt = this_cpu_ptr_hyp_sym(kvm_hyp_ctxt);
			ptrauth_save_keys(ctxt);
		}
	}
}

static bool kvm_vcpu_should_clear_twi(struct kvm_vcpu *vcpu)
{
	if (unlikely(kvm_wfi_trap_policy != KVM_WFX_NOTRAP_SINGLE_TASK))
		return kvm_wfi_trap_policy == KVM_WFX_NOTRAP;

	return single_task_running() &&
	       (atomic_read(&vcpu->arch.vgic_cpu.vgic_v3.its_vpe.vlpi_count) ||
		vcpu->kvm->arch.vgic.nassgireq);
}

static bool kvm_vcpu_should_clear_twe(struct kvm_vcpu *vcpu)
{
	if (unlikely(kvm_wfe_trap_policy != KVM_WFX_NOTRAP_SINGLE_TASK))
		return kvm_wfe_trap_policy == KVM_WFX_NOTRAP;

	return single_task_running();
}

void kvm_arch_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	struct kvm_s2_mmu *mmu;
	int *last_ran;

	if (is_protected_kvm_enabled())
		goto nommu;

	if (vcpu_has_nv(vcpu))
		kvm_vcpu_load_hw_mmu(vcpu);

	mmu = vcpu->arch.hw_mmu;
	last_ran = this_cpu_ptr(mmu->last_vcpu_ran);

	/*
	 * Ensure a VMID is allocated for the MMU before programming VTTBR_EL2,
	 * which happens eagerly in VHE.
	 *
	 * Also, the VMID allocator only preserves VMIDs that are active at the
	 * time of rollover, so KVM might need to grab a new VMID for the MMU if
	 * this is called from kvm_sched_in().
	 */
	kvm_arm_vmid_update(&mmu->vmid);

	/*
	 * We guarantee that both TLBs and I-cache are private to each
	 * vcpu. If detecting that a vcpu from the same VM has
	 * previously run on the same physical CPU, call into the
	 * hypervisor code to nuke the relevant contexts.
	 *
	 * We might get preempted before the vCPU actually runs, but
	 * over-invalidation doesn't affect correctness.
	 */
	if (*last_ran != vcpu->vcpu_idx) {
		kvm_call_hyp(__kvm_flush_cpu_context, mmu);
		*last_ran = vcpu->vcpu_idx;
	}

nommu:
	vcpu->cpu = cpu;

	/*
	 * The timer must be loaded before the vgic to correctly set up physical
	 * interrupt deactivation in nested state (e.g. timer interrupt).
	 */
	kvm_timer_vcpu_load(vcpu);
	kvm_vgic_load(vcpu);
	kvm_vcpu_load_debug(vcpu);
	if (has_vhe())
		kvm_vcpu_load_vhe(vcpu);
	kvm_arch_vcpu_load_fp(vcpu);
	kvm_vcpu_pmu_restore_guest(vcpu);
	if (kvm_arm_is_pvtime_enabled(&vcpu->arch))
		kvm_make_request(KVM_REQ_RECORD_STEAL, vcpu);

	if (kvm_vcpu_should_clear_twe(vcpu))
		vcpu->arch.hcr_el2 &= ~HCR_TWE;
	else
		vcpu->arch.hcr_el2 |= HCR_TWE;

	if (kvm_vcpu_should_clear_twi(vcpu))
		vcpu->arch.hcr_el2 &= ~HCR_TWI;
	else
		vcpu->arch.hcr_el2 |= HCR_TWI;

	vcpu_set_pauth_traps(vcpu);

	if (is_protected_kvm_enabled()) {
		kvm_call_hyp_nvhe(__pkvm_vcpu_load,
				  vcpu->kvm->arch.pkvm.handle,
				  vcpu->vcpu_idx, vcpu->arch.hcr_el2);
		kvm_call_hyp(__vgic_v3_restore_vmcr_aprs,
			     &vcpu->arch.vgic_cpu.vgic_v3);
	}

	if (!cpumask_test_cpu(cpu, vcpu->kvm->arch.supported_cpus))
		vcpu_set_on_unsupported_cpu(vcpu);
}

void kvm_arch_vcpu_put(struct kvm_vcpu *vcpu)
{
	if (is_protected_kvm_enabled()) {
		kvm_call_hyp(__vgic_v3_save_vmcr_aprs,
			     &vcpu->arch.vgic_cpu.vgic_v3);
		kvm_call_hyp_nvhe(__pkvm_vcpu_put);
	}

	kvm_vcpu_put_debug(vcpu);
	kvm_arch_vcpu_put_fp(vcpu);
	if (has_vhe())
		kvm_vcpu_put_vhe(vcpu);
	kvm_timer_vcpu_put(vcpu);
	kvm_vgic_put(vcpu);
	kvm_vcpu_pmu_restore_host(vcpu);
	if (vcpu_has_nv(vcpu))
		kvm_vcpu_put_hw_mmu(vcpu);
	kvm_arm_vmid_clear_active();

	vcpu_clear_on_unsupported_cpu(vcpu);
	vcpu->cpu = -1;
}

static void __kvm_arm_vcpu_power_off(struct kvm_vcpu *vcpu)
{
	WRITE_ONCE(vcpu->arch.mp_state.mp_state, KVM_MP_STATE_STOPPED);
	kvm_make_request(KVM_REQ_SLEEP, vcpu);
	kvm_vcpu_kick(vcpu);
}

void kvm_arm_vcpu_power_off(struct kvm_vcpu *vcpu)
{
	spin_lock(&vcpu->arch.mp_state_lock);
	__kvm_arm_vcpu_power_off(vcpu);
	spin_unlock(&vcpu->arch.mp_state_lock);
}

bool kvm_arm_vcpu_stopped(struct kvm_vcpu *vcpu)
{
	return READ_ONCE(vcpu->arch.mp_state.mp_state) == KVM_MP_STATE_STOPPED;
}

static void kvm_arm_vcpu_suspend(struct kvm_vcpu *vcpu)
{
	WRITE_ONCE(vcpu->arch.mp_state.mp_state, KVM_MP_STATE_SUSPENDED);
	kvm_make_request(KVM_REQ_SUSPEND, vcpu);
	kvm_vcpu_kick(vcpu);
}

static bool kvm_arm_vcpu_suspended(struct kvm_vcpu *vcpu)
{
	return READ_ONCE(vcpu->arch.mp_state.mp_state) == KVM_MP_STATE_SUSPENDED;
}

int kvm_arch_vcpu_ioctl_get_mpstate(struct kvm_vcpu *vcpu,
				    struct kvm_mp_state *mp_state)
{
	*mp_state = READ_ONCE(vcpu->arch.mp_state);

	return 0;
}

int kvm_arch_vcpu_ioctl_set_mpstate(struct kvm_vcpu *vcpu,
				    struct kvm_mp_state *mp_state)
{
	int ret = 0;

	spin_lock(&vcpu->arch.mp_state_lock);

	switch (mp_state->mp_state) {
	case KVM_MP_STATE_RUNNABLE:
		WRITE_ONCE(vcpu->arch.mp_state, *mp_state);
		break;
	case KVM_MP_STATE_STOPPED:
		__kvm_arm_vcpu_power_off(vcpu);
		break;
	case KVM_MP_STATE_SUSPENDED:
		kvm_arm_vcpu_suspend(vcpu);
		break;
	default:
		ret = -EINVAL;
	}

	spin_unlock(&vcpu->arch.mp_state_lock);

	return ret;
}

/**
 * kvm_arch_vcpu_runnable - determine if the vcpu can be scheduled
 * @v:		The VCPU pointer
 *
 * If the guest CPU is not waiting for interrupts or an interrupt line is
 * asserted, the CPU is by definition runnable.
 */
int kvm_arch_vcpu_runnable(struct kvm_vcpu *v)
{
	bool irq_lines = *vcpu_hcr(v) & (HCR_VI | HCR_VF);
	return ((irq_lines || kvm_vgic_vcpu_pending_irq(v))
		&& !kvm_arm_vcpu_stopped(v) && !v->arch.pause);
}

bool kvm_arch_vcpu_in_kernel(struct kvm_vcpu *vcpu)
{
	return vcpu_mode_priv(vcpu);
}

#ifdef CONFIG_GUEST_PERF_EVENTS
unsigned long kvm_arch_vcpu_get_ip(struct kvm_vcpu *vcpu)
{
	return *vcpu_pc(vcpu);
}
#endif

static void kvm_init_mpidr_data(struct kvm *kvm)
{
	struct kvm_mpidr_data *data = NULL;
	unsigned long c, mask, nr_entries;
	u64 aff_set = 0, aff_clr = ~0UL;
	struct kvm_vcpu *vcpu;

	mutex_lock(&kvm->arch.config_lock);

	if (rcu_access_pointer(kvm->arch.mpidr_data) ||
	    atomic_read(&kvm->online_vcpus) == 1)
		goto out;

	kvm_for_each_vcpu(c, vcpu, kvm) {
		u64 aff = kvm_vcpu_get_mpidr_aff(vcpu);
		aff_set |= aff;
		aff_clr &= aff;
	}

	/*
	 * A significant bit can be either 0 or 1, and will only appear in
	 * aff_set. Use aff_clr to weed out the useless stuff.
	 */
	mask = aff_set ^ aff_clr;
	nr_entries = BIT_ULL(hweight_long(mask));

	/*
	 * Don't let userspace fool us. If we need more than a single page
	 * to describe the compressed MPIDR array, just fall back to the
	 * iterative method. Single vcpu VMs do not need this either.
	 */
	if (struct_size(data, cmpidr_to_idx, nr_entries) <= PAGE_SIZE)
		data = kzalloc(struct_size(data, cmpidr_to_idx, nr_entries),
			       GFP_KERNEL_ACCOUNT);

	if (!data)
		goto out;

	data->mpidr_mask = mask;

	kvm_for_each_vcpu(c, vcpu, kvm) {
		u64 aff = kvm_vcpu_get_mpidr_aff(vcpu);
		u16 index = kvm_mpidr_index(data, aff);

		data->cmpidr_to_idx[index] = c;
	}

	rcu_assign_pointer(kvm->arch.mpidr_data, data);
out:
	mutex_unlock(&kvm->arch.config_lock);
}

/*
 * Handle both the initialisation that is being done when the vcpu is
 * run for the first time, as well as the updates that must be
 * performed each time we get a new thread dealing with this vcpu.
 */
int kvm_arch_vcpu_run_pid_change(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;
	int ret;

	if (!kvm_vcpu_initialized(vcpu))
		return -ENOEXEC;

	if (!kvm_arm_vcpu_is_finalized(vcpu))
		return -EPERM;

	ret = kvm_arch_vcpu_run_map_fp(vcpu);
	if (ret)
		return ret;

	if (likely(vcpu_has_run_once(vcpu)))
		return 0;

	kvm_init_mpidr_data(kvm);

	if (likely(irqchip_in_kernel(kvm))) {
		/*
		 * Map the VGIC hardware resources before running a vcpu the
		 * first time on this VM.
		 */
		ret = kvm_vgic_map_resources(kvm);
		if (ret)
			return ret;
	}

	ret = kvm_finalize_sys_regs(vcpu);
	if (ret)
		return ret;

	if (vcpu_has_nv(vcpu)) {
		ret = kvm_vcpu_allocate_vncr_tlb(vcpu);
		if (ret)
			return ret;

		ret = kvm_vgic_vcpu_nv_init(vcpu);
		if (ret)
			return ret;
	}

	/*
	 * This needs to happen after any restriction has been applied
	 * to the feature set.
	 */
	kvm_calculate_traps(vcpu);

	ret = kvm_timer_enable(vcpu);
	if (ret)
		return ret;

	if (kvm_vcpu_has_pmu(vcpu)) {
		ret = kvm_arm_pmu_v3_enable(vcpu);
		if (ret)
			return ret;
	}

	if (is_protected_kvm_enabled()) {
		ret = pkvm_create_hyp_vm(kvm);
		if (ret)
			return ret;

		ret = pkvm_create_hyp_vcpu(vcpu);
		if (ret)
			return ret;
	}

	mutex_lock(&kvm->arch.config_lock);
	set_bit(KVM_ARCH_FLAG_HAS_RAN_ONCE, &kvm->arch.flags);
	mutex_unlock(&kvm->arch.config_lock);

	return ret;
}

bool kvm_arch_intc_initialized(struct kvm *kvm)
{
	return vgic_initialized(kvm);
}

void kvm_arm_halt_guest(struct kvm *kvm)
{
	unsigned long i;
	struct kvm_vcpu *vcpu;

	kvm_for_each_vcpu(i, vcpu, kvm)
		vcpu->arch.pause = true;
	kvm_make_all_cpus_request(kvm, KVM_REQ_SLEEP);
}

void kvm_arm_resume_guest(struct kvm *kvm)
{
	unsigned long i;
	struct kvm_vcpu *vcpu;

	kvm_for_each_vcpu(i, vcpu, kvm) {
		vcpu->arch.pause = false;
		__kvm_vcpu_wake_up(vcpu);
	}
}

static void kvm_vcpu_sleep(struct kvm_vcpu *vcpu)
{
	struct rcuwait *wait = kvm_arch_vcpu_get_wait(vcpu);

	rcuwait_wait_event(wait,
			   (!kvm_arm_vcpu_stopped(vcpu)) && (!vcpu->arch.pause),
			   TASK_INTERRUPTIBLE);

	if (kvm_arm_vcpu_stopped(vcpu) || vcpu->arch.pause) {
		/* Awaken to handle a signal, request we sleep again later. */
		kvm_make_request(KVM_REQ_SLEEP, vcpu);
	}

	/*
	 * Make sure we will observe a potential reset request if we've
	 * observed a change to the power state. Pairs with the smp_wmb() in
	 * kvm_psci_vcpu_on().
	 */
	smp_rmb();
}

/**
 * kvm_vcpu_wfi - emulate Wait-For-Interrupt behavior
 * @vcpu:	The VCPU pointer
 *
 * Suspend execution of a vCPU until a valid wake event is detected, i.e. until
 * the vCPU is runnable.  The vCPU may or may not be scheduled out, depending
 * on when a wake event arrives, e.g. there may already be a pending wake event.
 */
void kvm_vcpu_wfi(struct kvm_vcpu *vcpu)
{
	/*
	 * Sync back the state of the GIC CPU interface so that we have
	 * the latest PMR and group enables. This ensures that
	 * kvm_arch_vcpu_runnable has up-to-date data to decide whether
	 * we have pending interrupts, e.g. when determining if the
	 * vCPU should block.
	 *
	 * For the same reason, we want to tell GICv4 that we need
	 * doorbells to be signalled, should an interrupt become pending.
	 */
	preempt_disable();
	vcpu_set_flag(vcpu, IN_WFI);
	kvm_vgic_put(vcpu);
	preempt_enable();

	kvm_vcpu_halt(vcpu);
	vcpu_clear_flag(vcpu, IN_WFIT);

	preempt_disable();
	vcpu_clear_flag(vcpu, IN_WFI);
	kvm_vgic_load(vcpu);
	preempt_enable();
}

static int kvm_vcpu_suspend(struct kvm_vcpu *vcpu)
{
	if (!kvm_arm_vcpu_suspended(vcpu))
		return 1;

	kvm_vcpu_wfi(vcpu);

	/*
	 * The suspend state is sticky; we do not leave it until userspace
	 * explicitly marks the vCPU as runnable. Request that we suspend again
	 * later.
	 */
	kvm_make_request(KVM_REQ_SUSPEND, vcpu);

	/*
	 * Check to make sure the vCPU is actually runnable. If so, exit to
	 * userspace informing it of the wakeup condition.
	 */
	if (kvm_arch_vcpu_runnable(vcpu)) {
		memset(&vcpu->run->system_event, 0, sizeof(vcpu->run->system_event));
		vcpu->run->system_event.type = KVM_SYSTEM_EVENT_WAKEUP;
		vcpu->run->exit_reason = KVM_EXIT_SYSTEM_EVENT;
		return 0;
	}

	/*
	 * Otherwise, we were unblocked to process a different event, such as a
	 * pending signal. Return 1 and allow kvm_arch_vcpu_ioctl_run() to
	 * process the event.
	 */
	return 1;
}

/**
 * check_vcpu_requests - check and handle pending vCPU requests
 * @vcpu:	the VCPU pointer
 *
 * Return: 1 if we should enter the guest
 *	   0 if we should exit to userspace
 *	   < 0 if we should exit to userspace, where the return value indicates
 *	   an error
 */
static int check_vcpu_requests(struct kvm_vcpu *vcpu)
{
	if (kvm_request_pending(vcpu)) {
		if (kvm_check_request(KVM_REQ_VM_DEAD, vcpu))
			return -EIO;

		if (kvm_check_request(KVM_REQ_SLEEP, vcpu))
			kvm_vcpu_sleep(vcpu);

		if (kvm_check_request(KVM_REQ_VCPU_RESET, vcpu))
			kvm_reset_vcpu(vcpu);

		/*
		 * Clear IRQ_PENDING requests that were made to guarantee
		 * that a VCPU sees new virtual interrupts.
		 */
		kvm_check_request(KVM_REQ_IRQ_PENDING, vcpu);

		if (kvm_check_request(KVM_REQ_RECORD_STEAL, vcpu))
			kvm_update_stolen_time(vcpu);

		if (kvm_check_request(KVM_REQ_RELOAD_GICv4, vcpu)) {
			/* The distributor enable bits were changed */
			preempt_disable();
			vgic_v4_put(vcpu);
			vgic_v4_load(vcpu);
			preempt_enable();
		}

		if (kvm_check_request(KVM_REQ_RELOAD_PMU, vcpu))
			kvm_vcpu_reload_pmu(vcpu);

		if (kvm_check_request(KVM_REQ_RESYNC_PMU_EL0, vcpu))
			kvm_vcpu_pmu_restore_guest(vcpu);

		if (kvm_check_request(KVM_REQ_SUSPEND, vcpu))
			return kvm_vcpu_suspend(vcpu);

		if (kvm_dirty_ring_check_request(vcpu))
			return 0;

		check_nested_vcpu_requests(vcpu);
	}

	return 1;
}

static bool vcpu_mode_is_bad_32bit(struct kvm_vcpu *vcpu)
{
	if (likely(!vcpu_mode_is_32bit(vcpu)))
		return false;

	if (vcpu_has_nv(vcpu))
		return true;

	return !kvm_supports_32bit_el0();
}

/**
 * kvm_vcpu_exit_request - returns true if the VCPU should *not* enter the guest
 * @vcpu:	The VCPU pointer
 * @ret:	Pointer to write optional return code
 *
 * Returns: true if the VCPU needs to return to a preemptible + interruptible
 *	    and skip guest entry.
 *
 * This function disambiguates between two different types of exits: exits to a
 * preemptible + interruptible kernel context and exits to userspace. For an
 * exit to userspace, this function will write the return code to ret and return
 * true. For an exit to preemptible + interruptible kernel context (i.e. check
 * for pending work and re-enter), return true without writing to ret.
 */
static bool kvm_vcpu_exit_request(struct kvm_vcpu *vcpu, int *ret)
{
	struct kvm_run *run = vcpu->run;

	/*
	 * If we're using a userspace irqchip, then check if we need
	 * to tell a userspace irqchip about timer or PMU level
	 * changes and if so, exit to userspace (the actual level
	 * state gets updated in kvm_timer_update_run and
	 * kvm_pmu_update_run below).
	 */
	if (unlikely(!irqchip_in_kernel(vcpu->kvm))) {
		if (kvm_timer_should_notify_user(vcpu) ||
		    kvm_pmu_should_notify_user(vcpu)) {
			*ret = -EINTR;
			run->exit_reason = KVM_EXIT_INTR;
			return true;
		}
	}

	if (unlikely(vcpu_on_unsupported_cpu(vcpu))) {
		run->exit_reason = KVM_EXIT_FAIL_ENTRY;
		run->fail_entry.hardware_entry_failure_reason = KVM_EXIT_FAIL_ENTRY_CPU_UNSUPPORTED;
		run->fail_entry.cpu = smp_processor_id();
		*ret = 0;
		return true;
	}

	return kvm_request_pending(vcpu) ||
			xfer_to_guest_mode_work_pending();
}

/*
 * Actually run the vCPU, entering an RCU extended quiescent state (EQS) while
 * the vCPU is running.
 *
 * This must be noinstr as instrumentation may make use of RCU, and this is not
 * safe during the EQS.
 */
static int noinstr kvm_arm_vcpu_enter_exit(struct kvm_vcpu *vcpu)
{
	int ret;

	guest_state_enter_irqoff();
	ret = kvm_call_hyp_ret(__kvm_vcpu_run, vcpu);
	guest_state_exit_irqoff();

	return ret;
}

/**
 * kvm_arch_vcpu_ioctl_run - the main VCPU run function to execute guest code
 * @vcpu:	The VCPU pointer
 *
 * This function is called through the VCPU_RUN ioctl called from user space. It
 * will execute VM code in a loop until the time slice for the process is used
 * or some emulation is needed from user space in which case the function will
 * return with return value 0 and with the kvm_run structure filled in with the
 * required data for the requested emulation.
 */
int kvm_arch_vcpu_ioctl_run(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;
	int ret;

	if (run->exit_reason == KVM_EXIT_MMIO) {
		ret = kvm_handle_mmio_return(vcpu);
		if (ret <= 0)
			return ret;
	}

	vcpu_load(vcpu);

	if (!vcpu->wants_to_run) {
		ret = -EINTR;
		goto out;
	}

	kvm_sigset_activate(vcpu);

	ret = 1;
	run->exit_reason = KVM_EXIT_UNKNOWN;
	run->flags = 0;
	while (ret > 0) {
		/*
		 * Check conditions before entering the guest
		 */
		ret = xfer_to_guest_mode_handle_work(vcpu);
		if (!ret)
			ret = 1;

		if (ret > 0)
			ret = check_vcpu_requests(vcpu);

		/*
		 * Preparing the interrupts to be injected also
		 * involves poking the GIC, which must be done in a
		 * non-preemptible context.
		 */
		preempt_disable();

		if (kvm_vcpu_has_pmu(vcpu))
			kvm_pmu_flush_hwstate(vcpu);

		local_irq_disable();

		kvm_vgic_flush_hwstate(vcpu);

		kvm_pmu_update_vcpu_events(vcpu);

		/*
		 * Ensure we set mode to IN_GUEST_MODE after we disable
		 * interrupts and before the final VCPU requests check.
		 * See the comment in kvm_vcpu_exiting_guest_mode() and
		 * Documentation/virt/kvm/vcpu-requests.rst
		 */
		smp_store_mb(vcpu->mode, IN_GUEST_MODE);

		if (ret <= 0 || kvm_vcpu_exit_request(vcpu, &ret)) {
			vcpu->mode = OUTSIDE_GUEST_MODE;
			isb(); /* Ensure work in x_flush_hwstate is committed */
			if (kvm_vcpu_has_pmu(vcpu))
				kvm_pmu_sync_hwstate(vcpu);
			if (unlikely(!irqchip_in_kernel(vcpu->kvm)))
				kvm_timer_sync_user(vcpu);
			kvm_vgic_sync_hwstate(vcpu);
			local_irq_enable();
			preempt_enable();
			continue;
		}

		kvm_arch_vcpu_ctxflush_fp(vcpu);

		/**************************************************************
		 * Enter the guest
		 */
		trace_kvm_entry(*vcpu_pc(vcpu));
		guest_timing_enter_irqoff();

		ret = kvm_arm_vcpu_enter_exit(vcpu);

		vcpu->mode = OUTSIDE_GUEST_MODE;
		vcpu->stat.exits++;
		/*
		 * Back from guest
		 *************************************************************/

		/*
		 * We must sync the PMU state before the vgic state so
		 * that the vgic can properly sample the updated state of the
		 * interrupt line.
		 */
		if (kvm_vcpu_has_pmu(vcpu))
			kvm_pmu_sync_hwstate(vcpu);

		/*
		 * Sync the vgic state before syncing the timer state because
		 * the timer code needs to know if the virtual timer
		 * interrupts are active.
		 */
		kvm_vgic_sync_hwstate(vcpu);

		/*
		 * Sync the timer hardware state before enabling interrupts as
		 * we don't want vtimer interrupts to race with syncing the
		 * timer virtual interrupt state.
		 */
		if (unlikely(!irqchip_in_kernel(vcpu->kvm)))
			kvm_timer_sync_user(vcpu);

		if (is_hyp_ctxt(vcpu))
			kvm_timer_sync_nested(vcpu);

		kvm_arch_vcpu_ctxsync_fp(vcpu);

		/*
		 * We must ensure that any pending interrupts are taken before
		 * we exit guest timing so that timer ticks are accounted as
		 * guest time. Transiently unmask interrupts so that any
		 * pending interrupts are taken.
		 *
		 * Per ARM DDI 0487G.b section D1.13.4, an ISB (or other
		 * context synchronization event) is necessary to ensure that
		 * pending interrupts are taken.
		 */
		if (ARM_EXCEPTION_CODE(ret) == ARM_EXCEPTION_IRQ) {
			local_irq_enable();
			isb();
			local_irq_disable();
		}

		guest_timing_exit_irqoff();

		local_irq_enable();

		trace_kvm_exit(ret, kvm_vcpu_trap_get_class(vcpu), *vcpu_pc(vcpu));

		/* Exit types that need handling before we can be preempted */
		handle_exit_early(vcpu, ret);

		preempt_enable();

		/*
		 * The ARMv8 architecture doesn't give the hypervisor
		 * a mechanism to prevent a guest from dropping to AArch32 EL0
		 * if implemented by the CPU. If we spot the guest in such
		 * state and that we decided it wasn't supposed to do so (like
		 * with the asymmetric AArch32 case), return to userspace with
		 * a fatal error.
		 */
		if (vcpu_mode_is_bad_32bit(vcpu)) {
			/*
			 * As we have caught the guest red-handed, decide that
			 * it isn't fit for purpose anymore by making the vcpu
			 * invalid. The VMM can try and fix it by issuing  a
			 * KVM_ARM_VCPU_INIT if it really wants to.
			 */
			vcpu_clear_flag(vcpu, VCPU_INITIALIZED);
			ret = ARM_EXCEPTION_IL;
		}

		ret = handle_exit(vcpu, ret);
	}

	/* Tell userspace about in-kernel device output levels */
	if (unlikely(!irqchip_in_kernel(vcpu->kvm))) {
		kvm_timer_update_run(vcpu);
		kvm_pmu_update_run(vcpu);
	}

	kvm_sigset_deactivate(vcpu);

out:
	/*
	 * In the unlikely event that we are returning to userspace
	 * with pending exceptions or PC adjustment, commit these
	 * adjustments in order to give userspace a consistent view of
	 * the vcpu state. Note that this relies on __kvm_adjust_pc()
	 * being preempt-safe on VHE.
	 */
	if (unlikely(vcpu_get_flag(vcpu, PENDING_EXCEPTION) ||
		     vcpu_get_flag(vcpu, INCREMENT_PC)))
		kvm_call_hyp(__kvm_adjust_pc, vcpu);

	vcpu_put(vcpu);
	return ret;
}

static int vcpu_interrupt_line(struct kvm_vcpu *vcpu, int number, bool level)
{
	int bit_index;
	bool set;
	unsigned long *hcr;

	if (number == KVM_ARM_IRQ_CPU_IRQ)
		bit_index = __ffs(HCR_VI);
	else /* KVM_ARM_IRQ_CPU_FIQ */
		bit_index = __ffs(HCR_VF);

	hcr = vcpu_hcr(vcpu);
	if (level)
		set = test_and_set_bit(bit_index, hcr);
	else
		set = test_and_clear_bit(bit_index, hcr);

	/*
	 * If we didn't change anything, no need to wake up or kick other CPUs
	 */
	if (set == level)
		return 0;

	/*
	 * The vcpu irq_lines field was updated, wake up sleeping VCPUs and
	 * trigger a world-switch round on the running physical CPU to set the
	 * virtual IRQ/FIQ fields in the HCR appropriately.
	 */
	kvm_make_request(KVM_REQ_IRQ_PENDING, vcpu);
	kvm_vcpu_kick(vcpu);

	return 0;
}

int kvm_vm_ioctl_irq_line(struct kvm *kvm, struct kvm_irq_level *irq_level,
			  bool line_status)
{
	u32 irq = irq_level->irq;
	unsigned int irq_type, vcpu_id, irq_num;
	struct kvm_vcpu *vcpu = NULL;
	bool level = irq_level->level;

	irq_type = (irq >> KVM_ARM_IRQ_TYPE_SHIFT) & KVM_ARM_IRQ_TYPE_MASK;
	vcpu_id = (irq >> KVM_ARM_IRQ_VCPU_SHIFT) & KVM_ARM_IRQ_VCPU_MASK;
	vcpu_id += ((irq >> KVM_ARM_IRQ_VCPU2_SHIFT) & KVM_ARM_IRQ_VCPU2_MASK) * (KVM_ARM_IRQ_VCPU_MASK + 1);
	irq_num = (irq >> KVM_ARM_IRQ_NUM_SHIFT) & KVM_ARM_IRQ_NUM_MASK;

	trace_kvm_irq_line(irq_type, vcpu_id, irq_num, irq_level->level);

	switch (irq_type) {
	case KVM_ARM_IRQ_TYPE_CPU:
		if (irqchip_in_kernel(kvm))
			return -ENXIO;

		vcpu = kvm_get_vcpu_by_id(kvm, vcpu_id);
		if (!vcpu)
			return -EINVAL;

		if (irq_num > KVM_ARM_IRQ_CPU_FIQ)
			return -EINVAL;

		return vcpu_interrupt_line(vcpu, irq_num, level);
	case KVM_ARM_IRQ_TYPE_PPI:
		if (!irqchip_in_kernel(kvm))
			return -ENXIO;

		vcpu = kvm_get_vcpu_by_id(kvm, vcpu_id);
		if (!vcpu)
			return -EINVAL;

		if (irq_num < VGIC_NR_SGIS || irq_num >= VGIC_NR_PRIVATE_IRQS)
			return -EINVAL;

		return kvm_vgic_inject_irq(kvm, vcpu, irq_num, level, NULL);
	case KVM_ARM_IRQ_TYPE_SPI:
		if (!irqchip_in_kernel(kvm))
			return -ENXIO;

		if (irq_num < VGIC_NR_PRIVATE_IRQS)
			return -EINVAL;

		return kvm_vgic_inject_irq(kvm, NULL, irq_num, level, NULL);
	}

	return -EINVAL;
}

static unsigned long system_supported_vcpu_features(void)
{
	unsigned long features = KVM_VCPU_VALID_FEATURES;

	if (!cpus_have_final_cap(ARM64_HAS_32BIT_EL1))
		clear_bit(KVM_ARM_VCPU_EL1_32BIT, &features);

	if (!kvm_supports_guest_pmuv3())
		clear_bit(KVM_ARM_VCPU_PMU_V3, &features);

	if (!system_supports_sve())
		clear_bit(KVM_ARM_VCPU_SVE, &features);

	if (!kvm_has_full_ptr_auth()) {
		clear_bit(KVM_ARM_VCPU_PTRAUTH_ADDRESS, &features);
		clear_bit(KVM_ARM_VCPU_PTRAUTH_GENERIC, &features);
	}

	if (!cpus_have_final_cap(ARM64_HAS_NESTED_VIRT))
		clear_bit(KVM_ARM_VCPU_HAS_EL2, &features);

	return features;
}

static int kvm_vcpu_init_check_features(struct kvm_vcpu *vcpu,
					const struct kvm_vcpu_init *init)
{
	unsigned long features = init->features[0];
	int i;

	if (features & ~KVM_VCPU_VALID_FEATURES)
		return -ENOENT;

	for (i = 1; i < ARRAY_SIZE(init->features); i++) {
		if (init->features[i])
			return -ENOENT;
	}

	if (features & ~system_supported_vcpu_features())
		return -EINVAL;

	/*
	 * For now make sure that both address/generic pointer authentication
	 * features are requested by the userspace together.
	 */
	if (test_bit(KVM_ARM_VCPU_PTRAUTH_ADDRESS, &features) !=
	    test_bit(KVM_ARM_VCPU_PTRAUTH_GENERIC, &features))
		return -EINVAL;

	if (!test_bit(KVM_ARM_VCPU_EL1_32BIT, &features))
		return 0;

	/* MTE is incompatible with AArch32 */
	if (kvm_has_mte(vcpu->kvm))
		return -EINVAL;

	/* NV is incompatible with AArch32 */
	if (test_bit(KVM_ARM_VCPU_HAS_EL2, &features))
		return -EINVAL;

	return 0;
}

static bool kvm_vcpu_init_changed(struct kvm_vcpu *vcpu,
				  const struct kvm_vcpu_init *init)
{
	unsigned long features = init->features[0];

	return !bitmap_equal(vcpu->kvm->arch.vcpu_features, &features,
			     KVM_VCPU_MAX_FEATURES);
}

static int kvm_setup_vcpu(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;
	int ret = 0;

	/*
	 * When the vCPU has a PMU, but no PMU is set for the guest
	 * yet, set the default one.
	 */
	if (kvm_vcpu_has_pmu(vcpu) && !kvm->arch.arm_pmu)
		ret = kvm_arm_set_default_pmu(kvm);

	/* Prepare for nested if required */
	if (!ret && vcpu_has_nv(vcpu))
		ret = kvm_vcpu_init_nested(vcpu);

	return ret;
}

static int __kvm_vcpu_set_target(struct kvm_vcpu *vcpu,
				 const struct kvm_vcpu_init *init)
{
	unsigned long features = init->features[0];
	struct kvm *kvm = vcpu->kvm;
	int ret = -EINVAL;

	mutex_lock(&kvm->arch.config_lock);

	if (test_bit(KVM_ARCH_FLAG_VCPU_FEATURES_CONFIGURED, &kvm->arch.flags) &&
	    kvm_vcpu_init_changed(vcpu, init))
		goto out_unlock;

	bitmap_copy(kvm->arch.vcpu_features, &features, KVM_VCPU_MAX_FEATURES);

	ret = kvm_setup_vcpu(vcpu);
	if (ret)
		goto out_unlock;

	/* Now we know what it is, we can reset it. */
	kvm_reset_vcpu(vcpu);

	set_bit(KVM_ARCH_FLAG_VCPU_FEATURES_CONFIGURED, &kvm->arch.flags);
	vcpu_set_flag(vcpu, VCPU_INITIALIZED);
	ret = 0;
out_unlock:
	mutex_unlock(&kvm->arch.config_lock);
	return ret;
}

static int kvm_vcpu_set_target(struct kvm_vcpu *vcpu,
			       const struct kvm_vcpu_init *init)
{
	int ret;

	if (init->target != KVM_ARM_TARGET_GENERIC_V8 &&
	    init->target != kvm_target_cpu())
		return -EINVAL;

	ret = kvm_vcpu_init_check_features(vcpu, init);
	if (ret)
		return ret;

	if (!kvm_vcpu_initialized(vcpu))
		return __kvm_vcpu_set_target(vcpu, init);

	if (kvm_vcpu_init_changed(vcpu, init))
		return -EINVAL;

	kvm_reset_vcpu(vcpu);
	return 0;
}

static int kvm_arch_vcpu_ioctl_vcpu_init(struct kvm_vcpu *vcpu,
					 struct kvm_vcpu_init *init)
{
	bool power_off = false;
	int ret;

	/*
	 * Treat the power-off vCPU feature as ephemeral. Clear the bit to avoid
	 * reflecting it in the finalized feature set, thus limiting its scope
	 * to a single KVM_ARM_VCPU_INIT call.
	 */
	if (init->features[0] & BIT(KVM_ARM_VCPU_POWER_OFF)) {
		init->features[0] &= ~BIT(KVM_ARM_VCPU_POWER_OFF);
		power_off = true;
	}

	ret = kvm_vcpu_set_target(vcpu, init);
	if (ret)
		return ret;

	/*
	 * Ensure a rebooted VM will fault in RAM pages and detect if the
	 * guest MMU is turned off and flush the caches as needed.
	 *
	 * S2FWB enforces all memory accesses to RAM being cacheable,
	 * ensuring that the data side is always coherent. We still
	 * need to invalidate the I-cache though, as FWB does *not*
	 * imply CTR_EL0.DIC.
	 */
	if (vcpu_has_run_once(vcpu)) {
		if (!cpus_have_final_cap(ARM64_HAS_STAGE2_FWB))
			stage2_unmap_vm(vcpu->kvm);
		else
			icache_inval_all_pou();
	}

	vcpu_reset_hcr(vcpu);

	/*
	 * Handle the "start in power-off" case.
	 */
	spin_lock(&vcpu->arch.mp_state_lock);

	if (power_off)
		__kvm_arm_vcpu_power_off(vcpu);
	else
		WRITE_ONCE(vcpu->arch.mp_state.mp_state, KVM_MP_STATE_RUNNABLE);

	spin_unlock(&vcpu->arch.mp_state_lock);

	return 0;
}

static int kvm_arm_vcpu_set_attr(struct kvm_vcpu *vcpu,
				 struct kvm_device_attr *attr)
{
	int ret = -ENXIO;

	switch (attr->group) {
	default:
		ret = kvm_arm_vcpu_arch_set_attr(vcpu, attr);
		break;
	}

	return ret;
}

static int kvm_arm_vcpu_get_attr(struct kvm_vcpu *vcpu,
				 struct kvm_device_attr *attr)
{
	int ret = -ENXIO;

	switch (attr->group) {
	default:
		ret = kvm_arm_vcpu_arch_get_attr(vcpu, attr);
		break;
	}

	return ret;
}

static int kvm_arm_vcpu_has_attr(struct kvm_vcpu *vcpu,
				 struct kvm_device_attr *attr)
{
	int ret = -ENXIO;

	switch (attr->group) {
	default:
		ret = kvm_arm_vcpu_arch_has_attr(vcpu, attr);
		break;
	}

	return ret;
}

static int kvm_arm_vcpu_get_events(struct kvm_vcpu *vcpu,
				   struct kvm_vcpu_events *events)
{
	memset(events, 0, sizeof(*events));

	return __kvm_arm_vcpu_get_events(vcpu, events);
}

static int kvm_arm_vcpu_set_events(struct kvm_vcpu *vcpu,
				   struct kvm_vcpu_events *events)
{
	int i;

	/* check whether the reserved field is zero */
	for (i = 0; i < ARRAY_SIZE(events->reserved); i++)
		if (events->reserved[i])
			return -EINVAL;

	/* check whether the pad field is zero */
	for (i = 0; i < ARRAY_SIZE(events->exception.pad); i++)
		if (events->exception.pad[i])
			return -EINVAL;

	return __kvm_arm_vcpu_set_events(vcpu, events);
}

long kvm_arch_vcpu_ioctl(struct file *filp,
			 unsigned int ioctl, unsigned long arg)
{
	struct kvm_vcpu *vcpu = filp->private_data;
	void __user *argp = (void __user *)arg;
	struct kvm_device_attr attr;
	long r;

	switch (ioctl) {
	case KVM_ARM_VCPU_INIT: {
		struct kvm_vcpu_init init;

		r = -EFAULT;
		if (copy_from_user(&init, argp, sizeof(init)))
			break;

		r = kvm_arch_vcpu_ioctl_vcpu_init(vcpu, &init);
		break;
	}
	case KVM_SET_ONE_REG:
	case KVM_GET_ONE_REG: {
		struct kvm_one_reg reg;

		r = -ENOEXEC;
		if (unlikely(!kvm_vcpu_initialized(vcpu)))
			break;

		r = -EFAULT;
		if (copy_from_user(&reg, argp, sizeof(reg)))
			break;

		/*
		 * We could owe a reset due to PSCI. Handle the pending reset
		 * here to ensure userspace register accesses are ordered after
		 * the reset.
		 */
		if (kvm_check_request(KVM_REQ_VCPU_RESET, vcpu))
			kvm_reset_vcpu(vcpu);

		if (ioctl == KVM_SET_ONE_REG)
			r = kvm_arm_set_reg(vcpu, &reg);
		else
			r = kvm_arm_get_reg(vcpu, &reg);
		break;
	}
	case KVM_GET_REG_LIST: {
		struct kvm_reg_list __user *user_list = argp;
		struct kvm_reg_list reg_list;
		unsigned n;

		r = -ENOEXEC;
		if (unlikely(!kvm_vcpu_initialized(vcpu)))
			break;

		r = -EPERM;
		if (!kvm_arm_vcpu_is_finalized(vcpu))
			break;

		r = -EFAULT;
		if (copy_from_user(&reg_list, user_list, sizeof(reg_list)))
			break;
		n = reg_list.n;
		reg_list.n = kvm_arm_num_regs(vcpu);
		if (copy_to_user(user_list, &reg_list, sizeof(reg_list)))
			break;
		r = -E2BIG;
		if (n < reg_list.n)
			break;
		r = kvm_arm_copy_reg_indices(vcpu, user_list->reg);
		break;
	}
	case KVM_SET_DEVICE_ATTR: {
		r = -EFAULT;
		if (copy_from_user(&attr, argp, sizeof(attr)))
			break;
		r = kvm_arm_vcpu_set_attr(vcpu, &attr);
		break;
	}
	case KVM_GET_DEVICE_ATTR: {
		r = -EFAULT;
		if (copy_from_user(&attr, argp, sizeof(attr)))
			break;
		r = kvm_arm_vcpu_get_attr(vcpu, &attr);
		break;
	}
	case KVM_HAS_DEVICE_ATTR: {
		r = -EFAULT;
		if (copy_from_user(&attr, argp, sizeof(attr)))
			break;
		r = kvm_arm_vcpu_has_attr(vcpu, &attr);
		break;
	}
	case KVM_GET_VCPU_EVENTS: {
		struct kvm_vcpu_events events;

		if (kvm_arm_vcpu_get_events(vcpu, &events))
			return -EINVAL;

		if (copy_to_user(argp, &events, sizeof(events)))
			return -EFAULT;

		return 0;
	}
	case KVM_SET_VCPU_EVENTS: {
		struct kvm_vcpu_events events;

		if (copy_from_user(&events, argp, sizeof(events)))
			return -EFAULT;

		return kvm_arm_vcpu_set_events(vcpu, &events);
	}
	case KVM_ARM_VCPU_FINALIZE: {
		int what;

		if (!kvm_vcpu_initialized(vcpu))
			return -ENOEXEC;

		if (get_user(what, (const int __user *)argp))
			return -EFAULT;

		return kvm_arm_vcpu_finalize(vcpu, what);
	}
	default:
		r = -EINVAL;
	}

	return r;
}

void kvm_arch_sync_dirty_log(struct kvm *kvm, struct kvm_memory_slot *memslot)
{

}

static int kvm_vm_ioctl_set_device_addr(struct kvm *kvm,
					struct kvm_arm_device_addr *dev_addr)
{
	switch (FIELD_GET(KVM_ARM_DEVICE_ID_MASK, dev_addr->id)) {
	case KVM_ARM_DEVICE_VGIC_V2:
		if (!vgic_present)
			return -ENXIO;
		return kvm_set_legacy_vgic_v2_addr(kvm, dev_addr);
	default:
		return -ENODEV;
	}
}

static int kvm_vm_has_attr(struct kvm *kvm, struct kvm_device_attr *attr)
{
	switch (attr->group) {
	case KVM_ARM_VM_SMCCC_CTRL:
		return kvm_vm_smccc_has_attr(kvm, attr);
	default:
		return -ENXIO;
	}
}

static int kvm_vm_set_attr(struct kvm *kvm, struct kvm_device_attr *attr)
{
	switch (attr->group) {
	case KVM_ARM_VM_SMCCC_CTRL:
		return kvm_vm_smccc_set_attr(kvm, attr);
	default:
		return -ENXIO;
	}
}

int kvm_arch_vm_ioctl(struct file *filp, unsigned int ioctl, unsigned long arg)
{
	struct kvm *kvm = filp->private_data;
	void __user *argp = (void __user *)arg;
	struct kvm_device_attr attr;

	switch (ioctl) {
	case KVM_CREATE_IRQCHIP: {
		int ret;
		if (!vgic_present)
			return -ENXIO;
		mutex_lock(&kvm->lock);
		ret = kvm_vgic_create(kvm, KVM_DEV_TYPE_ARM_VGIC_V2);
		mutex_unlock(&kvm->lock);
		return ret;
	}
	case KVM_ARM_SET_DEVICE_ADDR: {
		struct kvm_arm_device_addr dev_addr;

		if (copy_from_user(&dev_addr, argp, sizeof(dev_addr)))
			return -EFAULT;
		return kvm_vm_ioctl_set_device_addr(kvm, &dev_addr);
	}
	case KVM_ARM_PREFERRED_TARGET: {
		struct kvm_vcpu_init init = {
			.target = KVM_ARM_TARGET_GENERIC_V8,
		};

		if (copy_to_user(argp, &init, sizeof(init)))
			return -EFAULT;

		return 0;
	}
	case KVM_ARM_MTE_COPY_TAGS: {
		struct kvm_arm_copy_mte_tags copy_tags;

		if (copy_from_user(&copy_tags, argp, sizeof(copy_tags)))
			return -EFAULT;
		return kvm_vm_ioctl_mte_copy_tags(kvm, &copy_tags);
	}
	case KVM_ARM_SET_COUNTER_OFFSET: {
		struct kvm_arm_counter_offset offset;

		if (copy_from_user(&offset, argp, sizeof(offset)))
			return -EFAULT;
		return kvm_vm_ioctl_set_counter_offset(kvm, &offset);
	}
	case KVM_HAS_DEVICE_ATTR: {
		if (copy_from_user(&attr, argp, sizeof(attr)))
			return -EFAULT;

		return kvm_vm_has_attr(kvm, &attr);
	}
	case KVM_SET_DEVICE_ATTR: {
		if (copy_from_user(&attr, argp, sizeof(attr)))
			return -EFAULT;

		return kvm_vm_set_attr(kvm, &attr);
	}
	case KVM_ARM_GET_REG_WRITABLE_MASKS: {
		struct reg_mask_range range;

		if (copy_from_user(&range, argp, sizeof(range)))
			return -EFAULT;
		return kvm_vm_ioctl_get_reg_writable_masks(kvm, &range);
	}
	default:
		return -EINVAL;
	}
}

static unsigned long nvhe_percpu_size(void)
{
	return (unsigned long)CHOOSE_NVHE_SYM(__per_cpu_end) -
		(unsigned long)CHOOSE_NVHE_SYM(__per_cpu_start);
}

static unsigned long nvhe_percpu_order(void)
{
	unsigned long size = nvhe_percpu_size();

	return size ? get_order(size) : 0;
}

static size_t pkvm_host_sve_state_order(void)
{
	return get_order(pkvm_host_sve_state_size());
}

/* A lookup table holding the hypervisor VA for each vector slot */
static void *hyp_spectre_vector_selector[BP_HARDEN_EL2_SLOTS];

static void kvm_init_vector_slot(void *base, enum arm64_hyp_spectre_vector slot)
{
	hyp_spectre_vector_selector[slot] = __kvm_vector_slot2addr(base, slot);
}

static int kvm_init_vector_slots(void)
{
	int err;
	void *base;

	base = kern_hyp_va(kvm_ksym_ref(__kvm_hyp_vector));
	kvm_init_vector_slot(base, HYP_VECTOR_DIRECT);

	base = kern_hyp_va(kvm_ksym_ref(__bp_harden_hyp_vecs));
	kvm_init_vector_slot(base, HYP_VECTOR_SPECTRE_DIRECT);

	if (kvm_system_needs_idmapped_vectors() &&
	    !is_protected_kvm_enabled()) {
		err = create_hyp_exec_mappings(__pa_symbol(__bp_harden_hyp_vecs),
					       __BP_HARDEN_HYP_VECS_SZ, &base);
		if (err)
			return err;
	}

	kvm_init_vector_slot(base, HYP_VECTOR_INDIRECT);
	kvm_init_vector_slot(base, HYP_VECTOR_SPECTRE_INDIRECT);
	return 0;
}

static void __init cpu_prepare_hyp_mode(int cpu, u32 hyp_va_bits)
{
	struct kvm_nvhe_init_params *params = per_cpu_ptr_nvhe_sym(kvm_init_params, cpu);
	unsigned long tcr;

	/*
	 * Calculate the raw per-cpu offset without a translation from the
	 * kernel's mapping to the linear mapping, and store it in tpidr_el2
	 * so that we can use adr_l to access per-cpu variables in EL2.
	 * Also drop the KASAN tag which gets in the way...
	 */
	params->tpidr_el2 = (unsigned long)kasan_reset_tag(per_cpu_ptr_nvhe_sym(__per_cpu_start, cpu)) -
			    (unsigned long)kvm_ksym_ref(CHOOSE_NVHE_SYM(__per_cpu_start));

	params->mair_el2 = read_sysreg(mair_el1);

	tcr = read_sysreg(tcr_el1);
	if (cpus_have_final_cap(ARM64_KVM_HVHE)) {
		tcr &= ~(TCR_HD | TCR_HA | TCR_A1 | TCR_T0SZ_MASK);
		tcr |= TCR_EPD1_MASK;
	} else {
		unsigned long ips = FIELD_GET(TCR_IPS_MASK, tcr);

		tcr &= TCR_EL2_MASK;
		tcr |= TCR_EL2_RES1 | FIELD_PREP(TCR_EL2_PS_MASK, ips);
		if (lpa2_is_enabled())
			tcr |= TCR_EL2_DS;
	}
	tcr |= TCR_T0SZ(hyp_va_bits);
	params->tcr_el2 = tcr;

	params->pgd_pa = kvm_mmu_get_httbr();
	if (is_protected_kvm_enabled())
		params->hcr_el2 = HCR_HOST_NVHE_PROTECTED_FLAGS;
	else
		params->hcr_el2 = HCR_HOST_NVHE_FLAGS;
	if (cpus_have_final_cap(ARM64_KVM_HVHE))
		params->hcr_el2 |= HCR_E2H;
	params->vttbr = params->vtcr = 0;

	/*
	 * Flush the init params from the data cache because the struct will
	 * be read while the MMU is off.
	 */
	kvm_flush_dcache_to_poc(params, sizeof(*params));
}

static void hyp_install_host_vector(void)
{
	struct kvm_nvhe_init_params *params;
	struct arm_smccc_res res;

	/* Switch from the HYP stub to our own HYP init vector */
	__hyp_set_vectors(kvm_get_idmap_vector());

	/*
	 * Call initialization code, and switch to the full blown HYP code.
	 * If the cpucaps haven't been finalized yet, something has gone very
	 * wrong, and hyp will crash and burn when it uses any
	 * cpus_have_*_cap() wrapper.
	 */
	BUG_ON(!system_capabilities_finalized());
	params = this_cpu_ptr_nvhe_sym(kvm_init_params);
	arm_smccc_1_1_hvc(KVM_HOST_SMCCC_FUNC(__kvm_hyp_init), virt_to_phys(params), &res);
	WARN_ON(res.a0 != SMCCC_RET_SUCCESS);
}

static void cpu_init_hyp_mode(void)
{
	hyp_install_host_vector();

	/*
	 * Disabling SSBD on a non-VHE system requires us to enable SSBS
	 * at EL2.
	 */
	if (this_cpu_has_cap(ARM64_SSBS) &&
	    arm64_get_spectre_v4_state() == SPECTRE_VULNERABLE) {
		kvm_call_hyp_nvhe(__kvm_enable_ssbs);
	}
}

static void cpu_hyp_reset(void)
{
	if (!is_kernel_in_hyp_mode())
		__hyp_reset_vectors();
}

/*
 * EL2 vectors can be mapped and rerouted in a number of ways,
 * depending on the kernel configuration and CPU present:
 *
 * - If the CPU is affected by Spectre-v2, the hardening sequence is
 *   placed in one of the vector slots, which is executed before jumping
 *   to the real vectors.
 *
 * - If the CPU also has the ARM64_SPECTRE_V3A cap, the slot
 *   containing the hardening sequence is mapped next to the idmap page,
 *   and executed before jumping to the real vectors.
 *
 * - If the CPU only has the ARM64_SPECTRE_V3A cap, then an
 *   empty slot is selected, mapped next to the idmap page, and
 *   executed before jumping to the real vectors.
 *
 * Note that ARM64_SPECTRE_V3A is somewhat incompatible with
 * VHE, as we don't have hypervisor-specific mappings. If the system
 * is VHE and yet selects this capability, it will be ignored.
 */
static void cpu_set_hyp_vector(void)
{
	struct bp_hardening_data *data = this_cpu_ptr(&bp_hardening_data);
	void *vector = hyp_spectre_vector_selector[data->slot];

	if (!is_protected_kvm_enabled())
		*this_cpu_ptr_hyp_sym(kvm_hyp_vector) = (unsigned long)vector;
	else
		kvm_call_hyp_nvhe(__pkvm_cpu_set_vector, data->slot);
}

static void cpu_hyp_init_context(void)
{
	kvm_init_host_cpu_context(host_data_ptr(host_ctxt));
	kvm_init_host_debug_data();

	if (!is_kernel_in_hyp_mode())
		cpu_init_hyp_mode();
}

static void cpu_hyp_init_features(void)
{
	cpu_set_hyp_vector();

	if (is_kernel_in_hyp_mode())
		kvm_timer_init_vhe();

	if (vgic_present)
		kvm_vgic_init_cpu_hardware();
}

static void cpu_hyp_reinit(void)
{
	cpu_hyp_reset();
	cpu_hyp_init_context();
	cpu_hyp_init_features();
}

static void cpu_hyp_init(void *discard)
{
	if (!__this_cpu_read(kvm_hyp_initialized)) {
		cpu_hyp_reinit();
		__this_cpu_write(kvm_hyp_initialized, 1);
	}
}

static void cpu_hyp_uninit(void *discard)
{
	if (__this_cpu_read(kvm_hyp_initialized)) {
		cpu_hyp_reset();
		__this_cpu_write(kvm_hyp_initialized, 0);
	}
}

int kvm_arch_enable_virtualization_cpu(void)
{
	/*
	 * Most calls to this function are made with migration
	 * disabled, but not with preemption disabled. The former is
	 * enough to ensure correctness, but most of the helpers
	 * expect the later and will throw a tantrum otherwise.
	 */
	preempt_disable();

	cpu_hyp_init(NULL);

	kvm_vgic_cpu_up();
	kvm_timer_cpu_up();

	preempt_enable();

	return 0;
}

void kvm_arch_disable_virtualization_cpu(void)
{
	kvm_timer_cpu_down();
	kvm_vgic_cpu_down();

	if (!is_protected_kvm_enabled())
		cpu_hyp_uninit(NULL);
}

#ifdef CONFIG_CPU_PM
static int hyp_init_cpu_pm_notifier(struct notifier_block *self,
				    unsigned long cmd,
				    void *v)
{
	/*
	 * kvm_hyp_initialized is left with its old value over
	 * PM_ENTER->PM_EXIT. It is used to indicate PM_EXIT should
	 * re-enable hyp.
	 */
	switch (cmd) {
	case CPU_PM_ENTER:
		if (__this_cpu_read(kvm_hyp_initialized))
			/*
			 * don't update kvm_hyp_initialized here
			 * so that the hyp will be re-enabled
			 * when we resume. See below.
			 */
			cpu_hyp_reset();

		return NOTIFY_OK;
	case CPU_PM_ENTER_FAILED:
	case CPU_PM_EXIT:
		if (__this_cpu_read(kvm_hyp_initialized))
			/* The hyp was enabled before suspend. */
			cpu_hyp_reinit();

		return NOTIFY_OK;

	default:
		return NOTIFY_DONE;
	}
}

static struct notifier_block hyp_init_cpu_pm_nb = {
	.notifier_call = hyp_init_cpu_pm_notifier,
};

static void __init hyp_cpu_pm_init(void)
{
	if (!is_protected_kvm_enabled())
		cpu_pm_register_notifier(&hyp_init_cpu_pm_nb);
}
static void __init hyp_cpu_pm_exit(void)
{
	if (!is_protected_kvm_enabled())
		cpu_pm_unregister_notifier(&hyp_init_cpu_pm_nb);
}
#else
static inline void __init hyp_cpu_pm_init(void)
{
}
static inline void __init hyp_cpu_pm_exit(void)
{
}
#endif

static void __init init_cpu_logical_map(void)
{
	unsigned int cpu;

	/*
	 * Copy the MPIDR <-> logical CPU ID mapping to hyp.
	 * Only copy the set of online CPUs whose features have been checked
	 * against the finalized system capabilities. The hypervisor will not
	 * allow any other CPUs from the `possible` set to boot.
	 */
	for_each_online_cpu(cpu)
		hyp_cpu_logical_map[cpu] = cpu_logical_map(cpu);
}

#define init_psci_0_1_impl_state(config, what)	\
	config.psci_0_1_ ## what ## _implemented = psci_ops.what

static bool __init init_psci_relay(void)
{
	/*
	 * If PSCI has not been initialized, protected KVM cannot install
	 * itself on newly booted CPUs.
	 */
	if (!psci_ops.get_version) {
		kvm_err("Cannot initialize protected mode without PSCI\n");
		return false;
	}

	kvm_host_psci_config.version = psci_ops.get_version();
	kvm_host_psci_config.smccc_version = arm_smccc_get_version();

	if (kvm_host_psci_config.version == PSCI_VERSION(0, 1)) {
		kvm_host_psci_config.function_ids_0_1 = get_psci_0_1_function_ids();
		init_psci_0_1_impl_state(kvm_host_psci_config, cpu_suspend);
		init_psci_0_1_impl_state(kvm_host_psci_config, cpu_on);
		init_psci_0_1_impl_state(kvm_host_psci_config, cpu_off);
		init_psci_0_1_impl_state(kvm_host_psci_config, migrate);
	}
	return true;
}

static int __init init_subsystems(void)
{
	int err = 0;

	/*
	 * Enable hardware so that subsystem initialisation can access EL2.
	 */
	on_each_cpu(cpu_hyp_init, NULL, 1);

	/*
	 * Register CPU lower-power notifier
	 */
	hyp_cpu_pm_init();

	/*
	 * Init HYP view of VGIC
	 */
	err = kvm_vgic_hyp_init();
	switch (err) {
	case 0:
		vgic_present = true;
		break;
	case -ENODEV:
	case -ENXIO:
		/*
		 * No VGIC? No pKVM for you.
		 *
		 * Protected mode assumes that VGICv3 is present, so no point
		 * in trying to hobble along if vgic initialization fails.
		 */
		if (is_protected_kvm_enabled())
			goto out;

		/*
		 * Otherwise, userspace could choose to implement a GIC for its
		 * guest on non-cooperative hardware.
		 */
		vgic_present = false;
		err = 0;
		break;
	default:
		goto out;
	}

	if (kvm_mode == KVM_MODE_NV &&
	   !(vgic_present && kvm_vgic_global_state.type == VGIC_V3)) {
		kvm_err("NV support requires GICv3, giving up\n");
		err = -EINVAL;
		goto out;
	}

	/*
	 * Init HYP architected timer support
	 */
	err = kvm_timer_hyp_init(vgic_present);
	if (err)
		goto out;

	kvm_register_perf_callbacks(NULL);

out:
	if (err)
		hyp_cpu_pm_exit();

	if (err || !is_protected_kvm_enabled())
		on_each_cpu(cpu_hyp_uninit, NULL, 1);

	return err;
}

static void __init teardown_subsystems(void)
{
	kvm_unregister_perf_callbacks();
	hyp_cpu_pm_exit();
}

static void __init teardown_hyp_mode(void)
{
	bool free_sve = system_supports_sve() && is_protected_kvm_enabled();
	int cpu;

	free_hyp_pgds();
	for_each_possible_cpu(cpu) {
		free_pages(per_cpu(kvm_arm_hyp_stack_base, cpu), NVHE_STACK_SHIFT - PAGE_SHIFT);
		free_pages(kvm_nvhe_sym(kvm_arm_hyp_percpu_base)[cpu], nvhe_percpu_order());

		if (free_sve) {
			struct cpu_sve_state *sve_state;

			sve_state = per_cpu_ptr_nvhe_sym(kvm_host_data, cpu)->sve_state;
			free_pages((unsigned long) sve_state, pkvm_host_sve_state_order());
		}
	}
}

static int __init do_pkvm_init(u32 hyp_va_bits)
{
	void *per_cpu_base = kvm_ksym_ref(kvm_nvhe_sym(kvm_arm_hyp_percpu_base));
	int ret;

	preempt_disable();
	cpu_hyp_init_context();
	ret = kvm_call_hyp_nvhe(__pkvm_init, hyp_mem_base, hyp_mem_size,
				num_possible_cpus(), kern_hyp_va(per_cpu_base),
				hyp_va_bits);
	cpu_hyp_init_features();

	/*
	 * The stub hypercalls are now disabled, so set our local flag to
	 * prevent a later re-init attempt in kvm_arch_enable_virtualization_cpu().
	 */
	__this_cpu_write(kvm_hyp_initialized, 1);
	preempt_enable();

	return ret;
}

static u64 get_hyp_id_aa64pfr0_el1(void)
{
	/*
	 * Track whether the system isn't affected by spectre/meltdown in the
	 * hypervisor's view of id_aa64pfr0_el1, used for protected VMs.
	 * Although this is per-CPU, we make it global for simplicity, e.g., not
	 * to have to worry about vcpu migration.
	 *
	 * Unlike for non-protected VMs, userspace cannot override this for
	 * protected VMs.
	 */
	u64 val = read_sanitised_ftr_reg(SYS_ID_AA64PFR0_EL1);

	val &= ~(ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_CSV2) |
		 ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_CSV3));

	val |= FIELD_PREP(ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_CSV2),
			  arm64_get_spectre_v2_state() == SPECTRE_UNAFFECTED);
	val |= FIELD_PREP(ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_CSV3),
			  arm64_get_meltdown_state() == SPECTRE_UNAFFECTED);

	return val;
}

static void kvm_hyp_init_symbols(void)
{
	kvm_nvhe_sym(id_aa64pfr0_el1_sys_val) = get_hyp_id_aa64pfr0_el1();
	kvm_nvhe_sym(id_aa64pfr1_el1_sys_val) = read_sanitised_ftr_reg(SYS_ID_AA64PFR1_EL1);
	kvm_nvhe_sym(id_aa64isar0_el1_sys_val) = read_sanitised_ftr_reg(SYS_ID_AA64ISAR0_EL1);
	kvm_nvhe_sym(id_aa64isar1_el1_sys_val) = read_sanitised_ftr_reg(SYS_ID_AA64ISAR1_EL1);
	kvm_nvhe_sym(id_aa64isar2_el1_sys_val) = read_sanitised_ftr_reg(SYS_ID_AA64ISAR2_EL1);
	kvm_nvhe_sym(id_aa64mmfr0_el1_sys_val) = read_sanitised_ftr_reg(SYS_ID_AA64MMFR0_EL1);
	kvm_nvhe_sym(id_aa64mmfr1_el1_sys_val) = read_sanitised_ftr_reg(SYS_ID_AA64MMFR1_EL1);
	kvm_nvhe_sym(id_aa64mmfr2_el1_sys_val) = read_sanitised_ftr_reg(SYS_ID_AA64MMFR2_EL1);
	kvm_nvhe_sym(id_aa64smfr0_el1_sys_val) = read_sanitised_ftr_reg(SYS_ID_AA64SMFR0_EL1);
	kvm_nvhe_sym(__icache_flags) = __icache_flags;
	kvm_nvhe_sym(kvm_arm_vmid_bits) = kvm_arm_vmid_bits;

	/* Propagate the FGT state to the the nVHE side */
	kvm_nvhe_sym(hfgrtr_masks)  = hfgrtr_masks;
	kvm_nvhe_sym(hfgwtr_masks)  = hfgwtr_masks;
	kvm_nvhe_sym(hfgitr_masks)  = hfgitr_masks;
	kvm_nvhe_sym(hdfgrtr_masks) = hdfgrtr_masks;
	kvm_nvhe_sym(hdfgwtr_masks) = hdfgwtr_masks;
	kvm_nvhe_sym(hafgrtr_masks) = hafgrtr_masks;
	kvm_nvhe_sym(hfgrtr2_masks) = hfgrtr2_masks;
	kvm_nvhe_sym(hfgwtr2_masks) = hfgwtr2_masks;
	kvm_nvhe_sym(hfgitr2_masks) = hfgitr2_masks;
	kvm_nvhe_sym(hdfgrtr2_masks)= hdfgrtr2_masks;
	kvm_nvhe_sym(hdfgwtr2_masks)= hdfgwtr2_masks;

	/*
	 * Flush entire BSS since part of its data containing init symbols is read
	 * while the MMU is off.
	 */
	kvm_flush_dcache_to_poc(kvm_ksym_ref(__hyp_bss_start),
				kvm_ksym_ref(__hyp_bss_end) - kvm_ksym_ref(__hyp_bss_start));
}

static int __init kvm_hyp_init_protection(u32 hyp_va_bits)
{
	void *addr = phys_to_virt(hyp_mem_base);
	int ret;

	ret = create_hyp_mappings(addr, addr + hyp_mem_size, PAGE_HYP);
	if (ret)
		return ret;

	ret = do_pkvm_init(hyp_va_bits);
	if (ret)
		return ret;

	free_hyp_pgds();

	return 0;
}

static int init_pkvm_host_sve_state(void)
{
	int cpu;

	if (!system_supports_sve())
		return 0;

	/* Allocate pages for host sve state in protected mode. */
	for_each_possible_cpu(cpu) {
		struct page *page = alloc_pages(GFP_KERNEL, pkvm_host_sve_state_order());

		if (!page)
			return -ENOMEM;

		per_cpu_ptr_nvhe_sym(kvm_host_data, cpu)->sve_state = page_address(page);
	}

	/*
	 * Don't map the pages in hyp since these are only used in protected
	 * mode, which will (re)create its own mapping when initialized.
	 */

	return 0;
}

/*
 * Finalizes the initialization of hyp mode, once everything else is initialized
 * and the initialziation process cannot fail.
 */
static void finalize_init_hyp_mode(void)
{
	int cpu;

	if (system_supports_sve() && is_protected_kvm_enabled()) {
		for_each_possible_cpu(cpu) {
			struct cpu_sve_state *sve_state;

			sve_state = per_cpu_ptr_nvhe_sym(kvm_host_data, cpu)->sve_state;
			per_cpu_ptr_nvhe_sym(kvm_host_data, cpu)->sve_state =
				kern_hyp_va(sve_state);
		}
	}
}

static void pkvm_hyp_init_ptrauth(void)
{
	struct kvm_cpu_context *hyp_ctxt;
	int cpu;

	for_each_possible_cpu(cpu) {
		hyp_ctxt = per_cpu_ptr_nvhe_sym(kvm_hyp_ctxt, cpu);
		hyp_ctxt->sys_regs[APIAKEYLO_EL1] = get_random_long();
		hyp_ctxt->sys_regs[APIAKEYHI_EL1] = get_random_long();
		hyp_ctxt->sys_regs[APIBKEYLO_EL1] = get_random_long();
		hyp_ctxt->sys_regs[APIBKEYHI_EL1] = get_random_long();
		hyp_ctxt->sys_regs[APDAKEYLO_EL1] = get_random_long();
		hyp_ctxt->sys_regs[APDAKEYHI_EL1] = get_random_long();
		hyp_ctxt->sys_regs[APDBKEYLO_EL1] = get_random_long();
		hyp_ctxt->sys_regs[APDBKEYHI_EL1] = get_random_long();
		hyp_ctxt->sys_regs[APGAKEYLO_EL1] = get_random_long();
		hyp_ctxt->sys_regs[APGAKEYHI_EL1] = get_random_long();
	}
}

/* Inits Hyp-mode on all online CPUs */
static int __init init_hyp_mode(void)
{
	u32 hyp_va_bits;
	int cpu;
	int err = -ENOMEM;

	/*
	 * The protected Hyp-mode cannot be initialized if the memory pool
	 * allocation has failed.
	 */
	if (is_protected_kvm_enabled() && !hyp_mem_base)
		goto out_err;

	/*
	 * Allocate Hyp PGD and setup Hyp identity mapping
	 */
	err = kvm_mmu_init(&hyp_va_bits);
	if (err)
		goto out_err;

	/*
	 * Allocate stack pages for Hypervisor-mode
	 */
	for_each_possible_cpu(cpu) {
		unsigned long stack_base;

		stack_base = __get_free_pages(GFP_KERNEL, NVHE_STACK_SHIFT - PAGE_SHIFT);
		if (!stack_base) {
			err = -ENOMEM;
			goto out_err;
		}

		per_cpu(kvm_arm_hyp_stack_base, cpu) = stack_base;
	}

	/*
	 * Allocate and initialize pages for Hypervisor-mode percpu regions.
	 */
	for_each_possible_cpu(cpu) {
		struct page *page;
		void *page_addr;

		page = alloc_pages(GFP_KERNEL, nvhe_percpu_order());
		if (!page) {
			err = -ENOMEM;
			goto out_err;
		}

		page_addr = page_address(page);
		memcpy(page_addr, CHOOSE_NVHE_SYM(__per_cpu_start), nvhe_percpu_size());
		kvm_nvhe_sym(kvm_arm_hyp_percpu_base)[cpu] = (unsigned long)page_addr;
	}

	/*
	 * Map the Hyp-code called directly from the host
	 */
	err = create_hyp_mappings(kvm_ksym_ref(__hyp_text_start),
				  kvm_ksym_ref(__hyp_text_end), PAGE_HYP_EXEC);
	if (err) {
		kvm_err("Cannot map world-switch code\n");
		goto out_err;
	}

	err = create_hyp_mappings(kvm_ksym_ref(__hyp_data_start),
				  kvm_ksym_ref(__hyp_data_end), PAGE_HYP);
	if (err) {
		kvm_err("Cannot map .hyp.data section\n");
		goto out_err;
	}

	err = create_hyp_mappings(kvm_ksym_ref(__hyp_rodata_start),
				  kvm_ksym_ref(__hyp_rodata_end), PAGE_HYP_RO);
	if (err) {
		kvm_err("Cannot map .hyp.rodata section\n");
		goto out_err;
	}

	err = create_hyp_mappings(kvm_ksym_ref(__start_rodata),
				  kvm_ksym_ref(__end_rodata), PAGE_HYP_RO);
	if (err) {
		kvm_err("Cannot map rodata section\n");
		goto out_err;
	}

	/*
	 * .hyp.bss is guaranteed to be placed at the beginning of the .bss
	 * section thanks to an assertion in the linker script. Map it RW and
	 * the rest of .bss RO.
	 */
	err = create_hyp_mappings(kvm_ksym_ref(__hyp_bss_start),
				  kvm_ksym_ref(__hyp_bss_end), PAGE_HYP);
	if (err) {
		kvm_err("Cannot map hyp bss section: %d\n", err);
		goto out_err;
	}

	err = create_hyp_mappings(kvm_ksym_ref(__hyp_bss_end),
				  kvm_ksym_ref(__bss_stop), PAGE_HYP_RO);
	if (err) {
		kvm_err("Cannot map bss section\n");
		goto out_err;
	}

	/*
	 * Map the Hyp stack pages
	 */
	for_each_possible_cpu(cpu) {
		struct kvm_nvhe_init_params *params = per_cpu_ptr_nvhe_sym(kvm_init_params, cpu);
		char *stack_base = (char *)per_cpu(kvm_arm_hyp_stack_base, cpu);

		err = create_hyp_stack(__pa(stack_base), &params->stack_hyp_va);
		if (err) {
			kvm_err("Cannot map hyp stack\n");
			goto out_err;
		}

		/*
		 * Save the stack PA in nvhe_init_params. This will be needed
		 * to recreate the stack mapping in protected nVHE mode.
		 * __hyp_pa() won't do the right thing there, since the stack
		 * has been mapped in the flexible private VA space.
		 */
		params->stack_pa = __pa(stack_base);
	}

	for_each_possible_cpu(cpu) {
		char *percpu_begin = (char *)kvm_nvhe_sym(kvm_arm_hyp_percpu_base)[cpu];
		char *percpu_end = percpu_begin + nvhe_percpu_size();

		/* Map Hyp percpu pages */
		err = create_hyp_mappings(percpu_begin, percpu_end, PAGE_HYP);
		if (err) {
			kvm_err("Cannot map hyp percpu region\n");
			goto out_err;
		}

		/* Prepare the CPU initialization parameters */
		cpu_prepare_hyp_mode(cpu, hyp_va_bits);
	}

	kvm_hyp_init_symbols();

	if (is_protected_kvm_enabled()) {
		if (IS_ENABLED(CONFIG_ARM64_PTR_AUTH_KERNEL) &&
		    cpus_have_final_cap(ARM64_HAS_ADDRESS_AUTH))
			pkvm_hyp_init_ptrauth();

		init_cpu_logical_map();

		if (!init_psci_relay()) {
			err = -ENODEV;
			goto out_err;
		}

		err = init_pkvm_host_sve_state();
		if (err)
			goto out_err;

		err = kvm_hyp_init_protection(hyp_va_bits);
		if (err) {
			kvm_err("Failed to init hyp memory protection\n");
			goto out_err;
		}
	}

	return 0;

out_err:
	teardown_hyp_mode();
	kvm_err("error initializing Hyp mode: %d\n", err);
	return err;
}

struct kvm_vcpu *kvm_mpidr_to_vcpu(struct kvm *kvm, unsigned long mpidr)
{
	struct kvm_vcpu *vcpu = NULL;
	struct kvm_mpidr_data *data;
	unsigned long i;

	mpidr &= MPIDR_HWID_BITMASK;

	rcu_read_lock();
	data = rcu_dereference(kvm->arch.mpidr_data);

	if (data) {
		u16 idx = kvm_mpidr_index(data, mpidr);

		vcpu = kvm_get_vcpu(kvm, data->cmpidr_to_idx[idx]);
		if (mpidr != kvm_vcpu_get_mpidr_aff(vcpu))
			vcpu = NULL;
	}

	rcu_read_unlock();

	if (vcpu)
		return vcpu;

	kvm_for_each_vcpu(i, vcpu, kvm) {
		if (mpidr == kvm_vcpu_get_mpidr_aff(vcpu))
			return vcpu;
	}
	return NULL;
}

bool kvm_arch_irqchip_in_kernel(struct kvm *kvm)
{
	return irqchip_in_kernel(kvm);
}

int kvm_arch_irq_bypass_add_producer(struct irq_bypass_consumer *cons,
				      struct irq_bypass_producer *prod)
{
	struct kvm_kernel_irqfd *irqfd =
		container_of(cons, struct kvm_kernel_irqfd, consumer);
	struct kvm_kernel_irq_routing_entry *irq_entry = &irqfd->irq_entry;

	/*
	 * The only thing we have a chance of directly-injecting is LPIs. Maybe
	 * one day...
	 */
	if (irq_entry->type != KVM_IRQ_ROUTING_MSI)
		return 0;

	return kvm_vgic_v4_set_forwarding(irqfd->kvm, prod->irq,
					  &irqfd->irq_entry);
}

void kvm_arch_irq_bypass_del_producer(struct irq_bypass_consumer *cons,
				      struct irq_bypass_producer *prod)
{
	struct kvm_kernel_irqfd *irqfd =
		container_of(cons, struct kvm_kernel_irqfd, consumer);
	struct kvm_kernel_irq_routing_entry *irq_entry = &irqfd->irq_entry;

	if (irq_entry->type != KVM_IRQ_ROUTING_MSI)
		return;

	kvm_vgic_v4_unset_forwarding(irqfd->kvm, prod->irq);
}

bool kvm_arch_irqfd_route_changed(struct kvm_kernel_irq_routing_entry *old,
				  struct kvm_kernel_irq_routing_entry *new)
{
	if (old->type != KVM_IRQ_ROUTING_MSI ||
	    new->type != KVM_IRQ_ROUTING_MSI)
		return true;

	return memcmp(&old->msi, &new->msi, sizeof(new->msi));
}

int kvm_arch_update_irqfd_routing(struct kvm *kvm, unsigned int host_irq,
				  uint32_t guest_irq, bool set)
{
	/*
	 * Remapping the vLPI requires taking the its_lock mutex to resolve
	 * the new translation. We're in spinlock land at this point, so no
	 * chance of resolving the translation.
	 *
	 * Unmap the vLPI and fall back to software LPI injection.
	 */
	return kvm_vgic_v4_unset_forwarding(kvm, host_irq);
}

void kvm_arch_irq_bypass_stop(struct irq_bypass_consumer *cons)
{
	struct kvm_kernel_irqfd *irqfd =
		container_of(cons, struct kvm_kernel_irqfd, consumer);

	kvm_arm_halt_guest(irqfd->kvm);
}

void kvm_arch_irq_bypass_start(struct irq_bypass_consumer *cons)
{
	struct kvm_kernel_irqfd *irqfd =
		container_of(cons, struct kvm_kernel_irqfd, consumer);

	kvm_arm_resume_guest(irqfd->kvm);
}

/* Initialize Hyp-mode and memory mappings on all CPUs */
static __init int kvm_arm_init(void)
{
	int err;
	bool in_hyp_mode;

	if (!is_hyp_mode_available()) {
		kvm_info("HYP mode not available\n");
		return -ENODEV;
	}

	if (kvm_get_mode() == KVM_MODE_NONE) {
		kvm_info("KVM disabled from command line\n");
		return -ENODEV;
	}

	err = kvm_sys_reg_table_init();
	if (err) {
		kvm_info("Error initializing system register tables");
		return err;
	}

	in_hyp_mode = is_kernel_in_hyp_mode();

	if (cpus_have_final_cap(ARM64_WORKAROUND_DEVICE_LOAD_ACQUIRE) ||
	    cpus_have_final_cap(ARM64_WORKAROUND_1508412))
		kvm_info("Guests without required CPU erratum workarounds can deadlock system!\n" \
			 "Only trusted guests should be used on this system.\n");

	err = kvm_set_ipa_limit();
	if (err)
		return err;

	err = kvm_arm_init_sve();
	if (err)
		return err;

	err = kvm_arm_vmid_alloc_init();
	if (err) {
		kvm_err("Failed to initialize VMID allocator.\n");
		return err;
	}

	if (!in_hyp_mode) {
		err = init_hyp_mode();
		if (err)
			goto out_err;
	}

	err = kvm_init_vector_slots();
	if (err) {
		kvm_err("Cannot initialise vector slots\n");
		goto out_hyp;
	}

	err = init_subsystems();
	if (err)
		goto out_hyp;

	kvm_info("%s%sVHE%s mode initialized successfully\n",
		 in_hyp_mode ? "" : (is_protected_kvm_enabled() ?
				     "Protected " : "Hyp "),
		 in_hyp_mode ? "" : (cpus_have_final_cap(ARM64_KVM_HVHE) ?
				     "h" : "n"),
		 cpus_have_final_cap(ARM64_HAS_NESTED_VIRT) ? "+NV2": "");

	/*
	 * FIXME: Do something reasonable if kvm_init() fails after pKVM
	 * hypervisor protection is finalized.
	 */
	err = kvm_init(sizeof(struct kvm_vcpu), 0, THIS_MODULE);
	if (err)
		goto out_subs;

	/*
	 * This should be called after initialization is done and failure isn't
	 * possible anymore.
	 */
	if (!in_hyp_mode)
		finalize_init_hyp_mode();

	kvm_arm_initialised = true;

	return 0;

out_subs:
	teardown_subsystems();
out_hyp:
	if (!in_hyp_mode)
		teardown_hyp_mode();
out_err:
	kvm_arm_vmid_alloc_free();
	return err;
}

static int __init early_kvm_mode_cfg(char *arg)
{
	if (!arg)
		return -EINVAL;

	if (strcmp(arg, "none") == 0) {
		kvm_mode = KVM_MODE_NONE;
		return 0;
	}

	if (!is_hyp_mode_available()) {
		pr_warn_once("KVM is not available. Ignoring kvm-arm.mode\n");
		return 0;
	}

	if (strcmp(arg, "protected") == 0) {
		if (!is_kernel_in_hyp_mode())
			kvm_mode = KVM_MODE_PROTECTED;
		else
			pr_warn_once("Protected KVM not available with VHE\n");

		return 0;
	}

	if (strcmp(arg, "nvhe") == 0 && !WARN_ON(is_kernel_in_hyp_mode())) {
		kvm_mode = KVM_MODE_DEFAULT;
		return 0;
	}

	if (strcmp(arg, "nested") == 0 && !WARN_ON(!is_kernel_in_hyp_mode())) {
		kvm_mode = KVM_MODE_NV;
		return 0;
	}

	return -EINVAL;
}
early_param("kvm-arm.mode", early_kvm_mode_cfg);

static int __init early_kvm_wfx_trap_policy_cfg(char *arg, enum kvm_wfx_trap_policy *p)
{
	if (!arg)
		return -EINVAL;

	if (strcmp(arg, "trap") == 0) {
		*p = KVM_WFX_TRAP;
		return 0;
	}

	if (strcmp(arg, "notrap") == 0) {
		*p = KVM_WFX_NOTRAP;
		return 0;
	}

	return -EINVAL;
}

static int __init early_kvm_wfi_trap_policy_cfg(char *arg)
{
	return early_kvm_wfx_trap_policy_cfg(arg, &kvm_wfi_trap_policy);
}
early_param("kvm-arm.wfi_trap_policy", early_kvm_wfi_trap_policy_cfg);

static int __init early_kvm_wfe_trap_policy_cfg(char *arg)
{
	return early_kvm_wfx_trap_policy_cfg(arg, &kvm_wfe_trap_policy);
}
early_param("kvm-arm.wfe_trap_policy", early_kvm_wfe_trap_policy_cfg);

enum kvm_mode kvm_get_mode(void)
{
	return kvm_mode;
}

module_init(kvm_arm_init);
