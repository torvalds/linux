// SPDX-License-Identifier: GPL-2.0-only
/*
 * 3-axis accelerometer driver supporting following Bosch-Sensortec chips:
 *  - BMC150
 *  - BMI055
 *  - BMA255
 *  - BMA250E
 *  - BMA222
 *  - BMA222E
 *  - BMA280
 *
 * Copyright (c) 2014, Intel Corporation.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/events.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include "bmc150-accel.h"

#define BMC150_ACCEL_DRV_NAME			"bmc150_accel"
#define BMC150_ACCEL_IRQ_NAME			"bmc150_accel_event"

#define BMC150_ACCEL_REG_CHIP_ID		0x00

#define BMC150_ACCEL_REG_INT_STATUS_2		0x0B
#define BMC150_ACCEL_ANY_MOTION_MASK		0x07
#define BMC150_ACCEL_ANY_MOTION_BIT_X		BIT(0)
#define BMC150_ACCEL_ANY_MOTION_BIT_Y		BIT(1)
#define BMC150_ACCEL_ANY_MOTION_BIT_Z		BIT(2)
#define BMC150_ACCEL_ANY_MOTION_BIT_SIGN	BIT(3)

#define BMC150_ACCEL_REG_PMU_LPW		0x11
#define BMC150_ACCEL_PMU_MODE_MASK		0xE0
#define BMC150_ACCEL_PMU_MODE_SHIFT		5
#define BMC150_ACCEL_PMU_BIT_SLEEP_DUR_MASK	0x17
#define BMC150_ACCEL_PMU_BIT_SLEEP_DUR_SHIFT	1

#define BMC150_ACCEL_REG_PMU_RANGE		0x0F

#define BMC150_ACCEL_DEF_RANGE_2G		0x03
#define BMC150_ACCEL_DEF_RANGE_4G		0x05
#define BMC150_ACCEL_DEF_RANGE_8G		0x08
#define BMC150_ACCEL_DEF_RANGE_16G		0x0C

/* Default BW: 125Hz */
#define BMC150_ACCEL_REG_PMU_BW		0x10
#define BMC150_ACCEL_DEF_BW			125

#define BMC150_ACCEL_REG_RESET			0x14
#define BMC150_ACCEL_RESET_VAL			0xB6

#define BMC150_ACCEL_REG_INT_MAP_0		0x19
#define BMC150_ACCEL_INT_MAP_0_BIT_SLOPE	BIT(2)

#define BMC150_ACCEL_REG_INT_MAP_1		0x1A
#define BMC150_ACCEL_INT_MAP_1_BIT_DATA		BIT(0)
#define BMC150_ACCEL_INT_MAP_1_BIT_FWM		BIT(1)
#define BMC150_ACCEL_INT_MAP_1_BIT_FFULL	BIT(2)

#define BMC150_ACCEL_REG_INT_RST_LATCH		0x21
#define BMC150_ACCEL_INT_MODE_LATCH_RESET	0x80
#define BMC150_ACCEL_INT_MODE_LATCH_INT	0x0F
#define BMC150_ACCEL_INT_MODE_NON_LATCH_INT	0x00

#define BMC150_ACCEL_REG_INT_EN_0		0x16
#define BMC150_ACCEL_INT_EN_BIT_SLP_X		BIT(0)
#define BMC150_ACCEL_INT_EN_BIT_SLP_Y		BIT(1)
#define BMC150_ACCEL_INT_EN_BIT_SLP_Z		BIT(2)

#define BMC150_ACCEL_REG_INT_EN_1		0x17
#define BMC150_ACCEL_INT_EN_BIT_DATA_EN		BIT(4)
#define BMC150_ACCEL_INT_EN_BIT_FFULL_EN	BIT(5)
#define BMC150_ACCEL_INT_EN_BIT_FWM_EN		BIT(6)

#define BMC150_ACCEL_REG_INT_OUT_CTRL		0x20
#define BMC150_ACCEL_INT_OUT_CTRL_INT1_LVL	BIT(0)

#define BMC150_ACCEL_REG_INT_5			0x27
#define BMC150_ACCEL_SLOPE_DUR_MASK		0x03

#define BMC150_ACCEL_REG_INT_6			0x28
#define BMC150_ACCEL_SLOPE_THRES_MASK		0xFF

/* Slope duration in terms of number of samples */
#define BMC150_ACCEL_DEF_SLOPE_DURATION		1
/* in terms of multiples of g's/LSB, based on range */
#define BMC150_ACCEL_DEF_SLOPE_THRESHOLD	1

#define BMC150_ACCEL_REG_XOUT_L		0x02

#define BMC150_ACCEL_MAX_STARTUP_TIME_MS	100

/* Sleep Duration values */
#define BMC150_ACCEL_SLEEP_500_MICRO		0x05
#define BMC150_ACCEL_SLEEP_1_MS		0x06
#define BMC150_ACCEL_SLEEP_2_MS		0x07
#define BMC150_ACCEL_SLEEP_4_MS		0x08
#define BMC150_ACCEL_SLEEP_6_MS		0x09
#define BMC150_ACCEL_SLEEP_10_MS		0x0A
#define BMC150_ACCEL_SLEEP_25_MS		0x0B
#define BMC150_ACCEL_SLEEP_50_MS		0x0C
#define BMC150_ACCEL_SLEEP_100_MS		0x0D
#define BMC150_ACCEL_SLEEP_500_MS		0x0E
#define BMC150_ACCEL_SLEEP_1_SEC		0x0F

#define BMC150_ACCEL_REG_TEMP			0x08
#define BMC150_ACCEL_TEMP_CENTER_VAL		23

#define BMC150_ACCEL_AXIS_TO_REG(axis)	(BMC150_ACCEL_REG_XOUT_L + (axis * 2))
#define BMC150_AUTO_SUSPEND_DELAY_MS		2000

#define BMC150_ACCEL_REG_FIFO_STATUS		0x0E
#define BMC150_ACCEL_REG_FIFO_CONFIG0		0x30
#define BMC150_ACCEL_REG_FIFO_CONFIG1		0x3E
#define BMC150_ACCEL_REG_FIFO_DATA		0x3F
#define BMC150_ACCEL_FIFO_LENGTH		32

enum bmc150_accel_axis {
	AXIS_X,
	AXIS_Y,
	AXIS_Z,
	AXIS_MAX,
};

enum bmc150_power_modes {
	BMC150_ACCEL_SLEEP_MODE_NORMAL,
	BMC150_ACCEL_SLEEP_MODE_DEEP_SUSPEND,
	BMC150_ACCEL_SLEEP_MODE_LPM,
	BMC150_ACCEL_SLEEP_MODE_SUSPEND = 0x04,
};

struct bmc150_scale_info {
	int scale;
	u8 reg_range;
};

struct bmc150_accel_chip_info {
	const char *name;
	u8 chip_id;
	const struct iio_chan_spec *channels;
	int num_channels;
	const struct bmc150_scale_info scale_table[4];
};

struct bmc150_accel_interrupt {
	const struct bmc150_accel_interrupt_info *info;
	atomic_t users;
};

struct bmc150_accel_trigger {
	struct bmc150_accel_data *data;
	struct iio_trigger *indio_trig;
	int (*setup)(struct bmc150_accel_trigger *t, bool state);
	int intr;
	bool enabled;
};

enum bmc150_accel_interrupt_id {
	BMC150_ACCEL_INT_DATA_READY,
	BMC150_ACCEL_INT_ANY_MOTION,
	BMC150_ACCEL_INT_WATERMARK,
	BMC150_ACCEL_INTERRUPTS,
};

enum bmc150_accel_trigger_id {
	BMC150_ACCEL_TRIGGER_DATA_READY,
	BMC150_ACCEL_TRIGGER_ANY_MOTION,
	BMC150_ACCEL_TRIGGERS,
};

