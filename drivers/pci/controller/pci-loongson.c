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
#include <linux/pci-acpi.h>
#include <linux/pci-ecam.h>

#include "../pci.h"

/* Device IDs */
#define DEV_LS2K_PCIE_PORT0	0x1a05
#define DEV_LS7A_PCIE_PORT0	0x7a09
#define DEV_LS7A_PCIE_PORT1	0x7a19
#define DEV_LS7A_PCIE_PORT2	0x7a29
#define DEV_LS7A_PCIE_PORT3	0x7a39
#define DEV_LS7A_PCIE_PORT4	0x7a49
#define DEV_LS7A_PCIE_PORT5	0x7a59
#define DEV_LS7A_PCIE_PORT6	0x7a69

#define DEV_LS2K_APB	0x7a02
#define DEV_LS7A_GMAC	0x7a03
#define DEV_LS7A_DC1	0x7a06
#define DEV_LS7A_LPC	0x7a0c
#define DEV_LS7A_AHCI	0x7a08
#define DEV_LS7A_CONF	0x7a10
#define DEV_LS7A_GNET	0x7a13
#define DEV_LS7A_EHCI	0x7a14
#define DEV_LS7A_DC2	0x7a36
#define DEV_LS7A_HDMI	0x7a37

#define FLAG_CFG0	BIT(0)
#define FLAG_CFG1	BIT(1)
#define FLAG_DEV_FIX	BIT(2)
#define FLAG_DEV_HIDDEN	BIT(3)

struct loongson_pci_data {
	u32 flags;
	struct pci_ops *ops;
};

struct loongson_pci {
	void __iomem *cfg0_base;
	void __iomem *cfg1_base;
	struct platform_device *pdev;
	const struct loongson_pci_data *data;
};

/* Fixup wrong class code in PCIe bridges */
static void bridge_class_quirk(struct pci_dev *dev)
{
	dev->class = PCI_CLASS_BRIDGE_PCI_NORMAL;
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_LOONGSON,
			DEV_LS7A_PCIE_PORT0, bridge_class_quirk);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_LOONGSON,
			DEV_LS7A_PCIE_PORT1, bridge_class_quirk);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_LOONGSON,
			DEV_LS7A_PCIE_PORT2, bridge_class_quirk);

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

/*
 * Some Loongson PCIe ports have hardware limitations on their Maximum Read
 * Request Size. They can't handle anything larger than this.  Sane
 * firmware will set proper MRRS at boot, so we only need no_inc_mrrs for
 * bridges. However, some MIPS Loongson firmware doesn't set MRRS properly,
 * so we have to enforce maximum safe MRRS, which is 256 bytes.
 */
#ifdef CONFIG_MIPS
static void loongson_set_min_mrrs_quirk(struct pci_dev *pdev)
{
	struct pci_bus *bus = pdev->bus;
	struct pci_dev *bridge;
	static const struct pci_device_id bridge_devids[] = {
		{ PCI_VDEVICE(LOONGSON, DEV_LS2K_PCIE_PORT0) },
		{ PCI_VDEVICE(LOONGSON, DEV_LS7A_PCIE_PORT0) },
		{ PCI_VDEVICE(LOONGSON, DEV_LS7A_PCIE_PORT1) },
		{ PCI_VDEVICE(LOONGSON, DEV_LS7A_PCIE_PORT2) },
		{ PCI_VDEVICE(LOONGSON, DEV_LS7A_PCIE_PORT3) },
		{ PCI_VDEVICE(LOONGSON, DEV_LS7A_PCIE_PORT4) },
		{ PCI_VDEVICE(LOONGSON, DEV_LS7A_PCIE_PORT5) },
		{ PCI_VDEVICE(LOONGSON, DEV_LS7A_PCIE_PORT6) },
		{ 0, },
	};

	/* look for the matching bridge */
	while (!pci_is_root_bus(bus)) {
		bridge = bus->self;
		bus = bus->parent;

		if (pci_match_id(bridge_devids, bridge)) {
			if (pcie_get_readrq(pdev) > 256) {
				pci_info(pdev, "limiting MRRS to 256\n");
				pcie_set_readrq(pdev, 256);
			}
			break;
		}
	}
}
DECLARE_PCI_FIXUP_ENABLE(PCI_ANY_ID, PCI_ANY_ID, loongson_set_min_mrrs_quirk);
#endif

