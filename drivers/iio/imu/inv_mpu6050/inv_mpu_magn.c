// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 TDK-InvenSense, Inc.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/string.h>

#include "inv_mpu_aux.h"
#include "inv_mpu_iio.h"
#include "inv_mpu_magn.h"

/*
 * MPU9xxx magnetometer are AKM chips on I2C aux bus
 * MPU9150 is AK8975
 * MPU9250 is AK8963
 */
#define INV_MPU_MAGN_I2C_ADDR		0x0C

#define INV_MPU_MAGN_REG_WIA		0x00
#define INV_MPU_MAGN_BITS_WIA		0x48

#define INV_MPU_MAGN_REG_ST1		0x02
#define INV_MPU_MAGN_BIT_DRDY		0x01
#define INV_MPU_MAGN_BIT_DOR		0x02

#define INV_MPU_MAGN_REG_DATA		0x03

#define INV_MPU_MAGN_REG_ST2		0x09
#define INV_MPU_MAGN_BIT_HOFL		0x08
#define INV_MPU_MAGN_BIT_BITM		0x10

#define INV_MPU_MAGN_REG_CNTL1		0x0A
#define INV_MPU_MAGN_BITS_MODE_PWDN	0x00
#define INV_MPU_MAGN_BITS_MODE_SINGLE	0x01
#define INV_MPU_MAGN_BITS_MODE_FUSE	0x0F
#define INV_MPU9250_MAGN_BIT_OUTPUT_BIT	0x10

#define INV_MPU9250_MAGN_REG_CNTL2	0x0B
#define INV_MPU9250_MAGN_BIT_SRST	0x01

#define INV_MPU_MAGN_REG_ASAX		0x10
#define INV_MPU_MAGN_REG_ASAY		0x11
#define INV_MPU_MAGN_REG_ASAZ		0x12

/* Magnetometer maximum frequency */
#define INV_MPU_MAGN_FREQ_HZ_MAX	50

static bool inv_magn_supported(const struct inv_mpu6050_state *st)
{
	switch (st->chip_type) {
	case INV_MPU9150:
	case INV_MPU9250:
	case INV_MPU9255:
		return true;
	default:
		return false;
	}
}

