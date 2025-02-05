// SPDX-License-Identifier: GPL-2.0
/*
 * A V4L2 driver for Galaxycore GC2145 camera.
 * Copyright (C) 2023, STMicroelectronics SA
 *
 * Inspired by the imx219.c driver
 *
 * Datasheet v1.0 available at http://files.pine64.org/doc/datasheet/PinebookPro/GC2145%20CSP%20DataSheet%20release%20V1.0_20131201.pdf
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/units.h>

#include <media/mipi-csi2.h>
#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>

/* Chip ID */
#define GC2145_CHIP_ID		0x2145

/* Page 0 */
#define GC2145_REG_EXPOSURE	CCI_REG16(0x03)
#define GC2145_REG_HBLANK	CCI_REG16(0x05)
#define GC2145_REG_VBLANK	CCI_REG16(0x07)
#define GC2145_REG_ROW_START	CCI_REG16(0x09)
#define GC2145_REG_COL_START	CCI_REG16(0x0b)
#define GC2145_REG_WIN_HEIGHT	CCI_REG16(0x0d)
#define GC2145_REG_WIN_WIDTH	CCI_REG16(0x0f)
#define GC2145_REG_ANALOG_MODE1	CCI_REG8(0x17)
#define GC2145_REG_OUTPUT_FMT	CCI_REG8(0x84)
#define GC2145_REG_SYNC_MODE	CCI_REG8(0x86)
#define GC2145_SYNC_MODE_COL_SWITCH	BIT(4)
#define GC2145_SYNC_MODE_ROW_SWITCH	BIT(5)
#define GC2145_REG_BYPASS_MODE	CCI_REG8(0x89)
#define GC2145_BYPASS_MODE_SWITCH	BIT(5)
#define GC2145_REG_DEBUG_MODE2	CCI_REG8(0x8c)
#define GC2145_REG_DEBUG_MODE3	CCI_REG8(0x8d)
#define GC2145_REG_CROP_ENABLE	CCI_REG8(0x90)
#define GC2145_REG_CROP_Y	CCI_REG16(0x91)
#define GC2145_REG_CROP_X	CCI_REG16(0x93)
#define GC2145_REG_CROP_HEIGHT	CCI_REG16(0x95)
#define GC2145_REG_CROP_WIDTH	CCI_REG16(0x97)
#define GC2145_REG_GLOBAL_GAIN	CCI_REG8(0xb0)
#define GC2145_REG_CHIP_ID	CCI_REG16(0xf0)
#define GC2145_REG_PAD_IO	CCI_REG8(0xf2)
#define GC2145_REG_PAGE_SELECT	CCI_REG8(0xfe)
/* Page 3 */
#define GC2145_REG_DPHY_ANALOG_MODE1	CCI_REG8(0x01)
#define GC2145_DPHY_MODE_PHY_CLK_EN	BIT(0)
#define GC2145_DPHY_MODE_PHY_LANE0_EN	BIT(1)
#define GC2145_DPHY_MODE_PHY_LANE1_EN	BIT(2)
#define GC2145_DPHY_MODE_PHY_CLK_LANE_P2S_SEL	BIT(7)
#define GC2145_REG_DPHY_ANALOG_MODE2	CCI_REG8(0x02)
#define GC2145_DPHY_CLK_DIFF(a)		((a) & 0x07)
#define GC2145_DPHY_LANE0_DIFF(a)	(((a) & 0x07) << 4)
#define GC2145_REG_DPHY_ANALOG_MODE3	CCI_REG8(0x03)
#define GC2145_DPHY_LANE1_DIFF(a)	((a) & 0x07)
#define GC2145_DPHY_CLK_DELAY		BIT(4)
#define GC2145_DPHY_LANE0_DELAY		BIT(5)
#define GC2145_DPHY_LANE1_DELAY		BIT(6)
#define GC2145_REG_FIFO_FULL_LVL	CCI_REG16_LE(0x04)
#define GC2145_REG_FIFO_MODE		CCI_REG8(0x06)
#define GC2145_FIFO_MODE_READ_GATE	BIT(3)
#define GC2145_FIFO_MODE_MIPI_CLK_MODULE	BIT(7)
#define GC2145_REG_BUF_CSI2_MODE	CCI_REG8(0x10)
#define GC2145_CSI2_MODE_DOUBLE		BIT(0)
#define GC2145_CSI2_MODE_RAW8		BIT(2)
#define GC2145_CSI2_MODE_MIPI_EN	BIT(4)
#define GC2145_CSI2_MODE_EN		BIT(7)
#define GC2145_REG_MIPI_DT	CCI_REG8(0x11)
#define GC2145_REG_LWC		CCI_REG16_LE(0x12)
#define GC2145_REG_DPHY_MODE	CCI_REG8(0x15)
#define GC2145_DPHY_MODE_TRIGGER_PROG	BIT(4)
#define GC2145_REG_FIFO_GATE_MODE	CCI_REG8(0x17)
#define GC2145_REG_T_LPX	CCI_REG8(0x21)
#define GC2145_REG_T_CLK_HS_PREPARE	CCI_REG8(0x22)
#define GC2145_REG_T_CLK_ZERO	CCI_REG8(0x23)
#define GC2145_REG_T_CLK_PRE	CCI_REG8(0x24)
#define GC2145_REG_T_CLK_POST	CCI_REG8(0x25)
#define GC2145_REG_T_CLK_TRAIL	CCI_REG8(0x26)
#define GC2145_REG_T_HS_EXIT	CCI_REG8(0x27)
#define GC2145_REG_T_WAKEUP	CCI_REG8(0x28)
#define GC2145_REG_T_HS_PREPARE	CCI_REG8(0x29)
#define GC2145_REG_T_HS_ZERO	CCI_REG8(0x2a)
#define GC2145_REG_T_HS_TRAIL	CCI_REG8(0x2b)

/* External clock frequency is 24.0MHz */
#define GC2145_XCLK_FREQ	(24 * HZ_PER_MHZ)

#define GC2145_NATIVE_WIDTH	1616U
#define GC2145_NATIVE_HEIGHT	1232U

/**
 * struct gc2145_mode - GC2145 mode description
 * @width: frame width (in pixels)
 * @height: frame height (in pixels)
 * @reg_seq: registers config sequence to enter into the mode
 * @reg_seq_size: size of the sequence
 * @pixel_rate: pixel rate associated with the mode
 * @crop: window area captured
 * @hblank: default horizontal blanking
 * @vblank: default vertical blanking
 * @link_freq_index: index within the link frequency menu
 */
struct gc2145_mode {
	unsigned int width;
	unsigned int height;
	const struct cci_reg_sequence *reg_seq;
	size_t reg_seq_size;
	unsigned long pixel_rate;
	struct v4l2_rect crop;
	unsigned int hblank;
	unsigned int vblank;
	unsigned int link_freq_index;
};

