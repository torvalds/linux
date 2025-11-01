// SPDX-License-Identifier: GPL-2.0-only
/*
 * Texas Instruments ADS1119 ADC driver.
 *
 * Copyright 2024 Toradex
 */

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/units.h>

#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>

#define ADS1119_CMD_RESET		0x06
#define ADS1119_CMD_POWERDOWN		0x02
#define ADS1119_CMD_START_SYNC		0x08
#define ADS1119_CMD_RDATA		0x10
#define ADS1119_CMD_RREG_CONFIG		0x20
#define ADS1119_CMD_RREG_STATUS		0x24
#define ADS1119_CMD_WREG		0x40

#define ADS1119_CMD_RREG(reg)		(0x20 | (reg) << 2)

/* Config register */
#define ADS1119_REG_CONFIG	0x00
#define ADS1119_CONFIG_VREF_FIELD	BIT(0)
#define ADS1119_CONFIG_CM_FIELD		BIT(1)
#define ADS1119_CONFIG_DR_FIELD		GENMASK(3, 2)
#define ADS1119_CONFIG_GAIN_FIELD	BIT(4)
#define ADS1119_CONFIG_MUX_FIELD	GENMASK(7, 5)

#define ADS1119_VREF_INTERNAL	0
#define ADS1119_VREF_EXTERNAL	1
#define ADS1119_VREF_INTERNAL_VAL 2048000

#define ADS1119_CM_SINGLE	0
#define ADS1119_CM_CONTINUOUS	1

#define ADS1119_DR_20_SPS	0
#define ADS1119_DR_90_SPS	1
#define ADS1119_DR_330_SPS	2
#define ADS1119_DR_1000_SPS	3

#define ADS1119_GAIN_1	0
#define ADS1119_GAIN_4	1

#define ADS1119_MUX_AIN0_AIN1	0
#define ADS1119_MUX_AIN2_AIN3	1
#define ADS1119_MUX_AIN1_AIN2	2
#define ADS1119_MUX_AIN0	3
#define ADS1119_MUX_AIN1	4
#define ADS1119_MUX_AIN2	5
#define ADS1119_MUX_AIN3	6
#define ADS1119_MUX_SHORTED	7

/* Status register */
#define ADS1119_REG_STATUS	0x01
#define ADS1119_STATUS_DRDY_FIELD	BIT(7)

#define ADS1119_DEFAULT_GAIN		1
#define ADS1119_DEFAULT_DATARATE	20

#define ADS1119_SUSPEND_DELAY		2000

/* Timeout based on the minimum sample rate of 20 SPS (50000us) */
#define ADS1119_MAX_DRDY_TIMEOUT	85000

#define ADS1119_MAX_CHANNELS		7
#define ADS1119_MAX_SINGLE_CHANNELS	4

struct ads1119_channel_config {
	int gain;
	int datarate;
	int mux;
};

struct ads1119_state {
	struct completion completion;
	struct i2c_client *client;
	struct gpio_desc *reset_gpio;
	struct iio_trigger *trig;
	struct ads1119_channel_config *channels_cfg;
	unsigned int num_channels_cfg;
	unsigned int cached_config;
	int vref_uV;
};

static const char * const ads1119_power_supplies[] = {
	"avdd", "dvdd"
};

static const int ads1119_available_datarates[] = {
	20, 90, 330, 1000,
};

static const int ads1119_available_gains[] = {
	1, 1,
	1, 4,
};

static int ads1119_upd_cfg_reg(struct ads1119_state *st, unsigned int fields,
			       unsigned int val)
{
	unsigned int config = st->cached_config;
	int ret;

	config &= ~fields;
	config |= val;

	ret = i2c_smbus_write_byte_data(st->client, ADS1119_CMD_WREG, config);
	if (ret)
		return ret;

	st->cached_config = config;

	return 0;
}

