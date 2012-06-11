/*
 * drivers/i2c/chips/twl6030-gpadc.c
 *
 * TWL6030 GPADC module driver
 *
 * Copyright (C) 2009 Texas Instruments Inc.
 * Nishant Kamat <nskamat@ti.com>
 *
 * Based on twl4030-madc.c
 * Copyright (C) 2008 Nokia Corporation
 * Mikko Ylinen <mikko.k.ylinen@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
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

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/hwmon-sysfs.h>
#include <linux/i2c/twl.h>
#include <linux/i2c/twl6030-gpadc.h>

#include <linux/uaccess.h>

#define TWL6030_GPADC_PFX	"twl6030-gpadc: "
#define ENABLE_GPADC	0x02
#define REG_TOGGLE1	0x90
#define GPADCS		(1 << 1)
#define GPADCR		(1 << 0)

#define SCALE				(1 << 15)

struct twl6030_chnl_calib {
	s32 gain_error;
	s32 offset_error;
};

struct twl6030_ideal_code {
	s16 code1;
	s16 code2;
};

struct twl6032_chnl_calib {
	s32 gain;
	s32 gain_error;
	s32 offset_error;
};

struct twl6032_ideal_code {
	s16 code1;
	s16 code2;
	s16 v1;
	s16 v2;
};

static struct twl6030_chnl_calib
	twl6030_calib_tbl[GPADC_MAX_CHANNELS];
static const u32 calibration_bit_map = 0x47FF;

/* Trim address where measured offset from ideal code is stored */
static const u8 twl6030_trim_addr[GPADC_MAX_CHANNELS] = {
	0xCD, /* CHANNEL 0 */
	0xD1, /* CHANNEL 1 */
	0xD9, /* CHANNEL 2 */
	0xD1, /* CHANNEL 3 */
	0xD1, /* CHANNEL 4 */
	0xD1, /* CHANNEL 5 */
	0xD1, /* CHANNEL 6 */
	0xD3, /* CHANNEL 7 */
	0xCF, /* CHANNEL 8 */
	0xD5, /* CHANNEL 9 */
	0xD7, /* CHANNEL 10 */
	0x00, /* CHANNEL 11 */
	0x00, /* CHANNEL 12 */
	0x00, /* CHANNEL 13 */
	0xDB, /* CHANNEL 14 */
	0x00, /* CHANNEL 15 */
	0x00, /* CHANNEL 16 */
};

#define TWL6032_GPADC_TRIM1	0xCD
#define TWL6032_GPADC_TRIM2	0xCE
#define TWL6032_GPADC_TRIM3	0xCF
#define TWL6032_GPADC_TRIM4	0xD0
#define TWL6032_GPADC_TRIM5	0xD1
#define TWL6032_GPADC_TRIM6	0xD2
#define TWL6032_GPADC_TRIM7	0xD3
#define TWL6032_GPADC_TRIM8	0xD4
#define TWL6032_GPADC_TRIM9	0xD5
#define TWL6032_GPADC_TRIM10	0xD6
#define TWL6032_GPADC_TRIM11	0xD7
#define TWL6032_GPADC_TRIM12	0xD8
#define TWL6032_GPADC_TRIM13	0xD9
#define TWL6032_GPADC_TRIM14	0xDA
#define TWL6032_GPADC_TRIM15	0xDB
#define TWL6032_GPADC_TRIM16	0xDC
#define TWL6032_GPADC_TRIM19	0xFD

/*
 * actual scaler gain is multiplied by 8 for fixed point operation
 * 1.875 * 8 = 15
 * For channels 0, 1, 3, 4, 5, 6, 12, 13
 * 1.25 * 8 = 10
 * is used, as scaler is Vref * divider
 * Vref = 1.25
 */
static const u16 twl6030_gain[TWL6030_GPADC_MAX_CHANNELS] = {
	10,	/* CHANNEL 0 */
	10,	/* CHANNEL 1 */

	/* 1.875 */
	15,	/* CHANNEL 2 */

	10,	/* CHANNEL 3 */
	10,	/* CHANNEL 4 */
	10,	/* CHANNEL 5 */
	10,	/* CHANNEL 6 */

	/* 5 */
	40,	/* CHANNEL 7 */

	/* 6.25 */
	50,	/* CHANNEL 8 */

	/* 11.25 */
	90,	/* CHANNEL 9 */

	/* 6.875 */
	55,	/* CHANNEL 10 */

	/* 1.875 */
	15,	/* CHANNEL 11 */

	10,	/* CHANNEL 12 */
	10,	/* CHANNEL 13 */

	/* 6.875 */
	55,	/* CHANNEL 14 */

	/* 6.25 */
	50,	/* CHANNEL 15 */

	/* 4.75 */
	38,	/* CHANNEL 16 */
};

/*
 * calibration not needed for channel 11, 12, 13, 15 and 16
 * calibration offset is same for channel 1, 3, 4, 5
 */
static const struct twl6030_ideal_code
	twl6030_ideal[GPADC_MAX_CHANNELS] = {
	{116,	745},	/* CHANNEL 0 */
	{82,	900},	/* CHANNEL 1 */
	{55,	818},	/* CHANNEL 2 */
	{82,	900},	/* CHANNEL 3 */
	{82,	900},	/* CHANNEL 4 */
	{82,	900},	/* CHANNEL 5 */
	{82,	900},	/* CHANNEL 6 */
	{614,	941},	/* CHANNEL 7 */
	{82,	688},	/* CHANNEL 8 */
	{182,	818},	/* CHANNEL 9 */
	{149,	818},	/* CHANNEL 10 */
	{0,	0},	/* CHANNEL 11 */
	{0,	0},	/* CHANNEL 12 */
	{0,	0},	/* CHANNEL 13 */
	{48,	714},	/* CHANNEL 14 */
	{0,	0},	/* CHANNEL 15 */
	{0,	0},	/* CHANNEL 16 */
};

