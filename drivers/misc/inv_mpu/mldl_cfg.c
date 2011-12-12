/*
	$License:
	Copyright (C) 2011 InvenSense Corporation, All Rights Reserved.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
	$
 */

/**
 *  @addtogroup MLDL
 *
 *  @{
 *      @file   mldl_cfg.c
 *      @brief  The Motion Library Driver Layer.
 */

/* -------------------------------------------------------------------------- */
#include <linux/delay.h>
#include <linux/slab.h>

#include <stddef.h>

#include "mldl_cfg.h"
#include <linux/mpu.h>
#include "mpu3050.h"

#include "mlsl.h"
#include "mldl_print_cfg.h"
#include "log.h"
#undef MPL_LOG_TAG
#define MPL_LOG_TAG "mldl_cfg:"

/* -------------------------------------------------------------------------- */

#define SLEEP   1
#define WAKE_UP 0
#define RESET   1
#define STANDBY 1

/* -------------------------------------------------------------------------- */

/**
 * @brief Stop the DMP running
 *
 * @return INV_SUCCESS or non-zero error code
 */
static int dmp_stop(struct mldl_cfg *mldl_cfg, void *gyro_handle)
{
	unsigned char user_ctrl_reg;
	int result;

	if (mldl_cfg->inv_mpu_state->status & MPU_DMP_IS_SUSPENDED)
		return INV_SUCCESS;

	result = inv_serial_read(gyro_handle, mldl_cfg->mpu_chip_info->addr,
				 MPUREG_USER_CTRL, 1, &user_ctrl_reg);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	user_ctrl_reg = (user_ctrl_reg & (~BIT_FIFO_EN)) | BIT_FIFO_RST;
	user_ctrl_reg = (user_ctrl_reg & (~BIT_DMP_EN)) | BIT_DMP_RST;

	result = inv_serial_single_write(gyro_handle,
					 mldl_cfg->mpu_chip_info->addr,
					 MPUREG_USER_CTRL, user_ctrl_reg);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	mldl_cfg->inv_mpu_state->status |= MPU_DMP_IS_SUSPENDED;

	return result;
}

/**
 * @brief Starts the DMP running
 *
 * @return INV_SUCCESS or non-zero error code
 */
static int dmp_start(struct mldl_cfg *mldl_cfg, void *mlsl_handle)
{
	unsigned char user_ctrl_reg;
	int result;

	if ((!(mldl_cfg->inv_mpu_state->status & MPU_DMP_IS_SUSPENDED) &&
	    mldl_cfg->mpu_gyro_cfg->dmp_enable)
		||
	   ((mldl_cfg->inv_mpu_state->status & MPU_DMP_IS_SUSPENDED) &&
		   !mldl_cfg->mpu_gyro_cfg->dmp_enable))
		return INV_SUCCESS;

	result = inv_serial_read(mlsl_handle, mldl_cfg->mpu_chip_info->addr,
				 MPUREG_USER_CTRL, 1, &user_ctrl_reg);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	result = inv_serial_single_write(
		mlsl_handle, mldl_cfg->mpu_chip_info->addr,
		MPUREG_USER_CTRL,
		((user_ctrl_reg & (~BIT_FIFO_EN))
			| BIT_FIFO_RST));
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	result = inv_serial_single_write(
		mlsl_handle, mldl_cfg->mpu_chip_info->addr,
		MPUREG_USER_CTRL, user_ctrl_reg);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	result = inv_serial_read(mlsl_handle, mldl_cfg->mpu_chip_info->addr,
				 MPUREG_USER_CTRL, 1, &user_ctrl_reg);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	user_ctrl_reg |= BIT_DMP_EN;

	if (mldl_cfg->mpu_gyro_cfg->fifo_enable)
		user_ctrl_reg |= BIT_FIFO_EN;
	else
		user_ctrl_reg &= ~BIT_FIFO_EN;

	user_ctrl_reg |= BIT_DMP_RST;

	result = inv_serial_single_write(
		mlsl_handle, mldl_cfg->mpu_chip_info->addr,
		MPUREG_USER_CTRL, user_ctrl_reg);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	mldl_cfg->inv_mpu_state->status &= ~MPU_DMP_IS_SUSPENDED;

	return result;
}



static int mpu3050_set_i2c_bypass(struct mldl_cfg *mldl_cfg,
				  void *mlsl_handle, unsigned char enable)
{
	unsigned char b;
	int result;
	unsigned char status = mldl_cfg->inv_mpu_state->status;

	if ((status & MPU_GYRO_IS_BYPASSED && enable) ||
	    (!(status & MPU_GYRO_IS_BYPASSED) && !enable))
		return INV_SUCCESS;

	/*---- get current 'USER_CTRL' into b ----*/
	result = inv_serial_read(
		mlsl_handle, mldl_cfg->mpu_chip_info->addr,
		MPUREG_USER_CTRL, 1, &b);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	b &= ~BIT_AUX_IF_EN;

	if (!enable) {
		result = inv_serial_single_write(
			mlsl_handle, mldl_cfg->mpu_chip_info->addr,
			MPUREG_USER_CTRL,
			(b | BIT_AUX_IF_EN));
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
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
		result = inv_serial_single_write(
			mlsl_handle, mldl_cfg->mpu_chip_info->addr,
			MPUREG_AUX_SLV_ADDR, 0x7F);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
		/*
		 * 2) wait enough time for a nack to occur, then go into
		 *    bypass mode:
		 */
		msleep(2);
		result = inv_serial_single_write(
			mlsl_handle, mldl_cfg->mpu_chip_info->addr,
			MPUREG_USER_CTRL, (b));
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
		/*
		 * 3) wait for up to one MPU cycle then restore the slave
		 *    address
		 */
		msleep(inv_mpu_get_sampling_period_us(mldl_cfg->mpu_gyro_cfg)
			/ 1000);
		result = inv_serial_single_write(
			mlsl_handle, mldl_cfg->mpu_chip_info->addr,
			MPUREG_AUX_SLV_ADDR,
			mldl_cfg->pdata_slave[EXT_SLAVE_TYPE_ACCEL]
				->address);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}

		/*
		 * 4) reset the ime interface
		 */

		result = inv_serial_single_write(
			mlsl_handle, mldl_cfg->mpu_chip_info->addr,
			MPUREG_USER_CTRL,
			(b | BIT_AUX_IF_RST));
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
		msleep(2);
	}
	if (enable)
		mldl_cfg->inv_mpu_state->status |= MPU_GYRO_IS_BYPASSED;
	else
		mldl_cfg->inv_mpu_state->status &= ~MPU_GYRO_IS_BYPASSED;

	return result;
}


/**
 *  @brief  enables/disables the I2C bypass to an external device
 *          connected to MPU's secondary I2C bus.
 *  @param  enable
 *              Non-zero to enable pass through.
 *  @return INV_SUCCESS if successful, a non-zero error code otherwise.
 */
static int mpu_set_i2c_bypass(struct mldl_cfg *mldl_cfg, void *mlsl_handle,
			      unsigned char enable)
{
	return mpu3050_set_i2c_bypass(mldl_cfg, mlsl_handle, enable);
}


#define NUM_OF_PROD_REVS (ARRAY_SIZE(prod_rev_map))

/* NOTE : when not indicated, product revision
	  is considered an 'npp'; non production part */

struct prod_rev_map_t {
	unsigned char silicon_rev;
	unsigned short gyro_trim;
};

