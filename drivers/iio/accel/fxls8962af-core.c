// SPDX-License-Identifier: GPL-2.0
/*
 * NXP FXLS8962AF/FXLS8964AF Accelerometer Core Driver
 *
 * Copyright 2021 Connected Cars A/S
 *
 * Datasheet:
 * https://www.nxp.com/docs/en/data-sheet/FXLS8962AF.pdf
 * https://www.nxp.com/docs/en/data-sheet/FXLS8964AF.pdf
 *
 * Errata:
 * https://www.nxp.com/docs/en/errata/ES_FXLS8962AF.pdf
 */

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/regmap.h>

#include <linux/iio/buffer.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/iio/sysfs.h>

#include "fxls8962af.h"

#define FXLS8962AF_INT_STATUS			0x00
#define FXLS8962AF_INT_STATUS_SRC_BOOT		BIT(0)
#define FXLS8962AF_INT_STATUS_SRC_SDCD_OT	BIT(4)
#define FXLS8962AF_INT_STATUS_SRC_BUF		BIT(5)
#define FXLS8962AF_INT_STATUS_SRC_DRDY		BIT(7)
#define FXLS8962AF_TEMP_OUT			0x01
#define FXLS8962AF_VECM_LSB			0x02
#define FXLS8962AF_OUT_X_LSB			0x04
#define FXLS8962AF_OUT_Y_LSB			0x06
#define FXLS8962AF_OUT_Z_LSB			0x08
#define FXLS8962AF_BUF_STATUS			0x0b
#define FXLS8962AF_BUF_STATUS_BUF_CNT		GENMASK(5, 0)
#define FXLS8962AF_BUF_STATUS_BUF_OVF		BIT(6)
#define FXLS8962AF_BUF_STATUS_BUF_WMRK		BIT(7)
#define FXLS8962AF_BUF_X_LSB			0x0c
#define FXLS8962AF_BUF_Y_LSB			0x0e
#define FXLS8962AF_BUF_Z_LSB			0x10

#define FXLS8962AF_PROD_REV			0x12
#define FXLS8962AF_WHO_AM_I			0x13

#define FXLS8962AF_SYS_MODE			0x14
#define FXLS8962AF_SENS_CONFIG1			0x15
#define FXLS8962AF_SENS_CONFIG1_ACTIVE		BIT(0)
#define FXLS8962AF_SENS_CONFIG1_RST		BIT(7)
#define FXLS8962AF_SC1_FSR_MASK			GENMASK(2, 1)
#define FXLS8962AF_SC1_FSR_PREP(x)		FIELD_PREP(FXLS8962AF_SC1_FSR_MASK, (x))
#define FXLS8962AF_SC1_FSR_GET(x)		FIELD_GET(FXLS8962AF_SC1_FSR_MASK, (x))

#define FXLS8962AF_SENS_CONFIG2			0x16
#define FXLS8962AF_SENS_CONFIG3			0x17
#define FXLS8962AF_SC3_WAKE_ODR_MASK		GENMASK(7, 4)
#define FXLS8962AF_SC3_WAKE_ODR_PREP(x)		FIELD_PREP(FXLS8962AF_SC3_WAKE_ODR_MASK, (x))
#define FXLS8962AF_SC3_WAKE_ODR_GET(x)		FIELD_GET(FXLS8962AF_SC3_WAKE_ODR_MASK, (x))
#define FXLS8962AF_SENS_CONFIG4			0x18
#define FXLS8962AF_SC4_INT_PP_OD_MASK		BIT(1)
#define FXLS8962AF_SC4_INT_PP_OD_PREP(x)	FIELD_PREP(FXLS8962AF_SC4_INT_PP_OD_MASK, (x))
#define FXLS8962AF_SC4_INT_POL_MASK		BIT(0)
#define FXLS8962AF_SC4_INT_POL_PREP(x)		FIELD_PREP(FXLS8962AF_SC4_INT_POL_MASK, (x))
#define FXLS8962AF_SENS_CONFIG5			0x19

#define FXLS8962AF_WAKE_IDLE_LSB		0x1b
#define FXLS8962AF_SLEEP_IDLE_LSB		0x1c
#define FXLS8962AF_ASLP_COUNT_LSB		0x1e

#define FXLS8962AF_INT_EN			0x20
#define FXLS8962AF_INT_EN_SDCD_OT_EN		BIT(5)
#define FXLS8962AF_INT_EN_BUF_EN		BIT(6)
#define FXLS8962AF_INT_PIN_SEL			0x21
#define FXLS8962AF_INT_PIN_SEL_MASK		GENMASK(7, 0)
#define FXLS8962AF_INT_PIN_SEL_INT1		0x00
#define FXLS8962AF_INT_PIN_SEL_INT2		GENMASK(7, 0)

#define FXLS8962AF_OFF_X			0x22
#define FXLS8962AF_OFF_Y			0x23
#define FXLS8962AF_OFF_Z			0x24

#define FXLS8962AF_BUF_CONFIG1			0x26
#define FXLS8962AF_BC1_BUF_MODE_MASK		GENMASK(6, 5)
#define FXLS8962AF_BC1_BUF_MODE_PREP(x)		FIELD_PREP(FXLS8962AF_BC1_BUF_MODE_MASK, (x))
#define FXLS8962AF_BUF_CONFIG2			0x27
#define FXLS8962AF_BUF_CONFIG2_BUF_WMRK		GENMASK(5, 0)

