/*
 * Driver for the TXx9 SoC DMA Controller
 *
 * Copyright (C) 2009 Atsushi Nemoto
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef TXX9DMAC_H
#define TXX9DMAC_H

#include <linux/dmaengine.h>
#include <asm/txx9/dmac.h>

/*
 * Design Notes:
 *
 * This DMAC have four channels and one FIFO buffer.  Each channel can
 * be configured for memory-memory or device-memory transfer, but only
 * one channel can do alignment-free memory-memory transfer at a time
 * while the channel should occupy the FIFO buffer for effective
 * transfers.
 *
 * Instead of dynamically assign the FIFO buffer to channels, I chose
 * make one dedicated channel for memory-memory transfer.  The
 * dedicated channel is public.  Other channels are private and used
 * for slave transfer.  Some devices in the SoC are wired to certain
 * DMA channel.
 */

#ifdef CONFIG_MACH_TX49XX
static inline bool txx9_dma_have_SMPCHN(void)
{
	return true;
}
#define TXX9_DMA_USE_SIMPLE_CHAIN
#else
static inline bool txx9_dma_have_SMPCHN(void)
{
	return false;
}
#endif

#ifdef __LITTLE_ENDIAN
#ifdef CONFIG_MACH_TX49XX
#define CCR_LE	TXX9_DMA_CCR_LE
#define MCR_LE	0
#else
#define CCR_LE	0
#define MCR_LE	TXX9_DMA_MCR_LE
#endif
#else
#define CCR_LE	0
#define MCR_LE	0
#endif

/*
 * Redefine this macro to handle differences between 32- and 64-bit
 * addressing, big vs. little endian, etc.
 */
#ifdef __BIG_ENDIAN
#define TXX9_DMA_REG32(name)		u32 __pad_##name; u32 name
#else
#define TXX9_DMA_REG32(name)		u32 name; u32 __pad_##name
#endif

/* Hardware register definitions. */
struct txx9dmac_cregs {
#if defined(CONFIG_32BIT) && !defined(CONFIG_64BIT_PHYS_ADDR)
	TXX9_DMA_REG32(CHAR);	/* Chain Address Register */
#else
	u64 CHAR;		/* Chain Address Register */
#endif
	u64 SAR;		/* Source Address Register */
	u64 DAR;		/* Destination Address Register */
	TXX9_DMA_REG32(CNTR);	/* Count Register */
	TXX9_DMA_REG32(SAIR);	/* Source Address Increment Register */
	TXX9_DMA_REG32(DAIR);	/* Destination Address Increment Register */
	TXX9_DMA_REG32(CCR);	/* Channel Control Register */
	TXX9_DMA_REG32(CSR);	/* Channel Status Register */
};
struct txx9dmac_cregs32 {
	u32 CHAR;
	u32 SAR;
	u32 DAR;
	u32 CNTR;
	u32 SAIR;
	u32 DAIR;
	u32 CCR;
	u32 CSR;
};

struct txx9dmac_regs {
	/* per-channel registers */
	struct txx9dmac_cregs	CHAN[TXX9_DMA_MAX_NR_CHANNELS];
	u64	__pad[9];
	u64	MFDR;		/* Memory Fill Data Register */
	TXX9_DMA_REG32(MCR);	/* Master Control Register */
};
struct txx9dmac_regs32 {
	struct txx9dmac_cregs32	CHAN[TXX9_DMA_MAX_NR_CHANNELS];
	u32	__pad[9];
	u32	MFDR;
	u32	MCR;
};

/* bits for MCR */
#define TXX9_DMA_MCR_EIS(ch)	(0x10000000<<(ch))
#define TXX9_DMA_MCR_DIS(ch)	(0x01000000<<(ch))
#define TXX9_DMA_MCR_RSFIF	0x00000080
#define TXX9_DMA_MCR_FIFUM(ch)	(0x00000008<<(ch))
#define TXX9_DMA_MCR_LE		0x00000004
#define TXX9_DMA_MCR_RPRT	0x00000002
#define TXX9_DMA_MCR_MSTEN	0x00000001