static bool ads1119_data_ready(struct ads1119_state *st)
{
	int status;

	status = i2c_smbus_read_byte_data(st->client, ADS1119_CMD_RREG_STATUS);
	if (status < 0)
		return false;

	return FIELD_GET(ADS1119_STATUS_DRDY_FIELD, status);
}

static int ads1119_reset(struct ads1119_state *st)
{
	st->cached_config = 0;

	if (!st->reset_gpio)
		return i2c_smbus_write_byte(st->client, ADS1119_CMD_RESET);

	gpiod_set_value_cansleep(st->reset_gpio, 1);
	udelay(1);
	gpiod_set_value_cansleep(st->reset_gpio, 0);
	udelay(1);

	return 0;
}

static int ads1119_set_conv_mode(struct ads1119_state *st, bool continuous)
{
	unsigned int mode;

	if (continuous)
		mode = ADS1119_CM_CONTINUOUS;
	else
		mode = ADS1119_CM_SINGLE;

	return ads1119_upd_cfg_reg(st, ADS1119_CONFIG_CM_FIELD,
				   FIELD_PREP(ADS1119_CONFIG_CM_FIELD, mode));
}

static int ads1119_get_hw_gain(int gain)
{
	if (gain == 4)
		return ADS1119_GAIN_4;
	else
		return ADS1119_GAIN_1;
}

static int ads1119_get_hw_datarate(int datarate)
{
	switch (datarate) {
	case 90:
		return ADS1119_DR_90_SPS;
	case 330:
		return ADS1119_DR_330_SPS;
	case 1000:
		return ADS1119_DR_1000_SPS;
	case 20:
	default:
		return ADS1119_DR_20_SPS;
	}
}

static int ads1119_configure_channel(struct ads1119_state *st, int mux,
				     int gain, int datarate)
{
	int ret;

	ret = ads1119_upd_cfg_reg(st, ADS1119_CONFIG_MUX_FIELD,
				  FIELD_PREP(ADS1119_CONFIG_MUX_FIELD, mux));
	if (ret)
		return ret;

	ret = ads1119_upd_cfg_reg(st, ADS1119_CONFIG_GAIN_FIELD,
				  FIELD_PREP(ADS1119_CONFIG_GAIN_FIELD,
					     ads1119_get_hw_gain(gain)));
	if (ret)
		return ret;

	return ads1119_upd_cfg_reg(st, ADS1119_CONFIG_DR_FIELD,
				   FIELD_PREP(ADS1119_CONFIG_DR_FIELD,
					      ads1119_get_hw_datarate(datarate)));
}

static int ads1119_poll_data_ready(struct ads1119_state *st,
				   struct iio_chan_spec const *chan)
{
	unsigned int datarate = st->channels_cfg[chan->address].datarate;
	unsigned long wait_time;
	bool data_ready;

	/* Poll 5 times more than the data rate */
	wait_time = DIV_ROUND_CLOSEST(MICRO, 5 * datarate);

	return read_poll_timeout(ads1119_data_ready, data_ready,
				 data_ready, wait_time,
				 ADS1119_MAX_DRDY_TIMEOUT, false, st);
}

static int ads1119_read_data(struct ads1119_state *st,
			     struct iio_chan_spec const *chan,
			     unsigned int *val)
{
	unsigned int timeout;
	int ret = 0;

	timeout = msecs_to_jiffies(ADS1119_MAX_DRDY_TIMEOUT);

	if (!st->client->irq) {
		ret = ads1119_poll_data_ready(st, chan);
		if (ret)
			return ret;
	} else if (!wait_for_completion_timeout(&st->completion, timeout)) {
		return -ETIMEDOUT;
	}

	ret = i2c_smbus_read_word_swapped(st->client, ADS1119_CMD_RDATA);
	if (ret < 0)
		return ret;

	*val = ret;

	return 0;
}

