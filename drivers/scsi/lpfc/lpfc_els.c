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

static int lpfc_els_retry(struct lpfc_hba *, struct lpfc_iocbq *,
			  struct lpfc_iocbq *);
static int lpfc_max_els_tries = 3;

static int
lpfc_els_chk_latt(struct lpfc_hba * phba)
{
	struct lpfc_sli *psli;
	LPFC_MBOXQ_t *mbox;
	uint32_t ha_copy;
	int rc;

	psli = &phba->sli;

	if ((phba->hba_state >= LPFC_HBA_READY) ||
	    (phba->hba_state == LPFC_LINK_DOWN))
		return 0;

	/* Read the HBA Host Attention Register */
	spin_lock_irq(phba->host->host_lock);
	ha_copy = readl(phba->HAregaddr);
	spin_unlock_irq(phba->host->host_lock);

	if (!(ha_copy & HA_LATT))
		return 0;

	/* Pending Link Event during Discovery */
	lpfc_printf_log(phba, KERN_WARNING, LOG_DISCOVERY,
			"%d:0237 Pending Link Event during "
			"Discovery: State x%x\n",
			phba->brd_no, phba->hba_state);

	/* CLEAR_LA should re-enable link attention events and
	 * we should then imediately take a LATT event. The
	 * LATT processing should call lpfc_linkdown() which
	 * will cleanup any left over in-progress discovery
	 * events.
	 */
	spin_lock_irq(phba->host->host_lock);
	phba->fc_flag |= FC_ABORT_DISCOVERY;
	spin_unlock_irq(phba->host->host_lock);

	if (phba->hba_state != LPFC_CLEAR_LA) {
		if ((mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL))) {
			phba->hba_state = LPFC_CLEAR_LA;
			lpfc_clear_la(phba, mbox);
			mbox->mbox_cmpl = lpfc_mbx_cmpl_clear_la;
			rc = lpfc_sli_issue_mbox (phba, mbox,
						  (MBX_NOWAIT | MBX_STOP_IOCB));
			if (rc == MBX_NOT_FINISHED) {
				mempool_free(mbox, phba->mbox_mem_pool);
				phba->hba_state = LPFC_HBA_ERROR;
			}
		}
	}

	return 1;

}

static struct lpfc_iocbq *
lpfc_prep_els_iocb(struct lpfc_hba * phba, uint8_t expectRsp,
		   uint16_t cmdSize, uint8_t retry, struct lpfc_nodelist * ndlp,
		   uint32_t did, uint32_t elscmd)
{
	struct lpfc_sli_ring *pring;
	struct lpfc_iocbq *elsiocb;
	struct lpfc_dmabuf *pcmd, *prsp, *pbuflist;
	struct ulp_bde64 *bpl;
	IOCB_t *icmd;

	pring = &phba->sli.ring[LPFC_ELS_RING];

	if (phba->hba_state < LPFC_LINK_UP)
		return  NULL;

	/* Allocate buffer for  command iocb */
	spin_lock_irq(phba->host->host_lock);
	elsiocb = lpfc_sli_get_iocbq(phba);
	spin_unlock_irq(phba->host->host_lock);

	if (elsiocb == NULL)
		return NULL;
	icmd = &elsiocb->iocb;

	/* fill in BDEs for command */
	/* Allocate buffer for command payload */
	if (((pcmd = kmalloc(sizeof (struct lpfc_dmabuf), GFP_KERNEL)) == 0) ||
	    ((pcmd->virt = lpfc_mbuf_alloc(phba,
					   MEM_PRI, &(pcmd->phys))) == 0)) {
		kfree(pcmd);

		spin_lock_irq(phba->host->host_lock);
		lpfc_sli_release_iocbq(phba, elsiocb);
		spin_unlock_irq(phba->host->host_lock);
		return NULL;
	}

	INIT_LIST_HEAD(&pcmd->list);

	/* Allocate buffer for response payload */
	if (expectRsp) {
		prsp = kmalloc(sizeof (struct lpfc_dmabuf), GFP_KERNEL);
		if (prsp)
			prsp->virt = lpfc_mbuf_alloc(phba, MEM_PRI,
						     &prsp->phys);
		if (prsp == 0 || prsp->virt == 0) {
			kfree(prsp);
			lpfc_mbuf_free(phba, pcmd->virt, pcmd->phys);
			kfree(pcmd);
			spin_lock_irq(phba->host->host_lock);
			lpfc_sli_release_iocbq(phba, elsiocb);
			spin_unlock_irq(phba->host->host_lock);
			return NULL;
		}
		INIT_LIST_HEAD(&prsp->list);
	} else {
		prsp = NULL;
	}

	/* Allocate buffer for Buffer ptr list */
	pbuflist = kmalloc(sizeof (struct lpfc_dmabuf), GFP_KERNEL);
	if (pbuflist)
	    pbuflist->virt = lpfc_mbuf_alloc(phba, MEM_PRI,
					     &pbuflist->phys);
	if (pbuflist == 0 || pbuflist->virt == 0) {
		spin_lock_irq(phba->host->host_lock);
		lpfc_sli_release_iocbq(phba, elsiocb);
		spin_unlock_irq(phba->host->host_lock);
		lpfc_mbuf_free(phba, pcmd->virt, pcmd->phys);
		lpfc_mbuf_free(phba, prsp->virt, prsp->phys);
		kfree(pcmd);
		kfree(prsp);
		kfree(pbuflist);
		return NULL;
	}

	INIT_LIST_HEAD(&pbuflist->list);

	icmd->un.elsreq64.bdl.addrHigh = putPaddrHigh(pbuflist->phys);
	icmd->un.elsreq64.bdl.addrLow = putPaddrLow(pbuflist->phys);
	icmd->un.elsreq64.bdl.bdeFlags = BUFF_TYPE_BDL;
	if (expectRsp) {
		icmd->un.elsreq64.bdl.bdeSize = (2 * sizeof (struct ulp_bde64));
		icmd->un.elsreq64.remoteID = did;	/* DID */
		icmd->ulpCommand = CMD_ELS_REQUEST64_CR;
	} else {
		icmd->un.elsreq64.bdl.bdeSize = sizeof (struct ulp_bde64);
		icmd->ulpCommand = CMD_XMIT_ELS_RSP64_CX;
	}

	icmd->ulpBdeCount = 1;
	icmd->ulpLe = 1;
	icmd->ulpClass = CLASS3;

	bpl = (struct ulp_bde64 *) pbuflist->virt;
	bpl->addrLow = le32_to_cpu(putPaddrLow(pcmd->phys));
	bpl->addrHigh = le32_to_cpu(putPaddrHigh(pcmd->phys));
	bpl->tus.f.bdeSize = cmdSize;
	bpl->tus.f.bdeFlags = 0;
	bpl->tus.w = le32_to_cpu(bpl->tus.w);

	if (expectRsp) {
		bpl++;
		bpl->addrLow = le32_to_cpu(putPaddrLow(prsp->phys));
		bpl->addrHigh = le32_to_cpu(putPaddrHigh(prsp->phys));
		bpl->tus.f.bdeSize = FCELSSIZE;
		bpl->tus.f.bdeFlags = BUFF_USE_RCV;
		bpl->tus.w = le32_to_cpu(bpl->tus.w);
	}

	/* Save for completion so we can release these resources */
	elsiocb->context1 = (uint8_t *) ndlp;
	elsiocb->context2 = (uint8_t *) pcmd;
	elsiocb->context3 = (uint8_t *) pbuflist;
	elsiocb->retry = retry;
	elsiocb->drvrTimeout = (phba->fc_ratov << 1) + LPFC_DRVR_TIMEOUT;

	if (prsp) {
		list_add(&prsp->list, &pcmd->list);
	}

	if (expectRsp) {
		/* Xmit ELS command <elsCmd> to remote NPORT <did> */
		lpfc_printf_log(phba, KERN_INFO, LOG_ELS,
				"%d:0116 Xmit ELS command x%x to remote "
				"NPORT x%x Data: x%x x%x\n",
				phba->brd_no, elscmd,
				did, icmd->ulpIoTag, phba->hba_state);
	} else {
		/* Xmit ELS response <elsCmd> to remote NPORT <did> */
		lpfc_printf_log(phba, KERN_INFO, LOG_ELS,
				"%d:0117 Xmit ELS response x%x to remote "
				"NPORT x%x Data: x%x x%x\n",
				phba->brd_no, elscmd,
				ndlp->nlp_DID, icmd->ulpIoTag, cmdSize);
	}

	return elsiocb;
}


static int
lpfc_cmpl_els_flogi_fabric(struct lpfc_hba *phba, struct lpfc_nodelist *ndlp,
		struct serv_parm *sp, IOCB_t *irsp)
{
	LPFC_MBOXQ_t *mbox;
	int rc;

	spin_lock_irq(phba->host->host_lock);
	phba->fc_flag |= FC_FABRIC;
	spin_unlock_irq(phba->host->host_lock);

	phba->fc_edtov = be32_to_cpu(sp->cmn.e_d_tov);
	if (sp->cmn.edtovResolution)	/* E_D_TOV ticks are in nanoseconds */
		phba->fc_edtov = (phba->fc_edtov + 999999) / 1000000;

	phba->fc_ratov = (be32_to_cpu(sp->cmn.w2.r_a_tov) + 999) / 1000;

	if (phba->fc_topology == TOPOLOGY_LOOP) {
		spin_lock_irq(phba->host->host_lock);
		phba->fc_flag |= FC_PUBLIC_LOOP;
		spin_unlock_irq(phba->host->host_lock);
	} else {
		/*
		 * If we are a N-port connected to a Fabric, fixup sparam's so
		 * logins to devices on remote loops work.
		 */
		phba->fc_sparam.cmn.altBbCredit = 1;
	}

	phba->fc_myDID = irsp->un.ulpWord[4] & Mask_DID;
	memcpy(&ndlp->nlp_portname, &sp->portName, sizeof(struct lpfc_name));
	memcpy(&ndlp->nlp_nodename, &sp->nodeName, sizeof (struct lpfc_name));
	ndlp->nlp_class_sup = 0;
	if (sp->cls1.classValid)
		ndlp->nlp_class_sup |= FC_COS_CLASS1;
	if (sp->cls2.classValid)
		ndlp->nlp_class_sup |= FC_COS_CLASS2;
	if (sp->cls3.classValid)
		ndlp->nlp_class_sup |= FC_COS_CLASS3;
	if (sp->cls4.classValid)
		ndlp->nlp_class_sup |= FC_COS_CLASS4;
	ndlp->nlp_maxframe = ((sp->cmn.bbRcvSizeMsb & 0x0F) << 8) |
				sp->cmn.bbRcvSizeLsb;
	memcpy(&phba->fc_fabparam, sp, sizeof(struct serv_parm));

	mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!mbox)
		goto fail;

	phba->hba_state = LPFC_FABRIC_CFG_LINK;
	lpfc_config_link(phba, mbox);
	mbox->mbox_cmpl = lpfc_sli_def_mbox_cmpl;

	rc = lpfc_sli_issue_mbox(phba, mbox, MBX_NOWAIT | MBX_STOP_IOCB);
	if (rc == MBX_NOT_FINISHED)
		goto fail_free_mbox;

	mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!mbox)
		goto fail;

	if (lpfc_reg_login(phba, Fabric_DID, (uint8_t *) sp, mbox, 0))
		goto fail_free_mbox;

	mbox->mbox_cmpl = lpfc_mbx_cmpl_fabric_reg_login;
	mbox->context2 = ndlp;

	rc = lpfc_sli_issue_mbox(phba, mbox, MBX_NOWAIT | MBX_STOP_IOCB);
	if (rc == MBX_NOT_FINISHED)
		goto fail_free_mbox;

	return 0;

 fail_free_mbox:
	mempool_free(mbox, phba->mbox_mem_pool);
 fail:
	return -ENXIO;
}

/*
 * We FLOGIed into an NPort, initiate pt2pt protocol
 */
static int
lpfc_cmpl_els_flogi_nport(struct lpfc_hba *phba, struct lpfc_nodelist *ndlp,
		struct serv_parm *sp)
{
	LPFC_MBOXQ_t *mbox;
	int rc;

	spin_lock_irq(phba->host->host_lock);
	phba->fc_flag &= ~(FC_FABRIC | FC_PUBLIC_LOOP);
	spin_unlock_irq(phba->host->host_lock);

	phba->fc_edtov = FF_DEF_EDTOV;
	phba->fc_ratov = FF_DEF_RATOV;
	rc = memcmp(&phba->fc_portname, &sp->portName,
			sizeof(struct lpfc_name));
	if (rc >= 0) {
		/* This side will initiate the PLOGI */
		spin_lock_irq(phba->host->host_lock);
		phba->fc_flag |= FC_PT2PT_PLOGI;
		spin_unlock_irq(phba->host->host_lock);

		/*
		 * N_Port ID cannot be 0, set our to LocalID the other
		 * side will be RemoteID.
		 */

		/* not equal */
		if (rc)
			phba->fc_myDID = PT2PT_LocalID;

		mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
		if (!mbox)
			goto fail;

		lpfc_config_link(phba, mbox);

		mbox->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
		rc = lpfc_sli_issue_mbox(phba, mbox,
				MBX_NOWAIT | MBX_STOP_IOCB);
		if (rc == MBX_NOT_FINISHED) {
			mempool_free(mbox, phba->mbox_mem_pool);
			goto fail;
		}
		mempool_free(ndlp, phba->nlp_mem_pool);

		ndlp = lpfc_findnode_did(phba, NLP_SEARCH_ALL, PT2PT_RemoteID);
		if (!ndlp) {
			/*
			 * Cannot find existing Fabric ndlp, so allocate a
			 * new one
			 */
			ndlp = mempool_alloc(phba->nlp_mem_pool, GFP_KERNEL);
			if (!ndlp)
				goto fail;

			lpfc_nlp_init(phba, ndlp, PT2PT_RemoteID);
		}

		memcpy(&ndlp->nlp_portname, &sp->portName,
				sizeof(struct lpfc_name));
		memcpy(&ndlp->nlp_nodename, &sp->nodeName,
				sizeof(struct lpfc_name));
		ndlp->nlp_state = NLP_STE_NPR_NODE;
		lpfc_nlp_list(phba, ndlp, NLP_NPR_LIST);
		ndlp->nlp_flag |= NLP_NPR_2B_DISC;
	} else {
		/* This side will wait for the PLOGI */
		mempool_free( ndlp, phba->nlp_mem_pool);
	}

	spin_lock_irq(phba->host->host_lock);
	phba->fc_flag |= FC_PT2PT;
	spin_unlock_irq(phba->host->host_lock);

	/* Start discovery - this should just do CLEAR_LA */
	lpfc_disc_start(phba);
	return 0;
 fail:
	return -ENXIO;
}

static void
lpfc_cmpl_els_flogi(struct lpfc_hba * phba,
		    struct lpfc_iocbq * cmdiocb, struct lpfc_iocbq * rspiocb)
{
	IOCB_t *irsp = &rspiocb->iocb;
	struct lpfc_nodelist *ndlp = cmdiocb->context1;
	struct lpfc_dmabuf *pcmd = cmdiocb->context2, *prsp;
	struct serv_parm *sp;
	int rc;

	/* Check to see if link went down during discovery */
	if (lpfc_els_chk_latt(phba)) {
		lpfc_nlp_remove(phba, ndlp);
		goto out;
	}

	if (irsp->ulpStatus) {
		/* Check for retry */
		if (lpfc_els_retry(phba, cmdiocb, rspiocb)) {
			/* ELS command is being retried */
			goto out;
		}
		/* FLOGI failed, so there is no fabric */
		spin_lock_irq(phba->host->host_lock);
		phba->fc_flag &= ~(FC_FABRIC | FC_PUBLIC_LOOP);
		spin_unlock_irq(phba->host->host_lock);

		/* If private loop, then allow max outstandting els to be
		 * LPFC_MAX_DISC_THREADS (32). Scanning in the case of no
		 * alpa map would take too long otherwise.
		 */
		if (phba->alpa_map[0] == 0) {
			phba->cfg_discovery_threads =
			    LPFC_MAX_DISC_THREADS;
		}

		/* FLOGI failure */
		lpfc_printf_log(phba,
				KERN_INFO,
				LOG_ELS,
				"%d:0100 FLOGI failure Data: x%x x%x x%x\n",
				phba->brd_no,
				irsp->ulpStatus, irsp->un.ulpWord[4],
				irsp->ulpTimeout);
		goto flogifail;
	}

	/*
	 * The FLogI succeeded.  Sync the data for the CPU before
	 * accessing it.
	 */
	prsp = list_get_first(&pcmd->list, struct lpfc_dmabuf, list);

	sp = prsp->virt + sizeof(uint32_t);

	/* FLOGI completes successfully */
	lpfc_printf_log(phba, KERN_INFO, LOG_ELS,
			"%d:0101 FLOGI completes sucessfully "
			"Data: x%x x%x x%x x%x\n",
			phba->brd_no,
			irsp->un.ulpWord[4], sp->cmn.e_d_tov,
			sp->cmn.w2.r_a_tov, sp->cmn.edtovResolution);

