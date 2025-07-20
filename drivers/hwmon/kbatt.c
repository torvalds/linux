// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 KEBA Industrial Automation GmbH
 *
 * Driver for KEBA battery monitoring controller FPGA IP core
 */

#include <linux/hwmon.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/auxiliary_bus.h>
#include <linux/misc/keba.h>
#include <linux/mutex.h>

#define KBATT "kbatt"

#define KBATT_CONTROL_REG		0x4
#define   KBATT_CONTROL_BAT_TEST	0x01

#define KBATT_STATUS_REG		0x8
#define   KBATT_STATUS_BAT_OK		0x01

#define KBATT_MAX_UPD_INTERVAL	(10 * HZ)
#define KBATT_SETTLE_TIME_US	(100 * USEC_PER_MSEC)

struct kbatt {
	/* update lock */
	struct mutex lock;
	void __iomem *base;

	unsigned long next_update; /* in jiffies */
	bool alarm;
};

static bool kbatt_alarm(struct kbatt *kbatt)
{
	mutex_lock(&kbatt->lock);

	if (!kbatt->next_update || time_after(jiffies, kbatt->next_update)) {
		/* switch load on */
		iowrite8(KBATT_CONTROL_BAT_TEST,
			 kbatt->base + KBATT_CONTROL_REG);

		/* wait some time to let things settle */
		fsleep(KBATT_SETTLE_TIME_US);

		/* check battery state */
		if (ioread8(kbatt->base + KBATT_STATUS_REG) &
		    KBATT_STATUS_BAT_OK)
			kbatt->alarm = false;
		else
			kbatt->alarm = true;

		/* switch load off */
		iowrite8(0, kbatt->base + KBATT_CONTROL_REG);

		kbatt->next_update = jiffies + KBATT_MAX_UPD_INTERVAL;
	}

	mutex_unlock(&kbatt->lock);

	return kbatt->alarm;
}

static int kbatt_read(struct device *dev, enum hwmon_sensor_types type,
		      u32 attr, int channel, long *val)
{
	struct kbatt *kbatt = dev_get_drvdata(dev);

	*val = kbatt_alarm(kbatt) ? 1 : 0;

	return 0;
}

static umode_t kbatt_is_visible(const void *data, enum hwmon_sensor_types type,
				u32 attr, int channel)
{
	if (channel == 0 && attr == hwmon_in_min_alarm)
		return 0444;

	return 0;
}

static const struct hwmon_channel_info *kbatt_info[] = {
	HWMON_CHANNEL_INFO(in,
			   /* 0: input minimum alarm channel */
			   HWMON_I_MIN_ALARM),
	NULL
};

static const struct hwmon_ops kbatt_hwmon_ops = {
	.is_visible = kbatt_is_visible,
	.read = kbatt_read,
};

static const struct hwmon_chip_info kbatt_chip_info = {
	.ops = &kbatt_hwmon_ops,
	.info = kbatt_info,
};

static int kbatt_probe(struct auxiliary_device *auxdev,
		       const struct auxiliary_device_id *id)
{
	struct keba_batt_auxdev *kbatt_auxdev =
		container_of(auxdev, struct keba_batt_auxdev, auxdev);
	struct device *dev = &auxdev->dev;
	struct device *hwmon_dev;
	struct kbatt *kbatt;
	int retval;

	kbatt = devm_kzalloc(dev, sizeof(*kbatt), GFP_KERNEL);
	if (!kbatt)
		return -ENOMEM;

	retval = devm_mutex_init(dev, &kbatt->lock);
	if (retval)
		return retval;

	kbatt->base = devm_ioremap_resource(dev, &kbatt_auxdev->io);
	if (IS_ERR(kbatt->base))
		return PTR_ERR(kbatt->base);

	hwmon_dev = devm_hwmon_device_register_with_info(dev, KBATT, kbatt,
							 &kbatt_chip_info,
							 NULL);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct auxiliary_device_id kbatt_devtype_aux[] = {
	{ .name = "keba.batt" },
	{}
};
MODULE_DEVICE_TABLE(auxiliary, kbatt_devtype_aux);

static struct auxiliary_driver kbatt_driver_aux = {
	.name = KBATT,
	.id_table = kbatt_devtype_aux,
	.probe = kbatt_probe,
};
module_auxiliary_driver(kbatt_driver_aux);

MODULE_AUTHOR("Petar Bojanic <boja@keba.com>");
MODULE_AUTHOR("Gerhard Engleder <eg@keba.com>");
MODULE_DESCRIPTION("KEBA battery monitoring controller driver");
MODULE_LICENSE("GPL");
