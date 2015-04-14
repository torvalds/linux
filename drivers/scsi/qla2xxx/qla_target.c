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
	"\"enabled\" (default) - initiator mode will always stay enabled.");

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
	struct atio_from_isp *pkt);
static void qlt_response_pkt(struct scsi_qla_host *ha, response_t *pkt);
static int qlt_issue_task_mgmt(struct qla_tgt_sess *sess, uint32_t lun,
	int fn, void *iocb, int flags);
static void qlt_send_term_exchange(struct scsi_qla_host *ha, struct qla_tgt_cmd
	*cmd, struct atio_from_isp *atio, int ha_locked);
static void qlt_reject_free_srr_imm(struct scsi_qla_host *ha,
	struct qla_tgt_srr_imm *imm, int ha_lock);
static void qlt_abort_cmd_on_host_reset(struct scsi_qla_host *vha,
	struct qla_tgt_cmd *cmd);
static void qlt_alloc_qfull_cmd(struct scsi_qla_host *vha,
	struct atio_from_isp *atio, uint16_t status, int qfull);
static void qlt_disable_vha(struct scsi_qla_host *vha);
/*
 * Global Variables
 */
static struct kmem_cache *qla_tgt_mgmt_cmd_cachep;
static mempool_t *qla_tgt_mgmt_cmd_mempool;
static struct workqueue_struct *qla_tgt_wq;
static DEFINE_MUTEX(qla_tgt_mutex);
static LIST_HEAD(qla_tgt_glist);

/* ha->hardware_lock supposed to be held on entry (to protect tgt->sess_list) */
static struct qla_tgt_sess *qlt_find_sess_by_port_name(
	struct qla_tgt *tgt,
	const uint8_t *port_name)
{
	struct qla_tgt_sess *sess;

	list_for_each_entry(sess, &tgt->sess_list, sess_list_entry) {
		if (!memcmp(sess->port_name, port_name, WWN_SIZE))
			return sess;
	}

	return NULL;
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
	struct qla_hw_data *ha = vha->hw;
	uint8_t vp_idx;

	if ((vha->d_id.b.area != d_id[1]) || (vha->d_id.b.domain != d_id[0]))
		return NULL;

	if (vha->d_id.b.al_pa == d_id[2])
		return vha;

	BUG_ON(ha->tgt.tgt_vp_map == NULL);
	vp_idx = ha->tgt.tgt_vp_map[d_id[2]].idx;
	if (likely(test_bit(vp_idx, ha->vp_idx_map)))
		return ha->tgt.tgt_vp_map[vp_idx].vha;

	return NULL;
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
	if (vha->hw->tgt.num_pend_cmds > vha->hw->qla_stats.stat_max_pend_cmds)
		vha->hw->qla_stats.stat_max_pend_cmds =
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

static void qlt_24xx_atio_pkt_all_vps(struct scsi_qla_host *vha,
	struct atio_from_isp *atio)
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
			break;
		}
		qlt_24xx_atio_pkt(host, atio);
		break;
	}

	case IMMED_NOTIFY_TYPE:
	{
		struct scsi_qla_host *host = vha;
		struct imm_ntfy_from_isp *entry =
		    (struct imm_ntfy_from_isp *)atio;

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
		qlt_24xx_atio_pkt(host, atio);
		break;
	}

	default:
		ql_dbg(ql_dbg_tgt, vha, 0xe040,
		    "qla_target(%d): Received unknown ATIO atio "
		    "type %x\n", vha->vp_idx, atio->u.raw.entry_type);
		break;
	}

	return;
}

void qlt_response_pkt_all_vps(struct scsi_qla_host *vha, response_t *pkt)
{
	switch (pkt->entry_type) {
	case CTIO_CRC2:
		ql_dbg(ql_dbg_tgt, vha, 0xe073,
			"qla_target(%d):%s: CRC2 Response pkt\n",
			vha->vp_idx, __func__);
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
		qlt_response_pkt(host, pkt);
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
		qlt_response_pkt(host, pkt);
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
		qlt_response_pkt(host, pkt);
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
		qlt_response_pkt(host, pkt);
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
		qlt_response_pkt(host, pkt);
		break;
	}

	default:
		qlt_response_pkt(vha, pkt);
		break;
	}

}

static void qlt_free_session_done(struct work_struct *work)
{
	struct qla_tgt_sess *sess = container_of(work, struct qla_tgt_sess,
	    free_work);
	struct qla_tgt *tgt = sess->tgt;
	struct scsi_qla_host *vha = sess->vha;
	struct qla_hw_data *ha = vha->hw;

	BUG_ON(!tgt);
	/*
	 * Release the target session for FC Nexus from fabric module code.
	 */
	if (sess->se_sess != NULL)
		ha->tgt.tgt_ops->free_session(sess);

	ql_dbg(ql_dbg_tgt_mgt, vha, 0xf001,
	    "Unregistration of sess %p finished\n", sess);

	kfree(sess);
	/*
	 * We need to protect against race, when tgt is freed before or
	 * inside wake_up()
	 */
	tgt->sess_count--;
	if (tgt->sess_count == 0)
		wake_up_all(&tgt->waitQ);
}

/* ha->hardware_lock supposed to be held on entry */
void qlt_unreg_sess(struct qla_tgt_sess *sess)
{
	struct scsi_qla_host *vha = sess->vha;

	vha->hw->tgt.tgt_ops->clear_nacl_from_fcport_map(sess);

	list_del(&sess->sess_list_entry);
	if (sess->deleted)
		list_del(&sess->del_list_entry);

	INIT_WORK(&sess->free_work, qlt_free_session_done);
	schedule_work(&sess->free_work);
}
EXPORT_SYMBOL(qlt_unreg_sess);

/* ha->hardware_lock supposed to be held on entry */
static int qlt_reset(struct scsi_qla_host *vha, void *iocb, int mcmd)
{
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt_sess *sess = NULL;
	uint32_t unpacked_lun, lun = 0;
	uint16_t loop_id;
	int res = 0;
	struct imm_ntfy_from_isp *n = (struct imm_ntfy_from_isp *)iocb;
	struct atio_from_isp *a = (struct atio_from_isp *)iocb;

	loop_id = le16_to_cpu(n->u.isp24.nport_handle);
	if (loop_id == 0xFFFF) {
#if 0 /* FIXME: Re-enable Global event handling.. */
		/* Global event */
		atomic_inc(&ha->tgt.qla_tgt->tgt_global_resets_count);
		qlt_clear_tgt_db(ha->tgt.qla_tgt);
		if (!list_empty(&ha->tgt.qla_tgt->sess_list)) {
			sess = list_entry(ha->tgt.qla_tgt->sess_list.next,
			    typeof(*sess), sess_list_entry);
			switch (mcmd) {
			case QLA_TGT_NEXUS_LOSS_SESS:
				mcmd = QLA_TGT_NEXUS_LOSS;
				break;
			case QLA_TGT_ABORT_ALL_SESS:
				mcmd = QLA_TGT_ABORT_ALL;
				break;
			case QLA_TGT_NEXUS_LOSS:
			case QLA_TGT_ABORT_ALL:
				break;
			default:
				ql_dbg(ql_dbg_tgt, vha, 0xe046,
				    "qla_target(%d): Not allowed "
				    "command %x in %s", vha->vp_idx,
				    mcmd, __func__);
				sess = NULL;
				break;
			}
		} else
			sess = NULL;
#endif
	} else {
		sess = ha->tgt.tgt_ops->find_sess_by_loop_id(vha, loop_id);
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

	lun = a->u.isp24.fcp_cmnd.lun;
	unpacked_lun = scsilun_to_int((struct scsi_lun *)&lun);

	return qlt_issue_task_mgmt(sess, unpacked_lun, mcmd,
	    iocb, QLA24XX_MGMT_SEND_NACK);
}

/* ha->hardware_lock supposed to be held on entry */
static void qlt_schedule_sess_for_deletion(struct qla_tgt_sess *sess,
	bool immediate)
{
	struct qla_tgt *tgt = sess->tgt;
	uint32_t dev_loss_tmo = tgt->ha->port_down_retry_count + 5;

	if (sess->deleted)
		return;

	ql_dbg(ql_dbg_tgt, sess->vha, 0xe001,
	    "Scheduling sess %p for deletion\n", sess);
	list_add_tail(&sess->del_list_entry, &tgt->del_sess_list);
	sess->deleted = 1;

	if (immediate)
		dev_loss_tmo = 0;

	sess->expires = jiffies + dev_loss_tmo * HZ;

	ql_dbg(ql_dbg_tgt, sess->vha, 0xe048,
	    "qla_target(%d): session for port %8phC (loop ID %d) scheduled for "
	    "deletion in %u secs (expires: %lu) immed: %d\n",
	    sess->vha->vp_idx, sess->port_name, sess->loop_id, dev_loss_tmo,
	    sess->expires, immediate);

	if (immediate)
		schedule_delayed_work(&tgt->sess_del_work, 0);
	else
		schedule_delayed_work(&tgt->sess_del_work,
		    sess->expires - jiffies);
}

/* ha->hardware_lock supposed to be held on entry */
static void qlt_clear_tgt_db(struct qla_tgt *tgt)
{
	struct qla_tgt_sess *sess;

	list_for_each_entry(sess, &tgt->sess_list, sess_list_entry)
		qlt_schedule_sess_for_deletion(sess, true);

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
	rc = qla2x00_get_id_list(vha, gid_list, gid_list_dma, &entries);
	if (rc != QLA_SUCCESS) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf045,
		    "qla_target(%d): get_id_list() failed: %x\n",
		    vha->vp_idx, rc);
		res = -1;
		goto out_free_id_list;
	}

	id_iter = (char *)gid_list;
	res = -1;
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

/* ha->hardware_lock supposed to be held on entry */
static void qlt_undelete_sess(struct qla_tgt_sess *sess)
{
	BUG_ON(!sess->deleted);

	list_del(&sess->del_list_entry);
	sess->deleted = 0;
}

static void qlt_del_sess_work_fn(struct delayed_work *work)
{
	struct qla_tgt *tgt = container_of(work, struct qla_tgt,
	    sess_del_work);
	struct scsi_qla_host *vha = tgt->vha;
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt_sess *sess;
	unsigned long flags, elapsed;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	while (!list_empty(&tgt->del_sess_list)) {
		sess = list_entry(tgt->del_sess_list.next, typeof(*sess),
		    del_list_entry);
		elapsed = jiffies;
		if (time_after_eq(elapsed, sess->expires)) {
			qlt_undelete_sess(sess);

			ql_dbg(ql_dbg_tgt_mgt, vha, 0xf004,
			    "Timeout: sess %p about to be deleted\n",
			    sess);
			ha->tgt.tgt_ops->shutdown_sess(sess);
			ha->tgt.tgt_ops->put_sess(sess);
		} else {
			schedule_delayed_work(&tgt->sess_del_work,
			    sess->expires - elapsed);
			break;
		}
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

/*
 * Adds an extra ref to allow to drop hw lock after adding sess to the list.
 * Caller must put it.
 */
static struct qla_tgt_sess *qlt_create_sess(
	struct scsi_qla_host *vha,
	fc_port_t *fcport,
	bool local)
{
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt_sess *sess;
	unsigned long flags;
	unsigned char be_sid[3];

	/* Check to avoid double sessions */
	spin_lock_irqsave(&ha->hardware_lock, flags);
	list_for_each_entry(sess, &vha->vha_tgt.qla_tgt->sess_list,
				sess_list_entry) {
		if (!memcmp(sess->port_name, fcport->port_name, WWN_SIZE)) {
			ql_dbg(ql_dbg_tgt_mgt, vha, 0xf005,
			    "Double sess %p found (s_id %x:%x:%x, "
			    "loop_id %d), updating to d_id %x:%x:%x, "
			    "loop_id %d", sess, sess->s_id.b.domain,
			    sess->s_id.b.al_pa, sess->s_id.b.area,
			    sess->loop_id, fcport->d_id.b.domain,
			    fcport->d_id.b.al_pa, fcport->d_id.b.area,
			    fcport->loop_id);

			if (sess->deleted)
				qlt_undelete_sess(sess);

			kref_get(&sess->se_sess->sess_kref);
			ha->tgt.tgt_ops->update_sess(sess, fcport->d_id, fcport->loop_id,
						(fcport->flags & FCF_CONF_COMP_SUPPORTED));

			if (sess->local && !local)
				sess->local = 0;
			spin_unlock_irqrestore(&ha->hardware_lock, flags);

			return sess;
		}
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	sess = kzalloc(sizeof(*sess), GFP_KERNEL);
	if (!sess) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf04a,
		    "qla_target(%u): session allocation failed, all commands "
		    "from port %8phC will be refused", vha->vp_idx,
		    fcport->port_name);

		return NULL;
	}
	sess->tgt = vha->vha_tgt.qla_tgt;
	sess->vha = vha;
	sess->s_id = fcport->d_id;
	sess->loop_id = fcport->loop_id;
	sess->local = local;

	ql_dbg(ql_dbg_tgt_mgt, vha, 0xf006,
	    "Adding sess %p to tgt %p via ->check_initiator_node_acl()\n",
	    sess, vha->vha_tgt.qla_tgt);

	be_sid[0] = sess->s_id.b.domain;
	be_sid[1] = sess->s_id.b.area;
	be_sid[2] = sess->s_id.b.al_pa;
	/*
	 * Determine if this fc_port->port_name is allowed to access
	 * target mode using explict NodeACLs+MappedLUNs, or using
	 * TPG demo mode.  If this is successful a target mode FC nexus
	 * is created.
	 */
	if (ha->tgt.tgt_ops->check_initiator_node_acl(vha,
	    &fcport->port_name[0], sess, &be_sid[0], fcport->loop_id) < 0) {
		kfree(sess);
		return NULL;
	}
	/*
	 * Take an extra reference to ->sess_kref here to handle qla_tgt_sess
	 * access across ->hardware_lock reaquire.
	 */
	kref_get(&sess->se_sess->sess_kref);

	sess->conf_compl_supported = (fcport->flags & FCF_CONF_COMP_SUPPORTED);
	BUILD_BUG_ON(sizeof(sess->port_name) != sizeof(fcport->port_name));
	memcpy(sess->port_name, fcport->port_name, sizeof(sess->port_name));

	spin_lock_irqsave(&ha->hardware_lock, flags);
	list_add_tail(&sess->sess_list_entry, &vha->vha_tgt.qla_tgt->sess_list);
	vha->vha_tgt.qla_tgt->sess_count++;
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	ql_dbg(ql_dbg_tgt_mgt, vha, 0xf04b,
	    "qla_target(%d): %ssession for wwn %8phC (loop_id %d, "
	    "s_id %x:%x:%x, confirmed completion %ssupported) added\n",
	    vha->vp_idx, local ?  "local " : "", fcport->port_name,
	    fcport->loop_id, sess->s_id.b.domain, sess->s_id.b.area,
	    sess->s_id.b.al_pa, sess->conf_compl_supported ?  "" : "not ");

	return sess;
}

/*
 * Called from drivers/scsi/qla2xxx/qla_init.c:qla2x00_reg_remote_port()
 */
void qlt_fc_port_added(struct scsi_qla_host *vha, fc_port_t *fcport)
{
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt *tgt = vha->vha_tgt.qla_tgt;
	struct qla_tgt_sess *sess;
	unsigned long flags;

	if (!vha->hw->tgt.tgt_ops)
		return;

	if (!tgt || (fcport->port_type != FCT_INITIATOR))
		return;

	if (qla_ini_mode_enabled(vha))
		return;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	if (tgt->tgt_stop) {
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
		return;
	}
	sess = qlt_find_sess_by_port_name(tgt, fcport->port_name);
	if (!sess) {
		spin_unlock_irqrestore(&ha->hardware_lock, flags);

		mutex_lock(&vha->vha_tgt.tgt_mutex);
		sess = qlt_create_sess(vha, fcport, false);
		mutex_unlock(&vha->vha_tgt.tgt_mutex);

		spin_lock_irqsave(&ha->hardware_lock, flags);
	} else {
		kref_get(&sess->se_sess->sess_kref);

		if (sess->deleted) {
			qlt_undelete_sess(sess);

			ql_dbg(ql_dbg_tgt_mgt, vha, 0xf04c,
			    "qla_target(%u): %ssession for port %8phC "
			    "(loop ID %d) reappeared\n", vha->vp_idx,
			    sess->local ? "local " : "", sess->port_name,
			    sess->loop_id);

			ql_dbg(ql_dbg_tgt_mgt, vha, 0xf007,
			    "Reappeared sess %p\n", sess);
		}
		ha->tgt.tgt_ops->update_sess(sess, fcport->d_id, fcport->loop_id,
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
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

void qlt_fc_port_deleted(struct scsi_qla_host *vha, fc_port_t *fcport)
{
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt *tgt = vha->vha_tgt.qla_tgt;
	struct qla_tgt_sess *sess;
	unsigned long flags;

	if (!vha->hw->tgt.tgt_ops)
		return;

	if (!tgt || (fcport->port_type != FCT_INITIATOR))
		return;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	if (tgt->tgt_stop) {
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
		return;
	}
	sess = qlt_find_sess_by_port_name(tgt, fcport->port_name);
	if (!sess) {
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
		return;
	}

	ql_dbg(ql_dbg_tgt_mgt, vha, 0xf008, "qla_tgt_fc_port_deleted %p", sess);

	sess->local = 1;
	qlt_schedule_sess_for_deletion(sess, false);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
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
	spin_lock_irqsave(&ha->hardware_lock, flags);
	ql_dbg(ql_dbg_tgt, tgt->vha, 0xe002,
	    "tgt %p, empty(sess_list)=%d sess_count=%d\n",
	    tgt, list_empty(&tgt->sess_list), tgt->sess_count);
	res = (tgt->sess_count == 0);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

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
			return -EPERM;
		}
	}
	if (tgt->tgt_stop || tgt->tgt_stopped) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf04e,
		    "Already in tgt->tgt_stop or tgt_stopped state\n");
		mutex_unlock(&qla_tgt_mutex);
		return -EPERM;
	}

	ql_dbg(ql_dbg_tgt, vha, 0xe003, "Stopping target for host %ld(%p)\n",
	    vha->host_no, vha);
	/*
	 * Mutex needed to sync with qla_tgt_fc_port_[added,deleted].
	 * Lock is needed, because we still can get an incoming packet.
	 */
	mutex_lock(&vha->vha_tgt.tgt_mutex);
	spin_lock_irqsave(&ha->hardware_lock, flags);
	tgt->tgt_stop = 1;
	qlt_clear_tgt_db(tgt);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	mutex_unlock(&vha->vha_tgt.tgt_mutex);
	mutex_unlock(&qla_tgt_mutex);

	flush_delayed_work(&tgt->sess_del_work);

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
	    "Waiting for tgt %p: list_empty(sess_list)=%d "
	    "sess_count=%d\n", tgt, list_empty(&tgt->sess_list),
	    tgt->sess_count);

	wait_event(tgt->waitQ, test_tgt_sess_count(tgt));

	/* Big hammer */
	if (!ha->flags.host_shutting_down && qla_tgt_mode_enabled(vha))
		qlt_disable_vha(vha);

	/* Wait for sessions to clear out (just in case) */
	wait_event(tgt->waitQ, test_tgt_sess_count(tgt));
	return 0;
}
EXPORT_SYMBOL(qlt_stop_phase1);

