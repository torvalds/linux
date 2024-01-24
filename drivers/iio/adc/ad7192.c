// SPDX-License-Identifier: GPL-2.0
/*
 * AD7190 AD7192 AD7193 AD7195 SPI ADC driver
 *
 * Copyright 2011-2015 Analog Devices Inc.
 */

#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/spi/spi.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/of_device.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/adc/ad_sigma_delta.h>

/* Registers */
#define AD7192_REG_COMM		0 /* Communications Register (WO, 8-bit) */
#define AD7192_REG_STAT		0 /* Status Register	     (RO, 8-bit) */
#define AD7192_REG_MODE		1 /* Mode Register	     (RW, 24-bit */
#define AD7192_REG_CONF		2 /* Configuration Register  (RW, 24-bit) */
#define AD7192_REG_DATA		3 /* Data Register	     (RO, 24/32-bit) */
#define AD7192_REG_ID		4 /* ID Register	     (RO, 8-bit) */
#define AD7192_REG_GPOCON	5 /* GPOCON Register	     (RO, 8-bit) */
#define AD7192_REG_OFFSET	6 /* Offset Register	     (RW, 16-bit */
				  /* (AD7792)/24-bit (AD7192)) */
#define AD7192_REG_FULLSALE	7 /* Full-Scale Register */
				  /* (RW, 16-bit (AD7792)/24-bit (AD7192)) */

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
#define AD7192_MODE_STA(x)	(((x) & 0x1) << 20) /* Status Register transmission */
#define AD7192_MODE_STA_MASK	BIT(20) /* Status Register transmission Mask */
#define AD7192_MODE_CLKSRC(x)	(((x) & 0x3) << 18) /* Clock Source Select */
#define AD7192_MODE_SINC3	BIT(15) /* SINC3 Filter Select */
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
#define AD7192_CLK_EXT_MCLK1_2		0 /* External 4.92 MHz Clock connected*/
					  /* from MCLK1 to MCLK2 */
#define AD7192_CLK_EXT_MCLK2		1 /* External Clock applied to MCLK2 */
#define AD7192_CLK_INT			2 /* Internal 4.92 MHz Clock not */
					  /* available at the MCLK2 pin */
#define AD7192_CLK_INT_CO		3 /* Internal 4.92 MHz Clock available*/
					  /* at the MCLK2 pin */

/* Configuration Register Bit Designations (AD7192_REG_CONF) */

#define AD7192_CONF_CHOP	BIT(23) /* CHOP enable */
#define AD7192_CONF_ACX		BIT(22) /* AC excitation enable(AD7195 only) */
#define AD7192_CONF_REFSEL	BIT(20) /* REFIN1/REFIN2 Reference Select */
#define AD7192_CONF_CHAN(x)	((x) << 8) /* Channel select */
#define AD7192_CONF_CHAN_MASK	(0x7FF << 8) /* Channel select mask */
#define AD7192_CONF_BURN	BIT(7) /* Burnout current enable */
#define AD7192_CONF_REFDET	BIT(6) /* Reference detect enable */
#define AD7192_CONF_BUF		BIT(4) /* Buffered Mode Enable */
#define AD7192_CONF_UNIPOLAR	BIT(3) /* Unipolar/Bipolar Enable */
#define AD7192_CONF_GAIN(x)	((x) & 0x7) /* Gain Select */

#define AD7192_CH_AIN1P_AIN2M	BIT(0) /* AIN1(+) - AIN2(-) */
#define AD7192_CH_AIN3P_AIN4M	BIT(1) /* AIN3(+) - AIN4(-) */
#define AD7192_CH_TEMP		BIT(2) /* Temp Sensor */
#define AD7192_CH_AIN2P_AIN2M	BIT(3) /* AIN2(+) - AIN2(-) */
#define AD7192_CH_AIN1		BIT(4) /* AIN1 - AINCOM */
#define AD7192_CH_AIN2		BIT(5) /* AIN2 - AINCOM */
#define AD7192_CH_AIN3		BIT(6) /* AIN3 - AINCOM */
#define AD7192_CH_AIN4		BIT(7) /* AIN4 - AINCOM */

