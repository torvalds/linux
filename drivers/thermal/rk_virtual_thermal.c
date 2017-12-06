/*
 * rk virtual tsadc driver
 *
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd
 * Author: Rocky Hao <rocky.hao@rock-chips.com>
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
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>
#include <linux/timer.h>
#include <linux/nvmem-consumer.h>
#include <linux/backlight.h>
#include <linux/cpufreq.h>
#include <linux/power_supply.h>
#include <linux/clk-provider.h>
#include <dt-bindings/clock/rk3128-cru.h>

#define GPU_TEMP_COMPENSION			(6000)
#define VPU_TEMP_COMPENSION			(3000)

#define LOWEST_TEMP				(-273000)

#define BASE					(1024)
#define BASE_SHIFT				(10)
#define START_DEBOUNCE_COUNT			(100)
#define HIGHER_DEBOUNCE_TEMP			(30000)
#define LOWER_DEBOUNCE_TEMP			(15000)

#define LEAKAGE_INVALID				(0xff)
/*20ms as the unit, 60000 * 20ms = 20mins */
#define TEMP_STABLE_TIME			(60000)

#define MINIMAL_DISCHARGE_CURRENT		(-200000)
#define LOWEST_WORKING_TEMP			(-40000)

static unsigned int logout;
module_param(logout, int, 0644);
MODULE_PARM_DESC(logout, "switch to control logout or not");

struct temp_frequency_entry {
	unsigned int frequency;
	s32 time2temp[2];
	int time_bound;
	s32 time2temp2[2];
	int min_temp;
	int stable_temp;

	s32 temp2time[2];
	int temp_bound;
	s32 temp2time2[2];
};

static const struct temp_frequency_entry rk3126_table[] = {
	{400000, {18, 446167,}, 6000, {2, 541167,}, 44616, 69000, {555, -23865},
	 56000, {5000, -272785},},
	{816000, {18, 496167,}, 6000, {2, 591167,}, 49616, 74000, {555, -26640},
	 61000, {5000, -297785},},
	{912000, {21, 525167,}, 6000, {2, 639167,}, 52516, 80000, {476, -25007},
	 65000, {5000, -319067},},
	{1008000, {22, 563500,}, 6000, {3, 677500,}, 56350, 100000,
	 {454, -25613}, 70000, {3333, -227143},},
	{1104000, {33, 570000,}, 6000, {5, 738000,}, 57000, 109000,
	 {303, -17272}, 77000, {2000, -147941},},
	{1200000, {35, 620167,}, 6000, {5, 800167,}, 61016, 113000,
	 {285, -17719}, 83000, {2000, -160064},},
	{CPUFREQ_TABLE_END, {0, 0,}, 0, {0, 0,}, 0, 0, {0, 0,}, 0, {0, 0,} },
};

struct thermal_tuning_info {
	int load_slope;
	int load_intercept;

	int lkg_slope;
	int lkg_intercept;

	int cur_slope;
	int cur_intercept;

	int bn_slope;
	int bn_intercept;
	int bn_offsite;

	int vpu_slope;
	int gpu_slope;
	const struct temp_frequency_entry *map_entries;

	int vpu_ajust;
	int gpu_ajust;

	int fusing_step;
};

static const struct thermal_tuning_info rk3126_tuning_info = {
	.load_slope = 102,
	.load_intercept = 61800,

	.lkg_slope = 107,
	.lkg_intercept = 4713,

	.cur_slope = 42,
	.cur_intercept = 32661,

	.bn_slope = 1517,
	.bn_intercept = 199353,
	.bn_offsite = 262000,

	.vpu_slope = 5,
	.gpu_slope = 5,

	.map_entries = rk3126_table,

	.vpu_ajust = GPU_TEMP_COMPENSION,
	.gpu_ajust = VPU_TEMP_COMPENSION,

	.fusing_step = 2,
};