struct bmc150_accel_data {
	struct regmap *regmap;
	struct regulator_bulk_data regulators[2];
	struct bmc150_accel_interrupt interrupts[BMC150_ACCEL_INTERRUPTS];
	struct bmc150_accel_trigger triggers[BMC150_ACCEL_TRIGGERS];
	struct mutex mutex;
	u8 fifo_mode, watermark;
	s16 buffer[8];
	/*
	 * Ensure there is sufficient space and correct alignment for
	 * the timestamp if enabled
	 */
	struct {
		__le16 channels[3];
		s64 ts __aligned(8);
	} scan;
	u8 bw_bits;
	u32 slope_dur;
	u32 slope_thres;
	u32 range;
	int ev_enable_state;
	int64_t timestamp, old_timestamp; /* Only used in hw fifo mode. */
	const struct bmc150_accel_chip_info *chip_info;
	struct i2c_client *second_device;
	struct iio_mount_matrix orientation;
};

static const struct {
	int val;
	int val2;
	u8 bw_bits;
} bmc150_accel_samp_freq_table[] = { {15, 620000, 0x08},
				     {31, 260000, 0x09},
				     {62, 500000, 0x0A},
				     {125, 0, 0x0B},
				     {250, 0, 0x0C},
				     {500, 0, 0x0D},
				     {1000, 0, 0x0E},
				     {2000, 0, 0x0F} };

static const struct {
	int bw_bits;
	int msec;
} bmc150_accel_sample_upd_time[] = { {0x08, 64},
				     {0x09, 32},
				     {0x0A, 16},
				     {0x0B, 8},
				     {0x0C, 4},
				     {0x0D, 2},
				     {0x0E, 1},
				     {0x0F, 1} };

static const struct {
	int sleep_dur;
	u8 reg_value;
} bmc150_accel_sleep_value_table[] = { {0, 0},
				       {500, BMC150_ACCEL_SLEEP_500_MICRO},
				       {1000, BMC150_ACCEL_SLEEP_1_MS},
				       {2000, BMC150_ACCEL_SLEEP_2_MS},
				       {4000, BMC150_ACCEL_SLEEP_4_MS},
				       {6000, BMC150_ACCEL_SLEEP_6_MS},
				       {10000, BMC150_ACCEL_SLEEP_10_MS},
				       {25000, BMC150_ACCEL_SLEEP_25_MS},
				       {50000, BMC150_ACCEL_SLEEP_50_MS},
				       {100000, BMC150_ACCEL_SLEEP_100_MS},
				       {500000, BMC150_ACCEL_SLEEP_500_MS},
				       {1000000, BMC150_ACCEL_SLEEP_1_SEC} };

const struct regmap_config bmc150_regmap_conf = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x3f,
};
EXPORT_SYMBOL_GPL(bmc150_regmap_conf);

static int bmc150_accel_set_mode(struct bmc150_accel_data *data,
				 enum bmc150_power_modes mode,
				 int dur_us)
{
	struct device *dev = regmap_get_device(data->regmap);
	int i;
	int ret;
	u8 lpw_bits;
	int dur_val = -1;

	if (dur_us > 0) {
		for (i = 0; i < ARRAY_SIZE(bmc150_accel_sleep_value_table);
									 ++i) {
			if (bmc150_accel_sleep_value_table[i].sleep_dur ==
									dur_us)
				dur_val =
				bmc150_accel_sleep_value_table[i].reg_value;
		}
	} else {
		dur_val = 0;
	}

	if (dur_val < 0)
		return -EINVAL;

	lpw_bits = mode << BMC150_ACCEL_PMU_MODE_SHIFT;
	lpw_bits |= (dur_val << BMC150_ACCEL_PMU_BIT_SLEEP_DUR_SHIFT);

	dev_dbg(dev, "Set Mode bits %x\n", lpw_bits);

	ret = regmap_write(data->regmap, BMC150_ACCEL_REG_PMU_LPW, lpw_bits);
	if (ret < 0) {
		dev_err(dev, "Error writing reg_pmu_lpw\n");
		return ret;
	}

	return 0;
}

static int bmc150_accel_set_bw(struct bmc150_accel_data *data, int val,
			       int val2)
{
	int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(bmc150_accel_samp_freq_table); ++i) {
		if (bmc150_accel_samp_freq_table[i].val == val &&
		    bmc150_accel_samp_freq_table[i].val2 == val2) {
			ret = regmap_write(data->regmap,
				BMC150_ACCEL_REG_PMU_BW,
				bmc150_accel_samp_freq_table[i].bw_bits);
			if (ret < 0)
				return ret;

			data->bw_bits =
				bmc150_accel_samp_freq_table[i].bw_bits;
			return 0;
		}
	}

	return -EINVAL;
}

static int bmc150_accel_update_slope(struct bmc150_accel_data *data)
{
	struct device *dev = regmap_get_device(data->regmap);
	int ret;

	ret = regmap_write(data->regmap, BMC150_ACCEL_REG_INT_6,
					data->slope_thres);
	if (ret < 0) {
		dev_err(dev, "Error writing reg_int_6\n");
		return ret;
	}

	ret = regmap_update_bits(data->regmap, BMC150_ACCEL_REG_INT_5,
				 BMC150_ACCEL_SLOPE_DUR_MASK, data->slope_dur);
	if (ret < 0) {
		dev_err(dev, "Error updating reg_int_5\n");
		return ret;
	}

	dev_dbg(dev, "%x %x\n", data->slope_thres, data->slope_dur);

	return ret;
}

static int bmc150_accel_any_motion_setup(struct bmc150_accel_trigger *t,
					 bool state)
{
	if (state)
		return bmc150_accel_update_slope(t->data);

	return 0;
}

static int bmc150_accel_get_bw(struct bmc150_accel_data *data, int *val,
			       int *val2)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(bmc150_accel_samp_freq_table); ++i) {
		if (bmc150_accel_samp_freq_table[i].bw_bits == data->bw_bits) {
			*val = bmc150_accel_samp_freq_table[i].val;
			*val2 = bmc150_accel_samp_freq_table[i].val2;
			return IIO_VAL_INT_PLUS_MICRO;
		}
	}

	return -EINVAL;
}

#ifdef CONFIG_PM
static int bmc150_accel_get_startup_times(struct bmc150_accel_data *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(bmc150_accel_sample_upd_time); ++i) {
		if (bmc150_accel_sample_upd_time[i].bw_bits == data->bw_bits)
			return bmc150_accel_sample_upd_time[i].msec;
	}

	return BMC150_ACCEL_MAX_STARTUP_TIME_MS;
}

static int bmc150_accel_set_power_state(struct bmc150_accel_data *data, bool on)
{
	struct device *dev = regmap_get_device(data->regmap);
	int ret;

	if (on) {
		ret = pm_runtime_get_sync(dev);
	} else {
		pm_runtime_mark_last_busy(dev);
		ret = pm_runtime_put_autosuspend(dev);
	}

	if (ret < 0) {
		dev_err(dev,
			"Failed: %s for %d\n", __func__, on);
		if (on)
			pm_runtime_put_noidle(dev);

		return ret;
	}

	return 0;
}
#else
static int bmc150_accel_set_power_state(struct bmc150_accel_data *data, bool on)
{
	return 0;
}
#endif

#ifdef CONFIG_ACPI
/*
 * Support for getting accelerometer information from BOSC0200 ACPI nodes.
 *
 * There are 2 variants of the BOSC0200 ACPI node. Some 2-in-1s with 360 degree
 * hinges declare 2 I2C ACPI-resources for 2 accelerometers, 1 in the display
 * and 1 in the base of the 2-in-1. On these 2-in-1s the ROMS ACPI object
 * contains the mount-matrix for the sensor in the display and ROMK contains
 * the mount-matrix for the sensor in the base. On devices using a single
 * sensor there is a ROTM ACPI object which contains the mount-matrix.
 *
 * Here is an incomplete list of devices known to use 1 of these setups:
 *
 * Yoga devices with 2 accelerometers using ROMS + ROMK for the mount-matrices:
 * Lenovo Thinkpad Yoga 11e 3th gen
 * Lenovo Thinkpad Yoga 11e 4th gen
 *
 * Tablets using a single accelerometer using ROTM for the mount-matrix:
 * Chuwi Hi8 Pro (CWI513)
 * Chuwi Vi8 Plus (CWI519)
 * Chuwi Hi13
 * Irbis TW90
 * Jumper EZpad mini 3
 * Onda V80 plus
 * Predia Basic Tablet
 */
