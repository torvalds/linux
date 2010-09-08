#include <linux/io.h>

#include <asm/trampoline.h>
#include <asm/pgtable.h>
#include <asm/e820.h>

#if defined(CONFIG_X86_64) && defined(CONFIG_ACPI_SLEEP)
#define __trampinit
#define __trampinitdata
#else
#define __trampinit __cpuinit
#define __trampinitdata __cpuinitdata
#endif

/* ready for x86_64 and x86 */
unsigned char *__trampinitdata trampoline_base;

void __init reserve_trampoline_memory(void)
{
	unsigned long mem;

	/* Has to be in very low memory so we can execute real-mode AP code. */
	mem = find_e820_area(0, 1<<20, TRAMPOLINE_SIZE, PAGE_SIZE);
	if (mem == -1L)
		panic("Cannot allocate trampoline\n");

	trampoline_base = __va(mem);
	reserve_early(mem, mem + TRAMPOLINE_SIZE, "TRAMPOLINE");
}

/*
 * Currently trivial. Write the real->protected mode
 * bootstrap into the page concerned. The caller
 * has made sure it's suitably aligned.
 */
unsigned long __trampinit setup_trampoline(void)
{
	memcpy(trampoline_base, trampoline_data, TRAMPOLINE_SIZE);
	return virt_to_phys(trampoline_base);
}

void __init setup_trampoline_page_table(void)
{
#ifdef CONFIG_X86_32
	/* Copy kernel address range */
	clone_pgd_range(trampoline_pg_dir + KERNEL_PGD_BOUNDARY,
			swapper_pg_dir + KERNEL_PGD_BOUNDARY,
			KERNEL_PGD_PTRS);

	/* Initialize low mappings */
	clone_pgd_range(trampoline_pg_dir,
			swapper_pg_dir + KERNEL_PGD_BOUNDARY,
			min_t(unsigned long, KERNEL_PGD_PTRS,
			      KERNEL_PGD_BOUNDARY));
#endif
}
