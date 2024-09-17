/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  tw68-reg.h - TW68xx register offsets
 *
 *  Much of this code is derived from the cx88 and sa7134 drivers, which
 *  were in turn derived from the bt87x driver.  The original work was by
 *  Gerd Knorr; more recently the code was enhanced by Mauro Carvalho Chehab,
 *  Hans Verkuil, Andy Walls and many others.  Their work is gratefully
 *  acknowledged.  Full credit goes to them - any problems within this code
 *  are mine.
 *
 *  Copyright (C) William M. Brack
 *
 *  Refactored and updated to the latest v4l core frameworks:
 *
 *  Copyright (C) 2014 Hans Verkuil <hverkuil@xs4all.nl>
*/

#ifndef _TW68_REG_H_
#define _TW68_REG_H_

/* ---------------------------------------------------------------------- */
#define	TW68_DMAC		0x000
#define	TW68_DMAP_SA		0x004
#define	TW68_DMAP_EXE		0x008
#define	TW68_DMAP_PP		0x00c
#define	TW68_VBIC		0x010
#define	TW68_SBUSC		0x014
#define	TW68_SBUSSD		0x018
#define	TW68_INTSTAT		0x01C
#define	TW68_INTMASK		0x020
#define	TW68_GPIOC		0x024
#define	TW68_GPOE		0x028
#define	TW68_TESTREG		0x02C
#define	TW68_SBUSRD		0x030
#define	TW68_SBUS_TRIG		0x034
#define	TW68_CAP_CTL		0x040
#define	TW68_SUBSYS		0x054
#define	TW68_I2C_RST		0x064
#define	TW68_VBIINST		0x06C
/* define bits in FIFO and DMAP Control reg */
#define	TW68_DMAP_EN		(1 << 0)
#define	TW68_FIFO_EN		(1 << 1)
/* define the Interrupt Status Register bits */
#define	TW68_SBDONE		(1 << 0)
#define	TW68_DMAPI		(1 << 1)
#define	TW68_GPINT		(1 << 2)
#define	TW68_FFOF		(1 << 3)
#define	TW68_FDMIS		(1 << 4)
#define	TW68_DMAPERR		(1 << 5)
#define	TW68_PABORT		(1 << 6)
#define	TW68_SBDONE2		(1 << 12)
#define	TW68_SBERR2		(1 << 13)
#define	TW68_PPERR		(1 << 14)
#define	TW68_FFERR		(1 << 15)
#define	TW68_DET50		(1 << 16)
#define	TW68_FLOCK		(1 << 17)
#define	TW68_CCVALID		(1 << 18)
#define	TW68_VLOCK		(1 << 19)
#define	TW68_FIELD		(1 << 20)
#define	TW68_SLOCK		(1 << 21)
#define	TW68_HLOCK		(1 << 22)
#define	TW68_VDLOSS		(1 << 23)
#define	TW68_SBERR		(1 << 24)
/* define the i2c control register bits */
#define	TW68_SBMODE		(0)
#define	TW68_WREN		(1)
#define	TW68_SSCLK		(6)
#define	TW68_SSDAT		(7)
#define	TW68_SBCLK		(8)
#define	TW68_WDLEN		(16)
#define	TW68_RDLEN		(20)
#define	TW68_SBRW		(24)
#define	TW68_SBDEV		(25)

#define	TW68_SBMODE_B		(1 << TW68_SBMODE)
#define	TW68_WREN_B		(1 << TW68_WREN)
#define	TW68_SSCLK_B		(1 << TW68_SSCLK)
#define	TW68_SSDAT_B		(1 << TW68_SSDAT)
#define	TW68_SBRW_B		(1 << TW68_SBRW)

