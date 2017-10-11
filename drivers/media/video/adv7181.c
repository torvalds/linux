/*
 * drivers/media/video/adv7181.c
 *
 * Copyright (C) ROCKCHIP, Inc.
 * Author:zhoupeng<benjo.zhou@rock-chips.com>
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "generic_sensor.h"
#include <linux/moduleparam.h>
#include <linux/interrupt.h>
#include <linux/of_gpio.h>
#include <linux/debugfs.h>


/*
 *      Driver Version Note
 *v0.0.1: this driver is compatible with generic_sensor
 *v0.0.2: support debug_fs for debug
 */
static int version = KERNEL_VERSION(0, 0, 1);
module_param(version, int, S_IRUGO);

static int debug;
module_param(debug, int, S_IRUGO|S_IWUSR);

#define dprintk(level, fmt, arg...) do {		\
	if (debug >= level)				\
		printk(KERN_WARNING fmt, ## arg);	\
} while (0)
#define debug_printk(format, ...) dprintk(1, format, ## __VA_ARGS__)
/* Sensor Driver Configuration Begin */
#define SENSOR_NAME RK29_CAM_SENSOR_ADV7181
#define SENSOR_V4L2_IDENT V4L2_IDENT_ADV7181
#define SENSOR_ID 0x20
#define SENSOR_BUS_PARAM		(V4L2_MBUS_MASTER |		\
					V4L2_MBUS_PCLK_SAMPLE_RISING |	\
					V4L2_MBUS_HSYNC_ACTIVE_HIGH |	\
					V4L2_MBUS_VSYNC_ACTIVE_HIGH |	\
					V4L2_MBUS_DATA_ACTIVE_HIGH |	\
					SOCAM_MCLK_24MHZ)

#define ADV7181_STATUS1_REG		0x10
#define ADV7181_STATUS1_IN_LOCK		0x01
#define ADV7181_STATUS1_AUTOD_MASK	0x70
#define ADV7181_STATUS1_AUTOD_NTSM_M_J	0x00
#define ADV7181_STATUS1_AUTOD_NTSC_4_43 0x10
#define ADV7181_STATUS1_AUTOD_PAL_M	0x20
#define ADV7181_STATUS1_AUTOD_PAL_60	0x30
#define ADV7181_STATUS1_AUTOD_PAL_B_G	0x40
#define ADV7181_STATUS1_AUTOD_SECAM	0x50
#define ADV7181_STATUS1_AUTOD_PAL_COMB	0x60
#define ADV7181_STATUS1_AUTOD_SECAM_525	0x70

#define ADV7181_INPUT_CONTROL		0x00
#define ADV7181_INPUT_DEFAULT		0x00
#define ADV7181_INPUT_CVBS_AIN2		0x00
#define ADV7181_INPUT_CVBS_AIN3		0x01
#define ADV7181_INPUT_CVBS_AIN5		0x02
#define ADV7181_INPUT_CVBS_AIN6		0x03
#define ADV7181_INPUT_CVBS_AIN8		0x04
#define ADV7181_INPUT_CVBS_AIN10	0x05
#define ADV7181_INPUT_CVBS_AIN1		0x0B
#define ADV7181_INPUT_CVBS_AIN4		0x0D
#define ADV7181_INPUT_CVBS_AIN7		0x0F
#define ADV7181_INPUT_YPRPB_AIN6_8_10	0x09

static int SENSOR_PREVIEW_W = 720;
static int SENSOR_PREVIEW_H = 480;

static struct rk_camera_device_signal_config dev_info[] = {
	{
		.type = RK_CAMERA_DEVICE_CVBS_NTSC,
		.code = MEDIA_BUS_FMT_UYVY8_2X8,
		.crop = {
			.top = 24,
			.left = 0,
			.width = 720,
			.height = 480
		}
	}
};

static struct rk_camera_device_defrect defrects[4];
static struct rk_camera_device_channel_info channel_infos;
static int now_channel_value = ADV7181_INPUT_CVBS_AIN1;
static char input_mode[10] = "NTSC";

#define SENSOR_PREVIEW_FPS		30000		/* 30fps	*/
#define SENSOR_FULLRES_L_FPS		15000		/* 15fps	*/
#define SENSOR_FULLRES_H_FPS		15000		/* 15fps	*/
#define SENSOR_720P_FPS				0
#define SENSOR_1080P_FPS			0

#define SENSOR_REGISTER_LEN	1	/* sensor register address bytes */
#define SENSOR_VALUE_LEN	1	/* sensor register value bytes */

static unsigned int SensorConfiguration = (CFG_Effect | CFG_Scene);
static unsigned int SensorChipID[] = {SENSOR_ID};
/* Sensor Driver Configuration End */


#define SENSOR_NAME_STRING(a) STR(CONS(SENSOR_NAME, a))
#define SENSOR_NAME_VARFUN(a) CONS(SENSOR_NAME, a)

#define SensorRegVal(a, b) CONS4(SensorReg, SENSOR_REGISTER_LEN, Val, SENSOR_VALUE_LEN)(a, b)
#define sensor_write(client, reg, v) CONS4(sensor_write_reg, SENSOR_REGISTER_LEN, val, SENSOR_VALUE_LEN)(client, (reg), (v))
#define sensor_read(client, reg, v) CONS4(sensor_read_reg, SENSOR_REGISTER_LEN, val, SENSOR_VALUE_LEN)(client, (reg), (v))
#define sensor_write_array generic_sensor_write_array
static void adv7181_reinit_parameter(v4l2_std_id std,
				     struct generic_sensor *sensor);
static void adv7181_send_uevent(struct generic_sensor *sensor);

struct sensor_parameter {
	unsigned int PreviewDummyPixels;
	unsigned int CaptureDummyPixels;
	unsigned int preview_exposure;

	unsigned short int preview_line_width;
	unsigned short int preview_gain;
	unsigned short int PreviewPclk;
	unsigned short int CapturePclk;
	char awb[6];
};

struct specific_sensor {
	struct generic_sensor common_sensor;
	struct sensor_parameter parameter;
};

/*
 *  The follow setting need been filled.
 *
 *  Must Filled:
 *  sensor_init_data :               Sensor initial setting;
 *  sensor_fullres_lowfps_data :     Sensor full resolution setting with best auality, recommand for video;
 *  sensor_preview_data :            Sensor preview resolution setting, recommand it is vga or svga;
 *  sensor_softreset_data :          Sensor software reset register;
 *  sensor_check_id_data :           Sensir chip id register;
 *
 *  Optional filled:
 *  sensor_fullres_highfps_data:     Sensor full resolution setting with high framerate, recommand for video;
 *  sensor_720p:                     Sensor 720p setting, it is for video;
 *  sensor_1080p:                    Sensor 1080p setting, it is for video;
 *
 *  :::::WARNING:::::
 *  The SensorEnd which is the setting end flag must be filled int the last of each setting;
 */

