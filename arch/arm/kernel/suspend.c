#include <linux/init.h>

#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/memory.h>
#include <asm/suspend.h>
#include <asm/tlbflush.h>

static pgd_t *suspend_pgd;

extern int __cpu_suspend(unsigned long, int (*)(unsigned long));
extern void cpu_resume_mmu(void);

/*
 * This is called by __cpu_suspend() to save the state, and do whatever
 * flushing is required to ensure that when the CPU goes to sleep we have
 * the necessary data available when the caches are not searched.
 */
void __cpu_suspend_save(u32 *ptr, u32 ptrsz, u32 sp, u32 *save_ptr)
{
	*save_ptr = virt_to_phys(ptr);

	/* This must correspond to the LDM in cpu_resume() assembly */
	*ptr++ = virt_to_phys(suspend_pgd);
	*ptr++ = sp;
	*ptr++ = virt_to_phys(cpu_do_resume);

	cpu_do_suspend(ptr);

	flush_cache_all();
	outer_clean_range(*save_ptr, *save_ptr + ptrsz);
	outer_clean_range(virt_to_phys(save_ptr),
			  virt_to_phys(save_ptr) + sizeof(*save_ptr));
}

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
	 * Provide a temporary page table with an identity mapping for
	 * the MMU-enable code, required for resuming.  On successful
	 * resume (indicated by a zero return code), we need to switch
	 * back to the correct page tables.
	 */
	ret = __cpu_suspend(arg, fn);
	if (ret == 0) {
		cpu_switch_mm(mm->pgd, mm);
		local_flush_tlb_all();
	}

	return ret;
}

static int __init cpu_suspend_init(void)
{
	suspend_pgd = pgd_alloc(&init_mm);
	if (suspend_pgd) {
		unsigned long addr = virt_to_phys(cpu_resume_mmu);
		identity_mapping_add(suspend_pgd, addr, addr + SECTION_SIZE);
	}
	return suspend_pgd ? 0 : -ENOMEM;
}
core_initcall(cpu_suspend_init);
