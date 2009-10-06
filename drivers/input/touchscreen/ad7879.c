/*
 * Copyright (C) 2008-2009 Michael Hennerich, Analog Devices Inc.
 *
 * Description:	AD7879/AD7889 based touchscreen, and GPIO driver
 *		(I2C/SPI Interface)
 *
 * Bugs:        Enter bugs at http://blackfin.uclinux.org/
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
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * History:
 * Copyright (c) 2005 David Brownell
 * Copyright (c) 2006 Nokia Corporation
 * Various changes: Imre Deak <imre.deak@nokia.com>
 *
 * Using code from:
 *  - corgi_ts.c
 *	Copyright (C) 2004-2005 Richard Purdie
 *  - omap_ts.[hc], ads7846.h, ts_osk.c
 *	Copyright (C) 2002 MontaVista Software
 *	Copyright (C) 2004 Texas Instruments
 *	Copyright (C) 2005 Dirk Behme
 *  - ad7877.c
 *	Copyright (C) 2006-2008 Analog Devices Inc.
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/spi/spi.h>
#include <linux/i2c.h>

#include <linux/spi/ad7879.h>

#define AD7879_REG_ZEROS		0
#define AD7879_REG_CTRL1		1
#define AD7879_REG_CTRL2		2
#define AD7879_REG_CTRL3		3
#define AD7879_REG_AUX1HIGH		4
#define AD7879_REG_AUX1LOW		5
#define AD7879_REG_TEMP1HIGH		6
#define AD7879_REG_TEMP1LOW		7
#define AD7879_REG_XPLUS		8
#define AD7879_REG_YPLUS		9
#define AD7879_REG_Z1			10
#define AD7879_REG_Z2			11
#define AD7879_REG_AUXVBAT		12
#define AD7879_REG_TEMP			13
#define AD7879_REG_REVID		14

/* Control REG 1 */
#define AD7879_TMR(x)			((x & 0xFF) << 0)
#define AD7879_ACQ(x)			((x & 0x3) << 8)
#define AD7879_MODE_NOC			(0 << 10)	/* Do not convert */
#define AD7879_MODE_SCC			(1 << 10)	/* Single channel conversion */
#define AD7879_MODE_SEQ0		(2 << 10)	/* Sequence 0 in Slave Mode */
#define AD7879_MODE_SEQ1		(3 << 10)	/* Sequence 1 in Master Mode */
#define AD7879_MODE_INT			(1 << 15)	/* PENIRQ disabled INT enabled */

/* Control REG 2 */
#define AD7879_FCD(x)			((x & 0x3) << 0)
#define AD7879_RESET			(1 << 4)
#define AD7879_MFS(x)			((x & 0x3) << 5)
#define AD7879_AVG(x)			((x & 0x3) << 7)
#define	AD7879_SER			(1 << 9)	/* non-differential */
#define	AD7879_DFR			(0 << 9)	/* differential */
#define AD7879_GPIOPOL			(1 << 10)
#define AD7879_GPIODIR			(1 << 11)
#define AD7879_GPIO_DATA		(1 << 12)
#define AD7879_GPIO_EN			(1 << 13)
#define AD7879_PM(x)			((x & 0x3) << 14)
#define AD7879_PM_SHUTDOWN		(0)
#define AD7879_PM_DYN			(1)
#define AD7879_PM_FULLON		(2)

/* Control REG 3 */
#define AD7879_TEMPMASK_BIT		(1<<15)
#define AD7879_AUXVBATMASK_BIT		(1<<14)
#define AD7879_INTMODE_BIT		(1<<13)
#define AD7879_GPIOALERTMASK_BIT	(1<<12)
#define AD7879_AUXLOW_BIT		(1<<11)
#define AD7879_AUXHIGH_BIT		(1<<10)
#define AD7879_TEMPLOW_BIT		(1<<9)
#define AD7879_TEMPHIGH_BIT		(1<<8)
#define AD7879_YPLUS_BIT		(1<<7)
#define AD7879_XPLUS_BIT		(1<<6)
#define AD7879_Z1_BIT			(1<<5)
#define AD7879_Z2_BIT			(1<<4)
#define AD7879_AUX_BIT			(1<<3)
#define AD7879_VBAT_BIT			(1<<2)
#define AD7879_TEMP_BIT			(1<<1)

