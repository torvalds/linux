/*
 * drivers/input/touchscreen/xpt2046_ts.c - driver for rk29 spi xpt2046 device and console
 *
 * Copyright (C) 2011 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
 
#include <linux/hwmon.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <asm/irq.h>
#include <mach/iomux.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include "xpt2046_tslib_ts.h"
#include "ts_lib/tslib.h"
/*
 * This code has been heavily tested on a Nokia 770, and lightly
 * tested on other xpt2046 devices (OSK/Mistral, Lubbock).
 * TSC2046 is just newer xpt2046 silicon.
 * Support for ads7843 tested on Atmel at91sam926x-EK.
 * Support for ads7845 has only been stubbed in.
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
#define XPT2046_DEBUG			0
#if XPT2046_DEBUG
	#define xpt2046printk(msg...)	printk(msg);
#else
	#define xpt2046printk(msg...)
#endif

#define TS_POLL_DELAY	(10)	/* ns delay before the first sample */
#define TS_POLL_PERIOD	(20)	/* ns delay between samples */

/* this driver doesn't aim at the peak continuous sample rate */
#define	SAMPLE_BITS	(8 /*cmd*/ + 16 /*sample*/ + 2 /* before, after */)

struct ts_event {
	/* For portability, we can't read 12 bit values using SPI (which
	 * would make the controller deliver them as native byteorder u16
	 * with msbs zeroed).  Instead, we read them as two 8-bit values,
	 * *** WHICH NEED BYTESWAPPING *** and range adjustment.
	 */
	u16	x;
	u16	y;
	int	ignore;
};

/*
 * We allocate this separately to avoid cache line sharing issues when
 * driver is used with DMA-based SPI controllers (like atmel_spi) on
 * systems where main memory is not DMA-coherent (most non-x86 boards).
 */
struct xpt2046_packet {
	u8			read_x, read_y, pwrdown;
	u16			dummy;		/* for the pwrdown read */
	struct ts_event		tc;
};

struct xpt2046 {
	struct input_dev	*input;
	char	phys[32];
	char	name[32];
	char	pendown_iomux_name[IOMUX_NAME_SIZE];	
	struct spi_device	*spi;

	u16		model;
	u16		x_min, x_max;	
	u16		y_min, y_max; 
	u16		debounce_max;
	u16		debounce_tol;
	u16		debounce_rep;
	u16		penirq_recheck_delay_usecs;
	bool	swap_xy;

	struct xpt2046_packet	*packet;

	struct spi_transfer	xfer[18];
	struct spi_message	msg[5];
	struct spi_message	*last_msg;
	int		msg_idx;
	int		read_cnt;
	int		read_rep;
	int		last_read;
	int		pendown_iomux_mode;	
	int 	touch_ad_top;
	int     touch_ad_bottom;
	int 	touch_ad_left;
	int 	touch_ad_right;
	int 	touch_virtualkey_length;
	spinlock_t		lock;
	struct delayed_work      d_work;
	struct tslib_info tslib_info;
	unsigned		pendown:1;	/* P: lock */
	unsigned		pending:1;	/* P: lock */
// FIXME remove "irq_disabled"
	unsigned		irq_disabled:1;	/* P: lock */
	unsigned		disabled:1;
	unsigned		is_suspended:1;

	int			(*filter)(void *data, int data_idx, int *val);
	void		*filter_data;
	void		(*filter_cleanup)(void *data);
	int			(*get_pendown_state)(void);
	int			gpio_pendown;

	void		(*wait_for_sync)(void);
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
};

/* leave chip selected when we're done, for quicker re-select? */
#if	0
#define	CS_CHANGE(xfer)	((xfer).cs_change = 1)
#else
#define	CS_CHANGE(xfer)	((xfer).cs_change = 0)
#endif

/*--------------------------------------------------------------------------*/

/* The xpt2046 has touchscreen and other sensors.
 * Earlier xpt2046 chips are somewhat compatible.
 */
