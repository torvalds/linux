/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2004-2006 Emulex.  All rights reserved.           *
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

static void lpfc_disc_timeout_handler(struct lpfc_hba *);

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

	phba = ndlp->nlp_phba;

	spin_lock_irq(phba->host->host_lock);
	if (ndlp->nlp_sid != NLP_NO_SID) {
		lpfc_sli_abort_iocb(phba, &phba->sli.ring[phba->sli.fcp_ring],
			ndlp->nlp_sid, 0, 0, LPFC_CTX_TGT);
	}
	spin_unlock_irq(phba->host->host_lock);

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
	uint8_t *name;
	int warn_on = 0;
	struct lpfc_hba *phba;

	rdata = rport->dd_data;
	ndlp = rdata->pnode;

	if (!ndlp) {
		if (rport->roles & FC_RPORT_ROLE_FCP_TARGET)
			printk(KERN_ERR "Cannot find remote node"
			" for rport in dev_loss_tmo_callbk x%x\n",
			rport->port_id);
		return;
	}

	name = (uint8_t *)&ndlp->nlp_portname;
	phba = ndlp->nlp_phba;

	spin_lock_irq(phba->host->host_lock);

	if (ndlp->nlp_sid != NLP_NO_SID) {
		warn_on = 1;
		/* flush the target */
		lpfc_sli_abort_iocb(phba, &phba->sli.ring[phba->sli.fcp_ring],
			ndlp->nlp_sid, 0, 0, LPFC_CTX_TGT);
	}
	if (phba->fc_flag & FC_UNLOADING)
		warn_on = 0;

	spin_unlock_irq(phba->host->host_lock);

	if (warn_on) {
		lpfc_printf_log(phba, KERN_ERR, LOG_DISCOVERY,
				"%d:0203 Devloss timeout on "
				"WWPN %x:%x:%x:%x:%x:%x:%x:%x "
				"NPort x%x Data: x%x x%x x%x\n",
				phba->brd_no,
				*name, *(name+1), *(name+2), *(name+3),
				*(name+4), *(name+5), *(name+6), *(name+7),
				ndlp->nlp_DID, ndlp->nlp_flag,
				ndlp->nlp_state, ndlp->nlp_rpi);
	} else {
		lpfc_printf_log(phba, KERN_INFO, LOG_DISCOVERY,
				"%d:0204 Devloss timeout on "
				"WWPN %x:%x:%x:%x:%x:%x:%x:%x "
				"NPort x%x Data: x%x x%x x%x\n",
				phba->brd_no,
				*name, *(name+1), *(name+2), *(name+3),
				*(name+4), *(name+5), *(name+6), *(name+7),
				ndlp->nlp_DID, ndlp->nlp_flag,
				ndlp->nlp_state, ndlp->nlp_rpi);
	}

	ndlp->rport = NULL;
	rdata->pnode = NULL;

	if (!(phba->fc_flag & FC_UNLOADING))
		lpfc_disc_state_machine(phba, ndlp, NULL, NLP_EVT_DEVICE_RM);

	return;
}

static void
lpfc_work_list_done(struct lpfc_hba * phba)
{
	struct lpfc_work_evt  *evtp = NULL;
	struct lpfc_nodelist  *ndlp;
	int free_evt;

	spin_lock_irq(phba->host->host_lock);
	while(!list_empty(&phba->work_list)) {
		list_remove_head((&phba->work_list), evtp, typeof(*evtp),
				 evt_listp);
		spin_unlock_irq(phba->host->host_lock);
		free_evt = 1;
		switch (evtp->evt) {
		case LPFC_EVT_ELS_RETRY:
			ndlp = (struct lpfc_nodelist *)(evtp->evt_arg1);
			lpfc_els_retry_delay_handler(ndlp);
			free_evt = 0;
			break;
		case LPFC_EVT_ONLINE:
			if (phba->hba_state < LPFC_LINK_DOWN)
				*(int *)(evtp->evt_arg1)  = lpfc_online(phba);
			else
				*(int *)(evtp->evt_arg1)  = 0;
			complete((struct completion *)(evtp->evt_arg2));
			break;
		case LPFC_EVT_OFFLINE:
			if (phba->hba_state >= LPFC_LINK_DOWN)
				lpfc_offline(phba);
			lpfc_sli_brdrestart(phba);
			*(int *)(evtp->evt_arg1) =
				lpfc_sli_brdready(phba,HS_FFRDY | HS_MBRDY);
			complete((struct completion *)(evtp->evt_arg2));
			break;
		case LPFC_EVT_WARM_START:
			if (phba->hba_state >= LPFC_LINK_DOWN)
				lpfc_offline(phba);
			lpfc_reset_barrier(phba);
			lpfc_sli_brdreset(phba);
			lpfc_hba_down_post(phba);
			*(int *)(evtp->evt_arg1) =
				lpfc_sli_brdready(phba, HS_MBRDY);
			complete((struct completion *)(evtp->evt_arg2));
			break;
		case LPFC_EVT_KILL:
			if (phba->hba_state >= LPFC_LINK_DOWN)
				lpfc_offline(phba);
			*(int *)(evtp->evt_arg1)
				= (phba->stopped) ? 0 : lpfc_sli_brdkill(phba);
			complete((struct completion *)(evtp->evt_arg2));
			break;
		}
		if (free_evt)
			kfree(evtp);
		spin_lock_irq(phba->host->host_lock);
	}
	spin_unlock_irq(phba->host->host_lock);

}

static void
lpfc_work_done(struct lpfc_hba * phba)
{
	struct lpfc_sli_ring *pring;
	int i;
	uint32_t ha_copy;
	uint32_t control;
	uint32_t work_hba_events;

	spin_lock_irq(phba->host->host_lock);
	ha_copy = phba->work_ha;
	phba->work_ha = 0;
	work_hba_events=phba->work_hba_events;
	spin_unlock_irq(phba->host->host_lock);

	if (ha_copy & HA_ERATT)
		lpfc_handle_eratt(phba);

	if (ha_copy & HA_MBATT)
		lpfc_sli_handle_mb_event(phba);

	if (ha_copy & HA_LATT)
		lpfc_handle_latt(phba);

	if (work_hba_events & WORKER_DISC_TMO)
		lpfc_disc_timeout_handler(phba);

	if (work_hba_events & WORKER_ELS_TMO)
		lpfc_els_timeout_handler(phba);

	if (work_hba_events & WORKER_MBOX_TMO)
		lpfc_mbox_timeout_handler(phba);

	if (work_hba_events & WORKER_FDMI_TMO)
		lpfc_fdmi_tmo_handler(phba);

	spin_lock_irq(phba->host->host_lock);
	phba->work_hba_events &= ~work_hba_events;
	spin_unlock_irq(phba->host->host_lock);

	for (i = 0; i < phba->sli.num_rings; i++, ha_copy >>= 4) {
		pring = &phba->sli.ring[i];
		if ((ha_copy & HA_RXATT)
		    || (pring->flag & LPFC_DEFERRED_RING_EVENT)) {
			if (pring->flag & LPFC_STOP_IOCB_MASK) {
				pring->flag |= LPFC_DEFERRED_RING_EVENT;
			} else {
				lpfc_sli_handle_slow_ring_event(phba, pring,
								(ha_copy &
								 HA_RXMASK));
				pring->flag &= ~LPFC_DEFERRED_RING_EVENT;
			}
			/*
			 * Turn on Ring interrupts
			 */
			spin_lock_irq(phba->host->host_lock);
			control = readl(phba->HCregaddr);
			control |= (HC_R0INT_ENA << i);
			writel(control, phba->HCregaddr);
			readl(phba->HCregaddr); /* flush */
			spin_unlock_irq(phba->host->host_lock);
		}
	}

	lpfc_work_list_done (phba);

}

static int
check_work_wait_done(struct lpfc_hba *phba) {

	spin_lock_irq(phba->host->host_lock);
	if (phba->work_ha ||
	    phba->work_hba_events ||
	    (!list_empty(&phba->work_list)) ||
	    kthread_should_stop()) {
		spin_unlock_irq(phba->host->host_lock);
		return 1;
	} else {
		spin_unlock_irq(phba->host->host_lock);
		return 0;
	}
}

