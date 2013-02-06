/*
 * Driver for M9MO (16MP Camera) from NEC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __M9MO_H
#define __M9MO_H

#include <linux/wakelock.h>

#define CONFIG_CAM_DEBUG

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
#define CAM_DEBUG   (1 << 0)
#define CAM_TRACE   (1 << 1)
#define CAM_I2C     (1 << 2)

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
#define FRM_RATIO(x)    ((x)->width*10/(x)->height)

u8 buf_port_seting0[] = {
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F,
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07,
		  0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF,
		 };
u8 buf_port_seting1[] = {
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		 };
u8 buf_port_seting2[] = {
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C,
		  0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x10,
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		 };

enum m9mo_prev_frmsize {
	M9MO_PREVIEW_QCIF,
	M9MO_PREVIEW_QCIF2,
	M9MO_PREVIEW_QVGA,
	M9MO_PREVIEW_VGA,
	M9MO_PREVIEW_D1,
	M9MO_PREVIEW_WVGA,
	M9MO_PREVIEW_960_720,
	M9MO_PREVIEW_1080_720,
	M9MO_PREVIEW_720P,
	M9MO_PREVIEW_1080P,
	M9MO_PREVIEW_HDR,
	M9MO_PREVIEW_720P_60FPS,
	M9MO_PREVIEW_VGA_60FPS,
	M9MO_PREVIEW_1080P_DUAL,
	M9MO_PREVIEW_720P_DUAL,
	M9MO_PREVIEW_VGA_DUAL,
	M9MO_PREVIEW_QVGA_DUAL,
	M9MO_PREVIEW_1440_1080,
};

enum m9mo_cap_frmsize {
	M9MO_CAPTURE_HD,	/* 960 x 720 */
	M9MO_CAPTURE_1MP,	/* 1024 x 768 */
	M9MO_CAPTURE_2MPW,	/* 1920 x 1080 */
	M9MO_CAPTURE_3MP,	/* 1984 x 1488 */
	M9MO_CAPTURE_4MP,	/* 2304 x 1728 */
	M9MO_CAPTURE_5MP,	/* 2592 x 1944 */
	M9MO_CAPTURE_8MP,	/* 3264 x 2448 */
	M9MO_CAPTURE_10MP,	/* 3648 x 2736 */
	M9MO_CAPTURE_12MPW,	/* 4608 x 2592 */
	M9MO_CAPTURE_14MP,	/* 4608 x 3072 */
	M9MO_CAPTURE_16MP,	/* 4608 x 3456 */
	M9MO_CAPTURE_RAW,	/* 4088 x 2500 */
};

enum m9mo_post_frmsize {
	M9MO_CAPTURE_POSTQVGA,	/* 320 x 240 */
	M9MO_CAPTURE_POSTVGA,	/* 640 x 480 */
	M9MO_CAPTURE_POSTHD,	/* 960 x 720 */
	M9MO_CAPTURE_POSTP,	/* 1056 x 704 */
	M9MO_CAPTURE_POSTWVGA,	/* 800 x 480 */
	M9MO_CAPTURE_POSTWHD,	/* 1280 x 720 */
};

enum cam_frmratio {
	CAM_FRMRATIO_QCIF   = 12,   /* 11 : 9 */
	CAM_FRMRATIO_VGA    = 13,   /* 4 : 3 */
	CAM_FRMRATIO_D1     = 15,   /* 3 : 2 */
	CAM_FRMRATIO_WVGA   = 16,   /* 5 : 3 */
	CAM_FRMRATIO_HD     = 17,   /* 16 : 9 */
};

struct m9mo_control {
	u32 id;
	s32 value;
	s32 minimum;		/* Note signedness */
	s32 maximum;
	s32 step;
	s32 default_value;
};

struct m9mo_frmsizeenum {
	unsigned int index;
	unsigned int width;
	unsigned int height;
	u8 reg_val;		/* a value for category parameter */
};

struct m9mo_isp {
	wait_queue_head_t wait;
	unsigned int irq;	/* irq issued by ISP */
	unsigned int issued;
	unsigned int int_factor;
	unsigned int bad_fw:1;
};

