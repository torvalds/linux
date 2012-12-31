/* linux/arch/arm/plat-s5p/include/plat/regs-jpeg.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 * http://www.samsung.com/
 *
 * Register definition file for Samsung JPEG Encoder/Decoder
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARM_REGS_S5P_JPEG_H
#define __ASM_ARM_REGS_S5P_JPEG_H

/* JPEG Registers part */

/* JPEG Codec Control Registers */
#define S5P_JPEG_MOD_REG		0x00
#define S5P_JPEG_OPR_REG		0x04
#define S5P_JPEG_QTBL_REG		0x08
#define S5P_JPEG_HTBL_REG		0x0c
#define S5P_JPEG_DRI_U_REG		0x10
#define S5P_JPEG_DRI_L_REG		0x14
#define S5P_JPEG_Y_U_REG		0x18
#define S5P_JPEG_Y_L_REG		0x1c
#define S5P_JPEG_X_U_REG		0x20
#define S5P_JPEG_X_L_REG		0x24
#define S5P_JPEG_CNT_U_REG		0x28
#define S5P_JPEG_CNT_M_REG		0x2c
#define S5P_JPEG_CNT_L_REG		0x30
#define S5P_JPEG_INTSE_REG		0x34
#define S5P_JPEG_INTST_REG		0x38

#define S5P_JPEG_COM_REG		0x4c

/* raw image data address */
#define S5P_JPEG_IMGADR_REG		0x50

#define S5P_JPEG_JPGADR_REG		0x58
#define S5P_JPEG_COEF1_REG		0x5c
#define S5P_JPEG_COEF2_REG		0x60
#define S5P_JPEG_COEF3_REG		0x64

#define S5P_JPEG_CMOD_REG		0x68
#define S5P_JPEG_CLKCON_REG		0x6c

#define S5P_JPEG_JSTART_REG		0x70
#define S5P_JPEG_JRSTART_REG		0x74
#define S5P_JPEG_SW_RESET_REG		0x78

#define S5P_JPEG_TIMER_SE_REG		0x7c
#define S5P_JPEG_TIMER_ST_REG		0x80
#define S5P_JPEG_COMSTAT_REG		0x84
#define S5P_JPEG_OUTFORM_REG		0x88
#define S5P_JPEG_VERSION_REG		0x8c

#define S5P_JPEG_ENC_STREAM_INTSE_REG	0x98
#define S5P_JPEG_ENC_STREAM_INTST_REG	0x9c

#define S5P_JPEG_QTBL0_REG		0x400
#define S5P_JPEG_QTBL1_REG		0x500
#define S5P_JPEG_QTBL2_REG		0x600
#define S5P_JPEG_QTBL3_REG		0x700

#define S5P_JPEG_HDCTBL0_REG		0x800
#define S5P_JPEG_HDCTBLG0_REG		0x840
#define S5P_JPEG_HACTBL0_REG		0x880
#define S5P_JPEG_HACTBLG0_REG		0x8c0
#define S5P_JPEG_HDCTBL1_REG		0xc00
#define S5P_JPEG_HDCTBLG1_REG		0xc40
#define S5P_JPEG_HACTBL1_REG		0xc80
#define S5P_JPEG_HACTBLG1_REG		0xcc0

/***************************************************************/
/* Bit definition part					*/
/***************************************************************/

/* JPEG Mode Register bit */
#define S5P_JPEG_MOD_REG_PROC_ENC			(0<<3)
#define S5P_JPEG_MOD_REG_PROC_DEC			(1<<3)

#define S5P_JPEG_MOD_REG_SUBSAMPLE_444			(0<<0)
#define S5P_JPEG_MOD_REG_SUBSAMPLE_422			(1<<0)
#define S5P_JPEG_MOD_REG_SUBSAMPLE_420			(2<<0)
#define S5P_JPEG_MOD_REG_SUBSAMPLE_GRAY			(3<<0)

/* JPEG Operation Status Register bit */
#define S5P_JPEG_OPR_REG_OPERATE			(1<<0)
#define S5P_JPEG_OPR_REG_NO_OPERATE			(0<<0)

/* Quantization Table And Huffman Table Number Register bit */
#define S5P_JPEG_QHTBL_REG_QT_NUM4			(1<<6)
#define S5P_JPEG_QHTBL_REG_QT_NUM3			(1<<4)
#define S5P_JPEG_QHTBL_REG_QT_NUM2			(1<<2)
#define S5P_JPEG_QHTBL_REG_QT_NUM1			(1<<0)

#define S5P_JPEG_QHTBL_REG_HT_NUM4_AC			(1<<7)
#define S5P_JPEG_QHTBL_REG_HT_NUM4_DC			(1<<6)
#define S5P_JPEG_QHTBL_REG_HT_NUM3_AC			(1<<5)
#define S5P_JPEG_QHTBL_REG_HT_NUM3_DC			(1<<4)
#define S5P_JPEG_QHTBL_REG_HT_NUM2_AC			(1<<3)
#define S5P_JPEG_QHTBL_REG_HT_NUM2_DC			(1<<2)
#define S5P_JPEG_QHTBL_REG_HT_NUM1_AC			(1<<1)
#define S5P_JPEG_QHTBL_REG_HT_NUM1_DC			(1<<0)

/* JPEG Color Mode Register bit */
#define S5P_JPEG_CMOD_REG_MOD_SEL_RGB			(2<<5)
#define S5P_JPEG_CMOD_REG_MOD_SEL_YCBCR422		(1<<5)
#define S5P_JPEG_CMOD_REG_MOD_MODE_Y16			(1<<1)
#define S5P_JPEG_CMOD_REG_MOD_MODE_0			(0<<1)

/* JPEG Clock Control Register bit */
#define S5P_JPEG_CLKCON_REG_CLK_DOWN_READY_ENABLE	(0<<1)
#define S5P_JPEG_CLKCON_REG_CLK_DOWN_READY_DISABLE	(1<<1)
#define S5P_JPEG_CLKCON_REG_POWER_ON_ACTIVATE		(1<<0)
#define S5P_JPEG_CLKCON_REG_POWER_ON_DISABLE		(0<<0)

/* JPEG Start Register bit */
#define S5P_JPEG_JSTART_REG_ENABLE			(1<<0)

/* JPEG Rdstart Register bit */
#define S5P_JPEG_JRSTART_REG_ENABLE			(1<<0)

/* JPEG SW Reset Register bit */
#define S5P_JPEG_SW_RESET_REG_ENABLE			(1<<0)

/* JPEG Interrupt Setting Register bit */
#define S5P_JPEG_INTSE_REG_RSTM_INT_EN			(1<<7)
#define S5P_JPEG_INTSE_REG_DATA_NUM_INT_EN		(1<<6)
#define S5P_JPEG_INTSE_REG_FINAL_MCU_NUM_INT_EN		(1<<5)

/* JPEG Decompression Output Format Register bit */
#define S5P_JPEG_OUTFORM_REG_YCBCY422			(0<<0)
#define S5P_JPEG_OUTFORM_REG_YCBCY420			(1<<0)

/* JPEG Decompression Input Stream Size Register bit */
#define S5P_JPEG_DEC_STREAM_SIZE_REG_PROHIBIT		(0x1FFFFFFF<<0)

/* JPEG Command Register bit */
#define S5P_JPEG_COM_INT_RELEASE			(1<<2)

#endif /* __ASM_ARM_REGS_S3C_JPEG_H */

