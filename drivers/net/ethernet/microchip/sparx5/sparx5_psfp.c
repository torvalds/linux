// SPDX-License-Identifier: GPL-2.0+
/* Microchip Sparx5 Switch driver
 *
 * Copyright (c) 2023 Microchip Technology Inc. and its subsidiaries.
 */

#include "sparx5_main_regs.h"
#include "sparx5_main.h"

#define SPX5_PSFP_SF_CNT 1024
#define SPX5_PSFP_SG_CONFIG_CHANGE_SLEEP 1000
#define SPX5_PSFP_SG_CONFIG_CHANGE_TIMEO 100000

/* Pool of available service policers */
static struct sparx5_pool_entry sparx5_psfp_fm_pool[SPX5_SDLB_CNT];

/* Pool of available stream gates */
static struct sparx5_pool_entry sparx5_psfp_sg_pool[SPX5_PSFP_SG_CNT];

/* Pool of available stream filters */
static struct sparx5_pool_entry sparx5_psfp_sf_pool[SPX5_PSFP_SF_CNT];

static int sparx5_psfp_sf_get(struct sparx5 *sparx5, u32 *id)
{
	return sparx5_pool_get(sparx5_psfp_sf_pool,
			       sparx5->data->consts->n_filters, id);
}

static int sparx5_psfp_sf_put(struct sparx5 *sparx5, u32 id)
{
	return sparx5_pool_put(sparx5_psfp_sf_pool,
			       sparx5->data->consts->n_filters, id);
}

static int sparx5_psfp_sg_get(struct sparx5 *sparx5, u32 idx, u32 *id)
{
	return sparx5_pool_get_with_idx(sparx5_psfp_sg_pool,
					sparx5->data->consts->n_gates, idx, id);
}

static int sparx5_psfp_sg_put(struct sparx5 *sparx5, u32 id)
{
	return sparx5_pool_put(sparx5_psfp_sg_pool,
			       sparx5->data->consts->n_gates, id);
}

static int sparx5_psfp_fm_get(struct sparx5 *sparx5, u32 idx, u32 *id)
{
	return sparx5_pool_get_with_idx(sparx5_psfp_fm_pool,
					sparx5->data->consts->n_sdlbs, idx, id);
}

static int sparx5_psfp_fm_put(struct sparx5 *sparx5, u32 id)
{
	return sparx5_pool_put(sparx5_psfp_fm_pool,
			       sparx5->data->consts->n_sdlbs, id);
}

u32 sparx5_psfp_isdx_get_sf(struct sparx5 *sparx5, u32 isdx)
{
	return ANA_L2_TSN_CFG_TSN_SFID_GET(spx5_rd(sparx5,
						   ANA_L2_TSN_CFG(isdx)));
}

u32 sparx5_psfp_isdx_get_fm(struct sparx5 *sparx5, u32 isdx)
{
	return ANA_L2_DLB_CFG_DLB_IDX_GET(spx5_rd(sparx5,
						  ANA_L2_DLB_CFG(isdx)));
}

u32 sparx5_psfp_sf_get_sg(struct sparx5 *sparx5, u32 sfid)
{
	return ANA_AC_TSN_SF_CFG_TSN_SGID_GET(spx5_rd(sparx5,
						      ANA_AC_TSN_SF_CFG(sfid)));
}

void sparx5_isdx_conf_set(struct sparx5 *sparx5, u32 isdx, u32 sfid, u32 fmid)
{
	spx5_rmw(ANA_L2_TSN_CFG_TSN_SFID_SET(sfid), ANA_L2_TSN_CFG_TSN_SFID,
		 sparx5, ANA_L2_TSN_CFG(isdx));

	spx5_rmw(ANA_L2_DLB_CFG_DLB_IDX_SET(fmid), ANA_L2_DLB_CFG_DLB_IDX,
		 sparx5, ANA_L2_DLB_CFG(isdx));
}

/* Internal priority value to internal priority selector */
static u32 sparx5_psfp_ipv_to_ips(s32 ipv)
{
	return ipv > 0 ? (ipv | BIT(3)) : 0;
}

static int sparx5_psfp_sgid_get_status(struct sparx5 *sparx5)
{
	return spx5_rd(sparx5, ANA_AC_SG_ACCESS_CTRL);
}

