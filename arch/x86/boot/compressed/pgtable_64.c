#include <asm/processor.h>

/*
 * __force_order is used by special_insns.h asm code to force instruction
 * serialization.
 *
 * It is not referenced from the code, but GCC < 5 with -fPIE would fail
 * due to an undefined symbol. Define it to make these ancient GCCs work.
 */
unsigned long __force_order;

int l5_paging_required(void)
{
	/* Check if leaf 7 is supported. */

	if (native_cpuid_eax(0) < 7)
		return 0;

	/* Check if la57 is supported. */
	if (!(native_cpuid_ecx(7) & (1 << (X86_FEATURE_LA57 & 31))))
		return 0;

	/* Check if 5-level paging has already been enabled. */
	if (native_read_cr4() & X86_CR4_LA57)
		return 0;

	return 1;
}
