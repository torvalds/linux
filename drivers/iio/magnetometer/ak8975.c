/*
 * A sensor driver for the magnetometer AK8975.
 *
 * Magnetic compass sensor driver for monitoring magnetic flux information.
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA	02110-1301, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/acpi.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
/*
 * Register definitions, as well as various shifts and masks to get at the
 * individual fields of the registers.
 */
#define AK8975_REG_WIA			0x00
#define AK8975_DEVICE_ID		0x48

#define AK8975_REG_INFO			0x01

#define AK8975_REG_ST1			0x02
#define AK8975_REG_ST1_DRDY_SHIFT	0
#define AK8975_REG_ST1_DRDY_MASK	(1 << AK8975_REG_ST1_DRDY_SHIFT)

#define AK8975_REG_HXL			0x03
#define AK8975_REG_HXH			0x04
#define AK8975_REG_HYL			0x05
#define AK8975_REG_HYH			0x06
#define AK8975_REG_HZL			0x07
#define AK8975_REG_HZH			0x08
#define AK8975_REG_ST2			0x09
#define AK8975_REG_ST2_DERR_SHIFT	2
#define AK8975_REG_ST2_DERR_MASK	(1 << AK8975_REG_ST2_DERR_SHIFT)

#define AK8975_REG_ST2_HOFL_SHIFT	3
#define AK8975_REG_ST2_HOFL_MASK	(1 << AK8975_REG_ST2_HOFL_SHIFT)

#define AK8975_REG_CNTL			0x0A
#define AK8975_REG_CNTL_MODE_SHIFT	0
#define AK8975_REG_CNTL_MODE_MASK	(0xF << AK8975_REG_CNTL_MODE_SHIFT)
#define AK8975_REG_CNTL_MODE_POWER_DOWN	0x00
#define AK8975_REG_CNTL_MODE_ONCE	0x01
#define AK8975_REG_CNTL_MODE_SELF_TEST	0x08
#define AK8975_REG_CNTL_MODE_FUSE_ROM	0x0F

#define AK8975_REG_RSVC			0x0B
#define AK8975_REG_ASTC			0x0C
#define AK8975_REG_TS1			0x0D
#define AK8975_REG_TS2			0x0E
#define AK8975_REG_I2CDIS		0x0F
#define AK8975_REG_ASAX			0x10
#define AK8975_REG_ASAY			0x11
#define AK8975_REG_ASAZ			0x12

#define AK8975_MAX_REGS			AK8975_REG_ASAZ

/*
 * AK09912 Register definitions
 */
#define AK09912_REG_WIA1		0x00
#define AK09912_REG_WIA2		0x01
#define AK09912_DEVICE_ID		0x04
#define AK09911_DEVICE_ID		0x05

#define AK09911_REG_INFO1		0x02
#define AK09911_REG_INFO2		0x03

#define AK09912_REG_ST1			0x10

#define AK09912_REG_ST1_DRDY_SHIFT	0
#define AK09912_REG_ST1_DRDY_MASK	(1 << AK09912_REG_ST1_DRDY_SHIFT)

#define AK09912_REG_HXL			0x11
#define AK09912_REG_HXH			0x12
#define AK09912_REG_HYL			0x13
#define AK09912_REG_HYH			0x14
#define AK09912_REG_HZL			0x15
#define AK09912_REG_HZH			0x16
#define AK09912_REG_TMPS		0x17

#define AK09912_REG_ST2			0x18
#define AK09912_REG_ST2_HOFL_SHIFT	3
#define AK09912_REG_ST2_HOFL_MASK	(1 << AK09912_REG_ST2_HOFL_SHIFT)

#define AK09912_REG_CNTL1		0x30

