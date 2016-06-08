/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#include <linux/types.h>
#include <asm/byteorder.h>
#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include "qed.h"
#include "qed_cxt.h"
#include "qed_dcbx.h"
#include "qed_hsi.h"
#include "qed_sp.h"

#define QED_DCBX_MAX_MIB_READ_TRY       (100)
#define QED_ETH_TYPE_DEFAULT            (0)
#define QED_ETH_TYPE_ROCE               (0x8915)
#define QED_UDP_PORT_TYPE_ROCE_V2       (0x12B7)
#define QED_ETH_TYPE_FCOE               (0x8906)
#define QED_TCP_PORT_ISCSI              (0xCBC)

#define QED_DCBX_INVALID_PRIORITY       0xFF

/* Get Traffic Class from priority traffic class table, 4 bits represent
 * the traffic class corresponding to the priority.
 */
#define QED_DCBX_PRIO2TC(prio_tc_tbl, prio) \
	((u32)(prio_tc_tbl >> ((7 - prio) * 4)) & 0x7)

static const struct qed_dcbx_app_metadata qed_dcbx_app_update[] = {
	{DCBX_PROTOCOL_ISCSI, "ISCSI", QED_PCI_DEFAULT},
	{DCBX_PROTOCOL_FCOE, "FCOE", QED_PCI_DEFAULT},
	{DCBX_PROTOCOL_ROCE, "ROCE", QED_PCI_DEFAULT},
	{DCBX_PROTOCOL_ROCE_V2, "ROCE_V2", QED_PCI_DEFAULT},
	{DCBX_PROTOCOL_ETH, "ETH", QED_PCI_ETH}
};

static bool qed_dcbx_app_ethtype(u32 app_info_bitmap)
{
	return !!(QED_MFW_GET_FIELD(app_info_bitmap, DCBX_APP_SF) ==
		  DCBX_APP_SF_ETHTYPE);
}

static bool qed_dcbx_app_port(u32 app_info_bitmap)
{
	return !!(QED_MFW_GET_FIELD(app_info_bitmap, DCBX_APP_SF) ==
		  DCBX_APP_SF_PORT);
}

static bool qed_dcbx_default_tlv(u32 app_info_bitmap, u16 proto_id)
{
	return !!(qed_dcbx_app_ethtype(app_info_bitmap) &&
		  proto_id == QED_ETH_TYPE_DEFAULT);
}

static bool qed_dcbx_iscsi_tlv(u32 app_info_bitmap, u16 proto_id)
{
	return !!(qed_dcbx_app_port(app_info_bitmap) &&
		  proto_id == QED_TCP_PORT_ISCSI);
}

static bool qed_dcbx_fcoe_tlv(u32 app_info_bitmap, u16 proto_id)
{
	return !!(qed_dcbx_app_ethtype(app_info_bitmap) &&
		  proto_id == QED_ETH_TYPE_FCOE);
}

static bool qed_dcbx_roce_tlv(u32 app_info_bitmap, u16 proto_id)
{
	return !!(qed_dcbx_app_ethtype(app_info_bitmap) &&
		  proto_id == QED_ETH_TYPE_ROCE);
}

static bool qed_dcbx_roce_v2_tlv(u32 app_info_bitmap, u16 proto_id)
{
	return !!(qed_dcbx_app_port(app_info_bitmap) &&
		  proto_id == QED_UDP_PORT_TYPE_ROCE_V2);
}

static void
qed_dcbx_dp_protocol(struct qed_hwfn *p_hwfn, struct qed_dcbx_results *p_data)
{
	enum dcbx_protocol_type id;
	int i;

	DP_VERBOSE(p_hwfn, QED_MSG_DCB, "DCBX negotiated: %d\n",
		   p_data->dcbx_enabled);

	for (i = 0; i < ARRAY_SIZE(qed_dcbx_app_update); i++) {
		id = qed_dcbx_app_update[i].id;

		DP_VERBOSE(p_hwfn, QED_MSG_DCB,
			   "%s info: update %d, enable %d, prio %d, tc %d, num_tc %d\n",
			   qed_dcbx_app_update[i].name, p_data->arr[id].update,
			   p_data->arr[id].enable, p_data->arr[id].priority,
			   p_data->arr[id].tc, p_hwfn->hw_info.num_tc);
	}
}

static void
qed_dcbx_set_params(struct qed_dcbx_results *p_data,
		    struct qed_hw_info *p_info,
		    bool enable,
		    bool update,
		    u8 prio,
		    u8 tc,
		    enum dcbx_protocol_type type,
		    enum qed_pci_personality personality)
{
	/* PF update ramrod data */
	p_data->arr[type].update = update;
	p_data->arr[type].enable = enable;
	p_data->arr[type].priority = prio;
	p_data->arr[type].tc = tc;

	/* QM reconf data */
	if (p_info->personality == personality) {
		if (personality == QED_PCI_ETH)
			p_info->non_offload_tc = tc;
		else
			p_info->offload_tc = tc;
	}
}

/* Update app protocol data and hw_info fields with the TLV info */
static void
qed_dcbx_update_app_info(struct qed_dcbx_results *p_data,
			 struct qed_hwfn *p_hwfn,
			 bool enable,
			 bool update,
			 u8 prio, u8 tc, enum dcbx_protocol_type type)
{
	struct qed_hw_info *p_info = &p_hwfn->hw_info;
	enum qed_pci_personality personality;
	enum dcbx_protocol_type id;
	char *name;
	int i;

	for (i = 0; i < ARRAY_SIZE(qed_dcbx_app_update); i++) {
		id = qed_dcbx_app_update[i].id;

		if (type != id)
			continue;

		personality = qed_dcbx_app_update[i].personality;
		name = qed_dcbx_app_update[i].name;

		qed_dcbx_set_params(p_data, p_info, enable, update,
				    prio, tc, type, personality);
	}
}

