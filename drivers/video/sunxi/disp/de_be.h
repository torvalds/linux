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

#ifndef __DE_BE_H__
#define __DE_BE_H__

#include "ebios_de.h"

/*back-end registers offset*/
#define DE_BE_MODE_CTL_OFF		0x800 /* back-end mode control register offset */
#define DE_BE_COLOR_CTL_OFF		0x804 /* back-end color control register offset */
#define DE_BE_DISP_SIZE_OFF		0x808 /* back-end display size setting register offset */
#define DE_BE_ERROR_CORRECTION		0x80c
#define DE_BE_LAYER_SIZE_OFF		0x810 /* back-end layer size register offset */
#define DE_BE_LAYER_CRD_CTL_OFF		0x820 /* back-end layer coordinate control register offset */
#define DE_BE_FRMBUF_WLINE_OFF		0x840 /* back-end frame buffer line width register offset */
#define DE_BE_FRMBUF_LOW32ADDR_OFF	0X850 /* back-end frame buffer low 32bit address register offset */
#define DE_BE_FRMBUF_HIGH4ADDR_OFF	0X860 /* back-end frame buffer high 4bit address register offset */
#define DE_BE_FRMBUF_CTL_OFF		0X870 /* back-end frame buffer control register offset */
#define DE_BE_CLRKEY_MAX_OFF		0x880 /* back-end color key max register offset */
#define DE_BE_CLRKEY_MIN_OFF		0x884 /* back-end color key min register offset */
#define DE_BE_CLRKEY_CFG_OFF		0x888 /* back-end color key configuration register offset */
#define DE_BE_LAYER_ATTRCTL_OFF0	0x890 /* back-end layer attribute control register0 offset */
#define DE_BE_LAYER_ATTRCTL_OFF1	0x8a0 /* back-end layer attribute control register1 offset */
#define DE_BE_DLCDP_CTL_OFF		0x8b0 /* direct lcd pipe control register offset */
#define DE_BE_DLCDP_FRMBUF_ADDRCTL_OFF	0x8b4 /* direct lcd pipe frame buffer address control register offset */
#define DE_BE_DLCDP_CRD_CTL_OFF0	0x8b8 /* direct lcd pipe coordinate control register0 offset */
#define DE_BE_DLCDP_CRD_CTL_OFF1	0x8bc /* direct lcd pipe coordinate control register1 offset */
#define DE_BE_INT_EN_OFF		0x8c0
#define DE_BE_INT_FLAG_OFF		0x8c4
#define DE_BE_HWC_CRD_CTL_OFF		0x8d8 /* hardware cursor coordinate control register offset */
#define DE_BE_HWC_FRMBUF_OFF		0x8e0 /* hardware cursor framebuffer control */
#define DE_BE_WB_CTRL_OFF		0x8f0 /* back-end write back control */
#define DE_BE_WB_ADDR_OFF		0x8f4 /* back-end write back address */
#define DE_BE_WB_LINE_WIDTH_OFF		0x8f8 /* back-end write back buffer line width */
#define DE_BE_SPRITE_EN_OFF		0x900 /* sprite enable */
#define DE_BE_SPRITE_FORMAT_CTRL_OFF	0x908 /* sprite format control */
#define DE_BE_SPRITE_ALPHA_CTRL_OFF	0x90c /* sprite alpha control */
#define DE_BE_SPRITE_POS_CTRL_OFF	0xa00 /* sprite single block coordinate control */
#define DE_BE_SPRITE_ATTR_CTRL_OFF	0xb00 /* sprite single block attribute control */
#define DE_BE_SPRITE_ADDR_OFF		0xc00 /* sprite single block address setting SRAM array */
#define DE_BE_SPRITE_LINE_WIDTH_OFF	0xd00
#define DE_BE_YUV_CTRL_OFF		0x920 /* back-end input YUV channel control */
#define DE_BE_YUV_ADDR_OFF		0x930 /* back-end YUV channel frame buffer address */
#define DE_BE_YUV_LINE_WIDTH_OFF	0x940 /* back-end YUV channel buffer line width */
#define DE_BE_YG_COEFF_OFF		0x950 /* back Y/G coefficient */
#define DE_BE_YG_CONSTANT_OFF		0x95c /* back Y/G constant */
#define DE_BE_UR_COEFF_OFF		0x960 /* back U/R coefficient */
#define DE_BE_UR_CONSTANT_OFF		0x96c /* back U/R constant */
#define DE_BE_VB_COEFF_OFF		0x970 /* back V/B coefficient */
#define DE_BE_VB_CONSTANT_OFF		0x97c /* back V/B constant */
#define DE_BE_OUT_COLOR_CTRL_OFF	0x9c0
#define DE_BE_OUT_COLOR_R_COEFF_OFF	0x9d0
#define DE_BE_OUT_COLOR_R_CONSTANT_OFF	0x9dc
#define DE_BE_OUT_COLOR_G_COEFF_OFF	0x9e0
#define DE_BE_OUT_COLOR_G_CONSTANT_OFF	0x9ec
#define DE_BE_OUT_COLOR_B_COEFF_OFF	0x9f0
#define DE_BE_OUT_COLOR_B_CONSTANT_OFF	0x9fc

