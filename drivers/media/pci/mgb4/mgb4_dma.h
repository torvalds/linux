/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021-2023 Digiteq Automotive
 *     author: Martin Tuma <martin.tuma@digiteqautomotive.com>
 */

#ifndef __MGB4_DMA_H__
#define __MGB4_DMA_H__

#include "mgb4_core.h"

int mgb4_dma_channel_init(struct mgb4_dev *mgbdev);
void mgb4_dma_channel_free(struct mgb4_dev *mgbdev);

int mgb4_dma_transfer(struct mgb4_dev *mgbdev, u32 channel, bool write,
		      u64 paddr, struct sg_table *sgt);

#endif
