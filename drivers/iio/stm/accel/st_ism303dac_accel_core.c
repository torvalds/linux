// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics ism303dac driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2018 STMicroelectronics Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/iio/iio.h>
#include <linux/of.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/delay.h>
#include <linux/iio/buffer.h>
#include <linux/iio/events.h>
#include <asm/unaligned.h>

#include "st_ism303dac_accel.h"

#define ISM303DAC_FS_LIST_NUM			4
enum {
	ISM303DAC_LP_MODE = 0,
	ISM303DAC_HR_MODE,
	ISM303DAC_MODE_COUNT,
};

#define ISM303DAC_ADD_CHANNEL(device_type, modif, index, mod, endian, sbits,\
			      rbits, addr, s) \
{ \
	.type = device_type, \
	.modified = modif, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
			BIT(IIO_CHAN_INFO_SCALE), \
	.scan_index = index, \
	.channel2 = mod, \
	.address = addr, \
	.scan_type = { \
		.sign = s, \
		.realbits = rbits, \
		.shift = sbits - rbits, \
		.storagebits = sbits, \
		.endianness = endian, \
	}, \
}

struct ism303dac_odr_reg {
	u32 hz;
	u8 value;
	/* Skip samples. */
	u8 std_level;
};

static const struct ism303dac_odr_table_t {
	u8 addr;
	u8 mask;
	struct ism303dac_odr_reg odr_avl[ISM303DAC_MODE_COUNT][ISM303DAC_ODR_LP_LIST_NUM];
} ism303dac_odr_table = {
	.addr = ISM303DAC_ODR_ADDR,
	.mask = ISM303DAC_ODR_MASK,

	/* ODR values for Low Power Mode */
	.odr_avl[ISM303DAC_LP_MODE][0] = {.hz = 0,
					.value = ISM303DAC_ODR_POWER_OFF_VAL,
					.std_level = 0,},
	.odr_avl[ISM303DAC_LP_MODE][1] = {.hz = 1,
					.value = ISM303DAC_ODR_1HZ_LP_VAL,
					.std_level = 0,},
	.odr_avl[ISM303DAC_LP_MODE][2] = {.hz = 12,
					.value = ISM303DAC_ODR_12HZ_LP_VAL,
					.std_level = 4,},
	.odr_avl[ISM303DAC_LP_MODE][3] = {.hz = 25,
					.value = ISM303DAC_ODR_25HZ_LP_VAL,
					.std_level = 8,},
	.odr_avl[ISM303DAC_LP_MODE][4] = {.hz = 50,
					.value = ISM303DAC_ODR_50HZ_LP_VAL,
					.std_level = 24,},
	.odr_avl[ISM303DAC_LP_MODE][5] = {.hz = 100,
					.value = ISM303DAC_ODR_100HZ_LP_VAL,
					.std_level = 24,},
	.odr_avl[ISM303DAC_LP_MODE][6] = {.hz = 200,
					.value = ISM303DAC_ODR_200HZ_LP_VAL,
					.std_level = 32,},
	.odr_avl[ISM303DAC_LP_MODE][7] = {.hz = 400,
					.value = ISM303DAC_ODR_400HZ_LP_VAL,
					.std_level = 48,},
	.odr_avl[ISM303DAC_LP_MODE][8] = {.hz = 800,
					.value = ISM303DAC_ODR_800HZ_LP_VAL,
					.std_level = 50,},

	/* ODR values for High Resolution Mode */
	.odr_avl[ISM303DAC_HR_MODE][0] = {.hz = 0,
					.value = ISM303DAC_ODR_POWER_OFF_VAL,
					.std_level = 0,},
	.odr_avl[ISM303DAC_HR_MODE][1] = {.hz = 12,
					.value = ISM303DAC_ODR_12_5HZ_HR_VAL,
					.std_level = 4,},
	.odr_avl[ISM303DAC_HR_MODE][2] = {.hz = 25,
					.value = ISM303DAC_ODR_25HZ_HR_VAL,
					.std_level = 8,},
	.odr_avl[ISM303DAC_HR_MODE][3] = {.hz = 50,
					.value = ISM303DAC_ODR_50HZ_HR_VAL,
					.std_level = 24,},
	.odr_avl[ISM303DAC_HR_MODE][4] = {.hz = 100,
					.value = ISM303DAC_ODR_100HZ_HR_VAL,
					.std_level = 24,},
	.odr_avl[ISM303DAC_HR_MODE][5] = {.hz = 200,
					.value = ISM303DAC_ODR_200HZ_HR_VAL,
					.std_level = 32,},
	.odr_avl[ISM303DAC_HR_MODE][6] = {.hz = 400,
					.value = ISM303DAC_ODR_400HZ_HR_VAL,
					.std_level = 48,},
	.odr_avl[ISM303DAC_HR_MODE][7] = {.hz = 800,
					.value = ISM303DAC_ODR_800HZ_HR_VAL,
					.std_level = 50,},
};

struct ism303dac_fs_reg {
	unsigned int gain;
	u8 value;
	int urv;
};

