// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2016, 2018, 2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/cpu.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/slab.h>
#include <linux/qcom_scm.h>
#include <linux/platform_device.h>

/* pmu event max value */
#define PMU_EVENT_MAX			0x1F

#define PMU_MUX_OFFSET			4
#define PMU_MUX_MASK_BITS		0xF
#define ENABLE_OFFSET			1
#define ENABLE_MASK_BITS		0x1

#define _VAL(z)			(z##_MASK_BITS << z##_OFFSET)
#define _VALUE(_val, z)		(_val<<(z##_OFFSET))
#define _WRITE(x, y, z)		(((~(_VAL(z))) & y) | _VALUE(x, z))
#define _GET_BITS(_val, z)	((_val>>(z##_OFFSET)) & z##_MASK_BITS)

#define MODULE_NAME	"msm_hang_detect"
#define MAX_SYSFS_LEN 12

#define MAX_CHD_PERCPU_INFO_ARGS	2

struct hang_detect {
	phys_addr_t threshold[NR_CPUS];
	phys_addr_t config[NR_CPUS];
	uint32_t pmu_event_sel;
	struct kobject kobj;
};

/* interface for exporting attributes */
struct core_hang_attribute {
	struct attribute        attr;
	ssize_t (*show)(struct kobject *kobj, struct attribute *attr,
			char *buf);
	size_t (*store)(struct kobject *kobj, struct attribute *attr,
			const char *buf, size_t count);
};

#define CORE_HANG_ATTR(_name, _mode, _show, _store)	\
	struct core_hang_attribute hang_attr_##_name =	\
			__ATTR(_name, _mode, _show, _store)

#define to_core_hang_dev(kobj) \
	container_of(kobj, struct hang_detect, kobj)

#define to_core_attr(_attr) \
	container_of(_attr, struct core_hang_attribute, attr)

/*
 * On the kernel command line specify core_hang_detect.enable=1
 * to enable the core hang detect module.
 * By default core hang detect is turned on
 */
static int enable = 1;
module_param(enable, int, 0444);

static ssize_t attr_show(struct kobject *kobj, struct attribute *attr,
				char *buf)
{
	struct core_hang_attribute *core_attr = to_core_attr(attr);
	ssize_t ret = -EIO;

	if (core_attr->show)
		ret = core_attr->show(kobj, attr, buf);

	return ret;
}

static ssize_t attr_store(struct kobject *kobj, struct attribute *attr,
				const char *buf, size_t count)
{
	struct core_hang_attribute *core_attr = to_core_attr(attr);
	ssize_t ret = -EIO;

	if (core_attr->store)
		ret = core_attr->store(kobj, attr, buf, count);

	return ret;
}

static const struct sysfs_ops core_sysfs_ops = {
	.show	= attr_show,
	.store	= attr_store,
};

static struct kobj_type core_ktype = {
	.sysfs_ops	= &core_sysfs_ops,
};

static ssize_t show_threshold(struct kobject *kobj, struct attribute *attr,
				char *buf)
{
	struct hang_detect *device =  to_core_hang_dev(kobj);
	u32 threshold_val;

	if (qcom_scm_io_readl(device->threshold[0], &threshold_val)) {
		pr_err("%s: Failed to get threshold for core0\n", __func__);
		return -EIO;
	}

	return scnprintf(buf, MAX_SYSFS_LEN, "0x%x\n", threshold_val);
}

static size_t store_threshold(struct kobject *kobj, struct attribute *attr,
				const char *buf, size_t count)
{
	struct hang_detect *hang_dev = to_core_hang_dev(kobj);
	uint32_t threshold_val;
	int ret, cpu;

	ret = kstrtouint(buf, 0, &threshold_val);
	if (ret < 0)
		return ret;

	if (threshold_val == 0)
		return -EINVAL;

	for_each_possible_cpu(cpu) {
		if (!hang_dev->threshold[cpu])
			continue;

		if (qcom_scm_io_writel(hang_dev->threshold[cpu],
					threshold_val)) {
			pr_err("%s: Failed to set threshold for core%d\n",
					__func__, cpu);
			return -EIO;
		}
	}

	return count;
}
CORE_HANG_ATTR(threshold, 0644, show_threshold, store_threshold);