#define OLDEST_PROD_REV_SUPPORTED	11
static struct prod_rev_map_t prod_rev_map[] = {
	{0, 0},
	{MPU_SILICON_REV_A4, 131},	/* 1  A? OBSOLETED */
	{MPU_SILICON_REV_A4, 131},	/* 2  |  */
	{MPU_SILICON_REV_A4, 131},	/* 3  |  */
	{MPU_SILICON_REV_A4, 131},	/* 4  |  */
	{MPU_SILICON_REV_A4, 131},	/* 5  |  */
	{MPU_SILICON_REV_A4, 131},	/* 6  |  */
	{MPU_SILICON_REV_A4, 131},	/* 7  |  */
	{MPU_SILICON_REV_A4, 131},	/* 8  |  */
	{MPU_SILICON_REV_A4, 131},	/* 9  |  */
	{MPU_SILICON_REV_A4, 131},	/* 10 V  */
	{MPU_SILICON_REV_B1, 131},	/* 11 B1 */
	{MPU_SILICON_REV_B1, 131},	/* 12 |  */
	{MPU_SILICON_REV_B1, 131},	/* 13 |  */
	{MPU_SILICON_REV_B1, 131},	/* 14 V  */
	{MPU_SILICON_REV_B4, 131},	/* 15 B4 */
	{MPU_SILICON_REV_B4, 131},	/* 16 |  */
	{MPU_SILICON_REV_B4, 131},	/* 17 |  */
	{MPU_SILICON_REV_B4, 131},	/* 18 |  */
	{MPU_SILICON_REV_B4, 115},	/* 19 |  */
	{MPU_SILICON_REV_B4, 115},	/* 20 V  */
	{MPU_SILICON_REV_B6, 131},	/* 21 B6 (B6/A9)  */
	{MPU_SILICON_REV_B4, 115},	/* 22 B4 (B7/A10) */
	{MPU_SILICON_REV_B6, 0},	/* 23 B6 */
	{MPU_SILICON_REV_B6, 0},	/* 24 |  */
	{MPU_SILICON_REV_B6, 0},	/* 25 |  */
	{MPU_SILICON_REV_B6, 131},	/* 26 V  (B6/A11) */
};

/**
 *  @internal
 *  @brief  Get the silicon revision ID from OTP for MPU3050.
 *          The silicon revision number is in read from OTP bank 0,
 *          ADDR6[7:2].  The corresponding ID is retrieved by lookup
 *          in a map.
 *
 *  @param  mldl_cfg
 *              a pointer to the mldl config data structure.
 *  @param  mlsl_handle
 *              an file handle to the serial communication device the
 *              device is connected to.
 *
 *  @return 0 on success, a non-zero error code otherwise.
 */
static int inv_get_silicon_rev_mpu3050(
		struct mldl_cfg *mldl_cfg, void *mlsl_handle)
{
	int result;
	unsigned char index = 0x00;
	unsigned char bank =
	    (BIT_PRFTCH_EN | BIT_CFG_USER_BANK | MPU_MEM_OTP_BANK_0);
	unsigned short mem_addr = ((bank << 8) | 0x06);
	struct mpu_chip_info *mpu_chip_info = mldl_cfg->mpu_chip_info;

	result = inv_serial_read(mlsl_handle, mldl_cfg->mpu_chip_info->addr,
				 MPUREG_PRODUCT_ID, 1,
				 &mpu_chip_info->product_id);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	result = inv_serial_read_mem(
		mlsl_handle, mldl_cfg->mpu_chip_info->addr,
		mem_addr, 1, &index);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	index >>= 2;

	/* clean the prefetch and cfg user bank bits */
	result = inv_serial_single_write(
		mlsl_handle, mldl_cfg->mpu_chip_info->addr,
		MPUREG_BANK_SEL, 0);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	if (index < OLDEST_PROD_REV_SUPPORTED || index >= NUM_OF_PROD_REVS) {
		mpu_chip_info->silicon_revision = 0;
		mpu_chip_info->gyro_sens_trim = 0;
		MPL_LOGE("Unsupported Product Revision Detected : %d\n", index);
		return INV_ERROR_INVALID_MODULE;
	}

	mpu_chip_info->product_revision = index;
	mpu_chip_info->silicon_revision = prod_rev_map[index].silicon_rev;
	mpu_chip_info->gyro_sens_trim = prod_rev_map[index].gyro_trim;
	if (mpu_chip_info->gyro_sens_trim == 0) {
		MPL_LOGE("gyro sensitivity trim is 0"
			 " - unsupported non production part.\n");
		return INV_ERROR_INVALID_MODULE;
	}

	return result;
}
#define inv_get_silicon_rev inv_get_silicon_rev_mpu3050


/**
 *  @brief  Enable / Disable the use MPU's secondary I2C interface level
 *          shifters.
 *          When enabled the secondary I2C interface to which the external
 *          device is connected runs at VDD voltage (main supply).
 *          When disabled the 2nd interface runs at VDDIO voltage.
 *          See the device specification for more details.
 *
 *  @note   using this API may produce unpredictable results, depending on how
 *          the MPU and slave device are setup on the target platform.
 *          Use of this API should entirely be restricted to system
 *          integrators. Once the correct value is found, there should be no
 *          need to change the level shifter at runtime.
 *
 *  @pre    Must be called after inv_serial_start().
 *  @note   Typically called before inv_dmp_open().
 *
 *  @param[in]  enable:
 *                  0 to run at VDDIO (default),
 *                  1 to run at VDD.
 *
 *  @return INV_SUCCESS if successfull, a non-zero error code otherwise.
 */
static int inv_mpu_set_level_shifter_bit(struct mldl_cfg *mldl_cfg,
				  void *mlsl_handle, unsigned char enable)
{
	int result;
	unsigned char regval;

	unsigned char reg;
	unsigned char mask;

	if (0 == mldl_cfg->mpu_chip_info->silicon_revision)
		return INV_ERROR_INVALID_PARAMETER;

	/*-- on parts before B6 the VDDIO bit is bit 7 of ACCEL_BURST_ADDR --
	NOTE: this is incompatible with ST accelerometers where the VDDIO
	bit MUST be set to enable ST's internal logic to autoincrement
	the register address on burst reads --*/
	if ((mldl_cfg->mpu_chip_info->silicon_revision & 0xf)
		< MPU_SILICON_REV_B6) {
		reg = MPUREG_ACCEL_BURST_ADDR;
		mask = 0x80;
	} else {
		/*-- on B6 parts the VDDIO bit was moved to FIFO_EN2 =>
		  the mask is always 0x04 --*/
		reg = MPUREG_FIFO_EN2;
		mask = 0x04;
	}

	result = inv_serial_read(mlsl_handle, mldl_cfg->mpu_chip_info->addr,
				 reg, 1, &regval);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	if (enable)
		regval |= mask;
	else
		regval &= ~mask;

	result = inv_serial_single_write(
		mlsl_handle, mldl_cfg->mpu_chip_info->addr, reg, regval);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	return result;
	return INV_SUCCESS;
}


/**
 * @internal
 * @brief   This function controls the power management on the MPU device.
 *          The entire chip can be put to low power sleep mode, or individual
 *          gyros can be turned on/off.
 *
 *          Putting the device into sleep mode depending upon the changing needs
 *          of the associated applications is a recommended method for reducing
 *          power consuption.  It is a safe opearation in that sleep/wake up of
 *          gyros while running will not result in any interruption of data.
 *
 *          Although it is entirely allowed to put the device into full sleep
 *          while running the DMP, it is not recomended because it will disrupt
 *          the ongoing calculations carried on inside the DMP and consequently
 *          the sensor fusion algorithm. Furthermore, while in sleep mode
 *          read & write operation from the app processor on both registers and
 *          memory are disabled and can only regained by restoring the MPU in
 *          normal power mode.
 *          Disabling any of the gyro axis will reduce the associated power
 *          consuption from the PLL but will not stop the DMP from running
 *          state.
 *
 * @param   reset
 *              Non-zero to reset the device. Note that this setting
 *              is volatile and the corresponding register bit will
 *              clear itself right after being applied.
 * @param   sleep
 *              Non-zero to put device into full sleep.
 * @param   disable_gx
 *              Non-zero to disable gyro X.
 * @param   disable_gy
 *              Non-zero to disable gyro Y.
 * @param   disable_gz
 *              Non-zero to disable gyro Z.
 *
 * @return  INV_SUCCESS if successfull; a non-zero error code otherwise.
 */
