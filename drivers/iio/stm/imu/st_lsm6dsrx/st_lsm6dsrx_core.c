// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_lsm6dsrx sensor driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2021 STMicroelectronics Inc.
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/iio/buffer.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/version.h>

#include <linux/platform_data/st_sensors_pdata.h>

#include "st_lsm6dsrx.h"

static struct st_lsm6dsrx_selftest_table {
	char *string_mode;
	u8 accel_value;
	u8 gyro_value;
	u8 gyro_mask;
} st_lsm6dsrx_selftest_table[] = {
	[0] = {
		.string_mode = "disabled",
		.accel_value = ST_LSM6DSRX_SELF_TEST_DISABLED_VAL,
		.gyro_value = ST_LSM6DSRX_SELF_TEST_DISABLED_VAL,
	},
	[1] = {
		.string_mode = "positive-sign",
		.accel_value = ST_LSM6DSRX_SELF_TEST_POS_SIGN_VAL,
		.gyro_value = ST_LSM6DSRX_SELF_TEST_POS_SIGN_VAL
	},
	[2] = {
		.string_mode = "negative-sign",
		.accel_value = ST_LSM6DSRX_SELF_TEST_NEG_ACCEL_SIGN_VAL,
		.gyro_value = ST_LSM6DSRX_SELF_TEST_NEG_GYRO_SIGN_VAL
	},
};

static struct st_lsm6dsrx_power_mode_table {
	char *string_mode;
	enum st_lsm6dsrx_pm_t val;
} st_lsm6dsrx_power_mode[] = {
	[0] = {
		.string_mode = "HP_MODE",
		.val = ST_LSM6DSRX_HP_MODE,
	},
	[1] = {
		.string_mode = "LP_MODE",
		.val = ST_LSM6DSRX_LP_MODE,
	},
};

static struct st_lsm6dsrx_suspend_resume_entry
	st_lsm6dsrx_suspend_resume[ST_LSM6DSRX_SUSPEND_RESUME_REGS] = {
    [ST_LSM6DSRX_CTRL1_XL_REG] = {
		.page = FUNC_CFG_ACCESS_0,
		.addr = ST_LSM6DSRX_CTRL1_XL_ADDR,
		.mask = GENMASK(3, 2),
	},
	[ST_LSM6DSRX_CTRL2_G_REG] = {
		.page = FUNC_CFG_ACCESS_0,
		.addr = ST_LSM6DSRX_CTRL2_G_ADDR,
		.mask = GENMASK(3, 2),
	},
	[ST_LSM6DSRX_REG_CTRL3_C_REG] = {
		.page = FUNC_CFG_ACCESS_0,
		.addr = ST_LSM6DSRX_REG_CTRL3_C_ADDR,
		.mask = ST_LSM6DSRX_REG_BDU_MASK	|
			ST_LSM6DSRX_REG_PP_OD_MASK	|
			ST_LSM6DSRX_REG_H_LACTIVE_MASK,
	},
	[ST_LSM6DSRX_REG_CTRL4_C_REG] = {
		.page = FUNC_CFG_ACCESS_0,
		.addr = ST_LSM6DSRX_REG_CTRL4_C_ADDR,
		.mask = ST_LSM6DSRX_REG_DRDY_MASK,
	},
	[ST_LSM6DSRX_REG_CTRL5_C_REG] = {
		.page = FUNC_CFG_ACCESS_0,
		.addr = ST_LSM6DSRX_REG_CTRL5_C_ADDR,
		.mask = ST_LSM6DSRX_REG_ROUNDING_MASK,
	},
	[ST_LSM6DSRX_REG_CTRL10_C_REG] = {
		.page = FUNC_CFG_ACCESS_0,
		.addr = ST_LSM6DSRX_REG_CTRL10_C_ADDR,
		.mask = ST_LSM6DSRX_REG_TIMESTAMP_EN_MASK,
	},
	[ST_LSM6DSRX_REG_TAP_CFG0_REG] = {
		.page = FUNC_CFG_ACCESS_0,
		.addr = ST_LSM6DSRX_REG_TAP_CFG0_ADDR,
		.mask = ST_LSM6DSRX_REG_LIR_MASK,
	},
	[ST_LSM6DSRX_REG_INT1_CTRL_REG] = {
		.page = FUNC_CFG_ACCESS_0,
		.addr = ST_LSM6DSRX_REG_INT1_CTRL_ADDR,
		.mask = ST_LSM6DSRX_REG_FIFO_TH_MASK,
	},
	[ST_LSM6DSRX_REG_INT2_CTRL_REG] = {
		.page = FUNC_CFG_ACCESS_0,
		.addr = ST_LSM6DSRX_REG_INT2_CTRL_ADDR,
		.mask = ST_LSM6DSRX_REG_FIFO_TH_MASK,
	},
	[ST_LSM6DSRX_REG_FIFO_CTRL1_REG] = {
		.page = FUNC_CFG_ACCESS_0,
		.addr = ST_LSM6DSRX_REG_FIFO_CTRL1_ADDR,
		.mask = GENMASK(7, 0),
	},
	[ST_LSM6DSRX_REG_FIFO_CTRL2_REG] = {
		.page = FUNC_CFG_ACCESS_0,
		.addr = ST_LSM6DSRX_REG_FIFO_CTRL2_ADDR,
		.mask = ST_LSM6DSRX_REG_FIFO_WTM8_MASK,
	},
	[ST_LSM6DSRX_REG_FIFO_CTRL3_REG] = {
		.page = FUNC_CFG_ACCESS_0,
		.addr = ST_LSM6DSRX_REG_FIFO_CTRL3_ADDR,
		.mask = ST_LSM6DSRX_REG_BDR_XL_MASK |
			ST_LSM6DSRX_REG_BDR_GY_MASK,
	},
	[ST_LSM6DSRX_REG_FIFO_CTRL4_REG] = {
		.page = FUNC_CFG_ACCESS_0,
		.addr = ST_LSM6DSRX_REG_FIFO_CTRL4_ADDR,
		.mask = ST_LSM6DSRX_REG_DEC_TS_MASK |
			ST_LSM6DSRX_REG_ODR_T_BATCH_MASK,
	},
	[ST_LSM6DSRX_REG_EMB_FUNC_EN_B_REG] = {
		.page = FUNC_CFG_ACCESS_FUNC_CFG,
		.addr = ST_LSM6DSRX_EMB_FUNC_EN_B_ADDR,
		.mask = ST_LSM6DSRX_FSM_EN_MASK |
			ST_LSM6DSRX_MLC_EN_MASK,
	},
	[ST_LSM6DSRX_REG_FSM_INT1_A_REG] = {
		.page = FUNC_CFG_ACCESS_FUNC_CFG,
		.addr = ST_LSM6DSRX_FSM_INT1_A_ADDR,
		.mask = GENMASK(7, 0),
	},
	[ST_LSM6DSRX_REG_FSM_INT1_B_REG] = {
		.page = FUNC_CFG_ACCESS_FUNC_CFG,
		.addr = ST_LSM6DSRX_FSM_INT1_B_ADDR,
		.mask = GENMASK(7, 0),
	},
	[ST_LSM6DSRX_REG_MLC_INT1_REG] = {
		.page = FUNC_CFG_ACCESS_FUNC_CFG,
		.addr = ST_LSM6DSRX_MLC_INT1_ADDR,
		.mask = GENMASK(7, 0),
	},
	[ST_LSM6DSRX_REG_FSM_INT2_A_REG] = {
		.page = FUNC_CFG_ACCESS_FUNC_CFG,
		.addr = ST_LSM6DSRX_FSM_INT2_A_ADDR,
		.mask = GENMASK(7, 0),
	},
	[ST_LSM6DSRX_REG_FSM_INT2_B_REG] = {
		.page = FUNC_CFG_ACCESS_FUNC_CFG,
		.addr = ST_LSM6DSRX_FSM_INT2_B_ADDR,
		.mask = GENMASK(7, 0),
	},
	[ST_LSM6DSRX_REG_MLC_INT2_REG] = {
		.page = FUNC_CFG_ACCESS_FUNC_CFG,
		.addr = ST_LSM6DSRX_MLC_INT2_ADDR,
		.mask = GENMASK(7, 0),
	},
};

