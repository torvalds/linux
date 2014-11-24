/*
 * Freescale MMA9551L Intelligent Motion-Sensing Platform driver
 * Copyright (c) 2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/events.h>

#define MMA9551_DRV_NAME		"mma9551"
#define MMA9551_IRQ_NAME		"mma9551_event"
#define MMA9551_GPIO_NAME		"mma9551_int"
#define MMA9551_GPIO_COUNT		4

/* Applications IDs */
#define MMA9551_APPID_VERSION		0x00
#define MMA9551_APPID_GPIO		0x03
#define MMA9551_APPID_AFE		0x06
#define MMA9551_APPID_TILT		0x0B
#define MMA9551_APPID_SLEEP_WAKE	0x12
#define MMA9551_APPID_RESET		0x17
#define MMA9551_APPID_NONE		0xff

/* Command masks for mailbox write command */
#define MMA9551_CMD_READ_VERSION_INFO	0x00
#define MMA9551_CMD_READ_CONFIG		0x10
#define MMA9551_CMD_WRITE_CONFIG	0x20
#define MMA9551_CMD_READ_STATUS		0x30

enum mma9551_gpio_pin {
	mma9551_gpio6 = 0,
	mma9551_gpio7,
	mma9551_gpio8,
	mma9551_gpio9,
	mma9551_gpio_max = mma9551_gpio9,
};

/* Mailbox read command */
#define MMA9551_RESPONSE_COCO		BIT(7)

/* Error-Status codes returned in mailbox read command */
#define MMA9551_MCI_ERROR_NONE			0x00
#define MMA9551_MCI_ERROR_PARAM			0x04
#define MMA9551_MCI_INVALID_COUNT		0x19
#define MMA9551_MCI_ERROR_COMMAND		0x1C
#define MMA9551_MCI_ERROR_INVALID_LENGTH	0x21
#define MMA9551_MCI_ERROR_FIFO_BUSY		0x22
#define MMA9551_MCI_ERROR_FIFO_ALLOCATED	0x23
#define MMA9551_MCI_ERROR_FIFO_OVERSIZE		0x24

/* GPIO Application */
#define MMA9551_GPIO_POL_MSB		0x08
#define MMA9551_GPIO_POL_LSB		0x09

/* Sleep/Wake application */
#define MMA9551_SLEEP_CFG		0x06
#define MMA9551_SLEEP_CFG_SNCEN		BIT(0)
#define MMA9551_SLEEP_CFG_SCHEN		BIT(2)

/* AFE application */
#define MMA9551_AFE_X_ACCEL_REG		0x00
#define MMA9551_AFE_Y_ACCEL_REG		0x02
#define MMA9551_AFE_Z_ACCEL_REG		0x04

/* Tilt application (inclination in IIO terms). */
#define MMA9551_TILT_XZ_ANG_REG		0x00
#define MMA9551_TILT_YZ_ANG_REG		0x01
#define MMA9551_TILT_XY_ANG_REG		0x02
#define MMA9551_TILT_ANGFLG		BIT(7)
#define MMA9551_TILT_QUAD_REG		0x03
#define MMA9551_TILT_XY_QUAD_SHIFT	0
#define MMA9551_TILT_YZ_QUAD_SHIFT	2
#define MMA9551_TILT_XZ_QUAD_SHIFT	4
#define MMA9551_TILT_CFG_REG		0x01
#define MMA9551_TILT_ANG_THRESH_MASK	GENMASK(3, 0)

/* Tilt events are mapped to the first three GPIO pins. */
enum mma9551_tilt_axis {
	mma9551_x = 0,
	mma9551_y,
	mma9551_z,
};

/*
 * A response is composed of:
 * - control registers: MB0-3
 * - data registers: MB4-31
 *
 * A request is composed of:
 * - mbox to write to (always 0)
 * - control registers: MB1-4
 * - data registers: MB5-31
 */
#define MMA9551_MAILBOX_CTRL_REGS	4
#define MMA9551_MAX_MAILBOX_DATA_REGS	28
#define MMA9551_MAILBOX_REGS		32

