/*
 * Driver for AR0832 (8MP Camera) from Aptina
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __AR0832_H
#define __AR0832_H

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

enum ar0832_prev_frmsize {
	AR0832_PREVIEW_QCIF,
	AR0832_PREVIEW_QCIF2,
	AR0832_PREVIEW_QVGA,
	AR0832_PREVIEW_VGA,
	AR0832_PREVIEW_D1,
	AR0832_PREVIEW_WVGA,
	AR0832_PREVIEW_720P,
	AR0832_PREVIEW_1080P,
	AR0832_PREVIEW_HDR,
};

enum ar0832_cap_frmsize {
	AR0832_CAPTURE_VGA,	/* 640 x 480 */
	AR0832_CAPTURE_WVGA,	/* 800 x 480 */
	AR0832_CAPTURE_W1MP,	/* 1600 x 960 */
	AR0832_CAPTURE_2MP,	/* UXGA - 1600 x 1200 */
	AR0832_CAPTURE_W2MP,	/* 2048 x 1232 */
	AR0832_CAPTURE_3MP,	/* QXGA - 2048 x 1536 */
	AR0832_CAPTURE_W4MP,	/* WQXGA - 2560 x 1536 */
	AR0832_CAPTURE_5MP,	/* 2560 x 1920 */
	AR0832_CAPTURE_W6MP,	/* 3072 x 1856 */
	AR0832_CAPTURE_7MP,	/* 3072 x 2304 */
	AR0832_CAPTURE_W7MP,	/* WQXGA - 2560 x 1536 */
	AR0832_CAPTURE_8MP,	/* 3264 x 2448 */
};

struct ar0832_control {
	u32 id;
	s32 value;
	s32 minimum;		/* Note signedness */
	s32 maximum;
	s32 step;
	s32 default_value;
};

struct ar0832_frmsizeenum {
	unsigned int index;
	unsigned int width;
	unsigned int height;
	u8 reg_val;		/* a value for category parameter */
};

struct ar0832_isp {
	wait_queue_head_t wait;
	unsigned int irq;	/* irq issued by ISP */
	unsigned int issued;
	unsigned int int_factor;
	unsigned int bad_fw:1;
};

struct ar0832_jpeg {
	int quality;
	unsigned int main_size;	/* Main JPEG file size */
	unsigned int thumb_size;	/* Thumbnail file size */
	unsigned int main_offset;
	unsigned int thumb_offset;
	unsigned int postview_offset;
};

struct ar0832_focus {
	unsigned int mode;
	unsigned int lock;
	unsigned int status;
	unsigned int touch;
	unsigned int pos_x;
	unsigned int pos_y;
};

struct ar0832_exif {
	char unique_id[7];
	u32 exptime;		/* us */
	u16 flash;
	u16 iso;
	int tv;			/* shutter speed */
	int bv;			/* brightness */
	int ebv;		/* exposure bias */
};

struct ar0832_state {
	struct ar0832_platform_data *pdata;
	struct v4l2_subdev sd;

	struct ar0832_isp isp;

	const struct ar0832_frmsizeenum *preview;
	const struct ar0832_frmsizeenum *capture;

	enum v4l2_pix_format_mode format_mode;
	enum v4l2_sensor_mode sensor_mode;
	enum v4l2_flash_mode flash_mode;
	int vt_mode;
	int beauty_mode;
	int zoom;

	unsigned int fps;
	struct ar0832_focus focus;

	struct ar0832_jpeg jpeg;
	struct ar0832_exif exif;

	int check_dataline;
	char *fw_version;

#ifdef CONFIG_CAM_DEBUG
	u8 dbg_level;
#endif
struct i2c_client *this_client;

};

/* Category */
#define AR0832_CATEGORY_SYS	0x00
#define AR0832_CATEGORY_PARM	0x01
#define AR0832_CATEGORY_MON	0x02
#define AR0832_CATEGORY_AE	0x03
#define AR0832_CATEGORY_WB	0x06
#define AR0832_CATEGORY_EXIF	0x07
#define AR0832_CATEGORY_FD	0x09
#define AR0832_CATEGORY_LENS	0x0A
#define AR0832_CATEGORY_CAPPARM	0x0B
#define AR0832_CATEGORY_CAPCTRL	0x0C
#define AR0832_CATEGORY_TEST	0x0D
#define AR0832_CATEGORY_ADJST	0x0E
#define AR0832_CATEGORY_FLASH	0x0F    /* F/W update */