static struct ism303dac_fs_table {
	u8 addr;
	u8 mask;
	struct ism303dac_fs_reg fs_avl[ISM303DAC_FS_LIST_NUM];
} ism303dac_fs_table = {
	.addr = ISM303DAC_FS_ADDR,
	.mask = ISM303DAC_FS_MASK,
	.fs_avl[0] = {
		.gain = ISM303DAC_FS_2G_GAIN,
		.value = ISM303DAC_FS_2G_VAL,
		.urv = 2,
	},
	.fs_avl[1] = {
		.gain = ISM303DAC_FS_4G_GAIN,
		.value = ISM303DAC_FS_4G_VAL,
		.urv = 4,
	},
	.fs_avl[2] = {
		.gain = ISM303DAC_FS_8G_GAIN,
		.value = ISM303DAC_FS_8G_VAL,
		.urv = 8,
	},
	.fs_avl[3] = {
		.gain = ISM303DAC_FS_16G_GAIN,
		.value = ISM303DAC_FS_16G_VAL,
		.urv = 16,
	},
};

static const struct iio_event_spec singol_thr_event = {
	.type = IIO_EV_TYPE_THRESH,
	.dir = IIO_EV_DIR_RISING,
};

const struct iio_event_spec ism303dac_fifo_flush_event = {
	.type = STM_IIO_EV_TYPE_FIFO_FLUSH,
	.dir = IIO_EV_DIR_EITHER,
};

static const struct ism303dac_sensors_table {
	const char *name;
	const char *description;
	const u32 min_odr_hz;
	const u8 iio_channel_size;
	const struct iio_chan_spec iio_channel[ISM303DAC_MAX_CHANNEL_SPEC];
} ism303dac_sensors_table[ISM303DAC_SENSORS_NUMB] = {
	[ISM303DAC_ACCEL] = {
		.name = "accel",
		.description = "ST ISM303DAC Accelerometer Sensor",
		.min_odr_hz = ISM303DAC_ACCEL_ODR,
		.iio_channel = {
			ISM303DAC_ADD_CHANNEL(IIO_ACCEL, 1, 0, IIO_MOD_X, IIO_LE,
					16, 16, ISM303DAC_OUTX_L_ADDR, 's'),
			ISM303DAC_ADD_CHANNEL(IIO_ACCEL, 1, 1, IIO_MOD_Y, IIO_LE,
					16, 16, ISM303DAC_OUTY_L_ADDR, 's'),
			ISM303DAC_ADD_CHANNEL(IIO_ACCEL, 1, 2, IIO_MOD_Z, IIO_LE,
					16, 16, ISM303DAC_OUTZ_L_ADDR, 's'),
			ST_ISM303DAC_FLUSH_CHANNEL(IIO_ACCEL),
			IIO_CHAN_SOFT_TIMESTAMP(3)
		},
		.iio_channel_size = ISM303DAC_MAX_CHANNEL_SPEC,
	},
	[ISM303DAC_TAP] = {
		.name = "tap",
		.description = "ST ISM303DAC Tap Sensor",
		.min_odr_hz = ISM303DAC_TAP_ODR,
		.iio_channel = {
			{
				.type = STM_IIO_TAP,
				.channel = 0,
				.modified = 0,
				.event_spec = &singol_thr_event,
				.num_event_specs = 1,
			},
			IIO_CHAN_SOFT_TIMESTAMP(1)
		},
		.iio_channel_size = ISM303DAC_EVENT_CHANNEL_SPEC_SIZE,
	},
	[ISM303DAC_DOUBLE_TAP] = {
		.name = "tap_tap",
		.description = "ST ISM303DAC Double Tap Sensor",
		.min_odr_hz = ISM303DAC_TAP_ODR,
		.iio_channel = {
			{
				.type = STM_IIO_TAP_TAP,
				.channel = 0,
				.modified = 0,
				.event_spec = &singol_thr_event,
				.num_event_specs = 1,
			},
			IIO_CHAN_SOFT_TIMESTAMP(1)
		},
		.iio_channel_size = ISM303DAC_EVENT_CHANNEL_SPEC_SIZE,
	},
};

static const struct {
	char *mode_str;
	u8 streg_val;
} ism303dac_selftest_table[] = {
	{
		.mode_str = "normal-mode",
		.streg_val = ISM303DAC_SELFTEST_NORMAL,
	},
	{
		.mode_str = "positive-sign",
		.streg_val = ISM303DAC_SELFTEST_POS_SIGN,
	},
	{
		.mode_str = "negative-sign",
		.streg_val = ISM303DAC_SELFTEST_NEG_SIGN,
	},
};

int ism303dac_read_register(struct ism303dac_data *cdata, u8 reg_addr,
			    int data_len, u8 *data, bool b_lock)
{
	return cdata->tf->read(cdata, reg_addr, data_len, data, b_lock);
}

static int ism303dac_write_register(struct ism303dac_data *cdata, u8 reg_addr,
				    u8 mask, u8 data, bool b_lock)
{
	int err;
	u8 new_data = 0x00, old_data = 0x00;

	err = ism303dac_read_register(cdata, reg_addr, 1, &old_data, b_lock);
	if (err < 0)
		return err;

	new_data = ((old_data & (~mask)) | ((data << __ffs(mask)) & mask));
	if (new_data == old_data)
		return 1;

	return cdata->tf->write(cdata, reg_addr, 1, &new_data, b_lock);
}

