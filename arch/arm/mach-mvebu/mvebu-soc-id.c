/*
 * ID and revision information for mvebu SoCs
 *
 * Copyright (C) 2014 Marvell
 *
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * All the mvebu SoCs have information related to their variant and
 * revision that can be read from the PCI control register. This is
 * done before the PCI initialization to avoid any conflict. Once the
 * ID and revision are retrieved, the mapping is freed.
 */

#define pr_fmt(fmt) "mvebu-soc-id: " fmt

#include <linux/clk.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/sys_soc.h>
#include "common.h"
#include "mvebu-soc-id.h"

#define PCIE_DEV_ID_OFF		0x0
#define PCIE_DEV_REV_OFF	0x8

#define SOC_ID_MASK	    0xFFFF0000
#define SOC_REV_MASK	    0xFF

static u32 soc_dev_id;
static u32 soc_rev;
static bool is_id_valid;

static const struct of_device_id mvebu_pcie_of_match_table[] = {
	{ .compatible = "marvell,armada-xp-pcie", },
	{ .compatible = "marvell,armada-370-pcie", },
	{ .compatible = "marvell,kirkwood-pcie" },
	{},
};

int mvebu_get_soc_id(u32 *dev, u32 *rev)
{
	if (is_id_valid) {
		*dev = soc_dev_id;
		*rev = soc_rev;
		return 0;
	} else
		return -ENODEV;
}

static int __init get_soc_id_by_pci(void)
{
	struct device_node *np;
	int ret = 0;
	void __iomem *pci_base;
	struct clk *clk;
	struct device_node *child;

	np = of_find_matching_node(NULL, mvebu_pcie_of_match_table);
	if (!np)
		return ret;

	/*
	 * ID and revision are available from any port, so we
	 * just pick the first one
	 */
	child = of_get_next_child(np, NULL);
	if (child == NULL) {
		pr_err("cannot get pci node\n");
		ret = -ENOMEM;
		goto clk_err;
	}

	clk = of_clk_get_by_name(child, NULL);
	if (IS_ERR(clk)) {
		pr_err("cannot get clock\n");
		ret = -ENOMEM;
		goto clk_err;
	}

	ret = clk_prepare_enable(clk);
	if (ret) {
		pr_err("cannot enable clock\n");
		goto clk_err;
	}

	pci_base = of_iomap(child, 0);
	if (pci_base == NULL) {
		pr_err("cannot map registers\n");
		ret = -ENOMEM;
		goto res_ioremap;
	}

	/* SoC ID */
	soc_dev_id = readl(pci_base + PCIE_DEV_ID_OFF) >> 16;

	/* SoC revision */
	soc_rev = readl(pci_base + PCIE_DEV_REV_OFF) & SOC_REV_MASK;

	is_id_valid = true;

	pr_info("MVEBU SoC ID=0x%X, Rev=0x%X\n", soc_dev_id, soc_rev);

	iounmap(pci_base);

res_ioremap:
	/*
	 * If the PCIe unit is actually enabled and we have PCI
	 * support in the kernel, we intentionally do not release the
	 * reference to the clock. We want to keep it running since
	 * the bootloader does some PCIe link configuration that the
	 * kernel is for now unable to do, and gating the clock would
	 * make us loose this precious configuration.
	 */
	if (!of_device_is_available(child) || !IS_ENABLED(CONFIG_PCI_MVEBU)) {
		clk_disable_unprepare(clk);
		clk_put(clk);
	}

clk_err:
	of_node_put(child);
	of_node_put(np);

	return ret;
}

static int __init mvebu_soc_id_init(void)
{

	/*
	 * First try to get the ID and the revision by the system
	 * register and use PCI registers only if it is not possible
	 */
	if (!mvebu_system_controller_get_soc_id(&soc_dev_id, &soc_rev)) {
		is_id_valid = true;
		pr_info("MVEBU SoC ID=0x%X, Rev=0x%X\n", soc_dev_id, soc_rev);
		return 0;
	}

	return get_soc_id_by_pci();
}
early_initcall(mvebu_soc_id_init);

static int __init mvebu_soc_device(void)
{
	struct soc_device_attribute *soc_dev_attr;
	struct soc_device *soc_dev;

	/* Also protects against running on non-mvebu systems */
	if (!is_id_valid)
		return 0;

	soc_dev_attr = kzalloc(sizeof(*soc_dev_attr), GFP_KERNEL);
	if (!soc_dev_attr)
		return -ENOMEM;

	soc_dev_attr->family = kasprintf(GFP_KERNEL, "Marvell");
	soc_dev_attr->revision = kasprintf(GFP_KERNEL, "%X", soc_rev);
	soc_dev_attr->soc_id = kasprintf(GFP_KERNEL, "%X", soc_dev_id);

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev)) {
		kfree(soc_dev_attr->family);
		kfree(soc_dev_attr->revision);
		kfree(soc_dev_attr->soc_id);
		kfree(soc_dev_attr);
	}

	return 0;
}
postcore_initcall(mvebu_soc_device);
