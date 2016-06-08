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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/jiffies.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include <linux/poll.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>

#include "inv_mpu_iio.h"
#ifdef INV_KERNEL_3_10
#include <linux/iio/sysfs.h>
#else
#include "sysfs.h"
#endif
#include "inv_counters.h"

#define INV_SPI_READ 0x80
static int inv_spi_read(struct inv_mpu_iio_s *st, u8 reg, int len, u8 *data)
{
	struct spi_message msg;
	int res;
	u8 d[2];
	struct spi_transfer xfers[] = {
		{
			.tx_buf = d,
			.bits_per_word = 8,
			.len = 1,
		},
		{
			.rx_buf = data,
			.bits_per_word = 8,
			.len = len,
		}
	};

	if (!data)
		return -EINVAL;

	d[0] = (reg | INV_SPI_READ);

	spi_message_init(&msg);
	spi_message_add_tail(&xfers[0], &msg);
	spi_message_add_tail(&xfers[1], &msg);
	res = spi_sync(to_spi_device(st->dev), &msg);

	return res;
}

static int inv_spi_single_write(struct inv_mpu_iio_s *st, u8 reg, u8 data)
{
	struct spi_message msg;
	int res;
	u8 d[2];
	struct spi_transfer xfers = {
		.tx_buf = d,
		.bits_per_word = 8,
		.len = 2,
	};

	d[0] = reg;
	d[1] = data;
	spi_message_init(&msg);
	spi_message_add_tail(&xfers, &msg);
	res = spi_sync(to_spi_device(st->dev), &msg);

	return res;
}

static int inv_spi_secondary_read(struct inv_mpu_iio_s *st, u8 reg, int len, u8 *data)
{
	return 0;
}

static int inv_spi_secondary_write(struct inv_mpu_iio_s *st, u8 reg, u8 data)
{
	return 0;
}

static int mpu_spi_memory_write(struct inv_mpu_iio_s *st, u8 mpu_addr, u16 mem_addr,
		     u32 len, u8 const *data)
{
	struct spi_message msg;
	u8 buf[513];
	int res;

	struct spi_transfer xfers = {
		.tx_buf = buf,
		.bits_per_word = 8,
		.len = len + 1,
	};

	if (!data || !st)
		return -EINVAL;

	if (len > (sizeof(buf) - 1))
		return -ENOMEM;

	inv_plat_single_write(st, REG_BANK_SEL, mem_addr >> 8);
	inv_plat_single_write(st, REG_MEM_START_ADDR, mem_addr & 0xFF);

	buf[0] = REG_MEM_RW;
	memcpy(buf + 1, data, len);
	spi_message_init(&msg);
	spi_message_add_tail(&xfers, &msg);
	res = spi_sync(to_spi_device(st->dev), &msg);

	return res;
}

static int mpu_spi_memory_read(struct inv_mpu_iio_s *st, u8 mpu_addr, u16 mem_addr,
		    u32 len, u8 *data)
{
	int res;

	if (!data || !st)
		return -EINVAL;

	res = inv_plat_single_write(st, REG_BANK_SEL, mem_addr >> 8);
	res = inv_plat_single_write(st, REG_MEM_START_ADDR, mem_addr & 0xFF);
	res = inv_plat_read(st, REG_MEM_RW, len, data);

	return res;
}

#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/of.h>

static struct mpu_platform_data mpu_data = {
	.int_config  = 0x00,
	.level_shifter = 0,
	.orientation = {
			0,  1,  0,
			-1,  0,  0,
			0,  0, -1,
	},
/*
	.key = {
		221,  22, 205,   7, 217, 186, 151, 55,
		206, 254,  35, 144, 225, 102,  47, 50,
	},
*/
};

static int of_inv_parse_platform_data(struct spi_device *spi,
				      struct mpu_platform_data *pdata)
{
	struct device_node *np = spi->dev.of_node;
	unsigned long irq_flags;
	int irq_pin;
	int gpio_pin;

	gpio_pin = of_get_named_gpio_flags(np, "irq-gpio", 0, (enum of_gpio_flags *)&irq_flags);
	gpio_request(gpio_pin, "mpu6500");
	irq_pin = gpio_to_irq(gpio_pin);
	spi->irq = irq_pin;

	pr_info("%s: irq %x\n", __func__, spi->irq);

	return 0;
}

/**
 *  inv_mpu_probe() - probe function.
 */
