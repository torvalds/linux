/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * shmob_drm_regs.h  --  SH Mobile DRM registers
 *
 * Copyright (C) 2012 Renesas Electronics Corporation
 *
 * Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#ifndef __SHMOB_DRM_REGS_H__
#define __SHMOB_DRM_REGS_H__

#include <linux/io.h>
#include <linux/jiffies.h>

#include "shmob_drm_drv.h"

/* Register definitions */
#define LDDCKPAT1R		0x400
#define LDDCKPAT2R		0x404
#define LDDCKR			0x410
#define LDDCKR_ICKSEL_BUS	(0 << 16)
#define LDDCKR_ICKSEL_MIPI	(1 << 16)
#define LDDCKR_ICKSEL_HDMI	(2 << 16)
#define LDDCKR_ICKSEL_EXT	(3 << 16)
#define LDDCKR_ICKSEL_MASK	(7 << 16)
#define LDDCKR_MOSEL		(1 << 6)
#define LDDCKSTPR		0x414
#define LDDCKSTPR_DCKSTS	(1 << 16)
#define LDDCKSTPR_DCKSTP	(1 << 0)
#define LDMT1R			0x418
#define LDMT1R_VPOL		(1 << 28)
#define LDMT1R_HPOL		(1 << 27)
#define LDMT1R_DWPOL		(1 << 26)
#define LDMT1R_DIPOL		(1 << 25)
#define LDMT1R_DAPOL		(1 << 24)
#define LDMT1R_HSCNT		(1 << 17)
#define LDMT1R_DWCNT		(1 << 16)
#define LDMT1R_IFM		(1 << 12)
#define LDMT1R_MIFTYP_RGB8	(0x0 << 0)
#define LDMT1R_MIFTYP_RGB9	(0x4 << 0)
#define LDMT1R_MIFTYP_RGB12A	(0x5 << 0)
#define LDMT1R_MIFTYP_RGB12B	(0x6 << 0)
#define LDMT1R_MIFTYP_RGB16	(0x7 << 0)
#define LDMT1R_MIFTYP_RGB18	(0xa << 0)
#define LDMT1R_MIFTYP_RGB24	(0xb << 0)
#define LDMT1R_MIFTYP_YCBCR	(0xf << 0)
#define LDMT1R_MIFTYP_SYS8A	(0x0 << 0)
#define LDMT1R_MIFTYP_SYS8B	(0x1 << 0)
#define LDMT1R_MIFTYP_SYS8C	(0x2 << 0)
#define LDMT1R_MIFTYP_SYS8D	(0x3 << 0)
#define LDMT1R_MIFTYP_SYS9	(0x4 << 0)
#define LDMT1R_MIFTYP_SYS12	(0x5 << 0)
#define LDMT1R_MIFTYP_SYS16A	(0x7 << 0)
#define LDMT1R_MIFTYP_SYS16B	(0x8 << 0)
#define LDMT1R_MIFTYP_SYS16C	(0x9 << 0)
#define LDMT1R_MIFTYP_SYS18	(0xa << 0)
#define LDMT1R_MIFTYP_SYS24	(0xb << 0)
#define LDMT1R_MIFTYP_MASK	(0xf << 0)
#define LDMT2R			0x41c
#define LDMT2R_CSUP_MASK	(7 << 26)
#define LDMT2R_CSUP_SHIFT	26
#define LDMT2R_RSV		(1 << 25)
#define LDMT2R_VSEL		(1 << 24)
#define LDMT2R_WCSC_MASK	(0xff << 16)
#define LDMT2R_WCSC_SHIFT	16
#define LDMT2R_WCEC_MASK	(0xff << 8)
#define LDMT2R_WCEC_SHIFT	8
#define LDMT2R_WCLW_MASK	(0xff << 0)
#define LDMT2R_WCLW_SHIFT	0
#define LDMT3R			0x420
#define LDMT3R_RDLC_MASK	(0x3f << 24)
#define LDMT3R_RDLC_SHIFT	24
#define LDMT3R_RCSC_MASK	(0xff << 16)
#define LDMT3R_RCSC_SHIFT	16
#define LDMT3R_RCEC_MASK	(0xff << 8)
#define LDMT3R_RCEC_SHIFT	8
#define LDMT3R_RCLW_MASK	(0xff << 0)
#define LDMT3R_RCLW_SHIFT	0
#define LDDFR			0x424
#define LDDFR_CF1		(1 << 18)
#define LDDFR_CF0		(1 << 17)
#define LDDFR_CC		(1 << 16)
#define LDDFR_YF_420		(0 << 8)
#define LDDFR_YF_422		(1 << 8)
#define LDDFR_YF_444		(2 << 8)
#define LDDFR_YF_MASK		(3 << 8)
#define LDDFR_PKF_ARGB32	(0x00 << 0)
#define LDDFR_PKF_RGB16		(0x03 << 0)
#define LDDFR_PKF_RGB24		(0x0b << 0)
#define LDDFR_PKF_MASK		(0x1f << 0)
#define LDSM1R			0x428
#define LDSM1R_OS		(1 << 0)
#define LDSM2R			0x42c
#define LDSM2R_OSTRG		(1 << 0)
#define LDSA1R			0x430
#define LDSA2R			0x434
#define LDMLSR			0x438
#define LDWBFR			0x43c
#define LDWBCNTR		0x440
#define LDWBAR			0x444
#define LDHCNR			0x448
#define LDHSYNR			0x44c
#define LDVLNR			0x450
#define LDVSYNR			0x454
#define LDHPDR			0x458
#define LDVPDR			0x45c
#define LDPMR			0x460
#define LDPMR_LPS		(3 << 0)
#define LDINTR			0x468
#define LDINTR_FE		(1 << 10)
#define LDINTR_VSE		(1 << 9)
#define LDINTR_VEE		(1 << 8)
#define LDINTR_FS		(1 << 2)
#define LDINTR_VSS		(1 << 1)
#define LDINTR_VES		(1 << 0)
#define LDINTR_STATUS_MASK	(0xff << 0)
#define LDSR			0x46c
#define LDSR_MSS		(1 << 10)
#define LDSR_MRS		(1 << 8)
#define LDSR_AS			(1 << 1)
#define LDCNT1R			0x470
#define LDCNT1R_DE		(1 << 0)
#define LDCNT2R			0x474
#define LDCNT2R_BR		(1 << 8)
#define LDCNT2R_MD		(1 << 3)
#define LDCNT2R_SE		(1 << 2)
#define LDCNT2R_ME		(1 << 1)
#define LDCNT2R_DO		(1 << 0)
#define LDRCNTR			0x478
#define LDRCNTR_SRS		(1 << 17)
#define LDRCNTR_SRC		(1 << 16)
#define LDRCNTR_MRS		(1 << 1)
#define LDRCNTR_MRC		(1 << 0)
#define LDDDSR			0x47c
#define LDDDSR_LS		(1 << 2)
#define LDDDSR_WS		(1 << 1)
#define LDDDSR_BS		(1 << 0)
#define LDHAJR			0x4a0

