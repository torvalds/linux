/*
 * Header file required by picodlp panel driver
 *
 * Copyright (C) 2009-2011 Texas Instruments
 * Author: Mythri P K <mythripk@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef __OMAP2_DISPLAY_PANEL_PICODLP_H
#define __OMAP2_DISPLAY_PANEL_PICODLP_H

/* Commands used for configuring picodlp panel */

#define MAIN_STATUS			0x03
#define PBC_CONTROL			0x08
#define INPUT_SOURCE			0x0B
#define INPUT_RESOLUTION		0x0C
#define DATA_FORMAT			0x0D
#define IMG_ROTATION			0x0E
#define LONG_FLIP			0x0F
#define SHORT_FLIP			0x10
#define TEST_PAT_SELECT			0x11
#define R_DRIVE_CURRENT			0x12
#define G_DRIVE_CURRENT			0x13
#define B_DRIVE_CURRENT			0x14
#define READ_REG_SELECT			0x15
#define RGB_DRIVER_ENABLE		0x16

#define CPU_IF_MODE			0x18
#define FRAME_RATE			0x19
#define CPU_IF_SYNC_METHOD		0x1A
#define CPU_IF_SOF			0x1B
#define CPU_IF_EOF			0x1C
#define CPU_IF_SLEEP			0x1D

#define SEQUENCE_MODE			0x1E
#define SOFT_RESET			0x1F
#define FRONT_END_RESET			0x21
#define AUTO_PWR_ENABLE			0x22

#define VSYNC_LINE_DELAY		0x23
#define CPU_PI_HORIZ_START		0x24
#define CPU_PI_VERT_START		0x25
#define CPU_PI_HORIZ_WIDTH		0x26
#define CPU_PI_VERT_HEIGHT		0x27

#define PIXEL_MASK_CROP			0x28
#define CROP_FIRST_LINE			0x29
#define CROP_LAST_LINE			0x2A
#define CROP_FIRST_PIXEL		0x2B
#define CROP_LAST_PIXEL			0x2C
#define DMD_PARK_TRIGGER		0x2D

#define MISC_REG			0x30

/* AGC registers */
#define AGC_CTRL			0x50
#define AGC_CLIPPED_PIXS		0x55
#define AGC_BRIGHT_PIXS			0x56
#define AGC_BG_PIXS			0x57
#define AGC_SAFETY_MARGIN		0x17

/* Color Coordinate Adjustment registers */
#define CCA_ENABLE		0x5E
#define CCA_C1A			0x5F
#define CCA_C1B			0x60
#define CCA_C1C			0x61
#define CCA_C2A			0x62
#define CCA_C2B			0x63
#define CCA_C2C			0x64
#define CCA_C3A			0x65
#define CCA_C3B			0x66
#define CCA_C3C			0x67
#define CCA_C7A			0x71
#define CCA_C7B			0x72
#define CCA_C7C			0x73

/**
 * DLP Pico Processor 2600 comes with flash
 * We can do DMA operations from flash for accessing Look Up Tables
 */
#define DMA_STATUS			0x100
#define FLASH_ADDR_BYTES		0x74
#define FLASH_DUMMY_BYTES		0x75
#define FLASH_WRITE_BYTES		0x76
#define FLASH_READ_BYTES		0x77
#define FLASH_OPCODE			0x78
#define FLASH_START_ADDR		0x79
#define FLASH_DUMMY2			0x7A
#define FLASH_WRITE_DATA		0x7B

#define TEMPORAL_DITH_DISABLE		0x7E
#define SEQ_CONTROL			0x82
#define SEQ_VECTOR			0x83

/* DMD is Digital Micromirror Device */
#define DMD_BLOCK_COUNT			0x84
#define DMD_VCC_CONTROL			0x86
#define DMD_PARK_PULSE_COUNT		0x87
#define DMD_PARK_PULSE_WIDTH		0x88
#define DMD_PARK_DELAY			0x89
#define DMD_SHADOW_ENABLE		0x8E
#define SEQ_STATUS			0x8F
#define FLASH_CLOCK_CONTROL		0x98
#define DMD_PARK			0x2D

#define SDRAM_BIST_ENABLE		0x46
#define DDR_DRIVER_STRENGTH		0x9A
#define SDC_ENABLE			0x9D
#define SDC_BUFF_SWAP_DISABLE		0xA3
#define CURTAIN_CONTROL			0xA6
#define DDR_BUS_SWAP_ENABLE		0xA7
#define DMD_TRC_ENABLE			0xA8
#define DMD_BUS_SWAP_ENABLE		0xA9

#define ACTGEN_ENABLE			0xAE
#define ACTGEN_CONTROL			0xAF
#define ACTGEN_HORIZ_BP			0xB0
#define ACTGEN_VERT_BP			0xB1

/* Look Up Table access */
#define CMT_SPLASH_LUT_START_ADDR	0xFA
#define CMT_SPLASH_LUT_DEST_SELECT	0xFB
#define CMT_SPLASH_LUT_DATA		0xFC
#define SEQ_RESET_LUT_START_ADDR	0xFD
#define SEQ_RESET_LUT_DEST_SELECT	0xFE
#define SEQ_RESET_LUT_DATA		0xFF

/* Input source definitions */
#define PARALLEL_RGB		0
#define INT_TEST_PATTERN	1
#define SPLASH_SCREEN		2
#define CPU_INTF		3
#define BT656			4

