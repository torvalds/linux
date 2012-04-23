/*
 * ntc_thermistor.c - NTC Thermistors
 *
 *  Copyright (C) 2010 Samsung Electronics
 *  MyungJoo Ham <myungjoo.ham@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/math64.h>
#include <linux/platform_device.h>
#include <linux/err.h>

#include <linux/platform_data/ntc_thermistor.h>

#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>

struct ntc_compensation {
	int		temp_C;
	unsigned int	ohm;
};

/*
 * A compensation table should be sorted by the values of .ohm
 * in descending order.
 * The following compensation tables are from the specification of Murata NTC
 * Thermistors Datasheet
 */
const struct ntc_compensation ncpXXwb473[] = {
	{ .temp_C	= -40, .ohm	= 1747920 },
	{ .temp_C	= -35, .ohm	= 1245428 },
	{ .temp_C	= -30, .ohm	= 898485 },
	{ .temp_C	= -25, .ohm	= 655802 },
	{ .temp_C	= -20, .ohm	= 483954 },
	{ .temp_C	= -15, .ohm	= 360850 },
	{ .temp_C	= -10, .ohm	= 271697 },
	{ .temp_C	= -5, .ohm	= 206463 },
	{ .temp_C	= 0, .ohm	= 158214 },
	{ .temp_C	= 5, .ohm	= 122259 },
	{ .temp_C	= 10, .ohm	= 95227 },
	{ .temp_C	= 15, .ohm	= 74730 },
	{ .temp_C	= 20, .ohm	= 59065 },
	{ .temp_C	= 25, .ohm	= 47000 },
	{ .temp_C	= 30, .ohm	= 37643 },
	{ .temp_C	= 35, .ohm	= 30334 },
	{ .temp_C	= 40, .ohm	= 24591 },
	{ .temp_C	= 45, .ohm	= 20048 },
	{ .temp_C	= 50, .ohm	= 16433 },
	{ .temp_C	= 55, .ohm	= 13539 },
	{ .temp_C	= 60, .ohm	= 11209 },
	{ .temp_C	= 65, .ohm	= 9328 },
	{ .temp_C	= 70, .ohm	= 7798 },
	{ .temp_C	= 75, .ohm	= 6544 },
	{ .temp_C	= 80, .ohm	= 5518 },
	{ .temp_C	= 85, .ohm	= 4674 },
	{ .temp_C	= 90, .ohm	= 3972 },
	{ .temp_C	= 95, .ohm	= 3388 },
	{ .temp_C	= 100, .ohm	= 2902 },
	{ .temp_C	= 105, .ohm	= 2494 },
	{ .temp_C	= 110, .ohm	= 2150 },
	{ .temp_C	= 115, .ohm	= 1860 },
	{ .temp_C	= 120, .ohm	= 1615 },
	{ .temp_C	= 125, .ohm	= 1406 },
};
const struct ntc_compensation ncpXXwl333[] = {
	{ .temp_C	= -40, .ohm	= 1610154 },
	{ .temp_C	= -35, .ohm	= 1130850 },
	{ .temp_C	= -30, .ohm	= 802609 },
	{ .temp_C	= -25, .ohm	= 575385 },
	{ .temp_C	= -20, .ohm	= 416464 },
	{ .temp_C	= -15, .ohm	= 304219 },
	{ .temp_C	= -10, .ohm	= 224193 },
	{ .temp_C	= -5, .ohm	= 166623 },
	{ .temp_C	= 0, .ohm	= 124850 },
	{ .temp_C	= 5, .ohm	= 94287 },
	{ .temp_C	= 10, .ohm	= 71747 },
	{ .temp_C	= 15, .ohm	= 54996 },
	{ .temp_C	= 20, .ohm	= 42455 },
	{ .temp_C	= 25, .ohm	= 33000 },
	{ .temp_C	= 30, .ohm	= 25822 },
	{ .temp_C	= 35, .ohm	= 20335 },
	{ .temp_C	= 40, .ohm	= 16115 },
	{ .temp_C	= 45, .ohm	= 12849 },
	{ .temp_C	= 50, .ohm	= 10306 },
	{ .temp_C	= 55, .ohm	= 8314 },
	{ .temp_C	= 60, .ohm	= 6746 },
	{ .temp_C	= 65, .ohm	= 5503 },
	{ .temp_C	= 70, .ohm	= 4513 },
	{ .temp_C	= 75, .ohm	= 3721 },
	{ .temp_C	= 80, .ohm	= 3084 },
	{ .temp_C	= 85, .ohm	= 2569 },
	{ .temp_C	= 90, .ohm	= 2151 },
	{ .temp_C	= 95, .ohm	= 1809 },
	{ .temp_C	= 100, .ohm	= 1529 },
	{ .temp_C	= 105, .ohm	= 1299 },
	{ .temp_C	= 110, .ohm	= 1108 },
	{ .temp_C	= 115, .ohm	= 949 },
	{ .temp_C	= 120, .ohm	= 817 },
	{ .temp_C	= 125, .ohm	= 707 },
};

