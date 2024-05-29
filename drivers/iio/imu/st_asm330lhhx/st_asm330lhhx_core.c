// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_asm330lhhx sensor driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2019 STMicroelectronics Inc.
 */

#include <linux/kernel.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/version.h>

#include <linux/platform_data/st_sensors_pdata.h>

#include "st_asm330lhhx.h"

static struct st_asm330lhhx_selftest_table {
	char *string_mode;
	u8 accel_value;
	u8 gyro_value;
	u8 gyro_mask;
} st_asm330lhhx_selftest_table[] = {
	[0] = {
		.string_mode = "disabled",
		.accel_value = ST_ASM330LHHX_SELF_TEST_DISABLED_VAL,
		.gyro_value = ST_ASM330LHHX_SELF_TEST_DISABLED_VAL,
	},
	[1] = {
		.string_mode = "positive-sign",
		.accel_value = ST_ASM330LHHX_SELF_TEST_POS_SIGN_VAL,
		.gyro_value = ST_ASM330LHHX_SELF_TEST_POS_SIGN_VAL
	},
	[2] = {
		.string_mode = "negative-sign",
		.accel_value = ST_ASM330LHHX_SELF_TEST_NEG_ACCEL_SIGN_VAL,
		.gyro_value = ST_ASM330LHHX_SELF_TEST_NEG_GYRO_SIGN_VAL
	},
};

static const struct st_asm330lhhx_power_mode_table {
	char *string_mode;
	enum st_asm330lhhx_pm_t val;
} st_asm330lhhx_power_mode[] = {
	[0] = {
		.string_mode = "HP_MODE",
		.val = ST_ASM330LHHX_HP_MODE,
	},
	[1] = {
		.string_mode = "LP_MODE",
		.val = ST_ASM330LHHX_LP_MODE,
	},
};

static struct st_asm330lhhx_suspend_resume_entry
	st_asm330lhhx_suspend_resume[ST_ASM330LHHX_SUSPEND_RESUME_REGS] = {
	[ST_ASM330LHHX_CTRL1_XL_REG] = {
		.page = FUNC_CFG_ACCESS_0,
		.addr = ST_ASM330LHHX_CTRL1_XL_ADDR,
		.mask = GENMASK(3, 2),
	},
	[ST_ASM330LHHX_CTRL2_G_REG] = {
		.page = FUNC_CFG_ACCESS_0,
		.addr = ST_ASM330LHHX_CTRL2_G_ADDR,
		.mask = GENMASK(3, 2),
	},
	[ST_ASM330LHHX_REG_CTRL3_C_REG] = {
		.page = FUNC_CFG_ACCESS_0,
		.addr = ST_ASM330LHHX_REG_CTRL3_C_ADDR,
		.mask = ST_ASM330LHHX_REG_BDU_MASK	|
			ST_ASM330LHHX_REG_PP_OD_MASK	|
			ST_ASM330LHHX_REG_H_LACTIVE_MASK,
	},
	[ST_ASM330LHHX_REG_CTRL4_C_REG] = {
		.page = FUNC_CFG_ACCESS_0,
		.addr = ST_ASM330LHHX_REG_CTRL4_C_ADDR,
		.mask = ST_ASM330LHHX_REG_DRDY_MASK,
	},
	[ST_ASM330LHHX_REG_CTRL5_C_REG] = {
		.page = FUNC_CFG_ACCESS_0,
		.addr = ST_ASM330LHHX_REG_CTRL5_C_ADDR,
		.mask = ST_ASM330LHHX_REG_ROUNDING_MASK,
	},
	[ST_ASM330LHHX_REG_CTRL10_C_REG] = {
		.page = FUNC_CFG_ACCESS_0,
		.addr = ST_ASM330LHHX_REG_CTRL10_C_ADDR,
		.mask = ST_ASM330LHHX_REG_TIMESTAMP_EN_MASK,
	},
	[ST_ASM330LHHX_REG_TAP_CFG0_REG] = {
		.page = FUNC_CFG_ACCESS_0,
		.addr = ST_ASM330LHHX_REG_TAP_CFG0_ADDR,
		.mask = ST_ASM330LHHX_REG_LIR_MASK,
	},
	[ST_ASM330LHHX_REG_INT1_CTRL_REG] = {
		.page = FUNC_CFG_ACCESS_0,
		.addr = ST_ASM330LHHX_REG_INT1_CTRL_ADDR,
		.mask = ST_ASM330LHHX_REG_INT_FIFO_TH_MASK,
	},
	[ST_ASM330LHHX_REG_INT2_CTRL_REG] = {
		.page = FUNC_CFG_ACCESS_0,
		.addr = ST_ASM330LHHX_REG_INT2_CTRL_ADDR,
		.mask = ST_ASM330LHHX_REG_INT_FIFO_TH_MASK,
	},
	[ST_ASM330LHHX_REG_FIFO_CTRL1_REG] = {
		.page = FUNC_CFG_ACCESS_0,
		.addr = ST_ASM330LHHX_REG_FIFO_CTRL1_ADDR,
		.mask = GENMASK(7, 0),
	},
	[ST_ASM330LHHX_REG_FIFO_CTRL2_REG] = {
		.page = FUNC_CFG_ACCESS_0,
		.addr = ST_ASM330LHHX_REG_FIFO_CTRL2_ADDR,
		.mask = ST_ASM330LHHX_REG_FIFO_WTM8_MASK,
	},
	[ST_ASM330LHHX_REG_FIFO_CTRL3_REG] = {
		.page = FUNC_CFG_ACCESS_0,
		.addr = ST_ASM330LHHX_REG_FIFO_CTRL3_ADDR,
		.mask = ST_ASM330LHHX_REG_BDR_XL_MASK |
			ST_ASM330LHHX_REG_BDR_GY_MASK,
	},
	[ST_ASM330LHHX_REG_FIFO_CTRL4_REG] = {
		.page = FUNC_CFG_ACCESS_0,
		.addr = ST_ASM330LHHX_REG_FIFO_CTRL4_ADDR,
		.mask = ST_ASM330LHHX_REG_DEC_TS_MASK |
			ST_ASM330LHHX_REG_ODR_T_BATCH_MASK,
	},
	[ST_ASM330LHHX_REG_EMB_FUNC_EN_B_REG] = {
		.page = FUNC_CFG_ACCESS_FUNC_CFG,
		.addr = ST_ASM330LHHX_EMB_FUNC_EN_B_ADDR,
		.mask = ST_ASM330LHHX_FSM_EN_MASK |
			ST_ASM330LHHX_MLC_EN_MASK,
	},
	[ST_ASM330LHHX_REG_FSM_INT1_A_REG] = {
		.page = FUNC_CFG_ACCESS_FUNC_CFG,
		.addr = ST_ASM330LHHX_FSM_INT1_A_ADDR,
		.mask = GENMASK(7, 0),
	},
	[ST_ASM330LHHX_REG_FSM_INT1_B_REG] = {
		.page = FUNC_CFG_ACCESS_FUNC_CFG,
		.addr = ST_ASM330LHHX_FSM_INT1_B_ADDR,
		.mask = GENMASK(7, 0),
	},
	[ST_ASM330LHHX_REG_MLC_INT1_REG] = {
		.page = FUNC_CFG_ACCESS_FUNC_CFG,
		.addr = ST_ASM330LHHX_MLC_INT1_ADDR,
		.mask = GENMASK(7, 0),
	},
	[ST_ASM330LHHX_REG_FSM_INT2_A_REG] = {
		.page = FUNC_CFG_ACCESS_FUNC_CFG,
		.addr = ST_ASM330LHHX_FSM_INT2_A_ADDR,
		.mask = GENMASK(7, 0),
	},
	[ST_ASM330LHHX_REG_FSM_INT2_B_REG] = {
		.page = FUNC_CFG_ACCESS_FUNC_CFG,
		.addr = ST_ASM330LHHX_FSM_INT2_B_ADDR,
		.mask = GENMASK(7, 0),
	},
	[ST_ASM330LHHX_REG_MLC_INT2_REG] = {
		.page = FUNC_CFG_ACCESS_FUNC_CFG,
		.addr = ST_ASM330LHHX_MLC_INT2_ADDR,
		.mask = GENMASK(7, 0),
	},
};

static const struct st_asm330lhhx_odr_table_entry st_asm330lhhx_odr_table[] = {
	[ST_ASM330LHHX_ID_ACC] = {
		.size = 7,
		.reg = {
			.addr = ST_ASM330LHHX_CTRL1_XL_ADDR,
			.mask = GENMASK(7, 4),
		},
		.pm = {
			.addr = ST_ASM330LHHX_REG_CTRL6_C_ADDR,
			.mask = ST_ASM330LHHX_REG_XL_HM_MODE_MASK,
		},
		.batching_reg = {
			.addr = ST_ASM330LHHX_REG_FIFO_CTRL3_ADDR,
			.mask = GENMASK(3, 0),
		},
		.odr_avl[0] = {  12, 500000,  0x01,  0x01 },
		.odr_avl[1] = {  26,      0,  0x02,  0x02 },
		.odr_avl[2] = {  52,      0,  0x03,  0x03 },
		.odr_avl[3] = { 104,      0,  0x04,  0x04 },
		.odr_avl[4] = { 208,      0,  0x05,  0x05 },
		.odr_avl[5] = { 416,      0,  0x06,  0x06 },
		.odr_avl[6] = { 833,      0,  0x07,  0x07 },
	},
	[ST_ASM330LHHX_ID_GYRO] = {
		.size = 7,
		.reg = {
			.addr = ST_ASM330LHHX_CTRL2_G_ADDR,
			.mask = GENMASK(7, 4),
		},
		.pm = {
			.addr = ST_ASM330LHHX_REG_CTRL7_G_ADDR,
			.mask = ST_ASM330LHHX_REG_G_HM_MODE_MASK,
		},
		.batching_reg = {
			.addr = ST_ASM330LHHX_REG_FIFO_CTRL3_ADDR,
			.mask = GENMASK(7, 4),
		},
		.odr_avl[0] = {  12, 500000,  0x01,  0x01 },
		.odr_avl[1] = {  26,      0,  0x02,  0x02 },
		.odr_avl[2] = {  52,      0,  0x03,  0x03 },
		.odr_avl[3] = { 104,      0,  0x04,  0x04 },
		.odr_avl[4] = { 208,      0,  0x05,  0x05 },
		.odr_avl[5] = { 416,      0,  0x06,  0x06 },
		.odr_avl[6] = { 833,      0,  0x07,  0x07 },
	},
	[ST_ASM330LHHX_ID_TEMP] = {
		.size = 2,
		.batching_reg = {
			.addr = ST_ASM330LHHX_REG_FIFO_CTRL4_ADDR,
			.mask = GENMASK(5, 4),
		},
		.odr_avl[0] = { 12, 500000,   0x02,  0x02 },
		.odr_avl[1] = { 52,      0,   0x03,  0x03 },
	},
};

/**
 * List of supported supported device settings
 *
 * The following table list all device features in terms of supported
 * MLC and SHUB.
 */
static const struct st_asm330lhhx_settings st_asm330lhhx_sensor_settings[] = {
	{
		.id = {
			.hw_id = ST_ASM330LHHX_ID,
			.name = ST_ASM330LHHX_DEV_NAME,
		},
		.st_mlc_probe = true,
		.st_shub_probe = true,
		.st_power_mode = true,
	},
	{
		.id = {
			.hw_id = ST_ASM330LHH_ID,
			.name = ST_ASM330LHH_DEV_NAME,
		},
	},
	{
		.id = {
			.hw_id = ST_ASM330LHHXG1_ID,
			.name = ST_ASM330LHHXG1_DEV_NAME,
		},
		.st_mlc_probe = true,
		.st_shub_probe = true,
		.st_power_mode = true,
	},
	{
		.id = {
			.hw_id = ST_ASM330LHB_ID,
			.name = ST_ASM330LHB_DEV_NAME,
		},
		.st_mlc_probe = true,
		.st_shub_probe = true,
		.st_power_mode = true,
	},
};