/* PhoenixLite has a different calibration sysem to the Phoenix */
static const struct twl6032_ideal_code
			twl6032_ideal[GPADC_MAX_CHANNELS] = {
	{	/* CHANNEL 0 */
		.code1 = 1441,
		.code2 = 3276,
		.v1 = 440,
		.v2 = 1000,
	},
	{	/* CHANNEL 1 */
		.code1 = 1441,
		.code2 = 3276,
		.v1 = 440,
		.v2 = 1000,
	},
	{	/* CHANNEL 2 */
		.code1 = 1441,
		.code2 = 3276,
		.v1 = 660,
		.v2 = 1500,
	},
	{	/* CHANNEL 3 */
		.code1 = 1441,
		.code2 = 3276,
		.v1 = 440,
		.v2 = 1000,
	},
	{	/* CHANNEL 4 */
		.code1 = 1441,
		.code2 = 3276,
		.v1 = 440,
		.v2 = 1000,
	},
	{	/* CHANNEL 5 */
		.code1 = 1441,
		.code2 = 3276,
		.v1 = 440,
		.v2 = 1000,
	},
	{	/* CHANNEL 6 */
		.code1 = 1441,
		.code2 = 3276,
		.v1 = 440,
		.v2 = 1000,
	},
	{	/* CHANNEL 7 */
		.code1 = 1441,
		.code2 = 3276,
		.v1 = 2200,
		.v2 = 5000,
	},
	{	/* CHANNEL 8 */
		.code1 = 1441,
		.code2 = 3276,
		.v1 = 2200,
		.v2 = 5000,
	},
	{	/* CHANNEL 9 */
		.code1 = 1441,
		.code2 = 3276,
		.v1 = 3960,
		.v2 = 9000,
	},
	{	/* CHANNEL 10 */
		.code1 = 150,
		.code2 = 751,
		.v1 = 1000,
		.v2 = 5000,
	},
	{	/* CHANNEL 11 */
		.code1 = 1441,
		.code2 = 3276,
		.v1 = 660,
		.v2 = 1500,
	},
	{	/* CHANNEL 12 */
		.code1 = 1441,
		.code2 = 3276,
		.v1 = 440,
		.v2 = 1000,
	},
	{	/* CHANNEL 13 */
		.code1 = 1441,
		.code2 = 3276,
		.v1 = 440,
		.v2 = 1000,
	},
	{	/* CHANNEL 14 */
		.code1 = 1441,
		.code2 = 3276,
		.v1 = 2420,
		.v2 = 5500,
	},
	{},	/* CHANNEL 15 - UNUSED */
	{},	/* CHANNEL 16 - UNUSED */
	{},	/* CHANNEL 17 - UNUSED */
	{	/* CHANNEL 18 */
		.code1 = 1441,
		.code2 = 3276,
		.v1 = 2200,
		.v2 = 5000,
	},
};


struct twl6030_gpadc_data {
	struct device		*dev;
	struct mutex		lock;
	struct work_struct	ws;
	struct twl6030_gpadc_request	requests[TWL6030_GPADC_NUM_METHODS];
	int irq_n;
	struct twl6032_chnl_calib *twl6032_cal_tbl;
	unsigned long features;
};

static struct twl6030_gpadc_data *the_gpadc;

static const
struct twl6030_gpadc_conversion_method twl6030_conversion_methods_table[] = {
	[TWL6030_GPADC_RT] = {
		.sel	= TWL6030_GPADC_RTSELECT_LSB,
		.rbase	= TWL6030_GPADC_RTCH0_LSB,
		.mask	= TWL6030_GPADC_RT_SW1_EOC_MASK,
	},
	/*
	 * TWL6030_GPADC_SW1 is not supported as
	 * interrupt from RT and SW1 cannot be differentiated
	 */
	[TWL6030_GPADC_SW2] = {
		.rbase	= TWL6030_GPADC_GPCH0_LSB,
		.ctrl	= TWL6030_GPADC_CTRL_P2,
		.enable = TWL6030_GPADC_CTRL_P2_SP2,
		.mask	= TWL6030_GPADC_SW2_EOC_MASK,
	},
};

static const
struct twl6030_gpadc_conversion_method twl6032_conversion_methods_table[] = {
	[TWL6030_GPADC_RT] = {
		.sel	= TWL6032_GPADC_RTSELECT_LSB,
		.rbase	= TWL6032_RTCH0_LSB,
		.mask	= TWL6032_GPADC_RT_EOC_MASK,
	},
	[TWL6030_GPADC_SW2] = {
		.sel	= TWL6032_GPADC_GPSELECT_ISB,
		.rbase	= TWL6032_GPCH0_LSB,
		.ctrl	= TWL6032_GPADC_CTRL_P1,
		.enable = TWL6030_GPADC_CTRL_P1_SP1,
		.mask	= TWL6032_GPADC_SW_EOC_MASK,
	},
};

static const
struct twl6030_gpadc_conversion_method *twl6030_conversion_methods;

static ssize_t show_gain(struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int value;
	int status;

	value = twl6030_calib_tbl[attr->index].gain_error;

	status = sprintf(buf, "%d\n", value);
	return status;
}

static ssize_t set_gain(struct device *dev,
	struct device_attribute *devattr, const char *buf, size_t count)
{
	long val;
	int status = count;
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);

	if ((strict_strtol(buf, 10, &val) < 0) || (val < 15000)
							|| (val > 60000))
		return -EINVAL;

	twl6030_calib_tbl[attr->index].gain_error = val;

	return status;
}

static ssize_t show_offset(struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int value;
	int status;

	value = twl6030_calib_tbl[attr->index].offset_error;

	status = sprintf(buf, "%d\n", value);
	return status;
}

