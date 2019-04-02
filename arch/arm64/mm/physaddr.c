// SPDX-License-Identifier: GPL-2.0
#include <linux/.h>
#include <linux/export.h>
#include <linux/types.h>
#include <linux/mmde.h>
#include <linux/mm.h>

#include <asm/memory.h>

phys_addr_t __virt_to_phys(unsigned long x)
{
	WARN(!__is_lm_address(x),
	     "virt_to_phys used for non-linear address: %pK (%pS)\n",
	      (void *)x,
	      (void *)x);

	return __virt_to_phys_node(x);
}
EXPORT_SYMBOL(__virt_to_phys);

phys_addr_t __phys_addr_symbol(unsigned long x)
{
	/*
	 * This is bounds checking against the kernel image only.
	 * __pa_symbol should only be used on kernel symbol addresses.
	 */
	VIRTUAL__ON(x < (unsigned long) KERNEL_START ||
		       x > (unsigned long) KERNEL_END);
	return __pa_symbol_node(x);
}
EXPORT_SYMBOL(__phys_addr_symbol);
