// SPDX-License-Identifier: GPL-2.0
/* Marvell OcteonTx2 RVU Admin Function driver
 *
 * Copyright (C) 2018 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "rvu.h"
#include "cgx.h"

static inline u8 cgxlmac_id_to_bmap(u8 cgx_id, u8 lmac_id)
{
	return ((cgx_id & 0xF) << 4) | (lmac_id & 0xF);
}

static void *rvu_cgx_pdata(u8 cgx_id, struct rvu *rvu)
{
	if (cgx_id >= rvu->cgx_cnt)
		return NULL;

	return rvu->cgx_idmap[cgx_id];
}

static int rvu_map_cgx_lmac_pf(struct rvu *rvu)
{
	int cgx_cnt = rvu->cgx_cnt;
	int cgx, lmac_cnt, lmac;
	int pf = PF_CGXMAP_BASE;
	int size;

	if (!cgx_cnt)
		return 0;

	if (cgx_cnt > 0xF || MAX_LMAC_PER_CGX > 0xF)
		return -EINVAL;

	/* Alloc map table
	 * An additional entry is required since PF id starts from 1 and
	 * hence entry at offset 0 is invalid.
	 */
	size = (cgx_cnt * MAX_LMAC_PER_CGX + 1) * sizeof(u8);
	rvu->pf2cgxlmac_map = devm_kzalloc(rvu->dev, size, GFP_KERNEL);
	if (!rvu->pf2cgxlmac_map)
		return -ENOMEM;

	/* Initialize offset 0 with an invalid cgx and lmac id */
	rvu->pf2cgxlmac_map[0] = 0xFF;

	/* Reverse map table */
	rvu->cgxlmac2pf_map = devm_kzalloc(rvu->dev,
				  cgx_cnt * MAX_LMAC_PER_CGX * sizeof(u16),
				  GFP_KERNEL);
	if (!rvu->cgxlmac2pf_map)
		return -ENOMEM;

	rvu->cgx_mapped_pfs = 0;
	for (cgx = 0; cgx < cgx_cnt; cgx++) {
		lmac_cnt = cgx_get_lmac_cnt(rvu_cgx_pdata(cgx, rvu));
		for (lmac = 0; lmac < lmac_cnt; lmac++, pf++) {
			rvu->pf2cgxlmac_map[pf] = cgxlmac_id_to_bmap(cgx, lmac);
			rvu->cgxlmac2pf_map[CGX_OFFSET(cgx) + lmac] = 1 << pf;
			rvu->cgx_mapped_pfs++;
		}
	}
	return 0;
}

int rvu_cgx_probe(struct rvu *rvu)
{
	int i;

	/* find available cgx ports */
	rvu->cgx_cnt = cgx_get_cgx_cnt();
	if (!rvu->cgx_cnt) {
		dev_info(rvu->dev, "No CGX devices found!\n");
		return -ENODEV;
	}

	rvu->cgx_idmap = devm_kzalloc(rvu->dev, rvu->cgx_cnt * sizeof(void *),
				      GFP_KERNEL);
	if (!rvu->cgx_idmap)
		return -ENOMEM;

	/* Initialize the cgxdata table */
	for (i = 0; i < rvu->cgx_cnt; i++)
		rvu->cgx_idmap[i] = cgx_get_pdata(i);

	/* Map CGX LMAC interfaces to RVU PFs */
	return rvu_map_cgx_lmac_pf(rvu);
}
