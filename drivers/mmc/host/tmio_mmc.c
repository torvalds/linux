// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the MMC / SD / SDIO cell found in:
 *
 * TC6393XB TC6391XB TC6387XB T7L66XB ASIC3
 *
 * Copyright (C) 2017 Renesas Electronics Corporation
 * Copyright (C) 2017 Horms Solutions, Simon Horman
 * Copyright (C) 2007 Ian Molton
 * Copyright (C) 2004 Ian Molton
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/mfd/core.h>
#include <linux/mfd/tmio.h>
#include <linux/mmc/host.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/scatterlist.h>

#include "tmio_mmc.h"

/* Registers specific to this variant */
#define CTL_SDIO_REGS		0x100
#define CTL_CLK_AND_WAIT_CTL	0x138
#define CTL_RESET_SDIO		0x1e0

static void tmio_mmc_clk_start(struct tmio_mmc_host *host)
{
	sd_ctrl_write16(host, CTL_SD_CARD_CLK_CTL, CLK_CTL_SCLKEN |
		sd_ctrl_read16(host, CTL_SD_CARD_CLK_CTL));

	usleep_range(10000, 11000);
	sd_ctrl_write16(host, CTL_CLK_AND_WAIT_CTL, 0x0100);
	usleep_range(10000, 11000);
}

static void tmio_mmc_clk_stop(struct tmio_mmc_host *host)
{
	sd_ctrl_write16(host, CTL_CLK_AND_WAIT_CTL, 0x0000);
	usleep_range(10000, 11000);

	sd_ctrl_write16(host, CTL_SD_CARD_CLK_CTL, ~CLK_CTL_SCLKEN &
		sd_ctrl_read16(host, CTL_SD_CARD_CLK_CTL));

	usleep_range(10000, 11000);
}

static void tmio_mmc_set_clock(struct tmio_mmc_host *host,
			       unsigned int new_clock)
{
	unsigned int divisor;
	u32 clk = 0;
	int clk_sel;

	if (new_clock == 0) {
		tmio_mmc_clk_stop(host);
		return;
	}

	divisor = host->pdata->hclk / new_clock;

	/* bit7 set: 1/512, ... bit0 set: 1/4, all bits clear: 1/2 */
	clk_sel = (divisor <= 1);
	clk = clk_sel ? 0 : (roundup_pow_of_two(divisor) >> 2);

	host->pdata->set_clk_div(host->pdev, clk_sel);

	sd_ctrl_write16(host, CTL_SD_CARD_CLK_CTL, ~CLK_CTL_SCLKEN &
			sd_ctrl_read16(host, CTL_SD_CARD_CLK_CTL));
	sd_ctrl_write16(host, CTL_SD_CARD_CLK_CTL, clk & CLK_CTL_DIV_MASK);
	usleep_range(10000, 11000);

	tmio_mmc_clk_start(host);
}

static void tmio_mmc_reset(struct tmio_mmc_host *host)
{
	sd_ctrl_write16(host, CTL_RESET_SDIO, 0x0000);
	usleep_range(10000, 11000);
	sd_ctrl_write16(host, CTL_RESET_SDIO, 0x0001);
	usleep_range(10000, 11000);
}

#ifdef CONFIG_PM_SLEEP
static int tmio_mmc_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	const struct mfd_cell *cell = mfd_get_cell(pdev);
	int ret;

	ret = pm_runtime_force_suspend(dev);

	/* Tell MFD core it can disable us now.*/
	if (!ret && cell->disable)
		cell->disable(pdev);

	return ret;
}

static int tmio_mmc_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	const struct mfd_cell *cell = mfd_get_cell(pdev);
	int ret = 0;

	/* Tell the MFD core we are ready to be enabled */
	if (cell->resume)
		ret = cell->resume(pdev);

	if (!ret)
		ret = pm_runtime_force_resume(dev);

	return ret;
}
#endif

static int tmio_mmc_probe(struct platform_device *pdev)
{
	const struct mfd_cell *cell = mfd_get_cell(pdev);
	struct tmio_mmc_data *pdata;
	struct tmio_mmc_host *host;
	struct resource *res;
	int ret = -EINVAL, irq;

	if (pdev->num_resources != 2)
		goto out;

	pdata = pdev->dev.platform_data;
	if (!pdata || !pdata->hclk)
		goto out;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto out;
	}

	/* Tell the MFD core we are ready to be enabled */
	if (cell->enable) {
		ret = cell->enable(pdev);
		if (ret)
			goto out;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		ret = -EINVAL;
		goto cell_disable;
	}

	host = tmio_mmc_host_alloc(pdev, pdata);
	if (IS_ERR(host)) {
		ret = PTR_ERR(host);
		goto cell_disable;
	}

	/* SD control register space size is 0x200, 0x400 for bus_shift=1 */
	host->bus_shift = resource_size(res) >> 10;
	host->set_clock = tmio_mmc_set_clock;
	host->reset = tmio_mmc_reset;

	host->mmc->f_max = pdata->hclk;
	host->mmc->f_min = pdata->hclk / 512;

	ret = tmio_mmc_host_probe(host);
	if (ret)
		goto host_free;

	ret = devm_request_irq(&pdev->dev, irq, tmio_mmc_irq,
			       IRQF_TRIGGER_FALLING,
			       dev_name(&pdev->dev), host);
	if (ret)
		goto host_remove;

	pr_info("%s at 0x%08lx irq %d\n", mmc_hostname(host->mmc),
		(unsigned long)host->ctl, irq);

	return 0;

host_remove:
	tmio_mmc_host_remove(host);
host_free:
	tmio_mmc_host_free(host);
cell_disable:
	if (cell->disable)
		cell->disable(pdev);
out:
	return ret;
}

static int tmio_mmc_remove(struct platform_device *pdev)
{
	const struct mfd_cell *cell = mfd_get_cell(pdev);
	struct tmio_mmc_host *host = platform_get_drvdata(pdev);

	tmio_mmc_host_remove(host);
	if (cell->disable)
		cell->disable(pdev);

	return 0;
}

/* ------------------- device registration ----------------------- */

static const struct dev_pm_ops tmio_mmc_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(tmio_mmc_suspend, tmio_mmc_resume)
	SET_RUNTIME_PM_OPS(tmio_mmc_host_runtime_suspend,
			   tmio_mmc_host_runtime_resume, NULL)
};

static struct platform_driver tmio_mmc_driver = {
	.driver = {
		.name = "tmio-mmc",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.pm = &tmio_mmc_dev_pm_ops,
	},
	.probe = tmio_mmc_probe,
	.remove = tmio_mmc_remove,
};

module_platform_driver(tmio_mmc_driver);

MODULE_DESCRIPTION("Toshiba TMIO SD/MMC driver");
MODULE_AUTHOR("Ian Molton <spyro@f2s.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:tmio-mmc");
