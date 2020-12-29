/*
 * si1145.c - Support for Silabs SI1132 and SI1141/2/3/5/6/7 combined ambient
 * light, UV index and proximity sensors
 *
 * Copyright 2014-16 Peter Meerwald-Stadler <pmeerw@pmeerw.net>
 * Copyright 2016 Crestez Dan Leonard <leonard.crestez@intel.com>
 *
 * This file is subject to the terms and conditions of version 2 of
 * the GNU General Public License.  See the file COPYING in the main
 * directory of this archive for more details.
 *
 * SI1132 (7-bit I2C slave address 0x60)
 * SI1141/2/3 (7-bit I2C slave address 0x5a)
 * SI1145/6/6 (7-bit I2C slave address 0x60)
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/gpio.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/buffer.h>
#include <linux/util_macros.h>

#define SI1145_REG_PART_ID		0x00
#define SI1145_REG_REV_ID		0x01
#define SI1145_REG_SEQ_ID		0x02
#define SI1145_REG_INT_CFG		0x03
#define SI1145_REG_IRQ_ENABLE		0x04
#define SI1145_REG_IRQ_MODE		0x05
#define SI1145_REG_HW_KEY		0x07
#define SI1145_REG_MEAS_RATE		0x08
#define SI1145_REG_PS_LED21		0x0f
#define SI1145_REG_PS_LED3		0x10
#define SI1145_REG_UCOEF1		0x13
#define SI1145_REG_UCOEF2		0x14
#define SI1145_REG_UCOEF3		0x15
#define SI1145_REG_UCOEF4		0x16
#define SI1145_REG_PARAM_WR		0x17
#define SI1145_REG_COMMAND		0x18
#define SI1145_REG_RESPONSE		0x20
#define SI1145_REG_IRQ_STATUS		0x21
#define SI1145_REG_ALSVIS_DATA		0x22
#define SI1145_REG_ALSIR_DATA		0x24
#define SI1145_REG_PS1_DATA		0x26
#define SI1145_REG_PS2_DATA		0x28
#define SI1145_REG_PS3_DATA		0x2a
#define SI1145_REG_AUX_DATA		0x2c
#define SI1145_REG_PARAM_RD		0x2e
#define SI1145_REG_CHIP_STAT		0x30

#define SI1145_UCOEF1_DEFAULT		0x7b
#define SI1145_UCOEF2_DEFAULT		0x6b
#define SI1145_UCOEF3_DEFAULT		0x01
#define SI1145_UCOEF4_DEFAULT		0x00

/* Helper to figure out PS_LED register / shift per channel */
#define SI1145_PS_LED_REG(ch) \
	(((ch) == 2) ? SI1145_REG_PS_LED3 : SI1145_REG_PS_LED21)
#define SI1145_PS_LED_SHIFT(ch) \
	(((ch) == 1) ? 4 : 0)

/* Parameter offsets */
#define SI1145_PARAM_CHLIST		0x01
#define SI1145_PARAM_PSLED12_SELECT	0x02
#define SI1145_PARAM_PSLED3_SELECT	0x03
#define SI1145_PARAM_PS_ENCODING	0x05
#define SI1145_PARAM_ALS_ENCODING	0x06
#define SI1145_PARAM_PS1_ADC_MUX	0x07
#define SI1145_PARAM_PS2_ADC_MUX	0x08
#define SI1145_PARAM_PS3_ADC_MUX	0x09
#define SI1145_PARAM_PS_ADC_COUNTER	0x0a
#define SI1145_PARAM_PS_ADC_GAIN	0x0b
#define SI1145_PARAM_PS_ADC_MISC	0x0c
#define SI1145_PARAM_ALS_ADC_MUX	0x0d
#define SI1145_PARAM_ALSIR_ADC_MUX	0x0e
#define SI1145_PARAM_AUX_ADC_MUX	0x0f
#define SI1145_PARAM_ALSVIS_ADC_COUNTER	0x10
#define SI1145_PARAM_ALSVIS_ADC_GAIN	0x11
#define SI1145_PARAM_ALSVIS_ADC_MISC	0x12
#define SI1145_PARAM_LED_RECOVERY	0x1c
#define SI1145_PARAM_ALSIR_ADC_COUNTER	0x1d
#define SI1145_PARAM_ALSIR_ADC_GAIN	0x1e
#define SI1145_PARAM_ALSIR_ADC_MISC	0x1f
#define SI1145_PARAM_ADC_OFFSET		0x1a

/* Channel enable masks for CHLIST parameter */
#define SI1145_CHLIST_EN_PS1		BIT(0)
#define SI1145_CHLIST_EN_PS2		BIT(1)
#define SI1145_CHLIST_EN_PS3		BIT(2)
#define SI1145_CHLIST_EN_ALSVIS		BIT(4)
#define SI1145_CHLIST_EN_ALSIR		BIT(5)
#define SI1145_CHLIST_EN_AUX		BIT(6)
#define SI1145_CHLIST_EN_UV		BIT(7)

/* Proximity measurement mode for ADC_MISC parameter */
#define SI1145_PS_ADC_MODE_NORMAL	BIT(2)
/* Signal range mask for ADC_MISC parameter */
#define SI1145_ADC_MISC_RANGE		BIT(5)

/* Commands for REG_COMMAND */
#define SI1145_CMD_NOP			0x00
#define SI1145_CMD_RESET		0x01
#define SI1145_CMD_PS_FORCE		0x05
#define SI1145_CMD_ALS_FORCE		0x06
#define SI1145_CMD_PSALS_FORCE		0x07
#define SI1145_CMD_PS_PAUSE		0x09
#define SI1145_CMD_ALS_PAUSE		0x0a
#define SI1145_CMD_PSALS_PAUSE		0x0b
#define SI1145_CMD_PS_AUTO		0x0d
#define SI1145_CMD_ALS_AUTO		0x0e
#define SI1145_CMD_PSALS_AUTO		0x0f
#define SI1145_CMD_PARAM_QUERY		0x80
#define SI1145_CMD_PARAM_SET		0xa0