static bool
qed_dcbx_get_app_protocol_type(struct qed_hwfn *p_hwfn,
			       u32 app_prio_bitmap,
			       u16 id, enum dcbx_protocol_type *type)
{
	if (qed_dcbx_fcoe_tlv(app_prio_bitmap, id)) {
		*type = DCBX_PROTOCOL_FCOE;
	} else if (qed_dcbx_roce_tlv(app_prio_bitmap, id)) {
		*type = DCBX_PROTOCOL_ROCE;
	} else if (qed_dcbx_iscsi_tlv(app_prio_bitmap, id)) {
		*type = DCBX_PROTOCOL_ISCSI;
	} else if (qed_dcbx_default_tlv(app_prio_bitmap, id)) {
		*type = DCBX_PROTOCOL_ETH;
	} else if (qed_dcbx_roce_v2_tlv(app_prio_bitmap, id)) {
		*type = DCBX_PROTOCOL_ROCE_V2;
	} else {
		*type = DCBX_MAX_PROTOCOL_TYPE;
		DP_ERR(p_hwfn,
		       "No action required, App TLV id = 0x%x app_prio_bitmap = 0x%x\n",
		       id, app_prio_bitmap);
		return false;
	}

	return true;
}

/* Parse app TLV's to update TC information in hw_info structure for
 * reconfiguring QM. Get protocol specific data for PF update ramrod command.
 */
static int
qed_dcbx_process_tlv(struct qed_hwfn *p_hwfn,
		     struct qed_dcbx_results *p_data,
		     struct dcbx_app_priority_entry *p_tbl,
		     u32 pri_tc_tbl, int count, bool dcbx_enabled)
{
	u8 tc, priority_map;
	enum dcbx_protocol_type type;
	u16 protocol_id;
	int priority;
	bool enable;
	int i;

	DP_VERBOSE(p_hwfn, QED_MSG_DCB, "Num APP entries = %d\n", count);

	/* Parse APP TLV */
	for (i = 0; i < count; i++) {
		protocol_id = QED_MFW_GET_FIELD(p_tbl[i].entry,
						DCBX_APP_PROTOCOL_ID);
		priority_map = QED_MFW_GET_FIELD(p_tbl[i].entry,
						 DCBX_APP_PRI_MAP);
		priority = ffs(priority_map) - 1;
		if (priority < 0) {
			DP_ERR(p_hwfn, "Invalid priority\n");
			return -EINVAL;
		}

		tc = QED_DCBX_PRIO2TC(pri_tc_tbl, priority);
		if (qed_dcbx_get_app_protocol_type(p_hwfn, p_tbl[i].entry,
						   protocol_id, &type)) {
			/* ETH always have the enable bit reset, as it gets
			 * vlan information per packet. For other protocols,
			 * should be set according to the dcbx_enabled
			 * indication, but we only got here if there was an
			 * app tlv for the protocol, so dcbx must be enabled.
			 */
			enable = !(type == DCBX_PROTOCOL_ETH);

			qed_dcbx_update_app_info(p_data, p_hwfn, enable, true,
						 priority, tc, type);
		}
	}

	/* If RoCE-V2 TLV is not detected, driver need to use RoCE app
	 * data for RoCE-v2 not the default app data.
	 */
	if (!p_data->arr[DCBX_PROTOCOL_ROCE_V2].update &&
	    p_data->arr[DCBX_PROTOCOL_ROCE].update) {
		tc = p_data->arr[DCBX_PROTOCOL_ROCE].tc;
		priority = p_data->arr[DCBX_PROTOCOL_ROCE].priority;
		qed_dcbx_update_app_info(p_data, p_hwfn, true, true,
					 priority, tc, DCBX_PROTOCOL_ROCE_V2);
	}

	/* Update ramrod protocol data and hw_info fields
	 * with default info when corresponding APP TLV's are not detected.
	 * The enabled field has a different logic for ethernet as only for
	 * ethernet dcb should disabled by default, as the information arrives
	 * from the OS (unless an explicit app tlv was present).
	 */
	tc = p_data->arr[DCBX_PROTOCOL_ETH].tc;
	priority = p_data->arr[DCBX_PROTOCOL_ETH].priority;
	for (type = 0; type < DCBX_MAX_PROTOCOL_TYPE; type++) {
		if (p_data->arr[type].update)
			continue;

		enable = !(type == DCBX_PROTOCOL_ETH);
		qed_dcbx_update_app_info(p_data, p_hwfn, enable, true,
					 priority, tc, type);
	}

	return 0;
}

/* Parse app TLV's to update TC information in hw_info structure for
 * reconfiguring QM. Get protocol specific data for PF update ramrod command.
 */
static int qed_dcbx_process_mib_info(struct qed_hwfn *p_hwfn)
{
	struct dcbx_app_priority_feature *p_app;
	struct dcbx_app_priority_entry *p_tbl;
	struct qed_dcbx_results data = { 0 };
	struct dcbx_ets_feature *p_ets;
	struct qed_hw_info *p_info;
	u32 pri_tc_tbl, flags;
	bool dcbx_enabled;
	int num_entries;
	int rc = 0;

	/* If DCBx version is non zero, then negotiation was
	 * successfuly performed
	 */
	flags = p_hwfn->p_dcbx_info->operational.flags;
	dcbx_enabled = !!QED_MFW_GET_FIELD(flags, DCBX_CONFIG_VERSION);

	p_app = &p_hwfn->p_dcbx_info->operational.features.app;
	p_tbl = p_app->app_pri_tbl;

	p_ets = &p_hwfn->p_dcbx_info->operational.features.ets;
	pri_tc_tbl = p_ets->pri_tc_tbl[0];

	p_info = &p_hwfn->hw_info;
	num_entries = QED_MFW_GET_FIELD(p_app->flags, DCBX_APP_NUM_ENTRIES);

	rc = qed_dcbx_process_tlv(p_hwfn, &data, p_tbl, pri_tc_tbl,
				  num_entries, dcbx_enabled);
	if (rc)
		return rc;

	p_info->num_tc = QED_MFW_GET_FIELD(p_ets->flags, DCBX_ETS_MAX_TCS);
	data.pf_id = p_hwfn->rel_pf_id;
	data.dcbx_enabled = dcbx_enabled;

	qed_dcbx_dp_protocol(p_hwfn, &data);

	memcpy(&p_hwfn->p_dcbx_info->results, &data,
	       sizeof(struct qed_dcbx_results));

	return 0;
}