/* AR0832_CATEGORY_SYS: 0x00 */
#define AR0832_SYS_PJT_CODE	0x01
#define AR0832_SYS_VER_FW		0x02
#define AR0832_SYS_VER_HW		0x04
#define AR0832_SYS_VER_PARAM	0x06
#define AR0832_SYS_VER_AWB	0x08
#define AR0832_SYS_USER_VER	0x0A
#define AR0832_SYS_MODE		0x0B
#define AR0832_SYS_ESD_INT	0x0E
#define AR0832_SYS_INT_FACTOR	0x10
#define AR0832_SYS_INT_EN		0x11
#define AR0832_SYS_ROOT_EN	0x12

/* AR0832_CATEGORY_PARAM: 0x01 */
#define AR0832_PARM_OUT_SEL	0x00
#define AR0832_PARM_MON_SIZE	0x01
#define AR0832_PARM_EFFECT	0x0B
#define AR0832_PARM_FLEX_FPS	0x31
#define AR0832_PARM_HDMOVIE	0x32
#define AR0832_PARM_HDR_MON	0x39
#define AR0832_PARM_HDR_MON_OFFSET_EV	0x3A

/* AR0832_CATEGORY_MON: 0x02 */
#define AR0832_MON_ZOOM		0x01
#define AR0832_MON_MON_REVERSE	0x05
#define AR0832_MON_MON_MIRROR	0x06
#define AR0832_MON_SHOT_REVERSE	0x07
#define AR0832_MON_SHOT_MIRROR	0x08
#define AR0832_MON_CFIXB		0x09
#define AR0832_MON_CFIXR		0x0A
#define AR0832_MON_COLOR_EFFECT	0x0B
#define AR0832_MON_CHROMA_LVL	0x0F
#define AR0832_MON_EDGE_LVL	0x11
#define AR0832_MON_TONE_CTRL	0x25

/* AR0832_CATEGORY_AE: 0x03 */
#define AR0832_AE_LOCK		0x00
#define AR0832_AE_MODE		0x01
#define AR0832_AE_ISOSEL		0x05
#define AR0832_AE_FLICKER		0x06
#define AR0832_AE_EP_MODE_MON	0x0A
#define AR0832_AE_EP_MODE_CAP	0x0B
#define AR0832_AE_ONESHOT_MAX_EXP	0x36
#define AR0832_AE_INDEX		0x38

/* AR0832_CATEGORY_WB: 0x06 */
#define AR0832_AWB_LOCK		0x00
#define AR0832_WB_AWB_MODE	0x02
#define AR0832_WB_AWB_MANUAL	0x03

/* AR0832_CATEGORY_EXIF: 0x07 */
#define AR0832_EXIF_EXPTIME_NUM	0x00
#define AR0832_EXIF_EXPTIME_DEN	0x04
#define AR0832_EXIF_TV_NUM	0x08
#define AR0832_EXIF_TV_DEN	0x0C
#define AR0832_EXIF_BV_NUM	0x18
#define AR0832_EXIF_BV_DEN	0x1C
#define AR0832_EXIF_EBV_NUM	0x20
#define AR0832_EXIF_EBV_DEN	0x24
#define AR0832_EXIF_ISO		0x28
#define AR0832_EXIF_FLASH		0x2A

/* AR0832_CATEGORY_FD: 0x09 */
#define AR0832_FD_CTL		0x00
#define AR0832_FD_SIZE		0x01
#define AR0832_FD_MAX		0x02

/* AR0832_CATEGORY_LENS: 0x0A */
#define AR0832_LENS_AF_MODE	0x01
#define AR0832_LENS_AF_START	0x02
#define AR0832_LENS_AF_STATUS	0x03
#define AR0832_LENS_AF_UPBYTE_STEP	0x06
#define AR0832_LENS_AF_LOWBYTE_STEP	0x07
#define AR0832_LENS_AF_CAL	0x1D
#define AR0832_LENS_AF_TOUCH_POSX	0x30
#define AR0832_LENS_AF_TOUCH_POSY	0x32