#define AK09912_REG_CNTL2		0x31
#define AK09912_REG_CNTL_MODE_POWER_DOWN	0x00
#define AK09912_REG_CNTL_MODE_ONCE	0x01
#define AK09912_REG_CNTL_MODE_SELF_TEST	0x10
#define AK09912_REG_CNTL_MODE_FUSE_ROM	0x1F
#define AK09912_REG_CNTL2_MODE_SHIFT	0
#define AK09912_REG_CNTL2_MODE_MASK	(0x1F << AK09912_REG_CNTL2_MODE_SHIFT)

#define AK09912_REG_CNTL3		0x32

#define AK09912_REG_TS1			0x33
#define AK09912_REG_TS2			0x34
#define AK09912_REG_TS3			0x35
#define AK09912_REG_I2CDIS		0x36
#define AK09912_REG_TS4			0x37

#define AK09912_REG_ASAX		0x60
#define AK09912_REG_ASAY		0x61
#define AK09912_REG_ASAZ		0x62

#define AK09912_MAX_REGS		AK09912_REG_ASAZ

/*
 * Miscellaneous values.
 */
#define AK8975_MAX_CONVERSION_TIMEOUT	500
#define AK8975_CONVERSION_DONE_POLL_TIME 10
#define AK8975_DATA_READY_TIMEOUT	((100*HZ)/1000)

/*
 * Precalculate scale factor (in Gauss units) for each axis and
 * store in the device data.
 *
 * This scale factor is axis-dependent, and is derived from 3 calibration
 * factors ASA(x), ASA(y), and ASA(z).
 *
 * These ASA values are read from the sensor device at start of day, and
 * cached in the device context struct.
 *
 * Adjusting the flux value with the sensitivity adjustment value should be
 * done via the following formula:
 *
 * Hadj = H * ( ( ( (ASA-128)*0.5 ) / 128 ) + 1 )
 * where H is the raw value, ASA is the sensitivity adjustment, and Hadj
 * is the resultant adjusted value.
 *
 * We reduce the formula to:
 *
 * Hadj = H * (ASA + 128) / 256
 *
 * H is in the range of -4096 to 4095.  The magnetometer has a range of
 * +-1229uT.  To go from the raw value to uT is:
 *
 * HuT = H * 1229/4096, or roughly, 3/10.
 *
 * Since 1uT = 0.01 gauss, our final scale factor becomes:
 *
 * Hadj = H * ((ASA + 128) / 256) * 3/10 * 1/100
 * Hadj = H * ((ASA + 128) * 0.003) / 256
 *
 * Since ASA doesn't change, we cache the resultant scale factor into the
 * device context in ak8975_setup().
 *
 * Given we use IIO_VAL_INT_PLUS_MICRO bit when displaying the scale, we
 * multiply the stored scale value by 1e6.
 */
static long ak8975_raw_to_gauss(u16 data)
{
	return (((long)data + 128) * 3000) / 256;
}

/*
 * For AK8963 and AK09911, same calculation, but the device is less sensitive:
 *
 * H is in the range of +-8190.  The magnetometer has a range of
 * +-4912uT.  To go from the raw value to uT is:
 *
 * HuT = H * 4912/8190, or roughly, 6/10, instead of 3/10.
 */

static long ak8963_09911_raw_to_gauss(u16 data)
{
	return (((long)data + 128) * 6000) / 256;
}

/*
 * For AK09912, same calculation, except the device is more sensitive:
 *
 * H is in the range of -32752 to 32752.  The magnetometer has a range of
 * +-4912uT.  To go from the raw value to uT is:
 *
 * HuT = H * 4912/32752, or roughly, 3/20, instead of 3/10.
 */
static long ak09912_raw_to_gauss(u16 data)
{
	return (((long)data + 128) * 1500) / 256;
}

/* Compatible Asahi Kasei Compass parts */
enum asahi_compass_chipset {
	AK8975,
	AK8963,
	AK09911,
	AK09912,
	AK_MAX_TYPE
};

enum ak_ctrl_reg_addr {
	ST1,
	ST2,
	CNTL,
	ASA_BASE,
	MAX_REGS,
	REGS_END,
};

