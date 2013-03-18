/*
 * Backlight driver for Analog Devices ADP5520/ADP5501 MFD PMICs
 *
 * Copyright 2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/fb.h>
#include <linux/backlight.h>
#include <linux/mfd/adp5520.h>
#include <linux/slab.h>
#include <linux/module.h>

struct adp5520_bl {
	struct device *master;
	struct adp5520_backlight_platform_data *pdata;
	struct mutex lock;
	unsigned long cached_daylight_max;
	int id;
	int current_brightness;
};

static int adp5520_bl_set(struct backlight_device *bl, int brightness)
{
	struct adp5520_bl *data = bl_get_data(bl);
	struct device *master = data->master;
	int ret = 0;

	if (data->pdata->en_ambl_sens) {
		if ((brightness > 0) && (brightness < ADP5020_MAX_BRIGHTNESS)) {
			/* Disable Ambient Light auto adjust */
			ret |= adp5520_clr_bits(master, ADP5520_BL_CONTROL,
					ADP5520_BL_AUTO_ADJ);
			ret |= adp5520_write(master, ADP5520_DAYLIGHT_MAX,
					brightness);
		} else {
			/*
			 * MAX_BRIGHTNESS -> Enable Ambient Light auto adjust
			 * restore daylight l3 sysfs brightness
			 */
			ret |= adp5520_write(master, ADP5520_DAYLIGHT_MAX,
					 data->cached_daylight_max);
			ret |= adp5520_set_bits(master, ADP5520_BL_CONTROL,
					 ADP5520_BL_AUTO_ADJ);
		}
	} else {
		ret |= adp5520_write(master, ADP5520_DAYLIGHT_MAX, brightness);
	}

	if (data->current_brightness && brightness == 0)
		ret |= adp5520_set_bits(master,
				ADP5520_MODE_STATUS, ADP5520_DIM_EN);
	else if (data->current_brightness == 0 && brightness)
		ret |= adp5520_clr_bits(master,
				ADP5520_MODE_STATUS, ADP5520_DIM_EN);

	if (!ret)
		data->current_brightness = brightness;

	return ret;
}

static int adp5520_bl_update_status(struct backlight_device *bl)
{
	int brightness = bl->props.brightness;
	if (bl->props.power != FB_BLANK_UNBLANK)
		brightness = 0;

	if (bl->props.fb_blank != FB_BLANK_UNBLANK)
		brightness = 0;

	return adp5520_bl_set(bl, brightness);
}

static int adp5520_bl_get_brightness(struct backlight_device *bl)
{
	struct adp5520_bl *data = bl_get_data(bl);
	int error;
	uint8_t reg_val;

	error = adp5520_read(data->master, ADP5520_BL_VALUE, &reg_val);

	return error ? data->current_brightness : reg_val;
}

static const struct backlight_ops adp5520_bl_ops = {
	.update_status	= adp5520_bl_update_status,
	.get_brightness	= adp5520_bl_get_brightness,
};

