/*
 *  linux/arch/arm/mm/nommu.c
 *
 * ARM uCLinux supporting functions.
 */
#include <linux/module.h>

#include <asm/io.h>
#include <asm/page.h>

void __iomem *__ioremap_pfn(unsigned long pfn, unsigned long offset,
			    size_t size, unsigned long flags)
{
	if (pfn >= (0x100000000ULL >> PAGE_SHIFT))
		return NULL;
	return (void __iomem *) (offset + (pfn << PAGE_SHIFT));
}
EXPORT_SYMBOL(__ioremap_pfn);

void __iomem *__ioremap(unsigned long phys_addr, size_t size,
			unsigned long flags)
{
	return (void __iomem *)phys_addr;
}
EXPORT_SYMBOL(__ioremap);

void __iounmap(void __iomem *addr)
{
}
EXPORT_SYMBOL(__iounmap);