#define MMA9551_I2C_READ_RETRIES	5
#define MMA9551_I2C_READ_DELAY	50	/* us */

struct mma9551_mbox_request {
	u8 start_mbox;		/* Always 0. */
	u8 app_id;
	/*
	 * See Section 5.3.1 of the MMA955xL Software Reference Manual.
	 *
	 * Bit 7: reserved, always 0
	 * Bits 6-4: command
	 * Bits 3-0: upper bits of register offset
	 */
	u8 cmd_off;
	u8 lower_off;
	u8 nbytes;
	u8 buf[MMA9551_MAX_MAILBOX_DATA_REGS - 1];
} __packed;

struct mma9551_mbox_response {
	u8 app_id;
	/*
	 * See Section 5.3.3 of the MMA955xL Software Reference Manual.
	 *
	 * Bit 7: COCO
	 * Bits 6-0: Error code.
	 */
	u8 coco_err;
	u8 nbytes;
	u8 req_bytes;
	u8 buf[MMA9551_MAX_MAILBOX_DATA_REGS];
} __packed;

struct mma9551_version_info {
	__be32 device_id;
	u8 rom_version[2];
	u8 fw_version[2];
	u8 hw_version[2];
	u8 fw_build[2];
};

struct mma9551_data {
	struct i2c_client *client;
	struct mutex mutex;
	int event_enabled[3];
	int irqs[MMA9551_GPIO_COUNT];
};

static int mma9551_transfer(struct i2c_client *client,
			    u8 app_id, u8 command, u16 offset,
			    u8 *inbytes, int num_inbytes,
			    u8 *outbytes, int num_outbytes)
{
	struct mma9551_mbox_request req;
	struct mma9551_mbox_response rsp;
	struct i2c_msg in, out;
	u8 req_len, err_code;
	int ret, retries;

	if (offset >= 1 << 12) {
		dev_err(&client->dev, "register offset too large\n");
		return -EINVAL;
	}

	req_len = 1 + MMA9551_MAILBOX_CTRL_REGS + num_inbytes;
	req.start_mbox = 0;
	req.app_id = app_id;
	req.cmd_off = command | (offset >> 8);
	req.lower_off = offset;

	if (command == MMA9551_CMD_WRITE_CONFIG)
		req.nbytes = num_inbytes;
	else
		req.nbytes = num_outbytes;
	if (num_inbytes)
		memcpy(req.buf, inbytes, num_inbytes);

	out.addr = client->addr;
	out.flags = 0;
	out.len = req_len;
	out.buf = (u8 *)&req;

	ret = i2c_transfer(client->adapter, &out, 1);
	if (ret < 0) {
		dev_err(&client->dev, "i2c write failed\n");
		return ret;
	}

	retries = MMA9551_I2C_READ_RETRIES;
	do {
		udelay(MMA9551_I2C_READ_DELAY);

		in.addr = client->addr;
		in.flags = I2C_M_RD;
		in.len = sizeof(rsp);
		in.buf = (u8 *)&rsp;

		ret = i2c_transfer(client->adapter, &in, 1);
		if (ret < 0) {
			dev_err(&client->dev, "i2c read failed\n");
			return ret;
		}

		if (rsp.coco_err & MMA9551_RESPONSE_COCO)
			break;
	} while (--retries > 0);

	if (retries == 0) {
		dev_err(&client->dev,
			"timed out while waiting for command response\n");
		return -ETIMEDOUT;
	}

	if (rsp.app_id != app_id) {
		dev_err(&client->dev,
			"app_id mismatch in response got %02x expected %02x\n",
			rsp.app_id, app_id);
		return -EINVAL;
	}

	err_code = rsp.coco_err & ~MMA9551_RESPONSE_COCO;
	if (err_code != MMA9551_MCI_ERROR_NONE) {
		dev_err(&client->dev, "read returned error %x\n", err_code);
		return -EINVAL;
	}

	if (rsp.nbytes != rsp.req_bytes) {
		dev_err(&client->dev,
			"output length mismatch got %d expected %d\n",
			rsp.nbytes, rsp.req_bytes);
		return -EINVAL;
	}

	if (num_outbytes)
		memcpy(outbytes, rsp.buf, num_outbytes);

	return 0;
}