	if (phba->hba_state == LPFC_FLOGI) {
		/*
		 * If Common Service Parameters indicate Nport
		 * we are point to point, if Fport we are Fabric.
		 */
		if (sp->cmn.fPort)
			rc = lpfc_cmpl_els_flogi_fabric(phba, ndlp, sp, irsp);
		else
			rc = lpfc_cmpl_els_flogi_nport(phba, ndlp, sp);

		if (!rc)
			goto out;
	}

flogifail:
	lpfc_nlp_remove(phba, ndlp);

	if (irsp->ulpStatus != IOSTAT_LOCAL_REJECT ||
	    (irsp->un.ulpWord[4] != IOERR_SLI_ABORTED &&
	     irsp->un.ulpWord[4] != IOERR_SLI_DOWN)) {
		/* FLOGI failed, so just use loop map to make discovery list */
		lpfc_disc_list_loopmap(phba);

		/* Start discovery */
		lpfc_disc_start(phba);
	}

out:
	lpfc_els_free_iocb(phba, cmdiocb);
}

static int
lpfc_issue_els_flogi(struct lpfc_hba * phba, struct lpfc_nodelist * ndlp,
		     uint8_t retry)
{
	struct serv_parm *sp;
	IOCB_t *icmd;
	struct lpfc_iocbq *elsiocb;
	struct lpfc_sli_ring *pring;
	uint8_t *pcmd;
	uint16_t cmdsize;
	uint32_t tmo;
	int rc;

	pring = &phba->sli.ring[LPFC_ELS_RING];

	cmdsize = (sizeof (uint32_t) + sizeof (struct serv_parm));
	elsiocb = lpfc_prep_els_iocb(phba, 1, cmdsize, retry, ndlp,
						 ndlp->nlp_DID, ELS_CMD_FLOGI);
	if (!elsiocb)
		return 1;

	icmd = &elsiocb->iocb;
	pcmd = (uint8_t *) (((struct lpfc_dmabuf *) elsiocb->context2)->virt);

	/* For FLOGI request, remainder of payload is service parameters */
	*((uint32_t *) (pcmd)) = ELS_CMD_FLOGI;
	pcmd += sizeof (uint32_t);
	memcpy(pcmd, &phba->fc_sparam, sizeof (struct serv_parm));
	sp = (struct serv_parm *) pcmd;

	/* Setup CSPs accordingly for Fabric */
	sp->cmn.e_d_tov = 0;
	sp->cmn.w2.r_a_tov = 0;
	sp->cls1.classValid = 0;
	sp->cls2.seqDelivery = 1;
	sp->cls3.seqDelivery = 1;
	if (sp->cmn.fcphLow < FC_PH3)
		sp->cmn.fcphLow = FC_PH3;
	if (sp->cmn.fcphHigh < FC_PH3)
		sp->cmn.fcphHigh = FC_PH3;

	tmo = phba->fc_ratov;
	phba->fc_ratov = LPFC_DISC_FLOGI_TMO;
	lpfc_set_disctmo(phba);
	phba->fc_ratov = tmo;

	phba->fc_stat.elsXmitFLOGI++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_flogi;
	spin_lock_irq(phba->host->host_lock);
	rc = lpfc_sli_issue_iocb(phba, pring, elsiocb, 0);
	spin_unlock_irq(phba->host->host_lock);
	if (rc == IOCB_ERROR) {
		lpfc_els_free_iocb(phba, elsiocb);
		return 1;
	}
	return 0;
}

int
lpfc_els_abort_flogi(struct lpfc_hba * phba)
{
	struct lpfc_sli_ring *pring;
	struct lpfc_iocbq *iocb, *next_iocb;
	struct lpfc_nodelist *ndlp;
	IOCB_t *icmd;

	/* Abort outstanding I/O on NPort <nlp_DID> */
	lpfc_printf_log(phba, KERN_INFO, LOG_DISCOVERY,
			"%d:0201 Abort outstanding I/O on NPort x%x\n",
			phba->brd_no, Fabric_DID);

	pring = &phba->sli.ring[LPFC_ELS_RING];

	/*
	 * Check the txcmplq for an iocb that matches the nport the driver is
	 * searching for.
	 */
	spin_lock_irq(phba->host->host_lock);
	list_for_each_entry_safe(iocb, next_iocb, &pring->txcmplq, list) {
		icmd = &iocb->iocb;
		if (icmd->ulpCommand == CMD_ELS_REQUEST64_CR) {
			ndlp = (struct lpfc_nodelist *)(iocb->context1);
			if (ndlp && (ndlp->nlp_DID == Fabric_DID)) {
				list_del(&iocb->list);
				pring->txcmplq_cnt--;

				if ((icmd->un.elsreq64.bdl.ulpIoTag32)) {
					lpfc_sli_issue_abort_iotag32
						(phba, pring, iocb);
				}
				if (iocb->iocb_cmpl) {
					icmd->ulpStatus = IOSTAT_LOCAL_REJECT;
					icmd->un.ulpWord[4] =
					    IOERR_SLI_ABORTED;
					spin_unlock_irq(phba->host->host_lock);
					(iocb->iocb_cmpl) (phba, iocb, iocb);
					spin_lock_irq(phba->host->host_lock);
				} else
					lpfc_sli_release_iocbq(phba, iocb);
			}
		}
	}
	spin_unlock_irq(phba->host->host_lock);

	return 0;
}

int
lpfc_initial_flogi(struct lpfc_hba * phba)
{
	struct lpfc_nodelist *ndlp;

	/* First look for the Fabric ndlp */
	ndlp = lpfc_findnode_did(phba, NLP_SEARCH_ALL, Fabric_DID);
	if (!ndlp) {
		/* Cannot find existing Fabric ndlp, so allocate a new one */
		ndlp = mempool_alloc(phba->nlp_mem_pool, GFP_KERNEL);
		if (!ndlp)
			return 0;
		lpfc_nlp_init(phba, ndlp, Fabric_DID);
	} else {
		lpfc_nlp_list(phba, ndlp, NLP_JUST_DQ);
	}
	if (lpfc_issue_els_flogi(phba, ndlp, 0)) {
		mempool_free( ndlp, phba->nlp_mem_pool);
	}
	return 1;
}

static void
lpfc_more_plogi(struct lpfc_hba * phba)
{
	int sentplogi;

	if (phba->num_disc_nodes)
		phba->num_disc_nodes--;

	/* Continue discovery with <num_disc_nodes> PLOGIs to go */
	lpfc_printf_log(phba, KERN_INFO, LOG_DISCOVERY,
			"%d:0232 Continue discovery with %d PLOGIs to go "
			"Data: x%x x%x x%x\n",
			phba->brd_no, phba->num_disc_nodes, phba->fc_plogi_cnt,
			phba->fc_flag, phba->hba_state);

	/* Check to see if there are more PLOGIs to be sent */
	if (phba->fc_flag & FC_NLP_MORE) {
		/* go thru NPR list and issue any remaining ELS PLOGIs */
		sentplogi = lpfc_els_disc_plogi(phba);
	}
	return;
}

static struct lpfc_nodelist *
lpfc_plogi_confirm_nport(struct lpfc_hba * phba, struct lpfc_dmabuf *prsp,
			 struct lpfc_nodelist *ndlp)
{
	struct lpfc_nodelist *new_ndlp;
	uint32_t *lp;
	struct serv_parm *sp;
	uint8_t name[sizeof (struct lpfc_name)];
	uint32_t rc;

	/* Fabric nodes can have the same WWPN so we don't bother searching
	 * by WWPN.  Just return the ndlp that was given to us.
	 */
	if (ndlp->nlp_type & NLP_FABRIC)
		return ndlp;

	lp = (uint32_t *) prsp->virt;
	sp = (struct serv_parm *) ((uint8_t *) lp + sizeof (uint32_t));
	memset(name, 0, sizeof (struct lpfc_name));

	/* Now we to find out if the NPort we are logging into, matches the WWPN
	 * we have for that ndlp. If not, we have some work to do.
	 */
	new_ndlp = lpfc_findnode_wwpn(phba, NLP_SEARCH_ALL, &sp->portName);

	if (new_ndlp == ndlp)
		return ndlp;

	if (!new_ndlp) {
		rc =
		   memcmp(&ndlp->nlp_portname, name, sizeof(struct lpfc_name));
		if (!rc)
			return ndlp;
		new_ndlp = mempool_alloc(phba->nlp_mem_pool, GFP_ATOMIC);
		if (!new_ndlp)
			return ndlp;

		lpfc_nlp_init(phba, new_ndlp, ndlp->nlp_DID);
	}

	lpfc_unreg_rpi(phba, new_ndlp);
	new_ndlp->nlp_DID = ndlp->nlp_DID;
	new_ndlp->nlp_prev_state = ndlp->nlp_prev_state;
	new_ndlp->nlp_state = ndlp->nlp_state;
	lpfc_nlp_list(phba, new_ndlp, ndlp->nlp_flag & NLP_LIST_MASK);

	/* Move this back to NPR list */
	if (memcmp(&ndlp->nlp_portname, name, sizeof(struct lpfc_name)) == 0) {
		lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
	}
	else {
		lpfc_unreg_rpi(phba, ndlp);
		ndlp->nlp_DID = 0; /* Two ndlps cannot have the same did */
		ndlp->nlp_state = NLP_STE_NPR_NODE;
		lpfc_nlp_list(phba, ndlp, NLP_NPR_LIST);
	}
	return new_ndlp;
}

static void
lpfc_cmpl_els_plogi(struct lpfc_hba * phba, struct lpfc_iocbq * cmdiocb,
		    struct lpfc_iocbq * rspiocb)
{
	IOCB_t *irsp;
	struct lpfc_nodelist *ndlp;
	struct lpfc_dmabuf *prsp;
	int disc, rc, did, type;


	/* we pass cmdiocb to state machine which needs rspiocb as well */
	cmdiocb->context_un.rsp_iocb = rspiocb;

	irsp = &rspiocb->iocb;
	ndlp = lpfc_findnode_did(phba, NLP_SEARCH_ALL,
						irsp->un.elsreq64.remoteID);
	if (!ndlp)
		goto out;

	/* Since ndlp can be freed in the disc state machine, note if this node
	 * is being used during discovery.
	 */
	disc = (ndlp->nlp_flag & NLP_NPR_2B_DISC);
	spin_lock_irq(phba->host->host_lock);
	ndlp->nlp_flag &= ~NLP_NPR_2B_DISC;
	spin_unlock_irq(phba->host->host_lock);
	rc   = 0;

	/* PLOGI completes to NPort <nlp_DID> */
	lpfc_printf_log(phba, KERN_INFO, LOG_ELS,
			"%d:0102 PLOGI completes to NPort x%x "
			"Data: x%x x%x x%x x%x x%x\n",
			phba->brd_no, ndlp->nlp_DID, irsp->ulpStatus,
			irsp->un.ulpWord[4], irsp->ulpTimeout, disc,
			phba->num_disc_nodes);

	/* Check to see if link went down during discovery */
	if (lpfc_els_chk_latt(phba)) {
		spin_lock_irq(phba->host->host_lock);
		ndlp->nlp_flag |= NLP_NPR_2B_DISC;
		spin_unlock_irq(phba->host->host_lock);
		goto out;
	}

	/* ndlp could be freed in DSM, save these values now */
	type = ndlp->nlp_type;
	did = ndlp->nlp_DID;

	if (irsp->ulpStatus) {
		/* Check for retry */
		if (lpfc_els_retry(phba, cmdiocb, rspiocb)) {
			/* ELS command is being retried */
			if (disc) {
				spin_lock_irq(phba->host->host_lock);
				ndlp->nlp_flag |= NLP_NPR_2B_DISC;
				spin_unlock_irq(phba->host->host_lock);
			}
			goto out;
		}

		/* PLOGI failed */
		/* Do not call DSM for lpfc_els_abort'ed ELS cmds */
		if ((irsp->ulpStatus == IOSTAT_LOCAL_REJECT) &&
		   ((irsp->un.ulpWord[4] == IOERR_SLI_ABORTED) ||
		   (irsp->un.ulpWord[4] == IOERR_LINK_DOWN) ||
		   (irsp->un.ulpWord[4] == IOERR_SLI_DOWN))) {
			rc = NLP_STE_FREED_NODE;
		} else {
			rc = lpfc_disc_state_machine(phba, ndlp, cmdiocb,
					NLP_EVT_CMPL_PLOGI);
		}
	} else {
		/* Good status, call state machine */
		prsp = list_entry(((struct lpfc_dmabuf *)
			cmdiocb->context2)->list.next,
			struct lpfc_dmabuf, list);
		ndlp = lpfc_plogi_confirm_nport(phba, prsp, ndlp);
		rc = lpfc_disc_state_machine(phba, ndlp, cmdiocb,
					NLP_EVT_CMPL_PLOGI);
	}

	if (disc && phba->num_disc_nodes) {
		/* Check to see if there are more PLOGIs to be sent */
		lpfc_more_plogi(phba);

		if (phba->num_disc_nodes == 0) {
			spin_lock_irq(phba->host->host_lock);
			phba->fc_flag &= ~FC_NDISC_ACTIVE;
			spin_unlock_irq(phba->host->host_lock);

			lpfc_can_disctmo(phba);
			if (phba->fc_flag & FC_RSCN_MODE) {
				/*
				 * Check to see if more RSCNs came in while
				 * we were processing this one.
				 */
				if ((phba->fc_rscn_id_cnt == 0) &&
			    	(!(phba->fc_flag & FC_RSCN_DISCOVERY))) {
					spin_lock_irq(phba->host->host_lock);
					phba->fc_flag &= ~FC_RSCN_MODE;
					spin_unlock_irq(phba->host->host_lock);
				} else {
					lpfc_els_handle_rscn(phba);
				}
			}
		}
	}

out:
	lpfc_els_free_iocb(phba, cmdiocb);
	return;
}

int
lpfc_issue_els_plogi(struct lpfc_hba * phba, uint32_t did, uint8_t retry)
{
	struct serv_parm *sp;
	IOCB_t *icmd;
	struct lpfc_iocbq *elsiocb;
	struct lpfc_sli_ring *pring;
	struct lpfc_sli *psli;
	uint8_t *pcmd;
	uint16_t cmdsize;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */

	cmdsize = (sizeof (uint32_t) + sizeof (struct serv_parm));
	elsiocb = lpfc_prep_els_iocb(phba, 1, cmdsize, retry, NULL, did,
								ELS_CMD_PLOGI);
	if (!elsiocb)
		return 1;

	icmd = &elsiocb->iocb;
	pcmd = (uint8_t *) (((struct lpfc_dmabuf *) elsiocb->context2)->virt);

	/* For PLOGI request, remainder of payload is service parameters */
	*((uint32_t *) (pcmd)) = ELS_CMD_PLOGI;
	pcmd += sizeof (uint32_t);
	memcpy(pcmd, &phba->fc_sparam, sizeof (struct serv_parm));
	sp = (struct serv_parm *) pcmd;

	if (sp->cmn.fcphLow < FC_PH_4_3)
		sp->cmn.fcphLow = FC_PH_4_3;

	if (sp->cmn.fcphHigh < FC_PH3)
		sp->cmn.fcphHigh = FC_PH3;

	phba->fc_stat.elsXmitPLOGI++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_plogi;
	spin_lock_irq(phba->host->host_lock);
	if (lpfc_sli_issue_iocb(phba, pring, elsiocb, 0) == IOCB_ERROR) {
		spin_unlock_irq(phba->host->host_lock);
		lpfc_els_free_iocb(phba, elsiocb);
		return 1;
	}
	spin_unlock_irq(phba->host->host_lock);
	return 0;
}

static void
lpfc_cmpl_els_prli(struct lpfc_hba * phba, struct lpfc_iocbq * cmdiocb,
		   struct lpfc_iocbq * rspiocb)
{
	IOCB_t *irsp;
	struct lpfc_sli *psli;
	struct lpfc_nodelist *ndlp;

	psli = &phba->sli;
	/* we pass cmdiocb to state machine which needs rspiocb as well */
	cmdiocb->context_un.rsp_iocb = rspiocb;

	irsp = &(rspiocb->iocb);
	ndlp = (struct lpfc_nodelist *) cmdiocb->context1;
	spin_lock_irq(phba->host->host_lock);
	ndlp->nlp_flag &= ~NLP_PRLI_SND;
	spin_unlock_irq(phba->host->host_lock);

	/* PRLI completes to NPort <nlp_DID> */
	lpfc_printf_log(phba, KERN_INFO, LOG_ELS,
			"%d:0103 PRLI completes to NPort x%x "
			"Data: x%x x%x x%x x%x\n",
			phba->brd_no, ndlp->nlp_DID, irsp->ulpStatus,
			irsp->un.ulpWord[4], irsp->ulpTimeout,
			phba->num_disc_nodes);

	phba->fc_prli_sent--;
	/* Check to see if link went down during discovery */
	if (lpfc_els_chk_latt(phba))
		goto out;