static ssize_t set_offset(struct device *dev,
	struct device_attribute *devattr, const char *buf, size_t count)
{
	long val;
	int status = count;
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);

	if ((strict_strtol(buf, 10, &val) < 0) || (val < 15000)
							|| (val > 60000))
		return -EINVAL;

	twl6030_calib_tbl[attr->index].offset_error = val;

	return status;
}

static int twl6030_gpadc_read(struct twl6030_gpadc_data *gpadc, u8 reg)
{
	int ret;
	u8 val = 0;

	ret = twl_i2c_read_u8(TWL_MODULE_MADC, &val, reg);
	if (ret) {
		dev_dbg(gpadc->dev, "unable to read register 0x%X\n", reg);
		return ret;
	}

	return val;
}

static void twl6030_gpadc_write(struct twl6030_gpadc_data *gpadc,
				u8 reg, u8 val)
{
	int ret;

	ret = twl_i2c_write_u8(TWL_MODULE_MADC, val, reg);
	if (ret)
		dev_err(gpadc->dev, "unable to write register 0x%X\n", reg);
}

static int twl6030_gpadc_channel_raw_read(struct twl6030_gpadc_data *gpadc,
					  u8 reg)
{
	u8 msb, lsb;

	/* For each ADC channel, we have MSB and LSB register pair.
	 * MSB address is always LSB address+1. reg parameter is the
	 * addr of LSB register
	 */
	msb = twl6030_gpadc_read(gpadc, reg + 1);
	lsb = twl6030_gpadc_read(gpadc, reg);
	return (int)((msb << 8) | lsb);
}

static int twl6030_gpadc_read_channels(struct twl6030_gpadc_data *gpadc,
		u8 reg_base, u32 channels, struct twl6030_gpadc_request *req)
{
	int count = 0;
	u8 reg, i;
	s32 gain_error;
	s32 offset_error;
	s32 raw_code;
	s32 corrected_code;
	s32 raw_channel_value;

	channels = ~channels;
	if (gpadc->features & TWL6032_SUBCLASS) {
		for (i = 0; i < TWL6032_GPADC_MAX_CHANNELS; i++) {
			if (channels & BIT(i))
				continue;

			reg = reg_base + 2 * count;

			dev_dbg(gpadc->dev, "GPADC chn: %d\n", i);
			raw_code = twl6030_gpadc_channel_raw_read(gpadc, reg);
			dev_dbg(gpadc->dev, "GPADC raw: %d\n", raw_code);
			count++;
			req->buf[i].raw_code = raw_code;

			/* No correction for channels 15-17 */
			if (unlikely((i >= 15) && (i <= 17))) {
				raw_channel_value = raw_code;
				req->buf[i].code = raw_code;
				req->rbuf[i] = raw_code;
			} else {
				raw_channel_value = (raw_code *
					gpadc->twl6032_cal_tbl[i].gain);

				/* Shift back into mV range */
				raw_channel_value /= 1000;

				req->buf[i].code = corrected_code =
				((raw_code * 1000) -
				gpadc->twl6032_cal_tbl[i].offset_error) /
				gpadc->twl6032_cal_tbl[i].gain_error;

				dev_dbg(gpadc->dev, "GPADC cor: %d\n",
					corrected_code);

				req->rbuf[i] = corrected_code *
					gpadc->twl6032_cal_tbl[i].gain;

				/* Shift back into mV range */
				req->rbuf[i] /= 1000;
			}
			req->buf[i].raw_channel_value = raw_channel_value;
			dev_dbg(gpadc->dev, "GPADC val: %d\n", req->rbuf[i]);
		}
	} else {
		for (i = 0; i < TWL6030_GPADC_MAX_CHANNELS; i++) {
			if (channels & BIT(i))
				continue;
			reg = reg_base + 2 * i;
			raw_code = twl6030_gpadc_channel_raw_read(gpadc, reg);
			req->buf[i].raw_code = raw_code;
			count++;
			/*
			 * multiply by 1000 to convert the unit to milli
			 * division by 1024 (>> 10) for 10 bit ADC
			 * division by 8 (>> 3) for actual scaler gain
			 */
			raw_channel_value = (raw_code * twl6030_gain[i]
								* 1000) >> 13;
			req->buf[i].raw_channel_value = raw_channel_value;

			if (~calibration_bit_map & BIT(i)) {
				req->buf[i].code = raw_code;
				req->rbuf[i] = raw_channel_value;
			} else {
				gain_error = twl6030_calib_tbl[i].gain_error;
				offset_error = twl6030_calib_tbl[i].offset_error;
				req->buf[i].code = corrected_code =
					(raw_code * SCALE - offset_error) /
						gain_error;
				req->rbuf[i] = (corrected_code * twl6030_gain[i]
								* 1000) >> 13;
			}
			dev_dbg(gpadc->dev, "GPADC val: %d", req->rbuf[i]);
		}
	}
	return count;
}

static void twl6030_gpadc_enable_irq(u16 method)
{
	twl6030_interrupt_unmask(twl6030_conversion_methods[method].mask,
						REG_INT_MSK_LINE_B);
	twl6030_interrupt_unmask(twl6030_conversion_methods[method].mask,
						REG_INT_MSK_STS_B);
}

static void twl6030_gpadc_disable_irq(u16 method)
{
	twl6030_interrupt_mask(twl6030_conversion_methods[method].mask,
						REG_INT_MSK_LINE_B);
	twl6030_interrupt_mask(twl6030_conversion_methods[method].mask,
						REG_INT_MSK_STS_B);
}

static irqreturn_t twl6030_gpadc_irq_handler(int irq, void *_req)
{
	struct twl6030_gpadc_request *req = _req;

#ifdef CONFIG_LOCKDEP
	/* WORKAROUND for lockdep forcing IRQF_DISABLED on us, which
	 * we don't want and can't tolerate.  Although it might be
	 * friendlier not to borrow this thread context...
	 */
	local_irq_enable();
#endif

	/* Find the cause of the interrupt and enable the pending
	   bit for the corresponding method */
	twl6030_gpadc_disable_irq(req->method);
	req->result_pending = 1;

	schedule_work(&the_gpadc->ws);

	return IRQ_HANDLED;
}

