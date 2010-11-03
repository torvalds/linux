#include <linux/io.h>
#include <linux/memblock.h>

#include <asm/trampoline.h>
#include <asm/pgtable.h>

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
	phys_addr_t mem;

	/* Has to be in very low memory so we can execute real-mode AP code. */
	mem = memblock_find_in_range(0, 1<<20, TRAMPOLINE_SIZE, PAGE_SIZE);
	if (mem == MEMBLOCK_ERROR)
		panic("Cannot allocate trampoline\n");

	trampoline_base = __va(mem);
	memblock_x86_reserve_range(mem, mem + TRAMPOLINE_SIZE, "TRAMPOLINE");
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