static bool bmc150_apply_acpi_orientation(struct device *dev,
					  struct iio_mount_matrix *orientation)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct acpi_device *adev = ACPI_COMPANION(dev);
	char *name, *alt_name, *label, *str;
	union acpi_object *obj, *elements;
	acpi_status status;
	int i, j, val[3];

	if (!adev || !acpi_dev_hid_uid_match(adev, "BOSC0200", NULL))
		return false;

	if (strcmp(dev_name(dev), "i2c-BOSC0200:base") == 0) {
		alt_name = "ROMK";
		label = "accel-base";
	} else {
		alt_name = "ROMS";
		label = "accel-display";
	}

	if (acpi_has_method(adev->handle, "ROTM")) {
		name = "ROTM";
	} else if (acpi_has_method(adev->handle, alt_name)) {
		name = alt_name;
		indio_dev->label = label;
	} else {
		return false;
	}

	status = acpi_evaluate_object(adev->handle, name, NULL, &buffer);
	if (ACPI_FAILURE(status)) {
		dev_warn(dev, "Failed to get ACPI mount matrix: %d\n", status);
		return false;
	}

	obj = buffer.pointer;
	if (obj->type != ACPI_TYPE_PACKAGE || obj->package.count != 3)
		goto unknown_format;

	elements = obj->package.elements;
	for (i = 0; i < 3; i++) {
		if (elements[i].type != ACPI_TYPE_STRING)
			goto unknown_format;

		str = elements[i].string.pointer;
		if (sscanf(str, "%d %d %d", &val[0], &val[1], &val[2]) != 3)
			goto unknown_format;

		for (j = 0; j < 3; j++) {
			switch (val[j]) {
			case -1: str = "-1"; break;
			case 0:  str = "0";  break;
			case 1:  str = "1";  break;
			default: goto unknown_format;
			}
			orientation->rotation[i * 3 + j] = str;
		}
	}

	kfree(buffer.pointer);
	return true;

unknown_format:
	dev_warn(dev, "Unknown ACPI mount matrix format, ignoring\n");
	kfree(buffer.pointer);
	return false;
}
#else
static bool bmc150_apply_acpi_orientation(struct device *dev,
					  struct iio_mount_matrix *orientation)
{
	return false;
}
#endif

static const struct bmc150_accel_interrupt_info {
	u8 map_reg;
	u8 map_bitmask;
	u8 en_reg;
	u8 en_bitmask;
} bmc150_accel_interrupts[BMC150_ACCEL_INTERRUPTS] = {
	{ /* data ready interrupt */
		.map_reg = BMC150_ACCEL_REG_INT_MAP_1,
		.map_bitmask = BMC150_ACCEL_INT_MAP_1_BIT_DATA,
		.en_reg = BMC150_ACCEL_REG_INT_EN_1,
		.en_bitmask = BMC150_ACCEL_INT_EN_BIT_DATA_EN,
	},
	{  /* motion interrupt */
		.map_reg = BMC150_ACCEL_REG_INT_MAP_0,
		.map_bitmask = BMC150_ACCEL_INT_MAP_0_BIT_SLOPE,
		.en_reg = BMC150_ACCEL_REG_INT_EN_0,
		.en_bitmask =  BMC150_ACCEL_INT_EN_BIT_SLP_X |
			BMC150_ACCEL_INT_EN_BIT_SLP_Y |
			BMC150_ACCEL_INT_EN_BIT_SLP_Z
	},
	{ /* fifo watermark interrupt */
		.map_reg = BMC150_ACCEL_REG_INT_MAP_1,
		.map_bitmask = BMC150_ACCEL_INT_MAP_1_BIT_FWM,
		.en_reg = BMC150_ACCEL_REG_INT_EN_1,
		.en_bitmask = BMC150_ACCEL_INT_EN_BIT_FWM_EN,
	},
};

static void bmc150_accel_interrupts_setup(struct iio_dev *indio_dev,
					  struct bmc150_accel_data *data)
{
	int i;

	for (i = 0; i < BMC150_ACCEL_INTERRUPTS; i++)
		data->interrupts[i].info = &bmc150_accel_interrupts[i];
}

static int bmc150_accel_set_interrupt(struct bmc150_accel_data *data, int i,
				      bool state)
{
	struct device *dev = regmap_get_device(data->regmap);
	struct bmc150_accel_interrupt *intr = &data->interrupts[i];
	const struct bmc150_accel_interrupt_info *info = intr->info;
	int ret;

	if (state) {
		if (atomic_inc_return(&intr->users) > 1)
			return 0;
	} else {
		if (atomic_dec_return(&intr->users) > 0)
			return 0;
	}

	/*
	 * We will expect the enable and disable to do operation in reverse
	 * order. This will happen here anyway, as our resume operation uses
	 * sync mode runtime pm calls. The suspend operation will be delayed
	 * by autosuspend delay.
	 * So the disable operation will still happen in reverse order of
	 * enable operation. When runtime pm is disabled the mode is always on,
	 * so sequence doesn't matter.
	 */
	ret = bmc150_accel_set_power_state(data, state);
	if (ret < 0)
		return ret;

	/* map the interrupt to the appropriate pins */
	ret = regmap_update_bits(data->regmap, info->map_reg, info->map_bitmask,
				 (state ? info->map_bitmask : 0));
	if (ret < 0) {
		dev_err(dev, "Error updating reg_int_map\n");
		goto out_fix_power_state;
	}

	/* enable/disable the interrupt */
	ret = regmap_update_bits(data->regmap, info->en_reg, info->en_bitmask,
				 (state ? info->en_bitmask : 0));
	if (ret < 0) {
		dev_err(dev, "Error updating reg_int_en\n");
		goto out_fix_power_state;
	}

	return 0;

out_fix_power_state:
	bmc150_accel_set_power_state(data, false);
	return ret;
}

static int bmc150_accel_set_scale(struct bmc150_accel_data *data, int val)
{
	struct device *dev = regmap_get_device(data->regmap);
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(data->chip_info->scale_table); ++i) {
		if (data->chip_info->scale_table[i].scale == val) {
			ret = regmap_write(data->regmap,
				     BMC150_ACCEL_REG_PMU_RANGE,
				     data->chip_info->scale_table[i].reg_range);
			if (ret < 0) {
				dev_err(dev, "Error writing pmu_range\n");
				return ret;
			}

			data->range = data->chip_info->scale_table[i].reg_range;
			return 0;
		}
	}

	return -EINVAL;
}

static int bmc150_accel_get_temp(struct bmc150_accel_data *data, int *val)
{
	struct device *dev = regmap_get_device(data->regmap);
	int ret;
	unsigned int value;

	mutex_lock(&data->mutex);

	ret = regmap_read(data->regmap, BMC150_ACCEL_REG_TEMP, &value);
	if (ret < 0) {
		dev_err(dev, "Error reading reg_temp\n");
		mutex_unlock(&data->mutex);
		return ret;
	}
	*val = sign_extend32(value, 7);

	mutex_unlock(&data->mutex);

	return IIO_VAL_INT;
}

static int bmc150_accel_get_axis(struct bmc150_accel_data *data,
				 struct iio_chan_spec const *chan,
				 int *val)
{
	struct device *dev = regmap_get_device(data->regmap);
	int ret;
	int axis = chan->scan_index;
	__le16 raw_val;

	mutex_lock(&data->mutex);
	ret = bmc150_accel_set_power_state(data, true);
	if (ret < 0) {
		mutex_unlock(&data->mutex);
		return ret;
	}

	ret = regmap_bulk_read(data->regmap, BMC150_ACCEL_AXIS_TO_REG(axis),
			       &raw_val, sizeof(raw_val));
	if (ret < 0) {
		dev_err(dev, "Error reading axis %d\n", axis);
		bmc150_accel_set_power_state(data, false);
		mutex_unlock(&data->mutex);
		return ret;
	}
	*val = sign_extend32(le16_to_cpu(raw_val) >> chan->scan_type.shift,
			     chan->scan_type.realbits - 1);
	ret = bmc150_accel_set_power_state(data, false);
	mutex_unlock(&data->mutex);
	if (ret < 0)
		return ret;

	return IIO_VAL_INT;
}

