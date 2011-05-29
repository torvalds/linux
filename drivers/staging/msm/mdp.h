/* Copyright (c) 2008-2010, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef MDP_H
#define MDP_H

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/hrtimer.h>
#include "msm_mdp.h"

#include <mach/hardware.h>
#include <linux/io.h>

#include <asm/system.h>
#include <asm/mach-types.h>

#include "msm_fb_panel.h"

#ifdef CONFIG_MDP_PPP_ASYNC_OP
#include "mdp_ppp_dq.h"
#endif

#ifdef BIT
#undef BIT
#endif

#define BIT(x)  (1<<(x))

#define MDPOP_NOP               0
#define MDPOP_LR                BIT(0)	/* left to right flip */
#define MDPOP_UD                BIT(1)	/* up and down flip */
#define MDPOP_ROT90             BIT(2)	/* rotate image to 90 degree */
#define MDPOP_ROT180            (MDPOP_UD|MDPOP_LR)
#define MDPOP_ROT270            (MDPOP_ROT90|MDPOP_UD|MDPOP_LR)
#define MDPOP_ASCALE            BIT(7)
#define MDPOP_ALPHAB            BIT(8)	/* enable alpha blending */
#define MDPOP_TRANSP            BIT(9)	/* enable transparency */
#define MDPOP_DITHER            BIT(10)	/* enable dither */
#define MDPOP_SHARPENING	BIT(11) /* enable sharpening */
#define MDPOP_BLUR		BIT(12) /* enable blur */
#define MDPOP_FG_PM_ALPHA       BIT(13)

struct mdp_table_entry {
	uint32_t reg;
	uint32_t val;
};

extern struct mdp_ccs mdp_ccs_yuv2rgb ;
extern struct mdp_ccs mdp_ccs_rgb2yuv ;

/*
 * MDP Image Structure
 */
typedef struct mdpImg_ {
	uint32 imgType;		/* Image type */
	uint32 *bmy_addr;	/* bitmap or y addr */
	uint32 *cbcr_addr;	/* cbcr addr */
	uint32 width;		/* image width */
	uint32 mdpOp;		/* image opertion (rotation,flip up/down, alpha/tp) */
	uint32 tpVal;		/* transparency color */
	uint32 alpha;		/* alpha percentage 0%(0x0) ~ 100%(0x100) */
	int    sp_value;        /* sharpening strength */
} MDPIMG;

#ifdef CONFIG_MDP_PPP_ASYNC_OP
#define MDP_OUTP(addr, data)	mdp_ppp_outdw((uint32_t)(addr),	\
					 (uint32_t)(data))
#else
#define MDP_OUTP(addr, data) outpdw((addr), (data))
#endif

#define MDP_KTIME2USEC(kt) (kt.tv.sec*1000000 + kt.tv.nsec/1000)

#define MDP_BASE msm_mdp_base

typedef enum {
	MDP_BC_SCALE_POINT2_POINT4,
	MDP_BC_SCALE_POINT4_POINT6,
	MDP_BC_SCALE_POINT6_POINT8,
	MDP_BC_SCALE_POINT8_1,
	MDP_BC_SCALE_UP,
	MDP_PR_SCALE_POINT2_POINT4,
	MDP_PR_SCALE_POINT4_POINT6,
	MDP_PR_SCALE_POINT6_POINT8,
	MDP_PR_SCALE_POINT8_1,
	MDP_PR_SCALE_UP,
	MDP_SCALE_BLUR,
	MDP_INIT_SCALE
} MDP_SCALE_MODE;

typedef enum {
	MDP_BLOCK_POWER_OFF,
	MDP_BLOCK_POWER_ON
} MDP_BLOCK_POWER_STATE;

typedef enum {
	MDP_MASTER_BLOCK,
	MDP_CMD_BLOCK,
	MDP_PPP_BLOCK,
	MDP_DMA2_BLOCK,
	MDP_DMA3_BLOCK,
	MDP_DMA_S_BLOCK,
	MDP_DMA_E_BLOCK,
	MDP_OVERLAY0_BLOCK,
	MDP_OVERLAY1_BLOCK,
	MDP_MAX_BLOCK
} MDP_BLOCK_TYPE;

