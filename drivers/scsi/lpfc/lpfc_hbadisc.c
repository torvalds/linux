/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2004-2007 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.emulex.com                                                  *
 * Portions Copyright (C) 2004-2005 Christoph Hellwig              *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of version 2 of the GNU General       *
 * Public License as published by the Free Software Foundation.    *
 * This program is distributed in the hope that it will be useful. *
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND          *
 * WARRANTIES, INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,  *
 * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE      *
 * DISCLAIMED, EXCEPT TO THE EXTENT THAT SUCH DISCLAIMERS ARE HELD *
 * TO BE LEGALLY INVALID.  See the GNU General Public License for  *
 * more details, a copy of which can be found in the file COPYING  *
 * included with this package.                                     *
 *******************************************************************/

#include <linux/blkdev.h>
#include <linux/pci.h>
#include <linux/kthread.h>
#include <linux/interrupt.h>

#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport_fc.h>

#include "lpfc_hw.h"
#include "lpfc_disc.h"
#include "lpfc_sli.h"
#include "lpfc_scsi.h"
#include "lpfc.h"
#include "lpfc_logmsg.h"
#include "lpfc_crtn.h"
#include "lpfc_vport.h"
#include "lpfc_debugfs.h"

/* AlpaArray for assignment of scsid for scan-down and bind_method */
static uint8_t lpfcAlpaArray[] = {
	0xEF, 0xE8, 0xE4, 0xE2, 0xE1, 0xE0, 0xDC, 0xDA, 0xD9, 0xD6,
	0xD5, 0xD4, 0xD3, 0xD2, 0xD1, 0xCE, 0xCD, 0xCC, 0xCB, 0xCA,
	0xC9, 0xC7, 0xC6, 0xC5, 0xC3, 0xBC, 0xBA, 0xB9, 0xB6, 0xB5,
	0xB4, 0xB3, 0xB2, 0xB1, 0xAE, 0xAD, 0xAC, 0xAB, 0xAA, 0xA9,
	0xA7, 0xA6, 0xA5, 0xA3, 0x9F, 0x9E, 0x9D, 0x9B, 0x98, 0x97,
	0x90, 0x8F, 0x88, 0x84, 0x82, 0x81, 0x80, 0x7C, 0x7A, 0x79,
	0x76, 0x75, 0x74, 0x73, 0x72, 0x71, 0x6E, 0x6D, 0x6C, 0x6B,
	0x6A, 0x69, 0x67, 0x66, 0x65, 0x63, 0x5C, 0x5A, 0x59, 0x56,
	0x55, 0x54, 0x53, 0x52, 0x51, 0x4E, 0x4D, 0x4C, 0x4B, 0x4A,
	0x49, 0x47, 0x46, 0x45, 0x43, 0x3C, 0x3A, 0x39, 0x36, 0x35,
	0x34, 0x33, 0x32, 0x31, 0x2E, 0x2D, 0x2C, 0x2B, 0x2A, 0x29,
	0x27, 0x26, 0x25, 0x23, 0x1F, 0x1E, 0x1D, 0x1B, 0x18, 0x17,
	0x10, 0x0F, 0x08, 0x04, 0x02, 0x01
};

static void lpfc_disc_timeout_handler(struct lpfc_vport *);

void
lpfc_terminate_rport_io(struct fc_rport *rport)
{
	struct lpfc_rport_data *rdata;
	struct lpfc_nodelist * ndlp;
	struct lpfc_hba *phba;

	rdata = rport->dd_data;
	ndlp = rdata->pnode;

	if (!ndlp) {
		if (rport->roles & FC_RPORT_ROLE_FCP_TARGET)
			printk(KERN_ERR "Cannot find remote node"
			" to terminate I/O Data x%x\n",
			rport->port_id);
		return;
	}

	phba  = ndlp->vport->phba;

	lpfc_debugfs_disc_trc(ndlp->vport, LPFC_DISC_TRC_RPORT,
		"rport terminate: sid:x%x did:x%x flg:x%x",
		ndlp->nlp_sid, ndlp->nlp_DID, ndlp->nlp_flag);

	if (ndlp->nlp_sid != NLP_NO_SID) {
		lpfc_sli_abort_iocb(phba, &phba->sli.ring[phba->sli.fcp_ring],
			ndlp->nlp_sid, 0, 0, LPFC_CTX_TGT);
	}

	return;
}

/*
 * This function will be called when dev_loss_tmo fire.
 */
void
lpfc_dev_loss_tmo_callbk(struct fc_rport *rport)
{
	struct lpfc_rport_data *rdata;
	struct lpfc_nodelist * ndlp;
	struct lpfc_vport *vport;
	struct lpfc_hba   *phba;
	struct completion devloss_compl;
	struct lpfc_work_evt *evtp;

	rdata = rport->dd_data;
	ndlp = rdata->pnode;

	if (!ndlp) {
		if (rport->scsi_target_id != -1) {
			printk(KERN_ERR "Cannot find remote node"
				" for rport in dev_loss_tmo_callbk x%x\n",
				rport->port_id);
		}
		return;
	}

	vport = ndlp->vport;
	phba  = vport->phba;

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_RPORT,
		"rport devlosscb: sid:x%x did:x%x flg:x%x",
		ndlp->nlp_sid, ndlp->nlp_DID, ndlp->nlp_flag);

	init_completion(&devloss_compl);
	evtp = &ndlp->dev_loss_evt;

	if (!list_empty(&evtp->evt_listp))
		return;

	spin_lock_irq(&phba->hbalock);
	evtp->evt_arg1  = ndlp;
	evtp->evt_arg2  = &devloss_compl;
	evtp->evt       = LPFC_EVT_DEV_LOSS;
	list_add_tail(&evtp->evt_listp, &phba->work_list);
	if (phba->work_wait)
		wake_up(phba->work_wait);

	spin_unlock_irq(&phba->hbalock);

	wait_for_completion(&devloss_compl);

	return;
}

/*
 * This function is called from the worker thread when dev_loss_tmo
 * expire.
 */
void
lpfc_dev_loss_tmo_handler(struct lpfc_nodelist *ndlp)
{
	struct lpfc_rport_data *rdata;
	struct fc_rport   *rport;
	struct lpfc_vport *vport;
	struct lpfc_hba   *phba;
	uint8_t *name;
	int warn_on = 0;

	rport = ndlp->rport;

	if (!rport)
		return;

	rdata = rport->dd_data;
	name = (uint8_t *) &ndlp->nlp_portname;
	vport = ndlp->vport;
	phba  = vport->phba;

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_RPORT,
		"rport devlosstmo:did:x%x type:x%x id:x%x",
		ndlp->nlp_DID, ndlp->nlp_type, rport->scsi_target_id);

	if (!(vport->load_flag & FC_UNLOADING) &&
	    ndlp->nlp_state == NLP_STE_MAPPED_NODE)
		return;

	if (ndlp->nlp_type & NLP_FABRIC) {
		int  put_node;
		int  put_rport;

		/* We will clean up these Nodes in linkup */
		put_node = rdata->pnode != NULL;
		put_rport = ndlp->rport != NULL;
		rdata->pnode = NULL;
		ndlp->rport = NULL;
		if (put_node)
			lpfc_nlp_put(ndlp);
		if (put_rport)
			put_device(&rport->dev);
		return;
	}

	if (ndlp->nlp_sid != NLP_NO_SID) {
		warn_on = 1;
		/* flush the target */
		lpfc_sli_abort_iocb(phba, &phba->sli.ring[phba->sli.fcp_ring],
				    ndlp->nlp_sid, 0, 0, LPFC_CTX_TGT);
	}
	if (vport->load_flag & FC_UNLOADING)
		warn_on = 0;

	if (warn_on) {
		lpfc_printf_log(phba, KERN_ERR, LOG_DISCOVERY,
				"%d (%d):0203 Devloss timeout on "
				"WWPN %x:%x:%x:%x:%x:%x:%x:%x "
				"NPort x%x Data: x%x x%x x%x\n",
				phba->brd_no, vport->vpi,
				*name, *(name+1), *(name+2), *(name+3),
				*(name+4), *(name+5), *(name+6), *(name+7),
				ndlp->nlp_DID, ndlp->nlp_flag,
				ndlp->nlp_state, ndlp->nlp_rpi);
	} else {
		lpfc_printf_log(phba, KERN_INFO, LOG_DISCOVERY,
				"%d (%d):0204 Devloss timeout on "
				"WWPN %x:%x:%x:%x:%x:%x:%x:%x "
				"NPort x%x Data: x%x x%x x%x\n",
				phba->brd_no, vport->vpi,
				*name, *(name+1), *(name+2), *(name+3),
				*(name+4), *(name+5), *(name+6), *(name+7),
				ndlp->nlp_DID, ndlp->nlp_flag,
				ndlp->nlp_state, ndlp->nlp_rpi);
	}

	if (!(vport->load_flag & FC_UNLOADING) &&
	    !(ndlp->nlp_flag & NLP_DELAY_TMO) &&
	    !(ndlp->nlp_flag & NLP_NPR_2B_DISC) &&
	    (ndlp->nlp_state != NLP_STE_UNMAPPED_NODE))
		lpfc_disc_state_machine(vport, ndlp, NULL, NLP_EVT_DEVICE_RM);
	else {
		int  put_node;
		int  put_rport;

		put_node = rdata->pnode != NULL;
		put_rport = ndlp->rport != NULL;
		rdata->pnode = NULL;
		ndlp->rport = NULL;
		if (put_node)
			lpfc_nlp_put(ndlp);
		if (put_rport)
			put_device(&rport->dev);
	}
}


void
lpfc_worker_wake_up(struct lpfc_hba *phba)
{
	wake_up(phba->work_wait);
	return;
}

static void
lpfc_work_list_done(struct lpfc_hba *phba)
{
	struct lpfc_work_evt  *evtp = NULL;
	struct lpfc_nodelist  *ndlp;
	struct lpfc_vport     *vport;
	int free_evt;

	spin_lock_irq(&phba->hbalock);
	while (!list_empty(&phba->work_list)) {
		list_remove_head((&phba->work_list), evtp, typeof(*evtp),
				 evt_listp);
		spin_unlock_irq(&phba->hbalock);
		free_evt = 1;
		switch (evtp->evt) {
		case LPFC_EVT_DEV_LOSS_DELAY:
			free_evt = 0; /* evt is part of ndlp */
			ndlp = (struct lpfc_nodelist *) (evtp->evt_arg1);
			vport = ndlp->vport;
			if (!vport)
				break;

			lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_RPORT,
				"rport devlossdly:did:x%x flg:x%x",
				ndlp->nlp_DID, ndlp->nlp_flag, 0);

			if (!(vport->load_flag & FC_UNLOADING) &&
			    !(ndlp->nlp_flag & NLP_DELAY_TMO) &&
			    !(ndlp->nlp_flag & NLP_NPR_2B_DISC)) {
				lpfc_disc_state_machine(vport, ndlp, NULL,
					NLP_EVT_DEVICE_RM);
			}
			break;
		case LPFC_EVT_ELS_RETRY:
			ndlp = (struct lpfc_nodelist *) (evtp->evt_arg1);
			lpfc_els_retry_delay_handler(ndlp);
			free_evt = 0; /* evt is part of ndlp */
			break;
		case LPFC_EVT_DEV_LOSS:
			ndlp = (struct lpfc_nodelist *)(evtp->evt_arg1);
			lpfc_nlp_get(ndlp);
			lpfc_dev_loss_tmo_handler(ndlp);
			free_evt = 0;
			complete((struct completion *)(evtp->evt_arg2));
			lpfc_nlp_put(ndlp);
			break;
		case LPFC_EVT_ONLINE:
			if (phba->link_state < LPFC_LINK_DOWN)
				*(int *) (evtp->evt_arg1) = lpfc_online(phba);
			else
				*(int *) (evtp->evt_arg1) = 0;
			complete((struct completion *)(evtp->evt_arg2));
			break;
		case LPFC_EVT_OFFLINE_PREP:
			if (phba->link_state >= LPFC_LINK_DOWN)
				lpfc_offline_prep(phba);
			*(int *)(evtp->evt_arg1) = 0;
			complete((struct completion *)(evtp->evt_arg2));
			break;
		case LPFC_EVT_OFFLINE:
			lpfc_offline(phba);
			lpfc_sli_brdrestart(phba);
			*(int *)(evtp->evt_arg1) =
				lpfc_sli_brdready(phba, HS_FFRDY | HS_MBRDY);
			lpfc_unblock_mgmt_io(phba);
			complete((struct completion *)(evtp->evt_arg2));
			break;
		case LPFC_EVT_WARM_START:
			lpfc_offline(phba);
			lpfc_reset_barrier(phba);
			lpfc_sli_brdreset(phba);
			lpfc_hba_down_post(phba);
			*(int *)(evtp->evt_arg1) =
				lpfc_sli_brdready(phba, HS_MBRDY);
			lpfc_unblock_mgmt_io(phba);
			complete((struct completion *)(evtp->evt_arg2));
			break;
		case LPFC_EVT_KILL:
			lpfc_offline(phba);
			*(int *)(evtp->evt_arg1)
				= (phba->pport->stopped)
				        ? 0 : lpfc_sli_brdkill(phba);
			lpfc_unblock_mgmt_io(phba);
			complete((struct completion *)(evtp->evt_arg2));
			break;
		}
		if (free_evt)
			kfree(evtp);
		spin_lock_irq(&phba->hbalock);
	}
	spin_unlock_irq(&phba->hbalock);

}