/* AR0832_CATEGORY_CAPPARM: 0x0B */
#define AR0832_CAPPARM_YUVOUT_MAIN	0x00
#define AR0832_CAPPARM_MAIN_IMG_SIZE	0x01
#define AR0832_CAPPARM_YUVOUT_PREVIEW	0x05
#define AR0832_CAPPARM_PREVIEW_IMG_SIZE	0x06
#define AR0832_CAPPARM_YUVOUT_THUMB	0x0A
#define AR0832_CAPPARM_THUMB_IMG_SIZE	0x0B
#define AR0832_CAPPARM_JPEG_SIZE_MAX	0x0F
#define AR0832_CAPPARM_JPEG_RATIO		0x17
#define AR0832_CAPPARM_MCC_MODE		0x1D
#define AR0832_CAPPARM_WDR_EN		0x2C
#define AR0832_CAPPARM_LIGHT_CTRL		0x40
#define AR0832_CAPPARM_FLASH_CTRL		0x41
#define AR0832_CAPPARM_JPEG_RATIO_OFS	0x34
#define AR0832_CAPPARM_THUMB_JPEG_MAX	0x3C
#define AR0832_CAPPARM_AFB_CAP_EN		0x53

/* AR0832_CATEGORY_CAPCTRL: 0x0C */
#define AR0832_CAPCTRL_FRM_SEL	0x06
#define AR0832_CAPCTRL_TRANSFER	0x09
#define AR0832_CAPCTRL_IMG_SIZE	0x0D
#define AR0832_CAPCTRL_THUMB_SIZE	0x11

/* AR0832_CATEGORY_ADJST: 0x0E */
#define AR0832_ADJST_AWB_RG_H	0x3B
#define AR0832_ADJST_AWB_RG_L	0x3D
#define AR0832_ADJST_AWB_BG_H	0x3E
#define AR0832_ADJST_AWB_BG_L	0x3F

/* AR0832_CATEGORY_FLASH: 0x0F */
#define AR0832_FLASH_ADDR		0x00
#define AR0832_FLASH_BYTE		0x04
#define AR0832_FLASH_ERASE	0x06
#define AR0832_FLASH_WR		0x07
#define AR0832_FLASH_RAM_CLEAR	0x08
#define AR0832_FLASH_CAM_START	0x12
#define AR0832_FLASH_SEL		0x13

/* AR0832_CATEGORY_TEST:	0x0D */
#define AR0832_TEST_OUTPUT_YCO_TEST_DATA		0x1B
#define AR0832_TEST_ISP_PROCESS			0x59

/* AR0832 Sensor Mode */
#define AR0832_SYSINIT_MODE	0x0
#define AR0832_PARMSET_MODE	0x1
#define AR0832_MONITOR_MODE	0x2
#define AR0832_STILLCAP_MODE	0x3

/* Interrupt Factor */
#define AR0832_INT_SOUND		(1 << 7)
#define AR0832_INT_LENS_INIT	(1 << 6)
#define AR0832_INT_FD		(1 << 5)
#define AR0832_INT_FRAME_SYNC	(1 << 4)
#define AR0832_INT_CAPTURE	(1 << 3)
#define AR0832_INT_ZOOM		(1 << 2)
#define AR0832_INT_AF		(1 << 1)
#define AR0832_INT_MODE		(1 << 0)

/* ESD Interrupt */
#define AR0832_INT_ESD		(1 << 0)

/* AR0832 REG by hardkernel*/
#define AR0832_CATEGORY_MODEL_ID	0x00
#define AR0832_CATEGORY_REVISION_NUM	0x02
#define AR0832_CATEGORY_MANUFACTURE_ID	0x03
#define AR0832_CATEGORY_SMIA_VERSION	0x04