static int
qed_dcbx_copy_mib(struct qed_hwfn *p_hwfn,
		  struct qed_ptt *p_ptt,
		  struct qed_dcbx_mib_meta_data *p_data,
		  enum qed_mib_read_type type)
{
	u32 prefix_seq_num, suffix_seq_num;
	int read_count = 0;
	int rc = 0;

	/* The data is considered to be valid only if both sequence numbers are
	 * the same.
	 */
	do {
		if (type == QED_DCBX_REMOTE_LLDP_MIB) {
			qed_memcpy_from(p_hwfn, p_ptt, p_data->lldp_remote,
					p_data->addr, p_data->size);
			prefix_seq_num = p_data->lldp_remote->prefix_seq_num;
			suffix_seq_num = p_data->lldp_remote->suffix_seq_num;
		} else {
			qed_memcpy_from(p_hwfn, p_ptt, p_data->mib,
					p_data->addr, p_data->size);
			prefix_seq_num = p_data->mib->prefix_seq_num;
			suffix_seq_num = p_data->mib->suffix_seq_num;
		}
		read_count++;

		DP_VERBOSE(p_hwfn,
			   QED_MSG_DCB,
			   "mib type = %d, try count = %d prefix seq num  = %d suffix seq num = %d\n",
			   type, read_count, prefix_seq_num, suffix_seq_num);
	} while ((prefix_seq_num != suffix_seq_num) &&
		 (read_count < QED_DCBX_MAX_MIB_READ_TRY));

	if (read_count >= QED_DCBX_MAX_MIB_READ_TRY) {
		DP_ERR(p_hwfn,
		       "MIB read err, mib type = %d, try count = %d prefix seq num = %d suffix seq num = %d\n",
		       type, read_count, prefix_seq_num, suffix_seq_num);
		rc = -EIO;
	}

	return rc;
}

#ifdef CONFIG_DCB
static void
qed_dcbx_get_priority_info(struct qed_hwfn *p_hwfn,
			   struct qed_dcbx_app_prio *p_prio,
			   struct qed_dcbx_results *p_results)
{
	u8 val;

	p_prio->roce = QED_DCBX_INVALID_PRIORITY;
	p_prio->roce_v2 = QED_DCBX_INVALID_PRIORITY;
	p_prio->iscsi = QED_DCBX_INVALID_PRIORITY;
	p_prio->fcoe = QED_DCBX_INVALID_PRIORITY;

	if (p_results->arr[DCBX_PROTOCOL_ROCE].update &&
	    p_results->arr[DCBX_PROTOCOL_ROCE].enable)
		p_prio->roce = p_results->arr[DCBX_PROTOCOL_ROCE].priority;

	if (p_results->arr[DCBX_PROTOCOL_ROCE_V2].update &&
	    p_results->arr[DCBX_PROTOCOL_ROCE_V2].enable) {
		val = p_results->arr[DCBX_PROTOCOL_ROCE_V2].priority;
		p_prio->roce_v2 = val;
	}

	if (p_results->arr[DCBX_PROTOCOL_ISCSI].update &&
	    p_results->arr[DCBX_PROTOCOL_ISCSI].enable)
		p_prio->iscsi = p_results->arr[DCBX_PROTOCOL_ISCSI].priority;

	if (p_results->arr[DCBX_PROTOCOL_FCOE].update &&
	    p_results->arr[DCBX_PROTOCOL_FCOE].enable)
		p_prio->fcoe = p_results->arr[DCBX_PROTOCOL_FCOE].priority;

	if (p_results->arr[DCBX_PROTOCOL_ETH].update &&
	    p_results->arr[DCBX_PROTOCOL_ETH].enable)
		p_prio->eth = p_results->arr[DCBX_PROTOCOL_ETH].priority;

	DP_VERBOSE(p_hwfn, QED_MSG_DCB,
		   "Priorities: iscsi %d, roce %d, roce v2 %d, fcoe %d, eth %d\n",
		   p_prio->iscsi, p_prio->roce, p_prio->roce_v2, p_prio->fcoe,
		   p_prio->eth);
}

static void
qed_dcbx_get_app_data(struct qed_hwfn *p_hwfn,
		      struct dcbx_app_priority_feature *p_app,
		      struct dcbx_app_priority_entry *p_tbl,
		      struct qed_dcbx_params *p_params)
{
	struct qed_app_entry *entry;
	u8 pri_map;
	int i;

	p_params->app_willing = QED_MFW_GET_FIELD(p_app->flags,
						  DCBX_APP_WILLING);
	p_params->app_valid = QED_MFW_GET_FIELD(p_app->flags, DCBX_APP_ENABLED);
	p_params->app_error = QED_MFW_GET_FIELD(p_app->flags, DCBX_APP_ERROR);
	p_params->num_app_entries = QED_MFW_GET_FIELD(p_app->flags,
						      DCBX_APP_NUM_ENTRIES);
	for (i = 0; i < DCBX_MAX_APP_PROTOCOL; i++) {
		entry = &p_params->app_entry[i];
		entry->ethtype = !(QED_MFW_GET_FIELD(p_tbl[i].entry,
						     DCBX_APP_SF));
		pri_map = QED_MFW_GET_FIELD(p_tbl[i].entry, DCBX_APP_PRI_MAP);
		entry->prio = ffs(pri_map) - 1;
		entry->proto_id = QED_MFW_GET_FIELD(p_tbl[i].entry,
						    DCBX_APP_PROTOCOL_ID);
		qed_dcbx_get_app_protocol_type(p_hwfn, p_tbl[i].entry,
					       entry->proto_id,
					       &entry->proto_type);
	}

