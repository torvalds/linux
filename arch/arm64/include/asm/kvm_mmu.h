/*
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __ARM64_KVM_MMU_H__
#define __ARM64_KVM_MMU_H__

#include <asm/page.h>
#include <asm/memory.h>
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
 * This of course assumes that the trampoline page exists within the
 * VA_BITS range. If it doesn't, then it means we're in the odd case
 * where the kernel idmap (as well as HYP) uses more levels than the
 * kernel runtime page tables (as seen when the kernel is configured
 * for 4k pages, 39bits VA, and yet memory lives just above that
 * limit, forcing the idmap to use 4 levels of page tables while the
 * kernel itself only uses 3). In this particular case, it doesn't
 * matter which side of VA_BITS we use, as we're guaranteed not to
 * conflict with anything.
 *
 * When using VHE, there are no separate hyp mappings and all KVM
 * functionality is already mapped as part of the main kernel
 * mappings, and none of this applies in that case.
 */

#ifdef __ASSEMBLY__

#include <asm/alternative.h>
#include <asm/cpufeature.h>

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

#else

#include <asm/pgalloc.h>
#include <asm/cache.h>
#include <asm/cacheflush.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>

void kvm_update_va_mask(struct alt_instr *alt,
			__le32 *origptr, __le32 *updptr, int nr_inst);

static inline unsigned long __kern_hyp_va(unsigned long v)
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
 * Obtain the PC-relative address of a kernel symbol
 * s: symbol
 *
 * The goal of this macro is to return a symbol's address based on a
 * PC-relative computation, as opposed to a loading the VA from a
 * constant pool or something similar. This works well for HYP, as an
 * absolute VA is guaranteed to be wrong. Only use this if trying to
 * obtain the address of a symbol (i.e. not something you obtained by
 * following a pointer).
 */
#define hyp_symbol_addr(s)						\
	({								\
		typeof(s) *addr;					\
		asm("adrp	%0, %1\n"				\
		    "add	%0, %0, :lo12:%1\n"			\
		    : "=r" (addr) : "S" (&s));				\
		addr;							\
	})

/*
 * We currently only support a 40bit IPA.
 */
#define KVM_PHYS_SHIFT	(40)
#define KVM_PHYS_SIZE	(1UL << KVM_PHYS_SHIFT)
#define KVM_PHYS_MASK	(KVM_PHYS_SIZE - 1UL)

#include <asm/stage2_pgtable.h>

int create_hyp_mappings(void *from, void *to, pgprot_t prot);
int create_hyp_io_mappings(phys_addr_t phys_addr, size_t size,
			   void __iomem **kaddr,
			   void __iomem **haddr);
int create_hyp_exec_mappings(phys_addr_t phys_addr, size_t size,
			     void **haddr);
void free_hyp_pgds(void);

void stage2_unmap_vm(struct kvm *kvm);
int kvm_alloc_stage2_pgd(struct kvm *kvm);
void kvm_free_stage2_pgd(struct kvm *kvm);
int kvm_phys_addr_ioremap(struct kvm *kvm, phys_addr_t guest_ipa,
			  phys_addr_t pa, unsigned long size, bool writable);

int kvm_handle_guest_abort(struct kvm_vcpu *vcpu, struct kvm_run *run);

void kvm_mmu_free_memory_caches(struct kvm_vcpu *vcpu);

phys_addr_t kvm_mmu_get_httbr(void);
phys_addr_t kvm_get_idmap_vector(void);
int kvm_mmu_init(void);
void kvm_clear_hyp_idmap(void);

#define	kvm_set_pte(ptep, pte)		set_pte(ptep, pte)
#define	kvm_set_pmd(pmdp, pmd)		set_pmd(pmdp, pmd)

static inline pte_t kvm_s2pte_mkwrite(pte_t pte)
{
	pte_val(pte) |= PTE_S2_RDWR;
	return pte;
}

static inline pmd_t kvm_s2pmd_mkwrite(pmd_t pmd)
{
	pmd_val(pmd) |= PMD_S2_RDWR;
	return pmd;
}

static inline pte_t kvm_s2pte_mkexec(pte_t pte)
{
	pte_val(pte) &= ~PTE_S2_XN;
	return pte;
}

static inline pmd_t kvm_s2pmd_mkexec(pmd_t pmd)
{
	pmd_val(pmd) &= ~PMD_S2_XN;
	return pmd;
}

static inline void kvm_set_s2pte_readonly(pte_t *ptep)
{
	pteval_t old_pteval, pteval;

	pteval = READ_ONCE(pte_val(*ptep));
	do {
		old_pteval = pteval;
		pteval &= ~PTE_S2_RDWR;
		pteval |= PTE_S2_RDONLY;
		pteval = cmpxchg_relaxed(&pte_val(*ptep), old_pteval, pteval);
	} while (pteval != old_pteval);
}

