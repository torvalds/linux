// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2020 Hewlett Packard Enterprise, Inc. All rights reserved.
 */

#include "rxe.h"

int rxe_alloc_mw(struct ib_mw *ibmw, struct ib_udata *udata)
{
	struct rxe_mw *mw = to_rmw(ibmw);
	struct rxe_pd *pd = to_rpd(ibmw->pd);
	struct rxe_dev *rxe = to_rdev(ibmw->device);
	int ret;

	rxe_add_ref(pd);

	ret = rxe_add_to_pool(&rxe->mw_pool, mw);
	if (ret) {
		rxe_drop_ref(pd);
		return ret;
	}

	rxe_add_index(mw);
	ibmw->rkey = (mw->pelem.index << 8) | rxe_get_next_key(-1);
	mw->state = (mw->ibmw.type == IB_MW_TYPE_2) ?
			RXE_MW_STATE_FREE : RXE_MW_STATE_VALID;
	spin_lock_init(&mw->lock);

	return 0;
}

int rxe_dealloc_mw(struct ib_mw *ibmw)
{
	struct rxe_mw *mw = to_rmw(ibmw);
	struct rxe_pd *pd = to_rpd(ibmw->pd);
	unsigned long flags;

	spin_lock_irqsave(&mw->lock, flags);
	mw->state = RXE_MW_STATE_INVALID;
	spin_unlock_irqrestore(&mw->lock, flags);

	rxe_drop_ref(mw);
	rxe_drop_ref(pd);

	return 0;
}

void rxe_mw_cleanup(struct rxe_pool_entry *elem)
{
	struct rxe_mw *mw = container_of(elem, typeof(*mw), pelem);

	rxe_drop_index(mw);
}
