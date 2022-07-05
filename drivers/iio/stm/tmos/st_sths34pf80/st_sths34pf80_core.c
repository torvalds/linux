// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_sths34pf80 sensor driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2022 STMicroelectronics Inc.
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/iio/buffer.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_data/st_sensors_pdata.h>
#include <linux/pm.h>
#include <linux/regulator/consumer.h>
#include <linux/version.h>
#include <linux/workqueue.h>

#include "st_sths34pf80.h"

static int st_sths34pf80_write_cfg(struct st_sths34pf80_hw *hw,
				   int add, u8 *buffer, int len);
/**
 * struct st_sths34pf80_odr_table_entry - Sensor ODR table
 * @size: Size of ODR table.
 * @reg: ODR register.
 * @odr_avl: Array of supported ODR values.
 */
static const struct st_sths34pf80_odr_table_entry {
	u8 size;
	struct st_sths34pf80_reg reg;
	struct st_sths34pf80_odr odr_avl[9];
} st_sths34pf80_odr_table = {
	.size = 9,
	.reg = {
		.addr = ST_STHS34PF80_CTRL1_ADDR,
		.mask = ST_STHS34PF80_ODR_MASK,
	},
	.odr_avl = {
		{  0,      0,  0x00, 0x03 },
		{  0, 250000,  0x01, 0x03 },
		{  0, 500000,  0x02, 0x03 },
		{  1,      0,  0x03, 0x03 },
		{  2,      0,  0x04, 0x03 },
		{  4,      0,  0x05, 0x03 },
		{  8,      0,  0x06, 0x03 },
		{ 15,      0,  0x07, 0x02 },
		{ 30,      0,  0x08, 0x02 },
	},
};

/**
 * @brief  TAmbient and TObject IIO channels description
 *
 * TAmbient and TObject exports to IIO framework the following channels:
 *  1) tobject - temperature (milli celsius)
 *  2) tambient - temperature (milli celsius)
 *  3) timestamp (ns)
 */
static const struct iio_chan_spec st_sths34pf80_tobj_amb_channels[] = {
	ST_STHS34PF80_DATA_CHANNEL(IIO_TEMP,
				   ST_STHS34PF80_TOBJECT_L_ADDR, 1,
				   IIO_MOD_TEMP_OBJECT,
				   0, 16, 16, 's'),
	ST_STHS34PF80_DATA_CHANNEL(IIO_TEMP,
				   ST_STHS34PF80_TAMBIENT_L_ADDR, 1,
				   IIO_MOD_TEMP_AMBIENT,
				   1, 16, 16, 's'),
	IIO_CHAN_SOFT_TIMESTAMP(2),
};

#ifdef ST_STHS34PF80_TCOMP
/**
 * @brief  TObject compensated IIO channels description
 *
 * TObject compensated exports to IIO framework the following channels:
 *  1) TObject - temperature (milli celsius)
 *  2) timestamp (ns)
 */
static const struct iio_chan_spec st_sths34pf80_tobject_comp_channels[] = {
	ST_STHS34PF80_DATA_CHANNEL(IIO_TEMP,
				   ST_STHS34PF80_TOBJECT_COMP_L_ADDR, 1,
				   IIO_MOD_TEMP_OBJECT, 0, 16, 16, 's'),
	IIO_CHAN_SOFT_TIMESTAMP(1),
};
#endif /* ST_STHS34PF80_TCOMP */

/**
 * @brief  TMotion IIO channels description
 *
 * TMotion exports to IIO framework the following channels:
 *  1) tmotion
 *  2) tmotion flag
 *  2) timestamp (ns)
 */
static const struct iio_chan_spec st_sths34pf80_tmotion_channels[] = {
	ST_STHS34PF80_DATA_CHANNEL(IIO_TEMP,
				   ST_STHS34PF80_TMOTION_L_ADDR, 1,
				   IIO_MOD_X,
				   0, 16, 16, 's'),
	ST_STHS34PF80_DATA_CHANNEL(IIO_PROXIMITY,
				   ST_STHS34PF80_FUNC_STATUS_ADDR, 1,
				   IIO_MOD_TEMP_OBJECT,
				   1, 8, 8, 's'),
	ST_STHS34PF80_EVENT_CHANNEL(IIO_TEMP),
	IIO_CHAN_SOFT_TIMESTAMP(2),
};

/**
 * @brief  TPresence IIO channels description
 *
 * TPresence exports to IIO framework the following data channels:
 *  1) tpresence
 *  2) tpresence flag
 *  3) timestamp (ns)
 */
static const struct iio_chan_spec st_sths34pf80_tpresence_channels[] = {
	ST_STHS34PF80_DATA_CHANNEL(IIO_TEMP,
				   ST_STHS34PF80_TPRESENCE_L_ADDR, 1,
				   IIO_MOD_Y,
				   0, 16, 16, 's'),
	ST_STHS34PF80_DATA_CHANNEL(IIO_PROXIMITY,
				   ST_STHS34PF80_FUNC_STATUS_ADDR, 1,
				   IIO_MOD_TEMP_OBJECT,
				   1, 8, 8, 's'),
	ST_STHS34PF80_EVENT_CHANNEL(IIO_TEMP),
	IIO_CHAN_SOFT_TIMESTAMP(2),
};

/**
 * @brief  TAmbshock IIO channels description
 *
 * TAmbshock exports to IIO framework the following channels:
 *  1) tambshock
 *  2) tambshock flag
 *  3) timestamp (ns)
 */
static const struct iio_chan_spec st_sths34pf80_tambshock_channels[] = {
	ST_STHS34PF80_DATA_CHANNEL(IIO_TEMP,
				   ST_STHS34PF80_TAMB_SHOCK_L_ADDR, 1,
				   IIO_MOD_Z,
				   0, 16, 16, 's'),
	ST_STHS34PF80_DATA_CHANNEL(IIO_PROXIMITY,
				   ST_STHS34PF80_FUNC_STATUS_ADDR, 1,
				   IIO_MOD_TEMP_OBJECT,
				   1, 8, 8, 's'),
	ST_STHS34PF80_EVENT_CHANNEL(IIO_TEMP),
	IIO_CHAN_SOFT_TIMESTAMP(2),
};

