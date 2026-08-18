// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Mediatek Hardware Random Number Generator
 *
 * Copyright (C) 2017 Sean Wang <sean.wang@mediatek.com>
 * Copyright (C) 2026 Daniel Golle <daniel@makrotopia.org>
 */
#define MTK_RNG_DEV KBUILD_MODNAME

#include <linux/arm-smccc.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/hw_random.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/soc/mediatek/mtk_sip_svc.h>

/* Runtime PM autosuspend timeout: */
#define RNG_AUTOSUSPEND_TIMEOUT		100

#define USEC_POLL			2
#define TIMEOUT_POLL			60

#define RNG_CTRL			0x00
#define RNG_EN				BIT(0)
#define RNG_READY			BIT(31)

#define RNG_DATA			0x08

/* Driver feature flags */
#define MTK_RNG_SMC			BIT(0)

#define MTK_SIP_KERNEL_GET_RND		MTK_SIP_SMC_CMD(0x550)

#define to_mtk_rng(p)	container_of(p, struct mtk_rng, rng)

struct mtk_rng {
	void __iomem *base;
	struct clk *clk;
	struct hwrng rng;
	struct device *dev;
	unsigned long flags;
};

static int mtk_rng_init(struct hwrng *rng)
{
	struct mtk_rng *priv = to_mtk_rng(rng);
	u32 val;
	int err;

	err = clk_prepare_enable(priv->clk);
	if (err)
		return err;

	val = readl(priv->base + RNG_CTRL);
	val |= RNG_EN;
	writel(val, priv->base + RNG_CTRL);

	return 0;
}

static void mtk_rng_cleanup(struct hwrng *rng)
{
	struct mtk_rng *priv = to_mtk_rng(rng);
	u32 val;

	val = readl(priv->base + RNG_CTRL);
	val &= ~RNG_EN;
	writel(val, priv->base + RNG_CTRL);

	clk_disable_unprepare(priv->clk);
}

static bool mtk_rng_wait_ready(struct hwrng *rng, bool wait)
{
	struct mtk_rng *priv = to_mtk_rng(rng);
	int ready;

	ready = readl(priv->base + RNG_CTRL) & RNG_READY;
	if (!ready && wait)
		readl_poll_timeout_atomic(priv->base + RNG_CTRL, ready,
					  ready & RNG_READY, USEC_POLL,
					  TIMEOUT_POLL);
	return !!(ready & RNG_READY);
}

static int mtk_rng_read(struct hwrng *rng, void *buf, size_t max, bool wait)
{
	struct mtk_rng *priv = to_mtk_rng(rng);
	int retval = 0;

	pm_runtime_get_sync(priv->dev);

	while (max >= sizeof(u32)) {
		if (!mtk_rng_wait_ready(rng, wait))
			break;

		*(u32 *)buf = readl(priv->base + RNG_DATA);
		retval += sizeof(u32);
		buf += sizeof(u32);
		max -= sizeof(u32);
	}

	pm_runtime_put_sync_autosuspend(priv->dev);

	return retval || !wait ? retval : -EIO;
}

static int mtk_rng_read_smc(struct hwrng *rng, void *buf, size_t max,
			    bool wait)
{
	struct arm_smccc_res res;
	int retval = 0;

	while (max >= sizeof(u32)) {
		arm_smccc_smc(MTK_SIP_KERNEL_GET_RND, 0, 0, 0, 0, 0, 0, 0,
			      &res);
		if (res.a0)
			break;

		*(u32 *)buf = res.a1;
		retval += sizeof(u32);
		buf += sizeof(u32);
		max -= sizeof(u32);
	}

	return retval || !wait ? retval : -EIO;
}

static bool mtk_rng_hw_accessible(struct mtk_rng *priv)
{
	u32 val;
	int err;

	err = clk_prepare_enable(priv->clk);
	if (err)
		return false;

	val = readl(priv->base + RNG_CTRL);
	val |= RNG_EN;
	writel(val, priv->base + RNG_CTRL);

	val = readl(priv->base + RNG_CTRL);

	if (val & RNG_EN) {
		/* HW is accessible, clean up: disable RNG and clock */
		writel(val & ~RNG_EN, priv->base + RNG_CTRL);
		clk_disable_unprepare(priv->clk);
		return true;
	}

	/*
	 * If TF-A blocks direct access, the register reads back as 0.
	 * Leave the clock enabled as TF-A needs it.
	 */
	return false;
}

