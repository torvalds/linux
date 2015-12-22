/*
 * Copyright (C) 2011-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/of_device.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/v4l2-mediabus.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>

#define OV5647_VOLTAGE_ANALOG               2800000
#define OV5647_VOLTAGE_DIGITAL_CORE         1500000
#define OV5647_VOLTAGE_DIGITAL_IO           1800000

#define MIN_FPS 15
#define MAX_FPS 30
#define DEFAULT_FPS 30

#define OV5647_XCLK_MIN 6000000
#define OV5647_XCLK_MAX 24000000

#define OV5647_CHIP_ID_HIGH_BYTE	0x300A
#define OV5647_CHIP_ID_LOW_BYTE		0x300B

enum ov5647_mode {
	ov5647_mode_MIN = 0,
	ov5647_mode_VGA_640_480 = 0,
	ov5647_mode_720P_1280_720 = 1,
	ov5647_mode_1080P_1920_1080 = 2,
	ov5647_mode_QSXGA_2592_1944 = 3,
	ov5647_mode_MAX = 3,
	ov5647_mode_INIT = 0xff, /*only for sensor init*/
};

enum ov5647_frame_rate {
	ov5647_15_fps,
	ov5647_30_fps
};

static int ov5647_framerates[] = {
	[ov5647_15_fps] = 15,
	[ov5647_30_fps] = 30,
};

struct ov5647_datafmt {
	u32	code;
	enum v4l2_colorspace		colorspace;
};

/* image size under 1280 * 960 are SUBSAMPLING
 * image size upper 1280 * 960 are SCALING
 */
enum ov5647_downsize_mode {
	SUBSAMPLING,
	SCALING,
};

struct reg_value {
	u16 u16RegAddr;
	u8 u8Val;
	u8 u8Mask;
	u32 u32Delay_ms;
};

struct ov5647_mode_info {
	enum ov5647_mode mode;
	enum ov5647_downsize_mode dn_mode;
	u32 width;
	u32 height;
	struct reg_value *init_data_ptr;
	u32 init_data_size;
};

struct otp_struct {
	int customer_id;
	int module_integrator_id;
	int lens_id;
	int rg_ratio;
	int bg_ratio;
	int user_data[3];
	int light_rg;
	int light_bg;
};

struct ov5647 {
	struct v4l2_subdev		subdev;
	struct i2c_client *i2c_client;
	struct v4l2_pix_format pix;
	const struct ov5647_datafmt	*fmt;
	struct v4l2_captureparm streamcap;
	bool on;

	/* control settings */
	int brightness;
	int hue;
	int contrast;
	int saturation;
	int red;
	int green;
	int blue;
	int ae_mode;

	u32 mclk;
	u8 mclk_source;
	struct clk *sensor_clk;
	int csi;

	void (*io_init)(void);
};
/*!
 * Maintains the information on the current state of the sesor.
 */
static struct ov5647 ov5647_data;
static int pwn_gpio, rst_gpio;
static int prev_sysclk, prev_HTS;
static int AE_low, AE_high, AE_Target = 52;

/* R/G and B/G of typical camera module is defined here,
 * the typical camera module is selected by CameraAnalyzer. */
static int RG_Ratio_Typical = 0x70;
static int BG_Ratio_Typical = 0x70;


static struct reg_value ov5647_init_setting[] = {

	{0x0100, 0x00, 0, 0},                       {0x3035, 0x11, 0, 0},
	{0x3036, 0x69, 0, 0}, {0x303c, 0x11, 0, 0}, {0x3821, 0x07, 0, 0},
	{0x3820, 0x41, 0, 0}, {0x370c, 0x0f, 0, 0}, {0x3612, 0x59, 0, 0},
	{0x3618, 0x00, 0, 0}, {0x5000, 0x06, 0, 0}, {0x5002, 0x40, 0, 0},
	{0x5003, 0x08, 0, 0}, {0x5a00, 0x08, 0, 0}, {0x3000, 0xff, 0, 0},
	{0x3001, 0xff, 0, 0}, {0x3002, 0xff, 0, 0}, {0x301d, 0xf0, 0, 0},
	{0x3a18, 0x00, 0, 0}, {0x3a19, 0xf8, 0, 0}, {0x3c01, 0x80, 0, 0},
	{0x3b07, 0x0c, 0, 0}, {0x380c, 0x07, 0, 0}, {0x380d, 0x68, 0, 0},
	{0x380e, 0x03, 0, 0}, {0x380f, 0xd8, 0, 0}, {0x3814, 0x31, 0, 0},
	{0x3815, 0x31, 0, 0}, {0x3708, 0x64, 0, 0}, {0x3709, 0x52, 0, 0},
	{0x3808, 0x05, 0, 0}, {0x3809, 0x00, 0, 0}, {0x380a, 0x03, 0, 0},
	{0x380b, 0xc0, 0, 0}, {0x3800, 0x00, 0, 0}, {0x3801, 0x18, 0, 0},
	{0x3802, 0x00, 0, 0}, {0x3803, 0x0e, 0, 0}, {0x3804, 0x0a, 0, 0},
	{0x3805, 0x27, 0, 0}, {0x3806, 0x07, 0, 0}, {0x3807, 0x95, 0, 0},
	{0x3630, 0x2e, 0, 0}, {0x3632, 0xe2, 0, 0}, {0x3633, 0x23, 0, 0},
	{0x3634, 0x44, 0, 0}, {0x3620, 0x64, 0, 0}, {0x3621, 0xe0, 0, 0},
	{0x3600, 0x37, 0, 0}, {0x3704, 0xa0, 0, 0}, {0x3703, 0x5a, 0, 0},
	{0x3715, 0x78, 0, 0}, {0x3717, 0x01, 0, 0}, {0x3731, 0x02, 0, 0},
	{0x370b, 0x60, 0, 0}, {0x3705, 0x1a, 0, 0}, {0x3f05, 0x02, 0, 0},
	{0x3f06, 0x10, 0, 0}, {0x3f01, 0x0a, 0, 0}, {0x3a08, 0x01, 0, 0},
	{0x3a09, 0x27, 0, 0}, {0x3a0a, 0x00, 0, 0}, {0x3a0b, 0xf6, 0, 0},
	{0x3a0d, 0x04, 0, 0}, {0x3a0e, 0x03, 0, 0}, {0x3a0f, 0x58, 0, 0},
	{0x3a10, 0x50, 0, 0}, {0x3a1b, 0x58, 0, 0}, {0x3a1e, 0x50, 0, 0},
	{0x3a11, 0x60, 0, 0}, {0x3a1f, 0x28, 0, 0}, {0x4001, 0x02, 0, 0},
	{0x4004, 0x02, 0, 0}, {0x4000, 0x09, 0, 0}, {0x4050, 0x6e, 0, 0},
	{0x4051, 0x8f, 0, 0}, {0x4837, 0x17, 0, 0}, {0x3503, 0x03, 0, 0},
	{0x3501, 0x44, 0, 0}, {0x3502, 0x80, 0, 0}, {0x350a, 0x00, 0, 0},
	{0x350b, 0x7f, 0, 0}, {0x5001, 0x01, 0, 0}, {0x5002, 0x41, 0, 0},
	{0x5180, 0x08, 0, 0}, {0x5186, 0x04, 0, 0}, {0x5187, 0x00, 0, 0},
	{0x5188, 0x04, 0, 0}, {0x5189, 0x00, 0, 0}, {0x518a, 0x04, 0, 0},
	{0x518b, 0x00, 0, 0}, {0x5000, 0x86, 0, 0}, {0x5800, 0x11, 0, 0},
	{0x5801, 0x0a, 0, 0}, {0x5802, 0x09, 0, 0}, {0x5803, 0x09, 0, 0},
	{0x5804, 0x0a, 0, 0}, {0x5805, 0x0f, 0, 0}, {0x5806, 0x07, 0, 0},
	{0x5807, 0x05, 0, 0}, {0x5808, 0x03, 0, 0}, {0x5809, 0x03, 0, 0},
	{0x580a, 0x05, 0, 0}, {0x580b, 0x07, 0, 0}, {0x580c, 0x05, 0, 0},
	{0x580d, 0x02, 0, 0}, {0x580e, 0x00, 0, 0}, {0x580f, 0x00, 0, 0},
	{0x5810, 0x02, 0, 0}, {0x5811, 0x05, 0, 0}, {0x5812, 0x05, 0, 0},
	{0x5813, 0x02, 0, 0}, {0x5814, 0x00, 0, 0}, {0x5815, 0x00, 0, 0},
	{0x5816, 0x01, 0, 0}, {0x5817, 0x05, 0, 0}, {0x5818, 0x08, 0, 0},
	{0x5819, 0x05, 0, 0}, {0x581a, 0x03, 0, 0}, {0x581b, 0x03, 0, 0},
	{0x581c, 0x04, 0, 0}, {0x581d, 0x07, 0, 0}, {0x581e, 0x10, 0, 0},
	{0x581f, 0x0b, 0, 0}, {0x5820, 0x09, 0, 0}, {0x5821, 0x09, 0, 0},
	{0x5822, 0x09, 0, 0}, {0x5823, 0x0e, 0, 0}, {0x5824, 0x28, 0, 0},
	{0x5825, 0x1a, 0, 0}, {0x5826, 0x1a, 0, 0}, {0x5827, 0x1a, 0, 0},
	{0x5828, 0x46, 0, 0}, {0x5829, 0x2a, 0, 0}, {0x582a, 0x26, 0, 0},
	{0x582b, 0x44, 0, 0}, {0x582c, 0x26, 0, 0}, {0x582d, 0x2a, 0, 0},
	{0x582e, 0x28, 0, 0}, {0x582f, 0x42, 0, 0}, {0x5830, 0x40, 0, 0},
	{0x5831, 0x42, 0, 0}, {0x5832, 0x28, 0, 0}, {0x5833, 0x0a, 0, 0},
	{0x5834, 0x16, 0, 0}, {0x5835, 0x44, 0, 0}, {0x5836, 0x26, 0, 0},
	{0x5837, 0x2a, 0, 0}, {0x5838, 0x28, 0, 0}, {0x5839, 0x0a, 0, 0},
	{0x583a, 0x0a, 0, 0}, {0x583b, 0x0a, 0, 0}, {0x583c, 0x26, 0, 0},
	{0x583d, 0xbe, 0, 0}, {0x0100, 0x01, 0, 0}, {0x3000, 0x00, 0, 0},
	{0x3001, 0x00, 0, 0}, {0x3002, 0x00, 0, 0}, {0x3017, 0xe0, 0, 0},
	{0x301c, 0xfc, 0, 0}, {0x3636, 0x06, 0, 0}, {0x3016, 0x08, 0, 0},
	{0x3827, 0xec, 0, 0}, {0x3018, 0x44, 0, 0}, {0x3035, 0x21, 0, 0},
	{0x3106, 0xf5, 0, 0}, {0x3034, 0x18, 0, 0}, {0x301c, 0xf8, 0, 0},
};

