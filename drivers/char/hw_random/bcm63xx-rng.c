/*
 * Broadcom BCM63xx Random Number Generator support
 *
 * Copyright (C) 2011, Florian Fainelli <florian@openwrt.org>
 * Copyright (C) 2009, Broadcom Corporation
 *
 */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/hw_random.h>

#define RNG_CTRL			0x00
#define RNG_EN				(1 << 0)

#define RNG_STAT			0x04
#define RNG_AVAIL_MASK			(0xff000000)

#define RNG_DATA			0x08
#define RNG_THRES			0x0c
#define RNG_MASK			0x10

struct bcm63xx_rng_priv {
	struct hwrng rng;
	struct clk *clk;
	void __iomem *regs;
};

#define to_rng_priv(rng)	container_of(rng, struct bcm63xx_rng_priv, rng)

static int bcm63xx_rng_init(struct hwrng *rng)
{
	struct bcm63xx_rng_priv *priv = to_rng_priv(rng);
	u32 val;
	int error;

	error = clk_prepare_enable(priv->clk);
	if (error)
		return error;

	val = __raw_readl(priv->regs + RNG_CTRL);
	val |= RNG_EN;
	__raw_writel(val, priv->regs + RNG_CTRL);

	return 0;
}

static void bcm63xx_rng_cleanup(struct hwrng *rng)
{
	struct bcm63xx_rng_priv *priv = to_rng_priv(rng);
	u32 val;

	val = __raw_readl(priv->regs + RNG_CTRL);
	val &= ~RNG_EN;
	__raw_writel(val, priv->regs + RNG_CTRL);

	clk_disable_unprepare(priv->clk);
}

static int bcm63xx_rng_data_present(struct hwrng *rng, int wait)
{
	struct bcm63xx_rng_priv *priv = to_rng_priv(rng);

	return __raw_readl(priv->regs + RNG_STAT) & RNG_AVAIL_MASK;
}

static int bcm63xx_rng_data_read(struct hwrng *rng, u32 *data)
{
	struct bcm63xx_rng_priv *priv = to_rng_priv(rng);

	*data = __raw_readl(priv->regs + RNG_DATA);

	return 4;
}

static int bcm63xx_rng_probe(struct platform_device *pdev)
{
	struct resource *r;
	int ret;
	struct bcm63xx_rng_priv *priv;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		dev_err(&pdev->dev, "no iomem resource\n");
		return -ENXIO;
	}

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->rng.name = pdev->name;
	priv->rng.init = bcm63xx_rng_init;
	priv->rng.cleanup = bcm63xx_rng_cleanup;
	priv->rng.data_present = bcm63xx_rng_data_present;
	priv->rng.data_read = bcm63xx_rng_data_read;

	priv->clk = devm_clk_get(&pdev->dev, "ipsec");
	if (IS_ERR(priv->clk)) {
		ret = PTR_ERR(priv->clk);
		dev_err(&pdev->dev, "no clock for device: %d\n", ret);
		return ret;
	}

	if (!devm_request_mem_region(&pdev->dev, r->start,
					resource_size(r), pdev->name)) {
		dev_err(&pdev->dev, "request mem failed");
		return -EBUSY;
	}

	priv->regs = devm_ioremap_nocache(&pdev->dev, r->start,
					resource_size(r));
	if (!priv->regs) {
		dev_err(&pdev->dev, "ioremap failed");
		return -ENOMEM;
	}

	ret = devm_hwrng_register(&pdev->dev, &priv->rng);
	if (ret) {
		dev_err(&pdev->dev, "failed to register rng device: %d\n",
			ret);
		return ret;
	}

	dev_info(&pdev->dev, "registered RNG driver\n");

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id bcm63xx_rng_of_match[] = {
	{ .compatible = "brcm,bcm6368-rng", },
	{},
};
MODULE_DEVICE_TABLE(of, bcm63xx_rng_of_match);
#endif

static struct platform_driver bcm63xx_rng_driver = {
	.probe		= bcm63xx_rng_probe,
	.driver		= {
		.name	= "bcm63xx-rng",
		.of_match_table = of_match_ptr(bcm63xx_rng_of_match),
	},
};

module_platform_driver(bcm63xx_rng_driver);

MODULE_AUTHOR("Florian Fainelli <florian@openwrt.org>");
MODULE_DESCRIPTION("Broadcom BCM63xx RNG driver");
MODULE_LICENSE("GPL");
