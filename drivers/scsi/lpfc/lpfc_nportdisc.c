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
#include <linux/interrupt.h>

#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport_fc.h>

#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_disc.h"
#include "lpfc_scsi.h"
#include "lpfc.h"
#include "lpfc_logmsg.h"
#include "lpfc_crtn.h"


/* Called to verify a rcv'ed ADISC was intended for us. */
static int
lpfc_check_adisc(struct lpfc_hba * phba, struct lpfc_nodelist * ndlp,
		 struct lpfc_name * nn, struct lpfc_name * pn)
{
	/* Compare the ADISC rsp WWNN / WWPN matches our internal node
	 * table entry for that node.
	 */
	if (memcmp(nn, &ndlp->nlp_nodename, sizeof (struct lpfc_name)) != 0)
		return 0;

	if (memcmp(pn, &ndlp->nlp_portname, sizeof (struct lpfc_name)) != 0)
		return 0;

	/* we match, return success */
	return 1;
}

int
lpfc_check_sparm(struct lpfc_hba * phba,
		 struct lpfc_nodelist * ndlp, struct serv_parm * sp,
		 uint32_t class)
{
	volatile struct serv_parm *hsp = &phba->fc_sparam;
	uint16_t hsp_value, ssp_value = 0;

	/*
	 * The receive data field size and buffer-to-buffer receive data field
	 * size entries are 16 bits but are represented as two 8-bit fields in
	 * the driver data structure to account for rsvd bits and other control
	 * bits.  Reconstruct and compare the fields as a 16-bit values before
	 * correcting the byte values.
	 */
	if (sp->cls1.classValid) {
		hsp_value = (hsp->cls1.rcvDataSizeMsb << 8) |
				hsp->cls1.rcvDataSizeLsb;
		ssp_value = (sp->cls1.rcvDataSizeMsb << 8) |
				sp->cls1.rcvDataSizeLsb;
		if (ssp_value > hsp_value) {
			sp->cls1.rcvDataSizeLsb = hsp->cls1.rcvDataSizeLsb;
			sp->cls1.rcvDataSizeMsb = hsp->cls1.rcvDataSizeMsb;
		}
	} else if (class == CLASS1) {
		return 0;
	}

	if (sp->cls2.classValid) {
		hsp_value = (hsp->cls2.rcvDataSizeMsb << 8) |
				hsp->cls2.rcvDataSizeLsb;
		ssp_value = (sp->cls2.rcvDataSizeMsb << 8) |
				sp->cls2.rcvDataSizeLsb;
		if (ssp_value > hsp_value) {
			sp->cls2.rcvDataSizeLsb = hsp->cls2.rcvDataSizeLsb;
			sp->cls2.rcvDataSizeMsb = hsp->cls2.rcvDataSizeMsb;
		}
	} else if (class == CLASS2) {
		return 0;
	}

	if (sp->cls3.classValid) {
		hsp_value = (hsp->cls3.rcvDataSizeMsb << 8) |
				hsp->cls3.rcvDataSizeLsb;
		ssp_value = (sp->cls3.rcvDataSizeMsb << 8) |
				sp->cls3.rcvDataSizeLsb;
		if (ssp_value > hsp_value) {
			sp->cls3.rcvDataSizeLsb = hsp->cls3.rcvDataSizeLsb;
			sp->cls3.rcvDataSizeMsb = hsp->cls3.rcvDataSizeMsb;
		}
	} else if (class == CLASS3) {
		return 0;
	}

	/*
	 * Preserve the upper four bits of the MSB from the PLOGI response.
	 * These bits contain the Buffer-to-Buffer State Change Number
	 * from the target and need to be passed to the FW.
	 */
	hsp_value = (hsp->cmn.bbRcvSizeMsb << 8) | hsp->cmn.bbRcvSizeLsb;
	ssp_value = (sp->cmn.bbRcvSizeMsb << 8) | sp->cmn.bbRcvSizeLsb;
	if (ssp_value > hsp_value) {
		sp->cmn.bbRcvSizeLsb = hsp->cmn.bbRcvSizeLsb;
		sp->cmn.bbRcvSizeMsb = (sp->cmn.bbRcvSizeMsb & 0xF0) |
				       (hsp->cmn.bbRcvSizeMsb & 0x0F);
	}

	memcpy(&ndlp->nlp_nodename, &sp->nodeName, sizeof (struct lpfc_name));
	memcpy(&ndlp->nlp_portname, &sp->portName, sizeof (struct lpfc_name));
	return 1;
}

static void *
lpfc_check_elscmpl_iocb(struct lpfc_hba * phba,
		      struct lpfc_iocbq *cmdiocb,
		      struct lpfc_iocbq *rspiocb)
{
	struct lpfc_dmabuf *pcmd, *prsp;
	uint32_t *lp;
	void     *ptr = NULL;
	IOCB_t   *irsp;

	irsp = &rspiocb->iocb;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;

	/* For lpfc_els_abort, context2 could be zero'ed to delay
	 * freeing associated memory till after ABTS completes.
	 */
	if (pcmd) {
		prsp =  list_get_first(&pcmd->list, struct lpfc_dmabuf,
				       list);
		if (prsp) {
			lp = (uint32_t *) prsp->virt;
			ptr = (void *)((uint8_t *)lp + sizeof(uint32_t));
		}
	} else {
		/* Force ulpStatus error since we are returning NULL ptr */
		if (!(irsp->ulpStatus)) {
			irsp->ulpStatus = IOSTAT_LOCAL_REJECT;
			irsp->un.ulpWord[4] = IOERR_SLI_ABORTED;
		}
		ptr = NULL;
	}
	return ptr;
}


/*
 * Free resources / clean up outstanding I/Os
 * associated with a LPFC_NODELIST entry. This
 * routine effectively results in a "software abort".
 */
int
lpfc_els_abort(struct lpfc_hba * phba, struct lpfc_nodelist * ndlp,
	int send_abts)
{
	struct lpfc_sli *psli;
	struct lpfc_sli_ring *pring;
	struct lpfc_iocbq *iocb, *next_iocb;
	IOCB_t *icmd;
	int    found = 0;

	/* Abort outstanding I/O on NPort <nlp_DID> */
	lpfc_printf_log(phba, KERN_INFO, LOG_DISCOVERY,
			"%d:0205 Abort outstanding I/O on NPort x%x "
			"Data: x%x x%x x%x\n",
			phba->brd_no, ndlp->nlp_DID, ndlp->nlp_flag,
			ndlp->nlp_state, ndlp->nlp_rpi);

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];

	/* First check the txq */
	do {
		found = 0;
		spin_lock_irq(phba->host->host_lock);
		list_for_each_entry_safe(iocb, next_iocb, &pring->txq, list) {
			/* Check to see if iocb matches the nport we are looking
			   for */
			if ((lpfc_check_sli_ndlp(phba, pring, iocb, ndlp))) {
				found = 1;
				/* It matches, so deque and call compl with an
				   error */
				list_del(&iocb->list);
				pring->txq_cnt--;
				if (iocb->iocb_cmpl) {
					icmd = &iocb->iocb;
					icmd->ulpStatus = IOSTAT_LOCAL_REJECT;
					icmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
					spin_unlock_irq(phba->host->host_lock);
					(iocb->iocb_cmpl) (phba, iocb, iocb);
					spin_lock_irq(phba->host->host_lock);
				} else
					lpfc_sli_release_iocbq(phba, iocb);
				break;
			}
		}
		spin_unlock_irq(phba->host->host_lock);
	} while (found);

	/* Everything on txcmplq will be returned by firmware
	 * with a no rpi / linkdown / abort error.  For ring 0,
	 * ELS discovery, we want to get rid of it right here.
	 */
	/* Next check the txcmplq */
	do {
		found = 0;
		spin_lock_irq(phba->host->host_lock);
		list_for_each_entry_safe(iocb, next_iocb, &pring->txcmplq,
					 list) {
			/* Check to see if iocb matches the nport we are looking
			   for */
			if ((lpfc_check_sli_ndlp (phba, pring, iocb, ndlp))) {
				found = 1;
				/* It matches, so deque and call compl with an
				   error */
				list_del(&iocb->list);
				pring->txcmplq_cnt--;

				icmd = &iocb->iocb;
				/* If the driver is completing an ELS
				 * command early, flush it out of the firmware.
				 */
				if (send_abts &&
				   (icmd->ulpCommand == CMD_ELS_REQUEST64_CR) &&
				   (icmd->un.elsreq64.bdl.ulpIoTag32)) {
					lpfc_sli_issue_abort_iotag32(phba,
							     pring, iocb);
				}
				if (iocb->iocb_cmpl) {
					icmd->ulpStatus = IOSTAT_LOCAL_REJECT;
					icmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
					spin_unlock_irq(phba->host->host_lock);
					(iocb->iocb_cmpl) (phba, iocb, iocb);
					spin_lock_irq(phba->host->host_lock);
				} else
					lpfc_sli_release_iocbq(phba, iocb);
				break;
			}
		}
		spin_unlock_irq(phba->host->host_lock);
	} while(found);

	/* If we are delaying issuing an ELS command, cancel it */
	if (ndlp->nlp_flag & NLP_DELAY_TMO)
		lpfc_cancel_retry_delay_tmo(phba, ndlp);
	return 0;
}