/* Called by tcm_qla2xxx configfs code */
void qlt_stop_phase2(struct qla_tgt *tgt)
{
	struct qla_hw_data *ha = tgt->ha;
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);
	unsigned long flags;

	if (tgt->tgt_stopped) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf04f,
		    "Already in tgt->tgt_stopped state\n");
		dump_stack();
		return;
	}

	ql_dbg(ql_dbg_tgt_mgt, vha, 0xf00b,
	    "Waiting for %d IRQ commands to complete (tgt %p)",
	    tgt->irq_cmd_count, tgt);

	mutex_lock(&vha->vha_tgt.tgt_mutex);
	spin_lock_irqsave(&ha->hardware_lock, flags);
	while (tgt->irq_cmd_count != 0) {
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
		udelay(2);
		spin_lock_irqsave(&ha->hardware_lock, flags);
	}
	tgt->tgt_stop = 0;
	tgt->tgt_stopped = 1;
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	mutex_unlock(&vha->vha_tgt.tgt_mutex);

	ql_dbg(ql_dbg_tgt_mgt, vha, 0xf00c, "Stop of tgt %p finished",
	    tgt);
}
EXPORT_SYMBOL(qlt_stop_phase2);

/* Called from qlt_remove_target() -> qla2x00_remove_one() */
static void qlt_release(struct qla_tgt *tgt)
{
	scsi_qla_host_t *vha = tgt->vha;

	if ((vha->vha_tgt.qla_tgt != NULL) && !tgt->tgt_stopped)
		qlt_stop_phase2(tgt);

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
static void qlt_send_notify_ack(struct scsi_qla_host *vha,
	struct imm_ntfy_from_isp *ntfy,
	uint32_t add_flags, uint16_t resp_code, int resp_code_valid,
	uint16_t srr_flags, uint16_t srr_reject_code, uint8_t srr_explan)
{
	struct qla_hw_data *ha = vha->hw;
	request_t *pkt;
	struct nack_to_isp *nack;

	ql_dbg(ql_dbg_tgt, vha, 0xe004, "Sending NOTIFY_ACK (ha=%p)\n", ha);

	/* Send marker if required */
	if (qlt_issue_marker(vha, 1) != QLA_SUCCESS)
		return;

	pkt = (request_t *)qla2x00_alloc_iocbs(vha, NULL);
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

	nack->u.isp24.nport_handle = ntfy->u.isp24.nport_handle;
	if (le16_to_cpu(ntfy->u.isp24.status) == IMM_NTFY_ELS) {
		nack->u.isp24.flags = ntfy->u.isp24.flags &
			__constant_cpu_to_le32(NOTIFY24XX_FLAGS_PUREX_IOCB);
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
	qla2x00_start_iocbs(vha, vha->req);
}

/*
 * ha->hardware_lock supposed to be held on entry. Might drop it, then reaquire
 */
static void qlt_24xx_send_abts_resp(struct scsi_qla_host *vha,
	struct abts_recv_from_24xx *abts, uint32_t status,
	bool ids_reversed)
{
	struct qla_hw_data *ha = vha->hw;
	struct abts_resp_to_24xx *resp;
	uint32_t f_ctl;
	uint8_t *p;

	ql_dbg(ql_dbg_tgt, vha, 0xe006,
	    "Sending task mgmt ABTS response (ha=%p, atio=%p, status=%x\n",
	    ha, abts, status);

	/* Send marker if required */
	if (qlt_issue_marker(vha, 1) != QLA_SUCCESS)
		return;

	resp = (struct abts_resp_to_24xx *)qla2x00_alloc_iocbs_ready(vha, NULL);
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
	f_ctl = __constant_cpu_to_le32(F_CTL_EXCH_CONTEXT_RESP |
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
	qla2x00_start_iocbs(vha, vha->req);
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
	/* Send marker if required */
	if (qlt_issue_marker(vha, 1) != QLA_SUCCESS)
		return;

	ctio = (struct ctio7_to_24xx *)qla2x00_alloc_iocbs_ready(vha, NULL);
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
	ctio->timeout = __constant_cpu_to_le16(QLA_TGT_TIMEOUT);
	ctio->vp_index = vha->vp_idx;
	ctio->initiator_id[0] = entry->fcp_hdr_le.d_id[0];
	ctio->initiator_id[1] = entry->fcp_hdr_le.d_id[1];
	ctio->initiator_id[2] = entry->fcp_hdr_le.d_id[2];
	ctio->exchange_addr = entry->exchange_addr_to_abort;
	ctio->u.status1.flags =
	    __constant_cpu_to_le16(CTIO7_FLAGS_STATUS_MODE_1 |
		CTIO7_FLAGS_TERMINATE);
	ctio->u.status1.ox_id = cpu_to_le16(entry->fcp_hdr_le.ox_id);

	/* Memory Barrier */
	wmb();
	qla2x00_start_iocbs(vha, vha->req);

	qlt_24xx_send_abts_resp(vha, (struct abts_recv_from_24xx *)entry,
	    FCP_TMF_CMPL, true);
}

/* ha->hardware_lock supposed to be held on entry */
static int __qlt_24xx_handle_abts(struct scsi_qla_host *vha,
	struct abts_recv_from_24xx *abts, struct qla_tgt_sess *sess)
{
	struct qla_hw_data *ha = vha->hw;
	struct se_session *se_sess = sess->se_sess;
	struct qla_tgt_mgmt_cmd *mcmd;
	struct se_cmd *se_cmd;
	u32 lun = 0;
	int rc;
	bool found_lun = false;

	spin_lock(&se_sess->sess_cmd_lock);
	list_for_each_entry(se_cmd, &se_sess->sess_cmd_list, se_cmd_list) {
		struct qla_tgt_cmd *cmd =
			container_of(se_cmd, struct qla_tgt_cmd, se_cmd);
		if (se_cmd->tag == abts->exchange_addr_to_abort) {
			lun = cmd->unpacked_lun;
			found_lun = true;
			break;
		}
	}
	spin_unlock(&se_sess->sess_cmd_lock);

	if (!found_lun)
		return -ENOENT;

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
	mcmd->reset_count = vha->hw->chip_reset;

	rc = ha->tgt.tgt_ops->handle_tmr(mcmd, lun, TMR_ABORT_TASK,
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
	struct qla_tgt_sess *sess;
	uint32_t tag = abts->exchange_addr_to_abort;
	uint8_t s_id[3];
	int rc;

	if (le32_to_cpu(abts->fcp_hdr_le.parameter) & ABTS_PARAM_ABORT_SEQ) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf053,
		    "qla_target(%d): ABTS: Abort Sequence not "
		    "supported\n", vha->vp_idx);
		qlt_24xx_send_abts_resp(vha, abts, FCP_TMF_REJECTED, false);
		return;
	}

	if (tag == ATIO_EXCHANGE_ADDRESS_UNKNOWN) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf010,
		    "qla_target(%d): ABTS: Unknown Exchange "
		    "Address received\n", vha->vp_idx);
		qlt_24xx_send_abts_resp(vha, abts, FCP_TMF_REJECTED, false);
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

	sess = ha->tgt.tgt_ops->find_sess_by_s_id(vha, s_id);
	if (!sess) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf012,
		    "qla_target(%d): task abort for non-existant session\n",
		    vha->vp_idx);
		rc = qlt_sched_sess_work(vha->vha_tgt.qla_tgt,
		    QLA_TGT_SESS_WORK_ABORT, abts, sizeof(*abts));
		if (rc != 0) {
			qlt_24xx_send_abts_resp(vha, abts, FCP_TMF_REJECTED,
			    false);
		}
		return;
	}

	rc = __qlt_24xx_handle_abts(vha, abts, sess);
	if (rc != 0) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf054,
		    "qla_target(%d): __qlt_24xx_handle_abts() failed: %d\n",
		    vha->vp_idx, rc);
		qlt_24xx_send_abts_resp(vha, abts, FCP_TMF_REJECTED, false);
		return;
	}
}

/*
 * ha->hardware_lock supposed to be held on entry. Might drop it, then reaquire
 */
static void qlt_24xx_send_task_mgmt_ctio(struct scsi_qla_host *ha,
	struct qla_tgt_mgmt_cmd *mcmd, uint32_t resp_code)
{
	struct atio_from_isp *atio = &mcmd->orig_iocb.atio;
	struct ctio7_to_24xx *ctio;
	uint16_t temp;

	ql_dbg(ql_dbg_tgt, ha, 0xe008,
	    "Sending task mgmt CTIO7 (ha=%p, atio=%p, resp_code=%x\n",
	    ha, atio, resp_code);

	/* Send marker if required */
	if (qlt_issue_marker(ha, 1) != QLA_SUCCESS)
		return;

	ctio = (struct ctio7_to_24xx *)qla2x00_alloc_iocbs(ha, NULL);
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
	ctio->timeout = __constant_cpu_to_le16(QLA_TGT_TIMEOUT);
	ctio->vp_index = ha->vp_idx;
	ctio->initiator_id[0] = atio->u.isp24.fcp_hdr.s_id[2];
	ctio->initiator_id[1] = atio->u.isp24.fcp_hdr.s_id[1];
	ctio->initiator_id[2] = atio->u.isp24.fcp_hdr.s_id[0];
	ctio->exchange_addr = atio->u.isp24.exchange_addr;
	ctio->u.status1.flags = (atio->u.isp24.attr << 9) |
	    __constant_cpu_to_le16(CTIO7_FLAGS_STATUS_MODE_1 |
		CTIO7_FLAGS_SEND_STATUS);
	temp = be16_to_cpu(atio->u.isp24.fcp_hdr.ox_id);
	ctio->u.status1.ox_id = cpu_to_le16(temp);
	ctio->u.status1.scsi_status =
	    __constant_cpu_to_le16(SS_RESPONSE_INFO_LEN_VALID);
	ctio->u.status1.response_len = __constant_cpu_to_le16(8);
	ctio->u.status1.sense_data[0] = resp_code;

	/* Memory Barrier */
	wmb();
	qla2x00_start_iocbs(ha, ha->req);
}

void qlt_free_mcmd(struct qla_tgt_mgmt_cmd *mcmd)
{
	mempool_free(mcmd, qla_tgt_mgmt_cmd_mempool);
}
EXPORT_SYMBOL(qlt_free_mcmd);

/* callback from target fabric module code */
void qlt_xmit_tm_rsp(struct qla_tgt_mgmt_cmd *mcmd)
{
	struct scsi_qla_host *vha = mcmd->sess->vha;
	struct qla_hw_data *ha = vha->hw;
	unsigned long flags;

	ql_dbg(ql_dbg_tgt_mgt, vha, 0xf013,
	    "TM response mcmd (%p) status %#x state %#x",
	    mcmd, mcmd->fc_tm_rsp, mcmd->flags);

	spin_lock_irqsave(&ha->hardware_lock, flags);

	if (qla2x00_reset_active(vha) || mcmd->reset_count != ha->chip_reset) {
		/*
		 * Either a chip reset is active or this request was from
		 * previous life, just abort the processing.
		 */
		ql_dbg(ql_dbg_async, vha, 0xe100,
			"RESET-TMR active/old-count/new-count = %d/%d/%d.\n",
			qla2x00_reset_active(vha), mcmd->reset_count,
			ha->chip_reset);
		ha->tgt.tgt_ops->free_mcmd(mcmd);
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
		return;
	}

	if (mcmd->flags == QLA24XX_MGMT_SEND_NACK)
		qlt_send_notify_ack(vha, &mcmd->orig_iocb.imm_ntfy,
		    0, 0, 0, 0, 0, 0);
	else {
		if (mcmd->se_cmd.se_tmr_req->function == TMR_ABORT_TASK)
			qlt_24xx_send_abts_resp(vha, &mcmd->orig_iocb.abts,
			    mcmd->fc_tm_rsp, false);
		else
			qlt_24xx_send_task_mgmt_ctio(vha, mcmd,
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
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}
EXPORT_SYMBOL(qlt_xmit_tm_rsp);

/* No locks */
static int qlt_pci_map_calc_cnt(struct qla_tgt_prm *prm)
{
	struct qla_tgt_cmd *cmd = prm->cmd;

	BUG_ON(cmd->sg_cnt == 0);

	prm->sg = (struct scatterlist *)cmd->sg;
	prm->seg_cnt = pci_map_sg(prm->tgt->ha->pdev, cmd->sg,
	    cmd->sg_cnt, cmd->dma_data_direction);
	if (unlikely(prm->seg_cnt == 0))
		goto out_err;

	prm->cmd->sg_mapped = 1;

	if (cmd->se_cmd.prot_op == TARGET_PROT_NORMAL) {
		/*
		 * If greater than four sg entries then we need to allocate
		 * the continuation entries
		 */
		if (prm->seg_cnt > prm->tgt->datasegs_per_cmd)
			prm->req_cnt += DIV_ROUND_UP(prm->seg_cnt -
			prm->tgt->datasegs_per_cmd,
			prm->tgt->datasegs_per_cont);
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
			prm->prot_seg_cnt = pci_map_sg(prm->tgt->ha->pdev,
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
	ql_dbg(ql_dbg_tgt, prm->cmd->vha, 0xe04d,
	    "qla_target(%d): PCI mapping failed: sg_cnt=%d",
	    0, prm->cmd->sg_cnt);
	return -1;
}

static void qlt_unmap_sg(struct scsi_qla_host *vha, struct qla_tgt_cmd *cmd)
{
	struct qla_hw_data *ha = vha->hw;

	if (!cmd->sg_mapped)
		return;

	pci_unmap_sg(ha->pdev, cmd->sg, cmd->sg_cnt, cmd->dma_data_direction);
	cmd->sg_mapped = 0;

	if (cmd->prot_sg_cnt)
		pci_unmap_sg(ha->pdev, cmd->prot_sg, cmd->prot_sg_cnt,
			cmd->dma_data_direction);

	if (cmd->ctx_dsd_alloced)
		qla2x00_clean_dsd_pool(ha, NULL, cmd);

	if (cmd->ctx)
		dma_pool_free(ha->dl_dma_pool, cmd->ctx, cmd->ctx->crc_ctx_dma);
}

static int qlt_check_reserve_free_req(struct scsi_qla_host *vha,
	uint32_t req_cnt)
{
	uint32_t cnt, cnt_in;

	if (vha->req->cnt < (req_cnt + 2)) {
		cnt = (uint16_t)RD_REG_DWORD(vha->req->req_q_out);
		cnt_in = (uint16_t)RD_REG_DWORD(vha->req->req_q_in);

		if  (vha->req->ring_index < cnt)
			vha->req->cnt = cnt - vha->req->ring_index;
		else
			vha->req->cnt = vha->req->length -
			    (vha->req->ring_index - cnt);
	}

	if (unlikely(vha->req->cnt < (req_cnt + 2))) {
		ql_dbg(ql_dbg_io, vha, 0x305a,
		    "qla_target(%d): There is no room in the request ring: vha->req->ring_index=%d, vha->req->cnt=%d, req_cnt=%d Req-out=%d Req-in=%d Req-Length=%d\n",
		    vha->vp_idx, vha->req->ring_index,
		    vha->req->cnt, req_cnt, cnt, cnt_in, vha->req->length);
		return -EAGAIN;
	}
	vha->req->cnt -= req_cnt;

	return 0;
}

/*
 * ha->hardware_lock supposed to be held on entry. Might drop it, then reaquire
 */
static inline void *qlt_get_req_pkt(struct scsi_qla_host *vha)
{
	/* Adjust ring index. */
	vha->req->ring_index++;
	if (vha->req->ring_index == vha->req->length) {
		vha->req->ring_index = 0;
		vha->req->ring_ptr = vha->req->ring;
	} else {
		vha->req->ring_ptr++;
	}
	return (cont_entry_t *)vha->req->ring_ptr;
}

/* ha->hardware_lock supposed to be held on entry */
static inline uint32_t qlt_make_handle(struct scsi_qla_host *vha)
{
	struct qla_hw_data *ha = vha->hw;
	uint32_t h;

	h = ha->tgt.current_handle;
	/* always increment cmd handle */
	do {
		++h;
		if (h > DEFAULT_OUTSTANDING_COMMANDS)
			h = 1; /* 0 is QLA_TGT_NULL_HANDLE */
		if (h == ha->tgt.current_handle) {
			ql_dbg(ql_dbg_io, vha, 0x305b,
			    "qla_target(%d): Ran out of "
			    "empty cmd slots in ha %p\n", vha->vp_idx, ha);
			h = QLA_TGT_NULL_HANDLE;
			break;
		}
	} while ((h == QLA_TGT_NULL_HANDLE) ||
	    (h == QLA_TGT_SKIP_HANDLE) ||
	    (ha->tgt.cmds[h-1] != NULL));

	if (h != QLA_TGT_NULL_HANDLE)
		ha->tgt.current_handle = h;

	return h;
}

/* ha->hardware_lock supposed to be held on entry */
static int qlt_24xx_build_ctio_pkt(struct qla_tgt_prm *prm,
	struct scsi_qla_host *vha)
{
	uint32_t h;
	struct ctio7_to_24xx *pkt;
	struct qla_hw_data *ha = vha->hw;
	struct atio_from_isp *atio = &prm->cmd->atio;
	uint16_t temp;

	pkt = (struct ctio7_to_24xx *)vha->req->ring_ptr;
	prm->pkt = pkt;
	memset(pkt, 0, sizeof(*pkt));

	pkt->entry_type = CTIO_TYPE7;
	pkt->entry_count = (uint8_t)prm->req_cnt;
	pkt->vp_index = vha->vp_idx;

	h = qlt_make_handle(vha);
	if (unlikely(h == QLA_TGT_NULL_HANDLE)) {
		/*
		 * CTIO type 7 from the firmware doesn't provide a way to
		 * know the initiator's LOOP ID, hence we can't find
		 * the session and, so, the command.
		 */
		return -EAGAIN;
	} else
		ha->tgt.cmds[h-1] = prm->cmd;

	pkt->handle = h | CTIO_COMPLETION_HANDLE_MARK;
	pkt->nport_handle = prm->cmd->loop_id;
	pkt->timeout = __constant_cpu_to_le16(QLA_TGT_TIMEOUT);
	pkt->initiator_id[0] = atio->u.isp24.fcp_hdr.s_id[2];
	pkt->initiator_id[1] = atio->u.isp24.fcp_hdr.s_id[1];
	pkt->initiator_id[2] = atio->u.isp24.fcp_hdr.s_id[0];
	pkt->exchange_addr = atio->u.isp24.exchange_addr;
	pkt->u.status0.flags |= (atio->u.isp24.attr << 9);
	temp = be16_to_cpu(atio->u.isp24.fcp_hdr.ox_id);
	pkt->u.status0.ox_id = cpu_to_le16(temp);
	pkt->u.status0.relative_offset = cpu_to_le32(prm->cmd->offset);

	return 0;
}

/*
 * ha->hardware_lock supposed to be held on entry. We have already made sure
 * that there is sufficient amount of request entries to not drop it.
 */
static void qlt_load_cont_data_segments(struct qla_tgt_prm *prm,
	struct scsi_qla_host *vha)
{
	int cnt;
	uint32_t *dword_ptr;
	int enable_64bit_addressing = prm->tgt->tgt_enable_64bit_addr;

	/* Build continuation packets */
	while (prm->seg_cnt > 0) {
		cont_a64_entry_t *cont_pkt64 =
			(cont_a64_entry_t *)qlt_get_req_pkt(vha);

		/*
		 * Make sure that from cont_pkt64 none of
		 * 64-bit specific fields used for 32-bit
		 * addressing. Cast to (cont_entry_t *) for
		 * that.
		 */

		memset(cont_pkt64, 0, sizeof(*cont_pkt64));

		cont_pkt64->entry_count = 1;
		cont_pkt64->sys_define = 0;

		if (enable_64bit_addressing) {
			cont_pkt64->entry_type = CONTINUE_A64_TYPE;
			dword_ptr =
			    (uint32_t *)&cont_pkt64->dseg_0_address;
		} else {
			cont_pkt64->entry_type = CONTINUE_TYPE;
			dword_ptr =
			    (uint32_t *)&((cont_entry_t *)
				cont_pkt64)->dseg_0_address;
		}

		/* Load continuation entry data segments */
		for (cnt = 0;
		    cnt < prm->tgt->datasegs_per_cont && prm->seg_cnt;
		    cnt++, prm->seg_cnt--) {
			*dword_ptr++ =
			    cpu_to_le32(pci_dma_lo32
				(sg_dma_address(prm->sg)));
			if (enable_64bit_addressing) {
				*dword_ptr++ =
				    cpu_to_le32(pci_dma_hi32
					(sg_dma_address
					(prm->sg)));
			}
			*dword_ptr++ = cpu_to_le32(sg_dma_len(prm->sg));

			prm->sg = sg_next(prm->sg);
		}
	}
}

/*
 * ha->hardware_lock supposed to be held on entry. We have already made sure
 * that there is sufficient amount of request entries to not drop it.
 */
static void qlt_load_data_segments(struct qla_tgt_prm *prm,
	struct scsi_qla_host *vha)
{
	int cnt;
	uint32_t *dword_ptr;
	int enable_64bit_addressing = prm->tgt->tgt_enable_64bit_addr;
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
	    (cnt < prm->tgt->datasegs_per_cmd) && prm->seg_cnt;
	    cnt++, prm->seg_cnt--) {
		*dword_ptr++ =
		    cpu_to_le32(pci_dma_lo32(sg_dma_address(prm->sg)));
		if (enable_64bit_addressing) {
			*dword_ptr++ =
			    cpu_to_le32(pci_dma_hi32(
				sg_dma_address(prm->sg)));
		}
		*dword_ptr++ = cpu_to_le32(sg_dma_len(prm->sg));

		prm->sg = sg_next(prm->sg);
	}

	qlt_load_cont_data_segments(prm, vha);
}

static inline int qlt_has_data(struct qla_tgt_cmd *cmd)
{
	return cmd->bufflen > 0;
}

/*
 * Called without ha->hardware_lock held
 */
static int qlt_pre_xmit_response(struct qla_tgt_cmd *cmd,
	struct qla_tgt_prm *prm, int xmit_type, uint8_t scsi_status,
	uint32_t *full_req_cnt)
{
	struct qla_tgt *tgt = cmd->tgt;
	struct scsi_qla_host *vha = tgt->vha;
	struct qla_hw_data *ha = vha->hw;
	struct se_cmd *se_cmd = &cmd->se_cmd;

	if (unlikely(cmd->aborted)) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf014,
		       "qla_target(%d): terminating exchange for aborted cmd=%p (se_cmd=%p, tag=%lld)",
		       vha->vp_idx, cmd, se_cmd, se_cmd->tag);

		cmd->state = QLA_TGT_STATE_ABORTED;
		cmd->cmd_flags |= BIT_6;

		qlt_send_term_exchange(vha, cmd, &cmd->atio, 0);

		/* !! At this point cmd could be already freed !! */
		return QLA_TGT_PRE_XMIT_RESP_CMD_ABORTED;
	}

	prm->cmd = cmd;
	prm->tgt = tgt;
	prm->rq_result = scsi_status;
	prm->sense_buffer = &cmd->sense_buffer[0];
	prm->sense_buffer_len = TRANSPORT_SENSE_BUFFER;
	prm->sg = NULL;
	prm->seg_cnt = -1;
	prm->req_cnt = 1;
	prm->add_status_pkt = 0;

	/* Send marker if required */
	if (qlt_issue_marker(vha, 0) != QLA_SUCCESS)
		return -EFAULT;

	if ((xmit_type & QLA_TGT_XMIT_DATA) && qlt_has_data(cmd)) {
		if  (qlt_pci_map_calc_cnt(prm) != 0)
			return -EAGAIN;
	}

	*full_req_cnt = prm->req_cnt;

	if (se_cmd->se_cmd_flags & SCF_UNDERFLOW_BIT) {
		prm->residual = se_cmd->residual_count;
		ql_dbg(ql_dbg_io + ql_dbg_verbose, vha, 0x305c,
		    "Residual underflow: %d (tag %lld, op %x, bufflen %d, rq_result %x)\n",
		       prm->residual, se_cmd->tag,
		       se_cmd->t_task_cdb ? se_cmd->t_task_cdb[0] : 0,
		       cmd->bufflen, prm->rq_result);
		prm->rq_result |= SS_RESIDUAL_UNDER;
	} else if (se_cmd->se_cmd_flags & SCF_OVERFLOW_BIT) {
		prm->residual = se_cmd->residual_count;
		ql_dbg(ql_dbg_io, vha, 0x305d,
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
			    (IS_FWI2_CAPABLE(ha) &&
			    (prm->rq_result != 0))) {
				prm->add_status_pkt = 1;
				(*full_req_cnt)++;
			}
		}
	}

	return 0;
}