enum ak_ctrl_reg_mask {
	ST1_DRDY,
	ST2_HOFL,
	ST2_DERR,
	CNTL_MODE,
	MASK_END,
};

enum ak_ctrl_mode {
	POWER_DOWN,
	MODE_ONCE,
	SELF_TEST,
	FUSE_ROM,
	MODE_END,
};

struct ak_def {
	enum asahi_compass_chipset type;
	long (*raw_to_gauss)(u16 data);
	u16 range;
	u8 ctrl_regs[REGS_END];
	u8 ctrl_masks[MASK_END];
	u8 ctrl_modes[MODE_END];
	u8 data_regs[3];
};

static const struct ak_def ak_def_array[AK_MAX_TYPE] = {
	{
		.type = AK8975,
		.raw_to_gauss = ak8975_raw_to_gauss,
		.range = 4096,
		.ctrl_regs = {
			AK8975_REG_ST1,
			AK8975_REG_ST2,
			AK8975_REG_CNTL,
			AK8975_REG_ASAX,
			AK8975_MAX_REGS},
		.ctrl_masks = {
			AK8975_REG_ST1_DRDY_MASK,
			AK8975_REG_ST2_HOFL_MASK,
			AK8975_REG_ST2_DERR_MASK,
			AK8975_REG_CNTL_MODE_MASK},
		.ctrl_modes = {
			AK8975_REG_CNTL_MODE_POWER_DOWN,
			AK8975_REG_CNTL_MODE_ONCE,
			AK8975_REG_CNTL_MODE_SELF_TEST,
			AK8975_REG_CNTL_MODE_FUSE_ROM},
		.data_regs = {
			AK8975_REG_HXL,
			AK8975_REG_HYL,
			AK8975_REG_HZL},
	},
	{
		.type = AK8963,
		.raw_to_gauss = ak8963_09911_raw_to_gauss,
		.range = 8190,
		.ctrl_regs = {
			AK8975_REG_ST1,
			AK8975_REG_ST2,
			AK8975_REG_CNTL,
			AK8975_REG_ASAX,
			AK8975_MAX_REGS},
		.ctrl_masks = {
			AK8975_REG_ST1_DRDY_MASK,
			AK8975_REG_ST2_HOFL_MASK,
			0,
			AK8975_REG_CNTL_MODE_MASK},
		.ctrl_modes = {
			AK8975_REG_CNTL_MODE_POWER_DOWN,
			AK8975_REG_CNTL_MODE_ONCE,
			AK8975_REG_CNTL_MODE_SELF_TEST,
			AK8975_REG_CNTL_MODE_FUSE_ROM},
		.data_regs = {
			AK8975_REG_HXL,
			AK8975_REG_HYL,
			AK8975_REG_HZL},
	},
	{
		.type = AK09911,
		.raw_to_gauss = ak8963_09911_raw_to_gauss,
		.range = 8192,
		.ctrl_regs = {
			AK09912_REG_ST1,
			AK09912_REG_ST2,
			AK09912_REG_CNTL2,
			AK09912_REG_ASAX,
			AK09912_MAX_REGS},
		.ctrl_masks = {
			AK09912_REG_ST1_DRDY_MASK,
			AK09912_REG_ST2_HOFL_MASK,
			0,
			AK09912_REG_CNTL2_MODE_MASK},
		.ctrl_modes = {
			AK09912_REG_CNTL_MODE_POWER_DOWN,
			AK09912_REG_CNTL_MODE_ONCE,
			AK09912_REG_CNTL_MODE_SELF_TEST,
			AK09912_REG_CNTL_MODE_FUSE_ROM},
		.data_regs = {
			AK09912_REG_HXL,
			AK09912_REG_HYL,
			AK09912_REG_HZL},
	},
	{
		.type = AK09912,
		.raw_to_gauss = ak09912_raw_to_gauss,
		.range = 32752,
		.ctrl_regs = {
			AK09912_REG_ST1,
			AK09912_REG_ST2,
			AK09912_REG_CNTL2,
			AK09912_REG_ASAX,
			AK09912_MAX_REGS},
		.ctrl_masks = {
			AK09912_REG_ST1_DRDY_MASK,
			AK09912_REG_ST2_HOFL_MASK,
			0,
			AK09912_REG_CNTL2_MODE_MASK},
		.ctrl_modes = {
			AK09912_REG_CNTL_MODE_POWER_DOWN,
			AK09912_REG_CNTL_MODE_ONCE,
			AK09912_REG_CNTL_MODE_SELF_TEST,
			AK09912_REG_CNTL_MODE_FUSE_ROM},
		.data_regs = {
			AK09912_REG_HXL,
			AK09912_REG_HYL,
			AK09912_REG_HZL},
	}
};