static int
lpfc_rcv_plogi(struct lpfc_hba * phba,
		      struct lpfc_nodelist * ndlp,
		      struct lpfc_iocbq *cmdiocb)
{
	struct lpfc_dmabuf *pcmd;
	uint32_t *lp;
	IOCB_t *icmd;
	struct serv_parm *sp;
	LPFC_MBOXQ_t *mbox;
	struct ls_rjt stat;
	int rc;

	memset(&stat, 0, sizeof (struct ls_rjt));
	if (phba->hba_state <= LPFC_FLOGI) {
		/* Before responding to PLOGI, check for pt2pt mode.
		 * If we are pt2pt, with an outstanding FLOGI, abort
		 * the FLOGI and resend it first.
		 */
		if (phba->fc_flag & FC_PT2PT) {
			lpfc_els_abort_flogi(phba);
		        if (!(phba->fc_flag & FC_PT2PT_PLOGI)) {
				/* If the other side is supposed to initiate
				 * the PLOGI anyway, just ACC it now and
				 * move on with discovery.
				 */
				phba->fc_edtov = FF_DEF_EDTOV;
				phba->fc_ratov = FF_DEF_RATOV;
				/* Start discovery - this should just do
				   CLEAR_LA */
				lpfc_disc_start(phba);
			} else {
				lpfc_initial_flogi(phba);
			}
		} else {
			stat.un.b.lsRjtRsnCode = LSRJT_LOGICAL_BSY;
			stat.un.b.lsRjtRsnCodeExp = LSEXP_NOTHING_MORE;
			lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb,
					    ndlp);
			return 0;
		}
	}
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;
	lp = (uint32_t *) pcmd->virt;
	sp = (struct serv_parm *) ((uint8_t *) lp + sizeof (uint32_t));
	if ((lpfc_check_sparm(phba, ndlp, sp, CLASS3) == 0)) {
		/* Reject this request because invalid parameters */
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_SPARM_OPTIONS;
		lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);
		return 0;
	}
	icmd = &cmdiocb->iocb;

	/* PLOGI chkparm OK */
	lpfc_printf_log(phba,
			KERN_INFO,
			LOG_ELS,
			"%d:0114 PLOGI chkparm OK Data: x%x x%x x%x x%x\n",
			phba->brd_no,
			ndlp->nlp_DID, ndlp->nlp_state, ndlp->nlp_flag,
			ndlp->nlp_rpi);

	if ((phba->cfg_fcp_class == 2) &&
	    (sp->cls2.classValid)) {
		ndlp->nlp_fcp_info |= CLASS2;
	} else {
		ndlp->nlp_fcp_info |= CLASS3;
	}
	ndlp->nlp_class_sup = 0;
	if (sp->cls1.classValid)
		ndlp->nlp_class_sup |= FC_COS_CLASS1;
	if (sp->cls2.classValid)
		ndlp->nlp_class_sup |= FC_COS_CLASS2;
	if (sp->cls3.classValid)
		ndlp->nlp_class_sup |= FC_COS_CLASS3;
	if (sp->cls4.classValid)
		ndlp->nlp_class_sup |= FC_COS_CLASS4;
	ndlp->nlp_maxframe =
		((sp->cmn.bbRcvSizeMsb & 0x0F) << 8) | sp->cmn.bbRcvSizeLsb;

	/* no need to reg_login if we are already in one of these states */
	switch (ndlp->nlp_state) {
	case  NLP_STE_NPR_NODE:
		if (!(ndlp->nlp_flag & NLP_NPR_ADISC))
			break;
	case  NLP_STE_REG_LOGIN_ISSUE:
	case  NLP_STE_PRLI_ISSUE:
	case  NLP_STE_UNMAPPED_NODE:
	case  NLP_STE_MAPPED_NODE:
		lpfc_els_rsp_acc(phba, ELS_CMD_PLOGI, cmdiocb, ndlp, NULL, 0);
		return 1;
	}

	if ((phba->fc_flag & FC_PT2PT)
	    && !(phba->fc_flag & FC_PT2PT_PLOGI)) {
		/* rcv'ed PLOGI decides what our NPortId will be */
		phba->fc_myDID = icmd->un.rcvels.parmRo;
		mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
		if (mbox == NULL)
			goto out;
		lpfc_config_link(phba, mbox);
		mbox->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
		rc = lpfc_sli_issue_mbox
			(phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB));
		if (rc == MBX_NOT_FINISHED) {
			mempool_free( mbox, phba->mbox_mem_pool);
			goto out;
		}

		lpfc_can_disctmo(phba);
	}
	mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (mbox == NULL)
		goto out;

	if (lpfc_reg_login(phba, icmd->un.rcvels.remoteID,
			   (uint8_t *) sp, mbox, 0)) {
		mempool_free( mbox, phba->mbox_mem_pool);
		goto out;
	}

	/* ACC PLOGI rsp command needs to execute first,
	 * queue this mbox command to be processed later.
	 */
	mbox->mbox_cmpl = lpfc_mbx_cmpl_reg_login;
	mbox->context2  = ndlp;
	ndlp->nlp_flag |= (NLP_ACC_REGLOGIN | NLP_RCV_PLOGI);

	/*
	 * If there is an outstanding PLOGI issued, abort it before
	 * sending ACC rsp for received PLOGI. If pending plogi
	 * is not canceled here, the plogi will be rejected by
	 * remote port and will be retried. On a configuration with
	 * single discovery thread, this will cause a huge delay in
	 * discovery. Also this will cause multiple state machines
	 * running in parallel for this node.
	 */
	if (ndlp->nlp_state == NLP_STE_PLOGI_ISSUE) {
		/* software abort outstanding PLOGI */
		lpfc_els_abort(phba, ndlp, 1);
	}

	lpfc_els_rsp_acc(phba, ELS_CMD_PLOGI, cmdiocb, ndlp, mbox, 0);
	return 1;

out:
	stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
	stat.un.b.lsRjtRsnCodeExp = LSEXP_OUT_OF_RESOURCE;
	lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);
	return 0;
}

static int
lpfc_rcv_padisc(struct lpfc_hba * phba,
		struct lpfc_nodelist * ndlp,
		struct lpfc_iocbq *cmdiocb)
{
	struct lpfc_dmabuf *pcmd;
	struct serv_parm *sp;
	struct lpfc_name *pnn, *ppn;
	struct ls_rjt stat;
	ADISC *ap;
	IOCB_t *icmd;
	uint32_t *lp;
	uint32_t cmd;

	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;
	lp = (uint32_t *) pcmd->virt;

	cmd = *lp++;
	if (cmd == ELS_CMD_ADISC) {
		ap = (ADISC *) lp;
		pnn = (struct lpfc_name *) & ap->nodeName;
		ppn = (struct lpfc_name *) & ap->portName;
	} else {
		sp = (struct serv_parm *) lp;
		pnn = (struct lpfc_name *) & sp->nodeName;
		ppn = (struct lpfc_name *) & sp->portName;
	}

	icmd = &cmdiocb->iocb;
	if ((icmd->ulpStatus == 0) &&
	    (lpfc_check_adisc(phba, ndlp, pnn, ppn))) {
		if (cmd == ELS_CMD_ADISC) {
			lpfc_els_rsp_adisc_acc(phba, cmdiocb, ndlp);
		} else {
			lpfc_els_rsp_acc(phba, ELS_CMD_PLOGI, cmdiocb, ndlp,
				NULL, 0);
		}
		return 1;
	}
	/* Reject this request because invalid parameters */
	stat.un.b.lsRjtRsvd0 = 0;
	stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
	stat.un.b.lsRjtRsnCodeExp = LSEXP_SPARM_OPTIONS;
	stat.un.b.vendorUnique = 0;
	lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);

	/* 1 sec timeout */
	mod_timer(&ndlp->nlp_delayfunc, jiffies + HZ);

	spin_lock_irq(phba->host->host_lock);
	ndlp->nlp_flag |= NLP_DELAY_TMO;
	spin_unlock_irq(phba->host->host_lock);
	ndlp->nlp_last_elscmd = ELS_CMD_PLOGI;
	ndlp->nlp_prev_state = ndlp->nlp_state;
	ndlp->nlp_state = NLP_STE_NPR_NODE;
	lpfc_nlp_list(phba, ndlp, NLP_NPR_LIST);
	return 0;
}

