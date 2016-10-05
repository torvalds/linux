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
#include <linux/dcbnl.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include "qed.h"
#include "qed_cxt.h"
#include "qed_dcbx.h"
#include "qed_hsi.h"
#include "qed_sp.h"
#include "qed_sriov.h"
#ifdef CONFIG_DCB
#include <linux/qed/qed_eth_if.h>
#endif

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

static bool qed_dcbx_ieee_app_ethtype(u32 app_info_bitmap)
{
	u8 mfw_val = QED_MFW_GET_FIELD(app_info_bitmap, DCBX_APP_SF_IEEE);

	/* Old MFW */
	if (mfw_val == DCBX_APP_SF_IEEE_RESERVED)
		return qed_dcbx_app_ethtype(app_info_bitmap);

	return !!(mfw_val == DCBX_APP_SF_IEEE_ETHTYPE);
}

static bool qed_dcbx_app_port(u32 app_info_bitmap)
{
	return !!(QED_MFW_GET_FIELD(app_info_bitmap, DCBX_APP_SF) ==
		  DCBX_APP_SF_PORT);
}

static bool qed_dcbx_ieee_app_port(u32 app_info_bitmap, u8 type)
{
	u8 mfw_val = QED_MFW_GET_FIELD(app_info_bitmap, DCBX_APP_SF_IEEE);

	/* Old MFW */
	if (mfw_val == DCBX_APP_SF_IEEE_RESERVED)
		return qed_dcbx_app_port(app_info_bitmap);

	return !!(mfw_val == type || mfw_val == DCBX_APP_SF_IEEE_TCP_UDP_PORT);
}

static bool qed_dcbx_default_tlv(u32 app_info_bitmap, u16 proto_id, bool ieee)
{
	bool ethtype;

	if (ieee)
		ethtype = qed_dcbx_ieee_app_ethtype(app_info_bitmap);
	else
		ethtype = qed_dcbx_app_ethtype(app_info_bitmap);

	return !!(ethtype && (proto_id == QED_ETH_TYPE_DEFAULT));
}

static bool qed_dcbx_iscsi_tlv(u32 app_info_bitmap, u16 proto_id, bool ieee)
{
	bool port;

	if (ieee)
		port = qed_dcbx_ieee_app_port(app_info_bitmap,
					      DCBX_APP_SF_IEEE_TCP_PORT);
	else
		port = qed_dcbx_app_port(app_info_bitmap);

	return !!(port && (proto_id == QED_TCP_PORT_ISCSI));
}

static bool qed_dcbx_fcoe_tlv(u32 app_info_bitmap, u16 proto_id, bool ieee)
{
	bool ethtype;

	if (ieee)
		ethtype = qed_dcbx_ieee_app_ethtype(app_info_bitmap);
	else
		ethtype = qed_dcbx_app_ethtype(app_info_bitmap);

	return !!(ethtype && (proto_id == QED_ETH_TYPE_FCOE));
}

static bool qed_dcbx_roce_tlv(u32 app_info_bitmap, u16 proto_id, bool ieee)
{
	bool ethtype;

	if (ieee)
		ethtype = qed_dcbx_ieee_app_ethtype(app_info_bitmap);
	else
		ethtype = qed_dcbx_app_ethtype(app_info_bitmap);

	return !!(ethtype && (proto_id == QED_ETH_TYPE_ROCE));
}

static bool qed_dcbx_roce_v2_tlv(u32 app_info_bitmap, u16 proto_id, bool ieee)
{
	bool port;

	if (ieee)
		port = qed_dcbx_ieee_app_port(app_info_bitmap,
					      DCBX_APP_SF_IEEE_UDP_PORT);
	else
		port = qed_dcbx_app_port(app_info_bitmap);

	return !!(port && (proto_id == QED_UDP_PORT_TYPE_ROCE_V2));
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
			       u16 id, enum dcbx_protocol_type *type, bool ieee)
{
	if (qed_dcbx_fcoe_tlv(app_prio_bitmap, id, ieee)) {
		*type = DCBX_PROTOCOL_FCOE;
	} else if (qed_dcbx_roce_tlv(app_prio_bitmap, id, ieee)) {
		*type = DCBX_PROTOCOL_ROCE;
	} else if (qed_dcbx_iscsi_tlv(app_prio_bitmap, id, ieee)) {
		*type = DCBX_PROTOCOL_ISCSI;
	} else if (qed_dcbx_default_tlv(app_prio_bitmap, id, ieee)) {
		*type = DCBX_PROTOCOL_ETH;
	} else if (qed_dcbx_roce_v2_tlv(app_prio_bitmap, id, ieee)) {
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
		     u32 pri_tc_tbl, int count, u8 dcbx_version)
{
	u8 tc, priority_map;
	enum dcbx_protocol_type type;
	bool enable, ieee;
	u16 protocol_id;
	int priority;
	int i;

	DP_VERBOSE(p_hwfn, QED_MSG_DCB, "Num APP entries = %d\n", count);

	ieee = (dcbx_version == DCBX_CONFIG_VERSION_IEEE);
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
						   protocol_id, &type, ieee)) {
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
	u8 dcbx_version;
	int num_entries;
	int rc = 0;

	flags = p_hwfn->p_dcbx_info->operational.flags;
	dcbx_version = QED_MFW_GET_FIELD(flags, DCBX_CONFIG_VERSION);

	p_app = &p_hwfn->p_dcbx_info->operational.features.app;
	p_tbl = p_app->app_pri_tbl;

	p_ets = &p_hwfn->p_dcbx_info->operational.features.ets;
	pri_tc_tbl = p_ets->pri_tc_tbl[0];

	p_info = &p_hwfn->hw_info;
	num_entries = QED_MFW_GET_FIELD(p_app->flags, DCBX_APP_NUM_ENTRIES);

	rc = qed_dcbx_process_tlv(p_hwfn, &data, p_tbl, pri_tc_tbl,
				  num_entries, dcbx_version);
	if (rc)
		return rc;

	p_info->num_tc = QED_MFW_GET_FIELD(p_ets->flags, DCBX_ETS_MAX_TCS);
	data.pf_id = p_hwfn->rel_pf_id;
	data.dcbx_enabled = !!dcbx_version;

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
		      struct qed_dcbx_params *p_params, bool ieee)
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
		if (ieee) {
			u8 sf_ieee;
			u32 val;

			sf_ieee = QED_MFW_GET_FIELD(p_tbl[i].entry,
						    DCBX_APP_SF_IEEE);
			switch (sf_ieee) {
			case DCBX_APP_SF_IEEE_RESERVED:
				/* Old MFW */
				val = QED_MFW_GET_FIELD(p_tbl[i].entry,
							DCBX_APP_SF);
				entry->sf_ieee = val ?
				    QED_DCBX_SF_IEEE_TCP_UDP_PORT :
				    QED_DCBX_SF_IEEE_ETHTYPE;
				break;
			case DCBX_APP_SF_IEEE_ETHTYPE:
				entry->sf_ieee = QED_DCBX_SF_IEEE_ETHTYPE;
				break;
			case DCBX_APP_SF_IEEE_TCP_PORT:
				entry->sf_ieee = QED_DCBX_SF_IEEE_TCP_PORT;
				break;
			case DCBX_APP_SF_IEEE_UDP_PORT:
				entry->sf_ieee = QED_DCBX_SF_IEEE_UDP_PORT;
				break;
			case DCBX_APP_SF_IEEE_TCP_UDP_PORT:
				entry->sf_ieee = QED_DCBX_SF_IEEE_TCP_UDP_PORT;
				break;
			}
		} else {
			entry->ethtype = !(QED_MFW_GET_FIELD(p_tbl[i].entry,
							     DCBX_APP_SF));
		}

		pri_map = QED_MFW_GET_FIELD(p_tbl[i].entry, DCBX_APP_PRI_MAP);
		entry->prio = ffs(pri_map) - 1;
		entry->proto_id = QED_MFW_GET_FIELD(p_tbl[i].entry,
						    DCBX_APP_PROTOCOL_ID);
		qed_dcbx_get_app_protocol_type(p_hwfn, p_tbl[i].entry,
					       entry->proto_id,
					       &entry->proto_type, ieee);
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
	pri_map = p_ets->pri_tc_tbl[0];
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
			   u32 pfc, struct qed_dcbx_params *p_params, bool ieee)
{
	qed_dcbx_get_app_data(p_hwfn, p_app, p_tbl, p_params, ieee);
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
				   p_feat->pfc, &params->local.params, false);
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
				   p_feat->pfc, &params->remote.params, false);
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
				   p_feat->pfc, &params->operational.params,
				   p_operational->ieee);
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
	if (!p_hwfn->p_dcbx_info)
		rc = -ENOMEM;

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

	if (IS_VF(p_hwfn->cdev))
		return -EINVAL;

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

	*pfc &= ~DCBX_PFC_PRI_EN_BITMAP_MASK;
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
	for (i = 0; i < 2; i++) {
		p_ets->tc_bw_tbl[i] = cpu_to_be32(p_ets->tc_bw_tbl[i]);
		p_ets->tc_tsa_tbl[i] = cpu_to_be32(p_ets->tc_tsa_tbl[i]);
	}
}

