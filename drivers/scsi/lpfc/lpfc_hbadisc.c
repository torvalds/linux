/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2017 Broadcom. All Rights Reserved. The term      *
 * “Broadcom” refers to Broadcom Limited and/or its subsidiaries.  *
 * Copyright (C) 2004-2016 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.broadcom.com                                                *
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
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/kthread.h>
#include <linux/interrupt.h>
#include <linux/lockdep.h>

#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport_fc.h>
#include <scsi/fc/fc_fs.h>

#include <linux/nvme-fc-driver.h>

#include "lpfc_hw4.h"
#include "lpfc_hw.h"
#include "lpfc_nl.h"
#include "lpfc_disc.h"
#include "lpfc_sli.h"
#include "lpfc_sli4.h"
#include "lpfc.h"
#include "lpfc_scsi.h"
#include "lpfc_nvme.h"
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
static void lpfc_disc_flush_list(struct lpfc_vport *vport);
static void lpfc_unregister_fcfi_cmpl(struct lpfc_hba *, LPFC_MBOXQ_t *);
static int lpfc_fcf_inuse(struct lpfc_hba *);

void
lpfc_terminate_rport_io(struct fc_rport *rport)
{
	struct lpfc_rport_data *rdata;
	struct lpfc_nodelist * ndlp;
	struct lpfc_hba *phba;

	rdata = rport->dd_data;
	ndlp = rdata->pnode;

	if (!ndlp || !NLP_CHK_NODE_ACT(ndlp)) {
		if (rport->roles & FC_RPORT_ROLE_FCP_TARGET)
			printk(KERN_ERR "Cannot find remote node"
			" to terminate I/O Data x%x\n",
			rport->port_id);
		return;
	}

	phba  = ndlp->phba;

	lpfc_debugfs_disc_trc(ndlp->vport, LPFC_DISC_TRC_RPORT,
		"rport terminate: sid:x%x did:x%x flg:x%x",
		ndlp->nlp_sid, ndlp->nlp_DID, ndlp->nlp_flag);

	if (ndlp->nlp_sid != NLP_NO_SID) {
		lpfc_sli_abort_iocb(ndlp->vport,
			&phba->sli.sli3_ring[LPFC_FCP_RING],
			ndlp->nlp_sid, 0, LPFC_CTX_TGT);
	}
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
	struct Scsi_Host *shost;
	struct lpfc_hba   *phba;
	struct lpfc_work_evt *evtp;
	int  put_node;
	int  put_rport;

	rdata = rport->dd_data;
	ndlp = rdata->pnode;
	if (!ndlp || !NLP_CHK_NODE_ACT(ndlp))
		return;

	vport = ndlp->vport;
	phba  = vport->phba;

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_RPORT,
		"rport devlosscb: sid:x%x did:x%x flg:x%x",
		ndlp->nlp_sid, ndlp->nlp_DID, ndlp->nlp_flag);

	lpfc_printf_vlog(ndlp->vport, KERN_INFO, LOG_NODE,
			 "3181 dev_loss_callbk x%06x, rport %p flg x%x\n",
			 ndlp->nlp_DID, ndlp->rport, ndlp->nlp_flag);

	/* Don't defer this if we are in the process of deleting the vport
	 * or unloading the driver. The unload will cleanup the node
	 * appropriately we just need to cleanup the ndlp rport info here.
	 */
	if (vport->load_flag & FC_UNLOADING) {
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

	if (ndlp->nlp_state == NLP_STE_MAPPED_NODE)
		return;

	if (rport->port_name != wwn_to_u64(ndlp->nlp_portname.u.wwn))
		lpfc_printf_vlog(vport, KERN_ERR, LOG_NODE,
				"6789 rport name %llx != node port name %llx",
				rport->port_name,
				wwn_to_u64(ndlp->nlp_portname.u.wwn));

	evtp = &ndlp->dev_loss_evt;

	if (!list_empty(&evtp->evt_listp)) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_NODE,
				"6790 rport name %llx dev_loss_evt pending",
				rport->port_name);
		return;
	}

	shost = lpfc_shost_from_vport(vport);
	spin_lock_irq(shost->host_lock);
	ndlp->nlp_flag |= NLP_IN_DEV_LOSS;
	spin_unlock_irq(shost->host_lock);

	/* We need to hold the node by incrementing the reference
	 * count until this queued work is done
	 */
	evtp->evt_arg1  = lpfc_nlp_get(ndlp);

	spin_lock_irq(&phba->hbalock);
	if (evtp->evt_arg1) {
		evtp->evt = LPFC_EVT_DEV_LOSS;
		list_add_tail(&evtp->evt_listp, &phba->work_list);
		lpfc_worker_wake_up(phba);
	}
	spin_unlock_irq(&phba->hbalock);

	return;
}

/**
 * lpfc_dev_loss_tmo_handler - Remote node devloss timeout handler
 * @ndlp: Pointer to remote node object.
 *
 * This function is called from the worker thread when devloss timeout timer
 * expires. For SLI4 host, this routine shall return 1 when at lease one
 * remote node, including this @ndlp, is still in use of FCF; otherwise, this
 * routine shall return 0 when there is no remote node is still in use of FCF
 * when devloss timeout happened to this @ndlp.
 **/
static int
lpfc_dev_loss_tmo_handler(struct lpfc_nodelist *ndlp)
{
	struct lpfc_rport_data *rdata;
	struct fc_rport   *rport;
	struct lpfc_vport *vport;
	struct lpfc_hba   *phba;
	struct Scsi_Host  *shost;
	uint8_t *name;
	int  put_node;
	int warn_on = 0;
	int fcf_inuse = 0;

	rport = ndlp->rport;
	vport = ndlp->vport;
	shost = lpfc_shost_from_vport(vport);

	spin_lock_irq(shost->host_lock);
	ndlp->nlp_flag &= ~NLP_IN_DEV_LOSS;
	spin_unlock_irq(shost->host_lock);

	if (!rport)
		return fcf_inuse;

	name = (uint8_t *) &ndlp->nlp_portname;
	phba  = vport->phba;

	if (phba->sli_rev == LPFC_SLI_REV4)
		fcf_inuse = lpfc_fcf_inuse(phba);

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_RPORT,
		"rport devlosstmo:did:x%x type:x%x id:x%x",
		ndlp->nlp_DID, ndlp->nlp_type, rport->scsi_target_id);

	lpfc_printf_vlog(ndlp->vport, KERN_INFO, LOG_NODE,
			 "3182 dev_loss_tmo_handler x%06x, rport %p flg x%x\n",
			 ndlp->nlp_DID, ndlp->rport, ndlp->nlp_flag);

	/*
	 * lpfc_nlp_remove if reached with dangling rport drops the
	 * reference. To make sure that does not happen clear rport
	 * pointer in ndlp before lpfc_nlp_put.
	 */
	rdata = rport->dd_data;

	/* Don't defer this if we are in the process of deleting the vport
	 * or unloading the driver. The unload will cleanup the node
	 * appropriately we just need to cleanup the ndlp rport info here.
	 */
	if (vport->load_flag & FC_UNLOADING) {
		if (ndlp->nlp_sid != NLP_NO_SID) {
			/* flush the target */
			lpfc_sli_abort_iocb(vport,
					    &phba->sli.sli3_ring[LPFC_FCP_RING],
					    ndlp->nlp_sid, 0, LPFC_CTX_TGT);
		}
		put_node = rdata->pnode != NULL;
		rdata->pnode = NULL;
		ndlp->rport = NULL;
		if (put_node)
			lpfc_nlp_put(ndlp);
		put_device(&rport->dev);

		return fcf_inuse;
	}

	if (ndlp->nlp_state == NLP_STE_MAPPED_NODE) {
		lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
				 "0284 Devloss timeout Ignored on "
				 "WWPN %x:%x:%x:%x:%x:%x:%x:%x "
				 "NPort x%x\n",
				 *name, *(name+1), *(name+2), *(name+3),
				 *(name+4), *(name+5), *(name+6), *(name+7),
				 ndlp->nlp_DID);
		return fcf_inuse;
	}

	put_node = rdata->pnode != NULL;
	rdata->pnode = NULL;
	ndlp->rport = NULL;
	if (put_node)
		lpfc_nlp_put(ndlp);
	put_device(&rport->dev);

	if (ndlp->nlp_type & NLP_FABRIC)
		return fcf_inuse;

	if (ndlp->nlp_sid != NLP_NO_SID) {
		warn_on = 1;
		lpfc_sli_abort_iocb(vport, &phba->sli.sli3_ring[LPFC_FCP_RING],
				    ndlp->nlp_sid, 0, LPFC_CTX_TGT);
	}

	if (warn_on) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_DISCOVERY,
				 "0203 Devloss timeout on "
				 "WWPN %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x "
				 "NPort x%06x Data: x%x x%x x%x\n",
				 *name, *(name+1), *(name+2), *(name+3),
				 *(name+4), *(name+5), *(name+6), *(name+7),
				 ndlp->nlp_DID, ndlp->nlp_flag,
				 ndlp->nlp_state, ndlp->nlp_rpi);
	} else {
		lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
				 "0204 Devloss timeout on "
				 "WWPN %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x "
				 "NPort x%06x Data: x%x x%x x%x\n",
				 *name, *(name+1), *(name+2), *(name+3),
				 *(name+4), *(name+5), *(name+6), *(name+7),
				 ndlp->nlp_DID, ndlp->nlp_flag,
				 ndlp->nlp_state, ndlp->nlp_rpi);
	}

	if (!(ndlp->nlp_flag & NLP_DELAY_TMO) &&
	    !(ndlp->nlp_flag & NLP_NPR_2B_DISC) &&
	    (ndlp->nlp_state != NLP_STE_UNMAPPED_NODE) &&
	    (ndlp->nlp_state != NLP_STE_REG_LOGIN_ISSUE) &&
	    (ndlp->nlp_state != NLP_STE_PRLI_ISSUE))
		lpfc_disc_state_machine(vport, ndlp, NULL, NLP_EVT_DEVICE_RM);

	return fcf_inuse;
}

/**
 * lpfc_sli4_post_dev_loss_tmo_handler - SLI4 post devloss timeout handler
 * @phba: Pointer to hba context object.
 * @fcf_inuse: SLI4 FCF in-use state reported from devloss timeout handler.
 * @nlp_did: remote node identifer with devloss timeout.
 *
 * This function is called from the worker thread after invoking devloss
 * timeout handler and releasing the reference count for the ndlp with
 * which the devloss timeout was handled for SLI4 host. For the devloss
 * timeout of the last remote node which had been in use of FCF, when this
 * routine is invoked, it shall be guaranteed that none of the remote are
 * in-use of FCF. When devloss timeout to the last remote using the FCF,
 * if the FIP engine is neither in FCF table scan process nor roundrobin
 * failover process, the in-use FCF shall be unregistered. If the FIP
 * engine is in FCF discovery process, the devloss timeout state shall
 * be set for either the FCF table scan process or roundrobin failover
 * process to unregister the in-use FCF.
 **/
static void
lpfc_sli4_post_dev_loss_tmo_handler(struct lpfc_hba *phba, int fcf_inuse,
				    uint32_t nlp_did)
{
	/* If devloss timeout happened to a remote node when FCF had no
	 * longer been in-use, do nothing.
	 */
	if (!fcf_inuse)
		return;

	if ((phba->hba_flag & HBA_FIP_SUPPORT) && !lpfc_fcf_inuse(phba)) {
		spin_lock_irq(&phba->hbalock);
		if (phba->fcf.fcf_flag & FCF_DISCOVERY) {
			if (phba->hba_flag & HBA_DEVLOSS_TMO) {
				spin_unlock_irq(&phba->hbalock);
				return;
			}
			phba->hba_flag |= HBA_DEVLOSS_TMO;
			lpfc_printf_log(phba, KERN_INFO, LOG_FIP,
					"2847 Last remote node (x%x) using "
					"FCF devloss tmo\n", nlp_did);
		}
		if (phba->fcf.fcf_flag & FCF_REDISC_PROG) {
			spin_unlock_irq(&phba->hbalock);
			lpfc_printf_log(phba, KERN_INFO, LOG_FIP,
					"2868 Devloss tmo to FCF rediscovery "
					"in progress\n");
			return;
		}
		if (!(phba->hba_flag & (FCF_TS_INPROG | FCF_RR_INPROG))) {
			spin_unlock_irq(&phba->hbalock);
			lpfc_printf_log(phba, KERN_INFO, LOG_FIP,
					"2869 Devloss tmo to idle FIP engine, "
					"unreg in-use FCF and rescan.\n");
			/* Unregister in-use FCF and rescan */
			lpfc_unregister_fcf_rescan(phba);
			return;
		}
		spin_unlock_irq(&phba->hbalock);
		if (phba->hba_flag & FCF_TS_INPROG)
			lpfc_printf_log(phba, KERN_INFO, LOG_FIP,
					"2870 FCF table scan in progress\n");
		if (phba->hba_flag & FCF_RR_INPROG)
			lpfc_printf_log(phba, KERN_INFO, LOG_FIP,
					"2871 FLOGI roundrobin FCF failover "
					"in progress\n");
	}
	lpfc_unregister_unused_fcf(phba);
}

/**
 * lpfc_alloc_fast_evt - Allocates data structure for posting event
 * @phba: Pointer to hba context object.
 *
 * This function is called from the functions which need to post
 * events from interrupt context. This function allocates data
 * structure required for posting event. It also keeps track of
 * number of events pending and prevent event storm when there are
 * too many events.
 **/
struct lpfc_fast_path_event *
lpfc_alloc_fast_evt(struct lpfc_hba *phba) {
	struct lpfc_fast_path_event *ret;

	/* If there are lot of fast event do not exhaust memory due to this */
	if (atomic_read(&phba->fast_event_count) > LPFC_MAX_EVT_COUNT)
		return NULL;

	ret = kzalloc(sizeof(struct lpfc_fast_path_event),
			GFP_ATOMIC);
	if (ret) {
		atomic_inc(&phba->fast_event_count);
		INIT_LIST_HEAD(&ret->work_evt.evt_listp);
		ret->work_evt.evt = LPFC_EVT_FASTPATH_MGMT_EVT;
	}
	return ret;
}

/**
 * lpfc_free_fast_evt - Frees event data structure
 * @phba: Pointer to hba context object.
 * @evt:  Event object which need to be freed.
 *
 * This function frees the data structure required for posting
 * events.
 **/
void
lpfc_free_fast_evt(struct lpfc_hba *phba,
		struct lpfc_fast_path_event *evt) {

	atomic_dec(&phba->fast_event_count);
	kfree(evt);
}

/**
 * lpfc_send_fastpath_evt - Posts events generated from fast path
 * @phba: Pointer to hba context object.
 * @evtp: Event data structure.
 *
 * This function is called from worker thread, when the interrupt
 * context need to post an event. This function posts the event
 * to fc transport netlink interface.
 **/
static void
lpfc_send_fastpath_evt(struct lpfc_hba *phba,
		struct lpfc_work_evt *evtp)
{
	unsigned long evt_category, evt_sub_category;
	struct lpfc_fast_path_event *fast_evt_data;
	char *evt_data;
	uint32_t evt_data_size;
	struct Scsi_Host *shost;

	fast_evt_data = container_of(evtp, struct lpfc_fast_path_event,
		work_evt);

	evt_category = (unsigned long) fast_evt_data->un.fabric_evt.event_type;
	evt_sub_category = (unsigned long) fast_evt_data->un.
			fabric_evt.subcategory;
	shost = lpfc_shost_from_vport(fast_evt_data->vport);
	if (evt_category == FC_REG_FABRIC_EVENT) {
		if (evt_sub_category == LPFC_EVENT_FCPRDCHKERR) {
			evt_data = (char *) &fast_evt_data->un.read_check_error;
			evt_data_size = sizeof(fast_evt_data->un.
				read_check_error);
		} else if ((evt_sub_category == LPFC_EVENT_FABRIC_BUSY) ||
			(evt_sub_category == LPFC_EVENT_PORT_BUSY)) {
			evt_data = (char *) &fast_evt_data->un.fabric_evt;
			evt_data_size = sizeof(fast_evt_data->un.fabric_evt);
		} else {
			lpfc_free_fast_evt(phba, fast_evt_data);
			return;
		}
	} else if (evt_category == FC_REG_SCSI_EVENT) {
		switch (evt_sub_category) {
		case LPFC_EVENT_QFULL:
		case LPFC_EVENT_DEVBSY:
			evt_data = (char *) &fast_evt_data->un.scsi_evt;
			evt_data_size = sizeof(fast_evt_data->un.scsi_evt);
			break;
		case LPFC_EVENT_CHECK_COND:
			evt_data = (char *) &fast_evt_data->un.check_cond_evt;
			evt_data_size =  sizeof(fast_evt_data->un.
				check_cond_evt);
			break;
		case LPFC_EVENT_VARQUEDEPTH:
			evt_data = (char *) &fast_evt_data->un.queue_depth_evt;
			evt_data_size = sizeof(fast_evt_data->un.
				queue_depth_evt);
			break;
		default:
			lpfc_free_fast_evt(phba, fast_evt_data);
			return;
		}
	} else {
		lpfc_free_fast_evt(phba, fast_evt_data);
		return;
	}

	if (phba->cfg_enable_fc4_type != LPFC_ENABLE_NVME)
		fc_host_post_vendor_event(shost,
			fc_get_event_number(),
			evt_data_size,
			evt_data,
			LPFC_NL_VENDOR_ID);

	lpfc_free_fast_evt(phba, fast_evt_data);
	return;
}

static void
lpfc_work_list_done(struct lpfc_hba *phba)
{
	struct lpfc_work_evt  *evtp = NULL;
	struct lpfc_nodelist  *ndlp;
	int free_evt;
	int fcf_inuse;
	uint32_t nlp_did;

	spin_lock_irq(&phba->hbalock);
	while (!list_empty(&phba->work_list)) {
		list_remove_head((&phba->work_list), evtp, typeof(*evtp),
				 evt_listp);
		spin_unlock_irq(&phba->hbalock);
		free_evt = 1;
		switch (evtp->evt) {
		case LPFC_EVT_ELS_RETRY:
			ndlp = (struct lpfc_nodelist *) (evtp->evt_arg1);
			lpfc_els_retry_delay_handler(ndlp);
			free_evt = 0; /* evt is part of ndlp */
			/* decrement the node reference count held
			 * for this queued work
			 */
			lpfc_nlp_put(ndlp);
			break;
		case LPFC_EVT_DEV_LOSS:
			ndlp = (struct lpfc_nodelist *)(evtp->evt_arg1);
			fcf_inuse = lpfc_dev_loss_tmo_handler(ndlp);
			free_evt = 0;
			/* decrement the node reference count held for
			 * this queued work
			 */
			nlp_did = ndlp->nlp_DID;
			lpfc_nlp_put(ndlp);
			if (phba->sli_rev == LPFC_SLI_REV4)
				lpfc_sli4_post_dev_loss_tmo_handler(phba,
								    fcf_inuse,
								    nlp_did);
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
				lpfc_offline_prep(phba, LPFC_MBX_WAIT);
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
		case LPFC_EVT_FASTPATH_MGMT_EVT:
			lpfc_send_fastpath_evt(phba, evtp);
			free_evt = 0;
			break;
		case LPFC_EVT_RESET_HBA:
			if (!(phba->pport->load_flag & FC_UNLOADING))
				lpfc_reset_hba(phba);
			break;
		}
		if (free_evt)
			kfree(evtp);
		spin_lock_irq(&phba->hbalock);
	}
	spin_unlock_irq(&phba->hbalock);

}

static void
lpfc_work_done(struct lpfc_hba *phba)
{
	struct lpfc_sli_ring *pring;
	uint32_t ha_copy, status, control, work_port_events;
	struct lpfc_vport **vports;
	struct lpfc_vport *vport;
	int i;

	spin_lock_irq(&phba->hbalock);
	ha_copy = phba->work_ha;
	phba->work_ha = 0;
	spin_unlock_irq(&phba->hbalock);

	/* First, try to post the next mailbox command to SLI4 device */
	if (phba->pci_dev_grp == LPFC_PCI_DEV_OC)
		lpfc_sli4_post_async_mbox(phba);

	if (ha_copy & HA_ERATT)
		/* Handle the error attention event */
		lpfc_handle_eratt(phba);

	if (ha_copy & HA_MBATT)
		lpfc_sli_handle_mb_event(phba);

	if (ha_copy & HA_LATT)
		lpfc_handle_latt(phba);

	/* Process SLI4 events */
	if (phba->pci_dev_grp == LPFC_PCI_DEV_OC) {
		if (phba->hba_flag & HBA_RRQ_ACTIVE)
			lpfc_handle_rrq_active(phba);
		if (phba->hba_flag & FCP_XRI_ABORT_EVENT)
			lpfc_sli4_fcp_xri_abort_event_proc(phba);
		if (phba->hba_flag & NVME_XRI_ABORT_EVENT)
			lpfc_sli4_nvme_xri_abort_event_proc(phba);
		if (phba->hba_flag & ELS_XRI_ABORT_EVENT)
			lpfc_sli4_els_xri_abort_event_proc(phba);
		if (phba->hba_flag & ASYNC_EVENT)
			lpfc_sli4_async_event_proc(phba);
		if (phba->hba_flag & HBA_POST_RECEIVE_BUFFER) {
			spin_lock_irq(&phba->hbalock);
			phba->hba_flag &= ~HBA_POST_RECEIVE_BUFFER;
			spin_unlock_irq(&phba->hbalock);
			lpfc_sli_hbqbuf_add_hbqs(phba, LPFC_ELS_HBQ);
		}
		if (phba->fcf.fcf_flag & FCF_REDISC_EVT)
			lpfc_sli4_fcf_redisc_event_proc(phba);
	}

	vports = lpfc_create_vport_work_array(phba);
	if (vports != NULL)
		for (i = 0; i <= phba->max_vports; i++) {
			/*
			 * We could have no vports in array if unloading, so if
			 * this happens then just use the pport
			 */
			if (vports[i] == NULL && i == 0)
				vport = phba->pport;
			else
				vport = vports[i];
			if (vport == NULL)
				break;
			spin_lock_irq(&vport->work_port_lock);
			work_port_events = vport->work_port_events;
			vport->work_port_events &= ~work_port_events;
			spin_unlock_irq(&vport->work_port_lock);
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
			if (work_port_events & WORKER_RAMP_DOWN_QUEUE)
				lpfc_ramp_down_queue_handler(phba);
			if (work_port_events & WORKER_DELAYED_DISC_TMO)
				lpfc_delayed_disc_timeout_handler(vport);
		}
	lpfc_destroy_vport_work_array(phba, vports);

	pring = lpfc_phba_elsring(phba);
	status = (ha_copy & (HA_RXMASK  << (4*LPFC_ELS_RING)));
	status >>= (4*LPFC_ELS_RING);
	if (pring && (status & HA_RXMASK ||
		      pring->flag & LPFC_DEFERRED_RING_EVENT ||
		      phba->hba_flag & HBA_SP_QUEUE_EVT)) {
		if (pring->flag & LPFC_STOP_IOCB_EVENT) {
			pring->flag |= LPFC_DEFERRED_RING_EVENT;
			/* Set the lpfc data pending flag */
			set_bit(LPFC_DATA_READY, &phba->data_flags);
		} else {
			if (phba->link_state >= LPFC_LINK_UP ||
			    phba->link_flag & LS_MDS_LOOPBACK) {
				pring->flag &= ~LPFC_DEFERRED_RING_EVENT;
				lpfc_sli_handle_slow_ring_event(phba, pring,
								(status &
								HA_RXMASK));
			}
		}
		if ((phba->sli_rev == LPFC_SLI_REV4) &&
				 (!list_empty(&pring->txq)))
			lpfc_drain_txq(phba);
		/*
		 * Turn on Ring interrupts
		 */
		if (phba->sli_rev <= LPFC_SLI_REV3) {
			spin_lock_irq(&phba->hbalock);
			control = readl(phba->HCregaddr);
			if (!(control & (HC_R0INT_ENA << LPFC_ELS_RING))) {
				lpfc_debugfs_slow_ring_trc(phba,
					"WRK Enable ring: cntl:x%x hacopy:x%x",
					control, ha_copy, 0);

				control |= (HC_R0INT_ENA << LPFC_ELS_RING);
				writel(control, phba->HCregaddr);
				readl(phba->HCregaddr); /* flush */
			} else {
				lpfc_debugfs_slow_ring_trc(phba,
					"WRK Ring ok:     cntl:x%x hacopy:x%x",
					control, ha_copy, 0);
			}
			spin_unlock_irq(&phba->hbalock);
		}
	}
	lpfc_work_list_done(phba);
}

int
lpfc_do_work(void *p)
{
	struct lpfc_hba *phba = p;
	int rc;

	set_user_nice(current, MIN_NICE);
	current->flags |= PF_NOFREEZE;
	phba->data_flags = 0;

	while (!kthread_should_stop()) {
		/* wait and check worker queue activities */
		rc = wait_event_interruptible(phba->work_waitq,
					(test_and_clear_bit(LPFC_DATA_READY,
							    &phba->data_flags)
					 || kthread_should_stop()));
		/* Signal wakeup shall terminate the worker thread */
		if (rc) {
			lpfc_printf_log(phba, KERN_ERR, LOG_ELS,
					"0433 Wakeup on signal: rc=x%x\n", rc);
			break;
		}

		/* Attend pending lpfc data processing */
		lpfc_work_done(phba);
	}
	phba->worker_thread = NULL;
	lpfc_printf_log(phba, KERN_INFO, LOG_ELS,
			"0432 Worker thread stopped.\n");
	return 0;
}

/*
 * This is only called to handle FC worker events. Since this a rare
 * occurrence, we allocate a struct lpfc_work_evt structure here instead of
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
	spin_unlock_irqrestore(&phba->hbalock, flags);

	lpfc_worker_wake_up(phba);

	return 1;
}

void
lpfc_cleanup_rpis(struct lpfc_vport *vport, int remove)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct lpfc_hba  *phba = vport->phba;
	struct lpfc_nodelist *ndlp, *next_ndlp;

	list_for_each_entry_safe(ndlp, next_ndlp, &vport->fc_nodes, nlp_listp) {
		if (!NLP_CHK_NODE_ACT(ndlp))
			continue;
		if (ndlp->nlp_state == NLP_STE_UNUSED_NODE)
			continue;
		if ((phba->sli3_options & LPFC_SLI3_VPORT_TEARDOWN) ||
			((vport->port_type == LPFC_NPIV_PORT) &&
			(ndlp->nlp_DID == NameServer_DID)))
			lpfc_unreg_rpi(vport, ndlp);

		/* Leave Fabric nodes alone on link down */
		if ((phba->sli_rev < LPFC_SLI_REV4) &&
		    (!remove && ndlp->nlp_type & NLP_FABRIC))
			continue;
		lpfc_disc_state_machine(vport, ndlp, NULL,
					remove
					? NLP_EVT_DEVICE_RM
					: NLP_EVT_DEVICE_RECOVERY);
	}
	if (phba->sli3_options & LPFC_SLI3_VPORT_TEARDOWN) {
		if (phba->sli_rev == LPFC_SLI_REV4)
			lpfc_sli4_unreg_all_rpis(vport);
		lpfc_mbx_unreg_vpi(vport);
		spin_lock_irq(shost->host_lock);
		vport->fc_flag |= FC_VPORT_NEEDS_REG_VPI;
		spin_unlock_irq(shost->host_lock);
	}
}

void
lpfc_port_link_failure(struct lpfc_vport *vport)
{
	lpfc_vport_set_state(vport, FC_VPORT_LINKDOWN);

	/* Cleanup any outstanding received buffers */
	lpfc_cleanup_rcv_buffers(vport);

	/* Cleanup any outstanding RSCN activity */
	lpfc_els_flush_rscn(vport);

	/* Cleanup any outstanding ELS commands */
	lpfc_els_flush_cmd(vport);

	lpfc_cleanup_rpis(vport, 0);

	/* Turn off discovery timer if its running */
	lpfc_can_disctmo(vport);
}

void
lpfc_linkdown_port(struct lpfc_vport *vport)
{
	struct lpfc_hba  *phba = vport->phba;
	struct Scsi_Host  *shost = lpfc_shost_from_vport(vport);

	if (phba->cfg_enable_fc4_type != LPFC_ENABLE_NVME)
		fc_host_post_event(shost, fc_get_event_number(),
				   FCH_EVT_LINKDOWN, 0);

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_CMD,
		"Link Down:       state:x%x rtry:x%x flg:x%x",
		vport->port_state, vport->fc_ns_retry, vport->fc_flag);

	lpfc_port_link_failure(vport);

	/* Stop delayed Nport discovery */
	spin_lock_irq(shost->host_lock);
	vport->fc_flag &= ~FC_DISC_DELAYED;
	spin_unlock_irq(shost->host_lock);
	del_timer_sync(&vport->delayed_disc_tmo);
}

