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
 * kernel address).
 *
 * ARMv8.1 (using VHE) does have a TTBR1_EL2, and doesn't use these
 * macros (the entire kernel runs at EL2).
 */
#define HYP_PAGE_OFFSET_SHIFT	VA_BITS
#define HYP_PAGE_OFFSET_MASK	((UL(1) << HYP_PAGE_OFFSET_SHIFT) - 1)
#define HYP_PAGE_OFFSET		(PAGE_OFFSET & HYP_PAGE_OFFSET_MASK)

/*
 * Our virtual mapping for the idmap-ed MMU-enable code. Must be
 * shared across all the page-tables. Conveniently, we use the last
 * possible page, where no kernel mapping will ever exist.
 */
#define TRAMPOLINE_VA		(HYP_PAGE_OFFSET_MASK & PAGE_MASK)

#ifdef __ASSEMBLY__

#include <asm/alternative.h>
#include <asm/cpufeature.h>

/*
 * Convert a kernel VA into a HYP VA.
 * reg: VA to be converted.
 */
.macro kern_hyp_va	reg
alternative_if_not ARM64_HAS_VIRT_HOST_EXTN	
	and	\reg, \reg, #HYP_PAGE_OFFSET_MASK
alternative_else
	nop
alternative_endif
.endm

#else

#include <asm/pgalloc.h>
#include <asm/cachetype.h>
#include <asm/cacheflush.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>

#define KERN_TO_HYP(kva)	((unsigned long)kva - PAGE_OFFSET + HYP_PAGE_OFFSET)

/*
 * We currently only support a 40bit IPA.
 */
#define KVM_PHYS_SHIFT	(40)
#define KVM_PHYS_SIZE	(1UL << KVM_PHYS_SHIFT)
#define KVM_PHYS_MASK	(KVM_PHYS_SIZE - 1UL)

#include <asm/stage2_pgtable.h>

int create_hyp_mappings(void *from, void *to);
int create_hyp_io_mappings(void *from, void *to, phys_addr_t);
void free_boot_hyp_pgd(void);
void free_hyp_pgds(void);

void stage2_unmap_vm(struct kvm *kvm);
int kvm_alloc_stage2_pgd(struct kvm *kvm);
void kvm_free_stage2_pgd(struct kvm *kvm);
int kvm_phys_addr_ioremap(struct kvm *kvm, phys_addr_t guest_ipa,
			  phys_addr_t pa, unsigned long size, bool writable);

int kvm_handle_guest_abort(struct kvm_vcpu *vcpu, struct kvm_run *run);

void kvm_mmu_free_memory_caches(struct kvm_vcpu *vcpu);

phys_addr_t kvm_mmu_get_httbr(void);
phys_addr_t kvm_mmu_get_boot_httbr(void);
phys_addr_t kvm_get_idmap_vector(void);
phys_addr_t kvm_get_idmap_start(void);
int kvm_mmu_init(void);
void kvm_clear_hyp_idmap(void);

#define	kvm_set_pte(ptep, pte)		set_pte(ptep, pte)
#define	kvm_set_pmd(pmdp, pmd)		set_pmd(pmdp, pmd)

static inline void kvm_clean_pgd(pgd_t *pgd) {}
static inline void kvm_clean_pmd(pmd_t *pmd) {}
static inline void kvm_clean_pmd_entry(pmd_t *pmd) {}
static inline void kvm_clean_pte(pte_t *pte) {}
static inline void kvm_clean_pte_entry(pte_t *pte) {}

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

static inline void kvm_set_s2pte_readonly(pte_t *pte)
{
	pteval_t pteval;
	unsigned long tmp;

	asm volatile("//	kvm_set_s2pte_readonly\n"
	"	prfm	pstl1strm, %2\n"
	"1:	ldxr	%0, %2\n"
	"	and	%0, %0, %3		// clear PTE_S2_RDWR\n"
	"	orr	%0, %0, %4		// set PTE_S2_RDONLY\n"
	"	stxr	%w1, %0, %2\n"
	"	cbnz	%w1, 1b\n"
	: "=&r" (pteval), "=&r" (tmp), "+Q" (pte_val(*pte))
	: "L" (~PTE_S2_RDWR), "L" (PTE_S2_RDONLY));
}

static inline bool kvm_s2pte_readonly(pte_t *pte)
{
	return (pte_val(*pte) & PTE_S2_RDWR) == PTE_S2_RDONLY;
}

static inline void kvm_set_s2pmd_readonly(pmd_t *pmd)
{
	kvm_set_s2pte_readonly((pte_t *)pmd);
}

static inline bool kvm_s2pmd_readonly(pmd_t *pmd)
{
	return kvm_s2pte_readonly((pte_t *)pmd);
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
	return (vcpu_sys_reg(vcpu, SCTLR_EL1) & 0b101) == 0b101;
}

static inline void __coherent_cache_guest_page(struct kvm_vcpu *vcpu,
					       kvm_pfn_t pfn,
					       unsigned long size,
					       bool ipa_uncached)
{
	void *va = page_address(pfn_to_page(pfn));

	if (!vcpu_has_cache_enabled(vcpu) || ipa_uncached)
		kvm_flush_dcache_to_poc(va, size);

	if (!icache_is_aliasing()) {		/* PIPT */
		flush_icache_range((unsigned long)va,
				   (unsigned long)va + size);
	} else if (!icache_is_aivivt()) {	/* non ASID-tagged VIVT */
		/* any kind of VIPT cache */
		__flush_icache_all();
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

#define kvm_virt_to_phys(x)		__virt_to_phys((unsigned long)(x))

void kvm_set_way_flush(struct kvm_vcpu *vcpu);
void kvm_toggle_cache(struct kvm_vcpu *vcpu, bool was_enabled);

static inline bool __kvm_cpu_uses_extended_idmap(void)
{
	return __cpu_uses_extended_idmap();
}

static inline void __kvm_extend_hypmap(pgd_t *boot_hyp_pgd,
				       pgd_t *hyp_pgd,
				       pgd_t *merged_hyp_pgd,
				       unsigned long hyp_idmap_start)
{
	int idmap_idx;

	/*
	 * Use the first entry to access the HYP mappings. It is
	 * guaranteed to be free, otherwise we wouldn't use an
	 * extended idmap.
	 */
	VM_BUG_ON(pgd_val(merged_hyp_pgd[0]));
	merged_hyp_pgd[0] = __pgd(__pa(hyp_pgd) | PMD_TYPE_TABLE);

	/*
	 * Create another extended level entry that points to the boot HYP map,
	 * which contains an ID mapping of the HYP init code. We essentially
	 * merge the boot and runtime HYP maps by doing so, but they don't
	 * overlap anyway, so this is fine.
	 */
	idmap_idx = hyp_idmap_start >> VA_BITS;
	VM_BUG_ON(pgd_val(merged_hyp_pgd[idmap_idx]));
	merged_hyp_pgd[idmap_idx] = __pgd(__pa(boot_hyp_pgd) | PMD_TYPE_TABLE);
}

static inline unsigned int kvm_get_vmid_bits(void)
{
	int reg = read_system_reg(SYS_ID_AA64MMFR1_EL1);

	return (cpuid_feature_extract_unsigned_field(reg, ID_AA64MMFR1_VMIDBITS_SHIFT) == 2) ? 16 : 8;
}

#endif /* __ASSEMBLY__ */
#endif /* __ARM64_KVM_MMU_H__ */
