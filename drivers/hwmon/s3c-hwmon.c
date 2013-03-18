/* linux/drivers/hwmon/s3c-hwmon.c
 *
 * Copyright (C) 2005, 2008, 2009 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C24XX/S3C64XX ADC hwmon support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>

#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>

#include <plat/adc.h>
#include <linux/platform_data/hwmon-s3c.h>

struct s3c_hwmon_attr {
	struct sensor_device_attribute	in;
	struct sensor_device_attribute	label;
	char				in_name[12];
	char				label_name[12];
};

/**
 * struct s3c_hwmon - ADC hwmon client information
 * @lock: Access lock to serialise the conversions.
 * @client: The client we registered with the S3C ADC core.
 * @hwmon_dev: The hwmon device we created.
 * @attr: The holders for the channel attributes.
*/
struct s3c_hwmon {
	struct mutex		lock;
	struct s3c_adc_client	*client;
	struct device		*hwmon_dev;

	struct s3c_hwmon_attr	attrs[8];
};

/**
 * s3c_hwmon_read_ch - read a value from a given adc channel.
 * @dev: The device.
 * @hwmon: Our state.
 * @channel: The channel we're reading from.
 *
 * Read a value from the @channel with the proper locking and sleep until
 * either the read completes or we timeout awaiting the ADC core to get
 * back to us.
 */
static int s3c_hwmon_read_ch(struct device *dev,
			     struct s3c_hwmon *hwmon, int channel)
{
	int ret;

	ret = mutex_lock_interruptible(&hwmon->lock);
	if (ret < 0)
		return ret;

	dev_dbg(dev, "reading channel %d\n", channel);

	ret = s3c_adc_read(hwmon->client, channel);
	mutex_unlock(&hwmon->lock);

	return ret;
}

#ifdef CONFIG_SENSORS_S3C_RAW
/**
 * s3c_hwmon_show_raw - show a conversion from the raw channel number.
 * @dev: The device that the attribute belongs to.
 * @attr: The attribute being read.
 * @buf: The result buffer.
 *
 * This show deals with the raw attribute, registered for each possible
 * ADC channel. This does a conversion and returns the raw (un-scaled)
 * value returned from the hardware.
 */
static ssize_t s3c_hwmon_show_raw(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct s3c_hwmon *adc = platform_get_drvdata(to_platform_device(dev));
	struct sensor_device_attribute *sa = to_sensor_dev_attr(attr);
	int ret;

	ret = s3c_hwmon_read_ch(dev, adc, sa->index);

	return  (ret < 0) ? ret : snprintf(buf, PAGE_SIZE, "%d\n", ret);
}