static const struct st_lsm6dsrx_odr_table_entry st_lsm6dsrx_odr_table[] = {
	[ST_LSM6DSRX_ID_ACC] = {
		.size = 8,
		.reg = {
			.addr = ST_LSM6DSRX_CTRL1_XL_ADDR,
			.mask = GENMASK(7, 4),
		},
		.pm = {
			.addr = ST_LSM6DSRX_REG_CTRL6_C_ADDR,
			.mask = ST_LSM6DSRX_REG_XL_HM_MODE_MASK,
		},
		.batching_reg = {
			.addr = ST_LSM6DSRX_REG_FIFO_CTRL3_ADDR,
			.mask = GENMASK(3, 0),
		},
		.odr_avl[0] = {   1, 600000,  0x01,  0x0b },
		.odr_avl[1] = {  12, 500000,  0x01,  0x01 },
		.odr_avl[2] = {  26,      0,  0x02,  0x02 },
		.odr_avl[3] = {  52,      0,  0x03,  0x03 },
		.odr_avl[4] = { 104,      0,  0x04,  0x04 },
		.odr_avl[5] = { 208,      0,  0x05,  0x05 },
		.odr_avl[6] = { 416,      0,  0x06,  0x06 },
		.odr_avl[7] = { 833,      0,  0x07,  0x07 },
	},
	[ST_LSM6DSRX_ID_GYRO] = {
		.size = 8,
		.reg = {
			.addr = ST_LSM6DSRX_CTRL2_G_ADDR,
			.mask = GENMASK(7, 4),
		},
		.pm = {
			.addr = ST_LSM6DSRX_REG_CTRL7_G_ADDR,
			.mask = ST_LSM6DSRX_REG_G_HM_MODE_MASK,
		},
		.batching_reg = {
			.addr = ST_LSM6DSRX_REG_FIFO_CTRL3_ADDR,
			.mask = GENMASK(7, 4),
		},
		.odr_avl[0] = {   6, 500000,  0x01,  0x0b },
		.odr_avl[1] = {  12, 500000,  0x01,  0x01 },
		.odr_avl[2] = {  26,      0,  0x02,  0x02 },
		.odr_avl[3] = {  52,      0,  0x03,  0x03 },
		.odr_avl[4] = { 104,      0,  0x04,  0x04 },
		.odr_avl[5] = { 208,      0,  0x05,  0x05 },
		.odr_avl[6] = { 416,      0,  0x06,  0x06 },
		.odr_avl[7] = { 833,      0,  0x07,  0x07 },
	},
	[ST_LSM6DSRX_ID_TEMP] = {
		.size = 3,
		.batching_reg = {
			.addr = ST_LSM6DSRX_REG_FIFO_CTRL4_ADDR,
			.mask = GENMASK(5, 4),
		},
		.odr_avl[0] = {  1, 600000,   0x01,  0x01 },
		.odr_avl[1] = { 12, 500000,   0x02,  0x02 },
		.odr_avl[2] = { 52,      0,   0x03,  0x03 },
	},
};

/**
 * List of supported device settings
 *
 * The following table list all device features in terms of supported
 * MLC/FSM.
 */
static const struct st_lsm6dsrx_settings st_lsm6dsrx_sensor_settings[] = {
	{
		.id = {
			.hw_id = ST_LSM6DSRX_ID,
			.name = ST_LSM6DSRX_DEV_NAME,
		},
		.st_mlc_probe = true,
		.st_fsm_probe = true,
	},
	{
		.id = {
			.hw_id = ST_LSM6DSR_ID,
			.name = ST_LSM6DSR_DEV_NAME,
		},
		.st_fsm_probe = true,
	},
};

static const struct st_lsm6dsrx_fs_table_entry st_lsm6dsrx_fs_table[] = {
	[ST_LSM6DSRX_ID_ACC] = {
		.size = 4,
		.fs_avl[0] = {
			.reg = {
				.addr = ST_LSM6DSRX_CTRL1_XL_ADDR,
				.mask = GENMASK(3, 2),
			},
			.gain = IIO_G_TO_M_S_2(61000),
			.val = 0x0,
		},
		.fs_avl[1] = {
			.reg = {
				.addr = ST_LSM6DSRX_CTRL1_XL_ADDR,
				.mask = GENMASK(3, 2),
			},
			.gain = IIO_G_TO_M_S_2(122000),
			.val = 0x2,
		},
		.fs_avl[2] = {
			.reg = {
				.addr = ST_LSM6DSRX_CTRL1_XL_ADDR,
				.mask = GENMASK(3, 2),
			},
			.gain = IIO_G_TO_M_S_2(244000),
			.val = 0x3,
		},
		.fs_avl[3] = {
			.reg = {
				.addr = ST_LSM6DSRX_CTRL1_XL_ADDR,
				.mask = GENMASK(3, 2),
			},
			.gain = IIO_G_TO_M_S_2(488000),
			.val = 0x1,
		},
	},
	[ST_LSM6DSRX_ID_GYRO] = {
		.size = 5,
		.fs_avl[0] = {
			.reg = {
				.addr = ST_LSM6DSRX_CTRL2_G_ADDR,
				.mask = GENMASK(3, 0),
			},
			.gain = IIO_DEGREE_TO_RAD(8750000),
			.val = 0x0,
		},
		.fs_avl[1] = {
			.reg = {
				.addr = ST_LSM6DSRX_CTRL2_G_ADDR,
				.mask = GENMASK(3, 0),
			},
			.gain = IIO_DEGREE_TO_RAD(17500000),
			.val = 0x4,
		},
		.fs_avl[2] = {
			.reg = {
				.addr = ST_LSM6DSRX_CTRL2_G_ADDR,
				.mask = GENMASK(3, 0),
			},
			.gain = IIO_DEGREE_TO_RAD(35000000),
			.val = 0x8,
		},
		.fs_avl[3] = {
			.reg = {
				.addr = ST_LSM6DSRX_CTRL2_G_ADDR,
				.mask = GENMASK(3, 0),
			},
			.gain = IIO_DEGREE_TO_RAD(70000000),
			.val = 0x0C,
		},
		.fs_avl[4] = {
			.reg = {
				.addr = ST_LSM6DSRX_CTRL2_G_ADDR,
				.mask = GENMASK(3, 0),
			},
			.gain = IIO_DEGREE_TO_RAD(140000000),
			.val = 0x01,
		},
	},
	[ST_LSM6DSRX_ID_TEMP] = {
		.size = 1,
		.fs_avl[0] = {
			.reg = { 0 },
			.gain = (1000000 / ST_LSM6DSRX_TEMP_GAIN),
			.val = 0x0
		},
	},
};

static const struct iio_mount_matrix *
st_lsm6dsrx_get_mount_matrix(const struct iio_dev *iio_dev,
			     const struct iio_chan_spec *ch)
{
	struct st_lsm6dsrx_sensor *sensor = iio_priv(iio_dev);
	struct st_lsm6dsrx_hw *hw = sensor->hw;

	return &hw->orientation;
}

static const struct iio_chan_spec_ext_info st_lsm6dsrx_chan_spec_ext_info[] = {
	IIO_MOUNT_MATRIX(IIO_SHARED_BY_TYPE, st_lsm6dsrx_get_mount_matrix),
	{ }
};

#define IIO_CHAN_HW_TIMESTAMP(si) {					\
	.type = IIO_COUNT,						\
	.address = ST_LSM6DSRX_REG_TIMESTAMP0_ADDR,			\
	.scan_index = si,						\
	.scan_type = {							\
		.sign = 's',						\
		.realbits = 64,					\
		.storagebits = 64,					\
		.endianness = IIO_LE,					\
	},								\
}

static const struct iio_chan_spec st_lsm6dsrx_acc_channels[] = {
	ST_LSM6DSRX_DATA_CHANNEL(IIO_ACCEL, ST_LSM6DSRX_REG_OUTX_L_A_ADDR,
				1, IIO_MOD_X, 0, 16, 16, 's',
				st_lsm6dsrx_chan_spec_ext_info),
	ST_LSM6DSRX_DATA_CHANNEL(IIO_ACCEL, ST_LSM6DSRX_REG_OUTY_L_A_ADDR,
				1, IIO_MOD_Y, 1, 16, 16, 's',
				st_lsm6dsrx_chan_spec_ext_info),
	ST_LSM6DSRX_DATA_CHANNEL(IIO_ACCEL, ST_LSM6DSRX_REG_OUTZ_L_A_ADDR,
				1, IIO_MOD_Z, 2, 16, 16, 's',
				st_lsm6dsrx_chan_spec_ext_info),
	ST_LSM6DSRX_EVENT_CHANNEL(IIO_ACCEL, flush),
	IIO_CHAN_HW_TIMESTAMP(3),
	IIO_CHAN_SOFT_TIMESTAMP(4),
};

