/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2010 Juergen Beisert, Pengutronix
 * Copyright (C) 2016 Marek Vasut <marex@denx.de>
 *
 * i.MX23/i.MX28/i.MX6SX MXSFB LCD controller driver.
 */

#ifndef __MXSFB_REGS_H__
#define __MXSFB_REGS_H__

#define REG_SET	4
#define REG_CLR	8

#define LCDC_CTRL			0x00
#define LCDC_CTRL1			0x10
#define LCDC_V3_TRANSFER_COUNT		0x20
#define LCDC_V4_CTRL2			0x20
#define LCDC_V4_TRANSFER_COUNT		0x30
#define LCDC_V4_CUR_BUF			0x40
#define LCDC_V4_NEXT_BUF		0x50
#define LCDC_V3_CUR_BUF			0x30
#define LCDC_V3_NEXT_BUF		0x40
#define LCDC_VDCTRL0			0x70
#define LCDC_VDCTRL1			0x80
#define LCDC_VDCTRL2			0x90
#define LCDC_VDCTRL3			0xa0
#define LCDC_VDCTRL4			0xb0
#define LCDC_V4_DEBUG0			0x1d0
#define LCDC_V3_DEBUG0			0x1f0
#define LCDC_AS_CTRL			0x210
#define LCDC_AS_BUF			0x220
#define LCDC_AS_NEXT_BUF		0x230
#define LCDC_AS_CLRKEYLOW		0x240
#define LCDC_AS_CLRKEYHIGH		0x250

#define CTRL_SFTRST			BIT(31)
#define CTRL_CLKGATE			BIT(30)
#define CTRL_BYPASS_COUNT		BIT(19)
#define CTRL_VSYNC_MODE			BIT(18)
#define CTRL_DOTCLK_MODE		BIT(17)
#define CTRL_DATA_SELECT		BIT(16)
#define CTRL_BUS_WIDTH_16		(0 << 10)
#define CTRL_BUS_WIDTH_8		(1 << 10)
#define CTRL_BUS_WIDTH_18		(2 << 10)
#define CTRL_BUS_WIDTH_24		(3 << 10)
#define CTRL_BUS_WIDTH_MASK		(0x3 << 10)
#define CTRL_WORD_LENGTH_16		(0 << 8)
#define CTRL_WORD_LENGTH_8		(1 << 8)
#define CTRL_WORD_LENGTH_18		(2 << 8)
#define CTRL_WORD_LENGTH_24		(3 << 8)
#define CTRL_MASTER			BIT(5)
#define CTRL_DF16			BIT(3)
#define CTRL_DF18			BIT(2)
#define CTRL_DF24			BIT(1)
#define CTRL_RUN			BIT(0)

#define CTRL1_RECOVER_ON_UNDERFLOW	BIT(24)
#define CTRL1_FIFO_CLEAR		BIT(21)
#define CTRL1_SET_BYTE_PACKAGING(x)	(((x) & 0xf) << 16)
#define CTRL1_GET_BYTE_PACKAGING(x)	(((x) >> 16) & 0xf)
#define CTRL1_CUR_FRAME_DONE_IRQ_EN	BIT(13)
#define CTRL1_CUR_FRAME_DONE_IRQ	BIT(9)

#define CTRL2_SET_OUTSTANDING_REQS_1	0
#define CTRL2_SET_OUTSTANDING_REQS_2	(0x1 << 21)
#define CTRL2_SET_OUTSTANDING_REQS_4	(0x2 << 21)
#define CTRL2_SET_OUTSTANDING_REQS_8	(0x3 << 21)
#define CTRL2_SET_OUTSTANDING_REQS_16	(0x4 << 21)
#define CTRL2_SET_OUTSTANDING_REQS_MASK	(0x7 << 21)

