/**
 * Rockchip SoC Mali-450 DVFS driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software FoundatIon.
 */


/* #define ENABLE_DEBUG_LOG */
#include "custom_log.h"

#include "mali_platform.h"
#include "mali_dvfs.h"

/*---------------------------------------------------------------------------*/

/**
 * 若 count_of_continuous_requests_to_jump_up_in_dvfs_level_table 达到 value,
 * 将触发一次 current_dvfs_level jump_up.
 */
#define	NUM_OF_CONTINUOUS_REQUESTS_TO_TRIGGER_REAL_JUMPING_UP		(1)
/**
 * 若 count_of_continuous_requests_to_jump_down_in_dvfs_level_table 达到 value,
 * 将触发一次 current_dvfs_level jump_down.
 */
#define NUM_OF_CONTINUOUS_REQUESTS_TO_TRIGGER_REAL_JUMPING_DOWN		(30)

#define level0_min 0
#define level0_max 70
#define levelf_max 100

#define mali_dividend 7
#define mali_fix_float(a) \
	((((a) * mali_dividend) % 10) \
		? ((((a) * mali_dividend) / 10) + 1) \
		: (((a) * mali_dividend) / 10))

#define work_to_dvfs(w) container_of(w, struct mali_dvfs, work)
#define dvfs_to_drv_data(dvfs) \
	container_of(dvfs, struct mali_platform_drv_data, dvfs)

/*---------------------------------------------------------------------------*/
#if 0
static bool mali_dvfs_should_jump_up(const struct mali_dvfs *p_dvfs)
{
	return (p_dvfs->m_count_of_requests_to_jump_up
		>= NUM_OF_CONTINUOUS_REQUESTS_TO_TRIGGER_REAL_JUMPING_UP);
}

static bool mali_dvfs_should_jump_down(const struct mali_dvfs *p_dvfs)
{
	return (p_dvfs->m_count_of_requests_to_jump_down
		>= NUM_OF_CONTINUOUS_REQUESTS_TO_TRIGGER_REAL_JUMPING_DOWN);
}
#endif
/**
 * .KP :
 * work_to_handle_mali_utilization_event 的实现主体,
 * 将根据 current_mali_utilization_data, 改变 current_dvfs_level.
 */
 #if 0
static void mali_dvfs_event_proc(struct work_struct *w)
{
	struct mali_dvfs *dvfs = work_to_dvfs(w);/* mali_dvfs_context. */
	struct mali_platform_drv_data *drv_data = dvfs_to_drv_data(dvfs);
	unsigned int utilisation = dvfs->utilisation;
	int ret = 0;
	/* index_of_current_dvfs_level. */
	unsigned int level = dvfs->current_level;
	/* current_dvfs_level. */
	const struct mali_fv_info *threshold = &drv_data->fv_info[level];
	/* utilization_in_percentage. */
	utilisation = utilisation * 100 / 256;

	V("mali_utilization_in_percentage : %d; index_of_current_level : %d;"
			"min: %d, max : %d",
	  utilisation,
	  level,
	  threshold->min,
	  threshold->max);
	V("num_of_requests_to_jump_up : %d; num_of_requests_to_jump_down : %d",
	  dvfs->m_count_of_requests_to_jump_up,
	  dvfs->m_count_of_requests_to_jump_down);

	if (utilisation > threshold->max) {
		dvfs->m_count_of_requests_to_jump_down = 0;

		/* 若 current_dvfs_level 还 "有" 上跳的空间, 则... */
		if (level < (drv_data->fv_info_length - 1)) {
			V("to count one request_to_jump_up.");
			dvfs->m_count_of_requests_to_jump_up++;

			/* 若足够触发一次 real_jump_up, 则... */
			if (mali_dvfs_should_jump_up(dvfs)) {
				V("to preset real_jump_up.");
				level += 1;

				dvfs->m_count_of_requests_to_jump_up = 0;

				goto MAKE_DVFS_LEVEL_UPDATE_EFFECTIVE;
			}
			/* 否则, 即还 "不" 足以 real_jump_up, 则... */
			else
				return;
		}
		/* 否则, 即 "没有" 上跳的空间了, 则... */
		else
			return;
	} else if (utilisation < (threshold->min)) {
		dvfs->m_count_of_requests_to_jump_up = 0;

		/* 若 current_dvfs_level 还有 下跳的空间, 则... */
		if (level > 0) {
			V("to count one request_to_jump_down.");
			dvfs->m_count_of_requests_to_jump_down++;

			/* 若足够触发一次 real_jump_down, 则... */
			if (mali_dvfs_should_jump_down(dvfs)) {
				V("to preset real_jump_down.");
				level -= 1;

				dvfs->m_count_of_requests_to_jump_down = 0;

				goto MAKE_DVFS_LEVEL_UPDATE_EFFECTIVE;
			} else {
				return;
			}
		}
		/* 否则, 即 "没有" 下跳的空间了, 则... */
		else
			return;
	}
	/* 否则,
	 * 即 mali_utilization 在 'min' 和 'max' 之间,
	 * 即 current_dvfs_level 合适, 则 ... */
	else {
		dvfs->m_count_of_requests_to_jump_up = 0;
		dvfs->m_count_of_requests_to_jump_down = 0;
		return;
	}

	/*
	dev_dbg(drv_data->dev, "Setting dvfs level %u: freq = %lu Hz\n",
		level, drv_data->fv_info[level].freq);
	*/

MAKE_DVFS_LEVEL_UPDATE_EFFECTIVE:
	ret = mali_set_level(drv_data->dev, level);
	if (ret) {
		dev_err(drv_data->dev, "set freq error, %d", ret);
		return;
	}
}
#endif
/**
 * 返回 mali_dvfs_facility 当前是否被使能.
 */