static const struct iio_chan_spec st_lsm6dsrx_gyro_channels[] = {
	ST_LSM6DSRX_DATA_CHANNEL(IIO_ANGL_VEL, ST_LSM6DSRX_REG_OUTX_L_G_ADDR,
				1, IIO_MOD_X, 0, 16, 16, 's',
				st_lsm6dsrx_chan_spec_ext_info),
	ST_LSM6DSRX_DATA_CHANNEL(IIO_ANGL_VEL, ST_LSM6DSRX_REG_OUTY_L_G_ADDR,
				1, IIO_MOD_Y, 1, 16, 16, 's',
				st_lsm6dsrx_chan_spec_ext_info),
	ST_LSM6DSRX_DATA_CHANNEL(IIO_ANGL_VEL, ST_LSM6DSRX_REG_OUTZ_L_G_ADDR,
				1, IIO_MOD_Z, 2, 16, 16, 's',
				st_lsm6dsrx_chan_spec_ext_info),
	ST_LSM6DSRX_EVENT_CHANNEL(IIO_ANGL_VEL, flush),
	IIO_CHAN_HW_TIMESTAMP(3),
	IIO_CHAN_SOFT_TIMESTAMP(4),
};

static const struct iio_chan_spec st_lsm6dsrx_temp_channels[] = {
	{
		.type = IIO_TEMP,
		.address = ST_LSM6DSRX_REG_OUT_TEMP_L_ADDR,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW)
				| BIT(IIO_CHAN_INFO_OFFSET)
				| BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.scan_index = 0,
		.scan_type = {
			.sign = 's',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_LE,
		}
	},
	ST_LSM6DSRX_EVENT_CHANNEL(IIO_TEMP, flush),
	IIO_CHAN_HW_TIMESTAMP(1),
	IIO_CHAN_SOFT_TIMESTAMP(2),
};