static const struct st_asm330lhhx_fs_table_entry st_asm330lhhx_fs_table[] = {
	[ST_ASM330LHHX_ID_ACC] = {
		.size = ST_ASM330LHHX_FS_ACC_LIST_SIZE,
		.fs_avl[0] = {
			.reg = {
				.addr = ST_ASM330LHHX_CTRL1_XL_ADDR,
				.mask = GENMASK(3, 2),
			},
			.gain = ST_ASM330LHHX_ACC_FS_2G_GAIN,
			.val = 0x0,
		},
		.fs_avl[1] = {
			.reg = {
				.addr = ST_ASM330LHHX_CTRL1_XL_ADDR,
				.mask = GENMASK(3, 2),
			},
			.gain = ST_ASM330LHHX_ACC_FS_4G_GAIN,
			.val = 0x2,
		},
		.fs_avl[2] = {
			.reg = {
				.addr = ST_ASM330LHHX_CTRL1_XL_ADDR,
				.mask = GENMASK(3, 2),
			},
			.gain = ST_ASM330LHHX_ACC_FS_8G_GAIN,
			.val = 0x3,
		},
		.fs_avl[3] = {
			.reg = {
				.addr = ST_ASM330LHHX_CTRL1_XL_ADDR,
				.mask = GENMASK(3, 2),
			},
			.gain = ST_ASM330LHHX_ACC_FS_16G_GAIN,
			.val = 0x1,
		},
	},
	[ST_ASM330LHHX_ID_GYRO] = {
		.size = ST_ASM330LHHX_FS_GYRO_LIST_SIZE,
		.fs_avl[0] = {
			.reg = {
				.addr = ST_ASM330LHHX_CTRL2_G_ADDR,
				.mask = GENMASK(3, 0),
			},
			.gain = ST_ASM330LHHX_GYRO_FS_125_GAIN,
			.val = 0x02,
		},
		.fs_avl[1] = {
			.reg = {
				.addr = ST_ASM330LHHX_CTRL2_G_ADDR,
				.mask = GENMASK(3, 0),
			},
			.gain = ST_ASM330LHHX_GYRO_FS_250_GAIN,
			.val = 0x0,
		},
		.fs_avl[2] = {
			.reg = {
				.addr = ST_ASM330LHHX_CTRL2_G_ADDR,
				.mask = GENMASK(3, 0),
			},
			.gain = ST_ASM330LHHX_GYRO_FS_500_GAIN,
			.val = 0x4,
		},
		.fs_avl[3] = {
			.reg = {
				.addr = ST_ASM330LHHX_CTRL2_G_ADDR,
				.mask = GENMASK(3, 0),
			},
			.gain = ST_ASM330LHHX_GYRO_FS_1000_GAIN,
			.val = 0x8,
		},
		.fs_avl[4] = {
			.reg = {
				.addr = ST_ASM330LHHX_CTRL2_G_ADDR,
				.mask = GENMASK(3, 0),
			},
			.gain = ST_ASM330LHHX_GYRO_FS_2000_GAIN,
			.val = 0x0C,
		},
		.fs_avl[5] = {
			.reg = {
				.addr = ST_ASM330LHHX_CTRL2_G_ADDR,
				.mask = GENMASK(3, 0),
			},
			.gain = ST_ASM330LHHX_GYRO_FS_4000_GAIN,
			.val = 0x1,
		},
	},
	[ST_ASM330LHHX_ID_TEMP] = {
		.size = ST_ASM330LHHX_FS_TEMP_LIST_SIZE,
		.fs_avl[0] = {
			.gain = ST_ASM330LHHX_TEMP_FS_GAIN,
			.val = 0x0
		},
	},
};

#ifdef CONFIG_IIO_ST_ASM330LHHX_EN_BASIC_FEATURES
static const struct st_asm330lhhx_ff_th st_asm330lhhx_free_fall_threshold[] = {
	[0] = {
		.val = 0x00,
		.mg = 156,
	},
	[1] = {
		.val = 0x01,
		.mg = 219,
	},
	[2] = {
		.val = 0x02,
		.mg = 250,
	},
	[3] = {
		.val = 0x03,
		.mg = 312,
	},
	[4] = {
		.val = 0x04,
		.mg = 344,
	},
	[5] = {
		.val = 0x05,
		.mg = 406,
	},
	[6] = {
		.val = 0x06,
		.mg = 469,
	},
	[7] = {
		.val = 0x07,
		.mg = 500,
	},
};

static const struct st_asm330lhhx_6D_th st_asm330lhhx_6D_threshold[] = {
	[0] = {
		.val = 0x00,
		.deg = 80,
	},
	[1] = {
		.val = 0x01,
		.deg = 70,
	},
	[2] = {
		.val = 0x02,
		.deg = 60,
	},
	[3] = {
		.val = 0x03,
		.deg = 50,
	},
};
#endif /* CONFIG_IIO_ST_ASM330LHHX_EN_BASIC_FEATURES */

static const inline struct iio_mount_matrix *
st_asm330lhhx_get_mount_matrix(const struct iio_dev *iio_dev,
			      const struct iio_chan_spec *chan)
{
	struct st_asm330lhhx_sensor *sensor = iio_priv(iio_dev);
	struct st_asm330lhhx_hw *hw = sensor->hw;

	return &hw->orientation;
}

static const struct iio_chan_spec_ext_info st_asm330lhhx_ext_info[] = {
	IIO_MOUNT_MATRIX(IIO_SHARED_BY_ALL, st_asm330lhhx_get_mount_matrix),
	{},
};

#define IIO_CHAN_HW_TIMESTAMP(si) {					\
	.type = IIO_COUNT,						\
	.address = ST_ASM330LHHX_REG_TIMESTAMP0_ADDR,			\
	.scan_index = si,						\
	.scan_type = {							\
		.sign = 's',						\
		.realbits = 64,						\
		.storagebits = 64,					\
		.endianness = IIO_LE,					\
	},								\
}

static const struct iio_chan_spec st_asm330lhhx_acc_channels[] = {
	ST_ASM330LHHX_DATA_CHANNEL(IIO_ACCEL, ST_ASM330LHHX_REG_OUTX_L_A_ADDR,
				1, IIO_MOD_X, 0, 16, 16, 's', st_asm330lhhx_ext_info),
	ST_ASM330LHHX_DATA_CHANNEL(IIO_ACCEL, ST_ASM330LHHX_REG_OUTY_L_A_ADDR,
				1, IIO_MOD_Y, 1, 16, 16, 's', st_asm330lhhx_ext_info),
	ST_ASM330LHHX_DATA_CHANNEL(IIO_ACCEL, ST_ASM330LHHX_REG_OUTZ_L_A_ADDR,
				1, IIO_MOD_Z, 2, 16, 16, 's', st_asm330lhhx_ext_info),
	ST_ASM330LHHX_EVENT_CHANNEL(IIO_ACCEL, flush),
	IIO_CHAN_HW_TIMESTAMP(3),
	IIO_CHAN_SOFT_TIMESTAMP(4),
};

static const struct iio_chan_spec st_asm330lhhx_gyro_channels[] = {
	ST_ASM330LHHX_DATA_CHANNEL(IIO_ANGL_VEL, ST_ASM330LHHX_REG_OUTX_L_G_ADDR,
				1, IIO_MOD_X, 0, 16, 16, 's', st_asm330lhhx_ext_info),
	ST_ASM330LHHX_DATA_CHANNEL(IIO_ANGL_VEL, ST_ASM330LHHX_REG_OUTY_L_G_ADDR,
				1, IIO_MOD_Y, 1, 16, 16, 's', st_asm330lhhx_ext_info),
	ST_ASM330LHHX_DATA_CHANNEL(IIO_ANGL_VEL, ST_ASM330LHHX_REG_OUTZ_L_G_ADDR,
				1, IIO_MOD_Z, 2, 16, 16, 's', st_asm330lhhx_ext_info),
	ST_ASM330LHHX_EVENT_CHANNEL(IIO_ANGL_VEL, flush),
	IIO_CHAN_HW_TIMESTAMP(3),
	IIO_CHAN_SOFT_TIMESTAMP(4),
};

