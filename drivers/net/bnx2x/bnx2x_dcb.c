/* bnx2x_dcb.c: Broadcom Everest network driver.
 *
 * Copyright 2009-2010 Broadcom Corporation
 *
 * Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2, available
 * at http://www.gnu.org/licenses/old-licenses/gpl-2.0.html (the "GPL").
 *
 * Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a
 * license other than the GPL, without Broadcom's express prior written
 * consent.
 *
 * Maintained by: Eilon Greenstein <eilong@broadcom.com>
 * Written by: Dmitry Kravkov
 *
 */
#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/errno.h>

#include "bnx2x.h"
#include "bnx2x_cmn.h"
#include "bnx2x_dcb.h"


/* forward declarations of dcbx related functions */
static void bnx2x_dcbx_stop_hw_tx(struct bnx2x *bp);
static void bnx2x_pfc_set_pfc(struct bnx2x *bp);
static void bnx2x_dcbx_update_ets_params(struct bnx2x *bp);
static void bnx2x_dcbx_resume_hw_tx(struct bnx2x *bp);
static void bnx2x_dcbx_get_ets_pri_pg_tbl(struct bnx2x *bp,
					  u32 *set_configuration_ets_pg,
					  u32 *pri_pg_tbl);
static void bnx2x_dcbx_get_num_pg_traf_type(struct bnx2x *bp,
					    u32 *pg_pri_orginal_spread,
					    struct pg_help_data *help_data);
static void bnx2x_dcbx_fill_cos_params(struct bnx2x *bp,
				       struct pg_help_data *help_data,
				       struct dcbx_ets_feature *ets,
				       u32 *pg_pri_orginal_spread);
static void bnx2x_dcbx_separate_pauseable_from_non(struct bnx2x *bp,
				struct cos_help_data *cos_data,
				u32 *pg_pri_orginal_spread,
				struct dcbx_ets_feature *ets);
static void bnx2x_pfc_fw_struct_e2(struct bnx2x *bp);


static void bnx2x_pfc_set(struct bnx2x *bp)
{
	struct bnx2x_nig_brb_pfc_port_params pfc_params = {0};
	u32 pri_bit, val = 0;
	u8 pri;

	/* Tx COS configuration */
	if (bp->dcbx_port_params.ets.cos_params[0].pauseable)
		pfc_params.rx_cos0_priority_mask =
			bp->dcbx_port_params.ets.cos_params[0].pri_bitmask;
	if (bp->dcbx_port_params.ets.cos_params[1].pauseable)
		pfc_params.rx_cos1_priority_mask =
			bp->dcbx_port_params.ets.cos_params[1].pri_bitmask;


	/**
	 * Rx COS configuration
	 * Changing PFC RX configuration .
	 * In RX COS0 will always be configured to lossy and COS1 to lossless
	 */
	for (pri = 0 ; pri < MAX_PFC_PRIORITIES ; pri++) {
		pri_bit = 1 << pri;

		if (pri_bit & DCBX_PFC_PRI_PAUSE_MASK(bp))
			val |= 1 << (pri * 4);
	}

	pfc_params.pkt_priority_to_cos = val;

	/* RX COS0 */
	pfc_params.llfc_low_priority_classes = 0;
	/* RX COS1 */
	pfc_params.llfc_high_priority_classes = DCBX_PFC_PRI_PAUSE_MASK(bp);

	/* BRB configuration */
	pfc_params.cos0_pauseable = false;
	pfc_params.cos1_pauseable = true;

	bnx2x_acquire_phy_lock(bp);
	bp->link_params.feature_config_flags |= FEATURE_CONFIG_PFC_ENABLED;
	bnx2x_update_pfc(&bp->link_params, &bp->link_vars, &pfc_params);
	bnx2x_release_phy_lock(bp);
}

static void bnx2x_pfc_clear(struct bnx2x *bp)
{
	struct bnx2x_nig_brb_pfc_port_params nig_params = {0};
	nig_params.pause_enable = 1;
#ifdef BNX2X_SAFC
	if (bp->flags & SAFC_TX_FLAG) {
		u32 high = 0, low = 0;
		int i;

		for (i = 0; i < BNX2X_MAX_PRIORITY; i++) {
			if (bp->pri_map[i] == 1)
				high |= (1 << i);
			if (bp->pri_map[i] == 0)
				low |= (1 << i);
		}

		nig_params.llfc_low_priority_classes = high;
		nig_params.llfc_low_priority_classes = low;

		nig_params.pause_enable = 0;
		nig_params.llfc_enable = 1;
		nig_params.llfc_out_en = 1;
	}
#endif /* BNX2X_SAFC */
	bnx2x_acquire_phy_lock(bp);
	bp->link_params.feature_config_flags &= ~FEATURE_CONFIG_PFC_ENABLED;
	bnx2x_update_pfc(&bp->link_params, &bp->link_vars, &nig_params);
	bnx2x_release_phy_lock(bp);
}

static void  bnx2x_dump_dcbx_drv_param(struct bnx2x *bp,
				       struct dcbx_features *features,
				       u32 error)
{
	u8 i = 0;
	DP(NETIF_MSG_LINK, "local_mib.error %x\n", error);

	/* PG */
	DP(NETIF_MSG_LINK,
	   "local_mib.features.ets.enabled %x\n", features->ets.enabled);
	for (i = 0; i < DCBX_MAX_NUM_PG_BW_ENTRIES; i++)
		DP(NETIF_MSG_LINK,
		   "local_mib.features.ets.pg_bw_tbl[%d] %d\n", i,
		   DCBX_PG_BW_GET(features->ets.pg_bw_tbl, i));
	for (i = 0; i < DCBX_MAX_NUM_PRI_PG_ENTRIES; i++)
		DP(NETIF_MSG_LINK,
		   "local_mib.features.ets.pri_pg_tbl[%d] %d\n", i,
		   DCBX_PRI_PG_GET(features->ets.pri_pg_tbl, i));

	/* pfc */
	DP(NETIF_MSG_LINK, "dcbx_features.pfc.pri_en_bitmap %x\n",
					features->pfc.pri_en_bitmap);
	DP(NETIF_MSG_LINK, "dcbx_features.pfc.pfc_caps %x\n",
					features->pfc.pfc_caps);
	DP(NETIF_MSG_LINK, "dcbx_features.pfc.enabled %x\n",
					features->pfc.enabled);

	DP(NETIF_MSG_LINK, "dcbx_features.app.default_pri %x\n",
					features->app.default_pri);
	DP(NETIF_MSG_LINK, "dcbx_features.app.tc_supported %x\n",
					features->app.tc_supported);
	DP(NETIF_MSG_LINK, "dcbx_features.app.enabled %x\n",
					features->app.enabled);
	for (i = 0; i < DCBX_MAX_APP_PROTOCOL; i++) {
		DP(NETIF_MSG_LINK,
		   "dcbx_features.app.app_pri_tbl[%x].app_id %x\n",
		   i, features->app.app_pri_tbl[i].app_id);
		DP(NETIF_MSG_LINK,
		   "dcbx_features.app.app_pri_tbl[%x].pri_bitmap %x\n",
		   i, features->app.app_pri_tbl[i].pri_bitmap);
		DP(NETIF_MSG_LINK,
		   "dcbx_features.app.app_pri_tbl[%x].appBitfield %x\n",
		   i, features->app.app_pri_tbl[i].appBitfield);
	}
}

static void bnx2x_dcbx_get_ap_priority(struct bnx2x *bp,
				       u8 pri_bitmap,
				       u8 llfc_traf_type)
{
	u32 pri = MAX_PFC_PRIORITIES;
	u32 index = MAX_PFC_PRIORITIES - 1;
	u32 pri_mask;
	u32 *ttp = bp->dcbx_port_params.app.traffic_type_priority;

	/* Choose the highest priority */
	while ((MAX_PFC_PRIORITIES == pri) && (0 != index)) {
		pri_mask = 1 << index;
		if (GET_FLAGS(pri_bitmap, pri_mask))
			pri = index ;
		index--;
	}

	if (pri < MAX_PFC_PRIORITIES)
		ttp[llfc_traf_type] = max_t(u32, ttp[llfc_traf_type], pri);
}

static void bnx2x_dcbx_get_ap_feature(struct bnx2x *bp,
				   struct dcbx_app_priority_feature *app,
				   u32 error) {
	u8 index;
	u32 *ttp = bp->dcbx_port_params.app.traffic_type_priority;

	if (GET_FLAGS(error, DCBX_LOCAL_APP_ERROR))
		DP(NETIF_MSG_LINK, "DCBX_LOCAL_APP_ERROR\n");

	if (app->enabled && !GET_FLAGS(error, DCBX_LOCAL_APP_ERROR)) {

		bp->dcbx_port_params.app.enabled = true;

		for (index = 0 ; index < LLFC_DRIVER_TRAFFIC_TYPE_MAX; index++)
			ttp[index] = 0;

		if (app->default_pri < MAX_PFC_PRIORITIES)
			ttp[LLFC_TRAFFIC_TYPE_NW] = app->default_pri;

		for (index = 0 ; index < DCBX_MAX_APP_PROTOCOL; index++) {
			struct dcbx_app_priority_entry *entry =
							app->app_pri_tbl;

			if (GET_FLAGS(entry[index].appBitfield,
				     DCBX_APP_SF_ETH_TYPE) &&
			   ETH_TYPE_FCOE == entry[index].app_id)
				bnx2x_dcbx_get_ap_priority(bp,
						entry[index].pri_bitmap,
						LLFC_TRAFFIC_TYPE_FCOE);

			if (GET_FLAGS(entry[index].appBitfield,
				     DCBX_APP_SF_PORT) &&
			   TCP_PORT_ISCSI == entry[index].app_id)
				bnx2x_dcbx_get_ap_priority(bp,
						entry[index].pri_bitmap,
						LLFC_TRAFFIC_TYPE_ISCSI);
		}
	} else {
		DP(NETIF_MSG_LINK, "DCBX_LOCAL_APP_DISABLED\n");
		bp->dcbx_port_params.app.enabled = false;
		for (index = 0 ; index < LLFC_DRIVER_TRAFFIC_TYPE_MAX; index++)
			ttp[index] = INVALID_TRAFFIC_TYPE_PRIORITY;
	}
}

