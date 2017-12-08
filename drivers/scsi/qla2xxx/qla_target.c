/*
 *  qla_target.c SCSI LLD infrastructure for QLogic 22xx/23xx/24xx/25xx
 *
 *  based on qla2x00t.c code:
 *
 *  Copyright (C) 2004 - 2010 Vladislav Bolkhovitin <vst@vlnb.net>
 *  Copyright (C) 2004 - 2005 Leonid Stoljar
 *  Copyright (C) 2006 Nathaniel Clark <nate@misrule.us>
 *  Copyright (C) 2006 - 2010 ID7 Ltd.
 *
 *  Forward port and refactoring to modern qla2xxx and target/configfs
 *
 *  Copyright (C) 2010-2013 Nicholas A. Bellinger <nab@kernel.org>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation, version 2
 *  of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/blkdev.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/workqueue.h>
#include <asm/unaligned.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include <target/target_core_base.h>
#include <target/target_core_fabric.h>

#include "qla_def.h"
#include "qla_target.h"

static int ql2xtgt_tape_enable;
module_param(ql2xtgt_tape_enable, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(ql2xtgt_tape_enable,
		"Enables Sequence level error recovery (aka FC Tape). Default is 0 - no SLER. 1 - Enable SLER.");

static char *qlini_mode = QLA2XXX_INI_MODE_STR_ENABLED;
module_param(qlini_mode, charp, S_IRUGO);
MODULE_PARM_DESC(qlini_mode,
	"Determines when initiator mode will be enabled. Possible values: "
	"\"exclusive\" - initiator mode will be enabled on load, "
	"disabled on enabling target mode and then on disabling target mode "
	"enabled back; "
	"\"disabled\" - initiator mode will never be enabled; "
	"\"dual\" - Initiator Modes will be enabled. Target Mode can be activated "
	"when ready "
	"\"enabled\" (default) - initiator mode will always stay enabled.");

static int ql_dm_tgt_ex_pct = 0;
module_param(ql_dm_tgt_ex_pct, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(ql_dm_tgt_ex_pct,
	"For Dual Mode (qlini_mode=dual), this parameter determines "
	"the percentage of exchanges/cmds FW will allocate resources "
	"for Target mode.");

int ql2xuctrlirq = 1;
module_param(ql2xuctrlirq, int, 0644);
MODULE_PARM_DESC(ql2xuctrlirq,
    "User to control IRQ placement via smp_affinity."
    "Valid with qlini_mode=disabled."
    "1(default): enable");

int ql2x_ini_mode = QLA2XXX_INI_MODE_EXCLUSIVE;

static int temp_sam_status = SAM_STAT_BUSY;

/*
 * From scsi/fc/fc_fcp.h
 */
enum fcp_resp_rsp_codes {
	FCP_TMF_CMPL = 0,
	FCP_DATA_LEN_INVALID = 1,
	FCP_CMND_FIELDS_INVALID = 2,
	FCP_DATA_PARAM_MISMATCH = 3,
	FCP_TMF_REJECTED = 4,
	FCP_TMF_FAILED = 5,
	FCP_TMF_INVALID_LUN = 9,
};

/*
 * fc_pri_ta from scsi/fc/fc_fcp.h
 */
#define FCP_PTA_SIMPLE      0   /* simple task attribute */
#define FCP_PTA_HEADQ       1   /* head of queue task attribute */
#define FCP_PTA_ORDERED     2   /* ordered task attribute */
#define FCP_PTA_ACA         4   /* auto. contingent allegiance */
#define FCP_PTA_MASK        7   /* mask for task attribute field */
#define FCP_PRI_SHIFT       3   /* priority field starts in bit 3 */
#define FCP_PRI_RESVD_MASK  0x80        /* reserved bits in priority field */

/*
 * This driver calls qla2x00_alloc_iocbs() and qla2x00_issue_marker(), which
 * must be called under HW lock and could unlock/lock it inside.
 * It isn't an issue, since in the current implementation on the time when
 * those functions are called:
 *
 *   - Either context is IRQ and only IRQ handler can modify HW data,
 *     including rings related fields,
 *
 *   - Or access to target mode variables from struct qla_tgt doesn't
 *     cross those functions boundaries, except tgt_stop, which
 *     additionally protected by irq_cmd_count.
 */
/* Predefs for callbacks handed to qla2xxx LLD */
static void qlt_24xx_atio_pkt(struct scsi_qla_host *ha,
	struct atio_from_isp *pkt, uint8_t);
static void qlt_response_pkt(struct scsi_qla_host *ha, struct rsp_que *rsp,
	response_t *pkt);
static int qlt_issue_task_mgmt(struct fc_port *sess, u64 lun,
	int fn, void *iocb, int flags);
static void qlt_send_term_exchange(struct qla_qpair *, struct qla_tgt_cmd
	*cmd, struct atio_from_isp *atio, int ha_locked, int ul_abort);
static void qlt_alloc_qfull_cmd(struct scsi_qla_host *vha,
	struct atio_from_isp *atio, uint16_t status, int qfull);
static void qlt_disable_vha(struct scsi_qla_host *vha);
static void qlt_clear_tgt_db(struct qla_tgt *tgt);
static void qlt_send_notify_ack(struct qla_qpair *qpair,
	struct imm_ntfy_from_isp *ntfy,
	uint32_t add_flags, uint16_t resp_code, int resp_code_valid,
	uint16_t srr_flags, uint16_t srr_reject_code, uint8_t srr_explan);
static void qlt_send_term_imm_notif(struct scsi_qla_host *vha,
	struct imm_ntfy_from_isp *imm, int ha_locked);
static struct fc_port *qlt_create_sess(struct scsi_qla_host *vha,
	fc_port_t *fcport, bool local);
void qlt_unreg_sess(struct fc_port *sess);
static void qlt_24xx_handle_abts(struct scsi_qla_host *,
	struct abts_recv_from_24xx *);
static void qlt_send_busy(struct qla_qpair *, struct atio_from_isp *,
    uint16_t);

/*
 * Global Variables
 */
static struct kmem_cache *qla_tgt_mgmt_cmd_cachep;
struct kmem_cache *qla_tgt_plogi_cachep;
static mempool_t *qla_tgt_mgmt_cmd_mempool;
static struct workqueue_struct *qla_tgt_wq;
static DEFINE_MUTEX(qla_tgt_mutex);
static LIST_HEAD(qla_tgt_glist);

static const char *prot_op_str(u32 prot_op)
{
	switch (prot_op) {
	case TARGET_PROT_NORMAL:	return "NORMAL";
	case TARGET_PROT_DIN_INSERT:	return "DIN_INSERT";
	case TARGET_PROT_DOUT_INSERT:	return "DOUT_INSERT";
	case TARGET_PROT_DIN_STRIP:	return "DIN_STRIP";
	case TARGET_PROT_DOUT_STRIP:	return "DOUT_STRIP";
	case TARGET_PROT_DIN_PASS:	return "DIN_PASS";
	case TARGET_PROT_DOUT_PASS:	return "DOUT_PASS";
	default:			return "UNKNOWN";
	}
}

/* This API intentionally takes dest as a parameter, rather than returning
 * int value to avoid caller forgetting to issue wmb() after the store */
void qlt_do_generation_tick(struct scsi_qla_host *vha, int *dest)
{
	scsi_qla_host_t *base_vha = pci_get_drvdata(vha->hw->pdev);
	*dest = atomic_inc_return(&base_vha->generation_tick);
	/* memory barrier */
	wmb();
}

/* Might release hw lock, then reaquire!! */
static inline int qlt_issue_marker(struct scsi_qla_host *vha, int vha_locked)
{
	/* Send marker if required */
	if (unlikely(vha->marker_needed != 0)) {
		int rc = qla2x00_issue_marker(vha, vha_locked);
		if (rc != QLA_SUCCESS) {
			ql_dbg(ql_dbg_tgt, vha, 0xe03d,
			    "qla_target(%d): issue_marker() failed\n",
			    vha->vp_idx);
		}
		return rc;
	}
	return QLA_SUCCESS;
}

static inline
struct scsi_qla_host *qlt_find_host_by_d_id(struct scsi_qla_host *vha,
	uint8_t *d_id)
{
	struct scsi_qla_host *host;
	uint32_t key = 0;

	if ((vha->d_id.b.area == d_id[1]) && (vha->d_id.b.domain == d_id[0]) &&
	    (vha->d_id.b.al_pa == d_id[2]))
		return vha;

	key  = (uint32_t)d_id[0] << 16;
	key |= (uint32_t)d_id[1] <<  8;
	key |= (uint32_t)d_id[2];

	host = btree_lookup32(&vha->hw->tgt.host_map, key);
	if (!host)
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf005,
		    "Unable to find host %06x\n", key);

	return host;
}

static inline
struct scsi_qla_host *qlt_find_host_by_vp_idx(struct scsi_qla_host *vha,
	uint16_t vp_idx)
{
	struct qla_hw_data *ha = vha->hw;

	if (vha->vp_idx == vp_idx)
		return vha;

	BUG_ON(ha->tgt.tgt_vp_map == NULL);
	if (likely(test_bit(vp_idx, ha->vp_idx_map)))
		return ha->tgt.tgt_vp_map[vp_idx].vha;

	return NULL;
}

static inline void qlt_incr_num_pend_cmds(struct scsi_qla_host *vha)
{
	unsigned long flags;

	spin_lock_irqsave(&vha->hw->tgt.q_full_lock, flags);

	vha->hw->tgt.num_pend_cmds++;
	if (vha->hw->tgt.num_pend_cmds > vha->qla_stats.stat_max_pend_cmds)
		vha->qla_stats.stat_max_pend_cmds =
			vha->hw->tgt.num_pend_cmds;
	spin_unlock_irqrestore(&vha->hw->tgt.q_full_lock, flags);
}
static inline void qlt_decr_num_pend_cmds(struct scsi_qla_host *vha)
{
	unsigned long flags;

	spin_lock_irqsave(&vha->hw->tgt.q_full_lock, flags);
	vha->hw->tgt.num_pend_cmds--;
	spin_unlock_irqrestore(&vha->hw->tgt.q_full_lock, flags);
}


static void qlt_queue_unknown_atio(scsi_qla_host_t *vha,
	struct atio_from_isp *atio, uint8_t ha_locked)
{
	struct qla_tgt_sess_op *u;
	struct qla_tgt *tgt = vha->vha_tgt.qla_tgt;
	unsigned long flags;

	if (tgt->tgt_stop) {
		ql_dbg(ql_dbg_async, vha, 0x502c,
		    "qla_target(%d): dropping unknown ATIO_TYPE7, because tgt is being stopped",
		    vha->vp_idx);
		goto out_term;
	}

	u = kzalloc(sizeof(*u), GFP_ATOMIC);
	if (u == NULL)
		goto out_term;

	u->vha = vha;
	memcpy(&u->atio, atio, sizeof(*atio));
	INIT_LIST_HEAD(&u->cmd_list);

	spin_lock_irqsave(&vha->cmd_list_lock, flags);
	list_add_tail(&u->cmd_list, &vha->unknown_atio_list);
	spin_unlock_irqrestore(&vha->cmd_list_lock, flags);

	schedule_delayed_work(&vha->unknown_atio_work, 1);

out:
	return;

out_term:
	qlt_send_term_exchange(vha->hw->base_qpair, NULL, atio, ha_locked, 0);
	goto out;
}

static void qlt_try_to_dequeue_unknown_atios(struct scsi_qla_host *vha,
	uint8_t ha_locked)
{
	struct qla_tgt_sess_op *u, *t;
	scsi_qla_host_t *host;
	struct qla_tgt *tgt = vha->vha_tgt.qla_tgt;
	unsigned long flags;
	uint8_t queued = 0;

	list_for_each_entry_safe(u, t, &vha->unknown_atio_list, cmd_list) {
		if (u->aborted) {
			ql_dbg(ql_dbg_async, vha, 0x502e,
			    "Freeing unknown %s %p, because of Abort\n",
			    "ATIO_TYPE7", u);
			qlt_send_term_exchange(vha->hw->base_qpair, NULL,
			    &u->atio, ha_locked, 0);
			goto abort;
		}

		host = qlt_find_host_by_d_id(vha, u->atio.u.isp24.fcp_hdr.d_id);
		if (host != NULL) {
			ql_dbg(ql_dbg_async, vha, 0x502f,
			    "Requeuing unknown ATIO_TYPE7 %p\n", u);
			qlt_24xx_atio_pkt(host, &u->atio, ha_locked);
		} else if (tgt->tgt_stop) {
			ql_dbg(ql_dbg_async, vha, 0x503a,
			    "Freeing unknown %s %p, because tgt is being stopped\n",
			    "ATIO_TYPE7", u);
			qlt_send_term_exchange(vha->hw->base_qpair, NULL,
			    &u->atio, ha_locked, 0);
		} else {
			ql_dbg(ql_dbg_async, vha, 0x503d,
			    "Reschedule u %p, vha %p, host %p\n", u, vha, host);
			if (!queued) {
				queued = 1;
				schedule_delayed_work(&vha->unknown_atio_work,
				    1);
			}
			continue;
		}

abort:
		spin_lock_irqsave(&vha->cmd_list_lock, flags);
		list_del(&u->cmd_list);
		spin_unlock_irqrestore(&vha->cmd_list_lock, flags);
		kfree(u);
	}
}

void qlt_unknown_atio_work_fn(struct work_struct *work)
{
	struct scsi_qla_host *vha = container_of(to_delayed_work(work),
	    struct scsi_qla_host, unknown_atio_work);

	qlt_try_to_dequeue_unknown_atios(vha, 0);
}

static bool qlt_24xx_atio_pkt_all_vps(struct scsi_qla_host *vha,
	struct atio_from_isp *atio, uint8_t ha_locked)
{
	ql_dbg(ql_dbg_tgt, vha, 0xe072,
		"%s: qla_target(%d): type %x ox_id %04x\n",
		__func__, vha->vp_idx, atio->u.raw.entry_type,
		be16_to_cpu(atio->u.isp24.fcp_hdr.ox_id));

	switch (atio->u.raw.entry_type) {
	case ATIO_TYPE7:
	{
		struct scsi_qla_host *host = qlt_find_host_by_d_id(vha,
		    atio->u.isp24.fcp_hdr.d_id);
		if (unlikely(NULL == host)) {
			ql_dbg(ql_dbg_tgt, vha, 0xe03e,
			    "qla_target(%d): Received ATIO_TYPE7 "
			    "with unknown d_id %x:%x:%x\n", vha->vp_idx,
			    atio->u.isp24.fcp_hdr.d_id[0],
			    atio->u.isp24.fcp_hdr.d_id[1],
			    atio->u.isp24.fcp_hdr.d_id[2]);


			qlt_queue_unknown_atio(vha, atio, ha_locked);
			break;
		}
		if (unlikely(!list_empty(&vha->unknown_atio_list)))
			qlt_try_to_dequeue_unknown_atios(vha, ha_locked);

		qlt_24xx_atio_pkt(host, atio, ha_locked);
		break;
	}

	case IMMED_NOTIFY_TYPE:
	{
		struct scsi_qla_host *host = vha;
		struct imm_ntfy_from_isp *entry =
		    (struct imm_ntfy_from_isp *)atio;

		qlt_issue_marker(vha, ha_locked);

		if ((entry->u.isp24.vp_index != 0xFF) &&
		    (entry->u.isp24.nport_handle != 0xFFFF)) {
			host = qlt_find_host_by_vp_idx(vha,
			    entry->u.isp24.vp_index);
			if (unlikely(!host)) {
				ql_dbg(ql_dbg_tgt, vha, 0xe03f,
				    "qla_target(%d): Received "
				    "ATIO (IMMED_NOTIFY_TYPE) "
				    "with unknown vp_index %d\n",
				    vha->vp_idx, entry->u.isp24.vp_index);
				break;
			}
		}
		qlt_24xx_atio_pkt(host, atio, ha_locked);
		break;
	}

	case VP_RPT_ID_IOCB_TYPE:
		qla24xx_report_id_acquisition(vha,
			(struct vp_rpt_id_entry_24xx *)atio);
		break;

	case ABTS_RECV_24XX:
	{
		struct abts_recv_from_24xx *entry =
			(struct abts_recv_from_24xx *)atio;
		struct scsi_qla_host *host = qlt_find_host_by_vp_idx(vha,
			entry->vp_index);
		unsigned long flags;

		if (unlikely(!host)) {
			ql_dbg(ql_dbg_tgt, vha, 0xe00a,
			    "qla_target(%d): Response pkt (ABTS_RECV_24XX) "
			    "received, with unknown vp_index %d\n",
			    vha->vp_idx, entry->vp_index);
			break;
		}
		if (!ha_locked)
			spin_lock_irqsave(&host->hw->hardware_lock, flags);
		qlt_24xx_handle_abts(host, (struct abts_recv_from_24xx *)atio);
		if (!ha_locked)
			spin_unlock_irqrestore(&host->hw->hardware_lock, flags);
		break;
	}

	/* case PUREX_IOCB_TYPE: ql2xmvasynctoatio */

	default:
		ql_dbg(ql_dbg_tgt, vha, 0xe040,
		    "qla_target(%d): Received unknown ATIO atio "
		    "type %x\n", vha->vp_idx, atio->u.raw.entry_type);
		break;
	}

	return false;
}

void qlt_response_pkt_all_vps(struct scsi_qla_host *vha,
	struct rsp_que *rsp, response_t *pkt)
{
	switch (pkt->entry_type) {
	case CTIO_CRC2:
		ql_dbg(ql_dbg_tgt, vha, 0xe073,
			"qla_target(%d):%s: CRC2 Response pkt\n",
			vha->vp_idx, __func__);
		/* fall through */
	case CTIO_TYPE7:
	{
		struct ctio7_from_24xx *entry = (struct ctio7_from_24xx *)pkt;
		struct scsi_qla_host *host = qlt_find_host_by_vp_idx(vha,
		    entry->vp_index);
		if (unlikely(!host)) {
			ql_dbg(ql_dbg_tgt, vha, 0xe041,
			    "qla_target(%d): Response pkt (CTIO_TYPE7) "
			    "received, with unknown vp_index %d\n",
			    vha->vp_idx, entry->vp_index);
			break;
		}
		qlt_response_pkt(host, rsp, pkt);
		break;
	}

	case IMMED_NOTIFY_TYPE:
	{
		struct scsi_qla_host *host = vha;
		struct imm_ntfy_from_isp *entry =
		    (struct imm_ntfy_from_isp *)pkt;

		host = qlt_find_host_by_vp_idx(vha, entry->u.isp24.vp_index);
		if (unlikely(!host)) {
			ql_dbg(ql_dbg_tgt, vha, 0xe042,
			    "qla_target(%d): Response pkt (IMMED_NOTIFY_TYPE) "
			    "received, with unknown vp_index %d\n",
			    vha->vp_idx, entry->u.isp24.vp_index);
			break;
		}
		qlt_response_pkt(host, rsp, pkt);
		break;
	}

	case NOTIFY_ACK_TYPE:
	{
		struct scsi_qla_host *host = vha;
		struct nack_to_isp *entry = (struct nack_to_isp *)pkt;

		if (0xFF != entry->u.isp24.vp_index) {
			host = qlt_find_host_by_vp_idx(vha,
			    entry->u.isp24.vp_index);
			if (unlikely(!host)) {
				ql_dbg(ql_dbg_tgt, vha, 0xe043,
				    "qla_target(%d): Response "
				    "pkt (NOTIFY_ACK_TYPE) "
				    "received, with unknown "
				    "vp_index %d\n", vha->vp_idx,
				    entry->u.isp24.vp_index);
				break;
			}
		}
		qlt_response_pkt(host, rsp, pkt);
		break;
	}

	case ABTS_RECV_24XX:
	{
		struct abts_recv_from_24xx *entry =
		    (struct abts_recv_from_24xx *)pkt;
		struct scsi_qla_host *host = qlt_find_host_by_vp_idx(vha,
		    entry->vp_index);
		if (unlikely(!host)) {
			ql_dbg(ql_dbg_tgt, vha, 0xe044,
			    "qla_target(%d): Response pkt "
			    "(ABTS_RECV_24XX) received, with unknown "
			    "vp_index %d\n", vha->vp_idx, entry->vp_index);
			break;
		}
		qlt_response_pkt(host, rsp, pkt);
		break;
	}

	case ABTS_RESP_24XX:
	{
		struct abts_resp_to_24xx *entry =
		    (struct abts_resp_to_24xx *)pkt;
		struct scsi_qla_host *host = qlt_find_host_by_vp_idx(vha,
		    entry->vp_index);
		if (unlikely(!host)) {
			ql_dbg(ql_dbg_tgt, vha, 0xe045,
			    "qla_target(%d): Response pkt "
			    "(ABTS_RECV_24XX) received, with unknown "
			    "vp_index %d\n", vha->vp_idx, entry->vp_index);
			break;
		}
		qlt_response_pkt(host, rsp, pkt);
		break;
	}

	default:
		qlt_response_pkt(vha, rsp, pkt);
		break;
	}

}

/*
 * All qlt_plogi_ack_t operations are protected by hardware_lock
 */
static int qla24xx_post_nack_work(struct scsi_qla_host *vha, fc_port_t *fcport,
	struct imm_ntfy_from_isp *ntfy, int type)
{
	struct qla_work_evt *e;
	e = qla2x00_alloc_work(vha, QLA_EVT_NACK);
	if (!e)
		return QLA_FUNCTION_FAILED;

	e->u.nack.fcport = fcport;
	e->u.nack.type = type;
	memcpy(e->u.nack.iocb, ntfy, sizeof(struct imm_ntfy_from_isp));
	return qla2x00_post_work(vha, e);
}

static
void qla2x00_async_nack_sp_done(void *s, int res)
{
	struct srb *sp = (struct srb *)s;
	struct scsi_qla_host *vha = sp->vha;
	unsigned long flags;

	ql_dbg(ql_dbg_disc, vha, 0x20f2,
	    "Async done-%s res %x %8phC  type %d\n",
	    sp->name, res, sp->fcport->port_name, sp->type);

	spin_lock_irqsave(&vha->hw->tgt.sess_lock, flags);
	sp->fcport->flags &= ~FCF_ASYNC_SENT;
	sp->fcport->chip_reset = vha->hw->base_qpair->chip_reset;

	switch (sp->type) {
	case SRB_NACK_PLOGI:
		sp->fcport->login_gen++;
		sp->fcport->fw_login_state = DSC_LS_PLOGI_COMP;
		sp->fcport->logout_on_delete = 1;
		sp->fcport->plogi_nack_done_deadline = jiffies + HZ;
		sp->fcport->send_els_logo = 0;
		break;

	case SRB_NACK_PRLI:
		sp->fcport->fw_login_state = DSC_LS_PRLI_COMP;
		sp->fcport->deleted = 0;
		sp->fcport->send_els_logo = 0;

		if (!sp->fcport->login_succ &&
		    !IS_SW_RESV_ADDR(sp->fcport->d_id)) {
			sp->fcport->login_succ = 1;

			vha->fcport_count++;

			if (!IS_IIDMA_CAPABLE(vha->hw) ||
			    !vha->hw->flags.gpsc_supported) {
				ql_dbg(ql_dbg_disc, vha, 0x20f3,
				    "%s %d %8phC post upd_fcport fcp_cnt %d\n",
				    __func__, __LINE__,
				    sp->fcport->port_name,
				    vha->fcport_count);

				qla24xx_post_upd_fcport_work(vha, sp->fcport);
			} else {
				ql_dbg(ql_dbg_disc, vha, 0x20f5,
				    "%s %d %8phC post gpsc fcp_cnt %d\n",
				    __func__, __LINE__,
				    sp->fcport->port_name,
				    vha->fcport_count);

				qla24xx_post_gpsc_work(vha, sp->fcport);
			}
		}
		break;

	case SRB_NACK_LOGO:
		sp->fcport->login_gen++;
		sp->fcport->fw_login_state = DSC_LS_PORT_UNAVAIL;
		qlt_logo_completion_handler(sp->fcport, MBS_COMMAND_COMPLETE);
		break;
	}
	spin_unlock_irqrestore(&vha->hw->tgt.sess_lock, flags);

	sp->free(sp);
}

int qla24xx_async_notify_ack(scsi_qla_host_t *vha, fc_port_t *fcport,
	struct imm_ntfy_from_isp *ntfy, int type)
{
	int rval = QLA_FUNCTION_FAILED;
	srb_t *sp;
	char *c = NULL;

	fcport->flags |= FCF_ASYNC_SENT;
	switch (type) {
	case SRB_NACK_PLOGI:
		fcport->fw_login_state = DSC_LS_PLOGI_PEND;
		c = "PLOGI";
		break;
	case SRB_NACK_PRLI:
		fcport->fw_login_state = DSC_LS_PRLI_PEND;
		fcport->deleted = 0;
		c = "PRLI";
		break;
	case SRB_NACK_LOGO:
		fcport->fw_login_state = DSC_LS_LOGO_PEND;
		c = "LOGO";
		break;
	}

	sp = qla2x00_get_sp(vha, fcport, GFP_ATOMIC);
	if (!sp)
		goto done;

	sp->type = type;
	sp->name = "nack";

	qla2x00_init_timer(sp, qla2x00_get_async_timeout(vha)+2);

	sp->u.iocb_cmd.u.nack.ntfy = ntfy;
	sp->u.iocb_cmd.timeout = qla2x00_async_iocb_timeout;
	sp->done = qla2x00_async_nack_sp_done;

	rval = qla2x00_start_sp(sp);
	if (rval != QLA_SUCCESS)
		goto done_free_sp;

	ql_dbg(ql_dbg_disc, vha, 0x20f4,
	    "Async-%s %8phC hndl %x %s\n",
	    sp->name, fcport->port_name, sp->handle, c);

	return rval;

done_free_sp:
	sp->free(sp);
done:
	fcport->flags &= ~FCF_ASYNC_SENT;
	return rval;
}

void qla24xx_do_nack_work(struct scsi_qla_host *vha, struct qla_work_evt *e)
{
	fc_port_t *t;
	unsigned long flags;

	switch (e->u.nack.type) {
	case SRB_NACK_PRLI:
		mutex_lock(&vha->vha_tgt.tgt_mutex);
		t = qlt_create_sess(vha, e->u.nack.fcport, 0);
		mutex_unlock(&vha->vha_tgt.tgt_mutex);
		if (t) {
			ql_log(ql_log_info, vha, 0xd034,
			    "%s create sess success %p", __func__, t);
			spin_lock_irqsave(&vha->hw->tgt.sess_lock, flags);
			/* create sess has an extra kref */
			vha->hw->tgt.tgt_ops->put_sess(e->u.nack.fcport);
			spin_unlock_irqrestore(&vha->hw->tgt.sess_lock, flags);
		}
		break;
	}
	qla24xx_async_notify_ack(vha, e->u.nack.fcport,
	    (struct imm_ntfy_from_isp*)e->u.nack.iocb, e->u.nack.type);
}

void qla24xx_delete_sess_fn(struct work_struct *work)
{
	fc_port_t *fcport = container_of(work, struct fc_port, del_work);
	struct qla_hw_data *ha = fcport->vha->hw;
	unsigned long flags;

	spin_lock_irqsave(&ha->tgt.sess_lock, flags);

	if (fcport->se_sess) {
		ha->tgt.tgt_ops->shutdown_sess(fcport);
		ha->tgt.tgt_ops->put_sess(fcport);
	} else {
		qlt_unreg_sess(fcport);
	}
	spin_unlock_irqrestore(&ha->tgt.sess_lock, flags);
}

/*
 * Called from qla2x00_reg_remote_port()
 */
void qlt_fc_port_added(struct scsi_qla_host *vha, fc_port_t *fcport)
{
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt *tgt = vha->vha_tgt.qla_tgt;
	struct fc_port *sess = fcport;
	unsigned long flags;

	if (!vha->hw->tgt.tgt_ops)
		return;

	spin_lock_irqsave(&ha->tgt.sess_lock, flags);
	if (tgt->tgt_stop) {
		spin_unlock_irqrestore(&ha->tgt.sess_lock, flags);
		return;
	}

	if (fcport->disc_state == DSC_DELETE_PEND) {
		spin_unlock_irqrestore(&ha->tgt.sess_lock, flags);
		return;
	}

	if (!sess->se_sess) {
		spin_unlock_irqrestore(&ha->tgt.sess_lock, flags);

		mutex_lock(&vha->vha_tgt.tgt_mutex);
		sess = qlt_create_sess(vha, fcport, false);
		mutex_unlock(&vha->vha_tgt.tgt_mutex);

		spin_lock_irqsave(&ha->tgt.sess_lock, flags);
	} else {
		if (fcport->fw_login_state == DSC_LS_PRLI_COMP) {
			spin_unlock_irqrestore(&ha->tgt.sess_lock, flags);
			return;
		}

		if (!kref_get_unless_zero(&sess->sess_kref)) {
			ql_dbg(ql_dbg_disc, vha, 0x2107,
			    "%s: kref_get fail sess %8phC \n",
			    __func__, sess->port_name);
			spin_unlock_irqrestore(&ha->tgt.sess_lock, flags);
			return;
		}

		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf04c,
		    "qla_target(%u): %ssession for port %8phC "
		    "(loop ID %d) reappeared\n", vha->vp_idx,
		    sess->local ? "local " : "", sess->port_name, sess->loop_id);

		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf007,
		    "Reappeared sess %p\n", sess);

		ha->tgt.tgt_ops->update_sess(sess, fcport->d_id,
		    fcport->loop_id,
		    (fcport->flags & FCF_CONF_COMP_SUPPORTED));
	}

	if (sess && sess->local) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf04d,
		    "qla_target(%u): local session for "
		    "port %8phC (loop ID %d) became global\n", vha->vp_idx,
		    fcport->port_name, sess->loop_id);
		sess->local = 0;
	}
	ha->tgt.tgt_ops->put_sess(sess);
	spin_unlock_irqrestore(&ha->tgt.sess_lock, flags);
}

/*
 * This is a zero-base ref-counting solution, since hardware_lock
 * guarantees that ref_count is not modified concurrently.
 * Upon successful return content of iocb is undefined
 */
static struct qlt_plogi_ack_t *
qlt_plogi_ack_find_add(struct scsi_qla_host *vha, port_id_t *id,
		       struct imm_ntfy_from_isp *iocb)
{
	struct qlt_plogi_ack_t *pla;

	list_for_each_entry(pla, &vha->plogi_ack_list, list) {
		if (pla->id.b24 == id->b24) {
			qlt_send_term_imm_notif(vha, &pla->iocb, 1);
			memcpy(&pla->iocb, iocb, sizeof(pla->iocb));
			return pla;
		}
	}

	pla = kmem_cache_zalloc(qla_tgt_plogi_cachep, GFP_ATOMIC);
	if (!pla) {
		ql_dbg(ql_dbg_async, vha, 0x5088,
		       "qla_target(%d): Allocation of plogi_ack failed\n",
		       vha->vp_idx);
		return NULL;
	}

	memcpy(&pla->iocb, iocb, sizeof(pla->iocb));
	pla->id = *id;
	list_add_tail(&pla->list, &vha->plogi_ack_list);

	return pla;
}

void qlt_plogi_ack_unref(struct scsi_qla_host *vha,
    struct qlt_plogi_ack_t *pla)
{
	struct imm_ntfy_from_isp *iocb = &pla->iocb;
	port_id_t port_id;
	uint16_t loop_id;
	fc_port_t *fcport = pla->fcport;

	BUG_ON(!pla->ref_count);
	pla->ref_count--;

	if (pla->ref_count)
		return;

	ql_dbg(ql_dbg_disc, vha, 0x5089,
	    "Sending PLOGI ACK to wwn %8phC s_id %02x:%02x:%02x loop_id %#04x"
	    " exch %#x ox_id %#x\n", iocb->u.isp24.port_name,
	    iocb->u.isp24.port_id[2], iocb->u.isp24.port_id[1],
	    iocb->u.isp24.port_id[0],
	    le16_to_cpu(iocb->u.isp24.nport_handle),
	    iocb->u.isp24.exchange_address, iocb->ox_id);

	port_id.b.domain = iocb->u.isp24.port_id[2];
	port_id.b.area   = iocb->u.isp24.port_id[1];
	port_id.b.al_pa  = iocb->u.isp24.port_id[0];
	port_id.b.rsvd_1 = 0;

	loop_id = le16_to_cpu(iocb->u.isp24.nport_handle);

	fcport->loop_id = loop_id;
	fcport->d_id = port_id;
	qla24xx_post_nack_work(vha, fcport, iocb, SRB_NACK_PLOGI);

	list_for_each_entry(fcport, &vha->vp_fcports, list) {
		if (fcport->plogi_link[QLT_PLOGI_LINK_SAME_WWN] == pla)
			fcport->plogi_link[QLT_PLOGI_LINK_SAME_WWN] = NULL;
		if (fcport->plogi_link[QLT_PLOGI_LINK_CONFLICT] == pla)
			fcport->plogi_link[QLT_PLOGI_LINK_CONFLICT] = NULL;
	}

	list_del(&pla->list);
	kmem_cache_free(qla_tgt_plogi_cachep, pla);
}

void
qlt_plogi_ack_link(struct scsi_qla_host *vha, struct qlt_plogi_ack_t *pla,
    struct fc_port *sess, enum qlt_plogi_link_t link)
{
	struct imm_ntfy_from_isp *iocb = &pla->iocb;
	/* Inc ref_count first because link might already be pointing at pla */
	pla->ref_count++;

	ql_dbg(ql_dbg_tgt_mgt, vha, 0xf097,
		"Linking sess %p [%d] wwn %8phC with PLOGI ACK to wwn %8phC"
		" s_id %02x:%02x:%02x, ref=%d pla %p link %d\n",
		sess, link, sess->port_name,
		iocb->u.isp24.port_name, iocb->u.isp24.port_id[2],
		iocb->u.isp24.port_id[1], iocb->u.isp24.port_id[0],
		pla->ref_count, pla, link);

	if (link == QLT_PLOGI_LINK_CONFLICT) {
		switch (sess->disc_state) {
		case DSC_DELETED:
		case DSC_DELETE_PEND:
			pla->ref_count--;
			return;
		default:
			break;
		}
	}

	if (sess->plogi_link[link])
		qlt_plogi_ack_unref(vha, sess->plogi_link[link]);

	if (link == QLT_PLOGI_LINK_SAME_WWN)
		pla->fcport = sess;

	sess->plogi_link[link] = pla;
}

