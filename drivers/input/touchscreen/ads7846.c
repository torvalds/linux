/*
 * ADS7846 based touchscreen and sensor driver
 *
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
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#include <linux/device.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/spi/ads7846.h>
#include <asm/irq.h>

#ifdef	CONFIG_ARM
#include <asm/mach-types.h>
#ifdef	CONFIG_ARCH_OMAP
#include <asm/arch/gpio.h>
#endif
#endif


/*
 * This code has been heavily tested on a Nokia 770, and lightly
 * tested on other ads7846 devices (OSK/Mistral, Lubbock).
 * Support for ads7843 and ads7845 has only been stubbed in.
 *
 * IRQ handling needs a workaround because of a shortcoming in handling
 * edge triggered IRQs on some platforms like the OMAP1/2. These
 * platforms don't handle the ARM lazy IRQ disabling properly, thus we
 * have to maintain our own SW IRQ disabled status. This should be
 * removed as soon as the affected platform's IRQ handling is fixed.
 *
 * app note sbaa036 talks in more detail about accurate sampling...
 * that ought to help in situations like LCDs inducing noise (which
 * can also be helped by using synch signals) and more generally.
 * This driver tries to utilize the measures described in the app
 * note. The strength of filtering can be set in the board-* specific
 * files.
 */

#define	TS_POLL_PERIOD	msecs_to_jiffies(10)

/* this driver doesn't aim at the peak continuous sample rate */
#define	SAMPLE_BITS	(8 /*cmd*/ + 16 /*sample*/ + 2 /* before, after */)

struct ts_event {
	/* For portability, we can't read 12 bit values using SPI (which
	 * would make the controller deliver them as native byteorder u16
	 * with msbs zeroed).  Instead, we read them as two 8-bit values,
	 * which need byteswapping then range adjustment.
	 */
	__be16 x;
	__be16 y;
	__be16 z1, z2;
	int    ignore;
};

struct ads7846 {
	struct input_dev	*input;
	char			phys[32];

	struct spi_device	*spi;
	struct attribute_group	*attr_group;
	u16			model;
	u16			vref_delay_usecs;
	u16			x_plate_ohms;
	u16			pressure_max;

	u8			read_x, read_y, read_z1, read_z2, pwrdown;
	u16			dummy;		/* for the pwrdown read */
	struct ts_event		tc;

	struct spi_transfer	xfer[10];
	struct spi_message	msg[5];
	struct spi_message	*last_msg;
	int			msg_idx;
	int			read_cnt;
	int			read_rep;
	int			last_read;

	u16			debounce_max;
	u16			debounce_tol;
	u16			debounce_rep;

	spinlock_t		lock;
	struct timer_list	timer;		/* P: lock */
	unsigned		pendown:1;	/* P: lock */
	unsigned		pending:1;	/* P: lock */
// FIXME remove "irq_disabled"
	unsigned		irq_disabled:1;	/* P: lock */
	unsigned		disabled:1;

	int			(*get_pendown_state)(void);
};

/* leave chip selected when we're done, for quicker re-select? */
#if	0
#define	CS_CHANGE(xfer)	((xfer).cs_change = 1)
#else
#define	CS_CHANGE(xfer)	((xfer).cs_change = 0)
#endif

/*--------------------------------------------------------------------------*/

/* The ADS7846 has touchscreen and other sensors.
 * Earlier ads784x chips are somewhat compatible.
 */
#define	ADS_START		(1 << 7)
#define	ADS_A2A1A0_d_y		(1 << 4)	/* differential */
#define	ADS_A2A1A0_d_z1		(3 << 4)	/* differential */
#define	ADS_A2A1A0_d_z2		(4 << 4)	/* differential */
#define	ADS_A2A1A0_d_x		(5 << 4)	/* differential */
#define	ADS_A2A1A0_temp0	(0 << 4)	/* non-differential */
#define	ADS_A2A1A0_vbatt	(2 << 4)	/* non-differential */
#define	ADS_A2A1A0_vaux		(6 << 4)	/* non-differential */
#define	ADS_A2A1A0_temp1	(7 << 4)	/* non-differential */
#define	ADS_8_BIT		(1 << 3)
#define	ADS_12_BIT		(0 << 3)
#define	ADS_SER			(1 << 2)	/* non-differential */
#define	ADS_DFR			(0 << 2)	/* differential */
#define	ADS_PD10_PDOWN		(0 << 0)	/* lowpower mode + penirq */
#define	ADS_PD10_ADC_ON		(1 << 0)	/* ADC on */
#define	ADS_PD10_REF_ON		(2 << 0)	/* vREF on + penirq */
#define	ADS_PD10_ALL_ON		(3 << 0)	/* ADC + vREF on */