static inline int qlt_need_explicit_conf(struct qla_hw_data *ha,
	struct qla_tgt_cmd *cmd, int sending_sense)
{
	if (ha->tgt.enable_class_2)
		return 0;

	if (sending_sense)
		return cmd->conf_compl_supported;
	else
		return ha->tgt.enable_explicit_conf &&
		    cmd->conf_compl_supported;
}

#ifdef CONFIG_QLA_TGT_DEBUG_SRR
/*
 *  Original taken from the XFS code
 */
static unsigned long qlt_srr_random(void)
{
	static int Inited;
	static unsigned long RandomValue;
	static DEFINE_SPINLOCK(lock);
	/* cycles pseudo-randomly through all values between 1 and 2^31 - 2 */
	register long rv;
	register long lo;
	register long hi;
	unsigned long flags;

	spin_lock_irqsave(&lock, flags);
	if (!Inited) {
		RandomValue = jiffies;
		Inited = 1;
	}
	rv = RandomValue;
	hi = rv / 127773;
	lo = rv % 127773;
	rv = 16807 * lo - 2836 * hi;
	if (rv <= 0)
		rv += 2147483647;
	RandomValue = rv;
	spin_unlock_irqrestore(&lock, flags);
	return rv;
}

static void qlt_check_srr_debug(struct qla_tgt_cmd *cmd, int *xmit_type)
{
#if 0 /* This is not a real status packets lost, so it won't lead to SRR */
	if ((*xmit_type & QLA_TGT_XMIT_STATUS) && (qlt_srr_random() % 200)
	    == 50) {
		*xmit_type &= ~QLA_TGT_XMIT_STATUS;
		ql_dbg(ql_dbg_tgt_mgt, cmd->vha, 0xf015,
		    "Dropping cmd %p (tag %d) status", cmd, se_cmd->tag);
	}
#endif
	/*
	 * It's currently not possible to simulate SRRs for FCP_WRITE without
	 * a physical link layer failure, so don't even try here..
	 */
	if (cmd->dma_data_direction != DMA_FROM_DEVICE)
		return;

	if (qlt_has_data(cmd) && (cmd->sg_cnt > 1) &&
	    ((qlt_srr_random() % 100) == 20)) {
		int i, leave = 0;
		unsigned int tot_len = 0;

		while (leave == 0)
			leave = qlt_srr_random() % cmd->sg_cnt;

		for (i = 0; i < leave; i++)
			tot_len += cmd->sg[i].length;

		ql_dbg(ql_dbg_tgt_mgt, cmd->vha, 0xf016,
		    "Cutting cmd %p (tag %d) buffer"
		    " tail to len %d, sg_cnt %d (cmd->bufflen %d,"
		    " cmd->sg_cnt %d)", cmd, se_cmd->tag, tot_len, leave,
		    cmd->bufflen, cmd->sg_cnt);

		cmd->bufflen = tot_len;
		cmd->sg_cnt = leave;
	}

	if (qlt_has_data(cmd) && ((qlt_srr_random() % 100) == 70)) {
		unsigned int offset = qlt_srr_random() % cmd->bufflen;

		ql_dbg(ql_dbg_tgt_mgt, cmd->vha, 0xf017,
		    "Cutting cmd %p (tag %d) buffer head "
		    "to offset %d (cmd->bufflen %d)", cmd, se_cmd->tag, offset,
		    cmd->bufflen);
		if (offset == 0)
			*xmit_type &= ~QLA_TGT_XMIT_DATA;
		else if (qlt_set_data_offset(cmd, offset)) {
			ql_dbg(ql_dbg_tgt_mgt, cmd->vha, 0xf018,
			    "qlt_set_data_offset() failed (tag %d)", se_cmd->tag);
		}
	}
}
#else
static inline void qlt_check_srr_debug(struct qla_tgt_cmd *cmd, int *xmit_type)
{}
#endif

static void qlt_24xx_init_ctio_to_isp(struct ctio7_to_24xx *ctio,
	struct qla_tgt_prm *prm)
{
	prm->sense_buffer_len = min_t(uint32_t, prm->sense_buffer_len,
	    (uint32_t)sizeof(ctio->u.status1.sense_data));
	ctio->u.status0.flags |=
	    __constant_cpu_to_le16(CTIO7_FLAGS_SEND_STATUS);
	if (qlt_need_explicit_conf(prm->tgt->ha, prm->cmd, 0)) {
		ctio->u.status0.flags |= __constant_cpu_to_le16(
		    CTIO7_FLAGS_EXPLICIT_CONFORM |
		    CTIO7_FLAGS_CONFORM_REQ);
	}
	ctio->u.status0.residual = cpu_to_le32(prm->residual);
	ctio->u.status0.scsi_status = cpu_to_le16(prm->rq_result);
	if (QLA_TGT_SENSE_VALID(prm->sense_buffer)) {
		int i;

		if (qlt_need_explicit_conf(prm->tgt->ha, prm->cmd, 1)) {
			if (prm->cmd->se_cmd.scsi_status != 0) {
				ql_dbg(ql_dbg_tgt, prm->cmd->vha, 0xe017,
				    "Skipping EXPLICIT_CONFORM and "
				    "CTIO7_FLAGS_CONFORM_REQ for FCP READ w/ "
				    "non GOOD status\n");
				goto skip_explict_conf;
			}
			ctio->u.status1.flags |= __constant_cpu_to_le16(
			    CTIO7_FLAGS_EXPLICIT_CONFORM |
			    CTIO7_FLAGS_CONFORM_REQ);
		}
skip_explict_conf:
		ctio->u.status1.flags &=
		    ~__constant_cpu_to_le16(CTIO7_FLAGS_STATUS_MODE_0);
		ctio->u.status1.flags |=
		    __constant_cpu_to_le16(CTIO7_FLAGS_STATUS_MODE_1);
		ctio->u.status1.scsi_status |=
		    __constant_cpu_to_le16(SS_SENSE_LEN_VALID);
		ctio->u.status1.sense_length =
		    cpu_to_le16(prm->sense_buffer_len);
		for (i = 0; i < prm->sense_buffer_len/4; i++)
			((uint32_t *)ctio->u.status1.sense_data)[i] =
				cpu_to_be32(((uint32_t *)prm->sense_buffer)[i]);
#if 0
		if (unlikely((prm->sense_buffer_len % 4) != 0)) {
			static int q;
			if (q < 10) {
				ql_dbg(ql_dbg_tgt, vha, 0xe04f,
				    "qla_target(%d): %d bytes of sense "
				    "lost", prm->tgt->ha->vp_idx,
				    prm->sense_buffer_len % 4);
				q++;
			}
		}
#endif
	} else {
		ctio->u.status1.flags &=
		    ~__constant_cpu_to_le16(CTIO7_FLAGS_STATUS_MODE_0);
		ctio->u.status1.flags |=
		    __constant_cpu_to_le16(CTIO7_FLAGS_STATUS_MODE_1);
		ctio->u.status1.sense_length = 0;
		memset(ctio->u.status1.sense_data, 0,
		    sizeof(ctio->u.status1.sense_data));
	}

	/* Sense with len > 24, is it possible ??? */
}