struct virtual_thermal_data {
	struct platform_device *pdev;
	struct device *dev;
	struct thermal_zone_device *tzd;
	struct power_supply *psy_bat;
	struct power_supply *psy_usb;
	struct power_supply *psy_ac;
	struct cpufreq_freqs current_freq;
	const struct temp_frequency_entry *temp_freq;
	int cmp_lkg_temp;
	int sigma_time_20ms;
	struct kobject virtual_thermal_kobj;
	struct thermal_tuning_info *tuning_info;
};

static struct platform_device *platform_dev;

static int get_temp_by_freq_time(unsigned int freq, int time_20ms)
{
	struct virtual_thermal_data *ctx = platform_get_drvdata(platform_dev);

	const struct temp_frequency_entry *table = ctx->tuning_info->map_entries;

	int i = 0;
	int milli_deg = 0;

	for (i = 0; table[i].frequency != CPUFREQ_TABLE_END; i++) {
		if (freq < table[i].frequency) {
			ctx->temp_freq = &table[i];
			break;
		}
	}
	if (table[i].frequency == CPUFREQ_TABLE_END)
		ctx->temp_freq = &table[i - 1];

	if (time_20ms > TEMP_STABLE_TIME)
		return ctx->temp_freq->stable_temp;

	if (time_20ms < ctx->temp_freq->time_bound)
		milli_deg =
		    time_20ms * ctx->temp_freq->time2temp[0] +
		    ctx->temp_freq->time2temp[1];
	else
		milli_deg =
		    time_20ms * ctx->temp_freq->time2temp2[0] +
		    ctx->temp_freq->time2temp2[1];

	if (logout)
		dev_info(&platform_dev->dev, "current freq: %u stable_temp: %d milli_deg %d\n",
			 freq, ctx->temp_freq->stable_temp, milli_deg / 10);

	return milli_deg / 10;
}

static int get_time_by_temp(int milli_deg)
{
	int time_20ms = 0;
	int deg = milli_deg / 1000;

	struct virtual_thermal_data *ctx = platform_get_drvdata(platform_dev);

	if (milli_deg > ctx->temp_freq->stable_temp)
		return TEMP_STABLE_TIME;

	if (milli_deg < ctx->temp_freq->temp_bound) {
		time_20ms =
		    deg * ctx->temp_freq->temp2time[0] +
		    ctx->temp_freq->temp2time[1];
	} else {
		time_20ms =
		    deg * ctx->temp_freq->temp2time2[0] +
		    ctx->temp_freq->temp2time2[1];
	}

	if (logout)
		dev_info(&platform_dev->dev, "estimate time %d, by milli_deg %d\n",
			 time_20ms, milli_deg);

	return max(time_20ms, 0);
}

static u32 get_load(int cpu, int cpu_idx)
{
	static u64 time_in_idle[NR_CPUS] = { 0 };
	static u64 time_in_idle_timestamp[NR_CPUS] = { 0 };

	u32 load;
	u64 now, now_idle, delta_time, delta_idle;

	now_idle = get_cpu_idle_time(cpu, &now, 0);
	delta_idle = now_idle - time_in_idle[cpu_idx];
	delta_time = now - time_in_idle_timestamp[cpu_idx];

	if (delta_time <= delta_idle)
		load = 0;
	else
		load = div64_u64(100 * (delta_time - delta_idle), delta_time);

	time_in_idle[cpu_idx] = now_idle;
	time_in_idle_timestamp[cpu_idx] = now;

	return load;
}

static int get_all_load(void)
{
	u32 total_load = 0;
	int cpu;
	int i = 0;

	for_each_online_cpu(cpu) {
		u32 load;

		load = get_load(cpu, i);
		total_load += load;
		if (logout)
			dev_info(&platform_dev->dev, "cpu %d, load %d\n", i,
				 load);

		i++;
	}
	if (logout)
		dev_info(&platform_dev->dev, "total cpu load %d\n", total_load);

	return total_load;
}