static struct reg_value ov5647_setting_60fps_VGA_640_480[] = {
	{0x0100, 0x00, 0, 0},                        {0x3035, 0x11, 0, 0},
	{0x3036, 0x46, 0, 0}, {0x303c, 0x11, 0, 0}, {0x3821, 0x07, 0, 0},
	{0x3820, 0x41, 0, 0}, {0x370c, 0x0f, 0, 0}, {0x3612, 0x59, 0, 0},
	{0x3618, 0x00, 0, 0}, {0x5000, 0x06, 0, 0}, {0x5002, 0x40, 0, 0},
	{0x5003, 0x08, 0, 0}, {0x5a00, 0x08, 0, 0}, {0x3000, 0xff, 0, 0},
	{0x3001, 0xff, 0, 0}, {0x3002, 0xff, 0, 0}, {0x301d, 0xf0, 0, 0},
	{0x3a18, 0x00, 0, 0}, {0x3a19, 0xf8, 0, 0}, {0x3c01, 0x80, 0, 0},
	{0x3b07, 0x0c, 0, 0}, {0x380c, 0x07, 0, 0}, {0x380d, 0x3c, 0, 0},
	{0x380e, 0x01, 0, 0}, {0x380f, 0xf8, 0, 0}, {0x3814, 0x71, 0, 0},
	{0x3815, 0x71, 0, 0}, {0x3708, 0x64, 0, 0}, {0x3709, 0x52, 0, 0},
	{0x3808, 0x02, 0, 0}, {0x3809, 0x80, 0, 0}, {0x380a, 0x01, 0, 0},
	{0x380b, 0xe0, 0, 0}, {0x3800, 0x00, 0, 0}, {0x3801, 0x10, 0, 0},
	{0x3802, 0x00, 0, 0}, {0x3803, 0x00, 0, 0}, {0x3804, 0x0a, 0, 0},
	{0x3805, 0x2f, 0, 0}, {0x3806, 0x07, 0, 0}, {0x3807, 0x9f, 0, 0},
	{0x3630, 0x2e, 0, 0}, {0x3632, 0xe2, 0, 0}, {0x3633, 0x23, 0, 0},
	{0x3634, 0x44, 0, 0}, {0x3620, 0x64, 0, 0}, {0x3621, 0xe0, 0, 0},
	{0x3600, 0x37, 0, 0}, {0x3704, 0xa0, 0, 0}, {0x3703, 0x5a, 0, 0},
	{0x3715, 0x78, 0, 0}, {0x3717, 0x01, 0, 0}, {0x3731, 0x02, 0, 0},
	{0x370b, 0x60, 0, 0}, {0x3705, 0x1a, 0, 0}, {0x3f05, 0x02, 0, 0},
	{0x3f06, 0x10, 0, 0}, {0x3f01, 0x0a, 0, 0}, {0x3a08, 0x01, 0, 0},
	{0x3a09, 0x2e, 0, 0}, {0x3a0a, 0x00, 0, 0}, {0x3a0b, 0xfb, 0, 0},
	{0x3a0d, 0x02, 0, 0}, {0x3a0e, 0x01, 0, 0}, {0x3a0f, 0x58, 0, 0},
	{0x3a10, 0x50, 0, 0}, {0x3a1b, 0x58, 0, 0}, {0x3a1e, 0x50, 0, 0},
	{0x3a11, 0x60, 0, 0}, {0x3a1f, 0x28, 0, 0}, {0x4001, 0x02, 0, 0},
	{0x4004, 0x02, 0, 0}, {0x4000, 0x09, 0, 0}, {0x4050, 0x6e, 0, 0},
	{0x4051, 0x8f, 0, 0}, {0x0100, 0x01, 0, 0}, {0x3000, 0x00, 0, 0},
	{0x3001, 0x00, 0, 0}, {0x3002, 0x00, 0, 0}, {0x3017, 0xe0, 0, 0},
	{0x301c, 0xfc, 0, 0}, {0x3636, 0x06, 0, 0}, {0x3016, 0x08, 0, 0},
	{0x3827, 0xec, 0, 0}, {0x3018, 0x44, 0, 0}, {0x3035, 0x21, 0, 0},
	{0x3106, 0xf5, 0, 0}, {0x3034, 0x18, 0, 0}, {0x301c, 0xf8, 0, 0},
};

static struct reg_value ov5647_setting_30fps_720P_1280_720[] = {
	{0x0100, 0x00, 0, 0}, {0x3820, 0x41, 0, 0}, {0x3821, 0x07, 0, 0},
	{0x3612, 0x59, 0, 0}, {0x3618, 0x00, 0, 0}, {0x380c, 0x09, 0, 0},
	{0x380d, 0xe8, 0, 0}, {0x380e, 0x04, 0, 0}, {0x380f, 0x50, 0, 0},
	{0x3814, 0x31, 0, 0}, {0x3815, 0x31, 0, 0}, {0x3709, 0x52, 0, 0},
	{0x3808, 0x05, 0, 0}, {0x3809, 0x00, 0, 0}, {0x380a, 0x02, 0, 0},
	{0x380b, 0xd0, 0, 0}, {0x3801, 0x18, 0, 0}, {0x3802, 0x00, 0, 0},
	{0x3803, 0xf8, 0, 0}, {0x3804, 0x0a, 0, 0}, {0x3805, 0x27, 0, 0},
	{0x3806, 0x06, 0, 0}, {0x3807, 0xa7, 0, 0}, {0x3a09, 0xbe, 0, 0},
	{0x3a0a, 0x01, 0, 0}, {0x3a0b, 0x74, 0, 0}, {0x3a0d, 0x02, 0, 0},
	{0x3a0e, 0x01, 0, 0}, {0x4004, 0x02, 0, 0}, {0x4005, 0x18, 0, 0},
	{0x0100, 0x01, 0, 0},
};