#define DE_BE_REG_ADDR_OFF			0x0
#define DE_BE_HWC_PALETTE_TABLE_ADDR_OFF	0x4c00 /* back-end hardware cursor palette table address */
#define DE_BE_INTER_PALETTE_TABLE_ADDR_OFF	0x5000 /* back-end internal framebuffer or direct lcd pipe palette table */
#define DE_BE_SPRITE_PALETTE_TABLE_ADDR_OFF	0x4000 /* back-end sprite palette table address */
#define DE_BE_HWC_MEMORY_ADDR_OFF		0x4800 /* back-end hwc pattern memory block address */
#define DE_BE_INTERNAL_FB_ADDR_OFF		0x4000 /* back-end internal frame bufffer address definition */
#define DE_BE_GAMMA_TABLE_ADDR_OFF		0x4400 /* back-end gamma table address */
#define DE_BE_PALETTE_TABLE_ADDR_OFF		0x5000 /* back-end palette table address */
#define DE_FE_REG_ADDR_OFF			0x20000
#define DE_SCAL2_REG_ADDR_OFF			0x40000

#define DE_BE_REG_SIZE			0x1000
#define DE_BE_HWC_PALETTE_TABLE_SIZE	0x400 /* back-end hardware cursor palette table size */
#define DE_BE_INTER_PALETTE_TABLE_SIZE	0x400 /* back-end internal framebuffer or direct lcd pipe palette table size in bytes */
#define DE_BE_SPRITE_PALETTE_TABLE_SIZE	0x400 /* back-end sprite palette table size in bytes */
#define DE_BE_HWC_PATTERN_SIZE		0x400
#define DE_BE_INTERNAL_FB_SIZE		0x1800 /* back-end internal frame buffer size in byte*/
#define DE_BE_GAMMA_TABLE_SIZE		0x400 /* back-end gamma table size */
#define DE_BE_PALETTE_TABLE_SIZE	0x400 /* back-end palette table size in bytes */
#define DE_FE_REG_SIZE			0x1000
#define DE_SCAL2_REG_SIZE		0x1000

extern __u32 image_reg_base[2];
#define DE_BE_GET_REG_BASE(sel)(image_reg_base[sel])

#define DE_WUINT8(offset,value) (*((volatile __u8 *)(offset))=(value))
#define DE_RUINT8(offset)  (*((volatile __u8 *)(offset)))
#define DE_WUINT16(offset,value)(*((volatile __u16 *)(offset))=(value))
#define DE_RUINT16(offset) (*((volatile __u16 *)(offset)))
#define DE_WUINT32(offset,value)(*((volatile __u32 *)(offset))=(value))
#define DE_RUINT32(offset) (*((volatile __u32 *)(offset)))
#define DE_WUINT8IDX(offset,index,value)((*((volatile __u8 *)(offset+index)))=(value))
#define DE_RUINT8IDX(offset,index) (*((volatile __u8 *)(offset+index)))
#define DE_WUINT16IDX(offset,index,value)  (*((volatile __u16 *)(offset+2*index))=(value))
#define DE_RUINT16IDX(offset,index) ( *((volatile __u16 *)(offset+2*index)))
#define DE_WUINT32IDX(offset,index,value)  (*((volatile __u32 *)(offset+4*index))=(value))
#define DE_RUINT32IDX(offset,index) (*((volatile __u32 *)(offset+4*index)))

#define DE_BE_WUINT8(sel,offset,value)DE_WUINT8(DE_BE_GET_REG_BASE(sel)+(offset),value)
#define DE_BE_RUINT8(sel,offset) DE_RUINT8(DE_BE_GET_REG_BASE(sel)+(offset))
#define DE_BE_WUINT16(sel,offset,value) DE_WUINT16(DE_BE_GET_REG_BASE(sel)+(offset),value)
#define DE_BE_RUINT16(sel,offset) DE_RUINT16(DE_BE_GET_REG_BASE(sel)+(offset))
#define DE_BE_WUINT32(sel,offset,value) DE_WUINT32(DE_BE_GET_REG_BASE(sel)+(offset),value)
#define DE_BE_RUINT32(sel,offset) DE_RUINT32(DE_BE_GET_REG_BASE(sel)+(offset))
#define DE_BE_WUINT8IDX(sel,offset,index,value) DE_WUINT8IDX(DE_BE_GET_REG_BASE(sel)+(offset),index,value)
#define DE_BE_RUINT8IDX(sel,offset,index) DE_RUINT8IDX(DE_BE_GET_REG_BASE(sel)+(offset),index)
#define DE_BE_WUINT16IDX(sel,offset,index,value) DE_WUINT16IDX(DE_BE_GET_REG_BASE(sel)+(offset),index,value)
#define DE_BE_RUINT16IDX(sel,offset,index) DE_RUINT16IDX(DE_BE_GET_REG_BASE(sel)+(offset),index)
#define DE_BE_WUINT32IDX(sel,offset,index,value) DE_WUINT32IDX(DE_BE_GET_REG_BASE(sel)+(offset),index,value)
#define DE_BE_RUINT32IDX(sel,offset,index) DE_RUINT32IDX(DE_BE_GET_REG_BASE(sel)+(offset),index)

extern __u32 csc_tab[192];
extern __u32 image_enhance_tab[224];

#ifdef CONFIG_ARCH_SUN4I
#define FIR_TAB_SIZE 1792
#else
#define FIR_TAB_SIZE 672
#endif
extern __u32 fir_tab[FIR_TAB_SIZE];

#endif