static
__maybe_unused const struct iio_chan_spec st_asm330lhhx_temp_channels[] = {
	{
		.type = IIO_TEMP,
		.address = ST_ASM330LHHX_REG_OUT_TEMP_L_ADDR,
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
	ST_ASM330LHHX_EVENT_CHANNEL(IIO_TEMP, flush),
	IIO_CHAN_HW_TIMESTAMP(1),
	IIO_CHAN_SOFT_TIMESTAMP(2),
};

int __maybe_unused st_asm330lhhx_read_with_mask(struct st_asm330lhhx_hw *hw, u8 addr, u8 mask,
				u8 *val)
{
	u8 data;
	int err;

	err = regmap_bulk_read(hw->regmap, addr, &data, sizeof(data));
	if (err < 0) {
		dev_err(hw->dev, "failed to read %02x register\n", addr);

		goto out;
	}

	*val = (data & mask) >> __ffs(mask);

out:
	return (err < 0) ? err : 0;
}

int st_asm330lhhx_of_get_pin(struct st_asm330lhhx_hw *hw, int *pin)
{
	if (!dev_fwnode(hw->dev))
		return -EINVAL;

	return device_property_read_u32(hw->dev, "st,int-pin", pin);
}

static int st_asm330lhhx_get_int_reg(struct st_asm330lhhx_hw *hw, u8 *drdy_reg)
{
	int err = 0, int_pin;

	if (st_asm330lhhx_of_get_pin(hw, &int_pin) < 0) {
		struct st_sensors_platform_data *pdata;
		struct device *dev = hw->dev;

		pdata = (struct st_sensors_platform_data *)dev->platform_data;
		int_pin = pdata ? pdata->drdy_int_pin : 1;
	}

	switch (int_pin) {
	case 1:
		*drdy_reg = ST_ASM330LHHX_REG_INT1_CTRL_ADDR;
		break;
	case 2:
		*drdy_reg = ST_ASM330LHHX_REG_INT2_CTRL_ADDR;
		break;
	default:
		dev_err(hw->dev, "unsupported interrupt pin\n");
		err = -EINVAL;
		break;
	}

	hw->int_pin = int_pin;

	return err;
}

static int __maybe_unused st_asm330lhhx_bk_regs(struct st_asm330lhhx_hw *hw)
{
	unsigned int data;
	bool restore = 0;
	int i, err = 0;

	mutex_lock(&hw->page_lock);

	for (i = 0; i < ST_ASM330LHHX_SUSPEND_RESUME_REGS; i++) {
		if (st_asm330lhhx_suspend_resume[i].page != FUNC_CFG_ACCESS_0) {
			/* skip if not support mlc */
			if (!hw->settings->st_mlc_probe)
				continue;

			err = regmap_update_bits(hw->regmap,
				     ST_ASM330LHHX_REG_FUNC_CFG_ACCESS_ADDR,
				     ST_ASM330LHHX_REG_ACCESS_MASK,
				     FIELD_PREP(ST_ASM330LHHX_REG_ACCESS_MASK,
					st_asm330lhhx_suspend_resume[i].page));
			if (err < 0) {
				dev_err(hw->dev,
					"failed to update %02x reg\n",
					st_asm330lhhx_suspend_resume[i].addr);
				break;
			}

			restore = 1;
		}

		err = regmap_read(hw->regmap,
				  st_asm330lhhx_suspend_resume[i].addr,
				  &data);
		if (err < 0) {
			dev_err(hw->dev,
				"failed to save register %02x\n",
				st_asm330lhhx_suspend_resume[i].addr);
			goto out_lock;
		}

		if (restore) {
			err = regmap_update_bits(hw->regmap,
				     ST_ASM330LHHX_REG_FUNC_CFG_ACCESS_ADDR,
				     ST_ASM330LHHX_REG_ACCESS_MASK,
				     FIELD_PREP(ST_ASM330LHHX_REG_ACCESS_MASK,
						    FUNC_CFG_ACCESS_0));
			if (err < 0) {
				dev_err(hw->dev,
					"failed to update %02x reg\n",
					st_asm330lhhx_suspend_resume[i].addr);
				break;
			}

			restore = 0;
		}

		st_asm330lhhx_suspend_resume[i].val = data;
	}

out_lock:
	mutex_unlock(&hw->page_lock);

	return err;
}

static int __maybe_unused st_asm330lhhx_restore_regs(struct st_asm330lhhx_hw *hw)
{
	bool restore = 0;
	int i, err = 0;

	mutex_lock(&hw->page_lock);

	for (i = 0; i < ST_ASM330LHHX_SUSPEND_RESUME_REGS; i++) {
		if (st_asm330lhhx_suspend_resume[i].page != FUNC_CFG_ACCESS_0) {
			/* skip if not support mlc */
			if (!hw->settings->st_mlc_probe)
				continue;

			err = regmap_update_bits(hw->regmap,
				     ST_ASM330LHHX_REG_FUNC_CFG_ACCESS_ADDR,
				     ST_ASM330LHHX_REG_ACCESS_MASK,
				     FIELD_PREP(ST_ASM330LHHX_REG_ACCESS_MASK,
					st_asm330lhhx_suspend_resume[i].page));
			if (err < 0) {
				dev_err(hw->dev,
					"failed to update %02x reg\n",
					st_asm330lhhx_suspend_resume[i].addr);
				break;
			}

			restore = 1;
		}

		err = regmap_update_bits(hw->regmap,
					 st_asm330lhhx_suspend_resume[i].addr,
					 st_asm330lhhx_suspend_resume[i].mask,
					 st_asm330lhhx_suspend_resume[i].val);
		if (err < 0) {
			dev_err(hw->dev,
				"failed to update %02x reg\n",
				st_asm330lhhx_suspend_resume[i].addr);
			break;
		}

		if (restore) {
			err = regmap_update_bits(hw->regmap,
				     ST_ASM330LHHX_REG_FUNC_CFG_ACCESS_ADDR,
				     ST_ASM330LHHX_REG_ACCESS_MASK,
				     FIELD_PREP(ST_ASM330LHHX_REG_ACCESS_MASK,
						    FUNC_CFG_ACCESS_0));
			if (err < 0) {
				dev_err(hw->dev,
					"failed to update %02x reg\n",
					st_asm330lhhx_suspend_resume[i].addr);
				break;
			}

			restore = 0;
		}
	}

	mutex_unlock(&hw->page_lock);

	return err;
}

static int st_asm330lhhx_set_selftest(
				struct st_asm330lhhx_sensor *sensor, int index)
{
	u8 mode, mask;

	switch (sensor->id) {
	case ST_ASM330LHHX_ID_ACC:
		mask = ST_ASM330LHHX_REG_ST_XL_MASK;
		mode = st_asm330lhhx_selftest_table[index].accel_value;
		break;
	case ST_ASM330LHHX_ID_GYRO:
		mask = ST_ASM330LHHX_REG_ST_G_MASK;
		mode = st_asm330lhhx_selftest_table[index].gyro_value;
		break;
	default:
		return -EINVAL;
	}

	return st_asm330lhhx_update_bits_locked(sensor->hw,
					    ST_ASM330LHHX_REG_CTRL5_C_ADDR,
					    mask, mode);
}

static ssize_t st_asm330lhhx_sysfs_get_selftest_available(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s, %s\n",
		       st_asm330lhhx_selftest_table[1].string_mode,
		       st_asm330lhhx_selftest_table[2].string_mode);
}

