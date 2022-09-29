// SPDX-License-Identifier: GPL-2.0-only
/*
 * Freescale MU used as MSI controller
 *
 * Copyright (c) 2018 Pengutronix, Oleksij Rempel <o.rempel@pengutronix.de>
 * Copyright 2022 NXP
 *	Frank Li <Frank.Li@nxp.com>
 *	Peng Fan <peng.fan@nxp.com>
 *
 * Based on drivers/mailbox/imx-mailbox.c
 */

#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>
#include <linux/pm_domain.h>
#include <linux/spinlock.h>

#define IMX_MU_CHANS            4

enum imx_mu_xcr {
	IMX_MU_GIER,
	IMX_MU_GCR,
	IMX_MU_TCR,
	IMX_MU_RCR,
	IMX_MU_xCR_MAX,
};

enum imx_mu_xsr {
	IMX_MU_SR,
	IMX_MU_GSR,
	IMX_MU_TSR,
	IMX_MU_RSR,
	IMX_MU_xSR_MAX
};

enum imx_mu_type {
	IMX_MU_V2 = BIT(1),
};

/* Receive Interrupt Enable */
#define IMX_MU_xCR_RIEn(data, x) ((data->cfg->type) & IMX_MU_V2 ? BIT(x) : BIT(24 + (3 - (x))))
#define IMX_MU_xSR_RFn(data, x) ((data->cfg->type) & IMX_MU_V2 ? BIT(x) : BIT(24 + (3 - (x))))

struct imx_mu_dcfg {
	enum imx_mu_type type;
	u32     xTR;            /* Transmit Register0 */
	u32     xRR;            /* Receive Register0 */
	u32     xSR[IMX_MU_xSR_MAX];         /* Status Registers */
	u32     xCR[IMX_MU_xCR_MAX];         /* Control Registers */
};

struct imx_mu_msi {
	raw_spinlock_t			lock;
	struct irq_domain		*msi_domain;
	void __iomem			*regs;
	phys_addr_t			msiir_addr;
	const struct imx_mu_dcfg	*cfg;
	unsigned long			used;
	struct clk			*clk;
};

static void imx_mu_write(struct imx_mu_msi *msi_data, u32 val, u32 offs)
{
	iowrite32(val, msi_data->regs + offs);
}

static u32 imx_mu_read(struct imx_mu_msi *msi_data, u32 offs)
{
	return ioread32(msi_data->regs + offs);
}

static u32 imx_mu_xcr_rmw(struct imx_mu_msi *msi_data, enum imx_mu_xcr type, u32 set, u32 clr)
{
	unsigned long flags;
	u32 val;

	raw_spin_lock_irqsave(&msi_data->lock, flags);
	val = imx_mu_read(msi_data, msi_data->cfg->xCR[type]);
	val &= ~clr;
	val |= set;
	imx_mu_write(msi_data, val, msi_data->cfg->xCR[type]);
	raw_spin_unlock_irqrestore(&msi_data->lock, flags);

	return val;
}

static void imx_mu_msi_parent_mask_irq(struct irq_data *data)
{
	struct imx_mu_msi *msi_data = irq_data_get_irq_chip_data(data);

	imx_mu_xcr_rmw(msi_data, IMX_MU_RCR, 0, IMX_MU_xCR_RIEn(msi_data, data->hwirq));
}

static void imx_mu_msi_parent_unmask_irq(struct irq_data *data)
{
	struct imx_mu_msi *msi_data = irq_data_get_irq_chip_data(data);

	imx_mu_xcr_rmw(msi_data, IMX_MU_RCR, IMX_MU_xCR_RIEn(msi_data, data->hwirq), 0);
}

static void imx_mu_msi_parent_ack_irq(struct irq_data *data)
{
	struct imx_mu_msi *msi_data = irq_data_get_irq_chip_data(data);

	imx_mu_read(msi_data, msi_data->cfg->xRR + data->hwirq * 4);
}

