#include <linux/init.h>

#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/memory.h>
#include <asm/suspend.h>
#include <asm/tlbflush.h>

static pgd_t *suspend_pgd;

extern int __cpu_suspend(int, long, unsigned long, int (*)(unsigned long));
extern void cpu_resume_turn_mmu_on(void);

/*
 * Hide the first two arguments to __cpu_suspend - these are an implementation
 * detail which platform code shouldn't have to know about.
 */
int cpu_suspend(unsigned long arg, int (*fn)(unsigned long))
{
	struct mm_struct *mm = current->active_mm;
	int ret;

	if (!suspend_pgd)
		return -EINVAL;

	/*
	 * Temporarily switch the page tables to our suspend page
	 * tables, which contain the temporary identity mapping
	 * required for resuming.
	 */
	cpu_switch_mm(suspend_pgd, mm);
	ret = __cpu_suspend(0, PHYS_OFFSET - PAGE_OFFSET, arg, fn);
	cpu_switch_mm(mm->pgd, mm);
	local_flush_tlb_all();

	return ret;
}

static int __init cpu_suspend_init(void)
{
	suspend_pgd = pgd_alloc(&init_mm);
	if (suspend_pgd) {
		unsigned long addr = virt_to_phys(cpu_resume_turn_mmu_on);
		identity_mapping_add(suspend_pgd, addr, addr + SECTION_SIZE);
	}
	return suspend_pgd ? 0 : -ENOMEM;
}
core_initcall(cpu_suspend_init);
