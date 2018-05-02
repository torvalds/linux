/* bnx2x_sriov.c: QLogic Everest network driver.
 *
 * Copyright 2009-2013 Broadcom Corporation
 * Copyright 2014 QLogic Corporation
 * All rights reserved
 *
 * Unless you and QLogic execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2, available
 * at http://www.gnu.org/licenses/old-licenses/gpl-2.0.html (the "GPL").
 *
 * Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other QLogic software provided under a
 * license other than the GPL, without QLogic's express prior written
 * consent.
 *
 * Maintained by: Ariel Elior <ariel.elior@qlogic.com>
 * Written by: Shmulik Ravid
 *	       Ariel Elior <ariel.elior@qlogic.com>
 *
 */
#include "bnx2x.h"
#include "bnx2x_init.h"
#include "bnx2x_cmn.h"
#include "bnx2x_sp.h"
#include <linux/crc32.h>
#include <linux/if_vlan.h>

static int bnx2x_vf_op_prep(struct bnx2x *bp, int vfidx,
			    struct bnx2x_virtf **vf,
			    struct pf_vf_bulletin_content **bulletin,
			    bool test_queue);

/* General service functions */
static void storm_memset_vf_to_pf(struct bnx2x *bp, u16 abs_fid,
					 u16 pf_id)
{
	REG_WR8(bp, BAR_XSTRORM_INTMEM + XSTORM_VF_TO_PF_OFFSET(abs_fid),
		pf_id);
	REG_WR8(bp, BAR_CSTRORM_INTMEM + CSTORM_VF_TO_PF_OFFSET(abs_fid),
		pf_id);
	REG_WR8(bp, BAR_TSTRORM_INTMEM + TSTORM_VF_TO_PF_OFFSET(abs_fid),
		pf_id);
	REG_WR8(bp, BAR_USTRORM_INTMEM + USTORM_VF_TO_PF_OFFSET(abs_fid),
		pf_id);
}

static void storm_memset_func_en(struct bnx2x *bp, u16 abs_fid,
					u8 enable)
{
	REG_WR8(bp, BAR_XSTRORM_INTMEM + XSTORM_FUNC_EN_OFFSET(abs_fid),
		enable);
	REG_WR8(bp, BAR_CSTRORM_INTMEM + CSTORM_FUNC_EN_OFFSET(abs_fid),
		enable);
	REG_WR8(bp, BAR_TSTRORM_INTMEM + TSTORM_FUNC_EN_OFFSET(abs_fid),
		enable);
	REG_WR8(bp, BAR_USTRORM_INTMEM + USTORM_FUNC_EN_OFFSET(abs_fid),
		enable);
}

int bnx2x_vf_idx_by_abs_fid(struct bnx2x *bp, u16 abs_vfid)
{
	int idx;

	for_each_vf(bp, idx)
		if (bnx2x_vf(bp, idx, abs_vfid) == abs_vfid)
			break;
	return idx;
}

static
struct bnx2x_virtf *bnx2x_vf_by_abs_fid(struct bnx2x *bp, u16 abs_vfid)
{
	u16 idx =  (u16)bnx2x_vf_idx_by_abs_fid(bp, abs_vfid);
	return (idx < BNX2X_NR_VIRTFN(bp)) ? BP_VF(bp, idx) : NULL;
}

static void bnx2x_vf_igu_ack_sb(struct bnx2x *bp, struct bnx2x_virtf *vf,
				u8 igu_sb_id, u8 segment, u16 index, u8 op,
				u8 update)
{
	/* acking a VF sb through the PF - use the GRC */
	u32 ctl;
	u32 igu_addr_data = IGU_REG_COMMAND_REG_32LSB_DATA;
	u32 igu_addr_ctl = IGU_REG_COMMAND_REG_CTRL;
	u32 func_encode = vf->abs_vfid;
	u32 addr_encode = IGU_CMD_E2_PROD_UPD_BASE + igu_sb_id;
	struct igu_regular cmd_data = {0};

	cmd_data.sb_id_and_flags =
			((index << IGU_REGULAR_SB_INDEX_SHIFT) |
			 (segment << IGU_REGULAR_SEGMENT_ACCESS_SHIFT) |
			 (update << IGU_REGULAR_BUPDATE_SHIFT) |
			 (op << IGU_REGULAR_ENABLE_INT_SHIFT));

	ctl = addr_encode << IGU_CTRL_REG_ADDRESS_SHIFT		|
	      func_encode << IGU_CTRL_REG_FID_SHIFT		|
	      IGU_CTRL_CMD_TYPE_WR << IGU_CTRL_REG_TYPE_SHIFT;

	DP(NETIF_MSG_HW, "write 0x%08x to IGU(via GRC) addr 0x%x\n",
	   cmd_data.sb_id_and_flags, igu_addr_data);
	REG_WR(bp, igu_addr_data, cmd_data.sb_id_and_flags);
	mmiowb();
	barrier();

	DP(NETIF_MSG_HW, "write 0x%08x to IGU(via GRC) addr 0x%x\n",
	   ctl, igu_addr_ctl);
	REG_WR(bp, igu_addr_ctl, ctl);
	mmiowb();
	barrier();
}

static bool bnx2x_validate_vf_sp_objs(struct bnx2x *bp,
				       struct bnx2x_virtf *vf,
				       bool print_err)
{
	if (!bnx2x_leading_vfq(vf, sp_initialized)) {
		if (print_err)
			BNX2X_ERR("Slowpath objects not yet initialized!\n");
		else
			DP(BNX2X_MSG_IOV, "Slowpath objects not yet initialized!\n");
		return false;
	}
	return true;
}

/* VFOP operations states */
void bnx2x_vfop_qctor_dump_tx(struct bnx2x *bp, struct bnx2x_virtf *vf,
			      struct bnx2x_queue_init_params *init_params,
			      struct bnx2x_queue_setup_params *setup_params,
			      u16 q_idx, u16 sb_idx)
{
	DP(BNX2X_MSG_IOV,
	   "VF[%d] Q_SETUP: txq[%d]-- vfsb=%d, sb-index=%d, hc-rate=%d, flags=0x%lx, traffic-type=%d",
	   vf->abs_vfid,
	   q_idx,
	   sb_idx,
	   init_params->tx.sb_cq_index,
	   init_params->tx.hc_rate,
	   setup_params->flags,
	   setup_params->txq_params.traffic_type);
}

void bnx2x_vfop_qctor_dump_rx(struct bnx2x *bp, struct bnx2x_virtf *vf,
			    struct bnx2x_queue_init_params *init_params,
			    struct bnx2x_queue_setup_params *setup_params,
			    u16 q_idx, u16 sb_idx)
{
	struct bnx2x_rxq_setup_params *rxq_params = &setup_params->rxq_params;

	DP(BNX2X_MSG_IOV, "VF[%d] Q_SETUP: rxq[%d]-- vfsb=%d, sb-index=%d, hc-rate=%d, mtu=%d, buf-size=%d\n"
	   "sge-size=%d, max_sge_pkt=%d, tpa-agg-size=%d, flags=0x%lx, drop-flags=0x%x, cache-log=%d\n",
	   vf->abs_vfid,
	   q_idx,
	   sb_idx,
	   init_params->rx.sb_cq_index,
	   init_params->rx.hc_rate,
	   setup_params->gen_params.mtu,
	   rxq_params->buf_sz,
	   rxq_params->sge_buf_sz,
	   rxq_params->max_sges_pkt,
	   rxq_params->tpa_agg_sz,
	   setup_params->flags,
	   rxq_params->drop_flags,
	   rxq_params->cache_line_log);
}

void bnx2x_vfop_qctor_prep(struct bnx2x *bp,
			   struct bnx2x_virtf *vf,
			   struct bnx2x_vf_queue *q,
			   struct bnx2x_vf_queue_construct_params *p,
			   unsigned long q_type)
{
	struct bnx2x_queue_init_params *init_p = &p->qstate.params.init;
	struct bnx2x_queue_setup_params *setup_p = &p->prep_qsetup;

	/* INIT */

	/* Enable host coalescing in the transition to INIT state */
	if (test_bit(BNX2X_Q_FLG_HC, &init_p->rx.flags))
		__set_bit(BNX2X_Q_FLG_HC_EN, &init_p->rx.flags);

	if (test_bit(BNX2X_Q_FLG_HC, &init_p->tx.flags))
		__set_bit(BNX2X_Q_FLG_HC_EN, &init_p->tx.flags);

	/* FW SB ID */
	init_p->rx.fw_sb_id = vf_igu_sb(vf, q->sb_idx);
	init_p->tx.fw_sb_id = vf_igu_sb(vf, q->sb_idx);

	/* context */
	init_p->cxts[0] = q->cxt;

	/* SETUP */

	/* Setup-op general parameters */
	setup_p->gen_params.spcl_id = vf->sp_cl_id;
	setup_p->gen_params.stat_id = vfq_stat_id(vf, q);
	setup_p->gen_params.fp_hsi = vf->fp_hsi;

	/* Setup-op flags:
	 * collect statistics, zero statistics, local-switching, security,
	 * OV for Flex10, RSS and MCAST for leading
	 */
	if (test_bit(BNX2X_Q_FLG_STATS, &setup_p->flags))
		__set_bit(BNX2X_Q_FLG_ZERO_STATS, &setup_p->flags);

	/* for VFs, enable tx switching, bd coherency, and mac address
	 * anti-spoofing
	 */
	__set_bit(BNX2X_Q_FLG_TX_SWITCH, &setup_p->flags);
	__set_bit(BNX2X_Q_FLG_TX_SEC, &setup_p->flags);
	__set_bit(BNX2X_Q_FLG_ANTI_SPOOF, &setup_p->flags);

	/* Setup-op rx parameters */
	if (test_bit(BNX2X_Q_TYPE_HAS_RX, &q_type)) {
		struct bnx2x_rxq_setup_params *rxq_p = &setup_p->rxq_params;

		rxq_p->cl_qzone_id = vfq_qzone_id(vf, q);
		rxq_p->fw_sb_id = vf_igu_sb(vf, q->sb_idx);
		rxq_p->rss_engine_id = FW_VF_HANDLE(vf->abs_vfid);

		if (test_bit(BNX2X_Q_FLG_TPA, &setup_p->flags))
			rxq_p->max_tpa_queues = BNX2X_VF_MAX_TPA_AGG_QUEUES;
	}

	/* Setup-op tx parameters */
	if (test_bit(BNX2X_Q_TYPE_HAS_TX, &q_type)) {
		setup_p->txq_params.tss_leading_cl_id = vf->leading_rss;
		setup_p->txq_params.fw_sb_id = vf_igu_sb(vf, q->sb_idx);
	}
}

static int bnx2x_vf_queue_create(struct bnx2x *bp,
				 struct bnx2x_virtf *vf, int qid,
				 struct bnx2x_vf_queue_construct_params *qctor)
{
	struct bnx2x_queue_state_params *q_params;
	int rc = 0;

	DP(BNX2X_MSG_IOV, "vf[%d:%d]\n", vf->abs_vfid, qid);

	/* Prepare ramrod information */
	q_params = &qctor->qstate;
	q_params->q_obj = &bnx2x_vfq(vf, qid, sp_obj);
	set_bit(RAMROD_COMP_WAIT, &q_params->ramrod_flags);

	if (bnx2x_get_q_logical_state(bp, q_params->q_obj) ==
	    BNX2X_Q_LOGICAL_STATE_ACTIVE) {
		DP(BNX2X_MSG_IOV, "queue was already up. Aborting gracefully\n");
		goto out;
	}

	/* Run Queue 'construction' ramrods */
	q_params->cmd = BNX2X_Q_CMD_INIT;
	rc = bnx2x_queue_state_change(bp, q_params);
	if (rc)
		goto out;

	memcpy(&q_params->params.setup, &qctor->prep_qsetup,
	       sizeof(struct bnx2x_queue_setup_params));
	q_params->cmd = BNX2X_Q_CMD_SETUP;
	rc = bnx2x_queue_state_change(bp, q_params);
	if (rc)
		goto out;

	/* enable interrupts */
	bnx2x_vf_igu_ack_sb(bp, vf, vf_igu_sb(vf, bnx2x_vfq(vf, qid, sb_idx)),
			    USTORM_ID, 0, IGU_INT_ENABLE, 0);
out:
	return rc;
}

static int bnx2x_vf_queue_destroy(struct bnx2x *bp, struct bnx2x_virtf *vf,
				  int qid)
{
	enum bnx2x_queue_cmd cmds[] = {BNX2X_Q_CMD_HALT,
				       BNX2X_Q_CMD_TERMINATE,
				       BNX2X_Q_CMD_CFC_DEL};
	struct bnx2x_queue_state_params q_params;
	int rc, i;

	DP(BNX2X_MSG_IOV, "vf[%d]\n", vf->abs_vfid);

	/* Prepare ramrod information */
	memset(&q_params, 0, sizeof(struct bnx2x_queue_state_params));
	q_params.q_obj = &bnx2x_vfq(vf, qid, sp_obj);
	set_bit(RAMROD_COMP_WAIT, &q_params.ramrod_flags);

	if (bnx2x_get_q_logical_state(bp, q_params.q_obj) ==
	    BNX2X_Q_LOGICAL_STATE_STOPPED) {
		DP(BNX2X_MSG_IOV, "queue was already stopped. Aborting gracefully\n");
		goto out;
	}

	/* Run Queue 'destruction' ramrods */
	for (i = 0; i < ARRAY_SIZE(cmds); i++) {
		q_params.cmd = cmds[i];
		rc = bnx2x_queue_state_change(bp, &q_params);
		if (rc) {
			BNX2X_ERR("Failed to run Queue command %d\n", cmds[i]);
			return rc;
		}
	}
out:
	/* Clean Context */
	if (bnx2x_vfq(vf, qid, cxt)) {
		bnx2x_vfq(vf, qid, cxt)->ustorm_ag_context.cdu_usage = 0;
		bnx2x_vfq(vf, qid, cxt)->xstorm_ag_context.cdu_reserved = 0;
	}

	return 0;
}

static void
bnx2x_vf_set_igu_info(struct bnx2x *bp, u8 igu_sb_id, u8 abs_vfid)
{
	struct bnx2x_virtf *vf = bnx2x_vf_by_abs_fid(bp, abs_vfid);
	if (vf) {
		/* the first igu entry belonging to VFs of this PF */
		if (!BP_VFDB(bp)->first_vf_igu_entry)
			BP_VFDB(bp)->first_vf_igu_entry = igu_sb_id;

		/* the first igu entry belonging to this VF */
		if (!vf_sb_count(vf))
			vf->igu_base_id = igu_sb_id;

		++vf_sb_count(vf);
		++vf->sb_count;
	}
	BP_VFDB(bp)->vf_sbs_pool++;
}

static inline void bnx2x_vf_vlan_credit(struct bnx2x *bp,
					struct bnx2x_vlan_mac_obj *obj,
					atomic_t *counter)
{
	struct list_head *pos;
	int read_lock;
	int cnt = 0;

	read_lock = bnx2x_vlan_mac_h_read_lock(bp, obj);
	if (read_lock)
		DP(BNX2X_MSG_SP, "Failed to take vlan mac read head; continuing anyway\n");

	list_for_each(pos, &obj->head)
		cnt++;

	if (!read_lock)
		bnx2x_vlan_mac_h_read_unlock(bp, obj);

	atomic_set(counter, cnt);
}

