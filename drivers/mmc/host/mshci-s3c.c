/*
* linux/drivers/mmc/host/mshci-s3c.c
* Mobile Storage Host Controller Interface driver
*
* Copyright (c) 2011 Samsung Electronics Co., Ltd.
*		http://www.samsung.com
*
* Based on linux/drivers/mmc/host/sdhci-s3c.c
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or (at
* your option) any later version.
*
*/

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/gpio.h>

#include <linux/mmc/host.h>

#include <plat/gpio-cfg.h>
#include <plat/mshci.h>
#include <plat/clock.h>
#include <plat/cpu.h>

#include "mshci.h"

#ifdef CONFIG_MMC_MSHCI_S3C_DMA_MAP
int mshci_s3c_dma_map_sg(struct mshci_host *host, struct device *dev,
		struct scatterlist *sg, int nents,
		enum dma_data_direction dir, int flush_type);

void mshci_s3c_dma_unmap_sg(struct mshci_host *host, struct device *dev,
		struct scatterlist *sg, int nents,
		enum dma_data_direction dir, int flush_type);
#endif

#define MAX_BUS_CLK	(1)

/**
 * struct mshci_s3c - S3C MSHCI instance
 * @host: The MSHCI host created
 * @pdev: The platform device we where created from.
 * @ioarea: The resource created when we claimed the IO area.
 * @pdata: The platform data for this controller.
 * @cur_clk: The index of the current bus clock.
 * @clk_io: The clock for the internal bus interface.
 * @clk_bus: The clocks that are available for the SD/MMC bus clock.
 */
struct mshci_s3c {
	struct mshci_host	*host;
	struct platform_device	*pdev;
	struct resource		*ioarea;
	struct s3c_mshci_platdata *pdata;
	unsigned int		cur_clk;
	int			ext_cd_irq;
	int			ext_cd_gpio;

	struct clk		*clk_io;
	struct clk		*clk_bus[MAX_BUS_CLK];
};

static inline struct mshci_s3c *to_s3c(struct mshci_host *host)
{
	return mshci_priv(host);
}

/**
 * mshci_s3c_get_max_clk - callback to get maximum clock frequency.
 * @host: The MSHCI host instance.
 *
 * Callback to return the maximum clock rate acheivable by the controller.
*/
static unsigned int mshci_s3c_get_max_clk(struct mshci_host *host)
{
	struct mshci_s3c *ourhost = to_s3c(host);
	struct clk *busclk;
	unsigned int rate, max;
	int clk;

	for (max = 0, clk = 0; clk < MAX_BUS_CLK; clk++) {
		busclk = ourhost->clk_bus[clk];
		if (!busclk)
			continue;

		rate = clk_get_rate(busclk);
		/* It should be checked later ############# */
		if (rate > max) {
			if ((soc_is_exynos4412() || soc_is_exynos4212()) &&
				(samsung_rev() >= EXYNOS4412_REV_1_0))
				max = rate >> 2;
			else
				max = rate >> 1;
		}
	}

	/* max clock can be change after changing clock source. */
	host->mmc->f_max = max;
	return max;
}

/**
 * mshci_s3c_consider_clock - consider one the bus clocks for current setting
 * @ourhost: Our MSHCI instance.
 * @src: The source clock index.
 * @wanted: The clock frequency wanted.
 */
static unsigned int mshci_s3c_consider_clock(struct mshci_s3c *ourhost,
					     unsigned int src,
					     unsigned int wanted)
{
	unsigned long rate;
	struct clk *clksrc = ourhost->clk_bus[src];
	int div;

	if (!clksrc)
		return UINT_MAX;

	rate = clk_get_rate(clksrc);

	for (div = 1; div < 256; div *= 2) {
		if ((rate / div) <= wanted)
			break;
	}

	dev_dbg(&ourhost->pdev->dev, "clk %d: rate %ld, want %d, got %ld\n",
		src, rate, wanted, rate / div);

	return wanted - (rate / div);
}

