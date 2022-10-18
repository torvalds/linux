// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt)	"%s: " fmt, __func__

#include <linux/errno.h>
#include <linux/hrtimer.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/workqueue.h>

/* Vibrator-LDO register definitions */
#define QPNP_VIB_LDO_REG_STATUS1	0x08
#define QPNP_VIB_LDO_VREG_READY		BIT(7)

#define QPNP_VIB_LDO_REG_VSET_LB	0x40

#define QPNP_VIB_LDO_REG_EN_CTL		0x46
#define QPNP_VIB_LDO_EN			BIT(7)

/* Vibrator-LDO voltage settings */
#define QPNP_VIB_LDO_VMIN_UV		1504000
#define QPNP_VIB_LDO_VMAX_UV		3544000
#define QPNP_VIB_LDO_VOLT_STEP_UV	8000

/*
 * Define vibration periods: default(5sec), min(50ms), max(15sec) and
 * overdrive(30ms).
 */
#define QPNP_VIB_MIN_PLAY_MS		50
#define QPNP_VIB_PLAY_MS		5000
#define QPNP_VIB_MAX_PLAY_MS		15000
#define QPNP_VIB_OVERDRIVE_PLAY_MS	30

struct vib_ldo_chip {
	struct led_classdev	cdev;
	struct regmap		*regmap;
	struct mutex		lock;
	struct hrtimer		overdrive_timer;
	struct work_struct	overdrive_work;
	u16			base;
	int			vmax_uV;
	int			overdrive_volt_uV;
	int			ldo_uV;
	int			state;
	u64			vib_play_ms;
	bool			vib_enabled;
	bool			disable_overdrive;
};

static inline int qpnp_vib_ldo_poll_status(struct vib_ldo_chip *chip)
{
	unsigned int val;
	int ret;

	ret = regmap_read_poll_timeout(chip->regmap,
			chip->base + QPNP_VIB_LDO_REG_STATUS1, val,
			val & QPNP_VIB_LDO_VREG_READY, 100, 1000);
	if (ret < 0) {
		pr_err("Vibrator LDO vreg_ready timeout, status=0x%02x, ret=%d\n",
			val, ret);

		/* Keep VIB_LDO disabled */
		regmap_update_bits(chip->regmap,
			chip->base + QPNP_VIB_LDO_REG_EN_CTL,
			QPNP_VIB_LDO_EN, 0);
	}

	return ret;
}

static int qpnp_vib_ldo_set_voltage(struct vib_ldo_chip *chip, int new_uV)
{
	u32 vlevel;
	u8 reg[2];
	int ret;

	if (chip->ldo_uV == new_uV)
		return 0;

	vlevel = roundup(new_uV, QPNP_VIB_LDO_VOLT_STEP_UV) / 1000;
	reg[0] = vlevel & 0xff;
	reg[1] = (vlevel & 0xff00) >> 8;
	ret = regmap_bulk_write(chip->regmap,
				chip->base + QPNP_VIB_LDO_REG_VSET_LB, reg, 2);
	if (ret < 0) {
		pr_err("regmap write failed, ret=%d\n", ret);
		return ret;
	}

	if (chip->vib_enabled) {
		ret = qpnp_vib_ldo_poll_status(chip);
		if (ret < 0) {
			pr_err("Vibrator LDO status polling timedout\n");
			return ret;
		}
	}

	chip->ldo_uV = new_uV;
	return ret;
}

static inline int qpnp_vib_ldo_enable(struct vib_ldo_chip *chip, bool enable)
{
	int ret;

	if (chip->vib_enabled == enable)
		return 0;

	ret = regmap_update_bits(chip->regmap,
				chip->base + QPNP_VIB_LDO_REG_EN_CTL,
				QPNP_VIB_LDO_EN,
				enable ? QPNP_VIB_LDO_EN : 0);
	if (ret < 0) {
		pr_err("Program Vibrator LDO %s is failed, ret=%d\n",
			enable ? "enable" : "disable", ret);
		return ret;
	}

	if (enable) {
		ret = qpnp_vib_ldo_poll_status(chip);
		if (ret < 0) {
			pr_err("Vibrator LDO status polling timedout\n");
			return ret;
		}
	}

	chip->vib_enabled = enable;

	return ret;
}

