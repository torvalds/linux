/*
 * AD5933 AD5934 Impedance Converter, Network Analyzer
 *
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/sysfs.h>
#include <linux/i2c.h>
#include <linux/regulator/consumer.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/module.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/kfifo_buf.h>

/* AD5933/AD5934 Registers */
#define AD5933_REG_CONTROL_HB		0x80	/* R/W, 1 byte */
#define AD5933_REG_CONTROL_LB		0x81	/* R/W, 1 byte */
#define AD5933_REG_FREQ_START		0x82	/* R/W, 3 bytes */
#define AD5933_REG_FREQ_INC		0x85	/* R/W, 3 bytes */
#define AD5933_REG_INC_NUM		0x88	/* R/W, 2 bytes, 9 bit */
#define AD5933_REG_SETTLING_CYCLES	0x8A	/* R/W, 2 bytes */
#define AD5933_REG_STATUS		0x8F	/* R, 1 byte */
#define AD5933_REG_TEMP_DATA		0x92	/* R, 2 bytes*/
#define AD5933_REG_REAL_DATA		0x94	/* R, 2 bytes*/
#define AD5933_REG_IMAG_DATA		0x96	/* R, 2 bytes*/

/* AD5933_REG_CONTROL_HB Bits */
#define AD5933_CTRL_INIT_START_FREQ	(0x1 << 4)
#define AD5933_CTRL_START_SWEEP		(0x2 << 4)
#define AD5933_CTRL_INC_FREQ		(0x3 << 4)
#define AD5933_CTRL_REPEAT_FREQ		(0x4 << 4)
#define AD5933_CTRL_MEASURE_TEMP	(0x9 << 4)
#define AD5933_CTRL_POWER_DOWN		(0xA << 4)
#define AD5933_CTRL_STANDBY		(0xB << 4)

#define AD5933_CTRL_RANGE_2000mVpp	(0x0 << 1)
#define AD5933_CTRL_RANGE_200mVpp	(0x1 << 1)
#define AD5933_CTRL_RANGE_400mVpp	(0x2 << 1)
#define AD5933_CTRL_RANGE_1000mVpp	(0x3 << 1)
#define AD5933_CTRL_RANGE(x)		((x) << 1)

#define AD5933_CTRL_PGA_GAIN_1		(0x1 << 0)
#define AD5933_CTRL_PGA_GAIN_5		(0x0 << 0)

/* AD5933_REG_CONTROL_LB Bits */
#define AD5933_CTRL_RESET		(0x1 << 4)
#define AD5933_CTRL_INT_SYSCLK		(0x0 << 3)
#define AD5933_CTRL_EXT_SYSCLK		(0x1 << 3)

/* AD5933_REG_STATUS Bits */
#define AD5933_STAT_TEMP_VALID		(0x1 << 0)
#define AD5933_STAT_DATA_VALID		(0x1 << 1)
#define AD5933_STAT_SWEEP_DONE		(0x1 << 2)

/* I2C Block Commands */
#define AD5933_I2C_BLOCK_WRITE		0xA0
#define AD5933_I2C_BLOCK_READ		0xA1
#define AD5933_I2C_ADDR_POINTER		0xB0

/* Device Specs */
#define AD5933_INT_OSC_FREQ_Hz		16776000
#define AD5933_MAX_OUTPUT_FREQ_Hz	100000
#define AD5933_MAX_RETRIES		100

#define AD5933_OUT_RANGE		1
#define AD5933_OUT_RANGE_AVAIL		2
#define AD5933_OUT_SETTLING_CYCLES	3
#define AD5933_IN_PGA_GAIN		4
#define AD5933_IN_PGA_GAIN_AVAIL	5
#define AD5933_FREQ_POINTS		6

#define AD5933_POLL_TIME_ms		10
#define AD5933_INIT_EXCITATION_TIME_ms	100

/**
 * struct ad5933_platform_data - platform specific data
 * @ext_clk_Hz:		the external clock frequency in Hz, if not set
 *			the driver uses the internal clock (16.776 MHz)
 * @vref_mv:		the external reference voltage in millivolt
 */

struct ad5933_platform_data {
	unsigned long			ext_clk_Hz;
	unsigned short			vref_mv;
};

