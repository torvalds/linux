/*
 * Copyright (c) 2015, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/delay.h>

#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <linux/cpu.h>
#include <dt-bindings/clock/ddr.h>
#include <linux/rockchip/common.h>
#include <linux/rockchip/cpu.h>
#include <linux/rockchip/cru.h>
#include <linux/rockchip/dvfs.h>
#include <linux/rockchip/grf.h>
#include <linux/rockchip/iomap.h>
#include <linux/rockchip/pmu.h>
#include <linux/rk_fb.h>
#include <linux/scpi_protocol.h>

#define GRF_DDRC0_CON0    0x600
#define GRF_SOC_STATUS5  0x494
#define DDR_PCTL_TOGCNT_1U  0xc0

enum ddr_bandwidth_id {
	ddrbw_wr_num = 0,
	ddrbw_rd_num,
	ddrbw_act_num,
	ddrbw_time_num,
	ddrbw_eff,
	ddrbw_id_end
};

struct rockchip_ddr {
	struct regmap *ddrpctl_regs;
	struct regmap *msch_regs;
	struct regmap *grf_regs;
};

static struct rockchip_ddr *ddr_data = NULL;

static int _ddr_recalc_rate(void)
{
	int ddr_freq;

	regmap_read(ddr_data->ddrpctl_regs, DDR_PCTL_TOGCNT_1U,
		    &ddr_freq);
	ddr_freq = ddr_freq * 2 * 1000000;
	return ddr_freq;
}

static int _ddr_change_freq(u32 n_mhz)
{
	u32 ret;

	printk(KERN_DEBUG pr_fmt("In func %s,freq=%dMHz\n"), __func__, n_mhz);
	if (scpi_ddr_set_clk_rate(n_mhz))
		pr_info("set ddr freq timeout\n");
	ret = _ddr_recalc_rate() / 1000000;
	printk(KERN_DEBUG pr_fmt("Func %s out,freq=%dMHz\n"), __func__, ret);
	return ret;
}

static long _ddr_round_rate(u32 n_mhz)
{
	return (n_mhz / 12) * 12;
}

static void _ddr_set_auto_self_refresh(bool en)
{
	if (scpi_ddr_set_auto_self_refresh(en))
		printk(KERN_DEBUG pr_fmt("ddr set auto selfrefresh error\n"));
}

static void ddr_monitor_start(void)
{
	u32 i;

	/* cpum, gpu probe */
	for (i = 1; i < 3; i++) {
		regmap_write(ddr_data->msch_regs, 0x1000 + (0x400 * i) + 0x8,
			     0x8);
		regmap_write(ddr_data->msch_regs, 0x1000 + (0x400 * i) + 0xc,
			     0x1);
		regmap_write(ddr_data->msch_regs, 0x1000 + (0x400 * i) + 0x138,
			     0x6);
		regmap_write(ddr_data->msch_regs, 0x1000 + (0x400 * i) + 0x14c,
			     0x10);
		regmap_write(ddr_data->msch_regs, 0x1000 + (0x400 * i) + 0x160,
			     0x8);
		regmap_write(ddr_data->msch_regs, 0x1000 + (0x400 * i) + 0x174,
			     0x10);
	}
	/* video, vio0, vio1 probe */
	for (i = 0; i < 3; i++) {
		regmap_write(ddr_data->msch_regs, 0x2000 + (0x400 * i) + 0x8,
			     0x8);
		regmap_write(ddr_data->msch_regs, 0x2000 + (0x400 * i) + 0xc,
			     0x1);
		regmap_write(ddr_data->msch_regs, 0x2000 + (0x400 * i) + 0x138,
			     0x6);
		regmap_write(ddr_data->msch_regs, 0x2000 + (0x400 * i) + 0x14c,
			     0x10);
		regmap_write(ddr_data->msch_regs, 0x2000 + (0x400 * i) + 0x160,
			     0x8);
		regmap_write(ddr_data->msch_regs, 0x2000 + (0x400 * i) + 0x174,
			     0x10);
	}
	/* dfi eff start */
	regmap_write(ddr_data->grf_regs, GRF_DDRC0_CON0,
		     ((0x3 << 5) << 16) | 0x3 << 5);
	/*flash data */
	wmb();
	/* trigger statistic */
	for (i = 1; i < 3; i++)
		regmap_write(ddr_data->msch_regs, 0x1000 + (0x400 * i) + 0x28,
			     0x1);
	for (i = 0; i < 3; i++)
		regmap_write(ddr_data->msch_regs, 0x2000 + (0x400 * i) + 0x28,
			     0x1);
}