#define SI1145_RSP_INVALID_SETTING	0x80
#define SI1145_RSP_COUNTER_MASK		0x0F

/* Minimum sleep after each command to ensure it's received */
#define SI1145_COMMAND_MINSLEEP_MS	5
/* Return -ETIMEDOUT after this long */
#define SI1145_COMMAND_TIMEOUT_MS	25

/* Interrupt configuration masks for INT_CFG register */
#define SI1145_INT_CFG_OE		BIT(0) /* enable interrupt */
#define SI1145_INT_CFG_MODE		BIT(1) /* auto reset interrupt pin */

/* Interrupt enable masks for IRQ_ENABLE register */
#define SI1145_MASK_ALL_IE		(BIT(4) | BIT(3) | BIT(2) | BIT(0))

#define SI1145_MUX_TEMP			0x65
#define SI1145_MUX_VDD			0x75

/* Proximity LED current; see Table 2 in datasheet */
#define SI1145_LED_CURRENT_45mA		0x04

enum {
	SI1132,
	SI1141,
	SI1142,
	SI1143,
	SI1145,
	SI1146,
	SI1147,
};

struct si1145_part_info {
	u8 part;
	const struct iio_info *iio_info;
	const struct iio_chan_spec *channels;
	unsigned int num_channels;
	unsigned int num_leds;
	bool uncompressed_meas_rate;
};

/**
 * struct si1145_data - si1145 chip state data
 * @client:	I2C client
 * @lock:	mutex to protect shared state.
 * @cmdlock:	Low-level mutex to protect command execution only
 * @rsp_seq:	Next expected response number or -1 if counter reset required
 * @scan_mask:	Saved scan mask to avoid duplicate set_chlist
 * @autonomous: If automatic measurements are active (for buffer support)
 * @part_info:	Part information
 * @trig:	Pointer to iio trigger
 * @meas_rate:	Value of MEAS_RATE register. Only set in HW in auto mode
 * @buffer:	Used to pack data read from sensor.
 */
struct si1145_data {
	struct i2c_client *client;
	struct mutex lock;
	struct mutex cmdlock;
	int rsp_seq;
	const struct si1145_part_info *part_info;
	unsigned long scan_mask;
	bool autonomous;
	struct iio_trigger *trig;
	int meas_rate;
	/*
	 * Ensure timestamp will be naturally aligned if present.
	 * Maximum buffer size (may be only partly used if not all
	 * channels are enabled):
	 *   6*2 bytes channels data + 4 bytes alignment +
	 *   8 bytes timestamp
	 */
	u8 buffer[24] __aligned(8);
};

/**
 * __si1145_command_reset() - Send CMD_NOP and wait for response 0
 *
 * Does not modify data->rsp_seq
 *
 * Return: 0 on success and -errno on error.
 */
static int __si1145_command_reset(struct si1145_data *data)
{
	struct device *dev = &data->client->dev;
	unsigned long stop_jiffies;
	int ret;

	ret = i2c_smbus_write_byte_data(data->client, SI1145_REG_COMMAND,
						      SI1145_CMD_NOP);
	if (ret < 0)
		return ret;
	msleep(SI1145_COMMAND_MINSLEEP_MS);

	stop_jiffies = jiffies + SI1145_COMMAND_TIMEOUT_MS * HZ / 1000;
	while (true) {
		ret = i2c_smbus_read_byte_data(data->client,
					       SI1145_REG_RESPONSE);
		if (ret <= 0)
			return ret;
		if (time_after(jiffies, stop_jiffies)) {
			dev_warn(dev, "timeout on reset\n");
			return -ETIMEDOUT;
		}
		msleep(SI1145_COMMAND_MINSLEEP_MS);
		continue;
	}
}

/**
 * si1145_command() - Execute a command and poll the response register
 *
 * All conversion overflows are reported as -EOVERFLOW
 * INVALID_SETTING is reported as -EINVAL
 * Timeouts are reported as -ETIMEDOUT
 *
 * Return: 0 on success or -errno on failure
 */
static int si1145_command(struct si1145_data *data, u8 cmd)
{
	struct device *dev = &data->client->dev;
	unsigned long stop_jiffies;
	int ret;

	mutex_lock(&data->cmdlock);

	if (data->rsp_seq < 0) {
		ret = __si1145_command_reset(data);
		if (ret < 0) {
			dev_err(dev, "failed to reset command counter, ret=%d\n",
				ret);
			goto out;
		}
		data->rsp_seq = 0;
	}

	ret = i2c_smbus_write_byte_data(data->client, SI1145_REG_COMMAND, cmd);
	if (ret) {
		dev_warn(dev, "failed to write command, ret=%d\n", ret);
		goto out;
	}
	/* Sleep a little to ensure the command is received */
	msleep(SI1145_COMMAND_MINSLEEP_MS);

	stop_jiffies = jiffies + SI1145_COMMAND_TIMEOUT_MS * HZ / 1000;
	while (true) {
		ret = i2c_smbus_read_byte_data(data->client,
					       SI1145_REG_RESPONSE);
		if (ret < 0) {
			dev_warn(dev, "failed to read response, ret=%d\n", ret);
			break;
		}

		if ((ret & ~SI1145_RSP_COUNTER_MASK) == 0) {
			if (ret == data->rsp_seq) {
				if (time_after(jiffies, stop_jiffies)) {
					dev_warn(dev, "timeout on command %#02hhx\n",
						 cmd);
					ret = -ETIMEDOUT;
					break;
				}
				msleep(SI1145_COMMAND_MINSLEEP_MS);
				continue;
			}
			if (ret == ((data->rsp_seq + 1) &
				SI1145_RSP_COUNTER_MASK)) {
				data->rsp_seq = ret;
				ret = 0;
				break;
			}
			dev_warn(dev, "unexpected response counter %d instead of %d\n",
				 ret, (data->rsp_seq + 1) &
					SI1145_RSP_COUNTER_MASK);
			ret = -EIO;
		} else {
			if (ret == SI1145_RSP_INVALID_SETTING) {
				dev_warn(dev, "INVALID_SETTING error on command %#02hhx\n",
					 cmd);
				ret = -EINVAL;
			} else {
				/* All overflows are treated identically */
				dev_dbg(dev, "overflow, ret=%d, cmd=%#02hhx\n",
					ret, cmd);
				ret = -EOVERFLOW;
			}
		}

		/* Force a counter reset next time */
		data->rsp_seq = -1;
		break;
	}

out:
	mutex_unlock(&data->cmdlock);

	return ret;
}