static ssize_t st_asm330lhhx_sysfs_get_selftest_status(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int8_t result;
	char *message = NULL;
	struct st_asm330lhhx_sensor *sensor = iio_priv(dev_to_iio_dev(dev));
	enum st_asm330lhhx_sensor_id id = sensor->id;

	if (id != ST_ASM330LHHX_ID_ACC &&
	    id != ST_ASM330LHHX_ID_GYRO)
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

#ifdef CONFIG_IIO_ST_ASM330LHHX_EN_BASIC_FEATURES
/*
 * st_asm330lhhx_set_wake_up_thershold - set wake-up threshold in ug
 * @hw - ST IMU MEMS hw instance
 * @th_ug - wake-up threshold in ug (micro g)
 *
 * wake-up thershold register val = (th_ug * 2 ^ 6) / (1000000 * FS_XL)
 */
int st_asm330lhhx_set_wake_up_thershold(struct st_asm330lhhx_hw *hw, int th_ug)
{
	struct st_asm330lhhx_sensor *sensor;
	struct iio_dev *iio_dev;
	u8 val, fs_xl, max_th;
	int tmp, err;

	err = st_asm330lhhx_read_with_mask(hw,
		st_asm330lhhx_fs_table[ST_ASM330LHHX_ID_ACC].fs_avl[0].reg.addr,
		st_asm330lhhx_fs_table[ST_ASM330LHHX_ID_ACC].fs_avl[0].reg.mask,
		&fs_xl);
	if (err < 0)
		return err;

	tmp = (th_ug * 64) / (fs_xl * 1000000);
	val = (u8)tmp;
	max_th = ST_ASM330LHHX_WAKE_UP_THS_MASK >>
		  __ffs(ST_ASM330LHHX_WAKE_UP_THS_MASK);
	if (val > max_th)
		val = max_th;

	err = st_asm330lhhx_write_with_mask_locked(hw,
					   ST_ASM330LHHX_REG_WAKE_UP_THS_ADDR,
					   ST_ASM330LHHX_WAKE_UP_THS_MASK, val);
	if (err < 0)
		return err;

	iio_dev = hw->iio_devs[ST_ASM330LHHX_ID_WK];
	sensor = iio_priv(iio_dev);
	sensor->conf[0] = th_ug;

	return 0;
}

/*
 * st_asm330lhhx_set_wake_up_duration - set wake-up duration in ms
 * @hw - ST IMU MEMS hw instance
 * @dur_ms - wake-up duration in ms
 *
 * wake-up duration register val is related to XL ODR
 */
int st_asm330lhhx_set_wake_up_duration(struct st_asm330lhhx_hw *hw, int dur_ms)
{
	struct st_asm330lhhx_sensor *sensor;
	struct iio_dev *iio_dev;
	int i, tmp, sensor_odr, err;
	u8 val, odr_xl, max_dur;

	err = st_asm330lhhx_read_with_mask(hw,
		st_asm330lhhx_odr_table[ST_ASM330LHHX_ID_ACC].reg.addr,
		st_asm330lhhx_odr_table[ST_ASM330LHHX_ID_ACC].reg.mask,
		&odr_xl);
	if (err < 0)
		return err;

	if (odr_xl == 0) {
		dev_info(hw->dev, "use default ODR\n");
		odr_xl = st_asm330lhhx_odr_table[ST_ASM330LHHX_ID_ACC].odr_avl[ST_ASM330LHHX_DEFAULT_XL_ODR_INDEX].val;
	}

	for (i = 0; i < st_asm330lhhx_odr_table[ST_ASM330LHHX_ID_ACC].size; i++) {
		if (odr_xl ==
		     st_asm330lhhx_odr_table[ST_ASM330LHHX_ID_ACC].odr_avl[i].val)
			break;
	}

	if (i == st_asm330lhhx_odr_table[ST_ASM330LHHX_ID_ACC].size)
		return -EINVAL;


	sensor_odr = ST_ASM330LHHX_ODR_EXPAND(
		st_asm330lhhx_odr_table[ST_ASM330LHHX_ID_ACC].odr_avl[i].hz,
		st_asm330lhhx_odr_table[ST_ASM330LHHX_ID_ACC].odr_avl[i].uhz);

	tmp = dur_ms / (1000000 / (sensor_odr / 1000));
	val = (u8)tmp;
	max_dur = ST_ASM330LHHX_WAKE_UP_DUR_MASK >>
		  __ffs(ST_ASM330LHHX_WAKE_UP_DUR_MASK);
	if (val > max_dur)
		val = max_dur;

	err = st_asm330lhhx_write_with_mask_locked(hw,
				     ST_ASM330LHHX_REG_WAKE_UP_DUR_ADDR,
				     ST_ASM330LHHX_WAKE_UP_DUR_MASK,
				     val);
	if (err < 0)
		return err;

	iio_dev = hw->iio_devs[ST_ASM330LHHX_ID_WK];
	sensor = iio_priv(iio_dev);
	sensor->conf[1] = dur_ms;

	return 0;
}

/*
 * st_asm330lhhx_set_freefall_threshold - set free fall threshold detection mg
 * @hw - ST IMU MEMS hw instance
 * @th_mg - free fall threshold in mg
 */
int st_asm330lhhx_set_freefall_threshold(struct st_asm330lhhx_hw *hw, int th_mg)
{
	struct st_asm330lhhx_sensor *sensor;
	struct iio_dev *iio_dev;
	int i, err;

	for (i = 0; i < ARRAY_SIZE(st_asm330lhhx_free_fall_threshold); i++) {
		if (th_mg >= st_asm330lhhx_free_fall_threshold[i].mg)
			break;
	}

	if (i == ARRAY_SIZE(st_asm330lhhx_free_fall_threshold))
		return -EINVAL;

	err = st_asm330lhhx_write_with_mask_locked(hw,
			      ST_ASM330LHHX_REG_FREE_FALL_ADDR,
			      ST_ASM330LHHX_FF_THS_MASK,
			      st_asm330lhhx_free_fall_threshold[i].val);
	if (err < 0)
		return err;

	iio_dev = hw->iio_devs[ST_ASM330LHHX_ID_FF];
	sensor = iio_priv(iio_dev);
	sensor->conf[2] = th_mg;

	return 0;
}

/*
 * st_asm330lhhx_set_6D_threshold - set 6D threshold detection in degrees
 * @hw - ST IMU MEMS hw instance
 * @deg - 6D threshold in degrees
 */
int st_asm330lhhx_set_6D_threshold(struct st_asm330lhhx_hw *hw, int deg)
{
	struct st_asm330lhhx_sensor *sensor;
	struct iio_dev *iio_dev;
	int i, err;

	for (i = 0; i < ARRAY_SIZE(st_asm330lhhx_6D_threshold); i++) {
		if (deg >= st_asm330lhhx_6D_threshold[i].deg)
			break;
	}

	if (i == ARRAY_SIZE(st_asm330lhhx_6D_threshold))
		return -EINVAL;

	err = st_asm330lhhx_write_with_mask_locked(hw,
				     ST_ASM330LHHX_REG_THS_6D_ADDR,
				     ST_ASM330LHHX_SIXD_THS_MASK,
				     st_asm330lhhx_6D_threshold[i].val);
	if (err < 0)
		return err;

	iio_dev = hw->iio_devs[ST_ASM330LHHX_ID_6D];
	sensor = iio_priv(iio_dev);
	sensor->conf[3] = deg;

	return 0;
}
#endif /* CONFIG_IIO_ST_ASM330LHHX_EN_BASIC_FEATURES */

static __maybe_unused int st_asm330lhhx_reg_access(struct iio_dev *iio_dev,
				 unsigned int reg, unsigned int writeval,
				 unsigned int *readval)
{
	struct st_asm330lhhx_sensor *sensor = iio_priv(iio_dev);
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

static int st_asm330lhhx_set_page_0(struct st_asm330lhhx_hw *hw)
{
	return regmap_write(hw->regmap,
			    ST_ASM330LHHX_REG_FUNC_CFG_ACCESS_ADDR, 0);
}

static int st_asm330lhhx_check_whoami(struct st_asm330lhhx_hw *hw,
				      int id)
{
	int err, i;
	int data;

	for (i = 0; i < ARRAY_SIZE(st_asm330lhhx_sensor_settings); i++) {
			if (st_asm330lhhx_sensor_settings[i].id.name &&
			    st_asm330lhhx_sensor_settings[i].id.hw_id == id)
				break;
	}

	if (i == ARRAY_SIZE(st_asm330lhhx_sensor_settings)) {
		dev_err(hw->dev, "unsupported hw id [%02x]\n", id);

		return -ENODEV;
	}

	err = regmap_read(hw->regmap, ST_ASM330LHHX_REG_WHOAMI_ADDR, &data);
	if (err < 0) {
		dev_err(hw->dev, "failed to read whoami register\n");
		return err;
	}

	if (data != ST_ASM330LHHX_WHOAMI_VAL) {
		dev_err(hw->dev, "unsupported whoami [%02x]\n", data);
		return -ENODEV;
	}

	hw->settings = &st_asm330lhhx_sensor_settings[i];

	return 0;
}

static int st_asm330lhhx_get_odr_calibration(struct st_asm330lhhx_hw *hw)
{
	s64 odr_calib;
	int data;
	int err;

	err = regmap_read(hw->regmap, ST_ASM330LHHX_INTERNAL_FREQ_FINE,
			  &data);
	if (err < 0) {
		dev_err(hw->dev, "failed to read %d register\n",
				ST_ASM330LHHX_INTERNAL_FREQ_FINE);
		return err;
	}

	odr_calib = ((s8)data * 37500) / 1000;
	hw->ts_delta_ns = ST_ASM330LHHX_TS_DELTA_NS - odr_calib;

	dev_info(hw->dev, "Freq Fine %lld (ts %lld)\n", odr_calib, hw->ts_delta_ns);

	return 0;
}

static int st_asm330lhhx_set_full_scale(struct st_asm330lhhx_sensor *sensor,
				     u32 gain)
{
	enum st_asm330lhhx_sensor_id id = sensor->id;
	struct st_asm330lhhx_hw *hw = sensor->hw;
	int i, err;
	u8 val;

	/* for other sensors gain is fixed */
	if (id > ST_ASM330LHHX_ID_ACC)
		return 0;

	for (i = 0; i < st_asm330lhhx_fs_table[id].size; i++)
		if (st_asm330lhhx_fs_table[id].fs_avl[i].gain >= gain)
			break;

	if (i == st_asm330lhhx_fs_table[id].size)
		return -EINVAL;

	val = st_asm330lhhx_fs_table[id].fs_avl[i].val;
	err = regmap_update_bits(hw->regmap,
			st_asm330lhhx_fs_table[id].fs_avl[i].reg.addr,
			st_asm330lhhx_fs_table[id].fs_avl[i].reg.mask,
			ST_ASM330LHHX_SHIFT_VAL(val,
			    st_asm330lhhx_fs_table[id].fs_avl[i].reg.mask));
	if (err < 0)
		return err;

	sensor->gain = st_asm330lhhx_fs_table[id].fs_avl[i].gain;

	return 0;
}

int st_asm330lhhx_get_odr_val(enum st_asm330lhhx_sensor_id id, int odr,
			      int uodr, int *podr, int *puodr, u8 *val)
{
	int required_odr = ST_ASM330LHHX_ODR_EXPAND(odr, uodr);
	int sensor_odr;
	int i;

	for (i = 0; i < st_asm330lhhx_odr_table[id].size; i++) {
		sensor_odr = ST_ASM330LHHX_ODR_EXPAND(
				st_asm330lhhx_odr_table[id].odr_avl[i].hz,
				st_asm330lhhx_odr_table[id].odr_avl[i].uhz);
		if (sensor_odr >= required_odr)
			break;
	}

	if (i == st_asm330lhhx_odr_table[id].size)
		return -EINVAL;

	*val = st_asm330lhhx_odr_table[id].odr_avl[i].val;

	if (podr && puodr) {
		*podr = st_asm330lhhx_odr_table[id].odr_avl[i].hz;
		*puodr = st_asm330lhhx_odr_table[id].odr_avl[i].uhz;
	}

	return 0;
}

int __maybe_unused
st_asm330lhhx_get_odr_from_reg(enum st_asm330lhhx_sensor_id id,
			       u8 reg_val, u16 *podr, u32 *puodr)
{
	int i;

	for (i = 0; i < st_asm330lhhx_odr_table[id].size; i++) {
		if (reg_val == st_asm330lhhx_odr_table[id].odr_avl[i].val)
			break;
	}

	if (i == st_asm330lhhx_odr_table[id].size)
		return -EINVAL;

	*podr = st_asm330lhhx_odr_table[id].odr_avl[i].hz;
	*puodr = st_asm330lhhx_odr_table[id].odr_avl[i].uhz;

	return 0;
}

int st_asm330lhhx_get_batch_val(struct st_asm330lhhx_sensor *sensor,
			       int odr, int uodr, u8 *val)
{
	int required_odr = ST_ASM330LHHX_ODR_EXPAND(odr, uodr);
	enum st_asm330lhhx_sensor_id id = sensor->id;
	int sensor_odr;
	int i;

	for (i = 0; i < st_asm330lhhx_odr_table[id].size; i++) {
		sensor_odr = ST_ASM330LHHX_ODR_EXPAND(
				st_asm330lhhx_odr_table[id].odr_avl[i].hz,
				st_asm330lhhx_odr_table[id].odr_avl[i].uhz);
		if (sensor_odr >= required_odr)
			break;
	}

	if (i == st_asm330lhhx_odr_table[id].size)
		return -EINVAL;

	*val = st_asm330lhhx_odr_table[id].odr_avl[i].batch_val;

	return 0;
}

static u16 st_asm330lhhx_check_odr_dependency(struct st_asm330lhhx_hw *hw,
					   int odr, int uodr,
					   enum st_asm330lhhx_sensor_id ref_id)
{
	struct st_asm330lhhx_sensor *ref = iio_priv(hw->iio_devs[ref_id]);
	bool enable = ST_ASM330LHHX_ODR_EXPAND(odr, uodr) > 0;
	u16 ret;

	if (enable) {
		/* uodr not used */
		if (hw->enable_mask & BIT_ULL(ref_id))
			ret = max_t(u16, ref->odr, odr);
		else
			ret = odr;
	} else {
		ret = (hw->enable_mask & BIT_ULL(ref_id)) ? ref->odr : 0;
	}

	return ret;
}

static int st_asm330lhhx_update_odr_fsm(struct st_asm330lhhx_hw *hw,
					enum st_asm330lhhx_sensor_id id,
					enum st_asm330lhhx_sensor_id id_req,
					int val, int delay)
{
	bool fsm_running = st_asm330lhhx_fsm_running(hw);
	bool mlc_running = st_asm330lhhx_mlc_running(hw);
	int ret = 0;
	int status;

	if (fsm_running || mlc_running ||
	    (id_req > ST_ASM330LHHX_ID_MLC)) {
		/*
		 * In STMC_PAGE:
		 * Addr 0x02 bit 1 set to 1 -- CLK Disable
		 * Addr 0x05 bit 0 set to 0 -- FSM_EN=0
		 * Addr 0x05 bit 4 set to 0 -- MLC_EN=0
		 * Addr 0x67 bit 0 set to 0 -- FSM_INIT=0
		 * Addr 0x67 bit 4 set to 0 -- MLC_INIT=0
		 * Addr 0x02 bit 1 set to 0 -- CLK Disable
		 * - ODR change
		 * - Wait (~3 ODRs)
		 * In STMC_PAGE:
		 * Addr 0x05 bit 0 set to 1 -- FSM_EN = 1
		 * Addr 0x05 bit 4 set to 1 -- MLC_EN = 1
		 */
		mutex_lock(&hw->page_lock);
		ret = st_asm330lhhx_set_page_access(hw, true,
				       ST_ASM330LHHX_REG_FUNC_CFG_MASK);
		if (ret < 0)
			goto unlock_page;

		ret = regmap_read(hw->regmap,
				  ST_ASM330LHHX_EMB_FUNC_EN_B_ADDR,
				  &status);
		if (ret < 0)
			goto unlock_page;

		ret = regmap_update_bits(hw->regmap,
					 ST_ASM330LHHX_PAGE_SEL_ADDR,
					 BIT(1), FIELD_PREP(BIT(1), 1));
		if (ret < 0)
			goto unlock_page;

		ret = regmap_update_bits(hw->regmap,
			ST_ASM330LHHX_EMB_FUNC_EN_B_ADDR,
			ST_ASM330LHHX_FSM_EN_MASK,
			FIELD_PREP(ST_ASM330LHHX_FSM_EN_MASK, 0));
		if (ret < 0)
			goto unlock_page;

		if (st_asm330lhhx_mlc_running(hw)) {
			ret = regmap_update_bits(hw->regmap,
				ST_ASM330LHHX_EMB_FUNC_EN_B_ADDR,
				ST_ASM330LHHX_MLC_EN_MASK,
				FIELD_PREP(ST_ASM330LHHX_MLC_EN_MASK, 0));
			if (ret < 0)
				goto unlock_page;
		}

		ret = regmap_update_bits(hw->regmap,
			ST_ASM330LHHX_REG_EMB_FUNC_INIT_B_ADDR,
			ST_ASM330LHHX_MLC_INIT_MASK,
			FIELD_PREP(ST_ASM330LHHX_MLC_INIT_MASK, 0));
		if (ret < 0)
			goto unlock_page;

		ret = regmap_update_bits(hw->regmap,
			ST_ASM330LHHX_REG_EMB_FUNC_INIT_B_ADDR,
			ST_ASM330LHHX_FSM_INIT_MASK,
			FIELD_PREP(ST_ASM330LHHX_FSM_INIT_MASK, 0));
		if (ret < 0)
			goto unlock_page;

		ret = regmap_update_bits(hw->regmap,
					 ST_ASM330LHHX_PAGE_SEL_ADDR,
					 BIT(1), FIELD_PREP(BIT(1), 0));
		if (ret < 0)
			goto unlock_page;

		ret = st_asm330lhhx_set_page_access(hw, false,
				      ST_ASM330LHHX_REG_FUNC_CFG_MASK);
		if (ret < 0)
			goto unlock_page;

		ret = regmap_update_bits(hw->regmap,
			st_asm330lhhx_odr_table[id].reg.addr,
			st_asm330lhhx_odr_table[id].reg.mask,
			ST_ASM330LHHX_SHIFT_VAL(val,
				st_asm330lhhx_odr_table[id].reg.mask));
		if (ret < 0)
			goto unlock_page;

		usleep_range(delay, delay + (delay / 10));

		st_asm330lhhx_set_page_access(hw, true,
				       ST_ASM330LHHX_REG_FUNC_CFG_MASK);

		ret = regmap_write(hw->regmap,
				   ST_ASM330LHHX_EMB_FUNC_EN_B_ADDR,
				   status);
unlock_page:
		st_asm330lhhx_set_page_access(hw, false,
				       ST_ASM330LHHX_REG_FUNC_CFG_MASK);
		mutex_unlock(&hw->page_lock);
	} else {
		ret = st_asm330lhhx_update_bits_locked(hw,
				st_asm330lhhx_odr_table[id].reg.addr,
				st_asm330lhhx_odr_table[id].reg.mask,
				val);
	}

	return ret;
}

static int st_asm330lhhx_set_odr(struct st_asm330lhhx_sensor *sensor,
				 int req_odr, int req_uodr)
{
	enum st_asm330lhhx_sensor_id id_req = sensor->id;
	enum st_asm330lhhx_sensor_id id = sensor->id;
	struct st_asm330lhhx_hw *hw = sensor->hw;
	int err, delay;
	u8 val = 0;

	switch (id) {
	case ST_ASM330LHHX_ID_EXT0:
	case ST_ASM330LHHX_ID_EXT1:
	case ST_ASM330LHHX_ID_MLC_0:
	case ST_ASM330LHHX_ID_MLC_1:
	case ST_ASM330LHHX_ID_MLC_2:
	case ST_ASM330LHHX_ID_MLC_3:
	case ST_ASM330LHHX_ID_MLC_4:
	case ST_ASM330LHHX_ID_MLC_5:
	case ST_ASM330LHHX_ID_MLC_6:
	case ST_ASM330LHHX_ID_MLC_7:
	case ST_ASM330LHHX_ID_FSM_0:
	case ST_ASM330LHHX_ID_FSM_1:
	case ST_ASM330LHHX_ID_FSM_2:
	case ST_ASM330LHHX_ID_FSM_3:
	case ST_ASM330LHHX_ID_FSM_4:
	case ST_ASM330LHHX_ID_FSM_5:
	case ST_ASM330LHHX_ID_FSM_6:
	case ST_ASM330LHHX_ID_FSM_7:
	case ST_ASM330LHHX_ID_FSM_8:
	case ST_ASM330LHHX_ID_FSM_9:
	case ST_ASM330LHHX_ID_FSM_10:
	case ST_ASM330LHHX_ID_FSM_11:
	case ST_ASM330LHHX_ID_FSM_12:
	case ST_ASM330LHHX_ID_FSM_13:
	case ST_ASM330LHHX_ID_FSM_14:
	case ST_ASM330LHHX_ID_FSM_15:
	case ST_ASM330LHHX_ID_WK:
	case ST_ASM330LHHX_ID_FF:
	case ST_ASM330LHHX_ID_SC:
	case ST_ASM330LHHX_ID_6D:
	case ST_ASM330LHHX_ID_TEMP:
	case ST_ASM330LHHX_ID_ACC: {
		int odr;
		int i;

		id = ST_ASM330LHHX_ID_ACC;
		for (i = ST_ASM330LHHX_ID_ACC; i < ST_ASM330LHHX_ID_MAX; i++) {
			if (!hw->iio_devs[i])
				continue;

			if (i == sensor->id)
				continue;

			odr = st_asm330lhhx_check_odr_dependency(hw, req_odr,
								req_uodr, i);
			if (odr != req_odr) {
				/* device already configured */
				return 0;
			}
		}
		break;
	}
	case ST_ASM330LHHX_ID_GYRO:
		break;
	default:
		return 0;
	}

	err = st_asm330lhhx_get_odr_val(id, req_odr, req_uodr, NULL,
				       NULL, &val);
	if (err < 0)
		return err;

	/* check if sensor supports power mode setting */
	if (sensor->pm != ST_ASM330LHHX_NO_MODE &&
	    hw->settings->st_power_mode) {
		err = regmap_update_bits(hw->regmap,
				st_asm330lhhx_odr_table[id].pm.addr,
				st_asm330lhhx_odr_table[id].pm.mask,
				ST_ASM330LHHX_SHIFT_VAL(sensor->pm,
				 st_asm330lhhx_odr_table[id].pm.mask));
		if (err < 0)
			return err;
	}

	delay = req_odr > 0 ? 4000000 / req_odr : 0;

	return st_asm330lhhx_update_odr_fsm(hw, id, id_req, val, delay);
}

int st_asm330lhhx_sensor_set_enable(struct st_asm330lhhx_sensor *sensor,
				 bool enable)
{
	int uodr = enable ? sensor->uodr : 0;
	int odr = enable ? sensor->odr : 0;
	int err;

	err = st_asm330lhhx_set_odr(sensor, odr, uodr);
	if (err < 0)
		return err;

	if (enable)
		sensor->hw->enable_mask |= BIT_ULL(sensor->id);
	else
		sensor->hw->enable_mask &= ~BIT_ULL(sensor->id);

	return 0;
}

static int st_asm330lhhx_read_oneshot(struct st_asm330lhhx_sensor *sensor,
				   u8 addr, int *val)
{
	struct st_asm330lhhx_hw *hw = sensor->hw;
	int err, delay;
	__le16 data;

	err = st_asm330lhhx_sensor_set_enable(sensor, true);
	if (err < 0)
		return err;

	/* Use big delay for data valid because of drdy mask enabled */
	delay = 10000000 / sensor->odr;
	usleep_range(delay, 2 * delay);

	err = st_asm330lhhx_read_locked(hw, addr,
				    &data,
				    sizeof(data));
	if (err < 0)
		return err;

	err = st_asm330lhhx_sensor_set_enable(sensor, false);

	*val = (s16)le16_to_cpu(data);

	return IIO_VAL_INT;
}

static int st_asm330lhhx_read_raw(struct iio_dev *iio_dev,
			       struct iio_chan_spec const *ch,
			       int *val, int *val2, long mask)
{
	struct st_asm330lhhx_sensor *sensor = iio_priv(iio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(iio_dev);
		if (ret)
			break;

		ret = st_asm330lhhx_read_oneshot(sensor, ch->address, val);
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
			*val2 = ST_ASM330LHHX_TEMP_GAIN;
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

static int st_asm330lhhx_write_raw(struct iio_dev *iio_dev,
				struct iio_chan_spec const *chan,
				int val, int val2, long mask)
{
	struct st_asm330lhhx_sensor *s = iio_priv(iio_dev);
	int err;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		err = iio_device_claim_direct_mode(iio_dev);
		if (err)
			return err;

		err = st_asm330lhhx_set_full_scale(s, val2);
		iio_device_release_direct_mode(iio_dev);
		break;
	case IIO_CHAN_INFO_SAMP_FREQ: {
		int todr, tuodr;
		u8 data;

		err = st_asm330lhhx_get_odr_val(s->id, val, val2,
						 &todr, &tuodr, &data);
		if (!err) {
			s->odr = todr;
			s->uodr = tuodr;

			/*
			 * VTS test testSamplingRateHotSwitchOperation not
			 * toggle the enable status of sensor after changing
			 * the ODR -> force it
			 */
			if (s->hw->enable_mask & BIT_ULL(s->id)) {
				switch (s->id) {
				case ST_ASM330LHHX_ID_GYRO:
				case ST_ASM330LHHX_ID_ACC:
					err = st_asm330lhhx_set_odr(s, s->odr, s->uodr);
					if (err < 0)
						break;

					st_asm330lhhx_update_batching(iio_dev, 1);
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

	return err;
}

static ssize_t
st_asm330lhhx_sysfs_sampling_freq_avail(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct st_asm330lhhx_sensor *sensor = iio_priv(dev_to_iio_dev(dev));
	enum st_asm330lhhx_sensor_id id = sensor->id;
	int i, len = 0;

	for (i = 0; i < st_asm330lhhx_odr_table[id].size; i++) {
		len += scnprintf(buf + len, PAGE_SIZE - len, "%d.%06d ",
				 st_asm330lhhx_odr_table[id].odr_avl[i].hz,
				 st_asm330lhhx_odr_table[id].odr_avl[i].uhz);
	}

	buf[len - 1] = '\n';

	return len;
}

static ssize_t st_asm330lhhx_sysfs_scale_avail(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct st_asm330lhhx_sensor *sensor = iio_priv(dev_to_iio_dev(dev));
	enum st_asm330lhhx_sensor_id id = sensor->id;
	int i, len = 0;

	for (i = 0; i < st_asm330lhhx_fs_table[id].size; i++) {
		if (sensor->id != ST_ASM330LHHX_ID_TEMP) {
			len += scnprintf(buf + len, PAGE_SIZE - len, "0.%09u ",
						     st_asm330lhhx_fs_table[id].fs_avl[i].gain);
		} else {
			int hi, low;

			hi = (int)(st_asm330lhhx_fs_table[id].fs_avl[i].gain / 1000);
			low = (int)(st_asm330lhhx_fs_table[id].fs_avl[i].gain % 1000);
			len += scnprintf(buf + len, PAGE_SIZE - len, "%d.%d ",
						     hi, low);
		}
	}

	buf[len - 1] = '\n';

	return len;
}

static ssize_t
st_asm330lhhx_sysfs_get_power_mode_avail(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_asm330lhhx_sensor *sensor = iio_priv(iio_dev);
	struct st_asm330lhhx_hw *hw = sensor->hw;
	int i, len = 0;

	/* check for supported feature */
	if (hw->settings->st_power_mode) {
		for (i = 0; i < ARRAY_SIZE(st_asm330lhhx_power_mode); i++) {
			len += scnprintf(buf + len, PAGE_SIZE - len, "%s ",
					 st_asm330lhhx_power_mode[i].string_mode);
		}
	} else {
		len += scnprintf(buf + len, PAGE_SIZE - len, "%s ",
				 st_asm330lhhx_power_mode[0].string_mode);
	}

	buf[len - 1] = '\n';

	return len;
}

static ssize_t
st_asm330lhhx_get_power_mode(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_asm330lhhx_sensor *sensor = iio_priv(iio_dev);

	return sprintf(buf, "%s\n",
		       st_asm330lhhx_power_mode[sensor->pm].string_mode);
}

static ssize_t
st_asm330lhhx_set_power_mode(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_asm330lhhx_sensor *sensor = iio_priv(iio_dev);
	struct st_asm330lhhx_hw *hw = sensor->hw;
	int err, i;

	/* check for supported feature */
	if (!hw->settings->st_power_mode)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(st_asm330lhhx_power_mode); i++) {
		if (strncmp(buf, st_asm330lhhx_power_mode[i].string_mode,
		    strlen(st_asm330lhhx_power_mode[i].string_mode)) == 0)
			break;
	}

	if (i == ARRAY_SIZE(st_asm330lhhx_power_mode))
		return -EINVAL;

	err = iio_device_claim_direct_mode(iio_dev);
	if (err)
		return err;

	/* update power mode */
	sensor->pm = st_asm330lhhx_power_mode[i].val;

	iio_device_release_direct_mode(iio_dev);

	return size;
}

static int st_asm330lhhx_selftest_sensor(struct st_asm330lhhx_sensor *sensor,
					int test)
{
	int x_selftest = 0, y_selftest = 0, z_selftest = 0;
	int x = 0, y = 0, z = 0, try_count = 0;
	u8 i, status, n = 0;
	u8 reg, bitmask;
	int ret, delay, data_delay = 100000;
	u8 raw_data[6];

	switch(sensor->id) {
	case ST_ASM330LHHX_ID_ACC:
		reg = ST_ASM330LHHX_REG_OUTX_L_A_ADDR;
		bitmask = ST_ASM330LHHX_REG_STATUS_XLDA;
		data_delay = 50000;
		break;
	case ST_ASM330LHHX_ID_GYRO:
		reg = ST_ASM330LHHX_REG_OUTX_L_G_ADDR;
		bitmask = ST_ASM330LHHX_REG_STATUS_GDA;
		break;
	default:
		return -EINVAL;
	}

	/* reset selftest_status */
	sensor->selftest_status = -1;

	/* set selftest normal mode */
	ret = st_asm330lhhx_set_selftest(sensor, 0);
	if (ret < 0)
		return ret;

	ret = st_asm330lhhx_sensor_set_enable(sensor, true);
	if (ret < 0)
		return ret;

	/*
	 * wait at least one ODRs plus 10 % to be sure to fetch new
	 * sample data
	 */
	delay = 1000000 / sensor->odr;

	/* power up, wait for stable output */
	usleep_range(data_delay, data_delay + data_delay / 100);

	/* after enabled the sensor trash first sample */
	while (try_count < 3) {
		usleep_range(delay, delay + delay/10);
		ret = st_asm330lhhx_read_locked(sensor->hw,
						ST_ASM330LHHX_REG_STATUS_ADDR,
						&status, sizeof(status));
		if (ret < 0)
			goto selftest_failure;

		if (status & bitmask) {
			st_asm330lhhx_read_locked(sensor->hw, reg,
						  raw_data, sizeof(raw_data));
			break;
		}

		try_count++;
	}

	if (try_count == 3)
		goto selftest_failure;

	/* for 5 times, after checking status bit, read the output registers */
	for (i = 0; i < 5; i++) {
		try_count = 0;
		while (try_count < 3) {
			usleep_range(delay, delay + delay/10);
			ret = st_asm330lhhx_read_locked(sensor->hw,
						ST_ASM330LHHX_REG_STATUS_ADDR,
						&status, sizeof(status));
			if (ret < 0)
				goto selftest_failure;

			if (status & bitmask) {
				ret = st_asm330lhhx_read_locked(sensor->hw,
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
			} else {
				try_count++;
			}
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
	st_asm330lhhx_set_selftest(sensor, test);

	/* wait for stable output */
	usleep_range(data_delay, data_delay + data_delay / 100);

	try_count = 0;

	/* after enabled the sensor trash first sample */
	while (try_count < 3) {
		usleep_range(delay, delay + delay/10);
		ret = st_asm330lhhx_read_locked(sensor->hw,
						ST_ASM330LHHX_REG_STATUS_ADDR,
						&status, sizeof(status));
		if (ret < 0)
			goto selftest_failure;

		if (status & bitmask) {
			st_asm330lhhx_read_locked(sensor->hw, reg,
						  raw_data, sizeof(raw_data));
			break;
		}

		try_count++;
	}

	if (try_count == 3)
		goto selftest_failure;

	/* for 5 times, after checking status bit, read the output registers */
	for (i = 0; i < 5; i++) {
		try_count = 0;
		while (try_count < 3) {
			usleep_range(delay, delay + delay/10);
			ret = st_asm330lhhx_read_locked(sensor->hw,
						ST_ASM330LHHX_REG_STATUS_ADDR,
						&status, sizeof(status));
			if (ret < 0)
				goto selftest_failure;

			if (status & bitmask) {
				ret = st_asm330lhhx_read_locked(sensor->hw,
							      reg, raw_data,
							      sizeof(raw_data));
				if (ret < 0)
					goto selftest_failure;

				x_selftest += ((s16)*(u16 *)&raw_data[0]) / 5;
				y_selftest += ((s16)*(u16 *)&raw_data[2]) / 5;
				z_selftest += ((s16)*(u16 *)&raw_data[4]) / 5;
				n++;

				break;
			} else {
				try_count++;
			}
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
		dev_info(sensor->hw->dev, "st: failure on x: non-st(%d), st(%d)\n",
			 x, x_selftest);
		goto selftest_failure;
	}

	if ((abs(y_selftest - y) < sensor->min_st) ||
	    (abs(y_selftest - y) > sensor->max_st)) {
		sensor->selftest_status = -1;
		dev_info(sensor->hw->dev, "st: failure on y: non-st(%d), st(%d)\n",
			 y, y_selftest);
		goto selftest_failure;
	}

	if ((abs(z_selftest - z) < sensor->min_st) ||
	    (abs(z_selftest - z) > sensor->max_st)) {
		sensor->selftest_status = -1;
		dev_info(sensor->hw->dev, "st: failure on z: non-st(%d), st(%d)\n",
			 z, z_selftest);
		goto selftest_failure;
	}

	sensor->selftest_status = 1;

selftest_failure:
	/* restore selftest to normal mode */
	st_asm330lhhx_set_selftest(sensor, 0);

	return st_asm330lhhx_sensor_set_enable(sensor, false);
}

static ssize_t st_asm330lhhx_sysfs_start_selftest(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_asm330lhhx_sensor *sensor = iio_priv(iio_dev);
	enum st_asm330lhhx_sensor_id id = sensor->id;
	struct st_asm330lhhx_hw *hw = sensor->hw;
	int ret, test;
	u8 drdy_reg;
	u32 gain;

	if (id != ST_ASM330LHHX_ID_ACC &&
	    id != ST_ASM330LHHX_ID_GYRO)
		return -EINVAL;

	for (test = 0; test < ARRAY_SIZE(st_asm330lhhx_selftest_table); test++) {
		if (strncmp(buf, st_asm330lhhx_selftest_table[test].string_mode,
			strlen(st_asm330lhhx_selftest_table[test].string_mode)) == 0)
			break;
	}

	if (test == ARRAY_SIZE(st_asm330lhhx_selftest_table))
		return -EINVAL;

	ret = iio_device_claim_direct_mode(iio_dev);
	if (ret)
		return ret;

	/* self test mode unavailable if sensor enabled */
	if (hw->enable_mask & BIT_ULL(id)) {
		ret = -EBUSY;

		goto out_claim;
	}

	st_asm330lhhx_bk_regs(hw);

	/* disable FIFO watermak interrupt */
	ret = st_asm330lhhx_get_int_reg(hw, &drdy_reg);
	if (ret < 0)
		goto restore_regs;

	ret = st_asm330lhhx_update_bits_locked(hw, drdy_reg,
					   ST_ASM330LHHX_REG_INT_FIFO_TH_MASK,
					   0);
	if (ret < 0)
		goto restore_regs;

	gain = sensor->gain;
	if (id == ST_ASM330LHHX_ID_ACC) {
		/* set BDU = 1, FS = 4 g, ODR = 52 Hz */
		st_asm330lhhx_set_full_scale(sensor,
					    ST_ASM330LHHX_ACC_FS_4G_GAIN);
		st_asm330lhhx_set_odr(sensor, 52, 0);
		st_asm330lhhx_selftest_sensor(sensor, test);

		/* restore full scale after test */
		st_asm330lhhx_set_full_scale(sensor, gain);
	} else {
		/* set BDU = 1, ODR = 208 Hz, FS = 2000 dps */
		st_asm330lhhx_set_full_scale(sensor,
					    ST_ASM330LHHX_GYRO_FS_2000_GAIN);
		/* before enable gyro add 150 ms delay when gyro self-test */
		usleep_range(150000, 151000);

		st_asm330lhhx_set_odr(sensor, 208, 0);
		st_asm330lhhx_selftest_sensor(sensor, test);

		/* restore full scale after test */
		st_asm330lhhx_set_full_scale(sensor, gain);
	}

restore_regs:
	st_asm330lhhx_restore_regs(hw);

out_claim:
	iio_device_release_direct_mode(iio_dev);

	return size;
}

ssize_t st_asm330lhhx_get_module_id(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_asm330lhhx_sensor *sensor = iio_priv(iio_dev);
	struct st_asm330lhhx_hw *hw = sensor->hw;

	return scnprintf(buf, PAGE_SIZE, "%u\n", hw->module_id);
}

static IIO_DEV_ATTR_SAMP_FREQ_AVAIL(st_asm330lhhx_sysfs_sampling_freq_avail);
static IIO_DEVICE_ATTR(in_accel_scale_available, 0444,
		       st_asm330lhhx_sysfs_scale_avail, NULL, 0);
static IIO_DEVICE_ATTR(in_anglvel_scale_available, 0444,
		       st_asm330lhhx_sysfs_scale_avail, NULL, 0);
static IIO_DEVICE_ATTR(hwfifo_watermark_max, 0444,
		       st_asm330lhhx_get_max_watermark, NULL, 0);
static IIO_DEVICE_ATTR(in_temp_scale_available, 0444,
		       st_asm330lhhx_sysfs_scale_avail, NULL, 0);
static IIO_DEVICE_ATTR(hwfifo_flush, 0200, NULL, st_asm330lhhx_flush_fifo, 0);
static IIO_DEVICE_ATTR(hwfifo_watermark, 0644, st_asm330lhhx_get_watermark,
		       st_asm330lhhx_set_watermark, 0);

static IIO_DEVICE_ATTR(power_mode_available, 0444,
		       st_asm330lhhx_sysfs_get_power_mode_avail, NULL, 0);
static IIO_DEVICE_ATTR(power_mode, 0644,
		       st_asm330lhhx_get_power_mode,
		       st_asm330lhhx_set_power_mode, 0);

static IIO_DEVICE_ATTR(selftest_available, 0444,
		       st_asm330lhhx_sysfs_get_selftest_available,
		       NULL, 0);
static IIO_DEVICE_ATTR(selftest, 0644,
		       st_asm330lhhx_sysfs_get_selftest_status,
		       st_asm330lhhx_sysfs_start_selftest, 0);
static IIO_DEVICE_ATTR(module_id, 0444, st_asm330lhhx_get_module_id, NULL, 0);

static
ssize_t __maybe_unused st_asm330lhhx_get_discharded_samples(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_asm330lhhx_sensor *sensor = iio_priv(iio_dev);
	int ret;

	ret = sprintf(buf, "%d\n", sensor->discharged_samples);

	/* reset counter */
	sensor->discharged_samples = 0;

	return ret;
}

static int st_asm330lhhx_write_raw_get_fmt(struct iio_dev *indio_dev,
			struct iio_chan_spec const *chan, long mask)
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

static IIO_DEVICE_ATTR(discharded_samples, 0444,
		       st_asm330lhhx_get_discharded_samples, NULL, 0);

static struct attribute *st_asm330lhhx_acc_attributes[] = {
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

#ifdef ST_ASM330LHHX_DEBUG_DISCHARGE
	&iio_dev_attr_discharded_samples.dev_attr.attr,
#endif /* ST_ASM330LHHX_DEBUG_DISCHARGE */

	NULL,
};

static const struct attribute_group st_asm330lhhx_acc_attribute_group = {
	.attrs = st_asm330lhhx_acc_attributes,
};

static const struct iio_info st_asm330lhhx_acc_info = {
	.attrs = &st_asm330lhhx_acc_attribute_group,
	.read_raw = st_asm330lhhx_read_raw,
	.write_raw = st_asm330lhhx_write_raw,
	.write_raw_get_fmt = st_asm330lhhx_write_raw_get_fmt,
#ifdef CONFIG_DEBUG_FS
	.debugfs_reg_access = &st_asm330lhhx_reg_access,
#endif /* CONFIG_DEBUG_FS */
};

static struct attribute *st_asm330lhhx_gyro_attributes[] = {
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

#ifdef ST_ASM330LHHX_DEBUG_DISCHARGE
	&iio_dev_attr_discharded_samples.dev_attr.attr,
#endif /* ST_ASM330LHHX_DEBUG_DISCHARGE */

	NULL,
};

static const struct attribute_group st_asm330lhhx_gyro_attribute_group = {
	.attrs = st_asm330lhhx_gyro_attributes,
};

static const struct iio_info st_asm330lhhx_gyro_info = {
	.attrs = &st_asm330lhhx_gyro_attribute_group,
	.read_raw = st_asm330lhhx_read_raw,
	.write_raw = st_asm330lhhx_write_raw,
	.write_raw_get_fmt = st_asm330lhhx_write_raw_get_fmt,
};

static struct attribute *st_asm330lhhx_temp_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_temp_scale_available.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark_max.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark.dev_attr.attr,
	&iio_dev_attr_hwfifo_flush.dev_attr.attr,
	&iio_dev_attr_module_id.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_asm330lhhx_temp_attribute_group = {
	.attrs = st_asm330lhhx_temp_attributes,
};

static const struct iio_info st_asm330lhhx_temp_info = {
	.attrs = &st_asm330lhhx_temp_attribute_group,
	.read_raw = st_asm330lhhx_read_raw,
	.write_raw = st_asm330lhhx_write_raw,
	.write_raw_get_fmt = st_asm330lhhx_write_raw_get_fmt,
};

static const unsigned long st_asm330lhhx_available_scan_masks[] = {
	GENMASK(3, 0), 0x0
};

static const unsigned long st_asm330lhhx_temp_available_scan_masks[] = {
	GENMASK(1, 0), 0x0
};

static int st_asm330lhhx_reset_device(struct st_asm330lhhx_hw *hw)
{
	int err;

	/* set configuration bit */
	err = regmap_update_bits(hw->regmap,
				 ST_ASM330LHHX_REG_CTRL9_XL_ADDR,
				 ST_ASM330LHHX_REG_DEVICE_CONF_MASK,
				 FIELD_PREP(ST_ASM330LHHX_REG_DEVICE_CONF_MASK, 1));
	if (err < 0)
		return err;

	/* sw reset */
	err = regmap_update_bits(hw->regmap,
				 ST_ASM330LHHX_REG_CTRL3_C_ADDR,
				 ST_ASM330LHHX_REG_SW_RESET_MASK,
				 FIELD_PREP(ST_ASM330LHHX_REG_SW_RESET_MASK, 1));
	if (err < 0)
		return err;

	msleep(50);

	/* boot */
	err = regmap_update_bits(hw->regmap,
				 ST_ASM330LHHX_REG_CTRL3_C_ADDR,
				 ST_ASM330LHHX_REG_BOOT_MASK,
				 FIELD_PREP(ST_ASM330LHHX_REG_BOOT_MASK, 1));

	msleep(50);

	return err;
}

static int st_asm330lhhx_init_device(struct st_asm330lhhx_hw *hw)
{
	u8 drdy_reg;
	int err;

	/* latch interrupts */
	err = regmap_update_bits(hw->regmap,
				 ST_ASM330LHHX_REG_TAP_CFG0_ADDR,
				 ST_ASM330LHHX_REG_LIR_MASK,
				 FIELD_PREP(ST_ASM330LHHX_REG_LIR_MASK, 1));
	if (err < 0)
		return err;

	/* enable Block Data Update */
	err = regmap_update_bits(hw->regmap, ST_ASM330LHHX_REG_CTRL3_C_ADDR,
				 ST_ASM330LHHX_REG_BDU_MASK,
				 FIELD_PREP(ST_ASM330LHHX_REG_BDU_MASK, 1));
	if (err < 0)
		return err;

	err = regmap_update_bits(hw->regmap, ST_ASM330LHHX_REG_CTRL5_C_ADDR,
				 ST_ASM330LHHX_REG_ROUNDING_MASK,
				 FIELD_PREP(ST_ASM330LHHX_REG_ROUNDING_MASK, 3));
	if (err < 0)
		return err;

	/* init timestamp engine */
	err = regmap_update_bits(hw->regmap,
				 ST_ASM330LHHX_REG_CTRL10_C_ADDR,
				 ST_ASM330LHHX_REG_TIMESTAMP_EN_MASK,
				 ST_ASM330LHHX_SHIFT_VAL(true,
				  ST_ASM330LHHX_REG_TIMESTAMP_EN_MASK));
	if (err < 0)
		return err;

	err = st_asm330lhhx_get_int_reg(hw, &drdy_reg);
	if (err < 0)
		return err;

	/* Enable DRDY MASK for filters settling time */
	err = regmap_update_bits(hw->regmap, ST_ASM330LHHX_REG_CTRL4_C_ADDR,
				 ST_ASM330LHHX_REG_DRDY_MASK,
				 FIELD_PREP(ST_ASM330LHHX_REG_DRDY_MASK,
					    1));

	if (err < 0)
		return err;

	/* enable FIFO watermak interrupt */
	return regmap_update_bits(hw->regmap, drdy_reg,
				  ST_ASM330LHHX_REG_INT_FIFO_TH_MASK,
				  FIELD_PREP(ST_ASM330LHHX_REG_INT_FIFO_TH_MASK, 1));
}

#ifdef CONFIG_IIO_ST_ASM330LHHX_EN_BASIC_FEATURES
static int st_asm330lhhx_post_init_device(struct st_asm330lhhx_hw *hw)
{
	int err;

	/* Set default wake-up thershold to 93750 ug */
	err = st_asm330lhhx_set_wake_up_thershold(hw, 93750);
	if (err < 0)
		return err;

	/* Set default wake-up duration to 0 */
	err = st_asm330lhhx_set_wake_up_duration(hw, 0);
	if (err < 0)
		return err;

	/* setting default FF threshold to 312 mg */
	err = st_asm330lhhx_set_freefall_threshold(hw, 312);
	if (err < 0)
		return err;

	/* setting default 6D threshold to 60 degrees */
	return st_asm330lhhx_set_6D_threshold(hw, 60);
}
#endif /* CONFIG_IIO_ST_ASM330LHHX_EN_BASIC_FEATURES */

static struct iio_dev *st_asm330lhhx_alloc_iiodev(struct st_asm330lhhx_hw *hw,
					       enum st_asm330lhhx_sensor_id id)
{
	struct st_asm330lhhx_sensor *sensor;
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

#ifdef ST_ASM330LHHX_DEBUG_DISCHARGE
	sensor->discharged_samples = 0;
#endif /* ST_ASM330LHHX_DEBUG_DISCHARGE */

	/*
	 * for acc/gyro the default Android full scale settings are:
	 * Acc FS 8g (78.40 m/s^2)
	 * Gyro FS 1000dps (16.45 radians/sec)
	 */
	switch (id) {
	case ST_ASM330LHHX_ID_ACC:
		iio_dev->channels = st_asm330lhhx_acc_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_asm330lhhx_acc_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			 "%s_accel", hw->settings->id.name);
		iio_dev->info = &st_asm330lhhx_acc_info;
		iio_dev->available_scan_masks =
					st_asm330lhhx_available_scan_masks;
		sensor->max_watermark = ST_ASM330LHHX_MAX_FIFO_DEPTH;
		sensor->gain = st_asm330lhhx_fs_table[id].fs_avl[ST_ASM330LHHX_DEFAULT_XL_FS_INDEX].gain;
		sensor->odr = st_asm330lhhx_odr_table[id].odr_avl[ST_ASM330LHHX_DEFAULT_XL_ODR_INDEX].hz;
		sensor->uodr = st_asm330lhhx_odr_table[id].odr_avl[ST_ASM330LHHX_DEFAULT_XL_ODR_INDEX].uhz;
		sensor->offset = 0;
		sensor->pm = ST_ASM330LHHX_HP_MODE;
		sensor->min_st = ST_ASM330LHHX_SELFTEST_ACCEL_MIN;
		sensor->max_st = ST_ASM330LHHX_SELFTEST_ACCEL_MAX;
		break;
	case ST_ASM330LHHX_ID_GYRO:
		iio_dev->channels = st_asm330lhhx_gyro_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_asm330lhhx_gyro_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			  "%s_gyro", hw->settings->id.name);
		iio_dev->info = &st_asm330lhhx_gyro_info;
		iio_dev->available_scan_masks =
					st_asm330lhhx_available_scan_masks;
		sensor->max_watermark = ST_ASM330LHHX_MAX_FIFO_DEPTH;
		sensor->gain = st_asm330lhhx_fs_table[id].fs_avl[ST_ASM330LHHX_DEFAULT_G_FS_INDEX].gain;
		sensor->odr = st_asm330lhhx_odr_table[id].odr_avl[ST_ASM330LHHX_DEFAULT_G_ODR_INDEX].hz;
		sensor->uodr = st_asm330lhhx_odr_table[id].odr_avl[ST_ASM330LHHX_DEFAULT_G_ODR_INDEX].uhz;
		sensor->offset = 0;
		sensor->pm = ST_ASM330LHHX_HP_MODE;
		sensor->min_st = ST_ASM330LHHX_SELFTEST_GYRO_MIN;
		sensor->max_st = ST_ASM330LHHX_SELFTEST_GYRO_MAX;
		break;
	case ST_ASM330LHHX_ID_TEMP:
		iio_dev->channels = st_asm330lhhx_temp_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_asm330lhhx_temp_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			  "%s_temp", hw->settings->id.name);
		iio_dev->info = &st_asm330lhhx_temp_info;
		iio_dev->available_scan_masks =
				    st_asm330lhhx_temp_available_scan_masks;
		sensor->max_watermark = ST_ASM330LHHX_MAX_FIFO_DEPTH;
		sensor->gain = st_asm330lhhx_fs_table[id].fs_avl[ST_ASM330LHHX_DEFAULT_T_FS_INDEX].gain;
		sensor->odr = st_asm330lhhx_odr_table[id].odr_avl[ST_ASM330LHHX_DEFAULT_T_ODR_INDEX].hz;
		sensor->uodr = st_asm330lhhx_odr_table[id].odr_avl[ST_ASM330LHHX_DEFAULT_T_ODR_INDEX].uhz;
		sensor->offset = ST_ASM330LHHX_TEMP_OFFSET;
		sensor->pm = ST_ASM330LHHX_NO_MODE;
		break;
	default:
		return NULL;
	}

	st_asm330lhhx_set_full_scale(sensor, sensor->gain);
	iio_dev->name = sensor->name;

	return iio_dev;
}

static void st_asm330lhhx_get_properties(struct st_asm330lhhx_hw *hw)
{
	if (device_property_read_u32(hw->dev, "st,module_id",
				     &hw->module_id)) {
		hw->module_id = 1;
	}
}

static void st_asm330lhhx_disable_regulator_action(void *_data)
{
	struct st_asm330lhhx_hw *hw = _data;

	regulator_disable(hw->vddio_supply);
	regulator_disable(hw->vdd_supply);
}

static int st_asm330lhhx_power_enable(struct st_asm330lhhx_hw *hw)
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
				       st_asm330lhhx_disable_regulator_action,
				       hw);
	if (err) {
		dev_err(hw->dev, "Failed to setup regulator cleanup action %d\n", err);
		return err;
	}

	return 0;
}

static void st_asm330lhh_regulator_power_down(struct st_asm330lhhx_hw *hw)
{
	regulator_disable(hw->vdd);
	regulator_set_voltage(hw->vdd, 0, INT_MAX);
	regulator_set_load(hw->vdd, 0);
	regulator_disable(hw->vio);
	regulator_set_voltage(hw->vio, 0, INT_MAX);
	regulator_set_load(hw->vio, 0);
}

static int st_asm330lhh_regulator_init(struct st_asm330lhhx_hw *hw)
{
	hw->vdd  = devm_regulator_get(hw->dev, "vdd");
	if (IS_ERR(hw->vdd))
		return dev_err_probe(hw->dev, PTR_ERR(hw->vdd), "Failed to get vdd");

	hw->vio = devm_regulator_get(hw->dev, "vio");
	if (IS_ERR(hw->vio))
		return dev_err_probe(hw->dev, PTR_ERR(hw->vio), "Failed to get vio");

	return 0;
}

static int st_asm330lhh_regulator_power_up(struct st_asm330lhhx_hw *hw)
{
	struct device_node *np;
	u32 vdd_voltage[2];
	u32 vio_voltage[2];
	u32 vdd_current = 30000;
	u32 vio_current = 30000;
	int err = 0;

	np = hw->dev->of_node;

	if (of_property_read_u32(np, "vio-min-voltage", &vio_voltage[0]))
		vio_voltage[0] = 1620000;

	if (of_property_read_u32(np, "vio-max-voltage", &vio_voltage[1]))
		vio_voltage[1] = 3600000;

	if (of_property_read_u32(np, "vdd-min-voltage", &vdd_voltage[0]))
		vdd_voltage[0] = 3000000;

	if (of_property_read_u32(np, "vdd-max-voltage", &vdd_voltage[1]))
		vdd_voltage[1] = 3600000;

	/* Enable VDD for ASM330 */
	if (vdd_voltage[0] > 0 && vdd_voltage[0] <= vdd_voltage[1]) {
		err = regulator_set_voltage(hw->vdd, vdd_voltage[0],
						vdd_voltage[1]);
		if (err) {
			pr_err("Error %d during vdd set_voltage\n", err);
			return err;
		}
	}

	err = regulator_set_load(hw->vdd, vdd_current);
	if (err < 0) {
		pr_err("vdd regulator_set_load failed,err=%d\n", err);
		goto remove_vdd_voltage;
	}

	err = regulator_enable(hw->vdd);
	if (err) {
		dev_err(hw->dev, "vdd enable failed with error %d\n", err);
		goto remove_vdd_current;
	}

	/* Enable VIO for ASM330 */
	if (vio_voltage[0] > 0 && vio_voltage[0] <= vio_voltage[1]) {
		err = regulator_set_voltage(hw->vio, vio_voltage[0],
						vio_voltage[1]);
		if (err) {
			pr_err("Error %d during vio set_voltage\n", err);
			goto disable_vdd;
		}
	}

	err = regulator_set_load(hw->vio, vio_current);
	if (err < 0) {
		pr_err("vio regulator_set_load failed,err=%d\n", err);
		goto remove_vio_voltage;
	}

	err = regulator_enable(hw->vio);
	if (err) {
		dev_err(hw->dev, "vio enable failed with error %d\n", err);
		goto remove_vio_current;
	}

	return 0;

remove_vio_current:
	regulator_set_load(hw->vio, 0);
remove_vio_voltage:
	regulator_set_voltage(hw->vio, 0, INT_MAX);
disable_vdd:
	regulator_disable(hw->vdd);
remove_vdd_current:
	regulator_set_load(hw->vdd, 0);
remove_vdd_voltage:
	regulator_set_voltage(hw->vdd, 0, INT_MAX);

	return err;
}

int st_asm330lhhx_probe(struct device *dev, int irq, int hw_id,
			struct regmap *regmap)
{
	struct st_asm330lhhx_hw *hw;
	struct device_node *np;
	int i = 0, err = 0;

	hw = devm_kzalloc(dev, sizeof(*hw), GFP_KERNEL);
	if (!hw)
		return -ENOMEM;

	dev_set_drvdata(dev, (void *)hw);

	mutex_init(&hw->lock);
	mutex_init(&hw->fifo_lock);
	mutex_init(&hw->page_lock);

	hw->regmap = regmap;
	hw->dev = dev;
	hw->irq = irq;
	hw->odr_table_entry = st_asm330lhhx_odr_table;
	hw->hw_timestamp_global = 0;

	np = hw->dev->of_node;
	/* use qtimer if property is enabled */
	err = st_asm330lhh_regulator_init(hw);
	if (err < 0) {
		dev_err(hw->dev, "regulator init failed\n");
		return err;
	}

	err = st_asm330lhh_regulator_power_up(hw);
	if (err < 0) {
		dev_err(hw->dev, "regulator power up failed\n");
		return err;
	}

	/* allow time for enabling regulators */
	usleep_range(1000, 2000);

	err = st_asm330lhhx_power_enable(hw);
	if (err != 0)
		return err;

	/* set page zero before access to registers */
	if (hw_id == ST_ASM330LHHX_ID) {
		err = st_asm330lhhx_set_page_0(hw);
		if (err < 0)
			return err;
	}

	err = st_asm330lhhx_check_whoami(hw, hw_id);
	if (err < 0)
		return err;

	st_asm330lhhx_get_properties(hw);

	err = st_asm330lhhx_get_odr_calibration(hw);
	if (err < 0)
		return err;

	err = st_asm330lhhx_reset_device(hw);
	if (err < 0)
		return err;

	err = st_asm330lhhx_init_device(hw);
	if (err < 0)
		return err;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,15,0)
	err = iio_read_mount_matrix(hw->dev, &hw->orientation);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5,2,0)
	err = iio_read_mount_matrix(hw->dev, "mount-matrix", &hw->orientation);
#else /* LINUX_VERSION_CODE */
	err = of_iio_read_mount_matrix(hw->dev, "mount-matrix", &hw->orientation);
#endif /* LINUX_VERSION_CODE */

	if (err) {
		dev_err(dev, "Failed to retrieve mounting matrix %d\n", err);
		return err;
	}

	for (i = ST_ASM330LHHX_ID_GYRO; i <= ST_ASM330LHHX_ID_TEMP; i++) {
		hw->iio_devs[i] = st_asm330lhhx_alloc_iiodev(hw, i);
		if (!hw->iio_devs[i])
			return -ENOMEM;
	}

	if (hw->settings->st_shub_probe) {
		err = st_asm330lhhx_shub_probe(hw);
		if (err < 0)
			return err;
	}

	if (hw->irq > 0) {
		err = st_asm330lhhx_buffers_setup(hw);
		if (err < 0)
			return err;
	}

	if (hw->settings->st_mlc_probe) {
		err = st_asm330lhhx_mlc_probe(hw);
		if (err < 0)
			return err;
	}

	for (i = ST_ASM330LHHX_ID_GYRO; i < ST_ASM330LHHX_ID_MAX; i++) {
		if (!hw->iio_devs[i])
			continue;

		err = devm_iio_device_register(hw->dev, hw->iio_devs[i]);
		if (err)
			return err;
	}

	if (hw->settings->st_mlc_probe) {
		err = st_asm330lhhx_mlc_init_preload(hw);
		if (err)
			return err;
	}

#ifdef CONFIG_IIO_ST_ASM330LHHX_EN_BASIC_FEATURES
	err = st_asm330lhhx_probe_event(hw);
	if (err < 0)
		return err;

	err = st_asm330lhhx_post_init_device(hw);
	if (err < 0)
		return err;
#endif /* CONFIG_IIO_ST_ASM330LHHX_EN_BASIC_FEATURES */

	device_init_wakeup(dev,
			   device_property_read_bool(dev, "wakeup-source"));

	dev_info(dev, "Device probed\n");

	return 0;
}
EXPORT_SYMBOL(st_asm330lhhx_probe);

static int __maybe_unused st_asm330lhhx_suspend(struct device *dev)
{
	struct st_asm330lhhx_hw *hw = dev_get_drvdata(dev);
	struct st_asm330lhhx_sensor *sensor;
	int i, err = 0;

	dev_info(dev, "Suspending device\n");

	disable_hardirq(hw->irq);

	for (i = 0; i < ST_ASM330LHHX_ID_MAX; i++) {
		if (!hw->iio_devs[i])
			continue;

		sensor = iio_priv(hw->iio_devs[i]);
		if (!(hw->enable_mask & BIT_ULL(sensor->id)))
			continue;

		/* power off enabled sensors */
		err = st_asm330lhhx_set_odr(sensor, 0, 0);
		if (err < 0)
			return err;
	}

	if (st_asm330lhhx_is_fifo_enabled(hw)) {
		err = st_asm330lhhx_suspend_fifo(hw);
		if (err < 0)
			return err;
	}

	err = st_asm330lhhx_bk_regs(hw);

	if (device_may_wakeup(dev))
		enable_irq_wake(hw->irq);

	return err < 0 ? err : 0;
}

static int __maybe_unused st_asm330lhhx_resume(struct device *dev)
{
	struct st_asm330lhhx_hw *hw = dev_get_drvdata(dev);
	struct st_asm330lhhx_sensor *sensor;
	int i, err = 0;

	dev_info(dev, "Resuming device\n");

	if (device_may_wakeup(dev))
		disable_irq_wake(hw->irq);

	err = st_asm330lhhx_restore_regs(hw);
	if (err < 0)
		return err;

	for (i = 0; i < ST_ASM330LHHX_ID_MAX; i++) {
		if (!hw->iio_devs[i])
			continue;

		sensor = iio_priv(hw->iio_devs[i]);
		if (!(hw->enable_mask & BIT_ULL(sensor->id)))
			continue;

		err = st_asm330lhhx_set_odr(sensor, sensor->odr, sensor->uodr);
		if (err < 0)
			return err;
	}

	err = st_asm330lhhx_reset_hwts(hw);
	if (err < 0)
		return err;

	if (st_asm330lhhx_is_fifo_enabled(hw))
		err = st_asm330lhhx_set_fifo_mode(hw, ST_ASM330LHHX_FIFO_CONT);

	enable_irq(hw->irq);

	return err < 0 ? err : 0;
}

static int __maybe_unused st_asm330lhhx_freeze(struct device *dev)
{
	struct st_asm330lhhx_hw *hw = dev_get_drvdata(dev);
	struct st_asm330lhhx_sensor *sensor;
	int i, err = 0;

	dev_info(dev, "Freeze device\n");

	disable_hardirq(hw->irq);

	for (i = 0; i < ST_ASM330LHHX_ID_MAX; i++) {
		if (!hw->iio_devs[i])
			continue;

		sensor = iio_priv(hw->iio_devs[i]);
		if (!(hw->enable_mask & BIT_ULL(sensor->id)))
			continue;

		/* power off enabled sensors */
		err = st_asm330lhhx_set_odr(sensor, 0, 0);
		if (err < 0)
			return err;
	}

	if (st_asm330lhhx_is_fifo_enabled(hw)) {
		err = st_asm330lhhx_suspend_fifo(hw);
		if (err < 0)
			return err;
	}

	err = st_asm330lhhx_bk_regs(hw);

#ifdef CONFIG_IIO_ST_ASM330LHHX_MAY_WAKEUP
	if (device_may_wakeup(dev))
		enable_irq_wake(hw->irq);
#endif /* CONFIG_IIO_ST_ASM330LHHX_MAY_WAKEUP */


	st_asm330lhh_regulator_power_down(hw);

	return err < 0 ? err : 0;
}

static int __maybe_unused st_asm330lhhx_restore(struct device *dev)
{
	struct st_asm330lhhx_hw *hw = dev_get_drvdata(dev);
	struct st_asm330lhhx_sensor *sensor;
	int i, err = 0;

	dev_info(dev, "Restore device\n");
	err = st_asm330lhh_regulator_power_up(hw);
	if (err < 0) {
		dev_err(hw->dev, "regulator power up failed\n");
		return err;
	}

	/* allow time for enabling regulators */
	usleep_range(1000, 2000);

#ifdef CONFIG_IIO_ST_ASM330LHHX_MAY_WAKEUP
	if (device_may_wakeup(dev))
		disable_irq_wake(hw->irq);
#endif /* CONFIG_IIO_ST_ASM330LHHX_MAY_WAKEUP */
	err = st_asm330lhhx_restore_regs(hw);
	if (err < 0)
		return err;

	for (i = 0; i < ST_ASM330LHHX_ID_MAX; i++) {
		if (!hw->iio_devs[i])
			continue;

		sensor = iio_priv(hw->iio_devs[i]);
		if (!(hw->enable_mask & BIT_ULL(sensor->id)))
			continue;

		err = st_asm330lhhx_set_odr(sensor, sensor->odr, sensor->uodr);
		if (err < 0)
			return err;
	}

	err = st_asm330lhhx_reset_hwts(hw);
	if (err < 0)
		return err;

	if (st_asm330lhhx_is_fifo_enabled(hw))
		err = st_asm330lhhx_set_fifo_mode(hw, ST_ASM330LHHX_FIFO_CONT);

	enable_irq(hw->irq);

	return err < 0 ? err : 0;
}


const struct dev_pm_ops st_asm330lhhx_pm_ops = {
	.suspend = st_asm330lhhx_suspend,
	.resume  = st_asm330lhhx_resume,
	.freeze  = st_asm330lhhx_freeze,
	.restore = st_asm330lhhx_restore,
};
EXPORT_SYMBOL(st_asm330lhhx_pm_ops);

MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_DESCRIPTION("STMicroelectronics st_asm330lhhx driver");
MODULE_LICENSE("GPL v2");