static int bmc150_accel_read_raw(struct iio_dev *indio_dev,
				 struct iio_chan_spec const *chan,
				 int *val, int *val2, long mask)
{
	struct bmc150_accel_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_TEMP:
			return bmc150_accel_get_temp(data, val);
		case IIO_ACCEL:
			if (iio_buffer_enabled(indio_dev))
				return -EBUSY;
			else
				return bmc150_accel_get_axis(data, chan, val);
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_OFFSET:
		if (chan->type == IIO_TEMP) {
			*val = BMC150_ACCEL_TEMP_CENTER_VAL;
			return IIO_VAL_INT;
		} else {
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		switch (chan->type) {
		case IIO_TEMP:
			*val2 = 500000;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_ACCEL:
		{
			int i;
			const struct bmc150_scale_info *si;
			int st_size = ARRAY_SIZE(data->chip_info->scale_table);

			for (i = 0; i < st_size; ++i) {
				si = &data->chip_info->scale_table[i];
				if (si->reg_range == data->range) {
					*val2 = si->scale;
					return IIO_VAL_INT_PLUS_MICRO;
				}
			}
			return -EINVAL;
		}
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SAMP_FREQ:
		mutex_lock(&data->mutex);
		ret = bmc150_accel_get_bw(data, val, val2);
		mutex_unlock(&data->mutex);
		return ret;
	default:
		return -EINVAL;
	}
}

static int bmc150_accel_write_raw(struct iio_dev *indio_dev,
				  struct iio_chan_spec const *chan,
				  int val, int val2, long mask)
{
	struct bmc150_accel_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		mutex_lock(&data->mutex);
		ret = bmc150_accel_set_bw(data, val, val2);
		mutex_unlock(&data->mutex);
		break;
	case IIO_CHAN_INFO_SCALE:
		if (val)
			return -EINVAL;

		mutex_lock(&data->mutex);
		ret = bmc150_accel_set_scale(data, val2);
		mutex_unlock(&data->mutex);
		return ret;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int bmc150_accel_read_event(struct iio_dev *indio_dev,
				   const struct iio_chan_spec *chan,
				   enum iio_event_type type,
				   enum iio_event_direction dir,
				   enum iio_event_info info,
				   int *val, int *val2)
{
	struct bmc150_accel_data *data = iio_priv(indio_dev);

	*val2 = 0;
	switch (info) {
	case IIO_EV_INFO_VALUE:
		*val = data->slope_thres;
		break;
	case IIO_EV_INFO_PERIOD:
		*val = data->slope_dur;
		break;
	default:
		return -EINVAL;
	}

	return IIO_VAL_INT;
}

static int bmc150_accel_write_event(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir,
				    enum iio_event_info info,
				    int val, int val2)
{
	struct bmc150_accel_data *data = iio_priv(indio_dev);

	if (data->ev_enable_state)
		return -EBUSY;

	switch (info) {
	case IIO_EV_INFO_VALUE:
		data->slope_thres = val & BMC150_ACCEL_SLOPE_THRES_MASK;
		break;
	case IIO_EV_INFO_PERIOD:
		data->slope_dur = val & BMC150_ACCEL_SLOPE_DUR_MASK;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int bmc150_accel_read_event_config(struct iio_dev *indio_dev,
					  const struct iio_chan_spec *chan,
					  enum iio_event_type type,
					  enum iio_event_direction dir)
{
	struct bmc150_accel_data *data = iio_priv(indio_dev);

	return data->ev_enable_state;
}

static int bmc150_accel_write_event_config(struct iio_dev *indio_dev,
					   const struct iio_chan_spec *chan,
					   enum iio_event_type type,
					   enum iio_event_direction dir,
					   int state)
{
	struct bmc150_accel_data *data = iio_priv(indio_dev);
	int ret;

	if (state == data->ev_enable_state)
		return 0;

	mutex_lock(&data->mutex);

	ret = bmc150_accel_set_interrupt(data, BMC150_ACCEL_INT_ANY_MOTION,
					 state);
	if (ret < 0) {
		mutex_unlock(&data->mutex);
		return ret;
	}

	data->ev_enable_state = state;
	mutex_unlock(&data->mutex);

	return 0;
}

static int bmc150_accel_validate_trigger(struct iio_dev *indio_dev,
					 struct iio_trigger *trig)
{
	struct bmc150_accel_data *data = iio_priv(indio_dev);
	int i;

	for (i = 0; i < BMC150_ACCEL_TRIGGERS; i++) {
		if (data->triggers[i].indio_trig == trig)
			return 0;
	}

	return -EINVAL;
}

static ssize_t bmc150_accel_get_fifo_watermark(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct bmc150_accel_data *data = iio_priv(indio_dev);
	int wm;

	mutex_lock(&data->mutex);
	wm = data->watermark;
	mutex_unlock(&data->mutex);

	return sprintf(buf, "%d\n", wm);
}

static ssize_t bmc150_accel_get_fifo_state(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct bmc150_accel_data *data = iio_priv(indio_dev);
	bool state;

	mutex_lock(&data->mutex);
	state = data->fifo_mode;
	mutex_unlock(&data->mutex);

	return sprintf(buf, "%d\n", state);
}

static const struct iio_mount_matrix *
bmc150_accel_get_mount_matrix(const struct iio_dev *indio_dev,
				const struct iio_chan_spec *chan)
{
	struct bmc150_accel_data *data = iio_priv(indio_dev);

	return &data->orientation;
}

static const struct iio_chan_spec_ext_info bmc150_accel_ext_info[] = {
	IIO_MOUNT_MATRIX(IIO_SHARED_BY_DIR, bmc150_accel_get_mount_matrix),
	{ }
};

static IIO_CONST_ATTR(hwfifo_watermark_min, "1");
static IIO_CONST_ATTR(hwfifo_watermark_max,
		      __stringify(BMC150_ACCEL_FIFO_LENGTH));
static IIO_DEVICE_ATTR(hwfifo_enabled, S_IRUGO,
		       bmc150_accel_get_fifo_state, NULL, 0);
static IIO_DEVICE_ATTR(hwfifo_watermark, S_IRUGO,
		       bmc150_accel_get_fifo_watermark, NULL, 0);

static const struct attribute *bmc150_accel_fifo_attributes[] = {
	&iio_const_attr_hwfifo_watermark_min.dev_attr.attr,
	&iio_const_attr_hwfifo_watermark_max.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark.dev_attr.attr,
	&iio_dev_attr_hwfifo_enabled.dev_attr.attr,
	NULL,
};

static int bmc150_accel_set_watermark(struct iio_dev *indio_dev, unsigned val)
{
	struct bmc150_accel_data *data = iio_priv(indio_dev);

	if (val > BMC150_ACCEL_FIFO_LENGTH)
		val = BMC150_ACCEL_FIFO_LENGTH;

	mutex_lock(&data->mutex);
	data->watermark = val;
	mutex_unlock(&data->mutex);

	return 0;
}

/*
 * We must read at least one full frame in one burst, otherwise the rest of the
 * frame data is discarded.
 */
static int bmc150_accel_fifo_transfer(struct bmc150_accel_data *data,
				      char *buffer, int samples)
{
	struct device *dev = regmap_get_device(data->regmap);
	int sample_length = 3 * 2;
	int ret;
	int total_length = samples * sample_length;

	ret = regmap_raw_read(data->regmap, BMC150_ACCEL_REG_FIFO_DATA,
			      buffer, total_length);
	if (ret)
		dev_err(dev,
			"Error transferring data from fifo: %d\n", ret);

	return ret;
}

static int __bmc150_accel_fifo_flush(struct iio_dev *indio_dev,
				     unsigned samples, bool irq)
{
	struct bmc150_accel_data *data = iio_priv(indio_dev);
	struct device *dev = regmap_get_device(data->regmap);
	int ret, i;
	u8 count;
	u16 buffer[BMC150_ACCEL_FIFO_LENGTH * 3];
	int64_t tstamp;
	uint64_t sample_period;
	unsigned int val;

	ret = regmap_read(data->regmap, BMC150_ACCEL_REG_FIFO_STATUS, &val);
	if (ret < 0) {
		dev_err(dev, "Error reading reg_fifo_status\n");
		return ret;
	}

	count = val & 0x7F;

	if (!count)
		return 0;

	/*
	 * If we getting called from IRQ handler we know the stored timestamp is
	 * fairly accurate for the last stored sample. Otherwise, if we are
	 * called as a result of a read operation from userspace and hence
	 * before the watermark interrupt was triggered, take a timestamp
	 * now. We can fall anywhere in between two samples so the error in this
	 * case is at most one sample period.
	 */
	if (!irq) {
		data->old_timestamp = data->timestamp;
		data->timestamp = iio_get_time_ns(indio_dev);
	}

	/*
	 * Approximate timestamps for each of the sample based on the sampling
	 * frequency, timestamp for last sample and number of samples.
	 *
	 * Note that we can't use the current bandwidth settings to compute the
	 * sample period because the sample rate varies with the device
	 * (e.g. between 31.70ms to 32.20ms for a bandwidth of 15.63HZ). That
	 * small variation adds when we store a large number of samples and
	 * creates significant jitter between the last and first samples in
	 * different batches (e.g. 32ms vs 21ms).
	 *
	 * To avoid this issue we compute the actual sample period ourselves
	 * based on the timestamp delta between the last two flush operations.
	 */
	sample_period = (data->timestamp - data->old_timestamp);
	do_div(sample_period, count);
	tstamp = data->timestamp - (count - 1) * sample_period;

	if (samples && count > samples)
		count = samples;

	ret = bmc150_accel_fifo_transfer(data, (u8 *)buffer, count);
	if (ret)
		return ret;

	/*
	 * Ideally we want the IIO core to handle the demux when running in fifo
	 * mode but not when running in triggered buffer mode. Unfortunately
	 * this does not seem to be possible, so stick with driver demux for
	 * now.
	 */
	for (i = 0; i < count; i++) {
		int j, bit;

		j = 0;
		for_each_set_bit(bit, indio_dev->active_scan_mask,
				 indio_dev->masklength)
			memcpy(&data->scan.channels[j++], &buffer[i * 3 + bit],
			       sizeof(data->scan.channels[0]));

		iio_push_to_buffers_with_timestamp(indio_dev, &data->scan,
						   tstamp);

		tstamp += sample_period;
	}

	return count;
}

static int bmc150_accel_fifo_flush(struct iio_dev *indio_dev, unsigned samples)
{
	struct bmc150_accel_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->mutex);
	ret = __bmc150_accel_fifo_flush(indio_dev, samples, false);
	mutex_unlock(&data->mutex);

	return ret;
}

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL(
		"15.620000 31.260000 62.50000 125 250 500 1000 2000");

static struct attribute *bmc150_accel_attributes[] = {
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group bmc150_accel_attrs_group = {
	.attrs = bmc150_accel_attributes,
};

static const struct iio_event_spec bmc150_accel_event = {
		.type = IIO_EV_TYPE_ROC,
		.dir = IIO_EV_DIR_EITHER,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) |
				 BIT(IIO_EV_INFO_ENABLE) |
				 BIT(IIO_EV_INFO_PERIOD)
};

#define BMC150_ACCEL_CHANNEL(_axis, bits) {				\
	.type = IIO_ACCEL,						\
	.modified = 1,							\
	.channel2 = IIO_MOD_##_axis,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),			\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |		\
				BIT(IIO_CHAN_INFO_SAMP_FREQ),		\
	.scan_index = AXIS_##_axis,					\
	.scan_type = {							\
		.sign = 's',						\
		.realbits = (bits),					\
		.storagebits = 16,					\
		.shift = 16 - (bits),					\
		.endianness = IIO_LE,					\
	},								\
	.ext_info = bmc150_accel_ext_info,				\
	.event_spec = &bmc150_accel_event,				\
	.num_event_specs = 1						\
}

#define BMC150_ACCEL_CHANNELS(bits) {					\
	{								\
		.type = IIO_TEMP,					\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		\
				      BIT(IIO_CHAN_INFO_SCALE) |	\
				      BIT(IIO_CHAN_INFO_OFFSET),	\
		.scan_index = -1,					\
	},								\
	BMC150_ACCEL_CHANNEL(X, bits),					\
	BMC150_ACCEL_CHANNEL(Y, bits),					\
	BMC150_ACCEL_CHANNEL(Z, bits),					\
	IIO_CHAN_SOFT_TIMESTAMP(3),					\
}

static const struct iio_chan_spec bma222e_accel_channels[] =
	BMC150_ACCEL_CHANNELS(8);
static const struct iio_chan_spec bma250e_accel_channels[] =
	BMC150_ACCEL_CHANNELS(10);
static const struct iio_chan_spec bmc150_accel_channels[] =
	BMC150_ACCEL_CHANNELS(12);
static const struct iio_chan_spec bma280_accel_channels[] =
	BMC150_ACCEL_CHANNELS(14);

static const struct bmc150_accel_chip_info bmc150_accel_chip_info_tbl[] = {
	[bmc150] = {
		.name = "BMC150A",
		.chip_id = 0xFA,
		.channels = bmc150_accel_channels,
		.num_channels = ARRAY_SIZE(bmc150_accel_channels),
		.scale_table = { {9610, BMC150_ACCEL_DEF_RANGE_2G},
				 {19122, BMC150_ACCEL_DEF_RANGE_4G},
				 {38344, BMC150_ACCEL_DEF_RANGE_8G},
				 {76590, BMC150_ACCEL_DEF_RANGE_16G} },
	},
	[bmi055] = {
		.name = "BMI055A",
		.chip_id = 0xFA,
		.channels = bmc150_accel_channels,
		.num_channels = ARRAY_SIZE(bmc150_accel_channels),
		.scale_table = { {9610, BMC150_ACCEL_DEF_RANGE_2G},
				 {19122, BMC150_ACCEL_DEF_RANGE_4G},
				 {38344, BMC150_ACCEL_DEF_RANGE_8G},
				 {76590, BMC150_ACCEL_DEF_RANGE_16G} },
	},
	[bma255] = {
		.name = "BMA0255",
		.chip_id = 0xFA,
		.channels = bmc150_accel_channels,
		.num_channels = ARRAY_SIZE(bmc150_accel_channels),
		.scale_table = { {9610, BMC150_ACCEL_DEF_RANGE_2G},
				 {19122, BMC150_ACCEL_DEF_RANGE_4G},
				 {38344, BMC150_ACCEL_DEF_RANGE_8G},
				 {76590, BMC150_ACCEL_DEF_RANGE_16G} },
	},
	[bma250e] = {
		.name = "BMA250E",
		.chip_id = 0xF9,
		.channels = bma250e_accel_channels,
		.num_channels = ARRAY_SIZE(bma250e_accel_channels),
		.scale_table = { {38344, BMC150_ACCEL_DEF_RANGE_2G},
				 {76590, BMC150_ACCEL_DEF_RANGE_4G},
				 {153277, BMC150_ACCEL_DEF_RANGE_8G},
				 {306457, BMC150_ACCEL_DEF_RANGE_16G} },
	},
	[bma222] = {
		.name = "BMA222",
		.chip_id = 0x03,
		.channels = bma222e_accel_channels,
		.num_channels = ARRAY_SIZE(bma222e_accel_channels),
		/*
		 * The datasheet page 17 says:
		 * 15.6, 31.3, 62.5 and 125 mg per LSB.
		 */
		.scale_table = { {156000, BMC150_ACCEL_DEF_RANGE_2G},
				 {313000, BMC150_ACCEL_DEF_RANGE_4G},
				 {625000, BMC150_ACCEL_DEF_RANGE_8G},
				 {1250000, BMC150_ACCEL_DEF_RANGE_16G} },
	},
	[bma222e] = {
		.name = "BMA222E",
		.chip_id = 0xF8,
		.channels = bma222e_accel_channels,
		.num_channels = ARRAY_SIZE(bma222e_accel_channels),
		.scale_table = { {153277, BMC150_ACCEL_DEF_RANGE_2G},
				 {306457, BMC150_ACCEL_DEF_RANGE_4G},
				 {612915, BMC150_ACCEL_DEF_RANGE_8G},
				 {1225831, BMC150_ACCEL_DEF_RANGE_16G} },
	},
	[bma280] = {
		.name = "BMA0280",
		.chip_id = 0xFB,
		.channels = bma280_accel_channels,
		.num_channels = ARRAY_SIZE(bma280_accel_channels),
		.scale_table = { {2392, BMC150_ACCEL_DEF_RANGE_2G},
				 {4785, BMC150_ACCEL_DEF_RANGE_4G},
				 {9581, BMC150_ACCEL_DEF_RANGE_8G},
				 {19152, BMC150_ACCEL_DEF_RANGE_16G} },
	},
};

static const struct iio_info bmc150_accel_info = {
	.attrs			= &bmc150_accel_attrs_group,
	.read_raw		= bmc150_accel_read_raw,
	.write_raw		= bmc150_accel_write_raw,
	.read_event_value	= bmc150_accel_read_event,
	.write_event_value	= bmc150_accel_write_event,
	.write_event_config	= bmc150_accel_write_event_config,
	.read_event_config	= bmc150_accel_read_event_config,
};

static const struct iio_info bmc150_accel_info_fifo = {
	.attrs			= &bmc150_accel_attrs_group,
	.read_raw		= bmc150_accel_read_raw,
	.write_raw		= bmc150_accel_write_raw,
	.read_event_value	= bmc150_accel_read_event,
	.write_event_value	= bmc150_accel_write_event,
	.write_event_config	= bmc150_accel_write_event_config,
	.read_event_config	= bmc150_accel_read_event_config,
	.validate_trigger	= bmc150_accel_validate_trigger,
	.hwfifo_set_watermark	= bmc150_accel_set_watermark,
	.hwfifo_flush_to_buffer	= bmc150_accel_fifo_flush,
};

static const unsigned long bmc150_accel_scan_masks[] = {
					BIT(AXIS_X) | BIT(AXIS_Y) | BIT(AXIS_Z),
					0};

static irqreturn_t bmc150_accel_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct bmc150_accel_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->mutex);
	ret = regmap_bulk_read(data->regmap, BMC150_ACCEL_REG_XOUT_L,
			       data->buffer, AXIS_MAX * 2);
	mutex_unlock(&data->mutex);
	if (ret < 0)
		goto err_read;

	iio_push_to_buffers_with_timestamp(indio_dev, data->buffer,
					   pf->timestamp);