#define FXLS8962AF_ORIENT_STATUS		0x28
#define FXLS8962AF_ORIENT_CONFIG		0x29
#define FXLS8962AF_ORIENT_DBCOUNT		0x2a
#define FXLS8962AF_ORIENT_BF_ZCOMP		0x2b
#define FXLS8962AF_ORIENT_THS_REG		0x2c

#define FXLS8962AF_SDCD_INT_SRC1		0x2d
#define FXLS8962AF_SDCD_INT_SRC1_X_OT		BIT(5)
#define FXLS8962AF_SDCD_INT_SRC1_X_POL		BIT(4)
#define FXLS8962AF_SDCD_INT_SRC1_Y_OT		BIT(3)
#define FXLS8962AF_SDCD_INT_SRC1_Y_POL		BIT(2)
#define FXLS8962AF_SDCD_INT_SRC1_Z_OT		BIT(1)
#define FXLS8962AF_SDCD_INT_SRC1_Z_POL		BIT(0)
#define FXLS8962AF_SDCD_INT_SRC2		0x2e
#define FXLS8962AF_SDCD_CONFIG1			0x2f
#define FXLS8962AF_SDCD_CONFIG1_Z_OT_EN		BIT(3)
#define FXLS8962AF_SDCD_CONFIG1_Y_OT_EN		BIT(4)
#define FXLS8962AF_SDCD_CONFIG1_X_OT_EN		BIT(5)
#define FXLS8962AF_SDCD_CONFIG1_OT_ELE		BIT(7)
#define FXLS8962AF_SDCD_CONFIG2			0x30
#define FXLS8962AF_SDCD_CONFIG2_SDCD_EN		BIT(7)
#define FXLS8962AF_SC2_REF_UPDM_AC		GENMASK(6, 5)
#define FXLS8962AF_SDCD_OT_DBCNT		0x31
#define FXLS8962AF_SDCD_WT_DBCNT		0x32
#define FXLS8962AF_SDCD_LTHS_LSB		0x33
#define FXLS8962AF_SDCD_UTHS_LSB		0x35

#define FXLS8962AF_SELF_TEST_CONFIG1		0x37
#define FXLS8962AF_SELF_TEST_CONFIG2		0x38

#define FXLS8962AF_MAX_REG			0x38

#define FXLS8962AF_DEVICE_ID			0x62
#define FXLS8964AF_DEVICE_ID			0x84

/* Raw temp channel offset */
#define FXLS8962AF_TEMP_CENTER_VAL		25

#define FXLS8962AF_AUTO_SUSPEND_DELAY_MS	2000

#define FXLS8962AF_FIFO_LENGTH			32
#define FXLS8962AF_SCALE_TABLE_LEN		4
#define FXLS8962AF_SAMP_FREQ_TABLE_LEN		13

static const int fxls8962af_scale_table[FXLS8962AF_SCALE_TABLE_LEN][2] = {
	{0, IIO_G_TO_M_S_2(980000)},
	{0, IIO_G_TO_M_S_2(1950000)},
	{0, IIO_G_TO_M_S_2(3910000)},
	{0, IIO_G_TO_M_S_2(7810000)},
};

static const int fxls8962af_samp_freq_table[FXLS8962AF_SAMP_FREQ_TABLE_LEN][2] = {
	{3200, 0}, {1600, 0}, {800, 0}, {400, 0}, {200, 0}, {100, 0},
	{50, 0}, {25, 0}, {12, 500000}, {6, 250000}, {3, 125000},
	{1, 563000}, {0, 781000},
};

struct fxls8962af_chip_info {
	const char *name;
	const struct iio_chan_spec *channels;
	int num_channels;
	u8 chip_id;
};

struct fxls8962af_data {
	struct regmap *regmap;
	const struct fxls8962af_chip_info *chip_info;
	struct regulator *vdd_reg;
	struct {
		__le16 channels[3];
		s64 ts __aligned(8);
	} scan;
	int64_t timestamp, old_timestamp;	/* Only used in hw fifo mode. */
	struct iio_mount_matrix orientation;
	int irq;
	u8 watermark;
	u8 enable_event;
	u16 lower_thres;
	u16 upper_thres;
};

const struct regmap_config fxls8962af_i2c_regmap_conf = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = FXLS8962AF_MAX_REG,
};
EXPORT_SYMBOL_NS_GPL(fxls8962af_i2c_regmap_conf, IIO_FXLS8962AF);

const struct regmap_config fxls8962af_spi_regmap_conf = {
	.reg_bits = 8,
	.pad_bits = 8,
	.val_bits = 8,
	.max_register = FXLS8962AF_MAX_REG,
};
EXPORT_SYMBOL_NS_GPL(fxls8962af_spi_regmap_conf, IIO_FXLS8962AF);

enum {
	fxls8962af_idx_x,
	fxls8962af_idx_y,
	fxls8962af_idx_z,
	fxls8962af_idx_ts,
};

enum fxls8962af_int_pin {
	FXLS8962AF_PIN_INT1,
	FXLS8962AF_PIN_INT2,
};

static int fxls8962af_power_on(struct fxls8962af_data *data)
{
	struct device *dev = regmap_get_device(data->regmap);
	int ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		dev_err(dev, "failed to power on\n");

	return ret;
}

static int fxls8962af_power_off(struct fxls8962af_data *data)
{
	struct device *dev = regmap_get_device(data->regmap);
	int ret;

	pm_runtime_mark_last_busy(dev);
	ret = pm_runtime_put_autosuspend(dev);
	if (ret)
		dev_err(dev, "failed to power off\n");

	return ret;
}