static int bnx2x_vf_vlan_mac_clear(struct bnx2x *bp, struct bnx2x_virtf *vf,
				   int qid, bool drv_only, int type)
{
	struct bnx2x_vlan_mac_ramrod_params ramrod;
	int rc;

	DP(BNX2X_MSG_IOV, "vf[%d] - deleting all %s\n", vf->abs_vfid,
			  (type == BNX2X_VF_FILTER_VLAN_MAC) ? "VLAN-MACs" :
			  (type == BNX2X_VF_FILTER_MAC) ? "MACs" : "VLANs");

	/* Prepare ramrod params */
	memset(&ramrod, 0, sizeof(struct bnx2x_vlan_mac_ramrod_params));
	if (type == BNX2X_VF_FILTER_VLAN_MAC) {
		set_bit(BNX2X_ETH_MAC, &ramrod.user_req.vlan_mac_flags);
		ramrod.vlan_mac_obj = &bnx2x_vfq(vf, qid, vlan_mac_obj);
	} else if (type == BNX2X_VF_FILTER_MAC) {
		set_bit(BNX2X_ETH_MAC, &ramrod.user_req.vlan_mac_flags);
		ramrod.vlan_mac_obj = &bnx2x_vfq(vf, qid, mac_obj);
	} else {
		ramrod.vlan_mac_obj = &bnx2x_vfq(vf, qid, vlan_obj);
	}
	ramrod.user_req.cmd = BNX2X_VLAN_MAC_DEL;

	set_bit(RAMROD_EXEC, &ramrod.ramrod_flags);
	if (drv_only)
		set_bit(RAMROD_DRV_CLR_ONLY, &ramrod.ramrod_flags);
	else
		set_bit(RAMROD_COMP_WAIT, &ramrod.ramrod_flags);

	/* Start deleting */
	rc = ramrod.vlan_mac_obj->delete_all(bp,
					     ramrod.vlan_mac_obj,
					     &ramrod.user_req.vlan_mac_flags,
					     &ramrod.ramrod_flags);
	if (rc) {
		BNX2X_ERR("Failed to delete all %s\n",
			  (type == BNX2X_VF_FILTER_VLAN_MAC) ? "VLAN-MACs" :
			  (type == BNX2X_VF_FILTER_MAC) ? "MACs" : "VLANs");
		return rc;
	}

	return 0;
}

static int bnx2x_vf_mac_vlan_config(struct bnx2x *bp,
				    struct bnx2x_virtf *vf, int qid,
				    struct bnx2x_vf_mac_vlan_filter *filter,
				    bool drv_only)
{
	struct bnx2x_vlan_mac_ramrod_params ramrod;
	int rc;

	DP(BNX2X_MSG_IOV, "vf[%d] - %s a %s filter\n",
	   vf->abs_vfid, filter->add ? "Adding" : "Deleting",
	   (filter->type == BNX2X_VF_FILTER_VLAN_MAC) ? "VLAN-MAC" :
	   (filter->type == BNX2X_VF_FILTER_MAC) ? "MAC" : "VLAN");

	/* Prepare ramrod params */
	memset(&ramrod, 0, sizeof(struct bnx2x_vlan_mac_ramrod_params));
	if (filter->type == BNX2X_VF_FILTER_VLAN_MAC) {
		ramrod.vlan_mac_obj = &bnx2x_vfq(vf, qid, vlan_mac_obj);
		ramrod.user_req.u.vlan.vlan = filter->vid;
		memcpy(&ramrod.user_req.u.mac.mac, filter->mac, ETH_ALEN);
		set_bit(BNX2X_ETH_MAC, &ramrod.user_req.vlan_mac_flags);
	} else if (filter->type == BNX2X_VF_FILTER_VLAN) {
		ramrod.vlan_mac_obj = &bnx2x_vfq(vf, qid, vlan_obj);
		ramrod.user_req.u.vlan.vlan = filter->vid;
	} else {
		set_bit(BNX2X_ETH_MAC, &ramrod.user_req.vlan_mac_flags);
		ramrod.vlan_mac_obj = &bnx2x_vfq(vf, qid, mac_obj);
		memcpy(&ramrod.user_req.u.mac.mac, filter->mac, ETH_ALEN);
	}
	ramrod.user_req.cmd = filter->add ? BNX2X_VLAN_MAC_ADD :
					    BNX2X_VLAN_MAC_DEL;

	set_bit(RAMROD_EXEC, &ramrod.ramrod_flags);
	if (drv_only)
		set_bit(RAMROD_DRV_CLR_ONLY, &ramrod.ramrod_flags);
	else
		set_bit(RAMROD_COMP_WAIT, &ramrod.ramrod_flags);

	/* Add/Remove the filter */
	rc = bnx2x_config_vlan_mac(bp, &ramrod);
	if (rc == -EEXIST)
		return 0;
	if (rc) {
		BNX2X_ERR("Failed to %s %s\n",
			  filter->add ? "add" : "delete",
			  (filter->type == BNX2X_VF_FILTER_VLAN_MAC) ?
				"VLAN-MAC" :
			  (filter->type == BNX2X_VF_FILTER_MAC) ?
				"MAC" : "VLAN");
		return rc;
	}

	filter->applied = true;

	return 0;
}

int bnx2x_vf_mac_vlan_config_list(struct bnx2x *bp, struct bnx2x_virtf *vf,
				  struct bnx2x_vf_mac_vlan_filters *filters,
				  int qid, bool drv_only)
{
	int rc = 0, i;

	DP(BNX2X_MSG_IOV, "vf[%d]\n", vf->abs_vfid);

	if (!bnx2x_validate_vf_sp_objs(bp, vf, true))
		return -EINVAL;

	/* Prepare ramrod params */
	for (i = 0; i < filters->count; i++) {
		rc = bnx2x_vf_mac_vlan_config(bp, vf, qid,
					      &filters->filters[i], drv_only);
		if (rc)
			break;
	}

	/* Rollback if needed */
	if (i != filters->count) {
		BNX2X_ERR("Managed only %d/%d filters - rolling back\n",
			  i, filters->count);
		while (--i >= 0) {
			if (!filters->filters[i].applied)
				continue;
			filters->filters[i].add = !filters->filters[i].add;
			bnx2x_vf_mac_vlan_config(bp, vf, qid,
						 &filters->filters[i],
						 drv_only);
		}
	}

	/* It's our responsibility to free the filters */
	kfree(filters);

	return rc;
}

int bnx2x_vf_queue_setup(struct bnx2x *bp, struct bnx2x_virtf *vf, int qid,
			 struct bnx2x_vf_queue_construct_params *qctor)
{
	int rc;

	DP(BNX2X_MSG_IOV, "vf[%d:%d]\n", vf->abs_vfid, qid);

	rc = bnx2x_vf_queue_create(bp, vf, qid, qctor);
	if (rc)
		goto op_err;

	/* Schedule the configuration of any pending vlan filters */
	bnx2x_schedule_sp_rtnl(bp, BNX2X_SP_RTNL_HYPERVISOR_VLAN,
			       BNX2X_MSG_IOV);
	return 0;
op_err:
	BNX2X_ERR("QSETUP[%d:%d] error: rc %d\n", vf->abs_vfid, qid, rc);
	return rc;
}

static int bnx2x_vf_queue_flr(struct bnx2x *bp, struct bnx2x_virtf *vf,
			       int qid)
{
	int rc;

	DP(BNX2X_MSG_IOV, "vf[%d:%d]\n", vf->abs_vfid, qid);

	/* If needed, clean the filtering data base */
	if ((qid == LEADING_IDX) &&
	    bnx2x_validate_vf_sp_objs(bp, vf, false)) {
		rc = bnx2x_vf_vlan_mac_clear(bp, vf, qid, true,
					     BNX2X_VF_FILTER_VLAN_MAC);
		if (rc)
			goto op_err;
		rc = bnx2x_vf_vlan_mac_clear(bp, vf, qid, true,
					     BNX2X_VF_FILTER_VLAN);
		if (rc)
			goto op_err;
		rc = bnx2x_vf_vlan_mac_clear(bp, vf, qid, true,
					     BNX2X_VF_FILTER_MAC);
		if (rc)
			goto op_err;
	}

	/* Terminate queue */
	if (bnx2x_vfq(vf, qid, sp_obj).state != BNX2X_Q_STATE_RESET) {
		struct bnx2x_queue_state_params qstate;

		memset(&qstate, 0, sizeof(struct bnx2x_queue_state_params));
		qstate.q_obj = &bnx2x_vfq(vf, qid, sp_obj);
		qstate.q_obj->state = BNX2X_Q_STATE_STOPPED;
		qstate.cmd = BNX2X_Q_CMD_TERMINATE;
		set_bit(RAMROD_COMP_WAIT, &qstate.ramrod_flags);
		rc = bnx2x_queue_state_change(bp, &qstate);
		if (rc)
			goto op_err;
	}

	return 0;
op_err:
	BNX2X_ERR("vf[%d:%d] error: rc %d\n", vf->abs_vfid, qid, rc);
	return rc;
}

int bnx2x_vf_mcast(struct bnx2x *bp, struct bnx2x_virtf *vf,
		   bnx2x_mac_addr_t *mcasts, int mc_num, bool drv_only)
{
	struct bnx2x_mcast_list_elem *mc = NULL;
	struct bnx2x_mcast_ramrod_params mcast;
	int rc, i;

	DP(BNX2X_MSG_IOV, "vf[%d]\n", vf->abs_vfid);

	/* Prepare Multicast command */
	memset(&mcast, 0, sizeof(struct bnx2x_mcast_ramrod_params));
	mcast.mcast_obj = &vf->mcast_obj;
	if (drv_only)
		set_bit(RAMROD_DRV_CLR_ONLY, &mcast.ramrod_flags);
	else
		set_bit(RAMROD_COMP_WAIT, &mcast.ramrod_flags);
	if (mc_num) {
		mc = kzalloc(mc_num * sizeof(struct bnx2x_mcast_list_elem),
			     GFP_KERNEL);
		if (!mc) {
			BNX2X_ERR("Cannot Configure multicasts due to lack of memory\n");
			return -ENOMEM;
		}
	}

	if (mc_num) {
		INIT_LIST_HEAD(&mcast.mcast_list);
		for (i = 0; i < mc_num; i++) {
			mc[i].mac = mcasts[i];
			list_add_tail(&mc[i].link,
				      &mcast.mcast_list);
		}

		/* add new mcasts */
		mcast.mcast_list_len = mc_num;
		rc = bnx2x_config_mcast(bp, &mcast, BNX2X_MCAST_CMD_SET);
		if (rc)
			BNX2X_ERR("Failed to set multicasts\n");
	} else {
		/* clear existing mcasts */
		rc = bnx2x_config_mcast(bp, &mcast, BNX2X_MCAST_CMD_DEL);
		if (rc)
			BNX2X_ERR("Failed to remove multicasts\n");
	}

	kfree(mc);

	return rc;
}

static void bnx2x_vf_prep_rx_mode(struct bnx2x *bp, u8 qid,
				  struct bnx2x_rx_mode_ramrod_params *ramrod,
				  struct bnx2x_virtf *vf,
				  unsigned long accept_flags)
{
	struct bnx2x_vf_queue *vfq = vfq_get(vf, qid);

	memset(ramrod, 0, sizeof(*ramrod));
	ramrod->cid = vfq->cid;
	ramrod->cl_id = vfq_cl_id(vf, vfq);
	ramrod->rx_mode_obj = &bp->rx_mode_obj;
	ramrod->func_id = FW_VF_HANDLE(vf->abs_vfid);
	ramrod->rx_accept_flags = accept_flags;
	ramrod->tx_accept_flags = accept_flags;
	ramrod->pstate = &vf->filter_state;
	ramrod->state = BNX2X_FILTER_RX_MODE_PENDING;

	set_bit(BNX2X_FILTER_RX_MODE_PENDING, &vf->filter_state);
	set_bit(RAMROD_RX, &ramrod->ramrod_flags);
	set_bit(RAMROD_TX, &ramrod->ramrod_flags);

	ramrod->rdata = bnx2x_vf_sp(bp, vf, rx_mode_rdata.e2);
	ramrod->rdata_mapping = bnx2x_vf_sp_map(bp, vf, rx_mode_rdata.e2);
}

int bnx2x_vf_rxmode(struct bnx2x *bp, struct bnx2x_virtf *vf,
		    int qid, unsigned long accept_flags)
{
	struct bnx2x_rx_mode_ramrod_params ramrod;

	DP(BNX2X_MSG_IOV, "vf[%d]\n", vf->abs_vfid);

	bnx2x_vf_prep_rx_mode(bp, qid, &ramrod, vf, accept_flags);
	set_bit(RAMROD_COMP_WAIT, &ramrod.ramrod_flags);
	vfq_get(vf, qid)->accept_flags = ramrod.rx_accept_flags;
	return bnx2x_config_rx_mode(bp, &ramrod);
}

int bnx2x_vf_queue_teardown(struct bnx2x *bp, struct bnx2x_virtf *vf, int qid)
{
	int rc;

	DP(BNX2X_MSG_IOV, "vf[%d:%d]\n", vf->abs_vfid, qid);

	/* Remove all classification configuration for leading queue */
	if (qid == LEADING_IDX) {
		rc = bnx2x_vf_rxmode(bp, vf, qid, 0);
		if (rc)
			goto op_err;

		/* Remove filtering if feasible */
		if (bnx2x_validate_vf_sp_objs(bp, vf, true)) {
			rc = bnx2x_vf_vlan_mac_clear(bp, vf, qid,
						     false,
						     BNX2X_VF_FILTER_VLAN_MAC);
			if (rc)
				goto op_err;
			rc = bnx2x_vf_vlan_mac_clear(bp, vf, qid,
						     false,
						     BNX2X_VF_FILTER_VLAN);
			if (rc)
				goto op_err;
			rc = bnx2x_vf_vlan_mac_clear(bp, vf, qid,
						     false,
						     BNX2X_VF_FILTER_MAC);
			if (rc)
				goto op_err;
			rc = bnx2x_vf_mcast(bp, vf, NULL, 0, false);
			if (rc)
				goto op_err;
		}
	}

	/* Destroy queue */
	rc = bnx2x_vf_queue_destroy(bp, vf, qid);
	if (rc)
		goto op_err;
	return rc;
op_err:
	BNX2X_ERR("vf[%d:%d] error: rc %d\n",
		  vf->abs_vfid, qid, rc);
	return rc;
}

/* VF enable primitives
 * when pretend is required the caller is responsible
 * for calling pretend prior to calling these routines
 */

/* internal vf enable - until vf is enabled internally all transactions
 * are blocked. This routine should always be called last with pretend.
 */
static void bnx2x_vf_enable_internal(struct bnx2x *bp, u8 enable)
{
	REG_WR(bp, PGLUE_B_REG_INTERNAL_VFID_ENABLE, enable ? 1 : 0);
}

/* clears vf error in all semi blocks */
static void bnx2x_vf_semi_clear_err(struct bnx2x *bp, u8 abs_vfid)
{
	REG_WR(bp, TSEM_REG_VFPF_ERR_NUM, abs_vfid);
	REG_WR(bp, USEM_REG_VFPF_ERR_NUM, abs_vfid);
	REG_WR(bp, CSEM_REG_VFPF_ERR_NUM, abs_vfid);
	REG_WR(bp, XSEM_REG_VFPF_ERR_NUM, abs_vfid);
}

static void bnx2x_vf_pglue_clear_err(struct bnx2x *bp, u8 abs_vfid)
{
	u32 was_err_group = (2 * BP_PATH(bp) + abs_vfid) >> 5;
	u32 was_err_reg = 0;

	switch (was_err_group) {
	case 0:
	    was_err_reg = PGLUE_B_REG_WAS_ERROR_VF_31_0_CLR;
	    break;
	case 1:
	    was_err_reg = PGLUE_B_REG_WAS_ERROR_VF_63_32_CLR;
	    break;
	case 2:
	    was_err_reg = PGLUE_B_REG_WAS_ERROR_VF_95_64_CLR;
	    break;
	case 3:
	    was_err_reg = PGLUE_B_REG_WAS_ERROR_VF_127_96_CLR;
	    break;
	}
	REG_WR(bp, was_err_reg, 1 << (abs_vfid & 0x1f));
}

