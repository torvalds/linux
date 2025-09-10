// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Nuvoton Technology corporation.

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/init.h>
#include <linux/random.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/hw_random.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>

#define NPCM_RNGCS_REG		0x00	/* Control and status register */
#define NPCM_RNGD_REG		0x04	/* Data register */
#define NPCM_RNGMODE_REG	0x08	/* Mode register */

#define NPCM_RNG_CLK_SET_62_5MHZ	BIT(2) /* 60-80 MHz */
#define NPCM_RNG_CLK_SET_25MHZ	GENMASK(4, 3) /* 20-25 MHz */
#define NPCM_RNG_DATA_VALID	BIT(1)
#define NPCM_RNG_ENABLE		BIT(0)
#define NPCM_RNG_M1ROSEL	BIT(1)

#define NPCM_RNG_TIMEOUT_USEC	20000
#define NPCM_RNG_POLL_USEC	1000

#define to_npcm_rng(p)	container_of(p, struct npcm_rng, rng)

struct npcm_rng {
	void __iomem *base;
	struct hwrng rng;
	struct device *dev;
	u32 clkp;
};

static int npcm_rng_init(struct hwrng *rng)
{
	struct npcm_rng *priv = to_npcm_rng(rng);

	writel(priv->clkp | NPCM_RNG_ENABLE, priv->base + NPCM_RNGCS_REG);

	return 0;
}

static void npcm_rng_cleanup(struct hwrng *rng)
{
	struct npcm_rng *priv = to_npcm_rng(rng);

	writel(priv->clkp, priv->base + NPCM_RNGCS_REG);
}

static int npcm_rng_read(struct hwrng *rng, void *buf, size_t max, bool wait)
{
	struct npcm_rng *priv = to_npcm_rng(rng);
	int retval = 0;
	int ready;

	pm_runtime_get_sync(priv->dev);

	while (max) {
		if (wait) {
			if (readb_poll_timeout(priv->base + NPCM_RNGCS_REG,
					       ready,
					       ready & NPCM_RNG_DATA_VALID,
					       NPCM_RNG_POLL_USEC,
					       NPCM_RNG_TIMEOUT_USEC))
				break;
		} else {
			if ((readb(priv->base + NPCM_RNGCS_REG) &
			    NPCM_RNG_DATA_VALID) == 0)
				break;
		}

		*(u8 *)buf = readb(priv->base + NPCM_RNGD_REG);
		retval++;
		buf++;
		max--;
	}

	pm_runtime_put_sync_autosuspend(priv->dev);

	return retval || !wait ? retval : -EIO;
}

static int npcm_rng_probe(struct platform_device *pdev)
{
	struct npcm_rng *priv;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	dev_set_drvdata(&pdev->dev, priv);
	pm_runtime_set_autosuspend_delay(&pdev->dev, 100);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

#ifndef CONFIG_PM
	priv->rng.init = npcm_rng_init;
	priv->rng.cleanup = npcm_rng_cleanup;
#endif
	priv->rng.name = pdev->name;
	priv->rng.read = npcm_rng_read;
	priv->dev = &pdev->dev;
	priv->clkp = (u32)(uintptr_t)of_device_get_match_data(&pdev->dev);

	writel(NPCM_RNG_M1ROSEL, priv->base + NPCM_RNGMODE_REG);

	ret = devm_hwrng_register(&pdev->dev, &priv->rng);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register rng device: %d\n",
			ret);
		pm_runtime_disable(&pdev->dev);
		pm_runtime_set_suspended(&pdev->dev);
		return ret;
	}

	return 0;
}

static void npcm_rng_remove(struct platform_device *pdev)
{
	struct npcm_rng *priv = platform_get_drvdata(pdev);

	devm_hwrng_unregister(&pdev->dev, &priv->rng);
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
}

#ifdef CONFIG_PM
static int npcm_rng_runtime_suspend(struct device *dev)
{
	struct npcm_rng *priv = dev_get_drvdata(dev);

	npcm_rng_cleanup(&priv->rng);

	return 0;
}

static int npcm_rng_runtime_resume(struct device *dev)
{
	struct npcm_rng *priv = dev_get_drvdata(dev);

	return npcm_rng_init(&priv->rng);
}
#endif

static const struct dev_pm_ops npcm_rng_pm_ops = {
	SET_RUNTIME_PM_OPS(npcm_rng_runtime_suspend,
			   npcm_rng_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static const struct of_device_id rng_dt_id[] __maybe_unused = {
	{ .compatible = "nuvoton,npcm750-rng",
		.data = (void *)NPCM_RNG_CLK_SET_25MHZ },
	{ .compatible = "nuvoton,npcm845-rng",
		.data = (void *)NPCM_RNG_CLK_SET_62_5MHZ },
	{},
};
MODULE_DEVICE_TABLE(of, rng_dt_id);

static struct platform_driver npcm_rng_driver = {
	.driver = {
		.name		= "npcm-rng",
		.pm		= &npcm_rng_pm_ops,
		.of_match_table = of_match_ptr(rng_dt_id),
	},
	.probe		= npcm_rng_probe,
	.remove		= npcm_rng_remove,
};

module_platform_driver(npcm_rng_driver);

MODULE_DESCRIPTION("Nuvoton NPCM Random Number Generator Driver");
MODULE_AUTHOR("Tomer Maimon <tomer.maimon@nuvoton.com>");
MODULE_LICENSE("GPL v2");