static __maybe_unused int st_lsm6dsrx_reg_access(struct iio_dev *iio_dev,
				 unsigned int reg, unsigned int writeval,
				 unsigned int *readval)
{
	struct st_lsm6dsrx_sensor *sensor = iio_priv(iio_dev);
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

static int st_lsm6dsrx_set_page_0(struct st_lsm6dsrx_hw *hw)
{
	return regmap_write(hw->regmap,
			    ST_LSM6DSRX_REG_FUNC_CFG_ACCESS_ADDR, 0);
}

static int st_lsm6dsrx_check_whoami(struct st_lsm6dsrx_hw *hw,
				      int id)
{
	int err, i;
	int data;

	for (i = 0; i < ARRAY_SIZE(st_lsm6dsrx_sensor_settings); i++) {
			if (st_lsm6dsrx_sensor_settings[i].id.name &&
			    st_lsm6dsrx_sensor_settings[i].id.hw_id == id)
				break;
	}

	if (i == ARRAY_SIZE(st_lsm6dsrx_sensor_settings)) {
		dev_err(hw->dev, "unsupported hw id [%02x]\n", id);

		return -ENODEV;
	}

	err = regmap_read(hw->regmap, ST_LSM6DSRX_REG_WHOAMI_ADDR, &data);
	if (err < 0) {
		dev_err(hw->dev, "failed to read whoami register\n");
		return err;
	}

	if (data != ST_LSM6DSRX_WHOAMI_VAL) {
		dev_err(hw->dev, "unsupported whoami [%02x]\n", data);
		return -ENODEV;
	}

	hw->settings = &st_lsm6dsrx_sensor_settings[i];
	hw->fs_table = st_lsm6dsrx_fs_table;
	hw->odr_table = st_lsm6dsrx_odr_table;

	return 0;
}

static int st_lsm6dsrx_get_odr_calibration(struct st_lsm6dsrx_hw *hw)
{
	int err;
	int data;
	s64 odr_calib;

	err = regmap_read(hw->regmap, ST_LSM6DSRX_INTERNAL_FREQ_FINE, &data);
	if (err < 0) {
		dev_err(hw->dev, "failed to read %d register\n",
				ST_LSM6DSRX_INTERNAL_FREQ_FINE);
		return err;
	}

	odr_calib = ((s8)data * 37500) / 1000;
	hw->ts_delta_ns = ST_LSM6DSRX_TS_DELTA_NS - odr_calib;

	dev_info(hw->dev, "Freq Fine %lld (ts %lld)\n",
		 odr_calib, hw->ts_delta_ns);

	return 0;
}

static int st_lsm6dsrx_set_full_scale(struct st_lsm6dsrx_sensor *sensor,
				      u32 gain)
{
	enum st_lsm6dsrx_sensor_id id = sensor->id;
	struct st_lsm6dsrx_hw *hw = sensor->hw;
	int i, err;
	u8 val;

	for (i = 0; i < st_lsm6dsrx_fs_table[id].size; i++)
		if (st_lsm6dsrx_fs_table[id].fs_avl[i].gain == gain)
			break;

	if (i == st_lsm6dsrx_fs_table[id].size)
		return -EINVAL;

	val = st_lsm6dsrx_fs_table[id].fs_avl[i].val;
	err = regmap_update_bits(hw->regmap,
				 st_lsm6dsrx_fs_table[id].fs_avl[i].reg.addr,
				 st_lsm6dsrx_fs_table[id].fs_avl[i].reg.mask,
				 ST_LSM6DSRX_SHIFT_VAL(val,
				st_lsm6dsrx_fs_table[id].fs_avl[i].reg.mask));
	if (err < 0)
		return err;

	sensor->gain = gain;

	return 0;
}

static int st_lsm6dsrx_get_odr_val(enum st_lsm6dsrx_sensor_id id, int odr,
				   int uodr, struct st_lsm6dsrx_odr *oe)
{
	int req_odr = ST_LSM6DSRX_ODR_EXPAND(odr, uodr);
	int sensor_odr;
	int i;

	for (i = 0; i < st_lsm6dsrx_odr_table[id].size; i++) {
		sensor_odr = ST_LSM6DSRX_ODR_EXPAND(
				st_lsm6dsrx_odr_table[id].odr_avl[i].hz,
				st_lsm6dsrx_odr_table[id].odr_avl[i].uhz);
		if (sensor_odr >= req_odr) {
			oe->hz = st_lsm6dsrx_odr_table[id].odr_avl[i].hz;
			oe->uhz = st_lsm6dsrx_odr_table[id].odr_avl[i].uhz;
			oe->val = st_lsm6dsrx_odr_table[id].odr_avl[i].val;

			return 0;
		}
	}

	return -EINVAL;
}

int st_lsm6dsrx_get_batch_val(struct st_lsm6dsrx_sensor *sensor, int odr,
			      int uodr, u8 *val)
{
	enum st_lsm6dsrx_sensor_id id = sensor->id;
	int req_odr = ST_LSM6DSRX_ODR_EXPAND(odr, uodr);
	int i;
	int sensor_odr;

	for (i = 0; i < st_lsm6dsrx_odr_table[id].size; i++) {
		sensor_odr = ST_LSM6DSRX_ODR_EXPAND(
				st_lsm6dsrx_odr_table[id].odr_avl[i].hz,
				st_lsm6dsrx_odr_table[id].odr_avl[i].uhz);
		if (sensor_odr >= req_odr)
			break;
	}

	if (i == st_lsm6dsrx_odr_table[id].size)
		return -EINVAL;

	*val = st_lsm6dsrx_odr_table[id].odr_avl[i].batch_val;

	return 0;
}

static u16 st_lsm6dsrx_check_odr_dependency(struct st_lsm6dsrx_hw *hw,
					    int odr, int uodr,
					    enum st_lsm6dsrx_sensor_id ref_id)
{
	struct st_lsm6dsrx_sensor *ref = iio_priv(hw->iio_devs[ref_id]);
	bool enable = odr > 0;
	u16 ret;

	if (enable) {
		/* uodr not used */
		if (hw->enable_mask & BIT_ULL(ref_id))
			ret = max_t(u16, ref->odr, odr);
		else
			ret = odr;
	} else {
		ret = (hw->enable_mask & BIT_ULL(ref_id)) ?
		      ref->odr : 0;
	}

	return ret;
}

static int st_lsm6dsrx_set_odr(struct st_lsm6dsrx_sensor *sensor, int req_odr,
			       int req_uodr)
{
	enum st_lsm6dsrx_sensor_id id = sensor->id;
	struct st_lsm6dsrx_hw *hw = sensor->hw;
	struct st_lsm6dsrx_odr oe = { 0 };

	switch (id) {
	case ST_LSM6DSRX_ID_EXT0:
	case ST_LSM6DSRX_ID_EXT1:
	case ST_LSM6DSRX_ID_TEMP:
	case ST_LSM6DSRX_ID_FSM_0:
	case ST_LSM6DSRX_ID_FSM_1:
	case ST_LSM6DSRX_ID_FSM_2:
	case ST_LSM6DSRX_ID_FSM_3:
	case ST_LSM6DSRX_ID_FSM_4:
	case ST_LSM6DSRX_ID_FSM_5:
	case ST_LSM6DSRX_ID_FSM_6:
	case ST_LSM6DSRX_ID_FSM_7:
	case ST_LSM6DSRX_ID_FSM_8:
	case ST_LSM6DSRX_ID_FSM_9:
	case ST_LSM6DSRX_ID_FSM_10:
	case ST_LSM6DSRX_ID_FSM_11:
	case ST_LSM6DSRX_ID_FSM_12:
	case ST_LSM6DSRX_ID_FSM_13:
	case ST_LSM6DSRX_ID_FSM_14:
	case ST_LSM6DSRX_ID_FSM_15:
	case ST_LSM6DSRX_ID_MLC_0:
	case ST_LSM6DSRX_ID_MLC_1:
	case ST_LSM6DSRX_ID_MLC_2:
	case ST_LSM6DSRX_ID_MLC_3:
	case ST_LSM6DSRX_ID_MLC_4:
	case ST_LSM6DSRX_ID_MLC_5:
	case ST_LSM6DSRX_ID_MLC_6:
	case ST_LSM6DSRX_ID_MLC_7:
	case ST_LSM6DSRX_ID_STEP_COUNTER:
	case ST_LSM6DSRX_ID_STEP_DETECTOR:
	case ST_LSM6DSRX_ID_SIGN_MOTION:
	case ST_LSM6DSRX_ID_TILT:
	case ST_LSM6DSRX_ID_TAP:
	case ST_LSM6DSRX_ID_DTAP:
	case ST_LSM6DSRX_ID_WK:
	case ST_LSM6DSRX_ID_FF:
	case ST_LSM6DSRX_ID_SLPCHG:
	case ST_LSM6DSRX_ID_6D:
	case ST_LSM6DSRX_ID_ACC: {
		int odr;
		int i;

		id = ST_LSM6DSRX_ID_ACC;
		for (i = ST_LSM6DSRX_ID_ACC; i < ST_LSM6DSRX_ID_MAX; i++) {
			if (!hw->iio_devs[i] || i == sensor->id)
				continue;

			odr = st_lsm6dsrx_check_odr_dependency(hw, req_odr,
							       req_uodr, i);
			if (odr != req_odr) {
				/* device already configured */
				return 0;
			}
		}
		break;
	}
	default:
		break;
	}

	if (ST_LSM6DSRX_ODR_EXPAND(req_odr, req_uodr) > 0) {
		int err;

		err = st_lsm6dsrx_get_odr_val(id, req_odr, req_uodr, &oe);
		if (err)
			return err;

		/* check if sensor supports power mode setting */
		if (sensor->pm != ST_LSM6DSRX_NO_MODE) {
			err = st_lsm6dsrx_update_bits_locked(hw,
				      st_lsm6dsrx_odr_table[id].pm.addr,
				      st_lsm6dsrx_odr_table[id].pm.mask,
				      sensor->pm);
			if (err < 0)
				return err;
		}
	}

	return st_lsm6dsrx_update_bits_locked(hw,
				     st_lsm6dsrx_odr_table[id].reg.addr,
				     st_lsm6dsrx_odr_table[id].reg.mask,
				     oe.val);
}

int st_lsm6dsrx_sensor_set_enable(struct st_lsm6dsrx_sensor *sensor,
				  bool enable)
{
	int uodr = enable ? sensor->uodr : 0;
	int odr = enable ? sensor->odr : 0;
	int err;

	err = st_lsm6dsrx_set_odr(sensor, odr, uodr);
	if (err < 0)
		return err;

	if (enable)
		sensor->hw->enable_mask |= BIT_ULL(sensor->id);
	else
		sensor->hw->enable_mask &= ~BIT_ULL(sensor->id);

	return 0;
}

static int st_lsm6dsrx_read_oneshot(struct st_lsm6dsrx_sensor *sensor,
				    u8 addr, int *val)
{
	struct st_lsm6dsrx_hw *hw = sensor->hw;
	int err, delay;
	__le16 data;

	if (sensor->id == ST_LSM6DSRX_ID_TEMP) {
		u8 status;

		err = st_lsm6dsrx_read_locked(hw,
					       ST_LSM6DSRX_REG_STATUS_ADDR,
					       &status, sizeof(status));
		if (err < 0)
			return err;

		if (status & ST_LSM6DSRX_REG_STATUS_TDA) {
			err = st_lsm6dsrx_read_locked(hw, addr,
						       &data, sizeof(data));
			if (err < 0)
				return err;

			sensor->old_data = data;
		} else {
			data = sensor->old_data;
		}
	} else {
		err = st_lsm6dsrx_sensor_set_enable(sensor, true);
		if (err < 0)
			return err;

		/*
		 * - use big delay for data valid because of drdy mask enabled
		 * - uodr is neglected in this operation
		 */
		delay = 10000000 / sensor->odr;
		usleep_range(delay, 2 * delay);

		err = st_lsm6dsrx_read_locked(hw, addr,
				       &data, sizeof(data));

		st_lsm6dsrx_sensor_set_enable(sensor, false);
		if (err < 0)
			return err;
	}

	*val = (s16)le16_to_cpu(data);

	return IIO_VAL_INT;
}

static int st_lsm6dsrx_read_raw(struct iio_dev *iio_dev,
				struct iio_chan_spec const *ch,
				int *val, int *val2, long mask)
{
	struct st_lsm6dsrx_sensor *sensor = iio_priv(iio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(iio_dev);
		if (ret)
			return ret;

		ret = st_lsm6dsrx_read_oneshot(sensor, ch->address, val);
		iio_device_release_direct_mode(iio_dev);
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
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = (int)sensor->odr;
		*val2 = (int)sensor->uodr;
		ret = IIO_VAL_INT_PLUS_MICRO;
		break;
	case IIO_CHAN_INFO_SCALE:
		switch (ch->type) {
		case IIO_TEMP:
			*val = 1000;
			*val2 = ST_LSM6DSRX_TEMP_GAIN;
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
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int st_lsm6dsrx_write_raw(struct iio_dev *iio_dev,
				 struct iio_chan_spec const *chan,
				 int val, int val2, long mask)
{
	struct st_lsm6dsrx_sensor *sensor = iio_priv(iio_dev);
	int err;

	mutex_lock(&iio_dev->mlock);

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		err = st_lsm6dsrx_set_full_scale(sensor, val2);
		break;
	case IIO_CHAN_INFO_SAMP_FREQ: {
		struct st_lsm6dsrx_odr oe = { 0 };

		err = st_lsm6dsrx_get_odr_val(sensor->id, val, val2, &oe);
		if (!err) {
			sensor->odr = oe.hz;
			sensor->uodr = oe.uhz;

			/*
			 * VTS test testSamplingRateHotSwitchOperation not
			 * toggle the enable status of sensor after changing
			 * the ODR -> force it
			 */
			if (sensor->hw->enable_mask & BIT_ULL(sensor->id)) {
				switch (sensor->id) {
				case ST_LSM6DSRX_ID_GYRO:
				case ST_LSM6DSRX_ID_ACC: {
					err = st_lsm6dsrx_set_odr(sensor,
								  sensor->odr,
								  sensor->uodr);
					if (err < 0)
						break;

					st_lsm6dsrx_update_batching(iio_dev, 1);
					}
					break;
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

	return err;
}

static ssize_t
st_lsm6dsrx_sysfs_sampling_frequency_avail(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct st_lsm6dsrx_sensor *sensor = iio_priv(dev_get_drvdata(dev));
	enum st_lsm6dsrx_sensor_id id = sensor->id;
	int i, len = 0;

	for (i = 0; i < st_lsm6dsrx_odr_table[id].size; i++) {
		len += scnprintf(buf + len, PAGE_SIZE - len, "%d.%06d ",
				 st_lsm6dsrx_odr_table[id].odr_avl[i].hz,
				 st_lsm6dsrx_odr_table[id].odr_avl[i].uhz);
	}

	buf[len - 1] = '\n';

	return len;
}

static ssize_t st_lsm6dsrx_sysfs_scale_avail(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct st_lsm6dsrx_sensor *sensor = iio_priv(dev_get_drvdata(dev));
	enum st_lsm6dsrx_sensor_id id = sensor->id;
	int i, len = 0;

	for (i = 0; i < st_lsm6dsrx_fs_table[id].size; i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "0.%09u ",
				 st_lsm6dsrx_fs_table[id].fs_avl[i].gain);
	buf[len - 1] = '\n';

	return len;
}

static ssize_t
st_lsm6dsrx_sysfs_get_power_mode_avail(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	int i, len = 0;

	for (i = 0; i < ARRAY_SIZE(st_lsm6dsrx_power_mode); i++) {
		len += scnprintf(buf + len, PAGE_SIZE - len, "%s ",
				 st_lsm6dsrx_power_mode[i].string_mode);
	}

	buf[len - 1] = '\n';

	return len;
}

ssize_t st_lsm6dsrx_get_power_mode(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct iio_dev *iio_dev = dev_get_drvdata(dev);
	struct st_lsm6dsrx_sensor *sensor = iio_priv(iio_dev);

	return sprintf(buf, "%s\n",
		       st_lsm6dsrx_power_mode[sensor->pm].string_mode);
}

ssize_t st_lsm6dsrx_set_power_mode(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_get_drvdata(dev);
	struct st_lsm6dsrx_sensor *sensor = iio_priv(iio_dev);
	int err, i;

	for (i = 0; i < ARRAY_SIZE(st_lsm6dsrx_power_mode); i++) {
		if (strncmp(buf, st_lsm6dsrx_power_mode[i].string_mode,
		    strlen(st_lsm6dsrx_power_mode[i].string_mode)) == 0)
			break;
	}

	if (i == ARRAY_SIZE(st_lsm6dsrx_power_mode))
		return -EINVAL;

	err = iio_device_claim_direct_mode(iio_dev);
	if (err)
		return err;

	/* update power mode */
	sensor->pm = st_lsm6dsrx_power_mode[i].val;

	iio_device_release_direct_mode(iio_dev);

	return size;
}

static int __maybe_unused st_lsm6dsrx_bk_regs(struct st_lsm6dsrx_hw *hw)
{
	unsigned int data;
	bool restore = 0;
	int i, err = 0;

	mutex_lock(&hw->page_lock);

	for (i = 0; i < ST_LSM6DSRX_SUSPEND_RESUME_REGS; i++) {
		if (st_lsm6dsrx_suspend_resume[i].page != FUNC_CFG_ACCESS_0) {
			err = regmap_update_bits(hw->regmap,
					ST_LSM6DSRX_REG_FUNC_CFG_ACCESS_ADDR,
					ST_LSM6DSRX_REG_ACCESS_MASK,
					FIELD_PREP(ST_LSM6DSRX_REG_ACCESS_MASK,
					 st_lsm6dsrx_suspend_resume[i].page));
			if (err < 0) {
				dev_err(hw->dev,
					"failed to update %02x reg\n",
					st_lsm6dsrx_suspend_resume[i].addr);
				break;
			}

			restore = 1;
		}

		err = regmap_read(hw->regmap,
				  st_lsm6dsrx_suspend_resume[i].addr,
				  &data);
		if (err < 0) {
			dev_err(hw->dev,
				"failed to save register %02x\n",
				st_lsm6dsrx_suspend_resume[i].addr);
			goto out_lock;
		}

		if (restore) {
			err = regmap_update_bits(hw->regmap,
					ST_LSM6DSRX_REG_FUNC_CFG_ACCESS_ADDR,
					ST_LSM6DSRX_REG_ACCESS_MASK,
					FIELD_PREP(ST_LSM6DSRX_REG_ACCESS_MASK,
					 FUNC_CFG_ACCESS_0));
			if (err < 0) {
				dev_err(hw->dev,
					"failed to update %02x reg\n",
					st_lsm6dsrx_suspend_resume[i].addr);
				break;
			}

			restore = 0;
		}

		st_lsm6dsrx_suspend_resume[i].val = data;
	}

out_lock:
	mutex_unlock(&hw->page_lock);

	return err;
}

static int __maybe_unused st_lsm6dsrx_restore_regs(struct st_lsm6dsrx_hw *hw)
{
	bool restore = 0;
	int i, err = 0;

	mutex_lock(&hw->page_lock);

	for (i = 0; i < ST_LSM6DSRX_SUSPEND_RESUME_REGS; i++) {
		if (st_lsm6dsrx_suspend_resume[i].page != FUNC_CFG_ACCESS_0) {
			err = regmap_update_bits(hw->regmap,
					ST_LSM6DSRX_REG_FUNC_CFG_ACCESS_ADDR,
					ST_LSM6DSRX_REG_ACCESS_MASK,
					FIELD_PREP(ST_LSM6DSRX_REG_ACCESS_MASK,
					 st_lsm6dsrx_suspend_resume[i].page));
			if (err < 0) {
				dev_err(hw->dev,
					"failed to update %02x reg\n",
					st_lsm6dsrx_suspend_resume[i].addr);
				break;
			}

			restore = 1;
		}

		err = regmap_update_bits(hw->regmap,
					 st_lsm6dsrx_suspend_resume[i].addr,
					 st_lsm6dsrx_suspend_resume[i].mask,
					 st_lsm6dsrx_suspend_resume[i].val);
		if (err < 0) {
			dev_err(hw->dev,
				"failed to update %02x reg\n",
				st_lsm6dsrx_suspend_resume[i].addr);
			break;
		}

		if (restore) {
			err = regmap_update_bits(hw->regmap,
					ST_LSM6DSRX_REG_FUNC_CFG_ACCESS_ADDR,
					ST_LSM6DSRX_REG_ACCESS_MASK,
					FIELD_PREP(ST_LSM6DSRX_REG_ACCESS_MASK,
					 FUNC_CFG_ACCESS_0));
			if (err < 0) {
				dev_err(hw->dev,
					"failed to update %02x reg\n",
					st_lsm6dsrx_suspend_resume[i].addr);
				break;
			}

			restore = 0;
		}
	}

	mutex_unlock(&hw->page_lock);

	return err;
}

int st_lsm6dsrx_of_get_pin(struct st_lsm6dsrx_hw *hw, int *pin)
{
	struct device_node *np = hw->dev->of_node;

	if (!np)
		return -EINVAL;

	return of_property_read_u32(np, "st,int-pin", pin);
}

static int st_lsm6dsrx_get_int_reg(struct st_lsm6dsrx_hw *hw, u8 *drdy_reg)
{
	int err = 0, int_pin;

	if (st_lsm6dsrx_of_get_pin(hw, &int_pin) < 0) {
		struct st_sensors_platform_data *pdata;
		struct device *dev = hw->dev;

		pdata = (struct st_sensors_platform_data *)dev->platform_data;
		int_pin = pdata ? pdata->drdy_int_pin : 1;
	}

	switch (int_pin) {
	case 1:
		*drdy_reg = ST_LSM6DSRX_REG_INT1_CTRL_ADDR;
		break;
	case 2:
		*drdy_reg = ST_LSM6DSRX_REG_INT2_CTRL_ADDR;
		break;
	default:
		dev_err(hw->dev, "unsupported interrupt pin\n");
		err = -EINVAL;
		break;
	}

	hw->int_pin = int_pin;

	return err;
}

static int st_lsm6dsrx_set_selftest(
				struct st_lsm6dsrx_sensor *sensor, int index)
{
	u8 mode, mask;

	switch (sensor->id) {
	case ST_LSM6DSRX_ID_ACC:
		mask = ST_LSM6DSRX_REG_ST_XL_MASK;
		mode = st_lsm6dsrx_selftest_table[index].accel_value;
		break;
	case ST_LSM6DSRX_ID_GYRO:
		mask = ST_LSM6DSRX_REG_ST_G_MASK;
		mode = st_lsm6dsrx_selftest_table[index].gyro_value;
		break;
	default:
		return -EINVAL;
	}

	return st_lsm6dsrx_update_bits_locked(sensor->hw,
					   ST_LSM6DSRX_REG_CTRL5_C_ADDR,
					   mask, mode);
}

static ssize_t st_lsm6dsrx_sysfs_get_selftest_available(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s, %s\n",
		       st_lsm6dsrx_selftest_table[1].string_mode,
		       st_lsm6dsrx_selftest_table[2].string_mode);
}

static ssize_t st_lsm6dsrx_sysfs_get_selftest_status(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int8_t result;
	char *message = NULL;
	struct st_lsm6dsrx_sensor *sensor = iio_priv(dev_get_drvdata(dev));
	enum st_lsm6dsrx_sensor_id id = sensor->id;

	if (id != ST_LSM6DSRX_ID_ACC &&
	    id != ST_LSM6DSRX_ID_GYRO)
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

static int st_lsm6dsrx_selftest_sensor(struct st_lsm6dsrx_sensor *sensor,
				       int test)
{
	int x_selftest = 0, y_selftest = 0, z_selftest = 0;
	int x = 0, y = 0, z = 0, try_count = 0;
	u8 i, status, n = 0;
	u8 reg, bitmask;
	int ret, delay;
	u8 raw_data[6];

	switch (sensor->id) {
	case ST_LSM6DSRX_ID_ACC:
		reg = ST_LSM6DSRX_REG_OUTX_L_A_ADDR;
		bitmask = ST_LSM6DSRX_REG_STATUS_XLDA;
		break;
	case ST_LSM6DSRX_ID_GYRO:
		reg = ST_LSM6DSRX_REG_OUTX_L_G_ADDR;
		bitmask = ST_LSM6DSRX_REG_STATUS_GDA;
		break;
	default:
		return -EINVAL;
	}

	/* set selftest normal mode */
	ret = st_lsm6dsrx_set_selftest(sensor, 0);
	if (ret < 0)
		return ret;

	ret = st_lsm6dsrx_sensor_set_enable(sensor, true);
	if (ret < 0)
		return ret;

	/* wait at least 2 ODRs to be sure */
	delay = 2 * (1000000 / sensor->odr);

	/* power up, wait 100 ms for stable output */
	msleep(100);

	/* for 5 times, after checking status bit, read the output registers */
	for (i = 0; i < 5; i++) {
		try_count = 0;
		while (try_count < 3) {
			usleep_range(delay, 2 * delay);
			ret = st_lsm6dsrx_read_locked(sensor->hw,
						ST_LSM6DSRX_REG_STATUS_ADDR,
						&status, sizeof(status));
			if (ret < 0)
				goto selftest_failure;

			if (status & bitmask) {
				ret = st_lsm6dsrx_read_locked(sensor->hw,
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
			"some acc samples missing (expected %d, read %d)\n",
			i, n);
		ret = -1;

		goto selftest_failure;
	}

	n = 0;

	/* set selftest mode */
	st_lsm6dsrx_set_selftest(sensor, test);

	/* wait 100 ms for stable output */
	msleep(100);

	/* for 5 times, after checking status bit, read the output registers */
	for (i = 0; i < 5; i++) {
		try_count = 0;
		while (try_count < 3) {
			usleep_range(delay, 2 * delay);
			ret = st_lsm6dsrx_read_locked(sensor->hw,
						ST_LSM6DSRX_REG_STATUS_ADDR,
						&status, sizeof(status));
			if (ret < 0)
				goto selftest_failure;

			if (status & bitmask) {
				ret = st_lsm6dsrx_read_locked(sensor->hw,
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
	st_lsm6dsrx_set_selftest(sensor, 0);

	return st_lsm6dsrx_sensor_set_enable(sensor, false);
}

static ssize_t st_lsm6dsrx_sysfs_start_selftest(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_get_drvdata(dev);
	struct st_lsm6dsrx_sensor *sensor = iio_priv(iio_dev);
	enum st_lsm6dsrx_sensor_id id = sensor->id;
	struct st_lsm6dsrx_hw *hw = sensor->hw;
	int ret, test;
	u8 drdy_reg;
	u32 gain;

	if (id != ST_LSM6DSRX_ID_ACC &&
	    id != ST_LSM6DSRX_ID_GYRO)
		return -EINVAL;

	for (test = 0; test < ARRAY_SIZE(st_lsm6dsrx_selftest_table); test++) {
		if (strncmp(buf, st_lsm6dsrx_selftest_table[test].string_mode,
			strlen(st_lsm6dsrx_selftest_table[test].string_mode)) == 0)
			break;
	}

	if (test == ARRAY_SIZE(st_lsm6dsrx_selftest_table))
		return -EINVAL;

	ret = iio_device_claim_direct_mode(iio_dev);
	if (ret)
		return ret;

	/* self test mode unavailable if sensor enabled */
	if (hw->enable_mask & BIT_ULL(id)) {
		ret = -EBUSY;

		goto out_claim;
	}

	st_lsm6dsrx_bk_regs(hw);

	/* disable FIFO watermak interrupt */
	ret = st_lsm6dsrx_get_int_reg(hw, &drdy_reg);
	if (ret < 0)
		goto restore_regs;

	ret = st_lsm6dsrx_update_bits_locked(hw, drdy_reg,
					     ST_LSM6DSRX_REG_FIFO_TH_MASK, 0);
	if (ret < 0)
		goto restore_regs;

	gain = sensor->gain;
	if (id == ST_LSM6DSRX_ID_ACC) {
		/* set BDU = 1, FS = 4 g, ODR = 52 Hz */
		st_lsm6dsrx_set_full_scale(sensor, IIO_G_TO_M_S_2(122));
		st_lsm6dsrx_set_odr(sensor, 52, 0);
		st_lsm6dsrx_selftest_sensor(sensor, test);

		/* restore full scale after test */
		st_lsm6dsrx_set_full_scale(sensor, gain);
	} else {
		/* set BDU = 1, ODR = 208 Hz, FS = 2000 dps */
		st_lsm6dsrx_set_full_scale(sensor, IIO_DEGREE_TO_RAD(70000));
		st_lsm6dsrx_set_odr(sensor, 208, 0);
		st_lsm6dsrx_selftest_sensor(sensor, test);

		/* restore full scale after test */
		st_lsm6dsrx_set_full_scale(sensor, gain);
	}

restore_regs:
	st_lsm6dsrx_restore_regs(hw);

out_claim:
	iio_device_release_direct_mode(iio_dev);

	return size;
}

static int st_lsm6dsrx_write_raw_get_fmt(struct iio_dev *indio_dev,
					 struct iio_chan_spec const *chan,
					 long mask)
{
	if (mask == IIO_CHAN_INFO_SCALE) {
		switch (chan->type) {
		case IIO_ANGL_VEL:
		case IIO_ACCEL:
			return IIO_VAL_INT_PLUS_NANO;
		case IIO_TEMP:
			return IIO_VAL_FRACTIONAL;
		default:
			return -EINVAL;
		}
	}

	return -EINVAL;
}

ssize_t st_lsm6dsrx_get_module_id(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	struct iio_dev *iio_dev = dev_get_drvdata(dev);
	struct st_lsm6dsrx_sensor *sensor = iio_priv(iio_dev);
	struct st_lsm6dsrx_hw *hw = sensor->hw;

	return scnprintf(buf, PAGE_SIZE, "%u\n", hw->module_id);
}

static IIO_DEV_ATTR_SAMP_FREQ_AVAIL(st_lsm6dsrx_sysfs_sampling_frequency_avail);
static IIO_DEVICE_ATTR(in_accel_scale_available, 0444,
		       st_lsm6dsrx_sysfs_scale_avail, NULL, 0);
static IIO_DEVICE_ATTR(in_anglvel_scale_available, 0444,
		       st_lsm6dsrx_sysfs_scale_avail, NULL, 0);
static IIO_DEVICE_ATTR(in_temp_scale_available, 0444,
		       st_lsm6dsrx_sysfs_scale_avail, NULL, 0);

static IIO_DEVICE_ATTR(hwfifo_watermark_max, 0444,
		       st_lsm6dsrx_get_max_watermark, NULL, 0);
static IIO_DEVICE_ATTR(hwfifo_flush, 0200, NULL, st_lsm6dsrx_flush_fifo, 0);
static IIO_DEVICE_ATTR(hwfifo_watermark, 0644, st_lsm6dsrx_get_watermark,
		       st_lsm6dsrx_set_watermark, 0);

static IIO_DEVICE_ATTR(power_mode_available, 0444,
		       st_lsm6dsrx_sysfs_get_power_mode_avail, NULL, 0);
static IIO_DEVICE_ATTR(power_mode, 0644,
		       st_lsm6dsrx_get_power_mode,
		       st_lsm6dsrx_set_power_mode, 0);

static IIO_DEVICE_ATTR(selftest_available, 0444,
		       st_lsm6dsrx_sysfs_get_selftest_available,
		       NULL, 0);
static IIO_DEVICE_ATTR(selftest, 0644,
		       st_lsm6dsrx_sysfs_get_selftest_status,
		       st_lsm6dsrx_sysfs_start_selftest, 0);
static IIO_DEVICE_ATTR(module_id, 0444, st_lsm6dsrx_get_module_id, NULL, 0);

static struct attribute *st_lsm6dsrx_acc_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_accel_scale_available.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark_max.dev_attr.attr,
	&iio_dev_attr_power_mode_available.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark.dev_attr.attr,
	&iio_dev_attr_hwfifo_flush.dev_attr.attr,
	&iio_dev_attr_power_mode.dev_attr.attr,
	&iio_dev_attr_selftest_available.dev_attr.attr,
	&iio_dev_attr_selftest.dev_attr.attr,
	&iio_dev_attr_module_id.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lsm6dsrx_acc_attribute_group = {
	.attrs = st_lsm6dsrx_acc_attributes,
};

static const struct iio_info st_lsm6dsrx_acc_info = {
	.attrs = &st_lsm6dsrx_acc_attribute_group,
	.read_raw = st_lsm6dsrx_read_raw,
	.write_raw = st_lsm6dsrx_write_raw,
	.write_raw_get_fmt = st_lsm6dsrx_write_raw_get_fmt,
#ifdef CONFIG_DEBUG_FS
	.debugfs_reg_access = &st_lsm6dsrx_reg_access,
#endif /* CONFIG_DEBUG_FS */
};

static struct attribute *st_lsm6dsrx_gyro_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_anglvel_scale_available.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark_max.dev_attr.attr,
	&iio_dev_attr_power_mode_available.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark.dev_attr.attr,
	&iio_dev_attr_hwfifo_flush.dev_attr.attr,
	&iio_dev_attr_power_mode.dev_attr.attr,
	&iio_dev_attr_selftest_available.dev_attr.attr,
	&iio_dev_attr_selftest.dev_attr.attr,
	&iio_dev_attr_module_id.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lsm6dsrx_gyro_attribute_group = {
	.attrs = st_lsm6dsrx_gyro_attributes,
};

static const struct iio_info st_lsm6dsrx_gyro_info = {
	.attrs = &st_lsm6dsrx_gyro_attribute_group,
	.read_raw = st_lsm6dsrx_read_raw,
	.write_raw = st_lsm6dsrx_write_raw,
	.write_raw_get_fmt = st_lsm6dsrx_write_raw_get_fmt,
};

static struct attribute *st_lsm6dsrx_temp_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_temp_scale_available.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark_max.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark.dev_attr.attr,
	&iio_dev_attr_hwfifo_flush.dev_attr.attr,
	&iio_dev_attr_module_id.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lsm6dsrx_temp_attribute_group = {
	.attrs = st_lsm6dsrx_temp_attributes,
};

static const struct iio_info st_lsm6dsrx_temp_info = {
	.attrs = &st_lsm6dsrx_temp_attribute_group,
	.read_raw = st_lsm6dsrx_read_raw,
	.write_raw = st_lsm6dsrx_write_raw,
	.write_raw_get_fmt = st_lsm6dsrx_write_raw_get_fmt,
};

static const unsigned long st_lsm6dsrx_available_scan_masks[] = {
	GENMASK(3, 0), 0x0
};

static const unsigned long st_lsm6dsrx_temp_available_scan_masks[] = {
	0x1, 0x0
};

static int st_lsm6dsrx_reset_device(struct st_lsm6dsrx_hw *hw)
{
	int err;

	/* sw reset */
	err = regmap_update_bits(hw->regmap, ST_LSM6DSRX_REG_CTRL3_C_ADDR,
				 ST_LSM6DSRX_REG_SW_RESET_MASK,
				 FIELD_PREP(ST_LSM6DSRX_REG_SW_RESET_MASK, 1));
	if (err < 0)
		return err;

	msleep(50);

	/* boot */
	err = regmap_update_bits(hw->regmap, ST_LSM6DSRX_REG_CTRL3_C_ADDR,
				 ST_LSM6DSRX_REG_BOOT_MASK,
				 FIELD_PREP(ST_LSM6DSRX_REG_BOOT_MASK, 1));

	msleep(50);

	return err;
}

static int st_lsm6dsrx_init_timestamp_engine(struct st_lsm6dsrx_hw *hw,
					     bool enable)
{
	int err;

	/* init timestamp engine */
	err = regmap_update_bits(hw->regmap, ST_LSM6DSRX_REG_CTRL10_C_ADDR,
				 ST_LSM6DSRX_REG_TIMESTAMP_EN_MASK,
				 ST_LSM6DSRX_SHIFT_VAL(true,
				  ST_LSM6DSRX_REG_TIMESTAMP_EN_MASK));
	if (err < 0)
		return err;

	/* enable timestamp rollover interrupt on int2 */
	return regmap_update_bits(hw->regmap, ST_LSM6DSRX_REG_MD2_CFG_ADDR,
				  ST_LSM6DSRX_REG_INT2_TIMESTAMP_MASK,
				  ST_LSM6DSRX_SHIFT_VAL(enable,
				   ST_LSM6DSRX_REG_INT2_TIMESTAMP_MASK));
}

static int st_lsm6dsrx_init_device(struct st_lsm6dsrx_hw *hw)
{
	u8 drdy_reg;
	int err;

	/* latch interrupts */
	err = regmap_update_bits(hw->regmap, ST_LSM6DSRX_REG_TAP_CFG0_ADDR,
				 ST_LSM6DSRX_REG_LIR_MASK,
				 FIELD_PREP(ST_LSM6DSRX_REG_LIR_MASK, 1));
	if (err < 0)
		return err;

	/* enable (B)lock (D)ata (U)pdate */
	err = regmap_update_bits(hw->regmap, ST_LSM6DSRX_REG_CTRL3_C_ADDR,
				 ST_LSM6DSRX_REG_BDU_MASK,
				 FIELD_PREP(ST_LSM6DSRX_REG_BDU_MASK, 1));
	if (err < 0)
		return err;

	err = regmap_update_bits(hw->regmap, ST_LSM6DSRX_REG_CTRL5_C_ADDR,
				 ST_LSM6DSRX_REG_ROUNDING_MASK,
				 FIELD_PREP(ST_LSM6DSRX_REG_ROUNDING_MASK, 3));
	if (err < 0)
		return err;

	err = st_lsm6dsrx_init_timestamp_engine(hw, true);
	if (err < 0)
		return err;

	err = st_lsm6dsrx_get_int_reg(hw, &drdy_reg);
	if (err < 0)
		return err;

	/* Enable DRDY MASK for filters settling time */
	err = regmap_update_bits(hw->regmap, ST_LSM6DSRX_REG_CTRL4_C_ADDR,
				 ST_LSM6DSRX_REG_DRDY_MASK,
				 FIELD_PREP(ST_LSM6DSRX_REG_DRDY_MASK, 1));

	if (err < 0)
		return err;

	/* enable interrupt on FIFO watermak */
	return regmap_update_bits(hw->regmap, drdy_reg,
				  ST_LSM6DSRX_REG_FIFO_TH_MASK,
				  FIELD_PREP(ST_LSM6DSRX_REG_FIFO_TH_MASK, 1));
}

static struct iio_dev *st_lsm6dsrx_alloc_iiodev(struct st_lsm6dsrx_hw *hw,
						enum st_lsm6dsrx_sensor_id id)
{
	struct st_lsm6dsrx_sensor *sensor;
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

	/* set default FS to each sensor */
	sensor->gain = st_lsm6dsrx_fs_table[id].fs_avl[0].gain;

	switch (id) {
	case ST_LSM6DSRX_ID_ACC:
		iio_dev->channels = st_lsm6dsrx_acc_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_lsm6dsrx_acc_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			 "%s_accel", hw->settings->id.name);
		iio_dev->info = &st_lsm6dsrx_acc_info;
		iio_dev->available_scan_masks =
					st_lsm6dsrx_available_scan_masks;
		sensor->max_watermark = ST_LSM6DSRX_MAX_FIFO_DEPTH;
		sensor->offset = 0;
		sensor->pm = ST_LSM6DSRX_HP_MODE;
		sensor->odr = st_lsm6dsrx_odr_table[id].odr_avl[1].hz;
		sensor->uodr = st_lsm6dsrx_odr_table[id].odr_avl[1].uhz;
		sensor->min_st = ST_LSM6DSRX_SELFTEST_ACCEL_MIN;
		sensor->max_st = ST_LSM6DSRX_SELFTEST_ACCEL_MAX;
		break;
	case ST_LSM6DSRX_ID_GYRO:
		iio_dev->channels = st_lsm6dsrx_gyro_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_lsm6dsrx_gyro_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			  "%s_gyro", hw->settings->id.name);
		iio_dev->info = &st_lsm6dsrx_gyro_info;
		iio_dev->available_scan_masks =
					st_lsm6dsrx_available_scan_masks;
		sensor->max_watermark = ST_LSM6DSRX_MAX_FIFO_DEPTH;
		sensor->offset = 0;
		sensor->pm = ST_LSM6DSRX_HP_MODE;
		sensor->odr = st_lsm6dsrx_odr_table[id].odr_avl[1].hz;
		sensor->uodr = st_lsm6dsrx_odr_table[id].odr_avl[1].uhz;
		sensor->min_st = ST_LSM6DSRX_SELFTEST_GYRO_MIN;
		sensor->max_st = ST_LSM6DSRX_SELFTEST_GYRO_MAX;
		break;
	case ST_LSM6DSRX_ID_TEMP:
		iio_dev->channels = st_lsm6dsrx_temp_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_lsm6dsrx_temp_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			  "%s_temp", hw->settings->id.name);
		iio_dev->info = &st_lsm6dsrx_temp_info;
		iio_dev->available_scan_masks =
					st_lsm6dsrx_temp_available_scan_masks;
		sensor->max_watermark = ST_LSM6DSRX_MAX_FIFO_DEPTH;
		sensor->offset = ST_LSM6DSRX_TEMP_OFFSET;
		sensor->pm = ST_LSM6DSRX_NO_MODE;
		sensor->odr = st_lsm6dsrx_odr_table[id].odr_avl[1].hz;
		sensor->uodr = st_lsm6dsrx_odr_table[id].odr_avl[1].uhz;
		break;
	default:
		return NULL;
	}

	iio_dev->name = sensor->name;

	return iio_dev;
}

static void st_lsm6dsrx_get_properties(struct st_lsm6dsrx_hw *hw)
{
	if (device_property_read_u32(hw->dev, "st,module_id",
				     &hw->module_id)) {
		hw->module_id = 1;
	}
}

static void st_lsm6dsrx_disable_regulator_action(void *_data)
{
	struct st_lsm6dsrx_hw *hw = _data;

	regulator_disable(hw->vddio_supply);
	regulator_disable(hw->vdd_supply);
}

static int st_lsm6dsrx_power_enable(struct st_lsm6dsrx_hw *hw)
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
				       st_lsm6dsrx_disable_regulator_action,
				       hw);
	if (err) {
		dev_err(hw->dev,
			"Failed to setup regulator cleanup action %d\n",
			err);
		return err;
	}

	return 0;
}

int st_lsm6dsrx_probe(struct device *dev, int irq, int hw_id,
		      struct regmap *regmap)
{
	struct st_lsm6dsrx_hw *hw;
	int i, err;

	hw = devm_kzalloc(dev, sizeof(*hw), GFP_KERNEL);
	if (!hw)
		return -ENOMEM;

	dev_set_drvdata(dev, (void *)hw);

	mutex_init(&hw->fifo_lock);
	mutex_init(&hw->page_lock);

	hw->regmap = regmap;
	hw->dev = dev;
	hw->irq = irq;
	hw->odr_table_entry = st_lsm6dsrx_odr_table;

	err = st_lsm6dsrx_power_enable(hw);
	if (err != 0)
		return err;

	err = st_lsm6dsrx_set_page_0(hw);
	if (err < 0)
		return err;

	err = st_lsm6dsrx_check_whoami(hw, hw_id);
	if (err < 0)
		return err;

	st_lsm6dsrx_get_properties(hw);

	err = st_lsm6dsrx_get_odr_calibration(hw);
	if (err < 0)
		return err;

	err = st_lsm6dsrx_reset_device(hw);
	if (err < 0)
		return err;

	err = st_lsm6dsrx_init_device(hw);
	if (err < 0)
		return err;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,15,0)
	err = iio_read_mount_matrix(dev, &hw->orientation);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5,2,0)
	err = iio_read_mount_matrix(dev, "mount-matrix", &hw->orientation);
#else /* LINUX_VERSION_CODE */
	err = of_iio_read_mount_matrix(dev, "mount-matrix", &hw->orientation);
#endif /* LINUX_VERSION_CODE */

	if (err) {
		dev_err(dev, "Failed to retrieve mounting matrix %d\n", err);
		return err;
	}

	/* register only main data sensors */
	for (i = 0; i <= ST_LSM6DSRX_ID_TEMP; i++) {
		hw->iio_devs[i] = st_lsm6dsrx_alloc_iiodev(hw, i);
		if (!hw->iio_devs[i])
			return -ENOMEM;
	}

	err = st_lsm6dsrx_shub_probe(hw);
	if (err < 0)
		return err;

	/* allocate step counter before buffer setup because use FIFO */
	err = st_lsm6dsrx_probe_embfunc(hw);
	if (err < 0)
		return err;

	err = st_lsm6dsrx_probe_event(hw);
	if (err < 0)
		return err;

	if (hw->irq > 0) {
		err = st_lsm6dsrx_buffers_setup(hw);
		if (err < 0)
			return err;
	}

	if (st_lsm6dsrx_run_mlc_task(hw)) {
		err = st_lsm6dsrx_mlc_probe(hw);
		if (err < 0)
			return err;
	}

	for (i = 0; i < ST_LSM6DSRX_ID_MAX; i++) {
		if (!hw->iio_devs[i])
			continue;

		err = devm_iio_device_register(hw->dev, hw->iio_devs[i]);
		if (err)
			return err;
	}

	if (st_lsm6dsrx_run_mlc_task(hw)) {
		err = st_lsm6dsrx_mlc_init_preload(hw);
		if (err)
			return err;
	}

	device_init_wakeup(dev,
			   device_property_read_bool(dev, "wakeup-source"));

	return 0;
}
EXPORT_SYMBOL(st_lsm6dsrx_probe);

static int __maybe_unused st_lsm6dsrx_suspend(struct device *dev)
{
	struct st_lsm6dsrx_hw *hw = dev_get_drvdata(dev);
	struct st_lsm6dsrx_sensor *sensor;
	int i, err = 0;

	for (i = 0; i < ST_LSM6DSRX_ID_MAX; i++) {
		sensor = iio_priv(hw->iio_devs[i]);
		if (!hw->iio_devs[i])
			continue;

		if (!(hw->enable_mask & BIT_ULL(sensor->id)))
			continue;

		if (device_may_wakeup(dev) &&
		    ((hw->enable_mask & BIT_ULL(sensor->id)) &
		     ST_LSM6DSRX_WAKE_UP_SENSORS)) {
			/* do not disable sensors if requested by wake-up */
			err = st_lsm6dsrx_set_odr(sensor,
						  ST_LSM6DSRX_MIN_ODR_IN_WAKEUP,
						  0);
			if (err < 0)
				return err;
		} else {
			err = st_lsm6dsrx_set_odr(sensor, 0, 0);
			if (err < 0)
				return err;
		}
	}

	if (st_lsm6dsrx_is_fifo_enabled(hw)) {
		err = st_lsm6dsrx_suspend_fifo(hw);
		if (err < 0)
			return err;
	}

	err = st_lsm6dsrx_bk_regs(hw);

	if (device_may_wakeup(dev) &&
	    (hw->enable_mask & ST_LSM6DSRX_WAKE_UP_SENSORS))
		enable_irq_wake(hw->irq);

	dev_info(dev, "Suspending device\n");

	return err < 0 ? err : 0;
}

static int __maybe_unused st_lsm6dsrx_resume(struct device *dev)
{
	struct st_lsm6dsrx_hw *hw = dev_get_drvdata(dev);
	struct st_lsm6dsrx_sensor *sensor;
	int i, err = 0;

	dev_info(dev, "Resuming device\n");

	if (device_may_wakeup(dev))
		disable_irq_wake(hw->irq);

	err = st_lsm6dsrx_restore_regs(hw);
	if (err < 0)
		return err;

	for (i = 0; i < ST_LSM6DSRX_ID_MAX; i++) {
		sensor = iio_priv(hw->iio_devs[i]);
		if (!hw->iio_devs[i])
			continue;

		if (hw->enable_mask & BIT_ULL(sensor->id)) {
			err = st_lsm6dsrx_set_odr(sensor, sensor->odr,
						  sensor->uodr);
			if (err < 0)
				return err;
		}
	}

	if (st_lsm6dsrx_is_fifo_enabled(hw))
		err = st_lsm6dsrx_set_fifo_mode(hw, ST_LSM6DSRX_FIFO_CONT);

	return err < 0 ? err : 0;
}

const struct dev_pm_ops st_lsm6dsrx_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(st_lsm6dsrx_suspend, st_lsm6dsrx_resume)
};
EXPORT_SYMBOL(st_lsm6dsrx_pm_ops);

MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_DESCRIPTION("STMicroelectronics st_lsm6dsrx driver");
MODULE_LICENSE("GPL v2");