static struct irq_chip imx_mu_msi_irq_chip = {
	.name = "MU-MSI",
	.irq_ack = irq_chip_ack_parent,
};

static struct msi_domain_ops imx_mu_msi_irq_ops = {
};

static struct msi_domain_info imx_mu_msi_domain_info = {
	.flags	= (MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS),
	.ops	= &imx_mu_msi_irq_ops,
	.chip	= &imx_mu_msi_irq_chip,
};

static void imx_mu_msi_parent_compose_msg(struct irq_data *data,
					  struct msi_msg *msg)
{
	struct imx_mu_msi *msi_data = irq_data_get_irq_chip_data(data);
	u64 addr = msi_data->msiir_addr + 4 * data->hwirq;

	msg->address_hi = upper_32_bits(addr);
	msg->address_lo = lower_32_bits(addr);
	msg->data = data->hwirq;
}

static int imx_mu_msi_parent_set_affinity(struct irq_data *irq_data,
				   const struct cpumask *mask, bool force)
{
	return -EINVAL;
}

static struct irq_chip imx_mu_msi_parent_chip = {
	.name		= "MU",
	.irq_mask	= imx_mu_msi_parent_mask_irq,
	.irq_unmask	= imx_mu_msi_parent_unmask_irq,
	.irq_ack	= imx_mu_msi_parent_ack_irq,
	.irq_compose_msi_msg	= imx_mu_msi_parent_compose_msg,
	.irq_set_affinity = imx_mu_msi_parent_set_affinity,
};

static int imx_mu_msi_domain_irq_alloc(struct irq_domain *domain,
					unsigned int virq,
					unsigned int nr_irqs,
					void *args)
{
	struct imx_mu_msi *msi_data = domain->host_data;
	unsigned long flags;
	int pos, err = 0;

	WARN_ON(nr_irqs != 1);

	raw_spin_lock_irqsave(&msi_data->lock, flags);
	pos = find_first_zero_bit(&msi_data->used, IMX_MU_CHANS);
	if (pos < IMX_MU_CHANS)
		__set_bit(pos, &msi_data->used);
	else
		err = -ENOSPC;
	raw_spin_unlock_irqrestore(&msi_data->lock, flags);

	if (err)
		return err;

	irq_domain_set_info(domain, virq, pos,
			    &imx_mu_msi_parent_chip, msi_data,
			    handle_edge_irq, NULL, NULL);
	return 0;
}

static void imx_mu_msi_domain_irq_free(struct irq_domain *domain,
				       unsigned int virq, unsigned int nr_irqs)
{
	struct irq_data *d = irq_domain_get_irq_data(domain, virq);
	struct imx_mu_msi *msi_data = irq_data_get_irq_chip_data(d);
	unsigned long flags;

	raw_spin_lock_irqsave(&msi_data->lock, flags);
	__clear_bit(d->hwirq, &msi_data->used);
	raw_spin_unlock_irqrestore(&msi_data->lock, flags);
}

static const struct irq_domain_ops imx_mu_msi_domain_ops = {
	.alloc	= imx_mu_msi_domain_irq_alloc,
	.free	= imx_mu_msi_domain_irq_free,
};

static void imx_mu_msi_irq_handler(struct irq_desc *desc)
{
	struct imx_mu_msi *msi_data = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	u32 status;
	int i;

	status = imx_mu_read(msi_data, msi_data->cfg->xSR[IMX_MU_RSR]);

	chained_irq_enter(chip, desc);
	for (i = 0; i < IMX_MU_CHANS; i++) {
		if (status & IMX_MU_xSR_RFn(msi_data, i))
			generic_handle_domain_irq(msi_data->msi_domain, i);
	}
	chained_irq_exit(chip, desc);
}

