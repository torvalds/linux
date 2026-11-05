// SPDX-License-Identifier: GPL-2.0

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/miscdevice.h>
#include <linux/kvm.h>
#include <linux/kvm_types.h>
#include <linux/kvm_host.h>
#include <linux/cleanup.h>
#include <linux/fpu.h>

#include <asm/access-regs.h>

#include <arm64/kvm_emulate.h>
#include <arm64/sysreg.h>

#include <gmap.h>
#include <kvm_mmu.h>

#include "arm.h"
#include "handle_exit.h"

#define CREATE_TRACE_POINTS
#include "trace.h"

static unsigned long system_supported_vcpu_features(struct kvm_vcpu *vcpu);

#define __INCL_GEN_ARM_FILE
#include "generated/arm.inc"
#undef __INCL_GEN_ARM_FILE

#define KVM_PHYS_SHIFT (40)

int kvm_vm_ioctl_check_extension(struct kvm *kvm, long ext)
{
	int ret;

	switch (ext) {
	case KVM_CAP_NR_VCPUS:
	case KVM_CAP_MAX_VCPUS:
	case KVM_CAP_MAX_VCPU_ID:
		ret = KVM_MAX_VCPUS;
		break;
	case KVM_CAP_ARM_VM_IPA_SIZE:
		ret = get_kvm_ipa_limit();
		break;
	case KVM_CAP_IOEVENTFD:
	case KVM_CAP_PRE_FAULT_MEMORY:
		ret = 1;
		break;
	default:
		ret = 0;
	}

	return ret;
}

static u64 kvm_max_guest_address(void)
{
	u64 max_addr;

	max_addr = min_t(u64, TASK_SIZE_MAX, sclp.hamax);
	max_addr = max_t(u64, max_addr, SZ_1G - 1);
	return ALIGN_DOWN(max_addr + 1, SZ_1G) - 1;
}

static int kvm_gmap_init(struct kvm *kvm)
{
	struct crst_table *table;

	kvm->arch.gmap = gmap_new(kvm, gpa_to_gfn(kvm->arch.guest_phys_size));

	if (!kvm->arch.gmap)
		return -ENOMEM;

	/* arm64 (on s390) do not have pfault */
	clear_bit(GMAP_FLAG_PFAULT_ENABLED, &kvm->arch.gmap->flags);
	set_bit(GMAP_FLAG_ALLOW_HPAGE_1M, &kvm->arch.gmap->flags);

	table = dereference_asce(kvm->arch.gmap->asce);
	crst_table_init((void *)table, _CRSTE_HOLE(table->crstes[0].h.tt).val);

	return 0;
}

static int kvm_vm_type_ipa_size_shift(unsigned long type)
{
	int phys_shift;

	phys_shift = KVM_VM_TYPE_ARM_IPA_SIZE(type);
	if (phys_shift) {
		if (phys_shift > get_kvm_ipa_limit() ||
		    phys_shift < ARM64_MIN_PARANGE_BITS)
			return -EINVAL;
	} else {
		phys_shift = KVM_PHYS_SHIFT;
		if (phys_shift > get_kvm_ipa_limit()) {
			pr_warn_once("%s using unsupported default IPA limit\n",
				     current->comm);
			return -EINVAL;
		}
	}

	return phys_shift;
}

int kvm_arch_init_vm(struct kvm *kvm, unsigned long type)
{
	char debug_name[32];
	int ret;

	if (type & ~KVM_VM_TYPE_ARM_IPA_SIZE_MASK)
		return -EINVAL;

	ret = kvm_vm_type_ipa_size_shift(type);
	if (ret < 0)
		return ret;
	kvm->arch.guest_phys_size = 1UL << ret;

	mutex_init(&kvm->arch.config_lock);
	bitmap_zero(kvm->arch.vcpu_features, KVM_VCPU_MAX_FEATURES);

	snprintf(debug_name, sizeof(debug_name), "kvm-arm64-%u-%u",
		 current->pid, get_random_u32());
	kvm->arch.dbf = debug_register(debug_name, 32, 1, 7 * sizeof(long));
	if (!kvm->arch.dbf)
		return -ENOMEM;
	debug_register_view(kvm->arch.dbf, &debug_sprintf_view);

	ret = kvm_gmap_init(kvm);
	if (ret)
		goto out_err;
	kvm->arch.mem_limit = kvm->arch.guest_phys_size;

	VM_EVENT(kvm, 3, "vm created with type %lu", type);
	return 0;

out_err:
	debug_unregister(kvm->arch.dbf);

	return ret;
}

