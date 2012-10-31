/*
 * Copyright (C) 2007-2012 Allwinner Technology Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef __DE_FE_H__
#define __DE_FE_H__

#include "de_be.h"

#define SCALINITPASELMT (0xfffff)
#define SCALLINEMAX (2048)

/*
 * Detail information of registers
 */

typedef union {
	u32 dwval;
	struct {
		/*
		 * RIGHT_CH0_ADDR: 3D mode channel 0 buffer address.
		 * This address is the start address of right image in 3D mode.
		 */
		u32 right_ch0_addr; /* Default: 0 */
	} bits;
} SCAL_3D_BUF_ADDR0_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * RIGHT_CH1_ADDR: 3D mode channel 1 buffer address.
		 * This address is the start address of right image in 3D mode.
		 */
		u32 right_ch1_addr; /* Default: 0 */
	} bits;
} SCAL_3D_BUF_ADDR1_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * RIGHT_CH2_ADDR: 3D mode channel 2 buffer address.
		 * This address is the start address of right image in 3D mode.
		 */
		u32 right_ch2_addr; /* Default: 0 */
	} bits;
} SCAL_3D_BUF_ADDR2_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * MOD_SEL: 3D mode select.
		 * 00: normal output mode(2D mode)
		 * 01: 3D side by side/line interleaved/column interleaved
		 *     output mode
		 * 10: 3D top/bottom output mode,
		 * 11: reserved
		 *
		 * When 3D mode is enable, scaler will enter 3D mode (source
		 * will be composed of left and right frame, output will be
		 * composed of left and right frame)
		 */
		u32 mod_sel:2; /* Default: 0 */
		u32 res0:6;
		/*
		 * CI_OUT_EN: 3D Column interleaved mode output enable
		 */
		u32 ci_out_en:1; /* Default: 0 */
		u32 res1:1;
		/*
		 * SS_OUT_EN: 3D output side by side mode enable
		 */
		u32 ss_out_en:1; /* Default: 0 */
		/*
		 * LI_IN_EN: 3D input line interleaved enable
		 */
		u32 li_in_en:1; /* Default: 0 */
		/*
		 * TB_OUT_SCAN_MOD: Output top/bottom scan mode selection
		 *  0: progressive
		 *  1: interlace
		 */
		u32 tb_out_scan_mod:1; /* Default: 0 */
		u32 res2:3;
		/*
		 * CI_OUT_MOD: 3D column interleaved mode
		 *  0: CI_1
		 *  1: CI_2
		 *  2: CI_3
		 *  3: CI_4
		 * Other: reserved
		 */
		u32 ci_out_mod:3; /* Default: 0 */
		u32 res3:5;
		/*
		 * TB_OUT_MOD_FIELD: Top/bottom output mode field number
		 * 0: left or left 1st field(determined by reg0x2c)
		 * 1: right or right 1st field
		 * 2: left 2nd field
		 * 3: right 2nd field
		 */
		u32 tb_out_mod_field:2; /* Default: 0 */
		u32 res4:6;
	} bits;
} SCAL_3D_CTRL_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * X_OFFSET0: The x offset of the top-left point in the first
		 * macro block. This value is the start offset of right image in
		 * 3D mode.
		 */
		u32 x_offset0:5; /* Default: 0 */
		u32 res0:3;
		/*
		 * Y_OFFSET0: The y offset of the top-left point in the first
		 * macro block.
		 */
		u32 y_offset0:5; /* Default: 0 */
		u32 res1:3;
		/*
		 * X_OFFSET1: The x offset of the bottom-right point in the
		 * first macro block.
		 */
		u32 x_offset1:5; /* Default: 0 */
		u32 res2:11;
	} bits;
} SCAL_3D_MB_OFF0_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * X_OFFSET0: The x offset of the top-left point in the first
		 * macro block, This value is the start offset of right image
		 * in 3D mode.
		 */
		u32 x_offset0:5; /* Default: 0 */
		u32 res0:3;
		/*
		 * Y_OFFSET0: The y offset of the top-left point in the first
		 * macro block.
		 */
		u32 y_offset0:5; /* Default: 0 */
		u32 res1:3;
		/*
		 * X_OFFSET1: The x offset of the bottom-right point in the
		 * first macro block.
		 */
		u32 x_offset1:5; /* Default: 0 */
		u32 res2:11;
	} bits;
} SCAL_3D_MB_OFF1_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * X_OFFSET0: The x offset of the top-left point in the first
		 * macro block, This value is the start offset of right image
		 * in 3D mode.
		 */
		u32 x_offset0:5; /* Default: 0 */
		u32 res0:3;
		/*
		 * Y_OFFSET0: The y offset of the top-left point in the first
		 * macro block.
		 */
		u32 y_offset0:5; /* Default: 0 */
		u32 res1:3;
		/*
		 * X_OFFSET1: The x offset of the bottom-right point in the
		 * first macro block.
		 */
		u32 x_offset1:5; /* Default: 0 */
		u32 res2:11;
	} bits;
} SCAL_3D_MB_OFF2_REG;

typedef union {
	u32 dwval;
	struct {
		u32 res0:8;
		/*
		 * LINEBUF_AGTH: Scaler line buffer algorithm select.
		 * 0: horizontal filtered result
		 * 1: original data
		 */
		u32 linebuf_agth:1; /* Default: 0 */
		u32 res1:23;
	} bits;
} SCAL_AGTH_SEL_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * BUF_ADDR: Scaler frame buffer address.
		 * In macro block type:
		 *   The address is the start address of the line in the first
		 *   macro block used to generating output frame.
		 * In non-macro block type:
		 *   The address is the start address of the first line.
		 */
		u32 buf_addr; /* Default: 0 */
	} bits;
} SCAL_BUF_ADDR0_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * BUF_ADDR: Scaler frame buffer address.
		 * In macro block type:
		 *   The address is the start address of the line in the first
		 *   macro block used to generating output frame.
		 * In non-macro block type:
		 *   The address is the start address of the first line.
		 */
		u32 buf_addr; /* Default: 0 */
	} bits;
} SCAL_BUF_ADDR1_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * BUF_ADDR: Scaler frame buffer address.
		 * In macro block type:
		 *   The address is the start address of the line in the first
		 *   macro block used to generating output frame.
		 * In non-macro block type:
		 *   The address is the start address of the first line.
		 */
		u32 buf_addr; /* Default: 0 */
	} bits;
} SCAL_BUF_ADDR2_REG;

typedef union {
	u32 dwval;
	struct {
		u32 res0:1;
		/*
		 * CSC_BYPASS_EN: CSC by-pass enable.
		 * 0: CSC enable
		 * 1: CSC will be by-passed
		 * Actually, in order ensure the module working be correct,
		 * this bit only can be set when input data format is the same
		 * as output data format (both YUV or both RGB).
		 */
		u32 csc_bypass_en:1; /* Default: 0 */
		u32 res1:1;
	} bits;
} SCAL_BYPASS_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * TAP0: Horizontal tap0 coefficient.
		 * The value equals to coefficient * 26
		 */
		u32 tap0:8; /* default: 0 */
		/*
		 * TAP1: Horizontal tap1 coefficient.
		 * The value equals to coefficient * 26
		 */
		u32 tap1:8; /* default: 0 */
		/*
		 * TAP2: Horizontal tap2 coefficient.
		 * The value equals to coefficient * 26
		 */
		u32 tap2:8; /* default: 0 */
		/*
		 * TAP3: Horizontal tap3 coefficient.
		 * The value equals to coefficient * 26
		 */
		u32 tap3:8; /* default: 0 */
	} bits;
} SCAL_CH0_HORZCOEF0_REGNN0;

typedef union {
	u32 dwval;
	struct {
		/*
		 * TAP0: Horizontal tap0 coefficient.
		 * The value equals to coefficient * 26
		 */
		u32 tap0:8; /* default: 0 */
		/*
		 * TAP1: Horizontal tap1 coefficient.
		 * The value equals to coefficient * 26
		 */
		u32 tap1:8; /* default: 0 */
		/*
		 * TAP2: Horizontal tap2 coefficient.
		 * The value equals to coefficient * 26
		 */
		u32 tap2:8; /* default: 0 */
		/*
		 * TAP3: Horizontal tap3 coefficient.
		 * The value equals to coefficient * 26
		 */
		u32 tap3:8; /* default: 0 */
	} bits;
} SCAL_CH0_HORZCOEF0_REGN;

