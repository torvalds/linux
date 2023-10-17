// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for FPGA Management Engine (FME)
 *
 * Copyright (C) 2017-2018 Intel Corporation, Inc.
 *
 * Authors:
 *   Kang Luwei <luwei.kang@intel.com>
 *   Xiao Guangrong <guangrong.xiao@linux.intel.com>
 *   Joseph Grecco <joe.grecco@intel.com>
 *   Enno Luebbers <enno.luebbers@intel.com>
 *   Tim Whisonant <tim.whisonant@intel.com>
 *   Ananda Ravuri <ananda.ravuri@intel.com>
 *   Henry Mitchel <henry.mitchel@intel.com>
 */

#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/units.h>
#include <linux/fpga-dfl.h>

#include "dfl.h"
#include "dfl-fme.h"

static ssize_t ports_num_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	void __iomem *base;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(dev, FME_FEATURE_ID_HEADER);

	v = readq(base + FME_HDR_CAP);

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)FIELD_GET(FME_CAP_NUM_PORTS, v));
}
static DEVICE_ATTR_RO(ports_num);

/*
 * Bitstream (static FPGA region) identifier number. It contains the
 * detailed version and other information of this static FPGA region.
 */
static ssize_t bitstream_id_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	void __iomem *base;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(dev, FME_FEATURE_ID_HEADER);

	v = readq(base + FME_HDR_BITSTREAM_ID);

	return scnprintf(buf, PAGE_SIZE, "0x%llx\n", (unsigned long long)v);
}
static DEVICE_ATTR_RO(bitstream_id);

/*
 * Bitstream (static FPGA region) meta data. It contains the synthesis
 * date, seed and other information of this static FPGA region.
 */
static ssize_t bitstream_metadata_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	void __iomem *base;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(dev, FME_FEATURE_ID_HEADER);

	v = readq(base + FME_HDR_BITSTREAM_MD);

	return scnprintf(buf, PAGE_SIZE, "0x%llx\n", (unsigned long long)v);
}
static DEVICE_ATTR_RO(bitstream_metadata);

static ssize_t cache_size_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	void __iomem *base;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(dev, FME_FEATURE_ID_HEADER);

	v = readq(base + FME_HDR_CAP);

	return sprintf(buf, "%u\n",
		       (unsigned int)FIELD_GET(FME_CAP_CACHE_SIZE, v));
}
static DEVICE_ATTR_RO(cache_size);

static ssize_t fabric_version_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	void __iomem *base;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(dev, FME_FEATURE_ID_HEADER);

	v = readq(base + FME_HDR_CAP);

	return sprintf(buf, "%u\n",
		       (unsigned int)FIELD_GET(FME_CAP_FABRIC_VERID, v));
}
static DEVICE_ATTR_RO(fabric_version);

static ssize_t socket_id_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	void __iomem *base;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(dev, FME_FEATURE_ID_HEADER);

	v = readq(base + FME_HDR_CAP);

	return sprintf(buf, "%u\n",
		       (unsigned int)FIELD_GET(FME_CAP_SOCKET_ID, v));
}
static DEVICE_ATTR_RO(socket_id);

static struct attribute *fme_hdr_attrs[] = {
	&dev_attr_ports_num.attr,
	&dev_attr_bitstream_id.attr,
	&dev_attr_bitstream_metadata.attr,
	&dev_attr_cache_size.attr,
	&dev_attr_fabric_version.attr,
	&dev_attr_socket_id.attr,
	NULL,
};

static const struct attribute_group fme_hdr_group = {
	.attrs = fme_hdr_attrs,
};

static long fme_hdr_ioctl_release_port(struct dfl_feature_platform_data *pdata,
				       unsigned long arg)
{
	struct dfl_fpga_cdev *cdev = pdata->dfl_cdev;
	int port_id;

	if (get_user(port_id, (int __user *)arg))
		return -EFAULT;

	return dfl_fpga_cdev_release_port(cdev, port_id);
}

