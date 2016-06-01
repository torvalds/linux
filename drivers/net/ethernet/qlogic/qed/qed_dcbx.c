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

		enable = (type == DCBX_PROTOCOL_ETH) ? false : dcbx_enabled;
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
