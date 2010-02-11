/*
 * arch/sh/include/asm/dma-sh.h
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

#include <asm/dma.h>
#include <cpu/dma.h>

/* DMAOR contorl: The DMAOR access size is different by CPU.*/
#if defined(CONFIG_CPU_SUBTYPE_SH7723)	|| \
    defined(CONFIG_CPU_SUBTYPE_SH7724)	|| \
    defined(CONFIG_CPU_SUBTYPE_SH7780)	|| \
    defined(CONFIG_CPU_SUBTYPE_SH7785)
#define dmaor_read_reg(n) \
    (n ? __raw_readw(SH_DMAC_BASE1 + DMAOR) \
	: __raw_readw(SH_DMAC_BASE0 + DMAOR))
#define dmaor_write_reg(n, data) \
    (n ? __raw_writew(data, SH_DMAC_BASE1 + DMAOR) \
    : __raw_writew(data, SH_DMAC_BASE0 + DMAOR))
#else /* Other CPU */
#define dmaor_read_reg(n) __raw_readw(SH_DMAC_BASE0 + DMAOR)
#define dmaor_write_reg(n, data) __raw_writew(data, SH_DMAC_BASE0 + DMAOR)
#endif

static int dmte_irq_map[] __maybe_unused = {
#if (MAX_DMA_CHANNELS >= 4)
    DMTE0_IRQ,
    DMTE0_IRQ + 1,
    DMTE0_IRQ + 2,
    DMTE0_IRQ + 3,
#endif
#if (MAX_DMA_CHANNELS >= 6)
    DMTE4_IRQ,
    DMTE4_IRQ + 1,
#endif
#if (MAX_DMA_CHANNELS >= 8)
    DMTE6_IRQ,
    DMTE6_IRQ + 1,
#endif
#if (MAX_DMA_CHANNELS >= 12)
    DMTE8_IRQ,
    DMTE9_IRQ,
    DMTE10_IRQ,
    DMTE11_IRQ,
#endif
};

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
#define DM_FIX	0x0000c000
#define SM_INC	0x00001000
#define SM_DEC	0x00002000
#define SM_FIX	0x00003000
#define RS_IN	0x00000200
#define RS_OUT	0x00000300
#define TS_BLK	0x00000040
#define TM_BUR	0x00000020
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
#define RS_DUAL	(DM_INC | SM_INC | 0x400 | TS_INDEX2VAL(XMIT_SZ_32BIT))

/* DMA base address */
static u32 dma_base_addr[] __maybe_unused = {
#if (MAX_DMA_CHANNELS >= 4)
	SH_DMAC_BASE0 + 0x00,	/* channel 0 */
	SH_DMAC_BASE0 + 0x10,
	SH_DMAC_BASE0 + 0x20,
	SH_DMAC_BASE0 + 0x30,
#endif
#if (MAX_DMA_CHANNELS >= 6)
	SH_DMAC_BASE0 + 0x50,
	SH_DMAC_BASE0 + 0x60,
#endif
#if (MAX_DMA_CHANNELS >= 8)
	SH_DMAC_BASE1 + 0x00,
	SH_DMAC_BASE1 + 0x10,
#endif
#if (MAX_DMA_CHANNELS >= 12)
	SH_DMAC_BASE1 + 0x20,
	SH_DMAC_BASE1 + 0x30,
	SH_DMAC_BASE1 + 0x50,
	SH_DMAC_BASE1 + 0x60, /* channel 11 */
#endif
};

/* DMA register */
#define SAR     0x00
#define DAR     0x04
#define TCR     0x08
#define CHCR    0x0C
#define DMAOR	0x40

/*
 * for dma engine
 *
 * SuperH DMA mode
 */
#define SHDMA_MIX_IRQ	(1 << 1)
#define SHDMA_DMAOR1	(1 << 2)
#define SHDMA_DMAE1	(1 << 3)

enum sh_dmae_slave_chan_id {
	SHDMA_SLAVE_SCIF0_TX,
	SHDMA_SLAVE_SCIF0_RX,
	SHDMA_SLAVE_SCIF1_TX,
	SHDMA_SLAVE_SCIF1_RX,
	SHDMA_SLAVE_SCIF2_TX,
	SHDMA_SLAVE_SCIF2_RX,
	SHDMA_SLAVE_SCIF3_TX,
	SHDMA_SLAVE_SCIF3_RX,
	SHDMA_SLAVE_SCIF4_TX,
	SHDMA_SLAVE_SCIF4_RX,
	SHDMA_SLAVE_SCIF5_TX,
	SHDMA_SLAVE_SCIF5_RX,
	SHDMA_SLAVE_SIUA_TX,
	SHDMA_SLAVE_SIUA_RX,
	SHDMA_SLAVE_SIUB_TX,
	SHDMA_SLAVE_SIUB_RX,
	SHDMA_SLAVE_NUMBER,	/* Must stay last */
};

struct sh_dmae_slave_config {
	enum sh_dmae_slave_chan_id	slave_id;
	dma_addr_t			addr;
	u32				chcr;
	char				mid_rid;
};

struct sh_dmae_channel {
	unsigned int	offset;
	unsigned int	dmars;
	unsigned int	dmars_bit;
};

struct sh_dmae_pdata {
	struct sh_dmae_slave_config *slave;
	int slave_num;
	struct sh_dmae_channel *channel;
	int channel_num;
};

struct device;

struct sh_dmae_slave {
	enum sh_dmae_slave_chan_id	slave_id; /* Set by the platform */
	struct device			*dma_dev; /* Set by the platform */
	struct sh_dmae_slave_config	*config;  /* Set by the driver */
};

#endif /* __DMA_SH_H */
