/*
 * module/mite.h
 * Hardware driver for NI Mite PCI interface chip
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 1999 David A. Schleef <ds@schleef.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _MITE_H_
#define _MITE_H_

#include <linux/spinlock.h>

#define MAX_MITE_DMA_CHANNELS 8

struct comedi_device;
struct comedi_subdevice;
struct device;
struct pci_dev;

struct mite_dma_descriptor {
	__le32 count;
	__le32 addr;
	__le32 next;
	u32 dar;
};

struct mite_dma_descriptor_ring {
	struct device *hw_dev;
	unsigned int n_links;
	struct mite_dma_descriptor *descriptors;
	dma_addr_t descriptors_dma_addr;
};

struct mite_channel {
	struct mite_struct *mite;
	unsigned int channel;
	int dir;
	int done;
	struct mite_dma_descriptor_ring *ring;
};

struct mite_struct {
	struct pci_dev *pcidev;
	void __iomem *mite_io_addr;
	struct mite_channel channels[MAX_MITE_DMA_CHANNELS];
	short channel_allocated[MAX_MITE_DMA_CHANNELS];
	int num_channels;
	unsigned int fifo_size;
	/* protects mite_channel from being released by the driver */
	spinlock_t lock;
};

struct mite_struct *mite_alloc(struct pci_dev *pcidev);

int mite_setup2(struct comedi_device *, struct mite_struct *, bool use_win1);

static inline int mite_setup(struct comedi_device *dev,
			     struct mite_struct *mite)
{
	return mite_setup2(dev, mite, false);
}

void mite_detach(struct mite_struct *mite);
struct mite_dma_descriptor_ring *mite_alloc_ring(struct mite_struct *mite);
void mite_free_ring(struct mite_dma_descriptor_ring *ring);
struct mite_channel *
mite_request_channel_in_range(struct mite_struct *mite,
			      struct mite_dma_descriptor_ring *ring,
			      unsigned int min_channel,
			      unsigned int max_channel);
static inline struct mite_channel *
mite_request_channel(struct mite_struct *mite,
		     struct mite_dma_descriptor_ring *ring)
{
	return mite_request_channel_in_range(mite, ring, 0,
					     mite->num_channels - 1);
}

void mite_release_channel(struct mite_channel *mite_chan);

void mite_dma_arm(struct mite_channel *mite_chan);
void mite_dma_disarm(struct mite_channel *mite_chan);
void mite_sync_dma(struct mite_channel *mite_chan, struct comedi_subdevice *s);
u32 mite_bytes_in_transit(struct mite_channel *mite_chan);
unsigned int mite_ack_linkc(struct mite_channel *mite_chan);
int mite_done(struct mite_channel *mite_chan);

void mite_prep_dma(struct mite_channel *mite_chan,
		   unsigned int num_device_bits, unsigned int num_memory_bits);
int mite_buf_change(struct mite_dma_descriptor_ring *ring,
		    struct comedi_subdevice *s);
int mite_init_ring_descriptors(struct mite_dma_descriptor_ring *ring,
			       struct comedi_subdevice *s,
			       unsigned int nbytes);

/*
 * Mite registers (used outside of the mite driver)
 */
#define MITE_IODWBSR		0xc0	/* IO Device Window Base Size */
#define MITE_IODWBSR_1		0xc4	/* IO Device Window1 Base Size */
#define WENAB			BIT(7)	/* window enable */
#define MITE_IODWCR_1		0xf4

#define CHSR_INT		BIT(31)
#define CHSR_LPAUSES		BIT(29)
#define CHSR_SARS		BIT(27)
#define CHSR_DONE		BIT(25)
#define CHSR_MRDY		BIT(23)
#define CHSR_DRDY		BIT(21)
#define CHSR_LINKC		BIT(19)
#define CHSR_CONTS_RB		BIT(17)
#define CHSR_ERROR		BIT(15)
#define CHSR_SABORT		BIT(14)
#define CHSR_HABORT		BIT(13)
#define CHSR_STOPS		BIT(12)
#define CHSR_OPERR(x)		(((x) & 0x3) << 10)
#define CHSR_OPERR_MASK		CHSR_OPERR(3)
#define CHSR_OPERR_NOERROR	CHSR_OPERR(0)
#define CHSR_OPERR_FIFOERROR	CHSR_OPERR(1)
#define CHSR_OPERR_LINKERROR	CHSR_OPERR(1)	/* ??? */
#define CHSR_XFERR		BIT(9)
#define CHSR_END		BIT(8)
#define CHSR_DRQ1		BIT(7)
#define CHSR_DRQ0		BIT(6)
#define CHSR_LERR(x)		(((x) & 0x3) << 4)
#define CHSR_LERR_MASK		CHSR_LERR(3)
#define CHSR_LBERR		CHSR_LERR(1)
#define CHSR_LRERR		CHSR_LERR(2)
#define CHSR_LOERR		CHSR_LERR(3)
#define CHSR_MERR(x)		(((x) & 0x3) << 2)
#define CHSR_MERR_MASK		CHSR_MERR(3)
#define CHSR_MBERR		CHSR_MERR(1)
#define CHSR_MRERR		CHSR_MERR(2)
#define CHSR_MOERR		CHSR_MERR(3)
#define CHSR_DERR(x)		(((x) & 0x3) << 0)
#define CHSR_DERR_MASK		CHSR_DERR(3)
#define CHSR_DBERR		CHSR_DERR(1)
#define CHSR_DRERR		CHSR_DERR(2)
#define CHSR_DOERR		CHSR_DERR(3)

#endif
