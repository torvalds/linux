// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2006-2008 Michael Hennerich, Analog Devices Inc.
 *
 * Description:	AD7877 based touchscreen, sensor (ADCs), DAC and GPIO driver
 * Based on:	ads7846.c
 *
 * Bugs:        Enter bugs at http://blackfin.uclinux.org/
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
 */


#include <linux/device.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/spi/ad7877.h>
#include <linux/module.h>
#include <asm/irq.h>

#define	TS_PEN_UP_TIMEOUT	msecs_to_jiffies(100)

#define MAX_SPI_FREQ_HZ			20000000
#define	MAX_12BIT			((1<<12)-1)

#define AD7877_REG_ZEROS			0
#define AD7877_REG_CTRL1			1
#define AD7877_REG_CTRL2			2
#define AD7877_REG_ALERT			3
#define AD7877_REG_AUX1HIGH			4
#define AD7877_REG_AUX1LOW			5
#define AD7877_REG_BAT1HIGH			6
#define AD7877_REG_BAT1LOW			7
#define AD7877_REG_BAT2HIGH			8
#define AD7877_REG_BAT2LOW			9
#define AD7877_REG_TEMP1HIGH			10
#define AD7877_REG_TEMP1LOW			11
#define AD7877_REG_SEQ0				12
#define AD7877_REG_SEQ1				13
#define AD7877_REG_DAC				14
#define AD7877_REG_NONE1			15
#define AD7877_REG_EXTWRITE			15
#define AD7877_REG_XPLUS			16
#define AD7877_REG_YPLUS			17
#define AD7877_REG_Z2				18
#define AD7877_REG_aux1				19
#define AD7877_REG_aux2				20
#define AD7877_REG_aux3				21
#define AD7877_REG_bat1				22
#define AD7877_REG_bat2				23
#define AD7877_REG_temp1			24
#define AD7877_REG_temp2			25
#define AD7877_REG_Z1				26
#define AD7877_REG_GPIOCTRL1			27
#define AD7877_REG_GPIOCTRL2			28
#define AD7877_REG_GPIODATA			29
#define AD7877_REG_NONE2			30
#define AD7877_REG_NONE3			31

#define AD7877_SEQ_YPLUS_BIT			(1<<11)
#define AD7877_SEQ_XPLUS_BIT			(1<<10)
#define AD7877_SEQ_Z2_BIT			(1<<9)
#define AD7877_SEQ_AUX1_BIT			(1<<8)
#define AD7877_SEQ_AUX2_BIT			(1<<7)
#define AD7877_SEQ_AUX3_BIT			(1<<6)
#define AD7877_SEQ_BAT1_BIT			(1<<5)
#define AD7877_SEQ_BAT2_BIT			(1<<4)
#define AD7877_SEQ_TEMP1_BIT			(1<<3)
#define AD7877_SEQ_TEMP2_BIT			(1<<2)
#define AD7877_SEQ_Z1_BIT			(1<<1)

enum {
	AD7877_SEQ_YPOS  = 0,
	AD7877_SEQ_XPOS  = 1,
	AD7877_SEQ_Z2    = 2,
	AD7877_SEQ_AUX1  = 3,
	AD7877_SEQ_AUX2  = 4,
	AD7877_SEQ_AUX3  = 5,
	AD7877_SEQ_BAT1  = 6,
	AD7877_SEQ_BAT2  = 7,
	AD7877_SEQ_TEMP1 = 8,
	AD7877_SEQ_TEMP2 = 9,
	AD7877_SEQ_Z1    = 10,
	AD7877_NR_SENSE  = 11,
};

/* DAC Register Default RANGE 0 to Vcc, Volatge Mode, DAC On */
#define AD7877_DAC_CONF			0x1

/* If gpio3 is set AUX3/GPIO3 acts as GPIO Output */
#define AD7877_EXTW_GPIO_3_CONF		0x1C4
#define AD7877_EXTW_GPIO_DATA		0x200

/* Control REG 2 */
#define AD7877_TMR(x)			((x & 0x3) << 0)
#define AD7877_REF(x)			((x & 0x1) << 2)
#define AD7877_POL(x)			((x & 0x1) << 3)
#define AD7877_FCD(x)			((x & 0x3) << 4)
#define AD7877_PM(x)			((x & 0x3) << 6)
#define AD7877_ACQ(x)			((x & 0x3) << 8)
#define AD7877_AVG(x)			((x & 0x3) << 10)