#define GC2145_DEFAULT_EXPOSURE	0x04e2
#define GC2145_DEFAULT_GLOBAL_GAIN	0x55
static const struct cci_reg_sequence gc2145_common_regs[] = {
	{GC2145_REG_PAGE_SELECT, 0x00},
	/* SH Delay */
	{CCI_REG8(0x12), 0x2e},
	/* Flip */
	{GC2145_REG_ANALOG_MODE1, 0x14},
	/* Analog Conf */
	{CCI_REG8(0x18), 0x22}, {CCI_REG8(0x19), 0x0e}, {CCI_REG8(0x1a), 0x01},
	{CCI_REG8(0x1b), 0x4b}, {CCI_REG8(0x1c), 0x07}, {CCI_REG8(0x1d), 0x10},
	{CCI_REG8(0x1e), 0x88}, {CCI_REG8(0x1f), 0x78}, {CCI_REG8(0x20), 0x03},
	{CCI_REG8(0x21), 0x40}, {CCI_REG8(0x22), 0xa0}, {CCI_REG8(0x24), 0x16},
	{CCI_REG8(0x25), 0x01}, {CCI_REG8(0x26), 0x10}, {CCI_REG8(0x2d), 0x60},
	{CCI_REG8(0x30), 0x01}, {CCI_REG8(0x31), 0x90}, {CCI_REG8(0x33), 0x06},
	{CCI_REG8(0x34), 0x01},
	/* ISP related */
	{CCI_REG8(0x80), 0x7f}, {CCI_REG8(0x81), 0x26}, {CCI_REG8(0x82), 0xfa},
	{CCI_REG8(0x83), 0x00}, {CCI_REG8(0x84), 0x02}, {CCI_REG8(0x86), 0x02},
	{CCI_REG8(0x88), 0x03},
	{GC2145_REG_BYPASS_MODE, 0x03},
	{CCI_REG8(0x85), 0x08}, {CCI_REG8(0x8a), 0x00}, {CCI_REG8(0x8b), 0x00},
	{GC2145_REG_GLOBAL_GAIN, GC2145_DEFAULT_GLOBAL_GAIN},
	{CCI_REG8(0xc3), 0x00}, {CCI_REG8(0xc4), 0x80}, {CCI_REG8(0xc5), 0x90},
	{CCI_REG8(0xc6), 0x3b}, {CCI_REG8(0xc7), 0x46},
	/* BLK */
	{GC2145_REG_PAGE_SELECT, 0x00},
	{CCI_REG8(0x40), 0x42}, {CCI_REG8(0x41), 0x00}, {CCI_REG8(0x43), 0x5b},
	{CCI_REG8(0x5e), 0x00}, {CCI_REG8(0x5f), 0x00}, {CCI_REG8(0x60), 0x00},
	{CCI_REG8(0x61), 0x00}, {CCI_REG8(0x62), 0x00}, {CCI_REG8(0x63), 0x00},
	{CCI_REG8(0x64), 0x00}, {CCI_REG8(0x65), 0x00}, {CCI_REG8(0x66), 0x20},
	{CCI_REG8(0x67), 0x20}, {CCI_REG8(0x68), 0x20}, {CCI_REG8(0x69), 0x20},
	{CCI_REG8(0x76), 0x00}, {CCI_REG8(0x6a), 0x08}, {CCI_REG8(0x6b), 0x08},
	{CCI_REG8(0x6c), 0x08}, {CCI_REG8(0x6d), 0x08}, {CCI_REG8(0x6e), 0x08},
	{CCI_REG8(0x6f), 0x08}, {CCI_REG8(0x70), 0x08}, {CCI_REG8(0x71), 0x08},
	{CCI_REG8(0x76), 0x00}, {CCI_REG8(0x72), 0xf0}, {CCI_REG8(0x7e), 0x3c},
	{CCI_REG8(0x7f), 0x00},
	{GC2145_REG_PAGE_SELECT, 0x02},
	{CCI_REG8(0x48), 0x15}, {CCI_REG8(0x49), 0x00}, {CCI_REG8(0x4b), 0x0b},
	/* AEC */
	{GC2145_REG_PAGE_SELECT, 0x00},
	{GC2145_REG_EXPOSURE, GC2145_DEFAULT_EXPOSURE},
	{GC2145_REG_PAGE_SELECT, 0x01},
	{CCI_REG8(0x01), 0x04}, {CCI_REG8(0x02), 0xc0}, {CCI_REG8(0x03), 0x04},
	{CCI_REG8(0x04), 0x90}, {CCI_REG8(0x05), 0x30}, {CCI_REG8(0x06), 0x90},
	{CCI_REG8(0x07), 0x30}, {CCI_REG8(0x08), 0x80}, {CCI_REG8(0x09), 0x00},
	{CCI_REG8(0x0a), 0x82}, {CCI_REG8(0x0b), 0x11}, {CCI_REG8(0x0c), 0x10},
	{CCI_REG8(0x11), 0x10}, {CCI_REG8(0x13), 0x7b}, {CCI_REG8(0x17), 0x00},
	{CCI_REG8(0x1c), 0x11}, {CCI_REG8(0x1e), 0x61}, {CCI_REG8(0x1f), 0x35},
	{CCI_REG8(0x20), 0x40}, {CCI_REG8(0x22), 0x40}, {CCI_REG8(0x23), 0x20},
	{GC2145_REG_PAGE_SELECT, 0x02},
	{CCI_REG8(0x0f), 0x04},
	{GC2145_REG_PAGE_SELECT, 0x01},
	{CCI_REG8(0x12), 0x35}, {CCI_REG8(0x15), 0xb0}, {CCI_REG8(0x10), 0x31},
	{CCI_REG8(0x3e), 0x28}, {CCI_REG8(0x3f), 0xb0}, {CCI_REG8(0x40), 0x90},
	{CCI_REG8(0x41), 0x0f},
	/* INTPEE */
	{GC2145_REG_PAGE_SELECT, 0x02},
	{CCI_REG8(0x90), 0x6c}, {CCI_REG8(0x91), 0x03}, {CCI_REG8(0x92), 0xcb},
	{CCI_REG8(0x94), 0x33}, {CCI_REG8(0x95), 0x84}, {CCI_REG8(0x97), 0x65},
	{CCI_REG8(0xa2), 0x11},
	/* DNDD */
	{GC2145_REG_PAGE_SELECT, 0x02},
	{CCI_REG8(0x80), 0xc1}, {CCI_REG8(0x81), 0x08}, {CCI_REG8(0x82), 0x05},
	{CCI_REG8(0x83), 0x08}, {CCI_REG8(0x84), 0x0a}, {CCI_REG8(0x86), 0xf0},
	{CCI_REG8(0x87), 0x50}, {CCI_REG8(0x88), 0x15}, {CCI_REG8(0x89), 0xb0},
	{CCI_REG8(0x8a), 0x30}, {CCI_REG8(0x8b), 0x10},
	/* ASDE */
	{GC2145_REG_PAGE_SELECT, 0x01},
	{CCI_REG8(0x21), 0x04},
	{GC2145_REG_PAGE_SELECT, 0x02},
	{CCI_REG8(0xa3), 0x50}, {CCI_REG8(0xa4), 0x20}, {CCI_REG8(0xa5), 0x40},
	{CCI_REG8(0xa6), 0x80}, {CCI_REG8(0xab), 0x40}, {CCI_REG8(0xae), 0x0c},
	{CCI_REG8(0xb3), 0x46}, {CCI_REG8(0xb4), 0x64}, {CCI_REG8(0xb6), 0x38},
	{CCI_REG8(0xb7), 0x01}, {CCI_REG8(0xb9), 0x2b}, {CCI_REG8(0x3c), 0x04},
	{CCI_REG8(0x3d), 0x15}, {CCI_REG8(0x4b), 0x06}, {CCI_REG8(0x4c), 0x20},
	/* Gamma */
	{GC2145_REG_PAGE_SELECT, 0x02},
	{CCI_REG8(0x10), 0x09}, {CCI_REG8(0x11), 0x0d}, {CCI_REG8(0x12), 0x13},
	{CCI_REG8(0x13), 0x19}, {CCI_REG8(0x14), 0x27}, {CCI_REG8(0x15), 0x37},
	{CCI_REG8(0x16), 0x45}, {CCI_REG8(0x17), 0x53}, {CCI_REG8(0x18), 0x69},
	{CCI_REG8(0x19), 0x7d}, {CCI_REG8(0x1a), 0x8f}, {CCI_REG8(0x1b), 0x9d},
	{CCI_REG8(0x1c), 0xa9}, {CCI_REG8(0x1d), 0xbd}, {CCI_REG8(0x1e), 0xcd},
	{CCI_REG8(0x1f), 0xd9}, {CCI_REG8(0x20), 0xe3}, {CCI_REG8(0x21), 0xea},
	{CCI_REG8(0x22), 0xef}, {CCI_REG8(0x23), 0xf5}, {CCI_REG8(0x24), 0xf9},
	{CCI_REG8(0x25), 0xff},
	{GC2145_REG_PAGE_SELECT, 0x00},
	{CCI_REG8(0xc6), 0x20}, {CCI_REG8(0xc7), 0x2b},
	/* Gamma 2 */
	{GC2145_REG_PAGE_SELECT, 0x02},
	{CCI_REG8(0x26), 0x0f}, {CCI_REG8(0x27), 0x14}, {CCI_REG8(0x28), 0x19},
	{CCI_REG8(0x29), 0x1e}, {CCI_REG8(0x2a), 0x27}, {CCI_REG8(0x2b), 0x33},
	{CCI_REG8(0x2c), 0x3b}, {CCI_REG8(0x2d), 0x45}, {CCI_REG8(0x2e), 0x59},
	{CCI_REG8(0x2f), 0x69}, {CCI_REG8(0x30), 0x7c}, {CCI_REG8(0x31), 0x89},
	{CCI_REG8(0x32), 0x98}, {CCI_REG8(0x33), 0xae}, {CCI_REG8(0x34), 0xc0},
	{CCI_REG8(0x35), 0xcf}, {CCI_REG8(0x36), 0xda}, {CCI_REG8(0x37), 0xe2},
	{CCI_REG8(0x38), 0xe9}, {CCI_REG8(0x39), 0xf3}, {CCI_REG8(0x3a), 0xf9},
	{CCI_REG8(0x3b), 0xff},
	/* YCP */
	{GC2145_REG_PAGE_SELECT, 0x02},
	{CCI_REG8(0xd1), 0x32}, {CCI_REG8(0xd2), 0x32}, {CCI_REG8(0xd3), 0x40},
	{CCI_REG8(0xd6), 0xf0}, {CCI_REG8(0xd7), 0x10}, {CCI_REG8(0xd8), 0xda},
	{CCI_REG8(0xdd), 0x14}, {CCI_REG8(0xde), 0x86}, {CCI_REG8(0xed), 0x80},
	{CCI_REG8(0xee), 0x00}, {CCI_REG8(0xef), 0x3f}, {CCI_REG8(0xd8), 0xd8},
	/* ABS */
	{GC2145_REG_PAGE_SELECT, 0x01},
	{CCI_REG8(0x9f), 0x40},
	/* LSC */
	{GC2145_REG_PAGE_SELECT, 0x01},
	{CCI_REG8(0xc2), 0x14}, {CCI_REG8(0xc3), 0x0d}, {CCI_REG8(0xc4), 0x0c},
	{CCI_REG8(0xc8), 0x15}, {CCI_REG8(0xc9), 0x0d}, {CCI_REG8(0xca), 0x0a},
	{CCI_REG8(0xbc), 0x24}, {CCI_REG8(0xbd), 0x10}, {CCI_REG8(0xbe), 0x0b},
	{CCI_REG8(0xb6), 0x25}, {CCI_REG8(0xb7), 0x16}, {CCI_REG8(0xb8), 0x15},
	{CCI_REG8(0xc5), 0x00}, {CCI_REG8(0xc6), 0x00}, {CCI_REG8(0xc7), 0x00},
	{CCI_REG8(0xcb), 0x00}, {CCI_REG8(0xcc), 0x00}, {CCI_REG8(0xcd), 0x00},
	{CCI_REG8(0xbf), 0x07}, {CCI_REG8(0xc0), 0x00}, {CCI_REG8(0xc1), 0x00},
	{CCI_REG8(0xb9), 0x00}, {CCI_REG8(0xba), 0x00}, {CCI_REG8(0xbb), 0x00},
	{CCI_REG8(0xaa), 0x01}, {CCI_REG8(0xab), 0x01}, {CCI_REG8(0xac), 0x00},
	{CCI_REG8(0xad), 0x05}, {CCI_REG8(0xae), 0x06}, {CCI_REG8(0xaf), 0x0e},
	{CCI_REG8(0xb0), 0x0b}, {CCI_REG8(0xb1), 0x07}, {CCI_REG8(0xb2), 0x06},
	{CCI_REG8(0xb3), 0x17}, {CCI_REG8(0xb4), 0x0e}, {CCI_REG8(0xb5), 0x0e},
	{CCI_REG8(0xd0), 0x09}, {CCI_REG8(0xd1), 0x00}, {CCI_REG8(0xd2), 0x00},
	{CCI_REG8(0xd6), 0x08}, {CCI_REG8(0xd7), 0x00}, {CCI_REG8(0xd8), 0x00},
	{CCI_REG8(0xd9), 0x00}, {CCI_REG8(0xda), 0x00}, {CCI_REG8(0xdb), 0x00},
	{CCI_REG8(0xd3), 0x0a}, {CCI_REG8(0xd4), 0x00}, {CCI_REG8(0xd5), 0x00},
	{CCI_REG8(0xa4), 0x00}, {CCI_REG8(0xa5), 0x00}, {CCI_REG8(0xa6), 0x77},
	{CCI_REG8(0xa7), 0x77}, {CCI_REG8(0xa8), 0x77}, {CCI_REG8(0xa9), 0x77},
	{CCI_REG8(0xa1), 0x80}, {CCI_REG8(0xa2), 0x80},
	{GC2145_REG_PAGE_SELECT, 0x01},
	{CCI_REG8(0xdf), 0x0d}, {CCI_REG8(0xdc), 0x25}, {CCI_REG8(0xdd), 0x30},
	{CCI_REG8(0xe0), 0x77}, {CCI_REG8(0xe1), 0x80}, {CCI_REG8(0xe2), 0x77},
	{CCI_REG8(0xe3), 0x90}, {CCI_REG8(0xe6), 0x90}, {CCI_REG8(0xe7), 0xa0},
	{CCI_REG8(0xe8), 0x90}, {CCI_REG8(0xe9), 0xa0},
	/* AWB */
	/* measure window */
	{GC2145_REG_PAGE_SELECT, 0x00},
	{CCI_REG8(0xec), 0x06}, {CCI_REG8(0xed), 0x04}, {CCI_REG8(0xee), 0x60},
	{CCI_REG8(0xef), 0x90}, {CCI_REG8(0xb6), 0x01},
	{GC2145_REG_PAGE_SELECT, 0x01},
	{CCI_REG8(0x4f), 0x00}, {CCI_REG8(0x4f), 0x00}, {CCI_REG8(0x4b), 0x01},
	{CCI_REG8(0x4f), 0x00},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0x71}, {CCI_REG8(0x4e), 0x01},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0x91}, {CCI_REG8(0x4e), 0x01},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0x70}, {CCI_REG8(0x4e), 0x01},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0x90}, {CCI_REG8(0x4e), 0x02},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0xb0}, {CCI_REG8(0x4e), 0x02},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0x8f}, {CCI_REG8(0x4e), 0x02},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0x6f}, {CCI_REG8(0x4e), 0x02},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0xaf}, {CCI_REG8(0x4e), 0x02},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0xd0}, {CCI_REG8(0x4e), 0x02},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0xf0}, {CCI_REG8(0x4e), 0x02},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0xcf}, {CCI_REG8(0x4e), 0x02},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0xef}, {CCI_REG8(0x4e), 0x02},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0x6e}, {CCI_REG8(0x4e), 0x03},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0x8e}, {CCI_REG8(0x4e), 0x03},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0xae}, {CCI_REG8(0x4e), 0x03},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0xce}, {CCI_REG8(0x4e), 0x03},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0x4d}, {CCI_REG8(0x4e), 0x03},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0x6d}, {CCI_REG8(0x4e), 0x03},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0x8d}, {CCI_REG8(0x4e), 0x03},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0xad}, {CCI_REG8(0x4e), 0x03},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0xcd}, {CCI_REG8(0x4e), 0x03},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0x4c}, {CCI_REG8(0x4e), 0x03},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0x6c}, {CCI_REG8(0x4e), 0x03},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0x8c}, {CCI_REG8(0x4e), 0x03},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0xac}, {CCI_REG8(0x4e), 0x03},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0xcc}, {CCI_REG8(0x4e), 0x03},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0xcb}, {CCI_REG8(0x4e), 0x03},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0x4b}, {CCI_REG8(0x4e), 0x03},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0x6b}, {CCI_REG8(0x4e), 0x03},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0x8b}, {CCI_REG8(0x4e), 0x03},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0xab}, {CCI_REG8(0x4e), 0x03},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0x8a}, {CCI_REG8(0x4e), 0x04},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0xaa}, {CCI_REG8(0x4e), 0x04},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0xca}, {CCI_REG8(0x4e), 0x04},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0xca}, {CCI_REG8(0x4e), 0x04},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0xc9}, {CCI_REG8(0x4e), 0x04},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0x8a}, {CCI_REG8(0x4e), 0x04},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0x89}, {CCI_REG8(0x4e), 0x04},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0xa9}, {CCI_REG8(0x4e), 0x04},
	{CCI_REG8(0x4c), 0x02}, {CCI_REG8(0x4d), 0x0b}, {CCI_REG8(0x4e), 0x05},
	{CCI_REG8(0x4c), 0x02}, {CCI_REG8(0x4d), 0x0a}, {CCI_REG8(0x4e), 0x05},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0xeb}, {CCI_REG8(0x4e), 0x05},
	{CCI_REG8(0x4c), 0x01}, {CCI_REG8(0x4d), 0xea}, {CCI_REG8(0x4e), 0x05},
	{CCI_REG8(0x4c), 0x02}, {CCI_REG8(0x4d), 0x09}, {CCI_REG8(0x4e), 0x05},
	{CCI_REG8(0x4c), 0x02}, {CCI_REG8(0x4d), 0x29}, {CCI_REG8(0x4e), 0x05},
	{CCI_REG8(0x4c), 0x02}, {CCI_REG8(0x4d), 0x2a}, {CCI_REG8(0x4e), 0x05},
	{CCI_REG8(0x4c), 0x02}, {CCI_REG8(0x4d), 0x4a}, {CCI_REG8(0x4e), 0x05},
	{CCI_REG8(0x4c), 0x02}, {CCI_REG8(0x4d), 0x8a}, {CCI_REG8(0x4e), 0x06},
	{CCI_REG8(0x4c), 0x02}, {CCI_REG8(0x4d), 0x49}, {CCI_REG8(0x4e), 0x06},
	{CCI_REG8(0x4c), 0x02}, {CCI_REG8(0x4d), 0x69}, {CCI_REG8(0x4e), 0x06},
	{CCI_REG8(0x4c), 0x02}, {CCI_REG8(0x4d), 0x89}, {CCI_REG8(0x4e), 0x06},
	{CCI_REG8(0x4c), 0x02}, {CCI_REG8(0x4d), 0xa9}, {CCI_REG8(0x4e), 0x06},
	{CCI_REG8(0x4c), 0x02}, {CCI_REG8(0x4d), 0x48}, {CCI_REG8(0x4e), 0x06},
	{CCI_REG8(0x4c), 0x02}, {CCI_REG8(0x4d), 0x68}, {CCI_REG8(0x4e), 0x06},
	{CCI_REG8(0x4c), 0x02}, {CCI_REG8(0x4d), 0x69}, {CCI_REG8(0x4e), 0x06},
	{CCI_REG8(0x4c), 0x02}, {CCI_REG8(0x4d), 0xca}, {CCI_REG8(0x4e), 0x07},
	{CCI_REG8(0x4c), 0x02}, {CCI_REG8(0x4d), 0xc9}, {CCI_REG8(0x4e), 0x07},
	{CCI_REG8(0x4c), 0x02}, {CCI_REG8(0x4d), 0xe9}, {CCI_REG8(0x4e), 0x07},
	{CCI_REG8(0x4c), 0x03}, {CCI_REG8(0x4d), 0x09}, {CCI_REG8(0x4e), 0x07},
	{CCI_REG8(0x4c), 0x02}, {CCI_REG8(0x4d), 0xc8}, {CCI_REG8(0x4e), 0x07},
	{CCI_REG8(0x4c), 0x02}, {CCI_REG8(0x4d), 0xe8}, {CCI_REG8(0x4e), 0x07},
	{CCI_REG8(0x4c), 0x02}, {CCI_REG8(0x4d), 0xa7}, {CCI_REG8(0x4e), 0x07},
	{CCI_REG8(0x4c), 0x02}, {CCI_REG8(0x4d), 0xc7}, {CCI_REG8(0x4e), 0x07},
	{CCI_REG8(0x4c), 0x02}, {CCI_REG8(0x4d), 0xe7}, {CCI_REG8(0x4e), 0x07},
	{CCI_REG8(0x4c), 0x03}, {CCI_REG8(0x4d), 0x07}, {CCI_REG8(0x4e), 0x07},
	{CCI_REG8(0x4f), 0x01},
	{CCI_REG8(0x50), 0x80}, {CCI_REG8(0x51), 0xa8}, {CCI_REG8(0x52), 0x47},
	{CCI_REG8(0x53), 0x38}, {CCI_REG8(0x54), 0xc7}, {CCI_REG8(0x56), 0x0e},
	{CCI_REG8(0x58), 0x08}, {CCI_REG8(0x5b), 0x00}, {CCI_REG8(0x5c), 0x74},
	{CCI_REG8(0x5d), 0x8b}, {CCI_REG8(0x61), 0xdb}, {CCI_REG8(0x62), 0xb8},
	{CCI_REG8(0x63), 0x86}, {CCI_REG8(0x64), 0xc0}, {CCI_REG8(0x65), 0x04},
	{CCI_REG8(0x67), 0xa8}, {CCI_REG8(0x68), 0xb0}, {CCI_REG8(0x69), 0x00},
	{CCI_REG8(0x6a), 0xa8}, {CCI_REG8(0x6b), 0xb0}, {CCI_REG8(0x6c), 0xaf},
	{CCI_REG8(0x6d), 0x8b}, {CCI_REG8(0x6e), 0x50}, {CCI_REG8(0x6f), 0x18},
	{CCI_REG8(0x73), 0xf0}, {CCI_REG8(0x70), 0x0d}, {CCI_REG8(0x71), 0x60},
	{CCI_REG8(0x72), 0x80}, {CCI_REG8(0x74), 0x01}, {CCI_REG8(0x75), 0x01},
	{CCI_REG8(0x7f), 0x0c}, {CCI_REG8(0x76), 0x70}, {CCI_REG8(0x77), 0x58},
	{CCI_REG8(0x78), 0xa0}, {CCI_REG8(0x79), 0x5e}, {CCI_REG8(0x7a), 0x54},
	{CCI_REG8(0x7b), 0x58},
	/* CC */
	{GC2145_REG_PAGE_SELECT, 0x02},
	{CCI_REG8(0xc0), 0x01}, {CCI_REG8(0xc1), 0x44}, {CCI_REG8(0xc2), 0xfd},
	{CCI_REG8(0xc3), 0x04}, {CCI_REG8(0xc4), 0xf0}, {CCI_REG8(0xc5), 0x48},
	{CCI_REG8(0xc6), 0xfd}, {CCI_REG8(0xc7), 0x46}, {CCI_REG8(0xc8), 0xfd},
	{CCI_REG8(0xc9), 0x02}, {CCI_REG8(0xca), 0xe0}, {CCI_REG8(0xcb), 0x45},
	{CCI_REG8(0xcc), 0xec}, {CCI_REG8(0xcd), 0x48}, {CCI_REG8(0xce), 0xf0},
	{CCI_REG8(0xcf), 0xf0}, {CCI_REG8(0xe3), 0x0c}, {CCI_REG8(0xe4), 0x4b},
	{CCI_REG8(0xe5), 0xe0},
	/* ABS */
	{GC2145_REG_PAGE_SELECT, 0x01},
	{CCI_REG8(0x9f), 0x40},
	/* Dark sun */
	{GC2145_REG_PAGE_SELECT, 0x02},
	{CCI_REG8(0x40), 0xbf}, {CCI_REG8(0x46), 0xcf},
};

