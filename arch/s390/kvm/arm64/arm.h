/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ARCH_S390_KVM_ARM64_H
#define ARCH_S390_KVM_ARM64_H

#include <linux/compiler_attributes.h>
#include <linux/kvm_host.h>

#define VM_EVENT(d_kvm, d_loglevel, d_string, d_args...)			\
	debug_sprintf_event((d_kvm)->arch.dbf, d_loglevel,			\
			    __stringify(KVM_DEV_NAME) ": " d_string "\n",	\
			    d_args)

#define VCPU_EVENT(d_vcpu, d_loglevel, d_string, d_args...)				\
	debug_sprintf_event((d_vcpu)->kvm->arch.dbf, d_loglevel,			\
		__stringify(KVM_DEV_NAME) " %02d[%016llx-%016llx]: " d_string "\n",	\
		(d_vcpu)->vcpu_id, (d_vcpu)->arch.sae_block.pstate,			\
		(d_vcpu)->arch.sae_block.pc, d_args)

static __always_inline int kvm_is_ucontrol(struct kvm *kvm)
{
	return 0;
}

static __always_inline int __kvm_s390_pv_destroy_page(struct page *page)
{
	return 0;
}

static __always_inline void kvm_s390_vsie_gmap_notifier(struct gmap *gmap, gpa_t start, gpa_t end)
{
}

static __always_inline int kvm_s390_pv_get_handle(struct kvm *kvm)
{
	return 0;
}

static __always_inline int kvm_s390_is_migration_mode(struct kvm *kvm)
{
	return false;
}

static __always_inline bool kvm_arch_setup_async_pf(struct kvm_vcpu *vcpu)
{
	return false;
}

static __always_inline void kvm_s390_update_cmma_dirty(struct kvm *kvm, const struct kvm_memory_slot *old)
{
}

/* should never be called */
static __always_inline int kvm_s390_vm_stop_migration(struct kvm *kvm)
{
	return -EINVAL;
}

#endif /* ARCH_S390_KVM_ARM64_H */
