/*
 * Driver for older Chrome OS EC accelerometer
 *
 * Copyright 2017 Google, Inc
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This driver uses the memory mapper cros-ec interface to communicate
 * with the Chrome OS EC about accelerometer data.
 * Accelerometer access is presented through iio sysfs.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/kernel.h>
#include <linux/mfd/cros_ec.h>
#include <linux/mfd/cros_ec_commands.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/platform_device.h>

#define DRV_NAME	"cros-ec-accel-legacy"

/*
 * Sensor scale hard coded at 10 bits per g, computed as:
 * g / (2^10 - 1) = 0.009586168; with g = 9.80665 m.s^-2
 */
#define ACCEL_LEGACY_NSCALE 9586168

/* Indices for EC sensor values. */
enum {
	X,
	Y,
	Z,
	MAX_AXIS,
};

/* State data for cros_ec_accel_legacy iio driver. */
struct cros_ec_accel_legacy_state {
	struct cros_ec_device *ec;

	/*
	 * Array holding data from a single capture. 2 bytes per channel
	 * for the 3 channels plus the timestamp which is always last and
	 * 8-bytes aligned.
	 */
	s16 capture_data[8];
	s8 sign[MAX_AXIS];
	u8 sensor_num;
};

static int ec_cmd_read_u8(struct cros_ec_device *ec, unsigned int offset,
			  u8 *dest)
{
	return ec->cmd_readmem(ec, offset, 1, dest);
}

static int ec_cmd_read_u16(struct cros_ec_device *ec, unsigned int offset,
			   u16 *dest)
{
	__le16 tmp;
	int ret = ec->cmd_readmem(ec, offset, 2, &tmp);

	*dest = le16_to_cpu(tmp);

	return ret;
}

/**
 * read_ec_until_not_busy() - Read from EC status byte until it reads not busy.
 * @st: Pointer to state information for device.
 *
 * This function reads EC status until its busy bit gets cleared. It does not
 * wait indefinitely and returns -EIO if the EC status is still busy after a
 * few hundreds milliseconds.
 *
 * Return: 8-bit status if ok, -EIO on error
 */
static int read_ec_until_not_busy(struct cros_ec_accel_legacy_state *st)
{
	struct cros_ec_device *ec = st->ec;
	u8 status;
	int attempts = 0;

	ec_cmd_read_u8(ec, EC_MEMMAP_ACC_STATUS, &status);
	while (status & EC_MEMMAP_ACC_STATUS_BUSY_BIT) {
		/* Give up after enough attempts, return error. */
		if (attempts++ >= 50)
			return -EIO;

		/* Small delay every so often. */
		if (attempts % 5 == 0)
			msleep(25);

		ec_cmd_read_u8(ec, EC_MEMMAP_ACC_STATUS, &status);
	}

	return status;
}

/**
 * read_ec_accel_data_unsafe() - Read acceleration data from EC shared memory.
 * @st:        Pointer to state information for device.
 * @scan_mask: Bitmap of the sensor indices to scan.
 * @data:      Location to store data.
 *
 * This is the unsafe function for reading the EC data. It does not guarantee
 * that the EC will not modify the data as it is being read in.
 */
static void read_ec_accel_data_unsafe(struct cros_ec_accel_legacy_state *st,
				      unsigned long scan_mask, s16 *data)
{
	int i = 0;
	int num_enabled = bitmap_weight(&scan_mask, MAX_AXIS);

	/* Read all sensors enabled in scan_mask. Each value is 2 bytes. */
	while (num_enabled--) {
		i = find_next_bit(&scan_mask, MAX_AXIS, i);
		ec_cmd_read_u16(st->ec,
				EC_MEMMAP_ACC_DATA +
				sizeof(s16) *
				(1 + i + st->sensor_num * MAX_AXIS),
				data);
		*data *= st->sign[i];
		i++;
		data++;
	}
}

/**
 * read_ec_accel_data() - Read acceleration data from EC shared memory.
 * @st:        Pointer to state information for device.
 * @scan_mask: Bitmap of the sensor indices to scan.
 * @data:      Location to store data.
 *
 * This is the safe function for reading the EC data. It guarantees that
 * the data sampled was not modified by the EC while being read.
 *
 * Return: 0 if ok, -ve on error
 */