typedef union {
	u32 dwval;
	struct {
		/*
		 * TAP4: Horizontal tap4 coefficient.
		 * The value equals to coefficient * 26
		 */
		u32 tap4:8; /* default: 0 */
		/*
		 * TAP5: Horizontal tap5 coefficient.
		 * The value equals to coefficient * 26
		 */
		u32 tap5:8; /* default: 0 */
		/*
		 * TAP6: Horizontal tap6 coefficient.
		 * The value equals to coefficient * 26
		 */
		u32 tap6:8; /* default: 0 */
		/*
		 * TAP7: Horizontal tap7 coefficient.
		 * The value equals to coefficient * 26
		 */
		u32 tap7:8; /* default: 0 */
	} bits;
} SCAL_CH0_HORZCOEF1_REGNN0;

typedef union {
	u32 dwval;
	struct {
		/*
		 * TAP4: Horizontal tap4 coefficient.
		 * The value equals to coefficient * 26
		 */
		u32 tap4:8; /* default: 0 */
		 /*
		 * TAP5: Horizontal tap5 coefficient.
		 * The value equals to coefficient * 26
		 */
		u32 tap5:8; /* default: 0 */
		/*
		 * TAP6: Horizontal tap6 coefficient.
		 * The value equals to coefficient * 26
		 */
		u32 tap6:8; /* default: 0 */
		/*
		 * TAP7: Horizontal tap7 coefficient.
		 * The value equals to coefficient * 26
		 */
		u32 tap7:8; /* default: 0 */
	} bits;
} SCAL_CH0_HORZCOEF1_REGN;

typedef union {
	u32 dwval;
	struct {

		/*
		 * FACTOR_FRAC: The fractional part of the horizontal scaling
		 * ratio. The horizontal scaling ratio = input width/output
		 * width.
		 */
		u32 factor_frac:16; /* Default: 0 */
		/*
		 * FACTOR_INT: The integer part of the horizontal scaling ratio.
		 * The horizontal scaling ratio = input width/output width.
		 */
		u32 factor_int:8; /* Default: 0 */
		u32 res0:8;
	} bits;
} SCAL_CH0_HORZFACT_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * PHASE: Y/G component initial phase in horizontal
		 * (complement). This value equals to initial phase * 216.
		 */
		u32 phase:20; /* Default: 0 */
		u32 res0:12;
	} bits;
} SCAL_CH0_HORZPHASE_REG;

typedef union {
	u32 dwval;
	struct {
		/* TAP0: Tap 0 offset in horizontal */
		u32 tap0:7; /* Default: 0x7D */
		u32 res0:1;
		/* TAP1: Tap 1 offset in horizontal */
		u32 tap1:7; /* Default: 0x1 */
		u32 res1:1;
		/* TAP2: Tap 2 offset in horizontal */
		u32 tap2:7; /* Default: 0x1 */
		u32 res2:1;
		/* TAP3: Tap 3 offset in horizontal */
		u32 tap3:7; /* Default: 0x1 */
		u32 res3:1;
	} bits;
} SCAL_CH0_HORZTAP0_REG;

typedef union {
	u32 dwval;
	struct {
		/* TAP4: Tap 4 offset in horizontal */
		u32 tap4:7; /* Default: 0x1 */
		u32 res0:1;
		/* TAP5: Tap 5 offset in horizontal */
		u32 tap5:7; /* Default: 0x1 */
		u32 res1:1;
		/* TAP6: Tap 6 offset in horizontal */
		u32 tap6:7; /* Default: 0x1 */
		u32 res2:1;
		/* TAP7: Tap 7 offset in horizontal */
		u32 tap7:7; /* Default: 0x1 */
		u32 res3:1;
	} bits;
} SCAL_CH0_HORZTAP1_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * IN_WIDTH: Input image Y/G component width. The image width =
		 * The value of these bits add 1. When line buffer result
		 * selection is original data, the maximum width is 2048.
		 */
		u32 in_width:13; /* Default: 0x0 */
		u32 res0:3;
		/*
		 * IN_HEIGHT: Input image Y/G component height. Input image
		 * height = The value of these bits add 1.
		 */
		u32 in_height:13; /* Default: 0x0 */
		u32 res1:3;
	} bits;
} SCAL_CH0_INSIZE_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * OUT_WIDTH: Output layer Y/G component width. The output layer
		 * width = The value of these bits add 1. When line buffer
		 * result selection is horizontal filtered result, the maximum
		 * width is 2048.
		 */
		u32 out_width:13; /* Default: 0x0 */
		u32 res0:3;
		/*
		 * OUT_HEIGHT: Output layer Y/G component height. The output
		 * layer height = The value of these bits add 1.
		 */
		u32 out_height:13; /* Default: 0x0 */
		u32 res1:3;
	} bits;
} SCAL_CH0_OUTSIZE_REG;

typedef union {
	u32 dwval;

	struct {

		/*
		 * TAP0: Vertical tap0 coefficient.
		 * The value equals to coefficient*26.
		 */
		u32 tap0:8; /* Default: 0 */
		/*
		 * TAP1: Vertical tap1 coefficient.
		 * The value equals to coefficient*26.
		 */
		u32 tap1:8; /* Default: 0 */
		/*
		 * TAP2: Vertical tap2 coefficient.
		 * The value equals to coefficient*26.
		 */
		u32 tap2:8; /* Default: 0 */
		/*
		 * TAP3: Vertical tap3 coefficient.
		 * The value equals to coefficient*26.
		 */
		u32 tap3:8; /* Default: 0 */
	} bits;
} SCAL_CH0_VERTCOEF_REGNN0;

typedef union {
	u32 dwval;
	struct {
		/*
		 * TAP0: Vertical tap0 coefficient.
		 * The value equals to coefficient*26.
		 */
		u32 tap0:8; /* Default: 0 */
		 /*
		 * TAP1: Vertical tap1 coefficient.
		 * The value equals to coefficient*26.
		 */
		u32 tap1:8; /* Default: 0 */
		/*
		 * TAP2: Vertical tap2 coefficient.
		 * The value equals to coefficient*26.
		 */
		u32 tap2:8; /* Default: 0 */
		/*
		 * TAP3: Vertical tap3 coefficient.
		 * The value equals to coefficient*26.
		 */
		u32 tap3:8; /* Default: 0 */
	} bits;
} SCAL_CH0_VERTCOEF_REGN;

typedef union {
	u32 dwval;
	struct {
		/*
		 * FACTOR_FRAC: The fractional part of the vertical scaling
		 * ratio. The vertical scaling ratio = input height /output
		 * height.
		 */
		u32 factor_frac:16; /* Default: 0 */
		/*
		 * FACTOR_INT: The integer part of the vertical scaling ratio.
		 * The vertical scaling ratio = input height/output height.
		 */
		u32 factor_int:8; /* Default: 0x0 */
		u32 res0:8;
	} bits;
} SCAL_CH0_VERTFACT_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * PHASE: Y/G component initial phase in vertical for top field
		 * (complement). This value equals to initial phase * 216.
		 */
		u32 phase:20; /* Default: 0 */
		u32 res0:12;
	} bits;
} SCAL_CH0_VERTPHASE0_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * PHASE: Y/G component initial phase in vertical for bottom
		 * field (complement). This value equals to initial phase * 216.
		 */
		u32 phase:20; /* Default: 0 */
		u32 res0:12;
	} bits;
} SCAL_CH0_VERTPHASE1_REG;

typedef union {
	u32 dwval;
	struct {
		/* TAP0: Tap 0 offset in vertical. */
		u32 tap0:7; /* Default: 0x7F */
		u32 res0:1;
		/* TAP1: Tap 1 offset in vertical. */
		u32 tap1:7; /* Default: 0x1 */
		u32 res1:1;
		/* TAP2: Tap 2 offset in vertical. */
		u32 tap2:7; /* Default: 0x1 */
		u32 res2:1;
		/* TAP3: Tap 3 offset in vertical. */
		u32 tap3:7; /* Default: 0x1 */
		u32 res3:1;
	} bits;
} SCAL_CH0_VERTTAP;

