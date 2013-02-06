/* sec_subthermistor.c
 *
 * Copyright (C) 2012 Samsung Electronics
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/module.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <plat/adc.h>
#include <mach/sec_thermistor.h>

#define ADC_SAMPLING_CNT	7

struct sec_subtherm_info {
	struct device *dev;
	struct sec_therm_platform_data *pdata;
	struct s3c_adc_client *padc;
	struct delayed_work polling_work;

	int curr_temperature;
	int curr_temp_adc;
};

static ssize_t sec_subtherm_show_temperature(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct sec_subtherm_info *info = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", info->curr_temperature);
}

static ssize_t sec_subtherm_show_temp_adc(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct sec_subtherm_info *info = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", info->curr_temp_adc);
}

static DEVICE_ATTR(temperature, S_IRUGO, sec_subtherm_show_temperature, NULL);
static DEVICE_ATTR(temp_adc, S_IRUGO, sec_subtherm_show_temp_adc, NULL);

static struct attribute *sec_subtherm_attributes[] = {
	&dev_attr_temperature.attr,
	&dev_attr_temp_adc.attr,
	NULL
};

static const struct attribute_group sec_subtherm_group = {
	.attrs = sec_subtherm_attributes,
};

static int sec_subtherm_get_adc_data(struct sec_subtherm_info *info)
{
	int adc_ch;
	int adc_data;
	int adc_max = 0;
	int adc_min = 0;
	int adc_total = 0;
	int i;
	int err_value;

	adc_ch = info->pdata->adc_channel;

	for (i = 0; i < ADC_SAMPLING_CNT; i++) {
		adc_data = s3c_adc_read(info->padc, adc_ch);

		if (adc_data < 0) {
			dev_err(info->dev, "%s : err(%d) returned, skip read\n",
				__func__, adc_data);
			err_value = adc_data;
			goto err;
		}

		if (i != 0) {
			if (adc_data > adc_max)
				adc_max = adc_data;
			else if (adc_data < adc_min)
				adc_min = adc_data;
		} else {
			adc_max = adc_data;
			adc_min = adc_data;
		}
		adc_total += adc_data;
	}

	return (adc_total - adc_max - adc_min) / (ADC_SAMPLING_CNT - 2);
err:
	return err_value;
}

static int convert_adc_to_subtemper(struct sec_subtherm_info *info,
				int adc_data)
{
	int adc_value;
	int low, mid, high;
	struct sec_therm_adc_table *temper_table;

	low = mid = high = 0;

	if (!info->pdata->adc_table || !info->pdata->adc_arr_size) {
		/* using fake temp */
		adc_value = 300;
		dev_dbg(info->dev, " %s : fake temp\n", __func__);
		goto out;
	}

	temper_table = info->pdata->adc_table;
	high = info->pdata->adc_arr_size - 1;

	/* Out of table range */
	if (adc_data <= temper_table[low].adc) {
		adc_value = temper_table[low].temperature;
		dev_dbg(info->dev, " %s : Out of table range\n", __func__);
		goto out;
	} else if (adc_data >= temper_table[high].adc) {
		adc_value = temper_table[high].temperature;
		dev_dbg(info->dev, " %s : Out of table range\n", __func__);
		goto out;
	}

	while (low <= high) {
		mid = (low + high) / 2;
		if (temper_table[mid].adc > adc_data)
			high = mid - 1;
		else if (temper_table[mid].adc < adc_data)
			low = mid + 1;
		else
			break;
	}
	adc_value = temper_table[mid].temperature;

	/* high resolution */
	if (adc_data < temper_table[mid].adc) {
		adc_value = temper_table[mid].temperature +
			((temper_table[mid-1].temperature -
			  temper_table[mid].temperature) *
			(temper_table[mid].adc - adc_data) /
			(temper_table[mid].adc - temper_table[mid-1].adc));
	} else {
		adc_value = temper_table[mid].temperature -
			((temper_table[mid].temperature -
			  temper_table[mid+1].temperature) *
			(adc_data - temper_table[mid].adc) /
			(temper_table[mid+1].adc - temper_table[mid].adc));
	}

out:
	dev_dbg(info->dev, " %s: adc data(%d), adc temperature(%d)\n", __func__,
					adc_data, adc_value);

	return adc_value;
}