int
lpfc_do_work(void *p)
{
	struct lpfc_hba *phba = p;
	int rc;
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(work_waitq);

	set_user_nice(current, -20);
	phba->work_wait = &work_waitq;

	while (1) {

		rc = wait_event_interruptible(work_waitq,
						check_work_wait_done(phba));
		BUG_ON(rc);

		if (kthread_should_stop())
			break;

		lpfc_work_done(phba);

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
lpfc_workq_post_event(struct lpfc_hba * phba, void *arg1, void *arg2,
		      uint32_t evt)
{
	struct lpfc_work_evt  *evtp;

	/*
	 * All Mailbox completions and LPFC_ELS_RING rcv ring IOCB events will
	 * be queued to worker thread for processing
	 */
	evtp = kmalloc(sizeof(struct lpfc_work_evt), GFP_KERNEL);
	if (!evtp)
		return 0;

	evtp->evt_arg1  = arg1;
	evtp->evt_arg2  = arg2;
	evtp->evt       = evt;

	spin_lock_irq(phba->host->host_lock);
	list_add_tail(&evtp->evt_listp, &phba->work_list);
	if (phba->work_wait)
		wake_up(phba->work_wait);
	spin_unlock_irq(phba->host->host_lock);

	return 1;
}

int
lpfc_linkdown(struct lpfc_hba * phba)
{
	struct lpfc_sli       *psli;
	struct lpfc_nodelist  *ndlp, *next_ndlp;
	struct list_head *listp, *node_list[7];
	LPFC_MBOXQ_t     *mb;
	int               rc, i;

	psli = &phba->sli;
	/* sysfs or selective reset may call this routine to clean up */
	if (phba->hba_state >= LPFC_LINK_DOWN) {
		if (phba->hba_state == LPFC_LINK_DOWN)
			return 0;

		spin_lock_irq(phba->host->host_lock);
		phba->hba_state = LPFC_LINK_DOWN;
		spin_unlock_irq(phba->host->host_lock);
	}

	fc_host_post_event(phba->host, fc_get_event_number(),
			FCH_EVT_LINKDOWN, 0);

	/* Clean up any firmware default rpi's */
	if ((mb = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL))) {
		lpfc_unreg_did(phba, 0xffffffff, mb);
		mb->mbox_cmpl=lpfc_sli_def_mbox_cmpl;
		if (lpfc_sli_issue_mbox(phba, mb, (MBX_NOWAIT | MBX_STOP_IOCB))
		    == MBX_NOT_FINISHED) {
			mempool_free( mb, phba->mbox_mem_pool);
		}
	}

	/* Cleanup any outstanding RSCN activity */
	lpfc_els_flush_rscn(phba);

	/* Cleanup any outstanding ELS commands */
	lpfc_els_flush_cmd(phba);

	/* Issue a LINK DOWN event to all nodes */
	node_list[0] = &phba->fc_npr_list;  /* MUST do this list first */
	node_list[1] = &phba->fc_nlpmap_list;
	node_list[2] = &phba->fc_nlpunmap_list;
	node_list[3] = &phba->fc_prli_list;
	node_list[4] = &phba->fc_reglogin_list;
	node_list[5] = &phba->fc_adisc_list;
	node_list[6] = &phba->fc_plogi_list;
	for (i = 0; i < 7; i++) {
		listp = node_list[i];
		if (list_empty(listp))
			continue;

		list_for_each_entry_safe(ndlp, next_ndlp, listp, nlp_listp) {

			rc = lpfc_disc_state_machine(phba, ndlp, NULL,
					     NLP_EVT_DEVICE_RECOVERY);

		}
	}

	/* free any ndlp's on unused list */
	list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_unused_list,
				nlp_listp) {
		lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
	}

	/* Setup myDID for link up if we are in pt2pt mode */
	if (phba->fc_flag & FC_PT2PT) {
		phba->fc_myDID = 0;
		if ((mb = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL))) {
			lpfc_config_link(phba, mb);
			mb->mbox_cmpl=lpfc_sli_def_mbox_cmpl;
			if (lpfc_sli_issue_mbox
			    (phba, mb, (MBX_NOWAIT | MBX_STOP_IOCB))
			    == MBX_NOT_FINISHED) {
				mempool_free( mb, phba->mbox_mem_pool);
			}
		}
		spin_lock_irq(phba->host->host_lock);
		phba->fc_flag &= ~(FC_PT2PT | FC_PT2PT_PLOGI);
		spin_unlock_irq(phba->host->host_lock);
	}
	spin_lock_irq(phba->host->host_lock);
	phba->fc_flag &= ~FC_LBIT;
	spin_unlock_irq(phba->host->host_lock);

	/* Turn off discovery timer if its running */
	lpfc_can_disctmo(phba);

	/* Must process IOCBs on all rings to handle ABORTed I/Os */
	return 0;
}

static int
lpfc_linkup(struct lpfc_hba * phba)
{
	struct lpfc_nodelist *ndlp, *next_ndlp;
	struct list_head *listp, *node_list[7];
	int i;

	fc_host_post_event(phba->host, fc_get_event_number(),
			FCH_EVT_LINKUP, 0);

	spin_lock_irq(phba->host->host_lock);
	phba->hba_state = LPFC_LINK_UP;
	phba->fc_flag &= ~(FC_PT2PT | FC_PT2PT_PLOGI | FC_ABORT_DISCOVERY |
			   FC_RSCN_MODE | FC_NLP_MORE | FC_RSCN_DISCOVERY);
	phba->fc_flag |= FC_NDISC_ACTIVE;
	phba->fc_ns_retry = 0;
	spin_unlock_irq(phba->host->host_lock);


	node_list[0] = &phba->fc_plogi_list;
	node_list[1] = &phba->fc_adisc_list;
	node_list[2] = &phba->fc_reglogin_list;
	node_list[3] = &phba->fc_prli_list;
	node_list[4] = &phba->fc_nlpunmap_list;
	node_list[5] = &phba->fc_nlpmap_list;
	node_list[6] = &phba->fc_npr_list;
	for (i = 0; i < 7; i++) {
		listp = node_list[i];
		if (list_empty(listp))
			continue;

		list_for_each_entry_safe(ndlp, next_ndlp, listp, nlp_listp) {
			if (phba->fc_flag & FC_LBIT) {
				if (ndlp->nlp_type & NLP_FABRIC) {
					/* On Linkup its safe to clean up the
					 * ndlp from Fabric connections.
					 */
					lpfc_nlp_list(phba, ndlp,
							NLP_UNUSED_LIST);
				} else if (!(ndlp->nlp_flag & NLP_NPR_ADISC)) {
					/* Fail outstanding IO now since device
					 * is marked for PLOGI.
					 */
					lpfc_unreg_rpi(phba, ndlp);
				}
			}
		}
	}

	/* free any ndlp's on unused list */
	list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_unused_list,
				nlp_listp) {
		lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
	}

	return 0;
}

/*
 * This routine handles processing a CLEAR_LA mailbox
 * command upon completion. It is setup in the LPFC_MBOXQ
 * as the completion routine when the command is
 * handed off to the SLI layer.
 */
void
lpfc_mbx_cmpl_clear_la(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb)
{
	struct lpfc_sli *psli;
	MAILBOX_t *mb;
	uint32_t control;

	psli = &phba->sli;
	mb = &pmb->mb;
	/* Since we don't do discovery right now, turn these off here */
	psli->ring[psli->extra_ring].flag &= ~LPFC_STOP_IOCB_EVENT;
	psli->ring[psli->fcp_ring].flag &= ~LPFC_STOP_IOCB_EVENT;
	psli->ring[psli->next_ring].flag &= ~LPFC_STOP_IOCB_EVENT;

	/* Check for error */
	if ((mb->mbxStatus) && (mb->mbxStatus != 0x1601)) {
		/* CLEAR_LA mbox error <mbxStatus> state <hba_state> */
		lpfc_printf_log(phba, KERN_ERR, LOG_MBOX,
				"%d:0320 CLEAR_LA mbxStatus error x%x hba "
				"state x%x\n",
				phba->brd_no, mb->mbxStatus, phba->hba_state);

		phba->hba_state = LPFC_HBA_ERROR;
		goto out;
	}

	if (phba->fc_flag & FC_ABORT_DISCOVERY)
		goto out;

	phba->num_disc_nodes = 0;
	/* go thru NPR list and issue ELS PLOGIs */
	if (phba->fc_npr_cnt) {
		lpfc_els_disc_plogi(phba);
	}

	if (!phba->num_disc_nodes) {
		spin_lock_irq(phba->host->host_lock);
		phba->fc_flag &= ~FC_NDISC_ACTIVE;
		spin_unlock_irq(phba->host->host_lock);
	}

	phba->hba_state = LPFC_HBA_READY;

out:
	/* Device Discovery completes */
	lpfc_printf_log(phba,
			 KERN_INFO,
			 LOG_DISCOVERY,
			 "%d:0225 Device Discovery completes\n",
			 phba->brd_no);

	mempool_free( pmb, phba->mbox_mem_pool);

	spin_lock_irq(phba->host->host_lock);
	phba->fc_flag &= ~FC_ABORT_DISCOVERY;
	if (phba->fc_flag & FC_ESTABLISH_LINK) {
		phba->fc_flag &= ~FC_ESTABLISH_LINK;
	}
	spin_unlock_irq(phba->host->host_lock);

	del_timer_sync(&phba->fc_estabtmo);

	lpfc_can_disctmo(phba);

	/* turn on Link Attention interrupts */
	spin_lock_irq(phba->host->host_lock);
	psli->sli_flag |= LPFC_PROCESS_LA;
	control = readl(phba->HCregaddr);
	control |= HC_LAINT_ENA;
	writel(control, phba->HCregaddr);
	readl(phba->HCregaddr); /* flush */
	spin_unlock_irq(phba->host->host_lock);

	return;
}

static void
lpfc_mbx_cmpl_local_config_link(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmb)
{
	struct lpfc_sli *psli = &phba->sli;
	int rc;

	if (pmb->mb.mbxStatus)
		goto out;

	mempool_free(pmb, phba->mbox_mem_pool);

	if (phba->fc_topology == TOPOLOGY_LOOP &&
		phba->fc_flag & FC_PUBLIC_LOOP &&
		 !(phba->fc_flag & FC_LBIT)) {
			/* Need to wait for FAN - use discovery timer
			 * for timeout.  hba_state is identically
			 * LPFC_LOCAL_CFG_LINK while waiting for FAN
			 */
			lpfc_set_disctmo(phba);
			return;
		}

	/* Start discovery by sending a FLOGI. hba_state is identically
	 * LPFC_FLOGI while waiting for FLOGI cmpl
	 */
	phba->hba_state = LPFC_FLOGI;
	lpfc_set_disctmo(phba);
	lpfc_initial_flogi(phba);
	return;

out:
	lpfc_printf_log(phba, KERN_ERR, LOG_MBOX,
			"%d:0306 CONFIG_LINK mbxStatus error x%x "
			"HBA state x%x\n",
			phba->brd_no, pmb->mb.mbxStatus, phba->hba_state);

	lpfc_linkdown(phba);

	phba->hba_state = LPFC_HBA_ERROR;

	lpfc_printf_log(phba, KERN_ERR, LOG_DISCOVERY,
			"%d:0200 CONFIG_LINK bad hba state x%x\n",
			phba->brd_no, phba->hba_state);

	lpfc_clear_la(phba, pmb);
	pmb->mbox_cmpl = lpfc_mbx_cmpl_clear_la;
	rc = lpfc_sli_issue_mbox(phba, pmb, (MBX_NOWAIT | MBX_STOP_IOCB));
	if (rc == MBX_NOT_FINISHED) {
		mempool_free(pmb, phba->mbox_mem_pool);
		lpfc_disc_flush_list(phba);
		psli->ring[(psli->extra_ring)].flag &= ~LPFC_STOP_IOCB_EVENT;
		psli->ring[(psli->fcp_ring)].flag &= ~LPFC_STOP_IOCB_EVENT;
		psli->ring[(psli->next_ring)].flag &= ~LPFC_STOP_IOCB_EVENT;
		phba->hba_state = LPFC_HBA_READY;
	}
	return;
}