static int mpu3050_pwr_mgmt(struct mldl_cfg *mldl_cfg,
			    void *mlsl_handle,
			    unsigned char reset,
			    unsigned char sleep,
			    unsigned char disable_gx,
			    unsigned char disable_gy,
			    unsigned char disable_gz)
{
	unsigned char b;
	int result;

	result =
	    inv_serial_read(mlsl_handle, mldl_cfg->mpu_chip_info->addr,
			    MPUREG_PWR_MGM, 1, &b);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	/* If we are awake, we need to put it in bypass before resetting */
	if ((!(b & BIT_SLEEP)) && reset)
		result = mpu_set_i2c_bypass(mldl_cfg, mlsl_handle, 1);

	/* Reset if requested */
	if (reset) {
		MPL_LOGV("Reset MPU3050\n");
		result = inv_serial_single_write(
			mlsl_handle, mldl_cfg->mpu_chip_info->addr,
			MPUREG_PWR_MGM, b | BIT_H_RESET);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
		msleep(5);
		/* Some chips are awake after reset and some are asleep,
		 * check the status */
		result = inv_serial_read(
			mlsl_handle, mldl_cfg->mpu_chip_info->addr,
			MPUREG_PWR_MGM, 1, &b);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
	}

	/* Update the suspended state just in case we return early */
	if (b & BIT_SLEEP) {
		mldl_cfg->inv_mpu_state->status |= MPU_GYRO_IS_SUSPENDED;
		mldl_cfg->inv_mpu_state->status |= MPU_DEVICE_IS_SUSPENDED;
	} else {
		mldl_cfg->inv_mpu_state->status &= ~MPU_GYRO_IS_SUSPENDED;
		mldl_cfg->inv_mpu_state->status &= ~MPU_DEVICE_IS_SUSPENDED;
	}

	/* if power status match requested, nothing else's left to do */
	if ((b & (BIT_SLEEP | BIT_STBY_XG | BIT_STBY_YG | BIT_STBY_ZG)) ==
	    (((sleep != 0) * BIT_SLEEP) |
	     ((disable_gx != 0) * BIT_STBY_XG) |
	     ((disable_gy != 0) * BIT_STBY_YG) |
	     ((disable_gz != 0) * BIT_STBY_ZG))) {
		return INV_SUCCESS;
	}

	/*
	 * This specific transition between states needs to be reinterpreted:
	 *    (1,1,1,1) -> (0,1,1,1) has to become
	 *    (1,1,1,1) -> (1,0,0,0) -> (0,1,1,1)
	 * where
	 *    (1,1,1,1) is (sleep=1,disable_gx=1,disable_gy=1,disable_gz=1)
	 */
	if ((b & (BIT_SLEEP | BIT_STBY_XG | BIT_STBY_YG | BIT_STBY_ZG)) ==
	    (BIT_SLEEP | BIT_STBY_XG | BIT_STBY_YG | BIT_STBY_ZG)
	    && ((!sleep) && disable_gx && disable_gy && disable_gz)) {
		result = mpu3050_pwr_mgmt(mldl_cfg, mlsl_handle, 0, 1, 0, 0, 0);
		if (result)
			return result;
		b |= BIT_SLEEP;
		b &= ~(BIT_STBY_XG | BIT_STBY_YG | BIT_STBY_ZG);
	}

	if ((b & BIT_SLEEP) != ((sleep != 0) * BIT_SLEEP)) {
		if (sleep) {
			result = mpu_set_i2c_bypass(mldl_cfg, mlsl_handle, 1);
			if (result) {
				LOG_RESULT_LOCATION(result);
				return result;
			}
			b |= BIT_SLEEP;
			result =
			    inv_serial_single_write(
				    mlsl_handle, mldl_cfg->mpu_chip_info->addr,
				    MPUREG_PWR_MGM, b);
			if (result) {
				LOG_RESULT_LOCATION(result);
				return result;
			}
			mldl_cfg->inv_mpu_state->status |=
				MPU_GYRO_IS_SUSPENDED;
			mldl_cfg->inv_mpu_state->status |=
				MPU_DEVICE_IS_SUSPENDED;
		} else {
			b &= ~BIT_SLEEP;
			result =
			    inv_serial_single_write(
				    mlsl_handle, mldl_cfg->mpu_chip_info->addr,
				    MPUREG_PWR_MGM, b);
			if (result) {
				LOG_RESULT_LOCATION(result);
				return result;
			}
			mldl_cfg->inv_mpu_state->status &=
				~MPU_GYRO_IS_SUSPENDED;
			mldl_cfg->inv_mpu_state->status &=
				~MPU_DEVICE_IS_SUSPENDED;
			msleep(5);
		}
	}
	/*---
	  WORKAROUND FOR PUTTING GYRO AXIS in STAND-BY MODE
	  1) put one axis at a time in stand-by
	  ---*/
	if ((b & BIT_STBY_XG) != ((disable_gx != 0) * BIT_STBY_XG)) {
		b ^= BIT_STBY_XG;
		result = inv_serial_single_write(
			mlsl_handle, mldl_cfg->mpu_chip_info->addr,
			MPUREG_PWR_MGM, b);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
	}
	if ((b & BIT_STBY_YG) != ((disable_gy != 0) * BIT_STBY_YG)) {
		b ^= BIT_STBY_YG;
		result = inv_serial_single_write(
			mlsl_handle, mldl_cfg->mpu_chip_info->addr,
			MPUREG_PWR_MGM, b);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
	}
	if ((b & BIT_STBY_ZG) != ((disable_gz != 0) * BIT_STBY_ZG)) {
		b ^= BIT_STBY_ZG;
		result = inv_serial_single_write(
			mlsl_handle, mldl_cfg->mpu_chip_info->addr,
			MPUREG_PWR_MGM, b);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
	}

	return INV_SUCCESS;
}


/**
 *  @brief  sets the clock source for the gyros.
 *  @param  mldl_cfg
 *              a pointer to the struct mldl_cfg data structure.
 *  @param  gyro_handle
 *              an handle to the serial device the gyro is assigned to.
 *  @return ML_SUCCESS if successful, a non-zero error code otherwise.
 */
static int mpu_set_clock_source(void *gyro_handle, struct mldl_cfg *mldl_cfg)
{
	int result;
	unsigned char cur_clk_src;
	unsigned char reg;

	/* clock source selection */
	result = inv_serial_read(gyro_handle, mldl_cfg->mpu_chip_info->addr,
				 MPUREG_PWR_MGM, 1, &reg);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	cur_clk_src = reg & BITS_CLKSEL;
	reg &= ~BITS_CLKSEL;


	result = inv_serial_single_write(
		gyro_handle, mldl_cfg->mpu_chip_info->addr,
		MPUREG_PWR_MGM, mldl_cfg->mpu_gyro_cfg->clk_src | reg);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	/* TODO : workarounds to be determined and implemented */

	return result;
}