#define DEF_ADC_ATTR(x)	\
	static SENSOR_DEVICE_ATTR(adc##x##_raw, S_IRUGO, s3c_hwmon_show_raw, NULL, x)

DEF_ADC_ATTR(0);
DEF_ADC_ATTR(1);
DEF_ADC_ATTR(2);
DEF_ADC_ATTR(3);
DEF_ADC_ATTR(4);
DEF_ADC_ATTR(5);
DEF_ADC_ATTR(6);
DEF_ADC_ATTR(7);

static struct attribute *s3c_hwmon_attrs[9] = {
	&sensor_dev_attr_adc0_raw.dev_attr.attr,
	&sensor_dev_attr_adc1_raw.dev_attr.attr,
	&sensor_dev_attr_adc2_raw.dev_attr.attr,
	&sensor_dev_attr_adc3_raw.dev_attr.attr,
	&sensor_dev_attr_adc4_raw.dev_attr.attr,
	&sensor_dev_attr_adc5_raw.dev_attr.attr,
	&sensor_dev_attr_adc6_raw.dev_attr.attr,
	&sensor_dev_attr_adc7_raw.dev_attr.attr,
	NULL,
};

static struct attribute_group s3c_hwmon_attrgroup = {
	.attrs	= s3c_hwmon_attrs,
};

static inline int s3c_hwmon_add_raw(struct device *dev)
{
	return sysfs_create_group(&dev->kobj, &s3c_hwmon_attrgroup);
}

static inline void s3c_hwmon_remove_raw(struct device *dev)
{
	sysfs_remove_group(&dev->kobj, &s3c_hwmon_attrgroup);
}

#else

static inline int s3c_hwmon_add_raw(struct device *dev) { return 0; }
static inline void s3c_hwmon_remove_raw(struct device *dev) { }

#endif /* CONFIG_SENSORS_S3C_RAW */

/**
 * s3c_hwmon_ch_show - show value of a given channel
 * @dev: The device that the attribute belongs to.
 * @attr: The attribute being read.
 * @buf: The result buffer.
 *
 * Read a value from the ADC and scale it before returning it to the
 * caller. The scale factor is gained from the channel configuration
 * passed via the platform data when the device was registered.
 */
static ssize_t s3c_hwmon_ch_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct sensor_device_attribute *sen_attr = to_sensor_dev_attr(attr);
	struct s3c_hwmon *hwmon = platform_get_drvdata(to_platform_device(dev));
	struct s3c_hwmon_pdata *pdata = dev->platform_data;
	struct s3c_hwmon_chcfg *cfg;
	int ret;

	cfg = pdata->in[sen_attr->index];

	ret = s3c_hwmon_read_ch(dev, hwmon, sen_attr->index);
	if (ret < 0)
		return ret;

	ret *= cfg->mult;
	ret = DIV_ROUND_CLOSEST(ret, cfg->div);

	return snprintf(buf, PAGE_SIZE, "%d\n", ret);
}

/**
 * s3c_hwmon_label_show - show label name of the given channel.
 * @dev: The device that the attribute belongs to.
 * @attr: The attribute being read.
 * @buf: The result buffer.
 *
 * Return the label name of a given channel
 */
static ssize_t s3c_hwmon_label_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct sensor_device_attribute *sen_attr = to_sensor_dev_attr(attr);
	struct s3c_hwmon_pdata *pdata = dev->platform_data;
	struct s3c_hwmon_chcfg *cfg;

	cfg = pdata->in[sen_attr->index];

	return snprintf(buf, PAGE_SIZE, "%s\n", cfg->name);
}

/**
 * s3c_hwmon_create_attr - create hwmon attribute for given channel.
 * @dev: The device to create the attribute on.
 * @cfg: The channel configuration passed from the platform data.
 * @channel: The ADC channel number to process.
 *
 * Create the scaled attribute for use with hwmon from the specified
 * platform data in @pdata. The sysfs entry is handled by the routine
 * s3c_hwmon_ch_show().
 *
 * The attribute name is taken from the configuration data if present
 * otherwise the name is taken by concatenating in_ with the channel
 * number.
 */
static int s3c_hwmon_create_attr(struct device *dev,
				 struct s3c_hwmon_chcfg *cfg,
				 struct s3c_hwmon_attr *attrs,
				 int channel)
{
	struct sensor_device_attribute *attr;
	int ret;

	snprintf(attrs->in_name, sizeof(attrs->in_name), "in%d_input", channel);

	attr = &attrs->in;
	attr->index = channel;
	sysfs_attr_init(&attr->dev_attr.attr);
	attr->dev_attr.attr.name  = attrs->in_name;
	attr->dev_attr.attr.mode  = S_IRUGO;
	attr->dev_attr.show = s3c_hwmon_ch_show;

	ret =  device_create_file(dev, &attr->dev_attr);
	if (ret < 0) {
		dev_err(dev, "failed to create input attribute\n");
		return ret;
	}

	/* if this has a name, add a label */
	if (cfg->name) {
		snprintf(attrs->label_name, sizeof(attrs->label_name),
			 "in%d_label", channel);

		attr = &attrs->label;
		attr->index = channel;
		sysfs_attr_init(&attr->dev_attr.attr);
		attr->dev_attr.attr.name  = attrs->label_name;
		attr->dev_attr.attr.mode  = S_IRUGO;
		attr->dev_attr.show = s3c_hwmon_label_show;

		ret = device_create_file(dev, &attr->dev_attr);
		if (ret < 0) {
			device_remove_file(dev, &attrs->in.dev_attr);
			dev_err(dev, "failed to create label attribute\n");
		}
	}

	return ret;
}

