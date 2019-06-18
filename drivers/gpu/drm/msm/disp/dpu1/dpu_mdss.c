/*
 * SPDX-License-Identifier: GPL-2.0
 * Copyright (c) 2018, The Linux Foundation
 */

#include "dpu_kms.h"
#include <linux/interconnect.h>

#define to_dpu_mdss(x) container_of(x, struct dpu_mdss, base)

#define HW_INTR_STATUS			0x0010

/* Max BW defined in KBps */
#define MAX_BW				6800000

struct dpu_irq_controller {
	unsigned long enabled_mask;
	struct irq_domain *domain;
};

struct dpu_mdss {
	struct msm_mdss base;
	void __iomem *mmio;
	unsigned long mmio_len;
	u32 hwversion;
	struct dss_module_power mp;
	struct dpu_irq_controller irq_controller;
	struct icc_path *path[2];
	u32 num_paths;
};

static int dpu_mdss_parse_data_bus_icc_path(struct drm_device *dev,
						struct dpu_mdss *dpu_mdss)
{
	struct icc_path *path0 = of_icc_get(dev->dev, "mdp0-mem");
	struct icc_path *path1 = of_icc_get(dev->dev, "mdp1-mem");

	if (IS_ERR_OR_NULL(path0))
		return PTR_ERR_OR_ZERO(path0);

	dpu_mdss->path[0] = path0;
	dpu_mdss->num_paths = 1;

	if (!IS_ERR_OR_NULL(path1)) {
		dpu_mdss->path[1] = path1;
		dpu_mdss->num_paths++;
	}

	return 0;
}

static void dpu_mdss_icc_request_bw(struct msm_mdss *mdss)
{
	struct dpu_mdss *dpu_mdss = to_dpu_mdss(mdss);
	int i;
	u64 avg_bw = dpu_mdss->num_paths ? MAX_BW / dpu_mdss->num_paths : 0;

	for (i = 0; i < dpu_mdss->num_paths; i++)
		icc_set_bw(dpu_mdss->path[i], avg_bw, kBps_to_icc(MAX_BW));
}

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

	dpu_mdss_icc_request_bw(mdss);

	ret = msm_dss_enable_clk(mp->clk_config, mp->num_clk, true);
	if (ret)
		DPU_ERROR("clock enable failed, ret:%d\n", ret);

	return ret;
}

static int dpu_mdss_disable(struct msm_mdss *mdss)
{
	struct dpu_mdss *dpu_mdss = to_dpu_mdss(mdss);
	struct dss_module_power *mp = &dpu_mdss->mp;
	int ret, i;

	ret = msm_dss_enable_clk(mp->clk_config, mp->num_clk, false);
	if (ret)
		DPU_ERROR("clock disable failed, ret:%d\n", ret);

	for (i = 0; i < dpu_mdss->num_paths; i++)
		icc_set_bw(dpu_mdss->path[i], 0, 0);

	return ret;
}

static void dpu_mdss_destroy(struct drm_device *dev)
{
	struct platform_device *pdev = to_platform_device(dev->dev);
	struct msm_drm_private *priv = dev->dev_private;
	struct dpu_mdss *dpu_mdss = to_dpu_mdss(priv->mdss);
	struct dss_module_power *mp = &dpu_mdss->mp;
	int irq;
	int i;

	pm_runtime_suspend(dev->dev);
	pm_runtime_disable(dev->dev);
	_dpu_mdss_irq_domain_fini(dpu_mdss);
	irq = platform_get_irq(pdev, 0);
	irq_set_chained_handler_and_data(irq, NULL, NULL);
	msm_dss_put_clk(mp->clk_config, mp->num_clk);
	devm_kfree(&pdev->dev, mp->clk_config);

	for (i = 0; i < dpu_mdss->num_paths; i++)
		icc_put(dpu_mdss->path[i]);

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
	struct resource *res;
	struct dpu_mdss *dpu_mdss;
	struct dss_module_power *mp;
	int ret = 0;
	int irq;

	dpu_mdss = devm_kzalloc(dev->dev, sizeof(*dpu_mdss), GFP_KERNEL);
	if (!dpu_mdss)
		return -ENOMEM;

	dpu_mdss->mmio = msm_ioremap(pdev, "mdss", "mdss");
	if (IS_ERR(dpu_mdss->mmio))
		return PTR_ERR(dpu_mdss->mmio);

	DRM_DEBUG("mapped mdss address space @%pK\n", dpu_mdss->mmio);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mdss");
	if (!res) {
		DRM_ERROR("failed to get memory resource for mdss\n");
		return -ENOMEM;
	}
	dpu_mdss->mmio_len = resource_size(res);

	ret = dpu_mdss_parse_data_bus_icc_path(dev, dpu_mdss);
	if (ret)
		return ret;

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
	if (irq < 0)
		goto irq_error;

	irq_set_chained_handler_and_data(irq, dpu_mdss_irq,
					 dpu_mdss);

	priv->mdss = &dpu_mdss->base;

	pm_runtime_enable(dev->dev);

	dpu_mdss_icc_request_bw(priv->mdss);

	pm_runtime_get_sync(dev->dev);
	dpu_mdss->hwversion = readl_relaxed(dpu_mdss->mmio);
	pm_runtime_put_sync(dev->dev);

	return ret;

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