#define	XPT2046_START			(1 << 7)
#define	XPT2046_A2A1A0_d_y		(1 << 4)	/* differential */
#define	XPT2046_A2A1A0_d_z1		(3 << 4)	/* differential */
#define	XPT2046_A2A1A0_d_z2		(4 << 4)	/* differential */
#define	XPT2046_A2A1A0_d_x		(5 << 4)	/* differential */
#define	XPT2046_A2A1A0_temp0	(0 << 4)	/* non-differential */
#define	XPT2046_A2A1A0_vbatt	(2 << 4)	/* non-differential */
#define	XPT2046_A2A1A0_vaux		(6 << 4)	/* non-differential */
#define	XPT2046_A2A1A0_temp1	(7 << 4)	/* non-differential */
#define	XPT2046_8_BIT			(1 << 3)
#define	XPT2046_12_BIT			(0 << 3)
#define	XPT2046_SER				(1 << 2)	/* non-differential */
#define	XPT2046_DFR				(0 << 2)	/* differential */
#define	XPT2046_PD10_PDOWN		(0 << 0)	/* lowpower mode + penirq */
#define	XPT2046_PD10_ADC_ON		(1 << 0)	/* ADC on */
#define	XPT2046_PD10_REF_ON		(2 << 0)	/* vREF on + penirq */
#define	XPT2046_PD10_ALL_ON		(3 << 0)	/* ADC + vREF on */

#define	MAX_12BIT	((1<<12)-1)

/* leave ADC powered up (disables penirq) between differential samples */
#define	READ_12BIT_DFR(x, adc, vref) (XPT2046_START | XPT2046_A2A1A0_d_ ## x \
	| XPT2046_12_BIT | XPT2046_DFR | \
	(adc ? XPT2046_PD10_ADC_ON : 0) | (vref ? XPT2046_PD10_REF_ON : 0))

#define	READ_Y(vref)	(READ_12BIT_DFR(y,  1, vref))
#define	READ_Z1(vref)	(READ_12BIT_DFR(z1, 1, vref))
#define	READ_Z2(vref)	(READ_12BIT_DFR(z2, 1, vref))

#define	READ_X(vref)	(READ_12BIT_DFR(x,  1, vref))
#define	PWRDOWN		(READ_12BIT_DFR(y,  0, 0))	/* LAST */

/* single-ended samples need to first power up reference voltage;
 * we leave both ADC and VREF powered
 */
#define	READ_12BIT_SER(x) (XPT2046_START | XPT2046_A2A1A0_ ## x \
	| XPT2046_12_BIT | XPT2046_SER)

#define	REF_ON	(READ_12BIT_DFR(x, 1, 1))
#define	REF_OFF	(READ_12BIT_DFR(y, 0, 0))

/*--------------------------------------------------------------------------*/
/*
 * touchscreen sensors  use differential conversions.
 */

struct dfr_req {
	u8			command;
	u8			pwrdown;
	u16			dummy;		/* for the pwrdown read */
	__be16			sample;
	struct spi_message	msg;
	struct spi_transfer	xfer[4];
};
static DECLARE_WAIT_QUEUE_HEAD(wq_ts);
static int wq_condition_ts = 1;

static void xpt2046_enable(struct xpt2046 *ts);
static void xpt2046_disable(struct xpt2046 *ts);
static int xpt2046_verifyAndConvert(struct xpt2046 *ts,int adx, int ady,int *x, int *y)
{
	*x = ts->x_max * (ts->touch_ad_left - adx)/(ts->touch_ad_left - ts->touch_ad_right);
    *y = ts->y_max * (ts->touch_ad_top - ady)/(ts->touch_ad_top - ts->touch_ad_bottom);

	xpt2046printk("%s:(%d/%d)\n",__FUNCTION__,*x, *y);
	
	if((*x< ts->x_min) || (*x > ts->x_max))
		return 1;

	if((*y< ts->y_min) || (*y > ts->y_max + ts->touch_virtualkey_length))
		return 1;


	return 0;
}
static int device_suspended(struct device *dev)
{
	struct xpt2046 *ts = dev_get_drvdata(dev);
	return ts->is_suspended || ts->disabled;
}