typedef struct {
	/* These fields must be initialized by the caller */
	port_id_t id;
	/*
	 * number of cmds dropped while we were waiting for
	 * initiator to ack LOGO initialize to 1 if LOGO is
	 * triggered by a command, otherwise, to 0
	 */
	int cmd_count;

	/* These fields are used by callee */
	struct list_head list;
} qlt_port_logo_t;

static void
qlt_send_first_logo(struct scsi_qla_host *vha, qlt_port_logo_t *logo)
{
	qlt_port_logo_t *tmp;
	int res;

	mutex_lock(&vha->vha_tgt.tgt_mutex);

	list_for_each_entry(tmp, &vha->logo_list, list) {
		if (tmp->id.b24 == logo->id.b24) {
			tmp->cmd_count += logo->cmd_count;
			mutex_unlock(&vha->vha_tgt.tgt_mutex);
			return;
		}
	}

	list_add_tail(&logo->list, &vha->logo_list);

	mutex_unlock(&vha->vha_tgt.tgt_mutex);

	res = qla24xx_els_dcmd_iocb(vha, ELS_DCMD_LOGO, logo->id);

	mutex_lock(&vha->vha_tgt.tgt_mutex);
	list_del(&logo->list);
	mutex_unlock(&vha->vha_tgt.tgt_mutex);

	ql_dbg(ql_dbg_tgt_mgt, vha, 0xf098,
	    "Finished LOGO to %02x:%02x:%02x, dropped %d cmds, res = %#x\n",
	    logo->id.b.domain, logo->id.b.area, logo->id.b.al_pa,
	    logo->cmd_count, res);
}

static void qlt_free_session_done(struct work_struct *work)
{
	struct fc_port *sess = container_of(work, struct fc_port,
	    free_work);
	struct qla_tgt *tgt = sess->tgt;
	struct scsi_qla_host *vha = sess->vha;
	struct qla_hw_data *ha = vha->hw;
	unsigned long flags;
	bool logout_started = false;
	struct event_arg ea;
	scsi_qla_host_t *base_vha;

	ql_dbg(ql_dbg_tgt_mgt, vha, 0xf084,
		"%s: se_sess %p / sess %p from port %8phC loop_id %#04x"
		" s_id %02x:%02x:%02x logout %d keep %d els_logo %d\n",
		__func__, sess->se_sess, sess, sess->port_name, sess->loop_id,
		sess->d_id.b.domain, sess->d_id.b.area, sess->d_id.b.al_pa,
		sess->logout_on_delete, sess->keep_nport_handle,
		sess->send_els_logo);

	if (!IS_SW_RESV_ADDR(sess->d_id)) {
		if (sess->send_els_logo) {
			qlt_port_logo_t logo;

			logo.id = sess->d_id;
			logo.cmd_count = 0;
			qlt_send_first_logo(vha, &logo);
		}

		if (sess->logout_on_delete && sess->loop_id != FC_NO_LOOP_ID) {
			int rc;

			rc = qla2x00_post_async_logout_work(vha, sess, NULL);
			if (rc != QLA_SUCCESS)
				ql_log(ql_log_warn, vha, 0xf085,
				    "Schedule logo failed sess %p rc %d\n",
				    sess, rc);
			else
				logout_started = true;
		}
	}

	/*
	 * Release the target session for FC Nexus from fabric module code.
	 */
	if (sess->se_sess != NULL)
		ha->tgt.tgt_ops->free_session(sess);

	if (logout_started) {
		bool traced = false;

		while (!READ_ONCE(sess->logout_completed)) {
			if (!traced) {
				ql_dbg(ql_dbg_tgt_mgt, vha, 0xf086,
					"%s: waiting for sess %p logout\n",
					__func__, sess);
				traced = true;
			}
			msleep(100);
		}

		ql_dbg(ql_dbg_disc, vha, 0xf087,
		    "%s: sess %p logout completed\n",__func__, sess);
	}

	if (sess->logo_ack_needed) {
		sess->logo_ack_needed = 0;
		qla24xx_async_notify_ack(vha, sess,
			(struct imm_ntfy_from_isp *)sess->iocb, SRB_NACK_LOGO);
	}

	spin_lock_irqsave(&ha->tgt.sess_lock, flags);
	if (sess->se_sess) {
		sess->se_sess = NULL;
		if (tgt && !IS_SW_RESV_ADDR(sess->d_id))
			tgt->sess_count--;
	}

	sess->disc_state = DSC_DELETED;
	sess->fw_login_state = DSC_LS_PORT_UNAVAIL;
	sess->deleted = QLA_SESS_DELETED;
	sess->login_retry = vha->hw->login_retry_count;

	if (sess->login_succ && !IS_SW_RESV_ADDR(sess->d_id)) {
		vha->fcport_count--;
		sess->login_succ = 0;
	}

	qla2x00_clear_loop_id(sess);

	if (sess->conflict) {
		sess->conflict->login_pause = 0;
		sess->conflict = NULL;
		if (!test_bit(UNLOADING, &vha->dpc_flags))
			set_bit(RELOGIN_NEEDED, &vha->dpc_flags);
	}

	{
		struct qlt_plogi_ack_t *own =
		    sess->plogi_link[QLT_PLOGI_LINK_SAME_WWN];
		struct qlt_plogi_ack_t *con =
		    sess->plogi_link[QLT_PLOGI_LINK_CONFLICT];
		struct imm_ntfy_from_isp *iocb;

		if (con) {
			iocb = &con->iocb;
			ql_dbg(ql_dbg_tgt_mgt, vha, 0xf099,
				 "se_sess %p / sess %p port %8phC is gone,"
				 " %s (ref=%d), releasing PLOGI for %8phC (ref=%d)\n",
				 sess->se_sess, sess, sess->port_name,
				 own ? "releasing own PLOGI" : "no own PLOGI pending",
				 own ? own->ref_count : -1,
				 iocb->u.isp24.port_name, con->ref_count);
			qlt_plogi_ack_unref(vha, con);
			sess->plogi_link[QLT_PLOGI_LINK_CONFLICT] = NULL;
		} else {
			ql_dbg(ql_dbg_tgt_mgt, vha, 0xf09a,
			    "se_sess %p / sess %p port %8phC is gone, %s (ref=%d)\n",
			    sess->se_sess, sess, sess->port_name,
			    own ? "releasing own PLOGI" :
			    "no own PLOGI pending",
			    own ? own->ref_count : -1);
		}

		if (own) {
			sess->fw_login_state = DSC_LS_PLOGI_PEND;
			qlt_plogi_ack_unref(vha, own);
			sess->plogi_link[QLT_PLOGI_LINK_SAME_WWN] = NULL;
		}
	}
	spin_unlock_irqrestore(&ha->tgt.sess_lock, flags);

	ql_dbg(ql_dbg_tgt_mgt, vha, 0xf001,
	    "Unregistration of sess %p %8phC finished fcp_cnt %d\n",
		sess, sess->port_name, vha->fcport_count);

	if (tgt && (tgt->sess_count == 0))
		wake_up_all(&tgt->waitQ);

	if (vha->fcport_count == 0)
		wake_up_all(&vha->fcport_waitQ);

	base_vha = pci_get_drvdata(ha->pdev);
	if (test_bit(PFLG_DRIVER_REMOVING, &base_vha->pci_flags))
		return;

	if (!tgt || !tgt->tgt_stop) {
		memset(&ea, 0, sizeof(ea));
		ea.event = FCME_DELETE_DONE;
		ea.fcport = sess;
		qla2x00_fcport_event_handler(vha, &ea);
	}
}

/* ha->tgt.sess_lock supposed to be held on entry */
void qlt_unreg_sess(struct fc_port *sess)
{
	struct scsi_qla_host *vha = sess->vha;

	ql_dbg(ql_dbg_disc, sess->vha, 0x210a,
	    "%s sess %p for deletion %8phC\n",
	    __func__, sess, sess->port_name);

	if (sess->se_sess)
		vha->hw->tgt.tgt_ops->clear_nacl_from_fcport_map(sess);

	qla2x00_mark_device_lost(vha, sess, 1, 1);

	sess->deleted = QLA_SESS_DELETION_IN_PROGRESS;
	sess->disc_state = DSC_DELETE_PEND;
	sess->last_rscn_gen = sess->rscn_gen;
	sess->last_login_gen = sess->login_gen;

	if (sess->nvme_flag & NVME_FLAG_REGISTERED)
		schedule_work(&sess->nvme_del_work);

	INIT_WORK(&sess->free_work, qlt_free_session_done);
	schedule_work(&sess->free_work);
}
EXPORT_SYMBOL(qlt_unreg_sess);

static int qlt_reset(struct scsi_qla_host *vha, void *iocb, int mcmd)
{
	struct qla_hw_data *ha = vha->hw;
	struct fc_port *sess = NULL;
	uint16_t loop_id;
	int res = 0;
	struct imm_ntfy_from_isp *n = (struct imm_ntfy_from_isp *)iocb;
	unsigned long flags;

	loop_id = le16_to_cpu(n->u.isp24.nport_handle);
	if (loop_id == 0xFFFF) {
		/* Global event */
		atomic_inc(&vha->vha_tgt.qla_tgt->tgt_global_resets_count);
		spin_lock_irqsave(&ha->tgt.sess_lock, flags);
		qlt_clear_tgt_db(vha->vha_tgt.qla_tgt);
		spin_unlock_irqrestore(&ha->tgt.sess_lock, flags);
	} else {
		spin_lock_irqsave(&ha->tgt.sess_lock, flags);
		sess = ha->tgt.tgt_ops->find_sess_by_loop_id(vha, loop_id);
		spin_unlock_irqrestore(&ha->tgt.sess_lock, flags);
	}

	ql_dbg(ql_dbg_tgt, vha, 0xe000,
	    "Using sess for qla_tgt_reset: %p\n", sess);
	if (!sess) {
		res = -ESRCH;
		return res;
	}

	ql_dbg(ql_dbg_tgt, vha, 0xe047,
	    "scsi(%ld): resetting (session %p from port %8phC mcmd %x, "
	    "loop_id %d)\n", vha->host_no, sess, sess->port_name,
	    mcmd, loop_id);

	return qlt_issue_task_mgmt(sess, 0, mcmd, iocb, QLA24XX_MGMT_SEND_NACK);
}

static void qla24xx_chk_fcp_state(struct fc_port *sess)
{
	if (sess->chip_reset != sess->vha->hw->base_qpair->chip_reset) {
		sess->logout_on_delete = 0;
		sess->logo_ack_needed = 0;
		sess->fw_login_state = DSC_LS_PORT_UNAVAIL;
		sess->scan_state = 0;
	}
}

/* ha->tgt.sess_lock supposed to be held on entry */
void qlt_schedule_sess_for_deletion(struct fc_port *sess,
	bool immediate)
{
	struct qla_tgt *tgt = sess->tgt;

	if (sess->disc_state == DSC_DELETE_PEND)
		return;

	if (sess->disc_state == DSC_DELETED) {
		if (tgt && tgt->tgt_stop && (tgt->sess_count == 0))
			wake_up_all(&tgt->waitQ);
		if (sess->vha->fcport_count == 0)
			wake_up_all(&sess->vha->fcport_waitQ);

		if (!sess->plogi_link[QLT_PLOGI_LINK_SAME_WWN] &&
			!sess->plogi_link[QLT_PLOGI_LINK_CONFLICT])
			return;
	}

	sess->disc_state = DSC_DELETE_PEND;

	if (sess->deleted == QLA_SESS_DELETED)
		sess->logout_on_delete = 0;

	sess->deleted = QLA_SESS_DELETION_IN_PROGRESS;
	qla24xx_chk_fcp_state(sess);

	ql_dbg(ql_dbg_tgt, sess->vha, 0xe001,
	    "Scheduling sess %p for deletion\n", sess);

	INIT_WORK(&sess->del_work, qla24xx_delete_sess_fn);
	queue_work(sess->vha->hw->wq, &sess->del_work);
}

void qlt_schedule_sess_for_deletion_lock(struct fc_port *sess)
{
	unsigned long flags;
	struct qla_hw_data *ha = sess->vha->hw;
	spin_lock_irqsave(&ha->tgt.sess_lock, flags);
	qlt_schedule_sess_for_deletion(sess, 1);
	spin_unlock_irqrestore(&ha->tgt.sess_lock, flags);
}

/* ha->tgt.sess_lock supposed to be held on entry */
static void qlt_clear_tgt_db(struct qla_tgt *tgt)
{
	struct fc_port *sess;
	scsi_qla_host_t *vha = tgt->vha;

	list_for_each_entry(sess, &vha->vp_fcports, list) {
		if (sess->se_sess)
			qlt_schedule_sess_for_deletion(sess, 1);
	}

	/* At this point tgt could be already dead */
}

static int qla24xx_get_loop_id(struct scsi_qla_host *vha, const uint8_t *s_id,
	uint16_t *loop_id)
{
	struct qla_hw_data *ha = vha->hw;
	dma_addr_t gid_list_dma;
	struct gid_list_info *gid_list;
	char *id_iter;
	int res, rc, i;
	uint16_t entries;

	gid_list = dma_alloc_coherent(&ha->pdev->dev, qla2x00_gid_list_size(ha),
	    &gid_list_dma, GFP_KERNEL);
	if (!gid_list) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf044,
		    "qla_target(%d): DMA Alloc failed of %u\n",
		    vha->vp_idx, qla2x00_gid_list_size(ha));
		return -ENOMEM;
	}

	/* Get list of logged in devices */
	rc = qla24xx_gidlist_wait(vha, gid_list, gid_list_dma, &entries);
	if (rc != QLA_SUCCESS) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf045,
		    "qla_target(%d): get_id_list() failed: %x\n",
		    vha->vp_idx, rc);
		res = -EBUSY;
		goto out_free_id_list;
	}

	id_iter = (char *)gid_list;
	res = -ENOENT;
	for (i = 0; i < entries; i++) {
		struct gid_list_info *gid = (struct gid_list_info *)id_iter;
		if ((gid->al_pa == s_id[2]) &&
		    (gid->area == s_id[1]) &&
		    (gid->domain == s_id[0])) {
			*loop_id = le16_to_cpu(gid->loop_id);
			res = 0;
			break;
		}
		id_iter += ha->gid_list_info_size;
	}

out_free_id_list:
	dma_free_coherent(&ha->pdev->dev, qla2x00_gid_list_size(ha),
	    gid_list, gid_list_dma);
	return res;
}

/*
 * Adds an extra ref to allow to drop hw lock after adding sess to the list.
 * Caller must put it.
 */
static struct fc_port *qlt_create_sess(
	struct scsi_qla_host *vha,
	fc_port_t *fcport,
	bool local)
{
	struct qla_hw_data *ha = vha->hw;
	struct fc_port *sess = fcport;
	unsigned long flags;

	if (vha->vha_tgt.qla_tgt->tgt_stop)
		return NULL;

	if (fcport->se_sess) {
		if (!kref_get_unless_zero(&sess->sess_kref)) {
			ql_dbg(ql_dbg_disc, vha, 0x20f6,
			    "%s: kref_get_unless_zero failed for %8phC\n",
			    __func__, sess->port_name);
			return NULL;
		}
		return fcport;
	}
	sess->tgt = vha->vha_tgt.qla_tgt;
	sess->local = local;

	/*
	 * Under normal circumstances we want to logout from firmware when
	 * session eventually ends and release corresponding nport handle.
	 * In the exception cases (e.g. when new PLOGI is waiting) corresponding
	 * code will adjust these flags as necessary.
	 */
	sess->logout_on_delete = 1;
	sess->keep_nport_handle = 0;
	sess->logout_completed = 0;

	if (ha->tgt.tgt_ops->check_initiator_node_acl(vha,
	    &fcport->port_name[0], sess) < 0) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf015,
		    "(%d) %8phC check_initiator_node_acl failed\n",
		    vha->vp_idx, fcport->port_name);
		return NULL;
	} else {
		kref_init(&fcport->sess_kref);
		/*
		 * Take an extra reference to ->sess_kref here to handle
		 * fc_port access across ->tgt.sess_lock reaquire.
		 */
		if (!kref_get_unless_zero(&sess->sess_kref)) {
			ql_dbg(ql_dbg_disc, vha, 0x20f7,
			    "%s: kref_get_unless_zero failed for %8phC\n",
			    __func__, sess->port_name);
			return NULL;
		}

		spin_lock_irqsave(&ha->tgt.sess_lock, flags);
		if (!IS_SW_RESV_ADDR(sess->d_id))
			vha->vha_tgt.qla_tgt->sess_count++;

		qlt_do_generation_tick(vha, &sess->generation);
		spin_unlock_irqrestore(&ha->tgt.sess_lock, flags);
	}

	ql_dbg(ql_dbg_tgt_mgt, vha, 0xf006,
	    "Adding sess %p se_sess %p  to tgt %p sess_count %d\n",
	    sess, sess->se_sess, vha->vha_tgt.qla_tgt,
	    vha->vha_tgt.qla_tgt->sess_count);

	ql_dbg(ql_dbg_tgt_mgt, vha, 0xf04b,
	    "qla_target(%d): %ssession for wwn %8phC (loop_id %d, "
	    "s_id %x:%x:%x, confirmed completion %ssupported) added\n",
	    vha->vp_idx, local ?  "local " : "", fcport->port_name,
	    fcport->loop_id, sess->d_id.b.domain, sess->d_id.b.area,
	    sess->d_id.b.al_pa, sess->conf_compl_supported ?  "" : "not ");

	return sess;
}

/*
 * max_gen - specifies maximum session generation
 * at which this deletion requestion is still valid
 */
void
qlt_fc_port_deleted(struct scsi_qla_host *vha, fc_port_t *fcport, int max_gen)
{
	struct qla_tgt *tgt = vha->vha_tgt.qla_tgt;
	struct fc_port *sess = fcport;
	unsigned long flags;

	if (!vha->hw->tgt.tgt_ops)
		return;

	if (!tgt)
		return;

	spin_lock_irqsave(&vha->hw->tgt.sess_lock, flags);
	if (tgt->tgt_stop) {
		spin_unlock_irqrestore(&vha->hw->tgt.sess_lock, flags);
		return;
	}
	if (!sess->se_sess) {
		spin_unlock_irqrestore(&vha->hw->tgt.sess_lock, flags);
		return;
	}

	if (max_gen - sess->generation < 0) {
		spin_unlock_irqrestore(&vha->hw->tgt.sess_lock, flags);
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf092,
		    "Ignoring stale deletion request for se_sess %p / sess %p"
		    " for port %8phC, req_gen %d, sess_gen %d\n",
		    sess->se_sess, sess, sess->port_name, max_gen,
		    sess->generation);
		return;
	}

	ql_dbg(ql_dbg_tgt_mgt, vha, 0xf008, "qla_tgt_fc_port_deleted %p", sess);

	sess->local = 1;
	qlt_schedule_sess_for_deletion(sess, false);
	spin_unlock_irqrestore(&vha->hw->tgt.sess_lock, flags);
}

static inline int test_tgt_sess_count(struct qla_tgt *tgt)
{
	struct qla_hw_data *ha = tgt->ha;
	unsigned long flags;
	int res;
	/*
	 * We need to protect against race, when tgt is freed before or
	 * inside wake_up()
	 */
	spin_lock_irqsave(&ha->tgt.sess_lock, flags);
	ql_dbg(ql_dbg_tgt, tgt->vha, 0xe002,
	    "tgt %p, sess_count=%d\n",
	    tgt, tgt->sess_count);
	res = (tgt->sess_count == 0);
	spin_unlock_irqrestore(&ha->tgt.sess_lock, flags);

	return res;
}

/* Called by tcm_qla2xxx configfs code */
int qlt_stop_phase1(struct qla_tgt *tgt)
{
	struct scsi_qla_host *vha = tgt->vha;
	struct qla_hw_data *ha = tgt->ha;
	unsigned long flags;

	mutex_lock(&qla_tgt_mutex);
	if (!vha->fc_vport) {
		struct Scsi_Host *sh = vha->host;
		struct fc_host_attrs *fc_host = shost_to_fc_host(sh);
		bool npiv_vports;

		spin_lock_irqsave(sh->host_lock, flags);
		npiv_vports = (fc_host->npiv_vports_inuse);
		spin_unlock_irqrestore(sh->host_lock, flags);

		if (npiv_vports) {
			mutex_unlock(&qla_tgt_mutex);
			ql_dbg(ql_dbg_tgt_mgt, vha, 0xf021,
			    "NPIV is in use. Can not stop target\n");
			return -EPERM;
		}
	}
	if (tgt->tgt_stop || tgt->tgt_stopped) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf04e,
		    "Already in tgt->tgt_stop or tgt_stopped state\n");
		mutex_unlock(&qla_tgt_mutex);
		return -EPERM;
	}

	ql_dbg(ql_dbg_tgt_mgt, vha, 0xe003, "Stopping target for host %ld(%p)\n",
	    vha->host_no, vha);
	/*
	 * Mutex needed to sync with qla_tgt_fc_port_[added,deleted].
	 * Lock is needed, because we still can get an incoming packet.
	 */
	mutex_lock(&vha->vha_tgt.tgt_mutex);
	spin_lock_irqsave(&ha->tgt.sess_lock, flags);
	tgt->tgt_stop = 1;
	qlt_clear_tgt_db(tgt);
	spin_unlock_irqrestore(&ha->tgt.sess_lock, flags);
	mutex_unlock(&vha->vha_tgt.tgt_mutex);
	mutex_unlock(&qla_tgt_mutex);

	ql_dbg(ql_dbg_tgt_mgt, vha, 0xf009,
	    "Waiting for sess works (tgt %p)", tgt);
	spin_lock_irqsave(&tgt->sess_work_lock, flags);
	while (!list_empty(&tgt->sess_works_list)) {
		spin_unlock_irqrestore(&tgt->sess_work_lock, flags);
		flush_scheduled_work();
		spin_lock_irqsave(&tgt->sess_work_lock, flags);
	}
	spin_unlock_irqrestore(&tgt->sess_work_lock, flags);

	ql_dbg(ql_dbg_tgt_mgt, vha, 0xf00a,
	    "Waiting for tgt %p: sess_count=%d\n", tgt, tgt->sess_count);

	wait_event_timeout(tgt->waitQ, test_tgt_sess_count(tgt), 10*HZ);

	/* Big hammer */
	if (!ha->flags.host_shutting_down &&
	    (qla_tgt_mode_enabled(vha) || qla_dual_mode_enabled(vha)))
		qlt_disable_vha(vha);

	/* Wait for sessions to clear out (just in case) */
	wait_event_timeout(tgt->waitQ, test_tgt_sess_count(tgt), 10*HZ);
	return 0;
}
EXPORT_SYMBOL(qlt_stop_phase1);

/* Called by tcm_qla2xxx configfs code */
void qlt_stop_phase2(struct qla_tgt *tgt)
{
	scsi_qla_host_t *vha = tgt->vha;

	if (tgt->tgt_stopped) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf04f,
		    "Already in tgt->tgt_stopped state\n");
		dump_stack();
		return;
	}
	if (!tgt->tgt_stop) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf00b,
		    "%s: phase1 stop is not completed\n", __func__);
		dump_stack();
		return;
	}

	mutex_lock(&vha->vha_tgt.tgt_mutex);
	tgt->tgt_stop = 0;
	tgt->tgt_stopped = 1;
	mutex_unlock(&vha->vha_tgt.tgt_mutex);

	ql_dbg(ql_dbg_tgt_mgt, vha, 0xf00c, "Stop of tgt %p finished\n",
	    tgt);
}
EXPORT_SYMBOL(qlt_stop_phase2);

/* Called from qlt_remove_target() -> qla2x00_remove_one() */
static void qlt_release(struct qla_tgt *tgt)
{
	scsi_qla_host_t *vha = tgt->vha;
	void *node;
	u64 key = 0;
	u16 i;
	struct qla_qpair_hint *h;
	struct qla_hw_data *ha = vha->hw;

	if ((vha->vha_tgt.qla_tgt != NULL) && !tgt->tgt_stop &&
	    !tgt->tgt_stopped)
		qlt_stop_phase1(tgt);

	if ((vha->vha_tgt.qla_tgt != NULL) && !tgt->tgt_stopped)
		qlt_stop_phase2(tgt);

	for (i = 0; i < vha->hw->max_qpairs + 1; i++) {
		unsigned long flags;

		h = &tgt->qphints[i];
		if (h->qpair) {
			spin_lock_irqsave(h->qpair->qp_lock_ptr, flags);
			list_del(&h->hint_elem);
			spin_unlock_irqrestore(h->qpair->qp_lock_ptr, flags);
			h->qpair = NULL;
		}
	}
	kfree(tgt->qphints);
	mutex_lock(&qla_tgt_mutex);
	list_del(&vha->vha_tgt.qla_tgt->tgt_list_entry);
	mutex_unlock(&qla_tgt_mutex);

	btree_for_each_safe64(&tgt->lun_qpair_map, key, node)
		btree_remove64(&tgt->lun_qpair_map, key);

	btree_destroy64(&tgt->lun_qpair_map);

	if (vha->vp_idx)
		if (ha->tgt.tgt_ops &&
		    ha->tgt.tgt_ops->remove_target &&
		    vha->vha_tgt.target_lport_ptr)
			ha->tgt.tgt_ops->remove_target(vha);

	vha->vha_tgt.qla_tgt = NULL;

	ql_dbg(ql_dbg_tgt_mgt, vha, 0xf00d,
	    "Release of tgt %p finished\n", tgt);

	kfree(tgt);
}

/* ha->hardware_lock supposed to be held on entry */
static int qlt_sched_sess_work(struct qla_tgt *tgt, int type,
	const void *param, unsigned int param_size)
{
	struct qla_tgt_sess_work_param *prm;
	unsigned long flags;

	prm = kzalloc(sizeof(*prm), GFP_ATOMIC);
	if (!prm) {
		ql_dbg(ql_dbg_tgt_mgt, tgt->vha, 0xf050,
		    "qla_target(%d): Unable to create session "
		    "work, command will be refused", 0);
		return -ENOMEM;
	}

	ql_dbg(ql_dbg_tgt_mgt, tgt->vha, 0xf00e,
	    "Scheduling work (type %d, prm %p)"
	    " to find session for param %p (size %d, tgt %p)\n",
	    type, prm, param, param_size, tgt);

	prm->type = type;
	memcpy(&prm->tm_iocb, param, param_size);

	spin_lock_irqsave(&tgt->sess_work_lock, flags);
	list_add_tail(&prm->sess_works_list_entry, &tgt->sess_works_list);
	spin_unlock_irqrestore(&tgt->sess_work_lock, flags);

	schedule_work(&tgt->sess_work);

	return 0;
}

/*
 * ha->hardware_lock supposed to be held on entry. Might drop it, then reaquire
 */
static void qlt_send_notify_ack(struct qla_qpair *qpair,
	struct imm_ntfy_from_isp *ntfy,
	uint32_t add_flags, uint16_t resp_code, int resp_code_valid,
	uint16_t srr_flags, uint16_t srr_reject_code, uint8_t srr_explan)
{
	struct scsi_qla_host *vha = qpair->vha;
	struct qla_hw_data *ha = vha->hw;
	request_t *pkt;
	struct nack_to_isp *nack;

	if (!ha->flags.fw_started)
		return;

	ql_dbg(ql_dbg_tgt, vha, 0xe004, "Sending NOTIFY_ACK (ha=%p)\n", ha);

	pkt = (request_t *)__qla2x00_alloc_iocbs(qpair, NULL);
	if (!pkt) {
		ql_dbg(ql_dbg_tgt, vha, 0xe049,
		    "qla_target(%d): %s failed: unable to allocate "
		    "request packet\n", vha->vp_idx, __func__);
		return;
	}

	if (vha->vha_tgt.qla_tgt != NULL)
		vha->vha_tgt.qla_tgt->notify_ack_expected++;

	pkt->entry_type = NOTIFY_ACK_TYPE;
	pkt->entry_count = 1;

	nack = (struct nack_to_isp *)pkt;
	nack->ox_id = ntfy->ox_id;

	nack->u.isp24.handle = QLA_TGT_SKIP_HANDLE;
	nack->u.isp24.nport_handle = ntfy->u.isp24.nport_handle;
	if (le16_to_cpu(ntfy->u.isp24.status) == IMM_NTFY_ELS) {
		nack->u.isp24.flags = ntfy->u.isp24.flags &
			cpu_to_le32(NOTIFY24XX_FLAGS_PUREX_IOCB);
	}
	nack->u.isp24.srr_rx_id = ntfy->u.isp24.srr_rx_id;
	nack->u.isp24.status = ntfy->u.isp24.status;
	nack->u.isp24.status_subcode = ntfy->u.isp24.status_subcode;
	nack->u.isp24.fw_handle = ntfy->u.isp24.fw_handle;
	nack->u.isp24.exchange_address = ntfy->u.isp24.exchange_address;
	nack->u.isp24.srr_rel_offs = ntfy->u.isp24.srr_rel_offs;
	nack->u.isp24.srr_ui = ntfy->u.isp24.srr_ui;
	nack->u.isp24.srr_flags = cpu_to_le16(srr_flags);
	nack->u.isp24.srr_reject_code = srr_reject_code;
	nack->u.isp24.srr_reject_code_expl = srr_explan;
	nack->u.isp24.vp_index = ntfy->u.isp24.vp_index;

	ql_dbg(ql_dbg_tgt, vha, 0xe005,
	    "qla_target(%d): Sending 24xx Notify Ack %d\n",
	    vha->vp_idx, nack->u.isp24.status);

	/* Memory Barrier */
	wmb();
	qla2x00_start_iocbs(vha, qpair->req);
}

/*
 * ha->hardware_lock supposed to be held on entry. Might drop it, then reaquire
 */
static void qlt_24xx_send_abts_resp(struct qla_qpair *qpair,
	struct abts_recv_from_24xx *abts, uint32_t status,
	bool ids_reversed)
{
	struct scsi_qla_host *vha = qpair->vha;
	struct qla_hw_data *ha = vha->hw;
	struct abts_resp_to_24xx *resp;
	uint32_t f_ctl;
	uint8_t *p;

	ql_dbg(ql_dbg_tgt, vha, 0xe006,
	    "Sending task mgmt ABTS response (ha=%p, atio=%p, status=%x\n",
	    ha, abts, status);

	resp = (struct abts_resp_to_24xx *)qla2x00_alloc_iocbs_ready(qpair,
	    NULL);
	if (!resp) {
		ql_dbg(ql_dbg_tgt, vha, 0xe04a,
		    "qla_target(%d): %s failed: unable to allocate "
		    "request packet", vha->vp_idx, __func__);
		return;
	}

	resp->entry_type = ABTS_RESP_24XX;
	resp->entry_count = 1;
	resp->nport_handle = abts->nport_handle;
	resp->vp_index = vha->vp_idx;
	resp->sof_type = abts->sof_type;
	resp->exchange_address = abts->exchange_address;
	resp->fcp_hdr_le = abts->fcp_hdr_le;
	f_ctl = cpu_to_le32(F_CTL_EXCH_CONTEXT_RESP |
	    F_CTL_LAST_SEQ | F_CTL_END_SEQ |
	    F_CTL_SEQ_INITIATIVE);
	p = (uint8_t *)&f_ctl;
	resp->fcp_hdr_le.f_ctl[0] = *p++;
	resp->fcp_hdr_le.f_ctl[1] = *p++;
	resp->fcp_hdr_le.f_ctl[2] = *p;
	if (ids_reversed) {
		resp->fcp_hdr_le.d_id[0] = abts->fcp_hdr_le.d_id[0];
		resp->fcp_hdr_le.d_id[1] = abts->fcp_hdr_le.d_id[1];
		resp->fcp_hdr_le.d_id[2] = abts->fcp_hdr_le.d_id[2];
		resp->fcp_hdr_le.s_id[0] = abts->fcp_hdr_le.s_id[0];
		resp->fcp_hdr_le.s_id[1] = abts->fcp_hdr_le.s_id[1];
		resp->fcp_hdr_le.s_id[2] = abts->fcp_hdr_le.s_id[2];
	} else {
		resp->fcp_hdr_le.d_id[0] = abts->fcp_hdr_le.s_id[0];
		resp->fcp_hdr_le.d_id[1] = abts->fcp_hdr_le.s_id[1];
		resp->fcp_hdr_le.d_id[2] = abts->fcp_hdr_le.s_id[2];
		resp->fcp_hdr_le.s_id[0] = abts->fcp_hdr_le.d_id[0];
		resp->fcp_hdr_le.s_id[1] = abts->fcp_hdr_le.d_id[1];
		resp->fcp_hdr_le.s_id[2] = abts->fcp_hdr_le.d_id[2];
	}
	resp->exchange_addr_to_abort = abts->exchange_addr_to_abort;
	if (status == FCP_TMF_CMPL) {
		resp->fcp_hdr_le.r_ctl = R_CTL_BASIC_LINK_SERV | R_CTL_B_ACC;
		resp->payload.ba_acct.seq_id_valid = SEQ_ID_INVALID;
		resp->payload.ba_acct.low_seq_cnt = 0x0000;
		resp->payload.ba_acct.high_seq_cnt = 0xFFFF;
		resp->payload.ba_acct.ox_id = abts->fcp_hdr_le.ox_id;
		resp->payload.ba_acct.rx_id = abts->fcp_hdr_le.rx_id;
	} else {
		resp->fcp_hdr_le.r_ctl = R_CTL_BASIC_LINK_SERV | R_CTL_B_RJT;
		resp->payload.ba_rjt.reason_code =
			BA_RJT_REASON_CODE_UNABLE_TO_PERFORM;
		/* Other bytes are zero */
	}

	vha->vha_tgt.qla_tgt->abts_resp_expected++;

	/* Memory Barrier */
	wmb();
	if (qpair->reqq_start_iocbs)
		qpair->reqq_start_iocbs(qpair);
	else
		qla2x00_start_iocbs(vha, qpair->req);
}

/*
 * ha->hardware_lock supposed to be held on entry. Might drop it, then reaquire
 */
static void qlt_24xx_retry_term_exchange(struct scsi_qla_host *vha,
	struct abts_resp_from_24xx_fw *entry)
{
	struct ctio7_to_24xx *ctio;

	ql_dbg(ql_dbg_tgt, vha, 0xe007,
	    "Sending retry TERM EXCH CTIO7 (ha=%p)\n", vha->hw);

