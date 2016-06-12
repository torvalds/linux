/*
* Copyright (C) 2012 Invensense, Inc.
*
* This software is licensed under the terms of the GNU General Public
* License version 2, as published by the Free Software Foundation, and
* may be copied, distributed, and modified under those terms.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
*/

/**
 *  @addtogroup  DRIVERS
 *  @brief       Hardware drivers.
 *
 *  @{
 *      @file    inv_mpu3050_iio.c
 *      @brief   A sysfs device driver for Invensense devices
 *      @details This file is part of invensense mpu driver code
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/jiffies.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include <linux/poll.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>

#include "inv_mpu_iio.h"
#define MPU3050_NACK_MIN_TIME (2 * 1000)
#define MPU3050_NACK_MAX_TIME (3 * 1000)

#define MPU3050_ONE_MPU_TIME 20
#define MPU3050_BOGUS_ADDR  0x7F
int __attribute__((weak)) inv_register_mpu3050_slave(struct inv_mpu_iio_s *st)
{
	return 0;
}

int set_3050_bypass(struct inv_mpu_iio_s *st, bool enable)
{
	struct inv_reg_map_s *reg;
	int result;
	u8 b;

	reg = &st->reg;
	result = inv_plat_read(st, reg->user_ctrl, 1, &b);
	if (result)
		return result;
	if (((b & BIT_3050_AUX_IF_EN) == 0) && enable)
		return 0;
	if ((b & BIT_3050_AUX_IF_EN) && (enable == 0))
		return 0;
	b &= ~BIT_3050_AUX_IF_EN;
	if (!enable) {
		b |= BIT_3050_AUX_IF_EN;
		result = inv_plat_single_write(st, reg->user_ctrl, b | st->i2c_dis);
		return result;
	} else {
		/* Coming out of I2C is tricky due to several erratta.  Do not
		* modify this algorithm
		*/
		/*
		* 1) wait for the right time and send the command to change
		* the aux i2c slave address to an invalid address that will
		* get nack'ed
		*
		* 0x00 is broadcast.  0x7F is unlikely to be used by any aux.
		*/
		result = inv_plat_single_write(st, REG_3050_SLAVE_ADDR,
						MPU3050_BOGUS_ADDR);
		if (result)
			return result;
		/*
		* 2) wait enough time for a nack to occur, then go into
		*    bypass mode:
		*/
		usleep_range(MPU3050_NACK_MIN_TIME, MPU3050_NACK_MAX_TIME);
		result = inv_plat_single_write(st, reg->user_ctrl, b | st->i2c_dis);
		if (result)
			return result;
		/*
		* 3) wait for up to one MPU cycle then restore the slave
		*    address
		*/
		msleep(MPU3050_ONE_MPU_TIME);

		result = inv_plat_single_write(st, REG_3050_SLAVE_ADDR,
			st->plat_data.secondary_i2c_addr);
		if (result)
			return result;

		result = inv_plat_single_write(st, reg->user_ctrl,
				(b | BIT_3050_AUX_IF_RST | st->i2c_dis));
		if (result)
			return result;
		usleep_range(MPU3050_NACK_MIN_TIME, MPU3050_NACK_MAX_TIME);
	}
	return 0;
}

void inv_setup_reg_mpu3050(struct inv_reg_map_s *reg)
{
	reg->fifo_en         = REG_3050_FIFO_EN;
	reg->sample_rate_div = REG_3050_SAMPLE_RATE_DIV;
	reg->lpf             = REG_3050_LPF;
	reg->fifo_count_h    = REG_3050_FIFO_COUNT_H;
	reg->fifo_r_w        = REG_3050_FIFO_R_W;
	reg->user_ctrl       = REG_3050_USER_CTRL;
	reg->pwr_mgmt_1      = REG_3050_PWR_MGMT_1;
	reg->raw_gyro        = REG_3050_RAW_GYRO;
	reg->raw_accl        = REG_3050_AUX_XOUT_H;
	reg->temperature     = REG_3050_TEMPERATURE;
	reg->int_enable      = REG_3050_INT_ENABLE;
	reg->int_status      = REG_3050_INT_STATUS;
}

int inv_switch_3050_gyro_engine(struct inv_mpu_iio_s *st, bool en)
{
	struct inv_reg_map_s *reg;
	u8 data, p;
	int result;
	reg = &st->reg;
	if (en) {
		data = INV_CLK_PLL;
		p = (BITS_3050_POWER1 | data);
		result = inv_plat_single_write(st, reg->pwr_mgmt_1, p);
		if (result)
			return result;
		p = (BITS_3050_POWER2 | data);
		result = inv_plat_single_write(st, reg->pwr_mgmt_1, p);
		if (result)
			return result;
		p = data;
		result = inv_plat_single_write(st, reg->pwr_mgmt_1, p);
		msleep(SENSOR_UP_TIME);
	} else {
		p = BITS_3050_GYRO_STANDBY;
		result = inv_plat_single_write(st, reg->pwr_mgmt_1, p);
	}

	return result;
}