/* Control REG 1 */
#define	AD7877_SER			(1 << 11)	/* non-differential */
#define	AD7877_DFR			(0 << 11)	/* differential */

#define AD7877_MODE_NOC  (0)	/* Do not convert */
#define AD7877_MODE_SCC  (1)	/* Single channel conversion */
#define AD7877_MODE_SEQ0 (2)	/* Sequence 0 in Slave Mode */
#define AD7877_MODE_SEQ1 (3)	/* Sequence 1 in Master Mode */

#define AD7877_CHANADD(x)		((x&0xF)<<7)
#define AD7877_READADD(x)		((x)<<2)
#define AD7877_WRITEADD(x)		((x)<<12)

#define AD7877_READ_CHAN(x) (AD7877_WRITEADD(AD7877_REG_CTRL1) | AD7877_SER | \
		AD7877_MODE_SCC | AD7877_CHANADD(AD7877_REG_ ## x) | \
		AD7877_READADD(AD7877_REG_ ## x))

#define AD7877_MM_SEQUENCE (AD7877_SEQ_YPLUS_BIT | AD7877_SEQ_XPLUS_BIT | \
		AD7877_SEQ_Z2_BIT | AD7877_SEQ_Z1_BIT)

/*
 * Non-touchscreen sensors only use single-ended conversions.
 */

struct ser_req {
	u16			reset;
	u16			ref_on;
	u16			command;
	struct spi_message	msg;
	struct spi_transfer	xfer[6];

	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */
	u16 sample ____cacheline_aligned;
};

struct ad7877 {
	struct input_dev	*input;
	char			phys[32];

	struct spi_device	*spi;
	u16			model;
	u16			vref_delay_usecs;
	u16			x_plate_ohms;
	u16			pressure_max;

	u16			cmd_crtl1;
	u16			cmd_crtl2;
	u16			cmd_dummy;
	u16			dac;

	u8			stopacq_polarity;
	u8			first_conversion_delay;
	u8			acquisition_time;
	u8			averaging;
	u8			pen_down_acc_interval;

	struct spi_transfer	xfer[AD7877_NR_SENSE + 2];
	struct spi_message	msg;

	struct mutex		mutex;
	bool			disabled;	/* P: mutex */
	bool			gpio3;		/* P: mutex */
	bool			gpio4;		/* P: mutex */

	spinlock_t		lock;
	struct timer_list	timer;		/* P: lock */

	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */
	u16 conversion_data[AD7877_NR_SENSE] ____cacheline_aligned;
};

static bool gpio3;
module_param(gpio3, bool, 0);
MODULE_PARM_DESC(gpio3, "If gpio3 is set to 1 AUX3 acts as GPIO3");

static int ad7877_read(struct spi_device *spi, u16 reg)
{
	struct ser_req *req;
	int status, ret;

	req = kzalloc(sizeof *req, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	spi_message_init(&req->msg);

	req->command = (u16) (AD7877_WRITEADD(AD7877_REG_CTRL1) |
			AD7877_READADD(reg));
	req->xfer[0].tx_buf = &req->command;
	req->xfer[0].len = 2;
	req->xfer[0].cs_change = 1;

	req->xfer[1].rx_buf = &req->sample;
	req->xfer[1].len = 2;

	spi_message_add_tail(&req->xfer[0], &req->msg);
	spi_message_add_tail(&req->xfer[1], &req->msg);

	status = spi_sync(spi, &req->msg);
	ret = status ? : req->sample;

	kfree(req);

	return ret;
}

static int ad7877_write(struct spi_device *spi, u16 reg, u16 val)
{
	struct ser_req *req;
	int status;

	req = kzalloc(sizeof *req, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	spi_message_init(&req->msg);

	req->command = (u16) (AD7877_WRITEADD(reg) | (val & MAX_12BIT));
	req->xfer[0].tx_buf = &req->command;
	req->xfer[0].len = 2;

	spi_message_add_tail(&req->xfer[0], &req->msg);

	status = spi_sync(spi, &req->msg);

	kfree(req);

	return status;
}

static int ad7877_read_adc(struct spi_device *spi, unsigned command)
{
	struct ad7877 *ts = spi_get_drvdata(spi);
	struct ser_req *req;
	int status;
	int sample;
	int i;

	req = kzalloc(sizeof *req, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	spi_message_init(&req->msg);

	/* activate reference, so it has time to settle; */
	req->ref_on = AD7877_WRITEADD(AD7877_REG_CTRL2) |
			 AD7877_POL(ts->stopacq_polarity) |
			 AD7877_AVG(0) | AD7877_PM(2) | AD7877_TMR(0) |
			 AD7877_ACQ(ts->acquisition_time) | AD7877_FCD(0);

	req->reset = AD7877_WRITEADD(AD7877_REG_CTRL1) | AD7877_MODE_NOC;

	req->command = (u16) command;

	req->xfer[0].tx_buf = &req->reset;
	req->xfer[0].len = 2;
	req->xfer[0].cs_change = 1;

	req->xfer[1].tx_buf = &req->ref_on;
	req->xfer[1].len = 2;
	req->xfer[1].delay_usecs = ts->vref_delay_usecs;
	req->xfer[1].cs_change = 1;

	req->xfer[2].tx_buf = &req->command;
	req->xfer[2].len = 2;
	req->xfer[2].delay_usecs = ts->vref_delay_usecs;
	req->xfer[2].cs_change = 1;

	req->xfer[3].rx_buf = &req->sample;
	req->xfer[3].len = 2;
	req->xfer[3].cs_change = 1;

	req->xfer[4].tx_buf = &ts->cmd_crtl2;	/*REF OFF*/
	req->xfer[4].len = 2;
	req->xfer[4].cs_change = 1;

	req->xfer[5].tx_buf = &ts->cmd_crtl1;	/*DEFAULT*/
	req->xfer[5].len = 2;

	/* group all the transfers together, so we can't interfere with
	 * reading touchscreen state; disable penirq while sampling
	 */
	for (i = 0; i < 6; i++)
		spi_message_add_tail(&req->xfer[i], &req->msg);

	status = spi_sync(spi, &req->msg);
	sample = req->sample;

	kfree(req);

	return status ? : sample;
}

static int ad7877_process_data(struct ad7877 *ts)
{
	struct input_dev *input_dev = ts->input;
	unsigned Rt;
	u16 x, y, z1, z2;

	x = ts->conversion_data[AD7877_SEQ_XPOS] & MAX_12BIT;
	y = ts->conversion_data[AD7877_SEQ_YPOS] & MAX_12BIT;
	z1 = ts->conversion_data[AD7877_SEQ_Z1] & MAX_12BIT;
	z2 = ts->conversion_data[AD7877_SEQ_Z2] & MAX_12BIT;

	/*
	 * The samples processed here are already preprocessed by the AD7877.
	 * The preprocessing function consists of an averaging filter.
	 * The combination of 'first conversion delay' and averaging provides a robust solution,
	 * discarding the spurious noise in the signal and keeping only the data of interest.
	 * The size of the averaging filter is programmable. (dev.platform_data, see linux/spi/ad7877.h)
	 * Other user-programmable conversion controls include variable acquisition time,
	 * and first conversion delay. Up to 16 averages can be taken per conversion.
	 */

	if (likely(x && z1)) {
		/* compute touch pressure resistance using equation #1 */
		Rt = (z2 - z1) * x * ts->x_plate_ohms;
		Rt /= z1;
		Rt = (Rt + 2047) >> 12;

		/*
		 * Sample found inconsistent, pressure is beyond
		 * the maximum. Don't report it to user space.
		 */
		if (Rt > ts->pressure_max)
			return -EINVAL;

		if (!timer_pending(&ts->timer))
			input_report_key(input_dev, BTN_TOUCH, 1);

		input_report_abs(input_dev, ABS_X, x);
		input_report_abs(input_dev, ABS_Y, y);
		input_report_abs(input_dev, ABS_PRESSURE, Rt);
		input_sync(input_dev);

		return 0;
	}

	return -EINVAL;
}

static inline void ad7877_ts_event_release(struct ad7877 *ts)
{
	struct input_dev *input_dev = ts->input;

	input_report_abs(input_dev, ABS_PRESSURE, 0);
	input_report_key(input_dev, BTN_TOUCH, 0);
	input_sync(input_dev);
}

static void ad7877_timer(struct timer_list *t)
{
	struct ad7877 *ts = from_timer(ts, t, timer);
	unsigned long flags;

	spin_lock_irqsave(&ts->lock, flags);
	ad7877_ts_event_release(ts);
	spin_unlock_irqrestore(&ts->lock, flags);
}

static irqreturn_t ad7877_irq(int irq, void *handle)
{
	struct ad7877 *ts = handle;
	unsigned long flags;
	int error;

	error = spi_sync(ts->spi, &ts->msg);
	if (error) {
		dev_err(&ts->spi->dev, "spi_sync --> %d\n", error);
		goto out;
	}

	spin_lock_irqsave(&ts->lock, flags);
	error = ad7877_process_data(ts);
	if (!error)
		mod_timer(&ts->timer, jiffies + TS_PEN_UP_TIMEOUT);
	spin_unlock_irqrestore(&ts->lock, flags);

out:
	return IRQ_HANDLED;
}

static void ad7877_disable(void *data)
{
	struct ad7877 *ts = data;

	mutex_lock(&ts->mutex);

	if (!ts->disabled) {
		ts->disabled = true;
		disable_irq(ts->spi->irq);

		if (del_timer_sync(&ts->timer))
			ad7877_ts_event_release(ts);
	}

	/*
	 * We know the chip's in lowpower mode since we always
	 * leave it that way after every request
	 */

	mutex_unlock(&ts->mutex);
}

static void ad7877_enable(struct ad7877 *ts)
{
	mutex_lock(&ts->mutex);

	if (ts->disabled) {
		ts->disabled = false;
		enable_irq(ts->spi->irq);
	}

	mutex_unlock(&ts->mutex);
}

#define SHOW(name) static ssize_t \
name ## _show(struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	struct ad7877 *ts = dev_get_drvdata(dev); \
	ssize_t v = ad7877_read_adc(ts->spi, \
			AD7877_READ_CHAN(name)); \
	if (v < 0) \
		return v; \
	return sprintf(buf, "%u\n", (unsigned) v); \
} \
static DEVICE_ATTR(name, S_IRUGO, name ## _show, NULL);

SHOW(aux1)
SHOW(aux2)
SHOW(aux3)
SHOW(bat1)
SHOW(bat2)
SHOW(temp1)
SHOW(temp2)

static ssize_t ad7877_disable_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct ad7877 *ts = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", ts->disabled);
}

static ssize_t ad7877_disable_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct ad7877 *ts = dev_get_drvdata(dev);
	unsigned int val;
	int error;

	error = kstrtouint(buf, 10, &val);
	if (error)
		return error;

	if (val)
		ad7877_disable(ts);
	else
		ad7877_enable(ts);

	return count;
}

static DEVICE_ATTR(disable, 0664, ad7877_disable_show, ad7877_disable_store);

static ssize_t ad7877_dac_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct ad7877 *ts = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", ts->dac);
}

static ssize_t ad7877_dac_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct ad7877 *ts = dev_get_drvdata(dev);
	unsigned int val;
	int error;

	error = kstrtouint(buf, 10, &val);
	if (error)
		return error;

	mutex_lock(&ts->mutex);
	ts->dac = val & 0xFF;
	ad7877_write(ts->spi, AD7877_REG_DAC, (ts->dac << 4) | AD7877_DAC_CONF);
	mutex_unlock(&ts->mutex);

	return count;
}