/////////////////////////////////////////////////////////////////////////////////
// History:
// 
//
// V2.0 released on 2011-01-06
// 		Changes:
//		Added REG=3064, 0x7400 after the clock configuration.
//		removed modification to registers 0x3ED8, 0x3EDA, 0x3EDC
//		Modified value written in 0x316E from 0x869C to 0x869A
//		
// V3.0 released on 2011-01-26
// 		Changes:
//		Adjusted MIPI timings considering the specific Motorola frequency
//		Added/changed values to register 0x31B0-0x31BC in  [2-lane MIPI Interface Configuration]
//
// V4.0 Release on 2011-03-04
//		Changes:
//		Modified recommended registers to minimize row noise(included in V3 Plus).
//
/////////////////////////////////////////////////////////////////////////////////



//[MIPI 2-lane FPGA]
//SERIAL_REG = 0xCA, 0x00, 0x8016, 8:16   // FPGA disabled
//SERIAL_REG = 0xCA, 0x00, 0x0016, 8:16   // FPGA into MIPI dual lane mode



//[Start Streaming]
static unsigned short ar0832_reg_start_streaming1_3[]={	
0x301A, 0x200, 1,	//MASK_BAD_Frames
0x301A, 0x400, 1,	//Restart_bad_frames
0x301A, 0x8, 1,		//Lock_Register
};
static unsigned short ar0832_reg_start_streaming2[]={	
0x0104, 0x00,		//GROUPED_PARAMETER_HOLD
};
static unsigned short ar0832_reg_start_streaming3_3[]={	
0x301A, 0x4, 1,		//Start_Streaming
};
//STATE= Detect Master Clock, 1


//[Stop Streaming]
static unsigned short ar0832_reg_stop_streaming1_3[]={	
0x301A, 0x4, 0,		//Start_Streaming
0x301A, 0x8, 0,		//Lock_Register
};
static unsigned short ar0832_reg_stop_streaming2[]={	
0x0104, 0x01,		//GROUPED_PARAMETER_HOLD
};


//[2-lane MIPI Interface Configuration]
static unsigned short ar0832_init_reg_MIPI_IF_CFG1_3[]={	
0x3064, 0x0100, 0,	//embedded_data_enable
};
static unsigned short ar0832_init_reg_MIPI_IF_CFG2[]={	
0x31AE, 0x0202,	//2-lane MIPI SERIAL_FORMAT
0x31B0, 0x0083,
0x31B2, 0x004D,
0x31B4, 0x0E77,
0x31B6, 0x0D20,
0x31B8, 0x020E,
0x31BA, 0x0710,
0x31BC, 0x2A0D,
0xffff,50, //DELAY=5
};

static unsigned short ar0832_init_reg_raw10[]={	
//[RAW10]
0x0112, 0x0A0A		//CCP_DATA_FORMAT
};

static unsigned short ar0832_init_reg[]={	
//[Recommended Settings]

0x3044, 0x0590,
0x306E, 0xFC80,
0x30B2, 0xC000,
0x30D6, 0x0800,
0x316C, 0xB42F,
0x316E, 0x869A,
0x3170, 0x210E,
0x317A, 0x010E,
0x31E0, 0x1FB9,
0x31E6, 0x07FC,
0x37C0, 0x0000,
0x37C2, 0x0000,
0x37C4, 0x0000,
0x37C6, 0x0000,
0x3E00, 0x0011,
0x3E02, 0x8801,
0x3E04, 0x2801,
0x3E06, 0x8449,
0x3E08, 0x6841,
0x3E0A, 0x400C,
0x3E0C, 0x1001,
0x3E0E, 0x2603,
0x3E10, 0x4B41,
0x3E12, 0x4B24,
0x3E14, 0xA3CF,
0x3E16, 0x8802,
0x3E18, 0x8401,
0x3E1A, 0x8601,
0x3E1C, 0x8401,
0x3E1E, 0x840A,
0x3E20, 0xFF00,
0x3E22, 0x8401,
0x3E24, 0x00FF,
0x3E26, 0x0088,
0x3E28, 0x2E8A,
0x3E30, 0x0000,
0x3E32, 0x8801,
0x3E34, 0x4029,
0x3E36, 0x00FF,
0x3E38, 0x8469,
0x3E3A, 0x00FF,
0x3E3C, 0x2801,
0x3E3E, 0x3E2A,
0x3E40, 0x1C01,
0x3E42, 0xFF84,
0x3E44, 0x8401,
0x3E46, 0x0C01,
0x3E48, 0x8401,
0x3E4A, 0x00FF,
0x3E4C, 0x8402,
0x3E4E, 0x8984, //0x00FF
0x3E50, 0x6628,
0x3E52, 0x8340,
0x3E54, 0x00FF,
0x3E56, 0x4A42,
0x3E58, 0x2703, 
0x3E5A, 0x6752,
0x3E5C, 0x3F2A,
0x3E5E, 0x846A,
0x3E60, 0x4C01,
0x3E62, 0x8401,
0x3E66, 0x3901,
0x3E90, 0x2C01,
0x3E98, 0x2B02,
0x3E92, 0x2A04,
0x3E94, 0x2509,
0x3E96, 0x0000,
0x3E9A, 0x2905,
0x3E9C, 0x00FF,
0x3ECC, 0x00EB,
0x3ED0, 0x1E24,
0x3ED4, 0xAFC4,
0x3ED6, 0x909B,
0x3EE0, 0x2424,
0x3EE2, 0x9797,
0x3EE4, 0xC100,
0x3EE6, 0x0540,
0x3174, 0x8000,
};

