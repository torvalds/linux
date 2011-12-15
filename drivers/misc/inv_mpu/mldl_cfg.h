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
 *      @file   mldl_cfg.h
 *      @brief  The Motion Library Driver Layer Configuration header file.
 */

#ifndef __MLDL_CFG_H__
#define __MLDL_CFG_H__

#include "mltypes.h"
#include "mlsl.h"
#include <linux/mpu.h>
#include "mpu3050.h"

#include "log.h"

/*************************************************************************
 *  Sensors Bit definitions
 *************************************************************************/

#define INV_X_GYRO			(0x0001)
#define INV_Y_GYRO			(0x0002)
#define INV_Z_GYRO			(0x0004)
#define INV_DMP_PROCESSOR		(0x0008)

#define INV_X_ACCEL			(0x0010)
#define INV_Y_ACCEL			(0x0020)
#define INV_Z_ACCEL			(0x0040)

#define INV_X_COMPASS			(0x0080)
#define INV_Y_COMPASS			(0x0100)
#define INV_Z_COMPASS			(0x0200)

#define INV_X_PRESSURE			(0x0300)
#define INV_Y_PRESSURE			(0x0800)
#define INV_Z_PRESSURE			(0x1000)

#define INV_TEMPERATURE			(0x2000)
#define INV_TIME			(0x4000)

#define INV_THREE_AXIS_GYRO		(0x000F)
#define INV_THREE_AXIS_ACCEL		(0x0070)
#define INV_THREE_AXIS_COMPASS		(0x0380)
#define INV_THREE_AXIS_PRESSURE		(0x1C00)

#define INV_FIVE_AXIS			(0x007B)
#define INV_SIX_AXIS_GYRO_ACCEL		(0x007F)
#define INV_SIX_AXIS_ACCEL_COMPASS	(0x03F0)
#define INV_NINE_AXIS			(0x03FF)
#define INV_ALL_SENSORS			(0x7FFF)

#define MPL_PROD_KEY(ver, rev) (ver * 100 + rev)

/* -------------------------------------------------------------------------- */
struct mpu_ram {
	__u16 length;
	__u8 *ram;
};

struct mpu_gyro_cfg {
	__u8 int_config;
	__u8 ext_sync;
	__u8 full_scale;
	__u8 lpf;
	__u8 clk_src;
	__u8 divider;
	__u8 dmp_enable;
	__u8 fifo_enable;
	__u8 dmp_cfg1;
	__u8 dmp_cfg2;
};

/* Offset registers that can be calibrated */
struct mpu_offsets {
	__u8	tc[GYRO_NUM_AXES];
	__u16	gyro[GYRO_NUM_AXES];
};

/* Chip related information that can be read and verified */
struct mpu_chip_info {
	__u8 addr;
	__u8 product_revision;
	__u8 silicon_revision;
	__u8 product_id;
	__u16 gyro_sens_trim;
	/* Only used for MPU6050 */
	__u16 accel_sens_trim;
};


struct inv_mpu_cfg {
	__u32 requested_sensors;
	__u8 ignore_system_suspend;
};

/* Driver related state information */
struct inv_mpu_state {
#define MPU_GYRO_IS_SUSPENDED		(0x01 << EXT_SLAVE_TYPE_GYROSCOPE)
#define MPU_ACCEL_IS_SUSPENDED		(0x01 << EXT_SLAVE_TYPE_ACCEL)
#define MPU_COMPASS_IS_SUSPENDED	(0x01 << EXT_SLAVE_TYPE_COMPASS)
#define MPU_PRESSURE_IS_SUSPENDED	(0x01 << EXT_SLAVE_TYPE_PRESSURE)
#define MPU_GYRO_IS_BYPASSED		(0x10)
#define MPU_DMP_IS_SUSPENDED		(0x20)
#define MPU_GYRO_NEEDS_CONFIG		(0x40)
#define MPU_DEVICE_IS_SUSPENDED		(0x80)
	__u8 status;
	/* 0-1 for 3050, bitfield of BIT_SLVx_DLY_EN, x = [0..4] */
	__u8 i2c_slaves_enabled;
};

/* Platform data for the MPU */
struct mldl_cfg {
	struct mpu_ram			*mpu_ram;
	struct mpu_gyro_cfg		*mpu_gyro_cfg;
	struct mpu_offsets		*mpu_offsets;
	struct mpu_chip_info		*mpu_chip_info;

	/* MPU Related stored status and info */
	struct inv_mpu_cfg		*inv_mpu_cfg;
	struct inv_mpu_state		*inv_mpu_state;