static int ism303dac_set_fifo_mode(struct ism303dac_data *cdata, enum fifo_mode fm)
{
	u8 reg_value;

	switch (fm) {
	case BYPASS:
		reg_value = ISM303DAC_FIFO_MODE_BYPASS;
		break;
	case CONTINUOS:
		reg_value = ISM303DAC_FIFO_MODE_CONTINUOS;
		break;
	default:
		return -EINVAL;
	}

	return ism303dac_write_register(cdata, ISM303DAC_FIFO_MODE_ADDR,
				ISM303DAC_FIFO_MODE_MASK, reg_value, true);
}

static int ism303dac_set_fs(struct ism303dac_sensor_data *sdata, unsigned int fs)
{
	int err, i;

	for (i = 0; i < ISM303DAC_FS_LIST_NUM; i++) {
		if (ism303dac_fs_table.fs_avl[i].urv == fs)
			break;
	}

	if (i == ISM303DAC_FS_LIST_NUM)
		return -EINVAL;

	err = ism303dac_write_register(sdata->cdata,
				ism303dac_fs_table.addr,
				ism303dac_fs_table.mask,
				ism303dac_fs_table.fs_avl[i].value, true);
	if (err < 0)
		return err;

	sdata->gain = ism303dac_fs_table.fs_avl[i].gain;

	return 0;
}

static int ism303dac_set_selftest_mode(struct ism303dac_sensor_data *sdata,
				       u8 index)
{
	return ism303dac_write_register(sdata->cdata, ISM303DAC_SELFTEST_ADDR,
				ISM303DAC_SELFTEST_MASK,
				ism303dac_selftest_table[index].streg_val, true);
}

static u8 ism303dac_event_irq1_value(struct ism303dac_data *cdata)
{
	u8 value = 0x0;

	if (CHECK_BIT(cdata->enabled_sensor, ISM303DAC_DOUBLE_TAP))
		value |= ISM303DAC_INT1_TAP_MASK;

	if (CHECK_BIT(cdata->enabled_sensor, ISM303DAC_TAP))
		value |= ISM303DAC_INT1_S_TAP_MASK | ISM303DAC_INT1_TAP_MASK;

	return value;
}

static int ism303dac_write_max_odr(struct ism303dac_sensor_data *sdata)
{
	int err, i;
	u32 max_odr = 0;
	u8 power_mode = sdata->cdata->power_mode;
	struct ism303dac_sensor_data *t_sdata;

	for (i = 0; i < ISM303DAC_SENSORS_NUMB; i++)
		if (CHECK_BIT(sdata->cdata->enabled_sensor, i)) {
			t_sdata = iio_priv(sdata->cdata->iio_sensors_dev[i]);
			if (t_sdata->odr > max_odr)
				max_odr = t_sdata->odr;
		}

	for (i = 0; i < ISM303DAC_ODR_LP_LIST_NUM; i++) {
		if (ism303dac_odr_table.odr_avl[power_mode][i].hz >= max_odr)
			break;
	}

	if (i == ISM303DAC_ODR_LP_LIST_NUM)
		return -EINVAL;

	err = ism303dac_write_register(sdata->cdata,
			ism303dac_odr_table.addr,
			ism303dac_odr_table.mask,
			ism303dac_odr_table.odr_avl[power_mode][i].value, true);
	if (err < 0)
		return err;

	sdata->cdata->common_odr = max_odr;
	sdata->cdata->std_level = ism303dac_odr_table.odr_avl[power_mode][i].std_level;

	switch (max_odr) {
	case 0:
		sdata->cdata->accel_deltatime = 0;
		break;

	case 12:
		sdata->cdata->accel_deltatime =
			80000000LL * sdata->cdata->hwfifo_watermark;
		break;

	default:
		sdata->cdata->accel_deltatime =
			div_s64(1000000000LL, max_odr) *
			sdata->cdata->hwfifo_watermark;
		break;
	}

	return 0;
}

int ism303dac_update_drdy_irq(struct ism303dac_sensor_data *sdata, bool state)
{
	u8 reg_addr, reg_val, reg_mask;

	switch (sdata->sindex) {
	case ISM303DAC_TAP:
	case ISM303DAC_DOUBLE_TAP:
		reg_val = ism303dac_event_irq1_value(sdata->cdata);
		reg_addr = ISM303DAC_CTRL4_INT1_PAD_ADDR;
		reg_mask = ISM303DAC_INT1_EVENTS_MASK;

		break;

	case ISM303DAC_ACCEL:
		reg_addr = ISM303DAC_CTRL4_INT1_PAD_ADDR;
		reg_mask = (sdata->cdata->hwfifo_enabled) ?
				ISM303DAC_INT1_FTH_MASK:
				ISM303DAC_DRDY_MASK;
		if (state)
			reg_val = ISM303DAC_EN_BIT;
		else
			reg_val = ISM303DAC_DIS_BIT;

		break;

	default:
		return -EINVAL;
	}

	return ism303dac_write_register(sdata->cdata, reg_addr, reg_mask,
				reg_val, true);
}