/* Let's keep Q Factor power of 2 for optimization */
#define MDP_SCALE_Q_FACTOR 512

#ifdef CONFIG_FB_MSM_MDP31
#define MDP_MAX_X_SCALE_FACTOR (MDP_SCALE_Q_FACTOR*8)
#define MDP_MIN_X_SCALE_FACTOR (MDP_SCALE_Q_FACTOR/8)
#define MDP_MAX_Y_SCALE_FACTOR (MDP_SCALE_Q_FACTOR*8)
#define MDP_MIN_Y_SCALE_FACTOR (MDP_SCALE_Q_FACTOR/8)
#else
#define MDP_MAX_X_SCALE_FACTOR (MDP_SCALE_Q_FACTOR*4)
#define MDP_MIN_X_SCALE_FACTOR (MDP_SCALE_Q_FACTOR/4)
#define MDP_MAX_Y_SCALE_FACTOR (MDP_SCALE_Q_FACTOR*4)
#define MDP_MIN_Y_SCALE_FACTOR (MDP_SCALE_Q_FACTOR/4)
#endif

/* SHIM Q Factor */
#define PHI_Q_FACTOR          29
#define PQF_PLUS_5            (PHI_Q_FACTOR + 5)	/* due to 32 phases */
#define PQF_PLUS_4            (PHI_Q_FACTOR + 4)
#define PQF_PLUS_2            (PHI_Q_FACTOR + 2)	/* to get 4.0 */
#define PQF_MINUS_2           (PHI_Q_FACTOR - 2)	/* to get 0.25 */
#define PQF_PLUS_5_PLUS_2     (PQF_PLUS_5 + 2)
#define PQF_PLUS_5_MINUS_2    (PQF_PLUS_5 - 2)

#define MDP_CONVTP(tpVal) (((tpVal&0xF800)<<8)|((tpVal&0x7E0)<<5)|((tpVal&0x1F)<<3))

#define MDPOP_ROTATION (MDPOP_ROT90|MDPOP_LR|MDPOP_UD)
#define MDP_CHKBIT(val, bit) ((bit) == ((val) & (bit)))

/* overlay interface API defines */
typedef enum {
	MORE_IBUF,
	FINAL_IBUF,
	COMPLETE_IBUF
} MDP_IBUF_STATE;

struct mdp_dirty_region {
	__u32 xoffset;		/* source origin in the x-axis */
	__u32 yoffset;		/* source origin in the y-axis */
	__u32 width;		/* number of pixels in the x-axis */
	__u32 height;		/* number of pixels in the y-axis */
};

/*
 * MDP extended data types
 */
typedef struct mdp_roi_s {
	uint32 x;
	uint32 y;
	uint32 width;
	uint32 height;
	int32 lcd_x;
	int32 lcd_y;
	uint32 dst_width;
	uint32 dst_height;
} MDP_ROI;

typedef struct mdp_ibuf_s {
	uint8 *buf;
	uint32 bpp;
	uint32 ibuf_type;
	uint32 ibuf_width;
	uint32 ibuf_height;

	MDP_ROI roi;
	MDPIMG mdpImg;

	int32 dma_x;
	int32 dma_y;
	uint32 dma_w;
	uint32 dma_h;

	uint32 vsync_enable;
	uint32 visible_swapped;
} MDPIBUF;

struct mdp_dma_data {
	boolean busy;
	boolean waiting;
	struct mutex ov_mutex;
	struct semaphore mutex;
	struct completion comp;
};

#define MDP_CMD_DEBUG_ACCESS_BASE   (MDP_BASE+0x10000)

#define MDP_DMA2_TERM 0x1
#define MDP_DMA3_TERM 0x2
#define MDP_PPP_TERM 0x4
#define MDP_DMA_S_TERM 0x8
#ifdef CONFIG_FB_MSM_MDP40
#define MDP_DMA_E_TERM 0x10
#define MDP_OVERLAY0_TERM 0x20
#define MDP_OVERLAY1_TERM 0x40
#endif