static int qpnp_vibrator_play_on(struct vib_ldo_chip *chip)
{
	int volt_uV;
	int ret;

	volt_uV = chip->vmax_uV;
	if (!chip->disable_overdrive)
		volt_uV = chip->overdrive_volt_uV ? chip->overdrive_volt_uV
				: min(chip->vmax_uV * 2, QPNP_VIB_LDO_VMAX_UV);

	ret = qpnp_vib_ldo_set_voltage(chip, volt_uV);
	if (ret < 0) {
		pr_err("set voltage = %duV failed, ret=%d\n", volt_uV, ret);
		return ret;
	}
	pr_debug("voltage set to %d uV\n", volt_uV);

	ret = qpnp_vib_ldo_enable(chip, true);
	if (ret < 0) {
		pr_err("vibration enable failed, ret=%d\n", ret);
		return ret;
	}

	if (!chip->disable_overdrive)
		hrtimer_start(&chip->overdrive_timer,
			ms_to_ktime(QPNP_VIB_OVERDRIVE_PLAY_MS),
			HRTIMER_MODE_REL);

	return ret;
}

static void qpnp_vib_overdrive_work(struct work_struct *work)
{
	struct vib_ldo_chip *chip = container_of(work, struct vib_ldo_chip,
					     overdrive_work);
	int ret;

	mutex_lock(&chip->lock);

	/* LDO voltage update not required if Vibration disabled */
	if (!chip->vib_enabled)
		goto unlock;

	ret = qpnp_vib_ldo_set_voltage(chip, chip->vmax_uV);
	if (ret < 0) {
		pr_err("set vibration voltage = %duV failed, ret=%d\n",
			chip->vmax_uV, ret);
		qpnp_vib_ldo_enable(chip, false);
		goto unlock;
	}
	pr_debug("voltage set to %d\n", chip->vmax_uV);

unlock:
	mutex_unlock(&chip->lock);
}

static enum hrtimer_restart vib_overdrive_timer(struct hrtimer *timer)
{
	struct vib_ldo_chip *chip = container_of(timer, struct vib_ldo_chip,
					     overdrive_timer);
	schedule_work(&chip->overdrive_work);
	pr_debug("overdrive timer expired\n");
	return HRTIMER_NORESTART;
}