static int ads1119_single_conversion(struct ads1119_state *st,
				     struct iio_chan_spec const *chan,
				     int *val,
				     bool calib_offset)
{
	struct device *dev = &st->client->dev;
	int mux = st->channels_cfg[chan->address].mux;
	int gain = st->channels_cfg[chan->address].gain;
	int datarate = st->channels_cfg[chan->address].datarate;
	unsigned int sample;
	int ret;

	if (calib_offset)
		mux = ADS1119_MUX_SHORTED;

	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		goto pdown;

	ret = ads1119_configure_channel(st, mux, gain, datarate);
	if (ret)
		goto pdown;

	ret = i2c_smbus_write_byte(st->client, ADS1119_CMD_START_SYNC);
	if (ret)
		goto pdown;

	ret = ads1119_read_data(st, chan, &sample);
	if (ret)
		goto pdown;

	*val = sign_extend32(sample, chan->scan_type.realbits - 1);
	ret = IIO_VAL_INT;
pdown:
	pm_runtime_put_autosuspend(dev);
	return ret;
}

static int ads1119_validate_datarate(struct ads1119_state *st, int datarate)
{
	switch (datarate) {
	case 20:
	case 90:
	case 330:
	case 1000:
		return datarate;
	default:
		return -EINVAL;
	}
}

static int ads1119_read_avail(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      const int **vals, int *type, int *length,
			      long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		*type = IIO_VAL_FRACTIONAL;
		*vals = ads1119_available_gains;
		*length = ARRAY_SIZE(ads1119_available_gains);
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*type = IIO_VAL_INT;
		*vals = ads1119_available_datarates;
		*length = ARRAY_SIZE(ads1119_available_datarates);
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static int ads1119_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int *val,
			    int *val2, long mask)
{
	struct ads1119_state *st = iio_priv(indio_dev);
	unsigned int index = chan->address;
	int ret;

	if (index >= st->num_channels_cfg)
		return -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;
		ret = ads1119_single_conversion(st, chan, val, false);
		iio_device_release_direct(indio_dev);
		return ret;
	case IIO_CHAN_INFO_OFFSET:
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;
		ret = ads1119_single_conversion(st, chan, val, true);
		iio_device_release_direct(indio_dev);
		return ret;
	case IIO_CHAN_INFO_SCALE:
		*val = st->vref_uV / 1000;
		*val /= st->channels_cfg[index].gain;
		*val2 = chan->scan_type.realbits - 1;
		return IIO_VAL_FRACTIONAL_LOG2;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = st->channels_cfg[index].datarate;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int ads1119_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan, int val,
			     int val2, long mask)
{
	struct ads1119_state *st = iio_priv(indio_dev);
	unsigned int index = chan->address;
	int ret;

	if (index >= st->num_channels_cfg)
		return -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		ret = MICRO / ((val * MICRO) + val2);
		if (ret != 1 && ret != 4)
			return -EINVAL;

		st->channels_cfg[index].gain = ret;
		return 0;
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = ads1119_validate_datarate(st, val);
		if (ret < 0)
			return ret;

		st->channels_cfg[index].datarate = ret;
		return 0;
	default:
		return -EINVAL;
	}
}

static int ads1119_debugfs_reg_access(struct iio_dev *indio_dev,
				      unsigned int reg, unsigned int writeval,
				      unsigned int *readval)
{
	struct ads1119_state *st = iio_priv(indio_dev);
	int ret;

	if (reg > ADS1119_REG_STATUS)
		return -EINVAL;

	if (readval) {
		ret = i2c_smbus_read_byte_data(st->client,
					       ADS1119_CMD_RREG(reg));
		if (ret < 0)
			return ret;

		*readval = ret;
		return 0;
	}

	if (reg > ADS1119_REG_CONFIG)
		return -EINVAL;

	return i2c_smbus_write_byte_data(st->client, ADS1119_CMD_WREG,
					 writeval);
}

