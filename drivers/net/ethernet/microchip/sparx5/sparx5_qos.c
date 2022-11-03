// SPDX-License-Identifier: GPL-2.0+
/* Microchip Sparx5 Switch driver
 *
 * Copyright (c) 2022 Microchip Technology Inc. and its subsidiaries.
 */

#include <net/pkt_cls.h>

#include "sparx5_main.h"
#include "sparx5_qos.h"

/* Max rates for leak groups */
static const u32 spx5_hsch_max_group_rate[SPX5_HSCH_LEAK_GRP_CNT] = {
	1048568, /*  1.049 Gbps */
	2621420, /*  2.621 Gbps */
	10485680, /* 10.486 Gbps */
	26214200 /* 26.214 Gbps */
};

static struct sparx5_layer layers[SPX5_HSCH_LAYER_CNT];

static u32 sparx5_lg_get_leak_time(struct sparx5 *sparx5, u32 layer, u32 group)
{
	u32 value;

	value = spx5_rd(sparx5, HSCH_HSCH_TIMER_CFG(layer, group));
	return HSCH_HSCH_TIMER_CFG_LEAK_TIME_GET(value);
}

static void sparx5_lg_set_leak_time(struct sparx5 *sparx5, u32 layer, u32 group,
				    u32 leak_time)
{
	spx5_wr(HSCH_HSCH_TIMER_CFG_LEAK_TIME_SET(leak_time), sparx5,
		HSCH_HSCH_TIMER_CFG(layer, group));
}

static u32 sparx5_lg_get_first(struct sparx5 *sparx5, u32 layer, u32 group)
{
	u32 value;

	value = spx5_rd(sparx5, HSCH_HSCH_LEAK_CFG(layer, group));
	return HSCH_HSCH_LEAK_CFG_LEAK_FIRST_GET(value);
}

static u32 sparx5_lg_get_next(struct sparx5 *sparx5, u32 layer, u32 group,
			      u32 idx)

{
	u32 value;

	value = spx5_rd(sparx5, HSCH_SE_CONNECT(idx));
	return HSCH_SE_CONNECT_SE_LEAK_LINK_GET(value);
}

static u32 sparx5_lg_get_last(struct sparx5 *sparx5, u32 layer, u32 group)
{
	u32 itr, next;

	itr = sparx5_lg_get_first(sparx5, layer, group);

	for (;;) {
		next = sparx5_lg_get_next(sparx5, layer, group, itr);
		if (itr == next)
			return itr;

		itr = next;
	}
}

static bool sparx5_lg_is_last(struct sparx5 *sparx5, u32 layer, u32 group,
			      u32 idx)
{
	return idx == sparx5_lg_get_next(sparx5, layer, group, idx);
}

static bool sparx5_lg_is_first(struct sparx5 *sparx5, u32 layer, u32 group,
			       u32 idx)
{
	return idx == sparx5_lg_get_first(sparx5, layer, group);
}

static bool sparx5_lg_is_empty(struct sparx5 *sparx5, u32 layer, u32 group)
{
	return sparx5_lg_get_leak_time(sparx5, layer, group) == 0;
}

static bool sparx5_lg_is_singular(struct sparx5 *sparx5, u32 layer, u32 group)
{
	if (sparx5_lg_is_empty(sparx5, layer, group))
		return false;

	return sparx5_lg_get_first(sparx5, layer, group) ==
	       sparx5_lg_get_last(sparx5, layer, group);
}

static void sparx5_lg_enable(struct sparx5 *sparx5, u32 layer, u32 group,
			     u32 leak_time)
{
	sparx5_lg_set_leak_time(sparx5, layer, group, leak_time);
}

static void sparx5_lg_disable(struct sparx5 *sparx5, u32 layer, u32 group)
{
	sparx5_lg_set_leak_time(sparx5, layer, group, 0);
}

static int sparx5_lg_get_group_by_index(struct sparx5 *sparx5, u32 layer,
					u32 idx, u32 *group)
{
	u32 itr, next;
	int i;

	for (i = 0; i < SPX5_HSCH_LEAK_GRP_CNT; i++) {
		if (sparx5_lg_is_empty(sparx5, layer, i))
			continue;

		itr = sparx5_lg_get_first(sparx5, layer, i);

		for (;;) {
			next = sparx5_lg_get_next(sparx5, layer, i, itr);

			if (itr == idx) {
				*group = i;
				return 0; /* Found it */
			}
			if (itr == next)
				break; /* Was not found */

			itr = next;
		}
	}

	return -1;
}