static int fxls8962af_standby(struct fxls8962af_data *data)
{
	return regmap_update_bits(data->regmap, FXLS8962AF_SENS_CONFIG1,
				  FXLS8962AF_SENS_CONFIG1_ACTIVE, 0);
}

static int fxls8962af_active(struct fxls8962af_data *data)
{
	return regmap_update_bits(data->regmap, FXLS8962AF_SENS_CONFIG1,
				  FXLS8962AF_SENS_CONFIG1_ACTIVE, 1);
}

static int fxls8962af_is_active(struct fxls8962af_data *data)
{
	unsigned int reg;
	int ret;

	ret = regmap_read(data->regmap, FXLS8962AF_SENS_CONFIG1, &reg);
	if (ret)
		return ret;

	return reg & FXLS8962AF_SENS_CONFIG1_ACTIVE;
}

static int fxls8962af_get_out(struct fxls8962af_data *data,
			      struct iio_chan_spec const *chan, int *val)
{
	struct device *dev = regmap_get_device(data->regmap);
	__le16 raw_val;
	int is_active;
	int ret;

	is_active = fxls8962af_is_active(data);
	if (!is_active) {
		ret = fxls8962af_power_on(data);
		if (ret)
			return ret;
	}

	ret = regmap_bulk_read(data->regmap, chan->address,
			       &raw_val, sizeof(data->lower_thres));

	if (!is_active)
		fxls8962af_power_off(data);

	if (ret) {
		dev_err(dev, "failed to get out reg 0x%lx\n", chan->address);
		return ret;
	}

	*val = sign_extend32(le16_to_cpu(raw_val),
			     chan->scan_type.realbits - 1);

	return IIO_VAL_INT;
}

static int fxls8962af_read_avail(struct iio_dev *indio_dev,
				 struct iio_chan_spec const *chan,
				 const int **vals, int *type, int *length,
				 long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		*type = IIO_VAL_INT_PLUS_NANO;
		*vals = (int *)fxls8962af_scale_table;
		*length = ARRAY_SIZE(fxls8962af_scale_table) * 2;
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*type = IIO_VAL_INT_PLUS_MICRO;
		*vals = (int *)fxls8962af_samp_freq_table;
		*length = ARRAY_SIZE(fxls8962af_samp_freq_table) * 2;
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static int fxls8962af_write_raw_get_fmt(struct iio_dev *indio_dev,
					struct iio_chan_spec const *chan,
					long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		return IIO_VAL_INT_PLUS_NANO;
	case IIO_CHAN_INFO_SAMP_FREQ:
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return IIO_VAL_INT_PLUS_NANO;
	}
}

static int fxls8962af_update_config(struct fxls8962af_data *data, u8 reg,
				    u8 mask, u8 val)
{
	int ret;
	int is_active;

	is_active = fxls8962af_is_active(data);
	if (is_active) {
		ret = fxls8962af_standby(data);
		if (ret)
			return ret;
	}

	ret = regmap_update_bits(data->regmap, reg, mask, val);
	if (ret)
		return ret;

	if (is_active) {
		ret = fxls8962af_active(data);
		if (ret)
			return ret;
	}

	return 0;
}

static int fxls8962af_set_full_scale(struct fxls8962af_data *data, u32 scale)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(fxls8962af_scale_table); i++)
		if (scale == fxls8962af_scale_table[i][1])
			break;

	if (i == ARRAY_SIZE(fxls8962af_scale_table))
		return -EINVAL;

	return fxls8962af_update_config(data, FXLS8962AF_SENS_CONFIG1,
					FXLS8962AF_SC1_FSR_MASK,
					FXLS8962AF_SC1_FSR_PREP(i));
}

static unsigned int fxls8962af_read_full_scale(struct fxls8962af_data *data,
					       int *val)
{
	int ret;
	unsigned int reg;
	u8 range_idx;

	ret = regmap_read(data->regmap, FXLS8962AF_SENS_CONFIG1, &reg);
	if (ret)
		return ret;

	range_idx = FXLS8962AF_SC1_FSR_GET(reg);

	*val = fxls8962af_scale_table[range_idx][1];

	return IIO_VAL_INT_PLUS_NANO;
}

static int fxls8962af_set_samp_freq(struct fxls8962af_data *data, u32 val,
				    u32 val2)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(fxls8962af_samp_freq_table); i++)
		if (val == fxls8962af_samp_freq_table[i][0] &&
		    val2 == fxls8962af_samp_freq_table[i][1])
			break;

	if (i == ARRAY_SIZE(fxls8962af_samp_freq_table))
		return -EINVAL;

	return fxls8962af_update_config(data, FXLS8962AF_SENS_CONFIG3,
					FXLS8962AF_SC3_WAKE_ODR_MASK,
					FXLS8962AF_SC3_WAKE_ODR_PREP(i));
}

static unsigned int fxls8962af_read_samp_freq(struct fxls8962af_data *data,
					      int *val, int *val2)
{
	int ret;
	unsigned int reg;
	u8 range_idx;

	ret = regmap_read(data->regmap, FXLS8962AF_SENS_CONFIG3, &reg);
	if (ret)
		return ret;

	range_idx = FXLS8962AF_SC3_WAKE_ODR_GET(reg);

	*val = fxls8962af_samp_freq_table[range_idx][0];
	*val2 = fxls8962af_samp_freq_table[range_idx][1];

	return IIO_VAL_INT_PLUS_MICRO;
}