/* bits for CCRn */
#define TXX9_DMA_CCR_IMMCHN	0x20000000
#define TXX9_DMA_CCR_USEXFSZ	0x10000000
#define TXX9_DMA_CCR_LE		0x08000000
#define TXX9_DMA_CCR_DBINH	0x04000000
#define TXX9_DMA_CCR_SBINH	0x02000000
#define TXX9_DMA_CCR_CHRST	0x01000000
#define TXX9_DMA_CCR_RVBYTE	0x00800000
#define TXX9_DMA_CCR_ACKPOL	0x00400000
#define TXX9_DMA_CCR_REQPL	0x00200000
#define TXX9_DMA_CCR_EGREQ	0x00100000
#define TXX9_DMA_CCR_CHDN	0x00080000
#define TXX9_DMA_CCR_DNCTL	0x00060000
#define TXX9_DMA_CCR_EXTRQ	0x00010000
#define TXX9_DMA_CCR_INTRQD	0x0000e000
#define TXX9_DMA_CCR_INTENE	0x00001000
#define TXX9_DMA_CCR_INTENC	0x00000800
#define TXX9_DMA_CCR_INTENT	0x00000400
#define TXX9_DMA_CCR_CHNEN	0x00000200
#define TXX9_DMA_CCR_XFACT	0x00000100
#define TXX9_DMA_CCR_SMPCHN	0x00000020
#define TXX9_DMA_CCR_XFSZ(order)	(((order) << 2) & 0x0000001c)
#define TXX9_DMA_CCR_XFSZ_1	TXX9_DMA_CCR_XFSZ(0)
#define TXX9_DMA_CCR_XFSZ_2	TXX9_DMA_CCR_XFSZ(1)
#define TXX9_DMA_CCR_XFSZ_4	TXX9_DMA_CCR_XFSZ(2)
#define TXX9_DMA_CCR_XFSZ_8	TXX9_DMA_CCR_XFSZ(3)
#define TXX9_DMA_CCR_XFSZ_X4	TXX9_DMA_CCR_XFSZ(4)
#define TXX9_DMA_CCR_XFSZ_X8	TXX9_DMA_CCR_XFSZ(5)
#define TXX9_DMA_CCR_XFSZ_X16	TXX9_DMA_CCR_XFSZ(6)
#define TXX9_DMA_CCR_XFSZ_X32	TXX9_DMA_CCR_XFSZ(7)
#define TXX9_DMA_CCR_MEMIO	0x00000002
#define TXX9_DMA_CCR_SNGAD	0x00000001

/* bits for CSRn */
#define TXX9_DMA_CSR_CHNEN	0x00000400
#define TXX9_DMA_CSR_STLXFER	0x00000200
#define TXX9_DMA_CSR_XFACT	0x00000100
#define TXX9_DMA_CSR_ABCHC	0x00000080
#define TXX9_DMA_CSR_NCHNC	0x00000040
#define TXX9_DMA_CSR_NTRNFC	0x00000020
#define TXX9_DMA_CSR_EXTDN	0x00000010
#define TXX9_DMA_CSR_CFERR	0x00000008
#define TXX9_DMA_CSR_CHERR	0x00000004
#define TXX9_DMA_CSR_DESERR	0x00000002
#define TXX9_DMA_CSR_SORERR	0x00000001

struct txx9dmac_chan {
	struct dma_chan		chan;
	struct dma_device	dma;
	struct txx9dmac_dev	*ddev;
	void __iomem		*ch_regs;
	struct tasklet_struct	tasklet;
	int			irq;
	u32			ccr;

	spinlock_t		lock;

	/* these other elements are all protected by lock */
	dma_cookie_t		completed;
	struct list_head	active_list;
	struct list_head	queue;
	struct list_head	free_list;

	unsigned int		descs_allocated;
};