static void
lpfc_mbx_cmpl_read_sparam(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb)
{
	struct lpfc_sli *psli = &phba->sli;
	MAILBOX_t *mb = &pmb->mb;
	struct lpfc_dmabuf *mp = (struct lpfc_dmabuf *) pmb->context1;


	/* Check for error */
	if (mb->mbxStatus) {
		/* READ_SPARAM mbox error <mbxStatus> state <hba_state> */
		lpfc_printf_log(phba, KERN_ERR, LOG_MBOX,
				"%d:0319 READ_SPARAM mbxStatus error x%x "
				"hba state x%x>\n",
				phba->brd_no, mb->mbxStatus, phba->hba_state);

		lpfc_linkdown(phba);
		phba->hba_state = LPFC_HBA_ERROR;
		goto out;
	}

	memcpy((uint8_t *) & phba->fc_sparam, (uint8_t *) mp->virt,
	       sizeof (struct serv_parm));
	if (phba->cfg_soft_wwnn)
		u64_to_wwn(phba->cfg_soft_wwnn, phba->fc_sparam.nodeName.u.wwn);
	if (phba->cfg_soft_wwpn)
		u64_to_wwn(phba->cfg_soft_wwpn, phba->fc_sparam.portName.u.wwn);
	memcpy((uint8_t *) & phba->fc_nodename,
	       (uint8_t *) & phba->fc_sparam.nodeName,
	       sizeof (struct lpfc_name));
	memcpy((uint8_t *) & phba->fc_portname,
	       (uint8_t *) & phba->fc_sparam.portName,
	       sizeof (struct lpfc_name));
	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
	mempool_free( pmb, phba->mbox_mem_pool);
	return;

out:
	pmb->context1 = NULL;
	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
	if (phba->hba_state != LPFC_CLEAR_LA) {
		lpfc_clear_la(phba, pmb);
		pmb->mbox_cmpl = lpfc_mbx_cmpl_clear_la;
		if (lpfc_sli_issue_mbox(phba, pmb, (MBX_NOWAIT | MBX_STOP_IOCB))
		    == MBX_NOT_FINISHED) {
			mempool_free( pmb, phba->mbox_mem_pool);
			lpfc_disc_flush_list(phba);
			psli->ring[(psli->extra_ring)].flag &=
			    ~LPFC_STOP_IOCB_EVENT;
			psli->ring[(psli->fcp_ring)].flag &=
			    ~LPFC_STOP_IOCB_EVENT;
			psli->ring[(psli->next_ring)].flag &=
			    ~LPFC_STOP_IOCB_EVENT;
			phba->hba_state = LPFC_HBA_READY;
		}
	} else {
		mempool_free( pmb, phba->mbox_mem_pool);
	}
	return;
}

static void
lpfc_mbx_process_link_up(struct lpfc_hba *phba, READ_LA_VAR *la)
{
	int i;
	LPFC_MBOXQ_t *sparam_mbox, *cfglink_mbox;
	struct lpfc_dmabuf *mp;
	int rc;

	sparam_mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	cfglink_mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);

	spin_lock_irq(phba->host->host_lock);
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
		default:
			phba->fc_linkspeed = LA_UNKNW_LINK;
			break;
	}

	phba->fc_topology = la->topology;

	if (phba->fc_topology == TOPOLOGY_LOOP) {
	/* Get Loop Map information */

		if (la->il)
			phba->fc_flag |= FC_LBIT;

		phba->fc_myDID = la->granted_AL_PA;
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
		phba->fc_myDID = phba->fc_pref_DID;
		phba->fc_flag |= FC_LBIT;
	}
	spin_unlock_irq(phba->host->host_lock);

	lpfc_linkup(phba);
	if (sparam_mbox) {
		lpfc_read_sparam(phba, sparam_mbox);
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
			return;
		}
	}

	if (cfglink_mbox) {
		phba->hba_state = LPFC_LOCAL_CFG_LINK;
		lpfc_config_link(phba, cfglink_mbox);
		cfglink_mbox->mbox_cmpl = lpfc_mbx_cmpl_local_config_link;
		rc = lpfc_sli_issue_mbox(phba, cfglink_mbox,
						(MBX_NOWAIT | MBX_STOP_IOCB));
		if (rc == MBX_NOT_FINISHED)
			mempool_free(cfglink_mbox, phba->mbox_mem_pool);
	}
}

static void
lpfc_mbx_issue_link_down(struct lpfc_hba *phba) {
	uint32_t control;
	struct lpfc_sli *psli = &phba->sli;

	lpfc_linkdown(phba);

	/* turn on Link Attention interrupts - no CLEAR_LA needed */
	spin_lock_irq(phba->host->host_lock);
	psli->sli_flag |= LPFC_PROCESS_LA;
	control = readl(phba->HCregaddr);
	control |= HC_LAINT_ENA;
	writel(control, phba->HCregaddr);
	readl(phba->HCregaddr); /* flush */
	spin_unlock_irq(phba->host->host_lock);
}

/*
 * This routine handles processing a READ_LA mailbox
 * command upon completion. It is setup in the LPFC_MBOXQ
 * as the completion routine when the command is
 * handed off to the SLI layer.
 */
