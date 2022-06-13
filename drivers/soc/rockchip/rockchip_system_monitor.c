// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 * Author: Finley Xiao <finley.xiao@rock-chips.com>
 */

#include <dt-bindings/soc/rockchip-system-status.h>
#include <linux/clk-provider.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/devfreq.h>
#include <linux/device.h>
#include <linux/fb.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/pm_qos.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/coupler.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <linux/thermal.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <soc/rockchip/rockchip_opp_select.h>
#include <soc/rockchip/rockchip_system_monitor.h>
#include <soc/rockchip/rockchip-system-status.h>

#include "../../gpu/drm/rockchip/ebc-dev/ebc_dev.h"
#include "../../opp/opp.h"
#include "../../regulator/internal.h"
#include "../../thermal/thermal_core.h"

#define CPU_REBOOT_FREQ		816000 /* kHz */
#define VIDEO_1080P_SIZE	(1920 * 1080)
#define THERMAL_POLLING_DELAY	200 /* milliseconds */

struct video_info {
	unsigned int width;
	unsigned int height;
	unsigned int ishevc;
	unsigned int videoFramerate;
	unsigned int streamBitrate;
	struct list_head node;
};

struct system_monitor_attr {
	struct attribute attr;
	ssize_t (*show)(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf);
	ssize_t (*store)(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t n);
};

struct system_monitor {
	struct device *dev;
	struct cpumask video_4k_offline_cpus;
	struct cpumask status_offline_cpus;
	struct cpumask temp_offline_cpus;
	struct cpumask offline_cpus;
	struct notifier_block status_nb;
	struct kobject *kobj;

	struct thermal_zone_device *tz;
	struct delayed_work thermal_work;
	int offline_cpus_temp;
	int temp_hysteresis;
	unsigned int delay;
	bool is_temp_offline;
};

static unsigned long system_status;
static unsigned long ref_count[32] = {0};

static DEFINE_MUTEX(system_status_mutex);
static DEFINE_MUTEX(video_info_mutex);
static DEFINE_MUTEX(cpu_on_off_mutex);

static DECLARE_RWSEM(mdev_list_sem);

static LIST_HEAD(video_info_list);
static LIST_HEAD(monitor_dev_list);
static struct system_monitor *system_monitor;
static atomic_t monitor_in_suspend;

static BLOCKING_NOTIFIER_HEAD(system_status_notifier_list);

int rockchip_register_system_status_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&system_status_notifier_list,
						nb);
}
EXPORT_SYMBOL(rockchip_register_system_status_notifier);

int rockchip_unregister_system_status_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&system_status_notifier_list,
						  nb);
}
EXPORT_SYMBOL(rockchip_unregister_system_status_notifier);

static int rockchip_system_status_notifier_call_chain(unsigned long val)
{
	int ret = blocking_notifier_call_chain(&system_status_notifier_list,
					       val, NULL);

	return notifier_to_errno(ret);
}

void rockchip_set_system_status(unsigned long status)
{
	unsigned long old_system_status;
	unsigned int single_status_offset;

	mutex_lock(&system_status_mutex);

	old_system_status = system_status;

	while (status) {
		single_status_offset = fls(status) - 1;
		status &= ~(1 << single_status_offset);
		if (ref_count[single_status_offset] == 0)
			system_status |= 1 << single_status_offset;
		ref_count[single_status_offset]++;
	}

	if (old_system_status != system_status)
		rockchip_system_status_notifier_call_chain(system_status);

	mutex_unlock(&system_status_mutex);
}
EXPORT_SYMBOL(rockchip_set_system_status);

void rockchip_clear_system_status(unsigned long status)
{
	unsigned long old_system_status;
	unsigned int single_status_offset;

	mutex_lock(&system_status_mutex);

	old_system_status = system_status;

	while (status) {
		single_status_offset = fls(status) - 1;
		status &= ~(1 << single_status_offset);
		if (ref_count[single_status_offset] == 0) {
			continue;
		} else {
			if (ref_count[single_status_offset] == 1)
				system_status &= ~(1 << single_status_offset);
			ref_count[single_status_offset]--;
		}
	}

	if (old_system_status != system_status)
		rockchip_system_status_notifier_call_chain(system_status);

	mutex_unlock(&system_status_mutex);
}
EXPORT_SYMBOL(rockchip_clear_system_status);

unsigned long rockchip_get_system_status(void)
{
	return system_status;
}
EXPORT_SYMBOL(rockchip_get_system_status);

int rockchip_add_system_status_interface(struct device *dev)
{
	if (!system_monitor || !system_monitor->kobj) {
		pr_err("failed to get system status kobj\n");
		return -EINVAL;
	}

	return compat_only_sysfs_link_entry_to_kobj(&dev->kobj,
						    system_monitor->kobj,
						    "system_status", NULL);
}
EXPORT_SYMBOL(rockchip_add_system_status_interface);

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
	char *str, *p;
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
	p = str;
	strsep(&p, ",");
	video_info->width = rockchip_get_video_param(&p);
	video_info->height = rockchip_get_video_param(&p);
	video_info->ishevc = rockchip_get_video_param(&p);
	video_info->videoFramerate = rockchip_get_video_param(&p);
	video_info->streamBitrate = rockchip_get_video_param(&p);
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

static struct video_info *rockchip_find_video_info(const char *buf)
{
	struct video_info *info, *video_info;

	video_info = rockchip_parse_video_info(buf);

	if (!video_info)
		return NULL;

	mutex_lock(&video_info_mutex);
	list_for_each_entry(info, &video_info_list, node) {
		if (info->width == video_info->width &&
		    info->height == video_info->height &&
		    info->ishevc == video_info->ishevc &&
		    info->videoFramerate == video_info->videoFramerate &&
		    info->streamBitrate == video_info->streamBitrate) {
			mutex_unlock(&video_info_mutex);
			kfree(video_info);
			return info;
		}
	}

	mutex_unlock(&video_info_mutex);
	kfree(video_info);

	return NULL;
}

static void rockchip_add_video_info(struct video_info *video_info)
{
	if (video_info) {
		mutex_lock(&video_info_mutex);
		list_add(&video_info->node, &video_info_list);
		mutex_unlock(&video_info_mutex);
	}
}

static void rockchip_del_video_info(struct video_info *video_info)
{
	if (video_info) {
		mutex_lock(&video_info_mutex);
		list_del(&video_info->node);
		mutex_unlock(&video_info_mutex);
		kfree(video_info);
	}
}

static void rockchip_update_video_info(void)
{
	struct video_info *video_info;
	unsigned int max_res = 0, max_stream_bitrate = 0, res = 0;

	mutex_lock(&video_info_mutex);
	if (list_empty(&video_info_list)) {
		mutex_unlock(&video_info_mutex);
		rockchip_clear_system_status(SYS_STATUS_VIDEO);
		return;
	}

	list_for_each_entry(video_info, &video_info_list, node) {
		res = video_info->width * video_info->height;
		if (res > max_res)
			max_res = res;
		if (video_info->streamBitrate > max_stream_bitrate)
			max_stream_bitrate = video_info->streamBitrate;
	}
	mutex_unlock(&video_info_mutex);

	if (max_res <= VIDEO_1080P_SIZE) {
		rockchip_set_system_status(SYS_STATUS_VIDEO_1080P);
	} else {
		if (max_stream_bitrate == 10)
			rockchip_set_system_status(SYS_STATUS_VIDEO_4K_10B);
		else
			rockchip_set_system_status(SYS_STATUS_VIDEO_4K);
	}
}

