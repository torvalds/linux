/* SPDX-License-Identifier: GPL-2.0 */
/****************************************************************************************
 * File:		driver/input/gsensor/lis3dh.h
 * Copyright:	Copyright (C) 2012-2013 RK Corporation.
 * Author:		LiBing <libing@rock-chips.com>
 * Date:		2012.03.06
 * Description:	This driver use for rk29 chip extern gsensor. Use i2c IF ,the chip is 
 * 				STMicroelectronics lis3dh.
 *****************************************************************************************/

#ifndef LIS3DH_H
#define LIS3DH_H

#include <linux/ioctl.h>

#define ODR1					0x10  /* 1Hz output data rate */
#define ODR10					0x20  /* 10Hz output data rate */
#define ODR25					0x30  /* 25Hz output data rate */
#define ODR50					0x40  /* 50Hz output data rate */
#define ODR100					0x50  /* 100Hz output data rate */
#define ODR200					0x60  /* 200Hz output data rate */
#define ODR400					0x70  /* 400Hz output data rate */
#define ODR1250					0x90  /* 1250Hz output data rate */

#define SENSITIVITY_2G			1	/**	mg/LSB	*/
#define SENSITIVITY_4G			2	/**	mg/LSB	*/
#define SENSITIVITY_8G			4	/**	mg/LSB	*/
#define SENSITIVITY_16G			12	/**	mg/LSB	*/

#define	HIGH_RESOLUTION			0x08

/* Accelerometer Sensor Full Scale */
#define	LIS3DH_ACC_FS_MASK		0x30
#define LIS3DH_ACC_G_2G 		0x00
#define LIS3DH_ACC_G_4G 		0x10
#define LIS3DH_ACC_G_8G 		0x20
#define LIS3DH_ACC_G_16G		0x30

#define WHO_AM_I				0x0F
#define WHOAMI_LIS3DH_ACC		0x33
#define	AXISDATA_REG			0x28

#define	I2C_AUTO_INCREMENT		0x80
#define	I2C_RETRY_DELAY			5
#define	I2C_RETRIES				5

#define	RESUME_ENTRIES			17

#define	RES_CTRL_REG1			0
#define	RES_CTRL_REG2			1
#define	RES_CTRL_REG3			2
#define	RES_CTRL_REG4			3
#define	RES_CTRL_REG5			4
#define	RES_CTRL_REG6			5

#define	RES_INT_CFG1			6
#define	RES_INT_THS1			7
#define	RES_INT_DUR1			8

#define	RES_TT_CFG				9
#define	RES_TT_THS				10
#define	RES_TT_LIM				11
#define	RES_TT_TLAT				12
#define	RES_TT_TW				13
#define	TT_CFG					0x38	/*	tap config		*/
#define	TT_SRC					0x39	/*	tap source		*/
#define	TT_THS					0x3A	/*	tap threshold		*/
#define	TT_LIM					0x3B	/*	tap time limit		*/
#define	TT_TLAT					0x3C	/*	tap time latency	*/
#define	TT_TW					0x3D	/*	tap time window	*/

#define	RES_TEMP_CFG_REG		14
#define	RES_REFERENCE_REG		15
#define	RES_FIFO_CTRL_REG		16

#define	CTRL_REG1				0x20	/*	control reg 1		*/
#define	CTRL_REG2				0x21	/*	control reg 2		*/
#define	CTRL_REG3				0x22	/*	control reg 3		*/
#define	CTRL_REG4				0x23	/*	control reg 4		*/
#define	CTRL_REG5				0x24	/*	control reg 5		*/
#define	CTRL_REG6				0x25	/*	control reg 6		*/

#define	TEMP_CFG_REG			0x1F	/*	temper sens control reg	*/

#define	FIFO_CTRL_REG			0x2E	/*	FiFo control reg	*/

#define	INT_CFG1				0x30	/*	interrupt 1 config	*/
#define	INT_SRC1				0x31	/*	interrupt 1 source	*/
#define	INT_THS1				0x32	/*	interrupt 1 threshold	*/
#define	INT_DUR1				0x33	/*	interrupt 1 duration	*/

/* Default register settings */
#define RBUFF_SIZE				12	/* Rx buffer size */
#define STIO					0xA1

/* IOCTLs for LIS3DH library */
#define ST_IOCTL_INIT			_IO(STIO, 0x01)
#define ST_IOCTL_RESET			_IO(STIO, 0x04)
#define ST_IOCTL_CLOSE			_IO(STIO, 0x02)
#define ST_IOCTL_START			_IO(STIO, 0x03)
#define ST_IOCTL_GETDATA		_IOR(STIO, 0x08, char[RBUFF_SIZE+1])

/* IOCTLs for APPs */
#define ST_IOCTL_APP_SET_RATE	_IOW(STIO, 0x10, char)

/*rate*/
#define LIS3DH_RATE_800			0
#define LIS3DH_RATE_400			1
#define LIS3DH_RATE_200			2
#define LIS3DH_RATE_100			3
#define LIS3DH_RATE_50			4
#define LIS3DH_RATE_12P5		5
#define LIS3DH_RATE_6P25		6
#define LIS3DH_RATE_1P56		7
#define LIS3DH_RATE_SHIFT		3
#define LIS3DH_ASLP_RATE_50		0
#define LIS3DH_ASLP_RATE_12P5	1
#define LIS3DH_ASLP_RATE_6P25	2
#define LIS3DH_ASLP_RATE_1P56	3
#define LIS3DH_ASLP_RATE_SHIFT	6

#define ACTIVE_MASK				1
#define FREAD_MASK				2

/*status*/
#define LIS3DH_SUSPEND			2
#define LIS3DH_OPEN				1
#define LIS3DH_CLOSE			0
#define LIS3DH_SPEED			200 * 1000

#define LIS3DH_ACC_ENABLE_ALL_AXES	0x07

#define LIS3DH_RANGE			2000000
#define LIS3DH_PRECISION		16 //8bit data
#define LIS3DH_BOUNDARY			(0x1 << (LIS3DH_PRECISION - 1))
#define LIS3DH_GRAVITY_STEP		LIS3DH_RANGE / LIS3DH_BOUNDARY  //2g full scale range

#ifdef CONFIG_HAS_EARLYSUSPEND
static struct early_suspend lis3dh_early_suspend;
#endif

struct 
{	
	unsigned int cutoff_ms;
	unsigned int mask;
}lis3dh_acc_odr_table[] =
{
	{	 1,	ODR1250},
	{	 3,	ODR400 },
	{	 5,	ODR200 },
	{	10,	ODR100 },
	{   20, ODR50  },
	{   40, ODR25  },
	{  100, ODR10  },
	{ 1000, ODR1   },
};

struct lis3dh_axis {
	int x;
	int y;
	int z;
};


struct lis3dh_data {
    char		status;
    char 		curr_tate;
	
	unsigned int	poll_interval;
	unsigned int	min_interval;
	
	struct input_dev	*input_dev;
	struct i2c_client	*client;
	struct work_struct	work;
	struct delayed_work	delaywork;	/*report second event*/
    struct lis3dh_axis	sense_data;
    struct mutex		sense_data_mutex;
	struct mutex		operation_mutex;
	
    atomic_t			data_ready;
    wait_queue_head_t	data_ready_wq;
    int				start_count;
	u8				resume_state[RESUME_ENTRIES];  
};

#endif
