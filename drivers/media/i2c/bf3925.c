// SPDX-License-Identifier: GPL-2.0
/*
 * BF3925 CMOS Image Sensor driver
 *
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd.
 * V0.0X01.0X01 add enum_frame_interval function.
 * V0.0X01.0X02 add quick stream on/off
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/media.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/of_gpio.h>

#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/videodev2.h>
#include <linux/rk-camera-module.h>
#include <linux/version.h>
#include <media/media-entity.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-image-sizes.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x2)
#define DRIVER_NAME "bf3925"
#define BF3925_PIXEL_RATE		(120 * 1000 * 1000)

/*
 * BF3925 register definitions
 */
#define REG_SOFTWARE_STANDBY		0xf2

#define REG_SC_CHIP_ID_H		0xfc
#define REG_SC_CHIP_ID_L		0xfd

#define REG_NULL			0xFFFF	/* Array end token */

#define SENSOR_ID(_msb, _lsb)		((_msb) << 8 | (_lsb))
#define BF3925_ID			0x3925

struct sensor_register {
	u16 addr;
	u8 value;
};

struct bf3925_framesize {
	u16 width;
	u16 height;
	struct v4l2_fract max_fps;
	u16 max_exp_lines;
	const struct sensor_register *regs;
};

struct bf3925_pll_ctrl {
	u8 ctrl1;
	u8 ctrl2;
	u8 ctrl3;
};

struct bf3925_pixfmt {
	u32 code;
	/* Output format Register Value (REG_FORMAT_CTRL00) */
	struct sensor_register *format_ctrl_regs;
};

struct pll_ctrl_reg {
	unsigned int div;
	unsigned char reg;
};

static const char * const bf3925_supply_names[] = {
	"dovdd",	/* Digital I/O power */
	"avdd",		/* Analog power */
	"dvdd",		/* Digital core power */
};

#define BF3925_NUM_SUPPLIES ARRAY_SIZE(bf3925_supply_names)

struct bf3925 {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_mbus_framefmt format;
	unsigned int fps;
	unsigned int xvclk_frequency;
	struct clk *xvclk;
	struct gpio_desc *power_gpio;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *pwdn_gpio;
	struct gpio_desc *pwdn2_gpio;
	struct regulator_bulk_data supplies[BF3925_NUM_SUPPLIES];
	struct mutex lock;
	struct i2c_client *client;
	struct v4l2_ctrl_handler ctrls;
	struct v4l2_ctrl *link_frequency;
	const struct bf3925_framesize *frame_size;
	int streaming;
	u32 module_index;
	const char *module_facing;
	const char *module_name;
	const char *len_name;
};