#define ACTIVE_START_X_EN BIT(31)
#define ACTIVE_START_Y_EN BIT(31)
#define ACTIVE_HIGH 0
#define ACTIVE_LOW 1
#define MDP_DMA_S_DONE  BIT(2)
#define LCDC_FRAME_START    BIT(15)
#define LCDC_UNDERFLOW      BIT(16)

#ifdef CONFIG_FB_MSM_MDP22
#define MDP_DMA_P_DONE 	BIT(2)
#else
#define MDP_DMA_P_DONE 	BIT(14)
#endif

#define MDP_PPP_DONE 				BIT(0)
#define TV_OUT_DMA3_DONE    BIT(6)
#define TV_ENC_UNDERRUN     BIT(7)
#define TV_OUT_DMA3_START   BIT(13)
#define MDP_HIST_DONE       BIT(20)

#ifdef CONFIG_FB_MSM_MDP22
#define MDP_ANY_INTR_MASK (MDP_PPP_DONE| \
			MDP_DMA_P_DONE| \
			TV_ENC_UNDERRUN)
#else
#define MDP_ANY_INTR_MASK (MDP_PPP_DONE| \
			MDP_DMA_P_DONE| \
			MDP_DMA_S_DONE| \
			LCDC_UNDERFLOW| \
			MDP_HIST_DONE| \
			TV_ENC_UNDERRUN)
#endif

#define MDP_TOP_LUMA       16
#define MDP_TOP_CHROMA     0
#define MDP_BOTTOM_LUMA    19
#define MDP_BOTTOM_CHROMA  3
#define MDP_LEFT_LUMA      22
#define MDP_LEFT_CHROMA    6
#define MDP_RIGHT_LUMA     25
#define MDP_RIGHT_CHROMA   9

#define CLR_G 0x0
#define CLR_B 0x1
#define CLR_R 0x2
#define CLR_ALPHA 0x3

#define CLR_Y  CLR_G
#define CLR_CB CLR_B
#define CLR_CR CLR_R

/* from lsb to msb */
#define MDP_GET_PACK_PATTERN(a,x,y,z,bit) (((a)<<(bit*3))|((x)<<(bit*2))|((y)<<bit)|(z))

/*
 * 0x0000 0x0004 0x0008 MDP sync config
 */
#ifdef CONFIG_FB_MSM_MDP22
#define MDP_SYNCFG_HGT_LOC 22
#define MDP_SYNCFG_VSYNC_EXT_EN BIT(21)
#define MDP_SYNCFG_VSYNC_INT_EN BIT(20)
#else
#define MDP_SYNCFG_HGT_LOC 21
#define MDP_SYNCFG_VSYNC_EXT_EN BIT(20)
#define MDP_SYNCFG_VSYNC_INT_EN BIT(19)
#define MDP_HW_VSYNC
#endif

/*
 * 0x0018 MDP VSYNC THREASH
 */
#define MDP_PRIM_BELOW_LOC 0
#define MDP_PRIM_ABOVE_LOC 8

/*
 * MDP_PRIMARY_VSYNC_OUT_CTRL
 * 0x0080,84,88 internal vsync pulse config
 */
#define VSYNC_PULSE_EN BIT(31)
#define VSYNC_PULSE_INV BIT(30)

/*
 * 0x008c MDP VSYNC CONTROL
 */
#define DISP0_VSYNC_MAP_VSYNC0 0
#define DISP0_VSYNC_MAP_VSYNC1 BIT(0)
#define DISP0_VSYNC_MAP_VSYNC2 BIT(0)|BIT(1)

#define DISP1_VSYNC_MAP_VSYNC0 0
#define DISP1_VSYNC_MAP_VSYNC1 BIT(2)
#define DISP1_VSYNC_MAP_VSYNC2 BIT(2)|BIT(3)

#define PRIMARY_LCD_SYNC_EN BIT(4)
#define PRIMARY_LCD_SYNC_DISABLE 0