struct m9mo_jpeg {
	int quality;
	unsigned int main_size;	/* Main JPEG file size */
	unsigned int thumb_size;	/* Thumbnail file size */
	unsigned int main_offset;
	unsigned int thumb_offset;
	unsigned int postview_offset;
};

struct m9mo_focus {
	unsigned int start:1;
	unsigned int lock:1;
	unsigned int touch:1;

	unsigned int mode;
#if defined(CONFIG_TARGET_LOCALE_NA)
	unsigned int ui_mode;
	unsigned int mode_select;
#endif
	unsigned int status;

	unsigned int pos_x;
	unsigned int pos_y;
};

struct m9mo_factory_punt_data {
	unsigned int min;
	unsigned int max;
	unsigned int num;
};

struct m9mo_factory_zoom_data {
	unsigned int range_min;
	unsigned int range_max;
	unsigned int slope_min;
	unsigned int slope_max;
};

struct m9mo_factory_zoom_slope_data {
	unsigned int min;
	unsigned int max;
};

struct m9mo_exif {
	char unique_id[7];
	u32 exptime;		/* us */
	u16 flash;
	u16 iso;
	int tv;			/* shutter speed */
	int bv;			/* brightness */
	int ebv;		/* exposure bias */
	int av;			/* Aperture */
	int focal_length;
	int focal_35mm_length;
};

struct m9mo_state {
	struct m9mo_platform_data *pdata;
	struct device *m9mo_dev;
	struct v4l2_subdev sd;

	struct wake_lock wake_lock;

	struct m9mo_isp isp;

	const struct m9mo_frmsizeenum *preview;
	const struct m9mo_frmsizeenum *capture;
	const struct m9mo_frmsizeenum *postview;

	enum v4l2_pix_format_mode format_mode;
	enum v4l2_sensor_mode sensor_mode;
	enum v4l2_flash_mode flash_mode;
	enum v4l2_scene_mode scene_mode;
	int vt_mode;
	int samsung_app;
	int zoom;
	int smart_zoom_mode;

	int m9mo_fw_done;
	int fw_info_done;

	int start_cap_kind;
	unsigned int fps;

	int factory_down_check;
	int factory_result_check;
	int factory_end_check;
	int factory_category;
	int factory_byte;
	int factory_value;
	int factory_value_size;
	int factory_end_interrupt;

	struct m9mo_focus focus;
	struct m9mo_factory_punt_data f_punt_data;
	struct m9mo_factory_zoom_data f_zoom_data;

	unsigned int factory_log_addr;
	u16 factory_log_size;
	int factory_test_num;

	struct m9mo_jpeg jpeg;
	struct m9mo_exif exif;

	int isp_fw_ver;
	u8 sensor_ver[10];
	u8 phone_ver[10];
	u8 sensor_type[25];

	int fw_checksum_val;

#ifdef CONFIG_CAM_DEBUG
	u8 dbg_level;
#endif

	int facedetect_mode;
	int running_capture_mode;
	int fd_eyeblink_cap;
	int fd_red_eye_status;
	int image_stabilizer_mode;
	int ot_status;
	int ot_x_loc;
	int ot_y_loc;
	int ot_width;
	int ot_height;
	int bracket_wbb_val;

	unsigned int face_beauty:1;
	unsigned int recording:1;
	unsigned int check_dataline:1;
	int anti_banding;
	int pixelformat;

	int wb_g_value;
	int wb_b_value;
	int wb_a_value;
	int wb_m_value;
	int wb_custom_rg;
	int wb_custom_bg;

	int fast_capture_set;

	int vss_mode;
	int dual_capture_start;
	int dual_capture_frame;

	int focus_mode;
	int focus_range;
	int focus_area_mode;

	int f_number;
	int iso;
	int numerator;
	int denominator;

	int AV;
	int TV;
	int SV;
	int EV;
	int LV;

	int smart_scene_detect_mode;

	int continueFps;

	int fd_num;

	int caf_state;

	int mode;

	bool stream_on_part2;

	int widget_mode_level;
	int gamma_rgb_mon;
	int gamma_rgb_cap;
	int gamma_tbl_rgb_mon;
	int gamma_tbl_rgb_cap;
	int color_effect;

	int preview_width;
	int preview_height;

	int mburst_start;

	int strobe_en;
	int sharpness;
	int saturation;

	int isp_mode;

