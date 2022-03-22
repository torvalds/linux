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
#include <linux/iopoll.h>
#include <linux/hw_random.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#define TRNG_CR		0x00
#define TRNG_MR		0x04
#define TRNG_ISR	0x1c
#define TRNG_ISR_DATRDY	BIT(0)
#define TRNG_ODATA	0x50

#define TRNG_KEY	0x524e4700 /* RNG */

#define TRNG_HALFR	BIT(0) /* generate RN every 168 cycles */

struct atmel_trng_data {
	bool has_half_rate;
};

struct atmel_trng {
	struct clk *clk;
	void __iomem *base;
	struct hwrng rng;
	bool has_half_rate;
};

static bool atmel_trng_wait_ready(struct atmel_trng *trng, bool wait)
{
	int ready;

	ready = readl(trng->base + TRNG_ISR) & TRNG_ISR_DATRDY;
	if (!ready && wait)
		readl_poll_timeout(trng->base + TRNG_ISR, ready,
				   ready & TRNG_ISR_DATRDY, 1000, 20000);

	return !!ready;
}

static int atmel_trng_read(struct hwrng *rng, void *buf, size_t max,
			   bool wait)
{
	struct atmel_trng *trng = container_of(rng, struct atmel_trng, rng);
	u32 *data = buf;
	int ret;

	ret = pm_runtime_get_sync((struct device *)trng->rng.priv);
	if (ret < 0) {
		pm_runtime_put_sync((struct device *)trng->rng.priv);
		return ret;
	}

	ret = atmel_trng_wait_ready(trng, wait);
	if (!ret)
		goto out;

	*data = readl(trng->base + TRNG_ODATA);
	/*
	 * ensure data ready is only set again AFTER the next data word is ready
	 * in case it got set between checking ISR and reading ODATA, so we
	 * don't risk re-reading the same word
	 */
	readl(trng->base + TRNG_ISR);
	ret = 4;

out:
	pm_runtime_mark_last_busy((struct device *)trng->rng.priv);
	pm_runtime_put_sync_autosuspend((struct device *)trng->rng.priv);
	return ret;
}

static int atmel_trng_init(struct atmel_trng *trng)
{
	unsigned long rate;
	int ret;

	ret = clk_prepare_enable(trng->clk);
	if (ret)
		return ret;

	if (trng->has_half_rate) {
		rate = clk_get_rate(trng->clk);

		/* if peripheral clk is above 100MHz, set HALFR */
		if (rate > 100000000)
			writel(TRNG_HALFR, trng->base + TRNG_MR);
	}

	writel(TRNG_KEY | 1, trng->base + TRNG_CR);

	return 0;
}

static void atmel_trng_cleanup(struct atmel_trng *trng)
{
	writel(TRNG_KEY, trng->base + TRNG_CR);
	clk_disable_unprepare(trng->clk);
}

static int atmel_trng_probe(struct platform_device *pdev)
{
	struct atmel_trng *trng;
	const struct atmel_trng_data *data;
	int ret;

	trng = devm_kzalloc(&pdev->dev, sizeof(*trng), GFP_KERNEL);
	if (!trng)
		return -ENOMEM;

	trng->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(trng->base))
		return PTR_ERR(trng->base);

	trng->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(trng->clk))
		return PTR_ERR(trng->clk);
	data = of_device_get_match_data(&pdev->dev);
	if (!data)
		return -ENODEV;

	trng->has_half_rate = data->has_half_rate;
	trng->rng.name = pdev->name;
	trng->rng.read = atmel_trng_read;
	trng->rng.priv = (unsigned long)&pdev->dev;
	platform_set_drvdata(pdev, trng);

#ifndef CONFIG_PM
	ret = atmel_trng_init(trng);
	if (ret)
		return ret;
#endif

	pm_runtime_set_autosuspend_delay(&pdev->dev, 100);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	ret = devm_hwrng_register(&pdev->dev, &trng->rng);
	if (ret) {
		pm_runtime_disable(&pdev->dev);
		pm_runtime_set_suspended(&pdev->dev);
#ifndef CONFIG_PM
		atmel_trng_cleanup(trng);
#endif
	}

	return ret;
}

static int atmel_trng_remove(struct platform_device *pdev)
{
	struct atmel_trng *trng = platform_get_drvdata(pdev);

	atmel_trng_cleanup(trng);
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);

	return 0;
}

static int __maybe_unused atmel_trng_runtime_suspend(struct device *dev)
{
	struct atmel_trng *trng = dev_get_drvdata(dev);

	atmel_trng_cleanup(trng);

	return 0;
}

static int __maybe_unused atmel_trng_runtime_resume(struct device *dev)
{
	struct atmel_trng *trng = dev_get_drvdata(dev);

	return atmel_trng_init(trng);
}

static const struct dev_pm_ops __maybe_unused atmel_trng_pm_ops = {
	SET_RUNTIME_PM_OPS(atmel_trng_runtime_suspend,
			   atmel_trng_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static const struct atmel_trng_data at91sam9g45_config = {
	.has_half_rate = false,
};

static const struct atmel_trng_data sam9x60_config = {
	.has_half_rate = true,
};

static const struct of_device_id atmel_trng_dt_ids[] = {
	{
		.compatible = "atmel,at91sam9g45-trng",
		.data = &at91sam9g45_config,
	}, {
		.compatible = "microchip,sam9x60-trng",
		.data = &sam9x60_config,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, atmel_trng_dt_ids);

static struct platform_driver atmel_trng_driver = {
	.probe		= atmel_trng_probe,
	.remove		= atmel_trng_remove,
	.driver		= {
		.name	= "atmel-trng",
		.pm	= pm_ptr(&atmel_trng_pm_ops),
		.of_match_table = atmel_trng_dt_ids,
	},
};

module_platform_driver(atmel_trng_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Peter Korsgaard <jacmet@sunsite.dk>");
MODULE_DESCRIPTION("Atmel true random number generator driver");
