// SPDX-License-Identifier: GPL-2.0-only
/*
 * Atmel SSC driver
 *
 * Copyright (C) 2007 Atmel Corporation
 */

#include <linux/platform_device.h>
#include <linux/list.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/atmel-ssc.h>
#include <linux/slab.h>
#include <linux/module.h>

#include <linux/of.h>

#include "../../sound/soc/atmel/atmel_ssc_dai.h"

/* Serialize access to ssc_list and user count */
static DEFINE_SPINLOCK(user_lock);
static LIST_HEAD(ssc_list);

struct ssc_device *ssc_request(unsigned int ssc_num)
{
	int ssc_valid = 0;
	struct ssc_device *ssc;

	spin_lock(&user_lock);
	list_for_each_entry(ssc, &ssc_list, list) {
		if (ssc->pdev->dev.of_node) {
			if (of_alias_get_id(ssc->pdev->dev.of_node, "ssc")
				== ssc_num) {
				ssc->pdev->id = ssc_num;
				ssc_valid = 1;
				break;
			}
		} else if (ssc->pdev->id == ssc_num) {
			ssc_valid = 1;
			break;
		}
	}

	if (!ssc_valid) {
		spin_unlock(&user_lock);
		pr_err("ssc: ssc%d platform device is missing\n", ssc_num);
		return ERR_PTR(-ENODEV);
	}

	if (ssc->user) {
		spin_unlock(&user_lock);
		dev_dbg(&ssc->pdev->dev, "module busy\n");
		return ERR_PTR(-EBUSY);
	}
	ssc->user++;
	spin_unlock(&user_lock);

	clk_prepare(ssc->clk);

	return ssc;
}
EXPORT_SYMBOL(ssc_request);

void ssc_free(struct ssc_device *ssc)
{
	bool disable_clk = true;

	spin_lock(&user_lock);
	if (ssc->user)
		ssc->user--;
	else {
		disable_clk = false;
		dev_dbg(&ssc->pdev->dev, "device already free\n");
	}
	spin_unlock(&user_lock);

	if (disable_clk)
		clk_unprepare(ssc->clk);
}
EXPORT_SYMBOL(ssc_free);

static struct atmel_ssc_platform_data at91rm9200_config = {
	.use_dma = 0,
	.has_fslen_ext = 0,
};

static struct atmel_ssc_platform_data at91sam9rl_config = {
	.use_dma = 0,
	.has_fslen_ext = 1,
};

static struct atmel_ssc_platform_data at91sam9g45_config = {
	.use_dma = 1,
	.has_fslen_ext = 1,
};

static const struct platform_device_id atmel_ssc_devtypes[] = {
	{
		.name = "at91rm9200_ssc",
		.driver_data = (unsigned long) &at91rm9200_config,
	}, {
		.name = "at91sam9rl_ssc",
		.driver_data = (unsigned long) &at91sam9rl_config,
	}, {
		.name = "at91sam9g45_ssc",
		.driver_data = (unsigned long) &at91sam9g45_config,
	}, {
		/* sentinel */
	}
};