#define AD7193_CH_AIN1P_AIN2M	0x001  /* AIN1(+) - AIN2(-) */
#define AD7193_CH_AIN3P_AIN4M	0x002  /* AIN3(+) - AIN4(-) */
#define AD7193_CH_AIN5P_AIN6M	0x004  /* AIN5(+) - AIN6(-) */
#define AD7193_CH_AIN7P_AIN8M	0x008  /* AIN7(+) - AIN8(-) */
#define AD7193_CH_TEMP		0x100 /* Temp senseor */
#define AD7193_CH_AIN2P_AIN2M	0x200 /* AIN2(+) - AIN2(-) */
#define AD7193_CH_AIN1		0x401 /* AIN1 - AINCOM */
#define AD7193_CH_AIN2		0x402 /* AIN2 - AINCOM */
#define AD7193_CH_AIN3		0x404 /* AIN3 - AINCOM */
#define AD7193_CH_AIN4		0x408 /* AIN4 - AINCOM */
#define AD7193_CH_AIN5		0x410 /* AIN5 - AINCOM */
#define AD7193_CH_AIN6		0x420 /* AIN6 - AINCOM */
#define AD7193_CH_AIN7		0x440 /* AIN7 - AINCOM */
#define AD7193_CH_AIN8		0x480 /* AIN7 - AINCOM */
#define AD7193_CH_AINCOM	0x600 /* AINCOM - AINCOM */

/* ID Register Bit Designations (AD7192_REG_ID) */
#define CHIPID_AD7190		0x4
#define CHIPID_AD7192		0x0
#define CHIPID_AD7193		0x2
#define CHIPID_AD7195		0x6
#define AD7192_ID_MASK		0x0F

/* GPOCON Register Bit Designations (AD7192_REG_GPOCON) */
#define AD7192_GPOCON_BPDSW	BIT(6) /* Bridge power-down switch enable */
#define AD7192_GPOCON_GP32EN	BIT(5) /* Digital Output P3 and P2 enable */
#define AD7192_GPOCON_GP10EN	BIT(4) /* Digital Output P1 and P0 enable */
#define AD7192_GPOCON_P3DAT	BIT(3) /* P3 state */
#define AD7192_GPOCON_P2DAT	BIT(2) /* P2 state */
#define AD7192_GPOCON_P1DAT	BIT(1) /* P1 state */
#define AD7192_GPOCON_P0DAT	BIT(0) /* P0 state */

#define AD7192_EXT_FREQ_MHZ_MIN	2457600
#define AD7192_EXT_FREQ_MHZ_MAX	5120000
#define AD7192_INT_FREQ_MHZ	4915200

#define AD7192_NO_SYNC_FILTER	1
#define AD7192_SYNC3_FILTER	3
#define AD7192_SYNC4_FILTER	4

/* NOTE:
 * The AD7190/2/5 features a dual use data out ready DOUT/RDY output.
 * In order to avoid contentions on the SPI bus, it's therefore necessary
 * to use spi bus locking.
 *
 * The DOUT/RDY output must also be wired to an interrupt capable GPIO.
 */

enum {
	AD7192_SYSCALIB_ZERO_SCALE,
	AD7192_SYSCALIB_FULL_SCALE,
};

enum {
	ID_AD7190,
	ID_AD7192,
	ID_AD7193,
	ID_AD7195,
};

struct ad7192_chip_info {
	unsigned int			chip_id;
	const char			*name;
};

struct ad7192_state {
	const struct ad7192_chip_info	*chip_info;
	struct regulator		*avdd;
	struct regulator		*vref;
	struct clk			*mclk;
	u16				int_vref_mv;
	u32				fclk;
	u32				f_order;
	u32				mode;
	u32				conf;
	u32				scale_avail[8][2];
	u8				gpocon;
	u8				clock_sel;
	struct mutex			lock;	/* protect sensor state */
	u8				syscalib_mode[8];

	struct ad_sigma_delta		sd;
};

static const char * const ad7192_syscalib_modes[] = {
	[AD7192_SYSCALIB_ZERO_SCALE] = "zero_scale",
	[AD7192_SYSCALIB_FULL_SCALE] = "full_scale",
};

static int ad7192_set_syscalib_mode(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    unsigned int mode)
{
	struct ad7192_state *st = iio_priv(indio_dev);

	st->syscalib_mode[chan->channel] = mode;

	return 0;
}

static int ad7192_get_syscalib_mode(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan)
{
	struct ad7192_state *st = iio_priv(indio_dev);

	return st->syscalib_mode[chan->channel];
}