static struct reg_value ov5647_setting_30fps_1080P_1920_1080[] = {
	{0x0100, 0x00, 0, 0}, {0x3820, 0x00, 0, 0}, {0x3821, 0x06, 0, 0},
	{0x3612, 0x5b, 0, 0}, {0x3618, 0x04, 0, 0}, {0x380c, 0x09, 0, 0},
	{0x380d, 0xe8, 0, 0}, {0x380e, 0x04, 0, 0}, {0x380f, 0x50, 0, 0},
	{0x3814, 0x11, 0, 0}, {0x3815, 0x11, 0, 0}, {0x3709, 0x12, 0, 0},
	{0x3808, 0x07, 0, 0}, {0x3809, 0x80, 0, 0}, {0x380a, 0x04, 0, 0},
	{0x380b, 0x38, 0, 0}, {0x3801, 0x5c, 0, 0}, {0x3802, 0x01, 0, 0},
	{0x3803, 0xb2, 0, 0}, {0x3804, 0x08, 0, 0}, {0x3805, 0xe3, 0, 0},
	{0x3806, 0x05, 0, 0}, {0x3807, 0xf1, 0, 0}, {0x3a09, 0x4b, 0, 0},
	{0x3a0a, 0x01, 0, 0}, {0x3a0b, 0x13, 0, 0}, {0x3a0d, 0x04, 0, 0},
	{0x3a0e, 0x03, 0, 0}, {0x4004, 0x04, 0, 0}, {0x4005, 0x18, 0, 0},
	{0x0100, 0x01, 0, 0},
};

static struct reg_value ov5647_setting_15fps_QSXGA_2592_1944[] = {
	{0x0100, 0x00, 0, 0}, {0x3820, 0x00, 0, 0}, {0x3821, 0x06, 0, 0},
	{0x3612, 0x5b, 0, 0}, {0x3618, 0x04, 0, 0}, {0x380c, 0x0b, 0, 0},
	{0x380d, 0x10, 0, 0}, {0x380e, 0x07, 0, 0}, {0x380f, 0xb8, 0, 0},
	{0x3814, 0x11, 0, 0}, {0x3815, 0x11, 0, 0}, {0x3709, 0x12, 0, 0},
	{0x3808, 0x0a, 0, 0}, {0x3809, 0x20, 0, 0}, {0x380a, 0x07, 0, 0},
	{0x380b, 0x98, 0, 0}, {0x3801, 0x0c, 0, 0}, {0x3802, 0x00, 0, 0},
	{0x3803, 0x04, 0, 0}, {0x3804, 0x0a, 0, 0}, {0x3805, 0x33, 0, 0},
	{0x3806, 0x07, 0, 0}, {0x3807, 0xa3, 0, 0}, {0x3a09, 0x28, 0, 0},
	{0x3a0a, 0x00, 0, 0}, {0x3a0b, 0xf6, 0, 0}, {0x3a0d, 0x08, 0, 0},
	{0x3a0e, 0x06, 0, 0}, {0x4004, 0x04, 0, 0}, {0x4005, 0x1a, 0, 0},
	{0x0100, 0x01, 0, 0},
};

static struct ov5647_mode_info ov5647_mode_info_data[2][ov5647_mode_MAX + 1] = {
	{
		{ov5647_mode_VGA_640_480, -1, 0, 0, NULL, 0},
		{ov5647_mode_720P_1280_720, -1, 0, 0, NULL, 0},
		{ov5647_mode_1080P_1920_1080, -1, 0, 0, NULL, 0},
		{ov5647_mode_QSXGA_2592_1944, SCALING, 2592, 1944,
		ov5647_setting_15fps_QSXGA_2592_1944,
		ARRAY_SIZE(ov5647_setting_15fps_QSXGA_2592_1944)},
	},
	{
		/* Actually VGA working in 60fps mode */
		{ov5647_mode_VGA_640_480, SUBSAMPLING, 640,  480,
		ov5647_setting_60fps_VGA_640_480,
		ARRAY_SIZE(ov5647_setting_60fps_VGA_640_480)},
		{ov5647_mode_720P_1280_720, SUBSAMPLING, 1280, 720,
		ov5647_setting_30fps_720P_1280_720,
		ARRAY_SIZE(ov5647_setting_30fps_720P_1280_720)},
		{ov5647_mode_1080P_1920_1080, SCALING, 1920, 1080,
		ov5647_setting_30fps_1080P_1920_1080,
		ARRAY_SIZE(ov5647_setting_30fps_1080P_1920_1080)},
		{ov5647_mode_QSXGA_2592_1944, -1, 0, 0, NULL, 0},
	},
};

static struct regulator *io_regulator;
static struct regulator *core_regulator;
static struct regulator *analog_regulator;
static struct regulator *gpo_regulator;

static int ov5647_probe(struct i2c_client *adapter,
				const struct i2c_device_id *device_id);
static int ov5647_remove(struct i2c_client *client);

static s32 ov5647_read_reg(u16 reg, u8 *val);
static s32 ov5647_write_reg(u16 reg, u8 val);

static const struct i2c_device_id ov5647_id[] = {
	{"ov5647_mipi", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, ov5647_id);

static struct i2c_driver ov5647_i2c_driver = {
	.driver = {
		  .owner = THIS_MODULE,
		  .name  = "ov5647_mipi",
		  },
	.probe  = ov5647_probe,
	.remove = ov5647_remove,
	.id_table = ov5647_id,
};

static const struct ov5647_datafmt ov5647_colour_fmts[] = {
	{MEDIA_BUS_FMT_SBGGR8_1X8, V4L2_COLORSPACE_JPEG},
};

static struct ov5647 *to_ov5647(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct ov5647, subdev);
}

/* Find a data format by a pixel code in an array */
static const struct ov5647_datafmt
			*ov5647_find_datafmt(u32 code)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ov5647_colour_fmts); i++)
		if (ov5647_colour_fmts[i].code == code)
			return ov5647_colour_fmts + i;

	return NULL;
}

static inline void ov5647_power_down(int enable)
{
	if (pwn_gpio < 0)
		return;

	/* 19x19 pwdn pin invert by mipi daughter card */
	if (!enable)
		gpio_set_value_cansleep(pwn_gpio, 1);
	else
		gpio_set_value_cansleep(pwn_gpio, 0);

	msleep(2);
}

static void ov5647_reset(void)
{
	if (rst_gpio < 0 || pwn_gpio < 0)
		return;

	/* camera reset */
	gpio_set_value_cansleep(rst_gpio, 1);

	/* camera power dowmn */
	gpio_set_value_cansleep(pwn_gpio, 1);
	msleep(5);

	gpio_set_value_cansleep(pwn_gpio, 0);
	msleep(5);

	gpio_set_value_cansleep(rst_gpio, 0);
	msleep(1);

	gpio_set_value_cansleep(rst_gpio, 1);
	msleep(5);

	gpio_set_value_cansleep(pwn_gpio, 1);
}

static int ov5647_regulator_enable(struct device *dev)
{
	int ret = 0;

	io_regulator = devm_regulator_get(dev, "DOVDD");
	if (!IS_ERR(io_regulator)) {
		regulator_set_voltage(io_regulator,
				      OV5647_VOLTAGE_DIGITAL_IO,
				      OV5647_VOLTAGE_DIGITAL_IO);
		ret = regulator_enable(io_regulator);
		if (ret) {
			pr_err("%s:io set voltage error\n", __func__);
			return ret;
		} else {
			dev_dbg(dev,
				"%s:io set voltage ok\n", __func__);
		}
	} else {
		pr_err("%s: cannot get io voltage error\n", __func__);
		io_regulator = NULL;
	}

	core_regulator = devm_regulator_get(dev, "DVDD");
	if (!IS_ERR(core_regulator)) {
		regulator_set_voltage(core_regulator,
				      OV5647_VOLTAGE_DIGITAL_CORE,
				      OV5647_VOLTAGE_DIGITAL_CORE);
		ret = regulator_enable(core_regulator);
		if (ret) {
			pr_err("%s:core set voltage error\n", __func__);
			return ret;
		} else {
			dev_dbg(dev,
				"%s:core set voltage ok\n", __func__);
		}
	} else {
		core_regulator = NULL;
		pr_err("%s: cannot get core voltage error\n", __func__);
	}

	analog_regulator = devm_regulator_get(dev, "AVDD");
	if (!IS_ERR(analog_regulator)) {
		regulator_set_voltage(analog_regulator,
				      OV5647_VOLTAGE_ANALOG,
				      OV5647_VOLTAGE_ANALOG);
		ret = regulator_enable(analog_regulator);
		if (ret) {
			pr_err("%s:analog set voltage error\n",
				__func__);
			return ret;
		} else {
			dev_dbg(dev,
				"%s:analog set voltage ok\n", __func__);
		}
	} else {
		analog_regulator = NULL;
		pr_err("%s: cannot get analog voltage error\n", __func__);
	}

	return ret;
}