#define	MAX_12BIT	((1<<12)-1)

/* leave ADC powered up (disables penirq) between differential samples */
#define	READ_12BIT_DFR(x) (ADS_START | ADS_A2A1A0_d_ ## x \
	| ADS_12_BIT | ADS_DFR)

#define	READ_Y	(READ_12BIT_DFR(y)  | ADS_PD10_ADC_ON)
#define	READ_Z1	(READ_12BIT_DFR(z1) | ADS_PD10_ADC_ON)
#define	READ_Z2	(READ_12BIT_DFR(z2) | ADS_PD10_ADC_ON)

#define	READ_X	(READ_12BIT_DFR(x)  | ADS_PD10_ADC_ON)
#define	PWRDOWN	(READ_12BIT_DFR(y)  | ADS_PD10_PDOWN)	/* LAST */

/* single-ended samples need to first power up reference voltage;
 * we leave both ADC and VREF powered
 */
#define	READ_12BIT_SER(x) (ADS_START | ADS_A2A1A0_ ## x \
	| ADS_12_BIT | ADS_SER)

#define	REF_ON	(READ_12BIT_DFR(x) | ADS_PD10_ALL_ON)
#define	REF_OFF	(READ_12BIT_DFR(y) | ADS_PD10_PDOWN)

/*--------------------------------------------------------------------------*/

/*
 * Non-touchscreen sensors only use single-ended conversions.
 */

struct ser_req {
	u8			ref_on;
	u8			command;
	u8			ref_off;
	u16			scratch;
	__be16			sample;
	struct spi_message	msg;
	struct spi_transfer	xfer[6];
};

static void ads7846_enable(struct ads7846 *ts);
static void ads7846_disable(struct ads7846 *ts);

static int device_suspended(struct device *dev)
{
	struct ads7846 *ts = dev_get_drvdata(dev);
	return dev->power.power_state.event != PM_EVENT_ON || ts->disabled;
}

static int ads7846_read12_ser(struct device *dev, unsigned command)
{
	struct spi_device	*spi = to_spi_device(dev);
	struct ads7846		*ts = dev_get_drvdata(dev);
	struct ser_req		*req = kzalloc(sizeof *req, GFP_KERNEL);
	int			status;
	int			sample;
	int			i;

	if (!req)
		return -ENOMEM;

	spi_message_init(&req->msg);

	/* activate reference, so it has time to settle; */
	req->ref_on = REF_ON;
	req->xfer[0].tx_buf = &req->ref_on;
	req->xfer[0].len = 1;
	req->xfer[1].rx_buf = &req->scratch;
	req->xfer[1].len = 2;

	/*
	 * for external VREF, 0 usec (and assume it's always on);
	 * for 1uF, use 800 usec;
	 * no cap, 100 usec.
	 */
	req->xfer[1].delay_usecs = ts->vref_delay_usecs;

	/* take sample */
	req->command = (u8) command;
	req->xfer[2].tx_buf = &req->command;
	req->xfer[2].len = 1;
	req->xfer[3].rx_buf = &req->sample;
	req->xfer[3].len = 2;

	/* REVISIT:  take a few more samples, and compare ... */

	/* turn off reference */
	req->ref_off = REF_OFF;
	req->xfer[4].tx_buf = &req->ref_off;
	req->xfer[4].len = 1;
	req->xfer[5].rx_buf = &req->scratch;
	req->xfer[5].len = 2;

	CS_CHANGE(req->xfer[5]);

	/* group all the transfers together, so we can't interfere with
	 * reading touchscreen state; disable penirq while sampling
	 */
	for (i = 0; i < 6; i++)
		spi_message_add_tail(&req->xfer[i], &req->msg);

	ts->irq_disabled = 1;
	disable_irq(spi->irq);
	status = spi_sync(spi, &req->msg);
	ts->irq_disabled = 0;
	enable_irq(spi->irq);

	if (req->msg.status)
		status = req->msg.status;

	/* on-wire is a must-ignore bit, a BE12 value, then padding */
	sample = be16_to_cpu(req->sample);
	sample = sample >> 3;
	sample &= 0x0fff;

	kfree(req);
	return status ? status : sample;
}