static __maybe_unused int
st_sths34pf80_reg_access(struct iio_dev *iio_dev,
			 unsigned int reg, unsigned int writeval,
			 unsigned int *readval)
{
	struct st_sths34pf80_sensor *sensor = iio_priv(iio_dev);
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

/**
 * st_sths34pf80_check_whoami - Check value of HW Device ID
 *
 * @param  hw: ST TMOS MEMS sensr hw structure.
 * @return  0 if OK, negative value for ERROR.
 */
static int st_sths34pf80_check_whoami(struct st_sths34pf80_hw *hw)
{
	int err, data;

	err = regmap_read(hw->regmap, ST_STHS34PF80_WHOAMI_ADDR, &data);
	if (err < 0) {
		dev_err(hw->dev, "failed to read whoami register\n");

		return err;
	}

	if (data != ST_STHS34PF80_WHOAMI_VAL) {
		dev_err(hw->dev, "unsupported whoami [%02x]\n", data);

		return -ENODEV;
	}

	return 0;
}

static int
st_sths34pf80_get_odr_val(int odr, int uodr,
			  const struct st_sths34pf80_odr **oe)
{
	int req_odr = ST_STHS34PF80_ODR_EXPAND(odr, uodr);
	int sensor_odr;
	int i;

	for (i = 0; i < st_sths34pf80_odr_table.size; i++) {
		sensor_odr = ST_STHS34PF80_ODR_EXPAND(
			    st_sths34pf80_odr_table.odr_avl[i].hz,
			    st_sths34pf80_odr_table.odr_avl[i].uhz);
		if (sensor_odr >= req_odr) {
			*oe = &st_sths34pf80_odr_table.odr_avl[i];

			return 0;
		}
	}

	return -EINVAL;
}

static u16
st_sths34pf80_check_odr_dependency(struct st_sths34pf80_hw *hw,
				   int odr, int uodr,
				   enum st_sths34pf80_sensor_id ref_id,
				   enum st_sths34pf80_sensor_id id)
{
	struct st_sths34pf80_sensor *ref = iio_priv(hw->iio_devs[ref_id]);
	struct st_sths34pf80_sensor *orig = iio_priv(hw->iio_devs[id]);
	bool enable = odr > 0;
	u16 ret;

	if (enable) {
		if (hw->enable_mask & BIT(ref_id))
			ret = max_t(u16, ref->odr, odr);
		else
			ret = odr;
	} else {
		if (hw->enable_mask & BIT(id) && hw->event_mask & BIT(id))
			return orig->odr;

		ret = (hw->enable_mask & BIT(ref_id) ||
		       hw->event_mask & BIT(ref_id)) ? ref->odr : 0;
	}

	return ret;
}

static int st_sths34pf80_set_odr(struct st_sths34pf80_sensor *sensor,
				 int req_odr, int req_uodr)
{
	enum st_sths34pf80_sensor_id id = sensor->id;
	struct st_sths34pf80_hw *hw = sensor->hw;
	const struct st_sths34pf80_odr *oe;
	int ret;
	u8 updt;

	switch (id) {
	case ST_STHS34PF80_ID_TAMB_OBJ:

#ifdef ST_STHS34PF80_TCOMP
	case ST_STHS34PF80_ID_TOBJECT_COMP:
#endif /* ST_STHS34PF80_TCOMP */

	case ST_STHS34PF80_ID_TAMB_SHOCK:
	case ST_STHS34PF80_ID_TMOTION:
	case ST_STHS34PF80_ID_TPRESENCE: {
		int odr;
		int i;

		id = ST_STHS34PF80_ID_TAMB_OBJ;
		for (i = ST_STHS34PF80_ID_TAMB_OBJ;
		     i < ST_STHS34PF80_ID_MAX; i++) {
			if (!hw->iio_devs[i] || i == sensor->id)
				continue;

			odr = st_sths34pf80_check_odr_dependency(hw, req_odr,
								 req_uodr, i,
								 sensor->id);
			if (odr != req_odr)
				return 0;
		}
		break;
	}
	default:
		break;
	}

	ret = st_sths34pf80_get_odr_val(req_odr, req_uodr, &oe);
	if (ret)
		return ret;

	/* update avg tmos bitfield accordingly to new odr */
	ret = st_sths34pf80_update_bits_locked(hw,
					    ST_STHS34PF80_AVG_TRIM_ADDR,
					    ST_STHS34PF80_AVG_TMOS_MASK,
					    oe->avg);
	if (ret < 0)
		return ret;

	ret = st_sths34pf80_update_bits_locked(hw,
				       st_sths34pf80_odr_table.reg.addr,
				       st_sths34pf80_odr_table.reg.mask,
				       oe->val);
	if (ret < 0)
		return ret;

	/* reset algos when in power down mode (odr = 0) */
	if (oe->val == 0) {
		updt = ST_STHS34PF80_ALGO_ENABLE_RESET_MASK;
		ret = st_sths34pf80_write_cfg(hw, ST_STHS34PF80_RESET_ALGO_ADDR,
					      &updt, 1);
	}

	return ret < 0 ? ret : 0;
}

/**
 * st_sths34pf80_manage_interrupt_cfg - Configure interrupts
 *
 * @param  sensor: IIO event sensor.
 * @param  state: New event state.
 * @return 0 if OK, negative for ERROR
 */
static int
st_sths34pf80_manage_interrupt_cfg(struct st_sths34pf80_sensor *sensor,
				   bool new_ev_state, bool new_en_state)
{
	enum st_sths34pf80_sensor_id id = sensor->id;
	struct st_sths34pf80_hw *hw = sensor->hw;
	int err = 0;

	mutex_lock(&hw->int_lock);

	if ((hw->event_mask & BIT(id)) != new_ev_state) {
		if (!hw->event_mask && !hw->enable_mask && new_ev_state) {
			/* set data ready pulsed */
			err = st_sths34pf80_update_bits_locked(hw,
						 ST_STHS34PF80_CTRL3_ADDR,
						 ST_STHS34PF80_INT_LATCHED_MASK,
						 0);
			if (err < 0)
				goto unlock;

			err = st_sths34pf80_update_bits_locked(hw,
						  ST_STHS34PF80_CTRL3_ADDR,
						  ST_STHS34PF80_IEN_MASK,
						  ST_STHS34PF80_IEN_INT_OR_VAL);
		} else if (((hw->event_mask & BIT(id)) == hw->event_mask) &&
			    !new_ev_state) {
			if (!hw->edge_trigger) {
				/* set data ready latched */
				err = st_sths34pf80_update_bits_locked(hw,
						 ST_STHS34PF80_CTRL3_ADDR,
						 ST_STHS34PF80_INT_LATCHED_MASK,
						 1);
				if (err < 0)
					goto unlock;
			}

			err = st_sths34pf80_update_bits_locked(hw,
						ST_STHS34PF80_CTRL3_ADDR,
						ST_STHS34PF80_IEN_MASK,
						hw->enable_mask ?
						ST_STHS34PF80_IEN_DRDY_VAL : 0);
		}
	}

	if ((hw->enable_mask & BIT(id)) != new_en_state) {
		if (new_en_state) {
			err = st_sths34pf80_update_bits_locked(hw,
					    ST_STHS34PF80_CTRL3_ADDR,
					    ST_STHS34PF80_IEN_MASK,
					    ST_STHS34PF80_IEN_DRDY_VAL);
			if (err < 0)
				goto unlock;

			/* set data ready latched */
			err = st_sths34pf80_update_bits_locked(hw,
					 ST_STHS34PF80_CTRL3_ADDR,
					 ST_STHS34PF80_INT_LATCHED_MASK,
					 1);
			if (err < 0)
				goto unlock;
		} else {
			if ((hw->enable_mask) == BIT(id)) {
				err = st_sths34pf80_update_bits_locked(hw,
					      ST_STHS34PF80_CTRL3_ADDR,
					      ST_STHS34PF80_IEN_MASK,
					      hw->event_mask ?
					      ST_STHS34PF80_IEN_INT_OR_VAL : 0);
				if (err < 0)
					goto unlock;

				/* reset data ready latched */
				err = st_sths34pf80_update_bits_locked(hw,
						 ST_STHS34PF80_CTRL3_ADDR,
						 ST_STHS34PF80_INT_LATCHED_MASK,
						 0);
				if (err < 0)
					goto unlock;
			}
		}
	}

unlock:
	mutex_unlock(&hw->int_lock);

	return err < 0 ? err : 0;
}

static int
st_sths34pf80_sensor_set_enable(struct st_sths34pf80_sensor *sensor,
				bool enable)
{
	struct st_sths34pf80_hw *hw = sensor->hw;
	int uodr = enable ? sensor->uodr : 0;
	int odr = enable ? sensor->odr : 0;
	int err;

	err = st_sths34pf80_set_odr(sensor, odr, uodr);
	if (err < 0)
		return err;

	err = st_sths34pf80_manage_interrupt_cfg(sensor,
					   !!(hw->event_mask & BIT(sensor->id)),
					   enable);
	if (err)
		return err;

	if (enable)
		hw->enable_mask |= BIT(sensor->id);
	else
		hw->enable_mask &= ~BIT(sensor->id);

	return 0;
}

static int
st_sths34pf80_event_set_enable(struct st_sths34pf80_sensor *sensor,
			       bool enable)
{
	int uodr = enable ? sensor->uodr : 0;
	int odr = enable ? sensor->odr : 0;
	int err;

	err = st_sths34pf80_set_odr(sensor, odr, uodr);
	if (err < 0)
		return err;

	if (enable)
		sensor->hw->event_mask |= BIT(sensor->id);
	else
		sensor->hw->event_mask &= ~BIT(sensor->id);

	return 0;
}

/**
 * st_sths34pf80_write_cfg - write buffer to embedded function registers
 *
 * @param  hw: ST TMOS MEMS hw instance.
 * @param  add: Starting embedded function address register.
 * @param  buffer: Buffer data to write to embedded function registers.
 * @param  len: Buffer data len.
 * @return 0 if OK, negative for ERROR
 */
static int st_sths34pf80_write_cfg(struct st_sths34pf80_hw *hw,
				   int add, u8 *buffer, int len)
{
	int err, i;

	/* enable access to embedded function registers */
	err = regmap_update_bits(hw->regmap,
			  ST_STHS34PF80_CTRL2_ADDR,
			  ST_STHS34PF80_FUNC_CFG_ACCESS_MASK,
			  FIELD_PREP(ST_STHS34PF80_FUNC_CFG_ACCESS_MASK,
				     1));
	if (err < 0)
		return err;

	err = regmap_update_bits(hw->regmap, ST_STHS34PF80_PAGE_RW_ADDR,
				 ST_STHS34PF80_FUNC_CFG_WRITE_MASK,
				 FIELD_PREP(ST_STHS34PF80_FUNC_CFG_WRITE_MASK, 1));
	if (err < 0)
		goto out_err;

	err = regmap_write(hw->regmap,
			   ST_STHS34PF80_FUNC_CFG_ADDR_ADDR, add);
	if (err < 0)
		goto out_err;

	/* loop on write, by default auto increments is enabled */
	for (i = 0; i < len; i++) {
		err = regmap_write(hw->regmap,
				   ST_STHS34PF80_FUNC_CFG_DATA_ADDR,
				   buffer[i]);
		if (err < 0)
			goto out_err;
	}

	err = regmap_update_bits(hw->regmap, ST_STHS34PF80_PAGE_RW_ADDR,
				 ST_STHS34PF80_FUNC_CFG_WRITE_MASK,
				 FIELD_PREP(ST_STHS34PF80_FUNC_CFG_WRITE_MASK, 0));

out_err:
	regmap_write(hw->regmap, ST_STHS34PF80_CTRL2_ADDR, 0);

	return err < 0 ? err : 0;
}

/**
 * st_sths34pf80_read_cfg - read embedded function register
 *
 * @param  hw: ST TMOS MEMS hw instance.
 * @param  add: Starting embedded function address register.
 * @param  val: Buffer data to write to embedded function registers.
 * @return 0 if OK, negative for ERROR
 */
static int __maybe_unused st_sths34pf80_read_cfg(struct st_sths34pf80_hw *hw,
						 int add, u8 *val)
{
	unsigned int rval;
	int err;

	/* enable access to embedded function registers */
	err = regmap_update_bits(hw->regmap,
			  ST_STHS34PF80_CTRL2_ADDR,
			  ST_STHS34PF80_FUNC_CFG_ACCESS_MASK,
			  FIELD_PREP(ST_STHS34PF80_FUNC_CFG_ACCESS_MASK, 1));
	if (err < 0)
		return err;

	err = regmap_update_bits(hw->regmap, ST_STHS34PF80_PAGE_RW_ADDR,
				 ST_STHS34PF80_FUNC_CFG_READ_MASK,
				 FIELD_PREP(ST_STHS34PF80_FUNC_CFG_READ_MASK, 1));
	if (err < 0)
		goto out_err;

	err = regmap_write(hw->regmap, ST_STHS34PF80_FUNC_CFG_ADDR_ADDR, add);
	if (err < 0)
		goto out_err;

	err = regmap_read(hw->regmap,
			  ST_STHS34PF80_FUNC_CFG_DATA_ADDR,
			  &rval);
	if (err < 0)
		goto out_err;

	*val = (u8)rval;

	err = regmap_update_bits(hw->regmap, ST_STHS34PF80_PAGE_RW_ADDR,
				 ST_STHS34PF80_FUNC_CFG_READ_MASK,
				 FIELD_PREP(ST_STHS34PF80_FUNC_CFG_READ_MASK, 0));

out_err:
	regmap_write(hw->regmap, ST_STHS34PF80_CTRL2_ADDR, 0);

	return err < 0 ? err : 0;
}

/**
 * st_sths34pf80_update_cfg - update embedded function register
 *
 * @param  hw: ST TMOS MEMS hw instance.
 * @param  add: Embedded function address register.
 * @param  mask: Data mask.
 * @param  val: Data to update to embedded function registers.
 * @return 0 if OK, negative for ERROR
 */
static int st_sths34pf80_update_cfg(struct st_sths34pf80_hw *hw,
				    int add, u8 mask, u8 val)
{
	u8 data;
	int err;

	err = st_sths34pf80_read_cfg(hw, add, &data);
	if (err < 0)
		return err;

	data = (data & ~mask) | ((val << __ffs(mask)) & mask);

	return st_sths34pf80_write_cfg(hw, add, &data, 1);
}

static int st_sths34pf80_reset_algos(struct st_sths34pf80_hw *hw)
{
	int err, odr;
	u8 updt;

	/* disable odr */
	err = regmap_bulk_read(hw->regmap,
			       st_sths34pf80_odr_table.reg.addr,
			       &odr, 1);
	if (err)
		return err;

	err = __st_sths34pf80_write_with_mask(hw,
					      st_sths34pf80_odr_table.reg.addr,
					      st_sths34pf80_odr_table.reg.mask,
					      0);
	if (err)
		return err;

	updt = ST_STHS34PF80_ALGO_ENABLE_RESET_MASK;
	err = st_sths34pf80_write_cfg(hw, ST_STHS34PF80_RESET_ALGO_ADDR,
				      &updt, 1);
	if (err)
		return err;

	/* restore odr */
	err = regmap_write(hw->regmap, st_sths34pf80_odr_table.reg.addr, odr);

	return err < 0 ? err : 0;
}

#ifdef ST_STHS34PF80_TCOMP
static int st_sths34pf80_enable_comp(struct st_sths34pf80_hw *hw,
				     bool enable)
{
	u8 val;
	int err;
	int odr;

	mutex_lock(&hw->page_lock);

	/* disable odr */
	err = regmap_bulk_read(hw->regmap,
			       st_sths34pf80_odr_table.reg.addr,
			       &odr, 1);
	if (err)
		goto out_unlock;

	err = __st_sths34pf80_write_with_mask(hw,
					      st_sths34pf80_odr_table.reg.addr,
					      st_sths34pf80_odr_table.reg.mask,
					      0);
	if (err)
		goto out_unlock;

	err = st_sths34pf80_update_cfg(hw, ST_STHS34PF80_ALGO_CONFIG_ADDR,
				       ST_STHS34PF80_COMP_TYPE_MASK,
				       enable ? 1 : 0);
	if (err)
		goto out_unlock;

	val = ST_STHS34PF80_ALGO_ENABLE_RESET_MASK;
	err = st_sths34pf80_write_cfg(hw, ST_STHS34PF80_RESET_ALGO_ADDR,
				      &val, 1);
	if (err < 0)
		goto out_unlock;

	hw->tcomp = enable;

out_unlock:
	/* restore odr */
	err = regmap_write(hw->regmap, st_sths34pf80_odr_table.reg.addr, odr);
	mutex_unlock(&hw->page_lock);

	return err;
}
#endif /* ST_STHS34PF80_TCOMP */

static int
st_sths34pf80_read_oneshot(struct st_sths34pf80_sensor *sensor,
			   struct iio_chan_spec const *ch, int *val)
{
	struct st_sths34pf80_hw *hw = sensor->hw;
	enum st_sths34pf80_sensor_id id = sensor->id;
	u8 addr = ch->address;
	int err, delay;
	__le16 data;
	u8 status;

	err = st_sths34pf80_sensor_set_enable(sensor, true);
	if (err < 0)
		return err;

	switch (id) {
	case ST_STHS34PF80_ID_TAMB_OBJ:

#ifdef ST_STHS34PF80_TCOMP
	case ST_STHS34PF80_ID_TOBJECT_COMP:
#endif /* ST_STHS34PF80_TCOMP */

		/* consider also ODRs < 1 Hz */
		if (sensor->odr > 0)
			delay = 1000000 / sensor->odr;
		else
			delay = 1000000 * (1000000 / sensor->uodr);

		usleep_range(delay, (delay >> 1) + delay);

		err = st_sths34pf80_read_locked(hw, addr,
						&data, sizeof(data));
		if (err < 0)
			goto disable;

		*val = (s16)le16_to_cpu(data);
		break;
	case ST_STHS34PF80_ID_TAMB_SHOCK:
	case ST_STHS34PF80_ID_TMOTION:
	case ST_STHS34PF80_ID_TPRESENCE:
		/* consider also ODRs < 1 Hz */
		if (sensor->odr > 0)
			delay = 3000000 / sensor->odr;
		else
			delay = 3000000 * (1000000 / sensor->uodr);

		usleep_range(delay, (delay >> 1) + delay);

		if (ch->scan_index == 0) {
			err = st_sths34pf80_read_locked(hw, addr, &data,
							sizeof(data));
			if (err < 0)
				goto disable;

			*val = (s16)le16_to_cpu(data);
		} else {
			err = st_sths34pf80_read_locked(hw, addr, &status,
							sizeof(status));
			if (err < 0)
				goto disable;

			status = (status & (1 << (id - ST_STHS34PF80_ID_TAMB_SHOCK))) ? 1 : 0;
			*val = (int)status;
		}
		break;
	default:
		break;
	}
disable:
	st_sths34pf80_sensor_set_enable(sensor, false);

	return err < 0 ? err : IIO_VAL_INT;
}

static int st_sths34pf80_read_raw(struct iio_dev *iio_dev,
				  struct iio_chan_spec const *ch,
				  int *val, int *val2, long mask)
{
	struct st_sths34pf80_sensor *sensor = iio_priv(iio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(iio_dev);
		if (ret)
			return ret;

		ret = st_sths34pf80_read_oneshot(sensor, ch, val);
		iio_device_release_direct_mode(iio_dev);
		break;
	case IIO_CHAN_INFO_OFFSET:
		switch (ch->type) {
		case IIO_TEMP:
			*val = 0;
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
			switch (sensor->id) {
			case ST_STHS34PF80_ID_TAMB_OBJ:
				*val = 1000;
				ret = IIO_VAL_FRACTIONAL;

				if (ch->channel2 == IIO_MOD_TEMP_OBJECT)
					*val2 = ST_STHS34PF80_TOBJECT_GAIN;
				else
					*val2 = ST_STHS34PF80_TAMBIENT_GAIN;

				break;
#ifdef ST_STHS34PF80_TCOMP
			case ST_STHS34PF80_ID_TOBJECT_COMP:
				*val = 1;
				*val2 = ST_STHS34PF80_TOBJECT_COMP_GAIN;
				ret = IIO_VAL_FRACTIONAL;
				break;
#endif /* ST_STHS34PF80_TCOMP */
			case ST_STHS34PF80_ID_TAMB_SHOCK:
				*val = 1;
				*val2 = 0;
				ret = IIO_VAL_INT;
				break;
			case ST_STHS34PF80_ID_TMOTION:
				*val = 1;
				*val2 = 0;
				ret = IIO_VAL_INT;
				break;
			case ST_STHS34PF80_ID_TPRESENCE:
				*val = 1;
				*val2 = 0;
				ret = IIO_VAL_INT;
				break;
			default:
				return -EINVAL;
			}
		break;
		case IIO_PROXIMITY:
			switch (sensor->id) {
			case ST_STHS34PF80_ID_TAMB_SHOCK:
				*val = 1;
				*val2 = 0;
				ret = IIO_VAL_INT;
				break;
			case ST_STHS34PF80_ID_TMOTION:
				*val = 1;
				*val2 = 0;
				ret = IIO_VAL_INT;
				break;
			case ST_STHS34PF80_ID_TPRESENCE:
				*val = 1;
				*val2 = 0;
				ret = IIO_VAL_INT;
				break;
			default:
				return -EINVAL;
			}
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

static int st_sths34pf80_write_raw(struct iio_dev *iio_dev,
				   struct iio_chan_spec const *chan,
				   int val, int val2, long mask)
{
	struct st_sths34pf80_sensor *sensor = iio_priv(iio_dev);
	int err;

	mutex_lock(&iio_dev->mlock);

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ: {
		const struct st_sths34pf80_odr *oe;

		err = st_sths34pf80_get_odr_val(val, val2, &oe);
		if (!err) {
			sensor->odr = oe->hz;
			sensor->uodr = oe->uhz;
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

/**
 * st_sths34pf80_event_sensor_enable - Enable event sensor
 *
 * @param  sensor: IIO event sensor.
 * @param  state: New event state.
 * @return 0 if OK, negative for ERROR
 */
static int
st_sths34pf80_event_sensor_enable(struct st_sths34pf80_sensor *sensor,
				  bool state)
{
	struct st_sths34pf80_hw *hw = sensor->hw;
	u8 int_mask = 0;
	int err;

	if (!!(hw->event_mask & BIT(sensor->id)) == state)
		return 0;

	switch (sensor->id) {
	case ST_STHS34PF80_ID_TAMB_OBJ:

#ifdef ST_STHS34PF80_TCOMP
	case ST_STHS34PF80_ID_TOBJECT_COMP:
#endif /* ST_STHS34PF80_TCOMP */

		return -EINVAL;
	case ST_STHS34PF80_ID_TAMB_SHOCK:
		int_mask = ST_STHS34PF80_INT_MSK0_MASK;
		break;
	case ST_STHS34PF80_ID_TMOTION:
		int_mask = ST_STHS34PF80_INT_MSK1_MASK;
		break;
	case ST_STHS34PF80_ID_TPRESENCE:
		int_mask = ST_STHS34PF80_INT_MSK2_MASK;
		break;
	default:
		return -ENODEV;
	}

	err = st_sths34pf80_update_bits_locked(hw,
					       ST_STHS34PF80_CTRL3_ADDR,
					       int_mask, state ? 1 : 0);
	if (err)
		return err;

	err = st_sths34pf80_manage_interrupt_cfg(sensor, state,
					 !!(hw->enable_mask & BIT(sensor->id)));
	if (err)
		return err;

	return st_sths34pf80_event_set_enable(sensor, state);
}

/**
 * st_sths34pf80_read_event_config - Read sensor event configuration
 *
 * @param  iio_dev: IIO Device.
 * @param  chan: IIO Channel.
 * @param  type: Event Type.
 * @param  dir: Event Direction.
 * @return  1 if Enabled, 0 Disabled
 */
static int
st_sths34pf80_read_event_config(struct iio_dev *iio_dev,
				const struct iio_chan_spec *chan,
				enum iio_event_type type,
				enum iio_event_direction dir)
{
	struct st_sths34pf80_sensor *sensor = iio_priv(iio_dev);
	struct st_sths34pf80_hw *hw = sensor->hw;
	int ret;

	mutex_lock(&iio_dev->mlock);
	ret = !!(hw->event_mask & BIT(sensor->id));
	mutex_unlock(&iio_dev->mlock);

	return ret;
}

/**
 * st_sths34pf80_write_event_config - Write sensor event configuration
 *
 * @param  iio_dev: IIO Device.
 * @param  chan: IIO Channel.
 * @param  type: Event Type.
 * @param  dir: Event Direction.
 * @param  state: New event state.
 * @return  0 if OK, negative for ERROR
 */
static int
st_sths34pf80_write_event_config(struct iio_dev *iio_dev,
				 const struct iio_chan_spec *chan,
				 enum iio_event_type type,
				 enum iio_event_direction dir,
				 int state)
{
	struct st_sths34pf80_sensor *sensor = iio_priv(iio_dev);
	int err;

	mutex_lock(&iio_dev->mlock);
	err = st_sths34pf80_event_sensor_enable(sensor, state);
	mutex_unlock(&iio_dev->mlock);

	return err;
}

static ssize_t
st_sths34pf80_sysfs_sampling_freq_avail(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	int i, len = 0;

	for (i = 0; i < st_sths34pf80_odr_table.size; i++) {
		len += scnprintf(buf + len, PAGE_SIZE - len, "%d.%06d ",
				 st_sths34pf80_odr_table.odr_avl[i].hz,
				 st_sths34pf80_odr_table.odr_avl[i].uhz);
	}

	buf[len - 1] = '\n';

	return len;
}

static void __maybe_unused
st_sths34pf80_report_event(struct st_sths34pf80_sensor *sensor,
			   u8 *tmp, int64_t timestamp)
{
	u8 iio_buf[ALIGN(2, sizeof(s64)) + sizeof(s64) + sizeof(s64)];
	struct iio_dev *iio_dev = sensor->hw->iio_devs[sensor->id];

	memcpy(iio_buf, tmp, 2);
	iio_push_to_buffers_with_timestamp(iio_dev, iio_buf, timestamp);
}

static void
st_sths34pf80_report_2event(struct st_sths34pf80_sensor *sensor,
			    u8 *tmp, int64_t timestamp)
{
	u8 iio_buf[ALIGN(3, sizeof(s64)) + sizeof(s64) + sizeof(s64)];
	struct iio_dev *iio_dev = sensor->hw->iio_devs[sensor->id];

	memcpy(iio_buf, tmp, 4);
	iio_push_to_buffers_with_timestamp(iio_dev, iio_buf, timestamp);
}

static void
st_sths34pf80_report_algo_event(struct st_sths34pf80_sensor *sensor,
				u8 *tmp, int64_t timestamp)
{
	u8 iio_buf[ALIGN(3, sizeof(s64)) + sizeof(s64)];
	struct iio_dev *iio_dev = sensor->hw->iio_devs[sensor->id];

	memcpy(iio_buf, tmp, 3);
	iio_push_to_buffers_with_timestamp(iio_dev, iio_buf, timestamp);
}

static irqreturn_t st_sths34pf80_handler_irq(int irq, void *private)
{
	struct st_sths34pf80_hw *hw = (struct st_sths34pf80_hw *)private;

	hw->ts = st_sths34pf80_get_time_ns(hw);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t st_sths34pf80_handler_thread(int irq, void *private)
{
	struct st_sths34pf80_hw *hw = (struct st_sths34pf80_hw *)private;
	struct st_sths34pf80_sensor *sensor;
	struct iio_dev *iio_dev;
	u8 func_status;
	u8 data[4];
	s64 event;
	int err;
	int id;

	mutex_lock(&hw->page_lock);

	err = regmap_bulk_read(hw->regmap, ST_STHS34PF80_FUNC_STATUS_ADDR,
			       &func_status, 1);
	if (err < 0)
		goto out_err;

	if (hw->enable_mask & BIT(ST_STHS34PF80_ID_TAMB_OBJ)) {
		id = ST_STHS34PF80_ID_TAMB_OBJ;
		sensor = iio_priv(hw->iio_devs[id]);
		err = regmap_bulk_read(hw->regmap,
				       hw->iio_devs[id]->channels->address,
				       data, 4);
		if (err < 0)
			goto out_err;

		st_sths34pf80_report_2event(sensor, data, hw->ts);
	}

#ifdef ST_STHS34PF80_TCOMP
	if (hw->enable_mask & BIT(ST_STHS34PF80_ID_TOBJECT_COMP)) {
		id = ST_STHS34PF80_ID_TOBJECT_COMP;
		sensor = iio_priv(hw->iio_devs[id]);
		err = regmap_bulk_read(hw->regmap,
				       hw->iio_devs[id]->channels->address,
				       data, 2);
		if (err < 0)
			goto out_err;

		st_sths34pf80_report_event(sensor, data, hw->ts);
	}
#endif /* ST_STHS34PF80_TCOMP */

	if (hw->enable_mask & BIT(ST_STHS34PF80_ID_TAMB_SHOCK)) {
		id = ST_STHS34PF80_ID_TAMB_SHOCK;
		sensor = iio_priv(hw->iio_devs[id]);
		err = regmap_bulk_read(hw->regmap,
				       hw->iio_devs[id]->channels->address,
				       data, 2);
		if (err < 0)
			goto out_err;

		data[2] = (u8)(func_status & ST_STHS34PF80_TAMB_SHOCK_FLAG_MASK) ? 1 : 0;
		st_sths34pf80_report_algo_event(sensor, data, hw->ts);
	}

	if (hw->enable_mask & BIT(ST_STHS34PF80_ID_TMOTION)) {
		id = ST_STHS34PF80_ID_TMOTION;
		sensor = iio_priv(hw->iio_devs[id]);
		err = regmap_bulk_read(hw->regmap,
				       hw->iio_devs[id]->channels->address,
				       data, 2);
		if (err < 0)
			goto out_err;

		data[2] = (u8)(func_status & ST_STHS34PF80_MOT_FLAG_MASK) ? 1 : 0;
		st_sths34pf80_report_algo_event(sensor, data, hw->ts);
	}

	if (hw->enable_mask & BIT(ST_STHS34PF80_ID_TPRESENCE)) {
		id = ST_STHS34PF80_ID_TPRESENCE;
		sensor = iio_priv(hw->iio_devs[id]);
		err = regmap_bulk_read(hw->regmap,
				       hw->iio_devs[id]->channels->address,
				       data, 2);
		if (err < 0)
			goto out_err;

		data[2] = (u8)(func_status & ST_STHS34PF80_PRES_FLAG_MASK) ? 1 : 0;
		st_sths34pf80_report_algo_event(sensor, data, hw->ts);
	}

	if (func_status & ST_STHS34PF80_PRES_FLAG_MASK &&
	    hw->event_mask & BIT(ST_STHS34PF80_ID_TPRESENCE)) {
		iio_dev = hw->iio_devs[ST_STHS34PF80_ID_TPRESENCE];
		event = IIO_UNMOD_EVENT_CODE(IIO_TEMP, -1,
					     IIO_EV_TYPE_THRESH,
					     IIO_EV_DIR_RISING);
		iio_push_event(iio_dev, event, hw->ts);
	}

	if (func_status & ST_STHS34PF80_MOT_FLAG_MASK &&
	    hw->event_mask & BIT(ST_STHS34PF80_ID_TMOTION)) {
		iio_dev = hw->iio_devs[ST_STHS34PF80_ID_TMOTION];
		event = IIO_UNMOD_EVENT_CODE(IIO_TEMP, -1,
					     IIO_EV_TYPE_THRESH,
					     IIO_EV_DIR_RISING);
		iio_push_event(iio_dev, event, hw->ts);
	}

	if (func_status & ST_STHS34PF80_TAMB_SHOCK_FLAG_MASK &&
	    hw->event_mask & BIT(ST_STHS34PF80_ID_TAMB_SHOCK)) {
		iio_dev = hw->iio_devs[ST_STHS34PF80_ID_TAMB_SHOCK];
		event = IIO_UNMOD_EVENT_CODE(IIO_TEMP, -1,
					     IIO_EV_TYPE_THRESH,
					     IIO_EV_DIR_RISING);
		iio_push_event(iio_dev, event, hw->ts);
	}

out_err:
	mutex_unlock(&hw->page_lock);

	return IRQ_HANDLED;
}

static int st_sths34pf80_int_config(struct st_sths34pf80_hw *hw)
{
	bool edge_trigger = false;
	unsigned long irq_type;
	bool irq_active_low;
	int err;

	irq_type = irqd_get_trigger_type(irq_get_irq_data(hw->irq));
	if (irq_type == IRQF_TRIGGER_NONE)
		irq_type = IRQF_TRIGGER_RISING;

	switch (irq_type) {
	case IRQF_TRIGGER_RISING:
		edge_trigger = true;
		irq_active_low = false;
		break;
	case IRQF_TRIGGER_HIGH:
		irq_active_low = false;
		break;
	case IRQF_TRIGGER_FALLING:
		edge_trigger = true;
		irq_active_low = true;
		break;
	case IRQF_TRIGGER_LOW:
		irq_active_low = true;
		break;
	default:
		dev_err(hw->dev, "mode %lx unsupported\n", irq_type);

		return -EINVAL;
	}

	err = st_sths34pf80_update_bits_locked(hw, ST_STHS34PF80_CTRL3_ADDR,
					       ST_STHS34PF80_INT_H_L_MASK,
					       irq_active_low ? 1 : 0);
	if (err < 0)
		return err;

	if (device_property_read_bool(hw->dev, "drive-open-drain")) {
		err = st_sths34pf80_update_bits_locked(hw,
						   ST_STHS34PF80_CTRL3_ADDR,
						   ST_STHS34PF80_PP_OD_MASK, 1);
		if (err < 0)
			return err;
	}

	err = devm_request_threaded_irq(hw->dev, hw->irq,
					st_sths34pf80_handler_irq,
					st_sths34pf80_handler_thread,
					irq_type | IRQF_ONESHOT,
					"sths34pf80", hw);
	if (err) {
		dev_err(hw->dev, "failed to request trigger irq %d\n",
			hw->irq);

		return err;
	}

	if (edge_trigger) {
		/* set data ready pulsed */
		err = st_sths34pf80_update_bits_locked(hw,
						 ST_STHS34PF80_CTRL3_ADDR,
						 ST_STHS34PF80_INT_LATCHED_MASK,
						 0);
		if (err < 0)
			return err;

		hw->edge_trigger = true;
	} else {
		/* set data ready latched */
		err = st_sths34pf80_update_bits_locked(hw,
						 ST_STHS34PF80_CTRL3_ADDR,
						 ST_STHS34PF80_INT_LATCHED_MASK,
						 1);
		if (err < 0)
			return err;

		hw->edge_trigger = false;
	}

	/* enable int_or in pulsed mode */
	err = st_sths34pf80_update_cfg(hw, ST_STHS34PF80_ALGO_CONFIG_ADDR,
				       ST_STHS34PF80_INT_PULSED_MASK, 1);
	if (err)
		return err;

	err = st_sths34pf80_update_bits_locked(hw, ST_STHS34PF80_CTRL3_ADDR,
					       ST_STHS34PF80_IEN_MASK, 0);

	return err < 0 ? err : 0;
}

static int st_sths34pf80_update_lpf(struct st_sths34pf80_sensor *sensor,
				    u8 val)
{
	struct st_sths34pf80_hw *hw = sensor->hw;
	int err, odr;

	mutex_lock(&hw->page_lock);

	/* disable odr */
	err = regmap_bulk_read(sensor->hw->regmap,
			       st_sths34pf80_odr_table.reg.addr,
			       &odr, 1);
	if (err)
		goto out_err;

	err = __st_sths34pf80_write_with_mask(sensor->hw,
				st_sths34pf80_odr_table.reg.addr,
				st_sths34pf80_odr_table.reg.mask, 0);
	if (err)
		goto out_err;

	err = __st_sths34pf80_write_with_mask(sensor->hw,
					      sensor->lpf.reg,
					      sensor->lpf.mask, val);
	if (err)
		goto out_err;

	err = st_sths34pf80_reset_algos(hw);
	if (err)
		goto out_err;

	/* restore odr */
	err = regmap_write(sensor->hw->regmap,
			   st_sths34pf80_odr_table.reg.addr, odr);
	if (err < 0)
		goto out_err;

	sensor->lpf.val = val;

out_err:
	mutex_unlock(&hw->page_lock);

	return 0;
}

static ssize_t st_sths34pf80_lpf_get(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct st_sths34pf80_sensor *sensor =
					 iio_priv(dev_to_iio_dev(dev));

	return sprintf(buf, "%d\n", sensor->lpf.val);
}

static ssize_t st_sths34pf80_lpf_set(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_sths34pf80_sensor *sensor = iio_priv(iio_dev);
	int err, val;

	err = iio_device_claim_direct_mode(iio_dev);
	if (err)
		return err;

	err = kstrtoint(buf, 10, &val);
	if (err < 0)
		goto out;

	err = st_sths34pf80_update_lpf(sensor, val);

out:
	iio_device_release_direct_mode(iio_dev);

	return err < 0 ? err : size;
}

static int st_sths34pf80_update_threshold(struct st_sths34pf80_sensor *sensor,
					  u16 val)
{
	struct st_sths34pf80_hw *hw = sensor->hw;
	__le16 wdata;
	int err, odr;

	mutex_lock(&hw->page_lock);

	/* disable odr */
	err = regmap_bulk_read(hw->regmap,
			       st_sths34pf80_odr_table.reg.addr,
			       &odr, 1);
	if (err)
		goto out_err;

	err = __st_sths34pf80_write_with_mask(hw,
				st_sths34pf80_odr_table.reg.addr,
				st_sths34pf80_odr_table.reg.mask, 0);
	if (err)
		goto out_err;

	wdata = cpu_to_le16(val);
	err = st_sths34pf80_write_cfg(hw, sensor->threshold.reg,
				      (u8 *)&wdata, sizeof(wdata));
	if (err)
		goto out_err;

	err = st_sths34pf80_reset_algos(hw);
	if (err)
		goto out_err;

	/* restore odr */
	err = regmap_write(sensor->hw->regmap,
			   st_sths34pf80_odr_table.reg.addr, odr);
	if (err < 0)
		goto out_err;

	sensor->threshold.val = val;

out_err:
	mutex_unlock(&hw->page_lock);

	return 0;
}

static ssize_t st_sths34pf80_threshold_get(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct st_sths34pf80_sensor *sensor = iio_priv(dev_to_iio_dev(dev));

	return sprintf(buf, "%d\n", sensor->threshold.val);
}

static ssize_t st_sths34pf80_threshold_set(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_sths34pf80_sensor *sensor = iio_priv(iio_dev);
	int err, val;

	err = iio_device_claim_direct_mode(iio_dev);
	if (err)
		return err;

	err = kstrtoint(buf, 10, &val);
	if (err < 0)
		goto out;

	err = st_sths34pf80_update_threshold(sensor, val);

out:
	iio_device_release_direct_mode(iio_dev);

	return err < 0 ? err : size;
}

static int st_sths34pf80_update_hysteresis(struct st_sths34pf80_sensor *sensor,
					   u8 val)
{
	struct st_sths34pf80_hw *hw = sensor->hw;
	int err, odr;

	mutex_lock(&hw->page_lock);

	/* disable odr */
	err = regmap_bulk_read(hw->regmap,
			       st_sths34pf80_odr_table.reg.addr,
			       &odr, 1);
	if (err)
		goto out_err;

	err = __st_sths34pf80_write_with_mask(hw,
				st_sths34pf80_odr_table.reg.addr,
				st_sths34pf80_odr_table.reg.mask, 0);
	if (err)
		goto out_err;

	err = st_sths34pf80_write_cfg(hw, sensor->hysteresis.reg,
				      &val, sizeof(val));
	if (err)
		goto out_err;

	err = st_sths34pf80_reset_algos(hw);
	if (err)
		goto out_err;

	/* restore odr */
	err = regmap_write(sensor->hw->regmap,
			   st_sths34pf80_odr_table.reg.addr, odr);
	if (err < 0)
		goto out_err;

	sensor->hysteresis.val = val;

out_err:
	mutex_unlock(&hw->page_lock);

	return 0;
}

static ssize_t st_sths34pf80_hysteresis_get(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct st_sths34pf80_sensor *sensor = iio_priv(dev_to_iio_dev(dev));

	return sprintf(buf, "%d\n", sensor->hysteresis.val);
}

static ssize_t st_sths34pf80_hysteresis_set(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_sths34pf80_sensor *sensor = iio_priv(iio_dev);
	int err, val;

	err = iio_device_claim_direct_mode(iio_dev);
	if (err)
		return err;

	err = kstrtoint(buf, 10, &val);
	if (err < 0)
		goto out;

	err = st_sths34pf80_update_hysteresis(sensor, val);

out:
	iio_device_release_direct_mode(iio_dev);

	return err < 0 ? err : size;
}

static IIO_DEV_ATTR_SAMP_FREQ_AVAIL(st_sths34pf80_sysfs_sampling_freq_avail);

#ifdef ST_STHS34PF80_TCOMP
static ssize_t
st_sths34pf80_tobject_tcomp_get(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct st_sths34pf80_sensor *sensor =
					 iio_priv(dev_to_iio_dev(dev));

	return sprintf(buf, "%d\n", sensor->hw->tcomp ? 1 : 0);
}

static ssize_t
st_sths34pf80_tobject_tcomp_set(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_sths34pf80_sensor *sensor = iio_priv(iio_dev);
	int err, val;

	err = iio_device_claim_direct_mode(iio_dev);
	if (err)
		return err;

	err = kstrtoint(buf, 10, &val);
	if (err < 0)
		goto out;

	err = st_sths34pf80_enable_comp(sensor->hw, val);
	if (err < 0)
		goto out;

out:
	iio_device_release_direct_mode(iio_dev);

	return err < 0 ? err : size;
}

static int st_sths34pf80_read_sensitivity(struct st_sths34pf80_hw *hw)
{
	u8 sensitivity;
	int err;

	err = st_sths34pf80_read_locked(hw,
			  ST_STHS34PF80_SENSITIVITY_DATA_ADDR,
			  &sensitivity,
			  sizeof(sensitivity));
	if (err)
		return err;

	hw->sensitivity = sensitivity;

	return 0;
}

static int st_sths34pf80_update_sensitivity(struct st_sths34pf80_sensor *sensor,
					    u8 val)
{
	struct st_sths34pf80_hw *hw = sensor->hw;
	int err;

	mutex_lock(&hw->page_lock);

	err = regmap_write(hw->regmap,
			   ST_STHS34PF80_SENSITIVITY_DATA_ADDR,
			   val);
	if (err)
		goto out_err;

	hw->sensitivity = val;

out_err:
	mutex_unlock(&hw->page_lock);

	return 0;
}

static ssize_t st_sths34pf80_sensitivity_get(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct st_sths34pf80_sensor *sensor = iio_priv(dev_to_iio_dev(dev));
	int err;

	err = st_sths34pf80_read_sensitivity(sensor->hw);
	if (err)
		return err;

	return sprintf(buf, "%d\n", sensor->hw->sensitivity);
}

static ssize_t st_sths34pf80_sensitivity_set(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_sths34pf80_sensor *sensor = iio_priv(iio_dev);
	int err, val;

	err = iio_device_claim_direct_mode(iio_dev);
	if (err)
		return err;

	err = kstrtoint(buf, 10, &val);
	if (err < 0)
		goto out;

	err = st_sths34pf80_update_sensitivity(sensor, val);

out:
	iio_device_release_direct_mode(iio_dev);

	return err < 0 ? err : size;
}

static IIO_DEVICE_ATTR(tcomp, 0644,
		       st_sths34pf80_tobject_tcomp_get,
		       st_sths34pf80_tobject_tcomp_set, 0);
static IIO_DEVICE_ATTR(sensitivity, 0644,
		       st_sths34pf80_sensitivity_get,
		       st_sths34pf80_sensitivity_set, 0);

static struct attribute *st_sths34pf80_tobject_comp_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_tcomp.dev_attr.attr,
	&iio_dev_attr_sensitivity.dev_attr.attr,
	NULL,
};

static const struct
attribute_group st_sths34pf80_tobject_comp_attribute_group = {
	.attrs = st_sths34pf80_tobject_comp_attributes,
};

static const struct iio_info st_sths34pf80_tobject_comp_info = {
	.attrs = &st_sths34pf80_tobject_comp_attribute_group,
	.read_raw = st_sths34pf80_read_raw,
	.write_raw = st_sths34pf80_write_raw,
};
#endif /* ST_STHS34PF80_TCOMP */

static ssize_t st_sths34pf80_algo_reset(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_sths34pf80_sensor *sensor = iio_priv(iio_dev);
	struct st_sths34pf80_hw *hw = sensor->hw;
	int err;

	err = iio_device_claim_direct_mode(iio_dev);
	if (err)
		return err;

	mutex_lock(&hw->page_lock);
	err = st_sths34pf80_reset_algos(hw);
	mutex_unlock(&hw->page_lock);
	iio_device_release_direct_mode(iio_dev);

	return err ? err : size;
}

static IIO_DEVICE_ATTR(algo_reset, 0200, NULL,
		       st_sths34pf80_algo_reset, 0);

static struct attribute *st_sths34pf80_tobj_amb_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	NULL,
};

static const struct
attribute_group st_sths34pf80_tobj_amb_attribute_group = {
	.attrs = st_sths34pf80_tobj_amb_attributes,
};

static const struct iio_info st_sths34pf80_tobj_amb_info = {
	.attrs = &st_sths34pf80_tobj_amb_attribute_group,
	.read_raw = st_sths34pf80_read_raw,
	.write_raw = st_sths34pf80_write_raw,
#ifdef CONFIG_DEBUG_FS
	.debugfs_reg_access = &st_sths34pf80_reg_access,
#endif /* CONFIG_DEBUG_FS */
};

static IIO_DEVICE_ATTR(lpf, 0644,
		       st_sths34pf80_lpf_get,
		       st_sths34pf80_lpf_set, 0);
static IIO_DEVICE_ATTR(threshold, 0644,
		       st_sths34pf80_threshold_get,
		       st_sths34pf80_threshold_set, 0);
static IIO_DEVICE_ATTR(hysteresis, 0644,
		       st_sths34pf80_hysteresis_get,
		       st_sths34pf80_hysteresis_set, 0);

static struct attribute *st_sths34pf80_tpresence_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_lpf.dev_attr.attr,
	&iio_dev_attr_threshold.dev_attr.attr,
	&iio_dev_attr_hysteresis.dev_attr.attr,
	&iio_dev_attr_algo_reset.dev_attr.attr,
	&iio_dev_attr_sensitivity.dev_attr.attr,
	NULL,
};

static const struct
attribute_group st_sths34pf80_tpresence_attribute_group = {
	.attrs = st_sths34pf80_tpresence_attributes,
};

static const struct iio_info st_sths34pf80_tpresence_info = {
	.attrs = &st_sths34pf80_tpresence_attribute_group,
	.read_raw = st_sths34pf80_read_raw,
	.write_raw = st_sths34pf80_write_raw,
	.read_event_config = st_sths34pf80_read_event_config,
	.write_event_config = st_sths34pf80_write_event_config,
#ifdef CONFIG_DEBUG_FS
	.debugfs_reg_access = &st_sths34pf80_reg_access,
#endif /* CONFIG_DEBUG_FS */
};

static struct attribute *st_sths34pf80_tmotion_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_lpf.dev_attr.attr,
	&iio_dev_attr_threshold.dev_attr.attr,
	&iio_dev_attr_hysteresis.dev_attr.attr,
	&iio_dev_attr_algo_reset.dev_attr.attr,
	NULL,
};

static const struct
attribute_group st_sths34pf80_tmotion_attribute_group = {
	.attrs = st_sths34pf80_tmotion_attributes,
};

static const struct iio_info st_sths34pf80_tmotion_info = {
	.attrs = &st_sths34pf80_tmotion_attribute_group,
	.read_raw = st_sths34pf80_read_raw,
	.write_raw = st_sths34pf80_write_raw,
	.read_event_config = st_sths34pf80_read_event_config,
	.write_event_config = st_sths34pf80_write_event_config,
};

static struct attribute *st_sths34pf80_tambshock_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_lpf.dev_attr.attr,
	&iio_dev_attr_threshold.dev_attr.attr,
	&iio_dev_attr_algo_reset.dev_attr.attr,
	NULL,
};

static const struct
attribute_group st_sths34pf80_tambshock_attribute_group = {
	.attrs = st_sths34pf80_tambshock_attributes,
};

static const struct iio_info st_sths34pf80_tambshock_info = {
	.attrs = &st_sths34pf80_tambshock_attribute_group,
	.read_raw = st_sths34pf80_read_raw,
	.write_raw = st_sths34pf80_write_raw,
	.read_event_config = st_sths34pf80_read_event_config,
	.write_event_config = st_sths34pf80_write_event_config,
};

static const unsigned long st_sths34pf80_tobject_com_available_scan_masks[] = {
	BIT(1), BIT(0)
};

static const unsigned long st_sths34pf80_tobj_amb_available_scan_masks[] = {
	GENMASK(1, 0), BIT(0)
};

static const unsigned long st_sths34pf80_available_algo_scan_masks[] = {
	GENMASK(1, 0), BIT(0)
};

static int st_sths34pf80_init_device(struct st_sths34pf80_hw *hw)
{
	int err;

	/* enable Block Data Update */
	err = regmap_update_bits(hw->regmap, ST_STHS34PF80_CTRL1_ADDR,
				 ST_STHS34PF80_BDU_MASK,
				 FIELD_PREP(ST_STHS34PF80_BDU_MASK, 1));
	if (err < 0)
		return err;

#ifdef ST_STHS34PF80_TCOMP
	err = st_sths34pf80_enable_comp(hw, true);
	if (err < 0)
		return err;
#endif /* ST_STHS34PF80_TCOMP */

	err = st_sths34pf80_int_config(hw);
	if (err < 0) {
		dev_info(hw->dev, "unable to configure interrupt line (%d)\n",
			 err);

		return err;
	}

	return 0;
}

static struct iio_dev *
st_sths34pf80_alloc_iiodev(struct st_sths34pf80_hw *hw,
			   enum st_sths34pf80_sensor_id id)
{
	struct st_sths34pf80_sensor *sensor;
	struct iio_dev *iio_dev;

	iio_dev = devm_iio_device_alloc(hw->dev, sizeof(*sensor));
	if (!iio_dev)
		return NULL;

	iio_dev->modes = INDIO_DIRECT_MODE;
	iio_dev->dev.parent = hw->dev;

	sensor = iio_priv(iio_dev);
	sensor->id = id;
	sensor->hw = hw;

	switch (id) {
	case ST_STHS34PF80_ID_TAMB_OBJ:
		iio_dev->channels = st_sths34pf80_tobj_amb_channels;
		iio_dev->num_channels =
			     ARRAY_SIZE(st_sths34pf80_tobj_amb_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			  "sths34pf80_tobjamb");
		iio_dev->info = &st_sths34pf80_tobj_amb_info;
		iio_dev->available_scan_masks =
				    st_sths34pf80_tobj_amb_available_scan_masks;
		sensor->odr = st_sths34pf80_odr_table.odr_avl[1].hz;
		sensor->uodr = st_sths34pf80_odr_table.odr_avl[1].uhz;
		break;
#ifdef ST_STHS34PF80_TCOMP
	case ST_STHS34PF80_ID_TOBJECT_COMP:
		iio_dev->channels = st_sths34pf80_tobject_comp_channels;
		iio_dev->num_channels =
			ARRAY_SIZE(st_sths34pf80_tobject_comp_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			  "sths34pf80_tobject_comp");
		iio_dev->info = &st_sths34pf80_tobject_comp_info;
		iio_dev->available_scan_masks =
				 st_sths34pf80_tobject_com_available_scan_masks;
		sensor->odr = st_sths34pf80_odr_table.odr_avl[1].hz;
		sensor->uodr =
			     st_sths34pf80_odr_table.odr_avl[1].uhz;
		break;
#endif /* ST_STHS34PF80_TCOMP */
	case ST_STHS34PF80_ID_TAMB_SHOCK:
		iio_dev->channels = st_sths34pf80_tambshock_channels;
		iio_dev->num_channels =
			   ARRAY_SIZE(st_sths34pf80_tambshock_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			  "sths34pf80_tambshock");
		iio_dev->info = &st_sths34pf80_tambshock_info;
		iio_dev->available_scan_masks =
				st_sths34pf80_available_algo_scan_masks;
		sensor->odr = st_sths34pf80_odr_table.odr_avl[1].hz;
		sensor->uodr = st_sths34pf80_odr_table.odr_avl[1].uhz;
		sensor->lpf.reg = ST_STHS34PF80_LPF2_ADDR;
		sensor->lpf.mask = ST_STHS34PF80_LPF_A_T_MASK;
		st_sths34pf80_update_lpf(sensor,
					 ST_STHS34PF80_LPF_A_T_DEFAULT);
		sensor->threshold.reg = ST_STHS34PF80_TAMB_SHOCK_THS_ADDR;
		st_sths34pf80_update_threshold(sensor,
					 ST_STHS34PF80_TAMB_SHOCK_THS_DEFAULT);
		break;
	case ST_STHS34PF80_ID_TMOTION:
		iio_dev->channels = st_sths34pf80_tmotion_channels;
		iio_dev->num_channels =
			     ARRAY_SIZE(st_sths34pf80_tmotion_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			  "sths34pf80_tmotion");
		iio_dev->info = &st_sths34pf80_tmotion_info;
		iio_dev->available_scan_masks =
				st_sths34pf80_available_algo_scan_masks;
		sensor->odr = st_sths34pf80_odr_table.odr_avl[1].hz;
		sensor->uodr = st_sths34pf80_odr_table.odr_avl[1].uhz;
		sensor->lpf.reg = ST_STHS34PF80_LPF1_ADDR;
		sensor->lpf.mask = ST_STHS34PF80_LPF_M_MASK;
		st_sths34pf80_update_lpf(sensor,
					 ST_STHS34PF80_LPF_M_DEFAULT);
		sensor->threshold.reg = ST_STHS34PF80_MOTION_THS_ADDR;
		st_sths34pf80_update_threshold(sensor,
					 ST_STHS34PF80_MOTION_THS_DEFAULT);
		sensor->hysteresis.reg = ST_STHS34PF80_HYST_MOTION_ADDR;
		st_sths34pf80_update_hysteresis(sensor,
					 ST_STHS34PF80_HYST_MOTION_DEFAULT);
		break;
	case ST_STHS34PF80_ID_TPRESENCE:
		iio_dev->channels = st_sths34pf80_tpresence_channels;
		iio_dev->num_channels =
			   ARRAY_SIZE(st_sths34pf80_tpresence_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			  "sths34pf80_tpresence");
		iio_dev->info = &st_sths34pf80_tpresence_info;
		iio_dev->available_scan_masks =
				st_sths34pf80_available_algo_scan_masks;
		sensor->odr = st_sths34pf80_odr_table.odr_avl[1].hz;
		sensor->uodr = st_sths34pf80_odr_table.odr_avl[1].uhz;
		sensor->lpf.reg = ST_STHS34PF80_LPF2_ADDR;
		sensor->lpf.mask = ST_STHS34PF80_LPF_P_MASK;
		st_sths34pf80_update_lpf(sensor,
					 ST_STHS34PF80_LPF_P_DEFAULT);
		sensor->threshold.reg = ST_STHS34PF80_PRESENCE_THS_ADDR;
		st_sths34pf80_update_threshold(sensor,
					 ST_STHS34PF80_PRESENCE_THS_DEFAULT);
		sensor->hysteresis.reg = ST_STHS34PF80_HYST_PRESENCE_ADDR;
		st_sths34pf80_update_hysteresis(sensor,
					 ST_STHS34PF80_HYST_PRESENCE_DEFAULT);
		break;
	default:
		return NULL;
	}

	iio_dev->name = sensor->name;

	return iio_dev;
}

static void st_sths34pf80_disable_regulator_action(void *_data)
{
	struct st_sths34pf80_hw *hw = _data;

	regulator_disable(hw->vddio_supply);
	regulator_disable(hw->vdd_supply);
}

static int st_sths34pf80_power_enable(struct st_sths34pf80_hw *hw)
{
	int err;

	hw->vdd_supply = devm_regulator_get(hw->dev, "vdd");
	if (IS_ERR(hw->vdd_supply)) {
		if (PTR_ERR(hw->vdd_supply) != -EPROBE_DEFER)
			dev_err(hw->dev,
				"Failed to get vdd regulator %d\n",
				(int)PTR_ERR(hw->vdd_supply));

		return PTR_ERR(hw->vdd_supply);
	}

	hw->vddio_supply = devm_regulator_get(hw->dev, "vddio");
	if (IS_ERR(hw->vddio_supply)) {
		if (PTR_ERR(hw->vddio_supply) != -EPROBE_DEFER)
			dev_err(hw->dev,
				"Failed to get vddio regulator %d\n",
				(int)PTR_ERR(hw->vddio_supply));

		return PTR_ERR(hw->vddio_supply);
	}

	err = regulator_enable(hw->vdd_supply);
	if (err) {
		dev_err(hw->dev,
			"Failed to enable vdd regulator: %d\n", err);

		return err;
	}

	err = regulator_enable(hw->vddio_supply);
	if (err) {
		regulator_disable(hw->vdd_supply);

		return err;
	}

	err = devm_add_action_or_reset(hw->dev,
			st_sths34pf80_disable_regulator_action, hw);
	if (err) {
		dev_err(hw->dev,
			"Failed to setup regulator cleanup action %d\n",
			err);

		return err;
	}

	return 0;
}

static int st_sths34pf80_preenable(struct iio_dev *iio_dev)
{
	struct st_sths34pf80_sensor *sensor = iio_priv(iio_dev);

	return st_sths34pf80_sensor_set_enable(sensor, true);
}

static int st_sths34pf80_postdisable(struct iio_dev *iio_dev)
{
	struct st_sths34pf80_sensor *sensor = iio_priv(iio_dev);

	return st_sths34pf80_sensor_set_enable(sensor, false);
}

static const struct iio_buffer_setup_ops st_sths34pf80_fifo_ops = {
	.preenable = st_sths34pf80_preenable,
	.postdisable = st_sths34pf80_postdisable,
};

/**
 * Probe device function
 *
 * @param  dev: Device pointer.
 * @param  irq: I2C/SPI/I3C client irq.
 * @param  regmap: Bus Transfer Function pointer.
 * @retval 0 if OK, < 0 for error
 */
int st_sths34pf80_probe(struct device *dev, int irq,
			struct regmap *regmap)
{
	struct st_sths34pf80_hw *hw;
	int i, err;

	hw = devm_kzalloc(dev, sizeof(*hw), GFP_KERNEL);
	if (!hw)
		return -ENOMEM;

	dev_set_drvdata(dev, (void *)hw);

	mutex_init(&hw->page_lock);
	mutex_init(&hw->int_lock);

	hw->regmap = regmap;
	hw->dev = dev;
	hw->irq = irq;

	err = st_sths34pf80_power_enable(hw);
	if (err != 0)
		return err;

	/* wait sths34pf80 power up after enabling regulator */
	usleep_range(2500, 2600);

	err = st_sths34pf80_check_whoami(hw);
	if (err < 0)
		return err;

	err = st_sths34pf80_init_device(hw);
	if (err < 0)
		return err;

	for (i = 0; i < ST_STHS34PF80_ID_MAX; i++) {

#if KERNEL_VERSION(5, 13, 0) > LINUX_VERSION_CODE
		struct iio_buffer *buffer;
#endif /* LINUX_VERSION_CODE */

		hw->iio_devs[i] = st_sths34pf80_alloc_iiodev(hw, i);
		if (!hw->iio_devs[i])
			return -ENOMEM;

#if KERNEL_VERSION(5, 19, 0) <= LINUX_VERSION_CODE
		err = devm_iio_kfifo_buffer_setup(hw->dev, hw->iio_devs[i],
						  &st_sths34pf80_fifo_ops);
		if (err)
			return err;
#elif KERNEL_VERSION(5, 13, 0) <= LINUX_VERSION_CODE
		err = devm_iio_kfifo_buffer_setup(hw->dev,
						  hw->iio_devs[i],
						  INDIO_BUFFER_SOFTWARE,
						  &st_sths34pf80_fifo_ops);
		if (err)
			return err;
#else /* LINUX_VERSION_CODE */
		buffer = devm_iio_kfifo_allocate(hw->dev);
		if (!buffer)
			return -ENOMEM;

		iio_device_attach_buffer(hw->iio_devs[i], buffer);
		hw->iio_devs[i]->modes |= INDIO_BUFFER_SOFTWARE;
		hw->iio_devs[i]->setup_ops = &st_sths34pf80_fifo_ops;
#endif /* LINUX_VERSION_CODE */

		err = devm_iio_device_register(hw->dev,
					       hw->iio_devs[i]);
		if (err)
			return err;
	}

	dev_info(dev, "device probed\n");

	return 0;
}
EXPORT_SYMBOL(st_sths34pf80_probe);

int st_sths34pf80_remove(struct device *dev)
{
	struct st_sths34pf80_hw *hw = dev_get_drvdata(dev);
	struct st_sths34pf80_sensor *sensor;
	int i;

	for (i = 0; i < ST_STHS34PF80_ID_MAX; i++) {
		if (!hw->iio_devs[i])
			continue;

		sensor = iio_priv(hw->iio_devs[i]);
		st_sths34pf80_sensor_set_enable(sensor, false);
		st_sths34pf80_event_sensor_enable(sensor, false);
	}

	return 0;
}
EXPORT_SYMBOL(st_sths34pf80_remove);

static int __maybe_unused st_sths34pf80_suspend(struct device *dev)
{
	struct st_sths34pf80_hw *hw = dev_get_drvdata(dev);
	struct st_sths34pf80_sensor *sensor;
	int i;

	for (i = 0; i < ST_STHS34PF80_ID_MAX; i++) {
		int err;

		sensor = iio_priv(hw->iio_devs[i]);
		if (!hw->iio_devs[i])
			continue;

		if (!(hw->enable_mask & BIT(sensor->id)))
			continue;

		err = st_sths34pf80_set_odr(sensor, 0, 0);
		if (err < 0)
			return err;
	}

	return 0;
}

static int __maybe_unused st_sths34pf80_resume(struct device *dev)
{
	struct st_sths34pf80_hw *hw = dev_get_drvdata(dev);
	struct st_sths34pf80_sensor *sensor;
	int i;

	for (i = 0; i < ST_STHS34PF80_ID_MAX; i++) {
		int err;

		sensor = iio_priv(hw->iio_devs[i]);
		if (!hw->iio_devs[i])
			continue;

		if (!(hw->enable_mask & BIT(sensor->id)))
			continue;

		err = st_sths34pf80_set_odr(sensor, sensor->odr,
					    sensor->uodr);
		if (err < 0)
			return err;
	}

	return 0;
}

#ifdef CONFIG_PM_SLEEP
const struct dev_pm_ops st_sths34pf80_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(st_sths34pf80_suspend,
				st_sths34pf80_resume)
};
EXPORT_SYMBOL(st_sths34pf80_pm_ops);
#endif /* CONFIG_PM_SLEEP */

MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_DESCRIPTION("STMicroelectronics st_sths34pf80 driver");
MODULE_LICENSE("GPL v2");
