// SPDX-License-Identifier: GPL-2.0-only
/*
 * Marvell Fibre Channel HBA Driver
 * Copyright (c)  2021     Marvell
 */
#include "qla_def.h"
#include "qla_edif.h"

#include <linux/kthread.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <scsi/scsi_tcq.h>

static struct edif_sa_index_entry *qla_edif_sadb_find_sa_index_entry(uint16_t nport_handle,
		struct list_head *sa_list);
static uint16_t qla_edif_sadb_get_sa_index(fc_port_t *fcport,
		struct qla_sa_update_frame *sa_frame);
static int qla_edif_sadb_delete_sa_index(fc_port_t *fcport, uint16_t nport_handle,
		uint16_t sa_index);
static int qla_pur_get_pending(scsi_qla_host_t *, fc_port_t *, struct bsg_job *);

struct edb_node {
	struct  list_head	list;
	uint32_t		ntype;
	union {
		port_id_t	plogi_did;
		uint32_t	async;
		port_id_t	els_sid;
		struct edif_sa_update_aen	sa_aen;
	} u;
};

static struct els_sub_cmd {
	uint16_t cmd;
	const char *str;
} sc_str[] = {
	{SEND_ELS, "send ELS"},
	{SEND_ELS_REPLY, "send ELS Reply"},
	{PULL_ELS, "retrieve ELS"},
};

const char *sc_to_str(uint16_t cmd)
{
	int i;
	struct els_sub_cmd *e;

	for (i = 0; i < ARRAY_SIZE(sc_str); i++) {
		e = sc_str + i;
		if (cmd == e->cmd)
			return e->str;
	}
	return "unknown";
}

static struct edb_node *qla_edb_getnext(scsi_qla_host_t *vha)
{
	unsigned long   flags;
	struct edb_node *edbnode = NULL;

	spin_lock_irqsave(&vha->e_dbell.db_lock, flags);

	/* db nodes are fifo - no qualifications done */
	if (!list_empty(&vha->e_dbell.head)) {
		edbnode = list_first_entry(&vha->e_dbell.head,
					   struct edb_node, list);
		list_del_init(&edbnode->list);
	}

	spin_unlock_irqrestore(&vha->e_dbell.db_lock, flags);

	return edbnode;
}

static void qla_edb_node_free(scsi_qla_host_t *vha, struct edb_node *node)
{
	list_del_init(&node->list);
	kfree(node);
}

static struct edif_list_entry *qla_edif_list_find_sa_index(fc_port_t *fcport,
		uint16_t handle)
{
	struct edif_list_entry *entry;
	struct edif_list_entry *tentry;
	struct list_head *indx_list = &fcport->edif.edif_indx_list;

	list_for_each_entry_safe(entry, tentry, indx_list, next) {
		if (entry->handle == handle)
			return entry;
	}
	return NULL;
}

/* timeout called when no traffic and delayed rx sa_index delete */
static void qla2x00_sa_replace_iocb_timeout(struct timer_list *t)
{
	struct edif_list_entry *edif_entry = timer_container_of(edif_entry, t,
								timer);
	fc_port_t *fcport = edif_entry->fcport;
	struct scsi_qla_host *vha = fcport->vha;
	struct  edif_sa_ctl *sa_ctl;
	uint16_t nport_handle;
	unsigned long flags = 0;

	ql_dbg(ql_dbg_edif, vha, 0x3069,
	    "%s:  nport_handle 0x%x,  SA REPL Delay Timeout, %8phC portid=%06x\n",
	    __func__, edif_entry->handle, fcport->port_name, fcport->d_id.b24);

	/*
	 * if delete_sa_index is valid then no one has serviced this
	 * delayed delete
	 */
	spin_lock_irqsave(&fcport->edif.indx_list_lock, flags);

	/*
	 * delete_sa_index is invalidated when we find the new sa_index in
	 * the incoming data stream.  If it is not invalidated then we are
	 * still looking for the new sa_index because there is no I/O and we
	 * need to just force the rx delete and move on.  Otherwise
	 * we could get another rekey which will result in an error 66.
	 */
	if (edif_entry->delete_sa_index != INVALID_EDIF_SA_INDEX) {
		uint16_t delete_sa_index = edif_entry->delete_sa_index;

		edif_entry->delete_sa_index = INVALID_EDIF_SA_INDEX;
		nport_handle = edif_entry->handle;
		spin_unlock_irqrestore(&fcport->edif.indx_list_lock, flags);

		sa_ctl = qla_edif_find_sa_ctl_by_index(fcport,
		    delete_sa_index, 0);

		if (sa_ctl) {
			ql_dbg(ql_dbg_edif, vha, 0x3063,
			    "%s: sa_ctl: %p, delete index %d, update index: %d, lid: 0x%x\n",
			    __func__, sa_ctl, delete_sa_index, edif_entry->update_sa_index,
			    nport_handle);

			sa_ctl->flags = EDIF_SA_CTL_FLG_DEL;
			set_bit(EDIF_SA_CTL_REPL, &sa_ctl->state);
			qla_post_sa_replace_work(fcport->vha, fcport,
			    nport_handle, sa_ctl);

		} else {
			ql_dbg(ql_dbg_edif, vha, 0x3063,
			    "%s: sa_ctl not found for delete_sa_index: %d\n",
			    __func__, edif_entry->delete_sa_index);
		}
	} else {
		spin_unlock_irqrestore(&fcport->edif.indx_list_lock, flags);
	}
}

/*
 * create a new list entry for this nport handle and
 * add an sa_update index to the list - called for sa_update
 */
static int qla_edif_list_add_sa_update_index(fc_port_t *fcport,
		uint16_t sa_index, uint16_t handle)
{
	struct edif_list_entry *entry;
	unsigned long flags = 0;

	/* if the entry exists, then just update the sa_index */
	entry = qla_edif_list_find_sa_index(fcport, handle);
	if (entry) {
		entry->update_sa_index = sa_index;
		entry->count = 0;
		return 0;
	}

	/*
	 * This is the normal path - there should be no existing entry
	 * when update is called.  The exception is at startup
	 * when update is called for the first two sa_indexes
	 * followed by a delete of the first sa_index
	 */
	entry = kzalloc((sizeof(struct edif_list_entry)), GFP_ATOMIC);
	if (!entry)
		return -ENOMEM;

	INIT_LIST_HEAD(&entry->next);
	entry->handle = handle;
	entry->update_sa_index = sa_index;
	entry->delete_sa_index = INVALID_EDIF_SA_INDEX;
	entry->count = 0;
	entry->flags = 0;
	timer_setup(&entry->timer, qla2x00_sa_replace_iocb_timeout, 0);
	spin_lock_irqsave(&fcport->edif.indx_list_lock, flags);
	list_add_tail(&entry->next, &fcport->edif.edif_indx_list);
	spin_unlock_irqrestore(&fcport->edif.indx_list_lock, flags);
	return 0;
}

/* remove an entry from the list */
static void qla_edif_list_delete_sa_index(fc_port_t *fcport, struct edif_list_entry *entry)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&fcport->edif.indx_list_lock, flags);
	list_del(&entry->next);
	spin_unlock_irqrestore(&fcport->edif.indx_list_lock, flags);
}

int qla_post_sa_replace_work(struct scsi_qla_host *vha,
	 fc_port_t *fcport, uint16_t nport_handle, struct edif_sa_ctl *sa_ctl)
{
	struct qla_work_evt *e;

	e = qla2x00_alloc_work(vha, QLA_EVT_SA_REPLACE);
	if (!e)
		return QLA_FUNCTION_FAILED;

	e->u.sa_update.fcport = fcport;
	e->u.sa_update.sa_ctl = sa_ctl;
	e->u.sa_update.nport_handle = nport_handle;
	fcport->flags |= FCF_ASYNC_ACTIVE;
	return qla2x00_post_work(vha, e);
}

static void
qla_edif_sa_ctl_init(scsi_qla_host_t *vha, struct fc_port  *fcport)
{
	ql_dbg(ql_dbg_edif, vha, 0x2058,
	    "Init SA_CTL List for fcport - nn %8phN pn %8phN portid=%06x.\n",
	    fcport->node_name, fcport->port_name, fcport->d_id.b24);

	fcport->edif.tx_rekey_cnt = 0;
	fcport->edif.rx_rekey_cnt = 0;

	fcport->edif.tx_bytes = 0;
	fcport->edif.rx_bytes = 0;
}

static int qla_bsg_check(scsi_qla_host_t *vha, struct bsg_job *bsg_job,
fc_port_t *fcport)
{
	struct extra_auth_els *p;
	struct fc_bsg_reply *bsg_reply = bsg_job->reply;
	struct qla_bsg_auth_els_request *req =
	    (struct qla_bsg_auth_els_request *)bsg_job->request;

	if (!vha->hw->flags.edif_enabled) {
		ql_dbg(ql_dbg_edif, vha, 0x9105,
		    "%s edif not enabled\n", __func__);
		goto done;
	}
	if (DBELL_INACTIVE(vha)) {
		ql_dbg(ql_dbg_edif, vha, 0x09102,
		    "%s doorbell not enabled\n", __func__);
		goto done;
	}

	p = &req->e;

	/* Get response */
	if (p->sub_cmd == PULL_ELS) {
		struct qla_bsg_auth_els_reply *rpl =
			(struct qla_bsg_auth_els_reply *)bsg_job->reply;

		qla_pur_get_pending(vha, fcport, bsg_job);

		ql_dbg(ql_dbg_edif, vha, 0x911d,
			"%s %s %8phN sid=%x. xchg %x, nb=%xh bsg ptr %p\n",
			__func__, sc_to_str(p->sub_cmd), fcport->port_name,
			fcport->d_id.b24, rpl->rx_xchg_address,
			rpl->r.reply_payload_rcv_len, bsg_job);

		goto done;
	}
	return 0;

done:

	bsg_job_done(bsg_job, bsg_reply->result,
			bsg_reply->reply_payload_rcv_len);
	return -EIO;
}

fc_port_t *
qla2x00_find_fcport_by_pid(scsi_qla_host_t *vha, port_id_t *id)
{
	fc_port_t *f, *tf;

	f = NULL;
	list_for_each_entry_safe(f, tf, &vha->vp_fcports, list) {
		if (f->d_id.b24 == id->b24)
			return f;
	}
	return NULL;
}

/**
 * qla_edif_app_check(): check for valid application id.
 * @vha: host adapter pointer
 * @appid: application id
 * Return: false = fail, true = pass
 */
static bool
qla_edif_app_check(scsi_qla_host_t *vha, struct app_id appid)
{
	/* check that the app is allow/known to the driver */

	if (appid.app_vid != EDIF_APP_ID) {
		ql_dbg(ql_dbg_edif, vha, 0x911d, "%s app id not ok (%x)",
		    __func__, appid.app_vid);
		return false;
	}

	if (appid.version != EDIF_VERSION1) {
		ql_dbg(ql_dbg_edif, vha, 0x911d, "%s app version is not ok (%x)",
		    __func__, appid.version);
		return false;
	}

	return true;
}

static void
qla_edif_free_sa_ctl(fc_port_t *fcport, struct edif_sa_ctl *sa_ctl,
	int index)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&fcport->edif.sa_list_lock, flags);
	list_del(&sa_ctl->next);
	spin_unlock_irqrestore(&fcport->edif.sa_list_lock, flags);
	if (index >= 512)
		fcport->edif.tx_rekey_cnt--;
	else
		fcport->edif.rx_rekey_cnt--;
	kfree(sa_ctl);
}

/* return an index to the freepool */
static void qla_edif_add_sa_index_to_freepool(fc_port_t *fcport, int dir,
		uint16_t sa_index)
{
	void *sa_id_map;
	struct scsi_qla_host *vha = fcport->vha;
	struct qla_hw_data *ha = vha->hw;
	unsigned long flags = 0;
	u16 lsa_index = sa_index;

	ql_dbg(ql_dbg_edif + ql_dbg_verbose, vha, 0x3063,
	    "%s: entry\n", __func__);

	if (dir) {
		sa_id_map = ha->edif_tx_sa_id_map;
		lsa_index -= EDIF_TX_SA_INDEX_BASE;
	} else {
		sa_id_map = ha->edif_rx_sa_id_map;
	}

	spin_lock_irqsave(&ha->sadb_fp_lock, flags);
	clear_bit(lsa_index, sa_id_map);
	spin_unlock_irqrestore(&ha->sadb_fp_lock, flags);
	ql_dbg(ql_dbg_edif, vha, 0x3063,
	    "%s: index %d added to free pool\n", __func__, sa_index);
}

static void __qla2x00_release_all_sadb(struct scsi_qla_host *vha,
	struct fc_port *fcport, struct edif_sa_index_entry *entry,
	int pdir)
{
	struct edif_list_entry *edif_entry;
	struct  edif_sa_ctl *sa_ctl;
	int i, dir;
	int key_cnt = 0;

	for (i = 0; i < 2; i++) {
		if (entry->sa_pair[i].sa_index == INVALID_EDIF_SA_INDEX)
			continue;

		if (fcport->loop_id != entry->handle) {
			ql_dbg(ql_dbg_edif, vha, 0x3063,
			    "%s: ** WARNING %d** entry handle: 0x%x, lid: 0x%x, sa_index: %d\n",
			    __func__, i, entry->handle, fcport->loop_id,
			    entry->sa_pair[i].sa_index);
		}

		/* release the sa_ctl */
		sa_ctl = qla_edif_find_sa_ctl_by_index(fcport,
				entry->sa_pair[i].sa_index, pdir);
		if (sa_ctl &&
		    qla_edif_find_sa_ctl_by_index(fcport, sa_ctl->index, pdir)) {
			ql_dbg(ql_dbg_edif, vha, 0x3063,
			    "%s: freeing sa_ctl for index %d\n", __func__, sa_ctl->index);
			qla_edif_free_sa_ctl(fcport, sa_ctl, sa_ctl->index);
		} else {
			ql_dbg(ql_dbg_edif, vha, 0x3063,
			    "%s: sa_ctl NOT freed, sa_ctl: %p\n", __func__, sa_ctl);
		}

		/* Release the index */
		ql_dbg(ql_dbg_edif, vha, 0x3063,
			"%s: freeing sa_index %d, nph: 0x%x\n",
			__func__, entry->sa_pair[i].sa_index, entry->handle);

		dir = (entry->sa_pair[i].sa_index <
			EDIF_TX_SA_INDEX_BASE) ? 0 : 1;
		qla_edif_add_sa_index_to_freepool(fcport, dir,
			entry->sa_pair[i].sa_index);

		/* Delete timer on RX */
		if (pdir != SAU_FLG_TX) {
			edif_entry =
				qla_edif_list_find_sa_index(fcport, entry->handle);
			if (edif_entry) {
				ql_dbg(ql_dbg_edif, vha, 0x5033,
				    "%s: remove edif_entry %p, update_sa_index: 0x%x, delete_sa_index: 0x%x\n",
				    __func__, edif_entry, edif_entry->update_sa_index,
				    edif_entry->delete_sa_index);
				qla_edif_list_delete_sa_index(fcport, edif_entry);
				/*
				 * valid delete_sa_index indicates there is a rx
				 * delayed delete queued
				 */
				if (edif_entry->delete_sa_index !=
						INVALID_EDIF_SA_INDEX) {
					timer_shutdown(&edif_entry->timer);

					/* build and send the aen */
					fcport->edif.rx_sa_set = 1;
					fcport->edif.rx_sa_pending = 0;
					qla_edb_eventcreate(vha,
							VND_CMD_AUTH_STATE_SAUPDATE_COMPL,
							QL_VND_SA_STAT_SUCCESS,
							QL_VND_RX_SA_KEY, fcport);
				}
				ql_dbg(ql_dbg_edif, vha, 0x5033,
				    "%s: release edif_entry %p, update_sa_index: 0x%x, delete_sa_index: 0x%x\n",
				    __func__, edif_entry, edif_entry->update_sa_index,
				    edif_entry->delete_sa_index);

				kfree(edif_entry);
			}
		}
		key_cnt++;
	}
	ql_dbg(ql_dbg_edif, vha, 0x3063,
	    "%s: %d %s keys released\n",
	    __func__, key_cnt, pdir ? "tx" : "rx");
}

/* find an release all outstanding sadb sa_indicies */
void qla2x00_release_all_sadb(struct scsi_qla_host *vha, struct fc_port *fcport)
{
	struct edif_sa_index_entry *entry, *tmp;
	struct qla_hw_data *ha = vha->hw;
	unsigned long flags;

	ql_dbg(ql_dbg_edif + ql_dbg_verbose, vha, 0x3063,
	    "%s: Starting...\n", __func__);

	spin_lock_irqsave(&ha->sadb_lock, flags);

	list_for_each_entry_safe(entry, tmp, &ha->sadb_rx_index_list, next) {
		if (entry->fcport == fcport) {
			list_del(&entry->next);
			spin_unlock_irqrestore(&ha->sadb_lock, flags);
			__qla2x00_release_all_sadb(vha, fcport, entry, 0);
			kfree(entry);
			spin_lock_irqsave(&ha->sadb_lock, flags);
			break;
		}
	}

	list_for_each_entry_safe(entry, tmp, &ha->sadb_tx_index_list, next) {
		if (entry->fcport == fcport) {
			list_del(&entry->next);
			spin_unlock_irqrestore(&ha->sadb_lock, flags);

			__qla2x00_release_all_sadb(vha, fcport, entry, SAU_FLG_TX);

			kfree(entry);
			spin_lock_irqsave(&ha->sadb_lock, flags);
			break;
		}
	}
	spin_unlock_irqrestore(&ha->sadb_lock, flags);
}

/**
 * qla_delete_n2n_sess_and_wait: search for N2N session, tear it down and
 *    wait for tear down to complete.  In N2N topology, there is only one
 *    session being active in tracking the remote device.
 * @vha: host adapter pointer
 * return code:  0 - found the session and completed the tear down.
 *	1 - timeout occurred.  Caller to use link bounce to reset.
 */