int
lpfc_linkdown(struct lpfc_hba *phba)
{
	struct lpfc_vport *vport = phba->pport;
	struct Scsi_Host  *shost = lpfc_shost_from_vport(vport);
	struct lpfc_vport **vports;
	LPFC_MBOXQ_t          *mb;
	int i;

	if (phba->link_state == LPFC_LINK_DOWN)
		return 0;

	/* Block all SCSI stack I/Os */
	lpfc_scsi_dev_block(phba);

	spin_lock_irq(&phba->hbalock);
	phba->fcf.fcf_flag &= ~(FCF_AVAILABLE | FCF_SCAN_DONE);
	spin_unlock_irq(&phba->hbalock);
	if (phba->link_state > LPFC_LINK_DOWN) {
		phba->link_state = LPFC_LINK_DOWN;
		spin_lock_irq(shost->host_lock);
		phba->pport->fc_flag &= ~FC_LBIT;
		spin_unlock_irq(shost->host_lock);
	}
	vports = lpfc_create_vport_work_array(phba);
	if (vports != NULL) {
		for (i = 0; i <= phba->max_vports && vports[i] != NULL; i++) {
			/* Issue a LINK DOWN event to all nodes */
			lpfc_linkdown_port(vports[i]);

			vports[i]->fc_myDID = 0;

			if ((phba->cfg_enable_fc4_type == LPFC_ENABLE_BOTH) ||
			    (phba->cfg_enable_fc4_type == LPFC_ENABLE_NVME)) {
				if (phba->nvmet_support)
					lpfc_nvmet_update_targetport(phba);
				else
					lpfc_nvme_update_localport(vports[i]);
			}
		}
	}
	lpfc_destroy_vport_work_array(phba, vports);
	/* Clean up any firmware default rpi's */
	mb = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (mb) {
		lpfc_unreg_did(phba, 0xffff, LPFC_UNREG_ALL_DFLT_RPIS, mb);
		mb->vport = vport;
		mb->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
		if (lpfc_sli_issue_mbox(phba, mb, MBX_NOWAIT)
		    == MBX_NOT_FINISHED) {
			mempool_free(mb, phba->mbox_mem_pool);
		}
	}

	/* Setup myDID for link up if we are in pt2pt mode */
	if (phba->pport->fc_flag & FC_PT2PT) {
		mb = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
		if (mb) {
			lpfc_config_link(phba, mb);
			mb->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
			mb->vport = vport;
			if (lpfc_sli_issue_mbox(phba, mb, MBX_NOWAIT)
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
		if (!NLP_CHK_NODE_ACT(ndlp))
			continue;
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

	if (phba->cfg_enable_fc4_type != LPFC_ENABLE_NVME)
		fc_host_post_event(shost, fc_get_event_number(),
				   FCH_EVT_LINKUP, 0);

	spin_lock_irq(shost->host_lock);
	vport->fc_flag &= ~(FC_PT2PT | FC_PT2PT_PLOGI | FC_ABORT_DISCOVERY |
			    FC_RSCN_MODE | FC_NLP_MORE | FC_RSCN_DISCOVERY);
	vport->fc_flag |= FC_NDISC_ACTIVE;
	vport->fc_ns_retry = 0;
	spin_unlock_irq(shost->host_lock);

	if (vport->fc_flag & FC_LBIT)
		lpfc_linkup_cleanup_nodes(vport);

}

static int
lpfc_linkup(struct lpfc_hba *phba)
{
	struct lpfc_vport **vports;
	int i;

	phba->link_state = LPFC_LINK_UP;

	/* Unblock fabric iocbs if they are blocked */
	clear_bit(FABRIC_COMANDS_BLOCKED, &phba->bit_flags);
	del_timer_sync(&phba->fabric_block_timer);

	vports = lpfc_create_vport_work_array(phba);
	if (vports != NULL)
		for (i = 0; i <= phba->max_vports && vports[i] != NULL; i++)
			lpfc_linkup_port(vports[i]);
	lpfc_destroy_vport_work_array(phba, vports);

	return 0;
}

/*
 * This routine handles processing a CLEAR_LA mailbox
 * command upon completion. It is setup in the LPFC_MBOXQ
 * as the completion routine when the command is
 * handed off to the SLI layer. SLI3 only.
 */
static void
lpfc_mbx_cmpl_clear_la(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmb)
{
	struct lpfc_vport *vport = pmb->vport;
	struct Scsi_Host  *shost = lpfc_shost_from_vport(vport);
	struct lpfc_sli   *psli = &phba->sli;
	MAILBOX_t *mb = &pmb->u.mb;
	uint32_t control;

	/* Since we don't do discovery right now, turn these off here */
	psli->sli3_ring[LPFC_EXTRA_RING].flag &= ~LPFC_STOP_IOCB_EVENT;
	psli->sli3_ring[LPFC_FCP_RING].flag &= ~LPFC_STOP_IOCB_EVENT;

	/* Check for error */
	if ((mb->mbxStatus) && (mb->mbxStatus != 0x1601)) {
		/* CLEAR_LA mbox error <mbxStatus> state <hba_state> */
		lpfc_printf_vlog(vport, KERN_ERR, LOG_MBOX,
				 "0320 CLEAR_LA mbxStatus error x%x hba "
				 "state x%x\n",
				 mb->mbxStatus, vport->port_state);
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
	mempool_free(pmb, phba->mbox_mem_pool);
	return;

out:
	/* Device Discovery completes */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
			 "0225 Device Discovery completes\n");
	mempool_free(pmb, phba->mbox_mem_pool);

	spin_lock_irq(shost->host_lock);
	vport->fc_flag &= ~FC_ABORT_DISCOVERY;
	spin_unlock_irq(shost->host_lock);

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


void
lpfc_mbx_cmpl_local_config_link(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmb)
{
	struct lpfc_vport *vport = pmb->vport;
	uint8_t bbscn = 0;

	if (pmb->u.mb.mbxStatus)
		goto out;

	mempool_free(pmb, phba->mbox_mem_pool);

	/* don't perform discovery for SLI4 loopback diagnostic test */
	if ((phba->sli_rev == LPFC_SLI_REV4) &&
	    !(phba->hba_flag & HBA_FCOE_MODE) &&
	    (phba->link_flag & LS_LOOPBACK_MODE))
		return;

	if (phba->fc_topology == LPFC_TOPOLOGY_LOOP &&
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
		if (phba->bbcredit_support && phba->cfg_enable_bbcr) {
			bbscn = bf_get(lpfc_bbscn_def,
				       &phba->sli4_hba.bbscn_params);
			vport->fc_sparam.cmn.bbRcvSizeMsb &= 0xf;
			vport->fc_sparam.cmn.bbRcvSizeMsb |= (bbscn << 4);
		}
		lpfc_initial_flogi(vport);
	} else if (vport->fc_flag & FC_PT2PT) {
		lpfc_disc_start(vport);
	}
	return;

out:
	lpfc_printf_vlog(vport, KERN_ERR, LOG_MBOX,
			 "0306 CONFIG_LINK mbxStatus error x%x "
			 "HBA state x%x\n",
			 pmb->u.mb.mbxStatus, vport->port_state);
	mempool_free(pmb, phba->mbox_mem_pool);

	lpfc_linkdown(phba);

	lpfc_printf_vlog(vport, KERN_ERR, LOG_DISCOVERY,
			 "0200 CONFIG_LINK bad hba state x%x\n",
			 vport->port_state);

	lpfc_issue_clear_la(phba, vport);
	return;
}

/**
 * lpfc_sli4_clear_fcf_rr_bmask
 * @phba pointer to the struct lpfc_hba for this port.
 * This fucnction resets the round robin bit mask and clears the
 * fcf priority list. The list deletions are done while holding the
 * hbalock. The ON_LIST flag and the FLOGI_FAILED flags are cleared
 * from the lpfc_fcf_pri record.
 **/
void
lpfc_sli4_clear_fcf_rr_bmask(struct lpfc_hba *phba)
{
	struct lpfc_fcf_pri *fcf_pri;
	struct lpfc_fcf_pri *next_fcf_pri;
	memset(phba->fcf.fcf_rr_bmask, 0, sizeof(*phba->fcf.fcf_rr_bmask));
	spin_lock_irq(&phba->hbalock);
	list_for_each_entry_safe(fcf_pri, next_fcf_pri,
				&phba->fcf.fcf_pri_list, list) {
		list_del_init(&fcf_pri->list);
		fcf_pri->fcf_rec.flag = 0;
	}
	spin_unlock_irq(&phba->hbalock);
}
static void
lpfc_mbx_cmpl_reg_fcfi(struct lpfc_hba *phba, LPFC_MBOXQ_t *mboxq)
{
	struct lpfc_vport *vport = mboxq->vport;

	if (mboxq->u.mb.mbxStatus) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_MBOX,
			 "2017 REG_FCFI mbxStatus error x%x "
			 "HBA state x%x\n",
			 mboxq->u.mb.mbxStatus, vport->port_state);
		goto fail_out;
	}

	/* Start FCoE discovery by sending a FLOGI. */
	phba->fcf.fcfi = bf_get(lpfc_reg_fcfi_fcfi, &mboxq->u.mqe.un.reg_fcfi);
	/* Set the FCFI registered flag */
	spin_lock_irq(&phba->hbalock);
	phba->fcf.fcf_flag |= FCF_REGISTERED;
	spin_unlock_irq(&phba->hbalock);

	/* If there is a pending FCoE event, restart FCF table scan. */
	if ((!(phba->hba_flag & FCF_RR_INPROG)) &&
		lpfc_check_pending_fcoe_event(phba, LPFC_UNREG_FCF))
		goto fail_out;

	/* Mark successful completion of FCF table scan */
	spin_lock_irq(&phba->hbalock);
	phba->fcf.fcf_flag |= (FCF_SCAN_DONE | FCF_IN_USE);
	phba->hba_flag &= ~FCF_TS_INPROG;
	if (vport->port_state != LPFC_FLOGI) {
		phba->hba_flag |= FCF_RR_INPROG;
		spin_unlock_irq(&phba->hbalock);
		lpfc_issue_init_vfi(vport);
		goto out;
	}
	spin_unlock_irq(&phba->hbalock);
	goto out;

fail_out:
	spin_lock_irq(&phba->hbalock);
	phba->hba_flag &= ~FCF_RR_INPROG;
	spin_unlock_irq(&phba->hbalock);
out:
	mempool_free(mboxq, phba->mbox_mem_pool);
}

/**
 * lpfc_fab_name_match - Check if the fcf fabric name match.
 * @fab_name: pointer to fabric name.
 * @new_fcf_record: pointer to fcf record.
 *
 * This routine compare the fcf record's fabric name with provided
 * fabric name. If the fabric name are identical this function
 * returns 1 else return 0.
 **/
static uint32_t
lpfc_fab_name_match(uint8_t *fab_name, struct fcf_record *new_fcf_record)
{
	if (fab_name[0] != bf_get(lpfc_fcf_record_fab_name_0, new_fcf_record))
		return 0;
	if (fab_name[1] != bf_get(lpfc_fcf_record_fab_name_1, new_fcf_record))
		return 0;
	if (fab_name[2] != bf_get(lpfc_fcf_record_fab_name_2, new_fcf_record))
		return 0;
	if (fab_name[3] != bf_get(lpfc_fcf_record_fab_name_3, new_fcf_record))
		return 0;
	if (fab_name[4] != bf_get(lpfc_fcf_record_fab_name_4, new_fcf_record))
		return 0;
	if (fab_name[5] != bf_get(lpfc_fcf_record_fab_name_5, new_fcf_record))
		return 0;
	if (fab_name[6] != bf_get(lpfc_fcf_record_fab_name_6, new_fcf_record))
		return 0;
	if (fab_name[7] != bf_get(lpfc_fcf_record_fab_name_7, new_fcf_record))
		return 0;
	return 1;
}

/**
 * lpfc_sw_name_match - Check if the fcf switch name match.
 * @fab_name: pointer to fabric name.
 * @new_fcf_record: pointer to fcf record.
 *
 * This routine compare the fcf record's switch name with provided
 * switch name. If the switch name are identical this function
 * returns 1 else return 0.
 **/
static uint32_t
lpfc_sw_name_match(uint8_t *sw_name, struct fcf_record *new_fcf_record)
{
	if (sw_name[0] != bf_get(lpfc_fcf_record_switch_name_0, new_fcf_record))
		return 0;
	if (sw_name[1] != bf_get(lpfc_fcf_record_switch_name_1, new_fcf_record))
		return 0;
	if (sw_name[2] != bf_get(lpfc_fcf_record_switch_name_2, new_fcf_record))
		return 0;
	if (sw_name[3] != bf_get(lpfc_fcf_record_switch_name_3, new_fcf_record))
		return 0;
	if (sw_name[4] != bf_get(lpfc_fcf_record_switch_name_4, new_fcf_record))
		return 0;
	if (sw_name[5] != bf_get(lpfc_fcf_record_switch_name_5, new_fcf_record))
		return 0;
	if (sw_name[6] != bf_get(lpfc_fcf_record_switch_name_6, new_fcf_record))
		return 0;
	if (sw_name[7] != bf_get(lpfc_fcf_record_switch_name_7, new_fcf_record))
		return 0;
	return 1;
}

/**
 * lpfc_mac_addr_match - Check if the fcf mac address match.
 * @mac_addr: pointer to mac address.
 * @new_fcf_record: pointer to fcf record.
 *
 * This routine compare the fcf record's mac address with HBA's
 * FCF mac address. If the mac addresses are identical this function
 * returns 1 else return 0.
 **/
static uint32_t
lpfc_mac_addr_match(uint8_t *mac_addr, struct fcf_record *new_fcf_record)
{
	if (mac_addr[0] != bf_get(lpfc_fcf_record_mac_0, new_fcf_record))
		return 0;
	if (mac_addr[1] != bf_get(lpfc_fcf_record_mac_1, new_fcf_record))
		return 0;
	if (mac_addr[2] != bf_get(lpfc_fcf_record_mac_2, new_fcf_record))
		return 0;
	if (mac_addr[3] != bf_get(lpfc_fcf_record_mac_3, new_fcf_record))
		return 0;
	if (mac_addr[4] != bf_get(lpfc_fcf_record_mac_4, new_fcf_record))
		return 0;
	if (mac_addr[5] != bf_get(lpfc_fcf_record_mac_5, new_fcf_record))
		return 0;
	return 1;
}

static bool
lpfc_vlan_id_match(uint16_t curr_vlan_id, uint16_t new_vlan_id)
{
	return (curr_vlan_id == new_vlan_id);
}

/**
 * lpfc_update_fcf_record - Update driver fcf record
 * __lpfc_update_fcf_record_pri - update the lpfc_fcf_pri record.
 * @phba: pointer to lpfc hba data structure.
 * @fcf_index: Index for the lpfc_fcf_record.
 * @new_fcf_record: pointer to hba fcf record.
 *
 * This routine updates the driver FCF priority record from the new HBA FCF
 * record. This routine is called with the host lock held.
 **/
static void
__lpfc_update_fcf_record_pri(struct lpfc_hba *phba, uint16_t fcf_index,
				 struct fcf_record *new_fcf_record
				 )
{
	struct lpfc_fcf_pri *fcf_pri;

	lockdep_assert_held(&phba->hbalock);

	fcf_pri = &phba->fcf.fcf_pri[fcf_index];
	fcf_pri->fcf_rec.fcf_index = fcf_index;
	/* FCF record priority */
	fcf_pri->fcf_rec.priority = new_fcf_record->fip_priority;

}

/**
 * lpfc_copy_fcf_record - Copy fcf information to lpfc_hba.
 * @fcf: pointer to driver fcf record.
 * @new_fcf_record: pointer to fcf record.
 *
 * This routine copies the FCF information from the FCF
 * record to lpfc_hba data structure.
 **/
static void
lpfc_copy_fcf_record(struct lpfc_fcf_rec *fcf_rec,
		     struct fcf_record *new_fcf_record)
{
	/* Fabric name */
	fcf_rec->fabric_name[0] =
		bf_get(lpfc_fcf_record_fab_name_0, new_fcf_record);
	fcf_rec->fabric_name[1] =
		bf_get(lpfc_fcf_record_fab_name_1, new_fcf_record);
	fcf_rec->fabric_name[2] =
		bf_get(lpfc_fcf_record_fab_name_2, new_fcf_record);
	fcf_rec->fabric_name[3] =
		bf_get(lpfc_fcf_record_fab_name_3, new_fcf_record);
	fcf_rec->fabric_name[4] =
		bf_get(lpfc_fcf_record_fab_name_4, new_fcf_record);
	fcf_rec->fabric_name[5] =
		bf_get(lpfc_fcf_record_fab_name_5, new_fcf_record);
	fcf_rec->fabric_name[6] =
		bf_get(lpfc_fcf_record_fab_name_6, new_fcf_record);
	fcf_rec->fabric_name[7] =
		bf_get(lpfc_fcf_record_fab_name_7, new_fcf_record);
	/* Mac address */
	fcf_rec->mac_addr[0] = bf_get(lpfc_fcf_record_mac_0, new_fcf_record);
	fcf_rec->mac_addr[1] = bf_get(lpfc_fcf_record_mac_1, new_fcf_record);
	fcf_rec->mac_addr[2] = bf_get(lpfc_fcf_record_mac_2, new_fcf_record);
	fcf_rec->mac_addr[3] = bf_get(lpfc_fcf_record_mac_3, new_fcf_record);
	fcf_rec->mac_addr[4] = bf_get(lpfc_fcf_record_mac_4, new_fcf_record);
	fcf_rec->mac_addr[5] = bf_get(lpfc_fcf_record_mac_5, new_fcf_record);
	/* FCF record index */
	fcf_rec->fcf_indx = bf_get(lpfc_fcf_record_fcf_index, new_fcf_record);
	/* FCF record priority */
	fcf_rec->priority = new_fcf_record->fip_priority;
	/* Switch name */
	fcf_rec->switch_name[0] =
		bf_get(lpfc_fcf_record_switch_name_0, new_fcf_record);
	fcf_rec->switch_name[1] =
		bf_get(lpfc_fcf_record_switch_name_1, new_fcf_record);
	fcf_rec->switch_name[2] =
		bf_get(lpfc_fcf_record_switch_name_2, new_fcf_record);
	fcf_rec->switch_name[3] =
		bf_get(lpfc_fcf_record_switch_name_3, new_fcf_record);
	fcf_rec->switch_name[4] =
		bf_get(lpfc_fcf_record_switch_name_4, new_fcf_record);
	fcf_rec->switch_name[5] =
		bf_get(lpfc_fcf_record_switch_name_5, new_fcf_record);
	fcf_rec->switch_name[6] =
		bf_get(lpfc_fcf_record_switch_name_6, new_fcf_record);
	fcf_rec->switch_name[7] =
		bf_get(lpfc_fcf_record_switch_name_7, new_fcf_record);
}

/**
 * lpfc_update_fcf_record - Update driver fcf record
 * @phba: pointer to lpfc hba data structure.
 * @fcf_rec: pointer to driver fcf record.
 * @new_fcf_record: pointer to hba fcf record.
 * @addr_mode: address mode to be set to the driver fcf record.
 * @vlan_id: vlan tag to be set to the driver fcf record.
 * @flag: flag bits to be set to the driver fcf record.
 *
 * This routine updates the driver FCF record from the new HBA FCF record
 * together with the address mode, vlan_id, and other informations. This
 * routine is called with the host lock held.
 **/
static void
__lpfc_update_fcf_record(struct lpfc_hba *phba, struct lpfc_fcf_rec *fcf_rec,
		       struct fcf_record *new_fcf_record, uint32_t addr_mode,
		       uint16_t vlan_id, uint32_t flag)
{
	lockdep_assert_held(&phba->hbalock);

	/* Copy the fields from the HBA's FCF record */
	lpfc_copy_fcf_record(fcf_rec, new_fcf_record);
	/* Update other fields of driver FCF record */
	fcf_rec->addr_mode = addr_mode;
	fcf_rec->vlan_id = vlan_id;
	fcf_rec->flag |= (flag | RECORD_VALID);
	__lpfc_update_fcf_record_pri(phba,
		bf_get(lpfc_fcf_record_fcf_index, new_fcf_record),
				 new_fcf_record);
}

/**
 * lpfc_register_fcf - Register the FCF with hba.
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine issues a register fcfi mailbox command to register
 * the fcf with HBA.
 **/
static void
lpfc_register_fcf(struct lpfc_hba *phba)
{
	LPFC_MBOXQ_t *fcf_mbxq;
	int rc;

	spin_lock_irq(&phba->hbalock);
	/* If the FCF is not available do nothing. */
	if (!(phba->fcf.fcf_flag & FCF_AVAILABLE)) {
		phba->hba_flag &= ~(FCF_TS_INPROG | FCF_RR_INPROG);
		spin_unlock_irq(&phba->hbalock);
		return;
	}

	/* The FCF is already registered, start discovery */
	if (phba->fcf.fcf_flag & FCF_REGISTERED) {
		phba->fcf.fcf_flag |= (FCF_SCAN_DONE | FCF_IN_USE);
		phba->hba_flag &= ~FCF_TS_INPROG;
		if (phba->pport->port_state != LPFC_FLOGI &&
		    phba->pport->fc_flag & FC_FABRIC) {
			phba->hba_flag |= FCF_RR_INPROG;
			spin_unlock_irq(&phba->hbalock);
			lpfc_initial_flogi(phba->pport);
			return;
		}
		spin_unlock_irq(&phba->hbalock);
		return;
	}
	spin_unlock_irq(&phba->hbalock);

	fcf_mbxq = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!fcf_mbxq) {
		spin_lock_irq(&phba->hbalock);
		phba->hba_flag &= ~(FCF_TS_INPROG | FCF_RR_INPROG);
		spin_unlock_irq(&phba->hbalock);
		return;
	}

	lpfc_reg_fcfi(phba, fcf_mbxq);
	fcf_mbxq->vport = phba->pport;
	fcf_mbxq->mbox_cmpl = lpfc_mbx_cmpl_reg_fcfi;
	rc = lpfc_sli_issue_mbox(phba, fcf_mbxq, MBX_NOWAIT);
	if (rc == MBX_NOT_FINISHED) {
		spin_lock_irq(&phba->hbalock);
		phba->hba_flag &= ~(FCF_TS_INPROG | FCF_RR_INPROG);
		spin_unlock_irq(&phba->hbalock);
		mempool_free(fcf_mbxq, phba->mbox_mem_pool);
	}

	return;
}

/**
 * lpfc_match_fcf_conn_list - Check if the FCF record can be used for discovery.
 * @phba: pointer to lpfc hba data structure.
 * @new_fcf_record: pointer to fcf record.
 * @boot_flag: Indicates if this record used by boot bios.
 * @addr_mode: The address mode to be used by this FCF
 * @vlan_id: The vlan id to be used as vlan tagging by this FCF.
 *
 * This routine compare the fcf record with connect list obtained from the
 * config region to decide if this FCF can be used for SAN discovery. It returns
 * 1 if this record can be used for SAN discovery else return zero. If this FCF
 * record can be used for SAN discovery, the boot_flag will indicate if this FCF
 * is used by boot bios and addr_mode will indicate the addressing mode to be
 * used for this FCF when the function returns.
 * If the FCF record need to be used with a particular vlan id, the vlan is
 * set in the vlan_id on return of the function. If not VLAN tagging need to
 * be used with the FCF vlan_id will be set to LPFC_FCOE_NULL_VID;
 **/
static int
lpfc_match_fcf_conn_list(struct lpfc_hba *phba,
			struct fcf_record *new_fcf_record,
			uint32_t *boot_flag, uint32_t *addr_mode,
			uint16_t *vlan_id)
{
	struct lpfc_fcf_conn_entry *conn_entry;
	int i, j, fcf_vlan_id = 0;

	/* Find the lowest VLAN id in the FCF record */
	for (i = 0; i < 512; i++) {
		if (new_fcf_record->vlan_bitmap[i]) {
			fcf_vlan_id = i * 8;
			j = 0;
			while (!((new_fcf_record->vlan_bitmap[i] >> j) & 1)) {
				j++;
				fcf_vlan_id++;
			}
			break;
		}
	}

	/* FCF not valid/available or solicitation in progress */
	if (!bf_get(lpfc_fcf_record_fcf_avail, new_fcf_record) ||
	    !bf_get(lpfc_fcf_record_fcf_valid, new_fcf_record) ||
	    bf_get(lpfc_fcf_record_fcf_sol, new_fcf_record))
		return 0;

	if (!(phba->hba_flag & HBA_FIP_SUPPORT)) {
		*boot_flag = 0;
		*addr_mode = bf_get(lpfc_fcf_record_mac_addr_prov,
				new_fcf_record);
		if (phba->valid_vlan)
			*vlan_id = phba->vlan_id;
		else
			*vlan_id = LPFC_FCOE_NULL_VID;
		return 1;
	}

	/*
	 * If there are no FCF connection table entry, driver connect to all
	 * FCFs.
	 */
	if (list_empty(&phba->fcf_conn_rec_list)) {
		*boot_flag = 0;
		*addr_mode = bf_get(lpfc_fcf_record_mac_addr_prov,
			new_fcf_record);

		/*
		 * When there are no FCF connect entries, use driver's default
		 * addressing mode - FPMA.
		 */
		if (*addr_mode & LPFC_FCF_FPMA)
			*addr_mode = LPFC_FCF_FPMA;

		/* If FCF record report a vlan id use that vlan id */
		if (fcf_vlan_id)
			*vlan_id = fcf_vlan_id;
		else
			*vlan_id = LPFC_FCOE_NULL_VID;
		return 1;
	}

	list_for_each_entry(conn_entry,
			    &phba->fcf_conn_rec_list, list) {
		if (!(conn_entry->conn_rec.flags & FCFCNCT_VALID))
			continue;

		if ((conn_entry->conn_rec.flags & FCFCNCT_FBNM_VALID) &&
			!lpfc_fab_name_match(conn_entry->conn_rec.fabric_name,
					     new_fcf_record))
			continue;
		if ((conn_entry->conn_rec.flags & FCFCNCT_SWNM_VALID) &&
			!lpfc_sw_name_match(conn_entry->conn_rec.switch_name,
					    new_fcf_record))
			continue;
		if (conn_entry->conn_rec.flags & FCFCNCT_VLAN_VALID) {
			/*
			 * If the vlan bit map does not have the bit set for the
			 * vlan id to be used, then it is not a match.
			 */
			if (!(new_fcf_record->vlan_bitmap
				[conn_entry->conn_rec.vlan_tag / 8] &
				(1 << (conn_entry->conn_rec.vlan_tag % 8))))
				continue;
		}

		/*
		 * If connection record does not support any addressing mode,
		 * skip the FCF record.
		 */
		if (!(bf_get(lpfc_fcf_record_mac_addr_prov, new_fcf_record)
			& (LPFC_FCF_FPMA | LPFC_FCF_SPMA)))
			continue;

		/*
		 * Check if the connection record specifies a required
		 * addressing mode.
		 */
		if ((conn_entry->conn_rec.flags & FCFCNCT_AM_VALID) &&
			!(conn_entry->conn_rec.flags & FCFCNCT_AM_PREFERRED)) {

			/*
			 * If SPMA required but FCF not support this continue.
			 */
			if ((conn_entry->conn_rec.flags & FCFCNCT_AM_SPMA) &&
				!(bf_get(lpfc_fcf_record_mac_addr_prov,
					new_fcf_record) & LPFC_FCF_SPMA))
				continue;

			/*
			 * If FPMA required but FCF not support this continue.
			 */
			if (!(conn_entry->conn_rec.flags & FCFCNCT_AM_SPMA) &&
				!(bf_get(lpfc_fcf_record_mac_addr_prov,
				new_fcf_record) & LPFC_FCF_FPMA))
				continue;
		}

		/*
		 * This fcf record matches filtering criteria.
		 */
		if (conn_entry->conn_rec.flags & FCFCNCT_BOOT)
			*boot_flag = 1;
		else
			*boot_flag = 0;

		/*
		 * If user did not specify any addressing mode, or if the
		 * preferred addressing mode specified by user is not supported
		 * by FCF, allow fabric to pick the addressing mode.
		 */
		*addr_mode = bf_get(lpfc_fcf_record_mac_addr_prov,
				new_fcf_record);
		/*
		 * If the user specified a required address mode, assign that
		 * address mode
		 */
		if ((conn_entry->conn_rec.flags & FCFCNCT_AM_VALID) &&
			(!(conn_entry->conn_rec.flags & FCFCNCT_AM_PREFERRED)))
			*addr_mode = (conn_entry->conn_rec.flags &
				FCFCNCT_AM_SPMA) ?
				LPFC_FCF_SPMA : LPFC_FCF_FPMA;
		/*
		 * If the user specified a preferred address mode, use the
		 * addr mode only if FCF support the addr_mode.
		 */
		else if ((conn_entry->conn_rec.flags & FCFCNCT_AM_VALID) &&
			(conn_entry->conn_rec.flags & FCFCNCT_AM_PREFERRED) &&
			(conn_entry->conn_rec.flags & FCFCNCT_AM_SPMA) &&
			(*addr_mode & LPFC_FCF_SPMA))
				*addr_mode = LPFC_FCF_SPMA;
		else if ((conn_entry->conn_rec.flags & FCFCNCT_AM_VALID) &&
			(conn_entry->conn_rec.flags & FCFCNCT_AM_PREFERRED) &&
			!(conn_entry->conn_rec.flags & FCFCNCT_AM_SPMA) &&
			(*addr_mode & LPFC_FCF_FPMA))
				*addr_mode = LPFC_FCF_FPMA;

		/* If matching connect list has a vlan id, use it */
		if (conn_entry->conn_rec.flags & FCFCNCT_VLAN_VALID)
			*vlan_id = conn_entry->conn_rec.vlan_tag;
		/*
		 * If no vlan id is specified in connect list, use the vlan id
		 * in the FCF record
		 */
		else if (fcf_vlan_id)
			*vlan_id = fcf_vlan_id;
		else
			*vlan_id = LPFC_FCOE_NULL_VID;

		return 1;
	}

	return 0;
}

/**
 * lpfc_check_pending_fcoe_event - Check if there is pending fcoe event.
 * @phba: pointer to lpfc hba data structure.
 * @unreg_fcf: Unregister FCF if FCF table need to be re-scaned.
 *
 * This function check if there is any fcoe event pending while driver
 * scan FCF entries. If there is any pending event, it will restart the
 * FCF saning and return 1 else return 0.
 */
int
lpfc_check_pending_fcoe_event(struct lpfc_hba *phba, uint8_t unreg_fcf)
{
	/*
	 * If the Link is up and no FCoE events while in the
	 * FCF discovery, no need to restart FCF discovery.
	 */
	if ((phba->link_state  >= LPFC_LINK_UP) &&
	    (phba->fcoe_eventtag == phba->fcoe_eventtag_at_fcf_scan))
		return 0;

	lpfc_printf_log(phba, KERN_INFO, LOG_FIP,
			"2768 Pending link or FCF event during current "
			"handling of the previous event: link_state:x%x, "
			"evt_tag_at_scan:x%x, evt_tag_current:x%x\n",
			phba->link_state, phba->fcoe_eventtag_at_fcf_scan,
			phba->fcoe_eventtag);

	spin_lock_irq(&phba->hbalock);
	phba->fcf.fcf_flag &= ~FCF_AVAILABLE;
	spin_unlock_irq(&phba->hbalock);

	if (phba->link_state >= LPFC_LINK_UP) {
		lpfc_printf_log(phba, KERN_INFO, LOG_FIP | LOG_DISCOVERY,
				"2780 Restart FCF table scan due to "
				"pending FCF event:evt_tag_at_scan:x%x, "
				"evt_tag_current:x%x\n",
				phba->fcoe_eventtag_at_fcf_scan,
				phba->fcoe_eventtag);
		lpfc_sli4_fcf_scan_read_fcf_rec(phba, LPFC_FCOE_FCF_GET_FIRST);
	} else {
		/*
		 * Do not continue FCF discovery and clear FCF_TS_INPROG
		 * flag
		 */
		lpfc_printf_log(phba, KERN_INFO, LOG_FIP | LOG_DISCOVERY,
				"2833 Stop FCF discovery process due to link "
				"state change (x%x)\n", phba->link_state);
		spin_lock_irq(&phba->hbalock);
		phba->hba_flag &= ~(FCF_TS_INPROG | FCF_RR_INPROG);
		phba->fcf.fcf_flag &= ~(FCF_REDISC_FOV | FCF_DISCOVERY);
		spin_unlock_irq(&phba->hbalock);
	}

	/* Unregister the currently registered FCF if required */
	if (unreg_fcf) {
		spin_lock_irq(&phba->hbalock);
		phba->fcf.fcf_flag &= ~FCF_REGISTERED;
		spin_unlock_irq(&phba->hbalock);
		lpfc_sli4_unregister_fcf(phba);
	}
	return 1;
}

/**
 * lpfc_sli4_new_fcf_random_select - Randomly select an eligible new fcf record
 * @phba: pointer to lpfc hba data structure.
 * @fcf_cnt: number of eligible fcf record seen so far.
 *
 * This function makes an running random selection decision on FCF record to
 * use through a sequence of @fcf_cnt eligible FCF records with equal
 * probability. To perform integer manunipulation of random numbers with
 * size unit32_t, the lower 16 bits of the 32-bit random number returned
 * from prandom_u32() are taken as the random random number generated.
 *
 * Returns true when outcome is for the newly read FCF record should be
 * chosen; otherwise, return false when outcome is for keeping the previously
 * chosen FCF record.
 **/
static bool
lpfc_sli4_new_fcf_random_select(struct lpfc_hba *phba, uint32_t fcf_cnt)
{
	uint32_t rand_num;

	/* Get 16-bit uniform random number */
	rand_num = 0xFFFF & prandom_u32();

	/* Decision with probability 1/fcf_cnt */
	if ((fcf_cnt * rand_num) < 0xFFFF)
		return true;
	else
		return false;
}

/**
 * lpfc_sli4_fcf_rec_mbox_parse - Parse read_fcf mbox command.
 * @phba: pointer to lpfc hba data structure.
 * @mboxq: pointer to mailbox object.
 * @next_fcf_index: pointer to holder of next fcf index.
 *
 * This routine parses the non-embedded fcf mailbox command by performing the
 * necessarily error checking, non-embedded read FCF record mailbox command
 * SGE parsing, and endianness swapping.
 *
 * Returns the pointer to the new FCF record in the non-embedded mailbox
 * command DMA memory if successfully, other NULL.
 */
static struct fcf_record *
lpfc_sli4_fcf_rec_mbox_parse(struct lpfc_hba *phba, LPFC_MBOXQ_t *mboxq,
			     uint16_t *next_fcf_index)
{
	void *virt_addr;
	struct lpfc_mbx_sge sge;
	struct lpfc_mbx_read_fcf_tbl *read_fcf;
	uint32_t shdr_status, shdr_add_status, if_type;
	union lpfc_sli4_cfg_shdr *shdr;
	struct fcf_record *new_fcf_record;

	/* Get the first SGE entry from the non-embedded DMA memory. This
	 * routine only uses a single SGE.
	 */
	lpfc_sli4_mbx_sge_get(mboxq, 0, &sge);
	if (unlikely(!mboxq->sge_array)) {
		lpfc_printf_log(phba, KERN_ERR, LOG_MBOX,
				"2524 Failed to get the non-embedded SGE "
				"virtual address\n");
		return NULL;
	}
	virt_addr = mboxq->sge_array->addr[0];

	shdr = (union lpfc_sli4_cfg_shdr *)virt_addr;
	lpfc_sli_pcimem_bcopy(shdr, shdr,
			      sizeof(union lpfc_sli4_cfg_shdr));
	shdr_status = bf_get(lpfc_mbox_hdr_status, &shdr->response);
	if_type = bf_get(lpfc_sli_intf_if_type, &phba->sli4_hba.sli_intf);
	shdr_add_status = bf_get(lpfc_mbox_hdr_add_status, &shdr->response);
	if (shdr_status || shdr_add_status) {
		if (shdr_status == STATUS_FCF_TABLE_EMPTY ||
					if_type == LPFC_SLI_INTF_IF_TYPE_2)
			lpfc_printf_log(phba, KERN_ERR, LOG_FIP,
					"2726 READ_FCF_RECORD Indicates empty "
					"FCF table.\n");
		else
			lpfc_printf_log(phba, KERN_ERR, LOG_FIP,
					"2521 READ_FCF_RECORD mailbox failed "
					"with status x%x add_status x%x, "
					"mbx\n", shdr_status, shdr_add_status);
		return NULL;
	}

	/* Interpreting the returned information of the FCF record */
	read_fcf = (struct lpfc_mbx_read_fcf_tbl *)virt_addr;
	lpfc_sli_pcimem_bcopy(read_fcf, read_fcf,
			      sizeof(struct lpfc_mbx_read_fcf_tbl));
	*next_fcf_index = bf_get(lpfc_mbx_read_fcf_tbl_nxt_vindx, read_fcf);
	new_fcf_record = (struct fcf_record *)(virt_addr +
			  sizeof(struct lpfc_mbx_read_fcf_tbl));
	lpfc_sli_pcimem_bcopy(new_fcf_record, new_fcf_record,
				offsetof(struct fcf_record, vlan_bitmap));
	new_fcf_record->word137 = le32_to_cpu(new_fcf_record->word137);
	new_fcf_record->word138 = le32_to_cpu(new_fcf_record->word138);

	return new_fcf_record;
}

/**
 * lpfc_sli4_log_fcf_record_info - Log the information of a fcf record
 * @phba: pointer to lpfc hba data structure.
 * @fcf_record: pointer to the fcf record.
 * @vlan_id: the lowest vlan identifier associated to this fcf record.
 * @next_fcf_index: the index to the next fcf record in hba's fcf table.
 *
 * This routine logs the detailed FCF record if the LOG_FIP loggin is
 * enabled.
 **/
static void
lpfc_sli4_log_fcf_record_info(struct lpfc_hba *phba,
			      struct fcf_record *fcf_record,
			      uint16_t vlan_id,
			      uint16_t next_fcf_index)
{
	lpfc_printf_log(phba, KERN_INFO, LOG_FIP,
			"2764 READ_FCF_RECORD:\n"
			"\tFCF_Index     : x%x\n"
			"\tFCF_Avail     : x%x\n"
			"\tFCF_Valid     : x%x\n"
			"\tFCF_SOL       : x%x\n"
			"\tFIP_Priority  : x%x\n"
			"\tMAC_Provider  : x%x\n"
			"\tLowest VLANID : x%x\n"
			"\tFCF_MAC Addr  : x%x:%x:%x:%x:%x:%x\n"
			"\tFabric_Name   : x%x:%x:%x:%x:%x:%x:%x:%x\n"
			"\tSwitch_Name   : x%x:%x:%x:%x:%x:%x:%x:%x\n"
			"\tNext_FCF_Index: x%x\n",
			bf_get(lpfc_fcf_record_fcf_index, fcf_record),
			bf_get(lpfc_fcf_record_fcf_avail, fcf_record),
			bf_get(lpfc_fcf_record_fcf_valid, fcf_record),
			bf_get(lpfc_fcf_record_fcf_sol, fcf_record),
			fcf_record->fip_priority,
			bf_get(lpfc_fcf_record_mac_addr_prov, fcf_record),
			vlan_id,
			bf_get(lpfc_fcf_record_mac_0, fcf_record),
			bf_get(lpfc_fcf_record_mac_1, fcf_record),
			bf_get(lpfc_fcf_record_mac_2, fcf_record),
			bf_get(lpfc_fcf_record_mac_3, fcf_record),
			bf_get(lpfc_fcf_record_mac_4, fcf_record),
			bf_get(lpfc_fcf_record_mac_5, fcf_record),
			bf_get(lpfc_fcf_record_fab_name_0, fcf_record),
			bf_get(lpfc_fcf_record_fab_name_1, fcf_record),
			bf_get(lpfc_fcf_record_fab_name_2, fcf_record),
			bf_get(lpfc_fcf_record_fab_name_3, fcf_record),
			bf_get(lpfc_fcf_record_fab_name_4, fcf_record),
			bf_get(lpfc_fcf_record_fab_name_5, fcf_record),
			bf_get(lpfc_fcf_record_fab_name_6, fcf_record),
			bf_get(lpfc_fcf_record_fab_name_7, fcf_record),
			bf_get(lpfc_fcf_record_switch_name_0, fcf_record),
			bf_get(lpfc_fcf_record_switch_name_1, fcf_record),
			bf_get(lpfc_fcf_record_switch_name_2, fcf_record),
			bf_get(lpfc_fcf_record_switch_name_3, fcf_record),
			bf_get(lpfc_fcf_record_switch_name_4, fcf_record),
			bf_get(lpfc_fcf_record_switch_name_5, fcf_record),
			bf_get(lpfc_fcf_record_switch_name_6, fcf_record),
			bf_get(lpfc_fcf_record_switch_name_7, fcf_record),
			next_fcf_index);
}

/**
 lpfc_sli4_fcf_record_match - testing new FCF record for matching existing FCF
 * @phba: pointer to lpfc hba data structure.
 * @fcf_rec: pointer to an existing FCF record.
 * @new_fcf_record: pointer to a new FCF record.
 * @new_vlan_id: vlan id from the new FCF record.
 *
 * This function performs matching test of a new FCF record against an existing
 * FCF record. If the new_vlan_id passed in is LPFC_FCOE_IGNORE_VID, vlan id
 * will not be used as part of the FCF record matching criteria.
 *
 * Returns true if all the fields matching, otherwise returns false.
 */
static bool
lpfc_sli4_fcf_record_match(struct lpfc_hba *phba,
			   struct lpfc_fcf_rec *fcf_rec,
			   struct fcf_record *new_fcf_record,
			   uint16_t new_vlan_id)
{
	if (new_vlan_id != LPFC_FCOE_IGNORE_VID)
		if (!lpfc_vlan_id_match(fcf_rec->vlan_id, new_vlan_id))
			return false;
	if (!lpfc_mac_addr_match(fcf_rec->mac_addr, new_fcf_record))
		return false;
	if (!lpfc_sw_name_match(fcf_rec->switch_name, new_fcf_record))
		return false;
	if (!lpfc_fab_name_match(fcf_rec->fabric_name, new_fcf_record))
		return false;
	if (fcf_rec->priority != new_fcf_record->fip_priority)
		return false;
	return true;
}

/**
 * lpfc_sli4_fcf_rr_next_proc - processing next roundrobin fcf
 * @vport: Pointer to vport object.
 * @fcf_index: index to next fcf.
 *
 * This function processing the roundrobin fcf failover to next fcf index.
 * When this function is invoked, there will be a current fcf registered
 * for flogi.
 * Return: 0 for continue retrying flogi on currently registered fcf;
 *         1 for stop flogi on currently registered fcf;
 */
int lpfc_sli4_fcf_rr_next_proc(struct lpfc_vport *vport, uint16_t fcf_index)
{
	struct lpfc_hba *phba = vport->phba;
	int rc;

	if (fcf_index == LPFC_FCOE_FCF_NEXT_NONE) {
		spin_lock_irq(&phba->hbalock);
		if (phba->hba_flag & HBA_DEVLOSS_TMO) {
			spin_unlock_irq(&phba->hbalock);
			lpfc_printf_log(phba, KERN_INFO, LOG_FIP,
					"2872 Devloss tmo with no eligible "
					"FCF, unregister in-use FCF (x%x) "
					"and rescan FCF table\n",
					phba->fcf.current_rec.fcf_indx);
			lpfc_unregister_fcf_rescan(phba);
			goto stop_flogi_current_fcf;
		}
		/* Mark the end to FLOGI roundrobin failover */
		phba->hba_flag &= ~FCF_RR_INPROG;
		/* Allow action to new fcf asynchronous event */
		phba->fcf.fcf_flag &= ~(FCF_AVAILABLE | FCF_SCAN_DONE);
		spin_unlock_irq(&phba->hbalock);
		lpfc_printf_log(phba, KERN_INFO, LOG_FIP,
				"2865 No FCF available, stop roundrobin FCF "
				"failover and change port state:x%x/x%x\n",
				phba->pport->port_state, LPFC_VPORT_UNKNOWN);
		phba->pport->port_state = LPFC_VPORT_UNKNOWN;
		goto stop_flogi_current_fcf;
	} else {
		lpfc_printf_log(phba, KERN_INFO, LOG_FIP | LOG_ELS,
				"2794 Try FLOGI roundrobin FCF failover to "
				"(x%x)\n", fcf_index);
		rc = lpfc_sli4_fcf_rr_read_fcf_rec(phba, fcf_index);
		if (rc)
			lpfc_printf_log(phba, KERN_WARNING, LOG_FIP | LOG_ELS,
					"2761 FLOGI roundrobin FCF failover "
					"failed (rc:x%x) to read FCF (x%x)\n",
					rc, phba->fcf.current_rec.fcf_indx);
		else
			goto stop_flogi_current_fcf;
	}
	return 0;

stop_flogi_current_fcf:
	lpfc_can_disctmo(vport);
	return 1;
}

/**
 * lpfc_sli4_fcf_pri_list_del
 * @phba: pointer to lpfc hba data structure.
 * @fcf_index the index of the fcf record to delete
 * This routine checks the on list flag of the fcf_index to be deleted.
 * If it is one the list then it is removed from the list, and the flag
 * is cleared. This routine grab the hbalock before removing the fcf
 * record from the list.
 **/
static void lpfc_sli4_fcf_pri_list_del(struct lpfc_hba *phba,
			uint16_t fcf_index)
{
	struct lpfc_fcf_pri *new_fcf_pri;

	new_fcf_pri = &phba->fcf.fcf_pri[fcf_index];
	lpfc_printf_log(phba, KERN_INFO, LOG_FIP,
		"3058 deleting idx x%x pri x%x flg x%x\n",
		fcf_index, new_fcf_pri->fcf_rec.priority,
		 new_fcf_pri->fcf_rec.flag);
	spin_lock_irq(&phba->hbalock);
	if (new_fcf_pri->fcf_rec.flag & LPFC_FCF_ON_PRI_LIST) {
		if (phba->fcf.current_rec.priority ==
				new_fcf_pri->fcf_rec.priority)
			phba->fcf.eligible_fcf_cnt--;
		list_del_init(&new_fcf_pri->list);
		new_fcf_pri->fcf_rec.flag &= ~LPFC_FCF_ON_PRI_LIST;
	}
	spin_unlock_irq(&phba->hbalock);
}

/**
 * lpfc_sli4_set_fcf_flogi_fail
 * @phba: pointer to lpfc hba data structure.
 * @fcf_index the index of the fcf record to update
 * This routine acquires the hbalock and then set the LPFC_FCF_FLOGI_FAILED
 * flag so the the round robin slection for the particular priority level
 * will try a different fcf record that does not have this bit set.
 * If the fcf record is re-read for any reason this flag is cleared brfore
 * adding it to the priority list.
 **/
void
lpfc_sli4_set_fcf_flogi_fail(struct lpfc_hba *phba, uint16_t fcf_index)
{
	struct lpfc_fcf_pri *new_fcf_pri;
	new_fcf_pri = &phba->fcf.fcf_pri[fcf_index];
	spin_lock_irq(&phba->hbalock);
	new_fcf_pri->fcf_rec.flag |= LPFC_FCF_FLOGI_FAILED;
	spin_unlock_irq(&phba->hbalock);
}

/**
 * lpfc_sli4_fcf_pri_list_add
 * @phba: pointer to lpfc hba data structure.
 * @fcf_index the index of the fcf record to add
 * This routine checks the priority of the fcf_index to be added.
 * If it is a lower priority than the current head of the fcf_pri list
 * then it is added to the list in the right order.
 * If it is the same priority as the current head of the list then it
 * is added to the head of the list and its bit in the rr_bmask is set.
 * If the fcf_index to be added is of a higher priority than the current
 * head of the list then the rr_bmask is cleared, its bit is set in the
 * rr_bmask and it is added to the head of the list.
 * returns:
 * 0=success 1=failure
 **/
static int lpfc_sli4_fcf_pri_list_add(struct lpfc_hba *phba,
	uint16_t fcf_index,
	struct fcf_record *new_fcf_record)
{
	uint16_t current_fcf_pri;
	uint16_t last_index;
	struct lpfc_fcf_pri *fcf_pri;
	struct lpfc_fcf_pri *next_fcf_pri;
	struct lpfc_fcf_pri *new_fcf_pri;
	int ret;

	new_fcf_pri = &phba->fcf.fcf_pri[fcf_index];
	lpfc_printf_log(phba, KERN_INFO, LOG_FIP,
		"3059 adding idx x%x pri x%x flg x%x\n",
		fcf_index, new_fcf_record->fip_priority,
		 new_fcf_pri->fcf_rec.flag);
	spin_lock_irq(&phba->hbalock);
	if (new_fcf_pri->fcf_rec.flag & LPFC_FCF_ON_PRI_LIST)
		list_del_init(&new_fcf_pri->list);
	new_fcf_pri->fcf_rec.fcf_index = fcf_index;
	new_fcf_pri->fcf_rec.priority = new_fcf_record->fip_priority;
	if (list_empty(&phba->fcf.fcf_pri_list)) {
		list_add(&new_fcf_pri->list, &phba->fcf.fcf_pri_list);
		ret = lpfc_sli4_fcf_rr_index_set(phba,
				new_fcf_pri->fcf_rec.fcf_index);
		goto out;
	}

	last_index = find_first_bit(phba->fcf.fcf_rr_bmask,
				LPFC_SLI4_FCF_TBL_INDX_MAX);
	if (last_index >= LPFC_SLI4_FCF_TBL_INDX_MAX) {
		ret = 0; /* Empty rr list */
		goto out;
	}
	current_fcf_pri = phba->fcf.fcf_pri[last_index].fcf_rec.priority;
	if (new_fcf_pri->fcf_rec.priority <=  current_fcf_pri) {
		list_add(&new_fcf_pri->list, &phba->fcf.fcf_pri_list);
		if (new_fcf_pri->fcf_rec.priority <  current_fcf_pri) {
			memset(phba->fcf.fcf_rr_bmask, 0,
				sizeof(*phba->fcf.fcf_rr_bmask));
			/* fcfs_at_this_priority_level = 1; */
			phba->fcf.eligible_fcf_cnt = 1;
		} else
			/* fcfs_at_this_priority_level++; */
			phba->fcf.eligible_fcf_cnt++;
		ret = lpfc_sli4_fcf_rr_index_set(phba,
				new_fcf_pri->fcf_rec.fcf_index);
		goto out;
	}

	list_for_each_entry_safe(fcf_pri, next_fcf_pri,
				&phba->fcf.fcf_pri_list, list) {
		if (new_fcf_pri->fcf_rec.priority <=
				fcf_pri->fcf_rec.priority) {
			if (fcf_pri->list.prev == &phba->fcf.fcf_pri_list)
				list_add(&new_fcf_pri->list,
						&phba->fcf.fcf_pri_list);
			else
				list_add(&new_fcf_pri->list,
					 &((struct lpfc_fcf_pri *)
					fcf_pri->list.prev)->list);
			ret = 0;
			goto out;
		} else if (fcf_pri->list.next == &phba->fcf.fcf_pri_list
			|| new_fcf_pri->fcf_rec.priority <
				next_fcf_pri->fcf_rec.priority) {
			list_add(&new_fcf_pri->list, &fcf_pri->list);
			ret = 0;
			goto out;
		}
		if (new_fcf_pri->fcf_rec.priority > fcf_pri->fcf_rec.priority)
			continue;

	}
	ret = 1;
out:
	/* we use = instead of |= to clear the FLOGI_FAILED flag. */
	new_fcf_pri->fcf_rec.flag = LPFC_FCF_ON_PRI_LIST;
	spin_unlock_irq(&phba->hbalock);
	return ret;
}

/**
 * lpfc_mbx_cmpl_fcf_scan_read_fcf_rec - fcf scan read_fcf mbox cmpl handler.
 * @phba: pointer to lpfc hba data structure.
 * @mboxq: pointer to mailbox object.
 *
 * This function iterates through all the fcf records available in
 * HBA and chooses the optimal FCF record for discovery. After finding
 * the FCF for discovery it registers the FCF record and kicks start
 * discovery.
 * If FCF_IN_USE flag is set in currently used FCF, the routine tries to
 * use an FCF record which matches fabric name and mac address of the
 * currently used FCF record.
 * If the driver supports only one FCF, it will try to use the FCF record
 * used by BOOT_BIOS.
 */
void
lpfc_mbx_cmpl_fcf_scan_read_fcf_rec(struct lpfc_hba *phba, LPFC_MBOXQ_t *mboxq)
{
	struct fcf_record *new_fcf_record;
	uint32_t boot_flag, addr_mode;
	uint16_t fcf_index, next_fcf_index;
	struct lpfc_fcf_rec *fcf_rec = NULL;
	uint16_t vlan_id = LPFC_FCOE_NULL_VID;
	bool select_new_fcf;
	int rc;

	/* If there is pending FCoE event restart FCF table scan */
	if (lpfc_check_pending_fcoe_event(phba, LPFC_SKIP_UNREG_FCF)) {
		lpfc_sli4_mbox_cmd_free(phba, mboxq);
		return;
	}

	/* Parse the FCF record from the non-embedded mailbox command */
	new_fcf_record = lpfc_sli4_fcf_rec_mbox_parse(phba, mboxq,
						      &next_fcf_index);
	if (!new_fcf_record) {
		lpfc_printf_log(phba, KERN_ERR, LOG_FIP,
				"2765 Mailbox command READ_FCF_RECORD "
				"failed to retrieve a FCF record.\n");
		/* Let next new FCF event trigger fast failover */
		spin_lock_irq(&phba->hbalock);
		phba->hba_flag &= ~FCF_TS_INPROG;
		spin_unlock_irq(&phba->hbalock);
		lpfc_sli4_mbox_cmd_free(phba, mboxq);
		return;
	}

	/* Check the FCF record against the connection list */
	rc = lpfc_match_fcf_conn_list(phba, new_fcf_record, &boot_flag,
				      &addr_mode, &vlan_id);

	/* Log the FCF record information if turned on */
	lpfc_sli4_log_fcf_record_info(phba, new_fcf_record, vlan_id,
				      next_fcf_index);

	/*
	 * If the fcf record does not match with connect list entries
	 * read the next entry; otherwise, this is an eligible FCF
	 * record for roundrobin FCF failover.
	 */
	if (!rc) {
		lpfc_sli4_fcf_pri_list_del(phba,
					bf_get(lpfc_fcf_record_fcf_index,
					       new_fcf_record));
		lpfc_printf_log(phba, KERN_WARNING, LOG_FIP,
				"2781 FCF (x%x) failed connection "
				"list check: (x%x/x%x/%x)\n",
				bf_get(lpfc_fcf_record_fcf_index,
				       new_fcf_record),
				bf_get(lpfc_fcf_record_fcf_avail,
				       new_fcf_record),
				bf_get(lpfc_fcf_record_fcf_valid,
				       new_fcf_record),
				bf_get(lpfc_fcf_record_fcf_sol,
				       new_fcf_record));
		if ((phba->fcf.fcf_flag & FCF_IN_USE) &&
		    lpfc_sli4_fcf_record_match(phba, &phba->fcf.current_rec,
		    new_fcf_record, LPFC_FCOE_IGNORE_VID)) {
			if (bf_get(lpfc_fcf_record_fcf_index, new_fcf_record) !=
			    phba->fcf.current_rec.fcf_indx) {
				lpfc_printf_log(phba, KERN_ERR, LOG_FIP,
					"2862 FCF (x%x) matches property "
					"of in-use FCF (x%x)\n",
					bf_get(lpfc_fcf_record_fcf_index,
					       new_fcf_record),
					phba->fcf.current_rec.fcf_indx);
				goto read_next_fcf;
			}
			/*
			 * In case the current in-use FCF record becomes
			 * invalid/unavailable during FCF discovery that
			 * was not triggered by fast FCF failover process,
			 * treat it as fast FCF failover.
			 */
			if (!(phba->fcf.fcf_flag & FCF_REDISC_PEND) &&
			    !(phba->fcf.fcf_flag & FCF_REDISC_FOV)) {
				lpfc_printf_log(phba, KERN_WARNING, LOG_FIP,
						"2835 Invalid in-use FCF "
						"(x%x), enter FCF failover "
						"table scan.\n",
						phba->fcf.current_rec.fcf_indx);
				spin_lock_irq(&phba->hbalock);
				phba->fcf.fcf_flag |= FCF_REDISC_FOV;
				spin_unlock_irq(&phba->hbalock);
				lpfc_sli4_mbox_cmd_free(phba, mboxq);
				lpfc_sli4_fcf_scan_read_fcf_rec(phba,
						LPFC_FCOE_FCF_GET_FIRST);
				return;
			}
		}
		goto read_next_fcf;
	} else {
		fcf_index = bf_get(lpfc_fcf_record_fcf_index, new_fcf_record);
		rc = lpfc_sli4_fcf_pri_list_add(phba, fcf_index,
							new_fcf_record);
		if (rc)
			goto read_next_fcf;
	}

	/*
	 * If this is not the first FCF discovery of the HBA, use last
	 * FCF record for the discovery. The condition that a rescan
	 * matches the in-use FCF record: fabric name, switch name, mac
	 * address, and vlan_id.
	 */
	spin_lock_irq(&phba->hbalock);
	if (phba->fcf.fcf_flag & FCF_IN_USE) {
		if (phba->cfg_fcf_failover_policy == LPFC_FCF_FOV &&
			lpfc_sli4_fcf_record_match(phba, &phba->fcf.current_rec,
		    new_fcf_record, vlan_id)) {
			if (bf_get(lpfc_fcf_record_fcf_index, new_fcf_record) ==
			    phba->fcf.current_rec.fcf_indx) {
				phba->fcf.fcf_flag |= FCF_AVAILABLE;
				if (phba->fcf.fcf_flag & FCF_REDISC_PEND)
					/* Stop FCF redisc wait timer */
					__lpfc_sli4_stop_fcf_redisc_wait_timer(
									phba);
				else if (phba->fcf.fcf_flag & FCF_REDISC_FOV)
					/* Fast failover, mark completed */
					phba->fcf.fcf_flag &= ~FCF_REDISC_FOV;
				spin_unlock_irq(&phba->hbalock);
				lpfc_printf_log(phba, KERN_INFO, LOG_FIP,
						"2836 New FCF matches in-use "
						"FCF (x%x), port_state:x%x, "
						"fc_flag:x%x\n",
						phba->fcf.current_rec.fcf_indx,
						phba->pport->port_state,
						phba->pport->fc_flag);
				goto out;
			} else
				lpfc_printf_log(phba, KERN_ERR, LOG_FIP,
					"2863 New FCF (x%x) matches "
					"property of in-use FCF (x%x)\n",
					bf_get(lpfc_fcf_record_fcf_index,
					       new_fcf_record),
					phba->fcf.current_rec.fcf_indx);
		}
		/*
		 * Read next FCF record from HBA searching for the matching
		 * with in-use record only if not during the fast failover
		 * period. In case of fast failover period, it shall try to
		 * determine whether the FCF record just read should be the
		 * next candidate.
		 */
		if (!(phba->fcf.fcf_flag & FCF_REDISC_FOV)) {
			spin_unlock_irq(&phba->hbalock);
			goto read_next_fcf;
		}
	}
	/*
	 * Update on failover FCF record only if it's in FCF fast-failover
	 * period; otherwise, update on current FCF record.
	 */
	if (phba->fcf.fcf_flag & FCF_REDISC_FOV)
		fcf_rec = &phba->fcf.failover_rec;
	else
		fcf_rec = &phba->fcf.current_rec;

	if (phba->fcf.fcf_flag & FCF_AVAILABLE) {
		/*
		 * If the driver FCF record does not have boot flag
		 * set and new hba fcf record has boot flag set, use
		 * the new hba fcf record.
		 */
		if (boot_flag && !(fcf_rec->flag & BOOT_ENABLE)) {
			/* Choose this FCF record */
			lpfc_printf_log(phba, KERN_INFO, LOG_FIP,
					"2837 Update current FCF record "
					"(x%x) with new FCF record (x%x)\n",
					fcf_rec->fcf_indx,
					bf_get(lpfc_fcf_record_fcf_index,
					new_fcf_record));
			__lpfc_update_fcf_record(phba, fcf_rec, new_fcf_record,
					addr_mode, vlan_id, BOOT_ENABLE);
			spin_unlock_irq(&phba->hbalock);
			goto read_next_fcf;
		}
		/*
		 * If the driver FCF record has boot flag set and the
		 * new hba FCF record does not have boot flag, read
		 * the next FCF record.
		 */
		if (!boot_flag && (fcf_rec->flag & BOOT_ENABLE)) {
			spin_unlock_irq(&phba->hbalock);
			goto read_next_fcf;
		}
		/*
		 * If the new hba FCF record has lower priority value
		 * than the driver FCF record, use the new record.
		 */
		if (new_fcf_record->fip_priority < fcf_rec->priority) {
			/* Choose the new FCF record with lower priority */
			lpfc_printf_log(phba, KERN_INFO, LOG_FIP,
					"2838 Update current FCF record "
					"(x%x) with new FCF record (x%x)\n",
					fcf_rec->fcf_indx,
					bf_get(lpfc_fcf_record_fcf_index,
					       new_fcf_record));
			__lpfc_update_fcf_record(phba, fcf_rec, new_fcf_record,
					addr_mode, vlan_id, 0);
			/* Reset running random FCF selection count */
			phba->fcf.eligible_fcf_cnt = 1;
		} else if (new_fcf_record->fip_priority == fcf_rec->priority) {
			/* Update running random FCF selection count */
			phba->fcf.eligible_fcf_cnt++;
			select_new_fcf = lpfc_sli4_new_fcf_random_select(phba,
						phba->fcf.eligible_fcf_cnt);
			if (select_new_fcf) {
				lpfc_printf_log(phba, KERN_INFO, LOG_FIP,
					"2839 Update current FCF record "
					"(x%x) with new FCF record (x%x)\n",
					fcf_rec->fcf_indx,
					bf_get(lpfc_fcf_record_fcf_index,
					       new_fcf_record));
				/* Choose the new FCF by random selection */
				__lpfc_update_fcf_record(phba, fcf_rec,
							 new_fcf_record,
							 addr_mode, vlan_id, 0);
			}
		}
		spin_unlock_irq(&phba->hbalock);
		goto read_next_fcf;
	}
	/*
	 * This is the first suitable FCF record, choose this record for
	 * initial best-fit FCF.
	 */
	if (fcf_rec) {
		lpfc_printf_log(phba, KERN_INFO, LOG_FIP,
				"2840 Update initial FCF candidate "
				"with FCF (x%x)\n",
				bf_get(lpfc_fcf_record_fcf_index,
				       new_fcf_record));
		__lpfc_update_fcf_record(phba, fcf_rec, new_fcf_record,
					 addr_mode, vlan_id, (boot_flag ?
					 BOOT_ENABLE : 0));
		phba->fcf.fcf_flag |= FCF_AVAILABLE;
		/* Setup initial running random FCF selection count */
		phba->fcf.eligible_fcf_cnt = 1;
	}
	spin_unlock_irq(&phba->hbalock);
	goto read_next_fcf;

read_next_fcf:
	lpfc_sli4_mbox_cmd_free(phba, mboxq);
	if (next_fcf_index == LPFC_FCOE_FCF_NEXT_NONE || next_fcf_index == 0) {
		if (phba->fcf.fcf_flag & FCF_REDISC_FOV) {
			/*
			 * Case of FCF fast failover scan
			 */

			/*
			 * It has not found any suitable FCF record, cancel
			 * FCF scan inprogress, and do nothing
			 */
			if (!(phba->fcf.failover_rec.flag & RECORD_VALID)) {
				lpfc_printf_log(phba, KERN_WARNING, LOG_FIP,
					       "2782 No suitable FCF found: "
					       "(x%x/x%x)\n",
					       phba->fcoe_eventtag_at_fcf_scan,
					       bf_get(lpfc_fcf_record_fcf_index,
						      new_fcf_record));
				spin_lock_irq(&phba->hbalock);
				if (phba->hba_flag & HBA_DEVLOSS_TMO) {
					phba->hba_flag &= ~FCF_TS_INPROG;
					spin_unlock_irq(&phba->hbalock);
					/* Unregister in-use FCF and rescan */
					lpfc_printf_log(phba, KERN_INFO,
							LOG_FIP,
							"2864 On devloss tmo "
							"unreg in-use FCF and "
							"rescan FCF table\n");
					lpfc_unregister_fcf_rescan(phba);
					return;
				}
				/*
				 * Let next new FCF event trigger fast failover
				 */
				phba->hba_flag &= ~FCF_TS_INPROG;
				spin_unlock_irq(&phba->hbalock);
				return;
			}
			/*
			 * It has found a suitable FCF record that is not
			 * the same as in-use FCF record, unregister the
			 * in-use FCF record, replace the in-use FCF record
			 * with the new FCF record, mark FCF fast failover
			 * completed, and then start register the new FCF
			 * record.
			 */

			/* Unregister the current in-use FCF record */
			lpfc_unregister_fcf(phba);

			/* Replace in-use record with the new record */
			lpfc_printf_log(phba, KERN_INFO, LOG_FIP,
					"2842 Replace in-use FCF (x%x) "
					"with failover FCF (x%x)\n",
					phba->fcf.current_rec.fcf_indx,
					phba->fcf.failover_rec.fcf_indx);
			memcpy(&phba->fcf.current_rec,
			       &phba->fcf.failover_rec,
			       sizeof(struct lpfc_fcf_rec));
			/*
			 * Mark the fast FCF failover rediscovery completed
			 * and the start of the first round of the roundrobin
			 * FCF failover.
			 */
			spin_lock_irq(&phba->hbalock);
			phba->fcf.fcf_flag &= ~FCF_REDISC_FOV;
			spin_unlock_irq(&phba->hbalock);
			/* Register to the new FCF record */
			lpfc_register_fcf(phba);
		} else {
			/*
			 * In case of transaction period to fast FCF failover,
			 * do nothing when search to the end of the FCF table.
			 */
			if ((phba->fcf.fcf_flag & FCF_REDISC_EVT) ||
			    (phba->fcf.fcf_flag & FCF_REDISC_PEND))
				return;

			if (phba->cfg_fcf_failover_policy == LPFC_FCF_FOV &&
				phba->fcf.fcf_flag & FCF_IN_USE) {
				/*
				 * In case the current in-use FCF record no
				 * longer existed during FCF discovery that
				 * was not triggered by fast FCF failover
				 * process, treat it as fast FCF failover.
				 */
				lpfc_printf_log(phba, KERN_INFO, LOG_FIP,
						"2841 In-use FCF record (x%x) "
						"not reported, entering fast "
						"FCF failover mode scanning.\n",
						phba->fcf.current_rec.fcf_indx);
				spin_lock_irq(&phba->hbalock);
				phba->fcf.fcf_flag |= FCF_REDISC_FOV;
				spin_unlock_irq(&phba->hbalock);
				lpfc_sli4_fcf_scan_read_fcf_rec(phba,
						LPFC_FCOE_FCF_GET_FIRST);
				return;
			}
			/* Register to the new FCF record */
			lpfc_register_fcf(phba);
		}
	} else
		lpfc_sli4_fcf_scan_read_fcf_rec(phba, next_fcf_index);
	return;

out:
	lpfc_sli4_mbox_cmd_free(phba, mboxq);
	lpfc_register_fcf(phba);

	return;
}

/**
 * lpfc_mbx_cmpl_fcf_rr_read_fcf_rec - fcf roundrobin read_fcf mbox cmpl hdler
 * @phba: pointer to lpfc hba data structure.
 * @mboxq: pointer to mailbox object.
 *
 * This is the callback function for FLOGI failure roundrobin FCF failover
 * read FCF record mailbox command from the eligible FCF record bmask for
 * performing the failover. If the FCF read back is not valid/available, it
 * fails through to retrying FLOGI to the currently registered FCF again.
 * Otherwise, if the FCF read back is valid and available, it will set the
 * newly read FCF record to the failover FCF record, unregister currently
 * registered FCF record, copy the failover FCF record to the current
 * FCF record, and then register the current FCF record before proceeding
 * to trying FLOGI on the new failover FCF.
 */
void
lpfc_mbx_cmpl_fcf_rr_read_fcf_rec(struct lpfc_hba *phba, LPFC_MBOXQ_t *mboxq)
{
	struct fcf_record *new_fcf_record;
	uint32_t boot_flag, addr_mode;
	uint16_t next_fcf_index, fcf_index;
	uint16_t current_fcf_index;
	uint16_t vlan_id;
	int rc;

	/* If link state is not up, stop the roundrobin failover process */
	if (phba->link_state < LPFC_LINK_UP) {
		spin_lock_irq(&phba->hbalock);
		phba->fcf.fcf_flag &= ~FCF_DISCOVERY;
		phba->hba_flag &= ~FCF_RR_INPROG;
		spin_unlock_irq(&phba->hbalock);
		goto out;
	}

	/* Parse the FCF record from the non-embedded mailbox command */
	new_fcf_record = lpfc_sli4_fcf_rec_mbox_parse(phba, mboxq,
						      &next_fcf_index);
	if (!new_fcf_record) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_FIP,
				"2766 Mailbox command READ_FCF_RECORD "
				"failed to retrieve a FCF record. "
				"hba_flg x%x fcf_flg x%x\n", phba->hba_flag,
				phba->fcf.fcf_flag);
		lpfc_unregister_fcf_rescan(phba);
		goto out;
	}

	/* Get the needed parameters from FCF record */
	rc = lpfc_match_fcf_conn_list(phba, new_fcf_record, &boot_flag,
				      &addr_mode, &vlan_id);

	/* Log the FCF record information if turned on */
	lpfc_sli4_log_fcf_record_info(phba, new_fcf_record, vlan_id,
				      next_fcf_index);

	fcf_index = bf_get(lpfc_fcf_record_fcf_index, new_fcf_record);
	if (!rc) {
		lpfc_printf_log(phba, KERN_INFO, LOG_FIP,
				"2848 Remove ineligible FCF (x%x) from "
				"from roundrobin bmask\n", fcf_index);
		/* Clear roundrobin bmask bit for ineligible FCF */
		lpfc_sli4_fcf_rr_index_clear(phba, fcf_index);
		/* Perform next round of roundrobin FCF failover */
		fcf_index = lpfc_sli4_fcf_rr_next_index_get(phba);
		rc = lpfc_sli4_fcf_rr_next_proc(phba->pport, fcf_index);
		if (rc)
			goto out;
		goto error_out;
	}

	if (fcf_index == phba->fcf.current_rec.fcf_indx) {
		lpfc_printf_log(phba, KERN_INFO, LOG_FIP,
				"2760 Perform FLOGI roundrobin FCF failover: "
				"FCF (x%x) back to FCF (x%x)\n",
				phba->fcf.current_rec.fcf_indx, fcf_index);
		/* Wait 500 ms before retrying FLOGI to current FCF */
		msleep(500);
		lpfc_issue_init_vfi(phba->pport);
		goto out;
	}

	/* Upload new FCF record to the failover FCF record */
	lpfc_printf_log(phba, KERN_INFO, LOG_FIP,
			"2834 Update current FCF (x%x) with new FCF (x%x)\n",
			phba->fcf.failover_rec.fcf_indx, fcf_index);
	spin_lock_irq(&phba->hbalock);
	__lpfc_update_fcf_record(phba, &phba->fcf.failover_rec,
				 new_fcf_record, addr_mode, vlan_id,
				 (boot_flag ? BOOT_ENABLE : 0));
	spin_unlock_irq(&phba->hbalock);

	current_fcf_index = phba->fcf.current_rec.fcf_indx;

	/* Unregister the current in-use FCF record */
	lpfc_unregister_fcf(phba);

	/* Replace in-use record with the new record */
	memcpy(&phba->fcf.current_rec, &phba->fcf.failover_rec,
	       sizeof(struct lpfc_fcf_rec));

	lpfc_printf_log(phba, KERN_INFO, LOG_FIP,
			"2783 Perform FLOGI roundrobin FCF failover: FCF "
			"(x%x) to FCF (x%x)\n", current_fcf_index, fcf_index);

