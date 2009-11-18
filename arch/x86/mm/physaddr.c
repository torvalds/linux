#include <linux/mmdebug.h>
#include <linux/module.h>
#include <linux/mm.h>

#include <asm/page.h>

#include "physaddr.h"

#ifdef CONFIG_X86_64

unsigned long __phys_addr(unsigned long x)
{
	if (x >= __START_KERNEL_map) {
		x -= __START_KERNEL_map;
		VIRTUAL_BUG_ON(x >= KERNEL_IMAGE_SIZE);
		x += phys_base;
	} else {
		VIRTUAL_BUG_ON(x < PAGE_OFFSET);
		x -= PAGE_OFFSET;
		VIRTUAL_BUG_ON(!phys_addr_valid(x));
	}
	return x;
}
EXPORT_SYMBOL(__phys_addr);

bool __virt_addr_valid(unsigned long x)
{
	if (x >= __START_KERNEL_map) {
		x -= __START_KERNEL_map;
		if (x >= KERNEL_IMAGE_SIZE)
			return false;
		x += phys_base;
	} else {
		if (x < PAGE_OFFSET)
			return false;
		x -= PAGE_OFFSET;
		if (!phys_addr_valid(x))
			return false;
	}

	return pfn_valid(x >> PAGE_SHIFT);
}
EXPORT_SYMBOL(__virt_addr_valid);

#else

#ifdef CONFIG_DEBUG_VIRTUAL
unsigned long __phys_addr(unsigned long x)
{
	/* VMALLOC_* aren't constants  */
	VIRTUAL_BUG_ON(x < PAGE_OFFSET);
	VIRTUAL_BUG_ON(__vmalloc_start_set && is_vmalloc_addr((void *) x));
	return x - PAGE_OFFSET;
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