static int mma9551_read_config_byte(struct i2c_client *client, u8 app_id,
				    u16 reg, u8 *val)
{
	return mma9551_transfer(client, app_id, MMA9551_CMD_READ_CONFIG,
				reg, NULL, 0, val, 1);
}

static int mma9551_write_config_byte(struct i2c_client *client, u8 app_id,
				     u16 reg, u8 val)
{
	return mma9551_transfer(client, app_id, MMA9551_CMD_WRITE_CONFIG, reg,
				&val, 1, NULL, 0);
}

static int mma9551_read_status_byte(struct i2c_client *client, u8 app_id,
				    u16 reg, u8 *val)
{
	return mma9551_transfer(client, app_id, MMA9551_CMD_READ_STATUS,
				reg, NULL, 0, val, 1);
}

static int mma9551_read_status_word(struct i2c_client *client, u8 app_id,
				    u16 reg, u16 *val)
{
	int ret;
	__be16 v;

	ret = mma9551_transfer(client, app_id, MMA9551_CMD_READ_STATUS,
			       reg, NULL, 0, (u8 *)&v, 2);
	*val = be16_to_cpu(v);

	return ret;
}

static int mma9551_update_config_bits(struct i2c_client *client, u8 app_id,
				      u16 reg, u8 mask, u8 val)
{
	int ret;
	u8 tmp, orig;

	ret = mma9551_read_config_byte(client, app_id, reg, &orig);
	if (ret < 0)
		return ret;

	tmp = orig & ~mask;
	tmp |= val & mask;

	if (tmp == orig)
		return 0;

	return mma9551_write_config_byte(client, app_id, reg, tmp);
}

/*
 * The polarity parameter is described in section 6.2.2, page 66, of the
 * Software Reference Manual.  Basically, polarity=0 means the interrupt
 * line has the same value as the selected bit, while polarity=1 means
 * the line is inverted.
 */
static int mma9551_gpio_config(struct i2c_client *client,
			       enum mma9551_gpio_pin pin,
			       u8 app_id, u8 bitnum, int polarity)
{
	u8 reg, pol_mask, pol_val;
	int ret;

	if (pin > mma9551_gpio_max) {
		dev_err(&client->dev, "bad GPIO pin\n");
		return -EINVAL;
	}

	/*
	 * Pin 6 is configured by regs 0x00 and 0x01, pin 7 by 0x02 and
	 * 0x03, and so on.
	 */
	reg = pin * 2;

	ret = mma9551_write_config_byte(client, MMA9551_APPID_GPIO,
					reg, app_id);
	if (ret < 0) {
		dev_err(&client->dev, "error setting GPIO app_id\n");
		return ret;
	}

	ret = mma9551_write_config_byte(client, MMA9551_APPID_GPIO,
					reg + 1, bitnum);
	if (ret < 0) {
		dev_err(&client->dev, "error setting GPIO bit number\n");
		return ret;
	}

	switch (pin) {
	case mma9551_gpio6:
		reg = MMA9551_GPIO_POL_LSB;
		pol_mask = 1 << 6;
		break;
	case mma9551_gpio7:
		reg = MMA9551_GPIO_POL_LSB;
		pol_mask = 1 << 7;
		break;
	case mma9551_gpio8:
		reg = MMA9551_GPIO_POL_MSB;
		pol_mask = 1 << 0;
		break;
	case mma9551_gpio9:
		reg = MMA9551_GPIO_POL_MSB;
		pol_mask = 1 << 1;
		break;
	}
	pol_val = polarity ? pol_mask : 0;

	ret = mma9551_update_config_bits(client, MMA9551_APPID_GPIO, reg,
					 pol_mask, pol_val);
	if (ret < 0)
		dev_err(&client->dev, "error setting GPIO polarity\n");

	return ret;
}