	DP_VERBOSE(p_hwfn, QED_MSG_DCB,
		   "APP params: willing %d, valid %d error = %d\n",
		   p_params->app_willing, p_params->app_valid,
		   p_params->app_error);
}

static void
qed_dcbx_get_pfc_data(struct qed_hwfn *p_hwfn,
		      u32 pfc, struct qed_dcbx_params *p_params)
{
	u8 pfc_map;

	p_params->pfc.willing = QED_MFW_GET_FIELD(pfc, DCBX_PFC_WILLING);
	p_params->pfc.max_tc = QED_MFW_GET_FIELD(pfc, DCBX_PFC_CAPS);
	p_params->pfc.enabled = QED_MFW_GET_FIELD(pfc, DCBX_PFC_ENABLED);
	pfc_map = QED_MFW_GET_FIELD(pfc, DCBX_PFC_PRI_EN_BITMAP);
	p_params->pfc.prio[0] = !!(pfc_map & DCBX_PFC_PRI_EN_BITMAP_PRI_0);
	p_params->pfc.prio[1] = !!(pfc_map & DCBX_PFC_PRI_EN_BITMAP_PRI_1);
	p_params->pfc.prio[2] = !!(pfc_map & DCBX_PFC_PRI_EN_BITMAP_PRI_2);
	p_params->pfc.prio[3] = !!(pfc_map & DCBX_PFC_PRI_EN_BITMAP_PRI_3);
	p_params->pfc.prio[4] = !!(pfc_map & DCBX_PFC_PRI_EN_BITMAP_PRI_4);
	p_params->pfc.prio[5] = !!(pfc_map & DCBX_PFC_PRI_EN_BITMAP_PRI_5);
	p_params->pfc.prio[6] = !!(pfc_map & DCBX_PFC_PRI_EN_BITMAP_PRI_6);
	p_params->pfc.prio[7] = !!(pfc_map & DCBX_PFC_PRI_EN_BITMAP_PRI_7);

	DP_VERBOSE(p_hwfn, QED_MSG_DCB,
		   "PFC params: willing %d, pfc_bitmap %d\n",
		   p_params->pfc.willing, pfc_map);
}

static void
qed_dcbx_get_ets_data(struct qed_hwfn *p_hwfn,
		      struct dcbx_ets_feature *p_ets,
		      struct qed_dcbx_params *p_params)
{
	u32 bw_map[2], tsa_map[2], pri_map;
	int i;

	p_params->ets_willing = QED_MFW_GET_FIELD(p_ets->flags,
						  DCBX_ETS_WILLING);
	p_params->ets_enabled = QED_MFW_GET_FIELD(p_ets->flags,
						  DCBX_ETS_ENABLED);
	p_params->ets_cbs = QED_MFW_GET_FIELD(p_ets->flags, DCBX_ETS_CBS);
	p_params->max_ets_tc = QED_MFW_GET_FIELD(p_ets->flags,
						 DCBX_ETS_MAX_TCS);
	DP_VERBOSE(p_hwfn, QED_MSG_DCB,
		   "ETS params: willing %d, ets_cbs %d pri_tc_tbl_0 %x max_ets_tc %d\n",
		   p_params->ets_willing,
		   p_params->ets_cbs,
		   p_ets->pri_tc_tbl[0], p_params->max_ets_tc);

	/* 8 bit tsa and bw data corresponding to each of the 8 TC's are
	 * encoded in a type u32 array of size 2.
	 */
	bw_map[0] = be32_to_cpu(p_ets->tc_bw_tbl[0]);
	bw_map[1] = be32_to_cpu(p_ets->tc_bw_tbl[1]);
	tsa_map[0] = be32_to_cpu(p_ets->tc_tsa_tbl[0]);
	tsa_map[1] = be32_to_cpu(p_ets->tc_tsa_tbl[1]);
	pri_map = be32_to_cpu(p_ets->pri_tc_tbl[0]);
	for (i = 0; i < QED_MAX_PFC_PRIORITIES; i++) {
		p_params->ets_tc_bw_tbl[i] = ((u8 *)bw_map)[i];
		p_params->ets_tc_tsa_tbl[i] = ((u8 *)tsa_map)[i];
		p_params->ets_pri_tc_tbl[i] = QED_DCBX_PRIO2TC(pri_map, i);
		DP_VERBOSE(p_hwfn, QED_MSG_DCB,
			   "elem %d  bw_tbl %x tsa_tbl %x\n",
			   i, p_params->ets_tc_bw_tbl[i],
			   p_params->ets_tc_tsa_tbl[i]);
	}
}

static void
qed_dcbx_get_common_params(struct qed_hwfn *p_hwfn,
			   struct dcbx_app_priority_feature *p_app,
			   struct dcbx_app_priority_entry *p_tbl,
			   struct dcbx_ets_feature *p_ets,
			   u32 pfc, struct qed_dcbx_params *p_params)
{
	qed_dcbx_get_app_data(p_hwfn, p_app, p_tbl, p_params);
	qed_dcbx_get_ets_data(p_hwfn, p_ets, p_params);
	qed_dcbx_get_pfc_data(p_hwfn, pfc, p_params);
}

static void
qed_dcbx_get_local_params(struct qed_hwfn *p_hwfn,
			  struct qed_ptt *p_ptt, struct qed_dcbx_get *params)
{
	struct dcbx_features *p_feat;

	p_feat = &p_hwfn->p_dcbx_info->local_admin.features;
	qed_dcbx_get_common_params(p_hwfn, &p_feat->app,
				   p_feat->app.app_pri_tbl, &p_feat->ets,
				   p_feat->pfc, &params->local.params);
	params->local.valid = true;
}