#ifdef CONFIG_OF
static const struct of_device_id atmel_ssc_dt_ids[] = {
	{
		.compatible = "atmel,at91rm9200-ssc",
		.data = &at91rm9200_config,
	}, {
		.compatible = "atmel,at91sam9rl-ssc",
		.data = &at91sam9rl_config,
	}, {
		.compatible = "atmel,at91sam9g45-ssc",
		.data = &at91sam9g45_config,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, atmel_ssc_dt_ids);
#endif

static inline const struct atmel_ssc_platform_data *
	atmel_ssc_get_driver_data(struct platform_device *pdev)
{
	if (pdev->dev.of_node) {
		const struct of_device_id *match;
		match = of_match_node(atmel_ssc_dt_ids, pdev->dev.of_node);
		if (match == NULL)
			return NULL;
		return match->data;
	}

	return (struct atmel_ssc_platform_data *)
		platform_get_device_id(pdev)->driver_data;
}

#ifdef CONFIG_SND_ATMEL_SOC_SSC
static int ssc_sound_dai_probe(struct ssc_device *ssc)
{
	struct device_node *np = ssc->pdev->dev.of_node;
	int ret;
	int id;

	ssc->sound_dai = false;

	if (!of_property_read_bool(np, "#sound-dai-cells"))
		return 0;

	id = of_alias_get_id(np, "ssc");
	if (id < 0)
		return id;

	ret = atmel_ssc_set_audio(id);
	ssc->sound_dai = !ret;

	return ret;
}

static void ssc_sound_dai_remove(struct ssc_device *ssc)
{
	if (!ssc->sound_dai)
		return;

	atmel_ssc_put_audio(of_alias_get_id(ssc->pdev->dev.of_node, "ssc"));
}
#else
static inline int ssc_sound_dai_probe(struct ssc_device *ssc)
{
	if (of_property_read_bool(ssc->pdev->dev.of_node, "#sound-dai-cells"))
		return -ENOTSUPP;

	return 0;
}

static inline void ssc_sound_dai_remove(struct ssc_device *ssc)
{
}
#endif

static int ssc_probe(struct platform_device *pdev)
{
	struct resource *regs;
	struct ssc_device *ssc;
	const struct atmel_ssc_platform_data *plat_dat;

	ssc = devm_kzalloc(&pdev->dev, sizeof(struct ssc_device), GFP_KERNEL);
	if (!ssc) {
		dev_dbg(&pdev->dev, "out of memory\n");
		return -ENOMEM;
	}

	ssc->pdev = pdev;

	plat_dat = atmel_ssc_get_driver_data(pdev);
	if (!plat_dat)
		return -ENODEV;
	ssc->pdata = (struct atmel_ssc_platform_data *)plat_dat;

	if (pdev->dev.of_node) {
		struct device_node *np = pdev->dev.of_node;
		ssc->clk_from_rk_pin =
			of_property_read_bool(np, "atmel,clk-from-rk-pin");
	}

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ssc->regs = devm_ioremap_resource(&pdev->dev, regs);
	if (IS_ERR(ssc->regs))
		return PTR_ERR(ssc->regs);

	ssc->phybase = regs->start;

	ssc->clk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(ssc->clk)) {
		dev_dbg(&pdev->dev, "no pclk clock defined\n");
		return -ENXIO;
	}

	/* disable all interrupts */
	clk_prepare_enable(ssc->clk);
	ssc_writel(ssc->regs, IDR, -1);
	ssc_readl(ssc->regs, SR);
	clk_disable_unprepare(ssc->clk);

	ssc->irq = platform_get_irq(pdev, 0);
	if (!ssc->irq) {
		dev_dbg(&pdev->dev, "could not get irq\n");
		return -ENXIO;
	}

	spin_lock(&user_lock);
	list_add_tail(&ssc->list, &ssc_list);
	spin_unlock(&user_lock);

	platform_set_drvdata(pdev, ssc);

	dev_info(&pdev->dev, "Atmel SSC device at 0x%p (irq %d)\n",
			ssc->regs, ssc->irq);

	if (ssc_sound_dai_probe(ssc))
		dev_err(&pdev->dev, "failed to auto-setup ssc for audio\n");

	return 0;
}

static int ssc_remove(struct platform_device *pdev)
{
	struct ssc_device *ssc = platform_get_drvdata(pdev);

	ssc_sound_dai_remove(ssc);

	spin_lock(&user_lock);
	list_del(&ssc->list);
	spin_unlock(&user_lock);

	return 0;
}

static struct platform_driver ssc_driver = {
	.driver		= {
		.name		= "ssc",
		.of_match_table	= of_match_ptr(atmel_ssc_dt_ids),
	},
	.id_table	= atmel_ssc_devtypes,
	.probe		= ssc_probe,
	.remove		= ssc_remove,
};
module_platform_driver(ssc_driver);

MODULE_AUTHOR("Hans-Christian Egtvedt <hcegtvedt@atmel.com>");
MODULE_DESCRIPTION("SSC driver for Atmel AVR32 and AT91");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ssc");