#define LDDWD0R			0x800
#define LDDWDxR_WDACT		(1 << 28)
#define LDDWDxR_RSW		(1 << 24)
#define LDDRDR			0x840
#define LDDRDR_RSR		(1 << 24)
#define LDDRDR_DRD_MASK		(0x3ffff << 0)
#define LDDWAR			0x900
#define LDDWAR_WA		(1 << 0)
#define LDDRAR			0x904
#define LDDRAR_RA		(1 << 0)

#define LDBCR			0xb00
#define LDBCR_UPC(n)		(1 << ((n) + 16))
#define LDBCR_UPF(n)		(1 << ((n) + 8))
#define LDBCR_UPD(n)		(1 << ((n) + 0))
#define LDBnBSIFR(n)		(0xb20 + (n) * 0x20 + 0x00)
#define LDBBSIFR_EN		(1 << 31)
#define LDBBSIFR_VS		(1 << 29)
#define LDBBSIFR_BRSEL		(1 << 28)
#define LDBBSIFR_MX		(1 << 27)
#define LDBBSIFR_MY		(1 << 26)
#define LDBBSIFR_CV3		(3 << 24)
#define LDBBSIFR_CV2		(2 << 24)
#define LDBBSIFR_CV1		(1 << 24)
#define LDBBSIFR_CV0		(0 << 24)
#define LDBBSIFR_CV_MASK	(3 << 24)
#define LDBBSIFR_LAY_MASK	(0xff << 16)
#define LDBBSIFR_LAY_SHIFT	16
#define LDBBSIFR_ROP3_MASK	(0xff << 16)
#define LDBBSIFR_ROP3_SHIFT	16
#define LDBBSIFR_AL_PL8		(3 << 14)
#define LDBBSIFR_AL_PL1		(2 << 14)
#define LDBBSIFR_AL_PK		(1 << 14)
#define LDBBSIFR_AL_1		(0 << 14)
#define LDBBSIFR_AL_MASK	(3 << 14)
#define LDBBSIFR_SWPL		(1 << 10)
#define LDBBSIFR_SWPW		(1 << 9)
#define LDBBSIFR_SWPB		(1 << 8)
#define LDBBSIFR_RY		(1 << 7)
#define LDBBSIFR_CHRR_420	(2 << 0)
#define LDBBSIFR_CHRR_422	(1 << 0)
#define LDBBSIFR_CHRR_444	(0 << 0)
#define LDBBSIFR_RPKF_ARGB32	(0x00 << 0)
#define LDBBSIFR_RPKF_RGB16	(0x03 << 0)
#define LDBBSIFR_RPKF_RGB24	(0x0b << 0)
#define LDBBSIFR_RPKF_MASK	(0x1f << 0)
#define LDBnBSSZR(n)		(0xb20 + (n) * 0x20 + 0x04)
#define LDBBSSZR_BVSS_MASK	(0xfff << 16)
#define LDBBSSZR_BVSS_SHIFT	16
#define LDBBSSZR_BHSS_MASK	(0xfff << 0)
#define LDBBSSZR_BHSS_SHIFT	0
#define LDBnBLOCR(n)		(0xb20 + (n) * 0x20 + 0x08)
#define LDBBLOCR_CVLC_MASK	(0xfff << 16)
#define LDBBLOCR_CVLC_SHIFT	16
#define LDBBLOCR_CHLC_MASK	(0xfff << 0)
#define LDBBLOCR_CHLC_SHIFT	0
#define LDBnBSMWR(n)		(0xb20 + (n) * 0x20 + 0x0c)
#define LDBBSMWR_BSMWA_MASK	(0xffff << 16)
#define LDBBSMWR_BSMWA_SHIFT	16
#define LDBBSMWR_BSMW_MASK	(0xffff << 0)
#define LDBBSMWR_BSMW_SHIFT	0
#define LDBnBSAYR(n)		(0xb20 + (n) * 0x20 + 0x10)
#define LDBBSAYR_FG1A_MASK	(0xff << 24)
#define LDBBSAYR_FG1A_SHIFT	24
#define LDBBSAYR_FG1R_MASK	(0xff << 16)
#define LDBBSAYR_FG1R_SHIFT	16
#define LDBBSAYR_FG1G_MASK	(0xff << 8)
#define LDBBSAYR_FG1G_SHIFT	8
#define LDBBSAYR_FG1B_MASK	(0xff << 0)
#define LDBBSAYR_FG1B_SHIFT	0
#define LDBnBSACR(n)		(0xb20 + (n) * 0x20 + 0x14)
#define LDBBSACR_FG2A_MASK	(0xff << 24)
#define LDBBSACR_FG2A_SHIFT	24
#define LDBBSACR_FG2R_MASK	(0xff << 16)
#define LDBBSACR_FG2R_SHIFT	16
#define LDBBSACR_FG2G_MASK	(0xff << 8)
#define LDBBSACR_FG2G_SHIFT	8
#define LDBBSACR_FG2B_MASK	(0xff << 0)
#define LDBBSACR_FG2B_SHIFT	0
#define LDBnBSAAR(n)		(0xb20 + (n) * 0x20 + 0x18)
#define LDBBSAAR_AP_MASK	(0xff << 24)
#define LDBBSAAR_AP_SHIFT	24
#define LDBBSAAR_R_MASK		(0xff << 16)
#define LDBBSAAR_R_SHIFT	16
#define LDBBSAAR_GY_MASK	(0xff << 8)
#define LDBBSAAR_GY_SHIFT	8
#define LDBBSAAR_B_MASK		(0xff << 0)
#define LDBBSAAR_B_SHIFT	0
#define LDBnBPPCR(n)		(0xb20 + (n) * 0x20 + 0x1c)
#define LDBBPPCR_AP_MASK	(0xff << 24)
#define LDBBPPCR_AP_SHIFT	24
#define LDBBPPCR_R_MASK		(0xff << 16)
#define LDBBPPCR_R_SHIFT	16
#define LDBBPPCR_GY_MASK	(0xff << 8)
#define LDBBPPCR_GY_SHIFT	8
#define LDBBPPCR_B_MASK		(0xff << 0)
#define LDBBPPCR_B_SHIFT	0
#define LDBnBBGCL(n)		(0xb10 + (n) * 0x04)
#define LDBBBGCL_BGA_MASK	(0xff << 24)
#define LDBBBGCL_BGA_SHIFT	24
#define LDBBBGCL_BGR_MASK	(0xff << 16)
#define LDBBBGCL_BGR_SHIFT	16
#define LDBBBGCL_BGG_MASK	(0xff << 8)
#define LDBBBGCL_BGG_SHIFT	8
#define LDBBBGCL_BGB_MASK	(0xff << 0)
#define LDBBBGCL_BGB_SHIFT	0

