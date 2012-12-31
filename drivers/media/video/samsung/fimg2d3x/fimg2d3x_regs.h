/* drivers/media/video/samsung/fimg2d3x/fimg2d3x_regs.h
 *
 * Copyright (c) 2010 Samsung Electronics
 *		      http://www.samsungsemi.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.*/

#ifndef __SEC_FIMG2D3X_REGS_H
#define __SEC_FIMG2D3X_REGS_H
 
//**********************************************************
//     Address Definition of SFR
//**********************************************************
#define SEC_G2DREG(x)           ((x))

//**   General Register   *****************
#define CONRTOL_REG             SEC_G2DREG(0x0000)
#define SOFT_RESET_REG          SEC_G2DREG(0x0000)
#define INTEN_REG               SEC_G2DREG(0x0004)
#define INTC_PEND_REG           SEC_G2DREG(0x000C)
#define FIFO_STAT_REG           SEC_G2DREG(0x0010)
#define AXI_ID_MODE_REG         SEC_G2DREG(0x0014)
#define CACHECTL_REG            SEC_G2DREG(0x0018)

//**   G2D Command   *********************
#define BITBLT_START_REG        SEC_G2DREG(0x0100)
#define BITBLT_COMMAND_REG      SEC_G2DREG(0x0104)

//**   Rotation & Direction   *************
#define ROTATE_REG              SEC_G2DREG(0x0200)
#define SRC_MSK_DIRECT_REG      SEC_G2DREG(0x0204)
#define DST_PAT_DIRECT_REG      SEC_G2DREG(0x0208)
// for old vector
#define SRC_DIRECT_REG          SEC_G2DREG(0x0204)
#define DST_DIRECT_REG          SEC_G2DREG(0x0208)

//**   Source    **************************
#define SRC_SELECT_REG          SEC_G2DREG(0x0300)
#define SRC_BASE_ADDR_REG       SEC_G2DREG(0x0304)
#define SRC_STRIDE_REG          SEC_G2DREG(0x0308)
#define SRC_COLOR_MODE_REG      SEC_G2DREG(0x030C)
#define SRC_LEFT_TOP_REG        SEC_G2DREG(0x0310)
#define SRC_RIGHT_BOTTOM_REG    SEC_G2DREG(0x0314)

//**   Destination  ***********************
#define DST_SELECT_REG          SEC_G2DREG(0x0400)
#define DST_BASE_ADDR_REG       SEC_G2DREG(0x0404)
#define DST_STRIDE_REG          SEC_G2DREG(0x0408)
#define DST_COLOR_MODE_REG      SEC_G2DREG(0x040C)
#define DST_LEFT_TOP_REG        SEC_G2DREG(0x0410)
#define DST_RIGHT_BOTTOM_REG    SEC_G2DREG(0x0414)

//**   Pattern   **************************
#define PAT_BASE_ADDR_REG       SEC_G2DREG(0x0500)
#define PAT_SIZE_REG            SEC_G2DREG(0x0504)
#define PAT_COLOR_MODE_REG      SEC_G2DREG(0x0508)
#define PAT_OFFSET_REG          SEC_G2DREG(0x050C)
#define PAT_STRIDE_REG          SEC_G2DREG(0x0510)

//**   Mask   *****************************
#define MASK_BASE_ADDR_REG      SEC_G2DREG(0x0520)
#define MASK_STRIDE_REG         SEC_G2DREG(0x0524)

//**   Clipping Window  *******************
#define CW_LEFT_TOP_REG         SEC_G2DREG(0x0600)
#define	CW_RIGHT_BOTTOM_REG     SEC_G2DREG(0x0604)

//**   ROP4 & Blending   *****************
#define THIRD_OPERAND_REG       SEC_G2DREG(0x0610)
#define ROP4_REG                SEC_G2DREG(0x0614)
#define ALPHA_REG               SEC_G2DREG(0x0618)

//**   Color   ***************************
#define FG_COLOR_REG            SEC_G2DREG(0x0700)
#define BG_COLOR_REG            SEC_G2DREG(0x0704)
#define BS_COLOR_REG            SEC_G2DREG(0x0708)