static int qla_delete_n2n_sess_and_wait(scsi_qla_host_t *vha)
{
	struct fc_port *fcport;
	int rc = -EIO;
	ulong expire = jiffies + 23 * HZ;

	if (!N2N_TOPO(vha->hw))
		return 0;

	fcport = NULL;
	list_for_each_entry(fcport, &vha->vp_fcports, list) {
		if (!fcport->n2n_flag)
			continue;

		ql_dbg(ql_dbg_disc, fcport->vha, 0x2016,
		       "%s reset sess at app start \n", __func__);

		qla_edif_sa_ctl_init(vha, fcport);
		qlt_schedule_sess_for_deletion(fcport);

		while (time_before_eq(jiffies, expire)) {
			if (fcport->disc_state != DSC_DELETE_PEND) {
				rc = 0;
				break;
			}
			msleep(1);
		}

		set_bit(RELOGIN_NEEDED, &vha->dpc_flags);
		break;
	}

	return rc;
}

/**
 * qla_edif_app_start:  application has announce its present
 * @vha: host adapter pointer
 * @bsg_job: user request
 *
 * Set/activate doorbell.  Reset current sessions and re-login with
 * secure flag.
 */
static int
qla_edif_app_start(scsi_qla_host_t *vha, struct bsg_job *bsg_job)
{
	int32_t			rval = 0;
	struct fc_bsg_reply	*bsg_reply = bsg_job->reply;
	struct app_start	appstart;
	struct app_start_reply	appreply;
	struct fc_port  *fcport, *tf;

	ql_log(ql_log_info, vha, 0x1313,
	       "EDIF application registration with driver, FC device connections will be re-established.\n");

	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
	    bsg_job->request_payload.sg_cnt, &appstart,
	    sizeof(struct app_start));

	ql_dbg(ql_dbg_edif, vha, 0x911d, "%s app_vid=%x app_start_flags %x\n",
	     __func__, appstart.app_info.app_vid, appstart.app_start_flags);

	if (DBELL_INACTIVE(vha)) {
		/* mark doorbell as active since an app is now present */
		vha->e_dbell.db_flags |= EDB_ACTIVE;
	} else {
		goto out;
	}

	if (N2N_TOPO(vha->hw)) {
		list_for_each_entry_safe(fcport, tf, &vha->vp_fcports, list)
			fcport->n2n_link_reset_cnt = 0;

		if (vha->hw->flags.n2n_fw_acc_sec) {
			bool link_bounce = false;
			/*
			 * While authentication app was not running, remote device
			 * could still try to login with this local port.  Let's
			 * reset the session, reconnect and re-authenticate.
			 */
			if (qla_delete_n2n_sess_and_wait(vha))
				link_bounce = true;

			/* bounce the link to start login */
			if (!vha->hw->flags.n2n_bigger || link_bounce) {
				set_bit(N2N_LINK_RESET, &vha->dpc_flags);
				qla2xxx_wake_dpc(vha);
			}
		} else {
			qla2x00_wait_for_hba_online(vha);
			set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
			qla2xxx_wake_dpc(vha);
			qla2x00_wait_for_hba_online(vha);
		}
	} else {
		list_for_each_entry_safe(fcport, tf, &vha->vp_fcports, list) {
			ql_dbg(ql_dbg_edif, vha, 0x2058,
			       "FCSP - nn %8phN pn %8phN portid=%06x.\n",
			       fcport->node_name, fcport->port_name,
			       fcport->d_id.b24);
			ql_dbg(ql_dbg_edif, vha, 0xf084,
			       "%s: se_sess %p / sess %p from port %8phC "
			       "loop_id %#04x s_id %06x logout %d "
			       "keep %d els_logo %d disc state %d auth state %d"
			       "stop state %d\n",
			       __func__, fcport->se_sess, fcport,
			       fcport->port_name, fcport->loop_id,
			       fcport->d_id.b24, fcport->logout_on_delete,
			       fcport->keep_nport_handle, fcport->send_els_logo,
			       fcport->disc_state, fcport->edif.auth_state,
			       fcport->edif.app_stop);

			if (atomic_read(&vha->loop_state) == LOOP_DOWN)
				break;

			fcport->login_retry = vha->hw->login_retry_count;

			fcport->edif.app_stop = 0;
			fcport->edif.app_sess_online = 0;

			if (fcport->scan_state != QLA_FCPORT_FOUND)
				continue;

			if (fcport->port_type == FCT_UNKNOWN &&
			    !fcport->fc4_features)
				rval = qla24xx_async_gffid(vha, fcport, true);

			if (!rval && !(fcport->fc4_features & FC4_FF_TARGET ||
			    fcport->port_type & (FCT_TARGET|FCT_NVME_TARGET)))
				continue;

			rval = 0;

			ql_dbg(ql_dbg_edif, vha, 0x911e,
			       "%s wwpn %8phC calling qla_edif_reset_auth_wait\n",
			       __func__, fcport->port_name);
			qlt_schedule_sess_for_deletion(fcport);
			qla_edif_sa_ctl_init(vha, fcport);
		}
		set_bit(RELOGIN_NEEDED, &vha->dpc_flags);
	}

	if (vha->pur_cinfo.enode_flags != ENODE_ACTIVE) {
		/* mark as active since an app is now present */
		vha->pur_cinfo.enode_flags = ENODE_ACTIVE;
	} else {
		ql_dbg(ql_dbg_edif, vha, 0x911f, "%s enode already active\n",
		     __func__);
	}

out:
	appreply.host_support_edif = vha->hw->flags.edif_enabled;
	appreply.edif_enode_active = vha->pur_cinfo.enode_flags;
	appreply.edif_edb_active = vha->e_dbell.db_flags;
	appreply.version = EDIF_VERSION1;

	bsg_job->reply_len = sizeof(struct fc_bsg_reply);

	SET_DID_STATUS(bsg_reply->result, DID_OK);

	bsg_reply->reply_payload_rcv_len = sg_copy_from_buffer(bsg_job->reply_payload.sg_list,
							       bsg_job->reply_payload.sg_cnt,
							       &appreply,
							       sizeof(struct app_start_reply));

	ql_dbg(ql_dbg_edif, vha, 0x911d,
	    "%s app start completed with 0x%x\n",
	    __func__, rval);

	return rval;
}

/**
 * qla_edif_app_stop - app has announced it's exiting.
 * @vha: host adapter pointer
 * @bsg_job: user space command pointer
 *
 * Free any in flight messages, clear all doorbell events
 * to application. Reject any message relate to security.
 */
static int
qla_edif_app_stop(scsi_qla_host_t *vha, struct bsg_job *bsg_job)
{
	struct app_stop         appstop;
	struct fc_bsg_reply     *bsg_reply = bsg_job->reply;
	struct fc_port  *fcport, *tf;

	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
	    bsg_job->request_payload.sg_cnt, &appstop,
	    sizeof(struct app_stop));

	ql_dbg(ql_dbg_edif, vha, 0x911d, "%s Stopping APP: app_vid=%x\n",
	    __func__, appstop.app_info.app_vid);

	/* Call db stop and enode stop functions */

	/* if we leave this running short waits are operational < 16 secs */
	qla_enode_stop(vha);        /* stop enode */
	qla_edb_stop(vha);          /* stop db */

	list_for_each_entry_safe(fcport, tf, &vha->vp_fcports, list) {
		if (!(fcport->flags & FCF_FCSP_DEVICE))
			continue;

		if (fcport->flags & FCF_FCSP_DEVICE) {
			ql_dbg(ql_dbg_edif, vha, 0xf084,
			    "%s: sess %p from port %8phC lid %#04x s_id %06x logout %d keep %d els_logo %d\n",
			    __func__, fcport,
			    fcport->port_name, fcport->loop_id, fcport->d_id.b24,
			    fcport->logout_on_delete, fcport->keep_nport_handle,
			    fcport->send_els_logo);

			if (atomic_read(&vha->loop_state) == LOOP_DOWN)
				break;

			fcport->edif.app_stop = 1;
			ql_dbg(ql_dbg_edif, vha, 0x911e,
				"%s wwpn %8phC calling qla_edif_reset_auth_wait\n",
				__func__, fcport->port_name);

			fcport->send_els_logo = 1;
			qlt_schedule_sess_for_deletion(fcport);
		}
	}

	bsg_job->reply_len = sizeof(struct fc_bsg_reply);
	SET_DID_STATUS(bsg_reply->result, DID_OK);

	/* no return interface to app - it assumes we cleaned up ok */

	return 0;
}

static int
qla_edif_app_chk_sa_update(scsi_qla_host_t *vha, fc_port_t *fcport,
		struct app_plogi_reply *appplogireply)
{
	int	ret = 0;

	if (!(fcport->edif.rx_sa_set && fcport->edif.tx_sa_set)) {
		ql_dbg(ql_dbg_edif, vha, 0x911e,
		    "%s: wwpn %8phC Both SA indexes has not been SET TX %d, RX %d.\n",
		    __func__, fcport->port_name, fcport->edif.tx_sa_set,
		    fcport->edif.rx_sa_set);
		appplogireply->prli_status = 0;
		ret = 1;
	} else  {
		ql_dbg(ql_dbg_edif, vha, 0x911e,
		    "%s wwpn %8phC Both SA(s) updated.\n", __func__,
		    fcport->port_name);
		fcport->edif.rx_sa_set = fcport->edif.tx_sa_set = 0;
		fcport->edif.rx_sa_pending = fcport->edif.tx_sa_pending = 0;
		appplogireply->prli_status = 1;
	}
	return ret;
}

/**
 * qla_edif_app_authok - authentication by app succeeded.  Driver can proceed
 *   with prli
 * @vha: host adapter pointer
 * @bsg_job: user request
 */
static int
qla_edif_app_authok(scsi_qla_host_t *vha, struct bsg_job *bsg_job)
{
	struct auth_complete_cmd appplogiok;
	struct app_plogi_reply	appplogireply = {0};
	struct fc_bsg_reply	*bsg_reply = bsg_job->reply;
	fc_port_t		*fcport = NULL;
	port_id_t		portid = {0};

	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
	    bsg_job->request_payload.sg_cnt, &appplogiok,
	    sizeof(struct auth_complete_cmd));

	/* silent unaligned access warning */
	portid.b.domain = appplogiok.u.d_id.b.domain;
	portid.b.area   = appplogiok.u.d_id.b.area;
	portid.b.al_pa  = appplogiok.u.d_id.b.al_pa;

	appplogireply.version = EDIF_VERSION1;
	switch (appplogiok.type) {
	case PL_TYPE_WWPN:
		fcport = qla2x00_find_fcport_by_wwpn(vha,
		    appplogiok.u.wwpn, 0);
		if (!fcport)
			ql_dbg(ql_dbg_edif, vha, 0x911d,
			    "%s wwpn lookup failed: %8phC\n",
			    __func__, appplogiok.u.wwpn);
		break;
	case PL_TYPE_DID:
		fcport = qla2x00_find_fcport_by_pid(vha, &portid);
		if (!fcport)
			ql_dbg(ql_dbg_edif, vha, 0x911d,
			    "%s d_id lookup failed: %x\n", __func__,
			    portid.b24);
		break;
	default:
		ql_dbg(ql_dbg_edif, vha, 0x911d,
		    "%s undefined type: %x\n", __func__,
		    appplogiok.type);
		break;
	}

	if (!fcport) {
		SET_DID_STATUS(bsg_reply->result, DID_ERROR);
		goto errstate_exit;
	}

	/*
	 * if port is online then this is a REKEY operation
	 * Only do sa update checking
	 */
	if (atomic_read(&fcport->state) == FCS_ONLINE) {
		ql_dbg(ql_dbg_edif, vha, 0x911d,
		    "%s Skipping PRLI complete based on rekey\n", __func__);
		appplogireply.prli_status = 1;
		SET_DID_STATUS(bsg_reply->result, DID_OK);
		qla_edif_app_chk_sa_update(vha, fcport, &appplogireply);
		goto errstate_exit;
	}

	/* make sure in AUTH_PENDING or else reject */
	if (fcport->disc_state != DSC_LOGIN_AUTH_PEND) {
		ql_dbg(ql_dbg_edif, vha, 0x911e,
		    "%s wwpn %8phC is not in auth pending state (%x)\n",
		    __func__, fcport->port_name, fcport->disc_state);
		SET_DID_STATUS(bsg_reply->result, DID_OK);
		appplogireply.prli_status = 0;
		goto errstate_exit;
	}

	SET_DID_STATUS(bsg_reply->result, DID_OK);
	appplogireply.prli_status = 1;
	fcport->edif.authok = 1;
	if (!(fcport->edif.rx_sa_set && fcport->edif.tx_sa_set)) {
		ql_dbg(ql_dbg_edif, vha, 0x911e,
		    "%s: wwpn %8phC Both SA indexes has not been SET TX %d, RX %d.\n",
		    __func__, fcport->port_name, fcport->edif.tx_sa_set,
		    fcport->edif.rx_sa_set);
		SET_DID_STATUS(bsg_reply->result, DID_OK);
		appplogireply.prli_status = 0;
		goto errstate_exit;

	} else {
		ql_dbg(ql_dbg_edif, vha, 0x911e,
		    "%s wwpn %8phC Both SA(s) updated.\n", __func__,
		    fcport->port_name);
		fcport->edif.rx_sa_set = fcport->edif.tx_sa_set = 0;
		fcport->edif.rx_sa_pending = fcport->edif.tx_sa_pending = 0;
	}

	if (qla_ini_mode_enabled(vha)) {
		ql_dbg(ql_dbg_edif, vha, 0x911e,
		    "%s AUTH complete - RESUME with prli for wwpn %8phC\n",
		    __func__, fcport->port_name);
		qla24xx_post_prli_work(vha, fcport);
	}

errstate_exit:
	bsg_job->reply_len = sizeof(struct fc_bsg_reply);
	bsg_reply->reply_payload_rcv_len = sg_copy_from_buffer(bsg_job->reply_payload.sg_list,
							       bsg_job->reply_payload.sg_cnt,
							       &appplogireply,
							       sizeof(struct app_plogi_reply));

	return 0;
}

/**
 * qla_edif_app_authfail - authentication by app has failed.  Driver is given
 *   notice to tear down current session.
 * @vha: host adapter pointer
 * @bsg_job: user request
 */
static int
qla_edif_app_authfail(scsi_qla_host_t *vha, struct bsg_job *bsg_job)
{
	int32_t			rval = 0;
	struct auth_complete_cmd appplogifail;
	struct fc_bsg_reply	*bsg_reply = bsg_job->reply;
	fc_port_t		*fcport = NULL;
	port_id_t		portid = {0};

	ql_dbg(ql_dbg_edif, vha, 0x911d, "%s app auth fail\n", __func__);

	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
	    bsg_job->request_payload.sg_cnt, &appplogifail,
	    sizeof(struct auth_complete_cmd));

	/* silent unaligned access warning */
	portid.b.domain = appplogifail.u.d_id.b.domain;
	portid.b.area   = appplogifail.u.d_id.b.area;
	portid.b.al_pa  = appplogifail.u.d_id.b.al_pa;

	/*
	 * TODO: edif: app has failed this plogi. Inform driver to
	 * take any action (if any).
	 */
	switch (appplogifail.type) {
	case PL_TYPE_WWPN:
		fcport = qla2x00_find_fcport_by_wwpn(vha,
		    appplogifail.u.wwpn, 0);
		SET_DID_STATUS(bsg_reply->result, DID_OK);
		break;
	case PL_TYPE_DID:
		fcport = qla2x00_find_fcport_by_pid(vha, &portid);
		if (!fcport)
			ql_dbg(ql_dbg_edif, vha, 0x911d,
			    "%s d_id lookup failed: %x\n", __func__,
			    portid.b24);
		SET_DID_STATUS(bsg_reply->result, DID_OK);
		break;
	default:
		ql_dbg(ql_dbg_edif, vha, 0x911e,
		    "%s undefined type: %x\n", __func__,
		    appplogifail.type);
		bsg_job->reply_len = sizeof(struct fc_bsg_reply);
		SET_DID_STATUS(bsg_reply->result, DID_ERROR);
		rval = -1;
		break;
	}

	ql_dbg(ql_dbg_edif, vha, 0x911d,
	    "%s fcport is 0x%p\n", __func__, fcport);

	if (fcport) {
		/* set/reset edif values and flags */
		ql_dbg(ql_dbg_edif, vha, 0x911e,
		    "%s reset the auth process - %8phC, loopid=%x portid=%06x.\n",
		    __func__, fcport->port_name, fcport->loop_id, fcport->d_id.b24);

		if (qla_ini_mode_enabled(fcport->vha)) {
			fcport->send_els_logo = 1;
			qlt_schedule_sess_for_deletion(fcport);
		}
	}

	return rval;
}

/**
 * qla_edif_app_getfcinfo - app would like to read session info (wwpn, nportid,
 *   [initiator|target] mode.  It can specific session with specific nport id or
 *   all sessions.
 * @vha: host adapter pointer
 * @bsg_job: user request pointer
 */
