/*
 * PCIe host controller driver for Texas Instruments Keystone SoCs
 *
 * Copyright (C) 2013-2014 Texas Instruments., Ltd.
 *		http://www.ti.com
 *
 * Author: Murali Karicheri <m-karicheri2@ti.com>
 * Implementation based on pci-exynos.c and pcie-designware.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/irqchip/chained_irq.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/of_irq.h>
#include <linux/of.h>
#include <linux/of_pci.h>
#include <linux/platform_device.h>
#include <linux/phy/phy.h>
#include <linux/resource.h>
#include <linux/signal.h>

#include "pcie-designware.h"
#include "pci-keystone.h"

#define DRIVER_NAME	"keystone-pcie"

/* driver specific constants */
#define MAX_MSI_HOST_IRQS		8
#define MAX_LEGACY_HOST_IRQS		4

/* DEV_STAT_CTRL */
#define PCIE_CAP_BASE		0x70

/* PCIE controller device IDs */
#define PCIE_RC_K2HK		0xb008
#define PCIE_RC_K2E		0xb009
#define PCIE_RC_K2L		0xb00a

#define to_keystone_pcie(x)	container_of(x, struct keystone_pcie, pp)

static void quirk_limit_mrrs(struct pci_dev *dev)
{
	struct pci_bus *bus = dev->bus;
	struct pci_dev *bridge = bus->self;
	static const struct pci_device_id rc_pci_devids[] = {
		{ PCI_DEVICE(PCI_VENDOR_ID_TI, PCIE_RC_K2HK),
		 .class = PCI_CLASS_BRIDGE_PCI << 8, .class_mask = ~0, },
		{ PCI_DEVICE(PCI_VENDOR_ID_TI, PCIE_RC_K2E),
		 .class = PCI_CLASS_BRIDGE_PCI << 8, .class_mask = ~0, },
		{ PCI_DEVICE(PCI_VENDOR_ID_TI, PCIE_RC_K2L),
		 .class = PCI_CLASS_BRIDGE_PCI << 8, .class_mask = ~0, },
		{ 0, },
	};

	if (pci_is_root_bus(bus))
		return;

	/* look for the host bridge */
	while (!pci_is_root_bus(bus)) {
		bridge = bus->self;
		bus = bus->parent;
	}

	if (bridge) {
		/*
		 * Keystone PCI controller has a h/w limitation of
		 * 256 bytes maximum read request size.  It can't handle
		 * anything higher than this.  So force this limit on
		 * all downstream devices.
		 */
		if (pci_match_id(rc_pci_devids, bridge)) {
			if (pcie_get_readrq(dev) > 256) {
				dev_info(&dev->dev, "limiting MRRS to 256\n");
				pcie_set_readrq(dev, 256);
			}
		}
	}
}
DECLARE_PCI_FIXUP_ENABLE(PCI_ANY_ID, PCI_ANY_ID, quirk_limit_mrrs);

static int ks_pcie_establish_link(struct keystone_pcie *ks_pcie)
{
	struct pcie_port *pp = &ks_pcie->pp;
	int count = 200;

	dw_pcie_setup_rc(pp);

	if (dw_pcie_link_up(pp)) {
		dev_err(pp->dev, "Link already up\n");
		return 0;
	}

	ks_dw_pcie_initiate_link_train(ks_pcie);
	/* check if the link is up or not */
	while (!dw_pcie_link_up(pp)) {
		usleep_range(100, 1000);
		if (--count) {
			ks_dw_pcie_initiate_link_train(ks_pcie);
			continue;
		}
		dev_err(pp->dev, "phy link never came up\n");
		return -EINVAL;
	}

	return 0;
}

static void ks_pcie_msi_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	struct keystone_pcie *ks_pcie = irq_desc_get_handler_data(desc);
	u32 offset = irq - ks_pcie->msi_host_irqs[0];
	struct pcie_port *pp = &ks_pcie->pp;
	struct irq_chip *chip = irq_desc_get_chip(desc);

	dev_dbg(pp->dev, "ks_pci_msi_irq_handler, irq %d\n", irq);

	/*
	 * The chained irq handler installation would have replaced normal
	 * interrupt driver handler so we need to take care of mask/unmask and
	 * ack operation.
	 */
	chained_irq_enter(chip, desc);
	ks_dw_pcie_handle_msi_irq(ks_pcie, offset);
	chained_irq_exit(chip, desc);
}

/**
 * ks_pcie_legacy_irq_handler() - Handle legacy interrupt
 * @irq: IRQ line for legacy interrupts
 * @desc: Pointer to irq descriptor
 *
 * Traverse through pending legacy interrupts and invoke handler for each. Also
 * takes care of interrupt controller level mask/ack operation.
 */
