/*
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Author: Christoffer Dall <c.dall@virtualopensystems.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __ARM_KVM_MMU_H__
#define __ARM_KVM_MMU_H__

#include <asm/memory.h>
#include <asm/page.h>

/*
 * We directly use the kernel VA for the HYP, as we can directly share
 * the mapping (HTTBR "covers" TTBR1).
 */
#define kern_hyp_va(kva)	(kva)

/*
 * KVM_MMU_CACHE_MIN_PAGES is the number of stage2 page table translation levels.
 */
#define KVM_MMU_CACHE_MIN_PAGES	2

#ifndef __ASSEMBLY__

#include <linux/highmem.h>
#include <asm/cacheflush.h>
#include <asm/cputype.h>
#include <asm/kvm_hyp.h>
#include <asm/pgalloc.h>
#include <asm/stage2_pgtable.h>

int create_hyp_mappings(void *from, void *to, pgprot_t prot);
int create_hyp_io_mappings(void *from, void *to, phys_addr_t);
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

static inline void kvm_set_pmd(pmd_t *pmd, pmd_t new_pmd)
{
	*pmd = new_pmd;
	dsb(ishst);
}

static inline void kvm_set_pte(pte_t *pte, pte_t new_pte)
{
	*pte = new_pte;
	dsb(ishst);
}

static inline pte_t kvm_s2pte_mkwrite(pte_t pte)
{
	pte_val(pte) |= L_PTE_S2_RDWR;
	return pte;
}

static inline pmd_t kvm_s2pmd_mkwrite(pmd_t pmd)
{
	pmd_val(pmd) |= L_PMD_S2_RDWR;
	return pmd;
}

static inline pte_t kvm_s2pte_mkexec(pte_t pte)
{
	pte_val(pte) &= ~L_PTE_XN;
	return pte;
}

static inline pmd_t kvm_s2pmd_mkexec(pmd_t pmd)
{
	pmd_val(pmd) &= ~PMD_SECT_XN;
	return pmd;
}

static inline void kvm_set_s2pte_readonly(pte_t *pte)
{
	pte_val(*pte) = (pte_val(*pte) & ~L_PTE_S2_RDWR) | L_PTE_S2_RDONLY;
}

static inline bool kvm_s2pte_readonly(pte_t *pte)
{
	return (pte_val(*pte) & L_PTE_S2_RDWR) == L_PTE_S2_RDONLY;
}

static inline bool kvm_s2pte_exec(pte_t *pte)
{
	return !(pte_val(*pte) & L_PTE_XN);
}

static inline void kvm_set_s2pmd_readonly(pmd_t *pmd)
{
	pmd_val(*pmd) = (pmd_val(*pmd) & ~L_PMD_S2_RDWR) | L_PMD_S2_RDONLY;
}

static inline bool kvm_s2pmd_readonly(pmd_t *pmd)
{
	return (pmd_val(*pmd) & L_PMD_S2_RDWR) == L_PMD_S2_RDONLY;
}

static inline bool kvm_s2pmd_exec(pmd_t *pmd)
{
	return !(pmd_val(*pmd) & PMD_SECT_XN);
}

static inline bool kvm_page_empty(void *ptr)
{
	struct page *ptr_page = virt_to_page(ptr);
	return page_count(ptr_page) == 1;
}

#define kvm_pte_table_empty(kvm, ptep) kvm_page_empty(ptep)
#define kvm_pmd_table_empty(kvm, pmdp) kvm_page_empty(pmdp)
#define kvm_pud_table_empty(kvm, pudp) false

#define hyp_pte_table_empty(ptep) kvm_page_empty(ptep)
#define hyp_pmd_table_empty(pmdp) kvm_page_empty(pmdp)
#define hyp_pud_table_empty(pudp) false

struct kvm;

#define kvm_flush_dcache_to_poc(a,l)	__cpuc_flush_dcache_area((a), (l))

static inline bool vcpu_has_cache_enabled(struct kvm_vcpu *vcpu)
{
	return (vcpu_cp15(vcpu, c1_SCTLR) & 0b101) == 0b101;
}

