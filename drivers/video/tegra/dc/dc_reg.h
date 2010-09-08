/*
 * drivers/video/tegra/dc/dc_reg.h
 *
 * Copyright (C) 2010 Google, Inc.
 * Author: Erik Gilling <konkers@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __DRIVERS_VIDEO_TEGRA_DC_DC_REG_H
#define __DRIVERS_VIDEO_TEGRA_DC_DC_REG_H

#define DC_CMD_GENERAL_INCR_SYNCPT		0x000
#define DC_CMD_GENERAL_INCR_SYNCPT_CNTRL	0x001
#define DC_CMD_GENERAL_INCR_SYNCPT_ERROR	0x002
#define DC_CMD_WIN_A_INCR_SYNCPT		0x008
#define DC_CMD_WIN_A_INCR_SYNCPT_CNTRL		0x009
#define DC_CMD_WIN_A_INCR_SYNCPT_ERROR		0x00a
#define DC_CMD_WIN_B_INCR_SYNCPT		0x010
#define DC_CMD_WIN_B_INCR_SYNCPT_CNTRL		0x011
#define DC_CMD_WIN_B_INCR_SYNCPT_ERROR		0x012
#define DC_CMD_WIN_C_INCR_SYNCPT		0x018
#define DC_CMD_WIN_C_INCR_SYNCPT_CNTRL		0x019
#define DC_CMD_WIN_C_INCR_SYNCPT_ERROR		0x01a
#define DC_CMD_CONT_SYNCPT_VSYNC		0x028
#define DC_CMD_DISPLAY_COMMAND_OPTION0		0x031
#define DC_CMD_DISPLAY_COMMAND			0x032
#define  DISP_COMMAND_RAISE		(1 << 0)
#define  DISP_CTRL_MODE_STOP		(0 << 5)
#define  DISP_CTRL_MODE_C_DISPLAY	(1 << 5)
#define  DISP_CTRL_MODE_NC_DISPLAY	(2 << 5)
#define  DISP_COMMAND_RAISE_VECTOR(x)	(((x) & 0x1f) << 22)
#define  DISP_COMMAND_RAISE_CHANNEL_ID(x)	(((x) & 0xf) << 27)

#define DC_CMD_SIGNAL_RAISE			0x033
#define DC_CMD_DISPLAY_POWER_CONTROL		0x036
#define  PW0_ENABLE		(1 << 0)
#define  PW1_ENABLE		(1 << 2)
#define  PW2_ENABLE		(1 << 4)
#define  PW3_ENABLE		(1 << 6)
#define  PW4_ENABLE		(1 << 8)
#define  PM0_ENABLE		(1 << 16)
#define  PM1_ENABLE		(1 << 18)
#define  SPI_ENABLE		(1 << 24)
#define  HSPI_ENABLE		(1 << 25)

#define DC_CMD_INT_STATUS			0x037
#define DC_CMD_INT_MASK				0x038
#define DC_CMD_INT_ENABLE			0x039
#define DC_CMD_INT_TYPE				0x03a
#define DC_CMD_INT_POLARITY			0x03b
#define  CTXSW_INT		(1 << 0)
#define  FRAME_END_INT		(1 << 1)
#define  V_BLANK_INT		(1 << 2)
#define  H_BLANK_INT		(1 << 3)
#define  V_PULSE3_INT		(1 << 4)
#define  SPI_BUSY_INT		(1 << 7)
#define  WIN_A_UF_INT		(1 << 8)
#define  WIN_B_UF_INT		(1 << 9)
#define  WIN_C_UF_INT		(1 << 10)
#define  MSF_INT		(1 << 12)
#define  SSF_INT		(1 << 13)
#define  WIN_A_OF_INT		(1 << 14)
#define  WIN_B_OF_INT		(1 << 15)
#define  WIN_C_OF_INT		(1 << 16)
#define  GPIO_0_INT		(1 << 18)
#define  GPIO_1_INT		(1 << 19)
#define  GPIO_2_INT		(1 << 20)

#define DC_CMD_SIGNAL_RAISE1			0x03c
#define DC_CMD_SIGNAL_RAISE2			0x03d
#define DC_CMD_SIGNAL_RAISE3			0x03e
#define DC_CMD_STATE_ACCESS			0x040
#define DC_CMD_STATE_CONTROL			0x041
#define  GENERAL_ACT_REQ	(1 << 0)
#define  WIN_A_ACT_REQ		(1 << 1)
#define  WIN_B_ACT_REQ		(1 << 2)
#define  WIN_C_ACT_REQ		(1 << 3)

#define DC_CMD_DISPLAY_WINDOW_HEADER		0x042
#define  WINDOW_A_SELECT		(1 << 4)
#define  WINDOW_B_SELECT		(1 << 5)
#define  WINDOW_C_SELECT		(1 << 6)

#define DC_CMD_REG_ACT_CONTROL			0x043

#define DC_COM_CRC_CONTROL			0x300
#define DC_COM_CRC_CHECKSUM			0x301
#define DC_COM_PIN_OUTPUT_ENABLE0		0x302
#define DC_COM_PIN_OUTPUT_ENABLE1		0x303
#define DC_COM_PIN_OUTPUT_ENABLE2		0x304
#define DC_COM_PIN_OUTPUT_ENABLE3		0x305
#define DC_COM_PIN_OUTPUT_POLARITY0		0x306
#define DC_COM_PIN_OUTPUT_POLARITY1		0x307
#define DC_COM_PIN_OUTPUT_POLARITY2		0x308
#define DC_COM_PIN_OUTPUT_POLARITY3		0x309
#define DC_COM_PIN_OUTPUT_DATA0			0x30a
#define DC_COM_PIN_OUTPUT_DATA1			0x30b
#define DC_COM_PIN_OUTPUT_DATA2			0x30c
#define DC_COM_PIN_OUTPUT_DATA3			0x30d
#define DC_COM_PIN_INPUT_ENABLE0		0x30e
#define DC_COM_PIN_INPUT_ENABLE1		0x30f
#define DC_COM_PIN_INPUT_ENABLE2		0x310
#define DC_COM_PIN_INPUT_ENABLE3		0x311
#define DC_COM_PIN_INPUT_DATA0			0x312
#define DC_COM_PIN_INPUT_DATA1			0x313
#define DC_COM_PIN_OUTPUT_SELECT0		0x314
#define DC_COM_PIN_OUTPUT_SELECT1		0x315
#define DC_COM_PIN_OUTPUT_SELECT2		0x316
#define DC_COM_PIN_OUTPUT_SELECT3		0x317
#define DC_COM_PIN_OUTPUT_SELECT4		0x318
#define DC_COM_PIN_OUTPUT_SELECT5		0x319
#define DC_COM_PIN_OUTPUT_SELECT6		0x31a
#define DC_COM_PIN_MISC_CONTROL			0x31b
#define DC_COM_PM0_CONTROL			0x31c
#define DC_COM_PM0_DUTY_CYCLE			0x31d
#define DC_COM_PM1_CONTROL			0x31e
#define DC_COM_PM1_DUTY_CYCLE			0x31f
#define DC_COM_SPI_CONTROL			0x320
#define DC_COM_SPI_START_BYTE			0x321
#define DC_COM_HSPI_WRITE_DATA_AB		0x322
#define DC_COM_HSPI_WRITE_DATA_CD		0x323
#define DC_COM_HSPI_CS_DC			0x324
#define DC_COM_SCRATCH_REGISTER_A		0x325
#define DC_COM_SCRATCH_REGISTER_B		0x326
#define DC_COM_GPIO_CTRL			0x327
#define DC_COM_GPIO_DEBOUNCE_COUNTER		0x328
#define DC_COM_CRC_CHECKSUM_LATCHED		0x329

#define DC_DISP_DISP_SIGNAL_OPTIONS0		0x400
#define  H_PULSE_0_ENABLE		(1 << 8)
#define  H_PULSE_1_ENABLE		(1 << 10)
#define  H_PULSE_2_ENABLE		(1 << 12)
#define  V_PULSE_0_ENABLE		(1 << 16)
#define  V_PULSE_1_ENABLE		(1 << 18)
#define  V_PULSE_2_ENABLE		(1 << 19)
#define  V_PULSE_3_ENABLE		(1 << 20)
#define  M0_ENABLE			(1 << 24)
#define  M1_ENABLE			(1 << 26)

#define DC_DISP_DISP_SIGNAL_OPTIONS1		0x401
#define  DI_ENABLE			(1 << 16)
#define  PP_ENABLE			(1 << 18)

#define DC_DISP_DISP_WIN_OPTIONS		0x402
#define  CURSOR_ENABLE			(1 << 16)
#define  TVO_ENABLE			(1 << 28)
#define  DSI_ENABLE			(1 << 29)
#define  HDMI_ENABLE			(1 << 30)

#define DC_DISP_MEM_HIGH_PRIORITY		0x403
#define DC_DISP_MEM_HIGH_PRIORITY_TIMER		0x404
#define DC_DISP_DISP_TIMING_OPTIONS		0x405
#define  VSYNC_H_POSITION(x)		((x) & 0xfff)

#define DC_DISP_REF_TO_SYNC			0x406
#define DC_DISP_SYNC_WIDTH			0x407
#define DC_DISP_BACK_PORCH			0x408
#define DC_DISP_DISP_ACTIVE			0x409
#define DC_DISP_FRONT_PORCH			0x40a
#define DC_DISP_H_PULSE0_CONTROL		0x40b
#define DC_DISP_H_PULSE0_POSITION_A		0x40c
#define DC_DISP_H_PULSE0_POSITION_B		0x40d
#define DC_DISP_H_PULSE0_POSITION_C		0x40e
#define DC_DISP_H_PULSE0_POSITION_D		0x40f
#define DC_DISP_H_PULSE1_CONTROL		0x410
#define DC_DISP_H_PULSE1_POSITION_A		0x411
#define DC_DISP_H_PULSE1_POSITION_B		0x412
#define DC_DISP_H_PULSE1_POSITION_C		0x413
#define DC_DISP_H_PULSE1_POSITION_D		0x414
#define DC_DISP_H_PULSE2_CONTROL		0x415
#define DC_DISP_H_PULSE2_POSITION_A		0x416
#define DC_DISP_H_PULSE2_POSITION_B		0x417
#define DC_DISP_H_PULSE2_POSITION_C		0x418
#define DC_DISP_H_PULSE2_POSITION_D		0x419
#define DC_DISP_V_PULSE0_CONTROL		0x41a
#define DC_DISP_V_PULSE0_POSITION_A		0x41b
#define DC_DISP_V_PULSE0_POSITION_B		0x41c
#define DC_DISP_V_PULSE0_POSITION_C		0x41d
#define DC_DISP_V_PULSE1_CONTROL		0x41e
#define DC_DISP_V_PULSE1_POSITION_A		0x41f
#define DC_DISP_V_PULSE1_POSITION_B		0x420
#define DC_DISP_V_PULSE1_POSITION_C		0x421
#define DC_DISP_V_PULSE2_CONTROL		0x422
#define DC_DISP_V_PULSE2_POSITION_A		0x423
#define DC_DISP_V_PULSE3_CONTROL		0x424
#define DC_DISP_V_PULSE3_POSITION_A		0x425
#define DC_DISP_M0_CONTROL			0x426
#define DC_DISP_M1_CONTROL			0x427
#define DC_DISP_DI_CONTROL			0x428
#define DC_DISP_PP_CONTROL			0x429
#define DC_DISP_PP_SELECT_A			0x42a
#define DC_DISP_PP_SELECT_B			0x42b
#define DC_DISP_PP_SELECT_C			0x42c
#define DC_DISP_PP_SELECT_D			0x42d

#define  PULSE_MODE_NORMAL		(0 << 3)
#define  PULSE_MODE_ONE_CLOCK		(1 << 3)
#define  PULSE_POLARITY_HIGH		(0 << 4)
#define  PULSE_POLARITY_LOW		(1 << 4)
#define  PULSE_QUAL_ALWAYS		(0 << 6)
#define  PULSE_QUAL_VACTIVE		(2 << 6)
#define  PULSE_QUAL_VACTIVE1		(3 << 6)
#define  PULSE_LAST_START_A		(0 << 8)
#define  PULSE_LAST_END_A		(1 << 8)
#define  PULSE_LAST_START_B		(2 << 8)
#define  PULSE_LAST_END_B		(3 << 8)
#define  PULSE_LAST_START_C		(4 << 8)
#define  PULSE_LAST_END_C		(5 << 8)
#define  PULSE_LAST_START_D		(6 << 8)
#define  PULSE_LAST_END_D		(7 << 8)

#define  PULSE_START(x)			((x) & 0xfff)
#define  PULSE_END(x)			(((x) & 0xfff) << 16)

#define DC_DISP_DISP_CLOCK_CONTROL		0x42e
#define  PIXEL_CLK_DIVIDER_PCD1		(0 << 8)
#define  PIXEL_CLK_DIVIDER_PCD1H	(1 << 8)
#define  PIXEL_CLK_DIVIDER_PCD2		(2 << 8)
#define  PIXEL_CLK_DIVIDER_PCD3		(3 << 8)
#define  PIXEL_CLK_DIVIDER_PCD4		(4 << 8)
#define  PIXEL_CLK_DIVIDER_PCD6		(5 << 8)
#define  PIXEL_CLK_DIVIDER_PCD8		(6 << 8)
#define  PIXEL_CLK_DIVIDER_PCD9		(7 << 8)
#define  PIXEL_CLK_DIVIDER_PCD12	(8 << 8)
#define  PIXEL_CLK_DIVIDER_PCD16	(9 << 8)
#define  PIXEL_CLK_DIVIDER_PCD18	(10 << 8)
#define  PIXEL_CLK_DIVIDER_PCD24	(11 << 8)
#define  PIXEL_CLK_DIVIDER_PCD13	(12 << 8)
#define  SHIFT_CLK_DIVIDER(x)		((x) & 0xff)

#define DC_DISP_DISP_INTERFACE_CONTROL		0x42f
#define  DISP_DATA_FORMAT_DF1P1C	(0 << 0)
#define  DISP_DATA_FORMAT_DF1P2C24B	(1 << 0)
#define  DISP_DATA_FORMAT_DF1P2C18B	(2 << 0)
#define  DISP_DATA_FORMAT_DF1P2C16B	(3 << 0)
#define  DISP_DATA_FORMAT_DF2S		(5 << 0)
#define  DISP_DATA_FORMAT_DF3S		(6 << 0)
#define  DISP_DATA_FORMAT_DFSPI		(7 << 0)
#define  DISP_DATA_FORMAT_DF1P3C24B	(8 << 0)
#define  DISP_DATA_FORMAT_DF1P3C18B	(9 << 0)
#define  DISP_DATA_ALIGNMENT_MSB	(0 << 8)
#define  DISP_DATA_ALIGNMENT_LSB	(1 << 8)
#define  DISP_DATA_ORDER_RED_BLUE	(0 << 9)
#define  DISP_DATA_ORDER_BLUE_RED	(1 << 9)

#define DC_DISP_DISP_COLOR_CONTROL		0x430
#define  BASE_COLOR_SIZE666		(0 << 0)
#define  BASE_COLOR_SIZE111		(1 << 0)
#define  BASE_COLOR_SIZE222		(2 << 0)
#define  BASE_COLOR_SIZE333		(3 << 0)
#define  BASE_COLOR_SIZE444		(4 << 0)
#define  BASE_COLOR_SIZE555		(5 << 0)
#define  BASE_COLOR_SIZE565		(6 << 0)
#define  BASE_COLOR_SIZE332		(7 << 0)
#define  BASE_COLOR_SIZE888		(8 << 0)

#define  DITHER_CONTROL_DISABLE		(0 << 8)
#define  DITHER_CONTROL_ORDERED		(2 << 8)
#define  DITHER_CONTROL_ERRDIFF		(3 << 8)

#define DC_DISP_SHIFT_CLOCK_OPTIONS		0x431
#define DC_DISP_DATA_ENABLE_OPTIONS		0x432
#define   DE_SELECT_ACTIVE_BLANK	0x0
#define   DE_SELECT_ACTIVE		0x1
#define   DE_SELECT_ACTIVE_IS		0x2
#define   DE_CONTROL_ONECLK		(0 << 2)
#define   DE_CONTROL_NORMAL		(1 << 2)
#define   DE_CONTROL_EARLY_EXT		(2 << 2)
#define   DE_CONTROL_EARLY		(3 << 2)
#define   DE_CONTROL_ACTIVE_BLANK	(4 << 2)

#define DC_DISP_SERIAL_INTERFACE_OPTIONS	0x433
#define DC_DISP_LCD_SPI_OPTIONS			0x434
#define DC_DISP_BORDER_COLOR			0x435
#define DC_DISP_COLOR_KEY0_LOWER		0x436
#define DC_DISP_COLOR_KEY0_UPPER		0x437
#define DC_DISP_COLOR_KEY1_LOWER		0x438
#define DC_DISP_COLOR_KEY1_UPPER		0x439
#define DC_DISP_CURSOR_FOREGROUND		0x43c
#define DC_DISP_CURSOR_BACKGROUND		0x43d
#define DC_DISP_CURSOR_START_ADDR		0x43e
#define DC_DISP_CURSOR_START_ADDR_NS		0x43f
#define DC_DISP_CURSOR_POSITION			0x440
#define DC_DISP_CURSOR_POSITION_NS		0x441
#define DC_DISP_INIT_SEQ_CONTROL		0x442
#define DC_DISP_SPI_INIT_SEQ_DATA_A		0x443
#define DC_DISP_SPI_INIT_SEQ_DATA_B		0x444
#define DC_DISP_SPI_INIT_SEQ_DATA_C		0x445
#define DC_DISP_SPI_INIT_SEQ_DATA_D		0x446
#define DC_DISP_DC_MCCIF_FIFOCTRL		0x480
#define DC_DISP_MCCIF_DISPLAY0A_HYST		0x481
#define DC_DISP_MCCIF_DISPLAY0B_HYST		0x482
#define DC_DISP_MCCIF_DISPLAY0C_HYST		0x483
#define DC_DISP_MCCIF_DISPLAY1B_HYST		0x484
#define DC_DISP_DAC_CRT_CTRL			0x4c0
#define DC_DISP_DISP_MISC_CONTROL		0x4c1

#define DC_WINC_COLOR_PALETTE(x)		(0x500 + (x))

#define DC_WINC_PALETTE_COLOR_EXT		0x600
#define DC_WINC_H_FILTER_P(x)			(0x601 + (x))
#define DC_WINC_CSC_YOF				0x611
#define DC_WINC_CSC_KYRGB			0x612
#define DC_WINC_CSC_KUR				0x613
#define DC_WINC_CSC_KVR				0x614
#define DC_WINC_CSC_KUG				0x615
#define DC_WINC_CSC_KVG				0x616
#define DC_WINC_CSC_KUB				0x617
#define DC_WINC_CSC_KVB				0x618
#define DC_WINC_V_FILTER_P(x)			(0x619 + (x))
#define DC_WIN_WIN_OPTIONS			0x700
#define  H_DIRECTION_INCREMENT		(0 << 0)
#define  H_DIRECTION_DECREMENTT		(1 << 0)
#define  V_DIRECTION_INCREMENT		(0 << 2)
#define  V_DIRECTION_DECREMENTT		(1 << 2)
#define  COLOR_EXPAND			(1 << 6)
#define  CP_ENABLE			(1 << 16)
#define  DV_ENABLE			(1 << 20)
#define  WIN_ENABLE			(1 << 30)

#define DC_WIN_BYTE_SWAP			0x701
#define  BYTE_SWAP_NOSWAP		0
#define  BYTE_SWAP_SWAP2		1
#define  BYTE_SWAP_SWAP4		2
#define  BYTE_SWAP_SWAP4HW		3

#define DC_WIN_BUFFER_CONTROL			0x702
#define  BUFFER_CONTROL_HOST		0
#define  BUFFER_CONTROL_VI		1
#define  BUFFER_CONTROL_EPP		2
#define  BUFFER_CONTROL_MPEGE		3
#define  BUFFER_CONTROL_SB2D		4

#define DC_WIN_COLOR_DEPTH			0x703

#define DC_WIN_POSITION				0x704
#define  H_POSITION(x)		(((x) & 0xfff) << 0)
#define  V_POSITION(x)		(((x) & 0xfff) << 16)

#define DC_WIN_SIZE				0x705
#define  H_SIZE(x)		(((x) & 0xfff) << 0)
#define  V_SIZE(x)		(((x) & 0xfff) << 16)

#define DC_WIN_PRESCALED_SIZE			0x706
#define  H_PRESCALED_SIZE(x)	(((x) & 0x3fff) << 0)
#define  V_PRESCALED_SIZE(x)	(((x) & 0xfff) << 16)

#define DC_WIN_H_INITIAL_DDA			0x707
#define DC_WIN_V_INITIAL_DDA			0x708
#define DC_WIN_DDA_INCREMENT			0x709
#define  H_DDA_INC(x)		(((x) & 0xffff) << 0)
#define  V_DDA_INC(x)		(((x) & 0xffff) << 16)

#define DC_WIN_LINE_STRIDE			0x70a
#define DC_WIN_BUF_STRIDE			0x70b
#define DC_WIN_UV_BUF_STRIDE			0x70c
#define DC_WIN_BUFFER_ADDR_MODE			0x70d
#define DC_WIN_DV_CONTROL			0x70e
#define DC_WIN_BLEND_NOKEY			0x70f
#define DC_WIN_BLEND_1WIN			0x710
#define DC_WIN_BLEND_2WIN_X			0x711
#define DC_WIN_BLEND_2WIN_Y			0x712
#define DC_WIN_BLEND_3WIN_XY			0x713
#define  CKEY_NOKEY			(0 << 0)
#define  CKEY_KEY0			(1 << 0)
#define  CKEY_KEY1			(2 << 0)
#define  CKEY_KEY01			(3 << 0)
#define  BLEND_CONTROL_FIX		(0 << 2)
#define  BLEND_CONTROL_ALPHA		(1 << 2)
#define  BLEND_CONTROL_DEPENDANT	(2 << 2)
#define  BLEND_CONTROL_PREMULT		(3 << 2)
#define  BLEND_WEIGHT0(x)		(((x) & 0xff) << 8)
#define  BLEND_WEIGHT1(x)		(((x) & 0xff) << 16)
#define  BLEND(key, control, weight0, weight1)			\
	  (CKEY_ ## key | BLEND_CONTROL_ ## control |		\
	   BLEND_WEIGHT0(weight0) | BLEND_WEIGHT0(weight1))


#define DC_WIN_HP_FETCH_CONTROL			0x714
#define DC_WINBUF_START_ADDR			0x800
#define DC_WINBUF_START_ADDR_NS			0x801
#define DC_WINBUF_START_ADDR_U			0x802
#define DC_WINBUF_START_ADDR_U_NS		0x803
#define DC_WINBUF_START_ADDR_V			0x804
#define DC_WINBUF_START_ADDR_V_NS		0x805
#define DC_WINBUF_ADDR_H_OFFSET			0x806
#define DC_WINBUF_ADDR_H_OFFSET_NS		0x807
#define DC_WINBUF_ADDR_V_OFFSET			0x808
#define DC_WINBUF_ADDR_V_OFFSET_NS		0x809
#define DC_WINBUF_UFLOW_STATUS			0x80a

#endif
