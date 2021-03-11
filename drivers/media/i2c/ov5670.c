// SPDX-License-Identifier: GPL-2.0
/*
 * ov5670 driver
 *
 * Copyright (C) 2019 Fuzhou Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X01 add poweron function.
 * V0.0X01.0X02 fix mclk issue when probe multiple camera.
 * V0.0X01.0X03 add otp function.
 * V0.0X01.0X04 add enum_frame_interval function.
 * V0.0X01.0X05 add quick stream on/off
 * V0.0X01.0X06 add function g_mmbus_config
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/pinctrl/consumer.h>
#include <linux/version.h>
#include <media/v4l2-async.h>
#include <media/media-entity.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-image-sizes.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>

#include <linux/rk-camera-module.h>

/* verify default register values */
//#define CHECK_REG_VALUE

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x06)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
#define MIPI_FREQ	420000000U
#define OV5670_PIXEL_RATE		(420000000LL * 2LL * 2LL / 10)
#define OV5670_XVCLK_FREQ		24000000

#define CHIP_ID				0x5670
#define OV5670_REG_CHIP_ID		0x300b

#define OV5670_REG_CTRL_MODE		0x0100
#define OV5670_MODE_SW_STANDBY		0x00
#define OV5670_MODE_STREAMING		0x01

#define OV5670_REG_EXPOSURE		0x3500
#define	OV5670_EXPOSURE_MIN		4
#define	OV5670_EXPOSURE_STEP		1
#define OV5670_VTS_MAX			0x7fff

#define OV5670_REG_GAIN_H		0x3508
#define OV5670_REG_GAIN_L		0x3509
#define OV5670_GAIN_L_MASK		0xff
#define OV5670_GAIN_H_MASK		0x1f
#define OV5670_GAIN_H_SHIFT	8
#define	ANALOG_GAIN_MIN			0x80
#define	ANALOG_GAIN_MAX			0x400
#define	ANALOG_GAIN_STEP		1
#define	ANALOG_GAIN_DEFAULT		1024

#define OV5670_REG_GROUP	0x3208

#define OV5670_REG_TEST_PATTERN		0x4303
#define	OV5670_TEST_PATTERN_ENABLE	0x08
#define	OV5670_TEST_PATTERN_DISABLE	0x0

#define OV5670_REG_VTS			0x380e

#define REG_NULL			0xFFFF
#define DELAY_MS			0xEEEE	/* Array delay token */

#define OV5670_REG_VALUE_08BIT		1
#define OV5670_REG_VALUE_16BIT		2
#define OV5670_REG_VALUE_24BIT		3

#define OV5670_LANES			2
#define OV5670_BITS_PER_SAMPLE		10

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define OV5670_NAME			"ov5670"

#define  RG_Ratio_Typical_Default (0x16f)
#define  BG_Ratio_Typical_Default (0x16f)

#define ov5670_write_1byte(client, reg, val)	\
	ov5670_write_reg((client), (reg), OV5670_REG_VALUE_08BIT, (val))

#define ov5670_read_1byte(client, reg, val)	\
	ov5670_read_reg((client), (reg), OV5670_REG_VALUE_08BIT, (val))

struct ov5670_otp_info {
	int flag; // bit[7]: info, bit[6]:wb
	int module_id;
	int lens_id;
	int year;
	int month;
	int day;
	int rg_ratio;
	int bg_ratio;
};

static const char * const ov5670_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define OV5670_NUM_SUPPLIES ARRAY_SIZE(ov5670_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct ov5670_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct regval *reg_list;
};

struct ov5670 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*power_gpio;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[OV5670_NUM_SUPPLIES];

	struct pinctrl		*pinctrl;
	struct pinctrl_state	*pins_default;
	struct pinctrl_state	*pins_sleep;

	struct v4l2_subdev	subdev;
	struct media_pad	pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl	*exposure;
	struct v4l2_ctrl	*anal_gain;
	struct v4l2_ctrl	*digi_gain;
	struct v4l2_ctrl	*hblank;
	struct v4l2_ctrl	*vblank;
	struct v4l2_ctrl	*test_pattern;
	struct mutex		mutex;
	bool			streaming;
	bool			power_on;
	const struct ov5670_mode *cur_mode;
	unsigned int lane_num;
	unsigned int cfg_num;
	unsigned int pixel_rate;
	u32			module_index;
	struct ov5670_otp_info *otp;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	struct rkmodule_awb_cfg	awb_cfg;
};

#define to_ov5670(sd) container_of(sd, struct ov5670, subdev)

struct ov5670_id_name {
	int id;
	char name[RKMODULE_NAME_LEN];
};

static const struct ov5670_id_name ov5670_module_info[] = {
	{0x01, "Sunny"},
	{0x02, "Truly"},
	{0x03, "A-kerr"},
	{0x04, "LiteArray"},
	{0x05, "Darling"},
	{0x06, "Qtech"},
	{0x07, "OFlim"},
	{0x08, "Huaquan/Kingcom"},
	{0x09, "Booyi"},
	{0x0a, "Laimu"},
	{0x0b, "WDSEN"},
	{0x0c, "Sunrise"},
	{0x0d, "CameraKing"},
	{0x0e, "Sunniness/Riyong"},
	{0x0f, "Tongju"},
	{0x10, "Seasons/Sijichun"},
	{0x11, "Foxconn"},
	{0x12, "Importek"},
	{0x13, "Altek"},
	{0x14, "ABICO/Ability"},
	{0x15, "Lite-on"},
	{0x16, "Chicony"},
	{0x17, "Primax"},
	{0x18, "AVC"},
	{0x19, "Suyin"},
	{0x21, "Sharp"},
	{0x31, "MCNEX"},
	{0x32, "SEMCO"},
	{0x33, "Partron"},
	{0x41, "Reach/Zhongliancheng"},
	{0x42, "BYD"},
	{0x43, "OSTEC(AoShunChuang)"},
	{0x44, "Chengli"},
	{0x45, "Jiali"},
	{0x46, "Chippack"},
	{0x47, "RongSheng"},
	{0x48, "ShineTech/ShenTai"},
	{0x49, "Brodsands"},
	{0x50, "Others"},
	{0x00, "Unknown"}
};

static const struct ov5670_id_name ov5670_lens_info[] = {
	{0x01, "Largan 40010A2"},
	{0x10, "Largan 30048A1"},
	{0x11, "Largan 30031A1B"},
	{0x12, "Largan 40010A1"},
	{0x30, "Sunny 3531A"},
	{0x31, "Sunny 3531B"},
	{0x32, "Sunny 3533A"},
	{0x90, "Kinko 3956AH"},
	{0xa0, "E-pin D517"},
	{0xc0, "XuYe XA-0502B"},
	{0xc8, "XuYe XA-0502A"},
	{0xc9, "XuYe E009A"},
	{0x00, "Unknown"}
};

/*
 * Xclk 24Mhz
 * Pclk 84Mhz
 * linelength 2816(0xb00)
 * framelength 1984(0x7c0)
 * grabwindow_width 2592
 * grabwindow_height 1944
 * max_framerate 30fps
 * mipi_datarate per lane 840Mbps
 */