static int xpt2046_read12_dfr(struct device *dev, unsigned command)
{
	struct spi_device	*spi = to_spi_device(dev);
	struct xpt2046		*ts = dev_get_drvdata(dev);
	struct dfr_req		*req = kzalloc(sizeof *req, GFP_KERNEL);
	int			status;

	if (!req)
		return -ENOMEM;

	spi_message_init(&req->msg);

	/* take sample */
	req->command = (u8) command;
	req->xfer[0].tx_buf = &req->command;
	req->xfer[0].len = 1;
	spi_message_add_tail(&req->xfer[0], &req->msg);

	req->xfer[1].rx_buf = &req->sample;
	req->xfer[1].len = 2;
	spi_message_add_tail(&req->xfer[1], &req->msg);

	/* converter in low power mode & enable PENIRQ */
	req->pwrdown= PWRDOWN;
	req->xfer[2].tx_buf = &req->pwrdown;
	req->xfer[2].len = 1;
	spi_message_add_tail(&req->xfer[2], &req->msg);

	req->xfer[3].rx_buf = &req->dummy;
	req->xfer[3].len = 2;
	CS_CHANGE(req->xfer[3]);
	spi_message_add_tail(&req->xfer[3], &req->msg);

	ts->irq_disabled = 1;
	disable_irq(spi->irq);
	status = spi_sync(spi, &req->msg);
	ts->irq_disabled = 0;
	enable_irq(spi->irq);
	
	if (status == 0) {
		/* on-wire is a must-ignore bit, a BE12 value, then padding */
		status = be16_to_cpu(req->sample);
		status = status >> 3;
		status &= 0x0fff;
		xpt2046printk("***>%s:status=%d\n",__FUNCTION__,status);
	}

	kfree(req);
	return status;
}



/*--------------------------------------------------------------------------*/

static int get_pendown_state(struct xpt2046 *ts)
{
	if (ts->get_pendown_state)
		return ts->get_pendown_state();

	return !gpio_get_value(ts->gpio_pendown);
}

static void null_wait_for_sync(void)
{
	
}

/*
 * PENIRQ only kicks the timer.  The timer only reissues the SPI transfer,
 * to retrieve touchscreen status.
 *
 * The SPI transfer completion callback does the real work.  It reports
 * touchscreen events and reactivates the timer (or IRQ) as appropriate.
 */
static void xpt2046_rx(void *xpt)
{
	/* xpt2046_rx_val() did in-place conversion (including byteswap) from
	 * on-the-wire format as part of debouncing to get stable readings.
	 */
	wq_condition_ts= 1;
	wake_up(&wq_ts);

    return;
}

static int xpt2046_debounce(void *xpt, int data_idx, int *val)
{
	struct xpt2046		*ts = xpt;
	static int average_val[2];
	

	xpt2046printk("***>%s:%d,%d,%d,%d,%d,%d,%d,%d\n",__FUNCTION__,
		data_idx,ts->last_read,
	  ts->read_cnt,ts->debounce_max,
		abs(ts->last_read - *val),ts->debounce_tol,
		ts->read_rep,ts->debounce_rep);
	
	/* discard the first sample. */
	 //on info_it50, the top-left area(1cmx1cm top-left square ) is not responding cause the first sample is invalid, @sep 17th
	if(!ts->read_cnt)
	{
		//udelay(100);
		ts->read_cnt++;
		return XPT2046_FILTER_REPEAT;
	}
	if(*val == 4095 || *val == 0)
	{
		ts->read_cnt = 0;
		ts->last_read = 0;
		memset(average_val,0,sizeof(average_val));
		xpt2046printk("***>%s:*val == 4095 || *val == 0\n",__FUNCTION__);
		return XPT2046_FILTER_IGNORE;
	}
	/* discard the first sample. */
/*	if(!ts->read_cnt)
	{
		ts->read_cnt++;
		return XPT2046_FILTER_REPEAT;
	}
move discard ahead  */

	if (ts->read_cnt==1 || (abs(ts->last_read - *val) > ts->debounce_tol)) {
		/* Start over collecting consistent readings. */
		ts->read_rep = 1;
		average_val[data_idx] = *val;
		/* Repeat it, if this was the first read or the read
		 * wasn't consistent enough. */
		if (ts->read_cnt < ts->debounce_max) {
			ts->last_read = *val;
			ts->read_cnt++;
			return XPT2046_FILTER_REPEAT;
		} else {
			/* Maximum number of debouncing reached and still
			 * not enough number of consistent readings. Abort
			 * the whole sample, repeat it in the next sampling
			 * period.
			 */
			ts->read_cnt = 0;
			ts->last_read = 0;
			memset(average_val,0,sizeof(average_val));
			xpt2046printk("***>%s:XPT2046_FILTER_IGNORE\n",__FUNCTION__);
			return XPT2046_FILTER_IGNORE;
		}
	} 
	else {
		average_val[data_idx] += *val;
		
		if (++ts->read_rep >= ts->debounce_rep) {
			/* Got a good reading for this coordinate,
			 * go for the next one. */
			ts->read_cnt = 0;
			ts->read_rep = 0;
			ts->last_read = 0;
			*val = average_val[data_idx]/(ts->debounce_rep);
			return XPT2046_FILTER_OK;
		} else {
			/* Read more values that are consistent. */
			ts->read_cnt++;
			
			return XPT2046_FILTER_REPEAT;
		}
	}
}