/* diff  */
static inline int
qlt_hba_err_chk_enabled(struct se_cmd *se_cmd)
{
	/*
	 * Uncomment when corresponding SCSI changes are done.
	 *
	 if (!sp->cmd->prot_chk)
	 return 0;
	 *
	 */
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

/*
 * qla24xx_set_t10dif_tags_from_cmd - Extract Ref and App tags from SCSI command
 *
 */
static inline void
qlt_set_t10dif_tags(struct se_cmd *se_cmd, struct crc_context *ctx)
{
	uint32_t lba = 0xffffffff & se_cmd->t_task_lba;

	/* wait til Mode Sense/Select cmd, modepage Ah, subpage 2
	 * have been immplemented by TCM, before AppTag is avail.
	 * Look for modesense_handlers[]
	 */
	ctx->app_tag = 0;
	ctx->app_tag_mask[0] = 0x0;
	ctx->app_tag_mask[1] = 0x0;

	switch (se_cmd->prot_type) {
	case TARGET_DIF_TYPE0_PROT:
		/*
		 * No check for ql2xenablehba_err_chk, as it would be an
		 * I/O error if hba tag generation is not done.
		 */
		ctx->ref_tag = cpu_to_le32(lba);

		if (!qlt_hba_err_chk_enabled(se_cmd))
			break;

		/* enable ALL bytes of the ref tag */
		ctx->ref_tag_mask[0] = 0xff;
		ctx->ref_tag_mask[1] = 0xff;
		ctx->ref_tag_mask[2] = 0xff;
		ctx->ref_tag_mask[3] = 0xff;
		break;
	/*
	 * For TYpe 1 protection: 16 bit GUARD tag, 32 bit REF tag, and
	 * 16 bit app tag.
	 */
	case TARGET_DIF_TYPE1_PROT:
		ctx->ref_tag = cpu_to_le32(lba);

		if (!qlt_hba_err_chk_enabled(se_cmd))
			break;

		/* enable ALL bytes of the ref tag */
		ctx->ref_tag_mask[0] = 0xff;
		ctx->ref_tag_mask[1] = 0xff;
		ctx->ref_tag_mask[2] = 0xff;
		ctx->ref_tag_mask[3] = 0xff;
		break;
	/*
	 * For TYPE 2 protection: 16 bit GUARD + 32 bit REF tag has to
	 * match LBA in CDB + N
	 */
	case TARGET_DIF_TYPE2_PROT:
		ctx->ref_tag = cpu_to_le32(lba);

		if (!qlt_hba_err_chk_enabled(se_cmd))
			break;

		/* enable ALL bytes of the ref tag */
		ctx->ref_tag_mask[0] = 0xff;
		ctx->ref_tag_mask[1] = 0xff;
		ctx->ref_tag_mask[2] = 0xff;
		ctx->ref_tag_mask[3] = 0xff;
		break;

	/* For Type 3 protection: 16 bit GUARD only */
	case TARGET_DIF_TYPE3_PROT:
		ctx->ref_tag_mask[0] = ctx->ref_tag_mask[1] =
			ctx->ref_tag_mask[2] = ctx->ref_tag_mask[3] = 0x00;
		break;
	}
}


static inline int
qlt_build_ctio_crc2_pkt(struct qla_tgt_prm *prm, scsi_qla_host_t *vha)
{
	uint32_t		*cur_dsd;
	int			sgc;
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
	uint16_t t16;

	sgc = 0;
	ha = vha->hw;

	pkt = (struct ctio_crc2_to_fw *)vha->req->ring_ptr;
	prm->pkt = pkt;
	memset(pkt, 0, sizeof(*pkt));

	ql_dbg(ql_dbg_tgt, vha, 0xe071,
		"qla_target(%d):%s: se_cmd[%p] CRC2 prot_op[0x%x] cmd prot sg:cnt[%p:%x] lba[%llu]\n",
		vha->vp_idx, __func__, se_cmd, se_cmd->prot_op,
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
	pkt->vp_index = vha->vp_idx;

	h = qlt_make_handle(vha);
	if (unlikely(h == QLA_TGT_NULL_HANDLE)) {
		/*
		 * CTIO type 7 from the firmware doesn't provide a way to
		 * know the initiator's LOOP ID, hence we can't find
		 * the session and, so, the command.
		 */
		return -EAGAIN;
	} else
		ha->tgt.cmds[h-1] = prm->cmd;


	pkt->handle  = h | CTIO_COMPLETION_HANDLE_MARK;
	pkt->nport_handle = prm->cmd->loop_id;
	pkt->timeout = __constant_cpu_to_le16(QLA_TGT_TIMEOUT);
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
		pkt->flags = __constant_cpu_to_le16(CTIO7_FLAGS_DATA_IN);
	else if (cmd->dma_data_direction == DMA_FROM_DEVICE)
		pkt->flags = __constant_cpu_to_le16(CTIO7_FLAGS_DATA_OUT);


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

	qlt_set_t10dif_tags(se_cmd, crc_ctx_pkt);

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
	crc_ctx_pkt->guard_seed = __constant_cpu_to_le16(0);


	/* Walks data segments */
	pkt->flags |= __constant_cpu_to_le16(CTIO7_FLAGS_DSD_PTR);

	if (!bundling && prm->prot_seg_cnt) {
		if (qla24xx_walk_and_build_sglist_no_difb(ha, NULL, cur_dsd,
			prm->tot_dsds, cmd))
			goto crc_queuing_error;
	} else if (qla24xx_walk_and_build_sglist(ha, NULL, cur_dsd,
		(prm->tot_dsds - prm->prot_seg_cnt), cmd))
		goto crc_queuing_error;

	if (bundling && prm->prot_seg_cnt) {
		/* Walks dif segments */
		pkt->add_flags |= CTIO_CRC2_AF_DIF_DSD_ENA;

		cur_dsd = (uint32_t *) &crc_ctx_pkt->u.bundling.dif_address;
		if (qla24xx_walk_and_build_prot_sglist(ha, NULL, cur_dsd,
			prm->prot_seg_cnt, cmd))
			goto crc_queuing_error;
	}
	return QLA_SUCCESS;

crc_queuing_error:
	/* Cleanup will be performed by the caller */

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
	struct qla_hw_data *ha = vha->hw;
	struct ctio7_to_24xx *pkt;
	struct qla_tgt_prm prm;
	uint32_t full_req_cnt = 0;
	unsigned long flags = 0;
	int res;

	memset(&prm, 0, sizeof(prm));
	qlt_check_srr_debug(cmd, &xmit_type);

	ql_dbg(ql_dbg_tgt, cmd->vha, 0xe018,
	    "is_send_status=%d, cmd->bufflen=%d, cmd->sg_cnt=%d, cmd->dma_data_direction=%d se_cmd[%p]\n",
	    (xmit_type & QLA_TGT_XMIT_STATUS) ?
	    1 : 0, cmd->bufflen, cmd->sg_cnt, cmd->dma_data_direction,
	    &cmd->se_cmd);

	res = qlt_pre_xmit_response(cmd, &prm, xmit_type, scsi_status,
	    &full_req_cnt);
	if (unlikely(res != 0)) {
		if (res == QLA_TGT_PRE_XMIT_RESP_CMD_ABORTED)
			return 0;

		return res;
	}

	spin_lock_irqsave(&ha->hardware_lock, flags);

	if (qla2x00_reset_active(vha) || cmd->reset_count != ha->chip_reset) {
		/*
		 * Either a chip reset is active or this request was from
		 * previous life, just abort the processing.
		 */
		cmd->state = QLA_TGT_STATE_PROCESSED;
		qlt_abort_cmd_on_host_reset(cmd->vha, cmd);
		ql_dbg(ql_dbg_async, vha, 0xe101,
			"RESET-RSP active/old-count/new-count = %d/%d/%d.\n",
			qla2x00_reset_active(vha), cmd->reset_count,
			ha->chip_reset);
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
		return 0;
	}

	/* Does F/W have an IOCBs for this request */
	res = qlt_check_reserve_free_req(vha, full_req_cnt);
	if (unlikely(res))
		goto out_unmap_unlock;

	if (cmd->se_cmd.prot_op && (xmit_type & QLA_TGT_XMIT_DATA))
		res = qlt_build_ctio_crc2_pkt(&prm, vha);
	else
		res = qlt_24xx_build_ctio_pkt(&prm, vha);
	if (unlikely(res != 0))
		goto out_unmap_unlock;


	pkt = (struct ctio7_to_24xx *)prm.pkt;

	if (qlt_has_data(cmd) && (xmit_type & QLA_TGT_XMIT_DATA)) {
		pkt->u.status0.flags |=
		    __constant_cpu_to_le16(CTIO7_FLAGS_DATA_IN |
			CTIO7_FLAGS_STATUS_MODE_0);

		if (cmd->se_cmd.prot_op == TARGET_PROT_NORMAL)
			qlt_load_data_segments(&prm, vha);

		if (prm.add_status_pkt == 0) {
			if (xmit_type & QLA_TGT_XMIT_STATUS) {
				pkt->u.status0.scsi_status =
				    cpu_to_le16(prm.rq_result);
				pkt->u.status0.residual =
				    cpu_to_le32(prm.residual);
				pkt->u.status0.flags |= __constant_cpu_to_le16(
				    CTIO7_FLAGS_SEND_STATUS);
				if (qlt_need_explicit_conf(ha, cmd, 0)) {
					pkt->u.status0.flags |=
					    __constant_cpu_to_le16(
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
				(struct ctio7_to_24xx *)qlt_get_req_pkt(vha);

			ql_dbg(ql_dbg_io, vha, 0x305e,
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
			ctio->u.status1.flags &= ~__constant_cpu_to_le16(
			    CTIO7_FLAGS_DATA_IN);

			/* Real finish is ctio_m1's finish */
			pkt->handle |= CTIO_INTERMEDIATE_HANDLE_MARK;
			pkt->u.status0.flags |= __constant_cpu_to_le16(
			    CTIO7_FLAGS_DONT_RET_CTIO);

			/* qlt_24xx_init_ctio_to_isp will correct
			 * all neccessary fields that's part of CTIO7.
			 * There should be no residual of CTIO-CRC2 data.
			 */
			qlt_24xx_init_ctio_to_isp((struct ctio7_to_24xx *)ctio,
			    &prm);
			pr_debug("Status CTIO7: %p\n", ctio);
		}
	} else
		qlt_24xx_init_ctio_to_isp(pkt, &prm);


	cmd->state = QLA_TGT_STATE_PROCESSED; /* Mid-level is done processing */
	cmd->cmd_sent_to_fw = 1;

	/* Memory Barrier */
	wmb();
	qla2x00_start_iocbs(vha, vha->req);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return 0;

out_unmap_unlock:
	qlt_unmap_sg(vha, cmd);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return res;
}
EXPORT_SYMBOL(qlt_xmit_response);

int qlt_rdy_to_xfer(struct qla_tgt_cmd *cmd)
{
	struct ctio7_to_24xx *pkt;
	struct scsi_qla_host *vha = cmd->vha;
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt *tgt = cmd->tgt;
	struct qla_tgt_prm prm;
	unsigned long flags;
	int res = 0;

	memset(&prm, 0, sizeof(prm));
	prm.cmd = cmd;
	prm.tgt = tgt;
	prm.sg = NULL;
	prm.req_cnt = 1;

	/* Send marker if required */
	if (qlt_issue_marker(vha, 0) != QLA_SUCCESS)
		return -EIO;

	/* Calculate number of entries and segments required */
	if (qlt_pci_map_calc_cnt(&prm) != 0)
		return -EAGAIN;

	spin_lock_irqsave(&ha->hardware_lock, flags);

	if (qla2x00_reset_active(vha) || cmd->reset_count != ha->chip_reset) {
		/*
		 * Either a chip reset is active or this request was from
		 * previous life, just abort the processing.
		 */
		cmd->state = QLA_TGT_STATE_NEED_DATA;
		qlt_abort_cmd_on_host_reset(cmd->vha, cmd);
		ql_dbg(ql_dbg_async, vha, 0xe102,
			"RESET-XFR active/old-count/new-count = %d/%d/%d.\n",
			qla2x00_reset_active(vha), cmd->reset_count,
			ha->chip_reset);
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
		return 0;
	}

	/* Does F/W have an IOCBs for this request */
	res = qlt_check_reserve_free_req(vha, prm.req_cnt);
	if (res != 0)
		goto out_unlock_free_unmap;
	if (cmd->se_cmd.prot_op)
		res = qlt_build_ctio_crc2_pkt(&prm, vha);
	else
		res = qlt_24xx_build_ctio_pkt(&prm, vha);

	if (unlikely(res != 0))
		goto out_unlock_free_unmap;
	pkt = (struct ctio7_to_24xx *)prm.pkt;
	pkt->u.status0.flags |= __constant_cpu_to_le16(CTIO7_FLAGS_DATA_OUT |
	    CTIO7_FLAGS_STATUS_MODE_0);

	if (cmd->se_cmd.prot_op == TARGET_PROT_NORMAL)
		qlt_load_data_segments(&prm, vha);

	cmd->state = QLA_TGT_STATE_NEED_DATA;
	cmd->cmd_sent_to_fw = 1;

	/* Memory Barrier */
	wmb();
	qla2x00_start_iocbs(vha, vha->req);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return res;

out_unlock_free_unmap:
	qlt_unmap_sg(vha, cmd);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return res;
}
EXPORT_SYMBOL(qlt_rdy_to_xfer);


/*
 * Checks the guard or meta-data for the type of error
 * detected by the HBA.
 */
static inline int
qlt_handle_dif_error(struct scsi_qla_host *vha, struct qla_tgt_cmd *cmd,
		struct ctio_crc_from_fw *sts)
{
	uint8_t		*ap = &sts->actual_dif[0];
	uint8_t		*ep = &sts->expected_dif[0];
	uint32_t	e_ref_tag, a_ref_tag;
	uint16_t	e_app_tag, a_app_tag;
	uint16_t	e_guard, a_guard;
	uint64_t	lba = cmd->se_cmd.t_task_lba;

	a_guard   = be16_to_cpu(*(uint16_t *)(ap + 0));
	a_app_tag = be16_to_cpu(*(uint16_t *)(ap + 2));
	a_ref_tag = be32_to_cpu(*(uint32_t *)(ap + 4));

	e_guard   = be16_to_cpu(*(uint16_t *)(ep + 0));
	e_app_tag = be16_to_cpu(*(uint16_t *)(ep + 2));
	e_ref_tag = be32_to_cpu(*(uint32_t *)(ep + 4));

	ql_dbg(ql_dbg_tgt, vha, 0xe075,
	    "iocb(s) %p Returned STATUS.\n", sts);

	ql_dbg(ql_dbg_tgt, vha, 0xf075,
	    "dif check TGT cdb 0x%x lba 0x%llx: [Actual|Expected] Ref Tag[0x%x|0x%x], App Tag [0x%x|0x%x], Guard [0x%x|0x%x]\n",
	    cmd->atio.u.isp24.fcp_cmnd.cdb[0], lba,
	    a_ref_tag, e_ref_tag, a_app_tag, e_app_tag, a_guard, e_guard);

	/*
	 * Ignore sector if:
	 * For type     3: ref & app tag is all 'f's
	 * For type 0,1,2: app tag is all 'f's
	 */
	if ((a_app_tag == 0xffff) &&
	    ((cmd->se_cmd.prot_type != TARGET_DIF_TYPE3_PROT) ||
	     (a_ref_tag == 0xffffffff))) {
		uint32_t blocks_done;

		/* 2TB boundary case covered automatically with this */
		blocks_done = e_ref_tag - (uint32_t)lba + 1;
		cmd->se_cmd.bad_sector = e_ref_tag;
		cmd->se_cmd.pi_err = 0;
		ql_dbg(ql_dbg_tgt, vha, 0xf074,
			"need to return scsi good\n");

		/* Update protection tag */
		if (cmd->prot_sg_cnt) {
			uint32_t i, j = 0, k = 0, num_ent;
			struct scatterlist *sg, *sgl;


			sgl = cmd->prot_sg;

			/* Patch the corresponding protection tags */
			for_each_sg(sgl, sg, cmd->prot_sg_cnt, i) {
				num_ent = sg_dma_len(sg) / 8;
				if (k + num_ent < blocks_done) {
					k += num_ent;
					continue;
				}
				j = blocks_done - k - 1;
				k = blocks_done;
				break;
			}

			if (k != blocks_done) {
				ql_log(ql_log_warn, vha, 0xf076,
				    "unexpected tag values tag:lba=%u:%llu)\n",
				    e_ref_tag, (unsigned long long)lba);
				goto out;
			}

#if 0
			struct sd_dif_tuple *spt;
			/* TODO:
			 * This section came from initiator. Is it valid here?
			 * should ulp be override with actual val???
			 */
			spt = page_address(sg_page(sg)) + sg->offset;
			spt += j;

			spt->app_tag = 0xffff;
			if (cmd->se_cmd.prot_type == SCSI_PROT_DIF_TYPE3)
				spt->ref_tag = 0xffffffff;
#endif
		}

		return 0;
	}

	/* check guard */
	if (e_guard != a_guard) {
		cmd->se_cmd.pi_err = TCM_LOGICAL_BLOCK_GUARD_CHECK_FAILED;
		cmd->se_cmd.bad_sector = cmd->se_cmd.t_task_lba;

		ql_log(ql_log_warn, vha, 0xe076,
		    "Guard ERR: cdb 0x%x lba 0x%llx: [Actual|Expected] Ref Tag[0x%x|0x%x], App Tag [0x%x|0x%x], Guard [0x%x|0x%x] cmd=%p\n",
		    cmd->atio.u.isp24.fcp_cmnd.cdb[0], lba,
		    a_ref_tag, e_ref_tag, a_app_tag, e_app_tag,
		    a_guard, e_guard, cmd);
		goto out;
	}

	/* check ref tag */
	if (e_ref_tag != a_ref_tag) {
		cmd->se_cmd.pi_err = TCM_LOGICAL_BLOCK_REF_TAG_CHECK_FAILED;
		cmd->se_cmd.bad_sector = e_ref_tag;

		ql_log(ql_log_warn, vha, 0xe077,
			"Ref Tag ERR: cdb 0x%x lba 0x%llx: [Actual|Expected] Ref Tag[0x%x|0x%x], App Tag [0x%x|0x%x], Guard [0x%x|0x%x] cmd=%p\n",
			cmd->atio.u.isp24.fcp_cmnd.cdb[0], lba,
			a_ref_tag, e_ref_tag, a_app_tag, e_app_tag,
			a_guard, e_guard, cmd);
		goto out;
	}

	/* check appl tag */
	if (e_app_tag != a_app_tag) {
		cmd->se_cmd.pi_err = TCM_LOGICAL_BLOCK_APP_TAG_CHECK_FAILED;
		cmd->se_cmd.bad_sector = cmd->se_cmd.t_task_lba;

		ql_log(ql_log_warn, vha, 0xe078,
			"App Tag ERR: cdb 0x%x lba 0x%llx: [Actual|Expected] Ref Tag[0x%x|0x%x], App Tag [0x%x|0x%x], Guard [0x%x|0x%x] cmd=%p\n",
			cmd->atio.u.isp24.fcp_cmnd.cdb[0], lba,
			a_ref_tag, e_ref_tag, a_app_tag, e_app_tag,
			a_guard, e_guard, cmd);
		goto out;
	}
out:
	return 1;
}


/* If hardware_lock held on entry, might drop it, then reaquire */
/* This function sends the appropriate CTIO to ISP 2xxx or 24xx */
static int __qlt_send_term_exchange(struct scsi_qla_host *vha,
	struct qla_tgt_cmd *cmd,
	struct atio_from_isp *atio)
{
	struct ctio7_to_24xx *ctio24;
	struct qla_hw_data *ha = vha->hw;
	request_t *pkt;
	int ret = 0;
	uint16_t temp;

	ql_dbg(ql_dbg_tgt, vha, 0xe01c, "Sending TERM EXCH CTIO (ha=%p)\n", ha);

	pkt = (request_t *)qla2x00_alloc_iocbs_ready(vha, NULL);
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

	pkt->entry_count = 1;
	pkt->handle = QLA_TGT_SKIP_HANDLE | CTIO_COMPLETION_HANDLE_MARK;

	ctio24 = (struct ctio7_to_24xx *)pkt;
	ctio24->entry_type = CTIO_TYPE7;
	ctio24->nport_handle = cmd ? cmd->loop_id : CTIO7_NHANDLE_UNRECOGNIZED;
	ctio24->timeout = __constant_cpu_to_le16(QLA_TGT_TIMEOUT);
	ctio24->vp_index = vha->vp_idx;
	ctio24->initiator_id[0] = atio->u.isp24.fcp_hdr.s_id[2];
	ctio24->initiator_id[1] = atio->u.isp24.fcp_hdr.s_id[1];
	ctio24->initiator_id[2] = atio->u.isp24.fcp_hdr.s_id[0];
	ctio24->exchange_addr = atio->u.isp24.exchange_addr;
	ctio24->u.status1.flags = (atio->u.isp24.attr << 9) |
	    __constant_cpu_to_le16(CTIO7_FLAGS_STATUS_MODE_1 |
		CTIO7_FLAGS_TERMINATE);
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
	qla2x00_start_iocbs(vha, vha->req);
	return ret;
}

static void qlt_send_term_exchange(struct scsi_qla_host *vha,
	struct qla_tgt_cmd *cmd, struct atio_from_isp *atio, int ha_locked)
{
	unsigned long flags;
	int rc;

	if (qlt_issue_marker(vha, ha_locked) < 0)
		return;

	if (ha_locked) {
		rc = __qlt_send_term_exchange(vha, cmd, atio);
		if (rc == -ENOMEM)
			qlt_alloc_qfull_cmd(vha, atio, 0, 0);
		goto done;
	}
	spin_lock_irqsave(&vha->hw->hardware_lock, flags);
	rc = __qlt_send_term_exchange(vha, cmd, atio);
	if (rc == -ENOMEM)
		qlt_alloc_qfull_cmd(vha, atio, 0, 0);
	spin_unlock_irqrestore(&vha->hw->hardware_lock, flags);

done:
	if (cmd && ((cmd->state != QLA_TGT_STATE_ABORTED) ||
	    !cmd->cmd_sent_to_fw)) {
		if (!ha_locked && !in_interrupt())
			msleep(250); /* just in case */

		qlt_unmap_sg(vha, cmd);
		vha->hw->tgt.tgt_ops->free_cmd(cmd);
	}
	return;
}

static void qlt_init_term_exchange(struct scsi_qla_host *vha)
{
	struct list_head free_list;
	struct qla_tgt_cmd *cmd, *tcmd;

	vha->hw->tgt.leak_exchg_thresh_hold =
	    (vha->hw->fw_xcb_count/100) * LEAK_EXCHG_THRESH_HOLD_PERCENT;

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
		    total_leaked, vha->hw->fw_xcb_count);

		if (IS_P3P_TYPE(vha->hw))
			set_bit(FCOE_CTX_RESET_NEEDED, &vha->dpc_flags);
		else
			set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
		qla2xxx_wake_dpc(vha);
	}

}

void qlt_free_cmd(struct qla_tgt_cmd *cmd)
{
	struct qla_tgt_sess *sess = cmd->sess;

	ql_dbg(ql_dbg_tgt, cmd->vha, 0xe074,
	    "%s: se_cmd[%p] ox_id %04x\n",
	    __func__, &cmd->se_cmd,
	    be16_to_cpu(cmd->atio.u.isp24.fcp_hdr.ox_id));

	BUG_ON(cmd->cmd_in_wq);

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

/* ha->hardware_lock supposed to be held on entry */
static int qlt_prepare_srr_ctio(struct scsi_qla_host *vha,
	struct qla_tgt_cmd *cmd, void *ctio)
{
	struct qla_tgt_srr_ctio *sc;
	struct qla_tgt *tgt = vha->vha_tgt.qla_tgt;
	struct qla_tgt_srr_imm *imm;

	tgt->ctio_srr_id++;
	cmd->cmd_flags |= BIT_15;

	ql_dbg(ql_dbg_tgt_mgt, vha, 0xf019,
	    "qla_target(%d): CTIO with SRR status received\n", vha->vp_idx);

	if (!ctio) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf055,
		    "qla_target(%d): SRR CTIO, but ctio is NULL\n",
		    vha->vp_idx);
		return -EINVAL;
	}

	sc = kzalloc(sizeof(*sc), GFP_ATOMIC);
	if (sc != NULL) {
		sc->cmd = cmd;
		/* IRQ is already OFF */
		spin_lock(&tgt->srr_lock);
		sc->srr_id = tgt->ctio_srr_id;
		list_add_tail(&sc->srr_list_entry,
		    &tgt->srr_ctio_list);
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf01a,
		    "CTIO SRR %p added (id %d)\n", sc, sc->srr_id);
		if (tgt->imm_srr_id == tgt->ctio_srr_id) {
			int found = 0;
			list_for_each_entry(imm, &tgt->srr_imm_list,
			    srr_list_entry) {
				if (imm->srr_id == sc->srr_id) {
					found = 1;
					break;
				}
			}
			if (found) {
				ql_dbg(ql_dbg_tgt_mgt, vha, 0xf01b,
				    "Scheduling srr work\n");
				schedule_work(&tgt->srr_work);
			} else {
				ql_dbg(ql_dbg_tgt_mgt, vha, 0xf056,
				    "qla_target(%d): imm_srr_id "
				    "== ctio_srr_id (%d), but there is no "
				    "corresponding SRR IMM, deleting CTIO "
				    "SRR %p\n", vha->vp_idx,
				    tgt->ctio_srr_id, sc);
				list_del(&sc->srr_list_entry);
				spin_unlock(&tgt->srr_lock);

				kfree(sc);
				return -EINVAL;
			}
		}
		spin_unlock(&tgt->srr_lock);
	} else {
		struct qla_tgt_srr_imm *ti;

		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf057,
		    "qla_target(%d): Unable to allocate SRR CTIO entry\n",
		    vha->vp_idx);
		spin_lock(&tgt->srr_lock);
		list_for_each_entry_safe(imm, ti, &tgt->srr_imm_list,
		    srr_list_entry) {
			if (imm->srr_id == tgt->ctio_srr_id) {
				ql_dbg(ql_dbg_tgt_mgt, vha, 0xf01c,
				    "IMM SRR %p deleted (id %d)\n",
				    imm, imm->srr_id);
				list_del(&imm->srr_list_entry);
				qlt_reject_free_srr_imm(vha, imm, 1);
			}
		}
		spin_unlock(&tgt->srr_lock);

		return -ENOMEM;
	}

	return 0;
}

/*
 * ha->hardware_lock supposed to be held on entry. Might drop it, then reaquire
 */
static int qlt_term_ctio_exchange(struct scsi_qla_host *vha, void *ctio,
	struct qla_tgt_cmd *cmd, uint32_t status)
{
	int term = 0;

	if (ctio != NULL) {
		struct ctio7_from_24xx *c = (struct ctio7_from_24xx *)ctio;
		term = !(c->flags &
		    __constant_cpu_to_le16(OF_TERM_EXCH));
	} else
		term = 1;

	if (term)
		qlt_send_term_exchange(vha, cmd, &cmd->atio, 1);

	return term;
}

/* ha->hardware_lock supposed to be held on entry */
static inline struct qla_tgt_cmd *qlt_get_cmd(struct scsi_qla_host *vha,
	uint32_t handle)
{
	struct qla_hw_data *ha = vha->hw;

	handle--;
	if (ha->tgt.cmds[handle] != NULL) {
		struct qla_tgt_cmd *cmd = ha->tgt.cmds[handle];
		ha->tgt.cmds[handle] = NULL;
		return cmd;
	} else
		return NULL;
}

/* ha->hardware_lock supposed to be held on entry */
static struct qla_tgt_cmd *qlt_ctio_to_cmd(struct scsi_qla_host *vha,
	uint32_t handle, void *ctio)
{
	struct qla_tgt_cmd *cmd = NULL;

	/* Clear out internal marks */
	handle &= ~(CTIO_COMPLETION_HANDLE_MARK |
	    CTIO_INTERMEDIATE_HANDLE_MARK);

	if (handle != QLA_TGT_NULL_HANDLE) {
		if (unlikely(handle == QLA_TGT_SKIP_HANDLE))
			return NULL;

		/* handle-1 is actually used */
		if (unlikely(handle > DEFAULT_OUTSTANDING_COMMANDS)) {
			ql_dbg(ql_dbg_tgt, vha, 0xe052,
			    "qla_target(%d): Wrong handle %x received\n",
			    vha->vp_idx, handle);
			return NULL;
		}
		cmd = qlt_get_cmd(vha, handle);
		if (unlikely(cmd == NULL)) {
			ql_dbg(ql_dbg_tgt, vha, 0xe053,
			    "qla_target(%d): Suspicious: unable to "
			    "find the command with handle %x\n", vha->vp_idx,
			    handle);
			return NULL;
		}
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
static void
qlt_abort_cmd_on_host_reset(struct scsi_qla_host *vha, struct qla_tgt_cmd *cmd)
{
	struct qla_hw_data *ha = vha->hw;
	uint32_t handle;

	if (cmd->sg_mapped)
		qlt_unmap_sg(vha, cmd);

	handle = qlt_make_handle(vha);

	/* TODO: fix debug message type and ids. */
	if (cmd->state == QLA_TGT_STATE_PROCESSED) {
		ql_dbg(ql_dbg_io, vha, 0xff00,
		    "HOST-ABORT: handle=%d, state=PROCESSED.\n", handle);
	} else if (cmd->state == QLA_TGT_STATE_NEED_DATA) {
		cmd->write_data_transferred = 0;
		cmd->state = QLA_TGT_STATE_DATA_IN;

		ql_dbg(ql_dbg_io, vha, 0xff01,
		    "HOST-ABORT: handle=%d, state=DATA_IN.\n", handle);

		ha->tgt.tgt_ops->handle_data(cmd);
		return;
	} else if (cmd->state == QLA_TGT_STATE_ABORTED) {
		ql_dbg(ql_dbg_io, vha, 0xff02,
		    "HOST-ABORT: handle=%d, state=ABORTED.\n", handle);
	} else {
		ql_dbg(ql_dbg_io, vha, 0xff03,
		    "HOST-ABORT: handle=%d, state=BAD(%d).\n", handle,
		    cmd->state);
		dump_stack();
	}

	cmd->cmd_flags |= BIT_12;
	ha->tgt.tgt_ops->free_cmd(cmd);
}

void
qlt_host_reset_handler(struct qla_hw_data *ha)
{
	struct qla_tgt_cmd *cmd;
	unsigned long flags;
	scsi_qla_host_t *base_vha = pci_get_drvdata(ha->pdev);
	scsi_qla_host_t *vha = NULL;
	struct qla_tgt *tgt = base_vha->vha_tgt.qla_tgt;
	uint32_t i;

	if (!base_vha->hw->tgt.tgt_ops)
		return;

	if (!tgt || qla_ini_mode_enabled(base_vha)) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf003,
			"Target mode disabled\n");
		return;
	}

	ql_dbg(ql_dbg_tgt_mgt, vha, 0xff10,
	    "HOST-ABORT-HNDLR: base_vha->dpc_flags=%lx.\n",
	    base_vha->dpc_flags);

	spin_lock_irqsave(&ha->hardware_lock, flags);
	for (i = 1; i < DEFAULT_OUTSTANDING_COMMANDS + 1; i++) {
		cmd = qlt_get_cmd(base_vha, i);
		if (!cmd)
			continue;
		/* ha->tgt.cmds entry is cleared by qlt_get_cmd. */
		vha = cmd->vha;
		qlt_abort_cmd_on_host_reset(vha, cmd);
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}


/*
 * ha->hardware_lock supposed to be held on entry. Might drop it, then reaquire
 */
static void qlt_do_ctio_completion(struct scsi_qla_host *vha, uint32_t handle,
	uint32_t status, void *ctio)
{
	struct qla_hw_data *ha = vha->hw;
	struct se_cmd *se_cmd;
	const struct target_core_fabric_ops *tfo;
	struct qla_tgt_cmd *cmd;

	if (handle & CTIO_INTERMEDIATE_HANDLE_MARK) {
		/* That could happen only in case of an error/reset/abort */
		if (status != CTIO_SUCCESS) {
			ql_dbg(ql_dbg_tgt_mgt, vha, 0xf01d,
			    "Intermediate CTIO received"
			    " (status %x)\n", status);
		}
		return;
	}

	cmd = qlt_ctio_to_cmd(vha, handle, ctio);
	if (cmd == NULL)
		return;

	se_cmd = &cmd->se_cmd;
	tfo = se_cmd->se_tfo;
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
			ql_dbg(ql_dbg_tgt_mgt, vha, 0xf059,
			    "qla_target(%d): CTIO with PORT LOGGED "
			    "OUT (29) or PORT UNAVAILABLE (28) status %x "
			    "received (state %x, se_cmd %p)\n", vha->vp_idx,
			    status, cmd->state, se_cmd);
			break;

		case CTIO_SRR_RECEIVED:
			ql_dbg(ql_dbg_tgt_mgt, vha, 0xf05a,
			    "qla_target(%d): CTIO with SRR_RECEIVED"
			    " status %x received (state %x, se_cmd %p)\n",
			    vha->vp_idx, status, cmd->state, se_cmd);
			if (qlt_prepare_srr_ctio(vha, cmd, ctio) != 0)
				break;
			else
				return;

		case CTIO_DIF_ERROR: {
			struct ctio_crc_from_fw *crc =
				(struct ctio_crc_from_fw *)ctio;
			ql_dbg(ql_dbg_tgt_mgt, vha, 0xf073,
			    "qla_target(%d): CTIO with DIF_ERROR status %x received (state %x, se_cmd %p) actual_dif[0x%llx] expect_dif[0x%llx]\n",
			    vha->vp_idx, status, cmd->state, se_cmd,
			    *((u64 *)&crc->actual_dif[0]),
			    *((u64 *)&crc->expected_dif[0]));

			if (qlt_handle_dif_error(vha, cmd, ctio)) {
				if (cmd->state == QLA_TGT_STATE_NEED_DATA) {
					/* scsi Write/xfer rdy complete */
					goto skip_term;
				} else {
					/* scsi read/xmit respond complete
					 * call handle dif to send scsi status
					 * rather than terminate exchange.
					 */
					cmd->state = QLA_TGT_STATE_PROCESSED;
					ha->tgt.tgt_ops->handle_dif_err(cmd);
					return;
				}
			} else {
				/* Need to generate a SCSI good completion.
				 * because FW did not send scsi status.
				 */
				status = 0;
				goto skip_term;
			}
			break;
		}
		default:
			ql_dbg(ql_dbg_tgt_mgt, vha, 0xf05b,
			    "qla_target(%d): CTIO with error status 0x%x received (state %x, se_cmd %p\n",
			    vha->vp_idx, status, cmd->state, se_cmd);
			break;
		}


		/* "cmd->state == QLA_TGT_STATE_ABORTED" means
		 * cmd is already aborted/terminated, we don't
		 * need to terminate again.  The exchange is already
		 * cleaned up/freed at FW level.  Just cleanup at driver
		 * level.
		 */
		if ((cmd->state != QLA_TGT_STATE_NEED_DATA) &&
		    (cmd->state != QLA_TGT_STATE_ABORTED)) {
			cmd->cmd_flags |= BIT_13;
			if (qlt_term_ctio_exchange(vha, ctio, cmd, status))
				return;
		}
	}
skip_term:

	if (cmd->state == QLA_TGT_STATE_PROCESSED) {
		;
	} else if (cmd->state == QLA_TGT_STATE_NEED_DATA) {
		int rx_status = 0;

		cmd->state = QLA_TGT_STATE_DATA_IN;

		if (unlikely(status != CTIO_SUCCESS))
			rx_status = -EIO;
		else
			cmd->write_data_transferred = 1;

		ha->tgt.tgt_ops->handle_data(cmd);
		return;
	} else if (cmd->state == QLA_TGT_STATE_ABORTED) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf01e,
		  "Aborted command %p (tag %lld) finished\n", cmd, se_cmd->tag);
	} else {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf05c,
		    "qla_target(%d): A command in state (%d) should "
		    "not return a CTIO complete\n", vha->vp_idx, cmd->state);
	}

	if (unlikely(status != CTIO_SUCCESS) &&
		(cmd->state != QLA_TGT_STATE_ABORTED)) {
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

static struct qla_tgt_sess *qlt_make_local_sess(struct scsi_qla_host *,
					uint8_t *);
/*
 * Process context for I/O path into tcm_qla2xxx code
 */
static void __qlt_do_work(struct qla_tgt_cmd *cmd)
{
	scsi_qla_host_t *vha = cmd->vha;
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt *tgt = vha->vha_tgt.qla_tgt;
	struct qla_tgt_sess *sess = cmd->sess;
	struct atio_from_isp *atio = &cmd->atio;
	unsigned char *cdb;
	unsigned long flags;
	uint32_t data_length;
	int ret, fcp_task_attr, data_dir, bidi = 0;

	cmd->cmd_in_wq = 0;
	cmd->cmd_flags |= BIT_1;
	if (tgt->tgt_stop)
		goto out_term;

	cdb = &atio->u.isp24.fcp_cmnd.cdb[0];
	cmd->se_cmd.tag = atio->u.isp24.exchange_addr;
	cmd->unpacked_lun = scsilun_to_int(
	    (struct scsi_lun *)&atio->u.isp24.fcp_cmnd.lun);

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
	spin_lock_irqsave(&ha->hardware_lock, flags);
	ha->tgt.tgt_ops->put_sess(sess);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	return;

out_term:
	ql_dbg(ql_dbg_io, vha, 0x3060, "Terminating work cmd %p", cmd);
	/*
	 * cmd has not sent to target yet, so pass NULL as the second
	 * argument to qlt_send_term_exchange() and free the memory here.
	 */
	cmd->cmd_flags |= BIT_2;
	spin_lock_irqsave(&ha->hardware_lock, flags);
	qlt_send_term_exchange(vha, NULL, &cmd->atio, 1);

	qlt_decr_num_pend_cmds(vha);
	percpu_ida_free(&sess->se_sess->sess_tag_pool, cmd->se_cmd.map_tag);
	ha->tgt.tgt_ops->put_sess(sess);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

static void qlt_do_work(struct work_struct *work)
{
	struct qla_tgt_cmd *cmd = container_of(work, struct qla_tgt_cmd, work);

	__qlt_do_work(cmd);
}

static struct qla_tgt_cmd *qlt_get_tag(scsi_qla_host_t *vha,
				       struct qla_tgt_sess *sess,
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

	memcpy(&cmd->atio, atio, sizeof(*atio));
	cmd->state = QLA_TGT_STATE_NEW;
	cmd->tgt = vha->vha_tgt.qla_tgt;
	qlt_incr_num_pend_cmds(vha);
	cmd->vha = vha;
	cmd->se_cmd.map_tag = tag;
	cmd->sess = sess;
	cmd->loop_id = sess->loop_id;
	cmd->conf_compl_supported = sess->conf_compl_supported;

	return cmd;
}

static void qlt_send_busy(struct scsi_qla_host *, struct atio_from_isp *,
			  uint16_t);

static void qlt_create_sess_from_atio(struct work_struct *work)
{
	struct qla_tgt_sess_op *op = container_of(work,
					struct qla_tgt_sess_op, work);
	scsi_qla_host_t *vha = op->vha;
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt_sess *sess;
	struct qla_tgt_cmd *cmd;
	unsigned long flags;
	uint8_t *s_id = op->atio.u.isp24.fcp_hdr.s_id;

	ql_dbg(ql_dbg_tgt_mgt, vha, 0xf022,
		"qla_target(%d): Unable to find wwn login"
		" (s_id %x:%x:%x), trying to create it manually\n",
		vha->vp_idx, s_id[0], s_id[1], s_id[2]);

	if (op->atio.u.raw.entry_count > 1) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf023,
		        "Dropping multy entry atio %p\n", &op->atio);
		goto out_term;
	}

	mutex_lock(&vha->vha_tgt.tgt_mutex);
	sess = qlt_make_local_sess(vha, s_id);
	/* sess has an extra creation ref. */
	mutex_unlock(&vha->vha_tgt.tgt_mutex);

	if (!sess)
		goto out_term;
	/*
	 * Now obtain a pre-allocated session tag using the original op->atio
	 * packet header, and dispatch into __qlt_do_work() using the existing
	 * process context.
	 */
	cmd = qlt_get_tag(vha, sess, &op->atio);
	if (!cmd) {
		spin_lock_irqsave(&ha->hardware_lock, flags);
		qlt_send_busy(vha, &op->atio, SAM_STAT_BUSY);
		ha->tgt.tgt_ops->put_sess(sess);
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
		kfree(op);
		return;
	}
	/*
	 * __qlt_do_work() will call ha->tgt.tgt_ops->put_sess() to release
	 * the extra reference taken above by qlt_make_local_sess()
	 */
	__qlt_do_work(cmd);
	kfree(op);
	return;

out_term:
	spin_lock_irqsave(&ha->hardware_lock, flags);
	qlt_send_term_exchange(vha, NULL, &op->atio, 1);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	kfree(op);

}