static const struct regval ov5670_global_regs[] = {
	{0x0103, 0x01}, //software reset
	{DELAY_MS, 5},
	{0x0100, 0x00}, //software standby
	{0x0300, 0x04}, //PLL
	{0x0301, 0x00},
	{0x0302, 0x69}, //MIPI bit rate 840Mbps/lane
	{0x0303, 0x00},
	{0x0304, 0x03},
	{0x0305, 0x01},
	{0x0306, 0x01},
	{0x030a, 0x00},
	{0x030b, 0x00},
	{0x030c, 0x00},
	{0x030d, 0x1e},
	{0x030e, 0x00},
	{0x030f, 0x06},
	{0x0312, 0x01}, //PLL
	{0x3000, 0x00}, //Fsin/Vsync input
	{0x3002, 0x21}, //ULPM output
	{0x3005, 0xf0}, //sclk_psram on, sclk_syncfifo on
	{0x3007, 0x00},
	{0x3015, 0x0f}, //npump clock div = 1, disable Ppumu_clk
	{0x3018, 0x32}, //MIPI 2 lane

	{0x301a, 0xf0}, //sclk_stb on, sclk_ac on, slck_tc on
	{0x301b, 0xf0}, //sclk_blc/isp/testmode/vfifo on
	{0x301c, 0xf0}, //sclk_mipi on, sclk_dpcm on, sclk_otp on
	{0x301d, 0xf0}, //sclk_asram_tst on, sclk_grp on, sclk_bist on,
	{0x301e, 0xf0}, //sclk_ilpwm/lvds/vfifo/mipi on
	{0x3030, 0x00}, //sclk normal, pclk normal
	{0x3031, 0x0a}, //10-bit mode
	{0x303c, 0xff}, //reserved
	{0x303e, 0xff}, //reserved
	{0x3040, 0xf0}, //sclk_isp_fc_en, sclk_fc-en, sclk_tpm_en, sclk_fmt_en
	{0x3041, 0x00}, //reserved
	{0x3042, 0xf0}, //reserved
	{0x3106, 0x11}, //sclk_div = 1, sclk_pre_div = 1
	{0x3500, 0x00}, //exposure H
	{0x3501, 0x3d}, //exposure M
	{0x3502, 0x00}, //exposure L
	{0x3503, 0x04}, //gain no delay, use sensor gain
	{0x3504, 0x03}, //exposure manual, gain manual
	{0x3505, 0x83}, //sensor gain fixed bit
	{0x3508, 0x04}, //gain H
	{0x3509, 0x00}, //gain L
	{0x350e, 0x04}, //short digital gain H
	{0x350f, 0x00}, //short digital gain L
	{0x3510, 0x00}, //short exposure H
	{0x3511, 0x02}, //short exposure M
	{0x3512, 0x00}, //short exposure L
	{0x3601, 0xc8}, //analog control
	{0x3610, 0x88},
	{0x3612, 0x48},
	{0x3614, 0x5b},
	{0x3615, 0x96},
	{0x3621, 0xd0},
	{0x3622, 0x00},
	{0x3623, 0x00},
	{0x3633, 0x13},
	{0x3634, 0x13},
	{0x3635, 0x13},
	{0x3636, 0x13},
	{0x3645, 0x13},
	{0x3646, 0x82},
	{0x3650, 0x00},
	{0x3652, 0xff},
	{0x3655, 0x20},
	{0x3656, 0xff},
	{0x365a, 0xff},

	{0x365e, 0xff},
	{0x3668, 0x00},
	{0x366a, 0x07},
	{0x366e, 0x08},
	{0x366d, 0x00},
	{0x366f, 0x80}, //analog control
	{0x3700, 0x28}, //sensor control
	{0x3701, 0x10},
	{0x3702, 0x3a},
	{0x3703, 0x19},
	{0x3704, 0x10},
	{0x3705, 0x00},
	{0x3706, 0x66},
	{0x3707, 0x08},
	{0x3708, 0x34},
	{0x3709, 0x40},
	{0x370a, 0x01},
	{0x370b, 0x1b},
	{0x3714, 0x24},
	{0x371a, 0x3e},
	{0x3733, 0x00},
	{0x3734, 0x00},
	{0x373a, 0x05},
	{0x373b, 0x06},
	{0x373c, 0x0a},
	{0x373f, 0xa0},
	{0x3755, 0x00},
	{0x3758, 0x00},
	{0x375b, 0x0e},
	{0x3766, 0x5f},
	{0x3768, 0x00},
	{0x3769, 0x22},
	{0x3773, 0x08},
	{0x3774, 0x1f},
	{0x3776, 0x06},
	{0x37a0, 0x88},
	{0x37a1, 0x5c},
	{0x37a7, 0x88},
	{0x37a8, 0x70},
	{0x37aa, 0x88},
	{0x37ab, 0x48},
	{0x37b3, 0x66},
	{0x37c2, 0x04},
	{0x37c5, 0x00},
	{0x37c8, 0x00}, //sensor control

	{0x3800, 0x00}, //x addr start H
	{0x3801, 0x0c}, //x addr start L
	{0x3802, 0x00}, //y addr start H
	{0x3803, 0x04}, //y addr start L
	{0x3804, 0x0a}, //x addr end H
	{0x3805, 0x33}, //x addr end L
	{0x3806, 0x07}, //y addr end H
	{0x3807, 0xa3}, //y addr end L
	{0x3808, 0x05}, //x output size H
	{0x3809, 0x10}, //x output size L
	{0x380a, 0x03}, //y output size H
	{0x380b, 0xc0}, //y output size L
	{0x380c, 0x06}, //HTS H
	{0x380d, 0x90}, //HTS L
	{0x380e, 0x03}, //VTS H
	{0x380f, 0xfc}, //VTS L
	{0x3811, 0x04}, //ISP x win L
	{0x3813, 0x02}, //ISP y win L
	{0x3814, 0x03}, //x inc odd
	{0x3815, 0x01}, //x inc even
	{0x3816, 0x00}, //vsync start H
	{0x3817, 0x00}, //vsync star L
	{0x3818, 0x00}, //vsync end H
	{0x3819, 0x00}, //vsync end L
	{0x3820, 0x90}, //vsyn48_blc on, vflip off
	{0x3821, 0x47}, //hsync_en_o, mirror on, dig_bin on
	{0x3822, 0x48}, //addr0_num[3:1]=0x02, ablc_num[5:1]=0x08
	{0x3826, 0x00}, //r_rst_fsin H
	{0x3827, 0x08}, //r_rst_fsin L
	{0x382a, 0x03}, //y inc odd
	{0x382b, 0x01}, //y inc even
	{0x3830, 0x08},
	{0x3836, 0x02},
	{0x3837, 0x00},
	{0x3838, 0x10},
	{0x3841, 0xff},
	{0x3846, 0x48},
	{0x3861, 0x00},
	{0x3862, 0x04},
	{0x3863, 0x06},
	{0x3a11, 0x01},
	{0x3a12, 0x78},
	{0x3b00, 0x00}, //strobe
	{0x3b02, 0x00},
	{0x3b03, 0x00},

	{0x3b04, 0x00},
	{0x3b05, 0x00}, //strobe
	{0x3c00, 0x89},
	{0x3c01, 0xab},
	{0x3c02, 0x01},
	{0x3c03, 0x00},
	{0x3c04, 0x00},
	{0x3c05, 0x03},
	{0x3c06, 0x00},
	{0x3c07, 0x05},
	{0x3c0c, 0x00},
	{0x3c0d, 0x00},
	{0x3c0e, 0x00},
	{0x3c0f, 0x00},
	{0x3c40, 0x00},
	{0x3c41, 0xa3},
	{0x3c43, 0x7d},
	{0x3c45, 0xd7},
	{0x3c47, 0xfc},
	{0x3c50, 0x05},
	{0x3c52, 0xaa},
	{0x3c54, 0x71},
	{0x3c56, 0x80},
	{0x3d85, 0x17},
	{0x3f03, 0x00}, //PSRAM
	{0x3f0a, 0x00},
	{0x3f0b, 0x00}, //PSRAM
	{0x4001, 0x60}, //BLC, K enable
	{0x4009, 0x05}, //BLC, black line end line
	{0x4020, 0x00}, //BLC, offset compensation th000
	{0x4021, 0x00}, //BLC, offset compensation K000
	{0x4022, 0x00},
	{0x4023, 0x00},
	{0x4024, 0x00},
	{0x4025, 0x00},
	{0x4026, 0x00},
	{0x4027, 0x00},
	{0x4028, 0x00},
	{0x4029, 0x00},
	{0x402a, 0x00},
	{0x402b, 0x00},
	{0x402c, 0x00},
	{0x402d, 0x00},
	{0x402e, 0x00},
	{0x402f, 0x00},

	{0x4040, 0x00},
	{0x4041, 0x03},
	{0x4042, 0x00},
	{0x4043, 0x7A}, //1/1.05 x (0x80)
	{0x4044, 0x00},
	{0x4045, 0x7A},
	{0x4046, 0x00},
	{0x4047, 0x7A},
	{0x4048, 0x00}, //BLC, kcoef_r_man H
	{0x4049, 0x7A}, //BLC, kcoef_r_man L
	{0x4303, 0x00},
	{0x4307, 0x30},
	{0x4500, 0x58},
	{0x4501, 0x04},
	{0x4502, 0x48},
	{0x4503, 0x10},
	{0x4508, 0x55},
	{0x4509, 0x55},
	{0x450a, 0x00},
	{0x450b, 0x00},
	{0x4600, 0x00},
	{0x4601, 0x81},
	{0x4700, 0xa4},
	{0x4800, 0x4c}, //MIPI control
	{0x4816, 0x53}, //emb_dt
	{0x481f, 0x40}, //clock_prepare_min
	{0x4837, 0x13}, //MIPI global timing
	{0x5000, 0x56}, //dcblc_en, awb_gain_en, bc_en, wc_en
	{0x5001, 0x01}, //blc_en
	{0x5002, 0x28}, //otp_dpc_en
	{0x5004, 0x0c}, //ISP size auto control enable
	{0x5006, 0x0c},
	{0x5007, 0xe0},
	{0x5008, 0x01},
	{0x5009, 0xb0},
	{0x5901, 0x00}, //VAP
	{0x5a01, 0x00}, //WINC x start offset H
	{0x5a03, 0x00}, //WINC x start offset L
	{0x5a04, 0x0c}, //WINC y start offset H
	{0x5a05, 0xe0}, //WINC y start offset L
	{0x5a06, 0x09}, //WINC window width H
	{0x5a07, 0xb0}, //WINC window width L
	{0x5a08, 0x06}, //WINC window height H
	{0x5e00, 0x00}, //WINC window height L
	{0x3734, 0x40}, //Improve HFPN

	{0x5b00, 0x01}, //[2:0] otp start addr[10:8]
	{0x5b01, 0x10}, //[7:0] otp start addr[7:0]
	{0x5b02, 0x01}, //[2:0] otp end addr[10:8]
	{0x5b03, 0xdb}, //[7:0] otp end addr[7:0]
	{0x3d8c, 0x71}, //Header address high byte
	{0x3d8d, 0xea}, //Header address low byte
	{0x4017, 0x10}, //threshold = 4LSB for Binning sum format.
	{0x3618, 0x2a},
	{0x5780, 0x3e},
	{0x5781, 0x0f},
	{0x5782, 0x44},
	{0x5783, 0x02},
	{0x5784, 0x01},
	{0x5785, 0x01},
	{0x5786, 0x00},
	{0x5787, 0x04},
	{0x5788, 0x02},
	{0x5789, 0x0f},
	{0x578a, 0xfd},
	{0x578b, 0xf5},
	{0x578c, 0xf5},
	{0x578d, 0x03},
	{0x578e, 0x08},
	{0x578f, 0x0c},
	{0x5790, 0x08},
	{0x5791, 0x06},
	{0x5792, 0x00},
	{0x5793, 0x52},
	{0x5794, 0xa3},
	{0x3503, 0x30}, //exposure gain/exposure delay not used
	{0x3002, 0x61}, //[6]ULPM output enable
	{0x3010, 0x40}, //[6]enable ULPM as GPIO controlled by register
	{0x300d, 0x00}, //[6]ULPM output low (if 1=> high)
	{0x5045, 0x05}, //[2] enable MWB manual bias
	{0x5048, 0x10}, //MWB manual bias be the same with 0x4003 BLC target.
	//{0x0100, 0x01},

	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * Pclk 210Mhz
 * linelength 3360(0xd20
 * framelength 2038(0x7f6)
 * grabwindow_width 2592
 * grabwindow_height 1944
 * max_framerate 30fps
 * mipi_datarate per lane 840Mbps
 */
static const struct regval ov5670_2592x1944_regs_2lane[] = {
	// 2592x1944 30fps 2 lane MIPI 840Mbps/lane
	{0x0100, 0x00},
	{0x3501, 0x7b}, //exposure M
	{0x3623, 0x00}, //analog control
	{0x366e, 0x10}, //analog control
	{0x370b, 0x1b}, //sensor control
	{0x3808, 0x0a}, //x output size H
	{0x3809, 0x20}, //x output size L
	{0x380a, 0x07}, //y output size H
	{0x380b, 0x98}, //y output size L
	{0x380c, 0x06}, //HTS H
	{0x380d, 0x90}, //HTS L
	{0x380e, 0x07}, //VTS H
	{0x380f, 0xf6}, //VTS L
	{0x3814, 0x01}, //x inc odd
	{0x3820, 0x80}, //vsyn48_blc on, vflip off
	{0x3821, 0x46}, //hsync_en_o, mirror on, dig_bin on
	{0x382a, 0x01}, //y inc odd
	{0x4009, 0x0d}, //BLC, black line end line
	{0x4502, 0x40},
	{0x4508, 0xaa},
	{0x4509, 0xaa},
	{0x450a, 0x00},
	{0x4600, 0x01},
	{0x4601, 0x03},
	{0x4017, 0x08}, //BLC, offset trigger threshold
	//{0x0100, 0x01},

	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * Pclk 210Mhz
 * linelength 3360(0xd20
 * framelength 2038(0x7f6)
 * grabwindow_width 1296
 * grabwindow_height 960
 * max_framerate 30fps
 * mipi_datarate per lane 840Mbps
 */
static const struct regval ov5670_1296x960_regs_2lane[] = {
	// 1296x960 30fps 2 lane MIPI 840Mbps/lane
	{0x0100, 0x00},
	{0x3501, 0x3d}, //exposure M
	{0x3623, 0x00}, //analog control
	{0x366e, 0x08}, //analog control
	{0x370b, 0x1b}, //sensor control
	{0x3808, 0x05}, //x output size H
	{0x3809, 0x10}, //x output size L
	{0x380a, 0x03}, //y output size H
	{0x380b, 0xc0}, //y output size L
	{0x380c, 0x06}, //HTS H
	{0x380d, 0x90}, //HTS L
	{0x380e, 0x07}, //VTS H
	{0x380f, 0xf6}, //VTS L
	{0x3814, 0x03}, //x inc odd
	{0x3820, 0x90}, //vsyn48_blc on, vflip off
	{0x3821, 0x47}, //hsync_en_o, mirror on, dig_bin on
	{0x382a, 0x03}, //y inc odd
	{0x4009, 0x05}, //BLC, black line end line
	{0x4502, 0x48},
	{0x4508, 0x55},
	{0x4509, 0x55},
	{0x450a, 0x00},
	{0x4600, 0x00},
	{0x4601, 0x81},
	{0x4017, 0x10}, //BLC, offset trigger threshold
	//{0x0100, 0x01},

	{REG_NULL, 0x00}
};

static const struct ov5670_mode supported_modes_2lane[] = {
	{
		.width = 2592,
		.height = 1944,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x07d0,
		.hts_def = 0x0d20,
		.vts_def = 0x07f6,
		.reg_list = ov5670_2592x1944_regs_2lane,
	},
	{
		.width = 1296,
		.height = 960,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x03d0,
		.hts_def = 0x0d20,
		.vts_def = 0x07f6,
		.reg_list = ov5670_1296x960_regs_2lane,
	},
};

static const struct ov5670_mode *supported_modes;

static const s64 link_freq_menu_items[] = {
	MIPI_FREQ
};

static const char * const ov5670_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
};

/* Write registers up to 4 at a time */
static int ov5670_write_reg(struct i2c_client *client, u16 reg,
			    u32 len, u32 val)
{
	u32 buf_i, val_i;
	u8 buf[6];
	u8 *val_p;
	__be32 val_be;

	dev_dbg(&client->dev, "%s(%d) enter!\n", __func__, __LINE__);
	dev_dbg(&client->dev, "write reg(0x%x val:0x%x)!\n", reg, val);

	if (len > 4)
		return -EINVAL;

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;

	val_be = cpu_to_be32(val);
	val_p = (u8 *)&val_be;
	buf_i = 2;
	val_i = 4 - len;

	while (val_i < 4)
		buf[buf_i++] = val_p[val_i++];

	if (i2c_master_send(client, buf, len + 2) != len + 2) {
		dev_err(&client->dev,
			   "write reg(0x%x val:0x%x)failed !\n", reg, val);
		return -EIO;
	}
	return 0;
}

static int ov5670_write_array(struct i2c_client *client,
			      const struct regval *regs)
{
	int i, delay_ms, ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++) {
		if (regs[i].addr == DELAY_MS) {
			delay_ms = regs[i].val;
			dev_info(&client->dev, "delay(%d) ms !\n", delay_ms);
			usleep_range(1000 * delay_ms, 1000 * delay_ms + 100);
			continue;
		}
		ret = ov5670_write_reg(client, regs[i].addr,
				       OV5670_REG_VALUE_08BIT, regs[i].val);
		if (ret)
			dev_err(&client->dev, "%s failed !\n", __func__);
	}
	return ret;
}

/* Read registers up to 4 at a time */
static int ov5670_read_reg(struct i2c_client *client, u16 reg,
					unsigned int len, u32 *val)
{
	struct i2c_msg msgs[2];
	u8 *data_be_p;
	__be32 data_be = 0;
	__be16 reg_addr_be = cpu_to_be16(reg);
	int ret;

	if (len > 4 || !len)
		return -EINVAL;

	data_be_p = (u8 *)&data_be;
	/* Write register address */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 2;
	msgs[0].buf = (u8 *)&reg_addr_be;

	/* Read data from register */
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_be_p[4 - len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	*val = be32_to_cpu(data_be);

	return 0;
}

/* Check Register value */
#ifdef CHECK_REG_VALUE
static int ov5670_reg_verify(struct i2c_client *client,
				const struct regval *regs)
{
	u32 i;
	int ret = 0;
	u32 value;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++) {
		ret = ov5670_read_reg(client, regs[i].addr,
			  OV5670_REG_VALUE_08BIT, &value);
		if (value != regs[i].val) {
			dev_info(&client->dev, "%s: 0x%04x is 0x%x instead of 0x%x\n",
				  __func__, regs[i].addr, value, regs[i].val);
		}
	}
	return ret;
}
#endif

static int ov5670_get_reso_dist(const struct ov5670_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct ov5670_mode *
ov5670_find_best_fit(struct ov5670 *ov5670,
			struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ov5670->cfg_num; i++) {
		dist = ov5670_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int ov5670_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct ov5670 *ov5670 = to_ov5670(sd);
	const struct ov5670_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&ov5670->mutex);

	mode = ov5670_find_best_fit(ov5670, fmt);
	fmt->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&ov5670->mutex);
		return -ENOTTY;
#endif
	} else {
		ov5670->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(ov5670->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(ov5670->vblank, vblank_def,
					 OV5670_VTS_MAX - mode->height,
					 1, vblank_def);
	}

	mutex_unlock(&ov5670->mutex);

	return 0;
}

static int ov5670_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct ov5670 *ov5670 = to_ov5670(sd);
	const struct ov5670_mode *mode = ov5670->cur_mode;

	mutex_lock(&ov5670->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&ov5670->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;
		fmt->format.field = V4L2_FIELD_NONE;
	}
	mutex_unlock(&ov5670->mutex);

	return 0;
}