	ctio = (struct ctio7_to_24xx *)qla2x00_alloc_iocbs_ready(
	    vha->hw->base_qpair, NULL);
	if (ctio == NULL) {
		ql_dbg(ql_dbg_tgt, vha, 0xe04b,
		    "qla_target(%d): %s failed: unable to allocate "
		    "request packet\n", vha->vp_idx, __func__);
		return;
	}

	/*
	 * We've got on entrance firmware's response on by us generated
	 * ABTS response. So, in it ID fields are reversed.
	 */

	ctio->entry_type = CTIO_TYPE7;
	ctio->entry_count = 1;
	ctio->nport_handle = entry->nport_handle;
	ctio->handle = QLA_TGT_SKIP_HANDLE |	CTIO_COMPLETION_HANDLE_MARK;
	ctio->timeout = cpu_to_le16(QLA_TGT_TIMEOUT);
	ctio->vp_index = vha->vp_idx;
	ctio->initiator_id[0] = entry->fcp_hdr_le.d_id[0];
	ctio->initiator_id[1] = entry->fcp_hdr_le.d_id[1];
	ctio->initiator_id[2] = entry->fcp_hdr_le.d_id[2];
	ctio->exchange_addr = entry->exchange_addr_to_abort;
	ctio->u.status1.flags = cpu_to_le16(CTIO7_FLAGS_STATUS_MODE_1 |
					    CTIO7_FLAGS_TERMINATE);
	ctio->u.status1.ox_id = cpu_to_le16(entry->fcp_hdr_le.ox_id);

	/* Memory Barrier */
	wmb();
	qla2x00_start_iocbs(vha, vha->req);

	qlt_24xx_send_abts_resp(vha->hw->base_qpair,
	    (struct abts_recv_from_24xx *)entry,
	    FCP_TMF_CMPL, true);
}

static int abort_cmd_for_tag(struct scsi_qla_host *vha, uint32_t tag)
{
	struct qla_tgt_sess_op *op;
	struct qla_tgt_cmd *cmd;
	unsigned long flags;

	spin_lock_irqsave(&vha->cmd_list_lock, flags);
	list_for_each_entry(op, &vha->qla_sess_op_cmd_list, cmd_list) {
		if (tag == op->atio.u.isp24.exchange_addr) {
			op->aborted = true;
			spin_unlock_irqrestore(&vha->cmd_list_lock, flags);
			return 1;
		}
	}

	list_for_each_entry(op, &vha->unknown_atio_list, cmd_list) {
		if (tag == op->atio.u.isp24.exchange_addr) {
			op->aborted = true;
			spin_unlock_irqrestore(&vha->cmd_list_lock, flags);
			return 1;
		}
	}

	list_for_each_entry(cmd, &vha->qla_cmd_list, cmd_list) {
		if (tag == cmd->atio.u.isp24.exchange_addr) {
			cmd->aborted = 1;
			spin_unlock_irqrestore(&vha->cmd_list_lock, flags);
			return 1;
		}
	}
	spin_unlock_irqrestore(&vha->cmd_list_lock, flags);

	return 0;
}

/* drop cmds for the given lun
 * XXX only looks for cmds on the port through which lun reset was recieved
 * XXX does not go through the list of other port (which may have cmds
 *     for the same lun)
 */
static void abort_cmds_for_lun(struct scsi_qla_host *vha,
			        u64 lun, uint8_t *s_id)
{
	struct qla_tgt_sess_op *op;
	struct qla_tgt_cmd *cmd;
	uint32_t key;
	unsigned long flags;

	key = sid_to_key(s_id);
	spin_lock_irqsave(&vha->cmd_list_lock, flags);
	list_for_each_entry(op, &vha->qla_sess_op_cmd_list, cmd_list) {
		uint32_t op_key;
		u64 op_lun;

		op_key = sid_to_key(op->atio.u.isp24.fcp_hdr.s_id);
		op_lun = scsilun_to_int(
			(struct scsi_lun *)&op->atio.u.isp24.fcp_cmnd.lun);
		if (op_key == key && op_lun == lun)
			op->aborted = true;
	}

	list_for_each_entry(op, &vha->unknown_atio_list, cmd_list) {
		uint32_t op_key;
		u64 op_lun;

		op_key = sid_to_key(op->atio.u.isp24.fcp_hdr.s_id);
		op_lun = scsilun_to_int(
			(struct scsi_lun *)&op->atio.u.isp24.fcp_cmnd.lun);
		if (op_key == key && op_lun == lun)
			op->aborted = true;
	}

	list_for_each_entry(cmd, &vha->qla_cmd_list, cmd_list) {
		uint32_t cmd_key;
		u64 cmd_lun;

		cmd_key = sid_to_key(cmd->atio.u.isp24.fcp_hdr.s_id);
		cmd_lun = scsilun_to_int(
			(struct scsi_lun *)&cmd->atio.u.isp24.fcp_cmnd.lun);
		if (cmd_key == key && cmd_lun == lun)
			cmd->aborted = 1;
	}
	spin_unlock_irqrestore(&vha->cmd_list_lock, flags);
}

/* ha->hardware_lock supposed to be held on entry */
static int __qlt_24xx_handle_abts(struct scsi_qla_host *vha,
	struct abts_recv_from_24xx *abts, struct fc_port *sess)
{
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt_mgmt_cmd *mcmd;
	int rc;

	if (abort_cmd_for_tag(vha, abts->exchange_addr_to_abort)) {
		/* send TASK_ABORT response immediately */
		qlt_24xx_send_abts_resp(ha->base_qpair, abts, FCP_TMF_CMPL, false);
		return 0;
	}

	ql_dbg(ql_dbg_tgt_mgt, vha, 0xf00f,
	    "qla_target(%d): task abort (tag=%d)\n",
	    vha->vp_idx, abts->exchange_addr_to_abort);

	mcmd = mempool_alloc(qla_tgt_mgmt_cmd_mempool, GFP_ATOMIC);
	if (mcmd == NULL) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf051,
		    "qla_target(%d): %s: Allocation of ABORT cmd failed",
		    vha->vp_idx, __func__);
		return -ENOMEM;
	}
	memset(mcmd, 0, sizeof(*mcmd));

	mcmd->sess = sess;
	memcpy(&mcmd->orig_iocb.abts, abts, sizeof(mcmd->orig_iocb.abts));
	mcmd->reset_count = ha->base_qpair->chip_reset;
	mcmd->tmr_func = QLA_TGT_ABTS;
	mcmd->qpair = ha->base_qpair;
	mcmd->vha = vha;

	/*
	 * LUN is looked up by target-core internally based on the passed
	 * abts->exchange_addr_to_abort tag.
	 */
	rc = ha->tgt.tgt_ops->handle_tmr(mcmd, 0, mcmd->tmr_func,
	    abts->exchange_addr_to_abort);
	if (rc != 0) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf052,
		    "qla_target(%d):  tgt_ops->handle_tmr()"
		    " failed: %d", vha->vp_idx, rc);
		mempool_free(mcmd, qla_tgt_mgmt_cmd_mempool);
		return -EFAULT;
	}

	return 0;
}

/*
 * ha->hardware_lock supposed to be held on entry. Might drop it, then reaquire
 */
static void qlt_24xx_handle_abts(struct scsi_qla_host *vha,
	struct abts_recv_from_24xx *abts)
{
	struct qla_hw_data *ha = vha->hw;
	struct fc_port *sess;
	uint32_t tag = abts->exchange_addr_to_abort;
	uint8_t s_id[3];
	int rc;
	unsigned long flags;

	if (le32_to_cpu(abts->fcp_hdr_le.parameter) & ABTS_PARAM_ABORT_SEQ) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf053,
		    "qla_target(%d): ABTS: Abort Sequence not "
		    "supported\n", vha->vp_idx);
		qlt_24xx_send_abts_resp(ha->base_qpair, abts, FCP_TMF_REJECTED,
		    false);
		return;
	}

	if (tag == ATIO_EXCHANGE_ADDRESS_UNKNOWN) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf010,
		    "qla_target(%d): ABTS: Unknown Exchange "
		    "Address received\n", vha->vp_idx);
		qlt_24xx_send_abts_resp(ha->base_qpair, abts, FCP_TMF_REJECTED,
		    false);
		return;
	}

	ql_dbg(ql_dbg_tgt_mgt, vha, 0xf011,
	    "qla_target(%d): task abort (s_id=%x:%x:%x, "
	    "tag=%d, param=%x)\n", vha->vp_idx, abts->fcp_hdr_le.s_id[2],
	    abts->fcp_hdr_le.s_id[1], abts->fcp_hdr_le.s_id[0], tag,
	    le32_to_cpu(abts->fcp_hdr_le.parameter));

	s_id[0] = abts->fcp_hdr_le.s_id[2];
	s_id[1] = abts->fcp_hdr_le.s_id[1];
	s_id[2] = abts->fcp_hdr_le.s_id[0];

	spin_lock_irqsave(&ha->tgt.sess_lock, flags);
	sess = ha->tgt.tgt_ops->find_sess_by_s_id(vha, s_id);
	if (!sess) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf012,
		    "qla_target(%d): task abort for non-existant session\n",
		    vha->vp_idx);
		rc = qlt_sched_sess_work(vha->vha_tgt.qla_tgt,
		    QLA_TGT_SESS_WORK_ABORT, abts, sizeof(*abts));

		spin_unlock_irqrestore(&ha->tgt.sess_lock, flags);

		if (rc != 0) {
			qlt_24xx_send_abts_resp(ha->base_qpair, abts,
			    FCP_TMF_REJECTED, false);
		}
		return;
	}
	spin_unlock_irqrestore(&ha->tgt.sess_lock, flags);


	if (sess->deleted) {
		qlt_24xx_send_abts_resp(ha->base_qpair, abts, FCP_TMF_REJECTED,
		    false);
		return;
	}

	rc = __qlt_24xx_handle_abts(vha, abts, sess);
	if (rc != 0) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf054,
		    "qla_target(%d): __qlt_24xx_handle_abts() failed: %d\n",
		    vha->vp_idx, rc);
		qlt_24xx_send_abts_resp(ha->base_qpair, abts, FCP_TMF_REJECTED,
		    false);
		return;
	}
}

/*
 * ha->hardware_lock supposed to be held on entry. Might drop it, then reaquire
 */
static void qlt_24xx_send_task_mgmt_ctio(struct qla_qpair *qpair,
	struct qla_tgt_mgmt_cmd *mcmd, uint32_t resp_code)
{
	struct scsi_qla_host *ha = mcmd->vha;
	struct atio_from_isp *atio = &mcmd->orig_iocb.atio;
	struct ctio7_to_24xx *ctio;
	uint16_t temp;

	ql_dbg(ql_dbg_tgt, ha, 0xe008,
	    "Sending task mgmt CTIO7 (ha=%p, atio=%p, resp_code=%x\n",
	    ha, atio, resp_code);


	ctio = (struct ctio7_to_24xx *)__qla2x00_alloc_iocbs(qpair, NULL);
	if (ctio == NULL) {
		ql_dbg(ql_dbg_tgt, ha, 0xe04c,
		    "qla_target(%d): %s failed: unable to allocate "
		    "request packet\n", ha->vp_idx, __func__);
		return;
	}

	ctio->entry_type = CTIO_TYPE7;
	ctio->entry_count = 1;
	ctio->handle = QLA_TGT_SKIP_HANDLE | CTIO_COMPLETION_HANDLE_MARK;
	ctio->nport_handle = mcmd->sess->loop_id;
	ctio->timeout = cpu_to_le16(QLA_TGT_TIMEOUT);
	ctio->vp_index = ha->vp_idx;
	ctio->initiator_id[0] = atio->u.isp24.fcp_hdr.s_id[2];
	ctio->initiator_id[1] = atio->u.isp24.fcp_hdr.s_id[1];
	ctio->initiator_id[2] = atio->u.isp24.fcp_hdr.s_id[0];
	ctio->exchange_addr = atio->u.isp24.exchange_addr;
	temp = (atio->u.isp24.attr << 9)|
		CTIO7_FLAGS_STATUS_MODE_1 | CTIO7_FLAGS_SEND_STATUS;
	ctio->u.status1.flags = cpu_to_le16(temp);
	temp = be16_to_cpu(atio->u.isp24.fcp_hdr.ox_id);
	ctio->u.status1.ox_id = cpu_to_le16(temp);
	ctio->u.status1.scsi_status =
	    cpu_to_le16(SS_RESPONSE_INFO_LEN_VALID);
	ctio->u.status1.response_len = cpu_to_le16(8);
	ctio->u.status1.sense_data[0] = resp_code;

	/* Memory Barrier */
	wmb();
	if (qpair->reqq_start_iocbs)
		qpair->reqq_start_iocbs(qpair);
	else
		qla2x00_start_iocbs(ha, qpair->req);
}

void qlt_free_mcmd(struct qla_tgt_mgmt_cmd *mcmd)
{
	mempool_free(mcmd, qla_tgt_mgmt_cmd_mempool);
}
EXPORT_SYMBOL(qlt_free_mcmd);

/*
 * ha->hardware_lock supposed to be held on entry. Might drop it, then
 * reacquire
 */
void qlt_send_resp_ctio(struct qla_qpair *qpair, struct qla_tgt_cmd *cmd,
    uint8_t scsi_status, uint8_t sense_key, uint8_t asc, uint8_t ascq)
{
	struct atio_from_isp *atio = &cmd->atio;
	struct ctio7_to_24xx *ctio;
	uint16_t temp;
	struct scsi_qla_host *vha = cmd->vha;

	ql_dbg(ql_dbg_tgt_dif, vha, 0x3066,
	    "Sending response CTIO7 (vha=%p, atio=%p, scsi_status=%02x, "
	    "sense_key=%02x, asc=%02x, ascq=%02x",
	    vha, atio, scsi_status, sense_key, asc, ascq);

	ctio = (struct ctio7_to_24xx *)qla2x00_alloc_iocbs(vha, NULL);
	if (!ctio) {
		ql_dbg(ql_dbg_async, vha, 0x3067,
		    "qla2x00t(%ld): %s failed: unable to allocate request packet",
		    vha->host_no, __func__);
		goto out;
	}

	ctio->entry_type = CTIO_TYPE7;
	ctio->entry_count = 1;
	ctio->handle = QLA_TGT_SKIP_HANDLE;
	ctio->nport_handle = cmd->sess->loop_id;
	ctio->timeout = cpu_to_le16(QLA_TGT_TIMEOUT);
	ctio->vp_index = vha->vp_idx;
	ctio->initiator_id[0] = atio->u.isp24.fcp_hdr.s_id[2];
	ctio->initiator_id[1] = atio->u.isp24.fcp_hdr.s_id[1];
	ctio->initiator_id[2] = atio->u.isp24.fcp_hdr.s_id[0];
	ctio->exchange_addr = atio->u.isp24.exchange_addr;
	temp = (atio->u.isp24.attr << 9) |
	    CTIO7_FLAGS_STATUS_MODE_1 | CTIO7_FLAGS_SEND_STATUS;
	ctio->u.status1.flags = cpu_to_le16(temp);
	temp = be16_to_cpu(atio->u.isp24.fcp_hdr.ox_id);
	ctio->u.status1.ox_id = cpu_to_le16(temp);
	ctio->u.status1.scsi_status =
	    cpu_to_le16(SS_RESPONSE_INFO_LEN_VALID | scsi_status);
	ctio->u.status1.response_len = cpu_to_le16(18);
	ctio->u.status1.residual = cpu_to_le32(get_datalen_for_atio(atio));

	if (ctio->u.status1.residual != 0)
		ctio->u.status1.scsi_status |=
		    cpu_to_le16(SS_RESIDUAL_UNDER);

	/* Response code and sense key */
	put_unaligned_le32(((0x70 << 24) | (sense_key << 8)),
	    (&ctio->u.status1.sense_data)[0]);
	/* Additional sense length */
	put_unaligned_le32(0x0a, (&ctio->u.status1.sense_data)[1]);
	/* ASC and ASCQ */
	put_unaligned_le32(((asc << 24) | (ascq << 16)),
	    (&ctio->u.status1.sense_data)[3]);

	/* Memory Barrier */
	wmb();

	if (qpair->reqq_start_iocbs)
		qpair->reqq_start_iocbs(qpair);
	else
		qla2x00_start_iocbs(vha, qpair->req);

out:
	return;
}

/* callback from target fabric module code */
void qlt_xmit_tm_rsp(struct qla_tgt_mgmt_cmd *mcmd)
{
	struct scsi_qla_host *vha = mcmd->sess->vha;
	struct qla_hw_data *ha = vha->hw;
	unsigned long flags;
	struct qla_qpair *qpair = mcmd->qpair;

	ql_dbg(ql_dbg_tgt_mgt, vha, 0xf013,
	    "TM response mcmd (%p) status %#x state %#x",
	    mcmd, mcmd->fc_tm_rsp, mcmd->flags);

	spin_lock_irqsave(qpair->qp_lock_ptr, flags);

	if (!vha->flags.online || mcmd->reset_count != qpair->chip_reset) {
		/*
		 * Either the port is not online or this request was from
		 * previous life, just abort the processing.
		 */
		ql_dbg(ql_dbg_async, vha, 0xe100,
			"RESET-TMR online/active/old-count/new-count = %d/%d/%d/%d.\n",
			vha->flags.online, qla2x00_reset_active(vha),
			mcmd->reset_count, qpair->chip_reset);
		ha->tgt.tgt_ops->free_mcmd(mcmd);
		spin_unlock_irqrestore(qpair->qp_lock_ptr, flags);
		return;
	}

	if (mcmd->flags == QLA24XX_MGMT_SEND_NACK) {
		if (mcmd->orig_iocb.imm_ntfy.u.isp24.status_subcode ==
		    ELS_LOGO ||
		    mcmd->orig_iocb.imm_ntfy.u.isp24.status_subcode ==
		    ELS_PRLO ||
		    mcmd->orig_iocb.imm_ntfy.u.isp24.status_subcode ==
		    ELS_TPRLO) {
			ql_dbg(ql_dbg_disc, vha, 0x2106,
			    "TM response logo %phC status %#x state %#x",
			    mcmd->sess->port_name, mcmd->fc_tm_rsp,
			    mcmd->flags);
			qlt_schedule_sess_for_deletion_lock(mcmd->sess);
		} else {
			qlt_send_notify_ack(vha->hw->base_qpair,
			    &mcmd->orig_iocb.imm_ntfy, 0, 0, 0, 0, 0, 0);
		}
	} else {
		if (mcmd->orig_iocb.atio.u.raw.entry_type == ABTS_RECV_24XX)
			qlt_24xx_send_abts_resp(qpair, &mcmd->orig_iocb.abts,
			    mcmd->fc_tm_rsp, false);
		else
			qlt_24xx_send_task_mgmt_ctio(qpair, mcmd,
			    mcmd->fc_tm_rsp);
	}
	/*
	 * Make the callback for ->free_mcmd() to queue_work() and invoke
	 * target_put_sess_cmd() to drop cmd_kref to 1.  The final
	 * target_put_sess_cmd() call will be made from TFO->check_stop_free()
	 * -> tcm_qla2xxx_check_stop_free() to release the TMR associated se_cmd
	 * descriptor after TFO->queue_tm_rsp() -> tcm_qla2xxx_queue_tm_rsp() ->
	 * qlt_xmit_tm_rsp() returns here..
	 */
	ha->tgt.tgt_ops->free_mcmd(mcmd);
	spin_unlock_irqrestore(qpair->qp_lock_ptr, flags);
}
EXPORT_SYMBOL(qlt_xmit_tm_rsp);

/* No locks */
static int qlt_pci_map_calc_cnt(struct qla_tgt_prm *prm)
{
	struct qla_tgt_cmd *cmd = prm->cmd;

	BUG_ON(cmd->sg_cnt == 0);

	prm->sg = (struct scatterlist *)cmd->sg;
	prm->seg_cnt = pci_map_sg(cmd->qpair->pdev, cmd->sg,
	    cmd->sg_cnt, cmd->dma_data_direction);
	if (unlikely(prm->seg_cnt == 0))
		goto out_err;

	prm->cmd->sg_mapped = 1;

	if (cmd->se_cmd.prot_op == TARGET_PROT_NORMAL) {
		/*
		 * If greater than four sg entries then we need to allocate
		 * the continuation entries
		 */
		if (prm->seg_cnt > QLA_TGT_DATASEGS_PER_CMD_24XX)
			prm->req_cnt += DIV_ROUND_UP(prm->seg_cnt -
			QLA_TGT_DATASEGS_PER_CMD_24XX,
			QLA_TGT_DATASEGS_PER_CONT_24XX);
	} else {
		/* DIF */
		if ((cmd->se_cmd.prot_op == TARGET_PROT_DIN_INSERT) ||
		    (cmd->se_cmd.prot_op == TARGET_PROT_DOUT_STRIP)) {
			prm->seg_cnt = DIV_ROUND_UP(cmd->bufflen, cmd->blk_sz);
			prm->tot_dsds = prm->seg_cnt;
		} else
			prm->tot_dsds = prm->seg_cnt;

		if (cmd->prot_sg_cnt) {
			prm->prot_sg      = cmd->prot_sg;
			prm->prot_seg_cnt = pci_map_sg(cmd->qpair->pdev,
				cmd->prot_sg, cmd->prot_sg_cnt,
				cmd->dma_data_direction);
			if (unlikely(prm->prot_seg_cnt == 0))
				goto out_err;

			if ((cmd->se_cmd.prot_op == TARGET_PROT_DIN_INSERT) ||
			    (cmd->se_cmd.prot_op == TARGET_PROT_DOUT_STRIP)) {
				/* Dif Bundling not support here */
				prm->prot_seg_cnt = DIV_ROUND_UP(cmd->bufflen,
								cmd->blk_sz);
				prm->tot_dsds += prm->prot_seg_cnt;
			} else
				prm->tot_dsds += prm->prot_seg_cnt;
		}
	}

	return 0;

out_err:
	ql_dbg_qp(ql_dbg_tgt, prm->cmd->qpair, 0xe04d,
	    "qla_target(%d): PCI mapping failed: sg_cnt=%d",
	    0, prm->cmd->sg_cnt);
	return -1;
}

static void qlt_unmap_sg(struct scsi_qla_host *vha, struct qla_tgt_cmd *cmd)
{
	struct qla_hw_data *ha;
	struct qla_qpair *qpair;
	if (!cmd->sg_mapped)
		return;

	qpair = cmd->qpair;

	pci_unmap_sg(qpair->pdev, cmd->sg, cmd->sg_cnt,
	    cmd->dma_data_direction);
	cmd->sg_mapped = 0;

	if (cmd->prot_sg_cnt)
		pci_unmap_sg(qpair->pdev, cmd->prot_sg, cmd->prot_sg_cnt,
			cmd->dma_data_direction);

	if (!cmd->ctx)
		return;
	ha = vha->hw;
	if (cmd->ctx_dsd_alloced)
		qla2x00_clean_dsd_pool(ha, cmd->ctx);

	dma_pool_free(ha->dl_dma_pool, cmd->ctx, cmd->ctx->crc_ctx_dma);
}

static int qlt_check_reserve_free_req(struct qla_qpair *qpair,
	uint32_t req_cnt)
{
	uint32_t cnt;
	struct req_que *req = qpair->req;

	if (req->cnt < (req_cnt + 2)) {
		cnt = (uint16_t)(qpair->use_shadow_reg ? *req->out_ptr :
		    RD_REG_DWORD_RELAXED(req->req_q_out));

		if  (req->ring_index < cnt)
			req->cnt = cnt - req->ring_index;
		else
			req->cnt = req->length - (req->ring_index - cnt);

		if (unlikely(req->cnt < (req_cnt + 2)))
			return -EAGAIN;
	}

	req->cnt -= req_cnt;

	return 0;
}

/*
 * ha->hardware_lock supposed to be held on entry. Might drop it, then reaquire
 */
static inline void *qlt_get_req_pkt(struct req_que *req)
{
	/* Adjust ring index. */
	req->ring_index++;
	if (req->ring_index == req->length) {
		req->ring_index = 0;
		req->ring_ptr = req->ring;
	} else {
		req->ring_ptr++;
	}
	return (cont_entry_t *)req->ring_ptr;
}

/* ha->hardware_lock supposed to be held on entry */
static inline uint32_t qlt_make_handle(struct qla_qpair *qpair)
{
	uint32_t h;
	int index;
	uint8_t found = 0;
	struct req_que *req = qpair->req;

	h = req->current_outstanding_cmd;

	for (index = 1; index < req->num_outstanding_cmds; index++) {
		h++;
		if (h == req->num_outstanding_cmds)
			h = 1;

		if (h == QLA_TGT_SKIP_HANDLE)
			continue;

		if (!req->outstanding_cmds[h]) {
			found = 1;
			break;
		}
	}

	if (found) {
		req->current_outstanding_cmd = h;
	} else {
		ql_dbg(ql_dbg_io, qpair->vha, 0x305b,
		    "qla_target(%d): Ran out of empty cmd slots\n",
		    qpair->vha->vp_idx);
		h = QLA_TGT_NULL_HANDLE;
	}

	return h;
}

/* ha->hardware_lock supposed to be held on entry */
static int qlt_24xx_build_ctio_pkt(struct qla_qpair *qpair,
	struct qla_tgt_prm *prm)
{
	uint32_t h;
	struct ctio7_to_24xx *pkt;
	struct atio_from_isp *atio = &prm->cmd->atio;
	uint16_t temp;

	pkt = (struct ctio7_to_24xx *)qpair->req->ring_ptr;
	prm->pkt = pkt;
	memset(pkt, 0, sizeof(*pkt));

	pkt->entry_type = CTIO_TYPE7;
	pkt->entry_count = (uint8_t)prm->req_cnt;
	pkt->vp_index = prm->cmd->vp_idx;

	h = qlt_make_handle(qpair);
	if (unlikely(h == QLA_TGT_NULL_HANDLE)) {
		/*
		 * CTIO type 7 from the firmware doesn't provide a way to
		 * know the initiator's LOOP ID, hence we can't find
		 * the session and, so, the command.
		 */
		return -EAGAIN;
	} else
		qpair->req->outstanding_cmds[h] = (srb_t *)prm->cmd;

	pkt->handle = MAKE_HANDLE(qpair->req->id, h);
	pkt->handle |= CTIO_COMPLETION_HANDLE_MARK;
	pkt->nport_handle = cpu_to_le16(prm->cmd->loop_id);
	pkt->timeout = cpu_to_le16(QLA_TGT_TIMEOUT);
	pkt->initiator_id[0] = atio->u.isp24.fcp_hdr.s_id[2];
	pkt->initiator_id[1] = atio->u.isp24.fcp_hdr.s_id[1];
	pkt->initiator_id[2] = atio->u.isp24.fcp_hdr.s_id[0];
	pkt->exchange_addr = atio->u.isp24.exchange_addr;
	temp = atio->u.isp24.attr << 9;
	pkt->u.status0.flags |= cpu_to_le16(temp);
	temp = be16_to_cpu(atio->u.isp24.fcp_hdr.ox_id);
	pkt->u.status0.ox_id = cpu_to_le16(temp);
	pkt->u.status0.relative_offset = cpu_to_le32(prm->cmd->offset);

	return 0;
}

/*
 * ha->hardware_lock supposed to be held on entry. We have already made sure
 * that there is sufficient amount of request entries to not drop it.
 */
static void qlt_load_cont_data_segments(struct qla_tgt_prm *prm)
{
	int cnt;
	uint32_t *dword_ptr;

	/* Build continuation packets */
	while (prm->seg_cnt > 0) {
		cont_a64_entry_t *cont_pkt64 =
			(cont_a64_entry_t *)qlt_get_req_pkt(
			   prm->cmd->qpair->req);

		/*
		 * Make sure that from cont_pkt64 none of
		 * 64-bit specific fields used for 32-bit
		 * addressing. Cast to (cont_entry_t *) for
		 * that.
		 */

		memset(cont_pkt64, 0, sizeof(*cont_pkt64));

		cont_pkt64->entry_count = 1;
		cont_pkt64->sys_define = 0;

		cont_pkt64->entry_type = CONTINUE_A64_TYPE;
		dword_ptr = (uint32_t *)&cont_pkt64->dseg_0_address;

		/* Load continuation entry data segments */
		for (cnt = 0;
		    cnt < QLA_TGT_DATASEGS_PER_CONT_24XX && prm->seg_cnt;
		    cnt++, prm->seg_cnt--) {
			*dword_ptr++ =
			    cpu_to_le32(pci_dma_lo32
				(sg_dma_address(prm->sg)));
			*dword_ptr++ = cpu_to_le32(pci_dma_hi32
			    (sg_dma_address(prm->sg)));
			*dword_ptr++ = cpu_to_le32(sg_dma_len(prm->sg));

			prm->sg = sg_next(prm->sg);
		}
	}
}

/*
 * ha->hardware_lock supposed to be held on entry. We have already made sure
 * that there is sufficient amount of request entries to not drop it.
 */
static void qlt_load_data_segments(struct qla_tgt_prm *prm)
{
	int cnt;
	uint32_t *dword_ptr;
	struct ctio7_to_24xx *pkt24 = (struct ctio7_to_24xx *)prm->pkt;

	pkt24->u.status0.transfer_length = cpu_to_le32(prm->cmd->bufflen);

	/* Setup packet address segment pointer */
	dword_ptr = pkt24->u.status0.dseg_0_address;

	/* Set total data segment count */
	if (prm->seg_cnt)
		pkt24->dseg_count = cpu_to_le16(prm->seg_cnt);

	if (prm->seg_cnt == 0) {
		/* No data transfer */
		*dword_ptr++ = 0;
		*dword_ptr = 0;
		return;
	}

	/* If scatter gather */

	/* Load command entry data segments */
	for (cnt = 0;
	    (cnt < QLA_TGT_DATASEGS_PER_CMD_24XX) && prm->seg_cnt;
	    cnt++, prm->seg_cnt--) {
		*dword_ptr++ =
		    cpu_to_le32(pci_dma_lo32(sg_dma_address(prm->sg)));

		*dword_ptr++ = cpu_to_le32(pci_dma_hi32(
			sg_dma_address(prm->sg)));

		*dword_ptr++ = cpu_to_le32(sg_dma_len(prm->sg));

		prm->sg = sg_next(prm->sg);
	}

	qlt_load_cont_data_segments(prm);
}

static inline int qlt_has_data(struct qla_tgt_cmd *cmd)
{
	return cmd->bufflen > 0;
}

static void qlt_print_dif_err(struct qla_tgt_prm *prm)
{
	struct qla_tgt_cmd *cmd;
	struct scsi_qla_host *vha;

	/* asc 0x10=dif error */
	if (prm->sense_buffer && (prm->sense_buffer[12] == 0x10)) {
		cmd = prm->cmd;
		vha = cmd->vha;
		/* ASCQ */
		switch (prm->sense_buffer[13]) {
		case 1:
			ql_dbg(ql_dbg_tgt_dif, vha, 0xe00b,
			    "BE detected Guard TAG ERR: lba[0x%llx|%lld] len[0x%x] "
			    "se_cmd=%p tag[%x]",
			    cmd->lba, cmd->lba, cmd->num_blks, &cmd->se_cmd,
			    cmd->atio.u.isp24.exchange_addr);
			break;
		case 2:
			ql_dbg(ql_dbg_tgt_dif, vha, 0xe00c,
			    "BE detected APP TAG ERR: lba[0x%llx|%lld] len[0x%x] "
			    "se_cmd=%p tag[%x]",
			    cmd->lba, cmd->lba, cmd->num_blks, &cmd->se_cmd,
			    cmd->atio.u.isp24.exchange_addr);
			break;
		case 3:
			ql_dbg(ql_dbg_tgt_dif, vha, 0xe00f,
			    "BE detected REF TAG ERR: lba[0x%llx|%lld] len[0x%x] "
			    "se_cmd=%p tag[%x]",
			    cmd->lba, cmd->lba, cmd->num_blks, &cmd->se_cmd,
			    cmd->atio.u.isp24.exchange_addr);
			break;
		default:
			ql_dbg(ql_dbg_tgt_dif, vha, 0xe010,
			    "BE detected Dif ERR: lba[%llx|%lld] len[%x] "
			    "se_cmd=%p tag[%x]",
			    cmd->lba, cmd->lba, cmd->num_blks, &cmd->se_cmd,
			    cmd->atio.u.isp24.exchange_addr);
			break;
		}
		ql_dump_buffer(ql_dbg_tgt_dif, vha, 0xe011, cmd->cdb, 16);
	}
}

/*
 * Called without ha->hardware_lock held
 */
static int qlt_pre_xmit_response(struct qla_tgt_cmd *cmd,
	struct qla_tgt_prm *prm, int xmit_type, uint8_t scsi_status,
	uint32_t *full_req_cnt)
{
	struct se_cmd *se_cmd = &cmd->se_cmd;
	struct qla_qpair *qpair = cmd->qpair;

	prm->cmd = cmd;
	prm->tgt = cmd->tgt;
	prm->pkt = NULL;
	prm->rq_result = scsi_status;
	prm->sense_buffer = &cmd->sense_buffer[0];
	prm->sense_buffer_len = TRANSPORT_SENSE_BUFFER;
	prm->sg = NULL;
	prm->seg_cnt = -1;
	prm->req_cnt = 1;
	prm->residual = 0;
	prm->add_status_pkt = 0;
	prm->prot_sg = NULL;
	prm->prot_seg_cnt = 0;
	prm->tot_dsds = 0;

	if ((xmit_type & QLA_TGT_XMIT_DATA) && qlt_has_data(cmd)) {
		if  (qlt_pci_map_calc_cnt(prm) != 0)
			return -EAGAIN;
	}

	*full_req_cnt = prm->req_cnt;

	if (se_cmd->se_cmd_flags & SCF_UNDERFLOW_BIT) {
		prm->residual = se_cmd->residual_count;
		ql_dbg_qp(ql_dbg_io + ql_dbg_verbose, qpair, 0x305c,
		    "Residual underflow: %d (tag %lld, op %x, bufflen %d, rq_result %x)\n",
		       prm->residual, se_cmd->tag,
		       se_cmd->t_task_cdb ? se_cmd->t_task_cdb[0] : 0,
		       cmd->bufflen, prm->rq_result);
		prm->rq_result |= SS_RESIDUAL_UNDER;
	} else if (se_cmd->se_cmd_flags & SCF_OVERFLOW_BIT) {
		prm->residual = se_cmd->residual_count;
		ql_dbg_qp(ql_dbg_io, qpair, 0x305d,
		    "Residual overflow: %d (tag %lld, op %x, bufflen %d, rq_result %x)\n",
		       prm->residual, se_cmd->tag, se_cmd->t_task_cdb ?
		       se_cmd->t_task_cdb[0] : 0, cmd->bufflen, prm->rq_result);
		prm->rq_result |= SS_RESIDUAL_OVER;
	}

	if (xmit_type & QLA_TGT_XMIT_STATUS) {
		/*
		 * If QLA_TGT_XMIT_DATA is not set, add_status_pkt will be
		 * ignored in *xmit_response() below
		 */
		if (qlt_has_data(cmd)) {
			if (QLA_TGT_SENSE_VALID(prm->sense_buffer) ||
			    (IS_FWI2_CAPABLE(cmd->vha->hw) &&
			    (prm->rq_result != 0))) {
				prm->add_status_pkt = 1;
				(*full_req_cnt)++;
			}
		}
	}

	return 0;
}

