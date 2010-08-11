/*
 * Analog Devices ADIS16250/ADIS16255 Low Power Gyroscope
 *
 * Written by: Matthias Brugger <m_brugger@web.de>
 *
 * Copyright (C) 2010 Fraunhofer Institute for Integrated Circuits
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
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

/*
 * The driver just has a bare interface to the sysfs (sample rate in Hz,
 * orientation (x, y, z) and gyroscope data in °/sec.
 *
 * It should be added to iio subsystem when this has left staging.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#include <linux/interrupt.h>
#include <linux/sysfs.h>
#include <linux/stat.h>
#include <linux/delay.h>

#include <linux/gpio.h>

#include <linux/spi/spi.h>
#include <linux/workqueue.h>

#include "adis16255.h"

#define ADIS_STATUS        0x3d
#define ADIS_SMPL_PRD_MSB  0x37
#define ADIS_SMPL_PRD_LSB  0x36
#define ADIS_MSC_CTRL_MSB  0x35
#define ADIS_MSC_CTRL_LSB  0x34
#define ADIS_GPIO_CTRL     0x33
#define ADIS_ALM_SMPL1     0x25
#define ADIS_ALM_MAG1      0x21
#define ADIS_GYRO_SCALE    0x17
#define ADIS_GYRO_OUT      0x05
#define ADIS_SUPPLY_OUT    0x03
#define ADIS_ENDURANCE     0x01

/*
 * data structure for every sensor
 *
 * @dev:       Driver model representation of the device.
 * @spi:       Pointer to the spi device which will manage i/o to spi bus.
 * @data:      Last read data from device.
 * @irq_adis:  GPIO Number of IRQ signal
 * @irq:       irq line manage by kernel
 * @negative:  indicates if sensor is upside down (negative == 1)
 * @direction: indicates axis (x, y, z) the sensor is meassuring
 */
struct spi_adis16255_data {
	struct device dev;
	struct spi_device *spi;
	s16 data;
	int irq;
	u8 negative;
	char direction;
};

/*-------------------------------------------------------------------------*/