static void
qed_dcbx_set_app_data(struct qed_hwfn *p_hwfn,
		      struct dcbx_app_priority_feature *p_app,
		      struct qed_dcbx_params *p_params, bool ieee)
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
		*entry = 0;
		if (ieee) {
			*entry &= ~(DCBX_APP_SF_IEEE_MASK | DCBX_APP_SF_MASK);
			switch (p_params->app_entry[i].sf_ieee) {
			case QED_DCBX_SF_IEEE_ETHTYPE:
				*entry |= ((u32)DCBX_APP_SF_IEEE_ETHTYPE <<
					   DCBX_APP_SF_IEEE_SHIFT);
				*entry |= ((u32)DCBX_APP_SF_ETHTYPE <<
					   DCBX_APP_SF_SHIFT);
				break;
			case QED_DCBX_SF_IEEE_TCP_PORT:
				*entry |= ((u32)DCBX_APP_SF_IEEE_TCP_PORT <<
					   DCBX_APP_SF_IEEE_SHIFT);
				*entry |= ((u32)DCBX_APP_SF_PORT <<
					   DCBX_APP_SF_SHIFT);
				break;
			case QED_DCBX_SF_IEEE_UDP_PORT:
				*entry |= ((u32)DCBX_APP_SF_IEEE_UDP_PORT <<
					   DCBX_APP_SF_IEEE_SHIFT);
				*entry |= ((u32)DCBX_APP_SF_PORT <<
					   DCBX_APP_SF_SHIFT);
				break;
			case QED_DCBX_SF_IEEE_TCP_UDP_PORT:
				*entry |= ((u32)DCBX_APP_SF_IEEE_TCP_UDP_PORT <<
					   DCBX_APP_SF_IEEE_SHIFT);
				*entry |= ((u32)DCBX_APP_SF_PORT <<
					   DCBX_APP_SF_SHIFT);
				break;
			}
		} else {
			*entry &= ~DCBX_APP_SF_MASK;
			if (p_params->app_entry[i].ethtype)
				*entry |= ((u32)DCBX_APP_SF_ETHTYPE <<
					   DCBX_APP_SF_SHIFT);
			else
				*entry |= ((u32)DCBX_APP_SF_PORT <<
					   DCBX_APP_SF_SHIFT);
		}

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
	bool ieee = false;

	local_admin->flags = 0;
	memcpy(&local_admin->features,
	       &p_hwfn->p_dcbx_info->operational.features,
	       sizeof(local_admin->features));

	if (params->enabled) {
		local_admin->config = params->ver_num;
		ieee = !!(params->ver_num & DCBX_CONFIG_VERSION_IEEE);
	} else {
		local_admin->config = DCBX_CONFIG_VERSION_DISABLED;
	}

	if (params->override_flags & QED_DCBX_OVERRIDE_PFC_CFG)
		qed_dcbx_set_pfc_data(p_hwfn, &local_admin->features.pfc,
				      &params->config.params);

	if (params->override_flags & QED_DCBX_OVERRIDE_ETS_CFG)
		qed_dcbx_set_ets_data(p_hwfn, &local_admin->features.ets,
				      &params->config.params);

	if (params->override_flags & QED_DCBX_OVERRIDE_APP_CFG)
		qed_dcbx_set_app_data(p_hwfn, &local_admin->features.app,
				      &params->config.params, ieee);
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

	dcbx_info = kzalloc(sizeof(*dcbx_info), GFP_KERNEL);
	if (!dcbx_info)
		return -ENOMEM;

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

static struct qed_dcbx_get *qed_dcbnl_get_dcbx(struct qed_hwfn *hwfn,
					       enum qed_mib_read_type type)
{
	struct qed_dcbx_get *dcbx_info;

	dcbx_info = kzalloc(sizeof(*dcbx_info), GFP_KERNEL);
	if (!dcbx_info)
		return NULL;

	if (qed_dcbx_query_params(hwfn, dcbx_info, type)) {
		kfree(dcbx_info);
		return NULL;
	}