static int xpt2046_no_filter(void *xpt, int data_idx, int *val)
{
	return XPT2046_FILTER_OK;
}

static void xpt2046_rx_val(void *xpt)
{
	struct xpt2046 *ts = xpt;
	struct xpt2046_packet *packet = ts->packet;
	struct spi_message *m;
	struct spi_transfer *t;
	int val;
	int action;
	int status;
	
	m = &ts->msg[ts->msg_idx];
	t = list_entry(m->transfers.prev, struct spi_transfer, transfer_list);

	/* adjust:  on-wire is a must-ignore bit, a BE12 value, then padding;
	 * built from two 8 bit values written msb-first.
	 */
	val = (be16_to_cpup((__be16 *)t->rx_buf) >> 3) & 0x0fff;

	xpt2046printk("***>%s:value=%d\n",__FUNCTION__,val);
	
	action = ts->filter(ts->filter_data, ts->msg_idx, &val);
	switch (action) {
	case XPT2046_FILTER_REPEAT:
		break;
	case XPT2046_FILTER_IGNORE:
		packet->tc.ignore = 1;
		/* Last message will contain xpt2046_rx() as the
		 * completion function.
		 */
		m = ts->last_msg;
		break;
	case XPT2046_FILTER_OK:
		*(u16 *)t->rx_buf = val;
		packet->tc.ignore = 0;
		m = &ts->msg[++ts->msg_idx];
		break;
	default:
		BUG();
	}
	ts->wait_for_sync();
	status = spi_async(ts->spi, m);
	if (status)
		dev_err(&ts->spi->dev, "spi_async --> %d\n",
				status);
}

int xpt2046_get_raw_data(struct tslib_info *info, struct ts_sample *samp, int nr)
{
		
		struct xpt2046 *ts = container_of((void *)info, struct xpt2046, tslib_info);	
		struct xpt2046_packet	*packet = ts->packet;
		int		status = 0;
		memset(samp, 0, sizeof(struct ts_sample)*nr);
		
		wq_condition_ts = 0;
		
		ts->msg_idx = 0;
		ts->wait_for_sync();
		status = spi_async(ts->spi, &ts->msg[0]);
		if (status)
			dev_err(&ts->spi->dev, "spi_async --> %d\n", status);
		
		xpt2046printk("xpt2046_get_raw_data:wait_event \n");
		if(!wait_event_timeout(wq_ts, wq_condition_ts, msecs_to_jiffies(100))) 
		{
			samp->x = 4095;
			samp->y = 4095;
			samp->pressure = 0;
			return 1;
		}
		
		samp->x = packet->tc.x;
		samp->y = packet->tc.y;
		samp->pressure = !(packet->tc.ignore);
		//xpt2046printk("xpt2046_get_raw_data:samp->x=%d,samp->y=%d,samp->pressure=%d \n",
			//samp->x,samp->y,samp->pressure);
		return 1;
}

