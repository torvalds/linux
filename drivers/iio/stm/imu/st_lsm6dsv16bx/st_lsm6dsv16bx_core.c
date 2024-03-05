// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_lsm6dsv16bx imu sensor driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2023 STMicroelectronics Inc.
 */

#include <linux/kernel.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/version.h>

#include <linux/platform_data/st_sensors_pdata.h>

#include "st_lsm6dsv16bx.h"

/**
 * List of supported self test mode
 */
static struct st_lsm6dsv16bx_selftest_table_t {
	char *smode;
	u8 value;
	u8 mask;
} st_lsm6dsv16bx_selftest_table[] = {
	[0] = {
		.smode = "disabled",
		.value = ST_LSM6DSV16BX_SELF_TEST_NORMAL_MODE_VAL,
	},
	[1] = {
		.smode = "positive-sign",
		.value = ST_LSM6DSV16BX_SELF_TEST_POS_SIGN_VAL,
	},
	[2] = {
		.smode = "negative-sign",
		.value = ST_LSM6DSV16BX_SELF_TEST_NEG_SIGN_VAL,
	},
};

/**
 * List of supported device settings
 *
 * The following table list all device supported by st_lsm6dsv16bx driver.
 */
static const struct st_lsm6dsv16bx_settings st_lsm6dsv16bx_sensor_settings[] = {
	{
		.id = {
			.hw_id = ST_LSM6DSV16BX_ID,
			.name = ST_LSM6DSV16BX_DEV_NAME,
		},
		.st_qvar_probe = true,
		.st_mlc_probe = true,
		.st_fsm_probe = true,
		.st_sflp_probe = true,
		.st_tdm_probe = true,
		.fs_table = {
			[ST_LSM6DSV16BX_ID_ACC] = {
				.size = 4,
				.reg = {
					.addr = ST_LSM6DSV16BX_REG_CTRL8_ADDR,
					.mask = GENMASK(1, 0),
				},
				.fs_avl[0] = { ST_LSM6DSV16BX_ACC_FS_2G_GAIN, 0x0 },
				.fs_avl[1] = { ST_LSM6DSV16BX_ACC_FS_4G_GAIN, 0x1 },
				.fs_avl[2] = { ST_LSM6DSV16BX_ACC_FS_8G_GAIN, 0x2 },
				.fs_avl[3] = { ST_LSM6DSV16BX_ACC_FS_16G_GAIN, 0x3 },
			},
			[ST_LSM6DSV16BX_ID_GYRO] = {
				.size = 6,
				.reg = {
					.addr = ST_LSM6DSV16BX_REG_CTRL6_ADDR,
					.mask = GENMASK(3, 0),
				},
				.fs_avl[0] = { ST_LSM6DSV16BX_GYRO_FS_125_GAIN, 0x0 },
				.fs_avl[1] = { ST_LSM6DSV16BX_GYRO_FS_250_GAIN, 0x1 },
				.fs_avl[2] = { ST_LSM6DSV16BX_GYRO_FS_500_GAIN, 0x2 },
				.fs_avl[3] = { ST_LSM6DSV16BX_GYRO_FS_1000_GAIN, 0x3 },
				.fs_avl[4] = { ST_LSM6DSV16BX_GYRO_FS_2000_GAIN, 0x4 },
				.fs_avl[5] = { ST_LSM6DSV16BX_GYRO_FS_4000_GAIN, 0x6 },
			},
			[ST_LSM6DSV16BX_ID_TEMP] = {
				.size = 1,
				.fs_avl[0] = { (1000000 / ST_LSM6DSV16BX_TEMP_GAIN), 0x0 },
			},
		},
	},
	{
		.id = {
			.hw_id = ST_LSM6DSV16B_ID,
			.name = ST_LSM6DSV16B_DEV_NAME,
		},
		.st_qvar_probe = false,
		.st_mlc_probe = false,
		.st_fsm_probe = true,
		.st_sflp_probe = true,
		.st_tdm_probe = true,
		.fs_table = {
			[ST_LSM6DSV16BX_ID_ACC] = {
				.size = 4,
				.reg = {
					.addr = ST_LSM6DSV16BX_REG_CTRL8_ADDR,
					.mask = GENMASK(1, 0),
				},
				.fs_avl[0] = { ST_LSM6DSV16BX_ACC_FS_2G_GAIN, 0x0 },
				.fs_avl[1] = { ST_LSM6DSV16BX_ACC_FS_4G_GAIN, 0x1 },
				.fs_avl[2] = { ST_LSM6DSV16BX_ACC_FS_8G_GAIN, 0x2 },
				.fs_avl[3] = { ST_LSM6DSV16BX_ACC_FS_16G_GAIN, 0x3 },
			},
			[ST_LSM6DSV16BX_ID_GYRO] = {
				.size = 6,
				.reg = {
					.addr = ST_LSM6DSV16BX_REG_CTRL6_ADDR,
					.mask = GENMASK(3, 0),
				},
				.fs_avl[0] = { ST_LSM6DSV16BX_GYRO_FS_125_GAIN, 0x0 },
				.fs_avl[1] = { ST_LSM6DSV16BX_GYRO_FS_250_GAIN, 0x1 },
				.fs_avl[2] = { ST_LSM6DSV16BX_GYRO_FS_500_GAIN, 0x2 },
				.fs_avl[3] = { ST_LSM6DSV16BX_GYRO_FS_1000_GAIN, 0x3 },
				.fs_avl[4] = { ST_LSM6DSV16BX_GYRO_FS_2000_GAIN, 0x4 },
				.fs_avl[5] = { ST_LSM6DSV16BX_GYRO_FS_4000_GAIN, 0x6 },
			},
			[ST_LSM6DSV16BX_ID_TEMP] = {
				.size = 1,
				.fs_avl[0] = { (1000000 / ST_LSM6DSV16BX_TEMP_GAIN), 0x0 },
			},
		},
	},
};

static const struct st_lsm6dsv16bx_odr_table_entry
st_lsm6dsv16bx_odr_table[] = {
	[ST_LSM6DSV16BX_ID_ACC] = {
		.size = 8,
		.reg = {
			.addr = ST_LSM6DSV16BX_REG_CTRL1_ADDR,
			.mask = ST_LSM6DSV16BX_ODR_MASK,
		},
		/*              odr           val batch */
		.odr_avl[0] = {   7, 500000, 0x02, 0x02 },
		.odr_avl[1] = {  15,      0, 0x03, 0x03 },
		.odr_avl[2] = {  30,      0, 0x04, 0x04 },
		.odr_avl[3] = {  60,      0, 0x05, 0x05 },
		.odr_avl[4] = { 120,      0, 0x06, 0x06 },
		.odr_avl[5] = { 240,      0, 0x07, 0x07 },
		.odr_avl[6] = { 480,      0, 0x08, 0x08 },
		.odr_avl[7] = { 960,      0, 0x09, 0x09 },
	},
	[ST_LSM6DSV16BX_ID_GYRO] = {
		.size = 8,
		.reg = {
			.addr = ST_LSM6DSV16BX_REG_CTRL2_ADDR,
			.mask = ST_LSM6DSV16BX_ODR_MASK,
		},
		/* G LP MODE 7 Hz batch 7 Hz */
		.odr_avl[0] = {   7, 500000, 0x02, 0x02 },
		.odr_avl[1] = {  15,      0, 0x03, 0x03 },
		.odr_avl[2] = {  30,      0, 0x04, 0x04 },
		.odr_avl[3] = {  60,      0, 0x05, 0x05 },
		.odr_avl[4] = { 120,      0, 0x06, 0x06 },
		.odr_avl[5] = { 240,      0, 0x07, 0x07 },
		.odr_avl[6] = { 480,      0, 0x08, 0x08 },
		.odr_avl[7] = { 960,      0, 0x09, 0x09 },
	},
	[ST_LSM6DSV16BX_ID_TEMP] = {
		.size = 3,
		.odr_avl[0] = {  1, 875000, 0x00, 0x01 },
		.odr_avl[1] = { 15,      0, 0x00, 0x02 },
		.odr_avl[2] = { 60,      0, 0x00, 0x03 },
	},
	[ST_LSM6DSV16BX_ID_6X_GAME] = {
		.size = 6,
		.odr_avl[0] = {  15, 0, 0x00, 0x00 },
		.odr_avl[1] = {  30, 0, 0x00, 0x01 },
		.odr_avl[2] = {  60, 0, 0x00, 0x02 },
		.odr_avl[3] = { 120, 0, 0x00, 0x03 },
		.odr_avl[4] = { 240, 0, 0x00, 0x04 },
		.odr_avl[5] = { 480, 0, 0x00, 0x05 },
	},
};

static const struct iio_mount_matrix *
st_lsm6dsv16bx_get_mount_matrix(const struct iio_dev *iio_dev,
			     const struct iio_chan_spec *ch)
{
	struct st_lsm6dsv16bx_sensor *sensor = iio_priv(iio_dev);
	struct st_lsm6dsv16bx_hw *hw = sensor->hw;

	return &hw->orientation;
}

static const struct iio_chan_spec_ext_info st_lsm6dsv16bx_chan_spec_ext_info[] = {
	IIO_MOUNT_MATRIX(IIO_SHARED_BY_TYPE, st_lsm6dsv16bx_get_mount_matrix),
	{ }
};

