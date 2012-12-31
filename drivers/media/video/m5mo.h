/*
 * Driver for M5MO (5MP Camera) from NEC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __M5MO_H
#define __M5MO_H

#define CONFIG_CAM_DEBUG	1

#define cam_warn(fmt, ...)	\
	do { \
		printk(KERN_WARNING "%s: " fmt, __func__, ##__VA_ARGS__); \
	} while (0)

#define cam_err(fmt, ...)	\
	do { \
		printk(KERN_ERR "%s: " fmt, __func__, ##__VA_ARGS__); \
	} while (0)

#define cam_info(fmt, ...)	\
	do { \
		printk(KERN_INFO "%s: " fmt, __func__, ##__VA_ARGS__); \
	} while (0)

#ifdef CONFIG_CAM_DEBUG
#define CAM_DEBUG	(1 << 0)
#define CAM_TRACE	(1 << 1)
#define CAM_I2C		(1 << 2)

#define cam_dbg(fmt, ...)	\
	do { \
		if (to_state(sd)->dbg_level & CAM_DEBUG) \
			printk(KERN_DEBUG "%s: " fmt, __func__, ##__VA_ARGS__); \
	} while (0)

#define cam_trace(fmt, ...)	\
	do { \
		if (to_state(sd)->dbg_level & CAM_TRACE) \
			printk(KERN_DEBUG "%s: " fmt, __func__, ##__VA_ARGS__); \
	} while (0)

#define cam_i2c_dbg(fmt, ...)	\
	do { \
		if (to_state(sd)->dbg_level & CAM_I2C) \
			printk(KERN_DEBUG "%s: " fmt, __func__, ##__VA_ARGS__); \
	} while (0)
#else
#define cam_dbg(fmt, ...)
#define cam_trace(fmt, ...)
#define cam_i2c_dbg(fmt, ...)
#endif

enum m5mo_prev_frmsize {
	M5MO_PREVIEW_QCIF,
	M5MO_PREVIEW_QCIF2,
	M5MO_PREVIEW_QVGA,
	M5MO_PREVIEW_VGA,
	M5MO_PREVIEW_D1,
	M5MO_PREVIEW_WVGA,
	M5MO_PREVIEW_720P,
	M5MO_PREVIEW_1080P,
	M5MO_PREVIEW_HDR,
};

enum m5mo_cap_frmsize {
	M5MO_CAPTURE_VGA,	/* 640 x 480 */
	M5MO_CAPTURE_WVGA,	/* 800 x 480 */
	M5MO_CAPTURE_W1MP,	/* 1600 x 960 */
	M5MO_CAPTURE_2MP,	/* UXGA - 1600 x 1200 */
	M5MO_CAPTURE_W2MP,	/* 2048 x 1232 */
	M5MO_CAPTURE_3MP,	/* QXGA - 2048 x 1536 */
	M5MO_CAPTURE_W4MP,	/* WQXGA - 2560 x 1536 */
	M5MO_CAPTURE_5MP,	/* 2560 x 1920 */
	M5MO_CAPTURE_W6MP,	/* 3072 x 1856 */
	M5MO_CAPTURE_7MP,	/* 3072 x 2304 */
	M5MO_CAPTURE_W7MP,	/* WQXGA - 2560 x 1536 */
	M5MO_CAPTURE_8MP,	/* 3264 x 2448 */
};

struct m5mo_control {
	u32 id;
	s32 value;
	s32 minimum;		/* Note signedness */
	s32 maximum;
	s32 step;
	s32 default_value;
};

struct m5mo_frmsizeenum {
	unsigned int index;
	unsigned int width;
	unsigned int height;
	u8 reg_val;		/* a value for category parameter */
};

struct m5mo_isp {
	wait_queue_head_t wait;
	unsigned int irq;	/* irq issued by ISP */
	unsigned int issued;
	unsigned int int_factor;
	unsigned int bad_fw:1;
};

struct m5mo_jpeg {
	int quality;
	unsigned int main_size;	/* Main JPEG file size */
	unsigned int thumb_size;	/* Thumbnail file size */
	unsigned int main_offset;
	unsigned int thumb_offset;
	unsigned int postview_offset;
};

struct m5mo_focus {
	unsigned int mode;
	unsigned int lock;
	unsigned int status;
	unsigned int touch;
	unsigned int pos_x;
	unsigned int pos_y;
};

struct m5mo_exif {
	char unique_id[7];
	u32 exptime;		/* us */
	u16 flash;
	u16 iso;
	int tv;			/* shutter speed */
	int bv;			/* brightness */
	int ebv;		/* exposure bias */
};

