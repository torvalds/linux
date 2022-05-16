/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * zr36057.h - zr36057 register offsets
 *
 * Copyright (C) 1998 Dave Perks <dperks@ibm.net>
 */

#ifndef _ZR36057_H_
#define _ZR36057_H_

/* Zoran ZR36057 registers */

#define ZR36057_VFEHCR          0x000	/* Video Front End, Horizontal Configuration Register */
#define ZR36057_VFEHCR_HS_POL             BIT(30)
#define ZR36057_VFEHCR_H_START           10
#define ZR36057_VFEHCR_H_END		0
#define ZR36057_VFEHCR_HMASK		0x3ff

#define ZR36057_VFEVCR          0x004	/* Video Front End, Vertical Configuration Register */
#define ZR36057_VFEVCR_VS_POL             BIT(30)
#define ZR36057_VFEVCR_V_START           10
#define ZR36057_VFEVCR_V_END		0
#define ZR36057_VFEVCR_VMASK		0x3ff

#define ZR36057_VFESPFR         0x008	/* Video Front End, Scaler and Pixel Format Register */
#define ZR36057_VFESPFR_EXT_FL            BIT(26)
#define ZR36057_VFESPFR_TOP_FIELD         BIT(25)
#define ZR36057_VFESPFR_VCLK_POL          BIT(24)
#define ZR36057_VFESPFR_H_FILTER         21
#define ZR36057_VFESPFR_HOR_DCM          14
#define ZR36057_VFESPFR_VER_DCM          8
#define ZR36057_VFESPFR_DISP_MODE        6
#define ZR36057_VFESPFR_YUV422          (0<<3)
#define ZR36057_VFESPFR_RGB888          (1<<3)
#define ZR36057_VFESPFR_RGB565          (2<<3)
#define ZR36057_VFESPFR_RGB555          (3<<3)
#define ZR36057_VFESPFR_ERR_DIF          (1<<2)
#define ZR36057_VFESPFR_PACK24          (1<<1)
#define ZR36057_VFESPFR_LITTLE_ENDIAN    (1<<0)

#define ZR36057_VDTR            0x00c	/* Video Display "Top" Register */

#define ZR36057_VDBR            0x010	/* Video Display "Bottom" Register */

#define ZR36057_VSSFGR          0x014	/* Video Stride, Status, and Frame Grab Register */
#define ZR36057_VSSFGR_DISP_STRIDE       16
#define ZR36057_VSSFGR_VID_OVF            BIT(8)
#define ZR36057_VSSFGR_SNAP_SHOT          BIT(1)
#define ZR36057_VSSFGR_FRAME_GRAB         BIT(0)

#define ZR36057_VDCR            0x018	/* Video Display Configuration Register */
#define ZR36057_VDCR_VID_EN               BIT(31)
#define ZR36057_VDCR_MIN_PIX             24
#define ZR36057_VDCR_TRITON              BIT(24)
#define ZR36057_VDCR_VID_WIN_HT           12
#define ZR36057_VDCR_VID_WIN_WID          0

#define ZR36057_MMTR            0x01c	/* Masking Map "Top" Register */

#define ZR36057_MMBR            0x020	/* Masking Map "Bottom" Register */

#define ZR36057_OCR             0x024	/* Overlay Control Register */
#define ZR36057_OCR_OVL_ENABLE            BIT(15)
#define ZR36057_OCR_MASK_STRIDE          0

#define ZR36057_SPGPPCR         0x028	/* System, PCI, and General Purpose Pins Control Register */
#define ZR36057_SPGPPCR_SOFT_RESET	 BIT(24)

#define ZR36057_GPPGCR1         0x02c	/* General Purpose Pins and GuestBus Control Register (1) */

#define ZR36057_MCSAR           0x030	/* MPEG Code Source Address Register */

#define ZR36057_MCTCR           0x034	/* MPEG Code Transfer Control Register */
#define ZR36057_MCTCR_COD_TIME            BIT(30)
#define ZR36057_MCTCR_C_EMPTY             BIT(29)
#define ZR36057_MCTCR_C_FLUSH             BIT(28)
#define ZR36057_MCTCR_COD_GUEST_ID	20
#define ZR36057_MCTCR_COD_GUEST_REG	16