static void
qed_dcbx_get_remote_params(struct qed_hwfn *p_hwfn,
			   struct qed_ptt *p_ptt, struct qed_dcbx_get *params)
{
	struct dcbx_features *p_feat;

	p_feat = &p_hwfn->p_dcbx_info->remote.features;
	qed_dcbx_get_common_params(p_hwfn, &p_feat->app,
				   p_feat->app.app_pri_tbl, &p_feat->ets,
				   p_feat->pfc, &params->remote.params);
	params->remote.valid = true;
}

static void
qed_dcbx_get_operational_params(struct qed_hwfn *p_hwfn,
				struct qed_ptt *p_ptt,
				struct qed_dcbx_get *params)
{
	struct qed_dcbx_operational_params *p_operational;
	struct qed_dcbx_results *p_results;
	struct dcbx_features *p_feat;
	bool enabled, err;
	u32 flags;
	bool val;

	flags = p_hwfn->p_dcbx_info->operational.flags;

	/* If DCBx version is non zero, then negotiation
	 * was successfuly performed
	 */
	p_operational = &params->operational;
	enabled = !!(QED_MFW_GET_FIELD(flags, DCBX_CONFIG_VERSION) !=
		     DCBX_CONFIG_VERSION_DISABLED);
	if (!enabled) {
		p_operational->enabled = enabled;
		p_operational->valid = false;
		return;
	}

	p_feat = &p_hwfn->p_dcbx_info->operational.features;
	p_results = &p_hwfn->p_dcbx_info->results;

	val = !!(QED_MFW_GET_FIELD(flags, DCBX_CONFIG_VERSION) ==
		 DCBX_CONFIG_VERSION_IEEE);
	p_operational->ieee = val;
	val = !!(QED_MFW_GET_FIELD(flags, DCBX_CONFIG_VERSION) ==
		 DCBX_CONFIG_VERSION_CEE);
	p_operational->cee = val;

	DP_VERBOSE(p_hwfn, QED_MSG_DCB, "Version support: ieee %d, cee %d\n",
		   p_operational->ieee, p_operational->cee);

	qed_dcbx_get_common_params(p_hwfn, &p_feat->app,
				   p_feat->app.app_pri_tbl, &p_feat->ets,
				   p_feat->pfc, &params->operational.params);
	qed_dcbx_get_priority_info(p_hwfn, &p_operational->app_prio, p_results);
	err = QED_MFW_GET_FIELD(p_feat->app.flags, DCBX_APP_ERROR);
	p_operational->err = err;
	p_operational->enabled = enabled;
	p_operational->valid = true;
}

static void
qed_dcbx_get_local_lldp_params(struct qed_hwfn *p_hwfn,
			       struct qed_ptt *p_ptt,
			       struct qed_dcbx_get *params)
{
	struct lldp_config_params_s *p_local;

	p_local = &p_hwfn->p_dcbx_info->lldp_local[LLDP_NEAREST_BRIDGE];

	memcpy(params->lldp_local.local_chassis_id, p_local->local_chassis_id,
	       ARRAY_SIZE(p_local->local_chassis_id));
	memcpy(params->lldp_local.local_port_id, p_local->local_port_id,
	       ARRAY_SIZE(p_local->local_port_id));
}

static void
qed_dcbx_get_remote_lldp_params(struct qed_hwfn *p_hwfn,
				struct qed_ptt *p_ptt,
				struct qed_dcbx_get *params)
{
	struct lldp_status_params_s *p_remote;

	p_remote = &p_hwfn->p_dcbx_info->lldp_remote[LLDP_NEAREST_BRIDGE];

	memcpy(params->lldp_remote.peer_chassis_id, p_remote->peer_chassis_id,
	       ARRAY_SIZE(p_remote->peer_chassis_id));
	memcpy(params->lldp_remote.peer_port_id, p_remote->peer_port_id,
	       ARRAY_SIZE(p_remote->peer_port_id));
}

static int
qed_dcbx_get_params(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt,
		    struct qed_dcbx_get *p_params,
		    enum qed_mib_read_type type)
{
	switch (type) {
	case QED_DCBX_REMOTE_MIB:
		qed_dcbx_get_remote_params(p_hwfn, p_ptt, p_params);
		break;
	case QED_DCBX_LOCAL_MIB:
		qed_dcbx_get_local_params(p_hwfn, p_ptt, p_params);
		break;
	case QED_DCBX_OPERATIONAL_MIB:
		qed_dcbx_get_operational_params(p_hwfn, p_ptt, p_params);
		break;
	case QED_DCBX_REMOTE_LLDP_MIB:
		qed_dcbx_get_remote_lldp_params(p_hwfn, p_ptt, p_params);
		break;
	case QED_DCBX_LOCAL_LLDP_MIB:
		qed_dcbx_get_local_lldp_params(p_hwfn, p_ptt, p_params);
		break;
	default:
		DP_ERR(p_hwfn, "MIB read err, unknown mib type %d\n", type);
		return -EINVAL;
	}

	return 0;
}
#endif

static int
qed_dcbx_read_local_lldp_mib(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	struct qed_dcbx_mib_meta_data data;
	int rc = 0;

	memset(&data, 0, sizeof(data));
	data.addr = p_hwfn->mcp_info->port_addr + offsetof(struct public_port,
							   lldp_config_params);
	data.lldp_local = p_hwfn->p_dcbx_info->lldp_local;
	data.size = sizeof(struct lldp_config_params_s);
	qed_memcpy_from(p_hwfn, p_ptt, data.lldp_local, data.addr, data.size);

	return rc;
}

static int
qed_dcbx_read_remote_lldp_mib(struct qed_hwfn *p_hwfn,
			      struct qed_ptt *p_ptt,
			      enum qed_mib_read_type type)
{
	struct qed_dcbx_mib_meta_data data;
	int rc = 0;