#define GC2145_640_480_PIXELRATE	30000000
#define GC2145_640_480_LINKFREQ		120000000
#define GC2145_640_480_HBLANK		0x0130
#define GC2145_640_480_VBLANK		0x000c
static const struct cci_reg_sequence gc2145_mode_640_480_regs[] = {
	{GC2145_REG_PAGE_SELECT, 0xf0}, {GC2145_REG_PAGE_SELECT, 0xf0},
	{GC2145_REG_PAGE_SELECT, 0xf0}, {CCI_REG8(0xfc), 0x06},
	{CCI_REG8(0xf6), 0x00}, {CCI_REG8(0xf7), 0x1d}, {CCI_REG8(0xf8), 0x86},
	{CCI_REG8(0xfa), 0x00}, {CCI_REG8(0xf9), 0x8e},
	/* Disable PAD IO */
	{GC2145_REG_PAD_IO, 0x00},
	{GC2145_REG_PAGE_SELECT, 0x00},
	/* Row/Col start - 0/0 */
	{GC2145_REG_ROW_START, 0x0000},
	{GC2145_REG_COL_START, 0x0000},
	/* Window size 1216/1618 */
	{GC2145_REG_WIN_HEIGHT, 0x04c0},
	{GC2145_REG_WIN_WIDTH, 0x0652},
	/* Scalar more */
	{CCI_REG8(0xfd), 0x01}, {CCI_REG8(0xfa), 0x00},
	/* Crop 640-480@0-0 */
	{GC2145_REG_CROP_ENABLE, 0x01},
	{GC2145_REG_CROP_Y, 0x0000},
	{GC2145_REG_CROP_X, 0x0000},
	{GC2145_REG_CROP_HEIGHT, 0x01e0},
	{GC2145_REG_CROP_WIDTH, 0x0280},
	/* Subsampling configuration */
	{CCI_REG8(0x99), 0x55}, {CCI_REG8(0x9a), 0x06}, {CCI_REG8(0x9b), 0x01},
	{CCI_REG8(0x9c), 0x23}, {CCI_REG8(0x9d), 0x00}, {CCI_REG8(0x9e), 0x00},
	{CCI_REG8(0x9f), 0x01}, {CCI_REG8(0xa0), 0x23}, {CCI_REG8(0xa1), 0x00},
	{CCI_REG8(0xa2), 0x00},
	{GC2145_REG_PAGE_SELECT, 0x01},
	/* AEC anti-flicker */
	{CCI_REG16(0x25), 0x0175},
	/* AEC exposure level 1-5 */
	{CCI_REG16(0x27), 0x045f}, {CCI_REG16(0x29), 0x045f},
	{CCI_REG16(0x2b), 0x045f}, {CCI_REG16(0x2d), 0x045f},
};