void
lpfc_work_done(struct lpfc_hba *phba)
{
	struct lpfc_sli_ring *pring;
	uint32_t ha_copy, status, control, work_port_events;
	struct lpfc_vport *vport;

	spin_lock_irq(&phba->hbalock);
	ha_copy = phba->work_ha;
	phba->work_ha = 0;
	spin_unlock_irq(&phba->hbalock);

	if (ha_copy & HA_ERATT)
		lpfc_handle_eratt(phba);

	if (ha_copy & HA_MBATT)
		lpfc_sli_handle_mb_event(phba);

	if (ha_copy & HA_LATT)
		lpfc_handle_latt(phba);

	spin_lock_irq(&phba->hbalock);
	list_for_each_entry(vport, &phba->port_list, listentry) {
		struct Scsi_Host  *shost = lpfc_shost_from_vport(vport);

		if (!scsi_host_get(shost)) {
			continue;
		}
		spin_unlock_irq(&phba->hbalock);
		work_port_events = vport->work_port_events;

		if (work_port_events & WORKER_DISC_TMO)
			lpfc_disc_timeout_handler(vport);

		if (work_port_events & WORKER_ELS_TMO)
			lpfc_els_timeout_handler(vport);

		if (work_port_events & WORKER_HB_TMO)
			lpfc_hb_timeout_handler(phba);

		if (work_port_events & WORKER_MBOX_TMO)
			lpfc_mbox_timeout_handler(phba);

		if (work_port_events & WORKER_FABRIC_BLOCK_TMO)
			lpfc_unblock_fabric_iocbs(phba);

		if (work_port_events & WORKER_FDMI_TMO)
			lpfc_fdmi_timeout_handler(vport);

		if (work_port_events & WORKER_RAMP_DOWN_QUEUE)
			lpfc_ramp_down_queue_handler(phba);

		if (work_port_events & WORKER_RAMP_UP_QUEUE)
			lpfc_ramp_up_queue_handler(phba);

		spin_lock_irq(&vport->work_port_lock);
		vport->work_port_events &= ~work_port_events;
		spin_unlock_irq(&vport->work_port_lock);
		scsi_host_put(shost);
		spin_lock_irq(&phba->hbalock);
	}
	spin_unlock_irq(&phba->hbalock);

	pring = &phba->sli.ring[LPFC_ELS_RING];
	status = (ha_copy & (HA_RXMASK  << (4*LPFC_ELS_RING)));
	status >>= (4*LPFC_ELS_RING);
	if ((status & HA_RXMASK)
		|| (pring->flag & LPFC_DEFERRED_RING_EVENT)) {
		if (pring->flag & LPFC_STOP_IOCB_MASK) {
			pring->flag |= LPFC_DEFERRED_RING_EVENT;
		} else {
			lpfc_sli_handle_slow_ring_event(phba, pring,
							(status &
							 HA_RXMASK));
			pring->flag &= ~LPFC_DEFERRED_RING_EVENT;
		}
		/*
		 * Turn on Ring interrupts
		 */
		spin_lock_irq(&phba->hbalock);
		control = readl(phba->HCregaddr);
		if (!(control & (HC_R0INT_ENA << LPFC_ELS_RING))) {
			control |= (HC_R0INT_ENA << LPFC_ELS_RING);
			writel(control, phba->HCregaddr);
			readl(phba->HCregaddr); /* flush */
		}
		spin_unlock_irq(&phba->hbalock);
	}
	lpfc_work_list_done(phba);
}

static int
check_work_wait_done(struct lpfc_hba *phba)
{
	struct lpfc_vport *vport;
	struct lpfc_sli_ring *pring;
	int rc = 0;

	spin_lock_irq(&phba->hbalock);
	list_for_each_entry(vport, &phba->port_list, listentry) {
		if (vport->work_port_events) {
			rc = 1;
			goto exit;
		}
	}

	if (phba->work_ha || (!list_empty(&phba->work_list)) ||
	    kthread_should_stop()) {
		rc = 1;
		goto exit;
	}

	pring = &phba->sli.ring[LPFC_ELS_RING];
	if (pring->flag & LPFC_DEFERRED_RING_EVENT)
		rc = 1;
exit:
	if (rc)
		phba->work_found++;
	else
		phba->work_found = 0;

	spin_unlock_irq(&phba->hbalock);
	return rc;
}


int
lpfc_do_work(void *p)
{
	struct lpfc_hba *phba = p;
	int rc;
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(work_waitq);

	set_user_nice(current, -20);
	phba->work_wait = &work_waitq;
	phba->work_found = 0;

	while (1) {

		rc = wait_event_interruptible(work_waitq,
					      check_work_wait_done(phba));

		BUG_ON(rc);

		if (kthread_should_stop())
			break;

		lpfc_work_done(phba);

		/* If there is alot of slow ring work, like during link up
		 * check_work_wait_done() may cause this thread to not give
		 * up the CPU for very long periods of time. This may cause
		 * soft lockups or other problems. To avoid these situations
		 * give up the CPU here after LPFC_MAX_WORKER_ITERATION
		 * consecutive iterations.
		 */
		if (phba->work_found >= LPFC_MAX_WORKER_ITERATION) {
			phba->work_found = 0;
			schedule();
		}
	}
	phba->work_wait = NULL;
	return 0;
}

/*
 * This is only called to handle FC worker events. Since this a rare
 * occurance, we allocate a struct lpfc_work_evt structure here instead of
 * embedding it in the IOCB.
 */
int
lpfc_workq_post_event(struct lpfc_hba *phba, void *arg1, void *arg2,
		      uint32_t evt)
{
	struct lpfc_work_evt  *evtp;
	unsigned long flags;

	/*
	 * All Mailbox completions and LPFC_ELS_RING rcv ring IOCB events will
	 * be queued to worker thread for processing
	 */
	evtp = kmalloc(sizeof(struct lpfc_work_evt), GFP_ATOMIC);
	if (!evtp)
		return 0;

	evtp->evt_arg1  = arg1;
	evtp->evt_arg2  = arg2;
	evtp->evt       = evt;

	spin_lock_irqsave(&phba->hbalock, flags);
	list_add_tail(&evtp->evt_listp, &phba->work_list);
	if (phba->work_wait)
		lpfc_worker_wake_up(phba);
	spin_unlock_irqrestore(&phba->hbalock, flags);

	return 1;
}

void
lpfc_cleanup_rpis(struct lpfc_vport *vport, int remove)
{
	struct lpfc_hba  *phba = vport->phba;
	struct lpfc_nodelist *ndlp, *next_ndlp;
	int  rc;

	list_for_each_entry_safe(ndlp, next_ndlp, &vport->fc_nodes, nlp_listp) {
		if (ndlp->nlp_state == NLP_STE_UNUSED_NODE)
			continue;

		if (phba->sli3_options & LPFC_SLI3_VPORT_TEARDOWN)
			lpfc_unreg_rpi(vport, ndlp);

		/* Leave Fabric nodes alone on link down */
		if (!remove && ndlp->nlp_type & NLP_FABRIC)
			continue;
		rc = lpfc_disc_state_machine(vport, ndlp, NULL,
					     remove
					     ? NLP_EVT_DEVICE_RM
					     : NLP_EVT_DEVICE_RECOVERY);
	}
	if (phba->sli3_options & LPFC_SLI3_VPORT_TEARDOWN) {
		lpfc_mbx_unreg_vpi(vport);
		vport->fc_flag |= FC_VPORT_NEEDS_REG_VPI;
	}
}

static void
lpfc_linkdown_port(struct lpfc_vport *vport)
{
	struct lpfc_nodelist *ndlp, *next_ndlp;
	struct Scsi_Host  *shost = lpfc_shost_from_vport(vport);

	fc_host_post_event(shost, fc_get_event_number(), FCH_EVT_LINKDOWN, 0);

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_CMD,
		"Link Down:       state:x%x rtry:x%x flg:x%x",
		vport->port_state, vport->fc_ns_retry, vport->fc_flag);

	/* Cleanup any outstanding RSCN activity */
	lpfc_els_flush_rscn(vport);

	/* Cleanup any outstanding ELS commands */
	lpfc_els_flush_cmd(vport);

	lpfc_cleanup_rpis(vport, 0);

	/* free any ndlp's on unused list */
	list_for_each_entry_safe(ndlp, next_ndlp, &vport->fc_nodes, nlp_listp)
				/* free any ndlp's in unused state */
		if (ndlp->nlp_state == NLP_STE_UNUSED_NODE)
			lpfc_drop_node(vport, ndlp);

	/* Turn off discovery timer if its running */
	lpfc_can_disctmo(vport);
}

int
lpfc_linkdown(struct lpfc_hba *phba)
{
	struct lpfc_vport *vport = phba->pport;
	struct Scsi_Host  *shost = lpfc_shost_from_vport(vport);
	struct lpfc_vport *port_iterator;
	LPFC_MBOXQ_t          *mb;

	if (phba->link_state == LPFC_LINK_DOWN) {
		return 0;
	}
	spin_lock_irq(&phba->hbalock);
	if (phba->link_state > LPFC_LINK_DOWN) {
		phba->link_state = LPFC_LINK_DOWN;
		phba->pport->fc_flag &= ~FC_LBIT;
	}
	spin_unlock_irq(&phba->hbalock);

	list_for_each_entry(port_iterator, &phba->port_list, listentry) {

				/* Issue a LINK DOWN event to all nodes */
		lpfc_linkdown_port(port_iterator);
	}

	/* Clean up any firmware default rpi's */
	mb = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (mb) {
		lpfc_unreg_did(phba, 0xffff, 0xffffffff, mb);
		mb->vport = vport;
		mb->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
		if (lpfc_sli_issue_mbox(phba, mb, (MBX_NOWAIT | MBX_STOP_IOCB))
		    == MBX_NOT_FINISHED) {
			mempool_free(mb, phba->mbox_mem_pool);
		}
	}

	/* Setup myDID for link up if we are in pt2pt mode */
	if (phba->pport->fc_flag & FC_PT2PT) {
		phba->pport->fc_myDID = 0;
		mb = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
		if (mb) {
			lpfc_config_link(phba, mb);
			mb->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
			mb->vport = vport;
			if (lpfc_sli_issue_mbox(phba, mb,
						(MBX_NOWAIT | MBX_STOP_IOCB))
			    == MBX_NOT_FINISHED) {
				mempool_free(mb, phba->mbox_mem_pool);
			}
		}
		spin_lock_irq(shost->host_lock);
		phba->pport->fc_flag &= ~(FC_PT2PT | FC_PT2PT_PLOGI);
		spin_unlock_irq(shost->host_lock);
	}

	return 0;
}

static void
lpfc_linkup_cleanup_nodes(struct lpfc_vport *vport)
{
	struct lpfc_nodelist *ndlp;

	list_for_each_entry(ndlp, &vport->fc_nodes, nlp_listp) {
		if (ndlp->nlp_state == NLP_STE_UNUSED_NODE)
			continue;

		if (ndlp->nlp_type & NLP_FABRIC) {
				/* On Linkup its safe to clean up the ndlp
				 * from Fabric connections.
				 */
			if (ndlp->nlp_DID != Fabric_DID)
				lpfc_unreg_rpi(vport, ndlp);
			lpfc_nlp_set_state(vport, ndlp, NLP_STE_NPR_NODE);
		} else if (!(ndlp->nlp_flag & NLP_NPR_ADISC)) {
				/* Fail outstanding IO now since device is
				 * marked for PLOGI.
				 */
			lpfc_unreg_rpi(vport, ndlp);
		}
	}
}

static void
lpfc_linkup_port(struct lpfc_vport *vport)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct lpfc_nodelist *ndlp, *next_ndlp;
	struct lpfc_hba  *phba = vport->phba;

	if ((vport->load_flag & FC_UNLOADING) != 0)
		return;

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_CMD,
		"Link Up:         top:x%x speed:x%x flg:x%x",
		phba->fc_topology, phba->fc_linkspeed, phba->link_flag);

	/* If NPIV is not enabled, only bring the physical port up */
	if (!(phba->sli3_options & LPFC_SLI3_NPIV_ENABLED) &&
		(vport != phba->pport))
		return;

	fc_host_post_event(shost, fc_get_event_number(), FCH_EVT_LINKUP, 0);

	spin_lock_irq(shost->host_lock);
	vport->fc_flag &= ~(FC_PT2PT | FC_PT2PT_PLOGI | FC_ABORT_DISCOVERY |
			    FC_RSCN_MODE | FC_NLP_MORE | FC_RSCN_DISCOVERY);
	vport->fc_flag |= FC_NDISC_ACTIVE;
	vport->fc_ns_retry = 0;
	spin_unlock_irq(shost->host_lock);

	if (vport->fc_flag & FC_LBIT)
		lpfc_linkup_cleanup_nodes(vport);

				/* free any ndlp's in unused state */
	list_for_each_entry_safe(ndlp, next_ndlp, &vport->fc_nodes,
				 nlp_listp)
		if (ndlp->nlp_state == NLP_STE_UNUSED_NODE)
			lpfc_drop_node(vport, ndlp);
}