void rockchip_update_system_status(const char *buf)
{
	struct video_info *video_info;

	if (!buf)
		return;

	switch (buf[0]) {
	case '0':
		/* clear video flag */
		video_info = rockchip_find_video_info(buf);
		if (video_info) {
			rockchip_del_video_info(video_info);
			rockchip_update_video_info();
		}
		break;
	case '1':
		/* set video flag */
		video_info = rockchip_parse_video_info(buf);
		if (video_info) {
			rockchip_add_video_info(video_info);
			rockchip_update_video_info();
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
}
EXPORT_SYMBOL(rockchip_update_system_status);

static ssize_t status_show(struct kobject *kobj, struct kobj_attribute *attr,
			   char *buf)
{
	unsigned int status = rockchip_get_system_status();

	return sprintf(buf, "0x%x\n", status);
}

static ssize_t status_store(struct kobject *kobj, struct kobj_attribute *attr,
			    const char *buf, size_t n)
{
	if (!n)
		return -EINVAL;

	rockchip_update_system_status(buf);

	return n;
}

static struct system_monitor_attr status =
	__ATTR(system_status, 0644, status_show, status_store);

static int rockchip_get_temp_freq_table(struct device_node *np,
					char *porp_name,
					struct temp_freq_table **freq_table)
{
	struct temp_freq_table *table;
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

	table = kzalloc(sizeof(*table) * (count / 2 + 1), GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	for (i = 0; i < count / 2; i++) {
		of_property_read_u32_index(np, porp_name, 2 * i,
					   &table[i].temp);
		of_property_read_u32_index(np, porp_name, 2 * i + 1,
					   &table[i].freq);
	}
	table[i].freq = UINT_MAX;
	*freq_table = table;

	return 0;
}

static int rockchip_get_adjust_volt_table(struct device_node *np,
					  char *porp_name,
					  struct volt_adjust_table **table)
{
	struct volt_adjust_table *volt_table;
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

	if (count % 3)
		return -EINVAL;

	volt_table = kzalloc(sizeof(*volt_table) * (count / 3 + 1), GFP_KERNEL);
	if (!volt_table)
		return -ENOMEM;

	for (i = 0; i < count / 3; i++) {
		of_property_read_u32_index(np, porp_name, 3 * i,
					   &volt_table[i].min);
		of_property_read_u32_index(np, porp_name, 3 * i + 1,
					   &volt_table[i].max);
		of_property_read_u32_index(np, porp_name, 3 * i + 2,
					   &volt_table[i].volt);
	}
	volt_table[i].min = 0;
	volt_table[i].max = 0;
	volt_table[i].volt = INT_MAX;

	*table = volt_table;

	return 0;
}

static int rockchip_get_low_temp_volt(struct monitor_dev_info *info,
				      unsigned long rate, int *delta_volt)
{
	int i, ret = -EINVAL;
	unsigned int _rate = (unsigned int)(rate / 1000000);

	if (!info->low_temp_adjust_table)
		return ret;

	for (i = 0; info->low_temp_adjust_table[i].volt != INT_MAX; i++) {
		if (_rate >= info->low_temp_adjust_table[i].min &&
		    _rate <= info->low_temp_adjust_table[i].max) {
			*delta_volt = info->low_temp_adjust_table[i].volt;
			ret = 0;
		}
	}

	return ret;
}

static int rockchip_init_temp_opp_table(struct monitor_dev_info *info)
{
	struct device *dev = info->dev;
	struct opp_table *opp_table;
	struct dev_pm_opp *opp;
	int delta_volt = 0;
	int i = 0, max_count;
	unsigned long low_limit = 0, high_limit = 0;
	unsigned long low_limit_mem = 0, high_limit_mem = 0;
	bool reach_max_volt = false;
	bool reach_max_mem_volt = false;
	bool reach_high_temp_max_volt = false;
	bool reach_high_temp_max_mem_volt = false;

	max_count = dev_pm_opp_get_opp_count(dev);
	if (max_count <= 0)
		return max_count ? max_count : -ENODATA;
	info->opp_table = kzalloc(sizeof(*info->opp_table) * max_count,
				  GFP_KERNEL);
	if (!info->opp_table)
		return -ENOMEM;

	opp_table = dev_pm_opp_get_opp_table(dev);
	if (!opp_table) {
		kfree(info->opp_table);
		info->opp_table = NULL;
		return -ENOMEM;
	}
	mutex_lock(&opp_table->lock);
	list_for_each_entry(opp, &opp_table->opp_list, node) {
		if (!opp->available)
			continue;
		info->opp_table[i].rate = opp->rate;
		info->opp_table[i].volt = opp->supplies[0].u_volt;
		info->opp_table[i].max_volt = opp->supplies[0].u_volt_max;

		if (opp->supplies[0].u_volt <= info->high_temp_max_volt) {
			if (!reach_high_temp_max_volt)
				high_limit = opp->rate;
			if (opp->supplies[0].u_volt == info->high_temp_max_volt)
				reach_high_temp_max_volt = true;
		}

		if (rockchip_get_low_temp_volt(info, opp->rate, &delta_volt))
			delta_volt = 0;
		if ((opp->supplies[0].u_volt + delta_volt) <= info->max_volt) {
			info->opp_table[i].low_temp_volt =
				opp->supplies[0].u_volt + delta_volt;
			if (info->opp_table[i].low_temp_volt <
			    info->low_temp_min_volt)
				info->opp_table[i].low_temp_volt =
					info->low_temp_min_volt;
			if (!reach_max_volt)
				low_limit = opp->rate;
			if (info->opp_table[i].low_temp_volt == info->max_volt)
				reach_max_volt = true;
		} else {
			info->opp_table[i].low_temp_volt = info->max_volt;
		}
		if (low_limit && low_limit != opp->rate)
			info->low_limit = low_limit;
		if (high_limit && high_limit != opp->rate)
			info->high_limit = high_limit;

		if (opp_table->regulator_count > 1) {
			info->opp_table[i].mem_volt = opp->supplies[1].u_volt;
			info->opp_table[i].max_mem_volt = opp->supplies[1].u_volt_max;

			if (opp->supplies[1].u_volt <= info->high_temp_max_volt) {
				if (!reach_high_temp_max_mem_volt)
					high_limit_mem = opp->rate;
				if (opp->supplies[1].u_volt == info->high_temp_max_volt)
					reach_high_temp_max_mem_volt = true;
			}

			if ((opp->supplies[1].u_volt + delta_volt) <= info->max_volt) {
				info->opp_table[i].low_temp_mem_volt =
					opp->supplies[1].u_volt + delta_volt;
				if (info->opp_table[i].low_temp_mem_volt <
				    info->low_temp_min_volt)
					info->opp_table[i].low_temp_mem_volt =
						info->low_temp_min_volt;
				if (!reach_max_mem_volt)
					low_limit_mem = opp->rate;
				if (info->opp_table[i].low_temp_mem_volt == info->max_volt)
					reach_max_mem_volt = true;
			} else {
				info->opp_table[i].low_temp_mem_volt = info->max_volt;
			}

			if (low_limit_mem && low_limit_mem != opp->rate) {
				if (info->low_limit > low_limit_mem)
					info->low_limit = low_limit_mem;
			}
			if (high_limit_mem && high_limit_mem != opp->rate) {
				if (info->high_limit > high_limit_mem)
					info->high_limit = high_limit_mem;
			}
		}

		dev_dbg(dev, "rate=%lu, volt=%lu %lu low_temp_volt=%lu %lu\n",
			info->opp_table[i].rate, info->opp_table[i].volt,
			info->opp_table[i].mem_volt,
			info->opp_table[i].low_temp_volt,
			info->opp_table[i].low_temp_mem_volt);
		i++;
	}
	mutex_unlock(&opp_table->lock);

	dev_pm_opp_put_opp_table(opp_table);

	return 0;
}

static int monitor_device_parse_wide_temp_config(struct device_node *np,
						 struct monitor_dev_info *info)
{
	struct device *dev = info->dev;
	unsigned long high_temp_max_freq;
	int ret = 0;
	u32 value;

	np = of_parse_phandle(dev->of_node, "operating-points-v2", 0);
	if (!np)
		return -EINVAL;

	if (of_property_read_u32(np, "rockchip,max-volt", &value))
		info->max_volt = ULONG_MAX;
	else
		info->max_volt = value;
	of_property_read_u32(np, "rockchip,temp-hysteresis",
			     &info->temp_hysteresis);
	if (of_property_read_u32(np, "rockchip,low-temp", &info->low_temp))
		info->low_temp = INT_MIN;
	rockchip_get_adjust_volt_table(np, "rockchip,low-temp-adjust-volt",
				       &info->low_temp_adjust_table);
	if (!of_property_read_u32(np, "rockchip,low-temp-min-volt", &value))
		info->low_temp_min_volt = value;
	if (of_property_read_u32(np, "rockchip,high-temp", &info->high_temp))
		info->high_temp = INT_MAX;
	if (of_property_read_u32(np, "rockchip,high-temp-max-volt",
				 &value))
		info->high_temp_max_volt = ULONG_MAX;
	else
		info->high_temp_max_volt = value;
	rockchip_init_temp_opp_table(info);
	rockchip_get_temp_freq_table(np, "rockchip,temp-freq-table",
				     &info->high_limit_table);
	if (!info->high_limit_table)
		rockchip_get_temp_freq_table(np, "rockchip,high-temp-limit-table",
					     &info->high_limit_table);
	if (!info->high_limit_table) {
		if (!of_property_read_u32(np, "rockchip,high-temp-max-freq",
					  &value)) {
			high_temp_max_freq = value * 1000;
			if (info->high_limit)
				info->high_limit = min(high_temp_max_freq,
						       info->high_limit);
			else
				info->high_limit = high_temp_max_freq;
		}
	} else {
		info->high_limit = 0;
	}
	dev_info(dev, "l=%d h=%d hyst=%d l_limit=%lu h_limit=%lu h_table=%d\n",
		 info->low_temp, info->high_temp, info->temp_hysteresis,
		 info->low_limit, info->high_limit,
		 info->high_limit_table ? true : false);

	if ((info->low_temp + info->temp_hysteresis) > info->high_temp) {
		dev_err(dev, "Invalid temperature, low=%d high=%d hyst=%d\n",
			info->low_temp, info->high_temp,
			info->temp_hysteresis);
		ret = -EINVAL;
		goto err;
	}
	if (!info->low_temp_adjust_table && !info->low_temp_min_volt &&
	    !info->low_limit && !info->high_limit && !info->high_limit_table) {
		ret = -EINVAL;
		goto err;
	}
	if (info->low_temp_adjust_table || info->low_temp_min_volt)
		info->is_low_temp_enabled = true;

	return 0;
err:
	kfree(info->low_temp_adjust_table);
	info->low_temp_adjust_table = NULL;
	kfree(info->opp_table);
	info->opp_table = NULL;

	return ret;
}

static int monitor_device_parse_status_config(struct device_node *np,
					      struct monitor_dev_info *info)
{
	int ret;

	ret = of_property_read_u32(np, "rockchip,video-4k-freq",
				   &info->video_4k_freq);
	ret &= of_property_read_u32(np, "rockchip,reboot-freq",
				    &info->reboot_freq);
	if (info->devp->type == MONITOR_TPYE_CPU) {
		if (!info->reboot_freq) {
			info->reboot_freq = CPU_REBOOT_FREQ;
			ret = 0;
		}
	}

	return ret;
}

static int monitor_device_parse_early_min_volt(struct device_node *np,
					       struct monitor_dev_info *info)
{
	return of_property_read_u32(np, "rockchip,early-min-microvolt",
				    &info->early_min_volt);
}

static int monitor_device_parse_read_margin(struct device_node *np,
					    struct monitor_dev_info *info)
{
	if (of_property_read_bool(np, "volt-mem-read-margin"))
		return 0;
	return -EINVAL;
}

static int monitor_device_parse_scmi_clk(struct device_node *np,
					 struct monitor_dev_info *info)
{
	struct clk *clk;

	clk = clk_get(info->dev, NULL);
	if (strstr(__clk_get_name(clk), "scmi"))
		return 0;
	return -EINVAL;
}

static int monitor_device_parse_dt(struct device *dev,
				   struct monitor_dev_info *info)
{
	struct device_node *np;
	int ret;

	np = of_parse_phandle(dev->of_node, "operating-points-v2", 0);
	if (!np)
		return -EINVAL;

	of_property_read_u32(np, "rockchip,init-freq", &info->init_freq);

	ret = monitor_device_parse_wide_temp_config(np, info);
	ret &= monitor_device_parse_status_config(np, info);
	ret &= monitor_device_parse_early_min_volt(np, info);
	ret &= monitor_device_parse_read_margin(np, info);
	ret &= monitor_device_parse_scmi_clk(np, info);

	of_node_put(np);

	return ret;
}

int rockchip_monitor_cpu_low_temp_adjust(struct monitor_dev_info *info,
					 bool is_low)
{
	if (info->low_limit) {
		if (is_low)
			freq_qos_update_request(&info->max_temp_freq_req,
						info->low_limit / 1000);
		else
			freq_qos_update_request(&info->max_temp_freq_req,
						FREQ_QOS_MAX_DEFAULT_VALUE);
	}

	return 0;
}
EXPORT_SYMBOL(rockchip_monitor_cpu_low_temp_adjust);

int rockchip_monitor_cpu_high_temp_adjust(struct monitor_dev_info *info,
					  bool is_high)
{
	if (!info->high_limit)
		return 0;

	if (info->high_limit_table) {
		freq_qos_update_request(&info->max_temp_freq_req,
					info->high_limit / 1000);
		return 0;
	}

	if (is_high)
		freq_qos_update_request(&info->max_temp_freq_req,
					info->high_limit / 1000);
	else
		freq_qos_update_request(&info->max_temp_freq_req,
					FREQ_QOS_MAX_DEFAULT_VALUE);

	return 0;
}
EXPORT_SYMBOL(rockchip_monitor_cpu_high_temp_adjust);

int rockchip_monitor_dev_low_temp_adjust(struct monitor_dev_info *info,
					 bool is_low)
{
	if (!info->low_limit)
		return 0;

	if (is_low)
		dev_pm_qos_update_request(&info->dev_max_freq_req,
					  info->low_limit / 1000);
	else
		dev_pm_qos_update_request(&info->dev_max_freq_req,
					  PM_QOS_MAX_FREQUENCY_DEFAULT_VALUE);

	return 0;
}
EXPORT_SYMBOL(rockchip_monitor_dev_low_temp_adjust);

int rockchip_monitor_dev_high_temp_adjust(struct monitor_dev_info *info,
					  bool is_high)
{
	if (!info->high_limit)
		return 0;

	if (info->high_limit_table) {
		dev_pm_qos_update_request(&info->dev_max_freq_req,
					  info->high_limit / 1000);
		return 0;
	}

	if (is_high)
		dev_pm_qos_update_request(&info->dev_max_freq_req,
					  info->high_limit / 1000);
	else
		dev_pm_qos_update_request(&info->dev_max_freq_req,
					  PM_QOS_MAX_FREQUENCY_DEFAULT_VALUE);

	return 0;
}
EXPORT_SYMBOL(rockchip_monitor_dev_high_temp_adjust);

static int rockchip_adjust_low_temp_opp_volt(struct monitor_dev_info *info,
					     bool is_low_temp)
{
	struct device *dev = info->dev;
	struct opp_table *opp_table;
	struct dev_pm_opp *opp;
	int i = 0;

	opp_table = dev_pm_opp_get_opp_table(dev);
	if (!opp_table)
		return -ENOMEM;

	mutex_lock(&opp_table->lock);
	list_for_each_entry(opp, &opp_table->opp_list, node) {
		if (!opp->available)
			continue;
		if (is_low_temp) {
			if (opp->supplies[0].u_volt_max <
			    info->opp_table[i].low_temp_volt)
				opp->supplies[0].u_volt_max =
					info->opp_table[i].low_temp_volt;
			opp->supplies[0].u_volt =
				info->opp_table[i].low_temp_volt;
			opp->supplies[0].u_volt_min = opp->supplies[0].u_volt;
			if (opp_table->regulator_count > 1) {
				if (opp->supplies[1].u_volt_max <
				    info->opp_table[i].low_temp_mem_volt)
					opp->supplies[1].u_volt_max =
						info->opp_table[i].low_temp_mem_volt;
				opp->supplies[1].u_volt =
					info->opp_table[i].low_temp_mem_volt;
				opp->supplies[1].u_volt_min =
					opp->supplies[1].u_volt;
			}
		} else {
			opp->supplies[0].u_volt_min = info->opp_table[i].volt;
			opp->supplies[0].u_volt = opp->supplies[0].u_volt_min;
			opp->supplies[0].u_volt_max =
				info->opp_table[i].max_volt;
			if (opp_table->regulator_count > 1) {
				opp->supplies[1].u_volt_min =
					info->opp_table[i].mem_volt;
				opp->supplies[1].u_volt =
					opp->supplies[1].u_volt_min;
				opp->supplies[1].u_volt_max =
					info->opp_table[i].max_mem_volt;
			}
		}
		i++;
	}
	mutex_unlock(&opp_table->lock);

	dev_pm_opp_put_opp_table(opp_table);

	return 0;
}

static void rockchip_low_temp_adjust(struct monitor_dev_info *info,
				     bool is_low)
{
	struct monitor_dev_profile *devp = info->devp;
	int ret = 0;

	dev_dbg(info->dev, "low_temp %d\n", is_low);

	if (info->opp_table)
		rockchip_adjust_low_temp_opp_volt(info, is_low);

	if (devp->low_temp_adjust)
		ret = devp->low_temp_adjust(info, is_low);
	if (!ret)
		info->is_low_temp = is_low;

	if (devp->update_volt)
		devp->update_volt(info);
}

static void rockchip_high_temp_adjust(struct monitor_dev_info *info,
				      bool is_high)
{
	struct monitor_dev_profile *devp = info->devp;
	int ret = 0;

	if (!devp->high_temp_adjust)
		return;

	if (info->high_limit_table) {
		devp->high_temp_adjust(info, is_high);
	} else {
		dev_dbg(info->dev, "high_temp %d\n", is_high);
		ret = devp->high_temp_adjust(info, is_high);
		if (!ret)
			info->is_high_temp = is_high;
	}
}

int rockchip_monitor_suspend_low_temp_adjust(int cpu)
{
	struct monitor_dev_info *info = NULL, *tmp;

	list_for_each_entry(tmp, &monitor_dev_list, node) {
		if (tmp->devp->type != MONITOR_TPYE_CPU)
			continue;
		if (cpumask_test_cpu(cpu, &tmp->devp->allowed_cpus)) {
			info = tmp;
			break;
		}
	}

	if (!info || !info->is_low_temp_enabled)
		return 0;

	if (info->high_limit_table) {
		info->high_limit = 0;
		rockchip_high_temp_adjust(info, true);
	} else if (info->is_high_temp) {
		rockchip_high_temp_adjust(info, false);
	}
	if (!info->is_low_temp)
		rockchip_low_temp_adjust(info, true);

	return 0;
}
EXPORT_SYMBOL(rockchip_monitor_suspend_low_temp_adjust);

static int
rockchip_system_monitor_wide_temp_adjust(struct monitor_dev_info *info,
					 int temp)
{
	unsigned long target_freq = 0;
	int i;

	if (temp < info->low_temp) {
		if (!info->is_low_temp)
			rockchip_low_temp_adjust(info, true);
	} else if (temp > (info->low_temp + info->temp_hysteresis)) {
		if (info->is_low_temp)
			rockchip_low_temp_adjust(info, false);
	}

	if (info->high_limit_table) {
		for (i = 0; info->high_limit_table[i].freq != UINT_MAX; i++) {
			if (temp > info->high_limit_table[i].temp)
				target_freq =
					info->high_limit_table[i].freq * 1000;
		}
		if (target_freq != info->high_limit) {
			info->high_limit = target_freq;
			rockchip_high_temp_adjust(info, true);
		}
	} else {
		if (temp > info->high_temp) {
			if (!info->is_high_temp)
				rockchip_high_temp_adjust(info, true);
		} else if (temp < (info->high_temp - info->temp_hysteresis)) {
			if (info->is_high_temp)
				rockchip_high_temp_adjust(info, false);
		}
	}

	return 0;
}

static void
rockchip_system_monitor_wide_temp_init(struct monitor_dev_info *info)
{
	int ret, temp;

	if (!info->opp_table)
		return;

	/*
	 * set the init state to low temperature that the voltage will be enough
	 * when cpu up at low temperature.
	 */
	if (!info->is_low_temp) {
		if (info->opp_table)
			rockchip_adjust_low_temp_opp_volt(info, true);
		info->is_low_temp = true;
	}

	ret = thermal_zone_get_temp(system_monitor->tz, &temp);
	if (ret || temp == THERMAL_TEMP_INVALID) {
		dev_err(info->dev,
			"failed to read out thermal zone (%d)\n", ret);
		return;
	}

	if (temp > info->high_temp) {
		if (info->opp_table)
			rockchip_adjust_low_temp_opp_volt(info, false);
		info->is_low_temp = false;
		info->is_high_temp = true;
	} else if (temp > (info->low_temp + info->temp_hysteresis)) {
		if (info->opp_table)
			rockchip_adjust_low_temp_opp_volt(info, false);
		info->is_low_temp = false;
	}
}

static const char *get_rdev_name(struct regulator_dev *rdev)
{
	if (rdev->constraints && rdev->constraints->name)
		return rdev->constraints->name;
	else if (rdev->desc->name)
		return rdev->desc->name;
	else
		return "";
}

static void
rockchip_system_monitor_early_regulator_init(struct monitor_dev_info *info)
{
	struct regulator *reg;
	struct regulator_dev *rdev;

	if (!info->early_min_volt || !info->regulators)
		return;

	rdev = info->regulators[0]->rdev;
	reg = regulator_get(NULL, get_rdev_name(rdev));
	if (!IS_ERR_OR_NULL(reg)) {
		info->early_reg = reg;
		reg->voltage[PM_SUSPEND_ON].min_uV = info->early_min_volt;
		reg->voltage[PM_SUSPEND_ON].max_uV = rdev->constraints->max_uV;
	}
}

static int
rockchip_system_monitor_freq_qos_requset(struct monitor_dev_info *info)
{
	struct devfreq *devfreq;
	struct cpufreq_policy *policy;
	int max_default_value = FREQ_QOS_MAX_DEFAULT_VALUE;
	int ret;

	if (info->is_low_temp && info->low_limit)
		max_default_value = info->low_limit / 1000;
	else if (info->is_high_temp && info->high_limit)
		max_default_value = info->high_limit / 1000;

	if (info->devp->type == MONITOR_TPYE_CPU) {
		policy = (struct cpufreq_policy *)info->devp->data;
		ret = freq_qos_add_request(&policy->constraints,
					   &info->max_temp_freq_req,
					   FREQ_QOS_MAX,
					   max_default_value);
		if (ret < 0) {
			dev_info(info->dev,
				 "failed to add temp freq constraint\n");
			return ret;
		}
		ret = freq_qos_add_request(&policy->constraints,
					   &info->min_sta_freq_req,
					   FREQ_QOS_MIN,
					   FREQ_QOS_MIN_DEFAULT_VALUE);
		if (ret < 0) {
			dev_info(info->dev,
				 "failed to add sta freq constraint\n");
			freq_qos_remove_request(&info->max_temp_freq_req);
			return ret;
		}
		ret = freq_qos_add_request(&policy->constraints,
					   &info->max_sta_freq_req,
					   FREQ_QOS_MAX,
					   FREQ_QOS_MAX_DEFAULT_VALUE);
		if (ret < 0) {
			dev_info(info->dev,
				 "failed to add sta freq constraint\n");
			freq_qos_remove_request(&info->max_temp_freq_req);
			freq_qos_remove_request(&info->min_sta_freq_req);
			return ret;
		}
	} else if (info->devp->type == MONITOR_TPYE_DEV) {
		devfreq = (struct devfreq *)info->devp->data;
		ret = dev_pm_qos_add_request(devfreq->dev.parent,
					     &info->dev_max_freq_req,
					     DEV_PM_QOS_MAX_FREQUENCY,
					     max_default_value);
		if (ret < 0) {
			dev_info(info->dev, "failed to add freq constraint\n");
			return ret;
		}
	}

	return 0;
}

static int rockchip_system_monitor_parse_supplies(struct device *dev,
						  struct monitor_dev_info *info)
{
	struct opp_table *opp_table;
	struct dev_pm_set_opp_data *data;
	int len, count;

	opp_table = dev_pm_opp_get_opp_table(dev);
	if (IS_ERR(opp_table))
		return PTR_ERR(opp_table);

	if (opp_table->clk)
		info->clk = opp_table->clk;
	if (opp_table->regulators)
		info->regulators = opp_table->regulators;
	info->regulator_count = opp_table->regulator_count;

	if (opp_table->regulators && info->devp->set_opp) {
		count = opp_table->regulator_count;
		/* space for set_opp_data */
		len = sizeof(*data);
		/* space for old_opp.supplies and new_opp.supplies */
		len += 2 * sizeof(struct dev_pm_opp_supply) * count;
		data = kzalloc(len, GFP_KERNEL);
		if (!data)
			return -ENOMEM;
		data->old_opp.supplies = (void *)(data + 1);
		data->new_opp.supplies = data->old_opp.supplies + count;
		info->set_opp_data = data;
	}

	dev_pm_opp_put_opp_table(opp_table);

	return 0;
}

void rockchip_monitor_volt_adjust_lock(struct monitor_dev_info *info)
{
	if (info)
		mutex_lock(&info->volt_adjust_mutex);
}
EXPORT_SYMBOL(rockchip_monitor_volt_adjust_lock);

void rockchip_monitor_volt_adjust_unlock(struct monitor_dev_info *info)
{
	if (info)
		mutex_unlock(&info->volt_adjust_mutex);
}
EXPORT_SYMBOL(rockchip_monitor_volt_adjust_unlock);

static int rockchip_monitor_enable_opp_clk(struct device *dev,
					   struct rockchip_opp_info *opp_info)
{
	int ret = 0;

	if (!opp_info)
		return 0;

	ret = clk_bulk_prepare_enable(opp_info->num_clks, opp_info->clks);
	if (ret) {
		dev_err(dev, "failed to enable opp clks\n");
		return ret;
	}

	return 0;
}

static void rockchip_monitor_disable_opp_clk(struct device *dev,
					     struct rockchip_opp_info *opp_info)
{
	if (!opp_info)
		return;

	clk_bulk_disable_unprepare(opp_info->num_clks, opp_info->clks);
}

static int rockchip_monitor_set_opp(struct monitor_dev_info *info,
				    unsigned long old_freq,
				    unsigned long freq,
				    struct dev_pm_opp_supply *old_supply,
				    struct dev_pm_opp_supply *new_supply)
{
	struct dev_pm_set_opp_data *data;
	int size;

	data = info->set_opp_data;
	data->regulators = info->regulators;
	data->regulator_count = info->regulator_count;
	data->clk = info->clk;
	data->dev = info->dev;

	data->old_opp.rate = old_freq;
	size = sizeof(*old_supply) * info->regulator_count;
	if (!old_supply)
		memset(data->old_opp.supplies, 0, size);
	else
		memcpy(data->old_opp.supplies, old_supply, size);

	data->new_opp.rate = freq;
	memcpy(data->new_opp.supplies, new_supply, size);

	return info->devp->set_opp(data);
}

int rockchip_monitor_check_rate_volt(struct monitor_dev_info *info)
{
	struct device *dev = info->dev;
	struct regulator *vdd_reg = NULL;
	struct regulator *mem_reg = NULL;
	struct rockchip_opp_info *opp_info = info->devp->opp_info;
	struct dev_pm_opp *opp;
	unsigned long old_rate, new_rate, new_volt, new_mem_volt;
	int old_volt, old_mem_volt;
	u32 target_rm = UINT_MAX;
	bool is_set_clk = true;
	bool is_set_rm = false;
	int ret = 0;

	if (!info->regulators || !info->clk)
		return 0;

	mutex_lock(&info->volt_adjust_mutex);

	vdd_reg = info->regulators[0];
	old_rate = clk_get_rate(info->clk);
	old_volt = regulator_get_voltage(vdd_reg);
	if (info->regulator_count > 1) {
		mem_reg = info->regulators[1];
		old_mem_volt = regulator_get_voltage(mem_reg);
	}

	if (info->init_freq) {
		new_rate = info->init_freq * 1000;
		info->init_freq = 0;
	} else {
		new_rate = old_rate;
	}
	opp = dev_pm_opp_find_freq_ceil(dev, &new_rate);
	if (IS_ERR(opp)) {
		opp = dev_pm_opp_find_freq_floor(dev, &new_rate);
		if (IS_ERR(opp)) {
			ret = PTR_ERR(opp);
			goto unlock;
		}
	}
	new_volt = opp->supplies[0].u_volt;
	if (info->regulator_count > 1)
		new_mem_volt = opp->supplies[1].u_volt;
	dev_pm_opp_put(opp);

	if (old_rate == new_rate) {
		if (info->regulator_count > 1) {
			if (old_volt == new_volt &&
			    new_mem_volt == old_mem_volt)
				goto unlock;
		} else if (old_volt == new_volt) {
			goto unlock;
		}
	}
	if (!new_volt || (info->regulator_count > 1 && !new_mem_volt))
		goto unlock;

	if (info->devp->set_opp) {
		ret = rockchip_monitor_set_opp(info, old_rate, new_rate,
					       NULL, opp->supplies);
		goto unlock;
	}

	if (opp_info && opp_info->data && opp_info->data->set_read_margin) {
		is_set_rm = true;
		if (info->devp->type == MONITOR_TPYE_DEV) {
			if (!pm_runtime_active(dev)) {
				is_set_rm = false;
				if (opp_info->scmi_clk)
					is_set_clk = false;
			}
		}
	}
	rockchip_monitor_enable_opp_clk(dev, opp_info);
	rockchip_get_read_margin(dev, opp_info, new_volt, &target_rm);

	dev_dbg(dev, "%s: %lu Hz --> %lu Hz\n", __func__, old_rate, new_rate);
	if (new_rate >= old_rate) {
		rockchip_set_intermediate_rate(dev, opp_info, info->clk,
					       old_rate, new_rate,
					       true, is_set_clk);
		if (info->regulator_count > 1) {
			ret = regulator_set_voltage(mem_reg, new_mem_volt,
						    INT_MAX);
			if (ret) {
				dev_err(dev, "%s: failed to set volt: %lu\n",
					__func__, new_mem_volt);
				goto restore_voltage;
			}
		}
		ret = regulator_set_voltage(vdd_reg, new_volt, INT_MAX);
		if (ret) {
			dev_err(dev, "%s: failed to set volt: %lu\n",
				__func__, new_volt);
			goto restore_voltage;
		}
		rockchip_set_read_margin(dev, opp_info, target_rm, is_set_rm);
		if (is_set_clk && clk_set_rate(info->clk, new_rate)) {
			dev_err(dev, "%s: failed to set clock rate: %lu\n",
				__func__, new_rate);
			goto restore_rm;
		}
	} else {
		rockchip_set_intermediate_rate(dev, opp_info, info->clk,
					       old_rate, new_rate,
					       false, is_set_clk);
		rockchip_set_read_margin(dev, opp_info, target_rm, is_set_rm);
		if (is_set_clk && clk_set_rate(info->clk, new_rate)) {
			dev_err(dev, "%s: failed to set clock rate: %lu\n",
				__func__, new_rate);
			goto restore_rm;
		}
		ret = regulator_set_voltage(vdd_reg, new_volt,
					    INT_MAX);
		if (ret) {
			dev_err(dev, "%s: failed to set volt: %lu\n",
				__func__, new_volt);
			goto restore_freq;
		}
		if (info->regulator_count > 1) {
			ret = regulator_set_voltage(mem_reg, new_mem_volt,
						    INT_MAX);
			if (ret) {
				dev_err(dev, "%s: failed to set volt: %lu\n",
					__func__, new_mem_volt);
				goto restore_freq;
			}
		}
	}
	goto disable_clk;

restore_freq:
	if (is_set_clk && clk_set_rate(info->clk, old_rate))
		dev_err(dev, "%s: failed to restore old-freq (%lu Hz)\n",
			__func__, old_rate);
restore_rm:
	rockchip_get_read_margin(dev, opp_info, old_volt, &target_rm);
	rockchip_set_read_margin(dev, opp_info, target_rm, is_set_rm);
restore_voltage:
	if (info->regulator_count > 1)
		regulator_set_voltage(mem_reg, old_mem_volt, INT_MAX);
	regulator_set_voltage(vdd_reg, old_volt, INT_MAX);
disable_clk:
	rockchip_monitor_disable_opp_clk(dev, opp_info);
unlock:
	mutex_unlock(&info->volt_adjust_mutex);

	return ret;
}
EXPORT_SYMBOL(rockchip_monitor_check_rate_volt);

struct monitor_dev_info *
rockchip_system_monitor_register(struct device *dev,
				 struct monitor_dev_profile *devp)
{
	struct monitor_dev_info *info;

	if (!system_monitor)
		return ERR_PTR(-ENOMEM);

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);
	info->dev = dev;
	info->devp = devp;

	mutex_init(&info->volt_adjust_mutex);

	rockchip_system_monitor_parse_supplies(dev, info);
	if (monitor_device_parse_dt(dev, info)) {
		rockchip_monitor_check_rate_volt(info);
		devp->is_checked = true;
		kfree(info->set_opp_data);
		kfree(info);
		return ERR_PTR(-EINVAL);
	}

	rockchip_system_monitor_early_regulator_init(info);
	rockchip_system_monitor_wide_temp_init(info);
	rockchip_monitor_check_rate_volt(info);
	devp->is_checked = true;
	rockchip_system_monitor_freq_qos_requset(info);

	down_write(&mdev_list_sem);
	list_add(&info->node, &monitor_dev_list);
	up_write(&mdev_list_sem);

	return info;
}
EXPORT_SYMBOL(rockchip_system_monitor_register);

void rockchip_system_monitor_unregister(struct monitor_dev_info *info)
{
	if (!info)
		return;

	down_write(&mdev_list_sem);
	list_del(&info->node);
	up_write(&mdev_list_sem);

	if (info->devp->type == MONITOR_TPYE_CPU) {
		freq_qos_remove_request(&info->max_temp_freq_req);
		freq_qos_remove_request(&info->min_sta_freq_req);
		freq_qos_remove_request(&info->max_sta_freq_req);
	} else {
		dev_pm_qos_remove_request(&info->dev_max_freq_req);
	}

	kfree(info->low_temp_adjust_table);
	kfree(info->opp_table);
	kfree(info->set_opp_data);
	kfree(info);
}
EXPORT_SYMBOL(rockchip_system_monitor_unregister);

static int notify_dummy(struct thermal_zone_device *tz, int trip)
{
	return 0;
}

static struct thermal_governor thermal_gov_dummy = {
	.name		= "dummy",
	.throttle	= notify_dummy,
};

static int rockchip_system_monitor_parse_dt(struct system_monitor *monitor)
{
	struct device_node *np = monitor->dev->of_node;
	const char *tz_name, *buf = NULL;

	if (of_property_read_string(np, "rockchip,video-4k-offline-cpus", &buf))
		cpumask_clear(&system_monitor->video_4k_offline_cpus);
	else
		cpulist_parse(buf, &monitor->video_4k_offline_cpus);

	if (of_property_read_string(np, "rockchip,thermal-zone", &tz_name))
		goto out;
	monitor->tz = thermal_zone_get_zone_by_name(tz_name);
	if (IS_ERR(monitor->tz)) {
		monitor->tz = NULL;
		goto out;
	}
	if (of_property_read_u32(np, "rockchip,polling-delay",
				 &monitor->delay))
		monitor->delay = THERMAL_POLLING_DELAY;

	if (of_property_read_string(np, "rockchip,temp-offline-cpus",
				    &buf))
		cpumask_clear(&system_monitor->temp_offline_cpus);
	else
		cpulist_parse(buf, &system_monitor->temp_offline_cpus);

	if (of_property_read_u32(np, "rockchip,offline-cpu-temp",
				 &system_monitor->offline_cpus_temp))
		system_monitor->offline_cpus_temp = INT_MAX;
	of_property_read_u32(np, "rockchip,temp-hysteresis",
			     &system_monitor->temp_hysteresis);

	if (of_find_property(np, "rockchip,thermal-governor-dummy", NULL)) {
		if (monitor->tz->governor->unbind_from_tz)
			monitor->tz->governor->unbind_from_tz(monitor->tz);
		monitor->tz->governor = &thermal_gov_dummy;
	}

out:
	return 0;
}

static void rockchip_system_monitor_cpu_on_off(void)
{
#ifdef CONFIG_HOTPLUG_CPU
	struct cpumask online_cpus, offline_cpus;
	unsigned int cpu;

	mutex_lock(&cpu_on_off_mutex);

	cpumask_clear(&offline_cpus);
	if (system_monitor->is_temp_offline) {
		cpumask_or(&offline_cpus, &system_monitor->status_offline_cpus,
			   &system_monitor->temp_offline_cpus);
	} else {
		cpumask_copy(&offline_cpus,
			     &system_monitor->status_offline_cpus);
	}
	if (cpumask_equal(&offline_cpus, &system_monitor->offline_cpus))
		goto out;
	cpumask_copy(&system_monitor->offline_cpus, &offline_cpus);
	for_each_cpu(cpu, &system_monitor->offline_cpus) {
		if (cpu_online(cpu))
			remove_cpu(cpu);
	}

	cpumask_clear(&online_cpus);
	cpumask_andnot(&online_cpus, cpu_possible_mask,
		       &system_monitor->offline_cpus);
	cpumask_xor(&online_cpus, cpu_online_mask, &online_cpus);
	if (cpumask_empty(&online_cpus))
		goto out;
	for_each_cpu(cpu, &online_cpus)
		add_cpu(cpu);

out:
	mutex_unlock(&cpu_on_off_mutex);
#endif
}

static void rockchip_system_monitor_temp_cpu_on_off(int temp)
{
	bool is_temp_offline;

	if (cpumask_empty(&system_monitor->temp_offline_cpus))
		return;

	if (temp > system_monitor->offline_cpus_temp)
		is_temp_offline = true;
	else if (temp < system_monitor->offline_cpus_temp -
		 system_monitor->temp_hysteresis)
		is_temp_offline = false;
	else
		return;

	if (system_monitor->is_temp_offline == is_temp_offline)
		return;
	system_monitor->is_temp_offline = is_temp_offline;
	rockchip_system_monitor_cpu_on_off();
}

static void rockchip_system_monitor_thermal_update(void)
{
	int temp, ret;
	struct monitor_dev_info *info;
	static int last_temp = INT_MAX;

	ret = thermal_zone_get_temp(system_monitor->tz, &temp);
	if (ret || temp == THERMAL_TEMP_INVALID)
		goto out;

	dev_dbg(system_monitor->dev, "temperature=%d\n", temp);

	if (temp < last_temp && last_temp - temp <= 2000)
		goto out;
	last_temp = temp;

	down_read(&mdev_list_sem);
	list_for_each_entry(info, &monitor_dev_list, node)
		rockchip_system_monitor_wide_temp_adjust(info, temp);
	up_read(&mdev_list_sem);

	rockchip_system_monitor_temp_cpu_on_off(temp);

out:
	mod_delayed_work(system_freezable_wq, &system_monitor->thermal_work,
			 msecs_to_jiffies(system_monitor->delay));
}

static void rockchip_system_monitor_thermal_check(struct work_struct *work)
{
	if (atomic_read(&monitor_in_suspend))
		return;

	rockchip_system_monitor_thermal_update();
}

static void rockchip_system_status_cpu_limit_freq(struct monitor_dev_info *info,
						  unsigned long status)
{
	unsigned int target_freq = 0;

	if (status & SYS_STATUS_REBOOT) {
		freq_qos_update_request(&info->max_sta_freq_req,
					info->reboot_freq);
		freq_qos_update_request(&info->min_sta_freq_req,
					info->reboot_freq);
		return;
	}

	if (info->video_4k_freq && (status & SYS_STATUS_VIDEO_4K))
		target_freq = info->video_4k_freq;

	if (target_freq == info->status_max_limit)
		return;
	info->status_max_limit = target_freq;
	if (info->status_max_limit)
		freq_qos_update_request(&info->max_sta_freq_req,
					info->status_max_limit);
	else
		freq_qos_update_request(&info->max_sta_freq_req,
					FREQ_QOS_MAX_DEFAULT_VALUE);
}

static void rockchip_system_status_limit_freq(unsigned long status)
{
	struct monitor_dev_info *info;

	down_read(&mdev_list_sem);
	list_for_each_entry(info, &monitor_dev_list, node) {
		if (info->devp->type == MONITOR_TPYE_CPU)
			rockchip_system_status_cpu_limit_freq(info, status);
	}
	up_read(&mdev_list_sem);
}

static void rockchip_system_status_cpu_on_off(unsigned long status)
{
	struct cpumask offline_cpus;

	if (cpumask_empty(&system_monitor->video_4k_offline_cpus))
		return;

	cpumask_clear(&offline_cpus);
	if (status & SYS_STATUS_VIDEO_4K)
		cpumask_copy(&offline_cpus,
			     &system_monitor->video_4k_offline_cpus);
	if (cpumask_equal(&offline_cpus, &system_monitor->status_offline_cpus))
		return;
	cpumask_copy(&system_monitor->status_offline_cpus, &offline_cpus);
	rockchip_system_monitor_cpu_on_off();
}

static int rockchip_system_status_notifier(struct notifier_block *nb,
					   unsigned long status,
					   void *ptr)
{
	rockchip_system_status_limit_freq(status);

	rockchip_system_status_cpu_on_off(status);

	return NOTIFY_OK;
}

static int rockchip_system_monitor_set_cpu_uevent_suppress(bool is_suppress)
{
	struct monitor_dev_info *info;
	struct cpufreq_policy *policy;

	list_for_each_entry(info, &monitor_dev_list, node) {
		if (info->devp->type != MONITOR_TPYE_CPU)
			continue;
		policy = (struct cpufreq_policy *)info->devp->data;
		if (!policy || !policy->cdev)
			continue;
		if (is_suppress)
			dev_set_uevent_suppress(&policy->cdev->device, 1);
		else
			dev_set_uevent_suppress(&policy->cdev->device, 0);
	}

	return 0;
}

static int monitor_pm_notify(struct notifier_block *nb,
			     unsigned long mode, void *_unused)
{
	switch (mode) {
	case PM_HIBERNATION_PREPARE:
	case PM_RESTORE_PREPARE:
	case PM_SUSPEND_PREPARE:
		atomic_set(&monitor_in_suspend, 1);
		rockchip_system_monitor_set_cpu_uevent_suppress(true);
		break;
	case PM_POST_HIBERNATION:
	case PM_POST_RESTORE:
	case PM_POST_SUSPEND:
		if (system_monitor->tz)
			rockchip_system_monitor_thermal_update();
		atomic_set(&monitor_in_suspend, 0);
		rockchip_system_monitor_set_cpu_uevent_suppress(false);
		break;
	default:
		break;
	}
	return 0;
}

static struct notifier_block monitor_pm_nb = {
	.notifier_call = monitor_pm_notify,
};

static int rockchip_monitor_reboot_notifier(struct notifier_block *nb,
					     unsigned long action, void *ptr)
{
	rockchip_set_system_status(SYS_STATUS_REBOOT);
	if (system_monitor->tz)
		cancel_delayed_work_sync(&system_monitor->thermal_work);

	return NOTIFY_OK;
}

static struct notifier_block rockchip_monitor_reboot_nb = {
	.notifier_call = rockchip_monitor_reboot_notifier,
};

static int rockchip_monitor_fb_notifier(struct notifier_block *nb,
					unsigned long action, void *ptr)
{
	struct fb_event *event = ptr;

	if (action != FB_EVENT_BLANK)
		return NOTIFY_OK;

	switch (*((int *)event->data)) {
	case FB_BLANK_UNBLANK:
		rockchip_clear_system_status(SYS_STATUS_SUSPEND);
		break;
	case FB_BLANK_POWERDOWN:
		rockchip_set_system_status(SYS_STATUS_SUSPEND);
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block rockchip_monitor_fb_nb = {
	.notifier_call = rockchip_monitor_fb_notifier,
};

static int rockchip_eink_devfs_notifier(struct notifier_block *nb,
					unsigned long action, void *ptr)
{
	switch (action) {
	case EBC_ON:
		rockchip_clear_system_status(SYS_STATUS_LOW_POWER);
		break;
	case EBC_OFF:
		rockchip_set_system_status(SYS_STATUS_LOW_POWER);
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block rockchip_monitor_ebc_nb = {
	.notifier_call = rockchip_eink_devfs_notifier,
};

static void system_monitor_early_min_volt_function(struct work_struct *work)
{
	struct monitor_dev_info *info;
	struct regulator_dev *rdev;
	int min_uV, max_uV;
	int ret;

	down_read(&mdev_list_sem);
	list_for_each_entry(info, &monitor_dev_list, node) {
		if (!info->early_min_volt || !info->early_reg)
			continue;
		rdev = info->early_reg->rdev;
		min_uV = rdev->constraints->min_uV;
		max_uV = rdev->constraints->max_uV;
		ret = regulator_set_voltage(info->early_reg, min_uV, max_uV);
		if (ret)
			dev_err(&rdev->dev,
				"%s: failed to set volt\n", __func__);
		regulator_put(info->early_reg);
	}
	up_read(&mdev_list_sem);
}

static DECLARE_DELAYED_WORK(system_monitor_early_min_volt_work,
			    system_monitor_early_min_volt_function);

static int rockchip_system_monitor_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	system_monitor = devm_kzalloc(dev, sizeof(struct system_monitor),
				      GFP_KERNEL);
	if (!system_monitor)
		return -ENOMEM;
	system_monitor->dev = dev;

	system_monitor->kobj = kobject_create_and_add("system_monitor", NULL);
	if (!system_monitor->kobj)
		return -ENOMEM;
	if (sysfs_create_file(system_monitor->kobj, &status.attr))
		dev_err(dev, "failed to create system status sysfs\n");

	cpumask_clear(&system_monitor->status_offline_cpus);
	cpumask_clear(&system_monitor->offline_cpus);

	rockchip_system_monitor_parse_dt(system_monitor);
	if (system_monitor->tz) {
		INIT_DELAYED_WORK(&system_monitor->thermal_work,
				  rockchip_system_monitor_thermal_check);
		mod_delayed_work(system_freezable_wq,
				 &system_monitor->thermal_work,
				 msecs_to_jiffies(system_monitor->delay));
	}

	system_monitor->status_nb.notifier_call =
		rockchip_system_status_notifier;
	rockchip_register_system_status_notifier(&system_monitor->status_nb);

	if (register_pm_notifier(&monitor_pm_nb))
		dev_err(dev, "failed to register suspend notifier\n");

	register_reboot_notifier(&rockchip_monitor_reboot_nb);

	if (fb_register_client(&rockchip_monitor_fb_nb))
		dev_err(dev, "failed to register fb nb\n");

	ebc_register_notifier(&rockchip_monitor_ebc_nb);

	schedule_delayed_work(&system_monitor_early_min_volt_work,
			      msecs_to_jiffies(30000));

	dev_info(dev, "system monitor probe\n");

	return 0;
}

static const struct of_device_id rockchip_system_monitor_of_match[] = {
	{
		.compatible = "rockchip,system-monitor",
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, rockchip_system_monitor_of_match);

static struct platform_driver rockchip_system_monitor_driver = {
	.probe	= rockchip_system_monitor_probe,
	.driver = {
		.name	= "rockchip-system-monitor",
		.of_match_table = rockchip_system_monitor_of_match,
	},
};
module_platform_driver(rockchip_system_monitor_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Finley Xiao <finley.xiao@rock-chips.com>");
MODULE_DESCRIPTION("rockchip system monitor driver");
