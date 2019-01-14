// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2004 Koninklijke Philips Electronics NV
 *
 * Conversion to platform driver and DT:
 * Copyright 2014 Linaro Ltd.
 *
 * 14/04/2005 Initial version, colin.king@philips.com
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/platform_device.h>

#include "../pci.h"

static void __iomem *versatile_pci_base;
static void __iomem *versatile_cfg_base[2];

#define PCI_IMAP(m)		(versatile_pci_base + ((m) * 4))
#define PCI_SMAP(m)		(versatile_pci_base + 0x14 + ((m) * 4))
#define PCI_SELFID		(versatile_pci_base + 0xc)

#define VP_PCI_DEVICE_ID		0x030010ee
#define VP_PCI_CLASS_ID			0x0b400000

static u32 pci_slot_ignore;

static int __init versatile_pci_slot_ignore(char *str)
{
	int retval;
	int slot;

	while ((retval = get_option(&str, &slot))) {
		if ((slot < 0) || (slot > 31))
			pr_err("Illegal slot value: %d\n", slot);
		else
			pci_slot_ignore |= (1 << slot);
	}
	return 1;
}
__setup("pci_slot_ignore=", versatile_pci_slot_ignore);


static void __iomem *versatile_map_bus(struct pci_bus *bus,
				       unsigned int devfn, int offset)
{
	unsigned int busnr = bus->number;

	if (pci_slot_ignore & (1 << PCI_SLOT(devfn)))
		return NULL;

	return versatile_cfg_base[1] + ((busnr << 16) | (devfn << 8) | offset);
}

static struct pci_ops pci_versatile_ops = {
	.map_bus = versatile_map_bus,
	.read	= pci_generic_config_read32,
	.write	= pci_generic_config_write,
};

static int versatile_pci_parse_request_of_pci_ranges(struct device *dev,
						     struct list_head *res)
{
	int err, mem = 1, res_valid = 0;
	resource_size_t iobase;
	struct resource_entry *win, *tmp;

	err = devm_of_pci_get_host_bridge_resources(dev, 0, 0xff, res, &iobase);
	if (err)
		return err;

	err = devm_request_pci_bus_resources(dev, res);
	if (err)
		goto out_release_res;

	resource_list_for_each_entry_safe(win, tmp, res) {
		struct resource *res = win->res;

		switch (resource_type(res)) {
		case IORESOURCE_IO:
			err = devm_pci_remap_iospace(dev, res, iobase);
			if (err) {
				dev_warn(dev, "error %d: failed to map resource %pR\n",
					 err, res);
				resource_list_destroy_entry(win);
			}
			break;
		case IORESOURCE_MEM:
			res_valid |= !(res->flags & IORESOURCE_PREFETCH);

			writel(res->start >> 28, PCI_IMAP(mem));
			writel(PHYS_OFFSET >> 28, PCI_SMAP(mem));
			mem++;

			break;
		}
	}

	if (res_valid)
		return 0;

	dev_err(dev, "non-prefetchable memory resource required\n");
	err = -EINVAL;

out_release_res:
	pci_free_resource_list(res);
	return err;
}

static int versatile_pci_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	int ret, i, myslot = -1;
	u32 val;
	void __iomem *local_pci_cfg_base;
	struct pci_bus *bus, *child;
	struct pci_host_bridge *bridge;
	LIST_HEAD(pci_res);

	bridge = devm_pci_alloc_host_bridge(dev, 0);
	if (!bridge)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	versatile_pci_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(versatile_pci_base))
		return PTR_ERR(versatile_pci_base);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	versatile_cfg_base[0] = devm_ioremap_resource(dev, res);
	if (IS_ERR(versatile_cfg_base[0]))
		return PTR_ERR(versatile_cfg_base[0]);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	versatile_cfg_base[1] = devm_pci_remap_cfg_resource(dev, res);
	if (IS_ERR(versatile_cfg_base[1]))
		return PTR_ERR(versatile_cfg_base[1]);

	ret = versatile_pci_parse_request_of_pci_ranges(dev, &pci_res);
	if (ret)
		return ret;

	/*
	 * We need to discover the PCI core first to configure itself
	 * before the main PCI probing is performed
	 */
	for (i = 0; i < 32; i++) {
		if ((readl(versatile_cfg_base[0] + (i << 11) + PCI_VENDOR_ID) == VP_PCI_DEVICE_ID) &&
		    (readl(versatile_cfg_base[0] + (i << 11) + PCI_CLASS_REVISION) == VP_PCI_CLASS_ID)) {
			myslot = i;
			break;
		}
	}
	if (myslot == -1) {
		dev_err(dev, "Cannot find PCI core!\n");
		return -EIO;
	}
	/*
	 * Do not to map Versatile FPGA PCI device into memory space
	 */
	pci_slot_ignore |= (1 << myslot);

	dev_info(dev, "PCI core found (slot %d)\n", myslot);

	writel(myslot, PCI_SELFID);
	local_pci_cfg_base = versatile_cfg_base[1] + (myslot << 11);

	val = readl(local_pci_cfg_base + PCI_COMMAND);
	val |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER | PCI_COMMAND_INVALIDATE;
	writel(val, local_pci_cfg_base + PCI_COMMAND);

	/*
	 * Configure the PCI inbound memory windows to be 1:1 mapped to SDRAM
	 */
	writel(PHYS_OFFSET, local_pci_cfg_base + PCI_BASE_ADDRESS_0);
	writel(PHYS_OFFSET, local_pci_cfg_base + PCI_BASE_ADDRESS_1);
	writel(PHYS_OFFSET, local_pci_cfg_base + PCI_BASE_ADDRESS_2);

	/*
	 * For many years the kernel and QEMU were symbiotically buggy
	 * in that they both assumed the same broken IRQ mapping.
	 * QEMU therefore attempts to auto-detect old broken kernels
	 * so that they still work on newer QEMU as they did on old
	 * QEMU. Since we now use the correct (ie matching-hardware)
	 * IRQ mapping we write a definitely different value to a
	 * PCI_INTERRUPT_LINE register to tell QEMU that we expect
	 * real hardware behaviour and it need not be backwards
	 * compatible for us. This write is harmless on real hardware.
	 */
	writel(0, versatile_cfg_base[0] + PCI_INTERRUPT_LINE);

	pci_add_flags(PCI_ENABLE_PROC_DOMAINS);
	pci_add_flags(PCI_REASSIGN_ALL_BUS);

	list_splice_init(&pci_res, &bridge->windows);
	bridge->dev.parent = dev;
	bridge->sysdata = NULL;
	bridge->busnr = 0;
	bridge->ops = &pci_versatile_ops;
	bridge->map_irq = of_irq_parse_and_map_pci;
	bridge->swizzle_irq = pci_common_swizzle;

	ret = pci_scan_root_bus_bridge(bridge);
	if (ret < 0)
		return ret;

	bus = bridge->bus;

	pci_assign_unassigned_bus_resources(bus);
	list_for_each_entry(child, &bus->children, node)
		pcie_bus_configure_settings(child);
	pci_bus_add_devices(bus);

	return 0;
}

static const struct of_device_id versatile_pci_of_match[] = {
	{ .compatible = "arm,versatile-pci", },
	{ },
};
MODULE_DEVICE_TABLE(of, versatile_pci_of_match);

static struct platform_driver versatile_pci_driver = {
	.driver = {
		.name = "versatile-pci",
		.of_match_table = versatile_pci_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = versatile_pci_probe,
};
module_platform_driver(versatile_pci_driver);

MODULE_DESCRIPTION("Versatile PCI driver");
MODULE_LICENSE("GPL v2");
