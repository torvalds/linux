// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_ism330dhcx embedded function sensor driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2019 STMicroelectronics Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/iio/iio.h>

#include "st_ism330dhcx.h"

#define ST_ISM330DHCX_REG_PAGE_SEL_ADDR			0x02
#define ST_ISM330DHCX_REG_PAGE_SEL_RST_MASK		BIT(0)

#define ST_ISM330DHCX_REG_EMB_FUNC_EN_A_ADDR		0x04
#define ST_ISM330DHCX_REG_PAGE_ADDRESS			0x08
#define ST_ISM330DHCX_REG_PAGE_VALUE			0x09
#define ST_ISM330DHCX_FSM_BASE_ADDRESS			0x2da

#define ST_ISM330DHCX_REG_PEDO_EN_MASK			BIT(3)
#define ST_ISM330DHCX_REG_TILT_EN_MASK			BIT(4)
#define ST_ISM330DHCX_REG_SIGN_MOTION_EN_MASK		BIT(5)

#define ST_ISM330DHCX_REG_INT_DTAP_MASK			BIT(3)
#define ST_ISM330DHCX_REG_INT_STAP_MASK			BIT(6)

#define ST_ISM330DHCX_REG_EMB_FUNC_EN_B_ADDR		0x05
#define ST_ISM330DHCX_REG_FSM_EN_MASK			BIT(0)
#define ST_ISM330DHCX_REG_INT_STEP_DET_MASK		BIT(3)
#define ST_ISM330DHCX_REG_INT_TILT_MASK			BIT(4)
#define ST_ISM330DHCX_REG_INT_SIGMOT_MASK			BIT(5)

#define ST_ISM330DHCX_PAGE_RW_ADDR				0x17
#define ST_ISM330DHCX_REG_WR_MASK				GENMASK(6, 5)
#define ST_ISM330DHCX_REG_EMB_FUNC_LIR_MASK		BIT(7)

#define ST_ISM330DHCX_REG_EMB_FUNC_FIFO_CFG_ADDR		0x44
#define ST_ISM330DHCX_REG_PEDO_FIFO_EN_MASK		BIT(6)

#define ST_ISM330DHCX_REG_FSM_ENABLE_A_ADDR		0x46
#define ST_ISM330DHCX_REG_FSM_OUTS6_ADDR			0x51
#define ST_ISM330DHCX_REG_ORIENTATION_0_MASK		BIT(5)
#define ST_ISM330DHCX_REG_ORIENTATION_90_MASK		BIT(7)
#define ST_ISM330DHCX_REG_ORIENTATION_180_MASK		BIT(4)
#define ST_ISM330DHCX_REG_ORIENTATION_270_MASK		BIT(6)

/* Finite State Machine ODR configuration */
#define ST_ISM330DHCX_REG_EMB_FUNC_ODR_CFG_B_ADDR		0x5f
#define ST_ISM330DHCX_REG_FSM_ODR_MASK			GENMASK(5, 3)
#define ST_ISM330DHCX_FSM_ODR_12_5				0
#define ST_ISM330DHCX_FSM_ODR_26				1
#define ST_ISM330DHCX_FSM_ODR_52				2
#define ST_ISM330DHCX_FSM_ODR_104				3
#define ST_ISM330DHCX_FSM_ODR_208				4
#define ST_ISM330DHCX_FSM_ODR_416				5

#define ST_ISM330DHCX_REG_STEP_COUNTER_L_ADDR		0x62
#define ST_ISM330DHCX_REG_EMB_FUNC_SRC_ADDR		0x64
#define ST_ISM330DHCX_REG_PEDO_RST_STEP_MASK		BIT(7)

#define ST_ISM330DHCX_FSM_MAX_SIZE				255

