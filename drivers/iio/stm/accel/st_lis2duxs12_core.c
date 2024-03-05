// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_lis2duxs12 sensor driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2022 STMicroelectronics Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/buffer.h>
#include <linux/iio/events.h>
#include <linux/pm.h>
#include <linux/interrupt.h>
#include <linux/version.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <linux/platform_data/st_sensors_pdata.h>

#include "st_lis2duxs12.h"

static const struct st_lis2duxs12_std_entry {
	u16 odr;
	u8 val;
} st_lis2duxs12_std_table[] = {
	{   1,  1 },
	{   3,  1 },
	{   6,  2 },
	{  12,  3 },
	{  25,  3 },
	{  50,  3 },
	{ 100,  3 },
	{ 200,  3 },
	{ 400,  3 },
	{ 800,  3 },
};

static struct st_lis2duxs12_selftest_table {
	char *string_mode;
	u8 mode;
} st_lis2duxs12_selftest_table[] = {
	[0] = {
		.string_mode = "disabled",
		.mode = 0,
	},
	[1] = {
		.string_mode = "positive-sign",
		.mode = 1,
	},
	[2] = {
		.string_mode = "negative-sign",
		.mode = 2,
	},
};

static struct st_lis2duxs12_power_mode_table {
	char *string_mode;
	enum st_lis2duxs12_pm_t val;
} st_lis2duxs12_power_mode[] = {
	[ST_LIS2DUXS12_LP_MODE] = {
		.string_mode = "LP_MODE",
		.val = ST_LIS2DUXS12_LP_MODE,
	},
	[ST_LIS2DUXS12_HP_MODE] = {
		.string_mode = "HP_MODE",
		.val = ST_LIS2DUXS12_HP_MODE,
	},
};

static const struct
st_lis2duxs12_odr_table_entry st_lis2duxs12_odr_table[] = {
	[ST_LIS2DUXS12_ID_ACC] = {
		.size = 10,
		.reg = {
			.addr = ST_LIS2DUXS12_CTRL5_ADDR,
			.mask = ST_LIS2DUXS12_ODR_MASK,
		},
		.pm = {
			.addr = ST_LIS2DUXS12_CTRL3_ADDR,
			.mask = ST_LIS2DUXS12_HP_EN_MASK,
		},
		.odr_avl[0] = {   1, 600000,  0x01 },
		.odr_avl[1] = {   3,      0,  0x02 },
		.odr_avl[2] = {   6,      0,  0x04 },
		.odr_avl[3] = {  12, 500000,  0x05 },
		.odr_avl[4] = {  25,      0,  0x06 },
		.odr_avl[5] = {  50,      0,  0x07 },
		.odr_avl[6] = { 100,      0,  0x08 },
		.odr_avl[7] = { 200,      0,  0x09 },
		.odr_avl[8] = { 400,      0,  0x0a },
		.odr_avl[9] = { 800,      0,  0x0b },
	},
	[ST_LIS2DUXS12_ID_TEMP] = {
		.size = 10,
		.reg = {
			.addr = ST_LIS2DUXS12_CTRL5_ADDR,
			.mask = ST_LIS2DUXS12_ODR_MASK,
		},
		.odr_avl[0] = {   1, 600000,  0x01 },
		.odr_avl[1] = {   3,      0,  0x02 },
		.odr_avl[2] = {   6,      0,  0x04 },
		.odr_avl[3] = {  12, 500000,  0x05 },
		.odr_avl[4] = {  25,      0,  0x06 },
		.odr_avl[5] = {  50,      0,  0x07 },
		.odr_avl[6] = { 100,      0,  0x08 },
		.odr_avl[7] = { 200,      0,  0x09 },
		.odr_avl[8] = { 400,      0,  0x0a },
		.odr_avl[9] = { 800,      0,  0x0b },
	},
};

static const struct
st_lis2duxs12_fs_table_entry st_lis2duxs12_fs_table[] = {
	[ST_LIS2DUXS12_ID_ACC] = {
		.size = 4,
		.reg = {
			.addr = ST_LIS2DUXS12_CTRL5_ADDR,
			.mask = ST_LIS2DUXS12_FS_MASK,
		},
		.fs_avl[0] = {
			.gain = IIO_G_TO_M_S_2(61),
			.val = 0x0,
		},
		.fs_avl[1] = {
			.gain = IIO_G_TO_M_S_2(122),
			.val = 0x1,
		},
		.fs_avl[2] = {
			.gain = IIO_G_TO_M_S_2(244),
			.val = 0x2,
		},
		.fs_avl[3] = {
			.gain = IIO_G_TO_M_S_2(488),
			.val = 0x3,
		},
	},
	[ST_LIS2DUXS12_ID_TEMP] = {
		.size = 1,
		.fs_avl[0] = {
			.gain = (1000000 / ST_LIS2DUXS12_TEMP_GAIN),
			.val = 0x0
		},
	},
};

/**
 * List of supported device settings
 *
 * The following table list all device features in terms of supported
 * features.
 */
static const struct st_lis2duxs12_settings st_lis2duxs12_sensor_settings[] = {
	{
		.id = {
			.hw_id = ST_LIS2DUX12_ID,
			.name = ST_LIS2DUX12_DEV_NAME,
		},
	},
	{
		.id = {
			.hw_id = ST_LIS2DUXS12_ID,
			.name = ST_LIS2DUXS12_DEV_NAME,
		},
		.st_qvar_support = true,
	},
};

static const struct iio_mount_matrix *
st_lis2duxs12_get_mount_matrix(const struct iio_dev *iio_dev,
			       const struct iio_chan_spec *ch)
{
	struct st_lis2duxs12_sensor *sensor = iio_priv(iio_dev);
	struct st_lis2duxs12_hw *hw = sensor->hw;

	return &hw->orientation;
}

static const struct
iio_chan_spec_ext_info st_lis2duxs12_chan_spec_ext_info[] = {
	IIO_MOUNT_MATRIX(IIO_SHARED_BY_TYPE,
			 st_lis2duxs12_get_mount_matrix),
	{}
};