vm_fault_t kvm_arch_vcpu_fault(struct kvm_vcpu *vcpu, struct vm_fault *vmf)
{
	return VM_FAULT_SIGBUS;
}

long kvm_arch_dev_ioctl(struct file *filp,
			unsigned int ioctl, unsigned long arg)
{
	return -EINVAL;
}

void kvm_arch_destroy_vm(struct kvm *kvm)
{
	kvm_destroy_vcpus(kvm);
	debug_unregister(kvm->arch.dbf);
	kvm->arch.gmap = gmap_put(kvm->arch.gmap);
}

u32 get_kvm_ipa_limit(void)
{
	return fls64(kvm_max_guest_address() + 1) - 1;
}

int kvm_arch_vcpu_precreate(struct kvm *kvm, unsigned int id)
{
	return 0;
}

int kvm_arch_vcpu_create(struct kvm_vcpu *vcpu)
{
	struct kvm_sae_block *sae_block = &vcpu->arch.sae_block;
	struct kvm_sae_save_area *save_area = &vcpu->arch.save_area;

	spin_lock_init(&vcpu->arch.mp_state_lock);

	/* Force users to call KVM_ARM_VCPU_INIT */
	vcpu_clear_flag(vcpu, VCPU_INITIALIZED);

	vcpu->arch.mc = kvm_s390_new_mmu_cache();
	if (!vcpu->arch.mc)
		return -ENOMEM;

	save_area->sdo = virt_to_phys(sae_block);

	sae_block->hbasce = vcpu->kvm->arch.gmap->asce.val;
	sae_block->mso = 0L;
	sae_block->msl = vcpu->kvm->arch.mem_limit - 1;
	sae_block->save_area = virt_to_phys(save_area);

	VM_EVENT(vcpu->kvm, 3,
		 "create cpu %d at 0x%p, sae block at 0x%p, satellite at 0x%p",
		 vcpu->vcpu_id, vcpu, &vcpu->arch.sae_block,
		 &vcpu->arch.save_area);
	return 0;
}

void kvm_arch_vcpu_postcreate(struct kvm_vcpu *vcpu)
{
}

void kvm_arch_vcpu_destroy(struct kvm_vcpu *vcpu)
{
	kvm_s390_free_mmu_cache(vcpu->arch.mc);

	VCPU_EVENT(vcpu, 3, "%s", "free cpu");
}

void kvm_arch_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	save_access_regs(&vcpu->arch.host_acrs[0]);
	vcpu->cpu = cpu;

	lasrm(&vcpu->arch.save_area);
}

void kvm_arch_vcpu_put(struct kvm_vcpu *vcpu)
{
	stiasrm(&vcpu->arch.save_area);

	vcpu->cpu = -1;
	restore_access_regs(&vcpu->arch.host_acrs[0]);
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
	return -EINVAL;
}

int kvm_arch_vcpu_runnable(struct kvm_vcpu *vcpu)
{
	return 0;
}

static unsigned long system_supported_vcpu_features(struct kvm_vcpu *vcpu)
{
	unsigned long features = KVM_S390_ARM64_IMPL_FEATURES;

	return features;
}

bool kvm_arch_vcpu_in_kernel(struct kvm_vcpu *vcpu)
{
	return vcpu_mode_priv(vcpu);
}

int kvm_arch_vcpu_run_pid_change(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;

	if (!kvm_vcpu_initialized(vcpu))
		return -ENOEXEC;

	if (!kvm_arm_vcpu_is_finalized(vcpu))
		return -EPERM;

	if (likely(READ_ONCE(vcpu->pid)))
		return 0;

	scoped_guard(mutex, &kvm->arch.config_lock)
		set_bit(KVM_ARCH_FLAG_HAS_RAN_ONCE, &kvm->arch.flags);

	return 0;
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
		if (kvm_test_request(KVM_REQ_VM_DEAD, vcpu))
			return -EIO;
		if (kvm_check_request(KVM_REQ_VCPU_RESET, vcpu))
			kvm_reset_vcpu(vcpu);
		/*
		 * Clear IRQ_PENDING requests that were made to guarantee
		 * that a VCPU sees new virtual interrupts.
		 */
		kvm_check_request(KVM_REQ_IRQ_PENDING, vcpu);

		if (kvm_check_request(KVM_REQ_TLB_FLUSH, vcpu))
			asce_flush_tlb(vcpu->kvm->arch.gmap->asce);
	}

	return 1;
}

