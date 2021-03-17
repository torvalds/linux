/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2006-2010 Texas Instruments Inc
 */
#ifndef _VPBE_VENC_REGS_H
#define _VPBE_VENC_REGS_H

/* VPBE Video Encoder / Digital LCD Subsystem Registers (VENC) */
#define VENC_VMOD				0x00
#define VENC_VIDCTL				0x04
#define VENC_VDPRO				0x08
#define VENC_SYNCCTL				0x0C
#define VENC_HSPLS				0x10
#define VENC_VSPLS				0x14
#define VENC_HINT				0x18
#define VENC_HSTART				0x1C
#define VENC_HVALID				0x20
#define VENC_VINT				0x24
#define VENC_VSTART				0x28
#define VENC_VVALID				0x2C
#define VENC_HSDLY				0x30
#define VENC_VSDLY				0x34
#define VENC_YCCCTL				0x38
#define VENC_RGBCTL				0x3C
#define VENC_RGBCLP				0x40
#define VENC_LINECTL				0x44
#define VENC_CULLLINE				0x48
#define VENC_LCDOUT				0x4C
#define VENC_BRTS				0x50
#define VENC_BRTW				0x54
#define VENC_ACCTL				0x58
#define VENC_PWMP				0x5C
#define VENC_PWMW				0x60
#define VENC_DCLKCTL				0x64
#define VENC_DCLKPTN0				0x68
#define VENC_DCLKPTN1				0x6C
#define VENC_DCLKPTN2				0x70
#define VENC_DCLKPTN3				0x74
#define VENC_DCLKPTN0A				0x78
#define VENC_DCLKPTN1A				0x7C
#define VENC_DCLKPTN2A				0x80
#define VENC_DCLKPTN3A				0x84
#define VENC_DCLKHS				0x88
#define VENC_DCLKHSA				0x8C
#define VENC_DCLKHR				0x90
#define VENC_DCLKVS				0x94
#define VENC_DCLKVR				0x98
#define VENC_CAPCTL				0x9C
#define VENC_CAPDO				0xA0
#define VENC_CAPDE				0xA4
#define VENC_ATR0				0xA8
#define VENC_ATR1				0xAC
#define VENC_ATR2				0xB0
#define VENC_VSTAT				0xB8
#define VENC_RAMADR				0xBC
#define VENC_RAMPORT				0xC0
#define VENC_DACTST				0xC4
#define VENC_YCOLVL				0xC8
#define VENC_SCPROG				0xCC
#define VENC_CVBS				0xDC
#define VENC_CMPNT				0xE0
#define VENC_ETMG0				0xE4
#define VENC_ETMG1				0xE8
#define VENC_ETMG2				0xEC
#define VENC_ETMG3				0xF0
#define VENC_DACSEL				0xF4
#define VENC_ARGBX0				0x100
#define VENC_ARGBX1				0x104
#define VENC_ARGBX2				0x108
#define VENC_ARGBX3				0x10C
#define VENC_ARGBX4				0x110
#define VENC_DRGBX0				0x114
#define VENC_DRGBX1				0x118
#define VENC_DRGBX2				0x11C
#define VENC_DRGBX3				0x120
#define VENC_DRGBX4				0x124
#define VENC_VSTARTA				0x128
#define VENC_OSDCLK0				0x12C
#define VENC_OSDCLK1				0x130
#define VENC_HVLDCL0				0x134
#define VENC_HVLDCL1				0x138
#define VENC_OSDHADV				0x13C
#define VENC_CLKCTL				0x140
#define VENC_GAMCTL				0x144
#define VENC_XHINTVL				0x174

/* bit definitions */
#define VPBE_PCR_VENC_DIV			(1 << 1)
#define VPBE_PCR_CLK_OFF			(1 << 0)