static ssize_t ad7192_write_syscalib(struct iio_dev *indio_dev,
				     uintptr_t private,
				     const struct iio_chan_spec *chan,
				     const char *buf, size_t len)
{
	struct ad7192_state *st = iio_priv(indio_dev);
	bool sys_calib;
	int ret, temp;

	ret = kstrtobool(buf, &sys_calib);
	if (ret)
		return ret;

	temp = st->syscalib_mode[chan->channel];
	if (sys_calib) {
		if (temp == AD7192_SYSCALIB_ZERO_SCALE)
			ret = ad_sd_calibrate(&st->sd, AD7192_MODE_CAL_SYS_ZERO,
					      chan->address);
		else
			ret = ad_sd_calibrate(&st->sd, AD7192_MODE_CAL_SYS_FULL,
					      chan->address);
	}

	return ret ? ret : len;
}

static const struct iio_enum ad7192_syscalib_mode_enum = {
	.items = ad7192_syscalib_modes,
	.num_items = ARRAY_SIZE(ad7192_syscalib_modes),
	.set = ad7192_set_syscalib_mode,
	.get = ad7192_get_syscalib_mode
};

static const struct iio_chan_spec_ext_info ad7192_calibsys_ext_info[] = {
	{
		.name = "sys_calibration",
		.write = ad7192_write_syscalib,
		.shared = IIO_SEPARATE,
	},
	IIO_ENUM("sys_calibration_mode", IIO_SEPARATE,
		 &ad7192_syscalib_mode_enum),
	IIO_ENUM_AVAILABLE("sys_calibration_mode", IIO_SHARED_BY_TYPE,
			   &ad7192_syscalib_mode_enum),
	{}
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

static int ad7192_append_status(struct ad_sigma_delta *sd, bool append)
{
	struct ad7192_state *st = ad_sigma_delta_to_ad7192(sd);
	unsigned int mode = st->mode;
	int ret;

	mode &= ~AD7192_MODE_STA_MASK;
	mode |= AD7192_MODE_STA(append);

	ret = ad_sd_write_reg(&st->sd, AD7192_REG_MODE, 3, mode);
	if (ret < 0)
		return ret;

	st->mode = mode;

	return 0;
}

static int ad7192_disable_all(struct ad_sigma_delta *sd)
{
	struct ad7192_state *st = ad_sigma_delta_to_ad7192(sd);
	u32 conf = st->conf;
	int ret;

	conf &= ~AD7192_CONF_CHAN_MASK;

	ret = ad_sd_write_reg(&st->sd, AD7192_REG_CONF, 3, conf);
	if (ret < 0)
		return ret;

	st->conf = conf;

	return 0;
}

static const struct ad_sigma_delta_info ad7192_sigma_delta_info = {
	.set_channel = ad7192_set_channel,
	.append_status = ad7192_append_status,
	.disable_all = ad7192_disable_all,
	.set_mode = ad7192_set_mode,
	.has_registers = true,
	.addr_shift = 3,
	.read_mask = BIT(6),
	.status_ch_mask = GENMASK(3, 0),
	.num_slots = 4,
	.irq_flags = IRQF_TRIGGER_FALLING,
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

static inline bool ad7192_valid_external_frequency(u32 freq)
{
	return (freq >= AD7192_EXT_FREQ_MHZ_MIN &&
		freq <= AD7192_EXT_FREQ_MHZ_MAX);
}

static int ad7192_of_clock_select(struct ad7192_state *st)
{
	struct device_node *np = st->sd.spi->dev.of_node;
	unsigned int clock_sel;

	clock_sel = AD7192_CLK_INT;

	/* use internal clock */
	if (!st->mclk) {
		if (of_property_read_bool(np, "adi,int-clock-output-enable"))
			clock_sel = AD7192_CLK_INT_CO;
	} else {
		if (of_property_read_bool(np, "adi,clock-xtal"))
			clock_sel = AD7192_CLK_EXT_MCLK1_2;
		else
			clock_sel = AD7192_CLK_EXT_MCLK2;
	}

	return clock_sel;
}

static int ad7192_setup(struct iio_dev *indio_dev, struct device_node *np)
{
	struct ad7192_state *st = iio_priv(indio_dev);
	bool rej60_en, refin2_en;
	bool buf_en, bipolar, burnout_curr_en;
	unsigned long long scale_uv;
	int i, ret, id;

	/* reset the serial interface */
	ret = ad_sd_reset(&st->sd, 48);
	if (ret < 0)
		return ret;
	usleep_range(500, 1000); /* Wait for at least 500us */

	/* write/read test for device presence */
	ret = ad_sd_read_reg(&st->sd, AD7192_REG_ID, 1, &id);
	if (ret)
		return ret;

	id &= AD7192_ID_MASK;

	if (id != st->chip_info->chip_id)
		dev_warn(&st->sd.spi->dev, "device ID query failed (0x%X)\n",
			 id);

	st->mode = AD7192_MODE_SEL(AD7192_MODE_IDLE) |
		AD7192_MODE_CLKSRC(st->clock_sel) |
		AD7192_MODE_RATE(480);

	st->conf = AD7192_CONF_GAIN(0);

	rej60_en = of_property_read_bool(np, "adi,rejection-60-Hz-enable");
	if (rej60_en)
		st->mode |= AD7192_MODE_REJ60;

	refin2_en = of_property_read_bool(np, "adi,refin2-pins-enable");
	if (refin2_en && st->chip_info->chip_id != CHIPID_AD7195)
		st->conf |= AD7192_CONF_REFSEL;

	st->conf &= ~AD7192_CONF_CHOP;
	st->f_order = AD7192_NO_SYNC_FILTER;

	buf_en = of_property_read_bool(np, "adi,buffer-enable");
	if (buf_en)
		st->conf |= AD7192_CONF_BUF;

	bipolar = of_property_read_bool(np, "bipolar");
	if (!bipolar)
		st->conf |= AD7192_CONF_UNIPOLAR;

	burnout_curr_en = of_property_read_bool(np,
						"adi,burnout-currents-enable");
	if (burnout_curr_en && buf_en) {
		st->conf |= AD7192_CONF_BURN;
	} else if (burnout_curr_en) {
		dev_warn(&st->sd.spi->dev,
			 "Can't enable burnout currents: see CHOP or buffer\n");
	}

	ret = ad_sd_write_reg(&st->sd, AD7192_REG_MODE, 3, st->mode);
	if (ret)
		return ret;

	ret = ad_sd_write_reg(&st->sd, AD7192_REG_CONF, 3, st->conf);
	if (ret)
		return ret;

	ret = ad7192_calibrate_all(st);
	if (ret)
		return ret;

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
}

static ssize_t ad7192_show_ac_excitation(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad7192_state *st = iio_priv(indio_dev);

	return sysfs_emit(buf, "%d\n", !!(st->conf & AD7192_CONF_ACX));
}

static ssize_t ad7192_show_bridge_switch(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad7192_state *st = iio_priv(indio_dev);

	return sysfs_emit(buf, "%d\n", !!(st->gpocon & AD7192_GPOCON_BPDSW));
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

	ret = kstrtobool(buf, &val);
	if (ret < 0)
		return ret;

	ret = iio_device_claim_direct_mode(indio_dev);
	if (ret)
		return ret;

	switch ((u32)this_attr->address) {
	case AD7192_REG_GPOCON:
		if (val)
			st->gpocon |= AD7192_GPOCON_BPDSW;
		else
			st->gpocon &= ~AD7192_GPOCON_BPDSW;

		ad_sd_write_reg(&st->sd, AD7192_REG_GPOCON, 1, st->gpocon);
		break;
	case AD7192_REG_CONF:
		if (val)
			st->conf |= AD7192_CONF_ACX;
		else
			st->conf &= ~AD7192_CONF_ACX;

		ad_sd_write_reg(&st->sd, AD7192_REG_CONF, 3, st->conf);
		break;
	default:
		ret = -EINVAL;
	}

	iio_device_release_direct_mode(indio_dev);

	return ret ? ret : len;
}

static void ad7192_get_available_filter_freq(struct ad7192_state *st,
						    int *freq)
{
	unsigned int fadc;

	/* Formulas for filter at page 25 of the datasheet */
	fadc = DIV_ROUND_CLOSEST(st->fclk,
				 AD7192_SYNC4_FILTER * AD7192_MODE_RATE(st->mode));
	freq[0] = DIV_ROUND_CLOSEST(fadc * 240, 1024);

	fadc = DIV_ROUND_CLOSEST(st->fclk,
				 AD7192_SYNC3_FILTER * AD7192_MODE_RATE(st->mode));
	freq[1] = DIV_ROUND_CLOSEST(fadc * 240, 1024);

	fadc = DIV_ROUND_CLOSEST(st->fclk, AD7192_MODE_RATE(st->mode));
	freq[2] = DIV_ROUND_CLOSEST(fadc * 230, 1024);
	freq[3] = DIV_ROUND_CLOSEST(fadc * 272, 1024);
}

static ssize_t ad7192_show_filter_avail(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad7192_state *st = iio_priv(indio_dev);
	unsigned int freq_avail[4], i;
	size_t len = 0;

	ad7192_get_available_filter_freq(st, freq_avail);

	for (i = 0; i < ARRAY_SIZE(freq_avail); i++)
		len += scnprintf(buf + len, PAGE_SIZE - len,
				 "%d.%d ", freq_avail[i] / 1000,
				 freq_avail[i] % 1000);

	buf[len - 1] = '\n';

	return len;
}

static IIO_DEVICE_ATTR(filter_low_pass_3db_frequency_available,
		       0444, ad7192_show_filter_avail, NULL, 0);

static IIO_DEVICE_ATTR(bridge_switch_en, 0644,
		       ad7192_show_bridge_switch, ad7192_set,
		       AD7192_REG_GPOCON);

static IIO_DEVICE_ATTR(ac_excitation_en, 0644,
		       ad7192_show_ac_excitation, ad7192_set,
		       AD7192_REG_CONF);

static struct attribute *ad7192_attributes[] = {
	&iio_dev_attr_filter_low_pass_3db_frequency_available.dev_attr.attr,
	&iio_dev_attr_bridge_switch_en.dev_attr.attr,
	NULL
};

static const struct attribute_group ad7192_attribute_group = {
	.attrs = ad7192_attributes,
};

static struct attribute *ad7195_attributes[] = {
	&iio_dev_attr_filter_low_pass_3db_frequency_available.dev_attr.attr,
	&iio_dev_attr_bridge_switch_en.dev_attr.attr,
	&iio_dev_attr_ac_excitation_en.dev_attr.attr,
	NULL
};

static const struct attribute_group ad7195_attribute_group = {
	.attrs = ad7195_attributes,
};

static unsigned int ad7192_get_temp_scale(bool unipolar)
{
	return unipolar ? 2815 * 2 : 2815;
}

static int ad7192_set_3db_filter_freq(struct ad7192_state *st,
				      int val, int val2)
{
	int freq_avail[4], i, ret, freq;
	unsigned int diff_new, diff_old;
	int idx = 0;

	diff_old = U32_MAX;
	freq = val * 1000 + val2;

	ad7192_get_available_filter_freq(st, freq_avail);

	for (i = 0; i < ARRAY_SIZE(freq_avail); i++) {
		diff_new = abs(freq - freq_avail[i]);
		if (diff_new < diff_old) {
			diff_old = diff_new;
			idx = i;
		}
	}

	switch (idx) {
	case 0:
		st->f_order = AD7192_SYNC4_FILTER;
		st->mode &= ~AD7192_MODE_SINC3;

		st->conf |= AD7192_CONF_CHOP;
		break;
	case 1:
		st->f_order = AD7192_SYNC3_FILTER;
		st->mode |= AD7192_MODE_SINC3;

		st->conf |= AD7192_CONF_CHOP;
		break;
	case 2:
		st->f_order = AD7192_NO_SYNC_FILTER;
		st->mode &= ~AD7192_MODE_SINC3;

		st->conf &= ~AD7192_CONF_CHOP;
		break;
	case 3:
		st->f_order = AD7192_NO_SYNC_FILTER;
		st->mode |= AD7192_MODE_SINC3;

		st->conf &= ~AD7192_CONF_CHOP;
		break;
	}

	ret = ad_sd_write_reg(&st->sd, AD7192_REG_MODE, 3, st->mode);
	if (ret < 0)
		return ret;

	return ad_sd_write_reg(&st->sd, AD7192_REG_CONF, 3, st->conf);
}

static int ad7192_get_3db_filter_freq(struct ad7192_state *st)
{
	unsigned int fadc;

	fadc = DIV_ROUND_CLOSEST(st->fclk,
				 st->f_order * AD7192_MODE_RATE(st->mode));

	if (st->conf & AD7192_CONF_CHOP)
		return DIV_ROUND_CLOSEST(fadc * 240, 1024);
	if (st->mode & AD7192_MODE_SINC3)
		return DIV_ROUND_CLOSEST(fadc * 272, 1024);
	else
		return DIV_ROUND_CLOSEST(fadc * 230, 1024);
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
			mutex_lock(&st->lock);
			*val = st->scale_avail[AD7192_CONF_GAIN(st->conf)][0];
			*val2 = st->scale_avail[AD7192_CONF_GAIN(st->conf)][1];
			mutex_unlock(&st->lock);
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
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = st->fclk /
			(st->f_order * 1024 * AD7192_MODE_RATE(st->mode));
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		*val = ad7192_get_3db_filter_freq(st);
		*val2 = 1000;
		return IIO_VAL_FRACTIONAL;
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
	int ret, i, div;
	unsigned int tmp;

	ret = iio_device_claim_direct_mode(indio_dev);
	if (ret)
		return ret;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		ret = -EINVAL;
		mutex_lock(&st->lock);
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
		mutex_unlock(&st->lock);
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		if (!val) {
			ret = -EINVAL;
			break;
		}

		div = st->fclk / (val * st->f_order * 1024);
		if (div < 1 || div > 1023) {
			ret = -EINVAL;
			break;
		}

		st->mode &= ~AD7192_MODE_RATE(-1);
		st->mode |= AD7192_MODE_RATE(div);
		ad_sd_write_reg(&st->sd, AD7192_REG_MODE, 3, st->mode);
		break;
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		ret = ad7192_set_3db_filter_freq(st, val, val2 / 1000);
		break;
	default:
		ret = -EINVAL;
	}

	iio_device_release_direct_mode(indio_dev);

	return ret;
}

static int ad7192_write_raw_get_fmt(struct iio_dev *indio_dev,
				    struct iio_chan_spec const *chan,
				    long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		return IIO_VAL_INT_PLUS_NANO;
	case IIO_CHAN_INFO_SAMP_FREQ:
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int ad7192_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     const int **vals, int *type, int *length,
			     long mask)
{
	struct ad7192_state *st = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		*vals = (int *)st->scale_avail;
		*type = IIO_VAL_INT_PLUS_NANO;
		/* Values are stored in a 2D matrix  */
		*length = ARRAY_SIZE(st->scale_avail) * 2;

		return IIO_AVAIL_LIST;
	}

	return -EINVAL;
}

