// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
 */

#include <linux/irqdomain.h>
#include <linux/irq.h>

#include "msm_drv.h"
#include "mdp5_kms.h"

#define to_mdp5_mdss(x) container_of(x, struct mdp5_mdss, base)

struct mdp5_mdss {
	struct msm_mdss base;

	void __iomem *mmio, *vbif;

	struct regulator *vdd;

	struct clk *ahb_clk;
	struct clk *axi_clk;
	struct clk *vsync_clk;

	struct {
		volatile unsigned long enabled_mask;
		struct irq_domain *domain;
	} irqcontroller;
};

static inline void mdss_write(struct mdp5_mdss *mdp5_mdss, u32 reg, u32 data)
{
	msm_writel(data, mdp5_mdss->mmio + reg);
}

static inline u32 mdss_read(struct mdp5_mdss *mdp5_mdss, u32 reg)
{
	return msm_readl(mdp5_mdss->mmio + reg);
}

static irqreturn_t mdss_irq(int irq, void *arg)
{
	struct mdp5_mdss *mdp5_mdss = arg;
	u32 intr;

	intr = mdss_read(mdp5_mdss, REG_MDSS_HW_INTR_STATUS);

	VERB("intr=%08x", intr);

	while (intr) {
		irq_hw_number_t hwirq = fls(intr) - 1;

		generic_handle_domain_irq(mdp5_mdss->irqcontroller.domain, hwirq);
		intr &= ~(1 << hwirq);
	}

	return IRQ_HANDLED;
}

/*
 * interrupt-controller implementation, so sub-blocks (MDP/HDMI/eDP/DSI/etc)
 * can register to get their irq's delivered
 */

#define VALID_IRQS  (MDSS_HW_INTR_STATUS_INTR_MDP | \
		MDSS_HW_INTR_STATUS_INTR_DSI0 | \
		MDSS_HW_INTR_STATUS_INTR_DSI1 | \
		MDSS_HW_INTR_STATUS_INTR_HDMI | \
		MDSS_HW_INTR_STATUS_INTR_EDP)

static void mdss_hw_mask_irq(struct irq_data *irqd)
{
	struct mdp5_mdss *mdp5_mdss = irq_data_get_irq_chip_data(irqd);

	smp_mb__before_atomic();
	clear_bit(irqd->hwirq, &mdp5_mdss->irqcontroller.enabled_mask);
	smp_mb__after_atomic();
}

static void mdss_hw_unmask_irq(struct irq_data *irqd)
{
	struct mdp5_mdss *mdp5_mdss = irq_data_get_irq_chip_data(irqd);

	smp_mb__before_atomic();
	set_bit(irqd->hwirq, &mdp5_mdss->irqcontroller.enabled_mask);
	smp_mb__after_atomic();
}

static struct irq_chip mdss_hw_irq_chip = {
	.name		= "mdss",
	.irq_mask	= mdss_hw_mask_irq,
	.irq_unmask	= mdss_hw_unmask_irq,
};

static int mdss_hw_irqdomain_map(struct irq_domain *d, unsigned int irq,
				 irq_hw_number_t hwirq)
{
	struct mdp5_mdss *mdp5_mdss = d->host_data;

	if (!(VALID_IRQS & (1 << hwirq)))
		return -EPERM;

	irq_set_chip_and_handler(irq, &mdss_hw_irq_chip, handle_level_irq);
	irq_set_chip_data(irq, mdp5_mdss);

	return 0;
}

static const struct irq_domain_ops mdss_hw_irqdomain_ops = {
	.map = mdss_hw_irqdomain_map,
	.xlate = irq_domain_xlate_onecell,
};


static int mdss_irq_domain_init(struct mdp5_mdss *mdp5_mdss)
{
	struct device *dev = mdp5_mdss->base.dev->dev;
	struct irq_domain *d;

	d = irq_domain_add_linear(dev->of_node, 32, &mdss_hw_irqdomain_ops,
				  mdp5_mdss);
	if (!d) {
		DRM_DEV_ERROR(dev, "mdss irq domain add failed\n");
		return -ENXIO;
	}

	mdp5_mdss->irqcontroller.enabled_mask = 0;
	mdp5_mdss->irqcontroller.domain = d;

	return 0;
}

static int mdp5_mdss_enable(struct msm_mdss *mdss)
{
	struct mdp5_mdss *mdp5_mdss = to_mdp5_mdss(mdss);
	DBG("");

	clk_prepare_enable(mdp5_mdss->ahb_clk);
	clk_prepare_enable(mdp5_mdss->axi_clk);
	clk_prepare_enable(mdp5_mdss->vsync_clk);

	return 0;
}