static void xpt2046_report(struct xpt2046	*ts)
{
	struct ts_sample samp[3];	
	struct xpt2046_packet	*packet = ts->packet;
	int ret,Rt=1;
	int x = 0,y =0,z = 0;       
	
	ret = dejitter_read(&ts->tslib_info, samp, 1);

	xpt2046printk("***>%s:ret=%d,samp[0].x=%d,samp[0].y=%d,samp[0].pressure=%d\n",
		__FUNCTION__,ret,samp[0].x,samp[0].y,samp[0].pressure);
	if(ret == 1){
		x = samp[0].x;
		y = samp[0].y;
		z = samp[0].pressure;		
	}
	else if(ret == 2){
		x = (samp[0].x + samp[1].x) / 2;
		y = (samp[0].y + samp[1].y) / 2;
		z = (samp[0].pressure + samp[1].pressure) / 2;
	}
	else if(ret ==3){
		x = (samp[0].x + samp[1].x + samp[2].x)/3;
		y = (samp[0].y + samp[1].y + samp[2].y)/3;
		z = (samp[0].pressure + samp[1].pressure + samp[2].pressure)/3;
	}
	else{
		printk("no sampe\n");
		goto out;
	}

	if(z == 0)
	{
		xpt2046printk("***>%s:z == 0\n",__FUNCTION__);
		goto out;
	}

	/* range filtering */
	if (x == MAX_12BIT)
		x = 0;

	/* Sample found inconsistent by debouncing or pressure is beyond
	 * the maximum. Don't report it to user space, repeat at least
	 * once more the measurement
	 */
	if (packet->tc.ignore) {
		xpt2046printk("***>%s:ignored=%d\n",__FUNCTION__,packet->tc.ignore);
		schedule_delayed_work(&ts->d_work, msecs_to_jiffies(TS_POLL_PERIOD));
		return;
	}

	/* Maybe check the pendown state before reporting. This discards
	 * false readings when the pen is lifted.
	 */
	if (ts->penirq_recheck_delay_usecs) {
		udelay(ts->penirq_recheck_delay_usecs);
		if (!get_pendown_state(ts))
		{
			xpt2046printk("***>%s:get_pendown_state(ts)==0,discard false reading\n",__FUNCTION__);
			Rt = 0;
		}
	}

	/* NOTE: We can't rely on the pressure to determine the pen down
	 * state, even this controller has a pressure sensor.  The pressure
	 * value can fluctuate for quite a while after lifting the pen and
	 * in some cases may not even settle at the expected value.
	 *
	 * The only safe way to check for the pen up condition is in the
	 * timer by reading the pen signal state (it's a GPIO _and_ IRQ).
	 */
	if (Rt) {
		struct input_dev *input = ts->input;
		
		if (ts->swap_xy)
			swap(x, y);	
		if(xpt2046_verifyAndConvert(ts,x,y,&x,&y))
		{
			xpt2046printk("***>%s:xpt2046_verifyAndConvert fail\n",__FUNCTION__);
			goto out;
		}
		
		if (!ts->pendown) {
			input_report_key(input, BTN_TOUCH, 1);
			ts->pendown = 1;
			xpt2046printk("***>%s:input_report_key(pen down)\n",__FUNCTION__);
		}
		
		input_report_abs(input, ABS_X, x);
		input_report_abs(input, ABS_Y, y);

		input_sync(input);
		xpt2046printk("***>%s:input_report_abs(%4d/%4d)\n",__FUNCTION__,x, y);
	}
	
out:
	schedule_delayed_work(&ts->d_work, msecs_to_jiffies(TS_POLL_PERIOD));

	return;
}

static void xpt2046_dwork_handler(struct work_struct *work)
{
	struct xpt2046	*ts = (struct xpt2046 *)container_of(work, struct xpt2046, d_work.work);
	
	spin_lock(&ts->lock);

	if (unlikely(!get_pendown_state(ts) ||
		     device_suspended(&ts->spi->dev))) {
		if (ts->pendown) {
			struct input_dev *input = ts->input;
			variance_clear(&ts->tslib_info);
			input_report_key(input, BTN_TOUCH, 0);
			input_sync(input);

			ts->pendown = 0;
			
			xpt2046printk("***>%s:input_report_key(The touchscreen up)\n",__FUNCTION__);
		}

		/* measurement cycle ended */
		if (!device_suspended(&ts->spi->dev)) {
			xpt2046printk("***>%s:device_suspended==0\n",__FUNCTION__);
			ts->irq_disabled = 0;
			enable_irq(ts->spi->irq);
		}
		ts->pending = 0;
	} else {
		/* pen is still down, continue with the measurement */
		xpt2046printk("***>%s:pen is still down, continue with the measurement\n",__FUNCTION__);
		spin_unlock(&ts->lock);
		xpt2046_report(ts);
		return;
	}

	spin_unlock(&ts->lock);
	return;
}
static irqreturn_t xpt2046_irq(int irq, void *handle)
{
	struct xpt2046 *ts = handle;
	unsigned long flags;
	
	xpt2046printk("***>%s.....%s.....%d\n",__FILE__,__FUNCTION__,__LINE__);
	
	spin_lock_irqsave(&ts->lock, flags);

	if (likely(get_pendown_state(ts))) {
		if (!ts->irq_disabled) {
			/* The ARM do_simple_IRQ() dispatcher doesn't act
			 * like the other dispatchers:  it will report IRQs
			 * even after they've been disabled.  We work around
			 * that here.  (The "generic irq" framework may help...)
			 */
			ts->irq_disabled = 1;
			disable_irq_nosync(ts->spi->irq);
			ts->pending = 1;
			schedule_delayed_work(&ts->d_work, msecs_to_jiffies(TS_POLL_DELAY));
		}
	}
	spin_unlock_irqrestore(&ts->lock, flags);

	return IRQ_HANDLED;
}

