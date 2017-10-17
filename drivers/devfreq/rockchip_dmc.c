/*
 * Copyright (c) 2016, Fuzhou Rockchip Electronics Co., Ltd.
 * Author: Lin Huang <hl@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <dt-bindings/clock/rockchip-ddr.h>
#include <dt-bindings/display/rk_fb.h>
#include <dt-bindings/soc/rockchip-system-status.h>
#include <drm/drmP.h>
#include <drm/drm_modeset_lock.h>
#include <linux/arm-smccc.h>
#include <linux/clk.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/devfreq.h>
#include <linux/devfreq-event.h>
#include <linux/fb.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/reboot.h>
#include <linux/regulator/consumer.h>
#include <linux/rockchip/rockchip_sip.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/suspend.h>

#include <soc/rockchip/rkfb_dmc.h>
#include <soc/rockchip/rockchip_dmc.h>
#include <soc/rockchip/rockchip_sip.h>
#include <soc/rockchip/rockchip-system-status.h>
#include <soc/rockchip/scpi.h>
#include <uapi/drm/drm_mode.h>

#include "governor.h"

#define system_status_to_dmcfreq(nb) container_of(nb, struct rockchip_dmcfreq, \
						  system_status_nb)
#define reboot_to_dmcfreq(nb) container_of(nb, struct rockchip_dmcfreq, \
					   reboot_nb)

#define VIDEO_1080P_SIZE	(1920 * 1080)
#define FIQ_INIT_HANDLER	(0x1)
#define FIQ_CPU_TGT_BOOT	(0x0) /* to booting cpu */
#define FIQ_NUM_FOR_DCF		(143) /* NA irq map to fiq for dcf */
#define DTS_PAR_OFFSET		(4096)

struct freq_map_table {
	unsigned int min;
	unsigned int max;
	unsigned long freq;
};

struct video_info {
	unsigned int width;
	unsigned int height;
	unsigned int ishevc;
	unsigned int videoFramerate;
	unsigned int streamBitrate;
	struct list_head node;
};

struct share_params {
	u32 hz;
	u32 lcdc_type;
	u32 vop;
	u32 vop_dclk_mode;
	u32 sr_idle_en;
	u32 addr_mcu_el3;
	/*
	 * 1: need to wait flag1
	 * 0: never wait flag1
	 */
	u32 wait_flag1;
	/*
	 * 1: need to wait flag1
	 * 0: never wait flag1
	 */
	u32 wait_flag0;
	 /* if need, add parameter after */
};

static struct share_params *ddr_psci_param;

static const char *rk3128_dts_timing[] = {
	"ddr3_speed_bin",
	"pd_idle",
	"sr_idle",
	"auto_pd_dis_freq",
	"auto_sr_dis_freq",
	"ddr3_dll_dis_freq",
	"lpddr2_dll_dis_freq",
	"phy_dll_dis_freq",
	"ddr3_odt_dis_freq",
	"phy_ddr3_odt_disb_freq",
	"ddr3_drv",
	"ddr3_odt",
	"phy_ddr3_clk_drv",
	"phy_ddr3_cmd_drv",
	"phy_ddr3_dqs_drv",
	"phy_ddr3_odt",
	"lpddr2_drv",
	"phy_lpddr2_clk_drv",
	"phy_lpddr2_cmd_drv",
	"phy_lpddr2_dqs_drv"
};

struct rk3128_ddr_dts_config_timing {
	u32 ddr3_speed_bin;
	u32 pd_idle;
	u32 sr_idle;
	u32 auto_pd_dis_freq;
	u32 auto_sr_dis_freq;
	u32 ddr3_dll_dis_freq;
	u32 lpddr2_dll_dis_freq;
	u32 phy_dll_dis_freq;
	u32 ddr3_odt_dis_freq;
	u32 phy_ddr3_odt_disb_freq;
	u32 ddr3_drv;
	u32 ddr3_odt;
	u32 phy_ddr3_clk_drv;
	u32 phy_ddr3_cmd_drv;
	u32 phy_ddr3_dqs_drv;
	u32 phy_ddr3_odt;
	u32 lpddr2_drv;
	u32 phy_lpddr2_clk_drv;
	u32 phy_lpddr2_cmd_drv;
	u32 phy_lpddr2_dqs_drv;
	u32 available;
};

char *rk3288_dts_timing[] = {
	"ddr3_speed_bin",
	"pd_idle",
	"sr_idle",

	"auto_pd_dis_freq",
	"auto_sr_dis_freq",
	/* for ddr3 only */
	"ddr3_dll_dis_freq",
	"phy_dll_dis_freq",

	"ddr3_odt_dis_freq",
	"phy_ddr3_odt_dis_freq",
	"ddr3_drv",
	"ddr3_odt",
	"phy_ddr3_drv",
	"phy_ddr3_odt",

	"lpddr2_drv",
	"phy_lpddr2_drv",

	"lpddr3_odt_dis_freq",
	"phy_lpddr3_odt_dis_freq",
	"lpddr3_drv",
	"lpddr3_odt",
	"phy_lpddr3_drv",
	"phy_lpddr3_odt"
};

struct rk3288_ddr_dts_config_timing {
	unsigned int ddr3_speed_bin;
	unsigned int pd_idle;
	unsigned int sr_idle;

	unsigned int auto_pd_dis_freq;
	unsigned int auto_sr_dis_freq;
	/* for ddr3 only */
	unsigned int ddr3_dll_dis_freq;
	unsigned int phy_dll_dis_freq;

	unsigned int ddr3_odt_dis_freq;
	unsigned int phy_ddr3_odt_dis_freq;
	unsigned int ddr3_drv;
	unsigned int ddr3_odt;
	unsigned int phy_ddr3_drv;
	unsigned int phy_ddr3_odt;

	unsigned int lpddr2_drv;
	unsigned int phy_lpddr2_drv;

	unsigned int lpddr3_odt_dis_freq;
	unsigned int phy_lpddr3_odt_dis_freq;
	unsigned int lpddr3_drv;
	unsigned int lpddr3_odt;
	unsigned int phy_lpddr3_drv;
	unsigned int phy_lpddr3_odt;

	unsigned int available;
};

struct rk3368_dram_timing {
	u32 dram_spd_bin;
	u32 sr_idle;
	u32 pd_idle;
	u32 dram_dll_dis_freq;
	u32 phy_dll_dis_freq;
	u32 dram_odt_dis_freq;
	u32 phy_odt_dis_freq;
	u32 ddr3_drv;
	u32 ddr3_odt;
	u32 lpddr3_drv;
	u32 lpddr3_odt;
	u32 lpddr2_drv;
	u32 phy_clk_drv;
	u32 phy_cmd_drv;
	u32 phy_dqs_drv;
	u32 phy_odt;
};

struct rk3399_dram_timing {
	unsigned int ddr3_speed_bin;
	unsigned int pd_idle;
	unsigned int sr_idle;
	unsigned int sr_mc_gate_idle;
	unsigned int srpd_lite_idle;
	unsigned int standby_idle;
	unsigned int auto_lp_dis_freq;
	unsigned int ddr3_dll_dis_freq;
	unsigned int phy_dll_dis_freq;
	unsigned int ddr3_odt_dis_freq;
	unsigned int ddr3_drv;
	unsigned int ddr3_odt;
	unsigned int phy_ddr3_ca_drv;
	unsigned int phy_ddr3_dq_drv;
	unsigned int phy_ddr3_odt;
	unsigned int lpddr3_odt_dis_freq;
	unsigned int lpddr3_drv;
	unsigned int lpddr3_odt;
	unsigned int phy_lpddr3_ca_drv;
	unsigned int phy_lpddr3_dq_drv;
	unsigned int phy_lpddr3_odt;
	unsigned int lpddr4_odt_dis_freq;
	unsigned int lpddr4_drv;
	unsigned int lpddr4_dq_odt;
	unsigned int lpddr4_ca_odt;
	unsigned int phy_lpddr4_ca_drv;
	unsigned int phy_lpddr4_ck_cs_drv;
	unsigned int phy_lpddr4_dq_drv;
	unsigned int phy_lpddr4_odt;
};

