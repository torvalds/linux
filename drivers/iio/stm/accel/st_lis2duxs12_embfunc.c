// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_lis2duxs12 embedded function sensor driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2022 STMicroelectronics Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/iio/iio.h>

#include "st_lis2duxs12.h"

static int
st_lis2duxs12_ef_pg1_sensor_set_enable(struct st_lis2duxs12_sensor *sensor,
				      u8 mask, u8 irq_mask, bool enable)
{
	struct st_lis2duxs12_hw *hw = sensor->hw;
	int err;

	err = st_lis2duxs12_sensor_set_enable(sensor, enable);
	if (err < 0)
		return err;

	mutex_lock(&hw->page_lock);
	err = st_lis2duxs12_set_emb_access(hw, 1);
	if (err < 0)
		goto unlock;

	err = __st_lis2duxs12_write_with_mask(hw,
					ST_LIS2DUXS12_EMB_FUNC_EN_A_ADDR,
					mask, enable);
	if (err < 0)
		goto reset_page;

	err = __st_lis2duxs12_write_with_mask(hw, hw->emb_int_reg,
					      irq_mask, enable);

reset_page:
	st_lis2duxs12_set_emb_access(hw, 0);

	if (err < 0)
		goto unlock;

	if (((hw->enable_mask & ST_LIS2DUXS12_EMB_FUNC_ENABLED) && enable) ||
	    (!(hw->enable_mask & ST_LIS2DUXS12_EMB_FUNC_ENABLED) && !enable)) {
		err = __st_lis2duxs12_write_with_mask(hw, hw->md_int_reg,
					ST_LIS2DUXS12_INT_EMB_FUNC_MASK,
					enable);
	}

unlock:
	mutex_unlock(&hw->page_lock);

	return err;
}

/**
 * Enable Embedded Function sensor [EMB_FUN]
 *
 * @param  sensor: ST ACC sensor instance
 * @param  enable: Enable/Disable sensor
 * @return  < 0 if error, 0 otherwise
 */
int st_lis2duxs12_embfunc_sensor_set_enable(struct st_lis2duxs12_sensor *sensor,
					    bool enable)
{
	int err;

	switch (sensor->id) {
	case ST_LIS2DUXS12_ID_STEP_DETECTOR:
		err = st_lis2duxs12_ef_pg1_sensor_set_enable(sensor,
				ST_LIS2DUXS12_PEDO_EN_MASK,
				ST_LIS2DUXS12_INT_STEP_DETECTOR_MASK,
				enable);
		break;
	case ST_LIS2DUXS12_ID_SIGN_MOTION:
		err = st_lis2duxs12_ef_pg1_sensor_set_enable(sensor,
				ST_LIS2DUXS12_SIGN_MOTION_EN_MASK,
				ST_LIS2DUXS12_INT_SIG_MOT_MASK,
				enable);
		break;
	case ST_LIS2DUXS12_ID_TILT:
		err = st_lis2duxs12_ef_pg1_sensor_set_enable(sensor,
					ST_LIS2DUXS12_TILT_EN_MASK,
					ST_LIS2DUXS12_INT_TILT_MASK,
					enable);
		break;
	default:
		err = -EINVAL;
		break;
	}

	return err;
}

int
st_lis2duxs12_step_counter_set_enable(struct st_lis2duxs12_sensor *sensor,
				      bool enable)
{
	struct st_lis2duxs12_hw *hw = sensor->hw;
	bool run_enable = false;
	int err;

	mutex_lock(&hw->page_lock);
	err = st_lis2duxs12_set_emb_access(hw, 1);
	if (err < 0)
		goto unlock;

	err = __st_lis2duxs12_write_with_mask(hw,
					ST_LIS2DUXS12_EMB_FUNC_EN_A_ADDR,
					ST_LIS2DUXS12_PEDO_EN_MASK,
					enable);
	if (err < 0)
		goto reset_page;

	err = __st_lis2duxs12_write_with_mask(hw,
				ST_LIS2DUXS12_EMB_FUNC_FIFO_EN_ADDR,
				ST_LIS2DUXS12_STEP_COUNTER_FIFO_EN_MASK,
				enable);
	if (err < 0)
		goto reset_page;

	run_enable = true;

reset_page:
	st_lis2duxs12_set_emb_access(hw, 0);

unlock:
	mutex_unlock(&hw->page_lock);

	if (run_enable)
		err = st_lis2duxs12_sensor_set_enable(sensor, enable);

	return err;
}

int st_lis2duxs12_reset_step_counter(struct iio_dev *iio_dev)
{
	struct st_lis2duxs12_sensor *sensor = iio_priv(iio_dev);
	struct st_lis2duxs12_hw *hw = sensor->hw;
	__le16 data;
	int err;

	err = iio_device_claim_direct_mode(iio_dev);
	if (err)
		return err;

	err = st_lis2duxs12_step_counter_set_enable(sensor, true);
	if (err < 0)
		goto unlock_iio_dev;

	mutex_lock(&hw->page_lock);
	err = st_lis2duxs12_set_emb_access(hw, 1);
	if (err < 0)
		goto unlock_page;

	err = __st_lis2duxs12_write_with_mask(hw,
				ST_LIS2DUXS12_EMB_FUNC_SRC_ADDR,
				ST_LIS2DUXS12_PEDO_RST_STEP_MASK, 1);
	if (err < 0)
		goto reset_page;

	msleep(100);

	regmap_bulk_read(hw->regmap, ST_LIS2DUXS12_STEP_COUNTER_L_ADDR,
			 (u8 *)&data, sizeof(data));

reset_page:
	st_lis2duxs12_set_emb_access(hw, 0);

unlock_page:
	mutex_unlock(&hw->page_lock);

	err = st_lis2duxs12_step_counter_set_enable(sensor, false);

unlock_iio_dev:
	iio_device_release_direct_mode(iio_dev);

	return err;
}

int st_lis2duxs12_embedded_function_init(struct st_lis2duxs12_hw *hw)
{
	int err;

	err  = st_lis2duxs12_update_bits_locked(hw,
					ST_LIS2DUXS12_CTRL4_ADDR,
					ST_LIS2DUXS12_EMB_FUNC_EN_MASK,
					1);
	if (err < 0)
		return err;

	usleep_range(5000, 6000);

	/* enable latched interrupts */
	err = st_lis2duxs12_update_page_bits_locked(hw,
					ST_LIS2DUXS12_PAGE_RW_ADDR,
					ST_LIS2DUXS12_EMB_FUNC_LIR_MASK,
					1);

	return err;
}