#define SHOW(name) static ssize_t \
name ## _show(struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	ssize_t v = ads7846_read12_ser(dev, \
			READ_12BIT_SER(name) | ADS_PD10_ALL_ON); \
	if (v < 0) \
		return v; \
	return sprintf(buf, "%u\n", (unsigned) v); \
} \
static DEVICE_ATTR(name, S_IRUGO, name ## _show, NULL);

SHOW(temp0)
SHOW(temp1)
SHOW(vaux)
SHOW(vbatt)

static int is_pen_down(struct device *dev)
{
	struct ads7846		*ts = dev_get_drvdata(dev);

	return ts->pendown;
}

static ssize_t ads7846_pen_down_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", is_pen_down(dev));
}

static DEVICE_ATTR(pen_down, S_IRUGO, ads7846_pen_down_show, NULL);

static ssize_t ads7846_disable_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct ads7846	*ts = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", ts->disabled);
}

static ssize_t ads7846_disable_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct ads7846 *ts = dev_get_drvdata(dev);
	char *endp;
	int i;

	i = simple_strtoul(buf, &endp, 10);
	spin_lock_irq(&ts->lock);

	if (i)
		ads7846_disable(ts);
	else
		ads7846_enable(ts);

	spin_unlock_irq(&ts->lock);

	return count;
}

static DEVICE_ATTR(disable, 0664, ads7846_disable_show, ads7846_disable_store);

static struct attribute *ads7846_attributes[] = {
	&dev_attr_temp0.attr,
	&dev_attr_temp1.attr,
	&dev_attr_vbatt.attr,
	&dev_attr_vaux.attr,
	&dev_attr_pen_down.attr,
	&dev_attr_disable.attr,
	NULL,
};

static struct attribute_group ads7846_attr_group = {
	.attrs = ads7846_attributes,
};

/*
 * ads7843/7845 don't have temperature sensors, and
 * use the other sensors a bit differently too
 */

static struct attribute *ads7843_attributes[] = {
	&dev_attr_vbatt.attr,
	&dev_attr_vaux.attr,
	&dev_attr_pen_down.attr,
	&dev_attr_disable.attr,
	NULL,
};

static struct attribute_group ads7843_attr_group = {
	.attrs = ads7843_attributes,
};

static struct attribute *ads7845_attributes[] = {
	&dev_attr_vaux.attr,
	&dev_attr_pen_down.attr,
	&dev_attr_disable.attr,
	NULL,
};

static struct attribute_group ads7845_attr_group = {
	.attrs = ads7845_attributes,
};

/*--------------------------------------------------------------------------*/

/*
 * PENIRQ only kicks the timer.  The timer only reissues the SPI transfer,
 * to retrieve touchscreen status.
 *
 * The SPI transfer completion callback does the real work.  It reports
 * touchscreen events and reactivates the timer (or IRQ) as appropriate.
 */