static int mma9551_read_version(struct i2c_client *client)
{
	struct mma9551_version_info info;
	int ret;

	ret = mma9551_transfer(client, MMA9551_APPID_VERSION, 0x00, 0x00,
			       NULL, 0, (u8 *)&info, sizeof(info));
	if (ret < 0)
		return ret;

	dev_info(&client->dev, "Device ID 0x%x, firmware version %02x.%02x\n",
		 be32_to_cpu(info.device_id), info.fw_version[0],
		 info.fw_version[1]);

	return 0;
}

/*
 * Use 'false' as the second parameter to cause the device to enter
 * sleep.
 */
static int mma9551_set_device_state(struct i2c_client *client,
				    bool enable)
{
	return mma9551_update_config_bits(client, MMA9551_APPID_SLEEP_WAKE,
					  MMA9551_SLEEP_CFG,
					  MMA9551_SLEEP_CFG_SNCEN,
					  enable ? 0 : MMA9551_SLEEP_CFG_SNCEN);
}

static int mma9551_read_incli_chan(struct i2c_client *client,
				   const struct iio_chan_spec *chan,
				   int *val)
{
	u8 quad_shift, angle, quadrant;
	u16 reg_addr;
	int ret;

	switch (chan->channel2) {
	case IIO_MOD_X:
		reg_addr = MMA9551_TILT_YZ_ANG_REG;
		quad_shift = MMA9551_TILT_YZ_QUAD_SHIFT;
		break;
	case IIO_MOD_Y:
		reg_addr = MMA9551_TILT_XZ_ANG_REG;
		quad_shift = MMA9551_TILT_XZ_QUAD_SHIFT;
		break;
	case IIO_MOD_Z:
		reg_addr = MMA9551_TILT_XY_ANG_REG;
		quad_shift = MMA9551_TILT_XY_QUAD_SHIFT;
		break;
	default:
		return -EINVAL;
	}

	ret = mma9551_read_status_byte(client, MMA9551_APPID_TILT,
				       reg_addr, &angle);
	if (ret < 0)
		return ret;

	ret = mma9551_read_status_byte(client, MMA9551_APPID_TILT,
				       MMA9551_TILT_QUAD_REG, &quadrant);
	if (ret < 0)
		return ret;

	angle &= ~MMA9551_TILT_ANGFLG;
	quadrant = (quadrant >> quad_shift) & 0x03;

	if (quadrant == 1 || quadrant == 3)
		*val = 90 * (quadrant + 1) - angle;
	else
		*val = angle + 90 * quadrant;

	return IIO_VAL_INT;
}

static int mma9551_read_accel_chan(struct i2c_client *client,
				   const struct iio_chan_spec *chan,
				   int *val, int *val2)
{
	u16 reg_addr;
	s16 raw_accel;
	int ret;

	switch (chan->channel2) {
	case IIO_MOD_X:
		reg_addr = MMA9551_AFE_X_ACCEL_REG;
		break;
	case IIO_MOD_Y:
		reg_addr = MMA9551_AFE_Y_ACCEL_REG;
		break;
	case IIO_MOD_Z:
		reg_addr = MMA9551_AFE_Z_ACCEL_REG;
		break;
	default:
		return -EINVAL;
	}

	ret = mma9551_read_status_word(client, MMA9551_APPID_AFE,
				       reg_addr, &raw_accel);
	if (ret < 0)
		return ret;

	*val = raw_accel;

	return IIO_VAL_INT;
}

