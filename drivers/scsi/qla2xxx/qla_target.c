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
 *  Copyright (C) 2010-2011 Nicholas A. Bellinger <nab@kernel.org>
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
/*
 * Global Variables
 */
static struct kmem_cache *qla_tgt_cmd_cachep;
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

void qlt_24xx_atio_pkt_all_vps(struct scsi_qla_host *vha,
	struct atio_from_isp *atio)
{
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
		qlt_clear_tgt_db(ha->tgt.qla_tgt, 1);
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
	    "scsi(%ld): resetting (session %p from port "
	    "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x, "
	    "mcmd %x, loop_id %d)\n", vha->host_no, sess,
	    sess->port_name[0], sess->port_name[1],
	    sess->port_name[2], sess->port_name[3],
	    sess->port_name[4], sess->port_name[5],
	    sess->port_name[6], sess->port_name[7],
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
	    "qla_target(%d): session for port %02x:%02x:%02x:"
	    "%02x:%02x:%02x:%02x:%02x (loop ID %d) scheduled for "
	    "deletion in %u secs (expires: %lu) immed: %d\n",
	    sess->vha->vp_idx,
	    sess->port_name[0], sess->port_name[1],
	    sess->port_name[2], sess->port_name[3],
	    sess->port_name[4], sess->port_name[5],
	    sess->port_name[6], sess->port_name[7],
	    sess->loop_id, dev_loss_tmo, sess->expires, immediate);

	if (immediate)
		schedule_delayed_work(&tgt->sess_del_work, 0);
	else
		schedule_delayed_work(&tgt->sess_del_work,
		    jiffies - sess->expires);
}

/* ha->hardware_lock supposed to be held on entry */
static void qlt_clear_tgt_db(struct qla_tgt *tgt, bool local_only)
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
	unsigned long flags;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	while (!list_empty(&tgt->del_sess_list)) {
		sess = list_entry(tgt->del_sess_list.next, typeof(*sess),
		    del_list_entry);
		if (time_after_eq(jiffies, sess->expires)) {
			qlt_undelete_sess(sess);

			ql_dbg(ql_dbg_tgt_mgt, vha, 0xf004,
			    "Timeout: sess %p about to be deleted\n",
			    sess);
			ha->tgt.tgt_ops->shutdown_sess(sess);
			ha->tgt.tgt_ops->put_sess(sess);
		} else {
			schedule_delayed_work(&tgt->sess_del_work,
			    jiffies - sess->expires);
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
	list_for_each_entry(sess, &ha->tgt.qla_tgt->sess_list,
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
		    "qla_target(%u): session allocation failed, "
		    "all commands from port %02x:%02x:%02x:%02x:"
		    "%02x:%02x:%02x:%02x will be refused", vha->vp_idx,
		    fcport->port_name[0], fcport->port_name[1],
		    fcport->port_name[2], fcport->port_name[3],
		    fcport->port_name[4], fcport->port_name[5],
		    fcport->port_name[6], fcport->port_name[7]);

		return NULL;
	}
	sess->tgt = ha->tgt.qla_tgt;
	sess->vha = vha;
	sess->s_id = fcport->d_id;
	sess->loop_id = fcport->loop_id;
	sess->local = local;

	ql_dbg(ql_dbg_tgt_mgt, vha, 0xf006,
	    "Adding sess %p to tgt %p via ->check_initiator_node_acl()\n",
	    sess, ha->tgt.qla_tgt);

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
	list_add_tail(&sess->sess_list_entry, &ha->tgt.qla_tgt->sess_list);
	ha->tgt.qla_tgt->sess_count++;
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	ql_dbg(ql_dbg_tgt_mgt, vha, 0xf04b,
	    "qla_target(%d): %ssession for wwn %02x:%02x:%02x:%02x:"
	    "%02x:%02x:%02x:%02x (loop_id %d, s_id %x:%x:%x, confirmed"
	    " completion %ssupported) added\n",
	    vha->vp_idx, local ?  "local " : "", fcport->port_name[0],
	    fcport->port_name[1], fcport->port_name[2], fcport->port_name[3],
	    fcport->port_name[4], fcport->port_name[5], fcport->port_name[6],
	    fcport->port_name[7], fcport->loop_id, sess->s_id.b.domain,
	    sess->s_id.b.area, sess->s_id.b.al_pa, sess->conf_compl_supported ?
	    "" : "not ");

	return sess;
}

/*
 * Called from drivers/scsi/qla2xxx/qla_init.c:qla2x00_reg_remote_port()
 */
void qlt_fc_port_added(struct scsi_qla_host *vha, fc_port_t *fcport)
{
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt *tgt = ha->tgt.qla_tgt;
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

		mutex_lock(&ha->tgt.tgt_mutex);
		sess = qlt_create_sess(vha, fcport, false);
		mutex_unlock(&ha->tgt.tgt_mutex);

		spin_lock_irqsave(&ha->hardware_lock, flags);
	} else {
		kref_get(&sess->se_sess->sess_kref);

		if (sess->deleted) {
			qlt_undelete_sess(sess);

			ql_dbg(ql_dbg_tgt_mgt, vha, 0xf04c,
			    "qla_target(%u): %ssession for port %02x:"
			    "%02x:%02x:%02x:%02x:%02x:%02x:%02x (loop ID %d) "
			    "reappeared\n", vha->vp_idx, sess->local ? "local "
			    : "", sess->port_name[0], sess->port_name[1],
			    sess->port_name[2], sess->port_name[3],
			    sess->port_name[4], sess->port_name[5],
			    sess->port_name[6], sess->port_name[7],
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
		    "port %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x "
		    "(loop ID %d) became global\n", vha->vp_idx,
		    fcport->port_name[0], fcport->port_name[1],
		    fcport->port_name[2], fcport->port_name[3],
		    fcport->port_name[4], fcport->port_name[5],
		    fcport->port_name[6], fcport->port_name[7],
		    sess->loop_id);
		sess->local = 0;
	}
	ha->tgt.tgt_ops->put_sess(sess);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

void qlt_fc_port_deleted(struct scsi_qla_host *vha, fc_port_t *fcport)
{
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt *tgt = ha->tgt.qla_tgt;
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
void qlt_stop_phase1(struct qla_tgt *tgt)
{
	struct scsi_qla_host *vha = tgt->vha;
	struct qla_hw_data *ha = tgt->ha;
	unsigned long flags;

	if (tgt->tgt_stop || tgt->tgt_stopped) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf04e,
		    "Already in tgt->tgt_stop or tgt_stopped state\n");
		dump_stack();
		return;
	}

	ql_dbg(ql_dbg_tgt, vha, 0xe003, "Stopping target for host %ld(%p)\n",
	    vha->host_no, vha);
	/*
	 * Mutex needed to sync with qla_tgt_fc_port_[added,deleted].
	 * Lock is needed, because we still can get an incoming packet.
	 */
	mutex_lock(&ha->tgt.tgt_mutex);
	spin_lock_irqsave(&ha->hardware_lock, flags);
	tgt->tgt_stop = 1;
	qlt_clear_tgt_db(tgt, true);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	mutex_unlock(&ha->tgt.tgt_mutex);

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
}
EXPORT_SYMBOL(qlt_stop_phase1);