static void bnx2x_vf_igu_reset(struct bnx2x *bp, struct bnx2x_virtf *vf)
{
	int i;
	u32 val;

	/* Set VF masks and configuration - pretend */
	bnx2x_pretend_func(bp, HW_VF_HANDLE(bp, vf->abs_vfid));

	REG_WR(bp, IGU_REG_SB_INT_BEFORE_MASK_LSB, 0);
	REG_WR(bp, IGU_REG_SB_INT_BEFORE_MASK_MSB, 0);
	REG_WR(bp, IGU_REG_SB_MASK_LSB, 0);
	REG_WR(bp, IGU_REG_SB_MASK_MSB, 0);
	REG_WR(bp, IGU_REG_PBA_STATUS_LSB, 0);
	REG_WR(bp, IGU_REG_PBA_STATUS_MSB, 0);

	val = REG_RD(bp, IGU_REG_VF_CONFIGURATION);
	val |= (IGU_VF_CONF_FUNC_EN | IGU_VF_CONF_MSI_MSIX_EN);
	val &= ~IGU_VF_CONF_PARENT_MASK;
	val |= (BP_ABS_FUNC(bp) >> 1) << IGU_VF_CONF_PARENT_SHIFT;
	REG_WR(bp, IGU_REG_VF_CONFIGURATION, val);

	DP(BNX2X_MSG_IOV,
	   "value in IGU_REG_VF_CONFIGURATION of vf %d after write is 0x%08x\n",
	   vf->abs_vfid, val);

	bnx2x_pretend_func(bp, BP_ABS_FUNC(bp));

	/* iterate over all queues, clear sb consumer */
	for (i = 0; i < vf_sb_count(vf); i++) {
		u8 igu_sb_id = vf_igu_sb(vf, i);

		/* zero prod memory */
		REG_WR(bp, IGU_REG_PROD_CONS_MEMORY + igu_sb_id * 4, 0);

		/* clear sb state machine */
		bnx2x_igu_clear_sb_gen(bp, vf->abs_vfid, igu_sb_id,
				       false /* VF */);

		/* disable + update */
		bnx2x_vf_igu_ack_sb(bp, vf, igu_sb_id, USTORM_ID, 0,
				    IGU_INT_DISABLE, 1);
	}
}

void bnx2x_vf_enable_access(struct bnx2x *bp, u8 abs_vfid)
{
	/* set the VF-PF association in the FW */
	storm_memset_vf_to_pf(bp, FW_VF_HANDLE(abs_vfid), BP_FUNC(bp));
	storm_memset_func_en(bp, FW_VF_HANDLE(abs_vfid), 1);

	/* clear vf errors*/
	bnx2x_vf_semi_clear_err(bp, abs_vfid);
	bnx2x_vf_pglue_clear_err(bp, abs_vfid);

	/* internal vf-enable - pretend */
	bnx2x_pretend_func(bp, HW_VF_HANDLE(bp, abs_vfid));
	DP(BNX2X_MSG_IOV, "enabling internal access for vf %x\n", abs_vfid);
	bnx2x_vf_enable_internal(bp, true);
	bnx2x_pretend_func(bp, BP_ABS_FUNC(bp));
}

static void bnx2x_vf_enable_traffic(struct bnx2x *bp, struct bnx2x_virtf *vf)
{
	/* Reset vf in IGU  interrupts are still disabled */
	bnx2x_vf_igu_reset(bp, vf);

	/* pretend to enable the vf with the PBF */
	bnx2x_pretend_func(bp, HW_VF_HANDLE(bp, vf->abs_vfid));
	REG_WR(bp, PBF_REG_DISABLE_VF, 0);
	bnx2x_pretend_func(bp, BP_ABS_FUNC(bp));
}

static u8 bnx2x_vf_is_pcie_pending(struct bnx2x *bp, u8 abs_vfid)
{
	struct pci_dev *dev;
	struct bnx2x_virtf *vf = bnx2x_vf_by_abs_fid(bp, abs_vfid);

	if (!vf)
		return false;

	dev = pci_get_domain_bus_and_slot(vf->domain, vf->bus, vf->devfn);
	if (dev)
		return bnx2x_is_pcie_pending(dev);
	return false;
}

int bnx2x_vf_flr_clnup_epilog(struct bnx2x *bp, u8 abs_vfid)
{
	/* Verify no pending pci transactions */
	if (bnx2x_vf_is_pcie_pending(bp, abs_vfid))
		BNX2X_ERR("PCIE Transactions still pending\n");

	return 0;
}

/* must be called after the number of PF queues and the number of VFs are
 * both known
 */
static void
bnx2x_iov_static_resc(struct bnx2x *bp, struct bnx2x_virtf *vf)
{
	struct vf_pf_resc_request *resc = &vf->alloc_resc;

	/* will be set only during VF-ACQUIRE */
	resc->num_rxqs = 0;
	resc->num_txqs = 0;

	resc->num_mac_filters = VF_MAC_CREDIT_CNT;
	resc->num_vlan_filters = VF_VLAN_CREDIT_CNT;

	/* no real limitation */
	resc->num_mc_filters = 0;

	/* num_sbs already set */
	resc->num_sbs = vf->sb_count;
}

/* FLR routines: */
static void bnx2x_vf_free_resc(struct bnx2x *bp, struct bnx2x_virtf *vf)
{
	/* reset the state variables */
	bnx2x_iov_static_resc(bp, vf);
	vf->state = VF_FREE;
}

static void bnx2x_vf_flr_clnup_hw(struct bnx2x *bp, struct bnx2x_virtf *vf)
{
	u32 poll_cnt = bnx2x_flr_clnup_poll_count(bp);

	/* DQ usage counter */
	bnx2x_pretend_func(bp, HW_VF_HANDLE(bp, vf->abs_vfid));
	bnx2x_flr_clnup_poll_hw_counter(bp, DORQ_REG_VF_USAGE_CNT,
					"DQ VF usage counter timed out",
					poll_cnt);
	bnx2x_pretend_func(bp, BP_ABS_FUNC(bp));

	/* FW cleanup command - poll for the results */
	if (bnx2x_send_final_clnup(bp, (u8)FW_VF_HANDLE(vf->abs_vfid),
				   poll_cnt))
		BNX2X_ERR("VF[%d] Final cleanup timed-out\n", vf->abs_vfid);

	/* verify TX hw is flushed */
	bnx2x_tx_hw_flushed(bp, poll_cnt);
}

static void bnx2x_vf_flr(struct bnx2x *bp, struct bnx2x_virtf *vf)
{
	int rc, i;

	DP(BNX2X_MSG_IOV, "vf[%d]\n", vf->abs_vfid);

	/* the cleanup operations are valid if and only if the VF
	 * was first acquired.
	 */
	for (i = 0; i < vf_rxq_count(vf); i++) {
		rc = bnx2x_vf_queue_flr(bp, vf, i);
		if (rc)
			goto out;
	}

	/* remove multicasts */
	bnx2x_vf_mcast(bp, vf, NULL, 0, true);

	/* dispatch final cleanup and wait for HW queues to flush */
	bnx2x_vf_flr_clnup_hw(bp, vf);

	/* release VF resources */
	bnx2x_vf_free_resc(bp, vf);

	vf->malicious = false;

	/* re-open the mailbox */
	bnx2x_vf_enable_mbx(bp, vf->abs_vfid);
	return;
out:
	BNX2X_ERR("vf[%d:%d] failed flr: rc %d\n",
		  vf->abs_vfid, i, rc);
}

static void bnx2x_vf_flr_clnup(struct bnx2x *bp)
{
	struct bnx2x_virtf *vf;
	int i;

	for (i = 0; i < BNX2X_NR_VIRTFN(bp); i++) {
		/* VF should be RESET & in FLR cleanup states */
		if (bnx2x_vf(bp, i, state) != VF_RESET ||
		    !bnx2x_vf(bp, i, flr_clnup_stage))
			continue;

		DP(BNX2X_MSG_IOV, "next vf to cleanup: %d. Num of vfs: %d\n",
		   i, BNX2X_NR_VIRTFN(bp));

		vf = BP_VF(bp, i);

		/* lock the vf pf channel */
		bnx2x_lock_vf_pf_channel(bp, vf, CHANNEL_TLV_FLR);

		/* invoke the VF FLR SM */
		bnx2x_vf_flr(bp, vf);

		/* mark the VF to be ACKED and continue */
		vf->flr_clnup_stage = false;
		bnx2x_unlock_vf_pf_channel(bp, vf, CHANNEL_TLV_FLR);
	}

	/* Acknowledge the handled VFs.
	 * we are acknowledge all the vfs which an flr was requested for, even
	 * if amongst them there are such that we never opened, since the mcp
	 * will interrupt us immediately again if we only ack some of the bits,
	 * resulting in an endless loop. This can happen for example in KVM
	 * where an 'all ones' flr request is sometimes given by hyper visor
	 */
	DP(BNX2X_MSG_MCP, "DRV_STATUS_VF_DISABLED ACK for vfs 0x%x 0x%x\n",
	   bp->vfdb->flrd_vfs[0], bp->vfdb->flrd_vfs[1]);
	for (i = 0; i < FLRD_VFS_DWORDS; i++)
		SHMEM2_WR(bp, drv_ack_vf_disabled[BP_FW_MB_IDX(bp)][i],
			  bp->vfdb->flrd_vfs[i]);

	bnx2x_fw_command(bp, DRV_MSG_CODE_VF_DISABLED_DONE, 0);

	/* clear the acked bits - better yet if the MCP implemented
	 * write to clear semantics
	 */
	for (i = 0; i < FLRD_VFS_DWORDS; i++)
		SHMEM2_WR(bp, drv_ack_vf_disabled[BP_FW_MB_IDX(bp)][i], 0);
}

void bnx2x_vf_handle_flr_event(struct bnx2x *bp)
{
	int i;

	/* Read FLR'd VFs */
	for (i = 0; i < FLRD_VFS_DWORDS; i++)
		bp->vfdb->flrd_vfs[i] = SHMEM2_RD(bp, mcp_vf_disabled[i]);

	DP(BNX2X_MSG_MCP,
	   "DRV_STATUS_VF_DISABLED received for vfs 0x%x 0x%x\n",
	   bp->vfdb->flrd_vfs[0], bp->vfdb->flrd_vfs[1]);

	for_each_vf(bp, i) {
		struct bnx2x_virtf *vf = BP_VF(bp, i);
		u32 reset = 0;

		if (vf->abs_vfid < 32)
			reset = bp->vfdb->flrd_vfs[0] & (1 << vf->abs_vfid);
		else
			reset = bp->vfdb->flrd_vfs[1] &
				(1 << (vf->abs_vfid - 32));

		if (reset) {
			/* set as reset and ready for cleanup */
			vf->state = VF_RESET;
			vf->flr_clnup_stage = true;

			DP(BNX2X_MSG_IOV,
			   "Initiating Final cleanup for VF %d\n",
			   vf->abs_vfid);
		}
	}

	/* do the FLR cleanup for all marked VFs*/
	bnx2x_vf_flr_clnup(bp);
}

/* IOV global initialization routines  */
void bnx2x_iov_init_dq(struct bnx2x *bp)
{
	if (!IS_SRIOV(bp))
		return;

	/* Set the DQ such that the CID reflect the abs_vfid */
	REG_WR(bp, DORQ_REG_VF_NORM_VF_BASE, 0);
	REG_WR(bp, DORQ_REG_MAX_RVFID_SIZE, ilog2(BNX2X_MAX_NUM_OF_VFS));

	/* Set VFs starting CID. If its > 0 the preceding CIDs are belong to
	 * the PF L2 queues
	 */
	REG_WR(bp, DORQ_REG_VF_NORM_CID_BASE, BNX2X_FIRST_VF_CID);

	/* The VF window size is the log2 of the max number of CIDs per VF */
	REG_WR(bp, DORQ_REG_VF_NORM_CID_WND_SIZE, BNX2X_VF_CID_WND);

	/* The VF doorbell size  0 - *B, 4 - 128B. We set it here to match
	 * the Pf doorbell size although the 2 are independent.
	 */
	REG_WR(bp, DORQ_REG_VF_NORM_CID_OFST, 3);

	/* No security checks for now -
	 * configure single rule (out of 16) mask = 0x1, value = 0x0,
	 * CID range 0 - 0x1ffff
	 */
	REG_WR(bp, DORQ_REG_VF_TYPE_MASK_0, 1);
	REG_WR(bp, DORQ_REG_VF_TYPE_VALUE_0, 0);
	REG_WR(bp, DORQ_REG_VF_TYPE_MIN_MCID_0, 0);
	REG_WR(bp, DORQ_REG_VF_TYPE_MAX_MCID_0, 0x1ffff);

	/* set the VF doorbell threshold. This threshold represents the amount
	 * of doorbells allowed in the main DORQ fifo for a specific VF.
	 */
	REG_WR(bp, DORQ_REG_VF_USAGE_CT_LIMIT, 64);
}

void bnx2x_iov_init_dmae(struct bnx2x *bp)
{
	if (pci_find_ext_capability(bp->pdev, PCI_EXT_CAP_ID_SRIOV))
		REG_WR(bp, DMAE_REG_BACKWARD_COMP_EN, 0);
}

static int bnx2x_vf_domain(struct bnx2x *bp, int vfid)
{
	struct pci_dev *dev = bp->pdev;

	return pci_domain_nr(dev->bus);
}

static int bnx2x_vf_bus(struct bnx2x *bp, int vfid)
{
	struct pci_dev *dev = bp->pdev;
	struct bnx2x_sriov *iov = &bp->vfdb->sriov;

	return dev->bus->number + ((dev->devfn + iov->offset +
				    iov->stride * vfid) >> 8);
}

static int bnx2x_vf_devfn(struct bnx2x *bp, int vfid)
{
	struct pci_dev *dev = bp->pdev;
	struct bnx2x_sriov *iov = &bp->vfdb->sriov;

	return (dev->devfn + iov->offset + iov->stride * vfid) & 0xff;
}

static void bnx2x_vf_set_bars(struct bnx2x *bp, struct bnx2x_virtf *vf)
{
	int i, n;
	struct pci_dev *dev = bp->pdev;
	struct bnx2x_sriov *iov = &bp->vfdb->sriov;

	for (i = 0, n = 0; i < PCI_SRIOV_NUM_BARS; i += 2, n++) {
		u64 start = pci_resource_start(dev, PCI_IOV_RESOURCES + i);
		u32 size = pci_resource_len(dev, PCI_IOV_RESOURCES + i);

		size /= iov->total;
		vf->bars[n].bar = start + size * vf->abs_vfid;
		vf->bars[n].size = size;
	}
}

static int
bnx2x_get_vf_igu_cam_info(struct bnx2x *bp)
{
	int sb_id;
	u32 val;
	u8 fid, current_pf = 0;

	/* IGU in normal mode - read CAM */
	for (sb_id = 0; sb_id < IGU_REG_MAPPING_MEMORY_SIZE; sb_id++) {
		val = REG_RD(bp, IGU_REG_MAPPING_MEMORY + sb_id * 4);
		if (!(val & IGU_REG_MAPPING_MEMORY_VALID))
			continue;
		fid = GET_FIELD((val), IGU_REG_MAPPING_MEMORY_FID);
		if (fid & IGU_FID_ENCODE_IS_PF)
			current_pf = fid & IGU_FID_PF_NUM_MASK;
		else if (current_pf == BP_FUNC(bp))
			bnx2x_vf_set_igu_info(bp, sb_id,
					      (fid & IGU_FID_VF_NUM_MASK));
		DP(BNX2X_MSG_IOV, "%s[%d], igu_sb_id=%d, msix=%d\n",
		   ((fid & IGU_FID_ENCODE_IS_PF) ? "PF" : "VF"),
		   ((fid & IGU_FID_ENCODE_IS_PF) ? (fid & IGU_FID_PF_NUM_MASK) :
		   (fid & IGU_FID_VF_NUM_MASK)), sb_id,
		   GET_FIELD((val), IGU_REG_MAPPING_MEMORY_VECTOR));
	}
	DP(BNX2X_MSG_IOV, "vf_sbs_pool is %d\n", BP_VFDB(bp)->vf_sbs_pool);
	return BP_VFDB(bp)->vf_sbs_pool;
}

static void __bnx2x_iov_free_vfdb(struct bnx2x *bp)
{
	if (bp->vfdb) {
		kfree(bp->vfdb->vfqs);
		kfree(bp->vfdb->vfs);
		kfree(bp->vfdb);
	}
	bp->vfdb = NULL;
}

