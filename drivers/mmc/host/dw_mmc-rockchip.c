/*
 * Rockchip Specific Extensions for Synopsys DW Multimedia Card Interface driver
 *
 * Copyright (C) 2014, Rockchip Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/rk_mmc.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <linux/rockchip/cpu.h>
#include <linux/rockchip/cru.h>
#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include "rk_sdmmc.h"
#include "dw_mmc-pltfm.h"
#include "../../clk/rockchip/clk-ops.h"

#include "rk_sdmmc_dbg.h"

/* Rockchip implementation specific driver private data */
struct dw_mci_rockchip_priv_data {
	enum dw_mci_rockchip_type		ctrl_type;
	u8				ciu_div;
	u32				sdr_timing;
	u32				ddr_timing;
	u32				cur_speed;
};

static struct dw_mci_rockchip_compatible {
	char				*compatible;
	enum dw_mci_rockchip_type		ctrl_type;
} rockchip_compat[] = {
	{
		.compatible	= "rockchip,rk31xx-sdmmc",
		.ctrl_type	= DW_MCI_TYPE_RK3188,
	},
	{
		.compatible	= "rockchip,rk32xx-sdmmc",
		.ctrl_type	= DW_MCI_TYPE_RK3288,
	},
	{
		.compatible	= "rockchip,rk3036-sdmmc",
		.ctrl_type	= DW_MCI_TYPE_RK3036,
	},
	{
		.compatible	= "rockchip,rk312x-sdmmc",
		.ctrl_type	= DW_MCI_TYPE_RK312X,
	},
	{
		.compatible     = "rockchip,rk3368-sdmmc",
		.ctrl_type      = DW_MCI_TYPE_RK3368,
	},
};

#define syscon_find(np, property) \
syscon_regmap_lookup_by_phandle(np, property)

static int dw_mci_rockchip_priv_init(struct dw_mci *host)
{
	struct dw_mci_rockchip_priv_data *priv;
	int idx;

	priv = devm_kzalloc(host->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(host->dev, "mem alloc failed for private data\n");
		return -ENOMEM;
	}

	for (idx = 0; idx < ARRAY_SIZE(rockchip_compat); idx++) {
		if (of_device_is_compatible(host->dev->of_node,
					    rockchip_compat[idx].compatible)) {
			priv->ctrl_type = rockchip_compat[idx].ctrl_type;
			host->cid = priv->ctrl_type;
			if (priv->ctrl_type == DW_MCI_TYPE_RK3368) {
				host->grf = syscon_regmap_lookup_by_phandle(
						host->dev->of_node, "rockchip,grf");
				if (IS_ERR(host->grf)) {
					pr_err("No rockchip,grf phandle specified");
					return PTR_ERR(host->grf);
				}
				host->cru = syscon_regmap_lookup_by_phandle(
					host->dev->of_node, "rockchip,cru");
				if (IS_ERR(host->cru)) {
					pr_err("No rockchip,cru phandle specified");
					return PTR_ERR(host->cru);
				}
			}
		}
	}

	host->priv = priv;
	return 0;
}

static int dw_mci_rockchip_setup_clock(struct dw_mci *host)
{
	struct dw_mci_rockchip_priv_data *priv = host->priv;

	if ((priv->ctrl_type == DW_MCI_TYPE_RK3288) ||
	    (priv->ctrl_type == DW_MCI_TYPE_RK3036) ||
	    (priv->ctrl_type == DW_MCI_TYPE_RK312X) ||
	    (priv->ctrl_type == DW_MCI_TYPE_RK3368))
		host->bus_hz /= (priv->ciu_div + 1);

	return 0;
}

static void dw_mci_rockchip_prepare_command(struct dw_mci *host, u32 *cmdr)
{
}

static void dw_mci_rockchip_set_ios(struct dw_mci *host, struct mmc_ios *ios)
{
}

static int dw_mci_rockchip_parse_dt(struct dw_mci *host)
{
	return 0;
}

#define ROCKCHIP_MMC_DELAY_SEL BIT(10)
#define ROCKCHIP_MMC_DEGREE_MASK 0x3
#define ROCKCHIP_MMC_DELAYNUM_OFFSET 2
#define ROCKCHIP_MMC_DELAYNUM_MASK (0xff << ROCKCHIP_MMC_DELAYNUM_OFFSET)
#define PSECS_PER_SEC 1000000000000LL

/*
 * Each fine delay is between 40ps-80ps. Assume each fine delay is 60ps to
 * simplify calculations. So 45degs could be anywhere between 33deg and 66deg.
 */
#define ROCKCHIP_MMC_DELAY_ELEMENT_PSEC 60