static const struct sensor_register bf3925_init_regs[] = {
	{0xff, 0x01}, //Bit[0]: select reg page
	{0xff, 0x01}, //Bit[0]: select reg page
	{0x50, 0x00}, //bit[4]: digital subsample Data format selection
	{0x51, 0x02}, //YUV Sequence
	{0xe0, 0x00},
	{0xe2, 0x64},
	{0xe3, 0x48},
	{0xe4, 0x83}, //Drive capability //0x81 ljx
	{0xe7, 0x9b},

  //clock, dummy
	{0xff, 0x01},  //Bit[0]: select reg page
	{0xe9, 0x2a},  //08 PLL setting
	{0xff, 0x00},  //Bit[0]: select reg page
	{0x01, 0x00},
	{0x02, 0x90},  //Dummy Pixel Insert LSB
	{0x03, 0x00},  ///02 //yang
	{0x04, 0x00},  //Dummy line Insert LSB
	{0xff, 0x01},
	{0xe5, 0x32},
	//init black
	{0xff, 0x00},
	{0x3d, 0x00},
	{0x30, 0x61},
	{0x31, 0x63},
	{0x32, 0x60},
	{0x33, 0x63},

	//resolution
	{0xff, 0x00},
	{0x05, 0xa2}, ///a0
	{0x09, 0x90}, ///00
	{0x0a, 0x48},
	{0x0b, 0x60},
	{0x0c, 0x00},
	{0x0d, 0xb8},
	{0x0e, 0x40},
	{0xff, 0x01},
	{0x52, 0x01}, //Bit[1]: VSYNC option   Bit[0]: HSYNC option
	{0x5d, 0x02},
	{0x5a, 0x00},
	{0x5b, 0x00},
	{0x5c, 0x00},
	{0xff, 0x01},
	{0x53, 0x30}, ///60
	{0x54, 0x20}, ///40
	{0x55, 0x00},
	{0x56, 0x20}, ///40
	{0x57, 0x00},
	{0x58, 0x58}, ///b0
	{0xff, 0x01},
	{0x50, 0x00}, //bit[4]: digital subsample Data format selection

	//initial AWB and AE
	{0xff, 0x00},  //Bit[0]: select reg page
	{0xb2, 0x81},  //Manual AWB & AE
	{0xb0, 0x16},
	{0xb1, 0x1d},
	{0xb2, 0x89},
	{0xff, 0x01},
	{0x00, 0x00},
	{0x0e, 0x0a},
	{0x0f, 0x64},
	{0x10, 0x28},
	{0x00, 0x05},

	//black control
	{0xff, 0x00},
	{0x3c, 0x97},

	//black sun
	{0xff, 0x01}, //Bit[0]: select reg page
	{0xe1, 0xf8}, //28 bit[7:4]: Pixel bias current
	{0xff, 0x00}, //Bit[0]: select reg page
	{0x00, 0x47}, //bit[6]: black sun control bit[5:4]: mirror/flip
	{0x18, 0x0c}, //PRST indoor
	{0x19, 0x1a}, //PRST outdoor

	//lens shading
	{0xff, 0x00},  //Bit[0]: select reg page
	{0x52, 0x13},
	{0x53, 0x5c},
	{0x54, 0x24},
	{0x55, 0x13},
	{0x56, 0x5c},
	{0x57, 0x24},
	{0x58, 0xd3},
	{0x59, 0x5c},
	{0x5a, 0x24},
	{0x5b, 0x44}, ///46 lens shading gain of R
	{0x5c, 0x3C}, ///43 lens shading gain of G1
	{0x5d, 0x40}, //lens shading gain of B
	{0x5e, 0x3C}, /// 43lens shading gain of G0

#if 0
	/*gamma default */
	{0xff, 0x00}, //Bit[0]: select reg page
	{0x60, 0x30},
	{0x61, 0x2a},
	{0x62, 0x24},
	{0x63, 0x1b},
	{0x64, 0x18},
	{0x65, 0x16},
	{0x66, 0x14},
	{0x67, 0x12},
	{0x68, 0x10},
	{0x69, 0x0e},
	{0x6a, 0x0d},
	{0x6b, 0x0c},
	{0x6c, 0x0a},
	{0x6d, 0x09},
	{0x6e, 0x09},
#endif
	{0x6f, 0xf0},
	{0x70, 0x20},
	{0x71, 0x60},
	{0x72, 0x24},///10
	{0x73, 0x24},///10
#if 1
	//gamma hi-lit,nice over-ex.
	{0xff, 0x00},  //Bit[0]: select reg page
	{0x60, 0x33},
	{0x61, 0x2b},
	{0x62, 0x27},
	{0x63, 0x22},
	{0x64, 0x1b},
	{0x65, 0x17},
	{0x66, 0x14},
	{0x67, 0x11},
	{0x68, 0x0e},
	{0x69, 0x0c},
	{0x6a, 0x0b},
	{0x6b, 0x0a},
	{0x6c, 0x09},
	{0x6d, 0x08},
	{0x6e, 0x07},
#else
	//gamma  nice color
	{0xff, 0x00}, //Bit[0]: select reg page
	{0x60, 0x28},
	{0x61, 0x28},
	{0x62, 0x26},
	{0x63, 0x22},
	{0x64, 0x1f},
	{0x65, 0x1c},
	{0x66, 0x18},
	{0x67, 0x13},
	{0x68, 0x10},
	{0x69, 0x0d},
	{0x6a, 0x0c},
	{0x6b, 0x0a},
	{0x6c, 0x08},
	{0x6d, 0x07},
	{0x6e, 0x06},

	///gamma low denoise
	{0xff, 0x00}, //Bit[0]: select reg page
	{0x60, 0x24},
	{0x61, 0x30},
	{0x62, 0x20},
	{0x63, 0x1a},
	{0x64, 0x16},
	{0x65, 0x13},
	{0x66, 0x11},
	{0x67, 0x0e},
	{0x68, 0x0d},
	{0x69, 0x0c},
	{0x6a, 0x0b},
	{0x6b, 0x09},
	{0x6c, 0x09},
	{0x6d, 0x08},
	{0x6e, 0x07},
#endif

#if 1
	//clearer
	//denoise and edge enhancement
	{0xff, 0x00}, //Bit[0]: select reg page
	{0x80, 0x0f},
	{0x81, 0x1e},
	{0x83, 0x37}, //0x83[7:4]: de_noise threshhole; 0x83[3:0]: de_noise
	{0x84, 0xe6},
	{0x85, 0x00},
	{0x86, 0xfc},
	{0x87, 0x00},
	{0x88, 0xa2},  //bit[7:6] 0 is low noise;
	{0x89, 0xca},
	{0x8a, 0x44},
	{0x8b, 0x12},
	{0x91, 0x48},
	{0x92, 0x11},
	{0x93, 0x0c},
#else
	//denoise and edge enhancement
	{0xff, 0x00}, //Bit[0]: select reg page
	{0x80, 0x0f},
	{0x81, 0x0c},
	{0x83, 0x27}, //0x83[7:4]: de_noise threshhole; 0x83[3:0]: de_noise
	{0x84, 0xe6},
	{0x85, 0x88},
	{0x86, 0xfa},
	{0x87, 0x1a},
	{0x88, 0xa2}, //bit[7:6] 0 is low noise;
	{0x89, 0xca},
	{0x8b, 0x11}, //12 Bright/Dark edge enhancement
	{0x91, 0x48}, //45 0x91:40
#endif

	//AWB
	{0xff, 0x00}, //Bit[0]: select reg page
	{0xa2, 0x06},  //the low limit of blue gain for indoor scene
	{0xa3, 0x28},  //the upper limit of blue gain for indoor scene
	{0xa4, 0x0a},  //the low limit of red gain for indoor scene
	{0xa5, 0x2c},  //the upper limit of red gain for indoor scene
	{0xa7, 0x1b},  //Base B gain
	{0xa8, 0x14},  //Base R gain
	{0xa9, 0x15},
	{0xaa, 0x18},
	{0xab, 0x26},
	{0xac, 0x5c},
	{0xae, 0x47},
	{0xb2, 0x89},
	{0xb3, 0x66},  // green gain
	{0xb4, 0x03},  //the offset of F light
	{0xb5, 0x00},  //the offset of non-F light
	{0xb6, 0xd9},  //bit[7]: outdoor control
	{0xb8, 0xca},
	{0xbb, 0x0d},
	{0xbc, 0x15},
	{0xbd, 0x09},
	{0xbe, 0x24},
	{0xbf, 0x66},

#if 1
	// color default
	{0xff, 0x00}, //Bit[0]: select reg page
	{0xc0, 0x8a},
	{0xc1, 0x05},
	{0xc2, 0x84},
	{0xc3, 0x86},
	{0xc4, 0x03},
	{0xc5, 0x93},

#else
	//color Gorgeous
	{0xff, 0x00}, //Bit[0]: select reg page
	{0xc0, 0x83},
	{0xc1, 0x86},
	{0xc2, 0x82},
	{0xc3, 0x8a},
	{0xc4, 0x07},
	{0xc5, 0x9f},

	//color light
	{0xff, 0x00}, //Bit[0]: select reg page
	{0xc0, 0x83},
	{0xc1, 0x02},
	{0xc2, 0x84},
	{0xc3, 0x84},
	{0xc4, 0x03},
	{0xc5, 0x8d},
#endif

	// A color
	{0xff, 0x00}, //Bit[0]: select reg page
	{0xc6, 0x8a},
	{0xc7, 0x82},
	{0xc8, 0x8b},
	{0xc9, 0x87},
	{0xca, 0x83},
	{0xcb, 0x91},

	//Outdoor color
	{0xff, 0x00}, //Bit[0]: select reg page
	{0xd0, 0x90},
	{0xd1, 0x05},
	{0xd2, 0x82},
	{0xd3, 0x88},
	{0xd4, 0x03},
	{0xd5, 0x93},
	{0xcd, 0x30},
	{0xd6, 0x61},

	//AE
	{0xff, 0x01}, //Bit[0]: select reg page
	{0x00, 0x05},
	{0x01, 0x8a}, // AE window and weight
	{0x04, 0x48}, //4f AE Target//40
	{0x05, 0x48}, //4f Y target value1//48
	{0x07, 0x92}, //Bit[3:2]: the bigger, Y_AVER_MODIFY is smaller
	{0x09, 0x8a}, //92 Bit[5:0]: INT_MAX//8c
	{0x0a, 0xa5},
	{0x0b, 0x82}, //Bit[5:0]: INT_MIN
	{0x0c, 0xb4}, //78 50hz banding
	{0x0d, 0x96}, //64 60hz banding
	{0x15, 0x02}, //AEC
	{0x16, 0x8c},
	{0x17, 0xb5},
	{0x18, 0x50},  ///30
	{0x1b, 0x30}, ///33 minimum global gain
	{0x1c, 0x58}, ///66
	{0x1d, 0x38}, ///55
	{0x1e, 0x58}, ///80
	{0x1f, 0x60}, /// c0 maximum gain//a0

	// saturation
	{0xff, 0x01}, //Bit[0]: select reg page
	{0x30, 0xff}, ///e0
	{0x31, 0x48},
	{0x32, 0x60}, ///f0
	{0x34, 0xd8}, ///da Cb Saturation Coefficient low 8 bit for NF light
	{0x35, 0xc8}, ///ca Cr Saturation Coefficient low 8 bit for NF light
	{0x36, 0xff}, //Cb Saturation Coefficient low 8 bit for F light
	{0x37, 0xd0}, //Cr Saturation Coefficient low 8 bit for F light

	//skin
	{0xff, 0x01}, //Bit[0]: select reg page
	{0x3b, 0x08},

	// auto contrast
	{0xff, 0x01}, //Bit[0]: select reg page
	{0x3e, 0x02}, //
	{0x3e, 0x82}, //do not change
	{0x38, 0x40},
//yang add start switch to 1600*1200 UXGA
//1600*1200
//window
//yang add end switch to 1600*1200 UXGA
	{REG_NULL, 0x00},
};