/*
 * Per-instance context data for the device.
 */
struct ak8975_data {
	struct i2c_client	*client;
	const struct ak_def	*def;
	struct attribute_group	attrs;
	struct mutex		lock;
	u8			asa[3];
	long			raw_to_gauss[3];
	int			eoc_gpio;
	int			eoc_irq;
	wait_queue_head_t	data_ready_queue;
	unsigned long		flags;
	u8			cntl_cache;
};

/*
 * Return 0 if the i2c device is the one we expect.
 * return a negative error number otherwise
 */
static int ak8975_who_i_am(struct i2c_client *client,
			   enum asahi_compass_chipset type)
{
	u8 wia_val[2];
	int ret;

	/*
	 * Signature for each device:
	 * Device   |  WIA1      |  WIA2
	 * AK09912  |  DEVICE_ID |  AK09912_DEVICE_ID
	 * AK09911  |  DEVICE_ID |  AK09911_DEVICE_ID
	 * AK8975   |  DEVICE_ID |  NA
	 * AK8963   |  DEVICE_ID |  NA
	 */
	ret = i2c_smbus_read_i2c_block_data(client, AK09912_REG_WIA1,
					    2, wia_val);
	if (ret < 0) {
		dev_err(&client->dev, "Error reading WIA\n");
		return ret;
	}

	if (wia_val[0] != AK8975_DEVICE_ID)
		return -ENODEV;

	switch (type) {
	case AK8975:
	case AK8963:
		return 0;
	case AK09911:
		if (wia_val[1] == AK09911_DEVICE_ID)
			return 0;
		break;
	case AK09912:
		if (wia_val[1] == AK09912_DEVICE_ID)
			return 0;
		break;
	default:
		dev_err(&client->dev, "Type %d unknown\n", type);
	}
	return -ENODEV;
}

/*
 * Helper function to write to CNTL register.
 */
static int ak8975_set_mode(struct ak8975_data *data, enum ak_ctrl_mode mode)
{
	u8 regval;
	int ret;

	regval = (data->cntl_cache & ~data->def->ctrl_masks[CNTL_MODE]) |
		 data->def->ctrl_modes[mode];
	ret = i2c_smbus_write_byte_data(data->client,
					data->def->ctrl_regs[CNTL], regval);
	if (ret < 0) {
		return ret;
	}
	data->cntl_cache = regval;
	/* After mode change wait atleast 100us */
	usleep_range(100, 500);

	return 0;
}

/*
 * Handle data ready irq
 */
static irqreturn_t ak8975_irq_handler(int irq, void *data)
{
	struct ak8975_data *ak8975 = data;

	set_bit(0, &ak8975->flags);
	wake_up(&ak8975->data_ready_queue);

	return IRQ_HANDLED;
}

/*
 * Install data ready interrupt handler
 */