static inline int qlt_need_explicit_conf(struct qla_tgt_cmd *cmd,
    int sending_sense)
{
	if (cmd->qpair->enable_class_2)
		return 0;

	if (sending_sense)
		return cmd->conf_compl_supported;
	else
		return cmd->qpair->enable_explicit_conf &&
                    cmd->conf_compl_supported;
}

static void qlt_24xx_init_ctio_to_isp(struct ctio7_to_24xx *ctio,
	struct qla_tgt_prm *prm)
{
	prm->sense_buffer_len = min_t(uint32_t, prm->sense_buffer_len,
	    (uint32_t)sizeof(ctio->u.status1.sense_data));
	ctio->u.status0.flags |= cpu_to_le16(CTIO7_FLAGS_SEND_STATUS);
	if (qlt_need_explicit_conf(prm->cmd, 0)) {
		ctio->u.status0.flags |= cpu_to_le16(
		    CTIO7_FLAGS_EXPLICIT_CONFORM |
		    CTIO7_FLAGS_CONFORM_REQ);
	}
	ctio->u.status0.residual = cpu_to_le32(prm->residual);
	ctio->u.status0.scsi_status = cpu_to_le16(prm->rq_result);
	if (QLA_TGT_SENSE_VALID(prm->sense_buffer)) {
		int i;

		if (qlt_need_explicit_conf(prm->cmd, 1)) {
			if ((prm->rq_result & SS_SCSI_STATUS_BYTE) != 0) {
				ql_dbg_qp(ql_dbg_tgt, prm->cmd->qpair, 0xe017,
				    "Skipping EXPLICIT_CONFORM and "
				    "CTIO7_FLAGS_CONFORM_REQ for FCP READ w/ "
				    "non GOOD status\n");
				goto skip_explict_conf;
			}
			ctio->u.status1.flags |= cpu_to_le16(
			    CTIO7_FLAGS_EXPLICIT_CONFORM |
			    CTIO7_FLAGS_CONFORM_REQ);
		}
skip_explict_conf:
		ctio->u.status1.flags &=
		    ~cpu_to_le16(CTIO7_FLAGS_STATUS_MODE_0);
		ctio->u.status1.flags |=
		    cpu_to_le16(CTIO7_FLAGS_STATUS_MODE_1);
		ctio->u.status1.scsi_status |=
		    cpu_to_le16(SS_SENSE_LEN_VALID);
		ctio->u.status1.sense_length =
		    cpu_to_le16(prm->sense_buffer_len);
		for (i = 0; i < prm->sense_buffer_len/4; i++)
			((uint32_t *)ctio->u.status1.sense_data)[i] =
				cpu_to_be32(((uint32_t *)prm->sense_buffer)[i]);

		qlt_print_dif_err(prm);

	} else {
		ctio->u.status1.flags &=
		    ~cpu_to_le16(CTIO7_FLAGS_STATUS_MODE_0);
		ctio->u.status1.flags |=
		    cpu_to_le16(CTIO7_FLAGS_STATUS_MODE_1);
		ctio->u.status1.sense_length = 0;
		memset(ctio->u.status1.sense_data, 0,
		    sizeof(ctio->u.status1.sense_data));
	}

	/* Sense with len > 24, is it possible ??? */
}

static inline int
qlt_hba_err_chk_enabled(struct se_cmd *se_cmd)
{
	switch (se_cmd->prot_op) {
	case TARGET_PROT_DOUT_INSERT:
	case TARGET_PROT_DIN_STRIP:
		if (ql2xenablehba_err_chk >= 1)
			return 1;
		break;
	case TARGET_PROT_DOUT_PASS:
	case TARGET_PROT_DIN_PASS:
		if (ql2xenablehba_err_chk >= 2)
			return 1;
		break;
	case TARGET_PROT_DIN_INSERT:
	case TARGET_PROT_DOUT_STRIP:
		return 1;
	default:
		break;
	}
	return 0;
}

static inline int
qla_tgt_ref_mask_check(struct se_cmd *se_cmd)
{
	switch (se_cmd->prot_op) {
	case TARGET_PROT_DIN_INSERT:
	case TARGET_PROT_DOUT_INSERT:
	case TARGET_PROT_DIN_STRIP:
	case TARGET_PROT_DOUT_STRIP:
	case TARGET_PROT_DIN_PASS:
	case TARGET_PROT_DOUT_PASS:
	    return 1;
	default:
	    return 0;
	}
	return 0;
}

/*
 * qla_tgt_set_dif_tags - Extract Ref and App tags from SCSI command
 */
static void
qla_tgt_set_dif_tags(struct qla_tgt_cmd *cmd, struct crc_context *ctx,
    uint16_t *pfw_prot_opts)
{
	struct se_cmd *se_cmd = &cmd->se_cmd;
	uint32_t lba = 0xffffffff & se_cmd->t_task_lba;
	scsi_qla_host_t *vha = cmd->tgt->vha;
	struct qla_hw_data *ha = vha->hw;
	uint32_t t32 = 0;

	/*
	 * wait till Mode Sense/Select cmd, modepage Ah, subpage 2
	 * have been immplemented by TCM, before AppTag is avail.
	 * Look for modesense_handlers[]
	 */
	ctx->app_tag = 0;
	ctx->app_tag_mask[0] = 0x0;
	ctx->app_tag_mask[1] = 0x0;

	if (IS_PI_UNINIT_CAPABLE(ha)) {
		if ((se_cmd->prot_type == TARGET_DIF_TYPE1_PROT) ||
		    (se_cmd->prot_type == TARGET_DIF_TYPE2_PROT))
			*pfw_prot_opts |= PO_DIS_VALD_APP_ESC;
		else if (se_cmd->prot_type == TARGET_DIF_TYPE3_PROT)
			*pfw_prot_opts |= PO_DIS_VALD_APP_REF_ESC;
	}

	t32 = ha->tgt.tgt_ops->get_dif_tags(cmd, pfw_prot_opts);

	switch (se_cmd->prot_type) {
	case TARGET_DIF_TYPE0_PROT:
		/*
		 * No check for ql2xenablehba_err_chk, as it
		 * would be an I/O error if hba tag generation
		 * is not done.
		 */
		ctx->ref_tag = cpu_to_le32(lba);
		/* enable ALL bytes of the ref tag */
		ctx->ref_tag_mask[0] = 0xff;
		ctx->ref_tag_mask[1] = 0xff;
		ctx->ref_tag_mask[2] = 0xff;
		ctx->ref_tag_mask[3] = 0xff;
		break;
	case TARGET_DIF_TYPE1_PROT:
	    /*
	     * For TYPE 1 protection: 16 bit GUARD tag, 32 bit
	     * REF tag, and 16 bit app tag.
	     */
	    ctx->ref_tag = cpu_to_le32(lba);
	    if (!qla_tgt_ref_mask_check(se_cmd) ||
		!(ha->tgt.tgt_ops->chk_dif_tags(t32))) {
		    *pfw_prot_opts |= PO_DIS_REF_TAG_VALD;
		    break;
	    }
	    /* enable ALL bytes of the ref tag */
	    ctx->ref_tag_mask[0] = 0xff;
	    ctx->ref_tag_mask[1] = 0xff;
	    ctx->ref_tag_mask[2] = 0xff;
	    ctx->ref_tag_mask[3] = 0xff;
	    break;
	case TARGET_DIF_TYPE2_PROT:
	    /*
	     * For TYPE 2 protection: 16 bit GUARD + 32 bit REF
	     * tag has to match LBA in CDB + N
	     */
	    ctx->ref_tag = cpu_to_le32(lba);
	    if (!qla_tgt_ref_mask_check(se_cmd) ||
		!(ha->tgt.tgt_ops->chk_dif_tags(t32))) {
		    *pfw_prot_opts |= PO_DIS_REF_TAG_VALD;
		    break;
	    }
	    /* enable ALL bytes of the ref tag */
	    ctx->ref_tag_mask[0] = 0xff;
	    ctx->ref_tag_mask[1] = 0xff;
	    ctx->ref_tag_mask[2] = 0xff;
	    ctx->ref_tag_mask[3] = 0xff;
	    break;
	case TARGET_DIF_TYPE3_PROT:
	    /* For TYPE 3 protection: 16 bit GUARD only */
	    *pfw_prot_opts |= PO_DIS_REF_TAG_VALD;
	    ctx->ref_tag_mask[0] = ctx->ref_tag_mask[1] =
		ctx->ref_tag_mask[2] = ctx->ref_tag_mask[3] = 0x00;
	    break;
	}
}

static inline int
qlt_build_ctio_crc2_pkt(struct qla_qpair *qpair, struct qla_tgt_prm *prm)
{
	uint32_t		*cur_dsd;
	uint32_t		transfer_length = 0;
	uint32_t		data_bytes;
	uint32_t		dif_bytes;
	uint8_t			bundling = 1;
	uint8_t			*clr_ptr;
	struct crc_context	*crc_ctx_pkt = NULL;
	struct qla_hw_data	*ha;
	struct ctio_crc2_to_fw	*pkt;
	dma_addr_t		crc_ctx_dma;
	uint16_t		fw_prot_opts = 0;
	struct qla_tgt_cmd	*cmd = prm->cmd;
	struct se_cmd		*se_cmd = &cmd->se_cmd;
	uint32_t h;
	struct atio_from_isp *atio = &prm->cmd->atio;
	struct qla_tc_param	tc;
	uint16_t t16;
	scsi_qla_host_t *vha = cmd->vha;

	ha = vha->hw;

	pkt = (struct ctio_crc2_to_fw *)qpair->req->ring_ptr;
	prm->pkt = pkt;
	memset(pkt, 0, sizeof(*pkt));

	ql_dbg_qp(ql_dbg_tgt, cmd->qpair, 0xe071,
		"qla_target(%d):%s: se_cmd[%p] CRC2 prot_op[0x%x] cmd prot sg:cnt[%p:%x] lba[%llu]\n",
		cmd->vp_idx, __func__, se_cmd, se_cmd->prot_op,
		prm->prot_sg, prm->prot_seg_cnt, se_cmd->t_task_lba);

	if ((se_cmd->prot_op == TARGET_PROT_DIN_INSERT) ||
	    (se_cmd->prot_op == TARGET_PROT_DOUT_STRIP))
		bundling = 0;

	/* Compute dif len and adjust data len to incude protection */
	data_bytes = cmd->bufflen;
	dif_bytes  = (data_bytes / cmd->blk_sz) * 8;

	switch (se_cmd->prot_op) {
	case TARGET_PROT_DIN_INSERT:
	case TARGET_PROT_DOUT_STRIP:
		transfer_length = data_bytes;
		if (cmd->prot_sg_cnt)
			data_bytes += dif_bytes;
		break;
	case TARGET_PROT_DIN_STRIP:
	case TARGET_PROT_DOUT_INSERT:
	case TARGET_PROT_DIN_PASS:
	case TARGET_PROT_DOUT_PASS:
		transfer_length = data_bytes + dif_bytes;
		break;
	default:
		BUG();
		break;
	}

	if (!qlt_hba_err_chk_enabled(se_cmd))
		fw_prot_opts |= 0x10; /* Disable Guard tag checking */
	/* HBA error checking enabled */
	else if (IS_PI_UNINIT_CAPABLE(ha)) {
		if ((se_cmd->prot_type == TARGET_DIF_TYPE1_PROT) ||
		    (se_cmd->prot_type == TARGET_DIF_TYPE2_PROT))
			fw_prot_opts |= PO_DIS_VALD_APP_ESC;
		else if (se_cmd->prot_type == TARGET_DIF_TYPE3_PROT)
			fw_prot_opts |= PO_DIS_VALD_APP_REF_ESC;
	}

	switch (se_cmd->prot_op) {
	case TARGET_PROT_DIN_INSERT:
	case TARGET_PROT_DOUT_INSERT:
		fw_prot_opts |= PO_MODE_DIF_INSERT;
		break;
	case TARGET_PROT_DIN_STRIP:
	case TARGET_PROT_DOUT_STRIP:
		fw_prot_opts |= PO_MODE_DIF_REMOVE;
		break;
	case TARGET_PROT_DIN_PASS:
	case TARGET_PROT_DOUT_PASS:
		fw_prot_opts |= PO_MODE_DIF_PASS;
		/* FUTURE: does tcm require T10CRC<->IPCKSUM conversion? */
		break;
	default:/* Normal Request */
		fw_prot_opts |= PO_MODE_DIF_PASS;
		break;
	}

	/* ---- PKT ---- */
	/* Update entry type to indicate Command Type CRC_2 IOCB */
	pkt->entry_type  = CTIO_CRC2;
	pkt->entry_count = 1;
	pkt->vp_index = cmd->vp_idx;

	h = qlt_make_handle(qpair);
	if (unlikely(h == QLA_TGT_NULL_HANDLE)) {
		/*
		 * CTIO type 7 from the firmware doesn't provide a way to
		 * know the initiator's LOOP ID, hence we can't find
		 * the session and, so, the command.
		 */
		return -EAGAIN;
	} else
		qpair->req->outstanding_cmds[h] = (srb_t *)prm->cmd;

	pkt->handle  = MAKE_HANDLE(qpair->req->id, h);
	pkt->handle |= CTIO_COMPLETION_HANDLE_MARK;
	pkt->nport_handle = cpu_to_le16(prm->cmd->loop_id);
	pkt->timeout = cpu_to_le16(QLA_TGT_TIMEOUT);
	pkt->initiator_id[0] = atio->u.isp24.fcp_hdr.s_id[2];
	pkt->initiator_id[1] = atio->u.isp24.fcp_hdr.s_id[1];
	pkt->initiator_id[2] = atio->u.isp24.fcp_hdr.s_id[0];
	pkt->exchange_addr   = atio->u.isp24.exchange_addr;

	/* silence compile warning */
	t16 = be16_to_cpu(atio->u.isp24.fcp_hdr.ox_id);
	pkt->ox_id  = cpu_to_le16(t16);

	t16 = (atio->u.isp24.attr << 9);
	pkt->flags |= cpu_to_le16(t16);
	pkt->relative_offset = cpu_to_le32(prm->cmd->offset);

	/* Set transfer direction */
	if (cmd->dma_data_direction == DMA_TO_DEVICE)
		pkt->flags = cpu_to_le16(CTIO7_FLAGS_DATA_IN);
	else if (cmd->dma_data_direction == DMA_FROM_DEVICE)
		pkt->flags = cpu_to_le16(CTIO7_FLAGS_DATA_OUT);

	pkt->dseg_count = prm->tot_dsds;
	/* Fibre channel byte count */
	pkt->transfer_length = cpu_to_le32(transfer_length);

	/* ----- CRC context -------- */

	/* Allocate CRC context from global pool */
	crc_ctx_pkt = cmd->ctx =
	    dma_pool_alloc(ha->dl_dma_pool, GFP_ATOMIC, &crc_ctx_dma);

	if (!crc_ctx_pkt)
		goto crc_queuing_error;

	/* Zero out CTX area. */
	clr_ptr = (uint8_t *)crc_ctx_pkt;
	memset(clr_ptr, 0, sizeof(*crc_ctx_pkt));

	crc_ctx_pkt->crc_ctx_dma = crc_ctx_dma;
	INIT_LIST_HEAD(&crc_ctx_pkt->dsd_list);

	/* Set handle */
	crc_ctx_pkt->handle = pkt->handle;

	qla_tgt_set_dif_tags(cmd, crc_ctx_pkt, &fw_prot_opts);

	pkt->crc_context_address[0] = cpu_to_le32(LSD(crc_ctx_dma));
	pkt->crc_context_address[1] = cpu_to_le32(MSD(crc_ctx_dma));
	pkt->crc_context_len = CRC_CONTEXT_LEN_FW;

	if (!bundling) {
		cur_dsd = (uint32_t *) &crc_ctx_pkt->u.nobundling.data_address;
	} else {
		/*
		 * Configure Bundling if we need to fetch interlaving
		 * protection PCI accesses
		 */
		fw_prot_opts |= PO_ENABLE_DIF_BUNDLING;
		crc_ctx_pkt->u.bundling.dif_byte_count = cpu_to_le32(dif_bytes);
		crc_ctx_pkt->u.bundling.dseg_count =
			cpu_to_le16(prm->tot_dsds - prm->prot_seg_cnt);
		cur_dsd = (uint32_t *) &crc_ctx_pkt->u.bundling.data_address;
	}

	/* Finish the common fields of CRC pkt */
	crc_ctx_pkt->blk_size   = cpu_to_le16(cmd->blk_sz);
	crc_ctx_pkt->prot_opts  = cpu_to_le16(fw_prot_opts);
	crc_ctx_pkt->byte_count = cpu_to_le32(data_bytes);
	crc_ctx_pkt->guard_seed = cpu_to_le16(0);

	memset((uint8_t *)&tc, 0 , sizeof(tc));
	tc.vha = vha;
	tc.blk_sz = cmd->blk_sz;
	tc.bufflen = cmd->bufflen;
	tc.sg = cmd->sg;
	tc.prot_sg = cmd->prot_sg;
	tc.ctx = crc_ctx_pkt;
	tc.ctx_dsd_alloced = &cmd->ctx_dsd_alloced;

	/* Walks data segments */
	pkt->flags |= cpu_to_le16(CTIO7_FLAGS_DSD_PTR);

	if (!bundling && prm->prot_seg_cnt) {
		if (qla24xx_walk_and_build_sglist_no_difb(ha, NULL, cur_dsd,
			prm->tot_dsds, &tc))
			goto crc_queuing_error;
	} else if (qla24xx_walk_and_build_sglist(ha, NULL, cur_dsd,
		(prm->tot_dsds - prm->prot_seg_cnt), &tc))
		goto crc_queuing_error;

	if (bundling && prm->prot_seg_cnt) {
		/* Walks dif segments */
		pkt->add_flags |= CTIO_CRC2_AF_DIF_DSD_ENA;

		cur_dsd = (uint32_t *) &crc_ctx_pkt->u.bundling.dif_address;
		if (qla24xx_walk_and_build_prot_sglist(ha, NULL, cur_dsd,
			prm->prot_seg_cnt, &tc))
			goto crc_queuing_error;
	}
	return QLA_SUCCESS;

crc_queuing_error:
	/* Cleanup will be performed by the caller */
	qpair->req->outstanding_cmds[h] = NULL;

	return QLA_FUNCTION_FAILED;
}

/*
 * Callback to setup response of xmit_type of QLA_TGT_XMIT_DATA and *
 * QLA_TGT_XMIT_STATUS for >= 24xx silicon
 */
int qlt_xmit_response(struct qla_tgt_cmd *cmd, int xmit_type,
	uint8_t scsi_status)
{
	struct scsi_qla_host *vha = cmd->vha;
	struct qla_qpair *qpair = cmd->qpair;
	struct ctio7_to_24xx *pkt;
	struct qla_tgt_prm prm;
	uint32_t full_req_cnt = 0;
	unsigned long flags = 0;
	int res;

	if (cmd->sess && cmd->sess->deleted) {
		cmd->state = QLA_TGT_STATE_PROCESSED;
		if (cmd->sess->logout_completed)
			/* no need to terminate. FW already freed exchange. */
			qlt_abort_cmd_on_host_reset(cmd->vha, cmd);
		else
			qlt_send_term_exchange(qpair, cmd, &cmd->atio, 0, 0);
		return 0;
	}

	ql_dbg_qp(ql_dbg_tgt, qpair, 0xe018,
	    "is_send_status=%d, cmd->bufflen=%d, cmd->sg_cnt=%d, cmd->dma_data_direction=%d se_cmd[%p] qp %d\n",
	    (xmit_type & QLA_TGT_XMIT_STATUS) ?
	    1 : 0, cmd->bufflen, cmd->sg_cnt, cmd->dma_data_direction,
	    &cmd->se_cmd, qpair->id);

	res = qlt_pre_xmit_response(cmd, &prm, xmit_type, scsi_status,
	    &full_req_cnt);
	if (unlikely(res != 0)) {
		return res;
	}

	spin_lock_irqsave(qpair->qp_lock_ptr, flags);

	if (xmit_type == QLA_TGT_XMIT_STATUS)
		qpair->tgt_counters.core_qla_snd_status++;
	else
		qpair->tgt_counters.core_qla_que_buf++;

	if (!qpair->fw_started || cmd->reset_count != qpair->chip_reset) {
		/*
		 * Either the port is not online or this request was from
		 * previous life, just abort the processing.
		 */
		cmd->state = QLA_TGT_STATE_PROCESSED;
		qlt_abort_cmd_on_host_reset(cmd->vha, cmd);
		ql_dbg_qp(ql_dbg_async, qpair, 0xe101,
			"RESET-RSP online/active/old-count/new-count = %d/%d/%d/%d.\n",
			vha->flags.online, qla2x00_reset_active(vha),
			cmd->reset_count, qpair->chip_reset);
		spin_unlock_irqrestore(qpair->qp_lock_ptr, flags);
		return 0;
	}

	/* Does F/W have an IOCBs for this request */
	res = qlt_check_reserve_free_req(qpair, full_req_cnt);
	if (unlikely(res))
		goto out_unmap_unlock;

	if (cmd->se_cmd.prot_op && (xmit_type & QLA_TGT_XMIT_DATA))
		res = qlt_build_ctio_crc2_pkt(qpair, &prm);
	else
		res = qlt_24xx_build_ctio_pkt(qpair, &prm);
	if (unlikely(res != 0)) {
		qpair->req->cnt += full_req_cnt;
		goto out_unmap_unlock;
	}

	pkt = (struct ctio7_to_24xx *)prm.pkt;

	if (qlt_has_data(cmd) && (xmit_type & QLA_TGT_XMIT_DATA)) {
		pkt->u.status0.flags |=
		    cpu_to_le16(CTIO7_FLAGS_DATA_IN |
			CTIO7_FLAGS_STATUS_MODE_0);

		if (cmd->se_cmd.prot_op == TARGET_PROT_NORMAL)
			qlt_load_data_segments(&prm);

		if (prm.add_status_pkt == 0) {
			if (xmit_type & QLA_TGT_XMIT_STATUS) {
				pkt->u.status0.scsi_status =
				    cpu_to_le16(prm.rq_result);
				pkt->u.status0.residual =
				    cpu_to_le32(prm.residual);
				pkt->u.status0.flags |= cpu_to_le16(
				    CTIO7_FLAGS_SEND_STATUS);
				if (qlt_need_explicit_conf(cmd, 0)) {
					pkt->u.status0.flags |=
					    cpu_to_le16(
						CTIO7_FLAGS_EXPLICIT_CONFORM |
						CTIO7_FLAGS_CONFORM_REQ);
				}
			}

		} else {
			/*
			 * We have already made sure that there is sufficient
			 * amount of request entries to not drop HW lock in
			 * req_pkt().
			 */
			struct ctio7_to_24xx *ctio =
				(struct ctio7_to_24xx *)qlt_get_req_pkt(
				    qpair->req);

			ql_dbg_qp(ql_dbg_tgt, qpair, 0x305e,
			    "Building additional status packet 0x%p.\n",
			    ctio);

			/*
			 * T10Dif: ctio_crc2_to_fw overlay ontop of
			 * ctio7_to_24xx
			 */
			memcpy(ctio, pkt, sizeof(*ctio));
			/* reset back to CTIO7 */
			ctio->entry_count = 1;
			ctio->entry_type = CTIO_TYPE7;
			ctio->dseg_count = 0;
			ctio->u.status1.flags &= ~cpu_to_le16(
			    CTIO7_FLAGS_DATA_IN);

			/* Real finish is ctio_m1's finish */
			pkt->handle |= CTIO_INTERMEDIATE_HANDLE_MARK;
			pkt->u.status0.flags |= cpu_to_le16(
			    CTIO7_FLAGS_DONT_RET_CTIO);

			/* qlt_24xx_init_ctio_to_isp will correct
			 * all neccessary fields that's part of CTIO7.
			 * There should be no residual of CTIO-CRC2 data.
			 */
			qlt_24xx_init_ctio_to_isp((struct ctio7_to_24xx *)ctio,
			    &prm);
		}
	} else
		qlt_24xx_init_ctio_to_isp(pkt, &prm);


	cmd->state = QLA_TGT_STATE_PROCESSED; /* Mid-level is done processing */
	cmd->cmd_sent_to_fw = 1;

	/* Memory Barrier */
	wmb();
	if (qpair->reqq_start_iocbs)
		qpair->reqq_start_iocbs(qpair);
	else
		qla2x00_start_iocbs(vha, qpair->req);
	spin_unlock_irqrestore(qpair->qp_lock_ptr, flags);

	return 0;

out_unmap_unlock:
	qlt_unmap_sg(vha, cmd);
	spin_unlock_irqrestore(qpair->qp_lock_ptr, flags);

	return res;
}
EXPORT_SYMBOL(qlt_xmit_response);

int qlt_rdy_to_xfer(struct qla_tgt_cmd *cmd)
{
	struct ctio7_to_24xx *pkt;
	struct scsi_qla_host *vha = cmd->vha;
	struct qla_tgt *tgt = cmd->tgt;
	struct qla_tgt_prm prm;
	unsigned long flags = 0;
	int res = 0;
	struct qla_qpair *qpair = cmd->qpair;

	memset(&prm, 0, sizeof(prm));
	prm.cmd = cmd;
	prm.tgt = tgt;
	prm.sg = NULL;
	prm.req_cnt = 1;

	/* Calculate number of entries and segments required */
	if (qlt_pci_map_calc_cnt(&prm) != 0)
		return -EAGAIN;

	if (!qpair->fw_started || (cmd->reset_count != qpair->chip_reset) ||
	    (cmd->sess && cmd->sess->deleted)) {
		/*
		 * Either the port is not online or this request was from
		 * previous life, just abort the processing.
		 */
		cmd->state = QLA_TGT_STATE_NEED_DATA;
		qlt_abort_cmd_on_host_reset(cmd->vha, cmd);
		ql_dbg_qp(ql_dbg_async, qpair, 0xe102,
			"RESET-XFR online/active/old-count/new-count = %d/%d/%d/%d.\n",
			vha->flags.online, qla2x00_reset_active(vha),
			cmd->reset_count, qpair->chip_reset);
		return 0;
	}

	spin_lock_irqsave(qpair->qp_lock_ptr, flags);
	/* Does F/W have an IOCBs for this request */
	res = qlt_check_reserve_free_req(qpair, prm.req_cnt);
	if (res != 0)
		goto out_unlock_free_unmap;
	if (cmd->se_cmd.prot_op)
		res = qlt_build_ctio_crc2_pkt(qpair, &prm);
	else
		res = qlt_24xx_build_ctio_pkt(qpair, &prm);

	if (unlikely(res != 0)) {
		qpair->req->cnt += prm.req_cnt;
		goto out_unlock_free_unmap;
	}

	pkt = (struct ctio7_to_24xx *)prm.pkt;
	pkt->u.status0.flags |= cpu_to_le16(CTIO7_FLAGS_DATA_OUT |
	    CTIO7_FLAGS_STATUS_MODE_0);

	if (cmd->se_cmd.prot_op == TARGET_PROT_NORMAL)
		qlt_load_data_segments(&prm);

	cmd->state = QLA_TGT_STATE_NEED_DATA;
	cmd->cmd_sent_to_fw = 1;

	/* Memory Barrier */
	wmb();
	if (qpair->reqq_start_iocbs)
		qpair->reqq_start_iocbs(qpair);
	else
		qla2x00_start_iocbs(vha, qpair->req);
	spin_unlock_irqrestore(qpair->qp_lock_ptr, flags);

	return res;

out_unlock_free_unmap:
	qlt_unmap_sg(vha, cmd);
	spin_unlock_irqrestore(qpair->qp_lock_ptr, flags);

	return res;
}
EXPORT_SYMBOL(qlt_rdy_to_xfer);


/*
 * it is assumed either hardware_lock or qpair lock is held.
 */
static void
qlt_handle_dif_error(struct qla_qpair *qpair, struct qla_tgt_cmd *cmd,
	struct ctio_crc_from_fw *sts)
{
	uint8_t		*ap = &sts->actual_dif[0];
	uint8_t		*ep = &sts->expected_dif[0];
	uint64_t	lba = cmd->se_cmd.t_task_lba;
	uint8_t scsi_status, sense_key, asc, ascq;
	unsigned long flags;
	struct scsi_qla_host *vha = cmd->vha;

	cmd->trc_flags |= TRC_DIF_ERR;

	cmd->a_guard   = be16_to_cpu(*(uint16_t *)(ap + 0));
	cmd->a_app_tag = be16_to_cpu(*(uint16_t *)(ap + 2));
	cmd->a_ref_tag = be32_to_cpu(*(uint32_t *)(ap + 4));

	cmd->e_guard   = be16_to_cpu(*(uint16_t *)(ep + 0));
	cmd->e_app_tag = be16_to_cpu(*(uint16_t *)(ep + 2));
	cmd->e_ref_tag = be32_to_cpu(*(uint32_t *)(ep + 4));

	ql_dbg(ql_dbg_tgt_dif, vha, 0xf075,
	    "%s: aborted %d state %d\n", __func__, cmd->aborted, cmd->state);

	scsi_status = sense_key = asc = ascq = 0;

	/* check appl tag */
	if (cmd->e_app_tag != cmd->a_app_tag) {
		ql_dbg(ql_dbg_tgt_dif, vha, 0xe00d,
		    "App Tag ERR: cdb[%x] lba[%llx %llx] blks[%x] [Actual|Expected] Ref[%x|%x], App[%x|%x], Guard [%x|%x] cmd=%p ox_id[%04x]",
		    cmd->cdb[0], lba, (lba+cmd->num_blks), cmd->num_blks,
		    cmd->a_ref_tag, cmd->e_ref_tag, cmd->a_app_tag,
		    cmd->e_app_tag, cmd->a_guard, cmd->e_guard, cmd,
		    cmd->atio.u.isp24.fcp_hdr.ox_id);

		cmd->dif_err_code = DIF_ERR_APP;
		scsi_status = SAM_STAT_CHECK_CONDITION;
		sense_key = ABORTED_COMMAND;
		asc = 0x10;
		ascq = 0x2;
	}

	/* check ref tag */
	if (cmd->e_ref_tag != cmd->a_ref_tag) {
		ql_dbg(ql_dbg_tgt_dif, vha, 0xe00e,
		    "Ref Tag ERR: cdb[%x] lba[%llx %llx] blks[%x] [Actual|Expected] Ref[%x|%x], App[%x|%x], Guard[%x|%x] cmd=%p ox_id[%04x] ",
		    cmd->cdb[0], lba, (lba+cmd->num_blks), cmd->num_blks,
		    cmd->a_ref_tag, cmd->e_ref_tag, cmd->a_app_tag,
		    cmd->e_app_tag, cmd->a_guard, cmd->e_guard, cmd,
		    cmd->atio.u.isp24.fcp_hdr.ox_id);

		cmd->dif_err_code = DIF_ERR_REF;
		scsi_status = SAM_STAT_CHECK_CONDITION;
		sense_key = ABORTED_COMMAND;
		asc = 0x10;
		ascq = 0x3;
		goto out;
	}

	/* check guard */
	if (cmd->e_guard != cmd->a_guard) {
		ql_dbg(ql_dbg_tgt_dif, vha, 0xe012,
		    "Guard ERR: cdb[%x] lba[%llx %llx] blks[%x] [Actual|Expected] Ref[%x|%x], App[%x|%x], Guard [%x|%x] cmd=%p ox_id[%04x]",
		    cmd->cdb[0], lba, (lba+cmd->num_blks), cmd->num_blks,
		    cmd->a_ref_tag, cmd->e_ref_tag, cmd->a_app_tag,
		    cmd->e_app_tag, cmd->a_guard, cmd->e_guard, cmd,
		    cmd->atio.u.isp24.fcp_hdr.ox_id);

		cmd->dif_err_code = DIF_ERR_GRD;
		scsi_status = SAM_STAT_CHECK_CONDITION;
		sense_key = ABORTED_COMMAND;
		asc = 0x10;
		ascq = 0x1;
	}
out:
	switch (cmd->state) {
	case QLA_TGT_STATE_NEED_DATA:
		/* handle_data will load DIF error code  */
		cmd->state = QLA_TGT_STATE_DATA_IN;
		vha->hw->tgt.tgt_ops->handle_data(cmd);
		break;
	default:
		spin_lock_irqsave(&cmd->cmd_lock, flags);
		if (cmd->aborted) {
			spin_unlock_irqrestore(&cmd->cmd_lock, flags);
			vha->hw->tgt.tgt_ops->free_cmd(cmd);
			break;
		}
		spin_unlock_irqrestore(&cmd->cmd_lock, flags);

		qlt_send_resp_ctio(qpair, cmd, scsi_status, sense_key, asc,
		    ascq);
		/* assume scsi status gets out on the wire.
		 * Will not wait for completion.
		 */
		vha->hw->tgt.tgt_ops->free_cmd(cmd);
		break;
	}
}

/* If hardware_lock held on entry, might drop it, then reaquire */
/* This function sends the appropriate CTIO to ISP 2xxx or 24xx */
static int __qlt_send_term_imm_notif(struct scsi_qla_host *vha,
	struct imm_ntfy_from_isp *ntfy)
{
	struct nack_to_isp *nack;
	struct qla_hw_data *ha = vha->hw;
	request_t *pkt;
	int ret = 0;

	ql_dbg(ql_dbg_tgt_tmr, vha, 0xe01c,
	    "Sending TERM ELS CTIO (ha=%p)\n", ha);

	pkt = (request_t *)qla2x00_alloc_iocbs(vha, NULL);
	if (pkt == NULL) {
		ql_dbg(ql_dbg_tgt, vha, 0xe080,
		    "qla_target(%d): %s failed: unable to allocate "
		    "request packet\n", vha->vp_idx, __func__);
		return -ENOMEM;
	}