/* Senor full resolution setting */
static const struct sensor_register bf3925_full_regs[] = {
	//1600*1200
	//window
	{0xff, 0x00},
	{0x05, 0xa0},
	{0x09, 0x00},
	{0x0a, 0x48},
	{0x0b, 0x60},
	{0x0c, 0x00},
	{0x0d, 0xb8},
	{0x0e, 0x40},
	{0xff, 0x01},
	{0x52, 0x01},  //Bit[1]: VSYNC option   Bit[0]: HSYNC option
	{0x5d, 0x02},
	{0x5a, 0x00},
	{0x5b, 0x00},
	{0x5c, 0x00},
	{0xff, 0x01},
	{0x53, 0x60},
	{0x54, 0x40},
	{0x55, 0x00},
	{0x56, 0x40},
	{0x57, 0x00},
	{0x58, 0xb0},
	{0xff, 0x01},
	{0x50, 0x00},

	 //clock, dummy
	{0xff, 0x01}, //Bit[0]: select reg page
	{0x09, 0x86},
	{0xe9, 0x2a}, //08 PLL setting
	{0xff, 0x00}, //Bit[0]: select reg page
	{0x01, 0x00},
	{0x02, 0x00}, //Dummy Pixel Insert LSB
	{0x03, 0x00}, ///02 //yang
	{0x04, 0x00}, //Dummy line Insert LSB

	{REG_NULL, 0x00},
};