static int bnx2x_sriov_pci_cfg_info(struct bnx2x *bp, struct bnx2x_sriov *iov)
{
	int pos;
	struct pci_dev *dev = bp->pdev;

	pos = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_SRIOV);
	if (!pos) {
		BNX2X_ERR("failed to find SRIOV capability in device\n");
		return -ENODEV;
	}

	iov->pos = pos;
	DP(BNX2X_MSG_IOV, "sriov ext pos %d\n", pos);
	pci_read_config_word(dev, pos + PCI_SRIOV_CTRL, &iov->ctrl);
	pci_read_config_word(dev, pos + PCI_SRIOV_TOTAL_VF, &iov->total);
	pci_read_config_word(dev, pos + PCI_SRIOV_INITIAL_VF, &iov->initial);
	pci_read_config_word(dev, pos + PCI_SRIOV_VF_OFFSET, &iov->offset);
	pci_read_config_word(dev, pos + PCI_SRIOV_VF_STRIDE, &iov->stride);
	pci_read_config_dword(dev, pos + PCI_SRIOV_SUP_PGSIZE, &iov->pgsz);
	pci_read_config_dword(dev, pos + PCI_SRIOV_CAP, &iov->cap);
	pci_read_config_byte(dev, pos + PCI_SRIOV_FUNC_LINK, &iov->link);

	return 0;
}

static int bnx2x_sriov_info(struct bnx2x *bp, struct bnx2x_sriov *iov)
{
	u32 val;

	/* read the SRIOV capability structure
	 * The fields can be read via configuration read or
	 * directly from the device (starting at offset PCICFG_OFFSET)
	 */
	if (bnx2x_sriov_pci_cfg_info(bp, iov))
		return -ENODEV;

	/* get the number of SRIOV bars */
	iov->nres = 0;

	/* read the first_vfid */
	val = REG_RD(bp, PCICFG_OFFSET + GRC_CONFIG_REG_PF_INIT_VF);
	iov->first_vf_in_pf = ((val & GRC_CR_PF_INIT_VF_PF_FIRST_VF_NUM_MASK)
			       * 8) - (BNX2X_MAX_NUM_OF_VFS * BP_PATH(bp));

	DP(BNX2X_MSG_IOV,
	   "IOV info[%d]: first vf %d, nres %d, cap 0x%x, ctrl 0x%x, total %d, initial %d, num vfs %d, offset %d, stride %d, page size 0x%x\n",
	   BP_FUNC(bp),
	   iov->first_vf_in_pf, iov->nres, iov->cap, iov->ctrl, iov->total,
	   iov->initial, iov->nr_virtfn, iov->offset, iov->stride, iov->pgsz);

	return 0;
}

/* must be called after PF bars are mapped */
int bnx2x_iov_init_one(struct bnx2x *bp, int int_mode_param,
		       int num_vfs_param)
{
	int err, i;
	struct bnx2x_sriov *iov;
	struct pci_dev *dev = bp->pdev;

	bp->vfdb = NULL;

	/* verify is pf */
	if (IS_VF(bp))
		return 0;

	/* verify sriov capability is present in configuration space */
	if (!pci_find_ext_capability(dev, PCI_EXT_CAP_ID_SRIOV))
		return 0;

	/* verify chip revision */
	if (CHIP_IS_E1x(bp))
		return 0;

	/* check if SRIOV support is turned off */
	if (!num_vfs_param)
		return 0;

	/* SRIOV assumes that num of PF CIDs < BNX2X_FIRST_VF_CID */
	if (BNX2X_L2_MAX_CID(bp) >= BNX2X_FIRST_VF_CID) {
		BNX2X_ERR("PF cids %d are overspilling into vf space (starts at %d). Abort SRIOV\n",
			  BNX2X_L2_MAX_CID(bp), BNX2X_FIRST_VF_CID);
		return 0;
	}

	/* SRIOV can be enabled only with MSIX */
	if (int_mode_param == BNX2X_INT_MODE_MSI ||
	    int_mode_param == BNX2X_INT_MODE_INTX) {
		BNX2X_ERR("Forced MSI/INTx mode is incompatible with SRIOV\n");
		return 0;
	}

	err = -EIO;
	/* verify ari is enabled */
	if (!pci_ari_enabled(bp->pdev->bus)) {
		BNX2X_ERR("ARI not supported (check pci bridge ARI forwarding), SRIOV can not be enabled\n");
		return 0;
	}

	/* verify igu is in normal mode */
	if (CHIP_INT_MODE_IS_BC(bp)) {
		BNX2X_ERR("IGU not normal mode,  SRIOV can not be enabled\n");
		return 0;
	}

	/* allocate the vfs database */
	bp->vfdb = kzalloc(sizeof(*(bp->vfdb)), GFP_KERNEL);
	if (!bp->vfdb) {
		BNX2X_ERR("failed to allocate vf database\n");
		err = -ENOMEM;
		goto failed;
	}

	/* get the sriov info - Linux already collected all the pertinent
	 * information, however the sriov structure is for the private use
	 * of the pci module. Also we want this information regardless
	 * of the hyper-visor.
	 */
	iov = &(bp->vfdb->sriov);
	err = bnx2x_sriov_info(bp, iov);
	if (err)
		goto failed;

	/* SR-IOV capability was enabled but there are no VFs*/
	if (iov->total == 0)
		goto failed;

	iov->nr_virtfn = min_t(u16, iov->total, num_vfs_param);

	DP(BNX2X_MSG_IOV, "num_vfs_param was %d, nr_virtfn was %d\n",
	   num_vfs_param, iov->nr_virtfn);

	/* allocate the vf array */
	bp->vfdb->vfs = kzalloc(sizeof(struct bnx2x_virtf) *
				BNX2X_NR_VIRTFN(bp), GFP_KERNEL);
	if (!bp->vfdb->vfs) {
		BNX2X_ERR("failed to allocate vf array\n");
		err = -ENOMEM;
		goto failed;
	}

	/* Initial VF init - index and abs_vfid - nr_virtfn must be set */
	for_each_vf(bp, i) {
		bnx2x_vf(bp, i, index) = i;
		bnx2x_vf(bp, i, abs_vfid) = iov->first_vf_in_pf + i;
		bnx2x_vf(bp, i, state) = VF_FREE;
		mutex_init(&bnx2x_vf(bp, i, op_mutex));
		bnx2x_vf(bp, i, op_current) = CHANNEL_TLV_NONE;
	}

	/* re-read the IGU CAM for VFs - index and abs_vfid must be set */
	if (!bnx2x_get_vf_igu_cam_info(bp)) {
		BNX2X_ERR("No entries in IGU CAM for vfs\n");
		err = -EINVAL;
		goto failed;
	}

	/* allocate the queue arrays for all VFs */
	bp->vfdb->vfqs = kzalloc(
		BNX2X_MAX_NUM_VF_QUEUES * sizeof(struct bnx2x_vf_queue),
		GFP_KERNEL);

	if (!bp->vfdb->vfqs) {
		BNX2X_ERR("failed to allocate vf queue array\n");
		err = -ENOMEM;
		goto failed;
	}

	/* Prepare the VFs event synchronization mechanism */
	mutex_init(&bp->vfdb->event_mutex);

	mutex_init(&bp->vfdb->bulletin_mutex);

	if (SHMEM2_HAS(bp, sriov_switch_mode))
		SHMEM2_WR(bp, sriov_switch_mode, SRIOV_SWITCH_MODE_VEB);

	return 0;
failed:
	DP(BNX2X_MSG_IOV, "Failed err=%d\n", err);
	__bnx2x_iov_free_vfdb(bp);
	return err;
}

void bnx2x_iov_remove_one(struct bnx2x *bp)
{
	int vf_idx;

	/* if SRIOV is not enabled there's nothing to do */
	if (!IS_SRIOV(bp))
		return;

	bnx2x_disable_sriov(bp);

	/* disable access to all VFs */
	for (vf_idx = 0; vf_idx < bp->vfdb->sriov.total; vf_idx++) {
		bnx2x_pretend_func(bp,
				   HW_VF_HANDLE(bp,
						bp->vfdb->sriov.first_vf_in_pf +
						vf_idx));
		DP(BNX2X_MSG_IOV, "disabling internal access for vf %d\n",
		   bp->vfdb->sriov.first_vf_in_pf + vf_idx);
		bnx2x_vf_enable_internal(bp, 0);
		bnx2x_pretend_func(bp, BP_ABS_FUNC(bp));
	}

	/* free vf database */
	__bnx2x_iov_free_vfdb(bp);
}

void bnx2x_iov_free_mem(struct bnx2x *bp)
{
	int i;

	if (!IS_SRIOV(bp))
		return;

	/* free vfs hw contexts */
	for (i = 0; i < BNX2X_VF_CIDS/ILT_PAGE_CIDS; i++) {
		struct hw_dma *cxt = &bp->vfdb->context[i];
		BNX2X_PCI_FREE(cxt->addr, cxt->mapping, cxt->size);
	}

	BNX2X_PCI_FREE(BP_VFDB(bp)->sp_dma.addr,
		       BP_VFDB(bp)->sp_dma.mapping,
		       BP_VFDB(bp)->sp_dma.size);

	BNX2X_PCI_FREE(BP_VF_MBX_DMA(bp)->addr,
		       BP_VF_MBX_DMA(bp)->mapping,
		       BP_VF_MBX_DMA(bp)->size);

	BNX2X_PCI_FREE(BP_VF_BULLETIN_DMA(bp)->addr,
		       BP_VF_BULLETIN_DMA(bp)->mapping,
		       BP_VF_BULLETIN_DMA(bp)->size);
}

int bnx2x_iov_alloc_mem(struct bnx2x *bp)
{
	size_t tot_size;
	int i, rc = 0;

	if (!IS_SRIOV(bp))
		return rc;

	/* allocate vfs hw contexts */
	tot_size = (BP_VFDB(bp)->sriov.first_vf_in_pf + BNX2X_NR_VIRTFN(bp)) *
		BNX2X_CIDS_PER_VF * sizeof(union cdu_context);

	for (i = 0; i < BNX2X_VF_CIDS/ILT_PAGE_CIDS; i++) {
		struct hw_dma *cxt = BP_VF_CXT_PAGE(bp, i);
		cxt->size = min_t(size_t, tot_size, CDU_ILT_PAGE_SZ);

		if (cxt->size) {
			cxt->addr = BNX2X_PCI_ALLOC(&cxt->mapping, cxt->size);
			if (!cxt->addr)
				goto alloc_mem_err;
		} else {
			cxt->addr = NULL;
			cxt->mapping = 0;
		}
		tot_size -= cxt->size;
	}

	/* allocate vfs ramrods dma memory - client_init and set_mac */
	tot_size = BNX2X_NR_VIRTFN(bp) * sizeof(struct bnx2x_vf_sp);
	BP_VFDB(bp)->sp_dma.addr = BNX2X_PCI_ALLOC(&BP_VFDB(bp)->sp_dma.mapping,
						   tot_size);
	if (!BP_VFDB(bp)->sp_dma.addr)
		goto alloc_mem_err;
	BP_VFDB(bp)->sp_dma.size = tot_size;

	/* allocate mailboxes */
	tot_size = BNX2X_NR_VIRTFN(bp) * MBX_MSG_ALIGNED_SIZE;
	BP_VF_MBX_DMA(bp)->addr = BNX2X_PCI_ALLOC(&BP_VF_MBX_DMA(bp)->mapping,
						  tot_size);
	if (!BP_VF_MBX_DMA(bp)->addr)
		goto alloc_mem_err;

	BP_VF_MBX_DMA(bp)->size = tot_size;

	/* allocate local bulletin boards */
	tot_size = BNX2X_NR_VIRTFN(bp) * BULLETIN_CONTENT_SIZE;
	BP_VF_BULLETIN_DMA(bp)->addr = BNX2X_PCI_ALLOC(&BP_VF_BULLETIN_DMA(bp)->mapping,
						       tot_size);
	if (!BP_VF_BULLETIN_DMA(bp)->addr)
		goto alloc_mem_err;

	BP_VF_BULLETIN_DMA(bp)->size = tot_size;

	return 0;

alloc_mem_err:
	return -ENOMEM;
}

static void bnx2x_vfq_init(struct bnx2x *bp, struct bnx2x_virtf *vf,
			   struct bnx2x_vf_queue *q)
{
	u8 cl_id = vfq_cl_id(vf, q);
	u8 func_id = FW_VF_HANDLE(vf->abs_vfid);
	unsigned long q_type = 0;

	set_bit(BNX2X_Q_TYPE_HAS_TX, &q_type);
	set_bit(BNX2X_Q_TYPE_HAS_RX, &q_type);

	/* Queue State object */
	bnx2x_init_queue_obj(bp, &q->sp_obj,
			     cl_id, &q->cid, 1, func_id,
			     bnx2x_vf_sp(bp, vf, q_data),
			     bnx2x_vf_sp_map(bp, vf, q_data),
			     q_type);

	/* sp indication is set only when vlan/mac/etc. are initialized */
	q->sp_initialized = false;

	DP(BNX2X_MSG_IOV,
	   "initialized vf %d's queue object. func id set to %d. cid set to 0x%x\n",
	   vf->abs_vfid, q->sp_obj.func_id, q->cid);
}

static int bnx2x_max_speed_cap(struct bnx2x *bp)
{
	u32 supported = bp->port.supported[bnx2x_get_link_cfg_idx(bp)];

	if (supported &
	    (SUPPORTED_20000baseMLD2_Full | SUPPORTED_20000baseKR2_Full))
		return 20000;

	return 10000; /* assume lowest supported speed is 10G */
}

int bnx2x_iov_link_update_vf(struct bnx2x *bp, int idx)
{
	struct bnx2x_link_report_data *state = &bp->last_reported_link;
	struct pf_vf_bulletin_content *bulletin;
	struct bnx2x_virtf *vf;
	bool update = true;
	int rc = 0;

	/* sanity and init */
	rc = bnx2x_vf_op_prep(bp, idx, &vf, &bulletin, false);
	if (rc)
		return rc;

	mutex_lock(&bp->vfdb->bulletin_mutex);

	if (vf->link_cfg == IFLA_VF_LINK_STATE_AUTO) {
		bulletin->valid_bitmap |= 1 << LINK_VALID;

		bulletin->link_speed = state->line_speed;
		bulletin->link_flags = 0;
		if (test_bit(BNX2X_LINK_REPORT_LINK_DOWN,
			     &state->link_report_flags))
			bulletin->link_flags |= VFPF_LINK_REPORT_LINK_DOWN;
		if (test_bit(BNX2X_LINK_REPORT_FD,
			     &state->link_report_flags))
			bulletin->link_flags |= VFPF_LINK_REPORT_FULL_DUPLEX;
		if (test_bit(BNX2X_LINK_REPORT_RX_FC_ON,
			     &state->link_report_flags))
			bulletin->link_flags |= VFPF_LINK_REPORT_RX_FC_ON;
		if (test_bit(BNX2X_LINK_REPORT_TX_FC_ON,
			     &state->link_report_flags))
			bulletin->link_flags |= VFPF_LINK_REPORT_TX_FC_ON;
	} else if (vf->link_cfg == IFLA_VF_LINK_STATE_DISABLE &&
		   !(bulletin->link_flags & VFPF_LINK_REPORT_LINK_DOWN)) {
		bulletin->valid_bitmap |= 1 << LINK_VALID;
		bulletin->link_flags |= VFPF_LINK_REPORT_LINK_DOWN;
	} else if (vf->link_cfg == IFLA_VF_LINK_STATE_ENABLE &&
		   (bulletin->link_flags & VFPF_LINK_REPORT_LINK_DOWN)) {
		bulletin->valid_bitmap |= 1 << LINK_VALID;
		bulletin->link_speed = bnx2x_max_speed_cap(bp);
		bulletin->link_flags &= ~VFPF_LINK_REPORT_LINK_DOWN;
	} else {
		update = false;
	}

	if (update) {
		DP(NETIF_MSG_LINK | BNX2X_MSG_IOV,
		   "vf %d mode %u speed %d flags %x\n", idx,
		   vf->link_cfg, bulletin->link_speed, bulletin->link_flags);

		/* Post update on VF's bulletin board */
		rc = bnx2x_post_vf_bulletin(bp, idx);
		if (rc) {
			BNX2X_ERR("failed to update VF[%d] bulletin\n", idx);
			goto out;
		}
	}

out:
	mutex_unlock(&bp->vfdb->bulletin_mutex);
	return rc;
}

int bnx2x_set_vf_link_state(struct net_device *dev, int idx, int link_state)
{
	struct bnx2x *bp = netdev_priv(dev);
	struct bnx2x_virtf *vf = BP_VF(bp, idx);

	if (!vf)
		return -EINVAL;

	if (vf->link_cfg == link_state)
		return 0; /* nothing todo */

	vf->link_cfg = link_state;

	return bnx2x_iov_link_update_vf(bp, idx);
}