error_out:
	lpfc_register_fcf(phba);
out:
	lpfc_sli4_mbox_cmd_free(phba, mboxq);
}

/**
 * lpfc_mbx_cmpl_read_fcf_rec - read fcf completion handler.
 * @phba: pointer to lpfc hba data structure.
 * @mboxq: pointer to mailbox object.
 *
 * This is the callback function of read FCF record mailbox command for
 * updating the eligible FCF bmask for FLOGI failure roundrobin FCF
 * failover when a new FCF event happened. If the FCF read back is
 * valid/available and it passes the connection list check, it updates
 * the bmask for the eligible FCF record for roundrobin failover.
 */
void
lpfc_mbx_cmpl_read_fcf_rec(struct lpfc_hba *phba, LPFC_MBOXQ_t *mboxq)
{
	struct fcf_record *new_fcf_record;
	uint32_t boot_flag, addr_mode;
	uint16_t fcf_index, next_fcf_index;
	uint16_t vlan_id;
	int rc;

	/* If link state is not up, no need to proceed */
	if (phba->link_state < LPFC_LINK_UP)
		goto out;

	/* If FCF discovery period is over, no need to proceed */
	if (!(phba->fcf.fcf_flag & FCF_DISCOVERY))
		goto out;

	/* Parse the FCF record from the non-embedded mailbox command */
	new_fcf_record = lpfc_sli4_fcf_rec_mbox_parse(phba, mboxq,
						      &next_fcf_index);
	if (!new_fcf_record) {
		lpfc_printf_log(phba, KERN_INFO, LOG_FIP,
				"2767 Mailbox command READ_FCF_RECORD "
				"failed to retrieve a FCF record.\n");
		goto out;
	}

	/* Check the connection list for eligibility */
	rc = lpfc_match_fcf_conn_list(phba, new_fcf_record, &boot_flag,
				      &addr_mode, &vlan_id);

	/* Log the FCF record information if turned on */
	lpfc_sli4_log_fcf_record_info(phba, new_fcf_record, vlan_id,
				      next_fcf_index);

	if (!rc)
		goto out;

	/* Update the eligible FCF record index bmask */
	fcf_index = bf_get(lpfc_fcf_record_fcf_index, new_fcf_record);

	rc = lpfc_sli4_fcf_pri_list_add(phba, fcf_index, new_fcf_record);

out:
	lpfc_sli4_mbox_cmd_free(phba, mboxq);
}

