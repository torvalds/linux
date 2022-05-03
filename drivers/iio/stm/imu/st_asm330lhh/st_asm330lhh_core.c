/*
 * STMicroelectronics st_asm330lhh sensor driver
 *
 * Copyright 2019 STMicroelectronics Inc.
 *
 * Lorenzo Bianconi <lorenzo.bianconi@st.com>
 * Mario Tesi <mario.tesi@st.com>
 *
 * Licensed under the GPL-2.
 */

/*
 * Revision history:
 * 1.0: Added version
 *      Added voltage regulator
 * 1.1: Added self test procedure
 */
#include <linux/kernel.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/regulator/consumer.h>
#include <linux/version.h>

#include <linux/platform_data/st_sensors_pdata.h>

#include "st_asm330lhh.h"

static struct st_asm330lhh_selftest_table {
	char *string_mode;
	u8 accel_value;
	u8 gyro_value;
	u8 gyro_mask;
} st_asm330lhh_selftest_table[] = {
	[0] = {
		.string_mode = "disabled",
		.accel_value = ST_ASM330LHH_SELF_TEST_DISABLED_VAL,
		.gyro_value = ST_ASM330LHH_SELF_TEST_DISABLED_VAL,
	},
	[1] = {
		.string_mode = "positive-sign",
		.accel_value = ST_ASM330LHH_SELF_TEST_POS_SIGN_VAL,
		.gyro_value = ST_ASM330LHH_SELF_TEST_POS_SIGN_VAL
	},
	[2] = {
		.string_mode = "negative-sign",
		.accel_value = ST_ASM330LHH_SELF_TEST_NEG_ACCEL_SIGN_VAL,
		.gyro_value = ST_ASM330LHH_SELF_TEST_NEG_GYRO_SIGN_VAL
	},
};

static struct st_asm330lhh_suspend_resume_entry
	st_asm330lhh_suspend_resume[ST_ASM330LHH_SUSPEND_RESUME_REGS] = {
	[ST_ASM330LHH_CTRL1_XL_REG] = {
		.addr = ST_ASM330LHH_CTRL1_XL_ADDR,
		.mask = GENMASK(3, 2),
	},
	[ST_ASM330LHH_CTRL2_G_REG] = {
		.addr = ST_ASM330LHH_CTRL2_G_ADDR,
		.mask = GENMASK(3, 2),
	},
	[ST_ASM330LHH_REG_CTRL3_C_REG] = {
		.addr = ST_ASM330LHH_REG_CTRL3_C_ADDR,
		.mask = ST_ASM330LHH_REG_BDU_MASK	|
			ST_ASM330LHH_REG_PP_OD_MASK	|
			ST_ASM330LHH_REG_H_LACTIVE_MASK,
	},
	[ST_ASM330LHH_REG_CTRL4_C_REG] = {
		.addr = ST_ASM330LHH_REG_CTRL4_C_ADDR,
		.mask = ST_ASM330LHH_REG_DRDY_MASK,
	},
	[ST_ASM330LHH_REG_CTRL5_C_REG] = {
		.addr = ST_ASM330LHH_REG_CTRL5_C_ADDR,
		.mask = ST_ASM330LHH_REG_ROUNDING_MASK,
	},
	[ST_ASM330LHH_REG_CTRL10_C_REG] = {
		.addr = ST_ASM330LHH_REG_CTRL10_C_ADDR,
		.mask = ST_ASM330LHH_REG_TIMESTAMP_EN_MASK,
	},
	[ST_ASM330LHH_REG_TAP_CFG0_REG] = {
		.addr = ST_ASM330LHH_REG_TAP_CFG0_ADDR,
		.mask = ST_ASM330LHH_REG_LIR_MASK,
	},
	[ST_ASM330LHH_REG_INT1_CTRL_REG] = {
		.addr = ST_ASM330LHH_REG_INT1_CTRL_ADDR,
		.mask = ST_ASM330LHH_REG_INT_FIFO_TH_MASK,
	},
	[ST_ASM330LHH_REG_INT2_CTRL_REG] = {
		.addr = ST_ASM330LHH_REG_INT2_CTRL_ADDR,
		.mask = ST_ASM330LHH_REG_INT_FIFO_TH_MASK,
	},
	[ST_ASM330LHH_REG_FIFO_CTRL1_REG] = {
		.addr = ST_ASM330LHH_REG_FIFO_CTRL1_ADDR,
		.mask = GENMASK(7, 0),
	},
	[ST_ASM330LHH_REG_FIFO_CTRL2_REG] = {
		.addr = ST_ASM330LHH_REG_FIFO_CTRL2_ADDR,
		.mask = ST_ASM330LHH_REG_FIFO_WTM8_MASK,
	},
	[ST_ASM330LHH_REG_FIFO_CTRL3_REG] = {
		.addr = ST_ASM330LHH_REG_FIFO_CTRL3_ADDR,
		.mask = ST_ASM330LHH_REG_BDR_XL_MASK |
			ST_ASM330LHH_REG_BDR_GY_MASK,
	},
	[ST_ASM330LHH_REG_FIFO_CTRL4_REG] = {
		.addr = ST_ASM330LHH_REG_FIFO_CTRL4_ADDR,
		.mask = ST_ASM330LHH_REG_DEC_TS_MASK |
			ST_ASM330LHH_REG_ODR_T_BATCH_MASK,
	},
};