static int ak8975_setup_irq(struct ak8975_data *data)
{
	struct i2c_client *client = data->client;
	int rc;
	int irq;

	init_waitqueue_head(&data->data_ready_queue);
	clear_bit(0, &data->flags);
	if (client->irq)
		irq = client->irq;
	else
		irq = gpio_to_irq(data->eoc_gpio);

	rc = devm_request_irq(&client->dev, irq, ak8975_irq_handler,
			      IRQF_TRIGGER_RISING | IRQF_ONESHOT,
			      dev_name(&client->dev), data);
	if (rc < 0) {
		dev_err(&client->dev,
			"irq %d request failed, (gpio %d): %d\n",
			irq, data->eoc_gpio, rc);
		return rc;
	}

	data->eoc_irq = irq;

	return rc;
}


/*
 * Perform some start-of-day setup, including reading the asa calibration
 * values and caching them.
 */
static int ak8975_setup(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct ak8975_data *data = iio_priv(indio_dev);
	int ret;

	/* Write the fused rom access mode. */
	ret = ak8975_set_mode(data, FUSE_ROM);
	if (ret < 0) {
		dev_err(&client->dev, "Error in setting fuse access mode\n");
		return ret;
	}

	/* Get asa data and store in the device data. */
	ret = i2c_smbus_read_i2c_block_data(client,
					    data->def->ctrl_regs[ASA_BASE],
					    3, data->asa);
	if (ret < 0) {
		dev_err(&client->dev, "Not able to read asa data\n");
		return ret;
	}

	/* After reading fuse ROM data set power-down mode */
	ret = ak8975_set_mode(data, POWER_DOWN);
	if (ret < 0) {
		dev_err(&client->dev, "Error in setting power-down mode\n");
		return ret;
	}

	if (data->eoc_gpio > 0 || client->irq > 0) {
		ret = ak8975_setup_irq(data);
		if (ret < 0) {
			dev_err(&client->dev,
				"Error setting data ready interrupt\n");
			return ret;
		}
	}

	data->raw_to_gauss[0] = data->def->raw_to_gauss(data->asa[0]);
	data->raw_to_gauss[1] = data->def->raw_to_gauss(data->asa[1]);
	data->raw_to_gauss[2] = data->def->raw_to_gauss(data->asa[2]);

	return 0;
}

static int wait_conversion_complete_gpio(struct ak8975_data *data)
{
	struct i2c_client *client = data->client;
	u32 timeout_ms = AK8975_MAX_CONVERSION_TIMEOUT;
	int ret;

	/* Wait for the conversion to complete. */
	while (timeout_ms) {
		msleep(AK8975_CONVERSION_DONE_POLL_TIME);
		if (gpio_get_value(data->eoc_gpio))
			break;
		timeout_ms -= AK8975_CONVERSION_DONE_POLL_TIME;
	}
	if (!timeout_ms) {
		dev_err(&client->dev, "Conversion timeout happened\n");
		return -EINVAL;
	}

	ret = i2c_smbus_read_byte_data(client, data->def->ctrl_regs[ST1]);
	if (ret < 0)
		dev_err(&client->dev, "Error in reading ST1\n");

	return ret;
}

static int wait_conversion_complete_polled(struct ak8975_data *data)
{
	struct i2c_client *client = data->client;
	u8 read_status;
	u32 timeout_ms = AK8975_MAX_CONVERSION_TIMEOUT;
	int ret;

	/* Wait for the conversion to complete. */
	while (timeout_ms) {
		msleep(AK8975_CONVERSION_DONE_POLL_TIME);
		ret = i2c_smbus_read_byte_data(client,
					       data->def->ctrl_regs[ST1]);
		if (ret < 0) {
			dev_err(&client->dev, "Error in reading ST1\n");
			return ret;
		}
		read_status = ret;
		if (read_status)
			break;
		timeout_ms -= AK8975_CONVERSION_DONE_POLL_TIME;
	}
	if (!timeout_ms) {
		dev_err(&client->dev, "Conversion timeout happened\n");
		return -EINVAL;
	}

	return read_status;
}

