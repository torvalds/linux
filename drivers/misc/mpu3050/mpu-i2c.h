/*
 $License:
    Copyright (C) 2010 InvenSense Corporation, All Rights Reserved.

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
 * @defgroup
 * @brief
 *
 * @{
 * @file     mpu-i2c.c
 * @brief
 *
 *
 */

#ifndef __MPU_I2C_H__
#define __MPU_I2C_H__

#include <linux/i2c.h>

int sensor_i2c_write(struct i2c_adapter *i2c_adap,
		     unsigned char address,
		     unsigned int len, unsigned char const *data);

int sensor_i2c_write_register(struct i2c_adapter *i2c_adap,
			      unsigned char address,
			      unsigned char reg, unsigned char value);

int sensor_i2c_read(struct i2c_adapter *i2c_adap,
		    unsigned char address,
		    unsigned char reg,
		    unsigned int len, unsigned char *data);

int mpu_memory_read(struct i2c_adapter *i2c_adap,
		    unsigned char mpu_addr,
		    unsigned short mem_addr,
		    unsigned int len, unsigned char *data);

int mpu_memory_write(struct i2c_adapter *i2c_adap,
		     unsigned char mpu_addr,
		     unsigned short mem_addr,
		     unsigned int len, unsigned char const *data);

#endif	/* __MPU_I2C_H__ */