static int ad7192_update_scan_mode(struct iio_dev *indio_dev, const unsigned long *scan_mask)
{
	struct ad7192_state *st = iio_priv(indio_dev);
	u32 conf = st->conf;
	int ret;
	int i;

	conf &= ~AD7192_CONF_CHAN_MASK;
	for_each_set_bit(i, scan_mask, 8)
		conf |= AD7192_CONF_CHAN(i);

	ret = ad_sd_write_reg(&st->sd, AD7192_REG_CONF, 3, conf);
	if (ret < 0)
		return ret;

	st->conf = conf;

	return 0;
}

static const struct iio_info ad7192_info = {
	.read_raw = ad7192_read_raw,
	.write_raw = ad7192_write_raw,
	.write_raw_get_fmt = ad7192_write_raw_get_fmt,
	.read_avail = ad7192_read_avail,
	.attrs = &ad7192_attribute_group,
	.validate_trigger = ad_sd_validate_trigger,
	.update_scan_mode = ad7192_update_scan_mode,
};

static const struct iio_info ad7195_info = {
	.read_raw = ad7192_read_raw,
	.write_raw = ad7192_write_raw,
	.write_raw_get_fmt = ad7192_write_raw_get_fmt,
	.read_avail = ad7192_read_avail,
	.attrs = &ad7195_attribute_group,
	.validate_trigger = ad_sd_validate_trigger,
	.update_scan_mode = ad7192_update_scan_mode,
};