/**
 * @struct st_ism330dhcx_fsm_sensor
 * @brief Single FSM description entry
 *
 * Implements #595543 Feature
 *
 * The following FSM state machine ISM330DHCX features listed in EX_FUN_FSM_SENSOR:
 *
 * SENSOR_TYPE_GLANCE_GESTURE
 * SENSOR_TYPE_MOTION_DETECT
 * SENSOR_TYPE_STATIONARY_DETECT
 * SENSOR_TYPE_WAKE_GESTURE
 * SENSOR_TYPE_PICK_UP_GESTURE
 * SENSOR_TYPE_WRIST_TILT_GESTURE
 *
 * will be managed as event sensors
 *
 * data: FSM binary data block.
 * id: Sensor Identifier.
 * FSM binary data block len.
 */
struct st_ism330dhcx_fsm_sensor {
	u8 data[ST_ISM330DHCX_FSM_MAX_SIZE];
	enum st_ism330dhcx_sensor_id id;
	u16 len;
};

static const struct st_ism330dhcx_fsm_sensor st_ism330dhcx_fsm_sensor_list[] = {
	/* glance */
	{
		.id = ST_ISM330DHCX_ID_GLANCE,
		.data = {
			0xb2, 0x10, 0x24, 0x20, 0x17, 0x17, 0x66, 0x32,
			0x66, 0x3c, 0x20, 0x20, 0x02, 0x02, 0x08, 0x08,
			0x00, 0x04, 0x0c, 0x00, 0xc7, 0x66, 0x33, 0x73,
			0x77, 0x64, 0x88, 0x75, 0x99, 0x66, 0x33, 0x53,
			0x44, 0xf5, 0x22, 0x00,
		},
		.len = 36,
	},
	/* motion */
	{
		.id = ST_ISM330DHCX_ID_MOTION,
		.data = {
			0x51, 0x10, 0x16, 0x00, 0x00, 0x00, 0x66, 0x3c,
			0x02, 0x00, 0x00, 0x7d, 0x00, 0xc7, 0x05, 0x99,
			0x33, 0x53, 0x44, 0xf5, 0x22, 0x00,
		},
		.len = 22,
	},
	/* no motion */
	{
		.id = ST_ISM330DHCX_ID_NO_MOTION,
		.data = {
			0x51, 0x00, 0x10, 0x00, 0x00, 0x00, 0x66, 0x3c,
			0x02, 0x00, 0x00, 0x7d, 0xff, 0x53, 0x99, 0x50,
		},
		.len = 16,
	},
	/* wakeup */
	{
		.id = ST_ISM330DHCX_ID_WAKEUP,
		.data = {
			0xe2, 0x00, 0x1e, 0x20, 0x13, 0x15, 0x66, 0x3e,
			0x66, 0xbe, 0xcd, 0x3c, 0xc0, 0xc0, 0x02, 0x02,
			0x0b, 0x10, 0x05, 0x66, 0xcc, 0x35, 0x38, 0x35,
			0x77, 0xdd, 0x03, 0x54, 0x22, 0x00,
		},
		.len = 30,
	},
	/* pickup */
	{
		.id = ST_ISM330DHCX_ID_PICKUP,
		.data = {
			0x51, 0x00, 0x10, 0x00, 0x00, 0x00, 0x33, 0x3c,
			0x02, 0x00, 0x00, 0x05, 0x05, 0x99, 0x30, 0x00,
		},
		.len = 16,
	},
	/* orientation */
	{
		.id = ST_ISM330DHCX_ID_ORIENTATION,
		.data = {
			0x91, 0x10, 0x16, 0x00, 0x00, 0x00, 0x66, 0x3a,
			0x66, 0x32, 0xf0, 0x00, 0x00, 0x0d, 0x00, 0xc7,
			0x05, 0x73, 0x99, 0x08, 0xf5, 0x22,
		},
		.len = 22,
	},
	/* wrist tilt */
	{
		.id = ST_ISM330DHCX_ID_WRIST_TILT,
		.data = {
			0x52, 0x00, 0x14, 0x00, 0x00, 0x00, 0xae, 0xb7,
			0x80, 0x00, 0x00, 0x06, 0x0f, 0x05, 0x73, 0x33,
			0x07, 0x54, 0x44, 0x22,
		},
		.len = 20,
	},
};

struct st_ism330dhcx_fsm_fs {
	u32 gain;
	__le16 val;
};

