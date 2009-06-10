/* dma.h: FRV DMA controller management
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_DMA_H
#define _ASM_DMA_H

//#define DMA_DEBUG 1

#include <linux/interrupt.h>

#undef MAX_DMA_CHANNELS		/* don't use kernel/dma.c */

/* under 2.4 this is actually needed by the new bootmem allocator */
#define MAX_DMA_ADDRESS		PAGE_OFFSET

/*
 * FRV DMA controller management
 */
typedef irqreturn_t (*dma_irq_handler_t)(int dmachan, unsigned long cstr, void *data);

extern void frv_dma_init(void);

extern int frv_dma_open(const char *devname,
			unsigned long dmamask,
			int dmacap,
			dma_irq_handler_t handler,
			unsigned long irq_flags,
			void *data);

/* channels required */
#define FRV_DMA_MASK_ANY	ULONG_MAX	/* any channel */

/* capabilities required */
#define FRV_DMA_CAP_DREQ	0x01		/* DMA request pin */
#define FRV_DMA_CAP_DACK	0x02		/* DMA ACK pin */
#define FRV_DMA_CAP_DONE	0x04		/* DMA done pin */

extern void frv_dma_close(int dma);

extern void frv_dma_config(int dma, unsigned long ccfr, unsigned long cctr, unsigned long apr);

extern void frv_dma_start(int dma,
			  unsigned long sba, unsigned long dba,
			  unsigned long pix, unsigned long six, unsigned long bcl);

extern void frv_dma_restart_circular(int dma, unsigned long six);

extern void frv_dma_stop(int dma);

extern int is_frv_dma_interrupting(int dma);

extern void frv_dma_dump(int dma);

extern void frv_dma_status_clear(int dma);

#define FRV_DMA_NCHANS	8
#define FRV_DMA_4CHANS	4
#define FRV_DMA_8CHANS	8

#define DMAC_CCFRx		0x00	/* channel configuration reg */
#define DMAC_CCFRx_CM_SHIFT	16
#define DMAC_CCFRx_CM_DA	0x00000000
#define DMAC_CCFRx_CM_SCA	0x00010000
#define DMAC_CCFRx_CM_DCA	0x00020000
#define DMAC_CCFRx_CM_2D	0x00030000
#define DMAC_CCFRx_ATS_SHIFT	8
#define DMAC_CCFRx_RS_INTERN	0x00000000
#define DMAC_CCFRx_RS_EXTERN	0x00000001
#define DMAC_CCFRx_RS_SHIFT	0

#define DMAC_CSTRx		0x08	/* channel status reg */
#define DMAC_CSTRx_FS		0x0000003f
#define DMAC_CSTRx_NE		0x00000100
#define DMAC_CSTRx_FED		0x00000200
#define DMAC_CSTRx_WER		0x00000800
#define DMAC_CSTRx_RER		0x00001000
#define DMAC_CSTRx_CE		0x00002000
#define DMAC_CSTRx_INT		0x00800000
#define DMAC_CSTRx_BUSY		0x80000000

#define DMAC_CCTRx		0x10	/* channel control reg */
#define DMAC_CCTRx_DSIZ_1	0x00000000
#define DMAC_CCTRx_DSIZ_2	0x00000001
#define DMAC_CCTRx_DSIZ_4	0x00000002
#define DMAC_CCTRx_DSIZ_32	0x00000005
#define DMAC_CCTRx_DAU_HOLD	0x00000000
#define DMAC_CCTRx_DAU_INC	0x00000010
#define DMAC_CCTRx_DAU_DEC	0x00000020
#define DMAC_CCTRx_SSIZ_1	0x00000000
#define DMAC_CCTRx_SSIZ_2	0x00000100
#define DMAC_CCTRx_SSIZ_4	0x00000200
#define DMAC_CCTRx_SSIZ_32	0x00000500
#define DMAC_CCTRx_SAU_HOLD	0x00000000
#define DMAC_CCTRx_SAU_INC	0x00001000
#define DMAC_CCTRx_SAU_DEC	0x00002000
#define DMAC_CCTRx_FC		0x08000000
#define DMAC_CCTRx_ICE		0x10000000
#define DMAC_CCTRx_IE		0x40000000
#define DMAC_CCTRx_ACT		0x80000000

#define DMAC_SBAx		0x18	/* source base address reg */
#define DMAC_DBAx		0x20	/* data base address reg */
#define DMAC_PIXx		0x28	/* primary index reg */
#define DMAC_SIXx		0x30	/* secondary index reg */
#define DMAC_BCLx		0x38	/* byte count limit reg */
#define DMAC_APRx		0x40	/* alternate pointer reg */

/*
 * required for PCI + MODULES
 */
#ifdef CONFIG_PCI
extern int isa_dma_bridge_buggy;
#else
#define isa_dma_bridge_buggy 	(0)
#endif

#endif /* _ASM_DMA_H */