#define SECONDARY_LCD_SYNC_EN BIT(5)
#define SECONDARY_LCD_SYNC_DISABLE 0

#define EXTERNAL_LCD_SYNC_EN BIT(6)
#define EXTERNAL_LCD_SYNC_DISABLE 0

/*
 * 0x101f0 MDP VSYNC Threshold
 */
#define VSYNC_THRESHOLD_ABOVE_LOC 0
#define VSYNC_THRESHOLD_BELOW_LOC 16
#define VSYNC_ANTI_TEAR_EN BIT(31)

/*
 * 0x10004 command config
 */
#define MDP_CMD_DBGBUS_EN BIT(0)

/*
 * 0x10124 or 0x101d4PPP source config
 */
#define PPP_SRC_C0G_8BITS (BIT(1)|BIT(0))
#define PPP_SRC_C1B_8BITS (BIT(3)|BIT(2))
#define PPP_SRC_C2R_8BITS (BIT(5)|BIT(4))
#define PPP_SRC_C3A_8BITS (BIT(7)|BIT(6))

#define PPP_SRC_C0G_6BITS BIT(1)
#define PPP_SRC_C1B_6BITS BIT(3)
#define PPP_SRC_C2R_6BITS BIT(5)

#define PPP_SRC_C0G_5BITS BIT(0)
#define PPP_SRC_C1B_5BITS BIT(2)
#define PPP_SRC_C2R_5BITS BIT(4)

#define PPP_SRC_C3_ALPHA_EN BIT(8)

#define PPP_SRC_BPP_INTERLVD_1BYTES 0
#define PPP_SRC_BPP_INTERLVD_2BYTES BIT(9)
#define PPP_SRC_BPP_INTERLVD_3BYTES BIT(10)
#define PPP_SRC_BPP_INTERLVD_4BYTES (BIT(10)|BIT(9))

#define PPP_SRC_BPP_ROI_ODD_X BIT(11)
#define PPP_SRC_BPP_ROI_ODD_Y BIT(12)
#define PPP_SRC_INTERLVD_2COMPONENTS BIT(13)
#define PPP_SRC_INTERLVD_3COMPONENTS BIT(14)
#define PPP_SRC_INTERLVD_4COMPONENTS (BIT(14)|BIT(13))

/*
 * RGB666 unpack format
 * TIGHT means R6+G6+B6 together
 * LOOSE means R6+2 +G6+2+ B6+2 (with MSB)
 * or 2+R6 +2+G6 +2+B6 (with LSB)
 */
#define PPP_SRC_UNPACK_TIGHT BIT(17)
#define PPP_SRC_UNPACK_LOOSE 0
#define PPP_SRC_UNPACK_ALIGN_LSB 0
#define PPP_SRC_UNPACK_ALIGN_MSB BIT(18)

#define PPP_SRC_FETCH_PLANES_INTERLVD 0
#define PPP_SRC_FETCH_PLANES_PSEUDOPLNR BIT(20)

#define PPP_SRC_WMV9_MODE BIT(21)	/* window media version 9 */

/*
 * 0x10138 PPP operation config
 */
#define PPP_OP_SCALE_X_ON BIT(0)
#define PPP_OP_SCALE_Y_ON BIT(1)

#define PPP_OP_CONVERT_RGB2YCBCR 0
#define PPP_OP_CONVERT_YCBCR2RGB BIT(2)
#define PPP_OP_CONVERT_ON BIT(3)

#define PPP_OP_CONVERT_MATRIX_PRIMARY 0
#define PPP_OP_CONVERT_MATRIX_SECONDARY BIT(4)

#define PPP_OP_LUT_C0_ON BIT(5)
#define PPP_OP_LUT_C1_ON BIT(6)
#define PPP_OP_LUT_C2_ON BIT(7)

/* rotate or blend enable */
#define PPP_OP_ROT_ON BIT(8)

#define PPP_OP_ROT_90 BIT(9)
#define PPP_OP_FLIP_LR BIT(10)
#define PPP_OP_FLIP_UD BIT(11)