static void bnx2x_dcbx_get_ets_feature(struct bnx2x *bp,
				       struct dcbx_ets_feature *ets,
				       u32 error) {
	int i = 0;
	u32 pg_pri_orginal_spread[DCBX_MAX_NUM_PG_BW_ENTRIES] = {0};
	struct pg_help_data pg_help_data;
	struct bnx2x_dcbx_cos_params *cos_params =
			bp->dcbx_port_params.ets.cos_params;

	memset(&pg_help_data, 0, sizeof(struct pg_help_data));


	if (GET_FLAGS(error, DCBX_LOCAL_ETS_ERROR))
		DP(NETIF_MSG_LINK, "DCBX_LOCAL_ETS_ERROR\n");


	/* Clean up old settings of ets on COS */
	for (i = 0; i < E2_NUM_OF_COS ; i++) {

		cos_params[i].pauseable = false;
		cos_params[i].strict = BNX2X_DCBX_COS_NOT_STRICT;
		cos_params[i].bw_tbl = DCBX_INVALID_COS_BW;
		cos_params[i].pri_bitmask = DCBX_PFC_PRI_GET_NON_PAUSE(bp, 0);
	}

	if (bp->dcbx_port_params.app.enabled &&
	   !GET_FLAGS(error, DCBX_LOCAL_ETS_ERROR) &&
	   ets->enabled) {
		DP(NETIF_MSG_LINK, "DCBX_LOCAL_ETS_ENABLE\n");
		bp->dcbx_port_params.ets.enabled = true;

		bnx2x_dcbx_get_ets_pri_pg_tbl(bp,
					      pg_pri_orginal_spread,
					      ets->pri_pg_tbl);

		bnx2x_dcbx_get_num_pg_traf_type(bp,
						pg_pri_orginal_spread,
						&pg_help_data);

		bnx2x_dcbx_fill_cos_params(bp, &pg_help_data,
					   ets, pg_pri_orginal_spread);

	} else {
		DP(NETIF_MSG_LINK, "DCBX_LOCAL_ETS_DISABLED\n");
		bp->dcbx_port_params.ets.enabled = false;
		ets->pri_pg_tbl[0] = 0;

		for (i = 0; i < DCBX_MAX_NUM_PRI_PG_ENTRIES ; i++)
			DCBX_PG_BW_SET(ets->pg_bw_tbl, i, 1);
	}
}

static void  bnx2x_dcbx_get_pfc_feature(struct bnx2x *bp,
					struct dcbx_pfc_feature *pfc, u32 error)
{

	if (GET_FLAGS(error, DCBX_LOCAL_PFC_ERROR))
		DP(NETIF_MSG_LINK, "DCBX_LOCAL_PFC_ERROR\n");

	if (bp->dcbx_port_params.app.enabled &&
	   !GET_FLAGS(error, DCBX_LOCAL_PFC_ERROR) &&
	   pfc->enabled) {
		bp->dcbx_port_params.pfc.enabled = true;
		bp->dcbx_port_params.pfc.priority_non_pauseable_mask =
			~(pfc->pri_en_bitmap);
	} else {
		DP(NETIF_MSG_LINK, "DCBX_LOCAL_PFC_DISABLED\n");
		bp->dcbx_port_params.pfc.enabled = false;
		bp->dcbx_port_params.pfc.priority_non_pauseable_mask = 0;
	}
}

static void bnx2x_get_dcbx_drv_param(struct bnx2x *bp,
				     struct dcbx_features *features,
				     u32 error)
{
	bnx2x_dcbx_get_ap_feature(bp, &features->app, error);

	bnx2x_dcbx_get_pfc_feature(bp, &features->pfc, error);

	bnx2x_dcbx_get_ets_feature(bp, &features->ets, error);
}

#define DCBX_LOCAL_MIB_MAX_TRY_READ		(100)
static int bnx2x_dcbx_read_mib(struct bnx2x *bp,
			       u32 *base_mib_addr,
			       u32 offset,
			       int read_mib_type)
{
	int max_try_read = 0, i;
	u32 *buff, mib_size, prefix_seq_num, suffix_seq_num;
	struct lldp_remote_mib *remote_mib ;
	struct lldp_local_mib  *local_mib;


	switch (read_mib_type) {
	case DCBX_READ_LOCAL_MIB:
		mib_size = sizeof(struct lldp_local_mib);
		break;
	case DCBX_READ_REMOTE_MIB:
		mib_size = sizeof(struct lldp_remote_mib);
		break;
	default:
		return 1; /*error*/
	}

	offset += BP_PORT(bp) * mib_size;

	do {
		buff = base_mib_addr;
		for (i = 0; i < mib_size; i += 4, buff++)
			*buff = REG_RD(bp, offset + i);

		max_try_read++;

		switch (read_mib_type) {
		case DCBX_READ_LOCAL_MIB:
			local_mib = (struct lldp_local_mib *) base_mib_addr;
			prefix_seq_num = local_mib->prefix_seq_num;
			suffix_seq_num = local_mib->suffix_seq_num;
			break;
		case DCBX_READ_REMOTE_MIB:
			remote_mib = (struct lldp_remote_mib *) base_mib_addr;
			prefix_seq_num = remote_mib->prefix_seq_num;
			suffix_seq_num = remote_mib->suffix_seq_num;
			break;
		default:
			return 1; /*error*/
		}
	} while ((prefix_seq_num != suffix_seq_num) &&
	       (max_try_read < DCBX_LOCAL_MIB_MAX_TRY_READ));

	if (max_try_read >= DCBX_LOCAL_MIB_MAX_TRY_READ) {
		BNX2X_ERR("MIB could not be read\n");
		return 1;
	}

	return 0;
}

static void bnx2x_pfc_set_pfc(struct bnx2x *bp)
{
	if (CHIP_IS_E2(bp)) {
		if (BP_PORT(bp)) {
			BNX2X_ERR("4 port mode is not supported");
			return;
		}

		if (bp->dcbx_port_params.pfc.enabled)

			/* 1. Fills up common PFC structures if required.*/
			/* 2. Configure NIG, MAC and BRB via the elink:
			 *    elink must first check if BMAC is not in reset
			 *    and only then configures the BMAC
			 *    Or, configure EMAC.
			 */
			bnx2x_pfc_set(bp);

		else
			bnx2x_pfc_clear(bp);
	}
}

static void bnx2x_dcbx_stop_hw_tx(struct bnx2x *bp)
{
	DP(NETIF_MSG_LINK, "sending STOP TRAFFIC\n");
	bnx2x_sp_post(bp, RAMROD_CMD_ID_COMMON_STOP_TRAFFIC,
		      0 /* connectionless */,
		      0 /* dataHi is zero */,
		      0 /* dataLo is zero */,
		      1 /* common */);
}

static void bnx2x_dcbx_resume_hw_tx(struct bnx2x *bp)
{
	bnx2x_pfc_fw_struct_e2(bp);
	DP(NETIF_MSG_LINK, "sending START TRAFFIC\n");
	bnx2x_sp_post(bp, RAMROD_CMD_ID_COMMON_START_TRAFFIC,
		      0, /* connectionless */
		      U64_HI(bnx2x_sp_mapping(bp, pfc_config)),
		      U64_LO(bnx2x_sp_mapping(bp, pfc_config)),
		      1  /* commmon */);
}

static void bnx2x_dcbx_update_ets_params(struct bnx2x *bp)
{
	struct bnx2x_dcbx_pg_params *ets = &(bp->dcbx_port_params.ets);
	u8	status = 0;

	bnx2x_ets_disabled(&bp->link_params);

	if (!ets->enabled)
		return;

	if ((ets->num_of_cos == 0) || (ets->num_of_cos > E2_NUM_OF_COS)) {
		BNX2X_ERR("illegal num of cos= %x", ets->num_of_cos);
		return;
	}

	/* valid COS entries */
	if (ets->num_of_cos == 1)   /* no ETS */
		return;

	/* sanity */
	if (((BNX2X_DCBX_COS_NOT_STRICT == ets->cos_params[0].strict) &&
	     (DCBX_INVALID_COS_BW == ets->cos_params[0].bw_tbl)) ||
	    ((BNX2X_DCBX_COS_NOT_STRICT == ets->cos_params[1].strict) &&
	     (DCBX_INVALID_COS_BW == ets->cos_params[1].bw_tbl))) {
		BNX2X_ERR("all COS should have at least bw_limit or strict"
			    "ets->cos_params[0].strict= %x"
			    "ets->cos_params[0].bw_tbl= %x"
			    "ets->cos_params[1].strict= %x"
			    "ets->cos_params[1].bw_tbl= %x",
			  ets->cos_params[0].strict,
			  ets->cos_params[0].bw_tbl,
			  ets->cos_params[1].strict,
			  ets->cos_params[1].bw_tbl);
		return;
	}
	/* If we join a group and there is bw_tbl and strict then bw rules */
	if ((DCBX_INVALID_COS_BW != ets->cos_params[0].bw_tbl) &&
	    (DCBX_INVALID_COS_BW != ets->cos_params[1].bw_tbl)) {
		u32 bw_tbl_0 = ets->cos_params[0].bw_tbl;
		u32 bw_tbl_1 = ets->cos_params[1].bw_tbl;
		/* Do not allow 0-100 configuration
		 * since PBF does not support it
		 * force 1-99 instead
		 */
		if (bw_tbl_0 == 0) {
			bw_tbl_0 = 1;
			bw_tbl_1 = 99;
		} else if (bw_tbl_1 == 0) {
			bw_tbl_1 = 1;
			bw_tbl_0 = 99;
		}

		bnx2x_ets_bw_limit(&bp->link_params, bw_tbl_0, bw_tbl_1);
	} else {
		if (ets->cos_params[0].strict == BNX2X_DCBX_COS_HIGH_STRICT)
			status = bnx2x_ets_strict(&bp->link_params, 0);
		else if (ets->cos_params[1].strict
						== BNX2X_DCBX_COS_HIGH_STRICT)
			status = bnx2x_ets_strict(&bp->link_params, 1);

		if (status)
			BNX2X_ERR("update_ets_params failed\n");
	}
}

static int bnx2x_dcbx_read_shmem_neg_results(struct bnx2x *bp)
{
	struct lldp_local_mib local_mib = {0};
	u32 dcbx_neg_res_offset = SHMEM2_RD(bp, dcbx_neg_res_offset);
	int rc;

	DP(NETIF_MSG_LINK, "dcbx_neg_res_offset 0x%x\n", dcbx_neg_res_offset);

	if (SHMEM_DCBX_NEG_RES_NONE == dcbx_neg_res_offset) {
		BNX2X_ERR("FW doesn't support dcbx_neg_res_offset\n");
		return -EINVAL;
	}
	rc = bnx2x_dcbx_read_mib(bp, (u32 *)&local_mib, dcbx_neg_res_offset,
				 DCBX_READ_LOCAL_MIB);

	if (rc) {
		BNX2X_ERR("Faild to read local mib from FW\n");
		return rc;
	}

	/* save features and error */
	bp->dcbx_local_feat = local_mib.features;
	bp->dcbx_error = local_mib.error;
	return 0;
}

void bnx2x_dcbx_set_params(struct bnx2x *bp, u32 state)
{
	switch (state) {
	case BNX2X_DCBX_STATE_NEG_RECEIVED:
		{
			DP(NETIF_MSG_LINK, "BNX2X_DCBX_STATE_NEG_RECEIVED\n");

			/* Read neg results if dcbx is in the FW */
			if (bnx2x_dcbx_read_shmem_neg_results(bp))
				return;

			bnx2x_dump_dcbx_drv_param(bp, &bp->dcbx_local_feat,
						  bp->dcbx_error);

			bnx2x_get_dcbx_drv_param(bp, &bp->dcbx_local_feat,
						 bp->dcbx_error);

			if (bp->state != BNX2X_STATE_OPENING_WAIT4_LOAD) {
				bnx2x_dcbx_stop_hw_tx(bp);
				return;
			}
			/* fall through */
		}
	case BNX2X_DCBX_STATE_TX_PAUSED:
		DP(NETIF_MSG_LINK, "BNX2X_DCBX_STATE_TX_PAUSED\n");
		bnx2x_pfc_set_pfc(bp);

		bnx2x_dcbx_update_ets_params(bp);
		if (bp->state != BNX2X_STATE_OPENING_WAIT4_LOAD) {
			bnx2x_dcbx_resume_hw_tx(bp);
			return;
		}
		/* fall through */
	case BNX2X_DCBX_STATE_TX_RELEASED:
		DP(NETIF_MSG_LINK, "BNX2X_DCBX_STATE_TX_RELEASED\n");
		if (bp->state != BNX2X_STATE_OPENING_WAIT4_LOAD)
			bnx2x_fw_command(bp, DRV_MSG_CODE_DCBX_PMF_DRV_OK, 0);

		return;
	default:
		BNX2X_ERR("Unknown DCBX_STATE\n");
	}
}