static int ism303dac_update_fifo(struct ism303dac_data *cdata, u16 watermark)
{
	int err;
	int fifo_size;
	struct iio_dev *indio_dev;

	indio_dev = cdata->iio_sensors_dev[ISM303DAC_ACCEL];
	cdata->timestamp = ism303dac_get_time_ns(indio_dev);
	cdata->sample_timestamp = cdata->timestamp;
	cdata->samples = 0;

	err = ism303dac_write_register(cdata, ISM303DAC_FIFO_THS_ADDR,
				ISM303DAC_FIFO_THS_MASK,
				watermark, true);
	if (err < 0)
		return err;

	if (cdata->fifo_data)
		kfree(cdata->fifo_data);

	cdata->fifo_data = 0;

	fifo_size = watermark * ISM303DAC_FIFO_BYTE_FOR_SAMPLE;
	if (fifo_size > 0) {
		cdata->fifo_data = kmalloc(fifo_size, GFP_KERNEL);
		if (!cdata->fifo_data)
			return -ENOMEM;

		cdata->fifo_size = fifo_size;
	}

	return ism303dac_set_fifo_mode(cdata, CONTINUOS);
}

int ism303dac_set_enable(struct ism303dac_sensor_data *sdata, bool state)
{
	int err = 0;
	u8 mode;

	if (sdata->enabled == state)
		return 0;

	/*
	 * Start assuming the sensor enabled if state == true.
	 * It will be restored if an error occur.
	 */
	if (state) {
		SET_BIT(sdata->cdata->enabled_sensor, sdata->sindex);
		mode = CONTINUOS;
	} else {
		RESET_BIT(sdata->cdata->enabled_sensor, sdata->sindex);
		mode = BYPASS;
	}

	switch (sdata->sindex) {
	case ISM303DAC_TAP:
		if (state && CHECK_BIT(sdata->cdata->enabled_sensor,
				       ISM303DAC_DOUBLE_TAP)) {
			err = -EINVAL;

			goto enable_sensor_error;
		}

		break;

	case ISM303DAC_DOUBLE_TAP:
		if (state && CHECK_BIT(sdata->cdata->enabled_sensor,
				       ISM303DAC_TAP)) {
			err = -EINVAL;

			goto enable_sensor_error;
		}

		break;

	case ISM303DAC_ACCEL:
		break;

	default:
		return -EINVAL;
	}

	err = ism303dac_update_drdy_irq(sdata, state);
	if (err < 0)
		goto enable_sensor_error;

	err = ism303dac_set_fifo_mode(sdata->cdata, mode);
	if (err < 0)
		return err;

	err = ism303dac_write_max_odr(sdata);
	if (err < 0)
		goto enable_sensor_error;

	sdata->enabled = state;

	return 0;

enable_sensor_error:
	if (state) {
		RESET_BIT(sdata->cdata->enabled_sensor, sdata->sindex);
	} else {
		SET_BIT(sdata->cdata->enabled_sensor, sdata->sindex);
	}

	return err;
}

static int ism303dac_init_sensors(struct ism303dac_data *cdata)
{
	int err, i;
	struct ism303dac_sensor_data *sdata;

	for (i = 0; i < ISM303DAC_SENSORS_NUMB; i++) {
		sdata = iio_priv(cdata->iio_sensors_dev[i]);

		err = ism303dac_set_enable(sdata, false);
		if (err < 0)
			return err;

		if (sdata->sindex == ISM303DAC_ACCEL) {
			err = ism303dac_set_fs(sdata, ISM303DAC_DEFAULT_ACCEL_FS);
			if (err < 0)
				return err;
		}
	}

	cdata->selftest_status = 0;

	/*
	 * Soft reset the device on power on.
	 */
	err = ism303dac_write_register(cdata, ISM303DAC_SOFT_RESET_ADDR,
				       ISM303DAC_SOFT_RESET_MASK,
				       ISM303DAC_EN_BIT, true);
	if (err < 0)
		return err;

	if (cdata->spi_3wire) {
		u8 data = ISM303DAC_ADD_INC_MASK | ISM303DAC_SIM_MASK;

		err = cdata->tf->write(cdata, ISM303DAC_SIM_ADDR, 1, &data,
				       false);
		if (err < 0)
			return err;
	}

	/*
	 * Enable latched interrupt mode.
	 */
	err = ism303dac_write_register(cdata, ISM303DAC_LIR_ADDR,
				       ISM303DAC_LIR_MASK,
				       ISM303DAC_EN_BIT, true);
	if (err < 0)
		return err;

	/*
	 * Enable block data update feature.
	 */
	err = ism303dac_write_register(cdata, ISM303DAC_BDU_ADDR,
				       ISM303DAC_BDU_MASK,
				       ISM303DAC_EN_BIT, true);
	if (err < 0)
		return err;

	/*
	 * Route interrupt from INT2 to INT1 pin.
	 */
	err = ism303dac_write_register(cdata, ISM303DAC_INT2_ON_INT1_ADDR,
				       ISM303DAC_INT2_ON_INT1_MASK,
				       ISM303DAC_EN_BIT, true);
	if (err < 0)
		return err;

	/*
	 * Configure default free fall event threshold.
	 */
	err = ism303dac_write_register(sdata->cdata, ISM303DAC_FREE_FALL_ADDR,
				       ISM303DAC_FREE_FALL_THS_MASK,
				       ISM303DAC_FREE_FALL_THS_DEFAULT, true);
	if (err < 0)
		return err;

	/*
	 * Configure default free fall event duration.
	 */
	err = ism303dac_write_register(sdata->cdata, ISM303DAC_FREE_FALL_ADDR,
				       ISM303DAC_FREE_FALL_DUR_MASK,
				       ISM303DAC_FREE_FALL_DUR_DEFAULT, true);
	if (err < 0)
		return err;

	/*
	 * Configure Tap event recognition on all direction (X, Y and Z axes).
	 */
	err = ism303dac_write_register(sdata->cdata, ISM303DAC_TAP_AXIS_ADDR,
				       ISM303DAC_TAP_AXIS_MASK,
				       ISM303DAC_TAP_AXIS_ANABLE_ALL, true);
	if (err < 0)
		return err;

	/*
	 * Configure default threshold for Tap event recognition.
	 */
	err = ism303dac_write_register(sdata->cdata, ISM303DAC_TAP_THS_ADDR,
				       ISM303DAC_TAP_THS_MASK,
				       ISM303DAC_TAP_THS_DEFAULT, true);
	if (err < 0)
		return err;

	/*
	 * Configure default threshold for Wake Up event recognition.
	 */
	err = ism303dac_write_register(sdata->cdata, ISM303DAC_WAKE_UP_THS_ADDR,
				       ISM303DAC_WAKE_UP_THS_WU_MASK,
				       ISM303DAC_WAKE_UP_THS_WU_DEFAULT, true);
	if (err < 0)
		return err;

	return 0;
}