static int fxls8962af_read_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int *val, int *val2, long mask)
{
	struct fxls8962af_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_TEMP:
		case IIO_ACCEL:
			return fxls8962af_get_out(data, chan, val);
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_OFFSET:
		if (chan->type != IIO_TEMP)
			return -EINVAL;

		*val = FXLS8962AF_TEMP_CENTER_VAL;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		return fxls8962af_read_full_scale(data, val2);
	case IIO_CHAN_INFO_SAMP_FREQ:
		return fxls8962af_read_samp_freq(data, val, val2);
	default:
		return -EINVAL;
	}
}

static int fxls8962af_write_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int val, int val2, long mask)
{
	struct fxls8962af_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		if (val != 0)
			return -EINVAL;

		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;

		ret = fxls8962af_set_full_scale(data, val2);

		iio_device_release_direct_mode(indio_dev);
		return ret;
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;

		ret = fxls8962af_set_samp_freq(data, val, val2);

		iio_device_release_direct_mode(indio_dev);
		return ret;
	default:
		return -EINVAL;
	}
}

static int fxls8962af_event_setup(struct fxls8962af_data *data, int state)
{
	/* Enable wakeup interrupt */
	int mask = FXLS8962AF_INT_EN_SDCD_OT_EN;
	int value = state ? mask : 0;

	return regmap_update_bits(data->regmap, FXLS8962AF_INT_EN, mask, value);
}

static int fxls8962af_set_watermark(struct iio_dev *indio_dev, unsigned val)
{
	struct fxls8962af_data *data = iio_priv(indio_dev);

	if (val > FXLS8962AF_FIFO_LENGTH)
		val = FXLS8962AF_FIFO_LENGTH;

	data->watermark = val;

	return 0;
}

static int __fxls8962af_set_thresholds(struct fxls8962af_data *data,
				       const struct iio_chan_spec *chan,
				       enum iio_event_direction dir,
				       int val)
{
	switch (dir) {
	case IIO_EV_DIR_FALLING:
		data->lower_thres = val;
		return regmap_bulk_write(data->regmap, FXLS8962AF_SDCD_LTHS_LSB,
				&data->lower_thres, sizeof(data->lower_thres));
	case IIO_EV_DIR_RISING:
		data->upper_thres = val;
		return regmap_bulk_write(data->regmap, FXLS8962AF_SDCD_UTHS_LSB,
				&data->upper_thres, sizeof(data->upper_thres));
	default:
		return -EINVAL;
	}
}

static int fxls8962af_read_event(struct iio_dev *indio_dev,
				 const struct iio_chan_spec *chan,
				 enum iio_event_type type,
				 enum iio_event_direction dir,
				 enum iio_event_info info,
				 int *val, int *val2)
{
	struct fxls8962af_data *data = iio_priv(indio_dev);
	int ret;

	if (type != IIO_EV_TYPE_THRESH)
		return -EINVAL;

	switch (dir) {
	case IIO_EV_DIR_FALLING:
		ret = regmap_bulk_read(data->regmap, FXLS8962AF_SDCD_LTHS_LSB,
				       &data->lower_thres, sizeof(data->lower_thres));
		if (ret)
			return ret;

		*val = sign_extend32(data->lower_thres, chan->scan_type.realbits - 1);
		return IIO_VAL_INT;
	case IIO_EV_DIR_RISING:
		ret = regmap_bulk_read(data->regmap, FXLS8962AF_SDCD_UTHS_LSB,
				       &data->upper_thres, sizeof(data->upper_thres));
		if (ret)
			return ret;

		*val = sign_extend32(data->upper_thres, chan->scan_type.realbits - 1);
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int fxls8962af_write_event(struct iio_dev *indio_dev,
				  const struct iio_chan_spec *chan,
				  enum iio_event_type type,
				  enum iio_event_direction dir,
				  enum iio_event_info info,
				  int val, int val2)
{
	struct fxls8962af_data *data = iio_priv(indio_dev);
	int ret, val_masked;

	if (type != IIO_EV_TYPE_THRESH)
		return -EINVAL;

	if (val < -2048 || val > 2047)
		return -EINVAL;

	if (data->enable_event)
		return -EBUSY;

	val_masked = val & GENMASK(11, 0);
	if (fxls8962af_is_active(data)) {
		ret = fxls8962af_standby(data);
		if (ret)
			return ret;

		ret = __fxls8962af_set_thresholds(data, chan, dir, val_masked);
		if (ret)
			return ret;

		return fxls8962af_active(data);
	} else {
		return __fxls8962af_set_thresholds(data, chan, dir, val_masked);
	}
}

static int
fxls8962af_read_event_config(struct iio_dev *indio_dev,
			     const struct iio_chan_spec *chan,
			     enum iio_event_type type,
			     enum iio_event_direction dir)
{
	struct fxls8962af_data *data = iio_priv(indio_dev);

	if (type != IIO_EV_TYPE_THRESH)
		return -EINVAL;

	switch (chan->channel2) {
	case IIO_MOD_X:
		return !!(FXLS8962AF_SDCD_CONFIG1_X_OT_EN & data->enable_event);
	case IIO_MOD_Y:
		return !!(FXLS8962AF_SDCD_CONFIG1_Y_OT_EN & data->enable_event);
	case IIO_MOD_Z:
		return !!(FXLS8962AF_SDCD_CONFIG1_Z_OT_EN & data->enable_event);
	default:
		return -EINVAL;
	}
}

static int
fxls8962af_write_event_config(struct iio_dev *indio_dev,
			      const struct iio_chan_spec *chan,
			      enum iio_event_type type,
			      enum iio_event_direction dir, int state)
{
	struct fxls8962af_data *data = iio_priv(indio_dev);
	u8 enable_event, enable_bits;
	int ret, value;

	if (type != IIO_EV_TYPE_THRESH)
		return -EINVAL;

	switch (chan->channel2) {
	case IIO_MOD_X:
		enable_bits = FXLS8962AF_SDCD_CONFIG1_X_OT_EN;
		break;
	case IIO_MOD_Y:
		enable_bits = FXLS8962AF_SDCD_CONFIG1_Y_OT_EN;
		break;
	case IIO_MOD_Z:
		enable_bits = FXLS8962AF_SDCD_CONFIG1_Z_OT_EN;
		break;
	default:
		return -EINVAL;
	}

	if (state)
		enable_event = data->enable_event | enable_bits;
	else
		enable_event = data->enable_event & ~enable_bits;

	if (data->enable_event == enable_event)
		return 0;

	ret = fxls8962af_standby(data);
	if (ret)
		return ret;

	/* Enable events */
	value = enable_event | FXLS8962AF_SDCD_CONFIG1_OT_ELE;
	ret = regmap_write(data->regmap, FXLS8962AF_SDCD_CONFIG1, value);
	if (ret)
		return ret;

	/*
	 * Enable update of SDCD_REF_X/Y/Z values with the current decimated and
	 * trimmed X/Y/Z acceleration input data. This allows for acceleration
	 * slope detection with Data(n) to Data(n–1) always used as the input
	 * to the window comparator.
	 */
	value = enable_event ?
		FXLS8962AF_SDCD_CONFIG2_SDCD_EN | FXLS8962AF_SC2_REF_UPDM_AC :
		0x00;
	ret = regmap_write(data->regmap, FXLS8962AF_SDCD_CONFIG2, value);
	if (ret)
		return ret;

	ret = fxls8962af_event_setup(data, state);
	if (ret)
		return ret;

	data->enable_event = enable_event;

	if (data->enable_event) {
		fxls8962af_active(data);
		ret = fxls8962af_power_on(data);
	} else {
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;

		/* Not in buffered mode so disable power */
		ret = fxls8962af_power_off(data);

		iio_device_release_direct_mode(indio_dev);
	}

	return ret;
}

static const struct iio_event_spec fxls8962af_event[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_EITHER,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	},
};

#define FXLS8962AF_CHANNEL(axis, reg, idx) { \
	.type = IIO_ACCEL, \
	.address = reg, \
	.modified = 1, \
	.channel2 = IIO_MOD_##axis, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) | \
				    BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	.info_mask_shared_by_type_available = BIT(IIO_CHAN_INFO_SCALE) | \
					      BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	.scan_index = idx, \
	.scan_type = { \
		.sign = 's', \
		.realbits = 12, \
		.storagebits = 16, \
		.endianness = IIO_LE, \
	}, \
	.event_spec = fxls8962af_event, \
	.num_event_specs = ARRAY_SIZE(fxls8962af_event), \
}