static const struct st_ism330dhcx_fsm_fs st_ism330dhcx_fsm_fs_table[] = {
	{  ST_ISM330DHCX_ACC_FS_2G_GAIN, 0x03ff },
	{  ST_ISM330DHCX_ACC_FS_4G_GAIN, 0x07fe },
	{  ST_ISM330DHCX_ACC_FS_8G_GAIN, 0x0bfe },
	{ ST_ISM330DHCX_ACC_FS_16G_GAIN, 0x0ffe },
};

static inline
int st_ism330dhcx_fsm_set_access(struct st_ism330dhcx_hw *hw, bool enable)
{
	u8 val = enable ? 2 : 0;

	return __st_ism330dhcx_write_with_mask(hw,
					    ST_ISM330DHCX_PAGE_RW_ADDR,
					    ST_ISM330DHCX_REG_WR_MASK,
					    val);
}

static int st_ism330dhcx_fsm_write(struct st_ism330dhcx_hw *hw, u16 base_addr,
				int len, const u8 *data)
{
	u8 msb, lsb;
	int i, err;

	msb = (((base_addr >> 8) & 0xf) << 4) | 1;
	lsb = base_addr & 0xff;

	err = hw->tf->write(hw->dev,
			    ST_ISM330DHCX_REG_PAGE_ADDRESS,
			    sizeof(lsb),
			    &lsb);
	if (err < 0)
		return err;

	err = hw->tf->write(hw->dev,
			    ST_ISM330DHCX_REG_PAGE_SEL_ADDR,
			    sizeof(msb),
			    &msb);
	if (err < 0)
		return err;

	for (i = 0; i < len; i++) {
		err = hw->tf->write(hw->dev,
				    ST_ISM330DHCX_REG_PAGE_VALUE,
				    sizeof(u8),
				    &data[i]);
		if (err < 0)
			return err;

		if (++lsb == 0) {
			msb += (1 << 4);
			err = hw->tf->write(hw->dev,
					    ST_ISM330DHCX_REG_PAGE_SEL_ADDR,
					    sizeof(msb),
					    &msb);
			if (err < 0)
				return err;
		}
	}

	return err;
}

static int st_ism330dhcx_ef_pg1_sensor_set_enable(struct st_ism330dhcx_sensor *sensor,
					       u8 mask, u8 irq_mask,
					       bool enable)
{
	struct st_ism330dhcx_hw *hw = sensor->hw;
	int err;

	err = st_ism330dhcx_sensor_set_enable(sensor, enable);
	if (err < 0)
		return err;

	mutex_lock(&hw->page_lock);
	err = st_ism330dhcx_set_page_access(hw, ST_ISM330DHCX_REG_FUNC_CFG_MASK,
					 true);
	if (err < 0)
		goto unlock;

	err = __st_ism330dhcx_write_with_mask(hw,
					   ST_ISM330DHCX_REG_EMB_FUNC_EN_A_ADDR,
					   mask, enable);
	if (err < 0)
		goto reset_page;

	err = __st_ism330dhcx_write_with_mask(hw, hw->embfunc_irq_reg, irq_mask,
					   enable);
reset_page:
	st_ism330dhcx_set_page_access(hw, ST_ISM330DHCX_REG_FUNC_CFG_MASK, false);
unlock:
	mutex_unlock(&hw->page_lock);

	return err;
}

/**
 * FSM Function sensor [FSM_FUN]
 *
 * @param  sensor: ST IMU sensor instance
 * @param  enable: Enable/Disable sensor
 * @return  < 0 if error, 0 otherwise
 */