#define IIO_CHAN_HW_TIMESTAMP(si) {					\
	.type = IIO_COUNT,						\
	.address = ST_LSM6DSV16BX_REG_TIMESTAMP0_ADDR,			\
	.scan_index = si,						\
	.scan_type = {							\
		.sign = 's',						\
		.realbits = 64,						\
		.storagebits = 64,					\
		.endianness = IIO_LE,					\
	},								\
}

static const struct iio_chan_spec st_lsm6dsv16bx_acc_channels[] = {
	ST_LSM6DSV16BX_DATA_CHANNEL(IIO_ACCEL,
				    ST_LSM6DSV16BX_REG_OUTX_L_A_ADDR,
				    1, IIO_MOD_X, 0, 16, 16, 's',
				    st_lsm6dsv16bx_chan_spec_ext_info),
	ST_LSM6DSV16BX_DATA_CHANNEL(IIO_ACCEL,
				    ST_LSM6DSV16BX_REG_OUTY_L_A_ADDR,
				    1, IIO_MOD_Y, 1, 16, 16, 's',
				    st_lsm6dsv16bx_chan_spec_ext_info),
	ST_LSM6DSV16BX_DATA_CHANNEL(IIO_ACCEL,
				    ST_LSM6DSV16BX_REG_OUTZ_L_A_ADDR,
				    1, IIO_MOD_Z, 2, 16, 16, 's',
				    st_lsm6dsv16bx_chan_spec_ext_info),
	ST_LSM6DSV16BX_EVENT_CHANNEL(IIO_ACCEL, flush),
	IIO_CHAN_HW_TIMESTAMP(3),
	IIO_CHAN_SOFT_TIMESTAMP(4),
};

static const struct iio_chan_spec st_lsm6dsv16bx_gyro_channels[] = {
	ST_LSM6DSV16BX_DATA_CHANNEL(IIO_ANGL_VEL,
				    ST_LSM6DSV16BX_REG_OUTX_L_G_ADDR,
				    1, IIO_MOD_X, 0, 16, 16, 's',
				    st_lsm6dsv16bx_chan_spec_ext_info),
	ST_LSM6DSV16BX_DATA_CHANNEL(IIO_ANGL_VEL,
				    ST_LSM6DSV16BX_REG_OUTY_L_G_ADDR,
				    1, IIO_MOD_Y, 1, 16, 16, 's',
				    st_lsm6dsv16bx_chan_spec_ext_info),
	ST_LSM6DSV16BX_DATA_CHANNEL(IIO_ANGL_VEL,
				    ST_LSM6DSV16BX_REG_OUTZ_L_G_ADDR,
				    1, IIO_MOD_Z, 2, 16, 16, 's',
				    st_lsm6dsv16bx_chan_spec_ext_info),
	ST_LSM6DSV16BX_EVENT_CHANNEL(IIO_ANGL_VEL, flush),
	IIO_CHAN_HW_TIMESTAMP(3),
	IIO_CHAN_SOFT_TIMESTAMP(4),
};

static const struct iio_chan_spec st_lsm6dsv16bx_temp_channels[] = {
	{
		.type = IIO_TEMP,
		.address = ST_LSM6DSV16BX_REG_OUT_TEMP_L_ADDR,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_OFFSET) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.scan_index = 0,
		.scan_type = {
			.sign = 's',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_LE,
		}
	},
	ST_LSM6DSV16BX_EVENT_CHANNEL(IIO_TEMP, flush),
	IIO_CHAN_HW_TIMESTAMP(1),
	IIO_CHAN_SOFT_TIMESTAMP(2),
};

static const struct iio_chan_spec st_lsm6dsv16bx_sflp_channels[] = {
	ST_LSM6DSV16BX_SFLP_DATA_CHANNEL(IIO_ROT, 1, IIO_MOD_X,
					 0, 16, 16, 'u'),
	ST_LSM6DSV16BX_SFLP_DATA_CHANNEL(IIO_ROT, 1, IIO_MOD_Y,
					 1, 16, 16, 'u'),
	ST_LSM6DSV16BX_SFLP_DATA_CHANNEL(IIO_ROT, 1, IIO_MOD_Z,
					 2, 16, 16, 'u'),
	ST_LSM6DSV16BX_EVENT_CHANNEL(IIO_ANGL_VEL, flush),
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

static int st_lsm6dsv16bx_check_whoami(struct st_lsm6dsv16bx_hw *hw, int id)
{
	int data, err, i;

	for (i = 0; i < ARRAY_SIZE(st_lsm6dsv16bx_sensor_settings); i++) {
		if (st_lsm6dsv16bx_sensor_settings[i].id.name &&
		    st_lsm6dsv16bx_sensor_settings[i].id.hw_id == id)
			break;
	}

	if (i == ARRAY_SIZE(st_lsm6dsv16bx_sensor_settings)) {
		dev_err(hw->dev, "unsupported hw id [%02x]\n", id);

		return -ENODEV;
	}

	err = regmap_read(hw->regmap, ST_LSM6DSV16BX_REG_WHOAMI_ADDR, &data);
	if (err < 0) {
		dev_err(hw->dev, "failed to read whoami register\n");

		return err;
	}

	if (data != ST_LSM6DSV16BX_WHOAMI_VAL) {
		dev_err(hw->dev, "unsupported whoami [%02x]\n", data);

		return -ENODEV;
	}

	hw->settings = &st_lsm6dsv16bx_sensor_settings[i];
	hw->fs_table = hw->settings->fs_table;
	hw->odr_table = st_lsm6dsv16bx_odr_table;

	return 0;
}

static int st_lsm6dsv16bx_get_odr_calibration(struct st_lsm6dsv16bx_hw *hw)
{
	s64 odr_calib;
	int data;
	int err;

	err = regmap_read(hw->regmap,
			  ST_LSM6DSV16BX_REG_INTERNAL_FREQ_FINE, &data);
	if (err < 0) {
		dev_err(hw->dev, "failed to read %d register\n",
			ST_LSM6DSV16BX_REG_INTERNAL_FREQ_FINE);

		return err;
	}

	odr_calib = (data * 37500) / 1000;
	hw->ts_delta_ns = ST_LSM6DSV16BX_TS_DELTA_NS - odr_calib;

	dev_info(hw->dev, "Freq Fine %lld (ts %lld)\n",
		 odr_calib, hw->ts_delta_ns);

	return 0;
}

static int
st_lsm6dsv16bx_set_full_scale(struct st_lsm6dsv16bx_sensor *sensor, u32 gain)
{
	enum st_lsm6dsv16bx_sensor_id id = sensor->id;
	struct st_lsm6dsv16bx_hw *hw = sensor->hw;
	int i, err;
	u8 val;

	for (i = 0; i < hw->fs_table[id].size; i++)
		if (hw->fs_table[id].fs_avl[i].gain == gain)
			break;

	if (i == hw->fs_table[id].size)
		return -EINVAL;

	val = hw->fs_table[id].fs_avl[i].val;
	err = st_lsm6dsv16bx_write_with_mask(sensor->hw,
					  hw->fs_table[id].reg.addr,
					  hw->fs_table[id].reg.mask,
					  val);
	if (err < 0)
		return err;

	sensor->gain = gain;

	return 0;
}

static int st_lsm6dsv16bx_get_odr_val(enum st_lsm6dsv16bx_sensor_id id,
				      int odr, int uodr, int *podr,
				      int *puodr, u8 *val)
{
	int required_odr = ST_LSM6DSV16BX_ODR_EXPAND(odr, uodr);
	int sensor_odr;
	int i;

	for (i = 0; i < st_lsm6dsv16bx_odr_table[id].size; i++) {
		sensor_odr = ST_LSM6DSV16BX_ODR_EXPAND(
				   st_lsm6dsv16bx_odr_table[id].odr_avl[i].hz,
				   st_lsm6dsv16bx_odr_table[id].odr_avl[i].uhz);
		if (sensor_odr >= required_odr)
			break;
	}

	if (i == st_lsm6dsv16bx_odr_table[id].size)
		return -EINVAL;

	*val = st_lsm6dsv16bx_odr_table[id].odr_avl[i].val;

	if (podr && puodr) {
		*podr = st_lsm6dsv16bx_odr_table[id].odr_avl[i].hz;
		*puodr = st_lsm6dsv16bx_odr_table[id].odr_avl[i].uhz;
	}

	return 0;
}

int st_lsm6dsv16bx_get_batch_val(struct st_lsm6dsv16bx_sensor *sensor,
			      int odr, int uodr, u8 *val)
{
	int required_odr = ST_LSM6DSV16BX_ODR_EXPAND(odr, uodr);
	enum st_lsm6dsv16bx_sensor_id id = sensor->id;
	int sensor_odr;
	int i;

	for (i = 0; i < st_lsm6dsv16bx_odr_table[id].size; i++) {
		sensor_odr = ST_LSM6DSV16BX_ODR_EXPAND(
				   st_lsm6dsv16bx_odr_table[id].odr_avl[i].hz,
				   st_lsm6dsv16bx_odr_table[id].odr_avl[i].uhz);
		if (sensor_odr >= required_odr)
			break;
	}

	if (i == st_lsm6dsv16bx_odr_table[id].size)
		return -EINVAL;

	*val = st_lsm6dsv16bx_odr_table[id].odr_avl[i].batch_val;

	return 0;
}

static int
st_lsm6dsv16bx_set_hw_sensor_odr(struct st_lsm6dsv16bx_hw *hw,
				 enum st_lsm6dsv16bx_sensor_id id,
				 int req_odr, int req_uodr)
{
	int err;
	u8 val = 0;

	if (ST_LSM6DSV16BX_ODR_EXPAND(req_odr, req_uodr) > 0) {
		err = st_lsm6dsv16bx_get_odr_val(id, req_odr, req_uodr,
						 &req_odr, &req_uodr, &val);
		if (err < 0)
			return err;
	}

	err = st_lsm6dsv16bx_write_with_mask(hw,
					  st_lsm6dsv16bx_odr_table[id].reg.addr,
					  st_lsm6dsv16bx_odr_table[id].reg.mask,
					  val);

	return err < 0 ? err : 0;
}

static u16
st_lsm6dsv16bx_check_odr_dependency(struct st_lsm6dsv16bx_hw *hw, int odr,
				    int uodr,
				    enum st_lsm6dsv16bx_sensor_id ref_id)
{
	struct st_lsm6dsv16bx_sensor *ref = iio_priv(hw->iio_devs[ref_id]);
	bool enable = ST_LSM6DSV16BX_ODR_EXPAND(odr, uodr) > 0;
	u16 ret;

	if (enable) {
		/* uodr not used */
		if (hw->enable_mask & BIT(ref_id))
			ret = max_t(int, ref->odr, odr);
		else
			ret = odr;
	} else {
		ret = (hw->enable_mask & BIT(ref_id)) ? ref->odr : 0;
	}

	return ret;
}

static int
st_lsm6dsv16bx_check_acc_odr_dependency(struct st_lsm6dsv16bx_sensor *sensor,
					int req_odr, int req_uodr)
{
	struct st_lsm6dsv16bx_hw *hw = sensor->hw;
	enum st_lsm6dsv16bx_sensor_id id;
	int odr = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(st_lsm6dsv16bx_acc_dep_sensor_list); i++) {
		id = st_lsm6dsv16bx_acc_dep_sensor_list[i];
		if (!hw->iio_devs[id])
			continue;

		if (id == sensor->id)
			continue;

		/* req_uodr not used */
		odr = st_lsm6dsv16bx_check_odr_dependency(hw, req_odr,
							  req_uodr, id);
		if (odr != req_odr)
			return 0;
	}

	return odr;
}