enum {
	AD7879_SEQ_XPOS  = 0,
	AD7879_SEQ_YPOS  = 1,
	AD7879_SEQ_Z1    = 2,
	AD7879_SEQ_Z2    = 3,
	AD7879_NR_SENSE  = 4,
};

#define	MAX_12BIT			((1<<12)-1)
#define	TS_PEN_UP_TIMEOUT		msecs_to_jiffies(50)

#if defined(CONFIG_TOUCHSCREEN_AD7879_SPI) || defined(CONFIG_TOUCHSCREEN_AD7879_SPI_MODULE)
#define AD7879_DEVID		0x7A
typedef struct spi_device	bus_device;
#elif defined(CONFIG_TOUCHSCREEN_AD7879_I2C) || defined(CONFIG_TOUCHSCREEN_AD7879_I2C_MODULE)
#define AD7879_DEVID		0x79
typedef struct i2c_client	bus_device;
#endif

struct ad7879 {
	bus_device		*bus;
	struct input_dev	*input;
	struct work_struct	work;
	struct timer_list	timer;

	struct mutex		mutex;
	unsigned		disabled:1;	/* P: mutex */

#if defined(CONFIG_TOUCHSCREEN_AD7879_SPI) || defined(CONFIG_TOUCHSCREEN_AD7879_SPI_MODULE)
	struct spi_message	msg;
	struct spi_transfer	xfer[AD7879_NR_SENSE + 1];
	u16			cmd;
#endif
	u16			conversion_data[AD7879_NR_SENSE];
	char			phys[32];
	u8			first_conversion_delay;
	u8			acquisition_time;
	u8			averaging;
	u8			pen_down_acc_interval;
	u8			median;
	u16			x_plate_ohms;
	u16			pressure_max;
	u16			gpio_init;
	u16			cmd_crtl1;
	u16			cmd_crtl2;
	u16			cmd_crtl3;
	unsigned		gpio:1;
};

static int ad7879_read(bus_device *, u8);
static int ad7879_write(bus_device *, u8, u16);
static void ad7879_collect(struct ad7879 *);

static void ad7879_report(struct ad7879 *ts)
{
	struct input_dev *input_dev = ts->input;
	unsigned Rt;
	u16 x, y, z1, z2;

	x = ts->conversion_data[AD7879_SEQ_XPOS] & MAX_12BIT;
	y = ts->conversion_data[AD7879_SEQ_YPOS] & MAX_12BIT;
	z1 = ts->conversion_data[AD7879_SEQ_Z1] & MAX_12BIT;
	z2 = ts->conversion_data[AD7879_SEQ_Z2] & MAX_12BIT;

	/*
	 * The samples processed here are already preprocessed by the AD7879.
	 * The preprocessing function consists of a median and an averaging filter.
	 * The combination of these two techniques provides a robust solution,
	 * discarding the spurious noise in the signal and keeping only the data of interest.
	 * The size of both filters is programmable. (dev.platform_data, see linux/spi/ad7879.h)
	 * Other user-programmable conversion controls include variable acquisition time,
	 * and first conversion delay. Up to 16 averages can be taken per conversion.
	 */

	if (likely(x && z1)) {
		/* compute touch pressure resistance using equation #1 */
		Rt = (z2 - z1) * x * ts->x_plate_ohms;
		Rt /= z1;
		Rt = (Rt + 2047) >> 12;

		input_report_abs(input_dev, ABS_X, x);
		input_report_abs(input_dev, ABS_Y, y);
		input_report_abs(input_dev, ABS_PRESSURE, Rt);
		input_sync(input_dev);
	}
}

static void ad7879_work(struct work_struct *work)
{
	struct ad7879 *ts = container_of(work, struct ad7879, work);

	/* use keventd context to read the result registers */
	ad7879_collect(ts);
	ad7879_report(ts);
	mod_timer(&ts->timer, jiffies + TS_PEN_UP_TIMEOUT);
}