/* Sensor initial setting */
static struct rk_sensor_reg sensor_init_data[] = {
/*
	{0x04, 0xC5},
	{0xE6, 0x4f},
 */
	SensorEnd
};
/* Senor full resolution setting: recommand for capture */
static struct rk_sensor_reg sensor_fullres_lowfps_data[] = {
	SensorEnd
};
/* Senor full resolution setting: recommand for video */
static struct rk_sensor_reg sensor_fullres_highfps_data[] = {
	SensorEnd
};
/* Preview resolution setting*/

static struct rk_sensor_reg sensor_preview_data_yc[] = {
	{0x04, 0x57},//Enable SFL
	{0x1D, 0x47},//Enable 28MHz Crystal
	{0x31, 0x02},//Clears NEWAV_MODE, 0xSAV/EAV  to suit ADV video encoders
	{0x3A, 0x13},//Set Latch Clock & turn off ADC2 & ADC3
	{0x3B, 0x81},//Enable Internal Bias
	{0x3D, 0xA2},//MWE Enable Manual Window, 0xColour Kill Threshold to 2
	{0x3E, 0x6A},//BLM optimisation
	{0x3F, 0xA0},//BGB
	{0x86, 0x0B},//Enable stdi_line_count_mode
	{0x69, 0x03},//Sets SDM_SEL to 03 for YC/CVBS Auto
	{0xF3, 0x03},//Enable Anti Alias Filters on ADC0 & ADC1
	{0xF9, 0x03},//Set max v lock range
	{0xC4, 0x80},//Enable maual input muxing
	{0xC3, 0xED},//ADC1 to Ain9 (C) and ADC0 to Ain7 (Y)
	{0x0E, 0x80},//ADI Recommended Setting
	{0x52, 0x46},//ADI Recommended Setting
	{0x54, 0x80},//ADI Recommended Setting
	{0x7F, 0xFF},//ADI Recommended Setting
	{0x81, 0x30},//ADI Recommended Setting
	{0x90, 0xC9},//ADI Recommended Setting
	{0x91, 0x40},//ADI Recommended Setting
	{0x92, 0x3C},//ADI Recommended Setting
	{0x93, 0xCA},//ADI Recommended Setting
	{0x94, 0xD5},//ADI Recommended Setting
	{0xB1, 0xFF},//ADI Recommended Setting
	{0xB6, 0x08},//ADI Recommended Setting
	{0xC0, 0x9A},//ADI Recommended Setting
	{0xCF, 0x50},//ADI Recommended Setting
	{0xD0, 0x4E},//ADI Recommended Setting
	{0xD1, 0xB9},//ADI Recommended Setting
	{0xD6, 0xDD},//ADI Recommended Setting
	{0xD7, 0xE2},//ADI Recommended Setting
	{0xE5, 0x51},//ADI Recommended Setting
	{0xF6, 0x3B},//ADI Recommended Setting
	{0x0E, 0x00},//ADI Recommended Setting
	{0x03, 0x4C},//disable out put
	SensorEnd
};

static struct rk_sensor_reg sensor_preview_data_cvbs[] = {
	{0x04, 0x77},
	{0x17, 0x41},
	{0x1D, 0x47},
	{0x31, 0x02},
	{0x3A, 0x17},
	{0x3B, 0x81},
	{0x3D, 0xA2},
	{0x3E, 0x6A},
	{0x3F, 0xA0},
	{0x86, 0x0B},
	{0xF3, 0x01},
	{0xF9, 0x03},
	{0x0E, 0x80},
	{0x52, 0x46},
	{0x54, 0x80},
	{0x7F, 0xFF},
	{0x81, 0x30},
	{0x90, 0xC9},
	{0x91, 0x40},
	{0x92, 0x3C},
	{0x93, 0xCA},
	{0x94, 0xD5},
	{0xB1, 0xFF},
	{0xB6, 0x08},
	{0xC0, 0x9A},
	{0xCF, 0x50},
	{0xD0, 0x4E},
	{0xD1, 0xB9},
	{0xD6, 0xDD},
	{0xD7, 0xE2},
	{0xE5, 0x51},
	{0xF6, 0x3B},
	{0x0E, 0x00},
	{0x03, 0x4C}, //disable out put
	SensorEnd
};

static struct rk_sensor_reg sensor_preview_data_yprpb_p[] = {
/*
	{0x05, 0x01},//PRIM_MODE = 001b COMP
	{0x06, 0x06},//VID_STD for 525P 2x1
	{0xC3, 0x56},//ADC1 to Ain8, ADC0 to Ain10,
	{0xC4, 0xB4},//Enables manual override of mux & ADC2 to Ain6.
	{0x1D, 0x47},//Enable 28.63636MHz crystal
	{0x3A, 0x11},//Set Latch Clock 01b. Power down ADC3.
	{0x3B, 0x81},//Enable Internal Bias
	{0x3C, 0x3B},//PLL QPUMP to 011b
	{0x6B, 0x83},//422 8bit out
	{0xC9, 0x0C},//Enable 8-bit mode using psuedo DDR
	{0x73, 0xCF},//Enable Manual Gain and set CH_A gain
	{0x74, 0xA3},//Set CH_A and CH_B Gain - 0FAh
	{0x75, 0xE8},//Set CH_B and CH_C Gain
	{0x76, 0xFA},//Set CH_C Gain
	{0x7B, 0x0E},//Turn on EAV and SAV Codes.
	{0x85, 0x19},//Turn off SSPD and force SOY
	{0x86, 0x0B},//Enable STDI Line Count Mode
	{0xBF, 0x06},//Blue Screen Free Run Colour
	{0xC0, 0x40},//default color
	{0xC1, 0xF0},//default color
	{0xC2, 0x80},//Default color
	{0xC5, 0x01},//
	{0x0E, 0x80},//ADI recommended sequence
	{0x52, 0x46},//ADI recommended sequence
	{0x54, 0x80},//ADI Recommended Setting
	{0x57, 0x01},//ADI recommended sequence
	{0xF6, 0x3B},//ADI Recommended Setting
	{0x0E, 0x00},//ADI recommended sequence
	{0x67, 0x33},//DPP Filters ***
 */
	/*##CP 525p/625p##
	 *525p/60Hz YPrPb In 8Bit 422 HS/VS Encoder
	 */
	{0x05, 0x01},
	{0x06, 0x06},
	{0xc3, 0x56},
	{0xc4, 0xb4},
	{0x1d, 0x47},
	{0x3a, 0x11},
	{0x3b, 0x81},
	{0x3c, 0x3b},
	{0x6b, 0x83},
	{0xc9, 0x00},
	{0x73, 0x10},
	{0x74, 0xa3},
	{0x75, 0xe8},
	{0x76, 0xfa},
	{0x7b, 0x1c},
	{0x85, 0x19},
	{0x86, 0x0b},
	{0xbf, 0x06},
	{0xc0, 0x40},
	{0xc1, 0xf0},
	{0xc2, 0x80},
	{0xc5, 0x01},
	{0xc9, 0x08},
	{0x0e, 0x80},
	{0x52, 0x46},
	{0x54, 0x80},
	{0x57, 0x01},
	{0xf6, 0x3b},
	{0x0e, 0x00},
	{0x67, 0x2f},
	{0x03, 0x4C}, //disable out put
	SensorEnd
};