#define FXLS8962AF_TEMP_CHANNEL { \
	.type = IIO_TEMP, \
	.address = FXLS8962AF_TEMP_OUT, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
			      BIT(IIO_CHAN_INFO_OFFSET),\
	.scan_index = -1, \
	.scan_type = { \
		.realbits = 8, \
		.storagebits = 8, \
	}, \
}

static const struct iio_chan_spec fxls8962af_channels[] = {
	FXLS8962AF_CHANNEL(X, FXLS8962AF_OUT_X_LSB, fxls8962af_idx_x),
	FXLS8962AF_CHANNEL(Y, FXLS8962AF_OUT_Y_LSB, fxls8962af_idx_y),
	FXLS8962AF_CHANNEL(Z, FXLS8962AF_OUT_Z_LSB, fxls8962af_idx_z),
	IIO_CHAN_SOFT_TIMESTAMP(fxls8962af_idx_ts),
	FXLS8962AF_TEMP_CHANNEL,
};

static const struct fxls8962af_chip_info fxls_chip_info_table[] = {
	[fxls8962af] = {
		.chip_id = FXLS8962AF_DEVICE_ID,
		.name = "fxls8962af",
		.channels = fxls8962af_channels,
		.num_channels = ARRAY_SIZE(fxls8962af_channels),
	},
	[fxls8964af] = {
		.chip_id = FXLS8964AF_DEVICE_ID,
		.name = "fxls8964af",
		.channels = fxls8962af_channels,
		.num_channels = ARRAY_SIZE(fxls8962af_channels),
	},
};

static const struct iio_info fxls8962af_info = {
	.read_raw = &fxls8962af_read_raw,
	.write_raw = &fxls8962af_write_raw,
	.write_raw_get_fmt = fxls8962af_write_raw_get_fmt,
	.read_event_value = fxls8962af_read_event,
	.write_event_value = fxls8962af_write_event,
	.read_event_config = fxls8962af_read_event_config,
	.write_event_config = fxls8962af_write_event_config,
	.read_avail = fxls8962af_read_avail,
	.hwfifo_set_watermark = fxls8962af_set_watermark,
};

static int fxls8962af_reset(struct fxls8962af_data *data)
{
	struct device *dev = regmap_get_device(data->regmap);
	unsigned int reg;
	int ret;

	ret = regmap_update_bits(data->regmap, FXLS8962AF_SENS_CONFIG1,
				 FXLS8962AF_SENS_CONFIG1_RST,
				 FXLS8962AF_SENS_CONFIG1_RST);
	if (ret)
		return ret;

	/* TBOOT1, TBOOT2, specifies we have to wait between 1 - 17.7ms */
	ret = regmap_read_poll_timeout(data->regmap, FXLS8962AF_INT_STATUS, reg,
				       (reg & FXLS8962AF_INT_STATUS_SRC_BOOT),
				       1000, 18000);
	if (ret == -ETIMEDOUT)
		dev_err(dev, "reset timeout, int_status = 0x%x\n", reg);

	return ret;
}

