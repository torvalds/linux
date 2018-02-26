#include <asm/processor.h>

/*
 * __force_order is used by special_insns.h asm code to force instruction
 * serialization.
 *
 * It is not referenced from the code, but GCC < 5 with -fPIE would fail
 * due to an undefined symbol. Define it to make these ancient GCCs work.
 */
unsigned long __force_order;

struct paging_config {
	unsigned long trampoline_start;
	unsigned long l5_required;
};

struct paging_config paging_prepare(void)
{
	struct paging_config paging_config = {};

	/* Check if LA57 is desired and supported */
	if (IS_ENABLED(CONFIG_X86_5LEVEL) && native_cpuid_eax(0) >= 7 &&
			(native_cpuid_ecx(7) & (1 << (X86_FEATURE_LA57 & 31))))
		paging_config.l5_required = 1;

	return paging_config;
}
