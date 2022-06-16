// SPDX-License-Identifier: GPL-2.0-only
/*
 * Rockchip Generic dmc support.
 *
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 * Author: Finley Xiao <finley.xiao@rock-chips.com>
 */

#include <dt-bindings/clock/rockchip-ddr.h>
#include <dt-bindings/soc/rockchip-system-status.h>
#include <drm/drm_modeset_lock.h>
#include <linux/arm-smccc.h>
#include <linux/clk.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/devfreq.h>
#include <linux/devfreq_cooling.h>
#include <linux/devfreq-event.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/pm_qos.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/rockchip/rockchip_sip.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/suspend.h>
#include <linux/thermal.h>

#include <soc/rockchip/pm_domains.h>
#include <soc/rockchip/rkfb_dmc.h>
#include <soc/rockchip/rockchip_dmc.h>
#include <soc/rockchip/rockchip_sip.h>
#include <soc/rockchip/rockchip_system_monitor.h>
#include <soc/rockchip/rockchip-system-status.h>
#include <soc/rockchip/rockchip_opp_select.h>
#include <soc/rockchip/scpi.h>
#include <uapi/drm/drm_mode.h>

#include "governor.h"
#include "rockchip_dmc_timing.h"
#include "../clk/rockchip/clk.h"
#include "../gpu/drm/rockchip/rockchip_drm_drv.h"
#include "../opp/opp.h"

#define system_status_to_dmcfreq(nb) container_of(nb, struct rockchip_dmcfreq, \
						  status_nb)
#define reboot_to_dmcfreq(nb) container_of(nb, struct rockchip_dmcfreq, \
					   reboot_nb)
#define boost_to_dmcfreq(work) container_of(work, struct rockchip_dmcfreq, \
					    boost_work)
#define input_hd_to_dmcfreq(hd) container_of(hd, struct rockchip_dmcfreq, \
					     input_handler)

#define VIDEO_1080P_SIZE	(1920 * 1080)
#define FIQ_INIT_HANDLER	(0x1)
#define FIQ_CPU_TGT_BOOT	(0x0) /* to booting cpu */
#define FIQ_NUM_FOR_DCF		(143) /* NA irq map to fiq for dcf */
#define DTS_PAR_OFFSET		(4096)

#define FALLBACK_STATIC_TEMPERATURE 55000

struct dmc_freq_table {
	unsigned long freq;
	unsigned long volt;
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
	u32 complt_hwirq;
	u32 update_drv_odt_cfg;
	u32 update_deskew_cfg;

	u32 freq_count;
	u32 freq_info_mhz[6];
	 /* if need, add parameter after */
};

static struct share_params *ddr_psci_param;

struct rockchip_dmcfreq_ondemand_data {
	unsigned int upthreshold;
	unsigned int downdifferential;
};

struct rockchip_dmcfreq {
	struct device *dev;
	struct dmcfreq_common_info info;
	struct rockchip_dmcfreq_ondemand_data ondemand_data;
	struct clk *dmc_clk;
	struct devfreq_event_dev **edev;
	struct mutex lock; /* serializes access to video_info_list */
	struct dram_timing *timing;
	struct regulator *vdd_center;
	struct regulator *mem_reg;
	struct notifier_block status_nb;
	struct list_head video_info_list;
	struct freq_map_table *cpu_bw_tbl;
	struct work_struct boost_work;
	struct input_handler input_handler;
	struct monitor_dev_info *mdev_info;
	struct share_params *set_rate_params;

	unsigned long *nocp_bw;
	unsigned long rate;
	unsigned long volt, mem_volt;
	unsigned long sleep_volt, sleep_mem_volt;
	unsigned long auto_min_rate;
	unsigned long status_rate;
	unsigned long normal_rate;
	unsigned long video_1080p_rate;
	unsigned long video_4k_rate;
	unsigned long video_4k_10b_rate;
	unsigned long performance_rate;
	unsigned long hdmi_rate;
	unsigned long idle_rate;
	unsigned long suspend_rate;
	unsigned long reboot_rate;
	unsigned long boost_rate;
	unsigned long fixed_rate;
	unsigned long low_power_rate;

	unsigned long freq_count;
	unsigned long freq_info_rate[6];
	unsigned long rate_low;
	unsigned long rate_mid_low;
	unsigned long rate_mid_high;
	unsigned long rate_high;

	unsigned int min_cpu_freq;
	unsigned int system_status_en;
	unsigned int refresh;
	int edev_count;
	int dfi_id;
	int nocp_cpu_id;
	int regulator_count;

	bool is_fixed;
	bool is_set_rate_direct;

	struct thermal_cooling_device *devfreq_cooling;
	u32 static_coefficient;
	s32 ts[4];
	struct thermal_zone_device *ddr_tz;

	unsigned int touchboostpulse_duration_val;
	u64 touchboostpulse_endtime;

	int (*set_auto_self_refresh)(u32 en);
};

static struct pm_qos_request pm_qos;

static int rockchip_dmcfreq_opp_helper(struct dev_pm_set_opp_data *data);

static struct monitor_dev_profile dmc_mdevp = {
	.type = MONITOR_TPYE_DEV,
	.low_temp_adjust = rockchip_monitor_dev_low_temp_adjust,
	.high_temp_adjust = rockchip_monitor_dev_high_temp_adjust,
	.update_volt = rockchip_monitor_check_rate_volt,
	.set_opp = rockchip_dmcfreq_opp_helper,
};

static inline unsigned long is_dualview(unsigned long status)
{
	return (status & SYS_STATUS_LCDC0) && (status & SYS_STATUS_LCDC1);
}

static inline unsigned long is_isp(unsigned long status)
{
	return (status & SYS_STATUS_ISP) ||
	       (status & SYS_STATUS_CIF0) ||
	       (status & SYS_STATUS_CIF1);
}

/*
 * function: packaging de-skew setting to px30_ddr_dts_config_timing,
 *           px30_ddr_dts_config_timing will pass to trust firmware, and
 *           used direct to set register.
 * input: de_skew
 * output: tim
 */
static void px30_de_skew_set_2_reg(struct rk3328_ddr_de_skew_setting *de_skew,
				   struct px30_ddr_dts_config_timing *tim)
{
	u32 n;
	u32 offset;
	u32 shift;

	memset_io(tim->ca_skew, 0, sizeof(tim->ca_skew));
	memset_io(tim->cs0_skew, 0, sizeof(tim->cs0_skew));
	memset_io(tim->cs1_skew, 0, sizeof(tim->cs1_skew));

	/* CA de-skew */
	for (n = 0; n < ARRAY_SIZE(de_skew->ca_de_skew); n++) {
		offset = n / 2;
		shift = n % 2;
		/* 0 => 4; 1 => 0 */
		shift = (shift == 0) ? 4 : 0;
		tim->ca_skew[offset] &= ~(0xf << shift);
		tim->ca_skew[offset] |= (de_skew->ca_de_skew[n] << shift);
	}

	/* CS0 data de-skew */
	for (n = 0; n < ARRAY_SIZE(de_skew->cs0_de_skew); n++) {
		offset = ((n / 21) * 11) + ((n % 21) / 2);
		shift = ((n % 21) % 2);
		if ((n % 21) == 20)
			shift = 0;
		else
			/* 0 => 4; 1 => 0 */
			shift = (shift == 0) ? 4 : 0;
		tim->cs0_skew[offset] &= ~(0xf << shift);
		tim->cs0_skew[offset] |= (de_skew->cs0_de_skew[n] << shift);
	}

	/* CS1 data de-skew */
	for (n = 0; n < ARRAY_SIZE(de_skew->cs1_de_skew); n++) {
		offset = ((n / 21) * 11) + ((n % 21) / 2);
		shift = ((n % 21) % 2);
		if ((n % 21) == 20)
			shift = 0;
		else
			/* 0 => 4; 1 => 0 */
			shift = (shift == 0) ? 4 : 0;
		tim->cs1_skew[offset] &= ~(0xf << shift);
		tim->cs1_skew[offset] |= (de_skew->cs1_de_skew[n] << shift);
	}
}

/*
 * function: packaging de-skew setting to rk3328_ddr_dts_config_timing,
 *           rk3328_ddr_dts_config_timing will pass to trust firmware, and
 *           used direct to set register.
 * input: de_skew
 * output: tim
 */
static void
rk3328_de_skew_setting_2_register(struct rk3328_ddr_de_skew_setting *de_skew,
				  struct rk3328_ddr_dts_config_timing *tim)
{
	u32 n;
	u32 offset;
	u32 shift;

	memset_io(tim->ca_skew, 0, sizeof(tim->ca_skew));
	memset_io(tim->cs0_skew, 0, sizeof(tim->cs0_skew));
	memset_io(tim->cs1_skew, 0, sizeof(tim->cs1_skew));

	/* CA de-skew */
	for (n = 0; n < ARRAY_SIZE(de_skew->ca_de_skew); n++) {
		offset = n / 2;
		shift = n % 2;
		/* 0 => 4; 1 => 0 */
		shift = (shift == 0) ? 4 : 0;
		tim->ca_skew[offset] &= ~(0xf << shift);
		tim->ca_skew[offset] |= (de_skew->ca_de_skew[n] << shift);
	}

	/* CS0 data de-skew */
	for (n = 0; n < ARRAY_SIZE(de_skew->cs0_de_skew); n++) {
		offset = ((n / 21) * 11) + ((n % 21) / 2);
		shift = ((n % 21) % 2);
		if ((n % 21) == 20)
			shift = 0;
		else
			/* 0 => 4; 1 => 0 */
			shift = (shift == 0) ? 4 : 0;
		tim->cs0_skew[offset] &= ~(0xf << shift);
		tim->cs0_skew[offset] |= (de_skew->cs0_de_skew[n] << shift);
	}

	/* CS1 data de-skew */
	for (n = 0; n < ARRAY_SIZE(de_skew->cs1_de_skew); n++) {
		offset = ((n / 21) * 11) + ((n % 21) / 2);
		shift = ((n % 21) % 2);
		if ((n % 21) == 20)
			shift = 0;
		else
			/* 0 => 4; 1 => 0 */
			shift = (shift == 0) ? 4 : 0;
		tim->cs1_skew[offset] &= ~(0xf << shift);
		tim->cs1_skew[offset] |= (de_skew->cs1_de_skew[n] << shift);
	}
}

