/*
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "../iio.h"
#include "../trigger.h"

static ssize_t iio_sysfs_trigger_poll(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct iio_trigger *trig = dev_get_drvdata(dev);
	iio_trigger_poll(trig, 0);

	return count;
}

static DEVICE_ATTR(trigger_now, S_IWUSR, NULL, iio_sysfs_trigger_poll);
static IIO_TRIGGER_NAME_ATTR;

static struct attribute *iio_sysfs_trigger_attrs[] = {
	&dev_attr_trigger_now.attr,
	&dev_attr_name.attr,
	NULL,
};

static const struct attribute_group iio_sysfs_trigger_attr_group = {
	.attrs = iio_sysfs_trigger_attrs,
};

static int __devinit iio_sysfs_trigger_probe(struct platform_device *pdev)
{
	struct iio_trigger *trig;
	int ret;

	trig = iio_allocate_trigger();
	if (!trig) {
		ret = -ENOMEM;
		goto out1;
	}

	trig->control_attrs = &iio_sysfs_trigger_attr_group;
	trig->owner = THIS_MODULE;
	trig->name = kasprintf(GFP_KERNEL, "sysfstrig%d", pdev->id);
	if (trig->name == NULL) {
		ret = -ENOMEM;
		goto out2;
	}

	ret = iio_trigger_register(trig);
	if (ret)
		goto out3;

	platform_set_drvdata(pdev, trig);

	return 0;
out3:
	kfree(trig->name);
out2:
	iio_put_trigger(trig);
out1:

	return ret;
}

static int __devexit iio_sysfs_trigger_remove(struct platform_device *pdev)
{
	struct iio_trigger *trig = platform_get_drvdata(pdev);

	iio_trigger_unregister(trig);
	kfree(trig->name);
	iio_put_trigger(trig);

	return 0;
}

static struct platform_driver iio_sysfs_trigger_driver = {
	.driver = {
		.name = "iio_sysfs_trigger",
		.owner = THIS_MODULE,
	},
	.probe = iio_sysfs_trigger_probe,
	.remove = __devexit_p(iio_sysfs_trigger_remove),
};

static int __init iio_sysfs_trig_init(void)
{
	return platform_driver_register(&iio_sysfs_trigger_driver);
}
module_init(iio_sysfs_trig_init);

static void __exit iio_sysfs_trig_exit(void)
{
	platform_driver_unregister(&iio_sysfs_trigger_driver);
}
module_exit(iio_sysfs_trig_exit);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("Sysfs based trigger for the iio subsystem");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:iio-trig-sysfs");