void
lpfc_mbx_cmpl_read_la(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb)
{
	READ_LA_VAR *la;
	MAILBOX_t *mb = &pmb->mb;
	struct lpfc_dmabuf *mp = (struct lpfc_dmabuf *) (pmb->context1);

	/* Check for error */
	if (mb->mbxStatus) {
		lpfc_printf_log(phba,
				KERN_INFO,
				LOG_LINK_EVENT,
				"%d:1307 READ_LA mbox error x%x state x%x\n",
				phba->brd_no,
				mb->mbxStatus, phba->hba_state);
		lpfc_mbx_issue_link_down(phba);
		phba->hba_state = LPFC_HBA_ERROR;
		goto lpfc_mbx_cmpl_read_la_free_mbuf;
	}

	la = (READ_LA_VAR *) & pmb->mb.un.varReadLA;

	memcpy(&phba->alpa_map[0], mp->virt, 128);

	spin_lock_irq(phba->host->host_lock);
	if (la->pb)
		phba->fc_flag |= FC_BYPASSED_MODE;
	else
		phba->fc_flag &= ~FC_BYPASSED_MODE;
	spin_unlock_irq(phba->host->host_lock);

	if (((phba->fc_eventTag + 1) < la->eventTag) ||
	     (phba->fc_eventTag == la->eventTag)) {
		phba->fc_stat.LinkMultiEvent++;
		if (la->attType == AT_LINK_UP) {
			if (phba->fc_eventTag != 0)
				lpfc_linkdown(phba);
		}
	}

	phba->fc_eventTag = la->eventTag;

	if (la->attType == AT_LINK_UP) {
		phba->fc_stat.LinkUp++;
		lpfc_printf_log(phba, KERN_ERR, LOG_LINK_EVENT,
				"%d:1303 Link Up Event x%x received "
				"Data: x%x x%x x%x x%x\n",
				phba->brd_no, la->eventTag, phba->fc_eventTag,
				la->granted_AL_PA, la->UlnkSpeed,
				phba->alpa_map[0]);
		lpfc_mbx_process_link_up(phba, la);
	} else {
		phba->fc_stat.LinkDown++;
		lpfc_printf_log(phba, KERN_ERR, LOG_LINK_EVENT,
				"%d:1305 Link Down Event x%x received "
				"Data: x%x x%x x%x\n",
				phba->brd_no, la->eventTag, phba->fc_eventTag,
				phba->hba_state, phba->fc_flag);
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
lpfc_mbx_cmpl_reg_login(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb)
{
	struct lpfc_sli *psli;
	MAILBOX_t *mb;
	struct lpfc_dmabuf *mp;
	struct lpfc_nodelist *ndlp;

	psli = &phba->sli;
	mb = &pmb->mb;

	ndlp = (struct lpfc_nodelist *) pmb->context2;
	mp = (struct lpfc_dmabuf *) (pmb->context1);

	pmb->context1 = NULL;

	/* Good status, call state machine */
	lpfc_disc_state_machine(phba, ndlp, pmb, NLP_EVT_CMPL_REG_LOGIN);
	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
	mempool_free( pmb, phba->mbox_mem_pool);

	return;
}

/*
 * This routine handles processing a Fabric REG_LOGIN mailbox
 * command upon completion. It is setup in the LPFC_MBOXQ
 * as the completion routine when the command is
 * handed off to the SLI layer.
 */
void
lpfc_mbx_cmpl_fabric_reg_login(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb)
{
	struct lpfc_sli *psli;
	MAILBOX_t *mb;
	struct lpfc_dmabuf *mp;
	struct lpfc_nodelist *ndlp;
	struct lpfc_nodelist *ndlp_fdmi;


	psli = &phba->sli;
	mb = &pmb->mb;

	ndlp = (struct lpfc_nodelist *) pmb->context2;
	mp = (struct lpfc_dmabuf *) (pmb->context1);

	if (mb->mbxStatus) {
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		kfree(mp);
		mempool_free( pmb, phba->mbox_mem_pool);
		mempool_free( ndlp, phba->nlp_mem_pool);

		/* FLOGI failed, so just use loop map to make discovery list */
		lpfc_disc_list_loopmap(phba);

		/* Start discovery */
		lpfc_disc_start(phba);
		return;
	}

	pmb->context1 = NULL;

	ndlp->nlp_rpi = mb->un.varWords[0];
	ndlp->nlp_type |= NLP_FABRIC;
	ndlp->nlp_state = NLP_STE_UNMAPPED_NODE;
	lpfc_nlp_list(phba, ndlp, NLP_UNMAPPED_LIST);

	if (phba->hba_state == LPFC_FABRIC_CFG_LINK) {
		/* This NPort has been assigned an NPort_ID by the fabric as a
		 * result of the completed fabric login.  Issue a State Change
		 * Registration (SCR) ELS request to the fabric controller
		 * (SCR_DID) so that this NPort gets RSCN events from the
		 * fabric.
		 */
		lpfc_issue_els_scr(phba, SCR_DID, 0);

		ndlp = lpfc_findnode_did(phba, NLP_SEARCH_ALL, NameServer_DID);
		if (!ndlp) {
			/* Allocate a new node instance. If the pool is empty,
			 * start the discovery process and skip the Nameserver
			 * login process.  This is attempted again later on.
			 * Otherwise, issue a Port Login (PLOGI) to NameServer.
			 */
			ndlp = mempool_alloc(phba->nlp_mem_pool, GFP_ATOMIC);
			if (!ndlp) {
				lpfc_disc_start(phba);
				lpfc_mbuf_free(phba, mp->virt, mp->phys);
				kfree(mp);
				mempool_free( pmb, phba->mbox_mem_pool);
				return;
			} else {
				lpfc_nlp_init(phba, ndlp, NameServer_DID);
				ndlp->nlp_type |= NLP_FABRIC;
			}
		}
		ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
		lpfc_nlp_list(phba, ndlp, NLP_PLOGI_LIST);
		lpfc_issue_els_plogi(phba, NameServer_DID, 0);
		if (phba->cfg_fdmi_on) {
			ndlp_fdmi = mempool_alloc(phba->nlp_mem_pool,
								GFP_KERNEL);
			if (ndlp_fdmi) {
				lpfc_nlp_init(phba, ndlp_fdmi, FDMI_DID);
				ndlp_fdmi->nlp_type |= NLP_FABRIC;
				ndlp_fdmi->nlp_state = NLP_STE_PLOGI_ISSUE;
				lpfc_issue_els_plogi(phba, FDMI_DID, 0);
			}
		}
	}

	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
	mempool_free( pmb, phba->mbox_mem_pool);
	return;
}

/*
 * This routine handles processing a NameServer REG_LOGIN mailbox
 * command upon completion. It is setup in the LPFC_MBOXQ
 * as the completion routine when the command is
 * handed off to the SLI layer.
 */
void
lpfc_mbx_cmpl_ns_reg_login(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb)
{
	struct lpfc_sli *psli;
	MAILBOX_t *mb;
	struct lpfc_dmabuf *mp;
	struct lpfc_nodelist *ndlp;

	psli = &phba->sli;
	mb = &pmb->mb;

	ndlp = (struct lpfc_nodelist *) pmb->context2;
	mp = (struct lpfc_dmabuf *) (pmb->context1);

	if (mb->mbxStatus) {
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		kfree(mp);
		mempool_free( pmb, phba->mbox_mem_pool);
		lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);

		/* RegLogin failed, so just use loop map to make discovery
		   list */
		lpfc_disc_list_loopmap(phba);

		/* Start discovery */
		lpfc_disc_start(phba);
		return;
	}

	pmb->context1 = NULL;

	ndlp->nlp_rpi = mb->un.varWords[0];
	ndlp->nlp_type |= NLP_FABRIC;
	ndlp->nlp_state = NLP_STE_UNMAPPED_NODE;
	lpfc_nlp_list(phba, ndlp, NLP_UNMAPPED_LIST);

	if (phba->hba_state < LPFC_HBA_READY) {
		/* Link up discovery requires Fabrib registration. */
		lpfc_ns_cmd(phba, ndlp, SLI_CTNS_RNN_ID);
		lpfc_ns_cmd(phba, ndlp, SLI_CTNS_RSNN_NN);
		lpfc_ns_cmd(phba, ndlp, SLI_CTNS_RFT_ID);
		lpfc_ns_cmd(phba, ndlp, SLI_CTNS_RFF_ID);
	}

	phba->fc_ns_retry = 0;
	/* Good status, issue CT Request to NameServer */
	if (lpfc_ns_cmd(phba, ndlp, SLI_CTNS_GID_FT)) {
		/* Cannot issue NameServer Query, so finish up discovery */
		lpfc_disc_start(phba);
	}

	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
	mempool_free( pmb, phba->mbox_mem_pool);

	return;
}

static void
lpfc_register_remote_port(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp)
{
	struct fc_rport *rport;
	struct lpfc_rport_data *rdata;
	struct fc_rport_identifiers rport_ids;

	/* Remote port has reappeared. Re-register w/ FC transport */
	rport_ids.node_name = wwn_to_u64(ndlp->nlp_nodename.u.wwn);
	rport_ids.port_name = wwn_to_u64(ndlp->nlp_portname.u.wwn);
	rport_ids.port_id = ndlp->nlp_DID;
	rport_ids.roles = FC_RPORT_ROLE_UNKNOWN;

	ndlp->rport = rport = fc_remote_port_add(phba->host, 0, &rport_ids);
	if (!rport) {
		dev_printk(KERN_WARNING, &phba->pcidev->dev,
			   "Warning: fc_remote_port_add failed\n");
		return;
	}

	/* initialize static port data */
	rport->maxframe_size = ndlp->nlp_maxframe;
	rport->supported_classes = ndlp->nlp_class_sup;
	rdata = rport->dd_data;
	rdata->pnode = ndlp;

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
lpfc_unregister_remote_port(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp)
{
	struct fc_rport *rport = ndlp->rport;
	struct lpfc_rport_data *rdata = rport->dd_data;

	if (rport->scsi_target_id == -1) {
		ndlp->rport = NULL;
		rdata->pnode = NULL;
	}

	fc_remote_port_delete(rport);

	return;
}

int
lpfc_nlp_list(struct lpfc_hba * phba, struct lpfc_nodelist * nlp, int list)
{
	enum { none, unmapped, mapped } rport_add = none, rport_del = none;
	struct lpfc_sli      *psli;

	psli = &phba->sli;
	/* Sanity check to ensure we are not moving to / from the same list */
	if ((nlp->nlp_flag & NLP_LIST_MASK) == list)
		if (list != NLP_NO_LIST)
			return 0;

	spin_lock_irq(phba->host->host_lock);
	switch (nlp->nlp_flag & NLP_LIST_MASK) {
	case NLP_NO_LIST: /* Not on any list */
		break;
	case NLP_UNUSED_LIST:
		phba->fc_unused_cnt--;
		list_del(&nlp->nlp_listp);
		break;
	case NLP_PLOGI_LIST:
		phba->fc_plogi_cnt--;
		list_del(&nlp->nlp_listp);
		break;
	case NLP_ADISC_LIST:
		phba->fc_adisc_cnt--;
		list_del(&nlp->nlp_listp);
		break;
	case NLP_REGLOGIN_LIST:
		phba->fc_reglogin_cnt--;
		list_del(&nlp->nlp_listp);
		break;
	case NLP_PRLI_LIST:
		phba->fc_prli_cnt--;
		list_del(&nlp->nlp_listp);
		break;
	case NLP_UNMAPPED_LIST:
		phba->fc_unmap_cnt--;
		list_del(&nlp->nlp_listp);
		nlp->nlp_flag &= ~NLP_TGT_NO_SCSIID;
		nlp->nlp_type &= ~NLP_FC_NODE;
		phba->nport_event_cnt++;
		if (nlp->rport)
			rport_del = unmapped;
		break;
	case NLP_MAPPED_LIST:
		phba->fc_map_cnt--;
		list_del(&nlp->nlp_listp);
		phba->nport_event_cnt++;
		if (nlp->rport)
			rport_del = mapped;
		break;
	case NLP_NPR_LIST:
		phba->fc_npr_cnt--;
		list_del(&nlp->nlp_listp);
		/* Stop delay tmo if taking node off NPR list */
		if ((nlp->nlp_flag & NLP_DELAY_TMO) &&
		   (list != NLP_NPR_LIST)) {
			spin_unlock_irq(phba->host->host_lock);
			lpfc_cancel_retry_delay_tmo(phba, nlp);
			spin_lock_irq(phba->host->host_lock);
		}
		break;
	}

	nlp->nlp_flag &= ~NLP_LIST_MASK;

	/* Add NPort <did> to <num> list */
	lpfc_printf_log(phba,
			KERN_INFO,
			LOG_NODE,
			"%d:0904 Add NPort x%x to %d list Data: x%x\n",
			phba->brd_no,
			nlp->nlp_DID, list, nlp->nlp_flag);

	switch (list) {
	case NLP_NO_LIST: /* No list, just remove it */
		spin_unlock_irq(phba->host->host_lock);
		lpfc_nlp_remove(phba, nlp);
		spin_lock_irq(phba->host->host_lock);
		/* as node removed - stop further transport calls */
		rport_del = none;
		break;
	case NLP_UNUSED_LIST:
		nlp->nlp_flag |= list;
		/* Put it at the end of the unused list */
		list_add_tail(&nlp->nlp_listp, &phba->fc_unused_list);
		phba->fc_unused_cnt++;
		break;
	case NLP_PLOGI_LIST:
		nlp->nlp_flag |= list;
		/* Put it at the end of the plogi list */
		list_add_tail(&nlp->nlp_listp, &phba->fc_plogi_list);
		phba->fc_plogi_cnt++;
		break;
	case NLP_ADISC_LIST:
		nlp->nlp_flag |= list;
		/* Put it at the end of the adisc list */
		list_add_tail(&nlp->nlp_listp, &phba->fc_adisc_list);
		phba->fc_adisc_cnt++;
		break;
	case NLP_REGLOGIN_LIST:
		nlp->nlp_flag |= list;
		/* Put it at the end of the reglogin list */
		list_add_tail(&nlp->nlp_listp, &phba->fc_reglogin_list);
		phba->fc_reglogin_cnt++;
		break;
	case NLP_PRLI_LIST:
		nlp->nlp_flag |= list;
		/* Put it at the end of the prli list */
		list_add_tail(&nlp->nlp_listp, &phba->fc_prli_list);
		phba->fc_prli_cnt++;
		break;
	case NLP_UNMAPPED_LIST:
		rport_add = unmapped;
		/* ensure all vestiges of "mapped" significance are gone */
		nlp->nlp_type &= ~(NLP_FCP_TARGET | NLP_FCP_INITIATOR);
		nlp->nlp_flag |= list;
		/* Put it at the end of the unmap list */
		list_add_tail(&nlp->nlp_listp, &phba->fc_nlpunmap_list);
		phba->fc_unmap_cnt++;
		phba->nport_event_cnt++;
		nlp->nlp_flag &= ~NLP_NODEV_REMOVE;
		nlp->nlp_type |= NLP_FC_NODE;
		break;
	case NLP_MAPPED_LIST:
		rport_add = mapped;
		nlp->nlp_flag |= list;
		/* Put it at the end of the map list */
		list_add_tail(&nlp->nlp_listp, &phba->fc_nlpmap_list);
		phba->fc_map_cnt++;
		phba->nport_event_cnt++;
		nlp->nlp_flag &= ~NLP_NODEV_REMOVE;
		break;
	case NLP_NPR_LIST:
		nlp->nlp_flag |= list;
		/* Put it at the end of the npr list */
		list_add_tail(&nlp->nlp_listp, &phba->fc_npr_list);
		phba->fc_npr_cnt++;

		nlp->nlp_flag &= ~NLP_RCV_PLOGI;
		break;
	case NLP_JUST_DQ:
		break;
	}

	spin_unlock_irq(phba->host->host_lock);

	/*
	 * We make all the calls into the transport after we have
	 * moved the node between lists. This so that we don't
	 * release the lock while in-between lists.
	 */

	/* Don't upcall midlayer if we're unloading */
	if (!(phba->fc_flag & FC_UNLOADING)) {
		/*
		 * We revalidate the rport pointer as the "add" function
		 * may have removed the remote port.
		 */
		if ((rport_del != none) && nlp->rport)
			lpfc_unregister_remote_port(phba, nlp);

		if (rport_add != none) {
			/*
			 * Tell the fc transport about the port, if we haven't
			 * already. If we have, and it's a scsi entity, be
			 * sure to unblock any attached scsi devices
			 */
			if ((!nlp->rport) || (nlp->rport->port_state ==
					FC_PORTSTATE_BLOCKED))
				lpfc_register_remote_port(phba, nlp);

			/*
			 * if we added to Mapped list, but the remote port
			 * registration failed or assigned a target id outside
			 * our presentable range - move the node to the
			 * Unmapped List
			 */
			if ((rport_add == mapped) &&
			    ((!nlp->rport) ||
			     (nlp->rport->scsi_target_id == -1) ||
			     (nlp->rport->scsi_target_id >= LPFC_MAX_TARGET))) {
				nlp->nlp_state = NLP_STE_UNMAPPED_NODE;
				spin_lock_irq(phba->host->host_lock);
				nlp->nlp_flag |= NLP_TGT_NO_SCSIID;
				spin_unlock_irq(phba->host->host_lock);
				lpfc_nlp_list(phba, nlp, NLP_UNMAPPED_LIST);
			}
		}
	}
	return 0;
}

/*
 * Start / ReStart rescue timer for Discovery / RSCN handling
 */
void
lpfc_set_disctmo(struct lpfc_hba * phba)
{
	uint32_t tmo;

	if (phba->hba_state == LPFC_LOCAL_CFG_LINK) {
		/* For FAN, timeout should be greater then edtov */
		tmo = (((phba->fc_edtov + 999) / 1000) + 1);
	} else {
		/* Normal discovery timeout should be > then ELS/CT timeout
		 * FC spec states we need 3 * ratov for CT requests
		 */
		tmo = ((phba->fc_ratov * 3) + 3);
	}

	mod_timer(&phba->fc_disctmo, jiffies + HZ * tmo);
	spin_lock_irq(phba->host->host_lock);
	phba->fc_flag |= FC_DISC_TMO;
	spin_unlock_irq(phba->host->host_lock);

	/* Start Discovery Timer state <hba_state> */
	lpfc_printf_log(phba, KERN_INFO, LOG_DISCOVERY,
			"%d:0247 Start Discovery Timer state x%x "
			"Data: x%x x%lx x%x x%x\n",
			phba->brd_no,
			phba->hba_state, tmo, (unsigned long)&phba->fc_disctmo,
			phba->fc_plogi_cnt, phba->fc_adisc_cnt);

	return;
}

/*
 * Cancel rescue timer for Discovery / RSCN handling
 */
int
lpfc_can_disctmo(struct lpfc_hba * phba)
{
	/* Turn off discovery timer if its running */
	if (phba->fc_flag & FC_DISC_TMO) {
		spin_lock_irq(phba->host->host_lock);
		phba->fc_flag &= ~FC_DISC_TMO;
		spin_unlock_irq(phba->host->host_lock);
		del_timer_sync(&phba->fc_disctmo);
		phba->work_hba_events &= ~WORKER_DISC_TMO;
	}

	/* Cancel Discovery Timer state <hba_state> */
	lpfc_printf_log(phba, KERN_INFO, LOG_DISCOVERY,
			"%d:0248 Cancel Discovery Timer state x%x "
			"Data: x%x x%x x%x\n",
			phba->brd_no, phba->hba_state, phba->fc_flag,
			phba->fc_plogi_cnt, phba->fc_adisc_cnt);

	return 0;
}

/*
 * Check specified ring for outstanding IOCB on the SLI queue
 * Return true if iocb matches the specified nport
 */
int
lpfc_check_sli_ndlp(struct lpfc_hba * phba,
		    struct lpfc_sli_ring * pring,
		    struct lpfc_iocbq * iocb, struct lpfc_nodelist * ndlp)
{
	struct lpfc_sli *psli;
	IOCB_t *icmd;

	psli = &phba->sli;
	icmd = &iocb->iocb;
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
lpfc_no_rpi(struct lpfc_hba * phba, struct lpfc_nodelist * ndlp)
{
	struct lpfc_sli *psli;
	struct lpfc_sli_ring *pring;
	struct lpfc_iocbq *iocb, *next_iocb;
	IOCB_t *icmd;
	uint32_t rpi, i;

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

			spin_lock_irq(phba->host->host_lock);
			list_for_each_entry_safe(iocb, next_iocb, &pring->txq,
						list) {
				/*
				 * Check to see if iocb matches the nport we are
				 * looking for
				 */
				if ((lpfc_check_sli_ndlp
				     (phba, pring, iocb, ndlp))) {
					/* It matches, so deque and call compl
					   with an error */
					list_del(&iocb->list);
					pring->txq_cnt--;
					if (iocb->iocb_cmpl) {
						icmd = &iocb->iocb;
						icmd->ulpStatus =
						    IOSTAT_LOCAL_REJECT;
						icmd->un.ulpWord[4] =
						    IOERR_SLI_ABORTED;
						spin_unlock_irq(phba->host->
								host_lock);
						(iocb->iocb_cmpl) (phba,
								   iocb, iocb);
						spin_lock_irq(phba->host->
							      host_lock);
					} else
						lpfc_sli_release_iocbq(phba,
								       iocb);
				}
			}
			spin_unlock_irq(phba->host->host_lock);

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
lpfc_unreg_rpi(struct lpfc_hba * phba, struct lpfc_nodelist * ndlp)
{
	LPFC_MBOXQ_t *mbox;
	int rc;

	if (ndlp->nlp_rpi) {
		if ((mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL))) {
			lpfc_unreg_login(phba, ndlp->nlp_rpi, mbox);
			mbox->mbox_cmpl=lpfc_sli_def_mbox_cmpl;
			rc = lpfc_sli_issue_mbox
				    (phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB));
			if (rc == MBX_NOT_FINISHED)
				mempool_free( mbox, phba->mbox_mem_pool);
		}
		lpfc_no_rpi(phba, ndlp);
		ndlp->nlp_rpi = 0;
		return 1;
	}
	return 0;
}

/*
 * Free resources associated with LPFC_NODELIST entry
 * so it can be freed.
 */
static int
lpfc_freenode(struct lpfc_hba * phba, struct lpfc_nodelist * ndlp)
{
	LPFC_MBOXQ_t       *mb;
	LPFC_MBOXQ_t       *nextmb;
	struct lpfc_dmabuf *mp;

	/* Cleanup node for NPort <nlp_DID> */
	lpfc_printf_log(phba, KERN_INFO, LOG_NODE,
			"%d:0900 Cleanup node for NPort x%x "
			"Data: x%x x%x x%x\n",
			phba->brd_no, ndlp->nlp_DID, ndlp->nlp_flag,
			ndlp->nlp_state, ndlp->nlp_rpi);

	lpfc_nlp_list(phba, ndlp, NLP_JUST_DQ);

	/*
	 * if unloading the driver - just leave the remote port in place.
	 * The driver unload will force the attached devices to detach
	 * and flush cache's w/o generating flush errors.
	 */
	if ((ndlp->rport) && !(phba->fc_flag & FC_UNLOADING)) {
		lpfc_unregister_remote_port(phba, ndlp);
		ndlp->nlp_sid = NLP_NO_SID;
	}

	/* cleanup any ndlp on mbox q waiting for reglogin cmpl */
	if ((mb = phba->sli.mbox_active)) {
		if ((mb->mb.mbxCommand == MBX_REG_LOGIN64) &&
		   (ndlp == (struct lpfc_nodelist *) mb->context2)) {
			mb->context2 = NULL;
			mb->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
		}
	}

	spin_lock_irq(phba->host->host_lock);
	list_for_each_entry_safe(mb, nextmb, &phba->sli.mboxq, list) {
		if ((mb->mb.mbxCommand == MBX_REG_LOGIN64) &&
		   (ndlp == (struct lpfc_nodelist *) mb->context2)) {
			mp = (struct lpfc_dmabuf *) (mb->context1);
			if (mp) {
				lpfc_mbuf_free(phba, mp->virt, mp->phys);
				kfree(mp);
			}
			list_del(&mb->list);
			mempool_free(mb, phba->mbox_mem_pool);
		}
	}
	spin_unlock_irq(phba->host->host_lock);

	lpfc_els_abort(phba,ndlp,0);
	spin_lock_irq(phba->host->host_lock);
	ndlp->nlp_flag &= ~NLP_DELAY_TMO;
	spin_unlock_irq(phba->host->host_lock);

	ndlp->nlp_last_elscmd = 0;
	del_timer_sync(&ndlp->nlp_delayfunc);

	if (!list_empty(&ndlp->els_retry_evt.evt_listp))
		list_del_init(&ndlp->els_retry_evt.evt_listp);

	lpfc_unreg_rpi(phba, ndlp);

	return 0;
}

/*
 * Check to see if we can free the nlp back to the freelist.
 * If we are in the middle of using the nlp in the discovery state
 * machine, defer the free till we reach the end of the state machine.
 */
int
lpfc_nlp_remove(struct lpfc_hba * phba, struct lpfc_nodelist * ndlp)
{

	if (ndlp->nlp_flag & NLP_DELAY_TMO) {
		lpfc_cancel_retry_delay_tmo(phba, ndlp);
	}

	if (ndlp->nlp_disc_refcnt) {
		spin_lock_irq(phba->host->host_lock);
		ndlp->nlp_flag |= NLP_DELAY_REMOVE;
		spin_unlock_irq(phba->host->host_lock);
	} else {
		lpfc_freenode(phba, ndlp);
		mempool_free( ndlp, phba->nlp_mem_pool);
	}
	return 0;
}

static int
lpfc_matchdid(struct lpfc_hba * phba, struct lpfc_nodelist * ndlp, uint32_t did)
{
	D_ID mydid;
	D_ID ndlpdid;
	D_ID matchdid;

	if (did == Bcast_DID)
		return 0;

	if (ndlp->nlp_DID == 0) {
		return 0;
	}

	/* First check for Direct match */
	if (ndlp->nlp_DID == did)
		return 1;

	/* Next check for area/domain identically equals 0 match */
	mydid.un.word = phba->fc_myDID;
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

/* Search for a nodelist entry on a specific list */
struct lpfc_nodelist *
lpfc_findnode_did(struct lpfc_hba * phba, uint32_t order, uint32_t did)
{
	struct lpfc_nodelist *ndlp;
	struct list_head *lists[]={&phba->fc_nlpunmap_list,
				   &phba->fc_nlpmap_list,
				   &phba->fc_plogi_list,
				   &phba->fc_adisc_list,
				   &phba->fc_reglogin_list,
				   &phba->fc_prli_list,
				   &phba->fc_npr_list,
				   &phba->fc_unused_list};
	uint32_t search[]={NLP_SEARCH_UNMAPPED,
			   NLP_SEARCH_MAPPED,
			   NLP_SEARCH_PLOGI,
			   NLP_SEARCH_ADISC,
			   NLP_SEARCH_REGLOGIN,
			   NLP_SEARCH_PRLI,
			   NLP_SEARCH_NPR,
			   NLP_SEARCH_UNUSED};
	int i;
	uint32_t data1;

	spin_lock_irq(phba->host->host_lock);
	for (i = 0; i < ARRAY_SIZE(lists); i++ ) {
		if (!(order & search[i]))
			continue;
		list_for_each_entry(ndlp, lists[i], nlp_listp) {
			if (lpfc_matchdid(phba, ndlp, did)) {
				data1 = (((uint32_t) ndlp->nlp_state << 24) |
					 ((uint32_t) ndlp->nlp_xri << 16) |
					 ((uint32_t) ndlp->nlp_type << 8) |
					 ((uint32_t) ndlp->nlp_rpi & 0xff));
				lpfc_printf_log(phba, KERN_INFO, LOG_NODE,
						"%d:0929 FIND node DID "
						" Data: x%p x%x x%x x%x\n",
						phba->brd_no,
						ndlp, ndlp->nlp_DID,
						ndlp->nlp_flag, data1);
				spin_unlock_irq(phba->host->host_lock);
				return ndlp;
			}
		}
	}
	spin_unlock_irq(phba->host->host_lock);

	/* FIND node did <did> NOT FOUND */
	lpfc_printf_log(phba, KERN_INFO, LOG_NODE,
			"%d:0932 FIND node did x%x NOT FOUND Data: x%x\n",
			phba->brd_no, did, order);
	return NULL;
}

struct lpfc_nodelist *
lpfc_setup_disc_node(struct lpfc_hba * phba, uint32_t did)
{
	struct lpfc_nodelist *ndlp;
	uint32_t flg;

	ndlp = lpfc_findnode_did(phba, NLP_SEARCH_ALL, did);
	if (!ndlp) {
		if ((phba->fc_flag & FC_RSCN_MODE) &&
		   ((lpfc_rscn_payload_check(phba, did) == 0)))
			return NULL;
		ndlp = (struct lpfc_nodelist *)
		     mempool_alloc(phba->nlp_mem_pool, GFP_KERNEL);
		if (!ndlp)
			return NULL;
		lpfc_nlp_init(phba, ndlp, did);
		ndlp->nlp_state = NLP_STE_NPR_NODE;
		lpfc_nlp_list(phba, ndlp, NLP_NPR_LIST);
		ndlp->nlp_flag |= NLP_NPR_2B_DISC;
		return ndlp;
	}
	if (phba->fc_flag & FC_RSCN_MODE) {
		if (lpfc_rscn_payload_check(phba, did)) {
			ndlp->nlp_flag |= NLP_NPR_2B_DISC;

			/* Since this node is marked for discovery,
			 * delay timeout is not needed.
			 */
			if (ndlp->nlp_flag & NLP_DELAY_TMO)
				lpfc_cancel_retry_delay_tmo(phba, ndlp);
		} else
			ndlp = NULL;
	} else {
		flg = ndlp->nlp_flag & NLP_LIST_MASK;
		if ((flg == NLP_ADISC_LIST) || (flg == NLP_PLOGI_LIST))
			return NULL;
		ndlp->nlp_state = NLP_STE_NPR_NODE;
		lpfc_nlp_list(phba, ndlp, NLP_NPR_LIST);
		ndlp->nlp_flag |= NLP_NPR_2B_DISC;
	}
	return ndlp;
}

/* Build a list of nodes to discover based on the loopmap */
void
lpfc_disc_list_loopmap(struct lpfc_hba * phba)
{
	int j;
	uint32_t alpa, index;

	if (phba->hba_state <= LPFC_LINK_DOWN) {
		return;
	}
	if (phba->fc_topology != TOPOLOGY_LOOP) {
		return;
	}

	/* Check for loop map present or not */
	if (phba->alpa_map[0]) {
		for (j = 1; j <= phba->alpa_map[0]; j++) {
			alpa = phba->alpa_map[j];

			if (((phba->fc_myDID & 0xff) == alpa) || (alpa == 0)) {
				continue;
			}
			lpfc_setup_disc_node(phba, alpa);
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
			if ((phba->fc_myDID & 0xff) == alpa) {
				continue;
			}

			lpfc_setup_disc_node(phba, alpa);
		}
	}
	return;
}

/* Start Link up / RSCN discovery on NPR list */
void
lpfc_disc_start(struct lpfc_hba * phba)
{
	struct lpfc_sli *psli;
	LPFC_MBOXQ_t *mbox;
	struct lpfc_nodelist *ndlp, *next_ndlp;
	uint32_t did_changed, num_sent;
	uint32_t clear_la_pending;
	int rc;

	psli = &phba->sli;

	if (phba->hba_state <= LPFC_LINK_DOWN) {
		return;
	}
	if (phba->hba_state == LPFC_CLEAR_LA)
		clear_la_pending = 1;
	else
		clear_la_pending = 0;

	if (phba->hba_state < LPFC_HBA_READY) {
		phba->hba_state = LPFC_DISC_AUTH;
	}
	lpfc_set_disctmo(phba);

	if (phba->fc_prevDID == phba->fc_myDID) {
		did_changed = 0;
	} else {
		did_changed = 1;
	}
	phba->fc_prevDID = phba->fc_myDID;
	phba->num_disc_nodes = 0;

	/* Start Discovery state <hba_state> */
	lpfc_printf_log(phba, KERN_INFO, LOG_DISCOVERY,
			"%d:0202 Start Discovery hba state x%x "
			"Data: x%x x%x x%x\n",
			phba->brd_no, phba->hba_state, phba->fc_flag,
			phba->fc_plogi_cnt, phba->fc_adisc_cnt);

	/* If our did changed, we MUST do PLOGI */
	list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_npr_list,
				nlp_listp) {
		if (ndlp->nlp_flag & NLP_NPR_2B_DISC) {
			if (did_changed) {
				spin_lock_irq(phba->host->host_lock);
				ndlp->nlp_flag &= ~NLP_NPR_ADISC;
				spin_unlock_irq(phba->host->host_lock);
			}
		}
	}

	/* First do ADISCs - if any */
	num_sent = lpfc_els_disc_adisc(phba);

	if (num_sent)
		return;

	if ((phba->hba_state < LPFC_HBA_READY) && (!clear_la_pending)) {
		/* If we get here, there is nothing to ADISC */
		if ((mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL))) {
			phba->hba_state = LPFC_CLEAR_LA;
			lpfc_clear_la(phba, mbox);
			mbox->mbox_cmpl = lpfc_mbx_cmpl_clear_la;
			rc = lpfc_sli_issue_mbox(phba, mbox,
						 (MBX_NOWAIT | MBX_STOP_IOCB));
			if (rc == MBX_NOT_FINISHED) {
				mempool_free( mbox, phba->mbox_mem_pool);
				lpfc_disc_flush_list(phba);
				psli->ring[(psli->extra_ring)].flag &=
					~LPFC_STOP_IOCB_EVENT;
				psli->ring[(psli->fcp_ring)].flag &=
					~LPFC_STOP_IOCB_EVENT;
				psli->ring[(psli->next_ring)].flag &=
					~LPFC_STOP_IOCB_EVENT;
				phba->hba_state = LPFC_HBA_READY;
			}
		}
	} else {
		/* Next do PLOGIs - if any */
		num_sent = lpfc_els_disc_plogi(phba);

		if (num_sent)
			return;

		if (phba->fc_flag & FC_RSCN_MODE) {
			/* Check to see if more RSCNs came in while we
			 * were processing this one.
			 */
			if ((phba->fc_rscn_id_cnt == 0) &&
			    (!(phba->fc_flag & FC_RSCN_DISCOVERY))) {
				spin_lock_irq(phba->host->host_lock);
				phba->fc_flag &= ~FC_RSCN_MODE;
				spin_unlock_irq(phba->host->host_lock);
			} else
				lpfc_els_handle_rscn(phba);
		}
	}
	return;
}

/*
 *  Ignore completion for all IOCBs on tx and txcmpl queue for ELS
 *  ring the match the sppecified nodelist.
 */
static void
lpfc_free_tx(struct lpfc_hba * phba, struct lpfc_nodelist * ndlp)
{
	struct lpfc_sli *psli;
	IOCB_t     *icmd;
	struct lpfc_iocbq    *iocb, *next_iocb;
	struct lpfc_sli_ring *pring;
	struct lpfc_dmabuf   *mp;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];

	/* Error matching iocb on txq or txcmplq
	 * First check the txq.
	 */
	list_for_each_entry_safe(iocb, next_iocb, &pring->txq, list) {
		if (iocb->context1 != ndlp) {
			continue;
		}
		icmd = &iocb->iocb;
		if ((icmd->ulpCommand == CMD_ELS_REQUEST64_CR) ||
		    (icmd->ulpCommand == CMD_XMIT_ELS_RSP64_CX)) {

			list_del(&iocb->list);
			pring->txq_cnt--;
			lpfc_els_free_iocb(phba, iocb);
		}
	}

	/* Next check the txcmplq */
	list_for_each_entry_safe(iocb, next_iocb, &pring->txcmplq, list) {
		if (iocb->context1 != ndlp) {
			continue;
		}
		icmd = &iocb->iocb;
		if ((icmd->ulpCommand == CMD_ELS_REQUEST64_CR) ||
		    (icmd->ulpCommand == CMD_XMIT_ELS_RSP64_CX)) {

			iocb->iocb_cmpl = NULL;
			/* context2 = cmd, context2->next = rsp, context3 =
			   bpl */
			if (iocb->context2) {
				/* Free the response IOCB before handling the
				   command. */

				mp = (struct lpfc_dmabuf *) (iocb->context2);
				mp = list_get_first(&mp->list,
						    struct lpfc_dmabuf,
						    list);
				if (mp) {
					/* Delay before releasing rsp buffer to
					 * give UNREG mbox a chance to take
					 * effect.
					 */
					list_add(&mp->list,
						&phba->freebufList);
				}
				lpfc_mbuf_free(phba,
					       ((struct lpfc_dmabuf *)
						iocb->context2)->virt,
					       ((struct lpfc_dmabuf *)
						iocb->context2)->phys);
				kfree(iocb->context2);
			}

			if (iocb->context3) {
				lpfc_mbuf_free(phba,
					       ((struct lpfc_dmabuf *)
						iocb->context3)->virt,
					       ((struct lpfc_dmabuf *)
						iocb->context3)->phys);
				kfree(iocb->context3);
			}
		}
	}

	return;
}