static int predict_normal_temp(int milli_deg)
{
	int cov_q = 18;
	int cov_r = 542;

	int gain;
	int temp_mid;
	int temp_now;
	int prob_mid;
	int prob_now;
	static int temp_last = 50000;
	static int prob_last = 20;
	static int bounding_cnt;

	if (bounding_cnt++ > START_DEBOUNCE_COUNT) {
		bounding_cnt = START_DEBOUNCE_COUNT;
		if (milli_deg - temp_last > HIGHER_DEBOUNCE_TEMP)
			milli_deg = temp_last + HIGHER_DEBOUNCE_TEMP / 3;
		if (temp_last - milli_deg > LOWER_DEBOUNCE_TEMP)
			milli_deg = temp_last - LOWER_DEBOUNCE_TEMP / 3;
	}

	temp_mid = temp_last;
	prob_mid = prob_last + cov_q;
	gain = (prob_mid * BASE) / (prob_mid + cov_r);

	temp_now = temp_mid + (gain * (milli_deg - temp_mid) >> BASE_SHIFT);
	prob_now = ((BASE - gain) * prob_mid) >> BASE_SHIFT;

	prob_last = prob_now;
	temp_last = temp_now;

	return temp_last;
}

static int predict_cur_temp(int milli_cur_temp)
{
	int cov_q = 18;
	int cov_r = 542;

	int gain;
	int temp_mid;
	int temp_now;
	int prob_mid;
	int prob_now;
	static int cur_last = 50000;
	static int prob_last = 20;
	static int bounding_cnt;

	if (bounding_cnt++ > START_DEBOUNCE_COUNT) {
		bounding_cnt = START_DEBOUNCE_COUNT;
		if (milli_cur_temp - cur_last > HIGHER_DEBOUNCE_TEMP)
			milli_cur_temp = cur_last + HIGHER_DEBOUNCE_TEMP / 3;
		if (cur_last - milli_cur_temp > LOWER_DEBOUNCE_TEMP)
			milli_cur_temp = cur_last - LOWER_DEBOUNCE_TEMP / 3;
	}

	temp_mid = cur_last;
	prob_mid = prob_last + cov_q;
	gain = (prob_mid * BASE) / (prob_mid + cov_r);

	temp_now =
	    temp_mid + (gain * (milli_cur_temp - temp_mid) >> BASE_SHIFT);
	prob_now = ((BASE - gain) * prob_mid) >> BASE_SHIFT;

	prob_last = prob_now;
	cur_last = temp_now;

	return cur_last;
}

static void update_counting_time(void)
{
	struct virtual_thermal_data *ctx = platform_get_drvdata(platform_dev);
	static ktime_t delta_last;
	ktime_t delta;
	unsigned long long duration;
	ktime_t timestamp = ktime_get();

	delta = ktime_sub(timestamp, delta_last);
	duration = (unsigned long long)ktime_to_ns(delta) >> 20;
	delta_last = timestamp;

	if (duration < TEMP_STABLE_TIME)
		ctx->sigma_time_20ms += div64_u64(duration, 20);
	else
		ctx->sigma_time_20ms = 0;

	if (logout)
		dev_info(&platform_dev->dev, "sigma heating time %d\n",
			 ctx->sigma_time_20ms);
}

static s64 update_working_time_for_gpu_vpu(void)
{
	static ktime_t last_timestamp;
	ktime_t delta;
	s64 duration;
	ktime_t timestamp = ktime_get();

	delta = ktime_sub(timestamp, last_timestamp);
	duration = (long long)ktime_to_ns(delta) >> 20;
	last_timestamp = timestamp;
	duration = div64_s64(duration, 20);
	return duration;
}

static struct clk *clk_get_by_name(const char *clk_name)
{
	const char *name;
	struct clk *clk;
	struct device_node *np;
	struct of_phandle_args clkspec;
	int i;

