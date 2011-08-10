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
 *      @file   yas530.c
 *      @brief  Magnetometer setup and handling methods for Yamaha yas530
 *              compass.
 */

/* ------------------ */
/* - Include Files. - */
/* ------------------ */

#ifdef __KERNEL__
#include <linux/module.h>
#endif

#include "mpu.h"
#include "mlsl.h"
#include "mlos.h"

#include <log.h>
#undef MPL_LOG_TAG
#define MPL_LOG_TAG "MPL-compass"

/*----- YAMAHA YAS529 Registers ------*/
#define YAS530_REGADDR_DEVICE_ID          (0x80)
#define YAS530_REGADDR_ACTUATE_INIT_COIL  (0x81)
#define YAS530_REGADDR_MEASURE_COMMAND    (0x82)
#define YAS530_REGADDR_CONFIG             (0x83)
#define YAS530_REGADDR_MEASURE_INTERVAL   (0x84)
#define YAS530_REGADDR_OFFSET_X           (0x85)
#define YAS530_REGADDR_OFFSET_Y1          (0x86)
#define YAS530_REGADDR_OFFSET_Y2          (0x87)
#define YAS530_REGADDR_TEST1              (0x88)
#define YAS530_REGADDR_TEST2              (0x89)
#define YAS530_REGADDR_CAL                (0x90)
#define YAS530_REGADDR_MEASURE_DATA       (0xb0)


/* --------------------- */
/* -    Variables.     - */
/* --------------------- */
static int Cx, Cy1, Cy2;
static int /*a1, */a2, a3, a4, a5, a6, a7, a8, a9;
static int k;

static char dx, dy1, dy2;
static char d2, d3, d4, d5, d6, d7, d8, d9, d0;
static char dck;

/*****************************************
    Accelerometer Initialization Functions
*****************************************/

int set_hardware_offset(void *mlsl_handle,
		   struct ext_slave_descr *slave,
		   struct ext_slave_platform_data *pdata,
		   char offset_x, char offset_y1, char offset_y2)
{
	char data;
	int result = ML_SUCCESS;

	data = offset_x & 0x3f;
	result = MLSLSerialWriteSingle(mlsl_handle, pdata->address,
		YAS530_REGADDR_OFFSET_X, data);
	ERROR_CHECK(result);

	data = offset_y1 & 0x3f;
	result = MLSLSerialWriteSingle(mlsl_handle, pdata->address,
		YAS530_REGADDR_OFFSET_Y1, data);
	ERROR_CHECK(result);

	data = offset_y2 & 0x3f;
	result = MLSLSerialWriteSingle(mlsl_handle, pdata->address,
		YAS530_REGADDR_OFFSET_Y2, data);
	ERROR_CHECK(result);

	return result;
}

int set_measure_command(void *mlsl_handle,
		   struct ext_slave_descr *slave,
		   struct ext_slave_platform_data *pdata,
		   int ldtc, int fors, int dlymes)
{
	int result = ML_SUCCESS;

	result = MLSLSerialWriteSingle(mlsl_handle, pdata->address,
		YAS530_REGADDR_MEASURE_COMMAND, 0x01);
	ERROR_CHECK(result);

	return result;
}

int measure_normal(void *mlsl_handle,
		   struct ext_slave_descr *slave,
		   struct ext_slave_platform_data *pdata,
		   int *busy, unsigned short *t,
		   unsigned short *x, unsigned short *y1, unsigned short *y2)
{
	unsigned char data[8];
	unsigned short b, to, xo, y1o, y2o;
	int result;

	result = set_measure_command(mlsl_handle, slave, pdata, 0, 0, 0);
	MLOSSleep(2);

	result = MLSLSerialRead(mlsl_handle, pdata->address,
		YAS530_REGADDR_MEASURE_DATA, 8, data);
	ERROR_CHECK(result);
	MLOSSleep(2);

	b = (data[0]>>7) & 0x01;
	to = ((data[0]<<2) & 0x1fc) | ((data[1]>>6) & 0x03);
	xo = ((data[2]<<5) & 0xfe0) | ((data[3]>>3) & 0x1f);
	y1o = ((data[4]<<5) & 0xfe0) | ((data[5]>>3) & 0x1f);
	y2o = ((data[6]<<5) & 0xfe0) | ((data[7]>>3) & 0x1f);

	*busy = b;
	*t = to;
	*x = xo;
	*y1 = y1o;
	*y2 = y2o;

	return result;
}