/**
 * lpfc_init_vfi_cmpl - Completion handler for init_vfi mbox command.
 * @phba: pointer to lpfc hba data structure.
 * @mboxq: pointer to mailbox data structure.
 *
 * This function handles completion of init vfi mailbox command.
 */
static void
lpfc_init_vfi_cmpl(struct lpfc_hba *phba, LPFC_MBOXQ_t *mboxq)
{
	struct lpfc_vport *vport = mboxq->vport;

	/*
	 * VFI not supported on interface type 0, just do the flogi
	 * Also continue if the VFI is in use - just use the same one.
	 */
	if (mboxq->u.mb.mbxStatus &&
	    (bf_get(lpfc_sli_intf_if_type, &phba->sli4_hba.sli_intf) !=
			LPFC_SLI_INTF_IF_TYPE_0) &&
	    mboxq->u.mb.mbxStatus != MBX_VFI_IN_USE) {
		lpfc_printf_vlog(vport, KERN_ERR,
				LOG_MBOX,
				"2891 Init VFI mailbox failed 0x%x\n",
				mboxq->u.mb.mbxStatus);
		mempool_free(mboxq, phba->mbox_mem_pool);
		lpfc_vport_set_state(vport, FC_VPORT_FAILED);
		return;
	}

	lpfc_initial_flogi(vport);
	mempool_free(mboxq, phba->mbox_mem_pool);
	return;
}

/**
 * lpfc_issue_init_vfi - Issue init_vfi mailbox command.
 * @vport: pointer to lpfc_vport data structure.
 *
 * This function issue a init_vfi mailbox command to initialize the VFI and
 * VPI for the physical port.
 */
void
lpfc_issue_init_vfi(struct lpfc_vport *vport)
{
	LPFC_MBOXQ_t *mboxq;
	int rc;
	struct lpfc_hba *phba = vport->phba;

	mboxq = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!mboxq) {
		lpfc_printf_vlog(vport, KERN_ERR,
			LOG_MBOX, "2892 Failed to allocate "
			"init_vfi mailbox\n");
		return;
	}
	lpfc_init_vfi(mboxq, vport);
	mboxq->mbox_cmpl = lpfc_init_vfi_cmpl;
	rc = lpfc_sli_issue_mbox(phba, mboxq, MBX_NOWAIT);
	if (rc == MBX_NOT_FINISHED) {
		lpfc_printf_vlog(vport, KERN_ERR,
			LOG_MBOX, "2893 Failed to issue init_vfi mailbox\n");
		mempool_free(mboxq, vport->phba->mbox_mem_pool);
	}
}

/**
 * lpfc_init_vpi_cmpl - Completion handler for init_vpi mbox command.
 * @phba: pointer to lpfc hba data structure.
 * @mboxq: pointer to mailbox data structure.
 *
 * This function handles completion of init vpi mailbox command.
 */
void
lpfc_init_vpi_cmpl(struct lpfc_hba *phba, LPFC_MBOXQ_t *mboxq)
{
	struct lpfc_vport *vport = mboxq->vport;
	struct lpfc_nodelist *ndlp;
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);

	if (mboxq->u.mb.mbxStatus) {
		lpfc_printf_vlog(vport, KERN_ERR,
				LOG_MBOX,
				"2609 Init VPI mailbox failed 0x%x\n",
				mboxq->u.mb.mbxStatus);
		mempool_free(mboxq, phba->mbox_mem_pool);
		lpfc_vport_set_state(vport, FC_VPORT_FAILED);
		return;
	}
	spin_lock_irq(shost->host_lock);
	vport->fc_flag &= ~FC_VPORT_NEEDS_INIT_VPI;
	spin_unlock_irq(shost->host_lock);

	/* If this port is physical port or FDISC is done, do reg_vpi */
	if ((phba->pport == vport) || (vport->port_state == LPFC_FDISC)) {
			ndlp = lpfc_findnode_did(vport, Fabric_DID);
			if (!ndlp)
				lpfc_printf_vlog(vport, KERN_ERR,
					LOG_DISCOVERY,
					"2731 Cannot find fabric "
					"controller node\n");
			else
				lpfc_register_new_vport(phba, vport, ndlp);
			mempool_free(mboxq, phba->mbox_mem_pool);
			return;
	}

	if (phba->link_flag & LS_NPIV_FAB_SUPPORTED)
		lpfc_initial_fdisc(vport);
	else {
		lpfc_vport_set_state(vport, FC_VPORT_NO_FABRIC_SUPP);
		lpfc_printf_vlog(vport, KERN_ERR, LOG_ELS,
				 "2606 No NPIV Fabric support\n");
	}
	mempool_free(mboxq, phba->mbox_mem_pool);
	return;
}

/**
 * lpfc_issue_init_vpi - Issue init_vpi mailbox command.
 * @vport: pointer to lpfc_vport data structure.
 *
 * This function issue a init_vpi mailbox command to initialize
 * VPI for the vport.
 */
void
lpfc_issue_init_vpi(struct lpfc_vport *vport)
{
	LPFC_MBOXQ_t *mboxq;
	int rc, vpi;

	if ((vport->port_type != LPFC_PHYSICAL_PORT) && (!vport->vpi)) {
		vpi = lpfc_alloc_vpi(vport->phba);
		if (!vpi) {
			lpfc_printf_vlog(vport, KERN_ERR,
					 LOG_MBOX,
					 "3303 Failed to obtain vport vpi\n");
			lpfc_vport_set_state(vport, FC_VPORT_FAILED);
			return;
		}
		vport->vpi = vpi;
	}

	mboxq = mempool_alloc(vport->phba->mbox_mem_pool, GFP_KERNEL);
	if (!mboxq) {
		lpfc_printf_vlog(vport, KERN_ERR,
			LOG_MBOX, "2607 Failed to allocate "
			"init_vpi mailbox\n");
		return;
	}
	lpfc_init_vpi(vport->phba, mboxq, vport->vpi);
	mboxq->vport = vport;
	mboxq->mbox_cmpl = lpfc_init_vpi_cmpl;
	rc = lpfc_sli_issue_mbox(vport->phba, mboxq, MBX_NOWAIT);
	if (rc == MBX_NOT_FINISHED) {
		lpfc_printf_vlog(vport, KERN_ERR,
			LOG_MBOX, "2608 Failed to issue init_vpi mailbox\n");
		mempool_free(mboxq, vport->phba->mbox_mem_pool);
	}
}

/**
 * lpfc_start_fdiscs - send fdiscs for each vports on this port.
 * @phba: pointer to lpfc hba data structure.
 *
 * This function loops through the list of vports on the @phba and issues an
 * FDISC if possible.
 */
void
lpfc_start_fdiscs(struct lpfc_hba *phba)
{
	struct lpfc_vport **vports;
	int i;

	vports = lpfc_create_vport_work_array(phba);
	if (vports != NULL) {
		for (i = 0; i <= phba->max_vports && vports[i] != NULL; i++) {
			if (vports[i]->port_type == LPFC_PHYSICAL_PORT)
				continue;
			/* There are no vpi for this vport */
			if (vports[i]->vpi > phba->max_vpi) {
				lpfc_vport_set_state(vports[i],
						     FC_VPORT_FAILED);
				continue;
			}
			if (phba->fc_topology == LPFC_TOPOLOGY_LOOP) {
				lpfc_vport_set_state(vports[i],
						     FC_VPORT_LINKDOWN);
				continue;
			}
			if (vports[i]->fc_flag & FC_VPORT_NEEDS_INIT_VPI) {
				lpfc_issue_init_vpi(vports[i]);
				continue;
			}
			if (phba->link_flag & LS_NPIV_FAB_SUPPORTED)
				lpfc_initial_fdisc(vports[i]);
			else {
				lpfc_vport_set_state(vports[i],
						     FC_VPORT_NO_FABRIC_SUPP);
				lpfc_printf_vlog(vports[i], KERN_ERR,
						 LOG_ELS,
						 "0259 No NPIV "
						 "Fabric support\n");
			}
		}
	}
	lpfc_destroy_vport_work_array(phba, vports);
}

void
lpfc_mbx_cmpl_reg_vfi(struct lpfc_hba *phba, LPFC_MBOXQ_t *mboxq)
{
	struct lpfc_dmabuf *dmabuf = mboxq->context1;
	struct lpfc_vport *vport = mboxq->vport;
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);

	/*
	 * VFI not supported for interface type 0, so ignore any mailbox
	 * error (except VFI in use) and continue with the discovery.
	 */
	if (mboxq->u.mb.mbxStatus &&
	    (bf_get(lpfc_sli_intf_if_type, &phba->sli4_hba.sli_intf) !=
			LPFC_SLI_INTF_IF_TYPE_0) &&
	    mboxq->u.mb.mbxStatus != MBX_VFI_IN_USE) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_MBOX,
			 "2018 REG_VFI mbxStatus error x%x "
			 "HBA state x%x\n",
			 mboxq->u.mb.mbxStatus, vport->port_state);
		if (phba->fc_topology == LPFC_TOPOLOGY_LOOP) {
			/* FLOGI failed, use loop map to make discovery list */
			lpfc_disc_list_loopmap(vport);
			/* Start discovery */
			lpfc_disc_start(vport);
			goto out_free_mem;
		}
		lpfc_vport_set_state(vport, FC_VPORT_FAILED);
		goto out_free_mem;
	}

	/* If the VFI is already registered, there is nothing else to do
	 * Unless this was a VFI update and we are in PT2PT mode, then
	 * we should drop through to set the port state to ready.
	 */
	if (vport->fc_flag & FC_VFI_REGISTERED)
		if (!(phba->sli_rev == LPFC_SLI_REV4 &&
		      vport->fc_flag & FC_PT2PT))
			goto out_free_mem;

	/* The VPI is implicitly registered when the VFI is registered */
	spin_lock_irq(shost->host_lock);
	vport->vpi_state |= LPFC_VPI_REGISTERED;
	vport->fc_flag |= FC_VFI_REGISTERED;
	vport->fc_flag &= ~FC_VPORT_NEEDS_REG_VPI;
	vport->fc_flag &= ~FC_VPORT_NEEDS_INIT_VPI;
	spin_unlock_irq(shost->host_lock);

	/* In case SLI4 FC loopback test, we are ready */
	if ((phba->sli_rev == LPFC_SLI_REV4) &&
	    (phba->link_flag & LS_LOOPBACK_MODE)) {
		phba->link_state = LPFC_HBA_READY;
		goto out_free_mem;
	}

	lpfc_printf_vlog(vport, KERN_INFO, LOG_SLI,
			 "3313 cmpl reg vfi  port_state:%x fc_flag:%x myDid:%x "
			 "alpacnt:%d LinkState:%x topology:%x\n",
			 vport->port_state, vport->fc_flag, vport->fc_myDID,
			 vport->phba->alpa_map[0],
			 phba->link_state, phba->fc_topology);

	if (vport->port_state == LPFC_FABRIC_CFG_LINK) {
		/*
		 * For private loop or for NPort pt2pt,
		 * just start discovery and we are done.
		 */
		if ((vport->fc_flag & FC_PT2PT) ||
		    ((phba->fc_topology == LPFC_TOPOLOGY_LOOP) &&
		    !(vport->fc_flag & FC_PUBLIC_LOOP))) {

			/* Use loop map to make discovery list */
			lpfc_disc_list_loopmap(vport);
			/* Start discovery */
			if (vport->fc_flag & FC_PT2PT)
				vport->port_state = LPFC_VPORT_READY;
			else
				lpfc_disc_start(vport);
		} else {
			lpfc_start_fdiscs(phba);
			lpfc_do_scr_ns_plogi(phba, vport);
		}
	}

out_free_mem:
	mempool_free(mboxq, phba->mbox_mem_pool);
	if (dmabuf) {
		lpfc_mbuf_free(phba, dmabuf->virt, dmabuf->phys);
		kfree(dmabuf);
	}
	return;
}