#define PPP_OP_BLEND_ON BIT(12)

#define PPP_OP_BLEND_SRCPIXEL_ALPHA 0
#define PPP_OP_BLEND_DSTPIXEL_ALPHA BIT(13)
#define PPP_OP_BLEND_CONSTANT_ALPHA BIT(14)
#define PPP_OP_BLEND_SRCPIXEL_TRANSP (BIT(13)|BIT(14))

#define PPP_OP_BLEND_ALPHA_BLEND_NORMAL 0
#define PPP_OP_BLEND_ALPHA_BLEND_REVERSE BIT(15)

#define PPP_OP_DITHER_EN BIT(16)

#define PPP_OP_COLOR_SPACE_RGB 0
#define PPP_OP_COLOR_SPACE_YCBCR BIT(17)

#define PPP_OP_SRC_CHROMA_RGB 0
#define PPP_OP_SRC_CHROMA_H2V1 BIT(18)
#define PPP_OP_SRC_CHROMA_H1V2 BIT(19)
#define PPP_OP_SRC_CHROMA_420 (BIT(18)|BIT(19))
#define PPP_OP_SRC_CHROMA_COSITE 0
#define PPP_OP_SRC_CHROMA_OFFSITE BIT(20)

#define PPP_OP_DST_CHROMA_RGB 0
#define PPP_OP_DST_CHROMA_H2V1 BIT(21)
#define PPP_OP_DST_CHROMA_H1V2 BIT(22)
#define PPP_OP_DST_CHROMA_420 (BIT(21)|BIT(22))
#define PPP_OP_DST_CHROMA_COSITE 0
#define PPP_OP_DST_CHROMA_OFFSITE BIT(23)

#define PPP_BLEND_CALPHA_TRNASP BIT(24)

#define PPP_OP_BG_CHROMA_RGB 0
#define PPP_OP_BG_CHROMA_H2V1 BIT(25)
#define PPP_OP_BG_CHROMA_H1V2 BIT(26)
#define PPP_OP_BG_CHROMA_420 BIT(25)|BIT(26)
#define PPP_OP_BG_CHROMA_SITE_COSITE 0
#define PPP_OP_BG_CHROMA_SITE_OFFSITE BIT(27)
#define PPP_OP_DEINT_EN BIT(29)

#define PPP_BLEND_BG_USE_ALPHA_SEL      (1 << 0)
#define PPP_BLEND_BG_ALPHA_REVERSE      (1 << 3)
#define PPP_BLEND_BG_SRCPIXEL_ALPHA     (0 << 1)
#define PPP_BLEND_BG_DSTPIXEL_ALPHA     (1 << 1)
#define PPP_BLEND_BG_CONSTANT_ALPHA     (2 << 1)
#define PPP_BLEND_BG_CONST_ALPHA_VAL(x) ((x) << 24)

#define PPP_OP_DST_RGB 0
#define PPP_OP_DST_YCBCR BIT(30)
/*
 * 0x10150 PPP destination config
 */
#define PPP_DST_C0G_8BIT (BIT(0)|BIT(1))
#define PPP_DST_C1B_8BIT (BIT(3)|BIT(2))
#define PPP_DST_C2R_8BIT (BIT(5)|BIT(4))
#define PPP_DST_C3A_8BIT (BIT(7)|BIT(6))

#define PPP_DST_C0G_6BIT BIT(1)
#define PPP_DST_C1B_6BIT BIT(3)
#define PPP_DST_C2R_6BIT BIT(5)

#define PPP_DST_C0G_5BIT BIT(0)
#define PPP_DST_C1B_5BIT BIT(2)
#define PPP_DST_C2R_5BIT BIT(4)

#define PPP_DST_C3A_8BIT (BIT(7)|BIT(6))
#define PPP_DST_C3ALPHA_EN BIT(8)