void bnx2x_iov_link_update(struct bnx2x *bp)
{
	int vfid;

	if (!IS_SRIOV(bp))
		return;

	for_each_vf(bp, vfid)
		bnx2x_iov_link_update_vf(bp, vfid);
}

/* called by bnx2x_nic_load */
int bnx2x_iov_nic_init(struct bnx2x *bp)
{
	int vfid;

	if (!IS_SRIOV(bp)) {
		DP(BNX2X_MSG_IOV, "vfdb was not allocated\n");
		return 0;
	}

	DP(BNX2X_MSG_IOV, "num of vfs: %d\n", (bp)->vfdb->sriov.nr_virtfn);

	/* let FLR complete ... */
	msleep(100);

	/* initialize vf database */
	for_each_vf(bp, vfid) {
		struct bnx2x_virtf *vf = BP_VF(bp, vfid);

		int base_vf_cid = (BP_VFDB(bp)->sriov.first_vf_in_pf + vfid) *
			BNX2X_CIDS_PER_VF;

		union cdu_context *base_cxt = (union cdu_context *)
			BP_VF_CXT_PAGE(bp, base_vf_cid/ILT_PAGE_CIDS)->addr +
			(base_vf_cid & (ILT_PAGE_CIDS-1));

		DP(BNX2X_MSG_IOV,
		   "VF[%d] Max IGU SBs: %d, base vf cid 0x%x, base cid 0x%x, base cxt %p\n",
		   vf->abs_vfid, vf_sb_count(vf), base_vf_cid,
		   BNX2X_FIRST_VF_CID + base_vf_cid, base_cxt);

		/* init statically provisioned resources */
		bnx2x_iov_static_resc(bp, vf);

		/* queues are initialized during VF-ACQUIRE */
		vf->filter_state = 0;
		vf->sp_cl_id = bnx2x_fp(bp, 0, cl_id);

		bnx2x_init_credit_pool(&vf->vf_vlans_pool, 0,
				       vf_vlan_rules_cnt(vf));
		bnx2x_init_credit_pool(&vf->vf_macs_pool, 0,
				       vf_mac_rules_cnt(vf));

		/*  init mcast object - This object will be re-initialized
		 *  during VF-ACQUIRE with the proper cl_id and cid.
		 *  It needs to be initialized here so that it can be safely
		 *  handled by a subsequent FLR flow.
		 */
		bnx2x_init_mcast_obj(bp, &vf->mcast_obj, 0xFF,
				     0xFF, 0xFF, 0xFF,
				     bnx2x_vf_sp(bp, vf, mcast_rdata),
				     bnx2x_vf_sp_map(bp, vf, mcast_rdata),
				     BNX2X_FILTER_MCAST_PENDING,
				     &vf->filter_state,
				     BNX2X_OBJ_TYPE_RX_TX);

		/* set the mailbox message addresses */
		BP_VF_MBX(bp, vfid)->msg = (struct bnx2x_vf_mbx_msg *)
			(((u8 *)BP_VF_MBX_DMA(bp)->addr) + vfid *
			MBX_MSG_ALIGNED_SIZE);

		BP_VF_MBX(bp, vfid)->msg_mapping = BP_VF_MBX_DMA(bp)->mapping +
			vfid * MBX_MSG_ALIGNED_SIZE;

		/* Enable vf mailbox */
		bnx2x_vf_enable_mbx(bp, vf->abs_vfid);
	}

	/* Final VF init */
	for_each_vf(bp, vfid) {
		struct bnx2x_virtf *vf = BP_VF(bp, vfid);

		/* fill in the BDF and bars */
		vf->domain = bnx2x_vf_domain(bp, vfid);
		vf->bus = bnx2x_vf_bus(bp, vfid);
		vf->devfn = bnx2x_vf_devfn(bp, vfid);
		bnx2x_vf_set_bars(bp, vf);

		DP(BNX2X_MSG_IOV,
		   "VF info[%d]: bus 0x%x, devfn 0x%x, bar0 [0x%x, %d], bar1 [0x%x, %d], bar2 [0x%x, %d]\n",
		   vf->abs_vfid, vf->bus, vf->devfn,
		   (unsigned)vf->bars[0].bar, vf->bars[0].size,
		   (unsigned)vf->bars[1].bar, vf->bars[1].size,
		   (unsigned)vf->bars[2].bar, vf->bars[2].size);
	}

	return 0;
}

/* called by bnx2x_chip_cleanup */
int bnx2x_iov_chip_cleanup(struct bnx2x *bp)
{
	int i;

	if (!IS_SRIOV(bp))
		return 0;

	/* release all the VFs */
	for_each_vf(bp, i)
		bnx2x_vf_release(bp, BP_VF(bp, i));

	return 0;
}

/* called by bnx2x_init_hw_func, returns the next ilt line */
int bnx2x_iov_init_ilt(struct bnx2x *bp, u16 line)
{
	int i;
	struct bnx2x_ilt *ilt = BP_ILT(bp);

	if (!IS_SRIOV(bp))
		return line;

	/* set vfs ilt lines */
	for (i = 0; i < BNX2X_VF_CIDS/ILT_PAGE_CIDS; i++) {
		struct hw_dma *hw_cxt = BP_VF_CXT_PAGE(bp, i);

		ilt->lines[line+i].page = hw_cxt->addr;
		ilt->lines[line+i].page_mapping = hw_cxt->mapping;
		ilt->lines[line+i].size = hw_cxt->size; /* doesn't matter */
	}
	return line + i;
}

static u8 bnx2x_iov_is_vf_cid(struct bnx2x *bp, u16 cid)
{
	return ((cid >= BNX2X_FIRST_VF_CID) &&
		((cid - BNX2X_FIRST_VF_CID) < BNX2X_VF_CIDS));
}

static
void bnx2x_vf_handle_classification_eqe(struct bnx2x *bp,
					struct bnx2x_vf_queue *vfq,
					union event_ring_elem *elem)
{
	unsigned long ramrod_flags = 0;
	int rc = 0;
	u32 echo = le32_to_cpu(elem->message.data.eth_event.echo);

	/* Always push next commands out, don't wait here */
	set_bit(RAMROD_CONT, &ramrod_flags);

	switch (echo >> BNX2X_SWCID_SHIFT) {
	case BNX2X_FILTER_MAC_PENDING:
		rc = vfq->mac_obj.complete(bp, &vfq->mac_obj, elem,
					   &ramrod_flags);
		break;
	case BNX2X_FILTER_VLAN_PENDING:
		rc = vfq->vlan_obj.complete(bp, &vfq->vlan_obj, elem,
					    &ramrod_flags);
		break;
	default:
		BNX2X_ERR("Unsupported classification command: 0x%x\n", echo);
		return;
	}
	if (rc < 0)
		BNX2X_ERR("Failed to schedule new commands: %d\n", rc);
	else if (rc > 0)
		DP(BNX2X_MSG_IOV, "Scheduled next pending commands...\n");
}

static
void bnx2x_vf_handle_mcast_eqe(struct bnx2x *bp,
			       struct bnx2x_virtf *vf)
{
	struct bnx2x_mcast_ramrod_params rparam = {NULL};
	int rc;

	rparam.mcast_obj = &vf->mcast_obj;
	vf->mcast_obj.raw.clear_pending(&vf->mcast_obj.raw);

	/* If there are pending mcast commands - send them */
	if (vf->mcast_obj.check_pending(&vf->mcast_obj)) {
		rc = bnx2x_config_mcast(bp, &rparam, BNX2X_MCAST_CMD_CONT);
		if (rc < 0)
			BNX2X_ERR("Failed to send pending mcast commands: %d\n",
				  rc);
	}
}

static
void bnx2x_vf_handle_filters_eqe(struct bnx2x *bp,
				 struct bnx2x_virtf *vf)
{
	smp_mb__before_atomic();
	clear_bit(BNX2X_FILTER_RX_MODE_PENDING, &vf->filter_state);
	smp_mb__after_atomic();
}

static void bnx2x_vf_handle_rss_update_eqe(struct bnx2x *bp,
					   struct bnx2x_virtf *vf)
{
	vf->rss_conf_obj.raw.clear_pending(&vf->rss_conf_obj.raw);
}

int bnx2x_iov_eq_sp_event(struct bnx2x *bp, union event_ring_elem *elem)
{
	struct bnx2x_virtf *vf;
	int qidx = 0, abs_vfid;
	u8 opcode;
	u16 cid = 0xffff;

	if (!IS_SRIOV(bp))
		return 1;

	/* first get the cid - the only events we handle here are cfc-delete
	 * and set-mac completion
	 */
	opcode = elem->message.opcode;

	switch (opcode) {
	case EVENT_RING_OPCODE_CFC_DEL:
		cid = SW_CID(elem->message.data.cfc_del_event.cid);
		DP(BNX2X_MSG_IOV, "checking cfc-del comp cid=%d\n", cid);
		break;
	case EVENT_RING_OPCODE_CLASSIFICATION_RULES:
	case EVENT_RING_OPCODE_MULTICAST_RULES:
	case EVENT_RING_OPCODE_FILTERS_RULES:
	case EVENT_RING_OPCODE_RSS_UPDATE_RULES:
		cid = SW_CID(elem->message.data.eth_event.echo);
		DP(BNX2X_MSG_IOV, "checking filtering comp cid=%d\n", cid);
		break;
	case EVENT_RING_OPCODE_VF_FLR:
		abs_vfid = elem->message.data.vf_flr_event.vf_id;
		DP(BNX2X_MSG_IOV, "Got VF FLR notification abs_vfid=%d\n",
		   abs_vfid);
		goto get_vf;
	case EVENT_RING_OPCODE_MALICIOUS_VF:
		abs_vfid = elem->message.data.malicious_vf_event.vf_id;
		BNX2X_ERR("Got VF MALICIOUS notification abs_vfid=%d err_id=0x%x\n",
			  abs_vfid,
			  elem->message.data.malicious_vf_event.err_id);
		goto get_vf;
	default:
		return 1;
	}

	/* check if the cid is the VF range */
	if (!bnx2x_iov_is_vf_cid(bp, cid)) {
		DP(BNX2X_MSG_IOV, "cid is outside vf range: %d\n", cid);
		return 1;
	}

	/* extract vf and rxq index from vf_cid - relies on the following:
	 * 1. vfid on cid reflects the true abs_vfid
	 * 2. The max number of VFs (per path) is 64
	 */
	qidx = cid & ((1 << BNX2X_VF_CID_WND)-1);
	abs_vfid = (cid >> BNX2X_VF_CID_WND) & (BNX2X_MAX_NUM_OF_VFS-1);
get_vf:
	vf = bnx2x_vf_by_abs_fid(bp, abs_vfid);

	if (!vf) {
		BNX2X_ERR("EQ completion for unknown VF, cid %d, abs_vfid %d\n",
			  cid, abs_vfid);
		return 0;
	}

	switch (opcode) {
	case EVENT_RING_OPCODE_CFC_DEL:
		DP(BNX2X_MSG_IOV, "got VF [%d:%d] cfc delete ramrod\n",
		   vf->abs_vfid, qidx);
		vfq_get(vf, qidx)->sp_obj.complete_cmd(bp,
						       &vfq_get(vf,
								qidx)->sp_obj,
						       BNX2X_Q_CMD_CFC_DEL);
		break;
	case EVENT_RING_OPCODE_CLASSIFICATION_RULES:
		DP(BNX2X_MSG_IOV, "got VF [%d:%d] set mac/vlan ramrod\n",
		   vf->abs_vfid, qidx);
		bnx2x_vf_handle_classification_eqe(bp, vfq_get(vf, qidx), elem);
		break;
	case EVENT_RING_OPCODE_MULTICAST_RULES:
		DP(BNX2X_MSG_IOV, "got VF [%d:%d] set mcast ramrod\n",
		   vf->abs_vfid, qidx);
		bnx2x_vf_handle_mcast_eqe(bp, vf);
		break;
	case EVENT_RING_OPCODE_FILTERS_RULES:
		DP(BNX2X_MSG_IOV, "got VF [%d:%d] set rx-mode ramrod\n",
		   vf->abs_vfid, qidx);
		bnx2x_vf_handle_filters_eqe(bp, vf);
		break;
	case EVENT_RING_OPCODE_RSS_UPDATE_RULES:
		DP(BNX2X_MSG_IOV, "got VF [%d:%d] RSS update ramrod\n",
		   vf->abs_vfid, qidx);
		bnx2x_vf_handle_rss_update_eqe(bp, vf);
	case EVENT_RING_OPCODE_VF_FLR:
		/* Do nothing for now */
		return 0;
	case EVENT_RING_OPCODE_MALICIOUS_VF:
		vf->malicious = true;
		return 0;
	}

	return 0;
}

static struct bnx2x_virtf *bnx2x_vf_by_cid(struct bnx2x *bp, int vf_cid)
{
	/* extract the vf from vf_cid - relies on the following:
	 * 1. vfid on cid reflects the true abs_vfid
	 * 2. The max number of VFs (per path) is 64
	 */
	int abs_vfid = (vf_cid >> BNX2X_VF_CID_WND) & (BNX2X_MAX_NUM_OF_VFS-1);
	return bnx2x_vf_by_abs_fid(bp, abs_vfid);
}

void bnx2x_iov_set_queue_sp_obj(struct bnx2x *bp, int vf_cid,
				struct bnx2x_queue_sp_obj **q_obj)
{
	struct bnx2x_virtf *vf;

	if (!IS_SRIOV(bp))
		return;

	vf = bnx2x_vf_by_cid(bp, vf_cid);

	if (vf) {
		/* extract queue index from vf_cid - relies on the following:
		 * 1. vfid on cid reflects the true abs_vfid
		 * 2. The max number of VFs (per path) is 64
		 */
		int q_index = vf_cid & ((1 << BNX2X_VF_CID_WND)-1);
		*q_obj = &bnx2x_vfq(vf, q_index, sp_obj);
	} else {
		BNX2X_ERR("No vf matching cid %d\n", vf_cid);
	}
}

void bnx2x_iov_adjust_stats_req(struct bnx2x *bp)
{
	int i;
	int first_queue_query_index, num_queues_req;
	dma_addr_t cur_data_offset;
	struct stats_query_entry *cur_query_entry;
	u8 stats_count = 0;
	bool is_fcoe = false;

	if (!IS_SRIOV(bp))
		return;

	if (!NO_FCOE(bp))
		is_fcoe = true;

	/* fcoe adds one global request and one queue request */
	num_queues_req = BNX2X_NUM_ETH_QUEUES(bp) + is_fcoe;
	first_queue_query_index = BNX2X_FIRST_QUEUE_QUERY_IDX -
		(is_fcoe ? 0 : 1);

	DP_AND((BNX2X_MSG_IOV | BNX2X_MSG_STATS),
	       "BNX2X_NUM_ETH_QUEUES %d, is_fcoe %d, first_queue_query_index %d => determined the last non virtual statistics query index is %d. Will add queries on top of that\n",
	       BNX2X_NUM_ETH_QUEUES(bp), is_fcoe, first_queue_query_index,
	       first_queue_query_index + num_queues_req);

	cur_data_offset = bp->fw_stats_data_mapping +
		offsetof(struct bnx2x_fw_stats_data, queue_stats) +
		num_queues_req * sizeof(struct per_queue_stats);

	cur_query_entry = &bp->fw_stats_req->
		query[first_queue_query_index + num_queues_req];

	for_each_vf(bp, i) {
		int j;
		struct bnx2x_virtf *vf = BP_VF(bp, i);

		if (vf->state != VF_ENABLED) {
			DP_AND((BNX2X_MSG_IOV | BNX2X_MSG_STATS),
			       "vf %d not enabled so no stats for it\n",
			       vf->abs_vfid);
			continue;
		}

		if (vf->malicious) {
			DP_AND((BNX2X_MSG_IOV | BNX2X_MSG_STATS),
			       "vf %d malicious so no stats for it\n",
			       vf->abs_vfid);
			continue;
		}

		DP_AND((BNX2X_MSG_IOV | BNX2X_MSG_STATS),
		       "add addresses for vf %d\n", vf->abs_vfid);
		for_each_vfq(vf, j) {
			struct bnx2x_vf_queue *rxq = vfq_get(vf, j);

			dma_addr_t q_stats_addr =
				vf->fw_stat_map + j * vf->stats_stride;

			/* collect stats fro active queues only */
			if (bnx2x_get_q_logical_state(bp, &rxq->sp_obj) ==
			    BNX2X_Q_LOGICAL_STATE_STOPPED)
				continue;

			/* create stats query entry for this queue */
			cur_query_entry->kind = STATS_TYPE_QUEUE;
			cur_query_entry->index = vfq_stat_id(vf, rxq);
			cur_query_entry->funcID =
				cpu_to_le16(FW_VF_HANDLE(vf->abs_vfid));
			cur_query_entry->address.hi =
				cpu_to_le32(U64_HI(q_stats_addr));
			cur_query_entry->address.lo =
				cpu_to_le32(U64_LO(q_stats_addr));
			DP_AND((BNX2X_MSG_IOV | BNX2X_MSG_STATS),
			       "added address %x %x for vf %d queue %d client %d\n",
			       cur_query_entry->address.hi,
			       cur_query_entry->address.lo,
			       cur_query_entry->funcID,
			       j, cur_query_entry->index);
			cur_query_entry++;
			cur_data_offset += sizeof(struct per_queue_stats);
			stats_count++;

			/* all stats are coalesced to the leading queue */
			if (vf->cfg_flags & VF_CFG_STATS_COALESCE)
				break;
		}
	}
	bp->fw_stats_req->hdr.cmd_num = bp->fw_stats_num + stats_count;
}

