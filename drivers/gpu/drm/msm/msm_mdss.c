/*
 * SPDX-License-Identifier: GPL-2.0
 * Copyright (c) 2018, The Linux Foundation
 */

#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdesc.h>
#include <linux/irqchip/chained_irq.h>

#include "msm_drv.h"
#include "msm_kms.h"

/* for DPU_HW_* defines */
#include "disp/dpu1/dpu_hw_catalog.h"

#define to_dpu_mdss(x) container_of(x, struct dpu_mdss, base)

#define HW_REV				0x0
#define HW_INTR_STATUS			0x0010

#define UBWC_STATIC			0x144
#define UBWC_CTRL_2			0x150
#define UBWC_PREDICTION_MODE		0x154

struct dpu_mdss {
	struct msm_mdss base;
	void __iomem *mmio;
	struct clk_bulk_data *clocks;
	size_t num_clocks;
	bool is_mdp5;
	struct {
		unsigned long enabled_mask;
		struct irq_domain *domain;
	} irq_controller;
};

static void msm_mdss_irq(struct irq_desc *desc)
{
	struct dpu_mdss *dpu_mdss = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	u32 interrupts;

	chained_irq_enter(chip, desc);

	interrupts = readl_relaxed(dpu_mdss->mmio + HW_INTR_STATUS);

	while (interrupts) {
		irq_hw_number_t hwirq = fls(interrupts) - 1;
		int rc;

		rc = generic_handle_domain_irq(dpu_mdss->irq_controller.domain,
					       hwirq);
		if (rc < 0) {
			DRM_ERROR("handle irq fail: irq=%lu rc=%d\n",
				  hwirq, rc);
			break;
		}

		interrupts &= ~(1 << hwirq);
	}

	chained_irq_exit(chip, desc);
}

static void msm_mdss_irq_mask(struct irq_data *irqd)
{
	struct dpu_mdss *dpu_mdss = irq_data_get_irq_chip_data(irqd);

	/* memory barrier */
	smp_mb__before_atomic();
	clear_bit(irqd->hwirq, &dpu_mdss->irq_controller.enabled_mask);
	/* memory barrier */
	smp_mb__after_atomic();
}

static void msm_mdss_irq_unmask(struct irq_data *irqd)
{
	struct dpu_mdss *dpu_mdss = irq_data_get_irq_chip_data(irqd);

	/* memory barrier */
	smp_mb__before_atomic();
	set_bit(irqd->hwirq, &dpu_mdss->irq_controller.enabled_mask);
	/* memory barrier */
	smp_mb__after_atomic();
}

static struct irq_chip msm_mdss_irq_chip = {
	.name = "dpu_mdss",
	.irq_mask = msm_mdss_irq_mask,
	.irq_unmask = msm_mdss_irq_unmask,
};

static struct lock_class_key msm_mdss_lock_key, msm_mdss_request_key;

static int msm_mdss_irqdomain_map(struct irq_domain *domain,
		unsigned int irq, irq_hw_number_t hwirq)
{
	struct dpu_mdss *dpu_mdss = domain->host_data;

	irq_set_lockdep_class(irq, &msm_mdss_lock_key, &msm_mdss_request_key);
	irq_set_chip_and_handler(irq, &msm_mdss_irq_chip, handle_level_irq);

	return irq_set_chip_data(irq, dpu_mdss);
}

static const struct irq_domain_ops msm_mdss_irqdomain_ops = {
	.map = msm_mdss_irqdomain_map,
	.xlate = irq_domain_xlate_onecell,
};

static int _msm_mdss_irq_domain_add(struct dpu_mdss *dpu_mdss)
{
	struct device *dev;
	struct irq_domain *domain;

	dev = dpu_mdss->base.dev;

	domain = irq_domain_add_linear(dev->of_node, 32,
			&msm_mdss_irqdomain_ops, dpu_mdss);
	if (!domain) {
		DRM_ERROR("failed to add irq_domain\n");
		return -EINVAL;
	}

	dpu_mdss->irq_controller.enabled_mask = 0;
	dpu_mdss->irq_controller.domain = domain;

	return 0;
}