/**
 * Configures the MPU I2C Master
 *
 * @mldl_cfg Handle to the configuration data
 * @gyro_handle handle to the gyro communictation interface
 * @slave Can be Null if turning off the slave
 * @slave_pdata Can be null if turning off the slave
 * @slave_id enum ext_slave_type to determine which index to use
 *
 *
 * This fucntion configures the slaves by:
 * 1) Setting up the read
 *    a) Read Register
 *    b) Read Length
 * 2) Set up the data trigger (MPU6050 only)
 *    a) Set trigger write register
 *    b) Set Trigger write value
 * 3) Set up the divider (MPU6050 only)
 * 4) Set the slave bypass mode depending on slave
 *
 * returns INV_SUCCESS or non-zero error code
 */
static int mpu_set_slave_mpu3050(struct mldl_cfg *mldl_cfg,
				 void *gyro_handle,
				 struct ext_slave_descr *slave,
				 struct ext_slave_platform_data *slave_pdata,
				 int slave_id)
{
	int result;
	unsigned char reg;
	unsigned char slave_reg;
	unsigned char slave_len;
	unsigned char slave_endian;
	unsigned char slave_address;

	if (slave_id != EXT_SLAVE_TYPE_ACCEL)
		return 0;

	result = mpu_set_i2c_bypass(mldl_cfg, gyro_handle, true);

	if (NULL == slave || NULL == slave_pdata) {
		slave_reg = 0;
		slave_len = 0;
		slave_endian = 0;
		slave_address = 0;
		mldl_cfg->inv_mpu_state->i2c_slaves_enabled = 0;
	} else {
		slave_reg = slave->read_reg;
		slave_len = slave->read_len;
		slave_endian = slave->endian;
		slave_address = slave_pdata->address;
		mldl_cfg->inv_mpu_state->i2c_slaves_enabled = 1;
	}

	/* Address */
	result = inv_serial_single_write(gyro_handle,
					 mldl_cfg->mpu_chip_info->addr,
					 MPUREG_AUX_SLV_ADDR, slave_address);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	/* Register */
	result = inv_serial_read(gyro_handle, mldl_cfg->mpu_chip_info->addr,
				 MPUREG_ACCEL_BURST_ADDR, 1, &reg);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	reg = ((reg & 0x80) | slave_reg);
	result = inv_serial_single_write(gyro_handle,
					 mldl_cfg->mpu_chip_info->addr,
					 MPUREG_ACCEL_BURST_ADDR, reg);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	/* Length */
	result = inv_serial_read(gyro_handle, mldl_cfg->mpu_chip_info->addr,
				 MPUREG_USER_CTRL, 1, &reg);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	reg = (reg & ~BIT_AUX_RD_LENG);
	result = inv_serial_single_write(
		gyro_handle, mldl_cfg->mpu_chip_info->addr,
		MPUREG_USER_CTRL, reg);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	return result;
}


static int mpu_set_slave(struct mldl_cfg *mldl_cfg,
			 void *gyro_handle,
			 struct ext_slave_descr *slave,
			 struct ext_slave_platform_data *slave_pdata,
			 int slave_id)
{
	return mpu_set_slave_mpu3050(mldl_cfg, gyro_handle, slave,
				     slave_pdata, slave_id);
}
/**
 * Check to see if the gyro was reset by testing a couple of registers known
 * to change on reset.
 *
 * @mldl_cfg mldl configuration structure
 * @gyro_handle handle used to communicate with the gyro
 *
 * @return INV_SUCCESS or non-zero error code
 */
static int mpu_was_reset(struct mldl_cfg *mldl_cfg, void *gyro_handle)
{
	int result = INV_SUCCESS;
	unsigned char reg;

	result = inv_serial_read(gyro_handle, mldl_cfg->mpu_chip_info->addr,
				 MPUREG_DMP_CFG_2, 1, &reg);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	if (mldl_cfg->mpu_gyro_cfg->dmp_cfg2 != reg)
		return true;

	if (0 != mldl_cfg->mpu_gyro_cfg->dmp_cfg1)
		return false;

	/* Inconclusive assume it was reset */
	return true;
}


int inv_mpu_set_firmware(struct mldl_cfg *mldl_cfg, void *mlsl_handle,
			 const unsigned char *data, int size)
{
	int bank, offset, write_size;
	int result;
	unsigned char read[MPU_MEM_BANK_SIZE];

	if (mldl_cfg->inv_mpu_state->status & MPU_DEVICE_IS_SUSPENDED) {
#if INV_CACHE_DMP == 1
		memcpy(mldl_cfg->mpu_ram->ram, data, size);
		return INV_SUCCESS;
#else
		LOG_RESULT_LOCATION(INV_ERROR_MEMORY_SET);
		return INV_ERROR_MEMORY_SET;
#endif
	}

	if (!(mldl_cfg->inv_mpu_state->status & MPU_DMP_IS_SUSPENDED)) {
		LOG_RESULT_LOCATION(INV_ERROR_MEMORY_SET);
		return INV_ERROR_MEMORY_SET;
	}
	/* Write and verify memory */
	for (bank = 0; size > 0; bank++,
			size -= write_size,
			data += write_size) {
		if (size > MPU_MEM_BANK_SIZE)
			write_size = MPU_MEM_BANK_SIZE;
		else
			write_size = size;

		result = inv_serial_write_mem(mlsl_handle,
				mldl_cfg->mpu_chip_info->addr,
				((bank << 8) | 0x00),
				write_size,
				data);
		if (result) {
			LOG_RESULT_LOCATION(result);
			MPL_LOGE("Write mem error in bank %d\n", bank);
			return result;
		}
		result = inv_serial_read_mem(mlsl_handle,
				mldl_cfg->mpu_chip_info->addr,
				((bank << 8) | 0x00),
				write_size,
				read);
		if (result) {
			LOG_RESULT_LOCATION(result);
			MPL_LOGE("Read mem error in bank %d\n", bank);
			return result;
		}

#define ML_SKIP_CHECK 20
		for (offset = 0; offset < write_size; offset++) {
			/* skip the register memory locations */
			if (bank == 0 && offset < ML_SKIP_CHECK)
				continue;
			if (data[offset] != read[offset]) {
				result = INV_ERROR_SERIAL_WRITE;
				break;
			}
		}
		if (result != INV_SUCCESS) {
			LOG_RESULT_LOCATION(result);
			MPL_LOGE("Read data mismatch at bank %d, offset %d\n",
				bank, offset);
			return result;
		}
	}
	return INV_SUCCESS;
}