#define LLDP_STATS_OFFSET(bp)		(BP_PORT(bp)*\
					sizeof(struct lldp_dcbx_stat))

/* calculate struct offset in array according to chip information */
#define LLDP_PARAMS_OFFSET(bp)		(BP_PORT(bp)*sizeof(struct lldp_params))

#define LLDP_ADMIN_MIB_OFFSET(bp)	(PORT_MAX*sizeof(struct lldp_params) + \
				      BP_PORT(bp)*sizeof(struct lldp_admin_mib))

static void bnx2x_dcbx_lldp_updated_params(struct bnx2x *bp,
					   u32 dcbx_lldp_params_offset)
{
	struct lldp_params lldp_params = {0};
	u32 i = 0, *buff = NULL;
	u32 offset = dcbx_lldp_params_offset + LLDP_PARAMS_OFFSET(bp);

	DP(NETIF_MSG_LINK, "lldp_offset 0x%x\n", offset);

	if ((bp->lldp_config_params.overwrite_settings ==
				BNX2X_DCBX_OVERWRITE_SETTINGS_ENABLE)) {
		/* Read the data first */
		buff = (u32 *)&lldp_params;
		for (i = 0; i < sizeof(struct lldp_params); i += 4,  buff++)
			*buff = REG_RD(bp, (offset + i));

		lldp_params.msg_tx_hold =
			(u8)bp->lldp_config_params.msg_tx_hold;
		lldp_params.msg_fast_tx_interval =
			(u8)bp->lldp_config_params.msg_fast_tx;
		lldp_params.tx_crd_max =
			(u8)bp->lldp_config_params.tx_credit_max;
		lldp_params.msg_tx_interval =
			(u8)bp->lldp_config_params.msg_tx_interval;
		lldp_params.tx_fast =
			(u8)bp->lldp_config_params.tx_fast;

		/* Write the data.*/
		buff = (u32 *)&lldp_params;
		for (i = 0; i < sizeof(struct lldp_params); i += 4, buff++)
			REG_WR(bp, (offset + i) , *buff);


	} else if (BNX2X_DCBX_OVERWRITE_SETTINGS_ENABLE ==
				bp->lldp_config_params.overwrite_settings)
		bp->lldp_config_params.overwrite_settings =
				BNX2X_DCBX_OVERWRITE_SETTINGS_INVALID;
}

static void bnx2x_dcbx_admin_mib_updated_params(struct bnx2x *bp,
				u32 dcbx_lldp_params_offset)
{
	struct lldp_admin_mib admin_mib;
	u32 i, other_traf_type = PREDEFINED_APP_IDX_MAX, traf_type = 0;
	u32 *buff;
	u32 offset = dcbx_lldp_params_offset + LLDP_ADMIN_MIB_OFFSET(bp);

	/*shortcuts*/
	struct dcbx_features *af = &admin_mib.features;
	struct bnx2x_config_dcbx_params *dp = &bp->dcbx_config_params;

	memset(&admin_mib, 0, sizeof(struct lldp_admin_mib));
	buff = (u32 *)&admin_mib;
	/* Read the data first */
	for (i = 0; i < sizeof(struct lldp_admin_mib); i += 4, buff++)
		*buff = REG_RD(bp, (offset + i));

	if (bp->dcbx_enabled == BNX2X_DCBX_ENABLED_ON_NEG_ON)
		SET_FLAGS(admin_mib.ver_cfg_flags, DCBX_DCBX_ENABLED);
	else
		RESET_FLAGS(admin_mib.ver_cfg_flags, DCBX_DCBX_ENABLED);

	if ((BNX2X_DCBX_OVERWRITE_SETTINGS_ENABLE ==
				dp->overwrite_settings)) {
		RESET_FLAGS(admin_mib.ver_cfg_flags, DCBX_CEE_VERSION_MASK);
		admin_mib.ver_cfg_flags |=
			(dp->admin_dcbx_version << DCBX_CEE_VERSION_SHIFT) &
			 DCBX_CEE_VERSION_MASK;

		af->ets.enabled = (u8)dp->admin_ets_enable;

		af->pfc.enabled = (u8)dp->admin_pfc_enable;

		/* FOR IEEE dp->admin_tc_supported_tx_enable */
		if (dp->admin_ets_configuration_tx_enable)
			SET_FLAGS(admin_mib.ver_cfg_flags,
				  DCBX_ETS_CONFIG_TX_ENABLED);
		else
			RESET_FLAGS(admin_mib.ver_cfg_flags,
				    DCBX_ETS_CONFIG_TX_ENABLED);
		/* For IEEE admin_ets_recommendation_tx_enable */
		if (dp->admin_pfc_tx_enable)
			SET_FLAGS(admin_mib.ver_cfg_flags,
				  DCBX_PFC_CONFIG_TX_ENABLED);
		else
			RESET_FLAGS(admin_mib.ver_cfg_flags,
				  DCBX_PFC_CONFIG_TX_ENABLED);

		if (dp->admin_application_priority_tx_enable)
			SET_FLAGS(admin_mib.ver_cfg_flags,
				  DCBX_APP_CONFIG_TX_ENABLED);
		else
			RESET_FLAGS(admin_mib.ver_cfg_flags,
				  DCBX_APP_CONFIG_TX_ENABLED);

		if (dp->admin_ets_willing)
			SET_FLAGS(admin_mib.ver_cfg_flags, DCBX_ETS_WILLING);
		else
			RESET_FLAGS(admin_mib.ver_cfg_flags, DCBX_ETS_WILLING);
		/* For IEEE admin_ets_reco_valid */
		if (dp->admin_pfc_willing)
			SET_FLAGS(admin_mib.ver_cfg_flags, DCBX_PFC_WILLING);
		else
			RESET_FLAGS(admin_mib.ver_cfg_flags, DCBX_PFC_WILLING);

		if (dp->admin_app_priority_willing)
			SET_FLAGS(admin_mib.ver_cfg_flags, DCBX_APP_WILLING);
		else
			RESET_FLAGS(admin_mib.ver_cfg_flags, DCBX_APP_WILLING);

		for (i = 0 ; i < DCBX_MAX_NUM_PG_BW_ENTRIES; i++) {
			DCBX_PG_BW_SET(af->ets.pg_bw_tbl, i,
				(u8)dp->admin_configuration_bw_precentage[i]);

			DP(NETIF_MSG_LINK, "pg_bw_tbl[%d] = %02x\n",
			   i, DCBX_PG_BW_GET(af->ets.pg_bw_tbl, i));
		}

		for (i = 0; i < DCBX_MAX_NUM_PRI_PG_ENTRIES; i++) {
			DCBX_PRI_PG_SET(af->ets.pri_pg_tbl, i,
					(u8)dp->admin_configuration_ets_pg[i]);

			DP(NETIF_MSG_LINK, "pri_pg_tbl[%d] = %02x\n",
			   i, DCBX_PRI_PG_GET(af->ets.pri_pg_tbl, i));
		}

		/*For IEEE admin_recommendation_bw_precentage
		 *For IEEE admin_recommendation_ets_pg */
		af->pfc.pri_en_bitmap = (u8)dp->admin_pfc_bitmap;
		for (i = 0; i < 4; i++) {
			if (dp->admin_priority_app_table[i].valid) {
				struct bnx2x_admin_priority_app_table *table =
					dp->admin_priority_app_table;
				if ((ETH_TYPE_FCOE == table[i].app_id) &&
				   (TRAFFIC_TYPE_ETH == table[i].traffic_type))
					traf_type = FCOE_APP_IDX;
				else if ((TCP_PORT_ISCSI == table[i].app_id) &&
				   (TRAFFIC_TYPE_PORT == table[i].traffic_type))
					traf_type = ISCSI_APP_IDX;
				else
					traf_type = other_traf_type++;

				af->app.app_pri_tbl[traf_type].app_id =
					table[i].app_id;

				af->app.app_pri_tbl[traf_type].pri_bitmap =
					(u8)(1 << table[i].priority);

				af->app.app_pri_tbl[traf_type].appBitfield =
				    (DCBX_APP_ENTRY_VALID);

				af->app.app_pri_tbl[traf_type].appBitfield |=
				   (TRAFFIC_TYPE_ETH == table[i].traffic_type) ?
					DCBX_APP_SF_ETH_TYPE : DCBX_APP_SF_PORT;
			}
		}

		af->app.default_pri = (u8)dp->admin_default_priority;

	} else if (BNX2X_DCBX_OVERWRITE_SETTINGS_ENABLE ==
						dp->overwrite_settings)
		dp->overwrite_settings = BNX2X_DCBX_OVERWRITE_SETTINGS_INVALID;

	/* Write the data. */
	buff = (u32 *)&admin_mib;
	for (i = 0; i < sizeof(struct lldp_admin_mib); i += 4, buff++)
		REG_WR(bp, (offset + i), *buff);
}

void bnx2x_dcbx_set_state(struct bnx2x *bp, bool dcb_on, u32 dcbx_enabled)
{
	if (CHIP_IS_E2(bp) && !CHIP_MODE_IS_4_PORT(bp)) {
		bp->dcb_state = dcb_on;
		bp->dcbx_enabled = dcbx_enabled;
	} else {
		bp->dcb_state = false;
		bp->dcbx_enabled = BNX2X_DCBX_ENABLED_INVALID;
	}
	DP(NETIF_MSG_LINK, "DCB state [%s:%s]\n",
	   dcb_on ? "ON" : "OFF",
	   dcbx_enabled == BNX2X_DCBX_ENABLED_OFF ? "user-mode" :
	   dcbx_enabled == BNX2X_DCBX_ENABLED_ON_NEG_OFF ? "on-chip static" :
	   dcbx_enabled == BNX2X_DCBX_ENABLED_ON_NEG_ON ?
	   "on-chip with negotiation" : "invalid");
}