static int msm_mdss_enable(struct msm_mdss *mdss)
{
	struct dpu_mdss *dpu_mdss = to_dpu_mdss(mdss);
	int ret;

	ret = clk_bulk_prepare_enable(dpu_mdss->num_clocks, dpu_mdss->clocks);
	if (ret) {
		DRM_ERROR("clock enable failed, ret:%d\n", ret);
		return ret;
	}

	/*
	 * HW_REV requires MDSS_MDP_CLK, which is not enabled by the mdss on
	 * mdp5 hardware. Skip reading it for now.
	 */
	if (dpu_mdss->is_mdp5)
		return 0;

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

static int msm_mdss_disable(struct msm_mdss *mdss)
{
	struct dpu_mdss *dpu_mdss = to_dpu_mdss(mdss);

	clk_bulk_disable_unprepare(dpu_mdss->num_clocks, dpu_mdss->clocks);

	return 0;
}

static void msm_mdss_destroy(struct msm_mdss *mdss)
{
	struct platform_device *pdev = to_platform_device(mdss->dev);
	struct dpu_mdss *dpu_mdss = to_dpu_mdss(mdss);
	int irq;

	pm_runtime_suspend(mdss->dev);
	pm_runtime_disable(mdss->dev);
	irq_domain_remove(dpu_mdss->irq_controller.domain);
	dpu_mdss->irq_controller.domain = NULL;
	irq = platform_get_irq(pdev, 0);
	irq_set_chained_handler_and_data(irq, NULL, NULL);
}

static const struct msm_mdss_funcs mdss_funcs = {
	.enable	= msm_mdss_enable,
	.disable = msm_mdss_disable,
	.destroy = msm_mdss_destroy,
};

/*
 * MDP5 MDSS uses at most three specified clocks.
 */
#define MDP5_MDSS_NUM_CLOCKS 3
static int mdp5_mdss_parse_clock(struct platform_device *pdev, struct clk_bulk_data **clocks)
{
	struct clk_bulk_data *bulk;
	int num_clocks = 0;
	int ret;

	if (!pdev)
		return -EINVAL;

	bulk = devm_kcalloc(&pdev->dev, MDP5_MDSS_NUM_CLOCKS, sizeof(struct clk_bulk_data), GFP_KERNEL);
	if (!bulk)
		return -ENOMEM;

	bulk[num_clocks++].id = "iface";
	bulk[num_clocks++].id = "bus";
	bulk[num_clocks++].id = "vsync";

	ret = devm_clk_bulk_get_optional(&pdev->dev, num_clocks, bulk);
	if (ret)
		return ret;

	*clocks = bulk;

	return num_clocks;
}

int msm_mdss_init(struct platform_device *pdev, bool is_mdp5)
{
	struct msm_drm_private *priv = platform_get_drvdata(pdev);
	struct dpu_mdss *dpu_mdss;
	int ret;
	int irq;

	dpu_mdss = devm_kzalloc(&pdev->dev, sizeof(*dpu_mdss), GFP_KERNEL);
	if (!dpu_mdss)
		return -ENOMEM;

	dpu_mdss->mmio = msm_ioremap(pdev, is_mdp5 ? "mdss_phys" : "mdss");
	if (IS_ERR(dpu_mdss->mmio))
		return PTR_ERR(dpu_mdss->mmio);

	DRM_DEBUG("mapped mdss address space @%pK\n", dpu_mdss->mmio);

	if (is_mdp5)
		ret = mdp5_mdss_parse_clock(pdev, &dpu_mdss->clocks);
	else
		ret = devm_clk_bulk_get_all(&pdev->dev, &dpu_mdss->clocks);
	if (ret < 0) {
		DRM_ERROR("failed to parse clocks, ret=%d\n", ret);
		return ret;
	}
	dpu_mdss->num_clocks = ret;
	dpu_mdss->is_mdp5 = is_mdp5;

	dpu_mdss->base.dev = &pdev->dev;
	dpu_mdss->base.funcs = &mdss_funcs;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = _msm_mdss_irq_domain_add(dpu_mdss);
	if (ret)
		return ret;

	irq_set_chained_handler_and_data(irq, msm_mdss_irq,
					 dpu_mdss);

	priv->mdss = &dpu_mdss->base;

	pm_runtime_enable(&pdev->dev);

	return 0;
}