static const struct iio_chan_spec st_lis2duxs12_acc_channels[] = {
	ST_LIS2DUXS12_DATA_CHANNEL(IIO_ACCEL,
				   ST_LIS2DUXS12_OUT_X_L_ADDR,
				   1, IIO_MOD_X, 0, 16, 16, 's',
				   st_lis2duxs12_chan_spec_ext_info),
	ST_LIS2DUXS12_DATA_CHANNEL(IIO_ACCEL,
				   ST_LIS2DUXS12_OUT_Y_L_ADDR,
				   1, IIO_MOD_Y, 1, 16, 16, 's',
				   st_lis2duxs12_chan_spec_ext_info),
	ST_LIS2DUXS12_DATA_CHANNEL(IIO_ACCEL,
				   ST_LIS2DUXS12_OUT_Z_L_ADDR,
				   1, IIO_MOD_Z, 2, 16, 16, 's',
				   st_lis2duxs12_chan_spec_ext_info),
	ST_LIS2DUXS12_EVENT_CHANNEL(IIO_ACCEL, flush),
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

static const struct iio_chan_spec st_lis2duxs12_temp_channels[] = {
	{
		.type = IIO_TEMP,
		.address = ST_LIS2DUXS12_OUT_T_AH_QVAR_L_ADDR,
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
	ST_LIS2DUXS12_EVENT_CHANNEL(IIO_TEMP, flush),
	IIO_CHAN_SOFT_TIMESTAMP(1),
};

static const struct
iio_chan_spec st_lis2duxs12_step_counter_channels[] = {
	{
		.type = STM_IIO_STEP_COUNTER,
		.scan_index = 0,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_LE,
		},
	},
	ST_LIS2DUXS12_EVENT_CHANNEL(STM_IIO_STEP_COUNTER, flush),
	IIO_CHAN_SOFT_TIMESTAMP(1),
};

static const struct
iio_chan_spec st_lis2duxs12_step_detector_channels[] = {
	ST_LIS2DUXS12_EVENT_CHANNEL(IIO_STEPS, thr),
};

static const struct
iio_chan_spec st_lis2duxs12_sign_motion_channels[] = {
	ST_LIS2DUXS12_EVENT_CHANNEL(STM_IIO_SIGN_MOTION, thr),
};

static const struct iio_chan_spec st_lis2duxs12_tilt_channels[] = {
	ST_LIS2DUXS12_EVENT_CHANNEL(STM_IIO_TILT, thr),
};

static const unsigned long st_lis2duxs12_available_scan_masks[] = {
	GENMASK(2, 0), 0x0
};

static const unsigned long st_lis2duxs12_temp_available_scan_masks[] = {
	BIT(0), 0x0
};

static const unsigned long st_lis2duxs12_emb_available_scan_masks[] = {
	BIT(0), 0x0
};

static inline int
st_lis2duxs12_set_std_level(struct st_lis2duxs12_hw *hw, u16 odr)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(st_lis2duxs12_std_table); i++) {
		if (st_lis2duxs12_std_table[i].odr >= odr)
			break;
	}

	if (i == ARRAY_SIZE(st_lis2duxs12_std_table))
		return -EINVAL;

	hw->std_level = st_lis2duxs12_std_table[i].val;

	return 0;
}