static int ov5670_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_SBGGR10_1X10;

	return 0;
}

static int ov5670_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct ov5670 *ov5670 = to_ov5670(sd);

	if (fse->index >= ov5670->cfg_num)
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_SBGGR10_1X10)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int ov5670_enable_test_pattern(struct ov5670 *ov5670, u32 pattern)
{
	u32 val;

	if (pattern)
		val = (pattern - 1) | OV5670_TEST_PATTERN_ENABLE;
	else
		val = OV5670_TEST_PATTERN_DISABLE;

	return ov5670_write_reg(ov5670->client, OV5670_REG_TEST_PATTERN,
				OV5670_REG_VALUE_08BIT, val);
}

static void ov5670_get_otp(struct ov5670_otp_info *otp,
			       struct rkmodule_inf *inf)
{
	u32 i;
	int rg, bg;

	/* fac */
	if (otp->flag & 0x80) {
		inf->fac.flag = 1;
		inf->fac.year = otp->year;
		inf->fac.month = otp->month;
		inf->fac.day = otp->day;

		for (i = 0; i < ARRAY_SIZE(ov5670_module_info) - 1; i++) {
			if (ov5670_module_info[i].id == otp->module_id)
				break;
		}
		strlcpy(inf->fac.module, ov5670_module_info[i].name,
			sizeof(inf->fac.module));

		for (i = 0; i < ARRAY_SIZE(ov5670_lens_info) - 1; i++) {
			if (ov5670_lens_info[i].id == otp->lens_id)
				break;
		}
		strlcpy(inf->fac.lens, ov5670_lens_info[i].name,
			sizeof(inf->fac.lens));
	}