int check_offset(void *mlsl_handle,
		   struct ext_slave_descr *slave,
		   struct ext_slave_platform_data *pdata,
		   char offset_x, char offset_y1, char offset_y2,
		   int *flag_x, int *flag_y1, int *flag_y2)
{
	int result;
	int busy;
	short t, x, y1, y2;

	result = set_hardware_offset(mlsl_handle, slave, pdata,
		offset_x, offset_y1, offset_y2);
	ERROR_CHECK(result);

	result = measure_normal(mlsl_handle, slave, pdata,
		&busy, &t, &x, &y1, &y2);
	ERROR_CHECK(result);

	*flag_x = 0;
	*flag_y1 = 0;
	*flag_y2 = 0;

	if (x > 2048)
		*flag_x = 1;
	if (y1 > 2048)
		*flag_y1 = 1;
	if (y2 > 2048)
		*flag_y2 = 1;
	if (x < 2048)
		*flag_x = -1;
	if (y1 < 2048)
		*flag_y1 = -1;
	if (y2 < 2048)
		*flag_y2 = -1;

	return result;
}

int measure_and_set_offset(void *mlsl_handle,
		   struct ext_slave_descr *slave,
		   struct ext_slave_platform_data *pdata,
		   char *offset)
{
	int i;
	int result = ML_SUCCESS;
	char offset_x = 0, offset_y1 = 0, offset_y2 = 0;
	int flag_x = 0, flag_y1 = 0, flag_y2 = 0;
	static const int correct[5] = {16, 8, 4, 2, 1};

	for (i = 0; i < 5; i++) {
		result = check_offset(mlsl_handle, slave, pdata,
			offset_x, offset_y1, offset_y2,
			&flag_x, &flag_y1, &flag_y2);
		ERROR_CHECK(result);

		if (flag_x)
			offset_x += flag_x * correct[i];
		if (flag_y1)
			offset_y1 += flag_y1 * correct[i];
		if (flag_y2)
			offset_y2 += flag_y2 * correct[i];
	}

	result = set_hardware_offset(mlsl_handle, slave, pdata,
		offset_x, offset_y1, offset_y2);
	ERROR_CHECK(result);

	offset[0] = offset_x;
	offset[1] = offset_y1;
	offset[2] = offset_y2;

	return result;
}

void coordinate_conversion(short x, short y1, short y2, short t,
						  int *xo, int *yo, int *zo)
{
	int sx, sy1, sy2, sy, sz;
	int hx, hy, hz;

	sx  = x - (Cx  * t) / 100;
	sy1 = y1 - (Cy1 * t) / 100;
	sy2 = y2 - (Cy2 * t) / 100;

	sy = sy1 - sy2;
	sz = -sy1 - sy2;

	hx = k * ((100 * sx + a2 * sy + a3 * sz) / 10);
	hy = k * ((a4 * sx + a5 * sy + a6 * sz) / 10);
	hz = k * ((a7 * sx + a8 * sy + a9 * sz) / 10);

	*xo = hx;
	*yo = hy;
	*zo = hz;
}

int yas530_suspend(void *mlsl_handle,
		   struct ext_slave_descr *slave,
		   struct ext_slave_platform_data *pdata)
{
	int result = ML_SUCCESS;

	return result;
}

int yas530_resume(void *mlsl_handle,
		  struct ext_slave_descr *slave,
		  struct ext_slave_platform_data *pdata)
{
	int result = ML_SUCCESS;

	unsigned char dummyData = 0x00;
	char offset[3] = {0, 0, 0};
	unsigned char data[16];
	unsigned char read_reg[1];

	/* =============================================== */

