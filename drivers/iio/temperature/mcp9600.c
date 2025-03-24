// SPDX-License-Identifier: GPL-2.0+
/*
 * mcp9600.c - Support for Microchip MCP9600 thermocouple EMF converter
 *
 * Copyright (c) 2022 Andrew Hepp
 * Author: <andrew.hepp@ahepp.dev>
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/bits.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/math.h>
#include <linux/minmax.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>

#include <linux/iio/events.h>
#include <linux/iio/iio.h>

/* MCP9600 registers */
#define MCP9600_HOT_JUNCTION 0x0
#define MCP9600_COLD_JUNCTION 0x2
#define MCP9600_STATUS			0x4
#define MCP9600_STATUS_ALERT(x)		BIT(x)
#define MCP9600_ALERT_CFG1		0x8
#define MCP9600_ALERT_CFG(x)		(MCP9600_ALERT_CFG1 + (x - 1))
#define MCP9600_ALERT_CFG_ENABLE	BIT(0)
#define MCP9600_ALERT_CFG_ACTIVE_HIGH	BIT(2)
#define MCP9600_ALERT_CFG_FALLING	BIT(3)
#define MCP9600_ALERT_CFG_COLD_JUNCTION	BIT(4)
#define MCP9600_ALERT_HYSTERESIS1	0xc
#define MCP9600_ALERT_HYSTERESIS(x)	(MCP9600_ALERT_HYSTERESIS1 + (x - 1))
#define MCP9600_ALERT_LIMIT1		0x10
#define MCP9600_ALERT_LIMIT(x)		(MCP9600_ALERT_LIMIT1 + (x - 1))
#define MCP9600_ALERT_LIMIT_MASK	GENMASK(15, 2)
#define MCP9600_DEVICE_ID 0x20

/* MCP9600 device id value */
#define MCP9600_DEVICE_ID_MCP9600 0x40

#define MCP9600_ALERT_COUNT		4

#define MCP9600_MIN_TEMP_HOT_JUNCTION_MICRO	-200000000
#define MCP9600_MAX_TEMP_HOT_JUNCTION_MICRO	1800000000

#define MCP9600_MIN_TEMP_COLD_JUNCTION_MICRO	-40000000
#define MCP9600_MAX_TEMP_COLD_JUNCTION_MICRO	125000000

enum mcp9600_alert {
	MCP9600_ALERT1,
	MCP9600_ALERT2,
	MCP9600_ALERT3,
	MCP9600_ALERT4
};

static const char * const mcp9600_alert_name[MCP9600_ALERT_COUNT] = {
	[MCP9600_ALERT1] = "alert1",
	[MCP9600_ALERT2] = "alert2",
	[MCP9600_ALERT3] = "alert3",
	[MCP9600_ALERT4] = "alert4",
};

static const struct iio_event_spec mcp9600_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE) |
				 BIT(IIO_EV_INFO_VALUE) |
				 BIT(IIO_EV_INFO_HYSTERESIS),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE) |
				 BIT(IIO_EV_INFO_VALUE) |
				 BIT(IIO_EV_INFO_HYSTERESIS),
	},
};

#define MCP9600_CHANNELS(hj_num_ev, hj_ev_spec_off, cj_num_ev, cj_ev_spec_off) \
	{								       \
		{							       \
			.type = IIO_TEMP,				       \
			.address = MCP9600_HOT_JUNCTION,		       \
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |	       \
					      BIT(IIO_CHAN_INFO_SCALE),	       \
			.event_spec = &mcp9600_events[hj_ev_spec_off],	       \
			.num_event_specs = hj_num_ev,			       \
		},							       \
		{							       \
			.type = IIO_TEMP,				       \
			.address = MCP9600_COLD_JUNCTION,		       \
			.channel2 = IIO_MOD_TEMP_AMBIENT,		       \
			.modified = 1,					       \
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |	       \
					      BIT(IIO_CHAN_INFO_SCALE),	       \
			.event_spec = &mcp9600_events[cj_ev_spec_off],	       \
			.num_event_specs = cj_num_ev,			       \
		},							       \
	}

