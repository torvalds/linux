/*
 *
 * TWL4030 MADC module driver-This driver monitors the real time
 * conversion of analog signals like battery temperature,
 * battery type, battery level etc.
 *
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
 * J Keerthy <j-keerthy@ti.com>
 *
 * Based on twl4030-madc.c
 * Copyright (C) 2008 Nokia Corporation
 * Mikko Ylinen <mikko.k.ylinen@nokia.com>
 *
 * Amit Kucheria <amit.kucheria@canonical.com>
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

#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/i2c/twl.h>
#include <linux/i2c/twl4030-madc.h>
#include <linux/module.h>
#include <linux/stddef.h>
#include <linux/mutex.h>
#include <linux/bitops.h>
#include <linux/jiffies.h>
#include <linux/types.h>
#include <linux/gfp.h>
#include <linux/err.h>

#include <linux/iio/iio.h>

/**
 * struct twl4030_madc_data - a container for madc info
 * @dev:		Pointer to device structure for madc
 * @lock:		Mutex protecting this data structure
 * @requests:		Array of request struct corresponding to SW1, SW2 and RT
 * @use_second_irq:	IRQ selection (main or co-processor)
 * @imr:		Interrupt mask register of MADC
 * @isr:		Interrupt status register of MADC
 */
struct twl4030_madc_data {
	struct device *dev;
	struct mutex lock;	/* mutex protecting this data structure */
	struct twl4030_madc_request requests[TWL4030_MADC_NUM_METHODS];
	bool use_second_irq;
	u8 imr;
	u8 isr;
};

static int twl4030_madc_read(struct iio_dev *iio_dev,
			     const struct iio_chan_spec *chan,
			     int *val, int *val2, long mask)
{
	struct twl4030_madc_data *madc = iio_priv(iio_dev);
	struct twl4030_madc_request req;
	int ret;

	req.method = madc->use_second_irq ? TWL4030_MADC_SW2 : TWL4030_MADC_SW1;

	req.channels = BIT(chan->channel);
	req.active = false;
	req.func_cb = NULL;
	req.type = TWL4030_MADC_WAIT;
	req.raw = !(mask == IIO_CHAN_INFO_PROCESSED);
	req.do_avg = (mask == IIO_CHAN_INFO_AVERAGE_RAW);

	ret = twl4030_madc_conversion(&req);
	if (ret < 0)
		return ret;

	*val = req.rbuf[chan->channel];

	return IIO_VAL_INT;
}

static const struct iio_info twl4030_madc_iio_info = {
	.read_raw = &twl4030_madc_read,
	.driver_module = THIS_MODULE,
};

#define TWL4030_ADC_CHANNEL(_channel, _type, _name) {	\
	.type = _type,					\
	.channel = _channel,				\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |  \
			      BIT(IIO_CHAN_INFO_AVERAGE_RAW) | \
			      BIT(IIO_CHAN_INFO_PROCESSED), \
	.datasheet_name = _name,			\
	.indexed = 1,					\
}

static const struct iio_chan_spec twl4030_madc_iio_channels[] = {
	TWL4030_ADC_CHANNEL(0, IIO_VOLTAGE, "ADCIN0"),
	TWL4030_ADC_CHANNEL(1, IIO_TEMP, "ADCIN1"),
	TWL4030_ADC_CHANNEL(2, IIO_VOLTAGE, "ADCIN2"),
	TWL4030_ADC_CHANNEL(3, IIO_VOLTAGE, "ADCIN3"),
	TWL4030_ADC_CHANNEL(4, IIO_VOLTAGE, "ADCIN4"),
	TWL4030_ADC_CHANNEL(5, IIO_VOLTAGE, "ADCIN5"),
	TWL4030_ADC_CHANNEL(6, IIO_VOLTAGE, "ADCIN6"),
	TWL4030_ADC_CHANNEL(7, IIO_VOLTAGE, "ADCIN7"),
	TWL4030_ADC_CHANNEL(8, IIO_VOLTAGE, "ADCIN8"),
	TWL4030_ADC_CHANNEL(9, IIO_VOLTAGE, "ADCIN9"),
	TWL4030_ADC_CHANNEL(10, IIO_CURRENT, "ADCIN10"),
	TWL4030_ADC_CHANNEL(11, IIO_VOLTAGE, "ADCIN11"),
	TWL4030_ADC_CHANNEL(12, IIO_VOLTAGE, "ADCIN12"),
	TWL4030_ADC_CHANNEL(13, IIO_VOLTAGE, "ADCIN13"),
	TWL4030_ADC_CHANNEL(14, IIO_VOLTAGE, "ADCIN14"),
	TWL4030_ADC_CHANNEL(15, IIO_VOLTAGE, "ADCIN15"),
};