static int imx_mu_msi_domains_init(struct imx_mu_msi *msi_data, struct device *dev)
{
	struct fwnode_handle *fwnodes = dev_fwnode(dev);
	struct irq_domain *parent;

	/* Initialize MSI domain parent */
	parent = irq_domain_create_linear(fwnodes,
					    IMX_MU_CHANS,
					    &imx_mu_msi_domain_ops,
					    msi_data);
	if (!parent) {
		dev_err(dev, "failed to create IRQ domain\n");
		return -ENOMEM;
	}

	irq_domain_update_bus_token(parent, DOMAIN_BUS_NEXUS);

	msi_data->msi_domain = platform_msi_create_irq_domain(fwnodes,
					&imx_mu_msi_domain_info,
					parent);

	if (!msi_data->msi_domain) {
		dev_err(dev, "failed to create MSI domain\n");
		irq_domain_remove(parent);
		return -ENOMEM;
	}

	irq_domain_set_pm_device(msi_data->msi_domain, dev);

	return 0;
}

/* Register offset of different version MU IP */
static const struct imx_mu_dcfg imx_mu_cfg_imx6sx = {
	.type	= 0,
	.xTR    = 0x0,
	.xRR    = 0x10,
	.xSR    = {
			[IMX_MU_SR]  = 0x20,
			[IMX_MU_GSR] = 0x20,
			[IMX_MU_TSR] = 0x20,
			[IMX_MU_RSR] = 0x20,
		  },
	.xCR    = {
			[IMX_MU_GIER] = 0x24,
			[IMX_MU_GCR]  = 0x24,
			[IMX_MU_TCR]  = 0x24,
			[IMX_MU_RCR]  = 0x24,
		  },
};

static const struct imx_mu_dcfg imx_mu_cfg_imx7ulp = {
	.type	= 0,
	.xTR    = 0x20,
	.xRR    = 0x40,
	.xSR    = {
			[IMX_MU_SR]  = 0x60,
			[IMX_MU_GSR] = 0x60,
			[IMX_MU_TSR] = 0x60,
			[IMX_MU_RSR] = 0x60,
		  },
	.xCR    = {
			[IMX_MU_GIER] = 0x64,
			[IMX_MU_GCR]  = 0x64,
			[IMX_MU_TCR]  = 0x64,
			[IMX_MU_RCR]  = 0x64,
		  },
};

static const struct imx_mu_dcfg imx_mu_cfg_imx8ulp = {
	.type   = IMX_MU_V2,
	.xTR    = 0x200,
	.xRR    = 0x280,
	.xSR    = {
			[IMX_MU_SR]  = 0xC,
			[IMX_MU_GSR] = 0x118,
			[IMX_MU_GSR] = 0x124,
			[IMX_MU_RSR] = 0x12C,
		  },
	.xCR    = {
			[IMX_MU_GIER] = 0x110,
			[IMX_MU_GCR]  = 0x114,
			[IMX_MU_TCR]  = 0x120,
			[IMX_MU_RCR]  = 0x128
		  },
};