static int
lpfc_linkup(struct lpfc_hba *phba)
{
	struct lpfc_vport *vport;

	phba->link_state = LPFC_LINK_UP;

	/* Unblock fabric iocbs if they are blocked */
	clear_bit(FABRIC_COMANDS_BLOCKED, &phba->bit_flags);
	del_timer_sync(&phba->fabric_block_timer);

	list_for_each_entry(vport, &phba->port_list, listentry) {
		lpfc_linkup_port(vport);
	}
	if (phba->sli3_options & LPFC_SLI3_NPIV_ENABLED)
		lpfc_issue_clear_la(phba, phba->pport);

	return 0;
}

/*
 * This routine handles processing a CLEAR_LA mailbox
 * command upon completion. It is setup in the LPFC_MBOXQ
 * as the completion routine when the command is
 * handed off to the SLI layer.
 */
void
lpfc_mbx_cmpl_clear_la(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmb)
{
	struct lpfc_vport *vport = pmb->vport;
	struct Scsi_Host  *shost = lpfc_shost_from_vport(vport);
	struct lpfc_sli   *psli = &phba->sli;
	MAILBOX_t *mb = &pmb->mb;
	uint32_t control;

	/* Since we don't do discovery right now, turn these off here */
	psli->ring[psli->extra_ring].flag &= ~LPFC_STOP_IOCB_EVENT;
	psli->ring[psli->fcp_ring].flag &= ~LPFC_STOP_IOCB_EVENT;
	psli->ring[psli->next_ring].flag &= ~LPFC_STOP_IOCB_EVENT;

	/* Check for error */
	if ((mb->mbxStatus) && (mb->mbxStatus != 0x1601)) {
		/* CLEAR_LA mbox error <mbxStatus> state <hba_state> */
		lpfc_printf_log(phba, KERN_ERR, LOG_MBOX,
				"%d (%d):0320 CLEAR_LA mbxStatus error x%x hba "
				"state x%x\n",
				phba->brd_no, vport->vpi, mb->mbxStatus,
				vport->port_state);

		phba->link_state = LPFC_HBA_ERROR;
		goto out;
	}

	if (vport->port_type == LPFC_PHYSICAL_PORT)
		phba->link_state = LPFC_HBA_READY;

	spin_lock_irq(&phba->hbalock);
	psli->sli_flag |= LPFC_PROCESS_LA;
	control = readl(phba->HCregaddr);
	control |= HC_LAINT_ENA;
	writel(control, phba->HCregaddr);
	readl(phba->HCregaddr); /* flush */
	spin_unlock_irq(&phba->hbalock);
	return;

	vport->num_disc_nodes = 0;
	/* go thru NPR nodes and issue ELS PLOGIs */
	if (vport->fc_npr_cnt)
		lpfc_els_disc_plogi(vport);

	if (!vport->num_disc_nodes) {
		spin_lock_irq(shost->host_lock);
		vport->fc_flag &= ~FC_NDISC_ACTIVE;
		spin_unlock_irq(shost->host_lock);
	}

	vport->port_state = LPFC_VPORT_READY;

out:
	/* Device Discovery completes */
	lpfc_printf_log(phba, KERN_INFO, LOG_DISCOVERY,
			"%d (%d):0225 Device Discovery completes\n",
			phba->brd_no, vport->vpi);

	mempool_free(pmb, phba->mbox_mem_pool);

	spin_lock_irq(shost->host_lock);
	vport->fc_flag &= ~(FC_ABORT_DISCOVERY | FC_ESTABLISH_LINK);
	spin_unlock_irq(shost->host_lock);

	del_timer_sync(&phba->fc_estabtmo);

	lpfc_can_disctmo(vport);

	/* turn on Link Attention interrupts */

	spin_lock_irq(&phba->hbalock);
	psli->sli_flag |= LPFC_PROCESS_LA;
	control = readl(phba->HCregaddr);
	control |= HC_LAINT_ENA;
	writel(control, phba->HCregaddr);
	readl(phba->HCregaddr); /* flush */
	spin_unlock_irq(&phba->hbalock);

	return;
}


static void
lpfc_mbx_cmpl_local_config_link(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmb)
{
	struct lpfc_vport *vport = pmb->vport;

	if (pmb->mb.mbxStatus)
		goto out;

	mempool_free(pmb, phba->mbox_mem_pool);

	if (phba->fc_topology == TOPOLOGY_LOOP &&
	    vport->fc_flag & FC_PUBLIC_LOOP &&
	    !(vport->fc_flag & FC_LBIT)) {
			/* Need to wait for FAN - use discovery timer
			 * for timeout.  port_state is identically
			 * LPFC_LOCAL_CFG_LINK while waiting for FAN
			 */
			lpfc_set_disctmo(vport);
			return;
	}

	/* Start discovery by sending a FLOGI. port_state is identically
	 * LPFC_FLOGI while waiting for FLOGI cmpl
	 */
	if (vport->port_state != LPFC_FLOGI) {
		vport->port_state = LPFC_FLOGI;
		lpfc_set_disctmo(vport);
		lpfc_initial_flogi(vport);
	}
	return;

out:
	lpfc_printf_log(phba, KERN_ERR, LOG_MBOX,
			"%d (%d):0306 CONFIG_LINK mbxStatus error x%x "
			"HBA state x%x\n",
			phba->brd_no, vport->vpi, pmb->mb.mbxStatus,
			vport->port_state);

	mempool_free(pmb, phba->mbox_mem_pool);

	lpfc_linkdown(phba);

	lpfc_printf_log(phba, KERN_ERR, LOG_DISCOVERY,
			"%d (%d):0200 CONFIG_LINK bad hba state x%x\n",
			phba->brd_no, vport->vpi, vport->port_state);

	lpfc_issue_clear_la(phba, vport);
	return;
}

static void
lpfc_mbx_cmpl_read_sparam(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmb)
{
	MAILBOX_t *mb = &pmb->mb;
	struct lpfc_dmabuf *mp = (struct lpfc_dmabuf *) pmb->context1;
	struct lpfc_vport  *vport = pmb->vport;


	/* Check for error */
	if (mb->mbxStatus) {
		/* READ_SPARAM mbox error <mbxStatus> state <hba_state> */
		lpfc_printf_log(phba, KERN_ERR, LOG_MBOX,
				"%d (%d):0319 READ_SPARAM mbxStatus error x%x "
				"hba state x%x>\n",
				phba->brd_no, vport->vpi, mb->mbxStatus,
				vport->port_state);

		lpfc_linkdown(phba);
		goto out;
	}

	memcpy((uint8_t *) &vport->fc_sparam, (uint8_t *) mp->virt,
	       sizeof (struct serv_parm));
	if (phba->cfg_soft_wwnn)
		u64_to_wwn(phba->cfg_soft_wwnn,
			   vport->fc_sparam.nodeName.u.wwn);
	if (phba->cfg_soft_wwpn)
		u64_to_wwn(phba->cfg_soft_wwpn,
			   vport->fc_sparam.portName.u.wwn);
	memcpy(&vport->fc_nodename, &vport->fc_sparam.nodeName,
	       sizeof(vport->fc_nodename));
	memcpy(&vport->fc_portname, &vport->fc_sparam.portName,
	       sizeof(vport->fc_portname));
	if (vport->port_type == LPFC_PHYSICAL_PORT) {
		memcpy(&phba->wwnn, &vport->fc_nodename, sizeof(phba->wwnn));
		memcpy(&phba->wwpn, &vport->fc_portname, sizeof(phba->wwnn));
	}

	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
	mempool_free(pmb, phba->mbox_mem_pool);
	return;

out:
	pmb->context1 = NULL;
	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
	lpfc_issue_clear_la(phba, vport);
	mempool_free(pmb, phba->mbox_mem_pool);
	return;
}

static void
lpfc_mbx_process_link_up(struct lpfc_hba *phba, READ_LA_VAR *la)
{
	struct lpfc_vport *vport = phba->pport;
	LPFC_MBOXQ_t *sparam_mbox, *cfglink_mbox;
	int i;
	struct lpfc_dmabuf *mp;
	int rc;

	sparam_mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	cfglink_mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);

	spin_lock_irq(&phba->hbalock);
	switch (la->UlnkSpeed) {
	case LA_1GHZ_LINK:
		phba->fc_linkspeed = LA_1GHZ_LINK;
		break;
	case LA_2GHZ_LINK:
		phba->fc_linkspeed = LA_2GHZ_LINK;
		break;
	case LA_4GHZ_LINK:
		phba->fc_linkspeed = LA_4GHZ_LINK;
		break;
	case LA_8GHZ_LINK:
		phba->fc_linkspeed = LA_8GHZ_LINK;
		break;
	default:
		phba->fc_linkspeed = LA_UNKNW_LINK;
		break;
	}

	phba->fc_topology = la->topology;
	phba->link_flag &= ~LS_NPIV_FAB_SUPPORTED;

	if (phba->fc_topology == TOPOLOGY_LOOP) {
		phba->sli3_options &= ~LPFC_SLI3_NPIV_ENABLED;

				/* Get Loop Map information */
		if (la->il)
			vport->fc_flag |= FC_LBIT;

		vport->fc_myDID = la->granted_AL_PA;
		i = la->un.lilpBde64.tus.f.bdeSize;

		if (i == 0) {
			phba->alpa_map[0] = 0;
		} else {
			if (phba->cfg_log_verbose & LOG_LINK_EVENT) {
				int numalpa, j, k;
				union {
					uint8_t pamap[16];
					struct {
						uint32_t wd1;
						uint32_t wd2;
						uint32_t wd3;
						uint32_t wd4;
					} pa;
				} un;
				numalpa = phba->alpa_map[0];
				j = 0;
				while (j < numalpa) {
					memset(un.pamap, 0, 16);
					for (k = 1; j < numalpa; k++) {
						un.pamap[k - 1] =
							phba->alpa_map[j + 1];
						j++;
						if (k == 16)
							break;
					}
					/* Link Up Event ALPA map */
					lpfc_printf_log(phba,
							KERN_WARNING,
							LOG_LINK_EVENT,
							"%d:1304 Link Up Event "
							"ALPA map Data: x%x "
							"x%x x%x x%x\n",
							phba->brd_no,
							un.pa.wd1, un.pa.wd2,
							un.pa.wd3, un.pa.wd4);
				}
			}
		}
	} else {
		if (!(phba->sli3_options & LPFC_SLI3_NPIV_ENABLED)) {
			if (phba->max_vpi && phba->cfg_npiv_enable &&
			   (phba->sli_rev == 3))
				phba->sli3_options |= LPFC_SLI3_NPIV_ENABLED;
		}
		vport->fc_myDID = phba->fc_pref_DID;
		vport->fc_flag |= FC_LBIT;
	}
	spin_unlock_irq(&phba->hbalock);

	lpfc_linkup(phba);
	if (sparam_mbox) {
		lpfc_read_sparam(phba, sparam_mbox, 0);
		sparam_mbox->vport = vport;
		sparam_mbox->mbox_cmpl = lpfc_mbx_cmpl_read_sparam;
		rc = lpfc_sli_issue_mbox(phba, sparam_mbox,
				    (MBX_NOWAIT | MBX_STOP_IOCB));
		if (rc == MBX_NOT_FINISHED) {
			mp = (struct lpfc_dmabuf *) sparam_mbox->context1;
			lpfc_mbuf_free(phba, mp->virt, mp->phys);
			kfree(mp);
			mempool_free(sparam_mbox, phba->mbox_mem_pool);
			if (cfglink_mbox)
				mempool_free(cfglink_mbox, phba->mbox_mem_pool);
			goto out;
		}
	}

	if (cfglink_mbox) {
		vport->port_state = LPFC_LOCAL_CFG_LINK;
		lpfc_config_link(phba, cfglink_mbox);
		cfglink_mbox->vport = vport;
		cfglink_mbox->mbox_cmpl = lpfc_mbx_cmpl_local_config_link;
		rc = lpfc_sli_issue_mbox(phba, cfglink_mbox,
				    (MBX_NOWAIT | MBX_STOP_IOCB));
		if (rc != MBX_NOT_FINISHED)
			return;
		mempool_free(cfglink_mbox, phba->mbox_mem_pool);
	}
out:
	lpfc_vport_set_state(vport, FC_VPORT_FAILED);
	lpfc_printf_log(phba, KERN_ERR, LOG_MBOX,
		"%d (%d):0263 Discovery Mailbox error: state: 0x%x : %p %p\n",
		phba->brd_no, vport->vpi,
		vport->port_state, sparam_mbox, cfglink_mbox);

	lpfc_issue_clear_la(phba, vport);
	return;
}

static void
lpfc_mbx_issue_link_down(struct lpfc_hba *phba)
{
	uint32_t control;
	struct lpfc_sli *psli = &phba->sli;

	lpfc_linkdown(phba);

	/* turn on Link Attention interrupts - no CLEAR_LA needed */
	spin_lock_irq(&phba->hbalock);
	psli->sli_flag |= LPFC_PROCESS_LA;
	control = readl(phba->HCregaddr);
	control |= HC_LAINT_ENA;
	writel(control, phba->HCregaddr);
	readl(phba->HCregaddr); /* flush */
	spin_unlock_irq(&phba->hbalock);
}

/*
 * This routine handles processing a READ_LA mailbox
 * command upon completion. It is setup in the LPFC_MBOXQ
 * as the completion routine when the command is
 * handed off to the SLI layer.
 */
