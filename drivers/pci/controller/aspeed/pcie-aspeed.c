// SPDX-License-Identifier: GPL-2.0+
/*
 * PCIe host controller driver for ASPEED PCIe Bridge
 *
 */

#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>

#include <linux/interrupt.h>
#include <linux/irqchip/chained_irq.h>

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/msi.h>

#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#include "h2x-ast2600.h"

/*	PCI Host Controller registers */
#define ASPEED_PCIE_CLASS_CODE		0x04
#define ASPEED_PCIE_GLOBAL			0x30
#define ASPEED_PCIE_CFG_DIN			0x50
#define ASPEED_PCIE_CFG3			0x58
#define ASPEED_PCIE_LOCK			0x7C
#define ASPEED_PCIE_LINK			0xC0
#define ASPEED_PCIE_INT				0xC4
#define ASPEED_PCIE_LINK_STS		0xD0
/*	AST_PCIE_CFG2			0x04 */
#define PCIE_CFG_CLASS_CODE(x)	(x << 8)
#define PCIE_CFG_REV_ID(x)		(x)
/*	PEHR10: Miscellaneous Control 10H Register */
#define DATALINK_REPORT_CAPABLE	BIT(4)
/*	PEHR14: Miscellaneous Control 14H Register */
#define HOTPLUG_CAPABLE_ENABLE	BIT(6)
#define HOTPLUG_SURPRISE_ENABLE	BIT(5)
#define ATTENTION_BUTTON_ENALBE	BIT(0)
/*	PEHR30: Miscellaneous Control 30H Register */
/* Disable RC synchronous reset when link up to link down*/
#define RC_SYNC_RESET_DISABLE	BIT(20)
#define ROOT_COMPLEX_ID(x)		(x << 4)
#define PCIE_RC_SLOT_ENABLE		BIT(1)
/*	AST_PCIE_LOCK			0x7C */
#define PCIE_UNLOCK				0xa8
/*	AST_PCIE_LINK			0xC0 */
#define PCIE_LINK_STS			BIT(5)
/*  ASPEED_PCIE_LINK_STS	0xD0 */
#define PCIE_LINK_5G			BIT(17)
#define PCIE_LINK_2_5G			BIT(16)

static DECLARE_BITMAP(msi_irq_in_use, MAX_MSI_HOST_IRQS);

static int aspeed_wait_for_link(struct aspeed_pcie *pcie)
{
	/* Don't register host if link is down */
	if (readl(pcie->pciereg_base + ASPEED_PCIE_LINK) & PCIE_LINK_STS) {
		aspeed_h2x_set_slot_power_limit(pcie);

		if (readl(pcie->pciereg_base
				+ ASPEED_PCIE_LINK_STS) & PCIE_LINK_5G)
			dev_info(pcie->dev, "PCIE- Link up : 5G\n");
		if (readl(pcie->pciereg_base
				+ ASPEED_PCIE_LINK_STS) & PCIE_LINK_2_5G)
			dev_info(pcie->dev, "PCIE- Link up : 2.5G\n");
	} else {
		dev_info(pcie->dev, "PCIE- Link down\n");
	}
	return 0;
}

static struct irq_chip aspeed_leg_irq_chip = {
	.name = "ASPEED:IntX",
	.irq_ack = aspeed_h2x_intx_ack_irq,
	.irq_mask = aspeed_h2x_intx_mask_irq,
	.irq_unmask = aspeed_h2x_intx_unmask_irq,
};

static int aspeed_pcie_intx_map(struct irq_domain *domain, unsigned int irq,
			  irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &aspeed_leg_irq_chip, handle_level_irq);
	irq_set_chip_data(irq, domain->host_data);
	irq_set_status_flags(irq, IRQ_LEVEL);

	return 0;
}


/* INTx IRQ Domain operations */
static const struct irq_domain_ops aspeed_intx_domain_ops = {
	.map = aspeed_pcie_intx_map,
};

static irqreturn_t aspeed_pcie_intr_handler(int irq, void *data)
{
	struct aspeed_pcie *pcie = (struct aspeed_pcie *)data;

	aspeed_h2x_rc_intr_handler(pcie);
	return IRQ_HANDLED;
}

/* PCIe operations */
static struct pci_ops aspeed_pcie_ops = {
	.read	= aspeed_h2x_rd_conf,
	.write	= aspeed_h2x_wr_conf,
};

#ifdef CONFIG_PCI_MSI
static void aspeed_msi_compose_msi_msg(struct irq_data *data, struct msi_msg *msg)
{
	struct aspeed_pcie *pcie = irq_data_get_irq_chip_data(data);

	msg->address_hi = 0;
	msg->address_lo = pcie->msi_address;
	msg->data = data->hwirq;

}