static long fme_hdr_ioctl_assign_port(struct dfl_feature_platform_data *pdata,
				      unsigned long arg)
{
	struct dfl_fpga_cdev *cdev = pdata->dfl_cdev;
	int port_id;

	if (get_user(port_id, (int __user *)arg))
		return -EFAULT;

	return dfl_fpga_cdev_assign_port(cdev, port_id);
}

static long fme_hdr_ioctl(struct platform_device *pdev,
			  struct dfl_feature *feature,
			  unsigned int cmd, unsigned long arg)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(&pdev->dev);

	switch (cmd) {
	case DFL_FPGA_FME_PORT_RELEASE:
		return fme_hdr_ioctl_release_port(pdata, arg);
	case DFL_FPGA_FME_PORT_ASSIGN:
		return fme_hdr_ioctl_assign_port(pdata, arg);
	}

	return -ENODEV;
}

static const struct dfl_feature_id fme_hdr_id_table[] = {
	{.id = FME_FEATURE_ID_HEADER,},
	{0,}
};

static const struct dfl_feature_ops fme_hdr_ops = {
	.ioctl = fme_hdr_ioctl,
};

#define FME_THERM_THRESHOLD	0x8
#define TEMP_THRESHOLD1		GENMASK_ULL(6, 0)
#define TEMP_THRESHOLD1_EN	BIT_ULL(7)
#define TEMP_THRESHOLD2		GENMASK_ULL(14, 8)
#define TEMP_THRESHOLD2_EN	BIT_ULL(15)
#define TRIP_THRESHOLD		GENMASK_ULL(30, 24)
#define TEMP_THRESHOLD1_STATUS	BIT_ULL(32)		/* threshold1 reached */
#define TEMP_THRESHOLD2_STATUS	BIT_ULL(33)		/* threshold2 reached */
/* threshold1 policy: 0 - AP2 (90% throttle) / 1 - AP1 (50% throttle) */
#define TEMP_THRESHOLD1_POLICY	BIT_ULL(44)

#define FME_THERM_RDSENSOR_FMT1	0x10
#define FPGA_TEMPERATURE	GENMASK_ULL(6, 0)

#define FME_THERM_CAP		0x20
#define THERM_NO_THROTTLE	BIT_ULL(0)

#define MD_PRE_DEG

static bool fme_thermal_throttle_support(void __iomem *base)
{
	u64 v = readq(base + FME_THERM_CAP);

	return FIELD_GET(THERM_NO_THROTTLE, v) ? false : true;
}

static umode_t thermal_hwmon_attrs_visible(const void *drvdata,
					   enum hwmon_sensor_types type,
					   u32 attr, int channel)
{
	const struct dfl_feature *feature = drvdata;

	/* temperature is always supported, and check hardware cap for others */
	if (attr == hwmon_temp_input)
		return 0444;

	return fme_thermal_throttle_support(feature->ioaddr) ? 0444 : 0;
}

static int thermal_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			      u32 attr, int channel, long *val)
{
	struct dfl_feature *feature = dev_get_drvdata(dev);
	u64 v;

	switch (attr) {
	case hwmon_temp_input:
		v = readq(feature->ioaddr + FME_THERM_RDSENSOR_FMT1);
		*val = (long)(FIELD_GET(FPGA_TEMPERATURE, v) * MILLI);
		break;
	case hwmon_temp_max:
		v = readq(feature->ioaddr + FME_THERM_THRESHOLD);
		*val = (long)(FIELD_GET(TEMP_THRESHOLD1, v) * MILLI);
		break;
	case hwmon_temp_crit:
		v = readq(feature->ioaddr + FME_THERM_THRESHOLD);
		*val = (long)(FIELD_GET(TEMP_THRESHOLD2, v) * MILLI);
		break;
	case hwmon_temp_emergency:
		v = readq(feature->ioaddr + FME_THERM_THRESHOLD);
		*val = (long)(FIELD_GET(TRIP_THRESHOLD, v) * MILLI);
		break;
	case hwmon_temp_max_alarm:
		v = readq(feature->ioaddr + FME_THERM_THRESHOLD);
		*val = (long)FIELD_GET(TEMP_THRESHOLD1_STATUS, v);
		break;
	case hwmon_temp_crit_alarm:
		v = readq(feature->ioaddr + FME_THERM_THRESHOLD);
		*val = (long)FIELD_GET(TEMP_THRESHOLD2_STATUS, v);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static const struct hwmon_ops thermal_hwmon_ops = {
	.is_visible = thermal_hwmon_attrs_visible,
	.read = thermal_hwmon_read,
};

static const struct hwmon_channel_info * const thermal_hwmon_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT | HWMON_T_EMERGENCY |
				 HWMON_T_MAX   | HWMON_T_MAX_ALARM |
				 HWMON_T_CRIT  | HWMON_T_CRIT_ALARM),
	NULL
};