	if ((type == QED_DCBX_OPERATIONAL_MIB) &&
	    !dcbx_info->operational.enabled) {
		DP_INFO(hwfn, "DCBX is not enabled/operational\n");
		kfree(dcbx_info);
		return NULL;
	}

	return dcbx_info;
}

static u8 qed_dcbnl_getstate(struct qed_dev *cdev)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_dcbx_get *dcbx_info;
	bool enabled;

	dcbx_info = qed_dcbnl_get_dcbx(hwfn, QED_DCBX_OPERATIONAL_MIB);
	if (!dcbx_info)
		return 0;

	enabled = dcbx_info->operational.enabled;
	DP_VERBOSE(hwfn, QED_MSG_DCB, "DCB state = %d\n", enabled);
	kfree(dcbx_info);

	return enabled;
}

static u8 qed_dcbnl_setstate(struct qed_dev *cdev, u8 state)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_dcbx_set dcbx_set;
	struct qed_ptt *ptt;
	int rc;

	DP_VERBOSE(hwfn, QED_MSG_DCB, "DCB state = %d\n", state);

	memset(&dcbx_set, 0, sizeof(dcbx_set));
	rc = qed_dcbx_get_config_params(hwfn, &dcbx_set);
	if (rc)
		return 1;

	dcbx_set.enabled = !!state;

	ptt = qed_ptt_acquire(hwfn);
	if (!ptt)
		return 1;

	rc = qed_dcbx_config_params(hwfn, ptt, &dcbx_set, 0);

	qed_ptt_release(hwfn, ptt);

	return rc ? 1 : 0;
}

static void qed_dcbnl_getpgtccfgtx(struct qed_dev *cdev, int tc, u8 *prio_type,
				   u8 *pgid, u8 *bw_pct, u8 *up_map)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_dcbx_get *dcbx_info;

	DP_VERBOSE(hwfn, QED_MSG_DCB, "tc = %d\n", tc);
	*prio_type = *pgid = *bw_pct = *up_map = 0;
	if (tc < 0 || tc >= QED_MAX_PFC_PRIORITIES) {
		DP_INFO(hwfn, "Invalid tc %d\n", tc);
		return;
	}

	dcbx_info = qed_dcbnl_get_dcbx(hwfn, QED_DCBX_OPERATIONAL_MIB);
	if (!dcbx_info)
		return;

	*pgid = dcbx_info->operational.params.ets_pri_tc_tbl[tc];
	kfree(dcbx_info);
}

static void qed_dcbnl_getpgbwgcfgtx(struct qed_dev *cdev, int pgid, u8 *bw_pct)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_dcbx_get *dcbx_info;

	*bw_pct = 0;
	DP_VERBOSE(hwfn, QED_MSG_DCB, "pgid = %d\n", pgid);
	if (pgid < 0 || pgid >= QED_MAX_PFC_PRIORITIES) {
		DP_INFO(hwfn, "Invalid pgid %d\n", pgid);
		return;
	}

	dcbx_info = qed_dcbnl_get_dcbx(hwfn, QED_DCBX_OPERATIONAL_MIB);
	if (!dcbx_info)
		return;

	*bw_pct = dcbx_info->operational.params.ets_tc_bw_tbl[pgid];
	DP_VERBOSE(hwfn, QED_MSG_DCB, "bw_pct = %d\n", *bw_pct);
	kfree(dcbx_info);
}

static void qed_dcbnl_getpgtccfgrx(struct qed_dev *cdev, int tc, u8 *prio,
				   u8 *bwg_id, u8 *bw_pct, u8 *up_map)
{
	DP_INFO(QED_LEADING_HWFN(cdev), "Rx ETS is not supported\n");
	*prio = *bwg_id = *bw_pct = *up_map = 0;
}

static void qed_dcbnl_getpgbwgcfgrx(struct qed_dev *cdev,
				    int bwg_id, u8 *bw_pct)
{
	DP_INFO(QED_LEADING_HWFN(cdev), "Rx ETS is not supported\n");
	*bw_pct = 0;
}

static void qed_dcbnl_getpfccfg(struct qed_dev *cdev,
				int priority, u8 *setting)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_dcbx_get *dcbx_info;

	DP_VERBOSE(hwfn, QED_MSG_DCB, "priority = %d\n", priority);
	if (priority < 0 || priority >= QED_MAX_PFC_PRIORITIES) {
		DP_INFO(hwfn, "Invalid priority %d\n", priority);
		return;
	}

	dcbx_info = qed_dcbnl_get_dcbx(hwfn, QED_DCBX_OPERATIONAL_MIB);
	if (!dcbx_info)
		return;

	*setting = dcbx_info->operational.params.pfc.prio[priority];
	DP_VERBOSE(hwfn, QED_MSG_DCB, "setting = %d\n", *setting);
	kfree(dcbx_info);
}

static void qed_dcbnl_setpfccfg(struct qed_dev *cdev, int priority, u8 setting)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_dcbx_set dcbx_set;
	struct qed_ptt *ptt;
	int rc;

	DP_VERBOSE(hwfn, QED_MSG_DCB, "priority = %d setting = %d\n",
		   priority, setting);
	if (priority < 0 || priority >= QED_MAX_PFC_PRIORITIES) {
		DP_INFO(hwfn, "Invalid priority %d\n", priority);
		return;
	}

	memset(&dcbx_set, 0, sizeof(dcbx_set));
	rc = qed_dcbx_get_config_params(hwfn, &dcbx_set);
	if (rc)
		return;

	dcbx_set.override_flags |= QED_DCBX_OVERRIDE_PFC_CFG;
	dcbx_set.config.params.pfc.prio[priority] = !!setting;

	ptt = qed_ptt_acquire(hwfn);
	if (!ptt)
		return;

	rc = qed_dcbx_config_params(hwfn, ptt, &dcbx_set, 0);

	qed_ptt_release(hwfn, ptt);
}

static u8 qed_dcbnl_getcap(struct qed_dev *cdev, int capid, u8 *cap)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_dcbx_get *dcbx_info;
	int rc = 0;

	DP_VERBOSE(hwfn, QED_MSG_DCB, "capid = %d\n", capid);
	dcbx_info = qed_dcbnl_get_dcbx(hwfn, QED_DCBX_OPERATIONAL_MIB);
	if (!dcbx_info)
		return 1;

	switch (capid) {
	case DCB_CAP_ATTR_PG:
	case DCB_CAP_ATTR_PFC:
	case DCB_CAP_ATTR_UP2TC:
	case DCB_CAP_ATTR_GSP:
		*cap = true;
		break;
	case DCB_CAP_ATTR_PG_TCS:
	case DCB_CAP_ATTR_PFC_TCS:
		*cap = 0x80;
		break;
	case DCB_CAP_ATTR_DCBX:
		*cap = (DCB_CAP_DCBX_LLD_MANAGED | DCB_CAP_DCBX_VER_CEE |
			DCB_CAP_DCBX_VER_IEEE);
		break;
	default:
		*cap = false;
		rc = 1;
	}

	DP_VERBOSE(hwfn, QED_MSG_DCB, "id = %d caps = %d\n", capid, *cap);
	kfree(dcbx_info);

	return rc;
}