static ssize_t ism303dac_get_sampling_frequency(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct ism303dac_sensor_data *sdata = iio_priv(dev_to_iio_dev(dev));

	return sprintf(buf, "%d\n", sdata->odr);
}

ssize_t ism303dac_set_sampling_frequency(struct device * dev,
					 struct device_attribute * attr,
					 const char *buf, size_t count)
{
	int err;
	u8 power_mode;
	unsigned int odr, i;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ism303dac_sensor_data *sdata = iio_priv(indio_dev);

	err = kstrtoint(buf, 10, &odr);
	if (err < 0)
		return err;

	if (sdata->odr == odr)
		return count;

	power_mode = sdata->cdata->power_mode;
	for (i = 0; i < ISM303DAC_ODR_LP_LIST_NUM; i++) {
		if (ism303dac_odr_table.odr_avl[power_mode][i].hz >= odr)
			break;
	}
	if (i == ISM303DAC_ODR_LP_LIST_NUM)
		return -EINVAL;

	mutex_lock(&indio_dev->mlock);
	sdata->odr = ism303dac_odr_table.odr_avl[power_mode][i].hz;
	mutex_unlock(&indio_dev->mlock);

	err = ism303dac_write_max_odr(sdata);

	return (err < 0) ? err : count;
}

static ssize_t ism303dac_get_sampling_frequency_avail(struct device *dev,
						      struct device_attribute *attr,
						      char *buf)
{
	int i, len = 0, mode_count, mode;
	struct ism303dac_sensor_data *sdata = iio_priv(dev_to_iio_dev(dev));

	mode = sdata->cdata->power_mode;
	mode_count = (mode == ISM303DAC_LP_MODE) ?
			ISM303DAC_ODR_LP_LIST_NUM : ISM303DAC_ODR_HR_LIST_NUM;

	for (i = 1; i < mode_count; i++) {
		len += scnprintf(buf + len, PAGE_SIZE - len, "%d ",
				ism303dac_odr_table.odr_avl[mode][i].hz);
	}
	buf[len - 1] = '\n';

	return len;
}

static ssize_t ism303dac_get_scale_avail(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	int i, len = 0;

	for (i = 0; i < ISM303DAC_FS_LIST_NUM; i++) {
		len += scnprintf(buf + len, PAGE_SIZE - len, "0.%06u ",
				 ism303dac_fs_table.fs_avl[i].gain);
	}
	buf[len - 1] = '\n';

	return len;
}

static int ism303dac_read_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *ch, int *val,
			int *val2, long mask)
{
	int err;
	u8 outdata[2];
	struct ism303dac_sensor_data *sdata = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&indio_dev->mlock);
		if (ism303dac_iio_dev_currentmode(indio_dev) == INDIO_BUFFER_TRIGGERED) {
			mutex_unlock(&indio_dev->mlock);
			return -EBUSY;
		}

		err = ism303dac_set_enable(sdata, true);
		if (err < 0) {
			mutex_unlock(&indio_dev->mlock);
			return -EBUSY;
		}

		msleep(40);

		err = ism303dac_read_register(sdata->cdata, ch->address, 2,
					      outdata, true);
		if (err < 0) {
			mutex_unlock(&indio_dev->mlock);
			return err;
		}

		*val = (s16)get_unaligned_le16(outdata);
		*val = *val >> ch->scan_type.shift;

		err = ism303dac_set_enable(sdata, false);
		mutex_unlock(&indio_dev->mlock);

		if (err < 0)
			return err;

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = sdata->gain;

		return IIO_VAL_INT_PLUS_MICRO;

	default:
		return -EINVAL;
	}

	return 0;
}