static int inv_mpu_probe(struct spi_device *spi)
{
	struct inv_mpu_iio_s *st;
	struct iio_dev *indio_dev;
	int result;

	if (!spi)
		return -ENOMEM;

	spi->bits_per_word = 8;
	result = spi_setup(spi);
	if (result < 0) {
		dev_err(&spi->dev, "ERR: fail to setup spi\n");
		goto out_no_free;
	}

#ifdef INV_KERNEL_3_10
    indio_dev = iio_device_alloc(sizeof(*st));
#else
	indio_dev = iio_allocate_device(sizeof(*st));
#endif
	if (indio_dev == NULL) {
		pr_err("memory allocation failed\n");
		result =  -ENOMEM;
		goto out_no_free;
	}
	st = iio_priv(indio_dev);
	if (spi->dev.of_node) {
		result = of_inv_parse_platform_data(spi, &st->plat_data);
		if (result)
			goto out_free;
		st->plat_data = mpu_data;
		pr_info("secondary_i2c_addr=%x\n", st->plat_data.secondary_i2c_addr);
	} else
		st->plat_data =
			*(struct mpu_platform_data *)dev_get_platdata(&spi->dev);

	/* Make state variables available to all _show and _store functions. */
	spi_set_drvdata(spi, indio_dev);
	indio_dev->dev.parent = &spi->dev;
	st->dev = &spi->dev;
	st->irq = spi->irq;
	st->i2c_dis = BIT_I2C_IF_DIS;
	if (!strcmp(spi->modalias, "mpu6xxx"))
		indio_dev->name = st->name;
	else
		indio_dev->name = spi->modalias;

	st->plat_read = inv_spi_read;
	st->plat_single_write = inv_spi_single_write;
	st->secondary_read = inv_spi_secondary_read;
	st->secondary_write = inv_spi_secondary_write;
	st->memory_read = mpu_spi_memory_read;
	st->memory_write = mpu_spi_memory_write;

	/* power is turned on inside check chip type*/
	result = inv_check_chip_type(st, indio_dev->name);
	if (result)
		goto out_free;

	result = st->init_config(indio_dev);
	if (result) {
		dev_err(&spi->dev,
			"Could not initialize device.\n");
		goto out_free;
	}
	result = st->set_power_state(st, false);
	if (result) {
		dev_err(&spi->dev,
			"%s could not be turned off.\n", st->hw->name);
		goto out_free;
	}

	inv_set_iio_info(st, indio_dev);

	result = inv_mpu_configure_ring(indio_dev);
	if (result) {
		pr_err("configure ring buffer fail\n");
		goto out_free;
	}

	result = inv_mpu_probe_trigger(indio_dev);
	if (result) {
		pr_err("trigger probe fail\n");
		goto out_unreg_ring;
	}

	result = iio_device_register(indio_dev);
	if (result) {
		pr_err("IIO device register fail\n");
		goto out_remove_trigger;
	}

	if (INV_MPU6050 == st->chip_type ||
	    INV_MPU6500 == st->chip_type) {
		result = inv_create_dmp_sysfs(indio_dev);
		if (result) {
			pr_err("create dmp sysfs failed\n");
			goto out_unreg_iio;
		}
	}

	INIT_KFIFO(st->timestamps);
	spin_lock_init(&st->time_stamp_lock);
	dev_info(&spi->dev, "%s is ready to go!\n",
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
	dev_err(&spi->dev, "%s failed %d\n", __func__, result);

	return -EIO;
}

static void inv_mpu_shutdown(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct inv_mpu_iio_s *st = iio_priv(indio_dev);
	struct inv_reg_map_s *reg;
	int result;

	reg = &st->reg;
	dev_dbg(&spi->dev, "Shutting down %s...\n", st->hw->name);

	/* reset to make sure previous state are not there */
	result = inv_plat_single_write(st, reg->pwr_mgmt_1, BIT_H_RESET);
	if (result)
		dev_err(&spi->dev, "Failed to reset %s\n",
			st->hw->name);
	msleep(POWER_UP_TIME);
	/* turn off power to ensure gyro engine is off */
	result = st->set_power_state(st, false);
	if (result)
		dev_err(&spi->dev, "Failed to turn off %s\n",
			st->hw->name);
}

/**
 *  inv_mpu_remove() - remove function.
 */
static int inv_mpu_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
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

	dev_info(&spi->dev, "inv-mpu-iio module removed.\n");

	return 0;
}

#ifdef CONFIG_PM
static int inv_mpu_resume(struct device *dev)
{
	struct inv_mpu_iio_s *st =
			iio_priv(spi_get_drvdata(to_spi_device(dev)));
	pr_debug("%s inv_mpu_resume\n", st->hw->name);
	return st->set_power_state(st, true);
}

static int inv_mpu_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = spi_get_drvdata(to_spi_device(dev));
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
static const struct spi_device_id inv_mpu_id[] = {
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

MODULE_DEVICE_TABLE(i2c, inv_mpu_id);

static const struct of_device_id inv_mpu_of_match[] = {
	{ .compatible = "inv-spi,itg3500", },
	{ .compatible = "inv-spi,mpu3050", },
	{ .compatible = "inv-spi,mpu6050", },
	{ .compatible = "inv-spi,mpu9150", },
	{ .compatible = "inv-spi,mpu6500", },
	{ .compatible = "inv-spi,mpu9250", },
	{ .compatible = "inv-spi,mpu6xxx", },
	{ .compatible = "inv-spi,mpu9350", },
	{ .compatible = "inv-spi,mpu6515", },
	{}
};

MODULE_DEVICE_TABLE(of, inv_mpu_of_match);

static struct spi_driver inv_mpu_driver_spi = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "inv_mpu_iio_spi",
		.pm   =       INV_MPU_PMOPS,
		.of_match_table = of_match_ptr(inv_mpu_of_match),
	},
	.probe = inv_mpu_probe,
	.remove = inv_mpu_remove,
	.id_table = inv_mpu_id,
	.shutdown = inv_mpu_shutdown,
};

static int __init inv_mpu_init(void)
{
	int result = spi_register_driver(&inv_mpu_driver_spi);

	if (result) {
		pr_err("failed\n");
		return result;
	}
	return 0;
}

static void __exit inv_mpu_exit(void)
{
	spi_unregister_driver(&inv_mpu_driver_spi);
}

module_init(inv_mpu_init);
module_exit(inv_mpu_exit);

MODULE_AUTHOR("Invensense Corporation");
MODULE_DESCRIPTION("Invensense device driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("inv-mpu-iio-spi");

/**
 *  @}
 */
