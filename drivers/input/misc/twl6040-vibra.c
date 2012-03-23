/*
 * twl6040-vibra.c - TWL6040 Vibrator driver
 *
 * Author:      Jorge Eduardo Candelaria <jorge.candelaria@ti.com>
 * Author:      Misael Lopez Cruz <misael.lopez@ti.com>
 *
 * Copyright:   (C) 2011 Texas Instruments, Inc.
 *
 * Based on twl4030-vibra.c by Henrik Saari <henrik.saari@nokia.com>
 *				Felipe Balbi <felipe.balbi@nokia.com>
 *				Jari Vanhala <ext-javi.vanhala@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/i2c/twl.h>
#include <linux/mfd/twl6040.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>

#define EFFECT_DIR_180_DEG	0x8000

/* Recommended modulation index 85% */
#define TWL6040_VIBRA_MOD	85

#define TWL6040_NUM_SUPPLIES 2

struct vibra_info {
	struct device *dev;
	struct input_dev *input_dev;
	struct workqueue_struct *workqueue;
	struct work_struct play_work;
	struct mutex mutex;
	int irq;

	bool enabled;
	int weak_speed;
	int strong_speed;
	int direction;

	unsigned int vibldrv_res;
	unsigned int vibrdrv_res;
	unsigned int viblmotor_res;
	unsigned int vibrmotor_res;

	struct regulator_bulk_data supplies[TWL6040_NUM_SUPPLIES];

	struct twl6040 *twl6040;
};

static irqreturn_t twl6040_vib_irq_handler(int irq, void *data)
{
	struct vibra_info *info = data;
	struct twl6040 *twl6040 = info->twl6040;
	u8 status;

	status = twl6040_reg_read(twl6040, TWL6040_REG_STATUS);
	if (status & TWL6040_VIBLOCDET) {
		dev_warn(info->dev, "Left Vibrator overcurrent detected\n");
		twl6040_clear_bits(twl6040, TWL6040_REG_VIBCTLL,
				   TWL6040_VIBENA);
	}
	if (status & TWL6040_VIBROCDET) {
		dev_warn(info->dev, "Right Vibrator overcurrent detected\n");
		twl6040_clear_bits(twl6040, TWL6040_REG_VIBCTLR,
				   TWL6040_VIBENA);
	}

	return IRQ_HANDLED;
}

static void twl6040_vibra_enable(struct vibra_info *info)
{
	struct twl6040 *twl6040 = info->twl6040;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(info->supplies), info->supplies);
	if (ret) {
		dev_err(info->dev, "failed to enable regulators %d\n", ret);
		return;
	}

	twl6040_power(info->twl6040, 1);
	if (twl6040_get_revid(twl6040) <= TWL6040_REV_ES1_1) {
		/*
		 * ERRATA: Disable overcurrent protection for at least
		 * 3ms when enabling vibrator drivers to avoid false
		 * overcurrent detection
		 */
		twl6040_reg_write(twl6040, TWL6040_REG_VIBCTLL,
				  TWL6040_VIBENA | TWL6040_VIBCTRL);
		twl6040_reg_write(twl6040, TWL6040_REG_VIBCTLR,
				  TWL6040_VIBENA | TWL6040_VIBCTRL);
		usleep_range(3000, 3500);
	}

	twl6040_reg_write(twl6040, TWL6040_REG_VIBCTLL,
			  TWL6040_VIBENA);
	twl6040_reg_write(twl6040, TWL6040_REG_VIBCTLR,
			  TWL6040_VIBENA);

	info->enabled = true;
}

static void twl6040_vibra_disable(struct vibra_info *info)
{
	struct twl6040 *twl6040 = info->twl6040;

	twl6040_reg_write(twl6040, TWL6040_REG_VIBCTLL, 0x00);
	twl6040_reg_write(twl6040, TWL6040_REG_VIBCTLR, 0x00);
	twl6040_power(info->twl6040, 0);

	regulator_bulk_disable(ARRAY_SIZE(info->supplies), info->supplies);

	info->enabled = false;
}

static u8 twl6040_vibra_code(int vddvib, int vibdrv_res, int motor_res,
			     int speed, int direction)
{
	int vpk, max_code;
	u8 vibdat;

	/* output swing */
	vpk = (vddvib * motor_res * TWL6040_VIBRA_MOD) /
		(100 * (vibdrv_res + motor_res));

	/* 50mV per VIBDAT code step */
	max_code = vpk / 50;
	if (max_code > TWL6040_VIBDAT_MAX)
		max_code = TWL6040_VIBDAT_MAX;

	/* scale speed to max allowed code */
	vibdat = (u8)((speed * max_code) / USHRT_MAX);

	/* 2's complement for direction > 180 degrees */
	vibdat *= direction;

	return vibdat;
}