	if (irsp->ulpStatus) {
		/* Check for retry */
		if (lpfc_els_retry(phba, cmdiocb, rspiocb)) {
			/* ELS command is being retried */
			goto out;
		}
		/* PRLI failed */
		/* Do not call DSM for lpfc_els_abort'ed ELS cmds */
		if ((irsp->ulpStatus == IOSTAT_LOCAL_REJECT) &&
		   ((irsp->un.ulpWord[4] == IOERR_SLI_ABORTED) ||
		   (irsp->un.ulpWord[4] == IOERR_LINK_DOWN) ||
		   (irsp->un.ulpWord[4] == IOERR_SLI_DOWN))) {
			goto out;
		} else {
			lpfc_disc_state_machine(phba, ndlp, cmdiocb,
					NLP_EVT_CMPL_PRLI);
		}
	} else {
		/* Good status, call state machine */
		lpfc_disc_state_machine(phba, ndlp, cmdiocb, NLP_EVT_CMPL_PRLI);
	}

out:
	lpfc_els_free_iocb(phba, cmdiocb);
	return;
}

int
lpfc_issue_els_prli(struct lpfc_hba * phba, struct lpfc_nodelist * ndlp,
		    uint8_t retry)
{
	PRLI *npr;
	IOCB_t *icmd;
	struct lpfc_iocbq *elsiocb;
	struct lpfc_sli_ring *pring;
	struct lpfc_sli *psli;
	uint8_t *pcmd;
	uint16_t cmdsize;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */

	cmdsize = (sizeof (uint32_t) + sizeof (PRLI));
	elsiocb = lpfc_prep_els_iocb(phba, 1, cmdsize, retry, ndlp,
					ndlp->nlp_DID, ELS_CMD_PRLI);
	if (!elsiocb)
		return 1;

	icmd = &elsiocb->iocb;
	pcmd = (uint8_t *) (((struct lpfc_dmabuf *) elsiocb->context2)->virt);

	/* For PRLI request, remainder of payload is service parameters */
	memset(pcmd, 0, (sizeof (PRLI) + sizeof (uint32_t)));
	*((uint32_t *) (pcmd)) = ELS_CMD_PRLI;
	pcmd += sizeof (uint32_t);

	/* For PRLI, remainder of payload is PRLI parameter page */
	npr = (PRLI *) pcmd;
	/*
	 * If our firmware version is 3.20 or later,
	 * set the following bits for FC-TAPE support.
	 */
	if (phba->vpd.rev.feaLevelHigh >= 0x02) {
		npr->ConfmComplAllowed = 1;
		npr->Retry = 1;
		npr->TaskRetryIdReq = 1;
	}
	npr->estabImagePair = 1;
	npr->readXferRdyDis = 1;

	/* For FCP support */
	npr->prliType = PRLI_FCP_TYPE;
	npr->initiatorFunc = 1;

	phba->fc_stat.elsXmitPRLI++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_prli;
	spin_lock_irq(phba->host->host_lock);
	ndlp->nlp_flag |= NLP_PRLI_SND;
	if (lpfc_sli_issue_iocb(phba, pring, elsiocb, 0) == IOCB_ERROR) {
		ndlp->nlp_flag &= ~NLP_PRLI_SND;
		spin_unlock_irq(phba->host->host_lock);
		lpfc_els_free_iocb(phba, elsiocb);
		return 1;
	}
	spin_unlock_irq(phba->host->host_lock);
	phba->fc_prli_sent++;
	return 0;
}

static void
lpfc_more_adisc(struct lpfc_hba * phba)
{
	int sentadisc;

	if (phba->num_disc_nodes)
		phba->num_disc_nodes--;

	/* Continue discovery with <num_disc_nodes> ADISCs to go */
	lpfc_printf_log(phba, KERN_INFO, LOG_DISCOVERY,
			"%d:0210 Continue discovery with %d ADISCs to go "
			"Data: x%x x%x x%x\n",
			phba->brd_no, phba->num_disc_nodes, phba->fc_adisc_cnt,
			phba->fc_flag, phba->hba_state);

	/* Check to see if there are more ADISCs to be sent */
	if (phba->fc_flag & FC_NLP_MORE) {
		lpfc_set_disctmo(phba);

		/* go thru NPR list and issue any remaining ELS ADISCs */
		sentadisc = lpfc_els_disc_adisc(phba);
	}
	return;
}

static void
lpfc_rscn_disc(struct lpfc_hba * phba)
{
	/* RSCN discovery */
	/* go thru NPR list and issue ELS PLOGIs */
	if (phba->fc_npr_cnt) {
		if (lpfc_els_disc_plogi(phba))
			return;
	}
	if (phba->fc_flag & FC_RSCN_MODE) {
		/* Check to see if more RSCNs came in while we were
		 * processing this one.
		 */
		if ((phba->fc_rscn_id_cnt == 0) &&
		    (!(phba->fc_flag & FC_RSCN_DISCOVERY))) {
			spin_lock_irq(phba->host->host_lock);
			phba->fc_flag &= ~FC_RSCN_MODE;
			spin_unlock_irq(phba->host->host_lock);
		} else {
			lpfc_els_handle_rscn(phba);
		}
	}
}

static void
lpfc_cmpl_els_adisc(struct lpfc_hba * phba, struct lpfc_iocbq * cmdiocb,
		    struct lpfc_iocbq * rspiocb)
{
	IOCB_t *irsp;
	struct lpfc_sli *psli;
	struct lpfc_nodelist *ndlp;
	LPFC_MBOXQ_t *mbox;
	int disc, rc;

	psli = &phba->sli;

	/* we pass cmdiocb to state machine which needs rspiocb as well */
	cmdiocb->context_un.rsp_iocb = rspiocb;

	irsp = &(rspiocb->iocb);
	ndlp = (struct lpfc_nodelist *) cmdiocb->context1;

	/* Since ndlp can be freed in the disc state machine, note if this node
	 * is being used during discovery.
	 */
	disc = (ndlp->nlp_flag & NLP_NPR_2B_DISC);
	spin_lock_irq(phba->host->host_lock);
	ndlp->nlp_flag &= ~(NLP_ADISC_SND | NLP_NPR_2B_DISC);
	spin_unlock_irq(phba->host->host_lock);

	/* ADISC completes to NPort <nlp_DID> */
	lpfc_printf_log(phba, KERN_INFO, LOG_ELS,
			"%d:0104 ADISC completes to NPort x%x "
			"Data: x%x x%x x%x x%x x%x\n",
			phba->brd_no, ndlp->nlp_DID, irsp->ulpStatus,
			irsp->un.ulpWord[4], irsp->ulpTimeout, disc,
			phba->num_disc_nodes);

	/* Check to see if link went down during discovery */
	if (lpfc_els_chk_latt(phba)) {
		spin_lock_irq(phba->host->host_lock);
		ndlp->nlp_flag |= NLP_NPR_2B_DISC;
		spin_unlock_irq(phba->host->host_lock);
		goto out;
	}

	if (irsp->ulpStatus) {
		/* Check for retry */
		if (lpfc_els_retry(phba, cmdiocb, rspiocb)) {
			/* ELS command is being retried */
			if (disc) {
				spin_lock_irq(phba->host->host_lock);
				ndlp->nlp_flag |= NLP_NPR_2B_DISC;
				spin_unlock_irq(phba->host->host_lock);
				lpfc_set_disctmo(phba);
			}
			goto out;
		}
		/* ADISC failed */
		/* Do not call DSM for lpfc_els_abort'ed ELS cmds */
		if ((irsp->ulpStatus != IOSTAT_LOCAL_REJECT) ||
		   ((irsp->un.ulpWord[4] != IOERR_SLI_ABORTED) &&
		   (irsp->un.ulpWord[4] != IOERR_LINK_DOWN) &&
		   (irsp->un.ulpWord[4] != IOERR_SLI_DOWN))) {
			lpfc_disc_state_machine(phba, ndlp, cmdiocb,
					NLP_EVT_CMPL_ADISC);
		}
	} else {
		/* Good status, call state machine */
		lpfc_disc_state_machine(phba, ndlp, cmdiocb,
					NLP_EVT_CMPL_ADISC);
	}

	if (disc && phba->num_disc_nodes) {
		/* Check to see if there are more ADISCs to be sent */
		lpfc_more_adisc(phba);

		/* Check to see if we are done with ADISC authentication */
		if (phba->num_disc_nodes == 0) {
			lpfc_can_disctmo(phba);
			/* If we get here, there is nothing left to wait for */
			if ((phba->hba_state < LPFC_HBA_READY) &&
			    (phba->hba_state != LPFC_CLEAR_LA)) {
				/* Link up discovery */
				if ((mbox = mempool_alloc(phba->mbox_mem_pool,
							  GFP_KERNEL))) {
					phba->hba_state = LPFC_CLEAR_LA;
					lpfc_clear_la(phba, mbox);
					mbox->mbox_cmpl =
					    lpfc_mbx_cmpl_clear_la;
					rc = lpfc_sli_issue_mbox
						(phba, mbox,
						 (MBX_NOWAIT | MBX_STOP_IOCB));
					if (rc == MBX_NOT_FINISHED) {
						mempool_free(mbox,
						     phba->mbox_mem_pool);
						lpfc_disc_flush_list(phba);
						psli->ring[(psli->extra_ring)].
						    flag &=
						    ~LPFC_STOP_IOCB_EVENT;
						psli->ring[(psli->fcp_ring)].
						    flag &=
						    ~LPFC_STOP_IOCB_EVENT;
						psli->ring[(psli->next_ring)].
						    flag &=
						    ~LPFC_STOP_IOCB_EVENT;
						phba->hba_state =
						    LPFC_HBA_READY;
					}
				}
			} else {
				lpfc_rscn_disc(phba);
			}
		}
	}
out:
	lpfc_els_free_iocb(phba, cmdiocb);
	return;
}

int
lpfc_issue_els_adisc(struct lpfc_hba * phba, struct lpfc_nodelist * ndlp,
		     uint8_t retry)
{
	ADISC *ap;
	IOCB_t *icmd;
	struct lpfc_iocbq *elsiocb;
	struct lpfc_sli_ring *pring;
	struct lpfc_sli *psli;
	uint8_t *pcmd;
	uint16_t cmdsize;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */

	cmdsize = (sizeof (uint32_t) + sizeof (ADISC));
	elsiocb = lpfc_prep_els_iocb(phba, 1, cmdsize, retry, ndlp,
						ndlp->nlp_DID, ELS_CMD_ADISC);
	if (!elsiocb)
		return 1;

	icmd = &elsiocb->iocb;
	pcmd = (uint8_t *) (((struct lpfc_dmabuf *) elsiocb->context2)->virt);

	/* For ADISC request, remainder of payload is service parameters */
	*((uint32_t *) (pcmd)) = ELS_CMD_ADISC;
	pcmd += sizeof (uint32_t);

	/* Fill in ADISC payload */
	ap = (ADISC *) pcmd;
	ap->hardAL_PA = phba->fc_pref_ALPA;
	memcpy(&ap->portName, &phba->fc_portname, sizeof (struct lpfc_name));
	memcpy(&ap->nodeName, &phba->fc_nodename, sizeof (struct lpfc_name));
	ap->DID = be32_to_cpu(phba->fc_myDID);

	phba->fc_stat.elsXmitADISC++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_adisc;
	spin_lock_irq(phba->host->host_lock);
	ndlp->nlp_flag |= NLP_ADISC_SND;
	if (lpfc_sli_issue_iocb(phba, pring, elsiocb, 0) == IOCB_ERROR) {
		ndlp->nlp_flag &= ~NLP_ADISC_SND;
		spin_unlock_irq(phba->host->host_lock);
		lpfc_els_free_iocb(phba, elsiocb);
		return 1;
	}
	spin_unlock_irq(phba->host->host_lock);
	return 0;
}

static void
lpfc_cmpl_els_logo(struct lpfc_hba * phba, struct lpfc_iocbq * cmdiocb,
		   struct lpfc_iocbq * rspiocb)
{
	IOCB_t *irsp;
	struct lpfc_sli *psli;
	struct lpfc_nodelist *ndlp;

	psli = &phba->sli;
	/* we pass cmdiocb to state machine which needs rspiocb as well */
	cmdiocb->context_un.rsp_iocb = rspiocb;

	irsp = &(rspiocb->iocb);
	ndlp = (struct lpfc_nodelist *) cmdiocb->context1;
	spin_lock_irq(phba->host->host_lock);
	ndlp->nlp_flag &= ~NLP_LOGO_SND;
	spin_unlock_irq(phba->host->host_lock);

	/* LOGO completes to NPort <nlp_DID> */
	lpfc_printf_log(phba, KERN_INFO, LOG_ELS,
			"%d:0105 LOGO completes to NPort x%x "
			"Data: x%x x%x x%x x%x\n",
			phba->brd_no, ndlp->nlp_DID, irsp->ulpStatus,
			irsp->un.ulpWord[4], irsp->ulpTimeout,
			phba->num_disc_nodes);

	/* Check to see if link went down during discovery */
	if (lpfc_els_chk_latt(phba))
		goto out;

	if (irsp->ulpStatus) {
		/* Check for retry */
		if (lpfc_els_retry(phba, cmdiocb, rspiocb)) {
			/* ELS command is being retried */
			goto out;
		}
		/* LOGO failed */
		/* Do not call DSM for lpfc_els_abort'ed ELS cmds */
		if ((irsp->ulpStatus == IOSTAT_LOCAL_REJECT) &&
		   ((irsp->un.ulpWord[4] == IOERR_SLI_ABORTED) ||
		   (irsp->un.ulpWord[4] == IOERR_LINK_DOWN) ||
		   (irsp->un.ulpWord[4] == IOERR_SLI_DOWN))) {
			goto out;
		} else {
			lpfc_disc_state_machine(phba, ndlp, cmdiocb,
					NLP_EVT_CMPL_LOGO);
		}
	} else {
		/* Good status, call state machine.
		 * This will unregister the rpi if needed.
		 */
		lpfc_disc_state_machine(phba, ndlp, cmdiocb, NLP_EVT_CMPL_LOGO);
	}

out:
	lpfc_els_free_iocb(phba, cmdiocb);
	return;
}

int
lpfc_issue_els_logo(struct lpfc_hba * phba, struct lpfc_nodelist * ndlp,
		    uint8_t retry)
{
	IOCB_t *icmd;
	struct lpfc_iocbq *elsiocb;
	struct lpfc_sli_ring *pring;
	struct lpfc_sli *psli;
	uint8_t *pcmd;
	uint16_t cmdsize;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];

	cmdsize = (2 * sizeof (uint32_t)) + sizeof (struct lpfc_name);
	elsiocb = lpfc_prep_els_iocb(phba, 1, cmdsize, retry, ndlp,
						ndlp->nlp_DID, ELS_CMD_LOGO);
	if (!elsiocb)
		return 1;

	icmd = &elsiocb->iocb;
	pcmd = (uint8_t *) (((struct lpfc_dmabuf *) elsiocb->context2)->virt);
	*((uint32_t *) (pcmd)) = ELS_CMD_LOGO;
	pcmd += sizeof (uint32_t);

	/* Fill in LOGO payload */
	*((uint32_t *) (pcmd)) = be32_to_cpu(phba->fc_myDID);
	pcmd += sizeof (uint32_t);
	memcpy(pcmd, &phba->fc_portname, sizeof (struct lpfc_name));

	phba->fc_stat.elsXmitLOGO++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_logo;
	spin_lock_irq(phba->host->host_lock);
	ndlp->nlp_flag |= NLP_LOGO_SND;
	if (lpfc_sli_issue_iocb(phba, pring, elsiocb, 0) == IOCB_ERROR) {
		ndlp->nlp_flag &= ~NLP_LOGO_SND;
		spin_unlock_irq(phba->host->host_lock);
		lpfc_els_free_iocb(phba, elsiocb);
		return 1;
	}
	spin_unlock_irq(phba->host->host_lock);
	return 0;
}

static void
lpfc_cmpl_els_cmd(struct lpfc_hba * phba, struct lpfc_iocbq * cmdiocb,
		  struct lpfc_iocbq * rspiocb)
{
	IOCB_t *irsp;

	irsp = &rspiocb->iocb;

	/* ELS cmd tag <ulpIoTag> completes */
	lpfc_printf_log(phba,
			KERN_INFO,
			LOG_ELS,
			"%d:0106 ELS cmd tag x%x completes Data: x%x x%x x%x\n",
			phba->brd_no,
			irsp->ulpIoTag, irsp->ulpStatus,
			irsp->un.ulpWord[4], irsp->ulpTimeout);

	/* Check to see if link went down during discovery */
	lpfc_els_chk_latt(phba);
	lpfc_els_free_iocb(phba, cmdiocb);
	return;
}

