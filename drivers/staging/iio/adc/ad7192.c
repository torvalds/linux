/*
 * AD7190 AD7192 AD7195 SPI ADC driver
 *
 * Copyright 2011-2012 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/spi/spi.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/delay.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/adc/ad_sigma_delta.h>

#include "ad7192.h"

/* Registers */
#define AD7192_REG_COMM		0 /* Communications Register (WO, 8-bit) */
#define AD7192_REG_STAT		0 /* Status Register	     (RO, 8-bit) */
#define AD7192_REG_MODE		1 /* Mode Register	     (RW, 24-bit */
#define AD7192_REG_CONF		2 /* Configuration Register  (RW, 24-bit) */
#define AD7192_REG_DATA		3 /* Data Register	     (RO, 24/32-bit) */
#define AD7192_REG_ID		4 /* ID Register	     (RO, 8-bit) */
#define AD7192_REG_GPOCON	5 /* GPOCON Register	     (RO, 8-bit) */
#define AD7192_REG_OFFSET	6 /* Offset Register	     (RW, 16-bit
				   * (AD7792)/24-bit (AD7192)) */
#define AD7192_REG_FULLSALE	7 /* Full-Scale Register
				   * (RW, 16-bit (AD7792)/24-bit (AD7192)) */

/* Communications Register Bit Designations (AD7192_REG_COMM) */
#define AD7192_COMM_WEN		BIT(7) /* Write Enable */
#define AD7192_COMM_WRITE	0 /* Write Operation */
#define AD7192_COMM_READ	BIT(6) /* Read Operation */
#define AD7192_COMM_ADDR(x)	(((x) & 0x7) << 3) /* Register Address */
#define AD7192_COMM_CREAD	BIT(2) /* Continuous Read of Data Register */

/* Status Register Bit Designations (AD7192_REG_STAT) */
#define AD7192_STAT_RDY		BIT(7) /* Ready */
#define AD7192_STAT_ERR		BIT(6) /* Error (Overrange, Underrange) */
#define AD7192_STAT_NOREF	BIT(5) /* Error no external reference */
#define AD7192_STAT_PARITY	BIT(4) /* Parity */
#define AD7192_STAT_CH3		BIT(2) /* Channel 3 */
#define AD7192_STAT_CH2		BIT(1) /* Channel 2 */
#define AD7192_STAT_CH1		BIT(0) /* Channel 1 */

/* Mode Register Bit Designations (AD7192_REG_MODE) */
#define AD7192_MODE_SEL(x)	(((x) & 0x7) << 21) /* Operation Mode Select */
#define AD7192_MODE_SEL_MASK	(0x7 << 21) /* Operation Mode Select Mask */
#define AD7192_MODE_DAT_STA	BIT(20) /* Status Register transmission */
#define AD7192_MODE_CLKSRC(x)	(((x) & 0x3) << 18) /* Clock Source Select */
#define AD7192_MODE_SINC3	BIT(15) /* SINC3 Filter Select */
#define AD7192_MODE_ACX		BIT(14) /* AC excitation enable(AD7195 only)*/
#define AD7192_MODE_ENPAR	BIT(13) /* Parity Enable */
#define AD7192_MODE_CLKDIV	BIT(12) /* Clock divide by 2 (AD7190/2 only)*/
#define AD7192_MODE_SCYCLE	BIT(11) /* Single cycle conversion */
#define AD7192_MODE_REJ60	BIT(10) /* 50/60Hz notch filter */
#define AD7192_MODE_RATE(x)	((x) & 0x3FF) /* Filter Update Rate Select */

/* Mode Register: AD7192_MODE_SEL options */
#define AD7192_MODE_CONT		0 /* Continuous Conversion Mode */
#define AD7192_MODE_SINGLE		1 /* Single Conversion Mode */
#define AD7192_MODE_IDLE		2 /* Idle Mode */
#define AD7192_MODE_PWRDN		3 /* Power-Down Mode */
#define AD7192_MODE_CAL_INT_ZERO	4 /* Internal Zero-Scale Calibration */
#define AD7192_MODE_CAL_INT_FULL	5 /* Internal Full-Scale Calibration */
#define AD7192_MODE_CAL_SYS_ZERO	6 /* System Zero-Scale Calibration */
#define AD7192_MODE_CAL_SYS_FULL	7 /* System Full-Scale Calibration */

