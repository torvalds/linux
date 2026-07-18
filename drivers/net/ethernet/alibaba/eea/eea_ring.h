/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Driver for Alibaba Elastic Ethernet Adapter.
 *
 * Copyright (C) 2025 Alibaba Inc.
 */

#ifndef __EEA_RING_H__
#define __EEA_RING_H__

#include <linux/dma-mapping.h>
#include "eea_desc.h"

#define EEA_RING_DESC_F_MORE		BIT(0)
#define EEA_RING_DESC_F_CQ_PHASE	BIT(7)

/* These two values define the bounds for the queue depth returned by the
 * hardware.
 */
#define EEA_NET_IO_HW_RING_DEPTH_MAX (32 * 1024)
#define EEA_NET_IO_HW_RING_DEPTH_MIN 128

/* This value constrains the minimum queue depth that the driver configures for
 * the hardware, which typically applies to user-provided settings. Naturally,
 * the configured depth must also not exceed the maximum capacity supported by
 * the hardware.
 */
#define EEA_NET_IO_RING_DEPTH_MIN 64

struct eea_common_desc {
	__le16 flags;
	__le16 id;
};

struct eea_device;

struct eea_ring_sq {
	void *desc;

	u16 head;
	u16 hw_idx;

	u16 shadow_idx;
	__le16 shadow_id;
	u16 shadow_num;

	u8 desc_size;
	u8 desc_size_shift;

	dma_addr_t dma_addr;
	u32 dma_size;
};

struct eea_ring_cq {
	void *desc;

	u16 head;
	u16 hw_idx;

	u8 phase;
	u8 desc_size_shift;
	u8 desc_size;

	dma_addr_t dma_addr;
	u32 dma_size;
};

struct eea_ring {
	const char *name;
	struct eea_device *edev;
	u32 index;
	void __iomem *db;
	u16 msix_vec;

	u32 num;

	u32 num_free;

	struct eea_ring_sq sq;
	struct eea_ring_cq cq;
};

struct eea_ring *eea_ering_alloc(u32 index, u32 num, struct eea_device *edev,
				 u8 sq_desc_size, u8 cq_desc_size,
				 const char *name);
void eea_ering_free(struct eea_ring *ering);
void eea_ering_kick(struct eea_ring *ering);

void *eea_ering_sq_alloc_desc(struct eea_ring *ering, u16 id,
			      bool is_last, u16 flags);
void *eea_ering_aq_alloc_desc(struct eea_ring *ering);
void eea_ering_sq_commit_desc(struct eea_ring *ering);
void eea_ering_sq_cancel(struct eea_ring *ering);

void eea_ering_cq_ack_desc(struct eea_ring *ering, u32 num);

void eea_ering_irq_active(struct eea_ring *ering, struct eea_ring *tx_ering);
void *eea_ering_cq_get_desc(const struct eea_ring *ering);
#endif