/* Called by tcm_qla2xxx configfs code */
void qlt_stop_phase2(struct qla_tgt *tgt)
{
	struct qla_hw_data *ha = tgt->ha;
	unsigned long flags;

	if (tgt->tgt_stopped) {
		ql_dbg(ql_dbg_tgt_mgt, tgt->vha, 0xf04f,
		    "Already in tgt->tgt_stopped state\n");
		dump_stack();
		return;
	}

	ql_dbg(ql_dbg_tgt_mgt, tgt->vha, 0xf00b,
	    "Waiting for %d IRQ commands to complete (tgt %p)",
	    tgt->irq_cmd_count, tgt);

	mutex_lock(&ha->tgt.tgt_mutex);
	spin_lock_irqsave(&ha->hardware_lock, flags);
	while (tgt->irq_cmd_count != 0) {
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
		udelay(2);
		spin_lock_irqsave(&ha->hardware_lock, flags);
	}
	tgt->tgt_stop = 0;
	tgt->tgt_stopped = 1;
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	mutex_unlock(&ha->tgt.tgt_mutex);

	ql_dbg(ql_dbg_tgt_mgt, tgt->vha, 0xf00c, "Stop of tgt %p finished",
	    tgt);
}
EXPORT_SYMBOL(qlt_stop_phase2);

