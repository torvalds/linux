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
#include <linux/kmemleak.h>
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
#include <asm/kvm_emulate.h>
#include <asm/sections.h>

#include <kvm/arm_hypercalls.h>
#include <kvm/arm_pmu.h>
#include <kvm/arm_psci.h>

static enum kvm_mode kvm_mode = KVM_MODE_DEFAULT;
DEFINE_STATIC_KEY_FALSE(kvm_protected_mode_initialized);

DECLARE_KVM_HYP_PER_CPU(unsigned long, kvm_hyp_vector);

static DEFINE_PER_CPU(unsigned long, kvm_arm_hyp_stack_page);
unsigned long kvm_arm_hyp_percpu_base[NR_CPUS];
DECLARE_KVM_NVHE_PER_CPU(struct kvm_nvhe_init_params, kvm_init_params);

/* The VMID used in the VTTBR */
static atomic64_t kvm_vmid_gen = ATOMIC64_INIT(1);
static u32 kvm_next_vmid;
static DEFINE_SPINLOCK(kvm_vmid_lock);

static bool vgic_present;

static DEFINE_PER_CPU(unsigned char, kvm_arm_hardware_enabled);
DEFINE_STATIC_KEY_FALSE(userspace_irqchip_in_use);

int kvm_arch_vcpu_should_kick(struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_exiting_guest_mode(vcpu) == IN_GUEST_MODE;
}

int kvm_arch_hardware_setup(void *opaque)
{
	return 0;
}

int kvm_arch_check_processor_compat(void *opaque)
{
	return 0;
}

int kvm_vm_ioctl_enable_cap(struct kvm *kvm,
			    struct kvm_enable_cap *cap)
{
	int r;

	if (cap->flags)
		return -EINVAL;