static void
lpfc_mbx_cmpl_read_sparam(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmb)
{
	MAILBOX_t *mb = &pmb->u.mb;
	struct lpfc_dmabuf *mp = (struct lpfc_dmabuf *) pmb->context1;
	struct lpfc_vport  *vport = pmb->vport;
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct serv_parm *sp = &vport->fc_sparam;
	uint32_t ed_tov;

	/* Check for error */
	if (mb->mbxStatus) {
		/* READ_SPARAM mbox error <mbxStatus> state <hba_state> */
		lpfc_printf_vlog(vport, KERN_ERR, LOG_MBOX,
				 "0319 READ_SPARAM mbxStatus error x%x "
				 "hba state x%x>\n",
				 mb->mbxStatus, vport->port_state);
		lpfc_linkdown(phba);
		goto out;
	}

	memcpy((uint8_t *) &vport->fc_sparam, (uint8_t *) mp->virt,
	       sizeof (struct serv_parm));

	ed_tov = be32_to_cpu(sp->cmn.e_d_tov);
	if (sp->cmn.edtovResolution)	/* E_D_TOV ticks are in nanoseconds */
		ed_tov = (ed_tov + 999999) / 1000000;

	phba->fc_edtov = ed_tov;
	phba->fc_ratov = (2 * ed_tov) / 1000;
	if (phba->fc_ratov < FF_DEF_RATOV) {
		/* RA_TOV should be atleast 10sec for initial flogi */
		phba->fc_ratov = FF_DEF_RATOV;
	}

	lpfc_update_vport_wwn(vport);
	fc_host_port_name(shost) = wwn_to_u64(vport->fc_portname.u.wwn);
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
lpfc_mbx_process_link_up(struct lpfc_hba *phba, struct lpfc_mbx_read_top *la)
{
	struct lpfc_vport *vport = phba->pport;
	LPFC_MBOXQ_t *sparam_mbox, *cfglink_mbox = NULL;
	struct Scsi_Host *shost;
	int i;
	struct lpfc_dmabuf *mp;
	int rc;
	struct fcf_record *fcf_record;
	uint32_t fc_flags = 0;

	spin_lock_irq(&phba->hbalock);
	phba->fc_linkspeed = bf_get(lpfc_mbx_read_top_link_spd, la);

	if (!(phba->hba_flag & HBA_FCOE_MODE)) {
		switch (bf_get(lpfc_mbx_read_top_link_spd, la)) {
		case LPFC_LINK_SPEED_1GHZ:
		case LPFC_LINK_SPEED_2GHZ:
		case LPFC_LINK_SPEED_4GHZ:
		case LPFC_LINK_SPEED_8GHZ:
		case LPFC_LINK_SPEED_10GHZ:
		case LPFC_LINK_SPEED_16GHZ:
		case LPFC_LINK_SPEED_32GHZ:
			break;
		default:
			phba->fc_linkspeed = LPFC_LINK_SPEED_UNKNOWN;
			break;
		}
	}

	if (phba->fc_topology &&
	    phba->fc_topology != bf_get(lpfc_mbx_read_top_topology, la)) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_SLI,
				"3314 Toplogy changed was 0x%x is 0x%x\n",
				phba->fc_topology,
				bf_get(lpfc_mbx_read_top_topology, la));
		phba->fc_topology_changed = 1;
	}

	phba->fc_topology = bf_get(lpfc_mbx_read_top_topology, la);
	phba->link_flag &= ~LS_NPIV_FAB_SUPPORTED;

	shost = lpfc_shost_from_vport(vport);
	if (phba->fc_topology == LPFC_TOPOLOGY_LOOP) {
		phba->sli3_options &= ~LPFC_SLI3_NPIV_ENABLED;

		/* if npiv is enabled and this adapter supports npiv log
		 * a message that npiv is not supported in this topology
		 */
		if (phba->cfg_enable_npiv && phba->max_vpi)
			lpfc_printf_log(phba, KERN_ERR, LOG_LINK_EVENT,
				"1309 Link Up Event npiv not supported in loop "
				"topology\n");
				/* Get Loop Map information */
		if (bf_get(lpfc_mbx_read_top_il, la))
			fc_flags |= FC_LBIT;

		vport->fc_myDID = bf_get(lpfc_mbx_read_top_alpa_granted, la);
		i = la->lilpBde64.tus.f.bdeSize;

		if (i == 0) {
			phba->alpa_map[0] = 0;
		} else {
			if (vport->cfg_log_verbose & LOG_LINK_EVENT) {
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
							"1304 Link Up Event "
							"ALPA map Data: x%x "
							"x%x x%x x%x\n",
							un.pa.wd1, un.pa.wd2,
							un.pa.wd3, un.pa.wd4);
				}
			}
		}
	} else {
		if (!(phba->sli3_options & LPFC_SLI3_NPIV_ENABLED)) {
			if (phba->max_vpi && phba->cfg_enable_npiv &&
			   (phba->sli_rev >= LPFC_SLI_REV3))
				phba->sli3_options |= LPFC_SLI3_NPIV_ENABLED;
		}
		vport->fc_myDID = phba->fc_pref_DID;
		fc_flags |= FC_LBIT;
	}
	spin_unlock_irq(&phba->hbalock);

	if (fc_flags) {
		spin_lock_irq(shost->host_lock);
		vport->fc_flag |= fc_flags;
		spin_unlock_irq(shost->host_lock);
	}

	lpfc_linkup(phba);
	sparam_mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!sparam_mbox)
		goto out;

	rc = lpfc_read_sparam(phba, sparam_mbox, 0);
	if (rc) {
		mempool_free(sparam_mbox, phba->mbox_mem_pool);
		goto out;
	}
	sparam_mbox->vport = vport;
	sparam_mbox->mbox_cmpl = lpfc_mbx_cmpl_read_sparam;
	rc = lpfc_sli_issue_mbox(phba, sparam_mbox, MBX_NOWAIT);
	if (rc == MBX_NOT_FINISHED) {
		mp = (struct lpfc_dmabuf *) sparam_mbox->context1;
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		kfree(mp);
		mempool_free(sparam_mbox, phba->mbox_mem_pool);
		goto out;
	}

	if (!(phba->hba_flag & HBA_FCOE_MODE)) {
		cfglink_mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
		if (!cfglink_mbox)
			goto out;
		vport->port_state = LPFC_LOCAL_CFG_LINK;
		lpfc_config_link(phba, cfglink_mbox);
		cfglink_mbox->vport = vport;
		cfglink_mbox->mbox_cmpl = lpfc_mbx_cmpl_local_config_link;
		rc = lpfc_sli_issue_mbox(phba, cfglink_mbox, MBX_NOWAIT);
		if (rc == MBX_NOT_FINISHED) {
			mempool_free(cfglink_mbox, phba->mbox_mem_pool);
			goto out;
		}
	} else {
		vport->port_state = LPFC_VPORT_UNKNOWN;
		/*
		 * Add the driver's default FCF record at FCF index 0 now. This
		 * is phase 1 implementation that support FCF index 0 and driver
		 * defaults.
		 */
		if (!(phba->hba_flag & HBA_FIP_SUPPORT)) {
			fcf_record = kzalloc(sizeof(struct fcf_record),
					GFP_KERNEL);
			if (unlikely(!fcf_record)) {
				lpfc_printf_log(phba, KERN_ERR,
					LOG_MBOX | LOG_SLI,
					"2554 Could not allocate memory for "
					"fcf record\n");
				rc = -ENODEV;
				goto out;
			}

			lpfc_sli4_build_dflt_fcf_record(phba, fcf_record,
						LPFC_FCOE_FCF_DEF_INDEX);
			rc = lpfc_sli4_add_fcf_record(phba, fcf_record);
			if (unlikely(rc)) {
				lpfc_printf_log(phba, KERN_ERR,
					LOG_MBOX | LOG_SLI,
					"2013 Could not manually add FCF "
					"record 0, status %d\n", rc);
				rc = -ENODEV;
				kfree(fcf_record);
				goto out;
			}
			kfree(fcf_record);
		}
		/*
		 * The driver is expected to do FIP/FCF. Call the port
		 * and get the FCF Table.
		 */
		spin_lock_irq(&phba->hbalock);
		if (phba->hba_flag & FCF_TS_INPROG) {
			spin_unlock_irq(&phba->hbalock);
			return;
		}
		/* This is the initial FCF discovery scan */
		phba->fcf.fcf_flag |= FCF_INIT_DISC;
		spin_unlock_irq(&phba->hbalock);
		lpfc_printf_log(phba, KERN_INFO, LOG_FIP | LOG_DISCOVERY,
				"2778 Start FCF table scan at linkup\n");
		rc = lpfc_sli4_fcf_scan_read_fcf_rec(phba,
						     LPFC_FCOE_FCF_GET_FIRST);
		if (rc) {
			spin_lock_irq(&phba->hbalock);
			phba->fcf.fcf_flag &= ~FCF_INIT_DISC;
			spin_unlock_irq(&phba->hbalock);
			goto out;
		}
		/* Reset FCF roundrobin bmask for new discovery */
		lpfc_sli4_clear_fcf_rr_bmask(phba);
	}

	return;
out:
	lpfc_vport_set_state(vport, FC_VPORT_FAILED);
	lpfc_printf_vlog(vport, KERN_ERR, LOG_MBOX,
			 "0263 Discovery Mailbox error: state: 0x%x : %p %p\n",
			 vport->port_state, sparam_mbox, cfglink_mbox);
	lpfc_issue_clear_la(phba, vport);
	return;
}

static void
lpfc_enable_la(struct lpfc_hba *phba)
{
	uint32_t control;
	struct lpfc_sli *psli = &phba->sli;
	spin_lock_irq(&phba->hbalock);
	psli->sli_flag |= LPFC_PROCESS_LA;
	if (phba->sli_rev <= LPFC_SLI_REV3) {
		control = readl(phba->HCregaddr);
		control |= HC_LAINT_ENA;
		writel(control, phba->HCregaddr);
		readl(phba->HCregaddr); /* flush */
	}
	spin_unlock_irq(&phba->hbalock);
}

static void
lpfc_mbx_issue_link_down(struct lpfc_hba *phba)
{
	lpfc_linkdown(phba);
	lpfc_enable_la(phba);
	lpfc_unregister_unused_fcf(phba);
	/* turn on Link Attention interrupts - no CLEAR_LA needed */
}


/*
 * This routine handles processing a READ_TOPOLOGY mailbox
 * command upon completion. It is setup in the LPFC_MBOXQ
 * as the completion routine when the command is
 * handed off to the SLI layer. SLI4 only.
 */
void
lpfc_mbx_cmpl_read_topology(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmb)
{
	struct lpfc_vport *vport = pmb->vport;
	struct Scsi_Host  *shost = lpfc_shost_from_vport(vport);
	struct lpfc_mbx_read_top *la;
	struct lpfc_sli_ring *pring;
	MAILBOX_t *mb = &pmb->u.mb;
	struct lpfc_dmabuf *mp = (struct lpfc_dmabuf *) (pmb->context1);
	uint8_t attn_type;

	/* Unblock ELS traffic */
	pring = lpfc_phba_elsring(phba);
	pring->flag &= ~LPFC_STOP_IOCB_EVENT;

	/* Check for error */
	if (mb->mbxStatus) {
		lpfc_printf_log(phba, KERN_INFO, LOG_LINK_EVENT,
				"1307 READ_LA mbox error x%x state x%x\n",
				mb->mbxStatus, vport->port_state);
		lpfc_mbx_issue_link_down(phba);
		phba->link_state = LPFC_HBA_ERROR;
		goto lpfc_mbx_cmpl_read_topology_free_mbuf;
	}

	la = (struct lpfc_mbx_read_top *) &pmb->u.mb.un.varReadTop;
	attn_type = bf_get(lpfc_mbx_read_top_att_type, la);

	memcpy(&phba->alpa_map[0], mp->virt, 128);

	spin_lock_irq(shost->host_lock);
	if (bf_get(lpfc_mbx_read_top_pb, la))
		vport->fc_flag |= FC_BYPASSED_MODE;
	else
		vport->fc_flag &= ~FC_BYPASSED_MODE;
	spin_unlock_irq(shost->host_lock);

	if (phba->fc_eventTag <= la->eventTag) {
		phba->fc_stat.LinkMultiEvent++;
		if (attn_type == LPFC_ATT_LINK_UP)
			if (phba->fc_eventTag != 0)
				lpfc_linkdown(phba);
	}

	phba->fc_eventTag = la->eventTag;
	if (phba->sli_rev < LPFC_SLI_REV4) {
		spin_lock_irq(&phba->hbalock);
		if (bf_get(lpfc_mbx_read_top_mm, la))
			phba->sli.sli_flag |= LPFC_MENLO_MAINT;
		else
			phba->sli.sli_flag &= ~LPFC_MENLO_MAINT;
		spin_unlock_irq(&phba->hbalock);
	}

	phba->link_events++;
	if ((attn_type == LPFC_ATT_LINK_UP) &&
	    !(phba->sli.sli_flag & LPFC_MENLO_MAINT)) {
		phba->fc_stat.LinkUp++;
		if (phba->link_flag & LS_LOOPBACK_MODE) {
			lpfc_printf_log(phba, KERN_ERR, LOG_LINK_EVENT,
					"1306 Link Up Event in loop back mode "
					"x%x received Data: x%x x%x x%x x%x\n",
					la->eventTag, phba->fc_eventTag,
					bf_get(lpfc_mbx_read_top_alpa_granted,
					       la),
					bf_get(lpfc_mbx_read_top_link_spd, la),
					phba->alpa_map[0]);
		} else {
			lpfc_printf_log(phba, KERN_ERR, LOG_LINK_EVENT,
					"1303 Link Up Event x%x received "
					"Data: x%x x%x x%x x%x x%x x%x %d\n",
					la->eventTag, phba->fc_eventTag,
					bf_get(lpfc_mbx_read_top_alpa_granted,
					       la),
					bf_get(lpfc_mbx_read_top_link_spd, la),
					phba->alpa_map[0],
					bf_get(lpfc_mbx_read_top_mm, la),
					bf_get(lpfc_mbx_read_top_fa, la),
					phba->wait_4_mlo_maint_flg);
		}
		lpfc_mbx_process_link_up(phba, la);
	} else if (attn_type == LPFC_ATT_LINK_DOWN ||
		   attn_type == LPFC_ATT_UNEXP_WWPN) {
		phba->fc_stat.LinkDown++;
		if (phba->link_flag & LS_LOOPBACK_MODE)
			lpfc_printf_log(phba, KERN_ERR, LOG_LINK_EVENT,
				"1308 Link Down Event in loop back mode "
				"x%x received "
				"Data: x%x x%x x%x\n",
				la->eventTag, phba->fc_eventTag,
				phba->pport->port_state, vport->fc_flag);
		else if (attn_type == LPFC_ATT_UNEXP_WWPN)
			lpfc_printf_log(phba, KERN_ERR, LOG_LINK_EVENT,
				"1313 Link Down UNEXP WWPN Event x%x received "
				"Data: x%x x%x x%x x%x x%x\n",
				la->eventTag, phba->fc_eventTag,
				phba->pport->port_state, vport->fc_flag,
				bf_get(lpfc_mbx_read_top_mm, la),
				bf_get(lpfc_mbx_read_top_fa, la));
		else
			lpfc_printf_log(phba, KERN_ERR, LOG_LINK_EVENT,
				"1305 Link Down Event x%x received "
				"Data: x%x x%x x%x x%x x%x\n",
				la->eventTag, phba->fc_eventTag,
				phba->pport->port_state, vport->fc_flag,
				bf_get(lpfc_mbx_read_top_mm, la),
				bf_get(lpfc_mbx_read_top_fa, la));
		lpfc_mbx_issue_link_down(phba);
	}
	if (phba->sli.sli_flag & LPFC_MENLO_MAINT &&
	    attn_type == LPFC_ATT_LINK_UP) {
		if (phba->link_state != LPFC_LINK_DOWN) {
			phba->fc_stat.LinkDown++;
			lpfc_printf_log(phba, KERN_ERR, LOG_LINK_EVENT,
				"1312 Link Down Event x%x received "
				"Data: x%x x%x x%x\n",
				la->eventTag, phba->fc_eventTag,
				phba->pport->port_state, vport->fc_flag);
			lpfc_mbx_issue_link_down(phba);
		} else
			lpfc_enable_la(phba);

		lpfc_printf_log(phba, KERN_ERR, LOG_LINK_EVENT,
				"1310 Menlo Maint Mode Link up Event x%x rcvd "
				"Data: x%x x%x x%x\n",
				la->eventTag, phba->fc_eventTag,
				phba->pport->port_state, vport->fc_flag);
		/*
		 * The cmnd that triggered this will be waiting for this
		 * signal.
		 */
		/* WAKEUP for MENLO_SET_MODE or MENLO_RESET command. */
		if (phba->wait_4_mlo_maint_flg) {
			phba->wait_4_mlo_maint_flg = 0;
			wake_up_interruptible(&phba->wait_4_mlo_m_q);
		}
	}

	if ((phba->sli_rev < LPFC_SLI_REV4) &&
	    bf_get(lpfc_mbx_read_top_fa, la)) {
		if (phba->sli.sli_flag & LPFC_MENLO_MAINT)
			lpfc_issue_clear_la(phba, vport);
		lpfc_printf_log(phba, KERN_INFO, LOG_LINK_EVENT,
				"1311 fa %d\n",
				bf_get(lpfc_mbx_read_top_fa, la));
	}

lpfc_mbx_cmpl_read_topology_free_mbuf:
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
	struct Scsi_Host  *shost = lpfc_shost_from_vport(vport);

	pmb->context1 = NULL;
	pmb->context2 = NULL;

	lpfc_printf_vlog(vport, KERN_INFO, LOG_SLI,
			 "0002 rpi:%x DID:%x flg:%x %d map:%x %p\n",
			 ndlp->nlp_rpi, ndlp->nlp_DID, ndlp->nlp_flag,
			 kref_read(&ndlp->kref),
			 ndlp->nlp_usg_map, ndlp);
	if (ndlp->nlp_flag & NLP_REG_LOGIN_SEND)
		ndlp->nlp_flag &= ~NLP_REG_LOGIN_SEND;

	if (ndlp->nlp_flag & NLP_IGNR_REG_CMPL ||
	    ndlp->nlp_state != NLP_STE_REG_LOGIN_ISSUE) {
		/* We rcvd a rscn after issuing this
		 * mbox reg login, we may have cycled
		 * back through the state and be
		 * back at reg login state so this
		 * mbox needs to be ignored becase
		 * there is another reg login in
		 * process.
		 */
		spin_lock_irq(shost->host_lock);
		ndlp->nlp_flag &= ~NLP_IGNR_REG_CMPL;
		spin_unlock_irq(shost->host_lock);

		/*
		 * We cannot leave the RPI registered because
		 * if we go thru discovery again for this ndlp
		 * a subsequent REG_RPI will fail.
		 */
		ndlp->nlp_flag |= NLP_RPI_REGISTERED;
		lpfc_unreg_rpi(vport, ndlp);
	}

	/* Call state machine */
	lpfc_disc_state_machine(vport, ndlp, pmb, NLP_EVT_CMPL_REG_LOGIN);

	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
	mempool_free(pmb, phba->mbox_mem_pool);
	/* decrement the node reference count held for this callback
	 * function.
	 */
	lpfc_nlp_put(ndlp);

	return;
}

static void
lpfc_mbx_cmpl_unreg_vpi(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmb)
{
	MAILBOX_t *mb = &pmb->u.mb;
	struct lpfc_vport *vport = pmb->vport;
	struct Scsi_Host  *shost = lpfc_shost_from_vport(vport);

	switch (mb->mbxStatus) {
	case 0x0011:
	case 0x0020:
		lpfc_printf_vlog(vport, KERN_INFO, LOG_NODE,
				 "0911 cmpl_unreg_vpi, mb status = 0x%x\n",
				 mb->mbxStatus);
		break;
	/* If VPI is busy, reset the HBA */
	case 0x9700:
		lpfc_printf_vlog(vport, KERN_ERR, LOG_NODE,
			"2798 Unreg_vpi failed vpi 0x%x, mb status = 0x%x\n",
			vport->vpi, mb->mbxStatus);
		if (!(phba->pport->load_flag & FC_UNLOADING))
			lpfc_workq_post_event(phba, NULL, NULL,
				LPFC_EVT_RESET_HBA);
	}
	spin_lock_irq(shost->host_lock);
	vport->vpi_state &= ~LPFC_VPI_REGISTERED;
	vport->fc_flag |= FC_VPORT_NEEDS_REG_VPI;
	spin_unlock_irq(shost->host_lock);
	vport->unreg_vpi_cmpl = VPORT_OK;
	mempool_free(pmb, phba->mbox_mem_pool);
	lpfc_cleanup_vports_rrqs(vport, NULL);
	/*
	 * This shost reference might have been taken at the beginning of
	 * lpfc_vport_delete()
	 */
	if ((vport->load_flag & FC_UNLOADING) && (vport != phba->pport))
		scsi_host_put(shost);
}

int
lpfc_mbx_unreg_vpi(struct lpfc_vport *vport)
{
	struct lpfc_hba  *phba = vport->phba;
	LPFC_MBOXQ_t *mbox;
	int rc;

	mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!mbox)
		return 1;

	lpfc_unreg_vpi(phba, vport->vpi, mbox);
	mbox->vport = vport;
	mbox->mbox_cmpl = lpfc_mbx_cmpl_unreg_vpi;
	rc = lpfc_sli_issue_mbox(phba, mbox, MBX_NOWAIT);
	if (rc == MBX_NOT_FINISHED) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_MBOX | LOG_VPORT,
				 "1800 Could not issue unreg_vpi\n");
		mempool_free(mbox, phba->mbox_mem_pool);
		vport->unreg_vpi_cmpl = VPORT_ERROR;
		return rc;
	}
	return 0;
}

static void
lpfc_mbx_cmpl_reg_vpi(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmb)
{
	struct lpfc_vport *vport = pmb->vport;
	struct Scsi_Host  *shost = lpfc_shost_from_vport(vport);
	MAILBOX_t *mb = &pmb->u.mb;

	switch (mb->mbxStatus) {
	case 0x0011:
	case 0x9601:
	case 0x9602:
		lpfc_printf_vlog(vport, KERN_INFO, LOG_NODE,
				 "0912 cmpl_reg_vpi, mb status = 0x%x\n",
				 mb->mbxStatus);
		lpfc_vport_set_state(vport, FC_VPORT_FAILED);
		spin_lock_irq(shost->host_lock);
		vport->fc_flag &= ~(FC_FABRIC | FC_PUBLIC_LOOP);
		spin_unlock_irq(shost->host_lock);
		vport->fc_myDID = 0;

		if ((phba->cfg_enable_fc4_type == LPFC_ENABLE_BOTH) ||
		    (phba->cfg_enable_fc4_type == LPFC_ENABLE_NVME)) {
			if (phba->nvmet_support)
				lpfc_nvmet_update_targetport(phba);
			else
				lpfc_nvme_update_localport(vport);
		}
		goto out;
	}

	spin_lock_irq(shost->host_lock);
	vport->vpi_state |= LPFC_VPI_REGISTERED;
	vport->fc_flag &= ~FC_VPORT_NEEDS_REG_VPI;
	spin_unlock_irq(shost->host_lock);
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

/**
 * lpfc_create_static_vport - Read HBA config region to create static vports.
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine issue a DUMP mailbox command for config region 22 to get
 * the list of static vports to be created. The function create vports
 * based on the information returned from the HBA.
 **/
void
lpfc_create_static_vport(struct lpfc_hba *phba)
{
	LPFC_MBOXQ_t *pmb = NULL;
	MAILBOX_t *mb;
	struct static_vport_info *vport_info;
	int mbx_wait_rc = 0, i;
	struct fc_vport_identifiers vport_id;
	struct fc_vport *new_fc_vport;
	struct Scsi_Host *shost;
	struct lpfc_vport *vport;
	uint16_t offset = 0;
	uint8_t *vport_buff;
	struct lpfc_dmabuf *mp;
	uint32_t byte_count = 0;

	pmb = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!pmb) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0542 lpfc_create_static_vport failed to"
				" allocate mailbox memory\n");
		return;
	}
	memset(pmb, 0, sizeof(LPFC_MBOXQ_t));
	mb = &pmb->u.mb;

	vport_info = kzalloc(sizeof(struct static_vport_info), GFP_KERNEL);
	if (!vport_info) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0543 lpfc_create_static_vport failed to"
				" allocate vport_info\n");
		mempool_free(pmb, phba->mbox_mem_pool);
		return;
	}

	vport_buff = (uint8_t *) vport_info;
	do {
		/* free dma buffer from previous round */
		if (pmb->context1) {
			mp = (struct lpfc_dmabuf *)pmb->context1;
			lpfc_mbuf_free(phba, mp->virt, mp->phys);
			kfree(mp);
		}
		if (lpfc_dump_static_vport(phba, pmb, offset))
			goto out;

		pmb->vport = phba->pport;
		mbx_wait_rc = lpfc_sli_issue_mbox_wait(phba, pmb,
							LPFC_MBOX_TMO);

		if ((mbx_wait_rc != MBX_SUCCESS) || mb->mbxStatus) {
			lpfc_printf_log(phba, KERN_WARNING, LOG_INIT,
				"0544 lpfc_create_static_vport failed to"
				" issue dump mailbox command ret 0x%x "
				"status 0x%x\n",
				mbx_wait_rc, mb->mbxStatus);
			goto out;
		}

		if (phba->sli_rev == LPFC_SLI_REV4) {
			byte_count = pmb->u.mqe.un.mb_words[5];
			mp = (struct lpfc_dmabuf *)pmb->context1;
			if (byte_count > sizeof(struct static_vport_info) -
					offset)
				byte_count = sizeof(struct static_vport_info)
					- offset;
			memcpy(vport_buff + offset, mp->virt, byte_count);
			offset += byte_count;
		} else {
			if (mb->un.varDmp.word_cnt >
				sizeof(struct static_vport_info) - offset)
				mb->un.varDmp.word_cnt =
					sizeof(struct static_vport_info)
						- offset;
			byte_count = mb->un.varDmp.word_cnt;
			lpfc_sli_pcimem_bcopy(((uint8_t *)mb) + DMP_RSP_OFFSET,
				vport_buff + offset,
				byte_count);

			offset += byte_count;
		}

	} while (byte_count &&
		offset < sizeof(struct static_vport_info));


	if ((le32_to_cpu(vport_info->signature) != VPORT_INFO_SIG) ||
		((le32_to_cpu(vport_info->rev) & VPORT_INFO_REV_MASK)
			!= VPORT_INFO_REV)) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
			"0545 lpfc_create_static_vport bad"
			" information header 0x%x 0x%x\n",
			le32_to_cpu(vport_info->signature),
			le32_to_cpu(vport_info->rev) & VPORT_INFO_REV_MASK);

		goto out;
	}

	shost = lpfc_shost_from_vport(phba->pport);

	for (i = 0; i < MAX_STATIC_VPORT_COUNT; i++) {
		memset(&vport_id, 0, sizeof(vport_id));
		vport_id.port_name = wwn_to_u64(vport_info->vport_list[i].wwpn);
		vport_id.node_name = wwn_to_u64(vport_info->vport_list[i].wwnn);
		if (!vport_id.port_name || !vport_id.node_name)
			continue;

		vport_id.roles = FC_PORT_ROLE_FCP_INITIATOR;
		vport_id.vport_type = FC_PORTTYPE_NPIV;
		vport_id.disable = false;
		new_fc_vport = fc_vport_create(shost, 0, &vport_id);

		if (!new_fc_vport) {
			lpfc_printf_log(phba, KERN_WARNING, LOG_INIT,
				"0546 lpfc_create_static_vport failed to"
				" create vport\n");
			continue;
		}

		vport = *(struct lpfc_vport **)new_fc_vport->dd_data;
		vport->vport_flag |= STATIC_VPORT;
	}