	/* Slave related information */
	struct ext_slave_descr		*slave[EXT_SLAVE_NUM_TYPES];
	/* Platform Data */
	struct mpu_platform_data	*pdata;
	struct ext_slave_platform_data	*pdata_slave[EXT_SLAVE_NUM_TYPES];
};

/* -------------------------------------------------------------------------- */

int inv_mpu_open(struct mldl_cfg *mldl_cfg,
		 void *mlsl_handle,
		 void *accel_handle,
		 void *compass_handle,
		 void *pressure_handle);
int inv_mpu_close(struct mldl_cfg *mldl_cfg,
		  void *mlsl_handle,
		  void *accel_handle,
		  void *compass_handle,
		  void *pressure_handle);
int inv_mpu_resume(struct mldl_cfg *mldl_cfg,
		   void *gyro_handle,
		   void *accel_handle,
		   void *compass_handle,
		   void *pressure_handle,
		   unsigned long sensors);
int inv_mpu_suspend(struct mldl_cfg *mldl_cfg,
		    void *gyro_handle,
		    void *accel_handle,
		    void *compass_handle,
		    void *pressure_handle,
		    unsigned long sensors);
int inv_mpu_set_firmware(struct mldl_cfg *mldl_cfg,
			 void *mlsl_handle,
			 const unsigned char *data,
			 int size);

/* -------------------------------------------------------------------------- */
/* Slave Read functions */
int inv_mpu_slave_read(struct mldl_cfg *mldl_cfg,
		       void *gyro_handle,
		       void *slave_handle,
		       struct ext_slave_descr *slave,
		       struct ext_slave_platform_data *pdata,
		       unsigned char *data);
static inline int inv_mpu_read_accel(struct mldl_cfg *mldl_cfg,
				     void *gyro_handle,
				     void *accel_handle, unsigned char *data)
{
	if (!mldl_cfg) {
		LOG_RESULT_LOCATION(INV_ERROR_INVALID_PARAMETER);
		return INV_ERROR_INVALID_PARAMETER;
	}

	return inv_mpu_slave_read(
		mldl_cfg, gyro_handle, accel_handle,
		mldl_cfg->slave[EXT_SLAVE_TYPE_ACCEL],
		mldl_cfg->pdata_slave[EXT_SLAVE_TYPE_ACCEL],
		data);
}

static inline int inv_mpu_read_compass(struct mldl_cfg *mldl_cfg,
				       void *gyro_handle,
				       void *compass_handle,
				       unsigned char *data)
{
	if (!mldl_cfg) {
		LOG_RESULT_LOCATION(INV_ERROR_INVALID_PARAMETER);
		return INV_ERROR_INVALID_PARAMETER;
	}

	return inv_mpu_slave_read(
		mldl_cfg, gyro_handle, compass_handle,
		mldl_cfg->slave[EXT_SLAVE_TYPE_COMPASS],
		mldl_cfg->pdata_slave[EXT_SLAVE_TYPE_COMPASS],
		data);
}

static inline int inv_mpu_read_pressure(struct mldl_cfg *mldl_cfg,
					void *gyro_handle,
					void *pressure_handle,
					unsigned char *data)
{
	if (!mldl_cfg) {
		LOG_RESULT_LOCATION(INV_ERROR_INVALID_PARAMETER);
		return INV_ERROR_INVALID_PARAMETER;
	}

	return inv_mpu_slave_read(
		mldl_cfg, gyro_handle, pressure_handle,
		mldl_cfg->slave[EXT_SLAVE_TYPE_PRESSURE],
		mldl_cfg->pdata_slave[EXT_SLAVE_TYPE_PRESSURE],
		data);
}

int gyro_config(void *mlsl_handle,
		struct mldl_cfg *mldl_cfg,
		struct ext_slave_config *data);

/* Slave Config functions */
int inv_mpu_slave_config(struct mldl_cfg *mldl_cfg,
			 void *gyro_handle,
			 void *slave_handle,
			 struct ext_slave_config *data,
			 struct ext_slave_descr *slave,
			 struct ext_slave_platform_data *pdata);
static inline int inv_mpu_config_accel(struct mldl_cfg *mldl_cfg,
				       void *gyro_handle,
				       void *accel_handle,
				       struct ext_slave_config *data)
{
	if (!mldl_cfg) {
		LOG_RESULT_LOCATION(INV_ERROR_INVALID_PARAMETER);
		return INV_ERROR_INVALID_PARAMETER;
	}

	return inv_mpu_slave_config(
		mldl_cfg, gyro_handle, accel_handle, data,
		mldl_cfg->slave[EXT_SLAVE_TYPE_ACCEL],
		mldl_cfg->pdata_slave[EXT_SLAVE_TYPE_ACCEL]);
}