	pkt->entry_type = NOTIFY_ACK_TYPE;
	pkt->entry_count = 1;
	pkt->handle = QLA_TGT_SKIP_HANDLE;

	nack = (struct nack_to_isp *)pkt;
	nack->ox_id = ntfy->ox_id;

	nack->u.isp24.nport_handle = ntfy->u.isp24.nport_handle;
	if (le16_to_cpu(ntfy->u.isp24.status) == IMM_NTFY_ELS) {
		nack->u.isp24.flags = ntfy->u.isp24.flags &
			__constant_cpu_to_le32(NOTIFY24XX_FLAGS_PUREX_IOCB);
	}

	/* terminate */
	nack->u.isp24.flags |=
		__constant_cpu_to_le16(NOTIFY_ACK_FLAGS_TERMINATE);

	nack->u.isp24.srr_rx_id = ntfy->u.isp24.srr_rx_id;
	nack->u.isp24.status = ntfy->u.isp24.status;
	nack->u.isp24.status_subcode = ntfy->u.isp24.status_subcode;
	nack->u.isp24.fw_handle = ntfy->u.isp24.fw_handle;
	nack->u.isp24.exchange_address = ntfy->u.isp24.exchange_address;
	nack->u.isp24.srr_rel_offs = ntfy->u.isp24.srr_rel_offs;
	nack->u.isp24.srr_ui = ntfy->u.isp24.srr_ui;
	nack->u.isp24.vp_index = ntfy->u.isp24.vp_index;

	qla2x00_start_iocbs(vha, vha->req);
	return ret;
}

static void qlt_send_term_imm_notif(struct scsi_qla_host *vha,
	struct imm_ntfy_from_isp *imm, int ha_locked)
{
	unsigned long flags = 0;
	int rc;

	if (ha_locked) {
		rc = __qlt_send_term_imm_notif(vha, imm);

#if 0	/* Todo  */
		if (rc == -ENOMEM)
			qlt_alloc_qfull_cmd(vha, imm, 0, 0);
#else
		if (rc) {
		}
#endif
		goto done;
	}

	spin_lock_irqsave(&vha->hw->hardware_lock, flags);
	rc = __qlt_send_term_imm_notif(vha, imm);

#if 0	/* Todo */
	if (rc == -ENOMEM)
		qlt_alloc_qfull_cmd(vha, imm, 0, 0);
#endif

done:
	if (!ha_locked)
		spin_unlock_irqrestore(&vha->hw->hardware_lock, flags);
}

/*
 * If hardware_lock held on entry, might drop it, then reaquire
 * This function sends the appropriate CTIO to ISP 2xxx or 24xx
 */
static int __qlt_send_term_exchange(struct qla_qpair *qpair,
	struct qla_tgt_cmd *cmd,
	struct atio_from_isp *atio)
{
	struct scsi_qla_host *vha = qpair->vha;
	struct ctio7_to_24xx *ctio24;
	struct qla_hw_data *ha = vha->hw;
	request_t *pkt;
	int ret = 0;
	uint16_t temp;

	ql_dbg(ql_dbg_tgt, vha, 0xe009, "Sending TERM EXCH CTIO (ha=%p)\n", ha);

	if (cmd)
		vha = cmd->vha;

	pkt = (request_t *)qla2x00_alloc_iocbs_ready(qpair, NULL);
	if (pkt == NULL) {
		ql_dbg(ql_dbg_tgt, vha, 0xe050,
		    "qla_target(%d): %s failed: unable to allocate "
		    "request packet\n", vha->vp_idx, __func__);
		return -ENOMEM;
	}

	if (cmd != NULL) {
		if (cmd->state < QLA_TGT_STATE_PROCESSED) {
			ql_dbg(ql_dbg_tgt, vha, 0xe051,
			    "qla_target(%d): Terminating cmd %p with "
			    "incorrect state %d\n", vha->vp_idx, cmd,
			    cmd->state);
		} else
			ret = 1;
	}

	qpair->tgt_counters.num_term_xchg_sent++;
	pkt->entry_count = 1;
	pkt->handle = QLA_TGT_SKIP_HANDLE | CTIO_COMPLETION_HANDLE_MARK;

	ctio24 = (struct ctio7_to_24xx *)pkt;
	ctio24->entry_type = CTIO_TYPE7;
	ctio24->nport_handle = CTIO7_NHANDLE_UNRECOGNIZED;
	ctio24->timeout = cpu_to_le16(QLA_TGT_TIMEOUT);
	ctio24->vp_index = vha->vp_idx;
	ctio24->initiator_id[0] = atio->u.isp24.fcp_hdr.s_id[2];
	ctio24->initiator_id[1] = atio->u.isp24.fcp_hdr.s_id[1];
	ctio24->initiator_id[2] = atio->u.isp24.fcp_hdr.s_id[0];
	ctio24->exchange_addr = atio->u.isp24.exchange_addr;
	temp = (atio->u.isp24.attr << 9) | CTIO7_FLAGS_STATUS_MODE_1 |
		CTIO7_FLAGS_TERMINATE;
	ctio24->u.status1.flags = cpu_to_le16(temp);
	temp = be16_to_cpu(atio->u.isp24.fcp_hdr.ox_id);
	ctio24->u.status1.ox_id = cpu_to_le16(temp);

	/* Most likely, it isn't needed */
	ctio24->u.status1.residual = get_unaligned((uint32_t *)
	    &atio->u.isp24.fcp_cmnd.add_cdb[
	    atio->u.isp24.fcp_cmnd.add_cdb_len]);
	if (ctio24->u.status1.residual != 0)
		ctio24->u.status1.scsi_status |= SS_RESIDUAL_UNDER;

	/* Memory Barrier */
	wmb();
	if (qpair->reqq_start_iocbs)
		qpair->reqq_start_iocbs(qpair);
	else
		qla2x00_start_iocbs(vha, qpair->req);
	return ret;
}

static void qlt_send_term_exchange(struct qla_qpair *qpair,
	struct qla_tgt_cmd *cmd, struct atio_from_isp *atio, int ha_locked,
	int ul_abort)
{
	struct scsi_qla_host *vha;
	unsigned long flags = 0;
	int rc;

	/* why use different vha? NPIV */
	if (cmd)
		vha = cmd->vha;
	else
		vha = qpair->vha;

	if (ha_locked) {
		rc = __qlt_send_term_exchange(qpair, cmd, atio);
		if (rc == -ENOMEM)
			qlt_alloc_qfull_cmd(vha, atio, 0, 0);
		goto done;
	}
	spin_lock_irqsave(qpair->qp_lock_ptr, flags);
	rc = __qlt_send_term_exchange(qpair, cmd, atio);
	if (rc == -ENOMEM)
		qlt_alloc_qfull_cmd(vha, atio, 0, 0);

done:
	if (cmd && !ul_abort && !cmd->aborted) {
		if (cmd->sg_mapped)
			qlt_unmap_sg(vha, cmd);
		vha->hw->tgt.tgt_ops->free_cmd(cmd);
	}

	if (!ha_locked)
		spin_unlock_irqrestore(qpair->qp_lock_ptr, flags);

	return;
}

static void qlt_init_term_exchange(struct scsi_qla_host *vha)
{
	struct list_head free_list;
	struct qla_tgt_cmd *cmd, *tcmd;

	vha->hw->tgt.leak_exchg_thresh_hold =
	    (vha->hw->cur_fw_xcb_count/100) * LEAK_EXCHG_THRESH_HOLD_PERCENT;

	cmd = tcmd = NULL;
	if (!list_empty(&vha->hw->tgt.q_full_list)) {
		INIT_LIST_HEAD(&free_list);
		list_splice_init(&vha->hw->tgt.q_full_list, &free_list);

		list_for_each_entry_safe(cmd, tcmd, &free_list, cmd_list) {
			list_del(&cmd->cmd_list);
			/* This cmd was never sent to TCM.  There is no need
			 * to schedule free or call free_cmd
			 */
			qlt_free_cmd(cmd);
			vha->hw->tgt.num_qfull_cmds_alloc--;
		}
	}
	vha->hw->tgt.num_qfull_cmds_dropped = 0;
}

static void qlt_chk_exch_leak_thresh_hold(struct scsi_qla_host *vha)
{
	uint32_t total_leaked;

	total_leaked = vha->hw->tgt.num_qfull_cmds_dropped;

	if (vha->hw->tgt.leak_exchg_thresh_hold &&
	    (total_leaked > vha->hw->tgt.leak_exchg_thresh_hold)) {

		ql_dbg(ql_dbg_tgt, vha, 0xe079,
		    "Chip reset due to exchange starvation: %d/%d.\n",
		    total_leaked, vha->hw->cur_fw_xcb_count);

		if (IS_P3P_TYPE(vha->hw))
			set_bit(FCOE_CTX_RESET_NEEDED, &vha->dpc_flags);
		else
			set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
		qla2xxx_wake_dpc(vha);
	}

}

int qlt_abort_cmd(struct qla_tgt_cmd *cmd)
{
	struct qla_tgt *tgt = cmd->tgt;
	struct scsi_qla_host *vha = tgt->vha;
	struct se_cmd *se_cmd = &cmd->se_cmd;
	unsigned long flags;

	ql_dbg(ql_dbg_tgt_mgt, vha, 0xf014,
	    "qla_target(%d): terminating exchange for aborted cmd=%p "
	    "(se_cmd=%p, tag=%llu)", vha->vp_idx, cmd, &cmd->se_cmd,
	    se_cmd->tag);

	spin_lock_irqsave(&cmd->cmd_lock, flags);
	if (cmd->aborted) {
		spin_unlock_irqrestore(&cmd->cmd_lock, flags);
		/*
		 * It's normal to see 2 calls in this path:
		 *  1) XFER Rdy completion + CMD_T_ABORT
		 *  2) TCM TMR - drain_state_list
		 */
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf016,
		    "multiple abort. %p transport_state %x, t_state %x, "
		    "se_cmd_flags %x\n", cmd, cmd->se_cmd.transport_state,
		    cmd->se_cmd.t_state, cmd->se_cmd.se_cmd_flags);
		return EIO;
	}
	cmd->aborted = 1;
	cmd->trc_flags |= TRC_ABORT;
	spin_unlock_irqrestore(&cmd->cmd_lock, flags);

	qlt_send_term_exchange(cmd->qpair, cmd, &cmd->atio, 0, 1);
	return 0;
}
EXPORT_SYMBOL(qlt_abort_cmd);

void qlt_free_cmd(struct qla_tgt_cmd *cmd)
{
	struct fc_port *sess = cmd->sess;

	ql_dbg(ql_dbg_tgt, cmd->vha, 0xe074,
	    "%s: se_cmd[%p] ox_id %04x\n",
	    __func__, &cmd->se_cmd,
	    be16_to_cpu(cmd->atio.u.isp24.fcp_hdr.ox_id));

	BUG_ON(cmd->cmd_in_wq);

	if (cmd->sg_mapped)
		qlt_unmap_sg(cmd->vha, cmd);

	if (!cmd->q_full)
		qlt_decr_num_pend_cmds(cmd->vha);

	BUG_ON(cmd->sg_mapped);
	cmd->jiffies_at_free = get_jiffies_64();
	if (unlikely(cmd->free_sg))
		kfree(cmd->sg);

	if (!sess || !sess->se_sess) {
		WARN_ON(1);
		return;
	}
	cmd->jiffies_at_free = get_jiffies_64();
	percpu_ida_free(&sess->se_sess->sess_tag_pool, cmd->se_cmd.map_tag);
}
EXPORT_SYMBOL(qlt_free_cmd);

/*
 * ha->hardware_lock supposed to be held on entry. Might drop it, then reaquire
 */
static int qlt_term_ctio_exchange(struct qla_qpair *qpair, void *ctio,
	struct qla_tgt_cmd *cmd, uint32_t status)
{
	int term = 0;
	struct scsi_qla_host *vha = qpair->vha;

	if (cmd->se_cmd.prot_op)
		ql_dbg(ql_dbg_tgt_dif, vha, 0xe013,
		    "Term DIF cmd: lba[0x%llx|%lld] len[0x%x] "
		    "se_cmd=%p tag[%x] op %#x/%s",
		     cmd->lba, cmd->lba,
		     cmd->num_blks, &cmd->se_cmd,
		     cmd->atio.u.isp24.exchange_addr,
		     cmd->se_cmd.prot_op,
		     prot_op_str(cmd->se_cmd.prot_op));

	if (ctio != NULL) {
		struct ctio7_from_24xx *c = (struct ctio7_from_24xx *)ctio;
		term = !(c->flags &
		    cpu_to_le16(OF_TERM_EXCH));
	} else
		term = 1;

	if (term)
		qlt_term_ctio_exchange(qpair, ctio, cmd, status);

	return term;
}


/* ha->hardware_lock supposed to be held on entry */
static struct qla_tgt_cmd *qlt_ctio_to_cmd(struct scsi_qla_host *vha,
	struct rsp_que *rsp, uint32_t handle, void *ctio)
{
	struct qla_tgt_cmd *cmd = NULL;
	struct req_que *req;
	int qid = GET_QID(handle);
	uint32_t h = handle & ~QLA_TGT_HANDLE_MASK;

	if (unlikely(h == QLA_TGT_SKIP_HANDLE))
		return NULL;

	if (qid == rsp->req->id) {
		req = rsp->req;
	} else if (vha->hw->req_q_map[qid]) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0x1000a,
		    "qla_target(%d): CTIO completion with different QID %d handle %x\n",
		    vha->vp_idx, rsp->id, handle);
		req = vha->hw->req_q_map[qid];
	} else {
		return NULL;
	}

	h &= QLA_CMD_HANDLE_MASK;

	if (h != QLA_TGT_NULL_HANDLE) {
		if (unlikely(h >= req->num_outstanding_cmds)) {
			ql_dbg(ql_dbg_tgt, vha, 0xe052,
			    "qla_target(%d): Wrong handle %x received\n",
			    vha->vp_idx, handle);
			return NULL;
		}

		cmd = (struct qla_tgt_cmd *)req->outstanding_cmds[h];
		if (unlikely(cmd == NULL)) {
			ql_dbg(ql_dbg_async, vha, 0xe053,
			    "qla_target(%d): Suspicious: unable to find the command with handle %x req->id %d rsp->id %d\n",
				vha->vp_idx, handle, req->id, rsp->id);
			return NULL;
		}
		req->outstanding_cmds[h] = NULL;
	} else if (ctio != NULL) {
		/* We can't get loop ID from CTIO7 */
		ql_dbg(ql_dbg_tgt, vha, 0xe054,
		    "qla_target(%d): Wrong CTIO received: QLA24xx doesn't "
		    "support NULL handles\n", vha->vp_idx);
		return NULL;
	}

	return cmd;
}

/* hardware_lock should be held by caller. */
void
qlt_abort_cmd_on_host_reset(struct scsi_qla_host *vha, struct qla_tgt_cmd *cmd)
{
	struct qla_hw_data *ha = vha->hw;

	if (cmd->sg_mapped)
		qlt_unmap_sg(vha, cmd);

	/* TODO: fix debug message type and ids. */
	if (cmd->state == QLA_TGT_STATE_PROCESSED) {
		ql_dbg(ql_dbg_io, vha, 0xff00,
		    "HOST-ABORT: state=PROCESSED.\n");
	} else if (cmd->state == QLA_TGT_STATE_NEED_DATA) {
		cmd->write_data_transferred = 0;
		cmd->state = QLA_TGT_STATE_DATA_IN;

		ql_dbg(ql_dbg_io, vha, 0xff01,
		    "HOST-ABORT: state=DATA_IN.\n");

		ha->tgt.tgt_ops->handle_data(cmd);
		return;
	} else {
		ql_dbg(ql_dbg_io, vha, 0xff03,
		    "HOST-ABORT: state=BAD(%d).\n",
		    cmd->state);
		dump_stack();
	}

	cmd->trc_flags |= TRC_FLUSH;
	ha->tgt.tgt_ops->free_cmd(cmd);
}

/*
 * ha->hardware_lock supposed to be held on entry. Might drop it, then reaquire
 */
static void qlt_do_ctio_completion(struct scsi_qla_host *vha,
    struct rsp_que *rsp, uint32_t handle, uint32_t status, void *ctio)
{
	struct qla_hw_data *ha = vha->hw;
	struct se_cmd *se_cmd;
	struct qla_tgt_cmd *cmd;
	struct qla_qpair *qpair = rsp->qpair;

	if (handle & CTIO_INTERMEDIATE_HANDLE_MARK) {
		/* That could happen only in case of an error/reset/abort */
		if (status != CTIO_SUCCESS) {
			ql_dbg(ql_dbg_tgt_mgt, vha, 0xf01d,
			    "Intermediate CTIO received"
			    " (status %x)\n", status);
		}
		return;
	}

	cmd = qlt_ctio_to_cmd(vha, rsp, handle, ctio);
	if (cmd == NULL)
		return;

	se_cmd = &cmd->se_cmd;
	cmd->cmd_sent_to_fw = 0;

	qlt_unmap_sg(vha, cmd);

	if (unlikely(status != CTIO_SUCCESS)) {
		switch (status & 0xFFFF) {
		case CTIO_LIP_RESET:
		case CTIO_TARGET_RESET:
		case CTIO_ABORTED:
			/* driver request abort via Terminate exchange */
		case CTIO_TIMEOUT:
		case CTIO_INVALID_RX_ID:
			/* They are OK */
			ql_dbg(ql_dbg_tgt_mgt, vha, 0xf058,
			    "qla_target(%d): CTIO with "
			    "status %#x received, state %x, se_cmd %p, "
			    "(LIP_RESET=e, ABORTED=2, TARGET_RESET=17, "
			    "TIMEOUT=b, INVALID_RX_ID=8)\n", vha->vp_idx,
			    status, cmd->state, se_cmd);
			break;

		case CTIO_PORT_LOGGED_OUT:
		case CTIO_PORT_UNAVAILABLE:
		{
			int logged_out =
				(status & 0xFFFF) == CTIO_PORT_LOGGED_OUT;

			ql_dbg(ql_dbg_tgt_mgt, vha, 0xf059,
			    "qla_target(%d): CTIO with %s status %x "
			    "received (state %x, se_cmd %p)\n", vha->vp_idx,
			    logged_out ? "PORT LOGGED OUT" : "PORT UNAVAILABLE",
			    status, cmd->state, se_cmd);

			if (logged_out && cmd->sess) {
				/*
				 * Session is already logged out, but we need
				 * to notify initiator, who's not aware of this
				 */
				cmd->sess->logout_on_delete = 0;
				cmd->sess->send_els_logo = 1;
				ql_dbg(ql_dbg_disc, vha, 0x20f8,
				    "%s %d %8phC post del sess\n",
				    __func__, __LINE__, cmd->sess->port_name);

				qlt_schedule_sess_for_deletion_lock(cmd->sess);
			}
			break;
		}
		case CTIO_DIF_ERROR: {
			struct ctio_crc_from_fw *crc =
				(struct ctio_crc_from_fw *)ctio;
			ql_dbg(ql_dbg_tgt_mgt, vha, 0xf073,
			    "qla_target(%d): CTIO with DIF_ERROR status %x "
			    "received (state %x, ulp_cmd %p) actual_dif[0x%llx] "
			    "expect_dif[0x%llx]\n",
			    vha->vp_idx, status, cmd->state, se_cmd,
			    *((u64 *)&crc->actual_dif[0]),
			    *((u64 *)&crc->expected_dif[0]));

			qlt_handle_dif_error(qpair, cmd, ctio);
			return;
		}
		default:
			ql_dbg(ql_dbg_tgt_mgt, vha, 0xf05b,
			    "qla_target(%d): CTIO with error status 0x%x received (state %x, se_cmd %p\n",
			    vha->vp_idx, status, cmd->state, se_cmd);
			break;
		}


		/* "cmd->aborted" means
		 * cmd is already aborted/terminated, we don't
		 * need to terminate again.  The exchange is already
		 * cleaned up/freed at FW level.  Just cleanup at driver
		 * level.
		 */
		if ((cmd->state != QLA_TGT_STATE_NEED_DATA) &&
		    (!cmd->aborted)) {
			cmd->trc_flags |= TRC_CTIO_ERR;
			if (qlt_term_ctio_exchange(qpair, ctio, cmd, status))
				return;
		}
	}

	if (cmd->state == QLA_TGT_STATE_PROCESSED) {
		cmd->trc_flags |= TRC_CTIO_DONE;
	} else if (cmd->state == QLA_TGT_STATE_NEED_DATA) {
		cmd->state = QLA_TGT_STATE_DATA_IN;

		if (status == CTIO_SUCCESS)
			cmd->write_data_transferred = 1;

		ha->tgt.tgt_ops->handle_data(cmd);
		return;
	} else if (cmd->aborted) {
		cmd->trc_flags |= TRC_CTIO_ABORTED;
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf01e,
		  "Aborted command %p (tag %lld) finished\n", cmd, se_cmd->tag);
	} else {
		cmd->trc_flags |= TRC_CTIO_STRANGE;
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf05c,
		    "qla_target(%d): A command in state (%d) should "
		    "not return a CTIO complete\n", vha->vp_idx, cmd->state);
	}

	if (unlikely(status != CTIO_SUCCESS) &&
		!cmd->aborted) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf01f, "Finishing failed CTIO\n");
		dump_stack();
	}

	ha->tgt.tgt_ops->free_cmd(cmd);
}

static inline int qlt_get_fcp_task_attr(struct scsi_qla_host *vha,
	uint8_t task_codes)
{
	int fcp_task_attr;

	switch (task_codes) {
	case ATIO_SIMPLE_QUEUE:
		fcp_task_attr = TCM_SIMPLE_TAG;
		break;
	case ATIO_HEAD_OF_QUEUE:
		fcp_task_attr = TCM_HEAD_TAG;
		break;
	case ATIO_ORDERED_QUEUE:
		fcp_task_attr = TCM_ORDERED_TAG;
		break;
	case ATIO_ACA_QUEUE:
		fcp_task_attr = TCM_ACA_TAG;
		break;
	case ATIO_UNTAGGED:
		fcp_task_attr = TCM_SIMPLE_TAG;
		break;
	default:
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf05d,
		    "qla_target: unknown task code %x, use ORDERED instead\n",
		    task_codes);
		fcp_task_attr = TCM_ORDERED_TAG;
		break;
	}

	return fcp_task_attr;
}

static struct fc_port *qlt_make_local_sess(struct scsi_qla_host *,
					uint8_t *);
/*
 * Process context for I/O path into tcm_qla2xxx code
 */
static void __qlt_do_work(struct qla_tgt_cmd *cmd)
{
	scsi_qla_host_t *vha = cmd->vha;
	struct qla_hw_data *ha = vha->hw;
	struct fc_port *sess = cmd->sess;
	struct atio_from_isp *atio = &cmd->atio;
	unsigned char *cdb;
	unsigned long flags;
	uint32_t data_length;
	int ret, fcp_task_attr, data_dir, bidi = 0;
	struct qla_qpair *qpair = cmd->qpair;

	cmd->cmd_in_wq = 0;
	cmd->trc_flags |= TRC_DO_WORK;

	if (cmd->aborted) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf082,
		    "cmd with tag %u is aborted\n",
		    cmd->atio.u.isp24.exchange_addr);
		goto out_term;
	}

	spin_lock_init(&cmd->cmd_lock);
	cdb = &atio->u.isp24.fcp_cmnd.cdb[0];
	cmd->se_cmd.tag = atio->u.isp24.exchange_addr;

	if (atio->u.isp24.fcp_cmnd.rddata &&
	    atio->u.isp24.fcp_cmnd.wrdata) {
		bidi = 1;
		data_dir = DMA_TO_DEVICE;
	} else if (atio->u.isp24.fcp_cmnd.rddata)
		data_dir = DMA_FROM_DEVICE;
	else if (atio->u.isp24.fcp_cmnd.wrdata)
		data_dir = DMA_TO_DEVICE;
	else
		data_dir = DMA_NONE;

	fcp_task_attr = qlt_get_fcp_task_attr(vha,
	    atio->u.isp24.fcp_cmnd.task_attr);
	data_length = be32_to_cpu(get_unaligned((uint32_t *)
	    &atio->u.isp24.fcp_cmnd.add_cdb[
	    atio->u.isp24.fcp_cmnd.add_cdb_len]));

	ret = ha->tgt.tgt_ops->handle_cmd(vha, cmd, cdb, data_length,
				          fcp_task_attr, data_dir, bidi);
	if (ret != 0)
		goto out_term;
	/*
	 * Drop extra session reference from qla_tgt_handle_cmd_for_atio*(
	 */
	spin_lock_irqsave(&ha->tgt.sess_lock, flags);
	ha->tgt.tgt_ops->put_sess(sess);
	spin_unlock_irqrestore(&ha->tgt.sess_lock, flags);
	return;

out_term:
	ql_dbg(ql_dbg_io, vha, 0x3060, "Terminating work cmd %p", cmd);
	/*
	 * cmd has not sent to target yet, so pass NULL as the second
	 * argument to qlt_send_term_exchange() and free the memory here.
	 */
	cmd->trc_flags |= TRC_DO_WORK_ERR;
	spin_lock_irqsave(qpair->qp_lock_ptr, flags);
	qlt_send_term_exchange(qpair, NULL, &cmd->atio, 1, 0);

	qlt_decr_num_pend_cmds(vha);
	percpu_ida_free(&sess->se_sess->sess_tag_pool, cmd->se_cmd.map_tag);
	spin_unlock_irqrestore(qpair->qp_lock_ptr, flags);

	spin_lock_irqsave(&ha->tgt.sess_lock, flags);
	ha->tgt.tgt_ops->put_sess(sess);
	spin_unlock_irqrestore(&ha->tgt.sess_lock, flags);
}

static void qlt_do_work(struct work_struct *work)
{
	struct qla_tgt_cmd *cmd = container_of(work, struct qla_tgt_cmd, work);
	scsi_qla_host_t *vha = cmd->vha;
	unsigned long flags;

	spin_lock_irqsave(&vha->cmd_list_lock, flags);
	list_del(&cmd->cmd_list);
	spin_unlock_irqrestore(&vha->cmd_list_lock, flags);

	__qlt_do_work(cmd);
}

void qlt_clr_qp_table(struct scsi_qla_host *vha)
{
	unsigned long flags;
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt *tgt = vha->vha_tgt.qla_tgt;
	void *node;
	u64 key = 0;

	ql_log(ql_log_info, vha, 0x706c,
	    "User update Number of Active Qpairs %d\n",
	    ha->tgt.num_act_qpairs);

	spin_lock_irqsave(&ha->tgt.atio_lock, flags);

	btree_for_each_safe64(&tgt->lun_qpair_map, key, node)
		btree_remove64(&tgt->lun_qpair_map, key);

	ha->base_qpair->lun_cnt = 0;
	for (key = 0; key < ha->max_qpairs; key++)
		if (ha->queue_pair_map[key])
			ha->queue_pair_map[key]->lun_cnt = 0;

	spin_unlock_irqrestore(&ha->tgt.atio_lock, flags);
}

static void qlt_assign_qpair(struct scsi_qla_host *vha,
	struct qla_tgt_cmd *cmd)
{
	struct qla_qpair *qpair, *qp;
	struct qla_tgt *tgt = vha->vha_tgt.qla_tgt;
	struct qla_qpair_hint *h;

	if (vha->flags.qpairs_available) {
		h = btree_lookup64(&tgt->lun_qpair_map, cmd->unpacked_lun);
		if (unlikely(!h)) {
			/* spread lun to qpair ratio evently */
			int lcnt = 0, rc;
			struct scsi_qla_host *base_vha =
				pci_get_drvdata(vha->hw->pdev);

			qpair = vha->hw->base_qpair;
			if (qpair->lun_cnt == 0) {
				qpair->lun_cnt++;
				h = qla_qpair_to_hint(tgt, qpair);
				BUG_ON(!h);
				rc = btree_insert64(&tgt->lun_qpair_map,
					cmd->unpacked_lun, h, GFP_ATOMIC);
				if (rc) {
					qpair->lun_cnt--;
					ql_log(ql_log_info, vha, 0xd037,
					    "Unable to insert lun %llx into lun_qpair_map\n",
					    cmd->unpacked_lun);
				}
				goto out;
			} else {
				lcnt = qpair->lun_cnt;
			}

			h = NULL;
			list_for_each_entry(qp, &base_vha->qp_list,
			    qp_list_elem) {
				if (qp->lun_cnt == 0) {
					qp->lun_cnt++;
					h = qla_qpair_to_hint(tgt, qp);
					BUG_ON(!h);
					rc = btree_insert64(&tgt->lun_qpair_map,
					    cmd->unpacked_lun, h, GFP_ATOMIC);
					if (rc) {
						qp->lun_cnt--;
						ql_log(ql_log_info, vha, 0xd038,
							"Unable to insert lun %llx into lun_qpair_map\n",
							cmd->unpacked_lun);
					}
					qpair = qp;
					goto out;
				} else {
					if (qp->lun_cnt < lcnt) {
						lcnt = qp->lun_cnt;
						qpair = qp;
						continue;
					}
				}
			}
			BUG_ON(!qpair);
			qpair->lun_cnt++;
			h = qla_qpair_to_hint(tgt, qpair);
			BUG_ON(!h);
			rc = btree_insert64(&tgt->lun_qpair_map,
				cmd->unpacked_lun, h, GFP_ATOMIC);
			if (rc) {
				qpair->lun_cnt--;
				ql_log(ql_log_info, vha, 0xd039,
				   "Unable to insert lun %llx into lun_qpair_map\n",
				   cmd->unpacked_lun);
			}
		}
	} else {
		h = &tgt->qphints[0];
	}
out:
	cmd->qpair = h->qpair;
	cmd->se_cmd.cpuid = h->cpuid;
}

static struct qla_tgt_cmd *qlt_get_tag(scsi_qla_host_t *vha,
				       struct fc_port *sess,
				       struct atio_from_isp *atio)
{
	struct se_session *se_sess = sess->se_sess;
	struct qla_tgt_cmd *cmd;
	int tag;

	tag = percpu_ida_alloc(&se_sess->sess_tag_pool, TASK_RUNNING);
	if (tag < 0)
		return NULL;

	cmd = &((struct qla_tgt_cmd *)se_sess->sess_cmd_map)[tag];
	memset(cmd, 0, sizeof(struct qla_tgt_cmd));
	cmd->cmd_type = TYPE_TGT_CMD;
	memcpy(&cmd->atio, atio, sizeof(*atio));
	cmd->state = QLA_TGT_STATE_NEW;
	cmd->tgt = vha->vha_tgt.qla_tgt;
	qlt_incr_num_pend_cmds(vha);
	cmd->vha = vha;
	cmd->se_cmd.map_tag = tag;
	cmd->sess = sess;
	cmd->loop_id = sess->loop_id;
	cmd->conf_compl_supported = sess->conf_compl_supported;

	cmd->trc_flags = 0;
	cmd->jiffies_at_alloc = get_jiffies_64();

	cmd->unpacked_lun = scsilun_to_int(
	    (struct scsi_lun *)&atio->u.isp24.fcp_cmnd.lun);
	qlt_assign_qpair(vha, cmd);
	cmd->reset_count = vha->hw->base_qpair->chip_reset;
	cmd->vp_idx = vha->vp_idx;

	return cmd;
}

static void qlt_create_sess_from_atio(struct work_struct *work)
{
	struct qla_tgt_sess_op *op = container_of(work,
					struct qla_tgt_sess_op, work);
	scsi_qla_host_t *vha = op->vha;
	struct qla_hw_data *ha = vha->hw;
	struct fc_port *sess;
	struct qla_tgt_cmd *cmd;
	unsigned long flags;
	uint8_t *s_id = op->atio.u.isp24.fcp_hdr.s_id;

	spin_lock_irqsave(&vha->cmd_list_lock, flags);
	list_del(&op->cmd_list);
	spin_unlock_irqrestore(&vha->cmd_list_lock, flags);

	if (op->aborted) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf083,
		    "sess_op with tag %u is aborted\n",
		    op->atio.u.isp24.exchange_addr);
		goto out_term;
	}

	ql_dbg(ql_dbg_tgt_mgt, vha, 0xf022,
	    "qla_target(%d): Unable to find wwn login"
	    " (s_id %x:%x:%x), trying to create it manually\n",
	    vha->vp_idx, s_id[0], s_id[1], s_id[2]);

	if (op->atio.u.raw.entry_count > 1) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf023,
		    "Dropping multy entry atio %p\n", &op->atio);
		goto out_term;
	}

	sess = qlt_make_local_sess(vha, s_id);
	/* sess has an extra creation ref. */

	if (!sess)
		goto out_term;
	/*
	 * Now obtain a pre-allocated session tag using the original op->atio
	 * packet header, and dispatch into __qlt_do_work() using the existing
	 * process context.
	 */
	cmd = qlt_get_tag(vha, sess, &op->atio);
	if (!cmd) {
		struct qla_qpair *qpair = ha->base_qpair;

		spin_lock_irqsave(qpair->qp_lock_ptr, flags);
		qlt_send_busy(qpair, &op->atio, SAM_STAT_BUSY);
		spin_unlock_irqrestore(qpair->qp_lock_ptr, flags);

		spin_lock_irqsave(&ha->tgt.sess_lock, flags);
		ha->tgt.tgt_ops->put_sess(sess);
		spin_unlock_irqrestore(&ha->tgt.sess_lock, flags);
		kfree(op);
		return;
	}

	/*
	 * __qlt_do_work() will call qlt_put_sess() to release
	 * the extra reference taken above by qlt_make_local_sess()
	 */
	__qlt_do_work(cmd);
	kfree(op);
	return;
out_term:
	qlt_send_term_exchange(vha->hw->base_qpair, NULL, &op->atio, 0, 0);
	kfree(op);
}

/* ha->hardware_lock supposed to be held on entry */
static int qlt_handle_cmd_for_atio(struct scsi_qla_host *vha,
	struct atio_from_isp *atio)
{
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt *tgt = vha->vha_tgt.qla_tgt;
	struct fc_port *sess;
	struct qla_tgt_cmd *cmd;
	unsigned long flags;

	if (unlikely(tgt->tgt_stop)) {
		ql_dbg(ql_dbg_io, vha, 0x3061,
		    "New command while device %p is shutting down\n", tgt);
		return -EFAULT;
	}

