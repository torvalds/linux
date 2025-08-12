// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for GalaxyCore gc08a3 image sensor
 *
 * Copyright 2024 MediaTek
 *
 * Zhi Mao <zhi.mao@mediatek.com>
 */
#include <linux/array_size.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/container_of.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/math64.h>
#include <linux/mod_devicetable.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/types.h>
#include <linux/units.h>

#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#define GC08A3_REG_TEST_PATTERN_EN CCI_REG8(0x008c)
#define GC08A3_REG_TEST_PATTERN_IDX CCI_REG8(0x008d)
#define GC08A3_TEST_PATTERN_EN 0x01

#define GC08A3_STREAMING_REG CCI_REG8(0x0100)

#define GC08A3_FLIP_REG CCI_REG8(0x0101)
#define GC08A3_FLIP_H_MASK BIT(0)
#define GC08A3_FLIP_V_MASK BIT(1)

#define GC08A3_EXP_REG CCI_REG16(0x0202)
#define GC08A3_EXP_MARGIN 16
#define GC08A3_EXP_MIN 4
#define GC08A3_EXP_STEP 1

#define GC08A3_AGAIN_REG CCI_REG16(0x0204)
#define GC08A3_AGAIN_MIN 1024
#define GC08A3_AGAIN_MAX (1024 * 16)
#define GC08A3_AGAIN_STEP 1

#define GC08A3_FRAME_LENGTH_REG CCI_REG16(0x0340)
#define GC08A3_VTS_MAX 0xfff0

#define GC08A3_REG_CHIP_ID CCI_REG16(0x03f0)
#define GC08A3_CHIP_ID 0x08a3

#define GC08A3_NATIVE_WIDTH 3264
#define GC08A3_NATIVE_HEIGHT 2448

#define GC08A3_DEFAULT_CLK_FREQ (24 * HZ_PER_MHZ)
#define GC08A3_MBUS_CODE MEDIA_BUS_FMT_SRGGB10_1X10
#define GC08A3_DATA_LANES 4

#define GC08A3_RGB_DEPTH 10

#define GC08A3_SLEEP_US  (2 * USEC_PER_MSEC)

static const char *const gc08a3_test_pattern_menu[] = {
	"No Pattern", "Solid Black", "Colour Bar", "Solid White",
	"Solid Red", "Solid Green", "Solid Blue", "Solid Yellow",
};

static const s64 gc08a3_link_freq_menu_items[] = {
	(336 * HZ_PER_MHZ),
	(207 * HZ_PER_MHZ),
};

static const char *const gc08a3_supply_name[] = {
	"avdd",
	"dvdd",
	"dovdd",
};

struct gc08a3 {
	struct device *dev;
	struct v4l2_subdev sd;
	struct media_pad pad;

	struct clk *xclk;
	struct regulator_bulk_data supplies[ARRAY_SIZE(gc08a3_supply_name)];
	struct gpio_desc *reset_gpio;

	struct v4l2_ctrl_handler ctrls;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vflip;

	struct regmap *regmap;
	unsigned long link_freq_bitmap;
	const struct gc08a3_mode *cur_mode;
};

struct gc08a3_reg_list {
	u32 num_of_regs;
	const struct cci_reg_sequence *regs;
};

static const struct cci_reg_sequence mode_3264x2448[] = {
	/* system */
	{ CCI_REG8(0x0336), 0x70 },
	{ CCI_REG8(0x0383), 0xbb },
	{ CCI_REG8(0x0344), 0x00 },
	{ CCI_REG8(0x0345), 0x06 },
	{ CCI_REG8(0x0346), 0x00 },
	{ CCI_REG8(0x0347), 0x04 },
	{ CCI_REG8(0x0348), 0x0c },
	{ CCI_REG8(0x0349), 0xd0 },
	{ CCI_REG8(0x034a), 0x09 },
	{ CCI_REG8(0x034b), 0x9c },
	{ CCI_REG8(0x0202), 0x09 },
	{ CCI_REG8(0x0203), 0x04 },
	{ CCI_REG8(0x0340), 0x09 },
	{ CCI_REG8(0x0341), 0xf4 },
	{ CCI_REG8(0x0342), 0x07 },
	{ CCI_REG8(0x0343), 0x1c },

	{ CCI_REG8(0x0226), 0x00 },
	{ CCI_REG8(0x0227), 0x28 },
	{ CCI_REG8(0x0e38), 0x49 },
	{ CCI_REG8(0x0210), 0x13 },
	{ CCI_REG8(0x0218), 0x00 },
	{ CCI_REG8(0x0241), 0x88 },
	{ CCI_REG8(0x0392), 0x60 },

	/* ISP */
	{ CCI_REG8(0x00a2), 0x00 },
	{ CCI_REG8(0x00a3), 0x00 },
	{ CCI_REG8(0x00ab), 0x00 },
	{ CCI_REG8(0x00ac), 0x00 },

	/* GAIN */
	{ CCI_REG8(0x0204), 0x04 },
	{ CCI_REG8(0x0205), 0x00 },
	{ CCI_REG8(0x0050), 0x5c },
	{ CCI_REG8(0x0051), 0x44 },

	/* out window */
	{ CCI_REG8(0x009a), 0x66 },
	{ CCI_REG8(0x0351), 0x00 },
	{ CCI_REG8(0x0352), 0x06 },
	{ CCI_REG8(0x0353), 0x00 },
	{ CCI_REG8(0x0354), 0x08 },
	{ CCI_REG8(0x034c), 0x0c },
	{ CCI_REG8(0x034d), 0xc0 },
	{ CCI_REG8(0x034e), 0x09 },
	{ CCI_REG8(0x034f), 0x90 },

	/* MIPI */
	{ CCI_REG8(0x0114), 0x03 },
	{ CCI_REG8(0x0180), 0x65 },
	{ CCI_REG8(0x0181), 0xf0 },
	{ CCI_REG8(0x0185), 0x01 },
	{ CCI_REG8(0x0115), 0x30 },
	{ CCI_REG8(0x011b), 0x12 },
	{ CCI_REG8(0x011c), 0x12 },
	{ CCI_REG8(0x0121), 0x06 },
	{ CCI_REG8(0x0122), 0x06 },
	{ CCI_REG8(0x0123), 0x15 },
	{ CCI_REG8(0x0124), 0x01 },
	{ CCI_REG8(0x0125), 0x0b },
	{ CCI_REG8(0x0126), 0x08 },
	{ CCI_REG8(0x0129), 0x06 },
	{ CCI_REG8(0x012a), 0x08 },
	{ CCI_REG8(0x012b), 0x08 },

	{ CCI_REG8(0x0a73), 0x60 },
	{ CCI_REG8(0x0a70), 0x11 },
	{ CCI_REG8(0x0313), 0x80 },
	{ CCI_REG8(0x0aff), 0x00 },
	{ CCI_REG8(0x0a70), 0x00 },
	{ CCI_REG8(0x00a4), 0x80 },
	{ CCI_REG8(0x0316), 0x01 },
	{ CCI_REG8(0x0a67), 0x00 },
	{ CCI_REG8(0x0084), 0x10 },
	{ CCI_REG8(0x0102), 0x09 },
};

