// SPDX-License-Identifier: GPL-2.0
/*
 * Loongson PCI Host Controller Driver
 *
 * Copyright (C) 2020 Jiaxun Yang <jiaxun.yang@flygoat.com>
 */

#include <linux/of_device.h>
#include <linux/of_pci.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>

#include "../pci.h"

/* Device IDs */
#define DEV_PCIE_PORT_0	0x7a09
#define DEV_PCIE_PORT_1	0x7a19
#define DEV_PCIE_PORT_2	0x7a29

#define DEV_LS2K_APB	0x7a02
#define DEV_LS7A_CONF	0x7a10
#define DEV_LS7A_LPC	0x7a0c

#define FLAG_CFG0	BIT(0)
#define FLAG_CFG1	BIT(1)
#define FLAG_DEV_FIX	BIT(2)

struct loongson_pci {
	void __iomem *cfg0_base;
	void __iomem *cfg1_base;
	struct platform_device *pdev;
	u32 flags;
};

/* Fixup wrong class code in PCIe bridges */
static void bridge_class_quirk(struct pci_dev *dev)
{
	dev->class = PCI_CLASS_BRIDGE_PCI << 8;
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_LOONGSON,
			DEV_PCIE_PORT_0, bridge_class_quirk);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_LOONGSON,
			DEV_PCIE_PORT_1, bridge_class_quirk);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_LOONGSON,
			DEV_PCIE_PORT_2, bridge_class_quirk);

static void system_bus_quirk(struct pci_dev *pdev)
{
	/*
	 * The address space consumed by these devices is outside the
	 * resources of the host bridge.
	 */
	pdev->mmio_always_on = 1;
	pdev->non_compliant_bars = 1;
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_LOONGSON,
			DEV_LS2K_APB, system_bus_quirk);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_LOONGSON,
			DEV_LS7A_CONF, system_bus_quirk);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_LOONGSON,
			DEV_LS7A_LPC, system_bus_quirk);

static void loongson_mrrs_quirk(struct pci_dev *dev)
{
	struct pci_bus *bus = dev->bus;
	struct pci_dev *bridge;
	static const struct pci_device_id bridge_devids[] = {
		{ PCI_VDEVICE(LOONGSON, DEV_PCIE_PORT_0) },
		{ PCI_VDEVICE(LOONGSON, DEV_PCIE_PORT_1) },
		{ PCI_VDEVICE(LOONGSON, DEV_PCIE_PORT_2) },
		{ 0, },
	};

	/* look for the matching bridge */
	while (!pci_is_root_bus(bus)) {
		bridge = bus->self;
		bus = bus->parent;
		/*
		 * Some Loongson PCIe ports have a h/w limitation of
		 * 256 bytes maximum read request size. They can't handle
		 * anything larger than this. So force this limit on
		 * any devices attached under these ports.
		 */
		if (pci_match_id(bridge_devids, bridge)) {
			if (pcie_get_readrq(dev) > 256) {
				pci_info(dev, "limiting MRRS to 256\n");
				pcie_set_readrq(dev, 256);
			}
			break;
		}
	}
}
DECLARE_PCI_FIXUP_ENABLE(PCI_ANY_ID, PCI_ANY_ID, loongson_mrrs_quirk);

static void __iomem *cfg1_map(struct loongson_pci *priv, int bus,
				unsigned int devfn, int where)
{
	unsigned long addroff = 0x0;

	if (bus != 0)
		addroff |= BIT(28); /* Type 1 Access */
	addroff |= (where & 0xff) | ((where & 0xf00) << 16);
	addroff |= (bus << 16) | (devfn << 8);
	return priv->cfg1_base + addroff;
}

static void __iomem *cfg0_map(struct loongson_pci *priv, int bus,
				unsigned int devfn, int where)
{
	unsigned long addroff = 0x0;

	if (bus != 0)
		addroff |= BIT(24); /* Type 1 Access */
	addroff |= (bus << 16) | (devfn << 8) | where;
	return priv->cfg0_base + addroff;
}