#define	TW68_GPDATA		0x100
#define	TW68_STATUS1		0x204
#define	TW68_INFORM		0x208
#define	TW68_OPFORM		0x20C
#define	TW68_HSYNC		0x210
#define	TW68_ACNTL		0x218
#define	TW68_CROP_HI		0x21C
#define	TW68_VDELAY_LO		0x220
#define	TW68_VACTIVE_LO		0x224
#define	TW68_HDELAY_LO		0x228
#define	TW68_HACTIVE_LO		0x22C
#define	TW68_CNTRL1		0x230
#define	TW68_VSCALE_LO		0x234
#define	TW68_SCALE_HI		0x238
#define	TW68_HSCALE_LO		0x23C
#define	TW68_BRIGHT		0x240
#define	TW68_CONTRAST		0x244
#define	TW68_SHARPNESS		0x248
#define	TW68_SAT_U		0x24C
#define	TW68_SAT_V		0x250
#define	TW68_HUE		0x254
#define	TW68_SHARP2		0x258
#define	TW68_VSHARP		0x25C
#define	TW68_CORING		0x260
#define	TW68_VBICNTL		0x264
#define	TW68_CNTRL2		0x268
#define	TW68_CC_DATA		0x26C
#define	TW68_SDT		0x270
#define	TW68_SDTR		0x274
#define	TW68_RESERV2		0x278
#define	TW68_RESERV3		0x27C
#define	TW68_CLMPG		0x280
#define	TW68_IAGC		0x284
#define	TW68_AGCGAIN		0x288
#define	TW68_PEAKWT		0x28C
#define	TW68_CLMPL		0x290
#define	TW68_SYNCT		0x294
#define	TW68_MISSCNT		0x298
#define	TW68_PCLAMP		0x29C
#define	TW68_VCNTL1		0x2A0
#define	TW68_VCNTL2		0x2A4
#define	TW68_CKILL		0x2A8
#define	TW68_COMB		0x2AC
#define	TW68_LDLY		0x2B0
#define	TW68_MISC1		0x2B4
#define	TW68_LOOP		0x2B8
#define	TW68_MISC2		0x2BC
#define	TW68_MVSN		0x2C0
#define	TW68_STATUS2		0x2C4
#define	TW68_HFREF		0x2C8
#define	TW68_CLMD		0x2CC
#define	TW68_IDCNTL		0x2D0
#define	TW68_CLCNTL1		0x2D4

/* Audio */
#define	TW68_ACKI1		0x300
#define	TW68_ACKI2		0x304
#define	TW68_ACKI3		0x308
#define	TW68_ACKN1		0x30C
#define	TW68_ACKN2		0x310
#define	TW68_ACKN3		0x314
#define	TW68_SDIV		0x318
#define	TW68_LRDIV		0x31C
#define	TW68_ACCNTL		0x320

#define	TW68_VSCTL		0x3B8
#define	TW68_CHROMAGVAL		0x3BC

#define	TW68_F2CROP_HI		0x3DC
#define	TW68_F2VDELAY_LO	0x3E0
#define	TW68_F2VACTIVE_LO	0x3E4
#define	TW68_F2HDELAY_LO	0x3E8
#define	TW68_F2HACTIVE_LO	0x3EC
#define	TW68_F2CNT		0x3F0
#define	TW68_F2VSCALE_LO	0x3F4
#define	TW68_F2SCALE_HI		0x3F8
#define	TW68_F2HSCALE_LO	0x3FC

#define	RISC_INT_BIT		0x08000000
#define	RISC_SYNCO		0xC0000000
#define	RISC_SYNCE		0xD0000000
#define	RISC_JUMP		0xB0000000
#define	RISC_LINESTART		0x90000000
#define	RISC_INLINE		0xA0000000

#define VideoFormatNTSC		 0
#define VideoFormatNTSCJapan	 0
#define VideoFormatPALBDGHI	 1
#define VideoFormatSECAM	 2
#define VideoFormatNTSC443	 3
#define VideoFormatPALM		 4
#define VideoFormatPALN		 5
#define VideoFormatPALNC	 5
#define VideoFormatPAL60	 6
#define VideoFormatAuto		 7

#define ColorFormatRGB32	 0x00
#define ColorFormatRGB24	 0x10
#define ColorFormatRGB16	 0x20
#define ColorFormatRGB15	 0x30
#define ColorFormatYUY2		 0x40
#define ColorFormatBSWAP         0x04
#define ColorFormatWSWAP         0x08
#define ColorFormatGamma         0x80
#endif
