/*
 * arch/sh/drivers/dma/dma-sh.h
 *
 * Copyright (C) 2000  Takashi YOSHII
 * Copyright (C) 2003  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __DMA_SH_H
#define __DMA_SH_H

#include <cpu/dma.h>

/* Definitions for the SuperH DMAC */
#define REQ_L	0x00000000
#define REQ_E	0x00080000
#define RACK_H	0x00000000
#define RACK_L	0x00040000
#define ACK_R	0x00000000
#define ACK_W	0x00020000
#define ACK_H	0x00000000
#define ACK_L	0x00010000
#define DM_INC	0x00004000
#define DM_DEC	0x00008000
#define SM_INC	0x00001000
#define SM_DEC	0x00002000
#define RS_IN	0x00000200
#define RS_OUT	0x00000300
#define TS_BLK	0x00000040
#define CHCR_DE 0x00000001
#define CHCR_TE 0x00000002
#define CHCR_IE 0x00000004

/* DMAOR definitions */
#define DMAOR_AE	0x00000004
#define DMAOR_NMIF	0x00000002
#define DMAOR_DME	0x00000001

/*
 * Define the default configuration for dual address memory-memory transfer.
 * The 0x400 value represents auto-request, external->external.
 */
#define RS_DUAL	(DM_INC | SM_INC | 0x400 | TS_32)

#define MAX_DMAC_CHANNELS	(CONFIG_NR_ONCHIP_DMA_CHANNELS)

/*
 * Subtypes that have fewer channels than this simply need to change
 * CONFIG_NR_ONCHIP_DMA_CHANNELS. Likewise, subtypes with a larger number
 * of channels should expand on this.
 *
 * For most subtypes we can easily figure these values out with some
 * basic calculation, unfortunately on other subtypes these are more
 * scattered, so we just leave it unrolled for simplicity.
 */
#define SAR	((unsigned long[]){SH_DMAC_BASE + 0x00, SH_DMAC_BASE + 0x10, \
				   SH_DMAC_BASE + 0x20, SH_DMAC_BASE + 0x30, \
				   SH_DMAC_BASE + 0x50, SH_DMAC_BASE + 0x60})
#define DAR	((unsigned long[]){SH_DMAC_BASE + 0x04, SH_DMAC_BASE + 0x14, \
				   SH_DMAC_BASE + 0x24, SH_DMAC_BASE + 0x34, \
				   SH_DMAC_BASE + 0x54, SH_DMAC_BASE + 0x64})
#define DMATCR	((unsigned long[]){SH_DMAC_BASE + 0x08, SH_DMAC_BASE + 0x18, \
				   SH_DMAC_BASE + 0x28, SH_DMAC_BASE + 0x38, \
				   SH_DMAC_BASE + 0x58, SH_DMAC_BASE + 0x68})
#define CHCR	((unsigned long[]){SH_DMAC_BASE + 0x0c, SH_DMAC_BASE + 0x1c, \
				   SH_DMAC_BASE + 0x2c, SH_DMAC_BASE + 0x3c, \
				   SH_DMAC_BASE + 0x5c, SH_DMAC_BASE + 0x6c})

#define DMAOR	(SH_DMAC_BASE + 0x40)

#endif /* __DMA_SH_H */

