// SPDX-License-Identifier: GPL-2.0

#include <linux/types.h>
#include <linux/mmdebug.h>
#include <linux/mm.h>
#include <asm/page.h>
#include <asm/sections.h>

phys_addr_t __virt_to_phys(unsigned long x)
{
	phys_addr_t y = x - PAGE_OFFSET;

	/*
	 * Boundary checking aginst the kernel linear mapping space.
	 */
	WARN(y >= KERN_VIRT_SIZE,
	     "virt_to_phys used for non-linear address: %pK (%pS)\n",
	     (void *)x, (void *)x);

	return __va_to_pa_nodebug(x);
}
EXPORT_SYMBOL(__virt_to_phys);

phys_addr_t __phys_addr_symbol(unsigned long x)
{
	unsigned long kernel_start = (unsigned long)PAGE_OFFSET;
	unsigned long kernel_end = (unsigned long)_end;

	/*
	 * Boundary checking aginst the kernel image mapping.
	 * __pa_symbol should only be used on kernel symbol addresses.
	 */
	VIRTUAL_BUG_ON(x < kernel_start || x > kernel_end);

	return __va_to_pa_nodebug(x);
}
EXPORT_SYMBOL(__phys_addr_symbol);