static int __fxls8962af_fifo_set_mode(struct fxls8962af_data *data, bool onoff)
{
	int ret;

	/* Enable watermark at max fifo size */
	ret = regmap_update_bits(data->regmap, FXLS8962AF_BUF_CONFIG2,
				 FXLS8962AF_BUF_CONFIG2_BUF_WMRK,
				 data->watermark);
	if (ret)
		return ret;

	return regmap_update_bits(data->regmap, FXLS8962AF_BUF_CONFIG1,
				  FXLS8962AF_BC1_BUF_MODE_MASK,
				  FXLS8962AF_BC1_BUF_MODE_PREP(onoff));
}

static int fxls8962af_buffer_preenable(struct iio_dev *indio_dev)
{
	return fxls8962af_power_on(iio_priv(indio_dev));
}

static int fxls8962af_buffer_postenable(struct iio_dev *indio_dev)
{
	struct fxls8962af_data *data = iio_priv(indio_dev);
	int ret;

	fxls8962af_standby(data);

	/* Enable buffer interrupt */
	ret = regmap_update_bits(data->regmap, FXLS8962AF_INT_EN,
				 FXLS8962AF_INT_EN_BUF_EN,
				 FXLS8962AF_INT_EN_BUF_EN);
	if (ret)
		return ret;

	ret = __fxls8962af_fifo_set_mode(data, true);

	fxls8962af_active(data);

	return ret;
}

static int fxls8962af_buffer_predisable(struct iio_dev *indio_dev)
{
	struct fxls8962af_data *data = iio_priv(indio_dev);
	int ret;

	fxls8962af_standby(data);

	/* Disable buffer interrupt */
	ret = regmap_update_bits(data->regmap, FXLS8962AF_INT_EN,
				 FXLS8962AF_INT_EN_BUF_EN, 0);
	if (ret)
		return ret;

	ret = __fxls8962af_fifo_set_mode(data, false);

	if (data->enable_event)
		fxls8962af_active(data);

	return ret;
}

static int fxls8962af_buffer_postdisable(struct iio_dev *indio_dev)
{
	struct fxls8962af_data *data = iio_priv(indio_dev);

	if (!data->enable_event)
		fxls8962af_power_off(data);

	return 0;
}

static const struct iio_buffer_setup_ops fxls8962af_buffer_ops = {
	.preenable = fxls8962af_buffer_preenable,
	.postenable = fxls8962af_buffer_postenable,
	.predisable = fxls8962af_buffer_predisable,
	.postdisable = fxls8962af_buffer_postdisable,
};

static int fxls8962af_i2c_raw_read_errata3(struct fxls8962af_data *data,
					   u16 *buffer, int samples,
					   int sample_length)
{
	int i, ret;

	for (i = 0; i < samples; i++) {
		ret = regmap_raw_read(data->regmap, FXLS8962AF_BUF_X_LSB,
				      &buffer[i * 3], sample_length);
		if (ret)
			return ret;
	}

	return 0;
}

static int fxls8962af_fifo_transfer(struct fxls8962af_data *data,
				    u16 *buffer, int samples)
{
	struct device *dev = regmap_get_device(data->regmap);
	int sample_length = 3 * sizeof(*buffer);
	int total_length = samples * sample_length;
	int ret;

	if (i2c_verify_client(dev) &&
	    data->chip_info->chip_id == FXLS8962AF_DEVICE_ID)
		/*
		 * Due to errata bug (only applicable on fxls8962af):
		 * E3: FIFO burst read operation error using I2C interface
		 * We have to avoid burst reads on I2C..
		 */
		ret = fxls8962af_i2c_raw_read_errata3(data, buffer, samples,
						      sample_length);
	else
		ret = regmap_raw_read(data->regmap, FXLS8962AF_BUF_X_LSB, buffer,
				      total_length);

	if (ret)
		dev_err(dev, "Error transferring data from fifo: %d\n", ret);

	return ret;
}

static int fxls8962af_fifo_flush(struct iio_dev *indio_dev)
{
	struct fxls8962af_data *data = iio_priv(indio_dev);
	struct device *dev = regmap_get_device(data->regmap);
	u16 buffer[FXLS8962AF_FIFO_LENGTH * 3];
	uint64_t sample_period;
	unsigned int reg;
	int64_t tstamp;
	int ret, i;
	u8 count;

	ret = regmap_read(data->regmap, FXLS8962AF_BUF_STATUS, &reg);
	if (ret)
		return ret;

	if (reg & FXLS8962AF_BUF_STATUS_BUF_OVF) {
		dev_err(dev, "Buffer overflow");
		return -EOVERFLOW;
	}

	count = reg & FXLS8962AF_BUF_STATUS_BUF_CNT;
	if (!count)
		return 0;

	data->old_timestamp = data->timestamp;
	data->timestamp = iio_get_time_ns(indio_dev);

	/*
	 * Approximate timestamps for each of the sample based on the sampling,
	 * frequency, timestamp for last sample and number of samples.
	 */
	sample_period = (data->timestamp - data->old_timestamp);
	do_div(sample_period, count);
	tstamp = data->timestamp - (count - 1) * sample_period;

	ret = fxls8962af_fifo_transfer(data, buffer, count);
	if (ret)
		return ret;

	/* Demux hw FIFO into kfifo. */
	for (i = 0; i < count; i++) {
		int j, bit;

		j = 0;
		for_each_set_bit(bit, indio_dev->active_scan_mask,
				 indio_dev->masklength) {
			memcpy(&data->scan.channels[j++], &buffer[i * 3 + bit],
			       sizeof(data->scan.channels[0]));
		}

		iio_push_to_buffers_with_timestamp(indio_dev, &data->scan,
						   tstamp);

		tstamp += sample_period;
	}

	return count;
}