static void twl6040_vibra_set_effect(struct vibra_info *info)
{
	struct twl6040 *twl6040 = info->twl6040;
	u8 vibdatl, vibdatr;
	int volt;

	/* weak motor */
	volt = regulator_get_voltage(info->supplies[0].consumer) / 1000;
	vibdatl = twl6040_vibra_code(volt, info->vibldrv_res,
				     info->viblmotor_res,
				     info->weak_speed, info->direction);

	/* strong motor */
	volt = regulator_get_voltage(info->supplies[1].consumer) / 1000;
	vibdatr = twl6040_vibra_code(volt, info->vibrdrv_res,
				     info->vibrmotor_res,
				     info->strong_speed, info->direction);

	twl6040_reg_write(twl6040, TWL6040_REG_VIBDATL, vibdatl);
	twl6040_reg_write(twl6040, TWL6040_REG_VIBDATR, vibdatr);
}

static void vibra_play_work(struct work_struct *work)
{
	struct vibra_info *info = container_of(work,
				struct vibra_info, play_work);

	mutex_lock(&info->mutex);

	if (info->weak_speed || info->strong_speed) {
		if (!info->enabled)
			twl6040_vibra_enable(info);

		twl6040_vibra_set_effect(info);
	} else if (info->enabled)
		twl6040_vibra_disable(info);

	mutex_unlock(&info->mutex);
}

static int vibra_play(struct input_dev *input, void *data,
		      struct ff_effect *effect)
{
	struct vibra_info *info = input_get_drvdata(input);
	int ret;

	/* Do not allow effect, while the routing is set to use audio */
	ret = twl6040_get_vibralr_status(info->twl6040);
	if (ret & TWL6040_VIBSEL) {
		dev_info(&input->dev, "Vibra is configured for audio\n");
		return -EBUSY;
	}

	info->weak_speed = effect->u.rumble.weak_magnitude;
	info->strong_speed = effect->u.rumble.strong_magnitude;
	info->direction = effect->direction < EFFECT_DIR_180_DEG ? 1 : -1;

	ret = queue_work(info->workqueue, &info->play_work);
	if (!ret) {
		dev_info(&input->dev, "work is already on queue\n");
		return ret;
	}

	return 0;
}

static void twl6040_vibra_close(struct input_dev *input)
{
	struct vibra_info *info = input_get_drvdata(input);

	cancel_work_sync(&info->play_work);

	mutex_lock(&info->mutex);

	if (info->enabled)
		twl6040_vibra_disable(info);

	mutex_unlock(&info->mutex);
}

#ifdef CONFIG_PM_SLEEP
static int twl6040_vibra_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct vibra_info *info = platform_get_drvdata(pdev);

	mutex_lock(&info->mutex);

	if (info->enabled)
		twl6040_vibra_disable(info);

	mutex_unlock(&info->mutex);

	return 0;
}

#endif

static SIMPLE_DEV_PM_OPS(twl6040_vibra_pm_ops, twl6040_vibra_suspend, NULL);