void
lpfc_disc_flush_list(struct lpfc_hba * phba)
{
	struct lpfc_nodelist *ndlp, *next_ndlp;

	if (phba->fc_plogi_cnt) {
		list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_plogi_list,
					nlp_listp) {
			lpfc_free_tx(phba, ndlp);
			lpfc_nlp_remove(phba, ndlp);
		}
	}
	if (phba->fc_adisc_cnt) {
		list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_adisc_list,
					nlp_listp) {
			lpfc_free_tx(phba, ndlp);
			lpfc_nlp_remove(phba, ndlp);
		}
	}
	return;
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
	struct lpfc_hba *phba = (struct lpfc_hba *)ptr;
	unsigned long flags = 0;

	if (unlikely(!phba))
		return;

	spin_lock_irqsave(phba->host->host_lock, flags);
	if (!(phba->work_hba_events & WORKER_DISC_TMO)) {
		phba->work_hba_events |= WORKER_DISC_TMO;
		if (phba->work_wait)
			wake_up(phba->work_wait);
	}
	spin_unlock_irqrestore(phba->host->host_lock, flags);
	return;
}

static void
lpfc_disc_timeout_handler(struct lpfc_hba *phba)
{
	struct lpfc_sli *psli;
	struct lpfc_nodelist *ndlp, *next_ndlp;
	LPFC_MBOXQ_t *clearlambox, *initlinkmbox;
	int rc, clrlaerr = 0;

	if (unlikely(!phba))
		return;

	if (!(phba->fc_flag & FC_DISC_TMO))
		return;

	psli = &phba->sli;

	spin_lock_irq(phba->host->host_lock);
	phba->fc_flag &= ~FC_DISC_TMO;
	spin_unlock_irq(phba->host->host_lock);

	switch (phba->hba_state) {

	case LPFC_LOCAL_CFG_LINK:
	/* hba_state is identically LPFC_LOCAL_CFG_LINK while waiting for FAN */
		/* FAN timeout */
		lpfc_printf_log(phba,
				 KERN_WARNING,
				 LOG_DISCOVERY,
				 "%d:0221 FAN timeout\n",
				 phba->brd_no);

		/* Start discovery by sending FLOGI, clean up old rpis */
		list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_npr_list,
					nlp_listp) {
			if (ndlp->nlp_type & NLP_FABRIC) {
				/* Clean up the ndlp on Fabric connections */
				lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
			} else if (!(ndlp->nlp_flag & NLP_NPR_ADISC)) {
				/* Fail outstanding IO now since device
				 * is marked for PLOGI.
				 */
				lpfc_unreg_rpi(phba, ndlp);
			}
		}
		phba->hba_state = LPFC_FLOGI;
		lpfc_set_disctmo(phba);
		lpfc_initial_flogi(phba);
		break;

	case LPFC_FLOGI:
	/* hba_state is identically LPFC_FLOGI while waiting for FLOGI cmpl */
		/* Initial FLOGI timeout */
		lpfc_printf_log(phba,
				 KERN_ERR,
				 LOG_DISCOVERY,
				 "%d:0222 Initial FLOGI timeout\n",
				 phba->brd_no);

		/* Assume no Fabric and go on with discovery.
		 * Check for outstanding ELS FLOGI to abort.
		 */

		/* FLOGI failed, so just use loop map to make discovery list */
		lpfc_disc_list_loopmap(phba);

		/* Start discovery */
		lpfc_disc_start(phba);
		break;

	case LPFC_FABRIC_CFG_LINK:
	/* hba_state is identically LPFC_FABRIC_CFG_LINK while waiting for
	   NameServer login */
		lpfc_printf_log(phba, KERN_ERR, LOG_DISCOVERY,
				"%d:0223 Timeout while waiting for NameServer "
				"login\n", phba->brd_no);

		/* Next look for NameServer ndlp */
		ndlp = lpfc_findnode_did(phba, NLP_SEARCH_ALL, NameServer_DID);
		if (ndlp)
			lpfc_nlp_remove(phba, ndlp);
		/* Start discovery */
		lpfc_disc_start(phba);
		break;

	case LPFC_NS_QRY:
	/* Check for wait for NameServer Rsp timeout */
		lpfc_printf_log(phba, KERN_ERR, LOG_DISCOVERY,
				"%d:0224 NameServer Query timeout "
				"Data: x%x x%x\n",
				phba->brd_no,
				phba->fc_ns_retry, LPFC_MAX_NS_RETRY);

		ndlp = lpfc_findnode_did(phba, NLP_SEARCH_UNMAPPED,
								NameServer_DID);
		if (ndlp) {
			if (phba->fc_ns_retry < LPFC_MAX_NS_RETRY) {
				/* Try it one more time */
				rc = lpfc_ns_cmd(phba, ndlp, SLI_CTNS_GID_FT);
				if (rc == 0)
					break;
			}
			phba->fc_ns_retry = 0;
		}

		/* Nothing to authenticate, so CLEAR_LA right now */
		clearlambox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
		if (!clearlambox) {
			clrlaerr = 1;
			lpfc_printf_log(phba, KERN_ERR, LOG_DISCOVERY,
					"%d:0226 Device Discovery "
					"completion error\n",
					phba->brd_no);
			phba->hba_state = LPFC_HBA_ERROR;
			break;
		}

		phba->hba_state = LPFC_CLEAR_LA;
		lpfc_clear_la(phba, clearlambox);
		clearlambox->mbox_cmpl = lpfc_mbx_cmpl_clear_la;
		rc = lpfc_sli_issue_mbox(phba, clearlambox,
					 (MBX_NOWAIT | MBX_STOP_IOCB));
		if (rc == MBX_NOT_FINISHED) {
			mempool_free(clearlambox, phba->mbox_mem_pool);
			clrlaerr = 1;
			break;
		}

		/* Setup and issue mailbox INITIALIZE LINK command */
		initlinkmbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
		if (!initlinkmbox) {
			lpfc_printf_log(phba, KERN_ERR, LOG_DISCOVERY,
					"%d:0206 Device Discovery "
					"completion error\n",
					phba->brd_no);
			phba->hba_state = LPFC_HBA_ERROR;
			break;
		}

		lpfc_linkdown(phba);
		lpfc_init_link(phba, initlinkmbox, phba->cfg_topology,
			       phba->cfg_link_speed);
		initlinkmbox->mb.un.varInitLnk.lipsr_AL_PA = 0;
		rc = lpfc_sli_issue_mbox(phba, initlinkmbox,
					 (MBX_NOWAIT | MBX_STOP_IOCB));
		if (rc == MBX_NOT_FINISHED)
			mempool_free(initlinkmbox, phba->mbox_mem_pool);

		break;

	case LPFC_DISC_AUTH:
	/* Node Authentication timeout */
		lpfc_printf_log(phba,
				 KERN_ERR,
				 LOG_DISCOVERY,
				 "%d:0227 Node Authentication timeout\n",
				 phba->brd_no);
		lpfc_disc_flush_list(phba);
		clearlambox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
		if (!clearlambox) {
			clrlaerr = 1;
			lpfc_printf_log(phba, KERN_ERR, LOG_DISCOVERY,
					"%d:0207 Device Discovery "
					"completion error\n",
					phba->brd_no);
			phba->hba_state = LPFC_HBA_ERROR;
			break;
		}
		phba->hba_state = LPFC_CLEAR_LA;
		lpfc_clear_la(phba, clearlambox);
		clearlambox->mbox_cmpl = lpfc_mbx_cmpl_clear_la;
		rc = lpfc_sli_issue_mbox(phba, clearlambox,
					 (MBX_NOWAIT | MBX_STOP_IOCB));
		if (rc == MBX_NOT_FINISHED) {
			mempool_free(clearlambox, phba->mbox_mem_pool);
			clrlaerr = 1;
		}
		break;

	case LPFC_CLEAR_LA:
	/* CLEAR LA timeout */
		lpfc_printf_log(phba,
				 KERN_ERR,
				 LOG_DISCOVERY,
				 "%d:0228 CLEAR LA timeout\n",
				 phba->brd_no);
		clrlaerr = 1;
		break;

	case LPFC_HBA_READY:
		if (phba->fc_flag & FC_RSCN_MODE) {
			lpfc_printf_log(phba,
					KERN_ERR,
					LOG_DISCOVERY,
					"%d:0231 RSCN timeout Data: x%x x%x\n",
					phba->brd_no,
					phba->fc_ns_retry, LPFC_MAX_NS_RETRY);

			/* Cleanup any outstanding ELS commands */
			lpfc_els_flush_cmd(phba);

			lpfc_els_flush_rscn(phba);
			lpfc_disc_flush_list(phba);
		}
		break;
	}

	if (clrlaerr) {
		lpfc_disc_flush_list(phba);
		psli->ring[(psli->extra_ring)].flag &= ~LPFC_STOP_IOCB_EVENT;
		psli->ring[(psli->fcp_ring)].flag &= ~LPFC_STOP_IOCB_EVENT;
		psli->ring[(psli->next_ring)].flag &= ~LPFC_STOP_IOCB_EVENT;
		phba->hba_state = LPFC_HBA_READY;
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
lpfc_mbx_cmpl_fdmi_reg_login(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb)
{
	struct lpfc_sli *psli;
	MAILBOX_t *mb;
	struct lpfc_dmabuf *mp;
	struct lpfc_nodelist *ndlp;

	psli = &phba->sli;
	mb = &pmb->mb;

	ndlp = (struct lpfc_nodelist *) pmb->context2;
	mp = (struct lpfc_dmabuf *) (pmb->context1);

	pmb->context1 = NULL;

	ndlp->nlp_rpi = mb->un.varWords[0];
	ndlp->nlp_type |= NLP_FABRIC;
	ndlp->nlp_state = NLP_STE_UNMAPPED_NODE;
	lpfc_nlp_list(phba, ndlp, NLP_UNMAPPED_LIST);

	/* Start issuing Fabric-Device Management Interface (FDMI)
	 * command to 0xfffffa (FDMI well known port)
	 */
	if (phba->cfg_fdmi_on == 1) {
		lpfc_fdmi_cmd(phba, ndlp, SLI_MGMT_DHBA);
	} else {
		/*
		 * Delay issuing FDMI command if fdmi-on=2
		 * (supporting RPA/hostnmae)
		 */
		mod_timer(&phba->fc_fdmitmo, jiffies + HZ * 60);
	}

	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
	mempool_free( pmb, phba->mbox_mem_pool);

	return;
}

/*
 * This routine looks up the ndlp  lists
 * for the given RPI. If rpi found
 * it return the node list pointer
 * else return NULL.
 */
struct lpfc_nodelist *
lpfc_findnode_rpi(struct lpfc_hba * phba, uint16_t rpi)
{
	struct lpfc_nodelist *ndlp;
	struct list_head * lists[]={&phba->fc_nlpunmap_list,
				    &phba->fc_nlpmap_list,
				    &phba->fc_plogi_list,
				    &phba->fc_adisc_list,
				    &phba->fc_reglogin_list};
	int i;

	spin_lock_irq(phba->host->host_lock);
	for (i = 0; i < ARRAY_SIZE(lists); i++ )
		list_for_each_entry(ndlp, lists[i], nlp_listp)
			if (ndlp->nlp_rpi == rpi) {
				spin_unlock_irq(phba->host->host_lock);
				return ndlp;
			}
	spin_unlock_irq(phba->host->host_lock);
	return NULL;
}

/*
 * This routine looks up the ndlp  lists
 * for the given WWPN. If WWPN found
 * it return the node list pointer
 * else return NULL.
 */
struct lpfc_nodelist *
lpfc_findnode_wwpn(struct lpfc_hba * phba, uint32_t order,
		   struct lpfc_name * wwpn)
{
	struct lpfc_nodelist *ndlp;
	struct list_head * lists[]={&phba->fc_nlpunmap_list,
				    &phba->fc_nlpmap_list,
				    &phba->fc_npr_list,
				    &phba->fc_plogi_list,
				    &phba->fc_adisc_list,
				    &phba->fc_reglogin_list,
				    &phba->fc_prli_list};
	uint32_t search[]={NLP_SEARCH_UNMAPPED,
			   NLP_SEARCH_MAPPED,
			   NLP_SEARCH_NPR,
			   NLP_SEARCH_PLOGI,
			   NLP_SEARCH_ADISC,
			   NLP_SEARCH_REGLOGIN,
			   NLP_SEARCH_PRLI};
	int i;

	spin_lock_irq(phba->host->host_lock);
	for (i = 0; i < ARRAY_SIZE(lists); i++ ) {
		if (!(order & search[i]))
			continue;
		list_for_each_entry(ndlp, lists[i], nlp_listp) {
			if (memcmp(&ndlp->nlp_portname, wwpn,
				   sizeof(struct lpfc_name)) == 0) {
				spin_unlock_irq(phba->host->host_lock);
				return ndlp;
			}
		}
	}
	spin_unlock_irq(phba->host->host_lock);
	return NULL;
}

void
lpfc_nlp_init(struct lpfc_hba * phba, struct lpfc_nodelist * ndlp,
		 uint32_t did)
{
	memset(ndlp, 0, sizeof (struct lpfc_nodelist));
	INIT_LIST_HEAD(&ndlp->els_retry_evt.evt_listp);
	init_timer(&ndlp->nlp_delayfunc);
	ndlp->nlp_delayfunc.function = lpfc_els_retry_delay;
	ndlp->nlp_delayfunc.data = (unsigned long)ndlp;
	ndlp->nlp_DID = did;
	ndlp->nlp_phba = phba;
	ndlp->nlp_sid = NLP_NO_SID;
	return;
}
