/*
 * SPDX-License-Identifier: GPL-2.0
 * Copyright (c) 2018, The Linux Foundation
 */

#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdesc.h>
#include <linux/irqchip/chained_irq.h>
#include "dpu_kms.h"

#define to_dpu_mdss(x) container_of(x, struct dpu_mdss, base)

#define HW_REV				0x0
#define HW_INTR_STATUS			0x0010

#define UBWC_STATIC			0x144
#define UBWC_CTRL_2			0x150
#define UBWC_PREDICTION_MODE		0x154

/* Max BW defined in KBps */
#define MAX_BW				6800000

struct dpu_irq_controller {
	unsigned long enabled_mask;
	struct irq_domain *domain;
};

struct dpu_mdss {
	struct msm_mdss base;
	void __iomem *mmio;
	struct dss_module_power mp;
	struct dpu_irq_controller irq_controller;
};

static void dpu_mdss_irq(struct irq_desc *desc)
{
	struct dpu_mdss *dpu_mdss = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	u32 interrupts;

	chained_irq_enter(chip, desc);

	interrupts = readl_relaxed(dpu_mdss->mmio + HW_INTR_STATUS);

	while (interrupts) {
		irq_hw_number_t hwirq = fls(interrupts) - 1;
		unsigned int mapping;
		int rc;

		mapping = irq_find_mapping(dpu_mdss->irq_controller.domain,
					   hwirq);
		if (mapping == 0) {
			DRM_ERROR("couldn't find irq mapping for %lu\n", hwirq);
			break;
		}

		rc = generic_handle_irq(mapping);
		if (rc < 0) {
			DRM_ERROR("handle irq fail: irq=%lu mapping=%u rc=%d\n",
				  hwirq, mapping, rc);
			break;
		}

		interrupts &= ~(1 << hwirq);
	}

	chained_irq_exit(chip, desc);
}

static void dpu_mdss_irq_mask(struct irq_data *irqd)
{
	struct dpu_mdss *dpu_mdss = irq_data_get_irq_chip_data(irqd);

	/* memory barrier */
	smp_mb__before_atomic();
	clear_bit(irqd->hwirq, &dpu_mdss->irq_controller.enabled_mask);
	/* memory barrier */
	smp_mb__after_atomic();
}

static void dpu_mdss_irq_unmask(struct irq_data *irqd)
{
	struct dpu_mdss *dpu_mdss = irq_data_get_irq_chip_data(irqd);

	/* memory barrier */
	smp_mb__before_atomic();
	set_bit(irqd->hwirq, &dpu_mdss->irq_controller.enabled_mask);
	/* memory barrier */
	smp_mb__after_atomic();
}

static struct irq_chip dpu_mdss_irq_chip = {
	.name = "dpu_mdss",
	.irq_mask = dpu_mdss_irq_mask,
	.irq_unmask = dpu_mdss_irq_unmask,
};

static struct lock_class_key dpu_mdss_lock_key, dpu_mdss_request_key;

static int dpu_mdss_irqdomain_map(struct irq_domain *domain,
		unsigned int irq, irq_hw_number_t hwirq)
{
	struct dpu_mdss *dpu_mdss = domain->host_data;

	irq_set_lockdep_class(irq, &dpu_mdss_lock_key, &dpu_mdss_request_key);
	irq_set_chip_and_handler(irq, &dpu_mdss_irq_chip, handle_level_irq);
	return irq_set_chip_data(irq, dpu_mdss);
}

static const struct irq_domain_ops dpu_mdss_irqdomain_ops = {
	.map = dpu_mdss_irqdomain_map,
	.xlate = irq_domain_xlate_onecell,
};

static int _dpu_mdss_irq_domain_add(struct dpu_mdss *dpu_mdss)
{
	struct device *dev;
	struct irq_domain *domain;

	dev = dpu_mdss->base.dev->dev;

	domain = irq_domain_add_linear(dev->of_node, 32,
			&dpu_mdss_irqdomain_ops, dpu_mdss);
	if (!domain) {
		DPU_ERROR("failed to add irq_domain\n");
		return -EINVAL;
	}

	dpu_mdss->irq_controller.enabled_mask = 0;
	dpu_mdss->irq_controller.domain = domain;

	return 0;
}