static void __iomem *pci_loongson_map_bus(struct pci_bus *bus, unsigned int devfn,
			       int where)
{
	unsigned char busnum = bus->number;
	struct pci_host_bridge *bridge = pci_find_host_bridge(bus);
	struct loongson_pci *priv =  pci_host_bridge_priv(bridge);

	/*
	 * Do not read more than one device on the bus other than
	 * the host bus. For our hardware the root bus is always bus 0.
	 */
	if (priv->flags & FLAG_DEV_FIX && busnum != 0 &&
		PCI_SLOT(devfn) > 0)
		return NULL;

	/* CFG0 can only access standard space */
	if (where < PCI_CFG_SPACE_SIZE && priv->cfg0_base)
		return cfg0_map(priv, busnum, devfn, where);

	/* CFG1 can access extended space */
	if (where < PCI_CFG_SPACE_EXP_SIZE && priv->cfg1_base)
		return cfg1_map(priv, busnum, devfn, where);

	return NULL;
}

static int loongson_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	int irq;
	u8 val;

	irq = of_irq_parse_and_map_pci(dev, slot, pin);
	if (irq > 0)
		return irq;

	/* Care i8259 legacy systems */
	pci_read_config_byte(dev, PCI_INTERRUPT_LINE, &val);
	/* i8259 only have 15 IRQs */
	if (val > 15)
		return 0;

	return val;
}

/* H/w only accept 32-bit PCI operations */
static struct pci_ops loongson_pci_ops = {
	.map_bus = pci_loongson_map_bus,
	.read	= pci_generic_config_read32,
	.write	= pci_generic_config_write32,
};

static const struct of_device_id loongson_pci_of_match[] = {
	{ .compatible = "loongson,ls2k-pci",
		.data = (void *)(FLAG_CFG0 | FLAG_CFG1 | FLAG_DEV_FIX), },
	{ .compatible = "loongson,ls7a-pci",
		.data = (void *)(FLAG_CFG0 | FLAG_CFG1 | FLAG_DEV_FIX), },
	{ .compatible = "loongson,rs780e-pci",
		.data = (void *)(FLAG_CFG0), },
	{}
};

static int loongson_pci_probe(struct platform_device *pdev)
{
	struct loongson_pci *priv;
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct pci_host_bridge *bridge;
	struct resource *regs;
	int err;

	if (!node)
		return -ENODEV;

	bridge = devm_pci_alloc_host_bridge(dev, sizeof(*priv));
	if (!bridge)
		return -ENODEV;

	priv = pci_host_bridge_priv(bridge);
	priv->pdev = pdev;
	priv->flags = (unsigned long)of_device_get_match_data(dev);

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs) {
		dev_err(dev, "missing mem resources for cfg0\n");
		return -EINVAL;
	}

	priv->cfg0_base = devm_pci_remap_cfg_resource(dev, regs);
	if (IS_ERR(priv->cfg0_base))
		return PTR_ERR(priv->cfg0_base);

	/* CFG1 is optional */
	if (priv->flags & FLAG_CFG1) {
		regs = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		if (!regs)
			dev_info(dev, "missing mem resource for cfg1\n");
		else {
			priv->cfg1_base = devm_pci_remap_cfg_resource(dev, regs);
			if (IS_ERR(priv->cfg1_base))
				priv->cfg1_base = NULL;
		}
	}

	err = pci_parse_request_of_pci_ranges(dev, &bridge->windows,
						&bridge->dma_ranges, NULL);
	if (err) {
		dev_err(dev, "failed to get bridge resources\n");
		return err;
	}

	bridge->dev.parent = dev;
	bridge->sysdata = priv;
	bridge->ops = &loongson_pci_ops;
	bridge->map_irq = loongson_map_irq;

	err = pci_host_probe(bridge);
	if (err)
		return err;

	return 0;
}

static struct platform_driver loongson_pci_driver = {
	.driver = {
		.name = "loongson-pci",
		.of_match_table = loongson_pci_of_match,
	},
	.probe = loongson_pci_probe,
};
builtin_platform_driver(loongson_pci_driver);