static const struct cci_reg_sequence mode_1920x1080[] = {
	/* system */
	{ CCI_REG8(0x0336), 0x45 },
	{ CCI_REG8(0x0383), 0x8b },
	{ CCI_REG8(0x0344), 0x02 },
	{ CCI_REG8(0x0345), 0xa6 },
	{ CCI_REG8(0x0346), 0x02 },
	{ CCI_REG8(0x0347), 0xb0 },
	{ CCI_REG8(0x0348), 0x07 },
	{ CCI_REG8(0x0349), 0x90 },
	{ CCI_REG8(0x034a), 0x04 },
	{ CCI_REG8(0x034b), 0x44 },
	{ CCI_REG8(0x0202), 0x03 },
	{ CCI_REG8(0x0203), 0x00 },
	{ CCI_REG8(0x0340), 0x04 },
	{ CCI_REG8(0x0341), 0xfc },
	{ CCI_REG8(0x0342), 0x07 },
	{ CCI_REG8(0x0343), 0x1c },
	{ CCI_REG8(0x0226), 0x00 },
	{ CCI_REG8(0x0227), 0x88 },
	{ CCI_REG8(0x0e38), 0x49 },
	{ CCI_REG8(0x0210), 0x13 },
	{ CCI_REG8(0x0218), 0x00 },
	{ CCI_REG8(0x0241), 0x88 },
	{ CCI_REG8(0x0392), 0x60 },

	/* ISP */
	{ CCI_REG8(0x00a2), 0xac },
	{ CCI_REG8(0x00a3), 0x02 },
	{ CCI_REG8(0x00ab), 0xa0 },
	{ CCI_REG8(0x00ac), 0x02 },

	/* GAIN */
	{ CCI_REG8(0x0204), 0x04 },
	{ CCI_REG8(0x0205), 0x00 },
	{ CCI_REG8(0x0050), 0x38 },
	{ CCI_REG8(0x0051), 0x20 },

	/* out window */
	{ CCI_REG8(0x009a), 0x66 },
	{ CCI_REG8(0x0351), 0x00 },
	{ CCI_REG8(0x0352), 0x06 },
	{ CCI_REG8(0x0353), 0x00 },
	{ CCI_REG8(0x0354), 0x08 },
	{ CCI_REG8(0x034c), 0x07 },
	{ CCI_REG8(0x034d), 0x80 },
	{ CCI_REG8(0x034e), 0x04 },
	{ CCI_REG8(0x034f), 0x38 },

	/* MIPI */
	{ CCI_REG8(0x0114), 0x03 },
	{ CCI_REG8(0x0180), 0x65 },
	{ CCI_REG8(0x0181), 0xf0 },
	{ CCI_REG8(0x0185), 0x01 },
	{ CCI_REG8(0x0115), 0x30 },
	{ CCI_REG8(0x011b), 0x12 },
	{ CCI_REG8(0x011c), 0x12 },
	{ CCI_REG8(0x0121), 0x02 },
	{ CCI_REG8(0x0122), 0x03 },
	{ CCI_REG8(0x0123), 0x0c },
	{ CCI_REG8(0x0124), 0x00 },
	{ CCI_REG8(0x0125), 0x09 },
	{ CCI_REG8(0x0126), 0x06 },
	{ CCI_REG8(0x0129), 0x04 },
	{ CCI_REG8(0x012a), 0x03 },
	{ CCI_REG8(0x012b), 0x06 },

	{ CCI_REG8(0x0a73), 0x60 },
	{ CCI_REG8(0x0a70), 0x11 },
	{ CCI_REG8(0x0313), 0x80 },
	{ CCI_REG8(0x0aff), 0x00 },
	{ CCI_REG8(0x0a70), 0x00 },
	{ CCI_REG8(0x00a4), 0x80 },
	{ CCI_REG8(0x0316), 0x01 },
	{ CCI_REG8(0x0a67), 0x00 },
	{ CCI_REG8(0x0084), 0x10 },
	{ CCI_REG8(0x0102), 0x09 },
};