static s32 ov5647_write_reg(u16 reg, u8 val)
{
	u8 au8Buf[3] = {0};

	au8Buf[0] = reg >> 8;
	au8Buf[1] = reg & 0xff;
	au8Buf[2] = val;

	if (i2c_master_send(ov5647_data.i2c_client, au8Buf, 3) < 0) {
		pr_err("%s:write reg error:reg=%x,val=%x\n",
			__func__, reg, val);
		return -1;
	}

	return 0;
}

static s32 ov5647_read_reg(u16 reg, u8 *val)
{
	u8 au8RegBuf[2] = {0};
	u8 u8RdVal = 0;

	au8RegBuf[0] = reg >> 8;
	au8RegBuf[1] = reg & 0xff;

	if (2 != i2c_master_send(ov5647_data.i2c_client, au8RegBuf, 2)) {
		pr_err("%s:write reg error:reg=%x\n",
				__func__, reg);
		return -1;
	}

	if (1 != i2c_master_recv(ov5647_data.i2c_client, &u8RdVal, 1)) {
		pr_err("%s:read reg error:reg=%x,val=%x\n",
				__func__, reg, u8RdVal);
		return -1;
	}

	*val = u8RdVal;

	return u8RdVal;
}

/* index: index of otp group. (0, 1, 2)
 * return:
 * 0, group index is empty
 * 1, group index has invalid data
 * 2, group index has valid data */
static int ov5647_check_otp(int index)
{
	int i;
	int address;
	u8 temp;

	/* read otp into buffer */
	ov5647_write_reg(0x3d21, 0x01);
	msleep(20);
	address = 0x3d05 + index * 9;
	temp = ov5647_read_reg(address, &temp);

	/* disable otp read */
	ov5647_write_reg(0x3d21, 0x00);

	/* clear otp buffer */
	for (i = 0; i < 32; i++)
		ov5647_write_reg(0x3d00 + i, 0x00);

	if (!temp)
		return 0;
	else if ((!(temp & 0x80)) && (temp & 0x7f))
		return 2;
	else
		return 1;
}

/* index: index of otp group. (0, 1, 2)
 * return: 0 */
static int ov5647_read_otp(int index, struct otp_struct *otp_ptr)
{
	int i;
	int address;
	u8 temp;

	/* read otp into buffer */
	ov5647_write_reg(0x3d21, 0x01);
	msleep(2);
	address = 0x3d05 + index * 9;
	temp = ov5647_read_reg(address, &temp);
	(*otp_ptr).customer_id = temp & 0x7f;

	(*otp_ptr).module_integrator_id = ov5647_read_reg(address, &temp);
	(*otp_ptr).lens_id = ov5647_read_reg(address + 1, &temp);
	(*otp_ptr).rg_ratio = ov5647_read_reg(address + 2, &temp);
	(*otp_ptr).bg_ratio = ov5647_read_reg(address + 3, &temp);
	(*otp_ptr).user_data[0] = ov5647_read_reg(address + 4, &temp);
	(*otp_ptr).user_data[1] = ov5647_read_reg(address + 5, &temp);
	(*otp_ptr).user_data[2] = ov5647_read_reg(address + 6, &temp);
	(*otp_ptr).light_rg = ov5647_read_reg(address + 7, &temp);
	(*otp_ptr).light_bg = ov5647_read_reg(address + 8, &temp);

	/* disable otp read */
	ov5647_write_reg(0x3d21, 0x00);

	/* clear otp buffer */
	for (i = 0; i < 32; i++)
		ov5647_write_reg(0x3d00 + i, 0x00);

	return 0;
}

/* R_gain, sensor red gain of AWB, 0x400 =1
 * G_gain, sensor green gain of AWB, 0x400 =1
 * B_gain, sensor blue gain of AWB, 0x400 =1
 * return 0 */
static int ov5647_update_awb_gain(int R_gain, int G_gain, int B_gain)
{
	if (R_gain > 0x400) {
		ov5647_write_reg(0x5186, R_gain >> 8);
		ov5647_write_reg(0x5187, R_gain & 0x00ff);
	}
	if (G_gain > 0x400) {
		ov5647_write_reg(0x5188, G_gain >> 8);
		ov5647_write_reg(0x5189, G_gain & 0x00ff);
	}
	if (B_gain > 0x400) {
		ov5647_write_reg(0x518a, B_gain >> 8);
		ov5647_write_reg(0x518b, B_gain & 0x00ff);
	}

	return 0;
}

/* call this function after OV5647 initialization
 * return value:
 * 0 update success
 * 1 no OTP */
static int ov5647_update_otp(void)
{
	struct otp_struct current_otp;
	int i;
	int otp_index;
	int temp;
	int R_gain, G_gain, B_gain, G_gain_R, G_gain_B;
	int rg, bg;

	/* R/G and B/G of current camera module is read out from sensor OTP
	 * check first OTP with valid data */
	for (i = 0; i < 3; i++) {
		temp = ov5647_check_otp(i);
		if (temp == 2) {
			otp_index = i;
			break;
		}
	}
	if (i == 3) {
		/* no valid wb OTP data */
		printk(KERN_WARNING "No valid wb otp data\n");
		return 1;
	}

	ov5647_read_otp(otp_index, &current_otp);
	if (current_otp.light_rg == 0)
		rg = current_otp.rg_ratio;
	else
		rg = current_otp.rg_ratio * (current_otp.light_rg + 128) / 256;

	if (current_otp.light_bg == 0)
		bg = current_otp.bg_ratio;
	else
		bg = current_otp.bg_ratio * (current_otp.light_bg + 128) / 256;

	/* calculate G gain
	 * 0x400 = 1x gain */
	if (bg < BG_Ratio_Typical) {
		if (rg < RG_Ratio_Typical) {
			/* current_otp.bg_ratio < BG_Ratio_typical &&
			 * current_otp.rg_ratio < RG_Ratio_typical */
			G_gain = 0x400;
			B_gain = 0x400 * BG_Ratio_Typical / bg;
			R_gain = 0x400 * RG_Ratio_Typical / rg;
		} else {
			/* current_otp.bg_ratio < BG_Ratio_typical &&
			 * current_otp.rg_ratio >= RG_Ratio_typical */
			R_gain = 0x400;
			G_gain = 0x400 * rg / RG_Ratio_Typical;
			B_gain = G_gain * BG_Ratio_Typical / bg;
		}
	} else {
		if (rg < RG_Ratio_Typical) {
			/* current_otp.bg_ratio >= BG_Ratio_typical &&
			 * current_otp.rg_ratio < RG_Ratio_typical */
			B_gain = 0x400;
			G_gain = 0x400 * bg / BG_Ratio_Typical;
			R_gain = G_gain * RG_Ratio_Typical / rg;
		} else {
			/* current_otp.bg_ratio >= BG_Ratio_typical &&
			 * current_otp.rg_ratio >= RG_Ratio_typical */
			G_gain_B = 0x400 * bg / BG_Ratio_Typical;
			G_gain_R = 0x400 * rg / RG_Ratio_Typical;
			if (G_gain_B > G_gain_R) {
				B_gain = 0x400;
				G_gain = G_gain_B;
				R_gain = G_gain * RG_Ratio_Typical / rg;
			} else {
				R_gain = 0x400;
				G_gain = G_gain_R;
				B_gain = G_gain * BG_Ratio_Typical / bg;
			}
		}
	}
	ov5647_update_awb_gain(R_gain, G_gain, B_gain);
	return 0;
}

static void ov5647_stream_on(void)
{
	ov5647_write_reg(0x4202, 0x00);
}