static void twl6030_gpadc_work(struct work_struct *ws)
{
	const struct twl6030_gpadc_conversion_method *method;
	struct twl6030_gpadc_data *gpadc;
	struct twl6030_gpadc_request *r;
	int len, i;

	gpadc = container_of(ws, struct twl6030_gpadc_data, ws);
	mutex_lock(&gpadc->lock);

	for (i = 0; i < TWL6030_GPADC_NUM_METHODS; i++) {

		r = &gpadc->requests[i];

		/* No pending results for this method, move to next one */
		if (!r->result_pending)
			continue;

		method = &twl6030_conversion_methods[r->method];

		/* Read results */
		len = twl6030_gpadc_read_channels(gpadc, method->rbase,
						 r->channels, r);

		/* Return results to caller */
		if (r->func_cb != NULL) {
			r->func_cb(r);
			r->func_cb = NULL;
		}

		/* Free request */
		r->result_pending = 0;
		r->active	  = 0;
	}

	mutex_unlock(&gpadc->lock);
}

static int twl6030_gpadc_set_irq(struct twl6030_gpadc_data *gpadc,
		struct twl6030_gpadc_request *req)
{
	struct twl6030_gpadc_request *p;

	p = &gpadc->requests[req->method];
	p->channels = req->channels;
	p->method = req->method;
	p->func_cb = req->func_cb;
	p->type = req->type;

	twl6030_gpadc_enable_irq(req->method);

	return 0;
}

static inline void
twl6030_gpadc_start_conversion(struct twl6030_gpadc_data *gpadc,
			       int conv_method)
{
	const struct twl6030_gpadc_conversion_method *method;

	method = &twl6030_conversion_methods[conv_method];
	twl_i2c_write_u8(TWL6030_MODULE_ID1, GPADCS, REG_TOGGLE1);

	switch (conv_method) {
	case TWL6030_GPADC_SW2:
		twl6030_gpadc_write(gpadc, method->ctrl, method->enable);
		break;
	case TWL6030_GPADC_RT:
	default:
		break;
	}
}

static int twl6030_gpadc_is_conversion_ready(
		struct twl6030_gpadc_data *gpadc, u8 status_reg)
{
	u8 reg = twl6030_gpadc_read(gpadc, status_reg);
	return !(reg & TWL6030_GPADC_BUSY) && (reg & TWL6030_GPADC_EOC_SW);
}

static int twl6030_gpadc_wait_conversion_ready(
		struct twl6030_gpadc_data *gpadc,
		unsigned int timeout_ms, u8 status_reg)
{
	unsigned long timeout;

	timeout = jiffies + msecs_to_jiffies(timeout_ms);
	do {
		if (twl6030_gpadc_is_conversion_ready(gpadc, status_reg))
			return 0;
		msleep_interruptible(1);
	} while (!time_after(jiffies, timeout));

	/* one more checking against scheduler-caused timeout */
	if (twl6030_gpadc_is_conversion_ready(gpadc, status_reg))
		return 0;
	else
		return -EAGAIN;
}

/* locks held by caller */
static int _twl6030_gpadc_conversion(struct twl6030_gpadc_request *req,
	const struct twl6030_gpadc_conversion_method *method)
{
	u8 ch_msb, ch_lsb, ch_isb;
	int ret = 0;

	if (req->method == TWL6030_GPADC_RT) {
		ch_msb = (req->channels >> 16) & 0x01;
		ch_isb = (req->channels >> 8) & 0xff;
		ch_lsb = req->channels & 0xff;
		twl6030_gpadc_write(the_gpadc, method->sel + 2, ch_msb);
		twl6030_gpadc_write(the_gpadc, method->sel + 1, ch_isb);
		twl6030_gpadc_write(the_gpadc, method->sel, ch_lsb);
	}

	if ((req->type == TWL6030_GPADC_IRQ_ONESHOT) &&
		 (req->func_cb != NULL)) {
		twl6030_gpadc_set_irq(the_gpadc, req);
		twl6030_gpadc_start_conversion(the_gpadc, req->method);
		the_gpadc->requests[req->method].active = 1;
		ret = 0;
		goto out;
	}

	/* With RT method we should not be here anymore */
	if (req->method == TWL6030_GPADC_RT) {
		ret = -EINVAL;
		goto out;
	}

	twl6030_gpadc_start_conversion(the_gpadc, req->method);
	the_gpadc->requests[req->method].active = 1;

	/* Wait until conversion is ready (ctrl register returns EOC) */
	ret = twl6030_gpadc_wait_conversion_ready(the_gpadc, 5, method->ctrl);
	if (ret) {
		dev_dbg(the_gpadc->dev, "conversion timeout!\n");
		the_gpadc->requests[req->method].active = 0;
		goto out;
	}

	ret = twl6030_gpadc_read_channels(the_gpadc, method->rbase,
					  req->channels, req);
	the_gpadc->requests[req->method].active = 0;
out:
	return ret;
}

