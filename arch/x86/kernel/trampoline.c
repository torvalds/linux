#include <linux/io.h>

#include <asm/trampoline.h>
#include <asm/e820.h>

/* ready for x86_64 and x86 */
unsigned char *trampoline_base = __va(TRAMPOLINE_BASE);

void __init reserve_trampoline_memory(void)
{
#ifdef CONFIG_X86_32
	/*
	 * But first pinch a few for the stack/trampoline stuff
	 * FIXME: Don't need the extra page at 4K, but need to fix
	 * trampoline before removing it. (see the GDT stuff)
	 */
	reserve_early(PAGE_SIZE, PAGE_SIZE + PAGE_SIZE, "EX TRAMPOLINE");
#endif
	/* Has to be in very low memory so we can execute real-mode AP code. */
	reserve_early(TRAMPOLINE_BASE, TRAMPOLINE_BASE + TRAMPOLINE_SIZE,
			"TRAMPOLINE");
}

/*
 * Currently trivial. Write the real->protected mode
 * bootstrap into the page concerned. The caller
 * has made sure it's suitably aligned.
 */
unsigned long setup_trampoline(void)
{
	memcpy(trampoline_base, trampoline_data, TRAMPOLINE_SIZE);
	return virt_to_phys(trampoline_base);
}
