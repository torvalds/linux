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
#include <linux/of.h>
#include <linux/of_device.h>

#include <linux/platform_data/ntc_thermistor.h>

#include <linux/iio/iio.h>
#include <linux/iio/machine.h>
#include <linux/iio/driver.h>
#include <linux/iio/consumer.h>

#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/thermal.h>

struct ntc_compensation {
	int		temp_c;
	unsigned int	ohm;
};

/* Order matters, ntc_match references the entries by index */
static const struct platform_device_id ntc_thermistor_id[] = {
	{ "ncp15wb473", TYPE_NCPXXWB473 },
	{ "ncp18wb473", TYPE_NCPXXWB473 },
	{ "ncp21wb473", TYPE_NCPXXWB473 },
	{ "ncp03wb473", TYPE_NCPXXWB473 },
	{ "ncp15wl333", TYPE_NCPXXWL333 },
	{ "b57330v2103", TYPE_B57330V2103},
	{ },
};

/*
 * A compensation table should be sorted by the values of .ohm
 * in descending order.
 * The following compensation tables are from the specification of Murata NTC
 * Thermistors Datasheet
 */
static const struct ntc_compensation ncpXXwb473[] = {
	{ .temp_c	= -40, .ohm	= 1747920 },
	{ .temp_c	= -35, .ohm	= 1245428 },
	{ .temp_c	= -30, .ohm	= 898485 },
	{ .temp_c	= -25, .ohm	= 655802 },
	{ .temp_c	= -20, .ohm	= 483954 },
	{ .temp_c	= -15, .ohm	= 360850 },
	{ .temp_c	= -10, .ohm	= 271697 },
	{ .temp_c	= -5, .ohm	= 206463 },
	{ .temp_c	= 0, .ohm	= 158214 },
	{ .temp_c	= 5, .ohm	= 122259 },
	{ .temp_c	= 10, .ohm	= 95227 },
	{ .temp_c	= 15, .ohm	= 74730 },
	{ .temp_c	= 20, .ohm	= 59065 },
	{ .temp_c	= 25, .ohm	= 47000 },
	{ .temp_c	= 30, .ohm	= 37643 },
	{ .temp_c	= 35, .ohm	= 30334 },
	{ .temp_c	= 40, .ohm	= 24591 },
	{ .temp_c	= 45, .ohm	= 20048 },
	{ .temp_c	= 50, .ohm	= 16433 },
	{ .temp_c	= 55, .ohm	= 13539 },
	{ .temp_c	= 60, .ohm	= 11209 },
	{ .temp_c	= 65, .ohm	= 9328 },
	{ .temp_c	= 70, .ohm	= 7798 },
	{ .temp_c	= 75, .ohm	= 6544 },
	{ .temp_c	= 80, .ohm	= 5518 },
	{ .temp_c	= 85, .ohm	= 4674 },
	{ .temp_c	= 90, .ohm	= 3972 },
	{ .temp_c	= 95, .ohm	= 3388 },
	{ .temp_c	= 100, .ohm	= 2902 },
	{ .temp_c	= 105, .ohm	= 2494 },
	{ .temp_c	= 110, .ohm	= 2150 },
	{ .temp_c	= 115, .ohm	= 1860 },
	{ .temp_c	= 120, .ohm	= 1615 },
	{ .temp_c	= 125, .ohm	= 1406 },
};
static const struct ntc_compensation ncpXXwl333[] = {
	{ .temp_c	= -40, .ohm	= 1610154 },
	{ .temp_c	= -35, .ohm	= 1130850 },
	{ .temp_c	= -30, .ohm	= 802609 },
	{ .temp_c	= -25, .ohm	= 575385 },
	{ .temp_c	= -20, .ohm	= 416464 },
	{ .temp_c	= -15, .ohm	= 304219 },
	{ .temp_c	= -10, .ohm	= 224193 },
	{ .temp_c	= -5, .ohm	= 166623 },
	{ .temp_c	= 0, .ohm	= 124850 },
	{ .temp_c	= 5, .ohm	= 94287 },
	{ .temp_c	= 10, .ohm	= 71747 },
	{ .temp_c	= 15, .ohm	= 54996 },
	{ .temp_c	= 20, .ohm	= 42455 },
	{ .temp_c	= 25, .ohm	= 33000 },
	{ .temp_c	= 30, .ohm	= 25822 },
	{ .temp_c	= 35, .ohm	= 20335 },
	{ .temp_c	= 40, .ohm	= 16115 },
	{ .temp_c	= 45, .ohm	= 12849 },
	{ .temp_c	= 50, .ohm	= 10306 },
	{ .temp_c	= 55, .ohm	= 8314 },
	{ .temp_c	= 60, .ohm	= 6746 },
	{ .temp_c	= 65, .ohm	= 5503 },
	{ .temp_c	= 70, .ohm	= 4513 },
	{ .temp_c	= 75, .ohm	= 3721 },
	{ .temp_c	= 80, .ohm	= 3084 },
	{ .temp_c	= 85, .ohm	= 2569 },
	{ .temp_c	= 90, .ohm	= 2151 },
	{ .temp_c	= 95, .ohm	= 1809 },
	{ .temp_c	= 100, .ohm	= 1529 },
	{ .temp_c	= 105, .ohm	= 1299 },
	{ .temp_c	= 110, .ohm	= 1108 },
	{ .temp_c	= 115, .ohm	= 949 },
	{ .temp_c	= 120, .ohm	= 817 },
	{ .temp_c	= 125, .ohm	= 707 },
};