static int
st_lsm6dsv16bx_check_gyro_odr_dependency(struct st_lsm6dsv16bx_sensor *sensor,
					 int req_odr, int req_uodr)
{
	struct st_lsm6dsv16bx_hw *hw = sensor->hw;
	enum st_lsm6dsv16bx_sensor_id id;
	int odr = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(st_lsm6dsv16bx_gyro_dep_sensor_list); i++) {
		id = st_lsm6dsv16bx_gyro_dep_sensor_list[i];
		if (!hw->iio_devs[id])
			continue;

		if (id == sensor->id)
			continue;

		/* req_uodr not used */
		odr = st_lsm6dsv16bx_check_odr_dependency(hw, req_odr,
							  req_uodr, id);
		if (odr != req_odr)
			return 0;
	}

	return odr;
}
static int st_lsm6dsv16bx_set_odr(struct st_lsm6dsv16bx_sensor *sensor,
				  int req_odr, int req_uodr)
{
	enum st_lsm6dsv16bx_sensor_id id = sensor->id;
	struct st_lsm6dsv16bx_hw *hw = sensor->hw;
	int err, odr;

	switch (id) {
	case ST_LSM6DSV16BX_ID_QVAR:
	case ST_LSM6DSV16BX_ID_FSM_0:
	case ST_LSM6DSV16BX_ID_FSM_1:
	case ST_LSM6DSV16BX_ID_FSM_2:
	case ST_LSM6DSV16BX_ID_FSM_3:
	case ST_LSM6DSV16BX_ID_FSM_4:
	case ST_LSM6DSV16BX_ID_FSM_5:
	case ST_LSM6DSV16BX_ID_FSM_6:
	case ST_LSM6DSV16BX_ID_FSM_7:
	case ST_LSM6DSV16BX_ID_MLC_0:
	case ST_LSM6DSV16BX_ID_MLC_1:
	case ST_LSM6DSV16BX_ID_MLC_2:
	case ST_LSM6DSV16BX_ID_MLC_3:
	case ST_LSM6DSV16BX_ID_TEMP:
	case ST_LSM6DSV16BX_ID_STEP_COUNTER:
	case ST_LSM6DSV16BX_ID_STEP_DETECTOR:
	case ST_LSM6DSV16BX_ID_SIGN_MOTION:
	case ST_LSM6DSV16BX_ID_TILT:
	case ST_LSM6DSV16BX_ID_TAP:
	case ST_LSM6DSV16BX_ID_DTAP:
	case ST_LSM6DSV16BX_ID_WK:
	case ST_LSM6DSV16BX_ID_FF:
	case ST_LSM6DSV16BX_ID_SLPCHG:
	case ST_LSM6DSV16BX_ID_6D:
	case ST_LSM6DSV16BX_ID_ACC:
		odr = st_lsm6dsv16bx_check_acc_odr_dependency(sensor, req_odr,
							      req_uodr);
		if (odr != req_odr)
			return 0;

		return st_lsm6dsv16bx_set_hw_sensor_odr(hw, ST_LSM6DSV16BX_ID_ACC,
							req_odr, req_uodr);
	case ST_LSM6DSV16BX_ID_6X_GAME:
		odr = st_lsm6dsv16bx_check_acc_odr_dependency(sensor, req_odr,
							      req_uodr);
		if (odr != req_odr)
			return 0;

		err = st_lsm6dsv16bx_set_hw_sensor_odr(hw, ST_LSM6DSV16BX_ID_ACC,
						       req_odr, req_uodr);
		if (err < 0)
			return err;

		odr = st_lsm6dsv16bx_check_gyro_odr_dependency(sensor, req_odr,
							       req_uodr);
		if (odr != req_odr)
			return 0;

		return st_lsm6dsv16bx_set_hw_sensor_odr(hw, ST_LSM6DSV16BX_ID_GYRO,
							req_odr, req_uodr);
	case ST_LSM6DSV16BX_ID_GYRO:
		odr = st_lsm6dsv16bx_check_gyro_odr_dependency(sensor, req_odr,
							       req_uodr);
		if (odr != req_odr)
			return 0;

		return st_lsm6dsv16bx_set_hw_sensor_odr(hw, ST_LSM6DSV16BX_ID_GYRO,
							req_odr, req_uodr);
	default:
		break;
	}

	return 0;
}

int
st_lsm6dsv16bx_sensor_set_enable(struct st_lsm6dsv16bx_sensor *sensor,
				 bool enable)
{
	int uodr = enable ? sensor->uodr : 0;
	int odr = enable ? sensor->odr : 0;
	int err;

	err = st_lsm6dsv16bx_set_odr(sensor, odr, uodr);
	if (err < 0)
		return err;

	if (enable)
		sensor->hw->enable_mask |= BIT(sensor->id);
	else
		sensor->hw->enable_mask &= ~BIT(sensor->id);

	return 0;
}

int st_lsm6dsv16bx_sflp_set_enable(struct st_lsm6dsv16bx_sensor *sensor,
				   bool enable)
{
	struct st_lsm6dsv16bx_hw *hw = sensor->hw;
	int err;

	//if (sensor->id != ST_LSM6DSV16BX_ID_6X_GAME)
	//	return -EINVAL;

	mutex_lock(&hw->page_lock);
	err = st_lsm6dsv16bx_set_page_access(hw,
					ST_LSM6DSV16BX_EMB_FUNC_REG_ACCESS_MASK,
					1);
	if (err < 0)
		goto unlock;

	err = __st_lsm6dsv16bx_write_with_mask(hw,
					ST_LSM6DSV16BX_REG_EMB_FUNC_INIT_A_ADDR,
					ST_LSM6DSV16BX_SFLP_GAME_INIT_MASK,
					enable ? 1 : 0);
	if (err < 0)
		goto reset_page;

	err = __st_lsm6dsv16bx_write_with_mask(hw,
					  ST_LSM6DSV16BX_REG_EMB_FUNC_EN_A_ADDR,
					  ST_LSM6DSV16BX_SFLP_GAME_EN_MASK,
					  enable ? 1 : 0);
	if (err < 0)
		goto reset_page;

	err = __st_lsm6dsv16bx_write_with_mask(hw,
				     ST_LSM6DSV16BX_REG_EMB_FUNC_FIFO_EN_A_ADDR,
				     ST_LSM6DSV16BX_SFLP_GAME_FIFO_EN,
				     enable ? 1 : 0);
	if (err < 0)
		goto reset_page;

	err = __st_lsm6dsv16bx_set_sensor_batching_odr(sensor, enable);

reset_page:
	st_lsm6dsv16bx_set_page_access(hw,
				       ST_LSM6DSV16BX_EMB_FUNC_REG_ACCESS_MASK,
				       0);
unlock:
	mutex_unlock(&hw->page_lock);

	return st_lsm6dsv16bx_sensor_set_enable(sensor, enable);
}