static DEVICE_ATTR(dac, 0664, ad7877_dac_show, ad7877_dac_store);

static ssize_t ad7877_gpio3_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct ad7877 *ts = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", ts->gpio3);
}

static ssize_t ad7877_gpio3_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct ad7877 *ts = dev_get_drvdata(dev);
	unsigned int val;
	int error;

	error = kstrtouint(buf, 10, &val);
	if (error)
		return error;

	mutex_lock(&ts->mutex);
	ts->gpio3 = !!val;
	ad7877_write(ts->spi, AD7877_REG_EXTWRITE, AD7877_EXTW_GPIO_DATA |
		 (ts->gpio4 << 4) | (ts->gpio3 << 5));
	mutex_unlock(&ts->mutex);

	return count;
}

static DEVICE_ATTR(gpio3, 0664, ad7877_gpio3_show, ad7877_gpio3_store);

static ssize_t ad7877_gpio4_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct ad7877 *ts = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", ts->gpio4);
}

static ssize_t ad7877_gpio4_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct ad7877 *ts = dev_get_drvdata(dev);
	unsigned int val;
	int error;

	error = kstrtouint(buf, 10, &val);
	if (error)
		return error;

	mutex_lock(&ts->mutex);
	ts->gpio4 = !!val;
	ad7877_write(ts->spi, AD7877_REG_EXTWRITE, AD7877_EXTW_GPIO_DATA |
		     (ts->gpio4 << 4) | (ts->gpio3 << 5));
	mutex_unlock(&ts->mutex);

	return count;
}