/*
 * The following compensation table is from the specification of EPCOS NTC
 * Thermistors Datasheet
 */
static const struct ntc_compensation b57330v2103[] = {
	{ .temp_c	= -40, .ohm	= 190030 },
	{ .temp_c	= -35, .ohm	= 145360 },
	{ .temp_c	= -30, .ohm	= 112060 },
	{ .temp_c	= -25, .ohm	= 87041 },
	{ .temp_c	= -20, .ohm	= 68104 },
	{ .temp_c	= -15, .ohm	= 53665 },
	{ .temp_c	= -10, .ohm	= 42576 },
	{ .temp_c	= -5, .ohm	= 34001 },
	{ .temp_c	= 0, .ohm	= 27326 },
	{ .temp_c	= 5, .ohm	= 22096 },
	{ .temp_c	= 10, .ohm	= 17973 },
	{ .temp_c	= 15, .ohm	= 14703 },
	{ .temp_c	= 20, .ohm	= 12090 },
	{ .temp_c	= 25, .ohm	= 10000 },
	{ .temp_c	= 30, .ohm	= 8311 },
	{ .temp_c	= 35, .ohm	= 6941 },
	{ .temp_c	= 40, .ohm	= 5825 },
	{ .temp_c	= 45, .ohm	= 4911 },
	{ .temp_c	= 50, .ohm	= 4158 },
	{ .temp_c	= 55, .ohm	= 3536 },
	{ .temp_c	= 60, .ohm	= 3019 },
	{ .temp_c	= 65, .ohm	= 2588 },
	{ .temp_c	= 70, .ohm	= 2227 },
	{ .temp_c	= 75, .ohm	= 1924 },
	{ .temp_c	= 80, .ohm	= 1668 },
	{ .temp_c	= 85, .ohm	= 1451 },
	{ .temp_c	= 90, .ohm	= 1266 },
	{ .temp_c	= 95, .ohm	= 1108 },
	{ .temp_c	= 100, .ohm	= 973 },
	{ .temp_c	= 105, .ohm	= 857 },
	{ .temp_c	= 110, .ohm	= 757 },
	{ .temp_c	= 115, .ohm	= 671 },
	{ .temp_c	= 120, .ohm	= 596 },
	{ .temp_c	= 125, .ohm	= 531 },
};

struct ntc_data {
	struct device *hwmon_dev;
	struct ntc_thermistor_platform_data *pdata;
	const struct ntc_compensation *comp;
	struct device *dev;
	int n_comp;
	char name[PLATFORM_NAME_SIZE];
	struct thermal_zone_device *tz;
};

#if defined(CONFIG_OF) && IS_ENABLED(CONFIG_IIO)
static int ntc_adc_iio_read(struct ntc_thermistor_platform_data *pdata)
{
	struct iio_channel *channel = pdata->chan;
	s64 result;
	int val, ret;

	ret = iio_read_channel_raw(channel, &val);
	if (ret < 0) {
		pr_err("read channel() error: %d\n", ret);
		return ret;
	}

	/* unit: mV */
	result = pdata->pullup_uv * (s64) val;
	result >>= 12;

	return (int)result;
}