static void ov5647_stream_off(void)
{
	ov5647_write_reg(0x4202, 0x0f);
	/* both clock and data lane in LP00 */
	ov5647_write_reg(0x0100, 0x00);
}

static int ov5647_get_sysclk(void)
{
	/* calculate sysclk */
	int xvclk = ov5647_data.mclk / 10000;
	int sysclk, temp1, temp2;
	int pre_div02x, div_cnt7b, sdiv0, pll_rdiv, bit_div2x, sclk_div, VCO;
	int pre_div02x_map[] = {2, 2, 4, 6, 8, 3, 12, 5, 16, 2, 2, 2, 2, 2, 2, 2};
	int sdiv0_map[] = {16, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
	int pll_rdiv_map[] = {1, 2};
	int bit_div2x_map[] = {2, 2, 2, 2, 2, 2, 2, 2, 4, 2, 5, 2, 2, 2, 2, 2};
	int sclk_div_map[] = {1, 2, 4, 1};
	u8 temp;

	temp1 = ov5647_read_reg(0x3037, &temp);
	temp2 = temp1 & 0x0f;
	pre_div02x = pre_div02x_map[temp2];
	temp2 = (temp1 >> 4) & 0x01;
	pll_rdiv = pll_rdiv_map[temp2];
	temp1 = ov5647_read_reg(0x3036, &temp);

	div_cnt7b = temp1;

	VCO = xvclk * 2 / pre_div02x * div_cnt7b;
	temp1 = ov5647_read_reg(0x3035, &temp);
	temp2 = temp1 >> 4;
	sdiv0 = sdiv0_map[temp2];
	temp1 = ov5647_read_reg(0x3034, &temp);
	temp2 = temp1 & 0x0f;
	bit_div2x = bit_div2x_map[temp2];
	temp1 = ov5647_read_reg(0x3106, &temp);
	temp2 = (temp1 >> 2) & 0x03;
	sclk_div = sclk_div_map[temp2];
	sysclk = VCO * 2 / sdiv0 / pll_rdiv / bit_div2x / sclk_div;
	return sysclk;
}

static void ov5647_set_night_mode(void)
{
	 /* read HTS from register settings */
	u8 mode;

	ov5647_read_reg(0x3a00, &mode);
	mode &= 0xfb;
	ov5647_write_reg(0x3a00, mode);
}

static int ov5647_get_HTS(void)
{
	 /* read HTS from register settings */
	int HTS;
	u8 temp;

	HTS = ov5647_read_reg(0x380c, &temp);
	HTS = (HTS << 8) + ov5647_read_reg(0x380d, &temp);

	return HTS;
}

static int ov5647_soft_reset(void)
{
	/* soft reset ov5647 */

	ov5647_write_reg(0x0103, 0x1);
	msleep(5);

	return 0;
}

static int ov5647_get_VTS(void)
{
	 /* read VTS from register settings */
	int VTS;
	u8 temp;

	/* total vertical size[15:8] high byte */
	VTS = ov5647_read_reg(0x380e, &temp);

	VTS = (VTS << 8) + ov5647_read_reg(0x380f, &temp);

	return VTS;
}

static int ov5647_set_VTS(int VTS)
{
	 /* write VTS to registers */
	 int temp;

	 temp = VTS & 0xff;
	 ov5647_write_reg(0x380f, temp);

	 temp = VTS >> 8;
	 ov5647_write_reg(0x380e, temp);

	 return 0;
}

static int ov5647_get_shutter(void)
{
	 /* read shutter, in number of line period */
	int shutter;
	u8 temp;

	shutter = (ov5647_read_reg(0x03500, &temp) & 0x0f);
	shutter = (shutter << 8) + ov5647_read_reg(0x3501, &temp);
	shutter = (shutter << 4) + (ov5647_read_reg(0x3502, &temp)>>4);

	 return shutter;
}

static int ov5647_set_shutter(int shutter)
{
	 /* write shutter, in number of line period */
	 int temp;

	 shutter = shutter & 0xffff;

	 temp = shutter & 0x0f;
	 temp = temp << 4;
	 ov5647_write_reg(0x3502, temp);

	 temp = shutter & 0xfff;
	 temp = temp >> 4;
	 ov5647_write_reg(0x3501, temp);

	 temp = shutter >> 12;
	 ov5647_write_reg(0x3500, temp);

	 return 0;
}

static int ov5647_get_gain16(void)
{
	 /* read gain, 16 = 1x */
	int gain16;
	u8 temp;

	gain16 = ov5647_read_reg(0x350a, &temp) & 0x03;
	gain16 = (gain16 << 8) + ov5647_read_reg(0x350b, &temp);

	return gain16;
}

static int ov5647_set_gain16(int gain16)
{
	/* write gain, 16 = 1x */
	u8 temp;
	gain16 = gain16 & 0x3ff;

	temp = gain16 & 0xff;
	ov5647_write_reg(0x350b, temp);

	temp = gain16 >> 8;
	ov5647_write_reg(0x350a, temp);

	return 0;
}

static int ov5647_get_light_freq(void)
{
	/* get banding filter value */
	int temp, temp1, light_freq = 0;
	u8 tmp;

	temp = ov5647_read_reg(0x3c01, &tmp);

	if (temp & 0x80) {
		/* manual */
		temp1 = ov5647_read_reg(0x3c00, &tmp);
		if (temp1 & 0x04) {
			/* 50Hz */
			light_freq = 50;
		} else {
			/* 60Hz */
			light_freq = 60;
		}
	} else {
		/* auto */
		temp1 = ov5647_read_reg(0x3c0c, &tmp);
		if (temp1 & 0x01) {
			/* 50Hz */
			light_freq = 50;
		} else {
			/* 60Hz */
		}
	}
	return light_freq;
}

static void ov5647_set_bandingfilter(void)
{
	int prev_VTS;
	int band_step60, max_band60, band_step50, max_band50;

	/* read preview PCLK */
	prev_sysclk = ov5647_get_sysclk();
	/* read preview HTS */
	prev_HTS = ov5647_get_HTS();

	/* read preview VTS */
	prev_VTS = ov5647_get_VTS();

	/* calculate banding filter */
	/* 60Hz */
	band_step60 = prev_sysclk * 100/prev_HTS * 100/120;
	ov5647_write_reg(0x3a0a, (band_step60 >> 8));
	ov5647_write_reg(0x3a0b, (band_step60 & 0xff));

	max_band60 = (int)((prev_VTS-4)/band_step60);
	ov5647_write_reg(0x3a0d, max_band60);

	/* 50Hz */
	band_step50 = prev_sysclk * 100/prev_HTS;
	ov5647_write_reg(0x3a08, (band_step50 >> 8));
	ov5647_write_reg(0x3a09, (band_step50 & 0xff));

	max_band50 = (int)((prev_VTS-4)/band_step50);
	ov5647_write_reg(0x3a0e, max_band50);
}

static int ov5647_set_AE_target(int target)
{
	/* stable in high */
	int fast_high, fast_low;
	AE_low = target * 23 / 25;	/* 0.92 */
	AE_high = target * 27 / 25;	/* 1.08 */

	fast_high = AE_high<<1;
	if (fast_high > 255)
		fast_high = 255;

	fast_low = AE_low >> 1;

	ov5647_write_reg(0x3a0f, AE_high);
	ov5647_write_reg(0x3a10, AE_low);
	ov5647_write_reg(0x3a1b, AE_high);
	ov5647_write_reg(0x3a1e, AE_low);
	ov5647_write_reg(0x3a11, fast_high);
	ov5647_write_reg(0x3a1f, fast_low);

	return 0;
}

static void ov5647_turn_on_AE_AG(int enable)
{
	u8 ae_ag_ctrl;

	ov5647_read_reg(0x3503, &ae_ag_ctrl);
	if (enable) {
		/* turn on auto AE/AG */
		ae_ag_ctrl = ae_ag_ctrl & ~(0x03);
	} else {
		/* turn off AE/AG */
		ae_ag_ctrl = ae_ag_ctrl | 0x03;
	}
	ov5647_write_reg(0x3503, ae_ag_ctrl);
}

static void ov5647_set_virtual_channel(int channel)
{
	u8 channel_id;

	ov5647_read_reg(0x4814, &channel_id);
	channel_id &= ~(3 << 6);
	ov5647_write_reg(0x4814, channel_id | (channel << 6));
}

/* download ov5647 settings to sensor through i2c */
static int ov5647_download_firmware(struct reg_value *pModeSetting, s32 ArySize)
{
	register u32 Delay_ms = 0;
	register u16 RegAddr = 0;
	register u8 Mask = 0;
	register u8 Val = 0;
	u8 RegVal = 0;
	int i, retval = 0;

	for (i = 0; i < ArySize; ++i, ++pModeSetting) {
		Delay_ms = pModeSetting->u32Delay_ms;
		RegAddr = pModeSetting->u16RegAddr;
		Val = pModeSetting->u8Val;
		Mask = pModeSetting->u8Mask;

		if (Mask) {
			retval = ov5647_read_reg(RegAddr, &RegVal);
			if (retval < 0)
				goto err;

			RegVal &= ~(u8)Mask;
			Val &= Mask;
			Val |= RegVal;
		}

		retval = ov5647_write_reg(RegAddr, Val);
		if (retval < 0)
			goto err;

		if (Delay_ms)
			msleep(Delay_ms);
	}
err:
	return retval;
}

/* sensor changes between scaling and subsampling
 * go through exposure calcualtion
 */
static int ov5647_change_mode_exposure_calc(enum ov5647_frame_rate frame_rate,
				enum ov5647_mode mode)
{
	struct reg_value *pModeSetting = NULL;
	s32 ArySize = 0;
	int pre_shutter, pre_gain16;
	int cap_shutter, cap_gain16;
	int pre_sysclk, pre_HTS;
	int cap_sysclk, cap_HTS, cap_VTS;
	long cap_gain16_shutter;
	int retval = 0;

	/* check if the input mode and frame rate is valid */
	pModeSetting =
		ov5647_mode_info_data[frame_rate][mode].init_data_ptr;
	ArySize =
		ov5647_mode_info_data[frame_rate][mode].init_data_size;

	ov5647_data.pix.width =
		ov5647_mode_info_data[frame_rate][mode].width;
	ov5647_data.pix.height =
		ov5647_mode_info_data[frame_rate][mode].height;

	if (ov5647_data.pix.width == 0 || ov5647_data.pix.height == 0 ||
		pModeSetting == NULL || ArySize == 0)
		return -EINVAL;

	ov5647_stream_off();

	/* turn off night mode for capture */
	ov5647_set_night_mode();

	pre_shutter = ov5647_get_shutter();
	pre_gain16 = ov5647_get_gain16();
	pre_HTS = ov5647_get_HTS();
	pre_sysclk = ov5647_get_sysclk();

	/* Write capture setting */
	retval = ov5647_download_firmware(pModeSetting, ArySize);
	if (retval < 0)
		goto err;

	/* read capture VTS */
	cap_VTS = ov5647_get_VTS();
	cap_HTS = ov5647_get_HTS();
	cap_sysclk = ov5647_get_sysclk();

	/* calculate capture shutter/gain16 */
	cap_shutter = pre_shutter * cap_sysclk/pre_sysclk * pre_HTS / cap_HTS;

	if (cap_shutter < 16) {
		cap_gain16_shutter = pre_shutter * pre_gain16 *
				cap_sysclk / pre_sysclk * pre_HTS / cap_HTS;
		cap_shutter = ((int)(cap_gain16_shutter / 16));
		if (cap_shutter < 1)
			cap_shutter = 1;
		cap_gain16 = ((int)(cap_gain16_shutter / cap_shutter));
		if (cap_gain16 < 16)
			cap_gain16 = 16;
	} else
		cap_gain16 = pre_gain16;

	/* gain to shutter */
	while ((cap_gain16 > 32) &&
			(cap_shutter < ((int)((cap_VTS - 4) / 2)))) {
		cap_gain16 = cap_gain16 / 2;
		cap_shutter = cap_shutter * 2;
	}
	/* write capture gain */
	ov5647_set_gain16(cap_gain16);

	/* write capture shutter */
	if (cap_shutter > (cap_VTS - 4)) {
		cap_VTS = cap_shutter + 4;
		ov5647_set_VTS(cap_VTS);
	}
	ov5647_set_shutter(cap_shutter);

err:
	return retval;
}

/* if sensor changes inside scaling or subsampling
 * change mode directly
 * */
static int ov5647_change_mode_direct(enum ov5647_frame_rate frame_rate,
				enum ov5647_mode mode)
{
	struct reg_value *pModeSetting = NULL;
	s32 ArySize = 0;
	int retval = 0;

	/* check if the input mode and frame rate is valid */
	pModeSetting =
		ov5647_mode_info_data[frame_rate][mode].init_data_ptr;
	ArySize =
		ov5647_mode_info_data[frame_rate][mode].init_data_size;

	ov5647_data.pix.width =
		ov5647_mode_info_data[frame_rate][mode].width;
	ov5647_data.pix.height =
		ov5647_mode_info_data[frame_rate][mode].height;

	if (ov5647_data.pix.width == 0 || ov5647_data.pix.height == 0 ||
		pModeSetting == NULL || ArySize == 0)
		return -EINVAL;

	/* turn off AE/AG */
	ov5647_turn_on_AE_AG(0);

	ov5647_stream_off();

	/* Write capture setting */
	retval = ov5647_download_firmware(pModeSetting, ArySize);
	if (retval < 0)
		goto err;

	ov5647_turn_on_AE_AG(1);

err:
	return retval;
}

static int ov5647_init_mode(enum ov5647_frame_rate frame_rate,
			    enum ov5647_mode mode, enum ov5647_mode orig_mode)
{
	struct reg_value *pModeSetting = NULL;
	s32 ArySize = 0;
	int retval = 0;
	u32 msec_wait4stable = 0;
	enum ov5647_downsize_mode dn_mode, orig_dn_mode;

	if ((mode > ov5647_mode_MAX || mode < ov5647_mode_MIN)
		&& (mode != ov5647_mode_INIT)) {
		pr_err("Wrong ov5647 mode detected!\n");
		return -1;
	}

	dn_mode = ov5647_mode_info_data[frame_rate][mode].dn_mode;
	orig_dn_mode = ov5647_mode_info_data[frame_rate][orig_mode].dn_mode;
	if (mode == ov5647_mode_INIT) {
		ov5647_soft_reset();
		pModeSetting = ov5647_init_setting;
		ArySize = ARRAY_SIZE(ov5647_init_setting);
		retval = ov5647_download_firmware(pModeSetting, ArySize);
		if (retval < 0)
			goto err;
		pModeSetting = ov5647_setting_60fps_VGA_640_480;
		ArySize = ARRAY_SIZE(ov5647_setting_60fps_VGA_640_480);
		retval = ov5647_download_firmware(pModeSetting, ArySize);

		ov5647_data.pix.width = 640;
		ov5647_data.pix.height = 480;
	} else if ((dn_mode == SUBSAMPLING && orig_dn_mode == SCALING) ||
			(dn_mode == SCALING && orig_dn_mode == SUBSAMPLING)) {
		/* change between subsampling and scaling
		 * go through exposure calucation */
		retval = ov5647_change_mode_exposure_calc(frame_rate, mode);
	} else {
		/* change inside subsampling or scaling
		 * download firmware directly */
		retval = ov5647_change_mode_direct(frame_rate, mode);
	}

	if (retval < 0)
		goto err;

	ov5647_set_AE_target(AE_Target);
	ov5647_get_light_freq();
	ov5647_set_bandingfilter();
	ov5647_set_virtual_channel(ov5647_data.csi);

	/* add delay to wait for sensor stable */
	if (mode == ov5647_mode_QSXGA_2592_1944) {
		/* dump the first two frames: 1/7.5*2
		 * the frame rate of QSXGA is 7.5fps */
		msec_wait4stable = 267;
	} else if (frame_rate == ov5647_15_fps) {
		/* dump the first nine frames: 1/15*9 */
		msec_wait4stable = 600;
	} else if (frame_rate == ov5647_30_fps) {
		/* dump the first nine frames: 1/30*9 */
		msec_wait4stable = 300;
	}
	msleep(msec_wait4stable);

err:
	return retval;
}

/*!
 * ov5647_s_power - V4L2 sensor interface handler for VIDIOC_S_POWER ioctl
 * @s: pointer to standard V4L2 device structure
 * @on: indicates power mode (on or off)
 *
 * Turns the power on or off, depending on the value of on and returns the
 * appropriate error code.
 */
static int ov5647_s_power(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5647 *sensor = to_ov5647(client);

	if (on && !sensor->on) {
		if (io_regulator)
			if (regulator_enable(io_regulator) != 0)
				return -EIO;
		if (core_regulator)
			if (regulator_enable(core_regulator) != 0)
				return -EIO;
		if (gpo_regulator)
			if (regulator_enable(gpo_regulator) != 0)
				return -EIO;
		if (analog_regulator)
			if (regulator_enable(analog_regulator) != 0)
				return -EIO;
	} else if (!on && sensor->on) {
		if (analog_regulator)
			regulator_disable(analog_regulator);
		if (core_regulator)
			regulator_disable(core_regulator);
		if (io_regulator)
			regulator_disable(io_regulator);
		if (gpo_regulator)
			regulator_disable(gpo_regulator);
	}

	sensor->on = on;

	return 0;
}

/*!
 * ov5647_g_parm - V4L2 sensor interface handler for VIDIOC_G_PARM ioctl
 * @s: pointer to standard V4L2 sub device structure
 * @a: pointer to standard V4L2 VIDIOC_G_PARM ioctl structure
 *
 * Returns the sensor's video CAPTURE parameters.
 */
static int ov5647_g_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *a)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5647 *sensor = to_ov5647(client);
	struct v4l2_captureparm *cparm = &a->parm.capture;
	int ret = 0;

	switch (a->type) {
	/* This is the only case currently handled. */
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		memset(a, 0, sizeof(*a));
		a->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		cparm->capability = sensor->streamcap.capability;
		cparm->timeperframe = sensor->streamcap.timeperframe;
		cparm->capturemode = sensor->streamcap.capturemode;
		ret = 0;
		break;

	/* These are all the possible cases. */
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	case V4L2_BUF_TYPE_VBI_CAPTURE:
	case V4L2_BUF_TYPE_VBI_OUTPUT:
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		ret = -EINVAL;
		break;

	default:
		pr_debug("   type is unknown - %d\n", a->type);
		ret = -EINVAL;
		break;
	}

	return ret;
}