#define PPP_DST_PACKET_CNT_INTERLVD_2ELEM BIT(9)
#define PPP_DST_PACKET_CNT_INTERLVD_3ELEM BIT(10)
#define PPP_DST_PACKET_CNT_INTERLVD_4ELEM (BIT(10)|BIT(9))
#define PPP_DST_PACKET_CNT_INTERLVD_6ELEM (BIT(11)|BIT(9))

#define PPP_DST_PACK_LOOSE 0
#define PPP_DST_PACK_TIGHT BIT(13)
#define PPP_DST_PACK_ALIGN_LSB 0
#define PPP_DST_PACK_ALIGN_MSB BIT(14)

#define PPP_DST_OUT_SEL_AXI 0
#define PPP_DST_OUT_SEL_MDDI BIT(15)

#define PPP_DST_BPP_2BYTES BIT(16)
#define PPP_DST_BPP_3BYTES BIT(17)
#define PPP_DST_BPP_4BYTES (BIT(17)|BIT(16))

#define PPP_DST_PLANE_INTERLVD 0
#define PPP_DST_PLANE_PLANAR BIT(18)
#define PPP_DST_PLANE_PSEUDOPLN BIT(19)

#define PPP_DST_TO_TV BIT(20)

#define PPP_DST_MDDI_PRIMARY 0
#define PPP_DST_MDDI_SECONDARY BIT(21)
#define PPP_DST_MDDI_EXTERNAL BIT(22)

/*
 * 0x10180 DMA config
 */
#define DMA_DSTC0G_8BITS (BIT(1)|BIT(0))
#define DMA_DSTC1B_8BITS (BIT(3)|BIT(2))
#define DMA_DSTC2R_8BITS (BIT(5)|BIT(4))

#define DMA_DSTC0G_6BITS BIT(1)
#define DMA_DSTC1B_6BITS BIT(3)
#define DMA_DSTC2R_6BITS BIT(5)

#define DMA_DSTC0G_5BITS BIT(0)
#define DMA_DSTC1B_5BITS BIT(2)
#define DMA_DSTC2R_5BITS BIT(4)

#define DMA_PACK_TIGHT                      BIT(6)
#define DMA_PACK_LOOSE                      0
#define DMA_PACK_ALIGN_LSB                  0
/*
 * use DMA_PACK_ALIGN_MSB if the upper 6 bits from 8 bits output
 * from LCDC block maps into 6 pins out to the panel
 */
#define DMA_PACK_ALIGN_MSB                  BIT(7)
#define DMA_PACK_PATTERN_RGB \
       (MDP_GET_PACK_PATTERN(0, CLR_R, CLR_G, CLR_B, 2)<<8)
#define DMA_PACK_PATTERN_BGR \
       (MDP_GET_PACK_PATTERN(0, CLR_B, CLR_G, CLR_R, 2)<<8)
#define DMA_OUT_SEL_AHB                     0
#define DMA_OUT_SEL_LCDC                    BIT(20)
#define DMA_IBUF_FORMAT_RGB888              0
#define DMA_IBUF_FORMAT_xRGB8888_OR_ARGB8888  BIT(26)

#ifdef CONFIG_FB_MSM_MDP22
#define DMA_OUT_SEL_MDDI BIT(14)
#define DMA_AHBM_LCD_SEL_PRIMARY 0
#define DMA_AHBM_LCD_SEL_SECONDARY BIT(15)
#define DMA_IBUF_C3ALPHA_EN BIT(16)
#define DMA_DITHER_EN BIT(17)
#define DMA_MDDI_DMAOUT_LCD_SEL_PRIMARY 0
#define DMA_MDDI_DMAOUT_LCD_SEL_SECONDARY BIT(18)
#define DMA_MDDI_DMAOUT_LCD_SEL_EXTERNAL BIT(19)
#define DMA_IBUF_FORMAT_RGB565 BIT(20)
#define DMA_IBUF_FORMAT_RGB888_OR_ARGB8888 0
#define DMA_IBUF_NONCONTIGUOUS BIT(21)
#else
#define DMA_OUT_SEL_MDDI                    BIT(19)
#define DMA_AHBM_LCD_SEL_PRIMARY            0
#define DMA_AHBM_LCD_SEL_SECONDARY          0
#define DMA_IBUF_C3ALPHA_EN                 0
#define DMA_DITHER_EN                       BIT(24)
#define DMA_MDDI_DMAOUT_LCD_SEL_PRIMARY     0
#define DMA_MDDI_DMAOUT_LCD_SEL_SECONDARY   0
#define DMA_MDDI_DMAOUT_LCD_SEL_EXTERNAL    0
#define DMA_IBUF_FORMAT_RGB565              BIT(25)
#define DMA_IBUF_NONCONTIGUOUS 0
#endif

