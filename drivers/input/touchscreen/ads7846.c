/*
 * ADS7846 based touchscreen and sensor driver
 *
 * Copyright (c) 2005 David Brownell
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
 * This code has been lightly tested on an ads7846.
 * Support for ads7843 and ads7845 has only been stubbed in.
 *
 * Not yet done:  investigate the values reported.  Are x/y/pressure
 * event values sane enough for X11?  How accurate are the temperature
 * and voltage readings?  (System-specific calibration should support
 * accuracy of 0.3 degrees C; otherwise it's 2.0 degrees.)
 *
 * app note sbaa036 talks in more detail about accurate sampling...
 * that ought to help in situations like LCDs inducing noise (which
 * can also be helped by using synch signals) and more generally.
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
};

struct ads7846 {
	struct input_dev	*input;
	char			phys[32];

	struct spi_device	*spi;
	u16			model;
	u16			vref_delay_usecs;
	u16			x_plate_ohms;

	u8			read_x, read_y, read_z1, read_z2;
	struct ts_event		tc;

	struct spi_transfer	xfer[8];
	struct spi_message	msg;

	spinlock_t		lock;
	struct timer_list	timer;		/* P: lock */
	unsigned		pendown:1;	/* P: lock */
	unsigned		pending:1;	/* P: lock */
// FIXME remove "irq_disabled"
	unsigned		irq_disabled:1;	/* P: lock */
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
#define	READ_X	(READ_12BIT_DFR(x)  | ADS_PD10_PDOWN)	/* LAST */

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

static int ads7846_read12_ser(struct device *dev, unsigned command)
{
	struct spi_device	*spi = to_spi_device(dev);
	struct ads7846		*ts = dev_get_drvdata(dev);
	struct ser_req		*req = kzalloc(sizeof *req, SLAB_KERNEL);
	int			status;
	int			sample;
	int			i;

	if (!req)
		return -ENOMEM;

	INIT_LIST_HEAD(&req->msg.transfers);

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

	disable_irq(spi->irq);
	status = spi_sync(spi, &req->msg);
	enable_irq(spi->irq);

	if (req->msg.status)
		status = req->msg.status;
	sample = be16_to_cpu(req->sample);
	sample = sample >> 4;
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

	/* adjust:  12 bit samples (left aligned), built from
	 * two 8 bit values writen msb-first.
	 */
	x = be16_to_cpu(ts->tc.x) >> 4;
	y = be16_to_cpu(ts->tc.y) >> 4;
	z1 = be16_to_cpu(ts->tc.z1) >> 4;
	z2 = be16_to_cpu(ts->tc.z2) >> 4;

	/* range filtering */
	if (x == MAX_12BIT)
		x = 0;

	if (x && z1 && ts->spi->dev.power.power_state.event == PM_EVENT_ON) {
		/* compute touch pressure resistance using equation #2 */
		Rt = z2;
		Rt -= z1;
		Rt *= x;
		Rt *= ts->x_plate_ohms;
		Rt /= z1;
		Rt = (Rt + 2047) >> 12;
	} else
		Rt = 0;

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
		input_report_abs(input_dev, ABS_PRESSURE, Rt);
		sync = 1;
	}
	if (sync)
		input_sync(input_dev);

#ifdef	VERBOSE
	if (Rt || ts->pendown)
		pr_debug("%s: %d/%d/%d%s\n", ts->spi->dev.bus_id,
			x, y, Rt, Rt ? "" : " UP");
#endif

	/* don't retrigger while we're suspended */
	spin_lock_irqsave(&ts->lock, flags);

	ts->pendown = (Rt != 0);
	ts->pending = 0;

	if (ts->spi->dev.power.power_state.event == PM_EVENT_ON) {
		if (ts->pendown)
			mod_timer(&ts->timer, jiffies + TS_POLL_PERIOD);
		else if (ts->irq_disabled) {
			ts->irq_disabled = 0;
			enable_irq(ts->spi->irq);
		}
	}

	spin_unlock_irqrestore(&ts->lock, flags);
}

static void ads7846_timer(unsigned long handle)
{
	struct ads7846	*ts = (void *)handle;
	int		status = 0;
	unsigned long	flags;

	spin_lock_irqsave(&ts->lock, flags);
	if (!ts->pending) {
		ts->pending = 1;
		if (!ts->irq_disabled) {
			ts->irq_disabled = 1;
			disable_irq(ts->spi->irq);
		}
		status = spi_async(ts->spi, &ts->msg);
		if (status)
			dev_err(&ts->spi->dev, "spi_async --> %d\n",
					status);
	}
	spin_unlock_irqrestore(&ts->lock, flags);
}

static irqreturn_t ads7846_irq(int irq, void *handle, struct pt_regs *regs)
{
	ads7846_timer((unsigned long) handle);
	return IRQ_HANDLED;
}

/*--------------------------------------------------------------------------*/

