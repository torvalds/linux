// SPDX-License-Identifier: GPL-2.0
/* Marvell RVU Admin Function driver
 *
 * Copyright (C) 2024 Marvell.
 *
 */

#include <linux/bitfield.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "rvu.h"
#include "rvu_reg.h"

int rvu_mbox_handler_get_rep_cnt(struct rvu *rvu, struct msg_req *req,
				 struct get_rep_cnt_rsp *rsp)
{
	int pf, vf, numvfs, hwvf, rep = 0;
	u16 pcifunc;

	rvu->rep_pcifunc = req->hdr.pcifunc;
	rsp->rep_cnt = rvu->cgx_mapped_pfs + rvu->cgx_mapped_vfs;
	rvu->rep_cnt = rsp->rep_cnt;

	rvu->rep2pfvf_map = devm_kzalloc(rvu->dev, rvu->rep_cnt *
					 sizeof(u16), GFP_KERNEL);
	if (!rvu->rep2pfvf_map)
		return -ENOMEM;

	for (pf = 0; pf < rvu->hw->total_pfs; pf++) {
		if (!is_pf_cgxmapped(rvu, pf))
			continue;
		pcifunc = pf << RVU_PFVF_PF_SHIFT;
		rvu->rep2pfvf_map[rep] = pcifunc;
		rsp->rep_pf_map[rep] = pcifunc;
		rep++;
		rvu_get_pf_numvfs(rvu, pf, &numvfs, &hwvf);
		for (vf = 0; vf < numvfs; vf++) {
			rvu->rep2pfvf_map[rep] = pcifunc |
				((vf + 1) & RVU_PFVF_FUNC_MASK);
			rsp->rep_pf_map[rep] = rvu->rep2pfvf_map[rep];
			rep++;
		}
	}
	return 0;
}