void
lpfc_mbx_cmpl_read_la(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmb)
{
	struct lpfc_vport *vport = pmb->vport;
	struct Scsi_Host  *shost = lpfc_shost_from_vport(vport);
	READ_LA_VAR *la;
	MAILBOX_t *mb = &pmb->mb;
	struct lpfc_dmabuf *mp = (struct lpfc_dmabuf *) (pmb->context1);

	/* Check for error */
	if (mb->mbxStatus) {
		lpfc_printf_log(phba, KERN_INFO, LOG_LINK_EVENT,
				"%d:1307 READ_LA mbox error x%x state x%x\n",
				phba->brd_no, mb->mbxStatus, vport->port_state);
		lpfc_mbx_issue_link_down(phba);
		phba->link_state = LPFC_HBA_ERROR;
		goto lpfc_mbx_cmpl_read_la_free_mbuf;
	}

	la = (READ_LA_VAR *) & pmb->mb.un.varReadLA;

	memcpy(&phba->alpa_map[0], mp->virt, 128);

	spin_lock_irq(shost->host_lock);
	if (la->pb)
		vport->fc_flag |= FC_BYPASSED_MODE;
	else
		vport->fc_flag &= ~FC_BYPASSED_MODE;
	spin_unlock_irq(shost->host_lock);

	if (((phba->fc_eventTag + 1) < la->eventTag) ||
	    (phba->fc_eventTag == la->eventTag)) {
		phba->fc_stat.LinkMultiEvent++;
		if (la->attType == AT_LINK_UP)
			if (phba->fc_eventTag != 0)
				lpfc_linkdown(phba);
	}

	phba->fc_eventTag = la->eventTag;

	if (la->attType == AT_LINK_UP) {
		phba->fc_stat.LinkUp++;
		if (phba->link_flag & LS_LOOPBACK_MODE) {
			lpfc_printf_log(phba, KERN_INFO, LOG_LINK_EVENT,
				"%d:1306 Link Up Event in loop back mode "
				"x%x received Data: x%x x%x x%x x%x\n",
				phba->brd_no, la->eventTag, phba->fc_eventTag,
				la->granted_AL_PA, la->UlnkSpeed,
				phba->alpa_map[0]);
		} else {
			lpfc_printf_log(phba, KERN_ERR, LOG_LINK_EVENT,
				"%d:1303 Link Up Event x%x received "
				"Data: x%x x%x x%x x%x\n",
				phba->brd_no, la->eventTag, phba->fc_eventTag,
				la->granted_AL_PA, la->UlnkSpeed,
				phba->alpa_map[0]);
		}
		lpfc_mbx_process_link_up(phba, la);
	} else {
		phba->fc_stat.LinkDown++;
		lpfc_printf_log(phba, KERN_ERR, LOG_LINK_EVENT,
				"%d:1305 Link Down Event x%x received "
				"Data: x%x x%x x%x\n",
				phba->brd_no, la->eventTag, phba->fc_eventTag,
				phba->pport->port_state, vport->fc_flag);
		lpfc_mbx_issue_link_down(phba);
	}

lpfc_mbx_cmpl_read_la_free_mbuf:
	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
	mempool_free(pmb, phba->mbox_mem_pool);
	return;
}

/*
 * This routine handles processing a REG_LOGIN mailbox
 * command upon completion. It is setup in the LPFC_MBOXQ
 * as the completion routine when the command is
 * handed off to the SLI layer.
 */
void
lpfc_mbx_cmpl_reg_login(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmb)
{
	struct lpfc_vport  *vport = pmb->vport;
	struct lpfc_dmabuf *mp = (struct lpfc_dmabuf *) (pmb->context1);
	struct lpfc_nodelist *ndlp = (struct lpfc_nodelist *) pmb->context2;

	pmb->context1 = NULL;

	/* Good status, call state machine */
	lpfc_disc_state_machine(vport, ndlp, pmb, NLP_EVT_CMPL_REG_LOGIN);
	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
	mempool_free(pmb, phba->mbox_mem_pool);
	lpfc_nlp_put(ndlp);

	return;
}

static void
lpfc_mbx_cmpl_unreg_vpi(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmb)
{
	MAILBOX_t *mb = &pmb->mb;
	struct lpfc_vport *vport = pmb->vport;
	struct Scsi_Host  *shost = lpfc_shost_from_vport(vport);

	switch (mb->mbxStatus) {
	case 0x0011:
	case 0x0020:
	case 0x9700:
		lpfc_printf_log(phba, KERN_INFO, LOG_NODE,
				"%d (%d):0911 cmpl_unreg_vpi, "
				"mb status = 0x%x\n",
				phba->brd_no, vport->vpi, mb->mbxStatus);
		break;
	}
	vport->unreg_vpi_cmpl = VPORT_OK;
	mempool_free(pmb, phba->mbox_mem_pool);
	/*
	 * This shost reference might have been taken at the beginning of
	 * lpfc_vport_delete()
	 */
	if (vport->load_flag & FC_UNLOADING)
		scsi_host_put(shost);
}

void
lpfc_mbx_unreg_vpi(struct lpfc_vport *vport)
{
	struct lpfc_hba  *phba = vport->phba;
	LPFC_MBOXQ_t *mbox;
	int rc;

	mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!mbox)
		return;

	lpfc_unreg_vpi(phba, vport->vpi, mbox);
	mbox->vport = vport;
	mbox->mbox_cmpl = lpfc_mbx_cmpl_unreg_vpi;
	rc = lpfc_sli_issue_mbox(phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB));
	if (rc == MBX_NOT_FINISHED) {
		lpfc_printf_log(phba, KERN_ERR, LOG_MBOX | LOG_VPORT,
				"%d (%d):1800 Could not issue unreg_vpi\n",
				phba->brd_no, vport->vpi);
		mempool_free(mbox, phba->mbox_mem_pool);
		vport->unreg_vpi_cmpl = VPORT_ERROR;
	}
}

static void
lpfc_mbx_cmpl_reg_vpi(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmb)
{
	struct lpfc_vport *vport = pmb->vport;
	struct Scsi_Host  *shost = lpfc_shost_from_vport(vport);
	MAILBOX_t *mb = &pmb->mb;

	switch (mb->mbxStatus) {
	case 0x0011:
	case 0x9601:
	case 0x9602:
		lpfc_printf_log(phba, KERN_INFO, LOG_NODE,
				"%d (%d):0912 cmpl_reg_vpi, mb status = 0x%x\n",
				phba->brd_no, vport->vpi, mb->mbxStatus);
		lpfc_vport_set_state(vport, FC_VPORT_FAILED);
		spin_lock_irq(shost->host_lock);
		vport->fc_flag &= ~(FC_FABRIC | FC_PUBLIC_LOOP);
		spin_unlock_irq(shost->host_lock);
		vport->fc_myDID = 0;
		goto out;
	}

	vport->num_disc_nodes = 0;
	/* go thru NPR list and issue ELS PLOGIs */
	if (vport->fc_npr_cnt)
		lpfc_els_disc_plogi(vport);

	if (!vport->num_disc_nodes) {
		spin_lock_irq(shost->host_lock);
		vport->fc_flag &= ~FC_NDISC_ACTIVE;
		spin_unlock_irq(shost->host_lock);
		lpfc_can_disctmo(vport);
	}
	vport->port_state = LPFC_VPORT_READY;

out:
	mempool_free(pmb, phba->mbox_mem_pool);
	return;
}

/*
 * This routine handles processing a Fabric REG_LOGIN mailbox
 * command upon completion. It is setup in the LPFC_MBOXQ
 * as the completion routine when the command is
 * handed off to the SLI layer.
 */
void
lpfc_mbx_cmpl_fabric_reg_login(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmb)
{
	struct lpfc_vport *vport = pmb->vport;
	struct lpfc_vport *next_vport;
	MAILBOX_t *mb = &pmb->mb;
	struct lpfc_dmabuf *mp = (struct lpfc_dmabuf *) (pmb->context1);
	struct lpfc_nodelist *ndlp;
	ndlp = (struct lpfc_nodelist *) pmb->context2;

	pmb->context1 = NULL;
	pmb->context2 = NULL;

	if (mb->mbxStatus) {
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		kfree(mp);
		mempool_free(pmb, phba->mbox_mem_pool);
		lpfc_nlp_put(ndlp);

		if (phba->fc_topology == TOPOLOGY_LOOP) {
			/* FLOGI failed, use loop map to make discovery list */
			lpfc_disc_list_loopmap(vport);

			/* Start discovery */
			lpfc_disc_start(vport);
			return;
		}

		lpfc_vport_set_state(vport, FC_VPORT_FAILED);
		lpfc_printf_log(phba, KERN_ERR, LOG_MBOX,
			"%d (%d):0258 Register Fabric login error: 0x%x\n",
			phba->brd_no, vport->vpi, mb->mbxStatus);

		return;
	}

	ndlp->nlp_rpi = mb->un.varWords[0];
	ndlp->nlp_type |= NLP_FABRIC;
	lpfc_nlp_set_state(vport, ndlp, NLP_STE_UNMAPPED_NODE);

	lpfc_nlp_put(ndlp);	/* Drop the reference from the mbox */

	if (vport->port_state == LPFC_FABRIC_CFG_LINK) {
		list_for_each_entry(next_vport, &phba->port_list, listentry) {
			if (next_vport->port_type == LPFC_PHYSICAL_PORT)
				continue;

			if (phba->link_flag & LS_NPIV_FAB_SUPPORTED)
				lpfc_initial_fdisc(next_vport);
			else if (phba->sli3_options & LPFC_SLI3_NPIV_ENABLED) {
				lpfc_vport_set_state(vport,
						     FC_VPORT_NO_FABRIC_SUPP);
				lpfc_printf_log(phba, KERN_ERR, LOG_ELS,
						"%d (%d):0259 No NPIV Fabric "
						"support\n",
						phba->brd_no, vport->vpi);
			}
		}
		lpfc_do_scr_ns_plogi(phba, vport);
	}

	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
	mempool_free(pmb, phba->mbox_mem_pool);
	return;
}

/*
 * This routine handles processing a NameServer REG_LOGIN mailbox
 * command upon completion. It is setup in the LPFC_MBOXQ
 * as the completion routine when the command is
 * handed off to the SLI layer.
 */
void
lpfc_mbx_cmpl_ns_reg_login(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmb)
{
	MAILBOX_t *mb = &pmb->mb;
	struct lpfc_dmabuf *mp = (struct lpfc_dmabuf *) (pmb->context1);
	struct lpfc_nodelist *ndlp = (struct lpfc_nodelist *) pmb->context2;
	struct lpfc_vport *vport = pmb->vport;

	if (mb->mbxStatus) {
out:
		lpfc_nlp_put(ndlp);
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		kfree(mp);
		mempool_free(pmb, phba->mbox_mem_pool);
		lpfc_drop_node(vport, ndlp);

		if (phba->fc_topology == TOPOLOGY_LOOP) {
			/*
			 * RegLogin failed, use loop map to make discovery
			 * list
			 */
			lpfc_disc_list_loopmap(vport);

			/* Start discovery */
			lpfc_disc_start(vport);
			return;
		}
		lpfc_vport_set_state(vport, FC_VPORT_FAILED);
		lpfc_printf_log(phba, KERN_ERR, LOG_ELS,
			"%d (%d):0260 Register NameServer error: 0x%x\n",
			phba->brd_no, vport->vpi, mb->mbxStatus);
		return;
	}

	pmb->context1 = NULL;

	ndlp->nlp_rpi = mb->un.varWords[0];
	ndlp->nlp_type |= NLP_FABRIC;
	lpfc_nlp_set_state(vport, ndlp, NLP_STE_UNMAPPED_NODE);

	if (vport->port_state < LPFC_VPORT_READY) {
		/* Link up discovery requires Fabric registration. */
		lpfc_ns_cmd(vport, SLI_CTNS_RFF_ID, 0, 0); /* Do this first! */
		lpfc_ns_cmd(vport, SLI_CTNS_RNN_ID, 0, 0);
		lpfc_ns_cmd(vport, SLI_CTNS_RSNN_NN, 0, 0);
		lpfc_ns_cmd(vport, SLI_CTNS_RSPN_ID, 0, 0);
		lpfc_ns_cmd(vport, SLI_CTNS_RFT_ID, 0, 0);

		/* Issue SCR just before NameServer GID_FT Query */
		lpfc_issue_els_scr(vport, SCR_DID, 0);
	}

	vport->fc_ns_retry = 0;
	/* Good status, issue CT Request to NameServer */
	if (lpfc_ns_cmd(vport, SLI_CTNS_GID_FT, 0, 0)) {
		/* Cannot issue NameServer Query, so finish up discovery */
		goto out;
	}

	lpfc_nlp_put(ndlp);
	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
	mempool_free(pmb, phba->mbox_mem_pool);

	return;
}