static int gyro_resume(struct mldl_cfg *mldl_cfg, void *gyro_handle,
		       unsigned long sensors)
{
	int result;
	int ii;
	unsigned char reg;
	unsigned char regs[7];

	/* Wake up the part */
	result = mpu3050_pwr_mgmt(mldl_cfg, gyro_handle, false, false,
				  !(sensors & INV_X_GYRO),
				  !(sensors & INV_Y_GYRO),
				  !(sensors & INV_Z_GYRO));

	if (!(mldl_cfg->inv_mpu_state->status & MPU_GYRO_NEEDS_CONFIG) &&
	    !mpu_was_reset(mldl_cfg, gyro_handle)) {
		return INV_SUCCESS;
	}

	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	result = inv_serial_single_write(
		gyro_handle, mldl_cfg->mpu_chip_info->addr,
		MPUREG_INT_CFG,
		(mldl_cfg->mpu_gyro_cfg->int_config |
			mldl_cfg->pdata->int_config));
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	result = inv_serial_single_write(
		gyro_handle, mldl_cfg->mpu_chip_info->addr,
		MPUREG_SMPLRT_DIV, mldl_cfg->mpu_gyro_cfg->divider);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	result = mpu_set_clock_source(gyro_handle, mldl_cfg);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	reg = DLPF_FS_SYNC_VALUE(mldl_cfg->mpu_gyro_cfg->ext_sync,
				 mldl_cfg->mpu_gyro_cfg->full_scale,
				 mldl_cfg->mpu_gyro_cfg->lpf);
	result = inv_serial_single_write(
		gyro_handle, mldl_cfg->mpu_chip_info->addr,
		MPUREG_DLPF_FS_SYNC, reg);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	result = inv_serial_single_write(
		gyro_handle, mldl_cfg->mpu_chip_info->addr,
		MPUREG_DMP_CFG_1, mldl_cfg->mpu_gyro_cfg->dmp_cfg1);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	result = inv_serial_single_write(
		gyro_handle, mldl_cfg->mpu_chip_info->addr,
		MPUREG_DMP_CFG_2, mldl_cfg->mpu_gyro_cfg->dmp_cfg2);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	/* Write and verify memory */
#if INV_CACHE_DMP != 0
	inv_mpu_set_firmware(mldl_cfg, gyro_handle,
		mldl_cfg->mpu_ram->ram, mldl_cfg->mpu_ram->length);
#endif

	result = inv_serial_single_write(
		gyro_handle, mldl_cfg->mpu_chip_info->addr,
		MPUREG_XG_OFFS_TC, mldl_cfg->mpu_offsets->tc[0]);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	result = inv_serial_single_write(
		gyro_handle, mldl_cfg->mpu_chip_info->addr,
		MPUREG_YG_OFFS_TC, mldl_cfg->mpu_offsets->tc[1]);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	result = inv_serial_single_write(
		gyro_handle, mldl_cfg->mpu_chip_info->addr,
		MPUREG_ZG_OFFS_TC, mldl_cfg->mpu_offsets->tc[2]);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	regs[0] = MPUREG_X_OFFS_USRH;
	for (ii = 0; ii < ARRAY_SIZE(mldl_cfg->mpu_offsets->gyro); ii++) {
		regs[1 + ii * 2] =
			(unsigned char)(mldl_cfg->mpu_offsets->gyro[ii] >> 8)
			& 0xff;
		regs[1 + ii * 2 + 1] =
			(unsigned char)(mldl_cfg->mpu_offsets->gyro[ii] & 0xff);
	}
	result = inv_serial_write(gyro_handle, mldl_cfg->mpu_chip_info->addr,
				  7, regs);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	/* Configure slaves */
	result = inv_mpu_set_level_shifter_bit(mldl_cfg, gyro_handle,
					       mldl_cfg->pdata->level_shifter);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	mldl_cfg->inv_mpu_state->status &= ~MPU_GYRO_NEEDS_CONFIG;

	return result;
}

int gyro_config(void *mlsl_handle,
		struct mldl_cfg *mldl_cfg,
		struct ext_slave_config *data)
{
	struct mpu_gyro_cfg *mpu_gyro_cfg = mldl_cfg->mpu_gyro_cfg;
	struct mpu_chip_info *mpu_chip_info = mldl_cfg->mpu_chip_info;
	struct mpu_offsets *mpu_offsets = mldl_cfg->mpu_offsets;
	int ii;

	if (!data->data)
		return INV_ERROR_INVALID_PARAMETER;

	switch (data->key) {
	case MPU_SLAVE_INT_CONFIG:
		mpu_gyro_cfg->int_config = *((__u8 *)data->data);
		break;
	case MPU_SLAVE_EXT_SYNC:
		mpu_gyro_cfg->ext_sync = *((__u8 *)data->data);
		break;
	case MPU_SLAVE_FULL_SCALE:
		mpu_gyro_cfg->full_scale = *((__u8 *)data->data);
		break;
	case MPU_SLAVE_LPF:
		mpu_gyro_cfg->lpf = *((__u8 *)data->data);
		break;
	case MPU_SLAVE_CLK_SRC:
		mpu_gyro_cfg->clk_src = *((__u8 *)data->data);
		break;
	case MPU_SLAVE_DIVIDER:
		mpu_gyro_cfg->divider = *((__u8 *)data->data);
		break;
	case MPU_SLAVE_DMP_ENABLE:
		mpu_gyro_cfg->dmp_enable = *((__u8 *)data->data);
		break;
	case MPU_SLAVE_FIFO_ENABLE:
		mpu_gyro_cfg->fifo_enable = *((__u8 *)data->data);
		break;
	case MPU_SLAVE_DMP_CFG1:
		mpu_gyro_cfg->dmp_cfg1 = *((__u8 *)data->data);
		break;
	case MPU_SLAVE_DMP_CFG2:
		mpu_gyro_cfg->dmp_cfg2 = *((__u8 *)data->data);
		break;
	case MPU_SLAVE_TC:
		for (ii = 0; ii < GYRO_NUM_AXES; ii++)
			mpu_offsets->tc[ii] = ((__u8 *)data->data)[ii];
		break;
	case MPU_SLAVE_GYRO:
		for (ii = 0; ii < GYRO_NUM_AXES; ii++)
			mpu_offsets->gyro[ii] = ((__u16 *)data->data)[ii];
		break;
	case MPU_SLAVE_ADDR:
		mpu_chip_info->addr = *((__u8 *)data->data);
		break;
	case MPU_SLAVE_PRODUCT_REVISION:
		mpu_chip_info->product_revision = *((__u8 *)data->data);
		break;
	case MPU_SLAVE_SILICON_REVISION:
		mpu_chip_info->silicon_revision = *((__u8 *)data->data);
		break;
	case MPU_SLAVE_PRODUCT_ID:
		mpu_chip_info->product_id = *((__u8 *)data->data);
		break;
	case MPU_SLAVE_GYRO_SENS_TRIM:
		mpu_chip_info->gyro_sens_trim = *((__u16 *)data->data);
		break;
	case MPU_SLAVE_ACCEL_SENS_TRIM:
		mpu_chip_info->accel_sens_trim = *((__u16 *)data->data);
		break;
	case MPU_SLAVE_RAM:
		if (data->len != mldl_cfg->mpu_ram->length)
			return INV_ERROR_INVALID_PARAMETER;

		memcpy(mldl_cfg->mpu_ram->ram, data->data, data->len);
		break;
	default:
		LOG_RESULT_LOCATION(INV_ERROR_FEATURE_NOT_IMPLEMENTED);
		return INV_ERROR_FEATURE_NOT_IMPLEMENTED;
	};
	mldl_cfg->inv_mpu_state->status |= MPU_GYRO_NEEDS_CONFIG;
	return INV_SUCCESS;
}

int gyro_get_config(void *mlsl_handle,
		struct mldl_cfg *mldl_cfg,
		struct ext_slave_config *data)
{
	struct mpu_gyro_cfg *mpu_gyro_cfg = mldl_cfg->mpu_gyro_cfg;
	struct mpu_chip_info *mpu_chip_info = mldl_cfg->mpu_chip_info;
	struct mpu_offsets *mpu_offsets = mldl_cfg->mpu_offsets;
	int ii;

	if (!data->data)
		return INV_ERROR_INVALID_PARAMETER;

