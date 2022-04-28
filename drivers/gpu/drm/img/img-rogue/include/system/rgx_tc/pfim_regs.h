/******************************************************************************
@Title          Odin PFIM control register definitions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Odin register defs for PDP-FBDC Interface Module
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
******************************************************************************/
#ifndef _PFIM_REGS_H_
#define _PFIM_REGS_H_

/*
	Register CR_PFIM_NUM_TILES
*/
#define CR_PFIM_NUM_TILES 0x0000
#define CR_PFIM_NUM_TILES_MASK 0x007FFFFFU
#define CR_PFIM_NUM_TILES_SHIFT 0
#define CR_PFIM_NUM_TILES_SIGNED 0

/*
	Register CR_PFIM_TILES_PER_LINE
*/
#define CR_PFIM_TILES_PER_LINE 0x0004
#define CR_PFIM_TILES_PER_LINE_PFIM_TILES_PER_LINE_MASK 0x000000FFU
#define CR_PFIM_TILES_PER_LINE_PFIM_TILES_PER_LINE_SHIFT 0
#define CR_PFIM_TILES_PER_LINE_PFIM_TILES_PER_LINE_SIGNED 0

/*
	Register CR_PFIM_FBDC_YARGB_BASE_ADDR_LSB
*/
#define CR_PFIM_FBDC_YARGB_BASE_ADDR_LSB 0x0008
#define CR_PFIM_FBDC_YARGB_BASE_ADDR_LSB_MASK 0xFFFFFFFFU
#define CR_PFIM_FBDC_YARGB_BASE_ADDR_LSB_SHIFT 0
#define CR_PFIM_FBDC_YARGB_BASE_ADDR_LSB_SIGNED 0

/*
	Register CR_PFIM_FBDC_YARGB_BASE_ADDR_MSB
*/
#define CR_PFIM_FBDC_YARGB_BASE_ADDR_MSB 0x000C
#define CR_PFIM_FBDC_YARGB_BASE_ADDR_MSB_MASK 0x00000003U
#define CR_PFIM_FBDC_YARGB_BASE_ADDR_MSB_SHIFT 0
#define CR_PFIM_FBDC_YARGB_BASE_ADDR_MSB_SIGNED 0

/*
	Register CR_PFIM_FBDC_UV_BASE_ADDR_LSB
*/
#define CR_PFIM_FBDC_UV_BASE_ADDR_LSB 0x0010
#define CR_PFIM_FBDC_UV_BASE_ADDR_LSB_MASK 0xFFFFFFFFU
#define CR_PFIM_FBDC_UV_BASE_ADDR_LSB_SHIFT 0
#define CR_PFIM_FBDC_UV_BASE_ADDR_LSB_SIGNED 0

/*
	Register CR_PFIM_FBDC_UV_BASE_ADDR_MSB
*/
#define CR_PFIM_FBDC_UV_BASE_ADDR_MSB 0x0014
#define CR_PFIM_FBDC_UV_BASE_ADDR_MSB_MASK 0x00000003U
#define CR_PFIM_FBDC_UV_BASE_ADDR_MSB_SHIFT 0
#define CR_PFIM_FBDC_UV_BASE_ADDR_MSB_SIGNED 0

/*
	Register CR_PFIM_PDP_Y_BASE_ADDR
*/
#define CR_PFIM_PDP_Y_BASE_ADDR 0x0018
#define CR_PFIM_PDP_Y_BASE_ADDR_MASK 0xFFFFFFFFU
#define CR_PFIM_PDP_Y_BASE_ADDR_SHIFT 0
#define CR_PFIM_PDP_Y_BASE_ADDR_SIGNED 0

/*
	Register CR_PFIM_PDP_UV_BASE_ADDR
*/
#define CR_PFIM_PDP_UV_BASE_ADDR 0x001C
#define CR_PFIM_PDP_UV_BASE_ADDR_MASK 0xFFFFFFFFU
#define CR_PFIM_PDP_UV_BASE_ADDR_SHIFT 0
#define CR_PFIM_PDP_UV_BASE_ADDR_SIGNED 0