static int adp5520_bl_setup(struct backlight_device *bl)
{
	struct adp5520_bl *data = bl_get_data(bl);
	struct device *master = data->master;
	struct adp5520_backlight_platform_data *pdata = data->pdata;
	int ret = 0;

	ret |= adp5520_write(master, ADP5520_DAYLIGHT_MAX,
				pdata->l1_daylight_max);
	ret |= adp5520_write(master, ADP5520_DAYLIGHT_DIM,
				pdata->l1_daylight_dim);

	if (pdata->en_ambl_sens) {
		data->cached_daylight_max = pdata->l1_daylight_max;
		ret |= adp5520_write(master, ADP5520_OFFICE_MAX,
				pdata->l2_office_max);
		ret |= adp5520_write(master, ADP5520_OFFICE_DIM,
				pdata->l2_office_dim);
		ret |= adp5520_write(master, ADP5520_DARK_MAX,
				pdata->l3_dark_max);
		ret |= adp5520_write(master, ADP5520_DARK_DIM,
				pdata->l3_dark_dim);
		ret |= adp5520_write(master, ADP5520_L2_TRIP,
				pdata->l2_trip);
		ret |= adp5520_write(master, ADP5520_L2_HYS,
				pdata->l2_hyst);
		ret |= adp5520_write(master, ADP5520_L3_TRIP,
				 pdata->l3_trip);
		ret |= adp5520_write(master, ADP5520_L3_HYS,
				pdata->l3_hyst);
		ret |= adp5520_write(master, ADP5520_ALS_CMPR_CFG,
				ALS_CMPR_CFG_VAL(pdata->abml_filt,
				ADP5520_L3_EN));
	}

	ret |= adp5520_write(master, ADP5520_BL_CONTROL,
			BL_CTRL_VAL(pdata->fade_led_law,
					pdata->en_ambl_sens));

	ret |= adp5520_write(master, ADP5520_BL_FADE, FADE_VAL(pdata->fade_in,
			pdata->fade_out));

	ret |= adp5520_set_bits(master, ADP5520_MODE_STATUS,
			ADP5520_BL_EN | ADP5520_DIM_EN);

	return ret;
}

static ssize_t adp5520_show(struct device *dev, char *buf, int reg)
{
	struct adp5520_bl *data = dev_get_drvdata(dev);
	int error;
	uint8_t reg_val;

	mutex_lock(&data->lock);
	error = adp5520_read(data->master, reg, &reg_val);
	mutex_unlock(&data->lock);

	return sprintf(buf, "%u\n", reg_val);
}

static ssize_t adp5520_store(struct device *dev, const char *buf,
			 size_t count, int reg)
{
	struct adp5520_bl *data = dev_get_drvdata(dev);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	mutex_lock(&data->lock);
	adp5520_write(data->master, reg, val);
	mutex_unlock(&data->lock);

	return count;
}

static ssize_t adp5520_bl_dark_max_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return adp5520_show(dev, buf, ADP5520_DARK_MAX);
}

static ssize_t adp5520_bl_dark_max_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	return adp5520_store(dev, buf, count, ADP5520_DARK_MAX);
}
static DEVICE_ATTR(dark_max, 0664, adp5520_bl_dark_max_show,
			adp5520_bl_dark_max_store);

static ssize_t adp5520_bl_office_max_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return adp5520_show(dev, buf, ADP5520_OFFICE_MAX);
}

static ssize_t adp5520_bl_office_max_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	return adp5520_store(dev, buf, count, ADP5520_OFFICE_MAX);
}
static DEVICE_ATTR(office_max, 0664, adp5520_bl_office_max_show,
			adp5520_bl_office_max_store);

static ssize_t adp5520_bl_daylight_max_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return adp5520_show(dev, buf, ADP5520_DAYLIGHT_MAX);
}

static ssize_t adp5520_bl_daylight_max_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct adp5520_bl *data = dev_get_drvdata(dev);
	int ret;

	ret = kstrtoul(buf, 10, &data->cached_daylight_max);
	if (ret < 0)
		return ret;

	return adp5520_store(dev, buf, count, ADP5520_DAYLIGHT_MAX);
}
static DEVICE_ATTR(daylight_max, 0664, adp5520_bl_daylight_max_show,
			adp5520_bl_daylight_max_store);

static ssize_t adp5520_bl_dark_dim_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return adp5520_show(dev, buf, ADP5520_DARK_DIM);
}

static ssize_t adp5520_bl_dark_dim_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	return adp5520_store(dev, buf, count, ADP5520_DARK_DIM);
}
static DEVICE_ATTR(dark_dim, 0664, adp5520_bl_dark_dim_show,
			adp5520_bl_dark_dim_store);

static ssize_t adp5520_bl_office_dim_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return adp5520_show(dev, buf, ADP5520_OFFICE_DIM);
}

