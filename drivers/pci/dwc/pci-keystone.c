// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe host controller driver for Texas Instruments Keystone SoCs
 *
 * Copyright (C) 2013-2014 Texas Instruments., Ltd.
 *		http://www.ti.com
 *
 * Author: Murali Karicheri <m-karicheri2@ti.com>
 * Implementation based on pci-exynos.c and pcie-designware.c
 */

#include <linux/irqchip/chained_irq.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/init.h>
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

/* DEV_STAT_CTRL */
#define PCIE_CAP_BASE		0x70

/* PCIE controller device IDs */
#define PCIE_RC_K2HK		0xb008
#define PCIE_RC_K2E		0xb009
#define PCIE_RC_K2L		0xb00a

#define to_keystone_pcie(x)	dev_get_drvdata((x)->dev)

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
	struct dw_pcie *pci = ks_pcie->pci;
	struct pcie_port *pp = &pci->pp;
	struct device *dev = pci->dev;
	unsigned int retries;

	dw_pcie_setup_rc(pp);

	if (dw_pcie_link_up(pci)) {
		dev_err(dev, "Link already up\n");
		return 0;
	}

	/* check if the link is up or not */
	for (retries = 0; retries < 5; retries++) {
		ks_dw_pcie_initiate_link_train(ks_pcie);
		if (!dw_pcie_wait_for_link(pci))
			return 0;
	}

	dev_err(dev, "phy link never came up\n");
	return -ETIMEDOUT;
}