static const struct hwmon_chip_info thermal_hwmon_chip_info = {
	.ops = &thermal_hwmon_ops,
	.info = thermal_hwmon_info,
};

static ssize_t temp1_max_policy_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct dfl_feature *feature = dev_get_drvdata(dev);
	u64 v;

	v = readq(feature->ioaddr + FME_THERM_THRESHOLD);

	return sprintf(buf, "%u\n",
		       (unsigned int)FIELD_GET(TEMP_THRESHOLD1_POLICY, v));
}

static DEVICE_ATTR_RO(temp1_max_policy);

static struct attribute *thermal_extra_attrs[] = {
	&dev_attr_temp1_max_policy.attr,
	NULL,
};

static umode_t thermal_extra_attrs_visible(struct kobject *kobj,
					   struct attribute *attr, int index)
{
	struct device *dev = kobj_to_dev(kobj);
	struct dfl_feature *feature = dev_get_drvdata(dev);

	return fme_thermal_throttle_support(feature->ioaddr) ? attr->mode : 0;
}

static const struct attribute_group thermal_extra_group = {
	.attrs		= thermal_extra_attrs,
	.is_visible	= thermal_extra_attrs_visible,
};
__ATTRIBUTE_GROUPS(thermal_extra);

static int fme_thermal_mgmt_init(struct platform_device *pdev,
				 struct dfl_feature *feature)
{
	struct device *hwmon;

	/*
	 * create hwmon to allow userspace monitoring temperature and other
	 * threshold information.
	 *
	 * temp1_input      -> FPGA device temperature
	 * temp1_max        -> hardware threshold 1 -> 50% or 90% throttling
	 * temp1_crit       -> hardware threshold 2 -> 100% throttling
	 * temp1_emergency  -> hardware trip_threshold to shutdown FPGA
	 * temp1_max_alarm  -> hardware threshold 1 alarm
	 * temp1_crit_alarm -> hardware threshold 2 alarm
	 *
	 * create device specific sysfs interfaces, e.g. read temp1_max_policy
	 * to understand the actual hardware throttling action (50% vs 90%).
	 *
	 * If hardware doesn't support automatic throttling per thresholds,
	 * then all above sysfs interfaces are not visible except temp1_input
	 * for temperature.
	 */
	hwmon = devm_hwmon_device_register_with_info(&pdev->dev,
						     "dfl_fme_thermal", feature,
						     &thermal_hwmon_chip_info,
						     thermal_extra_groups);
	if (IS_ERR(hwmon)) {
		dev_err(&pdev->dev, "Fail to register thermal hwmon\n");
		return PTR_ERR(hwmon);
	}

	return 0;
}

static const struct dfl_feature_id fme_thermal_mgmt_id_table[] = {
	{.id = FME_FEATURE_ID_THERMAL_MGMT,},
	{0,}
};

static const struct dfl_feature_ops fme_thermal_mgmt_ops = {
	.init = fme_thermal_mgmt_init,
};

#define FME_PWR_STATUS		0x8
#define FME_LATENCY_TOLERANCE	BIT_ULL(18)
#define PWR_CONSUMED		GENMASK_ULL(17, 0)

#define FME_PWR_THRESHOLD	0x10
#define PWR_THRESHOLD1		GENMASK_ULL(6, 0)	/* in Watts */
#define PWR_THRESHOLD2		GENMASK_ULL(14, 8)	/* in Watts */
#define PWR_THRESHOLD_MAX	0x7f			/* in Watts */
#define PWR_THRESHOLD1_STATUS	BIT_ULL(16)
#define PWR_THRESHOLD2_STATUS	BIT_ULL(17)

