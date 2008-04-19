#include <linux/io.h>

#include <asm/trampoline.h>

/* ready for x86_64, no harm for x86, since it will overwrite after alloc */
unsigned char *trampoline_base = __va(TRAMPOLINE_BASE);

/*
 * Currently trivial. Write the real->protected mode
 * bootstrap into the page concerned. The caller
 * has made sure it's suitably aligned.
 */
unsigned long setup_trampoline(void)
{
	memcpy(trampoline_base, trampoline_data,
	       trampoline_end - trampoline_data);
	return virt_to_phys(trampoline_base);
}