static int spi_adis16255_read_data(struct spi_adis16255_data *spiadis,
					u8 adr,
					u8 *rbuf)
{
	struct spi_device *spi = spiadis->spi;
	struct spi_message msg;
	struct spi_transfer xfer1, xfer2;
	u8 *buf, *rx;
	int ret;

	buf = kzalloc(4, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	rx = kzalloc(4, GFP_KERNEL);
	if (rx == NULL) {
		ret = -ENOMEM;
		goto err_buf;
	}

	buf[0] = adr;

	spi_message_init(&msg);
	memset(&xfer1, 0, sizeof(xfer1));
	memset(&xfer2, 0, sizeof(xfer2));

	xfer1.tx_buf = buf;
	xfer1.rx_buf = buf + 2;
	xfer1.len = 2;
	xfer1.delay_usecs = 9;

	xfer2.tx_buf = rx + 2;
	xfer2.rx_buf = rx;
	xfer2.len = 2;

	spi_message_add_tail(&xfer1, &msg);
	spi_message_add_tail(&xfer2, &msg);

	ret = spi_sync(spi, &msg);
	if (ret == 0) {
		rbuf[0] = rx[0];
		rbuf[1] = rx[1];
	}

	kfree(rx);
err_buf:
	kfree(buf);

	return ret;
}

static int spi_adis16255_write_data(struct spi_adis16255_data *spiadis,
					u8 adr1,
					u8 adr2,
					u8 *wbuf)
{
	struct spi_device *spi = spiadis->spi;
	struct spi_message   msg;
	struct spi_transfer  xfer1, xfer2;
	u8       *buf, *rx;
	int         ret;

	buf = kmalloc(4, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	rx = kzalloc(4, GFP_KERNEL);
	if (rx == NULL) {
		ret = -ENOMEM;
		goto err_buf;
	}

	spi_message_init(&msg);
	memset(&xfer1, 0, sizeof(xfer1));
	memset(&xfer2, 0, sizeof(xfer2));

	buf[0] = adr1 | 0x80;
	buf[1] = *wbuf;

	buf[2] = adr2 | 0x80;
	buf[3] = *(wbuf + 1);

	xfer1.tx_buf = buf;
	xfer1.rx_buf = rx;
	xfer1.len = 2;
	xfer1.delay_usecs = 9;

	xfer2.tx_buf = buf+2;
	xfer2.rx_buf = rx+2;
	xfer2.len = 2;

	spi_message_add_tail(&xfer1, &msg);
	spi_message_add_tail(&xfer2, &msg);

	ret = spi_sync(spi, &msg);
	if (ret != 0)
		dev_warn(&spi->dev, "write data to %#x %#x failed\n",
				buf[0], buf[2]);

	kfree(rx);
err_buf:
	kfree(buf);
	return ret;
}

/*-------------------------------------------------------------------------*/

static irqreturn_t adis_irq_thread(int irq, void *dev_id)
{
	struct spi_adis16255_data *spiadis = dev_id;
	int status;
	u16 value = 0;

	status =  spi_adis16255_read_data(spiadis, ADIS_GYRO_OUT, (u8 *)&value);
	if (status != 0) {
		dev_warn(&spiadis->spi->dev, "SPI FAILED\n");
		goto exit;
	}

	/* perform on new data only... */
	if (value & 0x8000) {
		/* delete error and new data bit */
		value = value & 0x3fff;
		/* set negative value */
		if (value & 0x2000)
			value = value | 0xe000;

		if (likely(spiadis->negative))
			value = -value;

		spiadis->data = (s16) value;
	}

exit:
	return IRQ_HANDLED;
}

/*-------------------------------------------------------------------------*/

ssize_t adis16255_show_data(struct device *device,
		struct device_attribute *da,
		char *buf)
{
	struct spi_adis16255_data *spiadis = dev_get_drvdata(device);
	return snprintf(buf, PAGE_SIZE, "%d\n", spiadis->data);
}
DEVICE_ATTR(data, S_IRUGO , adis16255_show_data, NULL);

ssize_t adis16255_show_direction(struct device *device,
		struct device_attribute *da,
		char *buf)
{
	struct spi_adis16255_data *spiadis = dev_get_drvdata(device);
	return snprintf(buf, PAGE_SIZE, "%c\n", spiadis->direction);
}
DEVICE_ATTR(direction, S_IRUGO , adis16255_show_direction, NULL);

ssize_t adis16255_show_sample_rate(struct device *device,
		struct device_attribute *da,
		char *buf)
{
	struct spi_adis16255_data *spiadis = dev_get_drvdata(device);
	int status = 0;
	u16 value = 0;
	int ts = 0;

	status = spi_adis16255_read_data(spiadis, ADIS_SMPL_PRD_MSB,
				(u8 *)&value);
	if (status != 0)
		return -EINVAL;

	if (value & 0x80) {
		/* timebase = 60.54 ms */
		ts = 60540 * ((0x7f & value) + 1);
	} else {
		/* timebase = 1.953 ms */
		ts = 1953 * ((0x7f & value) + 1);
	}

	return snprintf(buf, PAGE_SIZE, "%d\n", (1000*1000)/ts);
}
DEVICE_ATTR(sample_rate, S_IRUGO , adis16255_show_sample_rate, NULL);

static struct attribute *adis16255_attributes[] = {
	&dev_attr_data.attr,
	&dev_attr_direction.attr,
	&dev_attr_sample_rate.attr,
	NULL
};

static const struct attribute_group adis16255_attr_group = {
	.attrs = adis16255_attributes,
};

/*-------------------------------------------------------------------------*/

static int spi_adis16255_shutdown(struct spi_adis16255_data *spiadis)
{
	u16 value = 0;
	/* turn sensor off */
	spi_adis16255_write_data(spiadis,
			ADIS_SMPL_PRD_MSB, ADIS_SMPL_PRD_LSB,
			(u8 *)&value);
	spi_adis16255_write_data(spiadis,
			ADIS_MSC_CTRL_MSB, ADIS_MSC_CTRL_LSB,
			(u8 *)&value);
	return 0;
}

static int spi_adis16255_bringup(struct spi_adis16255_data *spiadis)
{
	int status = 0;
	u16 value = 0;

	status = spi_adis16255_read_data(spiadis, ADIS_GYRO_SCALE,
				(u8 *)&value);
	if (status != 0)
		goto err;
	if (value != 0x0800) {
		dev_warn(&spiadis->spi->dev, "Scale factor is none default "
				"value (%.4x)\n", value);
	}

	/* timebase = 1.953 ms, Ns = 0 -> 512 Hz sample rate */
	value =  0x0001;
	status = spi_adis16255_write_data(spiadis,
				ADIS_SMPL_PRD_MSB, ADIS_SMPL_PRD_LSB,
				(u8 *)&value);
	if (status != 0)
		goto err;

	/* start internal self-test */
	value = 0x0400;
	status = spi_adis16255_write_data(spiadis,
				ADIS_MSC_CTRL_MSB, ADIS_MSC_CTRL_LSB,
				(u8 *)&value);
	if (status != 0)
		goto err;

	/* wait 35 ms to finish self-test */
	msleep(35);

	value = 0x0000;
	status = spi_adis16255_read_data(spiadis, ADIS_STATUS,
				(u8 *)&value);
	if (status != 0)
		goto err;

	if (value & 0x23) {
		if (value & 0x20) {
			dev_warn(&spiadis->spi->dev, "self-test error\n");
			status = -ENODEV;
			goto err;
		} else if (value & 0x3)	{
			dev_warn(&spiadis->spi->dev, "Sensor voltage "
						"out of range.\n");
			status = -ENODEV;
			goto err;
		}
	}

	/* set interrupt to active high on DIO0 when data ready */
	value = 0x0006;
	status = spi_adis16255_write_data(spiadis,
				ADIS_MSC_CTRL_MSB, ADIS_MSC_CTRL_LSB,
				(u8 *)&value);
	if (status != 0)
		goto err;
	return status;

err:
	spi_adis16255_shutdown(spiadis);
	return status;
}

/*-------------------------------------------------------------------------*/

static int __devinit spi_adis16255_probe(struct spi_device *spi)
{

	struct adis16255_init_data *init_data = spi->dev.platform_data;
	struct spi_adis16255_data  *spiadis;
	int status = 0;

	spiadis = kzalloc(sizeof(*spiadis), GFP_KERNEL);
	if (!spiadis)
		return -ENOMEM;

	spiadis->spi = spi;
	spiadis->direction = init_data->direction;

	if (init_data->negative)
		spiadis->negative = 1;

	status = gpio_request(init_data->irq, "adis16255");
	if (status != 0)
		goto err;

	status = gpio_direction_input(init_data->irq);
	if (status != 0)
		goto gpio_err;

	spiadis->irq = gpio_to_irq(init_data->irq);

	status = request_threaded_irq(spiadis->irq,
			NULL, adis_irq_thread,
			IRQF_DISABLED, "adis-driver", spiadis);

	if (status != 0) {
		dev_err(&spi->dev, "IRQ request failed\n");
		goto gpio_err;
	}

	dev_dbg(&spi->dev, "GPIO %d IRQ %d\n", init_data->irq, spiadis->irq);

	dev_set_drvdata(&spi->dev, spiadis);
	status = sysfs_create_group(&spi->dev.kobj, &adis16255_attr_group);
	if (status != 0)
		goto irq_err;

	status = spi_adis16255_bringup(spiadis);
	if (status != 0)
		goto sysfs_err;

	dev_info(&spi->dev, "spi_adis16255 driver added!\n");

	return status;

sysfs_err:
	sysfs_remove_group(&spiadis->spi->dev.kobj, &adis16255_attr_group);
irq_err:
	free_irq(spiadis->irq, spiadis);
gpio_err:
	gpio_free(init_data->irq);
err:
	kfree(spiadis);
	return status;
}

static int __devexit spi_adis16255_remove(struct spi_device *spi)
{
	struct spi_adis16255_data  *spiadis    = dev_get_drvdata(&spi->dev);

	spi_adis16255_shutdown(spiadis);

	free_irq(spiadis->irq, spiadis);
	gpio_free(irq_to_gpio(spiadis->irq));

	sysfs_remove_group(&spiadis->spi->dev.kobj, &adis16255_attr_group);

	kfree(spiadis);

	dev_info(&spi->dev, "spi_adis16255 driver removed!\n");
	return 0;
}

static struct spi_driver spi_adis16255_drv = {
	.driver = {
		.name =  "spi_adis16255",
		.owner = THIS_MODULE,
	},
	.probe = spi_adis16255_probe,
	.remove =   __devexit_p(spi_adis16255_remove),
};

/*-------------------------------------------------------------------------*/

static int __init spi_adis16255_init(void)
{
	return spi_register_driver(&spi_adis16255_drv);
}
module_init(spi_adis16255_init);

static void __exit spi_adis16255_exit(void)
{
	spi_unregister_driver(&spi_adis16255_drv);
}
module_exit(spi_adis16255_exit);

MODULE_AUTHOR("Matthias Brugger");
MODULE_DESCRIPTION("SPI device driver for ADIS16255 sensor");
MODULE_LICENSE("GPL");