static int st_lsm6dsv16bx_enable_tdm(struct st_lsm6dsv16bx_sensor *sensor,
				     bool enable)
{
	return st_lsm6dsv16bx_write_with_mask(sensor->hw,
					      ST_LSM6DSV16BX_REG_CTRL1_ADDR,
					      ST_LSM6DSV16BX_OP_MODE_MASK,
					      enable ? 0x02 : 0);
}

static int
st_lsm6dsv16bx_read_oneshot(struct st_lsm6dsv16bx_sensor *sensor, u8 addr,
			    int *val)
{
	int err, delay;
	__le16 data;

	err = st_lsm6dsv16bx_sensor_set_enable(sensor, true);
	if (err < 0)
		return err;

	delay = 1000000 / sensor->odr;
	usleep_range(delay, 2 * delay);

	err = st_lsm6dsv16bx_read_locked(sensor->hw, addr,
					 (u8 *)&data, sizeof(data));
	if (err < 0)
		return err;

	st_lsm6dsv16bx_sensor_set_enable(sensor, false);

	*val = (s16)le16_to_cpu(data);

	return IIO_VAL_INT;
}

static int st_lsm6dsv16bx_write_raw_get_fmt(struct iio_dev *indio_dev,
					    struct iio_chan_spec const *chan,
					    long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_ANGL_VEL:
		case IIO_ACCEL:
			return IIO_VAL_INT_PLUS_NANO;
		case IIO_TEMP:
			return IIO_VAL_FRACTIONAL;
		default:
			return IIO_VAL_INT_PLUS_MICRO;
		}
	default:
		return IIO_VAL_INT_PLUS_MICRO;
	}

	return -EINVAL;
}