/* Mode Register: AD7192_MODE_CLKSRC options */
#define AD7192_CLK_EXT_MCLK1_2		0 /* External 4.92 MHz Clock connected
					   * from MCLK1 to MCLK2 */
#define AD7192_CLK_EXT_MCLK2		1 /* External Clock applied to MCLK2 */
#define AD7192_CLK_INT			2 /* Internal 4.92 MHz Clock not
					   * available at the MCLK2 pin */
#define AD7192_CLK_INT_CO		3 /* Internal 4.92 MHz Clock available
					   * at the MCLK2 pin */

/* Configuration Register Bit Designations (AD7192_REG_CONF) */

#define AD7192_CONF_CHOP	BIT(23) /* CHOP enable */
#define AD7192_CONF_REFSEL	BIT(20) /* REFIN1/REFIN2 Reference Select */
#define AD7192_CONF_CHAN(x)	(((1 << (x)) & 0xFF) << 8) /* Channel select */
#define AD7192_CONF_CHAN_MASK	(0xFF << 8) /* Channel select mask */
#define AD7192_CONF_BURN	BIT(7) /* Burnout current enable */
#define AD7192_CONF_REFDET	BIT(6) /* Reference detect enable */
#define AD7192_CONF_BUF		BIT(4) /* Buffered Mode Enable */
#define AD7192_CONF_UNIPOLAR	BIT(3) /* Unipolar/Bipolar Enable */
#define AD7192_CONF_GAIN(x)	((x) & 0x7) /* Gain Select */

#define AD7192_CH_AIN1P_AIN2M	0 /* AIN1(+) - AIN2(-) */
#define AD7192_CH_AIN3P_AIN4M	1 /* AIN3(+) - AIN4(-) */
#define AD7192_CH_TEMP		2 /* Temp Sensor */
#define AD7192_CH_AIN2P_AIN2M	3 /* AIN2(+) - AIN2(-) */
#define AD7192_CH_AIN1		4 /* AIN1 - AINCOM */
#define AD7192_CH_AIN2		5 /* AIN2 - AINCOM */
#define AD7192_CH_AIN3		6 /* AIN3 - AINCOM */
#define AD7192_CH_AIN4		7 /* AIN4 - AINCOM */

/* ID Register Bit Designations (AD7192_REG_ID) */
#define ID_AD7190		0x4
#define ID_AD7192		0x0
#define ID_AD7195		0x6
#define AD7192_ID_MASK		0x0F

/* GPOCON Register Bit Designations (AD7192_REG_GPOCON) */
#define AD7192_GPOCON_BPDSW	BIT(6) /* Bridge power-down switch enable */
#define AD7192_GPOCON_GP32EN	BIT(5) /* Digital Output P3 and P2 enable */
#define AD7192_GPOCON_GP10EN	BIT(4) /* Digital Output P1 and P0 enable */
#define AD7192_GPOCON_P3DAT	BIT(3) /* P3 state */
#define AD7192_GPOCON_P2DAT	BIT(2) /* P2 state */
#define AD7192_GPOCON_P1DAT	BIT(1) /* P1 state */
#define AD7192_GPOCON_P0DAT	BIT(0) /* P0 state */

#define AD7192_INT_FREQ_MHZ	4915200

/* NOTE:
 * The AD7190/2/5 features a dual use data out ready DOUT/RDY output.
 * In order to avoid contentions on the SPI bus, it's therefore necessary
 * to use spi bus locking.
 *
 * The DOUT/RDY output must also be wired to an interrupt capable GPIO.
 */

struct ad7192_state {
	struct regulator		*reg;
	u16				int_vref_mv;
	u32				mclk;
	u32				f_order;
	u32				mode;
	u32				conf;
	u32				scale_avail[8][2];
	u8				gpocon;
	u8				devid;

	struct ad_sigma_delta		sd;
};

static struct ad7192_state *ad_sigma_delta_to_ad7192(struct ad_sigma_delta *sd)
{
	return container_of(sd, struct ad7192_state, sd);
}

static int ad7192_set_channel(struct ad_sigma_delta *sd, unsigned int channel)
{
	struct ad7192_state *st = ad_sigma_delta_to_ad7192(sd);

	st->conf &= ~AD7192_CONF_CHAN_MASK;
	st->conf |= AD7192_CONF_CHAN(channel);

	return ad_sd_write_reg(&st->sd, AD7192_REG_CONF, 3, st->conf);
}