struct ntc_data {
	struct device *hwmon_dev;
	struct ntc_thermistor_platform_data *pdata;
	const struct ntc_compensation *comp;
	struct device *dev;
	int n_comp;
	char name[PLATFORM_NAME_SIZE];
};

static inline u64 div64_u64_safe(u64 dividend, u64 divisor)
{
	if (divisor == 0 && dividend == 0)
		return 0;
	if (divisor == 0)
		return UINT_MAX;
	return div64_u64(dividend, divisor);
}

static unsigned int get_ohm_of_thermistor(struct ntc_data *data,
		unsigned int uV)
{
	struct ntc_thermistor_platform_data *pdata = data->pdata;
	u64 mV = uV / 1000;
	u64 pmV = pdata->pullup_uV / 1000;
	u64 N, puO, pdO;
	puO = pdata->pullup_ohm;
	pdO = pdata->pulldown_ohm;

	if (mV == 0) {
		if (pdata->connect == NTC_CONNECTED_POSITIVE)
			return UINT_MAX;
		return 0;
	}
	if (mV >= pmV)
		return (pdata->connect == NTC_CONNECTED_POSITIVE) ?
			0 : UINT_MAX;

	if (pdata->connect == NTC_CONNECTED_POSITIVE && puO == 0)
		N = div64_u64_safe(pdO * (pmV - mV), mV);
	else if (pdata->connect == NTC_CONNECTED_GROUND && pdO == 0)
		N = div64_u64_safe(puO * mV, pmV - mV);
	else if (pdata->connect == NTC_CONNECTED_POSITIVE)
		N = div64_u64_safe(pdO * puO * (pmV - mV),
				puO * mV - pdO * (pmV - mV));
	else
		N = div64_u64_safe(pdO * puO * mV, pdO * (pmV - mV) - puO * mV);

	return (unsigned int) N;
}

static int lookup_comp(struct ntc_data *data,
		unsigned int ohm, int *i_low, int *i_high)
{
	int start, end, mid = -1;

	/* Do a binary search on compensation table */
	start = 0;
	end = data->n_comp;

	while (end > start) {
		mid = start + (end - start) / 2;
		if (data->comp[mid].ohm < ohm)
			end = mid;
		else if (data->comp[mid].ohm > ohm)
			start = mid + 1;
		else
			break;
	}

	if (mid == 0) {
		if (data->comp[mid].ohm > ohm) {
			*i_high = mid;
			*i_low = mid + 1;
			return 0;
		} else {
			*i_low = mid;
			*i_high = -1;
			return -EINVAL;
		}
	}
	if (mid == (data->n_comp - 1)) {
		if (data->comp[mid].ohm <= ohm) {
			*i_low = mid;
			*i_high = mid - 1;
			return 0;
		} else {
			*i_low = -1;
			*i_high = mid;
			return -EINVAL;
		}
	}

	if (data->comp[mid].ohm <= ohm) {
		*i_low = mid;
		*i_high = mid - 1;
	} else {
		*i_low = mid + 1;
		*i_high = mid;
	}

	return 0;
}

static int get_temp_mC(struct ntc_data *data, unsigned int ohm, int *temp)
{
	int low, high;
	int ret;

	ret = lookup_comp(data, ohm, &low, &high);
	if (ret) {
		/* Unable to use linear approximation */
		if (low != -1)
			*temp = data->comp[low].temp_C * 1000;
		else if (high != -1)
			*temp = data->comp[high].temp_C * 1000;
		else
			return ret;
	} else {
		*temp = data->comp[low].temp_C * 1000 +
			((data->comp[high].temp_C - data->comp[low].temp_C) *
			 1000 * ((int)ohm - (int)data->comp[low].ohm)) /
			((int)data->comp[high].ohm - (int)data->comp[low].ohm);
	}

	return 0;
}

static int ntc_thermistor_read(struct ntc_data *data, int *temp)
{
	int ret;
	int read_ohm, read_uV;
	unsigned int ohm = 0;

	if (data->pdata->read_ohm) {
		read_ohm = data->pdata->read_ohm();
		if (read_ohm < 0)
			return read_ohm;
		ohm = (unsigned int)read_ohm;
	}

	if (data->pdata->read_uV) {
		read_uV = data->pdata->read_uV();
		if (read_uV < 0)
			return read_uV;
		ohm = get_ohm_of_thermistor(data, (unsigned int)read_uV);
	}

	ret = get_temp_mC(data, ohm, temp);
	if (ret) {
		dev_dbg(data->dev, "Sensor reading function not available.\n");
		return ret;
	}

	return 0;
}

static ssize_t ntc_show_name(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct ntc_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", data->name);
}

static ssize_t ntc_show_type(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "4\n");
}