	np = of_find_node_by_name(NULL, "clock-controller");
	clkspec.np = np;
	clkspec.args_count = 1;
	for (i = 1; i < CLK_NR_CLKS; i++) {
		clkspec.args[0] = i;
		clk = of_clk_get_from_provider(&clkspec);
		if (IS_ERR_OR_NULL(clk))
			continue;
		name = __clk_get_name(clk);
		if (strlen(name) != strlen(clk_name))
			continue;
		if (!strncmp(name, clk_name, strlen(clk_name)))
			break;
	}

	if (i == CLK_NR_CLKS)
		clk = NULL;

	return clk;
}

static int get_actual_brightness(void)
{
	struct backlight_device *bd;

	struct device_node *np;
	int brightness;

	np = of_find_node_by_name(NULL, "backlight");
	if (!np)
		return 0;
	bd = of_find_backlight_by_node(np);

	if (!bd)
		return 0;

	mutex_lock(&bd->ops_lock);
	if (bd->ops && bd->ops->get_brightness)
		brightness = bd->ops->get_brightness(bd);
	else
		brightness = bd->props.brightness;

	mutex_unlock(&bd->ops_lock);

	return brightness;
}

static int compensate_brightness(int cur)
{
	struct virtual_thermal_data *ctx = platform_get_drvdata(platform_dev);

	int slope = ctx->tuning_info->bn_slope;
	int intercept = ctx->tuning_info->bn_slope;
	int offsite = ctx->tuning_info->bn_offsite;

	int brightness;
	int cur_ajust = 0;

	brightness = get_actual_brightness();

	if (brightness == 0)
		cur_ajust = cur - offsite;
	else if (brightness > 0)
		cur_ajust = cur - intercept + brightness * slope;

	if (logout)
		dev_info(&platform_dev->dev, "brightness %d cur %d cur_ajust %d\n",
			 brightness, cur, cur_ajust);

	return cur_ajust;
}

static int rockchip_get_efuse_value(struct device_node *np, char *porp_name,
				    int *value)
{
	struct nvmem_cell *cell;
	unsigned char *buf;
	size_t len;

	cell = of_nvmem_cell_get(np, porp_name);
	if (IS_ERR(cell))
		return PTR_ERR(cell);

	buf = (unsigned char *)nvmem_cell_read(cell, &len);

	nvmem_cell_put(cell);

	if (IS_ERR(buf))
		return PTR_ERR(buf);

	if (buf[0] == LEAKAGE_INVALID) {
		kfree(buf);
		return -EINVAL;
	}

	*value = buf[0];

	kfree(buf);

	return 0;
}

static int ajust_temp_on_gpu_vpu(int temp)
{
	struct virtual_thermal_data *ctx = platform_get_drvdata(platform_dev);

	int vpu_slope = ctx->tuning_info->vpu_slope;
	int gpu_slope = ctx->tuning_info->gpu_slope;
	int vpu_ajust = ctx->tuning_info->vpu_ajust;
	int gpu_ajust = ctx->tuning_info->gpu_ajust;

	int delta_gpu_temp = 0;
	int delta_vpu_temp = 0;
	int gpu_enabled = 0;
	int vpu_enabled = 0;
	struct clk *clk;
	int delta;
	static int sigma_vpu_20ms;
	static int sigma_gpu_20ms;

	delta = (int)update_working_time_for_gpu_vpu();
	clk = clk_get_by_name("aclk_gpu");

	if (!IS_ERR(clk) && __clk_is_enabled(clk)) {
		gpu_enabled = 1;
		sigma_gpu_20ms -= delta;
		sigma_gpu_20ms = max(sigma_gpu_20ms, 0);
	} else {
		sigma_gpu_20ms += delta;
	}

	clk = clk_get_by_name("aclk_vdpu");

	if (!IS_ERR(clk) && __clk_is_enabled(clk)) {
		vpu_enabled = 1;
		sigma_vpu_20ms -= delta;
		sigma_vpu_20ms = max(sigma_vpu_20ms, 0);

	} else {
		sigma_vpu_20ms += delta;
	}

	delta_gpu_temp = sigma_gpu_20ms * gpu_slope;
	delta_vpu_temp = sigma_vpu_20ms * vpu_slope;

	if (delta_gpu_temp > gpu_ajust) {
		delta_gpu_temp = gpu_ajust;
		sigma_gpu_20ms = gpu_ajust / gpu_slope;
	}

	if (delta_vpu_temp > vpu_ajust) {
		delta_vpu_temp = vpu_ajust;
		sigma_vpu_20ms = vpu_ajust / vpu_slope;
	}

	if (logout)
		dev_info(&platform_dev->dev, "temp %d delta_vpu_temp %d delta_vpu_temp %d\n",
			 temp, delta_vpu_temp, delta_vpu_temp);

	temp = temp - delta_gpu_temp - delta_vpu_temp;

	return temp;
}

