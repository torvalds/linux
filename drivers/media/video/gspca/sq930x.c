/*
 * SQ930x subdriver
 *
 * Copyright (C) 2010 Jean-François Moine <http://moinejf.free.fr>
 * Copyright (C) 2006 -2008 Gerard Klaver <gerard at gkall dot hobby dot nl>
 * Copyright (C) 2007 Sam Revitch <samr7@cs.washington.edu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define MODULE_NAME "sq930x"

#include "gspca.h"

MODULE_AUTHOR("Jean-Francois Moine <http://moinejf.free.fr>\n"
		"Gerard Klaver <gerard at gkall dot hobby dot nl\n"
		"Sam Revitch <samr7@cs.washington.edu>");
MODULE_DESCRIPTION("GSPCA/SQ930x USB Camera Driver");
MODULE_LICENSE("GPL");

/* Structure to hold all of our device specific stuff */
struct sd {
	struct gspca_dev gspca_dev;	/* !! must be the first item */

	u16 expo;
	u8 gain;

	u8 do_ctrl;
	u8 gpio[2];
	u8 sensor;
	u8 type;
#define Generic 0
#define Creative_live_motion 1
};
enum sensors {
	SENSOR_ICX098BQ,
	SENSOR_LZ24BP,
	SENSOR_MI0360,
	SENSOR_MT9V111,		/* = MI360SOC */
	SENSOR_OV7660,
	SENSOR_OV9630,
};