err_read:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static void bmc150_accel_trig_reen(struct iio_trigger *trig)
{
	struct bmc150_accel_trigger *t = iio_trigger_get_drvdata(trig);
	struct bmc150_accel_data *data = t->data;
	struct device *dev = regmap_get_device(data->regmap);
	int ret;

	/* new data interrupts don't need ack */
	if (t == &t->data->triggers[BMC150_ACCEL_TRIGGER_DATA_READY])
		return;

	mutex_lock(&data->mutex);
	/* clear any latched interrupt */
	ret = regmap_write(data->regmap, BMC150_ACCEL_REG_INT_RST_LATCH,
			   BMC150_ACCEL_INT_MODE_LATCH_INT |
			   BMC150_ACCEL_INT_MODE_LATCH_RESET);
	mutex_unlock(&data->mutex);
	if (ret < 0)
		dev_err(dev, "Error writing reg_int_rst_latch\n");
}

static int bmc150_accel_trigger_set_state(struct iio_trigger *trig,
					  bool state)
{
	struct bmc150_accel_trigger *t = iio_trigger_get_drvdata(trig);
	struct bmc150_accel_data *data = t->data;
	int ret;

	mutex_lock(&data->mutex);

	if (t->enabled == state) {
		mutex_unlock(&data->mutex);
		return 0;
	}

	if (t->setup) {
		ret = t->setup(t, state);
		if (ret < 0) {
			mutex_unlock(&data->mutex);
			return ret;
		}
	}

	ret = bmc150_accel_set_interrupt(data, t->intr, state);
	if (ret < 0) {
		mutex_unlock(&data->mutex);
		return ret;
	}

	t->enabled = state;

	mutex_unlock(&data->mutex);

	return ret;
}