static int
qla_edif_app_getfcinfo(scsi_qla_host_t *vha, struct bsg_job *bsg_job)
{
	int32_t			rval = 0;
	int32_t			pcnt = 0;
	struct fc_bsg_reply	*bsg_reply = bsg_job->reply;
	struct app_pinfo_req	app_req;
	struct app_pinfo_reply	*app_reply;
	port_id_t		tdid;

	ql_dbg(ql_dbg_edif, vha, 0x911d, "%s app get fcinfo\n", __func__);

	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
	    bsg_job->request_payload.sg_cnt, &app_req,
	    sizeof(struct app_pinfo_req));

	app_reply = kzalloc((sizeof(struct app_pinfo_reply) +
	    sizeof(struct app_pinfo) * app_req.num_ports), GFP_KERNEL);

	if (!app_reply) {
		SET_DID_STATUS(bsg_reply->result, DID_ERROR);
		rval = -1;
	} else {
		struct fc_port	*fcport = NULL, *tf;

		app_reply->version = EDIF_VERSION1;

		list_for_each_entry_safe(fcport, tf, &vha->vp_fcports, list) {
			if (!(fcport->flags & FCF_FCSP_DEVICE))
				continue;

			tdid.b.domain = app_req.remote_pid.domain;
			tdid.b.area = app_req.remote_pid.area;
			tdid.b.al_pa = app_req.remote_pid.al_pa;

			ql_dbg(ql_dbg_edif, vha, 0x2058,
			    "APP request entry - portid=%06x.\n", tdid.b24);

			/* Ran out of space */
			if (pcnt >= app_req.num_ports)
				break;

			if (tdid.b24 != 0 && tdid.b24 != fcport->d_id.b24)
				continue;

			if (!N2N_TOPO(vha->hw)) {
				if (fcport->scan_state != QLA_FCPORT_FOUND)
					continue;

				if (fcport->port_type == FCT_UNKNOWN &&
				    !fcport->fc4_features)
					rval = qla24xx_async_gffid(vha, fcport,
								   true);

				if (!rval &&
				    !(fcport->fc4_features & FC4_FF_TARGET ||
				      fcport->port_type &
				      (FCT_TARGET | FCT_NVME_TARGET)))
					continue;
			}

			rval = 0;

			app_reply->ports[pcnt].version = EDIF_VERSION1;
			app_reply->ports[pcnt].remote_type =
				VND_CMD_RTYPE_UNKNOWN;
			if (fcport->port_type & (FCT_NVME_TARGET | FCT_TARGET))
				app_reply->ports[pcnt].remote_type |=
					VND_CMD_RTYPE_TARGET;
			if (fcport->port_type & (FCT_NVME_INITIATOR | FCT_INITIATOR))
				app_reply->ports[pcnt].remote_type |=
					VND_CMD_RTYPE_INITIATOR;

			app_reply->ports[pcnt].remote_pid = fcport->d_id;

			ql_dbg(ql_dbg_edif, vha, 0x2058,
			    "Found FC_SP fcport - nn %8phN pn %8phN pcnt %d portid=%06x secure %d.\n",
			    fcport->node_name, fcport->port_name, pcnt,
			    fcport->d_id.b24, fcport->flags & FCF_FCSP_DEVICE);

			switch (fcport->edif.auth_state) {
			case VND_CMD_AUTH_STATE_ELS_RCVD:
				if (fcport->disc_state == DSC_LOGIN_AUTH_PEND) {
					fcport->edif.auth_state = VND_CMD_AUTH_STATE_NEEDED;
					app_reply->ports[pcnt].auth_state =
						VND_CMD_AUTH_STATE_NEEDED;
				} else {
					app_reply->ports[pcnt].auth_state =
						VND_CMD_AUTH_STATE_ELS_RCVD;
				}
				break;
			default:
				app_reply->ports[pcnt].auth_state = fcport->edif.auth_state;
				break;
			}

			memcpy(app_reply->ports[pcnt].remote_wwpn,
			    fcport->port_name, 8);

			app_reply->ports[pcnt].remote_state =
				(atomic_read(&fcport->state) ==
				    FCS_ONLINE ? 1 : 0);

			pcnt++;

			if (tdid.b24 != 0)
				break;
		}
		app_reply->port_count = pcnt;
		SET_DID_STATUS(bsg_reply->result, DID_OK);
	}

	bsg_job->reply_len = sizeof(struct fc_bsg_reply);
	bsg_reply->reply_payload_rcv_len = sg_copy_from_buffer(bsg_job->reply_payload.sg_list,
							       bsg_job->reply_payload.sg_cnt,
							       app_reply,
							       sizeof(struct app_pinfo_reply) + sizeof(struct app_pinfo) * pcnt);

	kfree(app_reply);

	return rval;
}

/**
 * qla_edif_app_getstats - app would like to read various statistics info
 * @vha: host adapter pointer
 * @bsg_job: user request
 */
static int32_t
qla_edif_app_getstats(scsi_qla_host_t *vha, struct bsg_job *bsg_job)
{
	int32_t			rval = 0;
	struct fc_bsg_reply	*bsg_reply = bsg_job->reply;
	uint32_t size;

	struct app_sinfo_req	app_req;
	struct app_stats_reply	*app_reply;
	uint32_t pcnt = 0;

	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
	    bsg_job->request_payload.sg_cnt, &app_req,
	    sizeof(struct app_sinfo_req));
	if (app_req.num_ports == 0) {
		ql_dbg(ql_dbg_async, vha, 0x911d,
		   "%s app did not indicate number of ports to return\n",
		    __func__);
		SET_DID_STATUS(bsg_reply->result, DID_ERROR);
		rval = -1;
	}

	size = sizeof(struct app_stats_reply) +
	    (sizeof(struct app_sinfo) * app_req.num_ports);

	app_reply = kzalloc(size, GFP_KERNEL);
	if (!app_reply) {
		SET_DID_STATUS(bsg_reply->result, DID_ERROR);
		rval = -1;
	} else {
		struct fc_port	*fcport = NULL, *tf;

		app_reply->version = EDIF_VERSION1;

		list_for_each_entry_safe(fcport, tf, &vha->vp_fcports, list) {
			if (fcport->edif.enable) {
				if (pcnt >= app_req.num_ports)
					break;

				app_reply->elem[pcnt].rekey_count =
				    fcport->edif.rekey_cnt;
				app_reply->elem[pcnt].tx_bytes =
				    fcport->edif.tx_bytes;
				app_reply->elem[pcnt].rx_bytes =
				    fcport->edif.rx_bytes;

				memcpy(app_reply->elem[pcnt].remote_wwpn,
				    fcport->port_name, 8);

				pcnt++;
			}
		}
		app_reply->elem_count = pcnt;
		SET_DID_STATUS(bsg_reply->result, DID_OK);
	}

	bsg_job->reply_len = sizeof(struct fc_bsg_reply);
	bsg_reply->reply_payload_rcv_len =
	    sg_copy_from_buffer(bsg_job->reply_payload.sg_list,
	       bsg_job->reply_payload.sg_cnt, app_reply,
	       sizeof(struct app_stats_reply) + (sizeof(struct app_sinfo) * pcnt));

	kfree(app_reply);

	return rval;
}

static int32_t
qla_edif_ack(scsi_qla_host_t *vha, struct bsg_job *bsg_job)
{
	struct fc_port *fcport;
	struct aen_complete_cmd ack;
	struct fc_bsg_reply     *bsg_reply = bsg_job->reply;

	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
			  bsg_job->request_payload.sg_cnt, &ack, sizeof(ack));

	ql_dbg(ql_dbg_edif, vha, 0x70cf,
	       "%s: %06x event_code %x\n",
	       __func__, ack.port_id.b24, ack.event_code);

	fcport = qla2x00_find_fcport_by_pid(vha, &ack.port_id);
	SET_DID_STATUS(bsg_reply->result, DID_OK);

	if (!fcport) {
		ql_dbg(ql_dbg_edif, vha, 0x70cf,
		       "%s: unable to find fcport %06x \n",
		       __func__, ack.port_id.b24);
		return 0;
	}

	switch (ack.event_code) {
	case VND_CMD_AUTH_STATE_SESSION_SHUTDOWN:
		fcport->edif.sess_down_acked = 1;
		break;
	default:
		break;
	}
	return 0;
}

static int qla_edif_consume_dbell(scsi_qla_host_t *vha, struct bsg_job *bsg_job)
{
	struct fc_bsg_reply	*bsg_reply = bsg_job->reply;
	u32 sg_skip, reply_payload_len;
	bool keep;
	struct edb_node *dbnode = NULL;
	struct edif_app_dbell ap;
	int dat_size = 0;

	sg_skip = 0;
	reply_payload_len = bsg_job->reply_payload.payload_len;

	while ((reply_payload_len - sg_skip) >= sizeof(struct edb_node)) {
		dbnode = qla_edb_getnext(vha);
		if (dbnode) {
			keep = true;
			dat_size = 0;
			ap.event_code = dbnode->ntype;
			switch (dbnode->ntype) {
			case VND_CMD_AUTH_STATE_SESSION_SHUTDOWN:
			case VND_CMD_AUTH_STATE_NEEDED:
				ap.port_id = dbnode->u.plogi_did;
				dat_size += sizeof(ap.port_id);
				break;
			case VND_CMD_AUTH_STATE_ELS_RCVD:
				ap.port_id = dbnode->u.els_sid;
				dat_size += sizeof(ap.port_id);
				break;
			case VND_CMD_AUTH_STATE_SAUPDATE_COMPL:
				ap.port_id = dbnode->u.sa_aen.port_id;
				memcpy(&ap.event_data, &dbnode->u,
				    sizeof(struct edif_sa_update_aen));
				dat_size += sizeof(struct edif_sa_update_aen);
				break;
			default:
				keep = false;
				ql_log(ql_log_warn, vha, 0x09102,
					"%s unknown DB type=%d %p\n",
					__func__, dbnode->ntype, dbnode);
				break;
			}
			ap.event_data_size = dat_size;
			/* 8 = sizeof(ap.event_code + ap.event_data_size) */
			dat_size += 8;
			if (keep)
				sg_skip += sg_copy_buffer(bsg_job->reply_payload.sg_list,
						bsg_job->reply_payload.sg_cnt,
						&ap, dat_size, sg_skip, false);

			ql_dbg(ql_dbg_edif, vha, 0x09102,
				"%s Doorbell consumed : type=%d %p\n",
				__func__, dbnode->ntype, dbnode);

			kfree(dbnode);
		} else {
			break;
		}
	}

	SET_DID_STATUS(bsg_reply->result, DID_OK);
	bsg_reply->reply_payload_rcv_len = sg_skip;
	bsg_job->reply_len = sizeof(struct fc_bsg_reply);

	return 0;
}

static void __qla_edif_dbell_bsg_done(scsi_qla_host_t *vha, struct bsg_job *bsg_job,
	u32 delay)
{
	struct fc_bsg_reply *bsg_reply = bsg_job->reply;

	/* small sleep for doorbell events to accumulate */
	if (delay)
		msleep(delay);

	qla_edif_consume_dbell(vha, bsg_job);

	bsg_job_done(bsg_job, bsg_reply->result, bsg_reply->reply_payload_rcv_len);
}

static void qla_edif_dbell_bsg_done(scsi_qla_host_t *vha)
{
	unsigned long flags;
	struct bsg_job *prev_bsg_job = NULL;

	spin_lock_irqsave(&vha->e_dbell.db_lock, flags);
	if (vha->e_dbell.dbell_bsg_job) {
		prev_bsg_job = vha->e_dbell.dbell_bsg_job;
		vha->e_dbell.dbell_bsg_job = NULL;
	}
	spin_unlock_irqrestore(&vha->e_dbell.db_lock, flags);

	if (prev_bsg_job)
		__qla_edif_dbell_bsg_done(vha, prev_bsg_job, 0);
}

static int
qla_edif_dbell_bsg(scsi_qla_host_t *vha, struct bsg_job *bsg_job)
{
	unsigned long flags;
	bool return_bsg = false;

	/* flush previous dbell bsg */
	qla_edif_dbell_bsg_done(vha);

	spin_lock_irqsave(&vha->e_dbell.db_lock, flags);
	if (list_empty(&vha->e_dbell.head) && DBELL_ACTIVE(vha)) {
		/*
		 * when the next db event happens, bsg_job will return.
		 * Otherwise, timer will return it.
		 */
		vha->e_dbell.dbell_bsg_job = bsg_job;
		vha->e_dbell.bsg_expire = jiffies + 10 * HZ;
	} else {
		return_bsg = true;
	}
	spin_unlock_irqrestore(&vha->e_dbell.db_lock, flags);

	if (return_bsg)
		__qla_edif_dbell_bsg_done(vha, bsg_job, 1);

	return 0;
}

int32_t
qla_edif_app_mgmt(struct bsg_job *bsg_job)
{
	struct fc_bsg_request	*bsg_request = bsg_job->request;
	struct fc_bsg_reply	*bsg_reply = bsg_job->reply;
	struct Scsi_Host *host = fc_bsg_to_shost(bsg_job);
	scsi_qla_host_t		*vha = shost_priv(host);
	struct app_id		appcheck;
	bool done = true;
	int32_t         rval = 0;
	uint32_t	vnd_sc = bsg_request->rqst_data.h_vendor.vendor_cmd[1];
	u32 level = ql_dbg_edif;

	/* doorbell is high traffic */
	if (vnd_sc == QL_VND_SC_READ_DBELL)
		level = 0;

	ql_dbg(level, vha, 0x911d, "%s vnd subcmd=%x\n",
	    __func__, vnd_sc);

	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
	    bsg_job->request_payload.sg_cnt, &appcheck,
	    sizeof(struct app_id));

	if (!vha->hw->flags.edif_enabled ||
		test_bit(VPORT_DELETE, &vha->dpc_flags)) {
		ql_dbg(level, vha, 0x911d,
		    "%s edif not enabled or vp delete. bsg ptr done %p. dpc_flags %lx\n",
		    __func__, bsg_job, vha->dpc_flags);

		SET_DID_STATUS(bsg_reply->result, DID_ERROR);
		goto done;
	}

	if (!qla_edif_app_check(vha, appcheck)) {
		ql_dbg(level, vha, 0x911d,
		    "%s app checked failed.\n",
		    __func__);

		bsg_job->reply_len = sizeof(struct fc_bsg_reply);
		SET_DID_STATUS(bsg_reply->result, DID_ERROR);
		goto done;
	}

	switch (vnd_sc) {
	case QL_VND_SC_SA_UPDATE:
		done = false;
		rval = qla24xx_sadb_update(bsg_job);
		break;
	case QL_VND_SC_APP_START:
		rval = qla_edif_app_start(vha, bsg_job);
		break;
	case QL_VND_SC_APP_STOP:
		rval = qla_edif_app_stop(vha, bsg_job);
		break;
	case QL_VND_SC_AUTH_OK:
		rval = qla_edif_app_authok(vha, bsg_job);
		break;
	case QL_VND_SC_AUTH_FAIL:
		rval = qla_edif_app_authfail(vha, bsg_job);
		break;
	case QL_VND_SC_GET_FCINFO:
		rval = qla_edif_app_getfcinfo(vha, bsg_job);
		break;
	case QL_VND_SC_GET_STATS:
		rval = qla_edif_app_getstats(vha, bsg_job);
		break;
	case QL_VND_SC_AEN_COMPLETE:
		rval = qla_edif_ack(vha, bsg_job);
		break;
	case QL_VND_SC_READ_DBELL:
		rval = qla_edif_dbell_bsg(vha, bsg_job);
		done = false;
		break;
	default:
		ql_dbg(ql_dbg_edif, vha, 0x911d, "%s unknown cmd=%x\n",
		    __func__,
		    bsg_request->rqst_data.h_vendor.vendor_cmd[1]);
		rval = EXT_STATUS_INVALID_PARAM;
		done = false;
		break;
	}

done:
	if (done) {
		ql_dbg(level, vha, 0x7009,
		    "%s: %d  bsg ptr done %p\n", __func__, __LINE__, bsg_job);
		bsg_job_done(bsg_job, bsg_reply->result,
		    bsg_reply->reply_payload_rcv_len);
	}

	return rval;
}

static struct edif_sa_ctl *
qla_edif_add_sa_ctl(fc_port_t *fcport, struct qla_sa_update_frame *sa_frame,
	int dir)
{
	struct	edif_sa_ctl *sa_ctl;
	struct qla_sa_update_frame *sap;
	int	index = sa_frame->fast_sa_index;
	unsigned long flags = 0;

	sa_ctl = kzalloc(sizeof(*sa_ctl), GFP_KERNEL);
	if (!sa_ctl) {
		/* couldn't get space */
		ql_dbg(ql_dbg_edif, fcport->vha, 0x9100,
		    "unable to allocate SA CTL\n");
		return NULL;
	}

	/*
	 * need to allocate sa_index here and save it
	 * in both sa_ctl->index and sa_frame->fast_sa_index;
	 * If alloc fails then delete sa_ctl and return NULL
	 */
	INIT_LIST_HEAD(&sa_ctl->next);
	sap = &sa_ctl->sa_frame;
	*sap = *sa_frame;
	sa_ctl->index = index;
	sa_ctl->fcport = fcport;
	sa_ctl->flags = 0;
	sa_ctl->state = 0L;
	ql_dbg(ql_dbg_edif, fcport->vha, 0x9100,
	    "%s: Added sa_ctl %p, index %d, state 0x%lx\n",
	    __func__, sa_ctl, sa_ctl->index, sa_ctl->state);
	spin_lock_irqsave(&fcport->edif.sa_list_lock, flags);
	if (dir == SAU_FLG_TX)
		list_add_tail(&sa_ctl->next, &fcport->edif.tx_sa_list);
	else
		list_add_tail(&sa_ctl->next, &fcport->edif.rx_sa_list);
	spin_unlock_irqrestore(&fcport->edif.sa_list_lock, flags);

	return sa_ctl;
}

void
qla_edif_flush_sa_ctl_lists(fc_port_t *fcport)
{
	struct edif_sa_ctl *sa_ctl, *tsa_ctl;
	unsigned long flags = 0;

	spin_lock_irqsave(&fcport->edif.sa_list_lock, flags);

	list_for_each_entry_safe(sa_ctl, tsa_ctl, &fcport->edif.tx_sa_list,
	    next) {
		list_del(&sa_ctl->next);
		kfree(sa_ctl);
	}

	list_for_each_entry_safe(sa_ctl, tsa_ctl, &fcport->edif.rx_sa_list,
	    next) {
		list_del(&sa_ctl->next);
		kfree(sa_ctl);
	}

	spin_unlock_irqrestore(&fcport->edif.sa_list_lock, flags);
}

struct edif_sa_ctl *
qla_edif_find_sa_ctl_by_index(fc_port_t *fcport, int index, int dir)
{
	struct edif_sa_ctl *sa_ctl, *tsa_ctl;
	struct list_head *sa_list;

	if (dir == SAU_FLG_TX)
		sa_list = &fcport->edif.tx_sa_list;
	else
		sa_list = &fcport->edif.rx_sa_list;

	list_for_each_entry_safe(sa_ctl, tsa_ctl, sa_list, next) {
		if (test_bit(EDIF_SA_CTL_USED, &sa_ctl->state) &&
		    sa_ctl->index == index)
			return sa_ctl;
	}
	return NULL;
}

/* add the sa to the correct list */
static int
qla24xx_check_sadb_avail_slot(struct bsg_job *bsg_job, fc_port_t *fcport,
	struct qla_sa_update_frame *sa_frame)
{
	struct edif_sa_ctl *sa_ctl = NULL;
	int dir;
	uint16_t sa_index;

	dir = (sa_frame->flags & SAU_FLG_TX);