static void ads7846_rx(void *ads)
{
	struct ads7846		*ts = ads;
	struct input_dev	*input_dev = ts->input;
	unsigned		Rt;
	unsigned		sync = 0;
	u16			x, y, z1, z2;
	unsigned long		flags;

	/* adjust:  on-wire is a must-ignore bit, a BE12 value, then padding;
	 * built from two 8 bit values written msb-first.
	 */
	x = (be16_to_cpu(ts->tc.x) >> 3) & 0x0fff;
	y = (be16_to_cpu(ts->tc.y) >> 3) & 0x0fff;
	z1 = (be16_to_cpu(ts->tc.z1) >> 3) & 0x0fff;
	z2 = (be16_to_cpu(ts->tc.z2) >> 3) & 0x0fff;

	/* range filtering */
	if (x == MAX_12BIT)
		x = 0;

	if (likely(x && z1 && !device_suspended(&ts->spi->dev))) {
		/* compute touch pressure resistance using equation #2 */
		Rt = z2;
		Rt -= z1;
		Rt *= x;
		Rt *= ts->x_plate_ohms;
		Rt /= z1;
		Rt = (Rt + 2047) >> 12;
	} else
		Rt = 0;

	/* Sample found inconsistent by debouncing or pressure is beyond
	* the maximum. Don't report it to user space, repeat at least
	* once more the measurement */
	if (ts->tc.ignore || Rt > ts->pressure_max) {
		mod_timer(&ts->timer, jiffies + TS_POLL_PERIOD);
		return;
	}

	/* NOTE:  "pendown" is inferred from pressure; we don't rely on
	 * being able to check nPENIRQ status, or "friendly" trigger modes
	 * (both-edges is much better than just-falling or low-level).
	 *
	 * REVISIT:  some boards may require reading nPENIRQ; it's
	 * needed on 7843.  and 7845 reads pressure differently...
	 *
	 * REVISIT:  the touchscreen might not be connected; this code
	 * won't notice that, even if nPENIRQ never fires ...
	 */
	if (!ts->pendown && Rt != 0) {
		input_report_key(input_dev, BTN_TOUCH, 1);
		sync = 1;
	} else if (ts->pendown && Rt == 0) {
		input_report_key(input_dev, BTN_TOUCH, 0);
		sync = 1;
	}

	if (Rt) {
		input_report_abs(input_dev, ABS_X, x);
		input_report_abs(input_dev, ABS_Y, y);
		sync = 1;
	}

	if (sync) {
		input_report_abs(input_dev, ABS_PRESSURE, Rt);
		input_sync(input_dev);
	}

#ifdef	VERBOSE
	if (Rt || ts->pendown)
		pr_debug("%s: %d/%d/%d%s\n", ts->spi->dev.bus_id,
			x, y, Rt, Rt ? "" : " UP");
#endif

	spin_lock_irqsave(&ts->lock, flags);

	ts->pendown = (Rt != 0);
	mod_timer(&ts->timer, jiffies + TS_POLL_PERIOD);

	spin_unlock_irqrestore(&ts->lock, flags);
}

static void ads7846_debounce(void *ads)
{
	struct ads7846		*ts = ads;
	struct spi_message	*m;
	struct spi_transfer	*t;
	int			val;
	int			status;

	m = &ts->msg[ts->msg_idx];
	t = list_entry(m->transfers.prev, struct spi_transfer, transfer_list);
	val = (be16_to_cpu(*(__be16 *)t->rx_buf) >> 3) & 0x0fff;
	if (!ts->read_cnt || (abs(ts->last_read - val) > ts->debounce_tol)) {
		/* Repeat it, if this was the first read or the read
		 * wasn't consistent enough. */
		if (ts->read_cnt < ts->debounce_max) {
			ts->last_read = val;
			ts->read_cnt++;
		} else {
			/* Maximum number of debouncing reached and still
			 * not enough number of consistent readings. Abort
			 * the whole sample, repeat it in the next sampling
			 * period.
			 */
			ts->tc.ignore = 1;
			ts->read_cnt = 0;
			/* Last message will contain ads7846_rx() as the
			 * completion function.
			 */
			m = ts->last_msg;
		}
		/* Start over collecting consistent readings. */
		ts->read_rep = 0;
	} else {
		if (++ts->read_rep > ts->debounce_rep) {
			/* Got a good reading for this coordinate,
			 * go for the next one. */
			ts->tc.ignore = 0;
			ts->msg_idx++;
			ts->read_cnt = 0;
			ts->read_rep = 0;
			m++;
		} else
			/* Read more values that are consistent. */
			ts->read_cnt++;
	}
	status = spi_async(ts->spi, m);
	if (status)
		dev_err(&ts->spi->dev, "spi_async --> %d\n",
				status);
}