	sess = ha->tgt.tgt_ops->find_sess_by_s_id(vha, atio->u.isp24.fcp_hdr.s_id);
	if (unlikely(!sess)) {
		struct qla_tgt_sess_op *op = kzalloc(sizeof(struct qla_tgt_sess_op),
						     GFP_ATOMIC);
		if (!op)
			return -ENOMEM;

		memcpy(&op->atio, atio, sizeof(*atio));
		op->vha = vha;

		spin_lock_irqsave(&vha->cmd_list_lock, flags);
		list_add_tail(&op->cmd_list, &vha->qla_sess_op_cmd_list);
		spin_unlock_irqrestore(&vha->cmd_list_lock, flags);

		INIT_WORK(&op->work, qlt_create_sess_from_atio);
		queue_work(qla_tgt_wq, &op->work);
		return 0;
	}

	/* Another WWN used to have our s_id. Our PLOGI scheduled its
	 * session deletion, but it's still in sess_del_work wq */
	if (sess->deleted) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf002,
		    "New command while old session %p is being deleted\n",
		    sess);
		return -EFAULT;
	}

	/*
	 * Do kref_get() before returning + dropping qla_hw_data->hardware_lock.
	 */
	if (!kref_get_unless_zero(&sess->sess_kref)) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf004,
		    "%s: kref_get fail, %8phC oxid %x \n",
		    __func__, sess->port_name,
		     be16_to_cpu(atio->u.isp24.fcp_hdr.ox_id));
		return -EFAULT;
	}

	cmd = qlt_get_tag(vha, sess, atio);
	if (!cmd) {
		ql_dbg(ql_dbg_io, vha, 0x3062,
		    "qla_target(%d): Allocation of cmd failed\n", vha->vp_idx);
		spin_lock_irqsave(&ha->tgt.sess_lock, flags);
		ha->tgt.tgt_ops->put_sess(sess);
		spin_unlock_irqrestore(&ha->tgt.sess_lock, flags);
		return -ENOMEM;
	}

	cmd->cmd_in_wq = 1;
	cmd->trc_flags |= TRC_NEW_CMD;

	spin_lock_irqsave(&vha->cmd_list_lock, flags);
	list_add_tail(&cmd->cmd_list, &vha->qla_cmd_list);
	spin_unlock_irqrestore(&vha->cmd_list_lock, flags);

	INIT_WORK(&cmd->work, qlt_do_work);
	if (vha->flags.qpairs_available) {
		queue_work_on(cmd->se_cmd.cpuid, qla_tgt_wq, &cmd->work);
	} else if (ha->msix_count) {
		if (cmd->atio.u.isp24.fcp_cmnd.rddata)
			queue_work_on(smp_processor_id(), qla_tgt_wq,
			    &cmd->work);
		else
			queue_work_on(cmd->se_cmd.cpuid, qla_tgt_wq,
			    &cmd->work);
	} else {
		queue_work(qla_tgt_wq, &cmd->work);
	}

	return 0;
}

/* ha->hardware_lock supposed to be held on entry */
static int qlt_issue_task_mgmt(struct fc_port *sess, u64 lun,
	int fn, void *iocb, int flags)
{
	struct scsi_qla_host *vha = sess->vha;
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt_mgmt_cmd *mcmd;
	struct atio_from_isp *a = (struct atio_from_isp *)iocb;
	int res;

	mcmd = mempool_alloc(qla_tgt_mgmt_cmd_mempool, GFP_ATOMIC);
	if (!mcmd) {
		ql_dbg(ql_dbg_tgt_tmr, vha, 0x10009,
		    "qla_target(%d): Allocation of management "
		    "command failed, some commands and their data could "
		    "leak\n", vha->vp_idx);
		return -ENOMEM;
	}
	memset(mcmd, 0, sizeof(*mcmd));
	mcmd->sess = sess;

	if (iocb) {
		memcpy(&mcmd->orig_iocb.imm_ntfy, iocb,
		    sizeof(mcmd->orig_iocb.imm_ntfy));
	}
	mcmd->tmr_func = fn;
	mcmd->flags = flags;
	mcmd->reset_count = ha->base_qpair->chip_reset;
	mcmd->qpair = ha->base_qpair;
	mcmd->vha = vha;

	switch (fn) {
	case QLA_TGT_LUN_RESET:
	    abort_cmds_for_lun(vha, lun, a->u.isp24.fcp_hdr.s_id);
	    break;
	}

	res = ha->tgt.tgt_ops->handle_tmr(mcmd, lun, mcmd->tmr_func, 0);
	if (res != 0) {
		ql_dbg(ql_dbg_tgt_tmr, vha, 0x1000b,
		    "qla_target(%d): tgt.tgt_ops->handle_tmr() failed: %d\n",
		    sess->vha->vp_idx, res);
		mempool_free(mcmd, qla_tgt_mgmt_cmd_mempool);
		return -EFAULT;
	}

	return 0;
}

/* ha->hardware_lock supposed to be held on entry */
static int qlt_handle_task_mgmt(struct scsi_qla_host *vha, void *iocb)
{
	struct atio_from_isp *a = (struct atio_from_isp *)iocb;
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt *tgt;
	struct fc_port *sess;
	u64 unpacked_lun;
	int fn;
	unsigned long flags;

	tgt = vha->vha_tgt.qla_tgt;

	fn = a->u.isp24.fcp_cmnd.task_mgmt_flags;

	spin_lock_irqsave(&ha->tgt.sess_lock, flags);
	sess = ha->tgt.tgt_ops->find_sess_by_s_id(vha,
	    a->u.isp24.fcp_hdr.s_id);
	spin_unlock_irqrestore(&ha->tgt.sess_lock, flags);

	unpacked_lun =
	    scsilun_to_int((struct scsi_lun *)&a->u.isp24.fcp_cmnd.lun);

	if (!sess) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf024,
		    "qla_target(%d): task mgmt fn 0x%x for "
		    "non-existant session\n", vha->vp_idx, fn);
		return qlt_sched_sess_work(tgt, QLA_TGT_SESS_WORK_TM, iocb,
		    sizeof(struct atio_from_isp));
	}

	if (sess->deleted)
		return -EFAULT;

	return qlt_issue_task_mgmt(sess, unpacked_lun, fn, iocb, 0);
}

/* ha->hardware_lock supposed to be held on entry */
static int __qlt_abort_task(struct scsi_qla_host *vha,
	struct imm_ntfy_from_isp *iocb, struct fc_port *sess)
{
	struct atio_from_isp *a = (struct atio_from_isp *)iocb;
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt_mgmt_cmd *mcmd;
	u64 unpacked_lun;
	int rc;

	mcmd = mempool_alloc(qla_tgt_mgmt_cmd_mempool, GFP_ATOMIC);
	if (mcmd == NULL) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf05f,
		    "qla_target(%d): %s: Allocation of ABORT cmd failed\n",
		    vha->vp_idx, __func__);
		return -ENOMEM;
	}
	memset(mcmd, 0, sizeof(*mcmd));

	mcmd->sess = sess;
	memcpy(&mcmd->orig_iocb.imm_ntfy, iocb,
	    sizeof(mcmd->orig_iocb.imm_ntfy));

	unpacked_lun =
	    scsilun_to_int((struct scsi_lun *)&a->u.isp24.fcp_cmnd.lun);
	mcmd->reset_count = ha->base_qpair->chip_reset;
	mcmd->tmr_func = QLA_TGT_2G_ABORT_TASK;
	mcmd->qpair = ha->base_qpair;

	rc = ha->tgt.tgt_ops->handle_tmr(mcmd, unpacked_lun, mcmd->tmr_func,
	    le16_to_cpu(iocb->u.isp2x.seq_id));
	if (rc != 0) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf060,
		    "qla_target(%d): tgt_ops->handle_tmr() failed: %d\n",
		    vha->vp_idx, rc);
		mempool_free(mcmd, qla_tgt_mgmt_cmd_mempool);
		return -EFAULT;
	}

	return 0;
}

/* ha->hardware_lock supposed to be held on entry */
static int qlt_abort_task(struct scsi_qla_host *vha,
	struct imm_ntfy_from_isp *iocb)
{
	struct qla_hw_data *ha = vha->hw;
	struct fc_port *sess;
	int loop_id;
	unsigned long flags;

	loop_id = GET_TARGET_ID(ha, (struct atio_from_isp *)iocb);

	spin_lock_irqsave(&ha->tgt.sess_lock, flags);
	sess = ha->tgt.tgt_ops->find_sess_by_loop_id(vha, loop_id);
	spin_unlock_irqrestore(&ha->tgt.sess_lock, flags);

	if (sess == NULL) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf025,
		    "qla_target(%d): task abort for unexisting "
		    "session\n", vha->vp_idx);
		return qlt_sched_sess_work(vha->vha_tgt.qla_tgt,
		    QLA_TGT_SESS_WORK_ABORT, iocb, sizeof(*iocb));
	}

	return __qlt_abort_task(vha, iocb, sess);
}

void qlt_logo_completion_handler(fc_port_t *fcport, int rc)
{
	if (rc != MBS_COMMAND_COMPLETE) {
		ql_dbg(ql_dbg_tgt_mgt, fcport->vha, 0xf093,
			"%s: se_sess %p / sess %p from"
			" port %8phC loop_id %#04x s_id %02x:%02x:%02x"
			" LOGO failed: %#x\n",
			__func__,
			fcport->se_sess,
			fcport,
			fcport->port_name, fcport->loop_id,
			fcport->d_id.b.domain, fcport->d_id.b.area,
			fcport->d_id.b.al_pa, rc);
	}

	fcport->logout_completed = 1;
}

/*
* ha->hardware_lock supposed to be held on entry (to protect tgt->sess_list)
*
* Schedules sessions with matching port_id/loop_id but different wwn for
* deletion. Returns existing session with matching wwn if present.
* Null otherwise.
*/
struct fc_port *
qlt_find_sess_invalidate_other(scsi_qla_host_t *vha, uint64_t wwn,
    port_id_t port_id, uint16_t loop_id, struct fc_port **conflict_sess)
{
	struct fc_port *sess = NULL, *other_sess;
	uint64_t other_wwn;

	*conflict_sess = NULL;

	list_for_each_entry(other_sess, &vha->vp_fcports, list) {

		other_wwn = wwn_to_u64(other_sess->port_name);

		if (wwn == other_wwn) {
			WARN_ON(sess);
			sess = other_sess;
			continue;
		}

		/* find other sess with nport_id collision */
		if (port_id.b24 == other_sess->d_id.b24) {
			if (loop_id != other_sess->loop_id) {
				ql_dbg(ql_dbg_tgt_tmr, vha, 0x1000c,
				    "Invalidating sess %p loop_id %d wwn %llx.\n",
				    other_sess, other_sess->loop_id, other_wwn);

				/*
				 * logout_on_delete is set by default, but another
				 * session that has the same s_id/loop_id combo
				 * might have cleared it when requested this session
				 * deletion, so don't touch it
				 */
				qlt_schedule_sess_for_deletion(other_sess, true);
			} else {
				/*
				 * Another wwn used to have our s_id/loop_id
				 * kill the session, but don't free the loop_id
				 */
				ql_dbg(ql_dbg_tgt_tmr, vha, 0xf01b,
				    "Invalidating sess %p loop_id %d wwn %llx.\n",
				    other_sess, other_sess->loop_id, other_wwn);

				other_sess->keep_nport_handle = 1;
				if (other_sess->disc_state != DSC_DELETED)
					*conflict_sess = other_sess;
				qlt_schedule_sess_for_deletion(other_sess,
				    true);
			}
			continue;
		}

		/* find other sess with nport handle collision */
		if ((loop_id == other_sess->loop_id) &&
			(loop_id != FC_NO_LOOP_ID)) {
			ql_dbg(ql_dbg_tgt_tmr, vha, 0x1000d,
			       "Invalidating sess %p loop_id %d wwn %llx.\n",
			       other_sess, other_sess->loop_id, other_wwn);

			/* Same loop_id but different s_id
			 * Ok to kill and logout */
			qlt_schedule_sess_for_deletion(other_sess, true);
		}
	}

	return sess;
}

/* Abort any commands for this s_id waiting on qla_tgt_wq workqueue */
static int abort_cmds_for_s_id(struct scsi_qla_host *vha, port_id_t *s_id)
{
	struct qla_tgt_sess_op *op;
	struct qla_tgt_cmd *cmd;
	uint32_t key;
	int count = 0;
	unsigned long flags;

	key = (((u32)s_id->b.domain << 16) |
	       ((u32)s_id->b.area   <<  8) |
	       ((u32)s_id->b.al_pa));

	spin_lock_irqsave(&vha->cmd_list_lock, flags);
	list_for_each_entry(op, &vha->qla_sess_op_cmd_list, cmd_list) {
		uint32_t op_key = sid_to_key(op->atio.u.isp24.fcp_hdr.s_id);

		if (op_key == key) {
			op->aborted = true;
			count++;
		}
	}

	list_for_each_entry(op, &vha->unknown_atio_list, cmd_list) {
		uint32_t op_key = sid_to_key(op->atio.u.isp24.fcp_hdr.s_id);
		if (op_key == key) {
			op->aborted = true;
			count++;
		}
	}

	list_for_each_entry(cmd, &vha->qla_cmd_list, cmd_list) {
		uint32_t cmd_key = sid_to_key(cmd->atio.u.isp24.fcp_hdr.s_id);
		if (cmd_key == key) {
			cmd->aborted = 1;
			count++;
		}
	}
	spin_unlock_irqrestore(&vha->cmd_list_lock, flags);

	return count;
}

/*
 * ha->hardware_lock supposed to be held on entry. Might drop it, then reaquire
 */
static int qlt_24xx_handle_els(struct scsi_qla_host *vha,
	struct imm_ntfy_from_isp *iocb)
{
	struct qla_tgt *tgt = vha->vha_tgt.qla_tgt;
	struct qla_hw_data *ha = vha->hw;
	struct fc_port *sess = NULL, *conflict_sess = NULL;
	uint64_t wwn;
	port_id_t port_id;
	uint16_t loop_id;
	uint16_t wd3_lo;
	int res = 0;
	struct qlt_plogi_ack_t *pla;
	unsigned long flags;

	wwn = wwn_to_u64(iocb->u.isp24.port_name);

	port_id.b.domain = iocb->u.isp24.port_id[2];
	port_id.b.area   = iocb->u.isp24.port_id[1];
	port_id.b.al_pa  = iocb->u.isp24.port_id[0];
	port_id.b.rsvd_1 = 0;

	loop_id = le16_to_cpu(iocb->u.isp24.nport_handle);

	ql_dbg(ql_dbg_disc, vha, 0xf026,
	    "qla_target(%d): Port ID: %02x:%02x:%02x ELS opcode: 0x%02x lid %d %8phC\n",
	    vha->vp_idx, iocb->u.isp24.port_id[2],
		iocb->u.isp24.port_id[1], iocb->u.isp24.port_id[0],
		   iocb->u.isp24.status_subcode, loop_id,
		iocb->u.isp24.port_name);

	/* res = 1 means ack at the end of thread
	 * res = 0 means ack async/later.
	 */
	switch (iocb->u.isp24.status_subcode) {
	case ELS_PLOGI:

		/* Mark all stale commands in qla_tgt_wq for deletion */
		abort_cmds_for_s_id(vha, &port_id);

		if (wwn) {
			spin_lock_irqsave(&tgt->ha->tgt.sess_lock, flags);
			sess = qlt_find_sess_invalidate_other(vha, wwn,
				port_id, loop_id, &conflict_sess);
			spin_unlock_irqrestore(&tgt->ha->tgt.sess_lock, flags);
		}

		if (IS_SW_RESV_ADDR(port_id)) {
			res = 1;
			break;
		}

		pla = qlt_plogi_ack_find_add(vha, &port_id, iocb);
		if (!pla) {
			qlt_send_term_imm_notif(vha, iocb, 1);
			break;
		}

		res = 0;

		if (conflict_sess) {
			conflict_sess->login_gen++;
			qlt_plogi_ack_link(vha, pla, conflict_sess,
				QLT_PLOGI_LINK_CONFLICT);
		}

		if (!sess) {
			pla->ref_count++;
			qla24xx_post_newsess_work(vha, &port_id,
				iocb->u.isp24.port_name, pla);
			res = 0;
			break;
		}

		qlt_plogi_ack_link(vha, pla, sess, QLT_PLOGI_LINK_SAME_WWN);
		sess->fw_login_state = DSC_LS_PLOGI_PEND;
		sess->d_id = port_id;
		sess->login_gen++;

		ql_dbg(ql_dbg_disc, vha, 0x20f9,
		    "%s %d %8phC  DS %d\n",
		    __func__, __LINE__, sess->port_name, sess->disc_state);

		switch (sess->disc_state) {
		case DSC_DELETED:
			qlt_plogi_ack_unref(vha, pla);
			break;

		default:
			/*
			 * Under normal circumstances we want to release nport handle
			 * during LOGO process to avoid nport handle leaks inside FW.
			 * The exception is when LOGO is done while another PLOGI with
			 * the same nport handle is waiting as might be the case here.
			 * Note: there is always a possibily of a race where session
			 * deletion has already started for other reasons (e.g. ACL
			 * removal) and now PLOGI arrives:
			 * 1. if PLOGI arrived in FW after nport handle has been freed,
			 *    FW must have assigned this PLOGI a new/same handle and we
			 *    can proceed ACK'ing it as usual when session deletion
			 *    completes.
			 * 2. if PLOGI arrived in FW before LOGO with LCF_FREE_NPORT
			 *    bit reached it, the handle has now been released. We'll
			 *    get an error when we ACK this PLOGI. Nothing will be sent
			 *    back to initiator. Initiator should eventually retry
			 *    PLOGI and situation will correct itself.
			 */
			sess->keep_nport_handle = ((sess->loop_id == loop_id) &&
			   (sess->d_id.b24 == port_id.b24));

			ql_dbg(ql_dbg_disc, vha, 0x20f9,
			    "%s %d %8phC post del sess\n",
			    __func__, __LINE__, sess->port_name);


			qlt_schedule_sess_for_deletion_lock(sess);
			break;
		}

		break;

	case ELS_PRLI:
		wd3_lo = le16_to_cpu(iocb->u.isp24.u.prli.wd3_lo);

		if (wwn) {
			spin_lock_irqsave(&tgt->ha->tgt.sess_lock, flags);
			sess = qlt_find_sess_invalidate_other(vha, wwn, port_id,
				loop_id, &conflict_sess);
			spin_unlock_irqrestore(&tgt->ha->tgt.sess_lock, flags);
		}

		if (conflict_sess) {
			switch (conflict_sess->disc_state) {
			case DSC_DELETED:
			case DSC_DELETE_PEND:
				break;
			default:
				ql_dbg(ql_dbg_tgt_mgt, vha, 0xf09b,
				    "PRLI with conflicting sess %p port %8phC\n",
				    conflict_sess, conflict_sess->port_name);
				conflict_sess->fw_login_state =
				    DSC_LS_PORT_UNAVAIL;
				qlt_send_term_imm_notif(vha, iocb, 1);
				res = 0;
				break;
			}
		}

		if (sess != NULL) {
			if (sess->fw_login_state != DSC_LS_PLOGI_PEND &&
			    sess->fw_login_state != DSC_LS_PLOGI_COMP) {
				/*
				 * Impatient initiator sent PRLI before last
				 * PLOGI could finish. Will force him to re-try,
				 * while last one finishes.
				 */
				ql_log(ql_log_warn, sess->vha, 0xf095,
				    "sess %p PRLI received, before plogi ack.\n",
				    sess);
				qlt_send_term_imm_notif(vha, iocb, 1);
				res = 0;
				break;
			}

			/*
			 * This shouldn't happen under normal circumstances,
			 * since we have deleted the old session during PLOGI
			 */
			ql_dbg(ql_dbg_tgt_mgt, vha, 0xf096,
			    "PRLI (loop_id %#04x) for existing sess %p (loop_id %#04x)\n",
			    sess->loop_id, sess, iocb->u.isp24.nport_handle);

			sess->local = 0;
			sess->loop_id = loop_id;
			sess->d_id = port_id;
			sess->fw_login_state = DSC_LS_PRLI_PEND;

			if (wd3_lo & BIT_7)
				sess->conf_compl_supported = 1;

			if ((wd3_lo & BIT_4) == 0)
				sess->port_type = FCT_INITIATOR;
			else
				sess->port_type = FCT_TARGET;
		}
		res = 1; /* send notify ack */

		/* Make session global (not used in fabric mode) */
		if (ha->current_topology != ISP_CFG_F) {
			if (sess) {
				ql_dbg(ql_dbg_disc, vha, 0x20fa,
				    "%s %d %8phC post nack\n",
				    __func__, __LINE__, sess->port_name);
				qla24xx_post_nack_work(vha, sess, iocb,
					SRB_NACK_PRLI);
				res = 0;
			} else {
				set_bit(LOOP_RESYNC_NEEDED, &vha->dpc_flags);
				set_bit(LOCAL_LOOP_UPDATE, &vha->dpc_flags);
				qla2xxx_wake_dpc(vha);
			}
		} else {
			if (sess) {
				ql_dbg(ql_dbg_disc, vha, 0x20fb,
				    "%s %d %8phC post nack\n",
				    __func__, __LINE__, sess->port_name);
				qla24xx_post_nack_work(vha, sess, iocb,
					SRB_NACK_PRLI);
				res = 0;
			}
		}
		break;

	case ELS_TPRLO:
		if (le16_to_cpu(iocb->u.isp24.flags) &
			NOTIFY24XX_FLAGS_GLOBAL_TPRLO) {
			loop_id = 0xFFFF;
			qlt_reset(vha, iocb, QLA_TGT_NEXUS_LOSS);
			res = 1;
			break;
		}
		/* fall through */
	case ELS_LOGO:
	case ELS_PRLO:
		spin_lock_irqsave(&ha->tgt.sess_lock, flags);
		sess = qla2x00_find_fcport_by_loopid(vha, loop_id);
		spin_unlock_irqrestore(&ha->tgt.sess_lock, flags);

		if (sess) {
			sess->login_gen++;
			sess->fw_login_state = DSC_LS_LOGO_PEND;
			sess->logo_ack_needed = 1;
			memcpy(sess->iocb, iocb, IOCB_SIZE);
		}

		res = qlt_reset(vha, iocb, QLA_TGT_NEXUS_LOSS_SESS);

		ql_dbg(ql_dbg_disc, vha, 0x20fc,
		    "%s: logo %llx res %d sess %p ",
		    __func__, wwn, res, sess);
		if (res == 0) {
			/*
			 * cmd went upper layer, look for qlt_xmit_tm_rsp()
			 * for LOGO_ACK & sess delete
			 */
			BUG_ON(!sess);
			res = 0;
		} else {
			/* cmd did not go to upper layer. */
			if (sess) {
				qlt_schedule_sess_for_deletion_lock(sess);
				res = 0;
			}
			/* else logo will be ack */
		}
		break;
	case ELS_PDISC:
	case ELS_ADISC:
	{
		struct qla_tgt *tgt = vha->vha_tgt.qla_tgt;
		if (tgt->link_reinit_iocb_pending) {
			qlt_send_notify_ack(ha->base_qpair,
			    &tgt->link_reinit_iocb, 0, 0, 0, 0, 0, 0);
			tgt->link_reinit_iocb_pending = 0;
		}

		sess = qla2x00_find_fcport_by_wwpn(vha,
		    iocb->u.isp24.port_name, 1);
		if (sess) {
			ql_dbg(ql_dbg_disc, vha, 0x20fd,
				"sess %p lid %d|%d DS %d LS %d\n",
				sess, sess->loop_id, loop_id,
				sess->disc_state, sess->fw_login_state);
		}

		res = 1; /* send notify ack */
		break;
	}

	case ELS_FLOGI:	/* should never happen */
	default:
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf061,
		    "qla_target(%d): Unsupported ELS command %x "
		    "received\n", vha->vp_idx, iocb->u.isp24.status_subcode);
		res = qlt_reset(vha, iocb, QLA_TGT_NEXUS_LOSS_SESS);
		break;
	}

	return res;
}

/*
 * ha->hardware_lock supposed to be held on entry. Might drop it, then reaquire
 */
static void qlt_handle_imm_notify(struct scsi_qla_host *vha,
	struct imm_ntfy_from_isp *iocb)
{
	struct qla_hw_data *ha = vha->hw;
	uint32_t add_flags = 0;
	int send_notify_ack = 1;
	uint16_t status;

	status = le16_to_cpu(iocb->u.isp2x.status);
	switch (status) {
	case IMM_NTFY_LIP_RESET:
	{
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf032,
		    "qla_target(%d): LIP reset (loop %#x), subcode %x\n",
		    vha->vp_idx, le16_to_cpu(iocb->u.isp24.nport_handle),
		    iocb->u.isp24.status_subcode);

		if (qlt_reset(vha, iocb, QLA_TGT_ABORT_ALL) == 0)
			send_notify_ack = 0;
		break;
	}

	case IMM_NTFY_LIP_LINK_REINIT:
	{
		struct qla_tgt *tgt = vha->vha_tgt.qla_tgt;
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf033,
		    "qla_target(%d): LINK REINIT (loop %#x, "
		    "subcode %x)\n", vha->vp_idx,
		    le16_to_cpu(iocb->u.isp24.nport_handle),
		    iocb->u.isp24.status_subcode);
		if (tgt->link_reinit_iocb_pending) {
			qlt_send_notify_ack(ha->base_qpair,
			    &tgt->link_reinit_iocb, 0, 0, 0, 0, 0, 0);
		}
		memcpy(&tgt->link_reinit_iocb, iocb, sizeof(*iocb));
		tgt->link_reinit_iocb_pending = 1;
		/*
		 * QLogic requires to wait after LINK REINIT for possible
		 * PDISC or ADISC ELS commands
		 */
		send_notify_ack = 0;
		break;
	}

	case IMM_NTFY_PORT_LOGOUT:
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf034,
		    "qla_target(%d): Port logout (loop "
		    "%#x, subcode %x)\n", vha->vp_idx,
		    le16_to_cpu(iocb->u.isp24.nport_handle),
		    iocb->u.isp24.status_subcode);

		if (qlt_reset(vha, iocb, QLA_TGT_NEXUS_LOSS_SESS) == 0)
			send_notify_ack = 0;
		/* The sessions will be cleared in the callback, if needed */
		break;

	case IMM_NTFY_GLBL_TPRLO:
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf035,
		    "qla_target(%d): Global TPRLO (%x)\n", vha->vp_idx, status);
		if (qlt_reset(vha, iocb, QLA_TGT_NEXUS_LOSS) == 0)
			send_notify_ack = 0;
		/* The sessions will be cleared in the callback, if needed */
		break;

	case IMM_NTFY_PORT_CONFIG:
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf036,
		    "qla_target(%d): Port config changed (%x)\n", vha->vp_idx,
		    status);
		if (qlt_reset(vha, iocb, QLA_TGT_ABORT_ALL) == 0)
			send_notify_ack = 0;
		/* The sessions will be cleared in the callback, if needed */
		break;

	case IMM_NTFY_GLBL_LOGO:
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf06a,
		    "qla_target(%d): Link failure detected\n",
		    vha->vp_idx);
		/* I_T nexus loss */
		if (qlt_reset(vha, iocb, QLA_TGT_NEXUS_LOSS) == 0)
			send_notify_ack = 0;
		break;

	case IMM_NTFY_IOCB_OVERFLOW:
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf06b,
		    "qla_target(%d): Cannot provide requested "
		    "capability (IOCB overflowed the immediate notify "
		    "resource count)\n", vha->vp_idx);
		break;

	case IMM_NTFY_ABORT_TASK:
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf037,
		    "qla_target(%d): Abort Task (S %08x I %#x -> "
		    "L %#x)\n", vha->vp_idx,
		    le16_to_cpu(iocb->u.isp2x.seq_id),
		    GET_TARGET_ID(ha, (struct atio_from_isp *)iocb),
		    le16_to_cpu(iocb->u.isp2x.lun));
		if (qlt_abort_task(vha, iocb) == 0)
			send_notify_ack = 0;
		break;

	case IMM_NTFY_RESOURCE:
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf06c,
		    "qla_target(%d): Out of resources, host %ld\n",
		    vha->vp_idx, vha->host_no);
		break;

	case IMM_NTFY_MSG_RX:
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf038,
		    "qla_target(%d): Immediate notify task %x\n",
		    vha->vp_idx, iocb->u.isp2x.task_flags);
		if (qlt_handle_task_mgmt(vha, iocb) == 0)
			send_notify_ack = 0;
		break;

	case IMM_NTFY_ELS:
		if (qlt_24xx_handle_els(vha, iocb) == 0)
			send_notify_ack = 0;
		break;
	default:
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf06d,
		    "qla_target(%d): Received unknown immediate "
		    "notify status %x\n", vha->vp_idx, status);
		break;
	}

	if (send_notify_ack)
		qlt_send_notify_ack(ha->base_qpair, iocb, add_flags, 0, 0, 0,
		    0, 0);
}

/*
 * ha->hardware_lock supposed to be held on entry. Might drop it, then reaquire
 * This function sends busy to ISP 2xxx or 24xx.
 */
static int __qlt_send_busy(struct qla_qpair *qpair,
	struct atio_from_isp *atio, uint16_t status)
{
	struct scsi_qla_host *vha = qpair->vha;
	struct ctio7_to_24xx *ctio24;
	struct qla_hw_data *ha = vha->hw;
	request_t *pkt;
	struct fc_port *sess = NULL;
	unsigned long flags;
	u16 temp;

	spin_lock_irqsave(&ha->tgt.sess_lock, flags);
	sess = ha->tgt.tgt_ops->find_sess_by_s_id(vha,
	    atio->u.isp24.fcp_hdr.s_id);
	spin_unlock_irqrestore(&ha->tgt.sess_lock, flags);
	if (!sess) {
		qlt_send_term_exchange(qpair, NULL, atio, 1, 0);
		return 0;
	}
	/* Sending marker isn't necessary, since we called from ISR */

	pkt = (request_t *)__qla2x00_alloc_iocbs(qpair, NULL);
	if (!pkt) {
		ql_dbg(ql_dbg_io, vha, 0x3063,
		    "qla_target(%d): %s failed: unable to allocate "
		    "request packet", vha->vp_idx, __func__);
		return -ENOMEM;
	}

	qpair->tgt_counters.num_q_full_sent++;
	pkt->entry_count = 1;
	pkt->handle = QLA_TGT_SKIP_HANDLE | CTIO_COMPLETION_HANDLE_MARK;

	ctio24 = (struct ctio7_to_24xx *)pkt;
	ctio24->entry_type = CTIO_TYPE7;
	ctio24->nport_handle = sess->loop_id;
	ctio24->timeout = cpu_to_le16(QLA_TGT_TIMEOUT);
	ctio24->vp_index = vha->vp_idx;
	ctio24->initiator_id[0] = atio->u.isp24.fcp_hdr.s_id[2];
	ctio24->initiator_id[1] = atio->u.isp24.fcp_hdr.s_id[1];
	ctio24->initiator_id[2] = atio->u.isp24.fcp_hdr.s_id[0];
	ctio24->exchange_addr = atio->u.isp24.exchange_addr;
	temp = (atio->u.isp24.attr << 9) |
		CTIO7_FLAGS_STATUS_MODE_1 | CTIO7_FLAGS_SEND_STATUS |
		CTIO7_FLAGS_DONT_RET_CTIO;
	ctio24->u.status1.flags = cpu_to_le16(temp);
	/*
	 * CTIO from fw w/o se_cmd doesn't provide enough info to retry it,
	 * if the explicit conformation is used.
	 */
	ctio24->u.status1.ox_id = swab16(atio->u.isp24.fcp_hdr.ox_id);
	ctio24->u.status1.scsi_status = cpu_to_le16(status);
	/* Memory Barrier */
	wmb();
	if (qpair->reqq_start_iocbs)
		qpair->reqq_start_iocbs(qpair);
	else
		qla2x00_start_iocbs(vha, qpair->req);
	return 0;
}

/*
 * This routine is used to allocate a command for either a QFull condition
 * (ie reply SAM_STAT_BUSY) or to terminate an exchange that did not go
 * out previously.
 */
static void
qlt_alloc_qfull_cmd(struct scsi_qla_host *vha,
	struct atio_from_isp *atio, uint16_t status, int qfull)
{
	struct qla_tgt *tgt = vha->vha_tgt.qla_tgt;
	struct qla_hw_data *ha = vha->hw;
	struct fc_port *sess;
	struct se_session *se_sess;
	struct qla_tgt_cmd *cmd;
	int tag;
	unsigned long flags;

	if (unlikely(tgt->tgt_stop)) {
		ql_dbg(ql_dbg_io, vha, 0x300a,
			"New command while device %p is shutting down\n", tgt);
		return;
	}

	if ((vha->hw->tgt.num_qfull_cmds_alloc + 1) > MAX_QFULL_CMDS_ALLOC) {
		vha->hw->tgt.num_qfull_cmds_dropped++;
		if (vha->hw->tgt.num_qfull_cmds_dropped >
			vha->qla_stats.stat_max_qfull_cmds_dropped)
			vha->qla_stats.stat_max_qfull_cmds_dropped =
				vha->hw->tgt.num_qfull_cmds_dropped;

		ql_dbg(ql_dbg_io, vha, 0x3068,
			"qla_target(%d): %s: QFull CMD dropped[%d]\n",
			vha->vp_idx, __func__,
			vha->hw->tgt.num_qfull_cmds_dropped);

		qlt_chk_exch_leak_thresh_hold(vha);
		return;
	}

	sess = ha->tgt.tgt_ops->find_sess_by_s_id
		(vha, atio->u.isp24.fcp_hdr.s_id);
	if (!sess)
		return;

	se_sess = sess->se_sess;

	tag = percpu_ida_alloc(&se_sess->sess_tag_pool, TASK_RUNNING);
	if (tag < 0)
		return;

	cmd = &((struct qla_tgt_cmd *)se_sess->sess_cmd_map)[tag];
	if (!cmd) {
		ql_dbg(ql_dbg_io, vha, 0x3009,
			"qla_target(%d): %s: Allocation of cmd failed\n",
			vha->vp_idx, __func__);

		vha->hw->tgt.num_qfull_cmds_dropped++;
		if (vha->hw->tgt.num_qfull_cmds_dropped >
			vha->qla_stats.stat_max_qfull_cmds_dropped)
			vha->qla_stats.stat_max_qfull_cmds_dropped =
				vha->hw->tgt.num_qfull_cmds_dropped;

		qlt_chk_exch_leak_thresh_hold(vha);
		return;
	}