static void ad7879_ts_event_release(struct ad7879 *ts)
{
	struct input_dev *input_dev = ts->input;

	input_report_abs(input_dev, ABS_PRESSURE, 0);
	input_sync(input_dev);
}

static void ad7879_timer(unsigned long handle)
{
	struct ad7879 *ts = (void *)handle;

	ad7879_ts_event_release(ts);
}

static irqreturn_t ad7879_irq(int irq, void *handle)
{
	struct ad7879 *ts = handle;

	/* The repeated conversion sequencer controlled by TMR kicked off too fast.
	 * We ignore the last and process the sample sequence currently in the queue.
	 * It can't be older than 9.4ms
	 */

	if (!work_pending(&ts->work))
		schedule_work(&ts->work);

	return IRQ_HANDLED;
}

static void ad7879_setup(struct ad7879 *ts)
{
	ts->cmd_crtl3 = AD7879_YPLUS_BIT |
			AD7879_XPLUS_BIT |
			AD7879_Z2_BIT |
			AD7879_Z1_BIT |
			AD7879_TEMPMASK_BIT |
			AD7879_AUXVBATMASK_BIT |
			AD7879_GPIOALERTMASK_BIT;

	ts->cmd_crtl2 = AD7879_PM(AD7879_PM_DYN) | AD7879_DFR |
			AD7879_AVG(ts->averaging) |
			AD7879_MFS(ts->median) |
			AD7879_FCD(ts->first_conversion_delay) |
			ts->gpio_init;

	ts->cmd_crtl1 = AD7879_MODE_INT | AD7879_MODE_SEQ1 |
			AD7879_ACQ(ts->acquisition_time) |
			AD7879_TMR(ts->pen_down_acc_interval);

	ad7879_write(ts->bus, AD7879_REG_CTRL2, ts->cmd_crtl2);
	ad7879_write(ts->bus, AD7879_REG_CTRL3, ts->cmd_crtl3);
	ad7879_write(ts->bus, AD7879_REG_CTRL1, ts->cmd_crtl1);
}

static void ad7879_disable(struct ad7879 *ts)
{
	mutex_lock(&ts->mutex);

	if (!ts->disabled) {

		ts->disabled = 1;
		disable_irq(ts->bus->irq);

		cancel_work_sync(&ts->work);

		if (del_timer_sync(&ts->timer))
			ad7879_ts_event_release(ts);

		ad7879_write(ts->bus, AD7879_REG_CTRL2,
			     AD7879_PM(AD7879_PM_SHUTDOWN));
	}

	mutex_unlock(&ts->mutex);
}

static void ad7879_enable(struct ad7879 *ts)
{
	mutex_lock(&ts->mutex);

	if (ts->disabled) {
		ad7879_setup(ts);
		ts->disabled = 0;
		enable_irq(ts->bus->irq);
	}

	mutex_unlock(&ts->mutex);
}

static ssize_t ad7879_disable_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct ad7879 *ts = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", ts->disabled);
}

static ssize_t ad7879_disable_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct ad7879 *ts = dev_get_drvdata(dev);
	unsigned long val;
	int error;

	error = strict_strtoul(buf, 10, &val);
	if (error)
		return error;

	if (val)
		ad7879_disable(ts);
	else
		ad7879_enable(ts);

	return count;
}

static DEVICE_ATTR(disable, 0664, ad7879_disable_show, ad7879_disable_store);

static ssize_t ad7879_gpio_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct ad7879 *ts = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", ts->gpio);
}

static ssize_t ad7879_gpio_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct ad7879 *ts = dev_get_drvdata(dev);
	unsigned long val;
	int error;

	error = strict_strtoul(buf, 10, &val);
	if (error)
		return error;

	mutex_lock(&ts->mutex);
	ts->gpio = !!val;
	error = ad7879_write(ts->bus, AD7879_REG_CTRL2,
			   ts->gpio ?
				ts->cmd_crtl2 & ~AD7879_GPIO_DATA :
				ts->cmd_crtl2 | AD7879_GPIO_DATA);
	mutex_unlock(&ts->mutex);

	return error ? : count;
}