bool mali_dvfs_is_enabled(struct device *dev)
{
	struct mali_platform_drv_data *drv_data = dev_get_drvdata(dev);
	struct mali_dvfs *dvfs = &drv_data->dvfs;

	return dvfs->enabled;
}

/**
 * 使能 mali_dvfs_facility.
 */
void mali_dvfs_enable(struct device *dev)
{
	struct mali_platform_drv_data *drv_data = dev_get_drvdata(dev);
	struct mali_dvfs *dvfs = &drv_data->dvfs;

	dvfs->enabled = true;
}

/**
 * 禁用 mali_dvfs_facility.
 */
void mali_dvfs_disable(struct device *dev)
{
	struct mali_platform_drv_data *drv_data = dev_get_drvdata(dev);
	struct mali_dvfs *dvfs = &drv_data->dvfs;

	dvfs->enabled = false;
	cancel_work_sync(&dvfs->work);

	/* 使用 lowest_dvfs_level. */
	if (0 != mali_set_level(dev, 0))
		W("fail to set current_dvfs_level to index 0.");
}

/**
 * 返回当前 mali_dvfs_context 中最后记录的 mali_utilization.
 */
unsigned int mali_dvfs_utilisation(struct device *dev)
{
	struct mali_platform_drv_data *drv_data = dev_get_drvdata(dev);
	struct mali_dvfs *dvfs = &drv_data->dvfs;

	return dvfs->utilisation;
}

/**
 * 根据当前的 mali_utilization_event, 对应调整 mali_dvfs_facility.
 * 运行在 context_of_common_part_calling_back_mali_utilization_event 中.
 */
int mali_dvfs_event(struct device *dev, u32 utilisation)
{
	struct mali_platform_drv_data *drv_data = dev_get_drvdata(dev);
	struct mali_dvfs *dvfs = &drv_data->dvfs;

	dvfs->utilisation = utilisation;
	V("mali_utilization_in_percentage : %d", utilisation * 100 / 256);

#if 0
	if (dvfs->enabled) {
		/* 将 work_to_handle_mali_utilization_event,
		 * 放置到 kernel_global_workqueue, 待执行. */
		schedule_work(&dvfs->work);
	}
#endif

	return MALI_TRUE;
}
#if 0
static void mali_dvfs_threshold(u32 div,
				struct mali_platform_drv_data *drv_data)
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
				drv_data->fv_info[level].max
				= drv_data->fv_info[pre_level].max + div;

			drv_data->fv_info[level].min
			= drv_data->fv_info[pre_level].max
				* drv_data->fv_info[pre_level].freq
				/ drv_data->fv_info[level].freq;

			tmp
			= drv_data->fv_info[level].max
				- drv_data->fv_info[level].min;
			drv_data->fv_info[level].min += mali_fix_float(tmp);
		}

		dev_info(drv_data->dev, "freq: %lu, min_threshold: %d, max_threshold: %d\n",
			 drv_data->fv_info[level].freq,
			 drv_data->fv_info[level].min,
			 drv_data->fv_info[level].max);
	}
}
#endif
/*---------------------------------------------------------------------------*/

int mali_dvfs_init(struct device *dev)
{
#if 0
	struct mali_platform_drv_data *drv_data = dev_get_drvdata(dev);
	/*mali_dvfs_context. */
	struct mali_dvfs *dvfs = &drv_data->dvfs;
	/* gpu_clk_freq_table. */
	struct cpufreq_frequency_table *freq_table;
	int i = 0;
	int div_dvfs;
	int ret;

	freq_table = dvfs_get_freq_volt_table(drv_data->clk);
	if (!freq_table) {
		dev_err(dev, "Can't find dvfs table in dts\n");
		return -1;
	}

	/* 确定 len_of_avaialble_dvfs_level_list. */
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

	if (drv_data->fv_info_length > 1)
		div_dvfs
		= round_up(((levelf_max - level0_max)
				/ (drv_data->fv_info_length - 1)),
			   1);

	mali_dvfs_threshold(div_dvfs, drv_data);

	ret = dvfs_clk_set_rate(drv_data->clk, drv_data->fv_info[0].freq);
	if (ret)
		return ret;

	drv_data->dvfs.current_level = 0;

	dev_info(dev, "initial freq = %lu\n",
		 dvfs_clk_get_rate(drv_data->clk));

	INIT_WORK(&dvfs->work, mali_dvfs_event_proc);
	dvfs->enabled = true;

	dvfs->m_count_of_requests_to_jump_up = 0;
	dvfs->m_count_of_requests_to_jump_down = 0;

#endif
	return 0;
}

void mali_dvfs_term(struct device *dev)
{
#if 0
	struct mali_platform_drv_data *drv_data = dev_get_drvdata(dev);
	struct mali_dvfs *dvfs = &drv_data->dvfs;

	dvfs->m_count_of_requests_to_jump_up = 0;
	dvfs->m_count_of_requests_to_jump_down = 0;

	dvfs->enabled = false;
	cancel_work_sync(&dvfs->work);
#endif
}