static void ks_pcie_legacy_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	struct keystone_pcie *ks_pcie = irq_desc_get_handler_data(desc);
	struct pcie_port *pp = &ks_pcie->pp;
	u32 irq_offset = irq - ks_pcie->legacy_host_irqs[0];
	struct irq_chip *chip = irq_desc_get_chip(desc);

	dev_dbg(pp->dev, ": Handling legacy irq %d\n", irq);

	/*
	 * The chained irq handler installation would have replaced normal
	 * interrupt driver handler so we need to take care of mask/unmask and
	 * ack operation.
	 */
	chained_irq_enter(chip, desc);
	ks_dw_pcie_handle_legacy_irq(ks_pcie, irq_offset);
	chained_irq_exit(chip, desc);
}

static int ks_pcie_get_irq_controller_info(struct keystone_pcie *ks_pcie,
					   char *controller, int *num_irqs)
{
	int temp, max_host_irqs, legacy = 1, *host_irqs, ret = -EINVAL;
	struct device *dev = ks_pcie->pp.dev;
	struct device_node *np_pcie = dev->of_node, **np_temp;

	if (!strcmp(controller, "msi-interrupt-controller"))
		legacy = 0;

	if (legacy) {
		np_temp = &ks_pcie->legacy_intc_np;
		max_host_irqs = MAX_LEGACY_HOST_IRQS;
		host_irqs = &ks_pcie->legacy_host_irqs[0];
	} else {
		np_temp = &ks_pcie->msi_intc_np;
		max_host_irqs = MAX_MSI_HOST_IRQS;
		host_irqs =  &ks_pcie->msi_host_irqs[0];
	}

	/* interrupt controller is in a child node */
	*np_temp = of_find_node_by_name(np_pcie, controller);
	if (!(*np_temp)) {
		dev_err(dev, "Node for %s is absent\n", controller);
		goto out;
	}
	temp = of_irq_count(*np_temp);
	if (!temp)
		goto out;
	if (temp > max_host_irqs)
		dev_warn(dev, "Too many %s interrupts defined %u\n",
			(legacy ? "legacy" : "MSI"), temp);

	/*
	 * support upto max_host_irqs. In dt from index 0 to 3 (legacy) or 0 to
	 * 7 (MSI)
	 */
	for (temp = 0; temp < max_host_irqs; temp++) {
		host_irqs[temp] = irq_of_parse_and_map(*np_temp, temp);
		if (host_irqs[temp] < 0)
			break;
	}
	if (temp) {
		*num_irqs = temp;
		ret = 0;
	}
out:
	return ret;
}

static void ks_pcie_setup_interrupts(struct keystone_pcie *ks_pcie)
{
	int i;

	/* Legacy IRQ */
	for (i = 0; i < ks_pcie->num_legacy_host_irqs; i++) {
		irq_set_handler_data(ks_pcie->legacy_host_irqs[i], ks_pcie);
		irq_set_chained_handler(ks_pcie->legacy_host_irqs[i],
					ks_pcie_legacy_irq_handler);
	}
	ks_dw_pcie_enable_legacy_irqs(ks_pcie);

	/* MSI IRQ */
	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		for (i = 0; i < ks_pcie->num_msi_host_irqs; i++) {
			irq_set_chained_handler(ks_pcie->msi_host_irqs[i],
						ks_pcie_msi_irq_handler);
			irq_set_handler_data(ks_pcie->msi_host_irqs[i],
					     ks_pcie);
		}
	}
}

/*
 * When a PCI device does not exist during config cycles, keystone host gets a
 * bus error instead of returning 0xffffffff. This handler always returns 0
 * for this kind of faults.
 */
static int keystone_pcie_fault(unsigned long addr, unsigned int fsr,
				struct pt_regs *regs)
{
	unsigned long instr = *(unsigned long *) instruction_pointer(regs);

	if ((instr & 0x0e100090) == 0x00100090) {
		int reg = (instr >> 12) & 15;

		regs->uregs[reg] = -1;
		regs->ARM_pc += 4;
	}

	return 0;
}

static void __init ks_pcie_host_init(struct pcie_port *pp)
{
	struct keystone_pcie *ks_pcie = to_keystone_pcie(pp);
	u32 val;

	ks_pcie_establish_link(ks_pcie);
	ks_dw_pcie_setup_rc_app_regs(ks_pcie);
	ks_pcie_setup_interrupts(ks_pcie);
	writew(PCI_IO_RANGE_TYPE_32 | (PCI_IO_RANGE_TYPE_32 << 8),
			pp->dbi_base + PCI_IO_BASE);

	/* update the Vendor ID */
	writew(ks_pcie->device_id, pp->dbi_base + PCI_DEVICE_ID);

	/* update the DEV_STAT_CTRL to publish right mrrs */
	val = readl(pp->dbi_base + PCIE_CAP_BASE + PCI_EXP_DEVCTL);
	val &= ~PCI_EXP_DEVCTL_READRQ;
	/* set the mrrs to 256 bytes */
	val |= BIT(12);
	writel(val, pp->dbi_base + PCIE_CAP_BASE + PCI_EXP_DEVCTL);

	/*
	 * PCIe access errors that result into OCP errors are caught by ARM as
	 * "External aborts"
	 */
	hook_fault_code(17, keystone_pcie_fault, SIGBUS, 0,
			"Asynchronous external abort");
}