int
lpfc_issue_els_scr(struct lpfc_hba * phba, uint32_t nportid, uint8_t retry)
{
	IOCB_t *icmd;
	struct lpfc_iocbq *elsiocb;
	struct lpfc_sli_ring *pring;
	struct lpfc_sli *psli;
	uint8_t *pcmd;
	uint16_t cmdsize;
	struct lpfc_nodelist *ndlp;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */
	cmdsize = (sizeof (uint32_t) + sizeof (SCR));
	ndlp = mempool_alloc(phba->nlp_mem_pool, GFP_KERNEL);
	if (!ndlp)
		return 1;

	lpfc_nlp_init(phba, ndlp, nportid);

	elsiocb = lpfc_prep_els_iocb(phba, 1, cmdsize, retry, ndlp,
						ndlp->nlp_DID, ELS_CMD_SCR);
	if (!elsiocb) {
		mempool_free( ndlp, phba->nlp_mem_pool);
		return 1;
	}

	icmd = &elsiocb->iocb;
	pcmd = (uint8_t *) (((struct lpfc_dmabuf *) elsiocb->context2)->virt);

	*((uint32_t *) (pcmd)) = ELS_CMD_SCR;
	pcmd += sizeof (uint32_t);

	/* For SCR, remainder of payload is SCR parameter page */
	memset(pcmd, 0, sizeof (SCR));
	((SCR *) pcmd)->Function = SCR_FUNC_FULL;

	phba->fc_stat.elsXmitSCR++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_cmd;
	spin_lock_irq(phba->host->host_lock);
	if (lpfc_sli_issue_iocb(phba, pring, elsiocb, 0) == IOCB_ERROR) {
		spin_unlock_irq(phba->host->host_lock);
		mempool_free( ndlp, phba->nlp_mem_pool);
		lpfc_els_free_iocb(phba, elsiocb);
		return 1;
	}
	spin_unlock_irq(phba->host->host_lock);
	mempool_free( ndlp, phba->nlp_mem_pool);
	return 0;
}

static int
lpfc_issue_els_farpr(struct lpfc_hba * phba, uint32_t nportid, uint8_t retry)
{
	IOCB_t *icmd;
	struct lpfc_iocbq *elsiocb;
	struct lpfc_sli_ring *pring;
	struct lpfc_sli *psli;
	FARP *fp;
	uint8_t *pcmd;
	uint32_t *lp;
	uint16_t cmdsize;
	struct lpfc_nodelist *ondlp;
	struct lpfc_nodelist *ndlp;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */
	cmdsize = (sizeof (uint32_t) + sizeof (FARP));
	ndlp = mempool_alloc(phba->nlp_mem_pool, GFP_KERNEL);
	if (!ndlp)
		return 1;
	lpfc_nlp_init(phba, ndlp, nportid);

	elsiocb = lpfc_prep_els_iocb(phba, 1, cmdsize, retry, ndlp,
						ndlp->nlp_DID, ELS_CMD_RNID);
	if (!elsiocb) {
		mempool_free( ndlp, phba->nlp_mem_pool);
		return 1;
	}

	icmd = &elsiocb->iocb;
	pcmd = (uint8_t *) (((struct lpfc_dmabuf *) elsiocb->context2)->virt);

	*((uint32_t *) (pcmd)) = ELS_CMD_FARPR;
	pcmd += sizeof (uint32_t);

	/* Fill in FARPR payload */
	fp = (FARP *) (pcmd);
	memset(fp, 0, sizeof (FARP));
	lp = (uint32_t *) pcmd;
	*lp++ = be32_to_cpu(nportid);
	*lp++ = be32_to_cpu(phba->fc_myDID);
	fp->Rflags = 0;
	fp->Mflags = (FARP_MATCH_PORT | FARP_MATCH_NODE);

	memcpy(&fp->RportName, &phba->fc_portname, sizeof (struct lpfc_name));
	memcpy(&fp->RnodeName, &phba->fc_nodename, sizeof (struct lpfc_name));
	if ((ondlp = lpfc_findnode_did(phba, NLP_SEARCH_ALL, nportid))) {
		memcpy(&fp->OportName, &ondlp->nlp_portname,
		       sizeof (struct lpfc_name));
		memcpy(&fp->OnodeName, &ondlp->nlp_nodename,
		       sizeof (struct lpfc_name));
	}

	phba->fc_stat.elsXmitFARPR++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_cmd;
	spin_lock_irq(phba->host->host_lock);
	if (lpfc_sli_issue_iocb(phba, pring, elsiocb, 0) == IOCB_ERROR) {
		spin_unlock_irq(phba->host->host_lock);
		mempool_free( ndlp, phba->nlp_mem_pool);
		lpfc_els_free_iocb(phba, elsiocb);
		return 1;
	}
	spin_unlock_irq(phba->host->host_lock);
	mempool_free( ndlp, phba->nlp_mem_pool);
	return 0;
}

void
lpfc_cancel_retry_delay_tmo(struct lpfc_hba *phba, struct lpfc_nodelist * nlp)
{
	nlp->nlp_flag &= ~NLP_DELAY_TMO;
	del_timer_sync(&nlp->nlp_delayfunc);
	nlp->nlp_last_elscmd = 0;

	if (!list_empty(&nlp->els_retry_evt.evt_listp))
		list_del_init(&nlp->els_retry_evt.evt_listp);

	if (nlp->nlp_flag & NLP_NPR_2B_DISC) {
		nlp->nlp_flag &= ~NLP_NPR_2B_DISC;
		if (phba->num_disc_nodes) {
			/* Check to see if there are more
			 * PLOGIs to be sent
			 */
			lpfc_more_plogi(phba);

			if (phba->num_disc_nodes == 0) {
				phba->fc_flag &= ~FC_NDISC_ACTIVE;
				lpfc_can_disctmo(phba);
				if (phba->fc_flag & FC_RSCN_MODE) {
					/*
					 * Check to see if more RSCNs
					 * came in while we were
					 * processing this one.
					 */
					if((phba->fc_rscn_id_cnt==0) &&
					 !(phba->fc_flag & FC_RSCN_DISCOVERY)) {
						phba->fc_flag &= ~FC_RSCN_MODE;
					}
					else {
						lpfc_els_handle_rscn(phba);
					}
				}
			}
		}
	}
	return;
}

void
lpfc_els_retry_delay(unsigned long ptr)
{
	struct lpfc_nodelist *ndlp;
	struct lpfc_hba *phba;
	unsigned long iflag;
	struct lpfc_work_evt  *evtp;

	ndlp = (struct lpfc_nodelist *)ptr;
	phba = ndlp->nlp_phba;
	evtp = &ndlp->els_retry_evt;

	spin_lock_irqsave(phba->host->host_lock, iflag);
	if (!list_empty(&evtp->evt_listp)) {
		spin_unlock_irqrestore(phba->host->host_lock, iflag);
		return;
	}

	evtp->evt_arg1  = ndlp;
	evtp->evt       = LPFC_EVT_ELS_RETRY;
	list_add_tail(&evtp->evt_listp, &phba->work_list);
	if (phba->work_wait)
		wake_up(phba->work_wait);

	spin_unlock_irqrestore(phba->host->host_lock, iflag);
	return;
}

void
lpfc_els_retry_delay_handler(struct lpfc_nodelist *ndlp)
{
	struct lpfc_hba *phba;
	uint32_t cmd;
	uint32_t did;
	uint8_t retry;

	phba = ndlp->nlp_phba;
	spin_lock_irq(phba->host->host_lock);
	did = ndlp->nlp_DID;
	cmd = ndlp->nlp_last_elscmd;
	ndlp->nlp_last_elscmd = 0;

	if (!(ndlp->nlp_flag & NLP_DELAY_TMO)) {
		spin_unlock_irq(phba->host->host_lock);
		return;
	}

	ndlp->nlp_flag &= ~NLP_DELAY_TMO;
	spin_unlock_irq(phba->host->host_lock);
	/*
	 * If a discovery event readded nlp_delayfunc after timer
	 * firing and before processing the timer, cancel the
	 * nlp_delayfunc.
	 */
	del_timer_sync(&ndlp->nlp_delayfunc);
	retry = ndlp->nlp_retry;

	switch (cmd) {
	case ELS_CMD_FLOGI:
		lpfc_issue_els_flogi(phba, ndlp, retry);
		break;
	case ELS_CMD_PLOGI:
		if(!lpfc_issue_els_plogi(phba, ndlp->nlp_DID, retry)) {
			ndlp->nlp_prev_state = ndlp->nlp_state;
			ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
			lpfc_nlp_list(phba, ndlp, NLP_PLOGI_LIST);
		}
		break;
	case ELS_CMD_ADISC:
		if (!lpfc_issue_els_adisc(phba, ndlp, retry)) {
			ndlp->nlp_prev_state = ndlp->nlp_state;
			ndlp->nlp_state = NLP_STE_ADISC_ISSUE;
			lpfc_nlp_list(phba, ndlp, NLP_ADISC_LIST);
		}
		break;
	case ELS_CMD_PRLI:
		if (!lpfc_issue_els_prli(phba, ndlp, retry)) {
			ndlp->nlp_prev_state = ndlp->nlp_state;
			ndlp->nlp_state = NLP_STE_PRLI_ISSUE;
			lpfc_nlp_list(phba, ndlp, NLP_PRLI_LIST);
		}
		break;
	case ELS_CMD_LOGO:
		if (!lpfc_issue_els_logo(phba, ndlp, retry)) {
			ndlp->nlp_prev_state = ndlp->nlp_state;
			ndlp->nlp_state = NLP_STE_NPR_NODE;
			lpfc_nlp_list(phba, ndlp, NLP_NPR_LIST);
		}
		break;
	}
	return;
}

static int
lpfc_els_retry(struct lpfc_hba * phba, struct lpfc_iocbq * cmdiocb,
	       struct lpfc_iocbq * rspiocb)
{
	IOCB_t *irsp;
	struct lpfc_dmabuf *pcmd;
	struct lpfc_nodelist *ndlp;
	uint32_t *elscmd;
	struct ls_rjt stat;
	int retry, maxretry;
	int delay;
	uint32_t cmd;
	uint32_t did;

	retry = 0;
	delay = 0;
	maxretry = lpfc_max_els_tries;
	irsp = &rspiocb->iocb;
	ndlp = (struct lpfc_nodelist *) cmdiocb->context1;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;
	cmd = 0;

	/* Note: context2 may be 0 for internal driver abort
	 * of delays ELS command.
	 */

	if (pcmd && pcmd->virt) {
		elscmd = (uint32_t *) (pcmd->virt);
		cmd = *elscmd++;
	}

	if(ndlp)
		did = ndlp->nlp_DID;
	else {
		/* We should only hit this case for retrying PLOGI */
		did = irsp->un.elsreq64.remoteID;
		ndlp = lpfc_findnode_did(phba, NLP_SEARCH_ALL, did);
		if (!ndlp && (cmd != ELS_CMD_PLOGI))
			return 1;
	}

	switch (irsp->ulpStatus) {
	case IOSTAT_FCP_RSP_ERROR:
	case IOSTAT_REMOTE_STOP:
		break;

	case IOSTAT_LOCAL_REJECT:
		switch ((irsp->un.ulpWord[4] & 0xff)) {
		case IOERR_LOOP_OPEN_FAILURE:
			if (cmd == ELS_CMD_PLOGI) {
				if (cmdiocb->retry == 0) {
					delay = 1;
				}
			}
			retry = 1;
			break;

		case IOERR_SEQUENCE_TIMEOUT:
			retry = 1;
			break;

		case IOERR_NO_RESOURCES:
			if (cmd == ELS_CMD_PLOGI) {
				delay = 1;
			}
			retry = 1;
			break;

		case IOERR_INVALID_RPI:
			retry = 1;
			break;
		}
		break;

	case IOSTAT_NPORT_RJT:
	case IOSTAT_FABRIC_RJT:
		if (irsp->un.ulpWord[4] & RJT_UNAVAIL_TEMP) {
			retry = 1;
			break;
		}
		break;

	case IOSTAT_NPORT_BSY:
	case IOSTAT_FABRIC_BSY:
		retry = 1;
		break;

	case IOSTAT_LS_RJT:
		stat.un.lsRjtError = be32_to_cpu(irsp->un.ulpWord[4]);
		/* Added for Vendor specifc support
		 * Just keep retrying for these Rsn / Exp codes
		 */
		switch (stat.un.b.lsRjtRsnCode) {
		case LSRJT_UNABLE_TPC:
			if (stat.un.b.lsRjtRsnCodeExp ==
			    LSEXP_CMD_IN_PROGRESS) {
				if (cmd == ELS_CMD_PLOGI) {
					delay = 1;
					maxretry = 48;
				}
				retry = 1;
				break;
			}
			if (cmd == ELS_CMD_PLOGI) {
				delay = 1;
				maxretry = lpfc_max_els_tries + 1;
				retry = 1;
				break;
			}
			break;

		case LSRJT_LOGICAL_BSY:
			if (cmd == ELS_CMD_PLOGI) {
				delay = 1;
				maxretry = 48;
			}
			retry = 1;
			break;
		}
		break;

	case IOSTAT_INTERMED_RSP:
	case IOSTAT_BA_RJT:
		break;

	default:
		break;
	}

	if (did == FDMI_DID)
		retry = 1;

	if ((++cmdiocb->retry) >= maxretry) {
		phba->fc_stat.elsRetryExceeded++;
		retry = 0;
	}

	if (retry) {

		/* Retry ELS command <elsCmd> to remote NPORT <did> */
		lpfc_printf_log(phba, KERN_INFO, LOG_ELS,
				"%d:0107 Retry ELS command x%x to remote "
				"NPORT x%x Data: x%x x%x\n",
				phba->brd_no,
				cmd, did, cmdiocb->retry, delay);

		if ((cmd == ELS_CMD_PLOGI) || (cmd == ELS_CMD_ADISC)) {
			/* If discovery / RSCN timer is running, reset it */
			if (timer_pending(&phba->fc_disctmo) ||
			      (phba->fc_flag & FC_RSCN_MODE)) {
				lpfc_set_disctmo(phba);
			}
		}

		phba->fc_stat.elsXmitRetry++;
		if (ndlp && delay) {
			phba->fc_stat.elsDelayRetry++;
			ndlp->nlp_retry = cmdiocb->retry;

			mod_timer(&ndlp->nlp_delayfunc, jiffies + HZ);
			ndlp->nlp_flag |= NLP_DELAY_TMO;

			ndlp->nlp_prev_state = ndlp->nlp_state;
			ndlp->nlp_state = NLP_STE_NPR_NODE;
			lpfc_nlp_list(phba, ndlp, NLP_NPR_LIST);
			ndlp->nlp_last_elscmd = cmd;

			return 1;
		}
		switch (cmd) {
		case ELS_CMD_FLOGI:
			lpfc_issue_els_flogi(phba, ndlp, cmdiocb->retry);
			return 1;
		case ELS_CMD_PLOGI:
			if (ndlp) {
				ndlp->nlp_prev_state = ndlp->nlp_state;
				ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
				lpfc_nlp_list(phba, ndlp, NLP_PLOGI_LIST);
			}
			lpfc_issue_els_plogi(phba, did, cmdiocb->retry);
			return 1;
		case ELS_CMD_ADISC:
			ndlp->nlp_prev_state = ndlp->nlp_state;
			ndlp->nlp_state = NLP_STE_ADISC_ISSUE;
			lpfc_nlp_list(phba, ndlp, NLP_ADISC_LIST);
			lpfc_issue_els_adisc(phba, ndlp, cmdiocb->retry);
			return 1;
		case ELS_CMD_PRLI:
			ndlp->nlp_prev_state = ndlp->nlp_state;
			ndlp->nlp_state = NLP_STE_PRLI_ISSUE;
			lpfc_nlp_list(phba, ndlp, NLP_PRLI_LIST);
			lpfc_issue_els_prli(phba, ndlp, cmdiocb->retry);
			return 1;
		case ELS_CMD_LOGO:
			ndlp->nlp_prev_state = ndlp->nlp_state;
			ndlp->nlp_state = NLP_STE_NPR_NODE;
			lpfc_nlp_list(phba, ndlp, NLP_NPR_LIST);
			lpfc_issue_els_logo(phba, ndlp, cmdiocb->retry);
			return 1;
		}
	}

	/* No retry ELS command <elsCmd> to remote NPORT <did> */
	lpfc_printf_log(phba, KERN_INFO, LOG_ELS,
			"%d:0108 No retry ELS command x%x to remote NPORT x%x "
			"Data: x%x\n",
			phba->brd_no,
			cmd, did, cmdiocb->retry);

	return 0;
}

