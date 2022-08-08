/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#ifndef __ARM64_KVM_MMU_H__
#define __ARM64_KVM_MMU_H__

#include <asm/page.h>
#include <asm/memory.h>
#include <asm/mmu.h>
#include <asm/cpufeature.h>

/*
 * As ARMv8.0 only has the TTBR0_EL2 register, we cannot express
 * "negative" addresses. This makes it impossible to directly share
 * mappings with the kernel.
 *
 * Instead, give the HYP mode its own VA region at a fixed offset from
 * the kernel by just masking the top bits (which are all ones for a
 * kernel address). We need to find out how many bits to mask.
 *
 * We want to build a set of page tables that cover both parts of the
 * idmap (the trampoline page used to initialize EL2), and our normal
 * runtime VA space, at the same time.
 *
 * Given that the kernel uses VA_BITS for its entire address space,
 * and that half of that space (VA_BITS - 1) is used for the linear
 * mapping, we can also limit the EL2 space to (VA_BITS - 1).
 *
 * The main question is "Within the VA_BITS space, does EL2 use the
 * top or the bottom half of that space to shadow the kernel's linear
 * mapping?". As we need to idmap the trampoline page, this is
 * determined by the range in which this page lives.
 *
 * If the page is in the bottom half, we have to use the top half. If
 * the page is in the top half, we have to use the bottom half:
 *
 * T = __pa_symbol(__hyp_idmap_text_start)
 * if (T & BIT(VA_BITS - 1))
 *	HYP_VA_MIN = 0  //idmap in upper half
 * else
 *	HYP_VA_MIN = 1 << (VA_BITS - 1)
 * HYP_VA_MAX = HYP_VA_MIN + (1 << (VA_BITS - 1)) - 1
 *
 * When using VHE, there are no separate hyp mappings and all KVM
 * functionality is already mapped as part of the main kernel
 * mappings, and none of this applies in that case.
 */

#ifdef __ASSEMBLY__

#include <asm/alternative.h>

/*
 * Convert a kernel VA into a HYP VA.
 * reg: VA to be converted.
 *
 * The actual code generation takes place in kvm_update_va_mask, and
 * the instructions below are only there to reserve the space and
 * perform the register allocation (kvm_update_va_mask uses the
 * specific registers encoded in the instructions).
 */
.macro kern_hyp_va	reg
alternative_cb kvm_update_va_mask
	and     \reg, \reg, #1		/* mask with va_mask */
	ror	\reg, \reg, #1		/* rotate to the first tag bit */
	add	\reg, \reg, #0		/* insert the low 12 bits of the tag */
	add	\reg, \reg, #0, lsl 12	/* insert the top 12 bits of the tag */
	ror	\reg, \reg, #63		/* rotate back */
alternative_cb_end
.endm

/*
 * Convert a hypervisor VA to a PA
 * reg: hypervisor address to be converted in place
 * tmp: temporary register
 */
.macro hyp_pa reg, tmp
	ldr_l	\tmp, hyp_physvirt_offset
	add	\reg, \reg, \tmp
.endm

/*
 * Convert a hypervisor VA to a kernel image address
 * reg: hypervisor address to be converted in place
 * tmp: temporary register
 *
 * The actual code generation takes place in kvm_get_kimage_voffset, and
 * the instructions below are only there to reserve the space and
 * perform the register allocation (kvm_get_kimage_voffset uses the
 * specific registers encoded in the instructions).
 */
.macro hyp_kimg_va reg, tmp
	/* Convert hyp VA -> PA. */
	hyp_pa	\reg, \tmp

	/* Load kimage_voffset. */
alternative_cb kvm_get_kimage_voffset
	movz	\tmp, #0
	movk	\tmp, #0, lsl #16
	movk	\tmp, #0, lsl #32
	movk	\tmp, #0, lsl #48
alternative_cb_end

	/* Convert PA -> kimg VA. */
	add	\reg, \reg, \tmp
.endm

#else

#include <linux/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/cache.h>
#include <asm/cacheflush.h>
#include <asm/mmu_context.h>

void kvm_update_va_mask(struct alt_instr *alt,
			__le32 *origptr, __le32 *updptr, int nr_inst);
void kvm_compute_layout(void);
void kvm_apply_hyp_relocations(void);

static __always_inline unsigned long __kern_hyp_va(unsigned long v)
{
	asm volatile(ALTERNATIVE_CB("and %0, %0, #1\n"
				    "ror %0, %0, #1\n"
				    "add %0, %0, #0\n"
				    "add %0, %0, #0, lsl 12\n"
				    "ror %0, %0, #63\n",
				    kvm_update_va_mask)
		     : "+r" (v));
	return v;
}

#define kern_hyp_va(v) 	((typeof(v))(__kern_hyp_va((unsigned long)(v))))

/*
 * We currently support using a VM-specified IPA size. For backward
 * compatibility, the default IPA size is fixed to 40bits.
 */
#define KVM_PHYS_SHIFT	(40)

#define kvm_phys_shift(kvm)		VTCR_EL2_IPA(kvm->arch.vtcr)
#define kvm_phys_size(kvm)		(_AC(1, ULL) << kvm_phys_shift(kvm))
#define kvm_phys_mask(kvm)		(kvm_phys_size(kvm) - _AC(1, ULL))

