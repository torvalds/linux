/*
 * OpenFirmware bindings for Secure Digital Host Controller Interface.
 *
 * Copyright (c) 2007 Freescale Semiconductor, Inc.
 * Copyright (c) 2009 MontaVista Software, Inc.
 *
 * Authors: Xiaobo Xie <X.Xie@freescale.com>
 *	    Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/mmc/host.h>
#ifdef CONFIG_PPC
#include <asm/machdep.h>
#endif
#include "sdhci-of.h"
#include "sdhci.h"

#ifdef CONFIG_MMC_SDHCI_BIG_ENDIAN_32BIT_BYTE_SWAPPER

/*
 * These accessors are designed for big endian hosts doing I/O to
 * little endian controllers incorporating a 32-bit hardware byte swapper.
 */

u32 sdhci_be32bs_readl(struct sdhci_host *host, int reg)
{
	return in_be32(host->ioaddr + reg);
}

u16 sdhci_be32bs_readw(struct sdhci_host *host, int reg)
{
	return in_be16(host->ioaddr + (reg ^ 0x2));
}

u8 sdhci_be32bs_readb(struct sdhci_host *host, int reg)
{
	return in_8(host->ioaddr + (reg ^ 0x3));
}

void sdhci_be32bs_writel(struct sdhci_host *host, u32 val, int reg)
{
	out_be32(host->ioaddr + reg, val);
}

void sdhci_be32bs_writew(struct sdhci_host *host, u16 val, int reg)
{
	struct sdhci_of_host *of_host = sdhci_priv(host);
	int base = reg & ~0x3;
	int shift = (reg & 0x2) * 8;

	switch (reg) {
	case SDHCI_TRANSFER_MODE:
		/*
		 * Postpone this write, we must do it together with a
		 * command write that is down below.
		 */
		of_host->xfer_mode_shadow = val;
		return;
	case SDHCI_COMMAND:
		sdhci_be32bs_writel(host, val << 16 | of_host->xfer_mode_shadow,
				    SDHCI_TRANSFER_MODE);
		return;
	}
	clrsetbits_be32(host->ioaddr + base, 0xffff << shift, val << shift);
}

void sdhci_be32bs_writeb(struct sdhci_host *host, u8 val, int reg)
{
	int base = reg & ~0x3;
	int shift = (reg & 0x3) * 8;

	clrsetbits_be32(host->ioaddr + base , 0xff << shift, val << shift);
}
#endif /* CONFIG_MMC_SDHCI_BIG_ENDIAN_32BIT_BYTE_SWAPPER */

#ifdef CONFIG_PM

static int sdhci_of_suspend(struct platform_device *ofdev, pm_message_t state)
{
	struct sdhci_host *host = dev_get_drvdata(&ofdev->dev);

	return mmc_suspend_host(host->mmc);
}

static int sdhci_of_resume(struct platform_device *ofdev)
{
	struct sdhci_host *host = dev_get_drvdata(&ofdev->dev);

	return mmc_resume_host(host->mmc);
}

#else

#define sdhci_of_suspend NULL
#define sdhci_of_resume NULL

#endif

static bool __devinit sdhci_of_wp_inverted(struct device_node *np)
{
	if (of_get_property(np, "sdhci,wp-inverted", NULL))
		return true;

	/* Old device trees don't have the wp-inverted property. */
#ifdef CONFIG_PPC
	return machine_is(mpc837x_rdb) || machine_is(mpc837x_mds);
#else
	return false;
#endif
}