/*!
 * ov5460_s_parm - V4L2 sensor interface handler for VIDIOC_S_PARM ioctl
 * @s: pointer to standard V4L2 sub device structure
 * @a: pointer to standard V4L2 VIDIOC_S_PARM ioctl structure
 *
 * Configures the sensor to use the input parameters, if possible.  If
 * not possible, reverts to the old parameters and returns the
 * appropriate error code.
 */
static int ov5647_s_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *a)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5647 *sensor = to_ov5647(client);
	struct v4l2_fract *timeperframe = &a->parm.capture.timeperframe;
	u32 tgt_fps;	/* target frames per secound */
	enum ov5647_frame_rate frame_rate;
	enum ov5647_mode orig_mode;
	int ret = 0;

	/* Make sure power on */
	ov5647_power_down(0);

	switch (a->type) {
	/* This is the only case currently handled. */
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		/* Check that the new frame rate is allowed. */
		if ((timeperframe->numerator == 0) ||
		    (timeperframe->denominator == 0)) {
			timeperframe->denominator = DEFAULT_FPS;
			timeperframe->numerator = 1;
		}

		tgt_fps = timeperframe->denominator /
			  timeperframe->numerator;

		if (tgt_fps > MAX_FPS) {
			timeperframe->denominator = MAX_FPS;
			timeperframe->numerator = 1;
		} else if (tgt_fps < MIN_FPS) {
			timeperframe->denominator = MIN_FPS;
			timeperframe->numerator = 1;
		}

		/* Actual frame rate we use */
		tgt_fps = timeperframe->denominator /
			  timeperframe->numerator;

		if (tgt_fps == 15)
			frame_rate = ov5647_15_fps;
		else if (tgt_fps == 30)
			frame_rate = ov5647_30_fps;
		else {
			pr_err(" The camera frame rate is not supported!\n");
			return -EINVAL;
		}

		orig_mode = sensor->streamcap.capturemode;
		ret = ov5647_init_mode(frame_rate,
				(u32)a->parm.capture.capturemode, orig_mode);
		if (ret < 0)
			return ret;

		sensor->streamcap.timeperframe = *timeperframe;
		sensor->streamcap.capturemode =
				(u32)a->parm.capture.capturemode;

		break;

	/* These are all the possible cases. */
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	case V4L2_BUF_TYPE_VBI_CAPTURE:
	case V4L2_BUF_TYPE_VBI_OUTPUT:
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		pr_debug("type is not " \
			"V4L2_BUF_TYPE_VIDEO_CAPTURE but %d\n",
			a->type);
		ret = -EINVAL;
		break;

	default:
		pr_debug("type is unknown - %d\n", a->type);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int ov5647_try_fmt(struct v4l2_subdev *sd,
			  struct v4l2_mbus_framefmt *mf)
{
	const struct ov5647_datafmt *fmt = ov5647_find_datafmt(mf->code);

	if (!fmt) {
		mf->code	= ov5647_colour_fmts[0].code;
		mf->colorspace	= ov5647_colour_fmts[0].colorspace;
	}

	mf->field	= V4L2_FIELD_NONE;

	return 0;
}

static int ov5647_s_fmt(struct v4l2_subdev *sd,
			struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5647 *sensor = to_ov5647(client);

	/* MIPI CSI could have changed the format, double-check */
	if (!ov5647_find_datafmt(mf->code))
		return -EINVAL;

	ov5647_try_fmt(sd, mf);
	sensor->fmt = ov5647_find_datafmt(mf->code);

	return 0;
}

static int ov5647_g_fmt(struct v4l2_subdev *sd,
			struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5647 *sensor = to_ov5647(client);

	const struct ov5647_datafmt *fmt = sensor->fmt;

	mf->code	= fmt->code;
	mf->colorspace	= fmt->colorspace;
	mf->field	= V4L2_FIELD_NONE;

	return 0;
}

static int ov5647_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
			   u32 *code)
{
	if (index >= ARRAY_SIZE(ov5647_colour_fmts))
		return -EINVAL;

	*code = ov5647_colour_fmts[index].code;
	return 0;
}