static int ism303dac_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val, int val2, long mask)
{
	int err, i;
	struct ism303dac_sensor_data *sdata = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		mutex_lock(&indio_dev->mlock);

		if (ism303dac_iio_dev_currentmode(indio_dev) == INDIO_BUFFER_TRIGGERED) {
			mutex_unlock(&indio_dev->mlock);
			return -EBUSY;
		}

		for (i = 0; i < ISM303DAC_FS_LIST_NUM; i++) {
			if (ism303dac_fs_table.fs_avl[i].gain == val2)
				break;
		}

		err = ism303dac_set_fs(sdata, ism303dac_fs_table.fs_avl[i].urv);
		mutex_unlock(&indio_dev->mlock);

		break;

	default:
		return -EINVAL;
	}

	return err;
}

static ssize_t ism303dac_sysfs_get_hwfifo_enabled(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ism303dac_sensor_data *sdata = iio_priv(indio_dev);

	return sprintf(buf, "%d\n", sdata->cdata->hwfifo_enabled);
}

ssize_t ism303dac_sysfs_set_hwfifo_enabled(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	int err = 0, enable = 0;
	u8 mode = BYPASS;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ism303dac_sensor_data *sdata = iio_priv(indio_dev);

	err = kstrtoint(buf, 10, &enable);
	if (err < 0)
			return err;

	if (enable != 0x0 && enable != 0x1)
		return -EINVAL;

	mode = (enable == 0x0) ? BYPASS : CONTINUOS;

	err = ism303dac_set_fifo_mode(sdata->cdata, mode);
	if (err < 0)
		return err;

	sdata->cdata->hwfifo_enabled = enable;

	return count;
}

static ssize_t ism303dac_sysfs_get_hwfifo_watermark(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ism303dac_sensor_data *sdata = iio_priv(indio_dev);

	return sprintf(buf, "%d\n", sdata->cdata->hwfifo_watermark);
}

ssize_t ism303dac_sysfs_set_hwfifo_watermark(struct device * dev,
					struct device_attribute * attr,
					const char *buf, size_t count)
{
	int err = 0, watermark = 0;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ism303dac_sensor_data *sdata = iio_priv(indio_dev);

	err = kstrtoint(buf, 10, &watermark);
	if (err < 0)
			return err;

	if ((watermark < 1) || (watermark > ISM303DAC_MAX_FIFO_THS))
		return -EINVAL;

	mutex_lock(&sdata->cdata->fifo_lock);
	err = ism303dac_update_fifo(sdata->cdata, watermark);
	mutex_unlock(&sdata->cdata->fifo_lock);
	if (err < 0)
		return err;

	sdata->cdata->hwfifo_watermark = watermark;

	return count;
}

static ssize_t ism303dac_sysfs_get_hwfifo_watermark_min(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	return sprintf(buf, "%d\n", 1);
}

static ssize_t ism303dac_sysfs_get_hwfifo_watermark_max(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	return sprintf(buf, "%d\n", ISM303DAC_MAX_FIFO_THS);
}

ssize_t ism303dac_sysfs_flush_fifo(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	u64 event_type;
	int64_t sensor_last_timestamp;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ism303dac_sensor_data *sdata = iio_priv(indio_dev);

	mutex_lock(&indio_dev->mlock);

	if (ism303dac_iio_dev_currentmode(indio_dev) == INDIO_BUFFER_TRIGGERED) {
		disable_irq(sdata->cdata->irq);
	} else {
		mutex_unlock(&indio_dev->mlock);
		return -EINVAL;
	}

	sensor_last_timestamp = ism303dac_get_time_ns(indio_dev);

	mutex_lock(&sdata->cdata->fifo_lock);
	ism303dac_read_fifo(sdata->cdata, true);
	mutex_unlock(&sdata->cdata->fifo_lock);

	sdata->cdata->timestamp = sensor_last_timestamp;

	if (sensor_last_timestamp == sdata->cdata->sample_timestamp)
		event_type = STM_IIO_EV_DIR_FIFO_EMPTY;
	else
		event_type = STM_IIO_EV_DIR_FIFO_DATA;

	iio_push_event(indio_dev, IIO_UNMOD_EVENT_CODE(IIO_ACCEL,
		       -1, STM_IIO_EV_TYPE_FIFO_FLUSH, event_type),
		       sensor_last_timestamp);

	enable_irq(sdata->cdata->irq);
	mutex_unlock(&indio_dev->mlock);

	return size;
}

static ssize_t ism303dac_get_selftest_avail(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	return sprintf(buf, "%s %s %s\n", ism303dac_selftest_table[0].mode_str,
		       ism303dac_selftest_table[1].mode_str,
		       ism303dac_selftest_table[2].mode_str);
}

