// SPDX-License-Identifier: GPL-2.0
#include <linux/pci-ecam.h>
#include <linux/delay.h>
#include <linux/of.h>

#define SMP8759_MUX		0x48
#define SMP8759_TEST_OUT	0x74

struct tango_pcie {
	void __iomem *base;
};

static int smp8759_config_read(struct pci_bus *bus, unsigned int devfn,
			       int where, int size, u32 *val)
{
	struct pci_config_window *cfg = bus->sysdata;
	struct tango_pcie *pcie = dev_get_drvdata(cfg->parent);
	int ret;

	/* Reads in configuration space outside devfn 0 return garbage */
	if (devfn != 0)
		return PCIBIOS_FUNC_NOT_SUPPORTED;

	/*
	 * PCI config and MMIO accesses are muxed.  Linux doesn't have a
	 * mutual exclusion mechanism for config vs. MMIO accesses, so
	 * concurrent accesses may cause corruption.
	 */
	writel_relaxed(1, pcie->base + SMP8759_MUX);
	ret = pci_generic_config_read(bus, devfn, where, size, val);
	writel_relaxed(0, pcie->base + SMP8759_MUX);

	return ret;
}

static int smp8759_config_write(struct pci_bus *bus, unsigned int devfn,
				int where, int size, u32 val)
{
	struct pci_config_window *cfg = bus->sysdata;
	struct tango_pcie *pcie = dev_get_drvdata(cfg->parent);
	int ret;

	writel_relaxed(1, pcie->base + SMP8759_MUX);
	ret = pci_generic_config_write(bus, devfn, where, size, val);
	writel_relaxed(0, pcie->base + SMP8759_MUX);

	return ret;
}

static struct pci_ecam_ops smp8759_ecam_ops = {
	.bus_shift	= 20,
	.pci_ops	= {
		.map_bus	= pci_ecam_map_bus,
		.read		= smp8759_config_read,
		.write		= smp8759_config_write,
	}
};

static int tango_pcie_link_up(struct tango_pcie *pcie)
{
	void __iomem *test_out = pcie->base + SMP8759_TEST_OUT;
	int i;

	writel_relaxed(16, test_out);
	for (i = 0; i < 10; ++i) {
		u32 ltssm_state = readl_relaxed(test_out) >> 8;
		if ((ltssm_state & 0x1f) == 0xf) /* L0 */
			return 1;
		usleep_range(3000, 4000);
	}

	return 0;
}

static int tango_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tango_pcie *pcie;
	struct resource *res;
	int ret;

	dev_warn(dev, "simultaneous PCI config and MMIO accesses may cause data corruption\n");
	add_taint(TAINT_CRAP, LOCKDEP_STILL_OK);

	pcie = devm_kzalloc(dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	pcie->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(pcie->base))
		return PTR_ERR(pcie->base);

	platform_set_drvdata(pdev, pcie);

	if (!tango_pcie_link_up(pcie))
		return -ENODEV;

	return pci_host_common_probe(pdev, &smp8759_ecam_ops);
}

static const struct of_device_id tango_pcie_ids[] = {
	{ .compatible = "sigma,smp8759-pcie" },
	{ },
};

static struct platform_driver tango_pcie_driver = {
	.probe	= tango_pcie_probe,
	.driver	= {
		.name = KBUILD_MODNAME,
		.of_match_table = tango_pcie_ids,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver(tango_pcie_driver);

/*
 * The root complex advertises the wrong device class.
 * Header Type 1 is for PCI-to-PCI bridges.
 */
static void tango_fixup_class(struct pci_dev *dev)
{
	dev->class = PCI_CLASS_BRIDGE_PCI << 8;
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_SIGMA, 0x0024, tango_fixup_class);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_SIGMA, 0x0028, tango_fixup_class);

/*
 * The root complex exposes a "fake" BAR, which is used to filter
 * bus-to-system accesses.  Only accesses within the range defined by this
 * BAR are forwarded to the host, others are ignored.
 *
 * By default, the DMA framework expects an identity mapping, and DRAM0 is
 * mapped at 0x80000000.
 */
static void tango_fixup_bar(struct pci_dev *dev)
{
	dev->non_compliant_bars = true;
	pci_write_config_dword(dev, PCI_BASE_ADDRESS_0, 0x80000000);
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_SIGMA, 0x0024, tango_fixup_bar);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_SIGMA, 0x0028, tango_fixup_bar);