static void ddr_monitor_stop(void)
{
	/* dfi eff stop */
	regmap_write(ddr_data->grf_regs, GRF_DDRC0_CON0,
		     ((0x3 << 5) << 16) | 0x0 << 5);
}

static void _ddr_bandwidth_get(struct ddr_bw_info *ddr_bw_ch0,
			       struct ddr_bw_info *ddr_bw_ch1)
{
	u32 ddr_bw_val[2][ddrbw_id_end], ddr_freq, dfi_freq;
	u64 temp64;
	int i, j;
	u32 tmp32;

	if (!ddr_data)
		return;

	ddr_monitor_stop();
	/* read dfi eff */
	for (j = 0; j < 2; j++) {
		for (i = 0; i < ddrbw_eff; i++) {
			regmap_read(ddr_data->grf_regs,
				    GRF_SOC_STATUS5 + 4 * i + j * 16,
				    &ddr_bw_val[j][i]);
		}
	}
	if (!ddr_bw_val[0][ddrbw_time_num])
		goto end;
	if (ddr_bw_ch0) {
		regmap_read(ddr_data->ddrpctl_regs, DDR_PCTL_TOGCNT_1U,
			    &ddr_freq);
		ddr_freq *= 2;
		dfi_freq = ddr_freq / 2;
		/* dfi eff */
		temp64 = ((u64) ddr_bw_val[0][0] + ddr_bw_val[0][1]
			  + ddr_bw_val[1][0] + ddr_bw_val[1][1]) * 2 * 100;
		do_div(temp64, ddr_bw_val[0][ddrbw_time_num]);
		ddr_bw_val[0][ddrbw_eff] = temp64;
		ddr_bw_ch0->ddr_percent = temp64;
		ddr_bw_ch0->ddr_time =
		    ddr_bw_val[0][ddrbw_time_num] / (dfi_freq * 1000);
		/*unit:MB/s */
		ddr_bw_ch0->ddr_wr = (((u64)
				       (ddr_bw_val[0][ddrbw_wr_num] +
					ddr_bw_val[1][ddrbw_wr_num]) * 8 * 4) *
				      dfi_freq) / ddr_bw_val[0][ddrbw_time_num];
		ddr_bw_ch0->ddr_rd = (((u64)
				       (ddr_bw_val[0][ddrbw_rd_num] +
					ddr_bw_val[1][ddrbw_rd_num]) * 8 * 4) *
				      dfi_freq) / ddr_bw_val[0][ddrbw_time_num];
		ddr_bw_ch0->ddr_act = ddr_bw_val[0][ddrbw_act_num];
		ddr_bw_ch0->ddr_total = ddr_freq * 2 * 4;
		/* noc unit:bype */
		regmap_read(ddr_data->msch_regs, 0x1400 + 0x178, &tmp32);
		regmap_read(ddr_data->msch_regs, 0x1400 + 0x164,
			    &ddr_bw_ch0->cpum);
		ddr_bw_ch0->cpum += (tmp32 << 16);
		regmap_read(ddr_data->msch_regs, 0x1800 + 0x178, &tmp32);
		regmap_read(ddr_data->msch_regs, 0x1800 + 0x164,
			    &ddr_bw_ch0->gpu);
		ddr_bw_ch0->gpu += (tmp32 << 16);
		ddr_bw_ch0->peri = 0;
		regmap_read(ddr_data->msch_regs, 0x2000 + 0x178, &tmp32);
		regmap_read(ddr_data->msch_regs, 0x2000 + 0x164,
			    &ddr_bw_ch0->video);
		ddr_bw_ch0->video += (tmp32 << 16);
		regmap_read(ddr_data->msch_regs, 0x2400 + 0x178, &tmp32);
		regmap_read(ddr_data->msch_regs, 0x2400 + 0x164,
			    &ddr_bw_ch0->vio0);
		ddr_bw_ch0->vio0 += (tmp32 << 16);
		regmap_read(ddr_data->msch_regs, 0x2800 + 0x178, &tmp32);
		regmap_read(ddr_data->msch_regs, 0x2800 + 0x164,
			    &ddr_bw_ch0->vio1);
		ddr_bw_ch0->vio1 += (tmp32 << 16);
		ddr_bw_ch0->vio2 = 0;

		/* B/s => MB/s */
		ddr_bw_ch0->cpum =
		    (u64) ddr_bw_ch0->cpum * dfi_freq /
		    ddr_bw_val[0][ddrbw_time_num];
		ddr_bw_ch0->gpu =
		    (u64) ddr_bw_ch0->gpu * dfi_freq /
		    ddr_bw_val[0][ddrbw_time_num];
		ddr_bw_ch0->peri =
		    (u64) ddr_bw_ch0->peri * dfi_freq /
		    ddr_bw_val[0][ddrbw_time_num];
		ddr_bw_ch0->video =
		    (u64) ddr_bw_ch0->video * dfi_freq /
		    ddr_bw_val[0][ddrbw_time_num];
		ddr_bw_ch0->vio0 =
		    (u64) ddr_bw_ch0->vio0 * dfi_freq /
		    ddr_bw_val[0][ddrbw_time_num];
		ddr_bw_ch0->vio1 =
		    (u64) ddr_bw_ch0->vio1 * dfi_freq /
		    ddr_bw_val[0][ddrbw_time_num];
		ddr_bw_ch0->vio2 =
		    (u64) ddr_bw_ch0->vio2 * dfi_freq /
		    ddr_bw_val[0][ddrbw_time_num];
	}
end:
	ddr_monitor_start();
}