	/* awb */
	if (otp->flag & 0x40) {
		rg = otp->rg_ratio;
		bg = otp->bg_ratio;

		inf->awb.flag = 1;
		inf->awb.r_value = rg;
		inf->awb.b_value = bg;
		inf->awb.gr_value = 0x200;
		inf->awb.gb_value = 0x200;

		inf->awb.golden_r_value = 0;
		inf->awb.golden_b_value = 0;
		inf->awb.golden_gr_value = 0;
		inf->awb.golden_gb_value = 0;
	}
}

static int ov5670_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct ov5670 *ov5670 = to_ov5670(sd);
	const struct ov5670_mode *mode = ov5670->cur_mode;

	mutex_lock(&ov5670->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&ov5670->mutex);

	return 0;
}

static void ov5670_get_module_inf(struct ov5670 *ov5670,
				  struct rkmodule_inf *inf)
{
	struct ov5670_otp_info *otp = ov5670->otp;
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, OV5670_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, ov5670->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, ov5670->len_name, sizeof(inf->base.lens));
	if (otp)
		ov5670_get_otp(otp, inf);
}

static void ov5670_set_awb_cfg(struct ov5670 *ov5670,
				 struct rkmodule_awb_cfg *cfg)
{
	mutex_lock(&ov5670->mutex);
	memcpy(&ov5670->awb_cfg, cfg, sizeof(*cfg));
	mutex_unlock(&ov5670->mutex);
}