static int
ads7846_suspend(struct spi_device *spi, pm_message_t message)
{
	struct ads7846 *ts = dev_get_drvdata(&spi->dev);
	unsigned long	flags;

	spin_lock_irqsave(&ts->lock, flags);

	spi->dev.power.power_state = message;

	/* are we waiting for IRQ, or polling? */
	if (!ts->pendown) {
		if (!ts->irq_disabled) {
			ts->irq_disabled = 1;
			disable_irq(ts->spi->irq);
		}
	} else {
		/* polling; force a final SPI completion;
		 * that will clean things up neatly
		 */
		if (!ts->pending)
			mod_timer(&ts->timer, jiffies);

		while (ts->pendown || ts->pending) {
			spin_unlock_irqrestore(&ts->lock, flags);
			udelay(10);
			spin_lock_irqsave(&ts->lock, flags);
		}
	}

	/* we know the chip's in lowpower mode since we always
	 * leave it that way after every request
	 */

	spin_unlock_irqrestore(&ts->lock, flags);
	return 0;
}

static int ads7846_resume(struct spi_device *spi)
{
	struct ads7846 *ts = dev_get_drvdata(&spi->dev);

	ts->irq_disabled = 0;
	enable_irq(ts->spi->irq);
	spi->dev.power.power_state = PMSG_ON;
	return 0;
}

static int __devinit ads7846_probe(struct spi_device *spi)
{
	struct ads7846			*ts;
	struct input_dev		*input_dev;
	struct ads7846_platform_data	*pdata = spi->dev.platform_data;
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

	/* We'd set the wordsize to 12 bits ... except that some controllers
	 * will then treat the 8 bit command words as 12 bits (and drop the
	 * four MSBs of the 12 bit result).  Result: inputs must be shifted
	 * to discard the four garbage LSBs.
	 */

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

	ts->model = pdata->model ? : 7846;
	ts->vref_delay_usecs = pdata->vref_delay_usecs ? : 100;
	ts->x_plate_ohms = pdata->x_plate_ohms ? : 400;

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
	INIT_LIST_HEAD(&ts->msg.transfers);
	x = ts->xfer;

	/* y- still on; turn on only y+ (and ADC) */
	ts->read_y = READ_Y;
	x->tx_buf = &ts->read_y;
	x->len = 1;
	spi_message_add_tail(x, &ts->msg);

	x++;
	x->rx_buf = &ts->tc.y;
	x->len = 2;
	spi_message_add_tail(x, &ts->msg);

	/* turn y+ off, x- on; we'll use formula #2 */
	if (ts->model == 7846) {
		x++;
		ts->read_z1 = READ_Z1;
		x->tx_buf = &ts->read_z1;
		x->len = 1;
		spi_message_add_tail(x, &ts->msg);

		x++;
		x->rx_buf = &ts->tc.z1;
		x->len = 2;
		spi_message_add_tail(x, &ts->msg);

		x++;
		ts->read_z2 = READ_Z2;
		x->tx_buf = &ts->read_z2;
		x->len = 1;
		spi_message_add_tail(x, &ts->msg);

		x++;
		x->rx_buf = &ts->tc.z2;
		x->len = 2;
		spi_message_add_tail(x, &ts->msg);
	}

	/* turn y- off, x+ on, then leave in lowpower */
	x++;
	ts->read_x = READ_X;
	x->tx_buf = &ts->read_x;
	x->len = 1;
	spi_message_add_tail(x, &ts->msg);

	x++;
	x->rx_buf = &ts->tc.x;
	x->len = 2;
	CS_CHANGE(*x);
	spi_message_add_tail(x, &ts->msg);

	ts->msg.complete = ads7846_rx;
	ts->msg.context = ts;

	if (request_irq(spi->irq, ads7846_irq,
			SA_SAMPLE_RANDOM | SA_TRIGGER_FALLING,
			spi->dev.bus_id, ts)) {
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

	/* ads7843/7845 don't have temperature sensors, and
	 * use the other sensors a bit differently too
	 */
	if (ts->model == 7846) {
		device_create_file(&spi->dev, &dev_attr_temp0);
		device_create_file(&spi->dev, &dev_attr_temp1);
	}
	if (ts->model != 7845)
		device_create_file(&spi->dev, &dev_attr_vbatt);
	device_create_file(&spi->dev, &dev_attr_vaux);

	err = input_register_device(input_dev);
	if (err)
		goto err_free_irq;

	return 0;

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

	ads7846_suspend(spi, PMSG_SUSPEND);
	free_irq(ts->spi->irq, ts);
	if (ts->irq_disabled)
		enable_irq(ts->spi->irq);

	if (ts->model == 7846) {
		device_remove_file(&spi->dev, &dev_attr_temp0);
		device_remove_file(&spi->dev, &dev_attr_temp1);
	}
	if (ts->model != 7845)
		device_remove_file(&spi->dev, &dev_attr_vbatt);
	device_remove_file(&spi->dev, &dev_attr_vaux);

	input_unregister_device(ts->input);
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
