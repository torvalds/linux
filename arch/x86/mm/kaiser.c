

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/bug.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/mm.h>

#include <linux/uaccess.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/desc.h>
#ifdef CONFIG_KAISER

__visible DEFINE_PER_CPU_USER_MAPPED(unsigned long, unsafe_stack_register_backup);

/**
 * Get the real ppn from a address in kernel mapping.
 * @param address The virtual adrress
 * @return the physical address
 */
static inline unsigned long get_pa_from_mapping (unsigned long address)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	pgd = pgd_offset_k(address);
	BUG_ON(pgd_none(*pgd) || pgd_large(*pgd));

	pud = pud_offset(pgd, address);
	BUG_ON(pud_none(*pud));

	if (pud_large(*pud)) {
		return (pud_pfn(*pud) << PAGE_SHIFT) | (address & ~PUD_PAGE_MASK);
	}

	pmd = pmd_offset(pud, address);
	BUG_ON(pmd_none(*pmd));

	if (pmd_large(*pmd)) {
		return (pmd_pfn(*pmd) << PAGE_SHIFT) | (address & ~PMD_PAGE_MASK);
	}

	pte = pte_offset_kernel(pmd, address);
	BUG_ON(pte_none(*pte));

	return (pte_pfn(*pte) << PAGE_SHIFT) | (address & ~PAGE_MASK);
}

void _kaiser_copy (unsigned long start_addr, unsigned long size,
					unsigned long flags)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	unsigned long address;
	unsigned long end_addr = start_addr + size;
	unsigned long target_address;

	for (address = PAGE_ALIGN(start_addr - (PAGE_SIZE - 1));
			address < PAGE_ALIGN(end_addr); address += PAGE_SIZE) {
		target_address = get_pa_from_mapping(address);

		pgd = native_get_shadow_pgd(pgd_offset_k(address));

		BUG_ON(pgd_none(*pgd) && "All shadow pgds should be mapped at this time\n");
		BUG_ON(pgd_large(*pgd));

		pud = pud_offset(pgd, address);
		if (pud_none(*pud)) {
			set_pud(pud, __pud(_PAGE_TABLE | __pa(pmd_alloc_one(0, address))));
		}
		BUG_ON(pud_large(*pud));

		pmd = pmd_offset(pud, address);
		if (pmd_none(*pmd)) {
			set_pmd(pmd, __pmd(_PAGE_TABLE | __pa(pte_alloc_one_kernel(0, address))));
		}
		BUG_ON(pmd_large(*pmd));

		pte = pte_offset_kernel(pmd, address);
		if (pte_none(*pte)) {
			set_pte(pte, __pte(flags | target_address));
		} else {
			BUG_ON(__pa(pte_page(*pte)) != target_address);
		}
	}
}

// at first, add a pmd for every pgd entry in the shadowmem-kernel-part of the kernel mapping
static inline void __init _kaiser_init(void)
{
	pgd_t *pgd;
	int i = 0;

	pgd = native_get_shadow_pgd(pgd_offset_k((unsigned long )0));
	for (i = PTRS_PER_PGD / 2; i < PTRS_PER_PGD; i++) {
		set_pgd(pgd + i, __pgd(_PAGE_TABLE |__pa(pud_alloc_one(0, 0))));
	}
}

extern char __per_cpu_user_mapped_start[], __per_cpu_user_mapped_end[];
spinlock_t shadow_table_lock;
void __init kaiser_init(void)
{
	int cpu;
	spin_lock_init(&shadow_table_lock);

	spin_lock(&shadow_table_lock);

	_kaiser_init();

	for_each_possible_cpu(cpu) {
		// map the per cpu user variables
		_kaiser_copy(
				(unsigned long) (__per_cpu_user_mapped_start + per_cpu_offset(cpu)),
				(unsigned long) __per_cpu_user_mapped_end - (unsigned long) __per_cpu_user_mapped_start,
				__PAGE_KERNEL);
	}

	// map the entry/exit text section, which is responsible to switch between user- and kernel mode
	_kaiser_copy(
			(unsigned long) __entry_text_start,
			(unsigned long) __entry_text_end - (unsigned long) __entry_text_start,
			__PAGE_KERNEL_RX);

	// the fixed map address of the idt_table
	_kaiser_copy(
			(unsigned long) idt_descr.address,
			sizeof(gate_desc) * NR_VECTORS,
			__PAGE_KERNEL_RO);

	spin_unlock(&shadow_table_lock);
}

// add a mapping to the shadow-mapping, and synchronize the mappings
void kaiser_add_mapping(unsigned long addr, unsigned long size, unsigned long flags)
{
	spin_lock(&shadow_table_lock);
	_kaiser_copy(addr, size, flags);
	spin_unlock(&shadow_table_lock);
}

extern void unmap_pud_range(pgd_t *pgd, unsigned long start, unsigned long end);
void kaiser_remove_mapping(unsigned long start, unsigned long size)
{
	pgd_t *pgd = native_get_shadow_pgd(pgd_offset_k(start));
	spin_lock(&shadow_table_lock);
	do {
		unmap_pud_range(pgd, start, start + size);
	} while (pgd++ != native_get_shadow_pgd(pgd_offset_k(start + size)));
	spin_unlock(&shadow_table_lock);
}
#endif /* CONFIG_KAISER */