static void s3c_hwmon_remove_attr(struct device *dev,
				  struct s3c_hwmon_attr *attrs)
{
	device_remove_file(dev, &attrs->in.dev_attr);
	device_remove_file(dev, &attrs->label.dev_attr);
}

/**
 * s3c_hwmon_probe - device probe entry.
 * @dev: The device being probed.
*/
static int s3c_hwmon_probe(struct platform_device *dev)
{
	struct s3c_hwmon_pdata *pdata = dev->dev.platform_data;
	struct s3c_hwmon *hwmon;
	int ret = 0;
	int i;

	if (!pdata) {
		dev_err(&dev->dev, "no platform data supplied\n");
		return -EINVAL;
	}

	hwmon = devm_kzalloc(&dev->dev, sizeof(struct s3c_hwmon), GFP_KERNEL);
	if (hwmon == NULL) {
		dev_err(&dev->dev, "no memory\n");
		return -ENOMEM;
	}

	platform_set_drvdata(dev, hwmon);

	mutex_init(&hwmon->lock);

	/* Register with the core ADC driver. */

	hwmon->client = s3c_adc_register(dev, NULL, NULL, 0);
	if (IS_ERR(hwmon->client)) {
		dev_err(&dev->dev, "cannot register adc\n");
		return PTR_ERR(hwmon->client);
	}

	/* add attributes for our adc devices. */

	ret = s3c_hwmon_add_raw(&dev->dev);
	if (ret)
		goto err_registered;

	/* register with the hwmon core */

	hwmon->hwmon_dev = hwmon_device_register(&dev->dev);
	if (IS_ERR(hwmon->hwmon_dev)) {
		dev_err(&dev->dev, "error registering with hwmon\n");
		ret = PTR_ERR(hwmon->hwmon_dev);
		goto err_raw_attribute;
	}

	for (i = 0; i < ARRAY_SIZE(pdata->in); i++) {
		struct s3c_hwmon_chcfg *cfg = pdata->in[i];

		if (!cfg)
			continue;

		if (cfg->mult >= 0x10000)
			dev_warn(&dev->dev,
				 "channel %d multiplier too large\n",
				 i);

		if (cfg->div == 0) {
			dev_err(&dev->dev, "channel %d divider zero\n", i);
			continue;
		}

		ret = s3c_hwmon_create_attr(&dev->dev, pdata->in[i],
					    &hwmon->attrs[i], i);
		if (ret) {
			dev_err(&dev->dev,
					"error creating channel %d\n", i);

			for (i--; i >= 0; i--)
				s3c_hwmon_remove_attr(&dev->dev,
							      &hwmon->attrs[i]);

			goto err_hwmon_register;
		}
	}

	return 0;

 err_hwmon_register:
	hwmon_device_unregister(hwmon->hwmon_dev);

 err_raw_attribute:
	s3c_hwmon_remove_raw(&dev->dev);

 err_registered:
	s3c_adc_release(hwmon->client);

	return ret;
}

static int s3c_hwmon_remove(struct platform_device *dev)
{
	struct s3c_hwmon *hwmon = platform_get_drvdata(dev);
	int i;

	s3c_hwmon_remove_raw(&dev->dev);

	for (i = 0; i < ARRAY_SIZE(hwmon->attrs); i++)
		s3c_hwmon_remove_attr(&dev->dev, &hwmon->attrs[i]);

	hwmon_device_unregister(hwmon->hwmon_dev);
	s3c_adc_release(hwmon->client);

	return 0;
}

static struct platform_driver s3c_hwmon_driver = {
	.driver	= {
		.name		= "s3c-hwmon",
		.owner		= THIS_MODULE,
	},
	.probe		= s3c_hwmon_probe,
	.remove		= s3c_hwmon_remove,
};

module_platform_driver(s3c_hwmon_driver);

MODULE_AUTHOR("Ben Dooks <ben@simtec.co.uk>");
MODULE_DESCRIPTION("S3C ADC HWMon driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:s3c-hwmon");