/* ha->hardware_lock supposed to be held on entry */
static int qlt_handle_cmd_for_atio(struct scsi_qla_host *vha,
	struct atio_from_isp *atio)
{
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt *tgt = vha->vha_tgt.qla_tgt;
	struct qla_tgt_sess *sess;
	struct qla_tgt_cmd *cmd;

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
		INIT_WORK(&op->work, qlt_create_sess_from_atio);
		queue_work(qla_tgt_wq, &op->work);
		return 0;
	}
	/*
	 * Do kref_get() before returning + dropping qla_hw_data->hardware_lock.
	 */
	kref_get(&sess->se_sess->sess_kref);

	cmd = qlt_get_tag(vha, sess, atio);
	if (!cmd) {
		ql_dbg(ql_dbg_io, vha, 0x3062,
		    "qla_target(%d): Allocation of cmd failed\n", vha->vp_idx);
		ha->tgt.tgt_ops->put_sess(sess);
		return -ENOMEM;
	}

	cmd->cmd_flags = 0;
	cmd->jiffies_at_alloc = get_jiffies_64();

	cmd->reset_count = vha->hw->chip_reset;

	cmd->cmd_in_wq = 1;
	cmd->cmd_flags |= BIT_0;
	INIT_WORK(&cmd->work, qlt_do_work);
	queue_work(qla_tgt_wq, &cmd->work);
	return 0;

}