static void ads7846_timer(unsigned long handle)
{
	struct ads7846	*ts = (void *)handle;
	int		status = 0;

	spin_lock_irq(&ts->lock);

	if (unlikely(ts->msg_idx && !ts->pendown)) {
		/* measurement cycle ended */
		if (!device_suspended(&ts->spi->dev)) {
			ts->irq_disabled = 0;
			enable_irq(ts->spi->irq);
		}
		ts->pending = 0;
		ts->msg_idx = 0;
	} else {
		/* pen is still down, continue with the measurement */
		ts->msg_idx = 0;
		status = spi_async(ts->spi, &ts->msg[0]);
		if (status)
			dev_err(&ts->spi->dev, "spi_async --> %d\n", status);
	}

	spin_unlock_irq(&ts->lock);
}

static irqreturn_t ads7846_irq(int irq, void *handle)
{
	struct ads7846 *ts = handle;
	unsigned long flags;

	spin_lock_irqsave(&ts->lock, flags);
	if (likely(ts->get_pendown_state())) {
		if (!ts->irq_disabled) {
			/* The ARM do_simple_IRQ() dispatcher doesn't act
			 * like the other dispatchers:  it will report IRQs
			 * even after they've been disabled.  We work around
			 * that here.  (The "generic irq" framework may help...)
			 */
			ts->irq_disabled = 1;
			disable_irq(ts->spi->irq);
			ts->pending = 1;
			mod_timer(&ts->timer, jiffies);
		}
	}
	spin_unlock_irqrestore(&ts->lock, flags);

	return IRQ_HANDLED;
}

/*--------------------------------------------------------------------------*/

/* Must be called with ts->lock held */
static void ads7846_disable(struct ads7846 *ts)
{
	if (ts->disabled)
		return;

	ts->disabled = 1;

	/* are we waiting for IRQ, or polling? */
	if (!ts->pending) {
		ts->irq_disabled = 1;
		disable_irq(ts->spi->irq);
	} else {
		/* the timer will run at least once more, and
		 * leave everything in a clean state, IRQ disabled
		 */
		while (ts->pending) {
			spin_unlock_irq(&ts->lock);
			msleep(1);
			spin_lock_irq(&ts->lock);
		}
	}

	/* we know the chip's in lowpower mode since we always
	 * leave it that way after every request
	 */

}

/* Must be called with ts->lock held */
static void ads7846_enable(struct ads7846 *ts)
{
	if (!ts->disabled)
		return;

	ts->disabled = 0;
	ts->irq_disabled = 0;
	enable_irq(ts->spi->irq);
}

static int ads7846_suspend(struct spi_device *spi, pm_message_t message)
{
	struct ads7846 *ts = dev_get_drvdata(&spi->dev);

	spin_lock_irq(&ts->lock);

	spi->dev.power.power_state = message;
	ads7846_disable(ts);

	spin_unlock_irq(&ts->lock);

	return 0;

}

static int ads7846_resume(struct spi_device *spi)
{
	struct ads7846 *ts = dev_get_drvdata(&spi->dev);

	spin_lock_irq(&ts->lock);

	spi->dev.power.power_state = PMSG_ON;
	ads7846_enable(ts);

	spin_unlock_irq(&ts->lock);

	return 0;
}