static void loongson_mrrs_quirk(struct pci_dev *pdev)
{
	struct pci_host_bridge *bridge = pci_find_host_bridge(pdev->bus);

	bridge->no_inc_mrrs = 1;
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_LOONGSON,
			DEV_LS2K_PCIE_PORT0, loongson_mrrs_quirk);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_LOONGSON,
			DEV_LS7A_PCIE_PORT0, loongson_mrrs_quirk);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_LOONGSON,
			DEV_LS7A_PCIE_PORT1, loongson_mrrs_quirk);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_LOONGSON,
			DEV_LS7A_PCIE_PORT2, loongson_mrrs_quirk);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_LOONGSON,
			DEV_LS7A_PCIE_PORT3, loongson_mrrs_quirk);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_LOONGSON,
			DEV_LS7A_PCIE_PORT4, loongson_mrrs_quirk);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_LOONGSON,
			DEV_LS7A_PCIE_PORT5, loongson_mrrs_quirk);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_LOONGSON,
			DEV_LS7A_PCIE_PORT6, loongson_mrrs_quirk);

static void loongson_pci_pin_quirk(struct pci_dev *pdev)
{
	pdev->pin = 1 + (PCI_FUNC(pdev->devfn) & 3);
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_LOONGSON,
			DEV_LS7A_DC1, loongson_pci_pin_quirk);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_LOONGSON,
			DEV_LS7A_DC2, loongson_pci_pin_quirk);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_LOONGSON,
			DEV_LS7A_GMAC, loongson_pci_pin_quirk);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_LOONGSON,
			DEV_LS7A_AHCI, loongson_pci_pin_quirk);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_LOONGSON,
			DEV_LS7A_EHCI, loongson_pci_pin_quirk);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_LOONGSON,
			DEV_LS7A_GNET, loongson_pci_pin_quirk);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_LOONGSON,
			DEV_LS7A_HDMI, loongson_pci_pin_quirk);

static struct loongson_pci *pci_bus_to_loongson_pci(struct pci_bus *bus)
{
	struct pci_config_window *cfg;

	if (acpi_disabled)
		return (struct loongson_pci *)(bus->sysdata);

	cfg = bus->sysdata;
	return (struct loongson_pci *)(cfg->priv);
}

static void __iomem *cfg0_map(struct loongson_pci *priv, struct pci_bus *bus,
			      unsigned int devfn, int where)
{
	unsigned long addroff = 0x0;
	unsigned char busnum = bus->number;

	if (!pci_is_root_bus(bus)) {
		addroff |= BIT(24); /* Type 1 Access */
		addroff |= (busnum << 16);
	}
	addroff |= (devfn << 8) | where;
	return priv->cfg0_base + addroff;
}

static void __iomem *cfg1_map(struct loongson_pci *priv, struct pci_bus *bus,
			      unsigned int devfn, int where)
{
	unsigned long addroff = 0x0;
	unsigned char busnum = bus->number;

	if (!pci_is_root_bus(bus)) {
		addroff |= BIT(28); /* Type 1 Access */
		addroff |= (busnum << 16);
	}
	addroff |= (devfn << 8) | (where & 0xff) | ((where & 0xf00) << 16);
	return priv->cfg1_base + addroff;
}

static bool pdev_may_exist(struct pci_bus *bus, unsigned int device,
			   unsigned int function)
{
	return !(pci_is_root_bus(bus) &&
		(device >= 9 && device <= 20) && (function > 0));
}

static void __iomem *pci_loongson_map_bus(struct pci_bus *bus,
					  unsigned int devfn, int where)
{
	unsigned int device = PCI_SLOT(devfn);
	unsigned int function = PCI_FUNC(devfn);
	struct loongson_pci *priv = pci_bus_to_loongson_pci(bus);

	/*
	 * Do not read more than one device on the bus other than
	 * the host bus.
	 */
	if ((priv->data->flags & FLAG_DEV_FIX) && bus->self) {
		if (!pci_is_root_bus(bus) && (device > 0))
			return NULL;
	}

	/* Don't access non-existent devices */
	if (priv->data->flags & FLAG_DEV_HIDDEN) {
		if (!pdev_may_exist(bus, device, function))
			return NULL;
	}

	/* CFG0 can only access standard space */
	if (where < PCI_CFG_SPACE_SIZE && priv->cfg0_base)
		return cfg0_map(priv, bus, devfn, where);

	/* CFG1 can access extended space */
	if (where < PCI_CFG_SPACE_EXP_SIZE && priv->cfg1_base)
		return cfg1_map(priv, bus, devfn, where);

	return NULL;
}