/* Returns 0 if the end of conversion interrupt occured or -ETIME otherwise */
static int wait_conversion_complete_interrupt(struct ak8975_data *data)
{
	int ret;

	ret = wait_event_timeout(data->data_ready_queue,
				 test_bit(0, &data->flags),
				 AK8975_DATA_READY_TIMEOUT);
	clear_bit(0, &data->flags);

	return ret > 0 ? 0 : -ETIME;
}

/*
 * Emits the raw flux value for the x, y, or z axis.
 */
static int ak8975_read_axis(struct iio_dev *indio_dev, int index, int *val)
{
	struct ak8975_data *data = iio_priv(indio_dev);
	struct i2c_client *client = data->client;
	int ret;

	mutex_lock(&data->lock);

	/* Set up the device for taking a sample. */
	ret = ak8975_set_mode(data, MODE_ONCE);
	if (ret < 0) {
		dev_err(&client->dev, "Error in setting operating mode\n");
		goto exit;
	}

	/* Wait for the conversion to complete. */
	if (data->eoc_irq)
		ret = wait_conversion_complete_interrupt(data);
	else if (gpio_is_valid(data->eoc_gpio))
		ret = wait_conversion_complete_gpio(data);
	else
		ret = wait_conversion_complete_polled(data);
	if (ret < 0)
		goto exit;

	/* This will be executed only for non-interrupt based waiting case */
	if (ret & data->def->ctrl_masks[ST1_DRDY]) {
		ret = i2c_smbus_read_byte_data(client,
					       data->def->ctrl_regs[ST2]);
		if (ret < 0) {
			dev_err(&client->dev, "Error in reading ST2\n");
			goto exit;
		}
		if (ret & (data->def->ctrl_masks[ST2_DERR] |
			   data->def->ctrl_masks[ST2_HOFL])) {
			dev_err(&client->dev, "ST2 status error 0x%x\n", ret);
			ret = -EINVAL;
			goto exit;
		}
	}

	/* Read the flux value from the appropriate register
	   (the register is specified in the iio device attributes). */
	ret = i2c_smbus_read_word_data(client, data->def->data_regs[index]);
	if (ret < 0) {
		dev_err(&client->dev, "Read axis data fails\n");
		goto exit;
	}

	mutex_unlock(&data->lock);

	/* Clamp to valid range. */
	*val = clamp_t(s16, ret, -data->def->range, data->def->range);
	return IIO_VAL_INT;

exit:
	mutex_unlock(&data->lock);
	return ret;
}

static int ak8975_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2,
			   long mask)
{
	struct ak8975_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		return ak8975_read_axis(indio_dev, chan->address, val);
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = data->raw_to_gauss[chan->address];
		return IIO_VAL_INT_PLUS_MICRO;
	}
	return -EINVAL;
}

#define AK8975_CHANNEL(axis, index)					\
	{								\
		.type = IIO_MAGN,					\
		.modified = 1,						\
		.channel2 = IIO_MOD_##axis,				\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		\
			     BIT(IIO_CHAN_INFO_SCALE),			\
		.address = index,					\
	}

static const struct iio_chan_spec ak8975_channels[] = {
	AK8975_CHANNEL(X, 0), AK8975_CHANNEL(Y, 1), AK8975_CHANNEL(Z, 2),
};

static const struct iio_info ak8975_info = {
	.read_raw = &ak8975_read_raw,
	.driver_module = THIS_MODULE,
};

static const struct acpi_device_id ak_acpi_match[] = {
	{"AK8975", AK8975},
	{"AK8963", AK8963},
	{"INVN6500", AK8963},
	{"AK09911", AK09911},
	{"AK09912", AK09912},
	{ },
};
MODULE_DEVICE_TABLE(acpi, ak_acpi_match);

static const char *ak8975_match_acpi_device(struct device *dev,
					    enum asahi_compass_chipset *chipset)
{
	const struct acpi_device_id *id;

	id = acpi_match_device(dev->driver->acpi_match_table, dev);
	if (!id)
		return NULL;
	*chipset = (int)id->driver_data;

	return dev_name(dev);
}