#define GC2145_1280_720_PIXELRATE	48000000
#define GC2145_1280_720_LINKFREQ	192000000
#define GC2145_1280_720_HBLANK		0x0156
#define GC2145_1280_720_VBLANK		0x0011
static const struct cci_reg_sequence gc2145_mode_1280_720_regs[] = {
	{GC2145_REG_PAGE_SELECT, 0xf0}, {GC2145_REG_PAGE_SELECT, 0xf0},
	{GC2145_REG_PAGE_SELECT, 0xf0}, {CCI_REG8(0xfc), 0x06},
	{CCI_REG8(0xf6), 0x00}, {CCI_REG8(0xf7), 0x1d}, {CCI_REG8(0xf8), 0x83},
	{CCI_REG8(0xfa), 0x00}, {CCI_REG8(0xf9), 0x8e},
	/* Disable PAD IO */
	{GC2145_REG_PAD_IO, 0x00},
	{GC2145_REG_PAGE_SELECT, 0x00},
	/* Row/Col start - 240/160 */
	{GC2145_REG_ROW_START, 0x00f0},
	{GC2145_REG_COL_START, 0x00a0},
	/* Window size 736/1296 */
	{GC2145_REG_WIN_HEIGHT, 0x02e0},
	{GC2145_REG_WIN_WIDTH, 0x0510},
	/* Crop 1280-720@0-0 */
	{GC2145_REG_CROP_ENABLE, 0x01},
	{GC2145_REG_CROP_Y, 0x0000},
	{GC2145_REG_CROP_X, 0x0000},
	{GC2145_REG_CROP_HEIGHT, 0x02d0},
	{GC2145_REG_CROP_WIDTH, 0x0500},
	{GC2145_REG_PAGE_SELECT, 0x01},
	/* AEC anti-flicker */
	{CCI_REG16(0x25), 0x00e6},
	/* AEC exposure level 1-5 */
	{CCI_REG16(0x27), 0x02b2}, {CCI_REG16(0x29), 0x02b2},
	{CCI_REG16(0x2b), 0x02b2}, {CCI_REG16(0x2d), 0x02b2},
};