static void
lpfc_register_remote_port(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct fc_rport  *rport;
	struct lpfc_rport_data *rdata;
	struct fc_rport_identifiers rport_ids;
	struct lpfc_hba  *phba = vport->phba;

	/* Remote port has reappeared. Re-register w/ FC transport */
	rport_ids.node_name = wwn_to_u64(ndlp->nlp_nodename.u.wwn);
	rport_ids.port_name = wwn_to_u64(ndlp->nlp_portname.u.wwn);
	rport_ids.port_id = ndlp->nlp_DID;
	rport_ids.roles = FC_RPORT_ROLE_UNKNOWN;

	/*
	 * We leave our node pointer in rport->dd_data when we unregister a
	 * FCP target port.  But fc_remote_port_add zeros the space to which
	 * rport->dd_data points.  So, if we're reusing a previously
	 * registered port, drop the reference that we took the last time we
	 * registered the port.
	 */
	if (ndlp->rport && ndlp->rport->dd_data &&
	    ((struct lpfc_rport_data *) ndlp->rport->dd_data)->pnode == ndlp) {
		lpfc_nlp_put(ndlp);
	}

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_RPORT,
		"rport add:       did:x%x flg:x%x type x%x",
		ndlp->nlp_DID, ndlp->nlp_flag, ndlp->nlp_type);

	ndlp->rport = rport = fc_remote_port_add(shost, 0, &rport_ids);
	if (!rport || !get_device(&rport->dev)) {
		dev_printk(KERN_WARNING, &phba->pcidev->dev,
			   "Warning: fc_remote_port_add failed\n");
		return;
	}

	/* initialize static port data */
	rport->maxframe_size = ndlp->nlp_maxframe;
	rport->supported_classes = ndlp->nlp_class_sup;
	rdata = rport->dd_data;
	rdata->pnode = lpfc_nlp_get(ndlp);

	if (ndlp->nlp_type & NLP_FCP_TARGET)
		rport_ids.roles |= FC_RPORT_ROLE_FCP_TARGET;
	if (ndlp->nlp_type & NLP_FCP_INITIATOR)
		rport_ids.roles |= FC_RPORT_ROLE_FCP_INITIATOR;


	if (rport_ids.roles !=  FC_RPORT_ROLE_UNKNOWN)
		fc_remote_port_rolechg(rport, rport_ids.roles);

	if ((rport->scsi_target_id != -1) &&
	    (rport->scsi_target_id < LPFC_MAX_TARGET)) {
		ndlp->nlp_sid = rport->scsi_target_id;
	}
	return;
}

static void
lpfc_unregister_remote_port(struct lpfc_nodelist *ndlp)
{
	struct fc_rport *rport = ndlp->rport;

	lpfc_debugfs_disc_trc(ndlp->vport, LPFC_DISC_TRC_RPORT,
		"rport delete:    did:x%x flg:x%x type x%x",
		ndlp->nlp_DID, ndlp->nlp_flag, ndlp->nlp_type);

	fc_remote_port_delete(rport);

	return;
}

static void
lpfc_nlp_counters(struct lpfc_vport *vport, int state, int count)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);

	spin_lock_irq(shost->host_lock);
	switch (state) {
	case NLP_STE_UNUSED_NODE:
		vport->fc_unused_cnt += count;
		break;
	case NLP_STE_PLOGI_ISSUE:
		vport->fc_plogi_cnt += count;
		break;
	case NLP_STE_ADISC_ISSUE:
		vport->fc_adisc_cnt += count;
		break;
	case NLP_STE_REG_LOGIN_ISSUE:
		vport->fc_reglogin_cnt += count;
		break;
	case NLP_STE_PRLI_ISSUE:
		vport->fc_prli_cnt += count;
		break;
	case NLP_STE_UNMAPPED_NODE:
		vport->fc_unmap_cnt += count;
		break;
	case NLP_STE_MAPPED_NODE:
		vport->fc_map_cnt += count;
		break;
	case NLP_STE_NPR_NODE:
		vport->fc_npr_cnt += count;
		break;
	}
	spin_unlock_irq(shost->host_lock);
}

static void
lpfc_nlp_state_cleanup(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
		       int old_state, int new_state)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);

	if (new_state == NLP_STE_UNMAPPED_NODE) {
		ndlp->nlp_type &= ~(NLP_FCP_TARGET | NLP_FCP_INITIATOR);
		ndlp->nlp_flag &= ~NLP_NODEV_REMOVE;
		ndlp->nlp_type |= NLP_FC_NODE;
	}
	if (new_state == NLP_STE_MAPPED_NODE)
		ndlp->nlp_flag &= ~NLP_NODEV_REMOVE;
	if (new_state == NLP_STE_NPR_NODE)
		ndlp->nlp_flag &= ~NLP_RCV_PLOGI;

	/* Transport interface */
	if (ndlp->rport && (old_state == NLP_STE_MAPPED_NODE ||
			    old_state == NLP_STE_UNMAPPED_NODE)) {
		vport->phba->nport_event_cnt++;
		lpfc_unregister_remote_port(ndlp);
	}

	if (new_state ==  NLP_STE_MAPPED_NODE ||
	    new_state == NLP_STE_UNMAPPED_NODE) {
		vport->phba->nport_event_cnt++;
		/*
		 * Tell the fc transport about the port, if we haven't
		 * already. If we have, and it's a scsi entity, be
		 * sure to unblock any attached scsi devices
		 */
		lpfc_register_remote_port(vport, ndlp);
	}
	/*
	 * if we added to Mapped list, but the remote port
	 * registration failed or assigned a target id outside
	 * our presentable range - move the node to the
	 * Unmapped List
	 */
	if (new_state == NLP_STE_MAPPED_NODE &&
	    (!ndlp->rport ||
	     ndlp->rport->scsi_target_id == -1 ||
	     ndlp->rport->scsi_target_id >= LPFC_MAX_TARGET)) {
		spin_lock_irq(shost->host_lock);
		ndlp->nlp_flag |= NLP_TGT_NO_SCSIID;
		spin_unlock_irq(shost->host_lock);
		lpfc_nlp_set_state(vport, ndlp, NLP_STE_UNMAPPED_NODE);
	}
}

static char *
lpfc_nlp_state_name(char *buffer, size_t size, int state)
{
	static char *states[] = {
		[NLP_STE_UNUSED_NODE] = "UNUSED",
		[NLP_STE_PLOGI_ISSUE] = "PLOGI",
		[NLP_STE_ADISC_ISSUE] = "ADISC",
		[NLP_STE_REG_LOGIN_ISSUE] = "REGLOGIN",
		[NLP_STE_PRLI_ISSUE] = "PRLI",
		[NLP_STE_UNMAPPED_NODE] = "UNMAPPED",
		[NLP_STE_MAPPED_NODE] = "MAPPED",
		[NLP_STE_NPR_NODE] = "NPR",
	};

	if (state < ARRAY_SIZE(states) && states[state])
		strlcpy(buffer, states[state], size);
	else
		snprintf(buffer, size, "unknown (%d)", state);
	return buffer;
}

void
lpfc_nlp_set_state(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
		   int state)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	int  old_state = ndlp->nlp_state;
	char name1[16], name2[16];

	lpfc_printf_log(vport->phba, KERN_INFO, LOG_NODE,
			"%d (%d):0904 NPort state transition x%06x, %s -> %s\n",
			vport->phba->brd_no, vport->vpi,
			ndlp->nlp_DID,
			lpfc_nlp_state_name(name1, sizeof(name1), old_state),
			lpfc_nlp_state_name(name2, sizeof(name2), state));

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_NODE,
		"node statechg    did:x%x old:%d ste:%d",
		ndlp->nlp_DID, old_state, state);

	if (old_state == NLP_STE_NPR_NODE &&
	    (ndlp->nlp_flag & NLP_DELAY_TMO) != 0 &&
	    state != NLP_STE_NPR_NODE)
		lpfc_cancel_retry_delay_tmo(vport, ndlp);
	if (old_state == NLP_STE_UNMAPPED_NODE) {
		ndlp->nlp_flag &= ~NLP_TGT_NO_SCSIID;
		ndlp->nlp_type &= ~NLP_FC_NODE;
	}

	if (list_empty(&ndlp->nlp_listp)) {
		spin_lock_irq(shost->host_lock);
		list_add_tail(&ndlp->nlp_listp, &vport->fc_nodes);
		spin_unlock_irq(shost->host_lock);
	} else if (old_state)
		lpfc_nlp_counters(vport, old_state, -1);

	ndlp->nlp_state = state;
	lpfc_nlp_counters(vport, state, 1);
	lpfc_nlp_state_cleanup(vport, ndlp, old_state, state);
}

void
lpfc_dequeue_node(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);

	if ((ndlp->nlp_flag & NLP_DELAY_TMO) != 0)
		lpfc_cancel_retry_delay_tmo(vport, ndlp);
	if (ndlp->nlp_state && !list_empty(&ndlp->nlp_listp))
		lpfc_nlp_counters(vport, ndlp->nlp_state, -1);
	spin_lock_irq(shost->host_lock);
	list_del_init(&ndlp->nlp_listp);
	spin_unlock_irq(shost->host_lock);
	lpfc_nlp_state_cleanup(vport, ndlp, ndlp->nlp_state,
			       NLP_STE_UNUSED_NODE);
}

void
lpfc_drop_node(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);

	if ((ndlp->nlp_flag & NLP_DELAY_TMO) != 0)
		lpfc_cancel_retry_delay_tmo(vport, ndlp);
	if (ndlp->nlp_state && !list_empty(&ndlp->nlp_listp))
		lpfc_nlp_counters(vport, ndlp->nlp_state, -1);
	spin_lock_irq(shost->host_lock);
	list_del_init(&ndlp->nlp_listp);
	ndlp->nlp_flag &= ~NLP_TARGET_REMOVE;
	spin_unlock_irq(shost->host_lock);
	lpfc_nlp_put(ndlp);
}

/*
 * Start / ReStart rescue timer for Discovery / RSCN handling
 */
void
lpfc_set_disctmo(struct lpfc_vport *vport)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct lpfc_hba  *phba = vport->phba;
	uint32_t tmo;

	if (vport->port_state == LPFC_LOCAL_CFG_LINK) {
		/* For FAN, timeout should be greater then edtov */
		tmo = (((phba->fc_edtov + 999) / 1000) + 1);
	} else {
		/* Normal discovery timeout should be > then ELS/CT timeout
		 * FC spec states we need 3 * ratov for CT requests
		 */
		tmo = ((phba->fc_ratov * 3) + 3);
	}


	if (!timer_pending(&vport->fc_disctmo)) {
		lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_CMD,
			"set disc timer:  tmo:x%x state:x%x flg:x%x",
			tmo, vport->port_state, vport->fc_flag);
	}

	mod_timer(&vport->fc_disctmo, jiffies + HZ * tmo);
	spin_lock_irq(shost->host_lock);
	vport->fc_flag |= FC_DISC_TMO;
	spin_unlock_irq(shost->host_lock);

	/* Start Discovery Timer state <hba_state> */
	lpfc_printf_log(phba, KERN_INFO, LOG_DISCOVERY,
			"%d (%d):0247 Start Discovery Timer state x%x "
			"Data: x%x x%lx x%x x%x\n",
			phba->brd_no, vport->vpi, vport->port_state, tmo,
			(unsigned long)&vport->fc_disctmo, vport->fc_plogi_cnt,
			vport->fc_adisc_cnt);

	return;
}

/*
 * Cancel rescue timer for Discovery / RSCN handling
 */
int
lpfc_can_disctmo(struct lpfc_vport *vport)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct lpfc_hba  *phba = vport->phba;
	unsigned long iflags;

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_CMD,
		"can disc timer:  state:x%x rtry:x%x flg:x%x",
		vport->port_state, vport->fc_ns_retry, vport->fc_flag);

	/* Turn off discovery timer if its running */
	if (vport->fc_flag & FC_DISC_TMO) {
		spin_lock_irqsave(shost->host_lock, iflags);
		vport->fc_flag &= ~FC_DISC_TMO;
		spin_unlock_irqrestore(shost->host_lock, iflags);
		del_timer_sync(&vport->fc_disctmo);
		spin_lock_irqsave(&vport->work_port_lock, iflags);
		vport->work_port_events &= ~WORKER_DISC_TMO;
		spin_unlock_irqrestore(&vport->work_port_lock, iflags);
	}

	/* Cancel Discovery Timer state <hba_state> */
	lpfc_printf_log(phba, KERN_INFO, LOG_DISCOVERY,
			"%d (%d):0248 Cancel Discovery Timer state x%x "
			"Data: x%x x%x x%x\n",
			phba->brd_no, vport->vpi, vport->port_state,
			vport->fc_flag, vport->fc_plogi_cnt,
			vport->fc_adisc_cnt);

	return 0;
}

/*
 * Check specified ring for outstanding IOCB on the SLI queue
 * Return true if iocb matches the specified nport
 */
int
lpfc_check_sli_ndlp(struct lpfc_hba *phba,
		    struct lpfc_sli_ring *pring,
		    struct lpfc_iocbq *iocb,
		    struct lpfc_nodelist *ndlp)
{
	struct lpfc_sli *psli = &phba->sli;
	IOCB_t *icmd = &iocb->iocb;
	struct lpfc_vport    *vport = ndlp->vport;

	if (iocb->vport != vport)
		return 0;

	if (pring->ringno == LPFC_ELS_RING) {
		switch (icmd->ulpCommand) {
		case CMD_GEN_REQUEST64_CR:
			if (icmd->ulpContext == (volatile ushort)ndlp->nlp_rpi)
				return 1;
		case CMD_ELS_REQUEST64_CR:
			if (icmd->un.elsreq64.remoteID == ndlp->nlp_DID)
				return 1;
		case CMD_XMIT_ELS_RSP64_CX:
			if (iocb->context1 == (uint8_t *) ndlp)
				return 1;
		}
	} else if (pring->ringno == psli->extra_ring) {

	} else if (pring->ringno == psli->fcp_ring) {
		/* Skip match check if waiting to relogin to FCP target */
		if ((ndlp->nlp_type & NLP_FCP_TARGET) &&
		    (ndlp->nlp_flag & NLP_DELAY_TMO)) {
			return 0;
		}
		if (icmd->ulpContext == (volatile ushort)ndlp->nlp_rpi) {
			return 1;
		}
	} else if (pring->ringno == psli->next_ring) {

	}
	return 0;
}

