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
#include <asm/kvm_mmu.h>
#include <asm/kvm_pkvm.h>
#include <asm/kvm_emulate.h>
#include <asm/sections.h>

#include <kvm/arm_hypercalls.h>
#include <kvm/arm_pmu.h>
#include <kvm/arm_psci.h>

static enum kvm_mode kvm_mode = KVM_MODE_DEFAULT;

DECLARE_KVM_HYP_PER_CPU(unsigned long, kvm_hyp_vector);

DEFINE_PER_CPU(unsigned long, kvm_arm_hyp_stack_page);
DECLARE_KVM_NVHE_PER_CPU(struct kvm_nvhe_init_params, kvm_init_params);

DECLARE_KVM_NVHE_PER_CPU(struct kvm_cpu_context, kvm_hyp_ctxt);

static bool vgic_present;

static DEFINE_PER_CPU(unsigned char, kvm_arm_hardware_enabled);
DEFINE_STATIC_KEY_FALSE(userspace_irqchip_in_use);

int kvm_arch_vcpu_should_kick(struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_exiting_guest_mode(vcpu) == IN_GUEST_MODE;
}

int kvm_vm_ioctl_enable_cap(struct kvm *kvm,
			    struct kvm_enable_cap *cap)
{
	int r;
	u64 new_cap;

	if (cap->flags)
		return -EINVAL;