#ifdef CONFIG_OF

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

/* LS2K/LS7A accept 8/16/32-bit PCI config operations */
static struct pci_ops loongson_pci_ops = {
	.map_bus = pci_loongson_map_bus,
	.read	= pci_generic_config_read,
	.write	= pci_generic_config_write,
};

/* RS780/SR5690 only accept 32-bit PCI config operations */
static struct pci_ops loongson_pci_ops32 = {
	.map_bus = pci_loongson_map_bus,
	.read	= pci_generic_config_read32,
	.write	= pci_generic_config_write32,
};

static const struct loongson_pci_data ls2k_pci_data = {
	.flags = FLAG_CFG1 | FLAG_DEV_FIX | FLAG_DEV_HIDDEN,
	.ops = &loongson_pci_ops,
};

static const struct loongson_pci_data ls7a_pci_data = {
	.flags = FLAG_CFG1 | FLAG_DEV_FIX | FLAG_DEV_HIDDEN,
	.ops = &loongson_pci_ops,
};

static const struct loongson_pci_data rs780e_pci_data = {
	.flags = FLAG_CFG0,
	.ops = &loongson_pci_ops32,
};

static const struct of_device_id loongson_pci_of_match[] = {
	{ .compatible = "loongson,ls2k-pci",
		.data = &ls2k_pci_data, },
	{ .compatible = "loongson,ls7a-pci",
		.data = &ls7a_pci_data, },
	{ .compatible = "loongson,rs780e-pci",
		.data = &rs780e_pci_data, },
	{}
};

static int loongson_pci_probe(struct platform_device *pdev)
{
	struct loongson_pci *priv;
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct pci_host_bridge *bridge;
	struct resource *regs;

	if (!node)
		return -ENODEV;

	bridge = devm_pci_alloc_host_bridge(dev, sizeof(*priv));
	if (!bridge)
		return -ENODEV;

	priv = pci_host_bridge_priv(bridge);
	priv->pdev = pdev;
	priv->data = of_device_get_match_data(dev);

	if (priv->data->flags & FLAG_CFG0) {
		regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (!regs)
			dev_err(dev, "missing mem resources for cfg0\n");
		else {
			priv->cfg0_base = devm_pci_remap_cfg_resource(dev, regs);
			if (IS_ERR(priv->cfg0_base))
				return PTR_ERR(priv->cfg0_base);
		}
	}

	if (priv->data->flags & FLAG_CFG1) {
		regs = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		if (!regs)
			dev_info(dev, "missing mem resource for cfg1\n");
		else {
			priv->cfg1_base = devm_pci_remap_cfg_resource(dev, regs);
			if (IS_ERR(priv->cfg1_base))
				priv->cfg1_base = NULL;
		}
	}

	bridge->sysdata = priv;
	bridge->ops = priv->data->ops;
	bridge->map_irq = loongson_map_irq;

	return pci_host_probe(bridge);
}

static struct platform_driver loongson_pci_driver = {
	.driver = {
		.name = "loongson-pci",
		.of_match_table = loongson_pci_of_match,
	},
	.probe = loongson_pci_probe,
};
builtin_platform_driver(loongson_pci_driver);

#endif

#ifdef CONFIG_ACPI

static int loongson_pci_ecam_init(struct pci_config_window *cfg)
{
	struct device *dev = cfg->parent;
	struct loongson_pci *priv;
	struct loongson_pci_data *data;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	cfg->priv = priv;
	data->flags = FLAG_CFG1 | FLAG_DEV_HIDDEN;
	priv->data = data;
	priv->cfg1_base = cfg->win - (cfg->busr.start << 16);

	return 0;
}

const struct pci_ecam_ops loongson_pci_ecam_ops = {
	.bus_shift = 16,
	.init	   = loongson_pci_ecam_init,
	.pci_ops   = {
		.map_bus = pci_loongson_map_bus,
		.read	 = pci_generic_config_read,
		.write	 = pci_generic_config_write,
	}
};

#endif