static ssize_t adp5520_bl_office_dim_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	return adp5520_store(dev, buf, count, ADP5520_OFFICE_DIM);
}
static DEVICE_ATTR(office_dim, 0664, adp5520_bl_office_dim_show,
			adp5520_bl_office_dim_store);

static ssize_t adp5520_bl_daylight_dim_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return adp5520_show(dev, buf, ADP5520_DAYLIGHT_DIM);
}

static ssize_t adp5520_bl_daylight_dim_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	return adp5520_store(dev, buf, count, ADP5520_DAYLIGHT_DIM);
}
static DEVICE_ATTR(daylight_dim, 0664, adp5520_bl_daylight_dim_show,
			adp5520_bl_daylight_dim_store);

static struct attribute *adp5520_bl_attributes[] = {
	&dev_attr_dark_max.attr,
	&dev_attr_dark_dim.attr,
	&dev_attr_office_max.attr,
	&dev_attr_office_dim.attr,
	&dev_attr_daylight_max.attr,
	&dev_attr_daylight_dim.attr,
	NULL
};

static const struct attribute_group adp5520_bl_attr_group = {
	.attrs = adp5520_bl_attributes,
};

static int adp5520_bl_probe(struct platform_device *pdev)
{
	struct backlight_properties props;
	struct backlight_device *bl;
	struct adp5520_bl *data;
	int ret = 0;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	data->master = pdev->dev.parent;
	data->pdata = pdev->dev.platform_data;

	if (data->pdata  == NULL) {
		dev_err(&pdev->dev, "missing platform data\n");
		return -ENODEV;
	}

	data->id = pdev->id;
	data->current_brightness = 0;

	mutex_init(&data->lock);

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = ADP5020_MAX_BRIGHTNESS;
	bl = backlight_device_register(pdev->name, data->master, data,
				       &adp5520_bl_ops, &props);
	if (IS_ERR(bl)) {
		dev_err(&pdev->dev, "failed to register backlight\n");
		return PTR_ERR(bl);
	}

	bl->props.brightness = ADP5020_MAX_BRIGHTNESS;
	if (data->pdata->en_ambl_sens)
		ret = sysfs_create_group(&bl->dev.kobj,
			&adp5520_bl_attr_group);

	if (ret) {
		dev_err(&pdev->dev, "failed to register sysfs\n");
		backlight_device_unregister(bl);
	}

	platform_set_drvdata(pdev, bl);
	ret |= adp5520_bl_setup(bl);
	backlight_update_status(bl);

	return ret;
}

static int adp5520_bl_remove(struct platform_device *pdev)
{
	struct backlight_device *bl = platform_get_drvdata(pdev);
	struct adp5520_bl *data = bl_get_data(bl);

	adp5520_clr_bits(data->master, ADP5520_MODE_STATUS, ADP5520_BL_EN);

	if (data->pdata->en_ambl_sens)
		sysfs_remove_group(&bl->dev.kobj,
				&adp5520_bl_attr_group);

	backlight_device_unregister(bl);

	return 0;
}

#ifdef CONFIG_PM
static int adp5520_bl_suspend(struct platform_device *pdev,
				 pm_message_t state)
{
	struct backlight_device *bl = platform_get_drvdata(pdev);
	return adp5520_bl_set(bl, 0);
}

static int adp5520_bl_resume(struct platform_device *pdev)
{
	struct backlight_device *bl = platform_get_drvdata(pdev);

	backlight_update_status(bl);
	return 0;
}
#else
#define adp5520_bl_suspend	NULL
#define adp5520_bl_resume	NULL
#endif

static struct platform_driver adp5520_bl_driver = {
	.driver		= {
		.name	= "adp5520-backlight",
		.owner	= THIS_MODULE,
	},
	.probe		= adp5520_bl_probe,
	.remove		= adp5520_bl_remove,
	.suspend	= adp5520_bl_suspend,
	.resume		= adp5520_bl_resume,
};

module_platform_driver(adp5520_bl_driver);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("ADP5520(01) Backlight Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:adp5520-backlight");