#define GC2145_1600_1200_PIXELRATE	60000000
#define GC2145_1600_1200_LINKFREQ	240000000
#define GC2145_1600_1200_HBLANK		0x0156
#define GC2145_1600_1200_VBLANK		0x0010
static const struct cci_reg_sequence gc2145_mode_1600_1200_regs[] = {
	{GC2145_REG_PAGE_SELECT, 0xf0}, {GC2145_REG_PAGE_SELECT, 0xf0},
	{GC2145_REG_PAGE_SELECT, 0xf0}, {CCI_REG8(0xfc), 0x06},
	{CCI_REG8(0xf6), 0x00}, {CCI_REG8(0xf7), 0x1d}, {CCI_REG8(0xf8), 0x84},
	{CCI_REG8(0xfa), 0x00}, {CCI_REG8(0xf9), 0x8e},
	/* Disable PAD IO */
	{GC2145_REG_PAD_IO, 0x00},
	{GC2145_REG_PAGE_SELECT, 0x00},
	/* Row/Col start - 0/0 */
	{GC2145_REG_ROW_START, 0x0000},
	{GC2145_REG_COL_START, 0x0000},
	/* Window size: 1216/1618 */
	{GC2145_REG_WIN_HEIGHT, 0x04c0},
	{GC2145_REG_WIN_WIDTH, 0x0652},
	/* Crop 1600-1200@0-0 */
	{GC2145_REG_CROP_ENABLE, 0x01},
	{GC2145_REG_CROP_Y, 0x0000},
	{GC2145_REG_CROP_X, 0x0000},
	{GC2145_REG_CROP_HEIGHT, 0x04b0},
	{GC2145_REG_CROP_WIDTH, 0x0640},
	{GC2145_REG_PAGE_SELECT, 0x01},
	/* AEC anti-flicker */
	{CCI_REG16(0x25), 0x00fa},
	/* AEC exposure level 1-5 */
	{CCI_REG16(0x27), 0x04e2}, {CCI_REG16(0x29), 0x04e2},
	{CCI_REG16(0x2b), 0x04e2}, {CCI_REG16(0x2d), 0x04e2},
};

static const s64 gc2145_link_freq_menu[] = {
	GC2145_640_480_LINKFREQ,
	GC2145_1280_720_LINKFREQ,
	GC2145_1600_1200_LINKFREQ,
};

/* Regulators supplies */
static const char * const gc2145_supply_name[] = {
	"iovdd", /* Digital I/O (1.7-3V) suppply */
	"avdd",  /* Analog (2.7-3V) supply */
	"dvdd",  /* Digital Core (1.7-1.9V) supply */
};

#define GC2145_NUM_SUPPLIES ARRAY_SIZE(gc2145_supply_name)

/* Mode configs */
#define GC2145_MODE_640X480	0
#define GC2145_MODE_1280X720	1
#define GC2145_MODE_1600X1200	2
static const struct gc2145_mode supported_modes[] = {
	{
		/* 640x480 30fps mode */
		.width = 640,
		.height = 480,
		.reg_seq = gc2145_mode_640_480_regs,
		.reg_seq_size = ARRAY_SIZE(gc2145_mode_640_480_regs),
		.pixel_rate = GC2145_640_480_PIXELRATE,
		.crop = {
			.top = 0,
			.left = 0,
			.width = 640,
			.height = 480,
		},
		.hblank = GC2145_640_480_HBLANK,
		.vblank = GC2145_640_480_VBLANK,
		.link_freq_index = GC2145_MODE_640X480,
	},
	{
		/* 1280x720 30fps mode */
		.width = 1280,
		.height = 720,
		.reg_seq = gc2145_mode_1280_720_regs,
		.reg_seq_size = ARRAY_SIZE(gc2145_mode_1280_720_regs),
		.pixel_rate = GC2145_1280_720_PIXELRATE,
		.crop = {
			.top = 160,
			.left = 240,
			.width = 1280,
			.height = 720,
		},
		.hblank = GC2145_1280_720_HBLANK,
		.vblank = GC2145_1280_720_VBLANK,
		.link_freq_index = GC2145_MODE_1280X720,
	},
	{
		/* 1600x1200 20fps mode */
		.width = 1600,
		.height = 1200,
		.reg_seq = gc2145_mode_1600_1200_regs,
		.reg_seq_size = ARRAY_SIZE(gc2145_mode_1600_1200_regs),
		.pixel_rate = GC2145_1600_1200_PIXELRATE,
		.crop = {
			.top = 0,
			.left = 0,
			.width = 1600,
			.height = 1200,
		},
		.hblank = GC2145_1600_1200_HBLANK,
		.vblank = GC2145_1600_1200_VBLANK,
		.link_freq_index = GC2145_MODE_1600X1200,
	},
};

/**
 * struct gc2145_format - GC2145 pixel format description
 * @code: media bus (MBUS) associated code
 * @colorspace: V4L2 colorspace
 * @datatype: MIPI CSI2 data type
 * @output_fmt: GC2145 output format
 * @switch_bit: GC2145 first/second switch
 * @row_col_switch: GC2145 switch row and/or column
 */
struct gc2145_format {
	unsigned int code;
	unsigned int colorspace;
	unsigned char datatype;
	unsigned char output_fmt;
	bool switch_bit;
	unsigned char row_col_switch;
};

/* All supported formats */
static const struct gc2145_format supported_formats[] = {
	{
		.code		= MEDIA_BUS_FMT_UYVY8_1X16,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.datatype	= MIPI_CSI2_DT_YUV422_8B,
		.output_fmt	= 0x00,
	},
	{
		.code		= MEDIA_BUS_FMT_VYUY8_1X16,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.datatype	= MIPI_CSI2_DT_YUV422_8B,
		.output_fmt	= 0x01,
	},
	{
		.code		= MEDIA_BUS_FMT_YUYV8_1X16,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.datatype	= MIPI_CSI2_DT_YUV422_8B,
		.output_fmt	= 0x02,
	},
	{
		.code		= MEDIA_BUS_FMT_YVYU8_1X16,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.datatype	= MIPI_CSI2_DT_YUV422_8B,
		.output_fmt	= 0x03,
	},
	{
		.code		= MEDIA_BUS_FMT_RGB565_1X16,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.datatype	= MIPI_CSI2_DT_RGB565,
		.output_fmt	= 0x06,
		.switch_bit	= true,
	},
	{
		.code           = MEDIA_BUS_FMT_SGRBG8_1X8,
		.colorspace     = V4L2_COLORSPACE_RAW,
		.datatype       = MIPI_CSI2_DT_RAW8,
		.output_fmt     = 0x17,
		.row_col_switch = GC2145_SYNC_MODE_COL_SWITCH,
	},
	{
		.code           = MEDIA_BUS_FMT_SRGGB8_1X8,
		.colorspace     = V4L2_COLORSPACE_RAW,
		.datatype       = MIPI_CSI2_DT_RAW8,
		.output_fmt     = 0x17,
		.row_col_switch = GC2145_SYNC_MODE_COL_SWITCH | GC2145_SYNC_MODE_ROW_SWITCH,
	},
	{
		.code           = MEDIA_BUS_FMT_SBGGR8_1X8,
		.colorspace     = V4L2_COLORSPACE_RAW,
		.datatype       = MIPI_CSI2_DT_RAW8,
		.output_fmt     = 0x17,
		.row_col_switch = 0,
	},
	{
		.code           = MEDIA_BUS_FMT_SGBRG8_1X8,
		.colorspace     = V4L2_COLORSPACE_RAW,
		.datatype       = MIPI_CSI2_DT_RAW8,
		.output_fmt     = 0x17,
		.row_col_switch = GC2145_SYNC_MODE_ROW_SWITCH,
	},
};

struct gc2145_ctrls {
	struct v4l2_ctrl_handler handler;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *test_pattern;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vflip;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *vblank;
};

struct gc2145 {
	struct v4l2_subdev sd;
	struct media_pad pad;

	struct regmap *regmap;
	struct clk *xclk;

	struct gpio_desc *reset_gpio;
	struct gpio_desc *powerdown_gpio;
	struct regulator_bulk_data supplies[GC2145_NUM_SUPPLIES];

	/* V4L2 controls */
	struct gc2145_ctrls ctrls;

	/* Current mode */
	const struct gc2145_mode *mode;
};

static inline struct gc2145 *to_gc2145(struct v4l2_subdev *_sd)
{
	return container_of(_sd, struct gc2145, sd);
}

static inline struct v4l2_subdev *gc2145_ctrl_to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct gc2145,
			     ctrls.handler)->sd;
}

static const struct gc2145_format *
gc2145_get_format_code(struct gc2145 *gc2145, u32 code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_formats); i++) {
		if (supported_formats[i].code == code)
			break;
	}

	if (i >= ARRAY_SIZE(supported_formats))
		i = 0;

	return &supported_formats[i];
}

static void gc2145_update_pad_format(struct gc2145 *gc2145,
				     const struct gc2145_mode *mode,
				     struct v4l2_mbus_framefmt *fmt, u32 code,
				     u32 colorspace)
{
	fmt->code = code;
	fmt->width = mode->width;
	fmt->height = mode->height;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	fmt->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	fmt->quantization = V4L2_QUANTIZATION_DEFAULT;
	fmt->xfer_func = V4L2_XFER_FUNC_DEFAULT;
}

