/*
* Copyright (C) 2012 Invensense, Inc.
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/delay.h>
#include <linux/kfifo.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/of_gpio.h>
#include <linux/of.h>
#include "inv_mpu_iio.h"

static int inv_hid_read(struct inv_mpu_iio_s *st, u8 reg, int len, u8 *data)
{
	return 0;
}

static int inv_hid_single_write(struct inv_mpu_iio_s *st, u8 reg, u8 data)
{
	return 0;
}

static int inv_hid_secondary_read(struct inv_mpu_iio_s *st, u8 reg, int len, u8 *data)
{
	return 0;
}

static int inv_hid_secondary_write(struct inv_mpu_iio_s *st, u8 reg, u8 data)
{
	return 0;
}

static int mpu_hid_memory_write(struct inv_mpu_iio_s *st, u8 mpu_addr, u16 mem_addr,
			u32 len, u8 const *data)
{
	return 0;
}

static int mpu_hid_memory_read(struct inv_mpu_iio_s *st, u8 mpu_addr, u16 mem_addr,
			u32 len, u8 *data)
{
	return 0;
}

static struct mpu_platform_data mpu_data = {
	.int_config  = 0x00,
	.level_shifter = 0,
	.orientation = {
			1,  0,  0,
			0,  1,  0,
			0,  0,  1,
	},
};

/**
 *  inv_mpu_probe() - probe function.
 */
static int inv_mpu_probe(struct platform_device *pdev)
{
	struct inv_mpu_iio_s *st;
	struct iio_dev *indio_dev;
	int result = -ENOMEM;

#ifdef INV_KERNEL_3_10
	indio_dev = iio_device_alloc(sizeof(*st));
#else
	indio_dev = iio_allocate_device(sizeof(*st));
#endif
	if (!indio_dev) {
		pr_err("memory allocation failed\n");
		goto out_no_free;
	}
	st = iio_priv(indio_dev);
	st->plat_data = mpu_data;
	st->dev = &pdev->dev;
	indio_dev->dev.parent = &pdev->dev;
	indio_dev->name = "mpu6500";
	platform_set_drvdata(pdev, indio_dev);
	st->use_hid = 1;

	st->plat_read = inv_hid_read;
	st->plat_single_write = inv_hid_single_write;
	st->secondary_read = inv_hid_secondary_read;
	st->secondary_write = inv_hid_secondary_write;
	st->memory_read = mpu_hid_memory_read;
	st->memory_write = mpu_hid_memory_write;

	/* power is turned on inside check chip type*/
	result = inv_check_chip_type(st, indio_dev->name);
	if (result)
		goto out_free;

	result = st->init_config(indio_dev);
	if (result) {
		dev_err(&pdev->dev, "Could not initialize device.\n");
		goto out_free;
	}
	result = st->set_power_state(st, false);
	if (result) {
		dev_err(&pdev->dev, "%s could not be turned off.\n", st->hw->name);
		goto out_free;
	}

	inv_set_iio_info(st, indio_dev);

	result = inv_mpu_configure_ring(indio_dev);
	if (result) {
		dev_err(&pdev->dev, "configure ring buffer fail\n");
		goto out_free;
	}

	result = inv_mpu_probe_trigger(indio_dev);
	if (result) {
		dev_err(&pdev->dev, "trigger probe fail\n");
		goto out_unreg_ring;
	}

	result = iio_device_register(indio_dev);
	if (result) {
		dev_err(&pdev->dev, "IIO device register fail\n");
		goto out_remove_trigger;
	}

	if (INV_MPU6050 == st->chip_type ||
		INV_MPU6500 == st->chip_type) {
		result = inv_create_dmp_sysfs(indio_dev);
		if (result) {
			dev_err(&pdev->dev, "create dmp sysfs failed\n");
			goto out_unreg_iio;
		}
	}

	INIT_KFIFO(st->timestamps);
	spin_lock_init(&st->time_stamp_lock);
	dev_info(&pdev->dev, "%s is ready to go!\n",
					indio_dev->name);

	return 0;
out_unreg_iio:
	iio_device_unregister(indio_dev);
out_remove_trigger:
	if (indio_dev->modes & INDIO_BUFFER_TRIGGERED)
		inv_mpu_remove_trigger(indio_dev);
out_unreg_ring:
	inv_mpu_unconfigure_ring(indio_dev);
out_free:
#ifdef INV_KERNEL_3_10
	iio_device_free(indio_dev);
#else
	iio_free_device(indio_dev);
#endif
out_no_free:
	dev_err(&pdev->dev, "%s failed %d\n", __func__, result);

	return -EIO;
}