static int qed_dcbnl_getnumtcs(struct qed_dev *cdev, int tcid, u8 *num)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_dcbx_get *dcbx_info;
	int rc = 0;

	DP_VERBOSE(hwfn, QED_MSG_DCB, "tcid = %d\n", tcid);
	dcbx_info = qed_dcbnl_get_dcbx(hwfn, QED_DCBX_OPERATIONAL_MIB);
	if (!dcbx_info)
		return -EINVAL;

	switch (tcid) {
	case DCB_NUMTCS_ATTR_PG:
		*num = dcbx_info->operational.params.max_ets_tc;
		break;
	case DCB_NUMTCS_ATTR_PFC:
		*num = dcbx_info->operational.params.pfc.max_tc;
		break;
	default:
		rc = -EINVAL;
	}

	kfree(dcbx_info);
	DP_VERBOSE(hwfn, QED_MSG_DCB, "numtcs = %d\n", *num);

	return rc;
}

static u8 qed_dcbnl_getpfcstate(struct qed_dev *cdev)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_dcbx_get *dcbx_info;
	bool enabled;

	dcbx_info = qed_dcbnl_get_dcbx(hwfn, QED_DCBX_OPERATIONAL_MIB);
	if (!dcbx_info)
		return 0;

	enabled = dcbx_info->operational.params.pfc.enabled;
	DP_VERBOSE(hwfn, QED_MSG_DCB, "pfc state = %d\n", enabled);
	kfree(dcbx_info);

	return enabled;
}

static u8 qed_dcbnl_getdcbx(struct qed_dev *cdev)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_dcbx_get *dcbx_info;
	u8 mode = 0;

	dcbx_info = qed_dcbnl_get_dcbx(hwfn, QED_DCBX_OPERATIONAL_MIB);
	if (!dcbx_info)
		return 0;

	if (dcbx_info->operational.enabled)
		mode |= DCB_CAP_DCBX_LLD_MANAGED;
	if (dcbx_info->operational.ieee)
		mode |= DCB_CAP_DCBX_VER_IEEE;
	if (dcbx_info->operational.cee)
		mode |= DCB_CAP_DCBX_VER_CEE;

	DP_VERBOSE(hwfn, QED_MSG_DCB, "dcb mode = %d\n", mode);
	kfree(dcbx_info);

	return mode;
}

static void qed_dcbnl_setpgtccfgtx(struct qed_dev *cdev,
				   int tc,
				   u8 pri_type, u8 pgid, u8 bw_pct, u8 up_map)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_dcbx_set dcbx_set;
	struct qed_ptt *ptt;
	int rc;

	DP_VERBOSE(hwfn, QED_MSG_DCB,
		   "tc = %d pri_type = %d pgid = %d bw_pct = %d up_map = %d\n",
		   tc, pri_type, pgid, bw_pct, up_map);

	if (tc < 0 || tc >= QED_MAX_PFC_PRIORITIES) {
		DP_INFO(hwfn, "Invalid tc %d\n", tc);
		return;
	}

	memset(&dcbx_set, 0, sizeof(dcbx_set));
	rc = qed_dcbx_get_config_params(hwfn, &dcbx_set);
	if (rc)
		return;

	dcbx_set.override_flags |= QED_DCBX_OVERRIDE_ETS_CFG;
	dcbx_set.config.params.ets_pri_tc_tbl[tc] = pgid;

	ptt = qed_ptt_acquire(hwfn);
	if (!ptt)
		return;

	rc = qed_dcbx_config_params(hwfn, ptt, &dcbx_set, 0);

	qed_ptt_release(hwfn, ptt);
}

static void qed_dcbnl_setpgtccfgrx(struct qed_dev *cdev, int prio,
				   u8 pri_type, u8 pgid, u8 bw_pct, u8 up_map)
{
	DP_INFO(QED_LEADING_HWFN(cdev), "Rx ETS is not supported\n");
}

static void qed_dcbnl_setpgbwgcfgtx(struct qed_dev *cdev, int pgid, u8 bw_pct)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_dcbx_set dcbx_set;
	struct qed_ptt *ptt;
	int rc;

	DP_VERBOSE(hwfn, QED_MSG_DCB, "pgid = %d bw_pct = %d\n", pgid, bw_pct);
	if (pgid < 0 || pgid >= QED_MAX_PFC_PRIORITIES) {
		DP_INFO(hwfn, "Invalid pgid %d\n", pgid);
		return;
	}

	memset(&dcbx_set, 0, sizeof(dcbx_set));
	rc = qed_dcbx_get_config_params(hwfn, &dcbx_set);
	if (rc)
		return;

	dcbx_set.override_flags |= QED_DCBX_OVERRIDE_ETS_CFG;
	dcbx_set.config.params.ets_tc_bw_tbl[pgid] = bw_pct;

	ptt = qed_ptt_acquire(hwfn);
	if (!ptt)
		return;

	rc = qed_dcbx_config_params(hwfn, ptt, &dcbx_set, 0);

	qed_ptt_release(hwfn, ptt);
}

static void qed_dcbnl_setpgbwgcfgrx(struct qed_dev *cdev, int pgid, u8 bw_pct)
{
	DP_INFO(QED_LEADING_HWFN(cdev), "Rx ETS is not supported\n");
}

static u8 qed_dcbnl_setall(struct qed_dev *cdev)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_dcbx_set dcbx_set;
	struct qed_ptt *ptt;
	int rc;

	memset(&dcbx_set, 0, sizeof(dcbx_set));
	rc = qed_dcbx_get_config_params(hwfn, &dcbx_set);
	if (rc)
		return 1;

	ptt = qed_ptt_acquire(hwfn);
	if (!ptt)
		return 1;

	rc = qed_dcbx_config_params(hwfn, ptt, &dcbx_set, 1);

	qed_ptt_release(hwfn, ptt);

	return rc;
}

