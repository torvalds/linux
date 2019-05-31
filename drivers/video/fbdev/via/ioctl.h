/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 1998-2008 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.

 */

#ifndef __IOCTL_H__
#define __IOCTL_H__

#ifndef __user
#define __user
#endif

/* VIAFB IOCTL definition */
#define VIAFB_GET_INFO_SIZE		0x56494101	/* 'VIA\01' */
#define VIAFB_GET_INFO			0x56494102	/* 'VIA\02' */
#define VIAFB_HOTPLUG			0x56494103	/* 'VIA\03' */
#define VIAFB_SET_HOTPLUG_FLAG		0x56494104	/* 'VIA\04' */
#define VIAFB_GET_RESOLUTION		0x56494105	/* 'VIA\05' */
#define VIAFB_GET_SAMM_INFO		0x56494107	/* 'VIA\07' */
#define VIAFB_TURN_ON_OUTPUT_DEVICE     0x56494108	/* 'VIA\08' */
#define VIAFB_TURN_OFF_OUTPUT_DEVICE    0x56494109	/* 'VIA\09' */
#define VIAFB_GET_DEVICE		0x5649410B
#define VIAFB_GET_DRIVER_VERSION	0x56494112	/* 'VIA\12' */
#define VIAFB_GET_CHIP_INFO		0x56494113	/* 'VIA\13' */
#define VIAFB_GET_DEVICE_INFO           0x56494115

#define VIAFB_GET_DEVICE_SUPPORT	0x56494118
#define VIAFB_GET_DEVICE_CONNECT	0x56494119
#define VIAFB_GET_PANEL_SUPPORT_EXPAND	0x5649411A
#define VIAFB_GET_DRIVER_NAME		0x56494122
#define VIAFB_GET_DEVICE_SUPPORT_STATE	0x56494123
#define VIAFB_GET_GAMMA_LUT		0x56494124
#define VIAFB_SET_GAMMA_LUT		0x56494125
#define VIAFB_GET_GAMMA_SUPPORT_STATE	0x56494126
#define VIAFB_SYNC_SURFACE		0x56494130
#define VIAFB_GET_DRIVER_CAPS		0x56494131
#define VIAFB_GET_IGA_SCALING_INFO	0x56494132
#define VIAFB_GET_PANEL_MAX_SIZE	0x56494133
#define VIAFB_GET_PANEL_MAX_POSITION	0x56494134
#define VIAFB_SET_PANEL_SIZE		0x56494135
#define VIAFB_SET_PANEL_POSITION        0x56494136
#define VIAFB_GET_PANEL_POSITION        0x56494137
#define VIAFB_GET_PANEL_SIZE		0x56494138

#define None_Device 0x00
#define CRT_Device  0x01
#define LCD_Device  0x02
#define DVI_Device  0x08
#define CRT2_Device 0x10
#define LCD2_Device 0x40

#define OP_LCD_CENTERING   0x01
#define OP_LCD_PANEL_ID    0x02
#define OP_LCD_MODE        0x03

/*SAMM operation flag*/
#define OP_SAMM            0x80

#define LCD_PANEL_ID_MAXIMUM	23

#define STATE_ON            0x1
#define STATE_OFF           0x0
#define STATE_DEFAULT       0xFFFF

#define MAX_ACTIVE_DEV_NUM  2

struct device_t {
	unsigned short crt:1;
	unsigned short dvi:1;
	unsigned short lcd:1;
	unsigned short samm:1;
	unsigned short lcd_dsp_cent:1;
	unsigned char lcd_mode:1;
	unsigned short epia_dvi:1;
	unsigned short lcd_dual_edge:1;
	unsigned short lcd2:1;

	unsigned short primary_dev;
	unsigned char lcd_panel_id;
	unsigned short xres, yres;
	unsigned short xres1, yres1;
	unsigned short refresh;
	unsigned short bpp;
	unsigned short refresh1;
	unsigned short bpp1;
	unsigned short sequence;
	unsigned short bus_width;
};

struct viafb_ioctl_info {
	u32 viafb_id;		/* for identifying viafb */
#define VIAID       0x56494146	/* Identify myself with 'VIAF' */
	u16 vendor_id;
	u16 device_id;
	u8 version;
	u8 revision;
	u8 reserved[246];	/* for future use */
};

struct viafb_ioctl_mode {
	u32 xres;
	u32 yres;
	u32 refresh;
	u32 bpp;
	u32 xres_sec;
	u32 yres_sec;
	u32 virtual_xres_sec;
	u32 virtual_yres_sec;
	u32 refresh_sec;
	u32 bpp_sec;
};
struct viafb_ioctl_samm {
	u32 samm_status;
	u32 size_prim;
	u32 size_sec;
	u32 mem_base;
	u32 offset_sec;
};

struct viafb_driver_version {
	int iMajorNum;
	int iKernelNum;
	int iOSNum;
	int iMinorNum;
};

struct viafb_ioctl_lcd_attribute {
	unsigned int panel_id;
	unsigned int display_center;
	unsigned int lcd_mode;
};

struct viafb_ioctl_setting {
	/* Enable or disable active devices */
	unsigned short device_flag;
	/* Indicate which device should be turn on or turn off. */
	unsigned short device_status;
	unsigned int reserved;
	/* Indicate which LCD's attribute can be changed. */
	unsigned short lcd_operation_flag;
	/* 1: SAMM ON  0: SAMM OFF */
	unsigned short samm_status;
	/* horizontal resolution of first device */
	unsigned short first_dev_hor_res;
	/* vertical resolution of first device */
	unsigned short first_dev_ver_res;
	/* horizontal resolution of second device */
	unsigned short second_dev_hor_res;
	/* vertical resolution of second device */
	unsigned short second_dev_ver_res;
	/* refresh rate of first device */
	unsigned short first_dev_refresh;
	/* bpp of first device */
	unsigned short first_dev_bpp;
	/* refresh rate of second device */
	unsigned short second_dev_refresh;
	/* bpp of second device */
	unsigned short second_dev_bpp;
	/* Indicate which device are primary display device. */
	unsigned int primary_device;
	unsigned int struct_reserved[35];
	struct viafb_ioctl_lcd_attribute lcd_attributes;
};

struct _UTFunctionCaps {
	unsigned int dw3DScalingState;
	unsigned int reserved[31];
};

struct _POSITIONVALUE {
	unsigned int dwX;
	unsigned int dwY;
};

struct _panel_size_pos_info {
	unsigned int device_type;
	int x;
	int y;
};

extern int viafb_LCD_ON;
extern int viafb_DVI_ON;

int viafb_ioctl_get_viafb_info(u_long arg);
int viafb_ioctl_hotplug(int hres, int vres, int bpp);

#endif /* __IOCTL_H__ */