static int gc2145_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state)
{
	struct gc2145 *gc2145 = to_gc2145(sd);
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *crop;

	/* Initialize pad format */
	format = v4l2_subdev_state_get_format(state, 0);
	gc2145_update_pad_format(gc2145, &supported_modes[0], format,
				 MEDIA_BUS_FMT_RGB565_1X16,
				 V4L2_COLORSPACE_SRGB);

	/* Initialize crop rectangle. */
	crop = v4l2_subdev_state_get_crop(state, 0);
	*crop = supported_modes[0].crop;

	return 0;
}

static int gc2145_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		sel->r = *v4l2_subdev_state_get_crop(sd_state, 0);
		return 0;

	case V4L2_SEL_TGT_NATIVE_SIZE:
		sel->r.top = 0;
		sel->r.left = 0;
		sel->r.width = GC2145_NATIVE_WIDTH;
		sel->r.height = GC2145_NATIVE_HEIGHT;

		return 0;

	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.top = 0;
		sel->r.left = 0;
		sel->r.width = 1600;
		sel->r.height = 1200;

		return 0;
	}

	return -EINVAL;
}

static int gc2145_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= ARRAY_SIZE(supported_formats))
		return -EINVAL;

	code->code = supported_formats[code->index].code;
	return 0;
}

static int gc2145_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	struct gc2145 *gc2145 = to_gc2145(sd);
	const struct gc2145_format *gc2145_format;
	u32 code;

	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	gc2145_format = gc2145_get_format_code(gc2145, fse->code);
	code = gc2145_format->code;
	if (fse->code != code)
		return -EINVAL;

	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = supported_modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static int gc2145_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct gc2145 *gc2145 = to_gc2145(sd);
	const struct gc2145_mode *mode;
	const struct gc2145_format *gc2145_fmt;
	struct v4l2_mbus_framefmt *framefmt;
	struct gc2145_ctrls *ctrls = &gc2145->ctrls;
	struct v4l2_rect *crop;

	gc2145_fmt = gc2145_get_format_code(gc2145, fmt->format.code);
	mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes),
				      width, height,
				      fmt->format.width, fmt->format.height);

	/* In RAW mode, VGA is not possible so use 720p instead */
	if (gc2145_fmt->colorspace == V4L2_COLORSPACE_RAW &&
	    mode == &supported_modes[GC2145_MODE_640X480])
		mode = &supported_modes[GC2145_MODE_1280X720];

	gc2145_update_pad_format(gc2145, mode, &fmt->format, gc2145_fmt->code,
				 gc2145_fmt->colorspace);
	framefmt = v4l2_subdev_state_get_format(sd_state, fmt->pad);
	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		gc2145->mode = mode;
		/* Update pixel_rate based on the mode */
		__v4l2_ctrl_s_ctrl_int64(ctrls->pixel_rate, mode->pixel_rate);
		/* Update link_freq based on the mode */
		__v4l2_ctrl_s_ctrl(ctrls->link_freq, mode->link_freq_index);
		/* Update hblank/vblank based on the mode */
		__v4l2_ctrl_s_ctrl(ctrls->hblank, mode->hblank);
		__v4l2_ctrl_s_ctrl(ctrls->vblank, mode->vblank);
	}
	*framefmt = fmt->format;
	crop = v4l2_subdev_state_get_crop(sd_state, fmt->pad);
	*crop = mode->crop;

	return 0;
}

static const struct cci_reg_sequence gc2145_common_mipi_regs[] = {
	{GC2145_REG_PAGE_SELECT, 0x03},
	{GC2145_REG_DPHY_ANALOG_MODE1, GC2145_DPHY_MODE_PHY_CLK_EN |
				       GC2145_DPHY_MODE_PHY_LANE0_EN |
				       GC2145_DPHY_MODE_PHY_LANE1_EN |
				       GC2145_DPHY_MODE_PHY_CLK_LANE_P2S_SEL},
	{GC2145_REG_DPHY_ANALOG_MODE2, GC2145_DPHY_CLK_DIFF(2) |
				       GC2145_DPHY_LANE0_DIFF(2)},
	{GC2145_REG_DPHY_ANALOG_MODE3, GC2145_DPHY_LANE1_DIFF(0) |
				       GC2145_DPHY_CLK_DELAY},
	{GC2145_REG_FIFO_MODE, GC2145_FIFO_MODE_READ_GATE |
			       GC2145_FIFO_MODE_MIPI_CLK_MODULE},
	{GC2145_REG_DPHY_MODE, GC2145_DPHY_MODE_TRIGGER_PROG},
	/* Clock & Data lanes timing */
	{GC2145_REG_T_LPX, 0x10},
	{GC2145_REG_T_CLK_HS_PREPARE, 0x04}, {GC2145_REG_T_CLK_ZERO, 0x10},
	{GC2145_REG_T_CLK_PRE, 0x10}, {GC2145_REG_T_CLK_POST, 0x10},
	{GC2145_REG_T_CLK_TRAIL, 0x05},
	{GC2145_REG_T_HS_PREPARE, 0x03}, {GC2145_REG_T_HS_ZERO, 0x0a},
	{GC2145_REG_T_HS_TRAIL, 0x06},
};

static int gc2145_config_mipi_mode(struct gc2145 *gc2145,
				   const struct gc2145_format *gc2145_format)
{
	u16 lwc, fifo_full_lvl;
	int ret = 0;

	/* Common MIPI settings */
	cci_multi_reg_write(gc2145->regmap, gc2145_common_mipi_regs,
			    ARRAY_SIZE(gc2145_common_mipi_regs), &ret);

	/*
	 * Adjust the MIPI buffer settings.
	 * For YUV/RGB, LWC = image width * 2
	 * For RAW8, LWC = image width
	 * For RAW10, LWC = image width * 1.25
	 */
	if (gc2145_format->colorspace != V4L2_COLORSPACE_RAW)
		lwc = gc2145->mode->width * 2;
	else
		lwc = gc2145->mode->width;

	cci_write(gc2145->regmap, GC2145_REG_LWC, lwc, &ret);

	/*
	 * Adjust the MIPI FIFO Full Level
	 * 640x480 RGB: 0x0190
	 * 1280x720 / 1600x1200 (aka no scaler) non RAW: 0x0001
	 * 1600x1200 RAW: 0x0190
	 */
	if (gc2145_format->colorspace != V4L2_COLORSPACE_RAW) {
		if (gc2145->mode->width == 1280 || gc2145->mode->width == 1600)
			fifo_full_lvl = 0x0001;
		else
			fifo_full_lvl = 0x0190;
	} else {
		fifo_full_lvl = 0x0190;
	}

	cci_write(gc2145->regmap, GC2145_REG_FIFO_FULL_LVL,
		  fifo_full_lvl, &ret);

	/*
	 * Set the FIFO gate mode / MIPI wdiv set:
	 * 0xf1 in case of RAW mode and 0xf0 otherwise
	 */
	cci_write(gc2145->regmap, GC2145_REG_FIFO_GATE_MODE,
		  gc2145_format->colorspace == V4L2_COLORSPACE_RAW ?
		  0xf1 : 0xf0, &ret);

	/* Set the MIPI data type */
	cci_write(gc2145->regmap, GC2145_REG_MIPI_DT,
		  gc2145_format->datatype, &ret);

	/* Configure mode and enable CSI */
	cci_write(gc2145->regmap, GC2145_REG_BUF_CSI2_MODE,
		  GC2145_CSI2_MODE_RAW8 | GC2145_CSI2_MODE_DOUBLE |
		  GC2145_CSI2_MODE_EN | GC2145_CSI2_MODE_MIPI_EN, &ret);

	return ret;
}