#define TRANSFER_COUNT_SET_VCOUNT(x)	(((x) & 0xffff) << 16)
#define TRANSFER_COUNT_GET_VCOUNT(x)	(((x) >> 16) & 0xffff)
#define TRANSFER_COUNT_SET_HCOUNT(x)	((x) & 0xffff)
#define TRANSFER_COUNT_GET_HCOUNT(x)	((x) & 0xffff)

#define VDCTRL0_ENABLE_PRESENT		BIT(28)
#define VDCTRL0_VSYNC_ACT_HIGH		BIT(27)
#define VDCTRL0_HSYNC_ACT_HIGH		BIT(26)
#define VDCTRL0_DOTCLK_ACT_FALLING	BIT(25)
#define VDCTRL0_ENABLE_ACT_HIGH		BIT(24)
#define VDCTRL0_VSYNC_PERIOD_UNIT	BIT(21)
#define VDCTRL0_VSYNC_PULSE_WIDTH_UNIT	BIT(20)
#define VDCTRL0_HALF_LINE		BIT(19)
#define VDCTRL0_HALF_LINE_MODE		BIT(18)
#define VDCTRL0_SET_VSYNC_PULSE_WIDTH(x) ((x) & 0x3ffff)
#define VDCTRL0_GET_VSYNC_PULSE_WIDTH(x) ((x) & 0x3ffff)

#define VDCTRL2_SET_HSYNC_PERIOD(x)	((x) & 0x3ffff)
#define VDCTRL2_GET_HSYNC_PERIOD(x)	((x) & 0x3ffff)

#define VDCTRL3_MUX_SYNC_SIGNALS	BIT(29)
#define VDCTRL3_VSYNC_ONLY		BIT(28)
#define SET_HOR_WAIT_CNT(x)		(((x) & 0xfff) << 16)
#define GET_HOR_WAIT_CNT(x)		(((x) >> 16) & 0xfff)
#define SET_VERT_WAIT_CNT(x)		((x) & 0xffff)
#define GET_VERT_WAIT_CNT(x)		((x) & 0xffff)

#define VDCTRL4_SET_DOTCLK_DLY(x)	(((x) & 0x7) << 29) /* v4 only */
#define VDCTRL4_GET_DOTCLK_DLY(x)	(((x) >> 29) & 0x7) /* v4 only */
#define VDCTRL4_SYNC_SIGNALS_ON		BIT(18)
#define SET_DOTCLK_H_VALID_DATA_CNT(x)	((x) & 0x3ffff)

#define DEBUG0_HSYNC			BIT(26)
#define DEBUG0_VSYNC			BIT(25)

#define AS_CTRL_PS_DISABLE		BIT(23)
#define AS_CTRL_ALPHA_INVERT		BIT(20)
#define AS_CTRL_ALPHA(a)		(((a) & 0xff) << 8)
#define AS_CTRL_FORMAT_RGB565		(0xe << 4)
#define AS_CTRL_FORMAT_RGB444		(0xd << 4)
#define AS_CTRL_FORMAT_RGB555		(0xc << 4)
#define AS_CTRL_FORMAT_ARGB4444		(0x9 << 4)
#define AS_CTRL_FORMAT_ARGB1555		(0x8 << 4)
#define AS_CTRL_FORMAT_RGB888		(0x4 << 4)
#define AS_CTRL_FORMAT_ARGB8888		(0x0 << 4)
#define AS_CTRL_ENABLE_COLORKEY		BIT(3)
#define AS_CTRL_ALPHA_CTRL_ROP		(3 << 1)
#define AS_CTRL_ALPHA_CTRL_MULTIPLY	(2 << 1)
#define AS_CTRL_ALPHA_CTRL_OVERRIDE	(1 << 1)
#define AS_CTRL_ALPHA_CTRL_EMBEDDED	(0 << 1)
#define AS_CTRL_AS_ENABLE		BIT(0)

#define MXSFB_MIN_XRES			120
#define MXSFB_MIN_YRES			120
#define MXSFB_MAX_XRES			0xffff
#define MXSFB_MAX_YRES			0xffff

#endif /* __MXSFB_REGS_H__ */