/*!
 * ov5647_enum_framesizes - V4L2 sensor interface handler for
 *			   VIDIOC_ENUM_FRAMESIZES ioctl
 * @s: pointer to standard V4L2 device structure
 * @fsize: standard V4L2 VIDIOC_ENUM_FRAMESIZES ioctl structure
 *
 * Return 0 if successful, otherwise -EINVAL.
 */
static int ov5647_enum_framesizes(struct v4l2_subdev *sd,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index > ov5647_mode_MAX)
		return -EINVAL;

	fse->max_width =
			max(ov5647_mode_info_data[0][fse->index].width,
			    ov5647_mode_info_data[1][fse->index].width);
	fse->min_width = fse->max_width;
	fse->max_height =
			max(ov5647_mode_info_data[0][fse->index].height,
			    ov5647_mode_info_data[1][fse->index].height);
	fse->min_height = fse->max_height;
	return 0;
}

/*!
 * ov5647_enum_frameintervals - V4L2 sensor interface handler for
 *			       VIDIOC_ENUM_FRAMEINTERVALS ioctl
 * @s: pointer to standard V4L2 device structure
 * @fival: standard V4L2 VIDIOC_ENUM_FRAMEINTERVALS ioctl structure
 *
 * Return 0 if successful, otherwise -EINVAL.
 */
static int ov5647_enum_frameintervals(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_frame_interval_enum *fie)
{
	int i, j, count;

	if (fie->index < 0 || fie->index > ov5647_mode_MAX)
		return -EINVAL;

	if (fie->width == 0 || fie->height == 0 ||
	    fie->code == 0) {
		pr_warning("Please assign pixel format, width and height.\n");
		return -EINVAL;
	}

	fie->interval.numerator = 1;

	count = 0;
	for (i = 0; i < ARRAY_SIZE(ov5647_mode_info_data); i++) {
		for (j = 0; j < (ov5647_mode_MAX + 1); j++) {
			if (fie->width == ov5647_mode_info_data[i][j].width
			 && fie->height == ov5647_mode_info_data[i][j].height
			 && ov5647_mode_info_data[i][j].init_data_ptr != NULL) {
				count++;
			}
			if (fie->index == (count - 1)) {
				fie->interval.denominator =
						ov5647_framerates[i];
				return 0;
			}
		}
	}

	return -EINVAL;
}