struct rockchip_dmcfreq {
	struct device *dev;
	struct devfreq *devfreq;
	struct devfreq_simple_ondemand_data ondemand_data;
	struct clk *dmc_clk;
	struct devfreq_event_dev *edev;
	struct mutex lock; /* scaling frequency lock */
	struct dram_timing *timing;
	struct regulator *vdd_center;
	struct notifier_block system_status_nb;
	struct notifier_block reboot_nb;
	struct notifier_block fb_nb;
	struct list_head video_info_list;
	struct freq_map_table *vop_bw_tbl;

	unsigned long rate, target_rate;
	unsigned long volt, target_volt;

	unsigned long min;
	unsigned long max;
	unsigned long auto_min_rate;
	unsigned long status_rate;
	unsigned long normal_rate;
	unsigned long video_1080p_rate;
	unsigned long video_4k_rate;
	unsigned long video_4k_10b_rate;
	unsigned long performance_rate;
	unsigned long dualview_rate;
	unsigned long hdmi_rate;
	unsigned long idle_rate;
	unsigned long suspend_rate;
	unsigned long reboot_rate;
	unsigned long boost_rate;
	unsigned long isp_rate;
	unsigned long low_power_rate;
	unsigned long vop_req_rate;

	unsigned int min_cpu_freq;
	unsigned int auto_freq_en;
	unsigned int refresh;
	unsigned int last_refresh;
	bool is_dualview;

	int (*set_auto_self_refresh)(u32 en);
};

static struct rockchip_dmcfreq *rk_dmcfreq;

static int rockchip_dmcfreq_target(struct device *dev, unsigned long *freq,
				   u32 flags)
{
	struct rockchip_dmcfreq *dmcfreq = dev_get_drvdata(dev);
	struct dev_pm_opp *opp;
	struct cpufreq_policy *policy;
	unsigned long old_clk_rate = dmcfreq->rate;
	unsigned long temp_rate, target_volt, target_rate;
	unsigned int cpu_cur, cpufreq_cur;
	int err;

	rcu_read_lock();

	opp = devfreq_recommended_opp(dev, freq, flags);
	if (IS_ERR(opp)) {
		rcu_read_unlock();
		return PTR_ERR(opp);
	}
	temp_rate = dev_pm_opp_get_freq(opp);
	target_volt = dev_pm_opp_get_voltage(opp);

	rcu_read_unlock();

	target_rate = clk_round_rate(dmcfreq->dmc_clk, temp_rate);
	if ((long)target_rate <= 0)
		target_rate = temp_rate;

	if (dmcfreq->rate == target_rate) {
		if (dmcfreq->volt == target_volt)
			return 0;
		err = regulator_set_voltage(dmcfreq->vdd_center, target_volt,
					    INT_MAX);
		if (err) {
			dev_err(dev, "Cannot set voltage %lu uV\n",
				target_volt);
			return err;
		}
	}

	mutex_lock(&dmcfreq->lock);

	/*
	 * We need to prevent cpu hotplug from happening while a dmc freq rate
	 * change is happening.
	 *
	 * Do this before taking the policy rwsem to avoid deadlocks between the
	 * mutex that is locked/unlocked in cpu_hotplug_disable/enable. And it
	 * can also avoid deadlocks between the mutex that is locked/unlocked
	 * in get/put_online_cpus (such as store_scaling_max_freq()).
	 */
	get_online_cpus();

	/*
	 * Go to specified cpufreq and block other cpufreq changes since
	 * set_rate needs to complete during vblank.
	 */
	cpu_cur = smp_processor_id();
	policy = cpufreq_cpu_get(cpu_cur);
	if (!policy) {
		dev_err(dev, "cpu%d policy NULL\n", cpu_cur);
		goto cpufreq;
	}
	down_write(&policy->rwsem);
	cpufreq_cur = cpufreq_quick_get(cpu_cur);

	/* If we're thermally throttled; don't change; */
	if (dmcfreq->min_cpu_freq && cpufreq_cur < dmcfreq->min_cpu_freq) {
		if (policy->max >= dmcfreq->min_cpu_freq)
			__cpufreq_driver_target(policy, dmcfreq->min_cpu_freq,
						CPUFREQ_RELATION_L);
		else
			dev_dbg(dev, "CPU may too slow for DMC (%d MHz)\n",
				policy->max);
	}

	/*
	 * If frequency scaling from low to high, adjust voltage first.
	 * If frequency scaling from high to low, adjust frequency first.
	 */
	if (old_clk_rate < target_rate) {
		err = regulator_set_voltage(dmcfreq->vdd_center, target_volt,
					    INT_MAX);
		if (err) {
			dev_err(dev, "Cannot set voltage %lu uV\n",
				target_volt);
			goto out;
		}
	}

	dev_dbg(dev, "%lu-->%lu\n", old_clk_rate, target_rate);
	err = clk_set_rate(dmcfreq->dmc_clk, target_rate);
	if (err) {
		dev_err(dev, "Cannot set frequency %lu (%d)\n",
			target_rate, err);
		regulator_set_voltage(dmcfreq->vdd_center, dmcfreq->volt,
				      INT_MAX);
		goto out;
	}

	/*
	 * Check the dpll rate,
	 * There only two result we will get,
	 * 1. Ddr frequency scaling fail, we still get the old rate.
	 * 2. Ddr frequency scaling sucessful, we get the rate we set.
	 */
	dmcfreq->rate = clk_get_rate(dmcfreq->dmc_clk);

	/* If get the incorrect rate, set voltage to old value. */
	if (dmcfreq->rate != target_rate) {
		dev_err(dev, "Get wrong frequency, Request %lu, Current %lu\n",
			target_rate, dmcfreq->rate);
		regulator_set_voltage(dmcfreq->vdd_center, dmcfreq->volt,
				      INT_MAX);
		goto out;
	} else if (old_clk_rate > target_rate) {
		err = regulator_set_voltage(dmcfreq->vdd_center, target_volt,
					    INT_MAX);
		if (err) {
			dev_err(dev, "Cannot set vol %lu uV\n", target_volt);
			goto out;
		}
	}

	dmcfreq->volt = target_volt;
out:
	__cpufreq_driver_target(policy, cpufreq_cur, CPUFREQ_RELATION_L);
	up_write(&policy->rwsem);
	cpufreq_cpu_put(policy);
cpufreq:
	put_online_cpus();
	mutex_unlock(&dmcfreq->lock);
	return err;
}

static int rockchip_dmcfreq_get_dev_status(struct device *dev,
					   struct devfreq_dev_status *stat)
{
	struct rockchip_dmcfreq *dmcfreq = dev_get_drvdata(dev);
	struct devfreq_event_data edata;
	int ret = 0;

	ret = devfreq_event_get_event(dmcfreq->edev, &edata);
	if (ret < 0)
		return ret;

	stat->current_frequency = dmcfreq->rate;
	stat->busy_time = edata.load_count;
	stat->total_time = edata.total_count;

	return ret;
}

static int rockchip_dmcfreq_get_cur_freq(struct device *dev,
					 unsigned long *freq)
{
	struct rockchip_dmcfreq *dmcfreq = dev_get_drvdata(dev);

	*freq = dmcfreq->rate;

	return 0;
}

static struct devfreq_dev_profile rockchip_devfreq_dmc_profile = {
	.polling_ms	= 50,
	.target		= rockchip_dmcfreq_target,
	.get_dev_status	= rockchip_dmcfreq_get_dev_status,
	.get_cur_freq	= rockchip_dmcfreq_get_cur_freq,
};

static __maybe_unused int rockchip_dmcfreq_suspend(struct device *dev)
{
	struct rockchip_dmcfreq *dmcfreq = dev_get_drvdata(dev);
	int ret = 0;

	ret = devfreq_event_disable_edev(dmcfreq->edev);
	if (ret < 0) {
		dev_err(dev, "failed to disable the devfreq-event devices\n");
		return ret;
	}

	ret = devfreq_suspend_device(dmcfreq->devfreq);
	if (ret < 0) {
		dev_err(dev, "failed to suspend the devfreq devices\n");
		return ret;
	}

	return 0;
}