static int ad7192_set_mode(struct ad_sigma_delta *sd,
			   enum ad_sigma_delta_mode mode)
{
	struct ad7192_state *st = ad_sigma_delta_to_ad7192(sd);

	st->mode &= ~AD7192_MODE_SEL_MASK;
	st->mode |= AD7192_MODE_SEL(mode);

	return ad_sd_write_reg(&st->sd, AD7192_REG_MODE, 3, st->mode);
}

static const struct ad_sigma_delta_info ad7192_sigma_delta_info = {
	.set_channel = ad7192_set_channel,
	.set_mode = ad7192_set_mode,
	.has_registers = true,
	.addr_shift = 3,
	.read_mask = BIT(6),
};

static const struct ad_sd_calib_data ad7192_calib_arr[8] = {
	{AD7192_MODE_CAL_INT_ZERO, AD7192_CH_AIN1},
	{AD7192_MODE_CAL_INT_FULL, AD7192_CH_AIN1},
	{AD7192_MODE_CAL_INT_ZERO, AD7192_CH_AIN2},
	{AD7192_MODE_CAL_INT_FULL, AD7192_CH_AIN2},
	{AD7192_MODE_CAL_INT_ZERO, AD7192_CH_AIN3},
	{AD7192_MODE_CAL_INT_FULL, AD7192_CH_AIN3},
	{AD7192_MODE_CAL_INT_ZERO, AD7192_CH_AIN4},
	{AD7192_MODE_CAL_INT_FULL, AD7192_CH_AIN4}
};

static int ad7192_calibrate_all(struct ad7192_state *st)
{
		return ad_sd_calibrate_all(&st->sd, ad7192_calib_arr,
				ARRAY_SIZE(ad7192_calib_arr));
}

static int ad7192_setup(struct ad7192_state *st,
			const struct ad7192_platform_data *pdata)
{
	struct iio_dev *indio_dev = spi_get_drvdata(st->sd.spi);
	unsigned long long scale_uv;
	int i, ret, id;
	u8 ones[6];

	/* reset the serial interface */
	memset(&ones, 0xFF, 6);
	ret = spi_write(st->sd.spi, &ones, 6);
	if (ret < 0)
		goto out;
	usleep_range(500, 1000); /* Wait for at least 500us */

	/* write/read test for device presence */
	ret = ad_sd_read_reg(&st->sd, AD7192_REG_ID, 1, &id);
	if (ret)
		goto out;

	id &= AD7192_ID_MASK;

	if (id != st->devid)
		dev_warn(&st->sd.spi->dev, "device ID query failed (0x%X)\n",
			 id);

	switch (pdata->clock_source_sel) {
	case AD7192_CLK_EXT_MCLK1_2:
	case AD7192_CLK_EXT_MCLK2:
		st->mclk = AD7192_INT_FREQ_MHZ;
		break;
	case AD7192_CLK_INT:
	case AD7192_CLK_INT_CO:
		if (pdata->ext_clk_hz)
			st->mclk = pdata->ext_clk_hz;
		else
			st->mclk = AD7192_INT_FREQ_MHZ;
			break;
	default:
		ret = -EINVAL;
		goto out;
	}

	st->mode = AD7192_MODE_SEL(AD7192_MODE_IDLE) |
		AD7192_MODE_CLKSRC(pdata->clock_source_sel) |
		AD7192_MODE_RATE(480);

	st->conf = AD7192_CONF_GAIN(0);

	if (pdata->rej60_en)
		st->mode |= AD7192_MODE_REJ60;

	if (pdata->sinc3_en)
		st->mode |= AD7192_MODE_SINC3;

	if (pdata->refin2_en && (st->devid != ID_AD7195))
		st->conf |= AD7192_CONF_REFSEL;

	if (pdata->chop_en) {
		st->conf |= AD7192_CONF_CHOP;
		if (pdata->sinc3_en)
			st->f_order = 3; /* SINC 3rd order */
		else
			st->f_order = 4; /* SINC 4th order */
	} else {
		st->f_order = 1;
	}

	if (pdata->buf_en)
		st->conf |= AD7192_CONF_BUF;

	if (pdata->unipolar_en)
		st->conf |= AD7192_CONF_UNIPOLAR;

	if (pdata->burnout_curr_en)
		st->conf |= AD7192_CONF_BURN;

	ret = ad_sd_write_reg(&st->sd, AD7192_REG_MODE, 3, st->mode);
	if (ret)
		goto out;

	ret = ad_sd_write_reg(&st->sd, AD7192_REG_CONF, 3, st->conf);
	if (ret)
		goto out;

	ret = ad7192_calibrate_all(st);
	if (ret)
		goto out;

	/* Populate available ADC input ranges */
	for (i = 0; i < ARRAY_SIZE(st->scale_avail); i++) {
		scale_uv = ((u64)st->int_vref_mv * 100000000)
			>> (indio_dev->channels[0].scan_type.realbits -
			((st->conf & AD7192_CONF_UNIPOLAR) ? 0 : 1));
		scale_uv >>= i;

		st->scale_avail[i][1] = do_div(scale_uv, 100000000) * 10;
		st->scale_avail[i][0] = scale_uv;
	}

	return 0;
out:
	dev_err(&st->sd.spi->dev, "setup failed\n");
	return ret;
}