static const struct iio_info ads1119_info = {
	.read_avail = ads1119_read_avail,
	.read_raw = ads1119_read_raw,
	.write_raw = ads1119_write_raw,
	.debugfs_reg_access = ads1119_debugfs_reg_access,
};

static int ads1119_triggered_buffer_preenable(struct iio_dev *indio_dev)
{
	struct ads1119_state *st = iio_priv(indio_dev);
	struct device *dev = &st->client->dev;
	unsigned int index;
	int ret;

	index = find_first_bit(indio_dev->active_scan_mask,
			       iio_get_masklength(indio_dev));

	ret = ads1119_set_conv_mode(st, true);
	if (ret)
		return ret;

	ret = ads1119_configure_channel(st,
					st->channels_cfg[index].mux,
					st->channels_cfg[index].gain,
					st->channels_cfg[index].datarate);
	if (ret)
		return ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		return ret;

	return i2c_smbus_write_byte(st->client, ADS1119_CMD_START_SYNC);
}

static int ads1119_triggered_buffer_postdisable(struct iio_dev *indio_dev)
{
	struct ads1119_state *st = iio_priv(indio_dev);
	struct device *dev = &st->client->dev;
	int ret;

	ret = ads1119_set_conv_mode(st, false);
	if (ret)
		return ret;

	pm_runtime_put_autosuspend(dev);

	return 0;
}

static const struct iio_buffer_setup_ops ads1119_buffer_setup_ops = {
	.preenable = ads1119_triggered_buffer_preenable,
	.postdisable = ads1119_triggered_buffer_postdisable,
	.validate_scan_mask = &iio_validate_scan_mask_onehot,
};

static const struct iio_trigger_ops ads1119_trigger_ops = {
	.validate_device = &iio_trigger_validate_own_device,
};

static irqreturn_t ads1119_irq_handler(int irq, void *dev_id)
{
	struct iio_dev *indio_dev = dev_id;
	struct ads1119_state *st = iio_priv(indio_dev);

	if (iio_buffer_enabled(indio_dev) && iio_trigger_using_own(indio_dev))
		iio_trigger_poll(indio_dev->trig);
	else
		complete(&st->completion);

	return IRQ_HANDLED;
}

static irqreturn_t ads1119_trigger_handler(int irq, void *private)
{
	struct iio_poll_func *pf = private;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ads1119_state *st = iio_priv(indio_dev);
	struct {
		s16 sample;
		aligned_s64 timestamp;
	} scan = { };
	unsigned int index;
	int ret;

	if (!iio_trigger_using_own(indio_dev)) {
		index = find_first_bit(indio_dev->active_scan_mask,
				       iio_get_masklength(indio_dev));

		ret = ads1119_poll_data_ready(st, &indio_dev->channels[index]);
		if (ret) {
			dev_err(&st->client->dev,
				"Failed to poll data on trigger (%d)\n", ret);
			goto done;
		}
	}

	ret = i2c_smbus_read_word_swapped(st->client, ADS1119_CMD_RDATA);
	if (ret < 0) {
		dev_err(&st->client->dev,
			"Failed to read data on trigger (%d)\n", ret);
		goto done;
	}

	scan.sample = ret;

	iio_push_to_buffers_with_ts(indio_dev, &scan, sizeof(scan),
				    iio_get_time_ns(indio_dev));
done:
	iio_trigger_notify_done(indio_dev->trig);
	return IRQ_HANDLED;
}

static int ads1119_init(struct ads1119_state *st, bool vref_external)
{
	int ret;

	ret = ads1119_reset(st);
	if (ret)
		return ret;

	if (vref_external)
		return ads1119_upd_cfg_reg(st,
					   ADS1119_CONFIG_VREF_FIELD,
					   FIELD_PREP(ADS1119_CONFIG_VREF_FIELD,
						      ADS1119_VREF_EXTERNAL));
	return 0;
}