static int sparx5_psfp_sgid_wait_for_completion(struct sparx5 *sparx5)
{
	u32 val;

	return readx_poll_timeout(sparx5_psfp_sgid_get_status, sparx5, val,
				  !ANA_AC_SG_ACCESS_CTRL_CONFIG_CHANGE_GET(val),
				  SPX5_PSFP_SG_CONFIG_CHANGE_SLEEP,
				  SPX5_PSFP_SG_CONFIG_CHANGE_TIMEO);
}

static void sparx5_psfp_sg_config_change(struct sparx5 *sparx5, u32 id)
{
	spx5_wr(ANA_AC_SG_ACCESS_CTRL_SGID_SET(id), sparx5,
		ANA_AC_SG_ACCESS_CTRL);

	spx5_wr(ANA_AC_SG_ACCESS_CTRL_CONFIG_CHANGE_SET(1) |
		ANA_AC_SG_ACCESS_CTRL_SGID_SET(id),
		sparx5, ANA_AC_SG_ACCESS_CTRL);

	if (sparx5_psfp_sgid_wait_for_completion(sparx5) < 0)
		pr_debug("%s:%d timed out waiting for sgid completion",
			 __func__, __LINE__);
}

static void sparx5_psfp_sf_set(struct sparx5 *sparx5, u32 id,
			       const struct sparx5_psfp_sf *sf)
{
	/* Configure stream gate*/
	spx5_rmw(ANA_AC_TSN_SF_CFG_TSN_SGID_SET(sf->sgid) |
		ANA_AC_TSN_SF_CFG_TSN_MAX_SDU_SET(sf->max_sdu) |
		ANA_AC_TSN_SF_CFG_BLOCK_OVERSIZE_STATE_SET(sf->sblock_osize) |
		ANA_AC_TSN_SF_CFG_BLOCK_OVERSIZE_ENA_SET(sf->sblock_osize_ena),
		ANA_AC_TSN_SF_CFG_TSN_SGID | ANA_AC_TSN_SF_CFG_TSN_MAX_SDU |
		ANA_AC_TSN_SF_CFG_BLOCK_OVERSIZE_STATE |
		ANA_AC_TSN_SF_CFG_BLOCK_OVERSIZE_ENA,
		sparx5, ANA_AC_TSN_SF_CFG(id));
}