/*
	Register CR_PFIM_FBDC_REQ_CONTEXT
*/
#define CR_PFIM_FBDC_REQ_CONTEXT 0x0020
#define CR_PFIM_FBDC_REQ_CONTEXT_MASK 0x00000007U
#define CR_PFIM_FBDC_REQ_CONTEXT_SHIFT 0
#define CR_PFIM_FBDC_REQ_CONTEXT_SIGNED 0

/*
	Register CR_PFIM_FBDC_REQ_TAG
*/
#define CR_PFIM_FBDC_REQ_TAG 0x0024
#define CR_PFIM_FBDC_REQ_TAG_YARGB_MASK     0x00000003U
#define CR_PFIM_FBDC_REQ_TAG_YARGB_SHIFT    0
#define CR_PFIM_FBDC_REQ_TAG_YARGB_SIGNED   0

#define CR_PFIM_FBDC_REQ_TAG_UV_MASK        0x00000030U
#define CR_PFIM_FBDC_REQ_TAG_UV_SHIFT       4
#define CR_PFIM_FBDC_REQ_TAG_UV_SIGNED      0

/*
	Register CR_PFIM_FBDC_REQ_SB_TAG
*/
#define CR_PFIM_FBDC_REQ_SB_TAG 0x0028
#define CR_PFIM_FBDC_REQ_SB_TAG_YARGB_MASK  0x00000003U
#define CR_PFIM_FBDC_REQ_SB_TAG_YARGB_SHIFT 0
#define CR_PFIM_FBDC_REQ_SB_TAG_YARGB_SIGNED 0

#define CR_PFIM_FBDC_REQ_SB_TAG_UV_MASK     0x00000030U
#define CR_PFIM_FBDC_REQ_SB_TAG_UV_SHIFT    4
#define CR_PFIM_FBDC_REQ_SB_TAG_UV_SIGNED   0

/*
	Register CR_PFIM_FBDC_HDR_INVAL_REQ
*/
#define CR_PFIM_FBDC_HDR_INVAL_REQ 0x002C
#define CR_PFIM_FBDC_HDR_INVAL_REQ_MASK 0x00000001U
#define CR_PFIM_FBDC_HDR_INVAL_REQ_SHIFT 0
#define CR_PFIM_FBDC_HDR_INVAL_REQ_SIGNED 0

/*
	Register CR_PFIM_FBDC_PIX_FORMAT
*/
#define CR_PFIM_FBDC_PIX_FORMAT 0x0030
#define CR_PFIM_FBDC_PIX_FORMAT_FBDC_PIX_FMT_MASK 0x0000007FU
#define CR_PFIM_FBDC_PIX_FORMAT_FBDC_PIX_FMT_SHIFT 0
#define CR_PFIM_FBDC_PIX_FORMAT_FBDC_PIX_FMT_SIGNED 0

/*
	Register CR_PFIM_FBDC_CR_CH0123_VAL0
*/
#define CR_PFIM_FBDC_CR_CH0123_VAL0 0x0034
#define CR_PFIM_FBDC_CR_CH0123_VAL0_MASK 0xFFFFFFFFU
#define CR_PFIM_FBDC_CR_CH0123_VAL0_SHIFT 0
#define CR_PFIM_FBDC_CR_CH0123_VAL0_SIGNED 0

/*
	Register CR_PFIM_FBDC_CR_CH0123_VAL1
*/
#define CR_PFIM_FBDC_CR_CH0123_VAL1 0x0038
#define CR_PFIM_FBDC_CR_CH0123_VAL1_MASK 0xFFFFFFFFU
#define CR_PFIM_FBDC_CR_CH0123_VAL1_SHIFT 0
#define CR_PFIM_FBDC_CR_CH0123_VAL1_SIGNED 0

/*
	Register CR_PFIM_FBDC_CR_Y_VAL0
*/
#define CR_PFIM_FBDC_CR_Y_VAL0 0x003C
#define CR_PFIM_FBDC_CR_Y_VAL0_MASK 0x000003FFU
#define CR_PFIM_FBDC_CR_Y_VAL0_SHIFT 0
#define CR_PFIM_FBDC_CR_Y_VAL0_SIGNED 0

/*
	Register CR_PFIM_FBDC_CR_UV_VAL0
*/
#define CR_PFIM_FBDC_CR_UV_VAL0 0x0040
#define CR_PFIM_FBDC_CR_UV_VAL0_MASK 0x000003FFU
#define CR_PFIM_FBDC_CR_UV_VAL0_SHIFT 0
#define CR_PFIM_FBDC_CR_UV_VAL0_SIGNED 0