/* Preview resolution setting*/
static const struct sensor_register bf3925_svga_regs_15fps[] = {
	//800*600
	//window
	{0xff, 0x00},
	{0x05, 0xa0},
	{0x09, 0x00},
	{0x0a, 0x48},
	{0x0b, 0x60},
	{0x0c, 0x00},
	{0x0d, 0xb8},
	{0x0e, 0x40},
	{0xff, 0x01},
	{0x52, 0x01},  //Bit[1]: VSYNC option   Bit[0]: HSYNC option
	{0x5d, 0x02},
	{0x5a, 0x00},
	{0x5b, 0x00},
	{0x5c, 0x00},
	{0xff, 0x01},
	{0x53, 0x30},
	{0x54, 0x20},
	{0x55, 0x02},
	{0x56, 0x22},
	{0x57, 0x01},
	{0x58, 0x59},
	{0xff, 0x01},
	{0x50, 0x00},

	 //clock, dummy
	{0xff, 0x01}, //Bit[0]: select reg page
	{0x09, 0x86},
	{0xe9, 0x2a}, //08 PLL setting
	{0xff, 0x00}, //Bit[0]: select reg page
	{0x01, 0x00},
	{0x02, 0x00}, //Dummy Pixel Insert LSB
	{0x03, 0x00}, ///02 //yang
	{0x04, 0x00}, //Dummy line Insert LSB

	{REG_NULL, 0x00},
};

/* Preview resolution setting*/
static const struct sensor_register bf3925_svga_regs_30fps[] = {
	 //800*600
	{0xff, 0x00},
	{0x05, 0xa2},
	{0x09, 0x04},

	{0x0a, 0x4c},
	{0x0b, 0x60},
	{0x0c, 0x04},
	{0x0d, 0xbc},
	{0x0e, 0x40},
	{0xff, 0x01},
	{0x52, 0x01},
	{0x5d, 0x02},
	{0x5a, 0x00},
	{0x5b, 0x00},
	{0x5c, 0x00},
	{0xff, 0x01},
	{0x09, 0x83},
	{0x53, 0x30},
	{0x54, 0x20},
	{0x55, 0x02},
	{0x56, 0x22},
	{0x57, 0x01},
	{0x58, 0x59},
	{0xff, 0x01},
	{0x50, 0x00},
	{0xe9, 0x2a},
	//clock, dummy
	{0xff, 0x01},	//Bit[0]: select reg page
	{0x09, 0x83},

	/* 08 PLL setting   0x09: 1 times
	 * 0x1b: multiply 5/4 0x2b: 3/2 multiply
	 * 0x08:double  0x1a: 5/2 multiply
	 * 0x2a: triple 0x2a ljx 2017-6
	 */
	{0xe9, 0x08},
	{0xff, 0x00},	//Bit[0]: select reg page
	{0x01, 0x00},
	{0x02, 0xea},	//Dummy Pixel Insert LSB
	{0x03, 0x00},	///02 //yang
	{0x04, 0x00},	//Dummy line Insert LSB

	{REG_NULL, 0x00},
};

static const struct bf3925_framesize bf3925_framesizes[] = {
	{ /* SVGA */
		.width		= 800,
		.height		= 600,
		.max_fps = {
			.numerator = 10000,
			.denominator = 150000,
		},
		.regs		= bf3925_svga_regs_15fps,
	}, { /* SVGA */
		.width		= 800,
		.height		= 600,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.regs		= bf3925_svga_regs_30fps,
	}, { /* FULL */
		.width		= 1600,
		.height		= 1200,
		.max_fps = {
			.numerator = 10000,
			.denominator = 150000,
		},
		.regs		= bf3925_full_regs,
	}
};

static const struct bf3925_pixfmt bf3925_formats[] = {
	{
		.code = MEDIA_BUS_FMT_UYVY8_2X8,
	}
};

static inline struct bf3925 *to_bf3925(struct v4l2_subdev *sd)
{
	return container_of(sd, struct bf3925, sd);
}

/* sensor register write */
static int bf3925_write(struct i2c_client *client, u8 reg, u8 val)
{
	struct i2c_msg msg;
	u8 buf[2];
	int ret;

	dev_dbg(&client->dev, "write reg(0x%x val:0x%x)!\n", reg, val);
	buf[0] = reg & 0xFF;
	buf[1] = val;

	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.buf = buf;
	msg.len = sizeof(buf);

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret >= 0)
		return 0;

	dev_err(&client->dev,
		"bf3925 write reg(0x%x val:0x%x) failed !\n", reg, val);

	return ret;
}

/* sensor register read */
static int bf3925_read(struct i2c_client *client, u8 reg, u8 *val)
{
	struct i2c_msg msg[2];
	u8 buf[1];
	int ret;

	buf[0] = reg & 0xFF;

	msg[0].addr = client->addr;
	msg[0].flags = client->flags;
	msg[0].buf = buf;
	msg[0].len = sizeof(buf);

	msg[1].addr = client->addr;
	msg[1].flags = client->flags | I2C_M_RD;
	msg[1].buf = buf;
	msg[1].len = 1;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret >= 0) {
		*val = buf[0];
		return 0;
	}

	dev_err(&client->dev,
		"bf3925 read reg:0x%x failed !\n", reg);

	return ret;
}

static int bf3925_write_array(struct i2c_client *client,
			      const struct sensor_register *regs)
{
	int i, ret = 0;

	i = 0;
	while (regs[i].addr != REG_NULL) {
		ret = bf3925_write(client, regs[i].addr, regs[i].value);
		if (ret) {
			dev_err(&client->dev, "%s failed !\n", __func__);
			break;
		}

		i++;
	}

	return ret;
}