static void ddr_init(u32 dram_speed_bin, u32 freq)
{
	int lcdc_type;

	lcdc_type = rockchip_get_screen_type();
	printk(KERN_DEBUG pr_fmt("In Func:%s,dram_speed_bin:%d,freq:%d,lcdc_type:%d\n"),
	       __func__, dram_speed_bin, freq, lcdc_type);
	if (scpi_ddr_init(dram_speed_bin, freq, lcdc_type))
		pr_info("ddr init error\n");
	else
		printk(KERN_DEBUG pr_fmt("%s out\n"), __func__);
}

static int ddr_init_resume(struct platform_device *pdev)
{
	ddr_init(DDR3_DEFAULT, 0);
	return 0;
}

static int __init rockchip_ddr_probe(struct platform_device *pdev)
{
	struct device_node *np;

	np = pdev->dev.of_node;
	ddr_data =
	    devm_kzalloc(&pdev->dev, sizeof(struct rockchip_ddr), GFP_KERNEL);
	if (!ddr_data) {
		dev_err(&pdev->dev, "no memory for state\n");
		return -ENOMEM;
	}
	/* ddrpctl */
	ddr_data->ddrpctl_regs =
	    syscon_regmap_lookup_by_phandle(np, "rockchip,ddrpctl");
	if (IS_ERR(ddr_data->ddrpctl_regs)) {
		dev_err(&pdev->dev, "%s: could not find ddrpctl dt node\n",
			__func__);
		return -ENXIO;
	}

	/* grf */
	ddr_data->grf_regs =
	    syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR(ddr_data->grf_regs)) {
		dev_err(&pdev->dev, "%s: could not find grf dt node\n",
			__func__);
		return -ENXIO;
	}
	/* msch */
	ddr_data->msch_regs =
	    syscon_regmap_lookup_by_phandle(np, "rockchip,msch");
	if (IS_ERR(ddr_data->msch_regs)) {
		dev_err(&pdev->dev, "%s: could not find msch dt node\n",
			__func__);
		return -ENXIO;
	}

	platform_set_drvdata(pdev, ddr_data);
	ddr_change_freq = _ddr_change_freq;
	ddr_round_rate = _ddr_round_rate;
	ddr_set_auto_self_refresh = _ddr_set_auto_self_refresh;
	ddr_bandwidth_get = _ddr_bandwidth_get;
	ddr_recalc_rate = _ddr_recalc_rate;
	ddr_init(DDR3_DEFAULT, 0);
	pr_info("%s: success\n", __func__);
	return 0;
}

static const struct of_device_id rockchip_ddr_of_match[] __refdata = {
	{.compatible = "rockchip,rk3368-ddr", .data = NULL,},
	{},
};

static struct platform_driver rockchip_ddr_driver = {
#ifdef CONFIG_PM
	.resume = ddr_init_resume,
#endif /* CONFIG_PM */
	.driver = {
		   .name = "rockchip_ddr",
		   .of_match_table = rockchip_ddr_of_match,
	},
};

static int __init rockchip_ddr_init(void)
{
	pr_info("rockchip_ddr_init\n");
	return platform_driver_probe(&rockchip_ddr_driver, rockchip_ddr_probe);
}

device_initcall(rockchip_ddr_init);