static struct rk_sensor_reg sensor_preview_data_yprpb_i[] = {
/*	##SDP YPrPb##
 *	:AUTODETECT YPbPr In, 8 Bit 422 Encoder
 */
	{0x8D, 0x83},//ADI Recommended
	{0x1D, 0x47},//Enable 28MHz Crystal
	{0x27, 0x98},//YC Delay, 0x Swap Cb and Cr
	{0x31, 0x02},//Clears NEWAV_MODE, 0xSAV/EAV  to suit ADV video encoders
	{0x3A, 0x11},//set latch clock settings to 001b, 0xPower Down ADC3
	{0x3B, 0x81},//Enable internal Bias
	{0x3D, 0xA2},//MWE Enable Manual Window
	{0x3E, 0x6A},//BLM optimisation
	{0x3F, 0xA0},//ADI Recommended
	{0x86, 0x0B},//Enable stdi_line_count_mode
	{0xB4, 0xF9},//ADI Recommended
	{0xB5, 0x00},//ADI Recommended
	{0xC3, 0x46},//ADC1 to Ain6 (Pr), 0xADC0 to Ain10 (Y), 0x
	{0xC4, 0xB5},//ADC2 to Ain8 (Pb) and enables manual override of mux
	{0xF3, 0x07},//Enable Anti Alias Filters on ADC 0,1,2
	{0xF9, 0x03},//Set max v lock range
	{0x0E, 0x80},//ADI Recommended Setting
	{0x52, 0x46},//ADI Recommended Setting
	{0x54, 0x80},//ADI Recommended Setting
	{0x7F, 0xFF},//ADI Recommended Setting
	{0x81, 0x30},//ADI Recommended Setting
	{0x90, 0xC9},//ADI Recommended Setting
	{0x91, 0x40},//ADI Recommended Setting
	{0x92, 0x3C},//ADI Recommended Setting
	{0x93, 0xCA},//ADI Recommended Setting
	{0x94, 0xD5},//ADI Recommended Setting
	{0x7E, 0x73},//ADI Recommended Setting
	{0xB1, 0xFF},//ADI Recommended Setting
	{0xB6, 0x08},//ADI Recommended Setting
	{0xC0, 0x9A},//ADI Recommended Setting
	{0xCF, 0x50},//ADI Recommended Setting
	{0xD0, 0x4E},//ADI Recommended Setting
	{0xD1, 0xB9},//ADI Recommended Setting
	{0xD6, 0xDD},//ADI Recommended Setting
	{0xE5, 0x51},//ADI Recommended Setting
	{0xF6, 0x3B},//ADI Recommended Setting
	{0x0E, 0x00},//ADI Recommended Setting
	{0x03, 0x4C},//8 Bit Mode; disable out put
	SensorEnd
};

static struct rk_sensor_reg sensor_preview_data[] = {
	/* autodetect cvbs in ntsc/pal/secam 8-bit 422 encode */
	{0x04, 0x77},
	{0x17, 0x41},
	{0x1D, 0x47},
	{0x31, 0x02},
	{0x3A, 0x17},
	{0x3B, 0x81},
	{0x3D, 0xA2},
	{0x3E, 0x6A},
	{0x3F, 0xA0},
	{0x86, 0x0B},
	{0xF3, 0x01},
	{0xF9, 0x03},
	{0x0E, 0x80},
	{0x52, 0x46},
	{0x54, 0x80},
	{0x7F, 0xFF},
	{0x81, 0x30},
	{0x90, 0xC9},
	{0x91, 0x40},
	{0x92, 0x3C},
	{0x93, 0xCA},
	{0x94, 0xD5},
	{0xB1, 0xFF},
	{0xB6, 0x08},
	{0xC0, 0x9A},
	{0xCF, 0x50},
	{0xD0, 0x4E},
	{0xD1, 0xB9},
	{0xD6, 0xDD},
	{0xD7, 0xE2},
	{0xE5, 0x51},
	{0xF6, 0x3B},
	{0x0E, 0x00},
	{0x03, 0x4C}, //disable out put
	SensorEnd
};

/* 1280x720 */
static struct rk_sensor_reg sensor_720p[] = {
	SensorEnd
};