struct txx9dmac_dev {
	void __iomem		*regs;
	struct tasklet_struct	tasklet;
	int			irq;
	struct txx9dmac_chan	*chan[TXX9_DMA_MAX_NR_CHANNELS];
	bool			have_64bit_regs;
	unsigned int		descsize;
};

static inline bool __is_dmac64(const struct txx9dmac_dev *ddev)
{
	return ddev->have_64bit_regs;
}

static inline bool is_dmac64(const struct txx9dmac_chan *dc)
{
	return __is_dmac64(dc->ddev);
}

#ifdef TXX9_DMA_USE_SIMPLE_CHAIN
/* Hardware descriptor definition. (for simple-chain) */
struct txx9dmac_hwdesc {
#if defined(CONFIG_32BIT) && !defined(CONFIG_64BIT_PHYS_ADDR)
	TXX9_DMA_REG32(CHAR);
#else
	u64 CHAR;
#endif
	u64 SAR;
	u64 DAR;
	TXX9_DMA_REG32(CNTR);
};
struct txx9dmac_hwdesc32 {
	u32 CHAR;
	u32 SAR;
	u32 DAR;
	u32 CNTR;
};
#else
#define txx9dmac_hwdesc txx9dmac_cregs
#define txx9dmac_hwdesc32 txx9dmac_cregs32
#endif

struct txx9dmac_desc {
	/* FIRST values the hardware uses */
	union {
		struct txx9dmac_hwdesc hwdesc;
		struct txx9dmac_hwdesc32 hwdesc32;
	};

	/* THEN values for driver housekeeping */
	struct list_head		desc_node ____cacheline_aligned;
	struct dma_async_tx_descriptor	txd;
	size_t				len;
};

#ifdef TXX9_DMA_USE_SIMPLE_CHAIN

static inline bool txx9dmac_chan_INTENT(struct txx9dmac_chan *dc)
{
	return (dc->ccr & TXX9_DMA_CCR_INTENT) != 0;
}

static inline void txx9dmac_chan_set_INTENT(struct txx9dmac_chan *dc)
{
	dc->ccr |= TXX9_DMA_CCR_INTENT;
}

static inline void txx9dmac_desc_set_INTENT(struct txx9dmac_dev *ddev,
					    struct txx9dmac_desc *desc)
{
}

static inline void txx9dmac_chan_set_SMPCHN(struct txx9dmac_chan *dc)
{
	dc->ccr |= TXX9_DMA_CCR_SMPCHN;
}

static inline void txx9dmac_desc_set_nosimple(struct txx9dmac_dev *ddev,
					      struct txx9dmac_desc *desc,
					      u32 sair, u32 dair, u32 ccr)
{
}

#else /* TXX9_DMA_USE_SIMPLE_CHAIN */

static inline bool txx9dmac_chan_INTENT(struct txx9dmac_chan *dc)
{
	return true;
}

static void txx9dmac_chan_set_INTENT(struct txx9dmac_chan *dc)
{
}

static inline void txx9dmac_desc_set_INTENT(struct txx9dmac_dev *ddev,
					    struct txx9dmac_desc *desc)
{
	if (__is_dmac64(ddev))
		desc->hwdesc.CCR |= TXX9_DMA_CCR_INTENT;
	else
		desc->hwdesc32.CCR |= TXX9_DMA_CCR_INTENT;
}

static inline void txx9dmac_chan_set_SMPCHN(struct txx9dmac_chan *dc)
{
}

static inline void txx9dmac_desc_set_nosimple(struct txx9dmac_dev *ddev,
					      struct txx9dmac_desc *desc,
					      u32 sai, u32 dai, u32 ccr)
{
	if (__is_dmac64(ddev)) {
		desc->hwdesc.SAIR = sai;
		desc->hwdesc.DAIR = dai;
		desc->hwdesc.CCR = ccr;
	} else {
		desc->hwdesc32.SAIR = sai;
		desc->hwdesc32.DAIR = dai;
		desc->hwdesc32.CCR = ccr;
	}
}

#endif /* TXX9_DMA_USE_SIMPLE_CHAIN */

#endif /* TXX9DMAC_H */
