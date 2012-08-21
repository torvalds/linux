/*
 * drivers/input/touchscreen/sitronix_i2c_touch.h
 *
 * Touchscreen driver for Sitronix
 *
 * Copyright (C) 2011 Sitronix Technology Co., Ltd.
 *	Rudy Huang <rudy_huang@sitronix.com.tw>
 */
/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#ifndef __SITRONIX_I2C_TOUCH_h
#define __SITRONIX_I2C_TOUCH_h

#include <linux/ioctl.h> /* needed for the _IOW etc stuff used later */

#define SITRONIX_TOUCH_DRIVER_VERSION 0x03
#define SITRONIX_MAX_SUPPORTED_POINT 5
#define SITRONIX_I2C_TOUCH_DRV_NAME "sitronix"
#define SITRONIX_I2C_TOUCH_DEV_NAME "sitronixDev"

//#define SITRONIX_WAKE_UP_TOUCH_BY_INT

//#define SITRONIX_MONITOR_THREAD
#define DELAY_MONITOR_THREAD_START_PROBE 10000
#define DELAY_MONITOR_THREAD_START_RESUME 3000
#define SITRONIX_FW_UPGRADE_FEATURE
#define SITRONIX_LEVEL_TRIGGERED
// When enable_irq() is invoked, irq will be sent once while INT is not triggered if CONFIG_HARDIRQS_SW_RESEND is set.
// This behavior is implemented by linux kernel, it is used to prevent irq from losting when irq is edge-triggered mode.
#ifndef SITRONIX_LEVEL_TRIGGERED
#define SITRONIX_INT_POLLING_MODE
#define INT_POLLING_MODE_INTERVAL 14
#endif // SITRONIX_LEVEL_TRIGGERED
#define SITRONIX_FINGER_COUNT_REG_ENABLE

//#define EnableDbgMsg 0
//#define EnableUpgradeMsg 1

#ifdef EnableDbgMsg
#define DbgMsg(arg...) printk(arg)
#else
#define DbgMsg(arg...)
#endif

#ifdef EnableUpgradeMsg
#define UpgradeMsg(arg...) printk(arg)
#else
#define UpgradeMsg(arg...)
#endif

typedef enum{
	FIRMWARE_VERSION,
	STATUS_REG,
	DEVICE_CONTROL_REG,
	TIMEOUT_TO_IDLE_REG,
	XY_RESOLUTION_HIGH,
	X_RESOLUTION_LOW,
	Y_RESOLUTION_LOW,
	FIRMWARE_REVISION_3 = 0x0C,
	FIRMWARE_REVISION_2,
	FIRMWARE_REVISION_1,
	FIRMWARE_REVISION_0,
	FINGERS,
	KEYS_REG,
	XY0_COORD_H,
	X0_COORD_L,
	Y0_COORD_L,
	I2C_PROTOCOL = 0x3E,
	MAX_NUM_TOUCHES,
	DATA_0_HIGH,
	DATA_0_LOW,

	PAGE_REG = 0xff,
}RegisterOffset;

#define SITRONIX_TS_CHANGE_MODE_DELAY 150

typedef enum{
	XY_COORD_H,
	X_COORD_L,
	Y_COORD_L,
	PIXEL_DATA_LENGTH_B,
	PIXEL_DATA_LENGTH_A,
}PIXEL_DATA_FORMAT;

#define X_RES_H_SHFT 4
#define X_RES_H_BMSK 0xf
#define Y_RES_H_SHFT 0
#define Y_RES_H_BMSK 0xf
#define FINGERS_SHFT 0
#define FINGERS_BMSK 0xf
#define X_COORD_VALID_SHFT 7
#define X_COORD_VALID_BMSK 0x1
#define X_COORD_H_SHFT 4
#define X_COORD_H_BMSK 0x7
#define Y_COORD_H_SHFT 0
#define Y_COORD_H_BMSK 0x7

typedef enum{
	SITRONIX_A_TYPE = 1,
	SITRONIX_B_TYPE,
}I2C_PROTOCOL_TYPE;

#define I2C_PROTOCOL_SHFT 0x0
#define I2C_PROTOCOL_BMSK 0x3

#define SMT_IOC_MAGIC   0xf1

enum{
	SMT_GET_DRIVER_REVISION = 1,
	SMT_GET_FW_REVISION,
	SMT_ENABLE_IRQ,
	SMT_DISABLE_IRQ,
	SMT_RESUME,
	SMT_SUSPEND,
	SMT_HW_RESET,
	SMT_IOC_MAXNR,
};

#define IOCTL_SMT_GET_DRIVER_REVISION				_IOC(_IOC_READ,  SMT_IOC_MAGIC, SMT_GET_DRIVER_REVISION, 			1)
#define IOCTL_SMT_GET_FW_REVISION					_IOC(_IOC_READ,  SMT_IOC_MAGIC, SMT_GET_FW_REVISION, 				4)
#define IOCTL_SMT_ENABLE_IRQ					_IOC(_IOC_NONE, SMT_IOC_MAGIC, SMT_ENABLE_IRQ, 0)
#define IOCTL_SMT_DISABLE_IRQ					_IOC(_IOC_NONE, SMT_IOC_MAGIC, SMT_DISABLE_IRQ, 0)
#define IOCTL_SMT_RESUME					_IOC(_IOC_NONE, SMT_IOC_MAGIC, SMT_RESUME, 0)
#define IOCTL_SMT_SUSPEND					_IOC(_IOC_NONE, SMT_IOC_MAGIC, SMT_SUSPEND, 0)
#define IOCTL_SMT_HW_RESET					_IOC(_IOC_NONE, SMT_IOC_MAGIC, SMT_HW_RESET, 0)

#define SITRONIX_SENSOR_KEY
//#define SITRONIX_TOUCH_KEY
//#define SITRONIX_KEY_BOUNDARY_MANUAL_SPECIFY

typedef struct _tag_MTD_STRUCTURE{
	u16 Pixel_X;
	u16 Pixel_Y;
	u8 First_Pressed_area; /* 0: no press; 1: display; 2: touch key */
	u8 Current_Pressed_area; /* 0: no press; 1: display; 2: touch key */
	unsigned int First_key_index; 
	unsigned int Current_key_index; 
}MTD_STRUCTURE, *PMTD_STRUCTURE;

#ifndef SITRONIX_TOUCH_KEY
enum{
	AREA_NONE,
	AREA_DISPLAY,
};
#else
enum{
	AREA_NONE,
	AREA_DISPLAY,
	AREA_KEY,
	AREA_INVALID,
};

struct sitronix_AA_key{
	int x_low;
	int x_high;
	int y_low;
	int y_high;
	unsigned int code;
};
#endif // SITRONIX_TOUCH_KEY

struct sitronix_i2c_touch_platform_data {
	uint32_t version;	/* Use this entry for panels with */
				/* (major << 8 | minor) version or above. */
				/* If non-zero another array entry follows */
	void (*reset_ic)(void);
};

#endif // __SITRONIX_I2C_TOUCH_h