	int af_running;
};

/* Category */
#define M9MO_CATEGORY_SYS	0x00
#define M9MO_CATEGORY_PARM	0x01
#define M9MO_CATEGORY_MON	0x02
#define M9MO_CATEGORY_AE	0x03
#define M9MO_CATEGORY_NEW	0x04
#define M9MO_CATEGORY_PRO_MODE	0x05
#define M9MO_CATEGORY_WB	0x06
#define M9MO_CATEGORY_EXIF	0x07
#define M9MO_CATEGORY_OT    0x08
#define M9MO_CATEGORY_FD	0x09
#define M9MO_CATEGORY_LENS	0x0A
#define M9MO_CATEGORY_CAPPARM	0x0B
#define M9MO_CATEGORY_CAPCTRL	0x0C
#define M9MO_CATEGORY_TEST	0x0D
#define M9MO_CATEGORY_ADJST	0x0E
#define M9MO_CATEGORY_FLASH	0x0F    /* F/W update */

/* M9MO_CATEGORY_SYS: 0x00 */
#define M9MO_SYS_PJT_CODE	0x01
#define M9MO_SYS_VER_FW		0x02
#define M9MO_SYS_VER_HW		0x04
#define M9MO_SYS_VER_PARAM	0x06
#define M9MO_SYS_VER_AWB	0x08
#define M9MO_SYS_USER_VER	0x0A
#define M9MO_SYS_MODE		0x0B
#define M9MO_SYS_ESD_INT	0x0E

#define M9MO_SYS_INT_EN		0x10
#define M9MO_SYS_INT_FACTOR	0x1C
#define M9MO_SYS_FRAMESYNC_CNT	0x14
#define M9MO_SYS_LENS_TIMER	0x28


/* M9MO_CATEGORY_PARAM: 0x01 */
#define M9MO_PARM_OUT_SEL	0x00
#define M9MO_PARM_MON_SIZE	0x01
#define M9MO_PARM_MON_FPS	0x02
#define M9MO_PARM_EFFECT	0x0B
#define M9MO_PARM_FLEX_FPS	0x67
#define M9MO_PARM_HDMOVIE	0x32
#define M9MO_PARM_VIDEO_SNAP_IMG_TRANSFER_START 0x3A
#define M9MO_PARM_SEL_FRAME_VIDEO_SNAP 0x3B
#define M9MO_PARM_MON_MOVIE_SELECT	0x3C
#define M9MO_PARM_VSS_MODE 0x6E

/* M9MO_CATEGORY_MON: 0x02 */
#define M9MO_MON_ZOOM		0x01
#define M9MO_MON_HR_ZOOM    0x04
#define M9MO_MON_MON_REVERSE	0x05
#define M9MO_MON_MON_MIRROR	0x06
#define M9MO_MON_SHOT_REVERSE	0x07
#define M9MO_MON_SHOT_MIRROR	0x08
#define M9MO_MON_CFIXB		0x09
#define M9MO_MON_CFIXR		0x0A
#define M9MO_MON_COLOR_EFFECT	0x0B
#define M9MO_MON_CHROMA_LVL	0x0F
#define M9MO_MON_EDGE_LVL	0x11
#define M9MO_MON_EDGE_CTRL	0x20
#define M9MO_MON_POINT_COLOR	0x22
#define M9MO_MON_TONE_CTRL	0x25
#define M9MO_MON_START_VIDEO_SNAP_SHOT 0x56
#define M9MO_MON_VIDEO_SNAP_SHOT_FRAME_COUNT 0x57