int
lpfc_els_free_iocb(struct lpfc_hba * phba, struct lpfc_iocbq * elsiocb)
{
	struct lpfc_dmabuf *buf_ptr, *buf_ptr1;

	/* context2  = cmd,  context2->next = rsp, context3 = bpl */
	if (elsiocb->context2) {
		buf_ptr1 = (struct lpfc_dmabuf *) elsiocb->context2;
		/* Free the response before processing the command.  */
		if (!list_empty(&buf_ptr1->list)) {
			list_remove_head(&buf_ptr1->list, buf_ptr,
					 struct lpfc_dmabuf,
					 list);
			lpfc_mbuf_free(phba, buf_ptr->virt, buf_ptr->phys);
			kfree(buf_ptr);
		}
		lpfc_mbuf_free(phba, buf_ptr1->virt, buf_ptr1->phys);
		kfree(buf_ptr1);
	}

	if (elsiocb->context3) {
		buf_ptr = (struct lpfc_dmabuf *) elsiocb->context3;
		lpfc_mbuf_free(phba, buf_ptr->virt, buf_ptr->phys);
		kfree(buf_ptr);
	}
	spin_lock_irq(phba->host->host_lock);
	lpfc_sli_release_iocbq(phba, elsiocb);
	spin_unlock_irq(phba->host->host_lock);
	return 0;
}

static void
lpfc_cmpl_els_logo_acc(struct lpfc_hba * phba, struct lpfc_iocbq * cmdiocb,
		       struct lpfc_iocbq * rspiocb)
{
	struct lpfc_nodelist *ndlp;

	ndlp = (struct lpfc_nodelist *) cmdiocb->context1;

	/* ACC to LOGO completes to NPort <nlp_DID> */
	lpfc_printf_log(phba, KERN_INFO, LOG_ELS,
			"%d:0109 ACC to LOGO completes to NPort x%x "
			"Data: x%x x%x x%x\n",
			phba->brd_no, ndlp->nlp_DID, ndlp->nlp_flag,
			ndlp->nlp_state, ndlp->nlp_rpi);

	switch (ndlp->nlp_state) {
	case NLP_STE_UNUSED_NODE:	/* node is just allocated */
		lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
		break;
	case NLP_STE_NPR_NODE:		/* NPort Recovery mode */
		lpfc_unreg_rpi(phba, ndlp);
		break;
	default:
		break;
	}
	lpfc_els_free_iocb(phba, cmdiocb);
	return;
}

static void
lpfc_cmpl_els_acc(struct lpfc_hba * phba, struct lpfc_iocbq * cmdiocb,
		  struct lpfc_iocbq * rspiocb)
{
	IOCB_t *irsp;
	struct lpfc_nodelist *ndlp;
	LPFC_MBOXQ_t *mbox = NULL;

	irsp = &rspiocb->iocb;

	ndlp = (struct lpfc_nodelist *) cmdiocb->context1;
	if (cmdiocb->context_un.mbox)
		mbox = cmdiocb->context_un.mbox;


	/* Check to see if link went down during discovery */
	if ((lpfc_els_chk_latt(phba)) || !ndlp) {
		if (mbox) {
			mempool_free( mbox, phba->mbox_mem_pool);
		}
		goto out;
	}

	/* ELS response tag <ulpIoTag> completes */
	lpfc_printf_log(phba, KERN_INFO, LOG_ELS,
			"%d:0110 ELS response tag x%x completes "
			"Data: x%x x%x x%x x%x x%x x%x x%x\n",
			phba->brd_no,
			cmdiocb->iocb.ulpIoTag, rspiocb->iocb.ulpStatus,
			rspiocb->iocb.un.ulpWord[4], rspiocb->iocb.ulpTimeout,
 			ndlp->nlp_DID, ndlp->nlp_flag, ndlp->nlp_state,
			ndlp->nlp_rpi);

	if (mbox) {
		if ((rspiocb->iocb.ulpStatus == 0)
		    && (ndlp->nlp_flag & NLP_ACC_REGLOGIN)) {
			lpfc_unreg_rpi(phba, ndlp);
			mbox->mbox_cmpl = lpfc_mbx_cmpl_reg_login;
			mbox->context2 = ndlp;
			ndlp->nlp_prev_state = ndlp->nlp_state;
			ndlp->nlp_state = NLP_STE_REG_LOGIN_ISSUE;
			lpfc_nlp_list(phba, ndlp, NLP_REGLOGIN_LIST);
			if (lpfc_sli_issue_mbox(phba, mbox,
						(MBX_NOWAIT | MBX_STOP_IOCB))
			    != MBX_NOT_FINISHED) {
				goto out;
			}
			/* NOTE: we should have messages for unsuccessful
			   reglogin */
			mempool_free( mbox, phba->mbox_mem_pool);
		} else {
			mempool_free( mbox, phba->mbox_mem_pool);
			/* Do not call NO_LIST for lpfc_els_abort'ed ELS cmds */
			if (!((irsp->ulpStatus == IOSTAT_LOCAL_REJECT) &&
			      ((irsp->un.ulpWord[4] == IOERR_SLI_ABORTED) ||
			       (irsp->un.ulpWord[4] == IOERR_LINK_DOWN) ||
			       (irsp->un.ulpWord[4] == IOERR_SLI_DOWN)))) {
				if (ndlp->nlp_flag & NLP_ACC_REGLOGIN) {
					lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
					ndlp = NULL;
				}
			}
		}
	}
out:
	if (ndlp) {
		spin_lock_irq(phba->host->host_lock);
		ndlp->nlp_flag &= ~NLP_ACC_REGLOGIN;
		spin_unlock_irq(phba->host->host_lock);
	}
	lpfc_els_free_iocb(phba, cmdiocb);
	return;
}

int
lpfc_els_rsp_acc(struct lpfc_hba * phba, uint32_t flag,
		 struct lpfc_iocbq * oldiocb, struct lpfc_nodelist * ndlp,
		 LPFC_MBOXQ_t * mbox, uint8_t newnode)
{
	IOCB_t *icmd;
	IOCB_t *oldcmd;
	struct lpfc_iocbq *elsiocb;
	struct lpfc_sli_ring *pring;
	struct lpfc_sli *psli;
	uint8_t *pcmd;
	uint16_t cmdsize;
	int rc;
	ELS_PKT *els_pkt_ptr;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */
	oldcmd = &oldiocb->iocb;

	switch (flag) {
	case ELS_CMD_ACC:
		cmdsize = sizeof (uint32_t);
		elsiocb = lpfc_prep_els_iocb(phba, 0, cmdsize, oldiocb->retry,
					ndlp, ndlp->nlp_DID, ELS_CMD_ACC);
		if (!elsiocb) {
			ndlp->nlp_flag &= ~NLP_LOGO_ACC;
			return 1;
		}
		icmd = &elsiocb->iocb;
		icmd->ulpContext = oldcmd->ulpContext;	/* Xri */
		pcmd = (((struct lpfc_dmabuf *) elsiocb->context2)->virt);
		*((uint32_t *) (pcmd)) = ELS_CMD_ACC;
		pcmd += sizeof (uint32_t);
		break;
	case ELS_CMD_PLOGI:
		cmdsize = (sizeof (struct serv_parm) + sizeof (uint32_t));
		elsiocb = lpfc_prep_els_iocb(phba, 0, cmdsize, oldiocb->retry,
					ndlp, ndlp->nlp_DID, ELS_CMD_ACC);
		if (!elsiocb)
			return 1;

		icmd = &elsiocb->iocb;
		icmd->ulpContext = oldcmd->ulpContext;	/* Xri */
		pcmd = (((struct lpfc_dmabuf *) elsiocb->context2)->virt);

		if (mbox)
			elsiocb->context_un.mbox = mbox;

		*((uint32_t *) (pcmd)) = ELS_CMD_ACC;
		pcmd += sizeof (uint32_t);
		memcpy(pcmd, &phba->fc_sparam, sizeof (struct serv_parm));
		break;
	case ELS_CMD_PRLO:
		cmdsize = sizeof (uint32_t) + sizeof (PRLO);
		elsiocb = lpfc_prep_els_iocb(phba, 0, cmdsize, oldiocb->retry,
					     ndlp, ndlp->nlp_DID, ELS_CMD_PRLO);
		if (!elsiocb)
			return 1;

		icmd = &elsiocb->iocb;
		icmd->ulpContext = oldcmd->ulpContext; /* Xri */
		pcmd = (((struct lpfc_dmabuf *) elsiocb->context2)->virt);

		memcpy(pcmd, ((struct lpfc_dmabuf *) oldiocb->context2)->virt,
		       sizeof (uint32_t) + sizeof (PRLO));
		*((uint32_t *) (pcmd)) = ELS_CMD_PRLO_ACC;
		els_pkt_ptr = (ELS_PKT *) pcmd;
		els_pkt_ptr->un.prlo.acceptRspCode = PRLO_REQ_EXECUTED;
		break;
	default:
		return 1;
	}

	if (newnode)
		elsiocb->context1 = NULL;

	/* Xmit ELS ACC response tag <ulpIoTag> */
	lpfc_printf_log(phba, KERN_INFO, LOG_ELS,
			"%d:0128 Xmit ELS ACC response tag x%x "
			"Data: x%x x%x x%x x%x x%x\n",
			phba->brd_no,
			elsiocb->iocb.ulpIoTag,
			elsiocb->iocb.ulpContext, ndlp->nlp_DID,
			ndlp->nlp_flag, ndlp->nlp_state, ndlp->nlp_rpi);

	if (ndlp->nlp_flag & NLP_LOGO_ACC) {
		spin_lock_irq(phba->host->host_lock);
		ndlp->nlp_flag &= ~NLP_LOGO_ACC;
		spin_unlock_irq(phba->host->host_lock);
		elsiocb->iocb_cmpl = lpfc_cmpl_els_logo_acc;
	} else {
		elsiocb->iocb_cmpl = lpfc_cmpl_els_acc;
	}

	phba->fc_stat.elsXmitACC++;
	spin_lock_irq(phba->host->host_lock);
	rc = lpfc_sli_issue_iocb(phba, pring, elsiocb, 0);
	spin_unlock_irq(phba->host->host_lock);
	if (rc == IOCB_ERROR) {
		lpfc_els_free_iocb(phba, elsiocb);
		return 1;
	}
	return 0;
}

int
lpfc_els_rsp_reject(struct lpfc_hba * phba, uint32_t rejectError,
		    struct lpfc_iocbq * oldiocb, struct lpfc_nodelist * ndlp)
{
	IOCB_t *icmd;
	IOCB_t *oldcmd;
	struct lpfc_iocbq *elsiocb;
	struct lpfc_sli_ring *pring;
	struct lpfc_sli *psli;
	uint8_t *pcmd;
	uint16_t cmdsize;
	int rc;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */

	cmdsize = 2 * sizeof (uint32_t);
	elsiocb = lpfc_prep_els_iocb(phba, 0, cmdsize, oldiocb->retry,
					ndlp, ndlp->nlp_DID, ELS_CMD_LS_RJT);
	if (!elsiocb)
		return 1;

	icmd = &elsiocb->iocb;
	oldcmd = &oldiocb->iocb;
	icmd->ulpContext = oldcmd->ulpContext;	/* Xri */
	pcmd = (uint8_t *) (((struct lpfc_dmabuf *) elsiocb->context2)->virt);

	*((uint32_t *) (pcmd)) = ELS_CMD_LS_RJT;
	pcmd += sizeof (uint32_t);
	*((uint32_t *) (pcmd)) = rejectError;

	/* Xmit ELS RJT <err> response tag <ulpIoTag> */
	lpfc_printf_log(phba, KERN_INFO, LOG_ELS,
			"%d:0129 Xmit ELS RJT x%x response tag x%x "
			"Data: x%x x%x x%x x%x x%x\n",
			phba->brd_no,
			rejectError, elsiocb->iocb.ulpIoTag,
			elsiocb->iocb.ulpContext, ndlp->nlp_DID,
			ndlp->nlp_flag, ndlp->nlp_state, ndlp->nlp_rpi);

	phba->fc_stat.elsXmitLSRJT++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_acc;
	spin_lock_irq(phba->host->host_lock);
	rc = lpfc_sli_issue_iocb(phba, pring, elsiocb, 0);
	spin_unlock_irq(phba->host->host_lock);
	if (rc == IOCB_ERROR) {
		lpfc_els_free_iocb(phba, elsiocb);
		return 1;
	}
	return 0;
}

int
lpfc_els_rsp_adisc_acc(struct lpfc_hba * phba,
		       struct lpfc_iocbq * oldiocb, struct lpfc_nodelist * ndlp)
{
	ADISC *ap;
	IOCB_t *icmd;
	IOCB_t *oldcmd;
	struct lpfc_iocbq *elsiocb;
	struct lpfc_sli_ring *pring;
	struct lpfc_sli *psli;
	uint8_t *pcmd;
	uint16_t cmdsize;
	int rc;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */

	cmdsize = sizeof (uint32_t) + sizeof (ADISC);
	elsiocb = lpfc_prep_els_iocb(phba, 0, cmdsize, oldiocb->retry,
					ndlp, ndlp->nlp_DID, ELS_CMD_ACC);
	if (!elsiocb)
		return 1;

	/* Xmit ADISC ACC response tag <ulpIoTag> */
	lpfc_printf_log(phba, KERN_INFO, LOG_ELS,
			"%d:0130 Xmit ADISC ACC response tag x%x "
			"Data: x%x x%x x%x x%x x%x\n",
			phba->brd_no,
			elsiocb->iocb.ulpIoTag,
			elsiocb->iocb.ulpContext, ndlp->nlp_DID,
			ndlp->nlp_flag, ndlp->nlp_state, ndlp->nlp_rpi);

	icmd = &elsiocb->iocb;
	oldcmd = &oldiocb->iocb;
	icmd->ulpContext = oldcmd->ulpContext;	/* Xri */
	pcmd = (uint8_t *) (((struct lpfc_dmabuf *) elsiocb->context2)->virt);

	*((uint32_t *) (pcmd)) = ELS_CMD_ACC;
	pcmd += sizeof (uint32_t);

	ap = (ADISC *) (pcmd);
	ap->hardAL_PA = phba->fc_pref_ALPA;
	memcpy(&ap->portName, &phba->fc_portname, sizeof (struct lpfc_name));
	memcpy(&ap->nodeName, &phba->fc_nodename, sizeof (struct lpfc_name));
	ap->DID = be32_to_cpu(phba->fc_myDID);

	phba->fc_stat.elsXmitACC++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_acc;
	spin_lock_irq(phba->host->host_lock);
	rc = lpfc_sli_issue_iocb(phba, pring, elsiocb, 0);
	spin_unlock_irq(phba->host->host_lock);
	if (rc == IOCB_ERROR) {
		lpfc_els_free_iocb(phba, elsiocb);
		return 1;
	}
	return 0;
}

int
lpfc_els_rsp_prli_acc(struct lpfc_hba * phba,
		      struct lpfc_iocbq * oldiocb, struct lpfc_nodelist * ndlp)
{
	PRLI *npr;
	lpfc_vpd_t *vpd;
	IOCB_t *icmd;
	IOCB_t *oldcmd;
	struct lpfc_iocbq *elsiocb;
	struct lpfc_sli_ring *pring;
	struct lpfc_sli *psli;
	uint8_t *pcmd;
	uint16_t cmdsize;
	int rc;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */

	cmdsize = sizeof (uint32_t) + sizeof (PRLI);
	elsiocb = lpfc_prep_els_iocb(phba, 0, cmdsize, oldiocb->retry, ndlp,
		ndlp->nlp_DID, (ELS_CMD_ACC | (ELS_CMD_PRLI & ~ELS_RSP_MASK)));
	if (!elsiocb)
		return 1;

	/* Xmit PRLI ACC response tag <ulpIoTag> */
	lpfc_printf_log(phba, KERN_INFO, LOG_ELS,
			"%d:0131 Xmit PRLI ACC response tag x%x "
			"Data: x%x x%x x%x x%x x%x\n",
			phba->brd_no,
			elsiocb->iocb.ulpIoTag,
			elsiocb->iocb.ulpContext, ndlp->nlp_DID,
			ndlp->nlp_flag, ndlp->nlp_state, ndlp->nlp_rpi);

	icmd = &elsiocb->iocb;
	oldcmd = &oldiocb->iocb;
	icmd->ulpContext = oldcmd->ulpContext;	/* Xri */
	pcmd = (uint8_t *) (((struct lpfc_dmabuf *) elsiocb->context2)->virt);

	*((uint32_t *) (pcmd)) = (ELS_CMD_ACC | (ELS_CMD_PRLI & ~ELS_RSP_MASK));
	pcmd += sizeof (uint32_t);

	/* For PRLI, remainder of payload is PRLI parameter page */
	memset(pcmd, 0, sizeof (PRLI));

	npr = (PRLI *) pcmd;
	vpd = &phba->vpd;
	/*
	 * If our firmware version is 3.20 or later,
	 * set the following bits for FC-TAPE support.
	 */
	if (vpd->rev.feaLevelHigh >= 0x02) {
		npr->ConfmComplAllowed = 1;
		npr->Retry = 1;
		npr->TaskRetryIdReq = 1;
	}

	npr->acceptRspCode = PRLI_REQ_EXECUTED;
	npr->estabImagePair = 1;
	npr->readXferRdyDis = 1;
	npr->ConfmComplAllowed = 1;

	npr->prliType = PRLI_FCP_TYPE;
	npr->initiatorFunc = 1;

	phba->fc_stat.elsXmitACC++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_acc;