out:
	kfree(vport_info);
	if (mbx_wait_rc != MBX_TIMEOUT) {
		if (pmb->context1) {
			mp = (struct lpfc_dmabuf *)pmb->context1;
			lpfc_mbuf_free(phba, mp->virt, mp->phys);
			kfree(mp);
		}
		mempool_free(pmb, phba->mbox_mem_pool);
	}

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
	MAILBOX_t *mb = &pmb->u.mb;
	struct lpfc_dmabuf *mp = (struct lpfc_dmabuf *) (pmb->context1);
	struct lpfc_nodelist *ndlp;
	struct Scsi_Host *shost;

	ndlp = (struct lpfc_nodelist *) pmb->context2;
	pmb->context1 = NULL;
	pmb->context2 = NULL;

	if (mb->mbxStatus) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_MBOX,
				 "0258 Register Fabric login error: 0x%x\n",
				 mb->mbxStatus);
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		kfree(mp);
		mempool_free(pmb, phba->mbox_mem_pool);

		if (phba->fc_topology == LPFC_TOPOLOGY_LOOP) {
			/* FLOGI failed, use loop map to make discovery list */
			lpfc_disc_list_loopmap(vport);

			/* Start discovery */
			lpfc_disc_start(vport);
			/* Decrement the reference count to ndlp after the
			 * reference to the ndlp are done.
			 */
			lpfc_nlp_put(ndlp);
			return;
		}

		lpfc_vport_set_state(vport, FC_VPORT_FAILED);
		/* Decrement the reference count to ndlp after the reference
		 * to the ndlp are done.
		 */
		lpfc_nlp_put(ndlp);
		return;
	}

	if (phba->sli_rev < LPFC_SLI_REV4)
		ndlp->nlp_rpi = mb->un.varWords[0];
	ndlp->nlp_flag |= NLP_RPI_REGISTERED;
	ndlp->nlp_type |= NLP_FABRIC;
	lpfc_nlp_set_state(vport, ndlp, NLP_STE_UNMAPPED_NODE);

	if (vport->port_state == LPFC_FABRIC_CFG_LINK) {
		/* when physical port receive logo donot start
		 * vport discovery */
		if (!(vport->fc_flag & FC_LOGO_RCVD_DID_CHNG))
			lpfc_start_fdiscs(phba);
		else {
			shost = lpfc_shost_from_vport(vport);
			spin_lock_irq(shost->host_lock);
			vport->fc_flag &= ~FC_LOGO_RCVD_DID_CHNG ;
			spin_unlock_irq(shost->host_lock);
		}
		lpfc_do_scr_ns_plogi(phba, vport);
	}

	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
	mempool_free(pmb, phba->mbox_mem_pool);

	/* Drop the reference count from the mbox at the end after
	 * all the current reference to the ndlp have been done.
	 */
	lpfc_nlp_put(ndlp);
	return;
}

 /*
  * This routine will issue a GID_FT for each FC4 Type supported
  * by the driver. ALL GID_FTs must complete before discovery is started.
  */
int
lpfc_issue_gidft(struct lpfc_vport *vport)
{
	struct lpfc_hba *phba = vport->phba;

	/* Good status, issue CT Request to NameServer */
	if ((phba->cfg_enable_fc4_type == LPFC_ENABLE_BOTH) ||
	    (phba->cfg_enable_fc4_type == LPFC_ENABLE_FCP)) {
		if (lpfc_ns_cmd(vport, SLI_CTNS_GID_FT, 0, SLI_CTPT_FCP)) {
			/* Cannot issue NameServer FCP Query, so finish up
			 * discovery
			 */
			lpfc_printf_vlog(vport, KERN_ERR, LOG_SLI,
					 "0604 %s FC TYPE %x %s\n",
					 "Failed to issue GID_FT to ",
					 FC_TYPE_FCP,
					 "Finishing discovery.");
			return 0;
		}
		vport->gidft_inp++;
	}

	if ((phba->cfg_enable_fc4_type == LPFC_ENABLE_BOTH) ||
	    (phba->cfg_enable_fc4_type == LPFC_ENABLE_NVME)) {
		if (lpfc_ns_cmd(vport, SLI_CTNS_GID_FT, 0, SLI_CTPT_NVME)) {
			/* Cannot issue NameServer NVME Query, so finish up
			 * discovery
			 */
			lpfc_printf_vlog(vport, KERN_ERR, LOG_SLI,
					 "0605 %s FC_TYPE %x %s %d\n",
					 "Failed to issue GID_FT to ",
					 FC_TYPE_NVME,
					 "Finishing discovery: gidftinp ",
					 vport->gidft_inp);
			if (vport->gidft_inp == 0)
				return 0;
		} else
			vport->gidft_inp++;
	}
	return vport->gidft_inp;
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
	MAILBOX_t *mb = &pmb->u.mb;
	struct lpfc_dmabuf *mp = (struct lpfc_dmabuf *) (pmb->context1);
	struct lpfc_nodelist *ndlp = (struct lpfc_nodelist *) pmb->context2;
	struct lpfc_vport *vport = pmb->vport;

	pmb->context1 = NULL;
	pmb->context2 = NULL;
	vport->gidft_inp = 0;

	if (mb->mbxStatus) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_ELS,
				 "0260 Register NameServer error: 0x%x\n",
				 mb->mbxStatus);

out:
		/* decrement the node reference count held for this
		 * callback function.
		 */
		lpfc_nlp_put(ndlp);
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		kfree(mp);
		mempool_free(pmb, phba->mbox_mem_pool);

		/* If no other thread is using the ndlp, free it */
		lpfc_nlp_not_used(ndlp);

		if (phba->fc_topology == LPFC_TOPOLOGY_LOOP) {
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
		return;
	}

	if (phba->sli_rev < LPFC_SLI_REV4)
		ndlp->nlp_rpi = mb->un.varWords[0];
	ndlp->nlp_flag |= NLP_RPI_REGISTERED;
	ndlp->nlp_type |= NLP_FABRIC;
	lpfc_nlp_set_state(vport, ndlp, NLP_STE_UNMAPPED_NODE);
	lpfc_printf_vlog(vport, KERN_INFO, LOG_SLI,
			 "0003 rpi:%x DID:%x flg:%x %d map%x %p\n",
			 ndlp->nlp_rpi, ndlp->nlp_DID, ndlp->nlp_flag,
			 kref_read(&ndlp->kref),
			 ndlp->nlp_usg_map, ndlp);

	if (vport->port_state < LPFC_VPORT_READY) {
		/* Link up discovery requires Fabric registration. */
		lpfc_ns_cmd(vport, SLI_CTNS_RNN_ID, 0, 0);
		lpfc_ns_cmd(vport, SLI_CTNS_RSNN_NN, 0, 0);
		lpfc_ns_cmd(vport, SLI_CTNS_RSPN_ID, 0, 0);
		lpfc_ns_cmd(vport, SLI_CTNS_RFT_ID, 0, 0);

		if ((phba->cfg_enable_fc4_type == LPFC_ENABLE_BOTH) ||
		    (phba->cfg_enable_fc4_type == LPFC_ENABLE_FCP))
			lpfc_ns_cmd(vport, SLI_CTNS_RFF_ID, 0, FC_TYPE_FCP);

		if ((phba->cfg_enable_fc4_type == LPFC_ENABLE_BOTH) ||
		    (phba->cfg_enable_fc4_type == LPFC_ENABLE_NVME))
			lpfc_ns_cmd(vport, SLI_CTNS_RFF_ID, 0,
				    FC_TYPE_NVME);

		/* Issue SCR just before NameServer GID_FT Query */
		lpfc_issue_els_scr(vport, SCR_DID, 0);
	}

	vport->fc_ns_retry = 0;
	if (lpfc_issue_gidft(vport) == 0)
		goto out;

	/*
	 * At this point in time we may need to wait for multiple
	 * SLI_CTNS_GID_FT CT commands to complete before we start discovery.
	 *
	 * decrement the node reference count held for this
	 * callback function.
	 */
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

	if (phba->cfg_enable_fc4_type == LPFC_ENABLE_NVME)
		return;

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
	rport = ndlp->rport;
	if (rport) {
		rdata = rport->dd_data;
		/* break the link before dropping the ref */
		ndlp->rport = NULL;
		if (rdata) {
			if (rdata->pnode == ndlp)
				lpfc_nlp_put(ndlp);
			rdata->pnode = NULL;
		}
		/* drop reference for earlier registeration */
		put_device(&rport->dev);
	}

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_RPORT,
		"rport add:       did:x%x flg:x%x type x%x",
		ndlp->nlp_DID, ndlp->nlp_flag, ndlp->nlp_type);

	/* Don't add the remote port if unloading. */
	if (vport->load_flag & FC_UNLOADING)
		return;

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

	lpfc_printf_vlog(ndlp->vport, KERN_INFO, LOG_NODE,
			 "3183 rport register x%06x, rport %p role x%x\n",
			 ndlp->nlp_DID, rport, rport_ids.roles);

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
	struct lpfc_vport *vport = ndlp->vport;
	struct lpfc_hba  *phba = vport->phba;

	if (phba->cfg_enable_fc4_type == LPFC_ENABLE_NVME)
		return;

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_RPORT,
		"rport delete:    did:x%x flg:x%x type x%x",
		ndlp->nlp_DID, ndlp->nlp_flag, ndlp->nlp_type);

	lpfc_printf_vlog(vport, KERN_INFO, LOG_NODE,
			 "3184 rport unregister x%06x, rport %p\n",
			 ndlp->nlp_DID, rport);

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
		if (vport->fc_npr_cnt == 0 && count == -1)
			vport->fc_npr_cnt = 0;
		else
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
		ndlp->nlp_flag &= ~NLP_NODEV_REMOVE;
		ndlp->nlp_type |= NLP_FC_NODE;
	}
	if (new_state == NLP_STE_MAPPED_NODE)
		ndlp->nlp_flag &= ~NLP_NODEV_REMOVE;
	if (new_state == NLP_STE_NPR_NODE)
		ndlp->nlp_flag &= ~NLP_RCV_PLOGI;

	/* FCP and NVME Transport interface */
	if ((old_state == NLP_STE_MAPPED_NODE ||
	     old_state == NLP_STE_UNMAPPED_NODE)) {
		if (ndlp->rport) {
			vport->phba->nport_event_cnt++;
			lpfc_unregister_remote_port(ndlp);
		}

		if (ndlp->nlp_fc4_type & NLP_FC4_NVME) {
			vport->phba->nport_event_cnt++;
			if (vport->phba->nvmet_support == 0)
				/* Start devloss */
				lpfc_nvme_unregister_port(vport, ndlp);
			else
				/* NVMET has no upcall. */
				lpfc_nlp_put(ndlp);
		}
	}

	/* FCP and NVME Transport interfaces */

	if (new_state ==  NLP_STE_MAPPED_NODE ||
	    new_state == NLP_STE_UNMAPPED_NODE) {
		if (ndlp->nlp_fc4_type & NLP_FC4_FCP ||
		    ndlp->nlp_DID == Fabric_DID ||
		    ndlp->nlp_DID == NameServer_DID ||
		    ndlp->nlp_DID == FDMI_DID) {
			vport->phba->nport_event_cnt++;
			/*
			 * Tell the fc transport about the port, if we haven't
			 * already. If we have, and it's a scsi entity, be
			 */
			lpfc_register_remote_port(vport, ndlp);
		}
		/* Notify the NVME transport of this new rport. */
		if (vport->phba->sli_rev >= LPFC_SLI_REV4 &&
		    ndlp->nlp_fc4_type & NLP_FC4_NVME) {
			if (vport->phba->nvmet_support == 0) {
				/* Register this rport with the transport.
				 * Initiators take the NDLP ref count in
				 * the register.
				 */
				vport->phba->nport_event_cnt++;
				lpfc_nvme_register_port(vport, ndlp);
			} else {
				/* Just take an NDLP ref count since the
				 * target does not register rports.
				 */
				lpfc_nlp_get(ndlp);
			}
		}
	}

	if ((new_state ==  NLP_STE_MAPPED_NODE) &&
		(vport->stat_data_enabled)) {
		/*
		 * A new target is discovered, if there is no buffer for
		 * statistical data collection allocate buffer.
		 */
		ndlp->lat_data = kcalloc(LPFC_MAX_BUCKET_COUNT,
					 sizeof(struct lpfc_scsicmd_bkt),
					 GFP_KERNEL);

		if (!ndlp->lat_data)
			lpfc_printf_vlog(vport, KERN_ERR, LOG_NODE,
				"0286 lpfc_nlp_state_cleanup failed to "
				"allocate statistical data buffer DID "
				"0x%x\n", ndlp->nlp_DID);
	}
	/*
	 * If the node just added to Mapped list was an FCP target,
	 * but the remote port registration failed or assigned a target
	 * id outside the presentable range - move the node to the
	 * Unmapped List.
	 */
	if ((new_state == NLP_STE_MAPPED_NODE) &&
	    (ndlp->nlp_type & NLP_FCP_TARGET) &&
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
		[NLP_STE_LOGO_ISSUE] = "LOGO",
		[NLP_STE_UNMAPPED_NODE] = "UNMAPPED",
		[NLP_STE_MAPPED_NODE] = "MAPPED",
		[NLP_STE_NPR_NODE] = "NPR",
	};

	if (state < NLP_STE_MAX_STATE && states[state])
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

	lpfc_printf_vlog(vport, KERN_INFO, LOG_NODE,
			 "0904 NPort state transition x%06x, %s -> %s\n",
			 ndlp->nlp_DID,
			 lpfc_nlp_state_name(name1, sizeof(name1), old_state),
			 lpfc_nlp_state_name(name2, sizeof(name2), state));

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_NODE,
		"node statechg    did:x%x old:%d ste:%d",
		ndlp->nlp_DID, old_state, state);

	if (old_state == NLP_STE_NPR_NODE &&
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
lpfc_enqueue_node(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);

	if (list_empty(&ndlp->nlp_listp)) {
		spin_lock_irq(shost->host_lock);
		list_add_tail(&ndlp->nlp_listp, &vport->fc_nodes);
		spin_unlock_irq(shost->host_lock);
	}
}

void
lpfc_dequeue_node(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);

	lpfc_cancel_retry_delay_tmo(vport, ndlp);
	if (ndlp->nlp_state && !list_empty(&ndlp->nlp_listp))
		lpfc_nlp_counters(vport, ndlp->nlp_state, -1);
	spin_lock_irq(shost->host_lock);
	list_del_init(&ndlp->nlp_listp);
	spin_unlock_irq(shost->host_lock);
	lpfc_nlp_state_cleanup(vport, ndlp, ndlp->nlp_state,
				NLP_STE_UNUSED_NODE);
}

static void
lpfc_disable_node(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp)
{
	lpfc_cancel_retry_delay_tmo(vport, ndlp);
	if (ndlp->nlp_state && !list_empty(&ndlp->nlp_listp))
		lpfc_nlp_counters(vport, ndlp->nlp_state, -1);
	lpfc_nlp_state_cleanup(vport, ndlp, ndlp->nlp_state,
				NLP_STE_UNUSED_NODE);
}
/**
 * lpfc_initialize_node - Initialize all fields of node object
 * @vport: Pointer to Virtual Port object.
 * @ndlp: Pointer to FC node object.
 * @did: FC_ID of the node.
 *
 * This function is always called when node object need to be initialized.
 * It initializes all the fields of the node object. Although the reference
 * to phba from @ndlp can be obtained indirectly through it's reference to
 * @vport, a direct reference to phba is taken here by @ndlp. This is due
 * to the life-span of the @ndlp might go beyond the existence of @vport as
 * the final release of ndlp is determined by its reference count. And, the
 * operation on @ndlp needs the reference to phba.
 **/
static inline void
lpfc_initialize_node(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
	uint32_t did)
{
	INIT_LIST_HEAD(&ndlp->els_retry_evt.evt_listp);
	INIT_LIST_HEAD(&ndlp->dev_loss_evt.evt_listp);
	setup_timer(&ndlp->nlp_delayfunc, lpfc_els_retry_delay,
			(unsigned long)ndlp);
	ndlp->nlp_DID = did;
	ndlp->vport = vport;
	ndlp->phba = vport->phba;
	ndlp->nlp_sid = NLP_NO_SID;
	ndlp->nlp_fc4_type = NLP_FC4_NONE;
	kref_init(&ndlp->kref);
	NLP_INT_NODE_ACT(ndlp);
	atomic_set(&ndlp->cmd_pending, 0);
	ndlp->cmd_qdepth = vport->cfg_tgt_queue_depth;
}

struct lpfc_nodelist *
lpfc_enable_node(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
		 int state)
{
	struct lpfc_hba *phba = vport->phba;
	uint32_t did;
	unsigned long flags;
	unsigned long *active_rrqs_xri_bitmap = NULL;
	int rpi = LPFC_RPI_ALLOC_ERROR;

	if (!ndlp)
		return NULL;

	if (phba->sli_rev == LPFC_SLI_REV4) {
		rpi = lpfc_sli4_alloc_rpi(vport->phba);
		if (rpi == LPFC_RPI_ALLOC_ERROR)
			return NULL;
	}

	spin_lock_irqsave(&phba->ndlp_lock, flags);
	/* The ndlp should not be in memory free mode */
	if (NLP_CHK_FREE_REQ(ndlp)) {
		spin_unlock_irqrestore(&phba->ndlp_lock, flags);
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_NODE,
				"0277 lpfc_enable_node: ndlp:x%p "
				"usgmap:x%x refcnt:%d\n",
				(void *)ndlp, ndlp->nlp_usg_map,
				kref_read(&ndlp->kref));
		goto free_rpi;
	}
	/* The ndlp should not already be in active mode */
	if (NLP_CHK_NODE_ACT(ndlp)) {
		spin_unlock_irqrestore(&phba->ndlp_lock, flags);
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_NODE,
				"0278 lpfc_enable_node: ndlp:x%p "
				"usgmap:x%x refcnt:%d\n",
				(void *)ndlp, ndlp->nlp_usg_map,
				kref_read(&ndlp->kref));
		goto free_rpi;
	}

	/* Keep the original DID */
	did = ndlp->nlp_DID;
	if (phba->sli_rev == LPFC_SLI_REV4)
		active_rrqs_xri_bitmap = ndlp->active_rrqs_xri_bitmap;

	/* re-initialize ndlp except of ndlp linked list pointer */
	memset((((char *)ndlp) + sizeof (struct list_head)), 0,
		sizeof (struct lpfc_nodelist) - sizeof (struct list_head));
	lpfc_initialize_node(vport, ndlp, did);

	if (phba->sli_rev == LPFC_SLI_REV4)
		ndlp->active_rrqs_xri_bitmap = active_rrqs_xri_bitmap;

	spin_unlock_irqrestore(&phba->ndlp_lock, flags);
	if (vport->phba->sli_rev == LPFC_SLI_REV4) {
		ndlp->nlp_rpi = rpi;
		lpfc_printf_vlog(vport, KERN_INFO, LOG_NODE,
				 "0008 rpi:%x DID:%x flg:%x refcnt:%d "
				 "map:%x %p\n", ndlp->nlp_rpi, ndlp->nlp_DID,
				 ndlp->nlp_flag,
				 kref_read(&ndlp->kref),
				 ndlp->nlp_usg_map, ndlp);
	}


	if (state != NLP_STE_UNUSED_NODE)
		lpfc_nlp_set_state(vport, ndlp, state);

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_NODE,
		"node enable:       did:x%x",
		ndlp->nlp_DID, 0, 0);
	return ndlp;

free_rpi:
	if (phba->sli_rev == LPFC_SLI_REV4)
		lpfc_sli4_free_rpi(vport->phba, rpi);
	return NULL;
}

void
lpfc_drop_node(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp)
{
	/*
	 * Use of lpfc_drop_node and UNUSED list: lpfc_drop_node should
	 * be used if we wish to issue the "last" lpfc_nlp_put() to remove
	 * the ndlp from the vport. The ndlp marked as UNUSED on the list
	 * until ALL other outstanding threads have completed. We check
	 * that the ndlp not already in the UNUSED state before we proceed.
	 */
	if (ndlp->nlp_state == NLP_STE_UNUSED_NODE)
		return;
	lpfc_nlp_set_state(vport, ndlp, NLP_STE_UNUSED_NODE);
	if (vport->phba->sli_rev == LPFC_SLI_REV4) {
		lpfc_cleanup_vports_rrqs(vport, ndlp);
		lpfc_unreg_rpi(vport, ndlp);
	}

	lpfc_nlp_put(ndlp);
	return;
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
		/* For FAN, timeout should be greater than edtov */
		tmo = (((phba->fc_edtov + 999) / 1000) + 1);
	} else {
		/* Normal discovery timeout should be > than ELS/CT timeout
		 * FC spec states we need 3 * ratov for CT requests
		 */
		tmo = ((phba->fc_ratov * 3) + 3);
	}


	if (!timer_pending(&vport->fc_disctmo)) {
		lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_CMD,
			"set disc timer:  tmo:x%x state:x%x flg:x%x",
			tmo, vport->port_state, vport->fc_flag);
	}

	mod_timer(&vport->fc_disctmo, jiffies + msecs_to_jiffies(1000 * tmo));
	spin_lock_irq(shost->host_lock);
	vport->fc_flag |= FC_DISC_TMO;
	spin_unlock_irq(shost->host_lock);

	/* Start Discovery Timer state <hba_state> */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
			 "0247 Start Discovery Timer state x%x "
			 "Data: x%x x%lx x%x x%x\n",
			 vport->port_state, tmo,
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
	lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
			 "0248 Cancel Discovery Timer state x%x "
			 "Data: x%x x%x x%x\n",
			 vport->port_state, vport->fc_flag,
			 vport->fc_plogi_cnt, vport->fc_adisc_cnt);
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
	IOCB_t *icmd = &iocb->iocb;
	struct lpfc_vport    *vport = ndlp->vport;

	if (iocb->vport != vport)
		return 0;

	if (pring->ringno == LPFC_ELS_RING) {
		switch (icmd->ulpCommand) {
		case CMD_GEN_REQUEST64_CR:
			if (iocb->context_un.ndlp == ndlp)
				return 1;
		case CMD_ELS_REQUEST64_CR:
			if (icmd->un.elsreq64.remoteID == ndlp->nlp_DID)
				return 1;
		case CMD_XMIT_ELS_RSP64_CX:
			if (iocb->context1 == (uint8_t *) ndlp)
				return 1;
		}
	} else if (pring->ringno == LPFC_FCP_RING) {
		/* Skip match check if waiting to relogin to FCP target */
		if ((ndlp->nlp_type & NLP_FCP_TARGET) &&
		    (ndlp->nlp_flag & NLP_DELAY_TMO)) {
			return 0;
		}
		if (icmd->ulpContext == (volatile ushort)ndlp->nlp_rpi) {
			return 1;
		}
	}
	return 0;
}

static void
__lpfc_dequeue_nport_iocbs(struct lpfc_hba *phba,
		struct lpfc_nodelist *ndlp, struct lpfc_sli_ring *pring,
		struct list_head *dequeue_list)
{
	struct lpfc_iocbq *iocb, *next_iocb;

	list_for_each_entry_safe(iocb, next_iocb, &pring->txq, list) {
		/* Check to see if iocb matches the nport */
		if (lpfc_check_sli_ndlp(phba, pring, iocb, ndlp))
			/* match, dequeue */
			list_move_tail(&iocb->list, dequeue_list);
	}
}

static void
lpfc_sli3_dequeue_nport_iocbs(struct lpfc_hba *phba,
		struct lpfc_nodelist *ndlp, struct list_head *dequeue_list)
{
	struct lpfc_sli *psli = &phba->sli;
	uint32_t i;

	spin_lock_irq(&phba->hbalock);
	for (i = 0; i < psli->num_rings; i++)
		__lpfc_dequeue_nport_iocbs(phba, ndlp, &psli->sli3_ring[i],
						dequeue_list);
	spin_unlock_irq(&phba->hbalock);
}

static void
lpfc_sli4_dequeue_nport_iocbs(struct lpfc_hba *phba,
		struct lpfc_nodelist *ndlp, struct list_head *dequeue_list)
{
	struct lpfc_sli_ring *pring;
	struct lpfc_queue *qp = NULL;

	spin_lock_irq(&phba->hbalock);
	list_for_each_entry(qp, &phba->sli4_hba.lpfc_wq_list, wq_list) {
		pring = qp->pring;
		if (!pring)
			continue;
		spin_lock(&pring->ring_lock);
		__lpfc_dequeue_nport_iocbs(phba, ndlp, pring, dequeue_list);
		spin_unlock(&pring->ring_lock);
	}
	spin_unlock_irq(&phba->hbalock);
}

/*
 * Free resources / clean up outstanding I/Os
 * associated with nlp_rpi in the LPFC_NODELIST entry.
 */
static int
lpfc_no_rpi(struct lpfc_hba *phba, struct lpfc_nodelist *ndlp)
{
	LIST_HEAD(completions);

	lpfc_fabric_abort_nport(ndlp);

	/*
	 * Everything that matches on txcmplq will be returned
	 * by firmware with a no rpi error.
	 */
	if (ndlp->nlp_flag & NLP_RPI_REGISTERED) {
		if (phba->sli_rev != LPFC_SLI_REV4)
			lpfc_sli3_dequeue_nport_iocbs(phba, ndlp, &completions);
		else
			lpfc_sli4_dequeue_nport_iocbs(phba, ndlp, &completions);
	}

	/* Cancel all the IOCBs from the completions list */
	lpfc_sli_cancel_iocbs(phba, &completions, IOSTAT_LOCAL_REJECT,
			      IOERR_SLI_ABORTED);

	return 0;
}

/**
 * lpfc_nlp_logo_unreg - Unreg mailbox completion handler before LOGO
 * @phba: Pointer to HBA context object.
 * @pmb: Pointer to mailbox object.
 *
 * This function will issue an ELS LOGO command after completing
 * the UNREG_RPI.
 **/
static void
lpfc_nlp_logo_unreg(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmb)
{
	struct lpfc_vport  *vport = pmb->vport;
	struct lpfc_nodelist *ndlp;

	ndlp = (struct lpfc_nodelist *)(pmb->context1);
	if (!ndlp)
		return;
	lpfc_issue_els_logo(vport, ndlp, 0);
	mempool_free(pmb, phba->mbox_mem_pool);
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
	int rc, acc_plogi = 1;
	uint16_t rpi;

	if (ndlp->nlp_flag & NLP_RPI_REGISTERED ||
	    ndlp->nlp_flag & NLP_REG_LOGIN_SEND) {
		if (ndlp->nlp_flag & NLP_REG_LOGIN_SEND)
			lpfc_printf_vlog(vport, KERN_INFO, LOG_SLI,
					 "3366 RPI x%x needs to be "
					 "unregistered nlp_flag x%x "
					 "did x%x\n",
					 ndlp->nlp_rpi, ndlp->nlp_flag,
					 ndlp->nlp_DID);
		mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
		if (mbox) {
			/* SLI4 ports require the physical rpi value. */
			rpi = ndlp->nlp_rpi;
			if (phba->sli_rev == LPFC_SLI_REV4)
				rpi = phba->sli4_hba.rpi_ids[ndlp->nlp_rpi];

			lpfc_unreg_login(phba, vport->vpi, rpi, mbox);
			mbox->vport = vport;
			if (ndlp->nlp_flag & NLP_ISSUE_LOGO) {
				mbox->context1 = ndlp;
				mbox->mbox_cmpl = lpfc_nlp_logo_unreg;
			} else {
				if (phba->sli_rev == LPFC_SLI_REV4 &&
				    (!(vport->load_flag & FC_UNLOADING)) &&
				    (bf_get(lpfc_sli_intf_if_type,
				     &phba->sli4_hba.sli_intf) ==
				      LPFC_SLI_INTF_IF_TYPE_2) &&
				    (kref_read(&ndlp->kref) > 0)) {
					mbox->context1 = lpfc_nlp_get(ndlp);
					mbox->mbox_cmpl =
						lpfc_sli4_unreg_rpi_cmpl_clr;
					/*
					 * accept PLOGIs after unreg_rpi_cmpl
					 */
					acc_plogi = 0;
				} else
					mbox->mbox_cmpl =
						lpfc_sli_def_mbox_cmpl;
			}

			rc = lpfc_sli_issue_mbox(phba, mbox, MBX_NOWAIT);
			if (rc == MBX_NOT_FINISHED) {
				mempool_free(mbox, phba->mbox_mem_pool);
				acc_plogi = 1;
			}
		}
		lpfc_no_rpi(phba, ndlp);

		if (phba->sli_rev != LPFC_SLI_REV4)
			ndlp->nlp_rpi = 0;
		ndlp->nlp_flag &= ~NLP_RPI_REGISTERED;
		ndlp->nlp_flag &= ~NLP_NPR_ADISC;
		if (acc_plogi)
			ndlp->nlp_flag &= ~NLP_LOGO_ACC;
		return 1;
	}
	ndlp->nlp_flag &= ~NLP_LOGO_ACC;
	return 0;
}

/**
 * lpfc_unreg_hba_rpis - Unregister rpis registered to the hba.
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to unregister all the currently registered RPIs
 * to the HBA.
 **/
void
lpfc_unreg_hba_rpis(struct lpfc_hba *phba)
{
	struct lpfc_vport **vports;
	struct lpfc_nodelist *ndlp;
	struct Scsi_Host *shost;
	int i;

	vports = lpfc_create_vport_work_array(phba);
	if (!vports) {
		lpfc_printf_log(phba, KERN_ERR, LOG_DISCOVERY,
			"2884 Vport array allocation failed \n");
		return;
	}
	for (i = 0; i <= phba->max_vports && vports[i] != NULL; i++) {
		shost = lpfc_shost_from_vport(vports[i]);
		spin_lock_irq(shost->host_lock);
		list_for_each_entry(ndlp, &vports[i]->fc_nodes, nlp_listp) {
			if (ndlp->nlp_flag & NLP_RPI_REGISTERED) {
				/* The mempool_alloc might sleep */
				spin_unlock_irq(shost->host_lock);
				lpfc_unreg_rpi(vports[i], ndlp);
				spin_lock_irq(shost->host_lock);
			}
		}
		spin_unlock_irq(shost->host_lock);
	}
	lpfc_destroy_vport_work_array(phba, vports);
}