static int st_lsm6dsv16bx_read_raw(struct iio_dev *iio_dev,
				   struct iio_chan_spec const *ch,
				   int *val, int *val2, long mask)
{
	struct st_lsm6dsv16bx_sensor *sensor = iio_priv(iio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(iio_dev);
		if (ret)
			return ret;

		ret = st_lsm6dsv16bx_read_oneshot(sensor, ch->address, val);
		iio_device_release_direct_mode(iio_dev);
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = (int)sensor->odr;
		*val2 = (int)sensor->uodr;
		ret = IIO_VAL_INT_PLUS_MICRO;
		break;
	case IIO_CHAN_INFO_SCALE:
		switch (ch->type) {
		case IIO_TEMP:
			*val = 1000;
			*val2 = ST_LSM6DSV16BX_TEMP_GAIN;
			ret = IIO_VAL_FRACTIONAL;
			break;
		case IIO_ACCEL:
		case IIO_ANGL_VEL:
			*val = 0;
			*val2 = sensor->gain;
			ret = IIO_VAL_INT_PLUS_NANO;
			break;
		default:
			return -EINVAL;
		}
		break;
	case IIO_CHAN_INFO_OFFSET:
		switch (ch->type) {
		case IIO_TEMP:
			*val = sensor->offset;
			ret = IIO_VAL_INT;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int st_lsm6dsv16bx_write_raw(struct iio_dev *iio_dev,
				    struct iio_chan_spec const *chan,
				    int val, int val2, long mask)
{
	struct st_lsm6dsv16bx_sensor *sensor = iio_priv(iio_dev);
	int err;

	mutex_lock(&iio_dev->mlock);

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		err = st_lsm6dsv16bx_set_full_scale(sensor, val2);
		break;
	case IIO_CHAN_INFO_SAMP_FREQ: {
		int todr, tuodr;
		u8 data;

		err = st_lsm6dsv16bx_get_odr_val(sensor->id, val, val2,
					      &todr, &tuodr, &data);
		if (!err) {
			sensor->odr = val;
			sensor->uodr = tuodr;

			/*
			 * VTS test testSamplingRateHotSwitchOperation
			 * not toggle the enable status of sensor after
			 * changing the ODR -> force it
			 */
			if (sensor->hw->enable_mask & BIT(sensor->id)) {
				switch (sensor->id) {
				case ST_LSM6DSV16BX_ID_GYRO:
				case ST_LSM6DSV16BX_ID_ACC:
					err = st_lsm6dsv16bx_set_odr(sensor,
								  sensor->odr,
								  sensor->uodr);
					if (err < 0)
						break;

					err = st_lsm6dsv16bx_update_batching(iio_dev, 1);
				default:
					break;
				}
			}
		}
		break;
	}
	default:
		err = -EINVAL;
		break;
	}

	mutex_unlock(&iio_dev->mlock);

	return err < 0 ? err : 0;
}

static ssize_t
st_lsm6dsv16bx_sysfs_sampling_frequency_avail(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	struct st_lsm6dsv16bx_sensor *sensor = iio_priv(dev_get_drvdata(dev));
	enum st_lsm6dsv16bx_sensor_id id = sensor->id;
	int i, len = 0;

	for (i = 0; i < st_lsm6dsv16bx_odr_table[id].size; i++) {
		if (!st_lsm6dsv16bx_odr_table[id].odr_avl[i].hz)
			continue;

		len += scnprintf(buf + len, PAGE_SIZE - len, "%d.%06d ",
				 st_lsm6dsv16bx_odr_table[id].odr_avl[i].hz,
				 st_lsm6dsv16bx_odr_table[id].odr_avl[i].uhz);
	}

	buf[len - 1] = '\n';

	return len;
}

static ssize_t
st_lsm6dsv16bx_sysfs_scale_avail(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct st_lsm6dsv16bx_sensor *sensor = iio_priv(dev_get_drvdata(dev));
	enum st_lsm6dsv16bx_sensor_id id = sensor->id;
	struct st_lsm6dsv16bx_hw *hw = sensor->hw;
	int i, len = 0;

	for (i = 0; i < hw->fs_table[id].size; i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "0.%09u ",
				 hw->fs_table[id].fs_avl[i].gain);
	buf[len - 1] = '\n';

	return len;
}

static __maybe_unused int st_lsm6dsv16bx_reg_access(struct iio_dev *iio_dev,
						    unsigned int reg,
						    unsigned int writeval,
						    unsigned int *readval)
{
	struct st_lsm6dsv16bx_sensor *sensor = iio_priv(iio_dev);
	int ret;

	ret = iio_device_claim_direct_mode(iio_dev);
	if (ret)
		return ret;

	if (readval == NULL)
		ret = regmap_write(sensor->hw->regmap, reg, writeval);
	else
		ret = regmap_read(sensor->hw->regmap, reg, readval);

	iio_device_release_direct_mode(iio_dev);

	return (ret < 0) ? ret : 0;
}

static int st_lsm6dsv16bx_of_get_pin(struct st_lsm6dsv16bx_hw *hw, int *pin)
{
	struct device_node *np = hw->dev->of_node;

	if (!np)
		return -EINVAL;

	return of_property_read_u32(np, "st,int-pin", pin);
}

static int st_lsm6dsv16bx_get_int_reg(struct st_lsm6dsv16bx_hw *hw,
				      u8 *drdy_reg, u8 *ef_irq_reg)
{
	int int_pin;

	if (st_lsm6dsv16bx_of_get_pin(hw, &int_pin) < 0) {
		struct st_sensors_platform_data *pdata;
		struct device *dev = hw->dev;

		pdata = (struct st_sensors_platform_data *)dev->platform_data;
		int_pin = pdata ? pdata->drdy_int_pin : 1;
	}

	switch (int_pin) {
	case 1:
		*drdy_reg = ST_LSM6DSV16BX_REG_INT1_CTRL_ADDR;
		break;
	case 2:
		*drdy_reg = ST_LSM6DSV16BX_REG_INT2_CTRL_ADDR;
		break;
	default:
		dev_err(hw->dev, "unsupported interrupt pin\n");

		return -EINVAL;
	}

	hw->int_pin = int_pin;

	return 0;
}

static int
st_lsm6dsv16bx_set_selftest(struct st_lsm6dsv16bx_sensor *sensor, int index)
{
	u8 mask;

	switch (sensor->id) {
	case ST_LSM6DSV16BX_ID_ACC:
		mask = ST_LSM6DSV16BX_ST_XL_MASK;
		break;
	case ST_LSM6DSV16BX_ID_GYRO:
		mask = ST_LSM6DSV16BX_ST_G_MASK;
		break;
	default:
		return -EINVAL;
	}

	return st_lsm6dsv16bx_write_with_mask(sensor->hw,
				     ST_LSM6DSV16BX_REG_CTRL10_ADDR, mask,
				     st_lsm6dsv16bx_selftest_table[index].value);
}

static ssize_t st_lsm6dsv16bx_sysfs_get_selftest_available(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s, %s\n",
		       st_lsm6dsv16bx_selftest_table[1].smode,
		       st_lsm6dsv16bx_selftest_table[2].smode);
}

static ssize_t
st_lsm6dsv16bx_sysfs_get_selftest_status(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct st_lsm6dsv16bx_sensor *sensor = iio_priv(dev_get_drvdata(dev));
	enum st_lsm6dsv16bx_sensor_id id = sensor->id;
	int8_t result;
	char *message;

	if (id != ST_LSM6DSV16BX_ID_ACC &&
	    id != ST_LSM6DSV16BX_ID_GYRO)
		return -EINVAL;

	result = sensor->selftest_status;
	if (result == 0)
		message = "na";
	else if (result < 0)
		message = "fail";
	else if (result > 0)
		message = "pass";

	return sprintf(buf, "%s\n", message);
}

static int st_lsm6dsv16bx_selftest_sensor(struct st_lsm6dsv16bx_sensor *sensor,
					  int test)
{
	int x_selftest = 0, y_selftest = 0, z_selftest = 0;
	int x = 0, y = 0, z = 0, try_count = 0;
	u8 i, status, n = 0;
	u8 reg, bitmask;
	int ret, delay;
	u8 raw_data[6];

	switch (sensor->id) {
	case ST_LSM6DSV16BX_ID_ACC:
		reg = ST_LSM6DSV16BX_REG_OUTX_L_A_ADDR;
		bitmask = ST_LSM6DSV16BX_XLDA_MASK;
		break;
	case ST_LSM6DSV16BX_ID_GYRO:
		reg = ST_LSM6DSV16BX_REG_OUTX_L_G_ADDR;
		bitmask = ST_LSM6DSV16BX_GDA_MASK;
		break;
	default:
		return -EINVAL;
	}

	/* set selftest normal mode */
	ret = st_lsm6dsv16bx_set_selftest(sensor, 0);
	if (ret < 0)
		return ret;

	ret = st_lsm6dsv16bx_sensor_set_enable(sensor, true);
	if (ret < 0)
		return ret;

	/* calculate delay time because self test is running in polling mode */
	delay = 1100000 / sensor->odr;

	/* power up, wait 100 ms for stable output */
	msleep(100);

	/* for 5 times, after checking status bit, read the output registers */
	for (i = 0; i < 5; i++) {
		try_count = 0;
		while (try_count < 3) {
			usleep_range(delay, delay + 1);
			ret = st_lsm6dsv16bx_read_locked(sensor->hw,
					     ST_LSM6DSV16BX_REG_STATUS_REG_ADDR,
					     &status, sizeof(status));
			if (ret < 0)
				goto selftest_failure;

			if (status & bitmask) {
				ret = st_lsm6dsv16bx_read_locked(sensor->hw,
							      reg, raw_data,
							      sizeof(raw_data));
				if (ret < 0)
					goto selftest_failure;

				/*
				 * for 5 times, after checking status bit,
				 * read the output registers
				 */
				x += ((s16)*(u16 *)&raw_data[0]) / 5;
				y += ((s16)*(u16 *)&raw_data[2]) / 5;
				z += ((s16)*(u16 *)&raw_data[4]) / 5;
				n++;
				break;
			}
			try_count++;
		}
	}

	if (i != n) {
		dev_err(sensor->hw->dev,
			"some samples missing (expected %d, read %d)\n",
			i, n);
		ret = -1;

		goto selftest_failure;
	}

	n = 0;

	/* set selftest mode */
	st_lsm6dsv16bx_set_selftest(sensor, test);

	/* wait 100 ms for stable output */
	msleep(100);

	/* for 5 times, after checking status bit, read the output registers */
	for (i = 0; i < 5; i++) {
		try_count = 0;
		while (try_count < 3) {
			usleep_range(delay, delay + 1);
			ret = st_lsm6dsv16bx_read_locked(sensor->hw,
					     ST_LSM6DSV16BX_REG_STATUS_REG_ADDR,
					     &status, sizeof(status));
			if (ret < 0)
				goto selftest_failure;

			if (status & bitmask) {
				ret = st_lsm6dsv16bx_read_locked(sensor->hw,
							      reg, raw_data,
							      sizeof(raw_data));
				if (ret < 0)
					goto selftest_failure;

				x_selftest += ((s16)*(u16 *)&raw_data[0]) / 5;
				y_selftest += ((s16)*(u16 *)&raw_data[2]) / 5;
				z_selftest += ((s16)*(u16 *)&raw_data[4]) / 5;
				n++;
				break;
			}
			try_count++;
		}
	}

	if (i != n) {
		dev_err(sensor->hw->dev,
			"some samples missing (expected %d, read %d)\n",
			i, n);
		ret = -1;

		goto selftest_failure;
	}

	if ((abs(x_selftest - x) < sensor->min_st) ||
	    (abs(x_selftest - x) > sensor->max_st)) {
		sensor->selftest_status = -1;
		goto selftest_failure;
	}

	if ((abs(y_selftest - y) < sensor->min_st) ||
	    (abs(y_selftest - y) > sensor->max_st)) {
		sensor->selftest_status = -1;
		goto selftest_failure;
	}

	if ((abs(z_selftest - z) < sensor->min_st) ||
	    (abs(z_selftest - z) > sensor->max_st)) {
		sensor->selftest_status = -1;
		goto selftest_failure;
	}

	sensor->selftest_status = 1;

selftest_failure:
	/* restore selftest to normal mode */
	st_lsm6dsv16bx_set_selftest(sensor, 0);

	return st_lsm6dsv16bx_sensor_set_enable(sensor, false);
}

static ssize_t st_lsm6dsv16bx_sysfs_start_selftest(struct device *dev,
						  struct device_attribute *attr,
						  const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_get_drvdata(dev);
	struct st_lsm6dsv16bx_sensor *sensor = iio_priv(iio_dev);
	enum st_lsm6dsv16bx_sensor_id id = sensor->id;
	struct st_lsm6dsv16bx_hw *hw = sensor->hw;
	u8 drdy_reg, ef_irq_reg;
	int ret, test;
	int odr, uodr;
	u32 gain;

	if (id != ST_LSM6DSV16BX_ID_ACC &&
	    id != ST_LSM6DSV16BX_ID_GYRO)
		return -EINVAL;

	for (test = 0; test < ARRAY_SIZE(st_lsm6dsv16bx_selftest_table); test++) {
		if (strncmp(buf, st_lsm6dsv16bx_selftest_table[test].smode,
			strlen(st_lsm6dsv16bx_selftest_table[test].smode)) == 0)
			break;
	}

	if (test == ARRAY_SIZE(st_lsm6dsv16bx_selftest_table))
		return -EINVAL;

	ret = iio_device_claim_direct_mode(iio_dev);
	if (ret)
		return ret;

	/* self test mode unavailable if sensor enabled */
	if (hw->enable_mask & BIT(id)) {
		ret = -EBUSY;

		goto out_claim;
	}

	/* disable interrupt on FIFO watermak */
	ret = st_lsm6dsv16bx_get_int_reg(hw, &drdy_reg, &ef_irq_reg);
	if (ret < 0)
		goto restore_regs;

	ret = st_lsm6dsv16bx_write_with_mask(hw, drdy_reg,
					     ST_LSM6DSV16BX_INT_FIFO_TH_MASK,
					     0);
	if (ret < 0)
		goto restore_regs;

	gain = sensor->gain;
	odr = sensor->odr;
	uodr = sensor->uodr;
	if (id == ST_LSM6DSV16BX_ID_ACC) {
		/* set BDU = 1, FS = 4 g, ODR = 60 Hz */
		st_lsm6dsv16bx_set_full_scale(sensor,
					      ST_LSM6DSV16BX_ACC_FS_4G_GAIN);
		st_lsm6dsv16bx_set_odr(sensor, 60, 0);
	} else {
		/* set BDU = 1, ODR = 240 Hz, FS = 2000 dps */
		st_lsm6dsv16bx_set_full_scale(sensor,
					      ST_LSM6DSV16BX_GYRO_FS_2000_GAIN);
		st_lsm6dsv16bx_set_odr(sensor, 240, 0);
	}

	/* run test */
	st_lsm6dsv16bx_selftest_sensor(sensor, test);

restore_regs:
	/* restore configuration after test */
	st_lsm6dsv16bx_set_full_scale(sensor, gain);
	st_lsm6dsv16bx_set_odr(sensor, odr, uodr);
	st_lsm6dsv16bx_write_with_mask(hw, drdy_reg,
				       ST_LSM6DSV16BX_INT_FIFO_TH_MASK, 1);

out_claim:
	iio_device_release_direct_mode(iio_dev);

	return size;
}

ssize_t st_lsm6dsv16bx_get_module_id(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct iio_dev *iio_dev = dev_get_drvdata(dev);
	struct st_lsm6dsv16bx_sensor *sensor = iio_priv(iio_dev);
	struct st_lsm6dsv16bx_hw *hw = sensor->hw;

	return scnprintf(buf, PAGE_SIZE, "%u\n", hw->module_id);
}

ssize_t st_lsm6dsv16bx_get_en_tdm(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	struct iio_dev *iio_dev = dev_get_drvdata(dev);
	struct st_lsm6dsv16bx_sensor *sensor = iio_priv(iio_dev);

	return sprintf(buf, "%d\n", sensor->hw->en_tdm);
}

ssize_t st_lsm6dsv16bx_set_en_tdm(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_get_drvdata(dev);
	struct st_lsm6dsv16bx_sensor *sensor = iio_priv(iio_dev);
	int err, val;

	err = iio_device_claim_direct_mode(iio_dev);
	if (err)
		return err;

	err = kstrtoint(buf, 10, &val);
	if (err < 0)
		goto out;

	err = st_lsm6dsv16bx_enable_tdm(sensor, val > 0 ? true : false);
	if (err < 0)
		goto out;

	sensor->hw->en_tdm = val > 0 ? true : false;

out:
	iio_device_release_direct_mode(iio_dev);

	return err < 0 ? err : size;
}

static IIO_DEV_ATTR_SAMP_FREQ_AVAIL(st_lsm6dsv16bx_sysfs_sampling_frequency_avail);
static IIO_DEVICE_ATTR(in_accel_scale_available, 0444,
		       st_lsm6dsv16bx_sysfs_scale_avail, NULL, 0);
static IIO_DEVICE_ATTR(in_anglvel_scale_available, 0444,
		       st_lsm6dsv16bx_sysfs_scale_avail, NULL, 0);
static IIO_DEVICE_ATTR(in_temp_scale_available, 0444,
		       st_lsm6dsv16bx_sysfs_scale_avail, NULL, 0);
static IIO_DEVICE_ATTR(hwfifo_watermark_max, 0444,
		       st_lsm6dsv16bx_get_max_watermark, NULL, 0);
static IIO_DEVICE_ATTR(hwfifo_flush, 0200, NULL,
		       st_lsm6dsv16bx_flush_fifo, 0);
static IIO_DEVICE_ATTR(hwfifo_watermark, 0644,
		       st_lsm6dsv16bx_get_watermark,
		       st_lsm6dsv16bx_set_watermark, 0);
static IIO_DEVICE_ATTR(selftest_available, 0444,
		       st_lsm6dsv16bx_sysfs_get_selftest_available,
		       NULL, 0);
static IIO_DEVICE_ATTR(selftest, 0644,
		       st_lsm6dsv16bx_sysfs_get_selftest_status,
		       st_lsm6dsv16bx_sysfs_start_selftest, 0);
static IIO_DEVICE_ATTR(module_id, 0444, st_lsm6dsv16bx_get_module_id, NULL, 0);
static IIO_DEVICE_ATTR(en_tdm, 0644,
		       st_lsm6dsv16bx_get_en_tdm,
		       st_lsm6dsv16bx_set_en_tdm, 0);

static struct attribute *st_lsm6dsv16bx_acc_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_accel_scale_available.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark_max.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark.dev_attr.attr,
	&iio_dev_attr_hwfifo_flush.dev_attr.attr,
	&iio_dev_attr_selftest_available.dev_attr.attr,
	&iio_dev_attr_selftest.dev_attr.attr,
	&iio_dev_attr_module_id.dev_attr.attr,
	&iio_dev_attr_en_tdm.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lsm6dsv16bx_acc_attribute_group = {
	.attrs = st_lsm6dsv16bx_acc_attributes,
};

static const struct iio_info st_lsm6dsv16bx_acc_info = {
	.attrs = &st_lsm6dsv16bx_acc_attribute_group,
	.read_raw = st_lsm6dsv16bx_read_raw,
	.write_raw_get_fmt = st_lsm6dsv16bx_write_raw_get_fmt,
	.write_raw = st_lsm6dsv16bx_write_raw,
	.debugfs_reg_access = st_lsm6dsv16bx_reg_access,
};

static struct attribute *st_lsm6dsv16bx_gyro_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_anglvel_scale_available.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark_max.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark.dev_attr.attr,
	&iio_dev_attr_hwfifo_flush.dev_attr.attr,
	&iio_dev_attr_selftest_available.dev_attr.attr,
	&iio_dev_attr_selftest.dev_attr.attr,
	&iio_dev_attr_module_id.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lsm6dsv16bx_gyro_attribute_group = {
	.attrs = st_lsm6dsv16bx_gyro_attributes,
};

static const struct iio_info st_lsm6dsv16bx_gyro_info = {
	.attrs = &st_lsm6dsv16bx_gyro_attribute_group,
	.read_raw = st_lsm6dsv16bx_read_raw,
	.write_raw_get_fmt = st_lsm6dsv16bx_write_raw_get_fmt,
	.write_raw = st_lsm6dsv16bx_write_raw,
};

static struct attribute *st_lsm6dsv16bx_temp_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_temp_scale_available.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark_max.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark.dev_attr.attr,
	&iio_dev_attr_hwfifo_flush.dev_attr.attr,
	&iio_dev_attr_module_id.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lsm6dsv16bx_temp_attribute_group = {
	.attrs = st_lsm6dsv16bx_temp_attributes,
};

static const struct iio_info st_lsm6dsv16bx_temp_info = {
	.attrs = &st_lsm6dsv16bx_temp_attribute_group,
	.read_raw = st_lsm6dsv16bx_read_raw,
	.write_raw_get_fmt = st_lsm6dsv16bx_write_raw_get_fmt,
	.write_raw = st_lsm6dsv16bx_write_raw,
};

static struct attribute *st_lsm6dsv16bx_sflp_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark_max.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark.dev_attr.attr,
	&iio_dev_attr_hwfifo_flush.dev_attr.attr,
	&iio_dev_attr_module_id.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lsm6dsv16bx_sflp_attribute_group = {
	.attrs = st_lsm6dsv16bx_sflp_attributes,
};

static const struct iio_info st_lsm6dsv16bx_sflp_info = {
	.attrs = &st_lsm6dsv16bx_sflp_attribute_group,
	.read_raw = st_lsm6dsv16bx_read_raw,
	.write_raw = st_lsm6dsv16bx_write_raw,
};

static const unsigned long st_lsm6dsv16bx_available_scan_masks[] = {
	GENMASK(3, 0), 0x0
};

static const unsigned long st_lsm6dsv16bx_temp_available_scan_masks[] = {
	BIT(0), 0x0
};

static int st_lsm6dsv16bx_reset_device(struct st_lsm6dsv16bx_hw *hw)
{
	int err;

	/* sw reset */
	err = st_lsm6dsv16bx_write_with_mask(hw, ST_LSM6DSV16BX_REG_CTRL3_ADDR,
					     ST_LSM6DSV16BX_SW_RESET_MASK,
					     1);
	if (err < 0)
		return err;

	msleep(10);

	/* boot */
	err = st_lsm6dsv16bx_write_with_mask(hw, ST_LSM6DSV16BX_REG_CTRL3_ADDR,
					     ST_LSM6DSV16BX_BOOT_MASK, 1);

	msleep(50);

	return err;
}

/* from AN5845 */
static int st_lsm6dsv16bx_sflp_init_device(struct st_lsm6dsv16bx_hw *hw)
{
	int err;

	err = st_lsm6dsv16bx_set_page_access(hw,
					ST_LSM6DSV16BX_EMB_FUNC_REG_ACCESS_MASK,
					1);
	if (err < 0)
		return err;

	err = __st_lsm6dsv16bx_write_with_mask(hw,
					       ST_LSM6DSV16BX_REG_PAGE_RW_ADDR,
					       ST_LSM6DSV16BX_PAGE_WRITE_MASK,
					       1);
	if (err < 0)
		return err;

	err = regmap_write(hw->regmap, ST_LSM6DSV16BX_REG_PAGE_ADDRESS_ADDR, 0xd2);
	if (err < 0)
		return err;

	err = regmap_write(hw->regmap, ST_LSM6DSV16BX_REG_PAGE_VALUE_ADDR, 0x50);
	if (err < 0)
		return err;

	err = __st_lsm6dsv16bx_write_with_mask(hw,
					       ST_LSM6DSV16BX_REG_PAGE_RW_ADDR,
					       ST_LSM6DSV16BX_PAGE_WRITE_MASK,
					       0);
	if (err < 0)
		return err;

	return st_lsm6dsv16bx_set_page_access(hw,
					ST_LSM6DSV16BX_EMB_FUNC_REG_ACCESS_MASK,
					0);
}

/* from AN5845 */
static int st_lsm6dsv16bx_tdm_init_device(struct st_lsm6dsv16bx_hw *hw)
{
	int tdm_wclk, tdm_slot_sel, tdm_ord_sel, tdm_fs_xl, tdm_wclk_bclk_sel;
	struct device_node *np = hw->dev->of_node;
	int err;

	if (!np)
		return -EINVAL;

	err = of_property_read_u32(np, "tdm_wclk", &tdm_wclk);
	if (err < 0) {
		/* use default wclk 8 kHz */
		tdm_wclk = 0x01;
		tdm_wclk_bclk_sel = 0;
	}

	switch (tdm_wclk) {
	case 0:
		tdm_wclk_bclk_sel = 1;
		break;
	case 1:
		tdm_wclk_bclk_sel = 0;
		break;
	default:
		dev_err(hw->dev, "invalid tdm_wclk value %d\n", tdm_wclk);

		return -EINVAL;
	}

	err = of_property_read_u32(np, "tdm_slot_sel", &tdm_slot_sel);
	if (err < 0)
		/* use default slot {0, 1, 2} */
		tdm_slot_sel = 0x0;

	if (tdm_slot_sel < 0 || tdm_slot_sel > 1) {
		dev_err(hw->dev, "invalid tdm_slot_sel value %d\n", tdm_slot_sel);

		return -EINVAL;
	}

	err = of_property_read_u32(np, "tdm_ord_sel", &tdm_ord_sel);
	if (err < 0)
		/* use default X, Y, Z order */
		tdm_ord_sel = 0x02;

	if (tdm_ord_sel < 0 || tdm_ord_sel > 2) {
		dev_err(hw->dev, "invalid tdm_ord_sel value %d\n", tdm_ord_sel);

		return -EINVAL;
	}

	err = of_property_read_u32(np, "tdm_fs_xl", &tdm_fs_xl);
	if (err < 0)
		/* use default ±4 g */
		tdm_fs_xl = 0x01;

	if (tdm_fs_xl < 0 || tdm_fs_xl > 2) {
		dev_err(hw->dev, "invalid tdm_fs_xl value %d\n", tdm_fs_xl);

		return -EINVAL;
	}

	err = st_lsm6dsv16bx_write_with_mask(hw,
					     ST_LSM6DSV16BX_REG_TDM_CFG0_ADDR,
					     ST_LSM6DSV16BX_REG_TDM_WCLK_MASK,
					     tdm_wclk);
	if (err < 0)
		return err;

	err = st_lsm6dsv16bx_write_with_mask(hw,
					     ST_LSM6DSV16BX_REG_TDM_CFG0_ADDR,
					     ST_LSM6DSV16BX_REG_TDM_WCLK_BCLK_SEL_MASK,
					     tdm_wclk_bclk_sel);
	if (err < 0)
		return err;

	err = st_lsm6dsv16bx_write_with_mask(hw,
					     ST_LSM6DSV16BX_REG_TDM_CFG0_ADDR,
					     ST_LSM6DSV16BX_REG_TDM_SLOT_SEL_MASK,
					     tdm_slot_sel);
	if (err < 0)
		return err;

	err = st_lsm6dsv16bx_write_with_mask(hw,
					     ST_LSM6DSV16BX_REG_TDM_CFG1_ADDR,
					     ST_LSM6DSV16BX_REG_TDM_AXES_ORD_SEL_MASK,
					     tdm_ord_sel);
	if (err < 0)
		return err;

	return st_lsm6dsv16bx_write_with_mask(hw,
					     ST_LSM6DSV16BX_REG_TDM_CFG2_ADDR,
					     ST_LSM6DSV16BX_REG_TDM_FS_XL_MASK,
					     tdm_fs_xl);
}

static int st_lsm6dsv16bx_init_device(struct st_lsm6dsv16bx_hw *hw)
{
	u8 drdy_reg, ef_irq_reg;
	int err;

	/* latch interrupts */
	err = st_lsm6dsv16bx_write_with_mask(hw, ST_LSM6DSV16BX_REG_TAP_CFG0_ADDR,
					     ST_LSM6DSV16BX_LIR_MASK, 1);
	if (err < 0)
		return err;

	/* enable Block Data Update */
	err = st_lsm6dsv16bx_write_with_mask(hw, ST_LSM6DSV16BX_REG_CTRL3_ADDR,
					     ST_LSM6DSV16BX_BDU_MASK, 1);
	if (err < 0)
		return err;

	/* init timestamp engine */
	err = st_lsm6dsv16bx_write_with_mask(hw,
				       ST_LSM6DSV16BX_REG_FUNCTIONS_ENABLE_ADDR,
				       ST_LSM6DSV16BX_TIMESTAMP_EN_MASK, 1);
	if (err < 0)
		return err;

	err = st_lsm6dsv16bx_get_int_reg(hw, &drdy_reg, &ef_irq_reg);
	if (err < 0)
		return err;

	/* enable DRDY MASK for filters settling time */
	err = st_lsm6dsv16bx_write_with_mask(hw, ST_LSM6DSV16BX_REG_CTRL4_ADDR,
					     ST_LSM6DSV16BX_DRDY_MASK, 1);
	if (err < 0)
		return err;

	/* check for sflp support before init */
	if (hw->settings->st_sflp_probe) {
		err = st_lsm6dsv16bx_sflp_init_device(hw);
		if (err < 0)
			return err;
	}

	if (hw->settings->st_tdm_probe) {
		err = st_lsm6dsv16bx_tdm_init_device(hw);
		if (err < 0)
			return err;
	}

	/* enable FIFO watermak interrupt */
	return st_lsm6dsv16bx_write_with_mask(hw, drdy_reg,
					     ST_LSM6DSV16BX_INT_FIFO_TH_MASK,
					     1);
}

static struct iio_dev *
st_lsm6dsv16bx_alloc_iiodev(struct st_lsm6dsv16bx_hw *hw,
			    enum st_lsm6dsv16bx_sensor_id id)
{
	struct st_lsm6dsv16bx_sensor *sensor;
	struct iio_dev *iio_dev;

	iio_dev = devm_iio_device_alloc(hw->dev, sizeof(*sensor));
	if (!iio_dev)
		return NULL;

	iio_dev->modes = INDIO_DIRECT_MODE;
	iio_dev->dev.parent = hw->dev;

	sensor = iio_priv(iio_dev);
	sensor->id = id;
	sensor->hw = hw;
	sensor->watermark = 1;

	sensor->decimator = 0;
	sensor->dec_counter = 0;
	sensor->last_fifo_timestamp = 0;

	switch (id) {
	case ST_LSM6DSV16BX_ID_ACC:
		iio_dev->channels = st_lsm6dsv16bx_acc_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_lsm6dsv16bx_acc_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			 "%s_accel", hw->settings->id.name);
		iio_dev->info = &st_lsm6dsv16bx_acc_info;
		iio_dev->available_scan_masks = st_lsm6dsv16bx_available_scan_masks;

		sensor->batch_reg.addr = ST_LSM6DSV16BX_REG_FIFO_CTRL3_ADDR;
		sensor->batch_reg.mask = ST_LSM6DSV16BX_BDR_XL_MASK;
		sensor->max_watermark = ST_LSM6DSV16BX_MAX_FIFO_DEPTH;
		sensor->odr = st_lsm6dsv16bx_odr_table[id].odr_avl[1].hz;
		sensor->uodr = st_lsm6dsv16bx_odr_table[id].odr_avl[1].uhz;
		sensor->gain = hw->fs_table[id].fs_avl[0].gain;
		sensor->min_st = ST_LSM6DSV16BX_SELFTEST_ACCEL_MIN;
		sensor->max_st = ST_LSM6DSV16BX_SELFTEST_ACCEL_MAX;
		break;
	case ST_LSM6DSV16BX_ID_GYRO:
		iio_dev->channels = st_lsm6dsv16bx_gyro_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_lsm6dsv16bx_gyro_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			 "%s_gyro", hw->settings->id.name);
		iio_dev->info = &st_lsm6dsv16bx_gyro_info;
		iio_dev->available_scan_masks = st_lsm6dsv16bx_available_scan_masks;

		sensor->batch_reg.addr = ST_LSM6DSV16BX_REG_FIFO_CTRL3_ADDR;
		sensor->batch_reg.mask = ST_LSM6DSV16BX_BDR_GY_MASK;
		sensor->max_watermark = ST_LSM6DSV16BX_MAX_FIFO_DEPTH;
		sensor->odr = st_lsm6dsv16bx_odr_table[id].odr_avl[1].hz;
		sensor->uodr = st_lsm6dsv16bx_odr_table[id].odr_avl[1].uhz;
		sensor->gain = hw->fs_table[id].fs_avl[1].gain;
		sensor->min_st = ST_LSM6DSV16BX_SELFTEST_GYRO_MIN;
		sensor->max_st = ST_LSM6DSV16BX_SELFTEST_GYRO_MAX;
		break;
	case ST_LSM6DSV16BX_ID_TEMP:
		iio_dev->channels = st_lsm6dsv16bx_temp_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_lsm6dsv16bx_temp_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			 "%s_temp", hw->settings->id.name);
		iio_dev->info = &st_lsm6dsv16bx_temp_info;
		iio_dev->available_scan_masks =
					  st_lsm6dsv16bx_temp_available_scan_masks;

		sensor->batch_reg.addr = ST_LSM6DSV16BX_REG_FIFO_CTRL4_ADDR;
		sensor->batch_reg.mask = ST_LSM6DSV16BX_ODR_T_BATCH_MASK;
		sensor->max_watermark = ST_LSM6DSV16BX_MAX_FIFO_DEPTH;
		sensor->odr = st_lsm6dsv16bx_odr_table[id].odr_avl[1].hz;
		sensor->uodr = st_lsm6dsv16bx_odr_table[id].odr_avl[1].uhz;
		sensor->gain = hw->fs_table[id].fs_avl[1].gain;
		sensor->offset = ST_LSM6DSV16BX_TEMP_OFFSET;
		break;
	case ST_LSM6DSV16BX_ID_6X_GAME:
		iio_dev->channels = st_lsm6dsv16bx_sflp_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_lsm6dsv16bx_sflp_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			 "%s_gamerot", hw->settings->id.name);
		iio_dev->info = &st_lsm6dsv16bx_sflp_info;
		iio_dev->available_scan_masks = st_lsm6dsv16bx_available_scan_masks;

		sensor->batch_reg.addr = ST_LSM6DSV16BX_REG_SFLP_ODR_ADDR;
		sensor->batch_reg.mask = ST_LSM6DSV16BX_SFLP_GAME_ODR_MASK;
		sensor->max_watermark = ST_LSM6DSV16BX_MAX_FIFO_DEPTH;
		sensor->odr = st_lsm6dsv16bx_odr_table[id].odr_avl[3].hz;
		sensor->uodr = st_lsm6dsv16bx_odr_table[id].odr_avl[3].uhz;
		sensor->gain = 1;
		break;
	default:
		return NULL;
	}

	iio_dev->name = sensor->name;
	st_lsm6dsv16bx_set_full_scale(sensor, sensor->gain);

	return iio_dev;
}

