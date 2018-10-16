/* SPDX-License-Identifier: GPL-2.0
 * Marvell OcteonTx2 RVU Admin Function driver
 *
 * Copyright (C) 2018 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef COMMON_H
#define COMMON_H

#include "rvu_struct.h"

#define OTX2_ALIGN			128  /* Align to cacheline */

#define Q_SIZE_16		0ULL /* 16 entries */
#define Q_SIZE_64		1ULL /* 64 entries */
#define Q_SIZE_256		2ULL
#define Q_SIZE_1K		3ULL
#define Q_SIZE_4K		4ULL
#define Q_SIZE_16K		5ULL
#define Q_SIZE_64K		6ULL
#define Q_SIZE_256K		7ULL
#define Q_SIZE_1M		8ULL /* Million entries */
#define Q_SIZE_MIN		Q_SIZE_16
#define Q_SIZE_MAX		Q_SIZE_1M

#define Q_COUNT(x)		(16ULL << (2 * x))
#define Q_SIZE(x, n)		((ilog2(x) - (n)) / 2)

/* Admin queue info */

/* Since we intend to add only one instruction at a time,
 * keep queue size to it's minimum.
 */
#define AQ_SIZE			Q_SIZE_16
/* HW head & tail pointer mask */
#define AQ_PTR_MASK		0xFFFFF

struct qmem {
	void            *base;
	dma_addr_t	iova;
	int		alloc_sz;
	u8		entry_sz;
	u8		align;
	u32		qsize;
};

static inline int qmem_alloc(struct device *dev, struct qmem **q,
			     int qsize, int entry_sz)
{
	struct qmem *qmem;
	int aligned_addr;

	if (!qsize)
		return -EINVAL;

	*q = devm_kzalloc(dev, sizeof(*qmem), GFP_KERNEL);
	if (!*q)
		return -ENOMEM;
	qmem = *q;

	qmem->entry_sz = entry_sz;
	qmem->alloc_sz = (qsize * entry_sz) + OTX2_ALIGN;
	qmem->base = dma_zalloc_coherent(dev, qmem->alloc_sz,
					 &qmem->iova, GFP_KERNEL);
	if (!qmem->base)
		return -ENOMEM;

	qmem->qsize = qsize;

	aligned_addr = ALIGN((u64)qmem->iova, OTX2_ALIGN);
	qmem->align = (aligned_addr - qmem->iova);
	qmem->base += qmem->align;
	qmem->iova += qmem->align;
	return 0;
}

static inline void qmem_free(struct device *dev, struct qmem *qmem)
{
	if (!qmem)
		return;

	if (qmem->base)
		dma_free_coherent(dev, qmem->alloc_sz,
				  qmem->base - qmem->align,
				  qmem->iova - qmem->align);
	devm_kfree(dev, qmem);
}

struct admin_queue {
	struct qmem	*inst;
	struct qmem	*res;
	spinlock_t	lock; /* Serialize inst enqueue from PFs */
};

#endif /* COMMON_H */
