// SPDX-License-Identifier: GPL-2.0-only
#include <linux/io.h>
#include <ioremap.h>

void __iomem *ioremap_prot(phys_addr_t offset, unsigned long size,
			   pgprot_t prot)
{
	unsigned long flags = pgprot_val(prot) & _CACHE_MASK;
	u64 base = (flags == _CACHE_UNCACHED ? IO_BASE : UNCAC_BASE);
	void __iomem *addr;

	addr = plat_ioremap(offset, size, flags);
	if (!addr)
		addr = (void __iomem *)(unsigned long)(base + offset);
	return addr;
}
EXPORT_SYMBOL(ioremap_prot);

void iounmap(const volatile void __iomem *addr)
{
	plat_iounmap(addr);
}
EXPORT_SYMBOL(iounmap);