static int si1145_param_update(struct si1145_data *data, u8 op, u8 param,
			       u8 value)
{
	int ret;

	ret = i2c_smbus_write_byte_data(data->client,
		SI1145_REG_PARAM_WR, value);
	if (ret < 0)
		return ret;

	return si1145_command(data, op | (param & 0x1F));
}

static int si1145_param_set(struct si1145_data *data, u8 param, u8 value)
{
	return si1145_param_update(data, SI1145_CMD_PARAM_SET, param, value);
}

/* Set param. Returns negative errno or current value */
static int si1145_param_query(struct si1145_data *data, u8 param)
{
	int ret;

	ret = si1145_command(data, SI1145_CMD_PARAM_QUERY | (param & 0x1F));
	if (ret < 0)
		return ret;

	return i2c_smbus_read_byte_data(data->client, SI1145_REG_PARAM_RD);
}

/* Expand 8 bit compressed value to 16 bit, see Silabs AN498 */
static u16 si1145_uncompress(u8 x)
{
	u16 result = 0;
	u8 exponent = 0;

	if (x < 8)
		return 0;

	exponent = (x & 0xf0) >> 4;
	result = 0x10 | (x & 0x0f);

	if (exponent >= 4)
		return result << (exponent - 4);
	return result >> (4 - exponent);
}

/* Compress 16 bit value to 8 bit, see Silabs AN498 */
static u8 si1145_compress(u16 x)
{
	u32 exponent = 0;
	u32 significand = 0;
	u32 tmp = x;

	if (x == 0x0000)
		return 0x00;
	if (x == 0x0001)
		return 0x08;

	while (1) {
		tmp >>= 1;
		exponent += 1;
		if (tmp == 1)
			break;
	}

	if (exponent < 5) {
		significand = x << (4 - exponent);
		return (exponent << 4) | (significand & 0xF);
	}

	significand = x >> (exponent - 5);
	if (significand & 1) {
		significand += 2;
		if (significand & 0x0040) {
			exponent += 1;
			significand >>= 1;
		}
	}

	return (exponent << 4) | ((significand >> 1) & 0xF);
}

/* Write meas_rate in hardware */
static int si1145_set_meas_rate(struct si1145_data *data, int interval)
{
	if (data->part_info->uncompressed_meas_rate)
		return i2c_smbus_write_word_data(data->client,
			SI1145_REG_MEAS_RATE, interval);
	else
		return i2c_smbus_write_byte_data(data->client,
			SI1145_REG_MEAS_RATE, interval);
}

static int si1145_read_samp_freq(struct si1145_data *data, int *val, int *val2)
{
	*val = 32000;
	if (data->part_info->uncompressed_meas_rate)
		*val2 = data->meas_rate;
	else
		*val2 = si1145_uncompress(data->meas_rate);
	return IIO_VAL_FRACTIONAL;
}

/* Set the samp freq in driver private data */
static int si1145_store_samp_freq(struct si1145_data *data, int val)
{
	int ret = 0;
	int meas_rate;

	if (val <= 0 || val > 32000)
		return -ERANGE;
	meas_rate = 32000 / val;

	mutex_lock(&data->lock);
	if (data->autonomous) {
		ret = si1145_set_meas_rate(data, meas_rate);
		if (ret)
			goto out;
	}
	if (data->part_info->uncompressed_meas_rate)
		data->meas_rate = meas_rate;
	else
		data->meas_rate = si1145_compress(meas_rate);

out:
	mutex_unlock(&data->lock);

	return ret;
}

static irqreturn_t si1145_trigger_handler(int irq, void *private)
{
	struct iio_poll_func *pf = private;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct si1145_data *data = iio_priv(indio_dev);
	int i, j = 0;
	int ret;
	u8 irq_status = 0;

	if (!data->autonomous) {
		ret = si1145_command(data, SI1145_CMD_PSALS_FORCE);
		if (ret < 0 && ret != -EOVERFLOW)
			goto done;
	} else {
		irq_status = ret = i2c_smbus_read_byte_data(data->client,
				SI1145_REG_IRQ_STATUS);
		if (ret < 0)
			goto done;
		if (!(irq_status & SI1145_MASK_ALL_IE))
			goto done;
	}

	for_each_set_bit(i, indio_dev->active_scan_mask,
		indio_dev->masklength) {
		int run = 1;

		while (i + run < indio_dev->masklength) {
			if (!test_bit(i + run, indio_dev->active_scan_mask))
				break;
			if (indio_dev->channels[i + run].address !=
				indio_dev->channels[i].address + 2 * run)
				break;
			run++;
		}

		ret = i2c_smbus_read_i2c_block_data_or_emulated(
				data->client, indio_dev->channels[i].address,
				sizeof(u16) * run, &data->buffer[j]);
		if (ret < 0)
			goto done;
		j += run * sizeof(u16);
		i += run - 1;
	}

	if (data->autonomous) {
		ret = i2c_smbus_write_byte_data(data->client,
				SI1145_REG_IRQ_STATUS,
				irq_status & SI1145_MASK_ALL_IE);
		if (ret < 0)
			goto done;
	}

	iio_push_to_buffers_with_timestamp(indio_dev, data->buffer,
		iio_get_time_ns(indio_dev));

done:
	iio_trigger_notify_done(indio_dev->trig);
	return IRQ_HANDLED;
}