//**   Color Key   ***********************
#define SRC_COLORKEY_CTRL_REG   SEC_G2DREG(0x0710)
#define SRC_COLORKEY_DR_MIN_REG SEC_G2DREG(0x0714)
#define SRC_COLORKEY_DR_MAX_REG SEC_G2DREG(0x0718)
#define DST_COLORKEY_CTRL_REG   SEC_G2DREG(0x071C)
#define DST_COLORKEY_DR_MIN_REG SEC_G2DREG(0x0720)
#define DST_COLORKEY_DR_MAX_REG SEC_G2DREG(0x0724)

//**********************************************************
//     Bit Definition part
//**********************************************************
 
/* software reset register (SOFT_RESET_REG : 0x0000) */
#define	G2D_SWRESET_R_RESET                     (1<<0)

/* interrupt enable register (INTEN_REG : 0x0004)) */
#define G2D_INTEN_R_INT_TYPE_EDGE               (1<<1)
#define G2D_INTEN_R_INT_TYPE_LEVEL              (0<<1)
#define G2D_INTEN_R_CF_ENABLE                   (1<<0)
#define G2D_INTEN_R_CF_DISABLE                  (0<<0)

/* interrupt pending register (INTC_PEND_REG : 0x000C) */
#define	G2D_INTC_PEND_R_INTP_CMD_FIN            (1<<0)

/* AXI ID mode register (AXI_ID_MODE_REG : 0x0014) */
#define	G2D_AXIID_MODE_R_MULTI_ID               (1<<0)
#define G2D_AXIID_MODE_R_SIGNLE_ID              (0<<0)

/* bitblit start register (BITBLT_START_REG : 0x0100) */
#define	G2D_BITBLT_R_START                      (1<<0)

/* bitblt command register (BITBLT_COMMAND_REG : 0x0104) */
#define	G2D_BLT_CMD_R_COLOR_EXP_CORRECT         (0<<24)
#define	G2D_BLT_CMD_R_COLOR_EXP_ZERO            (1<<24)

#define	G2D_BLT_CMD_R_SRC_NON_PRE_BLEND_DISLABE         (0<<22)
#define G2D_BLT_CMD_R_SRC_NON_PRE_BLEND_CONSTANT_ALPHA  (1<<22)
#define G2D_BLT_CMD_R_SRC_NON_PRE_BLEND_PERPIXEL_ALPHA  (2<<22)

#define G2D_BLT_CMD_R_ALPHA_BLEND_NONE          (0<<20)
#define	G2D_BLT_CMD_R_ALPHA_BLEND_ALPHA_BLEND   (1<<20)
#define G2D_BLT_CMD_R_ALPHA_BLEND_FADE          (2<<20)
// #define G2D_BLT_CMD_R_ALPHA_BLEND_PERPIXEL      (3<<20)

#define G2D_BLT_CMD_R_ALPHA_BLEND_FAD_OFFSET    (8)

#define G2D_BLT_CMD_R_COLOR_KEY_DISABLE         (0<<16)
#define G2D_BLT_CMD_R_COLOR_KEY_ENABLE_SRC      (1<<16)
#define	G2D_BLT_CMD_R_COLOR_KEY_ENABLE_DST      (2<<16)
#define G2D_BLT_CMD_R_COLOP_KEY_ENABLE_SRC_DST  (3<<16)

#define	G2D_BLT_CMD_R_TRANSPARENT_MODE_OPAQUE   (0<<12)
#define	G2D_BLT_CMD_R_TRANSPARENT_MODE_TRANS    (1<<12)
#define	G2D_BLT_CMD_R_TRANSPARENT_MODE_BLUESCR  (2<<12)

#define G2D_BLT_CMD_R_CW_ENABLE              (1<<8)
#define G2D_BLT_CMD_R_STRETCH_ENABLE         (1<<4)
#define	G2D_BLT_CMD_R_MASK_ENABLE            (1<<0)

/* rotation register (ROTATE_REG : 0x02000) */
#define	G2D_ROT_CMD_R_0                      (0<<0)
#define	G2D_ROT_CMD_R_90                     (1<<0)