//STATE= Minimum Gain, 1500	// gain * 1000
//[Toolbar:eeprom]
//[2-lane MIPI 3272x2456 15FPS 66,7ms RAW10 Ext=24MHz Vt_pix_clk=192MHz Op_pix_clk=76,8MHz FOV=3264x2448] 
//Low FPS is limited by Demo2 HW, NOT sensor
//XMCLK=24000000 
//LOAD = MIPI 2-lane FPGA
//LOAD = Stop Streaming
//LOAD = 2-lane MIPI Interface Configuration
//LOAD = Recommended Settings
//LOAD = RAW10

static unsigned short ar0832_init_reg_pll[]={	
//PLL Configuration (Ext=24MHz, vt_pix_clk=192MHz, op_pix_clk=76.8MHz)
0x0300, 0x4, //VT_PIX_CLK_DIV=4
0x0302, 0x1, //VT_SYS_CLK_DIV=1
0x0304, 0x2, //PRE_PLL_CLK_DIV=2 //Note: 24MHz/2=12MHz
0x0306, 0x10,//0x10,//0x40 //PLL_MULTIPLIER=64 //Note: Running at 768MHz
0x0308, 0xa, //OP_PIX_CLK_DIV=10
0x030A, 0x1, //OP_SYS_CLK_DIV=1
0xffff,10, //DELAY=1
0x3064, 0x7400,
};
#if 0
static unsigned short ar0832_init_reg_output_size[]={	
//Output size (Pixel address must start with EVEN and end with ODD!)
0x0344, 0x4, //X_ADDR_START 4
0x0348, 0xCCB, //X_ADDR_END 3275
0x0346, 0x4 ,//Y_ADDR_START 4
0x034A, 0x99B, //Y_ADDR_END 2459
0x034C, 0xCC8, //X_OUTPUT_SIZE 3272
0x034E, 0x998, //Y_OUTPUT_SIZE 2456
0x3040, 0x0041
};
#else 
#if 0
static unsigned short ar0832_init_reg_output_size[]={	
//Output size (Pixel address must start with EVEN and end with ODD!)
0x0344, 0x0, //X_ADDR_START 4
0x0348, 639, //X_ADDR_END 3275
0x0346, 0x0, //Y_ADDR_START 4
0x034A, 479, //Y_ADDR_END 2459
0x034C, 640, //X_OUTPUT_SIZE 3272
0x034E, 480, //Y_OUTPUT_SIZE 2456
0x3040, 0x0041
};
#else
static unsigned short ar0832_init_reg_output_size[]={	
//Output size (Pixel address must start with EVEN and end with ODD!)
0x0344, 0x0, //X_ADDR_START 4
0x0348, 1279, //X_ADDR_END 3275
0x0346, 0x0, //Y_ADDR_START 4
0x034A, 719, //Y_ADDR_END 2459
0x034C, 1280, //X_OUTPUT_SIZE 3272
0x034E, 720, //Y_OUTPUT_SIZE 2456
0x3040, 0x0041
};

