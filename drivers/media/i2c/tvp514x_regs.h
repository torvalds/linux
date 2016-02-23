/*
 * drivers/media/i2c/tvp514x_regs.h
 *
 * Copyright (C) 2008 Texas Instruments Inc
 * Author: Vaibhav Hiremath <hvaibhav@ti.com>
 *
 * Contributors:
 *     Sivaraj R <sivaraj@ti.com>
 *     Brijesh R Jadav <brijesh.j@ti.com>
 *     Hardik Shah <hardik.shah@ti.com>
 *     Manjunath Hadli <mrh@ti.com>
 *     Karicheri Muralidharan <m-karicheri2@ti.com>
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef _TVP514X_REGS_H
#define _TVP514X_REGS_H

/*
 * TVP5146/47 registers
 */
#define REG_INPUT_SEL			(0x00)
#define REG_AFE_GAIN_CTRL		(0x01)
#define REG_VIDEO_STD			(0x02)
#define REG_OPERATION_MODE		(0x03)
#define REG_AUTOSWITCH_MASK		(0x04)

#define REG_COLOR_KILLER		(0x05)
#define REG_LUMA_CONTROL1		(0x06)
#define REG_LUMA_CONTROL2		(0x07)
#define REG_LUMA_CONTROL3		(0x08)

#define REG_BRIGHTNESS			(0x09)
#define REG_CONTRAST			(0x0A)
#define REG_SATURATION			(0x0B)
#define REG_HUE				(0x0C)

#define REG_CHROMA_CONTROL1		(0x0D)
#define REG_CHROMA_CONTROL2		(0x0E)

/* 0x0F Reserved */

#define REG_COMP_PR_SATURATION		(0x10)
#define REG_COMP_Y_CONTRAST		(0x11)
#define REG_COMP_PB_SATURATION		(0x12)

/* 0x13 Reserved */

#define REG_COMP_Y_BRIGHTNESS		(0x14)

/* 0x15 Reserved */

#define REG_AVID_START_PIXEL_LSB	(0x16)
#define REG_AVID_START_PIXEL_MSB	(0x17)
#define REG_AVID_STOP_PIXEL_LSB		(0x18)
#define REG_AVID_STOP_PIXEL_MSB		(0x19)

#define REG_HSYNC_START_PIXEL_LSB	(0x1A)
#define REG_HSYNC_START_PIXEL_MSB	(0x1B)
#define REG_HSYNC_STOP_PIXEL_LSB	(0x1C)
#define REG_HSYNC_STOP_PIXEL_MSB	(0x1D)

#define REG_VSYNC_START_LINE_LSB	(0x1E)
#define REG_VSYNC_START_LINE_MSB	(0x1F)
#define REG_VSYNC_STOP_LINE_LSB		(0x20)
#define REG_VSYNC_STOP_LINE_MSB		(0x21)

#define REG_VBLK_START_LINE_LSB		(0x22)
#define REG_VBLK_START_LINE_MSB		(0x23)
#define REG_VBLK_STOP_LINE_LSB		(0x24)
#define REG_VBLK_STOP_LINE_MSB		(0x25)

/* 0x26 - 0x27 Reserved */

#define REG_FAST_SWTICH_CONTROL		(0x28)

/* 0x29 Reserved */

#define REG_FAST_SWTICH_SCART_DELAY	(0x2A)

/* 0x2B Reserved */

#define REG_SCART_DELAY			(0x2C)
#define REG_CTI_DELAY			(0x2D)
#define REG_CTI_CONTROL			(0x2E)

/* 0x2F - 0x31 Reserved */

#define REG_SYNC_CONTROL		(0x32)
#define REG_OUTPUT_FORMATTER1		(0x33)
#define REG_OUTPUT_FORMATTER2		(0x34)
#define REG_OUTPUT_FORMATTER3		(0x35)
#define REG_OUTPUT_FORMATTER4		(0x36)
#define REG_OUTPUT_FORMATTER5		(0x37)
#define REG_OUTPUT_FORMATTER6		(0x38)
#define REG_CLEAR_LOST_LOCK		(0x39)

#define REG_STATUS1			(0x3A)
#define REG_STATUS2			(0x3B)

