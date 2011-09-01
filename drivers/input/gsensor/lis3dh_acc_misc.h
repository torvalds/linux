
/******************** (C) COPYRIGHT 2010 STMicroelectronics ********************
*
* File Name          : lis3dh_misc.h
* Authors            : MH - C&I BU - Application Team
*		     : Carmine Iascone (carmine.iascone@st.com)
*		     : Matteo Dameno (matteo.dameno@st.com)
* Version            : V 1.0.5
* Date               : 26/08/2010
*
********************************************************************************
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*
* THE PRESENT SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES
* OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, FOR THE SOLE
* PURPOSE TO SUPPORT YOUR APPLICATION DEVELOPMENT.
* AS A RESULT, STMICROELECTRONICS SHALL NOT BE HELD LIABLE FOR ANY DIRECT,
* INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING FROM THE
* CONTENT OF SUCH SOFTWARE AND/OR THE USE MADE BY CUSTOMERS OF THE CODING
* INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
*
* THIS SOFTWARE IS SPECIFICALLY DESIGNED FOR EXCLUSIVE USE WITH ST PARTS.
*
*******************************************************************************/
/*******************************************************************************
Version History.

Revision 1-0-0 05/11/2009
First Release
Revision 1-0-1 26/01/2010
Linux K&R Compliant Release
Revision 1-0-5 16/08/2010
Interrupt Management

*******************************************************************************/

#ifndef	__LIS3DH_H__
#define	__LIS3DH_H__

#include	<linux/ioctl.h>	/* For IOCTL macros */
#include	<linux/input.h>

#define SAD0L			0x00
#define SAD0H			0x01
#define LIS3DH_ACC_I2C_SADROOT	0x0C
#define LIS3DH_ACC_I2C_SAD_L	((LIS3DH_ACC_I2C_SADROOT<<1)|SAD0L)
#define LIS3DH_ACC_I2C_SAD_H	((LIS3DH_ACC_I2C_SADROOT<<1)|SAD0H)
#define	LIS3DH_ACC_DEV_NAME	"lis3dh_acc_misc"


#define	LIS3DH_ACC_IOCTL_BASE 77
/** The following define the IOCTL command values via the ioctl macros */
#define	LIS3DH_ACC_IOCTL_SET_DELAY		_IOW(LIS3DH_ACC_IOCTL_BASE, 0, int)
#define	LIS3DH_ACC_IOCTL_GET_DELAY		_IOR(LIS3DH_ACC_IOCTL_BASE, 1, int)
#define	LIS3DH_ACC_IOCTL_SET_ENABLE		_IOW(LIS3DH_ACC_IOCTL_BASE, 2, int)
#define	LIS3DH_ACC_IOCTL_GET_ENABLE		_IOR(LIS3DH_ACC_IOCTL_BASE, 3, int)
#define	LIS3DH_ACC_IOCTL_SET_FULLSCALE		_IOW(LIS3DH_ACC_IOCTL_BASE, 4, int)
#define	LIS3DH_ACC_IOCTL_SET_G_RANGE		LIS3DH_ACC_IOCTL_SET_FULLSCALE

#define	LIS3DH_ACC_IOCTL_SET_CTRL_REG3		_IOW(LIS3DH_ACC_IOCTL_BASE, 6, int)
#define	LIS3DH_ACC_IOCTL_SET_CTRL_REG6		_IOW(LIS3DH_ACC_IOCTL_BASE, 7, int)
#define	LIS3DH_ACC_IOCTL_SET_DURATION1		_IOW(LIS3DH_ACC_IOCTL_BASE, 8, int)
#define	LIS3DH_ACC_IOCTL_SET_THRESHOLD1		_IOW(LIS3DH_ACC_IOCTL_BASE, 9, int)
#define	LIS3DH_ACC_IOCTL_SET_CONFIG1		_IOW(LIS3DH_ACC_IOCTL_BASE, 10, int)

#define	LIS3DH_ACC_IOCTL_SET_DURATION2		_IOW(LIS3DH_ACC_IOCTL_BASE, 11, int)
#define	LIS3DH_ACC_IOCTL_SET_THRESHOLD2		_IOW(LIS3DH_ACC_IOCTL_BASE, 12, int)
#define	LIS3DH_ACC_IOCTL_SET_CONFIG2		_IOW(LIS3DH_ACC_IOCTL_BASE, 13, int)

#define	LIS3DH_ACC_IOCTL_GET_SOURCE1		_IOW(LIS3DH_ACC_IOCTL_BASE, 14, int)
#define	LIS3DH_ACC_IOCTL_GET_SOURCE2		_IOW(LIS3DH_ACC_IOCTL_BASE, 15, int)

#define	LIS3DH_ACC_IOCTL_GET_TAP_SOURCE		_IOW(LIS3DH_ACC_IOCTL_BASE, 16, int)

#define	LIS3DH_ACC_IOCTL_SET_TAP_TW		_IOW(LIS3DH_ACC_IOCTL_BASE, 17, int)
#define	LIS3DH_ACC_IOCTL_SET_TAP_CFG		_IOW(LIS3DH_ACC_IOCTL_BASE, 18, int)
#define	LIS3DH_ACC_IOCTL_SET_TAP_TLIM		_IOW(LIS3DH_ACC_IOCTL_BASE, 19, int)
#define	LIS3DH_ACC_IOCTL_SET_TAP_THS		_IOW(LIS3DH_ACC_IOCTL_BASE, 20, int)
#define	LIS3DH_ACC_IOCTL_SET_TAP_TLAT		_IOW(LIS3DH_ACC_IOCTL_BASE, 21, int)




/************************************************/
/* 	Accelerometer defines section	 	*/
/************************************************/

/* Accelerometer Sensor Full Scale */
#define	LIS3DH_ACC_FS_MASK		0x30
#define LIS3DH_ACC_G_2G 		0x00
#define LIS3DH_ACC_G_4G 		0x10
#define LIS3DH_ACC_G_8G 		0x20
#define LIS3DH_ACC_G_16G		0x30


/* Accelerometer Sensor Operating Mode */
#define LIS3DH_ACC_ENABLE		0x01
#define LIS3DH_ACC_DISABLE		0x00






#ifdef	__KERNEL__
struct lis3dh_acc_platform_data {
	int poll_interval;
	int min_interval;

	u8 g_range;

	u8 axis_map_x;
	u8 axis_map_y;
	u8 axis_map_z;

	u8 negate_x;
	u8 negate_y;
	u8 negate_z;

	int (*init)(void);
	void (*exit)(void);
	int (*power_on)(void);
	int (*power_off)(void);

	int gpio_int1;
	int gpio_int2;

};
#endif	/* __KERNEL__ */

#endif	/* __LIS3DH_H__ */