struct ad5933_state {
	struct i2c_client		*client;
	struct regulator		*reg;
	struct delayed_work		work;
	struct mutex			lock; /* Protect sensor state */
	unsigned long			mclk_hz;
	unsigned char			ctrl_hb;
	unsigned char			ctrl_lb;
	unsigned int			range_avail[4];
	unsigned short			vref_mv;
	unsigned short			settling_cycles;
	unsigned short			freq_points;
	unsigned int			freq_start;
	unsigned int			freq_inc;
	unsigned int			state;
	unsigned int			poll_time_jiffies;
};

static struct ad5933_platform_data ad5933_default_pdata  = {
	.vref_mv = 3300,
};

#define AD5933_CHANNEL(_type, _extend_name, _info_mask_separate, _address, \
		_scan_index, _realbits) { \
	.type = (_type), \
	.extend_name = (_extend_name), \
	.info_mask_separate = (_info_mask_separate), \
	.address = (_address), \
	.scan_index = (_scan_index), \
	.scan_type = { \
		.sign = 's', \
		.realbits = (_realbits), \
		.storagebits = 16, \
	}, \
}

static const struct iio_chan_spec ad5933_channels[] = {
	AD5933_CHANNEL(IIO_TEMP, NULL, BIT(IIO_CHAN_INFO_RAW) |
		BIT(IIO_CHAN_INFO_SCALE), AD5933_REG_TEMP_DATA, -1, 14),
	/* Ring Channels */
	AD5933_CHANNEL(IIO_VOLTAGE, "real", 0, AD5933_REG_REAL_DATA, 0, 16),
	AD5933_CHANNEL(IIO_VOLTAGE, "imag", 0, AD5933_REG_IMAG_DATA, 1, 16),
};

static int ad5933_i2c_write(struct i2c_client *client, u8 reg, u8 len, u8 *data)
{
	int ret;

	while (len--) {
		ret = i2c_smbus_write_byte_data(client, reg++, *data++);
		if (ret < 0) {
			dev_err(&client->dev, "I2C write error\n");
			return ret;
		}
	}
	return 0;
}

static int ad5933_i2c_read(struct i2c_client *client, u8 reg, u8 len, u8 *data)
{
	int ret;

	while (len--) {
		ret = i2c_smbus_read_byte_data(client, reg++);
		if (ret < 0) {
			dev_err(&client->dev, "I2C read error\n");
			return ret;
		}
		*data++ = ret;
	}
	return 0;
}

static int ad5933_cmd(struct ad5933_state *st, unsigned char cmd)
{
	unsigned char dat = st->ctrl_hb | cmd;

	return ad5933_i2c_write(st->client,
			AD5933_REG_CONTROL_HB, 1, &dat);
}

static int ad5933_reset(struct ad5933_state *st)
{
	unsigned char dat = st->ctrl_lb | AD5933_CTRL_RESET;

	return ad5933_i2c_write(st->client,
			AD5933_REG_CONTROL_LB, 1, &dat);
}

static int ad5933_wait_busy(struct ad5933_state *st, unsigned char event)
{
	unsigned char val, timeout = AD5933_MAX_RETRIES;
	int ret;

	while (timeout--) {
		ret =  ad5933_i2c_read(st->client, AD5933_REG_STATUS, 1, &val);
		if (ret < 0)
			return ret;
		if (val & event)
			return val;
		cpu_relax();
		mdelay(1);
	}

	return -EAGAIN;
}

static int ad5933_set_freq(struct ad5933_state *st,
			   unsigned int reg, unsigned long freq)
{
	unsigned long long freqreg;
	union {
		__be32 d32;
		u8 d8[4];
	} dat;

	freqreg = (u64) freq * (u64) (1 << 27);
	do_div(freqreg, st->mclk_hz / 4);

	switch (reg) {
	case AD5933_REG_FREQ_START:
		st->freq_start = freq;
		break;
	case AD5933_REG_FREQ_INC:
		st->freq_inc = freq;
		break;
	default:
		return -EINVAL;
	}

	dat.d32 = cpu_to_be32(freqreg);
	return ad5933_i2c_write(st->client, reg, 3, &dat.d8[1]);
}