struct m5mo_state {
	struct m5mo_platform_data *pdata;
	struct v4l2_subdev sd;

	struct m5mo_isp isp;

	const struct m5mo_frmsizeenum *preview;
	const struct m5mo_frmsizeenum *capture;

	enum v4l2_pix_format_mode format_mode;
	enum v4l2_sensor_mode sensor_mode;
	enum v4l2_flash_mode flash_mode;
	int vt_mode;
	int beauty_mode;
	int zoom;

	unsigned int fps;
	struct m5mo_focus focus;

	struct m5mo_jpeg jpeg;
	struct m5mo_exif exif;

	int check_dataline;
	char *fw_version;

#ifdef CONFIG_CAM_DEBUG
	u8 dbg_level;
#endif
};

/* Category */
#define M5MO_CATEGORY_SYS	0x00
#define M5MO_CATEGORY_PARM	0x01
#define M5MO_CATEGORY_MON	0x02
#define M5MO_CATEGORY_AE	0x03
#define M5MO_CATEGORY_WB	0x06
#define M5MO_CATEGORY_EXIF	0x07
#define M5MO_CATEGORY_FD	0x09
#define M5MO_CATEGORY_LENS	0x0A
#define M5MO_CATEGORY_CAPPARM	0x0B
#define M5MO_CATEGORY_CAPCTRL	0x0C
#define M5MO_CATEGORY_TEST	0x0D
#define M5MO_CATEGORY_ADJST	0x0E
#define M5MO_CATEGORY_FLASH	0x0F    /* F/W update */

/* M5MO_CATEGORY_SYS: 0x00 */
#define M5MO_SYS_PJT_CODE	0x01
#define M5MO_SYS_VER_FW		0x02
#define M5MO_SYS_VER_HW		0x04
#define M5MO_SYS_VER_PARAM	0x06
#define M5MO_SYS_VER_AWB	0x08
#define M5MO_SYS_USER_VER	0x0A
#define M5MO_SYS_MODE		0x0B
#define M5MO_SYS_ESD_INT	0x0E
#define M5MO_SYS_INT_FACTOR	0x10
#define M5MO_SYS_INT_EN		0x11
#define M5MO_SYS_ROOT_EN	0x12

/* M5MO_CATEGORY_PARAM: 0x01 */
#define M5MO_PARM_OUT_SEL	0x00
#define M5MO_PARM_MON_SIZE	0x01
#define M5MO_PARM_EFFECT	0x0B
#define M5MO_PARM_FLEX_FPS	0x31
#define M5MO_PARM_HDMOVIE	0x32
#define M5MO_PARM_HDR_MON	0x39
#define M5MO_PARM_HDR_MON_OFFSET_EV	0x3A

/* M5MO_CATEGORY_MON: 0x02 */
#define M5MO_MON_ZOOM		0x01
#define M5MO_MON_MON_REVERSE	0x05
#define M5MO_MON_MON_MIRROR	0x06
#define M5MO_MON_SHOT_REVERSE	0x07
#define M5MO_MON_SHOT_MIRROR	0x08
#define M5MO_MON_CFIXB		0x09
#define M5MO_MON_CFIXR		0x0A
#define M5MO_MON_COLOR_EFFECT	0x0B
#define M5MO_MON_CHROMA_LVL	0x0F
#define M5MO_MON_EDGE_LVL	0x11
#define M5MO_MON_TONE_CTRL	0x25

/* M5MO_CATEGORY_AE: 0x03 */
#define M5MO_AE_LOCK		0x00
#define M5MO_AE_MODE		0x01
#define M5MO_AE_ISOSEL		0x05
#define M5MO_AE_FLICKER		0x06
#define M5MO_AE_EP_MODE_MON	0x0A
#define M5MO_AE_EP_MODE_CAP	0x0B
#define M5MO_AE_ONESHOT_MAX_EXP	0x36
#define M5MO_AE_INDEX		0x38

/* M5MO_CATEGORY_WB: 0x06 */
#define M5MO_AWB_LOCK		0x00
#define M5MO_WB_AWB_MODE	0x02
#define M5MO_WB_AWB_MANUAL	0x03

/* M5MO_CATEGORY_EXIF: 0x07 */
#define M5MO_EXIF_EXPTIME_NUM	0x00
#define M5MO_EXIF_EXPTIME_DEN	0x04
#define M5MO_EXIF_TV_NUM	0x08
#define M5MO_EXIF_TV_DEN	0x0C
#define M5MO_EXIF_BV_NUM	0x18
#define M5MO_EXIF_BV_DEN	0x1C
#define M5MO_EXIF_EBV_NUM	0x20
#define M5MO_EXIF_EBV_DEN	0x24
#define M5MO_EXIF_ISO		0x28
#define M5MO_EXIF_FLASH		0x2A