/*!
 * dev_init - V4L2 sensor init
 * @s: pointer to standard V4L2 device structure
 *
 */
static int init_device(void)
{
	u32 tgt_xclk;	/* target xclk */
	u32 tgt_fps;	/* target frames per secound */
	enum ov5647_frame_rate frame_rate;
	int ret;

	ov5647_data.on = true;

	/* mclk */
	tgt_xclk = ov5647_data.mclk;
	tgt_xclk = min(tgt_xclk, (u32)OV5647_XCLK_MAX);
	tgt_xclk = max(tgt_xclk, (u32)OV5647_XCLK_MIN);
	ov5647_data.mclk = tgt_xclk;

	pr_debug("   Setting mclk to %d MHz\n", tgt_xclk / 1000000);

	/* Default camera frame rate is set in probe */
	tgt_fps = ov5647_data.streamcap.timeperframe.denominator /
		  ov5647_data.streamcap.timeperframe.numerator;

	if (tgt_fps == 15)
		frame_rate = ov5647_15_fps;
	else if (tgt_fps == 30)
		frame_rate = ov5647_30_fps;
	else
		return -EINVAL; /* Only support 15fps or 30fps now. */

	ret = ov5647_init_mode(frame_rate, ov5647_mode_INIT, ov5647_mode_INIT);

	ov5647_update_otp();
	return ret;
}

static int ov5647_s_stream(struct v4l2_subdev *sd, int enable)
{
	if (enable)
		ov5647_stream_on();
	else
		ov5647_stream_off();
	return 0;
}

static struct v4l2_subdev_video_ops ov5647_subdev_video_ops = {
	.g_parm = ov5647_g_parm,
	.s_parm = ov5647_s_parm,
	.s_stream = ov5647_s_stream,

	.s_mbus_fmt	= ov5647_s_fmt,
	.g_mbus_fmt	= ov5647_g_fmt,
	.try_mbus_fmt	= ov5647_try_fmt,
	.enum_mbus_fmt	= ov5647_enum_fmt,
};

static const struct v4l2_subdev_pad_ops ov5647_subdev_pad_ops = {
	.enum_frame_size       = ov5647_enum_framesizes,
	.enum_frame_interval   = ov5647_enum_frameintervals,
};

static struct v4l2_subdev_core_ops ov5647_subdev_core_ops = {
	.s_power	= ov5647_s_power,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register	= ov5647_get_register,
	.s_register	= ov5647_set_register,
#endif
};

static struct v4l2_subdev_ops ov5647_subdev_ops = {
	.core	= &ov5647_subdev_core_ops,
	.video	= &ov5647_subdev_video_ops,
	.pad	= &ov5647_subdev_pad_ops,
};


/*!
 * ov5647 I2C probe function
 *
 * @param adapter            struct i2c_adapter *
 * @return  Error code indicating success or failure
 */
static int ov5647_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct pinctrl *pinctrl;
	struct device *dev = &client->dev;
	int retval;
	u8 chip_id_high, chip_id_low;

	/* ov5647 pinctrl */
	pinctrl = devm_pinctrl_get_select_default(dev);
	if (IS_ERR(pinctrl)) {
		dev_warn(dev, "no  pin available\n");
	}


	/* request power down pin */
	pwn_gpio = of_get_named_gpio(dev->of_node, "pwn-gpios", 0);
	if (!gpio_is_valid(pwn_gpio)) {
		dev_warn(dev, "no sensor pwdn pin available\n");
		pwn_gpio = -1;
	} else {
		retval = devm_gpio_request_one(dev, pwn_gpio, GPIOF_OUT_INIT_HIGH,
						"ov5647_mipi_pwdn");
		if (retval < 0) {
			dev_warn(dev, "Failed to set power pin\n");
			return retval;
		}
	}

	/* request reset pin */
	rst_gpio = of_get_named_gpio(dev->of_node, "rst-gpios", 0);
	if (!gpio_is_valid(rst_gpio)) {
		dev_warn(dev, "no sensor reset pin available\n");
		rst_gpio = -1;
	} else {
		retval = devm_gpio_request_one(dev, rst_gpio, GPIOF_OUT_INIT_HIGH,
						"ov5647_mipi_reset");
		if (retval < 0) {
			dev_warn(dev, "Failed to set reset pin\n");
			return retval;
		}
	}

	/* Set initial values for the sensor struct. */
	memset(&ov5647_data, 0, sizeof(ov5647_data));
	ov5647_data.sensor_clk = devm_clk_get(dev, "csi_mclk");
	if (IS_ERR(ov5647_data.sensor_clk)) {
		/* assuming clock enabled by default */
		ov5647_data.sensor_clk = NULL;
		dev_err(dev, "clock-frequency missing or invalid\n");
		return PTR_ERR(ov5647_data.sensor_clk);
	}

	retval = of_property_read_u32(dev->of_node, "mclk",
					&(ov5647_data.mclk));
	if (retval) {
		dev_err(dev, "mclk missing or invalid\n");
		return retval;
	}

	retval = of_property_read_u32(dev->of_node, "mclk_source",
					(u32 *) &(ov5647_data.mclk_source));
	if (retval) {
		dev_err(dev, "mclk_source missing or invalid\n");
		return retval;
	}

	retval = of_property_read_u32(dev->of_node, "csi_id",
					&(ov5647_data.csi));
	if (retval) {
		dev_err(dev, "csi id missing or invalid\n");
		return retval;
	}

	clk_prepare_enable(ov5647_data.sensor_clk);

	ov5647_data.io_init = ov5647_reset;
	ov5647_data.i2c_client = client;
	ov5647_data.pix.pixelformat = V4L2_PIX_FMT_SBGGR8;
	ov5647_data.pix.width = 640;
	ov5647_data.pix.height = 480;
	ov5647_data.streamcap.capability = V4L2_MODE_HIGHQUALITY |
					   V4L2_CAP_TIMEPERFRAME;
	ov5647_data.streamcap.capturemode = 0;
	ov5647_data.streamcap.timeperframe.denominator = DEFAULT_FPS;
	ov5647_data.streamcap.timeperframe.numerator = 1;

	ov5647_regulator_enable(&client->dev);

	ov5647_reset();

	ov5647_power_down(0);

	retval = ov5647_read_reg(OV5647_CHIP_ID_HIGH_BYTE, &chip_id_high);
	if (retval < 0 || chip_id_high != 0x56) {
		pr_warning("camera ov5647_mipi is not found\n");
		clk_disable_unprepare(ov5647_data.sensor_clk);
		return -ENODEV;
	}
	retval = ov5647_read_reg(OV5647_CHIP_ID_LOW_BYTE, &chip_id_low);
	if (retval < 0 || chip_id_low != 0x47) {
		pr_warning("camera ov5647_mipi is not found\n");
		clk_disable_unprepare(ov5647_data.sensor_clk);
		return -ENODEV;
	}

	retval = init_device();
	if (retval < 0) {
		clk_disable_unprepare(ov5647_data.sensor_clk);
		pr_warning("camera ov5647 init failed\n");
		ov5647_power_down(1);
		return retval;
	}

	v4l2_i2c_subdev_init(&ov5647_data.subdev, client, &ov5647_subdev_ops);

	ov5647_data.subdev.grp_id = 678;
	retval = v4l2_async_register_subdev(&ov5647_data.subdev);
	if (retval < 0)
		dev_err(&client->dev,
					"%s--Async register failed, ret=%d\n", __func__, retval);

	ov5647_stream_off();
	pr_info("camera ov5647_mipi is found\n");
	return retval;
}

/*!
 * ov5647 I2C detach function
 *
 * @param client            struct i2c_client *
 * @return  Error code indicating success or failure
 */
static int ov5647_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_async_unregister_subdev(sd);

	clk_disable_unprepare(ov5647_data.sensor_clk);

	ov5647_power_down(1);

	if (gpo_regulator)
		regulator_disable(gpo_regulator);

	if (analog_regulator)
		regulator_disable(analog_regulator);

	if (core_regulator)
		regulator_disable(core_regulator);

	if (io_regulator)
		regulator_disable(io_regulator);

	return 0;
}

module_i2c_driver(ov5647_i2c_driver);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("OV5647 MIPI Camera Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
MODULE_ALIAS("CSI");