#define NUM_PHASES			32
#define TUNING_ITERATION_TO_PHASE(i)	(DIV_ROUND_UP((i) * 360, NUM_PHASES))

static int rockchip_mmc_set_phase(int degrees, struct dw_mci *host)
{
	unsigned long rate = clk_get_rate(host->clk_mmc)/2; /* 150M */
	u8 nineties, remainder;
	u8 delay_num;
	u32 raw_value;
	u64 delay;

	degrees++;
	degrees -= ((degrees) * 10 % (360/NUM_PHASES*10)) / 10;
	nineties = degrees / 90;
	remainder = (degrees % 90) / (360/NUM_PHASES);

	delay = PSECS_PER_SEC;
	do_div(delay, rate);
	do_div(delay, NUM_PHASES);
	do_div(delay, ROCKCHIP_MMC_DELAY_ELEMENT_PSEC);

	delay *= remainder;
	delay_num = (u8) min(delay, 255ULL);

	raw_value = delay_num ? ROCKCHIP_MMC_DELAY_SEL : 0;
	raw_value |= delay_num << ROCKCHIP_MMC_DELAYNUM_OFFSET;
	raw_value |= nineties;

	cru_writel(0x00010001, host->tune_regsbase);
	cru_writel(((0x07ff << 16)|(raw_value)), host->tune_regsbase + 4);
	cru_writel(0x00010000, host->tune_regsbase);

	return 0;
}

static int dw_mci_tuning_test(struct dw_mci_slot *slot, u32 opcode,
			      struct dw_mci_tuning_data *tuning_data,
			      u8 *blk_test)
{
	struct dw_mci *host = slot->host;
	struct mmc_host *mmc = slot->mmc;
	const u8 *blk_pattern = tuning_data->blk_pattern;
	unsigned int blksz = tuning_data->blksz;
	struct mmc_request mrq = { NULL };
	struct mmc_command cmd = {0};
	struct mmc_command stop = {0};
	struct mmc_data data = {0};
	struct scatterlist sg;

	memset(blk_test, 0, blksz);

	cmd.opcode = opcode;
	cmd.arg = 0;
	cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;

	stop.opcode = MMC_STOP_TRANSMISSION;
	stop.arg = 0;
	stop.flags = MMC_RSP_R1B | MMC_CMD_AC;

	data.blksz = blksz;
	data.blocks = 1;
	data.flags = MMC_DATA_READ;
	data.sg = &sg;
	data.sg_len = 1;

	sg_init_one(&sg, blk_test, blksz);
	mrq.cmd = &cmd;
	mrq.stop = &stop;
	mrq.data = &data;
	host->mrq = &mrq;
	mci_writel(host, TMOUT, ~0);

	mmc_wait_for_req(mmc, &mrq);
	if (!cmd.error && !data.error) {
		if (!memcmp(blk_pattern, blk_test, blksz))
			return 0;

		return -EIO;
	} else {
		dev_err(host->dev,
			"Tuning error: cmd.error:%d, data.error:%d\n",
			cmd.error, data.error);
		if (cmd.error)
			return cmd.error;
		else
			return data.error;
	}
}

static int
dw_mci_rockchip_execute_tuning(struct dw_mci_slot *slot, u32 opcode,
			       struct dw_mci_tuning_data *tuning_data)
{
	struct dw_mci *host = slot->host;
	struct device *dev = host->dev;
	struct device_node *np = dev->of_node;
	unsigned int blksz = tuning_data->blksz;
	u8 *blk_test;
	int ret = 0;
	int i;
	bool v, prev_v = 0, first_v;
	struct range_t {
		int start;
		int end; /* inclusive */
	};
	struct range_t *ranges;
	unsigned int range_count = 0;
	int longest_range_len = -1;
	int longest_range = -1;
	int middle_phase;

	if (!of_property_read_u32(np, "tune_regsbase", &host->tune_regsbase)) {
		pr_info("[%s] tuning regsbase addr 0x%03x.\n",
			mmc_hostname(host->mmc), host->tune_regsbase);
	} else {
		pr_err("[%s] tuning regsbase addr is missing!\n",
		       mmc_hostname(host->mmc));
		return -1;
	}

	blk_test = kmalloc(blksz, GFP_KERNEL);
	if (!blk_test)
		return -ENOMEM;


	ranges = kmalloc_array(NUM_PHASES / 2 + 1, sizeof(*ranges), GFP_KERNEL);
	if (!ranges) {
		ret = -ENOMEM;
		goto free_blk_test;
	}