static int st_ism330dhcx_fsm_set_enable(struct st_ism330dhcx_sensor *sensor,
				     bool enable)
{
	struct st_ism330dhcx_hw *hw = sensor->hw;
	u16 enable_mask = hw->fsm_enable_mask;
	int err, i;

	for (i = 0; i < ARRAY_SIZE(st_ism330dhcx_fsm_sensor_list); i++)
		if (st_ism330dhcx_fsm_sensor_list[i].id == sensor->id)
			break;

	if (i == ARRAY_SIZE(st_ism330dhcx_fsm_sensor_list))
		return -EINVAL;

	err = st_ism330dhcx_sensor_set_enable(sensor, enable);
	if (err < 0)
		return err;

	mutex_lock(&hw->page_lock);
	err = st_ism330dhcx_set_page_access(hw, ST_ISM330DHCX_REG_FUNC_CFG_MASK,
					 true);
	if (err < 0)
		goto unlock;

	if (enable)
		enable_mask |= BIT(i);
	else
		enable_mask &= ~BIT(i);

	err = hw->tf->write(hw->dev, ST_ISM330DHCX_REG_FSM_ENABLE_A_ADDR,
			    sizeof(enable_mask), (u8 *)&enable_mask);
	if (err < 0)
		goto reset_page;

	hw->fsm_enable_mask = enable_mask;

reset_page:
	st_ism330dhcx_set_page_access(hw, ST_ISM330DHCX_REG_FUNC_CFG_MASK, false);
unlock:
	mutex_unlock(&hw->page_lock);

	return err;
}

/**
 * Enable Embedded Function sensor [EMB_FUN]
 *
 * @param  sensor: ST IMU sensor instance
 * @param  enable: Enable/Disable sensor
 * @return  < 0 if error, 0 otherwise
 */
int st_ism330dhcx_embfunc_sensor_set_enable(struct st_ism330dhcx_sensor *sensor,
					 bool enable)
{
	int err;

	switch (sensor->id) {
	case ST_ISM330DHCX_ID_STEP_DETECTOR:
		err = st_ism330dhcx_ef_pg1_sensor_set_enable(sensor,
					ST_ISM330DHCX_REG_PEDO_EN_MASK,
					ST_ISM330DHCX_REG_INT_STEP_DET_MASK,
					enable);
		break;
	case ST_ISM330DHCX_ID_SIGN_MOTION:
		err = st_ism330dhcx_ef_pg1_sensor_set_enable(sensor,
					ST_ISM330DHCX_REG_SIGN_MOTION_EN_MASK,
					ST_ISM330DHCX_REG_INT_SIGMOT_MASK,
					enable);
		break;
	case ST_ISM330DHCX_ID_TILT:
		err = st_ism330dhcx_ef_pg1_sensor_set_enable(sensor,
						ST_ISM330DHCX_REG_TILT_EN_MASK,
						ST_ISM330DHCX_REG_TILT_EN_MASK,
						enable);
		break;
	case ST_ISM330DHCX_ID_NO_MOTION:
	case ST_ISM330DHCX_ID_MOTION:
	case ST_ISM330DHCX_ID_WAKEUP:
	case ST_ISM330DHCX_ID_PICKUP:
	case ST_ISM330DHCX_ID_ORIENTATION:
	case ST_ISM330DHCX_ID_WRIST_TILT:
	case ST_ISM330DHCX_ID_GLANCE:
		err = st_ism330dhcx_fsm_set_enable(sensor, enable);
		break;
	default:
		err = -EINVAL;
		break;
	}

	return err;
}

/**
 * Enable Step Counter Sensor [EMB_FUN]
 *
 * @param  sensor: ST IMU sensor instance
 * @param  enable: Enable/Disable sensor
 * @return  < 0 if error, 0 otherwise
 */
int st_ism330dhcx_step_counter_set_enable(struct st_ism330dhcx_sensor *sensor,
				       bool enable)
{
	struct st_ism330dhcx_hw *hw = sensor->hw;
	int err;

	err = st_ism330dhcx_sensor_set_enable(sensor, enable);
	if (err < 0)
		return err;

	mutex_lock(&hw->page_lock);
	err = st_ism330dhcx_set_page_access(hw, ST_ISM330DHCX_REG_FUNC_CFG_MASK,
					 true);
	if (err < 0)
		goto unlock;

	err = __st_ism330dhcx_write_with_mask(hw,
					   ST_ISM330DHCX_REG_EMB_FUNC_EN_A_ADDR,
					   ST_ISM330DHCX_REG_PEDO_EN_MASK,
					   enable);
	if (err < 0)
		goto reset_page;

	err = __st_ism330dhcx_write_with_mask(hw,
					ST_ISM330DHCX_REG_EMB_FUNC_FIFO_CFG_ADDR,
					ST_ISM330DHCX_REG_PEDO_FIFO_EN_MASK,
					enable);

reset_page:
	st_ism330dhcx_set_page_access(hw, ST_ISM330DHCX_REG_FUNC_CFG_MASK, false);
unlock:
	mutex_unlock(&hw->page_lock);

	return err;
}