	spin_lock_irq(phba->host->host_lock);
	rc = lpfc_sli_issue_iocb(phba, pring, elsiocb, 0);
	spin_unlock_irq(phba->host->host_lock);
	if (rc == IOCB_ERROR) {
		lpfc_els_free_iocb(phba, elsiocb);
		return 1;
	}
	return 0;
}

static int
lpfc_els_rsp_rnid_acc(struct lpfc_hba * phba,
		      uint8_t format,
		      struct lpfc_iocbq * oldiocb, struct lpfc_nodelist * ndlp)
{
	RNID *rn;
	IOCB_t *icmd;
	IOCB_t *oldcmd;
	struct lpfc_iocbq *elsiocb;
	struct lpfc_sli_ring *pring;
	struct lpfc_sli *psli;
	uint8_t *pcmd;
	uint16_t cmdsize;
	int rc;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];

	cmdsize = sizeof (uint32_t) + sizeof (uint32_t)
		+ (2 * sizeof (struct lpfc_name));
	if (format)
		cmdsize += sizeof (RNID_TOP_DISC);

	elsiocb = lpfc_prep_els_iocb(phba, 0, cmdsize, oldiocb->retry,
					ndlp, ndlp->nlp_DID, ELS_CMD_ACC);
	if (!elsiocb)
		return 1;

	/* Xmit RNID ACC response tag <ulpIoTag> */
	lpfc_printf_log(phba, KERN_INFO, LOG_ELS,
			"%d:0132 Xmit RNID ACC response tag x%x "
			"Data: x%x\n",
			phba->brd_no,
			elsiocb->iocb.ulpIoTag,
			elsiocb->iocb.ulpContext);

	icmd = &elsiocb->iocb;
	oldcmd = &oldiocb->iocb;
	icmd->ulpContext = oldcmd->ulpContext;	/* Xri */
	pcmd = (uint8_t *) (((struct lpfc_dmabuf *) elsiocb->context2)->virt);

	*((uint32_t *) (pcmd)) = ELS_CMD_ACC;
	pcmd += sizeof (uint32_t);

	memset(pcmd, 0, sizeof (RNID));
	rn = (RNID *) (pcmd);
	rn->Format = format;
	rn->CommonLen = (2 * sizeof (struct lpfc_name));
	memcpy(&rn->portName, &phba->fc_portname, sizeof (struct lpfc_name));
	memcpy(&rn->nodeName, &phba->fc_nodename, sizeof (struct lpfc_name));
	switch (format) {
	case 0:
		rn->SpecificLen = 0;
		break;
	case RNID_TOPOLOGY_DISC:
		rn->SpecificLen = sizeof (RNID_TOP_DISC);
		memcpy(&rn->un.topologyDisc.portName,
		       &phba->fc_portname, sizeof (struct lpfc_name));
		rn->un.topologyDisc.unitType = RNID_HBA;
		rn->un.topologyDisc.physPort = 0;
		rn->un.topologyDisc.attachedNodes = 0;
		break;
	default:
		rn->CommonLen = 0;
		rn->SpecificLen = 0;
		break;
	}

	phba->fc_stat.elsXmitACC++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_acc;
	elsiocb->context1 = NULL;  /* Don't need ndlp for cmpl,
				    * it could be freed */

	spin_lock_irq(phba->host->host_lock);
	rc = lpfc_sli_issue_iocb(phba, pring, elsiocb, 0);
	spin_unlock_irq(phba->host->host_lock);
	if (rc == IOCB_ERROR) {
		lpfc_els_free_iocb(phba, elsiocb);
		return 1;
	}
	return 0;
}

int
lpfc_els_disc_adisc(struct lpfc_hba * phba)
{
	int sentadisc;
	struct lpfc_nodelist *ndlp, *next_ndlp;

	sentadisc = 0;
	/* go thru NPR list and issue any remaining ELS ADISCs */
	list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_npr_list,
			nlp_listp) {
		if (ndlp->nlp_flag & NLP_NPR_2B_DISC) {
			if (ndlp->nlp_flag & NLP_NPR_ADISC) {
				ndlp->nlp_flag &= ~NLP_NPR_ADISC;
				ndlp->nlp_prev_state = ndlp->nlp_state;
				ndlp->nlp_state = NLP_STE_ADISC_ISSUE;
				lpfc_nlp_list(phba, ndlp,
					NLP_ADISC_LIST);
				lpfc_issue_els_adisc(phba, ndlp, 0);
				sentadisc++;
				phba->num_disc_nodes++;
				if (phba->num_disc_nodes >=
				    phba->cfg_discovery_threads) {
					spin_lock_irq(phba->host->host_lock);
					phba->fc_flag |= FC_NLP_MORE;
					spin_unlock_irq(phba->host->host_lock);
					break;
				}
			}
		}
	}
	if (sentadisc == 0) {
		spin_lock_irq(phba->host->host_lock);
		phba->fc_flag &= ~FC_NLP_MORE;
		spin_unlock_irq(phba->host->host_lock);
	}
	return sentadisc;
}

int
lpfc_els_disc_plogi(struct lpfc_hba * phba)
{
	int sentplogi;
	struct lpfc_nodelist *ndlp, *next_ndlp;

	sentplogi = 0;
	/* go thru NPR list and issue any remaining ELS PLOGIs */
	list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_npr_list,
				nlp_listp) {
		if ((ndlp->nlp_flag & NLP_NPR_2B_DISC) &&
		   (!(ndlp->nlp_flag & NLP_DELAY_TMO))) {
			if (!(ndlp->nlp_flag & NLP_NPR_ADISC)) {
				ndlp->nlp_prev_state = ndlp->nlp_state;
				ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
				lpfc_nlp_list(phba, ndlp, NLP_PLOGI_LIST);
				lpfc_issue_els_plogi(phba, ndlp->nlp_DID, 0);
				sentplogi++;
				phba->num_disc_nodes++;
				if (phba->num_disc_nodes >=
				    phba->cfg_discovery_threads) {
					spin_lock_irq(phba->host->host_lock);
					phba->fc_flag |= FC_NLP_MORE;
					spin_unlock_irq(phba->host->host_lock);
					break;
				}
			}
		}
	}
	if (sentplogi == 0) {
		spin_lock_irq(phba->host->host_lock);
		phba->fc_flag &= ~FC_NLP_MORE;
		spin_unlock_irq(phba->host->host_lock);
	}
	return sentplogi;
}

int
lpfc_els_flush_rscn(struct lpfc_hba * phba)
{
	struct lpfc_dmabuf *mp;
	int i;

	for (i = 0; i < phba->fc_rscn_id_cnt; i++) {
		mp = phba->fc_rscn_id_list[i];
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		kfree(mp);
		phba->fc_rscn_id_list[i] = NULL;
	}
	phba->fc_rscn_id_cnt = 0;
	spin_lock_irq(phba->host->host_lock);
	phba->fc_flag &= ~(FC_RSCN_MODE | FC_RSCN_DISCOVERY);
	spin_unlock_irq(phba->host->host_lock);
	lpfc_can_disctmo(phba);
	return 0;
}

int
lpfc_rscn_payload_check(struct lpfc_hba * phba, uint32_t did)
{
	D_ID ns_did;
	D_ID rscn_did;
	struct lpfc_dmabuf *mp;
	uint32_t *lp;
	uint32_t payload_len, cmd, i, match;

	ns_did.un.word = did;
	match = 0;

	/* Never match fabric nodes for RSCNs */
	if ((did & Fabric_DID_MASK) == Fabric_DID_MASK)
		return(0);

	/* If we are doing a FULL RSCN rediscovery, match everything */
	if (phba->fc_flag & FC_RSCN_DISCOVERY) {
		return did;
	}

	for (i = 0; i < phba->fc_rscn_id_cnt; i++) {
		mp = phba->fc_rscn_id_list[i];
		lp = (uint32_t *) mp->virt;
		cmd = *lp++;
		payload_len = be32_to_cpu(cmd) & 0xffff; /* payload length */
		payload_len -= sizeof (uint32_t);	/* take off word 0 */
		while (payload_len) {
			rscn_did.un.word = *lp++;
			rscn_did.un.word = be32_to_cpu(rscn_did.un.word);
			payload_len -= sizeof (uint32_t);
			switch (rscn_did.un.b.resv) {
			case 0:	/* Single N_Port ID effected */
				if (ns_did.un.word == rscn_did.un.word) {
					match = did;
				}
				break;
			case 1:	/* Whole N_Port Area effected */
				if ((ns_did.un.b.domain == rscn_did.un.b.domain)
				    && (ns_did.un.b.area == rscn_did.un.b.area))
					{
						match = did;
					}
				break;
			case 2:	/* Whole N_Port Domain effected */
				if (ns_did.un.b.domain == rscn_did.un.b.domain)
					{
						match = did;
					}
				break;
			case 3:	/* Whole Fabric effected */
				match = did;
				break;
			default:
				/* Unknown Identifier in RSCN list */
				lpfc_printf_log(phba, KERN_ERR, LOG_DISCOVERY,
						"%d:0217 Unknown Identifier in "
						"RSCN payload Data: x%x\n",
						phba->brd_no, rscn_did.un.word);
				break;
			}
			if (match) {
				break;
			}
		}
	}
	return match;
}

static int
lpfc_rscn_recovery_check(struct lpfc_hba * phba)
{
	struct lpfc_nodelist *ndlp = NULL, *next_ndlp;
	struct list_head *listp;
	struct list_head *node_list[7];
	int i;

	/* Look at all nodes effected by pending RSCNs and move
	 * them to NPR list.
	 */
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
			if (!(lpfc_rscn_payload_check(phba, ndlp->nlp_DID)))
				continue;

			lpfc_disc_state_machine(phba, ndlp, NULL,
					NLP_EVT_DEVICE_RECOVERY);

			/* Make sure NLP_DELAY_TMO is NOT running
			 * after a device recovery event.
			 */
			if (ndlp->nlp_flag & NLP_DELAY_TMO)
				lpfc_cancel_retry_delay_tmo(phba, ndlp);
		}
	}
	return 0;
}

static int
lpfc_els_rcv_rscn(struct lpfc_hba * phba,
		  struct lpfc_iocbq * cmdiocb,
		  struct lpfc_nodelist * ndlp, uint8_t newnode)
{
	struct lpfc_dmabuf *pcmd;
	uint32_t *lp;
	IOCB_t *icmd;
	uint32_t payload_len, cmd;
	int i;

	icmd = &cmdiocb->iocb;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;
	lp = (uint32_t *) pcmd->virt;

	cmd = *lp++;
	payload_len = be32_to_cpu(cmd) & 0xffff;	/* payload length */
	payload_len -= sizeof (uint32_t);	/* take off word 0 */
	cmd &= ELS_CMD_MASK;

	/* RSCN received */
	lpfc_printf_log(phba,
			KERN_INFO,
			LOG_DISCOVERY,
			"%d:0214 RSCN received Data: x%x x%x x%x x%x\n",
			phba->brd_no,
			phba->fc_flag, payload_len, *lp, phba->fc_rscn_id_cnt);

	for (i = 0; i < payload_len/sizeof(uint32_t); i++)
		fc_host_post_event(phba->host, fc_get_event_number(),
			FCH_EVT_RSCN, lp[i]);

	/* If we are about to begin discovery, just ACC the RSCN.
	 * Discovery processing will satisfy it.
	 */
	if (phba->hba_state <= LPFC_NS_QRY) {
		lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, NULL,
								newnode);
		return 0;
	}

	/* If we are already processing an RSCN, save the received
	 * RSCN payload buffer, cmdiocb->context2 to process later.
	 */
	if (phba->fc_flag & (FC_RSCN_MODE | FC_NDISC_ACTIVE)) {
		if ((phba->fc_rscn_id_cnt < FC_MAX_HOLD_RSCN) &&
		    !(phba->fc_flag & FC_RSCN_DISCOVERY)) {
			spin_lock_irq(phba->host->host_lock);
			phba->fc_flag |= FC_RSCN_MODE;
			spin_unlock_irq(phba->host->host_lock);
			phba->fc_rscn_id_list[phba->fc_rscn_id_cnt++] = pcmd;

			/* If we zero, cmdiocb->context2, the calling
			 * routine will not try to free it.
			 */
			cmdiocb->context2 = NULL;

			/* Deferred RSCN */
			lpfc_printf_log(phba, KERN_INFO, LOG_DISCOVERY,
					"%d:0235 Deferred RSCN "
					"Data: x%x x%x x%x\n",
					phba->brd_no, phba->fc_rscn_id_cnt,
					phba->fc_flag, phba->hba_state);
		} else {
			spin_lock_irq(phba->host->host_lock);
			phba->fc_flag |= FC_RSCN_DISCOVERY;
			spin_unlock_irq(phba->host->host_lock);
			/* ReDiscovery RSCN */
			lpfc_printf_log(phba, KERN_INFO, LOG_DISCOVERY,
					"%d:0234 ReDiscovery RSCN "
					"Data: x%x x%x x%x\n",
					phba->brd_no, phba->fc_rscn_id_cnt,
					phba->fc_flag, phba->hba_state);
		}
		/* Send back ACC */
		lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, NULL,
								newnode);

		/* send RECOVERY event for ALL nodes that match RSCN payload */
		lpfc_rscn_recovery_check(phba);
		return 0;
	}

	phba->fc_flag |= FC_RSCN_MODE;
	phba->fc_rscn_id_list[phba->fc_rscn_id_cnt++] = pcmd;
	/*
	 * If we zero, cmdiocb->context2, the calling routine will
	 * not try to free it.
	 */
	cmdiocb->context2 = NULL;

	lpfc_set_disctmo(phba);

	/* Send back ACC */
	lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, NULL, newnode);

	/* send RECOVERY event for ALL nodes that match RSCN payload */
	lpfc_rscn_recovery_check(phba);

	return lpfc_els_handle_rscn(phba);
}

int
lpfc_els_handle_rscn(struct lpfc_hba * phba)
{
	struct lpfc_nodelist *ndlp;

	/* Start timer for RSCN processing */
	lpfc_set_disctmo(phba);

	/* RSCN processed */
	lpfc_printf_log(phba,
			KERN_INFO,
			LOG_DISCOVERY,
			"%d:0215 RSCN processed Data: x%x x%x x%x x%x\n",
			phba->brd_no,
			phba->fc_flag, 0, phba->fc_rscn_id_cnt,
			phba->hba_state);

	/* To process RSCN, first compare RSCN data with NameServer */
	phba->fc_ns_retry = 0;
	ndlp = lpfc_findnode_did(phba, NLP_SEARCH_UNMAPPED, NameServer_DID);
	if (ndlp) {
		/* Good ndlp, issue CT Request to NameServer */
		if (lpfc_ns_cmd(phba, ndlp, SLI_CTNS_GID_FT) == 0) {
			/* Wait for NameServer query cmpl before we can
			   continue */
			return 1;
		}
	} else {
		/* If login to NameServer does not exist, issue one */
		/* Good status, issue PLOGI to NameServer */
		ndlp = lpfc_findnode_did(phba, NLP_SEARCH_ALL, NameServer_DID);
		if (ndlp) {
			/* Wait for NameServer login cmpl before we can
			   continue */
			return 1;
		}
		ndlp = mempool_alloc(phba->nlp_mem_pool, GFP_KERNEL);
		if (!ndlp) {
			lpfc_els_flush_rscn(phba);
			return 0;
		} else {
			lpfc_nlp_init(phba, ndlp, NameServer_DID);
			ndlp->nlp_type |= NLP_FABRIC;
			ndlp->nlp_prev_state = ndlp->nlp_state;
			ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
			lpfc_nlp_list(phba, ndlp, NLP_PLOGI_LIST);
			lpfc_issue_els_plogi(phba, NameServer_DID, 0);
			/* Wait for NameServer login cmpl before we can
			   continue */
			return 1;
		}
	}

	lpfc_els_flush_rscn(phba);
	return 0;
}

static int
lpfc_els_rcv_flogi(struct lpfc_hba * phba,
		   struct lpfc_iocbq * cmdiocb,
		   struct lpfc_nodelist * ndlp, uint8_t newnode)
{
	struct lpfc_dmabuf *pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;
	uint32_t *lp = (uint32_t *) pcmd->virt;
	IOCB_t *icmd = &cmdiocb->iocb;
	struct serv_parm *sp;
	LPFC_MBOXQ_t *mbox;
	struct ls_rjt stat;
	uint32_t cmd, did;
	int rc;

	cmd = *lp++;
	sp = (struct serv_parm *) lp;

	/* FLOGI received */

	lpfc_set_disctmo(phba);

	if (phba->fc_topology == TOPOLOGY_LOOP) {
		/* We should never receive a FLOGI in loop mode, ignore it */
		did = icmd->un.elsreq64.remoteID;

		/* An FLOGI ELS command <elsCmd> was received from DID <did> in
		   Loop Mode */
		lpfc_printf_log(phba, KERN_ERR, LOG_ELS,
				"%d:0113 An FLOGI ELS command x%x was received "
				"from DID x%x in Loop Mode\n",
				phba->brd_no, cmd, did);
		return 1;
	}