	switch (data->key) {
	case MPU_SLAVE_INT_CONFIG:
		*((__u8 *)data->data) = mpu_gyro_cfg->int_config;
		break;
	case MPU_SLAVE_EXT_SYNC:
		*((__u8 *)data->data) = mpu_gyro_cfg->ext_sync;
		break;
	case MPU_SLAVE_FULL_SCALE:
		*((__u8 *)data->data) = mpu_gyro_cfg->full_scale;
		break;
	case MPU_SLAVE_LPF:
		*((__u8 *)data->data) = mpu_gyro_cfg->lpf;
		break;
	case MPU_SLAVE_CLK_SRC:
		*((__u8 *)data->data) = mpu_gyro_cfg->clk_src;
		break;
	case MPU_SLAVE_DIVIDER:
		*((__u8 *)data->data) = mpu_gyro_cfg->divider;
		break;
	case MPU_SLAVE_DMP_ENABLE:
		*((__u8 *)data->data) = mpu_gyro_cfg->dmp_enable;
		break;
	case MPU_SLAVE_FIFO_ENABLE:
		*((__u8 *)data->data) = mpu_gyro_cfg->fifo_enable;
		break;
	case MPU_SLAVE_DMP_CFG1:
		*((__u8 *)data->data) = mpu_gyro_cfg->dmp_cfg1;
		break;
	case MPU_SLAVE_DMP_CFG2:
		*((__u8 *)data->data) = mpu_gyro_cfg->dmp_cfg2;
		break;
	case MPU_SLAVE_TC:
		for (ii = 0; ii < GYRO_NUM_AXES; ii++)
			((__u8 *)data->data)[ii] = mpu_offsets->tc[ii];
		break;
	case MPU_SLAVE_GYRO:
		for (ii = 0; ii < GYRO_NUM_AXES; ii++)
			((__u16 *)data->data)[ii] = mpu_offsets->gyro[ii];
		break;
	case MPU_SLAVE_ADDR:
		*((__u8 *)data->data) = mpu_chip_info->addr;
		break;
	case MPU_SLAVE_PRODUCT_REVISION:
		*((__u8 *)data->data) = mpu_chip_info->product_revision;
		break;
	case MPU_SLAVE_SILICON_REVISION:
		*((__u8 *)data->data) = mpu_chip_info->silicon_revision;
		break;
	case MPU_SLAVE_PRODUCT_ID:
		*((__u8 *)data->data) = mpu_chip_info->product_id;
		break;
	case MPU_SLAVE_GYRO_SENS_TRIM:
		*((__u16 *)data->data) = mpu_chip_info->gyro_sens_trim;
		break;
	case MPU_SLAVE_ACCEL_SENS_TRIM:
		*((__u16 *)data->data) = mpu_chip_info->accel_sens_trim;
		break;
	case MPU_SLAVE_RAM:
		if (data->len != mldl_cfg->mpu_ram->length)
			return INV_ERROR_INVALID_PARAMETER;

		memcpy(data->data, mldl_cfg->mpu_ram->ram, data->len);
		break;
	default:
		LOG_RESULT_LOCATION(INV_ERROR_FEATURE_NOT_IMPLEMENTED);
		return INV_ERROR_FEATURE_NOT_IMPLEMENTED;
	};

	return INV_SUCCESS;
}


/*******************************************************************************
 *******************************************************************************
 * Exported functions
 *******************************************************************************
 ******************************************************************************/

/**
 * Initializes the pdata structure to defaults.
 *
 * Opens the device to read silicon revision, product id and whoami.
 *
 * @mldl_cfg
 *          The internal device configuration data structure.
 * @mlsl_handle
 *          The serial communication handle.
 *
 * @return INV_SUCCESS if silicon revision, product id and woami are supported
 *         by this software.
 */
int inv_mpu_open(struct mldl_cfg *mldl_cfg,
		 void *gyro_handle,
		 void *accel_handle,
		 void *compass_handle, void *pressure_handle)
{
	int result;
	void *slave_handle[EXT_SLAVE_NUM_TYPES];
	int ii;

	/* Default is Logic HIGH, pushpull, latch disabled, anyread to clear */
	ii = 0;
	mldl_cfg->inv_mpu_cfg->ignore_system_suspend = false;
	mldl_cfg->mpu_gyro_cfg->int_config = BIT_DMP_INT_EN;
	mldl_cfg->mpu_gyro_cfg->clk_src = MPU_CLK_SEL_PLLGYROZ;
	mldl_cfg->mpu_gyro_cfg->lpf = MPU_FILTER_42HZ;
	mldl_cfg->mpu_gyro_cfg->full_scale = MPU_FS_2000DPS;
	mldl_cfg->mpu_gyro_cfg->divider = 4;
	mldl_cfg->mpu_gyro_cfg->dmp_enable = 1;
	mldl_cfg->mpu_gyro_cfg->fifo_enable = 1;
	mldl_cfg->mpu_gyro_cfg->ext_sync = 0;
	mldl_cfg->mpu_gyro_cfg->dmp_cfg1 = 0;
	mldl_cfg->mpu_gyro_cfg->dmp_cfg2 = 0;
	mldl_cfg->inv_mpu_state->status =
		MPU_DMP_IS_SUSPENDED |
		MPU_GYRO_IS_SUSPENDED |
		MPU_ACCEL_IS_SUSPENDED |
		MPU_COMPASS_IS_SUSPENDED |
		MPU_PRESSURE_IS_SUSPENDED |
		MPU_DEVICE_IS_SUSPENDED;
	mldl_cfg->inv_mpu_state->i2c_slaves_enabled = 0;

	slave_handle[EXT_SLAVE_TYPE_GYROSCOPE] = gyro_handle;
	slave_handle[EXT_SLAVE_TYPE_ACCEL] = accel_handle;
	slave_handle[EXT_SLAVE_TYPE_COMPASS] = compass_handle;
	slave_handle[EXT_SLAVE_TYPE_PRESSURE] = pressure_handle;

	if (mldl_cfg->mpu_chip_info->addr == 0) {
		LOG_RESULT_LOCATION(INV_ERROR_INVALID_PARAMETER);
		return INV_ERROR_INVALID_PARAMETER;
	}

	/*
	 * Reset,
	 * Take the DMP out of sleep, and
	 * read the product_id, sillicon rev and whoami
	 */
	mldl_cfg->inv_mpu_state->status |= MPU_GYRO_IS_BYPASSED;
	result = mpu3050_pwr_mgmt(mldl_cfg, gyro_handle, RESET, 0, 0, 0, 0);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	result = inv_get_silicon_rev(mldl_cfg, gyro_handle);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	/* Get the factory temperature compensation offsets */
	result = inv_serial_read(gyro_handle, mldl_cfg->mpu_chip_info->addr,
				 MPUREG_XG_OFFS_TC, 1,
				 &mldl_cfg->mpu_offsets->tc[0]);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	result = inv_serial_read(gyro_handle, mldl_cfg->mpu_chip_info->addr,
				 MPUREG_YG_OFFS_TC, 1,
				 &mldl_cfg->mpu_offsets->tc[1]);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	result = inv_serial_read(gyro_handle, mldl_cfg->mpu_chip_info->addr,
				 MPUREG_ZG_OFFS_TC, 1,
				 &mldl_cfg->mpu_offsets->tc[2]);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	/* Into bypass mode before sleeping and calling the slaves init */
	result = mpu_set_i2c_bypass(mldl_cfg, gyro_handle, true);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	result = inv_mpu_set_level_shifter_bit(mldl_cfg, gyro_handle,
			mldl_cfg->pdata->level_shifter);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}


#if INV_CACHE_DMP != 0
	result = mpu3050_pwr_mgmt(mldl_cfg, gyro_handle, 0, SLEEP, 0, 0, 0);
#endif
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}


	return result;

}

/**
 * Close the mpu interface
 *
 * @mldl_cfg pointer to the configuration structure
 * @mlsl_handle pointer to the serial layer handle
 *
 * @return INV_SUCCESS or non-zero error code
 */