#endif
#endif 
//ETC ..

//"X-Bin2 Y-Bin2" and "X-Bin2Skip2 Y-Bin2Skip2" Optimization
static unsigned short ar0832_init_reg_etc1_3[]={	
0x306E, 0x0030, 0x0, //Resample_Binning 3: enable
};
static unsigned short ar0832_init_reg_etc2[]={	
0x306E, 0xFC80,
0x0400, 0x0000, //SCALE_MODE: 2: ENABLE
0x0404, 0x10, //SCALE_M = 16
};
static unsigned short ar0832_init_reg_etc3_3[]={	
0x3178, 0x0800, 0, //XSkip2Bin2YSkip4 4x_optimization
0x3ED0, 0x0080, 0, //XSkip2Bin2YSkip4 4x_optimization
};

static unsigned short ar0832_init_reg_etc4[]={	
0x3178, 0x0000,
0x3ED0, 0x1E24,

//Scale Configuration
0x0400, 0x0000, //SCALE_MODE: 0:Disable
0x0404, 0x10, //SCALE_M = 16

//Timing Configuration
0x0342, 0x133C,//LINE_LENGTH_PCK 4924
0x0340, 0xA27,//FRAME_LENGTH_LINES 2599
0x0202, 0xA27, //COARSE_INTEGRATION_TIME 2599
0x3014, 0x9DC, //FINE_INTEGRATION_TIME 2524
0x3010, 0x78, //FINE_CORRECTION 120
};

//LOAD = Start Streaming
//LOAD=test
//STATE = Master Clock, 192000000
#if 1
#if 0
static unsigned short ar0832_reg_test[]={	
//[VGA]

	 0x0104, 0x01  , // GROUPED_PARAMETER_HOLD
	 0x0382, 0x0007,     // X_ODD_INC
	 0x0386, 0x0007,     // Y_ODD_INC
	 0x3040, 0x15C7,     // READ_MODE
	 0x3040, 0x11C7,     // READ_MODE
	 0x3040, 0x01C7,     // READ_MODE
	 0x306E, 0xFCA0,     // DATAPATH_SELECT
	 0x0344, 0x0008,     // X_ADDR_START
	 0x0346, 0x0008,     // Y_ADDR_START
	 0x0348, 0x0CC1,     // X_ADDR_END
	 0x034A, 0x0991,     // Y_ADDR_END
	 0x034C, 0x0280,     // X_OUTPUT_SIZE
	 0x034E, 0x01E0,     // Y_OUTPUT_SIZE
	 0x0400, 0x0002,     // SCALING_MODE
	0x0404, 0x0014,     // SCALE_M
	0x0104, 0x00,   // GROUPED_PARAMETER_HOLD

	0x0342, 0x143C, 	// LINE_LENGTH_PCK
	0x0202, 0x009B, 	// COARSE_INTEGRATION_TIME
	0x0202, 0x009B, 	// COARSE_INTEGRATION_TIME
	0x305A, 0x1443, 	// RED_GAIN
	0x305A, 0x1443, 	// RED_GAIN
	0x3056, 0x1060, 	// GREEN1_GAIN
	0x305C, 0x1060, 	// GREEN2_GAIN
	0x3056, 0x1060, 	// GREEN1_GAIN
	0x305C, 0x1060, 	// GREEN2_GAIN
	0x3058, 0x1453, 	// BLUE_GAIN
	0x3058, 0x1453, 	// BLUE_GAIN
};