/* Called from qlt_remove_target() -> qla2x00_remove_one() */
static void qlt_release(struct qla_tgt *tgt)
{
	struct qla_hw_data *ha = tgt->ha;

	if ((ha->tgt.qla_tgt != NULL) && !tgt->tgt_stopped)
		qlt_stop_phase2(tgt);

	ha->tgt.qla_tgt = NULL;

	ql_dbg(ql_dbg_tgt_mgt, tgt->vha, 0xf00d,
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

	if (ha->tgt.qla_tgt != NULL)
		ha->tgt.qla_tgt->notify_ack_expected++;

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

	resp = (struct abts_resp_to_24xx *)qla2x00_alloc_iocbs(vha, NULL);
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

	ha->tgt.qla_tgt->abts_resp_expected++;

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

	ctio = (struct ctio7_to_24xx *)qla2x00_alloc_iocbs(vha, NULL);
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
	ctio->u.status1.ox_id = entry->fcp_hdr_le.ox_id;

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
		if (cmd->tag == abts->exchange_addr_to_abort) {
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
		rc = qlt_sched_sess_work(ha->tgt.qla_tgt,
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
	ctio->u.status1.ox_id = swab16(atio->u.isp24.fcp_hdr.ox_id);
	ctio->u.status1.scsi_status =
	    __constant_cpu_to_le16(SS_RESPONSE_INFO_LEN_VALID);
	ctio->u.status1.response_len = __constant_cpu_to_le16(8);
	ctio->u.status1.sense_data[0] = resp_code;

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

	/*
	 * If greater than four sg entries then we need to allocate
	 * the continuation entries
	 */
	if (prm->seg_cnt > prm->tgt->datasegs_per_cmd)
		prm->req_cnt += DIV_ROUND_UP(prm->seg_cnt -
		    prm->tgt->datasegs_per_cmd, prm->tgt->datasegs_per_cont);

	ql_dbg(ql_dbg_tgt, prm->cmd->vha, 0xe009, "seg_cnt=%d, req_cnt=%d\n",
	    prm->seg_cnt, prm->req_cnt);
	return 0;

out_err:
	ql_dbg(ql_dbg_tgt, prm->cmd->vha, 0xe04d,
	    "qla_target(%d): PCI mapping failed: sg_cnt=%d",
	    0, prm->cmd->sg_cnt);
	return -1;
}

static inline void qlt_unmap_sg(struct scsi_qla_host *vha,
	struct qla_tgt_cmd *cmd)
{
	struct qla_hw_data *ha = vha->hw;

	BUG_ON(!cmd->sg_mapped);
	pci_unmap_sg(ha->pdev, cmd->sg, cmd->sg_cnt, cmd->dma_data_direction);
	cmd->sg_mapped = 0;
}

static int qlt_check_reserve_free_req(struct scsi_qla_host *vha,
	uint32_t req_cnt)
{
	struct qla_hw_data *ha = vha->hw;
	device_reg_t __iomem *reg = ha->iobase;
	uint32_t cnt;

	if (vha->req->cnt < (req_cnt + 2)) {
		cnt = (uint16_t)RD_REG_DWORD(&reg->isp24.req_q_out);

		ql_dbg(ql_dbg_tgt, vha, 0xe00a,
		    "Request ring circled: cnt=%d, vha->->ring_index=%d, "
		    "vha->req->cnt=%d, req_cnt=%d\n", cnt,
		    vha->req->ring_index, vha->req->cnt, req_cnt);
		if  (vha->req->ring_index < cnt)
			vha->req->cnt = cnt - vha->req->ring_index;
		else
			vha->req->cnt = vha->req->length -
			    (vha->req->ring_index - cnt);
	}

	if (unlikely(vha->req->cnt < (req_cnt + 2))) {
		ql_dbg(ql_dbg_tgt, vha, 0xe00b,
		    "qla_target(%d): There is no room in the "
		    "request ring: vha->req->ring_index=%d, vha->req->cnt=%d, "
		    "req_cnt=%d\n", vha->vp_idx, vha->req->ring_index,
		    vha->req->cnt, req_cnt);
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
			ql_dbg(ql_dbg_tgt, vha, 0xe04e,
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
	pkt->u.status0.ox_id = swab16(atio->u.isp24.fcp_hdr.ox_id);
	pkt->u.status0.relative_offset = cpu_to_le32(prm->cmd->offset);

	ql_dbg(ql_dbg_tgt, vha, 0xe00c,
	    "qla_target(%d): handle(cmd) -> %08x, timeout %d, ox_id %#x\n",
	    vha->vp_idx, pkt->handle, QLA_TGT_TIMEOUT,
	    le16_to_cpu(pkt->u.status0.ox_id));
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

			ql_dbg(ql_dbg_tgt, vha, 0xe00d,
			    "S/G Segment Cont. phys_addr=%llx:%llx, len=%d\n",
			    (long long unsigned int)
			    pci_dma_hi32(sg_dma_address(prm->sg)),
			    (long long unsigned int)
			    pci_dma_lo32(sg_dma_address(prm->sg)),
			    (int)sg_dma_len(prm->sg));

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

	ql_dbg(ql_dbg_tgt, vha, 0xe00e,
	    "iocb->scsi_status=%x, iocb->flags=%x\n",
	    le16_to_cpu(pkt24->u.status0.scsi_status),
	    le16_to_cpu(pkt24->u.status0.flags));

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
	ql_dbg(ql_dbg_tgt, vha, 0xe00f, "%s", "Building S/G data segments...");

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

		ql_dbg(ql_dbg_tgt, vha, 0xe010,
		    "S/G Segment phys_addr=%llx:%llx, len=%d\n",
		    (long long unsigned int)pci_dma_hi32(sg_dma_address(
		    prm->sg)),
		    (long long unsigned int)pci_dma_lo32(sg_dma_address(
		    prm->sg)),
		    (int)sg_dma_len(prm->sg));

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
		    "qla_target(%d): terminating exchange "
		    "for aborted cmd=%p (se_cmd=%p, tag=%d)", vha->vp_idx, cmd,
		    se_cmd, cmd->tag);

		cmd->state = QLA_TGT_STATE_ABORTED;

		qlt_send_term_exchange(vha, cmd, &cmd->atio, 0);

		/* !! At this point cmd could be already freed !! */
		return QLA_TGT_PRE_XMIT_RESP_CMD_ABORTED;
	}

	ql_dbg(ql_dbg_tgt, vha, 0xe011, "qla_target(%d): tag=%u\n",
	    vha->vp_idx, cmd->tag);

	prm->cmd = cmd;
	prm->tgt = tgt;
	prm->rq_result = scsi_status;
	prm->sense_buffer = &cmd->sense_buffer[0];
	prm->sense_buffer_len = TRANSPORT_SENSE_BUFFER;
	prm->sg = NULL;
	prm->seg_cnt = -1;
	prm->req_cnt = 1;
	prm->add_status_pkt = 0;

	ql_dbg(ql_dbg_tgt, vha, 0xe012, "rq_result=%x, xmit_type=%x\n",
	    prm->rq_result, xmit_type);

	/* Send marker if required */
	if (qlt_issue_marker(vha, 0) != QLA_SUCCESS)
		return -EFAULT;

	ql_dbg(ql_dbg_tgt, vha, 0xe013, "CTIO start: vha(%d)\n", vha->vp_idx);

	if ((xmit_type & QLA_TGT_XMIT_DATA) && qlt_has_data(cmd)) {
		if  (qlt_pci_map_calc_cnt(prm) != 0)
			return -EAGAIN;
	}

	*full_req_cnt = prm->req_cnt;

	if (se_cmd->se_cmd_flags & SCF_UNDERFLOW_BIT) {
		prm->residual = se_cmd->residual_count;
		ql_dbg(ql_dbg_tgt, vha, 0xe014,
		    "Residual underflow: %d (tag %d, "
		    "op %x, bufflen %d, rq_result %x)\n", prm->residual,
		    cmd->tag, se_cmd->t_task_cdb ? se_cmd->t_task_cdb[0] : 0,
		    cmd->bufflen, prm->rq_result);
		prm->rq_result |= SS_RESIDUAL_UNDER;
	} else if (se_cmd->se_cmd_flags & SCF_OVERFLOW_BIT) {
		prm->residual = se_cmd->residual_count;
		ql_dbg(ql_dbg_tgt, vha, 0xe015,
		    "Residual overflow: %d (tag %d, "
		    "op %x, bufflen %d, rq_result %x)\n", prm->residual,
		    cmd->tag, se_cmd->t_task_cdb ? se_cmd->t_task_cdb[0] : 0,
		    cmd->bufflen, prm->rq_result);
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

	ql_dbg(ql_dbg_tgt, vha, 0xe016,
	    "req_cnt=%d, full_req_cnt=%d, add_status_pkt=%d\n",
	    prm->req_cnt, *full_req_cnt, prm->add_status_pkt);

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
		    "Dropping cmd %p (tag %d) status", cmd, cmd->tag);
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
		    " cmd->sg_cnt %d)", cmd, cmd->tag, tot_len, leave,
		    cmd->bufflen, cmd->sg_cnt);

		cmd->bufflen = tot_len;
		cmd->sg_cnt = leave;
	}

	if (qlt_has_data(cmd) && ((qlt_srr_random() % 100) == 70)) {
		unsigned int offset = qlt_srr_random() % cmd->bufflen;

		ql_dbg(ql_dbg_tgt_mgt, cmd->vha, 0xf017,
		    "Cutting cmd %p (tag %d) buffer head "
		    "to offset %d (cmd->bufflen %d)", cmd, cmd->tag, offset,
		    cmd->bufflen);
		if (offset == 0)
			*xmit_type &= ~QLA_TGT_XMIT_DATA;
		else if (qlt_set_data_offset(cmd, offset)) {
			ql_dbg(ql_dbg_tgt_mgt, cmd->vha, 0xf018,
			    "qlt_set_data_offset() failed (tag %d)", cmd->tag);
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
	    "is_send_status=%d, cmd->bufflen=%d, cmd->sg_cnt=%d, "
	    "cmd->dma_data_direction=%d\n", (xmit_type & QLA_TGT_XMIT_STATUS) ?
	    1 : 0, cmd->bufflen, cmd->sg_cnt, cmd->dma_data_direction);

	res = qlt_pre_xmit_response(cmd, &prm, xmit_type, scsi_status,
	    &full_req_cnt);
	if (unlikely(res != 0)) {
		if (res == QLA_TGT_PRE_XMIT_RESP_CMD_ABORTED)
			return 0;

		return res;
	}

	spin_lock_irqsave(&ha->hardware_lock, flags);

	/* Does F/W have an IOCBs for this request */
	res = qlt_check_reserve_free_req(vha, full_req_cnt);
	if (unlikely(res))
		goto out_unmap_unlock;

	res = qlt_24xx_build_ctio_pkt(&prm, vha);
	if (unlikely(res != 0))
		goto out_unmap_unlock;


	pkt = (struct ctio7_to_24xx *)prm.pkt;

	if (qlt_has_data(cmd) && (xmit_type & QLA_TGT_XMIT_DATA)) {
		pkt->u.status0.flags |=
		    __constant_cpu_to_le16(CTIO7_FLAGS_DATA_IN |
			CTIO7_FLAGS_STATUS_MODE_0);

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

			ql_dbg(ql_dbg_tgt, vha, 0xe019,
			    "Building additional status packet\n");

			memcpy(ctio, pkt, sizeof(*ctio));
			ctio->entry_count = 1;
			ctio->dseg_count = 0;
			ctio->u.status1.flags &= ~__constant_cpu_to_le16(
			    CTIO7_FLAGS_DATA_IN);

			/* Real finish is ctio_m1's finish */
			pkt->handle |= CTIO_INTERMEDIATE_HANDLE_MARK;
			pkt->u.status0.flags |= __constant_cpu_to_le16(
			    CTIO7_FLAGS_DONT_RET_CTIO);
			qlt_24xx_init_ctio_to_isp((struct ctio7_to_24xx *)ctio,
			    &prm);
			pr_debug("Status CTIO7: %p\n", ctio);
		}
	} else
		qlt_24xx_init_ctio_to_isp(pkt, &prm);


	cmd->state = QLA_TGT_STATE_PROCESSED; /* Mid-level is done processing */

	ql_dbg(ql_dbg_tgt, vha, 0xe01a,
	    "Xmitting CTIO7 response pkt for 24xx: %p scsi_status: 0x%02x\n",
	    pkt, scsi_status);

	qla2x00_start_iocbs(vha, vha->req);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return 0;

out_unmap_unlock:
	if (cmd->sg_mapped)
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

	ql_dbg(ql_dbg_tgt, vha, 0xe01b, "CTIO_start: vha(%d)",
	    (int)vha->vp_idx);

	/* Calculate number of entries and segments required */
	if (qlt_pci_map_calc_cnt(&prm) != 0)
		return -EAGAIN;

	spin_lock_irqsave(&ha->hardware_lock, flags);

	/* Does F/W have an IOCBs for this request */
	res = qlt_check_reserve_free_req(vha, prm.req_cnt);
	if (res != 0)
		goto out_unlock_free_unmap;

	res = qlt_24xx_build_ctio_pkt(&prm, vha);
	if (unlikely(res != 0))
		goto out_unlock_free_unmap;
	pkt = (struct ctio7_to_24xx *)prm.pkt;
	pkt->u.status0.flags |= __constant_cpu_to_le16(CTIO7_FLAGS_DATA_OUT |
	    CTIO7_FLAGS_STATUS_MODE_0);
	qlt_load_data_segments(&prm, vha);

	cmd->state = QLA_TGT_STATE_NEED_DATA;

	qla2x00_start_iocbs(vha, vha->req);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return res;

out_unlock_free_unmap:
	if (cmd->sg_mapped)
		qlt_unmap_sg(vha, cmd);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return res;
}
EXPORT_SYMBOL(qlt_rdy_to_xfer);

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

	ql_dbg(ql_dbg_tgt, vha, 0xe01c, "Sending TERM EXCH CTIO (ha=%p)\n", ha);

	pkt = (request_t *)qla2x00_alloc_iocbs(vha, NULL);
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
	ctio24->u.status1.ox_id = swab16(atio->u.isp24.fcp_hdr.ox_id);

	/* Most likely, it isn't needed */
	ctio24->u.status1.residual = get_unaligned((uint32_t *)
	    &atio->u.isp24.fcp_cmnd.add_cdb[
	    atio->u.isp24.fcp_cmnd.add_cdb_len]);
	if (ctio24->u.status1.residual != 0)
		ctio24->u.status1.scsi_status |= SS_RESIDUAL_UNDER;

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
		goto done;
	}
	spin_lock_irqsave(&vha->hw->hardware_lock, flags);
	rc = __qlt_send_term_exchange(vha, cmd, atio);
	spin_unlock_irqrestore(&vha->hw->hardware_lock, flags);