static const struct of_device_id ntc_match[] = {
	{ .compatible = "murata,ncp15wb473",
		.data = &ntc_thermistor_id[0] },
	{ .compatible = "murata,ncp18wb473",
		.data = &ntc_thermistor_id[1] },
	{ .compatible = "murata,ncp21wb473",
		.data = &ntc_thermistor_id[2] },
	{ .compatible = "murata,ncp03wb473",
		.data = &ntc_thermistor_id[3] },
	{ .compatible = "murata,ncp15wl333",
		.data = &ntc_thermistor_id[4] },
	{ .compatible = "epcos,b57330v2103",
		.data = &ntc_thermistor_id[5]},

	/* Usage of vendor name "ntc" is deprecated */
	{ .compatible = "ntc,ncp15wb473",
		.data = &ntc_thermistor_id[0] },
	{ .compatible = "ntc,ncp18wb473",
		.data = &ntc_thermistor_id[1] },
	{ .compatible = "ntc,ncp21wb473",
		.data = &ntc_thermistor_id[2] },
	{ .compatible = "ntc,ncp03wb473",
		.data = &ntc_thermistor_id[3] },
	{ .compatible = "ntc,ncp15wl333",
		.data = &ntc_thermistor_id[4] },
	{ },
};
MODULE_DEVICE_TABLE(of, ntc_match);

static struct ntc_thermistor_platform_data *
ntc_thermistor_parse_dt(struct platform_device *pdev)
{
	struct iio_channel *chan;
	enum iio_chan_type type;
	struct device_node *np = pdev->dev.of_node;
	struct ntc_thermistor_platform_data *pdata;
	int ret;

	if (!np)
		return NULL;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	chan = iio_channel_get(&pdev->dev, NULL);
	if (IS_ERR(chan))
		return ERR_CAST(chan);

	ret = iio_get_channel_type(chan, &type);
	if (ret < 0)
		return ERR_PTR(ret);

	if (type != IIO_VOLTAGE)
		return ERR_PTR(-EINVAL);

	if (of_property_read_u32(np, "pullup-uv", &pdata->pullup_uv))
		return ERR_PTR(-ENODEV);
	if (of_property_read_u32(np, "pullup-ohm", &pdata->pullup_ohm))
		return ERR_PTR(-ENODEV);
	if (of_property_read_u32(np, "pulldown-ohm", &pdata->pulldown_ohm))
		return ERR_PTR(-ENODEV);

	if (of_find_property(np, "connected-positive", NULL))
		pdata->connect = NTC_CONNECTED_POSITIVE;
	else /* status change should be possible if not always on. */
		pdata->connect = NTC_CONNECTED_GROUND;

	pdata->chan = chan;
	pdata->read_uv = ntc_adc_iio_read;

	return pdata;
}
static void ntc_iio_channel_release(struct ntc_thermistor_platform_data *pdata)
{
	if (pdata->chan)
		iio_channel_release(pdata->chan);
}
#else
static struct ntc_thermistor_platform_data *
ntc_thermistor_parse_dt(struct platform_device *pdev)
{
	return NULL;
}

#define ntc_match	NULL

static void ntc_iio_channel_release(struct ntc_thermistor_platform_data *pdata)
{ }
#endif

static inline u64 div64_u64_safe(u64 dividend, u64 divisor)
{
	if (divisor == 0 && dividend == 0)
		return 0;
	if (divisor == 0)
		return UINT_MAX;
	return div64_u64(dividend, divisor);
}

