// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * IO workarounds for PCI on Celleb/Cell platform
 *
 * (C) Copyright 2006-2007 TOSHIBA CORPORATION
 */

#undef DEBUG

#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/io.h>

#include <asm/ppc-pci.h>
#include <asm/pci-bridge.h>
#include <asm/io-workarounds.h>

#define SPIDER_PCI_DISABLE_PREFETCH

struct spiderpci_iowa_private {
	void __iomem *regs;
};

static void spiderpci_io_flush(struct iowa_bus *bus)
{
	struct spiderpci_iowa_private *priv;

	priv = bus->private;
	in_be32(priv->regs + SPIDER_PCI_DUMMY_READ);
	iosync();
}

#define SPIDER_PCI_MMIO_READ(name, ret)					\
static ret spiderpci_##name(const PCI_IO_ADDR addr)			\
{									\
	ret val = __do_##name(addr);					\
	spiderpci_io_flush(iowa_mem_find_bus(addr));			\
	return val;							\
}

#define SPIDER_PCI_MMIO_READ_STR(name)					\
static void spiderpci_##name(const PCI_IO_ADDR addr, void *buf, 	\
			     unsigned long count)			\
{									\
	__do_##name(addr, buf, count);					\
	spiderpci_io_flush(iowa_mem_find_bus(addr));			\
}

SPIDER_PCI_MMIO_READ(readb, u8)
SPIDER_PCI_MMIO_READ(readw, u16)
SPIDER_PCI_MMIO_READ(readl, u32)
SPIDER_PCI_MMIO_READ(readq, u64)
SPIDER_PCI_MMIO_READ(readw_be, u16)
SPIDER_PCI_MMIO_READ(readl_be, u32)
SPIDER_PCI_MMIO_READ(readq_be, u64)
SPIDER_PCI_MMIO_READ_STR(readsb)
SPIDER_PCI_MMIO_READ_STR(readsw)
SPIDER_PCI_MMIO_READ_STR(readsl)

static void spiderpci_memcpy_fromio(void *dest, const PCI_IO_ADDR src,
				    unsigned long n)
{
	__do_memcpy_fromio(dest, src, n);
	spiderpci_io_flush(iowa_mem_find_bus(src));
}

static int __init spiderpci_pci_setup_chip(struct pci_controller *phb,
					   void __iomem *regs)
{
	void *dummy_page_va;
	dma_addr_t dummy_page_da;

#ifdef SPIDER_PCI_DISABLE_PREFETCH
	u32 val = in_be32(regs + SPIDER_PCI_VCI_CNTL_STAT);
	pr_debug("SPIDER_IOWA:PVCI_Control_Status was 0x%08x\n", val);
	out_be32(regs + SPIDER_PCI_VCI_CNTL_STAT, val | 0x8);
#endif /* SPIDER_PCI_DISABLE_PREFETCH */

	/* setup dummy read */
	/*
	 * On CellBlade, we can't know that which XDR memory is used by
	 * kmalloc() to allocate dummy_page_va.
	 * In order to improve the performance, the XDR which is used to
	 * allocate dummy_page_va is the nearest the spider-pci.
	 * We have to select the CBE which is the nearest the spider-pci
	 * to allocate memory from the best XDR, but I don't know that
	 * how to do.
	 *
	 * Celleb does not have this problem, because it has only one XDR.
	 */
	dummy_page_va = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!dummy_page_va) {
		pr_err("SPIDERPCI-IOWA:Alloc dummy_page_va failed.\n");
		return -1;
	}

	dummy_page_da = dma_map_single(phb->parent, dummy_page_va,
				       PAGE_SIZE, DMA_FROM_DEVICE);
	if (dma_mapping_error(phb->parent, dummy_page_da)) {
		pr_err("SPIDER-IOWA:Map dummy page filed.\n");
		kfree(dummy_page_va);
		return -1;
	}

	out_be32(regs + SPIDER_PCI_DUMMY_READ_BASE, dummy_page_da);

	return 0;
}

int __init spiderpci_iowa_init(struct iowa_bus *bus, void *data)
{
	void __iomem *regs = NULL;
	struct spiderpci_iowa_private *priv;
	struct device_node *np = bus->phb->dn;
	struct resource r;
	unsigned long offset = (unsigned long)data;

	pr_debug("SPIDERPCI-IOWA:Bus initialize for spider(%pOF)\n",
		 np);

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		pr_err("SPIDERPCI-IOWA:"
		       "Can't allocate struct spiderpci_iowa_private");
		return -1;
	}

	if (of_address_to_resource(np, 0, &r)) {
		pr_err("SPIDERPCI-IOWA:Can't get resource.\n");
		goto error;
	}

	regs = ioremap(r.start + offset, SPIDER_PCI_REG_SIZE);
	if (!regs) {
		pr_err("SPIDERPCI-IOWA:ioremap failed.\n");
		goto error;
	}
	priv->regs = regs;
	bus->private = priv;

	if (spiderpci_pci_setup_chip(bus->phb, regs))
		goto error;

	return 0;

error:
	kfree(priv);
	bus->private = NULL;

	if (regs)
		iounmap(regs);

	return -1;
}

struct ppc_pci_io spiderpci_ops = {
	.readb = spiderpci_readb,
	.readw = spiderpci_readw,
	.readl = spiderpci_readl,
	.readq = spiderpci_readq,
	.readw_be = spiderpci_readw_be,
	.readl_be = spiderpci_readl_be,
	.readq_be = spiderpci_readq_be,
	.readsb = spiderpci_readsb,
	.readsw = spiderpci_readsw,
	.readsl = spiderpci_readsl,
	.memcpy_fromio = spiderpci_memcpy_fromio,
};