	did = Fabric_DID;

	if ((lpfc_check_sparm(phba, ndlp, sp, CLASS3))) {
		/* For a FLOGI we accept, then if our portname is greater
		 * then the remote portname we initiate Nport login.
		 */

		rc = memcmp(&phba->fc_portname, &sp->portName,
			    sizeof (struct lpfc_name));

		if (!rc) {
			if ((mbox = mempool_alloc(phba->mbox_mem_pool,
						  GFP_KERNEL)) == 0) {
				return 1;
			}
			lpfc_linkdown(phba);
			lpfc_init_link(phba, mbox,
				       phba->cfg_topology,
				       phba->cfg_link_speed);
			mbox->mb.un.varInitLnk.lipsr_AL_PA = 0;
			mbox->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
			rc = lpfc_sli_issue_mbox
				(phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB));
			if (rc == MBX_NOT_FINISHED) {
				mempool_free( mbox, phba->mbox_mem_pool);
			}
			return 1;
		} else if (rc > 0) {	/* greater than */
			spin_lock_irq(phba->host->host_lock);
			phba->fc_flag |= FC_PT2PT_PLOGI;
			spin_unlock_irq(phba->host->host_lock);
		}
		phba->fc_flag |= FC_PT2PT;
		phba->fc_flag &= ~(FC_FABRIC | FC_PUBLIC_LOOP);
	} else {
		/* Reject this request because invalid parameters */
		stat.un.b.lsRjtRsvd0 = 0;
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_SPARM_OPTIONS;
		stat.un.b.vendorUnique = 0;
		lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);
		return 1;
	}

	/* Send back ACC */
	lpfc_els_rsp_acc(phba, ELS_CMD_PLOGI, cmdiocb, ndlp, NULL, newnode);

	return 0;
}

static int
lpfc_els_rcv_rnid(struct lpfc_hba * phba,
		  struct lpfc_iocbq * cmdiocb, struct lpfc_nodelist * ndlp)
{
	struct lpfc_dmabuf *pcmd;
	uint32_t *lp;
	IOCB_t *icmd;
	RNID *rn;
	struct ls_rjt stat;
	uint32_t cmd, did;

	icmd = &cmdiocb->iocb;
	did = icmd->un.elsreq64.remoteID;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;
	lp = (uint32_t *) pcmd->virt;

	cmd = *lp++;
	rn = (RNID *) lp;

	/* RNID received */

	switch (rn->Format) {
	case 0:
	case RNID_TOPOLOGY_DISC:
		/* Send back ACC */
		lpfc_els_rsp_rnid_acc(phba, rn->Format, cmdiocb, ndlp);
		break;
	default:
		/* Reject this request because format not supported */
		stat.un.b.lsRjtRsvd0 = 0;
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_CANT_GIVE_DATA;
		stat.un.b.vendorUnique = 0;
		lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);
	}
	return 0;
}

static int
lpfc_els_rcv_lirr(struct lpfc_hba * phba, struct lpfc_iocbq * cmdiocb,
		 struct lpfc_nodelist * ndlp)
{
	struct ls_rjt stat;

	/* For now, unconditionally reject this command */
	stat.un.b.lsRjtRsvd0 = 0;
	stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
	stat.un.b.lsRjtRsnCodeExp = LSEXP_CANT_GIVE_DATA;
	stat.un.b.vendorUnique = 0;
	lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);
	return 0;
}

static void
lpfc_els_rsp_rps_acc(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb)
{
	struct lpfc_sli *psli;
	struct lpfc_sli_ring *pring;
	MAILBOX_t *mb;
	IOCB_t *icmd;
	RPS_RSP *rps_rsp;
	uint8_t *pcmd;
	struct lpfc_iocbq *elsiocb;
	struct lpfc_nodelist *ndlp;
	uint16_t xri, status;
	uint32_t cmdsize;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];
	mb = &pmb->mb;

	ndlp = (struct lpfc_nodelist *) pmb->context2;
	xri = (uint16_t) ((unsigned long)(pmb->context1));
	pmb->context1 = NULL;
	pmb->context2 = NULL;

	if (mb->mbxStatus) {
		mempool_free( pmb, phba->mbox_mem_pool);
		return;
	}

	cmdsize = sizeof(RPS_RSP) + sizeof(uint32_t);
	mempool_free( pmb, phba->mbox_mem_pool);
	elsiocb = lpfc_prep_els_iocb(phba, 0, cmdsize, lpfc_max_els_tries, ndlp,
						ndlp->nlp_DID, ELS_CMD_ACC);
	if (!elsiocb)
		return;

	icmd = &elsiocb->iocb;
	icmd->ulpContext = xri;

	pcmd = (uint8_t *) (((struct lpfc_dmabuf *) elsiocb->context2)->virt);
	*((uint32_t *) (pcmd)) = ELS_CMD_ACC;
	pcmd += sizeof (uint32_t); /* Skip past command */
	rps_rsp = (RPS_RSP *)pcmd;

	if (phba->fc_topology != TOPOLOGY_LOOP)
		status = 0x10;
	else
		status = 0x8;
	if (phba->fc_flag & FC_FABRIC)
		status |= 0x4;

	rps_rsp->rsvd1 = 0;
	rps_rsp->portStatus = be16_to_cpu(status);
	rps_rsp->linkFailureCnt = be32_to_cpu(mb->un.varRdLnk.linkFailureCnt);
	rps_rsp->lossSyncCnt = be32_to_cpu(mb->un.varRdLnk.lossSyncCnt);
	rps_rsp->lossSignalCnt = be32_to_cpu(mb->un.varRdLnk.lossSignalCnt);
	rps_rsp->primSeqErrCnt = be32_to_cpu(mb->un.varRdLnk.primSeqErrCnt);
	rps_rsp->invalidXmitWord = be32_to_cpu(mb->un.varRdLnk.invalidXmitWord);
	rps_rsp->crcCnt = be32_to_cpu(mb->un.varRdLnk.crcCnt);

	/* Xmit ELS RPS ACC response tag <ulpIoTag> */
	lpfc_printf_log(phba, KERN_INFO, LOG_ELS,
			"%d:0118 Xmit ELS RPS ACC response tag x%x "
			"Data: x%x x%x x%x x%x x%x\n",
			phba->brd_no,
			elsiocb->iocb.ulpIoTag,
			elsiocb->iocb.ulpContext, ndlp->nlp_DID,
			ndlp->nlp_flag, ndlp->nlp_state, ndlp->nlp_rpi);

	elsiocb->iocb_cmpl = lpfc_cmpl_els_acc;
	phba->fc_stat.elsXmitACC++;
	if (lpfc_sli_issue_iocb(phba, pring, elsiocb, 0) == IOCB_ERROR) {
		lpfc_els_free_iocb(phba, elsiocb);
	}
	return;
}

static int
lpfc_els_rcv_rps(struct lpfc_hba * phba, struct lpfc_iocbq * cmdiocb,
		 struct lpfc_nodelist * ndlp)
{
	uint32_t *lp;
	uint8_t flag;
	LPFC_MBOXQ_t *mbox;
	struct lpfc_dmabuf *pcmd;
	RPS *rps;
	struct ls_rjt stat;

	if ((ndlp->nlp_state != NLP_STE_UNMAPPED_NODE) &&
	    (ndlp->nlp_state != NLP_STE_MAPPED_NODE)) {
		stat.un.b.lsRjtRsvd0 = 0;
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_CANT_GIVE_DATA;
		stat.un.b.vendorUnique = 0;
		lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);
	}

	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;
	lp = (uint32_t *) pcmd->virt;
	flag = (be32_to_cpu(*lp++) & 0xf);
	rps = (RPS *) lp;

	if ((flag == 0) ||
	    ((flag == 1) && (be32_to_cpu(rps->un.portNum) == 0)) ||
	    ((flag == 2) && (memcmp(&rps->un.portName, &phba->fc_portname,
			   sizeof (struct lpfc_name)) == 0))) {
		if ((mbox = mempool_alloc(phba->mbox_mem_pool, GFP_ATOMIC))) {
			lpfc_read_lnk_stat(phba, mbox);
			mbox->context1 =
			    (void *)((unsigned long)cmdiocb->iocb.ulpContext);
			mbox->context2 = ndlp;
			mbox->mbox_cmpl = lpfc_els_rsp_rps_acc;
			if (lpfc_sli_issue_mbox (phba, mbox,
			    (MBX_NOWAIT | MBX_STOP_IOCB)) != MBX_NOT_FINISHED) {
				/* Mbox completion will send ELS Response */
				return 0;
			}
			mempool_free(mbox, phba->mbox_mem_pool);
		}
	}
	stat.un.b.lsRjtRsvd0 = 0;
	stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
	stat.un.b.lsRjtRsnCodeExp = LSEXP_CANT_GIVE_DATA;
	stat.un.b.vendorUnique = 0;
	lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);
	return 0;
}

static int
lpfc_els_rsp_rpl_acc(struct lpfc_hba * phba, uint16_t cmdsize,
		 struct lpfc_iocbq * oldiocb, struct lpfc_nodelist * ndlp)
{
	IOCB_t *icmd;
	IOCB_t *oldcmd;
	RPL_RSP rpl_rsp;
	struct lpfc_iocbq *elsiocb;
	struct lpfc_sli_ring *pring;
	struct lpfc_sli *psli;
	uint8_t *pcmd;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */

	elsiocb = lpfc_prep_els_iocb(phba, 0, cmdsize, oldiocb->retry,
					ndlp, ndlp->nlp_DID, ELS_CMD_ACC);
	if (!elsiocb)
		return 1;

	icmd = &elsiocb->iocb;
	oldcmd = &oldiocb->iocb;
	icmd->ulpContext = oldcmd->ulpContext;	/* Xri */

	pcmd = (((struct lpfc_dmabuf *) elsiocb->context2)->virt);
	*((uint32_t *) (pcmd)) = ELS_CMD_ACC;
	pcmd += sizeof (uint16_t);
	*((uint16_t *)(pcmd)) = be16_to_cpu(cmdsize);
	pcmd += sizeof(uint16_t);

	/* Setup the RPL ACC payload */
	rpl_rsp.listLen = be32_to_cpu(1);
	rpl_rsp.index = 0;
	rpl_rsp.port_num_blk.portNum = 0;
	rpl_rsp.port_num_blk.portID = be32_to_cpu(phba->fc_myDID);
	memcpy(&rpl_rsp.port_num_blk.portName, &phba->fc_portname,
	    sizeof(struct lpfc_name));

	memcpy(pcmd, &rpl_rsp, cmdsize - sizeof(uint32_t));


	/* Xmit ELS RPL ACC response tag <ulpIoTag> */
	lpfc_printf_log(phba, KERN_INFO, LOG_ELS,
			"%d:0120 Xmit ELS RPL ACC response tag x%x "
			"Data: x%x x%x x%x x%x x%x\n",
			phba->brd_no,
			elsiocb->iocb.ulpIoTag,
			elsiocb->iocb.ulpContext, ndlp->nlp_DID,
			ndlp->nlp_flag, ndlp->nlp_state, ndlp->nlp_rpi);

	elsiocb->iocb_cmpl = lpfc_cmpl_els_acc;

	phba->fc_stat.elsXmitACC++;
	if (lpfc_sli_issue_iocb(phba, pring, elsiocb, 0) == IOCB_ERROR) {
		lpfc_els_free_iocb(phba, elsiocb);
		return 1;
	}
	return 0;
}

static int
lpfc_els_rcv_rpl(struct lpfc_hba * phba, struct lpfc_iocbq * cmdiocb,
		 struct lpfc_nodelist * ndlp)
{
	struct lpfc_dmabuf *pcmd;
	uint32_t *lp;
	uint32_t maxsize;
	uint16_t cmdsize;
	RPL *rpl;
	struct ls_rjt stat;

	if ((ndlp->nlp_state != NLP_STE_UNMAPPED_NODE) &&
	    (ndlp->nlp_state != NLP_STE_MAPPED_NODE)) {
		stat.un.b.lsRjtRsvd0 = 0;
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_CANT_GIVE_DATA;
		stat.un.b.vendorUnique = 0;
		lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);
	}

	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;
	lp = (uint32_t *) pcmd->virt;
	rpl = (RPL *) (lp + 1);

	maxsize = be32_to_cpu(rpl->maxsize);

	/* We support only one port */
	if ((rpl->index == 0) &&
	    ((maxsize == 0) ||
	     ((maxsize * sizeof(uint32_t)) >= sizeof(RPL_RSP)))) {
		cmdsize = sizeof(uint32_t) + sizeof(RPL_RSP);
	} else {
		cmdsize = sizeof(uint32_t) + maxsize * sizeof(uint32_t);
	}
	lpfc_els_rsp_rpl_acc(phba, cmdsize, cmdiocb, ndlp);

	return 0;
}

static int
lpfc_els_rcv_farp(struct lpfc_hba * phba,
		  struct lpfc_iocbq * cmdiocb, struct lpfc_nodelist * ndlp)
{
	struct lpfc_dmabuf *pcmd;
	uint32_t *lp;
	IOCB_t *icmd;
	FARP *fp;
	uint32_t cmd, cnt, did;

	icmd = &cmdiocb->iocb;
	did = icmd->un.elsreq64.remoteID;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;
	lp = (uint32_t *) pcmd->virt;

	cmd = *lp++;
	fp = (FARP *) lp;

	/* FARP-REQ received from DID <did> */
	lpfc_printf_log(phba,
			 KERN_INFO,
			 LOG_ELS,
			 "%d:0601 FARP-REQ received from DID x%x\n",
			 phba->brd_no, did);

	/* We will only support match on WWPN or WWNN */
	if (fp->Mflags & ~(FARP_MATCH_NODE | FARP_MATCH_PORT)) {
		return 0;
	}

	cnt = 0;
	/* If this FARP command is searching for my portname */
	if (fp->Mflags & FARP_MATCH_PORT) {
		if (memcmp(&fp->RportName, &phba->fc_portname,
			   sizeof (struct lpfc_name)) == 0)
			cnt = 1;
	}

	/* If this FARP command is searching for my nodename */
	if (fp->Mflags & FARP_MATCH_NODE) {
		if (memcmp(&fp->RnodeName, &phba->fc_nodename,
			   sizeof (struct lpfc_name)) == 0)
			cnt = 1;
	}

	if (cnt) {
		if ((ndlp->nlp_state == NLP_STE_UNMAPPED_NODE) ||
		   (ndlp->nlp_state == NLP_STE_MAPPED_NODE)) {
			/* Log back into the node before sending the FARP. */
			if (fp->Rflags & FARP_REQUEST_PLOGI) {
				ndlp->nlp_prev_state = ndlp->nlp_state;
				ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
				lpfc_nlp_list(phba, ndlp, NLP_PLOGI_LIST);
				lpfc_issue_els_plogi(phba, ndlp->nlp_DID, 0);
			}

			/* Send a FARP response to that node */
			if (fp->Rflags & FARP_REQUEST_FARPR) {
				lpfc_issue_els_farpr(phba, did, 0);
			}
		}
	}
	return 0;
}

static int
lpfc_els_rcv_farpr(struct lpfc_hba * phba,
		   struct lpfc_iocbq * cmdiocb, struct lpfc_nodelist * ndlp)
{
	struct lpfc_dmabuf *pcmd;
	uint32_t *lp;
	IOCB_t *icmd;
	uint32_t cmd, did;

	icmd = &cmdiocb->iocb;
	did = icmd->un.elsreq64.remoteID;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;
	lp = (uint32_t *) pcmd->virt;

	cmd = *lp++;
	/* FARP-RSP received from DID <did> */
	lpfc_printf_log(phba,
			 KERN_INFO,
			 LOG_ELS,
			 "%d:0600 FARP-RSP received from DID x%x\n",
			 phba->brd_no, did);

	/* ACCEPT the Farp resp request */
	lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, NULL, 0);

	return 0;
}

static int
lpfc_els_rcv_fan(struct lpfc_hba * phba, struct lpfc_iocbq * cmdiocb,
		 struct lpfc_nodelist * fan_ndlp)
{
	struct lpfc_dmabuf *pcmd;
	uint32_t *lp;
	IOCB_t *icmd;
	uint32_t cmd, did;
	FAN *fp;
	struct lpfc_nodelist *ndlp, *next_ndlp;

	/* FAN received */
	lpfc_printf_log(phba, KERN_INFO, LOG_ELS, "%d:0265 FAN received\n",
								phba->brd_no);

	icmd = &cmdiocb->iocb;
	did = icmd->un.elsreq64.remoteID;
	pcmd = (struct lpfc_dmabuf *)cmdiocb->context2;
	lp = (uint32_t *)pcmd->virt;

	cmd = *lp++;
	fp = (FAN *)lp;

	/* FAN received; Fan does not have a reply sequence */