static int mdp5_mdss_disable(struct msm_mdss *mdss)
{
	struct mdp5_mdss *mdp5_mdss = to_mdp5_mdss(mdss);
	DBG("");

	clk_disable_unprepare(mdp5_mdss->vsync_clk);
	clk_disable_unprepare(mdp5_mdss->axi_clk);
	clk_disable_unprepare(mdp5_mdss->ahb_clk);

	return 0;
}

static int msm_mdss_get_clocks(struct mdp5_mdss *mdp5_mdss)
{
	struct platform_device *pdev =
			to_platform_device(mdp5_mdss->base.dev->dev);

	mdp5_mdss->ahb_clk = msm_clk_get(pdev, "iface");
	if (IS_ERR(mdp5_mdss->ahb_clk))
		mdp5_mdss->ahb_clk = NULL;

	mdp5_mdss->axi_clk = msm_clk_get(pdev, "bus");
	if (IS_ERR(mdp5_mdss->axi_clk))
		mdp5_mdss->axi_clk = NULL;

	mdp5_mdss->vsync_clk = msm_clk_get(pdev, "vsync");
	if (IS_ERR(mdp5_mdss->vsync_clk))
		mdp5_mdss->vsync_clk = NULL;

	return 0;
}

static void mdp5_mdss_destroy(struct drm_device *dev)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct mdp5_mdss *mdp5_mdss = to_mdp5_mdss(priv->mdss);

	if (!mdp5_mdss)
		return;

	irq_domain_remove(mdp5_mdss->irqcontroller.domain);
	mdp5_mdss->irqcontroller.domain = NULL;

	regulator_disable(mdp5_mdss->vdd);

	pm_runtime_disable(dev->dev);
}

static const struct msm_mdss_funcs mdss_funcs = {
	.enable	= mdp5_mdss_enable,
	.disable = mdp5_mdss_disable,
	.destroy = mdp5_mdss_destroy,
};

int mdp5_mdss_init(struct drm_device *dev)
{
	struct platform_device *pdev = to_platform_device(dev->dev);
	struct msm_drm_private *priv = dev->dev_private;
	struct mdp5_mdss *mdp5_mdss;
	int ret;

	DBG("");

	if (!of_device_is_compatible(dev->dev->of_node, "qcom,mdss"))
		return 0;

	mdp5_mdss = devm_kzalloc(dev->dev, sizeof(*mdp5_mdss), GFP_KERNEL);
	if (!mdp5_mdss) {
		ret = -ENOMEM;
		goto fail;
	}

	mdp5_mdss->base.dev = dev;

	mdp5_mdss->mmio = msm_ioremap(pdev, "mdss_phys", "MDSS");
	if (IS_ERR(mdp5_mdss->mmio)) {
		ret = PTR_ERR(mdp5_mdss->mmio);
		goto fail;
	}

	mdp5_mdss->vbif = msm_ioremap(pdev, "vbif_phys", "VBIF");
	if (IS_ERR(mdp5_mdss->vbif)) {
		ret = PTR_ERR(mdp5_mdss->vbif);
		goto fail;
	}

	ret = msm_mdss_get_clocks(mdp5_mdss);
	if (ret) {
		DRM_DEV_ERROR(dev->dev, "failed to get clocks: %d\n", ret);
		goto fail;
	}

	/* Regulator to enable GDSCs in downstream kernels */
	mdp5_mdss->vdd = devm_regulator_get(dev->dev, "vdd");
	if (IS_ERR(mdp5_mdss->vdd)) {
		ret = PTR_ERR(mdp5_mdss->vdd);
		goto fail;
	}

	ret = regulator_enable(mdp5_mdss->vdd);
	if (ret) {
		DRM_DEV_ERROR(dev->dev, "failed to enable regulator vdd: %d\n",
			ret);
		goto fail;
	}

	ret = devm_request_irq(dev->dev, platform_get_irq(pdev, 0),
			       mdss_irq, 0, "mdss_isr", mdp5_mdss);
	if (ret) {
		DRM_DEV_ERROR(dev->dev, "failed to init irq: %d\n", ret);
		goto fail_irq;
	}

	ret = mdss_irq_domain_init(mdp5_mdss);
	if (ret) {
		DRM_DEV_ERROR(dev->dev, "failed to init sub-block irqs: %d\n", ret);
		goto fail_irq;
	}

	mdp5_mdss->base.funcs = &mdss_funcs;
	priv->mdss = &mdp5_mdss->base;

	pm_runtime_enable(dev->dev);

	return 0;
fail_irq:
	regulator_disable(mdp5_mdss->vdd);
fail:
	return ret;
}