/* locks held by caller */
static int _twl6032_gpadc_conversion(struct twl6030_gpadc_request *req,
	const struct twl6030_gpadc_conversion_method *method)
{
	int i, ret, count = 0, channelcnt = 0;
	u8 ch_msb, ch_lsb, ch_isb;

	if ((req->type == TWL6030_GPADC_IRQ_ONESHOT) &&
		(req->func_cb == NULL)) {
		ret = -EINVAL;
		goto out;
	}

	for (i = 0; i < TWL6032_GPADC_MAX_CHANNELS; i++)
		if (req->channels & BIT(i))
			channelcnt++;

	if (req->method == TWL6030_GPADC_RT) {
		/*
		 * For the TWL6032 real time conversion
		 * maximum channels count is 2
		 */
		if ((req->type != TWL6030_GPADC_IRQ_ONESHOT) ||
			 (channelcnt > 2)) {
			ret = -EINVAL;
			goto out;
		}

		ch_msb = (req->channels >> 16) & 0x07;
		ch_isb = (req->channels >> 8) & 0xff;
		ch_lsb = req->channels & 0xff;
		twl6030_gpadc_write(the_gpadc, method->sel + 2, ch_msb);
		twl6030_gpadc_write(the_gpadc, method->sel + 1, ch_isb);
		twl6030_gpadc_write(the_gpadc, method->sel, ch_lsb);
	}

	/*
	 * For the TWL6032 Asynchronous Conversion
	 * maximum channels count is 1
	 */
	if ((req->method == TWL6030_GPADC_SW2) &&
		 (req->type == TWL6030_GPADC_IRQ_ONESHOT)) {
		if (channelcnt > 1) {
			ret = -EINVAL;
			goto out;
		}

		for (i = 0; i < TWL6032_GPADC_MAX_CHANNELS; i++) {
			if (!(req->channels & BIT(i)))
				continue;

			/* select the ADC channel to be read */
			twl6030_gpadc_write(the_gpadc, method->sel, i);
		}
	}

	if (req->type == TWL6030_GPADC_IRQ_ONESHOT) {
		twl6030_gpadc_set_irq(the_gpadc, req);
		twl6030_gpadc_start_conversion(the_gpadc, req->method);
		the_gpadc->requests[req->method].active = 1;
		ret = 0;
		goto out;
	}

	for (i = 0; i < TWL6032_GPADC_MAX_CHANNELS; i++) {
		if (!(req->channels & BIT(i)))
			continue;

		/* select the ADC channel to be read */
		twl6030_gpadc_write(the_gpadc, method->sel, i);

		twl6030_gpadc_start_conversion(the_gpadc, req->method);
		the_gpadc->requests[req->method].active = 1;

		/* Wait until conversion is ready (ctrl register is EOC) */
		ret = twl6030_gpadc_wait_conversion_ready(the_gpadc, 5,
				method->ctrl);
		if (ret) {
			dev_dbg(the_gpadc->dev, "conversion timeout!\n");
			the_gpadc->requests[req->method].active = 0;
			goto out;
		}

		ret = twl6030_gpadc_read_channels(the_gpadc, method->rbase,
					1 << i, req);
		if (!ret)
			dev_err(the_gpadc->dev, "%s: channel error %d\n",
					__func__, i);

		count += ret;
		the_gpadc->requests[req->method].active = 0;
	}
	ret = count;
out:
	return ret;
}

int twl6030_gpadc_conversion(struct twl6030_gpadc_request *req)
{
	const struct twl6030_gpadc_conversion_method *method;
	int ret = 0;

	if (unlikely(!req))
		return -EINVAL;

	if (!the_gpadc)
		return -EAGAIN;

	mutex_lock(&the_gpadc->lock);

	if (req->method >= TWL6030_GPADC_NUM_METHODS) {
		dev_err(the_gpadc->dev, "unsupported conversion method\n");
		ret = -EINVAL;
		goto out;
	}

	/* Do we have a conversion request ongoing */
	if (the_gpadc->requests[req->method].active) {
		ret = -EBUSY;
		goto out;
	}

	method = &twl6030_conversion_methods[req->method];

	if (the_gpadc->features & TWL6032_SUBCLASS)
		ret = _twl6032_gpadc_conversion(req, method);
	else
		ret = _twl6030_gpadc_conversion(req, method);

out:
	mutex_unlock(&the_gpadc->lock);

	return ret;
}
EXPORT_SYMBOL(twl6030_gpadc_conversion);

static ssize_t show_channel(struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct twl6030_gpadc_request req;
	int temp = 0;
	int ret;

	req.channels = (1 << attr->index);
	req.method = TWL6030_GPADC_SW2;
	req.active = 0;
	req.func_cb = NULL;
	ret = twl6030_gpadc_conversion(&req);
	if (ret < 0)
		return ret;

	if (req.rbuf[attr->index] > 0)
		temp = req.rbuf[attr->index];

	ret = sprintf(buf, "%d\n", temp);

	return ret;
}

static ssize_t show_raw_code(struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct twl6030_gpadc_request req;
	int temp = 0;
	int ret;

	req.channels = (1 << attr->index);
	req.method = TWL6030_GPADC_SW2;
	req.active = 0;
	req.func_cb = NULL;
	ret = twl6030_gpadc_conversion(&req);
	if (ret < 0)
		return ret;

	if (req.buf[attr->index].raw_channel_value > 0)
		temp = req.buf[attr->index].raw_code;

	ret = sprintf(buf, "%d\n", temp);

	return ret;
}

#define in_gain(index) \
static SENSOR_DEVICE_ATTR(in##index##_gain, S_IRUGO|S_IWUSR, show_gain, \
	set_gain, index); \
static SENSOR_DEVICE_ATTR(in##index##_offset, S_IRUGO|S_IWUSR, show_offset, \
	set_offset, index)

in_gain(0);
in_gain(1);
in_gain(2);
in_gain(3);
in_gain(4);
in_gain(5);
in_gain(6);
in_gain(7);
in_gain(8);
in_gain(9);
in_gain(10);
in_gain(11);
in_gain(12);
in_gain(13);
in_gain(14);
in_gain(15);
in_gain(16);

#define in_channel(index) \
static SENSOR_DEVICE_ATTR(in##index##_channel, S_IRUGO, show_channel, \
	NULL, index); \
static SENSOR_DEVICE_ATTR(in##index##_raw_code, S_IRUGO, show_raw_code, \
	NULL, index)