/* M9MO_CATEGORY_AE: 0x03 */
#define M9MO_AE_LOCK		0x00
#define M9MO_AE_MODE		0x01
#define M9MO_AE_ISOSEL		0x05
#define M9MO_AE_FLICKER		0x06
#define M9MO_AE_INDEX		0x09
#define M9MO_AE_EP_MODE_MON	0x0A
#define M9MO_AE_EP_MODE_CAP	0x0B
#define M9MO_AF_AE_LOCK		0x0D
#define M9MO_AE_AUTO_BRACKET_EV	0x20
#define M9MO_AE_STABILITY	0x21
#define M9MO_AE_EV_PRG_MODE_CAP	0x34
#define M9MO_AE_EV_PRG_MODE_MON	0x35
#define M9MO_AE_EV_PRG_F_NUMBER	0x36
#define M9MO_AE_EV_PRG_SS_NUMERATOR		0x37
#define M9MO_AE_EV_PRG_SS_DENOMINATOR	0x39
#define M9MO_AE_EV_PRG_ISO_VALUE	0x3B
#define M9MO_AE_EV_PRG_F_NUMBER_MON				0x3D
#define M9MO_AE_EV_PRG_SS_NUMERATOR_MON		0x3E
#define M9MO_AE_EV_PRG_SS_DENOMINATOR_MON		0x40
#define M9MO_AE_EV_PRG_ISO_VALUE_MON			0x42
#define M9MO_AE_NOW_AV	0x54
#define M9MO_AE_NOW_TV	0x58
#define M9MO_AE_NOW_SV	0x5C
#define M9MO_AE_NOW_LV	0x52

/* M9MO_CATEGORY_NEW: 0x04 */
#define M9MO_NEW_TIME_INFO		0x02
#define M9MO_NEW_OIS_CUR_MODE	0x06
#define M9MO_NEW_OIS_TIMER		0x07
#define M9MO_NEW_DETECT_SCENE	0x0B
#define M9MO_NEW_OIS_VERSION	0x1B

/* M9MO_CATEGORY_PRO_MODE: 0x05 */
#define M9MO_PRO_SMART_READ1		0x20
#define M9MO_PRO_SMART_READ2		0x24
#define M9MO_PRO_SMART_READ3		0x28

/* M9MO_CATEGORY_WB: 0x06 */
#define M9MO_AWB_LOCK		0x00
#define M9MO_WB_AWB_MODE	0x02
#define M9MO_WB_AWB_MANUAL	0x03
#define M9MO_WB_GBAM_MODE	0x8D
#define M9MO_WB_G_VALUE		0x8E
#define M9MO_WB_B_VALUE		0x8F
#define M9MO_WB_A_VALUE		0x90
#define M9MO_WB_M_VALUE		0x91
#define M9MO_WB_K_VALUE		0x92
#define M9MO_WB_CWB_MODE		0x93
#define M9MO_WB_SET_CUSTOM_RG	0x94
#define M9MO_WB_SET_CUSTOM_BG	0x96
#define M9MO_WB_GET_CUSTOM_RG	0x98
#define M9MO_WB_GET_CUSTOM_BG	0x9A
#define M9MO_WB_WBB_MODE		0x9C
#define M9MO_WB_WBB_AB		0x9D
#define M9MO_WB_WBB_GM		0x9E

/* M9MO_CATEGORY_EXIF: 0x07 */
#define M9MO_EXIF_EXPTIME_NUM	0x00
#define M9MO_EXIF_EXPTIME_DEN	0x04
#define M9MO_EXIF_TV_NUM	0x08
#define M9MO_EXIF_TV_DEN	0x0C
#define M9MO_EXIF_BV_NUM	0x18
#define M9MO_EXIF_BV_DEN	0x1C
#define M9MO_EXIF_EBV_NUM	0x20
#define M9MO_EXIF_EBV_DEN	0x24
#define M9MO_EXIF_ISO		0x28
#define M9MO_EXIF_FLASH		0x2A
#define M9MO_EXIF_AV_NUM	0x10
#define M9MO_EXIF_AV_DEN	0x14
#define M9MO_EXIF_FL	0x11
#define M9MO_EXIF_FL_35	0x13

/* M9MO_CATEGORY_OT: 0x08 */
#define M9MO_OT_TRACKING_CTL		0x00
#define M9MO_OT_INFO_READY			0x01
#define M9MO_OT_X_START_LOCATION	0x05
#define M9MO_OT_Y_START_LOCATION	0x07
#define M9MO_OT_X_END_LOCATION		0x09
#define M9MO_OT_Y_END_LOCATION		0x0B
#define M9MO_OT_TRACKING_X_LOCATION	0x10
#define M9MO_OT_TRACKING_Y_LOCATION	0x12
#define M9MO_OT_TRACKING_FRAME_WIDTH	0x14
#define M9MO_OT_TRACKING_FRAME_HEIGHT	0x16
#define M9MO_OT_TRACKING_STATUS		0x18
#define M9MO_OT_FRAME_WIDTH			0x30