	if (phba->hba_state == LPFC_LOCAL_CFG_LINK) {
		if ((memcmp(&phba->fc_fabparam.nodeName, &fp->FnodeName,
			sizeof(struct lpfc_name)) != 0) ||
		    (memcmp(&phba->fc_fabparam.portName, &fp->FportName,
			sizeof(struct lpfc_name)) != 0)) {
			/*
			 * This node has switched fabrics.  FLOGI is required
			 * Clean up the old rpi's
			 */

			list_for_each_entry_safe(ndlp, next_ndlp,
				&phba->fc_npr_list, nlp_listp) {

				if (ndlp->nlp_type & NLP_FABRIC) {
					/*
					 * Clean up old Fabric, Nameserver and
					 * other NLP_FABRIC logins
					 */
					lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
				} else if (!(ndlp->nlp_flag & NLP_NPR_ADISC)) {
					/* Fail outstanding I/O now since this
					 * device is marked for PLOGI
					 */
					lpfc_unreg_rpi(phba, ndlp);
				}
			}

			phba->hba_state = LPFC_FLOGI;
			lpfc_set_disctmo(phba);
			lpfc_initial_flogi(phba);
			return 0;
		}
		/* Discovery not needed,
		 * move the nodes to their original state.
		 */
		list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_npr_list,
			nlp_listp) {

			switch (ndlp->nlp_prev_state) {
			case NLP_STE_UNMAPPED_NODE:
				ndlp->nlp_prev_state = NLP_STE_NPR_NODE;
				ndlp->nlp_state = NLP_STE_UNMAPPED_NODE;
				lpfc_nlp_list(phba, ndlp, NLP_UNMAPPED_LIST);
				break;

			case NLP_STE_MAPPED_NODE:
				ndlp->nlp_prev_state = NLP_STE_NPR_NODE;
				ndlp->nlp_state = NLP_STE_MAPPED_NODE;
				lpfc_nlp_list(phba, ndlp, NLP_MAPPED_LIST);
				break;

			default:
				break;
			}
		}

		/* Start discovery - this should just do CLEAR_LA */
		lpfc_disc_start(phba);
	}
	return 0;
}

void
lpfc_els_timeout(unsigned long ptr)
{
	struct lpfc_hba *phba;
	unsigned long iflag;

	phba = (struct lpfc_hba *)ptr;
	if (phba == 0)
		return;
	spin_lock_irqsave(phba->host->host_lock, iflag);
	if (!(phba->work_hba_events & WORKER_ELS_TMO)) {
		phba->work_hba_events |= WORKER_ELS_TMO;
		if (phba->work_wait)
			wake_up(phba->work_wait);
	}
	spin_unlock_irqrestore(phba->host->host_lock, iflag);
	return;
}

void
lpfc_els_timeout_handler(struct lpfc_hba *phba)
{
	struct lpfc_sli_ring *pring;
	struct lpfc_iocbq *tmp_iocb, *piocb;
	IOCB_t *cmd = NULL;
	struct lpfc_dmabuf *pcmd;
	struct list_head *dlp;
	uint32_t *elscmd;
	uint32_t els_command;
	uint32_t timeout;
	uint32_t remote_ID;

	if (phba == 0)
		return;
	spin_lock_irq(phba->host->host_lock);
	/* If the timer is already canceled do nothing */
	if (!(phba->work_hba_events & WORKER_ELS_TMO)) {
		spin_unlock_irq(phba->host->host_lock);
		return;
	}
	timeout = (uint32_t)(phba->fc_ratov << 1);

	pring = &phba->sli.ring[LPFC_ELS_RING];
	dlp = &pring->txcmplq;

	list_for_each_entry_safe(piocb, tmp_iocb, &pring->txcmplq, list) {
		cmd = &piocb->iocb;

		if (piocb->iocb_flag & LPFC_IO_LIBDFC) {
			continue;
		}
		pcmd = (struct lpfc_dmabuf *) piocb->context2;
		elscmd = (uint32_t *) (pcmd->virt);
		els_command = *elscmd;

		if ((els_command == ELS_CMD_FARP)
		    || (els_command == ELS_CMD_FARPR)) {
			continue;
		}

		if (piocb->drvrTimeout > 0) {
			if (piocb->drvrTimeout >= timeout) {
				piocb->drvrTimeout -= timeout;
			} else {
				piocb->drvrTimeout = 0;
			}
			continue;
		}

		list_del(&piocb->list);
		pring->txcmplq_cnt--;

		if (cmd->ulpCommand == CMD_GEN_REQUEST64_CR) {
			struct lpfc_nodelist *ndlp;
			spin_unlock_irq(phba->host->host_lock);
			ndlp = lpfc_findnode_rpi(phba, cmd->ulpContext);
			spin_lock_irq(phba->host->host_lock);
			remote_ID = ndlp->nlp_DID;
			if (cmd->un.elsreq64.bdl.ulpIoTag32) {
				lpfc_sli_issue_abort_iotag32(phba,
					pring, piocb);
			}
		} else {
			remote_ID = cmd->un.elsreq64.remoteID;
		}

		lpfc_printf_log(phba,
				KERN_ERR,
				LOG_ELS,
				"%d:0127 ELS timeout Data: x%x x%x x%x x%x\n",
				phba->brd_no, els_command,
				remote_ID, cmd->ulpCommand, cmd->ulpIoTag);

		/*
		 * The iocb has timed out; abort it.
		 */
		if (piocb->iocb_cmpl) {
			cmd->ulpStatus = IOSTAT_LOCAL_REJECT;
			cmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
			spin_unlock_irq(phba->host->host_lock);
			(piocb->iocb_cmpl) (phba, piocb, piocb);
			spin_lock_irq(phba->host->host_lock);
		} else
			lpfc_sli_release_iocbq(phba, piocb);
	}
	if (phba->sli.ring[LPFC_ELS_RING].txcmplq_cnt)
		mod_timer(&phba->els_tmofunc, jiffies + HZ * timeout);

	spin_unlock_irq(phba->host->host_lock);
}

void
lpfc_els_flush_cmd(struct lpfc_hba * phba)
{
	struct lpfc_sli_ring *pring;
	struct lpfc_iocbq *tmp_iocb, *piocb;
	IOCB_t *cmd = NULL;
	struct lpfc_dmabuf *pcmd;
	uint32_t *elscmd;
	uint32_t els_command;

	pring = &phba->sli.ring[LPFC_ELS_RING];
	spin_lock_irq(phba->host->host_lock);
	list_for_each_entry_safe(piocb, tmp_iocb, &pring->txq, list) {
		cmd = &piocb->iocb;

		if (piocb->iocb_flag & LPFC_IO_LIBDFC) {
			continue;
		}

		/* Do not flush out the QUE_RING and ABORT/CLOSE iocbs */
		if ((cmd->ulpCommand == CMD_QUE_RING_BUF_CN) ||
		    (cmd->ulpCommand == CMD_QUE_RING_BUF64_CN) ||
		    (cmd->ulpCommand == CMD_CLOSE_XRI_CN) ||
		    (cmd->ulpCommand == CMD_ABORT_XRI_CN)) {
			continue;
		}

		pcmd = (struct lpfc_dmabuf *) piocb->context2;
		elscmd = (uint32_t *) (pcmd->virt);
		els_command = *elscmd;

		list_del(&piocb->list);
		pring->txcmplq_cnt--;

		cmd->ulpStatus = IOSTAT_LOCAL_REJECT;
		cmd->un.ulpWord[4] = IOERR_SLI_ABORTED;

		if (piocb->iocb_cmpl) {
			spin_unlock_irq(phba->host->host_lock);
			(piocb->iocb_cmpl) (phba, piocb, piocb);
			spin_lock_irq(phba->host->host_lock);
		} else
			lpfc_sli_release_iocbq(phba, piocb);
	}

	list_for_each_entry_safe(piocb, tmp_iocb, &pring->txcmplq, list) {
		cmd = &piocb->iocb;

		if (piocb->iocb_flag & LPFC_IO_LIBDFC) {
			continue;
		}
		pcmd = (struct lpfc_dmabuf *) piocb->context2;
		elscmd = (uint32_t *) (pcmd->virt);
		els_command = *elscmd;

		list_del(&piocb->list);
		pring->txcmplq_cnt--;

		cmd->ulpStatus = IOSTAT_LOCAL_REJECT;
		cmd->un.ulpWord[4] = IOERR_SLI_ABORTED;

		if (piocb->iocb_cmpl) {
			spin_unlock_irq(phba->host->host_lock);
			(piocb->iocb_cmpl) (phba, piocb, piocb);
			spin_lock_irq(phba->host->host_lock);
		} else
			lpfc_sli_release_iocbq(phba, piocb);
	}
	spin_unlock_irq(phba->host->host_lock);
	return;
}

void
lpfc_els_unsol_event(struct lpfc_hba * phba,
		     struct lpfc_sli_ring * pring, struct lpfc_iocbq * elsiocb)
{
	struct lpfc_sli *psli;
	struct lpfc_nodelist *ndlp;
	struct lpfc_dmabuf *mp;
	uint32_t *lp;
	IOCB_t *icmd;
	struct ls_rjt stat;
	uint32_t cmd;
	uint32_t did;
	uint32_t newnode;
	uint32_t drop_cmd = 0;	/* by default do NOT drop received cmd */
	uint32_t rjt_err = 0;

	psli = &phba->sli;
	icmd = &elsiocb->iocb;

	if ((icmd->ulpStatus == IOSTAT_LOCAL_REJECT) &&
		((icmd->un.ulpWord[4] & 0xff) == IOERR_RCV_BUFFER_WAITING)) {
		/* Not enough posted buffers; Try posting more buffers */
		phba->fc_stat.NoRcvBuf++;
		lpfc_post_buffer(phba, pring, 0, 1);
		return;
	}

	/* If there are no BDEs associated with this IOCB,
	 * there is nothing to do.
	 */
	if (icmd->ulpBdeCount == 0)
		return;

	/* type of ELS cmd is first 32bit word in packet */
	mp = lpfc_sli_ringpostbuf_get(phba, pring, getPaddr(icmd->un.
							    cont64[0].
							    addrHigh,
							    icmd->un.
							    cont64[0].addrLow));
	if (mp == 0) {
		drop_cmd = 1;
		goto dropit;
	}

	newnode = 0;
	lp = (uint32_t *) mp->virt;
	cmd = *lp++;
	lpfc_post_buffer(phba, &psli->ring[LPFC_ELS_RING], 1, 1);

	if (icmd->ulpStatus) {
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		kfree(mp);
		drop_cmd = 1;
		goto dropit;
	}

	/* Check to see if link went down during discovery */
	if (lpfc_els_chk_latt(phba)) {
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		kfree(mp);
		drop_cmd = 1;
		goto dropit;
	}

	did = icmd->un.rcvels.remoteID;
	ndlp = lpfc_findnode_did(phba, NLP_SEARCH_ALL, did);
	if (!ndlp) {
		/* Cannot find existing Fabric ndlp, so allocate a new one */
		ndlp = mempool_alloc(phba->nlp_mem_pool, GFP_KERNEL);
		if (!ndlp) {
			lpfc_mbuf_free(phba, mp->virt, mp->phys);
			kfree(mp);
			drop_cmd = 1;
			goto dropit;
		}

		lpfc_nlp_init(phba, ndlp, did);
		newnode = 1;
		if ((did & Fabric_DID_MASK) == Fabric_DID_MASK) {
			ndlp->nlp_type |= NLP_FABRIC;
		}
		ndlp->nlp_state = NLP_STE_UNUSED_NODE;
		lpfc_nlp_list(phba, ndlp, NLP_UNUSED_LIST);
	}

	phba->fc_stat.elsRcvFrame++;
	elsiocb->context1 = ndlp;
	elsiocb->context2 = mp;

	if ((cmd & ELS_CMD_MASK) == ELS_CMD_RSCN) {
		cmd &= ELS_CMD_MASK;
	}
	/* ELS command <elsCmd> received from NPORT <did> */
	lpfc_printf_log(phba, KERN_INFO, LOG_ELS,
			"%d:0112 ELS command x%x received from NPORT x%x "
			"Data: x%x\n", phba->brd_no, cmd, did, phba->hba_state);

	switch (cmd) {
	case ELS_CMD_PLOGI:
		phba->fc_stat.elsRcvPLOGI++;
		if (phba->hba_state < LPFC_DISC_AUTH) {
			rjt_err = 1;
			break;
		}
		ndlp = lpfc_plogi_confirm_nport(phba, mp, ndlp);
		lpfc_disc_state_machine(phba, ndlp, elsiocb, NLP_EVT_RCV_PLOGI);
		break;
	case ELS_CMD_FLOGI:
		phba->fc_stat.elsRcvFLOGI++;
		lpfc_els_rcv_flogi(phba, elsiocb, ndlp, newnode);
		if (newnode) {
			lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
		}
		break;
	case ELS_CMD_LOGO:
		phba->fc_stat.elsRcvLOGO++;
		if (phba->hba_state < LPFC_DISC_AUTH) {
			rjt_err = 1;
			break;
		}
		lpfc_disc_state_machine(phba, ndlp, elsiocb, NLP_EVT_RCV_LOGO);
		break;
	case ELS_CMD_PRLO:
		phba->fc_stat.elsRcvPRLO++;
		if (phba->hba_state < LPFC_DISC_AUTH) {
			rjt_err = 1;
			break;
		}
		lpfc_disc_state_machine(phba, ndlp, elsiocb, NLP_EVT_RCV_PRLO);
		break;
	case ELS_CMD_RSCN:
		phba->fc_stat.elsRcvRSCN++;
		lpfc_els_rcv_rscn(phba, elsiocb, ndlp, newnode);
		if (newnode) {
			lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
		}
		break;
	case ELS_CMD_ADISC:
		phba->fc_stat.elsRcvADISC++;
		if (phba->hba_state < LPFC_DISC_AUTH) {
			rjt_err = 1;
			break;
		}
		lpfc_disc_state_machine(phba, ndlp, elsiocb, NLP_EVT_RCV_ADISC);
		break;
	case ELS_CMD_PDISC:
		phba->fc_stat.elsRcvPDISC++;
		if (phba->hba_state < LPFC_DISC_AUTH) {
			rjt_err = 1;
			break;
		}
		lpfc_disc_state_machine(phba, ndlp, elsiocb, NLP_EVT_RCV_PDISC);
		break;
	case ELS_CMD_FARPR:
		phba->fc_stat.elsRcvFARPR++;
		lpfc_els_rcv_farpr(phba, elsiocb, ndlp);
		break;
	case ELS_CMD_FARP:
		phba->fc_stat.elsRcvFARP++;
		lpfc_els_rcv_farp(phba, elsiocb, ndlp);
		break;
	case ELS_CMD_FAN:
		phba->fc_stat.elsRcvFAN++;
		lpfc_els_rcv_fan(phba, elsiocb, ndlp);
		break;
	case ELS_CMD_PRLI:
		phba->fc_stat.elsRcvPRLI++;
		if (phba->hba_state < LPFC_DISC_AUTH) {
			rjt_err = 1;
			break;
		}
		lpfc_disc_state_machine(phba, ndlp, elsiocb, NLP_EVT_RCV_PRLI);
		break;
	case ELS_CMD_LIRR:
		phba->fc_stat.elsRcvLIRR++;
		lpfc_els_rcv_lirr(phba, elsiocb, ndlp);
		if (newnode) {
			lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
		}
		break;
	case ELS_CMD_RPS:
		phba->fc_stat.elsRcvRPS++;
		lpfc_els_rcv_rps(phba, elsiocb, ndlp);
		if (newnode) {
			lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
		}
		break;
	case ELS_CMD_RPL:
		phba->fc_stat.elsRcvRPL++;
		lpfc_els_rcv_rpl(phba, elsiocb, ndlp);
		if (newnode) {
			lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
		}
		break;
	case ELS_CMD_RNID:
		phba->fc_stat.elsRcvRNID++;
		lpfc_els_rcv_rnid(phba, elsiocb, ndlp);
		if (newnode) {
			lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
		}
		break;
	default:
		/* Unsupported ELS command, reject */
		rjt_err = 1;

		/* Unknown ELS command <elsCmd> received from NPORT <did> */
		lpfc_printf_log(phba, KERN_ERR, LOG_ELS,
				"%d:0115 Unknown ELS command x%x received from "
				"NPORT x%x\n", phba->brd_no, cmd, did);
		if (newnode) {
			lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
		}
		break;
	}

	/* check if need to LS_RJT received ELS cmd */
	if (rjt_err) {
		stat.un.b.lsRjtRsvd0 = 0;
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_NOTHING_MORE;
		stat.un.b.vendorUnique = 0;
		lpfc_els_rsp_reject(phba, stat.un.lsRjtError, elsiocb, ndlp);
	}

	if (elsiocb->context2) {
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		kfree(mp);
	}
dropit:
	/* check if need to drop received ELS cmd */
	if (drop_cmd == 1) {
		lpfc_printf_log(phba, KERN_ERR, LOG_ELS,
				"%d:0111 Dropping received ELS cmd "
				"Data: x%x x%x x%x\n", phba->brd_no,
				icmd->ulpStatus, icmd->un.ulpWord[4],
				icmd->ulpTimeout);
		phba->fc_stat.elsRcvDrop++;
	}
	return;
}