static DEVICE_ATTR(gpio, 0664, ad7879_gpio_show, ad7879_gpio_store);

static struct attribute *ad7879_attributes[] = {
	&dev_attr_disable.attr,
	&dev_attr_gpio.attr,
	NULL
};

static const struct attribute_group ad7879_attr_group = {
	.attrs = ad7879_attributes,
};

static int __devinit ad7879_construct(bus_device *bus, struct ad7879 *ts)
{
	struct input_dev *input_dev;
	struct ad7879_platform_data *pdata = bus->dev.platform_data;
	int err;
	u16 revid;

	if (!bus->irq) {
		dev_err(&bus->dev, "no IRQ?\n");
		return -ENODEV;
	}

	if (!pdata) {
		dev_err(&bus->dev, "no platform data?\n");
		return -ENODEV;
	}

	input_dev = input_allocate_device();
	if (!input_dev)
		return -ENOMEM;

	ts->input = input_dev;

	setup_timer(&ts->timer, ad7879_timer, (unsigned long) ts);
	INIT_WORK(&ts->work, ad7879_work);
	mutex_init(&ts->mutex);

	ts->x_plate_ohms = pdata->x_plate_ohms ? : 400;
	ts->pressure_max = pdata->pressure_max ? : ~0;

	ts->first_conversion_delay = pdata->first_conversion_delay;
	ts->acquisition_time = pdata->acquisition_time;
	ts->averaging = pdata->averaging;
	ts->pen_down_acc_interval = pdata->pen_down_acc_interval;
	ts->median = pdata->median;

	if (pdata->gpio_output)
		ts->gpio_init = AD7879_GPIO_EN |
				(pdata->gpio_default ? 0 : AD7879_GPIO_DATA);
	else
		ts->gpio_init = AD7879_GPIO_EN | AD7879_GPIODIR;

	snprintf(ts->phys, sizeof(ts->phys), "%s/input0", dev_name(&bus->dev));

	input_dev->name = "AD7879 Touchscreen";
	input_dev->phys = ts->phys;
	input_dev->dev.parent = &bus->dev;

	__set_bit(EV_ABS, input_dev->evbit);
	__set_bit(ABS_X, input_dev->absbit);
	__set_bit(ABS_Y, input_dev->absbit);
	__set_bit(ABS_PRESSURE, input_dev->absbit);

	input_set_abs_params(input_dev, ABS_X,
			pdata->x_min ? : 0,
			pdata->x_max ? : MAX_12BIT,
			0, 0);
	input_set_abs_params(input_dev, ABS_Y,
			pdata->y_min ? : 0,
			pdata->y_max ? : MAX_12BIT,
			0, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE,
			pdata->pressure_min, pdata->pressure_max, 0, 0);

	err = ad7879_write(bus, AD7879_REG_CTRL2, AD7879_RESET);

	if (err < 0) {
		dev_err(&bus->dev, "Failed to write %s\n", input_dev->name);
		goto err_free_mem;
	}

	revid = ad7879_read(bus, AD7879_REG_REVID);

	if ((revid & 0xFF) != AD7879_DEVID) {
		dev_err(&bus->dev, "Failed to probe %s\n", input_dev->name);
		err = -ENODEV;
		goto err_free_mem;
	}

	ad7879_setup(ts);

	err = request_irq(bus->irq, ad7879_irq,
			  IRQF_TRIGGER_FALLING, bus->dev.driver->name, ts);

	if (err) {
		dev_err(&bus->dev, "irq %d busy?\n", bus->irq);
		goto err_free_mem;
	}

	err = sysfs_create_group(&bus->dev.kobj, &ad7879_attr_group);
	if (err)
		goto err_free_irq;

	err = input_register_device(input_dev);
	if (err)
		goto err_remove_attr;

	dev_info(&bus->dev, "Rev.%d touchscreen, irq %d\n",
		 revid >> 8, bus->irq);

	return 0;

err_remove_attr:
	sysfs_remove_group(&bus->dev.kobj, &ad7879_attr_group);
err_free_irq:
	free_irq(bus->irq, ts);
err_free_mem:
	input_free_device(input_dev);

	return err;
}