	switch (cap->cap) {
	case KVM_CAP_ARM_NISV_TO_USER:
		r = 0;
		kvm->arch.return_nisv_io_abort_to_user = true;
		break;
	case KVM_CAP_ARM_MTE:
		mutex_lock(&kvm->lock);
		if (!system_supports_mte() || kvm->created_vcpus) {
			r = -EINVAL;
		} else {
			r = 0;
			kvm->arch.mte_enabled = true;
		}
		mutex_unlock(&kvm->lock);
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

static void set_default_spectre(struct kvm *kvm)
{
	/*
	 * The default is to expose CSV2 == 1 if the HW isn't affected.
	 * Although this is a per-CPU feature, we make it global because
	 * asymmetric systems are just a nuisance.
	 *
	 * Userspace can override this as long as it doesn't promise
	 * the impossible.
	 */
	if (arm64_get_spectre_v2_state() == SPECTRE_UNAFFECTED)
		kvm->arch.pfr0_csv2 = 1;
	if (arm64_get_meltdown_state() == SPECTRE_UNAFFECTED)
		kvm->arch.pfr0_csv3 = 1;
}

/**
 * kvm_arch_init_vm - initializes a VM data structure
 * @kvm:	pointer to the KVM struct
 */
int kvm_arch_init_vm(struct kvm *kvm, unsigned long type)
{
	int ret;

	ret = kvm_arm_setup_stage2(kvm, type);
	if (ret)
		return ret;

	ret = kvm_init_stage2_mmu(kvm, &kvm->arch.mmu);
	if (ret)
		return ret;

	ret = create_hyp_mappings(kvm, kvm + 1, PAGE_HYP);
	if (ret)
		goto out_free_stage2_pgd;

	kvm_vgic_early_init(kvm);

	/* The maximum number of VCPUs is limited by the host's GIC model */
	kvm->arch.max_vcpus = kvm_arm_default_max_vcpus();

	set_default_spectre(kvm);

	return ret;
out_free_stage2_pgd:
	kvm_free_stage2_pgd(&kvm->arch.mmu);
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
	int i;

	bitmap_free(kvm->arch.pmu_filter);

	kvm_vgic_destroy(kvm);

	for (i = 0; i < KVM_MAX_VCPUS; ++i) {
		if (kvm->vcpus[i]) {
			kvm_vcpu_destroy(kvm->vcpus[i]);
			kvm->vcpus[i] = NULL;
		}
	}
	atomic_set(&kvm->online_vcpus, 0);
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
		r = 1;
		break;
	case KVM_CAP_SET_GUEST_DEBUG2:
		return KVM_GUESTDBG_VALID_MASK;
	case KVM_CAP_ARM_SET_DEVICE_ADDR:
		r = 1;
		break;
	case KVM_CAP_NR_VCPUS:
		r = num_online_cpus();
		break;
	case KVM_CAP_MAX_VCPUS:
	case KVM_CAP_MAX_VCPU_ID:
		if (kvm)
			r = kvm->arch.max_vcpus;
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
	if (!has_vhe())
		return kzalloc(sizeof(struct kvm), GFP_KERNEL);

	return vzalloc(sizeof(struct kvm));
}

void kvm_arch_free_vm(struct kvm *kvm)
{
	if (!has_vhe())
		kfree(kvm);
	else
		vfree(kvm);
}

int kvm_arch_vcpu_precreate(struct kvm *kvm, unsigned int id)
{
	if (irqchip_in_kernel(kvm) && vgic_initialized(kvm))
		return -EBUSY;

	if (id >= kvm->arch.max_vcpus)
		return -EINVAL;

	return 0;
}

int kvm_arch_vcpu_create(struct kvm_vcpu *vcpu)
{
	int err;

	/* Force users to call KVM_ARM_VCPU_INIT */
	vcpu->arch.target = -1;
	bitmap_zero(vcpu->arch.features, KVM_VCPU_MAX_FEATURES);

	vcpu->arch.mmu_page_cache.gfp_zero = __GFP_ZERO;

	/* Set up the timer */
	kvm_timer_vcpu_init(vcpu);

	kvm_pmu_vcpu_init(vcpu);

	kvm_arm_reset_debug_ptr(vcpu);

	kvm_arm_pvtime_vcpu_init(&vcpu->arch);

	vcpu->arch.hw_mmu = &vcpu->kvm->arch.mmu;

	err = kvm_vgic_vcpu_init(vcpu);
	if (err)
		return err;

	return create_hyp_mappings(vcpu, vcpu + 1, PAGE_HYP);
}

void kvm_arch_vcpu_postcreate(struct kvm_vcpu *vcpu)
{
}

void kvm_arch_vcpu_destroy(struct kvm_vcpu *vcpu)
{
	if (vcpu->arch.has_run_once && unlikely(!irqchip_in_kernel(vcpu->kvm)))
		static_branch_dec(&userspace_irqchip_in_use);

	kvm_mmu_free_memory_cache(&vcpu->arch.mmu_page_cache);
	kvm_timer_vcpu_terminate(vcpu);
	kvm_pmu_vcpu_destroy(vcpu);

	kvm_arm_vcpu_destroy(vcpu);
}

int kvm_cpu_has_pending_timer(struct kvm_vcpu *vcpu)
{
	return kvm_timer_is_pending(vcpu);
}

void kvm_arch_vcpu_blocking(struct kvm_vcpu *vcpu)
{
	/*
	 * If we're about to block (most likely because we've just hit a
	 * WFI), we need to sync back the state of the GIC CPU interface
	 * so that we have the latest PMR and group enables. This ensures
	 * that kvm_arch_vcpu_runnable has up-to-date data to decide
	 * whether we have pending interrupts.
	 *
	 * For the same reason, we want to tell GICv4 that we need
	 * doorbells to be signalled, should an interrupt become pending.
	 */
	preempt_disable();
	kvm_vgic_vmcr_sync(vcpu);
	vgic_v4_put(vcpu, true);
	preempt_enable();
}

void kvm_arch_vcpu_unblocking(struct kvm_vcpu *vcpu)
{
	preempt_disable();
	vgic_v4_load(vcpu);
	preempt_enable();
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

	vcpu->cpu = -1;
}

static void vcpu_power_off(struct kvm_vcpu *vcpu)
{
	vcpu->arch.power_off = true;
	kvm_make_request(KVM_REQ_SLEEP, vcpu);
	kvm_vcpu_kick(vcpu);
}

int kvm_arch_vcpu_ioctl_get_mpstate(struct kvm_vcpu *vcpu,
				    struct kvm_mp_state *mp_state)
{
	if (vcpu->arch.power_off)
		mp_state->mp_state = KVM_MP_STATE_STOPPED;
	else
		mp_state->mp_state = KVM_MP_STATE_RUNNABLE;

	return 0;
}

int kvm_arch_vcpu_ioctl_set_mpstate(struct kvm_vcpu *vcpu,
				    struct kvm_mp_state *mp_state)
{
	int ret = 0;

	switch (mp_state->mp_state) {
	case KVM_MP_STATE_RUNNABLE:
		vcpu->arch.power_off = false;
		break;
	case KVM_MP_STATE_STOPPED:
		vcpu_power_off(vcpu);
		break;
	default:
		ret = -EINVAL;
	}

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
		&& !v->arch.power_off && !v->arch.pause);
}

bool kvm_arch_vcpu_in_kernel(struct kvm_vcpu *vcpu)
{
	return vcpu_mode_priv(vcpu);
}

/* Just ensure a guest exit from a particular CPU */
static void exit_vm_noop(void *info)
{
}

void force_vm_exit(const cpumask_t *mask)
{
	preempt_disable();
	smp_call_function_many(mask, exit_vm_noop, NULL, true);
	preempt_enable();
}

/**
 * need_new_vmid_gen - check that the VMID is still valid
 * @vmid: The VMID to check
 *
 * return true if there is a new generation of VMIDs being used
 *
 * The hardware supports a limited set of values with the value zero reserved
 * for the host, so we check if an assigned value belongs to a previous
 * generation, which requires us to assign a new value. If we're the first to
 * use a VMID for the new generation, we must flush necessary caches and TLBs
 * on all CPUs.
 */
static bool need_new_vmid_gen(struct kvm_vmid *vmid)
{
	u64 current_vmid_gen = atomic64_read(&kvm_vmid_gen);
	smp_rmb(); /* Orders read of kvm_vmid_gen and kvm->arch.vmid */
	return unlikely(READ_ONCE(vmid->vmid_gen) != current_vmid_gen);
}

/**
 * update_vmid - Update the vmid with a valid VMID for the current generation
 * @vmid: The stage-2 VMID information struct
 */
static void update_vmid(struct kvm_vmid *vmid)
{
	if (!need_new_vmid_gen(vmid))
		return;

	spin_lock(&kvm_vmid_lock);

	/*
	 * We need to re-check the vmid_gen here to ensure that if another vcpu
	 * already allocated a valid vmid for this vm, then this vcpu should
	 * use the same vmid.
	 */
	if (!need_new_vmid_gen(vmid)) {
		spin_unlock(&kvm_vmid_lock);
		return;
	}

	/* First user of a new VMID generation? */
	if (unlikely(kvm_next_vmid == 0)) {
		atomic64_inc(&kvm_vmid_gen);
		kvm_next_vmid = 1;

		/*
		 * On SMP we know no other CPUs can use this CPU's or each
		 * other's VMID after force_vm_exit returns since the
		 * kvm_vmid_lock blocks them from reentry to the guest.
		 */
		force_vm_exit(cpu_all_mask);
		/*
		 * Now broadcast TLB + ICACHE invalidation over the inner
		 * shareable domain to make sure all data structures are
		 * clean.
		 */
		kvm_call_hyp(__kvm_flush_vm_context);
	}

	WRITE_ONCE(vmid->vmid, kvm_next_vmid);
	kvm_next_vmid++;
	kvm_next_vmid &= (1 << kvm_get_vmid_bits()) - 1;

	smp_wmb();
	WRITE_ONCE(vmid->vmid_gen, atomic64_read(&kvm_vmid_gen));

	spin_unlock(&kvm_vmid_lock);
}

static int kvm_vcpu_first_run_init(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;
	int ret = 0;

	if (likely(vcpu->arch.has_run_once))
		return 0;

	if (!kvm_arm_vcpu_is_finalized(vcpu))
		return -EPERM;

	vcpu->arch.has_run_once = true;

	kvm_arm_vcpu_init_debug(vcpu);

	if (likely(irqchip_in_kernel(kvm))) {
		/*
		 * Map the VGIC hardware resources before running a vcpu the
		 * first time on this VM.
		 */
		ret = kvm_vgic_map_resources(kvm);
		if (ret)
			return ret;
	} else {
		/*
		 * Tell the rest of the code that there are userspace irqchip
		 * VMs in the wild.
		 */
		static_branch_inc(&userspace_irqchip_in_use);
	}

	ret = kvm_timer_enable(vcpu);
	if (ret)
		return ret;

	ret = kvm_arm_pmu_v3_enable(vcpu);

	return ret;
}

bool kvm_arch_intc_initialized(struct kvm *kvm)
{
	return vgic_initialized(kvm);
}

void kvm_arm_halt_guest(struct kvm *kvm)
{
	int i;
	struct kvm_vcpu *vcpu;

	kvm_for_each_vcpu(i, vcpu, kvm)
		vcpu->arch.pause = true;
	kvm_make_all_cpus_request(kvm, KVM_REQ_SLEEP);
}

void kvm_arm_resume_guest(struct kvm *kvm)
{
	int i;
	struct kvm_vcpu *vcpu;

	kvm_for_each_vcpu(i, vcpu, kvm) {
		vcpu->arch.pause = false;
		rcuwait_wake_up(kvm_arch_vcpu_get_wait(vcpu));
	}
}

static void vcpu_req_sleep(struct kvm_vcpu *vcpu)
{
	struct rcuwait *wait = kvm_arch_vcpu_get_wait(vcpu);

	rcuwait_wait_event(wait,
			   (!vcpu->arch.power_off) &&(!vcpu->arch.pause),
			   TASK_INTERRUPTIBLE);

	if (vcpu->arch.power_off || vcpu->arch.pause) {
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

static int kvm_vcpu_initialized(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.target >= 0;
}

static void check_vcpu_requests(struct kvm_vcpu *vcpu)
{
	if (kvm_request_pending(vcpu)) {
		if (kvm_check_request(KVM_REQ_SLEEP, vcpu))
			vcpu_req_sleep(vcpu);

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
	}
}

static bool vcpu_mode_is_bad_32bit(struct kvm_vcpu *vcpu)
{
	if (likely(!vcpu_mode_is_32bit(vcpu)))
		return false;

	return !system_supports_32bit_el0() ||
		static_branch_unlikely(&arm64_mismatched_32bit_el0);
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

	return kvm_request_pending(vcpu) ||
			need_new_vmid_gen(&vcpu->arch.hw_mmu->vmid) ||
			xfer_to_guest_mode_work_pending();
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

	if (unlikely(!kvm_vcpu_initialized(vcpu)))
		return -ENOEXEC;

	ret = kvm_vcpu_first_run_init(vcpu);
	if (ret)
		return ret;

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
	while (ret > 0) {
		/*
		 * Check conditions before entering the guest
		 */
		ret = xfer_to_guest_mode_handle_work(vcpu);
		if (!ret)
			ret = 1;

		update_vmid(&vcpu->arch.hw_mmu->vmid);

		check_vcpu_requests(vcpu);

		/*
		 * Preparing the interrupts to be injected also
		 * involves poking the GIC, which must be done in a
		 * non-preemptible context.
		 */
		preempt_disable();

		kvm_pmu_flush_hwstate(vcpu);

		local_irq_disable();

		kvm_vgic_flush_hwstate(vcpu);

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

		/**************************************************************
		 * Enter the guest
		 */
		trace_kvm_entry(*vcpu_pc(vcpu));
		guest_enter_irqoff();

		ret = kvm_call_hyp_ret(__kvm_vcpu_run, vcpu);

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
		 * We may have taken a host interrupt in HYP mode (ie
		 * while executing the guest). This interrupt is still
		 * pending, as we haven't serviced it yet!
		 *
		 * We're now back in SVC mode, with interrupts
		 * disabled.  Enabling the interrupts now will have
		 * the effect of taking the interrupt again, in SVC
		 * mode this time.
		 */
		local_irq_enable();

		/*
		 * We do local_irq_enable() before calling guest_exit() so
		 * that if a timer interrupt hits while running the guest we
		 * account that tick as being spent in the guest.  We enable
		 * preemption after calling guest_exit() so that if we get
		 * preempted we make sure ticks after that is not counted as
		 * guest time.
		 */
		guest_exit();
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
			vcpu->arch.target = -1;
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
	if (unlikely(vcpu->arch.flags & (KVM_ARM64_PENDING_EXCEPTION |
					 KVM_ARM64_INCREMENT_PC)))
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

static int kvm_vcpu_set_target(struct kvm_vcpu *vcpu,
			       const struct kvm_vcpu_init *init)
{
	unsigned int i, ret;
	u32 phys_target = kvm_target_cpu();

	if (init->target != phys_target)
		return -EINVAL;

	/*
	 * Secondary and subsequent calls to KVM_ARM_VCPU_INIT must
	 * use the same target.
	 */
	if (vcpu->arch.target != -1 && vcpu->arch.target != init->target)
		return -EINVAL;

	/* -ENOENT for unknown features, -EINVAL for invalid combinations. */
	for (i = 0; i < sizeof(init->features) * 8; i++) {
		bool set = (init->features[i / 32] & (1 << (i % 32)));

		if (set && i >= KVM_VCPU_MAX_FEATURES)
			return -ENOENT;

		/*
		 * Secondary and subsequent calls to KVM_ARM_VCPU_INIT must
		 * use the same feature set.
		 */
		if (vcpu->arch.target != -1 && i < KVM_VCPU_MAX_FEATURES &&
		    test_bit(i, vcpu->arch.features) != set)
			return -EINVAL;

		if (set)
			set_bit(i, vcpu->arch.features);
	}

	vcpu->arch.target = phys_target;

	/* Now we know what it is, we can reset it. */
	ret = kvm_reset_vcpu(vcpu);
	if (ret) {
		vcpu->arch.target = -1;
		bitmap_zero(vcpu->arch.features, KVM_VCPU_MAX_FEATURES);
	}

	return ret;
}

static int kvm_arch_vcpu_ioctl_vcpu_init(struct kvm_vcpu *vcpu,
					 struct kvm_vcpu_init *init)
{
	int ret;

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
	if (vcpu->arch.has_run_once) {
		if (!cpus_have_final_cap(ARM64_HAS_STAGE2_FWB))
			stage2_unmap_vm(vcpu->kvm);
		else
			icache_inval_all_pou();
	}

	vcpu_reset_hcr(vcpu);
	vcpu->arch.cptr_el2 = CPTR_EL2_DEFAULT;

	/*
	 * Handle the "start in power-off" case.
	 */
	if (test_bit(KVM_ARM_VCPU_POWER_OFF, vcpu->arch.features))
		vcpu_power_off(vcpu);
	else
		vcpu->arch.power_off = false;

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
	unsigned long dev_id, type;

	dev_id = (dev_addr->id & KVM_ARM_DEVICE_ID_MASK) >>
		KVM_ARM_DEVICE_ID_SHIFT;
	type = (dev_addr->id & KVM_ARM_DEVICE_TYPE_MASK) >>
		KVM_ARM_DEVICE_TYPE_SHIFT;

	switch (dev_id) {
	case KVM_ARM_DEVICE_VGIC_V2:
		if (!vgic_present)
			return -ENXIO;
		return kvm_vgic_addr(kvm, type, &dev_addr->addr, true);
	default:
		return -ENODEV;
	}
}

long kvm_arch_vm_ioctl(struct file *filp,
		       unsigned int ioctl, unsigned long arg)
{
	struct kvm *kvm = filp->private_data;
	void __user *argp = (void __user *)arg;

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
		int err;
		struct kvm_vcpu_init init;

		err = kvm_vcpu_preferred_target(&init);
		if (err)
			return err;

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

	if (!cpus_have_const_cap(ARM64_SPECTRE_V3A))
		return 0;

	if (!has_vhe()) {
		err = create_hyp_exec_mappings(__pa_symbol(__bp_harden_hyp_vecs),
					       __BP_HARDEN_HYP_VECS_SZ, &base);
		if (err)
			return err;
	}

	kvm_init_vector_slot(base, HYP_VECTOR_INDIRECT);
	kvm_init_vector_slot(base, HYP_VECTOR_SPECTRE_INDIRECT);
	return 0;
}

static void cpu_prepare_hyp_mode(int cpu)
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

	/*
	 * The ID map may be configured to use an extended virtual address
	 * range. This is only the case if system RAM is out of range for the
	 * currently configured page size and VA_BITS, in which case we will
	 * also need the extended virtual range for the HYP ID map, or we won't
	 * be able to enable the EL2 MMU.
	 *
	 * However, at EL2, there is only one TTBR register, and we can't switch
	 * between translation tables *and* update TCR_EL2.T0SZ at the same
	 * time. Bottom line: we need to use the extended range with *both* our
	 * translation tables.
	 *
	 * So use the same T0SZ value we use for the ID map.
	 */
	tcr = (read_sysreg(tcr_el1) & TCR_EL2_MASK) | TCR_EL2_RES1;
	tcr &= ~TCR_T0SZ_MASK;
	tcr |= (idmap_t0sz & GENMASK(TCR_TxSZ_WIDTH - 1, 0)) << TCR_T0SZ_OFFSET;
	params->tcr_el2 = tcr;

	params->stack_hyp_va = kern_hyp_va(per_cpu(kvm_arm_hyp_stack_page, cpu) + PAGE_SIZE);
	params->pgd_pa = kvm_mmu_get_httbr();
	if (is_protected_kvm_enabled())
		params->hcr_el2 = HCR_HOST_NVHE_PROTECTED_FLAGS;
	else
		params->hcr_el2 = HCR_HOST_NVHE_FLAGS;
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

static void cpu_hyp_reinit(void)
{
	kvm_init_host_cpu_context(&this_cpu_ptr_hyp_sym(kvm_host_data)->host_ctxt);

	cpu_hyp_reset();

	if (is_kernel_in_hyp_mode())
		kvm_timer_init_vhe();
	else
		cpu_init_hyp_mode();

	cpu_set_hyp_vector();

	kvm_arm_init_debug();

	if (vgic_present)
		kvm_vgic_init_cpu_hardware();
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
	_kvm_arch_hardware_enable(NULL);
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

static void hyp_cpu_pm_init(void)
{
	if (!is_protected_kvm_enabled())
		cpu_pm_register_notifier(&hyp_init_cpu_pm_nb);
}
static void hyp_cpu_pm_exit(void)
{
	if (!is_protected_kvm_enabled())
		cpu_pm_unregister_notifier(&hyp_init_cpu_pm_nb);
}
#else
static inline void hyp_cpu_pm_init(void)
{
}
static inline void hyp_cpu_pm_exit(void)
{
}
#endif

static void init_cpu_logical_map(void)
{
	unsigned int cpu;

	/*
	 * Copy the MPIDR <-> logical CPU ID mapping to hyp.
	 * Only copy the set of online CPUs whose features have been chacked
	 * against the finalized system capabilities. The hypervisor will not
	 * allow any other CPUs from the `possible` set to boot.
	 */
	for_each_online_cpu(cpu)
		hyp_cpu_logical_map[cpu] = cpu_logical_map(cpu);
}

#define init_psci_0_1_impl_state(config, what)	\
	config.psci_0_1_ ## what ## _implemented = psci_ops.what

static bool init_psci_relay(void)
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

	if (kvm_host_psci_config.version == PSCI_VERSION(0, 1)) {
		kvm_host_psci_config.function_ids_0_1 = get_psci_0_1_function_ids();
		init_psci_0_1_impl_state(kvm_host_psci_config, cpu_suspend);
		init_psci_0_1_impl_state(kvm_host_psci_config, cpu_on);
		init_psci_0_1_impl_state(kvm_host_psci_config, cpu_off);
		init_psci_0_1_impl_state(kvm_host_psci_config, migrate);
	}
	return true;
}

static int init_subsystems(void)
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

	kvm_perf_init();
	kvm_sys_reg_table_init();

out:
	if (err || !is_protected_kvm_enabled())
		on_each_cpu(_kvm_arch_hardware_disable, NULL, 1);

	return err;
}

static void teardown_hyp_mode(void)
{
	int cpu;

	free_hyp_pgds();
	for_each_possible_cpu(cpu) {
		free_page(per_cpu(kvm_arm_hyp_stack_page, cpu));
		free_pages(kvm_arm_hyp_percpu_base[cpu], nvhe_percpu_order());
	}
}

static int do_pkvm_init(u32 hyp_va_bits)
{
	void *per_cpu_base = kvm_ksym_ref(kvm_arm_hyp_percpu_base);
	int ret;

	preempt_disable();
	hyp_install_host_vector();
	ret = kvm_call_hyp_nvhe(__pkvm_init, hyp_mem_base, hyp_mem_size,
				num_possible_cpus(), kern_hyp_va(per_cpu_base),
				hyp_va_bits);
	preempt_enable();

	return ret;
}

static int kvm_hyp_init_protection(u32 hyp_va_bits)
{
	void *addr = phys_to_virt(hyp_mem_base);
	int ret;

	kvm_nvhe_sym(id_aa64mmfr0_el1_sys_val) = read_sanitised_ftr_reg(SYS_ID_AA64MMFR0_EL1);
	kvm_nvhe_sym(id_aa64mmfr1_el1_sys_val) = read_sanitised_ftr_reg(SYS_ID_AA64MMFR1_EL1);

	ret = create_hyp_mappings(addr, addr + hyp_mem_size, PAGE_HYP);
	if (ret)
		return ret;

	ret = do_pkvm_init(hyp_va_bits);
	if (ret)
		return ret;

	free_hyp_pgds();

	return 0;
}

/**
 * Inits Hyp-mode on all online CPUs
 */
static int init_hyp_mode(void)
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
		kvm_arm_hyp_percpu_base[cpu] = (unsigned long)page_addr;
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
		char *stack_page = (char *)per_cpu(kvm_arm_hyp_stack_page, cpu);
		err = create_hyp_mappings(stack_page, stack_page + PAGE_SIZE,
					  PAGE_HYP);

		if (err) {
			kvm_err("Cannot map hyp stack\n");
			goto out_err;
		}
	}

	for_each_possible_cpu(cpu) {
		char *percpu_begin = (char *)kvm_arm_hyp_percpu_base[cpu];
		char *percpu_end = percpu_begin + nvhe_percpu_size();

		/* Map Hyp percpu pages */
		err = create_hyp_mappings(percpu_begin, percpu_end, PAGE_HYP);
		if (err) {
			kvm_err("Cannot map hyp percpu region\n");
			goto out_err;
		}

		/* Prepare the CPU initialization parameters */
		cpu_prepare_hyp_mode(cpu);
	}

	if (is_protected_kvm_enabled()) {
		init_cpu_logical_map();

		if (!init_psci_relay()) {
			err = -ENODEV;
			goto out_err;
		}
	}

	if (is_protected_kvm_enabled()) {
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

static void _kvm_host_prot_finalize(void *discard)
{
	WARN_ON(kvm_call_hyp_nvhe(__pkvm_prot_finalize));
}

static int finalize_hyp_mode(void)
{
	if (!is_protected_kvm_enabled())
		return 0;

	/*
	 * Exclude HYP BSS from kmemleak so that it doesn't get peeked
	 * at, which would end badly once the section is inaccessible.
	 * None of other sections should ever be introspected.
	 */
	kmemleak_free_part(__hyp_bss_start, __hyp_bss_end - __hyp_bss_start);

	/*
	 * Flip the static key upfront as that may no longer be possible
	 * once the host stage 2 is installed.
	 */
	static_branch_enable(&kvm_protected_mode_initialized);
	on_each_cpu(_kvm_host_prot_finalize, NULL, 1);

	return 0;
}

struct kvm_vcpu *kvm_mpidr_to_vcpu(struct kvm *kvm, unsigned long mpidr)
{
	struct kvm_vcpu *vcpu;
	int i;

	mpidr &= MPIDR_HWID_BITMASK;
	kvm_for_each_vcpu(i, vcpu, kvm) {
		if (mpidr == kvm_vcpu_get_mpidr_aff(vcpu))
			return vcpu;
	}
	return NULL;
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

/**
 * Initialize Hyp-mode and memory mappings on all CPUs.
 */
int kvm_arch_init(void *opaque)
{
	int err;
	bool in_hyp_mode;

	if (!is_hyp_mode_available()) {
		kvm_info("HYP mode not available\n");
		return -ENODEV;
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

	if (!in_hyp_mode) {
		err = init_hyp_mode();
		if (err)
			goto out_err;
	}

	err = kvm_init_vector_slots();
	if (err) {
		kvm_err("Cannot initialise vector slots\n");
		goto out_err;
	}

	err = init_subsystems();
	if (err)
		goto out_hyp;

	if (!in_hyp_mode) {
		err = finalize_hyp_mode();
		if (err) {
			kvm_err("Failed to finalize Hyp protection\n");
			goto out_hyp;
		}
	}

	if (is_protected_kvm_enabled()) {
		kvm_info("Protected nVHE mode initialized successfully\n");
	} else if (in_hyp_mode) {
		kvm_info("VHE mode initialized successfully\n");
	} else {
		kvm_info("Hyp mode initialized successfully\n");
	}

	return 0;

out_hyp:
	hyp_cpu_pm_exit();
	if (!in_hyp_mode)
		teardown_hyp_mode();
out_err:
	return err;
}

/* NOP: Compiling as a module not supported */
void kvm_arch_exit(void)
{
	kvm_perf_teardown();
}

static int __init early_kvm_mode_cfg(char *arg)
{
	if (!arg)
		return -EINVAL;

	if (strcmp(arg, "protected") == 0) {
		kvm_mode = KVM_MODE_PROTECTED;
		return 0;
	}

	if (strcmp(arg, "nvhe") == 0 && !WARN_ON(is_kernel_in_hyp_mode()))
		return 0;

	return -EINVAL;
}
early_param("kvm-arm.mode", early_kvm_mode_cfg);

enum kvm_mode kvm_get_mode(void)
{
	return kvm_mode;
}

static int arm_init(void)
{
	int rc = kvm_init(NULL, sizeof(struct kvm_vcpu), 0, THIS_MODULE);
	return rc;
}

module_init(arm_init);