static void bf3925_get_default_format(struct v4l2_mbus_framefmt *format)
{
	format->width = bf3925_framesizes[0].width;
	format->height = bf3925_framesizes[0].height;
	format->colorspace = V4L2_COLORSPACE_SRGB;
	format->code = bf3925_formats[0].code;
	format->field = V4L2_FIELD_NONE;
}

static void bf3925_set_streaming(struct bf3925 *bf3925, int on)
{
	struct i2c_client *client = bf3925->client;
	int ret;

	dev_dbg(&client->dev, "%s: on: %d\n", __func__, on);

	ret = bf3925_write(client, REG_SOFTWARE_STANDBY, on);
	if (ret)
		dev_err(&client->dev, "bf3925 soft standby failed\n");
}

/*
 * V4L2 subdev video and pad level operations
 */

static int bf3925_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	dev_dbg(&client->dev, "%s:\n", __func__);

	if (code->index >= ARRAY_SIZE(bf3925_formats))
		return -EINVAL;

	code->code = bf3925_formats[code->index].code;

	return 0;
}

static int bf3925_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int i = ARRAY_SIZE(bf3925_formats);

	dev_dbg(&client->dev, "%s:\n", __func__);

	if (fse->index >= ARRAY_SIZE(bf3925_framesizes))
		return -EINVAL;

	while (--i)
		if (fse->code == bf3925_formats[i].code)
			break;

	fse->code = bf3925_formats[i].code;

	fse->min_width  = bf3925_framesizes[fse->index].width;
	fse->max_width  = fse->min_width;
	fse->max_height = bf3925_framesizes[fse->index].height;
	fse->min_height = fse->max_height;

	return 0;
}

static int bf3925_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct bf3925 *bf3925 = to_bf3925(sd);

	dev_dbg(&client->dev, "%s enter\n", __func__);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		struct v4l2_mbus_framefmt *mf;

		mf = v4l2_subdev_get_try_format(sd, cfg, 0);
		mutex_lock(&bf3925->lock);
		fmt->format = *mf;
		mutex_unlock(&bf3925->lock);
		return 0;
#else
	return -ENOTTY;
#endif
	}

	mutex_lock(&bf3925->lock);
	fmt->format = bf3925->format;
	mutex_unlock(&bf3925->lock);

	dev_dbg(&client->dev, "%s: %x %dx%d\n", __func__,
		bf3925->format.code, bf3925->format.width,
		bf3925->format.height);

	return 0;
}

static void __bf3925_try_frame_size_fps(struct v4l2_mbus_framefmt *mf,
					const struct bf3925_framesize **size,
					unsigned int fps)
{
	const struct bf3925_framesize *fsize = &bf3925_framesizes[0];
	const struct bf3925_framesize *match = NULL;
	unsigned int i = ARRAY_SIZE(bf3925_framesizes);
	unsigned int min_err = UINT_MAX;

	while (i--) {
		unsigned int err = abs(fsize->width - mf->width)
				+ abs(fsize->height - mf->height);
		if (err < min_err && fsize->regs[0].addr) {
			min_err = err;
			match = fsize;
		}
		fsize++;
	}

	if (!match) {
		match = &bf3925_framesizes[0];
	} else {
		fsize = &bf3925_framesizes[0];
		for (i = 0; i < ARRAY_SIZE(bf3925_framesizes); i++) {
			if (fsize->width == match->width &&
			    fsize->height == match->height &&
			    fps >= DIV_ROUND_CLOSEST(fsize->max_fps.denominator,
				fsize->max_fps.numerator))
				match = fsize;

			fsize++;
		}
	}

	mf->width  = match->width;
	mf->height = match->height;

	if (size)
		*size = match;
}

static int bf3925_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int index = ARRAY_SIZE(bf3925_formats);
	struct v4l2_mbus_framefmt *mf = &fmt->format;
	const struct bf3925_framesize *size = NULL;
	struct bf3925 *bf3925 = to_bf3925(sd);
	int ret = 0;

	dev_dbg(&client->dev, "%s enter\n", __func__);

	__bf3925_try_frame_size_fps(mf, &size, bf3925->fps);

	while (--index >= 0)
		if (bf3925_formats[index].code == mf->code)
			break;

	if (index < 0)
		return -EINVAL;

	mf->colorspace = V4L2_COLORSPACE_SRGB;
	mf->code = bf3925_formats[index].code;
	mf->field = V4L2_FIELD_NONE;

	mutex_lock(&bf3925->lock);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		mf = v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
		*mf = fmt->format;
#else
		return -ENOTTY;
#endif
	} else {
		if (bf3925->streaming) {
			mutex_unlock(&bf3925->lock);
			return -EBUSY;
		}

		bf3925->frame_size = size;
		bf3925->format = fmt->format;
	}

	mutex_unlock(&bf3925->lock);
	return ret;
}

static int bf3925_s_stream(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct bf3925 *bf3925 = to_bf3925(sd);
	int ret = 0;

	dev_info(&client->dev, "%s: on: %d, %dx%d\n", __func__, on,
		bf3925->frame_size->width,
		bf3925->frame_size->height);

	mutex_lock(&bf3925->lock);

	on = !!on;

	if (bf3925->streaming == on)
		goto unlock;

	if (!on) {
		/* Stop Streaming Sequence */
		bf3925_set_streaming(bf3925, 0x02);
		bf3925->streaming = on;
		goto unlock;
	}

	ret = bf3925_write_array(client, bf3925->frame_size->regs);
	if (ret)
		goto unlock;

	bf3925_set_streaming(bf3925, 0x00);
	bf3925->streaming = on;

unlock:
	mutex_unlock(&bf3925->lock);
	return ret;
}