#include <asm/kvm_pgtable.h>
#include <asm/stage2_pgtable.h>

int create_hyp_mappings(void *from, void *to, enum kvm_pgtable_prot prot);
int create_hyp_io_mappings(phys_addr_t phys_addr, size_t size,
			   void __iomem **kaddr,
			   void __iomem **haddr);
int create_hyp_exec_mappings(phys_addr_t phys_addr, size_t size,
			     void **haddr);
void free_hyp_pgds(void);

void stage2_unmap_vm(struct kvm *kvm);
int kvm_init_stage2_mmu(struct kvm *kvm, struct kvm_s2_mmu *mmu);
void kvm_free_stage2_pgd(struct kvm_s2_mmu *mmu);
int kvm_phys_addr_ioremap(struct kvm *kvm, phys_addr_t guest_ipa,
			  phys_addr_t pa, unsigned long size, bool writable);

int kvm_handle_guest_abort(struct kvm_vcpu *vcpu);

phys_addr_t kvm_mmu_get_httbr(void);
phys_addr_t kvm_get_idmap_vector(void);
int kvm_mmu_init(void);

struct kvm;

#define kvm_flush_dcache_to_poc(a,l)	__flush_dcache_area((a), (l))

static inline bool vcpu_has_cache_enabled(struct kvm_vcpu *vcpu)
{
	return (vcpu_read_sys_reg(vcpu, SCTLR_EL1) & 0b101) == 0b101;
}

static inline void __clean_dcache_guest_page(kvm_pfn_t pfn, unsigned long size)
{
	void *va = page_address(pfn_to_page(pfn));

	/*
	 * With FWB, we ensure that the guest always accesses memory using
	 * cacheable attributes, and we don't have to clean to PoC when
	 * faulting in pages. Furthermore, FWB implies IDC, so cleaning to
	 * PoU is not required either in this case.
	 */
	if (cpus_have_const_cap(ARM64_HAS_STAGE2_FWB))
		return;

	kvm_flush_dcache_to_poc(va, size);
}

static inline void __invalidate_icache_guest_page(kvm_pfn_t pfn,
						  unsigned long size)
{
	if (icache_is_aliasing()) {
		/* any kind of VIPT cache */
		__flush_icache_all();
	} else if (is_kernel_in_hyp_mode() || !icache_is_vpipt()) {
		/* PIPT or VPIPT at EL2 (see comment in __kvm_tlb_flush_vmid_ipa) */
		void *va = page_address(pfn_to_page(pfn));

		invalidate_icache_range((unsigned long)va,
					(unsigned long)va + size);
	}
}

void kvm_set_way_flush(struct kvm_vcpu *vcpu);
void kvm_toggle_cache(struct kvm_vcpu *vcpu, bool was_enabled);

static inline unsigned int kvm_get_vmid_bits(void)
{
	int reg = read_sanitised_ftr_reg(SYS_ID_AA64MMFR1_EL1);

	return get_vmid_bits(reg);
}

/*
 * We are not in the kvm->srcu critical section most of the time, so we take
 * the SRCU read lock here. Since we copy the data from the user page, we
 * can immediately drop the lock again.
 */
static inline int kvm_read_guest_lock(struct kvm *kvm,
				      gpa_t gpa, void *data, unsigned long len)
{
	int srcu_idx = srcu_read_lock(&kvm->srcu);
	int ret = kvm_read_guest(kvm, gpa, data, len);

	srcu_read_unlock(&kvm->srcu, srcu_idx);

	return ret;
}

static inline int kvm_write_guest_lock(struct kvm *kvm, gpa_t gpa,
				       const void *data, unsigned long len)
{
	int srcu_idx = srcu_read_lock(&kvm->srcu);
	int ret = kvm_write_guest(kvm, gpa, data, len);

	srcu_read_unlock(&kvm->srcu, srcu_idx);

	return ret;
}

#define kvm_phys_to_vttbr(addr)		phys_to_ttbr(addr)

static __always_inline u64 kvm_get_vttbr(struct kvm_s2_mmu *mmu)
{
	struct kvm_vmid *vmid = &mmu->vmid;
	u64 vmid_field, baddr;
	u64 cnp = system_supports_cnp() ? VTTBR_CNP_BIT : 0;

	baddr = mmu->pgd_phys;
	vmid_field = (u64)vmid->vmid << VTTBR_VMID_SHIFT;
	return kvm_phys_to_vttbr(baddr) | vmid_field | cnp;
}

/*
 * Must be called from hyp code running at EL2 with an updated VTTBR
 * and interrupts disabled.
 */
static __always_inline void __load_guest_stage2(struct kvm_s2_mmu *mmu)
{
	write_sysreg(kern_hyp_va(mmu->kvm)->arch.vtcr, vtcr_el2);
	write_sysreg(kvm_get_vttbr(mmu), vttbr_el2);

	/*
	 * ARM errata 1165522 and 1530923 require the actual execution of the
	 * above before we can switch to the EL1/EL0 translation regime used by
	 * the guest.
	 */
	asm(ALTERNATIVE("nop", "isb", ARM64_WORKAROUND_SPECULATIVE_AT));
}

#endif /* __ASSEMBLY__ */
#endif /* __ARM64_KVM_MMU_H__ */