/**
 * mshci_s3c_set_clock - callback on clock change
 * @host: The MSHCI host being changed
 * @clock: The clock rate being requested.
 *
 * When the card's clock is going to be changed, look at the new frequency
 * and find the best clock source to go with it.
*/
static void mshci_s3c_set_clock(struct mshci_host *host, unsigned int clock)
{
	struct mshci_s3c *ourhost = to_s3c(host);
	unsigned int best = UINT_MAX;
	unsigned int delta;
	int best_src = 0;
	int src;

	/* don't bother if the clock is going off. */
	if (clock == 0)
		return;

	for (src = 0; src < MAX_BUS_CLK; src++) {
		delta = mshci_s3c_consider_clock(ourhost, src, clock);
		if (delta < best) {
			best = delta;
			best_src = src;
		}
	}

	dev_dbg(&ourhost->pdev->dev,
		"selected source %d, clock %d, delta %d\n",
		 best_src, clock, best);

	/* select the new clock source */

	if (ourhost->cur_clk != best_src) {
		struct clk *clk = ourhost->clk_bus[best_src];

		ourhost->cur_clk = best_src;
		host->max_clk = clk_get_rate(clk);
	}

	/* reconfigure the hardware for new clock rate */

	{
		struct mmc_ios ios;

		ios.clock = clock;

		if (ourhost->pdata->cfg_card)
			(ourhost->pdata->cfg_card)(ourhost->pdev, host->ioaddr,
						   &ios, NULL);
	}
}

/**
 * mshci_s3c_get_ro - callback for get_ro
 * @host: The MSHCI host being changed
 *
 * If the WP pin is connected with GPIO, can get the value which indicates
 * the card is locked or not.
*/
static int mshci_s3c_get_ro(struct mmc_host *mmc)
{
	struct mshci_s3c *ourhost = to_s3c(mmc_priv(mmc));

	return gpio_get_value(ourhost->pdata->wp_gpio);
}

/**
 * mshci_s3c_cfg_wp - configure GPIO for WP pin
 * @gpio_num: GPIO number which connected with WP line from SD/MMC slot
 *
 * Configure GPIO for using WP line
 */
static void mshci_s3c_cfg_wp(unsigned int gpio_num)
{
	s3c_gpio_cfgpin(gpio_num, S3C_GPIO_INPUT);
	s3c_gpio_setpull(gpio_num, S3C_GPIO_PULL_UP);
}

static void mshci_s3c_set_ios(struct mshci_host *host,
			      struct mmc_ios *ios)
{
	struct mshci_s3c *ourhost = to_s3c(host);
	struct s3c_mshci_platdata *pdata = ourhost->pdata;
	int width;

	if (ios->power_mode != MMC_POWER_OFF) {
		switch (ios->bus_width) {
		case MMC_BUS_WIDTH_8:
			width = 8;
			break;
		case MMC_BUS_WIDTH_4:
			width = 4;
			break;
		case MMC_BUS_WIDTH_1:
			width = 1;
			break;
		default:
			BUG();
		}

		if (pdata->cfg_gpio)
			pdata->cfg_gpio(ourhost->pdev, width);
	}

	if (pdata->cfg_card)
		pdata->cfg_card(ourhost->pdev, host->ioaddr,
				ios, host->mmc->card);

	if (pdata->cfg_ddr) {
		if (ios->timing == MMC_TIMING_UHS_DDR50)
			pdata->cfg_ddr(ourhost->pdev, 1);
		else
			pdata->cfg_ddr(ourhost->pdev, 0);
	}
	/* after change DDR/SDR, max_clk has been changed.
	   You should re-calc the max_clk */
	host->max_clk = mshci_s3c_get_max_clk(host);
}

/**
 * mshci_s3c_init_card - Reset eMMC device
 *
 * init eMMC_card.
 */