static __maybe_unused int
st_lis2duxs12_reg_access(struct iio_dev *iio_dev, unsigned int reg,
			 unsigned int writeval, unsigned int *readval)
{
	struct st_lis2duxs12_sensor *sensor = iio_priv(iio_dev);
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

static int st_lis2duxs12_power_up_command(struct st_lis2duxs12_hw *hw)
{
	int data;

	regmap_read(hw->regmap, ST_LIS2DUXS12_WHOAMI_ADDR, &data);

	usleep_range(25000, 26000);

	return data;
}

static int st_lis2duxs12_check_whoami(struct st_lis2duxs12_hw *hw,
				      enum st_lis2duxs12_hw_id hw_id)
{
	int data, err, i;

	for (i = 0; i < ARRAY_SIZE(st_lis2duxs12_sensor_settings); i++) {
		if (st_lis2duxs12_sensor_settings[i].id.name &&
		    st_lis2duxs12_sensor_settings[i].id.hw_id == hw_id)
			break;
	}

	if (i == ARRAY_SIZE(st_lis2duxs12_sensor_settings)) {
		dev_err(hw->dev, "unsupported hw id [%02x]\n", hw_id);

		return -ENODEV;
	}

	err = st_lis2duxs12_set_emb_access(hw, false);
	if (err < 0)
		return err;

	err = regmap_read(hw->regmap, ST_LIS2DUXS12_WHOAMI_ADDR, &data);
	if (err < 0) {
		dev_err(hw->dev, "failed to read whoami register\n");
		return err;
	}

	if (data != ST_LIS2DUXS12_WHOAMI_VAL) {
		dev_err(hw->dev, "unsupported whoami [%02x]\n", data);
		return -ENODEV;
	}

	hw->settings = &st_lis2duxs12_sensor_settings[i];

	return 0;
}

static int
st_lis2duxs12_set_full_scale(struct st_lis2duxs12_sensor *sensor,
			     u32 gain)
{
	enum st_lis2duxs12_sensor_id id = sensor->id;
	struct st_lis2duxs12_hw *hw = sensor->hw;
	int i, err;
	u8 val;

	for (i = 0; i < st_lis2duxs12_fs_table[id].size; i++)
		if (st_lis2duxs12_fs_table[id].fs_avl[i].gain == gain)
			break;

	if (i == st_lis2duxs12_fs_table[id].size)
		return -EINVAL;

	val = st_lis2duxs12_fs_table[id].fs_avl[i].val;
	err = regmap_update_bits(hw->regmap,
			st_lis2duxs12_fs_table[id].reg.addr,
			st_lis2duxs12_fs_table[id].reg.mask,
			ST_LIS2DUXS12_SHIFT_VAL(val,
			 st_lis2duxs12_fs_table[id].reg.mask));
	if (err < 0)
		return err;

	sensor->gain = gain;

	return 0;
}

static int st_lis2duxs12_get_odr_val(enum st_lis2duxs12_sensor_id id,
				     int odr, int uodr,
				     struct st_lis2duxs12_odr *oe)
{
	int req_odr = ST_LIS2DUXS12_ODR_EXPAND(odr, uodr);
	int sensor_odr;
	int i;

	for (i = 0; i < st_lis2duxs12_odr_table[id].size; i++) {
		sensor_odr = ST_LIS2DUXS12_ODR_EXPAND(
				st_lis2duxs12_odr_table[id].odr_avl[i].hz,
				st_lis2duxs12_odr_table[id].odr_avl[i].uhz);
		if (sensor_odr >= req_odr) {
			oe->hz = st_lis2duxs12_odr_table[id].odr_avl[i].hz;
			oe->uhz = st_lis2duxs12_odr_table[id].odr_avl[i].uhz;
			oe->val = st_lis2duxs12_odr_table[id].odr_avl[i].val;

			return 0;
		}
	}

	return -EINVAL;
}

static u16
st_lis2duxs12_check_odr_dependency(struct st_lis2duxs12_hw *hw,
				   int odr, int uodr,
				   enum st_lis2duxs12_sensor_id ref_id)
{
	struct st_lis2duxs12_sensor *ref = iio_priv(hw->iio_devs[ref_id]);
	bool enable = odr > 0;
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

static int st_lis2duxs12_set_odr(struct st_lis2duxs12_sensor *sensor,
				 int req_odr, int req_uodr)
{
	enum st_lis2duxs12_sensor_id id = ST_LIS2DUXS12_ID_ACC;
	struct st_lis2duxs12_hw *hw = sensor->hw;
	struct st_lis2duxs12_odr oe = { 0 };
	enum st_lis2duxs12_sensor_id i;
	int err, odr;

	for (i = ST_LIS2DUXS12_ID_ACC; i < ST_LIS2DUXS12_ID_MAX; i++) {
		if (!hw->iio_devs[i] || i == sensor->id)
			continue;

		odr = st_lis2duxs12_check_odr_dependency(hw, req_odr,
							req_uodr, i);
		if (odr != req_odr)
			return 0;
	}

	switch (sensor->id) {
	case ST_LIS2DUXS12_ID_FSM_0:
	case ST_LIS2DUXS12_ID_FSM_1:
	case ST_LIS2DUXS12_ID_FSM_2:
	case ST_LIS2DUXS12_ID_FSM_3:
	case ST_LIS2DUXS12_ID_FSM_4:
	case ST_LIS2DUXS12_ID_FSM_5:
	case ST_LIS2DUXS12_ID_FSM_6:
	case ST_LIS2DUXS12_ID_FSM_7:
	case ST_LIS2DUXS12_ID_MLC_0:
	case ST_LIS2DUXS12_ID_MLC_1:
	case ST_LIS2DUXS12_ID_MLC_2:
	case ST_LIS2DUXS12_ID_MLC_3:
		if ((hw->settings->st_qvar_support) &&
		    (hw->mlc_config->requested_device &
		     BIT(ST_LIS2DUXS12_ID_QVAR)) &&
		    !(hw->enable_mask & BIT(ST_LIS2DUXS12_ID_QVAR))) {
			err = st_lis2duxs12_write_with_mask_locked(hw,
					 ST_LIS2DUXS12_AH_QVAR_CFG_ADDR,
					 ST_LIS2DUXS12_AH_QVAR_EN_MASK,
					 req_odr > 0 ? 1 : 0);
			if (err < 0)
				return err;
		}
		break;
	default:
		break;
	}

	if (ST_LIS2DUXS12_ODR_EXPAND(req_odr, req_uodr) > 0) {
		err = st_lis2duxs12_get_odr_val(id, req_odr, req_uodr,
						&oe);
		if (err)
			return err;

		/* check if sensor supports power mode setting */
		if (sensor->pm != ST_LIS2DUXS12_NO_MODE) {
			err = st_lis2duxs12_update_bits_locked(hw,
				    st_lis2duxs12_odr_table[id].pm.addr,
				    st_lis2duxs12_odr_table[id].pm.mask,
				    sensor->pm);
			if (err < 0)
				return err;
		}
	}

	return st_lis2duxs12_update_bits_locked(hw,
				   st_lis2duxs12_odr_table[id].reg.addr,
				   st_lis2duxs12_odr_table[id].reg.mask,
				   oe.val);
}

int st_lis2duxs12_sensor_set_enable(struct st_lis2duxs12_sensor *sensor,
				    bool enable)
{
	int uodr = enable ? sensor->uodr : 0;
	int odr = enable ? sensor->odr : 0;
	int err;

	err = st_lis2duxs12_set_odr(sensor, odr, uodr);
	if (err < 0)
		return err;

	if (enable)
		sensor->hw->enable_mask |= BIT(sensor->id);
	else
		sensor->hw->enable_mask &= ~BIT(sensor->id);

	return 0;
}

static int
st_lis2duxs12_read_oneshot(struct st_lis2duxs12_sensor *sensor,
			   u8 addr, int *val)
{
	struct st_lis2duxs12_hw *hw = sensor->hw;
	int err, delay;
	__le16 data;

	if (sensor->id == ST_LIS2DUXS12_ID_TEMP) {
		u8 status;

		err = st_lis2duxs12_read_locked(hw,
					ST_LIS2DUXS12_STATUS_ADDR,
					&status, sizeof(status));
		if (err < 0)
			return err;

		if (status & ST_LIS2DUXS12_DRDY_MASK) {
			err = st_lis2duxs12_read_locked(hw, addr,
							&data,
							sizeof(data));
			if (err < 0)
				return err;

			sensor->old_data = data;
		} else {
			data = sensor->old_data;
		}
	} else {
		err = st_lis2duxs12_sensor_set_enable(sensor, true);
		if (err < 0)
			return err;

		/*
		 * use big delay for data valid because of drdy mask
		 * enabled uodr is neglected in this operation
		 */
		delay = 10000000 / sensor->odr;
		usleep_range(delay, 2 * delay);

		err = st_lis2duxs12_read_locked(hw, addr, &data,
						sizeof(data));

		st_lis2duxs12_sensor_set_enable(sensor, false);
		if (err < 0)
			return err;
	}

	*val = (s16)le16_to_cpu(data);

	return IIO_VAL_INT;
}

static int st_lis2duxs12_read_raw(struct iio_dev *iio_dev,
				  struct iio_chan_spec const *ch,
				  int *val, int *val2, long mask)
{
	struct st_lis2duxs12_sensor *sensor = iio_priv(iio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(iio_dev);
		if (ret)
			return ret;

		ret = st_lis2duxs12_read_oneshot(sensor, ch->address,
						 val);
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
			*val = 1;
			*val2 = ST_LIS2DUXS12_TEMP_GAIN;
			ret = IIO_VAL_FRACTIONAL;
			break;
		case IIO_ACCEL:
			*val = 0;
			*val2 = sensor->gain;
			ret = IIO_VAL_INT_PLUS_MICRO;
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

static int st_lis2duxs12_write_raw(struct iio_dev *iio_dev,
				   struct iio_chan_spec const *chan,
				   int val, int val2, long mask)
{
	struct st_lis2duxs12_sensor *sensor = iio_priv(iio_dev);
	int err;

	mutex_lock(&iio_dev->mlock);

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		err = st_lis2duxs12_set_full_scale(sensor, val2);
		break;
	case IIO_CHAN_INFO_SAMP_FREQ: {
		struct st_lis2duxs12_odr oe = { 0 };

		err = st_lis2duxs12_get_odr_val(sensor->id,
						val, val2, &oe);
		if (!err) {
			if (sensor->hw->enable_mask & BIT(sensor->id)) {
				switch (sensor->id) {
				case ST_LIS2DUXS12_ID_ACC: {
					err = st_lis2duxs12_set_odr(sensor,
								   oe.hz,
								   oe.uhz);
					if (err < 0)
						break;

					st_lis2duxs12_set_std_level(sensor->hw,
								 oe.hz);
					}
					break;
				default:
					break;
				}
			}

			sensor->odr = oe.hz;
			sensor->uodr = oe.uhz;
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

static int
st_lis2duxs12_read_event_config(struct iio_dev *iio_dev,
				const struct iio_chan_spec *chan,
				enum iio_event_type type,
				enum iio_event_direction dir)
{
	struct st_lis2duxs12_sensor *sensor = iio_priv(iio_dev);
	struct st_lis2duxs12_hw *hw = sensor->hw;

	return !!(hw->enable_mask & BIT(sensor->id));
}

static int
st_lis2duxs12_write_event_config(struct iio_dev *iio_dev,
				 const struct iio_chan_spec *chan,
				 enum iio_event_type type,
				 enum iio_event_direction dir,
				 int state)
{
	struct st_lis2duxs12_sensor *sensor = iio_priv(iio_dev);
	int err;

	mutex_lock(&iio_dev->mlock);
	err = st_lis2duxs12_embfunc_sensor_set_enable(sensor, state);
	mutex_unlock(&iio_dev->mlock);

	return err;
}

static ssize_t
st_lis2duxs12_sysfs_sampling_frequency_avail(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct st_lis2duxs12_sensor *sensor = iio_priv(dev_to_iio_dev(dev));
	enum st_lis2duxs12_sensor_id id = sensor->id;
	int i, len = 0;

	for (i = 0; i < st_lis2duxs12_odr_table[id].size; i++) {
		len += scnprintf(buf + len, PAGE_SIZE - len, "%d.%06d ",
			    st_lis2duxs12_odr_table[id].odr_avl[i].hz,
			    st_lis2duxs12_odr_table[id].odr_avl[i].uhz);
	}

	buf[len - 1] = '\n';

	return len;
}

static ssize_t
st_lis2duxs12_sysfs_scale_avail(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct st_lis2duxs12_sensor *sensor = iio_priv(dev_to_iio_dev(dev));
	enum st_lis2duxs12_sensor_id id = sensor->id;
	int i, len = 0;

	for (i = 0; i < st_lis2duxs12_fs_table[id].size; i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "0.%06u ",
			     st_lis2duxs12_fs_table[id].fs_avl[i].gain);
	buf[len - 1] = '\n';

	return len;
}

static ssize_t
st_lis2duxs12_sysfs_get_power_mode_avail(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	int i, len = 0;

	for (i = 0; i < ARRAY_SIZE(st_lis2duxs12_power_mode); i++) {
		len += scnprintf(buf + len, PAGE_SIZE - len, "%s ",
			       st_lis2duxs12_power_mode[i].string_mode);
	}

	buf[len - 1] = '\n';

	return len;
}

ssize_t st_lis2duxs12_get_power_mode(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_lis2duxs12_sensor *sensor = iio_priv(iio_dev);

	return sprintf(buf, "%s\n",
		       st_lis2duxs12_power_mode[sensor->pm].string_mode);
}

ssize_t st_lis2duxs12_set_power_mode(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_lis2duxs12_sensor *sensor = iio_priv(iio_dev);
	int err, i;

	for (i = 0; i < ARRAY_SIZE(st_lis2duxs12_power_mode); i++) {
		if (strncmp(buf, st_lis2duxs12_power_mode[i].string_mode,
		    strlen(st_lis2duxs12_power_mode[i].string_mode)) == 0)
			break;
	}

	if (i == ARRAY_SIZE(st_lis2duxs12_power_mode))
		return -EINVAL;

	err = iio_device_claim_direct_mode(iio_dev);
	if (err)
		return err;

	/* update power mode */
	sensor->pm = st_lis2duxs12_power_mode[i].val;

	iio_device_release_direct_mode(iio_dev);

	return size;
}

int st_lis2duxs12_get_int_reg(struct st_lis2duxs12_hw *hw)
{
	int err, ft_int_pin, md_int_pin, emb_int_pin;
	struct device_node *np = hw->dev->of_node;

	if (!np)
		return -EINVAL;

	err = of_property_read_u32(np, "st,int-pin", &ft_int_pin);
	if (err < 0) {
		struct st_sensors_platform_data *pdata;
		struct device *dev = hw->dev;

		pdata = (struct st_sensors_platform_data *)dev->platform_data;
		ft_int_pin = pdata ? pdata->drdy_int_pin : 1;
	}

	err = of_property_read_u32(np, "st,md-int-pin", &md_int_pin);
	if (err < 0)
		md_int_pin = ft_int_pin;

	err = of_property_read_u32(np, "st,emb-int-pin", &emb_int_pin);
	if (err < 0)
		emb_int_pin = ft_int_pin;

	switch (ft_int_pin) {
	case 1:
		hw->ft_int_reg = ST_LIS2DUXS12_CTRL2_ADDR;
		break;
	case 2:
		hw->ft_int_reg = ST_LIS2DUXS12_CTRL3_ADDR;
		break;
	default:
		dev_err(hw->dev, "unsupported interrupt pin\n");
		err = -EINVAL;
		break;
	}

	switch (md_int_pin) {
	case 1:
		hw->md_int_reg = ST_LIS2DUXS12_MD1_CFG_ADDR;
		break;
	case 2:
		hw->md_int_reg = ST_LIS2DUXS12_MD2_CFG_ADDR;
		break;
	default:
		dev_err(hw->dev, "unsupported interrupt pin\n");
		err = -EINVAL;
		break;
	}

	switch (emb_int_pin) {
	case 1:
		hw->emb_int_reg = ST_LIS2DUXS12_EMB_FUNC_INT1_ADDR;
		break;
	case 2:
		hw->emb_int_reg = ST_LIS2DUXS12_EMB_FUNC_INT2_ADDR;
		break;
	default:
		dev_err(hw->dev, "unsupported interrupt pin\n");
		err = -EINVAL;
		break;
	}

	hw->int_pin = ft_int_pin;

	return 0;
}

static ssize_t
st_lis2duxs12_sysfs_get_selftest_available(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	return sprintf(buf, "%s, %s\n",
		       st_lis2duxs12_selftest_table[1].string_mode,
		       st_lis2duxs12_selftest_table[2].string_mode);
}

static ssize_t
st_lis2duxs12_sysfs_get_selftest_status(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	int8_t result;
	char *message;
	struct st_lis2duxs12_sensor *sensor = iio_priv(dev_to_iio_dev(dev));
	enum st_lis2duxs12_sensor_id id = sensor->id;

	if (id != ST_LIS2DUXS12_ID_ACC)
		return -EINVAL;

	result = sensor->selftest_status;
	if (result == 0)
		message = "na";
	else if (result < 0)
		message = "fail";
	else
		message = "pass";

	return sprintf(buf, "%s\n", message);
}

static int
st_lis2duxs12_selftest_sensor(struct st_lis2duxs12_sensor *sensor,
			      int mode)
{
	enum st_lis2duxs12_pm_t pm;
	int xyz[2][3];
	u8 raw_data[6];
	int i, ret;
	int uodr;
	int odr;

	pm = sensor->pm;
	sensor->pm = ST_LIS2DUXS12_HP_MODE;

	ret = st_lis2duxs12_sensor_set_enable(sensor, false);
	if (ret < 0)
		return ret;

	/* wait 25 ms for stable output */
	msleep(25);

	odr = sensor->odr;
	uodr = sensor->uodr;
	if (mode == 1) {
		ret = st_lis2duxs12_update_bits_locked(sensor->hw,
					       ST_LIS2DUXS12_CTRL3_ADDR,
					       GENMASK(1, 0), 0x03);
		if (ret < 0)
			goto selftest_stop;

		ret = st_lis2duxs12_update_bits_locked(sensor->hw,
					 ST_LIS2DUXS12_WAKE_UP_DUR_ADDR,
					 BIT(4), 1);
		if (ret < 0)
			goto selftest_stop;
	} else {
		ret = st_lis2duxs12_update_bits_locked(sensor->hw,
					       ST_LIS2DUXS12_CTRL3_ADDR,
					       GENMASK(1, 0), 0x00);
		if (ret < 0)
			goto selftest_stop;

		ret = st_lis2duxs12_update_bits_locked(sensor->hw,
					 ST_LIS2DUXS12_WAKE_UP_DUR_ADDR,
					 BIT(4), 0);
		if (ret < 0)
			goto selftest_stop;
	}

	ret = st_lis2duxs12_update_bits_locked(sensor->hw,
					   ST_LIS2DUXS12_SELF_TEST_ADDR,
					   ST_LIS2DUXS12_ST_MASK, 0x02);
	if (ret < 0)
		goto selftest_stop;

	sensor->odr = 200;
	sensor->uodr = 0;

	ret = st_lis2duxs12_sensor_set_enable(sensor, true);
	if (ret < 0)
		goto selftest_stop;

	/* wait 30 ms for stable output */
	msleep(30);

	ret = st_lis2duxs12_read_locked(sensor->hw,
					ST_LIS2DUXS12_OUT_X_L_ADDR,
					raw_data, sizeof(raw_data));
	if (ret < 0)
		goto selftest_stop;

	for (i = 0; i < 3; i++)
		xyz[0][i] = ((s16)*(u16 *)&raw_data[2 * i]);

	ret = st_lis2duxs12_update_bits_locked(sensor->hw,
					   ST_LIS2DUXS12_SELF_TEST_ADDR,
					   ST_LIS2DUXS12_ST_MASK, 0x01);
	if (ret < 0)
		goto selftest_stop;

	/* wait 30 ms for stable output */
	msleep(30);

	ret = st_lis2duxs12_read_locked(sensor->hw,
					ST_LIS2DUXS12_OUT_X_L_ADDR,
					raw_data, sizeof(raw_data));
	if (ret < 0)
		goto selftest_stop;

	for (i = 0; i < 3; i++) {
		xyz[1][i] = ((s16)*(u16 *)&raw_data[2 * i]);
		if ((abs(xyz[1][i] - xyz[0][i]) < sensor->min_st) ||
		    (abs(xyz[1][i] - xyz[0][i]) > sensor->max_st)) {
			sensor->selftest_status = -1;
			goto selftest_stop;
		}
	}

	sensor->selftest_status = 1;

selftest_stop:
	/* restore sensor configuration */
	st_lis2duxs12_update_bits_locked(sensor->hw,
					 ST_LIS2DUXS12_SELF_TEST_ADDR,
					 ST_LIS2DUXS12_ST_MASK, 0x00);
	st_lis2duxs12_update_bits_locked(sensor->hw,
					 ST_LIS2DUXS12_CTRL3_ADDR,
					 GENMASK(1, 0), 0x00);
	st_lis2duxs12_update_bits_locked(sensor->hw,
					 ST_LIS2DUXS12_WAKE_UP_DUR_ADDR,
					 BIT(4), 0);
	sensor->pm = pm;
	sensor->odr = odr;
	sensor->uodr = uodr;

	return st_lis2duxs12_sensor_set_enable(sensor, false);
}

static ssize_t
st_lis2duxs12_sysfs_start_selftest(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_lis2duxs12_sensor *sensor = iio_priv(iio_dev);
	struct st_lis2duxs12_hw *hw = sensor->hw;
	int ret, mode;
	u32 gain;

	if (sensor->id != ST_LIS2DUXS12_ID_ACC)
		return -EINVAL;

	ret = iio_device_claim_direct_mode(iio_dev);
	if (ret)
		return ret;

	/* self test mode unavailable if some sensors enabled */
	if (hw->enable_mask &
	    GENMASK(ST_LIS2DUXS12_ID_MAX, ST_LIS2DUXS12_ID_ACC)) {
		ret = -EBUSY;

		goto out_claim;
	}

	for (mode = 0; mode < ARRAY_SIZE(st_lis2duxs12_selftest_table);
	     mode++) {
		if (strncmp(buf, st_lis2duxs12_selftest_table[mode].string_mode,
			strlen(st_lis2duxs12_selftest_table[mode].string_mode)) == 0)
			break;
	}

	if (mode == ARRAY_SIZE(st_lis2duxs12_selftest_table))
		return -EINVAL;

	/* set BDU = 1, FS = 8g, BW = ODR/16, ODR = 200 Hz */
	gain = sensor->gain;
	st_lis2duxs12_set_full_scale(sensor, IIO_G_TO_M_S_2(244));
	st_lis2duxs12_selftest_sensor(sensor, mode);

	/* restore full scale after test */
	st_lis2duxs12_set_full_scale(sensor, gain);

out_claim:
	iio_device_release_direct_mode(iio_dev);

	return size;
}

static ssize_t
st_lis2duxs12_sysfs_reset_step_counter(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	int err;

	err = st_lis2duxs12_reset_step_counter(iio_dev);

	return err < 0 ? err : size;
}

static IIO_DEV_ATTR_SAMP_FREQ_AVAIL(st_lis2duxs12_sysfs_sampling_frequency_avail);
static IIO_DEVICE_ATTR(in_accel_scale_available, 0444,
		       st_lis2duxs12_sysfs_scale_avail, NULL, 0);
static IIO_DEVICE_ATTR(in_temp_scale_available, 0444,
		       st_lis2duxs12_sysfs_scale_avail, NULL, 0);

static IIO_DEVICE_ATTR(hwfifo_watermark_max, 0444,
		       st_lis2duxs12_get_max_watermark, NULL, 0);
static IIO_DEVICE_ATTR(hwfifo_flush, 0200, NULL, st_lis2duxs12_flush_fifo, 0);
static IIO_DEVICE_ATTR(hwfifo_watermark, 0644, st_lis2duxs12_get_watermark,
		       st_lis2duxs12_set_watermark, 0);

static IIO_DEVICE_ATTR(power_mode_available, 0444,
		       st_lis2duxs12_sysfs_get_power_mode_avail, NULL, 0);
static IIO_DEVICE_ATTR(power_mode, 0644,
		       st_lis2duxs12_get_power_mode,
		       st_lis2duxs12_set_power_mode, 0);

static IIO_DEVICE_ATTR(selftest_available, 0444,
		       st_lis2duxs12_sysfs_get_selftest_available,
		       NULL, 0);
static IIO_DEVICE_ATTR(selftest, 0644,
		       st_lis2duxs12_sysfs_get_selftest_status,
		       st_lis2duxs12_sysfs_start_selftest, 0);
static IIO_DEVICE_ATTR(reset_counter, 0200, NULL,
		       st_lis2duxs12_sysfs_reset_step_counter, 0);

static struct attribute *st_lis2duxs12_acc_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_accel_scale_available.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark_max.dev_attr.attr,
	&iio_dev_attr_power_mode_available.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark.dev_attr.attr,
	&iio_dev_attr_hwfifo_flush.dev_attr.attr,
	&iio_dev_attr_power_mode.dev_attr.attr,
	&iio_dev_attr_selftest_available.dev_attr.attr,
	&iio_dev_attr_selftest.dev_attr.attr,
	NULL,
};

static const struct
attribute_group st_lis2duxs12_acc_attribute_group = {
	.attrs = st_lis2duxs12_acc_attributes,
};

static const struct iio_info st_lis2duxs12_acc_info = {
	.attrs = &st_lis2duxs12_acc_attribute_group,
	.read_raw = st_lis2duxs12_read_raw,
	.write_raw = st_lis2duxs12_write_raw,
#ifdef CONFIG_DEBUG_FS
	.debugfs_reg_access = &st_lis2duxs12_reg_access,
#endif /* CONFIG_DEBUG_FS */
};

static struct attribute *st_lis2duxs12_temp_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_temp_scale_available.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark_max.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark.dev_attr.attr,
	&iio_dev_attr_hwfifo_flush.dev_attr.attr,
	NULL,
};

static const struct
attribute_group st_lis2duxs12_temp_attribute_group = {
	.attrs = st_lis2duxs12_temp_attributes,
};

static const struct iio_info st_lis2duxs12_temp_info = {
	.attrs = &st_lis2duxs12_temp_attribute_group,
	.read_raw = st_lis2duxs12_read_raw,
	.write_raw = st_lis2duxs12_write_raw,
};

static struct attribute *st_lis2duxs12_sc_attributes[] = {
	&iio_dev_attr_hwfifo_watermark_max.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark.dev_attr.attr,
	&iio_dev_attr_reset_counter.dev_attr.attr,
	&iio_dev_attr_hwfifo_flush.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lis2duxs12_sc_attribute_group = {
	.attrs = st_lis2duxs12_sc_attributes,
};

static const struct iio_info st_lis2duxs12_sc_info = {
	.attrs = &st_lis2duxs12_sc_attribute_group,
};

static struct attribute *st_lis2duxs12_sd_attributes[] = {
	NULL,
};

static const struct attribute_group st_lis2duxs12_sd_attribute_group = {
	.attrs = st_lis2duxs12_sd_attributes,
};

static const struct iio_info st_lis2duxs12_sd_info = {
	.attrs = &st_lis2duxs12_sd_attribute_group,
	.read_event_config = st_lis2duxs12_read_event_config,
	.write_event_config = st_lis2duxs12_write_event_config,
};

static struct attribute *st_lis2duxs12_sm_attributes[] = {
	NULL,
};

static const struct attribute_group st_lis2duxs12_sm_attribute_group = {
	.attrs = st_lis2duxs12_sm_attributes,
};

static const struct iio_info st_lis2duxs12_sm_info = {
	.attrs = &st_lis2duxs12_sm_attribute_group,
	.read_event_config = st_lis2duxs12_read_event_config,
	.write_event_config = st_lis2duxs12_write_event_config,
};

static struct attribute *st_lis2duxs12_tilt_attributes[] = {
	NULL,
};

static const struct
attribute_group st_lis2duxs12_tilt_attribute_group = {
	.attrs = st_lis2duxs12_tilt_attributes,
};

static const struct iio_info st_lis2duxs12_tilt_info = {
	.attrs = &st_lis2duxs12_tilt_attribute_group,
	.read_event_config = st_lis2duxs12_read_event_config,
	.write_event_config = st_lis2duxs12_write_event_config,
};

/*
 * st_lis2duxs12_reset_device - sw reset
 */
static int st_lis2duxs12_reset_device(struct st_lis2duxs12_hw *hw)
{
	int ret;

	ret = regmap_update_bits(hw->regmap,
				 ST_LIS2DUXS12_CTRL1_ADDR,
				 ST_LIS2DUXS12_SW_RESET_MASK,
				 FIELD_PREP(ST_LIS2DUXS12_SW_RESET_MASK, 1));

	if (ret < 0)
		return ret;

	/* wait ~50 us */
	usleep_range(50, 51);

	ret = regmap_update_bits(hw->regmap,
				 ST_LIS2DUXS12_CTRL4_ADDR,
				 ST_LIS2DUXS12_BOOT_MASK,
				 FIELD_PREP(ST_LIS2DUXS12_BOOT_MASK, 1));

	/* wait ~20 ms */
	usleep_range(20000, 20100);

	return ret;
}

/*
 * st_lis2duxs12_init_timestamp_engine - Init timestamp engine
 */
static int
st_lis2duxs12_init_timestamp_engine(struct st_lis2duxs12_hw *hw,
				   bool enable)
{
	int err;

	err = regmap_update_bits(hw->regmap,
				 ST_LIS2DUXS12_INTERRUPT_CFG_ADDR,
				 ST_LIS2DUXS12_TIMESTAMP_EN_MASK,
				 FIELD_PREP(ST_LIS2DUXS12_TIMESTAMP_EN_MASK,
					    enable));
	if (err < 0)
		return err;

	hw->timestamp = enable;

	return err;
}

static int st_lis2duxs12_init_device(struct st_lis2duxs12_hw *hw)
{
	int err;

	/* latch interrupts */
	err = regmap_update_bits(hw->regmap,
				 ST_LIS2DUXS12_INTERRUPT_CFG_ADDR,
				 ST_LIS2DUXS12_LIR_MASK,
				 FIELD_PREP(ST_LIS2DUXS12_LIR_MASK, 1));
	if (err < 0)
		return err;

	/* enable Block Data Update */
	err = regmap_update_bits(hw->regmap,
				 ST_LIS2DUXS12_CTRL4_ADDR,
				 ST_LIS2DUXS12_BDU_MASK,
				 FIELD_PREP(ST_LIS2DUXS12_BDU_MASK, 1));
	if (err < 0)
		return err;

	err = st_lis2duxs12_get_int_reg(hw);
	if (err < 0)
		return err;

	err = st_lis2duxs12_init_timestamp_engine(hw, true);
	if (err < 0)
		return err;


	/* enable fifo configuration */
	err = regmap_update_bits(hw->regmap,
				 ST_LIS2DUXS12_CTRL4_ADDR,
				 ST_LIS2DUXS12_FIFO_EN_MASK,
				 FIELD_PREP(ST_LIS2DUXS12_FIFO_EN_MASK, 1));
	if (err < 0)
		return err;

	/* enable XL and Temp */
	err = regmap_update_bits(hw->regmap,
				 ST_LIS2DUXS12_FIFO_WTM_ADDR,
				 ST_LIS2DUXS12_XL_ONLY_FIFO_MASK,
				 FIELD_PREP(ST_LIS2DUXS12_XL_ONLY_FIFO_MASK, 1));
	if (err < 0)
		return err;

	hw->xl_only = true;

	/* enable FIFO watermak interrupt */
	return regmap_update_bits(hw->regmap, hw->ft_int_reg,
				  ST_LIS2DUXS12_INT_FIFO_TH_MASK,
				  FIELD_PREP(ST_LIS2DUXS12_INT_FIFO_TH_MASK, 1));
}

static struct
iio_dev *st_lis2duxs12_alloc_iiodev(struct st_lis2duxs12_hw *hw,
				    enum st_lis2duxs12_sensor_id id)
{
	struct st_lis2duxs12_sensor *sensor;
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

	switch (id) {
	case ST_LIS2DUXS12_ID_ACC:
		iio_dev->channels = st_lis2duxs12_acc_channels;
		iio_dev->num_channels =
				ARRAY_SIZE(st_lis2duxs12_acc_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			 "%s_accel", hw->settings->id.name);
		iio_dev->info = &st_lis2duxs12_acc_info;
		iio_dev->available_scan_masks =
				st_lis2duxs12_available_scan_masks;
		sensor->max_watermark = ST_LIS2DUXS12_MAX_FIFO_DEPTH;
		sensor->offset = 0;
		sensor->pm = ST_LIS2DUXS12_HP_MODE;
		sensor->odr = st_lis2duxs12_odr_table[id].odr_avl[3].hz;
		sensor->uodr =
			st_lis2duxs12_odr_table[id].odr_avl[3].uhz;
		sensor->min_st = ST_LIS2DUXS12_SELFTEST_ACCEL_MIN;
		sensor->max_st = ST_LIS2DUXS12_SELFTEST_ACCEL_MAX;

		/* set default FS to each sensor */
		sensor->gain = st_lis2duxs12_fs_table[id].fs_avl[0].gain;
		break;
	case ST_LIS2DUXS12_ID_TEMP:
		iio_dev->channels = st_lis2duxs12_temp_channels;
		iio_dev->num_channels =
				ARRAY_SIZE(st_lis2duxs12_temp_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			 "%s_temp", hw->settings->id.name);
		iio_dev->info = &st_lis2duxs12_temp_info;
		iio_dev->available_scan_masks =
				st_lis2duxs12_temp_available_scan_masks;
		sensor->max_watermark = ST_LIS2DUXS12_MAX_FIFO_DEPTH;
		sensor->offset = ST_LIS2DUXS12_TEMP_OFFSET;
		sensor->pm = ST_LIS2DUXS12_NO_MODE;
		sensor->odr = st_lis2duxs12_odr_table[id].odr_avl[3].hz;
		sensor->uodr =
			st_lis2duxs12_odr_table[id].odr_avl[3].uhz;

		/* set default FS to each sensor */
		sensor->gain = st_lis2duxs12_fs_table[id].fs_avl[0].gain;
		break;
	case ST_LIS2DUXS12_ID_STEP_COUNTER:
		iio_dev->channels = st_lis2duxs12_step_counter_channels;
		iio_dev->num_channels =
			ARRAY_SIZE(st_lis2duxs12_step_counter_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			 "%s_step_c", hw->settings->id.name);
		iio_dev->info = &st_lis2duxs12_sc_info;
		iio_dev->available_scan_masks =
				st_lis2duxs12_emb_available_scan_masks;

		/* request ODR @50 Hz to works properly */
		sensor->max_watermark = 1;
		sensor->odr = 50;
		sensor->uodr = 0;
		break;
	case ST_LIS2DUXS12_ID_STEP_DETECTOR:
		iio_dev->channels = st_lis2duxs12_step_detector_channels;
		iio_dev->num_channels =
			ARRAY_SIZE(st_lis2duxs12_step_detector_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			 "%s_step_d", hw->settings->id.name);
		iio_dev->info = &st_lis2duxs12_sd_info;
		iio_dev->available_scan_masks =
				st_lis2duxs12_emb_available_scan_masks;

		/* request ODR @50 Hz to works properly */
		sensor->odr = 50;
		sensor->uodr = 0;
		break;
	case ST_LIS2DUXS12_ID_SIGN_MOTION:
		iio_dev->channels = st_lis2duxs12_sign_motion_channels;
		iio_dev->num_channels =
			ARRAY_SIZE(st_lis2duxs12_sign_motion_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			 "%s_sign_motion", hw->settings->id.name);
		iio_dev->info = &st_lis2duxs12_sm_info;
		iio_dev->available_scan_masks =
				st_lis2duxs12_emb_available_scan_masks;

		/* request ODR @50 Hz to works properly */
		sensor->odr = 50;
		sensor->uodr = 0;
		break;
	case ST_LIS2DUXS12_ID_TILT:
		iio_dev->channels = st_lis2duxs12_tilt_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_lis2duxs12_tilt_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			 "%s_tilt", hw->settings->id.name);
		iio_dev->info = &st_lis2duxs12_tilt_info;
		iio_dev->available_scan_masks =
				st_lis2duxs12_emb_available_scan_masks;

		/* request ODR @50 Hz to works properly */
		sensor->odr = 50;
		sensor->uodr = 0;
		break;
	default:
		return NULL;
	}

	iio_dev->name = sensor->name;

	return iio_dev;
}

static void st_lis2duxs12_disable_regulator_action(void *_data)
{
	struct st_lis2duxs12_hw *hw = _data;

	regulator_disable(hw->vddio_supply);
	regulator_disable(hw->vdd_supply);
}

static int st_lis2duxs12_enable_regulator(struct st_lis2duxs12_hw *hw)
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
			    st_lis2duxs12_disable_regulator_action, hw);
	if (err) {
		dev_err(hw->dev,
			"Failed to setup regulator cleanup action %d\n", err);
		return err;
	}

	return err;
}

int st_lis2duxs12_probe(struct device *dev, int irq,
			enum st_lis2duxs12_hw_id hw_id, struct regmap *regmap)
{
	enum st_lis2duxs12_sensor_id id;
	struct st_lis2duxs12_hw *hw;
	int err;

	hw = devm_kzalloc(dev, sizeof(*hw), GFP_KERNEL);
	if (!hw)
		return -ENOMEM;

	dev_set_drvdata(dev, (void *)hw);

	mutex_init(&hw->fifo_lock);
	mutex_init(&hw->page_lock);

	hw->regmap = regmap;
	hw->dev = dev;
	hw->irq = irq;
	hw->odr_table_entry = st_lis2duxs12_odr_table;
	hw->fs_table_entry = st_lis2duxs12_fs_table;

	err = st_lis2duxs12_enable_regulator(hw);
	if (err < 0)
		return err;

	st_lis2duxs12_power_up_command(hw);

	err = st_lis2duxs12_check_whoami(hw, hw_id);
	if (err < 0)
		return err;

	err = st_lis2duxs12_reset_device(hw);
	if (err < 0)
		return err;

	err = st_lis2duxs12_init_device(hw);
	if (err < 0)
		return err;

#if KERNEL_VERSION(5, 15, 0) <= LINUX_VERSION_CODE
	err = iio_read_mount_matrix(hw->dev, &hw->orientation);
#elif KERNEL_VERSION(5, 2, 0) <= LINUX_VERSION_CODE
	err = iio_read_mount_matrix(hw->dev, "mount-matrix", &hw->orientation);
#else /* LINUX_VERSION_CODE */
	err = of_iio_read_mount_matrix(hw->dev, "mount-matrix", &hw->orientation);
#endif /* LINUX_VERSION_CODE */

	if (err) {
		dev_err(dev, "Failed to retrieve mounting matrix %d\n", err);

		return err;
	}

	for (id = ST_LIS2DUXS12_ID_ACC;
	     id <= ST_LIS2DUXS12_ID_TILT; id++) {
		hw->iio_devs[id] = st_lis2duxs12_alloc_iiodev(hw, id);
		if (!hw->iio_devs[id])
			return -ENOMEM;
	}

	if (hw->settings->st_qvar_support) {
		err = st_lis2duxs12_qvar_probe(hw);
		if (err)
			return err;
	}

	err = st_lis2duxs12_probe_basicfunc(hw);
	if (err < 0)
		return err;

	err = st_lis2duxs12_mlc_probe(hw);
	if (err < 0)
		return err;

	err = st_lis2duxs12_embedded_function_init(hw);
	if (err)
		return err;

	if (hw->irq > 0) {
		err = st_lis2duxs12_buffers_setup(hw);
		if (err < 0)
			return err;
	}

	for (id = 0; id < ST_LIS2DUXS12_ID_MAX; id++) {
		if (!hw->iio_devs[id])
			continue;

		err = devm_iio_device_register(hw->dev,
					       hw->iio_devs[id]);
		if (err)
			return err;
	}

	err = st_lis2duxs12_mlc_init_preload(hw);
	if (err)
		return err;

#if defined(CONFIG_PM)
	device_init_wakeup(dev, 1);
#endif /* CONFIG_PM && CONFIG */

	return 0;
}
EXPORT_SYMBOL(st_lis2duxs12_probe);

int st_lis2duxs12_remove(struct device *dev)
{
	st_lis2duxs12_mlc_remove(dev);

	return 0;
}
EXPORT_SYMBOL(st_lis2duxs12_remove);

static int __maybe_unused st_lis2duxs12_suspend(struct device *dev)
{
	struct st_lis2duxs12_hw *hw = dev_get_drvdata(dev);
	struct st_lis2duxs12_sensor *sensor;
	int i, err = 0;

	for (i = 0; i < ST_LIS2DUXS12_ID_MAX; i++) {
		sensor = iio_priv(hw->iio_devs[i]);
		if (!hw->iio_devs[i])
			continue;

		if (!(hw->enable_mask & BIT(sensor->id)))
			continue;

		/* do not disable sensors if requested by wake-up */
		if (!((hw->enable_mask & BIT(sensor->id)) &
		      ST_LIS2DUXS12_WAKE_UP_SENSORS)) {
			err = st_lis2duxs12_set_odr(sensor, 0, 0);
			if (err < 0)
				return err;
		} else {
			err = st_lis2duxs12_set_odr(sensor,
				    ST_LIS2DUXS12_MIN_ODR_IN_WAKEUP, 0);
			if (err < 0)
				return err;
		}
	}

	if (st_lis2duxs12_is_fifo_enabled(hw)) {
		err = st_lis2duxs12_suspend_fifo(hw);
		if (err < 0)
			return err;
	}

	if (hw->enable_mask & ST_LIS2DUXS12_WAKE_UP_SENSORS) {
		if (device_may_wakeup(dev))
			enable_irq_wake(hw->irq);
	}

	dev_info(dev, "Suspending device\n");

	return err < 0 ? err : 0;
}

static int __maybe_unused st_lis2duxs12_resume(struct device *dev)
{
	struct st_lis2duxs12_hw *hw = dev_get_drvdata(dev);
	struct st_lis2duxs12_sensor *sensor;
	int i, err = 0;

	dev_info(dev, "Resuming device\n");

	if (hw->enable_mask & ST_LIS2DUXS12_WAKE_UP_SENSORS) {
		if (device_may_wakeup(dev))
			disable_irq_wake(hw->irq);
	}

	for (i = 0; i < ST_LIS2DUXS12_ID_MAX; i++) {
		sensor = iio_priv(hw->iio_devs[i]);
		if (!hw->iio_devs[i])
			continue;

		if (!(hw->enable_mask & BIT(sensor->id)))
			continue;

		err = st_lis2duxs12_set_odr(sensor, sensor->odr,
					   sensor->uodr);
		if (err < 0)
			return err;
	}

	if (st_lis2duxs12_is_fifo_enabled(hw))
		err = st_lis2duxs12_set_fifo_mode(hw, ST_LIS2DUXS12_FIFO_CONT);

	return err < 0 ? err : 0;
}

const struct dev_pm_ops st_lis2duxs12_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(st_lis2duxs12_suspend, st_lis2duxs12_resume)
};
EXPORT_SYMBOL(st_lis2duxs12_pm_ops);

MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_DESCRIPTION("STMicroelectronics st_lis2duxs12 driver");
MODULE_LICENSE("GPL v2");
