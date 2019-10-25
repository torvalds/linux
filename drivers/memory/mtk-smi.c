// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2016 MediaTek Inc.
 * Author: Yong Wu <yong.wu@mediatek.com>
 */
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <soc/mediatek/smi.h>
#include <dt-bindings/memory/mt2701-larb-port.h>

/* mt8173 */
#define SMI_LARB_MMU_EN		0xf00

/* mt2701 */
#define REG_SMI_SECUR_CON_BASE		0x5c0

/* every register control 8 port, register offset 0x4 */
#define REG_SMI_SECUR_CON_OFFSET(id)	(((id) >> 3) << 2)
#define REG_SMI_SECUR_CON_ADDR(id)	\
	(REG_SMI_SECUR_CON_BASE + REG_SMI_SECUR_CON_OFFSET(id))

/*
 * every port have 4 bit to control, bit[port + 3] control virtual or physical,
 * bit[port + 2 : port + 1] control the domain, bit[port] control the security
 * or non-security.
 */
#define SMI_SECUR_CON_VAL_MSK(id)	(~(0xf << (((id) & 0x7) << 2)))
#define SMI_SECUR_CON_VAL_VIRT(id)	BIT((((id) & 0x7) << 2) + 3)
/* mt2701 domain should be set to 3 */
#define SMI_SECUR_CON_VAL_DOMAIN(id)	(0x3 << ((((id) & 0x7) << 2) + 1))

/* mt2712 */
#define SMI_LARB_NONSEC_CON(id)	(0x380 + ((id) * 4))
#define F_MMU_EN		BIT(0)

/* SMI COMMON */
#define SMI_BUS_SEL			0x220
#define SMI_BUS_LARB_SHIFT(larbid)	((larbid) << 1)
/* All are MMU0 defaultly. Only specialize mmu1 here. */
#define F_MMU1_LARB(larbid)		(0x1 << SMI_BUS_LARB_SHIFT(larbid))

enum mtk_smi_gen {
	MTK_SMI_GEN1,
	MTK_SMI_GEN2
};

struct mtk_smi_common_plat {
	enum mtk_smi_gen gen;
	bool             has_gals;
	u32              bus_sel; /* Balance some larbs to enter mmu0 or mmu1 */
};

struct mtk_smi_larb_gen {
	int port_in_larb[MTK_LARB_NR_MAX + 1];
	void (*config_port)(struct device *);
	unsigned int			larb_direct_to_common_mask;
	bool				has_gals;
};

struct mtk_smi {
	struct device			*dev;
	struct clk			*clk_apb, *clk_smi;
	struct clk			*clk_gals0, *clk_gals1;
	struct clk			*clk_async; /*only needed by mt2701*/
	union {
		void __iomem		*smi_ao_base; /* only for gen1 */
		void __iomem		*base;	      /* only for gen2 */
	};
	const struct mtk_smi_common_plat *plat;
};

struct mtk_smi_larb { /* larb: local arbiter */
	struct mtk_smi			smi;
	void __iomem			*base;
	struct device			*smi_common_dev;
	const struct mtk_smi_larb_gen	*larb_gen;
	int				larbid;
	u32				*mmu;
};

static int mtk_smi_clk_enable(const struct mtk_smi *smi)
{
	int ret;

	ret = clk_prepare_enable(smi->clk_apb);
	if (ret)
		return ret;

	ret = clk_prepare_enable(smi->clk_smi);
	if (ret)
		goto err_disable_apb;

	ret = clk_prepare_enable(smi->clk_gals0);
	if (ret)
		goto err_disable_smi;

	ret = clk_prepare_enable(smi->clk_gals1);
	if (ret)
		goto err_disable_gals0;

	return 0;

err_disable_gals0:
	clk_disable_unprepare(smi->clk_gals0);
err_disable_smi:
	clk_disable_unprepare(smi->clk_smi);
err_disable_apb:
	clk_disable_unprepare(smi->clk_apb);
	return ret;
}

static void mtk_smi_clk_disable(const struct mtk_smi *smi)
{
	clk_disable_unprepare(smi->clk_gals1);
	clk_disable_unprepare(smi->clk_gals0);
	clk_disable_unprepare(smi->clk_smi);
	clk_disable_unprepare(smi->clk_apb);
}

int mtk_smi_larb_get(struct device *larbdev)
{
	int ret = pm_runtime_get_sync(larbdev);

	return (ret < 0) ? ret : 0;
}
EXPORT_SYMBOL_GPL(mtk_smi_larb_get);

void mtk_smi_larb_put(struct device *larbdev)
{
	pm_runtime_put_sync(larbdev);
}
EXPORT_SYMBOL_GPL(mtk_smi_larb_put);