static int get_ohm_of_thermistor(struct ntc_data *data, unsigned int uv)
{
	struct ntc_thermistor_platform_data *pdata = data->pdata;
	u64 mv = uv / 1000;
	u64 pmv = pdata->pullup_uv / 1000;
	u64 n, puo, pdo;
	puo = pdata->pullup_ohm;
	pdo = pdata->pulldown_ohm;

	if (mv == 0) {
		if (pdata->connect == NTC_CONNECTED_POSITIVE)
			return INT_MAX;
		return 0;
	}
	if (mv >= pmv)
		return (pdata->connect == NTC_CONNECTED_POSITIVE) ?
			0 : INT_MAX;

	if (pdata->connect == NTC_CONNECTED_POSITIVE && puo == 0)
		n = div64_u64_safe(pdo * (pmv - mv), mv);
	else if (pdata->connect == NTC_CONNECTED_GROUND && pdo == 0)
		n = div64_u64_safe(puo * mv, pmv - mv);
	else if (pdata->connect == NTC_CONNECTED_POSITIVE)
		n = div64_u64_safe(pdo * puo * (pmv - mv),
				puo * mv - pdo * (pmv - mv));
	else
		n = div64_u64_safe(pdo * puo * mv, pdo * (pmv - mv) - puo * mv);

	if (n > INT_MAX)
		n = INT_MAX;
	return n;
}

static void lookup_comp(struct ntc_data *data, unsigned int ohm,
			int *i_low, int *i_high)
{
	int start, end, mid;

	/*
	 * Handle special cases: Resistance is higher than or equal to
	 * resistance in first table entry, or resistance is lower or equal
	 * to resistance in last table entry.
	 * In these cases, return i_low == i_high, either pointing to the
	 * beginning or to the end of the table depending on the condition.
	 */
	if (ohm >= data->comp[0].ohm) {
		*i_low = 0;
		*i_high = 0;
		return;
	}
	if (ohm <= data->comp[data->n_comp - 1].ohm) {
		*i_low = data->n_comp - 1;
		*i_high = data->n_comp - 1;
		return;
	}

	/* Do a binary search on compensation table */
	start = 0;
	end = data->n_comp;
	while (start < end) {
		mid = start + (end - start) / 2;
		/*
		 * start <= mid < end
		 * data->comp[start].ohm > ohm >= data->comp[end].ohm
		 *
		 * We could check for "ohm == data->comp[mid].ohm" here, but
		 * that is a quite unlikely condition, and we would have to
		 * check again after updating start. Check it at the end instead
		 * for simplicity.
		 */
		if (ohm >= data->comp[mid].ohm) {
			end = mid;
		} else {
			start = mid + 1;
			/*
			 * ohm >= data->comp[start].ohm might be true here,
			 * since we set start to mid + 1. In that case, we are
			 * done. We could keep going, but the condition is quite
			 * likely to occur, so it is worth checking for it.
			 */
			if (ohm >= data->comp[start].ohm)
				end = start;
		}
		/*
		 * start <= end
		 * data->comp[start].ohm >= ohm >= data->comp[end].ohm
		 */
	}
	/*
	 * start == end
	 * ohm >= data->comp[end].ohm
	 */
	*i_low = end;
	if (ohm == data->comp[end].ohm)
		*i_high = end;
	else
		*i_high = end - 1;
}

static int get_temp_mc(struct ntc_data *data, unsigned int ohm)
{
	int low, high;
	int temp;

	lookup_comp(data, ohm, &low, &high);
	if (low == high) {
		/* Unable to use linear approximation */
		temp = data->comp[low].temp_c * 1000;
	} else {
		temp = data->comp[low].temp_c * 1000 +
			((data->comp[high].temp_c - data->comp[low].temp_c) *
			 1000 * ((int)ohm - (int)data->comp[low].ohm)) /
			((int)data->comp[high].ohm - (int)data->comp[low].ohm);
	}
	return temp;
}

static int ntc_thermistor_get_ohm(struct ntc_data *data)
{
	int read_uv;

	if (data->pdata->read_ohm)
		return data->pdata->read_ohm();

	if (data->pdata->read_uv) {
		read_uv = data->pdata->read_uv(data->pdata);
		if (read_uv < 0)
			return read_uv;
		return get_ohm_of_thermistor(data, read_uv);
	}
	return -EINVAL;
}

