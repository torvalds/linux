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
 *  @defgroup   ACCELDL (Motion Library - Accelerometer Driver Layer)
 *  @brief      Provides the interface to setup and handle an accelerometers
 *              connected to the secondary I2C interface of the gyroscope.
 *
 *  @{
 *      @file   yas529.c
 *      @brief  Magnetometer setup and handling methods for Yamaha yas529
 *              compass.
 */

/* ------------------ */
/* - Include Files. - */
/* ------------------ */

#ifdef __KERNEL__
#include <linux/module.h>
#include <linux/i2c.h>
#endif

#include "mpu.h"
#include "mlos.h"

#include <log.h>
#undef MPL_LOG_TAG
#define MPL_LOG_TAG "MPL-acc"

/*----- YAMAHA YAS529 Registers ------*/
enum YAS_REG {
	YAS_REG_CMDR		= 0x00,	/* 000 < 5 */
	YAS_REG_XOFFSETR	= 0x20,	/* 001 < 5 */
	YAS_REG_Y1OFFSETR	= 0x40,	/* 010 < 5 */
	YAS_REG_Y2OFFSETR	= 0x60,	/* 011 < 5 */
	YAS_REG_ICOILR		= 0x80,	/* 100 < 5 */
	YAS_REG_CAL		= 0xA0,	/* 101 < 5 */
	YAS_REG_CONFR		= 0xC0,	/* 110 < 5 */
	YAS_REG_DOUTR		= 0xE0	/* 111 < 5 */
};

/* --------------------- */
/* -    Variables.     - */
/* --------------------- */

static long a1;
static long a2;
static long a3;
static long a4;
static long a5;
static long a6;
static long a7;
static long a8;
static long a9;

/*****************************************
    Yamaha I2C access functions
*****************************************/

static int yas529_sensor_i2c_write(struct i2c_adapter *i2c_adap,
				   unsigned char address,
				   unsigned int len, unsigned char *data)
{
	struct i2c_msg msgs[1];
	int res;

	if (NULL == data || NULL == i2c_adap)
		return -EINVAL;

	msgs[0].addr = address;
	msgs[0].flags = 0;	/* write */
	msgs[0].buf = (unsigned char *) data;
	msgs[0].len = len;

	res = i2c_transfer(i2c_adap, msgs, 1);
	if (res < 1)
		return res;
	else
		return 0;
}

static int yas529_sensor_i2c_read(struct i2c_adapter *i2c_adap,
				  unsigned char address,
				  unsigned char reg,
				  unsigned int len, unsigned char *data)
{
	struct i2c_msg msgs[2];
	int res;

	if (NULL == data || NULL == i2c_adap)
		return -EINVAL;

	msgs[0].addr = address;
	msgs[0].flags = I2C_M_RD;
	msgs[0].buf = data;
	msgs[0].len = len;

	res = i2c_transfer(i2c_adap, msgs, 1);
	if (res < 1)
		return res;
	else
		return 0;
}

/*****************************************
    Accelerometer Initialization Functions
*****************************************/

int yas529_suspend(void *mlsl_handle,
		   struct ext_slave_descr *slave,
		   struct ext_slave_platform_data *pdata)
{
	int result = ML_SUCCESS;

	return result;
}

int yas529_resume(void *mlsl_handle,
		  struct ext_slave_descr *slave,
		  struct ext_slave_platform_data *pdata)
{
	int result = ML_SUCCESS;

	unsigned char dummyData[1] = { 0 };
	unsigned char dummyRegister = 0;
	unsigned char rawData[6];
	unsigned char calData[9];

	short xoffset, y1offset, y2offset;
	short d2, d3, d4, d5, d6, d7, d8, d9;

	/* YAS529 Application Manual MS-3C - Section 4.4.5 */
	/* =============================================== */
	/* Step 1 - register initialization */
	/* zero initialization coil register - "100 00 000" */
	dummyData[0] = YAS_REG_ICOILR | 0x00;
	result =
	    yas529_sensor_i2c_write(mlsl_handle, pdata->address, 1,
				    dummyData);
	ERROR_CHECK(result);
	/* zero config register - "110 00 000" */
	dummyData[0] = YAS_REG_CONFR | 0x00;
	result =
	    yas529_sensor_i2c_write(mlsl_handle, pdata->address, 1,
				    dummyData);
	ERROR_CHECK(result);

	/* Step 2 - initialization coil operation */
	dummyData[0] = YAS_REG_ICOILR | 0x11;
	result =
	    yas529_sensor_i2c_write(mlsl_handle, pdata->address, 1,
				    dummyData);
	ERROR_CHECK(result);
	dummyData[0] = YAS_REG_ICOILR | 0x01;
	result =
	    yas529_sensor_i2c_write(mlsl_handle, pdata->address, 1,
				    dummyData);
	ERROR_CHECK(result);
	dummyData[0] = YAS_REG_ICOILR | 0x12;
	result =
	    yas529_sensor_i2c_write(mlsl_handle, pdata->address, 1,
				    dummyData);
	ERROR_CHECK(result);
	dummyData[0] = YAS_REG_ICOILR | 0x02;
	result =
	    yas529_sensor_i2c_write(mlsl_handle, pdata->address, 1,
				    dummyData);
	ERROR_CHECK(result);
	dummyData[0] = YAS_REG_ICOILR | 0x13;
	result =
	    yas529_sensor_i2c_write(mlsl_handle, pdata->address, 1,
				    dummyData);
	ERROR_CHECK(result);
	dummyData[0] = YAS_REG_ICOILR | 0x03;
	result =
	    yas529_sensor_i2c_write(mlsl_handle, pdata->address, 1,
				    dummyData);
	ERROR_CHECK(result);
	dummyData[0] = YAS_REG_ICOILR | 0x14;
	result =
	    yas529_sensor_i2c_write(mlsl_handle, pdata->address, 1,
				    dummyData);
	ERROR_CHECK(result);
	dummyData[0] = YAS_REG_ICOILR | 0x04;
	result =
	    yas529_sensor_i2c_write(mlsl_handle, pdata->address, 1,
				    dummyData);
	ERROR_CHECK(result);
	dummyData[0] = YAS_REG_ICOILR | 0x15;
	result =
	    yas529_sensor_i2c_write(mlsl_handle, pdata->address, 1,
				    dummyData);
	ERROR_CHECK(result);
	dummyData[0] = YAS_REG_ICOILR | 0x05;
	result =
	    yas529_sensor_i2c_write(mlsl_handle, pdata->address, 1,
				    dummyData);
	ERROR_CHECK(result);
	dummyData[0] = YAS_REG_ICOILR | 0x16;
	result =
	    yas529_sensor_i2c_write(mlsl_handle, pdata->address, 1,
				    dummyData);
	ERROR_CHECK(result);
	dummyData[0] = YAS_REG_ICOILR | 0x06;
	result =
	    yas529_sensor_i2c_write(mlsl_handle, pdata->address, 1,
				    dummyData);
	ERROR_CHECK(result);
	dummyData[0] = YAS_REG_ICOILR | 0x17;
	result =
	    yas529_sensor_i2c_write(mlsl_handle, pdata->address, 1,
				    dummyData);
	ERROR_CHECK(result);
	dummyData[0] = YAS_REG_ICOILR | 0x07;
	result =
	    yas529_sensor_i2c_write(mlsl_handle, pdata->address, 1,
				    dummyData);
	ERROR_CHECK(result);
	dummyData[0] = YAS_REG_ICOILR | 0x10;
	result =
	    yas529_sensor_i2c_write(mlsl_handle, pdata->address, 1,
				    dummyData);
	ERROR_CHECK(result);
	dummyData[0] = YAS_REG_ICOILR | 0x00;
	result =
	    yas529_sensor_i2c_write(mlsl_handle, pdata->address, 1,
				    dummyData);
	ERROR_CHECK(result);

	/* Step 3 - rough offset measurement */
	/* Config register - Measurements results - "110 00 000" */
	dummyData[0] = YAS_REG_CONFR | 0x00;
	result =
	    yas529_sensor_i2c_write(mlsl_handle, pdata->address, 1,
				    dummyData);
	ERROR_CHECK(result);
	/* Measurements command register - Rough offset measurement -
	   "000 00001" */
	dummyData[0] = YAS_REG_CMDR | 0x01;
	result =
	    yas529_sensor_i2c_write(mlsl_handle, pdata->address, 1,
				    dummyData);
	ERROR_CHECK(result);
	MLOSSleep(2);		/* wait at least 1.5ms */

	/* Measurement data read */
	result =
	    yas529_sensor_i2c_read(mlsl_handle, pdata->address,
				   dummyRegister, 6, rawData);
	ERROR_CHECK(result);
	xoffset =
	    (short) ((unsigned short) rawData[5] +
		     ((unsigned short) rawData[4] & 0x7) * 256) - 5;
	if (xoffset < 0)
		xoffset = 0;
	y1offset =
	    (short) ((unsigned short) rawData[3] +
		     ((unsigned short) rawData[2] & 0x7) * 256) - 5;
	if (y1offset < 0)
		y1offset = 0;
	y2offset =
	    (short) ((unsigned short) rawData[1] +
		     ((unsigned short) rawData[0] & 0x7) * 256) - 5;
	if (y2offset < 0)
		y2offset = 0;

	/* Step 4 - rough offset setting */
	/* Set rough offset register values */
	dummyData[0] = YAS_REG_XOFFSETR | xoffset;
	result =
	    yas529_sensor_i2c_write(mlsl_handle, pdata->address, 1,
				    dummyData);
	ERROR_CHECK(result);
	dummyData[0] = YAS_REG_Y1OFFSETR | y1offset;
	result =
	    yas529_sensor_i2c_write(mlsl_handle, pdata->address, 1,
				    dummyData);
	ERROR_CHECK(result);
	dummyData[0] = YAS_REG_Y2OFFSETR | y2offset;
	result =
	    yas529_sensor_i2c_write(mlsl_handle, pdata->address, 1,
				    dummyData);
	ERROR_CHECK(result);

	/* CAL matrix read (first read is invalid) */
	/* Config register - CAL register read - "110 01 000" */
	dummyData[0] = YAS_REG_CONFR | 0x08;
	result =
	    yas529_sensor_i2c_write(mlsl_handle, pdata->address, 1,
				    dummyData);
	ERROR_CHECK(result);
	/* CAL data read */
	result =
	    yas529_sensor_i2c_read(mlsl_handle, pdata->address,
				   dummyRegister, 9, calData);
	ERROR_CHECK(result);
	/* Config register - CAL register read - "110 01 000" */
	dummyData[0] = YAS_REG_CONFR | 0x08;
	result =
	    yas529_sensor_i2c_write(mlsl_handle, pdata->address, 1,
				    dummyData);
	ERROR_CHECK(result);
	/* CAL data read */
	result =
	    yas529_sensor_i2c_read(mlsl_handle, pdata->address,
				   dummyRegister, 9, calData);
	ERROR_CHECK(result);

	/* Calculate coefficients of the sensitivity corrcetion matrix */
#if 1				/* production sensor */
	a1 = 100;
	d2 = (calData[0] & 0xFC) >> 2;	/* [71..66] 6bit */
	a2 = (short) (d2 - 32);
	/* [65..62] 4bit */
	d3 = ((calData[0] & 0x03) << 2) | ((calData[1] & 0xC0) >> 6);
	a3 = (short) (d3 - 8);
	d4 = (calData[1] & 0x3F);	/* [61..56] 6bit */
	a4 = (short) (d4 - 32);
	d5 = (calData[2] & 0xFC) >> 2;	/* [55..50] 6bit */
	a5 = (short) (d5 - 32) + 70;
	/* [49..44] 6bit */
	d6 = ((calData[2] & 0x03) << 4) | ((calData[3] & 0xF0) >> 4);
	a6 = (short) (d6 - 32);
	/* [43..38] 6bit */
	d7 = ((calData[3] & 0x0F) << 2) | ((calData[4] & 0xC0) >> 6);
	a7 = (short) (d7 - 32);
	d8 = (calData[4] & 0x3F);	/* [37..32] 6bit */
	a8 = (short) (d8 - 32);
	d9 = (calData[5] & 0xFE) >> 1;	/* [31..25] 7bit */
	a9 = (short) (d9 - 64) + 130;
#else				/* evaluation sensor */
	a1 = 1.0f;
	/* [71..66] 6bit */
	d2 = (calData[0] & 0xFC) >> 2;
	a2 = (short) d2;
	/* [65..60] 6bit */
	d3 = ((calData[0] & 0x03) << 4) | ((calData[1] & 0xF0) >> 4);
	a3 = (short) d3;
	/* [59..54] 6bit */
	d4 = ((calData[1] & 0x0F) << 2) | ((calData[2] & 0xC0) >> 6);
	a4 = (short) d4;
	/* [53..48] 6bit */
	d5 = (calData[2] & 0x3F);
	a5 = (short) (d5 + 70);
	/* [47..42] 6bit */
	d6 = ((calData[3] & 0xFC) >> 2);
	a6 = (short) d6;
	/* [41..36] 6bit */
	d7 = ((calData[3] & 0x03) << 4) | ((calData[4] & 0xF0) >> 4);
	a7 = (short) d7;
	/* [35..30] 6bit */
	d8 = ((calData[4] & 0x0F) << 2) | ((calData[5] & 0xC0) >> 6);
	a8 = (short) d8;
	/* [29..24] 6bit */
	d9 = (calData[5] & 0x3F);
	a9 = (short) (d9 + 150);
#endif

	return result;
}

int yas529_read(void *mlsl_handle,
		struct ext_slave_descr *slave,
		struct ext_slave_platform_data *pdata, unsigned char *data)
{
	unsigned char stat;
	unsigned char rawData[6];
	unsigned char dummyData[1] = { 0 };
	unsigned char dummyRegister = 0;
	tMLError result = ML_SUCCESS;
	short SX, SY1, SY2, SY, SZ;
	short row1fixed, row2fixed, row3fixed;

	/* Config register - Measurements results - "110 00 000" */
	dummyData[0] = YAS_REG_CONFR | 0x00;
	result =
	    yas529_sensor_i2c_write(mlsl_handle, pdata->address, 1,
				    dummyData);
	ERROR_CHECK(result);
	/* Measurements command register - Normal magnetic field measurement -
	   "000 00000" */
	dummyData[0] = YAS_REG_CMDR | 0x00;
	result =
	    yas529_sensor_i2c_write(mlsl_handle, pdata->address, 1,
				    dummyData);
	ERROR_CHECK(result);
	MLOSSleep(10);
	/* Measurement data read */
	result =
	    yas529_sensor_i2c_read(mlsl_handle, pdata->address,
				   dummyRegister, 6,
				   (unsigned char *) &rawData);
	ERROR_CHECK(result);

	stat = rawData[0] & 0x80;
	if (stat == 0x00) {
		/* Extract raw data */
		SX = (short) ((unsigned short) rawData[5] +
			      ((unsigned short) rawData[4] & 0x7) * 256);
		SY1 =
		    (short) ((unsigned short) rawData[3] +
			     ((unsigned short) rawData[2] & 0x7) * 256);
		SY2 =
		    (short) ((unsigned short) rawData[1] +
			     ((unsigned short) rawData[0] & 0x7) * 256);
		if ((SX <= 1) || (SY1 <= 1) || (SY2 <= 1))
			return ML_ERROR_COMPASS_DATA_UNDERFLOW;
		if ((SX >= 1024) || (SY1 >= 1024) || (SY2 >= 1024))
			return ML_ERROR_COMPASS_DATA_OVERFLOW;
		/* Convert to XYZ axis */
		SX = -1 * SX;
		SY = SY2 - SY1;
		SZ = SY1 + SY2;

		/* Apply sensitivity correction matrix */
		row1fixed =
		    (short) ((a1 * SX + a2 * SY + a3 * SZ) >> 7) * 41;
		row2fixed =
		    (short) ((a4 * SX + a5 * SY + a6 * SZ) >> 7) * 41;
		row3fixed =
		    (short) ((a7 * SX + a8 * SY + a9 * SZ) >> 7) * 41;

		data[0] = row1fixed >> 8;
		data[1] = row1fixed & 0xFF;
		data[2] = row2fixed >> 8;
		data[3] = row2fixed & 0xFF;
		data[4] = row3fixed >> 8;
		data[5] = row3fixed & 0xFF;

		return ML_SUCCESS;
	} else {
		return ML_ERROR_COMPASS_DATA_NOT_READY;
	}
}

struct ext_slave_descr yas529_descr = {
	/*.init             = */ NULL,
	/*.exit             = */ NULL,
	/*.suspend          = */ yas529_suspend,
	/*.resume           = */ yas529_resume,
	/*.read             = */ yas529_read,
	/*.config           = */ NULL,
	/*.get_config       = */ NULL,
	/*.name             = */ "yas529",
	/*.type             = */ EXT_SLAVE_TYPE_COMPASS,
	/*.id               = */ COMPASS_ID_YAS529,
	/*.reg              = */ 0x06,
	/*.len              = */ 6,
	/*.endian           = */ EXT_SLAVE_BIG_ENDIAN,
	/*.range            = */ {19660, 8000},
};

struct ext_slave_descr *yas529_get_slave_descr(void)
{
	return &yas529_descr;
}
EXPORT_SYMBOL(yas529_get_slave_descr);

/**
 *  @}
 */