static int
lpfc_rcv_logo(struct lpfc_hba * phba,
		      struct lpfc_nodelist * ndlp,
		      struct lpfc_iocbq *cmdiocb,
		      uint32_t els_cmd)
{
	/* Put ndlp on NPR list with 1 sec timeout for plogi, ACC logo */
	/* Only call LOGO ACC for first LOGO, this avoids sending unnecessary
	 * PLOGIs during LOGO storms from a device.
	 */
	ndlp->nlp_flag |= NLP_LOGO_ACC;
	if (els_cmd == ELS_CMD_PRLO)
		lpfc_els_rsp_acc(phba, ELS_CMD_PRLO, cmdiocb, ndlp, NULL, 0);
	else
		lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, NULL, 0);

	if (!(ndlp->nlp_type & NLP_FABRIC) ||
		(ndlp->nlp_state == NLP_STE_ADISC_ISSUE)) {
		/* Only try to re-login if this is NOT a Fabric Node */
		mod_timer(&ndlp->nlp_delayfunc, jiffies + HZ * 1);
		spin_lock_irq(phba->host->host_lock);
		ndlp->nlp_flag |= NLP_DELAY_TMO;
		spin_unlock_irq(phba->host->host_lock);

		ndlp->nlp_last_elscmd = ELS_CMD_PLOGI;
		ndlp->nlp_prev_state = ndlp->nlp_state;
		ndlp->nlp_state = NLP_STE_NPR_NODE;
		lpfc_nlp_list(phba, ndlp, NLP_NPR_LIST);
	} else {
		ndlp->nlp_prev_state = ndlp->nlp_state;
		ndlp->nlp_state = NLP_STE_UNUSED_NODE;
		lpfc_nlp_list(phba, ndlp, NLP_UNUSED_LIST);
	}

	spin_lock_irq(phba->host->host_lock);
	ndlp->nlp_flag &= ~NLP_NPR_ADISC;
	spin_unlock_irq(phba->host->host_lock);
	/* The driver has to wait until the ACC completes before it continues
	 * processing the LOGO.  The action will resume in
	 * lpfc_cmpl_els_logo_acc routine. Since part of processing includes an
	 * unreg_login, the driver waits so the ACC does not get aborted.
	 */
	return 0;
}

static void
lpfc_rcv_prli(struct lpfc_hba * phba,
		      struct lpfc_nodelist * ndlp,
		      struct lpfc_iocbq *cmdiocb)
{
	struct lpfc_dmabuf *pcmd;
	uint32_t *lp;
	PRLI *npr;
	struct fc_rport *rport = ndlp->rport;
	u32 roles;

	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;
	lp = (uint32_t *) pcmd->virt;
	npr = (PRLI *) ((uint8_t *) lp + sizeof (uint32_t));

	ndlp->nlp_type &= ~(NLP_FCP_TARGET | NLP_FCP_INITIATOR);
	ndlp->nlp_fcp_info &= ~NLP_FCP_2_DEVICE;
	if ((npr->acceptRspCode == PRLI_REQ_EXECUTED) &&
	    (npr->prliType == PRLI_FCP_TYPE)) {
		if (npr->initiatorFunc)
			ndlp->nlp_type |= NLP_FCP_INITIATOR;
		if (npr->targetFunc)
			ndlp->nlp_type |= NLP_FCP_TARGET;
		if (npr->Retry)
			ndlp->nlp_fcp_info |= NLP_FCP_2_DEVICE;
	}
	if (rport) {
		/* We need to update the rport role values */
		roles = FC_RPORT_ROLE_UNKNOWN;
		if (ndlp->nlp_type & NLP_FCP_INITIATOR)
			roles |= FC_RPORT_ROLE_FCP_INITIATOR;
		if (ndlp->nlp_type & NLP_FCP_TARGET)
			roles |= FC_RPORT_ROLE_FCP_TARGET;
		fc_remote_port_rolechg(rport, roles);
	}
}

static uint32_t
lpfc_disc_set_adisc(struct lpfc_hba * phba,
		      struct lpfc_nodelist * ndlp)
{
	/* Check config parameter use-adisc or FCP-2 */
	if ((phba->cfg_use_adisc == 0) &&
		!(phba->fc_flag & FC_RSCN_MODE)) {
		if (!(ndlp->nlp_fcp_info & NLP_FCP_2_DEVICE))
			return 0;
	}
	spin_lock_irq(phba->host->host_lock);
	ndlp->nlp_flag |= NLP_NPR_ADISC;
	spin_unlock_irq(phba->host->host_lock);
	return 1;
}

static uint32_t
lpfc_disc_illegal(struct lpfc_hba * phba,
		   struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	lpfc_printf_log(phba,
			KERN_ERR,
			LOG_DISCOVERY,
			"%d:0253 Illegal State Transition: node x%x event x%x, "
			"state x%x Data: x%x x%x\n",
			phba->brd_no,
			ndlp->nlp_DID, evt, ndlp->nlp_state, ndlp->nlp_rpi,
			ndlp->nlp_flag);
	return ndlp->nlp_state;
}

/* Start of Discovery State Machine routines */

static uint32_t
lpfc_rcv_plogi_unused_node(struct lpfc_hba * phba,
			   struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;

	if (lpfc_rcv_plogi(phba, ndlp, cmdiocb)) {
		ndlp->nlp_prev_state = NLP_STE_UNUSED_NODE;
		ndlp->nlp_state = NLP_STE_UNUSED_NODE;
		lpfc_nlp_list(phba, ndlp, NLP_UNUSED_LIST);
		return ndlp->nlp_state;
	}
	lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
	return NLP_STE_FREED_NODE;
}

static uint32_t
lpfc_rcv_els_unused_node(struct lpfc_hba * phba,
			 struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	lpfc_issue_els_logo(phba, ndlp, 0);
	lpfc_nlp_list(phba, ndlp, NLP_UNUSED_LIST);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_logo_unused_node(struct lpfc_hba * phba,
			  struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq     *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;

	spin_lock_irq(phba->host->host_lock);
	ndlp->nlp_flag |= NLP_LOGO_ACC;
	spin_unlock_irq(phba->host->host_lock);
	lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, NULL, 0);
	lpfc_nlp_list(phba, ndlp, NLP_UNUSED_LIST);

	return ndlp->nlp_state;
}

static uint32_t
lpfc_cmpl_logo_unused_node(struct lpfc_hba * phba,
			  struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
	return NLP_STE_FREED_NODE;
}

static uint32_t
lpfc_device_rm_unused_node(struct lpfc_hba * phba,
			   struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
	return NLP_STE_FREED_NODE;
}

static uint32_t
lpfc_rcv_plogi_plogi_issue(struct lpfc_hba * phba, struct lpfc_nodelist * ndlp,
			   void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb = arg;
	struct lpfc_dmabuf *pcmd;
	struct serv_parm *sp;
	uint32_t *lp;
	struct ls_rjt stat;
	int port_cmp;

	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;
	lp = (uint32_t *) pcmd->virt;
	sp = (struct serv_parm *) ((uint8_t *) lp + sizeof (uint32_t));

	memset(&stat, 0, sizeof (struct ls_rjt));

	/* For a PLOGI, we only accept if our portname is less
	 * than the remote portname.
	 */
	phba->fc_stat.elsLogiCol++;
	port_cmp = memcmp(&phba->fc_portname, &sp->portName,
			  sizeof (struct lpfc_name));

	if (port_cmp >= 0) {
		/* Reject this request because the remote node will accept
		   ours */
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_CMD_IN_PROGRESS;
		lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);
	} else {
		lpfc_rcv_plogi(phba, ndlp, cmdiocb);
	} /* if our portname was less */

	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_logo_plogi_issue(struct lpfc_hba * phba,
			  struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq     *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;

	/* software abort outstanding PLOGI */
	lpfc_els_abort(phba, ndlp, 1);

	lpfc_rcv_logo(phba, ndlp, cmdiocb, ELS_CMD_LOGO);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_els_plogi_issue(struct lpfc_hba * phba,
			  struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq     *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;

	/* software abort outstanding PLOGI */
	lpfc_els_abort(phba, ndlp, 1);

	if (evt == NLP_EVT_RCV_LOGO) {
		lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, NULL, 0);
	} else {
		lpfc_issue_els_logo(phba, ndlp, 0);
	}

	/* Put ndlp in npr list set plogi timer for 1 sec */
	mod_timer(&ndlp->nlp_delayfunc, jiffies + HZ * 1);
	spin_lock_irq(phba->host->host_lock);
	ndlp->nlp_flag |= NLP_DELAY_TMO;
	spin_unlock_irq(phba->host->host_lock);
	ndlp->nlp_last_elscmd = ELS_CMD_PLOGI;
	ndlp->nlp_prev_state = NLP_STE_PLOGI_ISSUE;
	ndlp->nlp_state = NLP_STE_NPR_NODE;
	lpfc_nlp_list(phba, ndlp, NLP_NPR_LIST);

	return ndlp->nlp_state;
}