static struct twl4030_madc_data *twl4030_madc;

struct twl4030_prescale_divider_ratios {
	s16 numerator;
	s16 denominator;
};

static const struct twl4030_prescale_divider_ratios
twl4030_divider_ratios[16] = {
	{1, 1},		/* CHANNEL 0 No Prescaler */
	{1, 1},		/* CHANNEL 1 No Prescaler */
	{6, 10},	/* CHANNEL 2 */
	{6, 10},	/* CHANNEL 3 */
	{6, 10},	/* CHANNEL 4 */
	{6, 10},	/* CHANNEL 5 */
	{6, 10},	/* CHANNEL 6 */
	{6, 10},	/* CHANNEL 7 */
	{3, 14},	/* CHANNEL 8 */
	{1, 3},		/* CHANNEL 9 */
	{1, 1},		/* CHANNEL 10 No Prescaler */
	{15, 100},	/* CHANNEL 11 */
	{1, 4},		/* CHANNEL 12 */
	{1, 1},		/* CHANNEL 13 Reserved channels */
	{1, 1},		/* CHANNEL 14 Reseved channels */
	{5, 11},	/* CHANNEL 15 */
};


/* Conversion table from -3 to 55 degrees Celcius */
static int twl4030_therm_tbl[] = {
	30800,	29500,	28300,	27100,
	26000,	24900,	23900,	22900,	22000,	21100,	20300,	19400,	18700,
	17900,	17200,	16500,	15900,	15300,	14700,	14100,	13600,	13100,
	12600,	12100,	11600,	11200,	10800,	10400,	10000,	9630,	9280,
	8950,	8620,	8310,	8020,	7730,	7460,	7200,	6950,	6710,
	6470,	6250,	6040,	5830,	5640,	5450,	5260,	5090,	4920,
	4760,	4600,	4450,	4310,	4170,	4040,	3910,	3790,	3670,
	3550
};

/*
 * Structure containing the registers
 * of different conversion methods supported by MADC.
 * Hardware or RT real time conversion request initiated by external host
 * processor for RT Signal conversions.
 * External host processors can also request for non RT conversions
 * SW1 and SW2 software conversions also called asynchronous or GPC request.
 */
static
const struct twl4030_madc_conversion_method twl4030_conversion_methods[] = {
	[TWL4030_MADC_RT] = {
			     .sel = TWL4030_MADC_RTSELECT_LSB,
			     .avg = TWL4030_MADC_RTAVERAGE_LSB,
			     .rbase = TWL4030_MADC_RTCH0_LSB,
			     },
	[TWL4030_MADC_SW1] = {
			      .sel = TWL4030_MADC_SW1SELECT_LSB,
			      .avg = TWL4030_MADC_SW1AVERAGE_LSB,
			      .rbase = TWL4030_MADC_GPCH0_LSB,
			      .ctrl = TWL4030_MADC_CTRL_SW1,
			      },
	[TWL4030_MADC_SW2] = {
			      .sel = TWL4030_MADC_SW2SELECT_LSB,
			      .avg = TWL4030_MADC_SW2AVERAGE_LSB,
			      .rbase = TWL4030_MADC_GPCH0_LSB,
			      .ctrl = TWL4030_MADC_CTRL_SW2,
			      },
};