static inline bool kvm_s2pte_readonly(pte_t *ptep)
{
	return (READ_ONCE(pte_val(*ptep)) & PTE_S2_RDWR) == PTE_S2_RDONLY;
}

static inline bool kvm_s2pte_exec(pte_t *ptep)
{
	return !(READ_ONCE(pte_val(*ptep)) & PTE_S2_XN);
}

static inline void kvm_set_s2pmd_readonly(pmd_t *pmdp)
{
	kvm_set_s2pte_readonly((pte_t *)pmdp);
}

static inline bool kvm_s2pmd_readonly(pmd_t *pmdp)
{
	return kvm_s2pte_readonly((pte_t *)pmdp);
}

static inline bool kvm_s2pmd_exec(pmd_t *pmdp)
{
	return !(READ_ONCE(pmd_val(*pmdp)) & PMD_S2_XN);
}

static inline bool kvm_page_empty(void *ptr)
{
	struct page *ptr_page = virt_to_page(ptr);
	return page_count(ptr_page) == 1;
}

#define hyp_pte_table_empty(ptep) kvm_page_empty(ptep)

#ifdef __PAGETABLE_PMD_FOLDED
#define hyp_pmd_table_empty(pmdp) (0)
#else
#define hyp_pmd_table_empty(pmdp) kvm_page_empty(pmdp)
#endif

#ifdef __PAGETABLE_PUD_FOLDED
#define hyp_pud_table_empty(pudp) (0)
#else
#define hyp_pud_table_empty(pudp) kvm_page_empty(pudp)
#endif

struct kvm;

#define kvm_flush_dcache_to_poc(a,l)	__flush_dcache_area((a), (l))

static inline bool vcpu_has_cache_enabled(struct kvm_vcpu *vcpu)
{
	return (vcpu_read_sys_reg(vcpu, SCTLR_EL1) & 0b101) == 0b101;
}