/*--------------------------------------------------------------------------*/

/* Must be called with ts->lock held */
static void xpt2046_disable(struct xpt2046 *ts)
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
static void xpt2046_enable(struct xpt2046 *ts)
{
	if (!ts->disabled)
		return;

	ts->disabled = 0;
	ts->irq_disabled = 0;
	enable_irq(ts->spi->irq);
}

static int xpt2046_pSuspend(struct xpt2046 *ts)
{
	spin_lock_irq(&ts->lock);

	ts->is_suspended = 1;
	xpt2046_disable(ts);
		
	spin_unlock_irq(&ts->lock);

	return 0;
}

static int xpt2046_pResume(struct xpt2046 *ts)
{
	spin_lock_irq(&ts->lock);

	ts->is_suspended = 0;
	xpt2046_enable(ts);

	spin_unlock_irq(&ts->lock);

	return 0;
}

#if !defined(CONFIG_HAS_EARLYSUSPEND)
static int xpt2046_suspend(struct spi_device *spi, pm_message_t message)
{
	struct xpt2046 *ts = dev_get_drvdata(&spi->dev);
	
	printk("xpt2046_suspend\n");
	
	xpt2046_pSuspend(ts);
	
	return 0;
}

static int xpt2046_resume(struct spi_device *spi)
{
	struct xpt2046 *ts = dev_get_drvdata(&spi->dev);
	
	printk("xpt2046_resume\n");
	
	xpt2046_pResume(ts);

	return 0;
}

#elif defined(CONFIG_HAS_EARLYSUSPEND)
static void xpt2046_early_suspend(struct early_suspend *h)
{
	struct xpt2046	*ts;
    ts = container_of(h, struct xpt2046, early_suspend);
	
	printk("xpt2046_suspend early\n");
	
	xpt2046_pSuspend(ts);
	
	return;
}

static void xpt2046_late_resume(struct early_suspend *h)
{
	struct xpt2046	*ts;
    ts = container_of(h, struct xpt2046, early_suspend);
	
	printk("xpt2046_resume late\n");
	
	xpt2046_pResume(ts);

	return;
}
#endif

static int __devinit setup_pendown(struct spi_device *spi, struct xpt2046 *ts)
{
	struct xpt2046_platform_data *pdata = spi->dev.platform_data;
	int err;

	/* REVISIT when the irq can be triggered active-low, or if for some
	 * reason the touchscreen isn't hooked up, we don't need to access
	 * the pendown state.
	 */
	if (!pdata->get_pendown_state && !gpio_is_valid(pdata->gpio_pendown)) {
		dev_err(&spi->dev, "no get_pendown_state nor gpio_pendown?\n");
		return -EINVAL;
	}

	if (pdata->get_pendown_state) {
		ts->get_pendown_state = pdata->get_pendown_state;
		return 0;
	}
	
    if (pdata->io_init) {
        err = pdata->io_init();
        if (err)
            dev_err(&spi->dev, "xpt2046 io_init fail\n");
    }
	
	ts->gpio_pendown = pdata->gpio_pendown;
	strcpy(ts->pendown_iomux_name,pdata->pendown_iomux_name);
	ts->pendown_iomux_mode = pdata->pendown_iomux_mode;
	
    rk29_mux_api_set(ts->pendown_iomux_name,pdata->pendown_iomux_mode);
	err = gpio_request(pdata->gpio_pendown, "xpt2046_pendown");
	if (err) {
		dev_err(&spi->dev, "failed to request pendown GPIO%d\n",
				pdata->gpio_pendown);
		return err;
	}
	
	err = gpio_pull_updown(pdata->gpio_pendown, GPIOPullUp);
	if (err) {
		dev_err(&spi->dev, "failed to pullup pendown GPIO%d\n",
				pdata->gpio_pendown);
		return err;
	}
	ts->gpio_pendown = pdata->gpio_pendown;
	return 0;
}