	switch (cap->cap) {
	case KVM_CAP_ARM_NISV_TO_USER:
		r = 0;
		set_bit(KVM_ARCH_FLAG_RETURN_NISV_IO_ABORT_TO_USER,
			&kvm->arch.flags);
		break;
	case KVM_CAP_ARM_MTE:
		mutex_lock(&kvm->lock);
		if (!system_supports_mte() || kvm->created_vcpus) {
			r = -EINVAL;
		} else {
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
		new_cap = cap->args[0];

		mutex_lock(&kvm->slots_lock);
		/*
		 * To keep things simple, allow changing the chunk
		 * size only when no memory slots have been created.
		 */
		if (!kvm_are_all_memslots_empty(kvm)) {
			r = -EINVAL;
		} else if (new_cap && !kvm_is_block_size_supported(new_cap)) {
			r = -EINVAL;
		} else {
			r = 0;
			kvm->arch.mmu.split_page_chunk_size = new_cap;
		}
		mutex_unlock(&kvm->slots_lock);
		break;
	default:
		r = -EINVAL;
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

	kvm_destroy_vcpus(kvm);

	kvm_unshare_hyp(kvm, kvm + 1);

	kvm_arm_teardown_hypercalls(kvm);
}

int kvm_vm_ioctl_check_extension(struct kvm *kvm, long ext)
{
	int r;
	switch (ext) {
	case KVM_CAP_IRQCHIP:
		r = vgic_present;
		break;
	case KVM_CAP_IOEVENTFD:
	case KVM_CAP_DEVICE_CTRL:
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
		r = cpus_have_const_cap(ARM64_HAS_32BIT_EL1);
		break;
	case KVM_CAP_GUEST_DEBUG_HW_BPS:
		r = get_num_brps();
		break;
	case KVM_CAP_GUEST_DEBUG_HW_WPS:
		r = get_num_wrps();
		break;
	case KVM_CAP_ARM_PMU_V3:
		r = kvm_arm_support_pmu_v3();
		break;
	case KVM_CAP_ARM_INJECT_SERROR_ESR:
		r = cpus_have_const_cap(ARM64_HAS_RAS_EXTN);
		break;
	case KVM_CAP_ARM_VM_IPA_SIZE:
		r = get_kvm_ipa_limit();
		break;
	case KVM_CAP_ARM_SVE:
		r = system_supports_sve();
		break;
	case KVM_CAP_ARM_PTRAUTH_ADDRESS:
	case KVM_CAP_ARM_PTRAUTH_GENERIC:
		r = system_has_full_ptr_auth();
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
	bitmap_zero(vcpu->arch.features, KVM_VCPU_MAX_FEATURES);

	vcpu->arch.mmu_page_cache.gfp_zero = __GFP_ZERO;

	/*
	 * Default value for the FP state, will be overloaded at load
	 * time if we support FP (pretty likely)
	 */
	vcpu->arch.fp_state = FP_STATE_FREE;

	/* Set up the timer */
	kvm_timer_vcpu_init(vcpu);

	kvm_pmu_vcpu_init(vcpu);

	kvm_arm_reset_debug_ptr(vcpu);

	kvm_arm_pvtime_vcpu_init(&vcpu->arch);

	vcpu->arch.hw_mmu = &vcpu->kvm->arch.mmu;

	err = kvm_vgic_vcpu_init(vcpu);
	if (err)
		return err;

	return kvm_share_hyp(vcpu, vcpu + 1);
}

void kvm_arch_vcpu_postcreate(struct kvm_vcpu *vcpu)
{
}

void kvm_arch_vcpu_destroy(struct kvm_vcpu *vcpu)
{
	if (vcpu_has_run_once(vcpu) && unlikely(!irqchip_in_kernel(vcpu->kvm)))
		static_branch_dec(&userspace_irqchip_in_use);

	kvm_mmu_free_memory_cache(&vcpu->arch.mmu_page_cache);
	kvm_timer_vcpu_terminate(vcpu);
	kvm_pmu_vcpu_destroy(vcpu);

	kvm_arm_vcpu_destroy(vcpu);
}

void kvm_arch_vcpu_blocking(struct kvm_vcpu *vcpu)
{

}

void kvm_arch_vcpu_unblocking(struct kvm_vcpu *vcpu)
{

}

void kvm_arch_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	struct kvm_s2_mmu *mmu;
	int *last_ran;

	mmu = vcpu->arch.hw_mmu;
	last_ran = this_cpu_ptr(mmu->last_vcpu_ran);

	/*
	 * We guarantee that both TLBs and I-cache are private to each
	 * vcpu. If detecting that a vcpu from the same VM has
	 * previously run on the same physical CPU, call into the
	 * hypervisor code to nuke the relevant contexts.
	 *
	 * We might get preempted before the vCPU actually runs, but
	 * over-invalidation doesn't affect correctness.
	 */
	if (*last_ran != vcpu->vcpu_id) {
		kvm_call_hyp(__kvm_flush_cpu_context, mmu);
		*last_ran = vcpu->vcpu_id;
	}

	vcpu->cpu = cpu;

	kvm_vgic_load(vcpu);
	kvm_timer_vcpu_load(vcpu);
	if (has_vhe())
		kvm_vcpu_load_sysregs_vhe(vcpu);
	kvm_arch_vcpu_load_fp(vcpu);
	kvm_vcpu_pmu_restore_guest(vcpu);
	if (kvm_arm_is_pvtime_enabled(&vcpu->arch))
		kvm_make_request(KVM_REQ_RECORD_STEAL, vcpu);

	if (single_task_running())
		vcpu_clear_wfx_traps(vcpu);
	else
		vcpu_set_wfx_traps(vcpu);

	if (vcpu_has_ptrauth(vcpu))
		vcpu_ptrauth_disable(vcpu);
	kvm_arch_vcpu_load_debug_state_flags(vcpu);

	if (!cpumask_test_cpu(smp_processor_id(), vcpu->kvm->arch.supported_cpus))
		vcpu_set_on_unsupported_cpu(vcpu);
}

void kvm_arch_vcpu_put(struct kvm_vcpu *vcpu)
{
	kvm_arch_vcpu_put_debug_state_flags(vcpu);
	kvm_arch_vcpu_put_fp(vcpu);
	if (has_vhe())
		kvm_vcpu_put_sysregs_vhe(vcpu);
	kvm_timer_vcpu_put(vcpu);
	kvm_vgic_put(vcpu);
	kvm_vcpu_pmu_restore_host(vcpu);
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

static int kvm_vcpu_initialized(struct kvm_vcpu *vcpu)
{
	return vcpu_get_flag(vcpu, VCPU_INITIALIZED);
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

	kvm_arm_vcpu_init_debug(vcpu);

	if (likely(irqchip_in_kernel(kvm))) {
		/*
		 * Map the VGIC hardware resources before running a vcpu the
		 * first time on this VM.
		 */
		ret = kvm_vgic_map_resources(kvm);
		if (ret)
			return ret;
	}

	ret = kvm_timer_enable(vcpu);
	if (ret)
		return ret;

	ret = kvm_arm_pmu_v3_enable(vcpu);
	if (ret)
		return ret;

	if (is_protected_kvm_enabled()) {
		ret = pkvm_create_hyp_vm(kvm);
		if (ret)
			return ret;
	}

	if (!irqchip_in_kernel(kvm)) {
		/*
		 * Tell the rest of the code that there are userspace irqchip
		 * VMs in the wild.
		 */
		static_branch_inc(&userspace_irqchip_in_use);
	}

	/*
	 * Initialize traps for protected VMs.
	 * NOTE: Move to run in EL2 directly, rather than via a hypercall, once
	 * the code is in place for first run initialization at EL2.
	 */
	if (kvm_vm_is_protected(kvm))
		kvm_call_hyp_nvhe(__pkvm_vcpu_init_traps, vcpu);

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
	kvm_vgic_vmcr_sync(vcpu);
	vgic_v4_put(vcpu, true);
	preempt_enable();

	kvm_vcpu_halt(vcpu);
	vcpu_clear_flag(vcpu, IN_WFIT);

	preempt_disable();
	vgic_v4_load(vcpu);
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
			vgic_v4_put(vcpu, false);
			vgic_v4_load(vcpu);
			preempt_enable();
		}

		if (kvm_check_request(KVM_REQ_RELOAD_PMU, vcpu))
			kvm_pmu_handle_pmcr(vcpu,
					    __vcpu_sys_reg(vcpu, PMCR_EL0));

		if (kvm_check_request(KVM_REQ_SUSPEND, vcpu))
			return kvm_vcpu_suspend(vcpu);

		if (kvm_dirty_ring_check_request(vcpu))
			return 0;
	}

	return 1;
}

static bool vcpu_mode_is_bad_32bit(struct kvm_vcpu *vcpu)
{
	if (likely(!vcpu_mode_is_32bit(vcpu)))
		return false;

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
	if (static_branch_unlikely(&userspace_irqchip_in_use)) {
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
		if (ret)
			return ret;
	}

	vcpu_load(vcpu);

	if (run->immediate_exit) {
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

		/*
		 * The VMID allocator only tracks active VMIDs per
		 * physical CPU, and therefore the VMID allocated may not be
		 * preserved on VMID roll-over if the task was preempted,
		 * making a thread's VMID inactive. So we need to call
		 * kvm_arm_vmid_update() in non-premptible context.
		 */
		kvm_arm_vmid_update(&vcpu->arch.hw_mmu->vmid);

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
			kvm_pmu_sync_hwstate(vcpu);
			if (static_branch_unlikely(&userspace_irqchip_in_use))
				kvm_timer_sync_user(vcpu);
			kvm_vgic_sync_hwstate(vcpu);
			local_irq_enable();
			preempt_enable();
			continue;
		}

		kvm_arm_setup_debug(vcpu);
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

		kvm_arm_clear_debug(vcpu);

		/*
		 * We must sync the PMU state before the vgic state so
		 * that the vgic can properly sample the updated state of the
		 * interrupt line.
		 */
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
		if (static_branch_unlikely(&userspace_irqchip_in_use))
			kvm_timer_sync_user(vcpu);

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
	unsigned int irq_type, vcpu_idx, irq_num;
	int nrcpus = atomic_read(&kvm->online_vcpus);
	struct kvm_vcpu *vcpu = NULL;
	bool level = irq_level->level;

	irq_type = (irq >> KVM_ARM_IRQ_TYPE_SHIFT) & KVM_ARM_IRQ_TYPE_MASK;
	vcpu_idx = (irq >> KVM_ARM_IRQ_VCPU_SHIFT) & KVM_ARM_IRQ_VCPU_MASK;
	vcpu_idx += ((irq >> KVM_ARM_IRQ_VCPU2_SHIFT) & KVM_ARM_IRQ_VCPU2_MASK) * (KVM_ARM_IRQ_VCPU_MASK + 1);
	irq_num = (irq >> KVM_ARM_IRQ_NUM_SHIFT) & KVM_ARM_IRQ_NUM_MASK;

	trace_kvm_irq_line(irq_type, vcpu_idx, irq_num, irq_level->level);

	switch (irq_type) {
	case KVM_ARM_IRQ_TYPE_CPU:
		if (irqchip_in_kernel(kvm))
			return -ENXIO;

		if (vcpu_idx >= nrcpus)
			return -EINVAL;

		vcpu = kvm_get_vcpu(kvm, vcpu_idx);
		if (!vcpu)
			return -EINVAL;

		if (irq_num > KVM_ARM_IRQ_CPU_FIQ)
			return -EINVAL;

		return vcpu_interrupt_line(vcpu, irq_num, level);
	case KVM_ARM_IRQ_TYPE_PPI:
		if (!irqchip_in_kernel(kvm))
			return -ENXIO;

		if (vcpu_idx >= nrcpus)
			return -EINVAL;

		vcpu = kvm_get_vcpu(kvm, vcpu_idx);
		if (!vcpu)
			return -EINVAL;

		if (irq_num < VGIC_NR_SGIS || irq_num >= VGIC_NR_PRIVATE_IRQS)
			return -EINVAL;

		return kvm_vgic_inject_irq(kvm, vcpu->vcpu_id, irq_num, level, NULL);
	case KVM_ARM_IRQ_TYPE_SPI:
		if (!irqchip_in_kernel(kvm))
			return -ENXIO;

		if (irq_num < VGIC_NR_PRIVATE_IRQS)
			return -EINVAL;

		return kvm_vgic_inject_irq(kvm, 0, irq_num, level, NULL);
	}

	return -EINVAL;
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

	if (!test_bit(KVM_ARM_VCPU_EL1_32BIT, &features))
		return 0;

	if (!cpus_have_const_cap(ARM64_HAS_32BIT_EL1))
		return -EINVAL;

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

	return !bitmap_equal(vcpu->arch.features, &features, KVM_VCPU_MAX_FEATURES);
}

static int __kvm_vcpu_set_target(struct kvm_vcpu *vcpu,
				 const struct kvm_vcpu_init *init)
{
	unsigned long features = init->features[0];
	struct kvm *kvm = vcpu->kvm;
	int ret = -EINVAL;

	mutex_lock(&kvm->arch.config_lock);

	if (test_bit(KVM_ARCH_FLAG_VCPU_FEATURES_CONFIGURED, &kvm->arch.flags) &&
	    !bitmap_equal(kvm->arch.vcpu_features, &features, KVM_VCPU_MAX_FEATURES))
		goto out_unlock;

	bitmap_copy(vcpu->arch.features, &features, KVM_VCPU_MAX_FEATURES);

	/* Now we know what it is, we can reset it. */
	ret = kvm_reset_vcpu(vcpu);
	if (ret) {
		bitmap_zero(vcpu->arch.features, KVM_VCPU_MAX_FEATURES);
		goto out_unlock;
	}

	bitmap_copy(kvm->arch.vcpu_features, &features, KVM_VCPU_MAX_FEATURES);
	set_bit(KVM_ARCH_FLAG_VCPU_FEATURES_CONFIGURED, &kvm->arch.flags);
	vcpu_set_flag(vcpu, VCPU_INITIALIZED);
out_unlock:
	mutex_unlock(&kvm->arch.config_lock);
	return ret;
}

static int kvm_vcpu_set_target(struct kvm_vcpu *vcpu,
			       const struct kvm_vcpu_init *init)
{
	int ret;

	if (init->target != kvm_target_cpu())
		return -EINVAL;

	ret = kvm_vcpu_init_check_features(vcpu, init);
	if (ret)
		return ret;

	if (!kvm_vcpu_initialized(vcpu))
		return __kvm_vcpu_set_target(vcpu, init);

	if (kvm_vcpu_init_changed(vcpu, init))
		return -EINVAL;

	return kvm_reset_vcpu(vcpu);
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
	vcpu->arch.cptr_el2 = kvm_get_reset_cptr_el2(vcpu);

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

void kvm_arch_flush_remote_tlbs_memslot(struct kvm *kvm,
					const struct kvm_memory_slot *memslot)
{
	kvm_flush_remote_tlbs(kvm);
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
		struct kvm_vcpu_init init;

		kvm_vcpu_preferred_target(&init);

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
	default:
		return -EINVAL;
	}
}

/* unlocks vcpus from @vcpu_lock_idx and smaller */
static void unlock_vcpus(struct kvm *kvm, int vcpu_lock_idx)
{
	struct kvm_vcpu *tmp_vcpu;

	for (; vcpu_lock_idx >= 0; vcpu_lock_idx--) {
		tmp_vcpu = kvm_get_vcpu(kvm, vcpu_lock_idx);
		mutex_unlock(&tmp_vcpu->mutex);
	}
}

void unlock_all_vcpus(struct kvm *kvm)
{
	lockdep_assert_held(&kvm->lock);

	unlock_vcpus(kvm, atomic_read(&kvm->online_vcpus) - 1);
}

/* Returns true if all vcpus were locked, false otherwise */
bool lock_all_vcpus(struct kvm *kvm)
{
	struct kvm_vcpu *tmp_vcpu;
	unsigned long c;

	lockdep_assert_held(&kvm->lock);

	/*
	 * Any time a vcpu is in an ioctl (including running), the
	 * core KVM code tries to grab the vcpu->mutex.
	 *
	 * By grabbing the vcpu->mutex of all VCPUs we ensure that no
	 * other VCPUs can fiddle with the state while we access it.
	 */
	kvm_for_each_vcpu(c, tmp_vcpu, kvm) {
		if (!mutex_trylock(&tmp_vcpu->mutex)) {
			unlock_vcpus(kvm, c - 1);
			return false;
		}
	}

	return true;
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
		tcr |= TCR_EPD1_MASK;
	} else {
		tcr &= TCR_EL2_MASK;
		tcr |= TCR_EL2_RES1;
	}
	tcr &= ~TCR_T0SZ_MASK;
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
	 * cpus_have_const_cap() wrapper.
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
	kvm_init_host_cpu_context(&this_cpu_ptr_hyp_sym(kvm_host_data)->host_ctxt);