/* VF API helpers */
static void bnx2x_vf_qtbl_set_q(struct bnx2x *bp, u8 abs_vfid, u8 qid,
				u8 enable)
{
	u32 reg = PXP_REG_HST_ZONE_PERMISSION_TABLE + qid * 4;
	u32 val = enable ? (abs_vfid | (1 << 6)) : 0;

	REG_WR(bp, reg, val);
}

static void bnx2x_vf_clr_qtbl(struct bnx2x *bp, struct bnx2x_virtf *vf)
{
	int i;

	for_each_vfq(vf, i)
		bnx2x_vf_qtbl_set_q(bp, vf->abs_vfid,
				    vfq_qzone_id(vf, vfq_get(vf, i)), false);
}

static void bnx2x_vf_igu_disable(struct bnx2x *bp, struct bnx2x_virtf *vf)
{
	u32 val;

	/* clear the VF configuration - pretend */
	bnx2x_pretend_func(bp, HW_VF_HANDLE(bp, vf->abs_vfid));
	val = REG_RD(bp, IGU_REG_VF_CONFIGURATION);
	val &= ~(IGU_VF_CONF_MSI_MSIX_EN | IGU_VF_CONF_SINGLE_ISR_EN |
		 IGU_VF_CONF_FUNC_EN | IGU_VF_CONF_PARENT_MASK);
	REG_WR(bp, IGU_REG_VF_CONFIGURATION, val);
	bnx2x_pretend_func(bp, BP_ABS_FUNC(bp));
}

u8 bnx2x_vf_max_queue_cnt(struct bnx2x *bp, struct bnx2x_virtf *vf)
{
	return min_t(u8, min_t(u8, vf_sb_count(vf), BNX2X_CIDS_PER_VF),
		     BNX2X_VF_MAX_QUEUES);
}

static
int bnx2x_vf_chk_avail_resc(struct bnx2x *bp, struct bnx2x_virtf *vf,
			    struct vf_pf_resc_request *req_resc)
{
	u8 rxq_cnt = vf_rxq_count(vf) ? : bnx2x_vf_max_queue_cnt(bp, vf);
	u8 txq_cnt = vf_txq_count(vf) ? : bnx2x_vf_max_queue_cnt(bp, vf);

	return ((req_resc->num_rxqs <= rxq_cnt) &&
		(req_resc->num_txqs <= txq_cnt) &&
		(req_resc->num_sbs <= vf_sb_count(vf))   &&
		(req_resc->num_mac_filters <= vf_mac_rules_cnt(vf)) &&
		(req_resc->num_vlan_filters <= vf_vlan_rules_cnt(vf)));
}

/* CORE VF API */
int bnx2x_vf_acquire(struct bnx2x *bp, struct bnx2x_virtf *vf,
		     struct vf_pf_resc_request *resc)
{
	int base_vf_cid = (BP_VFDB(bp)->sriov.first_vf_in_pf + vf->index) *
		BNX2X_CIDS_PER_VF;

	union cdu_context *base_cxt = (union cdu_context *)
		BP_VF_CXT_PAGE(bp, base_vf_cid/ILT_PAGE_CIDS)->addr +
		(base_vf_cid & (ILT_PAGE_CIDS-1));
	int i;

	/* if state is 'acquired' the VF was not released or FLR'd, in
	 * this case the returned resources match the acquired already
	 * acquired resources. Verify that the requested numbers do
	 * not exceed the already acquired numbers.
	 */
	if (vf->state == VF_ACQUIRED) {
		DP(BNX2X_MSG_IOV, "VF[%d] Trying to re-acquire resources (VF was not released or FLR'd)\n",
		   vf->abs_vfid);

		if (!bnx2x_vf_chk_avail_resc(bp, vf, resc)) {
			BNX2X_ERR("VF[%d] When re-acquiring resources, requested numbers must be <= then previously acquired numbers\n",
				  vf->abs_vfid);
			return -EINVAL;
		}
		return 0;
	}

	/* Otherwise vf state must be 'free' or 'reset' */
	if (vf->state != VF_FREE && vf->state != VF_RESET) {
		BNX2X_ERR("VF[%d] Can not acquire a VF with state %d\n",
			  vf->abs_vfid, vf->state);
		return -EINVAL;
	}

	/* static allocation:
	 * the global maximum number are fixed per VF. Fail the request if
	 * requested number exceed these globals
	 */
	if (!bnx2x_vf_chk_avail_resc(bp, vf, resc)) {
		DP(BNX2X_MSG_IOV,
		   "cannot fulfill vf resource request. Placing maximal available values in response\n");
		/* set the max resource in the vf */
		return -ENOMEM;
	}

	/* Set resources counters - 0 request means max available */
	vf_sb_count(vf) = resc->num_sbs;
	vf_rxq_count(vf) = resc->num_rxqs ? : bnx2x_vf_max_queue_cnt(bp, vf);
	vf_txq_count(vf) = resc->num_txqs ? : bnx2x_vf_max_queue_cnt(bp, vf);

	DP(BNX2X_MSG_IOV,
	   "Fulfilling vf request: sb count %d, tx_count %d, rx_count %d, mac_rules_count %d, vlan_rules_count %d\n",
	   vf_sb_count(vf), vf_rxq_count(vf),
	   vf_txq_count(vf), vf_mac_rules_cnt(vf),
	   vf_vlan_rules_cnt(vf));

	/* Initialize the queues */
	if (!vf->vfqs) {
		DP(BNX2X_MSG_IOV, "vf->vfqs was not allocated\n");
		return -EINVAL;
	}

	for_each_vfq(vf, i) {
		struct bnx2x_vf_queue *q = vfq_get(vf, i);

		if (!q) {
			BNX2X_ERR("q number %d was not allocated\n", i);
			return -EINVAL;
		}

		q->index = i;
		q->cxt = &((base_cxt + i)->eth);
		q->cid = BNX2X_FIRST_VF_CID + base_vf_cid + i;

		DP(BNX2X_MSG_IOV, "VFQ[%d:%d]: index %d, cid 0x%x, cxt %p\n",
		   vf->abs_vfid, i, q->index, q->cid, q->cxt);

		/* init SP objects */
		bnx2x_vfq_init(bp, vf, q);
	}
	vf->state = VF_ACQUIRED;
	return 0;
}

int bnx2x_vf_init(struct bnx2x *bp, struct bnx2x_virtf *vf, dma_addr_t *sb_map)
{
	struct bnx2x_func_init_params func_init = {0};
	int i;

	/* the sb resources are initialized at this point, do the
	 * FW/HW initializations
	 */
	for_each_vf_sb(vf, i)
		bnx2x_init_sb(bp, (dma_addr_t)sb_map[i], vf->abs_vfid, true,
			      vf_igu_sb(vf, i), vf_igu_sb(vf, i));

	/* Sanity checks */
	if (vf->state != VF_ACQUIRED) {
		DP(BNX2X_MSG_IOV, "VF[%d] is not in VF_ACQUIRED, but %d\n",
		   vf->abs_vfid, vf->state);
		return -EINVAL;
	}

	/* let FLR complete ... */
	msleep(100);

	/* FLR cleanup epilogue */
	if (bnx2x_vf_flr_clnup_epilog(bp, vf->abs_vfid))
		return -EBUSY;

	/* reset IGU VF statistics: MSIX */
	REG_WR(bp, IGU_REG_STATISTIC_NUM_MESSAGE_SENT + vf->abs_vfid * 4 , 0);

	/* function setup */
	func_init.pf_id = BP_FUNC(bp);
	func_init.func_id = FW_VF_HANDLE(vf->abs_vfid);
	bnx2x_func_init(bp, &func_init);

	/* Enable the vf */
	bnx2x_vf_enable_access(bp, vf->abs_vfid);
	bnx2x_vf_enable_traffic(bp, vf);

	/* queue protection table */
	for_each_vfq(vf, i)
		bnx2x_vf_qtbl_set_q(bp, vf->abs_vfid,
				    vfq_qzone_id(vf, vfq_get(vf, i)), true);

	vf->state = VF_ENABLED;

	/* update vf bulletin board */
	bnx2x_post_vf_bulletin(bp, vf->index);

	return 0;
}

struct set_vf_state_cookie {
	struct bnx2x_virtf *vf;
	u8 state;
};

static void bnx2x_set_vf_state(void *cookie)
{
	struct set_vf_state_cookie *p = (struct set_vf_state_cookie *)cookie;

	p->vf->state = p->state;
}

int bnx2x_vf_close(struct bnx2x *bp, struct bnx2x_virtf *vf)
{
	int rc = 0, i;

	DP(BNX2X_MSG_IOV, "vf[%d]\n", vf->abs_vfid);

	/* Close all queues */
	for (i = 0; i < vf_rxq_count(vf); i++) {
		rc = bnx2x_vf_queue_teardown(bp, vf, i);
		if (rc)
			goto op_err;
	}

	/* disable the interrupts */
	DP(BNX2X_MSG_IOV, "disabling igu\n");
	bnx2x_vf_igu_disable(bp, vf);

	/* disable the VF */
	DP(BNX2X_MSG_IOV, "clearing qtbl\n");
	bnx2x_vf_clr_qtbl(bp, vf);

	/* need to make sure there are no outstanding stats ramrods which may
	 * cause the device to access the VF's stats buffer which it will free
	 * as soon as we return from the close flow.
	 */
	{
		struct set_vf_state_cookie cookie;

		cookie.vf = vf;
		cookie.state = VF_ACQUIRED;
		rc = bnx2x_stats_safe_exec(bp, bnx2x_set_vf_state, &cookie);
		if (rc)
			goto op_err;
	}

	DP(BNX2X_MSG_IOV, "set state to acquired\n");

	return 0;
op_err:
	BNX2X_ERR("vf[%d] CLOSE error: rc %d\n", vf->abs_vfid, rc);
	return rc;
}

/* VF release can be called either: 1. The VF was acquired but
 * not enabled 2. the vf was enabled or in the process of being
 * enabled
 */
int bnx2x_vf_free(struct bnx2x *bp, struct bnx2x_virtf *vf)
{
	int rc;

	DP(BNX2X_MSG_IOV, "VF[%d] STATE: %s\n", vf->abs_vfid,
	   vf->state == VF_FREE ? "Free" :
	   vf->state == VF_ACQUIRED ? "Acquired" :
	   vf->state == VF_ENABLED ? "Enabled" :
	   vf->state == VF_RESET ? "Reset" :
	   "Unknown");

	switch (vf->state) {
	case VF_ENABLED:
		rc = bnx2x_vf_close(bp, vf);
		if (rc)
			goto op_err;
		/* Fallthrough to release resources */
	case VF_ACQUIRED:
		DP(BNX2X_MSG_IOV, "about to free resources\n");
		bnx2x_vf_free_resc(bp, vf);
		break;

	case VF_FREE:
	case VF_RESET:
	default:
		break;
	}
	return 0;
op_err:
	BNX2X_ERR("VF[%d] RELEASE error: rc %d\n", vf->abs_vfid, rc);
	return rc;
}

int bnx2x_vf_rss_update(struct bnx2x *bp, struct bnx2x_virtf *vf,
			struct bnx2x_config_rss_params *rss)
{
	DP(BNX2X_MSG_IOV, "vf[%d]\n", vf->abs_vfid);
	set_bit(RAMROD_COMP_WAIT, &rss->ramrod_flags);
	return bnx2x_config_rss(bp, rss);
}

int bnx2x_vf_tpa_update(struct bnx2x *bp, struct bnx2x_virtf *vf,
			struct vfpf_tpa_tlv *tlv,
			struct bnx2x_queue_update_tpa_params *params)
{
	aligned_u64 *sge_addr = tlv->tpa_client_info.sge_addr;
	struct bnx2x_queue_state_params qstate;
	int qid, rc = 0;

	DP(BNX2X_MSG_IOV, "vf[%d]\n", vf->abs_vfid);

	/* Set ramrod params */
	memset(&qstate, 0, sizeof(struct bnx2x_queue_state_params));
	memcpy(&qstate.params.update_tpa, params,
	       sizeof(struct bnx2x_queue_update_tpa_params));
	qstate.cmd = BNX2X_Q_CMD_UPDATE_TPA;
	set_bit(RAMROD_COMP_WAIT, &qstate.ramrod_flags);

	for (qid = 0; qid < vf_rxq_count(vf); qid++) {
		qstate.q_obj = &bnx2x_vfq(vf, qid, sp_obj);
		qstate.params.update_tpa.sge_map = sge_addr[qid];
		DP(BNX2X_MSG_IOV, "sge_addr[%d:%d] %08x:%08x\n",
		   vf->abs_vfid, qid, U64_HI(sge_addr[qid]),
		   U64_LO(sge_addr[qid]));
		rc = bnx2x_queue_state_change(bp, &qstate);
		if (rc) {
			BNX2X_ERR("Failed to configure sge_addr %08x:%08x for [%d:%d]\n",
				  U64_HI(sge_addr[qid]), U64_LO(sge_addr[qid]),
				  vf->abs_vfid, qid);
			return rc;
		}
	}

	return rc;
}

/* VF release ~ VF close + VF release-resources
 * Release is the ultimate SW shutdown and is called whenever an
 * irrecoverable error is encountered.
 */
int bnx2x_vf_release(struct bnx2x *bp, struct bnx2x_virtf *vf)
{
	int rc;

	DP(BNX2X_MSG_IOV, "PF releasing vf %d\n", vf->abs_vfid);
	bnx2x_lock_vf_pf_channel(bp, vf, CHANNEL_TLV_PF_RELEASE_VF);

	rc = bnx2x_vf_free(bp, vf);
	if (rc)
		WARN(rc,
		     "VF[%d] Failed to allocate resources for release op- rc=%d\n",
		     vf->abs_vfid, rc);
	bnx2x_unlock_vf_pf_channel(bp, vf, CHANNEL_TLV_PF_RELEASE_VF);
	return rc;
}

void bnx2x_lock_vf_pf_channel(struct bnx2x *bp, struct bnx2x_virtf *vf,
			      enum channel_tlvs tlv)
{
	/* we don't lock the channel for unsupported tlvs */
	if (!bnx2x_tlv_supported(tlv)) {
		BNX2X_ERR("attempting to lock with unsupported tlv. Aborting\n");
		return;
	}

	/* lock the channel */
	mutex_lock(&vf->op_mutex);

	/* record the locking op */
	vf->op_current = tlv;

	/* log the lock */
	DP(BNX2X_MSG_IOV, "VF[%d]: vf pf channel locked by %d\n",
	   vf->abs_vfid, tlv);
}

void bnx2x_unlock_vf_pf_channel(struct bnx2x *bp, struct bnx2x_virtf *vf,
				enum channel_tlvs expected_tlv)
{
	enum channel_tlvs current_tlv;