/*
	Register CR_PFIM_FBDC_CR_Y_VAL1
*/
#define CR_PFIM_FBDC_CR_Y_VAL1 0x0044
#define CR_PFIM_FBDC_CR_Y_VAL1_MASK 0x000003FFU
#define CR_PFIM_FBDC_CR_Y_VAL1_SHIFT 0
#define CR_PFIM_FBDC_CR_Y_VAL1_SIGNED 0

/*
	Register CR_PFIM_FBDC_CR_UV_VAL1
*/
#define CR_PFIM_FBDC_CR_UV_VAL1 0x0048
#define CR_PFIM_FBDC_CR_UV_VAL1_MASK 0x000003FFU
#define CR_PFIM_FBDC_CR_UV_VAL1_SHIFT 0
#define CR_PFIM_FBDC_CR_UV_VAL1_SIGNED 0

/*
	Register CR_PFIM_FBDC_FILTER_ENABLE
*/
#define CR_PFIM_FBDC_FILTER_ENABLE 0x004C
#define CR_PFIM_FBDC_FILTER_ENABLE_MASK 0x00000001U
#define CR_PFIM_FBDC_FILTER_ENABLE_SHIFT 0
#define CR_PFIM_FBDC_FILTER_ENABLE_SIGNED 0

/*
	Register CR_PFIM_FBDC_FILTER_STATUS
*/
#define CR_PFIM_FBDC_FILTER_STATUS 0x0050
#define CR_PFIM_FBDC_FILTER_STATUS_MASK 0x0000000FU
#define CR_PFIM_FBDC_FILTER_STATUS_SHIFT 0
#define CR_PFIM_FBDC_FILTER_STATUS_SIGNED 0

/*
	Register CR_PFIM_FBDC_FILTER_CLEAR
*/
#define CR_PFIM_FBDC_FILTER_CLEAR 0x0054
#define CR_PFIM_FBDC_FILTER_CLEAR_MASK 0x0000000FU
#define CR_PFIM_FBDC_FILTER_CLEAR_SHIFT 0
#define CR_PFIM_FBDC_FILTER_CLEAR_SIGNED 0

/*
	Register CR_PFIM_FBDC_TILE_TYPE
*/
#define CR_PFIM_FBDC_TILE_TYPE 0x0058
#define CR_PFIM_FBDC_TILE_TYPE_MASK 0x00000003U
#define CR_PFIM_FBDC_TILE_TYPE_SHIFT 0
#define CR_PFIM_FBDC_TILE_TYPE_SIGNED 0

/*
	Register CR_PFIM_FBDC_CLEAR_COLOUR_LSB
*/
#define CR_PFIM_FBDC_CLEAR_COLOUR_LSB 0x005C
#define CR_PFIM_FBDC_CLEAR_COLOUR_LSB_MASK 0xFFFFFFFFU
#define CR_PFIM_FBDC_CLEAR_COLOUR_LSB_SHIFT 0
#define CR_PFIM_FBDC_CLEAR_COLOUR_LSB_SIGNED 0

/*
	Register CR_PFIM_FBDC_CLEAR_COLOUR_MSB
*/
#define CR_PFIM_FBDC_CLEAR_COLOUR_MSB 0x0060
#define CR_PFIM_FBDC_CLEAR_COLOUR_MSB_MASK 0xFFFFFFFFU
#define CR_PFIM_FBDC_CLEAR_COLOUR_MSB_SHIFT 0
#define CR_PFIM_FBDC_CLEAR_COLOUR_MSB_SIGNED 0

/*
	Register CR_PFIM_FBDC_REQ_LOSSY
*/
#define CR_PFIM_FBDC_REQ_LOSSY 0x0064
#define CR_PFIM_FBDC_REQ_LOSSY_MASK 0x00000001U
#define CR_PFIM_FBDC_REQ_LOSSY_SHIFT 0
#define CR_PFIM_FBDC_REQ_LOSSY_SIGNED 0

#endif /* _PFIM_REGS_H_ */

/******************************************************************************
 End of file (pfim_regs.h)
******************************************************************************/