static int __devinit ads7846_probe(struct spi_device *spi)
{
	struct ads7846			*ts;
	struct input_dev		*input_dev;
	struct ads7846_platform_data	*pdata = spi->dev.platform_data;
	struct spi_message		*m;
	struct spi_transfer		*x;
	int				err;

	if (!spi->irq) {
		dev_dbg(&spi->dev, "no IRQ?\n");
		return -ENODEV;
	}

	if (!pdata) {
		dev_dbg(&spi->dev, "no platform data?\n");
		return -ENODEV;
	}

	/* don't exceed max specified sample rate */
	if (spi->max_speed_hz > (125000 * SAMPLE_BITS)) {
		dev_dbg(&spi->dev, "f(sample) %d KHz?\n",
				(spi->max_speed_hz/SAMPLE_BITS)/1000);
		return -EINVAL;
	}

	/* REVISIT when the irq can be triggered active-low, or if for some
	 * reason the touchscreen isn't hooked up, we don't need to access
	 * the pendown state.
	 */
	if (pdata->get_pendown_state == NULL) {
		dev_dbg(&spi->dev, "no get_pendown_state function?\n");
		return -EINVAL;
	}

	/* We'd set TX wordsize 8 bits and RX wordsize to 13 bits ... except
	 * that even if the hardware can do that, the SPI controller driver
	 * may not.  So we stick to very-portable 8 bit words, both RX and TX.
	 */
	spi->bits_per_word = 8;

	ts = kzalloc(sizeof(struct ads7846), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!ts || !input_dev) {
		err = -ENOMEM;
		goto err_free_mem;
	}

	dev_set_drvdata(&spi->dev, ts);
	spi->dev.power.power_state = PMSG_ON;

	ts->spi = spi;
	ts->input = input_dev;

	init_timer(&ts->timer);
	ts->timer.data = (unsigned long) ts;
	ts->timer.function = ads7846_timer;

	spin_lock_init(&ts->lock);

	ts->model = pdata->model ? : 7846;
	ts->vref_delay_usecs = pdata->vref_delay_usecs ? : 100;
	ts->x_plate_ohms = pdata->x_plate_ohms ? : 400;
	ts->pressure_max = pdata->pressure_max ? : ~0;
	if (pdata->debounce_max) {
		ts->debounce_max = pdata->debounce_max;
		ts->debounce_tol = pdata->debounce_tol;
		ts->debounce_rep = pdata->debounce_rep;
		if (ts->debounce_rep > ts->debounce_max + 1)
			ts->debounce_rep = ts->debounce_max - 1;
	} else
		ts->debounce_tol = ~0;
	ts->get_pendown_state = pdata->get_pendown_state;

	snprintf(ts->phys, sizeof(ts->phys), "%s/input0", spi->dev.bus_id);

	input_dev->name = "ADS784x Touchscreen";
	input_dev->phys = ts->phys;
	input_dev->cdev.dev = &spi->dev;

	input_dev->evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);
	input_dev->keybit[LONG(BTN_TOUCH)] = BIT(BTN_TOUCH);
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

	/* set up the transfers to read touchscreen state; this assumes we
	 * use formula #2 for pressure, not #3.
	 */
	m = &ts->msg[0];
	x = ts->xfer;

	spi_message_init(m);

	/* y- still on; turn on only y+ (and ADC) */
	ts->read_y = READ_Y;
	x->tx_buf = &ts->read_y;
	x->len = 1;
	spi_message_add_tail(x, m);

	x++;
	x->rx_buf = &ts->tc.y;
	x->len = 2;
	spi_message_add_tail(x, m);

	m->complete = ads7846_debounce;
	m->context = ts;

	m++;
	spi_message_init(m);

	/* turn y- off, x+ on, then leave in lowpower */
	x++;
	ts->read_x = READ_X;
	x->tx_buf = &ts->read_x;
	x->len = 1;
	spi_message_add_tail(x, m);

	x++;
	x->rx_buf = &ts->tc.x;
	x->len = 2;
	spi_message_add_tail(x, m);

	m->complete = ads7846_debounce;
	m->context = ts;

	/* turn y+ off, x- on; we'll use formula #2 */
	if (ts->model == 7846) {
		m++;
		spi_message_init(m);

		x++;
		ts->read_z1 = READ_Z1;
		x->tx_buf = &ts->read_z1;
		x->len = 1;
		spi_message_add_tail(x, m);

		x++;
		x->rx_buf = &ts->tc.z1;
		x->len = 2;
		spi_message_add_tail(x, m);

		m->complete = ads7846_debounce;
		m->context = ts;

		m++;
		spi_message_init(m);

		x++;
		ts->read_z2 = READ_Z2;
		x->tx_buf = &ts->read_z2;
		x->len = 1;
		spi_message_add_tail(x, m);

		x++;
		x->rx_buf = &ts->tc.z2;
		x->len = 2;
		spi_message_add_tail(x, m);

		m->complete = ads7846_debounce;
		m->context = ts;
	}

	/* power down */
	m++;
	spi_message_init(m);

	x++;
	ts->pwrdown = PWRDOWN;
	x->tx_buf = &ts->pwrdown;
	x->len = 1;
	spi_message_add_tail(x, m);

	x++;
	x->rx_buf = &ts->dummy;
	x->len = 2;
	CS_CHANGE(*x);
	spi_message_add_tail(x, m);

	m->complete = ads7846_rx;
	m->context = ts;

	ts->last_msg = m;

	if (request_irq(spi->irq, ads7846_irq, IRQF_TRIGGER_FALLING,
			spi->dev.driver->name, ts)) {
		dev_dbg(&spi->dev, "irq %d busy?\n", spi->irq);
		err = -EBUSY;
		goto err_free_mem;
	}

	dev_info(&spi->dev, "touchscreen, irq %d\n", spi->irq);

	/* take a first sample, leaving nPENIRQ active; avoid
	 * the touchscreen, in case it's not connected.
	 */
	(void) ads7846_read12_ser(&spi->dev,
			  READ_12BIT_SER(vaux) | ADS_PD10_ALL_ON);

	switch (ts->model) {
	case 7846:
		ts->attr_group = &ads7846_attr_group;
		break;
	case 7845:
		ts->attr_group = &ads7845_attr_group;
		break;
	default:
		ts->attr_group = &ads7843_attr_group;
		break;
	}
	err = sysfs_create_group(&spi->dev.kobj, ts->attr_group);
	if (err)
		goto err_free_irq;

	err = input_register_device(input_dev);
	if (err)
		goto err_remove_attr_group;

	return 0;

 err_remove_attr_group:
	sysfs_remove_group(&spi->dev.kobj, ts->attr_group);
 err_free_irq:
	free_irq(spi->irq, ts);
 err_free_mem:
	input_free_device(input_dev);
	kfree(ts);
	return err;
}

