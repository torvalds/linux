// SPDX-License-Identifier: GPL-2.0+
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

struct mite_dma_desc {
	__le32 count;
	__le32 addr;
	__le32 next;
	u32 dar;
};

struct mite_ring {
	struct device *hw_dev;
	unsigned int n_links;
	struct mite_dma_desc *descs;
	dma_addr_t dma_addr;
};

struct mite_channel {
	struct mite *mite;
	unsigned int channel;
	int dir;
	int done;
	struct mite_ring *ring;
};

struct mite {
	struct pci_dev *pcidev;
	void __iomem *mmio;
	struct mite_channel channels[MAX_MITE_DMA_CHANNELS];
	int num_channels;
	unsigned int fifo_size;
	/* protects mite_channel from being released by the driver */
	spinlock_t lock;
};

u32 mite_bytes_in_transit(struct mite_channel *mite_chan);

void mite_sync_dma(struct mite_channel *mite_chan, struct comedi_subdevice *s);
void mite_ack_linkc(struct mite_channel *mite_chan, struct comedi_subdevice *s,
		    bool sync);
int mite_done(struct mite_channel *mite_chan);

void mite_dma_arm(struct mite_channel *mite_chan);
void mite_dma_disarm(struct mite_channel *mite_chan);

void mite_prep_dma(struct mite_channel *mite_chan,
		   unsigned int num_device_bits, unsigned int num_memory_bits);

struct mite_channel *mite_request_channel_in_range(struct mite *mite,
						   struct mite_ring *ring,
						   unsigned int min_channel,
						   unsigned int max_channel);
struct mite_channel *mite_request_channel(struct mite *mite,
					  struct mite_ring *ring);
void mite_release_channel(struct mite_channel *mite_chan);

int mite_init_ring_descriptors(struct mite_ring *ring,
			       struct comedi_subdevice *s, unsigned int nbytes);
int mite_buf_change(struct mite_ring *ring, struct comedi_subdevice *s);

struct mite_ring *mite_alloc_ring(struct mite *mite);
void mite_free_ring(struct mite_ring *ring);

struct mite *mite_attach(struct comedi_device *dev, bool use_win1);
void mite_detach(struct mite *mite);

/*
 * Mite registers (used outside of the mite driver)
 */
#define MITE_IODWBSR		0xc0	/* IO Device Window Base Size */
#define MITE_IODWBSR_1		0xc4	/* IO Device Window1 Base Size */
#define WENAB			BIT(7)	/* window enable */
#define MITE_IODWCR_1		0xf4

#endif