void bnx2x_dcbx_init_params(struct bnx2x *bp)
{
	bp->dcbx_config_params.admin_dcbx_version = 0x0; /* 0 - CEE; 1 - IEEE */
	bp->dcbx_config_params.admin_ets_willing = 1;
	bp->dcbx_config_params.admin_pfc_willing = 1;
	bp->dcbx_config_params.overwrite_settings = 1;
	bp->dcbx_config_params.admin_ets_enable = 1;
	bp->dcbx_config_params.admin_pfc_enable = 1;
	bp->dcbx_config_params.admin_tc_supported_tx_enable = 1;
	bp->dcbx_config_params.admin_ets_configuration_tx_enable = 1;
	bp->dcbx_config_params.admin_pfc_tx_enable = 1;
	bp->dcbx_config_params.admin_application_priority_tx_enable = 1;
	bp->dcbx_config_params.admin_ets_reco_valid = 1;
	bp->dcbx_config_params.admin_app_priority_willing = 1;
	bp->dcbx_config_params.admin_configuration_bw_precentage[0] = 00;
	bp->dcbx_config_params.admin_configuration_bw_precentage[1] = 50;
	bp->dcbx_config_params.admin_configuration_bw_precentage[2] = 50;
	bp->dcbx_config_params.admin_configuration_bw_precentage[3] = 0;
	bp->dcbx_config_params.admin_configuration_bw_precentage[4] = 0;
	bp->dcbx_config_params.admin_configuration_bw_precentage[5] = 0;
	bp->dcbx_config_params.admin_configuration_bw_precentage[6] = 0;
	bp->dcbx_config_params.admin_configuration_bw_precentage[7] = 0;
	bp->dcbx_config_params.admin_configuration_ets_pg[0] = 1;
	bp->dcbx_config_params.admin_configuration_ets_pg[1] = 0;
	bp->dcbx_config_params.admin_configuration_ets_pg[2] = 0;
	bp->dcbx_config_params.admin_configuration_ets_pg[3] = 2;
	bp->dcbx_config_params.admin_configuration_ets_pg[4] = 0;
	bp->dcbx_config_params.admin_configuration_ets_pg[5] = 0;
	bp->dcbx_config_params.admin_configuration_ets_pg[6] = 0;
	bp->dcbx_config_params.admin_configuration_ets_pg[7] = 0;
	bp->dcbx_config_params.admin_recommendation_bw_precentage[0] = 0;
	bp->dcbx_config_params.admin_recommendation_bw_precentage[1] = 1;
	bp->dcbx_config_params.admin_recommendation_bw_precentage[2] = 2;
	bp->dcbx_config_params.admin_recommendation_bw_precentage[3] = 0;
	bp->dcbx_config_params.admin_recommendation_bw_precentage[4] = 7;
	bp->dcbx_config_params.admin_recommendation_bw_precentage[5] = 5;
	bp->dcbx_config_params.admin_recommendation_bw_precentage[6] = 6;
	bp->dcbx_config_params.admin_recommendation_bw_precentage[7] = 7;
	bp->dcbx_config_params.admin_recommendation_ets_pg[0] = 0;
	bp->dcbx_config_params.admin_recommendation_ets_pg[1] = 1;
	bp->dcbx_config_params.admin_recommendation_ets_pg[2] = 2;
	bp->dcbx_config_params.admin_recommendation_ets_pg[3] = 3;
	bp->dcbx_config_params.admin_recommendation_ets_pg[4] = 4;
	bp->dcbx_config_params.admin_recommendation_ets_pg[5] = 5;
	bp->dcbx_config_params.admin_recommendation_ets_pg[6] = 6;
	bp->dcbx_config_params.admin_recommendation_ets_pg[7] = 7;
	bp->dcbx_config_params.admin_pfc_bitmap = 0x8; /* FCoE(3) enable */
	bp->dcbx_config_params.admin_priority_app_table[0].valid = 1;
	bp->dcbx_config_params.admin_priority_app_table[1].valid = 1;
	bp->dcbx_config_params.admin_priority_app_table[2].valid = 0;
	bp->dcbx_config_params.admin_priority_app_table[3].valid = 0;
	bp->dcbx_config_params.admin_priority_app_table[0].priority = 3;
	bp->dcbx_config_params.admin_priority_app_table[1].priority = 0;
	bp->dcbx_config_params.admin_priority_app_table[2].priority = 0;
	bp->dcbx_config_params.admin_priority_app_table[3].priority = 0;
	bp->dcbx_config_params.admin_priority_app_table[0].traffic_type = 0;
	bp->dcbx_config_params.admin_priority_app_table[1].traffic_type = 1;
	bp->dcbx_config_params.admin_priority_app_table[2].traffic_type = 0;
	bp->dcbx_config_params.admin_priority_app_table[3].traffic_type = 0;
	bp->dcbx_config_params.admin_priority_app_table[0].app_id = 0x8906;
	bp->dcbx_config_params.admin_priority_app_table[1].app_id = 3260;
	bp->dcbx_config_params.admin_priority_app_table[2].app_id = 0;
	bp->dcbx_config_params.admin_priority_app_table[3].app_id = 0;
	bp->dcbx_config_params.admin_default_priority =
		bp->dcbx_config_params.admin_priority_app_table[1].priority;
}

void bnx2x_dcbx_init(struct bnx2x *bp)
{
	u32 dcbx_lldp_params_offset = SHMEM_LLDP_DCBX_PARAMS_NONE;

	if (bp->dcbx_enabled <= 0)
		return;

	/* validate:
	 * chip of good for dcbx version,
	 * dcb is wanted
	 * the function is pmf
	 * shmem2 contains DCBX support fields
	 */
	DP(NETIF_MSG_LINK, "dcb_state %d bp->port.pmf %d\n",
	   bp->dcb_state, bp->port.pmf);

	if (bp->dcb_state ==  BNX2X_DCB_STATE_ON && bp->port.pmf &&
	    SHMEM2_HAS(bp, dcbx_lldp_params_offset)) {
		dcbx_lldp_params_offset =
			SHMEM2_RD(bp, dcbx_lldp_params_offset);

		DP(NETIF_MSG_LINK, "dcbx_lldp_params_offset 0x%x\n",
		   dcbx_lldp_params_offset);

		if (SHMEM_LLDP_DCBX_PARAMS_NONE != dcbx_lldp_params_offset) {
			bnx2x_dcbx_lldp_updated_params(bp,
						       dcbx_lldp_params_offset);

			bnx2x_dcbx_admin_mib_updated_params(bp,
				dcbx_lldp_params_offset);

			/* set default configuration BC has */
			bnx2x_dcbx_set_params(bp,
					      BNX2X_DCBX_STATE_NEG_RECEIVED);

			bnx2x_fw_command(bp,
					 DRV_MSG_CODE_DCBX_ADMIN_PMF_MSG, 0);
		}
	}
}

void bnx2x_dcb_init_intmem_pfc(struct bnx2x *bp)
{
	struct priority_cos pricos[MAX_PFC_TRAFFIC_TYPES];
	u32 i = 0, addr;
	memset(pricos, 0, sizeof(pricos));
	/* Default initialization */
	for (i = 0; i < MAX_PFC_TRAFFIC_TYPES; i++)
		pricos[i].priority = LLFC_TRAFFIC_TYPE_TO_PRIORITY_UNMAPPED;

	/* Store per port struct to internal memory */
	addr = BAR_XSTRORM_INTMEM +
			XSTORM_CMNG_PER_PORT_VARS_OFFSET(BP_PORT(bp)) +
			offsetof(struct cmng_struct_per_port,
				 traffic_type_to_priority_cos);
	__storm_memset_struct(bp, addr, sizeof(pricos), (u32 *)pricos);


	/* LLFC disabled.*/
	REG_WR8(bp , BAR_XSTRORM_INTMEM +
		    XSTORM_CMNG_PER_PORT_VARS_OFFSET(BP_PORT(bp)) +
		    offsetof(struct cmng_struct_per_port, llfc_mode),
			LLFC_MODE_NONE);

	/* DCBX disabled.*/
	REG_WR8(bp , BAR_XSTRORM_INTMEM +
		    XSTORM_CMNG_PER_PORT_VARS_OFFSET(BP_PORT(bp)) +
		    offsetof(struct cmng_struct_per_port, dcb_enabled),
			DCB_DISABLED);
}

static void
bnx2x_dcbx_print_cos_params(struct bnx2x *bp,
			    struct flow_control_configuration *pfc_fw_cfg)
{
	u8 pri = 0;
	u8 cos = 0;

	DP(NETIF_MSG_LINK,
	   "pfc_fw_cfg->dcb_version %x\n", pfc_fw_cfg->dcb_version);
	DP(NETIF_MSG_LINK,
	   "pdev->params.dcbx_port_params.pfc."
	   "priority_non_pauseable_mask %x\n",
	   bp->dcbx_port_params.pfc.priority_non_pauseable_mask);

	for (cos = 0 ; cos < bp->dcbx_port_params.ets.num_of_cos ; cos++) {
		DP(NETIF_MSG_LINK, "pdev->params.dcbx_port_params.ets."
		   "cos_params[%d].pri_bitmask %x\n", cos,
		   bp->dcbx_port_params.ets.cos_params[cos].pri_bitmask);

		DP(NETIF_MSG_LINK, "pdev->params.dcbx_port_params.ets."
		   "cos_params[%d].bw_tbl %x\n", cos,
		   bp->dcbx_port_params.ets.cos_params[cos].bw_tbl);

		DP(NETIF_MSG_LINK, "pdev->params.dcbx_port_params.ets."
		   "cos_params[%d].strict %x\n", cos,
		   bp->dcbx_port_params.ets.cos_params[cos].strict);

		DP(NETIF_MSG_LINK, "pdev->params.dcbx_port_params.ets."
		   "cos_params[%d].pauseable %x\n", cos,
		   bp->dcbx_port_params.ets.cos_params[cos].pauseable);
	}

	for (pri = 0; pri < LLFC_DRIVER_TRAFFIC_TYPE_MAX; pri++) {
		DP(NETIF_MSG_LINK,
		   "pfc_fw_cfg->traffic_type_to_priority_cos[%d]."
		   "priority %x\n", pri,
		   pfc_fw_cfg->traffic_type_to_priority_cos[pri].priority);

		DP(NETIF_MSG_LINK,
		   "pfc_fw_cfg->traffic_type_to_priority_cos[%d].cos %x\n",
		   pri, pfc_fw_cfg->traffic_type_to_priority_cos[pri].cos);
	}
}

/* fills help_data according to pg_info */
static void bnx2x_dcbx_get_num_pg_traf_type(struct bnx2x *bp,
					    u32 *pg_pri_orginal_spread,
					    struct pg_help_data *help_data)
{
	bool pg_found  = false;
	u32 i, traf_type, add_traf_type, add_pg;
	u32 *ttp = bp->dcbx_port_params.app.traffic_type_priority;
	struct pg_entry_help_data *data = help_data->data; /*shotcut*/

	/* Set to invalid */
	for (i = 0; i < LLFC_DRIVER_TRAFFIC_TYPE_MAX; i++)
		data[i].pg = DCBX_ILLEGAL_PG;