static int si1145_set_chlist(struct iio_dev *indio_dev, unsigned long scan_mask)
{
	struct si1145_data *data = iio_priv(indio_dev);
	u8 reg = 0, mux;
	int ret;
	int i;

	/* channel list already set, no need to reprogram */
	if (data->scan_mask == scan_mask)
		return 0;

	for_each_set_bit(i, &scan_mask, indio_dev->masklength) {
		switch (indio_dev->channels[i].address) {
		case SI1145_REG_ALSVIS_DATA:
			reg |= SI1145_CHLIST_EN_ALSVIS;
			break;
		case SI1145_REG_ALSIR_DATA:
			reg |= SI1145_CHLIST_EN_ALSIR;
			break;
		case SI1145_REG_PS1_DATA:
			reg |= SI1145_CHLIST_EN_PS1;
			break;
		case SI1145_REG_PS2_DATA:
			reg |= SI1145_CHLIST_EN_PS2;
			break;
		case SI1145_REG_PS3_DATA:
			reg |= SI1145_CHLIST_EN_PS3;
			break;
		case SI1145_REG_AUX_DATA:
			switch (indio_dev->channels[i].type) {
			case IIO_UVINDEX:
				reg |= SI1145_CHLIST_EN_UV;
				break;
			default:
				reg |= SI1145_CHLIST_EN_AUX;
				if (indio_dev->channels[i].type == IIO_TEMP)
					mux = SI1145_MUX_TEMP;
				else
					mux = SI1145_MUX_VDD;
				ret = si1145_param_set(data,
					SI1145_PARAM_AUX_ADC_MUX, mux);
				if (ret < 0)
					return ret;

				break;
			}
		}
	}

	data->scan_mask = scan_mask;
	ret = si1145_param_set(data, SI1145_PARAM_CHLIST, reg);

	return ret < 0 ? ret : 0;
}

static int si1145_measure(struct iio_dev *indio_dev,
			  struct iio_chan_spec const *chan)
{
	struct si1145_data *data = iio_priv(indio_dev);
	u8 cmd;
	int ret;

	ret = si1145_set_chlist(indio_dev, BIT(chan->scan_index));
	if (ret < 0)
		return ret;

	cmd = (chan->type == IIO_PROXIMITY) ? SI1145_CMD_PS_FORCE :
		SI1145_CMD_ALS_FORCE;
	ret = si1145_command(data, cmd);
	if (ret < 0 && ret != -EOVERFLOW)
		return ret;

	return i2c_smbus_read_word_data(data->client, chan->address);
}

/*
 * Conversion between iio scale and ADC_GAIN values
 * These could be further adjusted but proximity/intensity are dimensionless
 */
static const int si1145_proximity_scale_available[] = {
	128, 64, 32, 16, 8, 4};
static const int si1145_intensity_scale_available[] = {
	128, 64, 32, 16, 8, 4, 2, 1};
static IIO_CONST_ATTR(in_proximity_scale_available,
	"128 64 32 16 8 4");
static IIO_CONST_ATTR(in_intensity_scale_available,
	"128 64 32 16 8 4 2 1");
static IIO_CONST_ATTR(in_intensity_ir_scale_available,
	"128 64 32 16 8 4 2 1");

static int si1145_scale_from_adcgain(int regval)
{
	return 128 >> regval;
}

static int si1145_proximity_adcgain_from_scale(int val, int val2)
{
	val = find_closest_descending(val, si1145_proximity_scale_available,
				ARRAY_SIZE(si1145_proximity_scale_available));
	if (val < 0 || val > 5 || val2 != 0)
		return -EINVAL;

	return val;
}

static int si1145_intensity_adcgain_from_scale(int val, int val2)
{
	val = find_closest_descending(val, si1145_intensity_scale_available,
				ARRAY_SIZE(si1145_intensity_scale_available));
	if (val < 0 || val > 7 || val2 != 0)
		return -EINVAL;

	return val;
}

static int si1145_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int *val, int *val2, long mask)
{
	struct si1145_data *data = iio_priv(indio_dev);
	int ret;
	u8 reg;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_INTENSITY:
		case IIO_PROXIMITY:
		case IIO_VOLTAGE:
		case IIO_TEMP:
		case IIO_UVINDEX:
			ret = iio_device_claim_direct_mode(indio_dev);
			if (ret)
				return ret;
			ret = si1145_measure(indio_dev, chan);
			iio_device_release_direct_mode(indio_dev);

			if (ret < 0)
				return ret;

			*val = ret;

			return IIO_VAL_INT;
		case IIO_CURRENT:
			ret = i2c_smbus_read_byte_data(data->client,
				SI1145_PS_LED_REG(chan->channel));
			if (ret < 0)
				return ret;

			*val = (ret >> SI1145_PS_LED_SHIFT(chan->channel))
				& 0x0f;

			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_PROXIMITY:
			reg = SI1145_PARAM_PS_ADC_GAIN;
			break;
		case IIO_INTENSITY:
			if (chan->channel2 == IIO_MOD_LIGHT_IR)
				reg = SI1145_PARAM_ALSIR_ADC_GAIN;
			else
				reg = SI1145_PARAM_ALSVIS_ADC_GAIN;
			break;
		case IIO_TEMP:
			*val = 28;
			*val2 = 571429;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_UVINDEX:
			*val = 0;
			*val2 = 10000;
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}

		ret = si1145_param_query(data, reg);
		if (ret < 0)
			return ret;

		*val = si1145_scale_from_adcgain(ret & 0x07);

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_OFFSET:
		switch (chan->type) {
		case IIO_TEMP:
			/*
			 * -ADC offset - ADC counts @ 25°C -
			 *   35 * ADC counts / °C
			 */
			*val = -256 - 11136 + 25 * 35;
			return IIO_VAL_INT;
		default:
			/*
			 * All ADC measurements have are by default offset
			 * by -256
			 * See AN498 5.6.3
			 */
			ret = si1145_param_query(data, SI1145_PARAM_ADC_OFFSET);
			if (ret < 0)
				return ret;
			*val = -si1145_uncompress(ret);
			return IIO_VAL_INT;
		}
	case IIO_CHAN_INFO_SAMP_FREQ:
		return si1145_read_samp_freq(data, val, val2);
	default:
		return -EINVAL;
	}
}