static ssize_t show_pmu_event_sel(struct kobject *kobj, struct attribute *attr,
			char *buf)
{
	struct hang_detect *hang_device = to_core_hang_dev(kobj);

	return scnprintf(buf, MAX_SYSFS_LEN, "0x%x\n",
			hang_device->pmu_event_sel);
}

static size_t store_pmu_event_sel(struct kobject *kobj, struct attribute *attr,
			const char *buf, size_t count)
{
	int  cpu, ret;
	uint32_t pmu_event_sel, reg_value;
	struct hang_detect *hang_dev = to_core_hang_dev(kobj);

	ret = kstrtouint(buf, 0, &pmu_event_sel);
	if (ret < 0)
		return ret;

	if (pmu_event_sel > PMU_EVENT_MAX)
		return -EINVAL;

	for_each_possible_cpu(cpu) {
		if (!hang_dev->config[cpu])
			continue;

		if (qcom_scm_io_readl(hang_dev->config[cpu], &reg_value)) {
			pr_err("%s: Failed to get pmu config for core%d\n",
					__func__, cpu);
			return -EIO;
		}
		if (qcom_scm_io_writel(hang_dev->config[cpu],
			_WRITE(pmu_event_sel, reg_value, PMU_MUX))) {
			pr_err("%s: Failed to set pmu event for core%d\n",
					__func__, cpu);
			return -EIO;
		}
	}

	hang_dev->pmu_event_sel = pmu_event_sel;
	return count;
}
CORE_HANG_ATTR(pmu_event_sel, 0644, show_pmu_event_sel, store_pmu_event_sel);

static ssize_t show_enable(struct kobject *kobj, struct attribute *attr,
				char *buf)
{
	u32 enabled;
	struct hang_detect *hang_dev = to_core_hang_dev(kobj);

	if (qcom_scm_io_readl(hang_dev->config[0], &enabled)) {
		pr_err("%s: Failed to get pmu config for core0\n", __func__);
		return -EIO;
	}
	enabled = _GET_BITS(enabled, ENABLE);

	return scnprintf(buf, MAX_SYSFS_LEN, "%u\n", enabled);
}

static size_t store_enable(struct kobject *kobj, struct attribute *attr,
				const char *buf, size_t count)
{
	struct hang_detect *hang_dev = to_core_hang_dev(kobj);
	uint32_t reg_value;
	int cpu, ret;
	bool enabled;

	ret = kstrtobool(buf, &enabled);
	if (ret < 0)
		return -EINVAL;

	for_each_possible_cpu(cpu) {
		if (!hang_dev->config[cpu])
			continue;

		if (qcom_scm_io_readl(hang_dev->config[cpu], &reg_value)) {
			pr_err("%s: Failed to get pmu event for core%d\n",
					__func__, cpu);
			return -EIO;
		}
		if (qcom_scm_io_writel(hang_dev->config[cpu],
			_WRITE(enabled, reg_value, ENABLE))) {
			pr_err("%s: Failed to set enable for core%d\n",
					__func__, cpu);
			return -EIO;
		}
	}

	return count;
}
CORE_HANG_ATTR(enable, 0644, show_enable, store_enable);

static struct attribute *hang_attrs[] = {
	&hang_attr_threshold.attr,
	&hang_attr_pmu_event_sel.attr,
	&hang_attr_enable.attr,
	NULL
};

static struct attribute_group hang_attr_group = {
	.attrs = hang_attrs,
};

static const struct of_device_id msm_hang_detect_table[] = {
	{ .compatible = "qcom,core-hang-detect" },
	{},
};