	/* map the spi to an sa_index */
	sa_index = qla_edif_sadb_get_sa_index(fcport, sa_frame);
	if (sa_index == RX_DELETE_NO_EDIF_SA_INDEX) {
		/* process rx delete */
		ql_dbg(ql_dbg_edif, fcport->vha, 0x3063,
		    "%s: rx delete for lid 0x%x, spi 0x%x, no entry found\n",
		    __func__, fcport->loop_id, sa_frame->spi);

		/* build and send the aen */
		fcport->edif.rx_sa_set = 1;
		fcport->edif.rx_sa_pending = 0;
		qla_edb_eventcreate(fcport->vha,
		    VND_CMD_AUTH_STATE_SAUPDATE_COMPL,
		    QL_VND_SA_STAT_SUCCESS,
		    QL_VND_RX_SA_KEY, fcport);

		/* force a return of good bsg status; */
		return RX_DELETE_NO_EDIF_SA_INDEX;
	} else if (sa_index == INVALID_EDIF_SA_INDEX) {
		ql_dbg(ql_dbg_edif, fcport->vha, 0x9100,
		    "%s: Failed to get sa_index for spi 0x%x, dir: %d\n",
		    __func__, sa_frame->spi, dir);
		return INVALID_EDIF_SA_INDEX;
	}

	ql_dbg(ql_dbg_edif, fcport->vha, 0x9100,
	    "%s: index %d allocated to spi 0x%x, dir: %d, nport_handle: 0x%x\n",
	    __func__, sa_index, sa_frame->spi, dir, fcport->loop_id);

	/* This is a local copy of sa_frame. */
	sa_frame->fast_sa_index = sa_index;
	/* create the sa_ctl */
	sa_ctl = qla_edif_add_sa_ctl(fcport, sa_frame, dir);
	if (!sa_ctl) {
		ql_dbg(ql_dbg_edif, fcport->vha, 0x9100,
		    "%s: Failed to add sa_ctl for spi 0x%x, dir: %d, sa_index: %d\n",
		    __func__, sa_frame->spi, dir, sa_index);
		return -1;
	}

	set_bit(EDIF_SA_CTL_USED, &sa_ctl->state);

	if (dir == SAU_FLG_TX)
		fcport->edif.tx_rekey_cnt++;
	else
		fcport->edif.rx_rekey_cnt++;

	ql_dbg(ql_dbg_edif, fcport->vha, 0x9100,
	    "%s: Found sa_ctl %p, index %d, state 0x%lx, tx_cnt %d, rx_cnt %d, nport_handle: 0x%x\n",
	    __func__, sa_ctl, sa_ctl->index, sa_ctl->state,
	    fcport->edif.tx_rekey_cnt,
	    fcport->edif.rx_rekey_cnt, fcport->loop_id);

	return 0;
}

#define QLA_SA_UPDATE_FLAGS_RX_KEY      0x0
#define QLA_SA_UPDATE_FLAGS_TX_KEY      0x2
#define EDIF_MSLEEP_INTERVAL 100
#define EDIF_RETRY_COUNT  50

int
qla24xx_sadb_update(struct bsg_job *bsg_job)
{
	struct	fc_bsg_reply	*bsg_reply = bsg_job->reply;
	struct Scsi_Host *host = fc_bsg_to_shost(bsg_job);
	scsi_qla_host_t *vha = shost_priv(host);
	fc_port_t		*fcport = NULL;
	srb_t			*sp = NULL;
	struct edif_list_entry *edif_entry = NULL;
	int			found = 0;
	int			rval = 0;
	int result = 0, cnt;
	struct qla_sa_update_frame sa_frame;
	struct srb_iocb *iocb_cmd;
	port_id_t portid;

	ql_dbg(ql_dbg_edif + ql_dbg_verbose, vha, 0x911d,
	    "%s entered, vha: 0x%p\n", __func__, vha);

	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
	    bsg_job->request_payload.sg_cnt, &sa_frame,
	    sizeof(struct qla_sa_update_frame));

	/* Check if host is online */
	if (!vha->flags.online) {
		ql_log(ql_log_warn, vha, 0x70a1, "Host is not online\n");
		rval = -EIO;
		SET_DID_STATUS(bsg_reply->result, DID_ERROR);
		goto done;
	}

	if (DBELL_INACTIVE(vha)) {
		ql_log(ql_log_warn, vha, 0x70a1, "App not started\n");
		rval = -EIO;
		SET_DID_STATUS(bsg_reply->result, DID_ERROR);
		goto done;
	}

	/* silent unaligned access warning */
	portid.b.domain = sa_frame.port_id.b.domain;
	portid.b.area   = sa_frame.port_id.b.area;
	portid.b.al_pa  = sa_frame.port_id.b.al_pa;

	fcport = qla2x00_find_fcport_by_pid(vha, &portid);
	if (fcport) {
		found = 1;
		if (sa_frame.flags == QLA_SA_UPDATE_FLAGS_TX_KEY)
			fcport->edif.tx_bytes = 0;
		if (sa_frame.flags == QLA_SA_UPDATE_FLAGS_RX_KEY)
			fcport->edif.rx_bytes = 0;
	}

	if (!found) {
		ql_dbg(ql_dbg_edif, vha, 0x70a3, "Failed to find port= %06x\n",
		    sa_frame.port_id.b24);
		rval = -EINVAL;
		SET_DID_STATUS(bsg_reply->result, DID_NO_CONNECT);
		goto done;
	}

	/* make sure the nport_handle is valid */
	if (fcport->loop_id == FC_NO_LOOP_ID) {
		ql_dbg(ql_dbg_edif, vha, 0x70e1,
		    "%s: %8phN lid=FC_NO_LOOP_ID, spi: 0x%x, DS %d, returning NO_CONNECT\n",
		    __func__, fcport->port_name, sa_frame.spi,
		    fcport->disc_state);
		rval = -EINVAL;
		SET_DID_STATUS(bsg_reply->result, DID_NO_CONNECT);
		goto done;
	}

	/* allocate and queue an sa_ctl */
	result = qla24xx_check_sadb_avail_slot(bsg_job, fcport, &sa_frame);

	/* failure of bsg */
	if (result == INVALID_EDIF_SA_INDEX) {
		ql_dbg(ql_dbg_edif, vha, 0x70e1,
		    "%s: %8phN, skipping update.\n",
		    __func__, fcport->port_name);
		rval = -EINVAL;
		SET_DID_STATUS(bsg_reply->result, DID_ERROR);
		goto done;

	/* rx delete failure */
	} else if (result == RX_DELETE_NO_EDIF_SA_INDEX) {
		ql_dbg(ql_dbg_edif, vha, 0x70e1,
		    "%s: %8phN, skipping rx delete.\n",
		    __func__, fcport->port_name);
		SET_DID_STATUS(bsg_reply->result, DID_OK);
		goto done;
	}

	ql_dbg(ql_dbg_edif, vha, 0x70e1,
	    "%s: %8phN, sa_index in sa_frame: %d flags %xh\n",
	    __func__, fcport->port_name, sa_frame.fast_sa_index,
	    sa_frame.flags);

	/* looking for rx index and delete */
	if (((sa_frame.flags & SAU_FLG_TX) == 0) &&
	    (sa_frame.flags & SAU_FLG_INV)) {
		uint16_t nport_handle = fcport->loop_id;
		uint16_t sa_index = sa_frame.fast_sa_index;

		/*
		 * make sure we have an existing rx key, otherwise just process
		 * this as a straight delete just like TX
		 * This is NOT a normal case, it indicates an error recovery or key cleanup
		 * by the ipsec code above us.
		 */
		edif_entry = qla_edif_list_find_sa_index(fcport, fcport->loop_id);
		if (!edif_entry) {
			ql_dbg(ql_dbg_edif, vha, 0x911d,
			    "%s: WARNING: no active sa_index for nport_handle 0x%x, forcing delete for sa_index 0x%x\n",
			    __func__, fcport->loop_id, sa_index);
			goto force_rx_delete;
		}

		/*
		 * if we have a forced delete for rx, remove the sa_index from the edif list
		 * and proceed with normal delete.  The rx delay timer should not be running
		 */
		if ((sa_frame.flags & SAU_FLG_FORCE_DELETE) == SAU_FLG_FORCE_DELETE) {
			qla_edif_list_delete_sa_index(fcport, edif_entry);
			ql_dbg(ql_dbg_edif, vha, 0x911d,
			    "%s: FORCE DELETE flag found for nport_handle 0x%x, sa_index 0x%x, forcing DELETE\n",
			    __func__, fcport->loop_id, sa_index);
			kfree(edif_entry);
			goto force_rx_delete;
		}

		/*
		 * delayed rx delete
		 *
		 * if delete_sa_index is not invalid then there is already
		 * a delayed index in progress, return bsg bad status
		 */
		if (edif_entry->delete_sa_index != INVALID_EDIF_SA_INDEX) {
			struct edif_sa_ctl *sa_ctl;

			ql_dbg(ql_dbg_edif, vha, 0x911d,
			    "%s: delete for lid 0x%x, delete_sa_index %d is pending\n",
			    __func__, edif_entry->handle, edif_entry->delete_sa_index);

			/* free up the sa_ctl that was allocated with the sa_index */
			sa_ctl = qla_edif_find_sa_ctl_by_index(fcport, sa_index,
			    (sa_frame.flags & SAU_FLG_TX));
			if (sa_ctl) {
				ql_dbg(ql_dbg_edif, vha, 0x3063,
				    "%s: freeing sa_ctl for index %d\n",
				    __func__, sa_ctl->index);
				qla_edif_free_sa_ctl(fcport, sa_ctl, sa_ctl->index);
			}

			/* release the sa_index */
			ql_dbg(ql_dbg_edif, vha, 0x3063,
			    "%s: freeing sa_index %d, nph: 0x%x\n",
			    __func__, sa_index, nport_handle);
			qla_edif_sadb_delete_sa_index(fcport, nport_handle, sa_index);

			rval = -EINVAL;
			SET_DID_STATUS(bsg_reply->result, DID_ERROR);
			goto done;
		}

		fcport->edif.rekey_cnt++;

		/* configure and start the rx delay timer */
		edif_entry->fcport = fcport;
		edif_entry->timer.expires = jiffies + RX_DELAY_DELETE_TIMEOUT * HZ;

		ql_dbg(ql_dbg_edif, vha, 0x911d,
		    "%s: adding timer, entry: %p, delete sa_index %d, lid 0x%x to edif_list\n",
		    __func__, edif_entry, sa_index, nport_handle);

		/*
		 * Start the timer when we queue the delayed rx delete.
		 * This is an activity timer that goes off if we have not
		 * received packets with the new sa_index
		 */
		add_timer(&edif_entry->timer);

		/*
		 * sa_delete for rx key with an active rx key including this one
		 * add the delete rx sa index to the hash so we can look for it
		 * in the rsp queue.  Do this after making any changes to the
		 * edif_entry as part of the rx delete.
		 */

		ql_dbg(ql_dbg_edif, vha, 0x911d,
		    "%s: delete sa_index %d, lid 0x%x to edif_list. bsg done ptr %p\n",
		    __func__, sa_index, nport_handle, bsg_job);

		edif_entry->delete_sa_index = sa_index;

		bsg_job->reply_len = sizeof(struct fc_bsg_reply);
		bsg_reply->result = DID_OK << 16;

		goto done;

	/*
	 * rx index and update
	 * add the index to the list and continue with normal update
	 */
	} else if (((sa_frame.flags & SAU_FLG_TX) == 0) &&
	    ((sa_frame.flags & SAU_FLG_INV) == 0)) {
		/* sa_update for rx key */
		uint32_t nport_handle = fcport->loop_id;
		uint16_t sa_index = sa_frame.fast_sa_index;
		int result;

		/*
		 * add the update rx sa index to the hash so we can look for it
		 * in the rsp queue and continue normally
		 */

		ql_dbg(ql_dbg_edif, vha, 0x911d,
		    "%s:  adding update sa_index %d, lid 0x%x to edif_list\n",
		    __func__, sa_index, nport_handle);

		result = qla_edif_list_add_sa_update_index(fcport, sa_index,
		    nport_handle);
		if (result) {
			ql_dbg(ql_dbg_edif, vha, 0x911d,
			    "%s: SA_UPDATE failed to add new sa index %d to list for lid 0x%x\n",
			    __func__, sa_index, nport_handle);
		}
	}
	if (sa_frame.flags & SAU_FLG_GMAC_MODE)
		fcport->edif.aes_gmac = 1;
	else
		fcport->edif.aes_gmac = 0;

force_rx_delete:
	/*
	 * sa_update for both rx and tx keys, sa_delete for tx key
	 * immediately process the request
	 */
	sp = qla2x00_get_sp(vha, fcport, GFP_KERNEL);
	if (!sp) {
		rval = -ENOMEM;
		SET_DID_STATUS(bsg_reply->result, DID_IMM_RETRY);
		goto done;
	}

	sp->type = SRB_SA_UPDATE;
	sp->name = "bsg_sa_update";
	sp->u.bsg_job = bsg_job;
	/* sp->free = qla2x00_bsg_sp_free; */
	sp->free = qla2x00_rel_sp;
	sp->done = qla2x00_bsg_job_done;
	iocb_cmd = &sp->u.iocb_cmd;
	iocb_cmd->u.sa_update.sa_frame  = sa_frame;
	cnt = 0;
retry:
	rval = qla2x00_start_sp(sp);
	switch (rval) {
	case QLA_SUCCESS:
		break;
	case -EAGAIN:
		msleep(EDIF_MSLEEP_INTERVAL);
		cnt++;
		if (cnt < EDIF_RETRY_COUNT)
			goto retry;

		fallthrough;
	default:
		ql_log(ql_dbg_edif, vha, 0x70e3,
		       "%s qla2x00_start_sp failed=%d.\n",
		       __func__, rval);

		qla2x00_rel_sp(sp);
		rval = -EIO;
		SET_DID_STATUS(bsg_reply->result, DID_IMM_RETRY);
		goto done;
	}

	ql_dbg(ql_dbg_edif, vha, 0x911d,
	    "%s:  %s sent, hdl=%x, portid=%06x.\n",
	    __func__, sp->name, sp->handle, fcport->d_id.b24);

	fcport->edif.rekey_cnt++;
	bsg_job->reply_len = sizeof(struct fc_bsg_reply);
	SET_DID_STATUS(bsg_reply->result, DID_OK);

	return 0;

/*
 * send back error status
 */
done:
	bsg_job->reply_len = sizeof(struct fc_bsg_reply);
	ql_dbg(ql_dbg_edif, vha, 0x911d,
	    "%s:status: FAIL, result: 0x%x, bsg ptr done %p\n",
	    __func__, bsg_reply->result, bsg_job);
	bsg_job_done(bsg_job, bsg_reply->result,
	    bsg_reply->reply_payload_rcv_len);

	return 0;
}

static void
qla_enode_free(scsi_qla_host_t *vha, struct enode *node)
{
	node->ntype = N_UNDEF;
	kfree(node);
}

/**
 * qla_enode_init - initialize enode structs & lock
 * @vha: host adapter pointer
 *
 * should only be called when driver attaching
 */
void
qla_enode_init(scsi_qla_host_t *vha)
{
	struct	qla_hw_data *ha = vha->hw;
	char	name[32];

	if (vha->pur_cinfo.enode_flags == ENODE_ACTIVE) {
		/* list still active - error */
		ql_dbg(ql_dbg_edif, vha, 0x09102, "%s enode still active\n",
		    __func__);
		return;
	}

	/* initialize lock which protects pur_core & init list */
	spin_lock_init(&vha->pur_cinfo.pur_lock);
	INIT_LIST_HEAD(&vha->pur_cinfo.head);

	snprintf(name, sizeof(name), "%s_%d_purex", QLA2XXX_DRIVER_NAME,
	    ha->pdev->device);
}

/**
 * qla_enode_stop - stop and clear and enode data
 * @vha: host adapter pointer
 *
 * called when app notified it is exiting
 */
void
qla_enode_stop(scsi_qla_host_t *vha)
{
	unsigned long flags;
	struct enode *node, *q;

	if (vha->pur_cinfo.enode_flags != ENODE_ACTIVE) {
		/* doorbell list not enabled */
		ql_dbg(ql_dbg_edif, vha, 0x09102,
		    "%s enode not active\n", __func__);
		return;
	}

	/* grab lock so list doesn't move */
	spin_lock_irqsave(&vha->pur_cinfo.pur_lock, flags);

	vha->pur_cinfo.enode_flags &= ~ENODE_ACTIVE; /* mark it not active */

	/* hopefully this is a null list at this point */
	list_for_each_entry_safe(node, q, &vha->pur_cinfo.head, list) {
		ql_dbg(ql_dbg_edif, vha, 0x910f,
		    "%s freeing enode type=%x, cnt=%x\n", __func__, node->ntype,
		    node->dinfo.nodecnt);
		list_del_init(&node->list);
		qla_enode_free(vha, node);
	}
	spin_unlock_irqrestore(&vha->pur_cinfo.pur_lock, flags);
}

static void qla_enode_clear(scsi_qla_host_t *vha, port_id_t portid)
{
	unsigned    long flags;
	struct enode    *e, *tmp;
	struct purexevent   *purex;
	LIST_HEAD(enode_list);

	if (vha->pur_cinfo.enode_flags != ENODE_ACTIVE) {
		ql_dbg(ql_dbg_edif, vha, 0x09102,
		       "%s enode not active\n", __func__);
		return;
	}
	spin_lock_irqsave(&vha->pur_cinfo.pur_lock, flags);
	list_for_each_entry_safe(e, tmp, &vha->pur_cinfo.head, list) {
		purex = &e->u.purexinfo;
		if (purex->pur_info.pur_sid.b24 == portid.b24) {
			ql_dbg(ql_dbg_edif, vha, 0x911d,
			    "%s free ELS sid=%06x. xchg %x, nb=%xh\n",
			    __func__, portid.b24,
			    purex->pur_info.pur_rx_xchg_address,
			    purex->pur_info.pur_bytes_rcvd);

			list_del_init(&e->list);
			list_add_tail(&e->list, &enode_list);
		}
	}
	spin_unlock_irqrestore(&vha->pur_cinfo.pur_lock, flags);

	list_for_each_entry_safe(e, tmp, &enode_list, list) {
		list_del_init(&e->list);
		qla_enode_free(vha, e);
	}
}

/*
 *  allocate enode struct and populate buffer
 *  returns: enode pointer with buffers
 *           NULL on error
 */