static int kvm_vcpu_initialize(struct kvm_vcpu *vcpu,
			       const struct kvm_vcpu_init *init)
{
	unsigned long features = init->features[0];
	struct kvm *kvm = vcpu->kvm;

	guard(mutex)(&kvm->arch.config_lock);
	if (test_bit(KVM_ARCH_FLAG_VCPU_FEATURES_CONFIGURED, &kvm->arch.flags) &&
	    kvm_vcpu_init_changed(vcpu, init))
		return -EINVAL;

	bitmap_copy(kvm->arch.vcpu_features, &features, KVM_VCPU_MAX_FEATURES);

	kvm_reset_vcpu(vcpu);

	set_bit(KVM_ARCH_FLAG_VCPU_FEATURES_CONFIGURED, &kvm->arch.flags);
	vcpu_set_flag(vcpu, VCPU_INITIALIZED);

	return 0;
}

static int kvm_vcpu_set_target(struct kvm_vcpu *vcpu,
			       const struct kvm_vcpu_init *init)
{
	int ret;

	if (init->target != KVM_ARM_TARGET_GENERIC_V8)
		return -EINVAL;

	ret = kvm_vcpu_init_check_features(vcpu, init);
	if (ret)
		return ret;

	if (!kvm_vcpu_initialized(vcpu))
		return kvm_vcpu_initialize(vcpu, init);

	if (kvm_vcpu_init_changed(vcpu, init))
		return -EINVAL;

	kvm_reset_vcpu(vcpu);

	return 0;
}

static int kvm_arch_vcpu_ioctl_vcpu_init(struct kvm_vcpu *vcpu,
					 struct kvm_vcpu_init *init)
{
	struct kvm_sae_save_area *save_area = &vcpu->arch.save_area;
	struct kvm_sae_block *sae_block = &vcpu->arch.sae_block;
	int ret;

	sae_block->save_area = virt_to_phys(save_area);
	save_area->sdo = virt_to_phys(sae_block);

	ret = kvm_vcpu_set_target(vcpu, init);
	if (ret)
		return ret;

	scoped_guard(spinlock, &vcpu->arch.mp_state_lock) {
		WRITE_ONCE(vcpu->arch.mp_state.mp_state, KVM_MP_STATE_RUNNABLE);
	}

	return 0;
}

int kvm_vm_ioctl_irq_line(struct kvm *kvm, struct kvm_irq_level *irq_level,
			  bool line_status)
{
	/* stub for now*/
	return -EINVAL;
}

static void adjust_pc(struct kvm_vcpu *vcpu)
{
	if (vcpu_get_flag(vcpu, INCREMENT_PC)) {
		kvm_skip_instr(vcpu);
		vcpu_clear_flag(vcpu, INCREMENT_PC);
	}
}

static noinstr void arm_vcpu_enter_exit(phys_addr_t sae_block)
{
	guest_timing_enter_irqoff();
	guest_state_enter_irqoff();

	/*
	 * External interrupts may occur while the SAE instruction is executing
	 * and must preempt guest execution to maintain responsiveness of the
	 * host.
	 *
	 * As a temporary workaround, run SAE with interrupts enabled so that any
	 * interrupt is taken via the normal host interrupt path rather than
	 * being subsumed inside the SAE window.
	 */
	raw_local_irq_enable();
	sae(sae_block);
	raw_local_irq_disable();

	guest_state_exit_irqoff();
	guest_timing_exit_irqoff();
}

/** kvm_arch_vcpu_ioctl_run() - run arm64 vCPU
 *
 * Execute arm64 guest instructions using SAE.
 *
 * Returns:
 * 1 enter the guest (should not be observed by userspace)
 * 0 exit to userspace
 * < 0 exit to userspace, where the return value indicates an error
 *
 *
 */