#define REG_AGC_GAIN_STATUS_LSB		(0x3C)
#define REG_AGC_GAIN_STATUS_MSB		(0x3D)

/* 0x3E Reserved */

#define REG_VIDEO_STD_STATUS		(0x3F)
#define REG_GPIO_INPUT1			(0x40)
#define REG_GPIO_INPUT2			(0x41)

/* 0x42 - 0x45 Reserved */

#define REG_AFE_COARSE_GAIN_CH1		(0x46)
#define REG_AFE_COARSE_GAIN_CH2		(0x47)
#define REG_AFE_COARSE_GAIN_CH3		(0x48)
#define REG_AFE_COARSE_GAIN_CH4		(0x49)

#define REG_AFE_FINE_GAIN_PB_B_LSB	(0x4A)
#define REG_AFE_FINE_GAIN_PB_B_MSB	(0x4B)
#define REG_AFE_FINE_GAIN_Y_G_CHROMA_LSB	(0x4C)
#define REG_AFE_FINE_GAIN_Y_G_CHROMA_MSB	(0x4D)
#define REG_AFE_FINE_GAIN_PR_R_LSB	(0x4E)
#define REG_AFE_FINE_GAIN_PR_R_MSB	(0x4F)
#define REG_AFE_FINE_GAIN_CVBS_LUMA_LSB	(0x50)
#define REG_AFE_FINE_GAIN_CVBS_LUMA_MSB	(0x51)

/* 0x52 - 0x68 Reserved */

#define REG_FBIT_VBIT_CONTROL1		(0x69)

/* 0x6A - 0x6B Reserved */

#define REG_BACKEND_AGC_CONTROL		(0x6C)

/* 0x6D - 0x6E Reserved */

#define REG_AGC_DECREMENT_SPEED_CONTROL	(0x6F)
#define REG_ROM_VERSION			(0x70)

/* 0x71 - 0x73 Reserved */

#define REG_AGC_WHITE_PEAK_PROCESSING	(0x74)
#define REG_FBIT_VBIT_CONTROL2		(0x75)
#define REG_VCR_TRICK_MODE_CONTROL	(0x76)
#define REG_HORIZONTAL_SHAKE_INCREMENT	(0x77)
#define REG_AGC_INCREMENT_SPEED		(0x78)
#define REG_AGC_INCREMENT_DELAY		(0x79)

/* 0x7A - 0x7F Reserved */

#define REG_CHIP_ID_MSB			(0x80)
#define REG_CHIP_ID_LSB			(0x81)

/* 0x82 Reserved */

#define REG_CPLL_SPEED_CONTROL		(0x83)

/* 0x84 - 0x96 Reserved */

#define REG_STATUS_REQUEST		(0x97)

/* 0x98 - 0x99 Reserved */

#define REG_VERTICAL_LINE_COUNT_LSB	(0x9A)
#define REG_VERTICAL_LINE_COUNT_MSB	(0x9B)

/* 0x9C - 0x9D Reserved */

#define REG_AGC_DECREMENT_DELAY		(0x9E)

/* 0x9F - 0xB0 Reserved */

#define REG_VDP_TTX_FILTER_1_MASK1	(0xB1)
#define REG_VDP_TTX_FILTER_1_MASK2	(0xB2)
#define REG_VDP_TTX_FILTER_1_MASK3	(0xB3)
#define REG_VDP_TTX_FILTER_1_MASK4	(0xB4)
#define REG_VDP_TTX_FILTER_1_MASK5	(0xB5)
#define REG_VDP_TTX_FILTER_2_MASK1	(0xB6)
#define REG_VDP_TTX_FILTER_2_MASK2	(0xB7)
#define REG_VDP_TTX_FILTER_2_MASK3	(0xB8)
#define REG_VDP_TTX_FILTER_2_MASK4	(0xB9)
#define REG_VDP_TTX_FILTER_2_MASK5	(0xBA)
#define REG_VDP_TTX_FILTER_CONTROL	(0xBB)
#define REG_VDP_FIFO_WORD_COUNT		(0xBC)
#define REG_VDP_FIFO_INTERRUPT_THRLD	(0xBD)

/* 0xBE Reserved */