static const struct iio_trigger_ops bmc150_accel_trigger_ops = {
	.set_trigger_state = bmc150_accel_trigger_set_state,
	.reenable = bmc150_accel_trig_reen,
};

static int bmc150_accel_handle_roc_event(struct iio_dev *indio_dev)
{
	struct bmc150_accel_data *data = iio_priv(indio_dev);
	struct device *dev = regmap_get_device(data->regmap);
	int dir;
	int ret;
	unsigned int val;

	ret = regmap_read(data->regmap, BMC150_ACCEL_REG_INT_STATUS_2, &val);
	if (ret < 0) {
		dev_err(dev, "Error reading reg_int_status_2\n");
		return ret;
	}

	if (val & BMC150_ACCEL_ANY_MOTION_BIT_SIGN)
		dir = IIO_EV_DIR_FALLING;
	else
		dir = IIO_EV_DIR_RISING;

	if (val & BMC150_ACCEL_ANY_MOTION_BIT_X)
		iio_push_event(indio_dev,
			       IIO_MOD_EVENT_CODE(IIO_ACCEL,
						  0,
						  IIO_MOD_X,
						  IIO_EV_TYPE_ROC,
						  dir),
			       data->timestamp);

	if (val & BMC150_ACCEL_ANY_MOTION_BIT_Y)
		iio_push_event(indio_dev,
			       IIO_MOD_EVENT_CODE(IIO_ACCEL,
						  0,
						  IIO_MOD_Y,
						  IIO_EV_TYPE_ROC,
						  dir),
			       data->timestamp);

	if (val & BMC150_ACCEL_ANY_MOTION_BIT_Z)
		iio_push_event(indio_dev,
			       IIO_MOD_EVENT_CODE(IIO_ACCEL,
						  0,
						  IIO_MOD_Z,
						  IIO_EV_TYPE_ROC,
						  dir),
			       data->timestamp);

	return ret;
}

static irqreturn_t bmc150_accel_irq_thread_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct bmc150_accel_data *data = iio_priv(indio_dev);
	struct device *dev = regmap_get_device(data->regmap);
	bool ack = false;
	int ret;

	mutex_lock(&data->mutex);

	if (data->fifo_mode) {
		ret = __bmc150_accel_fifo_flush(indio_dev,
						BMC150_ACCEL_FIFO_LENGTH, true);
		if (ret > 0)
			ack = true;
	}

	if (data->ev_enable_state) {
		ret = bmc150_accel_handle_roc_event(indio_dev);
		if (ret > 0)
			ack = true;
	}

	if (ack) {
		ret = regmap_write(data->regmap, BMC150_ACCEL_REG_INT_RST_LATCH,
				   BMC150_ACCEL_INT_MODE_LATCH_INT |
				   BMC150_ACCEL_INT_MODE_LATCH_RESET);
		if (ret)
			dev_err(dev, "Error writing reg_int_rst_latch\n");

		ret = IRQ_HANDLED;
	} else {
		ret = IRQ_NONE;
	}

	mutex_unlock(&data->mutex);

	return ret;
}