static int __devinit xpt2046_probe(struct spi_device *spi)
{
	struct xpt2046			*ts;
	struct xpt2046_packet		*packet;
	struct input_dev		*input_dev;
	struct xpt2046_platform_data	*pdata = spi->dev.platform_data;
	struct spi_message		*m;
	struct spi_transfer		*x;
	int				vref;
	int				err;
	
	if (!spi->irq) {
		dev_dbg(&spi->dev, "no IRQ?\n");
		return -ENODEV;
	}
	else{
		spi->irq = gpio_to_irq(spi->irq);
		dev_dbg(&spi->dev, "no IRQ?\n");
	}
	    
    if (!pdata) {
		dev_err(&spi->dev, "empty platform_data\n");
		return -EFAULT;
    }
    
	/* don't exceed max specified sample rate */
	if (spi->max_speed_hz > (125000 * SAMPLE_BITS)) {
		dev_dbg(&spi->dev, "f(sample) %d KHz?\n",
				(spi->max_speed_hz/SAMPLE_BITS)/1000);
		return -EINVAL;
	}

	/* We'd set TX wordsize 8 bits and RX wordsize to 13 bits ... except
	 * that even if the hardware can do that, the SPI controller driver
	 * may not.  So we stick to very-portable 8 bit words, both RX and TX.
	 */
	spi->bits_per_word = 8;
	spi->mode = SPI_MODE_0;
	err = spi_setup(spi);
	if (err < 0)
		return err;

	ts = kzalloc(sizeof(struct xpt2046), GFP_KERNEL);
	packet = kzalloc(sizeof(struct xpt2046_packet), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!ts || !packet || !input_dev) {
		err = -ENOMEM;
		goto err_free_mem;
	}

	dev_set_drvdata(&spi->dev, ts);

	ts->packet = packet;
	ts->spi = spi;
	ts->input = input_dev;
	ts->swap_xy = pdata->swap_xy;
	
	INIT_DELAYED_WORK(&ts->d_work, xpt2046_dwork_handler);
	
	tslib_init(&ts->tslib_info, xpt2046_get_raw_data);

	spin_lock_init(&ts->lock);

	ts->model = pdata->model ? : 2046;

	if (pdata->filter != NULL) {
		if (pdata->filter_init != NULL) {
			err = pdata->filter_init(pdata, &ts->filter_data);
			if (err < 0)
				goto err_free_mem;
		}
		ts->filter = pdata->filter;
		ts->filter_cleanup = pdata->filter_cleanup;
	} else if (pdata->debounce_max) {
		ts->debounce_max = pdata->debounce_max;
		if (ts->debounce_max < pdata->debounce_rep)
			ts->debounce_max = pdata->debounce_rep;
		ts->debounce_tol = pdata->debounce_tol;
		ts->debounce_rep = pdata->debounce_rep;
		ts->filter = xpt2046_debounce;
		ts->filter_data = ts;
	} else
		ts->filter = xpt2046_no_filter;

	err = setup_pendown(spi, ts);
	if (err)
		goto err_cleanup_filter;

	if (pdata->penirq_recheck_delay_usecs)
		ts->penirq_recheck_delay_usecs =
				pdata->penirq_recheck_delay_usecs;

	ts->wait_for_sync = pdata->wait_for_sync ? : null_wait_for_sync;
	ts->x_min = pdata->x_min;
	ts->x_max = pdata->x_max;
	ts->y_min = pdata->y_min;
	ts->y_max = pdata->y_max;
	
	ts->touch_ad_top = pdata->touch_ad_top;
	ts->touch_ad_bottom= pdata->touch_ad_bottom;
	ts->touch_ad_left= pdata->touch_ad_left;
	ts->touch_ad_right= pdata->touch_ad_right;
	
	ts->touch_virtualkey_length = pdata->touch_virtualkey_length;
	
	snprintf(ts->phys, sizeof(ts->phys), "%s/input0", dev_name(&spi->dev));
	snprintf(ts->name, sizeof(ts->name), "xpt%d-touchscreen", ts->model);

	input_dev->name = ts->name;
	input_dev->phys = ts->phys;
	input_dev->dev.parent = &spi->dev;

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	input_set_abs_params(input_dev, ABS_X,
			ts->x_min ? : 0,
			ts->x_max ? : MAX_12BIT,
			0, 0);
	input_set_abs_params(input_dev, ABS_Y,
			ts->y_min ? : 0,
			ts->y_max ? : MAX_12BIT,
			0, 0);
	
	vref = pdata->keep_vref_on;

	/* set up the transfers to read touchscreen state; this assumes we
	 * use formula #2 for pressure, not #3.
	 */
	m = &ts->msg[0];
	x = ts->xfer;

	spi_message_init(m);

	/* y- still on; turn on only y+ (and ADC) */
	packet->read_y = READ_Y(vref);
	x->tx_buf = &packet->read_y;
	x->len = 1;
	spi_message_add_tail(x, m);

	x++;
	x->rx_buf = &packet->tc.y;
	x->len = 2;
	spi_message_add_tail(x, m);

	m->complete = xpt2046_rx_val;
	m->context = ts;

	m++;
	spi_message_init(m);

	/* turn y- off, x+ on, then leave in lowpower */
	x++;
	packet->read_x = READ_X(vref);
	x->tx_buf = &packet->read_x;
	x->len = 1;
	spi_message_add_tail(x, m);

	x++;
	x->rx_buf = &packet->tc.x;
	x->len = 2;
	spi_message_add_tail(x, m);

	m->complete = xpt2046_rx_val;
	m->context = ts;

	/* power down */
	m++;
	spi_message_init(m);

	x++;
	packet->pwrdown = PWRDOWN;
	x->tx_buf = &packet->pwrdown;
	x->len = 1;
	spi_message_add_tail(x, m);

	x++;
	x->rx_buf = &packet->dummy;
	x->len = 2;
	CS_CHANGE(*x);
	spi_message_add_tail(x, m);

	m->complete = xpt2046_rx;
	m->context = ts;

	ts->last_msg = m;

	if (request_irq(spi->irq, xpt2046_irq, IRQF_TRIGGER_FALLING,
			spi->dev.driver->name, ts)) {
		xpt2046printk("%s:trying pin change workaround on irq %d\n",__FUNCTION__,spi->irq);
		err = request_irq(spi->irq, xpt2046_irq,
				  IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
				  spi->dev.driver->name, ts);
		if (err) {
			dev_dbg(&spi->dev, "irq %d busy?\n", spi->irq);
			goto err_free_gpio;
		}
	}
	xpt2046printk("***>%s:touchscreen irq %d\n",__FUNCTION__,spi->irq);
	
	/* take a first sample, leaving nPENIRQ active and vREF off; avoid
	 * the touchscreen, in case it's not connected.
	 */
	xpt2046_read12_dfr(&spi->dev,READ_X(1));

	err = input_register_device(input_dev);
	if (err)
		goto err_remove_attr_group;
	
#ifdef CONFIG_HAS_EARLYSUSPEND
	ts->early_suspend.suspend = xpt2046_early_suspend;
	ts->early_suspend.resume = xpt2046_late_resume;
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	register_early_suspend(&ts->early_suspend);
#endif

    xpt2046printk("xpt2046_ts: driver initialized\n");
	return 0;

 err_remove_attr_group:
	free_irq(spi->irq, ts);
 err_free_gpio:
	if (ts->gpio_pendown != -1)
		gpio_free(ts->gpio_pendown);
 err_cleanup_filter:
	if (ts->filter_cleanup)
		ts->filter_cleanup(ts->filter_data);
 err_free_mem:
	input_free_device(input_dev);
	kfree(packet);
	kfree(ts);
	return err;
}