static int sparx5_psfp_sg_set(struct sparx5 *sparx5, u32 id,
			      const struct sparx5_psfp_sg *sg)
{
	u32 ips, base_lsb, base_msb, accum_time_interval = 0;
	const struct sparx5_psfp_gce *gce;
	int i;

	ips = sparx5_psfp_ipv_to_ips(sg->ipv);
	base_lsb = sg->basetime.tv_sec & 0xffffffff;
	base_msb = sg->basetime.tv_sec >> 32;

	/* Set stream gate id */
	spx5_wr(ANA_AC_SG_ACCESS_CTRL_SGID_SET(id), sparx5,
		ANA_AC_SG_ACCESS_CTRL);

	/* Write AdminPSFP values */
	spx5_wr(sg->basetime.tv_nsec, sparx5, ANA_AC_SG_CONFIG_REG_1);
	spx5_wr(base_lsb, sparx5, ANA_AC_SG_CONFIG_REG_2);

	spx5_rmw(ANA_AC_SG_CONFIG_REG_3_BASE_TIME_SEC_MSB_SET(base_msb) |
		ANA_AC_SG_CONFIG_REG_3_INIT_IPS_SET(ips) |
		ANA_AC_SG_CONFIG_REG_3_LIST_LENGTH_SET(sg->num_entries) |
		ANA_AC_SG_CONFIG_REG_3_INIT_GATE_STATE_SET(sg->gate_state) |
		ANA_AC_SG_CONFIG_REG_3_GATE_ENABLE_SET(1),
		ANA_AC_SG_CONFIG_REG_3_BASE_TIME_SEC_MSB |
		ANA_AC_SG_CONFIG_REG_3_INIT_IPS |
		ANA_AC_SG_CONFIG_REG_3_LIST_LENGTH |
		ANA_AC_SG_CONFIG_REG_3_INIT_GATE_STATE |
		ANA_AC_SG_CONFIG_REG_3_GATE_ENABLE,
		sparx5, ANA_AC_SG_CONFIG_REG_3);

	spx5_wr(sg->cycletime, sparx5, ANA_AC_SG_CONFIG_REG_4);
	spx5_wr(sg->cycletimeext, sparx5, ANA_AC_SG_CONFIG_REG_5);

	/* For each scheduling entry */
	for (i = 0; i < sg->num_entries; i++) {
		gce = &sg->gce[i];
		ips = sparx5_psfp_ipv_to_ips(gce->ipv);
		/* hardware needs TimeInterval to be cumulative */
		accum_time_interval += gce->interval;
		/* Set gate state */
		spx5_wr(ANA_AC_SG_GCL_GS_CONFIG_IPS_SET(ips) |
			ANA_AC_SG_GCL_GS_CONFIG_GATE_STATE_SET(gce->gate_state),
			sparx5, ANA_AC_SG_GCL_GS_CONFIG(i));

		/* Set time interval */
		spx5_wr(accum_time_interval, sparx5,
			ANA_AC_SG_GCL_TI_CONFIG(i));

		/* Set maximum octets */
		spx5_wr(gce->maxoctets, sparx5, ANA_AC_SG_GCL_OCT_CONFIG(i));
	}

	return 0;
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

int sparx5_psfp_sf_add(struct sparx5 *sparx5, const struct sparx5_psfp_sf *sf,
		       u32 *id)
{
	int ret;

	ret = sparx5_psfp_sf_get(sparx5, id);
	if (ret < 0)
		return ret;

	sparx5_psfp_sf_set(sparx5, *id, sf);

	return 0;
}

int sparx5_psfp_sf_del(struct sparx5 *sparx5, u32 id)
{
	const struct sparx5_psfp_sf sf = { 0 };

	sparx5_psfp_sf_set(sparx5, id, &sf);

	return sparx5_psfp_sf_put(sparx5, id);
}

int sparx5_psfp_sg_add(struct sparx5 *sparx5, u32 uidx,
		       struct sparx5_psfp_sg *sg, u32 *id)
{
	ktime_t basetime;
	int ret;

	ret = sparx5_psfp_sg_get(sparx5, uidx, id);
	if (ret < 0)
		return ret;
	/* Was already in use, no need to reconfigure */
	if (ret > 1)
		return 0;

	/* Calculate basetime for this stream gate */
	sparx5_new_base_time(sparx5, sg->cycletime, 0, &basetime);
	sg->basetime = ktime_to_timespec64(basetime);

	sparx5_psfp_sg_set(sparx5, *id, sg);

	/* Signal hardware to copy AdminPSFP values into OperPSFP values */
	sparx5_psfp_sg_config_change(sparx5, *id);

	return 0;
}

int sparx5_psfp_sg_del(struct sparx5 *sparx5, u32 id)
{
	const struct sparx5_psfp_sg sg = { 0 };
	int ret;

	ret = sparx5_psfp_sg_put(sparx5, id);
	if (ret < 0)
		return ret;
	/* Stream gate still in use ? */
	if (ret > 0)
		return 0;

	return sparx5_psfp_sg_set(sparx5, id, &sg);
}

int sparx5_psfp_fm_add(struct sparx5 *sparx5, u32 uidx,
		       struct sparx5_psfp_fm *fm, u32 *id)
{
	struct sparx5_policer *pol = &fm->pol;
	int ret;

	/* Get flow meter */
	ret = sparx5_psfp_fm_get(sparx5, uidx, &fm->pol.idx);
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

	ret = sparx5_psfp_fm_put(sparx5, id);
	if (ret < 0)
		return ret;
	/* Do not reset flow-meter if still in use. */
	if (ret > 0)
		return 0;

	return sparx5_sdlb_conf_set(sparx5, &fm);
}

void sparx5_psfp_init(struct sparx5 *sparx5)
{
	const struct sparx5_ops *ops = sparx5->data->ops;
	const struct sparx5_sdlb_group *group;
	int i;

	for (i = 0; i < sparx5->data->consts->n_lb_groups; i++) {
		group = ops->get_sdlb_group(i);
		sparx5_sdlb_group_init(sparx5, group->max_rate,
				       group->min_burst, group->frame_size, i);
	}

	spx5_wr(ANA_AC_SG_CYCLETIME_UPDATE_PERIOD_SG_CT_UPDATE_ENA_SET(1),
		sparx5, ANA_AC_SG_CYCLETIME_UPDATE_PERIOD);

	spx5_rmw(ANA_L2_FWD_CFG_ISDX_LOOKUP_ENA_SET(1),
		 ANA_L2_FWD_CFG_ISDX_LOOKUP_ENA, sparx5, ANA_L2_FWD_CFG);
}