static ssize_t ntc_show_temp(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct ntc_data *data = dev_get_drvdata(dev);
	int temp, ret;

	ret = ntc_thermistor_read(data, &temp);
	if (ret)
		return ret;
	return sprintf(buf, "%d\n", temp);
}

static SENSOR_DEVICE_ATTR(temp1_type, S_IRUGO, ntc_show_type, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, ntc_show_temp, NULL, 0);
static DEVICE_ATTR(name, S_IRUGO, ntc_show_name, NULL);

static struct attribute *ntc_attributes[] = {
	&dev_attr_name.attr,
	&sensor_dev_attr_temp1_type.dev_attr.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	NULL,
};

static const struct attribute_group ntc_attr_group = {
	.attrs = ntc_attributes,
};

static int __devinit ntc_thermistor_probe(struct platform_device *pdev)
{
	struct ntc_data *data;
	struct ntc_thermistor_platform_data *pdata = pdev->dev.platform_data;
	int ret = 0;

	if (!pdata) {
		dev_err(&pdev->dev, "No platform init data supplied.\n");
		return -ENODEV;
	}

	/* Either one of the two is required. */
	if (!pdata->read_uV && !pdata->read_ohm) {
		dev_err(&pdev->dev, "Both read_uV and read_ohm missing."
				"Need either one of the two.\n");
		return -EINVAL;
	}

	if (pdata->read_uV && pdata->read_ohm) {
		dev_warn(&pdev->dev, "Only one of read_uV and read_ohm "
				"is needed; ignoring read_uV.\n");
		pdata->read_uV = NULL;
	}

	if (pdata->read_uV && (pdata->pullup_uV == 0 ||
				(pdata->pullup_ohm == 0 && pdata->connect ==
				 NTC_CONNECTED_GROUND) ||
				(pdata->pulldown_ohm == 0 && pdata->connect ==
				 NTC_CONNECTED_POSITIVE) ||
				(pdata->connect != NTC_CONNECTED_POSITIVE &&
				 pdata->connect != NTC_CONNECTED_GROUND))) {
		dev_err(&pdev->dev, "Required data to use read_uV not "
				"supplied.\n");
		return -EINVAL;
	}

	data = devm_kzalloc(&pdev->dev, sizeof(struct ntc_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = &pdev->dev;
	data->pdata = pdata;
	strncpy(data->name, pdev->id_entry->name, PLATFORM_NAME_SIZE);

	switch (pdev->id_entry->driver_data) {
	case TYPE_NCPXXWB473:
		data->comp = ncpXXwb473;
		data->n_comp = ARRAY_SIZE(ncpXXwb473);
		break;
	case TYPE_NCPXXWL333:
		data->comp = ncpXXwl333;
		data->n_comp = ARRAY_SIZE(ncpXXwl333);
		break;
	default:
		dev_err(&pdev->dev, "Unknown device type: %lu(%s)\n",
				pdev->id_entry->driver_data,
				pdev->id_entry->name);
		return -EINVAL;
	}

	platform_set_drvdata(pdev, data);

	ret = sysfs_create_group(&data->dev->kobj, &ntc_attr_group);
	if (ret) {
		dev_err(data->dev, "unable to create sysfs files\n");
		return ret;
	}

	data->hwmon_dev = hwmon_device_register(data->dev);
	if (IS_ERR(data->hwmon_dev)) {
		dev_err(data->dev, "unable to register as hwmon device.\n");
		ret = PTR_ERR(data->hwmon_dev);
		goto err_after_sysfs;
	}

	dev_info(&pdev->dev, "Thermistor %s:%d (type: %s/%lu) successfully probed.\n",
			pdev->name, pdev->id, pdev->id_entry->name,
			pdev->id_entry->driver_data);
	return 0;
err_after_sysfs:
	sysfs_remove_group(&data->dev->kobj, &ntc_attr_group);
	return ret;
}

static int __devexit ntc_thermistor_remove(struct platform_device *pdev)
{
	struct ntc_data *data = platform_get_drvdata(pdev);

	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&data->dev->kobj, &ntc_attr_group);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static const struct platform_device_id ntc_thermistor_id[] = {
	{ "ncp15wb473", TYPE_NCPXXWB473 },
	{ "ncp18wb473", TYPE_NCPXXWB473 },
	{ "ncp21wb473", TYPE_NCPXXWB473 },
	{ "ncp03wb473", TYPE_NCPXXWB473 },
	{ "ncp15wl333", TYPE_NCPXXWL333 },
	{ },
};

static struct platform_driver ntc_thermistor_driver = {
	.driver = {
		.name = "ntc-thermistor",
		.owner = THIS_MODULE,
	},
	.probe = ntc_thermistor_probe,
	.remove = __devexit_p(ntc_thermistor_remove),
	.id_table = ntc_thermistor_id,
};

module_platform_driver(ntc_thermistor_driver);

MODULE_DESCRIPTION("NTC Thermistor Driver");
MODULE_AUTHOR("MyungJoo Ham <myungjoo.ham@samsung.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ntc-thermistor");
