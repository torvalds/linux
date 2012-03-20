/*
 * Copyright (c) 2010-2011 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/hw_random.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define DATA_REG_OFFSET		0x0200
#define CSR_REG_OFFSET		0x0278
#define CSR_OUT_EMPTY_MASK	(1 << 24)
#define CSR_FAULT_MASK		(1 << 1)
#define TRNG_BLOCK_RESET_MASK	(1 << 0)
#define TAI_REG_OFFSET		0x0380

/*
 * The maximum amount of time in microseconds to spend waiting for data if the
 * core wants us to wait.  The TRNG should generate 32 bits every 320ns so a
 * timeout of 20us seems reasonable.  The TRNG does builtin tests of the data
 * for randomness so we can't always assume there is data present.
 */
#define PICO_TRNG_TIMEOUT		20

static void __iomem *rng_base;
static struct clk *rng_clk;
struct device *rng_dev;

static inline u32 picoxcell_trng_read_csr(void)
{
	return __raw_readl(rng_base + CSR_REG_OFFSET);
}

static inline bool picoxcell_trng_is_empty(void)
{
	return picoxcell_trng_read_csr() & CSR_OUT_EMPTY_MASK;
}

/*
 * Take the random number generator out of reset and make sure the interrupts
 * are masked. We shouldn't need to get large amounts of random bytes so just
 * poll the status register. The hardware generates 32 bits every 320ns so we
 * shouldn't have to wait long enough to warrant waiting for an IRQ.
 */
static void picoxcell_trng_start(void)
{
	__raw_writel(0, rng_base + TAI_REG_OFFSET);
	__raw_writel(0, rng_base + CSR_REG_OFFSET);
}

static void picoxcell_trng_reset(void)
{
	__raw_writel(TRNG_BLOCK_RESET_MASK, rng_base + CSR_REG_OFFSET);
	__raw_writel(TRNG_BLOCK_RESET_MASK, rng_base + TAI_REG_OFFSET);
	picoxcell_trng_start();
}

/*
 * Get some random data from the random number generator. The hw_random core
 * layer provides us with locking.
 */
static int picoxcell_trng_read(struct hwrng *rng, void *buf, size_t max,
			       bool wait)
{
	int i;

	/* Wait for some data to become available. */
	for (i = 0; i < PICO_TRNG_TIMEOUT && picoxcell_trng_is_empty(); ++i) {
		if (!wait)
			return 0;

		udelay(1);
	}

	if (picoxcell_trng_read_csr() & CSR_FAULT_MASK) {
		dev_err(rng_dev, "fault detected, resetting TRNG\n");
		picoxcell_trng_reset();
		return -EIO;
	}

	if (i == PICO_TRNG_TIMEOUT)
		return 0;

	*(u32 *)buf = __raw_readl(rng_base + DATA_REG_OFFSET);
	return sizeof(u32);
}

static struct hwrng picoxcell_trng = {
	.name		= "picoxcell",
	.read		= picoxcell_trng_read,
};

static int picoxcell_trng_probe(struct platform_device *pdev)
{
	int ret;
	struct resource *mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (!mem) {
		dev_warn(&pdev->dev, "no memory resource\n");
		return -ENOMEM;
	}

	if (!devm_request_mem_region(&pdev->dev, mem->start, resource_size(mem),
				     "picoxcell_trng")) {
		dev_warn(&pdev->dev, "unable to request io mem\n");
		return -EBUSY;
	}

	rng_base = devm_ioremap(&pdev->dev, mem->start, resource_size(mem));
	if (!rng_base) {
		dev_warn(&pdev->dev, "unable to remap io mem\n");
		return -ENOMEM;
	}

	rng_clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(rng_clk)) {
		dev_warn(&pdev->dev, "no clk\n");
		return PTR_ERR(rng_clk);
	}

	ret = clk_enable(rng_clk);
	if (ret) {
		dev_warn(&pdev->dev, "unable to enable clk\n");
		goto err_enable;
	}

	picoxcell_trng_start();
	ret = hwrng_register(&picoxcell_trng);
	if (ret)
		goto err_register;

	rng_dev = &pdev->dev;
	dev_info(&pdev->dev, "pixoxcell random number generator active\n");

	return 0;

err_register:
	clk_disable(rng_clk);
err_enable:
	clk_put(rng_clk);

	return ret;
}

static int __devexit picoxcell_trng_remove(struct platform_device *pdev)
{
	hwrng_unregister(&picoxcell_trng);
	clk_disable(rng_clk);
	clk_put(rng_clk);

	return 0;
}

#ifdef CONFIG_PM
static int picoxcell_trng_suspend(struct device *dev)
{
	clk_disable(rng_clk);

	return 0;
}

static int picoxcell_trng_resume(struct device *dev)
{
	return clk_enable(rng_clk);
}

static const struct dev_pm_ops picoxcell_trng_pm_ops = {
	.suspend	= picoxcell_trng_suspend,
	.resume		= picoxcell_trng_resume,
};
#endif /* CONFIG_PM */

static struct platform_driver picoxcell_trng_driver = {
	.probe		= picoxcell_trng_probe,
	.remove		= __devexit_p(picoxcell_trng_remove),
	.driver		= {
		.name	= "picoxcell-trng",
		.owner	= THIS_MODULE,
#ifdef CONFIG_PM
		.pm	= &picoxcell_trng_pm_ops,
#endif /* CONFIG_PM */
	},
};

module_platform_driver(picoxcell_trng_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jamie Iles");
MODULE_DESCRIPTION("Picochip picoXcell TRNG driver");