static const struct cci_reg_sequence mode_table_common[] = {
	{ GC08A3_STREAMING_REG, 0x00 },
	/* system */
	{ CCI_REG8(0x031c), 0x60 },
	{ CCI_REG8(0x0337), 0x04 },
	{ CCI_REG8(0x0335), 0x51 },
	{ CCI_REG8(0x0336), 0x70 },
	{ CCI_REG8(0x0383), 0xbb },
	{ CCI_REG8(0x031a), 0x00 },
	{ CCI_REG8(0x0321), 0x10 },
	{ CCI_REG8(0x0327), 0x03 },
	{ CCI_REG8(0x0325), 0x40 },
	{ CCI_REG8(0x0326), 0x23 },
	{ CCI_REG8(0x0314), 0x11 },
	{ CCI_REG8(0x0315), 0xd6 },
	{ CCI_REG8(0x0316), 0x01 },
	{ CCI_REG8(0x0334), 0x40 },
	{ CCI_REG8(0x0324), 0x42 },
	{ CCI_REG8(0x031c), 0x00 },
	{ CCI_REG8(0x031c), 0x9f },
	{ CCI_REG8(0x039a), 0x13 },
	{ CCI_REG8(0x0084), 0x30 },
	{ CCI_REG8(0x02b3), 0x08 },
	{ CCI_REG8(0x0057), 0x0c },
	{ CCI_REG8(0x05c3), 0x50 },
	{ CCI_REG8(0x0311), 0x90 },
	{ CCI_REG8(0x05a0), 0x02 },
	{ CCI_REG8(0x0074), 0x0a },
	{ CCI_REG8(0x0059), 0x11 },
	{ CCI_REG8(0x0070), 0x05 },
	{ CCI_REG8(0x0101), 0x00 },

	/* analog */
	{ CCI_REG8(0x0344), 0x00 },
	{ CCI_REG8(0x0345), 0x06 },
	{ CCI_REG8(0x0346), 0x00 },
	{ CCI_REG8(0x0347), 0x04 },
	{ CCI_REG8(0x0348), 0x0c },
	{ CCI_REG8(0x0349), 0xd0 },
	{ CCI_REG8(0x034a), 0x09 },
	{ CCI_REG8(0x034b), 0x9c },
	{ CCI_REG8(0x0202), 0x09 },
	{ CCI_REG8(0x0203), 0x04 },

	{ CCI_REG8(0x0219), 0x05 },
	{ CCI_REG8(0x0226), 0x00 },
	{ CCI_REG8(0x0227), 0x28 },
	{ CCI_REG8(0x0e0a), 0x00 },
	{ CCI_REG8(0x0e0b), 0x00 },
	{ CCI_REG8(0x0e24), 0x04 },
	{ CCI_REG8(0x0e25), 0x04 },
	{ CCI_REG8(0x0e26), 0x00 },
	{ CCI_REG8(0x0e27), 0x10 },
	{ CCI_REG8(0x0e01), 0x74 },
	{ CCI_REG8(0x0e03), 0x47 },
	{ CCI_REG8(0x0e04), 0x33 },
	{ CCI_REG8(0x0e05), 0x44 },
	{ CCI_REG8(0x0e06), 0x44 },
	{ CCI_REG8(0x0e0c), 0x1e },
	{ CCI_REG8(0x0e17), 0x3a },
	{ CCI_REG8(0x0e18), 0x3c },
	{ CCI_REG8(0x0e19), 0x40 },
	{ CCI_REG8(0x0e1a), 0x42 },
	{ CCI_REG8(0x0e28), 0x21 },
	{ CCI_REG8(0x0e2b), 0x68 },
	{ CCI_REG8(0x0e2c), 0x0d },
	{ CCI_REG8(0x0e2d), 0x08 },
	{ CCI_REG8(0x0e34), 0xf4 },
	{ CCI_REG8(0x0e35), 0x44 },
	{ CCI_REG8(0x0e36), 0x07 },
	{ CCI_REG8(0x0e38), 0x49 },
	{ CCI_REG8(0x0210), 0x13 },
	{ CCI_REG8(0x0218), 0x00 },
	{ CCI_REG8(0x0241), 0x88 },
	{ CCI_REG8(0x0e32), 0x00 },
	{ CCI_REG8(0x0e33), 0x18 },
	{ CCI_REG8(0x0e42), 0x03 },
	{ CCI_REG8(0x0e43), 0x80 },
	{ CCI_REG8(0x0e44), 0x04 },
	{ CCI_REG8(0x0e45), 0x00 },
	{ CCI_REG8(0x0e4f), 0x04 },
	{ CCI_REG8(0x057a), 0x20 },
	{ CCI_REG8(0x0381), 0x7c },
	{ CCI_REG8(0x0382), 0x9b },
	{ CCI_REG8(0x0384), 0xfb },
	{ CCI_REG8(0x0389), 0x38 },
	{ CCI_REG8(0x038a), 0x03 },
	{ CCI_REG8(0x0390), 0x6a },
	{ CCI_REG8(0x0391), 0x0b },
	{ CCI_REG8(0x0392), 0x60 },
	{ CCI_REG8(0x0393), 0xc1 },
	{ CCI_REG8(0x0396), 0xff },
	{ CCI_REG8(0x0398), 0x62 },

	/* cisctl reset */
	{ CCI_REG8(0x031c), 0x80 },
	{ CCI_REG8(0x03fe), 0x10 },
	{ CCI_REG8(0x03fe), 0x00 },
	{ CCI_REG8(0x031c), 0x9f },
	{ CCI_REG8(0x03fe), 0x00 },
	{ CCI_REG8(0x03fe), 0x00 },
	{ CCI_REG8(0x03fe), 0x00 },
	{ CCI_REG8(0x03fe), 0x00 },
	{ CCI_REG8(0x031c), 0x80 },
	{ CCI_REG8(0x03fe), 0x10 },
	{ CCI_REG8(0x03fe), 0x00 },
	{ CCI_REG8(0x031c), 0x9f },
	{ CCI_REG8(0x0360), 0x01 },
	{ CCI_REG8(0x0360), 0x00 },
	{ CCI_REG8(0x0316), 0x09 },
	{ CCI_REG8(0x0a67), 0x80 },
	{ CCI_REG8(0x0313), 0x00 },
	{ CCI_REG8(0x0a53), 0x0e },
	{ CCI_REG8(0x0a65), 0x17 },
	{ CCI_REG8(0x0a68), 0xa1 },
	{ CCI_REG8(0x0a58), 0x00 },
	{ CCI_REG8(0x0ace), 0x0c },
	{ CCI_REG8(0x00a4), 0x00 },
	{ CCI_REG8(0x00a5), 0x01 },
	{ CCI_REG8(0x00a7), 0x09 },
	{ CCI_REG8(0x00a8), 0x9c },
	{ CCI_REG8(0x00a9), 0x0c },
	{ CCI_REG8(0x00aa), 0xd0 },
	{ CCI_REG8(0x0a8a), 0x00 },
	{ CCI_REG8(0x0a8b), 0xe0 },
	{ CCI_REG8(0x0a8c), 0x13 },
	{ CCI_REG8(0x0a8d), 0xe8 },
	{ CCI_REG8(0x0a90), 0x0a },
	{ CCI_REG8(0x0a91), 0x10 },
	{ CCI_REG8(0x0a92), 0xf8 },
	{ CCI_REG8(0x0a71), 0xf2 },
	{ CCI_REG8(0x0a72), 0x12 },
	{ CCI_REG8(0x0a73), 0x64 },
	{ CCI_REG8(0x0a75), 0x41 },
	{ CCI_REG8(0x0a70), 0x07 },
	{ CCI_REG8(0x0313), 0x80 },

	/* ISP */
	{ CCI_REG8(0x00a0), 0x01 },
	{ CCI_REG8(0x0080), 0xd2 },
	{ CCI_REG8(0x0081), 0x3f },
	{ CCI_REG8(0x0087), 0x51 },
	{ CCI_REG8(0x0089), 0x03 },
	{ CCI_REG8(0x009b), 0x40 },
	{ CCI_REG8(0x05a0), 0x82 },
	{ CCI_REG8(0x05ac), 0x00 },
	{ CCI_REG8(0x05ad), 0x01 },
	{ CCI_REG8(0x05ae), 0x00 },
	{ CCI_REG8(0x0800), 0x0a },
	{ CCI_REG8(0x0801), 0x14 },
	{ CCI_REG8(0x0802), 0x28 },
	{ CCI_REG8(0x0803), 0x34 },
	{ CCI_REG8(0x0804), 0x0e },
	{ CCI_REG8(0x0805), 0x33 },
	{ CCI_REG8(0x0806), 0x03 },
	{ CCI_REG8(0x0807), 0x8a },
	{ CCI_REG8(0x0808), 0x50 },
	{ CCI_REG8(0x0809), 0x00 },
	{ CCI_REG8(0x080a), 0x34 },
	{ CCI_REG8(0x080b), 0x03 },
	{ CCI_REG8(0x080c), 0x26 },
	{ CCI_REG8(0x080d), 0x03 },
	{ CCI_REG8(0x080e), 0x18 },
	{ CCI_REG8(0x080f), 0x03 },
	{ CCI_REG8(0x0810), 0x10 },
	{ CCI_REG8(0x0811), 0x03 },
	{ CCI_REG8(0x0812), 0x00 },
	{ CCI_REG8(0x0813), 0x00 },
	{ CCI_REG8(0x0814), 0x01 },
	{ CCI_REG8(0x0815), 0x00 },
	{ CCI_REG8(0x0816), 0x01 },
	{ CCI_REG8(0x0817), 0x00 },
	{ CCI_REG8(0x0818), 0x00 },
	{ CCI_REG8(0x0819), 0x0a },
	{ CCI_REG8(0x081a), 0x01 },
	{ CCI_REG8(0x081b), 0x6c },
	{ CCI_REG8(0x081c), 0x00 },
	{ CCI_REG8(0x081d), 0x0b },
	{ CCI_REG8(0x081e), 0x02 },
	{ CCI_REG8(0x081f), 0x00 },
	{ CCI_REG8(0x0820), 0x00 },
	{ CCI_REG8(0x0821), 0x0c },
	{ CCI_REG8(0x0822), 0x02 },
	{ CCI_REG8(0x0823), 0xd9 },
	{ CCI_REG8(0x0824), 0x00 },
	{ CCI_REG8(0x0825), 0x0d },
	{ CCI_REG8(0x0826), 0x03 },
	{ CCI_REG8(0x0827), 0xf0 },
	{ CCI_REG8(0x0828), 0x00 },
	{ CCI_REG8(0x0829), 0x0e },
	{ CCI_REG8(0x082a), 0x05 },
	{ CCI_REG8(0x082b), 0x94 },
	{ CCI_REG8(0x082c), 0x09 },
	{ CCI_REG8(0x082d), 0x6e },
	{ CCI_REG8(0x082e), 0x07 },
	{ CCI_REG8(0x082f), 0xe6 },
	{ CCI_REG8(0x0830), 0x10 },
	{ CCI_REG8(0x0831), 0x0e },
	{ CCI_REG8(0x0832), 0x0b },
	{ CCI_REG8(0x0833), 0x2c },
	{ CCI_REG8(0x0834), 0x14 },
	{ CCI_REG8(0x0835), 0xae },
	{ CCI_REG8(0x0836), 0x0f },
	{ CCI_REG8(0x0837), 0xc4 },
	{ CCI_REG8(0x0838), 0x18 },
	{ CCI_REG8(0x0839), 0x0e },
	{ CCI_REG8(0x05ac), 0x01 },
	{ CCI_REG8(0x059a), 0x00 },
	{ CCI_REG8(0x059b), 0x00 },
	{ CCI_REG8(0x059c), 0x01 },
	{ CCI_REG8(0x0598), 0x00 },
	{ CCI_REG8(0x0597), 0x14 },
	{ CCI_REG8(0x05ab), 0x09 },
	{ CCI_REG8(0x05a4), 0x02 },
	{ CCI_REG8(0x05a3), 0x05 },
	{ CCI_REG8(0x05a0), 0xc2 },
	{ CCI_REG8(0x0207), 0xc4 },

	/* GAIN */
	{ CCI_REG8(0x0208), 0x01 },
	{ CCI_REG8(0x0209), 0x72 },
	{ CCI_REG8(0x0204), 0x04 },
	{ CCI_REG8(0x0205), 0x00 },

	{ CCI_REG8(0x0040), 0x22 },
	{ CCI_REG8(0x0041), 0x20 },
	{ CCI_REG8(0x0043), 0x10 },
	{ CCI_REG8(0x0044), 0x00 },
	{ CCI_REG8(0x0046), 0x08 },
	{ CCI_REG8(0x0047), 0xf0 },
	{ CCI_REG8(0x0048), 0x0f },
	{ CCI_REG8(0x004b), 0x0f },
	{ CCI_REG8(0x004c), 0x00 },
	{ CCI_REG8(0x0050), 0x5c },
	{ CCI_REG8(0x0051), 0x44 },
	{ CCI_REG8(0x005b), 0x03 },
	{ CCI_REG8(0x00c0), 0x00 },
	{ CCI_REG8(0x00c1), 0x80 },
	{ CCI_REG8(0x00c2), 0x31 },
	{ CCI_REG8(0x00c3), 0x00 },
	{ CCI_REG8(0x0460), 0x04 },
	{ CCI_REG8(0x0462), 0x08 },
	{ CCI_REG8(0x0464), 0x0e },
	{ CCI_REG8(0x0466), 0x0a },
	{ CCI_REG8(0x0468), 0x12 },
	{ CCI_REG8(0x046a), 0x12 },
	{ CCI_REG8(0x046c), 0x10 },
	{ CCI_REG8(0x046e), 0x0c },
	{ CCI_REG8(0x0461), 0x03 },
	{ CCI_REG8(0x0463), 0x03 },
	{ CCI_REG8(0x0465), 0x03 },
	{ CCI_REG8(0x0467), 0x03 },
	{ CCI_REG8(0x0469), 0x04 },
	{ CCI_REG8(0x046b), 0x04 },
	{ CCI_REG8(0x046d), 0x04 },
	{ CCI_REG8(0x046f), 0x04 },
	{ CCI_REG8(0x0470), 0x04 },
	{ CCI_REG8(0x0472), 0x10 },
	{ CCI_REG8(0x0474), 0x26 },
	{ CCI_REG8(0x0476), 0x38 },
	{ CCI_REG8(0x0478), 0x20 },
	{ CCI_REG8(0x047a), 0x30 },
	{ CCI_REG8(0x047c), 0x38 },
	{ CCI_REG8(0x047e), 0x60 },
	{ CCI_REG8(0x0471), 0x05 },
	{ CCI_REG8(0x0473), 0x05 },
	{ CCI_REG8(0x0475), 0x05 },
	{ CCI_REG8(0x0477), 0x05 },
	{ CCI_REG8(0x0479), 0x04 },
	{ CCI_REG8(0x047b), 0x04 },
	{ CCI_REG8(0x047d), 0x04 },
	{ CCI_REG8(0x047f), 0x04 },
};