/* ha->hardware_lock supposed to be held on entry */
static int qlt_issue_task_mgmt(struct qla_tgt_sess *sess, uint32_t lun,
	int fn, void *iocb, int flags)
{
	struct scsi_qla_host *vha = sess->vha;
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt_mgmt_cmd *mcmd;
	int res;
	uint8_t tmr_func;

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
	mcmd->reset_count = vha->hw->chip_reset;

	switch (fn) {
	case QLA_TGT_CLEAR_ACA:
		ql_dbg(ql_dbg_tgt_tmr, vha, 0x10000,
		    "qla_target(%d): CLEAR_ACA received\n", sess->vha->vp_idx);
		tmr_func = TMR_CLEAR_ACA;
		break;

	case QLA_TGT_TARGET_RESET:
		ql_dbg(ql_dbg_tgt_tmr, vha, 0x10001,
		    "qla_target(%d): TARGET_RESET received\n",
		    sess->vha->vp_idx);
		tmr_func = TMR_TARGET_WARM_RESET;
		break;

	case QLA_TGT_LUN_RESET:
		ql_dbg(ql_dbg_tgt_tmr, vha, 0x10002,
		    "qla_target(%d): LUN_RESET received\n", sess->vha->vp_idx);
		tmr_func = TMR_LUN_RESET;
		break;

	case QLA_TGT_CLEAR_TS:
		ql_dbg(ql_dbg_tgt_tmr, vha, 0x10003,
		    "qla_target(%d): CLEAR_TS received\n", sess->vha->vp_idx);
		tmr_func = TMR_CLEAR_TASK_SET;
		break;

	case QLA_TGT_ABORT_TS:
		ql_dbg(ql_dbg_tgt_tmr, vha, 0x10004,
		    "qla_target(%d): ABORT_TS received\n", sess->vha->vp_idx);
		tmr_func = TMR_ABORT_TASK_SET;
		break;
#if 0
	case QLA_TGT_ABORT_ALL:
		ql_dbg(ql_dbg_tgt_tmr, vha, 0x10005,
		    "qla_target(%d): Doing ABORT_ALL_TASKS\n",
		    sess->vha->vp_idx);
		tmr_func = 0;
		break;

	case QLA_TGT_ABORT_ALL_SESS:
		ql_dbg(ql_dbg_tgt_tmr, vha, 0x10006,
		    "qla_target(%d): Doing ABORT_ALL_TASKS_SESS\n",
		    sess->vha->vp_idx);
		tmr_func = 0;
		break;

	case QLA_TGT_NEXUS_LOSS_SESS:
		ql_dbg(ql_dbg_tgt_tmr, vha, 0x10007,
		    "qla_target(%d): Doing NEXUS_LOSS_SESS\n",
		    sess->vha->vp_idx);
		tmr_func = 0;
		break;

	case QLA_TGT_NEXUS_LOSS:
		ql_dbg(ql_dbg_tgt_tmr, vha, 0x10008,
		    "qla_target(%d): Doing NEXUS_LOSS\n", sess->vha->vp_idx);
		tmr_func = 0;
		break;
#endif
	default:
		ql_dbg(ql_dbg_tgt_tmr, vha, 0x1000a,
		    "qla_target(%d): Unknown task mgmt fn 0x%x\n",
		    sess->vha->vp_idx, fn);
		mempool_free(mcmd, qla_tgt_mgmt_cmd_mempool);
		return -ENOSYS;
	}

	res = ha->tgt.tgt_ops->handle_tmr(mcmd, lun, tmr_func, 0);
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
	struct qla_tgt_sess *sess;
	uint32_t lun, unpacked_lun;
	int lun_size, fn;

	tgt = vha->vha_tgt.qla_tgt;

	lun = a->u.isp24.fcp_cmnd.lun;
	lun_size = sizeof(a->u.isp24.fcp_cmnd.lun);
	fn = a->u.isp24.fcp_cmnd.task_mgmt_flags;
	sess = ha->tgt.tgt_ops->find_sess_by_s_id(vha,
	    a->u.isp24.fcp_hdr.s_id);
	unpacked_lun = scsilun_to_int((struct scsi_lun *)&lun);

	if (!sess) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf024,
		    "qla_target(%d): task mgmt fn 0x%x for "
		    "non-existant session\n", vha->vp_idx, fn);
		return qlt_sched_sess_work(tgt, QLA_TGT_SESS_WORK_TM, iocb,
		    sizeof(struct atio_from_isp));
	}

	return qlt_issue_task_mgmt(sess, unpacked_lun, fn, iocb, 0);
}

/* ha->hardware_lock supposed to be held on entry */
static int __qlt_abort_task(struct scsi_qla_host *vha,
	struct imm_ntfy_from_isp *iocb, struct qla_tgt_sess *sess)
{
	struct atio_from_isp *a = (struct atio_from_isp *)iocb;
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt_mgmt_cmd *mcmd;
	uint32_t lun, unpacked_lun;
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

	lun = a->u.isp24.fcp_cmnd.lun;
	unpacked_lun = scsilun_to_int((struct scsi_lun *)&lun);
	mcmd->reset_count = vha->hw->chip_reset;

	rc = ha->tgt.tgt_ops->handle_tmr(mcmd, unpacked_lun, TMR_ABORT_TASK,
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
	struct qla_tgt_sess *sess;
	int loop_id;

	loop_id = GET_TARGET_ID(ha, (struct atio_from_isp *)iocb);

	sess = ha->tgt.tgt_ops->find_sess_by_loop_id(vha, loop_id);
	if (sess == NULL) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf025,
		    "qla_target(%d): task abort for unexisting "
		    "session\n", vha->vp_idx);
		return qlt_sched_sess_work(vha->vha_tgt.qla_tgt,
		    QLA_TGT_SESS_WORK_ABORT, iocb, sizeof(*iocb));
	}

	return __qlt_abort_task(vha, iocb, sess);
}

/*
 * ha->hardware_lock supposed to be held on entry. Might drop it, then reaquire
 */
static int qlt_24xx_handle_els(struct scsi_qla_host *vha,
	struct imm_ntfy_from_isp *iocb)
{
	int res = 0;

	ql_dbg(ql_dbg_tgt_mgt, vha, 0xf026,
	    "qla_target(%d): Port ID: 0x%3phC ELS opcode: 0x%02x\n",
	    vha->vp_idx, iocb->u.isp24.port_id, iocb->u.isp24.status_subcode);

	switch (iocb->u.isp24.status_subcode) {
	case ELS_PLOGI:
	case ELS_FLOGI:
	case ELS_PRLI:
	case ELS_LOGO:
	case ELS_PRLO:
		res = qlt_reset(vha, iocb, QLA_TGT_NEXUS_LOSS_SESS);
		break;
	case ELS_PDISC:
	case ELS_ADISC:
	{
		struct qla_tgt *tgt = vha->vha_tgt.qla_tgt;
		if (tgt->link_reinit_iocb_pending) {
			qlt_send_notify_ack(vha, &tgt->link_reinit_iocb,
			    0, 0, 0, 0, 0, 0);
			tgt->link_reinit_iocb_pending = 0;
		}
		res = 1; /* send notify ack */
		break;
	}

	default:
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf061,
		    "qla_target(%d): Unsupported ELS command %x "
		    "received\n", vha->vp_idx, iocb->u.isp24.status_subcode);
		res = qlt_reset(vha, iocb, QLA_TGT_NEXUS_LOSS_SESS);
		break;
	}

	return res;
}

static int qlt_set_data_offset(struct qla_tgt_cmd *cmd, uint32_t offset)
{
	struct scatterlist *sg, *sgp, *sg_srr, *sg_srr_start = NULL;
	size_t first_offset = 0, rem_offset = offset, tmp = 0;
	int i, sg_srr_cnt, bufflen = 0;

	ql_dbg(ql_dbg_tgt, cmd->vha, 0xe023,
	    "Entering qla_tgt_set_data_offset: cmd: %p, cmd->sg: %p, "
	    "cmd->sg_cnt: %u, direction: %d\n",
	    cmd, cmd->sg, cmd->sg_cnt, cmd->dma_data_direction);

	/*
	 * FIXME: Reject non zero SRR relative offset until we can test
	 * this code properly.
	 */
	pr_debug("Rejecting non zero SRR rel_offs: %u\n", offset);
	return -1;

	if (!cmd->sg || !cmd->sg_cnt) {
		ql_dbg(ql_dbg_tgt, cmd->vha, 0xe055,
		    "Missing cmd->sg or zero cmd->sg_cnt in"
		    " qla_tgt_set_data_offset\n");
		return -EINVAL;
	}
	/*
	 * Walk the current cmd->sg list until we locate the new sg_srr_start
	 */
	for_each_sg(cmd->sg, sg, cmd->sg_cnt, i) {
		ql_dbg(ql_dbg_tgt, cmd->vha, 0xe024,
		    "sg[%d]: %p page: %p, length: %d, offset: %d\n",
		    i, sg, sg_page(sg), sg->length, sg->offset);

		if ((sg->length + tmp) > offset) {
			first_offset = rem_offset;
			sg_srr_start = sg;
			ql_dbg(ql_dbg_tgt, cmd->vha, 0xe025,
			    "Found matching sg[%d], using %p as sg_srr_start, "
			    "and using first_offset: %zu\n", i, sg,
			    first_offset);
			break;
		}
		tmp += sg->length;
		rem_offset -= sg->length;
	}

	if (!sg_srr_start) {
		ql_dbg(ql_dbg_tgt, cmd->vha, 0xe056,
		    "Unable to locate sg_srr_start for offset: %u\n", offset);
		return -EINVAL;
	}
	sg_srr_cnt = (cmd->sg_cnt - i);

	sg_srr = kzalloc(sizeof(struct scatterlist) * sg_srr_cnt, GFP_KERNEL);
	if (!sg_srr) {
		ql_dbg(ql_dbg_tgt, cmd->vha, 0xe057,
		    "Unable to allocate sgp\n");
		return -ENOMEM;
	}
	sg_init_table(sg_srr, sg_srr_cnt);
	sgp = &sg_srr[0];
	/*
	 * Walk the remaining list for sg_srr_start, mapping to the newly
	 * allocated sg_srr taking first_offset into account.
	 */
	for_each_sg(sg_srr_start, sg, sg_srr_cnt, i) {
		if (first_offset) {
			sg_set_page(sgp, sg_page(sg),
			    (sg->length - first_offset), first_offset);
			first_offset = 0;
		} else {
			sg_set_page(sgp, sg_page(sg), sg->length, 0);
		}
		bufflen += sgp->length;

		sgp = sg_next(sgp);
		if (!sgp)
			break;
	}

	cmd->sg = sg_srr;
	cmd->sg_cnt = sg_srr_cnt;
	cmd->bufflen = bufflen;
	cmd->offset += offset;
	cmd->free_sg = 1;

	ql_dbg(ql_dbg_tgt, cmd->vha, 0xe026, "New cmd->sg: %p\n", cmd->sg);
	ql_dbg(ql_dbg_tgt, cmd->vha, 0xe027, "New cmd->sg_cnt: %u\n",
	    cmd->sg_cnt);
	ql_dbg(ql_dbg_tgt, cmd->vha, 0xe028, "New cmd->bufflen: %u\n",
	    cmd->bufflen);
	ql_dbg(ql_dbg_tgt, cmd->vha, 0xe029, "New cmd->offset: %u\n",
	    cmd->offset);

	if (cmd->sg_cnt < 0)
		BUG();

	if (cmd->bufflen < 0)
		BUG();

	return 0;
}

static inline int qlt_srr_adjust_data(struct qla_tgt_cmd *cmd,
	uint32_t srr_rel_offs, int *xmit_type)
{
	int res = 0, rel_offs;

	rel_offs = srr_rel_offs - cmd->offset;
	ql_dbg(ql_dbg_tgt_mgt, cmd->vha, 0xf027, "srr_rel_offs=%d, rel_offs=%d",
	    srr_rel_offs, rel_offs);

	*xmit_type = QLA_TGT_XMIT_ALL;

	if (rel_offs < 0) {
		ql_dbg(ql_dbg_tgt_mgt, cmd->vha, 0xf062,
		    "qla_target(%d): SRR rel_offs (%d) < 0",
		    cmd->vha->vp_idx, rel_offs);
		res = -1;
	} else if (rel_offs == cmd->bufflen)
		*xmit_type = QLA_TGT_XMIT_STATUS;
	else if (rel_offs > 0)
		res = qlt_set_data_offset(cmd, rel_offs);

	return res;
}

/* No locks, thread context */
static void qlt_handle_srr(struct scsi_qla_host *vha,
	struct qla_tgt_srr_ctio *sctio, struct qla_tgt_srr_imm *imm)
{
	struct imm_ntfy_from_isp *ntfy =
	    (struct imm_ntfy_from_isp *)&imm->imm_ntfy;
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt_cmd *cmd = sctio->cmd;
	struct se_cmd *se_cmd = &cmd->se_cmd;
	unsigned long flags;
	int xmit_type = 0, resp = 0;
	uint32_t offset;
	uint16_t srr_ui;

	offset = le32_to_cpu(ntfy->u.isp24.srr_rel_offs);
	srr_ui = ntfy->u.isp24.srr_ui;

	ql_dbg(ql_dbg_tgt_mgt, vha, 0xf028, "SRR cmd %p, srr_ui %x\n",
	    cmd, srr_ui);

	switch (srr_ui) {
	case SRR_IU_STATUS:
		spin_lock_irqsave(&ha->hardware_lock, flags);
		qlt_send_notify_ack(vha, ntfy,
		    0, 0, 0, NOTIFY_ACK_SRR_FLAGS_ACCEPT, 0, 0);
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
		xmit_type = QLA_TGT_XMIT_STATUS;
		resp = 1;
		break;
	case SRR_IU_DATA_IN:
		if (!cmd->sg || !cmd->sg_cnt) {
			ql_dbg(ql_dbg_tgt_mgt, vha, 0xf063,
			    "Unable to process SRR_IU_DATA_IN due to"
			    " missing cmd->sg, state: %d\n", cmd->state);
			dump_stack();
			goto out_reject;
		}
		if (se_cmd->scsi_status != 0) {
			ql_dbg(ql_dbg_tgt, vha, 0xe02a,
			    "Rejecting SRR_IU_DATA_IN with non GOOD "
			    "scsi_status\n");
			goto out_reject;
		}
		cmd->bufflen = se_cmd->data_length;

		if (qlt_has_data(cmd)) {
			if (qlt_srr_adjust_data(cmd, offset, &xmit_type) != 0)
				goto out_reject;
			spin_lock_irqsave(&ha->hardware_lock, flags);
			qlt_send_notify_ack(vha, ntfy,
			    0, 0, 0, NOTIFY_ACK_SRR_FLAGS_ACCEPT, 0, 0);
			spin_unlock_irqrestore(&ha->hardware_lock, flags);
			resp = 1;
		} else {
			ql_dbg(ql_dbg_tgt_mgt, vha, 0xf064,
			       "qla_target(%d): SRR for in data for cmd without them (tag %lld, SCSI status %d), reject",
			       vha->vp_idx, se_cmd->tag,
			    cmd->se_cmd.scsi_status);
			goto out_reject;
		}
		break;
	case SRR_IU_DATA_OUT:
		if (!cmd->sg || !cmd->sg_cnt) {
			ql_dbg(ql_dbg_tgt_mgt, vha, 0xf065,
			    "Unable to process SRR_IU_DATA_OUT due to"
			    " missing cmd->sg\n");
			dump_stack();
			goto out_reject;
		}
		if (se_cmd->scsi_status != 0) {
			ql_dbg(ql_dbg_tgt, vha, 0xe02b,
			    "Rejecting SRR_IU_DATA_OUT"
			    " with non GOOD scsi_status\n");
			goto out_reject;
		}
		cmd->bufflen = se_cmd->data_length;

		if (qlt_has_data(cmd)) {
			if (qlt_srr_adjust_data(cmd, offset, &xmit_type) != 0)
				goto out_reject;
			spin_lock_irqsave(&ha->hardware_lock, flags);
			qlt_send_notify_ack(vha, ntfy,
			    0, 0, 0, NOTIFY_ACK_SRR_FLAGS_ACCEPT, 0, 0);
			spin_unlock_irqrestore(&ha->hardware_lock, flags);
			if (xmit_type & QLA_TGT_XMIT_DATA) {
				cmd->cmd_flags |= BIT_8;
				qlt_rdy_to_xfer(cmd);
			}
		} else {
			ql_dbg(ql_dbg_tgt_mgt, vha, 0xf066,
			    "qla_target(%d): SRR for out data for cmd without them (tag %lld, SCSI status %d), reject",
			       vha->vp_idx, se_cmd->tag, cmd->se_cmd.scsi_status);
			goto out_reject;
		}
		break;
	default:
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf067,
		    "qla_target(%d): Unknown srr_ui value %x",
		    vha->vp_idx, srr_ui);
		goto out_reject;
	}

	/* Transmit response in case of status and data-in cases */
	if (resp) {
		cmd->cmd_flags |= BIT_7;
		qlt_xmit_response(cmd, xmit_type, se_cmd->scsi_status);
	}

	return;

out_reject:
	spin_lock_irqsave(&ha->hardware_lock, flags);
	qlt_send_notify_ack(vha, ntfy, 0, 0, 0,
	    NOTIFY_ACK_SRR_FLAGS_REJECT,
	    NOTIFY_ACK_SRR_REJECT_REASON_UNABLE_TO_PERFORM,
	    NOTIFY_ACK_SRR_FLAGS_REJECT_EXPL_NO_EXPL);
	if (cmd->state == QLA_TGT_STATE_NEED_DATA) {
		cmd->state = QLA_TGT_STATE_DATA_IN;
		dump_stack();
	} else {
		cmd->cmd_flags |= BIT_9;
		qlt_send_term_exchange(vha, cmd, &cmd->atio, 1);
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

static void qlt_reject_free_srr_imm(struct scsi_qla_host *vha,
	struct qla_tgt_srr_imm *imm, int ha_locked)
{
	struct qla_hw_data *ha = vha->hw;
	unsigned long flags = 0;

	if (!ha_locked)
		spin_lock_irqsave(&ha->hardware_lock, flags);

	qlt_send_notify_ack(vha, (void *)&imm->imm_ntfy, 0, 0, 0,
	    NOTIFY_ACK_SRR_FLAGS_REJECT,
	    NOTIFY_ACK_SRR_REJECT_REASON_UNABLE_TO_PERFORM,
	    NOTIFY_ACK_SRR_FLAGS_REJECT_EXPL_NO_EXPL);

	if (!ha_locked)
		spin_unlock_irqrestore(&ha->hardware_lock, flags);

	kfree(imm);
}

static void qlt_handle_srr_work(struct work_struct *work)
{
	struct qla_tgt *tgt = container_of(work, struct qla_tgt, srr_work);
	struct scsi_qla_host *vha = tgt->vha;
	struct qla_tgt_srr_ctio *sctio;
	unsigned long flags;

	ql_dbg(ql_dbg_tgt_mgt, vha, 0xf029, "Entering SRR work (tgt %p)\n",
	    tgt);

restart:
	spin_lock_irqsave(&tgt->srr_lock, flags);
	list_for_each_entry(sctio, &tgt->srr_ctio_list, srr_list_entry) {
		struct qla_tgt_srr_imm *imm, *i, *ti;
		struct qla_tgt_cmd *cmd;
		struct se_cmd *se_cmd;

		imm = NULL;
		list_for_each_entry_safe(i, ti, &tgt->srr_imm_list,
						srr_list_entry) {
			if (i->srr_id == sctio->srr_id) {
				list_del(&i->srr_list_entry);
				if (imm) {
					ql_dbg(ql_dbg_tgt_mgt, vha, 0xf068,
					  "qla_target(%d): There must be "
					  "only one IMM SRR per CTIO SRR "
					  "(IMM SRR %p, id %d, CTIO %p\n",
					  vha->vp_idx, i, i->srr_id, sctio);
					qlt_reject_free_srr_imm(tgt->vha, i, 0);
				} else
					imm = i;
			}
		}

		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf02a,
		    "IMM SRR %p, CTIO SRR %p (id %d)\n", imm, sctio,
		    sctio->srr_id);

		if (imm == NULL) {
			ql_dbg(ql_dbg_tgt_mgt, vha, 0xf02b,
			    "Not found matching IMM for SRR CTIO (id %d)\n",
			    sctio->srr_id);
			continue;
		} else
			list_del(&sctio->srr_list_entry);

		spin_unlock_irqrestore(&tgt->srr_lock, flags);

		cmd = sctio->cmd;
		/*
		 * Reset qla_tgt_cmd SRR values and SGL pointer+count to follow
		 * tcm_qla2xxx_write_pending() and tcm_qla2xxx_queue_data_in()
		 * logic..
		 */
		cmd->offset = 0;
		if (cmd->free_sg) {
			kfree(cmd->sg);
			cmd->sg = NULL;
			cmd->free_sg = 0;
		}
		se_cmd = &cmd->se_cmd;

		cmd->sg_cnt = se_cmd->t_data_nents;
		cmd->sg = se_cmd->t_data_sg;

		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf02c,
		       "SRR cmd %p (se_cmd %p, tag %lld, op %x), sg_cnt=%d, offset=%d",
		       cmd, &cmd->se_cmd, se_cmd->tag, se_cmd->t_task_cdb ?
		       se_cmd->t_task_cdb[0] : 0, cmd->sg_cnt, cmd->offset);

		qlt_handle_srr(vha, sctio, imm);

		kfree(imm);
		kfree(sctio);
		goto restart;
	}
	spin_unlock_irqrestore(&tgt->srr_lock, flags);
}