/**
 * Reset Step Counter value [EMB_FUN]
 *
 * @param  iio_dev: IIO device
 * @return  < 0 if error, 0 otherwise
 */
int st_ism330dhcx_reset_step_counter(struct iio_dev *iio_dev)
{
	struct st_ism330dhcx_sensor *sensor = iio_priv(iio_dev);
	struct st_ism330dhcx_hw *hw = sensor->hw;
	u16 prev_val, val = 0;
	__le16 data;
	int err;

	mutex_lock(&iio_dev->mlock);
	if (iio_buffer_enabled(iio_dev)) {
		err = -EBUSY;
		goto unlock_iio_dev;
	}

	err = st_ism330dhcx_step_counter_set_enable(sensor, true);
	if (err < 0)
		goto unlock_iio_dev;

	mutex_lock(&hw->page_lock);
	err = st_ism330dhcx_set_page_access(hw, ST_ISM330DHCX_REG_FUNC_CFG_MASK,
					 true);
	if (err < 0)
		goto unlock_page;

	do {
		prev_val = val;
		err = __st_ism330dhcx_write_with_mask(hw,
					ST_ISM330DHCX_REG_EMB_FUNC_SRC_ADDR,
					ST_ISM330DHCX_REG_PEDO_RST_STEP_MASK, 1);
		if (err < 0)
			goto reset_page;

		msleep(100);

		err = hw->tf->read(hw->dev,
				   ST_ISM330DHCX_REG_STEP_COUNTER_L_ADDR,
				   sizeof(data), (u8 *)&data);
		if (err < 0)
			goto reset_page;

		val = le16_to_cpu(data);
	} while (val && val >= prev_val);

reset_page:
	st_ism330dhcx_set_page_access(hw, ST_ISM330DHCX_REG_FUNC_CFG_MASK, false);
unlock_page:
	mutex_unlock(&hw->page_lock);

	err = st_ism330dhcx_step_counter_set_enable(sensor, false);
unlock_iio_dev:
	mutex_unlock(&iio_dev->mlock);

	return err;
}

/**
 * Read Orientation data sensor [EMB_FUN]
 *
 * @param  hw: ST IMU MEMS hw instance.
 * @param  out: Out data buffer.
 * @return  < 0 if error, 0 otherwise
 */
int st_ism330dhcx_fsm_get_orientation(struct st_ism330dhcx_hw *hw, u8 *out)
{
	int err;
	u8 data;

	mutex_lock(&hw->page_lock);
	err = st_ism330dhcx_set_page_access(hw, ST_ISM330DHCX_REG_FUNC_CFG_MASK,
					 true);
	if (err < 0)
		goto unlock;

	err = hw->tf->read(hw->dev, ST_ISM330DHCX_REG_FSM_OUTS6_ADDR,
			   sizeof(data), &data);
	if (err < 0)
		goto reset_page;

	switch (data) {
	case ST_ISM330DHCX_REG_ORIENTATION_0_MASK:
		*out = 0;
		break;
	case ST_ISM330DHCX_REG_ORIENTATION_90_MASK:
		*out = 1;
		break;
	case ST_ISM330DHCX_REG_ORIENTATION_180_MASK:
		*out = 2;
		break;
	case ST_ISM330DHCX_REG_ORIENTATION_270_MASK:
		*out = 3;
		break;
	default:
		err = -EINVAL;
		break;
	}

reset_page:
	st_ism330dhcx_set_page_access(hw, ST_ISM330DHCX_REG_FUNC_CFG_MASK, false);
unlock:
	mutex_unlock(&hw->page_lock);

	return err;
}


