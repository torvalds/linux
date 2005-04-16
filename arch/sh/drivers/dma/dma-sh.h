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
#define TM_BURST 0x0000080
#define TS_8	0x00000010
#define TS_16	0x00000020
#define TS_32	0x00000030
#define TS_64	0x00000000
#define TS_BLK	0x00000040
#define CHCR_DE 0x00000001
#define CHCR_TE 0x00000002
#define CHCR_IE 0x00000004

/* Define the default configuration for dual address memory-memory transfer.
 * The 0x400 value represents auto-request, external->external.
 */
#define RS_DUAL	(DM_INC | SM_INC | 0x400 | TS_32)

#define DMAOR_COD	0x00000008
#define DMAOR_AE	0x00000004
#define DMAOR_NMIF	0x00000002
#define DMAOR_DME	0x00000001

#define MAX_DMAC_CHANNELS	(CONFIG_NR_ONCHIP_DMA_CHANNELS)

#endif /* __DMA_SH_H */