static irqreturn_t bmc150_accel_irq_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct bmc150_accel_data *data = iio_priv(indio_dev);
	bool ack = false;
	int i;

	data->old_timestamp = data->timestamp;
	data->timestamp = iio_get_time_ns(indio_dev);

	for (i = 0; i < BMC150_ACCEL_TRIGGERS; i++) {
		if (data->triggers[i].enabled) {
			iio_trigger_poll(data->triggers[i].indio_trig);
			ack = true;
			break;
		}
	}

	if (data->ev_enable_state || data->fifo_mode)
		return IRQ_WAKE_THREAD;

	if (ack)
		return IRQ_HANDLED;

	return IRQ_NONE;
}

static const struct {
	int intr;
	const char *name;
	int (*setup)(struct bmc150_accel_trigger *t, bool state);
} bmc150_accel_triggers[BMC150_ACCEL_TRIGGERS] = {
	{
		.intr = 0,
		.name = "%s-dev%d",
	},
	{
		.intr = 1,
		.name = "%s-any-motion-dev%d",
		.setup = bmc150_accel_any_motion_setup,
	},
};

static void bmc150_accel_unregister_triggers(struct bmc150_accel_data *data,
					     int from)
{
	int i;

	for (i = from; i >= 0; i--) {
		if (data->triggers[i].indio_trig) {
			iio_trigger_unregister(data->triggers[i].indio_trig);
			data->triggers[i].indio_trig = NULL;
		}
	}
}

static int bmc150_accel_triggers_setup(struct iio_dev *indio_dev,
				       struct bmc150_accel_data *data)
{
	struct device *dev = regmap_get_device(data->regmap);
	int i, ret;

	for (i = 0; i < BMC150_ACCEL_TRIGGERS; i++) {
		struct bmc150_accel_trigger *t = &data->triggers[i];

		t->indio_trig = devm_iio_trigger_alloc(dev,
					bmc150_accel_triggers[i].name,
						       indio_dev->name,
						       indio_dev->id);
		if (!t->indio_trig) {
			ret = -ENOMEM;
			break;
		}

		t->indio_trig->ops = &bmc150_accel_trigger_ops;
		t->intr = bmc150_accel_triggers[i].intr;
		t->data = data;
		t->setup = bmc150_accel_triggers[i].setup;
		iio_trigger_set_drvdata(t->indio_trig, t);

		ret = iio_trigger_register(t->indio_trig);
		if (ret)
			break;
	}

	if (ret)
		bmc150_accel_unregister_triggers(data, i - 1);

	return ret;
}

#define BMC150_ACCEL_FIFO_MODE_STREAM          0x80
#define BMC150_ACCEL_FIFO_MODE_FIFO            0x40
#define BMC150_ACCEL_FIFO_MODE_BYPASS          0x00

static int bmc150_accel_fifo_set_mode(struct bmc150_accel_data *data)
{
	struct device *dev = regmap_get_device(data->regmap);
	u8 reg = BMC150_ACCEL_REG_FIFO_CONFIG1;
	int ret;

	ret = regmap_write(data->regmap, reg, data->fifo_mode);
	if (ret < 0) {
		dev_err(dev, "Error writing reg_fifo_config1\n");
		return ret;
	}

	if (!data->fifo_mode)
		return 0;

	ret = regmap_write(data->regmap, BMC150_ACCEL_REG_FIFO_CONFIG0,
			   data->watermark);
	if (ret < 0)
		dev_err(dev, "Error writing reg_fifo_config0\n");

	return ret;
}

static int bmc150_accel_buffer_preenable(struct iio_dev *indio_dev)
{
	struct bmc150_accel_data *data = iio_priv(indio_dev);

	return bmc150_accel_set_power_state(data, true);
}

static int bmc150_accel_buffer_postenable(struct iio_dev *indio_dev)
{
	struct bmc150_accel_data *data = iio_priv(indio_dev);
	int ret = 0;

	if (indio_dev->currentmode == INDIO_BUFFER_TRIGGERED)
		return 0;

	mutex_lock(&data->mutex);

	if (!data->watermark)
		goto out;

	ret = bmc150_accel_set_interrupt(data, BMC150_ACCEL_INT_WATERMARK,
					 true);
	if (ret)
		goto out;

	data->fifo_mode = BMC150_ACCEL_FIFO_MODE_FIFO;

	ret = bmc150_accel_fifo_set_mode(data);
	if (ret) {
		data->fifo_mode = 0;
		bmc150_accel_set_interrupt(data, BMC150_ACCEL_INT_WATERMARK,
					   false);
	}

out:
	mutex_unlock(&data->mutex);

	return ret;
}

static int bmc150_accel_buffer_predisable(struct iio_dev *indio_dev)
{
	struct bmc150_accel_data *data = iio_priv(indio_dev);

	if (indio_dev->currentmode == INDIO_BUFFER_TRIGGERED)
		return 0;

	mutex_lock(&data->mutex);

	if (!data->fifo_mode)
		goto out;

	bmc150_accel_set_interrupt(data, BMC150_ACCEL_INT_WATERMARK, false);
	__bmc150_accel_fifo_flush(indio_dev, BMC150_ACCEL_FIFO_LENGTH, false);
	data->fifo_mode = 0;
	bmc150_accel_fifo_set_mode(data);

out:
	mutex_unlock(&data->mutex);

	return 0;
}

static int bmc150_accel_buffer_postdisable(struct iio_dev *indio_dev)
{
	struct bmc150_accel_data *data = iio_priv(indio_dev);

	return bmc150_accel_set_power_state(data, false);
}

static const struct iio_buffer_setup_ops bmc150_accel_buffer_ops = {
	.preenable = bmc150_accel_buffer_preenable,
	.postenable = bmc150_accel_buffer_postenable,
	.predisable = bmc150_accel_buffer_predisable,
	.postdisable = bmc150_accel_buffer_postdisable,
};

static int bmc150_accel_chip_init(struct bmc150_accel_data *data)
{
	struct device *dev = regmap_get_device(data->regmap);
	int ret, i;
	unsigned int val;

	/*
	 * Reset chip to get it in a known good state. A delay of 1.8ms after
	 * reset is required according to the data sheets of supported chips.
	 */
	regmap_write(data->regmap, BMC150_ACCEL_REG_RESET,
		     BMC150_ACCEL_RESET_VAL);
	usleep_range(1800, 2500);

	ret = regmap_read(data->regmap, BMC150_ACCEL_REG_CHIP_ID, &val);
	if (ret < 0) {
		dev_err(dev, "Error: Reading chip id\n");
		return ret;
	}

	dev_dbg(dev, "Chip Id %x\n", val);
	for (i = 0; i < ARRAY_SIZE(bmc150_accel_chip_info_tbl); i++) {
		if (bmc150_accel_chip_info_tbl[i].chip_id == val) {
			data->chip_info = &bmc150_accel_chip_info_tbl[i];
			break;
		}
	}

	if (!data->chip_info) {
		dev_err(dev, "Invalid chip %x\n", val);
		return -ENODEV;
	}

	ret = bmc150_accel_set_mode(data, BMC150_ACCEL_SLEEP_MODE_NORMAL, 0);
	if (ret < 0)
		return ret;

	/* Set Bandwidth */
	ret = bmc150_accel_set_bw(data, BMC150_ACCEL_DEF_BW, 0);
	if (ret < 0)
		return ret;

	/* Set Default Range */
	ret = regmap_write(data->regmap, BMC150_ACCEL_REG_PMU_RANGE,
			   BMC150_ACCEL_DEF_RANGE_4G);
	if (ret < 0) {
		dev_err(dev, "Error writing reg_pmu_range\n");
		return ret;
	}

	data->range = BMC150_ACCEL_DEF_RANGE_4G;

	/* Set default slope duration and thresholds */
	data->slope_thres = BMC150_ACCEL_DEF_SLOPE_THRESHOLD;
	data->slope_dur = BMC150_ACCEL_DEF_SLOPE_DURATION;
	ret = bmc150_accel_update_slope(data);
	if (ret < 0)
		return ret;

	/* Set default as latched interrupts */
	ret = regmap_write(data->regmap, BMC150_ACCEL_REG_INT_RST_LATCH,
			   BMC150_ACCEL_INT_MODE_LATCH_INT |
			   BMC150_ACCEL_INT_MODE_LATCH_RESET);
	if (ret < 0) {
		dev_err(dev, "Error writing reg_int_rst_latch\n");
		return ret;
	}

	return 0;
}