static int read_ec_accel_data(struct cros_ec_accel_legacy_state *st,
			      unsigned long scan_mask, s16 *data)
{
	u8 samp_id = 0xff;
	u8 status = 0;
	int ret;
	int attempts = 0;

	/*
	 * Continually read all data from EC until the status byte after
	 * all reads reflects that the EC is not busy and the sample id
	 * matches the sample id from before all reads. This guarantees
	 * that data read in was not modified by the EC while reading.
	 */
	while ((status & (EC_MEMMAP_ACC_STATUS_BUSY_BIT |
			  EC_MEMMAP_ACC_STATUS_SAMPLE_ID_MASK)) != samp_id) {
		/* If we have tried to read too many times, return error. */
		if (attempts++ >= 5)
			return -EIO;

		/* Read status byte until EC is not busy. */
		ret = read_ec_until_not_busy(st);
		if (ret < 0)
			return ret;
		status = ret;

		/*
		 * Store the current sample id so that we can compare to the
		 * sample id after reading the data.
		 */
		samp_id = status & EC_MEMMAP_ACC_STATUS_SAMPLE_ID_MASK;

		/* Read all EC data, format it, and store it into data. */
		read_ec_accel_data_unsafe(st, scan_mask, data);

		/* Read status byte. */
		ec_cmd_read_u8(st->ec, EC_MEMMAP_ACC_STATUS, &status);
	}

	return 0;
}