/* ha->hardware_lock supposed to be held on entry */
static void qlt_prepare_srr_imm(struct scsi_qla_host *vha,
	struct imm_ntfy_from_isp *iocb)
{
	struct qla_tgt_srr_imm *imm;
	struct qla_tgt *tgt = vha->vha_tgt.qla_tgt;
	struct qla_tgt_srr_ctio *sctio;

	tgt->imm_srr_id++;

	ql_log(ql_log_warn, vha, 0xf02d, "qla_target(%d): SRR received\n",
	    vha->vp_idx);

	imm = kzalloc(sizeof(*imm), GFP_ATOMIC);
	if (imm != NULL) {
		memcpy(&imm->imm_ntfy, iocb, sizeof(imm->imm_ntfy));

		/* IRQ is already OFF */
		spin_lock(&tgt->srr_lock);
		imm->srr_id = tgt->imm_srr_id;
		list_add_tail(&imm->srr_list_entry,
		    &tgt->srr_imm_list);
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf02e,
		    "IMM NTFY SRR %p added (id %d, ui %x)\n",
		    imm, imm->srr_id, iocb->u.isp24.srr_ui);
		if (tgt->imm_srr_id == tgt->ctio_srr_id) {
			int found = 0;
			list_for_each_entry(sctio, &tgt->srr_ctio_list,
			    srr_list_entry) {
				if (sctio->srr_id == imm->srr_id) {
					found = 1;
					break;
				}
			}
			if (found) {
				ql_dbg(ql_dbg_tgt_mgt, vha, 0xf02f, "%s",
				    "Scheduling srr work\n");
				schedule_work(&tgt->srr_work);
			} else {
				ql_dbg(ql_dbg_tgt_mgt, vha, 0xf030,
				    "qla_target(%d): imm_srr_id "
				    "== ctio_srr_id (%d), but there is no "
				    "corresponding SRR CTIO, deleting IMM "
				    "SRR %p\n", vha->vp_idx, tgt->ctio_srr_id,
				    imm);
				list_del(&imm->srr_list_entry);

				kfree(imm);

				spin_unlock(&tgt->srr_lock);
				goto out_reject;
			}
		}
		spin_unlock(&tgt->srr_lock);
	} else {
		struct qla_tgt_srr_ctio *ts;

		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf069,
		    "qla_target(%d): Unable to allocate SRR IMM "
		    "entry, SRR request will be rejected\n", vha->vp_idx);

		/* IRQ is already OFF */
		spin_lock(&tgt->srr_lock);
		list_for_each_entry_safe(sctio, ts, &tgt->srr_ctio_list,
		    srr_list_entry) {
			if (sctio->srr_id == tgt->imm_srr_id) {
				ql_dbg(ql_dbg_tgt_mgt, vha, 0xf031,
				    "CTIO SRR %p deleted (id %d)\n",
				    sctio, sctio->srr_id);
				list_del(&sctio->srr_list_entry);
				qlt_send_term_exchange(vha, sctio->cmd,
				    &sctio->cmd->atio, 1);
				kfree(sctio);
			}
		}
		spin_unlock(&tgt->srr_lock);
		goto out_reject;
	}

	return;

out_reject:
	qlt_send_notify_ack(vha, iocb, 0, 0, 0,
	    NOTIFY_ACK_SRR_FLAGS_REJECT,
	    NOTIFY_ACK_SRR_REJECT_REASON_UNABLE_TO_PERFORM,
	    NOTIFY_ACK_SRR_FLAGS_REJECT_EXPL_NO_EXPL);
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
			qlt_send_notify_ack(vha, &tgt->link_reinit_iocb,
			    0, 0, 0, 0, 0, 0);
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

	case IMM_NTFY_SRR:
		qlt_prepare_srr_imm(vha, iocb);
		send_notify_ack = 0;
		break;

	default:
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf06d,
		    "qla_target(%d): Received unknown immediate "
		    "notify status %x\n", vha->vp_idx, status);
		break;
	}

	if (send_notify_ack)
		qlt_send_notify_ack(vha, iocb, add_flags, 0, 0, 0, 0, 0);
}

/*
 * ha->hardware_lock supposed to be held on entry. Might drop it, then reaquire
 * This function sends busy to ISP 2xxx or 24xx.
 */
static int __qlt_send_busy(struct scsi_qla_host *vha,
	struct atio_from_isp *atio, uint16_t status)
{
	struct ctio7_to_24xx *ctio24;
	struct qla_hw_data *ha = vha->hw;
	request_t *pkt;
	struct qla_tgt_sess *sess = NULL;

	sess = ha->tgt.tgt_ops->find_sess_by_s_id(vha,
	    atio->u.isp24.fcp_hdr.s_id);
	if (!sess) {
		qlt_send_term_exchange(vha, NULL, atio, 1);
		return 0;
	}
	/* Sending marker isn't necessary, since we called from ISR */

	pkt = (request_t *)qla2x00_alloc_iocbs(vha, NULL);
	if (!pkt) {
		ql_dbg(ql_dbg_io, vha, 0x3063,
		    "qla_target(%d): %s failed: unable to allocate "
		    "request packet", vha->vp_idx, __func__);
		return -ENOMEM;
	}

	pkt->entry_count = 1;
	pkt->handle = QLA_TGT_SKIP_HANDLE | CTIO_COMPLETION_HANDLE_MARK;

	ctio24 = (struct ctio7_to_24xx *)pkt;
	ctio24->entry_type = CTIO_TYPE7;
	ctio24->nport_handle = sess->loop_id;
	ctio24->timeout = __constant_cpu_to_le16(QLA_TGT_TIMEOUT);
	ctio24->vp_index = vha->vp_idx;
	ctio24->initiator_id[0] = atio->u.isp24.fcp_hdr.s_id[2];
	ctio24->initiator_id[1] = atio->u.isp24.fcp_hdr.s_id[1];
	ctio24->initiator_id[2] = atio->u.isp24.fcp_hdr.s_id[0];
	ctio24->exchange_addr = atio->u.isp24.exchange_addr;
	ctio24->u.status1.flags = (atio->u.isp24.attr << 9) |
	    __constant_cpu_to_le16(
		CTIO7_FLAGS_STATUS_MODE_1 | CTIO7_FLAGS_SEND_STATUS |
		CTIO7_FLAGS_DONT_RET_CTIO);
	/*
	 * CTIO from fw w/o se_cmd doesn't provide enough info to retry it,
	 * if the explicit conformation is used.
	 */
	ctio24->u.status1.ox_id = swab16(atio->u.isp24.fcp_hdr.ox_id);
	ctio24->u.status1.scsi_status = cpu_to_le16(status);
	/* Memory Barrier */
	wmb();
	qla2x00_start_iocbs(vha, vha->req);
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
	struct qla_tgt_sess *sess;
	struct se_session *se_sess;
	struct qla_tgt_cmd *cmd;
	int tag;

	if (unlikely(tgt->tgt_stop)) {
		ql_dbg(ql_dbg_io, vha, 0x300a,
			"New command while device %p is shutting down\n", tgt);
		return;
	}

	if ((vha->hw->tgt.num_qfull_cmds_alloc + 1) > MAX_QFULL_CMDS_ALLOC) {
		vha->hw->tgt.num_qfull_cmds_dropped++;
		if (vha->hw->tgt.num_qfull_cmds_dropped >
			vha->hw->qla_stats.stat_max_qfull_cmds_dropped)
			vha->hw->qla_stats.stat_max_qfull_cmds_dropped =
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
			vha->hw->qla_stats.stat_max_qfull_cmds_dropped)
			vha->hw->qla_stats.stat_max_qfull_cmds_dropped =
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
	cmd->reset_count = vha->hw->chip_reset;
	cmd->q_full = 1;

	if (qfull) {
		cmd->q_full = 1;
		/* NOTE: borrowing the state field to carry the status */
		cmd->state = status;
	} else
		cmd->term_exchg = 1;

	list_add_tail(&cmd->cmd_list, &vha->hw->tgt.q_full_list);

	vha->hw->tgt.num_qfull_cmds_alloc++;
	if (vha->hw->tgt.num_qfull_cmds_alloc >
		vha->hw->qla_stats.stat_max_qfull_cmds_alloc)
		vha->hw->qla_stats.stat_max_qfull_cmds_alloc =
			vha->hw->tgt.num_qfull_cmds_alloc;
}

int
qlt_free_qfull_cmds(struct scsi_qla_host *vha)
{
	struct qla_hw_data *ha = vha->hw;
	unsigned long flags;
	struct qla_tgt_cmd *cmd, *tcmd;
	struct list_head free_list;
	int rc = 0;

	if (list_empty(&ha->tgt.q_full_list))
		return 0;

	INIT_LIST_HEAD(&free_list);

	spin_lock_irqsave(&vha->hw->hardware_lock, flags);

	if (list_empty(&ha->tgt.q_full_list)) {
		spin_unlock_irqrestore(&vha->hw->hardware_lock, flags);
		return 0;
	}

	list_for_each_entry_safe(cmd, tcmd, &ha->tgt.q_full_list, cmd_list) {
		if (cmd->q_full)
			/* cmd->state is a borrowed field to hold status */
			rc = __qlt_send_busy(vha, &cmd->atio, cmd->state);
		else if (cmd->term_exchg)
			rc = __qlt_send_term_exchange(vha, NULL, &cmd->atio);

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
	spin_unlock_irqrestore(&vha->hw->hardware_lock, flags);

	cmd = NULL;

	list_for_each_entry_safe(cmd, tcmd, &free_list, cmd_list) {
		list_del(&cmd->cmd_list);
		/* This cmd was never sent to TCM.  There is no need
		 * to schedule free or call free_cmd
		 */
		qlt_free_cmd(cmd);
	}
	return rc;
}

static void
qlt_send_busy(struct scsi_qla_host *vha,
	struct atio_from_isp *atio, uint16_t status)
{
	int rc = 0;

	rc = __qlt_send_busy(vha, atio, status);
	if (rc == -ENOMEM)
		qlt_alloc_qfull_cmd(vha, atio, status, 1);
}

static int
qlt_chk_qfull_thresh_hold(struct scsi_qla_host *vha,
	struct atio_from_isp *atio)
{
	struct qla_hw_data *ha = vha->hw;
	uint16_t status;

	if (ha->tgt.num_pend_cmds < Q_FULL_THRESH_HOLD(ha))
		return 0;

	status = temp_sam_status;
	qlt_send_busy(vha, atio, status);
	return 1;
}

/* ha->hardware_lock supposed to be held on entry */
/* called via callback from qla2xxx */
static void qlt_24xx_atio_pkt(struct scsi_qla_host *vha,
	struct atio_from_isp *atio)
{
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt *tgt = vha->vha_tgt.qla_tgt;
	int rc;

	if (unlikely(tgt == NULL)) {
		ql_dbg(ql_dbg_io, vha, 0x3064,
		    "ATIO pkt, but no tgt (ha %p)", ha);
		return;
	}
	/*
	 * In tgt_stop mode we also should allow all requests to pass.
	 * Otherwise, some commands can stuck.
	 */

	tgt->irq_cmd_count++;

	switch (atio->u.raw.entry_type) {
	case ATIO_TYPE7:
		if (unlikely(atio->u.isp24.exchange_addr ==
		    ATIO_EXCHANGE_ADDRESS_UNKNOWN)) {
			ql_dbg(ql_dbg_io, vha, 0x3065,
			    "qla_target(%d): ATIO_TYPE7 "
			    "received with UNKNOWN exchange address, "
			    "sending QUEUE_FULL\n", vha->vp_idx);
			qlt_send_busy(vha, atio, SAM_STAT_TASK_SET_FULL);
			break;
		}



		if (likely(atio->u.isp24.fcp_cmnd.task_mgmt_flags == 0)) {
			rc = qlt_chk_qfull_thresh_hold(vha, atio);
			if (rc != 0) {
				tgt->irq_cmd_count--;
				return;
			}
			rc = qlt_handle_cmd_for_atio(vha, atio);
		} else {
			rc = qlt_handle_task_mgmt(vha, atio);
		}
		if (unlikely(rc != 0)) {
			if (rc == -ESRCH) {
#if 1 /* With TERM EXCHANGE some FC cards refuse to boot */
				qlt_send_busy(vha, atio, SAM_STAT_BUSY);
#else
				qlt_send_term_exchange(vha, NULL, atio, 1);
#endif
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
					qlt_send_busy(vha, atio, SAM_STAT_BUSY);
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
		qlt_handle_imm_notify(vha, (struct imm_ntfy_from_isp *)atio);
		break;
	}

	default:
		ql_dbg(ql_dbg_tgt, vha, 0xe05c,
		    "qla_target(%d): Received unknown ATIO atio "
		    "type %x\n", vha->vp_idx, atio->u.raw.entry_type);
		break;
	}

	tgt->irq_cmd_count--;
}

/* ha->hardware_lock supposed to be held on entry */
/* called via callback from qla2xxx */
static void qlt_response_pkt(struct scsi_qla_host *vha, response_t *pkt)
{
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt *tgt = vha->vha_tgt.qla_tgt;

	if (unlikely(tgt == NULL)) {
		ql_dbg(ql_dbg_tgt, vha, 0xe05d,
		    "qla_target(%d): Response pkt %x received, but no "
		    "tgt (ha %p)\n", vha->vp_idx, pkt->entry_type, ha);
		return;
	}

	/*
	 * In tgt_stop mode we also should allow all requests to pass.
	 * Otherwise, some commands can stuck.
	 */

	tgt->irq_cmd_count++;

	switch (pkt->entry_type) {
	case CTIO_CRC2:
	case CTIO_TYPE7:
	{
		struct ctio7_from_24xx *entry = (struct ctio7_from_24xx *)pkt;
		qlt_do_ctio_completion(vha, entry->handle,
		    le16_to_cpu(entry->status)|(pkt->entry_status << 16),
		    entry);
		break;
	}

	case ACCEPT_TGT_IO_TYPE:
	{
		struct atio_from_isp *atio = (struct atio_from_isp *)pkt;
		int rc;
		if (atio->u.isp2x.status !=
		    __constant_cpu_to_le16(ATIO_CDB_VALID)) {
			ql_dbg(ql_dbg_tgt, vha, 0xe05e,
			    "qla_target(%d): ATIO with error "
			    "status %x received\n", vha->vp_idx,
			    le16_to_cpu(atio->u.isp2x.status));
			break;
		}

		rc = qlt_chk_qfull_thresh_hold(vha, atio);
		if (rc != 0) {
			tgt->irq_cmd_count--;
			return;
		}

		rc = qlt_handle_cmd_for_atio(vha, atio);
		if (unlikely(rc != 0)) {
			if (rc == -ESRCH) {
#if 1 /* With TERM EXCHANGE some FC cards refuse to boot */
				qlt_send_busy(vha, atio, 0);
#else
				qlt_send_term_exchange(vha, NULL, atio, 1);
#endif
			} else {
				if (tgt->tgt_stop) {
					ql_dbg(ql_dbg_tgt, vha, 0xe05f,
					    "qla_target: Unable to send "
					    "command to target, sending TERM "
					    "EXCHANGE for rsp\n");
					qlt_send_term_exchange(vha, NULL,
					    atio, 1);
				} else {
					ql_dbg(ql_dbg_tgt, vha, 0xe060,
					    "qla_target(%d): Unable to send "
					    "command to target, sending BUSY "
					    "status\n", vha->vp_idx);
					qlt_send_busy(vha, atio, 0);
				}
			}
		}
	}
	break;

	case CONTINUE_TGT_IO_TYPE:
	{
		struct ctio_to_2xxx *entry = (struct ctio_to_2xxx *)pkt;
		qlt_do_ctio_completion(vha, entry->handle,
		    le16_to_cpu(entry->status)|(pkt->entry_status << 16),
		    entry);
		break;
	}

	case CTIO_A64_TYPE:
	{
		struct ctio_to_2xxx *entry = (struct ctio_to_2xxx *)pkt;
		qlt_do_ctio_completion(vha, entry->handle,
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
			    __constant_cpu_to_le16(NOTIFY_ACK_SUCCESS)) {
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

	tgt->irq_cmd_count--;
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

	if (!ha->tgt.tgt_ops)
		return;

	if (unlikely(tgt == NULL)) {
		ql_dbg(ql_dbg_tgt, vha, 0xe03a,
		    "ASYNC EVENT %#x, but no tgt (ha %p)\n", code, ha);
		return;
	}

	if (((code == MBA_POINT_TO_POINT) || (code == MBA_CHG_IN_CONNECTION)) &&
	    IS_QLA2100(ha))
		return;
	/*
	 * In tgt_stop mode we also should allow all requests to pass.
	 * Otherwise, some commands can stuck.
	 */

	tgt->irq_cmd_count++;

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
			qlt_send_notify_ack(vha, (void *)&tgt->link_reinit_iocb,
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

	case MBA_PORT_UPDATE:
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf03d,
		    "qla_target(%d): Port update async event %#x "
		    "occurred: updating the ports database (m[0]=%x, m[1]=%x, "
		    "m[2]=%x, m[3]=%x)", vha->vp_idx, code,
		    le16_to_cpu(mailbox[0]), le16_to_cpu(mailbox[1]),
		    le16_to_cpu(mailbox[2]), le16_to_cpu(mailbox[3]));

		login_code = le16_to_cpu(mailbox[2]);
		if (login_code == 0x4)
			ql_dbg(ql_dbg_tgt_mgt, vha, 0xf03e,
			    "Async MB 2: Got PLOGI Complete\n");
		else if (login_code == 0x7)
			ql_dbg(ql_dbg_tgt_mgt, vha, 0xf03f,
			    "Async MB 2: Port Logged Out\n");
		break;

	default:
		break;
	}

	tgt->irq_cmd_count--;
}

static fc_port_t *qlt_get_port_database(struct scsi_qla_host *vha,
	uint16_t loop_id)
{
	fc_port_t *fcport;
	int rc;

	fcport = kzalloc(sizeof(*fcport), GFP_KERNEL);
	if (!fcport) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf06f,
		    "qla_target(%d): Allocation of tmp FC port failed",
		    vha->vp_idx);
		return NULL;
	}

	fcport->loop_id = loop_id;

	rc = qla2x00_get_port_database(vha, fcport, 0);
	if (rc != QLA_SUCCESS) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf070,
		    "qla_target(%d): Failed to retrieve fcport "
		    "information -- get_port_database() returned %x "
		    "(loop_id=0x%04x)", vha->vp_idx, rc, loop_id);
		kfree(fcport);
		return NULL;
	}

	return fcport;
}