static __maybe_unused int rockchip_dmcfreq_resume(struct device *dev)
{
	struct rockchip_dmcfreq *dmcfreq = dev_get_drvdata(dev);
	int ret = 0;

	ret = devfreq_event_enable_edev(dmcfreq->edev);
	if (ret < 0) {
		dev_err(dev, "failed to enable the devfreq-event devices\n");
		return ret;
	}

	ret = devfreq_resume_device(dmcfreq->devfreq);
	if (ret < 0) {
		dev_err(dev, "failed to resume the devfreq devices\n");
		return ret;
	}
	return ret;
}

static SIMPLE_DEV_PM_OPS(rockchip_dmcfreq_pm, rockchip_dmcfreq_suspend,
			 rockchip_dmcfreq_resume);

static int rockchip_dmcfreq_init_freq_table(struct device *dev,
					    struct devfreq_dev_profile *devp)
{
	int count;
	int i = 0;
	unsigned long freq = 0;
	struct dev_pm_opp *opp;

	rcu_read_lock();
	count = dev_pm_opp_get_opp_count(dev);
	if (count < 0) {
		rcu_read_unlock();
		return count;
	}
	rcu_read_unlock();

	devp->freq_table = kmalloc_array(count, sizeof(devp->freq_table[0]),
				GFP_KERNEL);
	if (!devp->freq_table)
		return -ENOMEM;

	rcu_read_lock();
	for (i = 0; i < count; i++, freq++) {
		opp = dev_pm_opp_find_freq_ceil(dev, &freq);
		if (IS_ERR(opp))
			break;

		devp->freq_table[i] = freq;
	}
	rcu_read_unlock();

	if (count != i)
		dev_warn(dev, "Unable to enumerate all OPPs (%d!=%d)\n",
			 count, i);

	devp->max_state = i;
	return 0;
}

static void of_get_rk3128_timings(struct device *dev,
				  struct device_node *np, uint32_t *timing)
{
	struct device_node *np_tim;
	u32 *p;
	struct rk3128_ddr_dts_config_timing *dts_timing;
	struct share_params *init_timing;
	int ret = 0;
	u32 i;

	init_timing = (struct share_params *)timing;

	if (of_property_read_u32(np, "vop-dclk-mode",
				 &init_timing->vop_dclk_mode))
		init_timing->vop_dclk_mode = 0;

	p = timing + DTS_PAR_OFFSET / 4;
	np_tim = of_parse_phandle(np, "rockchip,ddr_timing", 0);
	if (!np_tim) {
		ret = -EINVAL;
		goto end;
	}
	for (i = 0; i < ARRAY_SIZE(rk3128_dts_timing); i++) {
		ret |= of_property_read_u32(np_tim, rk3128_dts_timing[i],
					p + i);
	}
end:
	dts_timing =
		(struct rk3128_ddr_dts_config_timing *)(timing +
							DTS_PAR_OFFSET / 4);
	if (!ret) {
		dts_timing->available = 1;
	} else {
		dts_timing->available = 0;
		dev_err(dev, "of_get_ddr_timings: fail\n");
	}

	of_node_put(np_tim);
}

static void of_get_rk3288_timings(struct device *dev,
				  struct device_node *np, uint32_t *timing)
{
	struct device_node *np_tim;
	u32 *p;
	struct rk3288_ddr_dts_config_timing *dts_timing;
	struct share_params *init_timing;
	int ret = 0;
	u32 i;

	init_timing = (struct share_params *)timing;

	if (of_property_read_u32(np, "vop-dclk-mode",
				 &init_timing->vop_dclk_mode))
		init_timing->vop_dclk_mode = 0;

	p = timing + DTS_PAR_OFFSET / 4;
	np_tim = of_parse_phandle(np, "rockchip,ddr_timing", 0);
	if (!np_tim) {
		ret = -EINVAL;
		goto end;
	}
	for (i = 0; i < ARRAY_SIZE(rk3288_dts_timing); i++) {
		ret |= of_property_read_u32(np_tim, rk3288_dts_timing[i],
					p + i);
	}
end:
	dts_timing =
		(struct rk3288_ddr_dts_config_timing *)(timing +
							DTS_PAR_OFFSET / 4);
	if (!ret) {
		dts_timing->available = 1;
	} else {
		dts_timing->available = 0;
		dev_err(dev, "of_get_ddr_timings: fail\n");
	}

	of_node_put(np_tim);
}

static struct rk3368_dram_timing *of_get_rk3368_timings(struct device *dev,
							struct device_node *np)
{
	struct rk3368_dram_timing *timing = NULL;
	struct device_node *np_tim;
	int ret = 0;

	np_tim = of_parse_phandle(np, "ddr_timing", 0);
	if (np_tim) {
		timing = devm_kzalloc(dev, sizeof(*timing), GFP_KERNEL);
		if (!timing)
			goto err;

		ret |= of_property_read_u32(np_tim, "dram_spd_bin",
					    &timing->dram_spd_bin);
		ret |= of_property_read_u32(np_tim, "sr_idle",
					    &timing->sr_idle);
		ret |= of_property_read_u32(np_tim, "pd_idle",
					    &timing->pd_idle);
		ret |= of_property_read_u32(np_tim, "dram_dll_disb_freq",
					    &timing->dram_dll_dis_freq);
		ret |= of_property_read_u32(np_tim, "phy_dll_disb_freq",
					    &timing->phy_dll_dis_freq);
		ret |= of_property_read_u32(np_tim, "dram_odt_disb_freq",
					    &timing->dram_odt_dis_freq);
		ret |= of_property_read_u32(np_tim, "phy_odt_disb_freq",
					    &timing->phy_odt_dis_freq);
		ret |= of_property_read_u32(np_tim, "ddr3_drv",
					    &timing->ddr3_drv);
		ret |= of_property_read_u32(np_tim, "ddr3_odt",
					    &timing->ddr3_odt);
		ret |= of_property_read_u32(np_tim, "lpddr3_drv",
					    &timing->lpddr3_drv);
		ret |= of_property_read_u32(np_tim, "lpddr3_odt",
					    &timing->lpddr3_odt);
		ret |= of_property_read_u32(np_tim, "lpddr2_drv",
					    &timing->lpddr2_drv);
		ret |= of_property_read_u32(np_tim, "phy_clk_drv",
					    &timing->phy_clk_drv);
		ret |= of_property_read_u32(np_tim, "phy_cmd_drv",
					    &timing->phy_cmd_drv);
		ret |= of_property_read_u32(np_tim, "phy_dqs_drv",
					    &timing->phy_dqs_drv);
		ret |= of_property_read_u32(np_tim, "phy_odt",
					    &timing->phy_odt);
		if (ret) {
			devm_kfree(dev, timing);
			goto err;
		}
		of_node_put(np_tim);
		return timing;
	}

err:
	if (timing) {
		devm_kfree(dev, timing);
		timing = NULL;
	}
	of_node_put(np_tim);
	return timing;
}

static int rk_drm_get_lcdc_type(void)
{
	struct drm_device *drm;
	u32 lcdc_type = 0;

	drm = drm_device_get_by_name("rockchip");
	if (drm) {
		struct drm_connector *conn;

		list_for_each_entry(conn, &drm->mode_config.connector_list,
				    head) {
			if (conn->encoder) {
				lcdc_type = conn->connector_type;
				break;
			}
		}
	}
	switch (lcdc_type) {
	case DRM_MODE_CONNECTOR_LVDS:
		lcdc_type = SCREEN_LVDS;
		break;
	case DRM_MODE_CONNECTOR_DisplayPort:
		lcdc_type = SCREEN_DP;
		break;
	case DRM_MODE_CONNECTOR_HDMIA:
	case DRM_MODE_CONNECTOR_HDMIB:
		lcdc_type = SCREEN_HDMI;
		break;
	case DRM_MODE_CONNECTOR_TV:
		lcdc_type = SCREEN_TVOUT;
		break;
	case DRM_MODE_CONNECTOR_eDP:
		lcdc_type = SCREEN_EDP;
		break;
	case DRM_MODE_CONNECTOR_DSI:
		lcdc_type = SCREEN_MIPI;
		break;
	default:
		lcdc_type = SCREEN_NULL;
		break;
	}

	return lcdc_type;
}