static int rk_drm_get_lcdc_type(void)
{
	u32 lcdc_type = rockchip_drm_get_sub_dev_type();

	switch (lcdc_type) {
	case DRM_MODE_CONNECTOR_DPI:
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

static int rockchip_ddr_set_rate(unsigned long target_rate)
{
	struct arm_smccc_res res;

	ddr_psci_param->hz = target_rate;
	ddr_psci_param->lcdc_type = rk_drm_get_lcdc_type();
	ddr_psci_param->wait_flag1 = 1;
	ddr_psci_param->wait_flag0 = 1;

	res = sip_smc_dram(SHARE_PAGE_TYPE_DDR, 0,
			   ROCKCHIP_SIP_CONFIG_DRAM_SET_RATE);

	if ((int)res.a1 == SIP_RET_SET_RATE_TIMEOUT)
		rockchip_dmcfreq_wait_complete();

	return res.a0;
}

static int rockchip_dmcfreq_set_volt(struct device *dev, struct regulator *reg,
				     struct dev_pm_opp_supply *supply,
				     char *reg_name)
{
	int ret;

	dev_dbg(dev, "%s: %s voltages (mV): %lu %lu %lu\n", __func__, reg_name,
		supply->u_volt_min, supply->u_volt, supply->u_volt_max);
	ret = regulator_set_voltage_triplet(reg, supply->u_volt_min,
					    supply->u_volt, INT_MAX);
	if (ret)
		dev_err(dev, "%s: failed to set voltage (%lu %lu %lu mV): %d\n",
			__func__, supply->u_volt_min, supply->u_volt,
			supply->u_volt_max, ret);

	return ret;
}

static int rockchip_dmcfreq_opp_helper(struct dev_pm_set_opp_data *data)
{
	struct dev_pm_opp_supply *old_supply_vdd = &data->old_opp.supplies[0];
	struct dev_pm_opp_supply *new_supply_vdd = &data->new_opp.supplies[0];
	struct regulator *vdd_reg = data->regulators[0];
	struct dev_pm_opp_supply *old_supply_mem;
	struct dev_pm_opp_supply *new_supply_mem;
	struct regulator *mem_reg;
	struct device *dev = data->dev;
	struct clk *clk = data->clk;
	struct rockchip_dmcfreq *dmcfreq = dev_get_drvdata(dev);
	struct cpufreq_policy *policy;
	unsigned long old_freq = data->old_opp.rate;
	unsigned long freq = data->new_opp.rate;
	unsigned int reg_count = data->regulator_count;
	bool is_cpufreq_changed = false;
	unsigned int cpu_cur, cpufreq_cur;
	int ret = 0;

	if (reg_count > 1) {
		old_supply_mem = &data->old_opp.supplies[1];
		new_supply_mem = &data->new_opp.supplies[1];
		mem_reg = data->regulators[1];
	}

	/*
	 * We need to prevent cpu hotplug from happening while a dmc freq rate
	 * change is happening.
	 *
	 * Do this before taking the policy rwsem to avoid deadlocks between the
	 * mutex that is locked/unlocked in cpu_hotplug_disable/enable. And it
	 * can also avoid deadlocks between the mutex that is locked/unlocked
	 * in cpus_read_lock/unlock (such as store_scaling_max_freq()).
	 */
	cpus_read_lock();

	if (dmcfreq->min_cpu_freq) {
		/*
		 * Go to specified cpufreq and block other cpufreq changes since
		 * set_rate needs to complete during vblank.
		 */
		cpu_cur = raw_smp_processor_id();
		policy = cpufreq_cpu_get(cpu_cur);
		if (!policy) {
			dev_err(dev, "cpu%d policy NULL\n", cpu_cur);
			ret = -EINVAL;
			goto cpufreq;
		}
		down_write(&policy->rwsem);
		cpufreq_cur = cpufreq_quick_get(cpu_cur);

		/* If we're thermally throttled; don't change; */
		if (cpufreq_cur < dmcfreq->min_cpu_freq) {
			if (policy->max >= dmcfreq->min_cpu_freq) {
				__cpufreq_driver_target(policy,
							dmcfreq->min_cpu_freq,
							CPUFREQ_RELATION_L);
				is_cpufreq_changed = true;
			} else {
				dev_dbg(dev,
					"CPU may too slow for DMC (%d MHz)\n",
					policy->max);
			}
		}
	}

	/* Scaling up? Scale voltage before frequency */
	if (freq >= old_freq) {
		if (reg_count > 1) {
			ret = rockchip_dmcfreq_set_volt(dev, mem_reg,
							new_supply_mem, "mem");
			if (ret)
				goto restore_voltage;
		}
		ret = rockchip_dmcfreq_set_volt(dev, vdd_reg, new_supply_vdd,
						"vdd");
		if (ret)
			goto restore_voltage;
		if (freq == old_freq)
			goto out;
	}

	/*
	 * Writer in rwsem may block readers even during its waiting in queue,
	 * and this may lead to a deadlock when the code path takes read sem
	 * twice (e.g. one in vop_lock() and another in rockchip_pmu_lock()).
	 * As a (suboptimal) workaround, let writer to spin until it gets the
	 * lock.
	 */
	while (!rockchip_dmcfreq_write_trylock())
		cond_resched();
	dev_dbg(dev, "%lu Hz --> %lu Hz\n", old_freq, freq);

	if (dmcfreq->set_rate_params) {
		dmcfreq->set_rate_params->lcdc_type = rk_drm_get_lcdc_type();
		dmcfreq->set_rate_params->wait_flag1 = 1;
		dmcfreq->set_rate_params->wait_flag0 = 1;
	}

	if (dmcfreq->is_set_rate_direct)
		ret = rockchip_ddr_set_rate(freq);
	else
		ret = clk_set_rate(clk, freq);

	rockchip_dmcfreq_write_unlock();
	if (ret) {
		dev_err(dev, "%s: failed to set clock rate: %d\n", __func__,
			ret);
		goto restore_voltage;
	}

	/*
	 * Check the dpll rate,
	 * There only two result we will get,
	 * 1. Ddr frequency scaling fail, we still get the old rate.
	 * 2. Ddr frequency scaling successful, we get the rate we set.
	 */
	dmcfreq->rate = clk_get_rate(clk);

	/* If get the incorrect rate, set voltage to old value. */
	if (dmcfreq->rate != freq) {
		dev_err(dev, "Get wrong frequency, Request %lu, Current %lu\n",
			freq, dmcfreq->rate);
		ret = -EINVAL;
		goto restore_voltage;
	}

	/* Scaling down? Scale voltage after frequency */
	if (freq < old_freq) {
		ret = rockchip_dmcfreq_set_volt(dev, vdd_reg, new_supply_vdd,
						"vdd");
		if (ret)
			goto restore_freq;
		if (reg_count > 1) {
			ret = rockchip_dmcfreq_set_volt(dev, mem_reg,
							new_supply_mem, "mem");
			if (ret)
				goto restore_freq;
		}
	}
	dmcfreq->volt = new_supply_vdd->u_volt;
	if (reg_count > 1)
		dmcfreq->mem_volt = new_supply_mem->u_volt;

	goto out;

restore_freq:
	if (dmcfreq->is_set_rate_direct)
		ret = rockchip_ddr_set_rate(freq);
	else
		ret = clk_set_rate(clk, freq);
	if (ret)
		dev_err(dev, "%s: failed to restore old-freq (%lu Hz)\n",
			__func__, old_freq);
restore_voltage:
	if (reg_count > 1 && old_supply_mem->u_volt)
		rockchip_dmcfreq_set_volt(dev, mem_reg, old_supply_mem, "mem");
	if (old_supply_vdd->u_volt)
		rockchip_dmcfreq_set_volt(dev, vdd_reg, old_supply_vdd, "vdd");
out:
	if (dmcfreq->min_cpu_freq) {
		if (is_cpufreq_changed)
			__cpufreq_driver_target(policy, cpufreq_cur,
						CPUFREQ_RELATION_L);
		up_write(&policy->rwsem);
		cpufreq_cpu_put(policy);
	}
cpufreq:
	cpus_read_unlock();

	return ret;
}

static int rockchip_dmcfreq_target(struct device *dev, unsigned long *freq,
				   u32 flags)
{
	struct rockchip_dmcfreq *dmcfreq = dev_get_drvdata(dev);
	struct devfreq *devfreq;
	struct dev_pm_opp *opp;
	int ret = 0;

	if (!dmc_mdevp.is_checked)
		return -EINVAL;

	opp = devfreq_recommended_opp(dev, freq, flags);
	if (IS_ERR(opp)) {
		dev_err(dev, "Failed to find opp for %lu Hz\n", *freq);
		return PTR_ERR(opp);
	}
	dev_pm_opp_put(opp);

	rockchip_monitor_volt_adjust_lock(dmcfreq->mdev_info);
	ret = dev_pm_opp_set_rate(dev, *freq);
	if (!ret) {
		if (dmcfreq->info.devfreq) {
			devfreq = dmcfreq->info.devfreq;
			devfreq->last_status.current_frequency = *freq;
		}
	}
	rockchip_monitor_volt_adjust_unlock(dmcfreq->mdev_info);

	return ret;
}

static int rockchip_dmcfreq_get_dev_status(struct device *dev,
					   struct devfreq_dev_status *stat)
{
	struct rockchip_dmcfreq *dmcfreq = dev_get_drvdata(dev);
	struct devfreq_event_data edata;
	int i, ret = 0;

	if (!dmcfreq->info.auto_freq_en)
		return -EINVAL;

	for (i = 0; i < dmcfreq->edev_count; i++) {
		ret = devfreq_event_get_event(dmcfreq->edev[i], &edata);
		if (ret < 0) {
			dev_err(dev, "failed to get event %s\n",
				dmcfreq->edev[i]->desc->name);
			return ret;
		}
		if (i == dmcfreq->dfi_id) {
			stat->busy_time = edata.load_count;
			stat->total_time = edata.total_count;
		} else {
			dmcfreq->nocp_bw[i] = edata.load_count;
		}
	}

	return 0;
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


static inline void reset_last_status(struct devfreq *devfreq)
{
	devfreq->last_status.total_time = 1;
	devfreq->last_status.busy_time = 1;
}

static void of_get_px30_timings(struct device *dev,
				struct device_node *np, uint32_t *timing)
{
	struct device_node *np_tim;
	u32 *p;
	struct px30_ddr_dts_config_timing *dts_timing;
	struct rk3328_ddr_de_skew_setting *de_skew;
	int ret = 0;
	u32 i;

	dts_timing =
		(struct px30_ddr_dts_config_timing *)(timing +
							DTS_PAR_OFFSET / 4);

	np_tim = of_parse_phandle(np, "ddr_timing", 0);
	if (!np_tim) {
		ret = -EINVAL;
		goto end;
	}
	de_skew = kmalloc(sizeof(*de_skew), GFP_KERNEL);
	if (!de_skew) {
		ret = -ENOMEM;
		goto end;
	}
	p = (u32 *)dts_timing;
	for (i = 0; i < ARRAY_SIZE(px30_dts_timing); i++) {
		ret |= of_property_read_u32(np_tim, px30_dts_timing[i],
					p + i);
	}
	p = (u32 *)de_skew->ca_de_skew;
	for (i = 0; i < ARRAY_SIZE(rk3328_dts_ca_timing); i++) {
		ret |= of_property_read_u32(np_tim, rk3328_dts_ca_timing[i],
					p + i);
	}
	p = (u32 *)de_skew->cs0_de_skew;
	for (i = 0; i < ARRAY_SIZE(rk3328_dts_cs0_timing); i++) {
		ret |= of_property_read_u32(np_tim, rk3328_dts_cs0_timing[i],
					p + i);
	}
	p = (u32 *)de_skew->cs1_de_skew;
	for (i = 0; i < ARRAY_SIZE(rk3328_dts_cs1_timing); i++) {
		ret |= of_property_read_u32(np_tim, rk3328_dts_cs1_timing[i],
					p + i);
	}
	if (!ret)
		px30_de_skew_set_2_reg(de_skew, dts_timing);
	kfree(de_skew);
end:
	if (!ret) {
		dts_timing->available = 1;
	} else {
		dts_timing->available = 0;
		dev_err(dev, "of_get_ddr_timings: fail\n");
	}

	of_node_put(np_tim);
}

static void of_get_rk1808_timings(struct device *dev,
				  struct device_node *np, uint32_t *timing)
{
	struct device_node *np_tim;
	u32 *p;
	struct rk1808_ddr_dts_config_timing *dts_timing;
	int ret = 0;
	u32 i;

	dts_timing =
		(struct rk1808_ddr_dts_config_timing *)(timing +
							DTS_PAR_OFFSET / 4);

	np_tim = of_parse_phandle(np, "ddr_timing", 0);
	if (!np_tim) {
		ret = -EINVAL;
		goto end;
	}

	p = (u32 *)dts_timing;
	for (i = 0; i < ARRAY_SIZE(px30_dts_timing); i++) {
		ret |= of_property_read_u32(np_tim, px30_dts_timing[i],
					p + i);
	}
	p = (u32 *)dts_timing->ca_de_skew;
	for (i = 0; i < ARRAY_SIZE(rk1808_dts_ca_timing); i++) {
		ret |= of_property_read_u32(np_tim, rk1808_dts_ca_timing[i],
					p + i);
	}
	p = (u32 *)dts_timing->cs0_a_de_skew;
	for (i = 0; i < ARRAY_SIZE(rk1808_dts_cs0_a_timing); i++) {
		ret |= of_property_read_u32(np_tim, rk1808_dts_cs0_a_timing[i],
					p + i);
	}
	p = (u32 *)dts_timing->cs0_b_de_skew;
	for (i = 0; i < ARRAY_SIZE(rk1808_dts_cs0_b_timing); i++) {
		ret |= of_property_read_u32(np_tim, rk1808_dts_cs0_b_timing[i],
					p + i);
	}
	p = (u32 *)dts_timing->cs1_a_de_skew;
	for (i = 0; i < ARRAY_SIZE(rk1808_dts_cs1_a_timing); i++) {
		ret |= of_property_read_u32(np_tim, rk1808_dts_cs1_a_timing[i],
					p + i);
	}
	p = (u32 *)dts_timing->cs1_b_de_skew;
	for (i = 0; i < ARRAY_SIZE(rk1808_dts_cs1_b_timing); i++) {
		ret |= of_property_read_u32(np_tim, rk1808_dts_cs1_b_timing[i],
					p + i);
	}

end:
	if (!ret) {
		dts_timing->available = 1;
	} else {
		dts_timing->available = 0;
		dev_err(dev, "of_get_ddr_timings: fail\n");
	}

	of_node_put(np_tim);
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

static uint32_t of_get_rk3228_timings(struct device *dev,
				      struct device_node *np, uint32_t *timing)
{
	struct device_node *np_tim;
	u32 *p;
	int ret = 0;
	u32 i;

	p = timing + DTS_PAR_OFFSET / 4;
	np_tim = of_parse_phandle(np, "rockchip,dram_timing", 0);
	if (!np_tim) {
		ret = -EINVAL;
		goto end;
	}
	for (i = 0; i < ARRAY_SIZE(rk3228_dts_timing); i++) {
		ret |= of_property_read_u32(np_tim, rk3228_dts_timing[i],
					p + i);
	}
end:
	if (ret)
		dev_err(dev, "of_get_ddr_timings: fail\n");

	of_node_put(np_tim);
	return ret;
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

static void of_get_rk3328_timings(struct device *dev,
				  struct device_node *np, uint32_t *timing)
{
	struct device_node *np_tim;
	u32 *p;
	struct rk3328_ddr_dts_config_timing *dts_timing;
	struct rk3328_ddr_de_skew_setting *de_skew;
	int ret = 0;
	u32 i;

	dts_timing =
		(struct rk3328_ddr_dts_config_timing *)(timing +
							DTS_PAR_OFFSET / 4);

	np_tim = of_parse_phandle(np, "ddr_timing", 0);
	if (!np_tim) {
		ret = -EINVAL;
		goto end;
	}
	de_skew = kmalloc(sizeof(*de_skew), GFP_KERNEL);
	if (!de_skew) {
		ret = -ENOMEM;
		goto end;
	}
	p = (u32 *)dts_timing;
	for (i = 0; i < ARRAY_SIZE(rk3328_dts_timing); i++) {
		ret |= of_property_read_u32(np_tim, rk3328_dts_timing[i],
					p + i);
	}
	p = (u32 *)de_skew->ca_de_skew;
	for (i = 0; i < ARRAY_SIZE(rk3328_dts_ca_timing); i++) {
		ret |= of_property_read_u32(np_tim, rk3328_dts_ca_timing[i],
					p + i);
	}
	p = (u32 *)de_skew->cs0_de_skew;
	for (i = 0; i < ARRAY_SIZE(rk3328_dts_cs0_timing); i++) {
		ret |= of_property_read_u32(np_tim, rk3328_dts_cs0_timing[i],
					p + i);
	}
	p = (u32 *)de_skew->cs1_de_skew;
	for (i = 0; i < ARRAY_SIZE(rk3328_dts_cs1_timing); i++) {
		ret |= of_property_read_u32(np_tim, rk3328_dts_cs1_timing[i],
					p + i);
	}
	if (!ret)
		rk3328_de_skew_setting_2_register(de_skew, dts_timing);
	kfree(de_skew);
end:
	if (!ret) {
		dts_timing->available = 1;
	} else {
		dts_timing->available = 0;
		dev_err(dev, "of_get_ddr_timings: fail\n");
	}

	of_node_put(np_tim);
}

static void of_get_rv1126_timings(struct device *dev,
				  struct device_node *np, uint32_t *timing)
{
	struct device_node *np_tim;
	u32 *p;
	struct rk1808_ddr_dts_config_timing *dts_timing;
	int ret = 0;
	u32 i;

	dts_timing =
		(struct rk1808_ddr_dts_config_timing *)(timing +
							DTS_PAR_OFFSET / 4);

	np_tim = of_parse_phandle(np, "ddr_timing", 0);
	if (!np_tim) {
		ret = -EINVAL;
		goto end;
	}

	p = (u32 *)dts_timing;
	for (i = 0; i < ARRAY_SIZE(px30_dts_timing); i++) {
		ret |= of_property_read_u32(np_tim, px30_dts_timing[i],
					p + i);
	}
	p = (u32 *)dts_timing->ca_de_skew;
	for (i = 0; i < ARRAY_SIZE(rv1126_dts_ca_timing); i++) {
		ret |= of_property_read_u32(np_tim, rv1126_dts_ca_timing[i],
					p + i);
	}
	p = (u32 *)dts_timing->cs0_a_de_skew;
	for (i = 0; i < ARRAY_SIZE(rv1126_dts_cs0_a_timing); i++) {
		ret |= of_property_read_u32(np_tim, rv1126_dts_cs0_a_timing[i],
					p + i);
	}
	p = (u32 *)dts_timing->cs0_b_de_skew;
	for (i = 0; i < ARRAY_SIZE(rv1126_dts_cs0_b_timing); i++) {
		ret |= of_property_read_u32(np_tim, rv1126_dts_cs0_b_timing[i],
					p + i);
	}
	p = (u32 *)dts_timing->cs1_a_de_skew;
	for (i = 0; i < ARRAY_SIZE(rv1126_dts_cs1_a_timing); i++) {
		ret |= of_property_read_u32(np_tim, rv1126_dts_cs1_a_timing[i],
					p + i);
	}
	p = (u32 *)dts_timing->cs1_b_de_skew;
	for (i = 0; i < ARRAY_SIZE(rv1126_dts_cs1_b_timing); i++) {
		ret |= of_property_read_u32(np_tim, rv1126_dts_cs1_b_timing[i],
					p + i);
	}

end:
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
		ret |= of_property_read_u32(np_tim, "ddr_2t",
					    &timing->ddr_2t);
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

static int rockchip_ddr_set_auto_self_refresh(uint32_t en)
{
	struct arm_smccc_res res;

	ddr_psci_param->sr_idle_en = en;
	res = sip_smc_dram(SHARE_PAGE_TYPE_DDR, 0,
			   ROCKCHIP_SIP_CONFIG_DRAM_SET_AT_SR);

	return res.a0;
}

struct dmcfreq_wait_ctrl_t {
	wait_queue_head_t wait_wq;
	int complt_irq;
	int wait_flag;
	int wait_en;
	int wait_time_out_ms;
	int dcf_en;
	struct regmap *regmap_dcf;
};

static struct dmcfreq_wait_ctrl_t wait_ctrl;

static irqreturn_t wait_complete_irq(int irqno, void *dev_id)
{
	struct dmcfreq_wait_ctrl_t *ctrl = dev_id;

	ctrl->wait_flag = 0;
	wake_up(&ctrl->wait_wq);
	return IRQ_HANDLED;
}

static irqreturn_t wait_dcf_complete_irq(int irqno, void *dev_id)
{
	struct arm_smccc_res res;
	struct dmcfreq_wait_ctrl_t *ctrl = dev_id;

	res = sip_smc_dram(SHARE_PAGE_TYPE_DDR, 0,
			   ROCKCHIP_SIP_CONFIG_DRAM_POST_SET_RATE);
	if (res.a0)
		pr_err("%s: dram post set rate error:%lx\n", __func__, res.a0);

	ctrl->wait_flag = 0;
	wake_up(&ctrl->wait_wq);
	return IRQ_HANDLED;
}

int rockchip_dmcfreq_wait_complete(void)
{
	struct arm_smccc_res res;

	if (!wait_ctrl.wait_en) {
		pr_err("%s: Do not support time out!\n", __func__);
		return 0;
	}
	wait_ctrl.wait_flag = -1;

	enable_irq(wait_ctrl.complt_irq);
	/*
	 * CPUs only enter WFI when idle to make sure that
	 * FIQn can quick response.
	 */
	cpu_latency_qos_update_request(&pm_qos, 0);

	if (wait_ctrl.dcf_en == 1) {
		/* start dcf */
		regmap_update_bits(wait_ctrl.regmap_dcf, 0x0, 0x1, 0x1);
	} else if (wait_ctrl.dcf_en == 2) {
		res = sip_smc_dram(0, 0, ROCKCHIP_SIP_CONFIG_MCU_START);
		if (res.a0) {
			pr_err("rockchip_sip_config_mcu_start error:%lx\n", res.a0);
			return -ENOMEM;
		}
	}

	wait_event_timeout(wait_ctrl.wait_wq, (wait_ctrl.wait_flag == 0),
			   msecs_to_jiffies(wait_ctrl.wait_time_out_ms));

	/*
	 * If waiting for wait_ctrl.complt_irq times out, clear the IRQ and stop the MCU by
	 * sip_smc_dram(DRAM_POST_SET_RATE).
	 */
	if (wait_ctrl.dcf_en == 2 && wait_ctrl.wait_flag != 0) {
		res = sip_smc_dram(SHARE_PAGE_TYPE_DDR, 0, ROCKCHIP_SIP_CONFIG_DRAM_POST_SET_RATE);
		if (res.a0)
			pr_err("%s: dram post set rate error:%lx\n", __func__, res.a0);
	}

	cpu_latency_qos_update_request(&pm_qos, PM_QOS_DEFAULT_VALUE);
	disable_irq(wait_ctrl.complt_irq);

	return 0;
}

static __maybe_unused int rockchip_get_freq_info(struct rockchip_dmcfreq *dmcfreq)
{
	struct arm_smccc_res res;
	struct dev_pm_opp *opp;
	struct dmc_freq_table *freq_table;
	unsigned long rate;
	int i, j, count, ret = 0;

	res = sip_smc_dram(SHARE_PAGE_TYPE_DDR, 0,
			   ROCKCHIP_SIP_CONFIG_DRAM_GET_FREQ_INFO);
	if (res.a0) {
		dev_err(dmcfreq->dev, "rockchip_sip_config_dram_get_freq_info error:%lx\n",
			res.a0);
		return -ENOMEM;
	}

	if (ddr_psci_param->freq_count == 0 || ddr_psci_param->freq_count > 6) {
		dev_err(dmcfreq->dev, "it is no available frequencies!\n");
		return -EPERM;
	}

	for (i = 0; i < ddr_psci_param->freq_count; i++)
		dmcfreq->freq_info_rate[i] = ddr_psci_param->freq_info_mhz[i] * 1000000;
	dmcfreq->freq_count = ddr_psci_param->freq_count;

	/* update dmc_opp_table */
	count = dev_pm_opp_get_opp_count(dmcfreq->dev);
	if (count <= 0) {
		ret = count ? count : -ENODATA;
		return ret;
	}

	freq_table = kmalloc(sizeof(struct dmc_freq_table) * count, GFP_KERNEL);
	for (i = 0, rate = 0; i < count; i++, rate++) {
		/* find next rate */
		opp = dev_pm_opp_find_freq_ceil(dmcfreq->dev, &rate);
		if (IS_ERR(opp)) {
			ret = PTR_ERR(opp);
			dev_err(dmcfreq->dev, "failed to find OPP for freq %lu.\n", rate);
			goto out;
		}
		freq_table[i].freq = rate;
		freq_table[i].volt = dev_pm_opp_get_voltage(opp);
		dev_pm_opp_put(opp);

		for (j = 0; j < dmcfreq->freq_count; j++) {
			if (rate == dmcfreq->freq_info_rate[j])
				break;
		}
		if (j == dmcfreq->freq_count)
			dev_pm_opp_disable(dmcfreq->dev, rate);
	}

	for (i = 0; i < dmcfreq->freq_count; i++) {
		for (j = 0; j < count; j++) {
			if (dmcfreq->freq_info_rate[i] == freq_table[j].freq) {
				break;
			} else if (dmcfreq->freq_info_rate[i] < freq_table[j].freq) {
				dev_pm_opp_add(dmcfreq->dev, dmcfreq->freq_info_rate[i],
					       freq_table[j].volt);
				break;
			}
		}
		if (j == count) {
			dev_err(dmcfreq->dev, "failed to match dmc_opp_table for %ld\n",
				dmcfreq->freq_info_rate[i]);
			if (i == 0)
				ret = -EPERM;
			else
				dmcfreq->freq_count = i;
			goto out;
		}
	}

out:
	kfree(freq_table);
	return ret;
}

static __maybe_unused int
rockchip_dmcfreq_adjust_opp_table(struct rockchip_dmcfreq *dmcfreq)
{
	struct device *dev = dmcfreq->dev;
	struct arm_smccc_res res;
	struct dev_pm_opp *opp;
	struct opp_table *opp_table;
	unsigned long target_rate = 0, last_rate = 0;
	int i, count = 0;

	res = sip_smc_dram(SHARE_PAGE_TYPE_DDR, 0,
			   ROCKCHIP_SIP_CONFIG_DRAM_GET_FREQ_INFO);
	if (res.a0) {
		dev_err(dev, "rockchip_sip_config_dram_get_freq_info error:%lx\n",
			res.a0);
		return -ENOMEM;
	}

	if (ddr_psci_param->freq_count == 0 || ddr_psci_param->freq_count > 6) {
		dev_err(dev, "there is no available frequencies!\n");
		return -EPERM;
	}

	for (i = 0; i < ddr_psci_param->freq_count; i++)
		dmcfreq->freq_info_rate[i] = ddr_psci_param->freq_info_mhz[i] * 1000000;
	dmcfreq->freq_count = ddr_psci_param->freq_count;

	opp_table = dev_pm_opp_get_opp_table(dev);
	if (!opp_table)
		return -ENOMEM;

	mutex_lock(&opp_table->lock);
	list_for_each_entry(opp, &opp_table->opp_list, node) {
		if (!opp->available)
			continue;
		/* Search for a rounded floor frequency */
		target_rate = 0;
		for (i = 0; i < dmcfreq->freq_count; i++) {
			if (dmcfreq->freq_info_rate[i] <= opp->rate)
				target_rate = dmcfreq->freq_info_rate[i];
		}
		/* If not find, disable the opp */
		if (!target_rate) {
			opp->available = false;
		} else {
			/* If the opp rate is equal to last opp rate, disable it */
			if (target_rate == last_rate) {
				opp->available = false;
			} else {
				opp->rate = target_rate;
				last_rate = opp->rate;
				count++;
			}
		}
	}
	mutex_unlock(&opp_table->lock);
	dev_pm_opp_put_opp_table(opp_table);
	if (!count) {
		dev_err(dev, "there is no available opp\n");
		return -EINVAL;
	}

	return 0;
}

static __maybe_unused int px30_dmc_init(struct platform_device *pdev,
					struct rockchip_dmcfreq *dmcfreq)
{
	struct arm_smccc_res res;
	u32 size;
	int ret;
	int complt_irq;
	u32 complt_hwirq;
	struct irq_data *complt_irq_data;

	res = sip_smc_dram(0, 0,
			   ROCKCHIP_SIP_CONFIG_DRAM_GET_VERSION);
	dev_notice(&pdev->dev, "current ATF version 0x%lx!\n", res.a1);
	if (res.a0 || res.a1 < 0x103) {
		dev_err(&pdev->dev,
			"trusted firmware need to update or is invalid!\n");
		return -ENXIO;
	}

	dev_notice(&pdev->dev, "read tf version 0x%lx!\n", res.a1);

	/*
	 * first 4KB is used for interface parameters
	 * after 4KB * N is dts parameters
	 */
	size = sizeof(struct px30_ddr_dts_config_timing);
	res = sip_smc_request_share_mem(DIV_ROUND_UP(size, 4096) + 1,
					SHARE_PAGE_TYPE_DDR);
	if (res.a0 != 0) {
		dev_err(&pdev->dev, "no ATF memory for init\n");
		return -ENOMEM;
	}
	ddr_psci_param = (struct share_params *)res.a1;
	of_get_px30_timings(&pdev->dev, pdev->dev.of_node,
			    (uint32_t *)ddr_psci_param);

	init_waitqueue_head(&wait_ctrl.wait_wq);
	wait_ctrl.wait_en = 1;
	wait_ctrl.wait_time_out_ms = 17 * 5;

	complt_irq = platform_get_irq_byname(pdev, "complete_irq");
	if (complt_irq < 0) {
		dev_err(&pdev->dev, "no IRQ for complete_irq: %d\n",
			complt_irq);
		return complt_irq;
	}
	wait_ctrl.complt_irq = complt_irq;

	ret = devm_request_irq(&pdev->dev, complt_irq, wait_complete_irq,
			       0, dev_name(&pdev->dev), &wait_ctrl);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot request complete_irq\n");
		return ret;
	}
	disable_irq(complt_irq);

	complt_irq_data = irq_get_irq_data(complt_irq);
	complt_hwirq = irqd_to_hwirq(complt_irq_data);
	ddr_psci_param->complt_hwirq = complt_hwirq;

	dmcfreq->set_rate_params = ddr_psci_param;
	rockchip_set_ddrclk_params(dmcfreq->set_rate_params);
	rockchip_set_ddrclk_dmcfreq_wait_complete(rockchip_dmcfreq_wait_complete);

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

static __maybe_unused int rk1808_dmc_init(struct platform_device *pdev,
					  struct rockchip_dmcfreq *dmcfreq)
{
	struct arm_smccc_res res;
	u32 size;
	int ret;
	int complt_irq;
	struct device_node *node;

	res = sip_smc_dram(0, 0,
			   ROCKCHIP_SIP_CONFIG_DRAM_GET_VERSION);
	dev_notice(&pdev->dev, "current ATF version 0x%lx!\n", res.a1);
	if (res.a0 || res.a1 < 0x101) {
		dev_err(&pdev->dev,
			"trusted firmware need to update or is invalid!\n");
		return -ENXIO;
	}

	/*
	 * first 4KB is used for interface parameters
	 * after 4KB * N is dts parameters
	 */
	size = sizeof(struct rk1808_ddr_dts_config_timing);
	res = sip_smc_request_share_mem(DIV_ROUND_UP(size, 4096) + 1,
					SHARE_PAGE_TYPE_DDR);
	if (res.a0 != 0) {
		dev_err(&pdev->dev, "no ATF memory for init\n");
		return -ENOMEM;
	}
	ddr_psci_param = (struct share_params *)res.a1;
	of_get_rk1808_timings(&pdev->dev, pdev->dev.of_node,
			      (uint32_t *)ddr_psci_param);

	/* enable start dcf in kernel after dcf ready */
	node = of_parse_phandle(pdev->dev.of_node, "dcf_reg", 0);
	wait_ctrl.regmap_dcf = syscon_node_to_regmap(node);
	if (IS_ERR(wait_ctrl.regmap_dcf))
		return PTR_ERR(wait_ctrl.regmap_dcf);
	wait_ctrl.dcf_en = 1;

	init_waitqueue_head(&wait_ctrl.wait_wq);
	wait_ctrl.wait_en = 1;
	wait_ctrl.wait_time_out_ms = 17 * 5;

	complt_irq = platform_get_irq_byname(pdev, "complete_irq");
	if (complt_irq < 0) {
		dev_err(&pdev->dev, "no IRQ for complete_irq: %d\n",
			complt_irq);
		return complt_irq;
	}
	wait_ctrl.complt_irq = complt_irq;

	ret = devm_request_irq(&pdev->dev, complt_irq, wait_dcf_complete_irq,
			       0, dev_name(&pdev->dev), &wait_ctrl);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot request complete_irq\n");
		return ret;
	}
	disable_irq(complt_irq);

	dmcfreq->set_rate_params = ddr_psci_param;
	rockchip_set_ddrclk_params(dmcfreq->set_rate_params);
	rockchip_set_ddrclk_dmcfreq_wait_complete(rockchip_dmcfreq_wait_complete);

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

static __maybe_unused int rk3128_dmc_init(struct platform_device *pdev,
					  struct rockchip_dmcfreq *dmcfreq)
{
	struct arm_smccc_res res;

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

	dmcfreq->set_rate_params = ddr_psci_param;
	rockchip_set_ddrclk_params(dmcfreq->set_rate_params);

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

static __maybe_unused int rk3228_dmc_init(struct platform_device *pdev,
					  struct rockchip_dmcfreq *dmcfreq)
{
	struct arm_smccc_res res;

	res = sip_smc_request_share_mem(DIV_ROUND_UP(sizeof(
					struct rk3228_ddr_dts_config_timing),
					4096) + 1, SHARE_PAGE_TYPE_DDR);
	if (res.a0) {
		dev_err(&pdev->dev, "no ATF memory for init\n");
		return -ENOMEM;
	}

	ddr_psci_param = (struct share_params *)res.a1;
	if (of_get_rk3228_timings(&pdev->dev, pdev->dev.of_node,
				  (uint32_t *)ddr_psci_param))
		return -ENOMEM;

	ddr_psci_param->hz = 0;

	dmcfreq->set_rate_params = ddr_psci_param;
	rockchip_set_ddrclk_params(dmcfreq->set_rate_params);

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

static __maybe_unused int rk3288_dmc_init(struct platform_device *pdev,
					  struct rockchip_dmcfreq *dmcfreq)
{
	struct device *dev = &pdev->dev;
	struct clk *pclk_phy, *pclk_upctl, *dmc_clk;
	struct arm_smccc_res res;
	int ret;

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

	dmcfreq->set_rate_params = ddr_psci_param;
	rockchip_set_ddrclk_params(dmcfreq->set_rate_params);

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

static __maybe_unused int rk3328_dmc_init(struct platform_device *pdev,
					  struct rockchip_dmcfreq *dmcfreq)
{
	struct arm_smccc_res res;
	u32 size;

	res = sip_smc_dram(0, 0,
			   ROCKCHIP_SIP_CONFIG_DRAM_GET_VERSION);
	dev_notice(&pdev->dev, "current ATF version 0x%lx!\n", res.a1);
	if (res.a0 || (res.a1 < 0x101)) {
		dev_err(&pdev->dev,
			"trusted firmware need to update or is invalid!\n");
		return -ENXIO;
	}

	dev_notice(&pdev->dev, "read tf version 0x%lx!\n", res.a1);

	/*
	 * first 4KB is used for interface parameters
	 * after 4KB * N is dts parameters
	 */
	size = sizeof(struct rk3328_ddr_dts_config_timing);
	res = sip_smc_request_share_mem(DIV_ROUND_UP(size, 4096) + 1,
					SHARE_PAGE_TYPE_DDR);
	if (res.a0 != 0) {
		dev_err(&pdev->dev, "no ATF memory for init\n");
		return -ENOMEM;
	}
	ddr_psci_param = (struct share_params *)res.a1;
	of_get_rk3328_timings(&pdev->dev, pdev->dev.of_node,
			      (uint32_t *)ddr_psci_param);

	dmcfreq->set_rate_params = ddr_psci_param;
	rockchip_set_ddrclk_params(dmcfreq->set_rate_params);

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

static __maybe_unused int rk3368_dmc_init(struct platform_device *pdev,
					  struct rockchip_dmcfreq *dmcfreq)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct arm_smccc_res res;
	struct rk3368_dram_timing *dram_timing;
	struct clk *pclk_phy, *pclk_upctl;
	int ret;
	u32 dram_spd_bin;
	u32 addr_mcu_el3;
	u32 dclk_mode;
	u32 lcdc_type;

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

	dmcfreq->set_rate_params =
		devm_kzalloc(dev, sizeof(struct share_params), GFP_KERNEL);
	if (!dmcfreq->set_rate_params)
		return -ENOMEM;
	rockchip_set_ddrclk_params(dmcfreq->set_rate_params);

	lcdc_type = rk_drm_get_lcdc_type();

	if (scpi_ddr_init(dram_spd_bin, 0, lcdc_type,
			  addr_mcu_el3))
		dev_err(dev, "ddr init error\n");
	else
		dev_dbg(dev, ("%s out\n"), __func__);

	dmcfreq->set_auto_self_refresh = scpi_ddr_set_auto_self_refresh;

	return 0;
}

static int rk3399_set_msch_readlatency(unsigned int readlatency)
{
	struct arm_smccc_res res;

	arm_smccc_smc(ROCKCHIP_SIP_DRAM_FREQ, readlatency, 0,
		      ROCKCHIP_SIP_CONFIG_DRAM_SET_MSCH_RL,
		      0, 0, 0, 0, &res);

	return res.a0;
}

static __maybe_unused int rk3399_dmc_init(struct platform_device *pdev,
					  struct rockchip_dmcfreq *dmcfreq)
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

	dmcfreq->set_rate_params =
		devm_kzalloc(dev, sizeof(struct share_params), GFP_KERNEL);
	if (!dmcfreq->set_rate_params)
		return -ENOMEM;
	rockchip_set_ddrclk_params(dmcfreq->set_rate_params);

	arm_smccc_smc(ROCKCHIP_SIP_DRAM_FREQ, 0, 0,
		      ROCKCHIP_SIP_CONFIG_DRAM_INIT,
		      0, 0, 0, 0, &res);

	dmcfreq->info.set_msch_readlatency = rk3399_set_msch_readlatency;

	return 0;
}

static __maybe_unused int rk3568_dmc_init(struct platform_device *pdev,
					  struct rockchip_dmcfreq *dmcfreq)
{
	struct arm_smccc_res res;
	int ret;
	int complt_irq;

	res = sip_smc_dram(0, 0,
			   ROCKCHIP_SIP_CONFIG_DRAM_GET_VERSION);
	dev_notice(&pdev->dev, "current ATF version 0x%lx\n", res.a1);
	if (res.a0 || res.a1 < 0x101) {
		dev_err(&pdev->dev, "trusted firmware need update to V1.01 and above.\n");
		return -ENXIO;
	}

	/*
	 * first 4KB is used for interface parameters
	 * after 4KB is dts parameters
	 * request share memory size 4KB * 2
	 */
	res = sip_smc_request_share_mem(2, SHARE_PAGE_TYPE_DDR);
	if (res.a0 != 0) {
		dev_err(&pdev->dev, "no ATF memory for init\n");
		return -ENOMEM;
	}
	ddr_psci_param = (struct share_params *)res.a1;
	/* Clear ddr_psci_param, size is 4KB * 2 */
	memset_io(ddr_psci_param, 0x0, 4096 * 2);

	/* start mcu with sip_smc_dram */
	wait_ctrl.dcf_en = 2;

	init_waitqueue_head(&wait_ctrl.wait_wq);
	wait_ctrl.wait_en = 1;
	wait_ctrl.wait_time_out_ms = 17 * 5;

	complt_irq = platform_get_irq_byname(pdev, "complete");
	if (complt_irq < 0) {
		dev_err(&pdev->dev, "no IRQ for complt_irq: %d\n",
			complt_irq);
		return complt_irq;
	}
	wait_ctrl.complt_irq = complt_irq;

	ret = devm_request_irq(&pdev->dev, complt_irq, wait_dcf_complete_irq,
			       0, dev_name(&pdev->dev), &wait_ctrl);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot request complt_irq\n");
		return ret;
	}
	disable_irq(complt_irq);

	res = sip_smc_dram(SHARE_PAGE_TYPE_DDR, 0,
			   ROCKCHIP_SIP_CONFIG_DRAM_INIT);
	if (res.a0) {
		dev_err(&pdev->dev, "rockchip_sip_config_dram_init error:%lx\n",
			res.a0);
		return -ENOMEM;
	}

	ret = rockchip_get_freq_info(dmcfreq);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot get frequency info\n");
		return ret;
	}
	dmcfreq->is_set_rate_direct = true;

	dmcfreq->set_auto_self_refresh = rockchip_ddr_set_auto_self_refresh;

	return 0;
}

static __maybe_unused int rk3588_dmc_init(struct platform_device *pdev,
					  struct rockchip_dmcfreq *dmcfreq)
{
	struct arm_smccc_res res;
	struct dev_pm_opp *opp;
	unsigned long opp_rate;
	int ret;
	int complt_irq;

	res = sip_smc_dram(0, 0, ROCKCHIP_SIP_CONFIG_DRAM_GET_VERSION);
	dev_notice(&pdev->dev, "current ATF version 0x%lx\n", res.a1);
	if (res.a0) {
		dev_err(&pdev->dev, "trusted firmware unsupported, please update.\n");
		return -ENXIO;
	}

	/*
	 * first 4KB is used for interface parameters
	 * after 4KB is dts parameters
	 * request share memory size 4KB * 2
	 */
	res = sip_smc_request_share_mem(2, SHARE_PAGE_TYPE_DDR);
	if (res.a0 != 0) {
		dev_err(&pdev->dev, "no ATF memory for init\n");
		return -ENOMEM;
	}
	ddr_psci_param = (struct share_params *)res.a1;
	/* Clear ddr_psci_param, size is 4KB * 2 */
	memset_io(ddr_psci_param, 0x0, 4096 * 2);

	/* start mcu with sip_smc_dram */
	wait_ctrl.dcf_en = 2;

	init_waitqueue_head(&wait_ctrl.wait_wq);
	wait_ctrl.wait_en = 1;
	wait_ctrl.wait_time_out_ms = 17 * 5;

	complt_irq = platform_get_irq_byname(pdev, "complete");
	if (complt_irq < 0) {
		dev_err(&pdev->dev, "no IRQ for complt_irq: %d\n", complt_irq);
		return complt_irq;
	}
	wait_ctrl.complt_irq = complt_irq;

	ret = devm_request_irq(&pdev->dev, complt_irq, wait_dcf_complete_irq,
			       0, dev_name(&pdev->dev), &wait_ctrl);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot request complt_irq\n");
		return ret;
	}
	disable_irq(complt_irq);

	res = sip_smc_dram(SHARE_PAGE_TYPE_DDR, 0, ROCKCHIP_SIP_CONFIG_DRAM_INIT);
	if (res.a0) {
		dev_err(&pdev->dev, "rockchip_sip_config_dram_init error:%lx\n", res.a0);
		return -ENOMEM;
	}

	ret = rockchip_dmcfreq_adjust_opp_table(dmcfreq);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot get frequency info\n");
		return ret;
	}
	dmcfreq->is_set_rate_direct = true;

	/* Config the dmcfreq->sleep_volt for deepsleep */
	opp_rate = dmcfreq->freq_info_rate[dmcfreq->freq_count - 1];
	opp = devfreq_recommended_opp(&pdev->dev, &opp_rate, 0);
	if (IS_ERR(opp)) {
		dev_err(&pdev->dev, "Failed to find opp for %lu Hz\n", opp_rate);
		return PTR_ERR(opp);
	}
	dmcfreq->sleep_volt = opp->supplies[0].u_volt;
	if (dmcfreq->regulator_count > 1)
		dmcfreq->sleep_mem_volt = opp->supplies[1].u_volt;
	dev_pm_opp_put(opp);

	dmcfreq->set_auto_self_refresh = rockchip_ddr_set_auto_self_refresh;

	return 0;
}

static __maybe_unused int rv1126_dmc_init(struct platform_device *pdev,
					  struct rockchip_dmcfreq *dmcfreq)
{
	struct arm_smccc_res res;
	u32 size;
	int ret;
	int complt_irq;
	struct device_node *node;

	res = sip_smc_dram(0, 0,
			   ROCKCHIP_SIP_CONFIG_DRAM_GET_VERSION);
	dev_notice(&pdev->dev, "current ATF version 0x%lx\n", res.a1);
	if (res.a0 || res.a1 < 0x100) {
		dev_err(&pdev->dev,
			"trusted firmware need to update or is invalid!\n");
		return -ENXIO;
	}

	/*
	 * first 4KB is used for interface parameters
	 * after 4KB * N is dts parameters
	 */
	size = sizeof(struct rk1808_ddr_dts_config_timing);
	res = sip_smc_request_share_mem(DIV_ROUND_UP(size, 4096) + 1,
					SHARE_PAGE_TYPE_DDR);
	if (res.a0 != 0) {
		dev_err(&pdev->dev, "no ATF memory for init\n");
		return -ENOMEM;
	}
	ddr_psci_param = (struct share_params *)res.a1;
	of_get_rv1126_timings(&pdev->dev, pdev->dev.of_node,
			      (uint32_t *)ddr_psci_param);

	/* enable start dcf in kernel after dcf ready */
	node = of_parse_phandle(pdev->dev.of_node, "dcf", 0);
	wait_ctrl.regmap_dcf = syscon_node_to_regmap(node);
	if (IS_ERR(wait_ctrl.regmap_dcf))
		return PTR_ERR(wait_ctrl.regmap_dcf);
	wait_ctrl.dcf_en = 1;

	init_waitqueue_head(&wait_ctrl.wait_wq);
	wait_ctrl.wait_en = 1;
	wait_ctrl.wait_time_out_ms = 17 * 5;

	complt_irq = platform_get_irq_byname(pdev, "complete");
	if (complt_irq < 0) {
		dev_err(&pdev->dev, "no IRQ for complt_irq: %d\n",
			complt_irq);
		return complt_irq;
	}
	wait_ctrl.complt_irq = complt_irq;

	ret = devm_request_irq(&pdev->dev, complt_irq, wait_dcf_complete_irq,
			       0, dev_name(&pdev->dev), &wait_ctrl);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot request complt_irq\n");
		return ret;
	}
	disable_irq(complt_irq);

	if (of_property_read_u32(pdev->dev.of_node, "update_drv_odt_cfg",
				 &ddr_psci_param->update_drv_odt_cfg))
		ddr_psci_param->update_drv_odt_cfg = 0;

	if (of_property_read_u32(pdev->dev.of_node, "update_deskew_cfg",
				 &ddr_psci_param->update_deskew_cfg))
		ddr_psci_param->update_deskew_cfg = 0;

	dmcfreq->set_rate_params = ddr_psci_param;
	rockchip_set_ddrclk_params(dmcfreq->set_rate_params);
	rockchip_set_ddrclk_dmcfreq_wait_complete(rockchip_dmcfreq_wait_complete);

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

static const struct of_device_id rockchip_dmcfreq_of_match[] = {
#if IS_ENABLED(CONFIG_CPU_PX30)
	{ .compatible = "rockchip,px30-dmc", .data = px30_dmc_init },
#endif
#if IS_ENABLED(CONFIG_CPU_RK1808)
	{ .compatible = "rockchip,rk1808-dmc", .data = rk1808_dmc_init },
#endif
#if IS_ENABLED(CONFIG_CPU_RK312X)
	{ .compatible = "rockchip,rk3128-dmc", .data = rk3128_dmc_init },
#endif
#if IS_ENABLED(CONFIG_CPU_RK322X)
	{ .compatible = "rockchip,rk3228-dmc", .data = rk3228_dmc_init },
#endif
#if IS_ENABLED(CONFIG_CPU_RK3288)
	{ .compatible = "rockchip,rk3288-dmc", .data = rk3288_dmc_init },
#endif
#if IS_ENABLED(CONFIG_CPU_RK3308)
	{ .compatible = "rockchip,rk3308-dmc", .data = NULL },
#endif
#if IS_ENABLED(CONFIG_CPU_RK3328)
	{ .compatible = "rockchip,rk3328-dmc", .data = rk3328_dmc_init },
#endif
#if IS_ENABLED(CONFIG_CPU_RK3368)
	{ .compatible = "rockchip,rk3368-dmc", .data = rk3368_dmc_init },
#endif
#if IS_ENABLED(CONFIG_CPU_RK3399)
	{ .compatible = "rockchip,rk3399-dmc", .data = rk3399_dmc_init },
#endif
#if IS_ENABLED(CONFIG_CPU_RK3568)
	{ .compatible = "rockchip,rk3568-dmc", .data = rk3568_dmc_init },
#endif
#if IS_ENABLED(CONFIG_CPU_RK3588)
	{ .compatible = "rockchip,rk3588-dmc", .data = rk3588_dmc_init },
#endif
#if IS_ENABLED(CONFIG_CPU_RV1126)
	{ .compatible = "rockchip,rv1126-dmc", .data = rv1126_dmc_init },
#endif
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
	tbl[i].freq = DMCFREQ_TABLE_END;

	*table = tbl;

	return 0;
}

static int rockchip_get_rl_map_talbe(struct device_node *np, char *porp_name,
				     struct rl_map_table **table)
{
	struct rl_map_table *tbl;
	const struct property *prop;
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

	tbl = kzalloc(sizeof(*tbl) * (count / 2 + 1), GFP_KERNEL);
	if (!tbl)
		return -ENOMEM;

	for (i = 0; i < count / 2; i++) {
		of_property_read_u32_index(np, porp_name, 2 * i, &tbl[i].pn);
		of_property_read_u32_index(np, porp_name, 2 * i + 1,
					   &tbl[i].rl);
	}

	tbl[i].pn = 0;
	tbl[i].rl = DMCFREQ_TABLE_END;

	*table = tbl;

	return 0;
}

static int rockchip_get_system_status_rate(struct device_node *np,
					   char *porp_name,
					   struct rockchip_dmcfreq *dmcfreq)
{
	const struct property *prop;
	unsigned int status = 0, freq = 0;
	unsigned long temp_rate = 0;
	int count, i;

	prop = of_find_property(np, porp_name, NULL);
	if (!prop)
		return -ENODEV;

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
		case SYS_STATUS_CIF0:
		case SYS_STATUS_CIF1:
		case SYS_STATUS_DUALVIEW:
			temp_rate = freq * 1000;
			if (dmcfreq->fixed_rate < temp_rate)
				dmcfreq->fixed_rate = temp_rate;
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

static unsigned long rockchip_freq_level_2_rate(struct rockchip_dmcfreq *dmcfreq,
						unsigned int level)
{
	unsigned long rate = 0;

	switch (level) {
	case DMC_FREQ_LEVEL_LOW:
		rate = dmcfreq->rate_low;
		break;
	case DMC_FREQ_LEVEL_MID_LOW:
		rate = dmcfreq->rate_mid_low;
		break;
	case DMC_FREQ_LEVEL_MID_HIGH:
		rate = dmcfreq->rate_mid_high;
		break;
	case DMC_FREQ_LEVEL_HIGH:
		rate = dmcfreq->rate_high;
		break;
	default:
		break;
	}

	return rate;
}

static int rockchip_get_system_status_level(struct device_node *np,
					    char *porp_name,
					    struct rockchip_dmcfreq *dmcfreq)
{
	const struct property *prop;
	unsigned int status = 0, level = 0;
	unsigned long temp_rate = 0;
	int count, i;

	prop = of_find_property(np, porp_name, NULL);
	if (!prop)
		return -ENODEV;

	if (!prop->value)
		return -ENODATA;

	count = of_property_count_u32_elems(np, porp_name);
	if (count < 0)
		return -EINVAL;

	if (count % 2)
		return -EINVAL;

	if (dmcfreq->freq_count == 1) {
		dmcfreq->rate_low = dmcfreq->freq_info_rate[0];
		dmcfreq->rate_mid_low = dmcfreq->freq_info_rate[0];
		dmcfreq->rate_mid_high = dmcfreq->freq_info_rate[0];
		dmcfreq->rate_high = dmcfreq->freq_info_rate[0];
	} else if (dmcfreq->freq_count == 2) {
		dmcfreq->rate_low = dmcfreq->freq_info_rate[0];
		dmcfreq->rate_mid_low = dmcfreq->freq_info_rate[0];
		dmcfreq->rate_mid_high = dmcfreq->freq_info_rate[1];
		dmcfreq->rate_high = dmcfreq->freq_info_rate[1];
	} else if (dmcfreq->freq_count == 3) {
		dmcfreq->rate_low = dmcfreq->freq_info_rate[0];
		dmcfreq->rate_mid_low = dmcfreq->freq_info_rate[1];
		dmcfreq->rate_mid_high = dmcfreq->freq_info_rate[1];
		dmcfreq->rate_high = dmcfreq->freq_info_rate[2];
	} else if (dmcfreq->freq_count == 4) {
		dmcfreq->rate_low = dmcfreq->freq_info_rate[0];
		dmcfreq->rate_mid_low = dmcfreq->freq_info_rate[1];
		dmcfreq->rate_mid_high = dmcfreq->freq_info_rate[2];
		dmcfreq->rate_high = dmcfreq->freq_info_rate[3];
	} else if (dmcfreq->freq_count == 5 || dmcfreq->freq_count == 6) {
		dmcfreq->rate_low = dmcfreq->freq_info_rate[0];
		dmcfreq->rate_mid_low = dmcfreq->freq_info_rate[1];
		dmcfreq->rate_mid_high = dmcfreq->freq_info_rate[dmcfreq->freq_count - 2];
		dmcfreq->rate_high = dmcfreq->freq_info_rate[dmcfreq->freq_count - 1];
	} else {
		return -EINVAL;
	}

	dmcfreq->auto_min_rate = dmcfreq->rate_low;

	for (i = 0; i < count / 2; i++) {
		of_property_read_u32_index(np, porp_name, 2 * i,
					   &status);
		of_property_read_u32_index(np, porp_name, 2 * i + 1,
					   &level);
		switch (status) {
		case SYS_STATUS_NORMAL:
			dmcfreq->normal_rate = rockchip_freq_level_2_rate(dmcfreq, level);
			dev_info(dmcfreq->dev, "normal_rate = %ld\n", dmcfreq->normal_rate);
			break;
		case SYS_STATUS_SUSPEND:
			dmcfreq->suspend_rate = rockchip_freq_level_2_rate(dmcfreq, level);
			dev_info(dmcfreq->dev, "suspend_rate = %ld\n", dmcfreq->suspend_rate);
			break;
		case SYS_STATUS_VIDEO_1080P:
			dmcfreq->video_1080p_rate = rockchip_freq_level_2_rate(dmcfreq, level);
			dev_info(dmcfreq->dev, "video_1080p_rate = %ld\n",
				 dmcfreq->video_1080p_rate);
			break;
		case SYS_STATUS_VIDEO_4K:
			dmcfreq->video_4k_rate = rockchip_freq_level_2_rate(dmcfreq, level);
			dev_info(dmcfreq->dev, "video_4k_rate = %ld\n", dmcfreq->video_4k_rate);
			break;
		case SYS_STATUS_VIDEO_4K_10B:
			dmcfreq->video_4k_10b_rate = rockchip_freq_level_2_rate(dmcfreq, level);
			dev_info(dmcfreq->dev, "video_4k_10b_rate = %ld\n",
				 dmcfreq->video_4k_10b_rate);
			break;
		case SYS_STATUS_PERFORMANCE:
			dmcfreq->performance_rate = rockchip_freq_level_2_rate(dmcfreq, level);
			dev_info(dmcfreq->dev, "performance_rate = %ld\n",
				 dmcfreq->performance_rate);
			break;
		case SYS_STATUS_HDMI:
			dmcfreq->hdmi_rate = rockchip_freq_level_2_rate(dmcfreq, level);
			dev_info(dmcfreq->dev, "hdmi_rate = %ld\n", dmcfreq->hdmi_rate);
			break;
		case SYS_STATUS_IDLE:
			dmcfreq->idle_rate = rockchip_freq_level_2_rate(dmcfreq, level);
			dev_info(dmcfreq->dev, "idle_rate = %ld\n", dmcfreq->idle_rate);
			break;
		case SYS_STATUS_REBOOT:
			dmcfreq->reboot_rate = rockchip_freq_level_2_rate(dmcfreq, level);
			dev_info(dmcfreq->dev, "reboot_rate = %ld\n", dmcfreq->reboot_rate);
			break;
		case SYS_STATUS_BOOST:
			dmcfreq->boost_rate = rockchip_freq_level_2_rate(dmcfreq, level);
			dev_info(dmcfreq->dev, "boost_rate = %ld\n", dmcfreq->boost_rate);
			break;
		case SYS_STATUS_ISP:
		case SYS_STATUS_CIF0:
		case SYS_STATUS_CIF1:
		case SYS_STATUS_DUALVIEW:
			temp_rate = rockchip_freq_level_2_rate(dmcfreq, level);
			if (dmcfreq->fixed_rate < temp_rate) {
				dmcfreq->fixed_rate = temp_rate;
				dev_info(dmcfreq->dev,
					 "fixed_rate(isp|cif0|cif1|dualview) = %ld\n",
					 dmcfreq->fixed_rate);
			}
			break;
		case SYS_STATUS_LOW_POWER:
			dmcfreq->low_power_rate = rockchip_freq_level_2_rate(dmcfreq, level);
			dev_info(dmcfreq->dev, "low_power_rate = %ld\n", dmcfreq->low_power_rate);
			break;
		default:
			break;
		}
	}

	return 0;
}

static void rockchip_dmcfreq_update_target(struct rockchip_dmcfreq *dmcfreq)
{
	struct devfreq *devfreq = dmcfreq->info.devfreq;

	mutex_lock(&devfreq->lock);
	update_devfreq(devfreq);
	mutex_unlock(&devfreq->lock);
}

static int rockchip_dmcfreq_system_status_notifier(struct notifier_block *nb,
						   unsigned long status,
						   void *ptr)
{
	struct rockchip_dmcfreq *dmcfreq = system_status_to_dmcfreq(nb);
	unsigned long target_rate = 0;
	unsigned int refresh = false;
	bool is_fixed = false;

	if (dmcfreq->fixed_rate && (is_dualview(status) || is_isp(status))) {
		if (dmcfreq->is_fixed)
			return NOTIFY_OK;
		is_fixed = true;
		target_rate = dmcfreq->fixed_rate;
		goto next;
	}

	if (dmcfreq->reboot_rate && (status & SYS_STATUS_REBOOT)) {
		if (dmcfreq->info.auto_freq_en)
			devfreq_monitor_stop(dmcfreq->info.devfreq);
		target_rate = dmcfreq->reboot_rate;
		goto next;
	}

	if (dmcfreq->suspend_rate && (status & SYS_STATUS_SUSPEND)) {
		target_rate = dmcfreq->suspend_rate;
		refresh = true;
		goto next;
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

next:

	dev_dbg(dmcfreq->dev, "status=0x%x\n", (unsigned int)status);
	dmcfreq->is_fixed = is_fixed;
	dmcfreq->status_rate = target_rate;
	if (dmcfreq->refresh != refresh) {
		if (dmcfreq->set_auto_self_refresh)
			dmcfreq->set_auto_self_refresh(refresh);
		dmcfreq->refresh = refresh;
	}
	rockchip_dmcfreq_update_target(dmcfreq);

	return NOTIFY_OK;
}

static ssize_t rockchip_dmcfreq_status_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	unsigned int status = rockchip_get_system_status();

	return sprintf(buf, "0x%x\n", status);
}

static ssize_t rockchip_dmcfreq_status_store(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf,
					     size_t count)
{
	if (!count)
		return -EINVAL;

	rockchip_update_system_status(buf);

	return count;
}

static DEVICE_ATTR(system_status, 0644, rockchip_dmcfreq_status_show,
		   rockchip_dmcfreq_status_store);

static ssize_t upthreshold_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct rockchip_dmcfreq *dmcfreq = dev_get_drvdata(dev->parent);
	struct rockchip_dmcfreq_ondemand_data *data = &dmcfreq->ondemand_data;

	return sprintf(buf, "%d\n", data->upthreshold);
}

static ssize_t upthreshold_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf,
				 size_t count)
{
	struct rockchip_dmcfreq *dmcfreq = dev_get_drvdata(dev->parent);
	struct rockchip_dmcfreq_ondemand_data *data = &dmcfreq->ondemand_data;
	unsigned int value;

	if (kstrtouint(buf, 10, &value))
		return -EINVAL;

	data->upthreshold = value;

	return count;
}

static DEVICE_ATTR_RW(upthreshold);

static ssize_t downdifferential_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct rockchip_dmcfreq *dmcfreq = dev_get_drvdata(dev->parent);
	struct rockchip_dmcfreq_ondemand_data *data = &dmcfreq->ondemand_data;

	return sprintf(buf, "%d\n", data->downdifferential);
}

static ssize_t downdifferential_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t count)
{
	struct rockchip_dmcfreq *dmcfreq = dev_get_drvdata(dev->parent);
	struct rockchip_dmcfreq_ondemand_data *data = &dmcfreq->ondemand_data;
	unsigned int value;

	if (kstrtouint(buf, 10, &value))
		return -EINVAL;

	data->downdifferential = value;

	return count;
}

static DEVICE_ATTR_RW(downdifferential);

static unsigned long get_nocp_req_rate(struct rockchip_dmcfreq *dmcfreq)
{
	unsigned long target = 0, cpu_bw = 0;
	int i;

	if (!dmcfreq->cpu_bw_tbl || dmcfreq->nocp_cpu_id < 0)
		goto out;

	cpu_bw = dmcfreq->nocp_bw[dmcfreq->nocp_cpu_id];

	for (i = 0; dmcfreq->cpu_bw_tbl[i].freq != CPUFREQ_TABLE_END; i++) {
		if (cpu_bw >= dmcfreq->cpu_bw_tbl[i].min)
			target = dmcfreq->cpu_bw_tbl[i].freq;
	}

out:
	return target;
}

static int devfreq_dmc_ondemand_func(struct devfreq *df,
				     unsigned long *freq)
{
	int err;
	struct devfreq_dev_status *stat;
	unsigned long long a, b;
	struct rockchip_dmcfreq *dmcfreq = dev_get_drvdata(df->dev.parent);
	struct rockchip_dmcfreq_ondemand_data *data = &dmcfreq->ondemand_data;
	unsigned int upthreshold = data->upthreshold;
	unsigned int downdifferential = data->downdifferential;
	unsigned long target_freq = 0, nocp_req_rate = 0;
	u64 now;

	if (dmcfreq->info.auto_freq_en && !dmcfreq->is_fixed) {
		if (dmcfreq->status_rate)
			target_freq = dmcfreq->status_rate;
		else if (dmcfreq->auto_min_rate)
			target_freq = dmcfreq->auto_min_rate;
		nocp_req_rate = get_nocp_req_rate(dmcfreq);
		target_freq = max3(target_freq, nocp_req_rate,
				   dmcfreq->info.vop_req_rate);
		now = ktime_to_us(ktime_get());
		if (now < dmcfreq->touchboostpulse_endtime)
			target_freq = max(target_freq, dmcfreq->boost_rate);
	} else {
		if (dmcfreq->status_rate)
			target_freq = dmcfreq->status_rate;
		else if (dmcfreq->normal_rate)
			target_freq = dmcfreq->normal_rate;
		if (target_freq)
			*freq = target_freq;
		if (dmcfreq->info.auto_freq_en && !devfreq_update_stats(df))
			return 0;
		goto reset_last_status;
	}

	if (!upthreshold || !downdifferential)
		goto reset_last_status;

	if (upthreshold > 100 ||
	    upthreshold < downdifferential)
		goto reset_last_status;

	err = devfreq_update_stats(df);
	if (err)
		goto reset_last_status;

	stat = &df->last_status;

	/* Assume MAX if it is going to be divided by zero */
	if (stat->total_time == 0) {
		*freq = DEVFREQ_MAX_FREQ;
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
		*freq = DEVFREQ_MAX_FREQ;
		return 0;
	}

	/* Set MAX if we do not know the initial frequency */
	if (stat->current_frequency == 0) {
		*freq = DEVFREQ_MAX_FREQ;
		return 0;
	}

	/* Keep the current frequency */
	if (stat->busy_time * 100 >
	    stat->total_time * (upthreshold - downdifferential)) {
		*freq = max(target_freq, stat->current_frequency);
		return 0;
	}

	/* Set the desired frequency based on the load */
	a = stat->busy_time;
	a *= stat->current_frequency;
	b = div_u64(a, stat->total_time);
	b *= 100;
	b = div_u64(b, (upthreshold - downdifferential / 2));
	*freq = max_t(unsigned long, target_freq, b);

	return 0;

reset_last_status:
	reset_last_status(df);

	return 0;
}

static int devfreq_dmc_ondemand_handler(struct devfreq *devfreq,
					unsigned int event, void *data)
{
	struct rockchip_dmcfreq *dmcfreq = dev_get_drvdata(devfreq->dev.parent);

	if (!dmcfreq->info.auto_freq_en)
		return 0;

	switch (event) {
	case DEVFREQ_GOV_START:
		devfreq_monitor_start(devfreq);
		break;

	case DEVFREQ_GOV_STOP:
		devfreq_monitor_stop(devfreq);
		break;

	case DEVFREQ_GOV_UPDATE_INTERVAL:
		devfreq_update_interval(devfreq, (unsigned int *)data);
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

static int rockchip_dmcfreq_enable_event(struct rockchip_dmcfreq *dmcfreq)
{
	int i, ret;

	if (!dmcfreq->info.auto_freq_en)
		return 0;

	for (i = 0; i < dmcfreq->edev_count; i++) {
		ret = devfreq_event_enable_edev(dmcfreq->edev[i]);
		if (ret < 0) {
			dev_err(dmcfreq->dev,
				"failed to enable devfreq-event\n");
			return ret;
		}
	}

	return 0;
}

static int rockchip_dmcfreq_disable_event(struct rockchip_dmcfreq *dmcfreq)
{
	int i, ret;

	if (!dmcfreq->info.auto_freq_en)
		return 0;

	for (i = 0; i < dmcfreq->edev_count; i++) {
		ret = devfreq_event_disable_edev(dmcfreq->edev[i]);
		if (ret < 0) {
			dev_err(dmcfreq->dev,
				"failed to disable devfreq-event\n");
			return ret;
		}
	}

	return 0;
}

static int rockchip_get_edev_id(struct rockchip_dmcfreq *dmcfreq,
				const char *name)
{
	struct devfreq_event_dev *edev;
	int i;

	for (i = 0; i < dmcfreq->edev_count; i++) {
		edev = dmcfreq->edev[i];
		if (!strcmp(edev->desc->name, name))
			return i;
	}

	return -EINVAL;
}

static int rockchip_dmcfreq_get_event(struct rockchip_dmcfreq *dmcfreq)
{
	struct device *dev = dmcfreq->dev;
	struct device_node *events_np, *np = dev->of_node;
	int i, j, count, available_count = 0;

	count = devfreq_event_get_edev_count(dev, "devfreq-events");
	if (count < 0) {
		dev_dbg(dev, "failed to get count of devfreq-event dev\n");
		return 0;
	}
	for (i = 0; i < count; i++) {
		events_np = of_parse_phandle(np, "devfreq-events", i);
		if (!events_np)
			continue;
		if (of_device_is_available(events_np))
			available_count++;
		of_node_put(events_np);
	}
	if (!available_count) {
		dev_dbg(dev, "failed to get available devfreq-event\n");
		return 0;
	}
	dmcfreq->edev_count = available_count;
	dmcfreq->edev = devm_kzalloc(dev,
				     sizeof(*dmcfreq->edev) * available_count,
				     GFP_KERNEL);
	if (!dmcfreq->edev)
		return -ENOMEM;

	for (i = 0, j = 0; i < count; i++) {
		events_np = of_parse_phandle(np, "devfreq-events", i);
		if (!events_np)
			continue;
		if (of_device_is_available(events_np)) {
			of_node_put(events_np);
			if (j >= available_count) {
				dev_err(dev, "invalid event conut\n");
				return -EINVAL;
			}
			dmcfreq->edev[j] =
				devfreq_event_get_edev_by_phandle(dev, "devfreq-events", i);
			if (IS_ERR(dmcfreq->edev[j]))
				return -EPROBE_DEFER;
			j++;
		} else {
			of_node_put(events_np);
		}
	}
	dmcfreq->info.auto_freq_en = true;
	dmcfreq->dfi_id = rockchip_get_edev_id(dmcfreq, "dfi");
	dmcfreq->nocp_cpu_id = rockchip_get_edev_id(dmcfreq, "nocp-cpu");
	dmcfreq->nocp_bw =
		devm_kzalloc(dev, sizeof(*dmcfreq->nocp_bw) * available_count,
			     GFP_KERNEL);
	if (!dmcfreq->nocp_bw)
		return -ENOMEM;

	return 0;
}

static int rockchip_dmcfreq_power_control(struct rockchip_dmcfreq *dmcfreq)
{
	struct device *dev = dmcfreq->dev;
	struct device_node *np = dev->of_node;
	struct opp_table *opp_table = NULL, *reg_opp_table = NULL;
	const char * const reg_names[] = {"center", "mem"};
	int ret = 0;

	if (of_find_property(np, "mem-supply", NULL))
		dmcfreq->regulator_count = 2;
	else
		dmcfreq->regulator_count = 1;
	reg_opp_table = dev_pm_opp_set_regulators(dev, reg_names,
						  dmcfreq->regulator_count);
	if (IS_ERR(reg_opp_table)) {
		dev_err(dev, "failed to set regulators\n");
		return PTR_ERR(reg_opp_table);
	}
	opp_table = dev_pm_opp_register_set_opp_helper(dev, rockchip_dmcfreq_opp_helper);
	if (IS_ERR(opp_table)) {
		dev_err(dev, "failed to set opp helper\n");
		ret = PTR_ERR(opp_table);
		goto reg_opp_table;
	}

	dmcfreq->vdd_center = devm_regulator_get_optional(dev, "center");
	if (IS_ERR(dmcfreq->vdd_center)) {
		dev_err(dev, "Cannot get the regulator \"center\"\n");
		ret = PTR_ERR(dmcfreq->vdd_center);
		goto opp_table;
	}
	if (dmcfreq->regulator_count > 1) {
		dmcfreq->mem_reg = devm_regulator_get_optional(dev, "mem");
		if (IS_ERR(dmcfreq->mem_reg)) {
			dev_err(dev, "Cannot get the regulator \"mem\"\n");
			ret = PTR_ERR(dmcfreq->mem_reg);
			goto opp_table;
		}
	}

	dmcfreq->dmc_clk = devm_clk_get(dev, "dmc_clk");
	if (IS_ERR(dmcfreq->dmc_clk)) {
		dev_err(dev, "Cannot get the clk dmc_clk. If using SCMI, trusted firmware need update to V1.01 and above.\n");
		ret = PTR_ERR(dmcfreq->dmc_clk);
		goto opp_table;
	}
	dmcfreq->rate = clk_get_rate(dmcfreq->dmc_clk);

	return 0;

opp_table:
	if (opp_table)
		dev_pm_opp_unregister_set_opp_helper(opp_table);
reg_opp_table:
	if (reg_opp_table)
		dev_pm_opp_put_regulators(reg_opp_table);

	return ret;
}

static int rockchip_dmcfreq_dmc_init(struct platform_device *pdev,
				     struct rockchip_dmcfreq *dmcfreq)
{
	const struct of_device_id *match;
	int (*init)(struct platform_device *pdev,
		    struct rockchip_dmcfreq *data);
	int ret;

	match = of_match_node(rockchip_dmcfreq_of_match, pdev->dev.of_node);
	if (match) {
		init = match->data;
		if (init) {
			ret = init(pdev, dmcfreq);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static void rockchip_dmcfreq_parse_dt(struct rockchip_dmcfreq *dmcfreq)
{
	struct device *dev = dmcfreq->dev;
	struct device_node *np = dev->of_node;

	if (!rockchip_get_system_status_rate(np, "system-status-freq", dmcfreq))
		dmcfreq->system_status_en = true;
	else if (!rockchip_get_system_status_level(np, "system-status-level", dmcfreq))
		dmcfreq->system_status_en = true;

	of_property_read_u32(np, "min-cpu-freq", &dmcfreq->min_cpu_freq);

	of_property_read_u32(np, "upthreshold",
			     &dmcfreq->ondemand_data.upthreshold);
	of_property_read_u32(np, "downdifferential",
			     &dmcfreq->ondemand_data.downdifferential);
	if (dmcfreq->info.auto_freq_en)
		of_property_read_u32(np, "auto-freq-en",
				     &dmcfreq->info.auto_freq_en);
	if (!dmcfreq->auto_min_rate) {
		of_property_read_u32(np, "auto-min-freq",
				     (u32 *)&dmcfreq->auto_min_rate);
		dmcfreq->auto_min_rate *= 1000;
	}

	if (rockchip_get_freq_map_talbe(np, "cpu-bw-dmc-freq",
					&dmcfreq->cpu_bw_tbl))
		dev_dbg(dev, "failed to get cpu bandwidth to dmc rate\n");
	if (rockchip_get_freq_map_talbe(np, "vop-frame-bw-dmc-freq",
					&dmcfreq->info.vop_frame_bw_tbl))
		dev_dbg(dev, "failed to get vop frame bandwidth to dmc rate\n");
	if (rockchip_get_freq_map_talbe(np, "vop-bw-dmc-freq",
					&dmcfreq->info.vop_bw_tbl))
		dev_err(dev, "failed to get vop bandwidth to dmc rate\n");
	if (rockchip_get_rl_map_talbe(np, "vop-pn-msch-readlatency",
				      &dmcfreq->info.vop_pn_rl_tbl))
		dev_err(dev, "failed to get vop pn to msch rl\n");

	of_property_read_u32(np, "touchboost_duration",
			     (u32 *)&dmcfreq->touchboostpulse_duration_val);
	if (dmcfreq->touchboostpulse_duration_val)
		dmcfreq->touchboostpulse_duration_val *= USEC_PER_MSEC;
	else
		dmcfreq->touchboostpulse_duration_val = 500 * USEC_PER_MSEC;
}

static int rockchip_dmcfreq_set_volt_only(struct rockchip_dmcfreq *dmcfreq)
{
	struct device *dev = dmcfreq->dev;
	struct dev_pm_opp *opp;
	unsigned long opp_volt, opp_rate = dmcfreq->rate;
	int ret;

	opp = devfreq_recommended_opp(dev, &opp_rate, 0);
	if (IS_ERR(opp)) {
		dev_err(dev, "Failed to find opp for %lu Hz\n", opp_rate);
		return PTR_ERR(opp);
	}
	opp_volt = dev_pm_opp_get_voltage(opp);
	dev_pm_opp_put(opp);

	ret = regulator_set_voltage(dmcfreq->vdd_center, opp_volt, INT_MAX);
	if (ret) {
		dev_err(dev, "Cannot set voltage %lu uV\n", opp_volt);
		return ret;
	}

	return 0;
}

static int rockchip_dmcfreq_add_devfreq(struct rockchip_dmcfreq *dmcfreq)
{
	struct devfreq_dev_profile *devp = &rockchip_devfreq_dmc_profile;
	struct device *dev = dmcfreq->dev;
	struct dev_pm_opp *opp;
	struct devfreq *devfreq;
	unsigned long opp_rate = dmcfreq->rate;

	opp = devfreq_recommended_opp(dev, &opp_rate, 0);
	if (IS_ERR(opp)) {
		dev_err(dev, "Failed to find opp for %lu Hz\n", opp_rate);
		return PTR_ERR(opp);
	}
	dev_pm_opp_put(opp);

	devp->initial_freq = dmcfreq->rate;
	devfreq = devm_devfreq_add_device(dev, devp, "dmc_ondemand",
					  &dmcfreq->ondemand_data);
	if (IS_ERR(devfreq)) {
		dev_err(dev, "failed to add devfreq\n");
		return PTR_ERR(devfreq);
	}

	devm_devfreq_register_opp_notifier(dev, devfreq);

	devfreq->last_status.current_frequency = opp_rate;

	reset_last_status(devfreq);

	dmcfreq->info.devfreq = devfreq;

	return 0;
}

static void rockchip_dmcfreq_register_notifier(struct rockchip_dmcfreq *dmcfreq)
{
	int ret;

	if (vop_register_dmc())
		dev_err(dmcfreq->dev, "fail to register notify to vop.\n");

	dmcfreq->status_nb.notifier_call =
		rockchip_dmcfreq_system_status_notifier;
	ret = rockchip_register_system_status_notifier(&dmcfreq->status_nb);
	if (ret)
		dev_err(dmcfreq->dev, "failed to register system_status nb\n");

	dmc_mdevp.data = dmcfreq->info.devfreq;
	dmcfreq->mdev_info = rockchip_system_monitor_register(dmcfreq->dev,
							      &dmc_mdevp);
	if (IS_ERR(dmcfreq->mdev_info)) {
		dev_dbg(dmcfreq->dev, "without without system monitor\n");
		dmcfreq->mdev_info = NULL;
	}
}

static void rockchip_dmcfreq_add_interface(struct rockchip_dmcfreq *dmcfreq)
{
	struct devfreq *devfreq = dmcfreq->info.devfreq;

	if (sysfs_create_file(&devfreq->dev.kobj, &dev_attr_upthreshold.attr))
		dev_err(dmcfreq->dev,
			"failed to register upthreshold sysfs file\n");
	if (sysfs_create_file(&devfreq->dev.kobj,
			      &dev_attr_downdifferential.attr))
		dev_err(dmcfreq->dev,
			"failed to register downdifferential sysfs file\n");

	if (!rockchip_add_system_status_interface(&devfreq->dev))
		return;
	if (sysfs_create_file(&devfreq->dev.kobj,
			      &dev_attr_system_status.attr))
		dev_err(dmcfreq->dev,
			"failed to register system_status sysfs file\n");
}

static void rockchip_dmcfreq_boost_work(struct work_struct *work)
{
	struct rockchip_dmcfreq *dmcfreq = boost_to_dmcfreq(work);

	rockchip_dmcfreq_update_target(dmcfreq);
}

static void rockchip_dmcfreq_input_event(struct input_handle *handle,
					 unsigned int type,
					 unsigned int code,
					 int value)
{
	struct rockchip_dmcfreq *dmcfreq = handle->private;
	u64 now, endtime;

	if (type != EV_ABS && type != EV_KEY)
		return;

	now = ktime_to_us(ktime_get());
	endtime = now + dmcfreq->touchboostpulse_duration_val;
	if (endtime < (dmcfreq->touchboostpulse_endtime + 10 * USEC_PER_MSEC))
		return;
	dmcfreq->touchboostpulse_endtime = endtime;

	queue_work(system_freezable_wq, &dmcfreq->boost_work);
}

static int rockchip_dmcfreq_input_connect(struct input_handler *handler,
					  struct input_dev *dev,
					  const struct input_device_id *id)
{
	int error;
	struct input_handle *handle;
	struct rockchip_dmcfreq *dmcfreq = input_hd_to_dmcfreq(handler);

	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "dmcfreq";
	handle->private = dmcfreq;

	error = input_register_handle(handle);
	if (error)
		goto err2;

	error = input_open_device(handle);
	if (error)
		goto err1;

	return 0;
err1:
	input_unregister_handle(handle);
err2:
	kfree(handle);
	return error;
}

static void rockchip_dmcfreq_input_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id rockchip_dmcfreq_input_ids[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT |
			INPUT_DEVICE_ID_MATCH_ABSBIT,
		.evbit = { BIT_MASK(EV_ABS) },
		.absbit = { [BIT_WORD(ABS_MT_POSITION_X)] =
			BIT_MASK(ABS_MT_POSITION_X) |
			BIT_MASK(ABS_MT_POSITION_Y) },
	},
	{
		.flags = INPUT_DEVICE_ID_MATCH_KEYBIT |
			INPUT_DEVICE_ID_MATCH_ABSBIT,
		.keybit = { [BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH) },
		.absbit = { [BIT_WORD(ABS_X)] =
			BIT_MASK(ABS_X) | BIT_MASK(ABS_Y) },
	},
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_KEY) },
	},
	{ },
};

static void rockchip_dmcfreq_boost_init(struct rockchip_dmcfreq *dmcfreq)
{
	if (!dmcfreq->boost_rate)
		return;
	INIT_WORK(&dmcfreq->boost_work, rockchip_dmcfreq_boost_work);
	dmcfreq->input_handler.event = rockchip_dmcfreq_input_event;
	dmcfreq->input_handler.connect = rockchip_dmcfreq_input_connect;
	dmcfreq->input_handler.disconnect = rockchip_dmcfreq_input_disconnect;
	dmcfreq->input_handler.name = "dmcfreq";
	dmcfreq->input_handler.id_table = rockchip_dmcfreq_input_ids;
	if (input_register_handler(&dmcfreq->input_handler))
		dev_err(dmcfreq->dev, "failed to register input handler\n");
}

static unsigned long model_static_power(struct devfreq *devfreq,
					unsigned long voltage)
{
	struct device *dev = devfreq->dev.parent;
	struct rockchip_dmcfreq *dmcfreq = dev_get_drvdata(dev);

	int temperature;
	unsigned long temp;
	unsigned long temp_squared, temp_cubed, temp_scaling_factor;
	const unsigned long voltage_cubed = (voltage * voltage * voltage) >> 10;

	if (!IS_ERR_OR_NULL(dmcfreq->ddr_tz) && dmcfreq->ddr_tz->ops->get_temp) {
		int ret;

		ret =
		    dmcfreq->ddr_tz->ops->get_temp(dmcfreq->ddr_tz,
						   &temperature);
		if (ret) {
			dev_warn_ratelimited(dev,
					     "failed to read temp for ddr thermal zone: %d\n",
					     ret);
			temperature = FALLBACK_STATIC_TEMPERATURE;
		}
	} else {
		temperature = FALLBACK_STATIC_TEMPERATURE;
	}

	/*
	 * Calculate the temperature scaling factor. To be applied to the
	 * voltage scaled power.
	 */
	temp = temperature / 1000;
	temp_squared = temp * temp;
	temp_cubed = temp_squared * temp;
	temp_scaling_factor = (dmcfreq->ts[3] * temp_cubed)
	    + (dmcfreq->ts[2] * temp_squared)
	    + (dmcfreq->ts[1] * temp)
	    + dmcfreq->ts[0];

	return (((dmcfreq->static_coefficient * voltage_cubed) >> 20)
		* temp_scaling_factor) / 1000000;
}

static struct devfreq_cooling_power ddr_cooling_power_data = {
	.get_static_power = model_static_power,
	.dyn_power_coeff = 120,
};

static int ddr_power_model_simple_init(struct rockchip_dmcfreq *dmcfreq)
{
	struct device_node *power_model_node;
	const char *tz_name;
	u32 temp;

	power_model_node = of_get_child_by_name(dmcfreq->dev->of_node,
						"ddr_power_model");
	if (!power_model_node) {
		dev_err(dmcfreq->dev, "could not find power_model node\n");
		return -ENODEV;
	}

	if (of_property_read_string(power_model_node, "thermal-zone", &tz_name)) {
		dev_err(dmcfreq->dev, "ts in power_model not available\n");
		return -EINVAL;
	}

	dmcfreq->ddr_tz = thermal_zone_get_zone_by_name(tz_name);
	if (IS_ERR(dmcfreq->ddr_tz)) {
		pr_warn_ratelimited
		    ("Error getting ddr thermal zone (%ld), not yet ready?\n",
		     PTR_ERR(dmcfreq->ddr_tz));
		dmcfreq->ddr_tz = NULL;

		return -EPROBE_DEFER;
	}

	if (of_property_read_u32(power_model_node, "static-power-coefficient",
				 &dmcfreq->static_coefficient)) {
		dev_err(dmcfreq->dev,
			"static-power-coefficient not available\n");
		return -EINVAL;
	}
	if (of_property_read_u32(power_model_node, "dynamic-power-coefficient",
				 &temp)) {
		dev_err(dmcfreq->dev,
			"dynamic-power-coefficient not available\n");
		return -EINVAL;
	}
	ddr_cooling_power_data.dyn_power_coeff = (unsigned long)temp;

	if (of_property_read_u32_array
	    (power_model_node, "ts", (u32 *)dmcfreq->ts, 4)) {
		dev_err(dmcfreq->dev, "ts in power_model not available\n");
		return -EINVAL;
	}

	return 0;
}

static void
rockchip_dmcfreq_register_cooling_device(struct rockchip_dmcfreq *dmcfreq)
{
	int ret;

	ret = ddr_power_model_simple_init(dmcfreq);
	if (ret)
		return;
	dmcfreq->devfreq_cooling =
		of_devfreq_cooling_register_power(dmcfreq->dev->of_node,
						  dmcfreq->info.devfreq,
						  &ddr_cooling_power_data);
	if (IS_ERR(dmcfreq->devfreq_cooling)) {
		ret = PTR_ERR(dmcfreq->devfreq_cooling);
		dev_err(dmcfreq->dev,
			"Failed to register cooling device (%d)\n",
			ret);
	}
}

static int rockchip_dmcfreq_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rockchip_dmcfreq *data;
	int ret;

	data = devm_kzalloc(dev, sizeof(struct rockchip_dmcfreq), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = dev;
	data->info.dev = dev;
	mutex_init(&data->lock);
	INIT_LIST_HEAD(&data->video_info_list);

	ret = rockchip_dmcfreq_get_event(data);
	if (ret)
		return ret;

	ret = rockchip_dmcfreq_power_control(data);
	if (ret)
		return ret;

	ret = rockchip_init_opp_table(dev, NULL, "ddr_leakage", "center");
	if (ret)
		return ret;

	ret = rockchip_dmcfreq_dmc_init(pdev, data);
	if (ret)
		return ret;

	rockchip_dmcfreq_parse_dt(data);
	if (!data->system_status_en && !data->info.auto_freq_en) {
		dev_info(dev, "don't add devfreq feature\n");
		return rockchip_dmcfreq_set_volt_only(data);
	}

	cpu_latency_qos_add_request(&pm_qos, PM_QOS_DEFAULT_VALUE);
	platform_set_drvdata(pdev, data);

	ret = devfreq_add_governor(&devfreq_dmc_ondemand);
	if (ret)
		return ret;
	ret = rockchip_dmcfreq_enable_event(data);
	if (ret)
		return ret;
	ret = rockchip_dmcfreq_add_devfreq(data);
	if (ret) {
		rockchip_dmcfreq_disable_event(data);
		return ret;
	}

	rockchip_dmcfreq_register_notifier(data);
	rockchip_dmcfreq_add_interface(data);
	rockchip_dmcfreq_boost_init(data);
	rockchip_dmcfreq_vop_bandwidth_init(&data->info);
	rockchip_dmcfreq_register_cooling_device(data);

	rockchip_set_system_status(SYS_STATUS_NORMAL);

	return 0;
}

static __maybe_unused int rockchip_dmcfreq_suspend(struct device *dev)
{
	struct rockchip_dmcfreq *dmcfreq = dev_get_drvdata(dev);
	int ret = 0;

	if (!dmcfreq)
		return 0;

	ret = rockchip_dmcfreq_disable_event(dmcfreq);
	if (ret)
		return ret;

	ret = devfreq_suspend_device(dmcfreq->info.devfreq);
	if (ret < 0) {
		dev_err(dev, "failed to suspend the devfreq devices\n");
		return ret;
	}

	/* set voltage to sleep_volt if need */
	if (dmcfreq->sleep_volt && dmcfreq->sleep_volt != dmcfreq->volt) {
		ret = regulator_set_voltage(dmcfreq->vdd_center,
					    dmcfreq->sleep_volt, INT_MAX);
		if (ret) {
			dev_err(dev, "Cannot set vdd voltage %lu uV\n",
				dmcfreq->sleep_volt);
			return ret;
		}
	}
	if (dmcfreq->sleep_mem_volt &&
	    dmcfreq->sleep_mem_volt != dmcfreq->mem_volt) {
		ret = regulator_set_voltage(dmcfreq->mem_reg,
					    dmcfreq->sleep_mem_volt, INT_MAX);
		if (ret) {
			dev_err(dev, "Cannot set mem voltage %lu uV\n",
				dmcfreq->sleep_mem_volt);
			return ret;
		}
	}

	return 0;
}

static __maybe_unused int rockchip_dmcfreq_resume(struct device *dev)
{
	struct rockchip_dmcfreq *dmcfreq = dev_get_drvdata(dev);
	int ret = 0;

	if (!dmcfreq)
		return 0;

	/* restore voltage if it is sleep_volt */
	if (dmcfreq->sleep_volt && dmcfreq->sleep_volt != dmcfreq->volt) {
		ret = regulator_set_voltage(dmcfreq->vdd_center, dmcfreq->volt,
					    INT_MAX);
		if (ret) {
			dev_err(dev, "Cannot set vdd voltage %lu uV\n",
				dmcfreq->volt);
			return ret;
		}
	}
	if (dmcfreq->sleep_mem_volt &&
	    dmcfreq->sleep_mem_volt != dmcfreq->mem_volt) {
		ret = regulator_set_voltage(dmcfreq->mem_reg, dmcfreq->mem_volt,
					    INT_MAX);
		if (ret) {
			dev_err(dev, "Cannot set mem voltage %lu uV\n",
				dmcfreq->mem_volt);
			return ret;
		}
	}

	ret = rockchip_dmcfreq_enable_event(dmcfreq);
	if (ret)
		return ret;

	ret = devfreq_resume_device(dmcfreq->info.devfreq);
	if (ret < 0) {
		dev_err(dev, "failed to resume the devfreq devices\n");
		return ret;
	}
	return ret;
}

static SIMPLE_DEV_PM_OPS(rockchip_dmcfreq_pm, rockchip_dmcfreq_suspend,
			 rockchip_dmcfreq_resume);
static struct platform_driver rockchip_dmcfreq_driver = {
	.probe	= rockchip_dmcfreq_probe,
	.driver = {
		.name	= "rockchip-dmc",
		.pm	= &rockchip_dmcfreq_pm,
		.of_match_table = rockchip_dmcfreq_of_match,
	},
};
module_platform_driver(rockchip_dmcfreq_driver);

MODULE_AUTHOR("Finley Xiao <finley.xiao@rock-chips.com>");
MODULE_DESCRIPTION("rockchip dmcfreq driver with devfreq framework");
MODULE_LICENSE("GPL v2");