/* Must be called under tgt_mutex */
static struct qla_tgt_sess *qlt_make_local_sess(struct scsi_qla_host *vha,
	uint8_t *s_id)
{
	struct qla_tgt_sess *sess = NULL;
	fc_port_t *fcport = NULL;
	int rc, global_resets;
	uint16_t loop_id = 0;

retry:
	global_resets =
	    atomic_read(&vha->vha_tgt.qla_tgt->tgt_global_resets_count);

	rc = qla24xx_get_loop_id(vha, s_id, &loop_id);
	if (rc != 0) {
		if ((s_id[0] == 0xFF) &&
		    (s_id[1] == 0xFC)) {
			/*
			 * This is Domain Controller, so it should be
			 * OK to drop SCSI commands from it.
			 */
			ql_dbg(ql_dbg_tgt_mgt, vha, 0xf042,
			    "Unable to find initiator with S_ID %x:%x:%x",
			    s_id[0], s_id[1], s_id[2]);
		} else
			ql_dbg(ql_dbg_tgt_mgt, vha, 0xf071,
			    "qla_target(%d): Unable to find "
			    "initiator with S_ID %x:%x:%x",
			    vha->vp_idx, s_id[0], s_id[1],
			    s_id[2]);
		return NULL;
	}

	fcport = qlt_get_port_database(vha, loop_id);
	if (!fcport)
		return NULL;

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

	kfree(fcport);
	return sess;
}

static void qlt_abort_work(struct qla_tgt *tgt,
	struct qla_tgt_sess_work_param *prm)
{
	struct scsi_qla_host *vha = tgt->vha;
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt_sess *sess = NULL;
	unsigned long flags;
	uint32_t be_s_id;
	uint8_t s_id[3];
	int rc;

	spin_lock_irqsave(&ha->hardware_lock, flags);

	if (tgt->tgt_stop)
		goto out_term;

	s_id[0] = prm->abts.fcp_hdr_le.s_id[2];
	s_id[1] = prm->abts.fcp_hdr_le.s_id[1];
	s_id[2] = prm->abts.fcp_hdr_le.s_id[0];

	sess = ha->tgt.tgt_ops->find_sess_by_s_id(vha,
	    (unsigned char *)&be_s_id);
	if (!sess) {
		spin_unlock_irqrestore(&ha->hardware_lock, flags);

		mutex_lock(&vha->vha_tgt.tgt_mutex);
		sess = qlt_make_local_sess(vha, s_id);
		/* sess has got an extra creation ref */
		mutex_unlock(&vha->vha_tgt.tgt_mutex);

		spin_lock_irqsave(&ha->hardware_lock, flags);
		if (!sess)
			goto out_term;
	} else {
		kref_get(&sess->se_sess->sess_kref);
	}

	if (tgt->tgt_stop)
		goto out_term;

	rc = __qlt_24xx_handle_abts(vha, &prm->abts, sess);
	if (rc != 0)
		goto out_term;

	ha->tgt.tgt_ops->put_sess(sess);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	return;

out_term:
	qlt_24xx_send_abts_resp(vha, &prm->abts, FCP_TMF_REJECTED, false);
	if (sess)
		ha->tgt.tgt_ops->put_sess(sess);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

static void qlt_tmr_work(struct qla_tgt *tgt,
	struct qla_tgt_sess_work_param *prm)
{
	struct atio_from_isp *a = &prm->tm_iocb2;
	struct scsi_qla_host *vha = tgt->vha;
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt_sess *sess = NULL;
	unsigned long flags;
	uint8_t *s_id = NULL; /* to hide compiler warnings */
	int rc;
	uint32_t lun, unpacked_lun;
	int lun_size, fn;
	void *iocb;

	spin_lock_irqsave(&ha->hardware_lock, flags);

	if (tgt->tgt_stop)
		goto out_term;

	s_id = prm->tm_iocb2.u.isp24.fcp_hdr.s_id;
	sess = ha->tgt.tgt_ops->find_sess_by_s_id(vha, s_id);
	if (!sess) {
		spin_unlock_irqrestore(&ha->hardware_lock, flags);

		mutex_lock(&vha->vha_tgt.tgt_mutex);
		sess = qlt_make_local_sess(vha, s_id);
		/* sess has got an extra creation ref */
		mutex_unlock(&vha->vha_tgt.tgt_mutex);

		spin_lock_irqsave(&ha->hardware_lock, flags);
		if (!sess)
			goto out_term;
	} else {
		kref_get(&sess->se_sess->sess_kref);
	}

	iocb = a;
	lun = a->u.isp24.fcp_cmnd.lun;
	lun_size = sizeof(lun);
	fn = a->u.isp24.fcp_cmnd.task_mgmt_flags;
	unpacked_lun = scsilun_to_int((struct scsi_lun *)&lun);

	rc = qlt_issue_task_mgmt(sess, unpacked_lun, fn, iocb, 0);
	if (rc != 0)
		goto out_term;

	ha->tgt.tgt_ops->put_sess(sess);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	return;

out_term:
	qlt_send_term_exchange(vha, NULL, &prm->tm_iocb2, 1);
	if (sess)
		ha->tgt.tgt_ops->put_sess(sess);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
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

	if (!(base_vha->host->hostt->supported_mode & MODE_TARGET))
		base_vha->host->hostt->supported_mode |= MODE_TARGET;

	tgt->ha = ha;
	tgt->vha = base_vha;
	init_waitqueue_head(&tgt->waitQ);
	INIT_LIST_HEAD(&tgt->sess_list);
	INIT_LIST_HEAD(&tgt->del_sess_list);
	INIT_DELAYED_WORK(&tgt->sess_del_work,
		(void (*)(struct work_struct *))qlt_del_sess_work_fn);
	spin_lock_init(&tgt->sess_work_lock);
	INIT_WORK(&tgt->sess_work, qlt_sess_work_fn);
	INIT_LIST_HEAD(&tgt->sess_works_list);
	spin_lock_init(&tgt->srr_lock);
	INIT_LIST_HEAD(&tgt->srr_ctio_list);
	INIT_LIST_HEAD(&tgt->srr_imm_list);
	INIT_WORK(&tgt->srr_work, qlt_handle_srr_work);
	atomic_set(&tgt->tgt_global_resets_count, 0);

	base_vha->vha_tgt.qla_tgt = tgt;

	ql_dbg(ql_dbg_tgt, base_vha, 0xe067,
		"qla_target(%d): using 64 Bit PCI addressing",
		base_vha->vp_idx);
	tgt->tgt_enable_64bit_addr = 1;
	/* 3 is reserved */
	tgt->sg_tablesize = QLA_TGT_MAX_SG_24XX(base_vha->req->length - 3);
	tgt->datasegs_per_cmd = QLA_TGT_DATASEGS_PER_CMD_24XX;
	tgt->datasegs_per_cont = QLA_TGT_DATASEGS_PER_CONT_24XX;

	if (base_vha->fc_vport)
		return 0;

	mutex_lock(&qla_tgt_mutex);
	list_add_tail(&tgt->tgt_list_entry, &qla_tgt_glist);
	mutex_unlock(&qla_tgt_mutex);

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

	mutex_lock(&qla_tgt_mutex);
	list_del(&vha->vha_tgt.qla_tgt->tgt_list_entry);
	mutex_unlock(&qla_tgt_mutex);

	ql_dbg(ql_dbg_tgt, vha, 0xe03c, "Unregistering target for host %ld(%p)",
	    vha->host_no, ha);
	qlt_release(vha->vha_tgt.qla_tgt);

	return 0;
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
	struct qla_hw_data *ha = vha->hw;

	switch (ql2x_ini_mode) {
	case QLA2XXX_INI_MODE_DISABLED:
	case QLA2XXX_INI_MODE_EXCLUSIVE:
		vha->host->active_mode = MODE_TARGET;
		break;
	case QLA2XXX_INI_MODE_ENABLED:
		vha->host->active_mode |= MODE_TARGET;
		break;
	default:
		break;
	}

	if (ha->tgt.ini_mode_force_reverse)
		qla_reverse_ini_mode(vha);
}

/* Must be called under HW lock */
static void qlt_clear_mode(struct scsi_qla_host *vha)
{
	struct qla_hw_data *ha = vha->hw;

	switch (ql2x_ini_mode) {
	case QLA2XXX_INI_MODE_DISABLED:
		vha->host->active_mode = MODE_UNKNOWN;
		break;
	case QLA2XXX_INI_MODE_EXCLUSIVE:
		vha->host->active_mode = MODE_INITIATOR;
		break;
	case QLA2XXX_INI_MODE_ENABLED:
		vha->host->active_mode &= ~MODE_TARGET;
		break;
	default:
		break;
	}

	if (ha->tgt.ini_mode_force_reverse)
		qla_reverse_ini_mode(vha);
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
	if (!qla_tgt_mode_enabled(vha))
		return;

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
		if (qla_ini_mode_enabled(vha))
			ct_req->req.rff_id.fc4_feature = BIT_0 | BIT_1;
		else
			ct_req->req.rff_id.fc4_feature = BIT_0;
	} else if (qla_ini_mode_enabled(vha)) {
		ct_req->req.rff_id.fc4_feature = BIT_1;
	}
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

	if (!qla_tgt_mode_enabled(vha))
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
qlt_24xx_process_atio_queue(struct scsi_qla_host *vha)
{
	struct qla_hw_data *ha = vha->hw;
	struct atio_from_isp *pkt;
	int cnt, i;

	if (!vha->flags.online)
		return;

	while (ha->tgt.atio_ring_ptr->signature != ATIO_PROCESSED) {
		pkt = (struct atio_from_isp *)ha->tgt.atio_ring_ptr;
		cnt = pkt->u.raw.entry_count;

		qlt_24xx_atio_pkt_all_vps(vha, (struct atio_from_isp *)pkt);

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
	if (!QLA_TGT_MODE_ENABLED())
		return;

	WRT_REG_DWORD(ISP_ATIO_Q_IN(vha), 0);
	WRT_REG_DWORD(ISP_ATIO_Q_OUT(vha), 0);
	RD_REG_DWORD(ISP_ATIO_Q_OUT(vha));

	if (IS_ATIO_MSIX_CAPABLE(ha)) {
		struct qla_msix_entry *msix = &ha->msix_entries[2];
		struct init_cb_24xx *icb = (struct init_cb_24xx *)ha->init_cb;

		icb->msix_atio = cpu_to_le16(msix->entry);
		ql_dbg(ql_dbg_init, vha, 0xf072,
		    "Registering ICB vector 0x%x for atio que.\n",
		    msix->entry);
	}
}

void
qlt_24xx_config_nvram_stage1(struct scsi_qla_host *vha, struct nvram_24xx *nv)
{
	struct qla_hw_data *ha = vha->hw;

	if (qla_tgt_mode_enabled(vha)) {
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

		nv->exchange_count = __constant_cpu_to_le16(0xFFFF);

		/* Enable target mode */
		nv->firmware_options_1 |= __constant_cpu_to_le32(BIT_4);

		/* Disable ini mode, if requested */
		if (!qla_ini_mode_enabled(vha))
			nv->firmware_options_1 |= __constant_cpu_to_le32(BIT_5);

		/* Disable Full Login after LIP */
		nv->firmware_options_1 &= __constant_cpu_to_le32(~BIT_13);
		/* Enable initial LIP */
		nv->firmware_options_1 &= __constant_cpu_to_le32(~BIT_9);
		if (ql2xtgt_tape_enable)
			/* Enable FC Tape support */
			nv->firmware_options_2 |= cpu_to_le32(BIT_12);
		else
			/* Disable FC Tape support */
			nv->firmware_options_2 &= cpu_to_le32(~BIT_12);

		/* Disable Full Login after LIP */
		nv->host_p &= __constant_cpu_to_le32(~BIT_10);
		/* Enable target PRLI control */
		nv->firmware_options_2 |= __constant_cpu_to_le32(BIT_14);
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

	/* out-of-order frames reassembly */
	nv->firmware_options_3 |= BIT_6|BIT_9;

	if (ha->tgt.enable_class_2) {
		if (vha->flags.init_done)
			fc_host_supported_classes(vha->host) =
				FC_COS_CLASS2 | FC_COS_CLASS3;

		nv->firmware_options_2 |= __constant_cpu_to_le32(BIT_8);
	} else {
		if (vha->flags.init_done)
			fc_host_supported_classes(vha->host) = FC_COS_CLASS3;

		nv->firmware_options_2 &= ~__constant_cpu_to_le32(BIT_8);
	}
}

void
qlt_24xx_config_nvram_stage2(struct scsi_qla_host *vha,
	struct init_cb_24xx *icb)
{
	struct qla_hw_data *ha = vha->hw;

	if (ha->tgt.node_name_set) {
		memcpy(icb->node_name, ha->tgt.tgt_node_name, WWN_SIZE);
		icb->firmware_options_1 |= __constant_cpu_to_le32(BIT_14);
	}
}

void
qlt_81xx_config_nvram_stage1(struct scsi_qla_host *vha, struct nvram_81xx *nv)
{
	struct qla_hw_data *ha = vha->hw;

	if (!QLA_TGT_MODE_ENABLED())
		return;

	if (qla_tgt_mode_enabled(vha)) {
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

		nv->exchange_count = __constant_cpu_to_le16(0xFFFF);

		/* Enable target mode */
		nv->firmware_options_1 |= __constant_cpu_to_le32(BIT_4);

		/* Disable ini mode, if requested */
		if (!qla_ini_mode_enabled(vha))
			nv->firmware_options_1 |=
			    __constant_cpu_to_le32(BIT_5);

		/* Disable Full Login after LIP */
		nv->firmware_options_1 &= __constant_cpu_to_le32(~BIT_13);
		/* Enable initial LIP */
		nv->firmware_options_1 &= __constant_cpu_to_le32(~BIT_9);
		if (ql2xtgt_tape_enable)
			/* Enable FC tape support */
			nv->firmware_options_2 |= cpu_to_le32(BIT_12);
		else
			/* Disable FC tape support */
			nv->firmware_options_2 &= cpu_to_le32(~BIT_12);

		/* Disable Full Login after LIP */
		nv->host_p &= __constant_cpu_to_le32(~BIT_10);
		/* Enable target PRLI control */
		nv->firmware_options_2 |= __constant_cpu_to_le32(BIT_14);
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

	/* out-of-order frames reassembly */
	nv->firmware_options_3 |= BIT_6|BIT_9;

	if (ha->tgt.enable_class_2) {
		if (vha->flags.init_done)
			fc_host_supported_classes(vha->host) =
				FC_COS_CLASS2 | FC_COS_CLASS3;

		nv->firmware_options_2 |= __constant_cpu_to_le32(BIT_8);
	} else {
		if (vha->flags.init_done)
			fc_host_supported_classes(vha->host) = FC_COS_CLASS3;

		nv->firmware_options_2 &= ~__constant_cpu_to_le32(BIT_8);
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
		icb->firmware_options_1 |= __constant_cpu_to_le32(BIT_14);
	}
}

void
qlt_83xx_iospace_config(struct qla_hw_data *ha)
{
	if (!QLA_TGT_MODE_ENABLED())
		return;

	ha->msix_count += 1; /* For ATIO Q */
}

int
qlt_24xx_process_response_error(struct scsi_qla_host *vha,
	struct sts_entry_24xx *pkt)
{
	switch (pkt->entry_type) {
	case ABTS_RECV_24XX:
	case ABTS_RESP_24XX:
	case CTIO_TYPE7:
	case NOTIFY_ACK_TYPE:
	case CTIO_CRC2:
		return 1;
	default:
		return 0;
	}
}

void
qlt_modify_vp_config(struct scsi_qla_host *vha,
	struct vp_config_entry_24xx *vpmod)
{
	if (qla_tgt_mode_enabled(vha))
		vpmod->options_idx1 &= ~BIT_5;
	/* Disable ini mode, if requested */
	if (!qla_ini_mode_enabled(vha))
		vpmod->options_idx1 &= ~BIT_4;
}

void
qlt_probe_one_stage1(struct scsi_qla_host *base_vha, struct qla_hw_data *ha)
{
	if (!QLA_TGT_MODE_ENABLED())
		return;

	if  (ha->mqenable || IS_QLA83XX(ha)) {
		ISP_ATIO_Q_IN(base_vha) = &ha->mqiobase->isp25mq.atio_q_in;
		ISP_ATIO_Q_OUT(base_vha) = &ha->mqiobase->isp25mq.atio_q_out;
	} else {
		ISP_ATIO_Q_IN(base_vha) = &ha->iobase->isp24.atio_q_in;
		ISP_ATIO_Q_OUT(base_vha) = &ha->iobase->isp24.atio_q_out;
	}

	mutex_init(&base_vha->vha_tgt.tgt_mutex);
	mutex_init(&base_vha->vha_tgt.tgt_host_action_mutex);
	qlt_clear_mode(base_vha);
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

	spin_lock_irqsave(&ha->hardware_lock, flags);

	qlt_24xx_process_atio_queue(vha);
	qla24xx_process_response_queue(vha, rsp);

	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return IRQ_HANDLED;
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
	if (!QLA_TGT_MODE_ENABLED())
		return;

	switch (cmd) {
	case SET_VP_IDX:
		vha->hw->tgt.tgt_vp_map[vha->vp_idx].vha = vha;
		break;
	case SET_AL_PA:
		vha->hw->tgt.tgt_vp_map[vha->d_id.b.al_pa].idx = vha->vp_idx;
		break;
	case RESET_VP_IDX:
		vha->hw->tgt.tgt_vp_map[vha->vp_idx].vha = NULL;
		break;
	case RESET_AL_PA:
		vha->hw->tgt.tgt_vp_map[vha->d_id.b.al_pa].idx = 0;
		break;
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
		ql_log(ql_log_fatal, NULL, 0xe06d,
		    "kmem_cache_create for qla_tgt_mgmt_cmd_cachep failed\n");
		return -ENOMEM;
	}

	qla_tgt_mgmt_cmd_mempool = mempool_create(25, mempool_alloc_slab,
	    mempool_free_slab, qla_tgt_mgmt_cmd_cachep);
	if (!qla_tgt_mgmt_cmd_mempool) {
		ql_log(ql_log_fatal, NULL, 0xe06e,
		    "mempool_create for qla_tgt_mgmt_cmd_mempool failed\n");
		ret = -ENOMEM;
		goto out_mgmt_cmd_cachep;
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
	kmem_cache_destroy(qla_tgt_mgmt_cmd_cachep);
}