static int rockchip_ddr_set_auto_self_refresh(uint32_t en)
{
	struct arm_smccc_res res;

	ddr_psci_param->sr_idle_en = en;
	res = sip_smc_dram(SHARE_PAGE_TYPE_DDR, 0,
			   ROCKCHIP_SIP_CONFIG_DRAM_SET_AT_SR);

	return res.a0;
}

static int rk3128_dmc_init(struct platform_device *pdev,
			   struct rockchip_dmcfreq *dmcfreq)
{
	struct arm_smccc_res res;
	struct drm_device *drm = drm_device_get_by_name("rockchip");

	if (!drm) {
		dev_err(&pdev->dev, "Get drm_device fail\n");
		return -EPROBE_DEFER;
	}

	res = sip_smc_request_share_mem(DIV_ROUND_UP(sizeof(
					struct rk3128_ddr_dts_config_timing),
					4096) + 1, SHARE_PAGE_TYPE_DDR);
	if (res.a0) {
		dev_err(&pdev->dev, "no ATF memory for init\n");
		return -ENOMEM;
	}
	ddr_psci_param = (struct share_params *)res.a1;
	of_get_rk3128_timings(&pdev->dev, pdev->dev.of_node,
			      (uint32_t *)ddr_psci_param);

	ddr_psci_param->hz = 0;
	ddr_psci_param->lcdc_type = rk_drm_get_lcdc_type();
	res = sip_smc_dram(SHARE_PAGE_TYPE_DDR, 0,
			   ROCKCHIP_SIP_CONFIG_DRAM_INIT);

	if (res.a0) {
		dev_err(&pdev->dev, "rockchip_sip_config_dram_init error:%lx\n",
			res.a0);
		return -ENOMEM;
	}

	dmcfreq->set_auto_self_refresh = rockchip_ddr_set_auto_self_refresh;

	return 0;
}

static int rk3288_dmc_init(struct platform_device *pdev,
			   struct rockchip_dmcfreq *dmcfreq)
{
	struct device *dev = &pdev->dev;
	struct clk *pclk_phy, *pclk_upctl, *dmc_clk;
	struct arm_smccc_res res;
	struct drm_device *drm = drm_device_get_by_name("rockchip");
	int ret;

	if (!drm) {
		dev_err(dev, "Get drm_device fail\n");
		return -EPROBE_DEFER;
	}

	dmc_clk = devm_clk_get(dev, "dmc_clk");
	if (IS_ERR(dmc_clk)) {
		dev_err(dev, "Cannot get the clk dmc_clk\n");
		return PTR_ERR(dmc_clk);
	}
	ret = clk_prepare_enable(dmc_clk);
	if (ret < 0) {
		dev_err(dev, "failed to prepare/enable dmc_clk\n");
		return ret;
	}

	pclk_phy = devm_clk_get(dev, "pclk_phy0");
	if (IS_ERR(pclk_phy)) {
		dev_err(dev, "Cannot get the clk pclk_phy0\n");
		return PTR_ERR(pclk_phy);
	}
	ret = clk_prepare_enable(pclk_phy);
	if (ret < 0) {
		dev_err(dev, "failed to prepare/enable pclk_phy0\n");
		return ret;
	}
	pclk_upctl = devm_clk_get(dev, "pclk_upctl0");
	if (IS_ERR(pclk_upctl)) {
		dev_err(dev, "Cannot get the clk pclk_upctl0\n");
		return PTR_ERR(pclk_upctl);
	}
	ret = clk_prepare_enable(pclk_upctl);
	if (ret < 0) {
		dev_err(dev, "failed to prepare/enable pclk_upctl1\n");
		return ret;
	}

	pclk_phy = devm_clk_get(dev, "pclk_phy1");
	if (IS_ERR(pclk_phy)) {
		dev_err(dev, "Cannot get the clk pclk_phy1\n");
		return PTR_ERR(pclk_phy);
	}
	ret = clk_prepare_enable(pclk_phy);
	if (ret < 0) {
		dev_err(dev, "failed to prepare/enable pclk_phy1\n");
		return ret;
	}
	pclk_upctl = devm_clk_get(dev, "pclk_upctl1");
	if (IS_ERR(pclk_upctl)) {
		dev_err(dev, "Cannot get the clk pclk_upctl1\n");
		return PTR_ERR(pclk_upctl);
	}
	ret = clk_prepare_enable(pclk_upctl);
	if (ret < 0) {
		dev_err(dev, "failed to prepare/enable pclk_upctl1\n");
		return ret;
	}

	res = sip_smc_request_share_mem(DIV_ROUND_UP(sizeof(
					struct rk3288_ddr_dts_config_timing),
					4096) + 1, SHARE_PAGE_TYPE_DDR);
	if (res.a0) {
		dev_err(&pdev->dev, "no ATF memory for init\n");
		return -ENOMEM;
	}

	ddr_psci_param = (struct share_params *)res.a1;
	of_get_rk3288_timings(&pdev->dev, pdev->dev.of_node,
			      (uint32_t *)ddr_psci_param);

	ddr_psci_param->hz = 0;
	ddr_psci_param->lcdc_type = rk_drm_get_lcdc_type();
	res = sip_smc_dram(SHARE_PAGE_TYPE_DDR, 0,
			   ROCKCHIP_SIP_CONFIG_DRAM_INIT);

	if (res.a0) {
		dev_err(&pdev->dev, "rockchip_sip_config_dram_init error:%lx\n",
			res.a0);
		return -ENOMEM;
	}

	dmcfreq->set_auto_self_refresh = rockchip_ddr_set_auto_self_refresh;

	return 0;
}

static int rk3368_dmc_init(struct platform_device *pdev,
			   struct rockchip_dmcfreq *dmcfreq)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct arm_smccc_res res;
	struct rk3368_dram_timing *dram_timing;
	struct clk *pclk_phy, *pclk_upctl;
	struct drm_device *drm = drm_device_get_by_name("rockchip");
	int ret;
	u32 dram_spd_bin;
	u32 addr_mcu_el3;
	u32 dclk_mode;
	u32 lcdc_type;

	if (!drm) {
		dev_err(dev, "Get drm_device fail\n");
		return -EPROBE_DEFER;
	}

	pclk_phy = devm_clk_get(dev, "pclk_phy");
	if (IS_ERR(pclk_phy)) {
		dev_err(dev, "Cannot get the clk pclk_phy\n");
		return PTR_ERR(pclk_phy);
	}
	ret = clk_prepare_enable(pclk_phy);
	if (ret < 0) {
		dev_err(dev, "failed to prepare/enable pclk_phy\n");
		return ret;
	}
	pclk_upctl = devm_clk_get(dev, "pclk_upctl");
	if (IS_ERR(pclk_upctl)) {
		dev_err(dev, "Cannot get the clk pclk_upctl\n");
		return PTR_ERR(pclk_upctl);
	}
	ret = clk_prepare_enable(pclk_upctl);
	if (ret < 0) {
		dev_err(dev, "failed to prepare/enable pclk_upctl\n");
		return ret;
	}

	/*
	 * Get dram timing and pass it to arm trust firmware,
	 * the dram drvier in arm trust firmware will get these
	 * timing and to do dram initial.
	 */
	dram_timing = of_get_rk3368_timings(dev, np);
	if (dram_timing) {
		dram_spd_bin = dram_timing->dram_spd_bin;
		if (scpi_ddr_send_timing((u32 *)dram_timing,
					 sizeof(struct rk3368_dram_timing)))
			dev_err(dev, "send ddr timing timeout\n");
	} else {
		dev_err(dev, "get ddr timing from dts error\n");
		dram_spd_bin = DDR3_DEFAULT;
	}

	res = sip_smc_mcu_el3fiq(FIQ_INIT_HANDLER,
				 FIQ_NUM_FOR_DCF,
				 FIQ_CPU_TGT_BOOT);
	if ((res.a0) || (res.a1 == 0) || (res.a1 > 0x80000))
		dev_err(dev, "Trust version error, pls check trust version\n");
	addr_mcu_el3 = res.a1;

	if (of_property_read_u32(np, "vop-dclk-mode", &dclk_mode) == 0)
		scpi_ddr_dclk_mode(dclk_mode);

	lcdc_type = rk_drm_get_lcdc_type();

	if (scpi_ddr_init(dram_spd_bin, 0, lcdc_type,
			  addr_mcu_el3))
		dev_err(dev, "ddr init error\n");
	else
		dev_dbg(dev, ("%s out\n"), __func__);

	dmcfreq->set_auto_self_refresh = scpi_ddr_set_auto_self_refresh;

	return 0;
}