static int sd_setexpo(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getexpo(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setgain(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getgain(struct gspca_dev *gspca_dev, __s32 *val);

static const struct ctrl sd_ctrls[] = {
	{
	    {
		.id = V4L2_CID_EXPOSURE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Exposure",
		.minimum = 0x0001,
		.maximum = 0x0fff,
		.step = 1,
#define EXPO_DEF 0x0356
		.default_value = EXPO_DEF,
	    },
	    .set = sd_setexpo,
	    .get = sd_getexpo,
	},
	{
	    {
		.id = V4L2_CID_GAIN,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Gain",
		.minimum = 0x01,
		.maximum = 0xff,
		.step = 1,
#define GAIN_DEF 0x8d
		.default_value = GAIN_DEF,
	    },
	    .set = sd_setgain,
	    .get = sd_getgain,
	},
};

static struct v4l2_pix_format vga_mode[] = {
	{320, 240, V4L2_PIX_FMT_SRGGB8, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0},
	{640, 480, V4L2_PIX_FMT_SRGGB8, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 1},
};

/* sq930x registers */
#define SQ930_CTRL_UCBUS_IO	0x0001
#define SQ930_CTRL_I2C_IO	0x0002
#define SQ930_CTRL_GPIO		0x0005
#define SQ930_CTRL_CAP_START	0x0010
#define SQ930_CTRL_CAP_STOP	0x0011
#define SQ930_CTRL_SET_EXPOSURE 0x001d
#define SQ930_CTRL_RESET	0x001e
#define SQ930_CTRL_GET_DEV_INFO 0x001f

/* gpio 1 (8..15) */
#define SQ930_GPIO_DFL_I2C_SDA	0x0001
#define SQ930_GPIO_DFL_I2C_SCL	0x0002
#define SQ930_GPIO_RSTBAR	0x0004
#define SQ930_GPIO_EXTRA1	0x0040
#define SQ930_GPIO_EXTRA2	0x0080
/* gpio 3 (24..31) */
#define SQ930_GPIO_POWER	0x0200
#define SQ930_GPIO_DFL_LED	0x1000

struct ucbus_write_cmd {
	u16	bw_addr;
	u8	bw_data;
};
struct i2c_write_cmd {
	u8	reg;
	u16	val;
};

static const struct ucbus_write_cmd icx098bq_start_0[] = {
	{0x0354, 0x00}, {0x03fa, 0x00}, {0xf800, 0x02}, {0xf801, 0xce},
	{0xf802, 0xc1}, {0xf804, 0x00}, {0xf808, 0x00}, {0xf809, 0x0e},
	{0xf80a, 0x01}, {0xf80b, 0xee}, {0xf807, 0x60}, {0xf80c, 0x02},
	{0xf80d, 0xf0}, {0xf80e, 0x03}, {0xf80f, 0x0a}, {0xf81c, 0x02},
	{0xf81d, 0xf0}, {0xf81e, 0x03}, {0xf81f, 0x0a}, {0xf83a, 0x00},
	{0xf83b, 0x10}, {0xf83c, 0x00}, {0xf83d, 0x4e}, {0xf810, 0x04},
	{0xf811, 0x00}, {0xf812, 0x02}, {0xf813, 0x10}, {0xf803, 0x00},
	{0xf814, 0x01}, {0xf815, 0x18}, {0xf816, 0x00}, {0xf817, 0x48},
	{0xf818, 0x00}, {0xf819, 0x25}, {0xf81a, 0x00}, {0xf81b, 0x3c},
	{0xf82f, 0x03}, {0xf820, 0xff}, {0xf821, 0x0d}, {0xf822, 0xff},
	{0xf823, 0x07}, {0xf824, 0xff}, {0xf825, 0x03}, {0xf826, 0xff},
	{0xf827, 0x06}, {0xf828, 0xff}, {0xf829, 0x03}, {0xf82a, 0xff},
	{0xf82b, 0x0c}, {0xf82c, 0xfd}, {0xf82d, 0x01}, {0xf82e, 0x00},
	{0xf830, 0x00}, {0xf831, 0x47}, {0xf832, 0x00}, {0xf833, 0x00},
	{0xf850, 0x00}, {0xf851, 0x00}, {0xf852, 0x00}, {0xf853, 0x24},
	{0xf854, 0x00}, {0xf855, 0x18}, {0xf856, 0x00}, {0xf857, 0x3c},
	{0xf858, 0x00}, {0xf859, 0x0c}, {0xf85a, 0x00}, {0xf85b, 0x30},
	{0xf85c, 0x00}, {0xf85d, 0x0c}, {0xf85e, 0x00}, {0xf85f, 0x30},
	{0xf860, 0x00}, {0xf861, 0x48}, {0xf862, 0x01}, {0xf863, 0xdc},
	{0xf864, 0xff}, {0xf865, 0x98}, {0xf866, 0xff}, {0xf867, 0xc0},
	{0xf868, 0xff}, {0xf869, 0x70}, {0xf86c, 0xff}, {0xf86d, 0x00},
	{0xf86a, 0xff}, {0xf86b, 0x48}, {0xf86e, 0xff}, {0xf86f, 0x00},
	{0xf870, 0x01}, {0xf871, 0xdb}, {0xf872, 0x01}, {0xf873, 0xfa},
	{0xf874, 0x01}, {0xf875, 0xdb}, {0xf876, 0x01}, {0xf877, 0xfa},
	{0xf878, 0x0f}, {0xf879, 0x0f}, {0xf87a, 0xff}, {0xf87b, 0xff},
	{0xf800, 0x03}
};
static const struct ucbus_write_cmd icx098bq_start_1[] = {
	{0xf5f0, 0x00}, {0xf5f1, 0xcd}, {0xf5f2, 0x80}, {0xf5f3, 0x80},
	{0xf5f4, 0xc0},
	{0xf5f0, 0x49}, {0xf5f1, 0xcd}, {0xf5f2, 0x80}, {0xf5f3, 0x80},
	{0xf5f4, 0xc0},
	{0xf5fa, 0x00}, {0xf5f6, 0x00}, {0xf5f7, 0x00}, {0xf5f8, 0x00},
	{0xf5f9, 0x00}
};

static const struct ucbus_write_cmd icx098bq_start_2[] = {
	{0xf800, 0x02}, {0xf807, 0xff}, {0xf805, 0x82}, {0xf806, 0x00},
	{0xf807, 0x7f}, {0xf800, 0x03},
	{0xf800, 0x02}, {0xf807, 0xff}, {0xf805, 0x40}, {0xf806, 0x00},
	{0xf807, 0x7f}, {0xf800, 0x03},
	{0xf800, 0x02}, {0xf807, 0xff}, {0xf805, 0xcf}, {0xf806, 0xd0},
	{0xf807, 0x7f}, {0xf800, 0x03},
	{0xf800, 0x02}, {0xf807, 0xff}, {0xf805, 0x00}, {0xf806, 0x00},
	{0xf807, 0x7f}, {0xf800, 0x03}
};

static const struct ucbus_write_cmd lz24bp_start_0[] = {
	{0x0354, 0x00}, {0x03fa, 0x00}, {0xf800, 0x02}, {0xf801, 0xbe},
	{0xf802, 0xc6}, {0xf804, 0x00}, {0xf808, 0x00}, {0xf809, 0x06},
	{0xf80a, 0x01}, {0xf80b, 0xfe}, {0xf807, 0x84}, {0xf80c, 0x02},
	{0xf80d, 0xf7}, {0xf80e, 0x03}, {0xf80f, 0x0b}, {0xf81c, 0x00},
	{0xf81d, 0x49}, {0xf81e, 0x03}, {0xf81f, 0x0b}, {0xf83a, 0x00},
	{0xf83b, 0x01}, {0xf83c, 0x00}, {0xf83d, 0x6b}, {0xf810, 0x03},
	{0xf811, 0x10}, {0xf812, 0x02}, {0xf813, 0x6f}, {0xf803, 0x00},
	{0xf814, 0x00}, {0xf815, 0x44}, {0xf816, 0x00}, {0xf817, 0x48},
	{0xf818, 0x00}, {0xf819, 0x25}, {0xf81a, 0x00}, {0xf81b, 0x3c},
	{0xf82f, 0x03}, {0xf820, 0xff}, {0xf821, 0x0d}, {0xf822, 0xff},
	{0xf823, 0x07}, {0xf824, 0xfd}, {0xf825, 0x07}, {0xf826, 0xf0},
	{0xf827, 0x0c}, {0xf828, 0xff}, {0xf829, 0x03}, {0xf82a, 0xff},
	{0xf82b, 0x0c}, {0xf82c, 0xfc}, {0xf82d, 0x01}, {0xf82e, 0x00},
	{0xf830, 0x00}, {0xf831, 0x47}, {0xf832, 0x00}, {0xf833, 0x00},
	{0xf850, 0x00}, {0xf851, 0x00}, {0xf852, 0x00}, {0xf853, 0x24},
	{0xf854, 0x00}, {0xf855, 0x0c}, {0xf856, 0x00}, {0xf857, 0x30},
	{0xf858, 0x00}, {0xf859, 0x18}, {0xf85a, 0x00}, {0xf85b, 0x3c},
	{0xf85c, 0x00}, {0xf85d, 0x18}, {0xf85e, 0x00}, {0xf85f, 0x3c},
	{0xf860, 0xff}, {0xf861, 0x37}, {0xf862, 0xff}, {0xf863, 0x1d},
	{0xf864, 0xff}, {0xf865, 0x98}, {0xf866, 0xff}, {0xf867, 0xc0},
	{0xf868, 0x00}, {0xf869, 0x37}, {0xf86c, 0x02}, {0xf86d, 0x1d},
	{0xf86a, 0x00}, {0xf86b, 0x37}, {0xf86e, 0x02}, {0xf86f, 0x1d},
	{0xf870, 0x01}, {0xf871, 0xc6}, {0xf872, 0x02}, {0xf873, 0x04},
	{0xf874, 0x01}, {0xf875, 0xc6}, {0xf876, 0x02}, {0xf877, 0x04},
	{0xf878, 0x0f}, {0xf879, 0x0f}, {0xf87a, 0xff}, {0xf87b, 0xff},
	{0xf800, 0x03}
};
static const struct ucbus_write_cmd lz24bp_start_1_gen[] = {
	{0xf5f0, 0x00}, {0xf5f1, 0xff}, {0xf5f2, 0x80}, {0xf5f3, 0x80},
	{0xf5f4, 0xb3},
	{0xf5f0, 0x40}, {0xf5f1, 0xff}, {0xf5f2, 0x80}, {0xf5f3, 0x80},
	{0xf5f4, 0xb3},
	{0xf5fa, 0x00}, {0xf5f6, 0x00}, {0xf5f7, 0x00}, {0xf5f8, 0x00},
	{0xf5f9, 0x00}
};

static const struct ucbus_write_cmd lz24bp_start_1_clm[] = {
	{0xf5f0, 0x00}, {0xf5f1, 0xff}, {0xf5f2, 0x88}, {0xf5f3, 0x88},
	{0xf5f4, 0xc0},
	{0xf5f0, 0x40}, {0xf5f1, 0xff}, {0xf5f2, 0x88}, {0xf5f3, 0x88},
	{0xf5f4, 0xc0},
	{0xf5fa, 0x00}, {0xf5f6, 0x00}, {0xf5f7, 0x00}, {0xf5f8, 0x00},
	{0xf5f9, 0x00}
};

static const struct ucbus_write_cmd lz24bp_start_2[] = {
	{0xf800, 0x02}, {0xf807, 0xff}, {0xf805, 0x80}, {0xf806, 0x00},
	{0xf807, 0x7f}, {0xf800, 0x03},
	{0xf800, 0x02}, {0xf807, 0xff}, {0xf805, 0x4e}, {0xf806, 0x00},
	{0xf807, 0x7f}, {0xf800, 0x03},
	{0xf800, 0x02}, {0xf807, 0xff}, {0xf805, 0xc0}, {0xf806, 0x48},
	{0xf807, 0x7f}, {0xf800, 0x03},
	{0xf800, 0x02}, {0xf807, 0xff}, {0xf805, 0x00}, {0xf806, 0x00},
	{0xf807, 0x7f}, {0xf800, 0x03}
};

static const struct ucbus_write_cmd mi0360_start_0[] = {
	{0x0354, 0x00}, {0x03fa, 0x00}, {0xf332, 0xcc}, {0xf333, 0xcc},
	{0xf334, 0xcc}, {0xf335, 0xcc}, {0xf33f, 0x00}
};
static const struct i2c_write_cmd mi0360_init_23[] = {
	{0x30, 0x0040},		/* reserved - def 0x0005 */
	{0x31, 0x0000},		/* reserved - def 0x002a */
	{0x34, 0x0100},		/* reserved - def 0x0100 */
	{0x3d, 0x068f},		/* reserved - def 0x068f */
};
static const struct i2c_write_cmd mi0360_init_24[] = {
	{0x03, 0x01e5},		/* window height */
	{0x04, 0x0285},		/* window width */
};
static const struct i2c_write_cmd mi0360_init_25[] = {
	{0x35, 0x0020},		/* global gain */
	{0x2b, 0x0020},		/* green1 gain */
	{0x2c, 0x002a},		/* blue gain */
	{0x2d, 0x0028},		/* red gain */
	{0x2e, 0x0020},		/* green2 gain */
};
static const struct ucbus_write_cmd mi0360_start_1[] = {
	{0xf5f0, 0x11}, {0xf5f1, 0x99}, {0xf5f2, 0x80}, {0xf5f3, 0x80},
	{0xf5f4, 0xa6},
	{0xf5f0, 0x51}, {0xf5f1, 0x99}, {0xf5f2, 0x80}, {0xf5f3, 0x80},
	{0xf5f4, 0xa6},
	{0xf5fa, 0x00}, {0xf5f6, 0x00}, {0xf5f7, 0x00}, {0xf5f8, 0x00},
	{0xf5f9, 0x00}
};
static const struct i2c_write_cmd mi0360_start_2[] = {
	{0x62, 0x041d},		/* reserved - def 0x0418 */
};
static const struct i2c_write_cmd mi0360_start_3[] = {
	{0x05, 0x007b},		/* horiz blanking */
};
static const struct i2c_write_cmd mi0360_start_4[] = {
	{0x05, 0x03f5},		/* horiz blanking */
};

static const struct i2c_write_cmd mt9v111_init_0[] = {
	{0x01, 0x0001},		/* select IFP/SOC registers */
	{0x06, 0x300c},		/* operating mode control */
	{0x08, 0xcc00},		/* output format control (RGB) */
	{0x01, 0x0004},		/* select sensor core registers */
};
static const struct i2c_write_cmd mt9v111_init_1[] = {
	{0x03, 0x01e5},		/* window height */
	{0x04, 0x0285},		/* window width */
};
static const struct i2c_write_cmd mt9v111_init_2[] = {
	{0x30, 0x7800},
	{0x31, 0x0000},
	{0x07, 0x3002},		/* output control */
	{0x35, 0x0020},		/* global gain */
	{0x2b, 0x0020},		/* green1 gain */
	{0x2c, 0x0020},		/* blue gain */
	{0x2d, 0x0020},		/* red gain */
	{0x2e, 0x0020},		/* green2 gain */
};
static const struct ucbus_write_cmd mt9v111_start_1[] = {
	{0xf5f0, 0x11}, {0xf5f1, 0x96}, {0xf5f2, 0x80}, {0xf5f3, 0x80},
	{0xf5f4, 0xaa},
	{0xf5f0, 0x51}, {0xf5f1, 0x96}, {0xf5f2, 0x80}, {0xf5f3, 0x80},
	{0xf5f4, 0xaa},
	{0xf5fa, 0x00}, {0xf5f6, 0x0a}, {0xf5f7, 0x0a}, {0xf5f8, 0x0a},
	{0xf5f9, 0x0a}
};
static const struct i2c_write_cmd mt9v111_init_3[] = {
	{0x62, 0x0405},
};
static const struct i2c_write_cmd mt9v111_init_4[] = {
/*	{0x05, 0x00ce}, */
	{0x05, 0x005d},		/* horizontal blanking */
};

static const struct ucbus_write_cmd ov7660_start_0[] = {
	{0x0354, 0x00}, {0x03fa, 0x00}, {0xf332, 0x00}, {0xf333, 0xc0},
	{0xf334, 0x39}, {0xf335, 0xe7}, {0xf33f, 0x03}
};

static const struct ucbus_write_cmd ov9630_start_0[] = {
	{0x0354, 0x00}, {0x03fa, 0x00}, {0xf332, 0x00}, {0xf333, 0x00},
	{0xf334, 0x3e}, {0xf335, 0xf8}, {0xf33f, 0x03}
};

/* start parameters indexed by [sensor][mode] */
static const struct cap_s {
	u8	cc_sizeid;
	u8	cc_bytes[32];
} capconfig[4][2] = {
	[SENSOR_ICX098BQ] = {
		{2,				/* Bayer 320x240 */
		  {0x05, 0x1f, 0x20, 0x0e, 0x00, 0x9f, 0x02, 0xee,
		   0x01, 0x01, 0x00, 0x08, 0x18, 0x12, 0x78, 0xc8,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00} },
		{4,				/* Bayer 640x480 */
		  {0x01, 0x1f, 0x20, 0x0e, 0x00, 0x9f, 0x02, 0xee,
		   0x01, 0x02, 0x00, 0x08, 0x18, 0x12, 0x78, 0xc8,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00} },
	},
	[SENSOR_LZ24BP] = {
		{2,				/* Bayer 320x240 */
		  {0x05, 0x22, 0x20, 0x0e, 0x00, 0xa2, 0x02, 0xee,
		   0x01, 0x01, 0x00, 0x08, 0x18, 0x12, 0x78, 0xc8,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00} },
		{4,				/* Bayer 640x480 */
		  {0x01, 0x22, 0x20, 0x0e, 0x00, 0xa2, 0x02, 0xee,
		   0x01, 0x02, 0x00, 0x08, 0x18, 0x12, 0x78, 0xc8,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00} },
	},
	[SENSOR_MI0360] = {
		{2,				/* Bayer 320x240 */
		  {0x05, 0x02, 0x20, 0x01, 0x20, 0x82, 0x02, 0xe1,
		   0x01, 0x01, 0x00, 0x08, 0x18, 0x12, 0x78, 0xc8,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00} },
		{4,				/* Bayer 640x480 */
		  {0x01, 0x02, 0x20, 0x01, 0x20, 0x82, 0x02, 0xe1,
		   0x01, 0x02, 0x00, 0x08, 0x18, 0x12, 0x78, 0xc8,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00} },
	},
	[SENSOR_MT9V111] = {
		{2,				/* Bayer 320x240 */
		  {0x05, 0x02, 0x20, 0x01, 0x20, 0x82, 0x02, 0xe1,
		   0x01, 0x01, 0x00, 0x08, 0x18, 0x12, 0x78, 0xc8,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00} },
		{4,				/* Bayer 640x480 */
		  {0x01, 0x02, 0x20, 0x01, 0x20, 0x82, 0x02, 0xe1,
		   0x01, 0x02, 0x00, 0x08, 0x18, 0x12, 0x78, 0xc8,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00} },
	},
};

struct sensor_s {
	const char *name;
	u8 i2c_addr;
	u8 i2c_dum;
	u8 gpio[5];
	u8 cmd_len;
	const struct ucbus_write_cmd *cmd;
};

static const struct sensor_s sensor_tb[] = {
	[SENSOR_ICX098BQ] = {
		"icx098bp",
		0x00, 0x00,
		{0,
		 SQ930_GPIO_DFL_I2C_SDA | SQ930_GPIO_DFL_I2C_SCL,
		 SQ930_GPIO_DFL_I2C_SDA,
		 0,
		 SQ930_GPIO_RSTBAR
		},
		8, icx098bq_start_0
	    },
	[SENSOR_LZ24BP] = {
		"lz24bp",
		0x00, 0x00,
		{0,
		 SQ930_GPIO_DFL_I2C_SDA | SQ930_GPIO_DFL_I2C_SCL,
		 SQ930_GPIO_DFL_I2C_SDA,
		 0,
		 SQ930_GPIO_RSTBAR
		},
		8, lz24bp_start_0
	    },
	[SENSOR_MI0360] = {
		"mi0360",
		0x5d, 0x80,
		{SQ930_GPIO_RSTBAR,
		 SQ930_GPIO_DFL_I2C_SDA | SQ930_GPIO_DFL_I2C_SCL,
		 SQ930_GPIO_DFL_I2C_SDA,
		 0,
		 0
		},
		7, mi0360_start_0
	    },
	[SENSOR_MT9V111] = {
		"mt9v111",
		0x5c, 0x7f,
		{SQ930_GPIO_RSTBAR,
		 SQ930_GPIO_DFL_I2C_SDA | SQ930_GPIO_DFL_I2C_SCL,
		 SQ930_GPIO_DFL_I2C_SDA,
		 0,
		 0
		},
		7, mi0360_start_0
	    },
	[SENSOR_OV7660] = {
		"ov7660",
		0x21, 0x00,
		{0,
		 SQ930_GPIO_DFL_I2C_SDA | SQ930_GPIO_DFL_I2C_SCL,
		 SQ930_GPIO_DFL_I2C_SDA,
		 0,
		 SQ930_GPIO_RSTBAR
		},
		7, ov7660_start_0
	    },
	[SENSOR_OV9630] = {
		"ov9630",
		0x30, 0x00,
		{0,
		 SQ930_GPIO_DFL_I2C_SDA | SQ930_GPIO_DFL_I2C_SCL,
		 SQ930_GPIO_DFL_I2C_SDA,
		 0,
		 SQ930_GPIO_RSTBAR
		},
		7, ov9630_start_0
	    },
};

static void reg_r(struct gspca_dev *gspca_dev,
		u16 value, int len)
{
	int ret;

	if (gspca_dev->usb_err < 0)
		return;
	ret = usb_control_msg(gspca_dev->dev,
			usb_rcvctrlpipe(gspca_dev->dev, 0),
			0x0c,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			value, 0, gspca_dev->usb_buf, len,
			500);
	if (ret < 0) {
		pr_err("reg_r %04x failed %d\n", value, ret);
		gspca_dev->usb_err = ret;
	}
}

static void reg_w(struct gspca_dev *gspca_dev, u16 value, u16 index)
{
	int ret;

	if (gspca_dev->usb_err < 0)
		return;
	PDEBUG(D_USBO, "reg_w v: %04x i: %04x", value, index);
	ret = usb_control_msg(gspca_dev->dev,
			usb_sndctrlpipe(gspca_dev->dev, 0),
			0x0c,			/* request */
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			value, index, NULL, 0,
			500);
	msleep(30);
	if (ret < 0) {
		pr_err("reg_w %04x %04x failed %d\n", value, index, ret);
		gspca_dev->usb_err = ret;
	}
}

static void reg_wb(struct gspca_dev *gspca_dev, u16 value, u16 index,
		const u8 *data, int len)
{
	int ret;

	if (gspca_dev->usb_err < 0)
		return;
	PDEBUG(D_USBO, "reg_wb v: %04x i: %04x %02x...%02x",
			value, index, *data, data[len - 1]);
	memcpy(gspca_dev->usb_buf, data, len);
	ret = usb_control_msg(gspca_dev->dev,
			usb_sndctrlpipe(gspca_dev->dev, 0),
			0x0c,			/* request */
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			value, index, gspca_dev->usb_buf, len,
			1000);
	msleep(30);
	if (ret < 0) {
		pr_err("reg_wb %04x %04x failed %d\n", value, index, ret);
		gspca_dev->usb_err = ret;
	}
}

static void i2c_write(struct sd *sd,
			const struct i2c_write_cmd *cmd,
			int ncmds)
{
	struct gspca_dev *gspca_dev = &sd->gspca_dev;
	const struct sensor_s *sensor;
	u16 val, idx;
	u8 *buf;
	int ret;

	if (gspca_dev->usb_err < 0)
		return;

	sensor = &sensor_tb[sd->sensor];

	val = (sensor->i2c_addr << 8) | SQ930_CTRL_I2C_IO;
	idx = (cmd->val & 0xff00) | cmd->reg;

	buf = gspca_dev->usb_buf;
	*buf++ = sensor->i2c_dum;
	*buf++ = cmd->val;

	while (--ncmds > 0) {
		cmd++;
		*buf++ = cmd->reg;
		*buf++ = cmd->val >> 8;
		*buf++ = sensor->i2c_dum;
		*buf++ = cmd->val;
	}

	PDEBUG(D_USBO, "i2c_w v: %04x i: %04x %02x...%02x",
			val, idx, gspca_dev->usb_buf[0], buf[-1]);
	ret = usb_control_msg(gspca_dev->dev,
			usb_sndctrlpipe(gspca_dev->dev, 0),
			0x0c,			/* request */
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			val, idx,
			gspca_dev->usb_buf, buf - gspca_dev->usb_buf,
			500);
	if (ret < 0) {
		pr_err("i2c_write failed %d\n", ret);
		gspca_dev->usb_err = ret;
	}
}

static void ucbus_write(struct gspca_dev *gspca_dev,
			const struct ucbus_write_cmd *cmd,
			int ncmds,
			int batchsize)
{
	u8 *buf;
	u16 val, idx;
	int len, ret;

	if (gspca_dev->usb_err < 0)
		return;

#ifdef GSPCA_DEBUG
	if ((batchsize - 1) * 3 > USB_BUF_SZ) {
		pr_err("Bug: usb_buf overflow\n");
		gspca_dev->usb_err = -ENOMEM;
		return;
	}
#endif

	for (;;) {
		len = ncmds;
		if (len > batchsize)
			len = batchsize;
		ncmds -= len;

		val = (cmd->bw_addr << 8) | SQ930_CTRL_UCBUS_IO;
		idx = (cmd->bw_data << 8) | (cmd->bw_addr >> 8);

		buf = gspca_dev->usb_buf;
		while (--len > 0) {
			cmd++;
			*buf++ = cmd->bw_addr;
			*buf++ = cmd->bw_addr >> 8;
			*buf++ = cmd->bw_data;
		}
		if (buf != gspca_dev->usb_buf)
			PDEBUG(D_USBO, "ucbus v: %04x i: %04x %02x...%02x",
					val, idx,
					gspca_dev->usb_buf[0], buf[-1]);
		else
			PDEBUG(D_USBO, "ucbus v: %04x i: %04x",
					val, idx);
		ret = usb_control_msg(gspca_dev->dev,
				usb_sndctrlpipe(gspca_dev->dev, 0),
				0x0c,			/* request */
			   USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
				val, idx,
				gspca_dev->usb_buf, buf - gspca_dev->usb_buf,
				500);
		if (ret < 0) {
			pr_err("ucbus_write failed %d\n", ret);
			gspca_dev->usb_err = ret;
			return;
		}
		msleep(30);
		if (ncmds <= 0)
			break;
		cmd++;
	}
}

static void gpio_set(struct sd *sd, u16 val, u16 mask)
{
	struct gspca_dev *gspca_dev = &sd->gspca_dev;

	if (mask & 0x00ff) {
		sd->gpio[0] &= ~mask;
		sd->gpio[0] |= val;
		reg_w(gspca_dev, 0x0100 | SQ930_CTRL_GPIO,
			~sd->gpio[0] << 8);
	}
	mask >>= 8;
	val >>= 8;
	if (mask) {
		sd->gpio[1] &= ~mask;
		sd->gpio[1] |= val;
		reg_w(gspca_dev, 0x0300 | SQ930_CTRL_GPIO,
			~sd->gpio[1] << 8);
	}
}

static void gpio_init(struct sd *sd,
			const u8 *gpio)
{
	gpio_set(sd, *gpio++, 0x000f);
	gpio_set(sd, *gpio++, 0x000f);
	gpio_set(sd, *gpio++, 0x000f);
	gpio_set(sd, *gpio++, 0x000f);
	gpio_set(sd, *gpio, 0x000f);
}

static void bridge_init(struct sd *sd)
{
	static const struct ucbus_write_cmd clkfreq_cmd = {
				0xf031, 0	/* SQ930_CLKFREQ_60MHZ */
	};

	ucbus_write(&sd->gspca_dev, &clkfreq_cmd, 1, 1);

	gpio_set(sd, SQ930_GPIO_POWER, 0xff00);
}

static void cmos_probe(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int i;
	const struct sensor_s *sensor;
	static const u8 probe_order[] = {
/*		SENSOR_LZ24BP,		(tested as ccd) */
		SENSOR_OV9630,
		SENSOR_MI0360,
		SENSOR_OV7660,
		SENSOR_MT9V111,
	};

	for (i = 0; i < ARRAY_SIZE(probe_order); i++) {
		sensor = &sensor_tb[probe_order[i]];
		ucbus_write(&sd->gspca_dev, sensor->cmd, sensor->cmd_len, 8);
		gpio_init(sd, sensor->gpio);
		msleep(100);
		reg_r(gspca_dev, (sensor->i2c_addr << 8) | 0x001c, 1);
		msleep(100);
		if (gspca_dev->usb_buf[0] != 0)
			break;
	}
	if (i >= ARRAY_SIZE(probe_order)) {
		pr_err("Unknown sensor\n");
		gspca_dev->usb_err = -EINVAL;
		return;
	}
	sd->sensor = probe_order[i];
	switch (sd->sensor) {
	case SENSOR_OV7660:
	case SENSOR_OV9630:
		pr_err("Sensor %s not yet treated\n",
		       sensor_tb[sd->sensor].name);
		gspca_dev->usb_err = -EINVAL;
		break;
	}
}

static void mt9v111_init(struct gspca_dev *gspca_dev)
{
	int i, nwait;
	static const u8 cmd_001b[] = {
		0x00, 0x3b, 0xf6, 0x01, 0x03, 0x02, 0x00, 0x00,
		0x00, 0x00, 0x00
	};
	static const u8 cmd_011b[][7] = {
		{0x10, 0x01, 0x66, 0x08, 0x00, 0x00, 0x00},
		{0x01, 0x00, 0x1a, 0x04, 0x00, 0x00, 0x00},
		{0x20, 0x00, 0x10, 0x04, 0x00, 0x00, 0x00},
		{0x02, 0x01, 0xae, 0x01, 0x00, 0x00, 0x00},
	};

	reg_wb(gspca_dev, 0x001b, 0x0000, cmd_001b, sizeof cmd_001b);
	for (i = 0; i < ARRAY_SIZE(cmd_011b); i++) {
		reg_wb(gspca_dev, 0x001b, 0x0000, cmd_011b[i],
				ARRAY_SIZE(cmd_011b[0]));
		msleep(400);
		nwait = 20;
		for (;;) {
			reg_r(gspca_dev, 0x031b, 1);
			if (gspca_dev->usb_buf[0] == 0
			 || gspca_dev->usb_err != 0)
				break;
			if (--nwait < 0) {
				PDEBUG(D_PROBE, "mt9v111_init timeout");
				gspca_dev->usb_err = -ETIME;
				return;
			}
			msleep(50);
		}
	}
}

static void global_init(struct sd *sd, int first_time)
{
	switch (sd->sensor) {
	case SENSOR_ICX098BQ:
		if (first_time)
			ucbus_write(&sd->gspca_dev,
					icx098bq_start_0,
					8, 8);
		gpio_init(sd, sensor_tb[sd->sensor].gpio);
		break;
	case SENSOR_LZ24BP:
		if (sd->type != Creative_live_motion)
			gpio_set(sd, SQ930_GPIO_EXTRA1, 0x00ff);
		else
			gpio_set(sd, 0, 0x00ff);
		msleep(50);
		if (first_time)
			ucbus_write(&sd->gspca_dev,
					lz24bp_start_0,
					8, 8);
		gpio_init(sd, sensor_tb[sd->sensor].gpio);
		break;
	case SENSOR_MI0360:
		if (first_time)
			ucbus_write(&sd->gspca_dev,
					mi0360_start_0,
					ARRAY_SIZE(mi0360_start_0),
					8);
		gpio_init(sd, sensor_tb[sd->sensor].gpio);
		gpio_set(sd, SQ930_GPIO_EXTRA2, SQ930_GPIO_EXTRA2);
		break;
	default:
/*	case SENSOR_MT9V111: */
		if (first_time)
			mt9v111_init(&sd->gspca_dev);
		else
			gpio_init(sd, sensor_tb[sd->sensor].gpio);
		break;
	}
}

static void lz24bp_ppl(struct sd *sd, u16 ppl)
{
	struct ucbus_write_cmd cmds[2] = {
		{0xf810, ppl >> 8},
		{0xf811, ppl}
	};

	ucbus_write(&sd->gspca_dev, cmds, ARRAY_SIZE(cmds), 2);
}

static void setexposure(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int i, integclks, intstartclk, frameclks, min_frclk;
	const struct sensor_s *sensor;
	u16 cmd;
	u8 buf[15];

	integclks = sd->expo;
	i = 0;
	cmd = SQ930_CTRL_SET_EXPOSURE;

	switch (sd->sensor) {
	case SENSOR_ICX098BQ:			/* ccd */
	case SENSOR_LZ24BP:
		min_frclk = sd->sensor == SENSOR_ICX098BQ ? 0x210 : 0x26f;
		if (integclks >= min_frclk) {
			intstartclk = 0;
			frameclks = integclks;
		} else {
			intstartclk = min_frclk - integclks;
			frameclks = min_frclk;
		}
		buf[i++] = intstartclk >> 8;
		buf[i++] = intstartclk;
		buf[i++] = frameclks >> 8;
		buf[i++] = frameclks;
		buf[i++] = sd->gain;
		break;
	default:				/* cmos */
/*	case SENSOR_MI0360: */
/*	case SENSOR_MT9V111: */
		cmd |= 0x0100;
		sensor = &sensor_tb[sd->sensor];
		buf[i++] = sensor->i2c_addr;	/* i2c_slave_addr */
		buf[i++] = 0x08;	/* 2 * ni2c */
		buf[i++] = 0x09;	/* reg = shutter width */
		buf[i++] = integclks >> 8; /* val H */
		buf[i++] = sensor->i2c_dum;
		buf[i++] = integclks;	/* val L */
		buf[i++] = 0x35;	/* reg = global gain */
		buf[i++] = 0x00;	/* val H */
		buf[i++] = sensor->i2c_dum;
		buf[i++] = 0x80 + sd->gain / 2; /* val L */
		buf[i++] = 0x00;
		buf[i++] = 0x00;
		buf[i++] = 0x00;
		buf[i++] = 0x00;
		buf[i++] = 0x83;
		break;
	}
	reg_wb(gspca_dev, cmd, 0, buf, i);
}

/* This function is called at probe time just before sd_init */
static int sd_config(struct gspca_dev *gspca_dev,
		const struct usb_device_id *id)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct cam *cam = &gspca_dev->cam;

	sd->sensor = id->driver_info >> 8;
	sd->type = id->driver_info;

	cam->cam_mode = vga_mode;
	cam->nmodes = ARRAY_SIZE(vga_mode);

	cam->bulk = 1;

	sd->gain = GAIN_DEF;
	sd->expo = EXPO_DEF;

	return 0;
}

/* this function is called at probe and resume time */
static int sd_init(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->gpio[0] = sd->gpio[1] = 0xff;	/* force gpio rewrite */

/*fixme: is this needed for icx098bp and mi0360?
	if (sd->sensor != SENSOR_LZ24BP)
		reg_w(gspca_dev, SQ930_CTRL_RESET, 0x0000);
 */

	reg_r(gspca_dev, SQ930_CTRL_GET_DEV_INFO, 8);
	if (gspca_dev->usb_err < 0)
		return gspca_dev->usb_err;

/* it returns:
 * 03 00 12 93 0b f6 c9 00	live! ultra
 * 03 00 07 93 0b f6 ca 00	live! ultra for notebook
 * 03 00 12 93 0b fe c8 00	Trust WB-3500T
 * 02 00 06 93 0b fe c8 00	Joy-IT 318S
 * 03 00 12 93 0b f6 cf 00	icam tracer - sensor icx098bq
 * 02 00 12 93 0b fe cf 00	ProQ Motion Webcam
 *
 * byte
 * 0: 02 = usb 1.0 (12Mbit) / 03 = usb2.0 (480Mbit)
 * 1: 00
 * 2: 06 / 07 / 12 = mode webcam? firmware??
 * 3: 93 chip = 930b (930b or 930c)
 * 4: 0b
 * 5: f6 = cdd (icx098bq, lz24bp) / fe or de = cmos (i2c) (other sensors)
 * 6: c8 / c9 / ca / cf = mode webcam?, sensor? webcam?
 * 7: 00
 */
	PDEBUG(D_PROBE, "info: %02x %02x %02x %02x %02x %02x %02x %02x",
			gspca_dev->usb_buf[0],
			gspca_dev->usb_buf[1],
			gspca_dev->usb_buf[2],
			gspca_dev->usb_buf[3],
			gspca_dev->usb_buf[4],
			gspca_dev->usb_buf[5],
			gspca_dev->usb_buf[6],
			gspca_dev->usb_buf[7]);

	bridge_init(sd);

	if (sd->sensor == SENSOR_MI0360) {

		/* no sensor probe for icam tracer */
		if (gspca_dev->usb_buf[5] == 0xf6)	/* if ccd */
			sd->sensor = SENSOR_ICX098BQ;
		else
			cmos_probe(gspca_dev);
	}
	if (gspca_dev->usb_err >= 0) {
		PDEBUG(D_PROBE, "Sensor %s", sensor_tb[sd->sensor].name);
		global_init(sd, 1);
	}
	return gspca_dev->usb_err;
}

/* send the start/stop commands to the webcam */
static void send_start(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	const struct cap_s *cap;
	int mode;

	mode = gspca_dev->cam.cam_mode[gspca_dev->curr_mode].priv;
	cap = &capconfig[sd->sensor][mode];
	reg_wb(gspca_dev, 0x0900 | SQ930_CTRL_CAP_START,
			0x0a00 | cap->cc_sizeid,
			cap->cc_bytes, 32);
}

static void send_stop(struct gspca_dev *gspca_dev)
{
	reg_w(gspca_dev, SQ930_CTRL_CAP_STOP, 0);
}

/* function called at start time before URB creation */
static int sd_isoc_init(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	gspca_dev->cam.bulk_nurbs = 1;	/* there must be one URB only */
	sd->do_ctrl = 0;
	gspca_dev->cam.bulk_size = gspca_dev->width * gspca_dev->height + 8;
	return 0;
}

/* start the capture */
static int sd_start(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int mode;

	bridge_init(sd);
	global_init(sd, 0);
	msleep(100);

	switch (sd->sensor) {
	case SENSOR_ICX098BQ:
		ucbus_write(gspca_dev, icx098bq_start_0,
				ARRAY_SIZE(icx098bq_start_0),
				8);
		ucbus_write(gspca_dev, icx098bq_start_1,
				ARRAY_SIZE(icx098bq_start_1),
				5);
		ucbus_write(gspca_dev, icx098bq_start_2,
				ARRAY_SIZE(icx098bq_start_2),
				6);
		msleep(50);

		/* 1st start */
		send_start(gspca_dev);
		gpio_set(sd, SQ930_GPIO_EXTRA2 | SQ930_GPIO_RSTBAR, 0x00ff);
		msleep(70);
		reg_w(gspca_dev, SQ930_CTRL_CAP_STOP, 0x0000);
		gpio_set(sd, 0x7f, 0x00ff);

		/* 2nd start */
		send_start(gspca_dev);
		gpio_set(sd, SQ930_GPIO_EXTRA2 | SQ930_GPIO_RSTBAR, 0x00ff);
		goto out;
	case SENSOR_LZ24BP:
		ucbus_write(gspca_dev, lz24bp_start_0,
				ARRAY_SIZE(lz24bp_start_0),
				8);
		if (sd->type != Creative_live_motion)
			ucbus_write(gspca_dev, lz24bp_start_1_gen,
					ARRAY_SIZE(lz24bp_start_1_gen),
					5);
		else
			ucbus_write(gspca_dev, lz24bp_start_1_clm,
					ARRAY_SIZE(lz24bp_start_1_clm),
					5);
		ucbus_write(gspca_dev, lz24bp_start_2,
				ARRAY_SIZE(lz24bp_start_2),
				6);
		mode = gspca_dev->cam.cam_mode[gspca_dev->curr_mode].priv;
		lz24bp_ppl(sd, mode == 1 ? 0x0564 : 0x0310);
		msleep(10);
		break;
	case SENSOR_MI0360:
		ucbus_write(gspca_dev, mi0360_start_0,
				ARRAY_SIZE(mi0360_start_0),
				8);
		i2c_write(sd, mi0360_init_23,
				ARRAY_SIZE(mi0360_init_23));
		i2c_write(sd, mi0360_init_24,
				ARRAY_SIZE(mi0360_init_24));
		i2c_write(sd, mi0360_init_25,
				ARRAY_SIZE(mi0360_init_25));
		ucbus_write(gspca_dev, mi0360_start_1,
				ARRAY_SIZE(mi0360_start_1),
				5);
		i2c_write(sd, mi0360_start_2,
				ARRAY_SIZE(mi0360_start_2));
		i2c_write(sd, mi0360_start_3,
				ARRAY_SIZE(mi0360_start_3));

		/* 1st start */
		send_start(gspca_dev);
		msleep(60);
		send_stop(gspca_dev);

		i2c_write(sd,
			mi0360_start_4, ARRAY_SIZE(mi0360_start_4));
		break;
	default:
/*	case SENSOR_MT9V111: */
		ucbus_write(gspca_dev, mi0360_start_0,
				ARRAY_SIZE(mi0360_start_0),
				8);
		i2c_write(sd, mt9v111_init_0,
				ARRAY_SIZE(mt9v111_init_0));
		i2c_write(sd, mt9v111_init_1,
				ARRAY_SIZE(mt9v111_init_1));
		i2c_write(sd, mt9v111_init_2,
				ARRAY_SIZE(mt9v111_init_2));
		ucbus_write(gspca_dev, mt9v111_start_1,
				ARRAY_SIZE(mt9v111_start_1),
				5);
		i2c_write(sd, mt9v111_init_3,
				ARRAY_SIZE(mt9v111_init_3));
		i2c_write(sd, mt9v111_init_4,
				ARRAY_SIZE(mt9v111_init_4));
		break;
	}

	send_start(gspca_dev);
out:
	msleep(1000);

	if (sd->sensor == SENSOR_MT9V111)
		gpio_set(sd, SQ930_GPIO_DFL_LED, SQ930_GPIO_DFL_LED);

	sd->do_ctrl = 1;	/* set the exposure */

	return gspca_dev->usb_err;
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (sd->sensor == SENSOR_MT9V111)
		gpio_set(sd, 0, SQ930_GPIO_DFL_LED);
	send_stop(gspca_dev);
}

/* function called when the application gets a new frame */
/* It sets the exposure if required and restart the bulk transfer. */
static void sd_dq_callback(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int ret;

	if (!sd->do_ctrl || gspca_dev->cam.bulk_nurbs != 0)
		return;
	sd->do_ctrl = 0;

	setexposure(gspca_dev);

	gspca_dev->cam.bulk_nurbs = 1;
	ret = usb_submit_urb(gspca_dev->urb[0], GFP_ATOMIC);
	if (ret < 0)
		pr_err("sd_dq_callback() err %d\n", ret);

	/* wait a little time, otherwise the webcam crashes */
	msleep(100);
}

static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			u8 *data,		/* isoc packet */
			int len)		/* iso packet length */
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (sd->do_ctrl)
		gspca_dev->cam.bulk_nurbs = 0;
	gspca_frame_add(gspca_dev, FIRST_PACKET, NULL, 0);
	gspca_frame_add(gspca_dev, INTER_PACKET, data, len - 8);
	gspca_frame_add(gspca_dev, LAST_PACKET, NULL, 0);
}