static uint32_t
lpfc_cmpl_plogi_plogi_issue(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb, *rspiocb;
	struct lpfc_dmabuf *pcmd, *prsp;
	uint32_t *lp;
	IOCB_t *irsp;
	struct serv_parm *sp;
	LPFC_MBOXQ_t *mbox;

	cmdiocb = (struct lpfc_iocbq *) arg;
	rspiocb = cmdiocb->context_un.rsp_iocb;

	if (ndlp->nlp_flag & NLP_ACC_REGLOGIN) {
		/* Recovery from PLOGI collision logic */
		return ndlp->nlp_state;
	}

	irsp = &rspiocb->iocb;

	if (irsp->ulpStatus)
		goto out;

	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;

	prsp = list_get_first(&pcmd->list,
			      struct lpfc_dmabuf,
			      list);
	lp = (uint32_t *) prsp->virt;

	sp = (struct serv_parm *) ((uint8_t *) lp + sizeof (uint32_t));
	if (!lpfc_check_sparm(phba, ndlp, sp, CLASS3))
		goto out;

	/* PLOGI chkparm OK */
	lpfc_printf_log(phba,
			KERN_INFO,
			LOG_ELS,
			"%d:0121 PLOGI chkparm OK "
			"Data: x%x x%x x%x x%x\n",
			phba->brd_no,
			ndlp->nlp_DID, ndlp->nlp_state,
			ndlp->nlp_flag, ndlp->nlp_rpi);

	if ((phba->cfg_fcp_class == 2) &&
	    (sp->cls2.classValid)) {
		ndlp->nlp_fcp_info |= CLASS2;
	} else {
		ndlp->nlp_fcp_info |= CLASS3;
	}
	ndlp->nlp_class_sup = 0;
	if (sp->cls1.classValid)
		ndlp->nlp_class_sup |= FC_COS_CLASS1;
	if (sp->cls2.classValid)
		ndlp->nlp_class_sup |= FC_COS_CLASS2;
	if (sp->cls3.classValid)
		ndlp->nlp_class_sup |= FC_COS_CLASS3;
	if (sp->cls4.classValid)
		ndlp->nlp_class_sup |= FC_COS_CLASS4;
	ndlp->nlp_maxframe =
		((sp->cmn.bbRcvSizeMsb & 0x0F) << 8) |
		sp->cmn.bbRcvSizeLsb;

	if (!(mbox = mempool_alloc(phba->mbox_mem_pool,
				   GFP_KERNEL)))
		goto out;

	lpfc_unreg_rpi(phba, ndlp);
	if (lpfc_reg_login
	    (phba, irsp->un.elsreq64.remoteID,
	     (uint8_t *) sp, mbox, 0) == 0) {
		switch (ndlp->nlp_DID) {
		case NameServer_DID:
			mbox->mbox_cmpl =
				lpfc_mbx_cmpl_ns_reg_login;
			break;
		case FDMI_DID:
			mbox->mbox_cmpl =
				lpfc_mbx_cmpl_fdmi_reg_login;
			break;
		default:
			mbox->mbox_cmpl =
				lpfc_mbx_cmpl_reg_login;
		}
		mbox->context2 = ndlp;
		if (lpfc_sli_issue_mbox(phba, mbox,
					(MBX_NOWAIT | MBX_STOP_IOCB))
		    != MBX_NOT_FINISHED) {
			ndlp->nlp_state =
				NLP_STE_REG_LOGIN_ISSUE;
			lpfc_nlp_list(phba, ndlp,
				      NLP_REGLOGIN_LIST);
			return ndlp->nlp_state;
		}
		mempool_free(mbox, phba->mbox_mem_pool);
	} else {
		mempool_free(mbox, phba->mbox_mem_pool);
	}


 out:
	/* Free this node since the driver cannot login or has the wrong
	   sparm */
	lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
	return NLP_STE_FREED_NODE;
}

static uint32_t
lpfc_device_rm_plogi_issue(struct lpfc_hba * phba,
			   struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	if(ndlp->nlp_flag & NLP_NPR_2B_DISC) {
		ndlp->nlp_flag |= NLP_NODEV_REMOVE;
		return ndlp->nlp_state;
	}
	else {
		/* software abort outstanding PLOGI */
		lpfc_els_abort(phba, ndlp, 1);

		lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
		return NLP_STE_FREED_NODE;
	}
}