static int cros_ec_accel_legacy_read(struct iio_dev *indio_dev,
				     struct iio_chan_spec const *chan,
				     int *val, int *val2, long mask)
{
	struct cros_ec_accel_legacy_state *st = iio_priv(indio_dev);
	s16 data = 0;
	int ret = IIO_VAL_INT;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = read_ec_accel_data(st, (1 << chan->scan_index), &data);
		if (ret)
			return ret;
		*val = data;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = ACCEL_LEGACY_NSCALE;
		return IIO_VAL_INT_PLUS_NANO;
	case IIO_CHAN_INFO_CALIBBIAS:
		/* Calibration not supported. */
		*val = 0;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int cros_ec_accel_legacy_write(struct iio_dev *indio_dev,
				      struct iio_chan_spec const *chan,
				      int val, int val2, long mask)
{
	/*
	 * Do nothing but don't return an error code to allow calibration
	 * script to work.
	 */
	if (mask == IIO_CHAN_INFO_CALIBBIAS)
		return 0;

	return -EINVAL;
}

static const struct iio_info cros_ec_accel_legacy_info = {
	.read_raw = &cros_ec_accel_legacy_read,
	.write_raw = &cros_ec_accel_legacy_write,
};

/**
 * cros_ec_accel_legacy_capture() - The trigger handler function
 * @irq: The interrupt number.
 * @p:   Private data - always a pointer to the poll func.
 *
 * On a trigger event occurring, if the pollfunc is attached then this
 * handler is called as a threaded interrupt (and hence may sleep). It
 * is responsible for grabbing data from the device and pushing it into
 * the associated buffer.
 *
 * Return: IRQ_HANDLED
 */
static irqreturn_t cros_ec_accel_legacy_capture(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct cros_ec_accel_legacy_state *st = iio_priv(indio_dev);

	/* Clear capture data. */
	memset(st->capture_data, 0, sizeof(st->capture_data));

	/*
	 * Read data based on which channels are enabled in scan mask. Note
	 * that on a capture we are always reading the calibrated data.
	 */
	read_ec_accel_data(st, *indio_dev->active_scan_mask, st->capture_data);

	iio_push_to_buffers_with_timestamp(indio_dev, (void *)st->capture_data,
					   iio_get_time_ns(indio_dev));

	/*
	 * Tell the core we are done with this trigger and ready for the
	 * next one.
	 */
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static char *cros_ec_accel_legacy_loc_strings[] = {
	[MOTIONSENSE_LOC_BASE] = "base",
	[MOTIONSENSE_LOC_LID] = "lid",
	[MOTIONSENSE_LOC_MAX] = "unknown",
};

static ssize_t cros_ec_accel_legacy_loc(struct iio_dev *indio_dev,
					uintptr_t private,
					const struct iio_chan_spec *chan,
					char *buf)
{
	struct cros_ec_accel_legacy_state *st = iio_priv(indio_dev);

	return sprintf(buf, "%s\n",
		       cros_ec_accel_legacy_loc_strings[st->sensor_num +
							MOTIONSENSE_LOC_BASE]);
}

static ssize_t cros_ec_accel_legacy_id(struct iio_dev *indio_dev,
				       uintptr_t private,
				       const struct iio_chan_spec *chan,
				       char *buf)
{
	struct cros_ec_accel_legacy_state *st = iio_priv(indio_dev);

	return sprintf(buf, "%d\n", st->sensor_num);
}

static const struct iio_chan_spec_ext_info cros_ec_accel_legacy_ext_info[] = {
	{
		.name = "id",
		.shared = IIO_SHARED_BY_ALL,
		.read = cros_ec_accel_legacy_id,
	},
	{
		.name = "location",
		.shared = IIO_SHARED_BY_ALL,
		.read = cros_ec_accel_legacy_loc,
	},
	{ }
};

#define CROS_EC_ACCEL_LEGACY_CHAN(_axis)				\
	{								\
		.type = IIO_ACCEL,					\
		.channel2 = IIO_MOD_X + (_axis),			\
		.modified = 1,					        \
		.info_mask_separate =					\
			BIT(IIO_CHAN_INFO_RAW) |			\
			BIT(IIO_CHAN_INFO_CALIBBIAS),			\
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SCALE),	\
		.ext_info = cros_ec_accel_legacy_ext_info,		\
		.scan_type = {						\
			.sign = 's',					\
			.realbits = 16,					\
			.storagebits = 16,				\
		},							\
	}								\

static struct iio_chan_spec ec_accel_channels[] = {
	CROS_EC_ACCEL_LEGACY_CHAN(X),
	CROS_EC_ACCEL_LEGACY_CHAN(Y),
	CROS_EC_ACCEL_LEGACY_CHAN(Z),
	IIO_CHAN_SOFT_TIMESTAMP(MAX_AXIS)
};

static int cros_ec_accel_legacy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cros_ec_dev *ec = dev_get_drvdata(dev->parent);
	struct cros_ec_sensor_platform *sensor_platform = dev_get_platdata(dev);
	struct iio_dev *indio_dev;
	struct cros_ec_accel_legacy_state *state;
	int ret, i;

	if (!ec || !ec->ec_dev) {
		dev_warn(&pdev->dev, "No EC device found.\n");
		return -EINVAL;
	}

	if (!ec->ec_dev->cmd_readmem) {
		dev_warn(&pdev->dev, "EC does not support direct reads.\n");
		return -EINVAL;
	}

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*state));
	if (!indio_dev)
		return -ENOMEM;

	platform_set_drvdata(pdev, indio_dev);
	state = iio_priv(indio_dev);
	state->ec = ec->ec_dev;
	state->sensor_num = sensor_platform->sensor_num;

	indio_dev->dev.parent = dev;
	indio_dev->name = pdev->name;
	indio_dev->channels = ec_accel_channels;
	/*
	 * Present the channel using HTML5 standard:
	 * need to invert X and Y and invert some lid axis.
	 */
	for (i = X ; i < MAX_AXIS; i++) {
		switch (i) {
		case X:
			ec_accel_channels[X].scan_index = Y;
		case Y:
			ec_accel_channels[Y].scan_index = X;
		case Z:
			ec_accel_channels[Z].scan_index = Z;
		}
		if (state->sensor_num == MOTIONSENSE_LOC_LID && i != Y)
			state->sign[i] = -1;
		else
			state->sign[i] = 1;
	}
	indio_dev->num_channels = ARRAY_SIZE(ec_accel_channels);
	indio_dev->dev.parent = &pdev->dev;
	indio_dev->info = &cros_ec_accel_legacy_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev, NULL,
					      cros_ec_accel_legacy_capture,
					      NULL);
	if (ret)
		return ret;

	return devm_iio_device_register(dev, indio_dev);
}

static struct platform_driver cros_ec_accel_platform_driver = {
	.driver = {
		.name	= DRV_NAME,
	},
	.probe		= cros_ec_accel_legacy_probe,
};
module_platform_driver(cros_ec_accel_platform_driver);

MODULE_DESCRIPTION("ChromeOS EC legacy accelerometer driver");
MODULE_AUTHOR("Gwendal Grignou <gwendal@chromium.org>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