static int ad5933_setup(struct ad5933_state *st)
{
	__be16 dat;
	int ret;

	ret = ad5933_reset(st);
	if (ret < 0)
		return ret;

	ret = ad5933_set_freq(st, AD5933_REG_FREQ_START, 10000);
	if (ret < 0)
		return ret;

	ret = ad5933_set_freq(st, AD5933_REG_FREQ_INC, 200);
	if (ret < 0)
		return ret;

	st->settling_cycles = 10;
	dat = cpu_to_be16(st->settling_cycles);

	ret = ad5933_i2c_write(st->client,
			       AD5933_REG_SETTLING_CYCLES,
			       2, (u8 *)&dat);
	if (ret < 0)
		return ret;

	st->freq_points = 100;
	dat = cpu_to_be16(st->freq_points);

	return ad5933_i2c_write(st->client, AD5933_REG_INC_NUM, 2, (u8 *)&dat);
}

static void ad5933_calc_out_ranges(struct ad5933_state *st)
{
	int i;
	unsigned int normalized_3v3[4] = {1980, 198, 383, 970};

	for (i = 0; i < 4; i++)
		st->range_avail[i] = normalized_3v3[i] * st->vref_mv / 3300;

}

/*
 * handles: AD5933_REG_FREQ_START and AD5933_REG_FREQ_INC
 */