static void mshci_s3c_init_card(struct mshci_host *host)
{
	struct mshci_s3c *ourhost = to_s3c(host);
	struct s3c_mshci_platdata *pdata = ourhost->pdata;

	if (pdata->init_card)
		pdata->init_card(ourhost->pdev);
}

static int mshci_s3c_get_fifo_depth(struct mshci_host *host)
{
	struct mshci_s3c *ourhost = to_s3c(host);
	struct s3c_mshci_platdata *pdata = ourhost->pdata;

	return pdata->fifo_depth;
}


static struct mshci_ops mshci_s3c_ops = {
	.get_max_clock		= mshci_s3c_get_max_clk,
	.set_clock		= mshci_s3c_set_clock,
	.set_ios		= mshci_s3c_set_ios,
	.init_card		= mshci_s3c_init_card,
#ifdef CONFIG_MMC_MSHCI_S3C_DMA_MAP
	.dma_map_sg		= mshci_s3c_dma_map_sg,
	.dma_unmap_sg		= mshci_s3c_dma_unmap_sg,
#endif
	.get_fifo_depth		= mshci_s3c_get_fifo_depth,
};

static void mshci_s3c_notify_change(struct platform_device *dev, int state)
{
	struct mshci_host *host;
	unsigned long flags;

	local_irq_save(flags);
	host = platform_get_drvdata(dev);
	if (host) {
		if (state) {
			dev_dbg(&dev->dev, "card inserted.\n");
			host->flags &= ~MSHCI_DEVICE_DEAD;
			tasklet_schedule(&host->card_tasklet);
		} else {
			dev_dbg(&dev->dev, "card removed.\n");
			host->flags |= MSHCI_DEVICE_DEAD;
			tasklet_schedule(&host->card_tasklet);
		}
	}
	local_irq_restore(flags);
}

static irqreturn_t mshci_s3c_gpio_card_detect_isr(int irq, void *dev_id)
{
	struct mshci_s3c *sc = dev_id;
	int status = gpio_get_value(sc->ext_cd_gpio);
	if (sc->pdata->ext_cd_gpio_invert)
		status = !status;
	mshci_s3c_notify_change(sc->pdev, status);
	return IRQ_HANDLED;
}


