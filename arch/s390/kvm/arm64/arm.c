// SPDX-License-Identifier: GPL-2.0

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/miscdevice.h>
#include <linux/kvm.h>
#include <linux/kvm_types.h>
#include <linux/kvm_host.h>

#include <gmap.h>
#include <kvm_mmu.h>

#include "arm.h"

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