int kvm_arch_vcpu_ioctl_run(struct kvm_vcpu *vcpu)
{
	DECLARE_KERNEL_FPU_ONSTACK32(fpu_save);
	struct kvm_run *kvm_run = vcpu->run;
	int ret;

	if (kvm_run->exit_reason == KVM_EXIT_MMIO) {
		ret = kvm_handle_mmio_return(vcpu);
		if (ret <= 0)
			return ret;
	}

	vcpu_load(vcpu);

	kernel_fpu_begin(&fpu_save, KERNEL_FPC | KERNEL_VXR);
	load_vx_regs((vcpu->arch.ctxt.vregs));

	if (!vcpu->wants_to_run) {
		ret = -EINTR;
		goto out;
	}

	kvm_sigset_activate(vcpu);

	might_fault();

	ret = 1;
	kvm_run->exit_reason = KVM_EXIT_UNKNOWN;
	while (ret > 0) {
		ret = kvm_xfer_to_guest_mode_handle_work(vcpu);
		if (!ret)
			ret = 1;

		if (ret > 0)
			ret = check_vcpu_requests(vcpu);

		vcpu->arch.sae_block.icptr = 0;

		local_irq_disable();
		xfer_to_guest_mode_prepare();
		/*
		 * The full memory barrier in smp_store_mb() orders the mode
		 * store against the kvm_request_pending() check below, so
		 * any pending kick is visible before we decide to enter the
		 * guest.
		 */
		smp_store_mb(vcpu->mode, IN_GUEST_MODE);

		if (ret <= 0 || kvm_request_pending(vcpu) ||
		    xfer_to_guest_mode_work_pending()) {
			vcpu->mode = OUTSIDE_GUEST_MODE;
			local_irq_enable();
			continue;
		}

		trace_kvm_entry(*vcpu_pc(vcpu));

		adjust_pc(vcpu);

		arm_vcpu_enter_exit(virt_to_phys(&vcpu->arch.sae_block));

		vcpu->mode = OUTSIDE_GUEST_MODE;
		vcpu->stat.exits++;

		local_irq_enable();
		trace_kvm_exit(kvm_vcpu_trap_get_class(vcpu), *vcpu_pc(vcpu));

		ret = handle_exit(vcpu);
	}

	kvm_sigset_deactivate(vcpu);
out:
	if (unlikely(vcpu_get_flag(vcpu, INCREMENT_PC)))
		adjust_pc(vcpu);

	save_vx_regs(vcpu->arch.ctxt.vregs);
	kernel_fpu_end(&fpu_save, KERNEL_FPC | KERNEL_VXR);
	vcpu_put(vcpu);

	return ret;
}

static int kvm_arm_vcpu_set_attr(struct kvm_vcpu *vcpu,
				 struct kvm_device_attr *attr)
{
	int ret;

	switch (attr->group) {
	default:
		ret = -ENXIO;
		break;
	}

	return ret;
}

static int kvm_arm_vcpu_get_attr(struct kvm_vcpu *vcpu,
				 struct kvm_device_attr *attr)
{
	int ret;

	switch (attr->group) {
	default:
		ret = -ENXIO;
		break;
	}

	return ret;
}

static int kvm_arm_vcpu_has_attr(struct kvm_vcpu *vcpu,
				 struct kvm_device_attr *attr)
{
	int ret;

	switch (attr->group) {
	default:
		ret = -ENXIO;
		break;
	}

	return ret;
}

long kvm_arch_vcpu_ioctl(struct file *filp, unsigned int ioctl, unsigned long arg)
{
	struct kvm_vcpu *vcpu = filp->private_data;
	void __user *argp = (void __user *)arg;
	struct kvm_device_attr attr;
	int ret;

	switch (ioctl) {
	case KVM_ARM_VCPU_INIT: {
		struct kvm_vcpu_init init;

		ret = -EFAULT;
		if (copy_from_user(&init, argp, sizeof(init)))
			break;

		ret = kvm_arch_vcpu_ioctl_vcpu_init(vcpu, &init);
		break;
	}
	case KVM_SET_ONE_REG:
	case KVM_GET_ONE_REG: {
		struct kvm_one_reg reg;

		ret = -ENOEXEC;
		if (unlikely(!kvm_vcpu_initialized(vcpu)))
			break;

		ret = -EFAULT;
		if (copy_from_user(&reg, argp, sizeof(reg)))
			break;

		if (kvm_check_request(KVM_REQ_VCPU_RESET, vcpu))
			kvm_reset_vcpu(vcpu);

		if (ioctl == KVM_SET_ONE_REG)
			ret = kvm_arm_set_reg(vcpu, &reg);
		else
			ret = kvm_arm_get_reg(vcpu, &reg);
		break;
	}
	case KVM_GET_REG_LIST: {
		struct kvm_reg_list __user *user_list = argp;
		struct kvm_reg_list reg_list;
		unsigned int n;

		ret = -ENOEXEC;
		if (unlikely(!kvm_vcpu_initialized(vcpu)))
			break;
		ret = -EPERM;
		if (!kvm_arm_vcpu_is_finalized(vcpu))
			break;
		ret = -EFAULT;
		if (copy_from_user(&reg_list, user_list, sizeof(reg_list)))
			break;
		n = reg_list.n;
		reg_list.n = kvm_arm_num_regs(vcpu);
		if (copy_to_user(user_list, &reg_list, sizeof(reg_list)))
			break;
		ret = -E2BIG;
		if (n < reg_list.n)
			break;
		ret = kvm_arm_copy_reg_indices(vcpu, user_list->reg);
		break;
	}
	case KVM_ARM_VCPU_FINALIZE: {
		int what;

		if (!kvm_vcpu_initialized(vcpu))
			return -ENOEXEC;

		if (get_user(what, (const int __user *)argp))
			return -EFAULT;

		ret = kvm_arm_vcpu_finalize(vcpu, what);
		break;
	}
	case KVM_SET_DEVICE_ATTR: {
		ret = -EFAULT;
		if (copy_from_user(&attr, argp, sizeof(attr)))
			break;
		ret = kvm_arm_vcpu_set_attr(vcpu, &attr);
		break;
	}
	case KVM_GET_DEVICE_ATTR: {
		ret = -EFAULT;
		if (copy_from_user(&attr, argp, sizeof(attr)))
			break;
		ret = kvm_arm_vcpu_get_attr(vcpu, &attr);
		break;
	}
	case KVM_HAS_DEVICE_ATTR: {
		ret = -EFAULT;
		if (copy_from_user(&attr, argp, sizeof(attr)))
			break;
		ret = kvm_arm_vcpu_has_attr(vcpu, &attr);
		break;
	}
	default:
		ret = -EINVAL;
	}

	return ret;
}