static int __devinit mshci_s3c_probe(struct platform_device *pdev)
{
	struct s3c_mshci_platdata *pdata = pdev->dev.platform_data;
	struct device *dev = &pdev->dev;
	struct mshci_host *host;
	struct mshci_s3c *sc;
	struct resource *res;
	int ret, irq, ptr, clks;

	if (!pdata) {
		dev_err(dev, "no device data specified\n");
		return -ENOENT;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "no irq specified\n");
		return irq;
	}
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "no memory specified\n");
		return -ENOENT;
	}
	host = mshci_alloc_host(dev, sizeof(struct mshci_s3c));
	if (IS_ERR(host)) {
		dev_err(dev, "mshci_alloc_host() failed\n");
		return PTR_ERR(host);
	}
	sc = mshci_priv(host);

	if (soc_is_exynos4210()) {
		host->data_addr = 0x0;
		host->hold_bit = 0;
	} else {
		host->data_addr = 0x100;
		host->hold_bit = CMD_USE_HOLD_REG;
	}

	sc->host = host;
	sc->pdev = pdev;
	sc->pdata = pdata;
	sc->ext_cd_gpio = -1;

	platform_set_drvdata(pdev, host);

	sc->clk_io = clk_get(dev, "dwmci");
	if (IS_ERR(sc->clk_io)) {
		dev_err(dev, "failed to get io clock\n");
		ret = PTR_ERR(sc->clk_io);
		goto err_io_clk;
	}

	/* enable the local io clock and keep it running for the moment. */
	clk_enable(sc->clk_io);

	for (clks = 0, ptr = 0; ptr < MAX_BUS_CLK; ptr++) {
		struct clk *clk;
		char *name = pdata->clocks[ptr];

		if (name == NULL)
			continue;
		clk = clk_get(dev, name);
		if (IS_ERR(clk)) {
			dev_err(dev, "failed to get clock %s\n", name);
			continue;
		}

#if defined(CONFIG_EXYNOS4_MSHC_VPLL_46MHZ) || \
	defined(CONFIG_EXYNOS4_MSHC_EPLL_45MHZ)
	if (!strcmp("sclk_dwmci", name)) {
		struct clk *parent_clk;

		parent_clk = clk_get_parent(clk);

		if (!parent_clk) {
			dev_err(dev, "failed to get parent clock %s\n"
			, (char *)(clk->name));
		} else {
			for ( ; ; ) {
				parent_clk = clk_get_parent(parent_clk);
				if (parent_clk) {
#ifdef CONFIG_EXYNOS4_MSHC_EPLL_45MHZ
					if (!strcmp("fout_epll", \
							parent_clk->name)) {
						clk_set_rate \
							(parent_clk, 180633600);
						pdata->cfg_ddr(pdev, 0);
#elif defined(CONFIG_EXYNOS4_MSHC_VPLL_46MHZ)
					if (!strcmp("fout_vpll", \
							parent_clk->name)) {
						clk_set_rate \
							(parent_clk, 370882812);
						pdata->cfg_ddr(pdev, 0);
#endif
						clk_enable(parent_clk);
						break;
					} else
						continue;
				} else {
					dev_err(dev, "failed to"
								"get parent"
								"clock %s\n"
								, clk->name);
					break;
				}
			}
		}
	}
#endif
		clks++;
		sc->clk_bus[ptr] = clk;
		clk_enable(clk);

		dev_info(dev, "clock source %d: %s (%ld Hz)\n",
			 ptr, name, clk_get_rate(clk));
	}

	if (clks == 0) {
		dev_err(dev, "failed to find any bus clocks\n");
		ret = -ENOENT;
		goto err_no_busclks;
	}

	sc->ioarea = request_mem_region(res->start, resource_size(res),
					mmc_hostname(host->mmc));
	if (!sc->ioarea) {
		dev_err(dev, "failed to reserve register area\n");
		ret = -ENXIO;
		goto err_req_regs;
	}

	host->ioaddr = ioremap_nocache(res->start, resource_size(res));
	if (!host->ioaddr) {
		dev_err(dev, "failed to map registers\n");
		ret = -ENXIO;
		goto err_req_regs;
	}

	/* Ensure we have minimal gpio selected CMD/CLK/Detect */
	if (pdata->cfg_gpio)
		pdata->cfg_gpio(pdev, pdata->max_width);
	else
		dev_err(dev, "cfg_gpio dose not exist.!\n");

	host->hw_name = "samsung-mshci";
	host->ops = &mshci_s3c_ops;
	host->quirks = 0;
	host->irq = irq;

	if (pdata->host_caps)
		host->mmc->caps = pdata->host_caps;
	else
		host->mmc->caps = 0;

        if (pdata->host_caps2)
                host->mmc->caps2 = pdata->host_caps2;
	else
                host->mmc->caps2 = 0;

	if (pdata->cd_type == S3C_MSHCI_CD_PERMANENT) {
		host->quirks |= MSHCI_QUIRK_BROKEN_PRESENT_BIT;
		host->mmc->caps |= MMC_CAP_NONREMOVABLE;
	}

	/* IF SD controller's WP pin donsn't connected with SD card and there
	 * is an allocated GPIO for getting WP data form SD card,
	 * use this quirk and send the GPIO number in pdata->wp_gpio. */
	if (pdata->has_wp_gpio && gpio_is_valid(pdata->wp_gpio)) {
		mshci_s3c_ops.get_ro = mshci_s3c_get_ro;
		host->quirks |= MSHCI_QUIRK_NO_WP_BIT;
		mshci_s3c_cfg_wp(pdata->wp_gpio);
	}

	ret = mshci_add_host(host);

	if (pdata->cd_type == S3C_MSHCI_CD_GPIO &&
		gpio_is_valid(pdata->ext_cd_gpio)) {

		ret = gpio_request(pdata->ext_cd_gpio, "MSHCI EXT CD");
		if (ret) {
			dev_err(&pdev->dev, "cannot request gpio for card detect\n");
			goto err_add_host;
		}

		sc->ext_cd_gpio = pdata->ext_cd_gpio;

		sc->ext_cd_irq = gpio_to_irq(pdata->ext_cd_gpio);
		if (sc->ext_cd_irq &&
			request_irq(sc->ext_cd_irq,
				mshci_s3c_gpio_card_detect_isr,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				dev_name(&pdev->dev), sc)) {
			dev_err(&pdev->dev, "cannot request irq for card detect\n");
			sc->ext_cd_irq = 0;
		}
		dev_dbg(&pdev->dev, "mshci detects a card insertion/removal"
							"by EINT\n");
	}

	if (ret) {
		dev_err(dev, "mshci_add_host() failed\n");
		goto err_add_host;
	}

	device_enable_async_suspend(dev);

	return 0;

 err_add_host:
	release_resource(sc->ioarea);
	kfree(sc->ioarea);

 err_req_regs:
	for (ptr = 0; ptr < MAX_BUS_CLK; ptr++) {
		clk_disable(sc->clk_bus[ptr]);
		clk_put(sc->clk_bus[ptr]);
	}

 err_no_busclks:
	clk_disable(sc->clk_io);
	clk_put(sc->clk_io);

 err_io_clk:
	mshci_free_host(host);
	return ret;
}