	memset(&data, 0, sizeof(data));
	data.addr = p_hwfn->mcp_info->port_addr + offsetof(struct public_port,
							   lldp_status_params);
	data.lldp_remote = p_hwfn->p_dcbx_info->lldp_remote;
	data.size = sizeof(struct lldp_status_params_s);
	rc = qed_dcbx_copy_mib(p_hwfn, p_ptt, &data, type);

	return rc;
}

static int
qed_dcbx_read_operational_mib(struct qed_hwfn *p_hwfn,
			      struct qed_ptt *p_ptt,
			      enum qed_mib_read_type type)
{
	struct qed_dcbx_mib_meta_data data;
	int rc = 0;

	memset(&data, 0, sizeof(data));
	data.addr = p_hwfn->mcp_info->port_addr +
		    offsetof(struct public_port, operational_dcbx_mib);
	data.mib = &p_hwfn->p_dcbx_info->operational;
	data.size = sizeof(struct dcbx_mib);
	rc = qed_dcbx_copy_mib(p_hwfn, p_ptt, &data, type);

	return rc;
}

static int
qed_dcbx_read_remote_mib(struct qed_hwfn *p_hwfn,
			 struct qed_ptt *p_ptt, enum qed_mib_read_type type)
{
	struct qed_dcbx_mib_meta_data data;
	int rc = 0;

	memset(&data, 0, sizeof(data));
	data.addr = p_hwfn->mcp_info->port_addr +
		    offsetof(struct public_port, remote_dcbx_mib);
	data.mib = &p_hwfn->p_dcbx_info->remote;
	data.size = sizeof(struct dcbx_mib);
	rc = qed_dcbx_copy_mib(p_hwfn, p_ptt, &data, type);

	return rc;
}

static int
qed_dcbx_read_local_mib(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	struct qed_dcbx_mib_meta_data data;
	int rc = 0;

	memset(&data, 0, sizeof(data));
	data.addr = p_hwfn->mcp_info->port_addr +
		    offsetof(struct public_port, local_admin_dcbx_mib);
	data.local_admin = &p_hwfn->p_dcbx_info->local_admin;
	data.size = sizeof(struct dcbx_local_params);
	qed_memcpy_from(p_hwfn, p_ptt, data.local_admin, data.addr, data.size);

	return rc;
}

static int qed_dcbx_read_mib(struct qed_hwfn *p_hwfn,
			     struct qed_ptt *p_ptt, enum qed_mib_read_type type)
{
	int rc = -EINVAL;

	switch (type) {
	case QED_DCBX_OPERATIONAL_MIB:
		rc = qed_dcbx_read_operational_mib(p_hwfn, p_ptt, type);
		break;
	case QED_DCBX_REMOTE_MIB:
		rc = qed_dcbx_read_remote_mib(p_hwfn, p_ptt, type);
		break;
	case QED_DCBX_LOCAL_MIB:
		rc = qed_dcbx_read_local_mib(p_hwfn, p_ptt);
		break;
	case QED_DCBX_REMOTE_LLDP_MIB:
		rc = qed_dcbx_read_remote_lldp_mib(p_hwfn, p_ptt, type);
		break;
	case QED_DCBX_LOCAL_LLDP_MIB:
		rc = qed_dcbx_read_local_lldp_mib(p_hwfn, p_ptt);
		break;
	default:
		DP_ERR(p_hwfn, "MIB read err, unknown mib type %d\n", type);
	}

	return rc;
}

/* Read updated MIB.
 * Reconfigure QM and invoke PF update ramrod command if operational MIB
 * change is detected.
 */
int
qed_dcbx_mib_update_event(struct qed_hwfn *p_hwfn,
			  struct qed_ptt *p_ptt, enum qed_mib_read_type type)
{
	int rc = 0;

	rc = qed_dcbx_read_mib(p_hwfn, p_ptt, type);
	if (rc)
		return rc;

	if (type == QED_DCBX_OPERATIONAL_MIB) {
		rc = qed_dcbx_process_mib_info(p_hwfn);
		if (!rc) {
			/* reconfigure tcs of QM queues according
			 * to negotiation results
			 */
			qed_qm_reconf(p_hwfn, p_ptt);

			/* update storm FW with negotiation results */
			qed_sp_pf_update(p_hwfn);
		}
	}

	return rc;
}

int qed_dcbx_info_alloc(struct qed_hwfn *p_hwfn)
{
	int rc = 0;

	p_hwfn->p_dcbx_info = kzalloc(sizeof(*p_hwfn->p_dcbx_info), GFP_KERNEL);
	if (!p_hwfn->p_dcbx_info) {
		DP_NOTICE(p_hwfn,
			  "Failed to allocate 'struct qed_dcbx_info'\n");
		rc = -ENOMEM;
	}

	return rc;
}

void qed_dcbx_info_free(struct qed_hwfn *p_hwfn,
			struct qed_dcbx_info *p_dcbx_info)
{
	kfree(p_hwfn->p_dcbx_info);
}

static void qed_dcbx_update_protocol_data(struct protocol_dcb_data *p_data,
					  struct qed_dcbx_results *p_src,
					  enum dcbx_protocol_type type)
{
	p_data->dcb_enable_flag = p_src->arr[type].enable;
	p_data->dcb_priority = p_src->arr[type].priority;
	p_data->dcb_tc = p_src->arr[type].tc;
}

/* Set pf update ramrod command params */
void qed_dcbx_set_pf_update_params(struct qed_dcbx_results *p_src,
				   struct pf_update_ramrod_data *p_dest)
{
	struct protocol_dcb_data *p_dcb_data;
	bool update_flag = false;

	p_dest->pf_id = p_src->pf_id;

	update_flag = p_src->arr[DCBX_PROTOCOL_FCOE].update;
	p_dest->update_fcoe_dcb_data_flag = update_flag;