#define __AD719x_CHANNEL(_si, _channel1, _channel2, _address, _extend_name, \
	_type, _mask_type_av, _ext_info) \
	{ \
		.type = (_type), \
		.differential = ((_channel2) == -1 ? 0 : 1), \
		.indexed = 1, \
		.channel = (_channel1), \
		.channel2 = (_channel2), \
		.address = (_address), \
		.extend_name = (_extend_name), \
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
			BIT(IIO_CHAN_INFO_OFFSET), \
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ) | \
			BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY), \
		.info_mask_shared_by_type_available = (_mask_type_av), \
		.ext_info = (_ext_info), \
		.scan_index = (_si), \
		.scan_type = { \
			.sign = 'u', \
			.realbits = 24, \
			.storagebits = 32, \
			.endianness = IIO_BE, \
		}, \
	}

#define AD719x_DIFF_CHANNEL(_si, _channel1, _channel2, _address) \
	__AD719x_CHANNEL(_si, _channel1, _channel2, _address, NULL, \
		IIO_VOLTAGE, BIT(IIO_CHAN_INFO_SCALE), \
		ad7192_calibsys_ext_info)

#define AD719x_CHANNEL(_si, _channel1, _address) \
	__AD719x_CHANNEL(_si, _channel1, -1, _address, NULL, IIO_VOLTAGE, \
		BIT(IIO_CHAN_INFO_SCALE), ad7192_calibsys_ext_info)