static int __devexit ad7879_destroy(bus_device *bus, struct ad7879 *ts)
{
	ad7879_disable(ts);
	sysfs_remove_group(&ts->bus->dev.kobj, &ad7879_attr_group);
	free_irq(ts->bus->irq, ts);
	input_unregister_device(ts->input);
	dev_dbg(&bus->dev, "unregistered touchscreen\n");

	return 0;
}

#ifdef CONFIG_PM
static int ad7879_suspend(bus_device *bus, pm_message_t message)
{
	struct ad7879 *ts = dev_get_drvdata(&bus->dev);

	ad7879_disable(ts);

	return 0;
}

static int ad7879_resume(bus_device *bus)
{
	struct ad7879 *ts = dev_get_drvdata(&bus->dev);

	ad7879_enable(ts);

	return 0;
}
#else
#define ad7879_suspend NULL
#define ad7879_resume  NULL
#endif

#if defined(CONFIG_TOUCHSCREEN_AD7879_SPI) || defined(CONFIG_TOUCHSCREEN_AD7879_SPI_MODULE)
#define MAX_SPI_FREQ_HZ		5000000
#define AD7879_CMD_MAGIC	0xE000
#define AD7879_CMD_READ		(1 << 10)
#define AD7879_WRITECMD(reg)	(AD7879_CMD_MAGIC | (reg & 0xF))
#define AD7879_READCMD(reg)	(AD7879_CMD_MAGIC | AD7879_CMD_READ | (reg & 0xF))

struct ser_req {
	u16			command;
	u16			data;
	struct spi_message	msg;
	struct spi_transfer	xfer[2];
};

/*
 * ad7879_read/write are only used for initial setup and for sysfs controls.
 * The main traffic is done in ad7879_collect().
 */