	update_flag = p_src->arr[DCBX_PROTOCOL_ROCE].update;
	p_dest->update_roce_dcb_data_flag = update_flag;
	update_flag = p_src->arr[DCBX_PROTOCOL_ROCE_V2].update;
	p_dest->update_roce_dcb_data_flag = update_flag;

	update_flag = p_src->arr[DCBX_PROTOCOL_ISCSI].update;
	p_dest->update_iscsi_dcb_data_flag = update_flag;
	update_flag = p_src->arr[DCBX_PROTOCOL_ETH].update;
	p_dest->update_eth_dcb_data_flag = update_flag;

	p_dcb_data = &p_dest->fcoe_dcb_data;
	qed_dcbx_update_protocol_data(p_dcb_data, p_src, DCBX_PROTOCOL_FCOE);
	p_dcb_data = &p_dest->roce_dcb_data;

	if (p_src->arr[DCBX_PROTOCOL_ROCE].update)
		qed_dcbx_update_protocol_data(p_dcb_data, p_src,
					      DCBX_PROTOCOL_ROCE);
	if (p_src->arr[DCBX_PROTOCOL_ROCE_V2].update)
		qed_dcbx_update_protocol_data(p_dcb_data, p_src,
					      DCBX_PROTOCOL_ROCE_V2);

	p_dcb_data = &p_dest->iscsi_dcb_data;
	qed_dcbx_update_protocol_data(p_dcb_data, p_src, DCBX_PROTOCOL_ISCSI);
	p_dcb_data = &p_dest->eth_dcb_data;
	qed_dcbx_update_protocol_data(p_dcb_data, p_src, DCBX_PROTOCOL_ETH);
}

#ifdef CONFIG_DCB
static int qed_dcbx_query_params(struct qed_hwfn *p_hwfn,
				 struct qed_dcbx_get *p_get,
				 enum qed_mib_read_type type)
{
	struct qed_ptt *p_ptt;
	int rc;

	p_ptt = qed_ptt_acquire(p_hwfn);
	if (!p_ptt)
		return -EBUSY;

	rc = qed_dcbx_read_mib(p_hwfn, p_ptt, type);
	if (rc)
		goto out;

	rc = qed_dcbx_get_params(p_hwfn, p_ptt, p_get, type);

out:
	qed_ptt_release(p_hwfn, p_ptt);
	return rc;
}

static void
qed_dcbx_set_pfc_data(struct qed_hwfn *p_hwfn,
		      u32 *pfc, struct qed_dcbx_params *p_params)
{
	u8 pfc_map = 0;
	int i;

	if (p_params->pfc.willing)
		*pfc |= DCBX_PFC_WILLING_MASK;
	else
		*pfc &= ~DCBX_PFC_WILLING_MASK;

	if (p_params->pfc.enabled)
		*pfc |= DCBX_PFC_ENABLED_MASK;
	else
		*pfc &= ~DCBX_PFC_ENABLED_MASK;

	*pfc &= ~DCBX_PFC_CAPS_MASK;
	*pfc |= (u32)p_params->pfc.max_tc << DCBX_PFC_CAPS_SHIFT;

	for (i = 0; i < QED_MAX_PFC_PRIORITIES; i++)
		if (p_params->pfc.prio[i])
			pfc_map |= BIT(i);

	*pfc |= (pfc_map << DCBX_PFC_PRI_EN_BITMAP_SHIFT);

	DP_VERBOSE(p_hwfn, QED_MSG_DCB, "pfc = 0x%x\n", *pfc);
}

static void
qed_dcbx_set_ets_data(struct qed_hwfn *p_hwfn,
		      struct dcbx_ets_feature *p_ets,
		      struct qed_dcbx_params *p_params)
{
	u8 *bw_map, *tsa_map;
	u32 val;
	int i;

	if (p_params->ets_willing)
		p_ets->flags |= DCBX_ETS_WILLING_MASK;
	else
		p_ets->flags &= ~DCBX_ETS_WILLING_MASK;

	if (p_params->ets_cbs)
		p_ets->flags |= DCBX_ETS_CBS_MASK;
	else
		p_ets->flags &= ~DCBX_ETS_CBS_MASK;

	if (p_params->ets_enabled)
		p_ets->flags |= DCBX_ETS_ENABLED_MASK;
	else
		p_ets->flags &= ~DCBX_ETS_ENABLED_MASK;

	p_ets->flags &= ~DCBX_ETS_MAX_TCS_MASK;
	p_ets->flags |= (u32)p_params->max_ets_tc << DCBX_ETS_MAX_TCS_SHIFT;

	bw_map = (u8 *)&p_ets->tc_bw_tbl[0];
	tsa_map = (u8 *)&p_ets->tc_tsa_tbl[0];
	p_ets->pri_tc_tbl[0] = 0;
	for (i = 0; i < QED_MAX_PFC_PRIORITIES; i++) {
		bw_map[i] = p_params->ets_tc_bw_tbl[i];
		tsa_map[i] = p_params->ets_tc_tsa_tbl[i];
		/* Copy the priority value to the corresponding 4 bits in the
		 * traffic class table.
		 */
		val = (((u32)p_params->ets_pri_tc_tbl[i]) << ((7 - i) * 4));
		p_ets->pri_tc_tbl[0] |= val;
	}
	p_ets->pri_tc_tbl[0] = cpu_to_be32(p_ets->pri_tc_tbl[0]);
	for (i = 0; i < 2; i++) {
		p_ets->tc_bw_tbl[i] = cpu_to_be32(p_ets->tc_bw_tbl[i]);
		p_ets->tc_tsa_tbl[i] = cpu_to_be32(p_ets->tc_tsa_tbl[i]);
	}
}

static void
qed_dcbx_set_app_data(struct qed_hwfn *p_hwfn,
		      struct dcbx_app_priority_feature *p_app,
		      struct qed_dcbx_params *p_params)
{
	u32 *entry;
	int i;