static ssize_t ad5933_show_frequency(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad5933_state *st = iio_priv(indio_dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int ret;
	unsigned long long freqreg;
	union {
		__be32 d32;
		u8 d8[4];
	} dat;

	ret = iio_device_claim_direct_mode(indio_dev);
	if (ret)
		return ret;
	ret = ad5933_i2c_read(st->client, this_attr->address, 3, &dat.d8[1]);
	iio_device_release_direct_mode(indio_dev);
	if (ret < 0)
		return ret;

	freqreg = be32_to_cpu(dat.d32) & 0xFFFFFF;

	freqreg = (u64)freqreg * (u64)(st->mclk_hz / 4);
	do_div(freqreg, 1 << 27);

	return sprintf(buf, "%d\n", (int)freqreg);
}

static ssize_t ad5933_store_frequency(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad5933_state *st = iio_priv(indio_dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	if (val > AD5933_MAX_OUTPUT_FREQ_Hz)
		return -EINVAL;

	ret = iio_device_claim_direct_mode(indio_dev);
	if (ret)
		return ret;
	ret = ad5933_set_freq(st, this_attr->address, val);
	iio_device_release_direct_mode(indio_dev);

	return ret ? ret : len;
}

static IIO_DEVICE_ATTR(out_voltage0_freq_start, 0644,
			ad5933_show_frequency,
			ad5933_store_frequency,
			AD5933_REG_FREQ_START);

static IIO_DEVICE_ATTR(out_voltage0_freq_increment, 0644,
			ad5933_show_frequency,
			ad5933_store_frequency,
			AD5933_REG_FREQ_INC);

static ssize_t ad5933_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad5933_state *st = iio_priv(indio_dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int ret = 0, len = 0;

	mutex_lock(&st->lock);
	switch ((u32)this_attr->address) {
	case AD5933_OUT_RANGE:
		len = sprintf(buf, "%u\n",
			      st->range_avail[(st->ctrl_hb >> 1) & 0x3]);
		break;
	case AD5933_OUT_RANGE_AVAIL:
		len = sprintf(buf, "%u %u %u %u\n", st->range_avail[0],
			      st->range_avail[3], st->range_avail[2],
			      st->range_avail[1]);
		break;
	case AD5933_OUT_SETTLING_CYCLES:
		len = sprintf(buf, "%d\n", st->settling_cycles);
		break;
	case AD5933_IN_PGA_GAIN:
		len = sprintf(buf, "%s\n",
			      (st->ctrl_hb & AD5933_CTRL_PGA_GAIN_1) ?
			      "1" : "0.2");
		break;
	case AD5933_IN_PGA_GAIN_AVAIL:
		len = sprintf(buf, "1 0.2\n");
		break;
	case AD5933_FREQ_POINTS:
		len = sprintf(buf, "%d\n", st->freq_points);
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&st->lock);
	return ret ? ret : len;
}

static ssize_t ad5933_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,
			    size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad5933_state *st = iio_priv(indio_dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	u16 val;
	int i, ret = 0;
	__be16 dat;

	if (this_attr->address != AD5933_IN_PGA_GAIN) {
		ret = kstrtou16(buf, 10, &val);
		if (ret)
			return ret;
	}

	ret = iio_device_claim_direct_mode(indio_dev);
	if (ret)
		return ret;
	mutex_lock(&st->lock);
	switch ((u32)this_attr->address) {
	case AD5933_OUT_RANGE:
		ret = -EINVAL;
		for (i = 0; i < 4; i++)
			if (val == st->range_avail[i]) {
				st->ctrl_hb &= ~AD5933_CTRL_RANGE(0x3);
				st->ctrl_hb |= AD5933_CTRL_RANGE(i);
				ret = ad5933_cmd(st, 0);
				break;
			}
		break;
	case AD5933_IN_PGA_GAIN:
		if (sysfs_streq(buf, "1")) {
			st->ctrl_hb |= AD5933_CTRL_PGA_GAIN_1;
		} else if (sysfs_streq(buf, "0.2")) {
			st->ctrl_hb &= ~AD5933_CTRL_PGA_GAIN_1;
		} else {
			ret = -EINVAL;
			break;
		}
		ret = ad5933_cmd(st, 0);
		break;
	case AD5933_OUT_SETTLING_CYCLES:
		val = clamp(val, (u16)0, (u16)0x7FF);
		st->settling_cycles = val;

		/* 2x, 4x handling, see datasheet */
		if (val > 1022)
			val = (val >> 2) | (3 << 9);
		else if (val > 511)
			val = (val >> 1) | (1 << 9);

		dat = cpu_to_be16(val);
		ret = ad5933_i2c_write(st->client,
				       AD5933_REG_SETTLING_CYCLES,
				       2, (u8 *)&dat);
		break;
	case AD5933_FREQ_POINTS:
		val = clamp(val, (u16)0, (u16)511);
		st->freq_points = val;

		dat = cpu_to_be16(val);
		ret = ad5933_i2c_write(st->client, AD5933_REG_INC_NUM, 2,
				       (u8 *)&dat);
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&st->lock);
	iio_device_release_direct_mode(indio_dev);
	return ret ? ret : len;
}

static IIO_DEVICE_ATTR(out_voltage0_scale, 0644,
			ad5933_show,
			ad5933_store,
			AD5933_OUT_RANGE);

static IIO_DEVICE_ATTR(out_voltage0_scale_available, 0444,
			ad5933_show,
			NULL,
			AD5933_OUT_RANGE_AVAIL);

static IIO_DEVICE_ATTR(in_voltage0_scale, 0644,
			ad5933_show,
			ad5933_store,
			AD5933_IN_PGA_GAIN);

static IIO_DEVICE_ATTR(in_voltage0_scale_available, 0444,
			ad5933_show,
			NULL,
			AD5933_IN_PGA_GAIN_AVAIL);

static IIO_DEVICE_ATTR(out_voltage0_freq_points, 0644,
			ad5933_show,
			ad5933_store,
			AD5933_FREQ_POINTS);

static IIO_DEVICE_ATTR(out_voltage0_settling_cycles, 0644,
			ad5933_show,
			ad5933_store,
			AD5933_OUT_SETTLING_CYCLES);

/* note:
 * ideally we would handle the scale attributes via the iio_info
 * (read|write)_raw methods, however this part is a untypical since we
 * don't create dedicated sysfs channel attributes for out0 and in0.
 */
static struct attribute *ad5933_attributes[] = {
	&iio_dev_attr_out_voltage0_scale.dev_attr.attr,
	&iio_dev_attr_out_voltage0_scale_available.dev_attr.attr,
	&iio_dev_attr_out_voltage0_freq_start.dev_attr.attr,
	&iio_dev_attr_out_voltage0_freq_increment.dev_attr.attr,
	&iio_dev_attr_out_voltage0_freq_points.dev_attr.attr,
	&iio_dev_attr_out_voltage0_settling_cycles.dev_attr.attr,
	&iio_dev_attr_in_voltage0_scale.dev_attr.attr,
	&iio_dev_attr_in_voltage0_scale_available.dev_attr.attr,
	NULL
};

static const struct attribute_group ad5933_attribute_group = {
	.attrs = ad5933_attributes,
};

static int ad5933_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long m)
{
	struct ad5933_state *st = iio_priv(indio_dev);
	__be16 dat;
	int ret;

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;
		ret = ad5933_cmd(st, AD5933_CTRL_MEASURE_TEMP);
		if (ret < 0)
			goto out;
		ret = ad5933_wait_busy(st, AD5933_STAT_TEMP_VALID);
		if (ret < 0)
			goto out;

		ret = ad5933_i2c_read(st->client,
				      AD5933_REG_TEMP_DATA,
				      2, (u8 *)&dat);
		if (ret < 0)
			goto out;
		iio_device_release_direct_mode(indio_dev);
		*val = sign_extend32(be16_to_cpu(dat), 13);

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 1000;
		*val2 = 5;
		return IIO_VAL_FRACTIONAL_LOG2;
	}

	return -EINVAL;
out:
	iio_device_release_direct_mode(indio_dev);
	return ret;
}