static const struct iio_chan_spec mcp9600_channels[][2] = {
	MCP9600_CHANNELS(0, 0, 0, 0), /* Alerts: - - - - */
	MCP9600_CHANNELS(1, 0, 0, 0), /* Alerts: 1 - - - */
	MCP9600_CHANNELS(1, 1, 0, 0), /* Alerts: - 2 - - */
	MCP9600_CHANNELS(2, 0, 0, 0), /* Alerts: 1 2 - - */
	MCP9600_CHANNELS(0, 0, 1, 0), /* Alerts: - - 3 - */
	MCP9600_CHANNELS(1, 0, 1, 0), /* Alerts: 1 - 3 - */
	MCP9600_CHANNELS(1, 1, 1, 0), /* Alerts: - 2 3 - */
	MCP9600_CHANNELS(2, 0, 1, 0), /* Alerts: 1 2 3 - */
	MCP9600_CHANNELS(0, 0, 1, 1), /* Alerts: - - - 4 */
	MCP9600_CHANNELS(1, 0, 1, 1), /* Alerts: 1 - - 4 */
	MCP9600_CHANNELS(1, 1, 1, 1), /* Alerts: - 2 - 4 */
	MCP9600_CHANNELS(2, 0, 1, 1), /* Alerts: 1 2 - 4 */
	MCP9600_CHANNELS(0, 0, 2, 0), /* Alerts: - - 3 4 */
	MCP9600_CHANNELS(1, 0, 2, 0), /* Alerts: 1 - 3 4 */
	MCP9600_CHANNELS(1, 1, 2, 0), /* Alerts: - 2 3 4 */
	MCP9600_CHANNELS(2, 0, 2, 0), /* Alerts: 1 2 3 4 */
};

struct mcp9600_data {
	struct i2c_client *client;
};

static int mcp9600_read(struct mcp9600_data *data,
			struct iio_chan_spec const *chan, int *val)
{
	int ret;

	ret = i2c_smbus_read_word_swapped(data->client, chan->address);

	if (ret < 0)
		return ret;

	*val = sign_extend32(ret, 15);

	return 0;
}