/* Standard input resolution definitions */
#define QWVGA_LANDSCAPE		3	/* (427h*240v) */
#define WVGA_864_LANDSCAPE	21	/* (864h*480v) */
#define WVGA_DMD_OPTICAL_TEST	35	/* (608h*684v) */

/* Standard data format definitions */
#define RGB565			0
#define RGB666			1
#define RGB888			2

/* Test Pattern definitions */
#define TPG_CHECKERBOARD	0
#define TPG_BLACK		1
#define TPG_WHITE		2
#define TPG_RED			3
#define TPG_BLUE		4
#define TPG_GREEN		5
#define TPG_VLINES_BLACK	6
#define TPG_HLINES_BLACK	7
#define TPG_VLINES_ALT		8
#define TPG_HLINES_ALT		9
#define TPG_DIAG_LINES		10
#define TPG_GREYRAMP_VERT	11
#define TPG_GREYRAMP_HORIZ	12
#define TPG_ANSI_CHECKERBOARD	13

/* sequence mode definitions */
#define SEQ_FREE_RUN		0
#define SEQ_LOCK		1

/* curtain color definitions */
#define CURTAIN_BLACK		0
#define CURTAIN_RED		1
#define CURTAIN_GREEN		2
#define CURTAIN_BLUE		3
#define CURTAIN_YELLOW		4
#define CURTAIN_MAGENTA		5
#define CURTAIN_CYAN		6
#define CURTAIN_WHITE		7

/* LUT definitions */
#define CMT_LUT_NONE		0
#define CMT_LUT_GREEN		1
#define CMT_LUT_RED		2
#define CMT_LUT_BLUE		3
#define CMT_LUT_ALL		4
#define SPLASH_LUT		5

#define SEQ_LUT_NONE		0
#define SEQ_DRC_LUT_0		1
#define SEQ_DRC_LUT_1		2
#define SEQ_DRC_LUT_2		3
#define SEQ_DRC_LUT_3		4
#define SEQ_SEQ_LUT		5
#define SEQ_DRC_LUT_ALL		6
#define WPC_PROGRAM_LUT		7

#define BITSTREAM_START_ADDR		0x00000000
#define BITSTREAM_SIZE			0x00040000

#define WPC_FW_0_START_ADDR		0x00040000
#define WPC_FW_0_SIZE			0x00000ce8

#define SEQUENCE_0_START_ADDR		0x00044000
#define SEQUENCE_0_SIZE			0x00001000

#define SEQUENCE_1_START_ADDR		0x00045000
#define SEQUENCE_1_SIZE			0x00000d10

#define SEQUENCE_2_START_ADDR		0x00046000
#define SEQUENCE_2_SIZE			0x00000d10

#define SEQUENCE_3_START_ADDR		0x00047000
#define SEQUENCE_3_SIZE			0x00000d10

#define SEQUENCE_4_START_ADDR		0x00048000
#define SEQUENCE_4_SIZE			0x00000d10

#define SEQUENCE_5_START_ADDR		0x00049000
#define SEQUENCE_5_SIZE			0x00000d10

#define SEQUENCE_6_START_ADDR		0x0004a000
#define SEQUENCE_6_SIZE			0x00000d10

#define CMT_LUT_0_START_ADDR		0x0004b200
#define CMT_LUT_0_SIZE			0x00000600

#define CMT_LUT_1_START_ADDR		0x0004b800
#define CMT_LUT_1_SIZE			0x00000600

#define CMT_LUT_2_START_ADDR		0x0004be00
#define CMT_LUT_2_SIZE			0x00000600

#define CMT_LUT_3_START_ADDR		0x0004c400
#define CMT_LUT_3_SIZE			0x00000600

#define CMT_LUT_4_START_ADDR		0x0004ca00
#define CMT_LUT_4_SIZE			0x00000600

#define CMT_LUT_5_START_ADDR		0x0004d000
#define CMT_LUT_5_SIZE			0x00000600

#define CMT_LUT_6_START_ADDR		0x0004d600
#define CMT_LUT_6_SIZE			0x00000600

#define DRC_TABLE_0_START_ADDR		0x0004dc00
#define DRC_TABLE_0_SIZE		0x00000100

#define SPLASH_0_START_ADDR		0x0004dd00
#define SPLASH_0_SIZE			0x00032280

#define SEQUENCE_7_START_ADDR		0x00080000
#define SEQUENCE_7_SIZE			0x00000d10

#define SEQUENCE_8_START_ADDR		0x00081800
#define SEQUENCE_8_SIZE			0x00000d10

#define SEQUENCE_9_START_ADDR		0x00083000
#define SEQUENCE_9_SIZE			0x00000d10

#define CMT_LUT_7_START_ADDR		0x0008e000
#define CMT_LUT_7_SIZE			0x00000600

#define CMT_LUT_8_START_ADDR		0x0008e800
#define CMT_LUT_8_SIZE			0x00000600

#define CMT_LUT_9_START_ADDR		0x0008f000
#define CMT_LUT_9_SIZE			0x00000600

#define SPLASH_1_START_ADDR		0x0009a000
#define SPLASH_1_SIZE			0x00032280

#define SPLASH_2_START_ADDR		0x000cd000
#define SPLASH_2_SIZE			0x00032280

#define SPLASH_3_START_ADDR		0x00100000
#define SPLASH_3_SIZE			0x00032280

#define OPT_SPLASH_0_START_ADDR		0x00134000
#define OPT_SPLASH_0_SIZE		0x000cb100

#endif