static int gc2145_enable_streams(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state, u32 pad,
				 u64 streams_mask)
{
	struct gc2145 *gc2145 = to_gc2145(sd);
	struct i2c_client *client = v4l2_get_subdevdata(&gc2145->sd);
	const struct gc2145_format *gc2145_format;
	struct v4l2_mbus_framefmt *fmt;
	int ret;

	ret = pm_runtime_resume_and_get(&client->dev);
	if (ret < 0)
		return ret;

	/* Apply default values of current mode */
	cci_multi_reg_write(gc2145->regmap, gc2145->mode->reg_seq,
			    gc2145->mode->reg_seq_size, &ret);
	cci_multi_reg_write(gc2145->regmap, gc2145_common_regs,
			    ARRAY_SIZE(gc2145_common_regs), &ret);
	if (ret) {
		dev_err(&client->dev, "%s failed to write regs\n", __func__);
		goto err_rpm_put;
	}

	fmt = v4l2_subdev_state_get_format(state, 0);
	gc2145_format = gc2145_get_format_code(gc2145, fmt->code);

	/* Set the output format */
	cci_write(gc2145->regmap, GC2145_REG_PAGE_SELECT, 0x00, &ret);

	cci_write(gc2145->regmap, GC2145_REG_OUTPUT_FMT,
		  gc2145_format->output_fmt, &ret);
	cci_update_bits(gc2145->regmap, GC2145_REG_BYPASS_MODE,
			GC2145_BYPASS_MODE_SWITCH,
			gc2145_format->switch_bit ? GC2145_BYPASS_MODE_SWITCH
						  : 0, &ret);
	cci_update_bits(gc2145->regmap, GC2145_REG_SYNC_MODE,
			GC2145_SYNC_MODE_COL_SWITCH |
			GC2145_SYNC_MODE_ROW_SWITCH,
			gc2145_format->row_col_switch, &ret);
	if (ret) {
		dev_err(&client->dev, "%s failed to write regs\n", __func__);
		goto err_rpm_put;
	}

	/* Apply customized values from user */
	ret =  __v4l2_ctrl_handler_setup(&gc2145->ctrls.handler);
	if (ret) {
		dev_err(&client->dev, "%s failed to apply ctrls\n", __func__);
		goto err_rpm_put;
	}

	/* Perform MIPI specific configuration */
	ret = gc2145_config_mipi_mode(gc2145, gc2145_format);
	if (ret) {
		dev_err(&client->dev, "%s failed to write mipi conf\n",
			__func__);
		goto err_rpm_put;
	}

	cci_write(gc2145->regmap, GC2145_REG_PAGE_SELECT, 0x00, &ret);

	return 0;

err_rpm_put:
	pm_runtime_mark_last_busy(&client->dev);
	pm_runtime_put_autosuspend(&client->dev);
	return ret;
}

static int gc2145_disable_streams(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state, u32 pad,
				  u64 streams_mask)
{
	struct gc2145 *gc2145 = to_gc2145(sd);
	struct i2c_client *client = v4l2_get_subdevdata(&gc2145->sd);
	int ret = 0;

	/* Disable lanes & mipi streaming */
	cci_write(gc2145->regmap, GC2145_REG_PAGE_SELECT, 0x03, &ret);
	cci_update_bits(gc2145->regmap, GC2145_REG_BUF_CSI2_MODE,
			GC2145_CSI2_MODE_EN | GC2145_CSI2_MODE_MIPI_EN, 0,
			&ret);
	cci_write(gc2145->regmap, GC2145_REG_PAGE_SELECT, 0x00, &ret);
	if (ret)
		dev_err(&client->dev, "%s failed to write regs\n", __func__);

	pm_runtime_mark_last_busy(&client->dev);
	pm_runtime_put_autosuspend(&client->dev);

	return ret;
}

/* Power/clock management functions */
static int gc2145_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct gc2145 *gc2145 = to_gc2145(sd);
	int ret;

	ret = regulator_bulk_enable(GC2145_NUM_SUPPLIES, gc2145->supplies);
	if (ret) {
		dev_err(dev, "failed to enable regulators\n");
		return ret;
	}

	ret = clk_prepare_enable(gc2145->xclk);
	if (ret) {
		dev_err(dev, "failed to enable clock\n");
		goto reg_off;
	}

	gpiod_set_value_cansleep(gc2145->powerdown_gpio, 0);
	gpiod_set_value_cansleep(gc2145->reset_gpio, 0);

	/*
	 * Datasheet doesn't mention timing between PWDN/RESETB control and
	 * i2c access however, experimentation shows that a rather big delay is
	 * needed.
	 */
	msleep(41);

	return 0;

reg_off:
	regulator_bulk_disable(GC2145_NUM_SUPPLIES, gc2145->supplies);

	return ret;
}

static int gc2145_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct gc2145 *gc2145 = to_gc2145(sd);

	gpiod_set_value_cansleep(gc2145->powerdown_gpio, 1);
	gpiod_set_value_cansleep(gc2145->reset_gpio, 1);
	clk_disable_unprepare(gc2145->xclk);
	regulator_bulk_disable(GC2145_NUM_SUPPLIES, gc2145->supplies);

	return 0;
}

static int gc2145_get_regulators(struct gc2145 *gc2145)
{
	struct i2c_client *client = v4l2_get_subdevdata(&gc2145->sd);
	unsigned int i;

	for (i = 0; i < GC2145_NUM_SUPPLIES; i++)
		gc2145->supplies[i].supply = gc2145_supply_name[i];

	return devm_regulator_bulk_get(&client->dev, GC2145_NUM_SUPPLIES,
				       gc2145->supplies);
}

/* Verify chip ID */
static int gc2145_identify_module(struct gc2145 *gc2145)
{
	struct i2c_client *client = v4l2_get_subdevdata(&gc2145->sd);
	int ret;
	u64 chip_id;

	ret = cci_read(gc2145->regmap, GC2145_REG_CHIP_ID, &chip_id, NULL);
	if (ret) {
		dev_err(&client->dev, "failed to read chip id (%d)\n", ret);
		return ret;
	}

	if (chip_id != GC2145_CHIP_ID) {
		dev_err(&client->dev, "chip id mismatch: %x!=%llx\n",
			GC2145_CHIP_ID, chip_id);
		return -EIO;
	}

	return 0;
}

static const char * const test_pattern_menu[] = {
	"Disabled",
	"Colored patterns",
	"Uniform white",
	"Uniform yellow",
	"Uniform cyan",
	"Uniform green",
	"Uniform magenta",
	"Uniform red",
	"Uniform black",
};

#define GC2145_TEST_PATTERN_ENABLE	BIT(0)
#define GC2145_TEST_PATTERN_UXGA	BIT(3)

#define GC2145_TEST_UNIFORM		BIT(3)
#define GC2145_TEST_WHITE		(4 << 4)
#define GC2145_TEST_YELLOW		(8 << 4)
#define GC2145_TEST_CYAN		(9 << 4)
#define GC2145_TEST_GREEN		(6 << 4)
#define GC2145_TEST_MAGENTA		(10 << 4)
#define GC2145_TEST_RED			(5 << 4)
#define GC2145_TEST_BLACK		(0)

static const u8 test_pattern_val[] = {
	0,
	GC2145_TEST_PATTERN_ENABLE,
	GC2145_TEST_UNIFORM | GC2145_TEST_WHITE,
	GC2145_TEST_UNIFORM | GC2145_TEST_YELLOW,
	GC2145_TEST_UNIFORM | GC2145_TEST_CYAN,
	GC2145_TEST_UNIFORM | GC2145_TEST_GREEN,
	GC2145_TEST_UNIFORM | GC2145_TEST_MAGENTA,
	GC2145_TEST_UNIFORM | GC2145_TEST_RED,
	GC2145_TEST_UNIFORM | GC2145_TEST_BLACK,
};

static const struct v4l2_subdev_video_ops gc2145_video_ops = {
	.s_stream = v4l2_subdev_s_stream_helper,
};

static const struct v4l2_subdev_pad_ops gc2145_pad_ops = {
	.enum_mbus_code = gc2145_enum_mbus_code,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = gc2145_set_pad_format,
	.get_selection = gc2145_get_selection,
	.enum_frame_size = gc2145_enum_frame_size,
	.enable_streams = gc2145_enable_streams,
	.disable_streams = gc2145_disable_streams,
};

static const struct v4l2_subdev_ops gc2145_subdev_ops = {
	.video = &gc2145_video_ops,
	.pad = &gc2145_pad_ops,
};

static const struct v4l2_subdev_internal_ops gc2145_subdev_internal_ops = {
	.init_state = gc2145_init_state,
};

static int gc2145_set_ctrl_test_pattern(struct gc2145 *gc2145, int value)
{
	int ret = 0;

	if (!value) {
		/* Disable test pattern */
		cci_write(gc2145->regmap, GC2145_REG_DEBUG_MODE2, 0, &ret);
		return cci_write(gc2145->regmap, GC2145_REG_DEBUG_MODE3, 0,
				 &ret);
	}

	/* Enable test pattern, colored or uniform */
	cci_write(gc2145->regmap, GC2145_REG_DEBUG_MODE2,
		  GC2145_TEST_PATTERN_ENABLE | GC2145_TEST_PATTERN_UXGA, &ret);

	if (!(test_pattern_val[value] & GC2145_TEST_UNIFORM))
		return cci_write(gc2145->regmap, GC2145_REG_DEBUG_MODE3, 0,
				 &ret);

	/* Uniform */
	return cci_write(gc2145->regmap, GC2145_REG_DEBUG_MODE3,
			 test_pattern_val[value], &ret);
}