/* init magnetometer chip */
static int inv_magn_init(struct inv_mpu6050_state *st)
{
	uint8_t val;
	uint8_t asa[3];
	int32_t sensitivity;
	int ret;

	/* check whoami */
	ret = inv_mpu_aux_read(st, INV_MPU_MAGN_I2C_ADDR, INV_MPU_MAGN_REG_WIA,
			       &val, sizeof(val));
	if (ret)
		return ret;
	if (val != INV_MPU_MAGN_BITS_WIA)
		return -ENODEV;

	/* software reset for MPU925x only */
	switch (st->chip_type) {
	case INV_MPU9250:
	case INV_MPU9255:
		ret = inv_mpu_aux_write(st, INV_MPU_MAGN_I2C_ADDR,
					INV_MPU9250_MAGN_REG_CNTL2,
					INV_MPU9250_MAGN_BIT_SRST);
		if (ret)
			return ret;
		break;
	default:
		break;
	}

	/* read fuse ROM data */
	ret = inv_mpu_aux_write(st, INV_MPU_MAGN_I2C_ADDR,
				INV_MPU_MAGN_REG_CNTL1,
				INV_MPU_MAGN_BITS_MODE_FUSE);
	if (ret)
		return ret;

	ret = inv_mpu_aux_read(st, INV_MPU_MAGN_I2C_ADDR, INV_MPU_MAGN_REG_ASAX,
			       asa, sizeof(asa));
	if (ret)
		return ret;

	/* switch back to power-down */
	ret = inv_mpu_aux_write(st, INV_MPU_MAGN_I2C_ADDR,
				INV_MPU_MAGN_REG_CNTL1,
				INV_MPU_MAGN_BITS_MODE_PWDN);
	if (ret)
		return ret;

	/*
	 * Sensor sentivity
	 * 1 uT = 0.01 G and value is in micron (1e6)
	 * sensitvity = x uT * 0.01 * 1e6
	 */
	switch (st->chip_type) {
	case INV_MPU9150:
		/* sensor sensitivity is 0.3 uT */
		sensitivity = 3000;
		break;
	case INV_MPU9250:
	case INV_MPU9255:
		/* sensor sensitivity in 16 bits mode: 0.15 uT */
		sensitivity = 1500;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * Sensitivity adjustement and scale to Gauss
	 *
	 * Hadj = H * (((ASA - 128) * 0.5 / 128) + 1)
	 * Factor simplification:
	 * Hadj = H * ((ASA + 128) / 256)
	 *
	 * raw_to_gauss = Hadj * sensitivity
	 */
	st->magn_raw_to_gauss[0] = (((int32_t)asa[0] + 128) * sensitivity) / 256;
	st->magn_raw_to_gauss[1] = (((int32_t)asa[1] + 128) * sensitivity) / 256;
	st->magn_raw_to_gauss[2] = (((int32_t)asa[2] + 128) * sensitivity) / 256;

	return 0;
}

/**
 * inv_mpu_magn_probe() - probe and setup magnetometer chip
 * @st: driver internal state
 *
 * Returns 0 on success, a negative error code otherwise
 *
 * It is probing the chip and setting up all needed i2c transfers.
 * Noop if there is no magnetometer in the chip.
 */
int inv_mpu_magn_probe(struct inv_mpu6050_state *st)
{
	uint8_t val;
	int ret;

	/* quit if chip is not supported */
	if (!inv_magn_supported(st))
		return 0;

	/* configure i2c master aux port */
	ret = inv_mpu_aux_init(st);
	if (ret)
		return ret;

	/* check and init mag chip */
	ret = inv_magn_init(st);
	if (ret)
		return ret;

	/*
	 * configure mpu i2c master accesses
	 * i2c SLV0: read sensor data, 7 bytes data(6)-ST2
	 * Byte swap data to store them in big-endian in impair address groups
	 */
	ret = regmap_write(st->map, INV_MPU6050_REG_I2C_SLV_ADDR(0),
			   INV_MPU6050_BIT_I2C_SLV_RNW | INV_MPU_MAGN_I2C_ADDR);
	if (ret)
		return ret;

	ret = regmap_write(st->map, INV_MPU6050_REG_I2C_SLV_REG(0),
			   INV_MPU_MAGN_REG_DATA);
	if (ret)
		return ret;

	ret = regmap_write(st->map, INV_MPU6050_REG_I2C_SLV_CTRL(0),
			   INV_MPU6050_BIT_SLV_EN |
			   INV_MPU6050_BIT_SLV_BYTE_SW |
			   INV_MPU6050_BIT_SLV_GRP |
			   INV_MPU9X50_BYTES_MAGN);
	if (ret)
		return ret;

	/* i2c SLV1: launch single measurement */
	ret = regmap_write(st->map, INV_MPU6050_REG_I2C_SLV_ADDR(1),
			   INV_MPU_MAGN_I2C_ADDR);
	if (ret)
		return ret;

	ret = regmap_write(st->map, INV_MPU6050_REG_I2C_SLV_REG(1),
			   INV_MPU_MAGN_REG_CNTL1);
	if (ret)
		return ret;

	/* add 16 bits mode for MPU925x */
	val = INV_MPU_MAGN_BITS_MODE_SINGLE;
	switch (st->chip_type) {
	case INV_MPU9250:
	case INV_MPU9255:
		val |= INV_MPU9250_MAGN_BIT_OUTPUT_BIT;
		break;
	default:
		break;
	}
	ret = regmap_write(st->map, INV_MPU6050_REG_I2C_SLV_DO(1), val);
	if (ret)
		return ret;

	return regmap_write(st->map, INV_MPU6050_REG_I2C_SLV_CTRL(1),
			    INV_MPU6050_BIT_SLV_EN | 1);
}

/**
 * inv_mpu_magn_set_rate() - set magnetometer sampling rate
 * @st: driver internal state
 * @fifo_rate: mpu set fifo rate
 *
 * Returns 0 on success, a negative error code otherwise
 *
 * Limit sampling frequency to the maximum value supported by the
 * magnetometer chip. Resulting in duplicated data for higher frequencies.
 * Noop if there is no magnetometer in the chip.
 */
int inv_mpu_magn_set_rate(const struct inv_mpu6050_state *st, int fifo_rate)
{
	uint8_t d;

	/* quit if chip is not supported */
	if (!inv_magn_supported(st))
		return 0;

	/*
	 * update i2c master delay to limit mag sampling to max frequency
	 * compute fifo_rate divider d: rate = fifo_rate / (d + 1)
	 */
	if (fifo_rate > INV_MPU_MAGN_FREQ_HZ_MAX)
		d = fifo_rate / INV_MPU_MAGN_FREQ_HZ_MAX - 1;
	else
		d = 0;

	return regmap_write(st->map, INV_MPU6050_REG_I2C_SLV4_CTRL, d);
}

/**
 * inv_mpu_magn_set_orient() - fill magnetometer mounting matrix
 * @st: driver internal state
 *
 * Returns 0 on success, a negative error code otherwise
 *
 * Fill magnetometer mounting matrix using the provided chip matrix.
 */
int inv_mpu_magn_set_orient(struct inv_mpu6050_state *st)
{
	const char *orient;
	char *str;
	int i;

	/* fill magnetometer orientation */
	switch (st->chip_type) {
	case INV_MPU9150:
	case INV_MPU9250:
	case INV_MPU9255:
		/* x <- y */
		st->magn_orient.rotation[0] = st->orientation.rotation[3];
		st->magn_orient.rotation[1] = st->orientation.rotation[4];
		st->magn_orient.rotation[2] = st->orientation.rotation[5];
		/* y <- x */
		st->magn_orient.rotation[3] = st->orientation.rotation[0];
		st->magn_orient.rotation[4] = st->orientation.rotation[1];
		st->magn_orient.rotation[5] = st->orientation.rotation[2];
		/* z <- -z */
		for (i = 0; i < 3; ++i) {
			orient = st->orientation.rotation[6 + i];
			/* use length + 2 for adding minus sign if needed */
			str = devm_kzalloc(regmap_get_device(st->map),
					   strlen(orient) + 2, GFP_KERNEL);
			if (str == NULL)
				return -ENOMEM;
			if (strcmp(orient, "0") == 0) {
				strcpy(str, orient);
			} else if (orient[0] == '-') {
				strcpy(str, &orient[1]);
			} else {
				str[0] = '-';
				strcpy(&str[1], orient);
			}
			st->magn_orient.rotation[6 + i] = str;
		}
		break;
	default:
		st->magn_orient = st->orientation;
		break;
	}

	return 0;
}

/**
 * inv_mpu_magn_read() - read magnetometer data
 * @st: driver internal state
 * @axis: IIO modifier axis value
 * @val: store corresponding axis value
 *
 * Returns 0 on success, a negative error code otherwise
 */
int inv_mpu_magn_read(const struct inv_mpu6050_state *st, int axis, int *val)
{
	unsigned int user_ctrl, status;
	__be16 data[3];
	uint8_t addr;
	uint8_t d;
	unsigned int period_ms;
	int ret;

	/* quit if chip is not supported */
	if (!inv_magn_supported(st))
		return -ENODEV;

	/* Mag data: X - Y - Z */
	switch (axis) {
	case IIO_MOD_X:
		addr = 0;
		break;
	case IIO_MOD_Y:
		addr = 1;
		break;
	case IIO_MOD_Z:
		addr = 2;
		break;
	default:
		return -EINVAL;
	}

	/* set sample rate to max mag freq */
	d = INV_MPU6050_FIFO_RATE_TO_DIVIDER(INV_MPU_MAGN_FREQ_HZ_MAX);
	ret = regmap_write(st->map, st->reg->sample_rate_div, d);
	if (ret)
		return ret;

	/* start i2c master, wait for xfer, stop */
	user_ctrl = st->chip_config.user_ctrl | INV_MPU6050_BIT_I2C_MST_EN;
	ret = regmap_write(st->map, st->reg->user_ctrl, user_ctrl);
	if (ret)
		return ret;

	/* need to wait 2 periods + half-period margin */
	period_ms = 1000 / INV_MPU_MAGN_FREQ_HZ_MAX;
	msleep(period_ms * 2 + period_ms / 2);
	user_ctrl = st->chip_config.user_ctrl;
	ret = regmap_write(st->map, st->reg->user_ctrl, user_ctrl);
	if (ret)
		return ret;

	/* restore sample rate */
	d = st->chip_config.divider;
	ret = regmap_write(st->map, st->reg->sample_rate_div, d);
	if (ret)
		return ret;

	/* check i2c status and read raw data */
	ret = regmap_read(st->map, INV_MPU6050_REG_I2C_MST_STATUS, &status);
	if (ret)
		return ret;

	if (status & INV_MPU6050_BIT_I2C_SLV0_NACK ||
			status & INV_MPU6050_BIT_I2C_SLV1_NACK)
		return -EIO;

	ret = regmap_bulk_read(st->map, INV_MPU6050_REG_EXT_SENS_DATA,
			       data, sizeof(data));
	if (ret)
		return ret;

	*val = (int16_t)be16_to_cpu(data[addr]);

	return IIO_VAL_INT;
}