static const struct st_asm330lhh_odr_table_entry st_asm330lhh_odr_table[] = {
	[ST_ASM330LHH_ID_ACC] = {
		.size = 7,
		.reg = {
			.addr = ST_ASM330LHH_CTRL1_XL_ADDR,
			.mask = GENMASK(7, 4),
		},
		.batching_reg = {
			.addr = ST_ASM330LHH_REG_FIFO_CTRL3_ADDR,
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
	[ST_ASM330LHH_ID_GYRO] = {
		.size = 7,
		.reg = {
			.addr = ST_ASM330LHH_CTRL2_G_ADDR,
			.mask = GENMASK(7, 4),
		},
		.batching_reg = {
			.addr = ST_ASM330LHH_REG_FIFO_CTRL3_ADDR,
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
#ifdef CONFIG_IIO_ST_ASM330LHH_EN_TEMPERATURE
	[ST_ASM330LHH_ID_TEMP] = {
		.size = 2,
		.batching_reg = {
			.addr = ST_ASM330LHH_REG_FIFO_CTRL4_ADDR,
			.mask = GENMASK(5, 4),
		},
		.odr_avl[0] = { 12, 500000,   0x02,  0x02 },
		.odr_avl[1] = { 52,      0,   0x03,  0x03 },
	},
#endif /* CONFIG_IIO_ST_ASM330LHH_EN_TEMPERATURE */
};

static const struct st_asm330lhh_fs_table_entry st_asm330lhh_fs_table[] = {
	[ST_ASM330LHH_ID_ACC] = {
		.size = ST_ASM330LHH_FS_ACC_LIST_SIZE,
		.fs_avl[0] = {
			.reg = {
				.addr = ST_ASM330LHH_CTRL1_XL_ADDR,
				.mask = GENMASK(3, 2),
			},
			.gain = ST_ASM330LHH_ACC_FS_2G_GAIN,
			.val = 0x0,
		},
		.fs_avl[1] = {
			.reg = {
				.addr = ST_ASM330LHH_CTRL1_XL_ADDR,
				.mask = GENMASK(3, 2),
			},
			.gain = ST_ASM330LHH_ACC_FS_4G_GAIN,
			.val = 0x2,
		},
		.fs_avl[2] = {
			.reg = {
				.addr = ST_ASM330LHH_CTRL1_XL_ADDR,
				.mask = GENMASK(3, 2),
			},
			.gain = ST_ASM330LHH_ACC_FS_8G_GAIN,
			.val = 0x3,
		},
		.fs_avl[3] = {
			.reg = {
				.addr = ST_ASM330LHH_CTRL1_XL_ADDR,
				.mask = GENMASK(3, 2),
			},
			.gain = ST_ASM330LHH_ACC_FS_16G_GAIN,
			.val = 0x1,
		},
	},
	[ST_ASM330LHH_ID_GYRO] = {
		.size = ST_ASM330LHH_FS_GYRO_LIST_SIZE,
		.fs_avl[0] = {
			.reg = {
				.addr = ST_ASM330LHH_CTRL2_G_ADDR,
				.mask = GENMASK(3, 0),
			},
			.gain = ST_ASM330LHH_GYRO_FS_250_GAIN,
			.val = 0x0,
		},
		.fs_avl[1] = {
			.reg = {
				.addr = ST_ASM330LHH_CTRL2_G_ADDR,
				.mask = GENMASK(3, 0),
			},
			.gain = ST_ASM330LHH_GYRO_FS_500_GAIN,
			.val = 0x4,
		},
		.fs_avl[2] = {
			.reg = {
				.addr = ST_ASM330LHH_CTRL2_G_ADDR,
				.mask = GENMASK(3, 0),
			},
			.gain = ST_ASM330LHH_GYRO_FS_1000_GAIN,
			.val = 0x8,
		},
		.fs_avl[3] = {
			.reg = {
				.addr = ST_ASM330LHH_CTRL2_G_ADDR,
				.mask = GENMASK(3, 0),
			},
			.gain = ST_ASM330LHH_GYRO_FS_2000_GAIN,
			.val = 0x0C,
		},
		.fs_avl[4] = {
			.reg = {
				.addr = ST_ASM330LHH_CTRL2_G_ADDR,
				.mask = GENMASK(3, 0),
			},
			.gain = ST_ASM330LHH_GYRO_FS_4000_GAIN,
			.val = 0x1,
		},
	},
#ifdef CONFIG_IIO_ST_ASM330LHH_EN_TEMPERATURE
	[ST_ASM330LHH_ID_TEMP] = {
		.size = ST_ASM330LHH_FS_TEMP_LIST_SIZE,
		.fs_avl[0] = {
			.gain = ST_ASM330LHH_TEMP_FS_GAIN,
			.val = 0x0
		},
	},
#endif /* CONFIG_IIO_ST_ASM330LHH_EN_TEMPERATURE */
};

#ifdef CONFIG_IIO_ST_ASM330LHH_EN_BASIC_FEATURES
static const struct st_asm330lhh_ff_th st_asm330lhh_free_fall_threshold[] = {
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

static const struct st_asm330lhh_6D_th st_asm330lhh_6D_threshold[] = {
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
#endif /* CONFIG_IIO_ST_ASM330LHH_EN_BASIC_FEATURES */

static const inline struct iio_mount_matrix *
st_asm330lhh_get_mount_matrix(const struct iio_dev *iio_dev,
			      const struct iio_chan_spec *chan)
{
	struct st_asm330lhh_sensor *sensor = iio_priv(iio_dev);
	struct st_asm330lhh_hw *hw = sensor->hw;

	return &hw->orientation;
}

static const struct iio_chan_spec_ext_info st_asm330lhh_ext_info[] = {
	IIO_MOUNT_MATRIX(IIO_SHARED_BY_ALL, st_asm330lhh_get_mount_matrix),
	{},
};

#define IIO_CHAN_HW_TIMESTAMP(si) {					\
	.type = IIO_COUNT,						\
	.address = ST_ASM330LHH_REG_TIMESTAMP0_ADDR,			\
	.scan_index = si,						\
	.scan_type = {							\
		.sign = 's',						\
		.realbits = 64,						\
		.storagebits = 64,					\
		.endianness = IIO_LE,					\
	},								\
}

static const struct iio_chan_spec st_asm330lhh_acc_channels[] = {
	ST_ASM330LHH_DATA_CHANNEL(IIO_ACCEL, ST_ASM330LHH_REG_OUTX_L_A_ADDR,
				1, IIO_MOD_X, 0, 16, 16, 's'),
	ST_ASM330LHH_DATA_CHANNEL(IIO_ACCEL, ST_ASM330LHH_REG_OUTY_L_A_ADDR,
				1, IIO_MOD_Y, 1, 16, 16, 's'),
	ST_ASM330LHH_DATA_CHANNEL(IIO_ACCEL, ST_ASM330LHH_REG_OUTZ_L_A_ADDR,
				1, IIO_MOD_Z, 2, 16, 16, 's'),
	ST_ASM330LHH_EVENT_CHANNEL(IIO_ACCEL, flush),
	IIO_CHAN_HW_TIMESTAMP(3),
	IIO_CHAN_SOFT_TIMESTAMP(4),
};

static const struct iio_chan_spec st_asm330lhh_gyro_channels[] = {
	ST_ASM330LHH_DATA_CHANNEL(IIO_ANGL_VEL, ST_ASM330LHH_REG_OUTX_L_G_ADDR,
				1, IIO_MOD_X, 0, 16, 16, 's'),
	ST_ASM330LHH_DATA_CHANNEL(IIO_ANGL_VEL, ST_ASM330LHH_REG_OUTY_L_G_ADDR,
				1, IIO_MOD_Y, 1, 16, 16, 's'),
	ST_ASM330LHH_DATA_CHANNEL(IIO_ANGL_VEL, ST_ASM330LHH_REG_OUTZ_L_G_ADDR,
				1, IIO_MOD_Z, 2, 16, 16, 's'),
	ST_ASM330LHH_EVENT_CHANNEL(IIO_ANGL_VEL, flush),
	IIO_CHAN_HW_TIMESTAMP(3),
	IIO_CHAN_SOFT_TIMESTAMP(4),
};

static
__maybe_unused const struct iio_chan_spec st_asm330lhh_temp_channels[] = {
	{
		.type = IIO_TEMP,
		.address = ST_ASM330LHH_REG_OUT_TEMP_L_ADDR,
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
	ST_ASM330LHH_EVENT_CHANNEL(IIO_TEMP, flush),
	IIO_CHAN_HW_TIMESTAMP(1),
	IIO_CHAN_SOFT_TIMESTAMP(2),
};

int __st_asm330lhh_write_with_mask(struct st_asm330lhh_hw *hw, u8 addr, u8 mask,
				 u8 val)
{
	u8 data, old_data;
	int err;

	mutex_lock(&hw->lock);

	err = hw->tf->read(hw->dev, addr, sizeof(old_data), &old_data);
	if (err < 0) {
		dev_err(hw->dev, "failed to read %02x register\n", addr);
		goto out;
	}

	data = (old_data & ~mask) | ((val << __ffs(mask)) & mask);

	/* avoid to write same value */
	if (old_data == data)
		goto out;

	err = hw->tf->write(hw->dev, addr, sizeof(data), &data);
	if (err < 0)
		dev_err(hw->dev, "failed to write %02x register\n", addr);

out:
	mutex_unlock(&hw->lock);

	return (err < 0) ? err : 0;
}

int __maybe_unused st_asm330lhh_read_with_mask(struct st_asm330lhh_hw *hw, u8 addr, u8 mask,
				u8 *val)
{
	u8 data;
	int err;

	err = hw->tf->read(hw->dev, addr, sizeof(data), &data);
	if (err < 0) {
		dev_err(hw->dev, "failed to read %02x register\n", addr);
		goto out;
	}

	*val = (data & mask) >> __ffs(mask);

out:
	return (err < 0) ? err : 0;
}

static int st_asm330lhh_of_get_pin(struct st_asm330lhh_hw *hw, int *pin)
{
	if (!dev_fwnode(hw->dev))
		return -EINVAL;

	return device_property_read_u32(hw->dev, "st,int-pin", pin);
}

static int st_asm330lhh_get_int_reg(struct st_asm330lhh_hw *hw, u8 *drdy_reg)
{
	int err = 0, int_pin;

	if (st_asm330lhh_of_get_pin(hw, &int_pin) < 0) {
		struct st_sensors_platform_data *pdata;
		struct device *dev = hw->dev;

		pdata = (struct st_sensors_platform_data *)dev->platform_data;
		int_pin = pdata ? pdata->drdy_int_pin : 1;
	}

	switch (int_pin) {
	case 1:
		*drdy_reg = ST_ASM330LHH_REG_INT1_CTRL_ADDR;
		break;
	case 2:
		*drdy_reg = ST_ASM330LHH_REG_INT2_CTRL_ADDR;
		break;
	default:
		dev_err(hw->dev, "unsupported interrupt pin\n");
		err = -EINVAL;
		break;
	}

	hw->int_pin = int_pin;

	return err;
}

static int __maybe_unused st_asm330lhh_bk_regs(struct st_asm330lhh_hw *hw)
{
	int i, err = 0;
	u8 data, addr;

	mutex_lock(&hw->page_lock);
	for (i = 0; i < ST_ASM330LHH_SUSPEND_RESUME_REGS; i++) {
		addr = st_asm330lhh_suspend_resume[i].addr;
		err = hw->tf->read(hw->dev, addr, sizeof(data), &data);
		if (err < 0) {
			dev_err(hw->dev, "failed to read whoami register\n");
			goto out_lock;
		}

		st_asm330lhh_suspend_resume[i].val = data;
	}

out_lock:
	mutex_unlock(&hw->page_lock);

	return err;
}

static int __maybe_unused st_asm330lhh_restore_regs(struct st_asm330lhh_hw *hw)
{
	int i, err = 0;
	u8 data, addr;

	mutex_lock(&hw->page_lock);
	for (i = 0; i < ST_ASM330LHH_SUSPEND_RESUME_REGS; i++) {
		addr = st_asm330lhh_suspend_resume[i].addr;
		err = hw->tf->read(hw->dev, addr, sizeof(data), &data);
		if (err < 0) {
			dev_err(hw->dev, "failed to read %02x reg\n", addr);
			goto out_lock;
		}

		data &= ~st_asm330lhh_suspend_resume[i].mask;
		data |= (st_asm330lhh_suspend_resume[i].val &
			 st_asm330lhh_suspend_resume[i].mask);

		err = hw->tf->write(hw->dev, addr, sizeof(data), &data);
		if (err < 0) {
			dev_err(hw->dev, "failed to write %02x reg\n", addr);
			goto out_lock;
		}
	}

out_lock:
	mutex_unlock(&hw->page_lock);

	return err;
}

static int st_asm330lhh_set_selftest(
				struct st_asm330lhh_sensor *sensor, int index)
{
	u8 mode, mask;

	switch (sensor->id) {
	case ST_ASM330LHH_ID_ACC:
		mask = ST_ASM330LHH_REG_ST_XL_MASK;
		mode = st_asm330lhh_selftest_table[index].accel_value;
		break;
	case ST_ASM330LHH_ID_GYRO:
		mask = ST_ASM330LHH_REG_ST_G_MASK;
		mode = st_asm330lhh_selftest_table[index].gyro_value;
		break;
	default:
		return -EINVAL;
	}

	return st_asm330lhh_write_with_mask(sensor->hw,
					    ST_ASM330LHH_REG_CTRL5_C_ADDR,
					    mask, mode);
}

static ssize_t st_asm330lhh_sysfs_get_selftest_available(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s, %s\n",
		       st_asm330lhh_selftest_table[1].string_mode,
		       st_asm330lhh_selftest_table[2].string_mode);
}

static ssize_t st_asm330lhh_sysfs_get_selftest_status(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int8_t result;
	char *message;
	struct st_asm330lhh_sensor *sensor = iio_priv(dev_get_drvdata(dev));
	enum st_asm330lhh_sensor_id id = sensor->id;

	if (id != ST_ASM330LHH_ID_ACC &&
	    id != ST_ASM330LHH_ID_GYRO)
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

#ifdef CONFIG_IIO_ST_ASM330LHH_EN_BASIC_FEATURES
/*
 * st_asm330lhh_set_wake_up_thershold - set wake-up threshold in ug
 * @hw - ST IMU MEMS hw instance
 * @th_ug - wake-up threshold in ug (micro g)
 *
 * wake-up thershold register val = (th_ug * 2 ^ 6) / (1000000 * FS_XL)
 */
int st_asm330lhh_set_wake_up_thershold(struct st_asm330lhh_hw *hw, int th_ug)
{
	struct st_asm330lhh_sensor *sensor;
	struct iio_dev *iio_dev;
	u8 val, fs_xl, max_th;
	int tmp, err;

	err = st_asm330lhh_read_with_mask(hw,
		st_asm330lhh_fs_table[ST_ASM330LHH_ID_ACC].fs_avl[0].reg.addr,
		st_asm330lhh_fs_table[ST_ASM330LHH_ID_ACC].fs_avl[0].reg.mask,
		&fs_xl);
	if (err < 0)
		return err;

	tmp = (th_ug * 64) / (fs_xl * 1000000);
	val = (u8)tmp;
	max_th = ST_ASM330LHH_WAKE_UP_THS_MASK >>
		  __ffs(ST_ASM330LHH_WAKE_UP_THS_MASK);
	if (val > max_th)
		val = max_th;

	err = st_asm330lhh_write_with_mask(hw,
					   ST_ASM330LHH_REG_WAKE_UP_THS_ADDR,
					   ST_ASM330LHH_WAKE_UP_THS_MASK, val);
	if (err < 0)
		return err;

	iio_dev = hw->iio_devs[ST_ASM330LHH_ID_WK];
	sensor = iio_priv(iio_dev);
	sensor->conf[0] = th_ug;

	return 0;
}

/*
 * st_asm330lhh_set_wake_up_duration - set wake-up duration in ms
 * @hw - ST IMU MEMS hw instance
 * @dur_ms - wake-up duration in ms
 *
 * wake-up duration register val is related to XL ODR
 */
int st_asm330lhh_set_wake_up_duration(struct st_asm330lhh_hw *hw, int dur_ms)
{
	struct st_asm330lhh_sensor *sensor;
	struct iio_dev *iio_dev;
	int i, tmp, sensor_odr, err;
	u8 val, odr_xl, max_dur;

	err = st_asm330lhh_read_with_mask(hw,
		st_asm330lhh_odr_table[ST_ASM330LHH_ID_ACC].reg.addr,
		st_asm330lhh_odr_table[ST_ASM330LHH_ID_ACC].reg.mask,
		&odr_xl);
	if (err < 0)
		return err;

	if (odr_xl == 0) {
		dev_info(hw->dev, "use default ODR\n");
		odr_xl = st_asm330lhh_odr_table[ST_ASM330LHH_ID_ACC].odr_avl[ST_ASM330LHH_DEFAULT_XL_ODR_INDEX].val;
	}

	for (i = 0; i < st_asm330lhh_odr_table[ST_ASM330LHH_ID_ACC].size; i++) {
		if (odr_xl ==
		     st_asm330lhh_odr_table[ST_ASM330LHH_ID_ACC].odr_avl[i].val)
			break;
	}

	if (i == st_asm330lhh_odr_table[ST_ASM330LHH_ID_ACC].size)
		return -EINVAL;


	sensor_odr = ST_ASM330LHH_ODR_EXPAND(
		st_asm330lhh_odr_table[ST_ASM330LHH_ID_ACC].odr_avl[i].hz,
		st_asm330lhh_odr_table[ST_ASM330LHH_ID_ACC].odr_avl[i].uhz);

	tmp = dur_ms / (1000000 / (sensor_odr / 1000));
	val = (u8)tmp;
	max_dur = ST_ASM330LHH_WAKE_UP_DUR_MASK >>
		  __ffs(ST_ASM330LHH_WAKE_UP_DUR_MASK);
	if (val > max_dur)
		val = max_dur;

	err = st_asm330lhh_write_with_mask(hw,
					   ST_ASM330LHH_REG_WAKE_UP_DUR_ADDR,
					   ST_ASM330LHH_WAKE_UP_DUR_MASK, val);
	if (err < 0)
		return err;

	iio_dev = hw->iio_devs[ST_ASM330LHH_ID_WK];
	sensor = iio_priv(iio_dev);
	sensor->conf[1] = dur_ms;

	return 0;
}

/*
 * st_asm330lhh_set_freefall_threshold - set free fall threshold detection mg
 * @hw - ST IMU MEMS hw instance
 * @th_mg - free fall threshold in mg
 */
int st_asm330lhh_set_freefall_threshold(struct st_asm330lhh_hw *hw, int th_mg)
{
	struct st_asm330lhh_sensor *sensor;
	struct iio_dev *iio_dev;
	int i, err;

	for (i = 0; i < ARRAY_SIZE(st_asm330lhh_free_fall_threshold); i++) {
		if (th_mg >= st_asm330lhh_free_fall_threshold[i].mg)
			break;
	}

	if (i == ARRAY_SIZE(st_asm330lhh_free_fall_threshold))
		return -EINVAL;

	err = st_asm330lhh_write_with_mask(hw,
				ST_ASM330LHH_REG_FREE_FALL_ADDR,
				ST_ASM330LHH_FF_THS_MASK,
				st_asm330lhh_free_fall_threshold[i].val);
	if (err < 0)
		return err;

	iio_dev = hw->iio_devs[ST_ASM330LHH_ID_FF];
	sensor = iio_priv(iio_dev);
	sensor->conf[2] = th_mg;

	return 0;
}

/*
 * st_asm330lhh_set_6D_threshold - set 6D threshold detection in degrees
 * @hw - ST IMU MEMS hw instance
 * @deg - 6D threshold in degrees
 */
int st_asm330lhh_set_6D_threshold(struct st_asm330lhh_hw *hw, int deg)
{
	struct st_asm330lhh_sensor *sensor;
	struct iio_dev *iio_dev;
	int i, err;

	for (i = 0; i < ARRAY_SIZE(st_asm330lhh_6D_threshold); i++) {
		if (deg >= st_asm330lhh_6D_threshold[i].deg)
			break;
	}

	if (i == ARRAY_SIZE(st_asm330lhh_6D_threshold))
		return -EINVAL;

	err = st_asm330lhh_write_with_mask(hw,
				ST_ASM330LHH_REG_THS_6D_ADDR,
				ST_ASM330LHH_SIXD_THS_MASK,
				st_asm330lhh_6D_threshold[i].val);
	if (err < 0)
		return err;

	iio_dev = hw->iio_devs[ST_ASM330LHH_ID_6D];
	sensor = iio_priv(iio_dev);
	sensor->conf[3] = deg;

	return 0;
}
#endif /* CONFIG_IIO_ST_ASM330LHH_EN_BASIC_FEATURES */

static __maybe_unused int st_asm330lhh_reg_access(struct iio_dev *iio_dev,
				 unsigned int reg, unsigned int writeval,
				 unsigned int *readval)
{
	struct st_asm330lhh_sensor *sensor = iio_priv(iio_dev);
	int ret;

	ret = iio_device_claim_direct_mode(iio_dev);
	if (ret)
		return ret;

	if (readval == NULL) {
		ret = sensor->hw->tf->write(sensor->hw->dev, reg, 1,
					    (u8 *)&writeval);
	} else {
		ret = sensor->hw->tf->read(sensor->hw->dev, reg, 1,
					   (u8 *)readval);
	}
	iio_device_release_direct_mode(iio_dev);

	return (ret < 0) ? ret : 0;
}

static int st_asm330lhh_check_whoami(struct st_asm330lhh_hw *hw)
{
	u8 data;
	int err;

	err = hw->tf->read(hw->dev, ST_ASM330LHH_REG_WHOAMI_ADDR, sizeof(data),
			   &data);
	if (err < 0) {
		dev_err(hw->dev, "failed to read whoami register\n");
		return err;
	}

	if (data != ST_ASM330LHH_WHOAMI_VAL) {
		dev_err(hw->dev, "unsupported whoami [%02x]\n", data);
		return -ENODEV;
	}

	return err;
}

static int st_asm330lhh_get_odr_calibration(struct st_asm330lhh_hw *hw)
{
	s64 odr_calib;
	int err;
	s8 data;

	err = hw->tf->read(hw->dev,
			   ST_ASM330LHH_INTERNAL_FREQ_FINE,
			   sizeof(data), (u8 *)&data);
	if (err < 0) {
		dev_err(hw->dev, "failed to read %d register\n",
				ST_ASM330LHH_INTERNAL_FREQ_FINE);
		return err;
	}

	odr_calib = (data * 37500) / 1000;
	hw->ts_delta_ns = ST_ASM330LHH_TS_DELTA_NS - odr_calib;

	dev_info(hw->dev, "Freq Fine %lld (ts %lld)\n", odr_calib, hw->ts_delta_ns);

	return 0;
}

static int st_asm330lhh_set_full_scale(struct st_asm330lhh_sensor *sensor,
				     u32 gain)
{
	enum st_asm330lhh_sensor_id id = sensor->id;
	int i, err;
	u8 val;

	/* for other sensors gain is fixed */
	if (id > ST_ASM330LHH_ID_ACC)
		return 0;

	for (i = 0; i < st_asm330lhh_fs_table[id].size; i++)
		if (st_asm330lhh_fs_table[id].fs_avl[i].gain >= gain)
			break;

	if (i == st_asm330lhh_fs_table[id].size)
		return -EINVAL;

	val = st_asm330lhh_fs_table[id].fs_avl[i].val;
	err = st_asm330lhh_write_with_mask(sensor->hw,
				st_asm330lhh_fs_table[id].fs_avl[i].reg.addr,
				st_asm330lhh_fs_table[id].fs_avl[i].reg.mask,
				val);
	if (err < 0)
		return err;

	sensor->gain = st_asm330lhh_fs_table[id].fs_avl[i].gain;

	return 0;
}

static int st_asm330lhh_get_odr_val(struct st_asm330lhh_sensor *sensor, int odr,
			     int uodr, int *podr, int *puodr, u8 *val)
{
	int required_odr = ST_ASM330LHH_ODR_EXPAND(odr, uodr);
	enum st_asm330lhh_sensor_id id = sensor->id;
	int sensor_odr;
	int i;

	for (i = 0; i < st_asm330lhh_odr_table[id].size; i++) {
		sensor_odr = ST_ASM330LHH_ODR_EXPAND(
				st_asm330lhh_odr_table[id].odr_avl[i].hz,
				st_asm330lhh_odr_table[id].odr_avl[i].uhz);
		if (sensor_odr >= required_odr)
			break;
	}

	if (i == st_asm330lhh_odr_table[id].size)
		return -EINVAL;

	*val = st_asm330lhh_odr_table[id].odr_avl[i].val;

	if (podr && puodr) {
		*podr = st_asm330lhh_odr_table[id].odr_avl[i].hz;
		*puodr = st_asm330lhh_odr_table[id].odr_avl[i].uhz;
    }

	return 0;
}

int st_asm330lhh_get_batch_val(struct st_asm330lhh_sensor *sensor,
			       int odr, int uodr, u8 *val)
{
	int required_odr = ST_ASM330LHH_ODR_EXPAND(odr, uodr);
	enum st_asm330lhh_sensor_id id = sensor->id;
	int sensor_odr;
	int i;

	for (i = 0; i < st_asm330lhh_odr_table[id].size; i++) {
		sensor_odr = ST_ASM330LHH_ODR_EXPAND(
				st_asm330lhh_odr_table[id].odr_avl[i].hz,
				st_asm330lhh_odr_table[id].odr_avl[i].uhz);
		if (sensor_odr >= required_odr)
			break;
	}

	if (i == st_asm330lhh_odr_table[id].size)
		return -EINVAL;

	*val = st_asm330lhh_odr_table[id].odr_avl[i].batch_val;

	return 0;
}

static u16 st_asm330lhh_check_odr_dependency(struct st_asm330lhh_hw *hw,
					   int odr, int uodr,
					   enum st_asm330lhh_sensor_id ref_id)
{
	struct st_asm330lhh_sensor *ref = iio_priv(hw->iio_devs[ref_id]);
	bool enable = ST_ASM330LHH_ODR_EXPAND(odr, uodr) > 0;
	u16 ret;

	if (enable) {
		/* uodr not used */
		if (hw->enable_mask & BIT(ref_id))
			ret = max_t(u16, ref->odr, odr);
		else
			ret = odr;
	} else {
		ret = (hw->enable_mask & BIT(ref_id)) ? ref->odr : 0;
	}

	return ret;
}

static int st_asm330lhh_set_odr(struct st_asm330lhh_sensor *sensor, int req_odr,
				int req_uodr)
{
	enum st_asm330lhh_sensor_id id = sensor->id;
	struct st_asm330lhh_hw *hw = sensor->hw;
	u8 val = 0;
	int err;

	switch (id) {
	case ST_ASM330LHH_ID_WK:
	case ST_ASM330LHH_ID_FF:
	case ST_ASM330LHH_ID_SC:
	case ST_ASM330LHH_ID_6D:
#ifdef CONFIG_IIO_ST_ASM330LHH_EN_TEMPERATURE
	case ST_ASM330LHH_ID_TEMP:
#endif /* CONFIG_IIO_ST_ASM330LHH_EN_TEMPERATURE */
	case ST_ASM330LHH_ID_ACC: {
		int odr;
		int i;

		id = ST_ASM330LHH_ID_ACC;
		for (i = ST_ASM330LHH_ID_ACC; i < ST_ASM330LHH_ID_MAX; i++) {
			if (!hw->iio_devs[i])
				continue;

			if (i == sensor->id)
				continue;

			odr = st_asm330lhh_check_odr_dependency(hw, req_odr,
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

	err = st_asm330lhh_get_odr_val(sensor, req_odr, req_uodr, NULL,
				       NULL, &val);
	if (err < 0)
		return err;

	err = st_asm330lhh_write_with_mask(hw,
					   st_asm330lhh_odr_table[sensor->id].reg.addr,
					   st_asm330lhh_odr_table[sensor->id].reg.mask,
					   val);

	return err < 0 ? err : 0;
}

int st_asm330lhh_sensor_set_enable(struct st_asm330lhh_sensor *sensor,
				 bool enable)
{
	int uodr = enable ? sensor->uodr : 0;
	int odr = enable ? sensor->odr : 0;
	int err;

	err = st_asm330lhh_set_odr(sensor, odr, uodr);
	if (err < 0)
		return err;

	if (enable)
		sensor->hw->enable_mask |= BIT(sensor->id);
	else
		sensor->hw->enable_mask &= ~BIT(sensor->id);

	return 0;
}

static int st_asm330lhh_read_oneshot(struct st_asm330lhh_sensor *sensor,
				   u8 addr, int *val)
{
	int err, delay;
	__le16 data;

	err = st_asm330lhh_sensor_set_enable(sensor, true);
	if (err < 0)
		return err;

	/* Use big delay for data valid because of drdy mask enabled */
	delay = 10000000 / sensor->odr;
	usleep_range(delay, 2 * delay);

	err = st_asm330lhh_read_atomic(sensor->hw, addr, sizeof(data),
				       (u8 *)&data);
	if (err < 0)
		return err;

	err = st_asm330lhh_sensor_set_enable(sensor, false);

	*val = (s16)le16_to_cpu(data);

	return IIO_VAL_INT;
}

static int st_asm330lhh_read_raw(struct iio_dev *iio_dev,
			       struct iio_chan_spec const *ch,
			       int *val, int *val2, long mask)
{
	struct st_asm330lhh_sensor *sensor = iio_priv(iio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(iio_dev);
		if (ret)
			break;

		ret = st_asm330lhh_read_oneshot(sensor, ch->address, val);
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
#ifdef CONFIG_IIO_ST_ASM330LHH_EN_TEMPERATURE
		case IIO_TEMP:
			*val = 1;
			*val2 = ST_ASM330LHH_TEMP_GAIN;
			ret = IIO_VAL_FRACTIONAL;
			break;
#endif /* CONFIG_IIO_ST_ASM330LHH_EN_TEMPERATURE */
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

static int st_asm330lhh_write_raw(struct iio_dev *iio_dev,
				struct iio_chan_spec const *chan,
				int val, int val2, long mask)
{
	struct st_asm330lhh_sensor *s = iio_priv(iio_dev);
	int err;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		err = iio_device_claim_direct_mode(iio_dev);
		if (err)
			return err;

		err = st_asm330lhh_set_full_scale(s, val2);
		iio_device_release_direct_mode(iio_dev);
		break;
	case IIO_CHAN_INFO_SAMP_FREQ: {
		int todr, tuodr;
		u8 data;

		err = st_asm330lhh_get_odr_val(s, val, val2, &todr, &tuodr, &data);
		if (!err) {
			s->odr = val;
			s->uodr = tuodr;

			/*
			 * VTS test testSamplingRateHotSwitchOperation not
			 * toggle the enable status of sensor after changing
			 * the ODR -> force it
			 */
			if (s->hw->enable_mask & BIT(s->id)) {
				switch (s->id) {
				case ST_ASM330LHH_ID_GYRO:
				case ST_ASM330LHH_ID_ACC:
					err = st_asm330lhh_set_odr(s, s->odr, s->uodr);
					if (err < 0)
						break;

					st_asm330lhh_update_batching(iio_dev, 1);
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
st_asm330lhh_sysfs_sampling_freq_avail(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct st_asm330lhh_sensor *sensor = iio_priv(dev_get_drvdata(dev));
	enum st_asm330lhh_sensor_id id = sensor->id;
	int i, len = 0;

	for (i = 0; i < st_asm330lhh_odr_table[id].size; i++) {
		len += scnprintf(buf + len, PAGE_SIZE - len, "%d.%06d ",
				 st_asm330lhh_odr_table[id].odr_avl[i].hz,
				 st_asm330lhh_odr_table[id].odr_avl[i].uhz);
	}

	buf[len - 1] = '\n';

	return len;
}

static ssize_t st_asm330lhh_sysfs_scale_avail(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct st_asm330lhh_sensor *sensor = iio_priv(dev_get_drvdata(dev));
	enum st_asm330lhh_sensor_id id = sensor->id;
	int i, len = 0;

	for (i = 0; i < st_asm330lhh_fs_table[id].size; i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "0.%09u ",
				 st_asm330lhh_fs_table[id].fs_avl[i].gain);
	buf[len - 1] = '\n';

	return len;
}

static int st_asm330lhh_selftest_sensor(struct st_asm330lhh_sensor *sensor,
					int test)
{
	int x_selftest = 0, y_selftest = 0, z_selftest = 0;
	int x = 0, y = 0, z = 0, try_count = 0;
	u8 i, status, n = 0;
	u8 reg, bitmask;
	int ret, delay;
	u8 raw_data[6];

	switch(sensor->id) {
	case ST_ASM330LHH_ID_ACC:
		reg = ST_ASM330LHH_REG_OUTX_L_A_ADDR;
		bitmask = ST_ASM330LHH_REG_STATUS_XLDA;
		break;
	case ST_ASM330LHH_ID_GYRO:
		reg = ST_ASM330LHH_REG_OUTX_L_G_ADDR;
		bitmask = ST_ASM330LHH_REG_STATUS_GDA;
		break;
	default:
		return -EINVAL;
	}

	/* set selftest normal mode */
	ret =st_asm330lhh_set_selftest(sensor, 0);
	if (ret < 0)
		return ret;

	ret = st_asm330lhh_sensor_set_enable(sensor, true);
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
			ret = st_asm330lhh_read_atomic(sensor->hw,
						ST_ASM330LHH_REG_STATUS_ADDR,
						sizeof(status), &status);
			if (ret < 0)
				goto selftest_failure;

			if (status & bitmask) {
				ret = st_asm330lhh_read_atomic(sensor->hw, reg,
						sizeof(raw_data), raw_data);
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
	st_asm330lhh_set_selftest(sensor, test);

	/* wait 100 ms for stable output */
	msleep(100);

	/* for 5 times, after checking status bit, read the output registers */
	for (i = 0; i < 5; i++) {
		try_count = 0;
		while (try_count < 3) {
			usleep_range(delay, 2 * delay);
			ret = st_asm330lhh_read_atomic(sensor->hw,
						ST_ASM330LHH_REG_STATUS_ADDR,
						sizeof(status), &status);
			if (ret < 0)
				goto selftest_failure;

			if (status & bitmask) {
				ret = st_asm330lhh_read_atomic(sensor->hw, reg,
						sizeof(raw_data), raw_data);
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
	st_asm330lhh_set_selftest(sensor, 0);

	return st_asm330lhh_sensor_set_enable(sensor, false);
}

static ssize_t st_asm330lhh_sysfs_start_selftest(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_get_drvdata(dev);
	struct st_asm330lhh_sensor *sensor = iio_priv(iio_dev);
	enum st_asm330lhh_sensor_id id = sensor->id;
	struct st_asm330lhh_hw *hw = sensor->hw;
	int ret, test;
	u8 drdy_reg;
	u32 gain;

	if (id != ST_ASM330LHH_ID_ACC &&
	    id != ST_ASM330LHH_ID_GYRO)
		return -EINVAL;

	for (test = 0; test < ARRAY_SIZE(st_asm330lhh_selftest_table); test++) {
		if (strncmp(buf, st_asm330lhh_selftest_table[test].string_mode,
			strlen(st_asm330lhh_selftest_table[test].string_mode)) == 0)
			break;
	}

	if (test == ARRAY_SIZE(st_asm330lhh_selftest_table))
		return -EINVAL;

	ret = iio_device_claim_direct_mode(iio_dev);
	if (ret)
		return ret;

	/* self test mode unavailable if sensor enabled */
	if (hw->enable_mask & BIT(id)) {
		ret = -EBUSY;

		goto out_claim;
	}

	st_asm330lhh_bk_regs(hw);

	/* disable FIFO watermak interrupt */
	ret = st_asm330lhh_get_int_reg(hw, &drdy_reg);
	if (ret < 0)
		goto restore_regs;

	ret = st_asm330lhh_write_with_mask(hw, drdy_reg,
					   ST_ASM330LHH_REG_INT_FIFO_TH_MASK,
					   0);
	if (ret < 0)
		goto restore_regs;

	gain = sensor->gain;
	if (id == ST_ASM330LHH_ID_ACC) {
		/* set BDU = 1, FS = 4 g, ODR = 52 Hz */
		st_asm330lhh_set_full_scale(sensor,
					    ST_ASM330LHH_ACC_FS_4G_GAIN);
		st_asm330lhh_set_odr(sensor, 52, 0);
		st_asm330lhh_selftest_sensor(sensor, test);

		/* restore full scale after test */
		st_asm330lhh_set_full_scale(sensor, gain);
	} else {
		/* set BDU = 1, ODR = 208 Hz, FS = 2000 dps */
		st_asm330lhh_set_full_scale(sensor,
					    ST_ASM330LHH_GYRO_FS_2000_GAIN);
		st_asm330lhh_set_odr(sensor, 208, 0);
		st_asm330lhh_selftest_sensor(sensor, test);

		/* restore full scale after test */
		st_asm330lhh_set_full_scale(sensor, gain);
	}

restore_regs:
	st_asm330lhh_restore_regs(hw);

out_claim:
	iio_device_release_direct_mode(iio_dev);

	return size;
}

static IIO_DEV_ATTR_SAMP_FREQ_AVAIL(st_asm330lhh_sysfs_sampling_freq_avail);
static IIO_DEVICE_ATTR(in_accel_scale_available, 0444,
		       st_asm330lhh_sysfs_scale_avail, NULL, 0);
static IIO_DEVICE_ATTR(in_anglvel_scale_available, 0444,
		       st_asm330lhh_sysfs_scale_avail, NULL, 0);
static IIO_DEVICE_ATTR(hwfifo_watermark_max, 0444,
		       st_asm330lhh_get_max_watermark, NULL, 0);
static IIO_DEVICE_ATTR(in_temp_scale_available, 0444,
		       st_asm330lhh_sysfs_scale_avail, NULL, 0);
static IIO_DEVICE_ATTR(hwfifo_flush, 0200, NULL, st_asm330lhh_flush_fifo, 0);
static IIO_DEVICE_ATTR(hwfifo_watermark, 0644, st_asm330lhh_get_watermark,
		       st_asm330lhh_set_watermark, 0);
static IIO_DEVICE_ATTR(selftest_available, S_IRUGO,
		       st_asm330lhh_sysfs_get_selftest_available,
		       NULL, 0);
static IIO_DEVICE_ATTR(selftest, S_IWUSR | S_IRUGO,
		       st_asm330lhh_sysfs_get_selftest_status,
		       st_asm330lhh_sysfs_start_selftest, 0);

static
ssize_t __maybe_unused st_asm330lhh_get_discharded_samples(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct iio_dev *iio_dev = dev_get_drvdata(dev);
	struct st_asm330lhh_sensor *sensor = iio_priv(iio_dev);
	int ret;

	ret = sprintf(buf, "%d\n", sensor->discharged_samples);

	/* reset counter */
	sensor->discharged_samples = 0;

	return ret;
}

static int st_asm330lhh_write_raw_get_fmt(struct iio_dev *indio_dev,
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
		       st_asm330lhh_get_discharded_samples, NULL, 0);

static struct attribute *st_asm330lhh_acc_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_accel_scale_available.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark_max.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark.dev_attr.attr,
	&iio_dev_attr_hwfifo_flush.dev_attr.attr,
	&iio_dev_attr_selftest_available.dev_attr.attr,
	&iio_dev_attr_selftest.dev_attr.attr,
#ifdef ST_ASM330LHH_DEBUG_DISCHARGE
	&iio_dev_attr_discharded_samples.dev_attr.attr,
#endif /* ST_ASM330LHH_DEBUG_DISCHARGE */
	NULL,
};

static const struct attribute_group st_asm330lhh_acc_attribute_group = {
	.attrs = st_asm330lhh_acc_attributes,
};

static const struct iio_info st_asm330lhh_acc_info = {
	.attrs = &st_asm330lhh_acc_attribute_group,
	.read_raw = st_asm330lhh_read_raw,
	.write_raw = st_asm330lhh_write_raw,
	.write_raw_get_fmt = &st_asm330lhh_write_raw_get_fmt,
#ifdef CONFIG_DEBUG_FS
	.debugfs_reg_access = &st_asm330lhh_reg_access,
#endif /* CONFIG_DEBUG_FS */
};

static struct attribute *st_asm330lhh_gyro_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_anglvel_scale_available.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark_max.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark.dev_attr.attr,
	&iio_dev_attr_hwfifo_flush.dev_attr.attr,
	&iio_dev_attr_selftest_available.dev_attr.attr,
	&iio_dev_attr_selftest.dev_attr.attr,
#ifdef ST_ASM330LHH_DEBUG_DISCHARGE
	&iio_dev_attr_discharded_samples.dev_attr.attr,
#endif /* ST_ASM330LHH_DEBUG_DISCHARGE */
	NULL,
};

static const struct attribute_group st_asm330lhh_gyro_attribute_group = {
	.attrs = st_asm330lhh_gyro_attributes,
};

static const struct iio_info st_asm330lhh_gyro_info = {
	.attrs = &st_asm330lhh_gyro_attribute_group,
	.read_raw = st_asm330lhh_read_raw,
	.write_raw = st_asm330lhh_write_raw,
	.write_raw_get_fmt = &st_asm330lhh_write_raw_get_fmt,
};

static struct attribute *st_asm330lhh_temp_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_temp_scale_available.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark_max.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark.dev_attr.attr,
	&iio_dev_attr_hwfifo_flush.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_asm330lhh_temp_attribute_group = {
	.attrs = st_asm330lhh_temp_attributes,
};

static const struct iio_info st_asm330lhh_temp_info = {
	.attrs = &st_asm330lhh_temp_attribute_group,
	.read_raw = st_asm330lhh_read_raw,
	.write_raw = st_asm330lhh_write_raw,
};

static const unsigned long st_asm330lhh_available_scan_masks[] = { BIT(0) |
								   BIT(1) |
								   BIT(2) |
								   BIT(3),
								   0x0 };

static int st_asm330lhh_reset_device(struct st_asm330lhh_hw *hw)
{
	int err;

	/* set configuration bit */
	err = st_asm330lhh_write_with_mask(hw, ST_ASM330LHH_REG_CTRL9_XL_ADDR,
				           ST_ASM330LHH_REG_DEVICE_CONF_MASK, 1);
	if (err < 0)
		return err;

	/* sw reset */
	err = st_asm330lhh_write_with_mask(hw, ST_ASM330LHH_REG_CTRL3_C_ADDR,
					 ST_ASM330LHH_REG_SW_RESET_MASK, 1);
	if (err < 0)
		return err;

	msleep(50);

	/* boot */
	err = st_asm330lhh_write_with_mask(hw, ST_ASM330LHH_REG_CTRL3_C_ADDR,
					 ST_ASM330LHH_REG_BOOT_MASK, 1);

	msleep(50);

	return err;
}

static int st_asm330lhh_init_device(struct st_asm330lhh_hw *hw)
{
	u8 drdy_reg;
	int err;

	/* latch interrupts */
	err = st_asm330lhh_write_with_mask(hw, ST_ASM330LHH_REG_TAP_CFG0_ADDR,
					 ST_ASM330LHH_REG_LIR_MASK, 1);
	if (err < 0)
		return err;

	/* enable Block Data Update */
	err = st_asm330lhh_write_with_mask(hw, ST_ASM330LHH_REG_CTRL3_C_ADDR,
					 ST_ASM330LHH_REG_BDU_MASK, 1);
	if (err < 0)
		return err;

	err = st_asm330lhh_write_with_mask(hw, ST_ASM330LHH_REG_CTRL5_C_ADDR,
					 ST_ASM330LHH_REG_ROUNDING_MASK, 3);
	if (err < 0)
		return err;

	/* init timestamp engine */
	err = st_asm330lhh_write_with_mask(hw, ST_ASM330LHH_REG_CTRL10_C_ADDR,
					ST_ASM330LHH_REG_TIMESTAMP_EN_MASK, 1);
	if (err < 0)
		return err;

	err = st_asm330lhh_get_int_reg(hw, &drdy_reg);
	if (err < 0)
		return err;

	/* enable DRDY MASK for filters settling time */
	err = st_asm330lhh_write_with_mask(hw, ST_ASM330LHH_REG_CTRL4_C_ADDR,
					 ST_ASM330LHH_REG_DRDY_MASK, 1);
	if (err < 0)
		return err;

	/* enable FIFO watermak interrupt */
	return st_asm330lhh_write_with_mask(hw, drdy_reg,
					 ST_ASM330LHH_REG_INT_FIFO_TH_MASK, 1);
}

#ifdef CONFIG_IIO_ST_ASM330LHH_EN_BASIC_FEATURES
static int st_asm330lhh_post_init_device(struct st_asm330lhh_hw *hw)
{
	int err;

	/* Set default wake-up thershold to 93750 ug */
	err = st_asm330lhh_set_wake_up_thershold(hw, 93750);
	if (err < 0)
		return err;

	/* Set default wake-up duration to 0 */
	err = st_asm330lhh_set_wake_up_duration(hw, 0);
	if (err < 0)
		return err;

	/* setting default FF threshold to 312 mg */
	err = st_asm330lhh_set_freefall_threshold(hw, 312);
	if (err < 0)
		return err;

	/* setting default 6D threshold to 60 degrees */
	return st_asm330lhh_set_6D_threshold(hw, 60);
}
#endif /* CONFIG_IIO_ST_ASM330LHH_EN_BASIC_FEATURES */

static struct iio_dev *st_asm330lhh_alloc_iiodev(struct st_asm330lhh_hw *hw,
					       enum st_asm330lhh_sensor_id id)
{
	struct st_asm330lhh_sensor *sensor;
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
	sensor->last_fifo_timestamp = 0;

#ifdef ST_ASM330LHH_DEBUG_DISCHARGE
	sensor->discharged_samples = 0;
#endif /* ST_ASM330LHH_DEBUG_DISCHARGE */

	/*
	 * for acc/gyro the default Android full scale settings are:
	 * Acc FS 8g (78.40 m/s^2)
	 * Gyro FS 1000dps (16.45 radians/sec)
	 */
	switch (id) {
	case ST_ASM330LHH_ID_ACC:
		iio_dev->channels = st_asm330lhh_acc_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_asm330lhh_acc_channels);
		iio_dev->name = "asm330lhh_accel";
		iio_dev->info = &st_asm330lhh_acc_info;
		iio_dev->available_scan_masks =
					st_asm330lhh_available_scan_masks;
		sensor->max_watermark = ST_ASM330LHH_MAX_FIFO_DEPTH;
		sensor->gain = st_asm330lhh_fs_table[id].fs_avl[ST_ASM330LHH_DEFAULT_XL_FS_INDEX].gain;
		sensor->odr = st_asm330lhh_odr_table[id].odr_avl[ST_ASM330LHH_DEFAULT_XL_ODR_INDEX].hz;
		sensor->uodr = st_asm330lhh_odr_table[id].odr_avl[ST_ASM330LHH_DEFAULT_XL_ODR_INDEX].uhz;
		sensor->offset = 0;
		sensor->min_st = ST_ASM330LHH_SELFTEST_ACCEL_MIN;
		sensor->max_st = ST_ASM330LHH_SELFTEST_ACCEL_MAX;
		break;
	case ST_ASM330LHH_ID_GYRO:
		iio_dev->channels = st_asm330lhh_gyro_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_asm330lhh_gyro_channels);
		iio_dev->name = "asm330lhh_gyro";
		iio_dev->info = &st_asm330lhh_gyro_info;
		iio_dev->available_scan_masks =
					st_asm330lhh_available_scan_masks;
		sensor->max_watermark = ST_ASM330LHH_MAX_FIFO_DEPTH;
		sensor->gain = st_asm330lhh_fs_table[id].fs_avl[ST_ASM330LHH_DEFAULT_G_FS_INDEX].gain;
		sensor->odr = st_asm330lhh_odr_table[id].odr_avl[ST_ASM330LHH_DEFAULT_G_ODR_INDEX].hz;
		sensor->uodr = st_asm330lhh_odr_table[id].odr_avl[ST_ASM330LHH_DEFAULT_G_ODR_INDEX].uhz;
		sensor->offset = 0;
		sensor->min_st = ST_ASM330LHH_SELFTEST_GYRO_MIN;
		sensor->max_st = ST_ASM330LHH_SELFTEST_GYRO_MAX;
		break;
#ifdef CONFIG_IIO_ST_ASM330LHH_EN_TEMPERATURE
	case ST_ASM330LHH_ID_TEMP:
		iio_dev->channels = st_asm330lhh_temp_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_asm330lhh_temp_channels);
		iio_dev->name = "asm330lhh_temp";
		iio_dev->info = &st_asm330lhh_temp_info;
		sensor->max_watermark = ST_ASM330LHH_MAX_FIFO_DEPTH;
		sensor->gain = st_asm330lhh_fs_table[id].fs_avl[ST_ASM330LHH_DEFAULT_T_FS_INDEX].gain;
		sensor->odr = st_asm330lhh_odr_table[id].odr_avl[ST_ASM330LHH_DEFAULT_T_ODR_INDEX].hz;
		sensor->uodr = st_asm330lhh_odr_table[id].odr_avl[ST_ASM330LHH_DEFAULT_T_ODR_INDEX].uhz;
		sensor->offset = ST_ASM330LHH_TEMP_OFFSET;
		break;
#endif /* CONFIG_IIO_ST_ASM330LHH_EN_TEMPERATURE */
	default:
		iio_device_free(iio_dev);

		return NULL;
	}

	st_asm330lhh_set_full_scale(sensor, sensor->gain);

	return iio_dev;
}

static void st_asm330lhh_disable_regulator_action(void *_data)
{
	struct st_asm330lhh_hw *hw = _data;

	regulator_disable(hw->vddio_supply);
	regulator_disable(hw->vdd_supply);
}

static int st_asm330lhh_power_enable(struct st_asm330lhh_hw *hw)
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
				       st_asm330lhh_disable_regulator_action,
				       hw);
	if (err) {
		dev_err(hw->dev, "Failed to setup regulator cleanup action %d\n", err);
		return err;
	}

	return 0;
}

int st_asm330lhh_probe(struct device *dev, int irq,
		     const struct st_asm330lhh_transfer_function *tf_ops)
{
	struct st_asm330lhh_hw *hw;
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
	hw->tf = tf_ops;
	hw->odr_table_entry = st_asm330lhh_odr_table;
	hw->hw_timestamp_global = 0;

	err = st_asm330lhh_power_enable(hw);
	if (err != 0)
		return err;

	err = st_asm330lhh_check_whoami(hw);
	if (err < 0)
		return err;

	err = st_asm330lhh_get_odr_calibration(hw);
	if (err < 0)
		return err;

	err = st_asm330lhh_reset_device(hw);
	if (err < 0)
		return err;

	err = st_asm330lhh_init_device(hw);
	if (err < 0)
		return err;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,2,0)
	err = iio_read_mount_matrix(hw->dev, "mount-matrix", &hw->orientation);
	if (err)
		return err;
#else /* LINUX_VERSION_CODE */
	err = of_iio_read_mount_matrix(hw->dev, "mount-matrix", &hw->orientation);
	if (err)
		return err;
#endif /* LINUX_VERSION_CODE */

	for (i = 0; i < ST_ASM330LHH_ID_EVENT; i++) {
		hw->iio_devs[i] = st_asm330lhh_alloc_iiodev(hw, i);
		if (!hw->iio_devs[i])
			return -ENOMEM;
	}

	if (hw->irq > 0) {
		err = st_asm330lhh_buffers_setup(hw);
		if (err < 0)
			return err;
	}

	for (i = 0; i < ST_ASM330LHH_ID_EVENT; i++) {
		if (!hw->iio_devs[i])
			continue;

		err = devm_iio_device_register(hw->dev, hw->iio_devs[i]);
		if (err)
			return err;
	}

#ifdef CONFIG_IIO_ST_ASM330LHH_EN_BASIC_FEATURES
	err = st_asm330lhh_probe_event(hw);
	if (err < 0)
		return err;

	err = st_asm330lhh_post_init_device(hw);
	if (err < 0)
		return err;
#endif /* CONFIG_IIO_ST_ASM330LHH_EN_BASIC_FEATURES */

#if defined(CONFIG_PM) && defined(CONFIG_IIO_ST_ASM330LHH_MAY_WAKEUP)
	err = device_init_wakeup(dev, 1);
	if (err)
		return err;
#endif /* CONFIG_PM && CONFIG_IIO_ST_ASM330LHH_MAY_WAKEUP */

	dev_info(dev, "Device probed v %s\n", ST_ASM330LHH_DRV_VERSION);

	return 0;
}
EXPORT_SYMBOL(st_asm330lhh_probe);

static int __maybe_unused st_asm330lhh_suspend(struct device *dev)
{
	struct st_asm330lhh_hw *hw = dev_get_drvdata(dev);
	struct st_asm330lhh_sensor *sensor;
	int i, err = 0;

	dev_info(dev, "Suspending device\n");

	for (i = 0; i < ST_ASM330LHH_ID_MAX; i++) {
		if (!hw->iio_devs[i])
			continue;

		sensor = iio_priv(hw->iio_devs[i]);
		if (!(hw->enable_mask & BIT(sensor->id)))
			continue;

		/* power off enabled sensors */
		err = st_asm330lhh_set_odr(sensor, 0, 0);
		if (err < 0)
			return err;
	}

	if (st_asm330lhh_is_fifo_enabled(hw)) {
		err = st_asm330lhh_suspend_fifo(hw);
		if (err < 0)
			return err;
	}

	err = st_asm330lhh_bk_regs(hw);

#ifdef CONFIG_IIO_ST_ASM330LHH_MAY_WAKEUP
	if (device_may_wakeup(dev))
		enable_irq_wake(hw->irq);
#endif /* CONFIG_IIO_ST_ASM330LHH_MAY_WAKEUP */

	return err < 0 ? err : 0;
}

static int __maybe_unused st_asm330lhh_resume(struct device *dev)
{
	struct st_asm330lhh_hw *hw = dev_get_drvdata(dev);
	struct st_asm330lhh_sensor *sensor;
	int i, err = 0;

	dev_info(dev, "Resuming device\n");

#ifdef CONFIG_IIO_ST_ASM330LHH_MAY_WAKEUP
	if (device_may_wakeup(dev))
		disable_irq_wake(hw->irq);
#endif /* CONFIG_IIO_ST_ASM330LHH_MAY_WAKEUP */

	err = st_asm330lhh_restore_regs(hw);
	if (err < 0)
		return err;

	for (i = 0; i < ST_ASM330LHH_ID_MAX; i++) {
		if (!hw->iio_devs[i])
			continue;

		sensor = iio_priv(hw->iio_devs[i]);
		if (!(hw->enable_mask & BIT(sensor->id)))
			continue;

		err = st_asm330lhh_set_odr(sensor, sensor->odr, sensor->uodr);
		if (err < 0)
			return err;
	}

	err = st_asm330lhh_reset_hwts(hw);
	if (err < 0)
		return err;

	if (st_asm330lhh_is_fifo_enabled(hw))
		err = st_asm330lhh_set_fifo_mode(hw, ST_ASM330LHH_FIFO_CONT);

	return err < 0 ? err : 0;
}

const struct dev_pm_ops st_asm330lhh_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(st_asm330lhh_suspend, st_asm330lhh_resume)
};
EXPORT_SYMBOL(st_asm330lhh_pm_ops);

MODULE_AUTHOR("Lorenzo Bianconi <lorenzo.bianconi@st.com>");
MODULE_AUTHOR("Mario Tesi <mario.tesi@st.com>");
MODULE_DESCRIPTION("STMicroelectronics st_asm330lhh driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(ST_ASM330LHH_DRV_VERSION);