	memset(cmd, 0, sizeof(struct qla_tgt_cmd));

	qlt_incr_num_pend_cmds(vha);
	INIT_LIST_HEAD(&cmd->cmd_list);
	memcpy(&cmd->atio, atio, sizeof(*atio));

	cmd->tgt = vha->vha_tgt.qla_tgt;
	cmd->vha = vha;
	cmd->reset_count = ha->base_qpair->chip_reset;
	cmd->q_full = 1;
	cmd->qpair = ha->base_qpair;

	if (qfull) {
		cmd->q_full = 1;
		/* NOTE: borrowing the state field to carry the status */
		cmd->state = status;
	} else
		cmd->term_exchg = 1;

	spin_lock_irqsave(&vha->hw->tgt.q_full_lock, flags);
	list_add_tail(&cmd->cmd_list, &vha->hw->tgt.q_full_list);

	vha->hw->tgt.num_qfull_cmds_alloc++;
	if (vha->hw->tgt.num_qfull_cmds_alloc >
		vha->qla_stats.stat_max_qfull_cmds_alloc)
		vha->qla_stats.stat_max_qfull_cmds_alloc =
			vha->hw->tgt.num_qfull_cmds_alloc;
	spin_unlock_irqrestore(&vha->hw->tgt.q_full_lock, flags);
}

int
qlt_free_qfull_cmds(struct qla_qpair *qpair)
{
	struct scsi_qla_host *vha = qpair->vha;
	struct qla_hw_data *ha = vha->hw;
	unsigned long flags;
	struct qla_tgt_cmd *cmd, *tcmd;
	struct list_head free_list, q_full_list;
	int rc = 0;

	if (list_empty(&ha->tgt.q_full_list))
		return 0;

	INIT_LIST_HEAD(&free_list);
	INIT_LIST_HEAD(&q_full_list);

	spin_lock_irqsave(&vha->hw->tgt.q_full_lock, flags);
	if (list_empty(&ha->tgt.q_full_list)) {
		spin_unlock_irqrestore(&vha->hw->tgt.q_full_lock, flags);
		return 0;
	}

	list_splice_init(&vha->hw->tgt.q_full_list, &q_full_list);
	spin_unlock_irqrestore(&vha->hw->tgt.q_full_lock, flags);

	spin_lock_irqsave(qpair->qp_lock_ptr, flags);
	list_for_each_entry_safe(cmd, tcmd, &q_full_list, cmd_list) {
		if (cmd->q_full)
			/* cmd->state is a borrowed field to hold status */
			rc = __qlt_send_busy(qpair, &cmd->atio, cmd->state);
		else if (cmd->term_exchg)
			rc = __qlt_send_term_exchange(qpair, NULL, &cmd->atio);

		if (rc == -ENOMEM)
			break;

		if (cmd->q_full)
			ql_dbg(ql_dbg_io, vha, 0x3006,
			    "%s: busy sent for ox_id[%04x]\n", __func__,
			    be16_to_cpu(cmd->atio.u.isp24.fcp_hdr.ox_id));
		else if (cmd->term_exchg)
			ql_dbg(ql_dbg_io, vha, 0x3007,
			    "%s: Term exchg sent for ox_id[%04x]\n", __func__,
			    be16_to_cpu(cmd->atio.u.isp24.fcp_hdr.ox_id));
		else
			ql_dbg(ql_dbg_io, vha, 0x3008,
			    "%s: Unexpected cmd in QFull list %p\n", __func__,
			    cmd);

		list_del(&cmd->cmd_list);
		list_add_tail(&cmd->cmd_list, &free_list);

		/* piggy back on hardware_lock for protection */
		vha->hw->tgt.num_qfull_cmds_alloc--;
	}
	spin_unlock_irqrestore(qpair->qp_lock_ptr, flags);

	cmd = NULL;

	list_for_each_entry_safe(cmd, tcmd, &free_list, cmd_list) {
		list_del(&cmd->cmd_list);
		/* This cmd was never sent to TCM.  There is no need
		 * to schedule free or call free_cmd
		 */
		qlt_free_cmd(cmd);
	}

	if (!list_empty(&q_full_list)) {
		spin_lock_irqsave(&vha->hw->tgt.q_full_lock, flags);
		list_splice(&q_full_list, &vha->hw->tgt.q_full_list);
		spin_unlock_irqrestore(&vha->hw->tgt.q_full_lock, flags);
	}

	return rc;
}

static void
qlt_send_busy(struct qla_qpair *qpair, struct atio_from_isp *atio,
    uint16_t status)
{
	int rc = 0;
	struct scsi_qla_host *vha = qpair->vha;

	rc = __qlt_send_busy(qpair, atio, status);
	if (rc == -ENOMEM)
		qlt_alloc_qfull_cmd(vha, atio, status, 1);
}

static int
qlt_chk_qfull_thresh_hold(struct scsi_qla_host *vha, struct qla_qpair *qpair,
	struct atio_from_isp *atio, uint8_t ha_locked)
{
	struct qla_hw_data *ha = vha->hw;
	uint16_t status;
	unsigned long flags;

	if (ha->tgt.num_pend_cmds < Q_FULL_THRESH_HOLD(ha))
		return 0;

	if (!ha_locked)
		spin_lock_irqsave(&ha->hardware_lock, flags);
	status = temp_sam_status;
	qlt_send_busy(qpair, atio, status);
	if (!ha_locked)
		spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return 1;
}

/* ha->hardware_lock supposed to be held on entry */
/* called via callback from qla2xxx */
static void qlt_24xx_atio_pkt(struct scsi_qla_host *vha,
	struct atio_from_isp *atio, uint8_t ha_locked)
{
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt *tgt = vha->vha_tgt.qla_tgt;
	int rc;
	unsigned long flags;

	if (unlikely(tgt == NULL)) {
		ql_dbg(ql_dbg_tgt, vha, 0x3064,
		    "ATIO pkt, but no tgt (ha %p)", ha);
		return;
	}
	/*
	 * In tgt_stop mode we also should allow all requests to pass.
	 * Otherwise, some commands can stuck.
	 */

	tgt->atio_irq_cmd_count++;

	switch (atio->u.raw.entry_type) {
	case ATIO_TYPE7:
		if (unlikely(atio->u.isp24.exchange_addr ==
		    ATIO_EXCHANGE_ADDRESS_UNKNOWN)) {
			ql_dbg(ql_dbg_io, vha, 0x3065,
			    "qla_target(%d): ATIO_TYPE7 "
			    "received with UNKNOWN exchange address, "
			    "sending QUEUE_FULL\n", vha->vp_idx);
			if (!ha_locked)
				spin_lock_irqsave(&ha->hardware_lock, flags);
			qlt_send_busy(ha->base_qpair, atio,
			    SAM_STAT_TASK_SET_FULL);
			if (!ha_locked)
				spin_unlock_irqrestore(&ha->hardware_lock,
				    flags);
			break;
		}

		if (likely(atio->u.isp24.fcp_cmnd.task_mgmt_flags == 0)) {
			rc = qlt_chk_qfull_thresh_hold(vha, ha->base_qpair,
			    atio, ha_locked);
			if (rc != 0) {
				tgt->atio_irq_cmd_count--;
				return;
			}
			rc = qlt_handle_cmd_for_atio(vha, atio);
		} else {
			rc = qlt_handle_task_mgmt(vha, atio);
		}
		if (unlikely(rc != 0)) {
			if (rc == -ESRCH) {
				if (!ha_locked)
					spin_lock_irqsave(&ha->hardware_lock,
					    flags);

#if 1 /* With TERM EXCHANGE some FC cards refuse to boot */
				qlt_send_busy(ha->base_qpair, atio,
				    SAM_STAT_BUSY);
#else
				qlt_send_term_exchange(ha->base_qpair, NULL,
				    atio, 1, 0);
#endif
				if (!ha_locked)
					spin_unlock_irqrestore(
					    &ha->hardware_lock, flags);
			} else {
				if (tgt->tgt_stop) {
					ql_dbg(ql_dbg_tgt, vha, 0xe059,
					    "qla_target: Unable to send "
					    "command to target for req, "
					    "ignoring.\n");
				} else {
					ql_dbg(ql_dbg_tgt, vha, 0xe05a,
					    "qla_target(%d): Unable to send "
					    "command to target, sending BUSY "
					    "status.\n", vha->vp_idx);
					if (!ha_locked)
						spin_lock_irqsave(
						    &ha->hardware_lock, flags);
					qlt_send_busy(ha->base_qpair,
					    atio, SAM_STAT_BUSY);
					if (!ha_locked)
						spin_unlock_irqrestore(
						    &ha->hardware_lock, flags);
				}
			}
		}
		break;

	case IMMED_NOTIFY_TYPE:
	{
		if (unlikely(atio->u.isp2x.entry_status != 0)) {
			ql_dbg(ql_dbg_tgt, vha, 0xe05b,
			    "qla_target(%d): Received ATIO packet %x "
			    "with error status %x\n", vha->vp_idx,
			    atio->u.raw.entry_type,
			    atio->u.isp2x.entry_status);
			break;
		}
		ql_dbg(ql_dbg_tgt, vha, 0xe02e, "%s", "IMMED_NOTIFY ATIO");

		if (!ha_locked)
			spin_lock_irqsave(&ha->hardware_lock, flags);
		qlt_handle_imm_notify(vha, (struct imm_ntfy_from_isp *)atio);
		if (!ha_locked)
			spin_unlock_irqrestore(&ha->hardware_lock, flags);
		break;
	}

	default:
		ql_dbg(ql_dbg_tgt, vha, 0xe05c,
		    "qla_target(%d): Received unknown ATIO atio "
		    "type %x\n", vha->vp_idx, atio->u.raw.entry_type);
		break;
	}

	tgt->atio_irq_cmd_count--;
}

/* ha->hardware_lock supposed to be held on entry */
/* called via callback from qla2xxx */
static void qlt_response_pkt(struct scsi_qla_host *vha,
	struct rsp_que *rsp, response_t *pkt)
{
	struct qla_tgt *tgt = vha->vha_tgt.qla_tgt;

	if (unlikely(tgt == NULL)) {
		ql_dbg(ql_dbg_tgt, vha, 0xe05d,
		    "qla_target(%d): Response pkt %x received, but no tgt (ha %p)\n",
		    vha->vp_idx, pkt->entry_type, vha->hw);
		return;
	}

	/*
	 * In tgt_stop mode we also should allow all requests to pass.
	 * Otherwise, some commands can stuck.
	 */

	switch (pkt->entry_type) {
	case CTIO_CRC2:
	case CTIO_TYPE7:
	{
		struct ctio7_from_24xx *entry = (struct ctio7_from_24xx *)pkt;
		qlt_do_ctio_completion(vha, rsp, entry->handle,
		    le16_to_cpu(entry->status)|(pkt->entry_status << 16),
		    entry);
		break;
	}

	case ACCEPT_TGT_IO_TYPE:
	{
		struct atio_from_isp *atio = (struct atio_from_isp *)pkt;
		int rc;
		if (atio->u.isp2x.status !=
		    cpu_to_le16(ATIO_CDB_VALID)) {
			ql_dbg(ql_dbg_tgt, vha, 0xe05e,
			    "qla_target(%d): ATIO with error "
			    "status %x received\n", vha->vp_idx,
			    le16_to_cpu(atio->u.isp2x.status));
			break;
		}

		rc = qlt_chk_qfull_thresh_hold(vha, rsp->qpair, atio, 1);
		if (rc != 0)
			return;

		rc = qlt_handle_cmd_for_atio(vha, atio);
		if (unlikely(rc != 0)) {
			if (rc == -ESRCH) {
#if 1 /* With TERM EXCHANGE some FC cards refuse to boot */
				qlt_send_busy(rsp->qpair, atio, 0);
#else
				qlt_send_term_exchange(rsp->qpair, NULL, atio, 1, 0);
#endif
			} else {
				if (tgt->tgt_stop) {
					ql_dbg(ql_dbg_tgt, vha, 0xe05f,
					    "qla_target: Unable to send "
					    "command to target, sending TERM "
					    "EXCHANGE for rsp\n");
					qlt_send_term_exchange(rsp->qpair, NULL,
					    atio, 1, 0);
				} else {
					ql_dbg(ql_dbg_tgt, vha, 0xe060,
					    "qla_target(%d): Unable to send "
					    "command to target, sending BUSY "
					    "status\n", vha->vp_idx);
					qlt_send_busy(rsp->qpair, atio, 0);
				}
			}
		}
	}
	break;

	case CONTINUE_TGT_IO_TYPE:
	{
		struct ctio_to_2xxx *entry = (struct ctio_to_2xxx *)pkt;
		qlt_do_ctio_completion(vha, rsp, entry->handle,
		    le16_to_cpu(entry->status)|(pkt->entry_status << 16),
		    entry);
		break;
	}

	case CTIO_A64_TYPE:
	{
		struct ctio_to_2xxx *entry = (struct ctio_to_2xxx *)pkt;
		qlt_do_ctio_completion(vha, rsp, entry->handle,
		    le16_to_cpu(entry->status)|(pkt->entry_status << 16),
		    entry);
		break;
	}

	case IMMED_NOTIFY_TYPE:
		ql_dbg(ql_dbg_tgt, vha, 0xe035, "%s", "IMMED_NOTIFY\n");
		qlt_handle_imm_notify(vha, (struct imm_ntfy_from_isp *)pkt);
		break;

	case NOTIFY_ACK_TYPE:
		if (tgt->notify_ack_expected > 0) {
			struct nack_to_isp *entry = (struct nack_to_isp *)pkt;
			ql_dbg(ql_dbg_tgt, vha, 0xe036,
			    "NOTIFY_ACK seq %08x status %x\n",
			    le16_to_cpu(entry->u.isp2x.seq_id),
			    le16_to_cpu(entry->u.isp2x.status));
			tgt->notify_ack_expected--;
			if (entry->u.isp2x.status !=
			    cpu_to_le16(NOTIFY_ACK_SUCCESS)) {
				ql_dbg(ql_dbg_tgt, vha, 0xe061,
				    "qla_target(%d): NOTIFY_ACK "
				    "failed %x\n", vha->vp_idx,
				    le16_to_cpu(entry->u.isp2x.status));
			}
		} else {
			ql_dbg(ql_dbg_tgt, vha, 0xe062,
			    "qla_target(%d): Unexpected NOTIFY_ACK received\n",
			    vha->vp_idx);
		}
		break;

	case ABTS_RECV_24XX:
		ql_dbg(ql_dbg_tgt, vha, 0xe037,
		    "ABTS_RECV_24XX: instance %d\n", vha->vp_idx);
		qlt_24xx_handle_abts(vha, (struct abts_recv_from_24xx *)pkt);
		break;

	case ABTS_RESP_24XX:
		if (tgt->abts_resp_expected > 0) {
			struct abts_resp_from_24xx_fw *entry =
				(struct abts_resp_from_24xx_fw *)pkt;
			ql_dbg(ql_dbg_tgt, vha, 0xe038,
			    "ABTS_RESP_24XX: compl_status %x\n",
			    entry->compl_status);
			tgt->abts_resp_expected--;
			if (le16_to_cpu(entry->compl_status) !=
			    ABTS_RESP_COMPL_SUCCESS) {
				if ((entry->error_subcode1 == 0x1E) &&
				    (entry->error_subcode2 == 0)) {
					/*
					 * We've got a race here: aborted
					 * exchange not terminated, i.e.
					 * response for the aborted command was
					 * sent between the abort request was
					 * received and processed.
					 * Unfortunately, the firmware has a
					 * silly requirement that all aborted
					 * exchanges must be explicitely
					 * terminated, otherwise it refuses to
					 * send responses for the abort
					 * requests. So, we have to
					 * (re)terminate the exchange and retry
					 * the abort response.
					 */
					qlt_24xx_retry_term_exchange(vha,
					    entry);
				} else
					ql_dbg(ql_dbg_tgt, vha, 0xe063,
					    "qla_target(%d): ABTS_RESP_24XX "
					    "failed %x (subcode %x:%x)",
					    vha->vp_idx, entry->compl_status,
					    entry->error_subcode1,
					    entry->error_subcode2);
			}
		} else {
			ql_dbg(ql_dbg_tgt, vha, 0xe064,
			    "qla_target(%d): Unexpected ABTS_RESP_24XX "
			    "received\n", vha->vp_idx);
		}
		break;

	default:
		ql_dbg(ql_dbg_tgt, vha, 0xe065,
		    "qla_target(%d): Received unknown response pkt "
		    "type %x\n", vha->vp_idx, pkt->entry_type);
		break;
	}

}

/*
 * ha->hardware_lock supposed to be held on entry. Might drop it, then reaquire
 */
void qlt_async_event(uint16_t code, struct scsi_qla_host *vha,
	uint16_t *mailbox)
{
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt *tgt = vha->vha_tgt.qla_tgt;
	int login_code;

	if (!tgt || tgt->tgt_stop || tgt->tgt_stopped)
		return;

	if (((code == MBA_POINT_TO_POINT) || (code == MBA_CHG_IN_CONNECTION)) &&
	    IS_QLA2100(ha))
		return;
	/*
	 * In tgt_stop mode we also should allow all requests to pass.
	 * Otherwise, some commands can stuck.
	 */


	switch (code) {
	case MBA_RESET:			/* Reset */
	case MBA_SYSTEM_ERR:		/* System Error */
	case MBA_REQ_TRANSFER_ERR:	/* Request Transfer Error */
	case MBA_RSP_TRANSFER_ERR:	/* Response Transfer Error */
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf03a,
		    "qla_target(%d): System error async event %#x "
		    "occurred", vha->vp_idx, code);
		break;
	case MBA_WAKEUP_THRES:		/* Request Queue Wake-up. */
		set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
		break;

	case MBA_LOOP_UP:
	{
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf03b,
		    "qla_target(%d): Async LOOP_UP occurred "
		    "(m[0]=%x, m[1]=%x, m[2]=%x, m[3]=%x)", vha->vp_idx,
		    le16_to_cpu(mailbox[0]), le16_to_cpu(mailbox[1]),
		    le16_to_cpu(mailbox[2]), le16_to_cpu(mailbox[3]));
		if (tgt->link_reinit_iocb_pending) {
			qlt_send_notify_ack(ha->base_qpair,
			    (void *)&tgt->link_reinit_iocb,
			    0, 0, 0, 0, 0, 0);
			tgt->link_reinit_iocb_pending = 0;
		}
		break;
	}

	case MBA_LIP_OCCURRED:
	case MBA_LOOP_DOWN:
	case MBA_LIP_RESET:
	case MBA_RSCN_UPDATE:
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf03c,
		    "qla_target(%d): Async event %#x occurred "
		    "(m[0]=%x, m[1]=%x, m[2]=%x, m[3]=%x)", vha->vp_idx, code,
		    le16_to_cpu(mailbox[0]), le16_to_cpu(mailbox[1]),
		    le16_to_cpu(mailbox[2]), le16_to_cpu(mailbox[3]));
		break;

	case MBA_REJECTED_FCP_CMD:
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf017,
		    "qla_target(%d): Async event LS_REJECT occurred (m[0]=%x, m[1]=%x, m[2]=%x, m[3]=%x)",
		    vha->vp_idx,
		    le16_to_cpu(mailbox[0]), le16_to_cpu(mailbox[1]),
		    le16_to_cpu(mailbox[2]), le16_to_cpu(mailbox[3]));

		if (le16_to_cpu(mailbox[3]) == 1) {
			/* exchange starvation. */
			vha->hw->exch_starvation++;
			if (vha->hw->exch_starvation > 5) {
				ql_log(ql_log_warn, vha, 0xd03a,
				    "Exchange starvation-. Resetting RISC\n");

				vha->hw->exch_starvation = 0;
				if (IS_P3P_TYPE(vha->hw))
					set_bit(FCOE_CTX_RESET_NEEDED,
					    &vha->dpc_flags);
				else
					set_bit(ISP_ABORT_NEEDED,
					    &vha->dpc_flags);
				qla2xxx_wake_dpc(vha);
			}
		}
		break;

	case MBA_PORT_UPDATE:
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf03d,
		    "qla_target(%d): Port update async event %#x "
		    "occurred: updating the ports database (m[0]=%x, m[1]=%x, "
		    "m[2]=%x, m[3]=%x)", vha->vp_idx, code,
		    le16_to_cpu(mailbox[0]), le16_to_cpu(mailbox[1]),
		    le16_to_cpu(mailbox[2]), le16_to_cpu(mailbox[3]));

		login_code = le16_to_cpu(mailbox[2]);
		if (login_code == 0x4) {
			ql_dbg(ql_dbg_tgt_mgt, vha, 0xf03e,
			    "Async MB 2: Got PLOGI Complete\n");
			vha->hw->exch_starvation = 0;
		} else if (login_code == 0x7)
			ql_dbg(ql_dbg_tgt_mgt, vha, 0xf03f,
			    "Async MB 2: Port Logged Out\n");
		break;
	default:
		break;
	}

}

static fc_port_t *qlt_get_port_database(struct scsi_qla_host *vha,
	uint16_t loop_id)
{
	fc_port_t *fcport, *tfcp, *del;
	int rc;
	unsigned long flags;
	u8 newfcport = 0;

	fcport = qla2x00_alloc_fcport(vha, GFP_KERNEL);
	if (!fcport) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf06f,
		    "qla_target(%d): Allocation of tmp FC port failed",
		    vha->vp_idx);
		return NULL;
	}

	fcport->loop_id = loop_id;

	rc = qla24xx_gpdb_wait(vha, fcport, 0);
	if (rc != QLA_SUCCESS) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf070,
		    "qla_target(%d): Failed to retrieve fcport "
		    "information -- get_port_database() returned %x "
		    "(loop_id=0x%04x)", vha->vp_idx, rc, loop_id);
		kfree(fcport);
		return NULL;
	}

	del = NULL;
	spin_lock_irqsave(&vha->hw->tgt.sess_lock, flags);
	tfcp = qla2x00_find_fcport_by_wwpn(vha, fcport->port_name, 1);

	if (tfcp) {
		tfcp->d_id = fcport->d_id;
		tfcp->port_type = fcport->port_type;
		tfcp->supported_classes = fcport->supported_classes;
		tfcp->flags |= fcport->flags;
		tfcp->scan_state = QLA_FCPORT_FOUND;

		del = fcport;
		fcport = tfcp;
	} else {
		if (vha->hw->current_topology == ISP_CFG_F)
			fcport->flags |= FCF_FABRIC_DEVICE;

		list_add_tail(&fcport->list, &vha->vp_fcports);
		if (!IS_SW_RESV_ADDR(fcport->d_id))
		   vha->fcport_count++;
		fcport->login_gen++;
		fcport->disc_state = DSC_LOGIN_COMPLETE;
		fcport->login_succ = 1;
		newfcport = 1;
	}

	fcport->deleted = 0;
	spin_unlock_irqrestore(&vha->hw->tgt.sess_lock, flags);

	switch (vha->host->active_mode) {
	case MODE_INITIATOR:
	case MODE_DUAL:
		if (newfcport) {
			if (!IS_IIDMA_CAPABLE(vha->hw) || !vha->hw->flags.gpsc_supported) {
				ql_dbg(ql_dbg_disc, vha, 0x20fe,
				   "%s %d %8phC post upd_fcport fcp_cnt %d\n",
				   __func__, __LINE__, fcport->port_name, vha->fcport_count);
				qla24xx_post_upd_fcport_work(vha, fcport);
			} else {
				ql_dbg(ql_dbg_disc, vha, 0x20ff,
				   "%s %d %8phC post gpsc fcp_cnt %d\n",
				   __func__, __LINE__, fcport->port_name, vha->fcport_count);
				qla24xx_post_gpsc_work(vha, fcport);
			}
		}
		break;

	case MODE_TARGET:
	default:
		break;
	}
	if (del)
		qla2x00_free_fcport(del);

	return fcport;
}

/* Must be called under tgt_mutex */
static struct fc_port *qlt_make_local_sess(struct scsi_qla_host *vha,
	uint8_t *s_id)
{
	struct fc_port *sess = NULL;
	fc_port_t *fcport = NULL;
	int rc, global_resets;
	uint16_t loop_id = 0;

	if ((s_id[0] == 0xFF) && (s_id[1] == 0xFC)) {
		/*
		 * This is Domain Controller, so it should be
		 * OK to drop SCSI commands from it.
		 */
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf042,
		    "Unable to find initiator with S_ID %x:%x:%x",
		    s_id[0], s_id[1], s_id[2]);
		return NULL;
	}

	mutex_lock(&vha->vha_tgt.tgt_mutex);

retry:
	global_resets =
	    atomic_read(&vha->vha_tgt.qla_tgt->tgt_global_resets_count);

	rc = qla24xx_get_loop_id(vha, s_id, &loop_id);
	if (rc != 0) {
		mutex_unlock(&vha->vha_tgt.tgt_mutex);

		ql_log(ql_log_info, vha, 0xf071,
		    "qla_target(%d): Unable to find "
		    "initiator with S_ID %x:%x:%x",
		    vha->vp_idx, s_id[0], s_id[1],
		    s_id[2]);

		if (rc == -ENOENT) {
			qlt_port_logo_t logo;
			sid_to_portid(s_id, &logo.id);
			logo.cmd_count = 1;
			qlt_send_first_logo(vha, &logo);
		}

		return NULL;
	}

	fcport = qlt_get_port_database(vha, loop_id);
	if (!fcport) {
		mutex_unlock(&vha->vha_tgt.tgt_mutex);
		return NULL;
	}

	if (global_resets !=
	    atomic_read(&vha->vha_tgt.qla_tgt->tgt_global_resets_count)) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf043,
		    "qla_target(%d): global reset during session discovery "
		    "(counter was %d, new %d), retrying", vha->vp_idx,
		    global_resets,
		    atomic_read(&vha->vha_tgt.
			qla_tgt->tgt_global_resets_count));
		goto retry;
	}

	sess = qlt_create_sess(vha, fcport, true);

	mutex_unlock(&vha->vha_tgt.tgt_mutex);

	return sess;
}

static void qlt_abort_work(struct qla_tgt *tgt,
	struct qla_tgt_sess_work_param *prm)
{
	struct scsi_qla_host *vha = tgt->vha;
	struct qla_hw_data *ha = vha->hw;
	struct fc_port *sess = NULL;
	unsigned long flags = 0, flags2 = 0;
	uint32_t be_s_id;
	uint8_t s_id[3];
	int rc;

	spin_lock_irqsave(&ha->tgt.sess_lock, flags2);

	if (tgt->tgt_stop)
		goto out_term2;

	s_id[0] = prm->abts.fcp_hdr_le.s_id[2];
	s_id[1] = prm->abts.fcp_hdr_le.s_id[1];
	s_id[2] = prm->abts.fcp_hdr_le.s_id[0];

	sess = ha->tgt.tgt_ops->find_sess_by_s_id(vha,
	    (unsigned char *)&be_s_id);
	if (!sess) {
		spin_unlock_irqrestore(&ha->tgt.sess_lock, flags2);

		sess = qlt_make_local_sess(vha, s_id);
		/* sess has got an extra creation ref */

		spin_lock_irqsave(&ha->tgt.sess_lock, flags2);
		if (!sess)
			goto out_term2;
	} else {
		if (sess->deleted) {
			sess = NULL;
			goto out_term2;
		}

		if (!kref_get_unless_zero(&sess->sess_kref)) {
			ql_dbg(ql_dbg_tgt_tmr, vha, 0xf01c,
			    "%s: kref_get fail %8phC \n",
			     __func__, sess->port_name);
			sess = NULL;
			goto out_term2;
		}
	}

	rc = __qlt_24xx_handle_abts(vha, &prm->abts, sess);
	ha->tgt.tgt_ops->put_sess(sess);
	spin_unlock_irqrestore(&ha->tgt.sess_lock, flags2);

	if (rc != 0)
		goto out_term;
	return;

out_term2:
	if (sess)
		ha->tgt.tgt_ops->put_sess(sess);
	spin_unlock_irqrestore(&ha->tgt.sess_lock, flags2);

out_term:
	spin_lock_irqsave(&ha->hardware_lock, flags);
	qlt_24xx_send_abts_resp(ha->base_qpair, &prm->abts,
	    FCP_TMF_REJECTED, false);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

static void qlt_tmr_work(struct qla_tgt *tgt,
	struct qla_tgt_sess_work_param *prm)
{
	struct atio_from_isp *a = &prm->tm_iocb2;
	struct scsi_qla_host *vha = tgt->vha;
	struct qla_hw_data *ha = vha->hw;
	struct fc_port *sess = NULL;
	unsigned long flags;
	uint8_t *s_id = NULL; /* to hide compiler warnings */
	int rc;
	u64 unpacked_lun;
	int fn;
	void *iocb;

	spin_lock_irqsave(&ha->tgt.sess_lock, flags);

	if (tgt->tgt_stop)
		goto out_term2;

	s_id = prm->tm_iocb2.u.isp24.fcp_hdr.s_id;
	sess = ha->tgt.tgt_ops->find_sess_by_s_id(vha, s_id);
	if (!sess) {
		spin_unlock_irqrestore(&ha->tgt.sess_lock, flags);

		sess = qlt_make_local_sess(vha, s_id);
		/* sess has got an extra creation ref */

		spin_lock_irqsave(&ha->tgt.sess_lock, flags);
		if (!sess)
			goto out_term2;
	} else {
		if (sess->deleted) {
			sess = NULL;
			goto out_term2;
		}

		if (!kref_get_unless_zero(&sess->sess_kref)) {
			ql_dbg(ql_dbg_tgt_tmr, vha, 0xf020,
			    "%s: kref_get fail %8phC\n",
			     __func__, sess->port_name);
			sess = NULL;
			goto out_term2;
		}
	}

	iocb = a;
	fn = a->u.isp24.fcp_cmnd.task_mgmt_flags;
	unpacked_lun =
	    scsilun_to_int((struct scsi_lun *)&a->u.isp24.fcp_cmnd.lun);

	rc = qlt_issue_task_mgmt(sess, unpacked_lun, fn, iocb, 0);
	ha->tgt.tgt_ops->put_sess(sess);
	spin_unlock_irqrestore(&ha->tgt.sess_lock, flags);