int inv_switch_3050_accl_engine(struct inv_mpu_iio_s *st, bool en)
{
	int result;
	if (NULL == st->mpu_slave)
		return -EPERM;
	if (en)
		result = st->mpu_slave->resume(st);
	else
		result = st->mpu_slave->suspend(st);

	return result;
}

/**
 *  inv_init_config_mpu3050() - Initialize hardware, disable FIFO.
 *  @st:	Device driver instance.
 *  Initial configuration:
 *  FSR: +/- 2000DPS
 *  DLPF: 42Hz
 *  FIFO rate: 50Hz
 *  Clock source: Gyro PLL
 */
int inv_init_config_mpu3050(struct iio_dev *indio_dev)
{
	struct inv_reg_map_s *reg;
	int result;
	u8 data;
	struct inv_mpu_iio_s *st = iio_priv(indio_dev);

	if (st->chip_config.is_asleep)
		return -EPERM;
	/*reading AUX VDDIO register */
	result = inv_plat_read(st, REG_3050_AUX_VDDIO, 1, &data);
	if (result)
		return result;
	data &= ~BIT_3050_VDDIO;
	if (st->plat_data.level_shifter)
		data |= BIT_3050_VDDIO;
	result = inv_plat_single_write(st, REG_3050_AUX_VDDIO, data);
	if (result)
		return result;

	reg = &st->reg;
	/*2000dps full scale range*/
	result = inv_plat_single_write(st, reg->lpf,
				(INV_FSR_2000DPS << GYRO_CONFIG_FSR_SHIFT)
				| INV_FILTER_42HZ);
	if (result)
		return result;
	st->chip_config.fsr = INV_FSR_2000DPS;
	st->chip_config.lpf = INV_FILTER_42HZ;
	result = inv_plat_single_write(st, reg->sample_rate_div,
					ONE_K_HZ/INIT_FIFO_RATE - 1);
	if (result)
		return result;
	st->chip_config.fifo_rate = INIT_FIFO_RATE;
	st->chip_config.new_fifo_rate = INIT_FIFO_RATE;
	st->irq_dur_ns            = INIT_DUR_TIME;
	if ((SECONDARY_SLAVE_TYPE_ACCEL == st->plat_data.sec_slave_type) &&
		st->mpu_slave) {
		result = st->mpu_slave->setup(st);
		if (result)
			return result;
		result = st->mpu_slave->set_fs(st, INV_FS_02G);
		if (result)
			return result;
		result = st->mpu_slave->set_lpf(st, INIT_FIFO_RATE);
		if (result)
			return result;
	}

	return 0;
}

/**
 *  set_power_mpu3050() - set power of mpu3050.
 *  @st:	Device driver instance.
 *  @power_on:  on/off
 */
int set_power_mpu3050(struct inv_mpu_iio_s *st, bool power_on)
{
	struct inv_reg_map_s *reg;
	u8 data, p;
	int result;
	reg = &st->reg;
	if (power_on) {
		data = 0;
	} else {
		if (st->mpu_slave) {
			result = st->mpu_slave->suspend(st);
			if (result)
				return result;
		}
		data = BIT_SLEEP;
	}
	if (st->chip_config.gyro_enable) {
		p = (BITS_3050_POWER1 | INV_CLK_PLL);
		result = inv_plat_single_write(st, reg->pwr_mgmt_1, data | p);
		if (result)
			return result;

		p = (BITS_3050_POWER2 | INV_CLK_PLL);
		result = inv_plat_single_write(st, reg->pwr_mgmt_1, data | p);
		if (result)
			return result;

		p = INV_CLK_PLL;
		result = inv_plat_single_write(st, reg->pwr_mgmt_1, data | p);
		if (result)
			return result;
	} else {
		data |= (BITS_3050_GYRO_STANDBY | INV_CLK_INTERNAL);
		result = inv_plat_single_write(st, reg->pwr_mgmt_1, data);
		if (result)
			return result;
	}
	if (power_on) {
		msleep(POWER_UP_TIME);
		if (st->mpu_slave) {
			result = st->mpu_slave->resume(st);
			if (result)
				return result;
		}
	}
	st->chip_config.is_asleep = !power_on;

	return 0;
}
/**
 *  @}
 */

