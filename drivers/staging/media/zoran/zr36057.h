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
#define ZR36057_VFEHCR_HSPol             BIT(30)
#define ZR36057_VFEHCR_HStart           10
#define ZR36057_VFEHCR_HEnd		0
#define ZR36057_VFEHCR_Hmask		0x3ff

#define ZR36057_VFEVCR          0x004	/* Video Front End, Vertical Configuration Register */
#define ZR36057_VFEVCR_VSPol             BIT(30)
#define ZR36057_VFEVCR_VStart           10
#define ZR36057_VFEVCR_VEnd		0
#define ZR36057_VFEVCR_Vmask		0x3ff

#define ZR36057_VFESPFR         0x008	/* Video Front End, Scaler and Pixel Format Register */
#define ZR36057_VFESPFR_ExtFl            BIT(26)
#define ZR36057_VFESPFR_TopField         BIT(25)
#define ZR36057_VFESPFR_VCLKPol          BIT(24)
#define ZR36057_VFESPFR_HFilter         21
#define ZR36057_VFESPFR_HorDcm          14
#define ZR36057_VFESPFR_VerDcm          8
#define ZR36057_VFESPFR_DispMode        6
#define ZR36057_VFESPFR_YUV422          (0<<3)
#define ZR36057_VFESPFR_RGB888          (1<<3)
#define ZR36057_VFESPFR_RGB565          (2<<3)
#define ZR36057_VFESPFR_RGB555          (3<<3)
#define ZR36057_VFESPFR_ErrDif          (1<<2)
#define ZR36057_VFESPFR_Pack24          (1<<1)
#define ZR36057_VFESPFR_LittleEndian    (1<<0)

#define ZR36057_VDTR            0x00c	/* Video Display "Top" Register */

#define ZR36057_VDBR            0x010	/* Video Display "Bottom" Register */

#define ZR36057_VSSFGR          0x014	/* Video Stride, Status, and Frame Grab Register */
#define ZR36057_VSSFGR_DispStride       16
#define ZR36057_VSSFGR_VidOvf            BIT(8)
#define ZR36057_VSSFGR_SnapShot          BIT(1)
#define ZR36057_VSSFGR_FrameGrab         BIT(0)

#define ZR36057_VDCR            0x018	/* Video Display Configuration Register */
#define ZR36057_VDCR_VidEn               BIT(31)
#define ZR36057_VDCR_MinPix             24
#define ZR36057_VDCR_Triton              BIT(24)
#define ZR36057_VDCR_VidWinHt           12
#define ZR36057_VDCR_VidWinWid          0

#define ZR36057_MMTR            0x01c	/* Masking Map "Top" Register */

#define ZR36057_MMBR            0x020	/* Masking Map "Bottom" Register */

#define ZR36057_OCR             0x024	/* Overlay Control Register */
#define ZR36057_OCR_OvlEnable            BIT(15)
#define ZR36057_OCR_MaskStride          0

#define ZR36057_SPGPPCR         0x028	/* System, PCI, and General Purpose Pins Control Register */
#define ZR36057_SPGPPCR_SoftReset	 BIT(24)

#define ZR36057_GPPGCR1         0x02c	/* General Purpose Pins and GuestBus Control Register (1) */

#define ZR36057_MCSAR           0x030	/* MPEG Code Source Address Register */

#define ZR36057_MCTCR           0x034	/* MPEG Code Transfer Control Register */
#define ZR36057_MCTCR_CodTime            BIT(30)
#define ZR36057_MCTCR_CEmpty             BIT(29)
#define ZR36057_MCTCR_CFlush             BIT(28)
#define ZR36057_MCTCR_CodGuestID	20
#define ZR36057_MCTCR_CodGuestReg	16

#define ZR36057_MCMPR           0x038	/* MPEG Code Memory Pointer Register */

#define ZR36057_ISR             0x03c	/* Interrupt Status Register */
#define ZR36057_ISR_GIRQ1                BIT(30)
#define ZR36057_ISR_GIRQ0                BIT(29)
#define ZR36057_ISR_CodRepIRQ            BIT(28)
#define ZR36057_ISR_JPEGRepIRQ           BIT(27)

#define ZR36057_ICR             0x040	/* Interrupt Control Register */
#define ZR36057_ICR_GIRQ1                BIT(30)
#define ZR36057_ICR_GIRQ0                BIT(29)
#define ZR36057_ICR_CodRepIRQ            BIT(28)
#define ZR36057_ICR_JPEGRepIRQ           BIT(27)
#define ZR36057_ICR_IntPinEn             BIT(24)

#define ZR36057_I2CBR           0x044	/* I2C Bus Register */
#define ZR36057_I2CBR_SDA		 BIT(1)
#define ZR36057_I2CBR_SCL		 BIT(0)

#define ZR36057_JMC             0x100	/* JPEG Mode and Control */
#define ZR36057_JMC_JPG                  BIT(31)
#define ZR36057_JMC_JPGExpMode          (0 << 29)
#define ZR36057_JMC_JPGCmpMode           BIT(29)
#define ZR36057_JMC_MJPGExpMode         (2 << 29)
#define ZR36057_JMC_MJPGCmpMode         (3 << 29)
#define ZR36057_JMC_RTBUSY_FB            BIT(6)
#define ZR36057_JMC_Go_en                BIT(5)
#define ZR36057_JMC_SyncMstr             BIT(4)
#define ZR36057_JMC_Fld_per_buff         BIT(3)
#define ZR36057_JMC_VFIFO_FB             BIT(2)
#define ZR36057_JMC_CFIFO_FB             BIT(1)
#define ZR36057_JMC_Stll_LitEndian       BIT(0)

#define ZR36057_JPC             0x104	/* JPEG Process Control */
#define ZR36057_JPC_P_Reset              BIT(7)
#define ZR36057_JPC_CodTrnsEn            BIT(5)
#define ZR36057_JPC_Active               BIT(0)

#define ZR36057_VSP             0x108	/* Vertical Sync Parameters */
#define ZR36057_VSP_VsyncSize           16
#define ZR36057_VSP_FrmTot              0

#define ZR36057_HSP             0x10c	/* Horizontal Sync Parameters */
#define ZR36057_HSP_HsyncStart          16
#define ZR36057_HSP_LineTot             0

#define ZR36057_FHAP            0x110	/* Field Horizontal Active Portion */
#define ZR36057_FHAP_NAX                16
#define ZR36057_FHAP_PAX                0

#define ZR36057_FVAP            0x114	/* Field Vertical Active Portion */
#define ZR36057_FVAP_NAY                16
#define ZR36057_FVAP_PAY                0

#define ZR36057_FPP             0x118	/* Field Process Parameters */
#define ZR36057_FPP_Odd_Even             BIT(0)

#define ZR36057_JCBA            0x11c	/* JPEG Code Base Address */

#define ZR36057_JCFT            0x120	/* JPEG Code FIFO Threshold */

#define ZR36057_JCGI            0x124	/* JPEG Codec Guest ID */
#define ZR36057_JCGI_JPEGuestID         4
#define ZR36057_JCGI_JPEGuestReg        0

#define ZR36057_GCR2            0x12c	/* GuestBus Control Register (2) */

#define ZR36057_POR             0x200	/* Post Office Register */
#define ZR36057_POR_POPen                BIT(25)
#define ZR36057_POR_POTime               BIT(24)
#define ZR36057_POR_PODir                BIT(23)

#define ZR36057_STR             0x300	/* "Still" Transfer Register */

#endif