typedef union {
	u32 dwval;
	struct {
		/* TAP0: Tap 0 offset in vertical. */
		u32 tap0:7; /* Default: 0x7F */
		u32 res0:1;
		/* TAP1: Tap 1 offset in vertical. */
		u32 tap1:7; /* Default: 0x1 */
		u32 res1:1;
		/* TAP2: Tap 2 offset in vertical. */
		u32 tap2:7; /* Default: 0x1 */
		u32 res2:1;
		/* TAP3: Tap 3 offset in vertical. */
		u32 tap3:7; /* Default: 0x1 */
		u32 res3:1;
	} bits;
} SCAL_CH0_VERTTAP_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * TAP0: Horizontal tap0 coefficient.
		 * The value equals to coefficient * 26
		 */
		u32 tap0:8; /* default: 0 */
		 /*
		 * TAP1: Horizontal tap1 coefficient.
		 * The value equals to coefficient * 26
		 */
		u32 tap1:8; /* default: 0 */
		/*
		 * TAP2: Horizontal tap2 coefficient.
		 * The value equals to coefficient * 26
		 */
		u32 tap2:8; /* default: 0 */
		/*
		 * TAP3: Horizontal tap3 coefficient.
		 * The value equals to coefficient * 26
		 */
		u32 tap3:8; /* default: 0 */
	} bits;
} SCAL_CH1_HORZCOEF0_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * TAP0: Horizontal tap0 coefficient.
		 * The value equals to coefficient * 26
		 */
		u32 tap0:8; /* default: 0 */
		/*
		 * TAP1: Horizontal tap1 coefficient.
		 * The value equals to coefficient * 26
		 */
		u32 tap1:8; /* default: 0 */
		/*
		 * TAP2: Horizontal tap2 coefficient.
		 * The value equals to coefficient * 26
		 */
		u32 tap2:8; /* default: 0 */
		/*
		 * TAP3: Horizontal tap3 coefficient.
		 * The value equals to coefficient * 26
		 */
		u32 tap3:8; /* default: 0 */
	} bits;
} SCAL_CH1_HORZCOEF0_REGN;

typedef union {
	u32 dwval;
	struct {
		/*
		 * TAP4: Horizontal tap4 coefficient.
		 * The value equals to coefficient * 26
		 */
		u32 tap4:8; /* default: 0 */
		/*
		 * TAP5: Horizontal tap5 coefficient.
		 * The value equals to coefficient * 26
		 */
		u32 tap5:8; /* default: 0 */
		/*
		 * TAP6: Horizontal tap6 coefficient.
		 * The value equals to coefficient * 26
		 */
		u32 tap6:8; /* default: 0 */
		/*
		 * TAP7: Horizontal tap7 coefficient.
		 * The value equals to coefficient * 26
		 */
		u32 tap7:8; /* default: 0 */
	} bits;
} SCAL_CH1_HORZCOEF1_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * TAP4: Horizontal tap4 coefficient.
		 * The value equals to coefficient * 26
		 */
		u32 tap4:8; /* default: 0 */
		/*
		 * TAP5: Horizontal tap5 coefficient.
		 * The value equals to coefficient * 26
		 */
		u32 tap5:8; /* default: 0 */
		/*
		 * TAP6: Horizontal tap6 coefficient.
		 * The value equals to coefficient * 26
		 */
		u32 tap6:8; /* default: 0 */
		/*
		 * TAP7: Horizontal tap7 coefficient.
		 * The value equals to coefficient * 26
		 */
		u32 tap7:8; /* default: 0 */
	} bits;
} SCAL_CH1_HORZCOEF1_REGN;

typedef union {
	u32 dwval;
	struct {
		/*
		 * FACTOR_FRAC: The fractional part of the horizontal scaling
		 * ratio. The horizontal scaling ratio = input width/output
		 * width.
		 */
		u32 factor_frac:16; /* Default: 0 */
		/*
		 * FACTOR_INT: The integer part of the horizontal scaling ratio.
		 * the horizontal scaling ratio = input width/output width.
		 */
		u32 factor_int:8; /* Default: 0 */
		u32 res0:8;
	} bits;
} SCAL_CH1_HORZFACT_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * PHASE: U/R component initial phase in horizontal
		 * (complement). This value equals to initial phase * 216.
		 */
		u32 phase:20; /* Default: 0 */
		u32 res0:12;
	} bits;
} SCAL_CH1_HORZPHASE_REG;

typedef union {
	u32 dwval;
	struct {
		/* TAP0: Tap 0 offset in horizontal */
		u32 tap0:7; /* Default: 0x7D */
		u32 res0:1;
		/* TAP1: Tap 1 offset in horizontal */
		u32 tap1:7; /* Default: 0x1 */
		u32 res1:1;
		/* TAP2: Tap 2 offset in horizontal */
		u32 tap2:7; /* Default: 0x1 */
		u32 res2:1;
		/* TAP3: Tap 3 offset in horizontal */
		u32 tap3:7; /* Default: 0x1 */
		u32 res3:1;
	} bits;
} SCAL_CH1_HORZTAP0_REG;

typedef union {
	u32 dwval;
	struct {
		/* TAP4: Tap 4 offset in horizontal */
		u32 tap4:7; /* Default: 0x1 */
		u32 res0:1;
		/* TAP5: Tap 5 offset in horizontal */
		u32 tap5:7; /* Default: 0x1 */
		u32 res1:1;
		/* TAP6: Tap 6 offset in horizontal */
		u32 tap6:7; /* Default: 0x1 */
		u32 res2:1;
		/* TAP7: Tap 7 offset in horizontal */
		u32 tap7:7; /* Default: 0x1 */
		u32 res3:1;
	} bits;
} SCAL_CH1_HORZTAP1_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * IN_WIDTH: Input image U/R component width, The image width =
		 * The value of these bits add 1. When line buffer result
		 * selection is original data, the maximum width is 2048.
		 */
		u32 in_width:13; /* Default: 0x0 */
		u32 res0:3;
		/*
		 * IN_HEIGHT: Input image U/R component height. Input image
		 * height = The value of these bits add 1.
		 */
		u32 in_height:13; /* Default: 0x0 */
		u32 res1:3;
	} bits;
} SCAL_CH1_INSIZE_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * OUT_WIDTH: Output layer U/R component width. The output layer
		 * width = The value of these bits add 1. When line buffer
		 * result selection is horizontal filtered result, the maximum
		 * width is 2048.
		 */
		u32 out_width:13; /* Default: 0 */
		u32 res0:3;
		/*
		 * OUT_HEIGHT: Output layer U/R component height. The output
		 * layer height = The value of these bits add 1.
		 */
		u32 out_height:13; /* Default: 0 */
		u32 res1:3;
	} bits;
} SCAL_CH1_OUTSIZE_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * TAP0: Vertical tap0 coefficient.
		 * The value equals to coefficient*26.
		 */
		u32 tap0:8; /* Default: 0 */
		/*
		 * TAP1: Vertical tap1 coefficient.
		 * The value equals to coefficient*26.
		 */
		u32 tap1:8; /* Default: 0 */
		/*
		 * TAP2: Vertical tap2 coefficient.
		 * The value equals to coefficient*26.
		 */
		u32 tap2:8; /* Default: 0 */
		/*
		 * TAP3: Vertical tap3 coefficient.
		 * The value equals to coefficient*26.
		 */
		u32 tap3:8; /* Default: 0 */
	} bits;
} SCAL_CH1_VERTCOEF_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * TAP0: Vertical tap0 coefficient.
		 * The value equals to coefficient*26.
		 */
		u32 tap0:8; /* Default: 0 */
		/*
		 * TAP1: Vertical tap1 coefficient.
		 * The value equals to coefficient*26.
		 */
		u32 tap1:8; /* Default: 0 */
		 /*
		 * TAP2: Vertical tap2 coefficient.
		 * The value equals to coefficient*26.
		 */
		u32 tap2:8; /* Default: 0 */
		/*
		 * TAP3: Vertical tap3 coefficient.
		 * The value equals to coefficient*26.
		 */
		u32 tap3:8; /* Default: 0 */
	} bits;
} SCAL_CH1_VERTCOEF_REGN;

