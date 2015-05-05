/*
 * Copyright (C) 2012 Avionic Design GmbH
 * Copyright (C) 2012 NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef TEGRA_DC_H
#define TEGRA_DC_H 1

#define DC_CMD_GENERAL_INCR_SYNCPT		0x000
#define DC_CMD_GENERAL_INCR_SYNCPT_CNTRL	0x001
#define  SYNCPT_CNTRL_NO_STALL   (1 << 8)
#define  SYNCPT_CNTRL_SOFT_RESET (1 << 0)
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
#define  SYNCPT_VSYNC_ENABLE (1 << 8)
#define DC_CMD_DISPLAY_COMMAND_OPTION0		0x031
#define DC_CMD_DISPLAY_COMMAND			0x032
#define DISP_CTRL_MODE_STOP (0 << 5)
#define DISP_CTRL_MODE_C_DISPLAY (1 << 5)
#define DISP_CTRL_MODE_NC_DISPLAY (2 << 5)
#define DISP_CTRL_MODE_MASK (3 << 5)
#define DC_CMD_SIGNAL_RAISE			0x033
#define DC_CMD_DISPLAY_POWER_CONTROL		0x036
#define PW0_ENABLE (1 <<  0)
#define PW1_ENABLE (1 <<  2)
#define PW2_ENABLE (1 <<  4)
#define PW3_ENABLE (1 <<  6)
#define PW4_ENABLE (1 <<  8)
#define PM0_ENABLE (1 << 16)
#define PM1_ENABLE (1 << 18)

#define DC_CMD_INT_STATUS			0x037
#define DC_CMD_INT_MASK				0x038
#define DC_CMD_INT_ENABLE			0x039
#define DC_CMD_INT_TYPE				0x03a
#define DC_CMD_INT_POLARITY			0x03b
#define CTXSW_INT     (1 << 0)
#define FRAME_END_INT (1 << 1)
#define VBLANK_INT    (1 << 2)
#define WIN_A_UF_INT  (1 << 8)
#define WIN_B_UF_INT  (1 << 9)
#define WIN_C_UF_INT  (1 << 10)
#define WIN_A_OF_INT  (1 << 14)
#define WIN_B_OF_INT  (1 << 15)
#define WIN_C_OF_INT  (1 << 16)

#define DC_CMD_SIGNAL_RAISE1			0x03c
#define DC_CMD_SIGNAL_RAISE2			0x03d
#define DC_CMD_SIGNAL_RAISE3			0x03e

#define DC_CMD_STATE_ACCESS			0x040
#define READ_MUX  (1 << 0)
#define WRITE_MUX (1 << 2)

#define DC_CMD_STATE_CONTROL			0x041
#define GENERAL_ACT_REQ (1 <<  0)
#define WIN_A_ACT_REQ   (1 <<  1)
#define WIN_B_ACT_REQ   (1 <<  2)
#define WIN_C_ACT_REQ   (1 <<  3)
#define CURSOR_ACT_REQ  (1 <<  7)
#define GENERAL_UPDATE  (1 <<  8)
#define WIN_A_UPDATE    (1 <<  9)
#define WIN_B_UPDATE    (1 << 10)
#define WIN_C_UPDATE    (1 << 11)
#define CURSOR_UPDATE   (1 << 15)
#define NC_HOST_TRIG    (1 << 24)

#define DC_CMD_DISPLAY_WINDOW_HEADER		0x042
#define WINDOW_A_SELECT (1 << 4)
#define WINDOW_B_SELECT (1 << 5)
#define WINDOW_C_SELECT (1 << 6)

#define DC_CMD_REG_ACT_CONTROL			0x043

#define DC_COM_CRC_CONTROL			0x300
#define DC_COM_CRC_CHECKSUM			0x301
#define DC_COM_PIN_OUTPUT_ENABLE(x) (0x302 + (x))
#define DC_COM_PIN_OUTPUT_POLARITY(x) (0x306 + (x))
#define LVS_OUTPUT_POLARITY_LOW (1 << 28)
#define LHS_OUTPUT_POLARITY_LOW (1 << 30)
#define DC_COM_PIN_OUTPUT_DATA(x) (0x30a + (x))
#define DC_COM_PIN_INPUT_ENABLE(x) (0x30e + (x))
#define DC_COM_PIN_INPUT_DATA(x) (0x312 + (x))
#define DC_COM_PIN_OUTPUT_SELECT(x) (0x314 + (x))

#define DC_COM_PIN_MISC_CONTROL			0x31b
#define DC_COM_PIN_PM0_CONTROL			0x31c
#define DC_COM_PIN_PM0_DUTY_CYCLE		0x31d
#define DC_COM_PIN_PM1_CONTROL			0x31e
#define DC_COM_PIN_PM1_DUTY_CYCLE		0x31f

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
#define H_PULSE_0_ENABLE (1 <<  8)
#define H_PULSE_1_ENABLE (1 << 10)
#define H_PULSE_2_ENABLE (1 << 12)

#define DC_DISP_DISP_SIGNAL_OPTIONS1		0x401

#define DC_DISP_DISP_WIN_OPTIONS		0x402
#define HDMI_ENABLE	(1 << 30)
#define DSI_ENABLE	(1 << 29)
#define SOR_ENABLE	(1 << 25)
#define CURSOR_ENABLE	(1 << 16)

#define DC_DISP_DISP_MEM_HIGH_PRIORITY		0x403
#define CURSOR_THRESHOLD(x)   (((x) & 0x03) << 24)
#define WINDOW_A_THRESHOLD(x) (((x) & 0x7f) << 16)
#define WINDOW_B_THRESHOLD(x) (((x) & 0x7f) <<  8)
#define WINDOW_C_THRESHOLD(x) (((x) & 0xff) <<  0)

#define DC_DISP_DISP_MEM_HIGH_PRIORITY_TIMER	0x404
#define CURSOR_DELAY(x)   (((x) & 0x3f) << 24)
#define WINDOW_A_DELAY(x) (((x) & 0x3f) << 16)
#define WINDOW_B_DELAY(x) (((x) & 0x3f) <<  8)
#define WINDOW_C_DELAY(x) (((x) & 0x3f) <<  0)

#define DC_DISP_DISP_TIMING_OPTIONS		0x405
#define VSYNC_H_POSITION(x) ((x) & 0xfff)

#define DC_DISP_REF_TO_SYNC			0x406
#define DC_DISP_SYNC_WIDTH			0x407
#define DC_DISP_BACK_PORCH			0x408
#define DC_DISP_ACTIVE				0x409
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

#define PULSE_MODE_NORMAL    (0 << 3)
#define PULSE_MODE_ONE_CLOCK (1 << 3)
#define PULSE_POLARITY_HIGH  (0 << 4)
#define PULSE_POLARITY_LOW   (1 << 4)
#define PULSE_QUAL_ALWAYS    (0 << 6)
#define PULSE_QUAL_VACTIVE   (2 << 6)
#define PULSE_QUAL_VACTIVE1  (3 << 6)
#define PULSE_LAST_START_A   (0 << 8)
#define PULSE_LAST_END_A     (1 << 8)
#define PULSE_LAST_START_B   (2 << 8)
#define PULSE_LAST_END_B     (3 << 8)
#define PULSE_LAST_START_C   (4 << 8)
#define PULSE_LAST_END_C     (5 << 8)
#define PULSE_LAST_START_D   (6 << 8)
#define PULSE_LAST_END_D     (7 << 8)

#define PULSE_START(x) (((x) & 0xfff) <<  0)
#define PULSE_END(x)   (((x) & 0xfff) << 16)

#define DC_DISP_DISP_CLOCK_CONTROL		0x42e
#define PIXEL_CLK_DIVIDER_PCD1  (0 << 8)
#define PIXEL_CLK_DIVIDER_PCD1H (1 << 8)
#define PIXEL_CLK_DIVIDER_PCD2  (2 << 8)
#define PIXEL_CLK_DIVIDER_PCD3  (3 << 8)
#define PIXEL_CLK_DIVIDER_PCD4  (4 << 8)
#define PIXEL_CLK_DIVIDER_PCD6  (5 << 8)
#define PIXEL_CLK_DIVIDER_PCD8  (6 << 8)
#define PIXEL_CLK_DIVIDER_PCD9  (7 << 8)
#define PIXEL_CLK_DIVIDER_PCD12 (8 << 8)
#define PIXEL_CLK_DIVIDER_PCD16 (9 << 8)
#define PIXEL_CLK_DIVIDER_PCD18 (10 << 8)
#define PIXEL_CLK_DIVIDER_PCD24 (11 << 8)
#define PIXEL_CLK_DIVIDER_PCD13 (12 << 8)
#define SHIFT_CLK_DIVIDER(x)    ((x) & 0xff)

#define DC_DISP_DISP_INTERFACE_CONTROL		0x42f
#define DISP_DATA_FORMAT_DF1P1C    (0 << 0)
#define DISP_DATA_FORMAT_DF1P2C24B (1 << 0)
#define DISP_DATA_FORMAT_DF1P2C18B (2 << 0)
#define DISP_DATA_FORMAT_DF1P2C16B (3 << 0)
#define DISP_DATA_FORMAT_DF2S      (4 << 0)
#define DISP_DATA_FORMAT_DF3S      (5 << 0)
#define DISP_DATA_FORMAT_DFSPI     (6 << 0)
#define DISP_DATA_FORMAT_DF1P3C24B (7 << 0)
#define DISP_DATA_FORMAT_DF1P3C18B (8 << 0)
#define DISP_ALIGNMENT_MSB         (0 << 8)
#define DISP_ALIGNMENT_LSB         (1 << 8)
#define DISP_ORDER_RED_BLUE        (0 << 9)
#define DISP_ORDER_BLUE_RED        (1 << 9)

#define DC_DISP_DISP_COLOR_CONTROL		0x430
#define BASE_COLOR_SIZE666     (0 << 0)
#define BASE_COLOR_SIZE111     (1 << 0)
#define BASE_COLOR_SIZE222     (2 << 0)
#define BASE_COLOR_SIZE333     (3 << 0)
#define BASE_COLOR_SIZE444     (4 << 0)
#define BASE_COLOR_SIZE555     (5 << 0)
#define BASE_COLOR_SIZE565     (6 << 0)
#define BASE_COLOR_SIZE332     (7 << 0)
#define BASE_COLOR_SIZE888     (8 << 0)
#define DITHER_CONTROL_DISABLE (0 << 8)
#define DITHER_CONTROL_ORDERED (2 << 8)
#define DITHER_CONTROL_ERRDIFF (3 << 8)

#define DC_DISP_SHIFT_CLOCK_OPTIONS		0x431
#define  SC1_H_QUALIFIER_NONE	(1 << 16)
#define  SC0_H_QUALIFIER_NONE	(1 <<  0)

#define DC_DISP_DATA_ENABLE_OPTIONS		0x432
#define DE_SELECT_ACTIVE_BLANK  (0 << 0)
#define DE_SELECT_ACTIVE        (1 << 0)
#define DE_SELECT_ACTIVE_IS     (2 << 0)
#define DE_CONTROL_ONECLK       (0 << 2)
#define DE_CONTROL_NORMAL       (1 << 2)
#define DE_CONTROL_EARLY_EXT    (2 << 2)
#define DE_CONTROL_EARLY        (3 << 2)
#define DE_CONTROL_ACTIVE_BLANK (4 << 2)

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
#define CURSOR_CLIP_DISPLAY	(0 << 28)
#define CURSOR_CLIP_WIN_A	(1 << 28)
#define CURSOR_CLIP_WIN_B	(2 << 28)
#define CURSOR_CLIP_WIN_C	(3 << 28)
#define CURSOR_SIZE_32x32	(0 << 24)
#define CURSOR_SIZE_64x64	(1 << 24)
#define CURSOR_SIZE_128x128	(2 << 24)
#define CURSOR_SIZE_256x256	(3 << 24)
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
#define DC_DISP_MCCIF_DISPLAY1A_HYST		0x483
#define DC_DISP_MCCIF_DISPLAY1B_HYST		0x484

#define DC_DISP_DAC_CRT_CTRL			0x4c0
#define DC_DISP_DISP_MISC_CONTROL		0x4c1
#define DC_DISP_SD_CONTROL			0x4c2
#define DC_DISP_SD_CSC_COEFF			0x4c3
#define DC_DISP_SD_LUT(x)			(0x4c4 + (x))
#define DC_DISP_SD_FLICKER_CONTROL		0x4cd
#define DC_DISP_DC_PIXEL_COUNT			0x4ce
#define DC_DISP_SD_HISTOGRAM(x)			(0x4cf + (x))
#define DC_DISP_SD_BL_PARAMETERS		0x4d7
#define DC_DISP_SD_BL_TF(x)			(0x4d8 + (x))
#define DC_DISP_SD_BL_CONTROL			0x4dc
#define DC_DISP_SD_HW_K_VALUES			0x4dd
#define DC_DISP_SD_MAN_K_VALUES			0x4de

#define DC_DISP_INTERLACE_CONTROL		0x4e5
#define  INTERLACE_STATUS (1 << 2)
#define  INTERLACE_START  (1 << 1)
#define  INTERLACE_ENABLE (1 << 0)

#define DC_DISP_CURSOR_START_ADDR_HI		0x4ec
#define DC_DISP_BLEND_CURSOR_CONTROL		0x4f1
#define CURSOR_MODE_LEGACY			(0 << 24)
#define CURSOR_MODE_NORMAL			(1 << 24)
#define CURSOR_DST_BLEND_ZERO			(0 << 16)
#define CURSOR_DST_BLEND_K1			(1 << 16)
#define CURSOR_DST_BLEND_NEG_K1_TIMES_SRC	(2 << 16)
#define CURSOR_DST_BLEND_MASK			(3 << 16)
#define CURSOR_SRC_BLEND_K1			(0 << 8)
#define CURSOR_SRC_BLEND_K1_TIMES_SRC		(1 << 8)
#define CURSOR_SRC_BLEND_MASK			(3 << 8)
#define CURSOR_ALPHA				0xff

#define DC_WIN_CSC_YOF				0x611
#define DC_WIN_CSC_KYRGB			0x612
#define DC_WIN_CSC_KUR				0x613
#define DC_WIN_CSC_KVR				0x614
#define DC_WIN_CSC_KUG				0x615
#define DC_WIN_CSC_KVG				0x616
#define DC_WIN_CSC_KUB				0x617
#define DC_WIN_CSC_KVB				0x618

#define DC_WIN_WIN_OPTIONS			0x700
#define H_DIRECTION  (1 <<  0)
#define V_DIRECTION  (1 <<  2)
#define COLOR_EXPAND (1 <<  6)
#define CSC_ENABLE   (1 << 18)
#define WIN_ENABLE   (1 << 30)

#define DC_WIN_BYTE_SWAP			0x701
#define BYTE_SWAP_NOSWAP  (0 << 0)
#define BYTE_SWAP_SWAP2   (1 << 0)
#define BYTE_SWAP_SWAP4   (2 << 0)
#define BYTE_SWAP_SWAP4HW (3 << 0)

#define DC_WIN_BUFFER_CONTROL			0x702
#define BUFFER_CONTROL_HOST  (0 << 0)
#define BUFFER_CONTROL_VI    (1 << 0)
#define BUFFER_CONTROL_EPP   (2 << 0)
#define BUFFER_CONTROL_MPEGE (3 << 0)
#define BUFFER_CONTROL_SB2D  (4 << 0)

#define DC_WIN_COLOR_DEPTH			0x703
#define WIN_COLOR_DEPTH_P1              0
#define WIN_COLOR_DEPTH_P2              1
#define WIN_COLOR_DEPTH_P4              2
#define WIN_COLOR_DEPTH_P8              3
#define WIN_COLOR_DEPTH_B4G4R4A4        4
#define WIN_COLOR_DEPTH_B5G5R5A         5
#define WIN_COLOR_DEPTH_B5G6R5          6
#define WIN_COLOR_DEPTH_AB5G5R5         7
#define WIN_COLOR_DEPTH_B8G8R8A8       12
#define WIN_COLOR_DEPTH_R8G8B8A8       13
#define WIN_COLOR_DEPTH_B6x2G6x2R6x2A8 14
#define WIN_COLOR_DEPTH_R6x2G6x2B6x2A8 15
#define WIN_COLOR_DEPTH_YCbCr422       16
#define WIN_COLOR_DEPTH_YUV422         17
#define WIN_COLOR_DEPTH_YCbCr420P      18
#define WIN_COLOR_DEPTH_YUV420P        19
#define WIN_COLOR_DEPTH_YCbCr422P      20
#define WIN_COLOR_DEPTH_YUV422P        21
#define WIN_COLOR_DEPTH_YCbCr422R      22
#define WIN_COLOR_DEPTH_YUV422R        23
#define WIN_COLOR_DEPTH_YCbCr422RA     24
#define WIN_COLOR_DEPTH_YUV422RA       25

#define DC_WIN_POSITION				0x704
#define H_POSITION(x) (((x) & 0x1fff) <<  0)
#define V_POSITION(x) (((x) & 0x1fff) << 16)

#define DC_WIN_SIZE				0x705
#define H_SIZE(x) (((x) & 0x1fff) <<  0)
#define V_SIZE(x) (((x) & 0x1fff) << 16)

#define DC_WIN_PRESCALED_SIZE			0x706
#define H_PRESCALED_SIZE(x) (((x) & 0x7fff) <<  0)
#define V_PRESCALED_SIZE(x) (((x) & 0x1fff) << 16)

#define DC_WIN_H_INITIAL_DDA			0x707
#define DC_WIN_V_INITIAL_DDA			0x708
#define DC_WIN_DDA_INC				0x709
#define H_DDA_INC(x) (((x) & 0xffff) <<  0)
#define V_DDA_INC(x) (((x) & 0xffff) << 16)

#define DC_WIN_LINE_STRIDE			0x70a
#define DC_WIN_BUF_STRIDE			0x70b
#define DC_WIN_UV_BUF_STRIDE			0x70c
#define DC_WIN_BUFFER_ADDR_MODE			0x70d
#define DC_WIN_BUFFER_ADDR_MODE_LINEAR		(0 <<  0)
#define DC_WIN_BUFFER_ADDR_MODE_TILE		(1 <<  0)
#define DC_WIN_BUFFER_ADDR_MODE_LINEAR_UV	(0 << 16)
#define DC_WIN_BUFFER_ADDR_MODE_TILE_UV		(1 << 16)
#define DC_WIN_DV_CONTROL			0x70e

#define DC_WIN_BLEND_NOKEY			0x70f
#define DC_WIN_BLEND_1WIN			0x710
#define DC_WIN_BLEND_2WIN_X			0x711
#define DC_WIN_BLEND_2WIN_Y			0x712
#define DC_WIN_BLEND_3WIN_XY			0x713

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
#define DC_WINBUF_SURFACE_KIND			0x80b
#define DC_WINBUF_SURFACE_KIND_PITCH	(0 << 0)
#define DC_WINBUF_SURFACE_KIND_TILED	(1 << 0)
#define DC_WINBUF_SURFACE_KIND_BLOCK	(2 << 0)
#define DC_WINBUF_SURFACE_KIND_BLOCK_HEIGHT(x) (((x) & 0x7) << 4)

#define DC_WINBUF_AD_UFLOW_STATUS		0xbca
#define DC_WINBUF_BD_UFLOW_STATUS		0xdca
#define DC_WINBUF_CD_UFLOW_STATUS		0xfca

#endif /* TEGRA_DC_H */