static ssize_t ism303dac_get_selftest_status(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	u8 status;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ism303dac_sensor_data *sdata = iio_priv(indio_dev);

	status = sdata->cdata->selftest_status;
	return sprintf(buf, "%s\n", ism303dac_selftest_table[status].mode_str);
}

static ssize_t ism303dac_set_selftest_status(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t size)
{
	int err, i;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ism303dac_sensor_data *sdata = iio_priv(indio_dev);

	for (i = 0; i < ARRAY_SIZE(ism303dac_selftest_table); i++) {
		if (strncmp(buf, ism303dac_selftest_table[i].mode_str,
			    size - 2) == 0)
			break;
	}
	if (i == ARRAY_SIZE(ism303dac_selftest_table))
		return -EINVAL;

	err = ism303dac_set_selftest_mode(sdata, i);
	if (err < 0)
		return err;

	sdata->cdata->selftest_status = i;

	return size;
}

static ST_ISM303DAC_HWFIFO_ENABLED();
static ST_ISM303DAC_HWFIFO_WATERMARK();
static ST_ISM303DAC_HWFIFO_WATERMARK_MIN();
static ST_ISM303DAC_HWFIFO_WATERMARK_MAX();
static ST_ISM303DAC_HWFIFO_FLUSH();

static IIO_DEV_ATTR_SAMP_FREQ(S_IWUSR | S_IRUGO,
			      ism303dac_get_sampling_frequency,
			      ism303dac_set_sampling_frequency);
static IIO_DEV_ATTR_SAMP_FREQ_AVAIL(ism303dac_get_sampling_frequency_avail);
static IIO_DEVICE_ATTR(in_accel_scale_available, S_IRUGO,
		       ism303dac_get_scale_avail, NULL, 0);
static IIO_DEVICE_ATTR(selftest_available, S_IRUGO,
		       ism303dac_get_selftest_avail, NULL, 0);
static IIO_DEVICE_ATTR(selftest, S_IWUSR | S_IRUGO,
		       ism303dac_get_selftest_status,
		       ism303dac_set_selftest_status, 0);

static struct attribute *ism303dac_accel_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_accel_scale_available.dev_attr.attr,
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	&iio_dev_attr_selftest_available.dev_attr.attr,
	&iio_dev_attr_selftest.dev_attr.attr,
	&iio_dev_attr_hwfifo_enabled.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark_min.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark_max.dev_attr.attr,
	&iio_dev_attr_hwfifo_flush.dev_attr.attr,

	NULL,
};

static struct attribute *ism303dac_step_tap_attributes[] = {
	NULL,
};

static struct attribute *ism303dac_step_double_tap_attributes[] = {
	NULL,
};

static const struct attribute_group ism303dac_accel_attribute_group = {
	.attrs = ism303dac_accel_attributes,
};

static const struct attribute_group ism303dac_tap_attribute_group = {
	.attrs = ism303dac_step_tap_attributes,
};

static const struct attribute_group ism303dac_double_tap_attribute_group = {
	.attrs = ism303dac_step_double_tap_attributes,
};

static const struct iio_info ism303dac_info[ISM303DAC_SENSORS_NUMB] = {
	[ISM303DAC_ACCEL] = {
		.attrs = &ism303dac_accel_attribute_group,
		.read_raw = &ism303dac_read_raw,
		.write_raw = &ism303dac_write_raw,
	},
	[ISM303DAC_TAP] = {
		.attrs = &ism303dac_tap_attribute_group,
	},
	[ISM303DAC_DOUBLE_TAP] = {
		.attrs = &ism303dac_double_tap_attribute_group,
	},
};

#ifdef CONFIG_IIO_TRIGGER
static const struct iio_trigger_ops ism303dac_trigger_ops = {
	.set_trigger_state = (&ism303dac_trig_set_state),
};
#define ISM303DAC_TRIGGER_OPS (&ism303dac_trigger_ops)
#else
#define ISM303DAC_TRIGGER_OPS NULL
#endif

#ifdef CONFIG_OF
static u32 ism303dac_parse_dt(struct ism303dac_data *cdata)
{
	u32 val;
	struct device_node *np;

	np = cdata->dev->of_node;
	if (!np)
		return -EINVAL;

	if (!of_property_read_u32(np, "st,drdy-int-pin", &val) &&
	    (val <= 2) && (val > 0))
		cdata->drdy_int_pin = (u8) val;
	else
		cdata->drdy_int_pin = 1;

	return 0;
}
#endif

static int ism303dac_init_interface(struct ism303dac_data *cdata)
{
	struct device_node *np = cdata->dev->of_node;

	if (np && of_property_read_bool(np, "spi-3wire")) {
		u8 data;
		int err;

		data = ISM303DAC_ADD_INC_MASK | ISM303DAC_SIM_MASK;
		err = cdata->tf->write(cdata, ISM303DAC_SIM_ADDR, 1, &data,
				       false);
		if (err < 0)
			return err;

		cdata->spi_3wire = true;
	}

	return 0;
}

