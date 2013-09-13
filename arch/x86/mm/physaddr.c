#include <linux/bootmem.h>
#include <linux/mmdebug.h>
#include <linux/module.h>
#include <linux/mm.h>

#include <asm/page.h>

#include "physaddr.h"

#ifdef CONFIG_X86_64

#ifdef CONFIG_DEBUG_VIRTUAL
unsigned long __phys_addr(unsigned long x)
{
	unsigned long y = x - __START_KERNEL_map;

	/* use the carry flag to determine if x was < __START_KERNEL_map */
	if (unlikely(x > y)) {
		x = y + phys_base;

		VIRTUAL_BUG_ON(y >= KERNEL_IMAGE_SIZE);
	} else {
		x = y + (__START_KERNEL_map - PAGE_OFFSET);

		/* carry flag will be set if starting x was >= PAGE_OFFSET */
		VIRTUAL_BUG_ON((x > y) || !phys_addr_valid(x));
	}

	return x;
}
EXPORT_SYMBOL(__phys_addr);

unsigned long __phys_addr_symbol(unsigned long x)
{
	unsigned long y = x - __START_KERNEL_map;

	/* only check upper bounds since lower bounds will trigger carry */
	VIRTUAL_BUG_ON(y >= KERNEL_IMAGE_SIZE);

	return y + phys_base;
}
EXPORT_SYMBOL(__phys_addr_symbol);
#endif

bool __virt_addr_valid(unsigned long x)
{
	unsigned long y = x - __START_KERNEL_map;

	/* use the carry flag to determine if x was < __START_KERNEL_map */
	if (unlikely(x > y)) {
		x = y + phys_base;

		if (y >= KERNEL_IMAGE_SIZE)
			return false;
	} else {
		x = y + (__START_KERNEL_map - PAGE_OFFSET);

		/* carry flag will be set if starting x was >= PAGE_OFFSET */
		if ((x > y) || !phys_addr_valid(x))
			return false;
	}

	return pfn_valid(x >> PAGE_SHIFT);
}
EXPORT_SYMBOL(__virt_addr_valid);

#else

#ifdef CONFIG_DEBUG_VIRTUAL
unsigned long __phys_addr(unsigned long x)
{
	unsigned long phys_addr = x - PAGE_OFFSET;
	/* VMALLOC_* aren't constants  */
	VIRTUAL_BUG_ON(x < PAGE_OFFSET);
	VIRTUAL_BUG_ON(__vmalloc_start_set && is_vmalloc_addr((void *) x));
	/* max_low_pfn is set early, but not _that_ early */
	if (max_low_pfn) {
		VIRTUAL_BUG_ON((phys_addr >> PAGE_SHIFT) > max_low_pfn);
		BUG_ON(slow_virt_to_phys((void *)x) != phys_addr);
	}
	return phys_addr;
}
EXPORT_SYMBOL(__phys_addr);
#endif

bool __virt_addr_valid(unsigned long x)
{
	if (x < PAGE_OFFSET)
		return false;
	if (__vmalloc_start_set && is_vmalloc_addr((void *) x))
		return false;
	if (x >= FIXADDR_START)
		return false;
	return pfn_valid((x - PAGE_OFFSET) >> PAGE_SHIFT);
}
EXPORT_SYMBOL(__virt_addr_valid);

#endif	/* CONFIG_X86_64 */
