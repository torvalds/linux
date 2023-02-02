// SPDX-License-Identifier: GPL-2.0+
/* Microchip Sparx5 Switch driver
 *
 * Copyright (c) 2023 Microchip Technology Inc. and its subsidiaries.
 */

#include "sparx5_main_regs.h"
#include "sparx5_main.h"

static int sparx5_policer_service_conf_set(struct sparx5 *sparx5,
					   struct sparx5_policer *pol)
{
	u32 idx, pup_tokens, max_pup_tokens, burst, thres;
	struct sparx5_sdlb_group *g;
	u64 rate;

	g = &sdlb_groups[pol->group];
	idx = pol->idx;

	rate = pol->rate * 1000;
	burst = pol->burst;

	pup_tokens = sparx5_sdlb_pup_token_get(sparx5, g->pup_interval, rate);
	max_pup_tokens =
		sparx5_sdlb_pup_token_get(sparx5, g->pup_interval, g->max_rate);

	thres = DIV_ROUND_UP(burst, g->min_burst);

	spx5_wr(ANA_AC_SDLB_PUP_TOKENS_PUP_TOKENS_SET(pup_tokens), sparx5,
		ANA_AC_SDLB_PUP_TOKENS(idx, 0));

	spx5_rmw(ANA_AC_SDLB_INH_CTRL_PUP_TOKENS_MAX_SET(max_pup_tokens),
		 ANA_AC_SDLB_INH_CTRL_PUP_TOKENS_MAX, sparx5,
		 ANA_AC_SDLB_INH_CTRL(idx, 0));

	spx5_rmw(ANA_AC_SDLB_THRES_THRES_SET(thres), ANA_AC_SDLB_THRES_THRES,
		 sparx5, ANA_AC_SDLB_THRES(idx, 0));

	return 0;
}

int sparx5_policer_conf_set(struct sparx5 *sparx5, struct sparx5_policer *pol)
{
	/* More policer types will be added later */
	switch (pol->type) {
	case SPX5_POL_SERVICE:
		return sparx5_policer_service_conf_set(sparx5, pol);
	default:
		break;
	}

	return 0;
}