static int
mtk_smi_larb_bind(struct device *dev, struct device *master, void *data)
{
	struct mtk_smi_larb *larb = dev_get_drvdata(dev);
	struct mtk_smi_larb_iommu *larb_mmu = data;
	unsigned int         i;

	for (i = 0; i < MTK_LARB_NR_MAX; i++) {
		if (dev == larb_mmu[i].dev) {
			larb->larbid = i;
			larb->mmu = &larb_mmu[i].mmu;
			return 0;
		}
	}
	return -ENODEV;
}

static void mtk_smi_larb_config_port_gen2_general(struct device *dev)
{
	struct mtk_smi_larb *larb = dev_get_drvdata(dev);
	u32 reg;
	int i;

	if (BIT(larb->larbid) & larb->larb_gen->larb_direct_to_common_mask)
		return;

	for_each_set_bit(i, (unsigned long *)larb->mmu, 32) {
		reg = readl_relaxed(larb->base + SMI_LARB_NONSEC_CON(i));
		reg |= F_MMU_EN;
		writel(reg, larb->base + SMI_LARB_NONSEC_CON(i));
	}
}

static void mtk_smi_larb_config_port_mt8173(struct device *dev)
{
	struct mtk_smi_larb *larb = dev_get_drvdata(dev);

	writel(*larb->mmu, larb->base + SMI_LARB_MMU_EN);
}

static void mtk_smi_larb_config_port_gen1(struct device *dev)
{
	struct mtk_smi_larb *larb = dev_get_drvdata(dev);
	const struct mtk_smi_larb_gen *larb_gen = larb->larb_gen;
	struct mtk_smi *common = dev_get_drvdata(larb->smi_common_dev);
	int i, m4u_port_id, larb_port_num;
	u32 sec_con_val, reg_val;

	m4u_port_id = larb_gen->port_in_larb[larb->larbid];
	larb_port_num = larb_gen->port_in_larb[larb->larbid + 1]
			- larb_gen->port_in_larb[larb->larbid];

	for (i = 0; i < larb_port_num; i++, m4u_port_id++) {
		if (*larb->mmu & BIT(i)) {
			/* bit[port + 3] controls the virtual or physical */
			sec_con_val = SMI_SECUR_CON_VAL_VIRT(m4u_port_id);
		} else {
			/* do not need to enable m4u for this port */
			continue;
		}
		reg_val = readl(common->smi_ao_base
			+ REG_SMI_SECUR_CON_ADDR(m4u_port_id));
		reg_val &= SMI_SECUR_CON_VAL_MSK(m4u_port_id);
		reg_val |= sec_con_val;
		reg_val |= SMI_SECUR_CON_VAL_DOMAIN(m4u_port_id);
		writel(reg_val,
			common->smi_ao_base
			+ REG_SMI_SECUR_CON_ADDR(m4u_port_id));
	}
}

static void
mtk_smi_larb_unbind(struct device *dev, struct device *master, void *data)
{
	/* Do nothing as the iommu is always enabled. */
}

static const struct component_ops mtk_smi_larb_component_ops = {
	.bind = mtk_smi_larb_bind,
	.unbind = mtk_smi_larb_unbind,
};

static const struct mtk_smi_larb_gen mtk_smi_larb_mt8173 = {
	/* mt8173 do not need the port in larb */
	.config_port = mtk_smi_larb_config_port_mt8173,
};

static const struct mtk_smi_larb_gen mtk_smi_larb_mt2701 = {
	.port_in_larb = {
		LARB0_PORT_OFFSET, LARB1_PORT_OFFSET,
		LARB2_PORT_OFFSET, LARB3_PORT_OFFSET
	},
	.config_port = mtk_smi_larb_config_port_gen1,
};

static const struct mtk_smi_larb_gen mtk_smi_larb_mt2712 = {
	.config_port                = mtk_smi_larb_config_port_gen2_general,
	.larb_direct_to_common_mask = BIT(8) | BIT(9),      /* bdpsys */
};

static const struct mtk_smi_larb_gen mtk_smi_larb_mt8183 = {
	.has_gals                   = true,
	.config_port                = mtk_smi_larb_config_port_gen2_general,
	.larb_direct_to_common_mask = BIT(2) | BIT(3) | BIT(7),
				      /* IPU0 | IPU1 | CCU */
};