static int mma9551_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct mma9551_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		switch (chan->type) {
		case IIO_INCLI:
			mutex_lock(&data->mutex);
			ret = mma9551_read_incli_chan(data->client, chan, val);
			mutex_unlock(&data->mutex);
			return ret;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_ACCEL:
			mutex_lock(&data->mutex);
			ret = mma9551_read_accel_chan(data->client,
						      chan, val, val2);
			mutex_unlock(&data->mutex);
			return ret;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_ACCEL:
			*val = 0;
			*val2 = 2440;
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int mma9551_read_event_config(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir)
{
	struct mma9551_data *data = iio_priv(indio_dev);

	switch (chan->type) {
	case IIO_INCLI:
		/* IIO counts axes from 1, because IIO_NO_MOD is 0. */
		return data->event_enabled[chan->channel2 - 1];
	default:
		return -EINVAL;
	}
}

static int mma9551_config_incli_event(struct iio_dev *indio_dev,
				      enum iio_modifier axis,
				      int state)
{
	struct mma9551_data *data = iio_priv(indio_dev);
	enum mma9551_tilt_axis mma_axis;
	int ret;

	/* IIO counts axes from 1, because IIO_NO_MOD is 0. */
	mma_axis = axis - 1;

	if (data->event_enabled[mma_axis] == state)
		return 0;

	if (state == 0) {
		ret = mma9551_gpio_config(data->client, mma_axis,
					  MMA9551_APPID_NONE, 0, 0);
		if (ret < 0)
			return ret;
	} else {
		int bitnum;

		/* Bit 7 of each angle register holds the angle flag. */
		switch (axis) {
		case IIO_MOD_X:
			bitnum = 7 + 8 * MMA9551_TILT_YZ_ANG_REG;
			break;
		case IIO_MOD_Y:
			bitnum = 7 + 8 * MMA9551_TILT_XZ_ANG_REG;
			break;
		case IIO_MOD_Z:
			bitnum = 7 + 8 * MMA9551_TILT_XY_ANG_REG;
			break;
		default:
			return -EINVAL;
		}

		ret = mma9551_gpio_config(data->client, mma_axis,
					  MMA9551_APPID_TILT, bitnum, 0);
		if (ret < 0)
			return ret;
	}

	data->event_enabled[mma_axis] = state;

	return ret;
}

static int mma9551_write_event_config(struct iio_dev *indio_dev,
				      const struct iio_chan_spec *chan,
				      enum iio_event_type type,
				      enum iio_event_direction dir,
				      int state)
{
	struct mma9551_data *data = iio_priv(indio_dev);
	int ret;

	switch (chan->type) {
	case IIO_INCLI:
		mutex_lock(&data->mutex);
		ret = mma9551_config_incli_event(indio_dev,
						 chan->channel2, state);
		mutex_unlock(&data->mutex);
		return ret;
	default:
		return -EINVAL;
	}
}

static int mma9551_write_event_value(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir,
				     enum iio_event_info info,
				     int val, int val2)
{
	struct mma9551_data *data = iio_priv(indio_dev);
	int ret;

	switch (chan->type) {
	case IIO_INCLI:
		if (val2 != 0 || val < 1 || val > 10)
			return -EINVAL;
		mutex_lock(&data->mutex);
		ret = mma9551_update_config_bits(data->client,
						 MMA9551_APPID_TILT,
						 MMA9551_TILT_CFG_REG,
						 MMA9551_TILT_ANG_THRESH_MASK,
						 val);
		mutex_unlock(&data->mutex);
		return ret;
	default:
		return -EINVAL;
	}
}

static int mma9551_read_event_value(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir,
				    enum iio_event_info info,
				    int *val, int *val2)
{
	struct mma9551_data *data = iio_priv(indio_dev);
	int ret;
	u8 tmp;

	switch (chan->type) {
	case IIO_INCLI:
		mutex_lock(&data->mutex);
		ret = mma9551_read_config_byte(data->client,
					       MMA9551_APPID_TILT,
					       MMA9551_TILT_CFG_REG, &tmp);
		mutex_unlock(&data->mutex);
		if (ret < 0)
			return ret;
		*val = tmp & MMA9551_TILT_ANG_THRESH_MASK;
		*val2 = 0;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static const struct iio_event_spec mma9551_incli_event = {
	.type = IIO_EV_TYPE_ROC,
	.dir = IIO_EV_DIR_RISING,
	.mask_separate = BIT(IIO_EV_INFO_ENABLE),
	.mask_shared_by_type = BIT(IIO_EV_INFO_VALUE),
};

#define MMA9551_ACCEL_CHANNEL(axis) {				\
	.type = IIO_ACCEL,					\
	.modified = 1,						\
	.channel2 = axis,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
}

#define MMA9551_INCLI_CHANNEL(axis) {				\
	.type = IIO_INCLI,					\
	.modified = 1,						\
	.channel2 = axis,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),	\
	.event_spec = &mma9551_incli_event,			\
	.num_event_specs = 1,					\
}