static int ntc_read_temp(void *dev, long *temp)
{
	struct ntc_data *data = dev_get_drvdata(dev);
	int ohm;

	ohm = ntc_thermistor_get_ohm(data);
	if (ohm < 0)
		return ohm;

	*temp = get_temp_mc(data, ohm);

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
	int ohm;

	ohm = ntc_thermistor_get_ohm(data);
	if (ohm < 0)
		return ohm;

	return sprintf(buf, "%d\n", get_temp_mc(data, ohm));
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

static const struct thermal_zone_of_device_ops ntc_of_thermal_ops = {
	.get_temp = ntc_read_temp,
};

static int ntc_thermistor_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id =
			of_match_device(of_match_ptr(ntc_match), &pdev->dev);
	const struct platform_device_id *pdev_id;
	struct ntc_thermistor_platform_data *pdata;
	struct ntc_data *data;
	int ret;

	pdata = ntc_thermistor_parse_dt(pdev);
	if (IS_ERR(pdata))
		return PTR_ERR(pdata);
	else if (pdata == NULL)
		pdata = dev_get_platdata(&pdev->dev);

	if (!pdata) {
		dev_err(&pdev->dev, "No platform init data supplied.\n");
		return -ENODEV;
	}

	/* Either one of the two is required. */
	if (!pdata->read_uv && !pdata->read_ohm) {
		dev_err(&pdev->dev,
			"Both read_uv and read_ohm missing. Need either one of the two.\n");
		return -EINVAL;
	}

	if (pdata->read_uv && pdata->read_ohm) {
		dev_warn(&pdev->dev,
			 "Only one of read_uv and read_ohm is needed; ignoring read_uv.\n");
		pdata->read_uv = NULL;
	}

	if (pdata->read_uv && (pdata->pullup_uv == 0 ||
				(pdata->pullup_ohm == 0 && pdata->connect ==
				 NTC_CONNECTED_GROUND) ||
				(pdata->pulldown_ohm == 0 && pdata->connect ==
				 NTC_CONNECTED_POSITIVE) ||
				(pdata->connect != NTC_CONNECTED_POSITIVE &&
				 pdata->connect != NTC_CONNECTED_GROUND))) {
		dev_err(&pdev->dev,
			"Required data to use read_uv not supplied.\n");
		return -EINVAL;
	}

	data = devm_kzalloc(&pdev->dev, sizeof(struct ntc_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	pdev_id = of_id ? of_id->data : platform_get_device_id(pdev);

	data->dev = &pdev->dev;
	data->pdata = pdata;
	strlcpy(data->name, pdev_id->name, sizeof(data->name));

	switch (pdev_id->driver_data) {
	case TYPE_NCPXXWB473:
		data->comp = ncpXXwb473;
		data->n_comp = ARRAY_SIZE(ncpXXwb473);
		break;
	case TYPE_NCPXXWL333:
		data->comp = ncpXXwl333;
		data->n_comp = ARRAY_SIZE(ncpXXwl333);
		break;
	case TYPE_B57330V2103:
		data->comp = b57330v2103;
		data->n_comp = ARRAY_SIZE(b57330v2103);
		break;
	default:
		dev_err(&pdev->dev, "Unknown device type: %lu(%s)\n",
				pdev_id->driver_data, pdev_id->name);
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

	dev_info(&pdev->dev, "Thermistor type: %s successfully probed.\n",
								pdev_id->name);

	data->tz = thermal_zone_of_sensor_register(data->dev, 0, data->dev,
						   &ntc_of_thermal_ops);
	if (IS_ERR(data->tz)) {
		dev_dbg(&pdev->dev, "Failed to register to thermal fw.\n");
		data->tz = NULL;
	}

	return 0;
err_after_sysfs:
	sysfs_remove_group(&data->dev->kobj, &ntc_attr_group);
	ntc_iio_channel_release(pdata);
	return ret;
}

static int ntc_thermistor_remove(struct platform_device *pdev)
{
	struct ntc_data *data = platform_get_drvdata(pdev);
	struct ntc_thermistor_platform_data *pdata = data->pdata;

	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&data->dev->kobj, &ntc_attr_group);
	ntc_iio_channel_release(pdata);

	thermal_zone_of_sensor_unregister(data->dev, data->tz);

	return 0;
}

static struct platform_driver ntc_thermistor_driver = {
	.driver = {
		.name = "ntc-thermistor",
		.of_match_table = of_match_ptr(ntc_match),
	},
	.probe = ntc_thermistor_probe,
	.remove = ntc_thermistor_remove,
	.id_table = ntc_thermistor_id,
};

module_platform_driver(ntc_thermistor_driver);

MODULE_DESCRIPTION("NTC Thermistor Driver");
MODULE_AUTHOR("MyungJoo Ham <myungjoo.ham@samsung.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ntc-thermistor");