static inline void __clean_dcache_guest_page(struct kvm_vcpu *vcpu,
					     kvm_pfn_t pfn,
					     unsigned long size)
{
	/*
	 * Clean the dcache to the Point of Coherency.
	 *
	 * We need to do this through a kernel mapping (using the
	 * user-space mapping has proved to be the wrong
	 * solution). For that, we need to kmap one page at a time,
	 * and iterate over the range.
	 */

	VM_BUG_ON(size & ~PAGE_MASK);

	while (size) {
		void *va = kmap_atomic_pfn(pfn);

		kvm_flush_dcache_to_poc(va, PAGE_SIZE);

		size -= PAGE_SIZE;
		pfn++;

		kunmap_atomic(va);
	}
}

static inline void __invalidate_icache_guest_page(struct kvm_vcpu *vcpu,
						  kvm_pfn_t pfn,
						  unsigned long size)
{
	u32 iclsz;

	/*
	 * If we are going to insert an instruction page and the icache is
	 * either VIPT or PIPT, there is a potential problem where the host
	 * (or another VM) may have used the same page as this guest, and we
	 * read incorrect data from the icache.  If we're using a PIPT cache,
	 * we can invalidate just that page, but if we are using a VIPT cache
	 * we need to invalidate the entire icache - damn shame - as written
	 * in the ARM ARM (DDI 0406C.b - Page B3-1393).
	 *
	 * VIVT caches are tagged using both the ASID and the VMID and doesn't
	 * need any kind of flushing (DDI 0406C.b - Page B3-1392).
	 */

	VM_BUG_ON(size & ~PAGE_MASK);

	if (icache_is_vivt_asid_tagged())
		return;

	if (!icache_is_pipt()) {
		/* any kind of VIPT cache */
		__flush_icache_all();
		return;
	}

	/*
	 * CTR IminLine contains Log2 of the number of words in the
	 * cache line, so we can get the number of words as
	 * 2 << (IminLine - 1).  To get the number of bytes, we
	 * multiply by 4 (the number of bytes in a 32-bit word), and
	 * get 4 << (IminLine).
	 */
	iclsz = 4 << (read_cpuid(CPUID_CACHETYPE) & 0xf);

	while (size) {
		void *va = kmap_atomic_pfn(pfn);
		void *end = va + PAGE_SIZE;
		void *addr = va;

		do {
			write_sysreg(addr, ICIMVAU);
			addr += iclsz;
		} while (addr < end);

		dsb(ishst);
		isb();

		size -= PAGE_SIZE;
		pfn++;

		kunmap_atomic(va);
	}

	/* Check if we need to invalidate the BTB */
	if ((read_cpuid_ext(CPUID_EXT_MMFR1) >> 28) != 4) {
		write_sysreg(0, BPIALLIS);
		dsb(ishst);
		isb();
	}
}

static inline void __kvm_flush_dcache_pte(pte_t pte)
{
	void *va = kmap_atomic(pte_page(pte));

	kvm_flush_dcache_to_poc(va, PAGE_SIZE);

	kunmap_atomic(va);
}

static inline void __kvm_flush_dcache_pmd(pmd_t pmd)
{
	unsigned long size = PMD_SIZE;
	kvm_pfn_t pfn = pmd_pfn(pmd);

	while (size) {
		void *va = kmap_atomic_pfn(pfn);

		kvm_flush_dcache_to_poc(va, PAGE_SIZE);

		pfn++;
		size -= PAGE_SIZE;

		kunmap_atomic(va);
	}
}

static inline void __kvm_flush_dcache_pud(pud_t pud)
{
}

#define kvm_virt_to_phys(x)		virt_to_idmap((unsigned long)(x))

void kvm_set_way_flush(struct kvm_vcpu *vcpu);
void kvm_toggle_cache(struct kvm_vcpu *vcpu, bool was_enabled);

static inline bool __kvm_cpu_uses_extended_idmap(void)
{
	return false;
}

static inline void __kvm_extend_hypmap(pgd_t *boot_hyp_pgd,
				       pgd_t *hyp_pgd,
				       pgd_t *merged_hyp_pgd,
				       unsigned long hyp_idmap_start) { }

static inline unsigned int kvm_get_vmid_bits(void)
{
	return 8;
}

#endif	/* !__ASSEMBLY__ */

#endif /* __ARM_KVM_MMU_H__ */