/**
 * twl4030_madc_channel_raw_read() - Function to read a particular channel value
 * @madc:	pointer to struct twl4030_madc_data
 * @reg:	lsb of ADC Channel
 *
 * Return: 0 on success, an error code otherwise.
 */
static int twl4030_madc_channel_raw_read(struct twl4030_madc_data *madc, u8 reg)
{
	u16 val;
	int ret;
	/*
	 * For each ADC channel, we have MSB and LSB register pair. MSB address
	 * is always LSB address+1. reg parameter is the address of LSB register
	 */
	ret = twl_i2c_read_u16(TWL4030_MODULE_MADC, &val, reg);
	if (ret) {
		dev_err(madc->dev, "unable to read register 0x%X\n", reg);
		return ret;
	}

	return (int)(val >> 6);
}

/*
 * Return battery temperature in degrees Celsius
 * Or < 0 on failure.
 */
static int twl4030battery_temperature(int raw_volt)
{
	u8 val;
	int temp, curr, volt, res, ret;

	volt = (raw_volt * TEMP_STEP_SIZE) / TEMP_PSR_R;
	/* Getting and calculating the supply current in micro amperes */
	ret = twl_i2c_read_u8(TWL_MODULE_MAIN_CHARGE, &val,
		REG_BCICTL2);
	if (ret < 0)
		return ret;

	curr = ((val & TWL4030_BCI_ITHEN) + 1) * 10;
	/* Getting and calculating the thermistor resistance in ohms */
	res = volt * 1000 / curr;
	/* calculating temperature */
	for (temp = 58; temp >= 0; temp--) {
		int actual = twl4030_therm_tbl[temp];
		if ((actual - res) >= 0)
			break;
	}

	return temp + 1;
}

static int twl4030battery_current(int raw_volt)
{
	int ret;
	u8 val;

	ret = twl_i2c_read_u8(TWL_MODULE_MAIN_CHARGE, &val,
		TWL4030_BCI_BCICTL1);
	if (ret)
		return ret;
	if (val & TWL4030_BCI_CGAIN) /* slope of 0.44 mV/mA */
		return (raw_volt * CURR_STEP_SIZE) / CURR_PSR_R1;
	else /* slope of 0.88 mV/mA */
		return (raw_volt * CURR_STEP_SIZE) / CURR_PSR_R2;
}

/*
 * Function to read channel values
 * @madc - pointer to twl4030_madc_data struct
 * @reg_base - Base address of the first channel
 * @Channels - 16 bit bitmap. If the bit is set, channel's value is read
 * @buf - The channel values are stored here. if read fails error
 * @raw - Return raw values without conversion
 * value is stored
 * Returns the number of successfully read channels.
 */
static int twl4030_madc_read_channels(struct twl4030_madc_data *madc,
				      u8 reg_base, unsigned
				      long channels, int *buf,
				      bool raw)
{
	int count = 0;
	int i;
	u8 reg;

	for_each_set_bit(i, &channels, TWL4030_MADC_MAX_CHANNELS) {
		reg = reg_base + (2 * i);
		buf[i] = twl4030_madc_channel_raw_read(madc, reg);
		if (buf[i] < 0) {
			dev_err(madc->dev, "Unable to read register 0x%X\n",
				reg);
			return buf[i];
		}
		if (raw) {
			count++;
			continue;
		}
		switch (i) {
		case 10:
			buf[i] = twl4030battery_current(buf[i]);
			if (buf[i] < 0) {
				dev_err(madc->dev, "err reading current\n");
				return buf[i];
			} else {
				count++;
				buf[i] = buf[i] - 750;
			}
			break;
		case 1:
			buf[i] = twl4030battery_temperature(buf[i]);
			if (buf[i] < 0) {
				dev_err(madc->dev, "err reading temperature\n");
				return buf[i];
			} else {
				buf[i] -= 3;
				count++;
			}
			break;
		default:
			count++;
			/* Analog Input (V) = conv_result * step_size / R
			 * conv_result = decimal value of 10-bit conversion
			 *		 result
			 * step size = 1.5 / (2 ^ 10 -1)
			 * R = Prescaler ratio for input channels.
			 * Result given in mV hence multiplied by 1000.
			 */
			buf[i] = (buf[i] * 3 * 1000 *
				 twl4030_divider_ratios[i].denominator)
				/ (2 * 1023 *
				twl4030_divider_ratios[i].numerator);
		}
	}

	return count;
}