/* source and mask direction register (SRC_MSK_DIRECT_REG : 0x0204) */
#define	G2D_SRC_MSK_DIR_R_MSK_Y_POSITIVE     (0<<8)
#define	G2D_SRC_MSK_DIR_R_MSK_Y_NEGATIVE     (0<<8)
#define	G2D_SRC_MSK_DIR_R_SRC_Y_POSITIVE     (0<<8)
#define	G2D_SRC_MSK_DIR_R_SRC_Y_POSITIVE     (0<<8)

/* source image selection register (SRC_SELECT_REG : 0x0300) */
#define G2D_SRC_SELECT_R_NORMAL              (0<<0)
#define G2D_SRC_SELECT_R_USE_FG_COLOR        (1<<0)
#define G2D_SRC_SELECT_R_USE_BG_COLOR        (2<<0)

/* source image color mode register (SRC_COLOR_MODE_REG : 0x030C) */


/* destination image selection register (DST_SELECT_REG : 0x0400) */
#define G2D_DST_SELECT_R_NORMAL              (0<<0)
#define G2D_DST_SELECT_R_USE_FG_COLOR        (1<<0)
#define G2D_DST_SELECT_R_USE_BG_COLOR        (2<<0)

#define	G2D_CMD0_REG_M_X                     (1<<8)

#define	G2D_CMD0_REG_L                       (1<<1)
#define	G2D_CMD0_REG_P                       (1<<0)

/* BitBLT */
#define	G2D_CMD1_REG_S                       (1<<1)
#define	G2D_CMD1_REG_N                       (1<<0)

/* resource color mode */
#define G2D_COLOR_MODE_REG_C3_32BPP          (1<<3)
#define G2D_COLOR_MODE_REG_C3_24BPP          (1<<3)
#define G2D_COLOR_MODE_REG_C2_18BPP          (1<<2)
#define G2D_COLOR_MODE_REG_C1_16BPP          (1<<1)
#define G2D_COLOR_MODE_REG_C0_15BPP          (1<<0)

#define G2D_COLOR_RGB_565                    (0x0<<0)
#define G2D_COLOR_RGBA_5551                  (0x1<<0)
#define G2D_COLOR_ARGB_1555                  (0x2<<0)
#define G2D_COLOR_RGBA_8888                  (0x3<<0)
#define G2D_COLOR_ARGB_8888                  (0x4<<0)
#define G2D_COLOR_XRGB_8888                  (0x5<<0)
#define G2D_COLOR_RGBX_8888                  (0x6<<0)
#define G2D_COLOR_YUV422_SP                  (0x1<<3)

#define G2D_CHL_ORDER_XRGB                   (0<<4) // ARGB,XRGB
#define G2D_CHL_ORDER_RGBX                   (1<<4) // RGBA,RGBX
#define G2D_CHL_ORDER_XBGR                   (2<<4) // ABGR,XBGR
#define G2D_CHL_ORDER_BGRX                   (3<<4) // BGRA,BGRX

#define G2D_FMT_XRGB_8888                    (0)
#define G2D_FMT_ARGB_8888                    (1)
#define G2D_FMT_RGB_565                      (2)
#define G2D_FMT_XRGB_1555                    (3)
#define G2D_FMT_ARGB_1555                    (4)
#define G2D_FMT_XRGB_4444                    (5)
#define G2D_FMT_ARGB_4444                    (6)
#define G2D_FMT_PACKED_RGB_888               (7)

/* rotation mode */
#define G2D_ROTATRE_REG_FY                   (1<<5)
#define G2D_ROTATRE_REG_FX                   (1<<4)
#define G2D_ROTATRE_REG_R3_270               (1<<3)
#define G2D_ROTATRE_REG_R2_180               (1<<2)
#define G2D_ROTATRE_REG_R1_90                (1<<1)
#define G2D_ROTATRE_REG_R0_0                 (1<<0)

/* Endian select */
#define G2D_ENDIAN_DST_BIG_ENDIAN            (1<<1)
#define G2D_ENDIAN_DST_LITTLE_ENDIAN         (0<<1)