done:
	if (rc == 1) {
		if (!ha_locked && !in_interrupt())
			msleep(250); /* just in case */

		vha->hw->tgt.tgt_ops->free_cmd(cmd);
	}
}

void qlt_free_cmd(struct qla_tgt_cmd *cmd)
{
	BUG_ON(cmd->sg_mapped);

	if (unlikely(cmd->free_sg))
		kfree(cmd->sg);
	kmem_cache_free(qla_tgt_cmd_cachep, cmd);
}
EXPORT_SYMBOL(qlt_free_cmd);

/* ha->hardware_lock supposed to be held on entry */
static int qlt_prepare_srr_ctio(struct scsi_qla_host *vha,
	struct qla_tgt_cmd *cmd, void *ctio)
{
	struct qla_tgt_srr_ctio *sc;
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt *tgt = ha->tgt.qla_tgt;
	struct qla_tgt_srr_imm *imm;

	tgt->ctio_srr_id++;

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
		if (unlikely(handle == QLA_TGT_SKIP_HANDLE)) {
			ql_dbg(ql_dbg_tgt, vha, 0xe01d, "%s",
			    "SKIP_HANDLE CTIO\n");
			return NULL;
		}
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

/*
 * ha->hardware_lock supposed to be held on entry. Might drop it, then reaquire
 */
static void qlt_do_ctio_completion(struct scsi_qla_host *vha, uint32_t handle,
	uint32_t status, void *ctio)
{
	struct qla_hw_data *ha = vha->hw;
	struct se_cmd *se_cmd;
	struct target_core_fabric_ops *tfo;
	struct qla_tgt_cmd *cmd;

	ql_dbg(ql_dbg_tgt, vha, 0xe01e,
	    "qla_target(%d): handle(ctio %p status %#x) <- %08x\n",
	    vha->vp_idx, ctio, status, handle);

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

	if (cmd->sg_mapped)
		qlt_unmap_sg(vha, cmd);

	if (unlikely(status != CTIO_SUCCESS)) {
		switch (status & 0xFFFF) {
		case CTIO_LIP_RESET:
		case CTIO_TARGET_RESET:
		case CTIO_ABORTED:
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

		default:
			ql_dbg(ql_dbg_tgt_mgt, vha, 0xf05b,
			    "qla_target(%d): CTIO with error status "
			    "0x%x received (state %x, se_cmd %p\n",
			    vha->vp_idx, status, cmd->state, se_cmd);
			break;
		}

		if (cmd->state != QLA_TGT_STATE_NEED_DATA)
			if (qlt_term_ctio_exchange(vha, ctio, cmd, status))
				return;
	}

	if (cmd->state == QLA_TGT_STATE_PROCESSED) {
		ql_dbg(ql_dbg_tgt, vha, 0xe01f, "Command %p finished\n", cmd);
	} else if (cmd->state == QLA_TGT_STATE_NEED_DATA) {
		int rx_status = 0;

		cmd->state = QLA_TGT_STATE_DATA_IN;

		if (unlikely(status != CTIO_SUCCESS))
			rx_status = -EIO;
		else
			cmd->write_data_transferred = 1;

		ql_dbg(ql_dbg_tgt, vha, 0xe020,
		    "Data received, context %x, rx_status %d\n",
		    0x0, rx_status);

		ha->tgt.tgt_ops->handle_data(cmd);
		return;
	} else if (cmd->state == QLA_TGT_STATE_ABORTED) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf01e,
		    "Aborted command %p (tag %d) finished\n", cmd, cmd->tag);
	} else {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf05c,
		    "qla_target(%d): A command in state (%d) should "
		    "not return a CTIO complete\n", vha->vp_idx, cmd->state);
	}

	if (unlikely(status != CTIO_SUCCESS)) {
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
		fcp_task_attr = MSG_SIMPLE_TAG;
		break;
	case ATIO_HEAD_OF_QUEUE:
		fcp_task_attr = MSG_HEAD_TAG;
		break;
	case ATIO_ORDERED_QUEUE:
		fcp_task_attr = MSG_ORDERED_TAG;
		break;
	case ATIO_ACA_QUEUE:
		fcp_task_attr = MSG_ACA_TAG;
		break;
	case ATIO_UNTAGGED:
		fcp_task_attr = MSG_SIMPLE_TAG;
		break;
	default:
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf05d,
		    "qla_target: unknown task code %x, use ORDERED instead\n",
		    task_codes);
		fcp_task_attr = MSG_ORDERED_TAG;
		break;
	}

	return fcp_task_attr;
}