static struct rk3399_dram_timing *of_get_rk3399_timings(struct device *dev,
							struct device_node *np)
{
	struct rk3399_dram_timing *timing = NULL;
	struct device_node *np_tim;
	int ret;

	np_tim = of_parse_phandle(np, "ddr_timing", 0);
	if (np_tim) {
		timing = devm_kzalloc(dev, sizeof(*timing), GFP_KERNEL);
		if (!timing)
			goto err;

		ret = of_property_read_u32(np_tim, "ddr3_speed_bin",
					   &timing->ddr3_speed_bin);
		ret |= of_property_read_u32(np_tim, "pd_idle",
					    &timing->pd_idle);
		ret |= of_property_read_u32(np_tim, "sr_idle",
					    &timing->sr_idle);
		ret |= of_property_read_u32(np_tim, "sr_mc_gate_idle",
					    &timing->sr_mc_gate_idle);
		ret |= of_property_read_u32(np_tim, "srpd_lite_idle",
					    &timing->srpd_lite_idle);
		ret |= of_property_read_u32(np_tim, "standby_idle",
					    &timing->standby_idle);
		ret |= of_property_read_u32(np_tim, "auto_lp_dis_freq",
					    &timing->auto_lp_dis_freq);
		ret |= of_property_read_u32(np_tim, "ddr3_dll_dis_freq",
					    &timing->ddr3_dll_dis_freq);
		ret |= of_property_read_u32(np_tim, "phy_dll_dis_freq",
					    &timing->phy_dll_dis_freq);
		ret |= of_property_read_u32(np_tim, "ddr3_odt_dis_freq",
					    &timing->ddr3_odt_dis_freq);
		ret |= of_property_read_u32(np_tim, "ddr3_drv",
					    &timing->ddr3_drv);
		ret |= of_property_read_u32(np_tim, "ddr3_odt",
					    &timing->ddr3_odt);
		ret |= of_property_read_u32(np_tim, "phy_ddr3_ca_drv",
					    &timing->phy_ddr3_ca_drv);
		ret |= of_property_read_u32(np_tim, "phy_ddr3_dq_drv",
					    &timing->phy_ddr3_dq_drv);
		ret |= of_property_read_u32(np_tim, "phy_ddr3_odt",
					    &timing->phy_ddr3_odt);
		ret |= of_property_read_u32(np_tim, "lpddr3_odt_dis_freq",
					    &timing->lpddr3_odt_dis_freq);
		ret |= of_property_read_u32(np_tim, "lpddr3_drv",
					    &timing->lpddr3_drv);
		ret |= of_property_read_u32(np_tim, "lpddr3_odt",
					    &timing->lpddr3_odt);
		ret |= of_property_read_u32(np_tim, "phy_lpddr3_ca_drv",
					    &timing->phy_lpddr3_ca_drv);
		ret |= of_property_read_u32(np_tim, "phy_lpddr3_dq_drv",
					    &timing->phy_lpddr3_dq_drv);
		ret |= of_property_read_u32(np_tim, "phy_lpddr3_odt",
					    &timing->phy_lpddr3_odt);
		ret |= of_property_read_u32(np_tim, "lpddr4_odt_dis_freq",
					    &timing->lpddr4_odt_dis_freq);
		ret |= of_property_read_u32(np_tim, "lpddr4_drv",
					    &timing->lpddr4_drv);
		ret |= of_property_read_u32(np_tim, "lpddr4_dq_odt",
					    &timing->lpddr4_dq_odt);
		ret |= of_property_read_u32(np_tim, "lpddr4_ca_odt",
					    &timing->lpddr4_ca_odt);
		ret |= of_property_read_u32(np_tim, "phy_lpddr4_ca_drv",
					    &timing->phy_lpddr4_ca_drv);
		ret |= of_property_read_u32(np_tim, "phy_lpddr4_ck_cs_drv",
					    &timing->phy_lpddr4_ck_cs_drv);
		ret |= of_property_read_u32(np_tim, "phy_lpddr4_dq_drv",
					    &timing->phy_lpddr4_dq_drv);
		ret |= of_property_read_u32(np_tim, "phy_lpddr4_odt",
					    &timing->phy_lpddr4_odt);
		if (ret) {
			devm_kfree(dev, timing);
			goto err;
		}
		of_node_put(np_tim);
		return timing;
	}

err:
	if (timing) {
		devm_kfree(dev, timing);
		timing = NULL;
	}
	of_node_put(np_tim);
	return timing;
}

static int rk3399_dmc_init(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct arm_smccc_res res;
	struct rk3399_dram_timing *dram_timing;
	int index, size;
	u32 *timing;

	/*
	 * Get dram timing and pass it to arm trust firmware,
	 * the dram drvier in arm trust firmware will get these
	 * timing and to do dram initial.
	 */
	dram_timing = of_get_rk3399_timings(dev, np);
	if (dram_timing) {
		timing = (u32 *)dram_timing;
		size = sizeof(struct rk3399_dram_timing) / 4;
		for (index = 0; index < size; index++) {
			arm_smccc_smc(ROCKCHIP_SIP_DRAM_FREQ, *timing++, index,
				      ROCKCHIP_SIP_CONFIG_DRAM_SET_PARAM,
				      0, 0, 0, 0, &res);
			if (res.a0) {
				dev_err(dev, "Failed to set dram param: %ld\n",
					res.a0);
				return -EINVAL;
			}
		}
	}

	arm_smccc_smc(ROCKCHIP_SIP_DRAM_FREQ, 0, 0,
		      ROCKCHIP_SIP_CONFIG_DRAM_INIT,
		      0, 0, 0, 0, &res);

	return 0;
}

static const struct of_device_id rockchip_dmcfreq_of_match[] = {
	{ .compatible = "rockchip,rk3128-dmc", .data = rk3128_dmc_init },
	{ .compatible = "rockchip,rk3288-dmc", .data = rk3288_dmc_init },
	{ .compatible = "rockchip,rk3368-dmc", .data = rk3368_dmc_init },
	{ .compatible = "rockchip,rk3399-dmc", .data = rk3399_dmc_init },
	{ },
};
MODULE_DEVICE_TABLE(of, rockchip_dmcfreq_of_match);

static int rockchip_get_freq_map_talbe(struct device_node *np, char *porp_name,
				       struct freq_map_table **table)
{
	struct freq_map_table *tbl;
	const struct property *prop;
	unsigned int temp_freq = 0;
	int count, i;

	prop = of_find_property(np, porp_name, NULL);
	if (!prop)
		return -EINVAL;

	if (!prop->value)
		return -ENODATA;

	count = of_property_count_u32_elems(np, porp_name);
	if (count < 0)
		return -EINVAL;

	if (count % 3)
		return -EINVAL;

	tbl = kzalloc(sizeof(*tbl) * (count / 3 + 1), GFP_KERNEL);
	if (!tbl)
		return -ENOMEM;

