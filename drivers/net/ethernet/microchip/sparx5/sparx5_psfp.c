// SPDX-License-Identifier: GPL-2.0+
/* Microchip Sparx5 Switch driver
 *
 * Copyright (c) 2023 Microchip Technology Inc. and its subsidiaries.
 */

#include "sparx5_main_regs.h"
#include "sparx5_main.h"

/* Pool of available service policers */
static struct sparx5_pool_entry sparx5_psfp_fm_pool[SPX5_SDLB_CNT];

static int sparx5_psfp_fm_get(u32 idx, u32 *id)
{
	return sparx5_pool_get_with_idx(sparx5_psfp_fm_pool, SPX5_SDLB_CNT, idx,
					id);
}

static int sparx5_psfp_fm_put(u32 id)
{
	return sparx5_pool_put(sparx5_psfp_fm_pool, SPX5_SDLB_CNT, id);
}

static int sparx5_sdlb_conf_set(struct sparx5 *sparx5,
				struct sparx5_psfp_fm *fm)
{
	int (*sparx5_sdlb_group_action)(struct sparx5 *sparx5, u32 group,
					u32 idx);

	if (!fm->pol.rate && !fm->pol.burst)
		sparx5_sdlb_group_action = &sparx5_sdlb_group_del;
	else
		sparx5_sdlb_group_action = &sparx5_sdlb_group_add;

	sparx5_policer_conf_set(sparx5, &fm->pol);

	return sparx5_sdlb_group_action(sparx5, fm->pol.group, fm->pol.idx);
}

int sparx5_psfp_fm_add(struct sparx5 *sparx5, u32 uidx,
		       struct sparx5_psfp_fm *fm, u32 *id)
{
	struct sparx5_policer *pol = &fm->pol;
	int ret;

	/* Get flow meter */
	ret = sparx5_psfp_fm_get(uidx, &fm->pol.idx);
	if (ret < 0)
		return ret;
	/* Was already in use, no need to reconfigure */
	if (ret > 1)
		return 0;

	ret = sparx5_sdlb_group_get_by_rate(sparx5, pol->rate, pol->burst);
	if (ret < 0)
		return ret;

	fm->pol.group = ret;

	ret = sparx5_sdlb_conf_set(sparx5, fm);
	if (ret < 0)
		return ret;

	*id = fm->pol.idx;

	return 0;
}

int sparx5_psfp_fm_del(struct sparx5 *sparx5, u32 id)
{
	struct sparx5_psfp_fm fm = { .pol.idx = id,
				     .pol.type = SPX5_POL_SERVICE };
	int ret;

	/* Find the group that this lb belongs to */
	ret = sparx5_sdlb_group_get_by_index(sparx5, id, &fm.pol.group);
	if (ret < 0)
		return ret;

	ret = sparx5_psfp_fm_put(id);
	if (ret < 0)
		return ret;
	/* Do not reset flow-meter if still in use. */
	if (ret > 0)
		return 0;

	return sparx5_sdlb_conf_set(sparx5, &fm);
}