static const struct of_device_id sdhci_of_match[];
static int __devinit sdhci_of_probe(struct platform_device *ofdev)
{
	const struct of_device_id *match;
	struct device_node *np = ofdev->dev.of_node;
	struct sdhci_of_data *sdhci_of_data;
	struct sdhci_host *host;
	struct sdhci_of_host *of_host;
	const __be32 *clk;
	int size;
	int ret;

	match = of_match_device(sdhci_of_match, &ofdev->dev);
	if (!match)
		return -EINVAL;
	sdhci_of_data = match->data;

	if (!of_device_is_available(np))
		return -ENODEV;

	host = sdhci_alloc_host(&ofdev->dev, sizeof(*of_host));
	if (IS_ERR(host))
		return -ENOMEM;

	of_host = sdhci_priv(host);
	dev_set_drvdata(&ofdev->dev, host);

	host->ioaddr = of_iomap(np, 0);
	if (!host->ioaddr) {
		ret = -ENOMEM;
		goto err_addr_map;
	}

	host->irq = irq_of_parse_and_map(np, 0);
	if (!host->irq) {
		ret = -EINVAL;
		goto err_no_irq;
	}

	host->hw_name = dev_name(&ofdev->dev);
	if (sdhci_of_data) {
		host->quirks = sdhci_of_data->quirks;
		host->ops = &sdhci_of_data->ops;
	}

	if (of_get_property(np, "sdhci,auto-cmd12", NULL))
		host->quirks |= SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12;


	if (of_get_property(np, "sdhci,1-bit-only", NULL))
		host->quirks |= SDHCI_QUIRK_FORCE_1_BIT_DATA;

	if (sdhci_of_wp_inverted(np))
		host->quirks |= SDHCI_QUIRK_INVERTED_WRITE_PROTECT;

	clk = of_get_property(np, "clock-frequency", &size);
	if (clk && size == sizeof(*clk) && *clk)
		of_host->clock = be32_to_cpup(clk);

	ret = sdhci_add_host(host);
	if (ret)
		goto err_add_host;

	return 0;

err_add_host:
	irq_dispose_mapping(host->irq);
err_no_irq:
	iounmap(host->ioaddr);
err_addr_map:
	sdhci_free_host(host);
	return ret;
}

static int __devexit sdhci_of_remove(struct platform_device *ofdev)
{
	struct sdhci_host *host = dev_get_drvdata(&ofdev->dev);

	sdhci_remove_host(host, 0);
	sdhci_free_host(host);
	irq_dispose_mapping(host->irq);
	iounmap(host->ioaddr);
	return 0;
}

static const struct of_device_id sdhci_of_match[] = {
#ifdef CONFIG_MMC_SDHCI_OF_ESDHC
	{ .compatible = "fsl,mpc8379-esdhc", .data = &sdhci_esdhc, },
	{ .compatible = "fsl,mpc8536-esdhc", .data = &sdhci_esdhc, },
	{ .compatible = "fsl,esdhc", .data = &sdhci_esdhc, },
#endif
#ifdef CONFIG_MMC_SDHCI_OF_HLWD
	{ .compatible = "nintendo,hollywood-sdhci", .data = &sdhci_hlwd, },
#endif
	{ .compatible = "generic-sdhci", },
	{},
};
MODULE_DEVICE_TABLE(of, sdhci_of_match);

static struct platform_driver sdhci_of_driver = {
	.driver = {
		.name = "sdhci-of",
		.owner = THIS_MODULE,
		.of_match_table = sdhci_of_match,
	},
	.probe = sdhci_of_probe,
	.remove = __devexit_p(sdhci_of_remove),
	.suspend = sdhci_of_suspend,
	.resume	= sdhci_of_resume,
};

static int __init sdhci_of_init(void)
{
	return platform_driver_register(&sdhci_of_driver);
}
module_init(sdhci_of_init);

static void __exit sdhci_of_exit(void)
{
	platform_driver_unregister(&sdhci_of_driver);
}
module_exit(sdhci_of_exit);

MODULE_DESCRIPTION("Secure Digital Host Controller Interface OF driver");
MODULE_AUTHOR("Xiaobo Xie <X.Xie@freescale.com>, "
	      "Anton Vorontsov <avorontsov@ru.mvista.com>");
MODULE_LICENSE("GPL");