typedef union {
	u32 dwval;
	struct {
		/*
		 * FACTOR_FRAC: The fractional part of the vertical scaling
		 * ratio. The vertical scaling ratio = input height / output
		 * height.
		 */
		u32 factor_frac:16; /* Default: 0 */
		/*
		 * FACTOR_INT: The integer part of the vertical scaling ratio.
		 * The vertical scaling ratio = input height / output height.
		 */
		u32 factor_int:8; /* Default: 0 */
		u32 res0:8;
	} bits;
} SCAL_CH1_VERTFACT_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * PHASE: U/R component initial phase in vertical for top field
		 * (complement). This value equals to initial phase * 216.
		 */
		u32 phase:20; /* Default: 0 */
		u32 res0:12;
	} bits;
} SCAL_CH1_VERTPHASE0_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * PHASE: U/R component initial phase in vertical for bottom
		 * field (complement). This value equals to initial phase * 216.
		 */
		u32 phase:20; /* Default: 0 */
		u32 res0:12;
	} bits;
} SCAL_CH1_VERTPHASE1_REG;

typedef union {
	u32 dwval;
	struct {
		/* TAP0: Tap 0 offset in vertical. */
		u32 tap0:7; /* Default: 0x7F */
		u32 res0:1;
		/* TAP1: Tap 1 offset in vertical. */
		u32 tap1:7; /* Default: 0x1 */
		u32 res1:1;
		/* TAP2: Tap 2 offset in vertical. */
		u32 tap2:7; /* Default: 0x1 */
		u32 res2:1;
		/* TAP3: Tap 3 offset in vertical. */
		u32 tap3:7; /* Default: 0x1 */
		u32 res3:1;
	} bits;
} SCAL_CH1_VERTTAP;

typedef union {
	u32 dwval;
	struct {
		/* TAP0: Tap 0 offset in vertical. */
		u32 tap0:7; /* Default: 0x7F */
		u32 res0:1;
		/* TAP1: Tap 1 offset in vertical. */
		u32 tap1:7; /* Default: 0x1 */
		u32 res1:1;
		/* TAP2: Tap 2 offset in vertical. */
		u32 tap2:7; /* Default: 0x1 */
		u32 res2:1;
		/* TAP3: Tap 3 offset in vertical. */
		u32 tap3:7; /* Default: 0x1 */
		u32 res3:1;
	} bits;
} SCAL_CH1_VERTTAP_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * COEF: the Y/G coefficient. The value equals to coefficient *
		 * 210.
		 */
		u32 coef:13; /* Default: 0 */
		u32 res0:19;
	} bits;
} SCAL_CSC_COEF00_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * COEF: the Y/G coefficient. The value equals to coefficient *
		 * 210.
		 */
		u32 coef:13; /* Default: 0 */
		u32 res0:19;
	} bits;
} SCAL_CSC_COEF01_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * COEF: the Y/G coefficient. The value equals to coefficient *
		 * 210.
		 */
		u32 coef:13; /* Default: 0 */
		u32 res0:19;
	} bits;
} SCAL_CSC_COEF02_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * CONT: the Y/G constant. The value equals to coefficient * 24.
		 */
		u32 cont:14; /* Default: 0 */
		u32 res0:18;
	} bits;
} SCAL_CSC_COEF03_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * COEF: the U/R coefficient. The value equals to coefficient *
		 * 210.
		 */
		u32 coef:13; /* Default: 0 */
		u32 res0:19;
	} bits;
} SCAL_CSC_COEF10_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * COEF: the U/R coefficient. The value equals to coefficient *
		 * 210.
		 */
		u32 coef:13; /* Default: 0 */
		u32 res0:19;
	} bits;
} SCAL_CSC_COEF11_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * COEF: the U/R coefficient. The value equals to coefficient *
		 * 210.
		 */
		u32 coef:13; /* Default: 0 */
		u32 res0:19;
	} bits;
} SCAL_CSC_COEF12_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * CONT: the U/R constant. The value equals to coefficient * 24.
		 */
		u32 cont:14; /* Default: 0 */
		u32 res0:18;
	} bits;
} SCAL_CSC_COEF13_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * COEF: the V/B coefficient. The value equals to coefficient *
		 * 210.
		 */
		u32 coef:13; /* Default: 0 */
		u32 res0:19;
	} bits;
} SCAL_CSC_COEF20_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * COEF: the V/B coefficient. The value equals to coefficient *
		 * 210.
		 */
		u32 coef:13; /* Default: 0 */
		u32 res0:19;
	} bits;
} SCAL_CSC_COEF21_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * COEF: the V/B coefficient. The value equals to coefficient *
		 * 210.
		 */
		u32 coef:13; /* Default: 0 */
		u32 res0:19;
	} bits;
} SCAL_CSC_COEF22_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * CONT: the V/B constant. The value equals to coefficient * 24.
		 */
		u32 cont:14; /* Default: 0 */
		u32 res0:18;
	} bits;
} SCAL_CSC_COEF23_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * BLK_FLAG_ADDR: Current frame block flag buffer address.
		 */
		u32 blk_flag_addr; /* Default: 0 */
	} bits;
} SCAL_DI_BLKFLAG_REG;

typedef union {
	u32 dwval;
	struct {
		/* LUMA: Luma burst length */
		u32 luma:6; /* Default: 0x1F */
		u32 res0:2;
		/* CHROMA: Chroma burst length */
		u32 chroma:6; /* Default: 0x1F */
		u32 res1:18;
	} bits;
} SCAL_DI_BURSTLEN_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * EN: De-interlacing enable.
		 * 0: de-interlacing disable.
		 * 1: de-interlacing enable.
		 */
		u32 en:1; /* Default: 0 */
		u32 res0:15;
		/*
		 * MOD: De-interlacing mode select.
		 * 00: weave
		 * 01: bob
		 * 10: DI-MAF
		 * 11: DI-MAF-BOB
		 */
		u32 mod:2; /* Default: 0 */
		u32 res1:6;
		/*
		 * DIAGINTP_EN: De-interlacing diagonal interpolate enable.
		 * 0: disable
		 * 1: enable
		 */
		u32 diagintp_en:1; /* Default: 0 */
		/*
		 * TEMPDIFF_EN: Temporal difference compare enable.
		 * 0: disable
		 * 1: enable
		 */
		u32 tempdiff_en:1; /* Default: 0 */
		u32 res2:6;
	} bits;
} SCAL_DI_CTRL_REG;

typedef union {
	u32 dwval;
	struct {
		/* TH0: Diagintp_th0 */
		u32 th0:7; /* Default: 0x4F */
		u32 res0:1;
		/* TH1: Diagintp_th1 */
		u32 th1:7; /* Default: 0x5 */
		u32 res1:1;
		/* TH2: Diagintp_th2 */
		u32 th2:8; /* Default: 0x10 */
		/* TH3: Diagintp_th3 */
		u32 th3:8; /* Default: 0x8 */
	} bits;
} SCAL_DI_DIAGINTP_REG;

typedef union {
	u32 dwval;
	struct {
		/* BLK_FLAG_LINESTRD: block flag line-stride. */
		u32 blk_flag_linestrd; /* Default: 0x40 */
	} bits;
} SCAL_DI_FLAGLINESTRD_REG;

typedef union {
	u32 dwval;
	struct {
		/* PREFRM_ADDR: Pre-frame buffer address of luma. */
		u32 prefrm_addr; /* Default: 0 */
	} bits;
} SCAL_DI_PRELUMA_REG;

typedef union {
	u32 dwval;
	struct {
		/* TH1: Sawtooth_th1 */
		u32 th1:8; /* Default: 0x14 */
		/* TH2: Sawtooth_th2 */
		u32 th2:8; /* Default: 0x8 */
		u32 res0:16;
	} bits;
} SCAL_DI_SAWTOOTH_REG;

typedef union {
	u32 dwval;
	struct {
		/* TH0: spatial_th0 */
		u32 th0:9; /* Default: 0x64 */
		u32 res0:7;
		/* TH1: spatial_th1 */
		u32 th1:9; /* Default: 0xA */
		u32 res1:7;
	} bits;
} SCAL_DI_SPATCOMP_REG;