#else 
/*
static unsigned short ar0832_reg_test[]={	
//[656x492]

	 0x0104, 0x01  , // GROUPED_PARAMETER_HOLD
	 0x0382, 0x0007,     // X_ODD_INC
	 0x0386, 0x0007,     // Y_ODD_INC
	 0x3040, 0x15C7,     // READ_MODE
	 0x3040, 0x11C7,     // READ_MODE
	 0x3040, 0x01C7,     // READ_MODE
	 0x306E, 0xFCA0,     // DATAPATH_SELECT
	 0x0344, 0x0008,     // X_ADDR_START
	 0x0346, 0x0008,     // Y_ADDR_START
	 0x0348, 0x0CC1,     // X_ADDR_END
	 0x034A, 0x0991,     // Y_ADDR_END
	 0x034C, 656,     // X_OUTPUT_SIZE
	 0x034E, 492,     // Y_OUTPUT_SIZE
	 0x0400, 0x0002,     // SCALING_MODE
	0x0404, 0x0014,     // SCALE_M
	0x0104, 0x00,   // GROUPED_PARAMETER_HOLD

	0x0342, 0x143C, 	// LINE_LENGTH_PCK
	0x0202, 0x009B, 	// COARSE_INTEGRATION_TIME
	0x0202, 0x009B, 	// COARSE_INTEGRATION_TIME
	0x305A, 0x1443, 	// RED_GAIN
	0x305A, 0x1443, 	// RED_GAIN
	0x3056, 0x1060, 	// GREEN1_GAIN
	0x305C, 0x1060, 	// GREEN2_GAIN
	0x3056, 0x1060, 	// GREEN1_GAIN
	0x305C, 0x1060, 	// GREEN2_GAIN
	0x3058, 0x1453, 	// BLUE_GAIN
	0x3058, 0x1453, 	// BLUE_GAIN
};
*/
	static unsigned short ar0832_reg_test[]={	
	//
		0x0104, 0x01  , // GROUPED_PARAMETER_HOLD
		0x0382, 0x0007,	 // X_ODD_INC
		0x0386, 0x0007,	 // Y_ODD_INC
		0x3040, 0x15C7,	 // READ_MODE
		0x3040, 0x11C7,	 // READ_MODE
		0x3040, 0x01C7,	 // READ_MODE
		0x306E, 0xFCA0,	 // DATAPATH_SELECT
		 0x0344, 0x0008,	 // X_ADDR_START
		 0x0346, 0x0008,	 // Y_ADDR_START
		 0x0348, 0x0CC1,	 // X_ADDR_END
		 0x034A, 0x0991,	 // Y_ADDR_END
		 0x034C, 1280,	  // X_OUTPUT_SIZE
		 0x034E, 720,	  // Y_OUTPUT_SIZE
		0x0400, 0x0002,	 // SCALING_MODE
		0x0404, 0x0014, 	// SCALE_M
		0x0104, 0x00,	// GROUPED_PARAMETER_HOLD
	
		0x0342, 0x143C, 	// LINE_LENGTH_PCK
		0x0202, 0x009B, 	// COARSE_INTEGRATION_TIME
		0x0202, 0x009B, 	// COARSE_INTEGRATION_TIME
		0x305A, 0x1443, 	// RED_GAIN
		0x305A, 0x1443, 	// RED_GAIN
		0x3056, 0x1060, 	// GREEN1_GAIN
		0x305C, 0x1060, 	// GREEN2_GAIN
		0x3056, 0x1060, 	// GREEN1_GAIN
		0x305C, 0x1060, 	// GREEN2_GAIN
		0x3058, 0x1453, 	// BLUE_GAIN
		0x3058, 0x1453, 	// BLUE_GAIN
	};

#endif


#else 
static unsigned short ar0832_reg_test[]={	
//[test]
0x0104, 0x01, 	// GROUPED_PARAMETER_HOLD
0x0382, 0x0001, 	// X_ODD_INC
0x0386, 0x0001, 	// Y_ODD_INC
0x3040, 0x0041, 	// READ_MODE
0x3040, 0x0041, 	// READ_MODE
0x3040, 0x0041, 	// READ_MODE
0x306E, 0xFC80, 	// DATAPATH_SELECT
0x0344, 0x0008, 	// X_ADDR_START
0x0346, 0x0008, 	// Y_ADDR_START
0x0348, 0x0CC7, 	// X_ADDR_END
0x034A, 0x0997, 	// Y_ADDR_END
0x034C, 0x0CC0, 	// X_OUTPUT_SIZE
0x034E, 0x0990, 	// Y_OUTPUT_SIZE
0x0400, 0x0000, 	// SCALING_MODE
0x0404, 0x0010, 	// SCALE_M
0x0104, 0x00, 	// GROUPED_PARAMETER_HOLD
0x0342, 0x143C, 	// LINE_LENGTH_PCK
0x0202, 0x009B, 	// COARSE_INTEGRATION_TIME
0x0202, 0x009B, 	// COARSE_INTEGRATION_TIME
0x305A, 0x1443, 	// RED_GAIN
0x305A, 0x1443, 	// RED_GAIN
0x3056, 0x1060, 	// GREEN1_GAIN
0x305C, 0x1060, 	// GREEN2_GAIN
0x3056, 0x1060, 	// GREEN1_GAIN
0x305C, 0x1060, 	// GREEN2_GAIN
0x3058, 0x1453, 	// BLUE_GAIN
0x3058, 0x1453, 	// BLUE_GAIN
};
//----------------------------------------------------------
#endif