static inline void __clean_dcache_guest_page(kvm_pfn_t pfn, unsigned long size)
{
	void *va = page_address(pfn_to_page(pfn));

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

static inline void __kvm_flush_dcache_pte(pte_t pte)
{
	struct page *page = pte_page(pte);
	kvm_flush_dcache_to_poc(page_address(page), PAGE_SIZE);
}

static inline void __kvm_flush_dcache_pmd(pmd_t pmd)
{
	struct page *page = pmd_page(pmd);
	kvm_flush_dcache_to_poc(page_address(page), PMD_SIZE);
}

static inline void __kvm_flush_dcache_pud(pud_t pud)
{
	struct page *page = pud_page(pud);
	kvm_flush_dcache_to_poc(page_address(page), PUD_SIZE);
}

#define kvm_virt_to_phys(x)		__pa_symbol(x)

void kvm_set_way_flush(struct kvm_vcpu *vcpu);
void kvm_toggle_cache(struct kvm_vcpu *vcpu, bool was_enabled);

static inline bool __kvm_cpu_uses_extended_idmap(void)
{
	return __cpu_uses_extended_idmap_level();
}

static inline unsigned long __kvm_idmap_ptrs_per_pgd(void)
{
	return idmap_ptrs_per_pgd;
}

/*
 * Can't use pgd_populate here, because the extended idmap adds an extra level
 * above CONFIG_PGTABLE_LEVELS (which is 2 or 3 if we're using the extended
 * idmap), and pgd_populate is only available if CONFIG_PGTABLE_LEVELS = 4.
 */
static inline void __kvm_extend_hypmap(pgd_t *boot_hyp_pgd,
				       pgd_t *hyp_pgd,
				       pgd_t *merged_hyp_pgd,
				       unsigned long hyp_idmap_start)
{
	int idmap_idx;
	u64 pgd_addr;

	/*
	 * Use the first entry to access the HYP mappings. It is
	 * guaranteed to be free, otherwise we wouldn't use an
	 * extended idmap.
	 */
	VM_BUG_ON(pgd_val(merged_hyp_pgd[0]));
	pgd_addr = __phys_to_pgd_val(__pa(hyp_pgd));
	merged_hyp_pgd[0] = __pgd(pgd_addr | PMD_TYPE_TABLE);

	/*
	 * Create another extended level entry that points to the boot HYP map,
	 * which contains an ID mapping of the HYP init code. We essentially
	 * merge the boot and runtime HYP maps by doing so, but they don't
	 * overlap anyway, so this is fine.
	 */
	idmap_idx = hyp_idmap_start >> VA_BITS;
	VM_BUG_ON(pgd_val(merged_hyp_pgd[idmap_idx]));
	pgd_addr = __phys_to_pgd_val(__pa(boot_hyp_pgd));
	merged_hyp_pgd[idmap_idx] = __pgd(pgd_addr | PMD_TYPE_TABLE);
}

static inline unsigned int kvm_get_vmid_bits(void)
{
	int reg = read_sanitised_ftr_reg(SYS_ID_AA64MMFR1_EL1);

	return (cpuid_feature_extract_unsigned_field(reg, ID_AA64MMFR1_VMIDBITS_SHIFT) == 2) ? 16 : 8;
}

#ifdef CONFIG_KVM_INDIRECT_VECTORS
/*
 * EL2 vectors can be mapped and rerouted in a number of ways,
 * depending on the kernel configuration and CPU present:
 *
 * - If the CPU has the ARM64_HARDEN_BRANCH_PREDICTOR cap, the
 *   hardening sequence is placed in one of the vector slots, which is
 *   executed before jumping to the real vectors.
 *
 * - If the CPU has both the ARM64_HARDEN_EL2_VECTORS cap and the
 *   ARM64_HARDEN_BRANCH_PREDICTOR cap, the slot containing the
 *   hardening sequence is mapped next to the idmap page, and executed
 *   before jumping to the real vectors.
 *
 * - If the CPU only has the ARM64_HARDEN_EL2_VECTORS cap, then an
 *   empty slot is selected, mapped next to the idmap page, and
 *   executed before jumping to the real vectors.
 *
 * Note that ARM64_HARDEN_EL2_VECTORS is somewhat incompatible with
 * VHE, as we don't have hypervisor-specific mappings. If the system
 * is VHE and yet selects this capability, it will be ignored.
 */
#include <asm/mmu.h>

extern void *__kvm_bp_vect_base;
extern int __kvm_harden_el2_vector_slot;

static inline void *kvm_get_hyp_vector(void)
{
	struct bp_hardening_data *data = arm64_get_bp_hardening_data();
	void *vect = kern_hyp_va(kvm_ksym_ref(__kvm_hyp_vector));
	int slot = -1;

	if (cpus_have_const_cap(ARM64_HARDEN_BRANCH_PREDICTOR) && data->fn) {
		vect = kern_hyp_va(kvm_ksym_ref(__bp_harden_hyp_vecs_start));
		slot = data->hyp_vectors_slot;
	}

	if (this_cpu_has_cap(ARM64_HARDEN_EL2_VECTORS) && !has_vhe()) {
		vect = __kvm_bp_vect_base;
		if (slot == -1)
			slot = __kvm_harden_el2_vector_slot;
	}

	if (slot != -1)
		vect += slot * SZ_2K;

	return vect;
}

/*  This is only called on a !VHE system */
static inline int kvm_map_vectors(void)
{
	/*
	 * HBP  = ARM64_HARDEN_BRANCH_PREDICTOR
	 * HEL2 = ARM64_HARDEN_EL2_VECTORS
	 *
	 * !HBP + !HEL2 -> use direct vectors
	 *  HBP + !HEL2 -> use hardened vectors in place
	 * !HBP +  HEL2 -> allocate one vector slot and use exec mapping
	 *  HBP +  HEL2 -> use hardened vertors and use exec mapping
	 */
	if (cpus_have_const_cap(ARM64_HARDEN_BRANCH_PREDICTOR)) {
		__kvm_bp_vect_base = kvm_ksym_ref(__bp_harden_hyp_vecs_start);
		__kvm_bp_vect_base = kern_hyp_va(__kvm_bp_vect_base);
	}

	if (cpus_have_const_cap(ARM64_HARDEN_EL2_VECTORS)) {
		phys_addr_t vect_pa = __pa_symbol(__bp_harden_hyp_vecs_start);
		unsigned long size = (__bp_harden_hyp_vecs_end -
				      __bp_harden_hyp_vecs_start);

		/*
		 * Always allocate a spare vector slot, as we don't
		 * know yet which CPUs have a BP hardening slot that
		 * we can reuse.
		 */
		__kvm_harden_el2_vector_slot = atomic_inc_return(&arm64_el2_vector_last_slot);
		BUG_ON(__kvm_harden_el2_vector_slot >= BP_HARDEN_EL2_SLOTS);
		return create_hyp_exec_mappings(vect_pa, size,
						&__kvm_bp_vect_base);
	}

	return 0;
}
#else
static inline void *kvm_get_hyp_vector(void)
{
	return kern_hyp_va(kvm_ksym_ref(__kvm_hyp_vector));
}

static inline int kvm_map_vectors(void)
{
	return 0;
}
#endif

#define kvm_phys_to_vttbr(addr)		phys_to_ttbr(addr)

#endif /* __ASSEMBLY__ */
#endif /* __ARM64_KVM_MMU_H__ */