static void st_lsm6dsv16bx_disable_regulator_action(void *_data)
{
	struct st_lsm6dsv16bx_hw *hw = _data;

	regulator_disable(hw->vddio_supply);
	regulator_disable(hw->vdd_supply);
}

static void st_lsm6dsv16bx_get_properties(struct st_lsm6dsv16bx_hw *hw)
{
	if (device_property_read_u32(hw->dev, "st,module_id",
				     &hw->module_id)) {
		hw->module_id = 1;
	}
}

static int st_lsm6dsv16bx_power_enable(struct st_lsm6dsv16bx_hw *hw)
{
	int err;

	hw->vdd_supply = devm_regulator_get(hw->dev, "vdd");
	if (IS_ERR(hw->vdd_supply)) {
		if (PTR_ERR(hw->vdd_supply) != -EPROBE_DEFER)
			dev_err(hw->dev, "Failed to get vdd regulator %d\n",
				(int)PTR_ERR(hw->vdd_supply));

		return PTR_ERR(hw->vdd_supply);
	}

	hw->vddio_supply = devm_regulator_get(hw->dev, "vddio");
	if (IS_ERR(hw->vddio_supply)) {
		if (PTR_ERR(hw->vddio_supply) != -EPROBE_DEFER)
			dev_err(hw->dev, "Failed to get vddio regulator %d\n",
				(int)PTR_ERR(hw->vddio_supply));

		return PTR_ERR(hw->vddio_supply);
	}

	err = regulator_enable(hw->vdd_supply);
	if (err) {
		dev_err(hw->dev, "Failed to enable vdd regulator: %d\n", err);

		return err;
	}

	err = regulator_enable(hw->vddio_supply);
	if (err) {
		regulator_disable(hw->vdd_supply);

		return err;
	}

	err = devm_add_action_or_reset(hw->dev,
				       st_lsm6dsv16bx_disable_regulator_action,
				       hw);
	if (err) {
		dev_err(hw->dev,
			"Failed to setup regulator cleanup action %d\n", err);

		return err;
	}

	return 0;
}