#define ZR36057_MCMPR           0x038	/* MPEG Code Memory Pointer Register */

#define ZR36057_ISR             0x03c	/* Interrupt Status Register */
#define ZR36057_ISR_GIRQ1                BIT(30)
#define ZR36057_ISR_GIRQ0                BIT(29)
#define ZR36057_ISR_COD_REP_IRQ            BIT(28)
#define ZR36057_ISR_JPEG_REP_IRQ           BIT(27)

#define ZR36057_ICR             0x040	/* Interrupt Control Register */
#define ZR36057_ICR_GIRQ1                BIT(30)
#define ZR36057_ICR_GIRQ0                BIT(29)
#define ZR36057_ICR_COD_REP_IRQ            BIT(28)
#define ZR36057_ICR_JPEG_REP_IRQ           BIT(27)
#define ZR36057_ICR_INT_PIN_EN             BIT(24)

#define ZR36057_I2CBR           0x044	/* I2C Bus Register */
#define ZR36057_I2CBR_SDA		 BIT(1)
#define ZR36057_I2CBR_SCL		 BIT(0)

#define ZR36057_JMC             0x100	/* JPEG Mode and Control */
#define ZR36057_JMC_JPG                  BIT(31)
#define ZR36057_JMC_JPG_EXP_MODE          (0 << 29)
#define ZR36057_JMC_JPG_CMP_MODE           BIT(29)
#define ZR36057_JMC_MJPG_EXP_MODE         (2 << 29)
#define ZR36057_JMC_MJPG_CMP_MODE         (3 << 29)
#define ZR36057_JMC_RTBUSY_FB            BIT(6)
#define ZR36057_JMC_GO_EN                BIT(5)
#define ZR36057_JMC_SYNC_MSTR             BIT(4)
#define ZR36057_JMC_FLD_PER_BUFF         BIT(3)
#define ZR36057_JMC_VFIFO_FB             BIT(2)
#define ZR36057_JMC_CFIFO_FB             BIT(1)
#define ZR36057_JMC_STLL_LIT_ENDIAN       BIT(0)

#define ZR36057_JPC             0x104	/* JPEG Process Control */
#define ZR36057_JPC_P_RESET              BIT(7)
#define ZR36057_JPC_COD_TRNS_EN            BIT(5)
#define ZR36057_JPC_ACTIVE               BIT(0)

#define ZR36057_VSP             0x108	/* Vertical Sync Parameters */
#define ZR36057_VSP_VSYNC_SIZE           16
#define ZR36057_VSP_FRM_TOT              0

#define ZR36057_HSP             0x10c	/* Horizontal Sync Parameters */
#define ZR36057_HSP_HSYNC_START          16
#define ZR36057_HSP_LINE_TOT             0

#define ZR36057_FHAP            0x110	/* Field Horizontal Active Portion */
#define ZR36057_FHAP_NAX                16
#define ZR36057_FHAP_PAX                0

#define ZR36057_FVAP            0x114	/* Field Vertical Active Portion */
#define ZR36057_FVAP_NAY                16
#define ZR36057_FVAP_PAY                0

#define ZR36057_FPP             0x118	/* Field Process Parameters */
#define ZR36057_FPP_ODD_EVEN             BIT(0)

#define ZR36057_JCBA            0x11c	/* JPEG Code Base Address */

#define ZR36057_JCFT            0x120	/* JPEG Code FIFO Threshold */

#define ZR36057_JCGI            0x124	/* JPEG Codec Guest ID */
#define ZR36057_JCGI_JPE_GUEST_ID         4
#define ZR36057_JCGI_JPE_GUEST_REG        0

#define ZR36057_GCR2            0x12c	/* GuestBus Control Register (2) */

#define ZR36057_POR             0x200	/* Post Office Register */
#define ZR36057_POR_PO_PEN                BIT(25)
#define ZR36057_POR_PO_TIME               BIT(24)
#define ZR36057_POR_PO_DIR                BIT(23)

#define ZR36057_STR             0x300	/* "Still" Transfer Register */

#endif