static const struct iio_chan_spec mma9551_channels[] = {
	MMA9551_ACCEL_CHANNEL(IIO_MOD_X),
	MMA9551_ACCEL_CHANNEL(IIO_MOD_Y),
	MMA9551_ACCEL_CHANNEL(IIO_MOD_Z),

	MMA9551_INCLI_CHANNEL(IIO_MOD_X),
	MMA9551_INCLI_CHANNEL(IIO_MOD_Y),
	MMA9551_INCLI_CHANNEL(IIO_MOD_Z),
};

static const struct iio_info mma9551_info = {
	.driver_module = THIS_MODULE,
	.read_raw = mma9551_read_raw,
	.read_event_config = mma9551_read_event_config,
	.write_event_config = mma9551_write_event_config,
	.read_event_value = mma9551_read_event_value,
	.write_event_value = mma9551_write_event_value,
};

static irqreturn_t mma9551_event_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct mma9551_data *data = iio_priv(indio_dev);
	int i, ret, mma_axis = -1;
	u16 reg;
	u8 val;

	mutex_lock(&data->mutex);

	for (i = 0; i < 3; i++)
		if (irq == data->irqs[i]) {
			mma_axis = i;
			break;
		}

	if (mma_axis == -1) {
		/* IRQ was triggered on 4th line, which we don't use. */
		dev_warn(&data->client->dev,
			 "irq triggered on unused line %d\n", data->irqs[3]);
		goto out;
	}

	switch (mma_axis) {
	case mma9551_x:
		reg = MMA9551_TILT_YZ_ANG_REG;
		break;
	case mma9551_y:
		reg = MMA9551_TILT_XZ_ANG_REG;
		break;
	case mma9551_z:
		reg = MMA9551_TILT_XY_ANG_REG;
		break;
	}

	/*
	 * Read the angle even though we don't use it, otherwise we
	 * won't get any further interrupts.
	 */
	ret = mma9551_read_status_byte(data->client, MMA9551_APPID_TILT,
				       reg, &val);
	if (ret < 0) {
		dev_err(&data->client->dev,
			"error %d reading tilt register in IRQ\n", ret);
		goto out;
	}

	iio_push_event(indio_dev,
		       IIO_MOD_EVENT_CODE(IIO_INCLI, 0, (mma_axis + 1),
					  IIO_EV_TYPE_ROC, IIO_EV_DIR_RISING),
		       iio_get_time_ns());

out:
	mutex_unlock(&data->mutex);

	return IRQ_HANDLED;
}

static int mma9551_init(struct mma9551_data *data)
{
	int ret;

	ret = mma9551_read_version(data->client);
	if (ret)
		return ret;

	/* Power on chip and enable doze mode. */
	return mma9551_update_config_bits(data->client,
			 MMA9551_APPID_SLEEP_WAKE,
			 MMA9551_SLEEP_CFG,
			 MMA9551_SLEEP_CFG_SCHEN | MMA9551_SLEEP_CFG_SNCEN,
			 MMA9551_SLEEP_CFG_SCHEN);
}