static struct qla_tgt_sess *qlt_make_local_sess(struct scsi_qla_host *,
					uint8_t *);
/*
 * Process context for I/O path into tcm_qla2xxx code
 */
static void qlt_do_work(struct work_struct *work)
{
	struct qla_tgt_cmd *cmd = container_of(work, struct qla_tgt_cmd, work);
	scsi_qla_host_t *vha = cmd->vha;
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt *tgt = ha->tgt.qla_tgt;
	struct qla_tgt_sess *sess = NULL;
	struct atio_from_isp *atio = &cmd->atio;
	unsigned char *cdb;
	unsigned long flags;
	uint32_t data_length;
	int ret, fcp_task_attr, data_dir, bidi = 0;

	if (tgt->tgt_stop)
		goto out_term;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	sess = ha->tgt.tgt_ops->find_sess_by_s_id(vha,
	    atio->u.isp24.fcp_hdr.s_id);
	/* Do kref_get() before dropping qla_hw_data->hardware_lock. */
	if (sess)
		kref_get(&sess->se_sess->sess_kref);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	if (unlikely(!sess)) {
		uint8_t *s_id =	atio->u.isp24.fcp_hdr.s_id;

		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf022,
			"qla_target(%d): Unable to find wwn login"
			" (s_id %x:%x:%x), trying to create it manually\n",
			vha->vp_idx, s_id[0], s_id[1], s_id[2]);

		if (atio->u.raw.entry_count > 1) {
			ql_dbg(ql_dbg_tgt_mgt, vha, 0xf023,
				"Dropping multy entry cmd %p\n", cmd);
			goto out_term;
		}

		mutex_lock(&ha->tgt.tgt_mutex);
		sess = qlt_make_local_sess(vha, s_id);
		/* sess has an extra creation ref. */
		mutex_unlock(&ha->tgt.tgt_mutex);

		if (!sess)
			goto out_term;
	}

	cmd->sess = sess;
	cmd->loop_id = sess->loop_id;
	cmd->conf_compl_supported = sess->conf_compl_supported;

	cdb = &atio->u.isp24.fcp_cmnd.cdb[0];
	cmd->tag = atio->u.isp24.exchange_addr;
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

	ql_dbg(ql_dbg_tgt, vha, 0xe022,
	    "qla_target: START qla command: %p lun: 0x%04x (tag %d)\n",
	    cmd, cmd->unpacked_lun, cmd->tag);

	ret = vha->hw->tgt.tgt_ops->handle_cmd(vha, cmd, cdb, data_length,
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
	ql_dbg(ql_dbg_tgt_mgt, vha, 0xf020, "Terminating work cmd %p", cmd);
	/*
	 * cmd has not sent to target yet, so pass NULL as the second
	 * argument to qlt_send_term_exchange() and free the memory here.
	 */
	spin_lock_irqsave(&ha->hardware_lock, flags);
	qlt_send_term_exchange(vha, NULL, &cmd->atio, 1);
	kmem_cache_free(qla_tgt_cmd_cachep, cmd);
	if (sess)
		ha->tgt.tgt_ops->put_sess(sess);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

/* ha->hardware_lock supposed to be held on entry */
static int qlt_handle_cmd_for_atio(struct scsi_qla_host *vha,
	struct atio_from_isp *atio)
{
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt *tgt = ha->tgt.qla_tgt;
	struct qla_tgt_cmd *cmd;

	if (unlikely(tgt->tgt_stop)) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf021,
		    "New command while device %p is shutting down\n", tgt);
		return -EFAULT;
	}

	cmd = kmem_cache_zalloc(qla_tgt_cmd_cachep, GFP_ATOMIC);
	if (!cmd) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf05e,
		    "qla_target(%d): Allocation of cmd failed\n", vha->vp_idx);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&cmd->cmd_list);

	memcpy(&cmd->atio, atio, sizeof(*atio));
	cmd->state = QLA_TGT_STATE_NEW;
	cmd->tgt = ha->tgt.qla_tgt;
	cmd->vha = vha;

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

	tgt = ha->tgt.qla_tgt;

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
		return qlt_sched_sess_work(ha->tgt.qla_tgt,
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
	struct qla_hw_data *ha = vha->hw;
	int res = 0;

	ql_dbg(ql_dbg_tgt_mgt, vha, 0xf026,
	    "qla_target(%d): Port ID: 0x%02x:%02x:%02x"
	    " ELS opcode: 0x%02x\n", vha->vp_idx, iocb->u.isp24.port_id[0],
	    iocb->u.isp24.port_id[1], iocb->u.isp24.port_id[2],
	    iocb->u.isp24.status_subcode);

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
		struct qla_tgt *tgt = ha->tgt.qla_tgt;
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
			    "qla_target(%d): SRR for in data for cmd "
			    "without them (tag %d, SCSI status %d), "
			    "reject", vha->vp_idx, cmd->tag,
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
			if (xmit_type & QLA_TGT_XMIT_DATA)
				qlt_rdy_to_xfer(cmd);
		} else {
			ql_dbg(ql_dbg_tgt_mgt, vha, 0xf066,
			    "qla_target(%d): SRR for out data for cmd "
			    "without them (tag %d, SCSI status %d), "
			    "reject", vha->vp_idx, cmd->tag,
			    cmd->se_cmd.scsi_status);
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
	if (resp)
		qlt_xmit_response(cmd, xmit_type, se_cmd->scsi_status);

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
	} else
		qlt_send_term_exchange(vha, cmd, &cmd->atio, 1);
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
		    "SRR cmd %p (se_cmd %p, tag %d, op %x), "
		    "sg_cnt=%d, offset=%d", cmd, &cmd->se_cmd, cmd->tag,
		    se_cmd->t_task_cdb[0], cmd->sg_cnt, cmd->offset);

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
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt *tgt = ha->tgt.qla_tgt;
	struct qla_tgt_srr_ctio *sctio;

	tgt->imm_srr_id++;

	ql_dbg(ql_dbg_tgt_mgt, vha, 0xf02d, "qla_target(%d): SRR received\n",
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
		struct qla_tgt *tgt = ha->tgt.qla_tgt;
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
static void qlt_send_busy(struct scsi_qla_host *vha,
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
		return;
	}
	/* Sending marker isn't necessary, since we called from ISR */

	pkt = (request_t *)qla2x00_alloc_iocbs(vha, NULL);
	if (!pkt) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf06e,
		    "qla_target(%d): %s failed: unable to allocate "
		    "request packet", vha->vp_idx, __func__);
		return;
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
	ctio24->u.status1.residual = get_unaligned((uint32_t *)
	    &atio->u.isp24.fcp_cmnd.add_cdb[
	    atio->u.isp24.fcp_cmnd.add_cdb_len]);
	if (ctio24->u.status1.residual != 0)
		ctio24->u.status1.scsi_status |= SS_RESIDUAL_UNDER;

	qla2x00_start_iocbs(vha, vha->req);
}