static int ad7879_read(struct spi_device *spi, u8 reg)
{
	struct ser_req *req;
	int status, ret;

	req = kzalloc(sizeof *req, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	spi_message_init(&req->msg);

	req->command = (u16) AD7879_READCMD(reg);
	req->xfer[0].tx_buf = &req->command;
	req->xfer[0].len = 2;

	req->xfer[1].rx_buf = &req->data;
	req->xfer[1].len = 2;

	spi_message_add_tail(&req->xfer[0], &req->msg);
	spi_message_add_tail(&req->xfer[1], &req->msg);

	status = spi_sync(spi, &req->msg);
	ret = status ? : req->data;

	kfree(req);

	return ret;
}

static int ad7879_write(struct spi_device *spi, u8 reg, u16 val)
{
	struct ser_req *req;
	int status;

	req = kzalloc(sizeof *req, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	spi_message_init(&req->msg);

	req->command = (u16) AD7879_WRITECMD(reg);
	req->xfer[0].tx_buf = &req->command;
	req->xfer[0].len = 2;

	req->data = val;
	req->xfer[1].tx_buf = &req->data;
	req->xfer[1].len = 2;

	spi_message_add_tail(&req->xfer[0], &req->msg);
	spi_message_add_tail(&req->xfer[1], &req->msg);

	status = spi_sync(spi, &req->msg);

	kfree(req);

	return status;
}

static void ad7879_collect(struct ad7879 *ts)
{
	int status = spi_sync(ts->bus, &ts->msg);

	if (status)
		dev_err(&ts->bus->dev, "spi_sync --> %d\n", status);
}

static void ad7879_setup_ts_def_msg(struct ad7879 *ts)
{
	struct spi_message *m;
	int i;

	ts->cmd = (u16) AD7879_READCMD(AD7879_REG_XPLUS);

	m = &ts->msg;
	spi_message_init(m);
	ts->xfer[0].tx_buf = &ts->cmd;
	ts->xfer[0].len = 2;

	spi_message_add_tail(&ts->xfer[0], m);

	for (i = 0; i < AD7879_NR_SENSE; i++) {
		ts->xfer[i + 1].rx_buf = &ts->conversion_data[i];
		ts->xfer[i + 1].len = 2;
		spi_message_add_tail(&ts->xfer[i + 1], m);
	}
}

static int __devinit ad7879_probe(struct spi_device *spi)
{
	struct ad7879 *ts;
	int error;

	/* don't exceed max specified SPI CLK frequency */
	if (spi->max_speed_hz > MAX_SPI_FREQ_HZ) {
		dev_err(&spi->dev, "SPI CLK %d Hz?\n", spi->max_speed_hz);
		return -EINVAL;
	}

	ts = kzalloc(sizeof(struct ad7879), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	dev_set_drvdata(&spi->dev, ts);
	ts->bus = spi;

	ad7879_setup_ts_def_msg(ts);

	error = ad7879_construct(spi, ts);
	if (error) {
		dev_set_drvdata(&spi->dev, NULL);
		kfree(ts);
	}

	return 0;
}

static int __devexit ad7879_remove(struct spi_device *spi)
{
	struct ad7879 *ts = dev_get_drvdata(&spi->dev);

	ad7879_destroy(spi, ts);
	dev_set_drvdata(&spi->dev, NULL);
	kfree(ts);

	return 0;
}

static struct spi_driver ad7879_driver = {
	.driver = {
		.name	= "ad7879",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe		= ad7879_probe,
	.remove		= __devexit_p(ad7879_remove),
	.suspend	= ad7879_suspend,
	.resume		= ad7879_resume,
};

static int __init ad7879_init(void)
{
	return spi_register_driver(&ad7879_driver);
}
module_init(ad7879_init);

static void __exit ad7879_exit(void)
{
	spi_unregister_driver(&ad7879_driver);
}
module_exit(ad7879_exit);

#elif defined(CONFIG_TOUCHSCREEN_AD7879_I2C) || defined(CONFIG_TOUCHSCREEN_AD7879_I2C_MODULE)

/* All registers are word-sized.
 * AD7879 uses a high-byte first convention.
 */
static int ad7879_read(struct i2c_client *client, u8 reg)
{
	return swab16(i2c_smbus_read_word_data(client, reg));
}

static int ad7879_write(struct i2c_client *client, u8 reg, u16 val)
{
	return i2c_smbus_write_word_data(client, reg, swab16(val));
}

static void ad7879_collect(struct ad7879 *ts)
{
	int i;

	for (i = 0; i < AD7879_NR_SENSE; i++)
		ts->conversion_data[i] = ad7879_read(ts->bus,
						     AD7879_REG_XPLUS + i);
}

static int __devinit ad7879_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	struct ad7879 *ts;
	int error;

	if (!i2c_check_functionality(client->adapter,
					I2C_FUNC_SMBUS_WORD_DATA)) {
		dev_err(&client->dev, "SMBUS Word Data not Supported\n");
		return -EIO;
	}

	ts = kzalloc(sizeof(struct ad7879), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	i2c_set_clientdata(client, ts);
	ts->bus = client;

	error = ad7879_construct(client, ts);
	if (error) {
		i2c_set_clientdata(client, NULL);
		kfree(ts);
	}

	return 0;
}

static int __devexit ad7879_remove(struct i2c_client *client)
{
	struct ad7879 *ts = dev_get_drvdata(&client->dev);

	ad7879_destroy(client, ts);
	i2c_set_clientdata(client, NULL);
	kfree(ts);

	return 0;
}

static const struct i2c_device_id ad7879_id[] = {
	{ "ad7879", 0 },
	{ "ad7889", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ad7879_id);

static struct i2c_driver ad7879_driver = {
	.driver = {
		.name	= "ad7879",
		.owner	= THIS_MODULE,
	},
	.probe		= ad7879_probe,
	.remove		= __devexit_p(ad7879_remove),
	.suspend	= ad7879_suspend,
	.resume		= ad7879_resume,
	.id_table	= ad7879_id,
};

static int __init ad7879_init(void)
{
	return i2c_add_driver(&ad7879_driver);
}
module_init(ad7879_init);

static void __exit ad7879_exit(void)
{
	i2c_del_driver(&ad7879_driver);
}
module_exit(ad7879_exit);
#endif

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("AD7879(-1) touchscreen Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:ad7879");
