// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/vgaarb.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <asm/cacheflush.h>
#include <asm/loongson.h>

#define PCI_DEVICE_ID_LOONGSON_HOST     0x7a00
#define PCI_DEVICE_ID_LOONGSON_DC1      0x7a06
#define PCI_DEVICE_ID_LOONGSON_DC2      0x7a36
#define PCI_DEVICE_ID_LOONGSON_DC3      0x7a46
#define PCI_DEVICE_ID_LOONGSON_GPU1     0x7a15
#define PCI_DEVICE_ID_LOONGSON_GPU2     0x7a25
#define PCI_DEVICE_ID_LOONGSON_GPU3     0x7a35

int raw_pci_read(unsigned int domain, unsigned int bus, unsigned int devfn,
						int reg, int len, u32 *val)
{
	struct pci_bus *bus_tmp = pci_find_bus(domain, bus);

	if (bus_tmp)
		return bus_tmp->ops->read(bus_tmp, devfn, reg, len, val);
	return -EINVAL;
}

int raw_pci_write(unsigned int domain, unsigned int bus, unsigned int devfn,
						int reg, int len, u32 val)
{
	struct pci_bus *bus_tmp = pci_find_bus(domain, bus);

	if (bus_tmp)
		return bus_tmp->ops->write(bus_tmp, devfn, reg, len, val);
	return -EINVAL;
}

phys_addr_t mcfg_addr_init(int node)
{
	return (((u64)node << 44) | MCFG_EXT_PCICFG_BASE);
}

static int __init pcibios_init(void)
{
	unsigned int lsize;

	/*
	 * Set PCI cacheline size to that of the last level in the
	 * cache hierarchy.
	 */
	lsize = cpu_last_level_cache_line_size();

	if (lsize) {
		pci_dfl_cache_line_size = lsize >> 2;

		pr_debug("PCI: pci_cache_line_size set to %d bytes\n", lsize);
	}

	return 0;
}

subsys_initcall(pcibios_init);

int pcibios_device_add(struct pci_dev *dev)
{
	int id;
	struct irq_domain *dom;

	id = pci_domain_nr(dev->bus);
	dom = irq_find_matching_fwnode(get_pch_msi_handle(id), DOMAIN_BUS_PCI_MSI);
	dev_set_msi_domain(&dev->dev, dom);

	return 0;
}

int pcibios_alloc_irq(struct pci_dev *dev)
{
	if (acpi_disabled)
		return 0;
	if (pci_dev_msi_enabled(dev))
		return 0;
	return acpi_pci_irq_enable(dev);
}

static void pci_fixup_vgadev(struct pci_dev *pdev)
{
	struct pci_dev *devp = NULL;

	while ((devp = pci_get_class(PCI_CLASS_DISPLAY_VGA << 8, devp))) {
		if (devp->vendor != PCI_VENDOR_ID_LOONGSON) {
			vga_set_default_device(devp);
			dev_info(&pdev->dev,
				"Overriding boot device as %X:%X\n",
				devp->vendor, devp->device);
		}
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_LOONGSON, PCI_DEVICE_ID_LOONGSON_DC1, pci_fixup_vgadev);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_LOONGSON, PCI_DEVICE_ID_LOONGSON_DC2, pci_fixup_vgadev);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_LOONGSON, PCI_DEVICE_ID_LOONGSON_DC3, pci_fixup_vgadev);

#define CRTC_NUM_MAX		2
#define CRTC_OUTPUT_ENABLE	0x100

static void loongson_gpu_fixup_dma_hang(struct pci_dev *pdev, bool on)
{
	u32 i, val, count, crtc_offset, device;
	void __iomem *crtc_reg, *base, *regbase;
	static u32 crtc_status[CRTC_NUM_MAX] = { 0 };

	base = pdev->bus->ops->map_bus(pdev->bus, pdev->devfn + 1, 0);
	device = readw(base + PCI_DEVICE_ID);

	regbase = ioremap(readq(base + PCI_BASE_ADDRESS_0) & ~0xffull, SZ_64K);
	if (!regbase) {
		pci_err(pdev, "Failed to ioremap()\n");
		return;
	}

	switch (device) {
	case PCI_DEVICE_ID_LOONGSON_DC2:
		crtc_reg = regbase + 0x1240;
		crtc_offset = 0x10;
		break;
	case PCI_DEVICE_ID_LOONGSON_DC3:
		crtc_reg = regbase;
		crtc_offset = 0x400;
		break;
	default:
		iounmap(regbase);
		return;
	}

	for (i = 0; i < CRTC_NUM_MAX; i++, crtc_reg += crtc_offset) {
		val = readl(crtc_reg);

		if (!on)
			crtc_status[i] = val;

		/* No need to fixup if the status is off at startup. */
		if (!(crtc_status[i] & CRTC_OUTPUT_ENABLE))
			continue;

		if (on)
			val |= CRTC_OUTPUT_ENABLE;
		else
			val &= ~CRTC_OUTPUT_ENABLE;

		mb();
		writel(val, crtc_reg);

		for (count = 0; count < 40; count++) {
			val = readl(crtc_reg) & CRTC_OUTPUT_ENABLE;
			if ((on && val) || (!on && !val))
				break;
			udelay(1000);
		}

		pci_info(pdev, "DMA hang fixup at reg[0x%lx]: 0x%x\n",
				(unsigned long)crtc_reg & 0xffff, readl(crtc_reg));
	}

	iounmap(regbase);
}

static void pci_fixup_dma_hang_early(struct pci_dev *pdev)
{
	loongson_gpu_fixup_dma_hang(pdev, false);
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_LOONGSON, PCI_DEVICE_ID_LOONGSON_GPU2, pci_fixup_dma_hang_early);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_LOONGSON, PCI_DEVICE_ID_LOONGSON_GPU3, pci_fixup_dma_hang_early);

static void pci_fixup_dma_hang_final(struct pci_dev *pdev)
{
	loongson_gpu_fixup_dma_hang(pdev, true);
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_LOONGSON, PCI_DEVICE_ID_LOONGSON_GPU2, pci_fixup_dma_hang_final);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_LOONGSON, PCI_DEVICE_ID_LOONGSON_GPU3, pci_fixup_dma_hang_final);
