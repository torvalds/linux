// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/export.h>

#include <asm/cpu.h>
#include <asm/bootinfo.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/tlb.h>

void local_flush_tlb_all(void)
{
	invtlb_all(INVTLB_CURRENT_ALL, 0, 0);
}
EXPORT_SYMBOL(local_flush_tlb_all);

void local_flush_tlb_user(void)
{
	invtlb_all(INVTLB_CURRENT_GFALSE, 0, 0);
}
EXPORT_SYMBOL(local_flush_tlb_user);

void local_flush_tlb_kernel(void)
{
	invtlb_all(INVTLB_CURRENT_GTRUE, 0, 0);
}
EXPORT_SYMBOL(local_flush_tlb_kernel);

/*
 * All entries common to a mm share an asid. To effectively flush
 * these entries, we just bump the asid.
 */
void local_flush_tlb_mm(struct mm_struct *mm)
{
	int cpu;

	preempt_disable();

	cpu = smp_processor_id();

	if (asid_valid(mm, cpu))
		drop_mmu_context(mm, cpu);
	else
		cpumask_clear_cpu(cpu, mm_cpumask(mm));

	preempt_enable();
}

void local_flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
	unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;
	int cpu = smp_processor_id();

	if (asid_valid(mm, cpu)) {
		unsigned long size, flags;

		local_irq_save(flags);
		start = round_down(start, PAGE_SIZE << 1);
		end = round_up(end, PAGE_SIZE << 1);
		size = (end - start) >> (PAGE_SHIFT + 1);
		if (size <= (current_cpu_data.tlbsizestlbsets ?
			     current_cpu_data.tlbsize / 8 :
			     current_cpu_data.tlbsize / 2)) {
			int asid = cpu_asid(cpu, mm);

			while (start < end) {
				invtlb(INVTLB_ADDR_GFALSE_AND_ASID, asid, start);
				start += (PAGE_SIZE << 1);
			}
		} else {
			drop_mmu_context(mm, cpu);
		}
		local_irq_restore(flags);
	} else {
		cpumask_clear_cpu(cpu, mm_cpumask(mm));
	}
}

void local_flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	unsigned long size, flags;

	local_irq_save(flags);
	size = (end - start + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
	size = (size + 1) >> 1;
	if (size <= (current_cpu_data.tlbsizestlbsets ?
		     current_cpu_data.tlbsize / 8 :
		     current_cpu_data.tlbsize / 2)) {

		start &= (PAGE_MASK << 1);
		end += ((PAGE_SIZE << 1) - 1);
		end &= (PAGE_MASK << 1);

		while (start < end) {
			invtlb_addr(INVTLB_ADDR_GTRUE_OR_ASID, 0, start);
			start += (PAGE_SIZE << 1);
		}
	} else {
		local_flush_tlb_kernel();
	}
	local_irq_restore(flags);
}

void local_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	int cpu = smp_processor_id();

	if (asid_valid(vma->vm_mm, cpu)) {
		int newpid;

		newpid = cpu_asid(cpu, vma->vm_mm);
		page &= (PAGE_MASK << 1);
		invtlb(INVTLB_ADDR_GFALSE_AND_ASID, newpid, page);
	} else {
		cpumask_clear_cpu(cpu, mm_cpumask(vma->vm_mm));
	}
}

/*
 * This one is only used for pages with the global bit set so we don't care
 * much about the ASID.
 */
void local_flush_tlb_one(unsigned long page)
{
	page &= (PAGE_MASK << 1);
	invtlb_addr(INVTLB_ADDR_GTRUE_OR_ASID, 0, page);
}