#define AD719x_TEMP_CHANNEL(_si, _address) \
	__AD719x_CHANNEL(_si, 0, -1, _address, NULL, IIO_TEMP, 0, NULL)

static const struct iio_chan_spec ad7192_channels[] = {
	AD719x_DIFF_CHANNEL(0, 1, 2, AD7192_CH_AIN1P_AIN2M),
	AD719x_DIFF_CHANNEL(1, 3, 4, AD7192_CH_AIN3P_AIN4M),
	AD719x_TEMP_CHANNEL(2, AD7192_CH_TEMP),
	AD719x_DIFF_CHANNEL(3, 2, 2, AD7192_CH_AIN2P_AIN2M),
	AD719x_CHANNEL(4, 1, AD7192_CH_AIN1),
	AD719x_CHANNEL(5, 2, AD7192_CH_AIN2),
	AD719x_CHANNEL(6, 3, AD7192_CH_AIN3),
	AD719x_CHANNEL(7, 4, AD7192_CH_AIN4),
	IIO_CHAN_SOFT_TIMESTAMP(8),
};

static const struct iio_chan_spec ad7193_channels[] = {
	AD719x_DIFF_CHANNEL(0, 1, 2, AD7193_CH_AIN1P_AIN2M),
	AD719x_DIFF_CHANNEL(1, 3, 4, AD7193_CH_AIN3P_AIN4M),
	AD719x_DIFF_CHANNEL(2, 5, 6, AD7193_CH_AIN5P_AIN6M),
	AD719x_DIFF_CHANNEL(3, 7, 8, AD7193_CH_AIN7P_AIN8M),
	AD719x_TEMP_CHANNEL(4, AD7193_CH_TEMP),
	AD719x_DIFF_CHANNEL(5, 2, 2, AD7193_CH_AIN2P_AIN2M),
	AD719x_CHANNEL(6, 1, AD7193_CH_AIN1),
	AD719x_CHANNEL(7, 2, AD7193_CH_AIN2),
	AD719x_CHANNEL(8, 3, AD7193_CH_AIN3),
	AD719x_CHANNEL(9, 4, AD7193_CH_AIN4),
	AD719x_CHANNEL(10, 5, AD7193_CH_AIN5),
	AD719x_CHANNEL(11, 6, AD7193_CH_AIN6),
	AD719x_CHANNEL(12, 7, AD7193_CH_AIN7),
	AD719x_CHANNEL(13, 8, AD7193_CH_AIN8),
	IIO_CHAN_SOFT_TIMESTAMP(14),
};