int ism303dac_common_probe(struct ism303dac_data *cdata, int irq)
{
	u8 wai = 0;
	int32_t err, i, n;
	struct iio_dev *piio_dev;
	struct ism303dac_sensor_data *sdata;

	mutex_init(&cdata->regs_lock);
	mutex_init(&cdata->tb.buf_lock);
	mutex_init(&cdata->fifo_lock);

	cdata->fifo_data = 0;

	err = ism303dac_init_interface(cdata);
	if (err < 0)
		return err;

	err = ism303dac_read_register(cdata, ISM303DAC_WHO_AM_I_ADDR, 1, &wai, true);
	if (err < 0) {
		dev_err(cdata->dev, "failed to read Who-Am-I register.\n");

		return err;
	}
	if (wai != ISM303DAC_WHO_AM_I_DEF) {
		dev_err(cdata->dev, "Who-Am-I value not valid (%x).\n", wai);

		return -ENODEV;
	}

	cdata->hwfifo_enabled = 0;

	err = ism303dac_set_fifo_mode(cdata, BYPASS);
	if (err < 0)
		return err;

	if (irq > 0) {
		cdata->irq = irq;
#ifdef CONFIG_OF
		err = ism303dac_parse_dt(cdata);
		if (err < 0)
			return err;
#else /* CONFIG_OF */
		if (cdata->dev->platform_data) {
			cdata->drdy_int_pin = ((struct ism303dac_platform_data *)
				cdata->dev->platform_data)->drdy_int_pin;

			if ((cdata->drdy_int_pin > 2) ||
			    (cdata->drdy_int_pin < 1))
				cdata->drdy_int_pin = 1;
		} else
			cdata->drdy_int_pin = 1;
#endif /* CONFIG_OF */

		dev_info(cdata->dev, "driver use DRDY int pin %d\n",
			 cdata->drdy_int_pin);
	}

	cdata->common_odr = 0;
	cdata->enabled_sensor = 0;
	/* Set min watermark. */
	cdata->hwfifo_watermark = 1;

	/*
	 * Select sensor power mode operation.
	 *
	 * - ISM303DAC_LP_MODE: Low Power. The output data are 10 bits encoded.
	 * - ISM303DAC_HR_MODE: High Resolution. 14 bits output data encoding.
	 */
	cdata->power_mode = ISM303DAC_MODE_DEFAULT;

	for (i = 0; i < ISM303DAC_SENSORS_NUMB; i++) {
		piio_dev = devm_iio_device_alloc(cdata->dev,
					sizeof(struct ism303dac_sensor_data *));
		if (!piio_dev)
			return -ENOMEM;

		cdata->iio_sensors_dev[i] = piio_dev;
		sdata = iio_priv(piio_dev);
		sdata->enabled = false;
		sdata->cdata = cdata;
		sdata->sindex = i;
		sdata->name = ism303dac_sensors_table[i].name;
		sdata->odr = ism303dac_sensors_table[i].min_odr_hz;

		piio_dev->channels = ism303dac_sensors_table[i].iio_channel;
		piio_dev->num_channels =
				ism303dac_sensors_table[i].iio_channel_size;
		piio_dev->info = &ism303dac_info[i];
		piio_dev->modes = INDIO_DIRECT_MODE;
		piio_dev->name = kasprintf(GFP_KERNEL, "%s_%s", cdata->name,
					   sdata->name);
	}

	err = ism303dac_init_sensors(cdata);
	if (err < 0)
		return err;

	err = ism303dac_allocate_rings(cdata);
	if (err < 0)
		return err;

	if (irq > 0) {
		err = ism303dac_allocate_triggers(cdata, ISM303DAC_TRIGGER_OPS);
		if (err < 0)
			goto deallocate_ring;
	}

	for (n = 0; n < ISM303DAC_SENSORS_NUMB; n++) {
		err = iio_device_register(cdata->iio_sensors_dev[n]);
		if (err)
			goto iio_device_unregister_and_trigger_deallocate;
	}

	dev_info(cdata->dev, "%s: probed\n", ISM303DAC_DEV_NAME);
	return 0;

iio_device_unregister_and_trigger_deallocate:
	for (n--; n >= 0; n--)
		iio_device_unregister(cdata->iio_sensors_dev[n]);

deallocate_ring:
	ism303dac_deallocate_rings(cdata);
	return err;
}
EXPORT_SYMBOL(ism303dac_common_probe);

void ism303dac_common_remove(struct ism303dac_data *cdata, int irq)
{
	int i;

	for (i = 0; i < ISM303DAC_SENSORS_NUMB; i++)
		iio_device_unregister(cdata->iio_sensors_dev[i]);

	if (irq > 0)
		ism303dac_deallocate_triggers(cdata);

	ism303dac_deallocate_rings(cdata);
}
EXPORT_SYMBOL(ism303dac_common_remove);

#ifdef CONFIG_PM
int __maybe_unused ism303dac_common_suspend(struct ism303dac_data *cdata)
{
	return 0;
}
EXPORT_SYMBOL(ism303dac_common_suspend);

int __maybe_unused ism303dac_common_resume(struct ism303dac_data *cdata)
{
	return 0;
}
EXPORT_SYMBOL(ism303dac_common_resume);
#endif /* CONFIG_PM */

MODULE_DESCRIPTION("STMicroelectronics ism303dac core driver");
MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_LICENSE("GPL v2");