static ssize_t qpnp_vib_show_vmax(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct vib_ldo_chip *chip = container_of(cdev, struct vib_ldo_chip,
						cdev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", chip->vmax_uV / 1000);
}

static ssize_t qpnp_vib_store_vmax(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct vib_ldo_chip *chip = container_of(cdev, struct vib_ldo_chip,
						cdev);
	int data, ret;

	ret = kstrtoint(buf, 10, &data);
	if (ret < 0)
		return ret;

	data = data * 1000; /* Convert to microvolts */

	/* check against vibrator ldo min/max voltage limits */
	data = min(data, QPNP_VIB_LDO_VMAX_UV);
	data = max(data, QPNP_VIB_LDO_VMIN_UV);

	mutex_lock(&chip->lock);
	chip->vmax_uV = data;
	mutex_unlock(&chip->lock);
	return count;
}

static struct device_attribute qpnp_vib_attrs =
	__ATTR(vmax_mv, 0644, qpnp_vib_show_vmax, qpnp_vib_store_vmax);

static int qpnp_vib_parse_dt(struct device *dev, struct vib_ldo_chip *chip)
{
	int ret;

	ret = of_property_read_u32(dev->of_node, "qcom,vib-ldo-volt-uv",
				&chip->vmax_uV);
	if (ret < 0) {
		pr_err("qcom,vib-ldo-volt-uv property read failed, ret=%d\n",
			ret);
		return ret;
	}

	chip->disable_overdrive = of_property_read_bool(dev->of_node,
					"qcom,disable-overdrive");

	if (of_find_property(dev->of_node, "qcom,vib-overdrive-volt-uv",
			     NULL)) {
		ret = of_property_read_u32(dev->of_node,
					   "qcom,vib-overdrive-volt-uv",
					   &chip->overdrive_volt_uV);
		if (ret < 0) {
			pr_err("qcom,vib-overdrive-volt-uv property read failed, ret=%d\n",
				ret);
			return ret;
		}

		/* check against vibrator ldo min/max voltage limits */
		chip->overdrive_volt_uV = min(chip->overdrive_volt_uV,
						QPNP_VIB_LDO_VMAX_UV);
		chip->overdrive_volt_uV = max(chip->overdrive_volt_uV,
						QPNP_VIB_LDO_VMIN_UV);
	}

	return ret;
}

static enum led_brightness qpnp_vib_brightness_get(struct led_classdev *cdev)
{
	struct vib_ldo_chip *chip = container_of(cdev, struct vib_ldo_chip,
						cdev);

	return chip->state;
}

static int qpnp_vib_brightness_set(struct led_classdev *cdev,
			enum led_brightness level)
{
	struct vib_ldo_chip *chip = container_of(cdev, struct vib_ldo_chip,
						cdev);
	int ret = 0;

	chip->state = level;

	if (chip->state) {
		ret = qpnp_vibrator_play_on(chip);
		if (ret < 0)
			pr_err("set vibrator-on failed, ret=%d\n", ret);
	} else {
		if (!chip->disable_overdrive) {
			hrtimer_cancel(&chip->overdrive_timer);
			cancel_work_sync(&chip->overdrive_work);
		}
		ret = qpnp_vib_ldo_enable(chip, false);
	}

	pr_debug("vibrator state=%d\n", chip->state);
	return ret;
}

static int qpnp_vibrator_ldo_suspend(struct device *dev)
{
	struct vib_ldo_chip *chip = dev_get_drvdata(dev);

	mutex_lock(&chip->lock);
	if (!chip->disable_overdrive) {
		hrtimer_cancel(&chip->overdrive_timer);
		cancel_work_sync(&chip->overdrive_work);
	}
	qpnp_vib_ldo_enable(chip, false);
	mutex_unlock(&chip->lock);

	return 0;
}
static SIMPLE_DEV_PM_OPS(qpnp_vibrator_ldo_pm_ops, qpnp_vibrator_ldo_suspend,
			NULL);

static int qpnp_vibrator_ldo_probe(struct platform_device *pdev)
{
	struct device_node *of_node = pdev->dev.of_node;
	struct vib_ldo_chip *chip;
	int ret;
	u32 base;

	ret = of_property_read_u32(of_node, "reg", &base);
	if (ret < 0) {
		pr_err("reg property reading failed, ret=%d\n", ret);
		return ret;
	}

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!chip->regmap) {
		pr_err("couldn't get parent's regmap\n");
		return -EINVAL;
	}

	ret = qpnp_vib_parse_dt(&pdev->dev, chip);
	if (ret < 0) {
		pr_err("couldn't parse device tree, ret=%d\n", ret);
		return ret;
	}

	chip->base = (uint16_t)base;
	chip->vib_play_ms = QPNP_VIB_PLAY_MS;
	mutex_init(&chip->lock);
	INIT_WORK(&chip->overdrive_work, qpnp_vib_overdrive_work);

	hrtimer_init(&chip->overdrive_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	chip->overdrive_timer.function = vib_overdrive_timer;
	dev_set_drvdata(&pdev->dev, chip);

	chip->cdev.name = "vibrator";
	chip->cdev.brightness_get = qpnp_vib_brightness_get;
	chip->cdev.brightness_set_blocking = qpnp_vib_brightness_set;
	chip->cdev.max_brightness = 100;
	ret = devm_led_classdev_register(&pdev->dev, &chip->cdev);
	if (ret < 0) {
		pr_err("Error in registering led class device, ret=%d\n", ret);
		goto fail;
	}

	ret = sysfs_create_file(&chip->cdev.dev->kobj,
			&qpnp_vib_attrs.attr);
	if (ret < 0) {
		dev_err(&pdev->dev, "Error in creating sysfs file, ret=%d\n",
			ret);
		goto sysfs_fail;
	}

	pr_info("Vibrator LDO successfully registered: uV = %d, overdrive = %s\n",
		chip->vmax_uV,
		chip->disable_overdrive ? "disabled" : "enabled");
	return 0;

sysfs_fail:
	sysfs_remove_file(&chip->cdev.dev->kobj,
			&qpnp_vib_attrs.attr);
fail:
	mutex_destroy(&chip->lock);
	dev_set_drvdata(&pdev->dev, NULL);
	return ret;
}

static int qpnp_vibrator_ldo_remove(struct platform_device *pdev)
{
	struct vib_ldo_chip *chip = dev_get_drvdata(&pdev->dev);

	if (!chip->disable_overdrive) {
		hrtimer_cancel(&chip->overdrive_timer);
		cancel_work_sync(&chip->overdrive_work);
	}
	mutex_destroy(&chip->lock);
	dev_set_drvdata(&pdev->dev, NULL);

	return 0;
}

static const struct of_device_id vibrator_ldo_match_table[] = {
	{ .compatible = "qcom,qpnp-vibrator-ldo" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, vibrator_ldo_match_table);

static struct platform_driver qpnp_vibrator_ldo_driver = {
	.driver	= {
		.name		= "qcom,qpnp-vibrator-ldo",
		.of_match_table	= vibrator_ldo_match_table,
		.pm		= &qpnp_vibrator_ldo_pm_ops,
	},
	.probe	= qpnp_vibrator_ldo_probe,
	.remove	= qpnp_vibrator_ldo_remove,
};
module_platform_driver(qpnp_vibrator_ldo_driver);

MODULE_DESCRIPTION("QPNP Vibrator-LDO driver");
MODULE_LICENSE("GPL v2");