static struct enode *
qla_enode_alloc(scsi_qla_host_t *vha, uint32_t ntype)
{
	struct enode		*node;
	struct purexevent	*purex;

	node = kzalloc(RX_ELS_SIZE, GFP_ATOMIC);
	if (!node)
		return NULL;

	purex = &node->u.purexinfo;
	purex->msgp = (u8 *)(node + 1);
	purex->msgp_len = ELS_MAX_PAYLOAD;

	node->ntype = ntype;
	INIT_LIST_HEAD(&node->list);
	return node;
}

static void
qla_enode_add(scsi_qla_host_t *vha, struct enode *ptr)
{
	unsigned long flags;

	ql_dbg(ql_dbg_edif + ql_dbg_verbose, vha, 0x9109,
	    "%s add enode for type=%x, cnt=%x\n",
	    __func__, ptr->ntype, ptr->dinfo.nodecnt);

	spin_lock_irqsave(&vha->pur_cinfo.pur_lock, flags);
	list_add_tail(&ptr->list, &vha->pur_cinfo.head);
	spin_unlock_irqrestore(&vha->pur_cinfo.pur_lock, flags);

	return;
}

static struct enode *
qla_enode_find(scsi_qla_host_t *vha, uint32_t ntype, uint32_t p1, uint32_t p2)
{
	struct enode		*node_rtn = NULL;
	struct enode		*list_node, *q;
	unsigned long		flags;
	uint32_t		sid;
	struct purexevent	*purex;

	/* secure the list from moving under us */
	spin_lock_irqsave(&vha->pur_cinfo.pur_lock, flags);

	list_for_each_entry_safe(list_node, q, &vha->pur_cinfo.head, list) {

		/* node type determines what p1 and p2 are */
		purex = &list_node->u.purexinfo;
		sid = p1;

		if (purex->pur_info.pur_sid.b24 == sid) {
			/* found it and its complete */
			node_rtn = list_node;
			list_del(&list_node->list);
			break;
		}
	}

	spin_unlock_irqrestore(&vha->pur_cinfo.pur_lock, flags);

	return node_rtn;
}

/**
 * qla_pur_get_pending - read/return authentication message sent
 *  from remote port
 * @vha: host adapter pointer
 * @fcport: session pointer
 * @bsg_job: user request where the message is copy to.
 */
static int
qla_pur_get_pending(scsi_qla_host_t *vha, fc_port_t *fcport,
	struct bsg_job *bsg_job)
{
	struct enode		*ptr;
	struct purexevent	*purex;
	struct qla_bsg_auth_els_reply *rpl =
	    (struct qla_bsg_auth_els_reply *)bsg_job->reply;

	bsg_job->reply_len = sizeof(*rpl);

	ptr = qla_enode_find(vha, N_PUREX, fcport->d_id.b24, PUR_GET);
	if (!ptr) {
		ql_dbg(ql_dbg_edif, vha, 0x9111,
		    "%s no enode data found for %8phN sid=%06x\n",
		    __func__, fcport->port_name, fcport->d_id.b24);
		SET_DID_STATUS(rpl->r.result, DID_IMM_RETRY);
		return -EIO;
	}

	/*
	 * enode is now off the linked list and is ours to deal with
	 */
	purex = &ptr->u.purexinfo;

	/* Copy info back to caller */
	rpl->rx_xchg_address = purex->pur_info.pur_rx_xchg_address;

	SET_DID_STATUS(rpl->r.result, DID_OK);
	rpl->r.reply_payload_rcv_len =
	    sg_pcopy_from_buffer(bsg_job->reply_payload.sg_list,
		bsg_job->reply_payload.sg_cnt, purex->msgp,
		purex->pur_info.pur_bytes_rcvd, 0);

	/* data copy / passback completed - destroy enode */
	qla_enode_free(vha, ptr);

	return 0;
}

/* it is assume qpair lock is held */
static int
qla_els_reject_iocb(scsi_qla_host_t *vha, struct qla_qpair *qp,
	struct qla_els_pt_arg *a)
{
	struct els_entry_24xx *els_iocb;

	els_iocb = __qla2x00_alloc_iocbs(qp, NULL);
	if (!els_iocb) {
		ql_log(ql_log_warn, vha, 0x700c,
		    "qla2x00_alloc_iocbs failed.\n");
		return QLA_FUNCTION_FAILED;
	}

	qla_els_pt_iocb(vha, els_iocb, a);

	ql_dbg(ql_dbg_edif, vha, 0x0183,
	    "Sending ELS reject ox_id %04x s:%06x -> d:%06x\n",
	    a->ox_id, a->sid.b24, a->did.b24);
	ql_dump_buffer(ql_dbg_edif + ql_dbg_verbose, vha, 0x0185,
	    vha->hw->elsrej.c, sizeof(*vha->hw->elsrej.c));
	/* flush iocb to mem before notifying hw doorbell */
	wmb();
	qla2x00_start_iocbs(vha, qp->req);
	return 0;
}

void
qla_edb_init(scsi_qla_host_t *vha)
{
	if (DBELL_ACTIVE(vha)) {
		/* list already init'd - error */
		ql_dbg(ql_dbg_edif, vha, 0x09102,
		    "edif db already initialized, cannot reinit\n");
		return;
	}

	/* initialize lock which protects doorbell & init list */
	spin_lock_init(&vha->e_dbell.db_lock);
	INIT_LIST_HEAD(&vha->e_dbell.head);
}

static void qla_edb_clear(scsi_qla_host_t *vha, port_id_t portid)
{
	unsigned long flags;
	struct edb_node *e, *tmp;
	port_id_t sid;
	LIST_HEAD(edb_list);

	if (DBELL_INACTIVE(vha)) {
		/* doorbell list not enabled */
		ql_dbg(ql_dbg_edif, vha, 0x09102,
		       "%s doorbell not enabled\n", __func__);
		return;
	}

	/* grab lock so list doesn't move */
	spin_lock_irqsave(&vha->e_dbell.db_lock, flags);
	list_for_each_entry_safe(e, tmp, &vha->e_dbell.head, list) {
		switch (e->ntype) {
		case VND_CMD_AUTH_STATE_NEEDED:
		case VND_CMD_AUTH_STATE_SESSION_SHUTDOWN:
			sid = e->u.plogi_did;
			break;
		case VND_CMD_AUTH_STATE_ELS_RCVD:
			sid = e->u.els_sid;
			break;
		case VND_CMD_AUTH_STATE_SAUPDATE_COMPL:
			/* app wants to see this  */
			continue;
		default:
			ql_log(ql_log_warn, vha, 0x09102,
			       "%s unknown node type: %x\n", __func__, e->ntype);
			sid.b24 = 0;
			break;
		}
		if (sid.b24 == portid.b24) {
			ql_dbg(ql_dbg_edif, vha, 0x910f,
			       "%s free doorbell event : node type = %x %p\n",
			       __func__, e->ntype, e);
			list_del_init(&e->list);
			list_add_tail(&e->list, &edb_list);
		}
	}
	spin_unlock_irqrestore(&vha->e_dbell.db_lock, flags);

	list_for_each_entry_safe(e, tmp, &edb_list, list)
		qla_edb_node_free(vha, e);
}

/* function called when app is stopping */

void
qla_edb_stop(scsi_qla_host_t *vha)
{
	unsigned long flags;
	struct edb_node *node, *q;

	if (DBELL_INACTIVE(vha)) {
		/* doorbell list not enabled */
		ql_dbg(ql_dbg_edif, vha, 0x09102,
		    "%s doorbell not enabled\n", __func__);
		return;
	}

	/* grab lock so list doesn't move */
	spin_lock_irqsave(&vha->e_dbell.db_lock, flags);

	vha->e_dbell.db_flags &= ~EDB_ACTIVE; /* mark it not active */
	/* hopefully this is a null list at this point */
	list_for_each_entry_safe(node, q, &vha->e_dbell.head, list) {
		ql_dbg(ql_dbg_edif, vha, 0x910f,
		    "%s freeing edb_node type=%x\n",
		    __func__, node->ntype);
		qla_edb_node_free(vha, node);
	}
	spin_unlock_irqrestore(&vha->e_dbell.db_lock, flags);

	qla_edif_dbell_bsg_done(vha);
}

static struct edb_node *
qla_edb_node_alloc(scsi_qla_host_t *vha, uint32_t ntype)
{
	struct edb_node	*node;

	node = kzalloc(sizeof(*node), GFP_ATOMIC);
	if (!node) {
		/* couldn't get space */
		ql_dbg(ql_dbg_edif, vha, 0x9100,
		    "edb node unable to be allocated\n");
		return NULL;
	}

	node->ntype = ntype;
	INIT_LIST_HEAD(&node->list);
	return node;
}

/* adds a already allocated enode to the linked list */
static bool
qla_edb_node_add(scsi_qla_host_t *vha, struct edb_node *ptr)
{
	unsigned long		flags;

	if (DBELL_INACTIVE(vha)) {
		/* doorbell list not enabled */
		ql_dbg(ql_dbg_edif, vha, 0x09102,
		    "%s doorbell not enabled\n", __func__);
		return false;
	}

	spin_lock_irqsave(&vha->e_dbell.db_lock, flags);
	list_add_tail(&ptr->list, &vha->e_dbell.head);
	spin_unlock_irqrestore(&vha->e_dbell.db_lock, flags);

	return true;
}

/* adds event to doorbell list */
void
qla_edb_eventcreate(scsi_qla_host_t *vha, uint32_t dbtype,
	uint32_t data, uint32_t data2, fc_port_t	*sfcport)
{
	struct edb_node	*edbnode;
	fc_port_t *fcport = sfcport;
	port_id_t id;

	if (!vha->hw->flags.edif_enabled) {
		/* edif not enabled */
		return;
	}

	if (DBELL_INACTIVE(vha)) {
		if (fcport)
			fcport->edif.auth_state = dbtype;
		/* doorbell list not enabled */
		ql_dbg(ql_dbg_edif, vha, 0x09102,
		    "%s doorbell not enabled (type=%d\n", __func__, dbtype);
		return;
	}

	edbnode = qla_edb_node_alloc(vha, dbtype);
	if (!edbnode) {
		ql_dbg(ql_dbg_edif, vha, 0x09102,
		    "%s unable to alloc db node\n", __func__);
		return;
	}

	if (!fcport) {
		id.b.domain = (data >> 16) & 0xff;
		id.b.area = (data >> 8) & 0xff;
		id.b.al_pa = data & 0xff;
		ql_dbg(ql_dbg_edif, vha, 0x09222,
		    "%s: Arrived s_id: %06x\n", __func__,
		    id.b24);
		fcport = qla2x00_find_fcport_by_pid(vha, &id);
		if (!fcport) {
			ql_dbg(ql_dbg_edif, vha, 0x09102,
			    "%s can't find fcport for sid= 0x%x - ignoring\n",
			__func__, id.b24);
			kfree(edbnode);
			return;
		}
	}

	/* populate the edb node */
	switch (dbtype) {
	case VND_CMD_AUTH_STATE_NEEDED:
	case VND_CMD_AUTH_STATE_SESSION_SHUTDOWN:
		edbnode->u.plogi_did.b24 = fcport->d_id.b24;
		break;
	case VND_CMD_AUTH_STATE_ELS_RCVD:
		edbnode->u.els_sid.b24 = fcport->d_id.b24;
		break;
	case VND_CMD_AUTH_STATE_SAUPDATE_COMPL:
		edbnode->u.sa_aen.port_id = fcport->d_id;
		edbnode->u.sa_aen.status =  data;
		edbnode->u.sa_aen.key_type =  data2;
		edbnode->u.sa_aen.version = EDIF_VERSION1;
		break;
	default:
		ql_dbg(ql_dbg_edif, vha, 0x09102,
			"%s unknown type: %x\n", __func__, dbtype);
		kfree(edbnode);
		edbnode = NULL;
		break;
	}

	if (edbnode) {
		if (!qla_edb_node_add(vha, edbnode)) {
			ql_dbg(ql_dbg_edif, vha, 0x09102,
			    "%s unable to add dbnode\n", __func__);
			kfree(edbnode);
			return;
		}
		ql_dbg(ql_dbg_edif, vha, 0x09102,
		    "%s Doorbell produced : type=%d %p\n", __func__, dbtype, edbnode);
		qla_edif_dbell_bsg_done(vha);
		if (fcport)
			fcport->edif.auth_state = dbtype;
	}
}

void
qla_edif_timer(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;

	if (!vha->vp_idx && N2N_TOPO(ha) && ha->flags.n2n_fw_acc_sec) {
		if (DBELL_INACTIVE(vha) &&
		    ha->edif_post_stop_cnt_down) {
			ha->edif_post_stop_cnt_down--;

			/*
			 * turn off auto 'Plogi Acc + secure=1' feature
			 * Set Add FW option[3]
			 * BIT_15, if.
			 */
			if (ha->edif_post_stop_cnt_down == 0) {
				ql_dbg(ql_dbg_async, vha, 0x911d,
				       "%s chip reset to turn off PLOGI ACC + secure\n",
				       __func__);
				set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
			}
		} else {
			ha->edif_post_stop_cnt_down = 60;
		}
	}

	if (vha->e_dbell.dbell_bsg_job && time_after_eq(jiffies, vha->e_dbell.bsg_expire))
		qla_edif_dbell_bsg_done(vha);
}

static void qla_noop_sp_done(srb_t *sp, int res)
{
	sp->fcport->flags &= ~(FCF_ASYNC_SENT | FCF_ASYNC_ACTIVE);
	/* ref: INIT */
	kref_put(&sp->cmd_kref, qla2x00_sp_release);
}

/*
 * Called from work queue
 * build and send the sa_update iocb to delete an rx sa_index
 */
int
qla24xx_issue_sa_replace_iocb(scsi_qla_host_t *vha, struct qla_work_evt *e)
{
	srb_t *sp;
	fc_port_t	*fcport = NULL;
	struct srb_iocb *iocb_cmd = NULL;
	int rval = QLA_SUCCESS;
	struct	edif_sa_ctl *sa_ctl = e->u.sa_update.sa_ctl;
	uint16_t nport_handle = e->u.sa_update.nport_handle;

	ql_dbg(ql_dbg_edif, vha, 0x70e6,
	    "%s: starting,  sa_ctl: %p\n", __func__, sa_ctl);

	if (!sa_ctl) {
		ql_dbg(ql_dbg_edif, vha, 0x70e6,
		    "sa_ctl allocation failed\n");
		rval = -ENOMEM;
		return rval;
	}

	fcport = sa_ctl->fcport;

	/* Alloc SRB structure */
	sp = qla2x00_get_sp(vha, fcport, GFP_KERNEL);
	if (!sp) {
		ql_dbg(ql_dbg_edif, vha, 0x70e6,
		 "SRB allocation failed\n");
		rval = -ENOMEM;
		goto done;
	}

	fcport->flags |= FCF_ASYNC_SENT;
	iocb_cmd = &sp->u.iocb_cmd;
	iocb_cmd->u.sa_update.sa_ctl = sa_ctl;

	ql_dbg(ql_dbg_edif, vha, 0x3073,
	    "Enter: SA REPL portid=%06x, sa_ctl %p, index %x, nport_handle: 0x%x\n",
	    fcport->d_id.b24, sa_ctl, sa_ctl->index, nport_handle);
	/*
	 * if this is a sadb cleanup delete, mark it so the isr can
	 * take the correct action
	 */
	if (sa_ctl->flags & EDIF_SA_CTL_FLG_CLEANUP_DEL) {
		/* mark this srb as a cleanup delete */
		sp->flags |= SRB_EDIF_CLEANUP_DELETE;
		ql_dbg(ql_dbg_edif, vha, 0x70e6,
		    "%s: sp 0x%p flagged as cleanup delete\n", __func__, sp);
	}

	sp->type = SRB_SA_REPLACE;
	sp->name = "SA_REPLACE";
	sp->fcport = fcport;
	sp->free = qla2x00_rel_sp;
	sp->done = qla_noop_sp_done;

	rval = qla2x00_start_sp(sp);

	if (rval != QLA_SUCCESS) {
		goto done_free_sp;
	}

	return rval;
done_free_sp:
	kref_put(&sp->cmd_kref, qla2x00_sp_release);
	fcport->flags &= ~FCF_ASYNC_SENT;
done:
	fcport->flags &= ~FCF_ASYNC_ACTIVE;
	return rval;
}