static int ps_get_cur_current(struct power_supply *psy, int *power_cur)
{
	union power_supply_propval val;
	int ret;

	ret = psy->desc->get_property(psy, POWER_SUPPLY_PROP_CURRENT_NOW, &val);
	if (!ret)
		*power_cur = val.intval;

	return ret;
}

static int map_temp_from_current(int cur)
{
	struct virtual_thermal_data *ctx = platform_get_drvdata(platform_dev);

	int slope = ctx->tuning_info->cur_slope;
	int intercept = ctx->tuning_info->cur_intercept;

	int milli_degree = cur * slope + intercept;

	milli_degree = predict_cur_temp(milli_degree);
	return milli_degree;
}

static int get_temp_by_current(void)
{
	int cur = 0;
	int temp = LOWEST_TEMP;
	int ret = -1;

	struct virtual_thermal_data *ctx = platform_get_drvdata(platform_dev);

	if (ctx->psy_bat)
		ret = ps_get_cur_current(ctx->psy_bat, &cur);

	if (ret)
		return temp;

	cur = compensate_brightness(cur);

	if (cur < MINIMAL_DISCHARGE_CURRENT) {
		cur = -cur;
		temp = map_temp_from_current(cur / 1000);
	}

	return temp;
}

static int ajudt_temp_by_load(int temp_delta)
{
	struct virtual_thermal_data *ctx = platform_get_drvdata(platform_dev);

	int slope = ctx->tuning_info->load_slope;
	int intercept = ctx->tuning_info->load_intercept;

	int load_rate;
	int total_load = 0;
	int temp_delta_ajust;

	total_load = get_all_load();

	load_rate = (total_load * slope + intercept) / 1000;

	load_rate = min(load_rate, 100);

	if (temp_delta > 0)
		temp_delta_ajust = temp_delta * load_rate / 100;
	else
		temp_delta_ajust = temp_delta * 100 / load_rate;

	if (logout)
		dev_info(&platform_dev->dev, "temp_delta %d load_rate %d temp_delta_ajust %d\n",
			 temp_delta, load_rate, temp_delta_ajust);

	return temp_delta_ajust;
}

static int is_charger_pluged_in(void)
{
	union power_supply_propval val;
	int ret = 0;
	struct virtual_thermal_data *ctx = platform_get_drvdata(platform_dev);

	struct power_supply *psy_usb = ctx->psy_usb;
	struct power_supply *psy_ac = ctx->psy_ac;

	if (psy_usb && psy_usb->desc && psy_usb->desc->get_property) {
		ret = psy_usb->desc->get_property(psy_usb,
						  POWER_SUPPLY_PROP_ONLINE,
						  &val);
		if (!ret && val.intval)
			return 1;
	}
	if (psy_ac && psy_ac->desc && psy_ac->desc->get_property) {
		ret = psy_ac->desc->get_property(psy_ac,
						 POWER_SUPPLY_PROP_ONLINE,
						 &val);
		if (!ret && val.intval)
			return 1;
	}
	return 0;
}