typedef union {
	u32 dwval;
	struct {
		u32 res0:8;
		/* TH: Temporal_th */
		u32 th:5; /* Default: 0xF */
		u32 res1:19;
	} bits;
} SCAL_DI_TEMPDIFF_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * EN: Scaler enable
		 *  0: Disable
		 *  1: Enable
		 * When scaler enable bit is disabled, the clock of scaler
		 * module will be disabled. If this bit is transitioned from 0
		 * to 1, the frame process control register and the interrupt
		 * enable register will be initialed to default value, and the
		 * state machine of the module is reset.
		 */
		u32 en:1; /* Default: 0 */
		u32 res0:31;
	} bits;
} SCAL_EN_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * FIELD_CNT: Field counter, each bit specify a field to
		 * display.
		 * 0: top field
		 * 1: bottom field
		 */
		u32 field_cnt:8; /* Default: 0 */
		/*
		 * VALID_FIELD_CNT: Valid field counter bit.
		 * the valid value = this value + 1
		 */
		u32 valid_field_cnt:3; /* Default: 0 */
		u32 res0:1;
		/*
		 * FIELD_LOOP_MOD: Field loop mode
		 * 0: the last field
		 * 1: the full frame
		 */
		u32 field_loop_mod:1; /* Default: 0 */
		u32 res1:19;
	} bits;
} SCAL_FIELD_CTRL_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * REG_RDY_EN: Register ready enable.
		 *  0: not ready
		 *  1: registers configuration ready.
		 * As same as filter coefficients configuration, in order to
		 * ensure the display be correct, the correlative display
		 * configuration registers are buffered too, the programmer also
		 * can change the value of correlative registers in any time.
		 * When the registers setting is finished, the programmer should
		 * set the bit if the programmer need the new configuration in
		 * next scaling frame., When the new frame start, the bit will
		 * also be self-cleared.
		 */
		u32 reg_rdy_en:1; /* Default: 0 */
		/*
		 * COEF_RDY_EN: Filter coefficients ready enable.
		 *  0: not ready
		 *  1: filter coefficients configuration ready.
		 * In order to avoid the noise, you have to ensure the same set
		 * filter coefficients are used in one frame, so the filter
		 * coefficients are buffered, the programmer can change the
		 * coefficients in any time. When the filter coefficients
		 * setting is finished, the pro, rammer should set the bit if
		 * the programmer need the new coefficients in next scaling
		 * frame. When the new frame start, the bit will be
		 * self-cleared.
		 */
		u32 coef_rdy_en:1; /* Default: 0 */
		/*
		 * WB_EN: Write back enable.
		 *  0: Disable
		 *  1: Enable
		 * If output to image is enable, the writing back process will
		 * start when write back enable bit is set and a new frame
		 * processing begins. The bit will be self-cleared when
		 * writing-back frame process starts.
		 */
		u32 wb_en:1; /* Default: 0 */
		u32 res0:5;
		/*
		 * OUT_PORT_SEL: Scaler output port select.
		 * 00: image0
		 * 01: image1
		 * other: reserved
		 */
		u32 out_port_sel:2; /* Default: 0 */
		u32 res1:1;
		/*
		 * OUT_CTRL: Scaler output control.
		 *  0: enable scaler output to image
		 *  1: disable scaler output to image.
		 * If scaler write back function is enable, scaler output to
		 * image isn't recommended.
		 */
		u32 out_ctrl:1; /* Default: 0 */
		/*
		 * IN_CTRL: Scaler input source control.
		 * 000: from dram
		 * 100: from image0 interface of image2lcd (don't influence the
		 *      interface timing of image)
		 * 101: from image1 interface of image2lcd (don't influence the
		 *      interface timing of image)
		 * 110: from image0 (influence the interface timing of image)
		 * 111: from image1 (influence the interface timing of image)
		 * Other: reserved
		 */
		u32 in_ctrl:3; /* Default: 0 */
		u32 res2:1;
		/*
		 * FRM_START: Frame start & reset control.
		 *  0: reset
		 *  1: start
		 * If the bit is written to zero, the whole state machine and
		 * data paths of scaler module will be reset., When the bit is
		 * written to 1, Scaler will start a new frame process.
		 */
		u32 frm_start:1; /* Default: 0 */
#ifdef CONFIG_ARCH_SUN4I
		u32 res3:15;
#else
		u32 res3:6;
		/* fir coef ram access control */
		u32 coef_access_ctrl:1;
		u32 res4:8;
#endif
	} bits;
} SCAL_FRM_CTRL_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * DATA_PS: Pixel sequence.
		 * In interleaved YUV422 data mode:
		 *  00: Y1V0Y0U0
		 *  01: V0Y1U0Y0
		 *  10: Y1U0Y0V0
		 *  11: U0Y1V0Y0
		 * In interleaved YUV444 data mode:
		 *  00: VUYA
		 *  01: AYUV
		 *  Other: reserved
		 * In UV combined data mode (UV component):
		 *  00: V1U1V0U0
		 *  01: U1V1U0V0
		 *  Other: reserved
		 * In interleaved ARGB8888 data mode:
		 *  00: BGRA
		 *  01: ARGB
		 *  Other: reserved
		 */
		u32 data_ps:2; /* Default: 0x0 */
		u32 res0:2;
		/*
		 * DATA_FMT: Input component data format.
		 * In non-macro block planar data mode:
		 *  000: YUV 4:4:4
		 *  001: YUV 4:2:2
		 *  010: YUV 4:2:0
		 *  011: YUV 4:1:1
		 *  100: CSI RGB data
		 *  101: RGB888
		 *  Other: Reserved
		 * In interleaved data mode:
		 *  000: YUV 4:4:4
		 *  001: YUV 4:2:2
		 *  101: ARGB8888
		 *  Other: reserved
		 * In non-macro block UV combined data mode:
		 *  001: YUV 4:2:2
		 *  010: YUV 4:2:0
		 *  011: YUV 4:1:1
		 *  Other: reserved
		 * In macro block planar data mode:
		 *  001: YUV 4:2:2
		 *  010: YUV 4:2:0
		 *  011: YUV 4:1:1
		 *  Other: Reserved
		 * In macro block UV combined data mode:
		 *  001: YUV 4:2:2
		 *  010: YUV 4:2:0
		 *  011: YUV 4:1:1
		 *  Other: reserved
		 */
		u32 data_fmt:3; /* Default: 0 */
		u32 res1:1;
		/*
		 * DATA_MOD: Input data mode selection.
		 *  000: non-macro block planar data
		 *  001: interleaved data
		 *  010: non-macro block UV combined data
		 *  100: macro block planar data
		 *  110: macro block UV combined data
		 *  other: reserved
		 */
		u32 data_mod:3; /* Default: 0 */
		u32 res2:1;
		/*
		 * SCAN_MOD: Scanning Mode selection.
		 * 0: non-interlace
		 * 1: interlace
		 */
		u32 scan_mod:1; /* Default: 0 */
		u32 res3:3;
		/*
		 *  BYTE_SEQ: Input data byte sequence selection.
		 * 0: P3P2P1P0(word)
		 * 1: P0P1P2P3(word)
		 */
		u32 byte_seq:1; /* Default: 0 */
		u32 res4:15;
	} bits;
} SCAL_INPUT_FMT_REG;

typedef union {
	u32 dwval;
	struct {
		u32 res0:7;
		/*
		 * WB_EN: Write-back end interrupt enable.
		 * 0: Disable
		 * 1: Enable
		 */
		u32 wb_en:1; /* Default: 0 */
		u32 res1:1;
		/* LINE_EN: Line interrupt enable */
		u32 line_en:1; /* Default: 0 */
		/* REG_LOAD_EN: Register ready load interrupt enable */
		u32 reg_load_en:1; /* Default: 0 */
		u32 res2:21;
	} bits;
} SCAL_INT_EN_REG;