struct gc08a3_mode {
	u32 width;
	u32 height;
	const struct gc08a3_reg_list reg_list;

	u32 hts; /* Horizontal timining size */
	u32 vts_def; /* Default vertical timining size */
	u32 vts_min; /* Min vertical timining size */
};

/* Declare modes in order, from biggest to smallest height. */
static const struct gc08a3_mode gc08a3_modes[] = {
	{
		/* 3264*2448@30fps */
		.width = GC08A3_NATIVE_WIDTH,
		.height = GC08A3_NATIVE_HEIGHT,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_3264x2448),
			.regs = mode_3264x2448,
		},
		.hts = 3640,
		.vts_def = 2548,
		.vts_min = 2548,
	},
	{
		/* 1920*1080@60fps */
		.width = 1920,
		.height = 1080,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_1920x1080),
			.regs = mode_1920x1080,
		},
		.hts = 3640,
		.vts_def = 1276,
		.vts_min = 1276,
	},
};

static inline struct gc08a3 *to_gc08a3(struct v4l2_subdev *sd)
{
	return container_of(sd, struct gc08a3, sd);
}

static int gc08a3_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct gc08a3 *gc08a3 = to_gc08a3(sd);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(gc08a3_supply_name),
				    gc08a3->supplies);
	if (ret < 0) {
		dev_err(gc08a3->dev, "failed to enable regulators: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(gc08a3->xclk);
	if (ret < 0) {
		regulator_bulk_disable(ARRAY_SIZE(gc08a3_supply_name),
				       gc08a3->supplies);
		dev_err(gc08a3->dev, "clk prepare enable failed\n");
		return ret;
	}

	fsleep(GC08A3_SLEEP_US);

	gpiod_set_value_cansleep(gc08a3->reset_gpio, 0);
	fsleep(GC08A3_SLEEP_US);

	return 0;
}

static int gc08a3_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct gc08a3 *gc08a3 = to_gc08a3(sd);

	clk_disable_unprepare(gc08a3->xclk);
	gpiod_set_value_cansleep(gc08a3->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(gc08a3_supply_name),
			       gc08a3->supplies);

	return 0;
}