/* M5MO_CATEGORY_FD: 0x09 */
#define M5MO_FD_CTL		0x00
#define M5MO_FD_SIZE		0x01
#define M5MO_FD_MAX		0x02

/* M5MO_CATEGORY_LENS: 0x0A */
#define M5MO_LENS_AF_MODE	0x01
#define M5MO_LENS_AF_START	0x02
#define M5MO_LENS_AF_STATUS	0x03
#define M5MO_LENS_AF_UPBYTE_STEP	0x06
#define M5MO_LENS_AF_LOWBYTE_STEP	0x07
#define M5MO_LENS_AF_CAL	0x1D
#define M5MO_LENS_AF_TOUCH_POSX	0x30
#define M5MO_LENS_AF_TOUCH_POSY	0x32

/* M5MO_CATEGORY_CAPPARM: 0x0B */
#define M5MO_CAPPARM_YUVOUT_MAIN	0x00
#define M5MO_CAPPARM_MAIN_IMG_SIZE	0x01
#define M5MO_CAPPARM_YUVOUT_PREVIEW	0x05
#define M5MO_CAPPARM_PREVIEW_IMG_SIZE	0x06
#define M5MO_CAPPARM_YUVOUT_THUMB	0x0A
#define M5MO_CAPPARM_THUMB_IMG_SIZE	0x0B
#define M5MO_CAPPARM_JPEG_SIZE_MAX	0x0F
#define M5MO_CAPPARM_JPEG_RATIO		0x17
#define M5MO_CAPPARM_MCC_MODE		0x1D
#define M5MO_CAPPARM_WDR_EN		0x2C
#define M5MO_CAPPARM_LIGHT_CTRL		0x40
#define M5MO_CAPPARM_FLASH_CTRL		0x41
#define M5MO_CAPPARM_JPEG_RATIO_OFS	0x34
#define M5MO_CAPPARM_THUMB_JPEG_MAX	0x3C
#define M5MO_CAPPARM_AFB_CAP_EN		0x53

/* M5MO_CATEGORY_CAPCTRL: 0x0C */
#define M5MO_CAPCTRL_FRM_SEL	0x06
#define M5MO_CAPCTRL_TRANSFER	0x09
#define M5MO_CAPCTRL_IMG_SIZE	0x0D
#define M5MO_CAPCTRL_THUMB_SIZE	0x11

/* M5MO_CATEGORY_ADJST: 0x0E */
#define M5MO_ADJST_AWB_RG_H	0x3B
#define M5MO_ADJST_AWB_RG_L	0x3D
#define M5MO_ADJST_AWB_BG_H	0x3E
#define M5MO_ADJST_AWB_BG_L	0x3F

/* M5MO_CATEGORY_FLASH: 0x0F */
#define M5MO_FLASH_ADDR		0x00
#define M5MO_FLASH_BYTE		0x04
#define M5MO_FLASH_ERASE	0x06
#define M5MO_FLASH_WR		0x07
#define M5MO_FLASH_RAM_CLEAR	0x08
#define M5MO_FLASH_CAM_START	0x12
#define M5MO_FLASH_SEL		0x13

/* M5MO_CATEGORY_TEST:	0x0D */
#define M5MO_TEST_OUTPUT_YCO_TEST_DATA		0x1B
#define M5MO_TEST_ISP_PROCESS			0x59

/* M5MO Sensor Mode */
#define M5MO_SYSINIT_MODE	0x0
#define M5MO_PARMSET_MODE	0x1
#define M5MO_MONITOR_MODE	0x2
#define M5MO_STILLCAP_MODE	0x3

/* Interrupt Factor */
#define M5MO_INT_SOUND		(1 << 7)
#define M5MO_INT_LENS_INIT	(1 << 6)
#define M5MO_INT_FD		(1 << 5)
#define M5MO_INT_FRAME_SYNC	(1 << 4)
#define M5MO_INT_CAPTURE	(1 << 3)
#define M5MO_INT_ZOOM		(1 << 2)
#define M5MO_INT_AF		(1 << 1)
#define M5MO_INT_MODE		(1 << 0)

/* ESD Interrupt */
#define M5MO_INT_ESD		(1 << 0)

#endif /* __M5MO_H */