static int gc2145_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = gc2145_ctrl_to_sd(ctrl);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct gc2145 *gc2145 = to_gc2145(sd);
	int ret;

	if (pm_runtime_get_if_in_use(&client->dev) == 0)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_HBLANK:
		ret = cci_write(gc2145->regmap, GC2145_REG_HBLANK, ctrl->val,
				NULL);
		break;
	case V4L2_CID_VBLANK:
		ret = cci_write(gc2145->regmap, GC2145_REG_VBLANK, ctrl->val,
				NULL);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = gc2145_set_ctrl_test_pattern(gc2145, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ret = cci_update_bits(gc2145->regmap, GC2145_REG_ANALOG_MODE1,
				      BIT(0), (ctrl->val ? BIT(0) : 0), NULL);
		break;
	case V4L2_CID_VFLIP:
		ret = cci_update_bits(gc2145->regmap, GC2145_REG_ANALOG_MODE1,
				      BIT(1), (ctrl->val ? BIT(1) : 0), NULL);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_mark_last_busy(&client->dev);
	pm_runtime_put_autosuspend(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops gc2145_ctrl_ops = {
	.s_ctrl = gc2145_s_ctrl,
};

/* Initialize control handlers */
static int gc2145_init_controls(struct gc2145 *gc2145)
{
	struct i2c_client *client = v4l2_get_subdevdata(&gc2145->sd);
	const struct v4l2_ctrl_ops *ops = &gc2145_ctrl_ops;
	struct gc2145_ctrls *ctrls = &gc2145->ctrls;
	struct v4l2_ctrl_handler *hdl = &ctrls->handler;
	struct v4l2_fwnode_device_properties props;
	int ret;

	ret = v4l2_ctrl_handler_init(hdl, 12);
	if (ret)
		return ret;

	ctrls->pixel_rate = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_PIXEL_RATE,
					      GC2145_640_480_PIXELRATE,
					      GC2145_1600_1200_PIXELRATE, 1,
					      supported_modes[0].pixel_rate);

	ctrls->link_freq = v4l2_ctrl_new_int_menu(hdl, ops, V4L2_CID_LINK_FREQ,
						  ARRAY_SIZE(gc2145_link_freq_menu) - 1,
						  0, gc2145_link_freq_menu);
	if (ctrls->link_freq)
		ctrls->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	ctrls->hblank = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_HBLANK,
					  0, 0xfff, 1, GC2145_640_480_HBLANK);

	ctrls->vblank = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_VBLANK,
					  0, 0x1fff, 1, GC2145_640_480_VBLANK);

	ctrls->test_pattern =
		v4l2_ctrl_new_std_menu_items(hdl, ops, V4L2_CID_TEST_PATTERN,
					     ARRAY_SIZE(test_pattern_menu) - 1,
					     0, 0, test_pattern_menu);
	ctrls->hflip = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_HFLIP,
					 0, 1, 1, 0);
	ctrls->vflip = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_VFLIP,
					 0, 1, 1, 0);

	if (hdl->error) {
		ret = hdl->error;
		dev_err(&client->dev, "control init failed (%d)\n", ret);
		goto error;
	}

	ret = v4l2_fwnode_device_parse(&client->dev, &props);
	if (ret)
		goto error;

	ret = v4l2_ctrl_new_fwnode_properties(hdl, &gc2145_ctrl_ops,
					      &props);
	if (ret)
		goto error;

	gc2145->sd.ctrl_handler = hdl;

	return 0;

error:
	v4l2_ctrl_handler_free(hdl);

	return ret;
}

static int gc2145_check_hwcfg(struct device *dev)
{
	struct fwnode_handle *endpoint;
	struct v4l2_fwnode_endpoint ep_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	int ret;

	endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
	if (!endpoint) {
		dev_err(dev, "endpoint node not found\n");
		return -EINVAL;
	}

	ret = v4l2_fwnode_endpoint_alloc_parse(endpoint, &ep_cfg);
	fwnode_handle_put(endpoint);
	if (ret)
		return ret;

	/* Check the number of MIPI CSI2 data lanes */
	if (ep_cfg.bus.mipi_csi2.num_data_lanes != 2) {
		dev_err(dev, "only 2 data lanes are currently supported\n");
		ret = -EINVAL;
		goto out;
	}

	/* Check the link frequency set in device tree */
	if (!ep_cfg.nr_of_link_frequencies) {
		dev_err(dev, "link-frequency property not found in DT\n");
		ret = -EINVAL;
		goto out;
	}

	if (ep_cfg.nr_of_link_frequencies != 3 ||
	    ep_cfg.link_frequencies[0] != GC2145_640_480_LINKFREQ ||
	    ep_cfg.link_frequencies[1] != GC2145_1280_720_LINKFREQ ||
	    ep_cfg.link_frequencies[2] != GC2145_1600_1200_LINKFREQ) {
		dev_err(dev, "Invalid link-frequencies provided\n");
		ret = -EINVAL;
	}

out:
	v4l2_fwnode_endpoint_free(&ep_cfg);

	return ret;
}

static int gc2145_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	unsigned int xclk_freq;
	struct gc2145 *gc2145;
	int ret;

	gc2145 = devm_kzalloc(&client->dev, sizeof(*gc2145), GFP_KERNEL);
	if (!gc2145)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&gc2145->sd, client, &gc2145_subdev_ops);
	gc2145->sd.internal_ops = &gc2145_subdev_internal_ops;

	/* Check the hardware configuration in device tree */
	if (gc2145_check_hwcfg(dev))
		return -EINVAL;

	/* Get system clock (xclk) */
	gc2145->xclk = devm_clk_get(dev, NULL);
	if (IS_ERR(gc2145->xclk))
		return dev_err_probe(dev, PTR_ERR(gc2145->xclk),
				     "failed to get xclk\n");

	xclk_freq = clk_get_rate(gc2145->xclk);
	if (xclk_freq != GC2145_XCLK_FREQ) {
		dev_err(dev, "xclk frequency not supported: %d Hz\n",
			xclk_freq);
		return -EINVAL;
	}

	ret = gc2145_get_regulators(gc2145);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to get regulators\n");

	/* Request optional reset pin */
	gc2145->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						     GPIOD_OUT_HIGH);
	if (IS_ERR(gc2145->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(gc2145->reset_gpio),
				     "failed to get reset_gpio\n");

	/* Request optional powerdown pin */
	gc2145->powerdown_gpio = devm_gpiod_get_optional(dev, "powerdown",
							 GPIOD_OUT_HIGH);
	if (IS_ERR(gc2145->powerdown_gpio))
		return dev_err_probe(dev, PTR_ERR(gc2145->powerdown_gpio),
				     "failed to get powerdown_gpio\n");

	/* Initialise the regmap for further cci access */
	gc2145->regmap = devm_cci_regmap_init_i2c(client, 8);
	if (IS_ERR(gc2145->regmap))
		return dev_err_probe(dev, PTR_ERR(gc2145->regmap),
				     "failed to get cci regmap\n");

	/*
	 * The sensor must be powered for gc2145_identify_module()
	 * to be able to read the CHIP_ID register
	 */
	ret = gc2145_power_on(dev);
	if (ret)
		return ret;

	ret = gc2145_identify_module(gc2145);
	if (ret)
		goto error_power_off;

	/* Set default mode */
	gc2145->mode = &supported_modes[0];

	ret = gc2145_init_controls(gc2145);
	if (ret)
		goto error_power_off;

	/* Initialize subdev */
	gc2145->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	gc2145->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	/* Initialize source pad */
	gc2145->pad.flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&gc2145->sd.entity, 1, &gc2145->pad);
	if (ret) {
		dev_err(dev, "failed to init entity pads: %d\n", ret);
		goto error_handler_free;
	}

	gc2145->sd.state_lock = gc2145->ctrls.handler.lock;
	ret = v4l2_subdev_init_finalize(&gc2145->sd);
	if (ret < 0) {
		dev_err(dev, "subdev init error: %d\n", ret);
		goto error_media_entity;
	}

	/* Enable runtime PM and turn off the device */
	pm_runtime_set_active(dev);
	pm_runtime_get_noresume(&client->dev);
	pm_runtime_enable(dev);

	pm_runtime_set_autosuspend_delay(&client->dev, 1000);
	pm_runtime_use_autosuspend(&client->dev);
	pm_runtime_put_autosuspend(&client->dev);

	ret = v4l2_async_register_subdev_sensor(&gc2145->sd);
	if (ret < 0) {
		dev_err(dev, "failed to register sensor sub-device: %d\n", ret);
		goto error_subdev_cleanup;
	}

	return 0;

error_subdev_cleanup:
	v4l2_subdev_cleanup(&gc2145->sd);
	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);

error_media_entity:
	media_entity_cleanup(&gc2145->sd.entity);

error_handler_free:
	v4l2_ctrl_handler_free(&gc2145->ctrls.handler);

error_power_off:
	gc2145_power_off(dev);

	return ret;
}

static void gc2145_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc2145 *gc2145 = to_gc2145(sd);

	v4l2_subdev_cleanup(sd);
	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(&gc2145->ctrls.handler);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		gc2145_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);
}

static const struct of_device_id gc2145_dt_ids[] = {
	{ .compatible = "galaxycore,gc2145" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, gc2145_dt_ids);

static const struct dev_pm_ops gc2145_pm_ops = {
	RUNTIME_PM_OPS(gc2145_power_off, gc2145_power_on, NULL)
};

static struct i2c_driver gc2145_i2c_driver = {
	.driver = {
		.name = "gc2145",
		.of_match_table	= gc2145_dt_ids,
		.pm = pm_ptr(&gc2145_pm_ops),
	},
	.probe = gc2145_probe,
	.remove = gc2145_remove,
};

module_i2c_driver(gc2145_i2c_driver);

MODULE_AUTHOR("Alain Volmat <alain.volmat@foss.st.com>");
MODULE_DESCRIPTION("GalaxyCore GC2145 sensor driver");
MODULE_LICENSE("GPL");