static int estimate_temp_internal(void)
{
	int temp = 0;
	static int last_temp = LOWEST_TEMP;
	int temp_delta;

	struct virtual_thermal_data *ctx = platform_get_drvdata(platform_dev);

	struct cpufreq_freqs *current_freq = &ctx->current_freq;

	update_counting_time();

	temp = get_temp_by_freq_time(current_freq->new, ctx->sigma_time_20ms);

	temp = ajust_temp_on_gpu_vpu(temp);

	if (last_temp == LOWEST_TEMP)
		temp_delta = 0;
	else
		temp_delta = temp - last_temp;

	temp_delta = ajudt_temp_by_load(temp_delta);

	if (last_temp != LOWEST_TEMP)
		temp = last_temp + temp_delta;

	last_temp = temp;

	temp = clamp(temp, ctx->temp_freq->min_temp, ctx->temp_freq->stable_temp);

	temp += ctx->cmp_lkg_temp;

	temp = predict_normal_temp(temp);

	ctx->sigma_time_20ms = get_time_by_temp(temp);

	if (logout)
		dev_info(&platform_dev->dev, "Temp1 %d cmp_lkg_temp %d sigma %d\n",
			 temp, ctx->cmp_lkg_temp, ctx->sigma_time_20ms);

	if (!is_charger_pluged_in()) {
		int temp_from_current = 0;
		int fusion_diff = 0;
		int fusing_step = ctx->tuning_info->fusing_step;

		temp_from_current = get_temp_by_current();
		if (temp_from_current > LOWEST_WORKING_TEMP) {
			fusion_diff = temp_from_current - temp;
			temp = temp + fusion_diff / fusing_step;
			ctx->sigma_time_20ms = get_time_by_temp(temp);
			if (logout)
				dev_info(&platform_dev->dev, "Temp2 %d temp_from_current %d sigma %d\n",
					 temp, temp_from_current,
					 ctx->sigma_time_20ms);
		}
	}
	return temp;
}

static int virtual_thermal_set_trips(void *_sensor, int low, int high)
{
	return 0;
}

static int virtual_thermal_get_temp(void *_sensor, int *out_temp)
{
	*out_temp = estimate_temp_internal();
	return 0;
}

static const struct thermal_zone_of_device_ops virtual_of_thermal_ops = {
	.get_temp = virtual_thermal_get_temp,
	.set_trips = virtual_thermal_set_trips,
};

static const struct of_device_id of_virtual_thermal_match[] = {
	{
	 .compatible = "rockchip,rk3126-tsadc-virtual",
	 .data = (void *)&rk3126_tuning_info,
	 },

	{ /* end */ },
};

MODULE_DEVICE_TABLE(of, of_virtual_thermal_match);

static int temp_interactive_notifier(struct notifier_block *nb,
				     unsigned long val, void *data)
{
	struct cpufreq_freqs *freq = data;
	struct virtual_thermal_data *ctx = platform_get_drvdata(platform_dev);

	if (!ctx)
		return 0;
	if (val == CPUFREQ_POSTCHANGE) {
		ctx->current_freq.new = freq->new;
		ctx->current_freq.old = freq->old;
	}
	return 0;
}

static struct notifier_block temp_notifier_block = {
	.notifier_call = temp_interactive_notifier,
};

static int compensate_leakage(int lkg)
{
	struct virtual_thermal_data *ctx = platform_get_drvdata(platform_dev);

	int slope = ctx->tuning_info->lkg_slope;
	int intercept = ctx->tuning_info->lkg_slope;

	int milli_degree = 0;

	if (lkg == 0)
		milli_degree = 0;
	else
		milli_degree = slope * lkg - intercept;

	return milli_degree;
}

void dump_virtual_temperature(void)
{
	struct virtual_thermal_data *ctx = platform_get_drvdata(platform_dev);

	struct thermal_zone_device *tz = ctx->tzd;

	if (tz->temperature != THERMAL_TEMP_INVALID)
		dev_warn(&platform_dev->dev, "virtual temperature(%d C)\n",
			 tz->temperature / 1000);
}
EXPORT_SYMBOL_GPL(dump_virtual_temperature);