/* ha->hardware_lock supposed to be held on entry */
/* called via callback from qla2xxx */
static void qlt_24xx_atio_pkt(struct scsi_qla_host *vha,
	struct atio_from_isp *atio)
{
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt *tgt = ha->tgt.qla_tgt;
	int rc;

	if (unlikely(tgt == NULL)) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf039,
		    "ATIO pkt, but no tgt (ha %p)", ha);
		return;
	}
	ql_dbg(ql_dbg_tgt, vha, 0xe02c,
	    "qla_target(%d): ATIO pkt %p: type %02x count %02x",
	    vha->vp_idx, atio, atio->u.raw.entry_type,
	    atio->u.raw.entry_count);
	/*
	 * In tgt_stop mode we also should allow all requests to pass.
	 * Otherwise, some commands can stuck.
	 */

	tgt->irq_cmd_count++;

	switch (atio->u.raw.entry_type) {
	case ATIO_TYPE7:
		ql_dbg(ql_dbg_tgt, vha, 0xe02d,
		    "ATIO_TYPE7 instance %d, lun %Lx, read/write %d/%d, "
		    "add_cdb_len %d, data_length %04x, s_id %x:%x:%x\n",
		    vha->vp_idx, atio->u.isp24.fcp_cmnd.lun,
		    atio->u.isp24.fcp_cmnd.rddata,
		    atio->u.isp24.fcp_cmnd.wrdata,
		    atio->u.isp24.fcp_cmnd.add_cdb_len,
		    be32_to_cpu(get_unaligned((uint32_t *)
			&atio->u.isp24.fcp_cmnd.add_cdb[
			atio->u.isp24.fcp_cmnd.add_cdb_len])),
		    atio->u.isp24.fcp_hdr.s_id[0],
		    atio->u.isp24.fcp_hdr.s_id[1],
		    atio->u.isp24.fcp_hdr.s_id[2]);

		if (unlikely(atio->u.isp24.exchange_addr ==
		    ATIO_EXCHANGE_ADDRESS_UNKNOWN)) {
			ql_dbg(ql_dbg_tgt, vha, 0xe058,
			    "qla_target(%d): ATIO_TYPE7 "
			    "received with UNKNOWN exchange address, "
			    "sending QUEUE_FULL\n", vha->vp_idx);
			qlt_send_busy(vha, atio, SAM_STAT_TASK_SET_FULL);
			break;
		}
		if (likely(atio->u.isp24.fcp_cmnd.task_mgmt_flags == 0))
			rc = qlt_handle_cmd_for_atio(vha, atio);
		else
			rc = qlt_handle_task_mgmt(vha, atio);
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
	struct qla_tgt *tgt = ha->tgt.qla_tgt;

	if (unlikely(tgt == NULL)) {
		ql_dbg(ql_dbg_tgt, vha, 0xe05d,
		    "qla_target(%d): Response pkt %x received, but no "
		    "tgt (ha %p)\n", vha->vp_idx, pkt->entry_type, ha);
		return;
	}

	ql_dbg(ql_dbg_tgt, vha, 0xe02f,
	    "qla_target(%d): response pkt %p: T %02x C %02x S %02x "
	    "handle %#x\n", vha->vp_idx, pkt, pkt->entry_type,
	    pkt->entry_count, pkt->entry_status, pkt->handle);

	/*
	 * In tgt_stop mode we also should allow all requests to pass.
	 * Otherwise, some commands can stuck.
	 */

	tgt->irq_cmd_count++;

	switch (pkt->entry_type) {
	case CTIO_TYPE7:
	{
		struct ctio7_from_24xx *entry = (struct ctio7_from_24xx *)pkt;
		ql_dbg(ql_dbg_tgt, vha, 0xe030, "CTIO_TYPE7: instance %d\n",
		    vha->vp_idx);
		qlt_do_ctio_completion(vha, entry->handle,
		    le16_to_cpu(entry->status)|(pkt->entry_status << 16),
		    entry);
		break;
	}

	case ACCEPT_TGT_IO_TYPE:
	{
		struct atio_from_isp *atio = (struct atio_from_isp *)pkt;
		int rc;
		ql_dbg(ql_dbg_tgt, vha, 0xe031,
		    "ACCEPT_TGT_IO instance %d status %04x "
		    "lun %04x read/write %d data_length %04x "
		    "target_id %02x rx_id %04x\n ", vha->vp_idx,
		    le16_to_cpu(atio->u.isp2x.status),
		    le16_to_cpu(atio->u.isp2x.lun),
		    atio->u.isp2x.execution_codes,
		    le32_to_cpu(atio->u.isp2x.data_length), GET_TARGET_ID(ha,
		    atio), atio->u.isp2x.rx_id);
		if (atio->u.isp2x.status !=
		    __constant_cpu_to_le16(ATIO_CDB_VALID)) {
			ql_dbg(ql_dbg_tgt, vha, 0xe05e,
			    "qla_target(%d): ATIO with error "
			    "status %x received\n", vha->vp_idx,
			    le16_to_cpu(atio->u.isp2x.status));
			break;
		}
		ql_dbg(ql_dbg_tgt, vha, 0xe032,
		    "FCP CDB: 0x%02x, sizeof(cdb): %lu",
		    atio->u.isp2x.cdb[0], (unsigned long
		    int)sizeof(atio->u.isp2x.cdb));

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
		ql_dbg(ql_dbg_tgt, vha, 0xe033,
		    "CONTINUE_TGT_IO: instance %d\n", vha->vp_idx);
		qlt_do_ctio_completion(vha, entry->handle,
		    le16_to_cpu(entry->status)|(pkt->entry_status << 16),
		    entry);
		break;
	}

	case CTIO_A64_TYPE:
	{
		struct ctio_to_2xxx *entry = (struct ctio_to_2xxx *)pkt;
		ql_dbg(ql_dbg_tgt, vha, 0xe034, "CTIO_A64: instance %d\n",
		    vha->vp_idx);
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
	struct qla_tgt *tgt = ha->tgt.qla_tgt;
	int login_code;

	ql_dbg(ql_dbg_tgt, vha, 0xe039,
	    "scsi(%ld): ha state %d init_done %d oper_mode %d topo %d\n",
	    vha->host_no, atomic_read(&vha->loop_state), vha->flags.init_done,
	    ha->operating_mode, ha->current_topology);

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
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf040,
		    "qla_target(%d): Async event %#x occurred: "
		    "ignore (m[0]=%x, m[1]=%x, m[2]=%x, m[3]=%x)", vha->vp_idx,
		    code, le16_to_cpu(mailbox[0]), le16_to_cpu(mailbox[1]),
		    le16_to_cpu(mailbox[2]), le16_to_cpu(mailbox[3]));
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

	ql_dbg(ql_dbg_tgt_mgt, vha, 0xf041, "loop_id %d", loop_id);

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
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt_sess *sess = NULL;
	fc_port_t *fcport = NULL;
	int rc, global_resets;
	uint16_t loop_id = 0;

retry:
	global_resets = atomic_read(&ha->tgt.qla_tgt->tgt_global_resets_count);

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
	    atomic_read(&ha->tgt.qla_tgt->tgt_global_resets_count)) {
		ql_dbg(ql_dbg_tgt_mgt, vha, 0xf043,
		    "qla_target(%d): global reset during session discovery "
		    "(counter was %d, new %d), retrying", vha->vp_idx,
		    global_resets,
		    atomic_read(&ha->tgt.qla_tgt->tgt_global_resets_count));
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

		mutex_lock(&ha->tgt.tgt_mutex);
		sess = qlt_make_local_sess(vha, s_id);
		/* sess has got an extra creation ref */
		mutex_unlock(&ha->tgt.tgt_mutex);

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

		mutex_lock(&ha->tgt.tgt_mutex);
		sess = qlt_make_local_sess(vha, s_id);
		/* sess has got an extra creation ref */
		mutex_unlock(&ha->tgt.tgt_mutex);

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
	    "Registering target for host %ld(%p)", base_vha->host_no, ha);

	BUG_ON((ha->tgt.qla_tgt != NULL) || (ha->tgt.tgt_ops != NULL));

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

	ha->tgt.qla_tgt = tgt;

	ql_dbg(ql_dbg_tgt, base_vha, 0xe067,
		"qla_target(%d): using 64 Bit PCI addressing",
		base_vha->vp_idx);
	tgt->tgt_enable_64bit_addr = 1;
	/* 3 is reserved */
	tgt->sg_tablesize = QLA_TGT_MAX_SG_24XX(base_vha->req->length - 3);
	tgt->datasegs_per_cmd = QLA_TGT_DATASEGS_PER_CMD_24XX;
	tgt->datasegs_per_cont = QLA_TGT_DATASEGS_PER_CONT_24XX;

	mutex_lock(&qla_tgt_mutex);
	list_add_tail(&tgt->tgt_list_entry, &qla_tgt_glist);
	mutex_unlock(&qla_tgt_mutex);

	return 0;
}

