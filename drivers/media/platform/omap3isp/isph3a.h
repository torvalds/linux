/*
 * isph3a.h
 *
 * TI OMAP3 ISP - H3A AF module
 *
 * Copyright (C) 2010 Nokia Corporation
 * Copyright (C) 2009 Texas Instruments, Inc.
 *
 * Contacts: David Cohen <dacohen@gmail.com>
 *	     Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	     Sakari Ailus <sakari.ailus@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#ifndef OMAP3_ISP_H3A_H
#define OMAP3_ISP_H3A_H

#include <linux/omap3isp.h>

/*
 * ----------
 * -H3A AEWB-
 * ----------
 */

#define AEWB_PACKET_SIZE	16
#define AEWB_SATURATION_LIMIT	0x3ff

/* Flags for changed registers */
#define PCR_CHNG		(1 << 0)
#define AEWWIN1_CHNG		(1 << 1)
#define AEWINSTART_CHNG		(1 << 2)
#define AEWINBLK_CHNG		(1 << 3)
#define AEWSUBWIN_CHNG		(1 << 4)
#define PRV_WBDGAIN_CHNG	(1 << 5)
#define PRV_WBGAIN_CHNG		(1 << 6)

/* ISPH3A REGISTERS bits */
#define ISPH3A_PCR_AF_EN	(1 << 0)
#define ISPH3A_PCR_AF_ALAW_EN	(1 << 1)
#define ISPH3A_PCR_AF_MED_EN	(1 << 2)
#define ISPH3A_PCR_AF_BUSY	(1 << 15)
#define ISPH3A_PCR_AEW_EN	(1 << 16)
#define ISPH3A_PCR_AEW_ALAW_EN	(1 << 17)
#define ISPH3A_PCR_AEW_BUSY	(1 << 18)
#define ISPH3A_PCR_AEW_MASK	(ISPH3A_PCR_AEW_ALAW_EN | \
				 ISPH3A_PCR_AEW_AVE2LMT_MASK)

/*
 * --------
 * -H3A AF-
 * --------
 */

/* Peripheral Revision */
#define AFPID				0x0

#define AFCOEF_OFFSET			0x00000004	/* COEF base address */

/* PCR fields */
#define AF_BUSYAF			(1 << 15)
#define AF_FVMODE			(1 << 14)
#define AF_RGBPOS			(0x7 << 11)
#define AF_MED_TH			(0xFF << 3)
#define AF_MED_EN			(1 << 2)
#define AF_ALAW_EN			(1 << 1)
#define AF_EN				(1 << 0)
#define AF_PCR_MASK			(AF_FVMODE | AF_RGBPOS | AF_MED_TH | \
					 AF_MED_EN | AF_ALAW_EN)

/* AFPAX1 fields */
#define AF_PAXW				(0x7F << 16)
#define AF_PAXH				0x7F

/* AFPAX2 fields */
#define AF_AFINCV			(0xF << 13)
#define AF_PAXVC			(0x7F << 6)
#define AF_PAXHC			0x3F

/* AFPAXSTART fields */
#define AF_PAXSH			(0xFFF<<16)
#define AF_PAXSV			0xFFF

/* COEFFICIENT MASK */
#define AF_COEF_MASK0			0xFFF
#define AF_COEF_MASK1			(0xFFF<<16)

/* BIT SHIFTS */
#define AF_RGBPOS_SHIFT			11
#define AF_MED_TH_SHIFT			3
#define AF_PAXW_SHIFT			16
#define AF_LINE_INCR_SHIFT		13
#define AF_VT_COUNT_SHIFT		6
#define AF_HZ_START_SHIFT		16
#define AF_COEF_SHIFT			16

/* Init and cleanup functions */
int omap3isp_h3a_aewb_init(struct isp_device *isp);
int omap3isp_h3a_af_init(struct isp_device *isp);

void omap3isp_h3a_aewb_cleanup(struct isp_device *isp);
void omap3isp_h3a_af_cleanup(struct isp_device *isp);

#endif /* OMAP3_ISP_H3A_H */