static int ak8975_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct ak8975_data *data;
	struct iio_dev *indio_dev;
	int eoc_gpio;
	int err;
	const char *name = NULL;
	enum asahi_compass_chipset chipset = AK_MAX_TYPE;

	/* Grab and set up the supplied GPIO. */
	if (client->dev.platform_data)
		eoc_gpio = *(int *)(client->dev.platform_data);
	else if (client->dev.of_node)
		eoc_gpio = of_get_gpio(client->dev.of_node, 0);
	else
		eoc_gpio = -1;

	if (eoc_gpio == -EPROBE_DEFER)
		return -EPROBE_DEFER;

	/* We may not have a GPIO based IRQ to scan, that is fine, we will
	   poll if so */
	if (gpio_is_valid(eoc_gpio)) {
		err = devm_gpio_request_one(&client->dev, eoc_gpio,
							GPIOF_IN, "ak_8975");
		if (err < 0) {
			dev_err(&client->dev,
				"failed to request GPIO %d, error %d\n",
							eoc_gpio, err);
			return err;
		}
	}

	/* Register with IIO */
	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (indio_dev == NULL)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);

	data->client = client;
	data->eoc_gpio = eoc_gpio;
	data->eoc_irq = 0;

	/* id will be NULL when enumerated via ACPI */
	if (id) {
		chipset = (enum asahi_compass_chipset)(id->driver_data);
		name = id->name;
	} else if (ACPI_HANDLE(&client->dev))
		name = ak8975_match_acpi_device(&client->dev, &chipset);
	else
		return -ENOSYS;

	if (chipset >= AK_MAX_TYPE) {
		dev_err(&client->dev, "AKM device type unsupported: %d\n",
			chipset);
		return -ENODEV;
	}

	data->def = &ak_def_array[chipset];
	err = ak8975_who_i_am(client, data->def->type);
	if (err < 0) {
		dev_err(&client->dev, "Unexpected device\n");
		return err;
	}
	dev_dbg(&client->dev, "Asahi compass chip %s\n", name);

	/* Perform some basic start-of-day setup of the device. */
	err = ak8975_setup(client);
	if (err < 0) {
		dev_err(&client->dev, "%s initialization fails\n", name);
		return err;
	}

	mutex_init(&data->lock);
	indio_dev->dev.parent = &client->dev;
	indio_dev->channels = ak8975_channels;
	indio_dev->num_channels = ARRAY_SIZE(ak8975_channels);
	indio_dev->info = &ak8975_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->name = name;
	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct i2c_device_id ak8975_id[] = {
	{"ak8975", AK8975},
	{"ak8963", AK8963},
	{"AK8963", AK8963},
	{"ak09911", AK09911},
	{"ak09912", AK09912},
	{}
};

MODULE_DEVICE_TABLE(i2c, ak8975_id);

static const struct of_device_id ak8975_of_match[] = {
	{ .compatible = "asahi-kasei,ak8975", },
	{ .compatible = "ak8975", },
	{ .compatible = "asahi-kasei,ak8963", },
	{ .compatible = "ak8963", },
	{ .compatible = "asahi-kasei,ak09911", },
	{ .compatible = "ak09911", },
	{ .compatible = "asahi-kasei,ak09912", },
	{ .compatible = "ak09912", },
	{}
};
MODULE_DEVICE_TABLE(of, ak8975_of_match);

static struct i2c_driver ak8975_driver = {
	.driver = {
		.name	= "ak8975",
		.of_match_table = of_match_ptr(ak8975_of_match),
		.acpi_match_table = ACPI_PTR(ak_acpi_match),
	},
	.probe		= ak8975_probe,
	.id_table	= ak8975_id,
};
module_i2c_driver(ak8975_driver);

MODULE_AUTHOR("Laxman Dewangan <ldewangan@nvidia.com>");
MODULE_DESCRIPTION("AK8975 magnetometer driver");
MODULE_LICENSE("GPL");