static int qed_dcbnl_setnumtcs(struct qed_dev *cdev, int tcid, u8 num)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_dcbx_set dcbx_set;
	struct qed_ptt *ptt;
	int rc;

	DP_VERBOSE(hwfn, QED_MSG_DCB, "tcid = %d num = %d\n", tcid, num);
	memset(&dcbx_set, 0, sizeof(dcbx_set));
	rc = qed_dcbx_get_config_params(hwfn, &dcbx_set);
	if (rc)
		return 1;

	switch (tcid) {
	case DCB_NUMTCS_ATTR_PG:
		dcbx_set.override_flags |= QED_DCBX_OVERRIDE_ETS_CFG;
		dcbx_set.config.params.max_ets_tc = num;
		break;
	case DCB_NUMTCS_ATTR_PFC:
		dcbx_set.override_flags |= QED_DCBX_OVERRIDE_PFC_CFG;
		dcbx_set.config.params.pfc.max_tc = num;
		break;
	default:
		DP_INFO(hwfn, "Invalid tcid %d\n", tcid);
		return -EINVAL;
	}

	ptt = qed_ptt_acquire(hwfn);
	if (!ptt)
		return -EINVAL;

	rc = qed_dcbx_config_params(hwfn, ptt, &dcbx_set, 0);

	qed_ptt_release(hwfn, ptt);

	return 0;
}

static void qed_dcbnl_setpfcstate(struct qed_dev *cdev, u8 state)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_dcbx_set dcbx_set;
	struct qed_ptt *ptt;
	int rc;

	DP_VERBOSE(hwfn, QED_MSG_DCB, "new state = %d\n", state);

	memset(&dcbx_set, 0, sizeof(dcbx_set));
	rc = qed_dcbx_get_config_params(hwfn, &dcbx_set);
	if (rc)
		return;

	dcbx_set.override_flags |= QED_DCBX_OVERRIDE_PFC_CFG;
	dcbx_set.config.params.pfc.enabled = !!state;

	ptt = qed_ptt_acquire(hwfn);
	if (!ptt)
		return;

	rc = qed_dcbx_config_params(hwfn, ptt, &dcbx_set, 0);

	qed_ptt_release(hwfn, ptt);
}

static int qed_dcbnl_getapp(struct qed_dev *cdev, u8 idtype, u16 idval)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_dcbx_get *dcbx_info;
	struct qed_app_entry *entry;
	bool ethtype;
	u8 prio = 0;
	int i;

	dcbx_info = qed_dcbnl_get_dcbx(hwfn, QED_DCBX_OPERATIONAL_MIB);
	if (!dcbx_info)
		return -EINVAL;

	ethtype = !!(idtype == DCB_APP_IDTYPE_ETHTYPE);
	for (i = 0; i < QED_DCBX_MAX_APP_PROTOCOL; i++) {
		entry = &dcbx_info->operational.params.app_entry[i];
		if ((entry->ethtype == ethtype) && (entry->proto_id == idval)) {
			prio = entry->prio;
			break;
		}
	}

	if (i == QED_DCBX_MAX_APP_PROTOCOL) {
		DP_ERR(cdev, "App entry (%d, %d) not found\n", idtype, idval);
		kfree(dcbx_info);
		return -EINVAL;
	}

	kfree(dcbx_info);

	return prio;
}

static int qed_dcbnl_setapp(struct qed_dev *cdev,
			    u8 idtype, u16 idval, u8 pri_map)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_dcbx_set dcbx_set;
	struct qed_app_entry *entry;
	struct qed_ptt *ptt;
	bool ethtype;
	int rc, i;

	memset(&dcbx_set, 0, sizeof(dcbx_set));
	rc = qed_dcbx_get_config_params(hwfn, &dcbx_set);
	if (rc)
		return -EINVAL;

	ethtype = !!(idtype == DCB_APP_IDTYPE_ETHTYPE);
	for (i = 0; i < QED_DCBX_MAX_APP_PROTOCOL; i++) {
		entry = &dcbx_set.config.params.app_entry[i];
		if ((entry->ethtype == ethtype) && (entry->proto_id == idval))
			break;
		/* First empty slot */
		if (!entry->proto_id) {
			dcbx_set.config.params.num_app_entries++;
			break;
		}
	}

	if (i == QED_DCBX_MAX_APP_PROTOCOL) {
		DP_ERR(cdev, "App table is full\n");
		return -EBUSY;
	}

	dcbx_set.override_flags |= QED_DCBX_OVERRIDE_APP_CFG;
	dcbx_set.config.params.app_entry[i].ethtype = ethtype;
	dcbx_set.config.params.app_entry[i].proto_id = idval;
	dcbx_set.config.params.app_entry[i].prio = pri_map;

	ptt = qed_ptt_acquire(hwfn);
	if (!ptt)
		return -EBUSY;

	rc = qed_dcbx_config_params(hwfn, ptt, &dcbx_set, 0);

	qed_ptt_release(hwfn, ptt);

	return rc;
}

static u8 qed_dcbnl_setdcbx(struct qed_dev *cdev, u8 mode)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_dcbx_set dcbx_set;
	struct qed_ptt *ptt;
	int rc;

	DP_VERBOSE(hwfn, QED_MSG_DCB, "new mode = %x\n", mode);

	if (!(mode & DCB_CAP_DCBX_VER_IEEE) && !(mode & DCB_CAP_DCBX_VER_CEE)) {
		DP_INFO(hwfn, "Allowed mode is cee, ieee or both\n");
		return 1;
	}

	memset(&dcbx_set, 0, sizeof(dcbx_set));
	rc = qed_dcbx_get_config_params(hwfn, &dcbx_set);
	if (rc)
		return 1;

	dcbx_set.ver_num = 0;
	if (mode & DCB_CAP_DCBX_VER_CEE) {
		dcbx_set.ver_num |= DCBX_CONFIG_VERSION_CEE;
		dcbx_set.enabled = true;
	}

	if (mode & DCB_CAP_DCBX_VER_IEEE) {
		dcbx_set.ver_num |= DCBX_CONFIG_VERSION_IEEE;
		dcbx_set.enabled = true;
	}

	ptt = qed_ptt_acquire(hwfn);
	if (!ptt)
		return 1;

	rc = qed_dcbx_config_params(hwfn, ptt, &dcbx_set, 0);

	qed_ptt_release(hwfn, ptt);

	return 0;
}