int inv_mpu_close(struct mldl_cfg *mldl_cfg,
		  void *gyro_handle,
		  void *accel_handle,
		  void *compass_handle,
		  void *pressure_handle)
{
	return 0;
}

/**
 *  @brief  resume the MPU device and all the other sensor
 *          devices from their low power state.
 *
 *  @mldl_cfg
 *              pointer to the configuration structure
 *  @gyro_handle
 *              the main file handle to the MPU device.
 *  @accel_handle
 *              an handle to the accelerometer device, if sitting
 *              onto a separate bus. Can match mlsl_handle if
 *              the accelerometer device operates on the same
 *              primary bus of MPU.
 *  @compass_handle
 *              an handle to the compass device, if sitting
 *              onto a separate bus. Can match mlsl_handle if
 *              the compass device operates on the same
 *              primary bus of MPU.
 *  @pressure_handle
 *              an handle to the pressure sensor device, if sitting
 *              onto a separate bus. Can match mlsl_handle if
 *              the pressure sensor device operates on the same
 *              primary bus of MPU.
 *  @resume_gyro
 *              whether resuming the gyroscope device is
 *              actually needed (if the device supports low power
 *              mode of some sort).
 *  @resume_accel
 *              whether resuming the accelerometer device is
 *              actually needed (if the device supports low power
 *              mode of some sort).
 *  @resume_compass
 *              whether resuming the compass device is
 *              actually needed (if the device supports low power
 *              mode of some sort).
 *  @resume_pressure
 *              whether resuming the pressure sensor device is
 *              actually needed (if the device supports low power
 *              mode of some sort).
 *  @return  INV_SUCCESS or a non-zero error code.
 */
int inv_mpu_resume(struct mldl_cfg *mldl_cfg,
		   void *gyro_handle,
		   void *accel_handle,
		   void *compass_handle,
		   void *pressure_handle,
		   unsigned long sensors)
{
	int result = INV_SUCCESS;
	int ii;
	bool resume_slave[EXT_SLAVE_NUM_TYPES];
	bool resume_dmp = sensors & INV_DMP_PROCESSOR;
	void *slave_handle[EXT_SLAVE_NUM_TYPES];
	resume_slave[EXT_SLAVE_TYPE_GYROSCOPE] =
		(sensors & (INV_X_GYRO | INV_Y_GYRO | INV_Z_GYRO));
	resume_slave[EXT_SLAVE_TYPE_ACCEL] =
		sensors & INV_THREE_AXIS_ACCEL;
	resume_slave[EXT_SLAVE_TYPE_COMPASS] =
		sensors & INV_THREE_AXIS_COMPASS;
	resume_slave[EXT_SLAVE_TYPE_PRESSURE] =
		sensors & INV_THREE_AXIS_PRESSURE;

	slave_handle[EXT_SLAVE_TYPE_GYROSCOPE] = gyro_handle;
	slave_handle[EXT_SLAVE_TYPE_ACCEL] = accel_handle;
	slave_handle[EXT_SLAVE_TYPE_COMPASS] = compass_handle;
	slave_handle[EXT_SLAVE_TYPE_PRESSURE] = pressure_handle;


	mldl_print_cfg(mldl_cfg);

	/* Skip the Gyro since slave[EXT_SLAVE_TYPE_GYROSCOPE] is NULL */
	for (ii = EXT_SLAVE_TYPE_ACCEL; ii < EXT_SLAVE_NUM_TYPES; ii++) {
		if (resume_slave[ii] &&
		    ((!mldl_cfg->slave[ii]) ||
			(!mldl_cfg->slave[ii]->resume))) {
			LOG_RESULT_LOCATION(INV_ERROR_INVALID_PARAMETER);
			return INV_ERROR_INVALID_PARAMETER;
		}
	}

	if ((resume_slave[EXT_SLAVE_TYPE_GYROSCOPE] || resume_dmp)
	    && ((mldl_cfg->inv_mpu_state->status & MPU_GYRO_IS_SUSPENDED) ||
		(mldl_cfg->inv_mpu_state->status & MPU_GYRO_NEEDS_CONFIG))) {
		result = mpu_set_i2c_bypass(mldl_cfg, gyro_handle, 1);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
		result = dmp_stop(mldl_cfg, gyro_handle);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
		result = gyro_resume(mldl_cfg, gyro_handle, sensors);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
	}

	for (ii = 0; ii < EXT_SLAVE_NUM_TYPES; ii++) {
		if (!mldl_cfg->slave[ii] ||
		    !mldl_cfg->pdata_slave[ii] ||
		    !resume_slave[ii] ||
		    !(mldl_cfg->inv_mpu_state->status & (1 << ii)))
			continue;

		if (EXT_SLAVE_BUS_SECONDARY ==
		    mldl_cfg->pdata_slave[ii]->bus) {
			result = mpu_set_i2c_bypass(mldl_cfg, gyro_handle,
						    true);
			if (result) {
				LOG_RESULT_LOCATION(result);
				return result;
			}
		}
		result = mldl_cfg->slave[ii]->resume(slave_handle[ii],
						mldl_cfg->slave[ii],
						mldl_cfg->pdata_slave[ii]);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
		mldl_cfg->inv_mpu_state->status &= ~(1 << ii);
	}

	for (ii = 0; ii < EXT_SLAVE_NUM_TYPES; ii++) {
		if (resume_dmp &&
		    !(mldl_cfg->inv_mpu_state->status & (1 << ii)) &&
		    mldl_cfg->pdata_slave[ii] &&
		    EXT_SLAVE_BUS_SECONDARY == mldl_cfg->pdata_slave[ii]->bus) {
			result = mpu_set_slave(mldl_cfg,
					gyro_handle,
					mldl_cfg->slave[ii],
					mldl_cfg->pdata_slave[ii],
					mldl_cfg->slave[ii]->type);
			if (result) {
				LOG_RESULT_LOCATION(result);
				return result;
			}
		}
	}

	/* Turn on the master i2c iterface if necessary */
	if (resume_dmp) {
		result = mpu_set_i2c_bypass(
			mldl_cfg, gyro_handle,
			!(mldl_cfg->inv_mpu_state->i2c_slaves_enabled));
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}

		/* Now start */
		result = dmp_start(mldl_cfg, gyro_handle);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
	}
	mldl_cfg->inv_mpu_cfg->requested_sensors = sensors;

	return result;
}

/**
 *  @brief  suspend the MPU device and all the other sensor
 *          devices into their low power state.
 *  @mldl_cfg
 *              a pointer to the struct mldl_cfg internal data
 *              structure.
 *  @gyro_handle
 *              the main file handle to the MPU device.
 *  @accel_handle
 *              an handle to the accelerometer device, if sitting
 *              onto a separate bus. Can match gyro_handle if
 *              the accelerometer device operates on the same
 *              primary bus of MPU.
 *  @compass_handle
 *              an handle to the compass device, if sitting
 *              onto a separate bus. Can match gyro_handle if
 *              the compass device operates on the same
 *              primary bus of MPU.
 *  @pressure_handle
 *              an handle to the pressure sensor device, if sitting
 *              onto a separate bus. Can match gyro_handle if
 *              the pressure sensor device operates on the same
 *              primary bus of MPU.
 *  @accel
 *              whether suspending the accelerometer device is
 *              actually needed (if the device supports low power
 *              mode of some sort).
 *  @compass
 *              whether suspending the compass device is
 *              actually needed (if the device supports low power
 *              mode of some sort).
 *  @pressure
 *              whether suspending the pressure sensor device is
 *              actually needed (if the device supports low power
 *              mode of some sort).
 *  @return  INV_SUCCESS or a non-zero error code.
 */
