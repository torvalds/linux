/*
 * Copyright (c) 2011 Peter Korsgaard <jacmet@sunsite.dk>
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/hw_random.h>
#include <linux/platform_device.h>

#define TRNG_CR		0x00
#define TRNG_ISR	0x1c
#define TRNG_ODATA	0x50

#define TRNG_KEY	0x524e4700 /* RNG */

struct atmel_trng {
	struct clk *clk;
	void __iomem *base;
	struct hwrng rng;
};

static int atmel_trng_read(struct hwrng *rng, void *buf, size_t max,
			   bool wait)
{
	struct atmel_trng *trng = container_of(rng, struct atmel_trng, rng);
	u32 *data = buf;

	/* data ready? */
	if (readl(trng->base + TRNG_ISR) & 1) {
		*data = readl(trng->base + TRNG_ODATA);
		/*
		  ensure data ready is only set again AFTER the next data
		  word is ready in case it got set between checking ISR
		  and reading ODATA, so we don't risk re-reading the
		  same word
		*/
		readl(trng->base + TRNG_ISR);
		return 4;
	} else
		return 0;
}

static void atmel_trng_enable(struct atmel_trng *trng)
{
	writel(TRNG_KEY | 1, trng->base + TRNG_CR);
}

static void atmel_trng_disable(struct atmel_trng *trng)
{
	writel(TRNG_KEY, trng->base + TRNG_CR);
}

static int atmel_trng_probe(struct platform_device *pdev)
{
	struct atmel_trng *trng;
	struct resource *res;
	int ret;

	trng = devm_kzalloc(&pdev->dev, sizeof(*trng), GFP_KERNEL);
	if (!trng)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	trng->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(trng->base))
		return PTR_ERR(trng->base);

	trng->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(trng->clk))
		return PTR_ERR(trng->clk);

	ret = clk_prepare_enable(trng->clk);
	if (ret)
		return ret;

	atmel_trng_enable(trng);
	trng->rng.name = pdev->name;
	trng->rng.read = atmel_trng_read;

	ret = hwrng_register(&trng->rng);
	if (ret)
		goto err_register;

	platform_set_drvdata(pdev, trng);

	return 0;

err_register:
	clk_disable_unprepare(trng->clk);
	return ret;
}

static int atmel_trng_remove(struct platform_device *pdev)
{
	struct atmel_trng *trng = platform_get_drvdata(pdev);

	hwrng_unregister(&trng->rng);

	atmel_trng_disable(trng);
	clk_disable_unprepare(trng->clk);

	return 0;
}

#ifdef CONFIG_PM
static int atmel_trng_suspend(struct device *dev)
{
	struct atmel_trng *trng = dev_get_drvdata(dev);

	atmel_trng_disable(trng);
	clk_disable_unprepare(trng->clk);

	return 0;
}

static int atmel_trng_resume(struct device *dev)
{
	struct atmel_trng *trng = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(trng->clk);
	if (ret)
		return ret;

	atmel_trng_enable(trng);

	return 0;
}

static const struct dev_pm_ops atmel_trng_pm_ops = {
	.suspend	= atmel_trng_suspend,
	.resume		= atmel_trng_resume,
};
#endif /* CONFIG_PM */

static const struct of_device_id atmel_trng_dt_ids[] = {
	{ .compatible = "atmel,at91sam9g45-trng" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, atmel_trng_dt_ids);

static struct platform_driver atmel_trng_driver = {
	.probe		= atmel_trng_probe,
	.remove		= atmel_trng_remove,
	.driver		= {
		.name	= "atmel-trng",
#ifdef CONFIG_PM
		.pm	= &atmel_trng_pm_ops,
#endif /* CONFIG_PM */
		.of_match_table = atmel_trng_dt_ids,
	},
};

module_platform_driver(atmel_trng_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Peter Korsgaard <jacmet@sunsite.dk>");
MODULE_DESCRIPTION("Atmel true random number generator driver");