/*
 * MDDI Register
 */
#define MDDI_VDO_PACKET_DESC  0x5666

#ifdef CONFIG_FB_MSM_MDP40
#define MDP_INTR_ENABLE		(msm_mdp_base + 0x0050)
#define MDP_INTR_STATUS		(msm_mdp_base + 0x0054)
#define MDP_INTR_CLEAR		(msm_mdp_base + 0x0058)
#define MDP_EBI2_LCD0		(msm_mdp_base + 0x0060)
#define MDP_EBI2_LCD1		(msm_mdp_base + 0x0064)
#define MDP_EBI2_PORTMAP_MODE	(msm_mdp_base + 0x0070)

#define MDP_DMA_P_HIST_INTR_STATUS 	(msm_mdp_base + 0x95014)
#define MDP_DMA_P_HIST_INTR_CLEAR 	(msm_mdp_base + 0x95018)
#define MDP_DMA_P_HIST_INTR_ENABLE 	(msm_mdp_base + 0x9501C)
#else
#define MDP_INTR_ENABLE		(msm_mdp_base + 0x0020)
#define MDP_INTR_STATUS		(msm_mdp_base + 0x0024)
#define MDP_INTR_CLEAR		(msm_mdp_base + 0x0028)
#define MDP_EBI2_LCD0		(msm_mdp_base + 0x003c)
#define MDP_EBI2_LCD1		(msm_mdp_base + 0x0040)
#define MDP_EBI2_PORTMAP_MODE	(msm_mdp_base + 0x005c)
#endif

#define MDP_FULL_BYPASS_WORD43  (msm_mdp_base + 0x101ac)

#define MDP_CSC_PFMVn(n)	(msm_mdp_base + 0x40400 + 4 * (n))
#define MDP_CSC_PRMVn(n)	(msm_mdp_base + 0x40440 + 4 * (n))
#define MDP_CSC_PRE_BV1n(n)	(msm_mdp_base + 0x40500 + 4 * (n))
#define MDP_CSC_PRE_BV2n(n)	(msm_mdp_base + 0x40540 + 4 * (n))
#define MDP_CSC_POST_BV1n(n)	(msm_mdp_base + 0x40580 + 4 * (n))
#define MDP_CSC_POST_BV2n(n)	(msm_mdp_base + 0x405c0 + 4 * (n))

#ifdef CONFIG_FB_MSM_MDP31
#define MDP_CSC_PRE_LV1n(n)	(msm_mdp_base + 0x40600 + 4 * (n))
#define MDP_CSC_PRE_LV2n(n)	(msm_mdp_base + 0x40640 + 4 * (n))
#define MDP_CSC_POST_LV1n(n)	(msm_mdp_base + 0x40680 + 4 * (n))
#define MDP_CSC_POST_LV2n(n)	(msm_mdp_base + 0x406c0 + 4 * (n))
#define MDP_PPP_SCALE_COEFF_LSBn(n)	(msm_mdp_base + 0x50400 + 8 * (n))
#define MDP_PPP_SCALE_COEFF_MSBn(n)	(msm_mdp_base + 0x50404 + 8 * (n))

#define SCALE_D0_SET  0
#define SCALE_D1_SET  BIT(0)
#define SCALE_D2_SET  BIT(1)
#define SCALE_U1_SET  (BIT(0)|BIT(1))