static int mma9551_gpio_probe(struct iio_dev *indio_dev)
{
	struct gpio_desc *gpio;
	int i, ret;
	struct mma9551_data *data = iio_priv(indio_dev);
	struct device *dev = &data->client->dev;

	for (i = 0; i < MMA9551_GPIO_COUNT; i++) {
		gpio = devm_gpiod_get_index(dev, MMA9551_GPIO_NAME, i);
		if (IS_ERR(gpio)) {
			dev_err(dev, "acpi gpio get index failed\n");
			return PTR_ERR(gpio);
		}

		ret = gpiod_direction_input(gpio);
		if (ret)
			return ret;

		data->irqs[i] = gpiod_to_irq(gpio);
		ret = devm_request_threaded_irq(dev, data->irqs[i],
				NULL, mma9551_event_handler,
				IRQF_TRIGGER_RISING | IRQF_ONESHOT,
				MMA9551_IRQ_NAME, indio_dev);
		if (ret < 0) {
			dev_err(dev, "request irq %d failed\n", data->irqs[i]);
			return ret;
		}

		dev_dbg(dev, "gpio resource, no:%d irq:%d\n",
			desc_to_gpio(gpio), data->irqs[i]);
	}

	return 0;
}

static const char *mma9551_match_acpi_device(struct device *dev)
{
	const struct acpi_device_id *id;

	id = acpi_match_device(dev->driver->acpi_match_table, dev);
	if (!id)
		return NULL;

	return dev_name(dev);
}

static int mma9551_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct mma9551_data *data;
	struct iio_dev *indio_dev;
	const char *name = NULL;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;

	if (id)
		name = id->name;
	else if (ACPI_HANDLE(&client->dev))
		name = mma9551_match_acpi_device(&client->dev);

	ret = mma9551_init(data);
	if (ret < 0)
		return ret;

	mutex_init(&data->mutex);

	indio_dev->dev.parent = &client->dev;
	indio_dev->channels = mma9551_channels;
	indio_dev->num_channels = ARRAY_SIZE(mma9551_channels);
	indio_dev->name = name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &mma9551_info;

	ret = mma9551_gpio_probe(indio_dev);
	if (ret < 0)
		goto out_poweroff;

	ret = iio_device_register(indio_dev);
	if (ret < 0) {
		dev_err(&client->dev, "unable to register iio device\n");
		goto out_poweroff;
	}

	return 0;

out_poweroff:
	mma9551_set_device_state(client, false);

	return ret;
}

static int mma9551_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct mma9551_data *data = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	mutex_lock(&data->mutex);
	mma9551_set_device_state(data->client, false);
	mutex_unlock(&data->mutex);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int mma9551_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct mma9551_data *data = iio_priv(indio_dev);

	mutex_lock(&data->mutex);
	mma9551_set_device_state(data->client, false);
	mutex_unlock(&data->mutex);

	return 0;
}

static int mma9551_resume(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct mma9551_data *data = iio_priv(indio_dev);

	mutex_lock(&data->mutex);
	mma9551_set_device_state(data->client, true);
	mutex_unlock(&data->mutex);

	return 0;
}
#else
#define mma9551_suspend NULL
#define mma9551_resume NULL
#endif

static const struct dev_pm_ops mma9551_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(mma9551_suspend, mma9551_resume)
};

static const struct acpi_device_id mma9551_acpi_match[] = {
	{"MMA9551", 0},
	{},
};

MODULE_DEVICE_TABLE(acpi, mma9551_acpi_match);

static const struct i2c_device_id mma9551_id[] = {
	{"mma9551", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, mma9551_id);

static struct i2c_driver mma9551_driver = {
	.driver = {
		   .name = MMA9551_DRV_NAME,
		   .acpi_match_table = ACPI_PTR(mma9551_acpi_match),
		   .pm = &mma9551_pm_ops,
		   },
	.probe = mma9551_probe,
	.remove = mma9551_remove,
	.id_table = mma9551_id,
};

module_i2c_driver(mma9551_driver);

MODULE_AUTHOR("Irina Tirdea <irina.tirdea@intel.com>");
MODULE_AUTHOR("Vlad Dogaru <vlad.dogaru@intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MMA9551L motion-sensing platform driver");