static u8 qed_dcbnl_getfeatcfg(struct qed_dev *cdev, int featid, u8 *flags)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_dcbx_get *dcbx_info;

	DP_VERBOSE(hwfn, QED_MSG_DCB, "Feature id  = %d\n", featid);
	dcbx_info = qed_dcbnl_get_dcbx(hwfn, QED_DCBX_OPERATIONAL_MIB);
	if (!dcbx_info)
		return 1;

	*flags = 0;
	switch (featid) {
	case DCB_FEATCFG_ATTR_PG:
		if (dcbx_info->operational.params.ets_enabled)
			*flags = DCB_FEATCFG_ENABLE;
		else
			*flags = DCB_FEATCFG_ERROR;
		break;
	case DCB_FEATCFG_ATTR_PFC:
		if (dcbx_info->operational.params.pfc.enabled)
			*flags = DCB_FEATCFG_ENABLE;
		else
			*flags = DCB_FEATCFG_ERROR;
		break;
	case DCB_FEATCFG_ATTR_APP:
		if (dcbx_info->operational.params.app_valid)
			*flags = DCB_FEATCFG_ENABLE;
		else
			*flags = DCB_FEATCFG_ERROR;
		break;
	default:
		DP_INFO(hwfn, "Invalid feature-ID %d\n", featid);
		kfree(dcbx_info);
		return 1;
	}

	DP_VERBOSE(hwfn, QED_MSG_DCB, "flags = %d\n", *flags);
	kfree(dcbx_info);

	return 0;
}

static u8 qed_dcbnl_setfeatcfg(struct qed_dev *cdev, int featid, u8 flags)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_dcbx_set dcbx_set;
	bool enabled, willing;
	struct qed_ptt *ptt;
	int rc;

	DP_VERBOSE(hwfn, QED_MSG_DCB, "featid = %d flags = %d\n",
		   featid, flags);
	memset(&dcbx_set, 0, sizeof(dcbx_set));
	rc = qed_dcbx_get_config_params(hwfn, &dcbx_set);
	if (rc)
		return 1;

	enabled = !!(flags & DCB_FEATCFG_ENABLE);
	willing = !!(flags & DCB_FEATCFG_WILLING);
	switch (featid) {
	case DCB_FEATCFG_ATTR_PG:
		dcbx_set.override_flags |= QED_DCBX_OVERRIDE_ETS_CFG;
		dcbx_set.config.params.ets_enabled = enabled;
		dcbx_set.config.params.ets_willing = willing;
		break;
	case DCB_FEATCFG_ATTR_PFC:
		dcbx_set.override_flags |= QED_DCBX_OVERRIDE_PFC_CFG;
		dcbx_set.config.params.pfc.enabled = enabled;
		dcbx_set.config.params.pfc.willing = willing;
		break;
	case DCB_FEATCFG_ATTR_APP:
		dcbx_set.override_flags |= QED_DCBX_OVERRIDE_APP_CFG;
		dcbx_set.config.params.app_willing = willing;
		break;
	default:
		DP_INFO(hwfn, "Invalid feature-ID %d\n", featid);
		return 1;
	}

	ptt = qed_ptt_acquire(hwfn);
	if (!ptt)
		return 1;

	rc = qed_dcbx_config_params(hwfn, ptt, &dcbx_set, 0);

	qed_ptt_release(hwfn, ptt);

	return 0;
}

static int qed_dcbnl_peer_getappinfo(struct qed_dev *cdev,
				     struct dcb_peer_app_info *info,
				     u16 *app_count)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_dcbx_get *dcbx_info;

	dcbx_info = qed_dcbnl_get_dcbx(hwfn, QED_DCBX_REMOTE_MIB);
	if (!dcbx_info)
		return -EINVAL;

	info->willing = dcbx_info->remote.params.app_willing;
	info->error = dcbx_info->remote.params.app_error;
	*app_count = dcbx_info->remote.params.num_app_entries;
	kfree(dcbx_info);

	return 0;
}

static int qed_dcbnl_peer_getapptable(struct qed_dev *cdev,
				      struct dcb_app *table)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_dcbx_get *dcbx_info;
	int i;

	dcbx_info = qed_dcbnl_get_dcbx(hwfn, QED_DCBX_REMOTE_MIB);
	if (!dcbx_info)
		return -EINVAL;

	for (i = 0; i < dcbx_info->remote.params.num_app_entries; i++) {
		if (dcbx_info->remote.params.app_entry[i].ethtype)
			table[i].selector = DCB_APP_IDTYPE_ETHTYPE;
		else
			table[i].selector = DCB_APP_IDTYPE_PORTNUM;
		table[i].priority = dcbx_info->remote.params.app_entry[i].prio;
		table[i].protocol =
		    dcbx_info->remote.params.app_entry[i].proto_id;
	}

	kfree(dcbx_info);

	return 0;
}

static int qed_dcbnl_cee_peer_getpfc(struct qed_dev *cdev, struct cee_pfc *pfc)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_dcbx_get *dcbx_info;
	int i;

	dcbx_info = qed_dcbnl_get_dcbx(hwfn, QED_DCBX_REMOTE_MIB);
	if (!dcbx_info)
		return -EINVAL;

	for (i = 0; i < QED_MAX_PFC_PRIORITIES; i++)
		if (dcbx_info->remote.params.pfc.prio[i])
			pfc->pfc_en |= BIT(i);

	pfc->tcs_supported = dcbx_info->remote.params.pfc.max_tc;
	DP_VERBOSE(hwfn, QED_MSG_DCB, "pfc state = %d tcs_supported = %d\n",
		   pfc->pfc_en, pfc->tcs_supported);
	kfree(dcbx_info);

	return 0;
}

static int qed_dcbnl_cee_peer_getpg(struct qed_dev *cdev, struct cee_pg *pg)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_dcbx_get *dcbx_info;
	int i;

	dcbx_info = qed_dcbnl_get_dcbx(hwfn, QED_DCBX_REMOTE_MIB);
	if (!dcbx_info)
		return -EINVAL;

	pg->willing = dcbx_info->remote.params.ets_willing;
	for (i = 0; i < QED_MAX_PFC_PRIORITIES; i++) {
		pg->pg_bw[i] = dcbx_info->remote.params.ets_tc_bw_tbl[i];
		pg->prio_pg[i] = dcbx_info->remote.params.ets_pri_tc_tbl[i];
	}

	DP_VERBOSE(hwfn, QED_MSG_DCB, "willing = %d", pg->willing);
	kfree(dcbx_info);

	return 0;
}