static int sparx5_lg_get_group_by_rate(u32 layer, u32 rate, u32 *group)
{
	struct sparx5_layer *l = &layers[layer];
	struct sparx5_lg *lg;
	u32 i;

	for (i = 0; i < SPX5_HSCH_LEAK_GRP_CNT; i++) {
		lg = &l->leak_groups[i];
		if (rate <= lg->max_rate) {
			*group = i;
			return 0;
		}
	}

	return -1;
}

static int sparx5_lg_get_adjacent(struct sparx5 *sparx5, u32 layer, u32 group,
				  u32 idx, u32 *prev, u32 *next, u32 *first)
{
	u32 itr;

	*first = sparx5_lg_get_first(sparx5, layer, group);
	*prev = *first;
	*next = *first;
	itr = *first;

	for (;;) {
		*next = sparx5_lg_get_next(sparx5, layer, group, itr);

		if (itr == idx)
			return 0; /* Found it */

		if (itr == *next)
			return -1; /* Was not found */

		*prev = itr;
		itr = *next;
	}

	return -1;
}

static int sparx5_lg_conf_set(struct sparx5 *sparx5, u32 layer, u32 group,
			      u32 se_first, u32 idx, u32 idx_next, bool empty)
{
	u32 leak_time = layers[layer].leak_groups[group].leak_time;

	/* Stop leaking */
	sparx5_lg_disable(sparx5, layer, group);

	if (empty)
		return 0;

	/* Select layer */
	spx5_rmw(HSCH_HSCH_CFG_CFG_HSCH_LAYER_SET(layer),
		 HSCH_HSCH_CFG_CFG_HSCH_LAYER, sparx5, HSCH_HSCH_CFG_CFG);

	/* Link elements */
	spx5_wr(HSCH_SE_CONNECT_SE_LEAK_LINK_SET(idx_next), sparx5,
		HSCH_SE_CONNECT(idx));

	/* Set the first element. */
	spx5_rmw(HSCH_HSCH_LEAK_CFG_LEAK_FIRST_SET(se_first),
		 HSCH_HSCH_LEAK_CFG_LEAK_FIRST, sparx5,
		 HSCH_HSCH_LEAK_CFG(layer, group));

	/* Start leaking */
	sparx5_lg_enable(sparx5, layer, group, leak_time);

	return 0;
}

static int sparx5_lg_del(struct sparx5 *sparx5, u32 layer, u32 group, u32 idx)
{
	u32 first, next, prev;
	bool empty = false;

	/* idx *must* be present in the leak group */
	WARN_ON(sparx5_lg_get_adjacent(sparx5, layer, group, idx, &prev, &next,
				       &first) < 0);

	if (sparx5_lg_is_singular(sparx5, layer, group)) {
		empty = true;
	} else if (sparx5_lg_is_last(sparx5, layer, group, idx)) {
		/* idx is removed, prev is now last */
		idx = prev;
		next = prev;
	} else if (sparx5_lg_is_first(sparx5, layer, group, idx)) {
		/* idx is removed and points to itself, first is next */
		first = next;
		next = idx;
	} else {
		/* Next is not touched */
		idx = prev;
	}

	return sparx5_lg_conf_set(sparx5, layer, group, first, idx, next,
				  empty);
}

static int sparx5_lg_add(struct sparx5 *sparx5, u32 layer, u32 new_group,
			 u32 idx)
{
	u32 first, next, old_group;

	pr_debug("ADD: layer: %d, new_group: %d, idx: %d", layer, new_group,
		 idx);

	/* Is this SE already shaping ? */
	if (sparx5_lg_get_group_by_index(sparx5, layer, idx, &old_group) >= 0) {
		if (old_group != new_group) {
			/* Delete from old group */
			sparx5_lg_del(sparx5, layer, old_group, idx);
		} else {
			/* Nothing to do here */
			return 0;
		}
	}

	/* We always add to head of the list */
	first = idx;

	if (sparx5_lg_is_empty(sparx5, layer, new_group))
		next = idx;
	else
		next = sparx5_lg_get_first(sparx5, layer, new_group);

	return sparx5_lg_conf_set(sparx5, layer, new_group, first, idx, next,
				  false);
}