static const struct ad7192_chip_info ad7192_chip_info_tbl[] = {
	[ID_AD7190] = {
		.chip_id = CHIPID_AD7190,
		.name = "ad7190",
	},
	[ID_AD7192] = {
		.chip_id = CHIPID_AD7192,
		.name = "ad7192",
	},
	[ID_AD7193] = {
		.chip_id = CHIPID_AD7193,
		.name = "ad7193",
	},
	[ID_AD7195] = {
		.chip_id = CHIPID_AD7195,
		.name = "ad7195",
	},
};

static int ad7192_channels_config(struct iio_dev *indio_dev)
{
	struct ad7192_state *st = iio_priv(indio_dev);

	switch (st->chip_info->chip_id) {
	case CHIPID_AD7193:
		indio_dev->channels = ad7193_channels;
		indio_dev->num_channels = ARRAY_SIZE(ad7193_channels);
		break;
	default:
		indio_dev->channels = ad7192_channels;
		indio_dev->num_channels = ARRAY_SIZE(ad7192_channels);
		break;
	}

	return 0;
}

static void ad7192_reg_disable(void *reg)
{
	regulator_disable(reg);
}

static void ad7192_clk_disable(void *clk)
{
	clk_disable_unprepare(clk);
}

static int ad7192_probe(struct spi_device *spi)
{
	struct ad7192_state *st;
	struct iio_dev *indio_dev;
	int ret;

	if (!spi->irq) {
		dev_err(&spi->dev, "no IRQ?\n");
		return -ENODEV;
	}

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	mutex_init(&st->lock);

	st->avdd = devm_regulator_get(&spi->dev, "avdd");
	if (IS_ERR(st->avdd))
		return PTR_ERR(st->avdd);

	ret = regulator_enable(st->avdd);
	if (ret) {
		dev_err(&spi->dev, "Failed to enable specified AVdd supply\n");
		return ret;
	}

	ret = devm_add_action_or_reset(&spi->dev, ad7192_reg_disable, st->avdd);
	if (ret)
		return ret;

	ret = devm_regulator_get_enable(&spi->dev, "dvdd");
	if (ret)
		return dev_err_probe(&spi->dev, ret, "Failed to enable specified DVdd supply\n");

	st->vref = devm_regulator_get_optional(&spi->dev, "vref");
	if (IS_ERR(st->vref)) {
		if (PTR_ERR(st->vref) != -ENODEV)
			return PTR_ERR(st->vref);

		ret = regulator_get_voltage(st->avdd);
		if (ret < 0)
			return dev_err_probe(&spi->dev, ret,
					     "Device tree error, AVdd voltage undefined\n");
	} else {
		ret = regulator_enable(st->vref);
		if (ret) {
			dev_err(&spi->dev, "Failed to enable specified Vref supply\n");
			return ret;
		}

		ret = devm_add_action_or_reset(&spi->dev, ad7192_reg_disable, st->vref);
		if (ret)
			return ret;

		ret = regulator_get_voltage(st->vref);
		if (ret < 0)
			return dev_err_probe(&spi->dev, ret,
					     "Device tree error, Vref voltage undefined\n");
	}
	st->int_vref_mv = ret / 1000;

	st->chip_info = of_device_get_match_data(&spi->dev);
	indio_dev->name = st->chip_info->name;
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = ad7192_channels_config(indio_dev);
	if (ret < 0)
		return ret;

	if (st->chip_info->chip_id == CHIPID_AD7195)
		indio_dev->info = &ad7195_info;
	else
		indio_dev->info = &ad7192_info;

	ad_sd_init(&st->sd, indio_dev, spi, &ad7192_sigma_delta_info);

	ret = devm_ad_sd_setup_buffer_and_trigger(&spi->dev, indio_dev);
	if (ret)
		return ret;

	st->fclk = AD7192_INT_FREQ_MHZ;

	st->mclk = devm_clk_get_optional(&spi->dev, "mclk");
	if (IS_ERR(st->mclk))
		return PTR_ERR(st->mclk);

	st->clock_sel = ad7192_of_clock_select(st);

	if (st->clock_sel == AD7192_CLK_EXT_MCLK1_2 ||
	    st->clock_sel == AD7192_CLK_EXT_MCLK2) {
		ret = clk_prepare_enable(st->mclk);
		if (ret < 0)
			return ret;

		ret = devm_add_action_or_reset(&spi->dev, ad7192_clk_disable,
					       st->mclk);
		if (ret)
			return ret;

		st->fclk = clk_get_rate(st->mclk);
		if (!ad7192_valid_external_frequency(st->fclk)) {
			dev_err(&spi->dev,
				"External clock frequency out of bounds\n");
			return -EINVAL;
		}
	}

	ret = ad7192_setup(indio_dev, spi->dev.of_node);
	if (ret)
		return ret;

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static const struct of_device_id ad7192_of_match[] = {
	{ .compatible = "adi,ad7190", .data = &ad7192_chip_info_tbl[ID_AD7190] },
	{ .compatible = "adi,ad7192", .data = &ad7192_chip_info_tbl[ID_AD7192] },
	{ .compatible = "adi,ad7193", .data = &ad7192_chip_info_tbl[ID_AD7193] },
	{ .compatible = "adi,ad7195", .data = &ad7192_chip_info_tbl[ID_AD7195] },
	{}
};
MODULE_DEVICE_TABLE(of, ad7192_of_match);

static struct spi_driver ad7192_driver = {
	.driver = {
		.name	= "ad7192",
		.of_match_table = ad7192_of_match,
	},
	.probe		= ad7192_probe,
};
module_spi_driver(ad7192_driver);

MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD7190, AD7192, AD7193, AD7195 ADC");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(IIO_AD_SIGMA_DELTA);