	for (i = 0; i < count / 3; i++) {
		of_property_read_u32_index(np, porp_name, 3 * i, &tbl[i].min);
		of_property_read_u32_index(np, porp_name, 3 * i + 1,
					   &tbl[i].max);
		of_property_read_u32_index(np, porp_name, 3 * i + 2,
					   &temp_freq);
		tbl[i].freq = temp_freq * 1000;
	}

	tbl[i].min = 0;
	tbl[i].max = 0;
	tbl[i].freq = CPUFREQ_TABLE_END;

	*table = tbl;

	return 0;
}

static int rockchip_get_system_status_rate(struct device_node *np,
					   char *porp_name,
					   struct rockchip_dmcfreq *dmcfreq)
{
	const struct property *prop;
	unsigned int status = 0, freq = 0;
	int count, i;

	prop = of_find_property(np, porp_name, NULL);
	if (!prop)
		return -EINVAL;

	if (!prop->value)
		return -ENODATA;

	count = of_property_count_u32_elems(np, porp_name);
	if (count < 0)
		return -EINVAL;

	if (count % 2)
		return -EINVAL;

	for (i = 0; i < count / 2; i++) {
		of_property_read_u32_index(np, porp_name, 2 * i,
					   &status);
		of_property_read_u32_index(np, porp_name, 2 * i + 1,
					   &freq);
		switch (status) {
		case SYS_STATUS_NORMAL:
			dmcfreq->normal_rate = freq * 1000;
			break;
		case SYS_STATUS_SUSPEND:
			dmcfreq->suspend_rate = freq * 1000;
			break;
		case SYS_STATUS_VIDEO_1080P:
			dmcfreq->video_1080p_rate = freq * 1000;
			break;
		case SYS_STATUS_VIDEO_4K:
			dmcfreq->video_4k_rate = freq * 1000;
			break;
		case SYS_STATUS_VIDEO_4K_10B:
			dmcfreq->video_4k_10b_rate = freq * 1000;
			break;
		case SYS_STATUS_PERFORMANCE:
			dmcfreq->performance_rate = freq * 1000;
			break;
		case SYS_STATUS_LCDC0 | SYS_STATUS_LCDC1:
			dmcfreq->dualview_rate = freq * 1000;
			break;
		case SYS_STATUS_HDMI:
			dmcfreq->hdmi_rate = freq * 1000;
			break;
		case SYS_STATUS_IDLE:
			dmcfreq->idle_rate = freq * 1000;
			break;
		case SYS_STATUS_REBOOT:
			dmcfreq->reboot_rate = freq * 1000;
			break;
		case SYS_STATUS_BOOST:
			dmcfreq->boost_rate = freq * 1000;
			break;
		case SYS_STATUS_ISP:
			dmcfreq->isp_rate = freq * 1000;
			break;
		case SYS_STATUS_LOW_POWER:
			dmcfreq->low_power_rate = freq * 1000;
			break;
		default:
			break;
		}
	}

	return 0;
}

static void rockchip_dmcfreq_update_target(struct rockchip_dmcfreq *dmcfreq)
{
	struct devfreq *df = dmcfreq->devfreq;

	mutex_lock(&df->lock);

	if (dmcfreq->last_refresh != dmcfreq->refresh) {
		if (dmcfreq->set_auto_self_refresh)
			dmcfreq->set_auto_self_refresh(dmcfreq->refresh);
		dmcfreq->last_refresh = dmcfreq->refresh;
	}

	update_devfreq(df);

	mutex_unlock(&df->lock);
}

static int rockchip_dmcfreq_system_status_notifier(struct notifier_block *nb,
						   unsigned long status,
						   void *ptr)
{
	struct rockchip_dmcfreq *dmcfreq = system_status_to_dmcfreq(nb);
	struct devfreq *df = dmcfreq->devfreq;
	unsigned long target_rate = 0;
	unsigned int refresh = false;
	bool is_dualview = false;

	if (dmcfreq->dualview_rate && (status & SYS_STATUS_LCDC0) &&
	    (status & SYS_STATUS_LCDC1)) {
		if (dmcfreq->dualview_rate > target_rate) {
			target_rate = dmcfreq->dualview_rate;
			is_dualview = true;
			goto next;
		}
	}

	if (dmcfreq->reboot_rate && (status & SYS_STATUS_REBOOT)) {
		target_rate = dmcfreq->reboot_rate;
		goto next;
	}

	if (dmcfreq->suspend_rate && (status & SYS_STATUS_SUSPEND)) {
		if (dmcfreq->suspend_rate > target_rate) {
			target_rate = dmcfreq->suspend_rate;
			refresh = true;
			goto next;
		}
	}

	if (dmcfreq->low_power_rate && (status & SYS_STATUS_LOW_POWER)) {
		target_rate = dmcfreq->low_power_rate;
		goto next;
	}

	if (dmcfreq->performance_rate && (status & SYS_STATUS_PERFORMANCE)) {
		if (dmcfreq->performance_rate > target_rate)
			target_rate = dmcfreq->performance_rate;
	}

	if (dmcfreq->hdmi_rate && (status & SYS_STATUS_HDMI)) {
		if (dmcfreq->hdmi_rate > target_rate)
			target_rate = dmcfreq->hdmi_rate;
	}

	if (dmcfreq->video_4k_rate && (status & SYS_STATUS_VIDEO_4K)) {
		if (dmcfreq->video_4k_rate > target_rate)
			target_rate = dmcfreq->video_4k_rate;
	}

	if (dmcfreq->video_4k_10b_rate && (status & SYS_STATUS_VIDEO_4K_10B)) {
		if (dmcfreq->video_4k_10b_rate > target_rate)
			target_rate = dmcfreq->video_4k_10b_rate;
	}

	if (dmcfreq->video_1080p_rate && (status & SYS_STATUS_VIDEO_1080P)) {
		if (dmcfreq->video_1080p_rate > target_rate)
			target_rate = dmcfreq->video_1080p_rate;
	}

	if (dmcfreq->isp_rate && (status & SYS_STATUS_ISP)) {
		if (dmcfreq->isp_rate > target_rate)
			target_rate = dmcfreq->isp_rate;
	}

next:

	dev_dbg(&df->dev, "status=0x%x\n", (unsigned int)status);
	dmcfreq->refresh = refresh;
	dmcfreq->is_dualview = is_dualview;
	dmcfreq->status_rate = target_rate;
	rockchip_dmcfreq_update_target(dmcfreq);

	return NOTIFY_OK;
}

static int rockchip_dmcfreq_reboot_notifier(struct notifier_block *nb,
					    unsigned long action, void *ptr)
{
	struct rockchip_dmcfreq *dmcfreq = reboot_to_dmcfreq(nb);

	devfreq_monitor_stop(dmcfreq->devfreq);
	rockchip_set_system_status(SYS_STATUS_REBOOT);

	return NOTIFY_OK;
}