/*
 * Free resources / clean up outstanding I/Os
 * associated with nlp_rpi in the LPFC_NODELIST entry.
 */
static int
lpfc_no_rpi(struct lpfc_hba *phba, struct lpfc_nodelist *ndlp)
{
	LIST_HEAD(completions);
	struct lpfc_sli *psli;
	struct lpfc_sli_ring *pring;
	struct lpfc_iocbq *iocb, *next_iocb;
	IOCB_t *icmd;
	uint32_t rpi, i;

	lpfc_fabric_abort_nport(ndlp);

	/*
	 * Everything that matches on txcmplq will be returned
	 * by firmware with a no rpi error.
	 */
	psli = &phba->sli;
	rpi = ndlp->nlp_rpi;
	if (rpi) {
		/* Now process each ring */
		for (i = 0; i < psli->num_rings; i++) {
			pring = &psli->ring[i];

			spin_lock_irq(&phba->hbalock);
			list_for_each_entry_safe(iocb, next_iocb, &pring->txq,
						 list) {
				/*
				 * Check to see if iocb matches the nport we are
				 * looking for
				 */
				if ((lpfc_check_sli_ndlp(phba, pring, iocb,
							 ndlp))) {
					/* It matches, so deque and call compl
					   with an error */
					list_move_tail(&iocb->list,
						       &completions);
					pring->txq_cnt--;
				}
			}
			spin_unlock_irq(&phba->hbalock);
		}
	}

	while (!list_empty(&completions)) {
		iocb = list_get_first(&completions, struct lpfc_iocbq, list);
		list_del_init(&iocb->list);

		if (!iocb->iocb_cmpl)
			lpfc_sli_release_iocbq(phba, iocb);
		else {
			icmd = &iocb->iocb;
			icmd->ulpStatus = IOSTAT_LOCAL_REJECT;
			icmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
			(iocb->iocb_cmpl)(phba, iocb, iocb);
		}
	}

	return 0;
}

/*
 * Free rpi associated with LPFC_NODELIST entry.
 * This routine is called from lpfc_freenode(), when we are removing
 * a LPFC_NODELIST entry. It is also called if the driver initiates a
 * LOGO that completes successfully, and we are waiting to PLOGI back
 * to the remote NPort. In addition, it is called after we receive
 * and unsolicated ELS cmd, send back a rsp, the rsp completes and
 * we are waiting to PLOGI back to the remote NPort.
 */
int
lpfc_unreg_rpi(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp)
{
	struct lpfc_hba *phba = vport->phba;
	LPFC_MBOXQ_t    *mbox;
	int rc;

	if (ndlp->nlp_rpi) {
		mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
		if (mbox) {
			lpfc_unreg_login(phba, vport->vpi, ndlp->nlp_rpi, mbox);
			mbox->vport = vport;
			mbox->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
			rc = lpfc_sli_issue_mbox(phba, mbox,
						 (MBX_NOWAIT | MBX_STOP_IOCB));
			if (rc == MBX_NOT_FINISHED)
				mempool_free(mbox, phba->mbox_mem_pool);
		}
		lpfc_no_rpi(phba, ndlp);
		ndlp->nlp_rpi = 0;
		return 1;
	}
	return 0;
}

void
lpfc_unreg_all_rpis(struct lpfc_vport *vport)
{
	struct lpfc_hba  *phba  = vport->phba;
	LPFC_MBOXQ_t     *mbox;
	int rc;

	mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (mbox) {
		lpfc_unreg_login(phba, vport->vpi, 0xffff, mbox);
		mbox->vport = vport;
		mbox->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
		rc = lpfc_sli_issue_mbox(phba, mbox,
					 (MBX_NOWAIT | MBX_STOP_IOCB));
		if (rc == MBX_NOT_FINISHED) {
			mempool_free(mbox, phba->mbox_mem_pool);
		}
	}
}

void
lpfc_unreg_default_rpis(struct lpfc_vport *vport)
{
	struct lpfc_hba  *phba  = vport->phba;
	LPFC_MBOXQ_t     *mbox;
	int rc;

	mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (mbox) {
		lpfc_unreg_did(phba, vport->vpi, 0xffffffff, mbox);
		mbox->vport = vport;
		mbox->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
		rc = lpfc_sli_issue_mbox(phba, mbox,
					 (MBX_NOWAIT | MBX_STOP_IOCB));
		if (rc == MBX_NOT_FINISHED) {
			lpfc_printf_log(phba, KERN_ERR, LOG_MBOX | LOG_VPORT,
					"%d (%d):1815 Could not issue "
					"unreg_did (default rpis)\n",
					phba->brd_no, vport->vpi);
			mempool_free(mbox, phba->mbox_mem_pool);
		}
	}
}

/*
 * Free resources associated with LPFC_NODELIST entry
 * so it can be freed.
 */
static int
lpfc_cleanup_node(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct lpfc_hba  *phba = vport->phba;
	LPFC_MBOXQ_t *mb, *nextmb;
	struct lpfc_dmabuf *mp;

	/* Cleanup node for NPort <nlp_DID> */
	lpfc_printf_log(phba, KERN_INFO, LOG_NODE,
			"%d (%d):0900 Cleanup node for NPort x%x "
			"Data: x%x x%x x%x\n",
			phba->brd_no, vport->vpi, ndlp->nlp_DID, ndlp->nlp_flag,
			ndlp->nlp_state, ndlp->nlp_rpi);

	lpfc_dequeue_node(vport, ndlp);

	/* cleanup any ndlp on mbox q waiting for reglogin cmpl */
	if ((mb = phba->sli.mbox_active)) {
		if ((mb->mb.mbxCommand == MBX_REG_LOGIN64) &&
		   (ndlp == (struct lpfc_nodelist *) mb->context2)) {
			mb->context2 = NULL;
			mb->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
		}
	}

	spin_lock_irq(&phba->hbalock);
	list_for_each_entry_safe(mb, nextmb, &phba->sli.mboxq, list) {
		if ((mb->mb.mbxCommand == MBX_REG_LOGIN64) &&
		    (ndlp == (struct lpfc_nodelist *) mb->context2)) {
			mp = (struct lpfc_dmabuf *) (mb->context1);
			if (mp) {
				__lpfc_mbuf_free(phba, mp->virt, mp->phys);
				kfree(mp);
			}
			list_del(&mb->list);
			mempool_free(mb, phba->mbox_mem_pool);
			lpfc_nlp_put(ndlp);
		}
	}
	spin_unlock_irq(&phba->hbalock);

	lpfc_els_abort(phba,ndlp);
	spin_lock_irq(shost->host_lock);
	ndlp->nlp_flag &= ~NLP_DELAY_TMO;
	spin_unlock_irq(shost->host_lock);

	ndlp->nlp_last_elscmd = 0;
	del_timer_sync(&ndlp->nlp_delayfunc);

	if (!list_empty(&ndlp->els_retry_evt.evt_listp))
		list_del_init(&ndlp->els_retry_evt.evt_listp);
	if (!list_empty(&ndlp->dev_loss_evt.evt_listp))
		list_del_init(&ndlp->dev_loss_evt.evt_listp);

	if (!list_empty(&ndlp->dev_loss_evt.evt_listp)) {
		list_del_init(&ndlp->dev_loss_evt.evt_listp);
		complete((struct completion *)(ndlp->dev_loss_evt.evt_arg2));
	}

	lpfc_unreg_rpi(vport, ndlp);

	return 0;
}

/*
 * Check to see if we can free the nlp back to the freelist.
 * If we are in the middle of using the nlp in the discovery state
 * machine, defer the free till we reach the end of the state machine.
 */
static void
lpfc_nlp_remove(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp)
{
	struct lpfc_rport_data *rdata;

	if (ndlp->nlp_flag & NLP_DELAY_TMO) {
		lpfc_cancel_retry_delay_tmo(vport, ndlp);
	}

	lpfc_cleanup_node(vport, ndlp);

	/*
	 * We can get here with a non-NULL ndlp->rport because when we
	 * unregister a rport we don't break the rport/node linkage.  So if we
	 * do, make sure we don't leaving any dangling pointers behind.
	 */
	if (ndlp->rport) {
		rdata = ndlp->rport->dd_data;
		rdata->pnode = NULL;
		ndlp->rport = NULL;
	}
}

static int
lpfc_matchdid(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
	      uint32_t did)
{
	D_ID mydid, ndlpdid, matchdid;

	if (did == Bcast_DID)
		return 0;

	if (ndlp->nlp_DID == 0) {
		return 0;
	}

	/* First check for Direct match */
	if (ndlp->nlp_DID == did)
		return 1;

	/* Next check for area/domain identically equals 0 match */
	mydid.un.word = vport->fc_myDID;
	if ((mydid.un.b.domain == 0) && (mydid.un.b.area == 0)) {
		return 0;
	}

	matchdid.un.word = did;
	ndlpdid.un.word = ndlp->nlp_DID;
	if (matchdid.un.b.id == ndlpdid.un.b.id) {
		if ((mydid.un.b.domain == matchdid.un.b.domain) &&
		    (mydid.un.b.area == matchdid.un.b.area)) {
			if ((ndlpdid.un.b.domain == 0) &&
			    (ndlpdid.un.b.area == 0)) {
				if (ndlpdid.un.b.id)
					return 1;
			}
			return 0;
		}

		matchdid.un.word = ndlp->nlp_DID;
		if ((mydid.un.b.domain == ndlpdid.un.b.domain) &&
		    (mydid.un.b.area == ndlpdid.un.b.area)) {
			if ((matchdid.un.b.domain == 0) &&
			    (matchdid.un.b.area == 0)) {
				if (matchdid.un.b.id)
					return 1;
			}
		}
	}
	return 0;
}

/* Search for a nodelist entry */
static struct lpfc_nodelist *
__lpfc_findnode_did(struct lpfc_vport *vport, uint32_t did)
{
	struct lpfc_hba  *phba = vport->phba;
	struct lpfc_nodelist *ndlp;
	uint32_t data1;

	list_for_each_entry(ndlp, &vport->fc_nodes, nlp_listp) {
		if (lpfc_matchdid(vport, ndlp, did)) {
			data1 = (((uint32_t) ndlp->nlp_state << 24) |
				 ((uint32_t) ndlp->nlp_xri << 16) |
				 ((uint32_t) ndlp->nlp_type << 8) |
				 ((uint32_t) ndlp->nlp_rpi & 0xff));
			lpfc_printf_log(phba, KERN_INFO, LOG_NODE,
					"%d (%d):0929 FIND node DID "
					" Data: x%p x%x x%x x%x\n",
					phba->brd_no, vport->vpi,
					ndlp, ndlp->nlp_DID,
					ndlp->nlp_flag, data1);
			return ndlp;
		}
	}

	/* FIND node did <did> NOT FOUND */
	lpfc_printf_log(phba, KERN_INFO, LOG_NODE,
			"%d (%d):0932 FIND node did x%x NOT FOUND.\n",
			phba->brd_no, vport->vpi, did);
	return NULL;
}

struct lpfc_nodelist *
lpfc_findnode_did(struct lpfc_vport *vport, uint32_t did)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct lpfc_nodelist *ndlp;

	spin_lock_irq(shost->host_lock);
	ndlp = __lpfc_findnode_did(vport, did);
	spin_unlock_irq(shost->host_lock);
	return ndlp;
}

struct lpfc_nodelist *
lpfc_setup_disc_node(struct lpfc_vport *vport, uint32_t did)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct lpfc_nodelist *ndlp;

	ndlp = lpfc_findnode_did(vport, did);
	if (!ndlp) {
		if ((vport->fc_flag & FC_RSCN_MODE) != 0 &&
		    lpfc_rscn_payload_check(vport, did) == 0)
			return NULL;
		ndlp = (struct lpfc_nodelist *)
		     mempool_alloc(vport->phba->nlp_mem_pool, GFP_KERNEL);
		if (!ndlp)
			return NULL;
		lpfc_nlp_init(vport, ndlp, did);
		lpfc_nlp_set_state(vport, ndlp, NLP_STE_NPR_NODE);
		spin_lock_irq(shost->host_lock);
		ndlp->nlp_flag |= NLP_NPR_2B_DISC;
		spin_unlock_irq(shost->host_lock);
		return ndlp;
	}
	if (vport->fc_flag & FC_RSCN_MODE) {
		if (lpfc_rscn_payload_check(vport, did)) {
			spin_lock_irq(shost->host_lock);
			ndlp->nlp_flag |= NLP_NPR_2B_DISC;
			spin_unlock_irq(shost->host_lock);

			/* Since this node is marked for discovery,
			 * delay timeout is not needed.
			 */
			if (ndlp->nlp_flag & NLP_DELAY_TMO)
				lpfc_cancel_retry_delay_tmo(vport, ndlp);
		} else
			ndlp = NULL;
	} else {
		if (ndlp->nlp_state == NLP_STE_ADISC_ISSUE ||
		    ndlp->nlp_state == NLP_STE_PLOGI_ISSUE)
			return NULL;
		lpfc_nlp_set_state(vport, ndlp, NLP_STE_NPR_NODE);
		spin_lock_irq(shost->host_lock);
		ndlp->nlp_flag |= NLP_NPR_2B_DISC;
		spin_unlock_irq(shost->host_lock);
	}
	return ndlp;
}