static int bf3925_set_test_pattern(struct bf3925 *bf3925, int value)
{
	return 0;
}

static int bf3925_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct bf3925 *bf3925 =
			container_of(ctrl->handler, struct bf3925, ctrls);

	switch (ctrl->id) {
	case V4L2_CID_TEST_PATTERN:
		return bf3925_set_test_pattern(bf3925, ctrl->val);
	}

	return 0;
}

static const struct v4l2_ctrl_ops bf3925_ctrl_ops = {
	.s_ctrl = bf3925_s_ctrl,
};

static const char * const bf3925_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bars",
};

/* -----------------------------------------------------------------------------
 * V4L2 subdev internal operations
 */

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int bf3925_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);

	dev_dbg(&client->dev, "%s:\n", __func__);

	bf3925_get_default_format(format);

	return 0;
}
#endif

static int bf3925_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *config)
{
	config->type = V4L2_MBUS_PARALLEL;
	config->flags = V4L2_MBUS_HSYNC_ACTIVE_HIGH |
			V4L2_MBUS_VSYNC_ACTIVE_LOW |
			V4L2_MBUS_PCLK_SAMPLE_RISING;

	return 0;
}

static int bf3925_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct bf3925 *bf3925 = to_bf3925(sd);

	mutex_lock(&bf3925->lock);
	fi->interval = bf3925->frame_size->max_fps;
	mutex_unlock(&bf3925->lock);

	return 0;
}

static int bf3925_s_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct bf3925 *bf3925 = to_bf3925(sd);
	const struct bf3925_framesize *size = NULL;
	struct v4l2_mbus_framefmt mf;
	unsigned int fps;
	int ret = 0;

	dev_dbg(&client->dev, "Setting %d/%d frame interval\n",
		 fi->interval.numerator, fi->interval.denominator);

	mutex_lock(&bf3925->lock);
	if (bf3925->format.width == 1600)
		goto unlock;
	fps = DIV_ROUND_CLOSEST(fi->interval.denominator,
				fi->interval.numerator);
	mf = bf3925->format;
	__bf3925_try_frame_size_fps(&mf, &size, fps);

	if (bf3925->frame_size != size) {
		dev_info(&client->dev, "%s match wxh@FPS is %dx%d@%d\n",
			__func__, size->width, size->height,
			DIV_ROUND_CLOSEST(size->max_fps.denominator,
					size->max_fps.numerator));
		ret = bf3925_write_array(client, size->regs);
		if (ret)
			goto unlock;
		bf3925->frame_size = size;
		bf3925->fps = fps;
	}
unlock:
	mutex_unlock(&bf3925->lock);

	return ret;
}

static void bf3925_get_module_inf(struct bf3925 *bf3925,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, DRIVER_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, bf3925->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, bf3925->len_name, sizeof(inf->base.lens));
}