in_channel(0);
in_channel(1);
in_channel(2);
in_channel(3);
in_channel(4);
in_channel(5);
in_channel(6);
in_channel(7);
in_channel(8);
in_channel(9);
in_channel(10);
in_channel(11);
in_channel(12);
in_channel(13);
in_channel(14);
in_channel(15);
in_channel(16);
in_channel(17);
in_channel(18);

#define IN_ATTRS(X)\
	&sensor_dev_attr_in##X##_gain.dev_attr.attr,	\
	&sensor_dev_attr_in##X##_offset.dev_attr.attr	\

#define IN_ATTRS_CHANNEL(X)\
	&sensor_dev_attr_in##X##_channel.dev_attr.attr,		\
	&sensor_dev_attr_in##X##_raw_code.dev_attr.attr	\

static struct attribute *twl6030_gpadc_attributes[] = {
	IN_ATTRS(0),
	IN_ATTRS(1),
	IN_ATTRS(2),
	IN_ATTRS(3),
	IN_ATTRS(4),
	IN_ATTRS(5),
	IN_ATTRS(6),
	IN_ATTRS(7),
	IN_ATTRS(8),
	IN_ATTRS(9),
	IN_ATTRS(10),
	IN_ATTRS(11),
	IN_ATTRS(12),
	IN_ATTRS(13),
	IN_ATTRS(14),
	IN_ATTRS(15),
	IN_ATTRS(16),
	IN_ATTRS_CHANNEL(0),
	IN_ATTRS_CHANNEL(1),
	IN_ATTRS_CHANNEL(2),
	IN_ATTRS_CHANNEL(3),
	IN_ATTRS_CHANNEL(4),
	IN_ATTRS_CHANNEL(5),
	IN_ATTRS_CHANNEL(6),
	IN_ATTRS_CHANNEL(7),
	IN_ATTRS_CHANNEL(8),
	IN_ATTRS_CHANNEL(9),
	IN_ATTRS_CHANNEL(10),
	IN_ATTRS_CHANNEL(11),
	IN_ATTRS_CHANNEL(12),
	IN_ATTRS_CHANNEL(13),
	IN_ATTRS_CHANNEL(14),
	IN_ATTRS_CHANNEL(15),
	IN_ATTRS_CHANNEL(16),
	IN_ATTRS_CHANNEL(17),
	IN_ATTRS_CHANNEL(18),
	NULL
};

static const struct attribute_group twl6030_gpadc_group = {
	.attrs = twl6030_gpadc_attributes,
};

static long twl6030_gpadc_ioctl(struct file *filp, unsigned int cmd,
			       unsigned long arg)
{
	struct twl6030_gpadc_user_parms par;
	int val, ret;

	ret = copy_from_user(&par, (void __user *) arg, sizeof(par));
	if (ret) {
		dev_dbg(the_gpadc->dev, "copy_from_user: %d\n", ret);
		return -EACCES;
	}

	switch (cmd) {
	case TWL6030_GPADC_IOCX_ADC_READ:
	case TWL6030_GPADC_IOCX_ADC_RAW_READ: {
		struct twl6030_gpadc_request req;
		if (the_gpadc->features & TWL6032_SUBCLASS) {
			if (par.channel >= TWL6032_GPADC_MAX_CHANNELS)
				return -EINVAL;
		} else {
			if (par.channel >= TWL6030_GPADC_MAX_CHANNELS)
				return -EINVAL;
		}

		req.channels = (1 << par.channel);
		req.method	= TWL6030_GPADC_SW2;
		req.func_cb	= NULL;

		val = twl6030_gpadc_conversion(&req);
		if (likely(val > 0)) {
			par.status = 0;
			if (cmd == TWL6030_GPADC_IOCX_ADC_READ)
				par.result = (u16)req.rbuf[par.channel];
			else
				par.result = (u16)req.buf[par.channel].raw_code;

		} else if (val == 0) {
			par.status = -ENODATA;
		} else {
			par.status = val;
		}
		break;
					     }
	default:
		return -EINVAL;
	}

	ret = copy_to_user((void __user *) arg, &par, sizeof(par));
	if (ret) {
		dev_dbg(the_gpadc->dev, "copy_to_user: %d\n", ret);
		return -EACCES;
	}

	return 0;
}

static const struct file_operations twl6030_gpadc_fileops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = twl6030_gpadc_ioctl
};

static struct miscdevice twl6030_gpadc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "twl6030-gpadc",
	.fops = &twl6030_gpadc_fileops
};

static int twl6030_calibration(void)
{
	s8 delta_error1 = 0, delta_error2 = 0;
	s16 ideal_code1, ideal_code2;
	s32 gain_error_1;
	s32 offset_error;
	u8 index;
	int ret;

	for (index = 0; index < TWL6030_GPADC_MAX_CHANNELS; index++) {
		if (~calibration_bit_map & (1 << index))
			continue;

		ret = twl_i2c_read_u8(TWL6030_MODULE_ID2, &delta_error1,
					twl6030_trim_addr[index]);
		if (ret < 0)
			return ret;

		twl_i2c_read_u8(TWL6030_MODULE_ID2, &delta_error2,
					(twl6030_trim_addr[index] + 1));
		if (ret < 0)
			return ret;

		/* convert 7 bit to 8 bit signed number */
		delta_error1 = ((s8)(delta_error1 << 1) >> 1);
		delta_error2 = ((s8)(delta_error2 << 1) >> 1);
		ideal_code1 = twl6030_ideal[index].code1;
		ideal_code2 = twl6030_ideal[index].code2;

		gain_error_1 = (delta_error2 - delta_error1) * SCALE
						/ (ideal_code2 - ideal_code1);
		offset_error = delta_error1 * SCALE - gain_error_1
							*  ideal_code1;
		twl6030_calib_tbl[index].gain_error = gain_error_1 + SCALE;
		twl6030_calib_tbl[index].offset_error = offset_error;
	}

	return 0;
}