static int si1145_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val, int val2, long mask)
{
	struct si1145_data *data = iio_priv(indio_dev);
	u8 reg1, reg2, shift;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_PROXIMITY:
			val = si1145_proximity_adcgain_from_scale(val, val2);
			if (val < 0)
				return val;
			reg1 = SI1145_PARAM_PS_ADC_GAIN;
			reg2 = SI1145_PARAM_PS_ADC_COUNTER;
			break;
		case IIO_INTENSITY:
			val = si1145_intensity_adcgain_from_scale(val, val2);
			if (val < 0)
				return val;
			if (chan->channel2 == IIO_MOD_LIGHT_IR) {
				reg1 = SI1145_PARAM_ALSIR_ADC_GAIN;
				reg2 = SI1145_PARAM_ALSIR_ADC_COUNTER;
			} else {
				reg1 = SI1145_PARAM_ALSVIS_ADC_GAIN;
				reg2 = SI1145_PARAM_ALSVIS_ADC_COUNTER;
			}
			break;
		default:
			return -EINVAL;
		}

		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;

		ret = si1145_param_set(data, reg1, val);
		if (ret < 0) {
			iio_device_release_direct_mode(indio_dev);
			return ret;
		}
		/* Set recovery period to one's complement of gain */
		ret = si1145_param_set(data, reg2, (~val & 0x07) << 4);
		iio_device_release_direct_mode(indio_dev);
		return ret;
	case IIO_CHAN_INFO_RAW:
		if (chan->type != IIO_CURRENT)
			return -EINVAL;

		if (val < 0 || val > 15 || val2 != 0)
			return -EINVAL;

		reg1 = SI1145_PS_LED_REG(chan->channel);
		shift = SI1145_PS_LED_SHIFT(chan->channel);

		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;

		ret = i2c_smbus_read_byte_data(data->client, reg1);
		if (ret < 0) {
			iio_device_release_direct_mode(indio_dev);
			return ret;
		}
		ret = i2c_smbus_write_byte_data(data->client, reg1,
			(ret & ~(0x0f << shift)) |
			((val & 0x0f) << shift));
		iio_device_release_direct_mode(indio_dev);
		return ret;
	case IIO_CHAN_INFO_SAMP_FREQ:
		return si1145_store_samp_freq(data, val);
	default:
		return -EINVAL;
	}
}

#define SI1145_ST { \
	.sign = 'u', \
	.realbits = 16, \
	.storagebits = 16, \
	.endianness = IIO_LE, \
}

#define SI1145_INTENSITY_CHANNEL(_si) { \
	.type = IIO_INTENSITY, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
			      BIT(IIO_CHAN_INFO_OFFSET) | \
			      BIT(IIO_CHAN_INFO_SCALE), \
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	.scan_type = SI1145_ST, \
	.scan_index = _si, \
	.address = SI1145_REG_ALSVIS_DATA, \
}

#define SI1145_INTENSITY_IR_CHANNEL(_si) { \
	.type = IIO_INTENSITY, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
			      BIT(IIO_CHAN_INFO_OFFSET) | \
			      BIT(IIO_CHAN_INFO_SCALE), \
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	.modified = 1, \
	.channel2 = IIO_MOD_LIGHT_IR, \
	.scan_type = SI1145_ST, \
	.scan_index = _si, \
	.address = SI1145_REG_ALSIR_DATA, \
}

#define SI1145_TEMP_CHANNEL(_si) { \
	.type = IIO_TEMP, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
			      BIT(IIO_CHAN_INFO_OFFSET) | \
			      BIT(IIO_CHAN_INFO_SCALE), \
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	.scan_type = SI1145_ST, \
	.scan_index = _si, \
	.address = SI1145_REG_AUX_DATA, \
}

#define SI1145_UV_CHANNEL(_si) { \
	.type = IIO_UVINDEX, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
			      BIT(IIO_CHAN_INFO_SCALE), \
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	.scan_type = SI1145_ST, \
	.scan_index = _si, \
	.address = SI1145_REG_AUX_DATA, \
}

#define SI1145_PROXIMITY_CHANNEL(_si, _ch) { \
	.type = IIO_PROXIMITY, \
	.indexed = 1, \
	.channel = _ch, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) | \
				    BIT(IIO_CHAN_INFO_OFFSET), \
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	.scan_type = SI1145_ST, \
	.scan_index = _si, \
	.address = SI1145_REG_PS1_DATA + _ch * 2, \
}

#define SI1145_VOLTAGE_CHANNEL(_si) { \
	.type = IIO_VOLTAGE, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	.scan_type = SI1145_ST, \
	.scan_index = _si, \
	.address = SI1145_REG_AUX_DATA, \
}

#define SI1145_CURRENT_CHANNEL(_ch) { \
	.type = IIO_CURRENT, \
	.indexed = 1, \
	.channel = _ch, \
	.output = 1, \
	.scan_index = -1, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
}

static const struct iio_chan_spec si1132_channels[] = {
	SI1145_INTENSITY_CHANNEL(0),
	SI1145_INTENSITY_IR_CHANNEL(1),
	SI1145_TEMP_CHANNEL(2),
	SI1145_VOLTAGE_CHANNEL(3),
	SI1145_UV_CHANNEL(4),
	IIO_CHAN_SOFT_TIMESTAMP(6),
};