static int gc08a3_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = GC08A3_MBUS_CODE;

	return 0;
}

static int gc08a3_enum_frame_size(struct v4l2_subdev *subdev,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->code != GC08A3_MBUS_CODE)
		return -EINVAL;

	if (fse->index >= ARRAY_SIZE(gc08a3_modes))
		return -EINVAL;

	fse->min_width = gc08a3_modes[fse->index].width;
	fse->max_width = gc08a3_modes[fse->index].width;
	fse->min_height = gc08a3_modes[fse->index].height;
	fse->max_height = gc08a3_modes[fse->index].height;

	return 0;
}

static int gc08a3_update_cur_mode_controls(struct gc08a3 *gc08a3,
					   const struct gc08a3_mode *mode)
{
	s64 exposure_max, h_blank;
	int ret;

	ret = __v4l2_ctrl_modify_range(gc08a3->vblank,
				       mode->vts_min - mode->height,
				       GC08A3_VTS_MAX - mode->height, 1,
				       mode->vts_def - mode->height);
	if (ret) {
		dev_err(gc08a3->dev, "VB ctrl range update failed\n");
		return ret;
	}

	h_blank = mode->hts - mode->width;
	ret = __v4l2_ctrl_modify_range(gc08a3->hblank, h_blank, h_blank, 1,
				       h_blank);
	if (ret) {
		dev_err(gc08a3->dev, "HB ctrl range update failed\n");
		return ret;
	}

	exposure_max = mode->vts_def - GC08A3_EXP_MARGIN;
	ret = __v4l2_ctrl_modify_range(gc08a3->exposure, GC08A3_EXP_MIN,
				       exposure_max, GC08A3_EXP_STEP,
				       exposure_max);
	if (ret) {
		dev_err(gc08a3->dev, "exposure ctrl range update failed\n");
		return ret;
	}

	return 0;
}

static void gc08a3_update_pad_format(struct gc08a3 *gc08a3,
				     const struct gc08a3_mode *mode,
				     struct v4l2_mbus_framefmt *fmt)
{
	fmt->width = mode->width;
	fmt->height = mode->height;
	fmt->code = GC08A3_MBUS_CODE;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_RAW;
	fmt->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(fmt->colorspace);
	fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	fmt->xfer_func = V4L2_XFER_FUNC_NONE;
}

static int gc08a3_set_format(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state,
			     struct v4l2_subdev_format *fmt)
{
	struct gc08a3 *gc08a3 = to_gc08a3(sd);
	struct v4l2_mbus_framefmt *mbus_fmt;
	struct v4l2_rect *crop;
	const struct gc08a3_mode *mode;

	mode = v4l2_find_nearest_size(gc08a3_modes, ARRAY_SIZE(gc08a3_modes),
				      width, height, fmt->format.width,
				      fmt->format.height);

	/* update crop info to subdev state */
	crop = v4l2_subdev_state_get_crop(state, 0);
	crop->width = mode->width;
	crop->height = mode->height;