typedef union {
	u32 dwval;
	struct {
		u32 res0:7;
		/* WB_STATUS: Write-back end interrupt status. */
		u32 wb_status:1; /* Default: 0 */
		u32 res1:1;
		/* LINE_STATUS: Line interrupt status */
		u32 line_status:1; /* Default: 0 */
		/* REG_LOAD_STATUS: Register ready load interrupt status */
		u32 reg_load_status:1; /* Default: 0 */
		u32 res2:21;
	} bits;
} SCAL_INT_STATUS_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * LINE_STRIDE:
		 * In macro block type: The stride length is the distance from
		 *   the start of the end line in one macro block to the start
		 *   of the first line in next macro block (here next macro
		 *   block is in vertical direction).
		 * In no macro block type: The stride length is the distance
		 *   from the start of one line to the start of the next line.
		 */
		u32 line_stride; /* Default: 0 */
	} bits;
} SCAL_LINESTRD0_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * LINE_STRIDE:
		 * In macro block type: The stride length is the distance from
		 *   the start of the end line in one macro block to the start
		 *   of the first line in next macro block (here next macro
		 *   block is in vertical direction).
		 * In no macro block type: The stride length is the distance
		 *   from the start of one line to the start of the next line.
		 */
		u32 line_stride; /* Default: 0 */
	} bits;
} SCAL_LINESTRD1_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * LINE_STRIDE:
		 * In macro block type: The stride length is the distance from
		 *   the start of the end line in one macro block to the start
		 *   of the first line in next macro block (here next macro
		 *   block is in vertical direction).
		 * In no macro block type: The stride length is the distance
		 *   from the start of one line to the start of the next line.
		 */
		u32 line_stride; /* Default: 0 */
	} bits;
} SCAL_LINESTRD2_REG;

typedef union {
	u32 dwval;
	struct {
		/* TRIG_LINE: Trigger line number of line interrupt */
		u32 trig_line:13; /* Default: 0 */
		u32 res0:2;
		/*
		 * FIELD_SEL: Field select.
		 * 0: each field
		 * 1: end field (field counter in reg0x2c)
		 */
		u32 field_sel:1; /* Default: 0 */
		/* CURRENT_LINE */
		u32 current_line:12; /* Default: 0x0 */
		u32 res1:4;
	} bits;
} SCAL_LINT_CTRL_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * X_OFFSET0: The x offset of the top-left point in the first
		 * macro block.
		 */
		u32 x_offset0:5; /* Default: 0 */
		u32 res0:3;
		/*
		 * Y_OFFSET0: The y offset of the top-left point in the first
		 * macro block.
		 */
		u32 y_offset0:5; /* Default: 0 */
		u32 res1:3;
		/*
		 * X_OFFSET1: The x offset of the bottom-right point in the end
		 * macro block.
		 */
		u32 x_offset1:5; /* Default: 0 */
		u32 res2:11;
	} bits;
} SCAL_MB_OFF0_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * X_OFFSET0: The x offset of the top-left point in the first
		 * macro block.
		 */
		u32 x_offset0:5; /* Default: 0 */
		u32 res0:3;
		/*
		 * Y_OFFSET0: The y offset of the top-left point in the first
		 * macro block.
		 */
		u32 y_offset0:5; /* Default: 0 */
		u32 res1:3;
		/*
		 * X_OFFSET1: The x offset of the bottom-right point in the end
		 * macro block.
		 */
		u32 x_offset1:5; /* Default: 0 */
		u32 res2:11;
	} bits;
} SCAL_MB_OFF1_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * X_OFFSET0: The x offset of the top-left point in the first
		 * macro block.
		 */
		u32 x_offset0:5; /* Default: 0 */
		u32 res0:3;
		/*
		 * Y_OFFSET0: The y offset of the top-left point in the first
		 * macro block.
		 */
		u32 y_offset0:5; /* Default: 0 */
		u32 res1:3;
		/*
		 * X_OFFSET1: The x offset of the bottom-right point in the end
		 * macro block.
		 */
		u32 x_offset1:5; /* Default: 0 */
		u32 res2:11;
	} bits;
} SCAL_MB_OFF2_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * DATA_FMT: Data format.
		 * 000: planar RGB888 conversion data format,
		 * 001: interleaved BGRA8888 conversion data format (A component
		 *      always be pad 0xff).
		 * 010: interleaved ARGB8888 conversion data format (A component
		 *       always be pad 0xff)
		 * 100: planar YUV 444
		 * 101: planar YUV 420 (only support YUV input and not
		 *      interleaved mode)
		 * 110: planar YUV 422 (only support YUV input)
		 * 111: planar YUV 411 (only support YUV input)
		 * Other: reserved
		 */
		u32 data_fmt:3; /* Default: 0 */
		u32 res0:1;
		/*
		 * SCAN_MOD: Output interlace enable.
		 *  0: disable
		 *  1: enable
		 * When output interlace enable, scaler selects YUV initial
		 * phase according to LCD field signal.
		 */
		u32 scan_mod:1; /* Default: 0 */
		u32 res1:3;
		/*
		 * BYTE_SEQ: Output data byte sequence selection.
		 * 0: P3P2P1P0(word)
		 * 1: P0P1P2P3(word)
		 * For ARGB, when this bit is 0, the byte sequence is BGRA, and
		 * when this bit is 1, the byte sequence is ARGB.
		 */
		u32 byte_seq:1; /* Default: 0 */
#ifdef CONFIG_ARCH_SUN4I
		u32 res2:23;
#else
		u32 res2:7;
		/* write back channel select */
		u32 wb_chsel:2;
		u32 res3:14;
#endif
	} bits;
} SCAL_OUTPUT_FMT_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * FRM_BUSY: Frame busy.
		 * This flag indicates that the frame is being processed.
		 * The bit will be set when frame process reset & start is set,
		 * and be cleared when frame process reset or disabled.
		 */
		u32 frm_busy:1; /* Default: 0 */
		/*
		 * WB_STATUS: Write-back process status.
		 *  0: write-back end or write-back disable
		 *  1: write-back in process
		 * This flag indicates that a full frame has not been written
		 * back to memory. The bit will be set when write-back enable
		 * bit is set, and be cleared when write-back process end.
		 */
		u32 wb_status:1; /* Default: 0 */
		/*
		 * CFG_PENDING: Register configuration pending.
		 *  0: no pending.
		 *  1: configuration pending
		 * This bit indicates the registers for the next frame has been
		 * configured. This bit will be set when configuration ready bit
		 * is set and this bit will be cleared when a new frame process
		 * begin.
		 */
		u32 cfg_pending:1; /* Default: 0 */
		u32 res0:1;
		/*
		 * DRAM_STATUS: Access dram status.
		 *  0: idle
		 *  1: busy
		 * This flag indicates whether scaler is accessing dram.
		 */
		u32 dram_status:1; /* Default: 0 */
		/*
		 * LCD_FIELD: LCD field status.
		 *  0: top field
		 *  1: bottom field
		 */
		u32 lcd_field:1; /* Default: 0 */
#ifdef CONFIG_ARCH_SUN4I
		u32 res1:6;
#else
		u32 res1:5;
		/* fir coef access status */
		u32 coef_access_status:1;
#endif
		/*
		 * WB_ERR_STATUS: write-back error status.
		 *  0: valid write back
		 *  1: un-valid write back
		 * This bit is cleared through write 0 to reset/start bit in
		 * frame control register
		 */
		u32 wb_err_status:1; /* Default: 0 */
		u32 res2:1;
		/* WB_ERR_LOSEDATA: Lose data flag when capture in process. */
		u32 wb_err_losedata:1; /* Default: 0 */
		/* WB_ERR_SYNC: Sync reach flag when capture in process. */
		u32 wb_err_sync:1; /* Default: 0 */
		/* LINE_ON_SYNC: Line number when sync reached. */
		u32 line_on_sync:13; /* Default: 0 */
		u32 res3:3;
	} bits;
} SCAL_STATUS_REG;