static void _dpu_mdss_irq_domain_fini(struct dpu_mdss *dpu_mdss)
{
	if (dpu_mdss->irq_controller.domain) {
		irq_domain_remove(dpu_mdss->irq_controller.domain);
		dpu_mdss->irq_controller.domain = NULL;
	}
}
static int dpu_mdss_enable(struct msm_mdss *mdss)
{
	struct dpu_mdss *dpu_mdss = to_dpu_mdss(mdss);
	struct dss_module_power *mp = &dpu_mdss->mp;
	int ret;

	ret = msm_dss_enable_clk(mp->clk_config, mp->num_clk, true);
	if (ret) {
		DPU_ERROR("clock enable failed, ret:%d\n", ret);
		return ret;
	}

	/*
	 * ubwc config is part of the "mdss" region which is not accessible
	 * from the rest of the driver. hardcode known configurations here
	 */
	switch (readl_relaxed(dpu_mdss->mmio + HW_REV)) {
	case DPU_HW_VER_500:
	case DPU_HW_VER_501:
		writel_relaxed(0x420, dpu_mdss->mmio + UBWC_STATIC);
		break;
	case DPU_HW_VER_600:
		/* TODO: 0x102e for LP_DDR4 */
		writel_relaxed(0x103e, dpu_mdss->mmio + UBWC_STATIC);
		writel_relaxed(2, dpu_mdss->mmio + UBWC_CTRL_2);
		writel_relaxed(1, dpu_mdss->mmio + UBWC_PREDICTION_MODE);
		break;
	case DPU_HW_VER_620:
		writel_relaxed(0x1e, dpu_mdss->mmio + UBWC_STATIC);
		break;
	case DPU_HW_VER_720:
		writel_relaxed(0x101e, dpu_mdss->mmio + UBWC_STATIC);
		break;
	}

	return ret;
}

static int dpu_mdss_disable(struct msm_mdss *mdss)
{
	struct dpu_mdss *dpu_mdss = to_dpu_mdss(mdss);
	struct dss_module_power *mp = &dpu_mdss->mp;
	int ret;

	ret = msm_dss_enable_clk(mp->clk_config, mp->num_clk, false);
	if (ret)
		DPU_ERROR("clock disable failed, ret:%d\n", ret);

	return ret;
}

static void dpu_mdss_destroy(struct drm_device *dev)
{
	struct platform_device *pdev = to_platform_device(dev->dev);
	struct msm_drm_private *priv = dev->dev_private;
	struct dpu_mdss *dpu_mdss = to_dpu_mdss(priv->mdss);
	struct dss_module_power *mp = &dpu_mdss->mp;
	int irq;

	pm_runtime_suspend(dev->dev);
	pm_runtime_disable(dev->dev);
	_dpu_mdss_irq_domain_fini(dpu_mdss);
	irq = platform_get_irq(pdev, 0);
	irq_set_chained_handler_and_data(irq, NULL, NULL);
	msm_dss_put_clk(mp->clk_config, mp->num_clk);
	devm_kfree(&pdev->dev, mp->clk_config);

	if (dpu_mdss->mmio)
		devm_iounmap(&pdev->dev, dpu_mdss->mmio);
	dpu_mdss->mmio = NULL;
	priv->mdss = NULL;
}

static const struct msm_mdss_funcs mdss_funcs = {
	.enable	= dpu_mdss_enable,
	.disable = dpu_mdss_disable,
	.destroy = dpu_mdss_destroy,
};

int dpu_mdss_init(struct drm_device *dev)
{
	struct platform_device *pdev = to_platform_device(dev->dev);
	struct msm_drm_private *priv = dev->dev_private;
	struct dpu_mdss *dpu_mdss;
	struct dss_module_power *mp;
	int ret;
	int irq;

	dpu_mdss = devm_kzalloc(dev->dev, sizeof(*dpu_mdss), GFP_KERNEL);
	if (!dpu_mdss)
		return -ENOMEM;

	dpu_mdss->mmio = msm_ioremap(pdev, "mdss", "mdss");
	if (IS_ERR(dpu_mdss->mmio))
		return PTR_ERR(dpu_mdss->mmio);

	DRM_DEBUG("mapped mdss address space @%pK\n", dpu_mdss->mmio);

	mp = &dpu_mdss->mp;
	ret = msm_dss_parse_clock(pdev, mp);
	if (ret) {
		DPU_ERROR("failed to parse clocks, ret=%d\n", ret);
		goto clk_parse_err;
	}

	dpu_mdss->base.dev = dev;
	dpu_mdss->base.funcs = &mdss_funcs;

	ret = _dpu_mdss_irq_domain_add(dpu_mdss);
	if (ret)
		goto irq_domain_error;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto irq_error;
	}

	irq_set_chained_handler_and_data(irq, dpu_mdss_irq,
					 dpu_mdss);

	priv->mdss = &dpu_mdss->base;

	pm_runtime_enable(dev->dev);

	return 0;

irq_error:
	_dpu_mdss_irq_domain_fini(dpu_mdss);
irq_domain_error:
	msm_dss_put_clk(mp->clk_config, mp->num_clk);
clk_parse_err:
	devm_kfree(&pdev->dev, mp->clk_config);
	if (dpu_mdss->mmio)
		devm_iounmap(&pdev->dev, dpu_mdss->mmio);
	dpu_mdss->mmio = NULL;
	return ret;
}