static const struct iio_chan_spec si1141_channels[] = {
	SI1145_INTENSITY_CHANNEL(0),
	SI1145_INTENSITY_IR_CHANNEL(1),
	SI1145_PROXIMITY_CHANNEL(2, 0),
	SI1145_TEMP_CHANNEL(3),
	SI1145_VOLTAGE_CHANNEL(4),
	IIO_CHAN_SOFT_TIMESTAMP(5),
	SI1145_CURRENT_CHANNEL(0),
};

static const struct iio_chan_spec si1142_channels[] = {
	SI1145_INTENSITY_CHANNEL(0),
	SI1145_INTENSITY_IR_CHANNEL(1),
	SI1145_PROXIMITY_CHANNEL(2, 0),
	SI1145_PROXIMITY_CHANNEL(3, 1),
	SI1145_TEMP_CHANNEL(4),
	SI1145_VOLTAGE_CHANNEL(5),
	IIO_CHAN_SOFT_TIMESTAMP(6),
	SI1145_CURRENT_CHANNEL(0),
	SI1145_CURRENT_CHANNEL(1),
};

static const struct iio_chan_spec si1143_channels[] = {
	SI1145_INTENSITY_CHANNEL(0),
	SI1145_INTENSITY_IR_CHANNEL(1),
	SI1145_PROXIMITY_CHANNEL(2, 0),
	SI1145_PROXIMITY_CHANNEL(3, 1),
	SI1145_PROXIMITY_CHANNEL(4, 2),
	SI1145_TEMP_CHANNEL(5),
	SI1145_VOLTAGE_CHANNEL(6),
	IIO_CHAN_SOFT_TIMESTAMP(7),
	SI1145_CURRENT_CHANNEL(0),
	SI1145_CURRENT_CHANNEL(1),
	SI1145_CURRENT_CHANNEL(2),
};

static const struct iio_chan_spec si1145_channels[] = {
	SI1145_INTENSITY_CHANNEL(0),
	SI1145_INTENSITY_IR_CHANNEL(1),
	SI1145_PROXIMITY_CHANNEL(2, 0),
	SI1145_TEMP_CHANNEL(3),
	SI1145_VOLTAGE_CHANNEL(4),
	SI1145_UV_CHANNEL(5),
	IIO_CHAN_SOFT_TIMESTAMP(6),
	SI1145_CURRENT_CHANNEL(0),
};

static const struct iio_chan_spec si1146_channels[] = {
	SI1145_INTENSITY_CHANNEL(0),
	SI1145_INTENSITY_IR_CHANNEL(1),
	SI1145_TEMP_CHANNEL(2),
	SI1145_VOLTAGE_CHANNEL(3),
	SI1145_UV_CHANNEL(4),
	SI1145_PROXIMITY_CHANNEL(5, 0),
	SI1145_PROXIMITY_CHANNEL(6, 1),
	IIO_CHAN_SOFT_TIMESTAMP(7),
	SI1145_CURRENT_CHANNEL(0),
	SI1145_CURRENT_CHANNEL(1),
};

static const struct iio_chan_spec si1147_channels[] = {
	SI1145_INTENSITY_CHANNEL(0),
	SI1145_INTENSITY_IR_CHANNEL(1),
	SI1145_PROXIMITY_CHANNEL(2, 0),
	SI1145_PROXIMITY_CHANNEL(3, 1),
	SI1145_PROXIMITY_CHANNEL(4, 2),
	SI1145_TEMP_CHANNEL(5),
	SI1145_VOLTAGE_CHANNEL(6),
	SI1145_UV_CHANNEL(7),
	IIO_CHAN_SOFT_TIMESTAMP(8),
	SI1145_CURRENT_CHANNEL(0),
	SI1145_CURRENT_CHANNEL(1),
	SI1145_CURRENT_CHANNEL(2),
};

static struct attribute *si1132_attributes[] = {
	&iio_const_attr_in_intensity_scale_available.dev_attr.attr,
	&iio_const_attr_in_intensity_ir_scale_available.dev_attr.attr,
	NULL,
};

