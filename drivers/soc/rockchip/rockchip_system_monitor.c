// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 * Author: Finley Xiao <finley.xiao@rock-chips.com>
 */

#include <dt-bindings/soc/rockchip-system-status.h>
#include <linux/cpu.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/thermal.h>
#include <soc/rockchip/rockchip-system-status.h>

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

static LIST_HEAD(video_info_list);
static struct system_monitor *system_monitor;

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

	return __compat_only_sysfs_link_entry_to_kobj(&dev->kobj,
						      system_monitor->kobj,
						      "system_status");
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
			cpu_down(cpu);
	}

	cpumask_clear(&online_cpus);
	cpumask_andnot(&online_cpus, cpu_possible_mask,
		       &system_monitor->offline_cpus);
	cpumask_xor(&online_cpus, cpu_online_mask, &online_cpus);
	if (cpumask_empty(&online_cpus))
		goto out;
	for_each_cpu(cpu, &online_cpus)
		cpu_up(cpu);

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

	ret = thermal_zone_get_temp(system_monitor->tz, &temp);
	if (ret || temp == THERMAL_TEMP_INVALID)
		goto out;

	dev_dbg(system_monitor->dev, "temperature=%d\n", temp);

	rockchip_system_monitor_temp_cpu_on_off(temp);

out:
	mod_delayed_work(system_freezable_wq, &system_monitor->thermal_work,
			 msecs_to_jiffies(system_monitor->delay));
}

static void rockchip_system_monitor_thermal_check(struct work_struct *work)
{
	rockchip_system_monitor_thermal_update();
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
	rockchip_system_status_cpu_on_off(status);

	return NOTIFY_OK;
}

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