	for (add_traf_type = 0;
	     add_traf_type < LLFC_DRIVER_TRAFFIC_TYPE_MAX; add_traf_type++) {
		pg_found = false;
		if (ttp[add_traf_type] < MAX_PFC_PRIORITIES) {
			add_pg = (u8)pg_pri_orginal_spread[ttp[add_traf_type]];
			for (traf_type = 0;
			     traf_type < LLFC_DRIVER_TRAFFIC_TYPE_MAX;
			     traf_type++) {
				if (data[traf_type].pg == add_pg) {
					if (!(data[traf_type].pg_priority &
					     (1 << ttp[add_traf_type])))
						data[traf_type].
							num_of_dif_pri++;
					data[traf_type].pg_priority |=
						(1 << ttp[add_traf_type]);
					pg_found = true;
					break;
				}
			}
			if (false == pg_found) {
				data[help_data->num_of_pg].pg = add_pg;
				data[help_data->num_of_pg].pg_priority =
						(1 << ttp[add_traf_type]);
				data[help_data->num_of_pg].num_of_dif_pri = 1;
				help_data->num_of_pg++;
			}
		}
		DP(NETIF_MSG_LINK,
		   "add_traf_type %d pg_found %s num_of_pg %d\n",
		   add_traf_type, (false == pg_found) ? "NO" : "YES",
		   help_data->num_of_pg);
	}
}


/*******************************************************************************
 * Description: single priority group
 *
 * Return:
 ******************************************************************************/
static void bnx2x_dcbx_ets_disabled_entry_data(struct bnx2x *bp,
					       struct cos_help_data *cos_data,
					       u32 pri_join_mask)
{
	/* Only one priority than only one COS */
	cos_data->data[0].pausable =
		IS_DCBX_PFC_PRI_ONLY_PAUSE(bp, pri_join_mask);
	cos_data->data[0].pri_join_mask = pri_join_mask;
	cos_data->data[0].cos_bw = 100;
	cos_data->num_of_cos = 1;
}

/*******************************************************************************
 * Description: updating the cos bw
 *
 * Return:
 ******************************************************************************/
static inline void bnx2x_dcbx_add_to_cos_bw(struct bnx2x *bp,
					    struct cos_entry_help_data *data,
					    u8 pg_bw)
{
	if (data->cos_bw == DCBX_INVALID_COS_BW)
		data->cos_bw = pg_bw;
	else
		data->cos_bw += pg_bw;
}

/*******************************************************************************
 * Description: single priority group
 *
 * Return:
 ******************************************************************************/
static void bnx2x_dcbx_separate_pauseable_from_non(struct bnx2x *bp,
			struct cos_help_data *cos_data,
			u32 *pg_pri_orginal_spread,
			struct dcbx_ets_feature *ets)
{
	u32	pri_tested	= 0;
	u8	i		= 0;
	u8	entry		= 0;
	u8	pg_entry	= 0;
	u8	num_of_pri	= LLFC_DRIVER_TRAFFIC_TYPE_MAX;

	cos_data->data[0].pausable = true;
	cos_data->data[1].pausable = false;
	cos_data->data[0].pri_join_mask = cos_data->data[1].pri_join_mask = 0;

	for (i = 0 ; i < num_of_pri ; i++) {
		pri_tested = 1 << bp->dcbx_port_params.
					app.traffic_type_priority[i];

		if (pri_tested & DCBX_PFC_PRI_NON_PAUSE_MASK(bp)) {
			cos_data->data[1].pri_join_mask |= pri_tested;
			entry = 1;
		} else {
			cos_data->data[0].pri_join_mask |= pri_tested;
			entry = 0;
		}
		pg_entry = (u8)pg_pri_orginal_spread[bp->dcbx_port_params.
						app.traffic_type_priority[i]];
		/* There can be only one strict pg */
		if (pg_entry < DCBX_MAX_NUM_PG_BW_ENTRIES)
			bnx2x_dcbx_add_to_cos_bw(bp, &cos_data->data[entry],
				DCBX_PG_BW_GET(ets->pg_bw_tbl, pg_entry));
		else
			/* If we join a group and one is strict
			 * than the bw rulls */
			cos_data->data[entry].strict =
						BNX2X_DCBX_COS_HIGH_STRICT;
	}
	if ((0 == cos_data->data[0].pri_join_mask) &&
	    (0 == cos_data->data[1].pri_join_mask))
		BNX2X_ERR("dcbx error: Both groups must have priorities\n");
}


#ifndef POWER_OF_2
#define POWER_OF_2(x)	((0 != x) && (0 == (x & (x-1))))
#endif

static void bxn2x_dcbx_single_pg_to_cos_params(struct bnx2x *bp,
					      struct pg_help_data *pg_help_data,
					      struct cos_help_data *cos_data,
					      u32 pri_join_mask,
					      u8 num_of_dif_pri)
{
	u8 i = 0;
	u32 pri_tested = 0;
	u32 pri_mask_without_pri = 0;
	u32 *ttp = bp->dcbx_port_params.app.traffic_type_priority;
	/*debug*/
	if (num_of_dif_pri == 1) {
		bnx2x_dcbx_ets_disabled_entry_data(bp, cos_data, pri_join_mask);
		return;
	}
	/* single priority group */
	if (pg_help_data->data[0].pg < DCBX_MAX_NUM_PG_BW_ENTRIES) {
		/* If there are both pauseable and non-pauseable priorities,
		 * the pauseable priorities go to the first queue and
		 * the non-pauseable priorities go to the second queue.
		 */
		if (IS_DCBX_PFC_PRI_MIX_PAUSE(bp, pri_join_mask)) {
			/* Pauseable */
			cos_data->data[0].pausable = true;
			/* Non pauseable.*/
			cos_data->data[1].pausable = false;

			if (2 == num_of_dif_pri) {
				cos_data->data[0].cos_bw = 50;
				cos_data->data[1].cos_bw = 50;
			}

			if (3 == num_of_dif_pri) {
				if (POWER_OF_2(DCBX_PFC_PRI_GET_PAUSE(bp,
							pri_join_mask))) {
					cos_data->data[0].cos_bw = 33;
					cos_data->data[1].cos_bw = 67;
				} else {
					cos_data->data[0].cos_bw = 67;
					cos_data->data[1].cos_bw = 33;
				}
			}

		} else if (IS_DCBX_PFC_PRI_ONLY_PAUSE(bp, pri_join_mask)) {
			/* If there are only pauseable priorities,
			 * then one/two priorities go to the first queue
			 * and one priority goes to the second queue.
			 */
			if (2 == num_of_dif_pri) {
				cos_data->data[0].cos_bw = 50;
				cos_data->data[1].cos_bw = 50;
			} else {
				cos_data->data[0].cos_bw = 67;
				cos_data->data[1].cos_bw = 33;
			}
			cos_data->data[1].pausable = true;
			cos_data->data[0].pausable = true;
			/* All priorities except FCOE */
			cos_data->data[0].pri_join_mask = (pri_join_mask &
				((u8)~(1 << ttp[LLFC_TRAFFIC_TYPE_FCOE])));
			/* Only FCOE priority.*/
			cos_data->data[1].pri_join_mask =
				(1 << ttp[LLFC_TRAFFIC_TYPE_FCOE]);
		} else
			/* If there are only non-pauseable priorities,
			 * they will all go to the same queue.
			 */
			bnx2x_dcbx_ets_disabled_entry_data(bp,
						cos_data, pri_join_mask);
	} else {
		/* priority group which is not BW limited (PG#15):*/
		if (IS_DCBX_PFC_PRI_MIX_PAUSE(bp, pri_join_mask)) {
			/* If there are both pauseable and non-pauseable
			 * priorities, the pauseable priorities go to the first
			 * queue and the non-pauseable priorities
			 * go to the second queue.
			 */
			if (DCBX_PFC_PRI_GET_PAUSE(bp, pri_join_mask) >
			    DCBX_PFC_PRI_GET_NON_PAUSE(bp, pri_join_mask)) {
				cos_data->data[0].strict =
					BNX2X_DCBX_COS_HIGH_STRICT;
				cos_data->data[1].strict =
					BNX2X_DCBX_COS_LOW_STRICT;
			} else {
				cos_data->data[0].strict =
					BNX2X_DCBX_COS_LOW_STRICT;
				cos_data->data[1].strict =
					BNX2X_DCBX_COS_HIGH_STRICT;
			}
			/* Pauseable */
			cos_data->data[0].pausable = true;
			/* Non pause-able.*/
			cos_data->data[1].pausable = false;
		} else {
			/* If there are only pauseable priorities or
			 * only non-pauseable,* the lower priorities go
			 * to the first queue and the higherpriorities go
			 * to the second queue.
			 */
			cos_data->data[0].pausable =
				cos_data->data[1].pausable =
				IS_DCBX_PFC_PRI_ONLY_PAUSE(bp, pri_join_mask);

			for (i = 0 ; i < LLFC_DRIVER_TRAFFIC_TYPE_MAX; i++) {
				pri_tested = 1 << bp->dcbx_port_params.
					app.traffic_type_priority[i];
				/* Remove priority tested */
				pri_mask_without_pri =
					(pri_join_mask & ((u8)(~pri_tested)));
				if (pri_mask_without_pri < pri_tested)
					break;
			}

			if (i == LLFC_DRIVER_TRAFFIC_TYPE_MAX)
				BNX2X_ERR("Invalid value for pri_join_mask -"
					  " could not find a priority\n");

			cos_data->data[0].pri_join_mask = pri_mask_without_pri;
			cos_data->data[1].pri_join_mask = pri_tested;
			/* Both queues are strict priority,
			 * and that with the highest priority
			 * gets the highest strict priority in the arbiter.
			 */
			cos_data->data[0].strict = BNX2X_DCBX_COS_LOW_STRICT;
			cos_data->data[1].strict = BNX2X_DCBX_COS_HIGH_STRICT;
		}
	}
}

static void bnx2x_dcbx_two_pg_to_cos_params(
			    struct bnx2x		*bp,
			    struct  pg_help_data	*pg_help_data,
			    struct dcbx_ets_feature	*ets,
			    struct cos_help_data	*cos_data,
			    u32			*pg_pri_orginal_spread,
			    u32				pri_join_mask,
			    u8				num_of_dif_pri)
{
	u8 i = 0;
	u8 pg[E2_NUM_OF_COS] = {0};

	/* If there are both pauseable and non-pauseable priorities,
	 * the pauseable priorities go to the first queue and
	 * the non-pauseable priorities go to the second queue.
	 */
	if (IS_DCBX_PFC_PRI_MIX_PAUSE(bp, pri_join_mask)) {
		if (IS_DCBX_PFC_PRI_MIX_PAUSE(bp,
					 pg_help_data->data[0].pg_priority) ||
		    IS_DCBX_PFC_PRI_MIX_PAUSE(bp,
					 pg_help_data->data[1].pg_priority)) {
			/* If one PG contains both pauseable and
			 * non-pauseable priorities then ETS is disabled.
			 */
			bnx2x_dcbx_separate_pauseable_from_non(bp, cos_data,
					pg_pri_orginal_spread, ets);
			bp->dcbx_port_params.ets.enabled = false;
			return;
		}

		/* Pauseable */
		cos_data->data[0].pausable = true;
		/* Non pauseable. */
		cos_data->data[1].pausable = false;
		if (IS_DCBX_PFC_PRI_ONLY_PAUSE(bp,
				pg_help_data->data[0].pg_priority)) {
			/* 0 is pauseable */
			cos_data->data[0].pri_join_mask =
				pg_help_data->data[0].pg_priority;
			pg[0] = pg_help_data->data[0].pg;
			cos_data->data[1].pri_join_mask =
				pg_help_data->data[1].pg_priority;
			pg[1] = pg_help_data->data[1].pg;
		} else {/* 1 is pauseable */
			cos_data->data[0].pri_join_mask =
				pg_help_data->data[1].pg_priority;
			pg[0] = pg_help_data->data[1].pg;
			cos_data->data[1].pri_join_mask =
				pg_help_data->data[0].pg_priority;
			pg[1] = pg_help_data->data[0].pg;
		}
	} else {
		/* If there are only pauseable priorities or
		 * only non-pauseable, each PG goes to a queue.
		 */
		cos_data->data[0].pausable = cos_data->data[1].pausable =
			IS_DCBX_PFC_PRI_ONLY_PAUSE(bp, pri_join_mask);
		cos_data->data[0].pri_join_mask =
			pg_help_data->data[0].pg_priority;
		pg[0] = pg_help_data->data[0].pg;
		cos_data->data[1].pri_join_mask =
			pg_help_data->data[1].pg_priority;
		pg[1] = pg_help_data->data[1].pg;
	}

	/* There can be only one strict pg */
	for (i = 0 ; i < E2_NUM_OF_COS; i++) {
		if (pg[i] < DCBX_MAX_NUM_PG_BW_ENTRIES)
			cos_data->data[i].cos_bw =
				DCBX_PG_BW_GET(ets->pg_bw_tbl, pg[i]);
		else
			cos_data->data[i].strict = BNX2X_DCBX_COS_HIGH_STRICT;
	}
}