/*
 * Enables irq.
 * @madc - pointer to twl4030_madc_data struct
 * @id - irq number to be enabled
 * can take one of TWL4030_MADC_RT, TWL4030_MADC_SW1, TWL4030_MADC_SW2
 * corresponding to RT, SW1, SW2 conversion requests.
 * If the i2c read fails it returns an error else returns 0.
 */
static int twl4030_madc_enable_irq(struct twl4030_madc_data *madc, u8 id)
{
	u8 val;
	int ret;

	ret = twl_i2c_read_u8(TWL4030_MODULE_MADC, &val, madc->imr);
	if (ret) {
		dev_err(madc->dev, "unable to read imr register 0x%X\n",
			madc->imr);
		return ret;
	}

	val &= ~(1 << id);
	ret = twl_i2c_write_u8(TWL4030_MODULE_MADC, val, madc->imr);
	if (ret) {
		dev_err(madc->dev,
			"unable to write imr register 0x%X\n", madc->imr);
		return ret;
	}

	return 0;
}

/*
 * Disables irq.
 * @madc - pointer to twl4030_madc_data struct
 * @id - irq number to be disabled
 * can take one of TWL4030_MADC_RT, TWL4030_MADC_SW1, TWL4030_MADC_SW2
 * corresponding to RT, SW1, SW2 conversion requests.
 * Returns error if i2c read/write fails.
 */
static int twl4030_madc_disable_irq(struct twl4030_madc_data *madc, u8 id)
{
	u8 val;
	int ret;

	ret = twl_i2c_read_u8(TWL4030_MODULE_MADC, &val, madc->imr);
	if (ret) {
		dev_err(madc->dev, "unable to read imr register 0x%X\n",
			madc->imr);
		return ret;
	}
	val |= (1 << id);
	ret = twl_i2c_write_u8(TWL4030_MODULE_MADC, val, madc->imr);
	if (ret) {
		dev_err(madc->dev,
			"unable to write imr register 0x%X\n", madc->imr);
		return ret;
	}

	return 0;
}