#define REG_VDP_FIFO_RESET		(0xBF)
#define REG_VDP_FIFO_OUTPUT_CONTROL	(0xC0)
#define REG_VDP_LINE_NUMBER_INTERRUPT	(0xC1)
#define REG_VDP_PIXEL_ALIGNMENT_LSB	(0xC2)
#define REG_VDP_PIXEL_ALIGNMENT_MSB	(0xC3)

/* 0xC4 - 0xD5 Reserved */

#define REG_VDP_LINE_START		(0xD6)
#define REG_VDP_LINE_STOP		(0xD7)
#define REG_VDP_GLOBAL_LINE_MODE	(0xD8)
#define REG_VDP_FULL_FIELD_ENABLE	(0xD9)
#define REG_VDP_FULL_FIELD_MODE		(0xDA)

/* 0xDB - 0xDF Reserved */

#define REG_VBUS_DATA_ACCESS_NO_VBUS_ADDR_INCR	(0xE0)
#define REG_VBUS_DATA_ACCESS_VBUS_ADDR_INCR	(0xE1)
#define REG_FIFO_READ_DATA			(0xE2)

/* 0xE3 - 0xE7 Reserved */

#define REG_VBUS_ADDRESS_ACCESS1	(0xE8)
#define REG_VBUS_ADDRESS_ACCESS2	(0xE9)
#define REG_VBUS_ADDRESS_ACCESS3	(0xEA)

/* 0xEB - 0xEF Reserved */

#define REG_INTERRUPT_RAW_STATUS0	(0xF0)
#define REG_INTERRUPT_RAW_STATUS1	(0xF1)
#define REG_INTERRUPT_STATUS0		(0xF2)
#define REG_INTERRUPT_STATUS1		(0xF3)
#define REG_INTERRUPT_MASK0		(0xF4)
#define REG_INTERRUPT_MASK1		(0xF5)
#define REG_INTERRUPT_CLEAR0		(0xF6)
#define REG_INTERRUPT_CLEAR1		(0xF7)

/* 0xF8 - 0xFF Reserved */

/*
 * Mask and bit definitions of TVP5146/47 registers
 */
/* The ID values we are looking for */
#define TVP514X_CHIP_ID_MSB		(0x51)
#define TVP5146_CHIP_ID_LSB		(0x46)
#define TVP5147_CHIP_ID_LSB		(0x47)

#define VIDEO_STD_MASK			(0x07)
#define VIDEO_STD_AUTO_SWITCH_BIT	(0x00)
#define VIDEO_STD_NTSC_MJ_BIT		(0x01)
#define VIDEO_STD_PAL_BDGHIN_BIT	(0x02)
#define VIDEO_STD_PAL_M_BIT		(0x03)
#define VIDEO_STD_PAL_COMBINATION_N_BIT	(0x04)
#define VIDEO_STD_NTSC_4_43_BIT		(0x05)
#define VIDEO_STD_SECAM_BIT		(0x06)
#define VIDEO_STD_PAL_60_BIT		(0x07)

/*
 * Status bit
 */
#define STATUS_TV_VCR_BIT		(1<<0)
#define STATUS_HORZ_SYNC_LOCK_BIT	(1<<1)
#define STATUS_VIRT_SYNC_LOCK_BIT	(1<<2)
#define STATUS_CLR_SUBCAR_LOCK_BIT	(1<<3)
#define STATUS_LOST_LOCK_DETECT_BIT	(1<<4)
#define STATUS_FEILD_RATE_BIT		(1<<5)
#define STATUS_LINE_ALTERNATING_BIT	(1<<6)
#define STATUS_PEAK_WHITE_DETECT_BIT	(1<<7)

/* Tokens for register write */
#define TOK_WRITE                       (0)     /* token for write operation */
#define TOK_TERM                        (1)     /* terminating token */
#define TOK_DELAY                       (2)     /* delay token for reg list */
#define TOK_SKIP                        (3)     /* token to skip a register */
/**
 * struct tvp514x_reg - Structure for TVP5146/47 register initialization values
 * @token - Token: TOK_WRITE, TOK_TERM etc..
 * @reg - Register offset
 * @val - Register Value for TOK_WRITE or delay in ms for TOK_DELAY
 */
struct tvp514x_reg {
	u8 token;
	u8 reg;
	u32 val;
};

#endif				/* ifndef _TVP514X_REGS_H */