static int twl6032_calibration(struct twl6030_gpadc_data *gpadc)
{
	int chn, d1 = 0, d2 = 0, b, k, gain, x1, x2, temp;
	u8 trim_regs[17];
	int ret;

	ret = twl_i2c_read(TWL6030_MODULE_ID2, trim_regs + 1,
			TWL6032_GPADC_TRIM1, 16);
	if (ret < 0)
		return ret;

	/* Loop to calculate the value needed for returning voltages from
	 * GPADC not values.
	 *
	 * gain is calculated to 3 decimal places fixed point.
	 */
	for (chn = 0; chn < TWL6032_GPADC_MAX_CHANNELS; chn++) {

		switch (chn) {
		case 0:
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 11:
		case 12:
		case 13:
		case 14:
			/* D1 */
			d1 = (trim_regs[3] & 0x1F) << 2;
			d1 |= (trim_regs[1] & 0x06) >> 1;
			if (trim_regs[1] & 0x01)
				d1 = -d1;

			/* D2 */
			d2 = (trim_regs[4] & 0x3F) << 2;
			d2 |= (trim_regs[2] & 0x06) >> 1;
			if (trim_regs[2] & 0x01)
				d2 = -d2;
			break;
		case 8:
			/* D1 */
			temp = (trim_regs[3] & 0x1F) << 2;
			temp |= (trim_regs[1] & 0x06) >> 1;
			if (trim_regs[1] & 0x01)
				temp = -temp;

			d1 = (trim_regs[8] & 0x18) << 1;
			d1 |= (trim_regs[7] & 0x1E) >> 1;
			if (trim_regs[7] & 0x01)
				d1 = -d1;

			d1 += temp;

			/* D2 */
			temp = (trim_regs[4] & 0x3F) << 2;
			temp |= (trim_regs[2] & 0x06) >> 1;
			if (trim_regs[2] & 0x01)
				temp = -temp;

			d2 = (trim_regs[10] & 0x1F) << 2;
			d2 |= (trim_regs[8] & 0x06) >> 1;
			if (trim_regs[8] & 0x01)
				d2 = -d2;

			d2 += temp;
			break;
		case 9:
			/* D1 */
			temp = (trim_regs[3] & 0x1F) << 2;
			temp |= (trim_regs[1] & 0x06) >> 1;
			if (trim_regs[1] & 0x01)
				temp = -temp;

			d1 = (trim_regs[14] & 0x18) << 1;
			d1 |= (trim_regs[12] & 0x1E) >> 1;
			if (trim_regs[12] & 0x01)
				d1 = -d1;

			d1 += temp;

			/* D2 */
			temp = (trim_regs[4] & 0x3F) << 2;
			temp |= (trim_regs[2] & 0x06) >> 1;
			if (trim_regs[2] & 0x01)
				temp = -temp;

			d2 = (trim_regs[16] & 0x1F) << 2;
			d2 |= (trim_regs[14] & 0x06) >> 1;
			if (trim_regs[14] & 0x01)
				d2 = -d2;

			d2 += temp;
		case 10:
			/* D1 */
			d1 = (trim_regs[11] & 0x0F) << 3;
			d1 |= (trim_regs[9] & 0x0E) >> 1;
			if (trim_regs[9] & 0x01)
				d1 = -d1;

			/* D2 */
			d2 = (trim_regs[15] & 0x0F) << 3;
			d2 |= (trim_regs[13] & 0x0E) >> 1;
			if (trim_regs[13] & 0x01)
				d2 = -d2;
			break;
		case 7:
		case 18:
			/* D1 */
			temp = (trim_regs[3] & 0x1F) << 2;
			temp |= (trim_regs[1] & 0x06) >> 1;
			if (trim_regs[1] & 0x01)
				temp = -temp;

			d1 = (trim_regs[5] & 0x7E) >> 1;
			if (trim_regs[5] & 0x01)
				d1 = -d1;

			d1 += temp;

			/* D2 */
			temp = (trim_regs[4] & 0x3F) << 2;
			temp |= (trim_regs[2] & 0x06) >> 1;
			if (trim_regs[2] & 0x01)
				temp = -temp;

			d2 = (trim_regs[6] & 0xFF) >> 1;
			if (trim_regs[6] & 0x01)
				d2 = -d2;

			d2 += temp;
			break;
		default:
			/* No data for other channels */
			continue;
		}

		dev_dbg(gpadc->dev, "GPADC d1   for Chn: %d = %d\n", chn, d1);
		dev_dbg(gpadc->dev, "GPADC d2   for Chn: %d = %d\n", chn, d2);

		/* Gain */
		gain = ((twl6032_ideal[chn].v2 -
			twl6032_ideal[chn].v1) * 1000) /
			((twl6032_ideal[chn].code2 -
			twl6032_ideal[chn].code1));

		x1 = twl6032_ideal[chn].code1;
		x2 = twl6032_ideal[chn].code2;

		/* k */
		k = 1000 + (((d2 - d1) * 1000) / (x2 - x1));

		/* b */
		b = (d1 * 1000) - (k - 1000) * x1;

		gpadc->twl6032_cal_tbl[chn].gain = gain;
		gpadc->twl6032_cal_tbl[chn].gain_error = k;
		gpadc->twl6032_cal_tbl[chn].offset_error = b;

		dev_dbg(gpadc->dev, "GPADC x1   for Chn: %d = %d\n", chn, x1);
		dev_dbg(gpadc->dev, "GPADC x2   for Chn: %d = %d\n", chn, x2);
		dev_dbg(gpadc->dev, "GPADC Gain for Chn: %d = %d\n", chn, gain);
		dev_dbg(gpadc->dev, "GPADC k    for Chn: %d = %d\n", chn, k);
		dev_dbg(gpadc->dev, "GPADC b    for Chn: %d = %d\n", chn, b);

	}

	return 0;
}