static int ads1119_map_analog_inputs_mux(int ain_pos, int ain_neg,
					 bool differential)
{
	if (ain_pos >= ADS1119_MAX_SINGLE_CHANNELS)
		return -EINVAL;

	if (!differential)
		return ADS1119_MUX_AIN0 + ain_pos;

	if (ain_pos == 0 && ain_neg == 1)
		return ADS1119_MUX_AIN0_AIN1;
	else if (ain_pos == 1 && ain_neg == 2)
		return ADS1119_MUX_AIN1_AIN2;
	else if (ain_pos == 2 && ain_neg == 3)
		return ADS1119_MUX_AIN2_AIN3;

	return -EINVAL;
}

static int ads1119_alloc_and_config_channels(struct iio_dev *indio_dev)
{
	const struct iio_chan_spec ads1119_channel =
		(const struct iio_chan_spec) {
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
		BIT(IIO_CHAN_INFO_SCALE) |
		BIT(IIO_CHAN_INFO_OFFSET) |
		BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_shared_by_all_available =
		BIT(IIO_CHAN_INFO_SCALE) |
		BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.scan_type = {
			.sign = 's',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_CPU,
		},
	};
	const struct iio_chan_spec ads1119_ts = IIO_CHAN_SOFT_TIMESTAMP(0);
	struct ads1119_state *st = iio_priv(indio_dev);
	struct iio_chan_spec *iio_channels, *chan;
	struct device *dev = &st->client->dev;
	unsigned int num_channels, i;
	bool differential;
	u32 ain[2];
	int ret;

	st->num_channels_cfg = device_get_child_node_count(dev);
	if (st->num_channels_cfg > ADS1119_MAX_CHANNELS)
		return dev_err_probe(dev, -EINVAL,
				     "Too many channels %d, max is %d\n",
				     st->num_channels_cfg,
				     ADS1119_MAX_CHANNELS);

	st->channels_cfg = devm_kcalloc(dev, st->num_channels_cfg,
					sizeof(*st->channels_cfg), GFP_KERNEL);
	if (!st->channels_cfg)
		return -ENOMEM;

	/* Allocate one more iio channel for the timestamp */
	num_channels = st->num_channels_cfg + 1;
	iio_channels = devm_kcalloc(dev, num_channels, sizeof(*iio_channels),
				    GFP_KERNEL);
	if (!iio_channels)
		return -ENOMEM;

	i = 0;

	device_for_each_child_node_scoped(dev, child) {
		chan = &iio_channels[i];

		differential = fwnode_property_present(child, "diff-channels");
		if (differential)
			ret = fwnode_property_read_u32_array(child,
							     "diff-channels",
							     ain, 2);
		else
			ret = fwnode_property_read_u32(child, "single-channel",
						       &ain[0]);

		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to get channel property\n");

		ret = ads1119_map_analog_inputs_mux(ain[0], ain[1],
						    differential);
		if (ret < 0)
			return dev_err_probe(dev, ret,
					     "Invalid channel value\n");

		st->channels_cfg[i].mux = ret;
		st->channels_cfg[i].gain = ADS1119_DEFAULT_GAIN;
		st->channels_cfg[i].datarate = ADS1119_DEFAULT_DATARATE;

		*chan = ads1119_channel;
		chan->channel = ain[0];
		chan->address = i;
		chan->scan_index = i;

		if (differential) {
			chan->channel2 = ain[1];
			chan->differential = 1;
		}

		dev_dbg(dev, "channel: index %d, mux %d\n", i,
			st->channels_cfg[i].mux);

		i++;
	}

	iio_channels[i] = ads1119_ts;
	iio_channels[i].address = i;
	iio_channels[i].scan_index = i;

	indio_dev->channels = iio_channels;
	indio_dev->num_channels = num_channels;

	return 0;
}

static void ads1119_powerdown(void *data)
{
	struct ads1119_state *st = data;

	i2c_smbus_write_byte(st->client, ADS1119_CMD_POWERDOWN);
}

