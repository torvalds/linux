// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 TDK-InvenSense, Inc.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/regmap.h>
#include <linux/delay.h>

#include "inv_mpu_aux.h"
#include "inv_mpu_iio.h"

/*
 * i2c master auxiliary bus transfer function.
 * Requires the i2c operations to be correctly setup before.
 */
static int inv_mpu_i2c_master_xfer(const struct inv_mpu6050_state *st)
{
	/* use 50hz frequency for xfer */
	const unsigned int freq = 50;
	const unsigned int period_ms = 1000 / freq;
	uint8_t d;
	unsigned int user_ctrl;
	int ret;

	/* set sample rate */
	d = INV_MPU6050_FIFO_RATE_TO_DIVIDER(freq);
	ret = regmap_write(st->map, st->reg->sample_rate_div, d);
	if (ret)
		return ret;

	/* start i2c master */
	user_ctrl = st->chip_config.user_ctrl | INV_MPU6050_BIT_I2C_MST_EN;
	ret = regmap_write(st->map, st->reg->user_ctrl, user_ctrl);
	if (ret)
		goto error_restore_rate;

	/* wait for xfer: 1 period + half-period margin */
	msleep(period_ms + period_ms / 2);

	/* stop i2c master */
	user_ctrl = st->chip_config.user_ctrl;
	ret = regmap_write(st->map, st->reg->user_ctrl, user_ctrl);
	if (ret)
		goto error_stop_i2c;

	/* restore sample rate */
	d = st->chip_config.divider;
	ret = regmap_write(st->map, st->reg->sample_rate_div, d);
	if (ret)
		goto error_restore_rate;

	return 0;

error_stop_i2c:
	regmap_write(st->map, st->reg->user_ctrl, st->chip_config.user_ctrl);
error_restore_rate:
	regmap_write(st->map, st->reg->sample_rate_div, st->chip_config.divider);
	return ret;
}

/**
 * inv_mpu_aux_init() - init i2c auxiliary bus
 * @st: driver internal state
 *
 * Returns 0 on success, a negative error code otherwise.
 */
int inv_mpu_aux_init(const struct inv_mpu6050_state *st)
{
	unsigned int val;
	int ret;

	/* configure i2c master */
	val = INV_MPU6050_BITS_I2C_MST_CLK_400KHZ |
			INV_MPU6050_BIT_WAIT_FOR_ES;
	ret = regmap_write(st->map, INV_MPU6050_REG_I2C_MST_CTRL, val);
	if (ret)
		return ret;

	/* configure i2c master delay */
	ret = regmap_write(st->map, INV_MPU6050_REG_I2C_SLV4_CTRL, 0);
	if (ret)
		return ret;

	val = INV_MPU6050_BIT_I2C_SLV0_DLY_EN |
			INV_MPU6050_BIT_I2C_SLV1_DLY_EN |
			INV_MPU6050_BIT_I2C_SLV2_DLY_EN |
			INV_MPU6050_BIT_I2C_SLV3_DLY_EN |
			INV_MPU6050_BIT_DELAY_ES_SHADOW;
	return regmap_write(st->map, INV_MPU6050_REG_I2C_MST_DELAY_CTRL, val);
}

/**
 * inv_mpu_aux_read() - read register function for i2c auxiliary bus
 * @st: driver internal state.
 * @addr: chip i2c Address
 * @reg: chip register address
 * @val: buffer for storing read bytes
 * @size: number of bytes to read
 *
 *  Returns 0 on success, a negative error code otherwise.
 */
int inv_mpu_aux_read(const struct inv_mpu6050_state *st, uint8_t addr,
		     uint8_t reg, uint8_t *val, size_t size)
{
	unsigned int status;
	int ret;

	if (size > 0x0F)
		return -EINVAL;

	/* setup i2c SLV0 control: i2c addr, register, enable + size */
	ret = regmap_write(st->map, INV_MPU6050_REG_I2C_SLV_ADDR(0),
			   INV_MPU6050_BIT_I2C_SLV_RNW | addr);
	if (ret)
		return ret;
	ret = regmap_write(st->map, INV_MPU6050_REG_I2C_SLV_REG(0), reg);
	if (ret)
		return ret;
	ret = regmap_write(st->map, INV_MPU6050_REG_I2C_SLV_CTRL(0),
			   INV_MPU6050_BIT_SLV_EN | size);
	if (ret)
		return ret;

	/* do i2c xfer */
	ret = inv_mpu_i2c_master_xfer(st);
	if (ret)
		goto error_disable_i2c;

	/* disable i2c slave */
	ret = regmap_write(st->map, INV_MPU6050_REG_I2C_SLV_CTRL(0), 0);
	if (ret)
		goto error_disable_i2c;

	/* check i2c status */
	ret = regmap_read(st->map, INV_MPU6050_REG_I2C_MST_STATUS, &status);
	if (ret)
		return ret;
	if (status & INV_MPU6050_BIT_I2C_SLV0_NACK)
		return -EIO;

	/* read data in registers */
	return regmap_bulk_read(st->map, INV_MPU6050_REG_EXT_SENS_DATA,
				val, size);

error_disable_i2c:
	regmap_write(st->map, INV_MPU6050_REG_I2C_SLV_CTRL(0), 0);
	return ret;
}

/**
 * inv_mpu_aux_write() - write register function for i2c auxiliary bus
 * @st: driver internal state.
 * @addr: chip i2c Address
 * @reg: chip register address
 * @val: 1 byte value to write
 *
 *  Returns 0 on success, a negative error code otherwise.
 */
int inv_mpu_aux_write(const struct inv_mpu6050_state *st, uint8_t addr,
		      uint8_t reg, uint8_t val)
{
	unsigned int status;
	int ret;

	/* setup i2c SLV0 control: i2c addr, register, value, enable + size */
	ret = regmap_write(st->map, INV_MPU6050_REG_I2C_SLV_ADDR(0), addr);
	if (ret)
		return ret;
	ret = regmap_write(st->map, INV_MPU6050_REG_I2C_SLV_REG(0), reg);
	if (ret)
		return ret;
	ret = regmap_write(st->map, INV_MPU6050_REG_I2C_SLV_DO(0), val);
	if (ret)
		return ret;
	ret = regmap_write(st->map, INV_MPU6050_REG_I2C_SLV_CTRL(0),
			   INV_MPU6050_BIT_SLV_EN | 1);
	if (ret)
		return ret;

	/* do i2c xfer */
	ret = inv_mpu_i2c_master_xfer(st);
	if (ret)
		goto error_disable_i2c;

	/* disable i2c slave */
	ret = regmap_write(st->map, INV_MPU6050_REG_I2C_SLV_CTRL(0), 0);
	if (ret)
		goto error_disable_i2c;

	/* check i2c status */
	ret = regmap_read(st->map, INV_MPU6050_REG_I2C_MST_STATUS, &status);
	if (ret)
		return ret;
	if (status & INV_MPU6050_BIT_I2C_SLV0_NACK)
		return -EIO;

	return 0;

error_disable_i2c:
	regmap_write(st->map, INV_MPU6050_REG_I2C_SLV_CTRL(0), 0);
	return ret;
}
