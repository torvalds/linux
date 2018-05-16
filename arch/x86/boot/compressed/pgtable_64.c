#include <asm/processor.h>
#include "pgtable.h"
#include "../string.h"

/*
 * __force_order is used by special_insns.h asm code to force instruction
 * serialization.
 *
 * It is not referenced from the code, but GCC < 5 with -fPIE would fail
 * due to an undefined symbol. Define it to make these ancient GCCs work.
 */
unsigned long __force_order;

#define BIOS_START_MIN		0x20000U	/* 128K, less than this is insane */
#define BIOS_START_MAX		0x9f000U	/* 640K, absolute maximum */

struct paging_config {
	unsigned long trampoline_start;
	unsigned long l5_required;
};

/* Buffer to preserve trampoline memory */
static char trampoline_save[TRAMPOLINE_32BIT_SIZE];

/*
 * Trampoline address will be printed by extract_kernel() for debugging
 * purposes.
 *
 * Avoid putting the pointer into .bss as it will be cleared between
 * paging_prepare() and extract_kernel().
 */
unsigned long *trampoline_32bit __section(.data);

struct paging_config paging_prepare(void)
{
	struct paging_config paging_config = {};
	unsigned long bios_start, ebda_start;

	/*
	 * Check if LA57 is desired and supported.
	 *
	 * There are two parts to the check:
	 *   - if the kernel supports 5-level paging: CONFIG_X86_5LEVEL=y
	 *   - if the machine supports 5-level paging:
	 *     + CPUID leaf 7 is supported
	 *     + the leaf has the feature bit set
	 *
	 * That's substitute for boot_cpu_has() in early boot code.
	 */
	if (IS_ENABLED(CONFIG_X86_5LEVEL) &&
			native_cpuid_eax(0) >= 7 &&
			(native_cpuid_ecx(7) & (1 << (X86_FEATURE_LA57 & 31)))) {
		paging_config.l5_required = 1;
	}

	/*
	 * Find a suitable spot for the trampoline.
	 * This code is based on reserve_bios_regions().
	 */

	ebda_start = *(unsigned short *)0x40e << 4;
	bios_start = *(unsigned short *)0x413 << 10;

	if (bios_start < BIOS_START_MIN || bios_start > BIOS_START_MAX)
		bios_start = BIOS_START_MAX;

	if (ebda_start > BIOS_START_MIN && ebda_start < bios_start)
		bios_start = ebda_start;

	/* Place the trampoline just below the end of low memory, aligned to 4k */
	paging_config.trampoline_start = bios_start - TRAMPOLINE_32BIT_SIZE;
	paging_config.trampoline_start = round_down(paging_config.trampoline_start, PAGE_SIZE);

	trampoline_32bit = (unsigned long *)paging_config.trampoline_start;

	/* Preserve trampoline memory */
	memcpy(trampoline_save, trampoline_32bit, TRAMPOLINE_32BIT_SIZE);

	/* Clear trampoline memory first */
	memset(trampoline_32bit, 0, TRAMPOLINE_32BIT_SIZE);

	/* Copy trampoline code in place */
	memcpy(trampoline_32bit + TRAMPOLINE_32BIT_CODE_OFFSET / sizeof(unsigned long),
			&trampoline_32bit_src, TRAMPOLINE_32BIT_CODE_SIZE);

	/*
	 * The code below prepares page table in trampoline memory.
	 *
	 * The new page table will be used by trampoline code for switching
	 * from 4- to 5-level paging or vice versa.
	 *
	 * If switching is not required, the page table is unused: trampoline
	 * code wouldn't touch CR3.
	 */

	/*
	 * We are not going to use the page table in trampoline memory if we
	 * are already in the desired paging mode.
	 */
	if (paging_config.l5_required == !!(native_read_cr4() & X86_CR4_LA57))
		goto out;

	if (paging_config.l5_required) {
		/*
		 * For 4- to 5-level paging transition, set up current CR3 as
		 * the first and the only entry in a new top-level page table.
		 */
		trampoline_32bit[TRAMPOLINE_32BIT_PGTABLE_OFFSET] = __native_read_cr3() | _PAGE_TABLE_NOENC;
	} else {
		unsigned long src;

		/*
		 * For 5- to 4-level paging transition, copy page table pointed
		 * by first entry in the current top-level page table as our
		 * new top-level page table.
		 *
		 * We cannot just point to the page table from trampoline as it
		 * may be above 4G.
		 */
		src = *(unsigned long *)__native_read_cr3() & PAGE_MASK;
		memcpy(trampoline_32bit + TRAMPOLINE_32BIT_PGTABLE_OFFSET / sizeof(unsigned long),
		       (void *)src, PAGE_SIZE);
	}

out:
	return paging_config;
}

void cleanup_trampoline(void *pgtable)
{
	void *trampoline_pgtable;

	trampoline_pgtable = trampoline_32bit + TRAMPOLINE_32BIT_PGTABLE_OFFSET;

	/*
	 * Move the top level page table out of trampoline memory,
	 * if it's there.
	 */
	if ((void *)__native_read_cr3() == trampoline_pgtable) {
		memcpy(pgtable, trampoline_pgtable, PAGE_SIZE);
		native_write_cr3((unsigned long)pgtable);
	}

	/* Restore trampoline memory */
	memcpy(trampoline_32bit, trampoline_save, TRAMPOLINE_32BIT_SIZE);
}
