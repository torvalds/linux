// SPDX-License-Identifier: GPL-2.0
/* Marvell CN10K MCS driver
 *
 * Copyright (C) 2022 Marvell.
 */

#include <linux/types.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "mcs.h"
#include "rvu.h"
#include "lmac_common.h"

static void rvu_mcs_set_lmac_bmap(struct rvu *rvu)
{
	struct mcs *mcs = mcs_get_pdata(0);
	unsigned long lmac_bmap;
	int cgx, lmac, port;

	for (port = 0; port < mcs->hw->lmac_cnt; port++) {
		cgx = port / rvu->hw->lmac_per_cgx;
		lmac = port % rvu->hw->lmac_per_cgx;
		if (!is_lmac_valid(rvu_cgx_pdata(cgx, rvu), lmac))
			continue;
		set_bit(port, &lmac_bmap);
	}
	mcs->hw->lmac_bmap = lmac_bmap;
}

int rvu_mcs_init(struct rvu *rvu)
{
	struct rvu_hwinfo *hw = rvu->hw;
	int err = 0;

	rvu->mcs_blk_cnt = mcs_get_blkcnt();

	if (!rvu->mcs_blk_cnt)
		return 0;

	/* Needed only for CN10K-B */
	if (rvu->mcs_blk_cnt == 1) {
		err = mcs_set_lmac_channels(0, hw->cgx_chan_base);
		if (err)
			return err;
		/* Set active lmacs */
		rvu_mcs_set_lmac_bmap(rvu);
	}

	return err;
}