void qla24xx_sa_update_iocb(srb_t *sp, struct sa_update_28xx *sa_update_iocb)
{
	int	itr = 0;
	struct	scsi_qla_host		*vha = sp->vha;
	struct	qla_sa_update_frame	*sa_frame =
		&sp->u.iocb_cmd.u.sa_update.sa_frame;
	u8 flags = 0;

	switch (sa_frame->flags & (SAU_FLG_INV | SAU_FLG_TX)) {
	case 0:
		ql_dbg(ql_dbg_edif, vha, 0x911d,
		    "%s: EDIF SA UPDATE RX IOCB  vha: 0x%p  index: %d\n",
		    __func__, vha, sa_frame->fast_sa_index);
		break;
	case 1:
		ql_dbg(ql_dbg_edif, vha, 0x911d,
		    "%s: EDIF SA DELETE RX IOCB  vha: 0x%p  index: %d\n",
		    __func__, vha, sa_frame->fast_sa_index);
		flags |= SA_FLAG_INVALIDATE;
		break;
	case 2:
		ql_dbg(ql_dbg_edif, vha, 0x911d,
		    "%s: EDIF SA UPDATE TX IOCB  vha: 0x%p  index: %d\n",
		    __func__, vha, sa_frame->fast_sa_index);
		flags |= SA_FLAG_TX;
		break;
	case 3:
		ql_dbg(ql_dbg_edif, vha, 0x911d,
		    "%s: EDIF SA DELETE TX IOCB  vha: 0x%p  index: %d\n",
		    __func__, vha, sa_frame->fast_sa_index);
		flags |= SA_FLAG_TX | SA_FLAG_INVALIDATE;
		break;
	}

	sa_update_iocb->entry_type = SA_UPDATE_IOCB_TYPE;
	sa_update_iocb->entry_count = 1;
	sa_update_iocb->sys_define = 0;
	sa_update_iocb->entry_status = 0;
	sa_update_iocb->handle = sp->handle;
	sa_update_iocb->u.nport_handle = cpu_to_le16(sp->fcport->loop_id);
	sa_update_iocb->vp_index = sp->fcport->vha->vp_idx;
	sa_update_iocb->port_id[0] = sp->fcport->d_id.b.al_pa;
	sa_update_iocb->port_id[1] = sp->fcport->d_id.b.area;
	sa_update_iocb->port_id[2] = sp->fcport->d_id.b.domain;

	sa_update_iocb->flags = flags;
	sa_update_iocb->salt = cpu_to_le32(sa_frame->salt);
	sa_update_iocb->spi = cpu_to_le32(sa_frame->spi);
	sa_update_iocb->sa_index = cpu_to_le16(sa_frame->fast_sa_index);

	sa_update_iocb->sa_control |= SA_CNTL_ENC_FCSP;
	if (sp->fcport->edif.aes_gmac)
		sa_update_iocb->sa_control |= SA_CNTL_AES_GMAC;

	if (sa_frame->flags & SAU_FLG_KEY256) {
		sa_update_iocb->sa_control |= SA_CNTL_KEY256;
		for (itr = 0; itr < 32; itr++)
			sa_update_iocb->sa_key[itr] = sa_frame->sa_key[itr];
	} else {
		sa_update_iocb->sa_control |= SA_CNTL_KEY128;
		for (itr = 0; itr < 16; itr++)
			sa_update_iocb->sa_key[itr] = sa_frame->sa_key[itr];
	}

	ql_dbg(ql_dbg_edif, vha, 0x921d,
	    "%s SAU Port ID = %02x%02x%02x, flags=%xh, index=%u, ctl=%xh, SPI 0x%x flags 0x%x hdl=%x gmac %d\n",
	    __func__, sa_update_iocb->port_id[2], sa_update_iocb->port_id[1],
	    sa_update_iocb->port_id[0], sa_update_iocb->flags, sa_update_iocb->sa_index,
	    sa_update_iocb->sa_control, sa_update_iocb->spi, sa_frame->flags, sp->handle,
	    sp->fcport->edif.aes_gmac);

	if (sa_frame->flags & SAU_FLG_TX)
		sp->fcport->edif.tx_sa_pending = 1;
	else
		sp->fcport->edif.rx_sa_pending = 1;

	sp->fcport->vha->qla_stats.control_requests++;
}

void
qla24xx_sa_replace_iocb(srb_t *sp, struct sa_update_28xx *sa_update_iocb)
{
	struct	scsi_qla_host		*vha = sp->vha;
	struct srb_iocb *srb_iocb = &sp->u.iocb_cmd;
	struct	edif_sa_ctl		*sa_ctl = srb_iocb->u.sa_update.sa_ctl;
	uint16_t nport_handle = sp->fcport->loop_id;

	sa_update_iocb->entry_type = SA_UPDATE_IOCB_TYPE;
	sa_update_iocb->entry_count = 1;
	sa_update_iocb->sys_define = 0;
	sa_update_iocb->entry_status = 0;
	sa_update_iocb->handle = sp->handle;

	sa_update_iocb->u.nport_handle = cpu_to_le16(nport_handle);

	sa_update_iocb->vp_index = sp->fcport->vha->vp_idx;
	sa_update_iocb->port_id[0] = sp->fcport->d_id.b.al_pa;
	sa_update_iocb->port_id[1] = sp->fcport->d_id.b.area;
	sa_update_iocb->port_id[2] = sp->fcport->d_id.b.domain;

	/* Invalidate the index. salt, spi, control & key are ignore */
	sa_update_iocb->flags = SA_FLAG_INVALIDATE;
	sa_update_iocb->salt = 0;
	sa_update_iocb->spi = 0;
	sa_update_iocb->sa_index = cpu_to_le16(sa_ctl->index);
	sa_update_iocb->sa_control = 0;

	ql_dbg(ql_dbg_edif, vha, 0x921d,
	    "%s SAU DELETE RX Port ID = %02x:%02x:%02x, lid %d flags=%xh, index=%u, hdl=%x\n",
	    __func__, sa_update_iocb->port_id[2], sa_update_iocb->port_id[1],
	    sa_update_iocb->port_id[0], nport_handle, sa_update_iocb->flags,
	    sa_update_iocb->sa_index, sp->handle);

	sp->fcport->vha->qla_stats.control_requests++;
}

void qla24xx_auth_els(scsi_qla_host_t *vha, void **pkt, struct rsp_que **rsp)
{
	struct purex_entry_24xx *p = *pkt;
	struct enode		*ptr;
	int		sid;
	u16 totlen;
	struct purexevent	*purex;
	struct scsi_qla_host *host = NULL;
	int rc;
	struct fc_port *fcport;
	struct qla_els_pt_arg a;
	be_id_t beid;

	memset(&a, 0, sizeof(a));

	a.els_opcode = ELS_AUTH_ELS;
	a.nport_handle = p->nport_handle;
	a.rx_xchg_address = p->rx_xchg_addr;
	a.did.b.domain = p->s_id[2];
	a.did.b.area   = p->s_id[1];
	a.did.b.al_pa  = p->s_id[0];
	a.tx_byte_count = a.tx_len = sizeof(struct fc_els_ls_rjt);
	a.tx_addr = vha->hw->elsrej.cdma;
	a.vp_idx = vha->vp_idx;
	a.control_flags = EPD_ELS_RJT;
	a.ox_id = le16_to_cpu(p->ox_id);

	sid = p->s_id[0] | (p->s_id[1] << 8) | (p->s_id[2] << 16);

	totlen = (le16_to_cpu(p->frame_size) & 0x0fff) - PURX_ELS_HEADER_SIZE;
	if (le16_to_cpu(p->status_flags) & 0x8000) {
		totlen = le16_to_cpu(p->trunc_frame_size);
		qla_els_reject_iocb(vha, (*rsp)->qpair, &a);
		__qla_consume_iocb(vha, pkt, rsp);
		return;
	}

	if (totlen > ELS_MAX_PAYLOAD) {
		ql_dbg(ql_dbg_edif, vha, 0x0910d,
		    "%s WARNING: verbose ELS frame received (totlen=%x)\n",
		    __func__, totlen);
		qla_els_reject_iocb(vha, (*rsp)->qpair, &a);
		__qla_consume_iocb(vha, pkt, rsp);
		return;
	}

	if (!vha->hw->flags.edif_enabled) {
		/* edif support not enabled */
		ql_dbg(ql_dbg_edif, vha, 0x910e, "%s edif not enabled\n",
		    __func__);
		qla_els_reject_iocb(vha, (*rsp)->qpair, &a);
		__qla_consume_iocb(vha, pkt, rsp);
		return;
	}

	ptr = qla_enode_alloc(vha, N_PUREX);
	if (!ptr) {
		ql_dbg(ql_dbg_edif, vha, 0x09109,
		    "WARNING: enode alloc failed for sid=%x\n",
		    sid);
		qla_els_reject_iocb(vha, (*rsp)->qpair, &a);
		__qla_consume_iocb(vha, pkt, rsp);
		return;
	}

	purex = &ptr->u.purexinfo;
	purex->pur_info.pur_sid = a.did;
	purex->pur_info.pur_bytes_rcvd = totlen;
	purex->pur_info.pur_rx_xchg_address = le32_to_cpu(p->rx_xchg_addr);
	purex->pur_info.pur_nphdl = le16_to_cpu(p->nport_handle);
	purex->pur_info.pur_did.b.domain =  p->d_id[2];
	purex->pur_info.pur_did.b.area =  p->d_id[1];
	purex->pur_info.pur_did.b.al_pa =  p->d_id[0];
	purex->pur_info.vp_idx = p->vp_idx;

	a.sid = purex->pur_info.pur_did;

	rc = __qla_copy_purex_to_buffer(vha, pkt, rsp, purex->msgp,
		purex->msgp_len);
	if (rc) {
		qla_els_reject_iocb(vha, (*rsp)->qpair, &a);
		qla_enode_free(vha, ptr);
		return;
	}
	beid.al_pa = purex->pur_info.pur_did.b.al_pa;
	beid.area   = purex->pur_info.pur_did.b.area;
	beid.domain = purex->pur_info.pur_did.b.domain;
	host = qla_find_host_by_d_id(vha, beid);
	if (!host) {
		ql_log(ql_log_fatal, vha, 0x508b,
		    "%s Drop ELS due to unable to find host %06x\n",
		    __func__, purex->pur_info.pur_did.b24);

		qla_els_reject_iocb(vha, (*rsp)->qpair, &a);
		qla_enode_free(vha, ptr);
		return;
	}

	fcport = qla2x00_find_fcport_by_pid(host, &purex->pur_info.pur_sid);

	if (DBELL_INACTIVE(vha)) {
		ql_dbg(ql_dbg_edif, host, 0x0910c, "%s e_dbell.db_flags =%x %06x\n",
		    __func__, host->e_dbell.db_flags,
		    fcport ? fcport->d_id.b24 : 0);

		qla_els_reject_iocb(host, (*rsp)->qpair, &a);
		qla_enode_free(host, ptr);
		return;
	}

	if (fcport && EDIF_SESSION_DOWN(fcport)) {
		ql_dbg(ql_dbg_edif, host, 0x13b6,
		    "%s terminate exchange. Send logo to 0x%x\n",
		    __func__, a.did.b24);

		a.tx_byte_count = a.tx_len = 0;
		a.tx_addr = 0;
		a.control_flags = EPD_RX_XCHG;  /* EPD_RX_XCHG = terminate cmd */
		qla_els_reject_iocb(host, (*rsp)->qpair, &a);
		qla_enode_free(host, ptr);
		/* send logo to let remote port knows to tear down session */
		fcport->send_els_logo = 1;
		qlt_schedule_sess_for_deletion(fcport);
		return;
	}

	/* add the local enode to the list */
	qla_enode_add(host, ptr);

	ql_dbg(ql_dbg_edif, host, 0x0910c,
	    "%s COMPLETE purex->pur_info.pur_bytes_rcvd =%xh s:%06x -> d:%06x xchg=%xh\n",
	    __func__, purex->pur_info.pur_bytes_rcvd, purex->pur_info.pur_sid.b24,
	    purex->pur_info.pur_did.b24, purex->pur_info.pur_rx_xchg_address);

	qla_edb_eventcreate(host, VND_CMD_AUTH_STATE_ELS_RCVD, sid, 0, NULL);
}

static uint16_t  qla_edif_get_sa_index_from_freepool(fc_port_t *fcport, int dir)
{
	struct scsi_qla_host *vha = fcport->vha;
	struct qla_hw_data *ha = vha->hw;
	void *sa_id_map;
	unsigned long flags = 0;
	u16 sa_index;

	ql_dbg(ql_dbg_edif + ql_dbg_verbose, vha, 0x3063,
	    "%s: entry\n", __func__);

	if (dir)
		sa_id_map = ha->edif_tx_sa_id_map;
	else
		sa_id_map = ha->edif_rx_sa_id_map;

	spin_lock_irqsave(&ha->sadb_fp_lock, flags);
	sa_index = find_first_zero_bit(sa_id_map, EDIF_NUM_SA_INDEX);
	if (sa_index >=  EDIF_NUM_SA_INDEX) {
		spin_unlock_irqrestore(&ha->sadb_fp_lock, flags);
		return INVALID_EDIF_SA_INDEX;
	}
	set_bit(sa_index, sa_id_map);
	spin_unlock_irqrestore(&ha->sadb_fp_lock, flags);

	if (dir)
		sa_index += EDIF_TX_SA_INDEX_BASE;

	ql_dbg(ql_dbg_edif, vha, 0x3063,
	    "%s: index retrieved from free pool %d\n", __func__, sa_index);

	return sa_index;
}

/* find an sadb entry for an nport_handle */
static struct edif_sa_index_entry *
qla_edif_sadb_find_sa_index_entry(uint16_t nport_handle,
		struct list_head *sa_list)
{
	struct edif_sa_index_entry *entry;
	struct edif_sa_index_entry *tentry;
	struct list_head *indx_list = sa_list;

	list_for_each_entry_safe(entry, tentry, indx_list, next) {
		if (entry->handle == nport_handle)
			return entry;
	}
	return NULL;
}

/* remove an sa_index from the nport_handle and return it to the free pool */
static int qla_edif_sadb_delete_sa_index(fc_port_t *fcport, uint16_t nport_handle,
		uint16_t sa_index)
{
	struct edif_sa_index_entry *entry;
	struct list_head *sa_list;
	int dir = (sa_index < EDIF_TX_SA_INDEX_BASE) ? 0 : 1;
	int slot = 0;
	int free_slot_count = 0;
	scsi_qla_host_t *vha = fcport->vha;
	struct qla_hw_data *ha = vha->hw;
	unsigned long flags = 0;

	ql_dbg(ql_dbg_edif, vha, 0x3063,
	    "%s: entry\n", __func__);

	if (dir)
		sa_list = &ha->sadb_tx_index_list;
	else
		sa_list = &ha->sadb_rx_index_list;

	entry = qla_edif_sadb_find_sa_index_entry(nport_handle, sa_list);
	if (!entry) {
		ql_dbg(ql_dbg_edif, vha, 0x3063,
		    "%s: no entry found for nport_handle 0x%x\n",
		    __func__, nport_handle);
		return -1;
	}

	spin_lock_irqsave(&ha->sadb_lock, flags);
	/*
	 * each tx/rx direction has up to 2 sa indexes/slots. 1 slot for in flight traffic
	 * the other is use at re-key time.
	 */
	for (slot = 0; slot < 2; slot++) {
		if (entry->sa_pair[slot].sa_index == sa_index) {
			entry->sa_pair[slot].sa_index = INVALID_EDIF_SA_INDEX;
			entry->sa_pair[slot].spi = 0;
			free_slot_count++;
			qla_edif_add_sa_index_to_freepool(fcport, dir, sa_index);
		} else if (entry->sa_pair[slot].sa_index == INVALID_EDIF_SA_INDEX) {
			free_slot_count++;
		}
	}

	if (free_slot_count == 2) {
		list_del(&entry->next);
		kfree(entry);
	}
	spin_unlock_irqrestore(&ha->sadb_lock, flags);

	ql_dbg(ql_dbg_edif, vha, 0x3063,
	    "%s: sa_index %d removed, free_slot_count: %d\n",
	    __func__, sa_index, free_slot_count);

	return 0;
}

void
qla28xx_sa_update_iocb_entry(scsi_qla_host_t *v, struct req_que *req,
	struct sa_update_28xx *pkt)
{
	const char *func = "SA_UPDATE_RESPONSE_IOCB";
	srb_t *sp;
	struct edif_sa_ctl *sa_ctl;
	int old_sa_deleted = 1;
	uint16_t nport_handle;
	struct scsi_qla_host *vha;

	sp = qla2x00_get_sp_from_handle(v, func, req, pkt);

	if (!sp) {
		ql_dbg(ql_dbg_edif, v, 0x3063,
			"%s: no sp found for pkt\n", __func__);
		return;
	}
	/* use sp->vha due to npiv */
	vha = sp->vha;

	switch (pkt->flags & (SA_FLAG_INVALIDATE | SA_FLAG_TX)) {
	case 0:
		ql_dbg(ql_dbg_edif, vha, 0x3063,
		    "%s: EDIF SA UPDATE RX IOCB  vha: 0x%p  index: %d\n",
		    __func__, vha, pkt->sa_index);
		break;
	case 1:
		ql_dbg(ql_dbg_edif, vha, 0x3063,
		    "%s: EDIF SA DELETE RX IOCB  vha: 0x%p  index: %d\n",
		    __func__, vha, pkt->sa_index);
		break;
	case 2:
		ql_dbg(ql_dbg_edif, vha, 0x3063,
		    "%s: EDIF SA UPDATE TX IOCB  vha: 0x%p  index: %d\n",
		    __func__, vha, pkt->sa_index);
		break;
	case 3:
		ql_dbg(ql_dbg_edif, vha, 0x3063,
		    "%s: EDIF SA DELETE TX IOCB  vha: 0x%p  index: %d\n",
		    __func__, vha, pkt->sa_index);
		break;
	}

	/*
	 * dig the nport handle out of the iocb, fcport->loop_id can not be trusted
	 * to be correct during cleanup sa_update iocbs.
	 */
	nport_handle = sp->fcport->loop_id;

	ql_dbg(ql_dbg_edif, vha, 0x3063,
	    "%s: %8phN comp status=%x old_sa_info=%x new_sa_info=%x lid %d, index=0x%x pkt_flags %xh hdl=%x\n",
	    __func__, sp->fcport->port_name, pkt->u.comp_sts, pkt->old_sa_info, pkt->new_sa_info,
	    nport_handle, pkt->sa_index, pkt->flags, sp->handle);

	/* if rx delete, remove the timer */
	if ((pkt->flags & (SA_FLAG_INVALIDATE | SA_FLAG_TX)) ==  SA_FLAG_INVALIDATE) {
		struct edif_list_entry *edif_entry;

		sp->fcport->flags &= ~(FCF_ASYNC_SENT | FCF_ASYNC_ACTIVE);

		edif_entry = qla_edif_list_find_sa_index(sp->fcport, nport_handle);
		if (edif_entry) {
			ql_dbg(ql_dbg_edif, vha, 0x5033,
			    "%s: removing edif_entry %p, new sa_index: 0x%x\n",
			    __func__, edif_entry, pkt->sa_index);
			qla_edif_list_delete_sa_index(sp->fcport, edif_entry);
			timer_shutdown(&edif_entry->timer);

			ql_dbg(ql_dbg_edif, vha, 0x5033,
			    "%s: releasing edif_entry %p, new sa_index: 0x%x\n",
			    __func__, edif_entry, pkt->sa_index);

			kfree(edif_entry);
		}
	}

	/*
	 * if this is a delete for either tx or rx, make sure it succeeded.
	 * The new_sa_info field should be 0xffff on success
	 */
	if (pkt->flags & SA_FLAG_INVALIDATE)
		old_sa_deleted = (le16_to_cpu(pkt->new_sa_info) == 0xffff) ? 1 : 0;

	/* Process update and delete the same way */