static int qed_dcbnl_get_ieee_pfc(struct qed_dev *cdev,
				  struct ieee_pfc *pfc, bool remote)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_dcbx_params *params;
	struct qed_dcbx_get *dcbx_info;
	int rc, i;

	dcbx_info = qed_dcbnl_get_dcbx(hwfn, QED_DCBX_OPERATIONAL_MIB);
	if (!dcbx_info)
		return -EINVAL;

	if (!dcbx_info->operational.ieee) {
		DP_INFO(hwfn, "DCBX is not enabled/operational in IEEE mode\n");
		kfree(dcbx_info);
		return -EINVAL;
	}

	if (remote) {
		memset(dcbx_info, 0, sizeof(*dcbx_info));
		rc = qed_dcbx_query_params(hwfn, dcbx_info,
					   QED_DCBX_REMOTE_MIB);
		if (rc) {
			kfree(dcbx_info);
			return -EINVAL;
		}

		params = &dcbx_info->remote.params;
	} else {
		params = &dcbx_info->operational.params;
	}

	pfc->pfc_cap = params->pfc.max_tc;
	pfc->pfc_en = 0;
	for (i = 0; i < QED_MAX_PFC_PRIORITIES; i++)
		if (params->pfc.prio[i])
			pfc->pfc_en |= BIT(i);

	kfree(dcbx_info);

	return 0;
}

static int qed_dcbnl_ieee_getpfc(struct qed_dev *cdev, struct ieee_pfc *pfc)
{
	return qed_dcbnl_get_ieee_pfc(cdev, pfc, false);
}

static int qed_dcbnl_ieee_setpfc(struct qed_dev *cdev, struct ieee_pfc *pfc)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_dcbx_get *dcbx_info;
	struct qed_dcbx_set dcbx_set;
	struct qed_ptt *ptt;
	int rc, i;

	dcbx_info = qed_dcbnl_get_dcbx(hwfn, QED_DCBX_OPERATIONAL_MIB);
	if (!dcbx_info)
		return -EINVAL;

	if (!dcbx_info->operational.ieee) {
		DP_INFO(hwfn, "DCBX is not enabled/operational in IEEE mode\n");
		kfree(dcbx_info);
		return -EINVAL;
	}

	kfree(dcbx_info);

	memset(&dcbx_set, 0, sizeof(dcbx_set));
	rc = qed_dcbx_get_config_params(hwfn, &dcbx_set);
	if (rc)
		return -EINVAL;

	dcbx_set.override_flags |= QED_DCBX_OVERRIDE_PFC_CFG;
	for (i = 0; i < QED_MAX_PFC_PRIORITIES; i++)
		dcbx_set.config.params.pfc.prio[i] = !!(pfc->pfc_en & BIT(i));

	ptt = qed_ptt_acquire(hwfn);
	if (!ptt)
		return -EINVAL;

	rc = qed_dcbx_config_params(hwfn, ptt, &dcbx_set, 0);

	qed_ptt_release(hwfn, ptt);

	return rc;
}

static int qed_dcbnl_get_ieee_ets(struct qed_dev *cdev,
				  struct ieee_ets *ets, bool remote)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_dcbx_get *dcbx_info;
	struct qed_dcbx_params *params;
	int rc;

	dcbx_info = qed_dcbnl_get_dcbx(hwfn, QED_DCBX_OPERATIONAL_MIB);
	if (!dcbx_info)
		return -EINVAL;

	if (!dcbx_info->operational.ieee) {
		DP_INFO(hwfn, "DCBX is not enabled/operational in IEEE mode\n");
		kfree(dcbx_info);
		return -EINVAL;
	}

	if (remote) {
		memset(dcbx_info, 0, sizeof(*dcbx_info));
		rc = qed_dcbx_query_params(hwfn, dcbx_info,
					   QED_DCBX_REMOTE_MIB);
		if (rc) {
			kfree(dcbx_info);
			return -EINVAL;
		}

		params = &dcbx_info->remote.params;
	} else {
		params = &dcbx_info->operational.params;
	}

	ets->ets_cap = params->max_ets_tc;
	ets->willing = params->ets_willing;
	ets->cbs = params->ets_cbs;
	memcpy(ets->tc_tx_bw, params->ets_tc_bw_tbl, sizeof(ets->tc_tx_bw));
	memcpy(ets->tc_tsa, params->ets_tc_tsa_tbl, sizeof(ets->tc_tsa));
	memcpy(ets->prio_tc, params->ets_pri_tc_tbl, sizeof(ets->prio_tc));
	kfree(dcbx_info);

	return 0;
}

static int qed_dcbnl_ieee_getets(struct qed_dev *cdev, struct ieee_ets *ets)
{
	return qed_dcbnl_get_ieee_ets(cdev, ets, false);
}

static int qed_dcbnl_ieee_setets(struct qed_dev *cdev, struct ieee_ets *ets)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_dcbx_get *dcbx_info;
	struct qed_dcbx_set dcbx_set;
	struct qed_ptt *ptt;
	int rc;

	dcbx_info = qed_dcbnl_get_dcbx(hwfn, QED_DCBX_OPERATIONAL_MIB);
	if (!dcbx_info)
		return -EINVAL;

	if (!dcbx_info->operational.ieee) {
		DP_INFO(hwfn, "DCBX is not enabled/operational in IEEE mode\n");
		kfree(dcbx_info);
		return -EINVAL;
	}

	kfree(dcbx_info);

	memset(&dcbx_set, 0, sizeof(dcbx_set));
	rc = qed_dcbx_get_config_params(hwfn, &dcbx_set);
	if (rc)
		return -EINVAL;

	dcbx_set.override_flags |= QED_DCBX_OVERRIDE_ETS_CFG;
	dcbx_set.config.params.max_ets_tc = ets->ets_cap;
	dcbx_set.config.params.ets_willing = ets->willing;
	dcbx_set.config.params.ets_cbs = ets->cbs;
	memcpy(dcbx_set.config.params.ets_tc_bw_tbl, ets->tc_tx_bw,
	       sizeof(ets->tc_tx_bw));
	memcpy(dcbx_set.config.params.ets_tc_tsa_tbl, ets->tc_tsa,
	       sizeof(ets->tc_tsa));
	memcpy(dcbx_set.config.params.ets_pri_tc_tbl, ets->prio_tc,
	       sizeof(ets->prio_tc));

	ptt = qed_ptt_acquire(hwfn);
	if (!ptt)
		return -EINVAL;

	rc = qed_dcbx_config_params(hwfn, ptt, &dcbx_set, 0);

	qed_ptt_release(hwfn, ptt);

	return rc;
}

static int
qed_dcbnl_ieee_peer_getets(struct qed_dev *cdev, struct ieee_ets *ets)
{
	return qed_dcbnl_get_ieee_ets(cdev, ets, true);
}

static int
qed_dcbnl_ieee_peer_getpfc(struct qed_dev *cdev, struct ieee_pfc *pfc)
{
	return qed_dcbnl_get_ieee_pfc(cdev, pfc, true);
}