	/* update fmt info to subdev state */
	gc08a3_update_pad_format(gc08a3, mode, &fmt->format);
	mbus_fmt = v4l2_subdev_state_get_format(state, 0);
	*mbus_fmt = fmt->format;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	gc08a3->cur_mode = mode;
	gc08a3_update_cur_mode_controls(gc08a3, mode);

	return 0;
}

static int gc08a3_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_selection *sel)
{
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP:
		sel->r = *v4l2_subdev_state_get_crop(state, 0);
		break;
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.top = 0;
		sel->r.left = 0;
		sel->r.width = GC08A3_NATIVE_WIDTH;
		sel->r.height = GC08A3_NATIVE_HEIGHT;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int gc08a3_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state)
{
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_TRY,
		.pad = 0,
		.format = {
			.code = GC08A3_MBUS_CODE,
			.width = gc08a3_modes[0].width,
			.height = gc08a3_modes[0].height,
		},
	};

	gc08a3_set_format(sd, state, &fmt);

	return 0;
}

static int gc08a3_set_ctrl_hflip(struct gc08a3 *gc08a3, u32 ctrl_val)
{
	int ret;
	u64 val;

	ret = cci_read(gc08a3->regmap, GC08A3_FLIP_REG, &val, NULL);
	if (ret) {
		dev_err(gc08a3->dev, "read hflip register failed: %d\n", ret);
		return ret;
	}

	return cci_update_bits(gc08a3->regmap, GC08A3_FLIP_REG,
			       GC08A3_FLIP_H_MASK,
			       ctrl_val ? GC08A3_FLIP_H_MASK : 0, NULL);
}

static int gc08a3_set_ctrl_vflip(struct gc08a3 *gc08a3, u32 ctrl_val)
{
	int ret;
	u64 val;

	ret = cci_read(gc08a3->regmap, GC08A3_FLIP_REG, &val, NULL);
	if (ret) {
		dev_err(gc08a3->dev, "read vflip register failed: %d\n", ret);
		return ret;
	}

	return cci_update_bits(gc08a3->regmap, GC08A3_FLIP_REG,
			       GC08A3_FLIP_V_MASK,
			       ctrl_val ? GC08A3_FLIP_V_MASK : 0, NULL);
}

static int gc08a3_test_pattern(struct gc08a3 *gc08a3, u32 pattern_menu)
{
	u32 pattern;
	int ret;

	if (pattern_menu) {
		switch (pattern_menu) {
		case 1:
			pattern = 0x00;
			break;
		case 2:
			pattern = 0x10;
			break;
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
			pattern = pattern_menu + 1;
			break;
		default:
			pattern = 0x00;
			break;
		}

		ret = cci_write(gc08a3->regmap, GC08A3_REG_TEST_PATTERN_IDX,
				pattern, NULL);
		if (ret)
			return ret;

		return cci_write(gc08a3->regmap, GC08A3_REG_TEST_PATTERN_EN,
				 GC08A3_TEST_PATTERN_EN, NULL);
	} else {
		return cci_write(gc08a3->regmap, GC08A3_REG_TEST_PATTERN_EN,
				 0x00, NULL);
	}
}

static int gc08a3_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gc08a3 *gc08a3 =
		container_of(ctrl->handler, struct gc08a3, ctrls);
	int ret = 0;
	s64 exposure_max;
	struct v4l2_subdev_state *state;
	const struct v4l2_mbus_framefmt *format;

	state = v4l2_subdev_get_locked_active_state(&gc08a3->sd);
	format = v4l2_subdev_state_get_format(state, 0);

	if (ctrl->id == V4L2_CID_VBLANK) {
		/* Update max exposure while meeting expected vblanking */
		exposure_max = format->height + ctrl->val - GC08A3_EXP_MARGIN;
		__v4l2_ctrl_modify_range(gc08a3->exposure,
					 gc08a3->exposure->minimum,
					 exposure_max, gc08a3->exposure->step,
					 exposure_max);
	}

	/*
	 * Applying V4L2 control value only happens
	 * when power is on for streaming.
	 */
	if (!pm_runtime_get_if_active(gc08a3->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		ret = cci_write(gc08a3->regmap, GC08A3_EXP_REG,
				ctrl->val, NULL);
		break;

	case V4L2_CID_ANALOGUE_GAIN:
		ret = cci_write(gc08a3->regmap, GC08A3_AGAIN_REG,
				ctrl->val, NULL);
		break;

	case V4L2_CID_VBLANK:
		ret = cci_write(gc08a3->regmap, GC08A3_FRAME_LENGTH_REG,
				gc08a3->cur_mode->height + ctrl->val, NULL);
		break;

	case V4L2_CID_HFLIP:
		ret = gc08a3_set_ctrl_hflip(gc08a3, ctrl->val);
		break;

	case V4L2_CID_VFLIP:
		ret = gc08a3_set_ctrl_vflip(gc08a3, ctrl->val);
		break;

	case V4L2_CID_TEST_PATTERN:
		ret = gc08a3_test_pattern(gc08a3, ctrl->val);
		break;

	default:
		break;
	}

	pm_runtime_put(gc08a3->dev);

	return ret;
}

static const struct v4l2_ctrl_ops gc08a3_ctrl_ops = {
	.s_ctrl = gc08a3_set_ctrl,
};

static int gc08a3_start_streaming(struct gc08a3 *gc08a3)
{
	const struct gc08a3_mode *mode;
	const struct gc08a3_reg_list *reg_list;
	int ret;

	ret = pm_runtime_resume_and_get(gc08a3->dev);
	if (ret < 0)
		return ret;

	ret = cci_multi_reg_write(gc08a3->regmap,
				  mode_table_common,
				  ARRAY_SIZE(mode_table_common), NULL);
	if (ret)
		goto err_rpm_put;

	mode = gc08a3->cur_mode;
	reg_list = &mode->reg_list;
	ret = cci_multi_reg_write(gc08a3->regmap,
				  reg_list->regs, reg_list->num_of_regs, NULL);
	if (ret < 0)
		goto err_rpm_put;

	ret = __v4l2_ctrl_handler_setup(&gc08a3->ctrls);
	if (ret < 0) {
		dev_err(gc08a3->dev, "could not sync v4l2 controls\n");
		goto err_rpm_put;
	}

	ret = cci_write(gc08a3->regmap, GC08A3_STREAMING_REG, 1, NULL);
	if (ret < 0) {
		dev_err(gc08a3->dev, "write STREAMING_REG failed: %d\n", ret);
		goto err_rpm_put;
	}

	return 0;

err_rpm_put:
	pm_runtime_put(gc08a3->dev);
	return ret;
}