#define LCDC_SIDE_B_OFFSET	0x1000
#define LCDC_MIRROR_OFFSET	0x2000

static inline bool lcdc_is_banked(u32 reg)
{
	switch (reg) {
	case LDMT1R:
	case LDMT2R:
	case LDMT3R:
	case LDDFR:
	case LDSM1R:
	case LDSA1R:
	case LDSA2R:
	case LDMLSR:
	case LDWBFR:
	case LDWBCNTR:
	case LDWBAR:
	case LDHCNR:
	case LDHSYNR:
	case LDVLNR:
	case LDVSYNR:
	case LDHPDR:
	case LDVPDR:
	case LDHAJR:
		return true;
	default:
		return reg >= LDBnBBGCL(0) && reg <= LDBnBPPCR(3);
	}
}

static inline void lcdc_write_mirror(struct shmob_drm_device *sdev, u32 reg,
				     u32 data)
{
	iowrite32(data, sdev->mmio + reg + LCDC_MIRROR_OFFSET);
}

static inline void lcdc_write(struct shmob_drm_device *sdev, u32 reg, u32 data)
{
	iowrite32(data, sdev->mmio + reg);
	if (lcdc_is_banked(reg))
		iowrite32(data, sdev->mmio + reg + LCDC_SIDE_B_OFFSET);
}

static inline u32 lcdc_read(struct shmob_drm_device *sdev, u32 reg)
{
	return ioread32(sdev->mmio + reg);
}

static inline int lcdc_wait_bit(struct shmob_drm_device *sdev, u32 reg,
				u32 mask, u32 until)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(5);

	while ((lcdc_read(sdev, reg) & mask) != until) {
		if (time_after(jiffies, timeout))
			return -ETIMEDOUT;
		cpu_relax();
	}

	return 0;
}

#endif /* __SHMOB_DRM_REGS_H__ */