	/* Step 1 - Test register initialization */
	dummyData = 0x00;
	result = MLSLSerialWriteSingle(mlsl_handle, pdata->address,
		YAS530_REGADDR_TEST1, dummyData);
	ERROR_CHECK(result);

	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  YAS530_REGADDR_TEST2, dummyData);
	ERROR_CHECK(result);

	/* Device ID read  */
	result = MLSLSerialRead(mlsl_handle, pdata->address,
		YAS530_REGADDR_DEVICE_ID, 1, read_reg);

	/*Step 2 Read the CAL register */
	/* CAL data read */
	result = MLSLSerialRead(mlsl_handle, pdata->address,
		YAS530_REGADDR_CAL, 16, data);
	ERROR_CHECK(result);
	/* CAL data Second Read */
	result = MLSLSerialRead(mlsl_handle, pdata->address,
		YAS530_REGADDR_CAL, 16, data);
	ERROR_CHECK(result);

	/*Cal data */
	dx = data[0];
	dy1 = data[1];
	dy2 = data[2];
	d2 = (data[3]>>2) & 0x03f;
	d3 = ((data[3]<<2) & 0x0c) | ((data[4]>>6) & 0x03);
	d4 = data[4] & 0x3f;
	d5 = (data[5]>>2) & 0x3f;
	d6 = ((data[5]<<4) & 0x30) | ((data[6]>>4) & 0x0f);
	d7 = ((data[6]<<3) & 0x78) | ((data[7]>>5) & 0x07);
	d8 = ((data[7]<<1) & 0x3e) | ((data[8]>>7) & 0x01);
	d9 = ((data[8]<<1) & 0xfe) | ((data[9]>>7) & 0x01);
	d0 = (data[9]>>2) & 0x1f;
	dck = ((data[9]<<1) & 0x06) | ((data[10]>>7) & 0x01);


	/*Correction Data */
	Cx = dx * 6 - 768;
	Cy1 = dy1 * 6 - 768;
	Cy2 = dy2 * 6 - 768;
	a2 = d2 - 32;
	a3 = d3 - 8;
	a4 = d4 - 32;
	a5 = d5 + 38;
	a6 = d6 - 32;
	a7 = d7 - 64;
	a8 = d8 - 32;
	a9 = d9;
	k = d0 + 10;

	/*Obtain the [49:47] bits */
	dck &= 0x07;

	/*Step 3 : Storing the CONFIG with the CLK value */
	dummyData = 0x00 | (dck << 2);
	result = MLSLSerialWriteSingle(mlsl_handle, pdata->address,
		YAS530_REGADDR_CONFIG, dummyData);
	ERROR_CHECK(result);

	/*Step 4 : Set Acquisition Interval Register */
	dummyData = 0x00;
	result = MLSLSerialWriteSingle(mlsl_handle, pdata->address,
		YAS530_REGADDR_MEASURE_INTERVAL, dummyData);
	ERROR_CHECK(result);

	/*Step 5 : Reset Coil */
	dummyData = 0x00;
	result = MLSLSerialWriteSingle(mlsl_handle, pdata->address,
		YAS530_REGADDR_ACTUATE_INIT_COIL, dummyData);
	ERROR_CHECK(result);

	/* Offset Measurement and Set*/
	result = measure_and_set_offset(mlsl_handle, slave, pdata, offset);
	ERROR_CHECK(result);

	return result;
}

int yas530_read(void *mlsl_handle,
		struct ext_slave_descr *slave,
		struct ext_slave_platform_data *pdata, unsigned char *data)
{
	int result = ML_SUCCESS;

	int busy;
	short t, x, y1, y2;
	int xyz[3];
	short rawfixed[3];

	result = measure_normal(mlsl_handle, slave, pdata,
			&busy, &t, &x, &y1, &y2);
	ERROR_CHECK(result);

	coordinate_conversion(x, y1, y2, t, &xyz[0], &xyz[1], &xyz[2]);

	rawfixed[0] = (short) (xyz[0]/100);
	rawfixed[1] = (short) (xyz[1]/100);
	rawfixed[2] = (short) (xyz[2]/100);

	data[0] = rawfixed[0]>>8;
	data[1] = rawfixed[0] & 0xFF;
	data[2] = rawfixed[1]>>8;
	data[3] = rawfixed[1] & 0xFF;
	data[4] = rawfixed[2]>>8;
	data[5] = rawfixed[2] & 0xFF;

	return result;
}

struct ext_slave_descr yas530_descr = {
	/*.init             = */ NULL,
	/*.exit             = */ NULL,
	/*.suspend          = */ yas530_suspend,
	/*.resume           = */ yas530_resume,
	/*.read             = */ yas530_read,
	/*.config           = */ NULL,
	/*.get_config       = */ NULL,
	/*.name             = */ "yas530",
	/*.type             = */ EXT_SLAVE_TYPE_COMPASS,
	/*.id               = */ COMPASS_ID_YAS530,
	/*.reg              = */ 0x06,
	/*.len              = */ 6,
	/*.endian           = */ EXT_SLAVE_BIG_ENDIAN,
	/*.range            = */ {3276, 8001},
};

struct ext_slave_descr *yas530_get_slave_descr(void)
{
	return &yas530_descr;
}
EXPORT_SYMBOL(yas530_get_slave_descr);

/**
 *  @}
**/