static struct attribute *si114x_attributes[] = {
	&iio_const_attr_in_intensity_scale_available.dev_attr.attr,
	&iio_const_attr_in_intensity_ir_scale_available.dev_attr.attr,
	&iio_const_attr_in_proximity_scale_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group si1132_attribute_group = {
	.attrs = si1132_attributes,
};

static const struct attribute_group si114x_attribute_group = {
	.attrs = si114x_attributes,
};


static const struct iio_info si1132_info = {
	.read_raw = si1145_read_raw,
	.write_raw = si1145_write_raw,
	.attrs = &si1132_attribute_group,
};

static const struct iio_info si114x_info = {
	.read_raw = si1145_read_raw,
	.write_raw = si1145_write_raw,
	.attrs = &si114x_attribute_group,
};

#define SI1145_PART(id, iio_info, chans, leds, uncompressed_meas_rate) \
	{id, iio_info, chans, ARRAY_SIZE(chans), leds, uncompressed_meas_rate}

static const struct si1145_part_info si1145_part_info[] = {
	[SI1132] = SI1145_PART(0x32, &si1132_info, si1132_channels, 0, true),
	[SI1141] = SI1145_PART(0x41, &si114x_info, si1141_channels, 1, false),
	[SI1142] = SI1145_PART(0x42, &si114x_info, si1142_channels, 2, false),
	[SI1143] = SI1145_PART(0x43, &si114x_info, si1143_channels, 3, false),
	[SI1145] = SI1145_PART(0x45, &si114x_info, si1145_channels, 1, true),
	[SI1146] = SI1145_PART(0x46, &si114x_info, si1146_channels, 2, true),
	[SI1147] = SI1145_PART(0x47, &si114x_info, si1147_channels, 3, true),
};

static int si1145_initialize(struct si1145_data *data)
{
	struct i2c_client *client = data->client;
	int ret;

	ret = i2c_smbus_write_byte_data(client, SI1145_REG_COMMAND,
					SI1145_CMD_RESET);
	if (ret < 0)
		return ret;
	msleep(SI1145_COMMAND_TIMEOUT_MS);

	/* Hardware key, magic value */
	ret = i2c_smbus_write_byte_data(client, SI1145_REG_HW_KEY, 0x17);
	if (ret < 0)
		return ret;
	msleep(SI1145_COMMAND_TIMEOUT_MS);

	/* Turn off autonomous mode */
	ret = si1145_set_meas_rate(data, 0);
	if (ret < 0)
		return ret;

	/* Initialize sampling freq to 10 Hz */
	ret = si1145_store_samp_freq(data, 10);
	if (ret < 0)
		return ret;

	/* Set LED currents to 45 mA; have 4 bits, see Table 2 in datasheet */
	switch (data->part_info->num_leds) {
	case 3:
		ret = i2c_smbus_write_byte_data(client,
						SI1145_REG_PS_LED3,
						SI1145_LED_CURRENT_45mA);
		if (ret < 0)
			return ret;
		/* fallthrough */
	case 2:
		ret = i2c_smbus_write_byte_data(client,
						SI1145_REG_PS_LED21,
						(SI1145_LED_CURRENT_45mA << 4) |
						SI1145_LED_CURRENT_45mA);
		break;
	case 1:
		ret = i2c_smbus_write_byte_data(client,
						SI1145_REG_PS_LED21,
						SI1145_LED_CURRENT_45mA);
		break;
	default:
		ret = 0;
		break;
	}
	if (ret < 0)
		return ret;

	/* Set normal proximity measurement mode */
	ret = si1145_param_set(data, SI1145_PARAM_PS_ADC_MISC,
			       SI1145_PS_ADC_MODE_NORMAL);
	if (ret < 0)
		return ret;

	ret = si1145_param_set(data, SI1145_PARAM_PS_ADC_GAIN, 0x01);
	if (ret < 0)
		return ret;

	/* ADC_COUNTER should be one complement of ADC_GAIN */
	ret = si1145_param_set(data, SI1145_PARAM_PS_ADC_COUNTER, 0x06 << 4);
	if (ret < 0)
		return ret;

	/* Set ALS visible measurement mode */
	ret = si1145_param_set(data, SI1145_PARAM_ALSVIS_ADC_MISC,
			       SI1145_ADC_MISC_RANGE);
	if (ret < 0)
		return ret;

	ret = si1145_param_set(data, SI1145_PARAM_ALSVIS_ADC_GAIN, 0x03);
	if (ret < 0)
		return ret;

	ret = si1145_param_set(data, SI1145_PARAM_ALSVIS_ADC_COUNTER,
			       0x04 << 4);
	if (ret < 0)
		return ret;

	/* Set ALS IR measurement mode */
	ret = si1145_param_set(data, SI1145_PARAM_ALSIR_ADC_MISC,
			       SI1145_ADC_MISC_RANGE);
	if (ret < 0)
		return ret;

	ret = si1145_param_set(data, SI1145_PARAM_ALSIR_ADC_GAIN, 0x01);
	if (ret < 0)
		return ret;

	ret = si1145_param_set(data, SI1145_PARAM_ALSIR_ADC_COUNTER,
			       0x06 << 4);
	if (ret < 0)
		return ret;

	/*
	 * Initialize UCOEF to default values in datasheet
	 * These registers are normally zero on reset
	 */
	if (data->part_info == &si1145_part_info[SI1132] ||
		data->part_info == &si1145_part_info[SI1145] ||
		data->part_info == &si1145_part_info[SI1146] ||
		data->part_info == &si1145_part_info[SI1147]) {
		ret = i2c_smbus_write_byte_data(data->client,
						SI1145_REG_UCOEF1,
						SI1145_UCOEF1_DEFAULT);
		if (ret < 0)
			return ret;
		ret = i2c_smbus_write_byte_data(data->client,
				SI1145_REG_UCOEF2, SI1145_UCOEF2_DEFAULT);
		if (ret < 0)
			return ret;
		ret = i2c_smbus_write_byte_data(data->client,
				SI1145_REG_UCOEF3, SI1145_UCOEF3_DEFAULT);
		if (ret < 0)
			return ret;
		ret = i2c_smbus_write_byte_data(data->client,
				SI1145_REG_UCOEF4, SI1145_UCOEF4_DEFAULT);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/*
 * Program the channels we want to measure with CMD_PSALS_AUTO. No need for
 * _postdisable as we stop with CMD_PSALS_PAUSE; single measurement (direct)
 * mode reprograms the channels list anyway...
 */
static int si1145_buffer_preenable(struct iio_dev *indio_dev)
{
	struct si1145_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->lock);
	ret = si1145_set_chlist(indio_dev, *indio_dev->active_scan_mask);
	mutex_unlock(&data->lock);

	return ret;
}

static bool si1145_validate_scan_mask(struct iio_dev *indio_dev,
			       const unsigned long *scan_mask)
{
	struct si1145_data *data = iio_priv(indio_dev);
	unsigned int count = 0;
	int i;

	/* Check that at most one AUX channel is enabled */
	for_each_set_bit(i, scan_mask, data->part_info->num_channels) {
		if (indio_dev->channels[i].address == SI1145_REG_AUX_DATA)
			count++;
	}

	return count <= 1;
}

static const struct iio_buffer_setup_ops si1145_buffer_setup_ops = {
	.preenable = si1145_buffer_preenable,
	.postenable = iio_triggered_buffer_postenable,
	.predisable = iio_triggered_buffer_predisable,
	.validate_scan_mask = si1145_validate_scan_mask,
};

/**
 * si1145_trigger_set_state() - Set trigger state
 *
 * When not using triggers interrupts are disabled and measurement rate is
 * set to zero in order to minimize power consumption.
 */
static int si1145_trigger_set_state(struct iio_trigger *trig, bool state)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct si1145_data *data = iio_priv(indio_dev);
	int err = 0, ret;

	mutex_lock(&data->lock);

	if (state) {
		data->autonomous = true;
		err = i2c_smbus_write_byte_data(data->client,
				SI1145_REG_INT_CFG, SI1145_INT_CFG_OE);
		if (err < 0)
			goto disable;
		err = i2c_smbus_write_byte_data(data->client,
				SI1145_REG_IRQ_ENABLE, SI1145_MASK_ALL_IE);
		if (err < 0)
			goto disable;
		err = si1145_set_meas_rate(data, data->meas_rate);
		if (err < 0)
			goto disable;
		err = si1145_command(data, SI1145_CMD_PSALS_AUTO);
		if (err < 0)
			goto disable;
	} else {
disable:
		/* Disable as much as possible skipping errors */
		ret = si1145_command(data, SI1145_CMD_PSALS_PAUSE);
		if (ret < 0 && !err)
			err = ret;
		ret = si1145_set_meas_rate(data, 0);
		if (ret < 0 && !err)
			err = ret;
		ret = i2c_smbus_write_byte_data(data->client,
						SI1145_REG_IRQ_ENABLE, 0);
		if (ret < 0 && !err)
			err = ret;
		ret = i2c_smbus_write_byte_data(data->client,
						SI1145_REG_INT_CFG, 0);
		if (ret < 0 && !err)
			err = ret;
		data->autonomous = false;
	}

	mutex_unlock(&data->lock);
	return err;
}

static const struct iio_trigger_ops si1145_trigger_ops = {
	.set_trigger_state = si1145_trigger_set_state,
};

static int si1145_probe_trigger(struct iio_dev *indio_dev)
{
	struct si1145_data *data = iio_priv(indio_dev);
	struct i2c_client *client = data->client;
	struct iio_trigger *trig;
	int ret;

	trig = devm_iio_trigger_alloc(&client->dev,
			"%s-dev%d", indio_dev->name, indio_dev->id);
	if (!trig)
		return -ENOMEM;

	trig->dev.parent = &client->dev;
	trig->ops = &si1145_trigger_ops;
	iio_trigger_set_drvdata(trig, indio_dev);

	ret = devm_request_irq(&client->dev, client->irq,
			  iio_trigger_generic_data_rdy_poll,
			  IRQF_TRIGGER_FALLING,
			  "si1145_irq",
			  trig);
	if (ret < 0) {
		dev_err(&client->dev, "irq request failed\n");
		return ret;
	}

	ret = iio_trigger_register(trig);
	if (ret)
		return ret;

	data->trig = trig;
	indio_dev->trig = iio_trigger_get(data->trig);

	return 0;
}

static void si1145_remove_trigger(struct iio_dev *indio_dev)
{
	struct si1145_data *data = iio_priv(indio_dev);

	if (data->trig) {
		iio_trigger_unregister(data->trig);
		data->trig = NULL;
	}
}

static int si1145_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct si1145_data *data;
	struct iio_dev *indio_dev;
	u8 part_id, rev_id, seq_id;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;
	data->part_info = &si1145_part_info[id->driver_data];