int st_lsm6dsv16bx_probe(struct device *dev, int irq, int hw_id,
			 struct regmap *regmap)
{
	struct st_lsm6dsv16bx_hw *hw;
	int i, err;

	hw = devm_kzalloc(dev, sizeof(*hw), GFP_KERNEL);
	if (!hw)
		return -ENOMEM;

	dev_set_drvdata(dev, (void *)hw);

	mutex_init(&hw->lock);
	mutex_init(&hw->fifo_lock);
	mutex_init(&hw->page_lock);

	hw->dev = dev;
	hw->irq = irq;
	hw->regmap = regmap;

	err = st_lsm6dsv16bx_power_enable(hw);
	if (err != 0)
		return err;

	/* select register bank zero */
	err = st_lsm6dsv16bx_set_page_access(hw,
				    ST_LSM6DSV16BX_EMB_FUNC_REG_ACCESS_MASK, 0);
	if (err < 0)
		return err;

	err = st_lsm6dsv16bx_check_whoami(hw, hw_id);
	if (err < 0)
		return err;

	st_lsm6dsv16bx_get_properties(hw);

	err = st_lsm6dsv16bx_get_odr_calibration(hw);
	if (err < 0)
		return err;

	err = st_lsm6dsv16bx_reset_device(hw);
	if (err < 0)
		return err;