static int rockchip_dmcfreq_fb_notifier(struct notifier_block *nb,
					unsigned long action, void *ptr)
{
	struct fb_event *event = ptr;

	switch (action) {
	case FB_EARLY_EVENT_BLANK:
		switch (*((int *)event->data)) {
		case FB_BLANK_UNBLANK:
			rockchip_clear_system_status(SYS_STATUS_SUSPEND);
			break;
		default:
			break;
		}
		break;
	case FB_EVENT_BLANK:
		switch (*((int *)event->data)) {
		case FB_BLANK_POWERDOWN:
			rockchip_set_system_status(SYS_STATUS_SUSPEND);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static ssize_t rockchip_dmcfreq_status_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	unsigned int status = rockchip_get_system_status();

	return sprintf(buf, "0x%x\n", status);
}

static unsigned long rockchip_get_video_param(char **str)
{
	char *p;
	unsigned long val = 0;

	strsep(str, "=");
	p = strsep(str, ",");
	if (p) {
		if (kstrtoul(p, 10, &val))
			return 0;
	}

	return val;
}

/*
 * format:
 * 0,width=val,height=val,ishevc=val,videoFramerate=val,streamBitrate=val
 * 1,width=val,height=val,ishevc=val,videoFramerate=val,streamBitrate=val
 */
static struct video_info *rockchip_parse_video_info(const char *buf)
{
	struct video_info *video_info;
	const char *cp = buf;
	char *str;
	int ntokens = 0;

	while ((cp = strpbrk(cp + 1, ",")))
		ntokens++;
	if (ntokens != 5)
		return NULL;

	video_info = kzalloc(sizeof(*video_info), GFP_KERNEL);
	if (!video_info)
		return NULL;

	INIT_LIST_HEAD(&video_info->node);

	str = kstrdup(buf, GFP_KERNEL);
	strsep(&str, ",");
	video_info->width = rockchip_get_video_param(&str);
	video_info->height = rockchip_get_video_param(&str);
	video_info->ishevc = rockchip_get_video_param(&str);
	video_info->videoFramerate = rockchip_get_video_param(&str);
	video_info->streamBitrate = rockchip_get_video_param(&str);
	pr_debug("%c,width=%d,height=%d,ishevc=%d,videoFramerate=%d,streamBitrate=%d\n",
		 buf[0],
		 video_info->width,
		 video_info->height,
		 video_info->ishevc,
		 video_info->videoFramerate,
		 video_info->streamBitrate);
	kfree(str);

	return video_info;
}

struct video_info *rockchip_find_video_info(struct rockchip_dmcfreq *dmcfreq,
					    const char *buf)
{
	struct video_info *info, *video_info;

	video_info = rockchip_parse_video_info(buf);

	if (!video_info)
		return NULL;

	list_for_each_entry(info, &dmcfreq->video_info_list, node) {
		if ((info->width == video_info->width) &&
		    (info->height == video_info->height) &&
		    (info->ishevc == video_info->ishevc) &&
		    (info->videoFramerate == video_info->videoFramerate) &&
		    (info->streamBitrate == video_info->streamBitrate)) {
			kfree(video_info);
			return info;
		}
	}

	kfree(video_info);

	return NULL;
}

static void rockchip_add_video_info(struct rockchip_dmcfreq *dmcfreq,
				    struct video_info *video_info)
{
	if (video_info)
		list_add(&video_info->node, &dmcfreq->video_info_list);
}

static void rockchip_del_video_info(struct video_info *video_info)
{
	if (video_info) {
		list_del(&video_info->node);
		kfree(video_info);
	}
}

static void rockchip_update_video_info(struct rockchip_dmcfreq *dmcfreq)
{
	struct video_info *video_info;
	int max_res = 0, max_stream_bitrate = 0, res = 0;

	if (list_empty(&dmcfreq->video_info_list)) {
		rockchip_clear_system_status(SYS_STATUS_VIDEO);
		return;
	}

	list_for_each_entry(video_info, &dmcfreq->video_info_list, node) {
		res = video_info->width * video_info->height;
		if (res > max_res)
			max_res = res;
		if (video_info->streamBitrate > max_stream_bitrate)
			max_stream_bitrate = video_info->streamBitrate;
	}

	if (max_res <= VIDEO_1080P_SIZE) {
		rockchip_set_system_status(SYS_STATUS_VIDEO_1080P);
	} else {
		if (max_stream_bitrate == 10)
			rockchip_set_system_status(SYS_STATUS_VIDEO_4K_10B);
		else
			rockchip_set_system_status(SYS_STATUS_VIDEO_4K);
	}
}

static ssize_t rockchip_dmcfreq_status_store(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf,
					     size_t count)
{
	struct devfreq *devfreq = to_devfreq(dev);
	struct rockchip_dmcfreq *dmcfreq = dev_get_drvdata(devfreq->dev.parent);
	struct video_info *video_info;

	if (!count)
		return -EINVAL;

	switch (buf[0]) {
	case '0':
		/* clear video flag */
		video_info = rockchip_find_video_info(dmcfreq, buf);
		if (video_info) {
			rockchip_del_video_info(video_info);
			rockchip_update_video_info(dmcfreq);
		}
		break;
	case '1':
		/* set video flag */
		video_info = rockchip_parse_video_info(buf);
		if (video_info) {
			rockchip_add_video_info(dmcfreq, video_info);
			rockchip_update_video_info(dmcfreq);
		}
		break;
	case 'L':
		/* clear low power flag */
		rockchip_clear_system_status(SYS_STATUS_LOW_POWER);
		break;
	case 'l':
		/* set low power flag */
		rockchip_set_system_status(SYS_STATUS_LOW_POWER);
		break;
	case 'p':
		/* set performance flag */
		rockchip_set_system_status(SYS_STATUS_PERFORMANCE);
		break;
	case 'n':
		/* clear performance flag */
		rockchip_clear_system_status(SYS_STATUS_PERFORMANCE);
		break;
	default:
		break;
	}

	return count;
}

static DEVICE_ATTR(system_status, 0644, rockchip_dmcfreq_status_show,
		   rockchip_dmcfreq_status_store);

void rockchip_dmcfreq_vop_bandwidth_update(unsigned int bw_mbyte)
{
	struct rockchip_dmcfreq *dmcfreq = rk_dmcfreq;
	unsigned long vop_last_rate, target = 0;
	int i;

	if (!dmcfreq || !dmcfreq->auto_freq_en || !dmcfreq->vop_bw_tbl)
		return;

	for (i = 0; dmcfreq->vop_bw_tbl[i].freq != CPUFREQ_TABLE_END; i++) {
		if (bw_mbyte >= dmcfreq->vop_bw_tbl[i].min)
			target = dmcfreq->vop_bw_tbl[i].freq;
	}

	dev_dbg(dmcfreq->dev, "bw=%u\n", bw_mbyte);

	if (!target || target == dmcfreq->vop_req_rate)
		return;

	vop_last_rate = dmcfreq->vop_req_rate;
	dmcfreq->vop_req_rate = target;

	if (target > vop_last_rate)
		rockchip_dmcfreq_update_target(dmcfreq);
}

int rockchip_dmcfreq_vop_bandwidth_request(unsigned int bw_mbyte)
{
	struct rockchip_dmcfreq *dmcfreq = rk_dmcfreq;
	unsigned long target = 0;
	int i;

	if (!dmcfreq || !dmcfreq->auto_freq_en || !dmcfreq->vop_bw_tbl)
		return 0;

	for (i = 0; dmcfreq->vop_bw_tbl[i].freq != CPUFREQ_TABLE_END; i++) {
		if (bw_mbyte <= dmcfreq->vop_bw_tbl[i].max) {
			target = dmcfreq->vop_bw_tbl[i].freq;
			break;
		}
	}
	if (target)
		return 0;
	else
		return -EINVAL;
}

static int devfreq_dmc_ondemand_func(struct devfreq *df,
				     unsigned long *freq)
{
	int err;
	struct devfreq_dev_status *stat;
	unsigned long long a, b;
	struct devfreq_simple_ondemand_data *data = df->data;
	unsigned int upthreshold = data->upthreshold;
	unsigned int downdifferential = data->downdifferential;
	unsigned long max_freq = (df->max_freq) ? df->max_freq : UINT_MAX;
	struct rockchip_dmcfreq *dmcfreq = dev_get_drvdata(df->dev.parent);
	unsigned long target_freq = 0;

	if (dmcfreq->auto_freq_en && !dmcfreq->is_dualview) {
		if (dmcfreq->status_rate)
			target_freq = dmcfreq->status_rate;
		else if (dmcfreq->auto_min_rate)
			target_freq = dmcfreq->auto_min_rate;
		target_freq = max(target_freq, dmcfreq->vop_req_rate);
	} else {
		if (dmcfreq->status_rate)
			target_freq = dmcfreq->status_rate;
		else if (dmcfreq->normal_rate)
			target_freq = dmcfreq->normal_rate;
		if (target_freq)
			*freq = target_freq;
		goto next;
	}

	if (!upthreshold || !downdifferential)
		goto next;

	if (upthreshold > 100 ||
	    upthreshold < downdifferential)
		goto next;

	err = devfreq_update_stats(df);
	if (err)
		goto next;

	stat = &df->last_status;

	/* Assume MAX if it is going to be divided by zero */
	if (stat->total_time == 0) {
		*freq = max_freq;
		return 0;
	}

	/* Prevent overflow */
	if (stat->busy_time >= (1 << 24) || stat->total_time >= (1 << 24)) {
		stat->busy_time >>= 7;
		stat->total_time >>= 7;
	}

	/* Set MAX if it's busy enough */
	if (stat->busy_time * 100 >
	    stat->total_time * upthreshold) {
		*freq = max_freq;
		return 0;
	}

	/* Set MAX if we do not know the initial frequency */
	if (stat->current_frequency == 0) {
		*freq = max_freq;
		return 0;
	}

	/* Keep the current frequency */
	if (stat->busy_time * 100 >
	    stat->total_time * (upthreshold - downdifferential)) {
		*freq = max(target_freq, stat->current_frequency);
		goto next;
	}

	/* Set the desired frequency based on the load */
	a = stat->busy_time;
	a *= stat->current_frequency;
	b = div_u64(a, stat->total_time);
	b *= 100;
	b = div_u64(b, (upthreshold - downdifferential / 2));
	*freq = max_t(unsigned long, target_freq, b);

next:
	if (df->min_freq && *freq < df->min_freq)
		*freq = df->min_freq;
	if (df->max_freq && *freq > df->max_freq)
		*freq = df->max_freq;

	return 0;
}

static int devfreq_dmc_ondemand_handler(struct devfreq *devfreq,
					unsigned int event, void *data)
{
	struct rockchip_dmcfreq *dmcfreq = dev_get_drvdata(devfreq->dev.parent);

	switch (event) {
	case DEVFREQ_GOV_START:
		if (!devfreq->data)
			devfreq->data = &dmcfreq->ondemand_data;
		devfreq_monitor_start(devfreq);
		break;

	case DEVFREQ_GOV_STOP:
		devfreq_monitor_stop(devfreq);
		break;

	case DEVFREQ_GOV_INTERVAL:
		devfreq_interval_update(devfreq, (unsigned int *)data);
		break;

	case DEVFREQ_GOV_SUSPEND:
		devfreq_monitor_suspend(devfreq);
		break;

	case DEVFREQ_GOV_RESUME:
		devfreq_monitor_resume(devfreq);
		break;

	default:
		break;
	}

	return 0;
}

static struct devfreq_governor devfreq_dmc_ondemand = {
	.name = "dmc_ondemand",
	.get_target_freq = devfreq_dmc_ondemand_func,
	.event_handler = devfreq_dmc_ondemand_handler,
};

static int rockchip_dmcfreq_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct rockchip_dmcfreq *data;
	struct devfreq_dev_profile *devp = &rockchip_devfreq_dmc_profile;
	const struct of_device_id *match;
	int (*init)(struct platform_device *pdev,
		    struct rockchip_dmcfreq *data);
	int ret;

	data = devm_kzalloc(dev, sizeof(struct rockchip_dmcfreq), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	mutex_init(&data->lock);
	INIT_LIST_HEAD(&data->video_info_list);

	data->vdd_center = devm_regulator_get_optional(dev, "center");
	if (IS_ERR(data->vdd_center)) {
		dev_err(dev, "Cannot get the regulator \"center\"\n");
		return PTR_ERR(data->vdd_center);
	}

	data->dmc_clk = devm_clk_get(dev, "dmc_clk");
	if (IS_ERR(data->dmc_clk)) {
		dev_err(dev, "Cannot get the clk dmc_clk\n");
		return PTR_ERR(data->dmc_clk);
	}

	data->edev = devfreq_event_get_edev_by_phandle(dev, 0);
	if (IS_ERR(data->edev))
		return -EPROBE_DEFER;

	ret = devfreq_event_enable_edev(data->edev);
	if (ret < 0) {
		dev_err(dev, "failed to enable devfreq-event devices\n");
		return ret;
	}

	match = of_match_node(rockchip_dmcfreq_of_match, pdev->dev.of_node);
	if (match) {
		init = match->data;
		if (init) {
			ret = init(pdev, data);
			if (ret)
				return ret;
		}
	}

	/*
	 * We add a devfreq driver to our parent since it has a device tree node
	 * with operating points.
	 */
	if (dev_pm_opp_of_add_table(dev)) {
		dev_err(dev, "Invalid operating-points in device tree.\n");
		return -EINVAL;
	}

	if (rockchip_dmcfreq_init_freq_table(dev, devp))
		return -EFAULT;

	of_property_read_u32(np, "upthreshold",
			     &data->ondemand_data.upthreshold);
	of_property_read_u32(np, "downdifferential",
			     &data->ondemand_data.downdifferential);
	of_property_read_u32(np, "min-cpu-freq", &data->min_cpu_freq);
	if (rockchip_get_system_status_rate(np, "system-status-freq", data))
		dev_err(dev, "failed to get system status rate\n");
	of_property_read_u32(np, "auto-freq-en", &data->auto_freq_en);
	of_property_read_u32(np, "auto-min-freq", (u32 *)&data->auto_min_rate);
	data->auto_min_rate *= 1000;
	if (rockchip_get_freq_map_talbe(np, "vop-bw-dmc-freq",
					&data->vop_bw_tbl))
		dev_err(dev, "failed to get vop bandwidth to dmc rate\n");

	data->rate = clk_get_rate(data->dmc_clk);
	data->volt = regulator_get_voltage(data->vdd_center);

	devp->initial_freq = data->rate;

	ret = devfreq_add_governor(&devfreq_dmc_ondemand);
	if (ret) {
		dev_err(dev, "Failed to add rockchip governor: %d\n", ret);
		return ret;
	}

	data->devfreq = devm_devfreq_add_device(dev, devp,
						"dmc_ondemand",
						&data->ondemand_data);
	if (IS_ERR(data->devfreq))
		return PTR_ERR(data->devfreq);

	devm_devfreq_register_opp_notifier(dev, data->devfreq);

	data->min = devp->freq_table[0];
	data->max = devp->freq_table[devp->max_state ? devp->max_state - 1 : 0];
	data->devfreq->min_freq = data->min;
	data->devfreq->max_freq = data->max;

	data->dev = dev;
	platform_set_drvdata(pdev, data);

	if (rockchip_drm_register_notifier_to_dmc(data->devfreq))
		dev_err(dev, "drm fail to register notifier to dmc\n");

	if (rockchip_pm_register_notify_to_dmc(data->devfreq))
		dev_err(dev, "pd fail to register notify to dmc\n");

	if (vop_register_dmc())
		dev_err(dev, "fail to register notify to vop.\n");

	data->system_status_nb.notifier_call =
		rockchip_dmcfreq_system_status_notifier;
	ret = rockchip_register_system_status_notifier(&data->system_status_nb);
	if (ret)
		dev_err(dev, "failed to register system_status nb\n");

	data->reboot_nb.notifier_call = rockchip_dmcfreq_reboot_notifier;
	ret = register_reboot_notifier(&data->reboot_nb);
	if (ret)
		dev_err(dev, "failed to register reboot nb\n");

	data->fb_nb.notifier_call = rockchip_dmcfreq_fb_notifier;
	ret = fb_register_client(&data->fb_nb);
	if (ret)
		dev_err(dev, "failed to register fb nb\n");

	ret = sysfs_create_file(&data->devfreq->dev.kobj,
				&dev_attr_system_status.attr);
	if (ret)
		dev_err(dev, "failed to register system_status sysfs file\n");

	rockchip_set_system_status(SYS_STATUS_NORMAL);

	rk_dmcfreq = data;

	return 0;
}

static struct platform_driver rockchip_dmcfreq_driver = {
	.probe	= rockchip_dmcfreq_probe,
	.driver = {
		.name	= "rockchip-dmc",
		.pm	= &rockchip_dmcfreq_pm,
		.of_match_table = rockchip_dmcfreq_of_match,
	},
};
module_platform_driver(rockchip_dmcfreq_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Lin Huang <hl@rock-chips.com>");
MODULE_DESCRIPTION("rockchip dmcfreq driver with devfreq framework");