static int __devexit mshci_s3c_remove(struct platform_device *pdev)
{
	return 0;
}

#ifdef CONFIG_PM

static int mshci_s3c_suspend(struct platform_device *dev, pm_message_t pm)
{
	struct mshci_host *host = platform_get_drvdata(dev);
	struct s3c_mshci_platdata *pdata = dev->dev.platform_data;

	mshci_suspend_host(host, pm);

	if (pdata->set_power)
		pdata->set_power(dev, 0);

	return 0;
}

static int mshci_s3c_resume(struct platform_device *dev)
{
	struct mshci_host *host = platform_get_drvdata(dev);
	struct s3c_mshci_platdata *pdata = dev->dev.platform_data;

	if (pdata->set_power)
		pdata->set_power(dev, 1);

	mshci_resume_host(host);
	return 0;
}

static void mshci_s3c_shutdown(struct platform_device *dev, pm_message_t pm)
{
	struct mshci_host *host = platform_get_drvdata(dev);
	struct s3c_mshci_platdata *pdata = dev->dev.platform_data;

	mshci_suspend_host(host, pm);

	if (pdata->shutdown)
		pdata->shutdown();
}


#else
#define mshci_s3c_suspend NULL
#define mshci_s3c_resume NULL
#endif

static struct platform_driver mshci_s3c_driver = {
	.probe		= mshci_s3c_probe,
	.remove		= __devexit_p(mshci_s3c_remove),
	.suspend	= mshci_s3c_suspend,
	.resume	        = mshci_s3c_resume,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "dw_mmc",
	},
};

static int __init mshci_s3c_init(void)
{
	return platform_driver_register(&mshci_s3c_driver);
}

static void __exit mshci_s3c_exit(void)
{
	platform_driver_unregister(&mshci_s3c_driver);
}

module_init(mshci_s3c_init);
module_exit(mshci_s3c_exit);

MODULE_DESCRIPTION("Samsung MSHCI (HSMMC) glue");
MODULE_AUTHOR("Hyunsung Jang, <hs79.jang@samsung.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:dw_mmc");