	/* If this is an sadb cleanup delete, bypass sending events to IPSEC */
	if (sp->flags & SRB_EDIF_CLEANUP_DELETE) {
		sp->fcport->flags &= ~(FCF_ASYNC_SENT | FCF_ASYNC_ACTIVE);
		ql_dbg(ql_dbg_edif, vha, 0x3063,
		    "%s: nph 0x%x, sa_index %d removed from fw\n",
		    __func__, sp->fcport->loop_id, pkt->sa_index);

	} else if ((pkt->entry_status == 0) && (pkt->u.comp_sts == 0) &&
	    old_sa_deleted) {
		/*
		 * Note: Wa are only keeping track of latest SA,
		 * so we know when we can start enableing encryption per I/O.
		 * If all SA's get deleted, let FW reject the IOCB.

		 * TODO: edif: don't set enabled here I think
		 * TODO: edif: prli complete is where it should be set
		 */
		ql_dbg(ql_dbg_edif + ql_dbg_verbose, vha, 0x3063,
			"SA(%x)updated for s_id %02x%02x%02x\n",
			pkt->new_sa_info,
			pkt->port_id[2], pkt->port_id[1], pkt->port_id[0]);
		sp->fcport->edif.enable = 1;
		if (pkt->flags & SA_FLAG_TX) {
			sp->fcport->edif.tx_sa_set = 1;
			sp->fcport->edif.tx_sa_pending = 0;
			qla_edb_eventcreate(vha, VND_CMD_AUTH_STATE_SAUPDATE_COMPL,
				QL_VND_SA_STAT_SUCCESS,
				QL_VND_TX_SA_KEY, sp->fcport);
		} else {
			sp->fcport->edif.rx_sa_set = 1;
			sp->fcport->edif.rx_sa_pending = 0;
			qla_edb_eventcreate(vha, VND_CMD_AUTH_STATE_SAUPDATE_COMPL,
				QL_VND_SA_STAT_SUCCESS,
				QL_VND_RX_SA_KEY, sp->fcport);
		}
	} else {
		ql_dbg(ql_dbg_edif, vha, 0x3063,
		    "%s: %8phN SA update FAILED: sa_index: %d, new_sa_info %d, %02x%02x%02x\n",
		    __func__, sp->fcport->port_name, pkt->sa_index, pkt->new_sa_info,
		    pkt->port_id[2], pkt->port_id[1], pkt->port_id[0]);

		if (pkt->flags & SA_FLAG_TX)
			qla_edb_eventcreate(vha, VND_CMD_AUTH_STATE_SAUPDATE_COMPL,
				(le16_to_cpu(pkt->u.comp_sts) << 16) | QL_VND_SA_STAT_FAILED,
				QL_VND_TX_SA_KEY, sp->fcport);
		else
			qla_edb_eventcreate(vha, VND_CMD_AUTH_STATE_SAUPDATE_COMPL,
				(le16_to_cpu(pkt->u.comp_sts) << 16) | QL_VND_SA_STAT_FAILED,
				QL_VND_RX_SA_KEY, sp->fcport);
	}

	/* for delete, release sa_ctl, sa_index */
	if (pkt->flags & SA_FLAG_INVALIDATE) {
		/* release the sa_ctl */
		sa_ctl = qla_edif_find_sa_ctl_by_index(sp->fcport,
		    le16_to_cpu(pkt->sa_index), (pkt->flags & SA_FLAG_TX));
		if (sa_ctl &&
		    qla_edif_find_sa_ctl_by_index(sp->fcport, sa_ctl->index,
			(pkt->flags & SA_FLAG_TX)) != NULL) {
			ql_dbg(ql_dbg_edif + ql_dbg_verbose, vha, 0x3063,
			    "%s: freeing sa_ctl for index %d\n",
			    __func__, sa_ctl->index);
			qla_edif_free_sa_ctl(sp->fcport, sa_ctl, sa_ctl->index);
		} else {
			ql_dbg(ql_dbg_edif, vha, 0x3063,
			    "%s: sa_ctl NOT freed, sa_ctl: %p\n",
			    __func__, sa_ctl);
		}
		ql_dbg(ql_dbg_edif, vha, 0x3063,
		    "%s: freeing sa_index %d, nph: 0x%x\n",
		    __func__, le16_to_cpu(pkt->sa_index), nport_handle);
		qla_edif_sadb_delete_sa_index(sp->fcport, nport_handle,
		    le16_to_cpu(pkt->sa_index));
	/*
	 * check for a failed sa_update and remove
	 * the sadb entry.
	 */
	} else if (pkt->u.comp_sts) {
		ql_dbg(ql_dbg_edif, vha, 0x3063,
		    "%s: freeing sa_index %d, nph: 0x%x\n",
		    __func__, pkt->sa_index, nport_handle);
		qla_edif_sadb_delete_sa_index(sp->fcport, nport_handle,
		    le16_to_cpu(pkt->sa_index));
		switch (le16_to_cpu(pkt->u.comp_sts)) {
		case CS_PORT_EDIF_UNAVAIL:
		case CS_PORT_EDIF_LOGOUT:
			qlt_schedule_sess_for_deletion(sp->fcport);
			break;
		default:
			break;
		}
	}

	sp->done(sp, 0);
}

/**
 * qla28xx_start_scsi_edif() - Send a SCSI type 6 command to the ISP
 * @sp: command to send to the ISP
 *
 * Return: non-zero if a failure occurred, else zero.
 */
int
qla28xx_start_scsi_edif(srb_t *sp)
{
	int             nseg;
	unsigned long   flags;
	struct scsi_cmnd *cmd;
	uint32_t        *clr_ptr;
	uint32_t        index, i;
	uint32_t        handle;
	uint16_t        cnt;
	int16_t        req_cnt;
	uint16_t        tot_dsds;
	__be32 *fcp_dl;
	uint8_t additional_cdb_len;
	struct ct6_dsd *ctx;
	struct scsi_qla_host *vha = sp->vha;
	struct qla_hw_data *ha = vha->hw;
	struct cmd_type_6 *cmd_pkt;
	struct dsd64	*cur_dsd;
	uint8_t		avail_dsds = 0;
	struct scatterlist *sg;
	struct req_que *req = sp->qpair->req;
	spinlock_t *lock = sp->qpair->qp_lock_ptr;

	/* Setup device pointers. */
	cmd = GET_CMD_SP(sp);

	/* So we know we haven't pci_map'ed anything yet */
	tot_dsds = 0;

	/* Send marker if required */
	if (vha->marker_needed != 0) {
		if (qla2x00_marker(vha, sp->qpair, 0, 0, MK_SYNC_ALL) !=
			QLA_SUCCESS) {
			ql_log(ql_log_warn, vha, 0x300c,
			    "qla2x00_marker failed for cmd=%p.\n", cmd);
			return QLA_FUNCTION_FAILED;
		}
		vha->marker_needed = 0;
	}

	/* Acquire ring specific lock */
	spin_lock_irqsave(lock, flags);

	/* Check for room in outstanding command list. */
	handle = req->current_outstanding_cmd;
	for (index = 1; index < req->num_outstanding_cmds; index++) {
		handle++;
		if (handle == req->num_outstanding_cmds)
			handle = 1;
		if (!req->outstanding_cmds[handle])
			break;
	}
	if (index == req->num_outstanding_cmds)
		goto queuing_error;

	/* Map the sg table so we have an accurate count of sg entries needed */
	if (scsi_sg_count(cmd)) {
		nseg = dma_map_sg(&ha->pdev->dev, scsi_sglist(cmd),
		    scsi_sg_count(cmd), cmd->sc_data_direction);
		if (unlikely(!nseg))
			goto queuing_error;
	} else {
		nseg = 0;
	}

	tot_dsds = nseg;
	req_cnt = qla24xx_calc_iocbs(vha, tot_dsds);

	sp->iores.res_type = RESOURCE_IOCB | RESOURCE_EXCH;
	sp->iores.exch_cnt = 1;
	sp->iores.iocb_cnt = req_cnt;
	if (qla_get_fw_resources(sp->qpair, &sp->iores))
		goto queuing_error;

	if (req->cnt < (req_cnt + 2)) {
		cnt = IS_SHADOW_REG_CAPABLE(ha) ? *req->out_ptr :
		    rd_reg_dword(req->req_q_out);
		if (req->ring_index < cnt)
			req->cnt = cnt - req->ring_index;
		else
			req->cnt = req->length -
			    (req->ring_index - cnt);
		if (req->cnt < (req_cnt + 2))
			goto queuing_error;
	}

	if (qla_get_buf(vha, sp->qpair, &sp->u.scmd.buf_dsc)) {
		ql_log(ql_log_fatal, vha, 0x3011,
		    "Failed to allocate buf for fcp_cmnd for cmd=%p.\n", cmd);
		goto queuing_error;
	}

	sp->flags |= SRB_GOT_BUF;
	ctx = &sp->u.scmd.ct6_ctx;
	ctx->fcp_cmnd = sp->u.scmd.buf_dsc.buf;
	ctx->fcp_cmnd_dma = sp->u.scmd.buf_dsc.buf_dma;

	if (cmd->cmd_len > 16) {
		additional_cdb_len = cmd->cmd_len - 16;
		if ((cmd->cmd_len % 4) != 0) {
			/*
			 * SCSI command bigger than 16 bytes must be
			 * multiple of 4
			 */
			ql_log(ql_log_warn, vha, 0x3012,
			    "scsi cmd len %d not multiple of 4 for cmd=%p.\n",
			    cmd->cmd_len, cmd);
			goto queuing_error_fcp_cmnd;
		}
		ctx->fcp_cmnd_len = 12 + cmd->cmd_len + 4;
	} else {
		additional_cdb_len = 0;
		ctx->fcp_cmnd_len = 12 + 16 + 4;
	}

	cmd_pkt = (struct cmd_type_6 *)req->ring_ptr;
	cmd_pkt->handle = make_handle(req->id, handle);

	/*
	 * Zero out remaining portion of packet.
	 * tagged queuing modifier -- default is TSK_SIMPLE (0).
	 */
	clr_ptr = (uint32_t *)cmd_pkt + 2;
	memset(clr_ptr, 0, REQUEST_ENTRY_SIZE - 8);
	cmd_pkt->dseg_count = cpu_to_le16(tot_dsds);

	/* No data transfer */
	if (!scsi_bufflen(cmd) || cmd->sc_data_direction == DMA_NONE) {
		cmd_pkt->byte_count = cpu_to_le32(0);
		goto no_dsds;
	}

	/* Set transfer direction */
	if (cmd->sc_data_direction == DMA_TO_DEVICE) {
		cmd_pkt->control_flags = cpu_to_le16(CF_WRITE_DATA);
		vha->qla_stats.output_bytes += scsi_bufflen(cmd);
		vha->qla_stats.output_requests++;
		sp->fcport->edif.tx_bytes += scsi_bufflen(cmd);
	} else if (cmd->sc_data_direction == DMA_FROM_DEVICE) {
		cmd_pkt->control_flags = cpu_to_le16(CF_READ_DATA);
		vha->qla_stats.input_bytes += scsi_bufflen(cmd);
		vha->qla_stats.input_requests++;
		sp->fcport->edif.rx_bytes += scsi_bufflen(cmd);
	}

	cmd_pkt->control_flags |= cpu_to_le16(CF_EN_EDIF);
	cmd_pkt->control_flags &= ~(cpu_to_le16(CF_NEW_SA));

	/* One DSD is available in the Command Type 6 IOCB */
	avail_dsds = 1;
	cur_dsd = &cmd_pkt->fcp_dsd;

	/* Load data segments */
	scsi_for_each_sg(cmd, sg, tot_dsds, i) {
		dma_addr_t      sle_dma;
		cont_a64_entry_t *cont_pkt;

		/* Allocate additional continuation packets? */
		if (avail_dsds == 0) {
			/*
			 * Five DSDs are available in the Continuation
			 * Type 1 IOCB.
			 */
			cont_pkt = qla2x00_prep_cont_type1_iocb(vha, req);
			cur_dsd = cont_pkt->dsd;
			avail_dsds = 5;
		}

		sle_dma = sg_dma_address(sg);
		put_unaligned_le64(sle_dma, &cur_dsd->address);
		cur_dsd->length = cpu_to_le32(sg_dma_len(sg));
		cur_dsd++;
		avail_dsds--;
	}

no_dsds:
	/* Set NPORT-ID and LUN number*/
	cmd_pkt->nport_handle = cpu_to_le16(sp->fcport->loop_id);
	cmd_pkt->port_id[0] = sp->fcport->d_id.b.al_pa;
	cmd_pkt->port_id[1] = sp->fcport->d_id.b.area;
	cmd_pkt->port_id[2] = sp->fcport->d_id.b.domain;
	cmd_pkt->vp_index = sp->vha->vp_idx;

	cmd_pkt->entry_type = COMMAND_TYPE_6;

	/* Set total data segment count. */
	cmd_pkt->entry_count = (uint8_t)req_cnt;

	int_to_scsilun(cmd->device->lun, &cmd_pkt->lun);
	host_to_fcp_swap((uint8_t *)&cmd_pkt->lun, sizeof(cmd_pkt->lun));

	/* build FCP_CMND IU */
	int_to_scsilun(cmd->device->lun, &ctx->fcp_cmnd->lun);
	ctx->fcp_cmnd->additional_cdb_len = additional_cdb_len;

	if (cmd->sc_data_direction == DMA_TO_DEVICE)
		ctx->fcp_cmnd->additional_cdb_len |= 1;
	else if (cmd->sc_data_direction == DMA_FROM_DEVICE)
		ctx->fcp_cmnd->additional_cdb_len |= 2;

	/* Populate the FCP_PRIO. */
	if (ha->flags.fcp_prio_enabled)
		ctx->fcp_cmnd->task_attribute |=
		    sp->fcport->fcp_prio << 3;

	memcpy(ctx->fcp_cmnd->cdb, cmd->cmnd, cmd->cmd_len);

	fcp_dl = (__be32 *)(ctx->fcp_cmnd->cdb + 16 +
	    additional_cdb_len);
	*fcp_dl = htonl((uint32_t)scsi_bufflen(cmd));

	cmd_pkt->fcp_cmnd_dseg_len = cpu_to_le16(ctx->fcp_cmnd_len);
	put_unaligned_le64(ctx->fcp_cmnd_dma, &cmd_pkt->fcp_cmnd_dseg_address);

	cmd_pkt->byte_count = cpu_to_le32((uint32_t)scsi_bufflen(cmd));
	/* Set total data segment count. */
	cmd_pkt->entry_count = (uint8_t)req_cnt;
	cmd_pkt->entry_status = 0;

	/* Build command packet. */
	req->current_outstanding_cmd = handle;
	req->outstanding_cmds[handle] = sp;
	sp->handle = handle;
	cmd->host_scribble = (unsigned char *)(unsigned long)handle;
	req->cnt -= req_cnt;

	/* Adjust ring index. */
	wmb();
	req->ring_index++;
	if (req->ring_index == req->length) {
		req->ring_index = 0;
		req->ring_ptr = req->ring;
	} else {
		req->ring_ptr++;
	}

	sp->qpair->cmd_cnt++;
	/* Set chip new ring index. */
	wrt_reg_dword(req->req_q_in, req->ring_index);

	spin_unlock_irqrestore(lock, flags);

	return QLA_SUCCESS;

queuing_error_fcp_cmnd:
queuing_error:
	if (tot_dsds)
		scsi_dma_unmap(cmd);

	qla_put_buf(sp->qpair, &sp->u.scmd.buf_dsc);
	qla_put_fw_resources(sp->qpair, &sp->iores);
	spin_unlock_irqrestore(lock, flags);

	return QLA_FUNCTION_FAILED;
}

/**********************************************
 * edif update/delete sa_index list functions *
 **********************************************/

/* clear the edif_indx_list for this port */
void qla_edif_list_del(fc_port_t *fcport)
{
	struct edif_list_entry *indx_lst;
	struct edif_list_entry *tindx_lst;
	struct list_head *indx_list = &fcport->edif.edif_indx_list;
	unsigned long flags = 0;

	spin_lock_irqsave(&fcport->edif.indx_list_lock, flags);
	list_for_each_entry_safe(indx_lst, tindx_lst, indx_list, next) {
		list_del(&indx_lst->next);
		kfree(indx_lst);
	}
	spin_unlock_irqrestore(&fcport->edif.indx_list_lock, flags);
}

/******************
 * SADB functions *
 ******************/

/* allocate/retrieve an sa_index for a given spi */
static uint16_t qla_edif_sadb_get_sa_index(fc_port_t *fcport,
		struct qla_sa_update_frame *sa_frame)
{
	struct edif_sa_index_entry *entry;
	struct list_head *sa_list;
	uint16_t sa_index;
	int dir = sa_frame->flags & SAU_FLG_TX;
	int slot = 0;
	int free_slot = -1;
	scsi_qla_host_t *vha = fcport->vha;
	struct qla_hw_data *ha = vha->hw;
	unsigned long flags = 0;
	uint16_t nport_handle = fcport->loop_id;

	ql_dbg(ql_dbg_edif, vha, 0x3063,
	    "%s: entry  fc_port: %p, nport_handle: 0x%x\n",
	    __func__, fcport, nport_handle);

	if (dir)
		sa_list = &ha->sadb_tx_index_list;
	else
		sa_list = &ha->sadb_rx_index_list;

	entry = qla_edif_sadb_find_sa_index_entry(nport_handle, sa_list);
	if (!entry) {
		if ((sa_frame->flags & (SAU_FLG_TX | SAU_FLG_INV)) == SAU_FLG_INV) {
			ql_dbg(ql_dbg_edif, vha, 0x3063,
			    "%s: rx delete request with no entry\n", __func__);
			return RX_DELETE_NO_EDIF_SA_INDEX;
		}

		/* if there is no entry for this nport, add one */
		entry = kzalloc((sizeof(struct edif_sa_index_entry)), GFP_ATOMIC);
		if (!entry)
			return INVALID_EDIF_SA_INDEX;

		sa_index = qla_edif_get_sa_index_from_freepool(fcport, dir);
		if (sa_index == INVALID_EDIF_SA_INDEX) {
			kfree(entry);
			return INVALID_EDIF_SA_INDEX;
		}

		INIT_LIST_HEAD(&entry->next);
		entry->handle = nport_handle;
		entry->fcport = fcport;
		entry->sa_pair[0].spi = sa_frame->spi;
		entry->sa_pair[0].sa_index = sa_index;
		entry->sa_pair[1].spi = 0;
		entry->sa_pair[1].sa_index = INVALID_EDIF_SA_INDEX;
		spin_lock_irqsave(&ha->sadb_lock, flags);
		list_add_tail(&entry->next, sa_list);
		spin_unlock_irqrestore(&ha->sadb_lock, flags);
		ql_dbg(ql_dbg_edif, vha, 0x3063,
		    "%s: Created new sadb entry for nport_handle 0x%x, spi 0x%x, returning sa_index %d\n",
		    __func__, nport_handle, sa_frame->spi, sa_index);

		return sa_index;
	}