	if (!is_kernel_in_hyp_mode())
		cpu_init_hyp_mode();
}

static void cpu_hyp_init_features(void)
{
	cpu_set_hyp_vector();
	kvm_arm_init_debug();

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

static void _kvm_arch_hardware_enable(void *discard)
{
	if (!__this_cpu_read(kvm_arm_hardware_enabled)) {
		cpu_hyp_reinit();
		__this_cpu_write(kvm_arm_hardware_enabled, 1);
	}
}

int kvm_arch_hardware_enable(void)
{
	int was_enabled = __this_cpu_read(kvm_arm_hardware_enabled);

	_kvm_arch_hardware_enable(NULL);

	if (!was_enabled) {
		kvm_vgic_cpu_up();
		kvm_timer_cpu_up();
	}

	return 0;
}

static void _kvm_arch_hardware_disable(void *discard)
{
	if (__this_cpu_read(kvm_arm_hardware_enabled)) {
		cpu_hyp_reset();
		__this_cpu_write(kvm_arm_hardware_enabled, 0);
	}
}

void kvm_arch_hardware_disable(void)
{
	if (__this_cpu_read(kvm_arm_hardware_enabled)) {
		kvm_timer_cpu_down();
		kvm_vgic_cpu_down();
	}

	if (!is_protected_kvm_enabled())
		_kvm_arch_hardware_disable(NULL);
}

#ifdef CONFIG_CPU_PM
static int hyp_init_cpu_pm_notifier(struct notifier_block *self,
				    unsigned long cmd,
				    void *v)
{
	/*
	 * kvm_arm_hardware_enabled is left with its old value over
	 * PM_ENTER->PM_EXIT. It is used to indicate PM_EXIT should
	 * re-enable hyp.
	 */
	switch (cmd) {
	case CPU_PM_ENTER:
		if (__this_cpu_read(kvm_arm_hardware_enabled))
			/*
			 * don't update kvm_arm_hardware_enabled here
			 * so that the hardware will be re-enabled
			 * when we resume. See below.
			 */
			cpu_hyp_reset();

		return NOTIFY_OK;
	case CPU_PM_ENTER_FAILED:
	case CPU_PM_EXIT:
		if (__this_cpu_read(kvm_arm_hardware_enabled))
			/* The hardware was enabled before suspend. */
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
	on_each_cpu(_kvm_arch_hardware_enable, NULL, 1);

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
		vgic_present = false;
		err = 0;
		break;
	default:
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
		on_each_cpu(_kvm_arch_hardware_disable, NULL, 1);

	return err;
}

static void __init teardown_subsystems(void)
{
	kvm_unregister_perf_callbacks();
	hyp_cpu_pm_exit();
}

static void __init teardown_hyp_mode(void)
{
	int cpu;

	free_hyp_pgds();
	for_each_possible_cpu(cpu) {
		free_page(per_cpu(kvm_arm_hyp_stack_page, cpu));
		free_pages(kvm_nvhe_sym(kvm_arm_hyp_percpu_base)[cpu], nvhe_percpu_order());
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
	 * prevent a later re-init attempt in kvm_arch_hardware_enable().
	 */
	__this_cpu_write(kvm_arm_hardware_enabled, 1);
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
		unsigned long stack_page;

		stack_page = __get_free_page(GFP_KERNEL);
		if (!stack_page) {
			err = -ENOMEM;
			goto out_err;
		}

		per_cpu(kvm_arm_hyp_stack_page, cpu) = stack_page;
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
		char *stack_page = (char *)per_cpu(kvm_arm_hyp_stack_page, cpu);
		unsigned long hyp_addr;

		/*
		 * Allocate a contiguous HYP private VA range for the stack
		 * and guard page. The allocation is also aligned based on
		 * the order of its size.
		 */
		err = hyp_alloc_private_va_range(PAGE_SIZE * 2, &hyp_addr);
		if (err) {
			kvm_err("Cannot allocate hyp stack guard page\n");
			goto out_err;
		}

		/*
		 * Since the stack grows downwards, map the stack to the page
		 * at the higher address and leave the lower guard page
		 * unbacked.
		 *
		 * Any valid stack address now has the PAGE_SHIFT bit as 1
		 * and addresses corresponding to the guard page have the
		 * PAGE_SHIFT bit as 0 - this is used for overflow detection.
		 */
		err = __create_hyp_mappings(hyp_addr + PAGE_SIZE, PAGE_SIZE,
					    __pa(stack_page), PAGE_HYP);
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
		params->stack_pa = __pa(stack_page);

		params->stack_hyp_va = hyp_addr + (2 * PAGE_SIZE);
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
		    cpus_have_const_cap(ARM64_HAS_ADDRESS_AUTH))
			pkvm_hyp_init_ptrauth();

		init_cpu_logical_map();

		if (!init_psci_relay()) {
			err = -ENODEV;
			goto out_err;
		}

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
	struct kvm_vcpu *vcpu;
	unsigned long i;

	mpidr &= MPIDR_HWID_BITMASK;
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

bool kvm_arch_has_irq_bypass(void)
{
	return true;
}

int kvm_arch_irq_bypass_add_producer(struct irq_bypass_consumer *cons,
				      struct irq_bypass_producer *prod)
{
	struct kvm_kernel_irqfd *irqfd =
		container_of(cons, struct kvm_kernel_irqfd, consumer);

	return kvm_vgic_v4_set_forwarding(irqfd->kvm, prod->irq,
					  &irqfd->irq_entry);
}
void kvm_arch_irq_bypass_del_producer(struct irq_bypass_consumer *cons,
				      struct irq_bypass_producer *prod)
{
	struct kvm_kernel_irqfd *irqfd =
		container_of(cons, struct kvm_kernel_irqfd, consumer);

	kvm_vgic_v4_unset_forwarding(irqfd->kvm, prod->irq,
				     &irqfd->irq_entry);
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

	if (is_protected_kvm_enabled()) {
		kvm_info("Protected nVHE mode initialized successfully\n");
	} else if (in_hyp_mode) {
		kvm_info("VHE mode initialized successfully\n");
	} else {
		kvm_info("Hyp mode initialized successfully\n");
	}

	/*
	 * FIXME: Do something reasonable if kvm_init() fails after pKVM
	 * hypervisor protection is finalized.
	 */
	err = kvm_init(sizeof(struct kvm_vcpu), 0, THIS_MODULE);
	if (err)
		goto out_subs;

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

enum kvm_mode kvm_get_mode(void)
{
	return kvm_mode;
}

module_init(kvm_arm_init);