static unsigned short ar0832_init_reg_media_test[]={	
//[for is media test]
0x0202, 0x01DA, 	// COARSE_INTEGRATION_TIME
0x305A, 0x1449, 	// RED_GAIN
0x3056, 0x1060, 	// GREEN1_GAIN
0x305C, 0x1060, 	// GREEN2_GAIN
0x3058, 0x1457 	// BLUE_GAIN
};

#define AR0832_REG_START_STREAMING1_3	(sizeof(ar0832_reg_start_streaming1_3) / 	sizeof(ar0832_reg_start_streaming1_3[0]))
#define AR0832_REG_START_STREAMING2		(sizeof(ar0832_reg_start_streaming2) / 	sizeof(ar0832_reg_start_streaming2[0]))
#define AR0832_REG_START_STREAMING3_3	(sizeof(ar0832_reg_start_streaming3_3) / 	sizeof(ar0832_reg_start_streaming3_3[0]))

#define AR0832_REG_STOP_STREAMING1_3	(sizeof(ar0832_reg_stop_streaming1_3) / 	sizeof(ar0832_reg_stop_streaming1_3[0]))
#define AR0832_REG_STOP_STREAMING2	(sizeof(ar0832_reg_stop_streaming2) / 	sizeof(ar0832_reg_stop_streaming2[0]))

#define AR0832_INIT_REG_MIPI_IF_CFG1_3	(sizeof(ar0832_init_reg_MIPI_IF_CFG1_3) / 	sizeof(ar0832_init_reg_MIPI_IF_CFG1_3[0]))
#define AR0832_INIT_REG_MIPI_IF_CFG2	(sizeof(ar0832_init_reg_MIPI_IF_CFG2) / 	sizeof(ar0832_init_reg_MIPI_IF_CFG2[0]))

#define AR0832_INIT_REG_RAW10		(sizeof(ar0832_init_reg_raw10) / 		sizeof(ar0832_init_reg_raw10[0]))
#define AR0832_INIT_REG_REG			(sizeof(ar0832_init_reg) / 				sizeof(ar0832_init_reg[0]))
#define AR0832_INIT_REG_PLL			(sizeof(ar0832_init_reg_pll) / 			sizeof(ar0832_init_reg_pll[0]))
#define AR0832_INIT_REG_OUTPUT_SIZE	(sizeof(ar0832_init_reg_output_size) /	sizeof(ar0832_init_reg_output_size[0]))

#define AR0832_INIT_REG_ETC1_3			(sizeof(ar0832_init_reg_etc1_3) / 			sizeof(ar0832_init_reg_etc1_3[0]))
#define AR0832_INIT_REG_ETC2			(sizeof(ar0832_init_reg_etc2) / 			sizeof(ar0832_init_reg_etc2[0]))
#define AR0832_INIT_REG_ETC3_3			(sizeof(ar0832_init_reg_etc3_3) / 			sizeof(ar0832_init_reg_etc3_3[0]))
#define AR0832_INIT_REG_ETC4			(sizeof(ar0832_init_reg_etc4) / 			sizeof(ar0832_init_reg_etc4[0]))

#define AR0832_INIT_REG_TEST		(sizeof(ar0832_reg_test) / 				sizeof(ar0832_reg_test[0]))
#define AR0832_INIT_REG_MEDIA_TEST	(sizeof(ar0832_init_reg_media_test) / 	sizeof(ar0832_init_reg_media_test[0]))


#endif /* __AR0832_H */