static int qed_dcbnl_ieee_getapp(struct qed_dev *cdev, struct dcb_app *app)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_dcbx_get *dcbx_info;
	struct qed_app_entry *entry;
	bool ethtype;
	u8 prio = 0;
	int i;

	dcbx_info = qed_dcbnl_get_dcbx(hwfn, QED_DCBX_OPERATIONAL_MIB);
	if (!dcbx_info)
		return -EINVAL;

	if (!dcbx_info->operational.ieee) {
		DP_INFO(hwfn, "DCBX is not enabled/operational in IEEE mode\n");
		kfree(dcbx_info);
		return -EINVAL;
	}

	/* ieee defines the selector field value for ethertype to be 1 */
	ethtype = !!((app->selector - 1) == DCB_APP_IDTYPE_ETHTYPE);
	for (i = 0; i < QED_DCBX_MAX_APP_PROTOCOL; i++) {
		entry = &dcbx_info->operational.params.app_entry[i];
		if ((entry->ethtype == ethtype) &&
		    (entry->proto_id == app->protocol)) {
			prio = entry->prio;
			break;
		}
	}

	if (i == QED_DCBX_MAX_APP_PROTOCOL) {
		DP_ERR(cdev, "App entry (%d, %d) not found\n", app->selector,
		       app->protocol);
		kfree(dcbx_info);
		return -EINVAL;
	}

	app->priority = ffs(prio) - 1;

	kfree(dcbx_info);

	return 0;
}

static int qed_dcbnl_ieee_setapp(struct qed_dev *cdev, struct dcb_app *app)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_dcbx_get *dcbx_info;
	struct qed_dcbx_set dcbx_set;
	struct qed_app_entry *entry;
	struct qed_ptt *ptt;
	bool ethtype;
	int rc, i;

	if (app->priority < 0 || app->priority >= QED_MAX_PFC_PRIORITIES) {
		DP_INFO(hwfn, "Invalid priority %d\n", app->priority);
		return -EINVAL;
	}

	dcbx_info = qed_dcbnl_get_dcbx(hwfn, QED_DCBX_OPERATIONAL_MIB);
	if (!dcbx_info)
		return -EINVAL;

	if (!dcbx_info->operational.ieee) {
		DP_INFO(hwfn, "DCBX is not enabled/operational in IEEE mode\n");
		kfree(dcbx_info);
		return -EINVAL;
	}

	kfree(dcbx_info);

	memset(&dcbx_set, 0, sizeof(dcbx_set));
	rc = qed_dcbx_get_config_params(hwfn, &dcbx_set);
	if (rc)
		return -EINVAL;

	/* ieee defines the selector field value for ethertype to be 1 */
	ethtype = !!((app->selector - 1) == DCB_APP_IDTYPE_ETHTYPE);
	for (i = 0; i < QED_DCBX_MAX_APP_PROTOCOL; i++) {
		entry = &dcbx_set.config.params.app_entry[i];
		if ((entry->ethtype == ethtype) &&
		    (entry->proto_id == app->protocol))
			break;
		/* First empty slot */
		if (!entry->proto_id) {
			dcbx_set.config.params.num_app_entries++;
			break;
		}
	}

	if (i == QED_DCBX_MAX_APP_PROTOCOL) {
		DP_ERR(cdev, "App table is full\n");
		return -EBUSY;
	}

	dcbx_set.override_flags |= QED_DCBX_OVERRIDE_APP_CFG;
	dcbx_set.config.params.app_entry[i].ethtype = ethtype;
	dcbx_set.config.params.app_entry[i].proto_id = app->protocol;
	dcbx_set.config.params.app_entry[i].prio = BIT(app->priority);

	ptt = qed_ptt_acquire(hwfn);
	if (!ptt)
		return -EBUSY;

	rc = qed_dcbx_config_params(hwfn, ptt, &dcbx_set, 0);

	qed_ptt_release(hwfn, ptt);

	return rc;
}

const struct qed_eth_dcbnl_ops qed_dcbnl_ops_pass = {
	.getstate = qed_dcbnl_getstate,
	.setstate = qed_dcbnl_setstate,
	.getpgtccfgtx = qed_dcbnl_getpgtccfgtx,
	.getpgbwgcfgtx = qed_dcbnl_getpgbwgcfgtx,
	.getpgtccfgrx = qed_dcbnl_getpgtccfgrx,
	.getpgbwgcfgrx = qed_dcbnl_getpgbwgcfgrx,
	.getpfccfg = qed_dcbnl_getpfccfg,
	.setpfccfg = qed_dcbnl_setpfccfg,
	.getcap = qed_dcbnl_getcap,
	.getnumtcs = qed_dcbnl_getnumtcs,
	.getpfcstate = qed_dcbnl_getpfcstate,
	.getdcbx = qed_dcbnl_getdcbx,
	.setpgtccfgtx = qed_dcbnl_setpgtccfgtx,
	.setpgtccfgrx = qed_dcbnl_setpgtccfgrx,
	.setpgbwgcfgtx = qed_dcbnl_setpgbwgcfgtx,
	.setpgbwgcfgrx = qed_dcbnl_setpgbwgcfgrx,
	.setall = qed_dcbnl_setall,
	.setnumtcs = qed_dcbnl_setnumtcs,
	.setpfcstate = qed_dcbnl_setpfcstate,
	.setapp = qed_dcbnl_setapp,
	.setdcbx = qed_dcbnl_setdcbx,
	.setfeatcfg = qed_dcbnl_setfeatcfg,
	.getfeatcfg = qed_dcbnl_getfeatcfg,
	.getapp = qed_dcbnl_getapp,
	.peer_getappinfo = qed_dcbnl_peer_getappinfo,
	.peer_getapptable = qed_dcbnl_peer_getapptable,
	.cee_peer_getpfc = qed_dcbnl_cee_peer_getpfc,
	.cee_peer_getpg = qed_dcbnl_cee_peer_getpg,
	.ieee_getpfc = qed_dcbnl_ieee_getpfc,
	.ieee_setpfc = qed_dcbnl_ieee_setpfc,
	.ieee_getets = qed_dcbnl_ieee_getets,
	.ieee_setets = qed_dcbnl_ieee_setets,
	.ieee_peer_getpfc = qed_dcbnl_ieee_peer_getpfc,
	.ieee_peer_getets = qed_dcbnl_ieee_peer_getets,
	.ieee_getapp = qed_dcbnl_ieee_getapp,
	.ieee_setapp = qed_dcbnl_ieee_setapp,
};

#endif