static int gc08a3_stop_streaming(struct gc08a3 *gc08a3)
{
	int ret;

	ret = cci_write(gc08a3->regmap, GC08A3_STREAMING_REG, 0, NULL);
	if (ret < 0)
		dev_err(gc08a3->dev, "could not sent stop streaming %d\n", ret);

	pm_runtime_put(gc08a3->dev);
	return ret;
}

static int gc08a3_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct gc08a3 *gc08a3 = to_gc08a3(subdev);
	struct v4l2_subdev_state *state;
	int ret;

	state = v4l2_subdev_lock_and_get_active_state(subdev);

	if (enable)
		ret = gc08a3_start_streaming(gc08a3);
	else
		ret = gc08a3_stop_streaming(gc08a3);

	v4l2_subdev_unlock_state(state);

	return ret;
}

static const struct v4l2_subdev_video_ops gc08a3_video_ops = {
	.s_stream = gc08a3_s_stream,
};

static const struct v4l2_subdev_pad_ops gc08a3_subdev_pad_ops = {
	.enum_mbus_code = gc08a3_enum_mbus_code,
	.enum_frame_size = gc08a3_enum_frame_size,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = gc08a3_set_format,
	.get_selection = gc08a3_get_selection,
};

static const struct v4l2_subdev_ops gc08a3_subdev_ops = {
	.video = &gc08a3_video_ops,
	.pad = &gc08a3_subdev_pad_ops,
};

static const struct v4l2_subdev_internal_ops gc08a3_internal_ops = {
	.init_state = gc08a3_init_state,
};

static int gc08a3_get_regulators(struct device *dev, struct gc08a3 *gc08a3)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(gc08a3_supply_name); i++)
		gc08a3->supplies[i].supply = gc08a3_supply_name[i];

	return devm_regulator_bulk_get(dev, ARRAY_SIZE(gc08a3_supply_name),
				       gc08a3->supplies);
}

static int gc08a3_parse_fwnode(struct gc08a3 *gc08a3)
{
	struct fwnode_handle *endpoint;
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY,
	};
	int ret;
	struct device *dev = gc08a3->dev;

	endpoint =
		fwnode_graph_get_endpoint_by_id(dev_fwnode(dev), 0, 0,
						FWNODE_GRAPH_ENDPOINT_NEXT);
	if (!endpoint) {
		dev_err(dev, "endpoint node not found\n");
		return -EINVAL;
	}

	ret = v4l2_fwnode_endpoint_alloc_parse(endpoint, &bus_cfg);
	if (ret) {
		dev_err(dev, "parsing endpoint node failed\n");
		goto done;
	}

	ret = v4l2_link_freq_to_bitmap(dev, bus_cfg.link_frequencies,
				       bus_cfg.nr_of_link_frequencies,
				       gc08a3_link_freq_menu_items,
				       ARRAY_SIZE(gc08a3_link_freq_menu_items),
				       &gc08a3->link_freq_bitmap);
	if (ret)
		goto done;

done:
	v4l2_fwnode_endpoint_free(&bus_cfg);
	fwnode_handle_put(endpoint);
	return ret;
}

static u64 gc08a3_to_pixel_rate(u32 f_index)
{
	u64 pixel_rate =
		gc08a3_link_freq_menu_items[f_index] * 2 * GC08A3_DATA_LANES;

	return div_u64(pixel_rate, GC08A3_RGB_DEPTH);
}

static int gc08a3_init_controls(struct gc08a3 *gc08a3)
{
	struct i2c_client *client = v4l2_get_subdevdata(&gc08a3->sd);
	const struct gc08a3_mode *mode = &gc08a3_modes[0];
	const struct v4l2_ctrl_ops *ops = &gc08a3_ctrl_ops;
	struct v4l2_fwnode_device_properties props;
	struct v4l2_ctrl_handler *ctrl_hdlr;
	s64 exposure_max, h_blank;
	int ret;

	ctrl_hdlr = &gc08a3->ctrls;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 9);
	if (ret)
		return ret;

	gc08a3->hflip = v4l2_ctrl_new_std(ctrl_hdlr, &gc08a3_ctrl_ops,
					  V4L2_CID_HFLIP, 0, 1, 1, 0);
	gc08a3->vflip = v4l2_ctrl_new_std(ctrl_hdlr, &gc08a3_ctrl_ops,
					  V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_cluster(2, &gc08a3->hflip);

	gc08a3->link_freq =
	v4l2_ctrl_new_int_menu(ctrl_hdlr,
			       &gc08a3_ctrl_ops,
			       V4L2_CID_LINK_FREQ,
			       ARRAY_SIZE(gc08a3_link_freq_menu_items) - 1,
			       0,
			       gc08a3_link_freq_menu_items);
	if (gc08a3->link_freq)
		gc08a3->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	gc08a3->pixel_rate =
		v4l2_ctrl_new_std(ctrl_hdlr,
				  &gc08a3_ctrl_ops,
				  V4L2_CID_PIXEL_RATE, 0,
				  gc08a3_to_pixel_rate(0),
				  1,
				  gc08a3_to_pixel_rate(0));

	gc08a3->vblank =
		v4l2_ctrl_new_std(ctrl_hdlr,
				  &gc08a3_ctrl_ops, V4L2_CID_VBLANK,
				  mode->vts_min - mode->height,
				  GC08A3_VTS_MAX - mode->height, 1,
				  mode->vts_def - mode->height);

	h_blank = mode->hts - mode->width;
	gc08a3->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &gc08a3_ctrl_ops,
					   V4L2_CID_HBLANK, h_blank, h_blank, 1,
					   h_blank);
	if (gc08a3->hblank)
		gc08a3->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(ctrl_hdlr, &gc08a3_ctrl_ops,
			  V4L2_CID_ANALOGUE_GAIN, GC08A3_AGAIN_MIN,
			  GC08A3_AGAIN_MAX, GC08A3_AGAIN_STEP,
			  GC08A3_AGAIN_MIN);

	exposure_max = mode->vts_def - GC08A3_EXP_MARGIN;
	gc08a3->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &gc08a3_ctrl_ops,
					     V4L2_CID_EXPOSURE, GC08A3_EXP_MIN,
					     exposure_max, GC08A3_EXP_STEP,
					     exposure_max);

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &gc08a3_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(gc08a3_test_pattern_menu) - 1,
				     0, 0, gc08a3_test_pattern_menu);

	/* register properties to fwnode (e.g. rotation, orientation) */
	ret = v4l2_fwnode_device_parse(&client->dev, &props);
	if (ret)
		goto error_ctrls;

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, ops, &props);
	if (ret)
		goto error_ctrls;

	if (ctrl_hdlr->error) {
		ret = ctrl_hdlr->error;
		goto error_ctrls;
	}

	gc08a3->sd.ctrl_handler = ctrl_hdlr;

	return 0;