#define FME_PWR_XEON_LIMIT	0x18
#define XEON_PWR_LIMIT		GENMASK_ULL(14, 0)	/* in 0.1 Watts */
#define XEON_PWR_EN		BIT_ULL(15)
#define FME_PWR_FPGA_LIMIT	0x20
#define FPGA_PWR_LIMIT		GENMASK_ULL(14, 0)	/* in 0.1 Watts */
#define FPGA_PWR_EN		BIT_ULL(15)

static int power_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			    u32 attr, int channel, long *val)
{
	struct dfl_feature *feature = dev_get_drvdata(dev);
	u64 v;

	switch (attr) {
	case hwmon_power_input:
		v = readq(feature->ioaddr + FME_PWR_STATUS);
		*val = (long)(FIELD_GET(PWR_CONSUMED, v) * MICRO);
		break;
	case hwmon_power_max:
		v = readq(feature->ioaddr + FME_PWR_THRESHOLD);
		*val = (long)(FIELD_GET(PWR_THRESHOLD1, v) * MICRO);
		break;
	case hwmon_power_crit:
		v = readq(feature->ioaddr + FME_PWR_THRESHOLD);
		*val = (long)(FIELD_GET(PWR_THRESHOLD2, v) * MICRO);
		break;
	case hwmon_power_max_alarm:
		v = readq(feature->ioaddr + FME_PWR_THRESHOLD);
		*val = (long)FIELD_GET(PWR_THRESHOLD1_STATUS, v);
		break;
	case hwmon_power_crit_alarm:
		v = readq(feature->ioaddr + FME_PWR_THRESHOLD);
		*val = (long)FIELD_GET(PWR_THRESHOLD2_STATUS, v);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int power_hwmon_write(struct device *dev, enum hwmon_sensor_types type,
			     u32 attr, int channel, long val)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(dev->parent);
	struct dfl_feature *feature = dev_get_drvdata(dev);
	int ret = 0;
	u64 v;

	val = clamp_val(val / MICRO, 0, PWR_THRESHOLD_MAX);

	mutex_lock(&pdata->lock);

	switch (attr) {
	case hwmon_power_max:
		v = readq(feature->ioaddr + FME_PWR_THRESHOLD);
		v &= ~PWR_THRESHOLD1;
		v |= FIELD_PREP(PWR_THRESHOLD1, val);
		writeq(v, feature->ioaddr + FME_PWR_THRESHOLD);
		break;
	case hwmon_power_crit:
		v = readq(feature->ioaddr + FME_PWR_THRESHOLD);
		v &= ~PWR_THRESHOLD2;
		v |= FIELD_PREP(PWR_THRESHOLD2, val);
		writeq(v, feature->ioaddr + FME_PWR_THRESHOLD);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	mutex_unlock(&pdata->lock);

	return ret;
}

static umode_t power_hwmon_attrs_visible(const void *drvdata,
					 enum hwmon_sensor_types type,
					 u32 attr, int channel)
{
	switch (attr) {
	case hwmon_power_input:
	case hwmon_power_max_alarm:
	case hwmon_power_crit_alarm:
		return 0444;
	case hwmon_power_max:
	case hwmon_power_crit:
		return 0644;
	}

	return 0;
}

static const struct hwmon_ops power_hwmon_ops = {
	.is_visible = power_hwmon_attrs_visible,
	.read = power_hwmon_read,
	.write = power_hwmon_write,
};

static const struct hwmon_channel_info * const power_hwmon_info[] = {
	HWMON_CHANNEL_INFO(power, HWMON_P_INPUT |
				  HWMON_P_MAX   | HWMON_P_MAX_ALARM |
				  HWMON_P_CRIT  | HWMON_P_CRIT_ALARM),
	NULL
};

static const struct hwmon_chip_info power_hwmon_chip_info = {
	.ops = &power_hwmon_ops,
	.info = power_hwmon_info,
};

static ssize_t power1_xeon_limit_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct dfl_feature *feature = dev_get_drvdata(dev);
	u16 xeon_limit = 0;
	u64 v;

	v = readq(feature->ioaddr + FME_PWR_XEON_LIMIT);

	if (FIELD_GET(XEON_PWR_EN, v))
		xeon_limit = FIELD_GET(XEON_PWR_LIMIT, v);

	return sprintf(buf, "%u\n", xeon_limit * 100000);
}

static ssize_t power1_fpga_limit_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct dfl_feature *feature = dev_get_drvdata(dev);
	u16 fpga_limit = 0;
	u64 v;

	v = readq(feature->ioaddr + FME_PWR_FPGA_LIMIT);

	if (FIELD_GET(FPGA_PWR_EN, v))
		fpga_limit = FIELD_GET(FPGA_PWR_LIMIT, v);

	return sprintf(buf, "%u\n", fpga_limit * 100000);
}