static void ks_pcie_msi_irq_handler(struct irq_desc *desc)
{
	unsigned int irq = irq_desc_get_irq(desc);
	struct keystone_pcie *ks_pcie = irq_desc_get_handler_data(desc);
	u32 offset = irq - ks_pcie->msi_host_irqs[0];
	struct dw_pcie *pci = ks_pcie->pci;
	struct device *dev = pci->dev;
	struct irq_chip *chip = irq_desc_get_chip(desc);

	dev_dbg(dev, "%s, irq %d\n", __func__, irq);

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
static void ks_pcie_legacy_irq_handler(struct irq_desc *desc)
{
	unsigned int irq = irq_desc_get_irq(desc);
	struct keystone_pcie *ks_pcie = irq_desc_get_handler_data(desc);
	struct dw_pcie *pci = ks_pcie->pci;
	struct device *dev = pci->dev;
	u32 irq_offset = irq - ks_pcie->legacy_host_irqs[0];
	struct irq_chip *chip = irq_desc_get_chip(desc);

	dev_dbg(dev, ": Handling legacy irq %d\n", irq);

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
	int temp, max_host_irqs, legacy = 1, *host_irqs;
	struct device *dev = ks_pcie->pci->dev;
	struct device_node *np_pcie = dev->of_node, **np_temp;

	if (!strcmp(controller, "msi-interrupt-controller"))
		legacy = 0;

	if (legacy) {
		np_temp = &ks_pcie->legacy_intc_np;
		max_host_irqs = PCI_NUM_INTX;
		host_irqs = &ks_pcie->legacy_host_irqs[0];
	} else {
		np_temp = &ks_pcie->msi_intc_np;
		max_host_irqs = MAX_MSI_HOST_IRQS;
		host_irqs =  &ks_pcie->msi_host_irqs[0];
	}

	/* interrupt controller is in a child node */
	*np_temp = of_get_child_by_name(np_pcie, controller);
	if (!(*np_temp)) {
		dev_err(dev, "Node for %s is absent\n", controller);
		return -EINVAL;
	}

	temp = of_irq_count(*np_temp);
	if (!temp) {
		dev_err(dev, "No IRQ entries in %s\n", controller);
		of_node_put(*np_temp);
		return -EINVAL;
	}

	if (temp > max_host_irqs)
		dev_warn(dev, "Too many %s interrupts defined %u\n",
			(legacy ? "legacy" : "MSI"), temp);

	/*
	 * support upto max_host_irqs. In dt from index 0 to 3 (legacy) or 0 to
	 * 7 (MSI)
	 */
	for (temp = 0; temp < max_host_irqs; temp++) {
		host_irqs[temp] = irq_of_parse_and_map(*np_temp, temp);
		if (!host_irqs[temp])
			break;
	}

	of_node_put(*np_temp);

	if (temp) {
		*num_irqs = temp;
		return 0;
	}

	return -EINVAL;
}

static void ks_pcie_setup_interrupts(struct keystone_pcie *ks_pcie)
{
	int i;

	/* Legacy IRQ */
	for (i = 0; i < ks_pcie->num_legacy_host_irqs; i++) {
		irq_set_chained_handler_and_data(ks_pcie->legacy_host_irqs[i],
						 ks_pcie_legacy_irq_handler,
						 ks_pcie);
	}
	ks_dw_pcie_enable_legacy_irqs(ks_pcie);

	/* MSI IRQ */
	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		for (i = 0; i < ks_pcie->num_msi_host_irqs; i++) {
			irq_set_chained_handler_and_data(ks_pcie->msi_host_irqs[i],
							 ks_pcie_msi_irq_handler,
							 ks_pcie);
		}
	}

	if (ks_pcie->error_irq > 0)
		ks_dw_pcie_enable_error_irq(ks_pcie);
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

static int __init ks_pcie_host_init(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct keystone_pcie *ks_pcie = to_keystone_pcie(pci);
	u32 val;

	ks_pcie_establish_link(ks_pcie);
	ks_dw_pcie_setup_rc_app_regs(ks_pcie);
	ks_pcie_setup_interrupts(ks_pcie);
	writew(PCI_IO_RANGE_TYPE_32 | (PCI_IO_RANGE_TYPE_32 << 8),
			pci->dbi_base + PCI_IO_BASE);

	/* update the Vendor ID */
	writew(ks_pcie->device_id, pci->dbi_base + PCI_DEVICE_ID);

	/* update the DEV_STAT_CTRL to publish right mrrs */
	val = readl(pci->dbi_base + PCIE_CAP_BASE + PCI_EXP_DEVCTL);
	val &= ~PCI_EXP_DEVCTL_READRQ;
	/* set the mrrs to 256 bytes */
	val |= BIT(12);
	writel(val, pci->dbi_base + PCIE_CAP_BASE + PCI_EXP_DEVCTL);

	/*
	 * PCIe access errors that result into OCP errors are caught by ARM as
	 * "External aborts"
	 */
	hook_fault_code(17, keystone_pcie_fault, SIGBUS, 0,
			"Asynchronous external abort");

	return 0;
}

static const struct dw_pcie_host_ops keystone_pcie_host_ops = {
	.rd_other_conf = ks_dw_pcie_rd_other_conf,
	.wr_other_conf = ks_dw_pcie_wr_other_conf,
	.host_init = ks_pcie_host_init,
	.msi_set_irq = ks_dw_pcie_msi_set_irq,
	.msi_clear_irq = ks_dw_pcie_msi_clear_irq,
	.get_msi_addr = ks_dw_pcie_get_msi_addr,
	.msi_host_init = ks_dw_pcie_msi_host_init,
	.scan_bus = ks_dw_pcie_v3_65_scan_bus,
};

static irqreturn_t pcie_err_irq_handler(int irq, void *priv)
{
	struct keystone_pcie *ks_pcie = priv;

	return ks_dw_pcie_handle_error_irq(ks_pcie);
}

static int __init ks_add_pcie_port(struct keystone_pcie *ks_pcie,
			 struct platform_device *pdev)
{
	struct dw_pcie *pci = ks_pcie->pci;
	struct pcie_port *pp = &pci->pp;
	struct device *dev = &pdev->dev;
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

	/*
	 * Index 0 is the platform interrupt for error interrupt
	 * from RC.  This is optional.
	 */
	ks_pcie->error_irq = irq_of_parse_and_map(ks_pcie->np, 0);
	if (ks_pcie->error_irq <= 0)
		dev_info(dev, "no error IRQ defined\n");
	else {
		ret = request_irq(ks_pcie->error_irq, pcie_err_irq_handler,
				  IRQF_SHARED, "pcie-error-irq", ks_pcie);
		if (ret < 0) {
			dev_err(dev, "failed to request error IRQ %d\n",
				ks_pcie->error_irq);
			return ret;
		}
	}

	pp->root_bus_nr = -1;
	pp->ops = &keystone_pcie_host_ops;
	ret = ks_dw_pcie_host_init(ks_pcie, ks_pcie->msi_intc_np);
	if (ret) {
		dev_err(dev, "failed to initialize host\n");
		return ret;
	}

	return 0;
}

static const struct of_device_id ks_pcie_of_match[] = {
	{
		.type = "pci",
		.compatible = "ti,keystone-pcie",
	},
	{ },
};

static const struct dw_pcie_ops dw_pcie_ops = {
	.link_up = ks_dw_pcie_link_up,
};

static int __exit ks_pcie_remove(struct platform_device *pdev)
{
	struct keystone_pcie *ks_pcie = platform_get_drvdata(pdev);

	clk_disable_unprepare(ks_pcie->clk);

	return 0;
}

static int __init ks_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dw_pcie *pci;
	struct keystone_pcie *ks_pcie;
	struct resource *res;
	void __iomem *reg_p;
	struct phy *phy;
	int ret;

	ks_pcie = devm_kzalloc(dev, sizeof(*ks_pcie), GFP_KERNEL);
	if (!ks_pcie)
		return -ENOMEM;

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	pci->dev = dev;
	pci->ops = &dw_pcie_ops;

	ks_pcie->pci = pci;

	/* initialize SerDes Phy if present */
	phy = devm_phy_get(dev, "pcie-phy");
	if (PTR_ERR_OR_ZERO(phy) == -EPROBE_DEFER)
		return PTR_ERR(phy);

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

	ks_pcie->np = dev->of_node;
	platform_set_drvdata(pdev, ks_pcie);
	ks_pcie->clk = devm_clk_get(dev, "pcie");
	if (IS_ERR(ks_pcie->clk)) {
		dev_err(dev, "Failed to get pcie rc clock\n");
		return PTR_ERR(ks_pcie->clk);
	}
	ret = clk_prepare_enable(ks_pcie->clk);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, ks_pcie);

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
		.of_match_table = of_match_ptr(ks_pcie_of_match),
	},
};
builtin_platform_driver(ks_pcie_driver);