static uint32_t
lpfc_device_recov_plogi_issue(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	/* software abort outstanding PLOGI */
	lpfc_els_abort(phba, ndlp, 1);

	ndlp->nlp_prev_state = NLP_STE_PLOGI_ISSUE;
	ndlp->nlp_state = NLP_STE_NPR_NODE;
	lpfc_nlp_list(phba, ndlp, NLP_NPR_LIST);
	spin_lock_irq(phba->host->host_lock);
	ndlp->nlp_flag &= ~(NLP_NODEV_REMOVE | NLP_NPR_2B_DISC);
	spin_unlock_irq(phba->host->host_lock);

	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_plogi_adisc_issue(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;

	/* software abort outstanding ADISC */
	lpfc_els_abort(phba, ndlp, 1);

	cmdiocb = (struct lpfc_iocbq *) arg;

	if (lpfc_rcv_plogi(phba, ndlp, cmdiocb)) {
		return ndlp->nlp_state;
	}
	ndlp->nlp_prev_state = NLP_STE_ADISC_ISSUE;
	ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
	lpfc_nlp_list(phba, ndlp, NLP_PLOGI_LIST);
	lpfc_issue_els_plogi(phba, ndlp->nlp_DID, 0);

	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_prli_adisc_issue(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;

	lpfc_els_rsp_prli_acc(phba, cmdiocb, ndlp);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_logo_adisc_issue(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;

	/* software abort outstanding ADISC */
	lpfc_els_abort(phba, ndlp, 0);

	lpfc_rcv_logo(phba, ndlp, cmdiocb, ELS_CMD_LOGO);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_padisc_adisc_issue(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;

	lpfc_rcv_padisc(phba, ndlp, cmdiocb);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_prlo_adisc_issue(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;

	/* Treat like rcv logo */
	lpfc_rcv_logo(phba, ndlp, cmdiocb, ELS_CMD_PRLO);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_cmpl_adisc_adisc_issue(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb, *rspiocb;
	IOCB_t *irsp;
	ADISC *ap;

	cmdiocb = (struct lpfc_iocbq *) arg;
	rspiocb = cmdiocb->context_un.rsp_iocb;

	ap = (ADISC *)lpfc_check_elscmpl_iocb(phba, cmdiocb, rspiocb);
	irsp = &rspiocb->iocb;

	if ((irsp->ulpStatus) ||
		(!lpfc_check_adisc(phba, ndlp, &ap->nodeName, &ap->portName))) {
		/* 1 sec timeout */
		mod_timer(&ndlp->nlp_delayfunc, jiffies + HZ);
		spin_lock_irq(phba->host->host_lock);
		ndlp->nlp_flag |= NLP_DELAY_TMO;
		spin_unlock_irq(phba->host->host_lock);
		ndlp->nlp_last_elscmd = ELS_CMD_PLOGI;

		memset(&ndlp->nlp_nodename, 0, sizeof (struct lpfc_name));
		memset(&ndlp->nlp_portname, 0, sizeof (struct lpfc_name));

		ndlp->nlp_prev_state = NLP_STE_ADISC_ISSUE;
		ndlp->nlp_state = NLP_STE_NPR_NODE;
		lpfc_nlp_list(phba, ndlp, NLP_NPR_LIST);
		lpfc_unreg_rpi(phba, ndlp);
		return ndlp->nlp_state;
	}

	if (ndlp->nlp_type & NLP_FCP_TARGET) {
		ndlp->nlp_prev_state = NLP_STE_ADISC_ISSUE;
		ndlp->nlp_state = NLP_STE_MAPPED_NODE;
		lpfc_nlp_list(phba, ndlp, NLP_MAPPED_LIST);
	} else {
		ndlp->nlp_prev_state = NLP_STE_ADISC_ISSUE;
		ndlp->nlp_state = NLP_STE_UNMAPPED_NODE;
		lpfc_nlp_list(phba, ndlp, NLP_UNMAPPED_LIST);
	}
	return ndlp->nlp_state;
}

static uint32_t
lpfc_device_rm_adisc_issue(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	if(ndlp->nlp_flag & NLP_NPR_2B_DISC) {
		ndlp->nlp_flag |= NLP_NODEV_REMOVE;
		return ndlp->nlp_state;
	}
	else {
		/* software abort outstanding ADISC */
		lpfc_els_abort(phba, ndlp, 1);

		lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
		return NLP_STE_FREED_NODE;
	}
}

static uint32_t
lpfc_device_recov_adisc_issue(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	/* software abort outstanding ADISC */
	lpfc_els_abort(phba, ndlp, 1);

	ndlp->nlp_prev_state = NLP_STE_ADISC_ISSUE;
	ndlp->nlp_state = NLP_STE_NPR_NODE;
	lpfc_nlp_list(phba, ndlp, NLP_NPR_LIST);
	spin_lock_irq(phba->host->host_lock);
	ndlp->nlp_flag &= ~(NLP_NODEV_REMOVE | NLP_NPR_2B_DISC);
	ndlp->nlp_flag |= NLP_NPR_ADISC;
	spin_unlock_irq(phba->host->host_lock);

	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_plogi_reglogin_issue(struct lpfc_hba * phba,
			      struct lpfc_nodelist * ndlp, void *arg,
			      uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;

	lpfc_rcv_plogi(phba, ndlp, cmdiocb);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_prli_reglogin_issue(struct lpfc_hba * phba,
			     struct lpfc_nodelist * ndlp, void *arg,
			     uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;

	lpfc_els_rsp_prli_acc(phba, cmdiocb, ndlp);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_logo_reglogin_issue(struct lpfc_hba * phba,
			     struct lpfc_nodelist * ndlp, void *arg,
			     uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;

	lpfc_rcv_logo(phba, ndlp, cmdiocb, ELS_CMD_LOGO);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_padisc_reglogin_issue(struct lpfc_hba * phba,
			       struct lpfc_nodelist * ndlp, void *arg,
			       uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;

	lpfc_rcv_padisc(phba, ndlp, cmdiocb);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_prlo_reglogin_issue(struct lpfc_hba * phba,
			     struct lpfc_nodelist * ndlp, void *arg,
			     uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;
	lpfc_els_rsp_acc(phba, ELS_CMD_PRLO, cmdiocb, ndlp, NULL, 0);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_cmpl_reglogin_reglogin_issue(struct lpfc_hba * phba,
				  struct lpfc_nodelist * ndlp,
				  void *arg, uint32_t evt)
{
	LPFC_MBOXQ_t *pmb;
	MAILBOX_t *mb;
	uint32_t did;

	pmb = (LPFC_MBOXQ_t *) arg;
	mb = &pmb->mb;
	did = mb->un.varWords[1];
	if (mb->mbxStatus) {
		/* RegLogin failed */
		lpfc_printf_log(phba,
				KERN_ERR,
				LOG_DISCOVERY,
				"%d:0246 RegLogin failed Data: x%x x%x x%x\n",
				phba->brd_no,
				did, mb->mbxStatus, phba->hba_state);

		/*
		 * If RegLogin failed due to lack of HBA resources do not
		 * retry discovery.
		 */
		if (mb->mbxStatus == MBXERR_RPI_FULL) {
			ndlp->nlp_prev_state = NLP_STE_UNUSED_NODE;
			ndlp->nlp_state = NLP_STE_UNUSED_NODE;
			lpfc_nlp_list(phba, ndlp, NLP_UNUSED_LIST);
			return ndlp->nlp_state;
		}

		/* Put ndlp in npr list set plogi timer for 1 sec */
		mod_timer(&ndlp->nlp_delayfunc, jiffies + HZ * 1);
		spin_lock_irq(phba->host->host_lock);
		ndlp->nlp_flag |= NLP_DELAY_TMO;
		spin_unlock_irq(phba->host->host_lock);
		ndlp->nlp_last_elscmd = ELS_CMD_PLOGI;

		lpfc_issue_els_logo(phba, ndlp, 0);
		ndlp->nlp_prev_state = NLP_STE_REG_LOGIN_ISSUE;
		ndlp->nlp_state = NLP_STE_NPR_NODE;
		lpfc_nlp_list(phba, ndlp, NLP_NPR_LIST);
		return ndlp->nlp_state;
	}

	ndlp->nlp_rpi = mb->un.varWords[0];

	/* Only if we are not a fabric nport do we issue PRLI */
	if (!(ndlp->nlp_type & NLP_FABRIC)) {
		ndlp->nlp_prev_state = NLP_STE_REG_LOGIN_ISSUE;
		ndlp->nlp_state = NLP_STE_PRLI_ISSUE;
		lpfc_nlp_list(phba, ndlp, NLP_PRLI_LIST);
		lpfc_issue_els_prli(phba, ndlp, 0);
	} else {
		ndlp->nlp_prev_state = NLP_STE_REG_LOGIN_ISSUE;
		ndlp->nlp_state = NLP_STE_UNMAPPED_NODE;
		lpfc_nlp_list(phba, ndlp, NLP_UNMAPPED_LIST);
	}
	return ndlp->nlp_state;
}

static uint32_t
lpfc_device_rm_reglogin_issue(struct lpfc_hba * phba,
			      struct lpfc_nodelist * ndlp, void *arg,
			      uint32_t evt)
{
	if(ndlp->nlp_flag & NLP_NPR_2B_DISC) {
		ndlp->nlp_flag |= NLP_NODEV_REMOVE;
		return ndlp->nlp_state;
	}
	else {
		lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
		return NLP_STE_FREED_NODE;
	}
}

static uint32_t
lpfc_device_recov_reglogin_issue(struct lpfc_hba * phba,
			       struct lpfc_nodelist * ndlp, void *arg,
			       uint32_t evt)
{
	ndlp->nlp_prev_state = NLP_STE_REG_LOGIN_ISSUE;
	ndlp->nlp_state = NLP_STE_NPR_NODE;
	lpfc_nlp_list(phba, ndlp, NLP_NPR_LIST);
	spin_lock_irq(phba->host->host_lock);
	ndlp->nlp_flag &= ~(NLP_NODEV_REMOVE | NLP_NPR_2B_DISC);
	spin_unlock_irq(phba->host->host_lock);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_plogi_prli_issue(struct lpfc_hba * phba,
			  struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;

	lpfc_rcv_plogi(phba, ndlp, cmdiocb);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_prli_prli_issue(struct lpfc_hba * phba,
			 struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;

	lpfc_els_rsp_prli_acc(phba, cmdiocb, ndlp);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_logo_prli_issue(struct lpfc_hba * phba,
			 struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;

	/* Software abort outstanding PRLI before sending acc */
	lpfc_els_abort(phba, ndlp, 1);

	lpfc_rcv_logo(phba, ndlp, cmdiocb, ELS_CMD_LOGO);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_padisc_prli_issue(struct lpfc_hba * phba,
			   struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;

	lpfc_rcv_padisc(phba, ndlp, cmdiocb);
	return ndlp->nlp_state;
}

/* This routine is envoked when we rcv a PRLO request from a nport
 * we are logged into.  We should send back a PRLO rsp setting the
 * appropriate bits.
 * NEXT STATE = PRLI_ISSUE
 */
static uint32_t
lpfc_rcv_prlo_prli_issue(struct lpfc_hba * phba,
			 struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;
	lpfc_els_rsp_acc(phba, ELS_CMD_PRLO, cmdiocb, ndlp, NULL, 0);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_cmpl_prli_prli_issue(struct lpfc_hba * phba,
			  struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb, *rspiocb;
	IOCB_t *irsp;
	PRLI *npr;

	cmdiocb = (struct lpfc_iocbq *) arg;
	rspiocb = cmdiocb->context_un.rsp_iocb;
	npr = (PRLI *)lpfc_check_elscmpl_iocb(phba, cmdiocb, rspiocb);

	irsp = &rspiocb->iocb;
	if (irsp->ulpStatus) {
		ndlp->nlp_prev_state = NLP_STE_PRLI_ISSUE;
		ndlp->nlp_state = NLP_STE_UNMAPPED_NODE;
		lpfc_nlp_list(phba, ndlp, NLP_UNMAPPED_LIST);
		return ndlp->nlp_state;
	}

	/* Check out PRLI rsp */
	ndlp->nlp_type &= ~(NLP_FCP_TARGET | NLP_FCP_INITIATOR);
	ndlp->nlp_fcp_info &= ~NLP_FCP_2_DEVICE;
	if ((npr->acceptRspCode == PRLI_REQ_EXECUTED) &&
	    (npr->prliType == PRLI_FCP_TYPE)) {
		if (npr->initiatorFunc)
			ndlp->nlp_type |= NLP_FCP_INITIATOR;
		if (npr->targetFunc)
			ndlp->nlp_type |= NLP_FCP_TARGET;
		if (npr->Retry)
			ndlp->nlp_fcp_info |= NLP_FCP_2_DEVICE;
	}

	ndlp->nlp_prev_state = NLP_STE_PRLI_ISSUE;
	ndlp->nlp_state = NLP_STE_MAPPED_NODE;
	lpfc_nlp_list(phba, ndlp, NLP_MAPPED_LIST);
	return ndlp->nlp_state;
}

/*! lpfc_device_rm_prli_issue
  *
  * \pre
  * \post
  * \param   phba
  * \param   ndlp
  * \param   arg
  * \param   evt
  * \return  uint32_t
  *
  * \b Description:
  *    This routine is envoked when we a request to remove a nport we are in the
  *    process of PRLIing. We should software abort outstanding prli, unreg
  *    login, send a logout. We will change node state to UNUSED_NODE, put it
  *    on plogi list so it can be freed when LOGO completes.
  *
  */
static uint32_t
lpfc_device_rm_prli_issue(struct lpfc_hba * phba,
			  struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	if(ndlp->nlp_flag & NLP_NPR_2B_DISC) {
		ndlp->nlp_flag |= NLP_NODEV_REMOVE;
		return ndlp->nlp_state;
	}
	else {
		/* software abort outstanding PLOGI */
		lpfc_els_abort(phba, ndlp, 1);

		lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
		return NLP_STE_FREED_NODE;
	}
}


/*! lpfc_device_recov_prli_issue
  *
  * \pre
  * \post
  * \param   phba
  * \param   ndlp
  * \param   arg
  * \param   evt
  * \return  uint32_t
  *
  * \b Description:
  *    The routine is envoked when the state of a device is unknown, like
  *    during a link down. We should remove the nodelist entry from the
  *    unmapped list, issue a UNREG_LOGIN, do a software abort of the
  *    outstanding PRLI command, then free the node entry.
  */
static uint32_t
lpfc_device_recov_prli_issue(struct lpfc_hba * phba,
			   struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	/* software abort outstanding PRLI */
	lpfc_els_abort(phba, ndlp, 1);

	ndlp->nlp_prev_state = NLP_STE_PRLI_ISSUE;
	ndlp->nlp_state = NLP_STE_NPR_NODE;
	lpfc_nlp_list(phba, ndlp, NLP_NPR_LIST);
	spin_lock_irq(phba->host->host_lock);
	ndlp->nlp_flag &= ~(NLP_NODEV_REMOVE | NLP_NPR_2B_DISC);
	spin_unlock_irq(phba->host->host_lock);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_plogi_unmap_node(struct lpfc_hba * phba,
			  struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;

	lpfc_rcv_plogi(phba, ndlp, cmdiocb);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_prli_unmap_node(struct lpfc_hba * phba,
			 struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;

	lpfc_rcv_prli(phba, ndlp, cmdiocb);
	lpfc_els_rsp_prli_acc(phba, cmdiocb, ndlp);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_logo_unmap_node(struct lpfc_hba * phba,
			 struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;

	lpfc_rcv_logo(phba, ndlp, cmdiocb, ELS_CMD_LOGO);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_padisc_unmap_node(struct lpfc_hba * phba,
			   struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;

	lpfc_rcv_padisc(phba, ndlp, cmdiocb);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_prlo_unmap_node(struct lpfc_hba * phba,
			 struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;

	lpfc_els_rsp_acc(phba, ELS_CMD_PRLO, cmdiocb, ndlp, NULL, 0);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_device_recov_unmap_node(struct lpfc_hba * phba,
			   struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	ndlp->nlp_prev_state = NLP_STE_UNMAPPED_NODE;
	ndlp->nlp_state = NLP_STE_NPR_NODE;
	lpfc_nlp_list(phba, ndlp, NLP_NPR_LIST);
	ndlp->nlp_flag &= ~(NLP_NODEV_REMOVE | NLP_NPR_2B_DISC);
	lpfc_disc_set_adisc(phba, ndlp);

	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_plogi_mapped_node(struct lpfc_hba * phba,
			   struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;

	lpfc_rcv_plogi(phba, ndlp, cmdiocb);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_prli_mapped_node(struct lpfc_hba * phba,
			  struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;

	lpfc_els_rsp_prli_acc(phba, cmdiocb, ndlp);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_logo_mapped_node(struct lpfc_hba * phba,
			  struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;

	lpfc_rcv_logo(phba, ndlp, cmdiocb, ELS_CMD_LOGO);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_padisc_mapped_node(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;

	lpfc_rcv_padisc(phba, ndlp, cmdiocb);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_prlo_mapped_node(struct lpfc_hba * phba,
			  struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;

	/* flush the target */
	spin_lock_irq(phba->host->host_lock);
	lpfc_sli_abort_iocb(phba, &phba->sli.ring[phba->sli.fcp_ring],
			       ndlp->nlp_sid, 0, 0, LPFC_CTX_TGT);
	spin_unlock_irq(phba->host->host_lock);

	/* Treat like rcv logo */
	lpfc_rcv_logo(phba, ndlp, cmdiocb, ELS_CMD_PRLO);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_device_recov_mapped_node(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	ndlp->nlp_prev_state = NLP_STE_MAPPED_NODE;
	ndlp->nlp_state = NLP_STE_NPR_NODE;
	lpfc_nlp_list(phba, ndlp, NLP_NPR_LIST);
	spin_lock_irq(phba->host->host_lock);
	ndlp->nlp_flag &= ~(NLP_NODEV_REMOVE | NLP_NPR_2B_DISC);
	spin_unlock_irq(phba->host->host_lock);
	lpfc_disc_set_adisc(phba, ndlp);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_plogi_npr_node(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;

	/* Ignore PLOGI if we have an outstanding LOGO */
	if (ndlp->nlp_flag & NLP_LOGO_SND) {
		return ndlp->nlp_state;
	}

	if (lpfc_rcv_plogi(phba, ndlp, cmdiocb)) {
		spin_lock_irq(phba->host->host_lock);
		ndlp->nlp_flag &= ~NLP_NPR_ADISC;
		spin_unlock_irq(phba->host->host_lock);
		return ndlp->nlp_state;
	}

	/* send PLOGI immediately, move to PLOGI issue state */
	if (!(ndlp->nlp_flag & NLP_DELAY_TMO)) {
		ndlp->nlp_prev_state = NLP_STE_NPR_NODE;
		ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
		lpfc_nlp_list(phba, ndlp, NLP_PLOGI_LIST);
		lpfc_issue_els_plogi(phba, ndlp->nlp_DID, 0);
	}

	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_prli_npr_node(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	struct lpfc_iocbq     *cmdiocb;
	struct ls_rjt          stat;

	cmdiocb = (struct lpfc_iocbq *) arg;

	memset(&stat, 0, sizeof (struct ls_rjt));
	stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
	stat.un.b.lsRjtRsnCodeExp = LSEXP_NOTHING_MORE;
	lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);

	if (!(ndlp->nlp_flag & NLP_DELAY_TMO)) {
		if (ndlp->nlp_flag & NLP_NPR_ADISC) {
			spin_lock_irq(phba->host->host_lock);
			ndlp->nlp_flag &= ~NLP_NPR_ADISC;
			spin_unlock_irq(phba->host->host_lock);
			ndlp->nlp_prev_state = NLP_STE_NPR_NODE;
			ndlp->nlp_state = NLP_STE_ADISC_ISSUE;
			lpfc_nlp_list(phba, ndlp, NLP_ADISC_LIST);
			lpfc_issue_els_adisc(phba, ndlp, 0);
		} else {
			ndlp->nlp_prev_state = NLP_STE_NPR_NODE;
			ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
			lpfc_nlp_list(phba, ndlp, NLP_PLOGI_LIST);
			lpfc_issue_els_plogi(phba, ndlp->nlp_DID, 0);
		}

	}
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_logo_npr_node(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	struct lpfc_iocbq     *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;

	lpfc_rcv_logo(phba, ndlp, cmdiocb, ELS_CMD_LOGO);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_padisc_npr_node(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	struct lpfc_iocbq     *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;

	lpfc_rcv_padisc(phba, ndlp, cmdiocb);

	/*
	 * Do not start discovery if discovery is about to start
	 * or discovery in progress for this node. Starting discovery
	 * here will affect the counting of discovery threads.
	 */
	if (!(ndlp->nlp_flag & NLP_DELAY_TMO) &&
		!(ndlp->nlp_flag & NLP_NPR_2B_DISC)){
		if (ndlp->nlp_flag & NLP_NPR_ADISC) {
			ndlp->nlp_prev_state = NLP_STE_NPR_NODE;
			ndlp->nlp_state = NLP_STE_ADISC_ISSUE;
			lpfc_nlp_list(phba, ndlp, NLP_ADISC_LIST);
			lpfc_issue_els_adisc(phba, ndlp, 0);
		} else {
			ndlp->nlp_prev_state = NLP_STE_NPR_NODE;
			ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
			lpfc_nlp_list(phba, ndlp, NLP_PLOGI_LIST);
			lpfc_issue_els_plogi(phba, ndlp->nlp_DID, 0);
		}
	}
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_prlo_npr_node(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	struct lpfc_iocbq     *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;

	spin_lock_irq(phba->host->host_lock);
	ndlp->nlp_flag |= NLP_LOGO_ACC;
	spin_unlock_irq(phba->host->host_lock);

	lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, NULL, 0);

	if (!(ndlp->nlp_flag & NLP_DELAY_TMO)) {
		mod_timer(&ndlp->nlp_delayfunc, jiffies + HZ * 1);
		spin_lock_irq(phba->host->host_lock);
		ndlp->nlp_flag |= NLP_DELAY_TMO;
		ndlp->nlp_flag &= ~NLP_NPR_ADISC;
		spin_unlock_irq(phba->host->host_lock);
		ndlp->nlp_last_elscmd = ELS_CMD_PLOGI;
	} else {
		spin_lock_irq(phba->host->host_lock);
		ndlp->nlp_flag &= ~NLP_NPR_ADISC;
		spin_unlock_irq(phba->host->host_lock);
	}
	return ndlp->nlp_state;
}

static uint32_t
lpfc_cmpl_plogi_npr_node(struct lpfc_hba * phba,
			  struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb, *rspiocb;
	IOCB_t *irsp;

	cmdiocb = (struct lpfc_iocbq *) arg;
	rspiocb = cmdiocb->context_un.rsp_iocb;

	irsp = &rspiocb->iocb;
	if (irsp->ulpStatus) {
		lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
		return NLP_STE_FREED_NODE;
	}
	return ndlp->nlp_state;
}

static uint32_t
lpfc_cmpl_prli_npr_node(struct lpfc_hba * phba,
			  struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb, *rspiocb;
	IOCB_t *irsp;

	cmdiocb = (struct lpfc_iocbq *) arg;
	rspiocb = cmdiocb->context_un.rsp_iocb;

	irsp = &rspiocb->iocb;
	if (irsp->ulpStatus && (ndlp->nlp_flag & NLP_NODEV_REMOVE)) {
		lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
		return NLP_STE_FREED_NODE;
	}
	return ndlp->nlp_state;
}

static uint32_t
lpfc_cmpl_logo_npr_node(struct lpfc_hba * phba,
		struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	lpfc_unreg_rpi(phba, ndlp);
	/* This routine does nothing, just return the current state */
	return ndlp->nlp_state;
}

static uint32_t
lpfc_cmpl_adisc_npr_node(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb, *rspiocb;
	IOCB_t *irsp;

	cmdiocb = (struct lpfc_iocbq *) arg;
	rspiocb = cmdiocb->context_un.rsp_iocb;

	irsp = &rspiocb->iocb;
	if (irsp->ulpStatus && (ndlp->nlp_flag & NLP_NODEV_REMOVE)) {
		lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
		return NLP_STE_FREED_NODE;
	}
	return ndlp->nlp_state;
}

static uint32_t
lpfc_cmpl_reglogin_npr_node(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	LPFC_MBOXQ_t *pmb;
	MAILBOX_t *mb;

	pmb = (LPFC_MBOXQ_t *) arg;
	mb = &pmb->mb;

	if (!mb->mbxStatus)
		ndlp->nlp_rpi = mb->un.varWords[0];
	else {
		if (ndlp->nlp_flag & NLP_NODEV_REMOVE) {
			lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
			return NLP_STE_FREED_NODE;
		}
	}
	return ndlp->nlp_state;
}

static uint32_t
lpfc_device_rm_npr_node(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	if (ndlp->nlp_flag & NLP_NPR_2B_DISC) {
		ndlp->nlp_flag |= NLP_NODEV_REMOVE;
		return ndlp->nlp_state;
	}
	lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
	return NLP_STE_FREED_NODE;
}

static uint32_t
lpfc_device_recov_npr_node(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	spin_lock_irq(phba->host->host_lock);
	ndlp->nlp_flag &= ~(NLP_NODEV_REMOVE | NLP_NPR_2B_DISC);
	spin_unlock_irq(phba->host->host_lock);
	if (ndlp->nlp_flag & NLP_DELAY_TMO) {
		lpfc_cancel_retry_delay_tmo(phba, ndlp);
	}
	return ndlp->nlp_state;
}


/* This next section defines the NPort Discovery State Machine */

/* There are 4 different double linked lists nodelist entries can reside on.
 * The plogi list and adisc list are used when Link Up discovery or RSCN
 * processing is needed. Each list holds the nodes that we will send PLOGI
 * or ADISC on. These lists will keep track of what nodes will be effected
 * by an RSCN, or a Link Up (Typically, all nodes are effected on Link Up).
 * The unmapped_list will contain all nodes that we have successfully logged
 * into at the Fibre Channel level. The mapped_list will contain all nodes
 * that are mapped FCP targets.
 */
/*
 * The bind list is a list of undiscovered (potentially non-existent) nodes
 * that we have saved binding information on. This information is used when
 * nodes transition from the unmapped to the mapped list.
 */
/* For UNUSED_NODE state, the node has just been allocated .
 * For PLOGI_ISSUE and REG_LOGIN_ISSUE, the node is on
 * the PLOGI list. For REG_LOGIN_COMPL, the node is taken off the PLOGI list
 * and put on the unmapped list. For ADISC processing, the node is taken off
 * the ADISC list and placed on either the mapped or unmapped list (depending
 * on its previous state). Once on the unmapped list, a PRLI is issued and the
 * state changed to PRLI_ISSUE. When the PRLI completion occurs, the state is
 * changed to UNMAPPED_NODE. If the completion indicates a mapped
 * node, the node is taken off the unmapped list. The binding list is checked
 * for a valid binding, or a binding is automatically assigned. If binding
 * assignment is unsuccessful, the node is left on the unmapped list. If
 * binding assignment is successful, the associated binding list entry (if
 * any) is removed, and the node is placed on the mapped list.
 */
/*
 * For a Link Down, all nodes on the ADISC, PLOGI, unmapped or mapped
 * lists will receive a DEVICE_RECOVERY event. If the linkdown or devloss timers
 * expire, all effected nodes will receive a DEVICE_RM event.
 */
/*
 * For a Link Up or RSCN, all nodes will move from the mapped / unmapped lists
 * to either the ADISC or PLOGI list.  After a Nameserver query or ALPA loopmap
 * check, additional nodes may be added or removed (via DEVICE_RM) to / from
 * the PLOGI or ADISC lists. Once the PLOGI and ADISC lists are populated,
 * we will first process the ADISC list.  32 entries are processed initially and
 * ADISC is initited for each one.  Completions / Events for each node are
 * funnelled thru the state machine.  As each node finishes ADISC processing, it
 * starts ADISC for any nodes waiting for ADISC processing. If no nodes are
 * waiting, and the ADISC list count is identically 0, then we are done. For
 * Link Up discovery, since all nodes on the PLOGI list are UNREG_LOGIN'ed, we
 * can issue a CLEAR_LA and reenable Link Events. Next we will process the PLOGI
 * list.  32 entries are processed initially and PLOGI is initited for each one.
 * Completions / Events for each node are funnelled thru the state machine.  As
 * each node finishes PLOGI processing, it starts PLOGI for any nodes waiting
 * for PLOGI processing. If no nodes are waiting, and the PLOGI list count is
 * indentically 0, then we are done. We have now completed discovery / RSCN
 * handling. Upon completion, ALL nodes should be on either the mapped or
 * unmapped lists.
 */

static uint32_t (*lpfc_disc_action[NLP_STE_MAX_STATE * NLP_EVT_MAX_EVENT])
     (struct lpfc_hba *, struct lpfc_nodelist *, void *, uint32_t) = {
	/* Action routine                  Event       Current State  */
	lpfc_rcv_plogi_unused_node,	/* RCV_PLOGI   UNUSED_NODE    */
	lpfc_rcv_els_unused_node,	/* RCV_PRLI        */
	lpfc_rcv_logo_unused_node,	/* RCV_LOGO        */
	lpfc_rcv_els_unused_node,	/* RCV_ADISC       */
	lpfc_rcv_els_unused_node,	/* RCV_PDISC       */
	lpfc_rcv_els_unused_node,	/* RCV_PRLO        */
	lpfc_disc_illegal,		/* CMPL_PLOGI      */
	lpfc_disc_illegal,		/* CMPL_PRLI       */
	lpfc_cmpl_logo_unused_node,	/* CMPL_LOGO       */
	lpfc_disc_illegal,		/* CMPL_ADISC      */
	lpfc_disc_illegal,		/* CMPL_REG_LOGIN  */
	lpfc_device_rm_unused_node,	/* DEVICE_RM       */
	lpfc_disc_illegal,		/* DEVICE_RECOVERY */

	lpfc_rcv_plogi_plogi_issue,	/* RCV_PLOGI   PLOGI_ISSUE    */
	lpfc_rcv_els_plogi_issue,	/* RCV_PRLI        */
	lpfc_rcv_logo_plogi_issue,	/* RCV_LOGO        */
	lpfc_rcv_els_plogi_issue,	/* RCV_ADISC       */
	lpfc_rcv_els_plogi_issue,	/* RCV_PDISC       */
	lpfc_rcv_els_plogi_issue,	/* RCV_PRLO        */
	lpfc_cmpl_plogi_plogi_issue,	/* CMPL_PLOGI      */
	lpfc_disc_illegal,		/* CMPL_PRLI       */
	lpfc_disc_illegal,		/* CMPL_LOGO       */
	lpfc_disc_illegal,		/* CMPL_ADISC      */
	lpfc_disc_illegal,		/* CMPL_REG_LOGIN  */
	lpfc_device_rm_plogi_issue,	/* DEVICE_RM       */
	lpfc_device_recov_plogi_issue,	/* DEVICE_RECOVERY */

	lpfc_rcv_plogi_adisc_issue,	/* RCV_PLOGI   ADISC_ISSUE    */
	lpfc_rcv_prli_adisc_issue,	/* RCV_PRLI        */
	lpfc_rcv_logo_adisc_issue,	/* RCV_LOGO        */
	lpfc_rcv_padisc_adisc_issue,	/* RCV_ADISC       */
	lpfc_rcv_padisc_adisc_issue,	/* RCV_PDISC       */
	lpfc_rcv_prlo_adisc_issue,	/* RCV_PRLO        */
	lpfc_disc_illegal,		/* CMPL_PLOGI      */
	lpfc_disc_illegal,		/* CMPL_PRLI       */
	lpfc_disc_illegal,		/* CMPL_LOGO       */
	lpfc_cmpl_adisc_adisc_issue,	/* CMPL_ADISC      */
	lpfc_disc_illegal,		/* CMPL_REG_LOGIN  */
	lpfc_device_rm_adisc_issue,	/* DEVICE_RM       */
	lpfc_device_recov_adisc_issue,	/* DEVICE_RECOVERY */

	lpfc_rcv_plogi_reglogin_issue,	/* RCV_PLOGI  REG_LOGIN_ISSUE */
	lpfc_rcv_prli_reglogin_issue,	/* RCV_PLOGI       */
	lpfc_rcv_logo_reglogin_issue,	/* RCV_LOGO        */
	lpfc_rcv_padisc_reglogin_issue,	/* RCV_ADISC       */
	lpfc_rcv_padisc_reglogin_issue,	/* RCV_PDISC       */
	lpfc_rcv_prlo_reglogin_issue,	/* RCV_PRLO        */
	lpfc_disc_illegal,		/* CMPL_PLOGI      */
	lpfc_disc_illegal,		/* CMPL_PRLI       */
	lpfc_disc_illegal,		/* CMPL_LOGO       */
	lpfc_disc_illegal,		/* CMPL_ADISC      */
	lpfc_cmpl_reglogin_reglogin_issue,/* CMPL_REG_LOGIN  */
	lpfc_device_rm_reglogin_issue,	/* DEVICE_RM       */
	lpfc_device_recov_reglogin_issue,/* DEVICE_RECOVERY */

	lpfc_rcv_plogi_prli_issue,	/* RCV_PLOGI   PRLI_ISSUE     */
	lpfc_rcv_prli_prli_issue,	/* RCV_PRLI        */
	lpfc_rcv_logo_prli_issue,	/* RCV_LOGO        */
	lpfc_rcv_padisc_prli_issue,	/* RCV_ADISC       */
	lpfc_rcv_padisc_prli_issue,	/* RCV_PDISC       */
	lpfc_rcv_prlo_prli_issue,	/* RCV_PRLO        */
	lpfc_disc_illegal,		/* CMPL_PLOGI      */
	lpfc_cmpl_prli_prli_issue,	/* CMPL_PRLI       */
	lpfc_disc_illegal,		/* CMPL_LOGO       */
	lpfc_disc_illegal,		/* CMPL_ADISC      */
	lpfc_disc_illegal,		/* CMPL_REG_LOGIN  */
	lpfc_device_rm_prli_issue,	/* DEVICE_RM       */
	lpfc_device_recov_prli_issue,	/* DEVICE_RECOVERY */

	lpfc_rcv_plogi_unmap_node,	/* RCV_PLOGI   UNMAPPED_NODE  */
	lpfc_rcv_prli_unmap_node,	/* RCV_PRLI        */
	lpfc_rcv_logo_unmap_node,	/* RCV_LOGO        */
	lpfc_rcv_padisc_unmap_node,	/* RCV_ADISC       */
	lpfc_rcv_padisc_unmap_node,	/* RCV_PDISC       */
	lpfc_rcv_prlo_unmap_node,	/* RCV_PRLO        */
	lpfc_disc_illegal,		/* CMPL_PLOGI      */
	lpfc_disc_illegal,		/* CMPL_PRLI       */
	lpfc_disc_illegal,		/* CMPL_LOGO       */
	lpfc_disc_illegal,		/* CMPL_ADISC      */
	lpfc_disc_illegal,		/* CMPL_REG_LOGIN  */
	lpfc_disc_illegal,		/* DEVICE_RM       */
	lpfc_device_recov_unmap_node,	/* DEVICE_RECOVERY */

	lpfc_rcv_plogi_mapped_node,	/* RCV_PLOGI   MAPPED_NODE    */
	lpfc_rcv_prli_mapped_node,	/* RCV_PRLI        */
	lpfc_rcv_logo_mapped_node,	/* RCV_LOGO        */
	lpfc_rcv_padisc_mapped_node,	/* RCV_ADISC       */
	lpfc_rcv_padisc_mapped_node,	/* RCV_PDISC       */
	lpfc_rcv_prlo_mapped_node,	/* RCV_PRLO        */
	lpfc_disc_illegal,		/* CMPL_PLOGI      */
	lpfc_disc_illegal,		/* CMPL_PRLI       */
	lpfc_disc_illegal,		/* CMPL_LOGO       */
	lpfc_disc_illegal,		/* CMPL_ADISC      */
	lpfc_disc_illegal,		/* CMPL_REG_LOGIN  */
	lpfc_disc_illegal,		/* DEVICE_RM       */
	lpfc_device_recov_mapped_node,	/* DEVICE_RECOVERY */

	lpfc_rcv_plogi_npr_node,        /* RCV_PLOGI   NPR_NODE    */
	lpfc_rcv_prli_npr_node,         /* RCV_PRLI        */
	lpfc_rcv_logo_npr_node,         /* RCV_LOGO        */
	lpfc_rcv_padisc_npr_node,       /* RCV_ADISC       */
	lpfc_rcv_padisc_npr_node,       /* RCV_PDISC       */
	lpfc_rcv_prlo_npr_node,         /* RCV_PRLO        */
	lpfc_cmpl_plogi_npr_node,	/* CMPL_PLOGI      */
	lpfc_cmpl_prli_npr_node,	/* CMPL_PRLI       */
	lpfc_cmpl_logo_npr_node,        /* CMPL_LOGO       */
	lpfc_cmpl_adisc_npr_node,       /* CMPL_ADISC      */
	lpfc_cmpl_reglogin_npr_node,    /* CMPL_REG_LOGIN  */
	lpfc_device_rm_npr_node,        /* DEVICE_RM       */
	lpfc_device_recov_npr_node,     /* DEVICE_RECOVERY */
};

int
lpfc_disc_state_machine(struct lpfc_hba * phba,
			struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	uint32_t cur_state, rc;
	uint32_t(*func) (struct lpfc_hba *, struct lpfc_nodelist *, void *,
			 uint32_t);

	ndlp->nlp_disc_refcnt++;
	cur_state = ndlp->nlp_state;

	/* DSM in event <evt> on NPort <nlp_DID> in state <cur_state> */
	lpfc_printf_log(phba,
			KERN_INFO,
			LOG_DISCOVERY,
			"%d:0211 DSM in event x%x on NPort x%x in state %d "
			"Data: x%x\n",
			phba->brd_no,
			evt, ndlp->nlp_DID, cur_state, ndlp->nlp_flag);

	func = lpfc_disc_action[(cur_state * NLP_EVT_MAX_EVENT) + evt];
	rc = (func) (phba, ndlp, arg, evt);

	/* DSM out state <rc> on NPort <nlp_DID> */
	lpfc_printf_log(phba,
		       KERN_INFO,
		       LOG_DISCOVERY,
		       "%d:0212 DSM out state %d on NPort x%x Data: x%x\n",
		       phba->brd_no,
		       rc, ndlp->nlp_DID, ndlp->nlp_flag);

	ndlp->nlp_disc_refcnt--;

	/* Check to see if ndlp removal is deferred */
	if ((ndlp->nlp_disc_refcnt == 0)
	    && (ndlp->nlp_flag & NLP_DELAY_REMOVE)) {
		spin_lock_irq(phba->host->host_lock);
		ndlp->nlp_flag &= ~NLP_DELAY_REMOVE;
		spin_unlock_irq(phba->host->host_lock);
		lpfc_nlp_remove(phba, ndlp);
		return NLP_STE_FREED_NODE;
	}
	if (rc == NLP_STE_FREED_NODE)
		return NLP_STE_FREED_NODE;
	return rc;
}