static int ads1119_probe(struct i2c_client *client)
{
	struct iio_dev *indio_dev;
	struct ads1119_state *st;
	struct device *dev = &client->dev;
	bool vref_external = true;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	st->client = client;

	indio_dev->name = "ads1119";
	indio_dev->info = &ads1119_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	i2c_set_clientdata(client, indio_dev);

	ret = devm_regulator_bulk_get_enable(dev,
					     ARRAY_SIZE(ads1119_power_supplies),
					     ads1119_power_supplies);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to get and enable supplies\n");

	st->vref_uV = devm_regulator_get_enable_read_voltage(dev, "vref");
	if (st->vref_uV == -ENODEV) {
		vref_external = false;
		st->vref_uV = ADS1119_VREF_INTERNAL_VAL;
	} else if (st->vref_uV < 0) {
		return dev_err_probe(dev, st->vref_uV, "Failed to get vref\n");
	}

	st->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(st->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(st->reset_gpio),
				     "Failed to get reset gpio\n");

	ret = ads1119_alloc_and_config_channels(indio_dev);
	if (ret)
		return ret;

	init_completion(&st->completion);

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev, NULL,
					      ads1119_trigger_handler,
					      &ads1119_buffer_setup_ops);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to setup IIO buffer\n");

	if (client->irq > 0) {
		ret = devm_request_threaded_irq(dev, client->irq,
						ads1119_irq_handler,
						NULL, IRQF_ONESHOT,
						"ads1119", indio_dev);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to allocate irq\n");

		st->trig = devm_iio_trigger_alloc(dev, "%s-dev%d",
						  indio_dev->name,
						  iio_device_id(indio_dev));
		if (!st->trig)
			return -ENOMEM;

		st->trig->ops = &ads1119_trigger_ops;
		iio_trigger_set_drvdata(st->trig, indio_dev);

		ret = devm_iio_trigger_register(dev, st->trig);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to register IIO trigger\n");
	}

	ret = ads1119_init(st, vref_external);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to initialize device\n");

	pm_runtime_set_autosuspend_delay(dev, ADS1119_SUSPEND_DELAY);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_set_active(dev);

	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable pm runtime\n");

	ret = devm_add_action_or_reset(dev, ads1119_powerdown, st);
	if (ret)
		return ret;

	return devm_iio_device_register(dev, indio_dev);
}

static int ads1119_runtime_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct ads1119_state *st = iio_priv(indio_dev);

	return i2c_smbus_write_byte(st->client, ADS1119_CMD_POWERDOWN);
}

/*
 * The ADS1119 does not require a resume function because it automatically
 * powers on after a reset.
 * After a power down command, the ADS1119 can still communicate but turns off
 * its analog parts. To resume from power down, the device will power up again
 * upon receiving a start/sync command.
 */
static DEFINE_RUNTIME_DEV_PM_OPS(ads1119_pm_ops, ads1119_runtime_suspend,
				 NULL, NULL);

static const struct of_device_id __maybe_unused ads1119_of_match[] = {
	{ .compatible = "ti,ads1119" },
	{ }
};
MODULE_DEVICE_TABLE(of, ads1119_of_match);

static const struct i2c_device_id ads1119_id[] = {
	{ "ads1119" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ads1119_id);

static struct i2c_driver ads1119_driver = {
	.driver = {
		.name = "ads1119",
		.of_match_table = ads1119_of_match,
		.pm = pm_ptr(&ads1119_pm_ops),
	},
	.probe = ads1119_probe,
	.id_table = ads1119_id,
};
module_i2c_driver(ads1119_driver);

MODULE_AUTHOR("João Paulo Gonçalves <joao.goncalves@toradex.com>");
MODULE_DESCRIPTION("Texas Instruments ADS1119 ADC Driver");
MODULE_LICENSE("GPL");