/*******************************************************************************
 * Description: Still
 *
 * Return:
 ******************************************************************************/
static void bnx2x_dcbx_three_pg_to_cos_params(
			      struct bnx2x		*bp,
			      struct pg_help_data	*pg_help_data,
			      struct dcbx_ets_feature	*ets,
			      struct cos_help_data	*cos_data,
			      u32			*pg_pri_orginal_spread,
			      u32			pri_join_mask,
			      u8			num_of_dif_pri)
{
	u8 i = 0;
	u32 pri_tested = 0;
	u8 entry = 0;
	u8 pg_entry = 0;
	bool b_found_strict = false;
	u8 num_of_pri = LLFC_DRIVER_TRAFFIC_TYPE_MAX;

	cos_data->data[0].pri_join_mask = cos_data->data[1].pri_join_mask = 0;
	/* If there are both pauseable and non-pauseable priorities,
	 * the pauseable priorities go to the first queue and the
	 * non-pauseable priorities go to the second queue.
	 */
	if (IS_DCBX_PFC_PRI_MIX_PAUSE(bp, pri_join_mask))
		bnx2x_dcbx_separate_pauseable_from_non(bp,
				cos_data, pg_pri_orginal_spread, ets);
	else {
		/* If two BW-limited PG-s were combined to one queue,
		 * the BW is their sum.
		 *
		 * If there are only pauseable priorities or only non-pauseable,
		 * and there are both BW-limited and non-BW-limited PG-s,
		 * the BW-limited PG/s go to one queue and the non-BW-limited
		 * PG/s go to the second queue.
		 *
		 * If there are only pauseable priorities or only non-pauseable
		 * and all are BW limited, then	two priorities go to the first
		 * queue and one priority goes to the second queue.
		 *
		 * We will join this two cases:
		 * if one is BW limited it will go to the secoend queue
		 * otherwise the last priority will get it
		 */

		cos_data->data[0].pausable = cos_data->data[1].pausable =
			IS_DCBX_PFC_PRI_ONLY_PAUSE(bp, pri_join_mask);

		for (i = 0 ; i < num_of_pri; i++) {
			pri_tested = 1 << bp->dcbx_port_params.
				app.traffic_type_priority[i];
			pg_entry = (u8)pg_pri_orginal_spread[bp->
				dcbx_port_params.app.traffic_type_priority[i]];

			if (pg_entry < DCBX_MAX_NUM_PG_BW_ENTRIES) {
				entry = 0;

				if (i == (num_of_pri-1) &&
				    false == b_found_strict)
					/* last entry will be handled separately
					 * If no priority is strict than last
					 * enty goes to last queue.*/
					entry = 1;
				cos_data->data[entry].pri_join_mask |=
								pri_tested;
				bnx2x_dcbx_add_to_cos_bw(bp,
					&cos_data->data[entry],
					DCBX_PG_BW_GET(ets->pg_bw_tbl,
						       pg_entry));
			} else {
				b_found_strict = true;
				cos_data->data[1].pri_join_mask |= pri_tested;
				/* If we join a group and one is strict
				 * than the bw rulls */
				cos_data->data[1].strict =
					BNX2X_DCBX_COS_HIGH_STRICT;
			}
		}
	}
}


static void bnx2x_dcbx_fill_cos_params(struct bnx2x *bp,
				       struct pg_help_data *help_data,
				       struct dcbx_ets_feature *ets,
				       u32 *pg_pri_orginal_spread)
{
	struct cos_help_data         cos_data ;
	u8                    i                           = 0;
	u32                   pri_join_mask               = 0;
	u8                    num_of_dif_pri              = 0;

	memset(&cos_data, 0, sizeof(cos_data));
	/* Validate the pg value */
	for (i = 0; i < help_data->num_of_pg ; i++) {
		if (DCBX_STRICT_PRIORITY != help_data->data[i].pg &&
		    DCBX_MAX_NUM_PG_BW_ENTRIES <= help_data->data[i].pg)
			BNX2X_ERR("Invalid pg[%d] data %x\n", i,
				  help_data->data[i].pg);
		pri_join_mask   |=  help_data->data[i].pg_priority;
		num_of_dif_pri  += help_data->data[i].num_of_dif_pri;
	}

	/* default settings */
	cos_data.num_of_cos = 2;
	for (i = 0; i < E2_NUM_OF_COS ; i++) {
		cos_data.data[i].pri_join_mask    = pri_join_mask;
		cos_data.data[i].pausable         = false;
		cos_data.data[i].strict           = BNX2X_DCBX_COS_NOT_STRICT;
		cos_data.data[i].cos_bw           = DCBX_INVALID_COS_BW;
	}

	switch (help_data->num_of_pg) {
	case 1:

		bxn2x_dcbx_single_pg_to_cos_params(
					       bp,
					       help_data,
					       &cos_data,
					       pri_join_mask,
					       num_of_dif_pri);
		break;
	case 2:
		bnx2x_dcbx_two_pg_to_cos_params(
					    bp,
					    help_data,
					    ets,
					    &cos_data,
					    pg_pri_orginal_spread,
					    pri_join_mask,
					    num_of_dif_pri);
		break;

	case 3:
		bnx2x_dcbx_three_pg_to_cos_params(
					      bp,
					      help_data,
					      ets,
					      &cos_data,
					      pg_pri_orginal_spread,
					      pri_join_mask,
					      num_of_dif_pri);

		break;
	default:
		BNX2X_ERR("Wrong pg_help_data.num_of_pg\n");
		bnx2x_dcbx_ets_disabled_entry_data(bp,
						   &cos_data, pri_join_mask);
	}

	for (i = 0; i < cos_data.num_of_cos ; i++) {
		struct bnx2x_dcbx_cos_params *params =
			&bp->dcbx_port_params.ets.cos_params[i];

		params->pauseable = cos_data.data[i].pausable;
		params->strict = cos_data.data[i].strict;
		params->bw_tbl = cos_data.data[i].cos_bw;
		if (params->pauseable) {
			params->pri_bitmask =
			DCBX_PFC_PRI_GET_PAUSE(bp,
					cos_data.data[i].pri_join_mask);
			DP(NETIF_MSG_LINK, "COS %d PAUSABLE prijoinmask 0x%x\n",
				  i, cos_data.data[i].pri_join_mask);
		} else {
			params->pri_bitmask =
			DCBX_PFC_PRI_GET_NON_PAUSE(bp,
					cos_data.data[i].pri_join_mask);
			DP(NETIF_MSG_LINK, "COS %d NONPAUSABLE prijoinmask "
					  "0x%x\n",
				  i, cos_data.data[i].pri_join_mask);
		}
	}

	bp->dcbx_port_params.ets.num_of_cos = cos_data.num_of_cos ;
}

static void bnx2x_dcbx_get_ets_pri_pg_tbl(struct bnx2x *bp,
				u32 *set_configuration_ets_pg,
				u32 *pri_pg_tbl)
{
	int i;

	for (i = 0; i < DCBX_MAX_NUM_PRI_PG_ENTRIES; i++) {
		set_configuration_ets_pg[i] = DCBX_PRI_PG_GET(pri_pg_tbl, i);

		DP(NETIF_MSG_LINK, "set_configuration_ets_pg[%d] = 0x%x\n",
		   i, set_configuration_ets_pg[i]);
	}
}

/*******************************************************************************
 * Description: Fill pfc_config struct that will be sent in DCBX start ramrod
 *
 * Return:
 ******************************************************************************/
static void bnx2x_pfc_fw_struct_e2(struct bnx2x *bp)
{
	struct flow_control_configuration   *pfc_fw_cfg = NULL;
	u16 pri_bit = 0;
	u8 cos = 0, pri = 0;
	struct priority_cos *tt2cos;
	u32 *ttp = bp->dcbx_port_params.app.traffic_type_priority;

	pfc_fw_cfg = (struct flow_control_configuration *)
					bnx2x_sp(bp, pfc_config);
	memset(pfc_fw_cfg, 0, sizeof(struct flow_control_configuration));

	/*shortcut*/
	tt2cos = pfc_fw_cfg->traffic_type_to_priority_cos;

	/* Fw version should be incremented each update */
	pfc_fw_cfg->dcb_version = ++bp->dcb_version;
	pfc_fw_cfg->dcb_enabled = DCB_ENABLED;

	/* Default initialization */
	for (pri = 0; pri < MAX_PFC_TRAFFIC_TYPES ; pri++) {
		tt2cos[pri].priority = LLFC_TRAFFIC_TYPE_TO_PRIORITY_UNMAPPED;
		tt2cos[pri].cos = 0;
	}

	/* Fill priority parameters */
	for (pri = 0; pri < LLFC_DRIVER_TRAFFIC_TYPE_MAX; pri++) {
		tt2cos[pri].priority = ttp[pri];
		pri_bit = 1 << tt2cos[pri].priority;

		/* Fill COS parameters based on COS calculated to
		 * make it more generally for future use */
		for (cos = 0; cos < bp->dcbx_port_params.ets.num_of_cos; cos++)
			if (bp->dcbx_port_params.ets.cos_params[cos].
						pri_bitmask & pri_bit)
					tt2cos[pri].cos = cos;
	}
	bnx2x_dcbx_print_cos_params(bp,	pfc_fw_cfg);
}
/* DCB netlink */
#ifdef BCM_DCB
#include <linux/dcbnl.h>

#define BNX2X_DCBX_CAPS		(DCB_CAP_DCBX_LLD_MANAGED | \
				DCB_CAP_DCBX_VER_CEE | DCB_CAP_DCBX_STATIC)