	if (p_params->app_willing)
		p_app->flags |= DCBX_APP_WILLING_MASK;
	else
		p_app->flags &= ~DCBX_APP_WILLING_MASK;

	if (p_params->app_valid)
		p_app->flags |= DCBX_APP_ENABLED_MASK;
	else
		p_app->flags &= ~DCBX_APP_ENABLED_MASK;

	p_app->flags &= ~DCBX_APP_NUM_ENTRIES_MASK;
	p_app->flags |= (u32)p_params->num_app_entries <<
	    DCBX_APP_NUM_ENTRIES_SHIFT;

	for (i = 0; i < DCBX_MAX_APP_PROTOCOL; i++) {
		entry = &p_app->app_pri_tbl[i].entry;
		*entry &= ~DCBX_APP_SF_MASK;
		if (p_params->app_entry[i].ethtype)
			*entry |= ((u32)DCBX_APP_SF_ETHTYPE <<
				   DCBX_APP_SF_SHIFT);
		else
			*entry |= ((u32)DCBX_APP_SF_PORT << DCBX_APP_SF_SHIFT);
		*entry &= ~DCBX_APP_PROTOCOL_ID_MASK;
		*entry |= ((u32)p_params->app_entry[i].proto_id <<
			   DCBX_APP_PROTOCOL_ID_SHIFT);
		*entry &= ~DCBX_APP_PRI_MAP_MASK;
		*entry |= ((u32)(p_params->app_entry[i].prio) <<
			   DCBX_APP_PRI_MAP_SHIFT);
	}
}

static void
qed_dcbx_set_local_params(struct qed_hwfn *p_hwfn,
			  struct dcbx_local_params *local_admin,
			  struct qed_dcbx_set *params)
{
	local_admin->flags = 0;
	memcpy(&local_admin->features,
	       &p_hwfn->p_dcbx_info->operational.features,
	       sizeof(local_admin->features));

	if (params->enabled)
		local_admin->config = params->ver_num;
	else
		local_admin->config = DCBX_CONFIG_VERSION_DISABLED;

	if (params->override_flags & QED_DCBX_OVERRIDE_PFC_CFG)
		qed_dcbx_set_pfc_data(p_hwfn, &local_admin->features.pfc,
				      &params->config.params);

	if (params->override_flags & QED_DCBX_OVERRIDE_ETS_CFG)
		qed_dcbx_set_ets_data(p_hwfn, &local_admin->features.ets,
				      &params->config.params);

	if (params->override_flags & QED_DCBX_OVERRIDE_APP_CFG)
		qed_dcbx_set_app_data(p_hwfn, &local_admin->features.app,
				      &params->config.params);
}

int qed_dcbx_config_params(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt,
			   struct qed_dcbx_set *params, bool hw_commit)
{
	struct dcbx_local_params local_admin;
	struct qed_dcbx_mib_meta_data data;
	u32 resp = 0, param = 0;
	int rc = 0;

	if (!hw_commit) {
		memcpy(&p_hwfn->p_dcbx_info->set, params,
		       sizeof(struct qed_dcbx_set));
		return 0;
	}

	/* clear set-parmas cache */
	memset(&p_hwfn->p_dcbx_info->set, 0, sizeof(p_hwfn->p_dcbx_info->set));

	memset(&local_admin, 0, sizeof(local_admin));
	qed_dcbx_set_local_params(p_hwfn, &local_admin, params);

	data.addr = p_hwfn->mcp_info->port_addr +
	    offsetof(struct public_port, local_admin_dcbx_mib);
	data.local_admin = &local_admin;
	data.size = sizeof(struct dcbx_local_params);
	qed_memcpy_to(p_hwfn, p_ptt, data.addr, data.local_admin, data.size);

	rc = qed_mcp_cmd(p_hwfn, p_ptt, DRV_MSG_CODE_SET_DCBX,
			 1 << DRV_MB_PARAM_LLDP_SEND_SHIFT, &resp, &param);
	if (rc)
		DP_NOTICE(p_hwfn, "Failed to send DCBX update request\n");

	return rc;
}

int qed_dcbx_get_config_params(struct qed_hwfn *p_hwfn,
			       struct qed_dcbx_set *params)
{
	struct qed_dcbx_get *dcbx_info;
	int rc;

	if (p_hwfn->p_dcbx_info->set.config.valid) {
		memcpy(params, &p_hwfn->p_dcbx_info->set,
		       sizeof(struct qed_dcbx_set));
		return 0;
	}

	dcbx_info = kmalloc(sizeof(*dcbx_info), GFP_KERNEL);
	if (!dcbx_info) {
		DP_ERR(p_hwfn, "Failed to allocate struct qed_dcbx_info\n");
		return -ENOMEM;
	}

	rc = qed_dcbx_query_params(p_hwfn, dcbx_info, QED_DCBX_OPERATIONAL_MIB);
	if (rc) {
		kfree(dcbx_info);
		return rc;
	}

	p_hwfn->p_dcbx_info->set.override_flags = 0;
	p_hwfn->p_dcbx_info->set.ver_num = DCBX_CONFIG_VERSION_DISABLED;
	if (dcbx_info->operational.cee)
		p_hwfn->p_dcbx_info->set.ver_num |= DCBX_CONFIG_VERSION_CEE;
	if (dcbx_info->operational.ieee)
		p_hwfn->p_dcbx_info->set.ver_num |= DCBX_CONFIG_VERSION_IEEE;

	p_hwfn->p_dcbx_info->set.enabled = dcbx_info->operational.enabled;
	memcpy(&p_hwfn->p_dcbx_info->set.config.params,
	       &dcbx_info->operational.params,
	       sizeof(struct qed_dcbx_admin_params));
	p_hwfn->p_dcbx_info->set.config.valid = true;

	memcpy(params, &p_hwfn->p_dcbx_info->set, sizeof(struct qed_dcbx_set));

	kfree(dcbx_info);

	return 0;
}
#endif