static const struct of_device_id mtk_smi_larb_of_ids[] = {
	{
		.compatible = "mediatek,mt8173-smi-larb",
		.data = &mtk_smi_larb_mt8173
	},
	{
		.compatible = "mediatek,mt2701-smi-larb",
		.data = &mtk_smi_larb_mt2701
	},
	{
		.compatible = "mediatek,mt2712-smi-larb",
		.data = &mtk_smi_larb_mt2712
	},
	{
		.compatible = "mediatek,mt8183-smi-larb",
		.data = &mtk_smi_larb_mt8183
	},
	{}
};

static int mtk_smi_larb_probe(struct platform_device *pdev)
{
	struct mtk_smi_larb *larb;
	struct resource *res;
	struct device *dev = &pdev->dev;
	struct device_node *smi_node;
	struct platform_device *smi_pdev;

	larb = devm_kzalloc(dev, sizeof(*larb), GFP_KERNEL);
	if (!larb)
		return -ENOMEM;

	larb->larb_gen = of_device_get_match_data(dev);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	larb->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(larb->base))
		return PTR_ERR(larb->base);

	larb->smi.clk_apb = devm_clk_get(dev, "apb");
	if (IS_ERR(larb->smi.clk_apb))
		return PTR_ERR(larb->smi.clk_apb);

	larb->smi.clk_smi = devm_clk_get(dev, "smi");
	if (IS_ERR(larb->smi.clk_smi))
		return PTR_ERR(larb->smi.clk_smi);

	if (larb->larb_gen->has_gals) {
		/* The larbs may still haven't gals even if the SoC support.*/
		larb->smi.clk_gals0 = devm_clk_get(dev, "gals");
		if (PTR_ERR(larb->smi.clk_gals0) == -ENOENT)
			larb->smi.clk_gals0 = NULL;
		else if (IS_ERR(larb->smi.clk_gals0))
			return PTR_ERR(larb->smi.clk_gals0);
	}
	larb->smi.dev = dev;

	smi_node = of_parse_phandle(dev->of_node, "mediatek,smi", 0);
	if (!smi_node)
		return -EINVAL;

	smi_pdev = of_find_device_by_node(smi_node);
	of_node_put(smi_node);
	if (smi_pdev) {
		if (!platform_get_drvdata(smi_pdev))
			return -EPROBE_DEFER;
		larb->smi_common_dev = &smi_pdev->dev;
	} else {
		dev_err(dev, "Failed to get the smi_common device\n");
		return -EINVAL;
	}

	pm_runtime_enable(dev);
	platform_set_drvdata(pdev, larb);
	return component_add(dev, &mtk_smi_larb_component_ops);
}

static int mtk_smi_larb_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
	component_del(&pdev->dev, &mtk_smi_larb_component_ops);
	return 0;
}

static int __maybe_unused mtk_smi_larb_resume(struct device *dev)
{
	struct mtk_smi_larb *larb = dev_get_drvdata(dev);
	const struct mtk_smi_larb_gen *larb_gen = larb->larb_gen;
	int ret;

	/* Power on smi-common. */
	ret = pm_runtime_get_sync(larb->smi_common_dev);
	if (ret < 0) {
		dev_err(dev, "Failed to pm get for smi-common(%d).\n", ret);
		return ret;
	}

	ret = mtk_smi_clk_enable(&larb->smi);
	if (ret < 0) {
		dev_err(dev, "Failed to enable clock(%d).\n", ret);
		pm_runtime_put_sync(larb->smi_common_dev);
		return ret;
	}

	/* Configure the basic setting for this larb */
	larb_gen->config_port(dev);

	return 0;
}

static int __maybe_unused mtk_smi_larb_suspend(struct device *dev)
{
	struct mtk_smi_larb *larb = dev_get_drvdata(dev);

	mtk_smi_clk_disable(&larb->smi);
	pm_runtime_put_sync(larb->smi_common_dev);
	return 0;
}

static const struct dev_pm_ops smi_larb_pm_ops = {
	SET_RUNTIME_PM_OPS(mtk_smi_larb_suspend, mtk_smi_larb_resume, NULL)
};

static struct platform_driver mtk_smi_larb_driver = {
	.probe	= mtk_smi_larb_probe,
	.remove	= mtk_smi_larb_remove,
	.driver	= {
		.name = "mtk-smi-larb",
		.of_match_table = mtk_smi_larb_of_ids,
		.pm             = &smi_larb_pm_ops,
	}
};

static const struct mtk_smi_common_plat mtk_smi_common_gen1 = {
	.gen = MTK_SMI_GEN1,
};

static const struct mtk_smi_common_plat mtk_smi_common_gen2 = {
	.gen = MTK_SMI_GEN2,
};