/**
 * Initialize Finite State Machine HW block [FSM_FUN]
 *
 * @param  hw: ST IMU MEMS hw instance
 * @return  < 0 if error, 0 otherwise
 */
int st_ism330dhcx_fsm_init(struct st_ism330dhcx_hw *hw)
{
	u8 nfsm[] = {
		ARRAY_SIZE(st_ism330dhcx_fsm_sensor_list),
		ARRAY_SIZE(st_ism330dhcx_fsm_sensor_list)
	};
	__le16 irq_mask, fsm_addr = ST_ISM330DHCX_FSM_BASE_ADDRESS;
	u8 val[2] = {};
	int i, err;

	mutex_lock(&hw->page_lock);
	err = st_ism330dhcx_set_page_access(hw, ST_ISM330DHCX_REG_FUNC_CFG_MASK,
					 true);
	if (err < 0)
		goto unlock;

	/* enable gesture rec */
	err = __st_ism330dhcx_write_with_mask(hw,
					   ST_ISM330DHCX_REG_EMB_FUNC_EN_B_ADDR,
					   ST_ISM330DHCX_REG_FSM_EN_MASK,
					   1);
	if (err < 0)
		goto reset_page;

	/* gest rec ODR 52Hz */
	err = __st_ism330dhcx_write_with_mask(hw,
					ST_ISM330DHCX_REG_EMB_FUNC_ODR_CFG_B_ADDR,
					ST_ISM330DHCX_REG_FSM_ODR_MASK,
					ST_ISM330DHCX_FSM_ODR_52);
	if (err < 0)
		goto reset_page;

	/* disable all fsm sensors */
	err = hw->tf->write(hw->dev, ST_ISM330DHCX_REG_FSM_ENABLE_A_ADDR,
			    sizeof(val), val);
	if (err < 0)
		goto reset_page;

	/* enable fsm interrupt */
	irq_mask = (1 << ARRAY_SIZE(st_ism330dhcx_fsm_sensor_list)) - 1;
	err = hw->tf->write(hw->dev, hw->embfunc_irq_reg + 1,
			    sizeof(irq_mask),
			    (u8 *)&irq_mask);
	if (err < 0)
		goto reset_page;

	/* enable latched interrupts */
	err  = __st_ism330dhcx_write_with_mask(hw,
					    ST_ISM330DHCX_PAGE_RW_ADDR,
					    ST_ISM330DHCX_REG_EMB_FUNC_LIR_MASK,
					    1);
	if (err < 0)
		goto reset_page;

	/* enable access */
	err = st_ism330dhcx_fsm_set_access(hw, true);
	if (err < 0)
		return err;

	/* # of configured fsm */
	err = st_ism330dhcx_fsm_write(hw, 0x17c, sizeof(nfsm), nfsm);
	if (err < 0)
		goto reset_access;

	err = st_ism330dhcx_fsm_write(hw, 0x17e, sizeof(fsm_addr), (u8 *)
				   &fsm_addr);
	if (err < 0)
		goto reset_access;

	/* configure fsm */
	for (i = 0; i < ARRAY_SIZE(st_ism330dhcx_fsm_sensor_list); i++) {
		err = st_ism330dhcx_fsm_write(hw, fsm_addr,
					st_ism330dhcx_fsm_sensor_list[i].len,
					st_ism330dhcx_fsm_sensor_list[i].data);
		if (err < 0)
			goto reset_access;

		fsm_addr += st_ism330dhcx_fsm_sensor_list[i].len;
	}

reset_access:
	st_ism330dhcx_fsm_set_access(hw, false);

	__st_ism330dhcx_write_with_mask(hw,
				     ST_ISM330DHCX_REG_PAGE_SEL_ADDR,
				     ST_ISM330DHCX_REG_PAGE_SEL_RST_MASK, 1);
reset_page:
	st_ism330dhcx_set_page_access(hw, ST_ISM330DHCX_REG_FUNC_CFG_MASK, false);
unlock:
	mutex_unlock(&hw->page_lock);

	return err;
}