	err = st_lsm6dsv16bx_init_device(hw);
	if (err < 0)
		return err;

#if KERNEL_VERSION(5, 15, 0) <= LINUX_VERSION_CODE
	err = iio_read_mount_matrix(dev, &hw->orientation);
#elif KERNEL_VERSION(5, 2, 0) <= LINUX_VERSION_CODE
	err = iio_read_mount_matrix(dev, "mount-matrix", &hw->orientation);
#else /* LINUX_VERSION_CODE */
	err = of_iio_read_mount_matrix(dev, "mount-matrix", &hw->orientation);
#endif /* LINUX_VERSION_CODE */

	if (err) {
		dev_err(dev, "Failed to retrieve mounting matrix %d\n", err);
		return err;
	}

	for (i = 0; i < ARRAY_SIZE(st_lsm6dsv16bx_main_sensor_list); i++) {
		enum st_lsm6dsv16bx_sensor_id id = st_lsm6dsv16bx_main_sensor_list[i];

		/* don't probe if sflp not supported */
		if (!hw->settings->st_sflp_probe &&
		    (id == ST_LSM6DSV16BX_ID_6X_GAME))
			continue;

		hw->iio_devs[id] = st_lsm6dsv16bx_alloc_iiodev(hw, id);
		if (!hw->iio_devs[id])
			return -ENOMEM;
	}

	/* allocate step counter before buffer setup because use FIFO */
	err = st_lsm6dsv16bx_probe_embfunc(hw);
	if (err < 0)
		return err;

	err = st_lsm6dsv16bx_probe_event(hw);
	if (err < 0)
		return err;

	if (hw->settings->st_qvar_probe &&
	    (!dev_fwnode(dev) ||
	     device_property_read_bool(dev, "enable-qvar"))) {
		err = st_lsm6dsv16bx_qvar_probe(hw);
		if (err)
			return err;
	}

	if (hw->irq > 0) {
		err = st_lsm6dsv16bx_buffers_setup(hw);
		if (err < 0)
			return err;
	}

	if (st_lsm6dsv16bx_run_mlc_task(hw)) {
		err = st_lsm6dsv16bx_mlc_probe(hw);
		if (err < 0)
			return err;
	}

	for (i = 0; i < ST_LSM6DSV16BX_ID_MAX; i++) {
		if (!hw->iio_devs[i])
			continue;

		err = devm_iio_device_register(hw->dev, hw->iio_devs[i]);
		if (err)
			return err;
	}

	if (st_lsm6dsv16bx_run_mlc_task(hw)) {
		err = st_lsm6dsv16bx_mlc_init_preload(hw);
		if (err)
			return err;
	}

	device_init_wakeup(dev,
			   device_property_read_bool(dev, "wakeup-source"));

	return 0;
}
EXPORT_SYMBOL(st_lsm6dsv16bx_probe);

static int __maybe_unused st_lsm6dsv16bx_suspend(struct device *dev)
{
	struct st_lsm6dsv16bx_hw *hw = dev_get_drvdata(dev);
	struct st_lsm6dsv16bx_sensor *sensor;
	int i, err = 0;

	for (i = 0; i < ST_LSM6DSV16BX_ID_MAX; i++) {
		if (!hw->iio_devs[i])
			continue;

		sensor = iio_priv(hw->iio_devs[i]);
		if (!(hw->enable_mask & BIT(sensor->id)))
			continue;

		err = st_lsm6dsv16bx_set_odr(sensor, 0, 0);
		if (err < 0)
			return err;
	}

	if (st_lsm6dsv16bx_is_fifo_enabled(hw))
		err = st_lsm6dsv16bx_suspend_fifo(hw);

	if (device_may_wakeup(dev))
		enable_irq_wake(hw->irq);

	dev_info(dev, "Suspending device\n");

	return err < 0 ? err : 0;
}

static int __maybe_unused st_lsm6dsv16bx_resume(struct device *dev)
{
	struct st_lsm6dsv16bx_hw *hw = dev_get_drvdata(dev);
	struct st_lsm6dsv16bx_sensor *sensor;
	int i, err = 0;

	dev_info(dev, "Resuming device\n");

	if (device_may_wakeup(dev))
		disable_irq_wake(hw->irq);

	for (i = 0; i < ST_LSM6DSV16BX_ID_MAX; i++) {
		if (!hw->iio_devs[i])
			continue;

		sensor = iio_priv(hw->iio_devs[i]);
		if (!(hw->enable_mask & BIT(sensor->id)))
			continue;

		err = st_lsm6dsv16bx_set_odr(sensor, sensor->odr, sensor->uodr);
		if (err < 0)
			return err;
	}

	if (st_lsm6dsv16bx_is_fifo_enabled(hw))
		err = st_lsm6dsv16bx_set_fifo_mode(hw,
						   ST_LSM6DSV16BX_FIFO_CONT);

	return err < 0 ? err : 0;
}

const struct dev_pm_ops st_lsm6dsv16bx_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(st_lsm6dsv16bx_suspend, st_lsm6dsv16bx_resume)
};
EXPORT_SYMBOL(st_lsm6dsv16bx_pm_ops);

MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_DESCRIPTION("STMicroelectronics st_lsm6dsv16bx driver");
MODULE_LICENSE("GPL v2");