/**
 *  inv_mpu_remove() - remove function.
 */
static int inv_mpu_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct inv_mpu_iio_s *st = iio_priv(indio_dev);

	kfifo_free(&st->timestamps);
	iio_device_unregister(indio_dev);
	if (indio_dev->modes & INDIO_BUFFER_TRIGGERED)
		inv_mpu_remove_trigger(indio_dev);
	inv_mpu_unconfigure_ring(indio_dev);
#ifdef INV_KERNEL_3_10
	iio_device_free(indio_dev);
#else
	iio_free_device(indio_dev);
#endif

	dev_info(&pdev->dev, "inv-mpu-iio module removed.\n");

	return 0;
}

static void inv_mpu_shutdown(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct inv_mpu_iio_s *st = iio_priv(indio_dev);
	struct inv_reg_map_s *reg;
	int result;

	reg = &st->reg;
	dev_dbg(&pdev->dev, "Shutting down %s...\n", st->hw->name);

	/* reset to make sure previous state are not there */
	result = inv_plat_single_write(st, reg->pwr_mgmt_1, BIT_H_RESET);
	if (result)
		dev_err(&pdev->dev, "Failed to reset %s\n",
			st->hw->name);
	msleep(POWER_UP_TIME);
	/* turn off power to ensure gyro engine is off */
	result = st->set_power_state(st, false);
	if (result)
		dev_err(&pdev->dev, "Failed to turn off %s\n",
			st->hw->name);
}

#ifdef CONFIG_PM
static int __maybe_unused inv_mpu_resume(struct device *dev)
{
	struct inv_mpu_iio_s *st =
			iio_priv(platform_get_drvdata(to_platform_device(dev)));
	pr_debug("%s inv_mpu_resume\n", st->hw->name);
	return st->set_power_state(st, true);
}

static int __maybe_unused inv_mpu_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(to_platform_device(dev));
	struct inv_mpu_iio_s *st = iio_priv(indio_dev);
	int result;

	pr_debug("%s inv_mpu_suspend\n", st->hw->name);
	mutex_lock(&indio_dev->mlock);
	result = 0;
	if ((!st->chip_config.dmp_on) ||
		(!st->chip_config.enable) ||
		(!st->chip_config.dmp_event_int_on))
		result = st->set_power_state(st, false);
	mutex_unlock(&indio_dev->mlock);

	return result;
}

static const struct dev_pm_ops inv_mpu_pmops = {
	SET_SYSTEM_SLEEP_PM_OPS(inv_mpu_suspend, inv_mpu_resume)
};

#define INV_MPU_PMOPS (&inv_mpu_pmops)
#else
#define INV_MPU_PMOPS NULL
#endif /* CONFIG_PM */

/* device id table is used to identify what device can be
 * supported by this driver
 */
static const struct platform_device_id inv_mpu_id[] = {
	{"itg3500", INV_ITG3500},
	{"mpu3050", INV_MPU3050},
	{"mpu6050", INV_MPU6050},
	{"mpu9150", INV_MPU9150},
	{"mpu6500", INV_MPU6500},
	{"mpu9250", INV_MPU9250},
	{"mpu6xxx", INV_MPU6XXX},
	{"mpu6515", INV_MPU6515},
	{}
};

MODULE_DEVICE_TABLE(platform, inv_mpu_id);

static const struct of_device_id inv_mpu_of_match[] = {
	{ .compatible = "inv-hid,itg3500", },
	{ .compatible = "inv-hid,mpu3050", },
	{ .compatible = "inv-hid,mpu6050", },
	{ .compatible = "inv-hid,mpu9150", },
	{ .compatible = "inv-hid,mpu6500", },
	{ .compatible = "inv-hid,mpu9250", },
	{ .compatible = "inv-hid,mpu6xxx", },
	{ .compatible = "inv-hid,mpu9350", },
	{ .compatible = "inv-hid,mpu6515", },
	{}
};

MODULE_DEVICE_TABLE(of, inv_mpu_of_match);

static struct platform_driver inv_mpu_platform_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name	= "inv_mpu_iio_hid",
		.pm	= INV_MPU_PMOPS,
		.of_match_table = of_match_ptr(inv_mpu_of_match),
	},
	.probe		= inv_mpu_probe,
	.remove		= inv_mpu_remove,
	.id_table = inv_mpu_id,
	.shutdown = inv_mpu_shutdown,
};

module_platform_driver(inv_mpu_platform_driver);

MODULE_AUTHOR("lyx <lyx@rock-chips.com>");
MODULE_DESCRIPTION("Invensense device driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("inv-mpu-iio-hid");