static const struct iio_info ad5933_info = {
	.read_raw = ad5933_read_raw,
	.attrs = &ad5933_attribute_group,
};

static int ad5933_ring_preenable(struct iio_dev *indio_dev)
{
	struct ad5933_state *st = iio_priv(indio_dev);
	int ret;

	if (bitmap_empty(indio_dev->active_scan_mask, indio_dev->masklength))
		return -EINVAL;

	ret = ad5933_reset(st);
	if (ret < 0)
		return ret;

	ret = ad5933_cmd(st, AD5933_CTRL_STANDBY);
	if (ret < 0)
		return ret;

	ret = ad5933_cmd(st, AD5933_CTRL_INIT_START_FREQ);
	if (ret < 0)
		return ret;

	st->state = AD5933_CTRL_INIT_START_FREQ;

	return 0;
}

static int ad5933_ring_postenable(struct iio_dev *indio_dev)
{
	struct ad5933_state *st = iio_priv(indio_dev);

	/* AD5933_CTRL_INIT_START_FREQ:
	 * High Q complex circuits require a long time to reach steady state.
	 * To facilitate the measurement of such impedances, this mode allows
	 * the user full control of the settling time requirement before
	 * entering start frequency sweep mode where the impedance measurement
	 * takes place. In this mode the impedance is excited with the
	 * programmed start frequency (ad5933_ring_preenable),
	 * but no measurement takes place.
	 */

	schedule_delayed_work(&st->work,
			      msecs_to_jiffies(AD5933_INIT_EXCITATION_TIME_ms));
	return 0;
}

static int ad5933_ring_postdisable(struct iio_dev *indio_dev)
{
	struct ad5933_state *st = iio_priv(indio_dev);

	cancel_delayed_work_sync(&st->work);
	return ad5933_cmd(st, AD5933_CTRL_POWER_DOWN);
}

static const struct iio_buffer_setup_ops ad5933_ring_setup_ops = {
	.preenable = ad5933_ring_preenable,
	.postenable = ad5933_ring_postenable,
	.postdisable = ad5933_ring_postdisable,
};

static int ad5933_register_ring_funcs_and_init(struct iio_dev *indio_dev)
{
	struct iio_buffer *buffer;

	buffer = iio_kfifo_allocate();
	if (!buffer)
		return -ENOMEM;

	iio_device_attach_buffer(indio_dev, buffer);

	/* Ring buffer functions - here trigger setup related */
	indio_dev->setup_ops = &ad5933_ring_setup_ops;

	return 0;
}