static int __init imx_mu_of_init(struct device_node *dn,
				 struct device_node *parent,
				 const struct imx_mu_dcfg *cfg)
{
	struct platform_device *pdev = of_find_device_by_node(dn);
	struct device_link *pd_link_a;
	struct device_link *pd_link_b;
	struct imx_mu_msi *msi_data;
	struct resource *res;
	struct device *pd_a;
	struct device *pd_b;
	struct device *dev;
	int ret;
	int irq;

	dev = &pdev->dev;

	msi_data = devm_kzalloc(&pdev->dev, sizeof(*msi_data), GFP_KERNEL);
	if (!msi_data)
		return -ENOMEM;

	msi_data->cfg = cfg;

	msi_data->regs = devm_platform_ioremap_resource_byname(pdev, "processor-a-side");
	if (IS_ERR(msi_data->regs)) {
		dev_err(&pdev->dev, "failed to initialize 'regs'\n");
		return PTR_ERR(msi_data->regs);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "processor-b-side");
	if (!res)
		return -EIO;

	msi_data->msiir_addr = res->start + msi_data->cfg->xTR;

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0)
		return -ENODEV;

	platform_set_drvdata(pdev, msi_data);

	msi_data->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(msi_data->clk))
		return PTR_ERR(msi_data->clk);

	pd_a = dev_pm_domain_attach_by_name(dev, "processor-a-side");
	if (IS_ERR(pd_a))
		return PTR_ERR(pd_a);

	pd_b = dev_pm_domain_attach_by_name(dev, "processor-b-side");
	if (IS_ERR(pd_b))
		return PTR_ERR(pd_b);

	pd_link_a = device_link_add(dev, pd_a,
			DL_FLAG_STATELESS |
			DL_FLAG_PM_RUNTIME |
			DL_FLAG_RPM_ACTIVE);

	if (!pd_link_a) {
		dev_err(dev, "Failed to add device_link to mu a.\n");
		goto err_pd_a;
	}

	pd_link_b = device_link_add(dev, pd_b,
			DL_FLAG_STATELESS |
			DL_FLAG_PM_RUNTIME |
			DL_FLAG_RPM_ACTIVE);


	if (!pd_link_b) {
		dev_err(dev, "Failed to add device_link to mu a.\n");
		goto err_pd_b;
	}

	ret = imx_mu_msi_domains_init(msi_data, dev);
	if (ret)
		goto err_dm_init;

	pm_runtime_enable(dev);

	irq_set_chained_handler_and_data(irq,
					 imx_mu_msi_irq_handler,
					 msi_data);

	return 0;

err_dm_init:
	device_link_remove(dev,	pd_b);
err_pd_b:
	device_link_remove(dev, pd_a);
err_pd_a:
	return -EINVAL;
}

static int __maybe_unused imx_mu_runtime_suspend(struct device *dev)
{
	struct imx_mu_msi *priv = dev_get_drvdata(dev);

	clk_disable_unprepare(priv->clk);

	return 0;
}

static int __maybe_unused imx_mu_runtime_resume(struct device *dev)
{
	struct imx_mu_msi *priv = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(priv->clk);
	if (ret)
		dev_err(dev, "failed to enable clock\n");

	return ret;
}

static const struct dev_pm_ops imx_mu_pm_ops = {
	SET_RUNTIME_PM_OPS(imx_mu_runtime_suspend,
			   imx_mu_runtime_resume, NULL)
};

static int __init imx_mu_imx7ulp_of_init(struct device_node *dn,
					 struct device_node *parent)
{
	return imx_mu_of_init(dn, parent, &imx_mu_cfg_imx7ulp);
}

static int __init imx_mu_imx6sx_of_init(struct device_node *dn,
					struct device_node *parent)
{
	return imx_mu_of_init(dn, parent, &imx_mu_cfg_imx6sx);
}

static int __init imx_mu_imx8ulp_of_init(struct device_node *dn,
					 struct device_node *parent)
{
	return imx_mu_of_init(dn, parent, &imx_mu_cfg_imx8ulp);
}

IRQCHIP_PLATFORM_DRIVER_BEGIN(imx_mu_msi)
IRQCHIP_MATCH("fsl,imx7ulp-mu-msi", imx_mu_imx7ulp_of_init)
IRQCHIP_MATCH("fsl,imx6sx-mu-msi", imx_mu_imx6sx_of_init)
IRQCHIP_MATCH("fsl,imx8ulp-mu-msi", imx_mu_imx8ulp_of_init)
IRQCHIP_PLATFORM_DRIVER_END(imx_mu_msi, .pm = &imx_mu_pm_ops)


MODULE_AUTHOR("Frank Li <Frank.Li@nxp.com>");
MODULE_DESCRIPTION("Freescale MU MSI controller driver");
MODULE_LICENSE("GPL");