static inline bool bnx2x_dcbnl_set_valid(struct bnx2x *bp)
{
	/* validate dcbnl call that may change HW state:
	 * DCB is on and DCBX mode was SUCCESSFULLY set by the user.
	 */
	return bp->dcb_state && bp->dcbx_mode_uset;
}

static u8 bnx2x_dcbnl_get_state(struct net_device *netdev)
{
	struct bnx2x *bp = netdev_priv(netdev);
	DP(NETIF_MSG_LINK, "state = %d\n", bp->dcb_state);
	return bp->dcb_state;
}

static u8 bnx2x_dcbnl_set_state(struct net_device *netdev, u8 state)
{
	struct bnx2x *bp = netdev_priv(netdev);
	DP(NETIF_MSG_LINK, "state = %s\n", state ? "on" : "off");

	bnx2x_dcbx_set_state(bp, (state ? true : false), bp->dcbx_enabled);
	return 0;
}

static void bnx2x_dcbnl_get_perm_hw_addr(struct net_device *netdev,
					 u8 *perm_addr)
{
	struct bnx2x *bp = netdev_priv(netdev);
	DP(NETIF_MSG_LINK, "GET-PERM-ADDR\n");

	/* first the HW mac address */
	memcpy(perm_addr, netdev->dev_addr, netdev->addr_len);

#ifdef BCM_CNIC
	/* second SAN address */
	memcpy(perm_addr+netdev->addr_len, bp->fip_mac, netdev->addr_len);
#endif
}

static void bnx2x_dcbnl_set_pg_tccfg_tx(struct net_device *netdev, int prio,
					u8 prio_type, u8 pgid, u8 bw_pct,
					u8 up_map)
{
	struct bnx2x *bp = netdev_priv(netdev);

	DP(NETIF_MSG_LINK, "prio[%d] = %d\n", prio, pgid);
	if (!bnx2x_dcbnl_set_valid(bp) || prio >= DCBX_MAX_NUM_PRI_PG_ENTRIES)
		return;

	/**
	 * bw_pct ingnored -	band-width percentage devision between user
	 *			priorities within the same group is not
	 *			standard and hence not supported
	 *
	 * prio_type igonred -	priority levels within the same group are not
	 *			standard and hence are not supported. According
	 *			to the standard pgid 15 is dedicated to strict
	 *			prioirty traffic (on the port level).
	 *
	 * up_map ignored
	 */

	bp->dcbx_config_params.admin_configuration_ets_pg[prio] = pgid;
	bp->dcbx_config_params.admin_ets_configuration_tx_enable = 1;
}

static void bnx2x_dcbnl_set_pg_bwgcfg_tx(struct net_device *netdev,
					 int pgid, u8 bw_pct)
{
	struct bnx2x *bp = netdev_priv(netdev);
	DP(NETIF_MSG_LINK, "pgid[%d] = %d\n", pgid, bw_pct);

	if (!bnx2x_dcbnl_set_valid(bp) || pgid >= DCBX_MAX_NUM_PG_BW_ENTRIES)
		return;

	bp->dcbx_config_params.admin_configuration_bw_precentage[pgid] = bw_pct;
	bp->dcbx_config_params.admin_ets_configuration_tx_enable = 1;
}

static void bnx2x_dcbnl_set_pg_tccfg_rx(struct net_device *netdev, int prio,
					u8 prio_type, u8 pgid, u8 bw_pct,
					u8 up_map)
{
	struct bnx2x *bp = netdev_priv(netdev);
	DP(NETIF_MSG_LINK, "Nothing to set; No RX support\n");
}

static void bnx2x_dcbnl_set_pg_bwgcfg_rx(struct net_device *netdev,
					 int pgid, u8 bw_pct)
{
	struct bnx2x *bp = netdev_priv(netdev);
	DP(NETIF_MSG_LINK, "Nothing to set; No RX support\n");
}

static void bnx2x_dcbnl_get_pg_tccfg_tx(struct net_device *netdev, int prio,
					u8 *prio_type, u8 *pgid, u8 *bw_pct,
					u8 *up_map)
{
	struct bnx2x *bp = netdev_priv(netdev);
	DP(NETIF_MSG_LINK, "prio = %d\n", prio);

	/**
	 * bw_pct ingnored -	band-width percentage devision between user
	 *			priorities within the same group is not
	 *			standard and hence not supported
	 *
	 * prio_type igonred -	priority levels within the same group are not
	 *			standard and hence are not supported. According
	 *			to the standard pgid 15 is dedicated to strict
	 *			prioirty traffic (on the port level).
	 *
	 * up_map ignored
	 */
	*up_map = *bw_pct = *prio_type = *pgid = 0;

	if (!bp->dcb_state || prio >= DCBX_MAX_NUM_PRI_PG_ENTRIES)
		return;

	*pgid = DCBX_PRI_PG_GET(bp->dcbx_local_feat.ets.pri_pg_tbl, prio);
}

static void bnx2x_dcbnl_get_pg_bwgcfg_tx(struct net_device *netdev,
					 int pgid, u8 *bw_pct)
{
	struct bnx2x *bp = netdev_priv(netdev);
	DP(NETIF_MSG_LINK, "pgid = %d\n", pgid);

	*bw_pct = 0;

	if (!bp->dcb_state || pgid >= DCBX_MAX_NUM_PG_BW_ENTRIES)
		return;

	*bw_pct = DCBX_PG_BW_GET(bp->dcbx_local_feat.ets.pg_bw_tbl, pgid);
}

static void bnx2x_dcbnl_get_pg_tccfg_rx(struct net_device *netdev, int prio,
					u8 *prio_type, u8 *pgid, u8 *bw_pct,
					u8 *up_map)
{
	struct bnx2x *bp = netdev_priv(netdev);
	DP(NETIF_MSG_LINK, "Nothing to get; No RX support\n");

	*prio_type = *pgid = *bw_pct = *up_map = 0;
}

static void bnx2x_dcbnl_get_pg_bwgcfg_rx(struct net_device *netdev,
					 int pgid, u8 *bw_pct)
{
	struct bnx2x *bp = netdev_priv(netdev);
	DP(NETIF_MSG_LINK, "Nothing to get; No RX support\n");

	*bw_pct = 0;
}

static void bnx2x_dcbnl_set_pfc_cfg(struct net_device *netdev, int prio,
				    u8 setting)
{
	struct bnx2x *bp = netdev_priv(netdev);
	DP(NETIF_MSG_LINK, "prio[%d] = %d\n", prio, setting);

	if (!bnx2x_dcbnl_set_valid(bp) || prio >= MAX_PFC_PRIORITIES)
		return;

	bp->dcbx_config_params.admin_pfc_bitmap |= ((setting ? 1 : 0) << prio);

	if (setting)
		bp->dcbx_config_params.admin_pfc_tx_enable = 1;
}

static void bnx2x_dcbnl_get_pfc_cfg(struct net_device *netdev, int prio,
				    u8 *setting)
{
	struct bnx2x *bp = netdev_priv(netdev);
	DP(NETIF_MSG_LINK, "prio = %d\n", prio);

	*setting = 0;

	if (!bp->dcb_state || prio >= MAX_PFC_PRIORITIES)
		return;

	*setting = (bp->dcbx_local_feat.pfc.pri_en_bitmap >> prio) & 0x1;
}

static u8 bnx2x_dcbnl_set_all(struct net_device *netdev)
{
	struct bnx2x *bp = netdev_priv(netdev);
	int rc = 0;

	DP(NETIF_MSG_LINK, "SET-ALL\n");

	if (!bnx2x_dcbnl_set_valid(bp))
		return 1;

	if (bp->recovery_state != BNX2X_RECOVERY_DONE) {
		netdev_err(bp->dev, "Handling parity error recovery. "
				"Try again later\n");
		return 1;
	}
	if (netif_running(bp->dev)) {
		bnx2x_nic_unload(bp, UNLOAD_NORMAL);
		rc = bnx2x_nic_load(bp, LOAD_NORMAL);
	}
	DP(NETIF_MSG_LINK, "set_dcbx_params done (%d)\n", rc);
	if (rc)
		return 1;

	return 0;
}

static u8 bnx2x_dcbnl_get_cap(struct net_device *netdev, int capid, u8 *cap)
{
	struct bnx2x *bp = netdev_priv(netdev);
	u8 rval = 0;

	if (bp->dcb_state) {
		switch (capid) {
		case DCB_CAP_ATTR_PG:
			*cap = true;
			break;
		case DCB_CAP_ATTR_PFC:
			*cap = true;
			break;
		case DCB_CAP_ATTR_UP2TC:
			*cap = false;
			break;
		case DCB_CAP_ATTR_PG_TCS:
			*cap = 0x80;	/* 8 priorities for PGs */
			break;
		case DCB_CAP_ATTR_PFC_TCS:
			*cap = 0x80;	/* 8 priorities for PFC */
			break;
		case DCB_CAP_ATTR_GSP:
			*cap = true;
			break;
		case DCB_CAP_ATTR_BCN:
			*cap = false;
			break;
		case DCB_CAP_ATTR_DCBX:
			*cap = BNX2X_DCBX_CAPS;
		default:
			rval = -EINVAL;
			break;
		}
	} else
		rval = -EINVAL;

	DP(NETIF_MSG_LINK, "capid %d:%x\n", capid, *cap);
	return rval;
}

static u8 bnx2x_dcbnl_get_numtcs(struct net_device *netdev, int tcid, u8 *num)
{
	struct bnx2x *bp = netdev_priv(netdev);
	u8 rval = 0;

	DP(NETIF_MSG_LINK, "tcid %d\n", tcid);

	if (bp->dcb_state) {
		switch (tcid) {
		case DCB_NUMTCS_ATTR_PG:
			*num = E2_NUM_OF_COS;
			break;
		case DCB_NUMTCS_ATTR_PFC:
			*num = E2_NUM_OF_COS;
			break;
		default:
			rval = -EINVAL;
			break;
		}
	} else
		rval = -EINVAL;

	return rval;
}

static u8 bnx2x_dcbnl_set_numtcs(struct net_device *netdev, int tcid, u8 num)
{
	struct bnx2x *bp = netdev_priv(netdev);
	DP(NETIF_MSG_LINK, "num tcs = %d; Not supported\n", num);
	return -EINVAL;
}

static u8  bnx2x_dcbnl_get_pfc_state(struct net_device *netdev)
{
	struct bnx2x *bp = netdev_priv(netdev);
	DP(NETIF_MSG_LINK, "state = %d\n", bp->dcbx_local_feat.pfc.enabled);

	if (!bp->dcb_state)
		return 0;

	return bp->dcbx_local_feat.pfc.enabled;
}

static void bnx2x_dcbnl_set_pfc_state(struct net_device *netdev, u8 state)
{
	struct bnx2x *bp = netdev_priv(netdev);
	DP(NETIF_MSG_LINK, "state = %s\n", state ? "on" : "off");

	if (!bnx2x_dcbnl_set_valid(bp))
		return;

	bp->dcbx_config_params.admin_pfc_tx_enable =
	bp->dcbx_config_params.admin_pfc_enable = (state ? 1 : 0);
}

