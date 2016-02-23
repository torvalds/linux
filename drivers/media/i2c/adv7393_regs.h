/*
 * ADV7393 encoder related structure and register definitions
 *
 * Copyright (C) 2010-2012 ADVANSEE - http://www.advansee.com/
 * Benoît Thébaudeau <benoit.thebaudeau@advansee.com>
 *
 * Based on ADV7343 driver,
 *
 * Copyright (C) 2009 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed .as is. WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef ADV7393_REGS_H
#define ADV7393_REGS_H

struct adv7393_std_info {
	u32 standard_val3;
	u32 fsc_val;
	v4l2_std_id stdid;
};

/* Register offset macros */
#define ADV7393_POWER_MODE_REG		(0x00)
#define ADV7393_MODE_SELECT_REG		(0x01)
#define ADV7393_MODE_REG0		(0x02)

#define ADV7393_DAC123_OUTPUT_LEVEL	(0x0B)

#define ADV7393_SOFT_RESET		(0x17)

#define ADV7393_HD_MODE_REG1		(0x30)
#define ADV7393_HD_MODE_REG2		(0x31)
#define ADV7393_HD_MODE_REG3		(0x32)
#define ADV7393_HD_MODE_REG4		(0x33)
#define ADV7393_HD_MODE_REG5		(0x34)
#define ADV7393_HD_MODE_REG6		(0x35)

#define ADV7393_HD_MODE_REG7		(0x39)

#define ADV7393_SD_MODE_REG1		(0x80)
#define ADV7393_SD_MODE_REG2		(0x82)
#define ADV7393_SD_MODE_REG3		(0x83)
#define ADV7393_SD_MODE_REG4		(0x84)
#define ADV7393_SD_MODE_REG5		(0x86)
#define ADV7393_SD_MODE_REG6		(0x87)
#define ADV7393_SD_MODE_REG7		(0x88)
#define ADV7393_SD_MODE_REG8		(0x89)

#define ADV7393_SD_TIMING_REG0		(0x8A)

#define ADV7393_FSC_REG0		(0x8C)
#define ADV7393_FSC_REG1		(0x8D)
#define ADV7393_FSC_REG2		(0x8E)
#define ADV7393_FSC_REG3		(0x8F)

#define ADV7393_SD_CGMS_WSS0		(0x99)

#define ADV7393_SD_HUE_ADJUST		(0xA0)
#define ADV7393_SD_BRIGHTNESS_WSS	(0xA1)

/* Default values for the registers */
#define ADV7393_POWER_MODE_REG_DEFAULT		(0x10)
#define ADV7393_HD_MODE_REG1_DEFAULT		(0x3C)	/* Changed Default
							   720p EAV/SAV code*/
#define ADV7393_HD_MODE_REG2_DEFAULT		(0x01)	/* Changed Pixel data
							   valid */
#define ADV7393_HD_MODE_REG3_DEFAULT		(0x00)	/* Color delay 0 clks */
#define ADV7393_HD_MODE_REG4_DEFAULT		(0xEC)	/* Changed */
#define ADV7393_HD_MODE_REG5_DEFAULT		(0x08)
#define ADV7393_HD_MODE_REG6_DEFAULT		(0x00)
#define ADV7393_HD_MODE_REG7_DEFAULT		(0x00)
#define ADV7393_SOFT_RESET_DEFAULT		(0x02)
#define ADV7393_COMPOSITE_POWER_VALUE		(0x10)
#define ADV7393_COMPONENT_POWER_VALUE		(0x1C)
#define ADV7393_SVIDEO_POWER_VALUE		(0x0C)
#define ADV7393_SD_HUE_ADJUST_DEFAULT		(0x80)
#define ADV7393_SD_BRIGHTNESS_WSS_DEFAULT	(0x00)

#define ADV7393_SD_CGMS_WSS0_DEFAULT		(0x10)

#define ADV7393_SD_MODE_REG1_DEFAULT		(0x10)
#define ADV7393_SD_MODE_REG2_DEFAULT		(0xC9)
#define ADV7393_SD_MODE_REG3_DEFAULT		(0x00)
#define ADV7393_SD_MODE_REG4_DEFAULT		(0x00)
#define ADV7393_SD_MODE_REG5_DEFAULT		(0x02)
#define ADV7393_SD_MODE_REG6_DEFAULT		(0x8C)
#define ADV7393_SD_MODE_REG7_DEFAULT		(0x14)
#define ADV7393_SD_MODE_REG8_DEFAULT		(0x00)