#define G2D_ENDIAN_SRC_BIG_ENDIAN            (1<<0)
#define G2D_ENDIAN_SRC_LITTLE_ENDIAN         (0<<0)

/* read buffer size */
#define	G2D_ENDIAN_READSIZE_READ_SIZE_1      (0<<0)
#define	G2D_ENDIAN_READSIZE_READ_SIZE_4      (1<<0)
#define	G2D_ENDIAN_READSIZE_READ_SIZE_8      (2<<0)
#define	G2D_ENDIAN_READSIZE_READ_SIZE_16     (3<<0)

/* Third Operans Select */
/*
#define G2D_ROP_REG_OS_PATTERN	             (0<<13)
#define G2D_ROP_REG_OS_FG_COLOR	             (1<<13)
#define G2D_ROP_REG_OS_PATTERN_MASK_SELECT   (0<<4)
#define G2D_ROP_REG_OS_PATTERN_THIRD         (0)
*/
#define G2D_THIRD_OP_REG_PATTERN             ((0<<4) | (0))
#define G2D_THIRD_OP_REG_FG_COLOR            ((1<<4) | (1))
#define G2D_THIRD_OP_REG_BG_COLOR            ((2<<4) | (2))
#define G2D_THIRD_OP_REG_NONE                ((3<<4) | (3))

/* Alpha Blending Mode */
#define G2D_ROP_REG_ABM_NO_BLENDING          (0<<10)
#define G2D_ROP_REG_ABM_SRC_BITMAP           (1<<10)
#define G2D_ROP_REG_ABM_REGISTER             (2<<10)
#define G2D_ROP_REG_ABM_FADING               (4<<10)

/* Raster operation mode */
#define G2D_ROP_REG_T_OPAQUE_MODE            (0<<9)
#define G2D_ROP_REG_T_TRANSP_MODE            (1<<9)

#define G2D_ROP_REG_B_BS_MODE_OFF            (0<<8)
#define G2D_ROP_REG_B_BS_MODE_ON             (1<<8)

/*
#define G2D_ROP_REG_SRC_ONLY                 (0xf0)
#define G2D_ROP_REG_3RD_OPRND_ONLY           (0xaa)
#define G2D_ROP_REG_DST_ONLY                 (0xcc)
#define G2D_ROP_REG_SRC_OR_DST               (0xfc)
#define G2D_ROP_REG_SRC_OR_3RD_OPRND         (0xfa)
#define G2D_ROP_REG_SRC_AND_DST              (0xc0) //(pat==1)? src:dst
#define G2D_ROP_REG_SRC_AND_3RD_OPRND        (0xa0)
#define G2D_ROP_REG_SRC_XOR_3RD_OPRND        (0x5a)
#define G2D_ROP_REG_DST_OR_3RD_OPRND         (0xee)
*/
#define G2D_ROP_REG_SRC                      (0xcc)
#define G2D_ROP_REG_DST                      (0xaa)
#define G2D_ROP_REG_SRC_AND_DST              (0x88) 
#define G2D_ROP_REG_SRC_OR_DST               (0xee)
#define G2D_ROP_REG_3RD_OPRND                (0xf0)
#define G2D_ROP_REG_SRC_AND_3RD_OPRND        (0xc0)
#define G2D_ROP_REG_SRC_OR_3RD_OPRND         (0xfc)
#define G2D_ROP_REG_SRC_XOR_3RD_OPRND        (0x3c)
#define G2D_ROP_REG_DST_OR_3RD_OPRND         (0xfa)


/* stencil control */
#define G2D_STENCIL_CNTL_REG_STENCIL_ON_ON   (1<<31)
#define G2D_STENCIL_CNTL_REG_STENCIL_ON_OFF  (0<<31)

#define G2D_STENCIL_CNTL_REG_STENCIL_INVERSE (1<<23)
#define G2D_STENCIL_CNTL_REG_STENCIL_SWAP    (1<<0)

/*********************************************************************************/

#endif /* __SEC_FIMG2D3X_REGS_H */