static void ad5933_work(struct work_struct *work)
{
	struct ad5933_state *st = container_of(work,
		struct ad5933_state, work.work);
	struct iio_dev *indio_dev = i2c_get_clientdata(st->client);
	__be16 buf[2];
	int val[2];
	unsigned char status;
	int ret;

	if (st->state == AD5933_CTRL_INIT_START_FREQ) {
		/* start sweep */
		ad5933_cmd(st, AD5933_CTRL_START_SWEEP);
		st->state = AD5933_CTRL_START_SWEEP;
		schedule_delayed_work(&st->work, st->poll_time_jiffies);
		return;
	}

	ret = ad5933_i2c_read(st->client, AD5933_REG_STATUS, 1, &status);
	if (ret)
		return;

	if (status & AD5933_STAT_DATA_VALID) {
		int scan_count = bitmap_weight(indio_dev->active_scan_mask,
					       indio_dev->masklength);
		ret = ad5933_i2c_read(st->client,
				test_bit(1, indio_dev->active_scan_mask) ?
				AD5933_REG_REAL_DATA : AD5933_REG_IMAG_DATA,
				scan_count * 2, (u8 *)buf);
		if (ret)
			return;

		if (scan_count == 2) {
			val[0] = be16_to_cpu(buf[0]);
			val[1] = be16_to_cpu(buf[1]);
		} else {
			val[0] = be16_to_cpu(buf[0]);
		}
		iio_push_to_buffers(indio_dev, val);
	} else {
		/* no data available - try again later */
		schedule_delayed_work(&st->work, st->poll_time_jiffies);
		return;
	}

	if (status & AD5933_STAT_SWEEP_DONE) {
		/* last sample received - power down do
		 * nothing until the ring enable is toggled
		 */
		ad5933_cmd(st, AD5933_CTRL_POWER_DOWN);
	} else {
		/* we just received a valid datum, move on to the next */
		ad5933_cmd(st, AD5933_CTRL_INC_FREQ);
		schedule_delayed_work(&st->work, st->poll_time_jiffies);
	}
}

static int ad5933_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int ret, voltage_uv = 0;
	struct ad5933_platform_data *pdata = dev_get_platdata(&client->dev);
	struct ad5933_state *st;
	struct iio_dev *indio_dev;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	st->client = client;

	mutex_init(&st->lock);

	if (!pdata)
		pdata = &ad5933_default_pdata;

	st->reg = devm_regulator_get(&client->dev, "vdd");
	if (IS_ERR(st->reg))
		return PTR_ERR(st->reg);

	ret = regulator_enable(st->reg);
	if (ret) {
		dev_err(&client->dev, "Failed to enable specified VDD supply\n");
		return ret;
	}
	voltage_uv = regulator_get_voltage(st->reg);

	if (voltage_uv)
		st->vref_mv = voltage_uv / 1000;
	else
		st->vref_mv = pdata->vref_mv;

	if (pdata->ext_clk_Hz) {
		st->mclk_hz = pdata->ext_clk_Hz;
		st->ctrl_lb = AD5933_CTRL_EXT_SYSCLK;
	} else {
		st->mclk_hz = AD5933_INT_OSC_FREQ_Hz;
		st->ctrl_lb = AD5933_CTRL_INT_SYSCLK;
	}

	ad5933_calc_out_ranges(st);
	INIT_DELAYED_WORK(&st->work, ad5933_work);
	st->poll_time_jiffies = msecs_to_jiffies(AD5933_POLL_TIME_ms);

	indio_dev->dev.parent = &client->dev;
	indio_dev->info = &ad5933_info;
	indio_dev->name = id->name;
	indio_dev->modes = (INDIO_BUFFER_SOFTWARE | INDIO_DIRECT_MODE);
	indio_dev->channels = ad5933_channels;
	indio_dev->num_channels = ARRAY_SIZE(ad5933_channels);

	ret = ad5933_register_ring_funcs_and_init(indio_dev);
	if (ret)
		goto error_disable_reg;

	ret = ad5933_setup(st);
	if (ret)
		goto error_unreg_ring;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_unreg_ring;

	return 0;

error_unreg_ring:
	iio_kfifo_free(indio_dev->buffer);
error_disable_reg:
	regulator_disable(st->reg);

	return ret;
}

static int ad5933_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct ad5933_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	iio_kfifo_free(indio_dev->buffer);
	regulator_disable(st->reg);

	return 0;
}

static const struct i2c_device_id ad5933_id[] = {
	{ "ad5933", 0 },
	{ "ad5934", 0 },
	{}
};

MODULE_DEVICE_TABLE(i2c, ad5933_id);

static struct i2c_driver ad5933_driver = {
	.driver = {
		.name = "ad5933",
	},
	.probe = ad5933_probe,
	.remove = ad5933_remove,
	.id_table = ad5933_id,
};
module_i2c_driver(ad5933_driver);

MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD5933 Impedance Conv. Network Analyzer");
MODULE_LICENSE("GPL v2");