#define VENC_VMOD_VDMD_SHIFT			12
#define VENC_VMOD_VDMD_YCBCR16			0
#define VENC_VMOD_VDMD_YCBCR8			1
#define VENC_VMOD_VDMD_RGB666			2
#define VENC_VMOD_VDMD_RGB8			3
#define VENC_VMOD_VDMD_EPSON			4
#define VENC_VMOD_VDMD_CASIO			5
#define VENC_VMOD_VDMD_UDISPQVGA		6
#define VENC_VMOD_VDMD_STNLCD			7
#define VENC_VMOD_VIE_SHIFT			1
#define VENC_VMOD_VDMD				(7 << 12)
#define VENC_VMOD_ITLCL				(1 << 11)
#define VENC_VMOD_ITLC				(1 << 10)
#define VENC_VMOD_NSIT				(1 << 9)
#define VENC_VMOD_HDMD				(1 << 8)
#define VENC_VMOD_TVTYP_SHIFT			6
#define VENC_VMOD_TVTYP				(3 << 6)
#define VENC_VMOD_SLAVE				(1 << 5)
#define VENC_VMOD_VMD				(1 << 4)
#define VENC_VMOD_BLNK				(1 << 3)
#define VENC_VMOD_VIE				(1 << 1)
#define VENC_VMOD_VENC				(1 << 0)

/* VMOD TVTYP options for HDMD=0 */
#define SDTV_NTSC				0
#define SDTV_PAL				1
/* VMOD TVTYP options for HDMD=1 */
#define HDTV_525P				0
#define HDTV_625P				1
#define HDTV_1080I				2
#define HDTV_720P				3

#define VENC_VIDCTL_VCLKP			(1 << 14)
#define VENC_VIDCTL_VCLKE_SHIFT			13
#define VENC_VIDCTL_VCLKE			(1 << 13)
#define VENC_VIDCTL_VCLKZ_SHIFT			12
#define VENC_VIDCTL_VCLKZ			(1 << 12)
#define VENC_VIDCTL_SYDIR_SHIFT			8
#define VENC_VIDCTL_SYDIR			(1 << 8)
#define VENC_VIDCTL_DOMD_SHIFT			4
#define VENC_VIDCTL_DOMD			(3 << 4)
#define VENC_VIDCTL_YCDIR_SHIFT			0
#define VENC_VIDCTL_YCDIR			(1 << 0)

#define VENC_VDPRO_ATYCC_SHIFT			5
#define VENC_VDPRO_ATYCC			(1 << 5)
#define VENC_VDPRO_ATCOM_SHIFT			4
#define VENC_VDPRO_ATCOM			(1 << 4)
#define VENC_VDPRO_DAFRQ			(1 << 3)
#define VENC_VDPRO_DAUPS			(1 << 2)
#define VENC_VDPRO_CUPS				(1 << 1)
#define VENC_VDPRO_YUPS				(1 << 0)

#define VENC_SYNCCTL_VPL_SHIFT			3
#define VENC_SYNCCTL_VPL			(1 << 3)
#define VENC_SYNCCTL_HPL_SHIFT			2
#define VENC_SYNCCTL_HPL			(1 << 2)
#define VENC_SYNCCTL_SYEV_SHIFT			1
#define VENC_SYNCCTL_SYEV			(1 << 1)
#define VENC_SYNCCTL_SYEH_SHIFT			0
#define VENC_SYNCCTL_SYEH			(1 << 0)
#define VENC_SYNCCTL_OVD_SHIFT			14
#define VENC_SYNCCTL_OVD			(1 << 14)

#define VENC_DCLKCTL_DCKEC_SHIFT		11
#define VENC_DCLKCTL_DCKEC			(1 << 11)
#define VENC_DCLKCTL_DCKPW_SHIFT		0
#define VENC_DCLKCTL_DCKPW			(0x3f << 0)

#define VENC_VSTAT_FIDST			(1 << 4)

#define VENC_CMPNT_MRGB_SHIFT			14
#define VENC_CMPNT_MRGB				(1 << 14)

#endif				/* _VPBE_VENC_REGS_H */
