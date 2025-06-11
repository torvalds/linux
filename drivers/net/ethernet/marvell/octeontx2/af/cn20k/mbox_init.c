// SPDX-License-Identifier: GPL-2.0
/* Marvell RVU Admin Function driver
 *
 * Copyright (C) 2024 Marvell.
 *
 */

#include <linux/interrupt.h>
#include <linux/irq.h>

#include "rvu_trace.h"
#include "mbox.h"
#include "reg.h"
#include "api.h"

int cn20k_rvu_get_mbox_regions(struct rvu *rvu, void **mbox_addr,
			       int num, int type, unsigned long *pf_bmap)
{
	int region;
	u64 bar;

	for (region = 0; region < num; region++) {
		if (!test_bit(region, pf_bmap))
			continue;

		bar = (u64)phys_to_virt((u64)rvu->ng_rvu->pf_mbox_addr->base);
		bar += region * MBOX_SIZE;

		mbox_addr[region] = (void *)bar;

		if (!mbox_addr[region])
			return -ENOMEM;
	}
	return 0;
}

static int rvu_alloc_mbox_memory(struct rvu *rvu, int type,
				 int ndevs, int mbox_size)
{
	struct qmem *mbox_addr;
	dma_addr_t iova;
	int pf, err;

	/* Allocate contiguous memory for mailbox communication.
	 * eg: AF <=> PFx mbox memory
	 * This allocated memory is split into chunks of MBOX_SIZE
	 * and setup into each of the RVU PFs. In HW this memory will
	 * get aliased to an offset within BAR2 of those PFs.
	 *
	 * AF will access mbox memory using direct physical addresses
	 * and PFs will access the same shared memory from BAR2.
	 */

	err = qmem_alloc(rvu->dev, &mbox_addr, ndevs, mbox_size);
	if (err)
		return -ENOMEM;

	switch (type) {
	case TYPE_AFPF:
		rvu->ng_rvu->pf_mbox_addr = mbox_addr;
		iova = (u64)mbox_addr->iova;
		for (pf = 0; pf < ndevs; pf++) {
			rvu_write64(rvu, BLKADDR_RVUM, RVU_MBOX_AF_PFX_ADDR(pf),
				    (u64)iova);
			iova += mbox_size;
		}
		break;
	default:
		return 0;
	}

	return 0;
}

int cn20k_rvu_mbox_init(struct rvu *rvu, int type, int ndevs)
{
	int dev;

	if (!is_cn20k(rvu->pdev))
		return 0;

	for (dev = 0; dev < ndevs; dev++)
		rvu_write64(rvu, BLKADDR_RVUM,
			    RVU_MBOX_AF_PFX_CFG(dev), ilog2(MBOX_SIZE));

	return rvu_alloc_mbox_memory(rvu, type, ndevs, MBOX_SIZE);
}

void cn20k_free_mbox_memory(struct rvu *rvu)
{
	if (!is_cn20k(rvu->pdev))
		return;

	qmem_free(rvu->dev, rvu->ng_rvu->pf_mbox_addr);
}