static int fxls8962af_event_interrupt(struct iio_dev *indio_dev)
{
	struct fxls8962af_data *data = iio_priv(indio_dev);
	s64 ts = iio_get_time_ns(indio_dev);
	unsigned int reg;
	u64 ev_code;
	int ret;

	ret = regmap_read(data->regmap, FXLS8962AF_SDCD_INT_SRC1, &reg);
	if (ret)
		return ret;

	if (reg & FXLS8962AF_SDCD_INT_SRC1_X_OT) {
		ev_code = reg & FXLS8962AF_SDCD_INT_SRC1_X_POL ?
			IIO_EV_DIR_RISING : IIO_EV_DIR_FALLING;
		iio_push_event(indio_dev,
				IIO_MOD_EVENT_CODE(IIO_ACCEL, 0, IIO_MOD_X,
					IIO_EV_TYPE_THRESH, ev_code), ts);
	}

	if (reg & FXLS8962AF_SDCD_INT_SRC1_Y_OT) {
		ev_code = reg & FXLS8962AF_SDCD_INT_SRC1_Y_POL ?
			IIO_EV_DIR_RISING : IIO_EV_DIR_FALLING;
		iio_push_event(indio_dev,
				IIO_MOD_EVENT_CODE(IIO_ACCEL, 0, IIO_MOD_X,
					IIO_EV_TYPE_THRESH, ev_code), ts);
	}

	if (reg & FXLS8962AF_SDCD_INT_SRC1_Z_OT) {
		ev_code = reg & FXLS8962AF_SDCD_INT_SRC1_Z_POL ?
			IIO_EV_DIR_RISING : IIO_EV_DIR_FALLING;
		iio_push_event(indio_dev,
				IIO_MOD_EVENT_CODE(IIO_ACCEL, 0, IIO_MOD_X,
					IIO_EV_TYPE_THRESH, ev_code), ts);
	}

	return 0;
}

