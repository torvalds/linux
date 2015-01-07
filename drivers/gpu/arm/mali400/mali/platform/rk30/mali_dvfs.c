/*
 * Rockchip SoC Mali-450 DVFS driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software FoundatIon.
 */

#include "mali_platform.h"
#include "mali_dvfs.h"

#define level0_min 0
#define level0_max 70
#define levelf_max 100

#define mali_dividend 7
#define mali_fix_float(a) ((((a)*mali_dividend)%10)?((((a)*mali_dividend)/10)+1):(((a)*mali_dividend)/10))

#define work_to_dvfs(w) container_of(w, struct mali_dvfs, work)
#define dvfs_to_drv_data(dvfs) container_of(dvfs, struct mali_platform_drv_data, dvfs)

static void mali_dvfs_event_proc(struct work_struct *w)
{
	struct mali_dvfs *dvfs = work_to_dvfs(w);
	struct mali_platform_drv_data *drv_data = dvfs_to_drv_data(dvfs);
	unsigned int utilisation = dvfs->utilisation;
	unsigned int level = dvfs->current_level;
	const struct mali_fv_info *threshold = &drv_data->fv_info[level];
	int ret;

	utilisation = utilisation * 100 / 256;

	// dev_dbg(drv_data->dev, "utilisation percent = %d\n", utilisation);

	if (utilisation > threshold->max &&
	    level < drv_data->fv_info_length - 1 - 1)
		level += 1;
	else if (level > 0 && utilisation < threshold->min)
		level -= 1;
	else
		return;

	dev_dbg(drv_data->dev, "Setting dvfs level %u: freq = %lu Hz\n",
		level, drv_data->fv_info[level].freq);

	ret = mali_set_level(drv_data->dev, level);
	if (ret) {
		dev_err(drv_data->dev, "set freq error, %d", ret);
		return;
	}
}

bool mali_dvfs_is_enabled(struct device *dev)
{
	struct mali_platform_drv_data *drv_data = dev_get_drvdata(dev);
	struct mali_dvfs *dvfs = &drv_data->dvfs;

	return dvfs->enabled;
}

void mali_dvfs_enable(struct device *dev)
{
	struct mali_platform_drv_data *drv_data = dev_get_drvdata(dev);
	struct mali_dvfs *dvfs = &drv_data->dvfs;

	dvfs->enabled = true;
}

void mali_dvfs_disable(struct device *dev)
{
	struct mali_platform_drv_data *drv_data = dev_get_drvdata(dev);
	struct mali_dvfs *dvfs = &drv_data->dvfs;

	dvfs->enabled = false;
	cancel_work_sync(&dvfs->work);
}

unsigned int mali_dvfs_utilisation(struct device *dev)
{
	struct mali_platform_drv_data *drv_data = dev_get_drvdata(dev);
	struct mali_dvfs *dvfs = &drv_data->dvfs;

	return dvfs->utilisation;
}

int mali_dvfs_event(struct device *dev, u32 utilisation)
{
	struct mali_platform_drv_data *drv_data = dev_get_drvdata(dev);
	struct mali_dvfs *dvfs = &drv_data->dvfs;

	dvfs->utilisation = utilisation;

	if (dvfs->enabled)
		schedule_work(&dvfs->work);

	return MALI_TRUE;
}
static void mali_dvfs_threshold(u32 div, struct mali_platform_drv_data *drv_data)
{
	int length = drv_data->fv_info_length;
	u32 pre_level;
	u32 tmp;
	int level;

	for (level = 0; level < length; level++) {
		if (level == 0) {
			drv_data->fv_info[level].min = level0_min;
			if (length == 1)
				drv_data->fv_info[level].max = levelf_max;
			else
				drv_data->fv_info[level].max = level0_max;
		} else {
			pre_level = level - 1;
			if (level == length - 1)
				drv_data->fv_info[level].max = levelf_max;
			else
				drv_data->fv_info[level].max = drv_data->fv_info[pre_level].max + div;

			drv_data->fv_info[level].min = drv_data->fv_info[pre_level].max *
						       drv_data->fv_info[pre_level].freq / drv_data->fv_info[level].freq;

			tmp = drv_data->fv_info[level].max - drv_data->fv_info[level].min;
			drv_data->fv_info[level].min += mali_fix_float(tmp);
		}

		dev_info(drv_data->dev, "freq: %lu, min_threshold: %d, max_threshold: %d\n",
			drv_data->fv_info[level].freq,
			drv_data->fv_info[level].min,
			drv_data->fv_info[level].max);
	}
}

int mali_dvfs_init(struct device *dev)
{
	struct mali_platform_drv_data *drv_data = dev_get_drvdata(dev);
	struct mali_dvfs *dvfs = &drv_data->dvfs;
	struct cpufreq_frequency_table *freq_table;
	int i = 0;
	int div_dvfs;
	int ret;

	freq_table = dvfs_get_freq_volt_table(drv_data->clk);
	if (!freq_table) {
		dev_err(dev, "Can't find dvfs table in dts\n");
		return -1;
	}

	while (freq_table[i].frequency != CPUFREQ_TABLE_END) {
		drv_data->fv_info_length++;
		i++;
	}

	drv_data->fv_info = devm_kcalloc(dev, drv_data->fv_info_length,
					 sizeof(*drv_data->fv_info),
					 GFP_KERNEL);
	if (!drv_data->fv_info)
		return -ENOMEM;

	for (i = 0; i < drv_data->fv_info_length; i++)
		drv_data->fv_info[i].freq = freq_table[i].frequency * 1000;

	if(drv_data->fv_info_length > 1)
		div_dvfs = round_up(((levelf_max - level0_max) /
				    (drv_data->fv_info_length-1)), 1);

	mali_dvfs_threshold(div_dvfs, drv_data);

	ret = dvfs_clk_set_rate(drv_data->clk, drv_data->fv_info[0].freq);
	if (ret)
		return ret;

	drv_data->dvfs.current_level = 0;

	dev_info(dev, "initial freq = %lu\n",
			 dvfs_clk_get_rate(drv_data->clk));

	INIT_WORK(&dvfs->work, mali_dvfs_event_proc);
	dvfs->enabled = true;

	return 0;
}

void mali_dvfs_term(struct device *dev)
{
	struct mali_platform_drv_data *drv_data = dev_get_drvdata(dev);
	struct mali_dvfs *dvfs = &drv_data->dvfs;

	dvfs->enabled = false;
	cancel_work_sync(&dvfs->work);
}