static int virtual_thermal_panic(struct notifier_block *this,
				 unsigned long ev, void *ptr)
{
	dump_virtual_temperature();
	return NOTIFY_DONE;
}

static struct notifier_block virtual_thermal_panic_block = {
	.notifier_call = virtual_thermal_panic,
};

static int virtual_thermal_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	int ret;
	int leakage = 0;
	struct virtual_thermal_data *ctx;

	const struct of_device_id *match;

	match = of_match_node(of_virtual_thermal_match, np);
	if (!match)
		return -ENXIO;

	ctx = devm_kzalloc(&pdev->dev, sizeof(struct virtual_thermal_data),
			   GFP_KERNEL);

	ctx->pdev = pdev;
	ctx->dev = &pdev->dev;
	platform_set_drvdata(pdev, ctx);

	platform_dev = pdev;

	ctx->tuning_info = (struct thermal_tuning_info *)match->data;
	if (!ctx->tuning_info) {
		dev_err(&pdev->dev,
			"failed to allocate memory for tuning info.\n");
		return -EINVAL;
	}

	ret = rockchip_get_efuse_value(np, "cpu_leakage", &leakage);
	if (!ret)
		dev_info(&pdev->dev, "leakage=%d\n", leakage);

	ctx->cmp_lkg_temp = compensate_leakage(leakage);

	ctx->psy_bat = power_supply_get_by_name("battery");
	ctx->psy_usb = power_supply_get_by_name("usb");
	ctx->psy_ac = power_supply_get_by_name("ac");

	ret = cpufreq_register_notifier(&temp_notifier_block,
					CPUFREQ_TRANSITION_NOTIFIER);
	if (ret) {
		dev_err(&pdev->dev, "failed to register cpufreq notifier: %d\n",
			ret);
		return ret;
	}

	ctx->tzd = devm_thermal_zone_of_sensor_register(&pdev->dev, 0,
						NULL,
						&virtual_of_thermal_ops);
	if (IS_ERR(ctx->tzd)) {
		ret = PTR_ERR(ctx->tzd);
		dev_err(&pdev->dev, "failed to register sensor 0: %d\n", ret);
		goto err_unreg_cpufreq_notifier;
	}

	ret = atomic_notifier_chain_register(&panic_notifier_list,
					     &virtual_thermal_panic_block);
	if (ret) {
		dev_err(&pdev->dev, "failed to register panic notifier: %d\n",
			ret);
		goto err_unreg_cpufreq_notifier;
	}

	dev_info(&pdev->dev, "virtual tsadc probed successfully\n");

	return 0;

err_unreg_cpufreq_notifier:
	cpufreq_unregister_notifier(&temp_notifier_block,
				    CPUFREQ_TRANSITION_NOTIFIER);
	return ret;
}

static int virtual_thermal_remove(struct platform_device *pdev)
{
	atomic_notifier_chain_unregister(&panic_notifier_list,
					 &virtual_thermal_panic_block);
	cpufreq_unregister_notifier(&temp_notifier_block,
				    CPUFREQ_TRANSITION_NOTIFIER);
	return 0;
}

static struct platform_driver virtual_thermal_driver = {
	.driver = {
		   .name = "virtual-thermal",
		   .of_match_table = of_virtual_thermal_match,
		   },
	.probe = virtual_thermal_probe,
	.remove = virtual_thermal_remove,
};

static int __init virtual_thermal_init_driver(void)
{
	return platform_driver_register(&virtual_thermal_driver);
}

late_initcall(virtual_thermal_init_driver);

MODULE_DESCRIPTION("ROCKCHIP THERMAL Driver");
MODULE_AUTHOR("Rockchip, Inc.");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:virtual-thermal");