static int msm_hang_detect_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct hang_detect *hang_det = NULL;
	int ret, cluster_cpu_count = 0;
	int cpu, num_cpu, entry, num_chd_entry;
	const char *name;
	u32 fw_cluster_id;
	struct of_phandle_args chd_entry;

	if (!pdev->dev.of_node || !enable)
		return -ENODEV;

	hang_det = devm_kzalloc(&pdev->dev,
			sizeof(struct hang_detect), GFP_KERNEL);

	if (!hang_det)
		return -ENOMEM;

	name = of_get_property(node, "label", NULL);
	if (!name) {
		pr_err("%s: Can't get label property\n", __func__);
		return -EINVAL;
	}

	ret = of_property_read_u32(node, "cluster-id", &fw_cluster_id);
	if (ret) {
		pr_err("%s: Missing cluster-id.\n", __func__);
	} else {

		for_each_possible_cpu(cpu) {
			if (topology_physical_package_id(cpu)
					== fw_cluster_id) {
				cluster_cpu_count++;
				break;
			}
		}

		if (cluster_cpu_count == 0) {
			pr_err("%s: Unable to find any CPU for cluster:%d\n",
					__func__, fw_cluster_id);
			return -EINVAL;
		}

	}

	num_chd_entry =
		of_count_phandle_with_args(node, "qcom,chd-percpu-info", NULL);
	if (num_chd_entry <= 0) {
		pr_err("%s: Failed to get qcom,chd-percpu-info DT property\n",
				__func__);
		return -EINVAL;
	}
	num_chd_entry /= (MAX_CHD_PERCPU_INFO_ARGS + 1);

	for (entry = 0, num_cpu = 0; entry < num_chd_entry; entry++) {
		ret = of_parse_phandle_with_fixed_args(node, "qcom,chd-percpu-info",
					MAX_CHD_PERCPU_INFO_ARGS, entry, &chd_entry);
		if (ret) {
			pr_err("%s: Failed to get qcom,chd-percpu-info DT list: entry %d\n",
					__func__, entry);
			return -EINVAL;
		}

		/* Adding only entries who's cpu nodes are available */
		cpu = of_cpu_node_to_id(chd_entry.np);
		if (cpu >= 0) {
			hang_det->threshold[cpu] = chd_entry.args[0];
			hang_det->config[cpu] = chd_entry.args[1];
			num_cpu++;
		}
		of_node_put(chd_entry.np);
	}

	if (num_cpu != nr_cpu_ids) {
		pr_err("%s: Unable to find enough data for CPUs: %d != %d\n",
				__func__, num_cpu, nr_cpu_ids);
		return -EINVAL;
	}

	ret = kobject_init_and_add(&hang_det->kobj, &core_ktype,
			&cpu_subsys.dev_root->kobj, "%s_%s",
			"hang_detect", name);
	if (ret) {
		pr_err("%s: Error in creation kobject_add\n", __func__);
		goto out_put_kobj;
	}

	ret = sysfs_create_group(&hang_det->kobj, &hang_attr_group);
	if (ret) {
		pr_err("%s: Error in creation sysfs_create_group\n", __func__);
		goto out_del_kobj;
	}

	platform_set_drvdata(pdev, hang_det);
	return 0;

out_del_kobj:
	kobject_del(&hang_det->kobj);
out_put_kobj:
	kobject_put(&hang_det->kobj);

	return ret;
}

static int msm_hang_detect_remove(struct platform_device *pdev)
{
	struct hang_detect *hang_det = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);
	sysfs_remove_group(&hang_det->kobj, &hang_attr_group);
	kobject_del(&hang_det->kobj);
	kobject_put(&hang_det->kobj);
	return 0;
}

static struct platform_driver msm_hang_detect_driver = {
	.probe = msm_hang_detect_probe,
	.remove = msm_hang_detect_remove,
	.driver = {
		.name = MODULE_NAME,
		.of_match_table = msm_hang_detect_table,
	},
};

module_platform_driver(msm_hang_detect_driver);

MODULE_DESCRIPTION("MSM Core Hang Detect Driver");
MODULE_LICENSE("GPL v2");