typedef union {
	u32 dwval;
	struct {
		/* BLE_EN: BLE enable */
		u32 ble_en:1; /* Default: 0 */
		u32 res0:7;
		/* BLE_THR: BLE threshold. Note: MUST BE set [0:127] */
		u32 ble_thr:8; /* Default: 0 */
		/* BLE_GAIN: BLE gain */
		u32 ble_gain:8; /* Default: 0 */
		u32 res1:8;
	} bits;
} SCAL_VPP_BLE_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * DCTI_EN:
		 * 0: Disable
		 * 1: Enable
		 */
		u32 dcti_en:1; /* Default: 0 */
		u32 res0:5;
		/*
		 * DCTI_HILL_EN: DCTI hill protection enable.
		 * 0: Disable
		 * 1: Enable
		 */
		u32 dcti_hill_en:1; /* Default: 0 */
		/*
		 * DCTI_SUPHILL_EN: DCTI super hill protection enable.
		 *  0: Disable
		 *  1: Enable
		 */
		u32 dcti_suphill_en:1; /* Default: 0 */
		/*
		 * DCTI_FILTER1_SEL: DCTI 1st filter algorithm selection.
		 * 00: algorithm0
		 * 01: algorithm1
		 * 10: algorithm2
		 * 11: reserved
		 */
		u32 dcti_filter1_sel:2; /* Default: 0 */
		/*
		 * DCTI_FILTER2_SEL: DCTI 2nd filter algorithm selection.
		 * 00: algorithm0
		 * 01: algorithm1
		 * 10: algorithm2
		 * 11: reserved
		 */
		u32 dcti_filter2_sel:2; /* Default: 0 */
		/* DCTI_PATH_LIMIT: Max path limit equal to 12 */
		u32 dcti_path_limit:4; /* Default: 0 */
		/* DCTI_GAIN */
		u32 dcti_gain:6; /* Default: 0 */
		u32 res1:2;
		/*
		 * UV_DIFF_SIGN_MODE_SEL: UV separate mode in different sign
		 * condition:
		 *  00: Using U always
		 *  01: Using V always
		 *  10: Using 0 always
		 *  11: Using Max/Min mode
		 */
		u32 uv_diff_sign_mode_sel:2; /* Default: 0 */
		/*
		 * UV_SAME_SIGN_MODE_SEL: UV separate mode in same sign
		 * condition:
		 *  00: Using U always
		 *  01: Using V always
		 *  10: Using 0 always
		 *  11: Using Max/Min mode
		 */
		u32 uv_same_sign_mode_sel:2; /* Default: 0 */
		/*
		 * UV_DIFF_SIGN_ MAX/MIN_MODE_SEL: UV direction detection using
		 * max or min of |U|/|V| in different sign condition, when
		 * related separate mode select "Using Max/Min mode" and U/V
		 * path shift are in the different sign, path shift use:
		 * 0: min(|U|,|V|)
		 * 1: max(|U|,|V|)
		 */
		u32 uv_diff_sign_maxmin_mode_sel:1; /* Default: 0 */
		/*
		 * UV_SAME_SIGN_MAX/MIN_MODE_SEL: UV direction detection using
		 * max or min of |U|/|V| in same sign condition, when related
		 * separate mode select "Using Max/Min mode" and U/V path shift
		 * are in the same sign, path shift use:
		 * 0: min(|U|,|V|)
		 * 1: max(|U|,|V|)
		 */
		u32 uv_same_sign_maxmin_mode_sel:1; /* Default: 0 */
		u32 res2:1;
		/*
		 * UV_SEPARATE_EN: UV separate enable.
		 * 0: U/V will be under direction detection control
		 * 1: U/V wont be under direction detection control
		 */
		u32 uv_separate_en:1; /* Default: 0 */
	} bits;
} SCAL_VPP_DCTI_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * EN: VPP enable.
		 *  0: Disable
		 *  1: Enable
		 */
		u32 en:1; /* Default: 0 */
		u32 res0:31;
	} bits;
} SCAL_VPP_EN_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * LP_EN:
		 * 0: Disable
		 * 1: Enable
		 */
		u32 lp_en:1; /* Default: 0 */
		u32 res0:7;
		/* TAU: LP band-pass filter1 gain. */
		u32 tau:5; /* Default: 0 */
		u32 res1:3;
		/* ALPHA: LP band-pass filter2 gain. */
		u32 alpha:5; /* Default: 0 */
		u32 res2:3;
		/* BETA: LP high-pass filter gain. */
		u32 beta:5; /* Default: 0 */
		u32 res3:3;
	} bits;
} SCAL_VPP_LP1_REG;

typedef union {
	u32 dwval;
	struct {
		/* LPF_GAIN: LP low-pass-filter gain. */
		u32 lpf_gain:5; /* Default: 0 */
		u32 res0:3;
		/* CPRTHR: LP coring threshold */
		u32 corthr:8; /* Default: 0 */
		/*
		 * NEGGAIN: LP LUT selection for undershot.
		 * 00: NEGGAIN0
		 * 01: NEGGAIN025
		 * 10: NEGGAIN05
		 * 11: NEGGAIN1
		 */
		 u32 neggain:2; /* Default: 0 */
		u32 res1:4;
		/*
		 * DELTA: LP LUT selection for overshoot.
		 * 00: DELTA0
		 * 01: DELTA025
		 * 10: DELTA05
		 * 11: DELTA1
		 */
		u32 delta:2; /* Default: 0 */
		/* LIMIT_THR: LP limit threshold. */
		u32 limit_thr:8; /* Default: 0 */
	} bits;
} SCAL_VPP_LP2_REG;

typedef union {
	u32 dwval;
	struct {
		/* WLE_EN: WLE enable. */
		u32 wle_en:1; /* Default: 0 */
		u32 res0:7;
		/* WLE_THR: WLE threshold. Note: MUST BE set [128-255] */
		u32 wle_thr:8; /* Default: 0 */
		/* WLE_GAIN: WLE gain. */
		u32 wle_gain:8; /* Default: 0 */
		u32 res1:8;
	} bits;
} SCAL_VPP_WLE_REG;

typedef union {
	u32 dwval;
	struct {
		/* WB_ADDR: Write-back address setting for scaled data. */
		u32 wb_addr; /* Default: 0 */
	} bits;
} SCAL_WB_ADDR0_REG;

typedef union {
	u32 dwval;
	struct {
		/* WB_ADDR: Write-back address setting for scaled data. */
		u32 wb_addr; /* Default: 0 */
	} bits;
} SCAL_WB_ADDR1_REG;

typedef union {
	u32 dwval;
	struct {
		/* WB_ADDR: Write-back address setting for scaled data. */
		u32 wb_addr; /* Default: 0 */
	} bits;
} SCAL_WB_ADDR2_REG;

typedef union {
	u32 dwval;
	struct {
		/* LINE_STRD: Ch3 write back line-stride. */
		u32 line_strd; /* Default: 0 */
	} bits;
} SCAL_WB_LINESTRD0_REG;

typedef union {
	u32 dwval;
	struct {
		/* LINE_STRD: Ch4 write back line-stride. */
		u32 line_strd; /* Default: 0 */
	} bits;
} SCAL_WB_LINESTRD1_REG;

typedef union {
	u32 dwval;
	struct {
		/* LINE_STRD: Ch5 write back line-stride. */
		u32 line_strd; /* Default: 0 */
	} bits;
} SCAL_WB_LINESTRD2_REG;

typedef union {
	u32 dwval;
	struct {
		/*
		 * EN: Write back line-stride enable.
		 * 0: disable
		 * 1: enable
		 */
		u32 en:1; /* Default: 0 */
		u32 res0:31;
	} bits;
} SCAL_WB_LINESTRD_EN_REG;

typedef union {
	__u32 dwval;
	struct {
		__u32 res0; /* Default: 0 */
	} bits;
} SCAL_RESERVED_REG;

/*
 * device define
 */
