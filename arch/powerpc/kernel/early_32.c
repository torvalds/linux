// SPDX-License-Identifier: GPL-2.0

/*
 * Early init before relocation
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <asm/setup.h>
#include <asm/sections.h>
#include <asm/asm-prototypes.h>

/*
 * We're called here very early in the boot.
 *
 * Note that the kernel may be running at an address which is different
 * from the address that it was linked at, so we must use RELOC/PTRRELOC
 * to access static data (including strings).  -- paulus
 */
notrace unsigned long __init early_init(unsigned long dt_ptr)
{
	unsigned long kva, offset = reloc_offset();

	kva = *PTRRELOC(&kernstart_virt_addr);

	/* First zero the BSS */
	if (kva == KERNELBASE)
		memset(PTRRELOC(&__bss_start), 0, __bss_stop - __bss_start);

	/*
	 * Identify the CPU type and fix up code sections
	 * that depend on which cpu we have.
	 */
	identify_cpu(offset, mfspr(SPRN_PVR));

	apply_feature_fixups();

	return kva + offset;
}