static ssize_t ad7192_read_frequency(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad7192_state *st = iio_priv(indio_dev);

	return sprintf(buf, "%d\n", st->mclk /
			(st->f_order * 1024 * AD7192_MODE_RATE(st->mode)));
}

static ssize_t ad7192_write_frequency(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad7192_state *st = iio_priv(indio_dev);
	unsigned long lval;
	int div, ret;

	ret = kstrtoul(buf, 10, &lval);
	if (ret)
		return ret;
	if (lval == 0)
		return -EINVAL;

	mutex_lock(&indio_dev->mlock);
	if (iio_buffer_enabled(indio_dev)) {
		mutex_unlock(&indio_dev->mlock);
		return -EBUSY;
	}

	div = st->mclk / (lval * st->f_order * 1024);
	if (div < 1 || div > 1023) {
		ret = -EINVAL;
		goto out;
	}

	st->mode &= ~AD7192_MODE_RATE(-1);
	st->mode |= AD7192_MODE_RATE(div);
	ad_sd_write_reg(&st->sd, AD7192_REG_MODE, 3, st->mode);

out:
	mutex_unlock(&indio_dev->mlock);

	return ret ? ret : len;
}

static IIO_DEV_ATTR_SAMP_FREQ(S_IWUSR | S_IRUGO,
		ad7192_read_frequency,
		ad7192_write_frequency);

static ssize_t
ad7192_show_scale_available(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad7192_state *st = iio_priv(indio_dev);
	int i, len = 0;

	for (i = 0; i < ARRAY_SIZE(st->scale_avail); i++)
		len += sprintf(buf + len, "%d.%09u ", st->scale_avail[i][0],
			       st->scale_avail[i][1]);

	len += sprintf(buf + len, "\n");

	return len;
}

static IIO_DEVICE_ATTR_NAMED(in_v_m_v_scale_available,
			     in_voltage-voltage_scale_available,
			     S_IRUGO, ad7192_show_scale_available, NULL, 0);

static IIO_DEVICE_ATTR(in_voltage_scale_available, S_IRUGO,
		       ad7192_show_scale_available, NULL, 0);

static ssize_t ad7192_show_ac_excitation(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad7192_state *st = iio_priv(indio_dev);

	return sprintf(buf, "%d\n", !!(st->mode & AD7192_MODE_ACX));
}

static ssize_t ad7192_show_bridge_switch(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad7192_state *st = iio_priv(indio_dev);

	return sprintf(buf, "%d\n", !!(st->gpocon & AD7192_GPOCON_BPDSW));
}