int kvm_vm_ioctl_get_dirty_log(struct kvm *kvm,
			       struct kvm_dirty_log *log)
{
	return s390_kvm_mmu_get_dirty_log(kvm, log);
}

bool kvm_age_gfn(struct kvm *kvm, struct kvm_gfn_range *range)
{
	scoped_guard(read_lock, &kvm->mmu_lock)
		return gmap_age_gfn(kvm->arch.gmap, range->start, range->end);
}

void kvm_arch_sync_dirty_log(struct kvm *kvm, struct kvm_memory_slot *memslot)
{
	gfn_t last_gfn = memslot->base_gfn + memslot->npages;

	scoped_guard(read_lock, &kvm->mmu_lock)
		gmap_sync_dirty_log(kvm->arch.gmap, memslot->base_gfn, last_gfn);
}

int kvm_arch_prepare_memory_region(struct kvm *kvm,
				   const struct kvm_memory_slot *old,
				   struct kvm_memory_slot *new,
				   enum kvm_mr_change change)
{
	return s390_kvm_mmu_prepare_memory_region(kvm, old, new, change);
}

void kvm_arch_commit_memory_region(struct kvm *kvm, struct kvm_memory_slot *old,
				   const struct kvm_memory_slot *new,
				   enum kvm_mr_change change)
{
}

bool kvm_unmap_gfn_range(struct kvm *kvm, struct kvm_gfn_range *range)
{
	return gmap_unmap_gfn_range(kvm->arch.gmap, range->slot, range->start, range->end);
}

bool kvm_test_age_gfn(struct kvm *kvm, struct kvm_gfn_range *range)
{
	scoped_guard(read_lock, &kvm->mmu_lock)
		return dat_test_age_gfn(kvm->arch.gmap->asce, range->start, range->end);
}

int kvm_arch_vm_ioctl(struct file *filp, unsigned int ioctl, unsigned long arg)
{
	void __user *argp = (void __user *)arg;

	switch (ioctl) {
	case KVM_ARM_PREFERRED_TARGET: {
		struct kvm_vcpu_init init = {
			.target = KVM_ARM_TARGET_GENERIC_V8,
		};

		if (copy_to_user(argp, &init, sizeof(init)))
			return -EFAULT;

		return 0;
	}

	default:
		return -EINVAL;
	}
}

bool kvm_arch_irqchip_in_kernel(struct kvm *kvm)
{
	return false;
}

int kvm_set_msi(struct kvm_kernel_irq_routing_entry *e, struct kvm *kvm,
		int irq_source_id, int level, bool line_status)
{
	return -EINVAL;
}

int kvm_set_routing_entry(struct kvm *kvm,
			  struct kvm_kernel_irq_routing_entry *e,
			  const struct kvm_irq_routing_entry *ue)
{
	return -EINVAL;
}

int kvm_cpu_has_pending_timer(struct kvm_vcpu *vcpu)
{
	return 0;
}

long kvm_arch_vcpu_unlocked_ioctl(struct file *filp, unsigned int ioctl,
				  unsigned long arg)
{
	return -ENOIOCTLCMD;
}

static int __init kvm_s390_arm64_init(void)
{
	if (!sclp.has_aef)
		return -ENODEV;

	return kvm_init(sizeof(struct kvm_vcpu), 0, THIS_MODULE);
}

static __exit void kvm_s390_arm64_exit(void)
{
	kvm_exit();
}

module_init(kvm_s390_arm64_init);
module_exit(kvm_s390_arm64_exit);
