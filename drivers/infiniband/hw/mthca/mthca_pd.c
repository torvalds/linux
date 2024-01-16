/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Cisco Systems.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/errno.h>

#include "mthca_dev.h"

int mthca_pd_alloc(struct mthca_dev *dev, int privileged, struct mthca_pd *pd)
{
	int err = 0;

	pd->privileged = privileged;

	atomic_set(&pd->sqp_count, 0);
	pd->pd_num = mthca_alloc(&dev->pd_table.alloc);
	if (pd->pd_num == -1)
		return -ENOMEM;

	if (privileged) {
		err = mthca_mr_alloc_notrans(dev, pd->pd_num,
					     MTHCA_MPT_FLAG_LOCAL_READ |
					     MTHCA_MPT_FLAG_LOCAL_WRITE,
					     &pd->ntmr);
		if (err)
			mthca_free(&dev->pd_table.alloc, pd->pd_num);
	}

	return err;
}

void mthca_pd_free(struct mthca_dev *dev, struct mthca_pd *pd)
{
	if (pd->privileged)
		mthca_free_mr(dev, &pd->ntmr);
	mthca_free(&dev->pd_table.alloc, pd->pd_num);
}

int mthca_init_pd_table(struct mthca_dev *dev)
{
	return mthca_alloc_init(&dev->pd_table.alloc,
				dev->limits.num_pds,
				(1 << 24) - 1,
				dev->limits.reserved_pds);
}

void mthca_cleanup_pd_table(struct mthca_dev *dev)
{
	/* XXX check if any PDs are still allocated? */
	mthca_alloc_cleanup(&dev->pd_table.alloc);
}