static irqreturn_t twl4030_madc_threaded_irq_handler(int irq, void *_madc)
{
	struct twl4030_madc_data *madc = _madc;
	const struct twl4030_madc_conversion_method *method;
	u8 isr_val, imr_val;
	int i, len, ret;
	struct twl4030_madc_request *r;

	mutex_lock(&madc->lock);
	ret = twl_i2c_read_u8(TWL4030_MODULE_MADC, &isr_val, madc->isr);
	if (ret) {
		dev_err(madc->dev, "unable to read isr register 0x%X\n",
			madc->isr);
		goto err_i2c;
	}
	ret = twl_i2c_read_u8(TWL4030_MODULE_MADC, &imr_val, madc->imr);
	if (ret) {
		dev_err(madc->dev, "unable to read imr register 0x%X\n",
			madc->imr);
		goto err_i2c;
	}
	isr_val &= ~imr_val;
	for (i = 0; i < TWL4030_MADC_NUM_METHODS; i++) {
		if (!(isr_val & (1 << i)))
			continue;
		ret = twl4030_madc_disable_irq(madc, i);
		if (ret < 0)
			dev_dbg(madc->dev, "Disable interrupt failed %d\n", i);
		madc->requests[i].result_pending = 1;
	}
	for (i = 0; i < TWL4030_MADC_NUM_METHODS; i++) {
		r = &madc->requests[i];
		/* No pending results for this method, move to next one */
		if (!r->result_pending)
			continue;
		method = &twl4030_conversion_methods[r->method];
		/* Read results */
		len = twl4030_madc_read_channels(madc, method->rbase,
						 r->channels, r->rbuf, r->raw);
		/* Return results to caller */
		if (r->func_cb != NULL) {
			r->func_cb(len, r->channels, r->rbuf);
			r->func_cb = NULL;
		}
		/* Free request */
		r->result_pending = 0;
		r->active = 0;
	}
	mutex_unlock(&madc->lock);

	return IRQ_HANDLED;

err_i2c:
	/*
	 * In case of error check whichever request is active
	 * and service the same.
	 */
	for (i = 0; i < TWL4030_MADC_NUM_METHODS; i++) {
		r = &madc->requests[i];
		if (r->active == 0)
			continue;
		method = &twl4030_conversion_methods[r->method];
		/* Read results */
		len = twl4030_madc_read_channels(madc, method->rbase,
						 r->channels, r->rbuf, r->raw);
		/* Return results to caller */
		if (r->func_cb != NULL) {
			r->func_cb(len, r->channels, r->rbuf);
			r->func_cb = NULL;
		}
		/* Free request */
		r->result_pending = 0;
		r->active = 0;
	}
	mutex_unlock(&madc->lock);

	return IRQ_HANDLED;
}

static int twl4030_madc_set_irq(struct twl4030_madc_data *madc,
				struct twl4030_madc_request *req)
{
	struct twl4030_madc_request *p;
	int ret;

	p = &madc->requests[req->method];
	memcpy(p, req, sizeof(*req));
	ret = twl4030_madc_enable_irq(madc, req->method);
	if (ret < 0) {
		dev_err(madc->dev, "enable irq failed!!\n");
		return ret;
	}

	return 0;
}

/*
 * Function which enables the madc conversion
 * by writing to the control register.
 * @madc - pointer to twl4030_madc_data struct
 * @conv_method - can be TWL4030_MADC_RT, TWL4030_MADC_SW2, TWL4030_MADC_SW1
 * corresponding to RT SW1 or SW2 conversion methods.
 * Returns 0 if succeeds else a negative error value
 */
static int twl4030_madc_start_conversion(struct twl4030_madc_data *madc,
					 int conv_method)
{
	const struct twl4030_madc_conversion_method *method;
	int ret = 0;

	if (conv_method != TWL4030_MADC_SW1 && conv_method != TWL4030_MADC_SW2)
		return -ENOTSUPP;

	method = &twl4030_conversion_methods[conv_method];
	ret = twl_i2c_write_u8(TWL4030_MODULE_MADC, TWL4030_MADC_SW_START,
			       method->ctrl);
	if (ret) {
		dev_err(madc->dev, "unable to write ctrl register 0x%X\n",
			method->ctrl);
		return ret;
	}

	return 0;
}

/*
 * Function that waits for conversion to be ready
 * @madc - pointer to twl4030_madc_data struct
 * @timeout_ms - timeout value in milliseconds
 * @status_reg - ctrl register
 * returns 0 if succeeds else a negative error value
 */
static int twl4030_madc_wait_conversion_ready(struct twl4030_madc_data *madc,
					      unsigned int timeout_ms,
					      u8 status_reg)
{
	unsigned long timeout;
	int ret;

	timeout = jiffies + msecs_to_jiffies(timeout_ms);
	do {
		u8 reg;

		ret = twl_i2c_read_u8(TWL4030_MODULE_MADC, &reg, status_reg);
		if (ret) {
			dev_err(madc->dev,
				"unable to read status register 0x%X\n",
				status_reg);
			return ret;
		}
		if (!(reg & TWL4030_MADC_BUSY) && (reg & TWL4030_MADC_EOC_SW))
			return 0;
		usleep_range(500, 2000);
	} while (!time_after(jiffies, timeout));
	dev_err(madc->dev, "conversion timeout!\n");

	return -EAGAIN;
}

