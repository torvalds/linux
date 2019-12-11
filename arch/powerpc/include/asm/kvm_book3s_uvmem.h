/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_KVM_BOOK3S_UVMEM_H__
#define __ASM_KVM_BOOK3S_UVMEM_H__

#ifdef CONFIG_PPC_UV
int kvmppc_uvmem_init(void);
void kvmppc_uvmem_free(void);
int kvmppc_uvmem_slot_init(struct kvm *kvm, const struct kvm_memory_slot *slot);
void kvmppc_uvmem_slot_free(struct kvm *kvm,
			    const struct kvm_memory_slot *slot);
unsigned long kvmppc_h_svm_page_in(struct kvm *kvm,
				   unsigned long gra,
				   unsigned long flags,
				   unsigned long page_shift);
unsigned long kvmppc_h_svm_page_out(struct kvm *kvm,
				    unsigned long gra,
				    unsigned long flags,
				    unsigned long page_shift);
unsigned long kvmppc_h_svm_init_start(struct kvm *kvm);
unsigned long kvmppc_h_svm_init_done(struct kvm *kvm);
int kvmppc_send_page_to_uv(struct kvm *kvm, unsigned long gfn);
void kvmppc_uvmem_drop_pages(const struct kvm_memory_slot *free,
			     struct kvm *kvm);
#else
static inline int kvmppc_uvmem_init(void)
{
	return 0;
}

static inline void kvmppc_uvmem_free(void) { }

static inline int
kvmppc_uvmem_slot_init(struct kvm *kvm, const struct kvm_memory_slot *slot)
{
	return 0;
}

static inline void
kvmppc_uvmem_slot_free(struct kvm *kvm, const struct kvm_memory_slot *slot) { }

static inline unsigned long
kvmppc_h_svm_page_in(struct kvm *kvm, unsigned long gra,
		     unsigned long flags, unsigned long page_shift)
{
	return H_UNSUPPORTED;
}

static inline unsigned long
kvmppc_h_svm_page_out(struct kvm *kvm, unsigned long gra,
		      unsigned long flags, unsigned long page_shift)
{
	return H_UNSUPPORTED;
}

static inline unsigned long kvmppc_h_svm_init_start(struct kvm *kvm)
{
	return H_UNSUPPORTED;
}

static inline unsigned long kvmppc_h_svm_init_done(struct kvm *kvm)
{
	return H_UNSUPPORTED;
}

static inline int kvmppc_send_page_to_uv(struct kvm *kvm, unsigned long gfn)
{
	return -EFAULT;
}

static inline void
kvmppc_uvmem_drop_pages(const struct kvm_memory_slot *free,
			struct kvm *kvm) { }
#endif /* CONFIG_PPC_UV */
#endif /* __ASM_KVM_BOOK3S_UVMEM_H__ */