	if (rc != 0)
		goto out_term;
	return;

out_term2:
	if (sess)
		ha->tgt.tgt_ops->put_sess(sess);
	spin_unlock_irqrestore(&ha->tgt.sess_lock, flags);
out_term:
	qlt_send_term_exchange(ha->base_qpair, NULL, &prm->tm_iocb2, 1, 0);
}

static void qlt_sess_work_fn(struct work_struct *work)
{
	struct qla_tgt *tgt = container_of(work, struct qla_tgt, sess_work);
	struct scsi_qla_host *vha = tgt->vha;
	unsigned long flags;

	ql_dbg(ql_dbg_tgt_mgt, vha, 0xf000, "Sess work (tgt %p)", tgt);

	spin_lock_irqsave(&tgt->sess_work_lock, flags);
	while (!list_empty(&tgt->sess_works_list)) {
		struct qla_tgt_sess_work_param *prm = list_entry(
		    tgt->sess_works_list.next, typeof(*prm),
		    sess_works_list_entry);

		/*
		 * This work can be scheduled on several CPUs at time, so we
		 * must delete the entry to eliminate double processing
		 */
		list_del(&prm->sess_works_list_entry);

		spin_unlock_irqrestore(&tgt->sess_work_lock, flags);

		switch (prm->type) {
		case QLA_TGT_SESS_WORK_ABORT:
			qlt_abort_work(tgt, prm);
			break;
		case QLA_TGT_SESS_WORK_TM:
			qlt_tmr_work(tgt, prm);
			break;
		default:
			BUG_ON(1);
			break;
		}

		spin_lock_irqsave(&tgt->sess_work_lock, flags);

		kfree(prm);
	}
	spin_unlock_irqrestore(&tgt->sess_work_lock, flags);
}

/* Must be called under tgt_host_action_mutex */
int qlt_add_target(struct qla_hw_data *ha, struct scsi_qla_host *base_vha)
{
	struct qla_tgt *tgt;
	int rc, i;
	struct qla_qpair_hint *h;

	if (!QLA_TGT_MODE_ENABLED())
		return 0;

	if (!IS_TGT_MODE_CAPABLE(ha)) {
		ql_log(ql_log_warn, base_vha, 0xe070,
		    "This adapter does not support target mode.\n");
		return 0;
	}

	ql_dbg(ql_dbg_tgt, base_vha, 0xe03b,
	    "Registering target for host %ld(%p).\n", base_vha->host_no, ha);

	BUG_ON(base_vha->vha_tgt.qla_tgt != NULL);

	tgt = kzalloc(sizeof(struct qla_tgt), GFP_KERNEL);
	if (!tgt) {
		ql_dbg(ql_dbg_tgt, base_vha, 0xe066,
		    "Unable to allocate struct qla_tgt\n");
		return -ENOMEM;
	}

	tgt->qphints = kzalloc((ha->max_qpairs + 1) *
	    sizeof(struct qla_qpair_hint), GFP_KERNEL);
	if (!tgt->qphints) {
		kfree(tgt);
		ql_log(ql_log_warn, base_vha, 0x0197,
		    "Unable to allocate qpair hints.\n");
		return -ENOMEM;
	}

	if (!(base_vha->host->hostt->supported_mode & MODE_TARGET))
		base_vha->host->hostt->supported_mode |= MODE_TARGET;

	rc = btree_init64(&tgt->lun_qpair_map);
	if (rc) {
		kfree(tgt->qphints);
		kfree(tgt);
		ql_log(ql_log_info, base_vha, 0x0198,
			"Unable to initialize lun_qpair_map btree\n");
		return -EIO;
	}
	h = &tgt->qphints[0];
	h->qpair = ha->base_qpair;
	INIT_LIST_HEAD(&h->hint_elem);
	h->cpuid = ha->base_qpair->cpuid;
	list_add_tail(&h->hint_elem, &ha->base_qpair->hints_list);

	for (i = 0; i < ha->max_qpairs; i++) {
		unsigned long flags;

		struct qla_qpair *qpair = ha->queue_pair_map[i];
		h = &tgt->qphints[i + 1];
		INIT_LIST_HEAD(&h->hint_elem);
		if (qpair) {
			h->qpair = qpair;
			spin_lock_irqsave(qpair->qp_lock_ptr, flags);
			list_add_tail(&h->hint_elem, &qpair->hints_list);
			spin_unlock_irqrestore(qpair->qp_lock_ptr, flags);
			h->cpuid = qpair->cpuid;
		}
	}

	tgt->ha = ha;
	tgt->vha = base_vha;
	init_waitqueue_head(&tgt->waitQ);
	INIT_LIST_HEAD(&tgt->del_sess_list);
	spin_lock_init(&tgt->sess_work_lock);
	INIT_WORK(&tgt->sess_work, qlt_sess_work_fn);
	INIT_LIST_HEAD(&tgt->sess_works_list);
	atomic_set(&tgt->tgt_global_resets_count, 0);

	base_vha->vha_tgt.qla_tgt = tgt;

	ql_dbg(ql_dbg_tgt, base_vha, 0xe067,
		"qla_target(%d): using 64 Bit PCI addressing",
		base_vha->vp_idx);
	/* 3 is reserved */
	tgt->sg_tablesize = QLA_TGT_MAX_SG_24XX(base_vha->req->length - 3);

	mutex_lock(&qla_tgt_mutex);
	list_add_tail(&tgt->tgt_list_entry, &qla_tgt_glist);
	mutex_unlock(&qla_tgt_mutex);

	if (ha->tgt.tgt_ops && ha->tgt.tgt_ops->add_target)
		ha->tgt.tgt_ops->add_target(base_vha);

	return 0;
}

/* Must be called under tgt_host_action_mutex */
int qlt_remove_target(struct qla_hw_data *ha, struct scsi_qla_host *vha)
{
	if (!vha->vha_tgt.qla_tgt)
		return 0;

	if (vha->fc_vport) {
		qlt_release(vha->vha_tgt.qla_tgt);
		return 0;
	}

	/* free left over qfull cmds */
	qlt_init_term_exchange(vha);

	ql_dbg(ql_dbg_tgt, vha, 0xe03c, "Unregistering target for host %ld(%p)",
	    vha->host_no, ha);
	qlt_release(vha->vha_tgt.qla_tgt);

	return 0;
}

void qlt_remove_target_resources(struct qla_hw_data *ha)
{
	struct scsi_qla_host *node;
	u32 key = 0;

	btree_for_each_safe32(&ha->tgt.host_map, key, node)
		btree_remove32(&ha->tgt.host_map, key);

	btree_destroy32(&ha->tgt.host_map);
}

static void qlt_lport_dump(struct scsi_qla_host *vha, u64 wwpn,
	unsigned char *b)
{
	int i;

	pr_debug("qla2xxx HW vha->node_name: ");
	for (i = 0; i < WWN_SIZE; i++)
		pr_debug("%02x ", vha->node_name[i]);
	pr_debug("\n");
	pr_debug("qla2xxx HW vha->port_name: ");
	for (i = 0; i < WWN_SIZE; i++)
		pr_debug("%02x ", vha->port_name[i]);
	pr_debug("\n");

	pr_debug("qla2xxx passed configfs WWPN: ");
	put_unaligned_be64(wwpn, b);
	for (i = 0; i < WWN_SIZE; i++)
		pr_debug("%02x ", b[i]);
	pr_debug("\n");
}

/**
 * qla_tgt_lport_register - register lport with external module
 *
 * @qla_tgt_ops: Pointer for tcm_qla2xxx qla_tgt_ops
 * @wwpn: Passwd FC target WWPN
 * @callback:  lport initialization callback for tcm_qla2xxx code
 * @target_lport_ptr: pointer for tcm_qla2xxx specific lport data
 */
int qlt_lport_register(void *target_lport_ptr, u64 phys_wwpn,
		       u64 npiv_wwpn, u64 npiv_wwnn,
		       int (*callback)(struct scsi_qla_host *, void *, u64, u64))
{
	struct qla_tgt *tgt;
	struct scsi_qla_host *vha;
	struct qla_hw_data *ha;
	struct Scsi_Host *host;
	unsigned long flags;
	int rc;
	u8 b[WWN_SIZE];

	mutex_lock(&qla_tgt_mutex);
	list_for_each_entry(tgt, &qla_tgt_glist, tgt_list_entry) {
		vha = tgt->vha;
		ha = vha->hw;

		host = vha->host;
		if (!host)
			continue;

		if (!(host->hostt->supported_mode & MODE_TARGET))
			continue;

		spin_lock_irqsave(&ha->hardware_lock, flags);
		if ((!npiv_wwpn || !npiv_wwnn) && host->active_mode & MODE_TARGET) {
			pr_debug("MODE_TARGET already active on qla2xxx(%d)\n",
			    host->host_no);
			spin_unlock_irqrestore(&ha->hardware_lock, flags);
			continue;
		}
		if (tgt->tgt_stop) {
			pr_debug("MODE_TARGET in shutdown on qla2xxx(%d)\n",
				 host->host_no);
			spin_unlock_irqrestore(&ha->hardware_lock, flags);
			continue;
		}
		spin_unlock_irqrestore(&ha->hardware_lock, flags);

		if (!scsi_host_get(host)) {
			ql_dbg(ql_dbg_tgt, vha, 0xe068,
			    "Unable to scsi_host_get() for"
			    " qla2xxx scsi_host\n");
			continue;
		}
		qlt_lport_dump(vha, phys_wwpn, b);

		if (memcmp(vha->port_name, b, WWN_SIZE)) {
			scsi_host_put(host);
			continue;
		}
		rc = (*callback)(vha, target_lport_ptr, npiv_wwpn, npiv_wwnn);
		if (rc != 0)
			scsi_host_put(host);

		mutex_unlock(&qla_tgt_mutex);
		return rc;
	}
	mutex_unlock(&qla_tgt_mutex);

	return -ENODEV;
}
EXPORT_SYMBOL(qlt_lport_register);

/**
 * qla_tgt_lport_deregister - Degister lport
 *
 * @vha:  Registered scsi_qla_host pointer
 */
void qlt_lport_deregister(struct scsi_qla_host *vha)
{
	struct qla_hw_data *ha = vha->hw;
	struct Scsi_Host *sh = vha->host;
	/*
	 * Clear the target_lport_ptr qla_target_template pointer in qla_hw_data
	 */
	vha->vha_tgt.target_lport_ptr = NULL;
	ha->tgt.tgt_ops = NULL;
	/*
	 * Release the Scsi_Host reference for the underlying qla2xxx host
	 */
	scsi_host_put(sh);
}
EXPORT_SYMBOL(qlt_lport_deregister);

/* Must be called under HW lock */
static void qlt_set_mode(struct scsi_qla_host *vha)
{
	switch (ql2x_ini_mode) {
	case QLA2XXX_INI_MODE_DISABLED:
	case QLA2XXX_INI_MODE_EXCLUSIVE:
		vha->host->active_mode = MODE_TARGET;
		break;
	case QLA2XXX_INI_MODE_ENABLED:
		vha->host->active_mode = MODE_UNKNOWN;
		break;
	case QLA2XXX_INI_MODE_DUAL:
		vha->host->active_mode = MODE_DUAL;
		break;
	default:
		break;
	}
}

/* Must be called under HW lock */
static void qlt_clear_mode(struct scsi_qla_host *vha)
{
	switch (ql2x_ini_mode) {
	case QLA2XXX_INI_MODE_DISABLED:
		vha->host->active_mode = MODE_UNKNOWN;
		break;
	case QLA2XXX_INI_MODE_EXCLUSIVE:
		vha->host->active_mode = MODE_INITIATOR;
		break;
	case QLA2XXX_INI_MODE_ENABLED:
	case QLA2XXX_INI_MODE_DUAL:
		vha->host->active_mode = MODE_INITIATOR;
		break;
	default:
		break;
	}
}

/*
 * qla_tgt_enable_vha - NO LOCK HELD
 *
 * host_reset, bring up w/ Target Mode Enabled
 */
void
qlt_enable_vha(struct scsi_qla_host *vha)
{
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt *tgt = vha->vha_tgt.qla_tgt;
	unsigned long flags;
	scsi_qla_host_t *base_vha = pci_get_drvdata(ha->pdev);

	if (!tgt) {
		ql_dbg(ql_dbg_tgt, vha, 0xe069,
		    "Unable to locate qla_tgt pointer from"
		    " struct qla_hw_data\n");
		dump_stack();
		return;
	}

	spin_lock_irqsave(&ha->hardware_lock, flags);
	tgt->tgt_stopped = 0;
	qlt_set_mode(vha);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	if (vha->vp_idx) {
		qla24xx_disable_vp(vha);
		qla24xx_enable_vp(vha);
	} else {
		set_bit(ISP_ABORT_NEEDED, &base_vha->dpc_flags);
		qla2xxx_wake_dpc(base_vha);
		qla2x00_wait_for_hba_online(base_vha);
	}
}
EXPORT_SYMBOL(qlt_enable_vha);

/*
 * qla_tgt_disable_vha - NO LOCK HELD
 *
 * Disable Target Mode and reset the adapter
 */
static void qlt_disable_vha(struct scsi_qla_host *vha)
{
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt *tgt = vha->vha_tgt.qla_tgt;
	unsigned long flags;

	if (!tgt) {
		ql_dbg(ql_dbg_tgt, vha, 0xe06a,
		    "Unable to locate qla_tgt pointer from"
		    " struct qla_hw_data\n");
		dump_stack();
		return;
	}

	spin_lock_irqsave(&ha->hardware_lock, flags);
	qlt_clear_mode(vha);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
	qla2xxx_wake_dpc(vha);
	qla2x00_wait_for_hba_online(vha);
}

/*
 * Called from qla_init.c:qla24xx_vport_create() contex to setup
 * the target mode specific struct scsi_qla_host and struct qla_hw_data
 * members.
 */
void
qlt_vport_create(struct scsi_qla_host *vha, struct qla_hw_data *ha)
{
	vha->vha_tgt.qla_tgt = NULL;

	mutex_init(&vha->vha_tgt.tgt_mutex);
	mutex_init(&vha->vha_tgt.tgt_host_action_mutex);

	qlt_clear_mode(vha);

	/*
	 * NOTE: Currently the value is kept the same for <24xx and
	 * >=24xx ISPs. If it is necessary to change it,
	 * the check should be added for specific ISPs,
	 * assigning the value appropriately.
	 */
	ha->tgt.atio_q_length = ATIO_ENTRY_CNT_24XX;

	qlt_add_target(ha, vha);
}

void
qlt_rff_id(struct scsi_qla_host *vha, struct ct_sns_req *ct_req)
{
	/*
	 * FC-4 Feature bit 0 indicates target functionality to the name server.
	 */
	if (qla_tgt_mode_enabled(vha)) {
		ct_req->req.rff_id.fc4_feature = BIT_0;
	} else if (qla_ini_mode_enabled(vha)) {
		ct_req->req.rff_id.fc4_feature = BIT_1;
	} else if (qla_dual_mode_enabled(vha))
		ct_req->req.rff_id.fc4_feature = BIT_0 | BIT_1;
}

/*
 * qlt_init_atio_q_entries() - Initializes ATIO queue entries.
 * @ha: HA context
 *
 * Beginning of ATIO ring has initialization control block already built
 * by nvram config routine.
 *
 * Returns 0 on success.
 */
void
qlt_init_atio_q_entries(struct scsi_qla_host *vha)
{
	struct qla_hw_data *ha = vha->hw;
	uint16_t cnt;
	struct atio_from_isp *pkt = (struct atio_from_isp *)ha->tgt.atio_ring;

	if (qla_ini_mode_enabled(vha))
		return;

	for (cnt = 0; cnt < ha->tgt.atio_q_length; cnt++) {
		pkt->u.raw.signature = ATIO_PROCESSED;
		pkt++;
	}

}

/*
 * qlt_24xx_process_atio_queue() - Process ATIO queue entries.
 * @ha: SCSI driver HA context
 */
void
qlt_24xx_process_atio_queue(struct scsi_qla_host *vha, uint8_t ha_locked)
{
	struct qla_hw_data *ha = vha->hw;
	struct atio_from_isp *pkt;
	int cnt, i;

	if (!ha->flags.fw_started)
		return;

	while ((ha->tgt.atio_ring_ptr->signature != ATIO_PROCESSED) ||
	    fcpcmd_is_corrupted(ha->tgt.atio_ring_ptr)) {
		pkt = (struct atio_from_isp *)ha->tgt.atio_ring_ptr;
		cnt = pkt->u.raw.entry_count;

		if (unlikely(fcpcmd_is_corrupted(ha->tgt.atio_ring_ptr))) {
			/*
			 * This packet is corrupted. The header + payload
			 * can not be trusted. There is no point in passing
			 * it further up.
			 */
			ql_log(ql_log_warn, vha, 0xd03c,
			    "corrupted fcp frame SID[%3phN] OXID[%04x] EXCG[%x] %64phN\n",
			    pkt->u.isp24.fcp_hdr.s_id,
			    be16_to_cpu(pkt->u.isp24.fcp_hdr.ox_id),
			    le32_to_cpu(pkt->u.isp24.exchange_addr), pkt);

			adjust_corrupted_atio(pkt);
			qlt_send_term_exchange(ha->base_qpair, NULL, pkt,
			    ha_locked, 0);
		} else {
			qlt_24xx_atio_pkt_all_vps(vha,
			    (struct atio_from_isp *)pkt, ha_locked);
		}

		for (i = 0; i < cnt; i++) {
			ha->tgt.atio_ring_index++;
			if (ha->tgt.atio_ring_index == ha->tgt.atio_q_length) {
				ha->tgt.atio_ring_index = 0;
				ha->tgt.atio_ring_ptr = ha->tgt.atio_ring;
			} else
				ha->tgt.atio_ring_ptr++;

			pkt->u.raw.signature = ATIO_PROCESSED;
			pkt = (struct atio_from_isp *)ha->tgt.atio_ring_ptr;
		}
		wmb();
	}

	/* Adjust ring index */
	WRT_REG_DWORD(ISP_ATIO_Q_OUT(vha), ha->tgt.atio_ring_index);
}

void
qlt_24xx_config_rings(struct scsi_qla_host *vha)
{
	struct qla_hw_data *ha = vha->hw;
	struct init_cb_24xx *icb;
	if (!QLA_TGT_MODE_ENABLED())
		return;

	WRT_REG_DWORD(ISP_ATIO_Q_IN(vha), 0);
	WRT_REG_DWORD(ISP_ATIO_Q_OUT(vha), 0);
	RD_REG_DWORD(ISP_ATIO_Q_OUT(vha));

	icb = (struct init_cb_24xx *)ha->init_cb;

	if ((ql2xenablemsix != 0) && IS_ATIO_MSIX_CAPABLE(ha)) {
		struct qla_msix_entry *msix = &ha->msix_entries[2];

		icb->msix_atio = cpu_to_le16(msix->entry);
		ql_dbg(ql_dbg_init, vha, 0xf072,
		    "Registering ICB vector 0x%x for atio que.\n",
		    msix->entry);
	} else if (ql2xenablemsix == 0) {
		icb->firmware_options_2 |= cpu_to_le32(BIT_26);
		ql_dbg(ql_dbg_init, vha, 0xf07f,
		    "Registering INTx vector for ATIO.\n");
	}
}

void
qlt_24xx_config_nvram_stage1(struct scsi_qla_host *vha, struct nvram_24xx *nv)
{
	struct qla_hw_data *ha = vha->hw;

	if (!QLA_TGT_MODE_ENABLED())
		return;

	if (qla_tgt_mode_enabled(vha) || qla_dual_mode_enabled(vha)) {
		if (!ha->tgt.saved_set) {
			/* We save only once */
			ha->tgt.saved_exchange_count = nv->exchange_count;
			ha->tgt.saved_firmware_options_1 =
			    nv->firmware_options_1;
			ha->tgt.saved_firmware_options_2 =
			    nv->firmware_options_2;
			ha->tgt.saved_firmware_options_3 =
			    nv->firmware_options_3;
			ha->tgt.saved_set = 1;
		}

		if (qla_tgt_mode_enabled(vha))
			nv->exchange_count = cpu_to_le16(0xFFFF);
		else			/* dual */
			nv->exchange_count = cpu_to_le16(ql2xexchoffld);

		/* Enable target mode */
		nv->firmware_options_1 |= cpu_to_le32(BIT_4);

		/* Disable ini mode, if requested */
		if (qla_tgt_mode_enabled(vha))
			nv->firmware_options_1 |= cpu_to_le32(BIT_5);

		/* Disable Full Login after LIP */
		nv->firmware_options_1 &= cpu_to_le32(~BIT_13);
		/* Enable initial LIP */
		nv->firmware_options_1 &= cpu_to_le32(~BIT_9);
		if (ql2xtgt_tape_enable)
			/* Enable FC Tape support */
			nv->firmware_options_2 |= cpu_to_le32(BIT_12);
		else
			/* Disable FC Tape support */
			nv->firmware_options_2 &= cpu_to_le32(~BIT_12);

		/* Disable Full Login after LIP */
		nv->host_p &= cpu_to_le32(~BIT_10);

		/*
		 * clear BIT 15 explicitly as we have seen at least
		 * a couple of instances where this was set and this
		 * was causing the firmware to not be initialized.
		 */
		nv->firmware_options_1 &= cpu_to_le32(~BIT_15);
		/* Enable target PRLI control */
		nv->firmware_options_2 |= cpu_to_le32(BIT_14);
	} else {
		if (ha->tgt.saved_set) {
			nv->exchange_count = ha->tgt.saved_exchange_count;
			nv->firmware_options_1 =
			    ha->tgt.saved_firmware_options_1;
			nv->firmware_options_2 =
			    ha->tgt.saved_firmware_options_2;
			nv->firmware_options_3 =
			    ha->tgt.saved_firmware_options_3;
		}
		return;
	}

	if (ha->base_qpair->enable_class_2) {
		if (vha->flags.init_done)
			fc_host_supported_classes(vha->host) =
				FC_COS_CLASS2 | FC_COS_CLASS3;

		nv->firmware_options_2 |= cpu_to_le32(BIT_8);
	} else {
		if (vha->flags.init_done)
			fc_host_supported_classes(vha->host) = FC_COS_CLASS3;

		nv->firmware_options_2 &= ~cpu_to_le32(BIT_8);
	}
}

void
qlt_24xx_config_nvram_stage2(struct scsi_qla_host *vha,
	struct init_cb_24xx *icb)
{
	struct qla_hw_data *ha = vha->hw;

	if (!QLA_TGT_MODE_ENABLED())
		return;

	if (ha->tgt.node_name_set) {
		memcpy(icb->node_name, ha->tgt.tgt_node_name, WWN_SIZE);
		icb->firmware_options_1 |= cpu_to_le32(BIT_14);
	}

	/* disable ZIO at start time. */
	if (!vha->flags.init_done) {
		uint32_t tmp;
		tmp = le32_to_cpu(icb->firmware_options_2);
		tmp &= ~(BIT_3 | BIT_2 | BIT_1 | BIT_0);
		icb->firmware_options_2 = cpu_to_le32(tmp);
	}
}

void
qlt_81xx_config_nvram_stage1(struct scsi_qla_host *vha, struct nvram_81xx *nv)
{
	struct qla_hw_data *ha = vha->hw;

	if (!QLA_TGT_MODE_ENABLED())
		return;

	if (qla_tgt_mode_enabled(vha) || qla_dual_mode_enabled(vha)) {
		if (!ha->tgt.saved_set) {
			/* We save only once */
			ha->tgt.saved_exchange_count = nv->exchange_count;
			ha->tgt.saved_firmware_options_1 =
			    nv->firmware_options_1;
			ha->tgt.saved_firmware_options_2 =
			    nv->firmware_options_2;
			ha->tgt.saved_firmware_options_3 =
			    nv->firmware_options_3;
			ha->tgt.saved_set = 1;
		}

		if (qla_tgt_mode_enabled(vha))
			nv->exchange_count = cpu_to_le16(0xFFFF);
		else			/* dual */
			nv->exchange_count = cpu_to_le16(ql2xexchoffld);

		/* Enable target mode */
		nv->firmware_options_1 |= cpu_to_le32(BIT_4);

		/* Disable ini mode, if requested */
		if (qla_tgt_mode_enabled(vha))
			nv->firmware_options_1 |= cpu_to_le32(BIT_5);
		/* Disable Full Login after LIP */
		nv->firmware_options_1 &= cpu_to_le32(~BIT_13);
		/* Enable initial LIP */
		nv->firmware_options_1 &= cpu_to_le32(~BIT_9);
		/*
		 * clear BIT 15 explicitly as we have seen at
		 * least a couple of instances where this was set
		 * and this was causing the firmware to not be
		 * initialized.
		 */
		nv->firmware_options_1 &= cpu_to_le32(~BIT_15);
		if (ql2xtgt_tape_enable)
			/* Enable FC tape support */
			nv->firmware_options_2 |= cpu_to_le32(BIT_12);
		else
			/* Disable FC tape support */
			nv->firmware_options_2 &= cpu_to_le32(~BIT_12);

		/* Disable Full Login after LIP */
		nv->host_p &= cpu_to_le32(~BIT_10);
		/* Enable target PRLI control */
		nv->firmware_options_2 |= cpu_to_le32(BIT_14);
	} else {
		if (ha->tgt.saved_set) {
			nv->exchange_count = ha->tgt.saved_exchange_count;
			nv->firmware_options_1 =
			    ha->tgt.saved_firmware_options_1;
			nv->firmware_options_2 =
			    ha->tgt.saved_firmware_options_2;
			nv->firmware_options_3 =
			    ha->tgt.saved_firmware_options_3;
		}
		return;
	}

	if (ha->base_qpair->enable_class_2) {
		if (vha->flags.init_done)
			fc_host_supported_classes(vha->host) =
				FC_COS_CLASS2 | FC_COS_CLASS3;

		nv->firmware_options_2 |= cpu_to_le32(BIT_8);
	} else {
		if (vha->flags.init_done)
			fc_host_supported_classes(vha->host) = FC_COS_CLASS3;

		nv->firmware_options_2 &= ~cpu_to_le32(BIT_8);
	}
}

void
qlt_81xx_config_nvram_stage2(struct scsi_qla_host *vha,
	struct init_cb_81xx *icb)
{
	struct qla_hw_data *ha = vha->hw;

	if (!QLA_TGT_MODE_ENABLED())
		return;

	if (ha->tgt.node_name_set) {
		memcpy(icb->node_name, ha->tgt.tgt_node_name, WWN_SIZE);
		icb->firmware_options_1 |= cpu_to_le32(BIT_14);
	}

	/* disable ZIO at start time. */
	if (!vha->flags.init_done) {
		uint32_t tmp;
		tmp = le32_to_cpu(icb->firmware_options_2);
		tmp &= ~(BIT_3 | BIT_2 | BIT_1 | BIT_0);
		icb->firmware_options_2 = cpu_to_le32(tmp);
	}

}

void
qlt_83xx_iospace_config(struct qla_hw_data *ha)
{
	if (!QLA_TGT_MODE_ENABLED())
		return;

	ha->msix_count += 1; /* For ATIO Q */
}


void
qlt_modify_vp_config(struct scsi_qla_host *vha,
	struct vp_config_entry_24xx *vpmod)
{
	/* enable target mode.  Bit5 = 1 => disable */
	if (qla_tgt_mode_enabled(vha) || qla_dual_mode_enabled(vha))
		vpmod->options_idx1 &= ~BIT_5;

	/* Disable ini mode, if requested.  bit4 = 1 => disable */
	if (qla_tgt_mode_enabled(vha))
		vpmod->options_idx1 &= ~BIT_4;
}

void
qlt_probe_one_stage1(struct scsi_qla_host *base_vha, struct qla_hw_data *ha)
{
	int rc;

	if (!QLA_TGT_MODE_ENABLED())
		return;

	if  ((ql2xenablemsix == 0) || IS_QLA83XX(ha) || IS_QLA27XX(ha)) {
		ISP_ATIO_Q_IN(base_vha) = &ha->mqiobase->isp25mq.atio_q_in;
		ISP_ATIO_Q_OUT(base_vha) = &ha->mqiobase->isp25mq.atio_q_out;
	} else {
		ISP_ATIO_Q_IN(base_vha) = &ha->iobase->isp24.atio_q_in;
		ISP_ATIO_Q_OUT(base_vha) = &ha->iobase->isp24.atio_q_out;
	}

	mutex_init(&base_vha->vha_tgt.tgt_mutex);
	mutex_init(&base_vha->vha_tgt.tgt_host_action_mutex);

	INIT_LIST_HEAD(&base_vha->unknown_atio_list);
	INIT_DELAYED_WORK(&base_vha->unknown_atio_work,
	    qlt_unknown_atio_work_fn);

	qlt_clear_mode(base_vha);

	rc = btree_init32(&ha->tgt.host_map);
	if (rc)
		ql_log(ql_log_info, base_vha, 0xd03d,
		    "Unable to initialize ha->host_map btree\n");

	qlt_update_vp_map(base_vha, SET_VP_IDX);
}

irqreturn_t
qla83xx_msix_atio_q(int irq, void *dev_id)
{
	struct rsp_que *rsp;
	scsi_qla_host_t	*vha;
	struct qla_hw_data *ha;
	unsigned long flags;

	rsp = (struct rsp_que *) dev_id;
	ha = rsp->hw;
	vha = pci_get_drvdata(ha->pdev);

	spin_lock_irqsave(&ha->tgt.atio_lock, flags);

	qlt_24xx_process_atio_queue(vha, 0);

	spin_unlock_irqrestore(&ha->tgt.atio_lock, flags);

	return IRQ_HANDLED;
}

static void
qlt_handle_abts_recv_work(struct work_struct *work)
{
	struct qla_tgt_sess_op *op = container_of(work,
		struct qla_tgt_sess_op, work);
	scsi_qla_host_t *vha = op->vha;
	struct qla_hw_data *ha = vha->hw;
	unsigned long flags;

	if (qla2x00_reset_active(vha) ||
	    (op->chip_reset != ha->base_qpair->chip_reset))
		return;

	spin_lock_irqsave(&ha->tgt.atio_lock, flags);
	qlt_24xx_process_atio_queue(vha, 0);
	spin_unlock_irqrestore(&ha->tgt.atio_lock, flags);

	spin_lock_irqsave(&ha->hardware_lock, flags);
	qlt_response_pkt_all_vps(vha, op->rsp, (response_t *)&op->atio);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	kfree(op);
}

void
qlt_handle_abts_recv(struct scsi_qla_host *vha, struct rsp_que *rsp,
    response_t *pkt)
{
	struct qla_tgt_sess_op *op;

	op = kzalloc(sizeof(*op), GFP_ATOMIC);

	if (!op) {
		/* do not reach for ATIO queue here.  This is best effort err
		 * recovery at this point.
		 */
		qlt_response_pkt_all_vps(vha, rsp, pkt);
		return;
	}

	memcpy(&op->atio, pkt, sizeof(*pkt));
	op->vha = vha;
	op->chip_reset = vha->hw->base_qpair->chip_reset;
	op->rsp = rsp;
	INIT_WORK(&op->work, qlt_handle_abts_recv_work);
	queue_work(qla_tgt_wq, &op->work);
	return;
}

int
qlt_mem_alloc(struct qla_hw_data *ha)
{
	if (!QLA_TGT_MODE_ENABLED())
		return 0;

	ha->tgt.tgt_vp_map = kzalloc(sizeof(struct qla_tgt_vp_map) *
	    MAX_MULTI_ID_FABRIC, GFP_KERNEL);
	if (!ha->tgt.tgt_vp_map)
		return -ENOMEM;

	ha->tgt.atio_ring = dma_alloc_coherent(&ha->pdev->dev,
	    (ha->tgt.atio_q_length + 1) * sizeof(struct atio_from_isp),
	    &ha->tgt.atio_dma, GFP_KERNEL);
	if (!ha->tgt.atio_ring) {
		kfree(ha->tgt.tgt_vp_map);
		return -ENOMEM;
	}
	return 0;
}

void
qlt_mem_free(struct qla_hw_data *ha)
{
	if (!QLA_TGT_MODE_ENABLED())
		return;

	if (ha->tgt.atio_ring) {
		dma_free_coherent(&ha->pdev->dev, (ha->tgt.atio_q_length + 1) *
		    sizeof(struct atio_from_isp), ha->tgt.atio_ring,
		    ha->tgt.atio_dma);
	}
	kfree(ha->tgt.tgt_vp_map);
}

/* vport_slock to be held by the caller */
void
qlt_update_vp_map(struct scsi_qla_host *vha, int cmd)
{
	void *slot;
	u32 key;
	int rc;

	if (!QLA_TGT_MODE_ENABLED())
		return;

	key = vha->d_id.b24;

	switch (cmd) {
	case SET_VP_IDX:
		vha->hw->tgt.tgt_vp_map[vha->vp_idx].vha = vha;
		break;
	case SET_AL_PA:
		slot = btree_lookup32(&vha->hw->tgt.host_map, key);
		if (!slot) {
			ql_dbg(ql_dbg_tgt_mgt, vha, 0xf018,
			    "Save vha in host_map %p %06x\n", vha, key);
			rc = btree_insert32(&vha->hw->tgt.host_map,
				key, vha, GFP_ATOMIC);
			if (rc)
				ql_log(ql_log_info, vha, 0xd03e,
				    "Unable to insert s_id into host_map: %06x\n",
				    key);
			return;
		}
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf019,
		    "replace existing vha in host_map %p %06x\n", vha, key);
		btree_update32(&vha->hw->tgt.host_map, key, vha);
		break;
	case RESET_VP_IDX:
		vha->hw->tgt.tgt_vp_map[vha->vp_idx].vha = NULL;
		break;
	case RESET_AL_PA:
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf01a,
		   "clear vha in host_map %p %06x\n", vha, key);
		slot = btree_lookup32(&vha->hw->tgt.host_map, key);
		if (slot)
			btree_remove32(&vha->hw->tgt.host_map, key);
		vha->d_id.b24 = 0;
		break;
	}
}

void qlt_update_host_map(struct scsi_qla_host *vha, port_id_t id)
{
	unsigned long flags;
	struct qla_hw_data *ha = vha->hw;

	if (!vha->d_id.b24) {
		spin_lock_irqsave(&ha->vport_slock, flags);
		vha->d_id = id;
		qlt_update_vp_map(vha, SET_AL_PA);
		spin_unlock_irqrestore(&ha->vport_slock, flags);
	} else if (vha->d_id.b24 != id.b24) {
		spin_lock_irqsave(&ha->vport_slock, flags);
		qlt_update_vp_map(vha, RESET_AL_PA);
		vha->d_id = id;
		qlt_update_vp_map(vha, SET_AL_PA);
		spin_unlock_irqrestore(&ha->vport_slock, flags);
	}
}

static int __init qlt_parse_ini_mode(void)
{
	if (strcasecmp(qlini_mode, QLA2XXX_INI_MODE_STR_EXCLUSIVE) == 0)
		ql2x_ini_mode = QLA2XXX_INI_MODE_EXCLUSIVE;
	else if (strcasecmp(qlini_mode, QLA2XXX_INI_MODE_STR_DISABLED) == 0)
		ql2x_ini_mode = QLA2XXX_INI_MODE_DISABLED;
	else if (strcasecmp(qlini_mode, QLA2XXX_INI_MODE_STR_ENABLED) == 0)
		ql2x_ini_mode = QLA2XXX_INI_MODE_ENABLED;
	else if (strcasecmp(qlini_mode, QLA2XXX_INI_MODE_STR_DUAL) == 0)
		ql2x_ini_mode = QLA2XXX_INI_MODE_DUAL;
	else
		return false;

	return true;
}

int __init qlt_init(void)
{
	int ret;

	if (!qlt_parse_ini_mode()) {
		ql_log(ql_log_fatal, NULL, 0xe06b,
		    "qlt_parse_ini_mode() failed\n");
		return -EINVAL;
	}

	if (!QLA_TGT_MODE_ENABLED())
		return 0;

	qla_tgt_mgmt_cmd_cachep = kmem_cache_create("qla_tgt_mgmt_cmd_cachep",
	    sizeof(struct qla_tgt_mgmt_cmd), __alignof__(struct
	    qla_tgt_mgmt_cmd), 0, NULL);
	if (!qla_tgt_mgmt_cmd_cachep) {
		ql_log(ql_log_fatal, NULL, 0xd04b,
		    "kmem_cache_create for qla_tgt_mgmt_cmd_cachep failed\n");
		return -ENOMEM;
	}

	qla_tgt_plogi_cachep = kmem_cache_create("qla_tgt_plogi_cachep",
	    sizeof(struct qlt_plogi_ack_t), __alignof__(struct qlt_plogi_ack_t),
	    0, NULL);

	if (!qla_tgt_plogi_cachep) {
		ql_log(ql_log_fatal, NULL, 0xe06d,
		    "kmem_cache_create for qla_tgt_plogi_cachep failed\n");
		ret = -ENOMEM;
		goto out_mgmt_cmd_cachep;
	}

	qla_tgt_mgmt_cmd_mempool = mempool_create(25, mempool_alloc_slab,
	    mempool_free_slab, qla_tgt_mgmt_cmd_cachep);
	if (!qla_tgt_mgmt_cmd_mempool) {
		ql_log(ql_log_fatal, NULL, 0xe06e,
		    "mempool_create for qla_tgt_mgmt_cmd_mempool failed\n");
		ret = -ENOMEM;
		goto out_plogi_cachep;
	}

	qla_tgt_wq = alloc_workqueue("qla_tgt_wq", 0, 0);
	if (!qla_tgt_wq) {
		ql_log(ql_log_fatal, NULL, 0xe06f,
		    "alloc_workqueue for qla_tgt_wq failed\n");
		ret = -ENOMEM;
		goto out_cmd_mempool;
	}
	/*
	 * Return 1 to signal that initiator-mode is being disabled
	 */
	return (ql2x_ini_mode == QLA2XXX_INI_MODE_DISABLED) ? 1 : 0;

out_cmd_mempool:
	mempool_destroy(qla_tgt_mgmt_cmd_mempool);
out_plogi_cachep:
	kmem_cache_destroy(qla_tgt_plogi_cachep);
out_mgmt_cmd_cachep:
	kmem_cache_destroy(qla_tgt_mgmt_cmd_cachep);
	return ret;
}

void qlt_exit(void)
{
	if (!QLA_TGT_MODE_ENABLED())
		return;

	destroy_workqueue(qla_tgt_wq);
	mempool_destroy(qla_tgt_mgmt_cmd_mempool);
	kmem_cache_destroy(qla_tgt_plogi_cachep);
	kmem_cache_destroy(qla_tgt_mgmt_cmd_cachep);
}