static void __update_hugetlb(struct vm_area_struct *vma, unsigned long address, pte_t *ptep)
{
#ifdef CONFIG_HUGETLB_PAGE
	int idx;
	unsigned long lo;
	unsigned long flags;

	local_irq_save(flags);

	address &= (PAGE_MASK << 1);
	write_csr_entryhi(address);
	tlb_probe();
	idx = read_csr_tlbidx();
	write_csr_pagesize(PS_HUGE_SIZE);
	lo = pmd_to_entrylo(pte_val(*ptep));
	write_csr_entrylo0(lo);
	write_csr_entrylo1(lo + (HPAGE_SIZE >> 1));

	if (idx < 0)
		tlb_write_random();
	else
		tlb_write_indexed();
	write_csr_pagesize(PS_DEFAULT_SIZE);

	local_irq_restore(flags);
#endif
}

void __update_tlb(struct vm_area_struct *vma, unsigned long address, pte_t *ptep)
{
	int idx;
	unsigned long flags;

	if (cpu_has_ptw)
		return;

	/*
	 * Handle debugger faulting in for debugee.
	 */
	if (current->active_mm != vma->vm_mm)
		return;

	if (pte_val(*ptep) & _PAGE_HUGE) {
		__update_hugetlb(vma, address, ptep);
		return;
	}

	local_irq_save(flags);

	if ((unsigned long)ptep & sizeof(pte_t))
		ptep--;

	address &= (PAGE_MASK << 1);
	write_csr_entryhi(address);
	tlb_probe();
	idx = read_csr_tlbidx();
	write_csr_pagesize(PS_DEFAULT_SIZE);
	write_csr_entrylo0(pte_val(*ptep++));
	write_csr_entrylo1(pte_val(*ptep));
	if (idx < 0)
		tlb_write_random();
	else
		tlb_write_indexed();

	local_irq_restore(flags);
}

static void setup_ptwalker(void)
{
	unsigned long pwctl0, pwctl1;
	unsigned long pgd_i = 0, pgd_w = 0;
	unsigned long pud_i = 0, pud_w = 0;
	unsigned long pmd_i = 0, pmd_w = 0;
	unsigned long pte_i = 0, pte_w = 0;

	pgd_i = PGDIR_SHIFT;
	pgd_w = PAGE_SHIFT - 3;
#if CONFIG_PGTABLE_LEVELS > 3
	pud_i = PUD_SHIFT;
	pud_w = PAGE_SHIFT - 3;
#endif
#if CONFIG_PGTABLE_LEVELS > 2
	pmd_i = PMD_SHIFT;
	pmd_w = PAGE_SHIFT - 3;
#endif
	pte_i = PAGE_SHIFT;
	pte_w = PAGE_SHIFT - 3;

	pwctl0 = pte_i | pte_w << 5 | pmd_i << 10 | pmd_w << 15 | pud_i << 20 | pud_w << 25;
	pwctl1 = pgd_i | pgd_w << 6;

	if (cpu_has_ptw)
		pwctl1 |= CSR_PWCTL1_PTW;

	csr_write64(pwctl0, LOONGARCH_CSR_PWCTL0);
	csr_write64(pwctl1, LOONGARCH_CSR_PWCTL1);
	csr_write64((long)swapper_pg_dir, LOONGARCH_CSR_PGDH);
	csr_write64((long)invalid_pg_dir, LOONGARCH_CSR_PGDL);
	csr_write64((long)smp_processor_id(), LOONGARCH_CSR_TMID);
}