static irqreturn_t fxls8962af_interrupt(int irq, void *p)
{
	struct iio_dev *indio_dev = p;
	struct fxls8962af_data *data = iio_priv(indio_dev);
	unsigned int reg;
	int ret;

	ret = regmap_read(data->regmap, FXLS8962AF_INT_STATUS, &reg);
	if (ret)
		return IRQ_NONE;

	if (reg & FXLS8962AF_INT_STATUS_SRC_BUF) {
		ret = fxls8962af_fifo_flush(indio_dev);
		if (ret < 0)
			return IRQ_NONE;

		return IRQ_HANDLED;
	}

	if (reg & FXLS8962AF_INT_STATUS_SRC_SDCD_OT) {
		ret = fxls8962af_event_interrupt(indio_dev);
		if (ret < 0)
			return IRQ_NONE;

		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static void fxls8962af_regulator_disable(void *data_ptr)
{
	struct fxls8962af_data *data = data_ptr;

	regulator_disable(data->vdd_reg);
}

static void fxls8962af_pm_disable(void *dev_ptr)
{
	struct device *dev = dev_ptr;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);

	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
	pm_runtime_put_noidle(dev);

	fxls8962af_standby(iio_priv(indio_dev));
}

static void fxls8962af_get_irq(struct device_node *of_node,
			       enum fxls8962af_int_pin *pin)
{
	int irq;

	irq = of_irq_get_byname(of_node, "INT2");
	if (irq > 0) {
		*pin = FXLS8962AF_PIN_INT2;
		return;
	}

	*pin = FXLS8962AF_PIN_INT1;
}

static int fxls8962af_irq_setup(struct iio_dev *indio_dev, int irq)
{
	struct fxls8962af_data *data = iio_priv(indio_dev);
	struct device *dev = regmap_get_device(data->regmap);
	unsigned long irq_type;
	bool irq_active_high;
	enum fxls8962af_int_pin int_pin;
	u8 int_pin_sel;
	int ret;

	fxls8962af_get_irq(dev->of_node, &int_pin);
	switch (int_pin) {
	case FXLS8962AF_PIN_INT1:
		int_pin_sel = FXLS8962AF_INT_PIN_SEL_INT1;
		break;
	case FXLS8962AF_PIN_INT2:
		int_pin_sel = FXLS8962AF_INT_PIN_SEL_INT2;
		break;
	default:
		dev_err(dev, "unsupported int pin selected\n");
		return -EINVAL;
	}

	ret = regmap_update_bits(data->regmap, FXLS8962AF_INT_PIN_SEL,
				 FXLS8962AF_INT_PIN_SEL_MASK, int_pin_sel);
	if (ret)
		return ret;

	irq_type = irqd_get_trigger_type(irq_get_irq_data(irq));

	switch (irq_type) {
	case IRQF_TRIGGER_HIGH:
	case IRQF_TRIGGER_RISING:
		irq_active_high = true;
		break;
	case IRQF_TRIGGER_LOW:
	case IRQF_TRIGGER_FALLING:
		irq_active_high = false;
		break;
	default:
		dev_info(dev, "mode %lx unsupported\n", irq_type);
		return -EINVAL;
	}

	ret = regmap_update_bits(data->regmap, FXLS8962AF_SENS_CONFIG4,
				 FXLS8962AF_SC4_INT_POL_MASK,
				 FXLS8962AF_SC4_INT_POL_PREP(irq_active_high));
	if (ret)
		return ret;

	if (device_property_read_bool(dev, "drive-open-drain")) {
		ret = regmap_update_bits(data->regmap, FXLS8962AF_SENS_CONFIG4,
					 FXLS8962AF_SC4_INT_PP_OD_MASK,
					 FXLS8962AF_SC4_INT_PP_OD_PREP(1));
		if (ret)
			return ret;

		irq_type |= IRQF_SHARED;
	}

	return devm_request_threaded_irq(dev,
					 irq,
					 NULL, fxls8962af_interrupt,
					 irq_type | IRQF_ONESHOT,
					 indio_dev->name, indio_dev);
}

int fxls8962af_core_probe(struct device *dev, struct regmap *regmap, int irq)
{
	struct fxls8962af_data *data;
	struct iio_dev *indio_dev;
	unsigned int reg;
	int ret, i;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	dev_set_drvdata(dev, indio_dev);
	data->regmap = regmap;
	data->irq = irq;

	ret = iio_read_mount_matrix(dev, &data->orientation);
	if (ret)
		return ret;

	data->vdd_reg = devm_regulator_get(dev, "vdd");
	if (IS_ERR(data->vdd_reg))
		return dev_err_probe(dev, PTR_ERR(data->vdd_reg),
				     "Failed to get vdd regulator\n");

	ret = regulator_enable(data->vdd_reg);
	if (ret) {
		dev_err(dev, "Failed to enable vdd regulator: %d\n", ret);
		return ret;
	}

	ret = devm_add_action_or_reset(dev, fxls8962af_regulator_disable, data);
	if (ret)
		return ret;

	ret = regmap_read(data->regmap, FXLS8962AF_WHO_AM_I, &reg);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(fxls_chip_info_table); i++) {
		if (fxls_chip_info_table[i].chip_id == reg) {
			data->chip_info = &fxls_chip_info_table[i];
			break;
		}
	}
	if (i == ARRAY_SIZE(fxls_chip_info_table)) {
		dev_err(dev, "failed to match device in table\n");
		return -ENXIO;
	}

	indio_dev->channels = data->chip_info->channels;
	indio_dev->num_channels = data->chip_info->num_channels;
	indio_dev->name = data->chip_info->name;
	indio_dev->info = &fxls8962af_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = fxls8962af_reset(data);
	if (ret)
		return ret;

	if (irq) {
		ret = fxls8962af_irq_setup(indio_dev, irq);
		if (ret)
			return ret;

		ret = devm_iio_kfifo_buffer_setup(dev, indio_dev,
						  &fxls8962af_buffer_ops);
		if (ret)
			return ret;
	}

	ret = pm_runtime_set_active(dev);
	if (ret)
		return ret;

	pm_runtime_enable(dev);
	pm_runtime_set_autosuspend_delay(dev, FXLS8962AF_AUTO_SUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(dev);

	ret = devm_add_action_or_reset(dev, fxls8962af_pm_disable, dev);
	if (ret)
		return ret;

	if (device_property_read_bool(dev, "wakeup-source"))
		device_init_wakeup(dev, true);

	return devm_iio_device_register(dev, indio_dev);
}
EXPORT_SYMBOL_NS_GPL(fxls8962af_core_probe, IIO_FXLS8962AF);

static int __maybe_unused fxls8962af_runtime_suspend(struct device *dev)
{
	struct fxls8962af_data *data = iio_priv(dev_get_drvdata(dev));
	int ret;

	ret = fxls8962af_standby(data);
	if (ret) {
		dev_err(dev, "powering off device failed\n");
		return ret;
	}

	return 0;
}

static int __maybe_unused fxls8962af_runtime_resume(struct device *dev)
{
	struct fxls8962af_data *data = iio_priv(dev_get_drvdata(dev));

	return fxls8962af_active(data);
}

static int __maybe_unused fxls8962af_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct fxls8962af_data *data = iio_priv(indio_dev);

	if (device_may_wakeup(dev) && data->enable_event) {
		enable_irq_wake(data->irq);

		/*
		 * Disable buffer, as the buffer is so small the device will wake
		 * almost immediately.
		 */
		if (iio_buffer_enabled(indio_dev))
			fxls8962af_buffer_predisable(indio_dev);
	} else {
		fxls8962af_runtime_suspend(dev);
	}

	return 0;
}

static int __maybe_unused fxls8962af_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct fxls8962af_data *data = iio_priv(indio_dev);

	if (device_may_wakeup(dev) && data->enable_event) {
		disable_irq_wake(data->irq);

		if (iio_buffer_enabled(indio_dev))
			fxls8962af_buffer_postenable(indio_dev);
	} else {
		fxls8962af_runtime_resume(dev);
	}

	return 0;
}

const struct dev_pm_ops fxls8962af_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(fxls8962af_suspend, fxls8962af_resume)
	SET_RUNTIME_PM_OPS(fxls8962af_runtime_suspend,
			   fxls8962af_runtime_resume, NULL)
};
EXPORT_SYMBOL_NS_GPL(fxls8962af_pm_ops, IIO_FXLS8962AF);

MODULE_AUTHOR("Sean Nyekjaer <sean@geanix.com>");
MODULE_DESCRIPTION("NXP FXLS8962AF/FXLS8964AF accelerometer driver");
MODULE_LICENSE("GPL v2");
