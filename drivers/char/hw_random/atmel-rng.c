/*
 * Copyright (c) 2011 Peter Korsgaard <jacmet@sunsite.dk>
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/module.h>
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
	if (readl(trng->base + TRNG_ODATA) & 1) {
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

static int atmel_trng_probe(struct platform_device *pdev)
{
	struct atmel_trng *trng;
	struct resource *res;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;

	trng = devm_kzalloc(&pdev->dev, sizeof(*trng), GFP_KERNEL);
	if (!trng)
		return -ENOMEM;

	if (!devm_request_mem_region(&pdev->dev, res->start,
				     resource_size(res), pdev->name))
		return -EBUSY;

	trng->base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!trng->base)
		return -EBUSY;

	trng->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(trng->clk))
		return PTR_ERR(trng->clk);

	ret = clk_enable(trng->clk);
	if (ret)
		goto err_enable;

	writel(TRNG_KEY | 1, trng->base + TRNG_CR);
	trng->rng.name = pdev->name;
	trng->rng.read = atmel_trng_read;

	ret = hwrng_register(&trng->rng);
	if (ret)
		goto err_register;

	platform_set_drvdata(pdev, trng);

	return 0;

err_register:
	clk_disable(trng->clk);
err_enable:
	clk_put(trng->clk);

	return ret;
}

static int __devexit atmel_trng_remove(struct platform_device *pdev)
{
	struct atmel_trng *trng = platform_get_drvdata(pdev);

	hwrng_unregister(&trng->rng);

	writel(TRNG_KEY, trng->base + TRNG_CR);
	clk_disable(trng->clk);
	clk_put(trng->clk);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_PM
static int atmel_trng_suspend(struct device *dev)
{
	struct atmel_trng *trng = dev_get_drvdata(dev);

	clk_disable(trng->clk);

	return 0;
}

static int atmel_trng_resume(struct device *dev)
{
	struct atmel_trng *trng = dev_get_drvdata(dev);

	return clk_enable(trng->clk);
}

static const struct dev_pm_ops atmel_trng_pm_ops = {
	.suspend	= atmel_trng_suspend,
	.resume		= atmel_trng_resume,
};
#endif /* CONFIG_PM */

static struct platform_driver atmel_trng_driver = {
	.probe		= atmel_trng_probe,
	.remove		= __devexit_p(atmel_trng_remove),
	.driver		= {
		.name	= "atmel-trng",
		.owner	= THIS_MODULE,
#ifdef CONFIG_PM
		.pm	= &atmel_trng_pm_ops,
#endif /* CONFIG_PM */
	},
};

module_platform_driver(atmel_trng_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Peter Korsgaard <jacmet@sunsite.dk>");
MODULE_DESCRIPTION("Atmel true random number generator driver");