static ssize_t ad7192_set(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf,
			  size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad7192_state *st = iio_priv(indio_dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int ret;
	bool val;

	ret = strtobool(buf, &val);
	if (ret < 0)
		return ret;

	mutex_lock(&indio_dev->mlock);
	if (iio_buffer_enabled(indio_dev)) {
		mutex_unlock(&indio_dev->mlock);
		return -EBUSY;
	}

	switch ((u32)this_attr->address) {
	case AD7192_REG_GPOCON:
		if (val)
			st->gpocon |= AD7192_GPOCON_BPDSW;
		else
			st->gpocon &= ~AD7192_GPOCON_BPDSW;

		ad_sd_write_reg(&st->sd, AD7192_REG_GPOCON, 1, st->gpocon);
		break;
	case AD7192_REG_MODE:
		if (val)
			st->mode |= AD7192_MODE_ACX;
		else
			st->mode &= ~AD7192_MODE_ACX;

		ad_sd_write_reg(&st->sd, AD7192_REG_MODE, 3, st->mode);
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&indio_dev->mlock);

	return ret ? ret : len;
}

static IIO_DEVICE_ATTR(bridge_switch_en, S_IRUGO | S_IWUSR,
		       ad7192_show_bridge_switch, ad7192_set,
		       AD7192_REG_GPOCON);

static IIO_DEVICE_ATTR(ac_excitation_en, S_IRUGO | S_IWUSR,
		       ad7192_show_ac_excitation, ad7192_set,
		       AD7192_REG_MODE);

static struct attribute *ad7192_attributes[] = {
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	&iio_dev_attr_in_v_m_v_scale_available.dev_attr.attr,
	&iio_dev_attr_in_voltage_scale_available.dev_attr.attr,
	&iio_dev_attr_bridge_switch_en.dev_attr.attr,
	&iio_dev_attr_ac_excitation_en.dev_attr.attr,
	NULL
};

static const struct attribute_group ad7192_attribute_group = {
	.attrs = ad7192_attributes,
};

static struct attribute *ad7195_attributes[] = {
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	&iio_dev_attr_in_v_m_v_scale_available.dev_attr.attr,
	&iio_dev_attr_in_voltage_scale_available.dev_attr.attr,
	&iio_dev_attr_bridge_switch_en.dev_attr.attr,
	NULL
};

static const struct attribute_group ad7195_attribute_group = {
	.attrs = ad7195_attributes,
};

static unsigned int ad7192_get_temp_scale(bool unipolar)
{
	return unipolar ? 2815 * 2 : 2815;
}

static int ad7192_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long m)
{
	struct ad7192_state *st = iio_priv(indio_dev);
	bool unipolar = !!(st->conf & AD7192_CONF_UNIPOLAR);

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		return ad_sigma_delta_single_conversion(indio_dev, chan, val);
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_VOLTAGE:
			mutex_lock(&indio_dev->mlock);
			*val = st->scale_avail[AD7192_CONF_GAIN(st->conf)][0];
			*val2 = st->scale_avail[AD7192_CONF_GAIN(st->conf)][1];
			mutex_unlock(&indio_dev->mlock);
			return IIO_VAL_INT_PLUS_NANO;
		case IIO_TEMP:
			*val = 0;
			*val2 = 1000000000 / ad7192_get_temp_scale(unipolar);
			return IIO_VAL_INT_PLUS_NANO;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_OFFSET:
		if (!unipolar)
			*val = -(1 << (chan->scan_type.realbits - 1));
		else
			*val = 0;
		/* Kelvin to Celsius */
		if (chan->type == IIO_TEMP)
			*val -= 273 * ad7192_get_temp_scale(unipolar);
		return IIO_VAL_INT;
	}

	return -EINVAL;
}

static int ad7192_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val,
			    int val2,
			    long mask)
{
	struct ad7192_state *st = iio_priv(indio_dev);
	int ret, i;
	unsigned int tmp;

	mutex_lock(&indio_dev->mlock);
	if (iio_buffer_enabled(indio_dev)) {
		mutex_unlock(&indio_dev->mlock);
		return -EBUSY;
	}

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		ret = -EINVAL;
		for (i = 0; i < ARRAY_SIZE(st->scale_avail); i++)
			if (val2 == st->scale_avail[i][1]) {
				ret = 0;
				tmp = st->conf;
				st->conf &= ~AD7192_CONF_GAIN(-1);
				st->conf |= AD7192_CONF_GAIN(i);
				if (tmp == st->conf)
					break;
				ad_sd_write_reg(&st->sd, AD7192_REG_CONF,
						3, st->conf);
				ad7192_calibrate_all(st);
				break;
			}
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&indio_dev->mlock);

	return ret;
}

static int ad7192_write_raw_get_fmt(struct iio_dev *indio_dev,
				    struct iio_chan_spec const *chan,
				    long mask)
{
	return IIO_VAL_INT_PLUS_NANO;
}

static const struct iio_info ad7192_info = {
	.read_raw = &ad7192_read_raw,
	.write_raw = &ad7192_write_raw,
	.write_raw_get_fmt = &ad7192_write_raw_get_fmt,
	.attrs = &ad7192_attribute_group,
	.validate_trigger = ad_sd_validate_trigger,
	.driver_module = THIS_MODULE,
};