static int sd_setgain(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->gain = val;
	if (gspca_dev->streaming)
		sd->do_ctrl = 1;
	return 0;
}

static int sd_getgain(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->gain;
	return 0;
}
static int sd_setexpo(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->expo = val;
	if (gspca_dev->streaming)
		sd->do_ctrl = 1;
	return 0;
}

static int sd_getexpo(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->expo;
	return 0;
}

/* sub-driver description */
static const struct sd_desc sd_desc = {
	.name   = MODULE_NAME,
	.ctrls = sd_ctrls,
	.nctrls = ARRAY_SIZE(sd_ctrls),
	.config = sd_config,
	.init   = sd_init,
	.isoc_init = sd_isoc_init,
	.start  = sd_start,
	.stopN  = sd_stopN,
	.pkt_scan = sd_pkt_scan,
	.dq_callback = sd_dq_callback,
};

/* Table of supported USB devices */
#define ST(sensor, type) \
	.driver_info = (SENSOR_ ## sensor << 8) \
			| (type)
static const struct usb_device_id device_table[] = {
	{USB_DEVICE(0x041e, 0x4038), ST(MI0360, 0)},
	{USB_DEVICE(0x041e, 0x403c), ST(LZ24BP, 0)},
	{USB_DEVICE(0x041e, 0x403d), ST(LZ24BP, 0)},
	{USB_DEVICE(0x041e, 0x4041), ST(LZ24BP, Creative_live_motion)},
	{USB_DEVICE(0x2770, 0x930b), ST(MI0360, 0)},
	{USB_DEVICE(0x2770, 0x930c), ST(MI0360, 0)},
	{}
};
MODULE_DEVICE_TABLE(usb, device_table);


/* -- device connect -- */
static int sd_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	return gspca_dev_probe(intf, id, &sd_desc, sizeof(struct sd),
			THIS_MODULE);
}

static struct usb_driver sd_driver = {
	.name	    = MODULE_NAME,
	.id_table   = device_table,
	.probe	    = sd_probe,
	.disconnect = gspca_disconnect,
#ifdef CONFIG_PM
	.suspend    = gspca_suspend,
	.resume     = gspca_resume,
#endif
};

module_usb_driver(sd_driver);