static void output_pgtable_bits_defines(void)
{
#define pr_define(fmt, ...)					\
	pr_debug("#define " fmt, ##__VA_ARGS__)

	pr_debug("#include <asm/asm.h>\n");
	pr_debug("#include <asm/regdef.h>\n");
	pr_debug("\n");

	pr_define("_PAGE_VALID_SHIFT %d\n", _PAGE_VALID_SHIFT);
	pr_define("_PAGE_DIRTY_SHIFT %d\n", _PAGE_DIRTY_SHIFT);
	pr_define("_PAGE_HUGE_SHIFT %d\n", _PAGE_HUGE_SHIFT);
	pr_define("_PAGE_GLOBAL_SHIFT %d\n", _PAGE_GLOBAL_SHIFT);
	pr_define("_PAGE_PRESENT_SHIFT %d\n", _PAGE_PRESENT_SHIFT);
	pr_define("_PAGE_WRITE_SHIFT %d\n", _PAGE_WRITE_SHIFT);
	pr_define("_PAGE_NO_READ_SHIFT %d\n", _PAGE_NO_READ_SHIFT);
	pr_define("_PAGE_NO_EXEC_SHIFT %d\n", _PAGE_NO_EXEC_SHIFT);
	pr_define("PFN_PTE_SHIFT %d\n", PFN_PTE_SHIFT);
	pr_debug("\n");
}

#ifdef CONFIG_NUMA
unsigned long pcpu_handlers[NR_CPUS];
#endif
extern long exception_handlers[VECSIZE * 128 / sizeof(long)];

static void setup_tlb_handler(int cpu)
{
	setup_ptwalker();
	local_flush_tlb_all();

	/* The tlb handlers are generated only once */
	if (cpu == 0) {
		memcpy((void *)tlbrentry, handle_tlb_refill, 0x80);
		local_flush_icache_range(tlbrentry, tlbrentry + 0x80);
		if (!cpu_has_ptw) {
			set_handler(EXCCODE_TLBI * VECSIZE, handle_tlb_load, VECSIZE);
			set_handler(EXCCODE_TLBL * VECSIZE, handle_tlb_load, VECSIZE);
			set_handler(EXCCODE_TLBS * VECSIZE, handle_tlb_store, VECSIZE);
			set_handler(EXCCODE_TLBM * VECSIZE, handle_tlb_modify, VECSIZE);
		} else {
			set_handler(EXCCODE_TLBI * VECSIZE, handle_tlb_load_ptw, VECSIZE);
			set_handler(EXCCODE_TLBL * VECSIZE, handle_tlb_load_ptw, VECSIZE);
			set_handler(EXCCODE_TLBS * VECSIZE, handle_tlb_store_ptw, VECSIZE);
			set_handler(EXCCODE_TLBM * VECSIZE, handle_tlb_modify_ptw, VECSIZE);
		}
		set_handler(EXCCODE_TLBNR * VECSIZE, handle_tlb_protect, VECSIZE);
		set_handler(EXCCODE_TLBNX * VECSIZE, handle_tlb_protect, VECSIZE);
		set_handler(EXCCODE_TLBPE * VECSIZE, handle_tlb_protect, VECSIZE);
	} else {
		int vec_sz __maybe_unused;
		void *addr __maybe_unused;
		struct page *page __maybe_unused;

		/* Avoid lockdep warning */
		rcu_cpu_starting(cpu);

#ifdef CONFIG_NUMA
		vec_sz = sizeof(exception_handlers);

		if (pcpu_handlers[cpu])
			return;

		page = alloc_pages_node(cpu_to_node(cpu), GFP_ATOMIC, get_order(vec_sz));
		if (!page)
			return;

		addr = page_address(page);
		pcpu_handlers[cpu] = (unsigned long)addr;
		memcpy((void *)addr, (void *)eentry, vec_sz);
		local_flush_icache_range((unsigned long)addr, (unsigned long)addr + vec_sz);
		csr_write64(pcpu_handlers[cpu], LOONGARCH_CSR_EENTRY);
		csr_write64(pcpu_handlers[cpu], LOONGARCH_CSR_MERRENTRY);
		csr_write64(pcpu_handlers[cpu] + 80*VECSIZE, LOONGARCH_CSR_TLBRENTRY);
#endif
	}
}

void tlb_init(int cpu)
{
	write_csr_pagesize(PS_DEFAULT_SIZE);
	write_csr_stlbpgsize(PS_DEFAULT_SIZE);
	write_csr_tlbrefill_pagesize(PS_DEFAULT_SIZE);

	setup_tlb_handler(cpu);
	output_pgtable_bits_defines();
}
