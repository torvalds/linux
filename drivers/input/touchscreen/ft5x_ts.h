/*
 * drivers/input/touchscreen/ft5x_ts.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef __LINUX_FT5X_TS_H__
#define __LINUX_FT5X_TS_H__

// gpio base address
#define CONFIG_FT5X0X_MULTITOUCH     (1)
#define CALIBRATION  (1)
#define UPGRADE   (5)
//#define CALIBRATION _IO(CALIBRATION_FLAG,0)
//#define UPDRAGE _IO(UPDRAGE_FLAG,0)
#define I2C_MINORS 	256
#define I2C_MAJOR 	125
                                
#undef  AW_GPIO_INT_API_ENABLE
//#define AW_FPGA_SIM
#ifdef AW_FPGA_SIM
#endif

#define AW_GPIO_API_ENABLE
//#undef CONFIG_HAS_EARLYSUSPEND
//#define CONFIG_HAS_EARLYSUSPEND
struct ft5x_ts_platform_data{
	u16	intr;		/* irq number	*/
};

enum ft5x_ts_regs {
	FT5X0X_REG_PMODE	= 0xA5,	/* Power Consume Mode		*/	
};

//FT5X0X_REG_PMODE
#define PMODE_ACTIVE        0x00
#define PMODE_MONITOR       0x01
#define PMODE_STANDBY       0x02
#define PMODE_HIBERNATE     0x03


#ifndef ABS_MT_TOUCH_MAJOR
    #define ABS_MT_TOUCH_MAJOR	0x30	/* touching ellipse */
    #define ABS_MT_TOUCH_MINOR	0x31	/* (omit if circular) */
    #define ABS_MT_WIDTH_MAJOR	0x32	/* approaching ellipse */
    #define ABS_MT_WIDTH_MINOR	0x33	/* (omit if circular) */
    #define ABS_MT_ORIENTATION	0x34	/* Ellipse orientation */
    #define ABS_MT_POSITION_X	0x35	/* Center X ellipse position */
    #define ABS_MT_POSITION_Y	0x36	/* Center Y ellipse position */
    #define ABS_MT_TOOL_TYPE	0x37	/* Type of touching device */
    #define ABS_MT_BLOB_ID		0x38	/* Group set of pkts as blob */
#endif /* ABS_MT_TOUCH_MAJOR */


#endif