void
lpfc_unreg_all_rpis(struct lpfc_vport *vport)
{
	struct lpfc_hba  *phba  = vport->phba;
	LPFC_MBOXQ_t     *mbox;
	int rc;

	if (phba->sli_rev == LPFC_SLI_REV4) {
		lpfc_sli4_unreg_all_rpis(vport);
		return;
	}

	mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (mbox) {
		lpfc_unreg_login(phba, vport->vpi, LPFC_UNREG_ALL_RPIS_VPORT,
				 mbox);
		mbox->vport = vport;
		mbox->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
		mbox->context1 = NULL;
		rc = lpfc_sli_issue_mbox_wait(phba, mbox, LPFC_MBOX_TMO);
		if (rc != MBX_TIMEOUT)
			mempool_free(mbox, phba->mbox_mem_pool);

		if ((rc == MBX_TIMEOUT) || (rc == MBX_NOT_FINISHED))
			lpfc_printf_vlog(vport, KERN_ERR, LOG_MBOX | LOG_VPORT,
				"1836 Could not issue "
				"unreg_login(all_rpis) status %d\n", rc);
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
		lpfc_unreg_did(phba, vport->vpi, LPFC_UNREG_ALL_DFLT_RPIS,
			       mbox);
		mbox->vport = vport;
		mbox->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
		mbox->context1 = NULL;
		rc = lpfc_sli_issue_mbox_wait(phba, mbox, LPFC_MBOX_TMO);
		if (rc != MBX_TIMEOUT)
			mempool_free(mbox, phba->mbox_mem_pool);

		if ((rc == MBX_TIMEOUT) || (rc == MBX_NOT_FINISHED))
			lpfc_printf_vlog(vport, KERN_ERR, LOG_MBOX | LOG_VPORT,
					 "1815 Could not issue "
					 "unreg_did (default rpis) status %d\n",
					 rc);
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
	lpfc_printf_vlog(vport, KERN_INFO, LOG_NODE,
			 "0900 Cleanup node for NPort x%x "
			 "Data: x%x x%x x%x\n",
			 ndlp->nlp_DID, ndlp->nlp_flag,
			 ndlp->nlp_state, ndlp->nlp_rpi);
	if (NLP_CHK_FREE_REQ(ndlp)) {
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_NODE,
				"0280 lpfc_cleanup_node: ndlp:x%p "
				"usgmap:x%x refcnt:%d\n",
				(void *)ndlp, ndlp->nlp_usg_map,
				kref_read(&ndlp->kref));
		lpfc_dequeue_node(vport, ndlp);
	} else {
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_NODE,
				"0281 lpfc_cleanup_node: ndlp:x%p "
				"usgmap:x%x refcnt:%d\n",
				(void *)ndlp, ndlp->nlp_usg_map,
				kref_read(&ndlp->kref));
		lpfc_disable_node(vport, ndlp);
	}


	/* Don't need to clean up REG_LOGIN64 cmds for Default RPI cleanup */

	/* cleanup any ndlp on mbox q waiting for reglogin cmpl */
	if ((mb = phba->sli.mbox_active)) {
		if ((mb->u.mb.mbxCommand == MBX_REG_LOGIN64) &&
		   !(mb->mbox_flag & LPFC_MBX_IMED_UNREG) &&
		   (ndlp == (struct lpfc_nodelist *) mb->context2)) {
			mb->context2 = NULL;
			mb->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
		}
	}

	spin_lock_irq(&phba->hbalock);
	/* Cleanup REG_LOGIN completions which are not yet processed */
	list_for_each_entry(mb, &phba->sli.mboxq_cmpl, list) {
		if ((mb->u.mb.mbxCommand != MBX_REG_LOGIN64) ||
			(mb->mbox_flag & LPFC_MBX_IMED_UNREG) ||
			(ndlp != (struct lpfc_nodelist *) mb->context2))
			continue;

		mb->context2 = NULL;
		mb->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
	}

	list_for_each_entry_safe(mb, nextmb, &phba->sli.mboxq, list) {
		if ((mb->u.mb.mbxCommand == MBX_REG_LOGIN64) &&
		   !(mb->mbox_flag & LPFC_MBX_IMED_UNREG) &&
		    (ndlp == (struct lpfc_nodelist *) mb->context2)) {
			mp = (struct lpfc_dmabuf *) (mb->context1);
			if (mp) {
				__lpfc_mbuf_free(phba, mp->virt, mp->phys);
				kfree(mp);
			}
			list_del(&mb->list);
			mempool_free(mb, phba->mbox_mem_pool);
			/* We shall not invoke the lpfc_nlp_put to decrement
			 * the ndlp reference count as we are in the process
			 * of lpfc_nlp_release.
			 */
		}
	}
	spin_unlock_irq(&phba->hbalock);

	lpfc_els_abort(phba, ndlp);

	spin_lock_irq(shost->host_lock);
	ndlp->nlp_flag &= ~NLP_DELAY_TMO;
	spin_unlock_irq(shost->host_lock);

	ndlp->nlp_last_elscmd = 0;
	del_timer_sync(&ndlp->nlp_delayfunc);

	list_del_init(&ndlp->els_retry_evt.evt_listp);
	list_del_init(&ndlp->dev_loss_evt.evt_listp);
	lpfc_cleanup_vports_rrqs(vport, ndlp);
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
	struct lpfc_hba  *phba = vport->phba;
	struct lpfc_rport_data *rdata;
	struct fc_rport *rport;
	LPFC_MBOXQ_t *mbox;
	int rc;

	lpfc_cancel_retry_delay_tmo(vport, ndlp);
	if ((ndlp->nlp_flag & NLP_DEFER_RM) &&
	    !(ndlp->nlp_flag & NLP_REG_LOGIN_SEND) &&
	    !(ndlp->nlp_flag & NLP_RPI_REGISTERED)) {
		/* For this case we need to cleanup the default rpi
		 * allocated by the firmware.
		 */
		lpfc_printf_vlog(vport, KERN_INFO, LOG_NODE,
				 "0005 rpi:%x DID:%x flg:%x %d map:%x %p\n",
				 ndlp->nlp_rpi, ndlp->nlp_DID, ndlp->nlp_flag,
				 kref_read(&ndlp->kref),
				 ndlp->nlp_usg_map, ndlp);
		if ((mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL))
			!= NULL) {
			rc = lpfc_reg_rpi(phba, vport->vpi, ndlp->nlp_DID,
			    (uint8_t *) &vport->fc_sparam, mbox, ndlp->nlp_rpi);
			if (rc) {
				mempool_free(mbox, phba->mbox_mem_pool);
			}
			else {
				mbox->mbox_flag |= LPFC_MBX_IMED_UNREG;
				mbox->mbox_cmpl = lpfc_mbx_cmpl_dflt_rpi;
				mbox->vport = vport;
				mbox->context2 = ndlp;
				rc =lpfc_sli_issue_mbox(phba, mbox, MBX_NOWAIT);
				if (rc == MBX_NOT_FINISHED) {
					mempool_free(mbox, phba->mbox_mem_pool);
				}
			}
		}
	}
	lpfc_cleanup_node(vport, ndlp);

	/*
	 * ndlp->rport must be set to NULL before it reaches here
	 * i.e. break rport/node link before doing lpfc_nlp_put for
	 * registered rport and then drop the reference of rport.
	 */
	if (ndlp->rport) {
		/*
		 * extra lpfc_nlp_put dropped the reference of ndlp
		 * for registered rport so need to cleanup rport
		 */
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_NODE,
				"0940 removed node x%p DID x%x "
				" rport not null %p\n",
				ndlp, ndlp->nlp_DID, ndlp->rport);
		rport = ndlp->rport;
		rdata = rport->dd_data;
		rdata->pnode = NULL;
		ndlp->rport = NULL;
		put_device(&rport->dev);
	}
}

static int
lpfc_matchdid(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
	      uint32_t did)
{
	D_ID mydid, ndlpdid, matchdid;

	if (did == Bcast_DID)
		return 0;

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
			/* This code is supposed to match the ID
			 * for a private loop device that is
			 * connect to fl_port. But we need to
			 * check that the port did not just go
			 * from pt2pt to fabric or we could end
			 * up matching ndlp->nlp_DID 000001 to
			 * fabric DID 0x20101
			 */
			if ((ndlpdid.un.b.domain == 0) &&
			    (ndlpdid.un.b.area == 0)) {
				if (ndlpdid.un.b.id &&
				    vport->phba->fc_topology ==
				    LPFC_TOPOLOGY_LOOP)
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
	struct lpfc_nodelist *ndlp;
	uint32_t data1;

	list_for_each_entry(ndlp, &vport->fc_nodes, nlp_listp) {
		if (lpfc_matchdid(vport, ndlp, did)) {
			data1 = (((uint32_t) ndlp->nlp_state << 24) |
				 ((uint32_t) ndlp->nlp_xri << 16) |
				 ((uint32_t) ndlp->nlp_type << 8) |
				 ((uint32_t) ndlp->nlp_rpi & 0xff));
			lpfc_printf_vlog(vport, KERN_INFO, LOG_NODE,
					 "0929 FIND node DID "
					 "Data: x%p x%x x%x x%x %p\n",
					 ndlp, ndlp->nlp_DID,
					 ndlp->nlp_flag, data1,
					 ndlp->active_rrqs_xri_bitmap);
			return ndlp;
		}
	}

	/* FIND node did <did> NOT FOUND */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_NODE,
			 "0932 FIND node did x%x NOT FOUND.\n", did);
	return NULL;
}

struct lpfc_nodelist *
lpfc_findnode_did(struct lpfc_vport *vport, uint32_t did)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct lpfc_nodelist *ndlp;
	unsigned long iflags;

	spin_lock_irqsave(shost->host_lock, iflags);
	ndlp = __lpfc_findnode_did(vport, did);
	spin_unlock_irqrestore(shost->host_lock, iflags);
	return ndlp;
}