static int mtk_rng_probe(struct platform_device *pdev)
{
	int ret;
	struct mtk_rng *priv;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &pdev->dev;
	priv->rng.name = pdev->name;
	priv->rng.quality = 900;
	priv->flags = (unsigned long)device_get_match_data(&pdev->dev);

	if (!(priv->flags & MTK_RNG_SMC)) {
		priv->clk = devm_clk_get(&pdev->dev, "rng");
		if (IS_ERR(priv->clk)) {
			ret = PTR_ERR(priv->clk);
			dev_err(&pdev->dev, "no clock for device: %d\n", ret);
			return ret;
		}

		priv->base = devm_platform_ioremap_resource(pdev, 0);
		if (IS_ERR(priv->base))
			return PTR_ERR(priv->base);

		if (IS_ENABLED(CONFIG_HAVE_ARM_SMCCC) &&
		    of_device_is_compatible(pdev->dev.of_node,
					    "mediatek,mt7986-rng") &&
		    !mtk_rng_hw_accessible(priv)) {
			priv->flags |= MTK_RNG_SMC;
			dev_info(&pdev->dev,
				 "HW RNG not MMIO accessible, using SMC\n");
		}
	}

	if (priv->flags & MTK_RNG_SMC) {
		if (!IS_ENABLED(CONFIG_HAVE_ARM_SMCCC))
			return -ENODEV;
		priv->rng.read = mtk_rng_read_smc;
	} else {
#ifndef CONFIG_PM
		priv->rng.init = mtk_rng_init;
		priv->rng.cleanup = mtk_rng_cleanup;
#endif
		priv->rng.read = mtk_rng_read;
	}

	ret = devm_hwrng_register(&pdev->dev, &priv->rng);
	if (ret) {
		dev_err(&pdev->dev, "failed to register rng device: %d\n",
			ret);
		return ret;
	}

	if (!(priv->flags & MTK_RNG_SMC)) {
		dev_set_drvdata(&pdev->dev, priv);
		pm_runtime_set_autosuspend_delay(&pdev->dev,
						 RNG_AUTOSUSPEND_TIMEOUT);
		pm_runtime_use_autosuspend(&pdev->dev);
		ret = devm_pm_runtime_enable(&pdev->dev);
		if (ret)
			return ret;
	}

	dev_info(&pdev->dev, "registered RNG driver\n");

	return 0;
}

#ifdef CONFIG_PM
static int mtk_rng_runtime_suspend(struct device *dev)
{
	struct mtk_rng *priv = dev_get_drvdata(dev);

	mtk_rng_cleanup(&priv->rng);

	return 0;
}

static int mtk_rng_runtime_resume(struct device *dev)
{
	struct mtk_rng *priv = dev_get_drvdata(dev);

	return mtk_rng_init(&priv->rng);
}

static const struct dev_pm_ops mtk_rng_pm_ops = {
	SET_RUNTIME_PM_OPS(mtk_rng_runtime_suspend,
			   mtk_rng_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

#define MTK_RNG_PM_OPS (&mtk_rng_pm_ops)
#else	/* CONFIG_PM */
#define MTK_RNG_PM_OPS NULL
#endif	/* CONFIG_PM */

static const struct of_device_id mtk_rng_match[] = {
	{ .compatible = "mediatek,mt7623-rng" },
	{ .compatible = "mediatek,mt7981-rng", .data = (void *)MTK_RNG_SMC },
	{ .compatible = "mediatek,mt7986-rng" },
	{ .compatible = "mediatek,mt7987-rng", .data = (void *)MTK_RNG_SMC },
	{ .compatible = "mediatek,mt7988-rng", .data = (void *)MTK_RNG_SMC },
	{},
};
MODULE_DEVICE_TABLE(of, mtk_rng_match);

static struct platform_driver mtk_rng_driver = {
	.probe          = mtk_rng_probe,
	.driver = {
		.name = MTK_RNG_DEV,
		.pm = MTK_RNG_PM_OPS,
		.of_match_table = mtk_rng_match,
	},
};

module_platform_driver(mtk_rng_driver);

MODULE_DESCRIPTION("Mediatek Random Number Generator Driver");
MODULE_AUTHOR("Sean Wang <sean.wang@mediatek.com>");
MODULE_AUTHOR("Daniel Golle <daniel@makrotopia.org>");
MODULE_LICENSE("GPL");