static DEVICE_ATTR(gpio4, 0664, ad7877_gpio4_show, ad7877_gpio4_store);

static struct attribute *ad7877_attributes[] = {
	&dev_attr_temp1.attr,
	&dev_attr_temp2.attr,
	&dev_attr_aux1.attr,
	&dev_attr_aux2.attr,
	&dev_attr_aux3.attr,
	&dev_attr_bat1.attr,
	&dev_attr_bat2.attr,
	&dev_attr_disable.attr,
	&dev_attr_dac.attr,
	&dev_attr_gpio3.attr,
	&dev_attr_gpio4.attr,
	NULL
};

static umode_t ad7877_attr_is_visible(struct kobject *kobj,
				     struct attribute *attr, int n)
{
	umode_t mode = attr->mode;

	if (attr == &dev_attr_aux3.attr) {
		if (gpio3)
			mode = 0;
	} else if (attr == &dev_attr_gpio3.attr) {
		if (!gpio3)
			mode = 0;
	}

	return mode;
}

static const struct attribute_group ad7877_attr_group = {
	.is_visible	= ad7877_attr_is_visible,
	.attrs		= ad7877_attributes,
};

static void ad7877_setup_ts_def_msg(struct spi_device *spi, struct ad7877 *ts)
{
	struct spi_message *m;
	int i;

	ts->cmd_crtl2 = AD7877_WRITEADD(AD7877_REG_CTRL2) |
			AD7877_POL(ts->stopacq_polarity) |
			AD7877_AVG(ts->averaging) | AD7877_PM(1) |
			AD7877_TMR(ts->pen_down_acc_interval) |
			AD7877_ACQ(ts->acquisition_time) |
			AD7877_FCD(ts->first_conversion_delay);

	ad7877_write(spi, AD7877_REG_CTRL2, ts->cmd_crtl2);

	ts->cmd_crtl1 = AD7877_WRITEADD(AD7877_REG_CTRL1) |
			AD7877_READADD(AD7877_REG_XPLUS-1) |
			AD7877_MODE_SEQ1 | AD7877_DFR;

	ad7877_write(spi, AD7877_REG_CTRL1, ts->cmd_crtl1);

	ts->cmd_dummy = 0;

	m = &ts->msg;

	spi_message_init(m);

	m->context = ts;

	ts->xfer[0].tx_buf = &ts->cmd_crtl1;
	ts->xfer[0].len = 2;
	ts->xfer[0].cs_change = 1;

	spi_message_add_tail(&ts->xfer[0], m);

	ts->xfer[1].tx_buf = &ts->cmd_dummy; /* Send ZERO */
	ts->xfer[1].len = 2;
	ts->xfer[1].cs_change = 1;

	spi_message_add_tail(&ts->xfer[1], m);

	for (i = 0; i < AD7877_NR_SENSE; i++) {
		ts->xfer[i + 2].rx_buf = &ts->conversion_data[AD7877_SEQ_YPOS + i];
		ts->xfer[i + 2].len = 2;
		if (i < (AD7877_NR_SENSE - 1))
			ts->xfer[i + 2].cs_change = 1;
		spi_message_add_tail(&ts->xfer[i + 2], m);
	}
}