static int __devexit ads7846_remove(struct spi_device *spi)
{
	struct ads7846		*ts = dev_get_drvdata(&spi->dev);

	input_unregister_device(ts->input);

	ads7846_suspend(spi, PMSG_SUSPEND);

	sysfs_remove_group(&spi->dev.kobj, ts->attr_group);

	free_irq(ts->spi->irq, ts);
	/* suspend left the IRQ disabled */
	enable_irq(ts->spi->irq);

	kfree(ts);

	dev_dbg(&spi->dev, "unregistered touchscreen\n");
	return 0;
}

static struct spi_driver ads7846_driver = {
	.driver = {
		.name	= "ads7846",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe		= ads7846_probe,
	.remove		= __devexit_p(ads7846_remove),
	.suspend	= ads7846_suspend,
	.resume		= ads7846_resume,
};

static int __init ads7846_init(void)
{
	/* grr, board-specific init should stay out of drivers!! */

#ifdef	CONFIG_ARCH_OMAP
	if (machine_is_omap_osk()) {
		/* GPIO4 = PENIRQ; GPIO6 = BUSY */
		omap_request_gpio(4);
		omap_set_gpio_direction(4, 1);
		omap_request_gpio(6);
		omap_set_gpio_direction(6, 1);
	}
	// also TI 1510 Innovator, bitbanging through FPGA
	// also Nokia 770
	// also Palm Tungsten T2
#endif

	// PXA:
	// also Dell Axim X50
	// also HP iPaq H191x/H192x/H415x/H435x
	// also Intel Lubbock (additional to UCB1400; as temperature sensor)
	// also Sharp Zaurus C7xx, C8xx (corgi/sheperd/husky)

	// Atmel at91sam9261-EK uses ads7843

	// also various AMD Au1x00 devel boards

	return spi_register_driver(&ads7846_driver);
}
module_init(ads7846_init);

static void __exit ads7846_exit(void)
{
	spi_unregister_driver(&ads7846_driver);

#ifdef	CONFIG_ARCH_OMAP
	if (machine_is_omap_osk()) {
		omap_free_gpio(4);
		omap_free_gpio(6);
	}
#endif

}
module_exit(ads7846_exit);

MODULE_DESCRIPTION("ADS7846 TouchScreen Driver");
MODULE_LICENSE("GPL");