error_ctrls:
	v4l2_ctrl_handler_free(ctrl_hdlr);

	return ret;
}

static int gc08a3_identify_module(struct gc08a3 *gc08a3)
{
	u64 val;
	int ret;

	ret = cci_read(gc08a3->regmap, GC08A3_REG_CHIP_ID, &val, NULL);
	if (ret) {
		dev_err(gc08a3->dev, "failed to read chip id");
		return ret;
	}

	if (val != GC08A3_CHIP_ID) {
		dev_err(gc08a3->dev, "chip id mismatch: 0x%x!=0x%llx",
			GC08A3_CHIP_ID, val);
		return -ENXIO;
	}

	return 0;
}

static int gc08a3_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct gc08a3 *gc08a3;
	int ret;

	gc08a3 = devm_kzalloc(dev, sizeof(*gc08a3), GFP_KERNEL);
	if (!gc08a3)
		return -ENOMEM;

	gc08a3->dev = dev;

	ret = gc08a3_parse_fwnode(gc08a3);
	if (ret)
		return ret;

	gc08a3->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(gc08a3->regmap))
		return dev_err_probe(dev, PTR_ERR(gc08a3->regmap),
				     "failed to init CCI\n");

	gc08a3->xclk = devm_v4l2_sensor_clk_get_legacy(dev, NULL, true,
						       GC08A3_DEFAULT_CLK_FREQ);
	if (IS_ERR(gc08a3->xclk))
		return dev_err_probe(dev, PTR_ERR(gc08a3->xclk),
				     "failed to get xclk\n");

	ret = gc08a3_get_regulators(dev, gc08a3);
	if (ret < 0)
		return dev_err_probe(dev, ret,
				     "failed to get regulators\n");

	gc08a3->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(gc08a3->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(gc08a3->reset_gpio),
				     "failed to get gpio\n");

	v4l2_i2c_subdev_init(&gc08a3->sd, client, &gc08a3_subdev_ops);
	gc08a3->sd.internal_ops = &gc08a3_internal_ops;
	gc08a3->cur_mode = &gc08a3_modes[0];

	ret = gc08a3_power_on(gc08a3->dev);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to sensor power on\n");

	ret = gc08a3_identify_module(gc08a3);
	if (ret) {
		dev_err(&client->dev, "failed to find sensor: %d\n", ret);
		goto err_power_off;
	}

	ret = gc08a3_init_controls(gc08a3);
	if (ret) {
		dev_err(&client->dev, "failed to init controls: %d", ret);
		goto err_power_off;
	}

	gc08a3->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	gc08a3->pad.flags = MEDIA_PAD_FL_SOURCE;
	gc08a3->sd.dev = &client->dev;
	gc08a3->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	ret = media_entity_pads_init(&gc08a3->sd.entity, 1, &gc08a3->pad);
	if (ret < 0) {
		dev_err(dev, "could not register media entity\n");
		goto err_v4l2_ctrl_handler_free;
	}

	gc08a3->sd.state_lock = gc08a3->ctrls.lock;
	ret = v4l2_subdev_init_finalize(&gc08a3->sd);
	if (ret < 0) {
		dev_err(dev, "v4l2 subdev init error: %d\n", ret);
		goto err_media_entity_cleanup;
	}

	pm_runtime_set_active(gc08a3->dev);
	pm_runtime_enable(gc08a3->dev);
	pm_runtime_set_autosuspend_delay(gc08a3->dev, 1000);
	pm_runtime_use_autosuspend(gc08a3->dev);
	pm_runtime_idle(gc08a3->dev);

	ret = v4l2_async_register_subdev_sensor(&gc08a3->sd);
	if (ret < 0) {
		dev_err(dev, "could not register v4l2 device\n");
		goto err_rpm;
	}

	return 0;

err_rpm:
	pm_runtime_disable(gc08a3->dev);
	v4l2_subdev_cleanup(&gc08a3->sd);

err_media_entity_cleanup:
	media_entity_cleanup(&gc08a3->sd.entity);

err_v4l2_ctrl_handler_free:
	v4l2_ctrl_handler_free(gc08a3->sd.ctrl_handler);

err_power_off:
	gc08a3_power_off(gc08a3->dev);

	return ret;
}

static void gc08a3_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc08a3 *gc08a3 = to_gc08a3(sd);

	v4l2_async_unregister_subdev(&gc08a3->sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&gc08a3->sd.entity);
	v4l2_ctrl_handler_free(&gc08a3->ctrls);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		gc08a3_power_off(gc08a3->dev);
	pm_runtime_set_suspended(&client->dev);
}

static const struct of_device_id gc08a3_of_match[] = {
	{ .compatible = "galaxycore,gc08a3" },
	{}
};
MODULE_DEVICE_TABLE(of, gc08a3_of_match);

static DEFINE_RUNTIME_DEV_PM_OPS(gc08a3_pm_ops,
				 gc08a3_power_off,
				 gc08a3_power_on,
				 NULL);

static struct i2c_driver gc08a3_i2c_driver = {
	.driver = {
		.of_match_table = gc08a3_of_match,
		.pm = pm_ptr(&gc08a3_pm_ops),
		.name  = "gc08a3",
	},
	.probe  = gc08a3_probe,
	.remove = gc08a3_remove,
};
module_i2c_driver(gc08a3_i2c_driver);

MODULE_DESCRIPTION("GalaxyCore gc08a3 Camera driver");
MODULE_AUTHOR("Zhi Mao <zhi.mao@mediatek.com>");
MODULE_LICENSE("GPL");