static const struct iio_info ad7195_info = {
	.read_raw = &ad7192_read_raw,
	.write_raw = &ad7192_write_raw,
	.write_raw_get_fmt = &ad7192_write_raw_get_fmt,
	.attrs = &ad7195_attribute_group,
	.validate_trigger = ad_sd_validate_trigger,
	.driver_module = THIS_MODULE,
};

static const struct iio_chan_spec ad7192_channels[] = {
	AD_SD_DIFF_CHANNEL(0, 1, 2, AD7192_CH_AIN1P_AIN2M, 24, 32, 0),
	AD_SD_DIFF_CHANNEL(1, 3, 4, AD7192_CH_AIN3P_AIN4M, 24, 32, 0),
	AD_SD_TEMP_CHANNEL(2, AD7192_CH_TEMP, 24, 32, 0),
	AD_SD_SHORTED_CHANNEL(3, 2, AD7192_CH_AIN2P_AIN2M, 24, 32, 0),
	AD_SD_CHANNEL(4, 1, AD7192_CH_AIN1, 24, 32, 0),
	AD_SD_CHANNEL(5, 2, AD7192_CH_AIN2, 24, 32, 0),
	AD_SD_CHANNEL(6, 3, AD7192_CH_AIN3, 24, 32, 0),
	AD_SD_CHANNEL(7, 4, AD7192_CH_AIN4, 24, 32, 0),
	IIO_CHAN_SOFT_TIMESTAMP(8),
};

static int ad7192_probe(struct spi_device *spi)
{
	const struct ad7192_platform_data *pdata = spi->dev.platform_data;
	struct ad7192_state *st;
	struct iio_dev *indio_dev;
	int ret, voltage_uv = 0;

	if (!pdata) {
		dev_err(&spi->dev, "no platform data?\n");
		return -ENODEV;
	}

	if (!spi->irq) {
		dev_err(&spi->dev, "no IRQ?\n");
		return -ENODEV;
	}

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	st->reg = devm_regulator_get(&spi->dev, "vcc");
	if (!IS_ERR(st->reg)) {
		ret = regulator_enable(st->reg);
		if (ret)
			return ret;

		voltage_uv = regulator_get_voltage(st->reg);
	}

	if (pdata->vref_mv)
		st->int_vref_mv = pdata->vref_mv;
	else if (voltage_uv)
		st->int_vref_mv = voltage_uv / 1000;
	else
		dev_warn(&spi->dev, "reference voltage undefined\n");

	spi_set_drvdata(spi, indio_dev);
	st->devid = spi_get_device_id(spi)->driver_data;
	indio_dev->dev.parent = &spi->dev;
	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = ad7192_channels;
	indio_dev->num_channels = ARRAY_SIZE(ad7192_channels);
	if (st->devid == ID_AD7195)
		indio_dev->info = &ad7195_info;
	else
		indio_dev->info = &ad7192_info;

	ad_sd_init(&st->sd, indio_dev, spi, &ad7192_sigma_delta_info);

	ret = ad_sd_setup_buffer_and_trigger(indio_dev);
	if (ret)
		goto error_disable_reg;

	ret = ad7192_setup(st, pdata);
	if (ret)
		goto error_remove_trigger;

	ret = iio_device_register(indio_dev);
	if (ret < 0)
		goto error_remove_trigger;
	return 0;

error_remove_trigger:
	ad_sd_cleanup_buffer_and_trigger(indio_dev);
error_disable_reg:
	if (!IS_ERR(st->reg))
		regulator_disable(st->reg);

	return ret;
}

static int ad7192_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct ad7192_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	ad_sd_cleanup_buffer_and_trigger(indio_dev);

	if (!IS_ERR(st->reg))
		regulator_disable(st->reg);

	return 0;
}

static const struct spi_device_id ad7192_id[] = {
	{"ad7190", ID_AD7190},
	{"ad7192", ID_AD7192},
	{"ad7195", ID_AD7195},
	{}
};
MODULE_DEVICE_TABLE(spi, ad7192_id);

static struct spi_driver ad7192_driver = {
	.driver = {
		.name	= "ad7192",
	},
	.probe		= ad7192_probe,
	.remove		= ad7192_remove,
	.id_table	= ad7192_id,
};
module_spi_driver(ad7192_driver);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("Analog Devices AD7190, AD7192, AD7195 ADC");
MODULE_LICENSE("GPL v2");