static int mcp9600_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int *val,
			    int *val2, long mask)
{
	struct mcp9600_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = mcp9600_read(data, chan, val);
		if (ret)
			return ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 62;
		*val2 = 500000;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int mcp9600_get_alert_index(int channel2, enum iio_event_direction dir)
{
	if (channel2 == IIO_MOD_TEMP_AMBIENT) {
		if (dir == IIO_EV_DIR_RISING)
			return MCP9600_ALERT3;
		else
			return MCP9600_ALERT4;
	} else {
		if (dir == IIO_EV_DIR_RISING)
			return MCP9600_ALERT1;
		else
			return MCP9600_ALERT2;
	}
}

static int mcp9600_read_event_config(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir)
{
	struct mcp9600_data *data = iio_priv(indio_dev);
	struct i2c_client *client = data->client;
	int i, ret;

	i = mcp9600_get_alert_index(chan->channel2, dir);
	ret = i2c_smbus_read_byte_data(client, MCP9600_ALERT_CFG(i + 1));
	if (ret < 0)
		return ret;

	return FIELD_GET(MCP9600_ALERT_CFG_ENABLE, ret);
}

static int mcp9600_write_event_config(struct iio_dev *indio_dev,
				      const struct iio_chan_spec *chan,
				      enum iio_event_type type,
				      enum iio_event_direction dir,
				      bool state)
{
	struct mcp9600_data *data = iio_priv(indio_dev);
	struct i2c_client *client = data->client;
	int i, ret;

	i = mcp9600_get_alert_index(chan->channel2, dir);
	ret = i2c_smbus_read_byte_data(client, MCP9600_ALERT_CFG(i + 1));
	if (ret < 0)
		return ret;

	if (state)
		ret |= MCP9600_ALERT_CFG_ENABLE;
	else
		ret &= ~MCP9600_ALERT_CFG_ENABLE;

	return i2c_smbus_write_byte_data(client, MCP9600_ALERT_CFG(i + 1), ret);
}

static int mcp9600_read_thresh(struct iio_dev *indio_dev,
			       const struct iio_chan_spec *chan,
			       enum iio_event_type type,
			       enum iio_event_direction dir,
			       enum iio_event_info info, int *val, int *val2)
{
	struct mcp9600_data *data = iio_priv(indio_dev);
	struct i2c_client *client = data->client;
	s32 ret;
	int i;

	i = mcp9600_get_alert_index(chan->channel2, dir);
	switch (info) {
	case IIO_EV_INFO_VALUE:
		ret = i2c_smbus_read_word_swapped(client, MCP9600_ALERT_LIMIT(i + 1));
		if (ret < 0)
			return ret;
		/*
		 * Temperature is stored in two’s complement format in
		 * bits(15:2), LSB is 0.25 degree celsius.
		 */
		*val = sign_extend32(FIELD_GET(MCP9600_ALERT_LIMIT_MASK, ret), 13);
		*val2 = 4;
		return IIO_VAL_FRACTIONAL;
	case IIO_EV_INFO_HYSTERESIS:
		ret = i2c_smbus_read_byte_data(client, MCP9600_ALERT_HYSTERESIS(i + 1));
		if (ret < 0)
			return ret;

		*val = ret;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int mcp9600_write_thresh(struct iio_dev *indio_dev,
				const struct iio_chan_spec *chan,
				enum iio_event_type type,
				enum iio_event_direction dir,
				enum iio_event_info info, int val, int val2)
{
	struct mcp9600_data *data = iio_priv(indio_dev);
	struct i2c_client *client = data->client;
	int s_val, i;
	s16 thresh;
	u8 hyst;

	i = mcp9600_get_alert_index(chan->channel2, dir);
	switch (info) {
	case IIO_EV_INFO_VALUE:
		/* Scale value to include decimal part into calculations */
		s_val = (val < 0) ? ((val * 1000000) - val2) :
				    ((val * 1000000) + val2);
		if (chan->channel2 == IIO_MOD_TEMP_AMBIENT) {
			s_val = max(s_val, MCP9600_MIN_TEMP_COLD_JUNCTION_MICRO);
			s_val = min(s_val, MCP9600_MAX_TEMP_COLD_JUNCTION_MICRO);
		} else {
			s_val = max(s_val, MCP9600_MIN_TEMP_HOT_JUNCTION_MICRO);
			s_val = min(s_val, MCP9600_MAX_TEMP_HOT_JUNCTION_MICRO);
		}

		/*
		 * Shift length 4 bits = 2(15:2) + 2(0.25 LSB), temperature is
		 * stored in two’s complement format.
		 */
		thresh = (s16)(s_val / (1000000 >> 4));
		return i2c_smbus_write_word_swapped(client,
						    MCP9600_ALERT_LIMIT(i + 1),
						    thresh);
	case IIO_EV_INFO_HYSTERESIS:
		hyst = min(abs(val), 255);
		return i2c_smbus_write_byte_data(client,
						 MCP9600_ALERT_HYSTERESIS(i + 1),
						 hyst);
	default:
		return -EINVAL;
	}
}

static const struct iio_info mcp9600_info = {
	.read_raw = mcp9600_read_raw,
	.read_event_config = mcp9600_read_event_config,
	.write_event_config = mcp9600_write_event_config,
	.read_event_value = mcp9600_read_thresh,
	.write_event_value = mcp9600_write_thresh,
};

static irqreturn_t mcp9600_alert_handler(void *private,
					 enum mcp9600_alert alert,
					 enum iio_modifier mod,
					 enum iio_event_direction dir)
{
	struct iio_dev *indio_dev = private;
	struct mcp9600_data *data = iio_priv(indio_dev);
	int ret;

	ret = i2c_smbus_read_byte_data(data->client, MCP9600_STATUS);
	if (ret < 0)
		return IRQ_HANDLED;

	if (!(ret & MCP9600_STATUS_ALERT(alert)))
		return IRQ_NONE;

	iio_push_event(indio_dev,
		       IIO_MOD_EVENT_CODE(IIO_TEMP, 0, mod, IIO_EV_TYPE_THRESH,
					  dir),
		       iio_get_time_ns(indio_dev));

	return IRQ_HANDLED;
}

static irqreturn_t mcp9600_alert1_handler(int irq, void *private)
{
	return mcp9600_alert_handler(private, MCP9600_ALERT1, IIO_NO_MOD,
				     IIO_EV_DIR_RISING);
}

static irqreturn_t mcp9600_alert2_handler(int irq, void *private)
{
	return mcp9600_alert_handler(private, MCP9600_ALERT2, IIO_NO_MOD,
				     IIO_EV_DIR_FALLING);
}

static irqreturn_t mcp9600_alert3_handler(int irq, void *private)
{
	return mcp9600_alert_handler(private, MCP9600_ALERT3,
				     IIO_MOD_TEMP_AMBIENT, IIO_EV_DIR_RISING);
}

static irqreturn_t mcp9600_alert4_handler(int irq, void *private)
{
	return mcp9600_alert_handler(private, MCP9600_ALERT4,
				     IIO_MOD_TEMP_AMBIENT, IIO_EV_DIR_FALLING);
}

static irqreturn_t (*mcp9600_alert_handler_func[MCP9600_ALERT_COUNT]) (int, void *) = {
	mcp9600_alert1_handler,
	mcp9600_alert2_handler,
	mcp9600_alert3_handler,
	mcp9600_alert4_handler,
};

static int mcp9600_probe_alerts(struct iio_dev *indio_dev)
{
	struct mcp9600_data *data = iio_priv(indio_dev);
	struct i2c_client *client = data->client;
	struct device *dev = &client->dev;
	struct fwnode_handle *fwnode = dev_fwnode(dev);
	unsigned int irq_type;
	int ret, irq, i;
	u8 val, ch_sel;

	/*
	 * alert1: hot junction, rising temperature
	 * alert2: hot junction, falling temperature
	 * alert3: cold junction, rising temperature
	 * alert4: cold junction, falling temperature
	 */
	ch_sel = 0;
	for (i = 0; i < MCP9600_ALERT_COUNT; i++) {
		irq = fwnode_irq_get_byname(fwnode, mcp9600_alert_name[i]);
		if (irq <= 0)
			continue;

		val = 0;
		irq_type = irq_get_trigger_type(irq);
		if (irq_type == IRQ_TYPE_EDGE_RISING)
			val |= MCP9600_ALERT_CFG_ACTIVE_HIGH;

		if (i == MCP9600_ALERT2 || i == MCP9600_ALERT4)
			val |= MCP9600_ALERT_CFG_FALLING;

		if (i == MCP9600_ALERT3 || i == MCP9600_ALERT4)
			val |= MCP9600_ALERT_CFG_COLD_JUNCTION;

		ret = i2c_smbus_write_byte_data(client,
						MCP9600_ALERT_CFG(i + 1),
						val);
		if (ret < 0)
			return ret;

		ret = devm_request_threaded_irq(dev, irq, NULL,
						mcp9600_alert_handler_func[i],
						IRQF_ONESHOT, "mcp9600",
						indio_dev);
		if (ret)
			return ret;

		ch_sel |= BIT(i);
	}

	return ch_sel;
}

static int mcp9600_probe(struct i2c_client *client)
{
	struct iio_dev *indio_dev;
	struct mcp9600_data *data;
	int ret, ch_sel;

	ret = i2c_smbus_read_byte_data(client, MCP9600_DEVICE_ID);
	if (ret < 0)
		return dev_err_probe(&client->dev, ret, "Failed to read device ID\n");
	if (ret != MCP9600_DEVICE_ID_MCP9600)
		dev_warn(&client->dev, "Expected ID %x, got %x\n",
				MCP9600_DEVICE_ID_MCP9600, ret);

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->client = client;

	ch_sel = mcp9600_probe_alerts(indio_dev);
	if (ch_sel < 0)
		return ch_sel;

	indio_dev->info = &mcp9600_info;
	indio_dev->name = "mcp9600";
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = mcp9600_channels[ch_sel];
	indio_dev->num_channels = ARRAY_SIZE(mcp9600_channels[ch_sel]);

	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct i2c_device_id mcp9600_id[] = {
	{ "mcp9600" },
	{}
};
MODULE_DEVICE_TABLE(i2c, mcp9600_id);

static const struct of_device_id mcp9600_of_match[] = {
	{ .compatible = "microchip,mcp9600" },
	{}
};
MODULE_DEVICE_TABLE(of, mcp9600_of_match);

static struct i2c_driver mcp9600_driver = {
	.driver = {
		.name = "mcp9600",
		.of_match_table = mcp9600_of_match,
	},
	.probe = mcp9600_probe,
	.id_table = mcp9600_id
};
module_i2c_driver(mcp9600_driver);

MODULE_AUTHOR("Dimitri Fedrau <dima.fedrau@gmail.com>");
MODULE_AUTHOR("Andrew Hepp <andrew.hepp@ahepp.dev>");
MODULE_DESCRIPTION("Microchip MCP9600 thermocouple EMF converter driver");
MODULE_LICENSE("GPL");