static int sparx5_shaper_conf_set(struct sparx5_port *port,
				  const struct sparx5_shaper *sh, u32 layer,
				  u32 idx, u32 group)
{
	int (*sparx5_lg_action)(struct sparx5 *, u32, u32, u32);
	struct sparx5 *sparx5 = port->sparx5;

	if (!sh->rate && !sh->burst)
		sparx5_lg_action = &sparx5_lg_del;
	else
		sparx5_lg_action = &sparx5_lg_add;

	/* Select layer */
	spx5_rmw(HSCH_HSCH_CFG_CFG_HSCH_LAYER_SET(layer),
		 HSCH_HSCH_CFG_CFG_HSCH_LAYER, sparx5, HSCH_HSCH_CFG_CFG);

	/* Set frame mode */
	spx5_rmw(HSCH_SE_CFG_SE_FRM_MODE_SET(sh->mode), HSCH_SE_CFG_SE_FRM_MODE,
		 sparx5, HSCH_SE_CFG(idx));

	/* Set committed rate and burst */
	spx5_wr(HSCH_CIR_CFG_CIR_RATE_SET(sh->rate) |
			HSCH_CIR_CFG_CIR_BURST_SET(sh->burst),
		sparx5, HSCH_CIR_CFG(idx));

	/* This has to be done after the shaper configuration has been set */
	sparx5_lg_action(sparx5, layer, group, idx);

	return 0;
}

static u32 sparx5_weight_to_hw_cost(u32 weight_min, u32 weight)
{
	return ((((SPX5_DWRR_COST_MAX << 4) * weight_min / weight) + 8) >> 4) -
	       1;
}

static int sparx5_dwrr_conf_set(struct sparx5_port *port,
				struct sparx5_dwrr *dwrr)
{
	int i;

	spx5_rmw(HSCH_HSCH_CFG_CFG_HSCH_LAYER_SET(2) |
		 HSCH_HSCH_CFG_CFG_CFG_SE_IDX_SET(port->portno),
		 HSCH_HSCH_CFG_CFG_HSCH_LAYER | HSCH_HSCH_CFG_CFG_CFG_SE_IDX,
		 port->sparx5, HSCH_HSCH_CFG_CFG);

	/* Number of *lower* indexes that are arbitrated dwrr */
	spx5_rmw(HSCH_SE_CFG_SE_DWRR_CNT_SET(dwrr->count),
		 HSCH_SE_CFG_SE_DWRR_CNT, port->sparx5,
		 HSCH_SE_CFG(port->portno));

	for (i = 0; i < dwrr->count; i++) {
		spx5_rmw(HSCH_DWRR_ENTRY_DWRR_COST_SET(dwrr->cost[i]),
			 HSCH_DWRR_ENTRY_DWRR_COST, port->sparx5,
			 HSCH_DWRR_ENTRY(i));
	}

	return 0;
}

static int sparx5_leak_groups_init(struct sparx5 *sparx5)
{
	struct sparx5_layer *layer;
	u32 sys_clk_per_100ps;
	struct sparx5_lg *lg;
	u32 leak_time_us;
	int i, ii;

	sys_clk_per_100ps = spx5_rd(sparx5, HSCH_SYS_CLK_PER);

	for (i = 0; i < SPX5_HSCH_LAYER_CNT; i++) {
		layer = &layers[i];
		for (ii = 0; ii < SPX5_HSCH_LEAK_GRP_CNT; ii++) {
			lg = &layer->leak_groups[ii];
			lg->max_rate = spx5_hsch_max_group_rate[ii];

			/* Calculate the leak time in us, to serve a maximum
			 * rate of 'max_rate' for this group
			 */
			leak_time_us = (SPX5_SE_RATE_MAX * 1000) / lg->max_rate;

			/* Hardware wants leak time in ns */
			lg->leak_time = 1000 * leak_time_us;

			/* Calculate resolution */
			lg->resolution = 1000 / leak_time_us;

			/* Maximum number of shapers that can be served by
			 * this leak group
			 */
			lg->max_ses = (1000 * leak_time_us) / sys_clk_per_100ps;

			/* Example:
			 * Wanted bandwidth is 100Mbit:
			 *
			 * 100 mbps can be served by leak group zero.
			 *
			 * leak_time is 125000 ns.
			 * resolution is: 8
			 *
			 * cir          = 100000 / 8 = 12500
			 * leaks_pr_sec = 125000 / 10^9 = 8000
			 * bw           = 12500 * 8000 = 10^8 (100 Mbit)
			 */

			/* Disable by default - this also indicates an empty
			 * leak group
			 */
			sparx5_lg_disable(sparx5, i, ii);
		}
	}

	return 0;
}