static int __devexit xpt2046_remove(struct spi_device *spi)
{
	struct xpt2046		*ts = dev_get_drvdata(&spi->dev);

	input_unregister_device(ts->input);
	
#if !defined(CONFIG_HAS_EARLYSUSPEND)
	xpt2046_suspend(spi, PMSG_SUSPEND);
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	xpt2046_early_suspend(&ts->early_suspend);
	unregister_early_suspend(&ts->early_suspend);
#endif

	free_irq(ts->spi->irq, ts);
	/* suspend left the IRQ disabled */
	enable_irq(ts->spi->irq);

	if (ts->gpio_pendown != -1)
		gpio_free(ts->gpio_pendown);

	if (ts->filter_cleanup)
		ts->filter_cleanup(ts->filter_data);

	kfree(ts->packet);
	kfree(ts);

	dev_dbg(&spi->dev, "unregistered touchscreen\n");
	return 0;
}

static struct spi_driver xpt2046_driver = {
	.driver = {
		.name	= "xpt2046_ts",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe		= xpt2046_probe,
	.remove		= __devexit_p(xpt2046_remove),
#ifndef CONFIG_HAS_EARLYSUSPEND
	.suspend	= xpt2046_suspend,
	.resume		= xpt2046_resume,
#endif
};

static int __init xpt2046_init(void)
{
	return spi_register_driver(&xpt2046_driver);
}
module_init(xpt2046_init);

static void __exit xpt2046_exit(void)
{
	spi_unregister_driver(&xpt2046_driver);
}
module_exit(xpt2046_exit);

MODULE_DESCRIPTION("rk29xx spi xpt2046 TouchScreen Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:xpt2046");