static int ad7877_probe(struct spi_device *spi)
{
	struct ad7877			*ts;
	struct input_dev		*input_dev;
	struct ad7877_platform_data	*pdata = dev_get_platdata(&spi->dev);
	int				err;
	u16				verify;

	if (!spi->irq) {
		dev_dbg(&spi->dev, "no IRQ?\n");
		return -ENODEV;
	}

	if (!pdata) {
		dev_dbg(&spi->dev, "no platform data?\n");
		return -ENODEV;
	}

	/* don't exceed max specified SPI CLK frequency */
	if (spi->max_speed_hz > MAX_SPI_FREQ_HZ) {
		dev_dbg(&spi->dev, "SPI CLK %d Hz?\n",spi->max_speed_hz);
		return -EINVAL;
	}

	spi->bits_per_word = 16;
	err = spi_setup(spi);
	if (err) {
		dev_dbg(&spi->dev, "spi master doesn't support 16 bits/word\n");
		return err;
	}

	ts = devm_kzalloc(&spi->dev, sizeof(struct ad7877), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	input_dev = devm_input_allocate_device(&spi->dev);
	if (!input_dev)
		return -ENOMEM;

	err = devm_add_action_or_reset(&spi->dev, ad7877_disable, ts);
	if (err)
		return err;

	spi_set_drvdata(spi, ts);
	ts->spi = spi;
	ts->input = input_dev;

	timer_setup(&ts->timer, ad7877_timer, 0);
	mutex_init(&ts->mutex);
	spin_lock_init(&ts->lock);

	ts->model = pdata->model ? : 7877;
	ts->vref_delay_usecs = pdata->vref_delay_usecs ? : 100;
	ts->x_plate_ohms = pdata->x_plate_ohms ? : 400;
	ts->pressure_max = pdata->pressure_max ? : ~0;

	ts->stopacq_polarity = pdata->stopacq_polarity;
	ts->first_conversion_delay = pdata->first_conversion_delay;
	ts->acquisition_time = pdata->acquisition_time;
	ts->averaging = pdata->averaging;
	ts->pen_down_acc_interval = pdata->pen_down_acc_interval;

	snprintf(ts->phys, sizeof(ts->phys), "%s/input0", dev_name(&spi->dev));

	input_dev->name = "AD7877 Touchscreen";
	input_dev->phys = ts->phys;
	input_dev->dev.parent = &spi->dev;

	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(BTN_TOUCH, input_dev->keybit);
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

	ad7877_write(spi, AD7877_REG_SEQ1, AD7877_MM_SEQUENCE);

	verify = ad7877_read(spi, AD7877_REG_SEQ1);

	if (verify != AD7877_MM_SEQUENCE) {
		dev_err(&spi->dev, "%s: Failed to probe %s\n",
			dev_name(&spi->dev), input_dev->name);
		return -ENODEV;
	}

	if (gpio3)
		ad7877_write(spi, AD7877_REG_EXTWRITE, AD7877_EXTW_GPIO_3_CONF);

	ad7877_setup_ts_def_msg(spi, ts);

	/* Request AD7877 /DAV GPIO interrupt */

	err = devm_request_threaded_irq(&spi->dev, spi->irq, NULL, ad7877_irq,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					spi->dev.driver->name, ts);
	if (err) {
		dev_dbg(&spi->dev, "irq %d busy?\n", spi->irq);
		return err;
	}

	err = devm_device_add_group(&spi->dev, &ad7877_attr_group);
	if (err)
		return err;

	err = input_register_device(input_dev);
	if (err)
		return err;

	return 0;
}

static int __maybe_unused ad7877_suspend(struct device *dev)
{
	struct ad7877 *ts = dev_get_drvdata(dev);

	ad7877_disable(ts);

	return 0;
}

static int __maybe_unused ad7877_resume(struct device *dev)
{
	struct ad7877 *ts = dev_get_drvdata(dev);

	ad7877_enable(ts);

	return 0;
}

static SIMPLE_DEV_PM_OPS(ad7877_pm, ad7877_suspend, ad7877_resume);

static struct spi_driver ad7877_driver = {
	.driver = {
		.name	= "ad7877",
		.pm	= &ad7877_pm,
	},
	.probe		= ad7877_probe,
};

module_spi_driver(ad7877_driver);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("AD7877 touchscreen Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:ad7877");