static int aspeed_msi_set_affinity(struct irq_data *irq_data,
				 const struct cpumask *mask, bool force)
{
	return -EINVAL;
}

static struct irq_chip aspeed_msi_bottom_irq_chip = {
	.name = "ASPEED MSI",
	.irq_compose_msi_msg = aspeed_msi_compose_msi_msg,
	.irq_set_affinity = aspeed_msi_set_affinity,
};

static int aspeed_irq_msi_domain_alloc(struct irq_domain *domain, unsigned int virq,
											unsigned int nr_irqs, void *args)
{
	int hwirq;

	hwirq = find_first_zero_bit(msi_irq_in_use, MAX_MSI_HOST_IRQS);
	if (hwirq >= MAX_MSI_HOST_IRQS)
		return -ENOSPC;

	set_bit(hwirq, msi_irq_in_use);
	irq_domain_set_info(domain, virq, hwirq,
						&aspeed_msi_bottom_irq_chip,
						domain->host_data, handle_simple_irq,
						NULL, NULL);
	return hwirq;

}

static void aspeed_irq_msi_domain_free(struct irq_domain *domain, unsigned int virq,
											unsigned int nr_irqs)
{
	struct irq_data *d = irq_domain_get_irq_data(domain, virq);

	clear_bit(d->hwirq, msi_irq_in_use);
}

static const struct irq_domain_ops aspeed_msi_domain_ops = {
	.alloc  = aspeed_irq_msi_domain_alloc,
	.free   = aspeed_irq_msi_domain_free,
};

static struct irq_chip aspeed_msi_irq_chip = {
	.name = "PCIe MSI",
	.irq_enable = pci_msi_unmask_irq,
	.irq_disable = pci_msi_mask_irq,
	.irq_mask = pci_msi_mask_irq,
	.irq_unmask = pci_msi_unmask_irq,
};

static struct msi_domain_info aspeed_msi_domain_info = {
	.flags = (MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
				MSI_FLAG_PCI_MSIX),

	.chip = &aspeed_msi_irq_chip,
};

#endif

static int aspeed_pcie_init_irq_domain(struct aspeed_pcie *pcie)
{
	struct device *dev = pcie->dev;
	struct device_node *node = dev->of_node;
	struct device_node *pcie_intc_node;
#ifdef CONFIG_PCI_MSI
	struct fwnode_handle *fwnode = dev_fwnode(pcie->dev);
	struct irq_domain *parent;
#endif

	/* Setup INTx */
	pcie_intc_node = of_get_next_child(node, NULL);
	if (!pcie_intc_node) {
		dev_err(dev, "No PCIe Intc node found\n");
		return -ENODEV;
	}

	pcie->leg_domain = irq_domain_add_linear(pcie_intc_node, PCI_NUM_INTX, &aspeed_intx_domain_ops, pcie);

	if (!pcie->leg_domain) {
		dev_err(dev, "failed to get an INTx IRQ domain\n");
		return -ENOMEM;
	}

	of_node_put(pcie_intc_node);

#ifdef CONFIG_PCI_MSI
	pcie->dev_domain = irq_domain_add_linear(NULL, MAX_MSI_HOST_IRQS, &aspeed_msi_domain_ops, pcie);
	if (!pcie->dev_domain) {
		dev_err(pcie->dev, "failed to create IRQ domain\n");
		return -ENOMEM;
	}

	pcie->msi_domain = pci_msi_create_irq_domain(fwnode, &aspeed_msi_domain_info, pcie->dev_domain);
	if (!pcie->msi_domain) {
		dev_err(pcie->dev, "failed to create MSI domain\n");
		irq_domain_remove(parent);
		return -ENOMEM;
	}
	aspeed_h2x_msi_enable(pcie);
#endif

	return 0;
}