/* Build a list of nodes to discover based on the loopmap */
void
lpfc_disc_list_loopmap(struct lpfc_vport *vport)
{
	struct lpfc_hba  *phba = vport->phba;
	int j;
	uint32_t alpa, index;

	if (!lpfc_is_link_up(phba))
		return;

	if (phba->fc_topology != TOPOLOGY_LOOP)
		return;

	/* Check for loop map present or not */
	if (phba->alpa_map[0]) {
		for (j = 1; j <= phba->alpa_map[0]; j++) {
			alpa = phba->alpa_map[j];
			if (((vport->fc_myDID & 0xff) == alpa) || (alpa == 0))
				continue;
			lpfc_setup_disc_node(vport, alpa);
		}
	} else {
		/* No alpamap, so try all alpa's */
		for (j = 0; j < FC_MAXLOOP; j++) {
			/* If cfg_scan_down is set, start from highest
			 * ALPA (0xef) to lowest (0x1).
			 */
			if (phba->cfg_scan_down)
				index = j;
			else
				index = FC_MAXLOOP - j - 1;
			alpa = lpfcAlpaArray[index];
			if ((vport->fc_myDID & 0xff) == alpa)
				continue;
			lpfc_setup_disc_node(vport, alpa);
		}
	}
	return;
}

void
lpfc_issue_clear_la(struct lpfc_hba *phba, struct lpfc_vport *vport)
{
	LPFC_MBOXQ_t *mbox;
	struct lpfc_sli *psli = &phba->sli;
	struct lpfc_sli_ring *extra_ring = &psli->ring[psli->extra_ring];
	struct lpfc_sli_ring *fcp_ring   = &psli->ring[psli->fcp_ring];
	struct lpfc_sli_ring *next_ring  = &psli->ring[psli->next_ring];
	int  rc;

	/*
	 * if it's not a physical port or if we already send
	 * clear_la then don't send it.
	 */
	if ((phba->link_state >= LPFC_CLEAR_LA) ||
	    (vport->port_type != LPFC_PHYSICAL_PORT))
		return;

			/* Link up discovery */
	if ((mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL)) != NULL) {
		phba->link_state = LPFC_CLEAR_LA;
		lpfc_clear_la(phba, mbox);
		mbox->mbox_cmpl = lpfc_mbx_cmpl_clear_la;
		mbox->vport = vport;
		rc = lpfc_sli_issue_mbox(phba, mbox, (MBX_NOWAIT |
						      MBX_STOP_IOCB));
		if (rc == MBX_NOT_FINISHED) {
			mempool_free(mbox, phba->mbox_mem_pool);
			lpfc_disc_flush_list(vport);
			extra_ring->flag &= ~LPFC_STOP_IOCB_EVENT;
			fcp_ring->flag &= ~LPFC_STOP_IOCB_EVENT;
			next_ring->flag &= ~LPFC_STOP_IOCB_EVENT;
			phba->link_state = LPFC_HBA_ERROR;
		}
	}
}

/* Reg_vpi to tell firmware to resume normal operations */
void
lpfc_issue_reg_vpi(struct lpfc_hba *phba, struct lpfc_vport *vport)
{
	LPFC_MBOXQ_t *regvpimbox;

	regvpimbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (regvpimbox) {
		lpfc_reg_vpi(phba, vport->vpi, vport->fc_myDID, regvpimbox);
		regvpimbox->mbox_cmpl = lpfc_mbx_cmpl_reg_vpi;
		regvpimbox->vport = vport;
		if (lpfc_sli_issue_mbox(phba, regvpimbox,
					(MBX_NOWAIT | MBX_STOP_IOCB))
					== MBX_NOT_FINISHED) {
			mempool_free(regvpimbox, phba->mbox_mem_pool);
		}
	}
}

/* Start Link up / RSCN discovery on NPR nodes */
void
lpfc_disc_start(struct lpfc_vport *vport)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct lpfc_hba  *phba = vport->phba;
	uint32_t num_sent;
	uint32_t clear_la_pending;
	int did_changed;

	if (!lpfc_is_link_up(phba))
		return;

	if (phba->link_state == LPFC_CLEAR_LA)
		clear_la_pending = 1;
	else
		clear_la_pending = 0;

	if (vport->port_state < LPFC_VPORT_READY)
		vport->port_state = LPFC_DISC_AUTH;

	lpfc_set_disctmo(vport);

	if (vport->fc_prevDID == vport->fc_myDID)
		did_changed = 0;
	else
		did_changed = 1;

	vport->fc_prevDID = vport->fc_myDID;
	vport->num_disc_nodes = 0;

	/* Start Discovery state <hba_state> */
	lpfc_printf_log(phba, KERN_INFO, LOG_DISCOVERY,
			"%d (%d):0202 Start Discovery hba state x%x "
			"Data: x%x x%x x%x\n",
			phba->brd_no, vport->vpi, vport->port_state,
			vport->fc_flag, vport->fc_plogi_cnt,
			vport->fc_adisc_cnt);

	/* First do ADISCs - if any */
	num_sent = lpfc_els_disc_adisc(vport);

	if (num_sent)
		return;

	/*
	 * For SLI3, cmpl_reg_vpi will set port_state to READY, and
	 * continue discovery.
	 */
	if ((phba->sli3_options & LPFC_SLI3_NPIV_ENABLED) &&
	    !(vport->fc_flag & FC_RSCN_MODE)) {
		lpfc_issue_reg_vpi(phba, vport);
		return;
	}

	/*
	 * For SLI2, we need to set port_state to READY and continue
	 * discovery.
	 */
	if (vport->port_state < LPFC_VPORT_READY && !clear_la_pending) {
		/* If we get here, there is nothing to ADISC */
		if (vport->port_type == LPFC_PHYSICAL_PORT)
			lpfc_issue_clear_la(phba, vport);

		if (!(vport->fc_flag & FC_ABORT_DISCOVERY)) {
			vport->num_disc_nodes = 0;
			/* go thru NPR nodes and issue ELS PLOGIs */
			if (vport->fc_npr_cnt)
				lpfc_els_disc_plogi(vport);

			if (!vport->num_disc_nodes) {
				spin_lock_irq(shost->host_lock);
				vport->fc_flag &= ~FC_NDISC_ACTIVE;
				spin_unlock_irq(shost->host_lock);
				lpfc_can_disctmo(vport);
			}
		}
		vport->port_state = LPFC_VPORT_READY;
	} else {
		/* Next do PLOGIs - if any */
		num_sent = lpfc_els_disc_plogi(vport);

		if (num_sent)
			return;

		if (vport->fc_flag & FC_RSCN_MODE) {
			/* Check to see if more RSCNs came in while we
			 * were processing this one.
			 */
			if ((vport->fc_rscn_id_cnt == 0) &&
			    (!(vport->fc_flag & FC_RSCN_DISCOVERY))) {
				spin_lock_irq(shost->host_lock);
				vport->fc_flag &= ~FC_RSCN_MODE;
				spin_unlock_irq(shost->host_lock);
				lpfc_can_disctmo(vport);
			} else
				lpfc_els_handle_rscn(vport);
		}
	}
	return;
}

/*
 *  Ignore completion for all IOCBs on tx and txcmpl queue for ELS
 *  ring the match the sppecified nodelist.
 */
static void
lpfc_free_tx(struct lpfc_hba *phba, struct lpfc_nodelist *ndlp)
{
	LIST_HEAD(completions);
	struct lpfc_sli *psli;
	IOCB_t     *icmd;
	struct lpfc_iocbq    *iocb, *next_iocb;
	struct lpfc_sli_ring *pring;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];

	/* Error matching iocb on txq or txcmplq
	 * First check the txq.
	 */
	spin_lock_irq(&phba->hbalock);
	list_for_each_entry_safe(iocb, next_iocb, &pring->txq, list) {
		if (iocb->context1 != ndlp) {
			continue;
		}
		icmd = &iocb->iocb;
		if ((icmd->ulpCommand == CMD_ELS_REQUEST64_CR) ||
		    (icmd->ulpCommand == CMD_XMIT_ELS_RSP64_CX)) {

			list_move_tail(&iocb->list, &completions);
			pring->txq_cnt--;
		}
	}

	/* Next check the txcmplq */
	list_for_each_entry_safe(iocb, next_iocb, &pring->txcmplq, list) {
		if (iocb->context1 != ndlp) {
			continue;
		}
		icmd = &iocb->iocb;
		if (icmd->ulpCommand == CMD_ELS_REQUEST64_CR ||
		    icmd->ulpCommand == CMD_XMIT_ELS_RSP64_CX) {
			lpfc_sli_issue_abort_iotag(phba, pring, iocb);
		}
	}
	spin_unlock_irq(&phba->hbalock);

	while (!list_empty(&completions)) {
		iocb = list_get_first(&completions, struct lpfc_iocbq, list);
		list_del_init(&iocb->list);

		if (!iocb->iocb_cmpl)
			lpfc_sli_release_iocbq(phba, iocb);
		else {
			icmd = &iocb->iocb;
			icmd->ulpStatus = IOSTAT_LOCAL_REJECT;
			icmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
			(iocb->iocb_cmpl) (phba, iocb, iocb);
		}
	}
}

void
lpfc_disc_flush_list(struct lpfc_vport *vport)
{
	struct lpfc_nodelist *ndlp, *next_ndlp;
	struct lpfc_hba *phba = vport->phba;

	if (vport->fc_plogi_cnt || vport->fc_adisc_cnt) {
		list_for_each_entry_safe(ndlp, next_ndlp, &vport->fc_nodes,
					 nlp_listp) {
			if (ndlp->nlp_state == NLP_STE_PLOGI_ISSUE ||
			    ndlp->nlp_state == NLP_STE_ADISC_ISSUE) {
				lpfc_free_tx(phba, ndlp);
				lpfc_nlp_put(ndlp);
			}
		}
	}
}

void
lpfc_cleanup_discovery_resources(struct lpfc_vport *vport)
{
	lpfc_els_flush_rscn(vport);
	lpfc_els_flush_cmd(vport);
	lpfc_disc_flush_list(vport);
}

/*****************************************************************************/
/*
 * NAME:     lpfc_disc_timeout
 *
 * FUNCTION: Fibre Channel driver discovery timeout routine.
 *
 * EXECUTION ENVIRONMENT: interrupt only
 *
 * CALLED FROM:
 *      Timer function
 *
 * RETURNS:
 *      none
 */
/*****************************************************************************/
void
lpfc_disc_timeout(unsigned long ptr)
{
	struct lpfc_vport *vport = (struct lpfc_vport *) ptr;
	struct lpfc_hba   *phba = vport->phba;
	unsigned long flags = 0;

	if (unlikely(!phba))
		return;

	if ((vport->work_port_events & WORKER_DISC_TMO) == 0) {
		spin_lock_irqsave(&vport->work_port_lock, flags);
		vport->work_port_events |= WORKER_DISC_TMO;
		spin_unlock_irqrestore(&vport->work_port_lock, flags);

		spin_lock_irqsave(&phba->hbalock, flags);
		if (phba->work_wait)
			lpfc_worker_wake_up(phba);
		spin_unlock_irqrestore(&phba->hbalock, flags);
	}
	return;
}