static long ov5670_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct ov5670 *ov5670 = to_ov5670(sd);
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		ov5670_get_module_inf(ov5670, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_AWB_CFG:
		ov5670_set_awb_cfg(ov5670, (struct rkmodule_awb_cfg *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = ov5670_write_reg(ov5670->client, OV5670_REG_CTRL_MODE,
				OV5670_REG_VALUE_08BIT, OV5670_MODE_STREAMING);
		else
			ret = ov5670_write_reg(ov5670->client, OV5670_REG_CTRL_MODE,
				OV5670_REG_VALUE_08BIT, OV5670_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long ov5670_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *awb_cfg;
	long ret;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = ov5670_ioctl(sd, cmd, inf);
		if (!ret)
			ret = copy_to_user(up, inf, sizeof(*inf));
		kfree(inf);
		break;
	case RKMODULE_AWB_CFG:
		awb_cfg = kzalloc(sizeof(*awb_cfg), GFP_KERNEL);
		if (!awb_cfg) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(awb_cfg, up, sizeof(*awb_cfg));
		if (!ret)
			ret = ov5670_ioctl(sd, cmd, awb_cfg);
		kfree(awb_cfg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = ov5670_ioctl(sd, cmd, &stream);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif
/*--------------------------------------------------------------------------*/
static int ov5670_apply_otp(struct ov5670 *ov5670)
{
	int rg, bg, R_gain, G_gain, B_gain, base_gain;
	struct i2c_client *client = ov5670->client;
	struct ov5670_otp_info *otp_ptr = ov5670->otp;
	struct rkmodule_awb_cfg *awb_cfg = &ov5670->awb_cfg;
	u32 golden_bg_ratio = 0;
	u32 golden_rg_ratio = 0;
	u32 golden_g_value = 0;

	if (awb_cfg->enable) {
		golden_g_value = (awb_cfg->golden_gb_value +
				  awb_cfg->golden_gr_value) / 2;
		if (golden_g_value != 0) {
			golden_rg_ratio = awb_cfg->golden_r_value * 0x200
				  / golden_g_value;
			golden_bg_ratio = awb_cfg->golden_b_value * 0x200
				  / golden_g_value;
		} else {
			golden_rg_ratio = RG_Ratio_Typical_Default;
			golden_bg_ratio = BG_Ratio_Typical_Default;
		}
	}

	/* apply OTP WB Calibration */
	if (otp_ptr->flag & 0x40) {
		rg = otp_ptr->rg_ratio;
		bg = otp_ptr->bg_ratio;

		/* calculate G gain */
		R_gain = (golden_rg_ratio * 1000) / rg;
		B_gain = (golden_bg_ratio * 1000) / bg;
		G_gain = 1000;
		if (R_gain < 1000 || B_gain < 1000) {
			if (R_gain < B_gain)
				base_gain = R_gain;
			else
				base_gain = B_gain;
		} else {
			base_gain = G_gain;
		}
		R_gain = 0x400 * R_gain / (base_gain);
		B_gain = 0x400 * B_gain / (base_gain);
		G_gain = 0x400 * G_gain / (base_gain);

		/* update sensor WB gain */
		if (R_gain > 0x400) {
			ov5670_write_1byte(client, 0x5032, R_gain >> 8);
			ov5670_write_1byte(client, 0x5033, R_gain & 0x00ff);
		}
		if (G_gain > 0x400) {
			ov5670_write_1byte(client, 0x5034, G_gain >> 8);
			ov5670_write_1byte(client, 0x5035, G_gain & 0x00ff);
		}
		if (B_gain > 0x400) {
			ov5670_write_1byte(client, 0x5036, B_gain >> 8);
			ov5670_write_1byte(client, 0x5037, B_gain & 0x00ff);
		}

		dev_info(&client->dev, "apply awb gain: 0x%x, 0x%x, 0x%x\n",
			R_gain, G_gain, B_gain);
	}
	return 0;
}

static int __ov5670_start_stream(struct ov5670 *ov5670)
{
	int ret;

	ret = ov5670_write_array(ov5670->client, ov5670->cur_mode->reg_list);
	if (ret)
		return ret;

#ifdef CHECK_REG_VALUE
	usleep_range(10000, 20000);
	/*  verify default values to make sure everything has */
	/*  been written correctly as expected */
	dev_info(&ov5670->client->dev, "%s:Check register value!\n",
				__func__);
	ret = ov5670_reg_verify(ov5670->client, ov5670_global_regs);
	if (ret)
		return ret;

	ret = ov5670_reg_verify(ov5670->client, ov5670->cur_mode->reg_list);
	if (ret)
		return ret;
#endif

	/* In case these controls are set before streaming */
	mutex_unlock(&ov5670->mutex);
	ret = v4l2_ctrl_handler_setup(&ov5670->ctrl_handler);
	mutex_lock(&ov5670->mutex);
	if (ret)
		return ret;

	if (ov5670->otp)
		ret = ov5670_apply_otp(ov5670);

	if (ret)
		dev_info(&ov5670->client->dev, "APPly otp failed!\n");

	ret = ov5670_write_reg(ov5670->client, OV5670_REG_CTRL_MODE,
				OV5670_REG_VALUE_08BIT, OV5670_MODE_STREAMING);
	return ret;
}

static int __ov5670_stop_stream(struct ov5670 *ov5670)
{
	return ov5670_write_reg(ov5670->client, OV5670_REG_CTRL_MODE,
				OV5670_REG_VALUE_08BIT, OV5670_MODE_SW_STANDBY);
}

static int ov5670_s_stream(struct v4l2_subdev *sd, int on)
{
	struct ov5670 *ov5670 = to_ov5670(sd);
	struct i2c_client *client = ov5670->client;
	int ret = 0;

	dev_info(&client->dev, "%s: on: %d, %dx%d@%d\n", __func__, on,
				ov5670->cur_mode->width,
				ov5670->cur_mode->height,
		DIV_ROUND_CLOSEST(ov5670->cur_mode->max_fps.denominator,
		ov5670->cur_mode->max_fps.numerator));

	mutex_lock(&ov5670->mutex);
	on = !!on;
	if (on == ov5670->streaming)
		goto unlock_and_return;

	if (on) {
		dev_info(&client->dev, "stream on!!!\n");
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __ov5670_start_stream(ov5670);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		dev_info(&client->dev, "stream off!!!\n");
		__ov5670_stop_stream(ov5670);
		pm_runtime_put(&client->dev);
	}

	ov5670->streaming = on;

unlock_and_return:
	mutex_unlock(&ov5670->mutex);

	return ret;
}

static int ov5670_s_power(struct v4l2_subdev *sd, int on)
{
	struct ov5670 *ov5670 = to_ov5670(sd);
	struct i2c_client *client = ov5670->client;
	int ret = 0;

	dev_info(&client->dev, "%s(%d) on(%d)\n", __func__, __LINE__, on);
	mutex_lock(&ov5670->mutex);

	/* If the power state is not modified - no work to do. */
	if (ov5670->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = ov5670_write_array(ov5670->client, ov5670_global_regs);
		if (ret) {
			v4l2_err(sd, "could not set init registers\n");
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ov5670->power_on = true;
		/* export gpio */
		if (!IS_ERR(ov5670->reset_gpio))
			gpiod_export(ov5670->reset_gpio, false);
		if (!IS_ERR(ov5670->pwdn_gpio))
			gpiod_export(ov5670->pwdn_gpio, false);
	} else {
		pm_runtime_put(&client->dev);
		ov5670->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&ov5670->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 ov5670_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, OV5670_XVCLK_FREQ / 1000 / 1000);
}

static int __ov5670_power_on(struct ov5670 *ov5670)
{
	int ret;
	u32 delay_us;
	struct device *dev = &ov5670->client->dev;

	if (!IS_ERR(ov5670->power_gpio))
		gpiod_set_value_cansleep(ov5670->power_gpio, 1);

	usleep_range(1000, 2000);

	if (!IS_ERR_OR_NULL(ov5670->pins_default)) {
		ret = pinctrl_select_state(ov5670->pinctrl,
					   ov5670->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(ov5670->xvclk, OV5670_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(ov5670->xvclk) != OV5670_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(ov5670->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	ret = regulator_bulk_enable(OV5670_NUM_SUPPLIES, ov5670->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(ov5670->reset_gpio))
		gpiod_set_value_cansleep(ov5670->reset_gpio, 1);

	if (!IS_ERR(ov5670->pwdn_gpio))
		gpiod_set_value_cansleep(ov5670->pwdn_gpio, 1);

	/* export gpio */
	if (!IS_ERR(ov5670->reset_gpio))
		gpiod_export(ov5670->reset_gpio, false);
	if (!IS_ERR(ov5670->pwdn_gpio))
		gpiod_export(ov5670->pwdn_gpio, false);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = ov5670_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);
	usleep_range(10000, 20000);
	return 0;

disable_clk:
	clk_disable_unprepare(ov5670->xvclk);

	return ret;
}

static void __ov5670_power_off(struct ov5670 *ov5670)
{
	int ret;
	struct device *dev = &ov5670->client->dev;

	if (!IS_ERR(ov5670->pwdn_gpio))
		gpiod_set_value_cansleep(ov5670->pwdn_gpio, 0);
	clk_disable_unprepare(ov5670->xvclk);
	if (!IS_ERR(ov5670->reset_gpio))
		gpiod_set_value_cansleep(ov5670->reset_gpio, 0);
	if (!IS_ERR_OR_NULL(ov5670->pins_sleep)) {
		ret = pinctrl_select_state(ov5670->pinctrl,
					   ov5670->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	if (!IS_ERR(ov5670->power_gpio))
		gpiod_set_value_cansleep(ov5670->power_gpio, 0);

	regulator_bulk_disable(OV5670_NUM_SUPPLIES, ov5670->supplies);
}

static int ov5670_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov5670 *ov5670 = to_ov5670(sd);

	return __ov5670_power_on(ov5670);
}

static int ov5670_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov5670 *ov5670 = to_ov5670(sd);

	__ov5670_power_off(ov5670);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int ov5670_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct ov5670 *ov5670 = to_ov5670(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct ov5670_mode *def_mode = &supported_modes[0];

	mutex_lock(&ov5670->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = MEDIA_BUS_FMT_SBGGR10_1X10;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&ov5670->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int ov5670_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	struct ov5670 *ov5670 = to_ov5670(sd);

	if (fie->index >= ov5670->cfg_num)
		return -EINVAL;

	if (fie->code != MEDIA_BUS_FMT_SBGGR10_1X10)
		return -EINVAL;

	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	return 0;
}

static int ov5670_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *config)
{
	u32 val = 0;

	val = 1 << (OV5670_LANES - 1) |
	      V4L2_MBUS_CSI2_CHANNEL_0 |
	      V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	config->type = V4L2_MBUS_CSI2;
	config->flags = val;

	return 0;
}

static const struct dev_pm_ops ov5670_pm_ops = {
	SET_RUNTIME_PM_OPS(ov5670_runtime_suspend,
			   ov5670_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops ov5670_internal_ops = {
	.open = ov5670_open,
};
#endif

static const struct v4l2_subdev_core_ops ov5670_core_ops = {
	.s_power = ov5670_s_power,
	.ioctl = ov5670_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = ov5670_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops ov5670_video_ops = {
	.s_stream = ov5670_s_stream,
	.g_frame_interval = ov5670_g_frame_interval,
	.g_mbus_config = ov5670_g_mbus_config,
};

static const struct v4l2_subdev_pad_ops ov5670_pad_ops = {
	.enum_mbus_code = ov5670_enum_mbus_code,
	.enum_frame_size = ov5670_enum_frame_sizes,
	.enum_frame_interval = ov5670_enum_frame_interval,
	.get_fmt = ov5670_get_fmt,
	.set_fmt = ov5670_set_fmt,
};

static const struct v4l2_subdev_ops ov5670_subdev_ops = {
	.core	= &ov5670_core_ops,
	.video	= &ov5670_video_ops,
	.pad	= &ov5670_pad_ops,
};

static int ov5670_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov5670 *ov5670 = container_of(ctrl->handler,
					     struct ov5670, ctrl_handler);
	struct i2c_client *client = ov5670->client;
	s64 max;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = ov5670->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(ov5670->exposure,
					 ov5670->exposure->minimum, max,
					 ov5670->exposure->step,
					 ov5670->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */
		/*group 0*/
		ret = ov5670_write_reg(ov5670->client, OV5670_REG_GROUP,
					   OV5670_REG_VALUE_08BIT, 0x00);
		ret = ov5670_write_reg(ov5670->client, OV5670_REG_EXPOSURE,
				       OV5670_REG_VALUE_24BIT, ctrl->val << 4);
		ret = ov5670_write_reg(ov5670->client, OV5670_REG_GROUP,
					   OV5670_REG_VALUE_08BIT, 0x10);
		ret = ov5670_write_reg(ov5670->client, OV5670_REG_GROUP,
					   OV5670_REG_VALUE_08BIT, 0xa0);

		break;
	case V4L2_CID_ANALOGUE_GAIN:
		/*group 1*/
		ret = ov5670_write_reg(ov5670->client, OV5670_REG_GROUP,
					   OV5670_REG_VALUE_08BIT, 0x01);

		ret = ov5670_write_reg(ov5670->client, OV5670_REG_GAIN_L,
				       OV5670_REG_VALUE_08BIT,
				       ctrl->val & OV5670_GAIN_L_MASK);
		ret |= ov5670_write_reg(ov5670->client, OV5670_REG_GAIN_H,
				       OV5670_REG_VALUE_08BIT,
				       (ctrl->val >> OV5670_GAIN_H_SHIFT) &
				       OV5670_GAIN_H_MASK);
		ret = ov5670_write_reg(ov5670->client, OV5670_REG_GROUP,
					   OV5670_REG_VALUE_08BIT, 0x11);
		ret = ov5670_write_reg(ov5670->client, OV5670_REG_GROUP,
					   OV5670_REG_VALUE_08BIT, 0xa1);
		break;
	case V4L2_CID_VBLANK:

		ret = ov5670_write_reg(ov5670->client, OV5670_REG_VTS,
				       OV5670_REG_VALUE_16BIT,
				       ctrl->val + ov5670->cur_mode->height);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = ov5670_enable_test_pattern(ov5670, ctrl->val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ov5670_ctrl_ops = {
	.s_ctrl = ov5670_set_ctrl,
};

static int ov5670_initialize_controls(struct ov5670 *ov5670)
{
	const struct ov5670_mode *mode;
	struct v4l2_ctrl_handler *handler;
	struct v4l2_ctrl *ctrl;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &ov5670->ctrl_handler;
	mode = ov5670->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &ov5670->mutex;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
				      0, 0, link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, ov5670->pixel_rate, 1, ov5670->pixel_rate);

	h_blank = mode->hts_def - mode->width;
	ov5670->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (ov5670->hblank)
		ov5670->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	ov5670->vblank = v4l2_ctrl_new_std(handler, &ov5670_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				OV5670_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def - 4;
	ov5670->exposure = v4l2_ctrl_new_std(handler, &ov5670_ctrl_ops,
				V4L2_CID_EXPOSURE, OV5670_EXPOSURE_MIN,
				exposure_max, OV5670_EXPOSURE_STEP,
				mode->exp_def);

	ov5670->anal_gain = v4l2_ctrl_new_std(handler, &ov5670_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, ANALOG_GAIN_MIN,
				ANALOG_GAIN_MAX, ANALOG_GAIN_STEP,
				ANALOG_GAIN_DEFAULT);

	ov5670->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
				&ov5670_ctrl_ops, V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(ov5670_test_pattern_menu) - 1,
				0, 0, ov5670_test_pattern_menu);

	if (handler->error) {
		ret = handler->error;
		dev_err(&ov5670->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	ov5670->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int ov5670_otp_read(struct ov5670 *ov5670)
{
	int otp_flag, addr, temp, i;
	struct ov5670_otp_info *otp_ptr;
	struct device *dev = &ov5670->client->dev;
	struct i2c_client *client = ov5670->client;

	otp_ptr = devm_kzalloc(dev, sizeof(*otp_ptr), GFP_KERNEL);
	if (!otp_ptr)
		return -ENOMEM;

	otp_flag = 0;
	ov5670_read_1byte(client, 0x7010, &otp_flag);
	if ((otp_flag & 0xc0) == 0x40)
		addr = 0x7011; /* base address of info group 1 */
	else if ((otp_flag & 0x30) == 0x10)
		addr = 0x7016; /* base address of info group 2 */
	else if ((otp_flag & 0x0c) == 0x04)
		addr = 0x701b; /* base address of info group 3 */
	else
		addr = 0;

	if (addr != 0) {
		otp_ptr->flag = 0x80; /* valid info in OTP */
		ov5670_read_1byte(client, addr, &otp_ptr->module_id);
		ov5670_read_1byte(client, addr + 1, &otp_ptr->lens_id);
		ov5670_read_1byte(client, addr + 2, &otp_ptr->year);
		ov5670_read_1byte(client, addr + 3, &otp_ptr->month);
		ov5670_read_1byte(client, addr + 4, &otp_ptr->day);
		dev_info(dev, "fac info: module(0x%x) lens(0x%x) time(%d_%d_%d)!\n",
			otp_ptr->module_id,
			otp_ptr->lens_id,
			otp_ptr->year,
			otp_ptr->month,
			otp_ptr->day);
	} else {
		otp_ptr->flag = 0x00; /* not info in OTP */
		otp_ptr->module_id = 0x00;
		otp_ptr->lens_id = 0x00;
		otp_ptr->year = 0x00;
		otp_ptr->month = 0x00;
		otp_ptr->day = 0x00;
		dev_warn(dev, "fac info: module(0x%x) lens(0x%x) time(%d_%d_%d)!\n",
			otp_ptr->module_id,
			otp_ptr->lens_id,
			otp_ptr->year,
			otp_ptr->month,
			otp_ptr->day);
	}

	/* OTP base information and WB calibration data */
	ov5670_read_1byte(client, 0x7020, &otp_flag);
	if ((otp_flag & 0xc0) == 0x40)
		addr = 0x7021; /* base address of info group 1 */
	else if ((otp_flag & 0x30) == 0x10)
		addr = 0x7024; /* base address of info group 2 */
	else if ((otp_flag & 0x0c) == 0x04)
		addr = 0x7027; /* base address of info group 3 */
	else
		addr = 0;

	if (addr != 0) {
		otp_ptr->flag |= 0x40; /* valid info and AWB in OTP */
		ov5670_read_1byte(client, addr + 2, &temp);
		ov5670_read_1byte(client, addr, &otp_ptr->rg_ratio);
		otp_ptr->rg_ratio = (otp_ptr->rg_ratio << 2) +
				    ((temp >> 6) & 0x03);
		ov5670_read_1byte(client, addr + 1, &otp_ptr->bg_ratio);
		otp_ptr->bg_ratio = (otp_ptr->bg_ratio << 2) +
				    ((temp >> 4) & 0x03);
		dev_info(dev, "awb info: (0x%x, 0x%x)!\n",
			otp_ptr->rg_ratio, otp_ptr->bg_ratio);
	} else {
		otp_ptr->rg_ratio = 0x00;
		otp_ptr->bg_ratio = 0x00;
		dev_warn(dev, "awb info: (0x%x, 0x%x)!\n",
			otp_ptr->rg_ratio, otp_ptr->bg_ratio);
	}

	for (i = 0x7010; i <= 0x7029; i++)
		ov5670_write_1byte(client, i, 0); /* clear OTP buffer */

	if (otp_ptr->flag) {
		ov5670->otp = otp_ptr;
	} else {
		ov5670->otp = NULL;
		dev_info(dev, "otp is null!\n");
		devm_kfree(dev, otp_ptr);
	}

	return 0;
}

static int ov5670_otp_check_read(struct ov5670 *ov5670)
{
	int temp = 0;
	int ret = 0;
	struct i2c_client *client = ov5670->client;

	/* stream on  */
	ov5670_write_1byte(client,
			   OV5670_REG_CTRL_MODE,
			   OV5670_MODE_STREAMING);

	ov5670_read_1byte(client, 0x5002, &temp);
	ov5670_write_1byte(client, 0x5002, (temp & (~0x08)));

	/* read OTP into buffer */
	ov5670_write_1byte(client, 0x3d84, 0xC0);
	ov5670_write_1byte(client, 0x3d88, 0x70); /* OTP start address */
	ov5670_write_1byte(client, 0x3d89, 0x10);
	ov5670_write_1byte(client, 0x3d8A, 0x70); /* OTP end address */
	ov5670_write_1byte(client, 0x3d8B, 0x29);
	ov5670_write_1byte(client, 0x3d81, 0x01); /* load otp into buffer */
	usleep_range(10000, 20000);

	ret = ov5670_otp_read(ov5670);

	/* set 0x5002[3] to "1" */
	ov5670_read_1byte(client, 0x5002, &temp);
	ov5670_write_1byte(client, 0x5002, 0x08 | (temp & (~0x08)));

	/* stream off */
	ov5670_write_1byte(client,
			   OV5670_REG_CTRL_MODE,
			   OV5670_MODE_SW_STANDBY);

	return ret;
}

static int ov5670_check_sensor_id(struct ov5670 *ov5670,
				  struct i2c_client *client)
{
	struct device *dev = &ov5670->client->dev;
	u32 id = 0;
	int ret;

	ret = ov5670_read_reg(client, OV5670_REG_CHIP_ID,
			      OV5670_REG_VALUE_16BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected OV%06x sensor\n", CHIP_ID);

	return 0;
}

static int ov5670_configure_regulators(struct ov5670 *ov5670)
{
	unsigned int i;

	for (i = 0; i < OV5670_NUM_SUPPLIES; i++)
		ov5670->supplies[i].supply = ov5670_supply_names[i];

	return devm_regulator_bulk_get(&ov5670->client->dev,
				       OV5670_NUM_SUPPLIES,
				       ov5670->supplies);
}

static int ov5670_parse_of(struct ov5670 *ov5670)
{
	struct device *dev = &ov5670->client->dev;
	struct device_node *endpoint;
	struct fwnode_handle *fwnode;
	int rval;

	endpoint = of_graph_get_next_endpoint(dev->of_node, NULL);
	if (!endpoint) {
		dev_err(dev, "Failed to get endpoint\n");
		return -EINVAL;
	}
	fwnode = of_fwnode_handle(endpoint);
	rval = fwnode_property_read_u32_array(fwnode, "data-lanes", NULL, 0);
	if (rval <= 0) {
		dev_warn(dev, " Get mipi lane num failed!\n");
		return -1;
	}

	ov5670->lane_num = rval;
	if (2 == ov5670->lane_num) {
		ov5670->cur_mode = &supported_modes_2lane[0];
		supported_modes = supported_modes_2lane;
		ov5670->cfg_num = ARRAY_SIZE(supported_modes_2lane);

		/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
		ov5670->pixel_rate = MIPI_FREQ * 2U * ov5670->lane_num / 8U;
		dev_info(dev, "lane_num(%d)  pixel_rate(%u)\n",
				 ov5670->lane_num, ov5670->pixel_rate);
	} else {
		dev_err(dev, "unsupported lane_num(%d)\n", ov5670->lane_num);
		return -1;
	}

	return 0;
}

static int ov5670_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct ov5670 *ov5670;
	struct v4l2_subdev *sd;
	char facing[2] = "b";
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	ov5670 = devm_kzalloc(dev, sizeof(*ov5670), GFP_KERNEL);
	if (!ov5670)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &ov5670->module_index);
	if (ret) {
		dev_warn(dev, "could not get module index!\n");
		ov5670->module_index = 0;
	}
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &ov5670->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &ov5670->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &ov5670->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	ov5670->client = client;

	ov5670->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(ov5670->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	ov5670->power_gpio = devm_gpiod_get(dev, "power", GPIOD_OUT_LOW);
	if (IS_ERR(ov5670->power_gpio))
		dev_warn(dev, "Failed to get power-gpios, maybe no use\n");

	ov5670->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ov5670->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios, maybe no use\n");

	ov5670->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(ov5670->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ret = ov5670_configure_regulators(ov5670);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}
	ret = ov5670_parse_of(ov5670);
	if (ret != 0)
		return -EINVAL;

	ov5670->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(ov5670->pinctrl)) {
		ov5670->pins_default =
			pinctrl_lookup_state(ov5670->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(ov5670->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		ov5670->pins_sleep =
			pinctrl_lookup_state(ov5670->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(ov5670->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	}

	mutex_init(&ov5670->mutex);

	sd = &ov5670->subdev;
	v4l2_i2c_subdev_init(sd, client, &ov5670_subdev_ops);
	ret = ov5670_initialize_controls(ov5670);
	if (ret)
		goto err_destroy_mutex;

	ret = __ov5670_power_on(ov5670);
	if (ret)
		goto err_free_handler;

	ret = ov5670_check_sensor_id(ov5670, client);
	if (ret < 0) {
		dev_info(&client->dev, "%s(%d) Check id  failed\n"
				  "check following information:\n"
				  "Power/PowerDown/Reset/Mclk/I2cBus !!\n",
				  __func__, __LINE__);
		goto err_power_off;
	}
	ov5670_otp_check_read(ov5670);

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &ov5670_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	ov5670->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &ov5670->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(ov5670->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 ov5670->module_index, facing,
		 OV5670_NAME, dev_name(sd->dev));

	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_clean_entity;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	return 0;

err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
err_power_off:
	__ov5670_power_off(ov5670);
err_free_handler:
	v4l2_ctrl_handler_free(&ov5670->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&ov5670->mutex);

	return ret;
}

static int ov5670_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov5670 *ov5670 = to_ov5670(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&ov5670->ctrl_handler);
	mutex_destroy(&ov5670->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__ov5670_power_off(ov5670);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id ov5670_of_match[] = {
	{ .compatible = "ovti,ov5670" },
	{},
};
MODULE_DEVICE_TABLE(of, ov5670_of_match);
#endif

static const struct i2c_device_id ov5670_match_id[] = {
	{ "ovti,ov5670", 0 },
	{ },
};

static struct i2c_driver ov5670_i2c_driver = {
	.driver = {
		.name = OV5670_NAME,
		.pm = &ov5670_pm_ops,
		.of_match_table = of_match_ptr(ov5670_of_match),
	},
	.probe		= &ov5670_probe,
	.remove		= &ov5670_remove,
	.id_table	= ov5670_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&ov5670_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&ov5670_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("OmniVision ov5670 sensor driver");
MODULE_LICENSE("GPL v2");