	spin_lock_irqsave(&ha->sadb_lock, flags);

	/* see if we already have an entry for this spi */
	for (slot = 0; slot < 2; slot++) {
		if (entry->sa_pair[slot].sa_index == INVALID_EDIF_SA_INDEX) {
			free_slot = slot;
		} else {
			if (entry->sa_pair[slot].spi == sa_frame->spi) {
				spin_unlock_irqrestore(&ha->sadb_lock, flags);
				ql_dbg(ql_dbg_edif, vha, 0x3063,
				    "%s: sadb slot %d entry for lid 0x%x, spi 0x%x found, sa_index %d\n",
				    __func__, slot, entry->handle, sa_frame->spi,
				    entry->sa_pair[slot].sa_index);
				return entry->sa_pair[slot].sa_index;
			}
		}
	}
	spin_unlock_irqrestore(&ha->sadb_lock, flags);

	/* both slots are used */
	if (free_slot == -1) {
		ql_dbg(ql_dbg_edif, vha, 0x3063,
		    "%s: WARNING: No free slots in sadb for nport_handle 0x%x, spi: 0x%x\n",
		    __func__, entry->handle, sa_frame->spi);
		ql_dbg(ql_dbg_edif, vha, 0x3063,
		    "%s: Slot 0  spi: 0x%x  sa_index: %d,  Slot 1  spi: 0x%x  sa_index: %d\n",
		    __func__, entry->sa_pair[0].spi, entry->sa_pair[0].sa_index,
		    entry->sa_pair[1].spi, entry->sa_pair[1].sa_index);

		return INVALID_EDIF_SA_INDEX;
	}

	/* there is at least one free slot, use it */
	sa_index = qla_edif_get_sa_index_from_freepool(fcport, dir);
	if (sa_index == INVALID_EDIF_SA_INDEX) {
		ql_dbg(ql_dbg_edif, fcport->vha, 0x3063,
		    "%s: empty freepool!!\n", __func__);
		return INVALID_EDIF_SA_INDEX;
	}

	spin_lock_irqsave(&ha->sadb_lock, flags);
	entry->sa_pair[free_slot].spi = sa_frame->spi;
	entry->sa_pair[free_slot].sa_index = sa_index;
	spin_unlock_irqrestore(&ha->sadb_lock, flags);
	ql_dbg(ql_dbg_edif, fcport->vha, 0x3063,
	    "%s: sadb slot %d entry for nport_handle 0x%x, spi 0x%x added, returning sa_index %d\n",
	    __func__, free_slot, entry->handle, sa_frame->spi, sa_index);

	return sa_index;
}

/* release any sadb entries -- only done at teardown */
void qla_edif_sadb_release(struct qla_hw_data *ha)
{
	struct edif_sa_index_entry *entry, *tmp;

	list_for_each_entry_safe(entry, tmp, &ha->sadb_rx_index_list, next) {
		list_del(&entry->next);
		kfree(entry);
	}

	list_for_each_entry_safe(entry, tmp, &ha->sadb_tx_index_list, next) {
		list_del(&entry->next);
		kfree(entry);
	}
}

/**************************
 * sadb freepool functions
 **************************/

/* build the rx and tx sa_index free pools -- only done at fcport init */
int qla_edif_sadb_build_free_pool(struct qla_hw_data *ha)
{
	ha->edif_tx_sa_id_map =
	    kcalloc(BITS_TO_LONGS(EDIF_NUM_SA_INDEX), sizeof(long), GFP_KERNEL);

	if (!ha->edif_tx_sa_id_map) {
		ql_log_pci(ql_log_fatal, ha->pdev, 0x0009,
		    "Unable to allocate memory for sadb tx.\n");
		return -ENOMEM;
	}

	ha->edif_rx_sa_id_map =
	    kcalloc(BITS_TO_LONGS(EDIF_NUM_SA_INDEX), sizeof(long), GFP_KERNEL);
	if (!ha->edif_rx_sa_id_map) {
		kfree(ha->edif_tx_sa_id_map);
		ha->edif_tx_sa_id_map = NULL;
		ql_log_pci(ql_log_fatal, ha->pdev, 0x0009,
		    "Unable to allocate memory for sadb rx.\n");
		return -ENOMEM;
	}
	return 0;
}

/* release the free pool - only done during fcport teardown */
void qla_edif_sadb_release_free_pool(struct qla_hw_data *ha)
{
	kfree(ha->edif_tx_sa_id_map);
	ha->edif_tx_sa_id_map = NULL;
	kfree(ha->edif_rx_sa_id_map);
	ha->edif_rx_sa_id_map = NULL;
}

static void __chk_edif_rx_sa_delete_pending(scsi_qla_host_t *vha,
		fc_port_t *fcport, uint32_t handle, uint16_t sa_index)
{
	struct edif_list_entry *edif_entry;
	struct edif_sa_ctl *sa_ctl;
	uint16_t delete_sa_index = INVALID_EDIF_SA_INDEX;
	unsigned long flags = 0;
	uint16_t nport_handle = fcport->loop_id;
	uint16_t cached_nport_handle;

	spin_lock_irqsave(&fcport->edif.indx_list_lock, flags);
	edif_entry = qla_edif_list_find_sa_index(fcport, nport_handle);
	if (!edif_entry) {
		spin_unlock_irqrestore(&fcport->edif.indx_list_lock, flags);
		return;		/* no pending delete for this handle */
	}

	/*
	 * check for no pending delete for this index or iocb does not
	 * match rx sa_index
	 */
	if (edif_entry->delete_sa_index == INVALID_EDIF_SA_INDEX ||
	    edif_entry->update_sa_index != sa_index) {
		spin_unlock_irqrestore(&fcport->edif.indx_list_lock, flags);
		return;
	}

	/*
	 * wait until we have seen at least EDIF_DELAY_COUNT transfers before
	 * queueing RX delete
	 */
	if (edif_entry->count++ < EDIF_RX_DELETE_FILTER_COUNT) {
		spin_unlock_irqrestore(&fcport->edif.indx_list_lock, flags);
		return;
	}

	ql_dbg(ql_dbg_edif, vha, 0x5033,
	    "%s: invalidating delete_sa_index,  update_sa_index: 0x%x sa_index: 0x%x, delete_sa_index: 0x%x\n",
	    __func__, edif_entry->update_sa_index, sa_index, edif_entry->delete_sa_index);

	delete_sa_index = edif_entry->delete_sa_index;
	edif_entry->delete_sa_index = INVALID_EDIF_SA_INDEX;
	cached_nport_handle = edif_entry->handle;
	spin_unlock_irqrestore(&fcport->edif.indx_list_lock, flags);

	/* sanity check on the nport handle */
	if (nport_handle != cached_nport_handle) {
		ql_dbg(ql_dbg_edif, vha, 0x3063,
		    "%s: POST SA DELETE nport_handle mismatch: lid: 0x%x, edif_entry nph: 0x%x\n",
		    __func__, nport_handle, cached_nport_handle);
	}

	/* find the sa_ctl for the delete and schedule the delete */
	sa_ctl = qla_edif_find_sa_ctl_by_index(fcport, delete_sa_index, 0);
	if (sa_ctl) {
		ql_dbg(ql_dbg_edif, vha, 0x3063,
		    "%s: POST SA DELETE sa_ctl: %p, index recvd %d\n",
		    __func__, sa_ctl, sa_index);
		ql_dbg(ql_dbg_edif, vha, 0x3063,
		    "delete index %d, update index: %d, nport handle: 0x%x, handle: 0x%x\n",
		    delete_sa_index,
		    edif_entry->update_sa_index, nport_handle, handle);

		sa_ctl->flags = EDIF_SA_CTL_FLG_DEL;
		set_bit(EDIF_SA_CTL_REPL, &sa_ctl->state);
		qla_post_sa_replace_work(fcport->vha, fcport,
		    nport_handle, sa_ctl);
	} else {
		ql_dbg(ql_dbg_edif, vha, 0x3063,
		    "%s: POST SA DELETE sa_ctl not found for delete_sa_index: %d\n",
		    __func__, delete_sa_index);
	}
}

void qla_chk_edif_rx_sa_delete_pending(scsi_qla_host_t *vha,
		srb_t *sp, struct sts_entry_24xx *sts24)
{
	fc_port_t *fcport = sp->fcport;
	/* sa_index used by this iocb */
	struct scsi_cmnd *cmd = GET_CMD_SP(sp);
	uint32_t handle;

	handle = (uint32_t)LSW(sts24->handle);

	/* find out if this status iosb is for a scsi read */
	if (cmd->sc_data_direction != DMA_FROM_DEVICE)
		return;

	return __chk_edif_rx_sa_delete_pending(vha, fcport, handle,
	   le16_to_cpu(sts24->edif_sa_index));
}

void qlt_chk_edif_rx_sa_delete_pending(scsi_qla_host_t *vha, fc_port_t *fcport,
		struct ctio7_from_24xx *pkt)
{
	__chk_edif_rx_sa_delete_pending(vha, fcport,
	    pkt->handle, le16_to_cpu(pkt->edif_sa_index));
}

static void qla_parse_auth_els_ctl(struct srb *sp)
{
	struct qla_els_pt_arg *a = &sp->u.bsg_cmd.u.els_arg;
	struct bsg_job *bsg_job = sp->u.bsg_cmd.bsg_job;
	struct fc_bsg_request *request = bsg_job->request;
	struct qla_bsg_auth_els_request *p =
	    (struct qla_bsg_auth_els_request *)bsg_job->request;

	a->tx_len = a->tx_byte_count = sp->remap.req.len;
	a->tx_addr = sp->remap.req.dma;
	a->rx_len = a->rx_byte_count = sp->remap.rsp.len;
	a->rx_addr = sp->remap.rsp.dma;

	if (p->e.sub_cmd == SEND_ELS_REPLY) {
		a->control_flags = p->e.extra_control_flags << 13;
		a->rx_xchg_address = cpu_to_le32(p->e.extra_rx_xchg_address);
		if (p->e.extra_control_flags == BSG_CTL_FLAG_LS_ACC)
			a->els_opcode = ELS_LS_ACC;
		else if (p->e.extra_control_flags == BSG_CTL_FLAG_LS_RJT)
			a->els_opcode = ELS_LS_RJT;
	}
	a->did = sp->fcport->d_id;
	a->els_opcode =  request->rqst_data.h_els.command_code;
	a->nport_handle = cpu_to_le16(sp->fcport->loop_id);
	a->vp_idx = sp->vha->vp_idx;
}

int qla_edif_process_els(scsi_qla_host_t *vha, struct bsg_job *bsg_job)
{
	struct fc_bsg_request *bsg_request = bsg_job->request;
	struct fc_bsg_reply *bsg_reply = bsg_job->reply;
	fc_port_t *fcport = NULL;
	struct qla_hw_data *ha = vha->hw;
	srb_t *sp;
	int rval =  (DID_ERROR << 16), cnt;
	port_id_t d_id;
	struct qla_bsg_auth_els_request *p =
	    (struct qla_bsg_auth_els_request *)bsg_job->request;
	struct qla_bsg_auth_els_reply *rpl =
	    (struct qla_bsg_auth_els_reply *)bsg_job->reply;

	rpl->version = EDIF_VERSION1;

	d_id.b.al_pa = bsg_request->rqst_data.h_els.port_id[2];
	d_id.b.area = bsg_request->rqst_data.h_els.port_id[1];
	d_id.b.domain = bsg_request->rqst_data.h_els.port_id[0];

	/* find matching d_id in fcport list */
	fcport = qla2x00_find_fcport_by_pid(vha, &d_id);
	if (!fcport) {
		ql_dbg(ql_dbg_edif, vha, 0x911a,
		    "%s fcport not find online portid=%06x.\n",
		    __func__, d_id.b24);
		SET_DID_STATUS(bsg_reply->result, DID_ERROR);
		return -EIO;
	}

	if (qla_bsg_check(vha, bsg_job, fcport))
		return 0;

	if (EDIF_SESS_DELETE(fcport)) {
		ql_dbg(ql_dbg_edif, vha, 0x910d,
		    "%s ELS code %x, no loop id.\n", __func__,
		    bsg_request->rqst_data.r_els.els_code);
		SET_DID_STATUS(bsg_reply->result, DID_BAD_TARGET);
		return -ENXIO;
	}

	if (!vha->flags.online) {
		ql_log(ql_log_warn, vha, 0x7005, "Host not online.\n");
		SET_DID_STATUS(bsg_reply->result, DID_BAD_TARGET);
		rval = -EIO;
		goto done;
	}

	/* pass through is supported only for ISP 4Gb or higher */
	if (!IS_FWI2_CAPABLE(ha)) {
		ql_dbg(ql_dbg_user, vha, 0x7001,
		    "ELS passthru not supported for ISP23xx based adapters.\n");
		SET_DID_STATUS(bsg_reply->result, DID_BAD_TARGET);
		rval = -EPERM;
		goto done;
	}

	sp = qla2x00_get_sp(vha, fcport, GFP_KERNEL);
	if (!sp) {
		ql_dbg(ql_dbg_user, vha, 0x7004,
		    "Failed get sp pid=%06x\n", fcport->d_id.b24);
		rval = -ENOMEM;
		SET_DID_STATUS(bsg_reply->result, DID_IMM_RETRY);
		goto done;
	}

	sp->remap.req.len = bsg_job->request_payload.payload_len;
	sp->remap.req.buf = dma_pool_alloc(ha->purex_dma_pool,
	    GFP_KERNEL, &sp->remap.req.dma);
	if (!sp->remap.req.buf) {
		ql_dbg(ql_dbg_user, vha, 0x7005,
		    "Failed allocate request dma len=%x\n",
		    bsg_job->request_payload.payload_len);
		rval = -ENOMEM;
		SET_DID_STATUS(bsg_reply->result, DID_IMM_RETRY);
		goto done_free_sp;
	}

	sp->remap.rsp.len = bsg_job->reply_payload.payload_len;
	sp->remap.rsp.buf = dma_pool_alloc(ha->purex_dma_pool,
	    GFP_KERNEL, &sp->remap.rsp.dma);
	if (!sp->remap.rsp.buf) {
		ql_dbg(ql_dbg_user, vha, 0x7006,
		    "Failed allocate response dma len=%x\n",
		    bsg_job->reply_payload.payload_len);
		rval = -ENOMEM;
		SET_DID_STATUS(bsg_reply->result, DID_IMM_RETRY);
		goto done_free_remap_req;
	}
	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
	    bsg_job->request_payload.sg_cnt, sp->remap.req.buf,
	    sp->remap.req.len);
	sp->remap.remapped = true;

	sp->type = SRB_ELS_CMD_HST_NOLOGIN;
	sp->name = "SPCN_BSG_HST_NOLOGIN";
	sp->u.bsg_cmd.bsg_job = bsg_job;
	qla_parse_auth_els_ctl(sp);

	sp->free = qla2x00_bsg_sp_free;
	sp->done = qla2x00_bsg_job_done;

	cnt = 0;
retry:
	rval = qla2x00_start_sp(sp);
	switch (rval) {
	case QLA_SUCCESS:
		ql_dbg(ql_dbg_edif, vha, 0x700a,
		       "%s %s %8phN xchg %x ctlflag %x hdl %x reqlen %xh bsg ptr %p\n",
		       __func__, sc_to_str(p->e.sub_cmd), fcport->port_name,
		       p->e.extra_rx_xchg_address, p->e.extra_control_flags,
		       sp->handle, sp->remap.req.len, bsg_job);
		break;
	case -EAGAIN:
		msleep(EDIF_MSLEEP_INTERVAL);
		cnt++;
		if (cnt < EDIF_RETRY_COUNT)
			goto retry;
		fallthrough;
	default:
		ql_log(ql_log_warn, vha, 0x700e,
		    "%s qla2x00_start_sp failed = %d\n", __func__, rval);
		SET_DID_STATUS(bsg_reply->result, DID_IMM_RETRY);
		rval = -EIO;
		goto done_free_remap_rsp;
	}
	return rval;

done_free_remap_rsp:
	dma_pool_free(ha->purex_dma_pool, sp->remap.rsp.buf,
	    sp->remap.rsp.dma);
done_free_remap_req:
	dma_pool_free(ha->purex_dma_pool, sp->remap.req.buf,
	    sp->remap.req.dma);
done_free_sp:
	qla2x00_rel_sp(sp);

done:
	return rval;
}

void qla_edif_sess_down(struct scsi_qla_host *vha, struct fc_port *sess)
{
	u16 cnt = 0;

	if (sess->edif.app_sess_online && DBELL_ACTIVE(vha)) {
		ql_dbg(ql_dbg_disc, vha, 0xf09c,
			"%s: sess %8phN send port_offline event\n",
			__func__, sess->port_name);
		sess->edif.app_sess_online = 0;
		sess->edif.sess_down_acked = 0;
		qla_edb_eventcreate(vha, VND_CMD_AUTH_STATE_SESSION_SHUTDOWN,
		    sess->d_id.b24, 0, sess);
		qla2x00_post_aen_work(vha, FCH_EVT_PORT_OFFLINE, sess->d_id.b24);

		while (!READ_ONCE(sess->edif.sess_down_acked) &&
		       !test_bit(VPORT_DELETE, &vha->dpc_flags)) {
			msleep(100);
			cnt++;
			if (cnt > 100)
				break;
		}
		sess->edif.sess_down_acked = 0;
		ql_dbg(ql_dbg_disc, vha, 0xf09c,
		       "%s: sess %8phN port_offline event completed\n",
		       __func__, sess->port_name);
	}
}

void qla_edif_clear_appdata(struct scsi_qla_host *vha, struct fc_port *fcport)
{
	if (!(fcport->flags & FCF_FCSP_DEVICE))
		return;

	qla_edb_clear(vha, fcport->d_id);
	qla_enode_clear(vha, fcport->d_id);
}