static const struct mtk_smi_common_plat mtk_smi_common_mt8183 = {
	.gen      = MTK_SMI_GEN2,
	.has_gals = true,
	.bus_sel  = F_MMU1_LARB(1) | F_MMU1_LARB(2) | F_MMU1_LARB(5) |
		    F_MMU1_LARB(7),
};

static const struct of_device_id mtk_smi_common_of_ids[] = {
	{
		.compatible = "mediatek,mt8173-smi-common",
		.data = &mtk_smi_common_gen2,
	},
	{
		.compatible = "mediatek,mt2701-smi-common",
		.data = &mtk_smi_common_gen1,
	},
	{
		.compatible = "mediatek,mt2712-smi-common",
		.data = &mtk_smi_common_gen2,
	},
	{
		.compatible = "mediatek,mt8183-smi-common",
		.data = &mtk_smi_common_mt8183,
	},
	{}
};

static int mtk_smi_common_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_smi *common;
	struct resource *res;
	int ret;

	common = devm_kzalloc(dev, sizeof(*common), GFP_KERNEL);
	if (!common)
		return -ENOMEM;
	common->dev = dev;
	common->plat = of_device_get_match_data(dev);

	common->clk_apb = devm_clk_get(dev, "apb");
	if (IS_ERR(common->clk_apb))
		return PTR_ERR(common->clk_apb);

	common->clk_smi = devm_clk_get(dev, "smi");
	if (IS_ERR(common->clk_smi))
		return PTR_ERR(common->clk_smi);

	if (common->plat->has_gals) {
		common->clk_gals0 = devm_clk_get(dev, "gals0");
		if (IS_ERR(common->clk_gals0))
			return PTR_ERR(common->clk_gals0);

		common->clk_gals1 = devm_clk_get(dev, "gals1");
		if (IS_ERR(common->clk_gals1))
			return PTR_ERR(common->clk_gals1);
	}

	/*
	 * for mtk smi gen 1, we need to get the ao(always on) base to config
	 * m4u port, and we need to enable the aync clock for transform the smi
	 * clock into emi clock domain, but for mtk smi gen2, there's no smi ao
	 * base.
	 */
	if (common->plat->gen == MTK_SMI_GEN1) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		common->smi_ao_base = devm_ioremap_resource(dev, res);
		if (IS_ERR(common->smi_ao_base))
			return PTR_ERR(common->smi_ao_base);

		common->clk_async = devm_clk_get(dev, "async");
		if (IS_ERR(common->clk_async))
			return PTR_ERR(common->clk_async);

		ret = clk_prepare_enable(common->clk_async);
		if (ret)
			return ret;
	} else {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		common->base = devm_ioremap_resource(dev, res);
		if (IS_ERR(common->base))
			return PTR_ERR(common->base);
	}
	pm_runtime_enable(dev);
	platform_set_drvdata(pdev, common);
	return 0;
}

static int mtk_smi_common_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
	return 0;
}

static int __maybe_unused mtk_smi_common_resume(struct device *dev)
{
	struct mtk_smi *common = dev_get_drvdata(dev);
	u32 bus_sel = common->plat->bus_sel;
	int ret;

	ret = mtk_smi_clk_enable(common);
	if (ret) {
		dev_err(common->dev, "Failed to enable clock(%d).\n", ret);
		return ret;
	}

	if (common->plat->gen == MTK_SMI_GEN2 && bus_sel)
		writel(bus_sel, common->base + SMI_BUS_SEL);
	return 0;
}

static int __maybe_unused mtk_smi_common_suspend(struct device *dev)
{
	struct mtk_smi *common = dev_get_drvdata(dev);

	mtk_smi_clk_disable(common);
	return 0;
}

static const struct dev_pm_ops smi_common_pm_ops = {
	SET_RUNTIME_PM_OPS(mtk_smi_common_suspend, mtk_smi_common_resume, NULL)
};

static struct platform_driver mtk_smi_common_driver = {
	.probe	= mtk_smi_common_probe,
	.remove = mtk_smi_common_remove,
	.driver	= {
		.name = "mtk-smi-common",
		.of_match_table = mtk_smi_common_of_ids,
		.pm             = &smi_common_pm_ops,
	}
};

static int __init mtk_smi_init(void)
{
	int ret;

	ret = platform_driver_register(&mtk_smi_common_driver);
	if (ret != 0) {
		pr_err("Failed to register SMI driver\n");
		return ret;
	}

	ret = platform_driver_register(&mtk_smi_larb_driver);
	if (ret != 0) {
		pr_err("Failed to register SMI-LARB driver\n");
		goto err_unreg_smi;
	}
	return ret;

err_unreg_smi:
	platform_driver_unregister(&mtk_smi_common_driver);
	return ret;
}

module_init(mtk_smi_init);
