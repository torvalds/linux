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

enum mite_registers {
	MITE_IODWBSR = 0xc0,	/* IO Device Window Base Size Register */
	MITE_IODWBSR_1 = 0xc4,	/* IO Device Window Base Size Register 1 */
	MITE_IODWCR_1 = 0xf4,
};

enum MITE_IODWBSR_bits {
	WENAB = 0x80,		/* window enable */
};

enum CHSR_bits {
	CHSR_INT = (1 << 31),
	CHSR_LPAUSES = (1 << 29),
	CHSR_SARS = (1 << 27),
	CHSR_DONE = (1 << 25),
	CHSR_MRDY = (1 << 23),
	CHSR_DRDY = (1 << 21),
	CHSR_LINKC = (1 << 19),
	CHSR_CONTS_RB = (1 << 17),
	CHSR_ERROR = (1 << 15),
	CHSR_SABORT = (1 << 14),
	CHSR_HABORT = (1 << 13),
	CHSR_STOPS = (1 << 12),
	CHSR_OPERR_MASK = (3 << 10),
	CHSR_OPERR_NOERROR = (0 << 10),
	CHSR_OPERR_FIFOERROR = (1 << 10),
	CHSR_OPERR_LINKERROR = (1 << 10),	/* ??? */
	CHSR_XFERR = (1 << 9),
	CHSR_END = (1 << 8),
	CHSR_DRQ1 = (1 << 7),
	CHSR_DRQ0 = (1 << 6),
	CHSR_LERR_MASK = (3 << 4),
	CHSR_LBERR = (1 << 4),
	CHSR_LRERR = (2 << 4),
	CHSR_LOERR = (3 << 4),
	CHSR_MERR_MASK = (3 << 2),
	CHSR_MBERR = (1 << 2),
	CHSR_MRERR = (2 << 2),
	CHSR_MOERR = (3 << 2),
	CHSR_DERR_MASK = (3 << 0),
	CHSR_DBERR = (1 << 0),
	CHSR_DRERR = (2 << 0),
	CHSR_DOERR = (3 << 0),
};

#endif