	if (!vf) {
		BNX2X_ERR("VF was %p\n", vf);
		return;
	}

	current_tlv = vf->op_current;

	/* we don't unlock the channel for unsupported tlvs */
	if (!bnx2x_tlv_supported(expected_tlv))
		return;

	WARN(expected_tlv != vf->op_current,
	     "lock mismatch: expected %d found %d", expected_tlv,
	     vf->op_current);

	/* record the locking op */
	vf->op_current = CHANNEL_TLV_NONE;

	/* lock the channel */
	mutex_unlock(&vf->op_mutex);

	/* log the unlock */
	DP(BNX2X_MSG_IOV, "VF[%d]: vf pf channel unlocked by %d\n",
	   vf->abs_vfid, current_tlv);
}

static int bnx2x_set_pf_tx_switching(struct bnx2x *bp, bool enable)
{
	struct bnx2x_queue_state_params q_params;
	u32 prev_flags;
	int i, rc;

	/* Verify changes are needed and record current Tx switching state */
	prev_flags = bp->flags;
	if (enable)
		bp->flags |= TX_SWITCHING;
	else
		bp->flags &= ~TX_SWITCHING;
	if (prev_flags == bp->flags)
		return 0;

	/* Verify state enables the sending of queue ramrods */
	if ((bp->state != BNX2X_STATE_OPEN) ||
	    (bnx2x_get_q_logical_state(bp,
				      &bnx2x_sp_obj(bp, &bp->fp[0]).q_obj) !=
	     BNX2X_Q_LOGICAL_STATE_ACTIVE))
		return 0;

	/* send q. update ramrod to configure Tx switching */
	memset(&q_params, 0, sizeof(q_params));
	__set_bit(RAMROD_COMP_WAIT, &q_params.ramrod_flags);
	q_params.cmd = BNX2X_Q_CMD_UPDATE;
	__set_bit(BNX2X_Q_UPDATE_TX_SWITCHING_CHNG,
		  &q_params.params.update.update_flags);
	if (enable)
		__set_bit(BNX2X_Q_UPDATE_TX_SWITCHING,
			  &q_params.params.update.update_flags);
	else
		__clear_bit(BNX2X_Q_UPDATE_TX_SWITCHING,
			    &q_params.params.update.update_flags);

	/* send the ramrod on all the queues of the PF */
	for_each_eth_queue(bp, i) {
		struct bnx2x_fastpath *fp = &bp->fp[i];

		/* Set the appropriate Queue object */
		q_params.q_obj = &bnx2x_sp_obj(bp, fp).q_obj;

		/* Update the Queue state */
		rc = bnx2x_queue_state_change(bp, &q_params);
		if (rc) {
			BNX2X_ERR("Failed to configure Tx switching\n");
			return rc;
		}
	}

	DP(BNX2X_MSG_IOV, "%s Tx Switching\n", enable ? "Enabled" : "Disabled");
	return 0;
}

int bnx2x_sriov_configure(struct pci_dev *dev, int num_vfs_param)
{
	struct bnx2x *bp = netdev_priv(pci_get_drvdata(dev));

	if (!IS_SRIOV(bp)) {
		BNX2X_ERR("failed to configure SR-IOV since vfdb was not allocated. Check dmesg for errors in probe stage\n");
		return -EINVAL;
	}

	DP(BNX2X_MSG_IOV, "bnx2x_sriov_configure called with %d, BNX2X_NR_VIRTFN(bp) was %d\n",
	   num_vfs_param, BNX2X_NR_VIRTFN(bp));

	/* HW channel is only operational when PF is up */
	if (bp->state != BNX2X_STATE_OPEN) {
		BNX2X_ERR("VF num configuration via sysfs not supported while PF is down\n");
		return -EINVAL;
	}

	/* we are always bound by the total_vfs in the configuration space */
	if (num_vfs_param > BNX2X_NR_VIRTFN(bp)) {
		BNX2X_ERR("truncating requested number of VFs (%d) down to maximum allowed (%d)\n",
			  num_vfs_param, BNX2X_NR_VIRTFN(bp));
		num_vfs_param = BNX2X_NR_VIRTFN(bp);
	}

	bp->requested_nr_virtfn = num_vfs_param;
	if (num_vfs_param == 0) {
		bnx2x_set_pf_tx_switching(bp, false);
		bnx2x_disable_sriov(bp);
		return 0;
	} else {
		return bnx2x_enable_sriov(bp);
	}
}

#define IGU_ENTRY_SIZE 4

int bnx2x_enable_sriov(struct bnx2x *bp)
{
	int rc = 0, req_vfs = bp->requested_nr_virtfn;
	int vf_idx, sb_idx, vfq_idx, qcount, first_vf;
	u32 igu_entry, address;
	u16 num_vf_queues;

	if (req_vfs == 0)
		return 0;

	first_vf = bp->vfdb->sriov.first_vf_in_pf;

	/* statically distribute vf sb pool between VFs */
	num_vf_queues = min_t(u16, BNX2X_VF_MAX_QUEUES,
			      BP_VFDB(bp)->vf_sbs_pool / req_vfs);

	/* zero previous values learned from igu cam */
	for (vf_idx = 0; vf_idx < req_vfs; vf_idx++) {
		struct bnx2x_virtf *vf = BP_VF(bp, vf_idx);

		vf->sb_count = 0;
		vf_sb_count(BP_VF(bp, vf_idx)) = 0;
	}
	bp->vfdb->vf_sbs_pool = 0;

	/* prepare IGU cam */
	sb_idx = BP_VFDB(bp)->first_vf_igu_entry;
	address = IGU_REG_MAPPING_MEMORY + sb_idx * IGU_ENTRY_SIZE;
	for (vf_idx = first_vf; vf_idx < first_vf + req_vfs; vf_idx++) {
		for (vfq_idx = 0; vfq_idx < num_vf_queues; vfq_idx++) {
			igu_entry = vf_idx << IGU_REG_MAPPING_MEMORY_FID_SHIFT |
				vfq_idx << IGU_REG_MAPPING_MEMORY_VECTOR_SHIFT |
				IGU_REG_MAPPING_MEMORY_VALID;
			DP(BNX2X_MSG_IOV, "assigning sb %d to vf %d\n",
			   sb_idx, vf_idx);
			REG_WR(bp, address, igu_entry);
			sb_idx++;
			address += IGU_ENTRY_SIZE;
		}
	}

	/* Reinitialize vf database according to igu cam */
	bnx2x_get_vf_igu_cam_info(bp);

	DP(BNX2X_MSG_IOV, "vf_sbs_pool %d, num_vf_queues %d\n",
	   BP_VFDB(bp)->vf_sbs_pool, num_vf_queues);

	qcount = 0;
	for_each_vf(bp, vf_idx) {
		struct bnx2x_virtf *vf = BP_VF(bp, vf_idx);

		/* set local queue arrays */
		vf->vfqs = &bp->vfdb->vfqs[qcount];
		qcount += vf_sb_count(vf);
		bnx2x_iov_static_resc(bp, vf);
	}

	/* prepare msix vectors in VF configuration space - the value in the
	 * PCI configuration space should be the index of the last entry,
	 * namely one less than the actual size of the table
	 */
	for (vf_idx = first_vf; vf_idx < first_vf + req_vfs; vf_idx++) {
		bnx2x_pretend_func(bp, HW_VF_HANDLE(bp, vf_idx));
		REG_WR(bp, PCICFG_OFFSET + GRC_CONFIG_REG_VF_MSIX_CONTROL,
		       num_vf_queues - 1);
		DP(BNX2X_MSG_IOV, "set msix vec num in VF %d cfg space to %d\n",
		   vf_idx, num_vf_queues - 1);
	}
	bnx2x_pretend_func(bp, BP_ABS_FUNC(bp));

	/* enable sriov. This will probe all the VFs, and consequentially cause
	 * the "acquire" messages to appear on the VF PF channel.
	 */
	DP(BNX2X_MSG_IOV, "about to call enable sriov\n");
	bnx2x_disable_sriov(bp);

	rc = bnx2x_set_pf_tx_switching(bp, true);
	if (rc)
		return rc;

	rc = pci_enable_sriov(bp->pdev, req_vfs);
	if (rc) {
		BNX2X_ERR("pci_enable_sriov failed with %d\n", rc);
		return rc;
	}
	DP(BNX2X_MSG_IOV, "sriov enabled (%d vfs)\n", req_vfs);
	return req_vfs;
}

void bnx2x_pf_set_vfs_vlan(struct bnx2x *bp)
{
	int vfidx;
	struct pf_vf_bulletin_content *bulletin;

	DP(BNX2X_MSG_IOV, "configuring vlan for VFs from sp-task\n");
	for_each_vf(bp, vfidx) {
		bulletin = BP_VF_BULLETIN(bp, vfidx);
		if (bulletin->valid_bitmap & (1 << VLAN_VALID))
			bnx2x_set_vf_vlan(bp->dev, vfidx, bulletin->vlan, 0,
					  htons(ETH_P_8021Q));
	}
}

void bnx2x_disable_sriov(struct bnx2x *bp)
{
	if (pci_vfs_assigned(bp->pdev)) {
		DP(BNX2X_MSG_IOV,
		   "Unloading driver while VFs are assigned - VFs will not be deallocated\n");
		return;
	}

	pci_disable_sriov(bp->pdev);
}

static int bnx2x_vf_op_prep(struct bnx2x *bp, int vfidx,
			    struct bnx2x_virtf **vf,
			    struct pf_vf_bulletin_content **bulletin,
			    bool test_queue)
{
	if (bp->state != BNX2X_STATE_OPEN) {
		BNX2X_ERR("PF is down - can't utilize iov-related functionality\n");
		return -EINVAL;
	}

	if (!IS_SRIOV(bp)) {
		BNX2X_ERR("sriov is disabled - can't utilize iov-related functionality\n");
		return -EINVAL;
	}

	if (vfidx >= BNX2X_NR_VIRTFN(bp)) {
		BNX2X_ERR("VF is uninitialized - can't utilize iov-related functionality. vfidx was %d BNX2X_NR_VIRTFN was %d\n",
			  vfidx, BNX2X_NR_VIRTFN(bp));
		return -EINVAL;
	}

	/* init members */
	*vf = BP_VF(bp, vfidx);
	*bulletin = BP_VF_BULLETIN(bp, vfidx);

	if (!*vf) {
		BNX2X_ERR("Unable to get VF structure for vfidx %d\n", vfidx);
		return -EINVAL;
	}

	if (test_queue && !(*vf)->vfqs) {
		BNX2X_ERR("vfqs struct is null. Was this invoked before dynamically enabling SR-IOV? vfidx was %d\n",
			  vfidx);
		return -EINVAL;
	}

	if (!*bulletin) {
		BNX2X_ERR("Bulletin Board struct is null for vfidx %d\n",
			  vfidx);
		return -EINVAL;
	}

	return 0;
}

int bnx2x_get_vf_config(struct net_device *dev, int vfidx,
			struct ifla_vf_info *ivi)
{
	struct bnx2x *bp = netdev_priv(dev);
	struct bnx2x_virtf *vf = NULL;
	struct pf_vf_bulletin_content *bulletin = NULL;
	struct bnx2x_vlan_mac_obj *mac_obj;
	struct bnx2x_vlan_mac_obj *vlan_obj;
	int rc;

	/* sanity and init */
	rc = bnx2x_vf_op_prep(bp, vfidx, &vf, &bulletin, true);
	if (rc)
		return rc;

	mac_obj = &bnx2x_leading_vfq(vf, mac_obj);
	vlan_obj = &bnx2x_leading_vfq(vf, vlan_obj);
	if (!mac_obj || !vlan_obj) {
		BNX2X_ERR("VF partially initialized\n");
		return -EINVAL;
	}

	ivi->vf = vfidx;
	ivi->qos = 0;
	ivi->max_tx_rate = 10000; /* always 10G. TBA take from link struct */
	ivi->min_tx_rate = 0;
	ivi->spoofchk = 1; /*always enabled */
	if (vf->state == VF_ENABLED) {
		/* mac and vlan are in vlan_mac objects */
		if (bnx2x_validate_vf_sp_objs(bp, vf, false)) {
			mac_obj->get_n_elements(bp, mac_obj, 1, (u8 *)&ivi->mac,
						0, ETH_ALEN);
			vlan_obj->get_n_elements(bp, vlan_obj, 1,
						 (u8 *)&ivi->vlan, 0,
						 VLAN_HLEN);
		}
	} else {
		mutex_lock(&bp->vfdb->bulletin_mutex);
		/* mac */
		if (bulletin->valid_bitmap & (1 << MAC_ADDR_VALID))
			/* mac configured by ndo so its in bulletin board */
			memcpy(&ivi->mac, bulletin->mac, ETH_ALEN);
		else
			/* function has not been loaded yet. Show mac as 0s */
			eth_zero_addr(ivi->mac);

		/* vlan */
		if (bulletin->valid_bitmap & (1 << VLAN_VALID))
			/* vlan configured by ndo so its in bulletin board */
			memcpy(&ivi->vlan, &bulletin->vlan, VLAN_HLEN);
		else
			/* function has not been loaded yet. Show vlans as 0s */
			memset(&ivi->vlan, 0, VLAN_HLEN);

		mutex_unlock(&bp->vfdb->bulletin_mutex);
	}

	return 0;
}

/* New mac for VF. Consider these cases:
 * 1. VF hasn't been acquired yet - save the mac in local bulletin board and
 *    supply at acquire.
 * 2. VF has already been acquired but has not yet initialized - store in local
 *    bulletin board. mac will be posted on VF bulletin board after VF init. VF
 *    will configure this mac when it is ready.
 * 3. VF has already initialized but has not yet setup a queue - post the new
 *    mac on VF's bulletin board right now. VF will configure this mac when it
 *    is ready.
 * 4. VF has already set a queue - delete any macs already configured for this
 *    queue and manually config the new mac.
 * In any event, once this function has been called refuse any attempts by the
 * VF to configure any mac for itself except for this mac. In case of a race
 * where the VF fails to see the new post on its bulletin board before sending a
 * mac configuration request, the PF will simply fail the request and VF can try
 * again after consulting its bulletin board.
 */
int bnx2x_set_vf_mac(struct net_device *dev, int vfidx, u8 *mac)
{
	struct bnx2x *bp = netdev_priv(dev);
	int rc, q_logical_state;
	struct bnx2x_virtf *vf = NULL;
	struct pf_vf_bulletin_content *bulletin = NULL;

	if (!is_valid_ether_addr(mac)) {
		BNX2X_ERR("mac address invalid\n");
		return -EINVAL;
	}

	/* sanity and init */
	rc = bnx2x_vf_op_prep(bp, vfidx, &vf, &bulletin, true);
	if (rc)
		return rc;

	mutex_lock(&bp->vfdb->bulletin_mutex);

	/* update PF's copy of the VF's bulletin. Will no longer accept mac
	 * configuration requests from vf unless match this mac
	 */
	bulletin->valid_bitmap |= 1 << MAC_ADDR_VALID;
	memcpy(bulletin->mac, mac, ETH_ALEN);

	/* Post update on VF's bulletin board */
	rc = bnx2x_post_vf_bulletin(bp, vfidx);

	/* release lock before checking return code */
	mutex_unlock(&bp->vfdb->bulletin_mutex);

	if (rc) {
		BNX2X_ERR("failed to update VF[%d] bulletin\n", vfidx);
		return rc;
	}

	q_logical_state =
		bnx2x_get_q_logical_state(bp, &bnx2x_leading_vfq(vf, sp_obj));
	if (vf->state == VF_ENABLED &&
	    q_logical_state == BNX2X_Q_LOGICAL_STATE_ACTIVE) {
		/* configure the mac in device on this vf's queue */
		unsigned long ramrod_flags = 0;
		struct bnx2x_vlan_mac_obj *mac_obj;

		/* User should be able to see failure reason in system logs */
		if (!bnx2x_validate_vf_sp_objs(bp, vf, true))
			return -EINVAL;

		/* must lock vfpf channel to protect against vf flows */
		bnx2x_lock_vf_pf_channel(bp, vf, CHANNEL_TLV_PF_SET_MAC);

		/* remove existing eth macs */
		mac_obj = &bnx2x_leading_vfq(vf, mac_obj);
		rc = bnx2x_del_all_macs(bp, mac_obj, BNX2X_ETH_MAC, true);
		if (rc) {
			BNX2X_ERR("failed to delete eth macs\n");
			rc = -EINVAL;
			goto out;
		}

		/* remove existing uc list macs */
		rc = bnx2x_del_all_macs(bp, mac_obj, BNX2X_UC_LIST_MAC, true);
		if (rc) {
			BNX2X_ERR("failed to delete uc_list macs\n");
			rc = -EINVAL;
			goto out;
		}

		/* configure the new mac to device */
		__set_bit(RAMROD_COMP_WAIT, &ramrod_flags);
		bnx2x_set_mac_one(bp, (u8 *)&bulletin->mac, mac_obj, true,
				  BNX2X_ETH_MAC, &ramrod_flags);

out:
		bnx2x_unlock_vf_pf_channel(bp, vf, CHANNEL_TLV_PF_SET_MAC);
	}

	return rc;
}