int bmc150_accel_core_probe(struct device *dev, struct regmap *regmap, int irq,
			    const char *name, bool block_supported)
{
	const struct attribute **fifo_attrs;
	struct bmc150_accel_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	dev_set_drvdata(dev, indio_dev);

	data->regmap = regmap;

	if (!bmc150_apply_acpi_orientation(dev, &data->orientation)) {
		ret = iio_read_mount_matrix(dev, "mount-matrix",
					     &data->orientation);
		if (ret)
			return ret;
	}

	/*
	 * VDD   is the analog and digital domain voltage supply
	 * VDDIO is the digital I/O voltage supply
	 */
	data->regulators[0].supply = "vdd";
	data->regulators[1].supply = "vddio";
	ret = devm_regulator_bulk_get(dev,
				      ARRAY_SIZE(data->regulators),
				      data->regulators);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get regulators\n");

	ret = regulator_bulk_enable(ARRAY_SIZE(data->regulators),
				    data->regulators);
	if (ret) {
		dev_err(dev, "failed to enable regulators: %d\n", ret);
		return ret;
	}
	/*
	 * 2ms or 3ms power-on time according to datasheets, let's better
	 * be safe than sorry and set this delay to 5ms.
	 */
	msleep(5);

	ret = bmc150_accel_chip_init(data);
	if (ret < 0)
		goto err_disable_regulators;

	mutex_init(&data->mutex);

	indio_dev->channels = data->chip_info->channels;
	indio_dev->num_channels = data->chip_info->num_channels;
	indio_dev->name = name ? name : data->chip_info->name;
	indio_dev->available_scan_masks = bmc150_accel_scan_masks;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &bmc150_accel_info;

	if (block_supported) {
		indio_dev->modes |= INDIO_BUFFER_SOFTWARE;
		indio_dev->info = &bmc150_accel_info_fifo;
		fifo_attrs = bmc150_accel_fifo_attributes;
	} else {
		fifo_attrs = NULL;
	}

	ret = iio_triggered_buffer_setup_ext(indio_dev,
					     &iio_pollfunc_store_time,
					     bmc150_accel_trigger_handler,
					     &bmc150_accel_buffer_ops,
					     fifo_attrs);
	if (ret < 0) {
		dev_err(dev, "Failed: iio triggered buffer setup\n");
		goto err_disable_regulators;
	}

	if (irq > 0) {
		ret = devm_request_threaded_irq(dev, irq,
						bmc150_accel_irq_handler,
						bmc150_accel_irq_thread_handler,
						IRQF_TRIGGER_RISING,
						BMC150_ACCEL_IRQ_NAME,
						indio_dev);
		if (ret)
			goto err_buffer_cleanup;

		/*
		 * Set latched mode interrupt. While certain interrupts are
		 * non-latched regardless of this settings (e.g. new data) we
		 * want to use latch mode when we can to prevent interrupt
		 * flooding.
		 */
		ret = regmap_write(data->regmap, BMC150_ACCEL_REG_INT_RST_LATCH,
				   BMC150_ACCEL_INT_MODE_LATCH_RESET);
		if (ret < 0) {
			dev_err(dev, "Error writing reg_int_rst_latch\n");
			goto err_buffer_cleanup;
		}

		bmc150_accel_interrupts_setup(indio_dev, data);

		ret = bmc150_accel_triggers_setup(indio_dev, data);
		if (ret)
			goto err_buffer_cleanup;
	}

	ret = pm_runtime_set_active(dev);
	if (ret)
		goto err_trigger_unregister;

	pm_runtime_enable(dev);
	pm_runtime_set_autosuspend_delay(dev, BMC150_AUTO_SUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(dev);

	ret = iio_device_register(indio_dev);
	if (ret < 0) {
		dev_err(dev, "Unable to register iio device\n");
		goto err_trigger_unregister;
	}

	return 0;

err_trigger_unregister:
	bmc150_accel_unregister_triggers(data, BMC150_ACCEL_TRIGGERS - 1);
err_buffer_cleanup:
	iio_triggered_buffer_cleanup(indio_dev);
err_disable_regulators:
	regulator_bulk_disable(ARRAY_SIZE(data->regulators),
			       data->regulators);

	return ret;
}
EXPORT_SYMBOL_GPL(bmc150_accel_core_probe);

struct i2c_client *bmc150_get_second_device(struct i2c_client *client)
{
	struct bmc150_accel_data *data = i2c_get_clientdata(client);

	if (!data)
		return NULL;

	return data->second_device;
}
EXPORT_SYMBOL_GPL(bmc150_get_second_device);

void bmc150_set_second_device(struct i2c_client *client)
{
	struct bmc150_accel_data *data = i2c_get_clientdata(client);

	if (data)
		data->second_device = client;
}
EXPORT_SYMBOL_GPL(bmc150_set_second_device);

int bmc150_accel_core_remove(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct bmc150_accel_data *data = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);

	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
	pm_runtime_put_noidle(dev);

	bmc150_accel_unregister_triggers(data, BMC150_ACCEL_TRIGGERS - 1);

	iio_triggered_buffer_cleanup(indio_dev);

	mutex_lock(&data->mutex);
	bmc150_accel_set_mode(data, BMC150_ACCEL_SLEEP_MODE_DEEP_SUSPEND, 0);
	mutex_unlock(&data->mutex);

	regulator_bulk_disable(ARRAY_SIZE(data->regulators),
			       data->regulators);

	return 0;
}
EXPORT_SYMBOL_GPL(bmc150_accel_core_remove);

#ifdef CONFIG_PM_SLEEP
static int bmc150_accel_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct bmc150_accel_data *data = iio_priv(indio_dev);

	mutex_lock(&data->mutex);
	bmc150_accel_set_mode(data, BMC150_ACCEL_SLEEP_MODE_SUSPEND, 0);
	mutex_unlock(&data->mutex);

	return 0;
}

static int bmc150_accel_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct bmc150_accel_data *data = iio_priv(indio_dev);

	mutex_lock(&data->mutex);
	bmc150_accel_set_mode(data, BMC150_ACCEL_SLEEP_MODE_NORMAL, 0);
	bmc150_accel_fifo_set_mode(data);
	mutex_unlock(&data->mutex);

	return 0;
}
#endif

#ifdef CONFIG_PM
static int bmc150_accel_runtime_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct bmc150_accel_data *data = iio_priv(indio_dev);
	int ret;

	ret = bmc150_accel_set_mode(data, BMC150_ACCEL_SLEEP_MODE_SUSPEND, 0);
	if (ret < 0)
		return -EAGAIN;

	return 0;
}

static int bmc150_accel_runtime_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct bmc150_accel_data *data = iio_priv(indio_dev);
	int ret;
	int sleep_val;

	ret = bmc150_accel_set_mode(data, BMC150_ACCEL_SLEEP_MODE_NORMAL, 0);
	if (ret < 0)
		return ret;
	ret = bmc150_accel_fifo_set_mode(data);
	if (ret < 0)
		return ret;

	sleep_val = bmc150_accel_get_startup_times(data);
	if (sleep_val < 20)
		usleep_range(sleep_val * 1000, 20000);
	else
		msleep_interruptible(sleep_val);

	return 0;
}
#endif

const struct dev_pm_ops bmc150_accel_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(bmc150_accel_suspend, bmc150_accel_resume)
	SET_RUNTIME_PM_OPS(bmc150_accel_runtime_suspend,
			   bmc150_accel_runtime_resume, NULL)
};
EXPORT_SYMBOL_GPL(bmc150_accel_pm_ops);

MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("BMC150 accelerometer driver");