static struct pcie_host_ops keystone_pcie_host_ops = {
	.rd_other_conf = ks_dw_pcie_rd_other_conf,
	.wr_other_conf = ks_dw_pcie_wr_other_conf,
	.link_up = ks_dw_pcie_link_up,
	.host_init = ks_pcie_host_init,
	.msi_set_irq = ks_dw_pcie_msi_set_irq,
	.msi_clear_irq = ks_dw_pcie_msi_clear_irq,
	.get_msi_addr = ks_dw_pcie_get_msi_addr,
	.msi_host_init = ks_dw_pcie_msi_host_init,
	.scan_bus = ks_dw_pcie_v3_65_scan_bus,
};

static int __init ks_add_pcie_port(struct keystone_pcie *ks_pcie,
			 struct platform_device *pdev)
{
	struct pcie_port *pp = &ks_pcie->pp;
	int ret;

	ret = ks_pcie_get_irq_controller_info(ks_pcie,
					"legacy-interrupt-controller",
					&ks_pcie->num_legacy_host_irqs);
	if (ret)
		return ret;

	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		ret = ks_pcie_get_irq_controller_info(ks_pcie,
						"msi-interrupt-controller",
						&ks_pcie->num_msi_host_irqs);
		if (ret)
			return ret;
	}

	pp->root_bus_nr = -1;
	pp->ops = &keystone_pcie_host_ops;
	ret = ks_dw_pcie_host_init(ks_pcie, ks_pcie->msi_intc_np);
	if (ret) {
		dev_err(&pdev->dev, "failed to initialize host\n");
		return ret;
	}

	return ret;
}

static const struct of_device_id ks_pcie_of_match[] = {
	{
		.type = "pci",
		.compatible = "ti,keystone-pcie",
	},
	{ },
};
MODULE_DEVICE_TABLE(of, ks_pcie_of_match);

static int __exit ks_pcie_remove(struct platform_device *pdev)
{
	struct keystone_pcie *ks_pcie = platform_get_drvdata(pdev);

	clk_disable_unprepare(ks_pcie->clk);

	return 0;
}

static int __init ks_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct keystone_pcie *ks_pcie;
	struct pcie_port *pp;
	struct resource *res;
	void __iomem *reg_p;
	struct phy *phy;
	int ret = 0;

	ks_pcie = devm_kzalloc(&pdev->dev, sizeof(*ks_pcie),
				GFP_KERNEL);
	if (!ks_pcie) {
		dev_err(dev, "no memory for keystone pcie\n");
		return -ENOMEM;
	}
	pp = &ks_pcie->pp;

	/* initialize SerDes Phy if present */
	phy = devm_phy_get(dev, "pcie-phy");
	if (!IS_ERR_OR_NULL(phy)) {
		ret = phy_init(phy);
		if (ret < 0)
			return ret;
	}

	/* index 2 is to read PCI DEVICE_ID */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	reg_p = devm_ioremap_resource(dev, res);
	if (IS_ERR(reg_p))
		return PTR_ERR(reg_p);
	ks_pcie->device_id = readl(reg_p) >> 16;
	devm_iounmap(dev, reg_p);
	devm_release_mem_region(dev, res->start, resource_size(res));

	pp->dev = dev;
	platform_set_drvdata(pdev, ks_pcie);
	ks_pcie->clk = devm_clk_get(dev, "pcie");
	if (IS_ERR(ks_pcie->clk)) {
		dev_err(dev, "Failed to get pcie rc clock\n");
		return PTR_ERR(ks_pcie->clk);
	}
	ret = clk_prepare_enable(ks_pcie->clk);
	if (ret)
		return ret;

	ret = ks_add_pcie_port(ks_pcie, pdev);
	if (ret < 0)
		goto fail_clk;

	return 0;
fail_clk:
	clk_disable_unprepare(ks_pcie->clk);

	return ret;
}

static struct platform_driver ks_pcie_driver __refdata = {
	.probe  = ks_pcie_probe,
	.remove = __exit_p(ks_pcie_remove),
	.driver = {
		.name	= "keystone-pcie",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(ks_pcie_of_match),
	},
};

module_platform_driver(ks_pcie_driver);

MODULE_AUTHOR("Murali Karicheri <m-karicheri2@ti.com>");
MODULE_DESCRIPTION("Keystone PCIe host controller driver");
MODULE_LICENSE("GPL v2");