static void
lpfc_disc_timeout_handler(struct lpfc_vport *vport)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct lpfc_hba  *phba = vport->phba;
	struct lpfc_sli  *psli = &phba->sli;
	struct lpfc_nodelist *ndlp, *next_ndlp;
	LPFC_MBOXQ_t *initlinkmbox;
	int rc, clrlaerr = 0;

	if (!(vport->fc_flag & FC_DISC_TMO))
		return;

	spin_lock_irq(shost->host_lock);
	vport->fc_flag &= ~FC_DISC_TMO;
	spin_unlock_irq(shost->host_lock);

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_CMD,
		"disc timeout:    state:x%x rtry:x%x flg:x%x",
		vport->port_state, vport->fc_ns_retry, vport->fc_flag);

	switch (vport->port_state) {

	case LPFC_LOCAL_CFG_LINK:
	/* port_state is identically  LPFC_LOCAL_CFG_LINK while waiting for
	 * FAN
	 */
				/* FAN timeout */
		lpfc_printf_log(phba, KERN_WARNING, LOG_DISCOVERY,
				"%d (%d):0221 FAN timeout\n",
				phba->brd_no, vport->vpi);

		/* Start discovery by sending FLOGI, clean up old rpis */
		list_for_each_entry_safe(ndlp, next_ndlp, &vport->fc_nodes,
					 nlp_listp) {
			if (ndlp->nlp_state != NLP_STE_NPR_NODE)
				continue;
			if (ndlp->nlp_type & NLP_FABRIC) {
				/* Clean up the ndlp on Fabric connections */
				lpfc_drop_node(vport, ndlp);
			} else if (!(ndlp->nlp_flag & NLP_NPR_ADISC)) {
				/* Fail outstanding IO now since device
				 * is marked for PLOGI.
				 */
				lpfc_unreg_rpi(vport, ndlp);
			}
		}
		if (vport->port_state != LPFC_FLOGI) {
			vport->port_state = LPFC_FLOGI;
			lpfc_set_disctmo(vport);
			lpfc_initial_flogi(vport);
		}
		break;

	case LPFC_FDISC:
	case LPFC_FLOGI:
	/* port_state is identically LPFC_FLOGI while waiting for FLOGI cmpl */
		/* Initial FLOGI timeout */
		lpfc_printf_log(phba, KERN_ERR, LOG_DISCOVERY,
				"%d (%d):0222 Initial %s timeout\n",
				phba->brd_no, vport->vpi,
				vport->vpi ? "FLOGI" : "FDISC");

		/* Assume no Fabric and go on with discovery.
		 * Check for outstanding ELS FLOGI to abort.
		 */

		/* FLOGI failed, so just use loop map to make discovery list */
		lpfc_disc_list_loopmap(vport);

		/* Start discovery */
		lpfc_disc_start(vport);
		break;

	case LPFC_FABRIC_CFG_LINK:
	/* hba_state is identically LPFC_FABRIC_CFG_LINK while waiting for
	   NameServer login */
		lpfc_printf_log(phba, KERN_ERR, LOG_DISCOVERY,
				"%d (%d):0223 Timeout while waiting for "
				"NameServer login\n",
				phba->brd_no, vport->vpi);

		/* Next look for NameServer ndlp */
		ndlp = lpfc_findnode_did(vport, NameServer_DID);
		if (ndlp)
			lpfc_nlp_put(ndlp);
		/* Start discovery */
		lpfc_disc_start(vport);
		break;

	case LPFC_NS_QRY:
	/* Check for wait for NameServer Rsp timeout */
		lpfc_printf_log(phba, KERN_ERR, LOG_DISCOVERY,
				"%d (%d):0224 NameServer Query timeout "
				"Data: x%x x%x\n",
				phba->brd_no, vport->vpi,
				vport->fc_ns_retry, LPFC_MAX_NS_RETRY);

		if (vport->fc_ns_retry < LPFC_MAX_NS_RETRY) {
			/* Try it one more time */
			vport->fc_ns_retry++;
			rc = lpfc_ns_cmd(vport, SLI_CTNS_GID_FT,
					 vport->fc_ns_retry, 0);
			if (rc == 0)
				break;
		}
		vport->fc_ns_retry = 0;

		/*
		 * Discovery is over.
		 * set port_state to PORT_READY if SLI2.
		 * cmpl_reg_vpi will set port_state to READY for SLI3.
		 */
		if (phba->sli3_options & LPFC_SLI3_NPIV_ENABLED)
			lpfc_issue_reg_vpi(phba, vport);
		else  {	/* NPIV Not enabled */
			lpfc_issue_clear_la(phba, vport);
			vport->port_state = LPFC_VPORT_READY;
		}

		/* Setup and issue mailbox INITIALIZE LINK command */
		initlinkmbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
		if (!initlinkmbox) {
			lpfc_printf_log(phba, KERN_ERR, LOG_DISCOVERY,
					"%d (%d):0206 Device Discovery "
					"completion error\n",
					phba->brd_no, vport->vpi);
			phba->link_state = LPFC_HBA_ERROR;
			break;
		}

		lpfc_linkdown(phba);
		lpfc_init_link(phba, initlinkmbox, phba->cfg_topology,
			       phba->cfg_link_speed);
		initlinkmbox->mb.un.varInitLnk.lipsr_AL_PA = 0;
		initlinkmbox->vport = vport;
		initlinkmbox->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
		rc = lpfc_sli_issue_mbox(phba, initlinkmbox,
					 (MBX_NOWAIT | MBX_STOP_IOCB));
		lpfc_set_loopback_flag(phba);
		if (rc == MBX_NOT_FINISHED)
			mempool_free(initlinkmbox, phba->mbox_mem_pool);

		break;

	case LPFC_DISC_AUTH:
	/* Node Authentication timeout */
		lpfc_printf_log(phba, KERN_ERR, LOG_DISCOVERY,
				"%d (%d):0227 Node Authentication timeout\n",
				phba->brd_no, vport->vpi);
		lpfc_disc_flush_list(vport);

		/*
		 * set port_state to PORT_READY if SLI2.
		 * cmpl_reg_vpi will set port_state to READY for SLI3.
		 */
		if (phba->sli3_options & LPFC_SLI3_NPIV_ENABLED)
			lpfc_issue_reg_vpi(phba, vport);
		else {	/* NPIV Not enabled */
			lpfc_issue_clear_la(phba, vport);
			vport->port_state = LPFC_VPORT_READY;
		}
		break;

	case LPFC_VPORT_READY:
		if (vport->fc_flag & FC_RSCN_MODE) {
			lpfc_printf_log(phba, KERN_ERR, LOG_DISCOVERY,
					"%d (%d):0231 RSCN timeout Data: x%x "
					"x%x\n",
					phba->brd_no, vport->vpi,
					vport->fc_ns_retry, LPFC_MAX_NS_RETRY);

			/* Cleanup any outstanding ELS commands */
			lpfc_els_flush_cmd(vport);

			lpfc_els_flush_rscn(vport);
			lpfc_disc_flush_list(vport);
		}
		break;

	default:
		lpfc_printf_log(phba, KERN_ERR, LOG_DISCOVERY,
				"%d (%d):0229 Unexpected discovery timeout, "
				"vport State x%x\n",
				phba->brd_no, vport->vpi, vport->port_state);

		break;
	}

	switch (phba->link_state) {
	case LPFC_CLEAR_LA:
				/* CLEAR LA timeout */
		lpfc_printf_log(phba, KERN_ERR, LOG_DISCOVERY,
				"%d (%d):0228 CLEAR LA timeout\n",
				phba->brd_no, vport->vpi);
		clrlaerr = 1;
		break;

	case LPFC_LINK_UNKNOWN:
	case LPFC_WARM_START:
	case LPFC_INIT_START:
	case LPFC_INIT_MBX_CMDS:
	case LPFC_LINK_DOWN:
	case LPFC_LINK_UP:
	case LPFC_HBA_ERROR:
		lpfc_printf_log(phba, KERN_ERR, LOG_DISCOVERY,
				"%d (%d):0230 Unexpected timeout, hba link "
				"state x%x\n",
				phba->brd_no, vport->vpi, phba->link_state);
		clrlaerr = 1;
		break;

	case LPFC_HBA_READY:
		break;
	}

	if (clrlaerr) {
		lpfc_disc_flush_list(vport);
		psli->ring[(psli->extra_ring)].flag &= ~LPFC_STOP_IOCB_EVENT;
		psli->ring[(psli->fcp_ring)].flag &= ~LPFC_STOP_IOCB_EVENT;
		psli->ring[(psli->next_ring)].flag &= ~LPFC_STOP_IOCB_EVENT;
		vport->port_state = LPFC_VPORT_READY;
	}

	return;
}

/*
 * This routine handles processing a NameServer REG_LOGIN mailbox
 * command upon completion. It is setup in the LPFC_MBOXQ
 * as the completion routine when the command is
 * handed off to the SLI layer.
 */
void
lpfc_mbx_cmpl_fdmi_reg_login(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmb)
{
	MAILBOX_t *mb = &pmb->mb;
	struct lpfc_dmabuf   *mp = (struct lpfc_dmabuf *) (pmb->context1);
	struct lpfc_nodelist *ndlp = (struct lpfc_nodelist *) pmb->context2;
	struct lpfc_vport    *vport = pmb->vport;

	pmb->context1 = NULL;

	ndlp->nlp_rpi = mb->un.varWords[0];
	ndlp->nlp_type |= NLP_FABRIC;
	lpfc_nlp_set_state(vport, ndlp, NLP_STE_UNMAPPED_NODE);

	/*
	 * Start issuing Fabric-Device Management Interface (FDMI) command to
	 * 0xfffffa (FDMI well known port) or Delay issuing FDMI command if
	 * fdmi-on=2 (supporting RPA/hostnmae)
	 */

	if (phba->cfg_fdmi_on == 1)
		lpfc_fdmi_cmd(vport, ndlp, SLI_MGMT_DHBA);
	else
		mod_timer(&vport->fc_fdmitmo, jiffies + HZ * 60);

				/* Mailbox took a reference to the node */
	lpfc_nlp_put(ndlp);
	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
	mempool_free(pmb, phba->mbox_mem_pool);

	return;
}

static int
lpfc_filter_by_rpi(struct lpfc_nodelist *ndlp, void *param)
{
	uint16_t *rpi = param;

	return ndlp->nlp_rpi == *rpi;
}

static int
lpfc_filter_by_wwpn(struct lpfc_nodelist *ndlp, void *param)
{
	return memcmp(&ndlp->nlp_portname, param,
		      sizeof(ndlp->nlp_portname)) == 0;
}

struct lpfc_nodelist *
__lpfc_find_node(struct lpfc_vport *vport, node_filter filter, void *param)
{
	struct lpfc_nodelist *ndlp;

	list_for_each_entry(ndlp, &vport->fc_nodes, nlp_listp) {
		if (ndlp->nlp_state != NLP_STE_UNUSED_NODE &&
		    filter(ndlp, param))
			return ndlp;
	}
	return NULL;
}

/*
 * Search node lists for a remote port matching filter criteria
 * Caller needs to hold host_lock before calling this routine.
 */
struct lpfc_nodelist *
lpfc_find_node(struct lpfc_vport *vport, node_filter filter, void *param)
{
	struct Scsi_Host     *shost = lpfc_shost_from_vport(vport);
	struct lpfc_nodelist *ndlp;

	spin_lock_irq(shost->host_lock);
	ndlp = __lpfc_find_node(vport, filter, param);
	spin_unlock_irq(shost->host_lock);
	return ndlp;
}

/*
 * This routine looks up the ndlp lists for the given RPI. If rpi found it
 * returns the node list element pointer else return NULL.
 */
struct lpfc_nodelist *
__lpfc_findnode_rpi(struct lpfc_vport *vport, uint16_t rpi)
{
	return __lpfc_find_node(vport, lpfc_filter_by_rpi, &rpi);
}

struct lpfc_nodelist *
lpfc_findnode_rpi(struct lpfc_vport *vport, uint16_t rpi)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct lpfc_nodelist *ndlp;

	spin_lock_irq(shost->host_lock);
	ndlp = __lpfc_findnode_rpi(vport, rpi);
	spin_unlock_irq(shost->host_lock);
	return ndlp;
}

/*
 * This routine looks up the ndlp lists for the given WWPN. If WWPN found it
 * returns the node element list pointer else return NULL.
 */
struct lpfc_nodelist *
lpfc_findnode_wwpn(struct lpfc_vport *vport, struct lpfc_name *wwpn)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct lpfc_nodelist *ndlp;

	spin_lock_irq(shost->host_lock);
	ndlp = __lpfc_find_node(vport, lpfc_filter_by_wwpn, wwpn);
	spin_unlock_irq(shost->host_lock);
	return ndlp;
}

void
lpfc_dev_loss_delay(unsigned long ptr)
{
	struct lpfc_nodelist *ndlp = (struct lpfc_nodelist *) ptr;
	struct lpfc_vport *vport = ndlp->vport;
	struct lpfc_hba   *phba = vport->phba;
	struct lpfc_work_evt  *evtp = &ndlp->dev_loss_evt;
	unsigned long flags;

	evtp = &ndlp->dev_loss_evt;

	spin_lock_irqsave(&phba->hbalock, flags);
	if (!list_empty(&evtp->evt_listp)) {
		spin_unlock_irqrestore(&phba->hbalock, flags);
		return;
	}

	evtp->evt_arg1  = ndlp;
	evtp->evt       = LPFC_EVT_DEV_LOSS_DELAY;
	list_add_tail(&evtp->evt_listp, &phba->work_list);
	if (phba->work_wait)
		lpfc_worker_wake_up(phba);
	spin_unlock_irqrestore(&phba->hbalock, flags);
	return;
}

void
lpfc_nlp_init(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
	      uint32_t did)
{
	memset(ndlp, 0, sizeof (struct lpfc_nodelist));
	INIT_LIST_HEAD(&ndlp->els_retry_evt.evt_listp);
	INIT_LIST_HEAD(&ndlp->dev_loss_evt.evt_listp);
	init_timer(&ndlp->nlp_delayfunc);
	ndlp->nlp_delayfunc.function = lpfc_els_retry_delay;
	ndlp->nlp_delayfunc.data = (unsigned long)ndlp;
	ndlp->nlp_DID = did;
	ndlp->vport = vport;
	ndlp->nlp_sid = NLP_NO_SID;
	INIT_LIST_HEAD(&ndlp->nlp_listp);
	kref_init(&ndlp->kref);

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_NODE,
		"node init:       did:x%x",
		ndlp->nlp_DID, 0, 0);

	return;
}

void
lpfc_nlp_release(struct kref *kref)
{
	struct lpfc_nodelist *ndlp = container_of(kref, struct lpfc_nodelist,
						  kref);

	lpfc_debugfs_disc_trc(ndlp->vport, LPFC_DISC_TRC_NODE,
		"node release:    did:x%x flg:x%x type:x%x",
		ndlp->nlp_DID, ndlp->nlp_flag, ndlp->nlp_type);

	lpfc_nlp_remove(ndlp->vport, ndlp);
	mempool_free(ndlp, ndlp->vport->phba->nlp_mem_pool);
}

struct lpfc_nodelist *
lpfc_nlp_get(struct lpfc_nodelist *ndlp)
{
	if (ndlp)
		kref_get(&ndlp->kref);
	return ndlp;
}

int
lpfc_nlp_put(struct lpfc_nodelist *ndlp)
{
	return ndlp ? kref_put(&ndlp->kref, lpfc_nlp_release) : 0;
}