static long bf3925_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct bf3925 *bf3925 = to_bf3925(sd);
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		bf3925_get_module_inf(bf3925, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		stream = *((u32 *)arg);
		if (stream)
			bf3925_set_streaming(bf3925, 0x00);
		else
			bf3925_set_streaming(bf3925, 0x02);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long bf3925_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	long ret;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = bf3925_ioctl(sd, cmd, inf);
		if (!ret)
			ret = copy_to_user(up, inf, sizeof(*inf));
		kfree(inf);
		break;
	case RKMODULE_AWB_CFG:
		cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
		if (!cfg) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(cfg, up, sizeof(*cfg));
		if (!ret)
			ret = bf3925_ioctl(sd, cmd, cfg);
		kfree(cfg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = bf3925_ioctl(sd, cmd, &stream);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int bf3925_init(struct v4l2_subdev *sd, u32 val)
{
	int ret;
	struct bf3925 *bf3925 = to_bf3925(sd);
	struct i2c_client *client = bf3925->client;

	dev_info(&client->dev, "%s(%d)\n", __func__, __LINE__);
	/* soft reset */
	ret = bf3925_write(client, 0xf2, 0x03);
	ret = bf3925_write_array(client, bf3925_init_regs);
	return ret;
}

static int bf3925_power(struct v4l2_subdev *sd, int on)
{
	int ret;
	struct bf3925 *bf3925 = to_bf3925(sd);
	struct i2c_client *client = bf3925->client;
	struct device *dev = &bf3925->client->dev;

	dev_info(&client->dev, "%s(%d) on(%d)\n", __func__, __LINE__, on);
	if (on) {
		if (!IS_ERR(bf3925->pwdn_gpio)) {
			gpiod_set_value_cansleep(bf3925->pwdn_gpio, 0);
			usleep_range(2000, 5000);
		}
		ret = bf3925_init(sd, 0);
		usleep_range(10000, 20000);
		if (ret)
			dev_err(dev, "init error\n");
	} else {
		if (!IS_ERR(bf3925->pwdn_gpio)) {
			gpiod_set_value_cansleep(bf3925->pwdn_gpio, 1);
			usleep_range(2000, 5000);
		}
	}
	return 0;
}

static int bf3925_enum_frame_interval(struct v4l2_subdev *sd,
				      struct v4l2_subdev_pad_config *cfg,
				      struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->index >= ARRAY_SIZE(bf3925_framesizes))
		return -EINVAL;

	if (fie->code != MEDIA_BUS_FMT_UYVY8_2X8)
		return -EINVAL;

	fie->width = bf3925_framesizes[fie->index].width;
	fie->height = bf3925_framesizes[fie->index].height;
	fie->interval = bf3925_framesizes[fie->index].max_fps;
	return 0;
}

static const struct v4l2_subdev_core_ops bf3925_subdev_core_ops = {
	.log_status = v4l2_ctrl_subdev_log_status,
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
	.ioctl = bf3925_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = bf3925_compat_ioctl32,
#endif
	.s_power = bf3925_power,
};

static const struct v4l2_subdev_video_ops bf3925_subdev_video_ops = {
	.s_stream = bf3925_s_stream,
	.g_mbus_config = bf3925_g_mbus_config,
	.g_frame_interval = bf3925_g_frame_interval,
	.s_frame_interval = bf3925_s_frame_interval,
};

static const struct v4l2_subdev_pad_ops bf3925_subdev_pad_ops = {
	.enum_mbus_code = bf3925_enum_mbus_code,
	.enum_frame_size = bf3925_enum_frame_sizes,
	.enum_frame_interval = bf3925_enum_frame_interval,
	.get_fmt = bf3925_get_fmt,
	.set_fmt = bf3925_set_fmt,
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_ops bf3925_subdev_ops = {
	.core  = &bf3925_subdev_core_ops,
	.video = &bf3925_subdev_video_ops,
	.pad   = &bf3925_subdev_pad_ops,
};

static const struct v4l2_subdev_internal_ops bf3925_subdev_internal_ops = {
	.open = bf3925_open,
};
#endif

static int bf3925_detect(struct bf3925 *bf3925)
{
	struct i2c_client *client = bf3925->client;
	u8 pid, ver;
	int ret;

	dev_dbg(&client->dev, "%s:\n", __func__);

	/* Check sensor revision */
	ret = bf3925_read(client, REG_SC_CHIP_ID_H, &pid);
	if (!ret)
		ret = bf3925_read(client, REG_SC_CHIP_ID_L, &ver);

	if (!ret) {
		unsigned short id;

		id = SENSOR_ID(pid, ver);
		if (id != BF3925_ID) {
			ret = -1;
			dev_err(&client->dev,
				"Sensor detection failed (%04X, %d)\n",
				id, ret);
		} else {
			dev_info(&client->dev, "Found BF%04X sensor\n", id);
			if (!IS_ERR(bf3925->pwdn_gpio))
				gpiod_set_value_cansleep(bf3925->pwdn_gpio, 1);
		}
	}

	return ret;
}

static int __bf3925_power_on(struct bf3925 *bf3925)
{
	int ret;
	struct device *dev = &bf3925->client->dev;

	dev_info(dev, "power on!!!\n");
	if (!IS_ERR(bf3925->xvclk)) {
		ret = clk_set_rate(bf3925->xvclk, 24000000);
		if (ret < 0)
			dev_info(dev, "Failed to set xvclk rate (24MHz)\n");
	}
	if (!IS_ERR(bf3925->xvclk)) {
		ret = clk_prepare_enable(bf3925->xvclk);
		if (ret < 0)
			dev_info(dev, "Failed to enable xvclk\n");
	}
	usleep_range(7000, 10000);

	if (!IS_ERR(bf3925->pwdn_gpio)) {
		gpiod_set_value_cansleep(bf3925->pwdn_gpio, 1);
		usleep_range(2000, 5000);
	}

	if (!IS_ERR(bf3925->supplies)) {
		ret = regulator_bulk_enable(BF3925_NUM_SUPPLIES,
			bf3925->supplies);
		if (ret < 0)
			dev_info(dev, "Failed to enable regulators\n");

		usleep_range(20000, 50000);
	}

	if (!IS_ERR(bf3925->pwdn2_gpio)) {
		gpiod_set_value_cansleep(bf3925->pwdn2_gpio, 1);
		usleep_range(2000, 5000);
	}

	if (!IS_ERR(bf3925->pwdn_gpio)) {
		gpiod_set_value_cansleep(bf3925->pwdn_gpio, 0);
		usleep_range(2000, 5000);
	}

	return 0;
}

static void __bf3925_power_off(struct bf3925 *bf3925)
{
	if (!IS_ERR(bf3925->xvclk))
		clk_disable_unprepare(bf3925->xvclk);
	if (!IS_ERR(bf3925->supplies))
		regulator_bulk_disable(BF3925_NUM_SUPPLIES, bf3925->supplies);
	if (!IS_ERR(bf3925->pwdn_gpio))
		gpiod_set_value_cansleep(bf3925->pwdn_gpio, 1);
}

static int bf3925_configure_regulators(struct bf3925 *bf3925)
{
	unsigned int i;

	for (i = 0; i < BF3925_NUM_SUPPLIES; i++)
		bf3925->supplies[i].supply = bf3925_supply_names[i];

	return devm_regulator_bulk_get(&bf3925->client->dev,
				       BF3925_NUM_SUPPLIES,
				       bf3925->supplies);
}

static int bf3925_parse_of(struct bf3925 *bf3925)
{
	struct device *dev = &bf3925->client->dev;
	struct device_node *node = dev->of_node;
	struct gpio_desc *pwdn_gpio;
	unsigned int pwdn = -1;
	enum of_gpio_flags flags;
	int ret;

	bf3925->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(bf3925->pwdn_gpio)) {
		dev_info(dev, "Failed to get pwdn-gpios, maybe no use\n");
		pwdn = of_get_named_gpio_flags(node, "pwdn-gpios", 0, &flags);
		pwdn_gpio = gpio_to_desc(pwdn);
		if (IS_ERR(pwdn_gpio))
			dev_info(dev, "Failed to get pwdn-gpios again\n");
		else
			bf3925->pwdn_gpio = pwdn_gpio;
	}

	bf3925->pwdn2_gpio = devm_gpiod_get(dev, "pwdn2", GPIOD_OUT_LOW);
	if (IS_ERR(bf3925->pwdn2_gpio)) {
		dev_info(dev, "Failed to get pwdn2-gpios, maybe no use\n");
		pwdn = of_get_named_gpio_flags(node, "pwdn2-gpios", 0, &flags);
		pwdn_gpio = gpio_to_desc(pwdn);
		if (IS_ERR(pwdn_gpio))
			dev_info(dev, "Failed to get pwdn2-gpios again\n");
		else
			bf3925->pwdn2_gpio = pwdn_gpio;
	}

	ret = bf3925_configure_regulators(bf3925);
	if (ret)
		dev_info(dev, "Failed to get power regulators\n");

	return __bf3925_power_on(bf3925);
}

static int bf3925_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct v4l2_subdev *sd;
	struct bf3925 *bf3925;
	char facing[2] = "b";
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	bf3925 = devm_kzalloc(&client->dev, sizeof(*bf3925), GFP_KERNEL);
	if (!bf3925)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &bf3925->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &bf3925->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &bf3925->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &bf3925->len_name);
	if (ret) {
		dev_err(&client->dev, "could not get module information!\n");
		return -EINVAL;
	}

	bf3925->client = client;
	bf3925->xvclk = devm_clk_get(&client->dev, "xvclk");
	if (IS_ERR(bf3925->xvclk)) {
		dev_err(&client->dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	bf3925_parse_of(bf3925);

	bf3925->xvclk_frequency = clk_get_rate(bf3925->xvclk);
	if (bf3925->xvclk_frequency < 6000000 ||
	    bf3925->xvclk_frequency > 27000000)
		return -EINVAL;

	v4l2_ctrl_handler_init(&bf3925->ctrls, 2);
	bf3925->link_frequency =
			v4l2_ctrl_new_std(&bf3925->ctrls, &bf3925_ctrl_ops,
					  V4L2_CID_PIXEL_RATE, 0,
					  BF3925_PIXEL_RATE, 1,
					  BF3925_PIXEL_RATE);

	v4l2_ctrl_new_std_menu_items(&bf3925->ctrls, &bf3925_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(bf3925_test_pattern_menu) - 1,
				     0, 0, bf3925_test_pattern_menu);
	bf3925->sd.ctrl_handler = &bf3925->ctrls;

	if (bf3925->ctrls.error) {
		dev_err(&client->dev, "%s: control initialization error %d\n",
			__func__, bf3925->ctrls.error);
		return  bf3925->ctrls.error;
	}

	sd = &bf3925->sd;
	client->flags |= I2C_CLIENT_SCCB;
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	v4l2_i2c_subdev_init(sd, client, &bf3925_subdev_ops);

	sd->internal_ops = &bf3925_subdev_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif

#if defined(CONFIG_MEDIA_CONTROLLER)
	bf3925->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &bf3925->pad);
	if (ret < 0) {
		v4l2_ctrl_handler_free(&bf3925->ctrls);
		return ret;
	}
#endif

	mutex_init(&bf3925->lock);

	bf3925_get_default_format(&bf3925->format);
	bf3925->frame_size = &bf3925_framesizes[0];
	bf3925->format.width = bf3925_framesizes[0].width;
	bf3925->format.height = bf3925_framesizes[0].height;
	bf3925->fps = DIV_ROUND_CLOSEST(bf3925_framesizes[0].max_fps.denominator,
				bf3925_framesizes[0].max_fps.numerator);

	ret = bf3925_detect(bf3925);
	if (ret < 0)
		goto error;

	memset(facing, 0, sizeof(facing));
	if (strcmp(bf3925->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 bf3925->module_index, facing,
		 DRIVER_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev_sensor_common(&bf3925->sd);
	if (ret)
		goto error;

	dev_info(&client->dev, "%s sensor driver registered !!\n", sd->name);

	return 0;

error:
	v4l2_ctrl_handler_free(&bf3925->ctrls);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	mutex_destroy(&bf3925->lock);
	__bf3925_power_off(bf3925);
	return ret;
}

static int bf3925_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct bf3925 *bf3925 = to_bf3925(sd);

	v4l2_ctrl_handler_free(&bf3925->ctrls);
	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	mutex_destroy(&bf3925->lock);

	__bf3925_power_off(bf3925);

	return 0;
}

static const struct i2c_device_id bf3925_id[] = {
	{ "bf3925", 0 },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(i2c, bf3925_id);

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id bf3925_of_match[] = {
	{ .compatible = "byd,bf3925", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, bf3925_of_match);
#endif

static struct i2c_driver bf3925_i2c_driver = {
	.driver = {
		.name	= DRIVER_NAME,
		.of_match_table = of_match_ptr(bf3925_of_match),
	},
	.probe		= bf3925_probe,
	.remove		= bf3925_remove,
	.id_table	= bf3925_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&bf3925_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&bf3925_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_AUTHOR("Benoit Parrot <bparrot@ti.com>");
MODULE_DESCRIPTION("BF3925 CMOS Image Sensor driver");
MODULE_LICENSE("GPL v2");