#define AHBC_UNLOCK	0xAEED1A03
static int aspeed_pcie_bridge_init(struct aspeed_pcie *pcie)
{
	//scu init
	reset_control_assert(pcie->h2x_reset);
	reset_control_deassert(pcie->h2x_reset);

	//init
	writel(0x1, pcie->h2xreg_base + 0x00);

	//ahb to pcie rc
	writel(0xe0006000, pcie->h2xreg_base + 0x60);
	writel(0x00000000, pcie->h2xreg_base + 0x64);
	writel(0xFFFFFFFF, pcie->h2xreg_base + 0x68);

	reset_control_assert(pcie->reset);
	mdelay(50);
	reset_control_deassert(pcie->reset);
	mdelay(50);

	//ahbc remap enable
	regmap_write(pcie->ahbc, 0x00, AHBC_UNLOCK);
	regmap_update_bits(pcie->ahbc, 0x8C, BIT(5), BIT(5));
	regmap_write(pcie->ahbc, 0x00, 0x1);

	aspeed_h2x_rc_init(pcie);

	//plda init
	writel(PCIE_UNLOCK, pcie->pciereg_base + ASPEED_PCIE_LOCK);
//	writel(PCIE_CFG_CLASS_CODE(0x60000) | PCIE_CFG_REV_ID(4),
//				pcie->pciereg_base + ASPEED_PCIE_CLASS_CODE);
#ifdef CONFIG_HOTPLUG_PCI
	writel(RC_SYNC_RESET_DISABLE | ROOT_COMPLEX_ID(0x3) | PCIE_RC_SLOT_ENABLE, pcie->pciereg_base + ASPEED_PCIE_GLOBAL);
	writel(0xd7040022 | DATALINK_REPORT_CAPABLE, pcie->pciereg_base + 0x10);
	writel(HOTPLUG_CAPABLE_ENABLE | HOTPLUG_SURPRISE_ENABLE | ATTENTION_BUTTON_ENALBE, pcie->pciereg_base + 0x14);
#else
	writel(ROOT_COMPLEX_ID(0x3), pcie->pciereg_base + ASPEED_PCIE_GLOBAL);
#endif

	aspeed_wait_for_link(pcie);


	return 0;
}

static int aspeed_pcie_parse_dt(struct aspeed_pcie *pcie,
			     struct platform_device *pdev)
{
	struct device *dev = pcie->dev;
	struct device_node *node = dev->of_node;
	struct resource *res;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "pciereg");
	pcie->pciereg_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(pcie->pciereg_base))
		return PTR_ERR(pcie->pciereg_base);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "h2xreg");
	pcie->h2xreg_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(pcie->h2xreg_base))
		return PTR_ERR(pcie->h2xreg_base);

	pcie->ahbc = syscon_regmap_lookup_by_compatible("aspeed,aspeed-ahbc");
	if (IS_ERR(pcie->ahbc))
		return IS_ERR(pcie->ahbc);

	pcie->irq_pcie = platform_get_irq_byname(pdev, "pcie");
	if (pcie->irq_pcie < 0)
		return pcie->irq_pcie;

	pcie->irq_h2x = platform_get_irq_byname(pdev, "h2x");
	if (pcie->irq_h2x < 0)
		return pcie->irq_h2x;

	of_property_read_u32(node, "rc_offset", &pcie->rc_offset);
	pcie->h2x_rc_base = (void *)((u32)pcie->h2xreg_base + pcie->rc_offset);

	of_property_read_u32(node, "msi_address", &pcie->msi_address);

	pcie->reset = reset_control_get_exclusive(dev, "pcie");
	if (IS_ERR(pcie->reset)) {
		dev_err(&pdev->dev, "can't get pcie reset\n");
		return PTR_ERR(pcie->reset);
	}

	pcie->h2x_reset = reset_control_get_exclusive(dev, "h2x");
	if (IS_ERR(pcie->h2x_reset)) {
		dev_err(&pdev->dev, "can't get h2x reset\n");
		return PTR_ERR(pcie->h2x_reset);
	}

	return 0;
}

static const struct of_device_id aspeed_pcie_of_match[] = {
	{ .compatible = "aspeed,ast2600-pcie", },
	{}
};

static int aspeed_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pci_host_bridge *bridge;
	struct aspeed_pcie *pcie;
	int err;

	bridge = devm_pci_alloc_host_bridge(dev, sizeof(*pcie));
	if (!bridge)
		return -ENODEV;

	pcie = pci_host_bridge_priv(bridge);
	pcie->dev = dev;
	pcie->txTag = 0;


	err = aspeed_pcie_parse_dt(pcie, pdev);
	if (err) {
		dev_err(dev, "Parsing DT failed\n");
		return err;
	}


	err = aspeed_pcie_bridge_init(pcie);
	if (err) {
		dev_err(dev, "HW Initialization failed\n");
		return err;
	}

	err = aspeed_pcie_init_irq_domain(pcie);
	if (err) {
		dev_err(dev, "Failed creating IRQ Domain\n");
		return err;
	}

	pci_add_flags(PCI_REASSIGN_ALL_BUS);

	bridge->sysdata = pcie;
	bridge->ops = &aspeed_pcie_ops;

	err = devm_request_irq(dev, pcie->irq_pcie, aspeed_pcie_intr_handler,
						   0,
						   "aspeed-pcie", pcie);
	if (err) {
		dev_err(dev, "unable to request irq %d\n", pcie->irq_pcie);
		return err;
	}

	return pci_host_probe(bridge);
}

static struct platform_driver aspeed_pcie_driver = {
	.driver = {
		.name = "aspeed-pcie",
		.suppress_bind_attrs = true,
		.of_match_table = aspeed_pcie_of_match,
	},
	.probe = aspeed_pcie_probe,
};
builtin_platform_driver(aspeed_pcie_driver);