#define ADV7393_SD_TIMING_REG0_DEFAULT		(0x0C)

/* Bit masks for Mode Select Register */
#define INPUT_MODE_MASK			(0x70)
#define SD_INPUT_MODE			(0x00)
#define HD_720P_INPUT_MODE		(0x10)
#define HD_1080I_INPUT_MODE		(0x10)

/* Bit masks for Mode Register 0 */
#define TEST_PATTERN_BLACK_BAR_EN	(0x04)
#define YUV_OUTPUT_SELECT		(0x20)
#define RGB_OUTPUT_SELECT		(0xDF)

/* Bit masks for SD brightness/WSS */
#define SD_BRIGHTNESS_VALUE_MASK	(0x7F)
#define SD_BLANK_WSS_DATA_MASK		(0x80)

/* Bit masks for soft reset register */
#define SOFT_RESET			(0x02)

/* Bit masks for HD Mode Register 1 */
#define OUTPUT_STD_MASK		(0x03)
#define OUTPUT_STD_SHIFT	(0)
#define OUTPUT_STD_EIA0_2	(0x00)
#define OUTPUT_STD_EIA0_1	(0x01)
#define OUTPUT_STD_FULL		(0x02)
#define EMBEDDED_SYNC		(0x04)
#define EXTERNAL_SYNC		(0xFB)
#define STD_MODE_MASK		(0x1F)
#define STD_MODE_SHIFT		(3)
#define STD_MODE_720P		(0x05)
#define STD_MODE_720P_25	(0x08)
#define STD_MODE_720P_30	(0x07)
#define STD_MODE_720P_50	(0x06)
#define STD_MODE_1080I		(0x0D)
#define STD_MODE_1080I_25	(0x0E)
#define STD_MODE_1080P_24	(0x11)
#define STD_MODE_1080P_25	(0x10)
#define STD_MODE_1080P_30	(0x0F)
#define STD_MODE_525P		(0x00)
#define STD_MODE_625P		(0x03)

/* Bit masks for SD Mode Register 1 */
#define SD_STD_MASK		(0x03)
#define SD_STD_NTSC		(0x00)
#define SD_STD_PAL_BDGHI	(0x01)
#define SD_STD_PAL_M		(0x02)
#define SD_STD_PAL_N		(0x03)
#define SD_LUMA_FLTR_MASK	(0x07)
#define SD_LUMA_FLTR_SHIFT	(2)
#define SD_CHROMA_FLTR_MASK	(0x07)
#define SD_CHROMA_FLTR_SHIFT	(5)

/* Bit masks for SD Mode Register 2 */
#define SD_PRPB_SSAF_EN		(0x01)
#define SD_PRPB_SSAF_DI		(0xFE)
#define SD_DAC_OUT1_EN		(0x02)
#define SD_DAC_OUT1_DI		(0xFD)
#define SD_PEDESTAL_EN		(0x08)
#define SD_PEDESTAL_DI		(0xF7)
#define SD_SQUARE_PIXEL_EN	(0x10)
#define SD_SQUARE_PIXEL_DI	(0xEF)
#define SD_PIXEL_DATA_VALID	(0x40)
#define SD_ACTIVE_EDGE_EN	(0x80)
#define SD_ACTIVE_EDGE_DI	(0x7F)

/* Bit masks for HD Mode Register 6 */
#define HD_PRPB_SYNC_EN		(0x04)
#define HD_PRPB_SYNC_DI		(0xFB)
#define HD_DAC_SWAP_EN		(0x08)
#define HD_DAC_SWAP_DI		(0xF7)
#define HD_GAMMA_CURVE_A	(0xEF)
#define HD_GAMMA_CURVE_B	(0x10)
#define HD_GAMMA_EN		(0x20)
#define HD_GAMMA_DI		(0xDF)
#define HD_ADPT_FLTR_MODEA	(0xBF)
#define HD_ADPT_FLTR_MODEB	(0x40)
#define HD_ADPT_FLTR_EN		(0x80)
#define HD_ADPT_FLTR_DI		(0x7F)

#define ADV7393_BRIGHTNESS_MAX	(63)
#define ADV7393_BRIGHTNESS_MIN	(-64)
#define ADV7393_BRIGHTNESS_DEF	(0)
#define ADV7393_HUE_MAX		(127)
#define ADV7393_HUE_MIN		(-128)
#define ADV7393_HUE_DEF		(0)
#define ADV7393_GAIN_MAX	(64)
#define ADV7393_GAIN_MIN	(-64)
#define ADV7393_GAIN_DEF	(0)

#endif