int sparx5_qos_init(struct sparx5 *sparx5)
{
	int ret;

	ret = sparx5_leak_groups_init(sparx5);
	if (ret < 0)
		return ret;

	ret = sparx5_dcb_init(sparx5);
	if (ret < 0)
		return ret;

	return 0;
}

int sparx5_tc_mqprio_add(struct net_device *ndev, u8 num_tc)
{
	int i;

	if (num_tc != SPX5_PRIOS) {
		netdev_err(ndev, "Only %d traffic classes supported\n",
			   SPX5_PRIOS);
		return -EINVAL;
	}

	netdev_set_num_tc(ndev, num_tc);

	for (i = 0; i < num_tc; i++)
		netdev_set_tc_queue(ndev, i, 1, i);

	netdev_dbg(ndev, "dev->num_tc %u dev->real_num_tx_queues %u\n",
		   ndev->num_tc, ndev->real_num_tx_queues);

	return 0;
}

int sparx5_tc_mqprio_del(struct net_device *ndev)
{
	netdev_reset_tc(ndev);

	netdev_dbg(ndev, "dev->num_tc %u dev->real_num_tx_queues %u\n",
		   ndev->num_tc, ndev->real_num_tx_queues);

	return 0;
}

int sparx5_tc_tbf_add(struct sparx5_port *port,
		      struct tc_tbf_qopt_offload_replace_params *params,
		      u32 layer, u32 idx)
{
	struct sparx5_shaper sh = {
		.mode = SPX5_SE_MODE_DATARATE,
		.rate = div_u64(params->rate.rate_bytes_ps, 1000) * 8,
		.burst = params->max_size,
	};
	struct sparx5_lg *lg;
	u32 group;

	/* Find suitable group for this se */
	if (sparx5_lg_get_group_by_rate(layer, sh.rate, &group) < 0) {
		pr_debug("Could not find leak group for se with rate: %d",
			 sh.rate);
		return -EINVAL;
	}

	lg = &layers[layer].leak_groups[group];

	pr_debug("Found matching group (speed: %d)\n", lg->max_rate);

	if (sh.rate < SPX5_SE_RATE_MIN || sh.burst < SPX5_SE_BURST_MIN)
		return -EINVAL;

	/* Calculate committed rate and burst */
	sh.rate = DIV_ROUND_UP(sh.rate, lg->resolution);
	sh.burst = DIV_ROUND_UP(sh.burst, SPX5_SE_BURST_UNIT);

	if (sh.rate > SPX5_SE_RATE_MAX || sh.burst > SPX5_SE_BURST_MAX)
		return -EINVAL;

	return sparx5_shaper_conf_set(port, &sh, layer, idx, group);
}

int sparx5_tc_tbf_del(struct sparx5_port *port, u32 layer, u32 idx)
{
	struct sparx5_shaper sh = {0};
	u32 group;

	sparx5_lg_get_group_by_index(port->sparx5, layer, idx, &group);

	return sparx5_shaper_conf_set(port, &sh, layer, idx, group);
}

int sparx5_tc_ets_add(struct sparx5_port *port,
		      struct tc_ets_qopt_offload_replace_params *params)
{
	struct sparx5_dwrr dwrr = {0};
	/* Minimum weight for each iteration */
	unsigned int w_min = 100;
	int i;

	/* Find minimum weight for all dwrr bands */
	for (i = 0; i < SPX5_PRIOS; i++) {
		if (params->quanta[i] == 0)
			continue;
		w_min = min(w_min, params->weights[i]);
	}

	for (i = 0; i < SPX5_PRIOS; i++) {
		/* Strict band; skip */
		if (params->quanta[i] == 0)
			continue;

		dwrr.count++;

		/* On the sparx5, bands with higher indexes are preferred and
		 * arbitrated strict. Strict bands are put in the lower indexes,
		 * by tc, so we reverse the bands here.
		 *
		 * Also convert the weight to something the hardware
		 * understands.
		 */
		dwrr.cost[SPX5_PRIOS - i - 1] =
			sparx5_weight_to_hw_cost(w_min, params->weights[i]);
	}

	return sparx5_dwrr_conf_set(port, &dwrr);
}

int sparx5_tc_ets_del(struct sparx5_port *port)
{
	struct sparx5_dwrr dwrr = {0};

	return sparx5_dwrr_conf_set(port, &dwrr);
}