static inline int inv_mpu_config_compass(struct mldl_cfg *mldl_cfg,
					 void *gyro_handle,
					 void *compass_handle,
					 struct ext_slave_config *data)
{
	if (!mldl_cfg) {
		LOG_RESULT_LOCATION(INV_ERROR_INVALID_PARAMETER);
		return INV_ERROR_INVALID_PARAMETER;
	}

	return inv_mpu_slave_config(
		mldl_cfg, gyro_handle, compass_handle, data,
		mldl_cfg->slave[EXT_SLAVE_TYPE_COMPASS],
		mldl_cfg->pdata_slave[EXT_SLAVE_TYPE_COMPASS]);
}

static inline int inv_mpu_config_pressure(struct mldl_cfg *mldl_cfg,
					  void *gyro_handle,
					  void *pressure_handle,
					  struct ext_slave_config *data)
{
	if (!mldl_cfg) {
		LOG_RESULT_LOCATION(INV_ERROR_INVALID_PARAMETER);
		return INV_ERROR_INVALID_PARAMETER;
	}

	return inv_mpu_slave_config(
		mldl_cfg, gyro_handle, pressure_handle, data,
		mldl_cfg->slave[EXT_SLAVE_TYPE_PRESSURE],
		mldl_cfg->pdata_slave[EXT_SLAVE_TYPE_PRESSURE]);
}

int gyro_get_config(void *mlsl_handle,
		struct mldl_cfg *mldl_cfg,
		struct ext_slave_config *data);

/* Slave get config functions */
int inv_mpu_get_slave_config(struct mldl_cfg *mldl_cfg,
			     void *gyro_handle,
			     void *slave_handle,
			     struct ext_slave_config *data,
			     struct ext_slave_descr *slave,
			     struct ext_slave_platform_data *pdata);

static inline int inv_mpu_get_accel_config(struct mldl_cfg *mldl_cfg,
					   void *gyro_handle,
					   void *accel_handle,
					   struct ext_slave_config *data)
{
	if (!mldl_cfg) {
		LOG_RESULT_LOCATION(INV_ERROR_INVALID_PARAMETER);
		return INV_ERROR_INVALID_PARAMETER;
	}

	return inv_mpu_get_slave_config(
		mldl_cfg, gyro_handle, accel_handle, data,
		mldl_cfg->slave[EXT_SLAVE_TYPE_ACCEL],
		mldl_cfg->pdata_slave[EXT_SLAVE_TYPE_ACCEL]);
}

static inline int inv_mpu_get_compass_config(struct mldl_cfg *mldl_cfg,
					     void *gyro_handle,
					     void *compass_handle,
					     struct ext_slave_config *data)
{
	if (!mldl_cfg || !(mldl_cfg->pdata)) {
		LOG_RESULT_LOCATION(INV_ERROR_INVALID_PARAMETER);
		return INV_ERROR_INVALID_PARAMETER;
	}

	return inv_mpu_get_slave_config(
		mldl_cfg, gyro_handle, compass_handle, data,
		mldl_cfg->slave[EXT_SLAVE_TYPE_COMPASS],
		mldl_cfg->pdata_slave[EXT_SLAVE_TYPE_COMPASS]);
}

static inline int inv_mpu_get_pressure_config(struct mldl_cfg *mldl_cfg,
					      void *gyro_handle,
					      void *pressure_handle,
					      struct ext_slave_config *data)
{
	if (!mldl_cfg || !(mldl_cfg->pdata)) {
		LOG_RESULT_LOCATION(INV_ERROR_INVALID_PARAMETER);
		return INV_ERROR_INVALID_PARAMETER;
	}

	return inv_mpu_get_slave_config(
		mldl_cfg, gyro_handle, pressure_handle, data,
		mldl_cfg->slave[EXT_SLAVE_TYPE_PRESSURE],
		mldl_cfg->pdata_slave[EXT_SLAVE_TYPE_PRESSURE]);
}

/* -------------------------------------------------------------------------- */

static inline
long inv_mpu_get_sampling_rate_hz(struct mpu_gyro_cfg *gyro_cfg)
{
	if (((gyro_cfg->lpf) == 0) || ((gyro_cfg->lpf) == 7))
		return 8000L / (gyro_cfg->divider + 1);
	else
		return 1000L / (gyro_cfg->divider + 1);
}

static inline
long inv_mpu_get_sampling_period_us(struct mpu_gyro_cfg *gyro_cfg)
{
	if (((gyro_cfg->lpf) == 0) || ((gyro_cfg->lpf) == 7))
		return (long) (1000000L * (gyro_cfg->divider + 1)) / 8000L;
	else
		return (long) (1000000L * (gyro_cfg->divider + 1)) / 1000L;
}

#endif				/* __MLDL_CFG_H__ */

/**
 * @}
 */