#else
#define MDP_CSC_PRE_LV1n(n)	(msm_mdp_base + 0x40580 + 4 * (n))
#endif

#define MDP_CURSOR_WIDTH 64
#define MDP_CURSOR_HEIGHT 64
#define MDP_CURSOR_SIZE (MDP_CURSOR_WIDTH*MDP_CURSOR_WIDTH*4)

#define MDP_DMA_P_LUT_C0_EN   BIT(0)
#define MDP_DMA_P_LUT_C1_EN   BIT(1)
#define MDP_DMA_P_LUT_C2_EN   BIT(2)
#define MDP_DMA_P_LUT_POST    BIT(4)

void mdp_hw_init(void);
int mdp_ppp_pipe_wait(void);
void mdp_pipe_kickoff(uint32 term, struct msm_fb_data_type *mfd);
void mdp_pipe_ctrl(MDP_BLOCK_TYPE block, MDP_BLOCK_POWER_STATE state,
		   boolean isr);
void mdp_set_dma_pan_info(struct fb_info *info, struct mdp_dirty_region *dirty,
			  boolean sync);
void mdp_dma_pan_update(struct fb_info *info);
void mdp_refresh_screen(unsigned long data);
int mdp_ppp_blit(struct fb_info *info, struct mdp_blit_req *req,
		struct file **pp_src, struct file **pp_dest);
void mdp_lcd_update_workqueue_handler(struct work_struct *work);
void mdp_vsync_resync_workqueue_handler(struct work_struct *work);
void mdp_dma2_update(struct msm_fb_data_type *mfd);
void mdp_config_vsync(struct msm_fb_data_type *);
uint32 mdp_get_lcd_line_counter(struct msm_fb_data_type *mfd);
enum hrtimer_restart mdp_dma2_vsync_hrtimer_handler(struct hrtimer *ht);
void mdp_set_scale(MDPIBUF *iBuf,
		   uint32 dst_roi_width,
		   uint32 dst_roi_height,
		   boolean inputRGB, boolean outputRGB, uint32 *pppop_reg_ptr);
void mdp_init_scale_table(void);
void mdp_adjust_start_addr(uint8 **src0,
			   uint8 **src1,
			   int v_slice,
			   int h_slice,
			   int x,
			   int y,
			   uint32 width,
			   uint32 height, int bpp, MDPIBUF *iBuf, int layer);
void mdp_set_blend_attr(MDPIBUF *iBuf,
			uint32 *alpha,
			uint32 *tpVal,
			uint32 perPixelAlpha, uint32 *pppop_reg_ptr);

int mdp_dma3_on(struct platform_device *pdev);
int mdp_dma3_off(struct platform_device *pdev);
void mdp_dma3_update(struct msm_fb_data_type *mfd);

int mdp_lcdc_on(struct platform_device *pdev);
int mdp_lcdc_off(struct platform_device *pdev);
void mdp_lcdc_update(struct msm_fb_data_type *mfd);
int mdp_hw_cursor_update(struct fb_info *info, struct fb_cursor *cursor);
void mdp_enable_irq(uint32 term);
void mdp_disable_irq(uint32 term);
void mdp_disable_irq_nolock(uint32 term);
uint32_t mdp_get_bytes_per_pixel(uint32_t format);

#ifdef MDP_HW_VSYNC
void mdp_hw_vsync_clk_enable(struct msm_fb_data_type *mfd);
void mdp_hw_vsync_clk_disable(struct msm_fb_data_type *mfd);
#endif

void mdp_dma_s_update(struct msm_fb_data_type *mfd);

/* Added to support flipping */
void mdp_set_offset_info(struct fb_info *info, uint32 address, uint32 interval);

int get_gem_img(struct mdp_img *img, unsigned long *start,
		unsigned long *len);
int get_img(struct mdp_img *img, struct fb_info *info,
		unsigned long *start, unsigned long *len,
		struct file **pp_file);


/*int get_img(struct msmfb_data *img, struct fb_info *info,
	unsigned long *start, unsigned long *len, struct file **pp_file);*/
#endif /* MDP_H */