/* 1920x1080 */
static struct rk_sensor_reg sensor_1080p[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_softreset_data[] = {
	/*SensorRegVal(0x0f, 0x00 | 0x80),*/
	SensorEnd
};

static struct rk_sensor_reg sensor_check_id_data[] = {
	SensorRegVal(0x11, 0x0),
	SensorEnd
};
/*
 *  The following setting must been filled,
 *  if the function is turn on by CONFIG_SENSOR_xxxx
 */
static struct rk_sensor_reg sensor_WhiteB_Auto[] = {
	SensorEnd
};
/* Cloudy Colour Temperature : 6500K - 8000K  */
static	struct rk_sensor_reg sensor_WhiteB_Cloudy[] = {
	SensorEnd
};
/* ClearDay Colour Temperature : 5000K - 6500K	*/
static	struct rk_sensor_reg sensor_WhiteB_ClearDay[] = {
	SensorEnd
};
/* Office Colour Temperature : 3500K - 5000K  */
static	struct rk_sensor_reg sensor_WhiteB_TungstenLamp1[] = {
	SensorEnd
};
/* Home Colour Temperature : 2500K - 3500K	*/
static	struct rk_sensor_reg sensor_WhiteB_TungstenLamp2[] = {
	SensorEnd
};
static struct rk_sensor_reg *sensor_WhiteBalanceSeqe[] = {
	sensor_WhiteB_Auto,
	sensor_WhiteB_TungstenLamp1,
	sensor_WhiteB_TungstenLamp2,
	sensor_WhiteB_ClearDay,
	sensor_WhiteB_Cloudy,
	NULL,
};

static struct rk_sensor_reg sensor_Brightness0[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Brightness1[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Brightness2[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Brightness3[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Brightness4[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Brightness5[] = {
	SensorEnd
};

static struct rk_sensor_reg *sensor_BrightnessSeqe[] = {
	sensor_Brightness0,
	sensor_Brightness1,
	sensor_Brightness2,
	sensor_Brightness3,
	sensor_Brightness4,
	sensor_Brightness5,
	NULL,
};

static struct rk_sensor_reg sensor_Effect_Normal[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Effect_WandB[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Effect_Sepia[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Effect_Negative[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Effect_Bluish[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Effect_Green[] = {
	SensorEnd
};

static struct rk_sensor_reg *sensor_EffectSeqe[] = {
	sensor_Effect_Normal,
	sensor_Effect_WandB,
	sensor_Effect_Negative,
	sensor_Effect_Sepia,
	sensor_Effect_Bluish,
	sensor_Effect_Green,
	NULL,
};

static struct rk_sensor_reg sensor_Exposure0[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Exposure1[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Exposure2[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Exposure3[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Exposure4[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Exposure5[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Exposure6[] = {
	SensorEnd
};

static struct rk_sensor_reg *sensor_ExposureSeqe[] = {
	sensor_Exposure0,
	sensor_Exposure1,
	sensor_Exposure2,
	sensor_Exposure3,
	sensor_Exposure4,
	sensor_Exposure5,
	sensor_Exposure6,
	NULL,
};

static struct rk_sensor_reg sensor_Saturation0[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Saturation1[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Saturation2[] = {
	SensorEnd
};
static struct rk_sensor_reg *sensor_SaturationSeqe[] = {
	sensor_Saturation0,
	sensor_Saturation1,
	sensor_Saturation2,
	NULL,
};

static struct rk_sensor_reg sensor_Contrast0[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Contrast1[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Contrast2[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Contrast3[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Contrast4[] = {
	SensorEnd
};


static struct rk_sensor_reg sensor_Contrast5[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Contrast6[] = {
	SensorEnd
};
static struct rk_sensor_reg *sensor_ContrastSeqe[] = {
	sensor_Contrast0,
	sensor_Contrast1,
	sensor_Contrast2,
	sensor_Contrast3,
	sensor_Contrast4,
	sensor_Contrast5,
	sensor_Contrast6,
	NULL,
};

static struct rk_sensor_reg sensor_SceneAuto[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_SceneNight[] = {
	SensorEnd
};
static struct rk_sensor_reg *sensor_SceneSeqe[] = {
	sensor_SceneAuto,
	sensor_SceneNight,
	NULL,
};

static struct rk_sensor_reg sensor_Zoom0[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Zoom1[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Zoom2[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Zoom3[] = {
	SensorEnd
};

static struct rk_sensor_reg *sensor_ZoomSeqe[] = {
	sensor_Zoom0,
	sensor_Zoom1,
	sensor_Zoom2,
	sensor_Zoom3,
	NULL,
};

/*
 * User could be add v4l2_querymenu in sensor_controls by new_usr_v4l2menu
 */
static struct v4l2_querymenu sensor_menus[] = {
};

/*
 * User could be add v4l2_queryctrl in sensor_controls by new_user_v4l2ctrl
 */

static inline int adv7181_channel_set(struct i2c_client *client,
				      struct sensor_v4l2ctrl_info_s *ctrl_info,
				      struct v4l2_ext_control *ext_ctrl)
{
	struct generic_sensor *sensor = to_generic_sensor(client);
	struct rk_sensor_sequence *sensor_series =
		sensor->info_priv.sensor_series;
	int series_num = sensor->info_priv.num_series;
	int ret = 0;
	int i;
	int id;
	int channel_ain;

	if ((ext_ctrl->value < ctrl_info->qctrl->minimum) ||
	    (ext_ctrl->value > ctrl_info->qctrl->maximum)) {
		SENSOR_TR("%s(%d):channel(%d) is not support\n",
			  __func__, __LINE__, ext_ctrl->value);
		ret = -EINVAL;
		goto end;
	}
	if (sensor->channel_id != ext_ctrl->value) {
		SENSOR_TR("%s(%d):set channel(%d)!\n",
			  __func__, __LINE__, ext_ctrl->value);
		sensor->channel_id = ext_ctrl->value;
		id = sensor->channel_id;
		for (i = 0; i < series_num; i++)
			if ((sensor_series[i].property == SEQUENCE_PREVIEW) &&
			    (sensor_series[i].data[0].reg != SEQCMD_END))
				break;
		if (strstr(channel_infos.channel_info[id], "cvbs")) {
			ret = sscanf(channel_infos.channel_info[id], "%*s %d",
				     &channel_ain);
			if (IS_ERR_VALUE(ret)) {
				SENSOR_TR("%s(%d): channel_infos err!\n",
					  __func__, __LINE__);
				ret = -EINVAL;
				goto end;
			}
			switch (channel_ain) {
			case 1:
				now_channel_value = ADV7181_INPUT_CVBS_AIN1;
				break;
			case 2:
				now_channel_value = ADV7181_INPUT_CVBS_AIN2;
				break;
			case 3:
				now_channel_value = ADV7181_INPUT_CVBS_AIN3;
				break;
			case 4:
				now_channel_value = ADV7181_INPUT_CVBS_AIN4;
				break;
			case 5:
				now_channel_value = ADV7181_INPUT_CVBS_AIN5;
				break;
			case 6:
				now_channel_value = ADV7181_INPUT_CVBS_AIN6;
				break;
			case 7:
				now_channel_value = ADV7181_INPUT_CVBS_AIN7;
				break;
			case 8:
				now_channel_value = ADV7181_INPUT_CVBS_AIN8;
				break;
			case 10:
				now_channel_value = ADV7181_INPUT_CVBS_AIN10;
				break;
			}
			sensor_series[i].data = sensor_preview_data_cvbs;
			sensor->info_priv.dev_sig_cnf.type =
				RK_CAMERA_DEVICE_CVBS_NTSC;
			sensor->info_priv.dev_sig_cnf.code =
				MEDIA_BUS_FMT_UYVY8_2X8;
			adv7181_reinit_parameter(V4L2_STD_NTSC, sensor);
		} else if (strstr(channel_infos.channel_info[id], "yprpb")) {
			now_channel_value = ADV7181_INPUT_DEFAULT;
			sensor_series[i].data = sensor_preview_data_yprpb_p;
			sensor->info_priv.dev_sig_cnf.type =
				RK_CAMERA_DEVICE_BT601_PIONGPONG;
			sensor->info_priv.dev_sig_cnf.code =
				MEDIA_BUS_FMT_YVYU8_2X8;
			adv7181_reinit_parameter(V4L2_STD_525_60, sensor);
		} else if (strstr(channel_infos.channel_info[id], "ycrpc")) {
			now_channel_value = ADV7181_INPUT_YPRPB_AIN6_8_10;
			sensor_series[i].data = sensor_preview_data_yprpb_i;
			sensor->info_priv.dev_sig_cnf.type =
				RK_CAMERA_DEVICE_CVBS_NTSC;
			sensor->info_priv.dev_sig_cnf.code =
				MEDIA_BUS_FMT_UYVY8_2X8;
		} else if (strstr(channel_infos.channel_info[id], "yc")) {
			SENSOR_TR("%s(%d): yc signal not support\n",
				  __func__, __LINE__);
			ret = -EINVAL;
			goto end;
			now_channel_value = ADV7181_INPUT_DEFAULT;
			sensor_series[i].data = sensor_preview_data_yc;
		}

		sensor_write(client, ADV7181_INPUT_CONTROL, now_channel_value);
		generic_sensor_write_array(client, sensor_series[i].data);
	}

end:
	return ret;
}

static inline int sensor_v4l2ctrl_inside_cb(struct soc_camera_device *icd,
					    struct sensor_v4l2ctrl_info_s *ctrl_info,
					    struct v4l2_ext_control *ext_ctrl,
					    bool is_set)
{
	struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
	struct generic_sensor *sensor = to_generic_sensor(client);
	int ret = 0;

	switch (ctrl_info->qctrl->id) {
	case V4L2_CID_DEINTERLACE:
		{
			if (is_set) {
				SENSOR_TR("deinterlace isn't support set!\n");
				ret = -EINVAL;
				goto cb_end;
			} else {
				if ((RK_CAMERA_DEVICE_BT601_8 ==
				     sensor->info_priv.dev_sig_cnf.type) ||
				    (RK_CAMERA_DEVICE_BT601_PIONGPONG ==
				     sensor->info_priv.dev_sig_cnf.type)) {
					/* don't need deinterlace process */
					ext_ctrl->value = 0;
					ctrl_info->cur_value = 0;
				} else {
					ext_ctrl->value = 1;
					ctrl_info->cur_value = 1;
				}
			}
			break;
		}
	case V4L2_CID_CHANNEL:
	{
		if (is_set) {
			ret = adv7181_channel_set(client, ctrl_info, ext_ctrl);
			if (ret)
				goto cb_end;
		} else {
			ext_ctrl->value = sensor->channel_id;
			ctrl_info->cur_value = sensor->channel_id;
		}
		break;
	}
	case V4L2_CID_VIDEO_STATE:
	{
		if (is_set) {
			SENSOR_TR("%s(%d): set isn't support!\n",
				  __func__, __LINE__);
		} else {
			ext_ctrl->value = sensor->info_priv.video_state;
			ctrl_info->cur_value = sensor->info_priv.video_state;
		}
		break;
	}
	default:
		{
			SENSOR_TR("%s(%d): cmd(0x%x) is unknown !",
				  __func__, __LINE__, ctrl_info->qctrl->id);
			ret = -EINVAL;
		}
	}

cb_end:
	return ret;
}

static struct sensor_v4l2ctrl_usr_s sensor_controls[] = {
	{
		{
			V4L2_CID_DEINTERLACE,
			V4L2_CTRL_TYPE_BOOLEAN,
			"deinterlace",
			0,
			1,
			1,
			0
		},
		sensor_v4l2ctrl_inside_cb,
		NULL
	},
	{
		{
			V4L2_CID_CHANNEL,
			V4L2_CTRL_TYPE_INTEGER,
			"channel",
			0,
			4,
			1,
			0
		},
		sensor_v4l2ctrl_inside_cb,
		NULL
	},
	{
		{
			V4L2_CID_VIDEO_STATE,
			V4L2_CTRL_TYPE_BOOLEAN,
			"video_state",
			0,
			1,
			1,
			0
		},
		sensor_v4l2ctrl_inside_cb,
		NULL
	}
};

/*
 * MUST define the current used format as the first item
 */
static struct rk_sensor_datafmt sensor_colour_fmts[] = {
	{MEDIA_BUS_FMT_UYVY8_2X8, V4L2_COLORSPACE_JPEG},
};

/*
 **********************************************************
 * Following is local code:
 *
 * Please codeing your program here
 **********************************************************
 */
static int sensor_parameter_record(struct i2c_client *client)
{
	return 0;
}

static int sensor_ae_transfer(struct i2c_client *client)
{
	return 0;
}
/*
 **********************************************************
 * Following is callback
 * If necessary, you could coding these callback
 **********************************************************
 */
static void adv7181_reinit_parameter(v4l2_std_id std,
				     struct generic_sensor *sensor)
{
	struct rk_sensor_sequence *series = sensor->info_priv.sensor_series;
	int num_series = sensor->info_priv.num_series;
	int i;

	if (std == V4L2_STD_NTSC || std == V4L2_STD_NTSC_443) {
		SENSOR_PREVIEW_W = 720;
		SENSOR_PREVIEW_H = 480;
		strcpy(input_mode, "NTSC");
	} else if (std == V4L2_STD_PAL || std == V4L2_STD_PAL_60 ||
		   std == V4L2_STD_PAL_M) {
		SENSOR_PREVIEW_W = 720;
		SENSOR_PREVIEW_H = 576;
		strcpy(input_mode, "PAL");
	} else {
		SENSOR_PREVIEW_W = 1024;
		SENSOR_PREVIEW_H = 500;
		strcpy(input_mode, "YUV");
	}
	for (i = 0; i < 4; i++) {
		if ((defrects[i].width == SENSOR_PREVIEW_W) &&
		    (defrects[i].height == SENSOR_PREVIEW_H)) {
			SENSOR_PREVIEW_W = defrects[i].defrect.width;
			SENSOR_PREVIEW_H = defrects[i].defrect.height;
			memcpy(&sensor->info_priv.dev_sig_cnf.crop,
			       &defrects[i].defrect,
			       sizeof(defrects[i].defrect));

			if (!defrects[i].interface) {
				SENSOR_TR("%s(%d): interface is NULL\n",
					  __func__, __LINE__);
				continue;
			}

			SENSOR_TR("%s(%d): type 0x%x\n", __func__, __LINE__,
				  sensor->info_priv.dev_sig_cnf.type);
		}
	}
	/*update sensor info_priv*/
	for (i = 0; i < num_series; i++) {
		series[i].gSeq_info.w = SENSOR_PREVIEW_W;
		series[i].gSeq_info.h = SENSOR_PREVIEW_H;
	}
	generic_sensor_get_max_min_res(sensor->info_priv.sensor_series,
				       sensor->info_priv.num_series,
				       &(sensor->info_priv.max_real_res),
				       &(sensor->info_priv.max_res),
				       &(sensor->info_priv.min_res));
}

static void adv7181_send_uevent(struct generic_sensor *sensor)
{
	char *event_msg = NULL;
	char *envp[2];

	event_msg = kasprintf(GFP_KERNEL,
			      "CVBS_NAME=ADV7181, NOW_INPUT_MODE=%s, RESOLUTION=%dx%d",
			      input_mode, SENSOR_PREVIEW_W, SENSOR_PREVIEW_H);
	SENSOR_TR("%s(%d): event_msg: %s\n", __func__, __LINE__, event_msg);
	envp[0] = event_msg;
	envp[1] = NULL;
	kobject_uevent_env(&(sensor->subdev.v4l2_dev->dev->kobj),
			   KOBJ_CHANGE, envp);
}

static v4l2_std_id adv7181_std_to_v4l2(u8 status1)
{
	/* in case V4L2_IN_ST_NO_SIGNAL */
	if (!(status1 & ADV7181_STATUS1_IN_LOCK))
		return V4L2_STD_UNKNOWN;

	switch (status1 & ADV7181_STATUS1_AUTOD_MASK) {
	case ADV7181_STATUS1_AUTOD_NTSM_M_J:
		return V4L2_STD_NTSC;
	case ADV7181_STATUS1_AUTOD_NTSC_4_43:
		return V4L2_STD_NTSC_443;
	case ADV7181_STATUS1_AUTOD_PAL_M:
		return V4L2_STD_PAL_M;
	case ADV7181_STATUS1_AUTOD_PAL_60:
		return V4L2_STD_PAL_60;
	case ADV7181_STATUS1_AUTOD_PAL_B_G:
		return V4L2_STD_PAL;
	case ADV7181_STATUS1_AUTOD_SECAM:
		return V4L2_STD_SECAM;
	case ADV7181_STATUS1_AUTOD_PAL_COMB:
		return V4L2_STD_PAL_Nc | V4L2_STD_PAL_N;
	case ADV7181_STATUS1_AUTOD_SECAM_525:
		return V4L2_STD_SECAM;
	default:
		return V4L2_STD_UNKNOWN;
	}
}

static u32 adv7181_status_to_v4l2(u8 status1)
{
	if (!(status1 & ADV7181_STATUS1_IN_LOCK))
		return V4L2_IN_ST_NO_SIGNAL;

	return 0;
}

static int adv7181_uevent_video_state(struct i2c_client *client, int state)
{
	struct generic_sensor *sensor = to_generic_sensor(client);
	char *event_msg = NULL;
	char *envp[2];

	return 0;
	event_msg = kasprintf(GFP_KERNEL, "CVBS_NAME=ADV7181, VIDEO_STATUS=%d", state);
	SENSOR_TR("%s(%d): event_msg: %s\n", __func__, __LINE__, event_msg);
	envp[0] = event_msg;
	envp[1] = NULL;
	kobject_uevent_env(&(sensor->subdev.v4l2_dev->dev->kobj), KOBJ_CHANGE, envp);

	return 0;
}

static int adv7181_status(struct i2c_client *client, u32 *status,
			  v4l2_std_id *std)
{
	unsigned char status1 = 0;
	int ret = 0;
	struct generic_sensor *sensor = to_generic_sensor(client);

	ret = sensor_read(client, ADV7181_STATUS1_REG, &status1);

	if (IS_ERR_VALUE(ret)) {
		SENSOR_TR("%s(%d): register read failed: 0x%x\n",
			  __func__, __LINE__, ADV7181_STATUS1_REG);
		return status1;
	}
	SENSOR_DG("%s(%d): ADV7181_STATUS1_REG read : 0x%x\n",
		  __func__, __LINE__, status1);

	if (status1 < 0)
		return status1;

	if (adv7181_status_to_v4l2(status1) == V4L2_IN_ST_NO_SIGNAL) {
		sensor->info_priv.video_state = RK_CAM_INPUT_VIDEO_STATE_LOSS;
		adv7181_uevent_video_state(client, 0);
	} else {
		if (sensor->info_priv.video_state == RK_CAM_INPUT_VIDEO_STATE_LOSS) {
			sensor->info_priv.video_state = RK_CAM_INPUT_VIDEO_STATE_LOCKED;
			adv7181_uevent_video_state(client, 1);
		}
	}

	if (status)
		*status = adv7181_status_to_v4l2(status1);
	if (std)
		*std = adv7181_std_to_v4l2(status1);

	return 0;
}

/*
 * the function is called in open sensor
 */
static int sensor_activate_cb(struct i2c_client *client)
{
	struct generic_sensor *sensor = to_generic_sensor(client);
/*	u32 status = V4L2_IN_ST_NO_SIGNAL;
	v4l2_std_id std;

	while (status == V4L2_IN_ST_NO_SIGNAL) {
		msleep(20);
		adv7181_status(client, &status, NULL);
	}
	SENSOR_TR("status %d\n", (int)status);
	adv7181_status(client, NULL, &std);
	adv7181_reinit_parameter(std, sensor);
	SENSOR_TR("%s(%d):now mode %s\n", __func__, __LINE__, input_mode);
 */
	SENSOR_DG("%s: %d\n", __func__, __LINE__);
	sensor_write(client, ADV7181_INPUT_CONTROL, now_channel_value);

	sensor->info_priv.video_state = RK_CAM_INPUT_VIDEO_STATE_LOCKED;
	if (sensor->state_check_work.state_check_wq) {
		SENSOR_DG("sensor_activate_cb: queue_delayed_work 1000ms");
		queue_delayed_work(sensor->state_check_work.state_check_wq,
				   &sensor->state_check_work.work, 100);
	}

	return 0;
}
/*
 * the function is called in close sensor
 */
static int sensor_deactivate_cb(struct i2c_client *client)
{
	int ret = 0;
	struct generic_sensor *sensor = to_generic_sensor(client);

	ret = cancel_delayed_work_sync(&sensor->state_check_work.work);
	return ret;
}

static int sensor_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	SENSOR_DG("stream %d\n", enable);
	if (enable)
		sensor_write(client, 0x03, 0x0C);
	else
		sensor_write(client, 0x03, 0x4C);
	return 0;
}

/*
 * the function is called before sensor register setting in VIDIOC_S_FMT
*/
static int sensor_s_fmt_cb_th(struct i2c_client *client,
			      struct v4l2_mbus_framefmt *mf,
			      bool capture)
{
	if (capture)
		sensor_parameter_record(client);

	return 0;
}
/*
 * the function is called after sensor register setting finished in VIDIOC_S_FMT
 */
static int sensor_s_fmt_cb_bh(struct i2c_client *client,
			      struct v4l2_mbus_framefmt *mf,
			      bool capture)
{
	if (capture)
		sensor_ae_transfer(client);

	return 0;
}
static int sensor_try_fmt_cb_th(struct i2c_client *client,
				struct v4l2_mbus_framefmt *mf)
{
	return 0;
}

static int sensor_softrest_usr_cb(struct i2c_client *client,
				  struct rk_sensor_reg *series)
{
	return 0;
}
static int sensor_check_id_usr_cb(struct i2c_client *client,
				  struct rk_sensor_reg *series)
{
	return 0;
}

static int sensor_suspend(struct soc_camera_device *icd, pm_message_t pm_msg)
{
	return 0;
}

static int sensor_resume(struct soc_camera_device *icd)
{
	SENSOR_DG("Resume");

	return 0;
}
static int sensor_mirror_cb(struct i2c_client *client, int mirror)
{
	return 0;
}
/*
 * the function is v4l2 control V4L2_CID_HFLIP callback
 */
static int sensor_v4l2ctrl_mirror_cb(struct soc_camera_device *icd,
				     struct sensor_v4l2ctrl_info_s *ctrl_info,
				     struct v4l2_ext_control *ext_ctrl)
{
	SENSOR_DG("sensor_mirror success, value:0x%x", ext_ctrl->value);
	return 0;
}

static int sensor_flip_cb(struct i2c_client *client, int flip)
{
	return 0;
}
/*
 * the function is v4l2 control V4L2_CID_VFLIP callback
 */
static int sensor_v4l2ctrl_flip_cb(struct soc_camera_device *icd,
				   struct sensor_v4l2ctrl_info_s *ctrl_info,
				   struct v4l2_ext_control *ext_ctrl)
{
	SENSOR_DG("sensor_flip success, value:0x%x", ext_ctrl->value);
	return 0;
}
/*
 * the functions are focus callbacks
 */
static int sensor_focus_init_usr_cb(struct i2c_client *client)
{
	return 0;
}

static int sensor_focus_af_single_usr_cb(struct i2c_client *client)
{
	return 0;
}

static int sensor_focus_af_near_usr_cb(struct i2c_client *client)
{
	return 0;
}

static int sensor_focus_af_far_usr_cb(struct i2c_client *client)
{
	return 0;
}

static int sensor_focus_af_specialpos_usr_cb(struct i2c_client *client, int pos)
{
	return 0;
}

static int sensor_focus_af_const_usr_cb(struct i2c_client *client)
{
	return 0;
}
static int sensor_focus_af_const_pause_usr_cb(struct i2c_client *client)
{
	return 0;
}
static int sensor_focus_af_close_usr_cb(struct i2c_client *client)
{
	return 0;
}

static int sensor_focus_af_zoneupdate_usr_cb(struct i2c_client *client,
					     int *zone_tm_pos)
{
	return 0;
}

/*
 *face defect call back
 */
static int sensor_face_detect_usr_cb(struct i2c_client *client, int on)
{
	return 0;
}

/* config debug fs ops */
#define DEBUG_FS_NTSC_WIDTH 0x8000
#define DEBUG_FS_NTSC_HEIGHT 0x8001
#define DEBUG_FS_NTSC_LEFT 0x8002
#define DEBUG_FS_NTSC_TOP 0x8003

#define DEBUG_FS_PAL_WIDTH 0x8004
#define DEBUG_FS_PAL_HEIGHT 0x8005
#define DEBUG_FS_PAL_LEFT 0x8006
#define DEBUG_FS_PAL_TOP 0x8007

static ssize_t adv7181_debugfs_reg_write(struct file *file,
					 const char __user *buf,
					 size_t count, loff_t *ppos)
{
	struct specific_sensor *spsensor =
		((struct seq_file *)file->private_data)->private;
	struct i2c_client *client = spsensor->common_sensor.client;
	int reg, val, ret;
	unsigned char read;
	char kbuf[30];
	int nbytes = min(count, sizeof(kbuf) - 1);
	int i = 0;

	if (copy_from_user(kbuf, buf, nbytes))
		return -EFAULT;

	kbuf[nbytes] = '\0';
	if (sscanf(kbuf, " %x %x", &reg, &val) != 2)
		return -EINVAL;

	SENSOR_TR("%s(%d): register write reg: 0x%x, val 0x%x\n",
		  __func__, __LINE__, reg, val);

	switch (reg) {
	case DEBUG_FS_NTSC_WIDTH:
		{
			for (i = 0; i < 4; i++)
				if (defrects[i].height == 480)
					defrects[i].defrect.width = val;
			break;
		}
	case DEBUG_FS_NTSC_HEIGHT:
		{
			for (i = 0; i < 4; i++)
				if (defrects[i].height == 480)
					defrects[i].defrect.height = val;
			break;
		}
	case DEBUG_FS_NTSC_TOP:
		{
			for (i = 0; i < 4; i++)
				if (defrects[i].height == 480)
					defrects[i].defrect.top = val;
			break;
		}
	case DEBUG_FS_NTSC_LEFT:
		{
			for (i = 0; i < 4; i++)
				if (defrects[i].height == 480)
					defrects[i].defrect.left = val;
			break;
		}
	case DEBUG_FS_PAL_WIDTH:
		{
			for (i = 0; i < 4; i++)
				if (defrects[i].height == 576)
					defrects[i].defrect.width = val;
			break;
		}
	case DEBUG_FS_PAL_HEIGHT:
		{
			for (i = 0; i < 4; i++)
				if (defrects[i].height == 576)
					defrects[i].defrect.height = val;
			break;
		}
	case DEBUG_FS_PAL_LEFT:
		{
			for (i = 0; i < 4; i++)
				if (defrects[i].height == 576)
					defrects[i].defrect.left = val;
			break;
		}
	case DEBUG_FS_PAL_TOP:
		{
			for (i = 0; i < 4; i++)
				if (defrects[i].height == 576)
					defrects[i].defrect.top = val;
			break;
		}
	default:
		{
			ret = sensor_write(client, reg, val);
			if (IS_ERR_VALUE(ret)) {
				SENSOR_TR("d_fs: write fail: 0x%x, val 0x%x\n",
					  reg, val);
			}

			ret = sensor_read(client, reg, &read);
			if (IS_ERR_VALUE(ret)) {
				SENSOR_TR("d_fs: write fail: 0x%x, val 0x%x\n",
					  reg, read);
			} else
				SENSOR_TR("d_fs: read 0x%x return 0x%x\n",
					  reg, val);
			break;
		}
	}

	return count;
}

static int adv7181_debugfs_reg_show(struct seq_file *s, void *v)
{
	int i, ret;
	unsigned char val;
	struct specific_sensor *spsensor = s->private;
	struct i2c_client *client = spsensor->common_sensor.client;

	SENSOR_TR("%s(%d): test\n", __func__, __LINE__);

	for (i = 0; i <= 0xff; i++) {
		ret = sensor_read(client, i, &val);
		if (IS_ERR_VALUE(ret))
			SENSOR_TR("%s(%d): register read failed: 0x%x\n",
				  __func__, __LINE__, i);

		seq_printf(s, "0x%02x : 0x%02x\n", i, (u8)val);
	}

	return 0;
}

static int adv7181_debugfs_open(struct inode *inode, struct file *file)
{
	struct specific_sensor *spsensor = inode->i_private;

	return single_open(file, adv7181_debugfs_reg_show, spsensor);
}

static const struct file_operations adv7181_debugfs_fops = {
	.owner			= THIS_MODULE,
	.open			= adv7181_debugfs_open,
	.read			= seq_read,
	.write			= adv7181_debugfs_reg_write,
	.llseek			= seq_lseek,
	.release		= single_release
};

static void adv7181_check_state_work(struct work_struct *work)
{
	struct rk_state_check_work *state_check_work =
		container_of(work, struct rk_state_check_work, work.work);
	struct generic_sensor *sensor =
		container_of(state_check_work,
			     struct generic_sensor,
			     state_check_work);
	struct i2c_client *client = sensor->client;
	static v4l2_std_id std_old = V4L2_STD_NTSC;
	v4l2_std_id std;

	if ((sensor->info_priv.dev_sig_cnf.type ==
	     RK_CAMERA_DEVICE_BT601_8) ||
	    (sensor->info_priv.dev_sig_cnf.type ==
	     RK_CAMERA_DEVICE_BT601_PIONGPONG))
		return;

	adv7181_status(client, NULL, &std);
	if ((std_old != std) && (std != V4L2_STD_UNKNOWN)) {
		adv7181_reinit_parameter(std, sensor);
		adv7181_send_uevent(sensor);
		std_old = std;
		SENSOR_TR("%s(%d):now mode %s\n",
			  __func__, __LINE__, input_mode);
	}

	queue_delayed_work(sensor->state_check_work.state_check_wq,
			   &sensor->state_check_work.work, 100);
}

/*
 *   The function can been run in sensor_init_parametres which run in
 *   sensor_probe, so user can do some initialization in the function.
 */
static void sensor_init_parameters_user(struct specific_sensor *spsensor,
					struct soc_camera_device *icd)
{
	struct soc_camera_desc *desc = to_soc_camera_desc(icd);
	struct rk29camera_platform_data *pdata = desc->subdev_desc.drv_priv;
	struct rkcamera_platform_data *sensor_device = NULL, *new_camera;
	struct dentry *debugfs_dir =
		spsensor->common_sensor.info_priv.debugfs_dir;
	int i;

	new_camera = pdata->register_dev_new;
	while (new_camera != NULL) {
		SENSOR_TR("%s(%d): icd_name %s, new_camera_name %s.\n",
			  __func__, __LINE__,
			  dev_name(icd->pdev), new_camera->dev_name);
		if (strcmp(dev_name(icd->pdev), new_camera->dev_name) == 0) {
			sensor_device = new_camera;
			break;
		}
		new_camera = new_camera->next_camera;
	}
	if (!sensor_device) {
		SENSOR_TR("%s(%d): Could not find %s\n", __func__, __LINE__,
			  dev_name(icd->pdev));
		return;
	}
	memcpy(&defrects, &sensor_device->defrects,
	       sizeof(sensor_device->defrects));
	for (i = 0; i < 4; i++) {
		if ((defrects[i].width == SENSOR_PREVIEW_W) &&
		    (defrects[i].height == SENSOR_PREVIEW_H)) {
			memcpy(&dev_info[0].crop, &defrects[i].defrect,
			       sizeof(dev_info[0].crop));
			if (!strcmp(defrects[i].interface, "bt601_8"))
				dev_info[i].type = RK_CAMERA_DEVICE_BT601_8;
			if (!strcmp(defrects[i].interface, "cvbs_ntsc"))
				dev_info[i].type = RK_CAMERA_DEVICE_CVBS_NTSC;
			if (!strcmp(defrects[i].interface, "cvbs_pal"))
				dev_info[i].type = RK_CAMERA_DEVICE_CVBS_PAL;
		}
	}

	SENSOR_TR("%s(%d): channel %d, default %d\n", __func__, __LINE__,
		  sensor_device->channel_info.channel_total,
		  sensor_device->channel_info.default_id);
	spsensor->common_sensor.channel_id =
		sensor_device->channel_info.default_id;
	memcpy(&channel_infos, &sensor_device->channel_info,
	       sizeof(channel_infos));
	for (i = 0; i < 5; i++) {
		SENSOR_TR("%s(%d): channel_ain%d %s\n", __func__, __LINE__, i,
			  channel_infos.channel_info[i]);
	}

	/* init irq interrupt */
	/*SENSOR_TR("%s(%d): irq_active %d.\n",
		    __func__, __LINE__, new_camera->io.gpio_irq);
	if (gpio_request(new_camera->io.gpio_irq, NULL) != 0) {
		gpio_free(new_camera->io.gpio_irq);
		SENSOR_TR("%s(%d): request irq gpio error\n",
			  __func__, __LINE__);
	}

	spsensor->common_sensor.irq = gpio_to_irq(new_camera->io.gpio_irq);
	ret = request_irq(spsensor->common_sensor.irq, ADV7181_irq,
			  IRQF_TRIGGER_FALLING, dev_name(icd->pdev), spsensor);

	if (ret < 0) {
		SENSOR_TR("%s(%d): request irq failed\n", __func__, __LINE__);
		free_irq(spsensor->common_sensor.irq, spsensor);
	}
	*/

	/* init debugfs */
	debugfs_dir = debugfs_create_dir("adv7181", NULL);
	if (IS_ERR(debugfs_dir))
		SENSOR_TR("%s(%d): create debugfs dir failed\n",
			  __func__, __LINE__);
	else
		debugfs_create_file("register", S_IRUSR, debugfs_dir, spsensor,
				    &adv7181_debugfs_fops);

	/* init work_queue for state_check */
	INIT_DELAYED_WORK(&spsensor->common_sensor.state_check_work.work,
			  adv7181_check_state_work);
	spsensor->common_sensor.state_check_work.state_check_wq =
		create_singlethread_workqueue(SENSOR_NAME_STRING(_state_check_workqueue));
	if (spsensor->common_sensor.state_check_work.state_check_wq == NULL) {
		SENSOR_TR("%s(%d): %s create failed.\n", __func__, __LINE__,
			  SENSOR_NAME_STRING(_state_check_workqueue));
		BUG();
	}

	memcpy(&spsensor->common_sensor.info_priv.dev_sig_cnf,
	       &dev_info[0], sizeof(dev_info));
	spsensor->common_sensor.crop_percent = 0;
	spsensor->common_sensor.sensor_cb.sensor_s_stream_cb = sensor_s_stream;
}

/*
 * :::::WARNING:::::
 * It is not allowed to modify the following code
 */

sensor_init_parameters_default_code();

sensor_v4l2_struct_initialization();

sensor_probe_default_code();

sensor_remove_default_code();

sensor_driver_default_module_code();