	/* Try each phase and extract good ranges */
	for (i = 0; i < NUM_PHASES - 6; i++) {
		rockchip_mmc_set_phase(TUNING_ITERATION_TO_PHASE(i), host);
		v = !dw_mci_tuning_test(slot, opcode, tuning_data, blk_test);

		if ((!prev_v) && v) {
			range_count++;
			ranges[range_count-1].start = i;
		}
		if (v)
			ranges[range_count-1].end = i;

		if (i == 0)
			first_v = v;

		prev_v = v;
	}

	if (range_count == 0) {
		dev_err(host->dev, "All phases bad!");
		ret = -EIO;
		goto free;
	}

	/* wrap around case, merge the end points
	if ((range_count > 1) && first_v && v) {
		ranges[0].start = ranges[range_count-1].start;
		range_count--;
	}
	*/

	if ((ranges[0].start == 0) && (ranges[0].end == NUM_PHASES - 1)) {
		rockchip_mmc_set_phase(0, host);
		dev_err(host->dev,
			"All phases work, using default phase %d.", 0);
		goto free;
	}

	/* Find the longest range */
	for (i = 0; i < range_count; i++) {
		int len = (ranges[i].end - ranges[i].start + 1);

		if (len < 0)
			len += NUM_PHASES;

		if (longest_range_len < len) {
			longest_range_len = len;
			longest_range = i;
		}

		dev_err(host->dev, "Good phase range %d-%d (%d len)\n",
			TUNING_ITERATION_TO_PHASE(ranges[i].start),
			TUNING_ITERATION_TO_PHASE(ranges[i].end),
			len
		);
	}

	dev_err(host->dev, "Best phase range %d-%d (%d len)\n",
		TUNING_ITERATION_TO_PHASE(ranges[longest_range].start),
		TUNING_ITERATION_TO_PHASE(ranges[longest_range].end),
		longest_range_len
	);

	middle_phase = ranges[longest_range].start + longest_range_len / 2;
	middle_phase %= NUM_PHASES;
	dev_err(host->dev, "Successfully tuned phase to %d\n",
		TUNING_ITERATION_TO_PHASE(middle_phase));

	rockchip_mmc_set_phase(TUNING_ITERATION_TO_PHASE(middle_phase), host);

free:
	kfree(ranges);
free_blk_test:
	kfree(blk_test);
	return ret;
}

/* Common capabilities of RK32XX SoC */
static unsigned long rockchip_dwmmc_caps[4] = {
	MMC_CAP_CMD23,
	MMC_CAP_CMD23,
	MMC_CAP_CMD23,
	MMC_CAP_CMD23,
};

unsigned int  rockchip_dwmmc_hold_reg[4] = {1, 0, 0, 0};

static const struct dw_mci_drv_data rockchip_drv_data = {
	.caps			= rockchip_dwmmc_caps,
	.hold_reg_flag  = rockchip_dwmmc_hold_reg,
	.init			= dw_mci_rockchip_priv_init,
	.setup_clock		= dw_mci_rockchip_setup_clock,
	.prepare_command	= dw_mci_rockchip_prepare_command,
	.set_ios		= dw_mci_rockchip_set_ios,
	.parse_dt		= dw_mci_rockchip_parse_dt,
	.execute_tuning		= dw_mci_rockchip_execute_tuning,
};

static const struct of_device_id dw_mci_rockchip_match[] = {
	{ .compatible = "rockchip,rk_mmc",
			.data = &rockchip_drv_data, },
	{ /* Sentinel */},
};
MODULE_DEVICE_TABLE(of, dw_mci_rockchip_match);

static int dw_mci_rockchip_probe(struct platform_device *pdev)
{
	const struct dw_mci_drv_data *drv_data;
	const struct of_device_id *match;

	match = of_match_node(dw_mci_rockchip_match, pdev->dev.of_node);
	drv_data = match->data;
	return dw_mci_pltfm_register(pdev, drv_data);
}

static struct platform_driver dw_mci_rockchip_pltfm_driver = {
	.probe		= dw_mci_rockchip_probe,
	.remove		= __exit_p(dw_mci_pltfm_remove),
	.driver		= {
		.name		= "dwmmc_rockchip",
		.of_match_table	= dw_mci_rockchip_match,
		.pm		= &dw_mci_pltfm_pmops,
	},
};

module_platform_driver(dw_mci_rockchip_pltfm_driver);

MODULE_DESCRIPTION("Rockchip Specific DW-SDMMC Driver Extension");
MODULE_AUTHOR("Shawn Lin <lintao@rock-chips.com>");
MODULE_AUTHOR("Bangwang Xie <xbw@rock-chips.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:dwmmc-rockchip");