static ssize_t power1_ltr_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct dfl_feature *feature = dev_get_drvdata(dev);
	u64 v;

	v = readq(feature->ioaddr + FME_PWR_STATUS);

	return sprintf(buf, "%u\n",
		       (unsigned int)FIELD_GET(FME_LATENCY_TOLERANCE, v));
}

static DEVICE_ATTR_RO(power1_xeon_limit);
static DEVICE_ATTR_RO(power1_fpga_limit);
static DEVICE_ATTR_RO(power1_ltr);

static struct attribute *power_extra_attrs[] = {
	&dev_attr_power1_xeon_limit.attr,
	&dev_attr_power1_fpga_limit.attr,
	&dev_attr_power1_ltr.attr,
	NULL
};

ATTRIBUTE_GROUPS(power_extra);

static int fme_power_mgmt_init(struct platform_device *pdev,
			       struct dfl_feature *feature)
{
	struct device *hwmon;

	hwmon = devm_hwmon_device_register_with_info(&pdev->dev,
						     "dfl_fme_power", feature,
						     &power_hwmon_chip_info,
						     power_extra_groups);
	if (IS_ERR(hwmon)) {
		dev_err(&pdev->dev, "Fail to register power hwmon\n");
		return PTR_ERR(hwmon);
	}

	return 0;
}

static const struct dfl_feature_id fme_power_mgmt_id_table[] = {
	{.id = FME_FEATURE_ID_POWER_MGMT,},
	{0,}
};

static const struct dfl_feature_ops fme_power_mgmt_ops = {
	.init = fme_power_mgmt_init,
};

static struct dfl_feature_driver fme_feature_drvs[] = {
	{
		.id_table = fme_hdr_id_table,
		.ops = &fme_hdr_ops,
	},
	{
		.id_table = fme_pr_mgmt_id_table,
		.ops = &fme_pr_mgmt_ops,
	},
	{
		.id_table = fme_global_err_id_table,
		.ops = &fme_global_err_ops,
	},
	{
		.id_table = fme_thermal_mgmt_id_table,
		.ops = &fme_thermal_mgmt_ops,
	},
	{
		.id_table = fme_power_mgmt_id_table,
		.ops = &fme_power_mgmt_ops,
	},
	{
		.id_table = fme_perf_id_table,
		.ops = &fme_perf_ops,
	},
	{
		.ops = NULL,
	},
};

static long fme_ioctl_check_extension(struct dfl_feature_platform_data *pdata,
				      unsigned long arg)
{
	/* No extension support for now */
	return 0;
}

static int fme_open(struct inode *inode, struct file *filp)
{
	struct platform_device *fdev = dfl_fpga_inode_to_feature_dev(inode);
	struct dfl_feature_platform_data *pdata = dev_get_platdata(&fdev->dev);
	int ret;

	if (WARN_ON(!pdata))
		return -ENODEV;

	mutex_lock(&pdata->lock);
	ret = dfl_feature_dev_use_begin(pdata, filp->f_flags & O_EXCL);
	if (!ret) {
		dev_dbg(&fdev->dev, "Device File Opened %d Times\n",
			dfl_feature_dev_use_count(pdata));
		filp->private_data = pdata;
	}
	mutex_unlock(&pdata->lock);

	return ret;
}

