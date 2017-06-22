/*
 * Copyright (C) 2010 Juergen Beisert, Pengutronix
 * Copyright (C) 2016 Marek Vasut <marex@denx.de>
 *
 * i.MX23/i.MX28/i.MX6SX MXSFB LCD controller driver.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __MXSFB_REGS_H__
#define __MXSFB_REGS_H__

#define REG_SET	4
#define REG_CLR	8

#define LCDC_CTRL			0x00
#define LCDC_CTRL1			0x10
#define LCDC_V3_TRANSFER_COUNT		0x20
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

#define CTRL_SFTRST			(1 << 31)
#define CTRL_CLKGATE			(1 << 30)
#define CTRL_BYPASS_COUNT		(1 << 19)
#define CTRL_VSYNC_MODE			(1 << 18)
#define CTRL_DOTCLK_MODE		(1 << 17)
#define CTRL_DATA_SELECT		(1 << 16)
#define CTRL_SET_BUS_WIDTH(x)		(((x) & 0x3) << 10)
#define CTRL_GET_BUS_WIDTH(x)		(((x) >> 10) & 0x3)
#define CTRL_BUS_WIDTH_MASK		(0x3 << 10)
#define CTRL_SET_WORD_LENGTH(x)		(((x) & 0x3) << 8)
#define CTRL_GET_WORD_LENGTH(x)		(((x) >> 8) & 0x3)
#define CTRL_MASTER			(1 << 5)
#define CTRL_DF16			(1 << 3)
#define CTRL_DF18			(1 << 2)
#define CTRL_DF24			(1 << 1)
#define CTRL_RUN			(1 << 0)

#define CTRL1_FIFO_CLEAR		(1 << 21)
#define CTRL1_SET_BYTE_PACKAGING(x)	(((x) & 0xf) << 16)
#define CTRL1_GET_BYTE_PACKAGING(x)	(((x) >> 16) & 0xf)
#define CTRL1_CUR_FRAME_DONE_IRQ_EN	(1 << 13)
#define CTRL1_CUR_FRAME_DONE_IRQ	(1 << 9)

#define TRANSFER_COUNT_SET_VCOUNT(x)	(((x) & 0xffff) << 16)
#define TRANSFER_COUNT_GET_VCOUNT(x)	(((x) >> 16) & 0xffff)
#define TRANSFER_COUNT_SET_HCOUNT(x)	((x) & 0xffff)
#define TRANSFER_COUNT_GET_HCOUNT(x)	((x) & 0xffff)

#define VDCTRL0_ENABLE_PRESENT		(1 << 28)
#define VDCTRL0_VSYNC_ACT_HIGH		(1 << 27)
#define VDCTRL0_HSYNC_ACT_HIGH		(1 << 26)
#define VDCTRL0_DOTCLK_ACT_FALLING	(1 << 25)
#define VDCTRL0_ENABLE_ACT_HIGH		(1 << 24)
#define VDCTRL0_VSYNC_PERIOD_UNIT	(1 << 21)
#define VDCTRL0_VSYNC_PULSE_WIDTH_UNIT	(1 << 20)
#define VDCTRL0_HALF_LINE		(1 << 19)
#define VDCTRL0_HALF_LINE_MODE		(1 << 18)
#define VDCTRL0_SET_VSYNC_PULSE_WIDTH(x) ((x) & 0x3ffff)
#define VDCTRL0_GET_VSYNC_PULSE_WIDTH(x) ((x) & 0x3ffff)

#define VDCTRL2_SET_HSYNC_PERIOD(x)	((x) & 0x3ffff)
#define VDCTRL2_GET_HSYNC_PERIOD(x)	((x) & 0x3ffff)

#define VDCTRL3_MUX_SYNC_SIGNALS	(1 << 29)
#define VDCTRL3_VSYNC_ONLY		(1 << 28)
#define SET_HOR_WAIT_CNT(x)		(((x) & 0xfff) << 16)
#define GET_HOR_WAIT_CNT(x)		(((x) >> 16) & 0xfff)
#define SET_VERT_WAIT_CNT(x)		((x) & 0xffff)
#define GET_VERT_WAIT_CNT(x)		((x) & 0xffff)

#define VDCTRL4_SET_DOTCLK_DLY(x)	(((x) & 0x7) << 29) /* v4 only */
#define VDCTRL4_GET_DOTCLK_DLY(x)	(((x) >> 29) & 0x7) /* v4 only */
#define VDCTRL4_SYNC_SIGNALS_ON		(1 << 18)
#define SET_DOTCLK_H_VALID_DATA_CNT(x)	((x) & 0x3ffff)

#define DEBUG0_HSYNC			(1 < 26)
#define DEBUG0_VSYNC			(1 < 25)

#define MXSFB_MIN_XRES			120
#define MXSFB_MIN_YRES			120
#define MXSFB_MAX_XRES			0xffff
#define MXSFB_MAX_YRES			0xffff

#define RED 0
#define GREEN 1
#define BLUE 2
#define TRANSP 3

#define STMLCDIF_8BIT  1 /* pixel data bus to the display is of 8 bit width */
#define STMLCDIF_16BIT 0 /* pixel data bus to the display is of 16 bit width */
#define STMLCDIF_18BIT 2 /* pixel data bus to the display is of 18 bit width */
#define STMLCDIF_24BIT 3 /* pixel data bus to the display is of 24 bit width */

#define MXSFB_SYNC_DATA_ENABLE_HIGH_ACT	(1 << 6)
#define MXSFB_SYNC_DOTCLK_FALLING_ACT	(1 << 7) /* negative edge sampling */

#endif /* __MXSFB_REGS_H__ */