	part_id = ret = i2c_smbus_read_byte_data(data->client,
						 SI1145_REG_PART_ID);
	if (ret < 0)
		return ret;
	rev_id = ret = i2c_smbus_read_byte_data(data->client,
						SI1145_REG_REV_ID);
	if (ret < 0)
		return ret;
	seq_id = ret = i2c_smbus_read_byte_data(data->client,
						SI1145_REG_SEQ_ID);
	if (ret < 0)
		return ret;
	dev_info(&client->dev, "device ID part %#02hhx rev %#02hhx seq %#02hhx\n",
			part_id, rev_id, seq_id);
	if (part_id != data->part_info->part) {
		dev_err(&client->dev, "part ID mismatch got %#02hhx, expected %#02x\n",
				part_id, data->part_info->part);
		return -ENODEV;
	}

	indio_dev->dev.parent = &client->dev;
	indio_dev->name = id->name;
	indio_dev->channels = data->part_info->channels;
	indio_dev->num_channels = data->part_info->num_channels;
	indio_dev->info = data->part_info->iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	mutex_init(&data->lock);
	mutex_init(&data->cmdlock);

	ret = si1145_initialize(data);
	if (ret < 0)
		return ret;

	ret = iio_triggered_buffer_setup(indio_dev, NULL,
		si1145_trigger_handler, &si1145_buffer_setup_ops);
	if (ret < 0)
		return ret;

	if (client->irq) {
		ret = si1145_probe_trigger(indio_dev);
		if (ret < 0)
			goto error_free_buffer;
	} else {
		dev_info(&client->dev, "no irq, using polling\n");
	}

	ret = iio_device_register(indio_dev);
	if (ret < 0)
		goto error_free_trigger;

	return 0;

error_free_trigger:
	si1145_remove_trigger(indio_dev);
error_free_buffer:
	iio_triggered_buffer_cleanup(indio_dev);

	return ret;
}

static const struct i2c_device_id si1145_ids[] = {
	{ "si1132", SI1132 },
	{ "si1141", SI1141 },
	{ "si1142", SI1142 },
	{ "si1143", SI1143 },
	{ "si1145", SI1145 },
	{ "si1146", SI1146 },
	{ "si1147", SI1147 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, si1145_ids);

static int si1145_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);

	iio_device_unregister(indio_dev);
	si1145_remove_trigger(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);

	return 0;
}

static struct i2c_driver si1145_driver = {
	.driver = {
		.name   = "si1145",
	},
	.probe  = si1145_probe,
	.remove = si1145_remove,
	.id_table = si1145_ids,
};

module_i2c_driver(si1145_driver);

MODULE_AUTHOR("Peter Meerwald-Stadler <pmeerw@pmeerw.net>");
MODULE_DESCRIPTION("Silabs SI1132 and SI1141/2/3/5/6/7 proximity, ambient light and UV index sensor driver");
MODULE_LICENSE("GPL");