static void notify_change_of_subtemperature(struct sec_subtherm_info *info)
{
	char temp_buf[20];
	char siop_buf[20];
	char *envp[3];
	int env_offset = 0;
	int siop_level = -1;

	snprintf(temp_buf, sizeof(temp_buf), "SUBTEMPERATURE=%d",
		 info->curr_temperature);
	envp[env_offset++] = temp_buf;

	if (info->pdata->get_siop_level)
		siop_level =
		    info->pdata->get_siop_level(info->curr_temperature);

	if (siop_level >= 0) {
		snprintf(siop_buf, sizeof(siop_buf), "SIOP_LEVEL=%d",
			 siop_level);
		envp[env_offset++] = siop_buf;
		dev_info(info->dev, "%s: uevent: %s\n", __func__, siop_buf);
	}
	envp[env_offset] = NULL;

	dev_info(info->dev, "%s: uevent: %s\n", __func__, temp_buf);
	kobject_uevent_env(&info->dev->kobj, KOBJ_CHANGE, envp);
}

static void sec_subtherm_polling_work(struct work_struct *work)
{
	struct sec_subtherm_info *info =
		container_of(work, struct sec_subtherm_info, polling_work.work);
	int adc;
	int temper;

	adc = sec_subtherm_get_adc_data(info);
	dev_dbg(info->dev, "%s: adc=%d\n", __func__, adc);

	if (adc < 0)
		goto out;

	temper = convert_adc_to_subtemper(info, adc);
	dev_dbg(info->dev, "%s: temper=%d\n", __func__, temper);

	/* if temperature was changed, notify to framework */
	if (info->curr_temperature != temper) {
		info->curr_temp_adc = adc;
		info->curr_temperature = temper;
		notify_change_of_subtemperature(info);
	}
out:
	schedule_delayed_work(&info->polling_work,
			msecs_to_jiffies(info->pdata->polling_interval));
}

static __devinit int sec_subtherm_probe(struct platform_device *pdev)
{
	struct sec_therm_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct sec_subtherm_info *info;
	int ret = 0;

	dev_info(&pdev->dev, "%s: SEC Sub Thermistor Driver Loading\n", __func__);

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	platform_set_drvdata(pdev, info);

	info->dev = &pdev->dev;
	info->pdata = pdata;

	info->padc = s3c_adc_register(pdev, NULL, NULL, 0);

	ret = sysfs_create_group(&info->dev->kobj, &sec_subtherm_group);

	if (ret) {
		dev_err(info->dev,
			"failed to create sysfs attribute group\n");
	}

	INIT_DELAYED_WORK_DEFERRABLE(&info->polling_work,
			sec_subtherm_polling_work);
	schedule_delayed_work(&info->polling_work,
			msecs_to_jiffies(info->pdata->polling_interval));

	return ret;
}

static int __devexit sec_subtherm_remove(struct platform_device *pdev)
{
	struct sec_subtherm_info *info = platform_get_drvdata(pdev);

	if (!info)
		return 0;

	sysfs_remove_group(&info->dev->kobj, &sec_subtherm_group);

	cancel_delayed_work(&info->polling_work);
	s3c_adc_release(info->padc);
	kfree(info);

	return 0;
}

#ifdef CONFIG_PM
static int sec_subtherm_suspend(struct device *dev)
{
	struct sec_subtherm_info *info = dev_get_drvdata(dev);

	cancel_delayed_work(&info->polling_work);

	return 0;
}

static int sec_subtherm_resume(struct device *dev)
{
	struct sec_subtherm_info *info = dev_get_drvdata(dev);

	schedule_delayed_work(&info->polling_work,
			msecs_to_jiffies(info->pdata->polling_interval));
	return 0;
}
#else
#define	sec_subtherm_suspend	NULL
#define sec_subtherm_resume	NULL
#endif

static const struct dev_pm_ops sec_subthermistor_pm_ops = {
	.suspend = sec_subtherm_suspend,
	.resume = sec_subtherm_resume,
};

static struct platform_driver sec_subthermistor_driver = {
	.driver = {
		.name = "sec-subthermistor",
		.owner = THIS_MODULE,
		.pm = &sec_subthermistor_pm_ops,
	},
	.probe = sec_subtherm_probe,
	.remove = __devexit_p(sec_subtherm_remove),
};

static int __init sec_subtherm_init(void)
{
	return platform_driver_register(&sec_subthermistor_driver);
}
module_init(sec_subtherm_init);

static void __exit sec_subtherm_exit(void)
{
	platform_driver_unregister(&sec_subthermistor_driver);
}
module_exit(sec_subtherm_exit);

MODULE_AUTHOR("bw.moon@samsung.com");
MODULE_DESCRIPTION("sec sub thermistor driver");
MODULE_LICENSE("GPL");