static int __devinit twl6040_vibra_probe(struct platform_device *pdev)
{
	struct twl4030_vibra_data *pdata = pdev->dev.platform_data;
	struct vibra_info *info;
	int ret;

	if (!pdata) {
		dev_err(&pdev->dev, "platform_data not available\n");
		return -EINVAL;
	}

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		dev_err(&pdev->dev, "couldn't allocate memory\n");
		return -ENOMEM;
	}

	info->dev = &pdev->dev;
	info->twl6040 = dev_get_drvdata(pdev->dev.parent);
	info->vibldrv_res = pdata->vibldrv_res;
	info->vibrdrv_res = pdata->vibrdrv_res;
	info->viblmotor_res = pdata->viblmotor_res;
	info->vibrmotor_res = pdata->vibrmotor_res;
	if ((!info->vibldrv_res && !info->viblmotor_res) ||
	    (!info->vibrdrv_res && !info->vibrmotor_res)) {
		dev_err(info->dev, "invalid vibra driver/motor resistance\n");
		ret = -EINVAL;
		goto err_kzalloc;
	}

	info->irq = platform_get_irq(pdev, 0);
	if (info->irq < 0) {
		dev_err(info->dev, "invalid irq\n");
		ret = -EINVAL;
		goto err_kzalloc;
	}

	mutex_init(&info->mutex);

	info->input_dev = input_allocate_device();
	if (info->input_dev == NULL) {
		dev_err(info->dev, "couldn't allocate input device\n");
		ret = -ENOMEM;
		goto err_kzalloc;
	}

	input_set_drvdata(info->input_dev, info);

	info->input_dev->name = "twl6040:vibrator";
	info->input_dev->id.version = 1;
	info->input_dev->dev.parent = pdev->dev.parent;
	info->input_dev->close = twl6040_vibra_close;
	__set_bit(FF_RUMBLE, info->input_dev->ffbit);

	ret = input_ff_create_memless(info->input_dev, NULL, vibra_play);
	if (ret < 0) {
		dev_err(info->dev, "couldn't register vibrator to FF\n");
		goto err_ialloc;
	}

	ret = input_register_device(info->input_dev);
	if (ret < 0) {
		dev_err(info->dev, "couldn't register input device\n");
		goto err_iff;
	}

	platform_set_drvdata(pdev, info);

	ret = request_threaded_irq(info->irq, NULL, twl6040_vib_irq_handler, 0,
				   "twl6040_irq_vib", info);
	if (ret) {
		dev_err(info->dev, "VIB IRQ request failed: %d\n", ret);
		goto err_irq;
	}

	info->supplies[0].supply = "vddvibl";
	info->supplies[1].supply = "vddvibr";
	ret = regulator_bulk_get(info->dev, ARRAY_SIZE(info->supplies),
				 info->supplies);
	if (ret) {
		dev_err(info->dev, "couldn't get regulators %d\n", ret);
		goto err_regulator;
	}

	if (pdata->vddvibl_uV) {
		ret = regulator_set_voltage(info->supplies[0].consumer,
					    pdata->vddvibl_uV,
					    pdata->vddvibl_uV);
		if (ret) {
			dev_err(info->dev, "failed to set VDDVIBL volt %d\n",
				ret);
			goto err_voltage;
		}
	}

	if (pdata->vddvibr_uV) {
		ret = regulator_set_voltage(info->supplies[1].consumer,
					    pdata->vddvibr_uV,
					    pdata->vddvibr_uV);
		if (ret) {
			dev_err(info->dev, "failed to set VDDVIBR volt %d\n",
				ret);
			goto err_voltage;
		}
	}

	info->workqueue = alloc_workqueue("twl6040-vibra", 0, 0);
	if (info->workqueue == NULL) {
		dev_err(info->dev, "couldn't create workqueue\n");
		ret = -ENOMEM;
		goto err_voltage;
	}
	INIT_WORK(&info->play_work, vibra_play_work);

	return 0;

err_voltage:
	regulator_bulk_free(ARRAY_SIZE(info->supplies), info->supplies);
err_regulator:
	free_irq(info->irq, info);
err_irq:
	input_unregister_device(info->input_dev);
	info->input_dev = NULL;
err_iff:
	if (info->input_dev)
		input_ff_destroy(info->input_dev);
err_ialloc:
	input_free_device(info->input_dev);
err_kzalloc:
	kfree(info);
	return ret;
}

static int __devexit twl6040_vibra_remove(struct platform_device *pdev)
{
	struct vibra_info *info = platform_get_drvdata(pdev);

	input_unregister_device(info->input_dev);
	free_irq(info->irq, info);
	regulator_bulk_free(ARRAY_SIZE(info->supplies), info->supplies);
	destroy_workqueue(info->workqueue);
	kfree(info);

	return 0;
}

static struct platform_driver twl6040_vibra_driver = {
	.probe		= twl6040_vibra_probe,
	.remove		= __devexit_p(twl6040_vibra_remove),
	.driver		= {
		.name	= "twl6040-vibra",
		.owner	= THIS_MODULE,
		.pm	= &twl6040_vibra_pm_ops,
	},
};
module_platform_driver(twl6040_vibra_driver);

MODULE_ALIAS("platform:twl6040-vibra");
MODULE_DESCRIPTION("TWL6040 Vibra driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jorge Eduardo Candelaria <jorge.candelaria@ti.com>");
MODULE_AUTHOR("Misael Lopez Cruz <misael.lopez@ti.com>");