/* M9MO_CATEGORY_FD: 0x09 */
#define M9MO_FD_CTL					0x00
#define M9MO_FD_SIZE				0x01
#define M9MO_FD_MAX					0x02
#define M9MO_FD_RED_EYE				0x55
#define M9MO_FD_RED_DET_STATUS		0x56
#define M9MO_FD_BLINK_FRAMENO		0x59
#define M9MO_FD_BLINK_LEVEL_1		0x5A
#define M9MO_FD_BLINK_LEVEL_2		0x5B
#define M9MO_FD_BLINK_LEVEL_3		0x5C

/* M9MO_CATEGORY_LENS: 0x0A */
#define M9MO_LENS_AF_INITIAL		0x00
#define M9MO_LENS_AF_LENS_CLOSE		0x01
#define M9MO_LENS_AF_ZOOM_CTRL		0x02
#define M9MO_LENS_AF_START_STOP		0x03
#define M9MO_LENS_AF_IRIS_STEP		0x05
#define M9MO_LENS_AF_ZOOM_LEVEL		0x06
#define M9MO_LENS_AF_SCAN_RANGE		0x07
#define M9MO_LENS_AF_MODE			0x08
#define M9MO_LENS_AF_WINDOW_MODE	0x09
#define M9MO_LENS_AF_BACKLASH_ADJ	0x0A
#define M9MO_LENS_AF_FOCUS_ADJ		0x0B
#define M9MO_LENS_AF_TILT_ADJ		0x0C
#define M9MO_LENS_AF_AF_ADJ			0x0D
#define M9MO_LENS_AF_PUNT_ADJ		0x0E
#define M9MO_LENS_AF_ZOOM_ADJ		0x0F
#define M9MO_LENS_AF_ADJ_TEMP_VALUE	0x0C
#define M9MO_LENS_AF_ALGORITHM		0x0D
#define M9MO_LENS_ZOOM_LEVEL_INFO	0x10
#define M9MO_LENS_AF_LED			0x1C
#define M9MO_LENS_AF_CAL			0x1D
#define M9MO_LENS_AF_RESULT			0x20
#define M9MO_LENS_ZOOM_SET_INFO		0x22
#define M9MO_LENS_ZOOM_SPEED		0x25
#define M9MO_LENS_ZOOM_STATUS		0x26
#define M9MO_LENS_LENS_STATUS		0x28
#define M9MO_LENS_ZOOM_LENS_STATUS	0x2A
#define M9MO_LENS_AF_TEMP_INDICATE	0x2B
#define M9MO_LENS_TIMER_LED			0x2D
#define M9MO_LENS_AF_TOUCH_POSX		0x30
#define M9MO_LENS_AF_TOUCH_POSY		0x32
#define M9MO_LENS_AF_VERSION		0x60

/* M9MO_CATEGORY_CAPPARM: 0x0B */
#define M9MO_CAPPARM_YUVOUT_MAIN	0x00
#define M9MO_CAPPARM_MAIN_IMG_SIZE	0x01
#define M9MO_CAPPARM_YUVOUT_PREVIEW	0x05
#define M9MO_CAPPARM_PREVIEW_IMG_SIZE	0x06
#define M9MO_CAPPARM_YUVOUT_THUMB	0x0A
#define M9MO_CAPPARM_THUMB_IMG_SIZE	0x0B
#define M9MO_CAPPARM_JPEG_SIZE_MAX	0x0F
#define M9MO_CAPPARM_JPEG_SIZE_MIN	0x13
#define M9MO_CAPPARM_JPEG_RATIO		0x17
#define M9MO_CAPPARM_MCC_MODE		0x1D
#define M9MO_CAPPARM_STROBE_EN		0x22
#define M9MO_CAPPARM_STROBE_CHARGE	0x27
#define M9MO_CAPPARM_STROBE_EVC		0x28
#define M9MO_CAPPARM_STROBE_UP_DOWN	0x29
#define M9MO_CAPPARM_WDR_EN			0x2C
#define M9MO_CAPPARM_JPEG_RATIO_OFS	0x1B
#define M9MO_CAPPARM_THUMB_JPEG_MAX	0x3C
#define M9MO_CAPPARM_STROBE_BATT_INFO	0x3F
#define M9MO_CAPPARM_AFB_CAP_EN		0x53