typedef struct __DE_SCAL_DEV {
	SCAL_EN_REG modl_en;				/* 0x00 */
	SCAL_FRM_CTRL_REG frm_ctrl;			/* 0x04 */
	SCAL_BYPASS_REG bypass;				/* 0x08 */
	SCAL_AGTH_SEL_REG agth_sel;			/* 0x0c */
	SCAL_LINT_CTRL_REG lint_ctrl;			/* 0x10 */
	SCAL_RESERVED_REG res0x14[3];			/* 0x14-0x1f */
	SCAL_BUF_ADDR0_REG buf_addr0;			/* 0x20 */
	SCAL_BUF_ADDR1_REG buf_addr1;			/* 0x24 */
	SCAL_BUF_ADDR2_REG buf_addr2;			/* 0x28 */
	SCAL_FIELD_CTRL_REG field_ctrl;			/* 0x2c */
	SCAL_MB_OFF0_REG mb_off0;			/* 0x30 */
	SCAL_MB_OFF1_REG mb_off1;			/* 0x34 */
	SCAL_MB_OFF2_REG mb_off2;			/* 0x38 */
	SCAL_RESERVED_REG res0x3c;			/* 0x3c */
	SCAL_LINESTRD0_REG linestrd0;			/* 0x40 */
	SCAL_LINESTRD1_REG linestrd1;			/* 0x44 */
	SCAL_LINESTRD2_REG linestrd2;			/* 0x48 */
	SCAL_INPUT_FMT_REG input_fmt;			/* 0x4c */
	SCAL_WB_ADDR0_REG wb_addr0;			/* 0x50 */
	SCAL_WB_ADDR1_REG wb_addr1;			/* 0x54 */
	SCAL_WB_ADDR2_REG wb_addr2;			/* 0x58 */
	SCAL_OUTPUT_FMT_REG output_fmt;			/* 0x5c */
	SCAL_INT_EN_REG int_en;				/* 0x60 */
	SCAL_INT_STATUS_REG int_status;			/* 0x64 */
	SCAL_STATUS_REG status;				/* 0x68 */
	SCAL_RESERVED_REG res0x6c;			/* 0x6c */
	SCAL_CSC_COEF03_REG csc_coef[12];		/* 0x70-0x9f */
	SCAL_DI_CTRL_REG di_ctrl;			/* 0xa0 */
	SCAL_DI_DIAGINTP_REG di_diagintp;		/* 0xa4 */
	SCAL_DI_TEMPDIFF_REG di_tempdiff;		/* 0xa8 */
	SCAL_DI_SAWTOOTH_REG di_sawtooth;		/* 0xac */
	SCAL_DI_SPATCOMP_REG di_spatcomp;		/* 0xb0 */
	SCAL_DI_BURSTLEN_REG di_burstlen;		/* 0xb4 */
	SCAL_DI_PRELUMA_REG di_preluma;			/* 0xb8 */
	SCAL_DI_BLKFLAG_REG di_blkflag;			/* 0xbc */
	SCAL_DI_FLAGLINESTRD_REG di_flaglinestrd;	/* 0xc0 */
	SCAL_RESERVED_REG res0xc4[3];			/* 0xc4-0xcf */
	SCAL_WB_LINESTRD_EN_REG wb_linestrd_en;		/* 0xd0 */
	SCAL_WB_LINESTRD0_REG wb_linestrd0;		/* 0xd4 */
	SCAL_WB_LINESTRD1_REG wb_linestrd1;		/* 0xd8 */
	SCAL_WB_LINESTRD2_REG wb_linestrd2;		/* 0xdc */
	SCAL_3D_CTRL_REG trd_ctrl;			/* 0xe0 */
	SCAL_3D_BUF_ADDR0_REG trd_buf_addr0;		/* 0xe4 */
	SCAL_3D_BUF_ADDR1_REG trd_buf_addr1;		/* 0xe8 */
	SCAL_3D_BUF_ADDR2_REG trd_buf_addr2;		/* 0xec */
	SCAL_3D_MB_OFF0_REG trd_mb_off0;		/* 0xf0 */
	SCAL_3D_MB_OFF1_REG trd_mb_off1;		/* 0xf4 */
	SCAL_3D_MB_OFF2_REG trd_mb_off2;		/* 0xf8 */
	SCAL_RESERVED_REG res0xfc;			/* 0xfc */
	SCAL_CH0_INSIZE_REG ch0_insize;			/* 0x100 */
	SCAL_CH0_OUTSIZE_REG ch0_outsize;		/* 0x104 */
	SCAL_CH0_HORZFACT_REG ch0_horzfact;		/* 0x108 */
	SCAL_CH0_VERTFACT_REG ch0_vertfact;		/* 0x10c */
	SCAL_CH0_HORZPHASE_REG ch0_horzphase;		/* 0x110 */
	SCAL_CH0_VERTPHASE0_REG ch0_vertphase0;		/* 0x114 */
	SCAL_CH0_VERTPHASE1_REG ch0_vertphase1;		/* 0x118 */
	SCAL_RESERVED_REG res0x11c;			/* 0x11c */
	SCAL_CH0_HORZTAP0_REG ch0_horztap0;		/* 0x120 */
	SCAL_CH0_HORZTAP1_REG ch0_horztap1;		/* 0x124 */
	SCAL_CH0_VERTTAP_REG ch0_verttap;		/* 0x128 */
	SCAL_RESERVED_REG res0x12c[53];			/* 0x12c-0x1FF */
	SCAL_CH1_INSIZE_REG ch1_insize;			/* 0x200 */
	SCAL_CH1_OUTSIZE_REG ch1_outsize;		/* 0x204 */
	SCAL_CH1_HORZFACT_REG ch1_horzfact;		/* 0x208 */
	SCAL_CH1_VERTFACT_REG ch1_vertfact;		/* 0x20c */
	SCAL_CH1_HORZPHASE_REG ch1_horzphase;		/* 0x210 */
	SCAL_CH1_VERTPHASE0_REG ch1_vertphase0;		/* 0x214 */
	SCAL_CH1_VERTPHASE1_REG ch1_vertphase1;		/* 0x218 */
	SCAL_RESERVED_REG res0x21c;			/* 0x21c */
	SCAL_CH1_HORZTAP0_REG ch1_horztap0;		/* 0x220 */
	SCAL_CH1_HORZTAP1_REG ch1_horztap1;		/* 0x224 */
	SCAL_CH1_VERTTAP_REG ch1_verttap;		/* 0x228 */
	SCAL_RESERVED_REG res0x22c[117];		/* 0x22c-0x3FF */
	SCAL_CH0_HORZCOEF0_REGN ch0_horzcoef0[32];	/* 0x400-0x47f */
	SCAL_CH0_HORZCOEF1_REGN ch0_horzcoef1[32];	/* 0x480-0x4ff */
	SCAL_CH0_VERTCOEF_REGN ch0_vertcoef[32];	/* 0x500-0x5ff */
	SCAL_RESERVED_REG res0x580[32];			/* 0x580-0x57f */
	SCAL_CH1_HORZCOEF0_REGN ch1_horzcoef0[32];	/* 0x600-0x67f */
	SCAL_CH1_HORZCOEF1_REGN ch1_horzcoef1[32];	/* 0x680-6ff */
	SCAL_CH1_VERTCOEF_REGN ch1_vertcoef[32];	/* 0x700-0x77f */
	SCAL_RESERVED_REG res0x780[32];			/* 0x780-0x7ff */
	SCAL_RESERVED_REG res0x800[128];		/* 0x800-0x9ff */
	SCAL_VPP_EN_REG vpp_en;				/* 0xA00 */
	SCAL_VPP_DCTI_REG vpp_dcti;			/* 0xa04 */
	SCAL_VPP_LP1_REG vpp_lp1;			/* 0xa08 */
	SCAL_VPP_LP2_REG vpp_lp2;			/* 0xa0c */
	SCAL_VPP_WLE_REG vpp_wle;			/* 0xa10 */
	SCAL_VPP_BLE_REG vpp_ble;			/* 0xa14 */
} __de_scal_dev_t;

#ifdef CONFIG_ARCH_SUN4I
typedef struct __SCAL_MATRIX4X4 {
	__s64 x00;
	__s64 x01;
	__s64 x02;
	__s64 x03;
	__s64 x10;
	__s64 x11;
	__s64 x12;
	__s64 x13;
	__s64 x20;
	__s64 x21;
	__s64 x22;
	__s64 x23;
	__s64 x30;
	__s64 x31;
	__s64 x32;
	__s64 x33;
} __scal_matrix4x4;

extern __s32 iDE_SCAL_Matrix_Mul(__scal_matrix4x4 in1, __scal_matrix4x4 in2,
				 __scal_matrix4x4 *result);
extern __s32 iDE_SCAL_Csc_Lmt(__s64 *value, __s32 min, __s32 max, __s32 shift,
			      __s32 validbit);
#else
typedef struct __SCAL_MATRIX4X4 {
	__s32 x00;
	__s32 x01;
	__s32 x02;
	__s32 x03;
	__s32 x10;
	__s32 x11;
	__s32 x12;
	__s32 x13;
	__s32 x20;
	__s32 x21;
	__s32 x22;
	__s32 x23;
	__s32 x30;
	__s32 x31;
	__s32 x32;
	__s32 x33;
} __scal_matrix4x4;

extern __s32 iDE_SCAL_Matrix_Mul(__scal_matrix4x4 in1, __scal_matrix4x4 in2,
				 __scal_matrix4x4 *result);
extern __s32 iDE_SCAL_Csc_Lmt(__s32 *value, __s32 min, __s32 max, __s32 shift,
			      __s32 validbit);
#endif /* CONFIG_ARCH_SUN4I */

#endif