static void bnx2x_set_vf_vlan_acceptance(struct bnx2x *bp,
					 struct bnx2x_virtf *vf, bool accept)
{
	struct bnx2x_rx_mode_ramrod_params rx_ramrod;
	unsigned long accept_flags;

	/* need to remove/add the VF's accept_any_vlan bit */
	accept_flags = bnx2x_leading_vfq(vf, accept_flags);
	if (accept)
		set_bit(BNX2X_ACCEPT_ANY_VLAN, &accept_flags);
	else
		clear_bit(BNX2X_ACCEPT_ANY_VLAN, &accept_flags);

	bnx2x_vf_prep_rx_mode(bp, LEADING_IDX, &rx_ramrod, vf,
			      accept_flags);
	bnx2x_leading_vfq(vf, accept_flags) = accept_flags;
	bnx2x_config_rx_mode(bp, &rx_ramrod);
}

static int bnx2x_set_vf_vlan_filter(struct bnx2x *bp, struct bnx2x_virtf *vf,
				    u16 vlan, bool add)
{
	struct bnx2x_vlan_mac_ramrod_params ramrod_param;
	unsigned long ramrod_flags = 0;
	int rc = 0;

	/* configure the new vlan to device */
	memset(&ramrod_param, 0, sizeof(ramrod_param));
	__set_bit(RAMROD_COMP_WAIT, &ramrod_flags);
	ramrod_param.vlan_mac_obj = &bnx2x_leading_vfq(vf, vlan_obj);
	ramrod_param.ramrod_flags = ramrod_flags;
	ramrod_param.user_req.u.vlan.vlan = vlan;
	ramrod_param.user_req.cmd = add ? BNX2X_VLAN_MAC_ADD
					: BNX2X_VLAN_MAC_DEL;
	rc = bnx2x_config_vlan_mac(bp, &ramrod_param);
	if (rc) {
		BNX2X_ERR("failed to configure vlan\n");
		return -EINVAL;
	}

	return 0;
}

int bnx2x_set_vf_vlan(struct net_device *dev, int vfidx, u16 vlan, u8 qos,
		      __be16 vlan_proto)
{
	struct pf_vf_bulletin_content *bulletin = NULL;
	struct bnx2x *bp = netdev_priv(dev);
	struct bnx2x_vlan_mac_obj *vlan_obj;
	unsigned long vlan_mac_flags = 0;
	unsigned long ramrod_flags = 0;
	struct bnx2x_virtf *vf = NULL;
	int i, rc;

	if (vlan > 4095) {
		BNX2X_ERR("illegal vlan value %d\n", vlan);
		return -EINVAL;
	}

	if (vlan_proto != htons(ETH_P_8021Q))
		return -EPROTONOSUPPORT;

	DP(BNX2X_MSG_IOV, "configuring VF %d with VLAN %d qos %d\n",
	   vfidx, vlan, 0);

	/* sanity and init */
	rc = bnx2x_vf_op_prep(bp, vfidx, &vf, &bulletin, true);
	if (rc)
		return rc;

	/* update PF's copy of the VF's bulletin. No point in posting the vlan
	 * to the VF since it doesn't have anything to do with it. But it useful
	 * to store it here in case the VF is not up yet and we can only
	 * configure the vlan later when it does. Treat vlan id 0 as remove the
	 * Host tag.
	 */
	mutex_lock(&bp->vfdb->bulletin_mutex);

	if (vlan > 0)
		bulletin->valid_bitmap |= 1 << VLAN_VALID;
	else
		bulletin->valid_bitmap &= ~(1 << VLAN_VALID);
	bulletin->vlan = vlan;

	/* Post update on VF's bulletin board */
	rc = bnx2x_post_vf_bulletin(bp, vfidx);
	if (rc)
		BNX2X_ERR("failed to update VF[%d] bulletin\n", vfidx);
	mutex_unlock(&bp->vfdb->bulletin_mutex);

	/* is vf initialized and queue set up? */
	if (vf->state != VF_ENABLED ||
	    bnx2x_get_q_logical_state(bp, &bnx2x_leading_vfq(vf, sp_obj)) !=
	    BNX2X_Q_LOGICAL_STATE_ACTIVE)
		return rc;

	/* User should be able to see error in system logs */
	if (!bnx2x_validate_vf_sp_objs(bp, vf, true))
		return -EINVAL;

	/* must lock vfpf channel to protect against vf flows */
	bnx2x_lock_vf_pf_channel(bp, vf, CHANNEL_TLV_PF_SET_VLAN);

	/* remove existing vlans */
	__set_bit(RAMROD_COMP_WAIT, &ramrod_flags);
	vlan_obj = &bnx2x_leading_vfq(vf, vlan_obj);
	rc = vlan_obj->delete_all(bp, vlan_obj, &vlan_mac_flags,
				  &ramrod_flags);
	if (rc) {
		BNX2X_ERR("failed to delete vlans\n");
		rc = -EINVAL;
		goto out;
	}

	/* clear accept_any_vlan when HV forces vlan, otherwise
	 * according to VF capabilities
	 */
	if (vlan || !(vf->cfg_flags & VF_CFG_VLAN_FILTER))
		bnx2x_set_vf_vlan_acceptance(bp, vf, !vlan);

	rc = bnx2x_set_vf_vlan_filter(bp, vf, vlan, true);
	if (rc)
		goto out;

	/* send queue update ramrods to configure default vlan and
	 * silent vlan removal
	 */
	for_each_vfq(vf, i) {
		struct bnx2x_queue_state_params q_params = {NULL};
		struct bnx2x_queue_update_params *update_params;

		q_params.q_obj = &bnx2x_vfq(vf, i, sp_obj);

		/* validate the Q is UP */
		if (bnx2x_get_q_logical_state(bp, q_params.q_obj) !=
		    BNX2X_Q_LOGICAL_STATE_ACTIVE)
			continue;

		__set_bit(RAMROD_COMP_WAIT, &q_params.ramrod_flags);
		q_params.cmd = BNX2X_Q_CMD_UPDATE;
		update_params = &q_params.params.update;
		__set_bit(BNX2X_Q_UPDATE_DEF_VLAN_EN_CHNG,
			  &update_params->update_flags);
		__set_bit(BNX2X_Q_UPDATE_SILENT_VLAN_REM_CHNG,
			  &update_params->update_flags);
		if (vlan == 0) {
			/* if vlan is 0 then we want to leave the VF traffic
			 * untagged, and leave the incoming traffic untouched
			 * (i.e. do not remove any vlan tags).
			 */
			__clear_bit(BNX2X_Q_UPDATE_DEF_VLAN_EN,
				    &update_params->update_flags);
			__clear_bit(BNX2X_Q_UPDATE_SILENT_VLAN_REM,
				    &update_params->update_flags);
		} else {
			/* configure default vlan to vf queue and set silent
			 * vlan removal (the vf remains unaware of this vlan).
			 */
			__set_bit(BNX2X_Q_UPDATE_DEF_VLAN_EN,
				  &update_params->update_flags);
			__set_bit(BNX2X_Q_UPDATE_SILENT_VLAN_REM,
				  &update_params->update_flags);
			update_params->def_vlan = vlan;
			update_params->silent_removal_value =
				vlan & VLAN_VID_MASK;
			update_params->silent_removal_mask = VLAN_VID_MASK;
		}

		/* Update the Queue state */
		rc = bnx2x_queue_state_change(bp, &q_params);
		if (rc) {
			BNX2X_ERR("Failed to configure default VLAN queue %d\n",
				  i);
			goto out;
		}
	}
out:
	bnx2x_unlock_vf_pf_channel(bp, vf, CHANNEL_TLV_PF_SET_VLAN);

	if (rc)
		DP(BNX2X_MSG_IOV,
		   "updated VF[%d] vlan configuration (vlan = %d)\n",
		   vfidx, vlan);

	return rc;
}

/* crc is the first field in the bulletin board. Compute the crc over the
 * entire bulletin board excluding the crc field itself. Use the length field
 * as the Bulletin Board was posted by a PF with possibly a different version
 * from the vf which will sample it. Therefore, the length is computed by the
 * PF and then used blindly by the VF.
 */
u32 bnx2x_crc_vf_bulletin(struct pf_vf_bulletin_content *bulletin)
{
	return crc32(BULLETIN_CRC_SEED,
		 ((u8 *)bulletin) + sizeof(bulletin->crc),
		 bulletin->length - sizeof(bulletin->crc));
}

/* Check for new posts on the bulletin board */
enum sample_bulletin_result bnx2x_sample_bulletin(struct bnx2x *bp)
{
	struct pf_vf_bulletin_content *bulletin;
	int attempts;

	/* sampling structure in mid post may result with corrupted data
	 * validate crc to ensure coherency.
	 */
	for (attempts = 0; attempts < BULLETIN_ATTEMPTS; attempts++) {
		u32 crc;

		/* sample the bulletin board */
		memcpy(&bp->shadow_bulletin, bp->pf2vf_bulletin,
		       sizeof(union pf_vf_bulletin));

		crc = bnx2x_crc_vf_bulletin(&bp->shadow_bulletin.content);

		if (bp->shadow_bulletin.content.crc == crc)
			break;

		BNX2X_ERR("bad crc on bulletin board. Contained %x computed %x\n",
			  bp->shadow_bulletin.content.crc, crc);
	}

	if (attempts >= BULLETIN_ATTEMPTS) {
		BNX2X_ERR("pf to vf bulletin board crc was wrong %d consecutive times. Aborting\n",
			  attempts);
		return PFVF_BULLETIN_CRC_ERR;
	}
	bulletin = &bp->shadow_bulletin.content;

	/* bulletin board hasn't changed since last sample */
	if (bp->old_bulletin.version == bulletin->version)
		return PFVF_BULLETIN_UNCHANGED;

	/* the mac address in bulletin board is valid and is new */
	if (bulletin->valid_bitmap & 1 << MAC_ADDR_VALID &&
	    !ether_addr_equal(bulletin->mac, bp->old_bulletin.mac)) {
		/* update new mac to net device */
		memcpy(bp->dev->dev_addr, bulletin->mac, ETH_ALEN);
	}

	if (bulletin->valid_bitmap & (1 << LINK_VALID)) {
		DP(BNX2X_MSG_IOV, "link update speed %d flags %x\n",
		   bulletin->link_speed, bulletin->link_flags);

		bp->vf_link_vars.line_speed = bulletin->link_speed;
		bp->vf_link_vars.link_report_flags = 0;
		/* Link is down */
		if (bulletin->link_flags & VFPF_LINK_REPORT_LINK_DOWN)
			__set_bit(BNX2X_LINK_REPORT_LINK_DOWN,
				  &bp->vf_link_vars.link_report_flags);
		/* Full DUPLEX */
		if (bulletin->link_flags & VFPF_LINK_REPORT_FULL_DUPLEX)
			__set_bit(BNX2X_LINK_REPORT_FD,
				  &bp->vf_link_vars.link_report_flags);
		/* Rx Flow Control is ON */
		if (bulletin->link_flags & VFPF_LINK_REPORT_RX_FC_ON)
			__set_bit(BNX2X_LINK_REPORT_RX_FC_ON,
				  &bp->vf_link_vars.link_report_flags);
		/* Tx Flow Control is ON */
		if (bulletin->link_flags & VFPF_LINK_REPORT_TX_FC_ON)
			__set_bit(BNX2X_LINK_REPORT_TX_FC_ON,
				  &bp->vf_link_vars.link_report_flags);
		__bnx2x_link_report(bp);
	}

	/* copy new bulletin board to bp */
	memcpy(&bp->old_bulletin, bulletin,
	       sizeof(struct pf_vf_bulletin_content));

	return PFVF_BULLETIN_UPDATED;
}

void bnx2x_timer_sriov(struct bnx2x *bp)
{
	bnx2x_sample_bulletin(bp);

	/* if channel is down we need to self destruct */
	if (bp->old_bulletin.valid_bitmap & 1 << CHANNEL_DOWN)
		bnx2x_schedule_sp_rtnl(bp, BNX2X_SP_RTNL_VFPF_CHANNEL_DOWN,
				       BNX2X_MSG_IOV);
}

void __iomem *bnx2x_vf_doorbells(struct bnx2x *bp)
{
	/* vf doorbells are embedded within the regview */
	return bp->regview + PXP_VF_ADDR_DB_START;
}

void bnx2x_vf_pci_dealloc(struct bnx2x *bp)
{
	BNX2X_PCI_FREE(bp->vf2pf_mbox, bp->vf2pf_mbox_mapping,
		       sizeof(struct bnx2x_vf_mbx_msg));
	BNX2X_PCI_FREE(bp->pf2vf_bulletin, bp->pf2vf_bulletin_mapping,
		       sizeof(union pf_vf_bulletin));
}

int bnx2x_vf_pci_alloc(struct bnx2x *bp)
{
	mutex_init(&bp->vf2pf_mutex);

	/* allocate vf2pf mailbox for vf to pf channel */
	bp->vf2pf_mbox = BNX2X_PCI_ALLOC(&bp->vf2pf_mbox_mapping,
					 sizeof(struct bnx2x_vf_mbx_msg));
	if (!bp->vf2pf_mbox)
		goto alloc_mem_err;

	/* allocate pf 2 vf bulletin board */
	bp->pf2vf_bulletin = BNX2X_PCI_ALLOC(&bp->pf2vf_bulletin_mapping,
					     sizeof(union pf_vf_bulletin));
	if (!bp->pf2vf_bulletin)
		goto alloc_mem_err;

	bnx2x_vf_bulletin_finalize(&bp->pf2vf_bulletin->content, true);

	return 0;

alloc_mem_err:
	bnx2x_vf_pci_dealloc(bp);
	return -ENOMEM;
}

void bnx2x_iov_channel_down(struct bnx2x *bp)
{
	int vf_idx;
	struct pf_vf_bulletin_content *bulletin;

	if (!IS_SRIOV(bp))
		return;

	for_each_vf(bp, vf_idx) {
		/* locate this VFs bulletin board and update the channel down
		 * bit
		 */
		bulletin = BP_VF_BULLETIN(bp, vf_idx);
		bulletin->valid_bitmap |= 1 << CHANNEL_DOWN;

		/* update vf bulletin board */
		bnx2x_post_vf_bulletin(bp, vf_idx);
	}
}

void bnx2x_iov_task(struct work_struct *work)
{
	struct bnx2x *bp = container_of(work, struct bnx2x, iov_task.work);

	if (!netif_running(bp->dev))
		return;

	if (test_and_clear_bit(BNX2X_IOV_HANDLE_FLR,
			       &bp->iov_task_state))
		bnx2x_vf_handle_flr_event(bp);

	if (test_and_clear_bit(BNX2X_IOV_HANDLE_VF_MSG,
			       &bp->iov_task_state))
		bnx2x_vf_mbx(bp);
}

void bnx2x_schedule_iov_task(struct bnx2x *bp, enum bnx2x_iov_flag flag)
{
	smp_mb__before_atomic();
	set_bit(flag, &bp->iov_task_state);
	smp_mb__after_atomic();
	DP(BNX2X_MSG_IOV, "Scheduling iov task [Flag: %d]\n", flag);
	queue_delayed_work(bnx2x_iov_wq, &bp->iov_task, 0);
}