/* M9MO_CATEGORY_CAPCTRL: 0x0C */
#define M9MO_CAPCTRL_CAP_MODE	0x00
#define M9MO_CAPCTRL_CAP_FRM_INTERVAL 0x01
#define M9MO_CAPCTRL_CAP_FRM_COUNT 0x02
#define M9MO_CAPCTRL_START_DUALCAP 0x05
#define M9MO_CAPCTRL_FRM_SEL	0x06
#define M9MO_CAPCTRL_FRM_PRV_SEL	0x07
#define M9MO_CAPCTRL_FRM_THUMB_SEL	0x08
#define M9MO_CAPCTRL_TRANSFER	0x09
#define M9MO_CAPCTRL_IMG_SIZE	0x0D
#define M9MO_CAPCTRL_THUMB_SIZE	0x11

/* M9MO_CATEGORY_CAPCTRL: 0x0C  M9MO_CAPCTRL_CAP_MODE: 0x00 */
#define M9MO_CAP_MODE_SINGLE_CAPTURE		(0x00)
#define M9MO_CAP_MODE_MULTI_CAPTURE			(0x01)
#define M9MO_CAP_MODE_DUAL_CAPTURE			(0x05)
#define M9MO_CAP_MODE_BRACKET_CAPTURE		(0x06)
#define M9MO_CAP_MODE_ADDPIXEL_CAPTURE		(0x08)
#define M9MO_CAP_MODE_PANORAMA_CAPTURE		(0x0B)
#define M9MO_CAP_MODE_BLINK_CAPTURE			(0x0C)
#define M9MO_CAP_MODE_RAW			(0x0D)

/* M9MO_CATEGORY_ADJST: 0x0E */
#define M9MO_ADJST_SHUTTER_MODE	0x33
#define M9MO_ADJST_AWB_RG_H	0x3C
#define M9MO_ADJST_AWB_RG_L	0x3D
#define M9MO_ADJST_AWB_BG_H	0x3E
#define M9MO_ADJST_AWB_BG_L	0x3F

/* M9MO_CATEGORY_FLASH: 0x0F */
#define M9MO_FLASH_ADDR		0x00
#define M9MO_FLASH_BYTE		0x04
#define M9MO_FLASH_ERASE	0x06
#define M9MO_FLASH_WR		0x07
#define M9MO_FLASH_RAM_CLEAR	0x08
#define M9MO_FLASH_CAM_START	0x12
#define M9MO_FLASH_SEL		0x13

/* M9MO_CATEGORY_TEST:	0x0D */
#define M9MO_TEST_OUTPUT_YCO_TEST_DATA	0x1B
#define M9MO_TEST_ISP_PROCESS		0x59

/* M9MO Sensor Mode */
#define M9MO_SYSINIT_MODE	0x0
#define M9MO_PARMSET_MODE	0x1
#define M9MO_MONITOR_MODE	0x2
#define M9MO_STILLCAP_MODE	0x3

/* Interrupt Factor */
#define M9MO_INT_SOUND		(1 << 15)
#define M9MO_INT_LENS_INIT	(1 << 14)
#define M9MO_INT_FD		(1 << 13)
#define M9MO_INT_FRAME_SYNC	(1 << 12)
#define M9MO_INT_CAPTURE	(1 << 11)
#define M9MO_INT_ZOOM		(1 << 10)
#define M9MO_INT_AF		(1 << 9)
#define M9MO_INT_MODE		(1 << 8)
#define M9MO_INT_ATSCENE	(1 << 7)
#define M9MO_INT_ATSCENE_UPDATE	(1 << 6)
#define M9MO_INT_AF_STATUS		(1 << 5)
#define M9MO_INT_OIS_SET	(1 << 4)
#define M9MO_INT_OIS_INIT	(1 << 3)
#define M9MO_INT_STNW_DETECT	(1 << 2)
#define M9MO_INT_SCENARIO_FIN	(1 << 1)
#define M9MO_INT_PRINT	(1 << 0)

/* ESD Interrupt */
#define M9MO_INT_ESD		(1 << 0)

#endif /* __M9MO_H */