/* Must be called under tgt_host_action_mutex */
int qlt_remove_target(struct qla_hw_data *ha, struct scsi_qla_host *vha)
{
	if (!ha->tgt.qla_tgt)
		return 0;

	mutex_lock(&qla_tgt_mutex);
	list_del(&ha->tgt.qla_tgt->tgt_list_entry);
	mutex_unlock(&qla_tgt_mutex);

	ql_dbg(ql_dbg_tgt, vha, 0xe03c, "Unregistering target for host %ld(%p)",
	    vha->host_no, ha);
	qlt_release(ha->tgt.qla_tgt);

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
int qlt_lport_register(struct qla_tgt_func_tmpl *qla_tgt_ops, u64 wwpn,
	int (*callback)(struct scsi_qla_host *), void *target_lport_ptr)
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

		if (ha->tgt.tgt_ops != NULL)
			continue;

		if (!(host->hostt->supported_mode & MODE_TARGET))
			continue;

		spin_lock_irqsave(&ha->hardware_lock, flags);
		if (host->active_mode & MODE_TARGET) {
			pr_debug("MODE_TARGET already active on qla2xxx(%d)\n",
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
		qlt_lport_dump(vha, wwpn, b);

		if (memcmp(vha->port_name, b, WWN_SIZE)) {
			scsi_host_put(host);
			continue;
		}
		/*
		 * Setup passed parameters ahead of invoking callback
		 */
		ha->tgt.tgt_ops = qla_tgt_ops;
		ha->tgt.target_lport_ptr = target_lport_ptr;
		rc = (*callback)(vha);
		if (rc != 0) {
			ha->tgt.tgt_ops = NULL;
			ha->tgt.target_lport_ptr = NULL;
		}
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
	ha->tgt.target_lport_ptr = NULL;
	ha->tgt.tgt_ops = NULL;
	/*
	 * Release the Scsi_Host reference for the underlying qla2xxx host
	 */
	scsi_host_put(sh);
}
EXPORT_SYMBOL(qlt_lport_deregister);

/* Must be called under HW lock */
void qlt_set_mode(struct scsi_qla_host *vha)
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
void qlt_clear_mode(struct scsi_qla_host *vha)
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
	struct qla_tgt *tgt = ha->tgt.qla_tgt;
	unsigned long flags;

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

	set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
	qla2xxx_wake_dpc(vha);
	qla2x00_wait_for_hba_online(vha);
}
EXPORT_SYMBOL(qlt_enable_vha);

/*
 * qla_tgt_disable_vha - NO LOCK HELD
 *
 * Disable Target Mode and reset the adapter
 */
void
qlt_disable_vha(struct scsi_qla_host *vha)
{
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt *tgt = ha->tgt.qla_tgt;
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

	mutex_init(&ha->tgt.tgt_mutex);
	mutex_init(&ha->tgt.tgt_host_action_mutex);

	qlt_clear_mode(vha);

	/*
	 * NOTE: Currently the value is kept the same for <24xx and
	 * >=24xx ISPs. If it is necessary to change it,
	 * the check should be added for specific ISPs,
	 * assigning the value appropriately.
	 */
	ha->tgt.atio_q_length = ATIO_ENTRY_CNT_24XX;
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
		/* Enable FC tapes support */
		nv->firmware_options_2 |= __constant_cpu_to_le32(BIT_12);
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
		/* Enable FC tapes support */
		nv->firmware_options_2 |= __constant_cpu_to_le32(BIT_12);
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

	mutex_init(&ha->tgt.tgt_mutex);
	mutex_init(&ha->tgt.tgt_host_action_mutex);
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

	qla_tgt_cmd_cachep = kmem_cache_create("qla_tgt_cmd_cachep",
	    sizeof(struct qla_tgt_cmd), __alignof__(struct qla_tgt_cmd), 0,
	    NULL);
	if (!qla_tgt_cmd_cachep) {
		ql_log(ql_log_fatal, NULL, 0xe06c,
		    "kmem_cache_create for qla_tgt_cmd_cachep failed\n");
		return -ENOMEM;
	}

	qla_tgt_mgmt_cmd_cachep = kmem_cache_create("qla_tgt_mgmt_cmd_cachep",
	    sizeof(struct qla_tgt_mgmt_cmd), __alignof__(struct
	    qla_tgt_mgmt_cmd), 0, NULL);
	if (!qla_tgt_mgmt_cmd_cachep) {
		ql_log(ql_log_fatal, NULL, 0xe06d,
		    "kmem_cache_create for qla_tgt_mgmt_cmd_cachep failed\n");
		ret = -ENOMEM;
		goto out;
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
out:
	kmem_cache_destroy(qla_tgt_cmd_cachep);
	return ret;
}

void qlt_exit(void)
{
	if (!QLA_TGT_MODE_ENABLED())
		return;

	destroy_workqueue(qla_tgt_wq);
	mempool_destroy(qla_tgt_mgmt_cmd_mempool);
	kmem_cache_destroy(qla_tgt_mgmt_cmd_cachep);
	kmem_cache_destroy(qla_tgt_cmd_cachep);
}