static bool bnx2x_app_is_equal(struct dcbx_app_priority_entry *app_ent,
			       u8 idtype, u16 idval)
{
	if (!(app_ent->appBitfield & DCBX_APP_ENTRY_VALID))
		return false;

	switch (idtype) {
	case DCB_APP_IDTYPE_ETHTYPE:
		if ((app_ent->appBitfield & DCBX_APP_ENTRY_SF_MASK) !=
			DCBX_APP_SF_ETH_TYPE)
			return false;
		break;
	case DCB_APP_IDTYPE_PORTNUM:
		if ((app_ent->appBitfield & DCBX_APP_ENTRY_SF_MASK) !=
			DCBX_APP_SF_PORT)
			return false;
		break;
	default:
		return false;
	}
	if (app_ent->app_id != idval)
		return false;

	return true;
}

static void bnx2x_admin_app_set_ent(
	struct bnx2x_admin_priority_app_table *app_ent,
	u8 idtype, u16 idval, u8 up)
{
	app_ent->valid = 1;

	switch (idtype) {
	case DCB_APP_IDTYPE_ETHTYPE:
		app_ent->traffic_type = TRAFFIC_TYPE_ETH;
		break;
	case DCB_APP_IDTYPE_PORTNUM:
		app_ent->traffic_type = TRAFFIC_TYPE_PORT;
		break;
	default:
		break; /* never gets here */
	}
	app_ent->app_id = idval;
	app_ent->priority = up;
}

static bool bnx2x_admin_app_is_equal(
	struct bnx2x_admin_priority_app_table *app_ent,
	u8 idtype, u16 idval)
{
	if (!app_ent->valid)
		return false;

	switch (idtype) {
	case DCB_APP_IDTYPE_ETHTYPE:
		if (app_ent->traffic_type != TRAFFIC_TYPE_ETH)
			return false;
		break;
	case DCB_APP_IDTYPE_PORTNUM:
		if (app_ent->traffic_type != TRAFFIC_TYPE_PORT)
			return false;
		break;
	default:
		return false;
	}
	if (app_ent->app_id != idval)
		return false;

	return true;
}

static int bnx2x_set_admin_app_up(struct bnx2x *bp, u8 idtype, u16 idval, u8 up)
{
	int i, ff;

	/* iterate over the app entries looking for idtype and idval */
	for (i = 0, ff = -1; i < 4; i++) {
		struct bnx2x_admin_priority_app_table *app_ent =
			&bp->dcbx_config_params.admin_priority_app_table[i];
		if (bnx2x_admin_app_is_equal(app_ent, idtype, idval))
			break;

		if (ff < 0 && !app_ent->valid)
			ff = i;
	}
	if (i < 4)
		/* if found overwrite up */
		bp->dcbx_config_params.
			admin_priority_app_table[i].priority = up;
	else if (ff >= 0)
		/* not found use first-free */
		bnx2x_admin_app_set_ent(
			&bp->dcbx_config_params.admin_priority_app_table[ff],
			idtype, idval, up);
	else
		/* app table is full */
		return -EBUSY;

	/* up configured, if not 0 make sure feature is enabled */
	if (up)
		bp->dcbx_config_params.admin_application_priority_tx_enable = 1;

	return 0;
}

static u8 bnx2x_dcbnl_set_app_up(struct net_device *netdev, u8 idtype,
				 u16 idval, u8 up)
{
	struct bnx2x *bp = netdev_priv(netdev);

	DP(NETIF_MSG_LINK, "app_type %d, app_id %x, prio bitmap %d\n",
	   idtype, idval, up);

	if (!bnx2x_dcbnl_set_valid(bp))
		return -EINVAL;

	/* verify idtype */
	switch (idtype) {
	case DCB_APP_IDTYPE_ETHTYPE:
	case DCB_APP_IDTYPE_PORTNUM:
		break;
	default:
		return -EINVAL;
	}
	return bnx2x_set_admin_app_up(bp, idtype, idval, up);
}

static u8 bnx2x_dcbnl_get_app_up(struct net_device *netdev, u8 idtype,
				 u16 idval)
{
	int i;
	u8 up = 0;

	struct bnx2x *bp = netdev_priv(netdev);
	DP(NETIF_MSG_LINK, "app_type %d, app_id 0x%x\n", idtype, idval);

	/* iterate over the app entries looking for idtype and idval */
	for (i = 0; i < DCBX_MAX_APP_PROTOCOL; i++)
		if (bnx2x_app_is_equal(&bp->dcbx_local_feat.app.app_pri_tbl[i],
				       idtype, idval))
			break;

	if (i < DCBX_MAX_APP_PROTOCOL)
		/* if found return up */
		up = bp->dcbx_local_feat.app.app_pri_tbl[i].pri_bitmap;
	else
		DP(NETIF_MSG_LINK, "app not found\n");

	return up;
}

static u8 bnx2x_dcbnl_get_dcbx(struct net_device *netdev)
{
	struct bnx2x *bp = netdev_priv(netdev);
	u8 state;

	state = DCB_CAP_DCBX_LLD_MANAGED | DCB_CAP_DCBX_VER_CEE;

	if (bp->dcbx_enabled == BNX2X_DCBX_ENABLED_ON_NEG_OFF)
		state |= DCB_CAP_DCBX_STATIC;

	return state;
}

static u8 bnx2x_dcbnl_set_dcbx(struct net_device *netdev, u8 state)
{
	struct bnx2x *bp = netdev_priv(netdev);
	DP(NETIF_MSG_LINK, "state = %02x\n", state);

	/* set dcbx mode */

	if ((state & BNX2X_DCBX_CAPS) != state) {
		BNX2X_ERR("Requested DCBX mode %x is beyond advertised "
			  "capabilities\n", state);
		return 1;
	}

	if (bp->dcb_state != BNX2X_DCB_STATE_ON) {
		BNX2X_ERR("DCB turned off, DCBX configuration is invalid\n");
		return 1;
	}

	if (state & DCB_CAP_DCBX_STATIC)
		bp->dcbx_enabled = BNX2X_DCBX_ENABLED_ON_NEG_OFF;
	else
		bp->dcbx_enabled = BNX2X_DCBX_ENABLED_ON_NEG_ON;

	bp->dcbx_mode_uset = true;
	return 0;
}


static u8 bnx2x_dcbnl_get_featcfg(struct net_device *netdev, int featid,
				  u8 *flags)
{
	struct bnx2x *bp = netdev_priv(netdev);
	u8 rval = 0;

	DP(NETIF_MSG_LINK, "featid %d\n", featid);

	if (bp->dcb_state) {
		*flags = 0;
		switch (featid) {
		case DCB_FEATCFG_ATTR_PG:
			if (bp->dcbx_local_feat.ets.enabled)
				*flags |= DCB_FEATCFG_ENABLE;
			if (bp->dcbx_error & DCBX_LOCAL_ETS_ERROR)
				*flags |= DCB_FEATCFG_ERROR;
			break;
		case DCB_FEATCFG_ATTR_PFC:
			if (bp->dcbx_local_feat.pfc.enabled)
				*flags |= DCB_FEATCFG_ENABLE;
			if (bp->dcbx_error & (DCBX_LOCAL_PFC_ERROR |
			    DCBX_LOCAL_PFC_MISMATCH))
				*flags |= DCB_FEATCFG_ERROR;
			break;
		case DCB_FEATCFG_ATTR_APP:
			if (bp->dcbx_local_feat.app.enabled)
				*flags |= DCB_FEATCFG_ENABLE;
			if (bp->dcbx_error & (DCBX_LOCAL_APP_ERROR |
			    DCBX_LOCAL_APP_MISMATCH))
				*flags |= DCB_FEATCFG_ERROR;
			break;
		default:
			rval = -EINVAL;
			break;
		}
	} else
		rval = -EINVAL;

	return rval;
}

static u8 bnx2x_dcbnl_set_featcfg(struct net_device *netdev, int featid,
				  u8 flags)
{
	struct bnx2x *bp = netdev_priv(netdev);
	u8 rval = 0;

	DP(NETIF_MSG_LINK, "featid = %d flags = %02x\n", featid, flags);

	/* ignore the 'advertise' flag */
	if (bnx2x_dcbnl_set_valid(bp)) {
		switch (featid) {
		case DCB_FEATCFG_ATTR_PG:
			bp->dcbx_config_params.admin_ets_enable =
				flags & DCB_FEATCFG_ENABLE ? 1 : 0;
			bp->dcbx_config_params.admin_ets_willing =
				flags & DCB_FEATCFG_WILLING ? 1 : 0;
			break;
		case DCB_FEATCFG_ATTR_PFC:
			bp->dcbx_config_params.admin_pfc_enable =
				flags & DCB_FEATCFG_ENABLE ? 1 : 0;
			bp->dcbx_config_params.admin_pfc_willing =
				flags & DCB_FEATCFG_WILLING ? 1 : 0;
			break;
		case DCB_FEATCFG_ATTR_APP:
			/* ignore enable, always enabled */
			bp->dcbx_config_params.admin_app_priority_willing =
				flags & DCB_FEATCFG_WILLING ? 1 : 0;
			break;
		default:
			rval = -EINVAL;
			break;
		}
	} else
		rval = -EINVAL;

	return rval;
}

const struct dcbnl_rtnl_ops bnx2x_dcbnl_ops = {
	.getstate       = bnx2x_dcbnl_get_state,
	.setstate       = bnx2x_dcbnl_set_state,
	.getpermhwaddr  = bnx2x_dcbnl_get_perm_hw_addr,
	.setpgtccfgtx   = bnx2x_dcbnl_set_pg_tccfg_tx,
	.setpgbwgcfgtx  = bnx2x_dcbnl_set_pg_bwgcfg_tx,
	.setpgtccfgrx   = bnx2x_dcbnl_set_pg_tccfg_rx,
	.setpgbwgcfgrx  = bnx2x_dcbnl_set_pg_bwgcfg_rx,
	.getpgtccfgtx   = bnx2x_dcbnl_get_pg_tccfg_tx,
	.getpgbwgcfgtx  = bnx2x_dcbnl_get_pg_bwgcfg_tx,
	.getpgtccfgrx   = bnx2x_dcbnl_get_pg_tccfg_rx,
	.getpgbwgcfgrx  = bnx2x_dcbnl_get_pg_bwgcfg_rx,
	.setpfccfg      = bnx2x_dcbnl_set_pfc_cfg,
	.getpfccfg      = bnx2x_dcbnl_get_pfc_cfg,
	.setall         = bnx2x_dcbnl_set_all,
	.getcap         = bnx2x_dcbnl_get_cap,
	.getnumtcs      = bnx2x_dcbnl_get_numtcs,
	.setnumtcs      = bnx2x_dcbnl_set_numtcs,
	.getpfcstate    = bnx2x_dcbnl_get_pfc_state,
	.setpfcstate    = bnx2x_dcbnl_set_pfc_state,
	.getapp         = bnx2x_dcbnl_get_app_up,
	.setapp         = bnx2x_dcbnl_set_app_up,
	.getdcbx        = bnx2x_dcbnl_get_dcbx,
	.setdcbx        = bnx2x_dcbnl_set_dcbx,
	.getfeatcfg     = bnx2x_dcbnl_get_featcfg,
	.setfeatcfg     = bnx2x_dcbnl_set_featcfg,
};

#endif /* BCM_DCB */