static int __devinit twl6030_gpadc_probe(struct platform_device *pdev)
{
	struct twl6030_gpadc_data *gpadc;
	struct twl4030_madc_platform_data *pdata = pdev->dev.platform_data;
	int irq;
	int irq_rt;
	int ret = 0;

	gpadc = kzalloc(sizeof *gpadc, GFP_KERNEL);
	if (!gpadc)
		return -ENOMEM;

	if (!pdata) {
		dev_dbg(&pdev->dev, "platform_data not available\n");
		ret = -EINVAL;
		goto err_pdata;
	}

	if (pdata->features & TWL6032_SUBCLASS) {
		gpadc->twl6032_cal_tbl = kzalloc(
				sizeof(struct twl6032_chnl_calib) *
				TWL6032_GPADC_MAX_CHANNELS,
				GFP_KERNEL);
		if (!gpadc->twl6032_cal_tbl) {
			ret = -ENOMEM;
			goto err_pdata;
		}
	}

	gpadc->dev = &pdev->dev;

	gpadc->features = pdata->features;

	twl6030_conversion_methods = twl6030_conversion_methods_table;

	if (gpadc->features & TWL6032_SUBCLASS)
		twl6030_conversion_methods = twl6032_conversion_methods_table;

	ret = misc_register(&twl6030_gpadc_device);
	if (ret) {
		dev_dbg(&pdev->dev, "could not register misc_device\n");
		goto err_misc;
	}

	irq_rt = platform_get_irq(pdev, 0);
	if (irq_rt < 0) {
		dev_err(&pdev->dev, "failed to get irq\n");
		goto err_irq;
	}

	ret = request_threaded_irq(irq_rt, NULL, twl6030_gpadc_irq_handler,
		0, "twl6030_gpadc", &gpadc->requests[TWL6030_GPADC_RT]);
	if (ret) {
		dev_dbg(&pdev->dev, "could not request irq\n");
		goto err_irq;
	}

	irq = platform_get_irq(pdev, 1);
	if (irq < 0) {
		dev_err(&pdev->dev, "failed to get irq\n");
		goto err_irq_rt;
	}

	ret = request_threaded_irq(irq, NULL, twl6030_gpadc_irq_handler,
		0, "twl6030_gpadc", &gpadc->requests[TWL6030_GPADC_SW2]);
	if (ret) {
		dev_dbg(&pdev->dev, "could not request irq\n");
		goto err_irq_rt;
	}

	platform_set_drvdata(pdev, gpadc);
	mutex_init(&gpadc->lock);
	INIT_WORK(&gpadc->ws, twl6030_gpadc_work);

	if (gpadc->features & TWL6032_SUBCLASS)
		ret = twl6032_calibration(gpadc);
	else
		ret = twl6030_calibration();
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to read calibration registers\n");
		goto err_calib;
	}
	the_gpadc = gpadc;

	ret = sysfs_create_group(&pdev->dev.kobj, &twl6030_gpadc_group);
	if (ret)
		dev_err(&pdev->dev, "could not create sysfs files\n");

	return 0;

err_calib:
	free_irq(irq, &gpadc->requests[TWL6030_GPADC_SW2]);
err_irq_rt:
	free_irq(irq_rt, &gpadc->requests[TWL6030_GPADC_RT]);
err_irq:
	misc_deregister(&twl6030_gpadc_device);

err_misc:
	if (pdata->features & TWL6032_SUBCLASS)
		kfree(gpadc->twl6032_cal_tbl);
err_pdata:
	kfree(gpadc);

	return ret;
}

static int __devexit twl6030_gpadc_remove(struct platform_device *pdev)
{
	struct twl6030_gpadc_data *gpadc = platform_get_drvdata(pdev);

	twl6030_gpadc_disable_irq(TWL6030_GPADC_RT);
	twl6030_gpadc_disable_irq(TWL6030_GPADC_SW2);
	free_irq(platform_get_irq(pdev, 0), gpadc);
	sysfs_remove_group(&pdev->dev.kobj, &twl6030_gpadc_group);
	cancel_work_sync(&gpadc->ws);
	misc_deregister(&twl6030_gpadc_device);

	return 0;
}

static int twl6030_gpadc_suspend(struct device *pdev)
{
	int ret;

	ret = twl_i2c_write_u8(TWL6030_MODULE_ID1, GPADCR, REG_TOGGLE1);
	if (ret)
		pr_err("%s: Error reseting GPADC (%d)!\n", __func__, ret);

	return 0;
};

static int twl6030_gpadc_resume(struct device *pdev)
{
	int ret;

	ret = twl_i2c_write_u8(TWL6030_MODULE_ID1, GPADCS, REG_TOGGLE1);
	if (ret)
		pr_err("%s: Error setting GPADC (%d)!\n", __func__, ret);

	return 0;
};
static const struct dev_pm_ops twl6030_gpadc_pm_ops = {
	.suspend = twl6030_gpadc_suspend,
	.resume = twl6030_gpadc_resume,
};

static struct platform_driver twl6030_gpadc_driver = {
	.probe		= twl6030_gpadc_probe,
	.remove		= __devexit_p(twl6030_gpadc_remove),
	.driver		= {
		.name	= "twl6030_gpadc",
		.owner	= THIS_MODULE,
		.pm = &twl6030_gpadc_pm_ops,
	},
};

static int __init twl6030_gpadc_init(void)
{
	return platform_driver_register(&twl6030_gpadc_driver);
}
module_init(twl6030_gpadc_init);

static void __exit twl6030_gpadc_exit(void)
{
	platform_driver_unregister(&twl6030_gpadc_driver);
}
module_exit(twl6030_gpadc_exit);

MODULE_ALIAS("platform:twl6030-gpadc");
MODULE_AUTHOR("Texas Instruments Inc.");
MODULE_DESCRIPTION("twl6030 ADC driver");
MODULE_LICENSE("GPL");