struct lpfc_nodelist *
lpfc_setup_disc_node(struct lpfc_vport *vport, uint32_t did)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct lpfc_nodelist *ndlp;

	ndlp = lpfc_findnode_did(vport, did);
	if (!ndlp) {
		if (vport->phba->nvmet_support)
			return NULL;
		if ((vport->fc_flag & FC_RSCN_MODE) != 0 &&
		    lpfc_rscn_payload_check(vport, did) == 0)
			return NULL;
		ndlp = lpfc_nlp_init(vport, did);
		if (!ndlp)
			return NULL;
		lpfc_nlp_set_state(vport, ndlp, NLP_STE_NPR_NODE);
		spin_lock_irq(shost->host_lock);
		ndlp->nlp_flag |= NLP_NPR_2B_DISC;
		spin_unlock_irq(shost->host_lock);
		return ndlp;
	} else if (!NLP_CHK_NODE_ACT(ndlp)) {
		if (vport->phba->nvmet_support)
			return NULL;
		ndlp = lpfc_enable_node(vport, ndlp, NLP_STE_NPR_NODE);
		if (!ndlp)
			return NULL;
		spin_lock_irq(shost->host_lock);
		ndlp->nlp_flag |= NLP_NPR_2B_DISC;
		spin_unlock_irq(shost->host_lock);
		return ndlp;
	}

	/* The NVME Target does not want to actively manage an rport.
	 * The goal is to allow the target to reset its state and clear
	 * pending IO in preparation for the initiator to recover.
	 */
	if ((vport->fc_flag & FC_RSCN_MODE) &&
	    !(vport->fc_flag & FC_NDISC_ACTIVE)) {
		if (lpfc_rscn_payload_check(vport, did)) {

			/* Since this node is marked for discovery,
			 * delay timeout is not needed.
			 */
			lpfc_cancel_retry_delay_tmo(vport, ndlp);

			/* NVME Target mode waits until rport is known to be
			 * impacted by the RSCN before it transitions.  No
			 * active management - just go to NPR provided the
			 * node had a valid login.
			 */
			if (vport->phba->nvmet_support)
				return ndlp;

			/* If we've already received a PLOGI from this NPort
			 * we don't need to try to discover it again.
			 */
			if (ndlp->nlp_flag & NLP_RCV_PLOGI)
				return NULL;

			spin_lock_irq(shost->host_lock);
			ndlp->nlp_flag |= NLP_NPR_2B_DISC;
			spin_unlock_irq(shost->host_lock);
		} else
			ndlp = NULL;
	} else {
		/* If the initiator received a PLOGI from this NPort or if the
		 * initiator is already in the process of discovery on it,
		 * there's no need to try to discover it again.
		 */
		if (ndlp->nlp_state == NLP_STE_ADISC_ISSUE ||
		    ndlp->nlp_state == NLP_STE_PLOGI_ISSUE ||
		    (!vport->phba->nvmet_support &&
		     ndlp->nlp_flag & NLP_RCV_PLOGI))
			return NULL;

		if (vport->phba->nvmet_support)
			return ndlp;

		/* Moving to NPR state clears unsolicited flags and
		 * allows for rediscovery
		 */
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

	if (phba->fc_topology != LPFC_TOPOLOGY_LOOP)
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
			if (vport->cfg_scan_down)
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

/* SLI3 only */
void
lpfc_issue_clear_la(struct lpfc_hba *phba, struct lpfc_vport *vport)
{
	LPFC_MBOXQ_t *mbox;
	struct lpfc_sli *psli = &phba->sli;
	struct lpfc_sli_ring *extra_ring = &psli->sli3_ring[LPFC_EXTRA_RING];
	struct lpfc_sli_ring *fcp_ring   = &psli->sli3_ring[LPFC_FCP_RING];
	int  rc;

	/*
	 * if it's not a physical port or if we already send
	 * clear_la then don't send it.
	 */
	if ((phba->link_state >= LPFC_CLEAR_LA) ||
	    (vport->port_type != LPFC_PHYSICAL_PORT) ||
		(phba->sli_rev == LPFC_SLI_REV4))
		return;

			/* Link up discovery */
	if ((mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL)) != NULL) {
		phba->link_state = LPFC_CLEAR_LA;
		lpfc_clear_la(phba, mbox);
		mbox->mbox_cmpl = lpfc_mbx_cmpl_clear_la;
		mbox->vport = vport;
		rc = lpfc_sli_issue_mbox(phba, mbox, MBX_NOWAIT);
		if (rc == MBX_NOT_FINISHED) {
			mempool_free(mbox, phba->mbox_mem_pool);
			lpfc_disc_flush_list(vport);
			extra_ring->flag &= ~LPFC_STOP_IOCB_EVENT;
			fcp_ring->flag &= ~LPFC_STOP_IOCB_EVENT;
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
		lpfc_reg_vpi(vport, regvpimbox);
		regvpimbox->mbox_cmpl = lpfc_mbx_cmpl_reg_vpi;
		regvpimbox->vport = vport;
		if (lpfc_sli_issue_mbox(phba, regvpimbox, MBX_NOWAIT)
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

	if (!lpfc_is_link_up(phba)) {
		lpfc_printf_vlog(vport, KERN_INFO, LOG_SLI,
				 "3315 Link is not up %x\n",
				 phba->link_state);
		return;
	}

	if (phba->link_state == LPFC_CLEAR_LA)
		clear_la_pending = 1;
	else
		clear_la_pending = 0;

	if (vport->port_state < LPFC_VPORT_READY)
		vport->port_state = LPFC_DISC_AUTH;

	lpfc_set_disctmo(vport);

	vport->fc_prevDID = vport->fc_myDID;
	vport->num_disc_nodes = 0;

	/* Start Discovery state <hba_state> */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
			 "0202 Start Discovery hba state x%x "
			 "Data: x%x x%x x%x\n",
			 vport->port_state, vport->fc_flag, vport->fc_plogi_cnt,
			 vport->fc_adisc_cnt);

	/* First do ADISCs - if any */
	num_sent = lpfc_els_disc_adisc(vport);

	if (num_sent)
		return;

	/* Register the VPI for SLI3, NPIV only. */
	if ((phba->sli3_options & LPFC_SLI3_NPIV_ENABLED) &&
	    !(vport->fc_flag & FC_PT2PT) &&
	    !(vport->fc_flag & FC_RSCN_MODE) &&
	    (phba->sli_rev < LPFC_SLI_REV4)) {
		lpfc_issue_clear_la(phba, vport);
		lpfc_issue_reg_vpi(phba, vport);
		return;
	}

	/*
	 * For SLI2, we need to set port_state to READY and continue
	 * discovery.
	 */
	if (vport->port_state < LPFC_VPORT_READY && !clear_la_pending) {
		/* If we get here, there is nothing to ADISC */
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
	pring = lpfc_phba_elsring(phba);

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

	/* Cancel all the IOCBs from the completions list */
	lpfc_sli_cancel_iocbs(phba, &completions, IOSTAT_LOCAL_REJECT,
			      IOERR_SLI_ABORTED);
}

static void
lpfc_disc_flush_list(struct lpfc_vport *vport)
{
	struct lpfc_nodelist *ndlp, *next_ndlp;
	struct lpfc_hba *phba = vport->phba;

	if (vport->fc_plogi_cnt || vport->fc_adisc_cnt) {
		list_for_each_entry_safe(ndlp, next_ndlp, &vport->fc_nodes,
					 nlp_listp) {
			if (!NLP_CHK_NODE_ACT(ndlp))
				continue;
			if (ndlp->nlp_state == NLP_STE_PLOGI_ISSUE ||
			    ndlp->nlp_state == NLP_STE_ADISC_ISSUE) {
				lpfc_free_tx(phba, ndlp);
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
	uint32_t tmo_posted;
	unsigned long flags = 0;

	if (unlikely(!phba))
		return;

	spin_lock_irqsave(&vport->work_port_lock, flags);
	tmo_posted = vport->work_port_events & WORKER_DISC_TMO;
	if (!tmo_posted)
		vport->work_port_events |= WORKER_DISC_TMO;
	spin_unlock_irqrestore(&vport->work_port_lock, flags);

	if (!tmo_posted)
		lpfc_worker_wake_up(phba);
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
		/*
		 * port_state is identically  LPFC_LOCAL_CFG_LINK while
		 * waiting for FAN timeout
		 */
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_DISCOVERY,
				 "0221 FAN timeout\n");

		/* Start discovery by sending FLOGI, clean up old rpis */
		list_for_each_entry_safe(ndlp, next_ndlp, &vport->fc_nodes,
					 nlp_listp) {
			if (!NLP_CHK_NODE_ACT(ndlp))
				continue;
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
			if (phba->sli_rev <= LPFC_SLI_REV3)
				lpfc_initial_flogi(vport);
			else
				lpfc_issue_init_vfi(vport);
			return;
		}
		break;

	case LPFC_FDISC:
	case LPFC_FLOGI:
	/* port_state is identically LPFC_FLOGI while waiting for FLOGI cmpl */
		/* Initial FLOGI timeout */
		lpfc_printf_vlog(vport, KERN_ERR, LOG_DISCOVERY,
				 "0222 Initial %s timeout\n",
				 vport->vpi ? "FDISC" : "FLOGI");

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
		lpfc_printf_vlog(vport, KERN_ERR, LOG_DISCOVERY,
				 "0223 Timeout while waiting for "
				 "NameServer login\n");
		/* Next look for NameServer ndlp */
		ndlp = lpfc_findnode_did(vport, NameServer_DID);
		if (ndlp && NLP_CHK_NODE_ACT(ndlp))
			lpfc_els_abort(phba, ndlp);

		/* ReStart discovery */
		goto restart_disc;

	case LPFC_NS_QRY:
	/* Check for wait for NameServer Rsp timeout */
		lpfc_printf_vlog(vport, KERN_ERR, LOG_DISCOVERY,
				 "0224 NameServer Query timeout "
				 "Data: x%x x%x\n",
				 vport->fc_ns_retry, LPFC_MAX_NS_RETRY);

		if (vport->fc_ns_retry < LPFC_MAX_NS_RETRY) {
			/* Try it one more time */
			vport->fc_ns_retry++;
			vport->gidft_inp = 0;
			rc = lpfc_issue_gidft(vport);
			if (rc == 0)
				break;
		}
		vport->fc_ns_retry = 0;

restart_disc:
		/*
		 * Discovery is over.
		 * set port_state to PORT_READY if SLI2.
		 * cmpl_reg_vpi will set port_state to READY for SLI3.
		 */
		if (phba->sli_rev < LPFC_SLI_REV4) {
			if (phba->sli3_options & LPFC_SLI3_NPIV_ENABLED)
				lpfc_issue_reg_vpi(phba, vport);
			else  {
				lpfc_issue_clear_la(phba, vport);
				vport->port_state = LPFC_VPORT_READY;
			}
		}

		/* Setup and issue mailbox INITIALIZE LINK command */
		initlinkmbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
		if (!initlinkmbox) {
			lpfc_printf_vlog(vport, KERN_ERR, LOG_DISCOVERY,
					 "0206 Device Discovery "
					 "completion error\n");
			phba->link_state = LPFC_HBA_ERROR;
			break;
		}

		lpfc_linkdown(phba);
		lpfc_init_link(phba, initlinkmbox, phba->cfg_topology,
			       phba->cfg_link_speed);
		initlinkmbox->u.mb.un.varInitLnk.lipsr_AL_PA = 0;
		initlinkmbox->vport = vport;
		initlinkmbox->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
		rc = lpfc_sli_issue_mbox(phba, initlinkmbox, MBX_NOWAIT);
		lpfc_set_loopback_flag(phba);
		if (rc == MBX_NOT_FINISHED)
			mempool_free(initlinkmbox, phba->mbox_mem_pool);

		break;

	case LPFC_DISC_AUTH:
	/* Node Authentication timeout */
		lpfc_printf_vlog(vport, KERN_ERR, LOG_DISCOVERY,
				 "0227 Node Authentication timeout\n");
		lpfc_disc_flush_list(vport);

		/*
		 * set port_state to PORT_READY if SLI2.
		 * cmpl_reg_vpi will set port_state to READY for SLI3.
		 */
		if (phba->sli_rev < LPFC_SLI_REV4) {
			if (phba->sli3_options & LPFC_SLI3_NPIV_ENABLED)
				lpfc_issue_reg_vpi(phba, vport);
			else  {	/* NPIV Not enabled */
				lpfc_issue_clear_la(phba, vport);
				vport->port_state = LPFC_VPORT_READY;
			}
		}
		break;

	case LPFC_VPORT_READY:
		if (vport->fc_flag & FC_RSCN_MODE) {
			lpfc_printf_vlog(vport, KERN_ERR, LOG_DISCOVERY,
					 "0231 RSCN timeout Data: x%x "
					 "x%x\n",
					 vport->fc_ns_retry, LPFC_MAX_NS_RETRY);

			/* Cleanup any outstanding ELS commands */
			lpfc_els_flush_cmd(vport);

			lpfc_els_flush_rscn(vport);
			lpfc_disc_flush_list(vport);
		}
		break;

	default:
		lpfc_printf_vlog(vport, KERN_ERR, LOG_DISCOVERY,
				 "0273 Unexpected discovery timeout, "
				 "vport State x%x\n", vport->port_state);
		break;
	}

	switch (phba->link_state) {
	case LPFC_CLEAR_LA:
				/* CLEAR LA timeout */
		lpfc_printf_vlog(vport, KERN_ERR, LOG_DISCOVERY,
				 "0228 CLEAR LA timeout\n");
		clrlaerr = 1;
		break;

	case LPFC_LINK_UP:
		lpfc_issue_clear_la(phba, vport);
		/* Drop thru */
	case LPFC_LINK_UNKNOWN:
	case LPFC_WARM_START:
	case LPFC_INIT_START:
	case LPFC_INIT_MBX_CMDS:
	case LPFC_LINK_DOWN:
	case LPFC_HBA_ERROR:
		lpfc_printf_vlog(vport, KERN_ERR, LOG_DISCOVERY,
				 "0230 Unexpected timeout, hba link "
				 "state x%x\n", phba->link_state);
		clrlaerr = 1;
		break;

	case LPFC_HBA_READY:
		break;
	}

	if (clrlaerr) {
		lpfc_disc_flush_list(vport);
		if (phba->sli_rev != LPFC_SLI_REV4) {
			psli->sli3_ring[(LPFC_EXTRA_RING)].flag &=
				~LPFC_STOP_IOCB_EVENT;
			psli->sli3_ring[LPFC_FCP_RING].flag &=
				~LPFC_STOP_IOCB_EVENT;
		}
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
	MAILBOX_t *mb = &pmb->u.mb;
	struct lpfc_dmabuf   *mp = (struct lpfc_dmabuf *) (pmb->context1);
	struct lpfc_nodelist *ndlp = (struct lpfc_nodelist *) pmb->context2;
	struct lpfc_vport    *vport = pmb->vport;

	pmb->context1 = NULL;
	pmb->context2 = NULL;

	if (phba->sli_rev < LPFC_SLI_REV4)
		ndlp->nlp_rpi = mb->un.varWords[0];
	ndlp->nlp_flag |= NLP_RPI_REGISTERED;
	ndlp->nlp_type |= NLP_FABRIC;
	lpfc_nlp_set_state(vport, ndlp, NLP_STE_UNMAPPED_NODE);
	lpfc_printf_vlog(vport, KERN_INFO, LOG_SLI,
			 "0004 rpi:%x DID:%x flg:%x %d map:%x %p\n",
			 ndlp->nlp_rpi, ndlp->nlp_DID, ndlp->nlp_flag,
			 kref_read(&ndlp->kref),
			 ndlp->nlp_usg_map, ndlp);
	/*
	 * Start issuing Fabric-Device Management Interface (FDMI) command to
	 * 0xfffffa (FDMI well known port).
	 * DHBA -> DPRT -> RHBA -> RPA  (physical port)
	 * DPRT -> RPRT (vports)
	 */
	if (vport->port_type == LPFC_PHYSICAL_PORT)
		lpfc_fdmi_cmd(vport, ndlp, SLI_MGMT_DHBA, 0);
	else
		lpfc_fdmi_cmd(vport, ndlp, SLI_MGMT_DPRT, 0);


	/* decrement the node reference count held for this callback
	 * function.
	 */
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

	/* check for active node */
	if (!NLP_CHK_NODE_ACT(ndlp))
		return 0;

	return ndlp->nlp_rpi == *rpi;
}

static int
lpfc_filter_by_wwpn(struct lpfc_nodelist *ndlp, void *param)
{
	return memcmp(&ndlp->nlp_portname, param,
		      sizeof(ndlp->nlp_portname)) == 0;
}

static struct lpfc_nodelist *
__lpfc_find_node(struct lpfc_vport *vport, node_filter filter, void *param)
{
	struct lpfc_nodelist *ndlp;

	list_for_each_entry(ndlp, &vport->fc_nodes, nlp_listp) {
		if (filter(ndlp, param)) {
			lpfc_printf_vlog(vport, KERN_INFO, LOG_NODE,
					 "3185 FIND node filter %p DID "
					 "Data: x%p x%x x%x\n",
					 filter, ndlp, ndlp->nlp_DID,
					 ndlp->nlp_flag);
			return ndlp;
		}
	}
	lpfc_printf_vlog(vport, KERN_INFO, LOG_NODE,
			 "3186 FIND node filter %p NOT FOUND.\n", filter);
	return NULL;
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

/*
 * This routine looks up the ndlp lists for the given RPI. If the rpi
 * is found, the routine returns the node element list pointer else
 * return NULL.
 */
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

/**
 * lpfc_find_vport_by_vpid - Find a vport on a HBA through vport identifier
 * @phba: pointer to lpfc hba data structure.
 * @vpi: the physical host virtual N_Port identifier.
 *
 * This routine finds a vport on a HBA (referred by @phba) through a
 * @vpi. The function walks the HBA's vport list and returns the address
 * of the vport with the matching @vpi.
 *
 * Return code
 *    NULL - No vport with the matching @vpi found
 *    Otherwise - Address to the vport with the matching @vpi.
 **/
struct lpfc_vport *
lpfc_find_vport_by_vpid(struct lpfc_hba *phba, uint16_t vpi)
{
	struct lpfc_vport *vport;
	unsigned long flags;
	int i = 0;

	/* The physical ports are always vpi 0 - translate is unnecessary. */
	if (vpi > 0) {
		/*
		 * Translate the physical vpi to the logical vpi.  The
		 * vport stores the logical vpi.
		 */
		for (i = 0; i < phba->max_vpi; i++) {
			if (vpi == phba->vpi_ids[i])
				break;
		}

		if (i >= phba->max_vpi) {
			lpfc_printf_log(phba, KERN_ERR, LOG_ELS,
					 "2936 Could not find Vport mapped "
					 "to vpi %d\n", vpi);
			return NULL;
		}
	}

	spin_lock_irqsave(&phba->hbalock, flags);
	list_for_each_entry(vport, &phba->port_list, listentry) {
		if (vport->vpi == i) {
			spin_unlock_irqrestore(&phba->hbalock, flags);
			return vport;
		}
	}
	spin_unlock_irqrestore(&phba->hbalock, flags);
	return NULL;
}

struct lpfc_nodelist *
lpfc_nlp_init(struct lpfc_vport *vport, uint32_t did)
{
	struct lpfc_nodelist *ndlp;
	int rpi = LPFC_RPI_ALLOC_ERROR;

	if (vport->phba->sli_rev == LPFC_SLI_REV4) {
		rpi = lpfc_sli4_alloc_rpi(vport->phba);
		if (rpi == LPFC_RPI_ALLOC_ERROR)
			return NULL;
	}

	ndlp = mempool_alloc(vport->phba->nlp_mem_pool, GFP_KERNEL);
	if (!ndlp) {
		if (vport->phba->sli_rev == LPFC_SLI_REV4)
			lpfc_sli4_free_rpi(vport->phba, rpi);
		return NULL;
	}

	memset(ndlp, 0, sizeof (struct lpfc_nodelist));

	lpfc_initialize_node(vport, ndlp, did);
	INIT_LIST_HEAD(&ndlp->nlp_listp);
	if (vport->phba->sli_rev == LPFC_SLI_REV4) {
		ndlp->nlp_rpi = rpi;
		lpfc_printf_vlog(vport, KERN_INFO, LOG_NODE,
				 "0007 rpi:%x DID:%x flg:%x refcnt:%d "
				 "map:%x %p\n", ndlp->nlp_rpi, ndlp->nlp_DID,
				 ndlp->nlp_flag,
				 kref_read(&ndlp->kref),
				 ndlp->nlp_usg_map, ndlp);

		ndlp->active_rrqs_xri_bitmap =
				mempool_alloc(vport->phba->active_rrq_pool,
					      GFP_KERNEL);
		if (ndlp->active_rrqs_xri_bitmap)
			memset(ndlp->active_rrqs_xri_bitmap, 0,
			       ndlp->phba->cfg_rrq_xri_bitmap_sz);
	}



	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_NODE,
		"node init:       did:x%x",
		ndlp->nlp_DID, 0, 0);

	return ndlp;
}

/* This routine releases all resources associated with a specifc NPort's ndlp
 * and mempool_free's the nodelist.
 */
static void
lpfc_nlp_release(struct kref *kref)
{
	struct lpfc_hba *phba;
	unsigned long flags;
	struct lpfc_nodelist *ndlp = container_of(kref, struct lpfc_nodelist,
						  kref);

	lpfc_debugfs_disc_trc(ndlp->vport, LPFC_DISC_TRC_NODE,
		"node release:    did:x%x flg:x%x type:x%x",
		ndlp->nlp_DID, ndlp->nlp_flag, ndlp->nlp_type);

	lpfc_printf_vlog(ndlp->vport, KERN_INFO, LOG_NODE,
			"0279 lpfc_nlp_release: ndlp:x%p did %x "
			"usgmap:x%x refcnt:%d rpi:%x\n",
			(void *)ndlp, ndlp->nlp_DID, ndlp->nlp_usg_map,
			kref_read(&ndlp->kref), ndlp->nlp_rpi);

	/* remove ndlp from action. */
	lpfc_nlp_remove(ndlp->vport, ndlp);

	/* clear the ndlp active flag for all release cases */
	phba = ndlp->phba;
	spin_lock_irqsave(&phba->ndlp_lock, flags);
	NLP_CLR_NODE_ACT(ndlp);
	spin_unlock_irqrestore(&phba->ndlp_lock, flags);
	if (phba->sli_rev == LPFC_SLI_REV4)
		lpfc_sli4_free_rpi(phba, ndlp->nlp_rpi);

	/* free ndlp memory for final ndlp release */
	if (NLP_CHK_FREE_REQ(ndlp)) {
		kfree(ndlp->lat_data);
		if (phba->sli_rev == LPFC_SLI_REV4)
			mempool_free(ndlp->active_rrqs_xri_bitmap,
				     ndlp->phba->active_rrq_pool);
		mempool_free(ndlp, ndlp->phba->nlp_mem_pool);
	}
}

/* This routine bumps the reference count for a ndlp structure to ensure
 * that one discovery thread won't free a ndlp while another discovery thread
 * is using it.
 */
struct lpfc_nodelist *
lpfc_nlp_get(struct lpfc_nodelist *ndlp)
{
	struct lpfc_hba *phba;
	unsigned long flags;

	if (ndlp) {
		lpfc_debugfs_disc_trc(ndlp->vport, LPFC_DISC_TRC_NODE,
			"node get:        did:x%x flg:x%x refcnt:x%x",
			ndlp->nlp_DID, ndlp->nlp_flag,
			kref_read(&ndlp->kref));
		/* The check of ndlp usage to prevent incrementing the
		 * ndlp reference count that is in the process of being
		 * released.
		 */
		phba = ndlp->phba;
		spin_lock_irqsave(&phba->ndlp_lock, flags);
		if (!NLP_CHK_NODE_ACT(ndlp) || NLP_CHK_FREE_ACK(ndlp)) {
			spin_unlock_irqrestore(&phba->ndlp_lock, flags);
			lpfc_printf_vlog(ndlp->vport, KERN_WARNING, LOG_NODE,
				"0276 lpfc_nlp_get: ndlp:x%p "
				"usgmap:x%x refcnt:%d\n",
				(void *)ndlp, ndlp->nlp_usg_map,
				kref_read(&ndlp->kref));
			return NULL;
		} else
			kref_get(&ndlp->kref);
		spin_unlock_irqrestore(&phba->ndlp_lock, flags);
	}
	return ndlp;
}

/* This routine decrements the reference count for a ndlp structure. If the
 * count goes to 0, this indicates the the associated nodelist should be
 * freed. Returning 1 indicates the ndlp resource has been released; on the
 * other hand, returning 0 indicates the ndlp resource has not been released
 * yet.
 */
int
lpfc_nlp_put(struct lpfc_nodelist *ndlp)
{
	struct lpfc_hba *phba;
	unsigned long flags;

	if (!ndlp)
		return 1;

	lpfc_debugfs_disc_trc(ndlp->vport, LPFC_DISC_TRC_NODE,
	"node put:        did:x%x flg:x%x refcnt:x%x",
		ndlp->nlp_DID, ndlp->nlp_flag,
		kref_read(&ndlp->kref));
	phba = ndlp->phba;
	spin_lock_irqsave(&phba->ndlp_lock, flags);
	/* Check the ndlp memory free acknowledge flag to avoid the
	 * possible race condition that kref_put got invoked again
	 * after previous one has done ndlp memory free.
	 */
	if (NLP_CHK_FREE_ACK(ndlp)) {
		spin_unlock_irqrestore(&phba->ndlp_lock, flags);
		lpfc_printf_vlog(ndlp->vport, KERN_WARNING, LOG_NODE,
				"0274 lpfc_nlp_put: ndlp:x%p "
				"usgmap:x%x refcnt:%d\n",
				(void *)ndlp, ndlp->nlp_usg_map,
				kref_read(&ndlp->kref));
		return 1;
	}
	/* Check the ndlp inactivate log flag to avoid the possible
	 * race condition that kref_put got invoked again after ndlp
	 * is already in inactivating state.
	 */
	if (NLP_CHK_IACT_REQ(ndlp)) {
		spin_unlock_irqrestore(&phba->ndlp_lock, flags);
		lpfc_printf_vlog(ndlp->vport, KERN_WARNING, LOG_NODE,
				"0275 lpfc_nlp_put: ndlp:x%p "
				"usgmap:x%x refcnt:%d\n",
				(void *)ndlp, ndlp->nlp_usg_map,
				kref_read(&ndlp->kref));
		return 1;
	}
	/* For last put, mark the ndlp usage flags to make sure no
	 * other kref_get and kref_put on the same ndlp shall get
	 * in between the process when the final kref_put has been
	 * invoked on this ndlp.
	 */
	if (kref_read(&ndlp->kref) == 1) {
		/* Indicate ndlp is put to inactive state. */
		NLP_SET_IACT_REQ(ndlp);
		/* Acknowledge ndlp memory free has been seen. */
		if (NLP_CHK_FREE_REQ(ndlp))
			NLP_SET_FREE_ACK(ndlp);
	}
	spin_unlock_irqrestore(&phba->ndlp_lock, flags);
	/* Note, the kref_put returns 1 when decrementing a reference
	 * count that was 1, it invokes the release callback function,
	 * but it still left the reference count as 1 (not actually
	 * performs the last decrementation). Otherwise, it actually
	 * decrements the reference count and returns 0.
	 */
	return kref_put(&ndlp->kref, lpfc_nlp_release);
}

/* This routine free's the specified nodelist if it is not in use
 * by any other discovery thread. This routine returns 1 if the
 * ndlp has been freed. A return value of 0 indicates the ndlp is
 * not yet been released.
 */
int
lpfc_nlp_not_used(struct lpfc_nodelist *ndlp)
{
	lpfc_debugfs_disc_trc(ndlp->vport, LPFC_DISC_TRC_NODE,
		"node not used:   did:x%x flg:x%x refcnt:x%x",
		ndlp->nlp_DID, ndlp->nlp_flag,
		kref_read(&ndlp->kref));
	if (kref_read(&ndlp->kref) == 1)
		if (lpfc_nlp_put(ndlp))
			return 1;
	return 0;
}

/**
 * lpfc_fcf_inuse - Check if FCF can be unregistered.
 * @phba: Pointer to hba context object.
 *
 * This function iterate through all FC nodes associated
 * will all vports to check if there is any node with
 * fc_rports associated with it. If there is an fc_rport
 * associated with the node, then the node is either in
 * discovered state or its devloss_timer is pending.
 */
static int
lpfc_fcf_inuse(struct lpfc_hba *phba)
{
	struct lpfc_vport **vports;
	int i, ret = 0;
	struct lpfc_nodelist *ndlp;
	struct Scsi_Host  *shost;

	vports = lpfc_create_vport_work_array(phba);

	/* If driver cannot allocate memory, indicate fcf is in use */
	if (!vports)
		return 1;

	for (i = 0; i <= phba->max_vports && vports[i] != NULL; i++) {
		shost = lpfc_shost_from_vport(vports[i]);
		spin_lock_irq(shost->host_lock);
		/*
		 * IF the CVL_RCVD bit is not set then we have sent the
		 * flogi.
		 * If dev_loss fires while we are waiting we do not want to
		 * unreg the fcf.
		 */
		if (!(vports[i]->fc_flag & FC_VPORT_CVL_RCVD)) {
			spin_unlock_irq(shost->host_lock);
			ret =  1;
			goto out;
		}
		list_for_each_entry(ndlp, &vports[i]->fc_nodes, nlp_listp) {
			if (NLP_CHK_NODE_ACT(ndlp) && ndlp->rport &&
			  (ndlp->rport->roles & FC_RPORT_ROLE_FCP_TARGET)) {
				ret = 1;
				spin_unlock_irq(shost->host_lock);
				goto out;
			} else if (ndlp->nlp_flag & NLP_RPI_REGISTERED) {
				ret = 1;
				lpfc_printf_log(phba, KERN_INFO, LOG_ELS,
						"2624 RPI %x DID %x flag %x "
						"still logged in\n",
						ndlp->nlp_rpi, ndlp->nlp_DID,
						ndlp->nlp_flag);
			}
		}
		spin_unlock_irq(shost->host_lock);
	}
out:
	lpfc_destroy_vport_work_array(phba, vports);
	return ret;
}

/**
 * lpfc_unregister_vfi_cmpl - Completion handler for unreg vfi.
 * @phba: Pointer to hba context object.
 * @mboxq: Pointer to mailbox object.
 *
 * This function frees memory associated with the mailbox command.
 */
void
lpfc_unregister_vfi_cmpl(struct lpfc_hba *phba, LPFC_MBOXQ_t *mboxq)
{
	struct lpfc_vport *vport = mboxq->vport;
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);

	if (mboxq->u.mb.mbxStatus) {
		lpfc_printf_log(phba, KERN_ERR, LOG_DISCOVERY|LOG_MBOX,
			"2555 UNREG_VFI mbxStatus error x%x "
			"HBA state x%x\n",
			mboxq->u.mb.mbxStatus, vport->port_state);
	}
	spin_lock_irq(shost->host_lock);
	phba->pport->fc_flag &= ~FC_VFI_REGISTERED;
	spin_unlock_irq(shost->host_lock);
	mempool_free(mboxq, phba->mbox_mem_pool);
	return;
}

/**
 * lpfc_unregister_fcfi_cmpl - Completion handler for unreg fcfi.
 * @phba: Pointer to hba context object.
 * @mboxq: Pointer to mailbox object.
 *
 * This function frees memory associated with the mailbox command.
 */
static void
lpfc_unregister_fcfi_cmpl(struct lpfc_hba *phba, LPFC_MBOXQ_t *mboxq)
{
	struct lpfc_vport *vport = mboxq->vport;

	if (mboxq->u.mb.mbxStatus) {
		lpfc_printf_log(phba, KERN_ERR, LOG_DISCOVERY|LOG_MBOX,
			"2550 UNREG_FCFI mbxStatus error x%x "
			"HBA state x%x\n",
			mboxq->u.mb.mbxStatus, vport->port_state);
	}
	mempool_free(mboxq, phba->mbox_mem_pool);
	return;
}

/**
 * lpfc_unregister_fcf_prep - Unregister fcf record preparation
 * @phba: Pointer to hba context object.
 *
 * This function prepare the HBA for unregistering the currently registered
 * FCF from the HBA. It performs unregistering, in order, RPIs, VPIs, and
 * VFIs.
 */
int
lpfc_unregister_fcf_prep(struct lpfc_hba *phba)
{
	struct lpfc_vport **vports;
	struct lpfc_nodelist *ndlp;
	struct Scsi_Host *shost;
	int i = 0, rc;

	/* Unregister RPIs */
	if (lpfc_fcf_inuse(phba))
		lpfc_unreg_hba_rpis(phba);

	/* At this point, all discovery is aborted */
	phba->pport->port_state = LPFC_VPORT_UNKNOWN;

	/* Unregister VPIs */
	vports = lpfc_create_vport_work_array(phba);
	if (vports && (phba->sli3_options & LPFC_SLI3_NPIV_ENABLED))
		for (i = 0; i <= phba->max_vports && vports[i] != NULL; i++) {
			/* Stop FLOGI/FDISC retries */
			ndlp = lpfc_findnode_did(vports[i], Fabric_DID);
			if (ndlp)
				lpfc_cancel_retry_delay_tmo(vports[i], ndlp);
			lpfc_cleanup_pending_mbox(vports[i]);
			if (phba->sli_rev == LPFC_SLI_REV4)
				lpfc_sli4_unreg_all_rpis(vports[i]);
			lpfc_mbx_unreg_vpi(vports[i]);
			shost = lpfc_shost_from_vport(vports[i]);
			spin_lock_irq(shost->host_lock);
			vports[i]->fc_flag |= FC_VPORT_NEEDS_INIT_VPI;
			vports[i]->vpi_state &= ~LPFC_VPI_REGISTERED;
			spin_unlock_irq(shost->host_lock);
		}
	lpfc_destroy_vport_work_array(phba, vports);
	if (i == 0 && (!(phba->sli3_options & LPFC_SLI3_NPIV_ENABLED))) {
		ndlp = lpfc_findnode_did(phba->pport, Fabric_DID);
		if (ndlp)
			lpfc_cancel_retry_delay_tmo(phba->pport, ndlp);
		lpfc_cleanup_pending_mbox(phba->pport);
		if (phba->sli_rev == LPFC_SLI_REV4)
			lpfc_sli4_unreg_all_rpis(phba->pport);
		lpfc_mbx_unreg_vpi(phba->pport);
		shost = lpfc_shost_from_vport(phba->pport);
		spin_lock_irq(shost->host_lock);
		phba->pport->fc_flag |= FC_VPORT_NEEDS_INIT_VPI;
		phba->pport->vpi_state &= ~LPFC_VPI_REGISTERED;
		spin_unlock_irq(shost->host_lock);
	}

	/* Cleanup any outstanding ELS commands */
	lpfc_els_flush_all_cmd(phba);

	/* Unregister the physical port VFI */
	rc = lpfc_issue_unreg_vfi(phba->pport);
	return rc;
}

/**
 * lpfc_sli4_unregister_fcf - Unregister currently registered FCF record
 * @phba: Pointer to hba context object.
 *
 * This function issues synchronous unregister FCF mailbox command to HBA to
 * unregister the currently registered FCF record. The driver does not reset
 * the driver FCF usage state flags.
 *
 * Return 0 if successfully issued, none-zero otherwise.
 */
int
lpfc_sli4_unregister_fcf(struct lpfc_hba *phba)
{
	LPFC_MBOXQ_t *mbox;
	int rc;

	mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!mbox) {
		lpfc_printf_log(phba, KERN_ERR, LOG_DISCOVERY|LOG_MBOX,
				"2551 UNREG_FCFI mbox allocation failed"
				"HBA state x%x\n", phba->pport->port_state);
		return -ENOMEM;
	}
	lpfc_unreg_fcfi(mbox, phba->fcf.fcfi);
	mbox->vport = phba->pport;
	mbox->mbox_cmpl = lpfc_unregister_fcfi_cmpl;
	rc = lpfc_sli_issue_mbox(phba, mbox, MBX_NOWAIT);

	if (rc == MBX_NOT_FINISHED) {
		lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
				"2552 Unregister FCFI command failed rc x%x "
				"HBA state x%x\n",
				rc, phba->pport->port_state);
		return -EINVAL;
	}
	return 0;
}

/**
 * lpfc_unregister_fcf_rescan - Unregister currently registered fcf and rescan
 * @phba: Pointer to hba context object.
 *
 * This function unregisters the currently reigstered FCF. This function
 * also tries to find another FCF for discovery by rescan the HBA FCF table.
 */
void
lpfc_unregister_fcf_rescan(struct lpfc_hba *phba)
{
	int rc;

	/* Preparation for unregistering fcf */
	rc = lpfc_unregister_fcf_prep(phba);
	if (rc) {
		lpfc_printf_log(phba, KERN_ERR, LOG_DISCOVERY,
				"2748 Failed to prepare for unregistering "
				"HBA's FCF record: rc=%d\n", rc);
		return;
	}

	/* Now, unregister FCF record and reset HBA FCF state */
	rc = lpfc_sli4_unregister_fcf(phba);
	if (rc)
		return;
	/* Reset HBA FCF states after successful unregister FCF */
	phba->fcf.fcf_flag = 0;
	phba->fcf.current_rec.flag = 0;

	/*
	 * If driver is not unloading, check if there is any other
	 * FCF record that can be used for discovery.
	 */
	if ((phba->pport->load_flag & FC_UNLOADING) ||
	    (phba->link_state < LPFC_LINK_UP))
		return;

	/* This is considered as the initial FCF discovery scan */
	spin_lock_irq(&phba->hbalock);
	phba->fcf.fcf_flag |= FCF_INIT_DISC;
	spin_unlock_irq(&phba->hbalock);

	/* Reset FCF roundrobin bmask for new discovery */
	lpfc_sli4_clear_fcf_rr_bmask(phba);

	rc = lpfc_sli4_fcf_scan_read_fcf_rec(phba, LPFC_FCOE_FCF_GET_FIRST);

	if (rc) {
		spin_lock_irq(&phba->hbalock);
		phba->fcf.fcf_flag &= ~FCF_INIT_DISC;
		spin_unlock_irq(&phba->hbalock);
		lpfc_printf_log(phba, KERN_ERR, LOG_DISCOVERY|LOG_MBOX,
				"2553 lpfc_unregister_unused_fcf failed "
				"to read FCF record HBA state x%x\n",
				phba->pport->port_state);
	}
}

/**
 * lpfc_unregister_fcf - Unregister the currently registered fcf record
 * @phba: Pointer to hba context object.
 *
 * This function just unregisters the currently reigstered FCF. It does not
 * try to find another FCF for discovery.
 */
void
lpfc_unregister_fcf(struct lpfc_hba *phba)
{
	int rc;

	/* Preparation for unregistering fcf */
	rc = lpfc_unregister_fcf_prep(phba);
	if (rc) {
		lpfc_printf_log(phba, KERN_ERR, LOG_DISCOVERY,
				"2749 Failed to prepare for unregistering "
				"HBA's FCF record: rc=%d\n", rc);
		return;
	}

	/* Now, unregister FCF record and reset HBA FCF state */
	rc = lpfc_sli4_unregister_fcf(phba);
	if (rc)
		return;
	/* Set proper HBA FCF states after successful unregister FCF */
	spin_lock_irq(&phba->hbalock);
	phba->fcf.fcf_flag &= ~FCF_REGISTERED;
	spin_unlock_irq(&phba->hbalock);
}

/**
 * lpfc_unregister_unused_fcf - Unregister FCF if all devices are disconnected.
 * @phba: Pointer to hba context object.
 *
 * This function check if there are any connected remote port for the FCF and
 * if all the devices are disconnected, this function unregister FCFI.
 * This function also tries to use another FCF for discovery.
 */
void
lpfc_unregister_unused_fcf(struct lpfc_hba *phba)
{
	/*
	 * If HBA is not running in FIP mode, if HBA does not support
	 * FCoE, if FCF discovery is ongoing, or if FCF has not been
	 * registered, do nothing.
	 */
	spin_lock_irq(&phba->hbalock);
	if (!(phba->hba_flag & HBA_FCOE_MODE) ||
	    !(phba->fcf.fcf_flag & FCF_REGISTERED) ||
	    !(phba->hba_flag & HBA_FIP_SUPPORT) ||
	    (phba->fcf.fcf_flag & FCF_DISCOVERY) ||
	    (phba->pport->port_state == LPFC_FLOGI)) {
		spin_unlock_irq(&phba->hbalock);
		return;
	}
	spin_unlock_irq(&phba->hbalock);

	if (lpfc_fcf_inuse(phba))
		return;

	lpfc_unregister_fcf_rescan(phba);
}

/**
 * lpfc_read_fcf_conn_tbl - Create driver FCF connection table.
 * @phba: Pointer to hba context object.
 * @buff: Buffer containing the FCF connection table as in the config
 *         region.
 * This function create driver data structure for the FCF connection
 * record table read from config region 23.
 */
static void
lpfc_read_fcf_conn_tbl(struct lpfc_hba *phba,
	uint8_t *buff)
{
	struct lpfc_fcf_conn_entry *conn_entry, *next_conn_entry;
	struct lpfc_fcf_conn_hdr *conn_hdr;
	struct lpfc_fcf_conn_rec *conn_rec;
	uint32_t record_count;
	int i;

	/* Free the current connect table */
	list_for_each_entry_safe(conn_entry, next_conn_entry,
		&phba->fcf_conn_rec_list, list) {
		list_del_init(&conn_entry->list);
		kfree(conn_entry);
	}

	conn_hdr = (struct lpfc_fcf_conn_hdr *) buff;
	record_count = conn_hdr->length * sizeof(uint32_t)/
		sizeof(struct lpfc_fcf_conn_rec);

	conn_rec = (struct lpfc_fcf_conn_rec *)
		(buff + sizeof(struct lpfc_fcf_conn_hdr));

	for (i = 0; i < record_count; i++) {
		if (!(conn_rec[i].flags & FCFCNCT_VALID))
			continue;
		conn_entry = kzalloc(sizeof(struct lpfc_fcf_conn_entry),
			GFP_KERNEL);
		if (!conn_entry) {
			lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"2566 Failed to allocate connection"
				" table entry\n");
			return;
		}

		memcpy(&conn_entry->conn_rec, &conn_rec[i],
			sizeof(struct lpfc_fcf_conn_rec));
		list_add_tail(&conn_entry->list,
			&phba->fcf_conn_rec_list);
	}

	if (!list_empty(&phba->fcf_conn_rec_list)) {
		i = 0;
		list_for_each_entry(conn_entry, &phba->fcf_conn_rec_list,
				    list) {
			conn_rec = &conn_entry->conn_rec;
			lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
					"3345 FCF connection list rec[%02d]: "
					"flags:x%04x, vtag:x%04x, "
					"fabric_name:x%02x:%02x:%02x:%02x:"
					"%02x:%02x:%02x:%02x, "
					"switch_name:x%02x:%02x:%02x:%02x:"
					"%02x:%02x:%02x:%02x\n", i++,
					conn_rec->flags, conn_rec->vlan_tag,
					conn_rec->fabric_name[0],
					conn_rec->fabric_name[1],
					conn_rec->fabric_name[2],
					conn_rec->fabric_name[3],
					conn_rec->fabric_name[4],
					conn_rec->fabric_name[5],
					conn_rec->fabric_name[6],
					conn_rec->fabric_name[7],
					conn_rec->switch_name[0],
					conn_rec->switch_name[1],
					conn_rec->switch_name[2],
					conn_rec->switch_name[3],
					conn_rec->switch_name[4],
					conn_rec->switch_name[5],
					conn_rec->switch_name[6],
					conn_rec->switch_name[7]);
		}
	}
}

/**
 * lpfc_read_fcoe_param - Read FCoe parameters from conf region..
 * @phba: Pointer to hba context object.
 * @buff: Buffer containing the FCoE parameter data structure.
 *
 *  This function update driver data structure with config
 *  parameters read from config region 23.
 */
static void
lpfc_read_fcoe_param(struct lpfc_hba *phba,
			uint8_t *buff)
{
	struct lpfc_fip_param_hdr *fcoe_param_hdr;
	struct lpfc_fcoe_params *fcoe_param;

	fcoe_param_hdr = (struct lpfc_fip_param_hdr *)
		buff;
	fcoe_param = (struct lpfc_fcoe_params *)
		(buff + sizeof(struct lpfc_fip_param_hdr));

	if ((fcoe_param_hdr->parm_version != FIPP_VERSION) ||
		(fcoe_param_hdr->length != FCOE_PARAM_LENGTH))
		return;

	if (fcoe_param_hdr->parm_flags & FIPP_VLAN_VALID) {
		phba->valid_vlan = 1;
		phba->vlan_id = le16_to_cpu(fcoe_param->vlan_tag) &
			0xFFF;
	}

	phba->fc_map[0] = fcoe_param->fc_map[0];
	phba->fc_map[1] = fcoe_param->fc_map[1];
	phba->fc_map[2] = fcoe_param->fc_map[2];
	return;
}

/**
 * lpfc_get_rec_conf23 - Get a record type in config region data.
 * @buff: Buffer containing config region 23 data.
 * @size: Size of the data buffer.
 * @rec_type: Record type to be searched.
 *
 * This function searches config region data to find the beginning
 * of the record specified by record_type. If record found, this
 * function return pointer to the record else return NULL.
 */
static uint8_t *
lpfc_get_rec_conf23(uint8_t *buff, uint32_t size, uint8_t rec_type)
{
	uint32_t offset = 0, rec_length;

	if ((buff[0] == LPFC_REGION23_LAST_REC) ||
		(size < sizeof(uint32_t)))
		return NULL;

	rec_length = buff[offset + 1];

	/*
	 * One TLV record has one word header and number of data words
	 * specified in the rec_length field of the record header.
	 */
	while ((offset + rec_length * sizeof(uint32_t) + sizeof(uint32_t))
		<= size) {
		if (buff[offset] == rec_type)
			return &buff[offset];

		if (buff[offset] == LPFC_REGION23_LAST_REC)
			return NULL;

		offset += rec_length * sizeof(uint32_t) + sizeof(uint32_t);
		rec_length = buff[offset + 1];
	}
	return NULL;
}

/**
 * lpfc_parse_fcoe_conf - Parse FCoE config data read from config region 23.
 * @phba: Pointer to lpfc_hba data structure.
 * @buff: Buffer containing config region 23 data.
 * @size: Size of the data buffer.
 *
 * This function parses the FCoE config parameters in config region 23 and
 * populate driver data structure with the parameters.
 */
void
lpfc_parse_fcoe_conf(struct lpfc_hba *phba,
		uint8_t *buff,
		uint32_t size)
{
	uint32_t offset = 0;
	uint8_t *rec_ptr;

	/*
	 * If data size is less than 2 words signature and version cannot be
	 * verified.
	 */
	if (size < 2*sizeof(uint32_t))
		return;

	/* Check the region signature first */
	if (memcmp(buff, LPFC_REGION23_SIGNATURE, 4)) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
			"2567 Config region 23 has bad signature\n");
		return;
	}

	offset += 4;

	/* Check the data structure version */
	if (buff[offset] != LPFC_REGION23_VERSION) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
			"2568 Config region 23 has bad version\n");
		return;
	}
	offset += 4;

	/* Read FCoE param record */
	rec_ptr = lpfc_get_rec_conf23(&buff[offset],
			size - offset, FCOE_PARAM_TYPE);
	if (rec_ptr)
		lpfc_read_fcoe_param(phba, rec_ptr);

	/* Read FCF connection table */
	rec_ptr = lpfc_get_rec_conf23(&buff[offset],
		size - offset, FCOE_CONN_TBL_TYPE);
	if (rec_ptr)
		lpfc_read_fcf_conn_tbl(phba, rec_ptr);

}