/*
 * An exported function which can be called from other kernel drivers.
 * @req twl4030_madc_request structure
 * req->rbuf will be filled with read values of channels based on the
 * channel index. If a particular channel reading fails there will
 * be a negative error value in the corresponding array element.
 * returns 0 if succeeds else error value
 */
int twl4030_madc_conversion(struct twl4030_madc_request *req)
{
	const struct twl4030_madc_conversion_method *method;
	int ret;

	if (!req || !twl4030_madc)
		return -EINVAL;

	mutex_lock(&twl4030_madc->lock);
	if (req->method < TWL4030_MADC_RT || req->method > TWL4030_MADC_SW2) {
		ret = -EINVAL;
		goto out;
	}
	/* Do we have a conversion request ongoing */
	if (twl4030_madc->requests[req->method].active) {
		ret = -EBUSY;
		goto out;
	}
	method = &twl4030_conversion_methods[req->method];
	/* Select channels to be converted */
	ret = twl_i2c_write_u16(TWL4030_MODULE_MADC, req->channels, method->sel);
	if (ret) {
		dev_err(twl4030_madc->dev,
			"unable to write sel register 0x%X\n", method->sel);
		goto out;
	}
	/* Select averaging for all channels if do_avg is set */
	if (req->do_avg) {
		ret = twl_i2c_write_u16(TWL4030_MODULE_MADC, req->channels,
				       method->avg);
		if (ret) {
			dev_err(twl4030_madc->dev,
				"unable to write avg register 0x%X\n",
				method->avg);
			goto out;
		}
	}
	if (req->type == TWL4030_MADC_IRQ_ONESHOT && req->func_cb != NULL) {
		ret = twl4030_madc_set_irq(twl4030_madc, req);
		if (ret < 0)
			goto out;
		ret = twl4030_madc_start_conversion(twl4030_madc, req->method);
		if (ret < 0)
			goto out;
		twl4030_madc->requests[req->method].active = 1;
		ret = 0;
		goto out;
	}
	/* With RT method we should not be here anymore */
	if (req->method == TWL4030_MADC_RT) {
		ret = -EINVAL;
		goto out;
	}
	ret = twl4030_madc_start_conversion(twl4030_madc, req->method);
	if (ret < 0)
		goto out;
	twl4030_madc->requests[req->method].active = 1;
	/* Wait until conversion is ready (ctrl register returns EOC) */
	ret = twl4030_madc_wait_conversion_ready(twl4030_madc, 5, method->ctrl);
	if (ret) {
		twl4030_madc->requests[req->method].active = 0;
		goto out;
	}
	ret = twl4030_madc_read_channels(twl4030_madc, method->rbase,
					 req->channels, req->rbuf, req->raw);
	twl4030_madc->requests[req->method].active = 0;

out:
	mutex_unlock(&twl4030_madc->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(twl4030_madc_conversion);

int twl4030_get_madc_conversion(int channel_no)
{
	struct twl4030_madc_request req;
	int temp = 0;
	int ret;

	req.channels = (1 << channel_no);
	req.method = TWL4030_MADC_SW2;
	req.active = 0;
	req.raw = 0;
	req.func_cb = NULL;
	ret = twl4030_madc_conversion(&req);
	if (ret < 0)
		return ret;
	if (req.rbuf[channel_no] > 0)
		temp = req.rbuf[channel_no];

	return temp;
}
EXPORT_SYMBOL_GPL(twl4030_get_madc_conversion);

/**
 * twl4030_madc_set_current_generator() - setup bias current
 *
 * @madc:	pointer to twl4030_madc_data struct
 * @chan:	can be one of the two values:
 *		TWL4030_BCI_ITHEN
 *		Enables bias current for main battery type reading
 *		TWL4030_BCI_TYPEN
 *		Enables bias current for main battery temperature sensing
 * @on:		enable or disable chan.
 *
 * Function to enable or disable bias current for
 * main battery type reading or temperature sensing
 */
static int twl4030_madc_set_current_generator(struct twl4030_madc_data *madc,
					      int chan, int on)
{
	int ret;
	int regmask;
	u8 regval;

	ret = twl_i2c_read_u8(TWL_MODULE_MAIN_CHARGE,
			      &regval, TWL4030_BCI_BCICTL1);
	if (ret) {
		dev_err(madc->dev, "unable to read BCICTL1 reg 0x%X",
			TWL4030_BCI_BCICTL1);
		return ret;
	}

	regmask = chan ? TWL4030_BCI_ITHEN : TWL4030_BCI_TYPEN;
	if (on)
		regval |= regmask;
	else
		regval &= ~regmask;

	ret = twl_i2c_write_u8(TWL_MODULE_MAIN_CHARGE,
			       regval, TWL4030_BCI_BCICTL1);
	if (ret) {
		dev_err(madc->dev, "unable to write BCICTL1 reg 0x%X\n",
			TWL4030_BCI_BCICTL1);
		return ret;
	}

	return 0;
}

/*
 * Function that sets MADC software power on bit to enable MADC
 * @madc - pointer to twl4030_madc_data struct
 * @on - Enable or disable MADC software power on bit.
 * returns error if i2c read/write fails else 0
 */
static int twl4030_madc_set_power(struct twl4030_madc_data *madc, int on)
{
	u8 regval;
	int ret;

	ret = twl_i2c_read_u8(TWL_MODULE_MAIN_CHARGE,
			      &regval, TWL4030_MADC_CTRL1);
	if (ret) {
		dev_err(madc->dev, "unable to read madc ctrl1 reg 0x%X\n",
			TWL4030_MADC_CTRL1);
		return ret;
	}
	if (on)
		regval |= TWL4030_MADC_MADCON;
	else
		regval &= ~TWL4030_MADC_MADCON;
	ret = twl_i2c_write_u8(TWL4030_MODULE_MADC, regval, TWL4030_MADC_CTRL1);
	if (ret) {
		dev_err(madc->dev, "unable to write madc ctrl1 reg 0x%X\n",
			TWL4030_MADC_CTRL1);
		return ret;
	}

	return 0;
}

/*
 * Initialize MADC and request for threaded irq
 */
static int twl4030_madc_probe(struct platform_device *pdev)
{
	struct twl4030_madc_data *madc;
	struct twl4030_madc_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct device_node *np = pdev->dev.of_node;
	int irq, ret;
	u8 regval;
	struct iio_dev *iio_dev = NULL;

	if (!pdata && !np) {
		dev_err(&pdev->dev, "neither platform data nor Device Tree node available\n");
		return -EINVAL;
	}

	iio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*madc));
	if (!iio_dev) {
		dev_err(&pdev->dev, "failed allocating iio device\n");
		return -ENOMEM;
	}

	madc = iio_priv(iio_dev);
	madc->dev = &pdev->dev;

	iio_dev->name = dev_name(&pdev->dev);
	iio_dev->dev.parent = &pdev->dev;
	iio_dev->dev.of_node = pdev->dev.of_node;
	iio_dev->info = &twl4030_madc_iio_info;
	iio_dev->modes = INDIO_DIRECT_MODE;
	iio_dev->channels = twl4030_madc_iio_channels;
	iio_dev->num_channels = ARRAY_SIZE(twl4030_madc_iio_channels);

	/*
	 * Phoenix provides 2 interrupt lines. The first one is connected to
	 * the OMAP. The other one can be connected to the other processor such
	 * as modem. Hence two separate ISR and IMR registers.
	 */
	if (pdata)
		madc->use_second_irq = (pdata->irq_line != 1);
	else
		madc->use_second_irq = of_property_read_bool(np,
				       "ti,system-uses-second-madc-irq");

	madc->imr = madc->use_second_irq ? TWL4030_MADC_IMR2 :
					   TWL4030_MADC_IMR1;
	madc->isr = madc->use_second_irq ? TWL4030_MADC_ISR2 :
					   TWL4030_MADC_ISR1;

	ret = twl4030_madc_set_power(madc, 1);
	if (ret < 0)
		return ret;
	ret = twl4030_madc_set_current_generator(madc, 0, 1);
	if (ret < 0)
		goto err_current_generator;

	ret = twl_i2c_read_u8(TWL_MODULE_MAIN_CHARGE,
			      &regval, TWL4030_BCI_BCICTL1);
	if (ret) {
		dev_err(&pdev->dev, "unable to read reg BCI CTL1 0x%X\n",
			TWL4030_BCI_BCICTL1);
		goto err_i2c;
	}
	regval |= TWL4030_BCI_MESBAT;
	ret = twl_i2c_write_u8(TWL_MODULE_MAIN_CHARGE,
			       regval, TWL4030_BCI_BCICTL1);
	if (ret) {
		dev_err(&pdev->dev, "unable to write reg BCI Ctl1 0x%X\n",
			TWL4030_BCI_BCICTL1);
		goto err_i2c;
	}

	/* Check that MADC clock is on */
	ret = twl_i2c_read_u8(TWL4030_MODULE_INTBR, &regval, TWL4030_REG_GPBR1);
	if (ret) {
		dev_err(&pdev->dev, "unable to read reg GPBR1 0x%X\n",
				TWL4030_REG_GPBR1);
		goto err_i2c;
	}

	/* If MADC clk is not on, turn it on */
	if (!(regval & TWL4030_GPBR1_MADC_HFCLK_EN)) {
		dev_info(&pdev->dev, "clk disabled, enabling\n");
		regval |= TWL4030_GPBR1_MADC_HFCLK_EN;
		ret = twl_i2c_write_u8(TWL4030_MODULE_INTBR, regval,
				       TWL4030_REG_GPBR1);
		if (ret) {
			dev_err(&pdev->dev, "unable to write reg GPBR1 0x%X\n",
					TWL4030_REG_GPBR1);
			goto err_i2c;
		}
	}

	platform_set_drvdata(pdev, iio_dev);
	mutex_init(&madc->lock);

	irq = platform_get_irq(pdev, 0);
	ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
				   twl4030_madc_threaded_irq_handler,
				   IRQF_TRIGGER_RISING, "twl4030_madc", madc);
	if (ret) {
		dev_err(&pdev->dev, "could not request irq\n");
		goto err_i2c;
	}
	twl4030_madc = madc;

	ret = iio_device_register(iio_dev);
	if (ret) {
		dev_err(&pdev->dev, "could not register iio device\n");
		goto err_i2c;
	}

	return 0;

err_i2c:
	twl4030_madc_set_current_generator(madc, 0, 0);
err_current_generator:
	twl4030_madc_set_power(madc, 0);
	return ret;
}

static int twl4030_madc_remove(struct platform_device *pdev)
{
	struct iio_dev *iio_dev = platform_get_drvdata(pdev);
	struct twl4030_madc_data *madc = iio_priv(iio_dev);

	iio_device_unregister(iio_dev);

	twl4030_madc_set_current_generator(madc, 0, 0);
	twl4030_madc_set_power(madc, 0);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id twl_madc_of_match[] = {
	{ .compatible = "ti,twl4030-madc", },
	{ },
};
MODULE_DEVICE_TABLE(of, twl_madc_of_match);
#endif

static struct platform_driver twl4030_madc_driver = {
	.probe = twl4030_madc_probe,
	.remove = twl4030_madc_remove,
	.driver = {
		   .name = "twl4030_madc",
		   .of_match_table = of_match_ptr(twl_madc_of_match),
	},
};

module_platform_driver(twl4030_madc_driver);

MODULE_DESCRIPTION("TWL4030 ADC driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("J Keerthy");
MODULE_ALIAS("platform:twl4030_madc");