static int fme_release(struct inode *inode, struct file *filp)
{
	struct dfl_feature_platform_data *pdata = filp->private_data;
	struct platform_device *pdev = pdata->dev;
	struct dfl_feature *feature;

	dev_dbg(&pdev->dev, "Device File Release\n");

	mutex_lock(&pdata->lock);
	dfl_feature_dev_use_end(pdata);

	if (!dfl_feature_dev_use_count(pdata))
		dfl_fpga_dev_for_each_feature(pdata, feature)
			dfl_fpga_set_irq_triggers(feature, 0,
						  feature->nr_irqs, NULL);
	mutex_unlock(&pdata->lock);

	return 0;
}

static long fme_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct dfl_feature_platform_data *pdata = filp->private_data;
	struct platform_device *pdev = pdata->dev;
	struct dfl_feature *f;
	long ret;

	dev_dbg(&pdev->dev, "%s cmd 0x%x\n", __func__, cmd);

	switch (cmd) {
	case DFL_FPGA_GET_API_VERSION:
		return DFL_FPGA_API_VERSION;
	case DFL_FPGA_CHECK_EXTENSION:
		return fme_ioctl_check_extension(pdata, arg);
	default:
		/*
		 * Let sub-feature's ioctl function to handle the cmd.
		 * Sub-feature's ioctl returns -ENODEV when cmd is not
		 * handled in this sub feature, and returns 0 or other
		 * error code if cmd is handled.
		 */
		dfl_fpga_dev_for_each_feature(pdata, f) {
			if (f->ops && f->ops->ioctl) {
				ret = f->ops->ioctl(pdev, f, cmd, arg);
				if (ret != -ENODEV)
					return ret;
			}
		}
	}

	return -EINVAL;
}

static int fme_dev_init(struct platform_device *pdev)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct dfl_fme *fme;

	fme = devm_kzalloc(&pdev->dev, sizeof(*fme), GFP_KERNEL);
	if (!fme)
		return -ENOMEM;

	fme->pdata = pdata;

	mutex_lock(&pdata->lock);
	dfl_fpga_pdata_set_private(pdata, fme);
	mutex_unlock(&pdata->lock);

	return 0;
}

static void fme_dev_destroy(struct platform_device *pdev)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(&pdev->dev);

	mutex_lock(&pdata->lock);
	dfl_fpga_pdata_set_private(pdata, NULL);
	mutex_unlock(&pdata->lock);
}

static const struct file_operations fme_fops = {
	.owner		= THIS_MODULE,
	.open		= fme_open,
	.release	= fme_release,
	.unlocked_ioctl = fme_ioctl,
};

static int fme_probe(struct platform_device *pdev)
{
	int ret;

	ret = fme_dev_init(pdev);
	if (ret)
		goto exit;

	ret = dfl_fpga_dev_feature_init(pdev, fme_feature_drvs);
	if (ret)
		goto dev_destroy;

	ret = dfl_fpga_dev_ops_register(pdev, &fme_fops, THIS_MODULE);
	if (ret)
		goto feature_uinit;

	return 0;

feature_uinit:
	dfl_fpga_dev_feature_uinit(pdev);
dev_destroy:
	fme_dev_destroy(pdev);
exit:
	return ret;
}

static int fme_remove(struct platform_device *pdev)
{
	dfl_fpga_dev_ops_unregister(pdev);
	dfl_fpga_dev_feature_uinit(pdev);
	fme_dev_destroy(pdev);

	return 0;
}

static const struct attribute_group *fme_dev_groups[] = {
	&fme_hdr_group,
	&fme_global_err_group,
	NULL
};

static struct platform_driver fme_driver = {
	.driver	= {
		.name       = DFL_FPGA_FEATURE_DEV_FME,
		.dev_groups = fme_dev_groups,
	},
	.probe   = fme_probe,
	.remove  = fme_remove,
};

module_platform_driver(fme_driver);

MODULE_DESCRIPTION("FPGA Management Engine driver");
MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:dfl-fme");