int inv_mpu_suspend(struct mldl_cfg *mldl_cfg,
		    void *gyro_handle,
		    void *accel_handle,
		    void *compass_handle,
		    void *pressure_handle,
		    unsigned long sensors)
{
	int result = INV_SUCCESS;
	int ii;
	struct ext_slave_descr **slave = mldl_cfg->slave;
	struct ext_slave_platform_data **pdata_slave = mldl_cfg->pdata_slave;
	bool suspend_dmp = ((sensors & INV_DMP_PROCESSOR) == INV_DMP_PROCESSOR);
	bool suspend_slave[EXT_SLAVE_NUM_TYPES];
	void *slave_handle[EXT_SLAVE_NUM_TYPES];

	suspend_slave[EXT_SLAVE_TYPE_GYROSCOPE] =
		((sensors & (INV_X_GYRO | INV_Y_GYRO | INV_Z_GYRO))
			== (INV_X_GYRO | INV_Y_GYRO | INV_Z_GYRO));
	suspend_slave[EXT_SLAVE_TYPE_ACCEL] =
		((sensors & INV_THREE_AXIS_ACCEL) == INV_THREE_AXIS_ACCEL);
	suspend_slave[EXT_SLAVE_TYPE_COMPASS] =
		((sensors & INV_THREE_AXIS_COMPASS) == INV_THREE_AXIS_COMPASS);
	suspend_slave[EXT_SLAVE_TYPE_PRESSURE] =
		((sensors & INV_THREE_AXIS_PRESSURE) ==
			INV_THREE_AXIS_PRESSURE);

	slave_handle[EXT_SLAVE_TYPE_GYROSCOPE] = gyro_handle;
	slave_handle[EXT_SLAVE_TYPE_ACCEL] = accel_handle;
	slave_handle[EXT_SLAVE_TYPE_COMPASS] = compass_handle;
	slave_handle[EXT_SLAVE_TYPE_PRESSURE] = pressure_handle;

	if (suspend_dmp) {
		result = mpu_set_i2c_bypass(mldl_cfg, gyro_handle, 1);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
		result = dmp_stop(mldl_cfg, gyro_handle);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
	}

	/* Gyro */
	if (suspend_slave[EXT_SLAVE_TYPE_GYROSCOPE] &&
	    !(mldl_cfg->inv_mpu_state->status & MPU_GYRO_IS_SUSPENDED)) {
		result = mpu3050_pwr_mgmt(
			mldl_cfg, gyro_handle, 0,
			suspend_dmp && suspend_slave[EXT_SLAVE_TYPE_GYROSCOPE],
			(unsigned char)(sensors & INV_X_GYRO),
			(unsigned char)(sensors & INV_Y_GYRO),
			(unsigned char)(sensors & INV_Z_GYRO));
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
	}

	for (ii = 0; ii < EXT_SLAVE_NUM_TYPES; ii++) {
		bool is_suspended = mldl_cfg->inv_mpu_state->status & (1 << ii);
		if (!slave[ii]   || !pdata_slave[ii] ||
		    is_suspended || !suspend_slave[ii])
			continue;

		if (EXT_SLAVE_BUS_SECONDARY == pdata_slave[ii]->bus) {
			result = mpu_set_i2c_bypass(mldl_cfg, gyro_handle, 1);
			if (result) {
				LOG_RESULT_LOCATION(result);
				return result;
			}
		}
		result = slave[ii]->suspend(slave_handle[ii],
						  slave[ii],
						  pdata_slave[ii]);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
		if (EXT_SLAVE_BUS_SECONDARY == pdata_slave[ii]->bus) {
			result = mpu_set_slave(mldl_cfg, gyro_handle,
					       NULL, NULL,
					       slave[ii]->type);
			if (result) {
				LOG_RESULT_LOCATION(result);
				return result;
			}
		}
		mldl_cfg->inv_mpu_state->status |= (1 << ii);
	}

	/* Re-enable the i2c master if there are configured slaves and DMP */
	if (!suspend_dmp) {
		result = mpu_set_i2c_bypass(
			mldl_cfg, gyro_handle,
			!(mldl_cfg->inv_mpu_state->i2c_slaves_enabled));
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
	}
	mldl_cfg->inv_mpu_cfg->requested_sensors = (~sensors) & INV_ALL_SENSORS;

	return result;
}

int inv_mpu_slave_read(struct mldl_cfg *mldl_cfg,
		       void *gyro_handle,
		       void *slave_handle,
		       struct ext_slave_descr *slave,
		       struct ext_slave_platform_data *pdata,
		       unsigned char *data)
{
	int result;
	int bypass_result;
	int remain_bypassed = true;

	if (NULL == slave || NULL == slave->read) {
		LOG_RESULT_LOCATION(INV_ERROR_INVALID_CONFIGURATION);
		return INV_ERROR_INVALID_CONFIGURATION;
	}

	if ((EXT_SLAVE_BUS_SECONDARY == pdata->bus)
	    && (!(mldl_cfg->inv_mpu_state->status & MPU_GYRO_IS_BYPASSED))) {
		remain_bypassed = false;
		result = mpu_set_i2c_bypass(mldl_cfg, gyro_handle, 1);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
	}

	result = slave->read(slave_handle, slave, pdata, data);

	if (!remain_bypassed) {
		bypass_result = mpu_set_i2c_bypass(mldl_cfg, gyro_handle, 0);
		if (bypass_result) {
			LOG_RESULT_LOCATION(bypass_result);
			return bypass_result;
		}
	}
	return result;
}

int inv_mpu_slave_config(struct mldl_cfg *mldl_cfg,
			 void *gyro_handle,
			 void *slave_handle,
			 struct ext_slave_config *data,
			 struct ext_slave_descr *slave,
			 struct ext_slave_platform_data *pdata)
{
	int result;
	int remain_bypassed = true;

	if (NULL == slave || NULL == slave->config) {
		LOG_RESULT_LOCATION(INV_ERROR_INVALID_CONFIGURATION);
		return INV_ERROR_INVALID_CONFIGURATION;
	}

	if (data->apply && (EXT_SLAVE_BUS_SECONDARY == pdata->bus)
	    && (!(mldl_cfg->inv_mpu_state->status & MPU_GYRO_IS_BYPASSED))) {
		remain_bypassed = false;
		result = mpu_set_i2c_bypass(mldl_cfg, gyro_handle, 1);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
	}

	result = slave->config(slave_handle, slave, pdata, data);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	if (!remain_bypassed) {
		result = mpu_set_i2c_bypass(mldl_cfg, gyro_handle, 0);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
	}
	return result;
}

int inv_mpu_get_slave_config(struct mldl_cfg *mldl_cfg,
			     void *gyro_handle,
			     void *slave_handle,
			     struct ext_slave_config *data,
			     struct ext_slave_descr *slave,
			     struct ext_slave_platform_data *pdata)
{
	int result;
	int remain_bypassed = true;

	if (NULL == slave || NULL == slave->get_config) {
		LOG_RESULT_LOCATION(INV_ERROR_INVALID_CONFIGURATION);
		return INV_ERROR_INVALID_CONFIGURATION;
	}

	if (data->apply && (EXT_SLAVE_BUS_SECONDARY == pdata->bus)
	    && (!(mldl_cfg->inv_mpu_state->status & MPU_GYRO_IS_BYPASSED))) {
		remain_bypassed = false;
		result = mpu_set_i2c_bypass(mldl_cfg, gyro_handle, 1);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
	}

	result = slave->get_config(slave_handle, slave, pdata, data);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	if (!remain_bypassed) {
		result = mpu_set_i2c_bypass(mldl_cfg, gyro_handle, 0);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
	}
	return result;
}

/**
 * @}
 */
