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
/* See Fibre Channel protocol T11 FC-LS for details */
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
#include "lpfc_vport.h"
#include "lpfc_debugfs.h"

static int lpfc_els_retry(struct lpfc_hba *, struct lpfc_iocbq *,
			  struct lpfc_iocbq *);
static void lpfc_cmpl_fabric_iocb(struct lpfc_hba *, struct lpfc_iocbq *,
			struct lpfc_iocbq *);
static void lpfc_fabric_abort_vport(struct lpfc_vport *vport);
static int lpfc_issue_els_fdisc(struct lpfc_vport *vport,
				struct lpfc_nodelist *ndlp, uint8_t retry);
static int lpfc_issue_fabric_iocb(struct lpfc_hba *phba,
				  struct lpfc_iocbq *iocb);
static void lpfc_register_new_vport(struct lpfc_hba *phba,
				    struct lpfc_vport *vport,
				    struct lpfc_nodelist *ndlp);

static int lpfc_max_els_tries = 3;

int
lpfc_els_chk_latt(struct lpfc_vport *vport)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct lpfc_hba  *phba = vport->phba;
	uint32_t ha_copy;

	if (vport->port_state >= LPFC_VPORT_READY ||
	    phba->link_state == LPFC_LINK_DOWN)
		return 0;

	/* Read the HBA Host Attention Register */
	ha_copy = readl(phba->HAregaddr);

	if (!(ha_copy & HA_LATT))
		return 0;

	/* Pending Link Event during Discovery */
	lpfc_printf_vlog(vport, KERN_ERR, LOG_DISCOVERY,
			 "0237 Pending Link Event during "
			 "Discovery: State x%x\n",
			 phba->pport->port_state);

	/* CLEAR_LA should re-enable link attention events and
	 * we should then imediately take a LATT event. The
	 * LATT processing should call lpfc_linkdown() which
	 * will cleanup any left over in-progress discovery
	 * events.
	 */
	spin_lock_irq(shost->host_lock);
	vport->fc_flag |= FC_ABORT_DISCOVERY;
	spin_unlock_irq(shost->host_lock);

	if (phba->link_state != LPFC_CLEAR_LA)
		lpfc_issue_clear_la(phba, vport);

	return 1;
}

static struct lpfc_iocbq *
lpfc_prep_els_iocb(struct lpfc_vport *vport, uint8_t expectRsp,
		   uint16_t cmdSize, uint8_t retry,
		   struct lpfc_nodelist *ndlp, uint32_t did,
		   uint32_t elscmd)
{
	struct lpfc_hba  *phba = vport->phba;
	struct lpfc_iocbq *elsiocb;
	struct lpfc_dmabuf *pcmd, *prsp, *pbuflist;
	struct ulp_bde64 *bpl;
	IOCB_t *icmd;


	if (!lpfc_is_link_up(phba))
		return NULL;

	/* Allocate buffer for  command iocb */
	elsiocb = lpfc_sli_get_iocbq(phba);

	if (elsiocb == NULL)
		return NULL;
	icmd = &elsiocb->iocb;

	/* fill in BDEs for command */
	/* Allocate buffer for command payload */
	pcmd = kmalloc(sizeof(struct lpfc_dmabuf), GFP_KERNEL);
	if (pcmd)
		pcmd->virt = lpfc_mbuf_alloc(phba, MEM_PRI, &pcmd->phys);
	if (!pcmd || !pcmd->virt)
		goto els_iocb_free_pcmb_exit;

	INIT_LIST_HEAD(&pcmd->list);

	/* Allocate buffer for response payload */
	if (expectRsp) {
		prsp = kmalloc(sizeof(struct lpfc_dmabuf), GFP_KERNEL);
		if (prsp)
			prsp->virt = lpfc_mbuf_alloc(phba, MEM_PRI,
						     &prsp->phys);
		if (!prsp || !prsp->virt)
			goto els_iocb_free_prsp_exit;
		INIT_LIST_HEAD(&prsp->list);
	} else {
		prsp = NULL;
	}

	/* Allocate buffer for Buffer ptr list */
	pbuflist = kmalloc(sizeof(struct lpfc_dmabuf), GFP_KERNEL);
	if (pbuflist)
		pbuflist->virt = lpfc_mbuf_alloc(phba, MEM_PRI,
						 &pbuflist->phys);
	if (!pbuflist || !pbuflist->virt)
		goto els_iocb_free_pbuf_exit;

	INIT_LIST_HEAD(&pbuflist->list);

	icmd->un.elsreq64.bdl.addrHigh = putPaddrHigh(pbuflist->phys);
	icmd->un.elsreq64.bdl.addrLow = putPaddrLow(pbuflist->phys);
	icmd->un.elsreq64.bdl.bdeFlags = BUFF_TYPE_BDL;
	icmd->un.elsreq64.remoteID = did;	/* DID */
	if (expectRsp) {
		icmd->un.elsreq64.bdl.bdeSize = (2 * sizeof(struct ulp_bde64));
		icmd->ulpCommand = CMD_ELS_REQUEST64_CR;
		icmd->ulpTimeout = phba->fc_ratov * 2;
	} else {
		icmd->un.elsreq64.bdl.bdeSize = sizeof(struct ulp_bde64);
		icmd->ulpCommand = CMD_XMIT_ELS_RSP64_CX;
	}
	icmd->ulpBdeCount = 1;
	icmd->ulpLe = 1;
	icmd->ulpClass = CLASS3;

	if (phba->sli3_options & LPFC_SLI3_NPIV_ENABLED) {
		icmd->un.elsreq64.myID = vport->fc_myDID;

		/* For ELS_REQUEST64_CR, use the VPI by default */
		icmd->ulpContext = vport->vpi;
		icmd->ulpCt_h = 0;
		icmd->ulpCt_l = 1;
	}

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

	/* prevent preparing iocb with NULL ndlp reference */
	elsiocb->context1 = lpfc_nlp_get(ndlp);
	if (!elsiocb->context1)
		goto els_iocb_free_pbuf_exit;
	elsiocb->context2 = pcmd;
	elsiocb->context3 = pbuflist;
	elsiocb->retry = retry;
	elsiocb->vport = vport;
	elsiocb->drvrTimeout = (phba->fc_ratov << 1) + LPFC_DRVR_TIMEOUT;

	if (prsp) {
		list_add(&prsp->list, &pcmd->list);
	}
	if (expectRsp) {
		/* Xmit ELS command <elsCmd> to remote NPORT <did> */
		lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS,
				 "0116 Xmit ELS command x%x to remote "
				 "NPORT x%x I/O tag: x%x, port state: x%x\n",
				 elscmd, did, elsiocb->iotag,
				 vport->port_state);
	} else {
		/* Xmit ELS response <elsCmd> to remote NPORT <did> */
		lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS,
				 "0117 Xmit ELS response x%x to remote "
				 "NPORT x%x I/O tag: x%x, size: x%x\n",
				 elscmd, ndlp->nlp_DID, elsiocb->iotag,
				 cmdSize);
	}
	return elsiocb;

els_iocb_free_pbuf_exit:
	lpfc_mbuf_free(phba, prsp->virt, prsp->phys);
	kfree(pbuflist);

els_iocb_free_prsp_exit:
	lpfc_mbuf_free(phba, pcmd->virt, pcmd->phys);
	kfree(prsp);

els_iocb_free_pcmb_exit:
	kfree(pcmd);
	lpfc_sli_release_iocbq(phba, elsiocb);
	return NULL;
}

static int
lpfc_issue_fabric_reglogin(struct lpfc_vport *vport)
{
	struct lpfc_hba  *phba = vport->phba;
	LPFC_MBOXQ_t *mbox;
	struct lpfc_dmabuf *mp;
	struct lpfc_nodelist *ndlp;
	struct serv_parm *sp;
	int rc;
	int err = 0;

	sp = &phba->fc_fabparam;
	ndlp = lpfc_findnode_did(vport, Fabric_DID);
	if (!ndlp) {
		err = 1;
		goto fail;
	}

	mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!mbox) {
		err = 2;
		goto fail;
	}

	vport->port_state = LPFC_FABRIC_CFG_LINK;
	lpfc_config_link(phba, mbox);
	mbox->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
	mbox->vport = vport;

	rc = lpfc_sli_issue_mbox(phba, mbox, MBX_NOWAIT);
	if (rc == MBX_NOT_FINISHED) {
		err = 3;
		goto fail_free_mbox;
	}

	mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!mbox) {
		err = 4;
		goto fail;
	}
	rc = lpfc_reg_login(phba, vport->vpi, Fabric_DID, (uint8_t *)sp, mbox,
			    0);
	if (rc) {
		err = 5;
		goto fail_free_mbox;
	}

	mbox->mbox_cmpl = lpfc_mbx_cmpl_fabric_reg_login;
	mbox->vport = vport;
	mbox->context2 = lpfc_nlp_get(ndlp);

	rc = lpfc_sli_issue_mbox(phba, mbox, MBX_NOWAIT);
	if (rc == MBX_NOT_FINISHED) {
		err = 6;
		goto fail_issue_reg_login;
	}

	return 0;

fail_issue_reg_login:
	lpfc_nlp_put(ndlp);
	mp = (struct lpfc_dmabuf *) mbox->context1;
	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
fail_free_mbox:
	mempool_free(mbox, phba->mbox_mem_pool);

fail:
	lpfc_vport_set_state(vport, FC_VPORT_FAILED);
	lpfc_printf_vlog(vport, KERN_ERR, LOG_ELS,
		"0249 Cannot issue Register Fabric login: Err %d\n", err);
	return -ENXIO;
}

static int
lpfc_cmpl_els_flogi_fabric(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			   struct serv_parm *sp, IOCB_t *irsp)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct lpfc_hba  *phba = vport->phba;
	struct lpfc_nodelist *np;
	struct lpfc_nodelist *next_np;

	spin_lock_irq(shost->host_lock);
	vport->fc_flag |= FC_FABRIC;
	spin_unlock_irq(shost->host_lock);

	phba->fc_edtov = be32_to_cpu(sp->cmn.e_d_tov);
	if (sp->cmn.edtovResolution)	/* E_D_TOV ticks are in nanoseconds */
		phba->fc_edtov = (phba->fc_edtov + 999999) / 1000000;

	phba->fc_ratov = (be32_to_cpu(sp->cmn.w2.r_a_tov) + 999) / 1000;

	if (phba->fc_topology == TOPOLOGY_LOOP) {
		spin_lock_irq(shost->host_lock);
		vport->fc_flag |= FC_PUBLIC_LOOP;
		spin_unlock_irq(shost->host_lock);
	} else {
		/*
		 * If we are a N-port connected to a Fabric, fixup sparam's so
		 * logins to devices on remote loops work.
		 */
		vport->fc_sparam.cmn.altBbCredit = 1;
	}

	vport->fc_myDID = irsp->un.ulpWord[4] & Mask_DID;
	memcpy(&ndlp->nlp_portname, &sp->portName, sizeof(struct lpfc_name));
	memcpy(&ndlp->nlp_nodename, &sp->nodeName, sizeof(struct lpfc_name));
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

	if (phba->sli3_options & LPFC_SLI3_NPIV_ENABLED) {
		if (sp->cmn.response_multiple_NPort) {
			lpfc_printf_vlog(vport, KERN_WARNING,
					 LOG_ELS | LOG_VPORT,
					 "1816 FLOGI NPIV supported, "
					 "response data 0x%x\n",
					 sp->cmn.response_multiple_NPort);
			phba->link_flag |= LS_NPIV_FAB_SUPPORTED;
		} else {
			/* Because we asked f/w for NPIV it still expects us
			to call reg_vnpid atleast for the physcial host */
			lpfc_printf_vlog(vport, KERN_WARNING,
					 LOG_ELS | LOG_VPORT,
					 "1817 Fabric does not support NPIV "
					 "- configuring single port mode.\n");
			phba->link_flag &= ~LS_NPIV_FAB_SUPPORTED;
		}
	}

	if ((vport->fc_prevDID != vport->fc_myDID) &&
		!(vport->fc_flag & FC_VPORT_NEEDS_REG_VPI)) {

		/* If our NportID changed, we need to ensure all
		 * remaining NPORTs get unreg_login'ed.
		 */
		list_for_each_entry_safe(np, next_np,
					&vport->fc_nodes, nlp_listp) {
			if ((np->nlp_state != NLP_STE_NPR_NODE) ||
				   !(np->nlp_flag & NLP_NPR_ADISC))
				continue;
			spin_lock_irq(shost->host_lock);
			np->nlp_flag &= ~NLP_NPR_ADISC;
			spin_unlock_irq(shost->host_lock);
			lpfc_unreg_rpi(vport, np);
		}
		if (phba->sli3_options & LPFC_SLI3_NPIV_ENABLED) {
			lpfc_mbx_unreg_vpi(vport);
			spin_lock_irq(shost->host_lock);
			vport->fc_flag |= FC_VPORT_NEEDS_REG_VPI;
			spin_unlock_irq(shost->host_lock);
		}
	}

	lpfc_nlp_set_state(vport, ndlp, NLP_STE_REG_LOGIN_ISSUE);

	if (phba->sli3_options & LPFC_SLI3_NPIV_ENABLED &&
	    vport->fc_flag & FC_VPORT_NEEDS_REG_VPI) {
		lpfc_register_new_vport(phba, vport, ndlp);
		return 0;
	}
	lpfc_issue_fabric_reglogin(vport);
	return 0;
}

/*
 * We FLOGIed into an NPort, initiate pt2pt protocol
 */
static int
lpfc_cmpl_els_flogi_nport(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			  struct serv_parm *sp)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct lpfc_hba  *phba = vport->phba;
	LPFC_MBOXQ_t *mbox;
	int rc;

	spin_lock_irq(shost->host_lock);
	vport->fc_flag &= ~(FC_FABRIC | FC_PUBLIC_LOOP);
	spin_unlock_irq(shost->host_lock);

	phba->fc_edtov = FF_DEF_EDTOV;
	phba->fc_ratov = FF_DEF_RATOV;
	rc = memcmp(&vport->fc_portname, &sp->portName,
		    sizeof(vport->fc_portname));
	if (rc >= 0) {
		/* This side will initiate the PLOGI */
		spin_lock_irq(shost->host_lock);
		vport->fc_flag |= FC_PT2PT_PLOGI;
		spin_unlock_irq(shost->host_lock);

		/*
		 * N_Port ID cannot be 0, set our to LocalID the other
		 * side will be RemoteID.
		 */

		/* not equal */
		if (rc)
			vport->fc_myDID = PT2PT_LocalID;

		mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
		if (!mbox)
			goto fail;

		lpfc_config_link(phba, mbox);

		mbox->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
		mbox->vport = vport;
		rc = lpfc_sli_issue_mbox(phba, mbox, MBX_NOWAIT);
		if (rc == MBX_NOT_FINISHED) {
			mempool_free(mbox, phba->mbox_mem_pool);
			goto fail;
		}
		lpfc_nlp_put(ndlp);

		ndlp = lpfc_findnode_did(vport, PT2PT_RemoteID);
		if (!ndlp) {
			/*
			 * Cannot find existing Fabric ndlp, so allocate a
			 * new one
			 */
			ndlp = mempool_alloc(phba->nlp_mem_pool, GFP_KERNEL);
			if (!ndlp)
				goto fail;

			lpfc_nlp_init(vport, ndlp, PT2PT_RemoteID);
		}

		memcpy(&ndlp->nlp_portname, &sp->portName,
		       sizeof(struct lpfc_name));
		memcpy(&ndlp->nlp_nodename, &sp->nodeName,
		       sizeof(struct lpfc_name));
		lpfc_nlp_set_state(vport, ndlp, NLP_STE_NPR_NODE);
		spin_lock_irq(shost->host_lock);
		ndlp->nlp_flag |= NLP_NPR_2B_DISC;
		spin_unlock_irq(shost->host_lock);
	} else {
		/* This side will wait for the PLOGI */
		lpfc_nlp_put(ndlp);
	}

	/* If we are pt2pt with another NPort, force NPIV off! */
	phba->sli3_options &= ~LPFC_SLI3_NPIV_ENABLED;

	spin_lock_irq(shost->host_lock);
	vport->fc_flag |= FC_PT2PT;
	spin_unlock_irq(shost->host_lock);

	/* Start discovery - this should just do CLEAR_LA */
	lpfc_disc_start(vport);
	return 0;
fail:
	return -ENXIO;
}

static void
lpfc_cmpl_els_flogi(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
		    struct lpfc_iocbq *rspiocb)
{
	struct lpfc_vport *vport = cmdiocb->vport;
	struct Scsi_Host  *shost = lpfc_shost_from_vport(vport);
	IOCB_t *irsp = &rspiocb->iocb;
	struct lpfc_nodelist *ndlp = cmdiocb->context1;
	struct lpfc_dmabuf *pcmd = cmdiocb->context2, *prsp;
	struct serv_parm *sp;
	int rc;

	/* Check to see if link went down during discovery */
	if (lpfc_els_chk_latt(vport)) {
		/* One additional decrement on node reference count to
		 * trigger the release of the node
		 */
		lpfc_nlp_put(ndlp);
		goto out;
	}

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_CMD,
		"FLOGI cmpl:      status:x%x/x%x state:x%x",
		irsp->ulpStatus, irsp->un.ulpWord[4],
		vport->port_state);

	if (irsp->ulpStatus) {
		/* Check for retry */
		if (lpfc_els_retry(phba, cmdiocb, rspiocb))
			goto out;

		/* FLOGI failed, so there is no fabric */
		spin_lock_irq(shost->host_lock);
		vport->fc_flag &= ~(FC_FABRIC | FC_PUBLIC_LOOP);
		spin_unlock_irq(shost->host_lock);

		/* If private loop, then allow max outstanding els to be
		 * LPFC_MAX_DISC_THREADS (32). Scanning in the case of no
		 * alpa map would take too long otherwise.
		 */
		if (phba->alpa_map[0] == 0) {
			vport->cfg_discovery_threads = LPFC_MAX_DISC_THREADS;
		}

		/* FLOGI failure */
		lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS,
				 "0100 FLOGI failure Data: x%x x%x "
				 "x%x\n",
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
	lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS,
			 "0101 FLOGI completes sucessfully "
			 "Data: x%x x%x x%x x%x\n",
			 irsp->un.ulpWord[4], sp->cmn.e_d_tov,
			 sp->cmn.w2.r_a_tov, sp->cmn.edtovResolution);

	if (vport->port_state == LPFC_FLOGI) {
		/*
		 * If Common Service Parameters indicate Nport
		 * we are point to point, if Fport we are Fabric.
		 */
		if (sp->cmn.fPort)
			rc = lpfc_cmpl_els_flogi_fabric(vport, ndlp, sp, irsp);
		else
			rc = lpfc_cmpl_els_flogi_nport(vport, ndlp, sp);

		if (!rc)
			goto out;
	}

flogifail:
	lpfc_nlp_put(ndlp);

	if (!lpfc_error_lost_link(irsp)) {
		/* FLOGI failed, so just use loop map to make discovery list */
		lpfc_disc_list_loopmap(vport);

		/* Start discovery */
		lpfc_disc_start(vport);
	} else if (((irsp->ulpStatus != IOSTAT_LOCAL_REJECT) ||
			((irsp->un.ulpWord[4] != IOERR_SLI_ABORTED) &&
			(irsp->un.ulpWord[4] != IOERR_SLI_DOWN))) &&
			(phba->link_state != LPFC_CLEAR_LA)) {
		/* If FLOGI failed enable link interrupt. */
		lpfc_issue_clear_la(phba, vport);
	}
out:
	lpfc_els_free_iocb(phba, cmdiocb);
}

static int
lpfc_issue_els_flogi(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
		     uint8_t retry)
{
	struct lpfc_hba  *phba = vport->phba;
	struct serv_parm *sp;
	IOCB_t *icmd;
	struct lpfc_iocbq *elsiocb;
	struct lpfc_sli_ring *pring;
	uint8_t *pcmd;
	uint16_t cmdsize;
	uint32_t tmo;
	int rc;

	pring = &phba->sli.ring[LPFC_ELS_RING];

	cmdsize = (sizeof(uint32_t) + sizeof(struct serv_parm));
	elsiocb = lpfc_prep_els_iocb(vport, 1, cmdsize, retry, ndlp,
				     ndlp->nlp_DID, ELS_CMD_FLOGI);

	if (!elsiocb)
		return 1;

	icmd = &elsiocb->iocb;
	pcmd = (uint8_t *) (((struct lpfc_dmabuf *) elsiocb->context2)->virt);

	/* For FLOGI request, remainder of payload is service parameters */
	*((uint32_t *) (pcmd)) = ELS_CMD_FLOGI;
	pcmd += sizeof(uint32_t);
	memcpy(pcmd, &vport->fc_sparam, sizeof(struct serv_parm));
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

	if (phba->sli3_options & LPFC_SLI3_NPIV_ENABLED) {
		sp->cmn.request_multiple_Nport = 1;

		/* For FLOGI, Let FLOGI rsp set the NPortID for VPI 0 */
		icmd->ulpCt_h = 1;
		icmd->ulpCt_l = 0;
	}

	if (phba->fc_topology != TOPOLOGY_LOOP) {
		icmd->un.elsreq64.myID = 0;
		icmd->un.elsreq64.fl = 1;
	}

	tmo = phba->fc_ratov;
	phba->fc_ratov = LPFC_DISC_FLOGI_TMO;
	lpfc_set_disctmo(vport);
	phba->fc_ratov = tmo;

	phba->fc_stat.elsXmitFLOGI++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_flogi;

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_CMD,
		"Issue FLOGI:     opt:x%x",
		phba->sli3_options, 0, 0);

	rc = lpfc_issue_fabric_iocb(phba, elsiocb);
	if (rc == IOCB_ERROR) {
		lpfc_els_free_iocb(phba, elsiocb);
		return 1;
	}
	return 0;
}

int
lpfc_els_abort_flogi(struct lpfc_hba *phba)
{
	struct lpfc_sli_ring *pring;
	struct lpfc_iocbq *iocb, *next_iocb;
	struct lpfc_nodelist *ndlp;
	IOCB_t *icmd;

	/* Abort outstanding I/O on NPort <nlp_DID> */
	lpfc_printf_log(phba, KERN_INFO, LOG_DISCOVERY,
			"0201 Abort outstanding I/O on NPort x%x\n",
			Fabric_DID);

	pring = &phba->sli.ring[LPFC_ELS_RING];

	/*
	 * Check the txcmplq for an iocb that matches the nport the driver is
	 * searching for.
	 */
	spin_lock_irq(&phba->hbalock);
	list_for_each_entry_safe(iocb, next_iocb, &pring->txcmplq, list) {
		icmd = &iocb->iocb;
		if (icmd->ulpCommand == CMD_ELS_REQUEST64_CR &&
		    icmd->un.elsreq64.bdl.ulpIoTag32) {
			ndlp = (struct lpfc_nodelist *)(iocb->context1);
			if (ndlp && (ndlp->nlp_DID == Fabric_DID)) {
				lpfc_sli_issue_abort_iotag(phba, pring, iocb);
			}
		}
	}
	spin_unlock_irq(&phba->hbalock);

	return 0;
}

int
lpfc_initial_flogi(struct lpfc_vport *vport)
{
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_nodelist *ndlp;

	vport->port_state = LPFC_FLOGI;
	lpfc_set_disctmo(vport);

	/* First look for the Fabric ndlp */
	ndlp = lpfc_findnode_did(vport, Fabric_DID);
	if (!ndlp) {
		/* Cannot find existing Fabric ndlp, so allocate a new one */
		ndlp = mempool_alloc(phba->nlp_mem_pool, GFP_KERNEL);
		if (!ndlp)
			return 0;
		lpfc_nlp_init(vport, ndlp, Fabric_DID);
	} else {
		lpfc_dequeue_node(vport, ndlp);
	}

	if (lpfc_issue_els_flogi(vport, ndlp, 0)) {
		/* This decrement of reference count to node shall kick off
		 * the release of the node.
		 */
		lpfc_nlp_put(ndlp);
	}
	return 1;
}

int
lpfc_initial_fdisc(struct lpfc_vport *vport)
{
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_nodelist *ndlp;

	/* First look for the Fabric ndlp */
	ndlp = lpfc_findnode_did(vport, Fabric_DID);
	if (!ndlp) {
		/* Cannot find existing Fabric ndlp, so allocate a new one */
		ndlp = mempool_alloc(phba->nlp_mem_pool, GFP_KERNEL);
		if (!ndlp)
			return 0;
		lpfc_nlp_init(vport, ndlp, Fabric_DID);
	} else {
		lpfc_dequeue_node(vport, ndlp);
	}
	if (lpfc_issue_els_fdisc(vport, ndlp, 0)) {
		/* decrement node reference count to trigger the release of
		 * the node.
		 */
		lpfc_nlp_put(ndlp);
		return 0;
	}
	return 1;
}

void
lpfc_more_plogi(struct lpfc_vport *vport)
{
	int sentplogi;

	if (vport->num_disc_nodes)
		vport->num_disc_nodes--;

	/* Continue discovery with <num_disc_nodes> PLOGIs to go */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
			 "0232 Continue discovery with %d PLOGIs to go "
			 "Data: x%x x%x x%x\n",
			 vport->num_disc_nodes, vport->fc_plogi_cnt,
			 vport->fc_flag, vport->port_state);
	/* Check to see if there are more PLOGIs to be sent */
	if (vport->fc_flag & FC_NLP_MORE)
		/* go thru NPR nodes and issue any remaining ELS PLOGIs */
		sentplogi = lpfc_els_disc_plogi(vport);

	return;
}

static struct lpfc_nodelist *
lpfc_plogi_confirm_nport(struct lpfc_hba *phba, uint32_t *prsp,
			 struct lpfc_nodelist *ndlp)
{
	struct lpfc_vport    *vport = ndlp->vport;
	struct lpfc_nodelist *new_ndlp;
	struct lpfc_rport_data *rdata;
	struct fc_rport *rport;
	struct serv_parm *sp;
	uint8_t  name[sizeof(struct lpfc_name)];
	uint32_t rc;

	/* Fabric nodes can have the same WWPN so we don't bother searching
	 * by WWPN.  Just return the ndlp that was given to us.
	 */
	if (ndlp->nlp_type & NLP_FABRIC)
		return ndlp;

	sp = (struct serv_parm *) ((uint8_t *) prsp + sizeof(uint32_t));
	memset(name, 0, sizeof(struct lpfc_name));

	/* Now we find out if the NPort we are logging into, matches the WWPN
	 * we have for that ndlp. If not, we have some work to do.
	 */
	new_ndlp = lpfc_findnode_wwpn(vport, &sp->portName);

	if (new_ndlp == ndlp)
		return ndlp;

	if (!new_ndlp) {
		rc = memcmp(&ndlp->nlp_portname, name,
			    sizeof(struct lpfc_name));
		if (!rc)
			return ndlp;
		new_ndlp = mempool_alloc(phba->nlp_mem_pool, GFP_ATOMIC);
		if (!new_ndlp)
			return ndlp;

		lpfc_nlp_init(vport, new_ndlp, ndlp->nlp_DID);
	}

	lpfc_unreg_rpi(vport, new_ndlp);
	new_ndlp->nlp_DID = ndlp->nlp_DID;
	new_ndlp->nlp_prev_state = ndlp->nlp_prev_state;

	if (ndlp->nlp_flag & NLP_NPR_2B_DISC)
		new_ndlp->nlp_flag |= NLP_NPR_2B_DISC;
	ndlp->nlp_flag &= ~NLP_NPR_2B_DISC;

	lpfc_nlp_set_state(vport, new_ndlp, ndlp->nlp_state);

	/* Move this back to NPR state */
	if (memcmp(&ndlp->nlp_portname, name, sizeof(struct lpfc_name)) == 0) {
		/* The new_ndlp is replacing ndlp totally, so we need
		 * to put ndlp on UNUSED list and try to free it.
		 */

		/* Fix up the rport accordingly */
		rport =  ndlp->rport;
		if (rport) {
			rdata = rport->dd_data;
			if (rdata->pnode == ndlp) {
				lpfc_nlp_put(ndlp);
				ndlp->rport = NULL;
				rdata->pnode = lpfc_nlp_get(new_ndlp);
				new_ndlp->rport = rport;
			}
			new_ndlp->nlp_type = ndlp->nlp_type;
		}

		lpfc_drop_node(vport, ndlp);
	}
	else {
		lpfc_unreg_rpi(vport, ndlp);
		ndlp->nlp_DID = 0; /* Two ndlps cannot have the same did */
		lpfc_nlp_set_state(vport, ndlp, NLP_STE_NPR_NODE);
	}
	return new_ndlp;
}

void
lpfc_end_rscn(struct lpfc_vport *vport)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);

	if (vport->fc_flag & FC_RSCN_MODE) {
		/*
		 * Check to see if more RSCNs came in while we were
		 * processing this one.
		 */
		if (vport->fc_rscn_id_cnt ||
		    (vport->fc_flag & FC_RSCN_DISCOVERY) != 0)
			lpfc_els_handle_rscn(vport);
		else {
			spin_lock_irq(shost->host_lock);
			vport->fc_flag &= ~FC_RSCN_MODE;
			spin_unlock_irq(shost->host_lock);
		}
	}
}

static void
lpfc_cmpl_els_plogi(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
		    struct lpfc_iocbq *rspiocb)
{
	struct lpfc_vport *vport = cmdiocb->vport;
	struct Scsi_Host  *shost = lpfc_shost_from_vport(vport);
	IOCB_t *irsp;
	struct lpfc_nodelist *ndlp;
	struct lpfc_dmabuf *prsp;
	int disc, rc, did, type;

	/* we pass cmdiocb to state machine which needs rspiocb as well */
	cmdiocb->context_un.rsp_iocb = rspiocb;

	irsp = &rspiocb->iocb;
	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_CMD,
		"PLOGI cmpl:      status:x%x/x%x did:x%x",
		irsp->ulpStatus, irsp->un.ulpWord[4],
		irsp->un.elsreq64.remoteID);

	ndlp = lpfc_findnode_did(vport, irsp->un.elsreq64.remoteID);
	if (!ndlp) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_ELS,
				 "0136 PLOGI completes to NPort x%x "
				 "with no ndlp. Data: x%x x%x x%x\n",
				 irsp->un.elsreq64.remoteID,
				 irsp->ulpStatus, irsp->un.ulpWord[4],
				 irsp->ulpIoTag);
		goto out;
	}

	/* Since ndlp can be freed in the disc state machine, note if this node
	 * is being used during discovery.
	 */
	spin_lock_irq(shost->host_lock);
	disc = (ndlp->nlp_flag & NLP_NPR_2B_DISC);
	ndlp->nlp_flag &= ~NLP_NPR_2B_DISC;
	spin_unlock_irq(shost->host_lock);
	rc   = 0;

	/* PLOGI completes to NPort <nlp_DID> */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS,
			 "0102 PLOGI completes to NPort x%x "
			 "Data: x%x x%x x%x x%x x%x\n",
			 ndlp->nlp_DID, irsp->ulpStatus, irsp->un.ulpWord[4],
			 irsp->ulpTimeout, disc, vport->num_disc_nodes);
	/* Check to see if link went down during discovery */
	if (lpfc_els_chk_latt(vport)) {
		spin_lock_irq(shost->host_lock);
		ndlp->nlp_flag |= NLP_NPR_2B_DISC;
		spin_unlock_irq(shost->host_lock);
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
				spin_lock_irq(shost->host_lock);
				ndlp->nlp_flag |= NLP_NPR_2B_DISC;
				spin_unlock_irq(shost->host_lock);
			}
			goto out;
		}
		/* PLOGI failed */
		/* Do not call DSM for lpfc_els_abort'ed ELS cmds */
		if (lpfc_error_lost_link(irsp)) {
			rc = NLP_STE_FREED_NODE;
		} else {
			rc = lpfc_disc_state_machine(vport, ndlp, cmdiocb,
						     NLP_EVT_CMPL_PLOGI);
		}
	} else {
		/* Good status, call state machine */
		prsp = list_entry(((struct lpfc_dmabuf *)
				   cmdiocb->context2)->list.next,
				  struct lpfc_dmabuf, list);
		ndlp = lpfc_plogi_confirm_nport(phba, prsp->virt, ndlp);
		rc = lpfc_disc_state_machine(vport, ndlp, cmdiocb,
					     NLP_EVT_CMPL_PLOGI);
	}

	if (disc && vport->num_disc_nodes) {
		/* Check to see if there are more PLOGIs to be sent */
		lpfc_more_plogi(vport);

		if (vport->num_disc_nodes == 0) {
			spin_lock_irq(shost->host_lock);
			vport->fc_flag &= ~FC_NDISC_ACTIVE;
			spin_unlock_irq(shost->host_lock);

			lpfc_can_disctmo(vport);
			lpfc_end_rscn(vport);
		}
	}

out:
	lpfc_els_free_iocb(phba, cmdiocb);
	return;
}

int
lpfc_issue_els_plogi(struct lpfc_vport *vport, uint32_t did, uint8_t retry)
{
	struct lpfc_hba  *phba = vport->phba;
	struct serv_parm *sp;
	IOCB_t *icmd;
	struct lpfc_nodelist *ndlp;
	struct lpfc_iocbq *elsiocb;
	struct lpfc_sli_ring *pring;
	struct lpfc_sli *psli;
	uint8_t *pcmd;
	uint16_t cmdsize;
	int ret;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */

	ndlp = lpfc_findnode_did(vport, did);
	/* If ndlp if not NULL, we will bump the reference count on it */

	cmdsize = (sizeof(uint32_t) + sizeof(struct serv_parm));
	elsiocb = lpfc_prep_els_iocb(vport, 1, cmdsize, retry, ndlp, did,
				     ELS_CMD_PLOGI);
	if (!elsiocb)
		return 1;

	icmd = &elsiocb->iocb;
	pcmd = (uint8_t *) (((struct lpfc_dmabuf *) elsiocb->context2)->virt);

	/* For PLOGI request, remainder of payload is service parameters */
	*((uint32_t *) (pcmd)) = ELS_CMD_PLOGI;
	pcmd += sizeof(uint32_t);
	memcpy(pcmd, &vport->fc_sparam, sizeof(struct serv_parm));
	sp = (struct serv_parm *) pcmd;

	if (sp->cmn.fcphLow < FC_PH_4_3)
		sp->cmn.fcphLow = FC_PH_4_3;

	if (sp->cmn.fcphHigh < FC_PH3)
		sp->cmn.fcphHigh = FC_PH3;

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_CMD,
		"Issue PLOGI:     did:x%x",
		did, 0, 0);

	phba->fc_stat.elsXmitPLOGI++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_plogi;
	ret = lpfc_sli_issue_iocb(phba, pring, elsiocb, 0);

	if (ret == IOCB_ERROR) {
		lpfc_els_free_iocb(phba, elsiocb);
		return 1;
	}
	return 0;
}

static void
lpfc_cmpl_els_prli(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
		   struct lpfc_iocbq *rspiocb)
{
	struct lpfc_vport *vport = cmdiocb->vport;
	struct Scsi_Host  *shost = lpfc_shost_from_vport(vport);
	IOCB_t *irsp;
	struct lpfc_sli *psli;
	struct lpfc_nodelist *ndlp;

	psli = &phba->sli;
	/* we pass cmdiocb to state machine which needs rspiocb as well */
	cmdiocb->context_un.rsp_iocb = rspiocb;

	irsp = &(rspiocb->iocb);
	ndlp = (struct lpfc_nodelist *) cmdiocb->context1;
	spin_lock_irq(shost->host_lock);
	ndlp->nlp_flag &= ~NLP_PRLI_SND;
	spin_unlock_irq(shost->host_lock);

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_CMD,
		"PRLI cmpl:       status:x%x/x%x did:x%x",
		irsp->ulpStatus, irsp->un.ulpWord[4],
		ndlp->nlp_DID);
	/* PRLI completes to NPort <nlp_DID> */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS,
			 "0103 PRLI completes to NPort x%x "
			 "Data: x%x x%x x%x x%x\n",
			 ndlp->nlp_DID, irsp->ulpStatus, irsp->un.ulpWord[4],
			 irsp->ulpTimeout, vport->num_disc_nodes);

	vport->fc_prli_sent--;
	/* Check to see if link went down during discovery */
	if (lpfc_els_chk_latt(vport))
		goto out;

	if (irsp->ulpStatus) {
		/* Check for retry */
		if (lpfc_els_retry(phba, cmdiocb, rspiocb)) {
			/* ELS command is being retried */
			goto out;
		}
		/* PRLI failed */
		/* Do not call DSM for lpfc_els_abort'ed ELS cmds */
		if (lpfc_error_lost_link(irsp)) {
			goto out;
		} else {
			lpfc_disc_state_machine(vport, ndlp, cmdiocb,
						NLP_EVT_CMPL_PRLI);
		}
	} else {
		/* Good status, call state machine */
		lpfc_disc_state_machine(vport, ndlp, cmdiocb,
					NLP_EVT_CMPL_PRLI);
	}

out:
	lpfc_els_free_iocb(phba, cmdiocb);
	return;
}

int
lpfc_issue_els_prli(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
		    uint8_t retry)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct lpfc_hba *phba = vport->phba;
	PRLI *npr;
	IOCB_t *icmd;
	struct lpfc_iocbq *elsiocb;
	struct lpfc_sli_ring *pring;
	struct lpfc_sli *psli;
	uint8_t *pcmd;
	uint16_t cmdsize;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */

	cmdsize = (sizeof(uint32_t) + sizeof(PRLI));
	elsiocb = lpfc_prep_els_iocb(vport, 1, cmdsize, retry, ndlp,
				     ndlp->nlp_DID, ELS_CMD_PRLI);
	if (!elsiocb)
		return 1;

	icmd = &elsiocb->iocb;
	pcmd = (uint8_t *) (((struct lpfc_dmabuf *) elsiocb->context2)->virt);

	/* For PRLI request, remainder of payload is service parameters */
	memset(pcmd, 0, (sizeof(PRLI) + sizeof(uint32_t)));
	*((uint32_t *) (pcmd)) = ELS_CMD_PRLI;
	pcmd += sizeof(uint32_t);

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

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_CMD,
		"Issue PRLI:      did:x%x",
		ndlp->nlp_DID, 0, 0);

	phba->fc_stat.elsXmitPRLI++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_prli;
	spin_lock_irq(shost->host_lock);
	ndlp->nlp_flag |= NLP_PRLI_SND;
	spin_unlock_irq(shost->host_lock);
	if (lpfc_sli_issue_iocb(phba, pring, elsiocb, 0) == IOCB_ERROR) {
		spin_lock_irq(shost->host_lock);
		ndlp->nlp_flag &= ~NLP_PRLI_SND;
		spin_unlock_irq(shost->host_lock);
		lpfc_els_free_iocb(phba, elsiocb);
		return 1;
	}
	vport->fc_prli_sent++;
	return 0;
}

void
lpfc_more_adisc(struct lpfc_vport *vport)
{
	int sentadisc;

	if (vport->num_disc_nodes)
		vport->num_disc_nodes--;
	/* Continue discovery with <num_disc_nodes> ADISCs to go */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
			 "0210 Continue discovery with %d ADISCs to go "
			 "Data: x%x x%x x%x\n",
			 vport->num_disc_nodes, vport->fc_adisc_cnt,
			 vport->fc_flag, vport->port_state);
	/* Check to see if there are more ADISCs to be sent */
	if (vport->fc_flag & FC_NLP_MORE) {
		lpfc_set_disctmo(vport);
		/* go thru NPR nodes and issue any remaining ELS ADISCs */
		sentadisc = lpfc_els_disc_adisc(vport);
	}
	return;
}

static void
lpfc_rscn_disc(struct lpfc_vport *vport)
{
	lpfc_can_disctmo(vport);

	/* RSCN discovery */
	/* go thru NPR nodes and issue ELS PLOGIs */
	if (vport->fc_npr_cnt)
		if (lpfc_els_disc_plogi(vport))
			return;

	lpfc_end_rscn(vport);
}

static void
lpfc_cmpl_els_adisc(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
		    struct lpfc_iocbq *rspiocb)
{
	struct lpfc_vport *vport = cmdiocb->vport;
	struct Scsi_Host  *shost = lpfc_shost_from_vport(vport);
	IOCB_t *irsp;
	struct lpfc_nodelist *ndlp;
	int  disc;

	/* we pass cmdiocb to state machine which needs rspiocb as well */
	cmdiocb->context_un.rsp_iocb = rspiocb;

	irsp = &(rspiocb->iocb);
	ndlp = (struct lpfc_nodelist *) cmdiocb->context1;

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_CMD,
		"ADISC cmpl:      status:x%x/x%x did:x%x",
		irsp->ulpStatus, irsp->un.ulpWord[4],
		ndlp->nlp_DID);

	/* Since ndlp can be freed in the disc state machine, note if this node
	 * is being used during discovery.
	 */
	spin_lock_irq(shost->host_lock);
	disc = (ndlp->nlp_flag & NLP_NPR_2B_DISC);
	ndlp->nlp_flag &= ~(NLP_ADISC_SND | NLP_NPR_2B_DISC);
	spin_unlock_irq(shost->host_lock);
	/* ADISC completes to NPort <nlp_DID> */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS,
			 "0104 ADISC completes to NPort x%x "
			 "Data: x%x x%x x%x x%x x%x\n",
			 ndlp->nlp_DID, irsp->ulpStatus, irsp->un.ulpWord[4],
			 irsp->ulpTimeout, disc, vport->num_disc_nodes);
	/* Check to see if link went down during discovery */
	if (lpfc_els_chk_latt(vport)) {
		spin_lock_irq(shost->host_lock);
		ndlp->nlp_flag |= NLP_NPR_2B_DISC;
		spin_unlock_irq(shost->host_lock);
		goto out;
	}

	if (irsp->ulpStatus) {
		/* Check for retry */
		if (lpfc_els_retry(phba, cmdiocb, rspiocb)) {
			/* ELS command is being retried */
			if (disc) {
				spin_lock_irq(shost->host_lock);
				ndlp->nlp_flag |= NLP_NPR_2B_DISC;
				spin_unlock_irq(shost->host_lock);
				lpfc_set_disctmo(vport);
			}
			goto out;
		}
		/* ADISC failed */
		/* Do not call DSM for lpfc_els_abort'ed ELS cmds */
		if (!lpfc_error_lost_link(irsp)) {
			lpfc_disc_state_machine(vport, ndlp, cmdiocb,
						NLP_EVT_CMPL_ADISC);
		}
	} else {
		/* Good status, call state machine */
		lpfc_disc_state_machine(vport, ndlp, cmdiocb,
					NLP_EVT_CMPL_ADISC);
	}

	if (disc && vport->num_disc_nodes) {
		/* Check to see if there are more ADISCs to be sent */
		lpfc_more_adisc(vport);

		/* Check to see if we are done with ADISC authentication */
		if (vport->num_disc_nodes == 0) {
			/* If we get here, there is nothing left to ADISC */
			/*
			 * For NPIV, cmpl_reg_vpi will set port_state to READY,
			 * and continue discovery.
			 */
			if ((phba->sli3_options & LPFC_SLI3_NPIV_ENABLED) &&
			   !(vport->fc_flag & FC_RSCN_MODE)) {
				lpfc_issue_reg_vpi(phba, vport);
				goto out;
			}
			/*
			 * For SLI2, we need to set port_state to READY
			 * and continue discovery.
			 */
			if (vport->port_state < LPFC_VPORT_READY) {
				/* If we get here, there is nothing to ADISC */
				if (vport->port_type == LPFC_PHYSICAL_PORT)
					lpfc_issue_clear_la(phba, vport);

				if (!(vport->fc_flag & FC_ABORT_DISCOVERY)) {
					vport->num_disc_nodes = 0;
					/* go thru NPR list, issue ELS PLOGIs */
					if (vport->fc_npr_cnt)
						lpfc_els_disc_plogi(vport);

					if (!vport->num_disc_nodes) {
						spin_lock_irq(shost->host_lock);
						vport->fc_flag &=
							~FC_NDISC_ACTIVE;
						spin_unlock_irq(
							shost->host_lock);
						lpfc_can_disctmo(vport);
					}
				}
				vport->port_state = LPFC_VPORT_READY;
			} else {
				lpfc_rscn_disc(vport);
			}
		}
	}
out:
	lpfc_els_free_iocb(phba, cmdiocb);
	return;
}

int
lpfc_issue_els_adisc(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
		     uint8_t retry)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct lpfc_hba  *phba = vport->phba;
	ADISC *ap;
	IOCB_t *icmd;
	struct lpfc_iocbq *elsiocb;
	struct lpfc_sli *psli = &phba->sli;
	struct lpfc_sli_ring *pring = &psli->ring[LPFC_ELS_RING];
	uint8_t *pcmd;
	uint16_t cmdsize;

	cmdsize = (sizeof(uint32_t) + sizeof(ADISC));
	elsiocb = lpfc_prep_els_iocb(vport, 1, cmdsize, retry, ndlp,
				     ndlp->nlp_DID, ELS_CMD_ADISC);
	if (!elsiocb)
		return 1;

	icmd = &elsiocb->iocb;
	pcmd = (uint8_t *) (((struct lpfc_dmabuf *) elsiocb->context2)->virt);

	/* For ADISC request, remainder of payload is service parameters */
	*((uint32_t *) (pcmd)) = ELS_CMD_ADISC;
	pcmd += sizeof(uint32_t);

	/* Fill in ADISC payload */
	ap = (ADISC *) pcmd;
	ap->hardAL_PA = phba->fc_pref_ALPA;
	memcpy(&ap->portName, &vport->fc_portname, sizeof(struct lpfc_name));
	memcpy(&ap->nodeName, &vport->fc_nodename, sizeof(struct lpfc_name));
	ap->DID = be32_to_cpu(vport->fc_myDID);

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_CMD,
		"Issue ADISC:     did:x%x",
		ndlp->nlp_DID, 0, 0);

	phba->fc_stat.elsXmitADISC++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_adisc;
	spin_lock_irq(shost->host_lock);
	ndlp->nlp_flag |= NLP_ADISC_SND;
	spin_unlock_irq(shost->host_lock);
	if (lpfc_sli_issue_iocb(phba, pring, elsiocb, 0) == IOCB_ERROR) {
		spin_lock_irq(shost->host_lock);
		ndlp->nlp_flag &= ~NLP_ADISC_SND;
		spin_unlock_irq(shost->host_lock);
		lpfc_els_free_iocb(phba, elsiocb);
		return 1;
	}
	return 0;
}

static void
lpfc_cmpl_els_logo(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
		   struct lpfc_iocbq *rspiocb)
{
	struct lpfc_nodelist *ndlp = (struct lpfc_nodelist *) cmdiocb->context1;
	struct lpfc_vport *vport = ndlp->vport;
	struct Scsi_Host  *shost = lpfc_shost_from_vport(vport);
	IOCB_t *irsp;
	struct lpfc_sli *psli;

	psli = &phba->sli;
	/* we pass cmdiocb to state machine which needs rspiocb as well */
	cmdiocb->context_un.rsp_iocb = rspiocb;

	irsp = &(rspiocb->iocb);
	spin_lock_irq(shost->host_lock);
	ndlp->nlp_flag &= ~NLP_LOGO_SND;
	spin_unlock_irq(shost->host_lock);

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_CMD,
		"LOGO cmpl:       status:x%x/x%x did:x%x",
		irsp->ulpStatus, irsp->un.ulpWord[4],
		ndlp->nlp_DID);
	/* LOGO completes to NPort <nlp_DID> */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS,
			 "0105 LOGO completes to NPort x%x "
			 "Data: x%x x%x x%x x%x\n",
			 ndlp->nlp_DID, irsp->ulpStatus, irsp->un.ulpWord[4],
			 irsp->ulpTimeout, vport->num_disc_nodes);
	/* Check to see if link went down during discovery */
	if (lpfc_els_chk_latt(vport))
		goto out;

	if (ndlp->nlp_flag & NLP_TARGET_REMOVE) {
	        /* NLP_EVT_DEVICE_RM should unregister the RPI
		 * which should abort all outstanding IOs.
		 */
		lpfc_disc_state_machine(vport, ndlp, cmdiocb,
					NLP_EVT_DEVICE_RM);
		goto out;
	}

	if (irsp->ulpStatus) {
		/* Check for retry */
		if (lpfc_els_retry(phba, cmdiocb, rspiocb))
			/* ELS command is being retried */
			goto out;
		/* LOGO failed */
		/* Do not call DSM for lpfc_els_abort'ed ELS cmds */
		if (lpfc_error_lost_link(irsp))
			goto out;
		else
			lpfc_disc_state_machine(vport, ndlp, cmdiocb,
						NLP_EVT_CMPL_LOGO);
	} else {
		/* Good status, call state machine.
		 * This will unregister the rpi if needed.
		 */
		lpfc_disc_state_machine(vport, ndlp, cmdiocb,
					NLP_EVT_CMPL_LOGO);
	}

out:
	lpfc_els_free_iocb(phba, cmdiocb);
	return;
}

int
lpfc_issue_els_logo(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
		    uint8_t retry)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct lpfc_hba  *phba = vport->phba;
	IOCB_t *icmd;
	struct lpfc_iocbq *elsiocb;
	struct lpfc_sli_ring *pring;
	struct lpfc_sli *psli;
	uint8_t *pcmd;
	uint16_t cmdsize;
	int rc;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];

	spin_lock_irq(shost->host_lock);
	if (ndlp->nlp_flag & NLP_LOGO_SND) {
		spin_unlock_irq(shost->host_lock);
		return 0;
	}
	spin_unlock_irq(shost->host_lock);

	cmdsize = (2 * sizeof(uint32_t)) + sizeof(struct lpfc_name);
	elsiocb = lpfc_prep_els_iocb(vport, 1, cmdsize, retry, ndlp,
				     ndlp->nlp_DID, ELS_CMD_LOGO);
	if (!elsiocb)
		return 1;

	icmd = &elsiocb->iocb;
	pcmd = (uint8_t *) (((struct lpfc_dmabuf *) elsiocb->context2)->virt);
	*((uint32_t *) (pcmd)) = ELS_CMD_LOGO;
	pcmd += sizeof(uint32_t);

	/* Fill in LOGO payload */
	*((uint32_t *) (pcmd)) = be32_to_cpu(vport->fc_myDID);
	pcmd += sizeof(uint32_t);
	memcpy(pcmd, &vport->fc_portname, sizeof(struct lpfc_name));

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_CMD,
		"Issue LOGO:      did:x%x",
		ndlp->nlp_DID, 0, 0);

	phba->fc_stat.elsXmitLOGO++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_logo;
	spin_lock_irq(shost->host_lock);
	ndlp->nlp_flag |= NLP_LOGO_SND;
	spin_unlock_irq(shost->host_lock);
	rc = lpfc_sli_issue_iocb(phba, pring, elsiocb, 0);

	if (rc == IOCB_ERROR) {
		spin_lock_irq(shost->host_lock);
		ndlp->nlp_flag &= ~NLP_LOGO_SND;
		spin_unlock_irq(shost->host_lock);
		lpfc_els_free_iocb(phba, elsiocb);
		return 1;
	}
	return 0;
}

static void
lpfc_cmpl_els_cmd(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
		  struct lpfc_iocbq *rspiocb)
{
	struct lpfc_vport *vport = cmdiocb->vport;
	IOCB_t *irsp;

	irsp = &rspiocb->iocb;

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_CMD,
		"ELS cmd cmpl:    status:x%x/x%x did:x%x",
		irsp->ulpStatus, irsp->un.ulpWord[4],
		irsp->un.elsreq64.remoteID);
	/* ELS cmd tag <ulpIoTag> completes */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS,
			 "0106 ELS cmd tag x%x completes Data: x%x x%x x%x\n",
			 irsp->ulpIoTag, irsp->ulpStatus,
			 irsp->un.ulpWord[4], irsp->ulpTimeout);
	/* Check to see if link went down during discovery */
	lpfc_els_chk_latt(vport);
	lpfc_els_free_iocb(phba, cmdiocb);
	return;
}

int
lpfc_issue_els_scr(struct lpfc_vport *vport, uint32_t nportid, uint8_t retry)
{
	struct lpfc_hba  *phba = vport->phba;
	IOCB_t *icmd;
	struct lpfc_iocbq *elsiocb;
	struct lpfc_sli_ring *pring;
	struct lpfc_sli *psli;
	uint8_t *pcmd;
	uint16_t cmdsize;
	struct lpfc_nodelist *ndlp;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */
	cmdsize = (sizeof(uint32_t) + sizeof(SCR));
	ndlp = mempool_alloc(phba->nlp_mem_pool, GFP_KERNEL);
	if (!ndlp)
		return 1;

	lpfc_nlp_init(vport, ndlp, nportid);

	elsiocb = lpfc_prep_els_iocb(vport, 1, cmdsize, retry, ndlp,
				     ndlp->nlp_DID, ELS_CMD_SCR);

	if (!elsiocb) {
		/* This will trigger the release of the node just
		 * allocated
		 */
		lpfc_nlp_put(ndlp);
		return 1;
	}

	icmd = &elsiocb->iocb;
	pcmd = (uint8_t *) (((struct lpfc_dmabuf *) elsiocb->context2)->virt);

	*((uint32_t *) (pcmd)) = ELS_CMD_SCR;
	pcmd += sizeof(uint32_t);

	/* For SCR, remainder of payload is SCR parameter page */
	memset(pcmd, 0, sizeof(SCR));
	((SCR *) pcmd)->Function = SCR_FUNC_FULL;

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_CMD,
		"Issue SCR:       did:x%x",
		ndlp->nlp_DID, 0, 0);

	phba->fc_stat.elsXmitSCR++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_cmd;
	if (lpfc_sli_issue_iocb(phba, pring, elsiocb, 0) == IOCB_ERROR) {
		/* The additional lpfc_nlp_put will cause the following
		 * lpfc_els_free_iocb routine to trigger the rlease of
		 * the node.
		 */
		lpfc_nlp_put(ndlp);
		lpfc_els_free_iocb(phba, elsiocb);
		return 1;
	}
	/* This will cause the callback-function lpfc_cmpl_els_cmd to
	 * trigger the release of node.
	 */
	lpfc_nlp_put(ndlp);
	return 0;
}

static int
lpfc_issue_els_farpr(struct lpfc_vport *vport, uint32_t nportid, uint8_t retry)
{
	struct lpfc_hba  *phba = vport->phba;
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
	cmdsize = (sizeof(uint32_t) + sizeof(FARP));
	ndlp = mempool_alloc(phba->nlp_mem_pool, GFP_KERNEL);
	if (!ndlp)
		return 1;

	lpfc_nlp_init(vport, ndlp, nportid);

	elsiocb = lpfc_prep_els_iocb(vport, 1, cmdsize, retry, ndlp,
				     ndlp->nlp_DID, ELS_CMD_RNID);
	if (!elsiocb) {
		/* This will trigger the release of the node just
		 * allocated
		 */
		lpfc_nlp_put(ndlp);
		return 1;
	}

	icmd = &elsiocb->iocb;
	pcmd = (uint8_t *) (((struct lpfc_dmabuf *) elsiocb->context2)->virt);

	*((uint32_t *) (pcmd)) = ELS_CMD_FARPR;
	pcmd += sizeof(uint32_t);

	/* Fill in FARPR payload */
	fp = (FARP *) (pcmd);
	memset(fp, 0, sizeof(FARP));
	lp = (uint32_t *) pcmd;
	*lp++ = be32_to_cpu(nportid);
	*lp++ = be32_to_cpu(vport->fc_myDID);
	fp->Rflags = 0;
	fp->Mflags = (FARP_MATCH_PORT | FARP_MATCH_NODE);

	memcpy(&fp->RportName, &vport->fc_portname, sizeof(struct lpfc_name));
	memcpy(&fp->RnodeName, &vport->fc_nodename, sizeof(struct lpfc_name));
	ondlp = lpfc_findnode_did(vport, nportid);
	if (ondlp) {
		memcpy(&fp->OportName, &ondlp->nlp_portname,
		       sizeof(struct lpfc_name));
		memcpy(&fp->OnodeName, &ondlp->nlp_nodename,
		       sizeof(struct lpfc_name));
	}

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_CMD,
		"Issue FARPR:     did:x%x",
		ndlp->nlp_DID, 0, 0);

	phba->fc_stat.elsXmitFARPR++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_cmd;
	if (lpfc_sli_issue_iocb(phba, pring, elsiocb, 0) == IOCB_ERROR) {
		/* The additional lpfc_nlp_put will cause the following
		 * lpfc_els_free_iocb routine to trigger the release of
		 * the node.
		 */
		lpfc_nlp_put(ndlp);
		lpfc_els_free_iocb(phba, elsiocb);
		return 1;
	}
	/* This will cause the callback-function lpfc_cmpl_els_cmd to
	 * trigger the release of the node.
	 */
	lpfc_nlp_put(ndlp);
	return 0;
}

void
lpfc_cancel_retry_delay_tmo(struct lpfc_vport *vport, struct lpfc_nodelist *nlp)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);

	spin_lock_irq(shost->host_lock);
	nlp->nlp_flag &= ~NLP_DELAY_TMO;
	spin_unlock_irq(shost->host_lock);
	del_timer_sync(&nlp->nlp_delayfunc);
	nlp->nlp_last_elscmd = 0;

	if (!list_empty(&nlp->els_retry_evt.evt_listp))
		list_del_init(&nlp->els_retry_evt.evt_listp);

	if (nlp->nlp_flag & NLP_NPR_2B_DISC) {
		spin_lock_irq(shost->host_lock);
		nlp->nlp_flag &= ~NLP_NPR_2B_DISC;
		spin_unlock_irq(shost->host_lock);
		if (vport->num_disc_nodes) {
			/* Check to see if there are more
			 * PLOGIs to be sent
			 */
			lpfc_more_plogi(vport);

			if (vport->num_disc_nodes == 0) {
				spin_lock_irq(shost->host_lock);
				vport->fc_flag &= ~FC_NDISC_ACTIVE;
				spin_unlock_irq(shost->host_lock);
				lpfc_can_disctmo(vport);
				lpfc_end_rscn(vport);
			}
		}
	}
	return;
}

void
lpfc_els_retry_delay(unsigned long ptr)
{
	struct lpfc_nodelist *ndlp = (struct lpfc_nodelist *) ptr;
	struct lpfc_vport *vport = ndlp->vport;
	struct lpfc_hba   *phba = vport->phba;
	unsigned long flags;
	struct lpfc_work_evt  *evtp = &ndlp->els_retry_evt;

	ndlp = (struct lpfc_nodelist *) ptr;
	phba = ndlp->vport->phba;
	evtp = &ndlp->els_retry_evt;

	spin_lock_irqsave(&phba->hbalock, flags);
	if (!list_empty(&evtp->evt_listp)) {
		spin_unlock_irqrestore(&phba->hbalock, flags);
		return;
	}

	/* We need to hold the node by incrementing the reference
	 * count until the queued work is done
	 */
	evtp->evt_arg1  = lpfc_nlp_get(ndlp);
	evtp->evt       = LPFC_EVT_ELS_RETRY;
	list_add_tail(&evtp->evt_listp, &phba->work_list);
	if (phba->work_wait)
		lpfc_worker_wake_up(phba);

	spin_unlock_irqrestore(&phba->hbalock, flags);
	return;
}

void
lpfc_els_retry_delay_handler(struct lpfc_nodelist *ndlp)
{
	struct lpfc_vport *vport = ndlp->vport;
	struct Scsi_Host  *shost = lpfc_shost_from_vport(vport);
	uint32_t cmd, did, retry;

	spin_lock_irq(shost->host_lock);
	did = ndlp->nlp_DID;
	cmd = ndlp->nlp_last_elscmd;
	ndlp->nlp_last_elscmd = 0;

	if (!(ndlp->nlp_flag & NLP_DELAY_TMO)) {
		spin_unlock_irq(shost->host_lock);
		return;
	}

	ndlp->nlp_flag &= ~NLP_DELAY_TMO;
	spin_unlock_irq(shost->host_lock);
	/*
	 * If a discovery event readded nlp_delayfunc after timer
	 * firing and before processing the timer, cancel the
	 * nlp_delayfunc.
	 */
	del_timer_sync(&ndlp->nlp_delayfunc);
	retry = ndlp->nlp_retry;

	switch (cmd) {
	case ELS_CMD_FLOGI:
		lpfc_issue_els_flogi(vport, ndlp, retry);
		break;
	case ELS_CMD_PLOGI:
		if (!lpfc_issue_els_plogi(vport, ndlp->nlp_DID, retry)) {
			ndlp->nlp_prev_state = ndlp->nlp_state;
			lpfc_nlp_set_state(vport, ndlp, NLP_STE_PLOGI_ISSUE);
		}
		break;
	case ELS_CMD_ADISC:
		if (!lpfc_issue_els_adisc(vport, ndlp, retry)) {
			ndlp->nlp_prev_state = ndlp->nlp_state;
			lpfc_nlp_set_state(vport, ndlp, NLP_STE_ADISC_ISSUE);
		}
		break;
	case ELS_CMD_PRLI:
		if (!lpfc_issue_els_prli(vport, ndlp, retry)) {
			ndlp->nlp_prev_state = ndlp->nlp_state;
			lpfc_nlp_set_state(vport, ndlp, NLP_STE_PRLI_ISSUE);
		}
		break;
	case ELS_CMD_LOGO:
		if (!lpfc_issue_els_logo(vport, ndlp, retry)) {
			ndlp->nlp_prev_state = ndlp->nlp_state;
			lpfc_nlp_set_state(vport, ndlp, NLP_STE_NPR_NODE);
		}
		break;
	case ELS_CMD_FDISC:
		lpfc_issue_els_fdisc(vport, ndlp, retry);
		break;
	}
	return;
}

static int
lpfc_els_retry(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
	       struct lpfc_iocbq *rspiocb)
{
	struct lpfc_vport *vport = cmdiocb->vport;
	struct Scsi_Host  *shost = lpfc_shost_from_vport(vport);
	IOCB_t *irsp = &rspiocb->iocb;
	struct lpfc_nodelist *ndlp = (struct lpfc_nodelist *) cmdiocb->context1;
	struct lpfc_dmabuf *pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;
	uint32_t *elscmd;
	struct ls_rjt stat;
	int retry = 0, maxretry = lpfc_max_els_tries, delay = 0;
	int logerr = 0;
	uint32_t cmd = 0;
	uint32_t did;


	/* Note: context2 may be 0 for internal driver abort
	 * of delays ELS command.
	 */

	if (pcmd && pcmd->virt) {
		elscmd = (uint32_t *) (pcmd->virt);
		cmd = *elscmd++;
	}

	if (ndlp)
		did = ndlp->nlp_DID;
	else {
		/* We should only hit this case for retrying PLOGI */
		did = irsp->un.elsreq64.remoteID;
		ndlp = lpfc_findnode_did(vport, did);
		if (!ndlp && (cmd != ELS_CMD_PLOGI))
			return 1;
	}

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_CMD,
		"Retry ELS:       wd7:x%x wd4:x%x did:x%x",
		*(((uint32_t *) irsp) + 7), irsp->un.ulpWord[4], ndlp->nlp_DID);

	switch (irsp->ulpStatus) {
	case IOSTAT_FCP_RSP_ERROR:
	case IOSTAT_REMOTE_STOP:
		break;

	case IOSTAT_LOCAL_REJECT:
		switch ((irsp->un.ulpWord[4] & 0xff)) {
		case IOERR_LOOP_OPEN_FAILURE:
			if (cmd == ELS_CMD_PLOGI && cmdiocb->retry == 0)
				delay = 1000;
			retry = 1;
			break;

		case IOERR_ILLEGAL_COMMAND:
			if ((phba->sli3_options & LPFC_SLI3_VPORT_TEARDOWN) &&
			    (cmd == ELS_CMD_FDISC)) {
				lpfc_printf_vlog(vport, KERN_ERR, LOG_ELS,
						 "0124 FDISC failed (3/6) "
						 "retrying...\n");
				lpfc_mbx_unreg_vpi(vport);
				retry = 1;
				/* FDISC retry policy */
				maxretry = 48;
				if (cmdiocb->retry >= 32)
					delay = 1000;
			}
			break;

		case IOERR_NO_RESOURCES:
			logerr = 1; /* HBA out of resources */
			retry = 1;
			if (cmdiocb->retry > 100)
				delay = 100;
			maxretry = 250;
			break;

		case IOERR_ILLEGAL_FRAME:
			delay = 100;
			retry = 1;
			break;

		case IOERR_SEQUENCE_TIMEOUT:
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
		logerr = 1; /* Fabric / Remote NPort out of resources */
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
					delay = 1000;
					maxretry = 48;
				}
				retry = 1;
				break;
			}
			if (cmd == ELS_CMD_PLOGI) {
				delay = 1000;
				maxretry = lpfc_max_els_tries + 1;
				retry = 1;
				break;
			}
			if ((phba->sli3_options & LPFC_SLI3_NPIV_ENABLED) &&
			  (cmd == ELS_CMD_FDISC) &&
			  (stat.un.b.lsRjtRsnCodeExp == LSEXP_OUT_OF_RESOURCE)){
				lpfc_printf_vlog(vport, KERN_ERR, LOG_ELS,
						 "0125 FDISC Failed (x%x). "
						 "Fabric out of resources\n",
						 stat.un.lsRjtError);
				lpfc_vport_set_state(vport,
						     FC_VPORT_NO_FABRIC_RSCS);
			}
			break;

		case LSRJT_LOGICAL_BSY:
			if ((cmd == ELS_CMD_PLOGI) ||
			    (cmd == ELS_CMD_PRLI)) {
				delay = 1000;
				maxretry = 48;
			} else if (cmd == ELS_CMD_FDISC) {
				/* FDISC retry policy */
				maxretry = 48;
				if (cmdiocb->retry >= 32)
					delay = 1000;
			}
			retry = 1;
			break;

		case LSRJT_LOGICAL_ERR:
		case LSRJT_PROTOCOL_ERR:
			if ((phba->sli3_options & LPFC_SLI3_NPIV_ENABLED) &&
			  (cmd == ELS_CMD_FDISC) &&
			  ((stat.un.b.lsRjtRsnCodeExp == LSEXP_INVALID_PNAME) ||
			  (stat.un.b.lsRjtRsnCodeExp == LSEXP_INVALID_NPORT_ID))
			  ) {
				lpfc_printf_vlog(vport, KERN_ERR, LOG_ELS,
						 "0123 FDISC Failed (x%x). "
						 "Fabric Detected Bad WWN\n",
						 stat.un.lsRjtError);
				lpfc_vport_set_state(vport,
						     FC_VPORT_FABRIC_REJ_WWN);
			}
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

	if ((cmd == ELS_CMD_FLOGI) &&
	    (phba->fc_topology != TOPOLOGY_LOOP)) {
		/* FLOGI retry policy */
		retry = 1;
		maxretry = 48;
		if (cmdiocb->retry >= 32)
			delay = 1000;
	}

	if ((++cmdiocb->retry) >= maxretry) {
		phba->fc_stat.elsRetryExceeded++;
		retry = 0;
	}

	if ((vport->load_flag & FC_UNLOADING) != 0)
		retry = 0;

	if (retry) {

		/* Retry ELS command <elsCmd> to remote NPORT <did> */
		lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS,
				 "0107 Retry ELS command x%x to remote "
				 "NPORT x%x Data: x%x x%x\n",
				 cmd, did, cmdiocb->retry, delay);

		if (((cmd == ELS_CMD_PLOGI) || (cmd == ELS_CMD_ADISC)) &&
			((irsp->ulpStatus != IOSTAT_LOCAL_REJECT) ||
			((irsp->un.ulpWord[4] & 0xff) != IOERR_NO_RESOURCES))) {
			/* Don't reset timer for no resources */

			/* If discovery / RSCN timer is running, reset it */
			if (timer_pending(&vport->fc_disctmo) ||
			    (vport->fc_flag & FC_RSCN_MODE))
				lpfc_set_disctmo(vport);
		}

		phba->fc_stat.elsXmitRetry++;
		if (ndlp && delay) {
			phba->fc_stat.elsDelayRetry++;
			ndlp->nlp_retry = cmdiocb->retry;

			/* delay is specified in milliseconds */
			mod_timer(&ndlp->nlp_delayfunc,
				jiffies + msecs_to_jiffies(delay));
			spin_lock_irq(shost->host_lock);
			ndlp->nlp_flag |= NLP_DELAY_TMO;
			spin_unlock_irq(shost->host_lock);

			ndlp->nlp_prev_state = ndlp->nlp_state;
			if (cmd == ELS_CMD_PRLI)
				lpfc_nlp_set_state(vport, ndlp,
					NLP_STE_REG_LOGIN_ISSUE);
			else
				lpfc_nlp_set_state(vport, ndlp,
					NLP_STE_NPR_NODE);
			ndlp->nlp_last_elscmd = cmd;

			return 1;
		}
		switch (cmd) {
		case ELS_CMD_FLOGI:
			lpfc_issue_els_flogi(vport, ndlp, cmdiocb->retry);
			return 1;
		case ELS_CMD_FDISC:
			lpfc_issue_els_fdisc(vport, ndlp, cmdiocb->retry);
			return 1;
		case ELS_CMD_PLOGI:
			if (ndlp) {
				ndlp->nlp_prev_state = ndlp->nlp_state;
				lpfc_nlp_set_state(vport, ndlp,
						   NLP_STE_PLOGI_ISSUE);
			}
			lpfc_issue_els_plogi(vport, did, cmdiocb->retry);
			return 1;
		case ELS_CMD_ADISC:
			ndlp->nlp_prev_state = ndlp->nlp_state;
			lpfc_nlp_set_state(vport, ndlp, NLP_STE_ADISC_ISSUE);
			lpfc_issue_els_adisc(vport, ndlp, cmdiocb->retry);
			return 1;
		case ELS_CMD_PRLI:
			ndlp->nlp_prev_state = ndlp->nlp_state;
			lpfc_nlp_set_state(vport, ndlp, NLP_STE_PRLI_ISSUE);
			lpfc_issue_els_prli(vport, ndlp, cmdiocb->retry);
			return 1;
		case ELS_CMD_LOGO:
			ndlp->nlp_prev_state = ndlp->nlp_state;
			lpfc_nlp_set_state(vport, ndlp, NLP_STE_NPR_NODE);
			lpfc_issue_els_logo(vport, ndlp, cmdiocb->retry);
			return 1;
		}
	}
	/* No retry ELS command <elsCmd> to remote NPORT <did> */
	if (logerr) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_ELS,
			 "0137 No retry ELS command x%x to remote "
			 "NPORT x%x: Out of Resources: Error:x%x/%x\n",
			 cmd, did, irsp->ulpStatus,
			 irsp->un.ulpWord[4]);
	}
	else {
		lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS,
			 "0108 No retry ELS command x%x to remote "
			 "NPORT x%x Retried:%d Error:x%x/%x\n",
			 cmd, did, cmdiocb->retry, irsp->ulpStatus,
			 irsp->un.ulpWord[4]);
	}
	return 0;
}

static int
lpfc_els_free_data(struct lpfc_hba *phba, struct lpfc_dmabuf *buf_ptr1)
{
	struct lpfc_dmabuf *buf_ptr;

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
	return 0;
}

static int
lpfc_els_free_bpl(struct lpfc_hba *phba, struct lpfc_dmabuf *buf_ptr)
{
	lpfc_mbuf_free(phba, buf_ptr->virt, buf_ptr->phys);
	kfree(buf_ptr);
	return 0;
}

int
lpfc_els_free_iocb(struct lpfc_hba *phba, struct lpfc_iocbq *elsiocb)
{
	struct lpfc_dmabuf *buf_ptr, *buf_ptr1;
	struct lpfc_nodelist *ndlp;

	ndlp = (struct lpfc_nodelist *)elsiocb->context1;
	if (ndlp) {
		if (ndlp->nlp_flag & NLP_DEFER_RM) {
			lpfc_nlp_put(ndlp);

			/* If the ndlp is not being used by another discovery
			 * thread, free it.
			 */
			if (!lpfc_nlp_not_used(ndlp)) {
				/* If ndlp is being used by another discovery
				 * thread, just clear NLP_DEFER_RM
				 */
				ndlp->nlp_flag &= ~NLP_DEFER_RM;
			}
		}
		else
			lpfc_nlp_put(ndlp);
		elsiocb->context1 = NULL;
	}
	/* context2  = cmd,  context2->next = rsp, context3 = bpl */
	if (elsiocb->context2) {
		if (elsiocb->iocb_flag & LPFC_DELAY_MEM_FREE) {
			/* Firmware could still be in progress of DMAing
			 * payload, so don't free data buffer till after
			 * a hbeat.
			 */
			elsiocb->iocb_flag &= ~LPFC_DELAY_MEM_FREE;
			buf_ptr = elsiocb->context2;
			elsiocb->context2 = NULL;
			if (buf_ptr) {
				buf_ptr1 = NULL;
				spin_lock_irq(&phba->hbalock);
				if (!list_empty(&buf_ptr->list)) {
					list_remove_head(&buf_ptr->list,
						buf_ptr1, struct lpfc_dmabuf,
						list);
					INIT_LIST_HEAD(&buf_ptr1->list);
					list_add_tail(&buf_ptr1->list,
						&phba->elsbuf);
					phba->elsbuf_cnt++;
				}
				INIT_LIST_HEAD(&buf_ptr->list);
				list_add_tail(&buf_ptr->list, &phba->elsbuf);
				phba->elsbuf_cnt++;
				spin_unlock_irq(&phba->hbalock);
			}
		} else {
			buf_ptr1 = (struct lpfc_dmabuf *) elsiocb->context2;
			lpfc_els_free_data(phba, buf_ptr1);
		}
	}

	if (elsiocb->context3) {
		buf_ptr = (struct lpfc_dmabuf *) elsiocb->context3;
		lpfc_els_free_bpl(phba, buf_ptr);
	}
	lpfc_sli_release_iocbq(phba, elsiocb);
	return 0;
}

static void
lpfc_cmpl_els_logo_acc(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
		       struct lpfc_iocbq *rspiocb)
{
	struct lpfc_nodelist *ndlp = (struct lpfc_nodelist *) cmdiocb->context1;
	struct lpfc_vport *vport = cmdiocb->vport;
	IOCB_t *irsp;

	irsp = &rspiocb->iocb;
	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_RSP,
		"ACC LOGO cmpl:   status:x%x/x%x did:x%x",
		irsp->ulpStatus, irsp->un.ulpWord[4], ndlp->nlp_DID);
	/* ACC to LOGO completes to NPort <nlp_DID> */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS,
			 "0109 ACC to LOGO completes to NPort x%x "
			 "Data: x%x x%x x%x\n",
			 ndlp->nlp_DID, ndlp->nlp_flag, ndlp->nlp_state,
			 ndlp->nlp_rpi);

	if (ndlp->nlp_state == NLP_STE_NPR_NODE) {
		/* NPort Recovery mode or node is just allocated */
		if (!lpfc_nlp_not_used(ndlp)) {
			/* If the ndlp is being used by another discovery
			 * thread, just unregister the RPI.
			 */
			lpfc_unreg_rpi(vport, ndlp);
		} else {
			/* Indicate the node has already released, should
			 * not reference to it from within lpfc_els_free_iocb.
			 */
			cmdiocb->context1 = NULL;
		}
	}
	lpfc_els_free_iocb(phba, cmdiocb);
	return;
}

void
lpfc_mbx_cmpl_dflt_rpi(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmb)
{
	struct lpfc_dmabuf *mp = (struct lpfc_dmabuf *) (pmb->context1);
	struct lpfc_nodelist *ndlp = (struct lpfc_nodelist *) pmb->context2;

	pmb->context1 = NULL;
	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
	mempool_free(pmb, phba->mbox_mem_pool);
	if (ndlp) {
		lpfc_nlp_put(ndlp);
		/* This is the end of the default RPI cleanup logic for this
		 * ndlp. If no other discovery threads are using this ndlp.
		 * we should free all resources associated with it.
		 */
		lpfc_nlp_not_used(ndlp);
	}
	return;
}

static void
lpfc_cmpl_els_rsp(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
		  struct lpfc_iocbq *rspiocb)
{
	struct lpfc_nodelist *ndlp = (struct lpfc_nodelist *) cmdiocb->context1;
	struct lpfc_vport *vport = ndlp ? ndlp->vport : NULL;
	struct Scsi_Host  *shost = vport ? lpfc_shost_from_vport(vport) : NULL;
	IOCB_t  *irsp;
	uint8_t *pcmd;
	LPFC_MBOXQ_t *mbox = NULL;
	struct lpfc_dmabuf *mp = NULL;
	uint32_t ls_rjt = 0;

	irsp = &rspiocb->iocb;

	if (cmdiocb->context_un.mbox)
		mbox = cmdiocb->context_un.mbox;

	/* First determine if this is a LS_RJT cmpl. Note, this callback
	 * function can have cmdiocb->contest1 (ndlp) field set to NULL.
	 */
	pcmd = (uint8_t *) (((struct lpfc_dmabuf *) cmdiocb->context2)->virt);
	if (ndlp && (*((uint32_t *) (pcmd)) == ELS_CMD_LS_RJT)) {
		/* A LS_RJT associated with Default RPI cleanup has its own
		 * seperate code path.
		 */
		if (!(ndlp->nlp_flag & NLP_RM_DFLT_RPI))
			ls_rjt = 1;
	}

	/* Check to see if link went down during discovery */
	if (!ndlp || lpfc_els_chk_latt(vport)) {
		if (mbox) {
			mp = (struct lpfc_dmabuf *) mbox->context1;
			if (mp) {
				lpfc_mbuf_free(phba, mp->virt, mp->phys);
				kfree(mp);
			}
			mempool_free(mbox, phba->mbox_mem_pool);
		}
		if (ndlp && (ndlp->nlp_flag & NLP_RM_DFLT_RPI))
			if (lpfc_nlp_not_used(ndlp)) {
				ndlp = NULL;
				/* Indicate the node has already released,
				 * should not reference to it from within
				 * the routine lpfc_els_free_iocb.
				 */
				cmdiocb->context1 = NULL;
			}
		goto out;
	}

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_RSP,
		"ELS rsp cmpl:    status:x%x/x%x did:x%x",
		irsp->ulpStatus, irsp->un.ulpWord[4],
		cmdiocb->iocb.un.elsreq64.remoteID);
	/* ELS response tag <ulpIoTag> completes */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS,
			 "0110 ELS response tag x%x completes "
			 "Data: x%x x%x x%x x%x x%x x%x x%x\n",
			 cmdiocb->iocb.ulpIoTag, rspiocb->iocb.ulpStatus,
			 rspiocb->iocb.un.ulpWord[4], rspiocb->iocb.ulpTimeout,
			 ndlp->nlp_DID, ndlp->nlp_flag, ndlp->nlp_state,
			 ndlp->nlp_rpi);
	if (mbox) {
		if ((rspiocb->iocb.ulpStatus == 0)
		    && (ndlp->nlp_flag & NLP_ACC_REGLOGIN)) {
			lpfc_unreg_rpi(vport, ndlp);
			mbox->context2 = lpfc_nlp_get(ndlp);
			mbox->vport = vport;
			if (ndlp->nlp_flag & NLP_RM_DFLT_RPI) {
				mbox->mbox_flag |= LPFC_MBX_IMED_UNREG;
				mbox->mbox_cmpl = lpfc_mbx_cmpl_dflt_rpi;
			}
			else {
				mbox->mbox_cmpl = lpfc_mbx_cmpl_reg_login;
				ndlp->nlp_prev_state = ndlp->nlp_state;
				lpfc_nlp_set_state(vport, ndlp,
					   NLP_STE_REG_LOGIN_ISSUE);
			}
			if (lpfc_sli_issue_mbox(phba, mbox, MBX_NOWAIT)
			    != MBX_NOT_FINISHED) {
				goto out;
			}

			/* ELS rsp: Cannot issue reg_login for <NPortid> */
			lpfc_printf_vlog(vport, KERN_ERR, LOG_ELS,
				"0138 ELS rsp: Cannot issue reg_login for x%x "
				"Data: x%x x%x x%x\n",
				ndlp->nlp_DID, ndlp->nlp_flag, ndlp->nlp_state,
				ndlp->nlp_rpi);

			if (lpfc_nlp_not_used(ndlp)) {
				ndlp = NULL;
				/* Indicate node has already been released,
				 * should not reference to it from within
				 * the routine lpfc_els_free_iocb.
				 */
				cmdiocb->context1 = NULL;
			}
		} else {
			/* Do not drop node for lpfc_els_abort'ed ELS cmds */
			if (!lpfc_error_lost_link(irsp) &&
			    ndlp->nlp_flag & NLP_ACC_REGLOGIN) {
				if (lpfc_nlp_not_used(ndlp)) {
					ndlp = NULL;
					/* Indicate node has already been
					 * released, should not reference
					 * to it from within the routine
					 * lpfc_els_free_iocb.
					 */
					cmdiocb->context1 = NULL;
				}
			}
		}
		mp = (struct lpfc_dmabuf *) mbox->context1;
		if (mp) {
			lpfc_mbuf_free(phba, mp->virt, mp->phys);
			kfree(mp);
		}
		mempool_free(mbox, phba->mbox_mem_pool);
	}
out:
	if (ndlp) {
		spin_lock_irq(shost->host_lock);
		ndlp->nlp_flag &= ~(NLP_ACC_REGLOGIN | NLP_RM_DFLT_RPI);
		spin_unlock_irq(shost->host_lock);

		/* If the node is not being used by another discovery thread,
		 * and we are sending a reject, we are done with it.
		 * Release driver reference count here and free associated
		 * resources.
		 */
		if (ls_rjt)
			if (lpfc_nlp_not_used(ndlp))
				/* Indicate node has already been released,
				 * should not reference to it from within
				 * the routine lpfc_els_free_iocb.
				 */
				cmdiocb->context1 = NULL;
	}

	lpfc_els_free_iocb(phba, cmdiocb);
	return;
}

int
lpfc_els_rsp_acc(struct lpfc_vport *vport, uint32_t flag,
		 struct lpfc_iocbq *oldiocb, struct lpfc_nodelist *ndlp,
		 LPFC_MBOXQ_t *mbox)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct lpfc_hba  *phba = vport->phba;
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
		cmdsize = sizeof(uint32_t);
		elsiocb = lpfc_prep_els_iocb(vport, 0, cmdsize, oldiocb->retry,
					     ndlp, ndlp->nlp_DID, ELS_CMD_ACC);
		if (!elsiocb) {
			spin_lock_irq(shost->host_lock);
			ndlp->nlp_flag &= ~NLP_LOGO_ACC;
			spin_unlock_irq(shost->host_lock);
			return 1;
		}

		icmd = &elsiocb->iocb;
		icmd->ulpContext = oldcmd->ulpContext;	/* Xri */
		pcmd = (((struct lpfc_dmabuf *) elsiocb->context2)->virt);
		*((uint32_t *) (pcmd)) = ELS_CMD_ACC;
		pcmd += sizeof(uint32_t);

		lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_RSP,
			"Issue ACC:       did:x%x flg:x%x",
			ndlp->nlp_DID, ndlp->nlp_flag, 0);
		break;
	case ELS_CMD_PLOGI:
		cmdsize = (sizeof(struct serv_parm) + sizeof(uint32_t));
		elsiocb = lpfc_prep_els_iocb(vport, 0, cmdsize, oldiocb->retry,
					     ndlp, ndlp->nlp_DID, ELS_CMD_ACC);
		if (!elsiocb)
			return 1;

		icmd = &elsiocb->iocb;
		icmd->ulpContext = oldcmd->ulpContext;	/* Xri */
		pcmd = (((struct lpfc_dmabuf *) elsiocb->context2)->virt);

		if (mbox)
			elsiocb->context_un.mbox = mbox;

		*((uint32_t *) (pcmd)) = ELS_CMD_ACC;
		pcmd += sizeof(uint32_t);
		memcpy(pcmd, &vport->fc_sparam, sizeof(struct serv_parm));

		lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_RSP,
			"Issue ACC PLOGI: did:x%x flg:x%x",
			ndlp->nlp_DID, ndlp->nlp_flag, 0);
		break;
	case ELS_CMD_PRLO:
		cmdsize = sizeof(uint32_t) + sizeof(PRLO);
		elsiocb = lpfc_prep_els_iocb(vport, 0, cmdsize, oldiocb->retry,
					     ndlp, ndlp->nlp_DID, ELS_CMD_PRLO);
		if (!elsiocb)
			return 1;

		icmd = &elsiocb->iocb;
		icmd->ulpContext = oldcmd->ulpContext; /* Xri */
		pcmd = (((struct lpfc_dmabuf *) elsiocb->context2)->virt);

		memcpy(pcmd, ((struct lpfc_dmabuf *) oldiocb->context2)->virt,
		       sizeof(uint32_t) + sizeof(PRLO));
		*((uint32_t *) (pcmd)) = ELS_CMD_PRLO_ACC;
		els_pkt_ptr = (ELS_PKT *) pcmd;
		els_pkt_ptr->un.prlo.acceptRspCode = PRLO_REQ_EXECUTED;

		lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_RSP,
			"Issue ACC PRLO:  did:x%x flg:x%x",
			ndlp->nlp_DID, ndlp->nlp_flag, 0);
		break;
	default:
		return 1;
	}
	/* Xmit ELS ACC response tag <ulpIoTag> */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS,
			 "0128 Xmit ELS ACC response tag x%x, XRI: x%x, "
			 "DID: x%x, nlp_flag: x%x nlp_state: x%x RPI: x%x\n",
			 elsiocb->iotag, elsiocb->iocb.ulpContext,
			 ndlp->nlp_DID, ndlp->nlp_flag, ndlp->nlp_state,
			 ndlp->nlp_rpi);
	if (ndlp->nlp_flag & NLP_LOGO_ACC) {
		spin_lock_irq(shost->host_lock);
		ndlp->nlp_flag &= ~NLP_LOGO_ACC;
		spin_unlock_irq(shost->host_lock);
		elsiocb->iocb_cmpl = lpfc_cmpl_els_logo_acc;
	} else {
		elsiocb->iocb_cmpl = lpfc_cmpl_els_rsp;
	}

	phba->fc_stat.elsXmitACC++;
	rc = lpfc_sli_issue_iocb(phba, pring, elsiocb, 0);
	if (rc == IOCB_ERROR) {
		lpfc_els_free_iocb(phba, elsiocb);
		return 1;
	}
	return 0;
}

int
lpfc_els_rsp_reject(struct lpfc_vport *vport, uint32_t rejectError,
		    struct lpfc_iocbq *oldiocb, struct lpfc_nodelist *ndlp,
		    LPFC_MBOXQ_t *mbox)
{
	struct lpfc_hba  *phba = vport->phba;
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

	cmdsize = 2 * sizeof(uint32_t);
	elsiocb = lpfc_prep_els_iocb(vport, 0, cmdsize, oldiocb->retry, ndlp,
				     ndlp->nlp_DID, ELS_CMD_LS_RJT);
	if (!elsiocb)
		return 1;

	icmd = &elsiocb->iocb;
	oldcmd = &oldiocb->iocb;
	icmd->ulpContext = oldcmd->ulpContext;	/* Xri */
	pcmd = (uint8_t *) (((struct lpfc_dmabuf *) elsiocb->context2)->virt);

	*((uint32_t *) (pcmd)) = ELS_CMD_LS_RJT;
	pcmd += sizeof(uint32_t);
	*((uint32_t *) (pcmd)) = rejectError;

	if (mbox)
		elsiocb->context_un.mbox = mbox;

	/* Xmit ELS RJT <err> response tag <ulpIoTag> */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS,
			 "0129 Xmit ELS RJT x%x response tag x%x "
			 "xri x%x, did x%x, nlp_flag x%x, nlp_state x%x, "
			 "rpi x%x\n",
			 rejectError, elsiocb->iotag,
			 elsiocb->iocb.ulpContext, ndlp->nlp_DID,
			 ndlp->nlp_flag, ndlp->nlp_state, ndlp->nlp_rpi);
	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_RSP,
		"Issue LS_RJT:    did:x%x flg:x%x err:x%x",
		ndlp->nlp_DID, ndlp->nlp_flag, rejectError);

	phba->fc_stat.elsXmitLSRJT++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_rsp;
	rc = lpfc_sli_issue_iocb(phba, pring, elsiocb, 0);

	if (rc == IOCB_ERROR) {
		lpfc_els_free_iocb(phba, elsiocb);
		return 1;
	}
	return 0;
}

int
lpfc_els_rsp_adisc_acc(struct lpfc_vport *vport, struct lpfc_iocbq *oldiocb,
		       struct lpfc_nodelist *ndlp)
{
	struct lpfc_hba  *phba = vport->phba;
	struct lpfc_sli  *psli = &phba->sli;
	struct lpfc_sli_ring *pring = &psli->ring[LPFC_ELS_RING];
	ADISC *ap;
	IOCB_t *icmd, *oldcmd;
	struct lpfc_iocbq *elsiocb;
	uint8_t *pcmd;
	uint16_t cmdsize;
	int rc;

	cmdsize = sizeof(uint32_t) + sizeof(ADISC);
	elsiocb = lpfc_prep_els_iocb(vport, 0, cmdsize, oldiocb->retry, ndlp,
				     ndlp->nlp_DID, ELS_CMD_ACC);
	if (!elsiocb)
		return 1;

	icmd = &elsiocb->iocb;
	oldcmd = &oldiocb->iocb;
	icmd->ulpContext = oldcmd->ulpContext;	/* Xri */

	/* Xmit ADISC ACC response tag <ulpIoTag> */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS,
			 "0130 Xmit ADISC ACC response iotag x%x xri: "
			 "x%x, did x%x, nlp_flag x%x, nlp_state x%x rpi x%x\n",
			 elsiocb->iotag, elsiocb->iocb.ulpContext,
			 ndlp->nlp_DID, ndlp->nlp_flag, ndlp->nlp_state,
			 ndlp->nlp_rpi);
	pcmd = (uint8_t *) (((struct lpfc_dmabuf *) elsiocb->context2)->virt);

	*((uint32_t *) (pcmd)) = ELS_CMD_ACC;
	pcmd += sizeof(uint32_t);

	ap = (ADISC *) (pcmd);
	ap->hardAL_PA = phba->fc_pref_ALPA;
	memcpy(&ap->portName, &vport->fc_portname, sizeof(struct lpfc_name));
	memcpy(&ap->nodeName, &vport->fc_nodename, sizeof(struct lpfc_name));
	ap->DID = be32_to_cpu(vport->fc_myDID);

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_RSP,
		"Issue ACC ADISC: did:x%x flg:x%x",
		ndlp->nlp_DID, ndlp->nlp_flag, 0);

	phba->fc_stat.elsXmitACC++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_rsp;
	rc = lpfc_sli_issue_iocb(phba, pring, elsiocb, 0);
	if (rc == IOCB_ERROR) {
		lpfc_els_free_iocb(phba, elsiocb);
		return 1;
	}
	return 0;
}

int
lpfc_els_rsp_prli_acc(struct lpfc_vport *vport, struct lpfc_iocbq *oldiocb,
		      struct lpfc_nodelist *ndlp)
{
	struct lpfc_hba  *phba = vport->phba;
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

	cmdsize = sizeof(uint32_t) + sizeof(PRLI);
	elsiocb = lpfc_prep_els_iocb(vport, 0, cmdsize, oldiocb->retry, ndlp,
		ndlp->nlp_DID, (ELS_CMD_ACC | (ELS_CMD_PRLI & ~ELS_RSP_MASK)));
	if (!elsiocb)
		return 1;

	icmd = &elsiocb->iocb;
	oldcmd = &oldiocb->iocb;
	icmd->ulpContext = oldcmd->ulpContext;	/* Xri */
	/* Xmit PRLI ACC response tag <ulpIoTag> */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS,
			 "0131 Xmit PRLI ACC response tag x%x xri x%x, "
			 "did x%x, nlp_flag x%x, nlp_state x%x, rpi x%x\n",
			 elsiocb->iotag, elsiocb->iocb.ulpContext,
			 ndlp->nlp_DID, ndlp->nlp_flag, ndlp->nlp_state,
			 ndlp->nlp_rpi);
	pcmd = (uint8_t *) (((struct lpfc_dmabuf *) elsiocb->context2)->virt);

	*((uint32_t *) (pcmd)) = (ELS_CMD_ACC | (ELS_CMD_PRLI & ~ELS_RSP_MASK));
	pcmd += sizeof(uint32_t);

	/* For PRLI, remainder of payload is PRLI parameter page */
	memset(pcmd, 0, sizeof(PRLI));

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

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_RSP,
		"Issue ACC PRLI:  did:x%x flg:x%x",
		ndlp->nlp_DID, ndlp->nlp_flag, 0);

	phba->fc_stat.elsXmitACC++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_rsp;

	rc = lpfc_sli_issue_iocb(phba, pring, elsiocb, 0);
	if (rc == IOCB_ERROR) {
		lpfc_els_free_iocb(phba, elsiocb);
		return 1;
	}
	return 0;
}

static int
lpfc_els_rsp_rnid_acc(struct lpfc_vport *vport, uint8_t format,
		      struct lpfc_iocbq *oldiocb, struct lpfc_nodelist *ndlp)
{
	struct lpfc_hba  *phba = vport->phba;
	RNID *rn;
	IOCB_t *icmd, *oldcmd;
	struct lpfc_iocbq *elsiocb;
	struct lpfc_sli_ring *pring;
	struct lpfc_sli *psli;
	uint8_t *pcmd;
	uint16_t cmdsize;
	int rc;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];

	cmdsize = sizeof(uint32_t) + sizeof(uint32_t)
					+ (2 * sizeof(struct lpfc_name));
	if (format)
		cmdsize += sizeof(RNID_TOP_DISC);

	elsiocb = lpfc_prep_els_iocb(vport, 0, cmdsize, oldiocb->retry, ndlp,
				     ndlp->nlp_DID, ELS_CMD_ACC);
	if (!elsiocb)
		return 1;

	icmd = &elsiocb->iocb;
	oldcmd = &oldiocb->iocb;
	icmd->ulpContext = oldcmd->ulpContext;	/* Xri */
	/* Xmit RNID ACC response tag <ulpIoTag> */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS,
			 "0132 Xmit RNID ACC response tag x%x xri x%x\n",
			 elsiocb->iotag, elsiocb->iocb.ulpContext);
	pcmd = (uint8_t *) (((struct lpfc_dmabuf *) elsiocb->context2)->virt);
	*((uint32_t *) (pcmd)) = ELS_CMD_ACC;
	pcmd += sizeof(uint32_t);

	memset(pcmd, 0, sizeof(RNID));
	rn = (RNID *) (pcmd);
	rn->Format = format;
	rn->CommonLen = (2 * sizeof(struct lpfc_name));
	memcpy(&rn->portName, &vport->fc_portname, sizeof(struct lpfc_name));
	memcpy(&rn->nodeName, &vport->fc_nodename, sizeof(struct lpfc_name));
	switch (format) {
	case 0:
		rn->SpecificLen = 0;
		break;
	case RNID_TOPOLOGY_DISC:
		rn->SpecificLen = sizeof(RNID_TOP_DISC);
		memcpy(&rn->un.topologyDisc.portName,
		       &vport->fc_portname, sizeof(struct lpfc_name));
		rn->un.topologyDisc.unitType = RNID_HBA;
		rn->un.topologyDisc.physPort = 0;
		rn->un.topologyDisc.attachedNodes = 0;
		break;
	default:
		rn->CommonLen = 0;
		rn->SpecificLen = 0;
		break;
	}

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_RSP,
		"Issue ACC RNID:  did:x%x flg:x%x",
		ndlp->nlp_DID, ndlp->nlp_flag, 0);

	phba->fc_stat.elsXmitACC++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_rsp;
	lpfc_nlp_put(ndlp);
	elsiocb->context1 = NULL;  /* Don't need ndlp for cmpl,
				    * it could be freed */

	rc = lpfc_sli_issue_iocb(phba, pring, elsiocb, 0);
	if (rc == IOCB_ERROR) {
		lpfc_els_free_iocb(phba, elsiocb);
		return 1;
	}
	return 0;
}

int
lpfc_els_disc_adisc(struct lpfc_vport *vport)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct lpfc_nodelist *ndlp, *next_ndlp;
	int sentadisc = 0;

	/* go thru NPR nodes and issue any remaining ELS ADISCs */
	list_for_each_entry_safe(ndlp, next_ndlp, &vport->fc_nodes, nlp_listp) {
		if (ndlp->nlp_state == NLP_STE_NPR_NODE &&
		    (ndlp->nlp_flag & NLP_NPR_2B_DISC) != 0 &&
		    (ndlp->nlp_flag & NLP_NPR_ADISC) != 0) {
			spin_lock_irq(shost->host_lock);
			ndlp->nlp_flag &= ~NLP_NPR_ADISC;
			spin_unlock_irq(shost->host_lock);
			ndlp->nlp_prev_state = ndlp->nlp_state;
			lpfc_nlp_set_state(vport, ndlp, NLP_STE_ADISC_ISSUE);
			lpfc_issue_els_adisc(vport, ndlp, 0);
			sentadisc++;
			vport->num_disc_nodes++;
			if (vport->num_disc_nodes >=
			    vport->cfg_discovery_threads) {
				spin_lock_irq(shost->host_lock);
				vport->fc_flag |= FC_NLP_MORE;
				spin_unlock_irq(shost->host_lock);
				break;
			}
		}
	}
	if (sentadisc == 0) {
		spin_lock_irq(shost->host_lock);
		vport->fc_flag &= ~FC_NLP_MORE;
		spin_unlock_irq(shost->host_lock);
	}
	return sentadisc;
}

int
lpfc_els_disc_plogi(struct lpfc_vport *vport)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct lpfc_nodelist *ndlp, *next_ndlp;
	int sentplogi = 0;

	/* go thru NPR nodes and issue any remaining ELS PLOGIs */
	list_for_each_entry_safe(ndlp, next_ndlp, &vport->fc_nodes, nlp_listp) {
		if (ndlp->nlp_state == NLP_STE_NPR_NODE &&
		    (ndlp->nlp_flag & NLP_NPR_2B_DISC) != 0 &&
		    (ndlp->nlp_flag & NLP_DELAY_TMO) == 0 &&
		    (ndlp->nlp_flag & NLP_NPR_ADISC) == 0) {
			ndlp->nlp_prev_state = ndlp->nlp_state;
			lpfc_nlp_set_state(vport, ndlp, NLP_STE_PLOGI_ISSUE);
			lpfc_issue_els_plogi(vport, ndlp->nlp_DID, 0);
			sentplogi++;
			vport->num_disc_nodes++;
			if (vport->num_disc_nodes >=
			    vport->cfg_discovery_threads) {
				spin_lock_irq(shost->host_lock);
				vport->fc_flag |= FC_NLP_MORE;
				spin_unlock_irq(shost->host_lock);
				break;
			}
		}
	}
	if (sentplogi) {
		lpfc_set_disctmo(vport);
	}
	else {
		spin_lock_irq(shost->host_lock);
		vport->fc_flag &= ~FC_NLP_MORE;
		spin_unlock_irq(shost->host_lock);
	}
	return sentplogi;
}

void
lpfc_els_flush_rscn(struct lpfc_vport *vport)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct lpfc_hba  *phba = vport->phba;
	int i;

	for (i = 0; i < vport->fc_rscn_id_cnt; i++) {
		lpfc_in_buf_free(phba, vport->fc_rscn_id_list[i]);
		vport->fc_rscn_id_list[i] = NULL;
	}
	spin_lock_irq(shost->host_lock);
	vport->fc_rscn_id_cnt = 0;
	vport->fc_flag &= ~(FC_RSCN_MODE | FC_RSCN_DISCOVERY);
	spin_unlock_irq(shost->host_lock);
	lpfc_can_disctmo(vport);
}

int
lpfc_rscn_payload_check(struct lpfc_vport *vport, uint32_t did)
{
	D_ID ns_did;
	D_ID rscn_did;
	uint32_t *lp;
	uint32_t payload_len, i;

	ns_did.un.word = did;

	/* Never match fabric nodes for RSCNs */
	if ((did & Fabric_DID_MASK) == Fabric_DID_MASK)
		return 0;

	/* If we are doing a FULL RSCN rediscovery, match everything */
	if (vport->fc_flag & FC_RSCN_DISCOVERY)
		return did;

	for (i = 0; i < vport->fc_rscn_id_cnt; i++) {
		lp = vport->fc_rscn_id_list[i]->virt;
		payload_len = be32_to_cpu(*lp++ & ~ELS_CMD_MASK);
		payload_len -= sizeof(uint32_t);	/* take off word 0 */
		while (payload_len) {
			rscn_did.un.word = be32_to_cpu(*lp++);
			payload_len -= sizeof(uint32_t);
			switch (rscn_did.un.b.resv) {
			case 0:	/* Single N_Port ID effected */
				if (ns_did.un.word == rscn_did.un.word)
					return did;
				break;
			case 1:	/* Whole N_Port Area effected */
				if ((ns_did.un.b.domain == rscn_did.un.b.domain)
				    && (ns_did.un.b.area == rscn_did.un.b.area))
					return did;
				break;
			case 2:	/* Whole N_Port Domain effected */
				if (ns_did.un.b.domain == rscn_did.un.b.domain)
					return did;
				break;
			default:
				/* Unknown Identifier in RSCN node */
				lpfc_printf_vlog(vport, KERN_ERR, LOG_DISCOVERY,
						 "0217 Unknown Identifier in "
						 "RSCN payload Data: x%x\n",
						 rscn_did.un.word);
			case 3:	/* Whole Fabric effected */
				return did;
			}
		}
	}
	return 0;
}

static int
lpfc_rscn_recovery_check(struct lpfc_vport *vport)
{
	struct lpfc_nodelist *ndlp = NULL;

	/* Look at all nodes effected by pending RSCNs and move
	 * them to NPR state.
	 */

	list_for_each_entry(ndlp, &vport->fc_nodes, nlp_listp) {
		if (ndlp->nlp_state == NLP_STE_UNUSED_NODE ||
		    lpfc_rscn_payload_check(vport, ndlp->nlp_DID) == 0)
			continue;

		lpfc_disc_state_machine(vport, ndlp, NULL,
						NLP_EVT_DEVICE_RECOVERY);

		/*
		 * Make sure NLP_DELAY_TMO is NOT running after a device
		 * recovery event.
		 */
		if (ndlp->nlp_flag & NLP_DELAY_TMO)
			lpfc_cancel_retry_delay_tmo(vport, ndlp);
	}

	return 0;
}

static int
lpfc_els_rcv_rscn(struct lpfc_vport *vport, struct lpfc_iocbq *cmdiocb,
		  struct lpfc_nodelist *ndlp)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct lpfc_hba  *phba = vport->phba;
	struct lpfc_dmabuf *pcmd;
	uint32_t *lp, *datap;
	IOCB_t *icmd;
	uint32_t payload_len, length, nportid, *cmd;
	int rscn_cnt = vport->fc_rscn_id_cnt;
	int rscn_id = 0, hba_id = 0;
	int i;

	icmd = &cmdiocb->iocb;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;
	lp = (uint32_t *) pcmd->virt;

	payload_len = be32_to_cpu(*lp++ & ~ELS_CMD_MASK);
	payload_len -= sizeof(uint32_t);	/* take off word 0 */
	/* RSCN received */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
			 "0214 RSCN received Data: x%x x%x x%x x%x\n",
			 vport->fc_flag, payload_len, *lp, rscn_cnt);
	for (i = 0; i < payload_len/sizeof(uint32_t); i++)
		fc_host_post_event(shost, fc_get_event_number(),
			FCH_EVT_RSCN, lp[i]);

	/* If we are about to begin discovery, just ACC the RSCN.
	 * Discovery processing will satisfy it.
	 */
	if (vport->port_state <= LPFC_NS_QRY) {
		lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_UNSOL,
			"RCV RSCN ignore: did:x%x/ste:x%x flg:x%x",
			ndlp->nlp_DID, vport->port_state, ndlp->nlp_flag);

		lpfc_els_rsp_acc(vport, ELS_CMD_ACC, cmdiocb, ndlp, NULL);
		return 0;
	}

	/* If this RSCN just contains NPortIDs for other vports on this HBA,
	 * just ACC and ignore it.
	 */
	if ((phba->sli3_options & LPFC_SLI3_NPIV_ENABLED) &&
		!(vport->cfg_peer_port_login)) {
		i = payload_len;
		datap = lp;
		while (i > 0) {
			nportid = *datap++;
			nportid = ((be32_to_cpu(nportid)) & Mask_DID);
			i -= sizeof(uint32_t);
			rscn_id++;
			if (lpfc_find_vport_by_did(phba, nportid))
				hba_id++;
		}
		if (rscn_id == hba_id) {
			/* ALL NPortIDs in RSCN are on HBA */
			lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
					 "0214 Ignore RSCN "
					 "Data: x%x x%x x%x x%x\n",
					 vport->fc_flag, payload_len,
					 *lp, rscn_cnt);
			lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_UNSOL,
				"RCV RSCN vport:  did:x%x/ste:x%x flg:x%x",
				ndlp->nlp_DID, vport->port_state,
				ndlp->nlp_flag);

			lpfc_els_rsp_acc(vport, ELS_CMD_ACC, cmdiocb,
				ndlp, NULL);
			return 0;
		}
	}

	/* If we are already processing an RSCN, save the received
	 * RSCN payload buffer, cmdiocb->context2 to process later.
	 */
	if (vport->fc_flag & (FC_RSCN_MODE | FC_NDISC_ACTIVE)) {
		lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_UNSOL,
			"RCV RSCN defer:  did:x%x/ste:x%x flg:x%x",
			ndlp->nlp_DID, vport->port_state, ndlp->nlp_flag);

		spin_lock_irq(shost->host_lock);
		vport->fc_flag |= FC_RSCN_DEFERRED;
		if ((rscn_cnt < FC_MAX_HOLD_RSCN) &&
		    !(vport->fc_flag & FC_RSCN_DISCOVERY)) {
			vport->fc_flag |= FC_RSCN_MODE;
			spin_unlock_irq(shost->host_lock);
			if (rscn_cnt) {
				cmd = vport->fc_rscn_id_list[rscn_cnt-1]->virt;
				length = be32_to_cpu(*cmd & ~ELS_CMD_MASK);
			}
			if ((rscn_cnt) &&
			    (payload_len + length <= LPFC_BPL_SIZE)) {
				*cmd &= ELS_CMD_MASK;
				*cmd |= be32_to_cpu(payload_len + length);
				memcpy(((uint8_t *)cmd) + length, lp,
				       payload_len);
			} else {
				vport->fc_rscn_id_list[rscn_cnt] = pcmd;
				vport->fc_rscn_id_cnt++;
				/* If we zero, cmdiocb->context2, the calling
				 * routine will not try to free it.
				 */
				cmdiocb->context2 = NULL;
			}

			/* Deferred RSCN */
			lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
					 "0235 Deferred RSCN "
					 "Data: x%x x%x x%x\n",
					 vport->fc_rscn_id_cnt, vport->fc_flag,
					 vport->port_state);
		} else {
			vport->fc_flag |= FC_RSCN_DISCOVERY;
			spin_unlock_irq(shost->host_lock);
			/* ReDiscovery RSCN */
			lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
					 "0234 ReDiscovery RSCN "
					 "Data: x%x x%x x%x\n",
					 vport->fc_rscn_id_cnt, vport->fc_flag,
					 vport->port_state);
		}
		/* Send back ACC */
		lpfc_els_rsp_acc(vport, ELS_CMD_ACC, cmdiocb, ndlp, NULL);

		/* send RECOVERY event for ALL nodes that match RSCN payload */
		lpfc_rscn_recovery_check(vport);
		spin_lock_irq(shost->host_lock);
		vport->fc_flag &= ~FC_RSCN_DEFERRED;
		spin_unlock_irq(shost->host_lock);
		return 0;
	}

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_UNSOL,
		"RCV RSCN:        did:x%x/ste:x%x flg:x%x",
		ndlp->nlp_DID, vport->port_state, ndlp->nlp_flag);

	spin_lock_irq(shost->host_lock);
	vport->fc_flag |= FC_RSCN_MODE;
	spin_unlock_irq(shost->host_lock);
	vport->fc_rscn_id_list[vport->fc_rscn_id_cnt++] = pcmd;
	/*
	 * If we zero, cmdiocb->context2, the calling routine will
	 * not try to free it.
	 */
	cmdiocb->context2 = NULL;

	lpfc_set_disctmo(vport);

	/* Send back ACC */
	lpfc_els_rsp_acc(vport, ELS_CMD_ACC, cmdiocb, ndlp, NULL);

	/* send RECOVERY event for ALL nodes that match RSCN payload */
	lpfc_rscn_recovery_check(vport);

	return lpfc_els_handle_rscn(vport);
}

int
lpfc_els_handle_rscn(struct lpfc_vport *vport)
{
	struct lpfc_nodelist *ndlp;
	struct lpfc_hba *phba = vport->phba;

	/* Ignore RSCN if the port is being torn down. */
	if (vport->load_flag & FC_UNLOADING) {
		lpfc_els_flush_rscn(vport);
		return 0;
	}

	/* Start timer for RSCN processing */
	lpfc_set_disctmo(vport);

	/* RSCN processed */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
			 "0215 RSCN processed Data: x%x x%x x%x x%x\n",
			 vport->fc_flag, 0, vport->fc_rscn_id_cnt,
			 vport->port_state);

	/* To process RSCN, first compare RSCN data with NameServer */
	vport->fc_ns_retry = 0;
	vport->num_disc_nodes = 0;

	ndlp = lpfc_findnode_did(vport, NameServer_DID);
	if (ndlp && ndlp->nlp_state == NLP_STE_UNMAPPED_NODE) {
		/* Good ndlp, issue CT Request to NameServer */
		if (lpfc_ns_cmd(vport, SLI_CTNS_GID_FT, 0, 0) == 0)
			/* Wait for NameServer query cmpl before we can
			   continue */
			return 1;
	} else {
		/* If login to NameServer does not exist, issue one */
		/* Good status, issue PLOGI to NameServer */
		ndlp = lpfc_findnode_did(vport, NameServer_DID);
		if (ndlp)
			/* Wait for NameServer login cmpl before we can
			   continue */
			return 1;

		ndlp = mempool_alloc(phba->nlp_mem_pool, GFP_KERNEL);
		if (!ndlp) {
			lpfc_els_flush_rscn(vport);
			return 0;
		} else {
			lpfc_nlp_init(vport, ndlp, NameServer_DID);
			ndlp->nlp_type |= NLP_FABRIC;
			ndlp->nlp_prev_state = ndlp->nlp_state;
			lpfc_nlp_set_state(vport, ndlp, NLP_STE_PLOGI_ISSUE);
			lpfc_issue_els_plogi(vport, NameServer_DID, 0);
			/* Wait for NameServer login cmpl before we can
			   continue */
			return 1;
		}
	}

	lpfc_els_flush_rscn(vport);
	return 0;
}

static int
lpfc_els_rcv_flogi(struct lpfc_vport *vport, struct lpfc_iocbq *cmdiocb,
		   struct lpfc_nodelist *ndlp)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct lpfc_hba  *phba = vport->phba;
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

	lpfc_set_disctmo(vport);

	if (phba->fc_topology == TOPOLOGY_LOOP) {
		/* We should never receive a FLOGI in loop mode, ignore it */
		did = icmd->un.elsreq64.remoteID;

		/* An FLOGI ELS command <elsCmd> was received from DID <did> in
		   Loop Mode */
		lpfc_printf_vlog(vport, KERN_ERR, LOG_ELS,
				 "0113 An FLOGI ELS command x%x was "
				 "received from DID x%x in Loop Mode\n",
				 cmd, did);
		return 1;
	}

	did = Fabric_DID;

	if ((lpfc_check_sparm(vport, ndlp, sp, CLASS3))) {
		/* For a FLOGI we accept, then if our portname is greater
		 * then the remote portname we initiate Nport login.
		 */

		rc = memcmp(&vport->fc_portname, &sp->portName,
			    sizeof(struct lpfc_name));

		if (!rc) {
			mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
			if (!mbox)
				return 1;

			lpfc_linkdown(phba);
			lpfc_init_link(phba, mbox,
				       phba->cfg_topology,
				       phba->cfg_link_speed);
			mbox->mb.un.varInitLnk.lipsr_AL_PA = 0;
			mbox->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
			mbox->vport = vport;
			rc = lpfc_sli_issue_mbox(phba, mbox, MBX_NOWAIT);
			lpfc_set_loopback_flag(phba);
			if (rc == MBX_NOT_FINISHED) {
				mempool_free(mbox, phba->mbox_mem_pool);
			}
			return 1;
		} else if (rc > 0) {	/* greater than */
			spin_lock_irq(shost->host_lock);
			vport->fc_flag |= FC_PT2PT_PLOGI;
			spin_unlock_irq(shost->host_lock);
		}
		spin_lock_irq(shost->host_lock);
		vport->fc_flag |= FC_PT2PT;
		vport->fc_flag &= ~(FC_FABRIC | FC_PUBLIC_LOOP);
		spin_unlock_irq(shost->host_lock);
	} else {
		/* Reject this request because invalid parameters */
		stat.un.b.lsRjtRsvd0 = 0;
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_SPARM_OPTIONS;
		stat.un.b.vendorUnique = 0;
		lpfc_els_rsp_reject(vport, stat.un.lsRjtError, cmdiocb, ndlp,
			NULL);
		return 1;
	}

	/* Send back ACC */
	lpfc_els_rsp_acc(vport, ELS_CMD_PLOGI, cmdiocb, ndlp, NULL);

	return 0;
}

static int
lpfc_els_rcv_rnid(struct lpfc_vport *vport, struct lpfc_iocbq *cmdiocb,
		  struct lpfc_nodelist *ndlp)
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
		lpfc_els_rsp_rnid_acc(vport, rn->Format, cmdiocb, ndlp);
		break;
	default:
		/* Reject this request because format not supported */
		stat.un.b.lsRjtRsvd0 = 0;
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_CANT_GIVE_DATA;
		stat.un.b.vendorUnique = 0;
		lpfc_els_rsp_reject(vport, stat.un.lsRjtError, cmdiocb, ndlp,
			NULL);
	}
	return 0;
}

static int
lpfc_els_rcv_lirr(struct lpfc_vport *vport, struct lpfc_iocbq *cmdiocb,
		  struct lpfc_nodelist *ndlp)
{
	struct ls_rjt stat;

	/* For now, unconditionally reject this command */
	stat.un.b.lsRjtRsvd0 = 0;
	stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
	stat.un.b.lsRjtRsnCodeExp = LSEXP_CANT_GIVE_DATA;
	stat.un.b.vendorUnique = 0;
	lpfc_els_rsp_reject(vport, stat.un.lsRjtError, cmdiocb, ndlp, NULL);
	return 0;
}

static void
lpfc_els_rsp_rps_acc(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmb)
{
	struct lpfc_sli *psli = &phba->sli;
	struct lpfc_sli_ring *pring = &psli->ring[LPFC_ELS_RING];
	MAILBOX_t *mb;
	IOCB_t *icmd;
	RPS_RSP *rps_rsp;
	uint8_t *pcmd;
	struct lpfc_iocbq *elsiocb;
	struct lpfc_nodelist *ndlp;
	uint16_t xri, status;
	uint32_t cmdsize;

	mb = &pmb->mb;

	ndlp = (struct lpfc_nodelist *) pmb->context2;
	xri = (uint16_t) ((unsigned long)(pmb->context1));
	pmb->context1 = NULL;
	pmb->context2 = NULL;

	if (mb->mbxStatus) {
		mempool_free(pmb, phba->mbox_mem_pool);
		return;
	}

	cmdsize = sizeof(RPS_RSP) + sizeof(uint32_t);
	mempool_free(pmb, phba->mbox_mem_pool);
	elsiocb = lpfc_prep_els_iocb(phba->pport, 0, cmdsize,
				     lpfc_max_els_tries, ndlp,
				     ndlp->nlp_DID, ELS_CMD_ACC);

	/* Decrement the ndlp reference count from previous mbox command */
	lpfc_nlp_put(ndlp);

	if (!elsiocb)
		return;

	icmd = &elsiocb->iocb;
	icmd->ulpContext = xri;

	pcmd = (uint8_t *) (((struct lpfc_dmabuf *) elsiocb->context2)->virt);
	*((uint32_t *) (pcmd)) = ELS_CMD_ACC;
	pcmd += sizeof(uint32_t); /* Skip past command */
	rps_rsp = (RPS_RSP *)pcmd;

	if (phba->fc_topology != TOPOLOGY_LOOP)
		status = 0x10;
	else
		status = 0x8;
	if (phba->pport->fc_flag & FC_FABRIC)
		status |= 0x4;

	rps_rsp->rsvd1 = 0;
	rps_rsp->portStatus = cpu_to_be16(status);
	rps_rsp->linkFailureCnt = cpu_to_be32(mb->un.varRdLnk.linkFailureCnt);
	rps_rsp->lossSyncCnt = cpu_to_be32(mb->un.varRdLnk.lossSyncCnt);
	rps_rsp->lossSignalCnt = cpu_to_be32(mb->un.varRdLnk.lossSignalCnt);
	rps_rsp->primSeqErrCnt = cpu_to_be32(mb->un.varRdLnk.primSeqErrCnt);
	rps_rsp->invalidXmitWord = cpu_to_be32(mb->un.varRdLnk.invalidXmitWord);
	rps_rsp->crcCnt = cpu_to_be32(mb->un.varRdLnk.crcCnt);
	/* Xmit ELS RPS ACC response tag <ulpIoTag> */
	lpfc_printf_vlog(ndlp->vport, KERN_INFO, LOG_ELS,
			 "0118 Xmit ELS RPS ACC response tag x%x xri x%x, "
			 "did x%x, nlp_flag x%x, nlp_state x%x, rpi x%x\n",
			 elsiocb->iotag, elsiocb->iocb.ulpContext,
			 ndlp->nlp_DID, ndlp->nlp_flag, ndlp->nlp_state,
			 ndlp->nlp_rpi);
	elsiocb->iocb_cmpl = lpfc_cmpl_els_rsp;
	phba->fc_stat.elsXmitACC++;
	if (lpfc_sli_issue_iocb(phba, pring, elsiocb, 0) == IOCB_ERROR)
		lpfc_els_free_iocb(phba, elsiocb);
	return;
}

static int
lpfc_els_rcv_rps(struct lpfc_vport *vport, struct lpfc_iocbq *cmdiocb,
		 struct lpfc_nodelist *ndlp)
{
	struct lpfc_hba *phba = vport->phba;
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
		lpfc_els_rsp_reject(vport, stat.un.lsRjtError, cmdiocb, ndlp,
			NULL);
	}

	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;
	lp = (uint32_t *) pcmd->virt;
	flag = (be32_to_cpu(*lp++) & 0xf);
	rps = (RPS *) lp;

	if ((flag == 0) ||
	    ((flag == 1) && (be32_to_cpu(rps->un.portNum) == 0)) ||
	    ((flag == 2) && (memcmp(&rps->un.portName, &vport->fc_portname,
				    sizeof(struct lpfc_name)) == 0))) {

		printk("Fix me....\n");
		dump_stack();
		mbox = mempool_alloc(phba->mbox_mem_pool, GFP_ATOMIC);
		if (mbox) {
			lpfc_read_lnk_stat(phba, mbox);
			mbox->context1 =
			    (void *)((unsigned long) cmdiocb->iocb.ulpContext);
			mbox->context2 = lpfc_nlp_get(ndlp);
			mbox->vport = vport;
			mbox->mbox_cmpl = lpfc_els_rsp_rps_acc;
			if (lpfc_sli_issue_mbox(phba, mbox, MBX_NOWAIT)
				!= MBX_NOT_FINISHED)
				/* Mbox completion will send ELS Response */
				return 0;
			/* Decrement reference count used for the failed mbox
			 * command.
			 */
			lpfc_nlp_put(ndlp);
			mempool_free(mbox, phba->mbox_mem_pool);
		}
	}
	stat.un.b.lsRjtRsvd0 = 0;
	stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
	stat.un.b.lsRjtRsnCodeExp = LSEXP_CANT_GIVE_DATA;
	stat.un.b.vendorUnique = 0;
	lpfc_els_rsp_reject(vport, stat.un.lsRjtError, cmdiocb, ndlp, NULL);
	return 0;
}

static int
lpfc_els_rsp_rpl_acc(struct lpfc_vport *vport, uint16_t cmdsize,
		     struct lpfc_iocbq *oldiocb, struct lpfc_nodelist *ndlp)
{
	struct lpfc_hba *phba = vport->phba;
	IOCB_t *icmd, *oldcmd;
	RPL_RSP rpl_rsp;
	struct lpfc_iocbq *elsiocb;
	struct lpfc_sli *psli = &phba->sli;
	struct lpfc_sli_ring *pring = &psli->ring[LPFC_ELS_RING];
	uint8_t *pcmd;

	elsiocb = lpfc_prep_els_iocb(vport, 0, cmdsize, oldiocb->retry, ndlp,
				     ndlp->nlp_DID, ELS_CMD_ACC);

	if (!elsiocb)
		return 1;

	icmd = &elsiocb->iocb;
	oldcmd = &oldiocb->iocb;
	icmd->ulpContext = oldcmd->ulpContext;	/* Xri */

	pcmd = (((struct lpfc_dmabuf *) elsiocb->context2)->virt);
	*((uint32_t *) (pcmd)) = ELS_CMD_ACC;
	pcmd += sizeof(uint16_t);
	*((uint16_t *)(pcmd)) = be16_to_cpu(cmdsize);
	pcmd += sizeof(uint16_t);

	/* Setup the RPL ACC payload */
	rpl_rsp.listLen = be32_to_cpu(1);
	rpl_rsp.index = 0;
	rpl_rsp.port_num_blk.portNum = 0;
	rpl_rsp.port_num_blk.portID = be32_to_cpu(vport->fc_myDID);
	memcpy(&rpl_rsp.port_num_blk.portName, &vport->fc_portname,
	    sizeof(struct lpfc_name));
	memcpy(pcmd, &rpl_rsp, cmdsize - sizeof(uint32_t));
	/* Xmit ELS RPL ACC response tag <ulpIoTag> */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS,
			 "0120 Xmit ELS RPL ACC response tag x%x "
			 "xri x%x, did x%x, nlp_flag x%x, nlp_state x%x, "
			 "rpi x%x\n",
			 elsiocb->iotag, elsiocb->iocb.ulpContext,
			 ndlp->nlp_DID, ndlp->nlp_flag, ndlp->nlp_state,
			 ndlp->nlp_rpi);
	elsiocb->iocb_cmpl = lpfc_cmpl_els_rsp;
	phba->fc_stat.elsXmitACC++;
	if (lpfc_sli_issue_iocb(phba, pring, elsiocb, 0) == IOCB_ERROR) {
		lpfc_els_free_iocb(phba, elsiocb);
		return 1;
	}
	return 0;
}

static int
lpfc_els_rcv_rpl(struct lpfc_vport *vport, struct lpfc_iocbq *cmdiocb,
		 struct lpfc_nodelist *ndlp)
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
		lpfc_els_rsp_reject(vport, stat.un.lsRjtError, cmdiocb, ndlp,
			NULL);
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
	lpfc_els_rsp_rpl_acc(vport, cmdsize, cmdiocb, ndlp);

	return 0;
}

static int
lpfc_els_rcv_farp(struct lpfc_vport *vport, struct lpfc_iocbq *cmdiocb,
		  struct lpfc_nodelist *ndlp)
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
	lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS,
			 "0601 FARP-REQ received from DID x%x\n", did);
	/* We will only support match on WWPN or WWNN */
	if (fp->Mflags & ~(FARP_MATCH_NODE | FARP_MATCH_PORT)) {
		return 0;
	}

	cnt = 0;
	/* If this FARP command is searching for my portname */
	if (fp->Mflags & FARP_MATCH_PORT) {
		if (memcmp(&fp->RportName, &vport->fc_portname,
			   sizeof(struct lpfc_name)) == 0)
			cnt = 1;
	}

	/* If this FARP command is searching for my nodename */
	if (fp->Mflags & FARP_MATCH_NODE) {
		if (memcmp(&fp->RnodeName, &vport->fc_nodename,
			   sizeof(struct lpfc_name)) == 0)
			cnt = 1;
	}

	if (cnt) {
		if ((ndlp->nlp_state == NLP_STE_UNMAPPED_NODE) ||
		   (ndlp->nlp_state == NLP_STE_MAPPED_NODE)) {
			/* Log back into the node before sending the FARP. */
			if (fp->Rflags & FARP_REQUEST_PLOGI) {
				ndlp->nlp_prev_state = ndlp->nlp_state;
				lpfc_nlp_set_state(vport, ndlp,
						   NLP_STE_PLOGI_ISSUE);
				lpfc_issue_els_plogi(vport, ndlp->nlp_DID, 0);
			}

			/* Send a FARP response to that node */
			if (fp->Rflags & FARP_REQUEST_FARPR)
				lpfc_issue_els_farpr(vport, did, 0);
		}
	}
	return 0;
}

static int
lpfc_els_rcv_farpr(struct lpfc_vport *vport, struct lpfc_iocbq *cmdiocb,
		   struct lpfc_nodelist  *ndlp)
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
	lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS,
			 "0600 FARP-RSP received from DID x%x\n", did);
	/* ACCEPT the Farp resp request */
	lpfc_els_rsp_acc(vport, ELS_CMD_ACC, cmdiocb, ndlp, NULL);

	return 0;
}

static int
lpfc_els_rcv_fan(struct lpfc_vport *vport, struct lpfc_iocbq *cmdiocb,
		 struct lpfc_nodelist *fan_ndlp)
{
	struct lpfc_dmabuf *pcmd;
	uint32_t *lp;
	IOCB_t *icmd;
	uint32_t cmd, did;
	FAN *fp;
	struct lpfc_nodelist *ndlp, *next_ndlp;
	struct lpfc_hba *phba = vport->phba;

	/* FAN received */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS,
			 "0265 FAN received\n");
	icmd = &cmdiocb->iocb;
	did = icmd->un.elsreq64.remoteID;
	pcmd = (struct lpfc_dmabuf *)cmdiocb->context2;
	lp = (uint32_t *)pcmd->virt;

	cmd = *lp++;
	fp = (FAN *) lp;

	/* FAN received; Fan does not have a reply sequence */

	if (phba->pport->port_state == LPFC_LOCAL_CFG_LINK) {
		if ((memcmp(&phba->fc_fabparam.nodeName, &fp->FnodeName,
			sizeof(struct lpfc_name)) != 0) ||
		    (memcmp(&phba->fc_fabparam.portName, &fp->FportName,
			sizeof(struct lpfc_name)) != 0)) {
			/*
			 * This node has switched fabrics.  FLOGI is required
			 * Clean up the old rpi's
			 */

			list_for_each_entry_safe(ndlp, next_ndlp,
						 &vport->fc_nodes, nlp_listp) {
				if (ndlp->nlp_state != NLP_STE_NPR_NODE)
					continue;
				if (ndlp->nlp_type & NLP_FABRIC) {
					/*
					 * Clean up old Fabric, Nameserver and
					 * other NLP_FABRIC logins
					 */
					lpfc_drop_node(vport, ndlp);

				} else if (!(ndlp->nlp_flag & NLP_NPR_ADISC)) {
					/* Fail outstanding I/O now since this
					 * device is marked for PLOGI
					 */
					lpfc_unreg_rpi(vport, ndlp);
				}
			}

			lpfc_initial_flogi(vport);
			return 0;
		}
		/* Discovery not needed,
		 * move the nodes to their original state.
		 */
		list_for_each_entry_safe(ndlp, next_ndlp, &vport->fc_nodes,
					 nlp_listp) {
			if (ndlp->nlp_state != NLP_STE_NPR_NODE)
				continue;

			switch (ndlp->nlp_prev_state) {
			case NLP_STE_UNMAPPED_NODE:
				ndlp->nlp_prev_state = NLP_STE_NPR_NODE;
				lpfc_nlp_set_state(vport, ndlp,
						   NLP_STE_UNMAPPED_NODE);
				break;

			case NLP_STE_MAPPED_NODE:
				ndlp->nlp_prev_state = NLP_STE_NPR_NODE;
				lpfc_nlp_set_state(vport, ndlp,
						   NLP_STE_MAPPED_NODE);
				break;

			default:
				break;
			}
		}

		/* Start discovery - this should just do CLEAR_LA */
		lpfc_disc_start(vport);
	}
	return 0;
}

void
lpfc_els_timeout(unsigned long ptr)
{
	struct lpfc_vport *vport = (struct lpfc_vport *) ptr;
	struct lpfc_hba   *phba = vport->phba;
	unsigned long iflag;

	spin_lock_irqsave(&vport->work_port_lock, iflag);
	if ((vport->work_port_events & WORKER_ELS_TMO) == 0) {
		vport->work_port_events |= WORKER_ELS_TMO;
		spin_unlock_irqrestore(&vport->work_port_lock, iflag);

		spin_lock_irqsave(&phba->hbalock, iflag);
		if (phba->work_wait)
			lpfc_worker_wake_up(phba);
		spin_unlock_irqrestore(&phba->hbalock, iflag);
	}
	else
		spin_unlock_irqrestore(&vport->work_port_lock, iflag);
	return;
}

void
lpfc_els_timeout_handler(struct lpfc_vport *vport)
{
	struct lpfc_hba  *phba = vport->phba;
	struct lpfc_sli_ring *pring;
	struct lpfc_iocbq *tmp_iocb, *piocb;
	IOCB_t *cmd = NULL;
	struct lpfc_dmabuf *pcmd;
	uint32_t els_command = 0;
	uint32_t timeout;
	uint32_t remote_ID = 0xffffffff;

	/* If the timer is already canceled do nothing */
	if ((vport->work_port_events & WORKER_ELS_TMO) == 0) {
		return;
	}
	spin_lock_irq(&phba->hbalock);
	timeout = (uint32_t)(phba->fc_ratov << 1);

	pring = &phba->sli.ring[LPFC_ELS_RING];

	list_for_each_entry_safe(piocb, tmp_iocb, &pring->txcmplq, list) {
		cmd = &piocb->iocb;

		if ((piocb->iocb_flag & LPFC_IO_LIBDFC) != 0 ||
		    piocb->iocb.ulpCommand == CMD_ABORT_XRI_CN ||
		    piocb->iocb.ulpCommand == CMD_CLOSE_XRI_CN)
			continue;

		if (piocb->vport != vport)
			continue;

		pcmd = (struct lpfc_dmabuf *) piocb->context2;
		if (pcmd)
			els_command = *(uint32_t *) (pcmd->virt);

		if (els_command == ELS_CMD_FARP ||
		    els_command == ELS_CMD_FARPR ||
		    els_command == ELS_CMD_FDISC)
			continue;

		if (vport != piocb->vport)
			continue;

		if (piocb->drvrTimeout > 0) {
			if (piocb->drvrTimeout >= timeout)
				piocb->drvrTimeout -= timeout;
			else
				piocb->drvrTimeout = 0;
			continue;
		}

		remote_ID = 0xffffffff;
		if (cmd->ulpCommand != CMD_GEN_REQUEST64_CR)
			remote_ID = cmd->un.elsreq64.remoteID;
		else {
			struct lpfc_nodelist *ndlp;
			ndlp = __lpfc_findnode_rpi(vport, cmd->ulpContext);
			if (ndlp)
				remote_ID = ndlp->nlp_DID;
		}
		lpfc_printf_vlog(vport, KERN_ERR, LOG_ELS,
				 "0127 ELS timeout Data: x%x x%x x%x "
				 "x%x\n", els_command,
				 remote_ID, cmd->ulpCommand, cmd->ulpIoTag);
		lpfc_sli_issue_abort_iotag(phba, pring, piocb);
	}
	spin_unlock_irq(&phba->hbalock);

	if (phba->sli.ring[LPFC_ELS_RING].txcmplq_cnt)
		mod_timer(&vport->els_tmofunc, jiffies + HZ * timeout);
}

void
lpfc_els_flush_cmd(struct lpfc_vport *vport)
{
	LIST_HEAD(completions);
	struct lpfc_hba  *phba = vport->phba;
	struct lpfc_sli_ring *pring = &phba->sli.ring[LPFC_ELS_RING];
	struct lpfc_iocbq *tmp_iocb, *piocb;
	IOCB_t *cmd = NULL;

	lpfc_fabric_abort_vport(vport);

	spin_lock_irq(&phba->hbalock);
	list_for_each_entry_safe(piocb, tmp_iocb, &pring->txq, list) {
		cmd = &piocb->iocb;

		if (piocb->iocb_flag & LPFC_IO_LIBDFC) {
			continue;
		}

		/* Do not flush out the QUE_RING and ABORT/CLOSE iocbs */
		if (cmd->ulpCommand == CMD_QUE_RING_BUF_CN ||
		    cmd->ulpCommand == CMD_QUE_RING_BUF64_CN ||
		    cmd->ulpCommand == CMD_CLOSE_XRI_CN ||
		    cmd->ulpCommand == CMD_ABORT_XRI_CN)
			continue;

		if (piocb->vport != vport)
			continue;

		list_move_tail(&piocb->list, &completions);
		pring->txq_cnt--;
	}

	list_for_each_entry_safe(piocb, tmp_iocb, &pring->txcmplq, list) {
		if (piocb->iocb_flag & LPFC_IO_LIBDFC) {
			continue;
		}

		if (piocb->vport != vport)
			continue;

		lpfc_sli_issue_abort_iotag(phba, pring, piocb);
	}
	spin_unlock_irq(&phba->hbalock);

	while (!list_empty(&completions)) {
		piocb = list_get_first(&completions, struct lpfc_iocbq, list);
		cmd = &piocb->iocb;
		list_del_init(&piocb->list);

		if (!piocb->iocb_cmpl)
			lpfc_sli_release_iocbq(phba, piocb);
		else {
			cmd->ulpStatus = IOSTAT_LOCAL_REJECT;
			cmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
			(piocb->iocb_cmpl) (phba, piocb, piocb);
		}
	}

	return;
}

void
lpfc_els_flush_all_cmd(struct lpfc_hba  *phba)
{
	LIST_HEAD(completions);
	struct lpfc_sli_ring *pring = &phba->sli.ring[LPFC_ELS_RING];
	struct lpfc_iocbq *tmp_iocb, *piocb;
	IOCB_t *cmd = NULL;

	lpfc_fabric_abort_hba(phba);
	spin_lock_irq(&phba->hbalock);
	list_for_each_entry_safe(piocb, tmp_iocb, &pring->txq, list) {
		cmd = &piocb->iocb;
		if (piocb->iocb_flag & LPFC_IO_LIBDFC)
			continue;
		/* Do not flush out the QUE_RING and ABORT/CLOSE iocbs */
		if (cmd->ulpCommand == CMD_QUE_RING_BUF_CN ||
		    cmd->ulpCommand == CMD_QUE_RING_BUF64_CN ||
		    cmd->ulpCommand == CMD_CLOSE_XRI_CN ||
		    cmd->ulpCommand == CMD_ABORT_XRI_CN)
			continue;
		list_move_tail(&piocb->list, &completions);
		pring->txq_cnt--;
	}
	list_for_each_entry_safe(piocb, tmp_iocb, &pring->txcmplq, list) {
		if (piocb->iocb_flag & LPFC_IO_LIBDFC)
			continue;
		lpfc_sli_issue_abort_iotag(phba, pring, piocb);
	}
	spin_unlock_irq(&phba->hbalock);
	while (!list_empty(&completions)) {
		piocb = list_get_first(&completions, struct lpfc_iocbq, list);
		cmd = &piocb->iocb;
		list_del_init(&piocb->list);
		if (!piocb->iocb_cmpl)
			lpfc_sli_release_iocbq(phba, piocb);
		else {
			cmd->ulpStatus = IOSTAT_LOCAL_REJECT;
			cmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
			(piocb->iocb_cmpl) (phba, piocb, piocb);
		}
	}
	return;
}

static void
lpfc_els_unsol_buffer(struct lpfc_hba *phba, struct lpfc_sli_ring *pring,
		      struct lpfc_vport *vport, struct lpfc_iocbq *elsiocb)
{
	struct Scsi_Host  *shost;
	struct lpfc_nodelist *ndlp;
	struct ls_rjt stat;
	uint32_t *payload;
	uint32_t cmd, did, newnode, rjt_err = 0;
	IOCB_t *icmd = &elsiocb->iocb;

	if (vport == NULL || elsiocb->context2 == NULL)
		goto dropit;

	newnode = 0;
	payload = ((struct lpfc_dmabuf *)elsiocb->context2)->virt;
	cmd = *payload;
	if ((phba->sli3_options & LPFC_SLI3_HBQ_ENABLED) == 0)
		lpfc_post_buffer(phba, pring, 1, 1);

	did = icmd->un.rcvels.remoteID;
	if (icmd->ulpStatus) {
		lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_UNSOL,
			"RCV Unsol ELS:  status:x%x/x%x did:x%x",
			icmd->ulpStatus, icmd->un.ulpWord[4], did);
		goto dropit;
	}

	/* Check to see if link went down during discovery */
	if (lpfc_els_chk_latt(vport))
		goto dropit;

	/* Ignore traffic recevied during vport shutdown. */
	if (vport->load_flag & FC_UNLOADING)
		goto dropit;

	ndlp = lpfc_findnode_did(vport, did);
	if (!ndlp) {
		/* Cannot find existing Fabric ndlp, so allocate a new one */
		ndlp = mempool_alloc(phba->nlp_mem_pool, GFP_KERNEL);
		if (!ndlp)
			goto dropit;

		lpfc_nlp_init(vport, ndlp, did);
		lpfc_nlp_set_state(vport, ndlp, NLP_STE_NPR_NODE);
		newnode = 1;
		if ((did & Fabric_DID_MASK) == Fabric_DID_MASK) {
			ndlp->nlp_type |= NLP_FABRIC;
		}
	}
	else {
		if (ndlp->nlp_state == NLP_STE_UNUSED_NODE) {
			/* This is simular to the new node path */
			lpfc_nlp_get(ndlp);
			lpfc_nlp_set_state(vport, ndlp, NLP_STE_NPR_NODE);
			newnode = 1;
		}
	}

	phba->fc_stat.elsRcvFrame++;
	if (elsiocb->context1)
		lpfc_nlp_put(elsiocb->context1);
	elsiocb->context1 = lpfc_nlp_get(ndlp);
	elsiocb->vport = vport;

	if ((cmd & ELS_CMD_MASK) == ELS_CMD_RSCN) {
		cmd &= ELS_CMD_MASK;
	}
	/* ELS command <elsCmd> received from NPORT <did> */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS,
			 "0112 ELS command x%x received from NPORT x%x "
			 "Data: x%x\n", cmd, did, vport->port_state);
	switch (cmd) {
	case ELS_CMD_PLOGI:
		lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_UNSOL,
			"RCV PLOGI:       did:x%x/ste:x%x flg:x%x",
			did, vport->port_state, ndlp->nlp_flag);

		phba->fc_stat.elsRcvPLOGI++;
		ndlp = lpfc_plogi_confirm_nport(phba, payload, ndlp);

		if (vport->port_state < LPFC_DISC_AUTH) {
			rjt_err = LSRJT_UNABLE_TPC;
			break;
		}

		shost = lpfc_shost_from_vport(vport);
		spin_lock_irq(shost->host_lock);
		ndlp->nlp_flag &= ~NLP_TARGET_REMOVE;
		spin_unlock_irq(shost->host_lock);

		lpfc_disc_state_machine(vport, ndlp, elsiocb,
					NLP_EVT_RCV_PLOGI);

		break;
	case ELS_CMD_FLOGI:
		lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_UNSOL,
			"RCV FLOGI:       did:x%x/ste:x%x flg:x%x",
			did, vport->port_state, ndlp->nlp_flag);

		phba->fc_stat.elsRcvFLOGI++;
		lpfc_els_rcv_flogi(vport, elsiocb, ndlp);
		if (newnode)
			lpfc_nlp_put(ndlp);
		break;
	case ELS_CMD_LOGO:
		lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_UNSOL,
			"RCV LOGO:        did:x%x/ste:x%x flg:x%x",
			did, vport->port_state, ndlp->nlp_flag);

		phba->fc_stat.elsRcvLOGO++;
		if (vport->port_state < LPFC_DISC_AUTH) {
			rjt_err = LSRJT_UNABLE_TPC;
			break;
		}
		lpfc_disc_state_machine(vport, ndlp, elsiocb, NLP_EVT_RCV_LOGO);
		break;
	case ELS_CMD_PRLO:
		lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_UNSOL,
			"RCV PRLO:        did:x%x/ste:x%x flg:x%x",
			did, vport->port_state, ndlp->nlp_flag);

		phba->fc_stat.elsRcvPRLO++;
		if (vport->port_state < LPFC_DISC_AUTH) {
			rjt_err = LSRJT_UNABLE_TPC;
			break;
		}
		lpfc_disc_state_machine(vport, ndlp, elsiocb, NLP_EVT_RCV_PRLO);
		break;
	case ELS_CMD_RSCN:
		phba->fc_stat.elsRcvRSCN++;
		lpfc_els_rcv_rscn(vport, elsiocb, ndlp);
		if (newnode)
			lpfc_nlp_put(ndlp);
		break;
	case ELS_CMD_ADISC:
		lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_UNSOL,
			"RCV ADISC:       did:x%x/ste:x%x flg:x%x",
			did, vport->port_state, ndlp->nlp_flag);

		phba->fc_stat.elsRcvADISC++;
		if (vport->port_state < LPFC_DISC_AUTH) {
			rjt_err = LSRJT_UNABLE_TPC;
			break;
		}
		lpfc_disc_state_machine(vport, ndlp, elsiocb,
					NLP_EVT_RCV_ADISC);
		break;
	case ELS_CMD_PDISC:
		lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_UNSOL,
			"RCV PDISC:       did:x%x/ste:x%x flg:x%x",
			did, vport->port_state, ndlp->nlp_flag);

		phba->fc_stat.elsRcvPDISC++;
		if (vport->port_state < LPFC_DISC_AUTH) {
			rjt_err = LSRJT_UNABLE_TPC;
			break;
		}
		lpfc_disc_state_machine(vport, ndlp, elsiocb,
					NLP_EVT_RCV_PDISC);
		break;
	case ELS_CMD_FARPR:
		lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_UNSOL,
			"RCV FARPR:       did:x%x/ste:x%x flg:x%x",
			did, vport->port_state, ndlp->nlp_flag);

		phba->fc_stat.elsRcvFARPR++;
		lpfc_els_rcv_farpr(vport, elsiocb, ndlp);
		break;
	case ELS_CMD_FARP:
		lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_UNSOL,
			"RCV FARP:        did:x%x/ste:x%x flg:x%x",
			did, vport->port_state, ndlp->nlp_flag);

		phba->fc_stat.elsRcvFARP++;
		lpfc_els_rcv_farp(vport, elsiocb, ndlp);
		break;
	case ELS_CMD_FAN:
		lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_UNSOL,
			"RCV FAN:         did:x%x/ste:x%x flg:x%x",
			did, vport->port_state, ndlp->nlp_flag);

		phba->fc_stat.elsRcvFAN++;
		lpfc_els_rcv_fan(vport, elsiocb, ndlp);
		break;
	case ELS_CMD_PRLI:
		lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_UNSOL,
			"RCV PRLI:        did:x%x/ste:x%x flg:x%x",
			did, vport->port_state, ndlp->nlp_flag);

		phba->fc_stat.elsRcvPRLI++;
		if (vport->port_state < LPFC_DISC_AUTH) {
			rjt_err = LSRJT_UNABLE_TPC;
			break;
		}
		lpfc_disc_state_machine(vport, ndlp, elsiocb, NLP_EVT_RCV_PRLI);
		break;
	case ELS_CMD_LIRR:
		lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_UNSOL,
			"RCV LIRR:        did:x%x/ste:x%x flg:x%x",
			did, vport->port_state, ndlp->nlp_flag);

		phba->fc_stat.elsRcvLIRR++;
		lpfc_els_rcv_lirr(vport, elsiocb, ndlp);
		if (newnode)
			lpfc_nlp_put(ndlp);
		break;
	case ELS_CMD_RPS:
		lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_UNSOL,
			"RCV RPS:         did:x%x/ste:x%x flg:x%x",
			did, vport->port_state, ndlp->nlp_flag);

		phba->fc_stat.elsRcvRPS++;
		lpfc_els_rcv_rps(vport, elsiocb, ndlp);
		if (newnode)
			lpfc_nlp_put(ndlp);
		break;
	case ELS_CMD_RPL:
		lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_UNSOL,
			"RCV RPL:         did:x%x/ste:x%x flg:x%x",
			did, vport->port_state, ndlp->nlp_flag);

		phba->fc_stat.elsRcvRPL++;
		lpfc_els_rcv_rpl(vport, elsiocb, ndlp);
		if (newnode)
			lpfc_nlp_put(ndlp);
		break;
	case ELS_CMD_RNID:
		lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_UNSOL,
			"RCV RNID:        did:x%x/ste:x%x flg:x%x",
			did, vport->port_state, ndlp->nlp_flag);

		phba->fc_stat.elsRcvRNID++;
		lpfc_els_rcv_rnid(vport, elsiocb, ndlp);
		if (newnode)
			lpfc_nlp_put(ndlp);
		break;
	default:
		lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_UNSOL,
			"RCV ELS cmd:     cmd:x%x did:x%x/ste:x%x",
			cmd, did, vport->port_state);

		/* Unsupported ELS command, reject */
		rjt_err = LSRJT_INVALID_CMD;

		/* Unknown ELS command <elsCmd> received from NPORT <did> */
		lpfc_printf_vlog(vport, KERN_ERR, LOG_ELS,
				 "0115 Unknown ELS command x%x "
				 "received from NPORT x%x\n", cmd, did);
		if (newnode)
			lpfc_nlp_put(ndlp);
		break;
	}

	/* check if need to LS_RJT received ELS cmd */
	if (rjt_err) {
		memset(&stat, 0, sizeof(stat));
		stat.un.b.lsRjtRsnCode = rjt_err;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_NOTHING_MORE;
		lpfc_els_rsp_reject(vport, stat.un.lsRjtError, elsiocb, ndlp,
			NULL);
	}

	return;

dropit:
	if (vport && !(vport->load_flag & FC_UNLOADING))
		lpfc_printf_log(phba, KERN_ERR, LOG_ELS,
			"(%d):0111 Dropping received ELS cmd "
			"Data: x%x x%x x%x\n",
			vport->vpi, icmd->ulpStatus,
			icmd->un.ulpWord[4], icmd->ulpTimeout);
	phba->fc_stat.elsRcvDrop++;
}

static struct lpfc_vport *
lpfc_find_vport_by_vpid(struct lpfc_hba *phba, uint16_t vpi)
{
	struct lpfc_vport *vport;
	unsigned long flags;

	spin_lock_irqsave(&phba->hbalock, flags);
	list_for_each_entry(vport, &phba->port_list, listentry) {
		if (vport->vpi == vpi) {
			spin_unlock_irqrestore(&phba->hbalock, flags);
			return vport;
		}
	}
	spin_unlock_irqrestore(&phba->hbalock, flags);
	return NULL;
}

void
lpfc_els_unsol_event(struct lpfc_hba *phba, struct lpfc_sli_ring *pring,
		     struct lpfc_iocbq *elsiocb)
{
	struct lpfc_vport *vport = phba->pport;
	IOCB_t *icmd = &elsiocb->iocb;
	dma_addr_t paddr;
	struct lpfc_dmabuf *bdeBuf1 = elsiocb->context2;
	struct lpfc_dmabuf *bdeBuf2 = elsiocb->context3;

	elsiocb->context2 = NULL;
	elsiocb->context3 = NULL;

	if (icmd->ulpStatus == IOSTAT_NEED_BUFFER) {
		lpfc_sli_hbqbuf_add_hbqs(phba, LPFC_ELS_HBQ);
	} else if (icmd->ulpStatus == IOSTAT_LOCAL_REJECT &&
	    (icmd->un.ulpWord[4] & 0xff) == IOERR_RCV_BUFFER_WAITING) {
		phba->fc_stat.NoRcvBuf++;
		/* Not enough posted buffers; Try posting more buffers */
		if (!(phba->sli3_options & LPFC_SLI3_HBQ_ENABLED))
			lpfc_post_buffer(phba, pring, 0, 1);
		return;
	}

	if ((phba->sli3_options & LPFC_SLI3_NPIV_ENABLED) &&
	    (icmd->ulpCommand == CMD_IOCB_RCV_ELS64_CX ||
	     icmd->ulpCommand == CMD_IOCB_RCV_SEQ64_CX)) {
		if (icmd->unsli3.rcvsli3.vpi == 0xffff)
			vport = phba->pport;
		else {
			uint16_t vpi = icmd->unsli3.rcvsli3.vpi;
			vport = lpfc_find_vport_by_vpid(phba, vpi);
		}
	}
				/* If there are no BDEs associated
				 * with this IOCB, there is nothing to do.
				 */
	if (icmd->ulpBdeCount == 0)
		return;

				/* type of ELS cmd is first 32bit word
				 * in packet
				 */
	if (phba->sli3_options & LPFC_SLI3_HBQ_ENABLED) {
		elsiocb->context2 = bdeBuf1;
	} else {
		paddr = getPaddr(icmd->un.cont64[0].addrHigh,
				 icmd->un.cont64[0].addrLow);
		elsiocb->context2 = lpfc_sli_ringpostbuf_get(phba, pring,
							     paddr);
	}

	lpfc_els_unsol_buffer(phba, pring, vport, elsiocb);
	/*
	 * The different unsolicited event handlers would tell us
	 * if they are done with "mp" by setting context2 to NULL.
	 */
	lpfc_nlp_put(elsiocb->context1);
	elsiocb->context1 = NULL;
	if (elsiocb->context2) {
		lpfc_in_buf_free(phba, (struct lpfc_dmabuf *)elsiocb->context2);
		elsiocb->context2 = NULL;
	}

	/* RCV_ELS64_CX provide for 2 BDEs - process 2nd if included */
	if ((phba->sli3_options & LPFC_SLI3_HBQ_ENABLED) &&
	    icmd->ulpBdeCount == 2) {
		elsiocb->context2 = bdeBuf2;
		lpfc_els_unsol_buffer(phba, pring, vport, elsiocb);
		/* free mp if we are done with it */
		if (elsiocb->context2) {
			lpfc_in_buf_free(phba, elsiocb->context2);
			elsiocb->context2 = NULL;
		}
	}
}

void
lpfc_do_scr_ns_plogi(struct lpfc_hba *phba, struct lpfc_vport *vport)
{
	struct lpfc_nodelist *ndlp, *ndlp_fdmi;

	ndlp = lpfc_findnode_did(vport, NameServer_DID);
	if (!ndlp) {
		ndlp = mempool_alloc(phba->nlp_mem_pool, GFP_KERNEL);
		if (!ndlp) {
			if (phba->fc_topology == TOPOLOGY_LOOP) {
				lpfc_disc_start(vport);
				return;
			}
			lpfc_vport_set_state(vport, FC_VPORT_FAILED);
			lpfc_printf_vlog(vport, KERN_ERR, LOG_ELS,
					 "0251 NameServer login: no memory\n");
			return;
		}
		lpfc_nlp_init(vport, ndlp, NameServer_DID);
		ndlp->nlp_type |= NLP_FABRIC;
	}

	lpfc_nlp_set_state(vport, ndlp, NLP_STE_PLOGI_ISSUE);

	if (lpfc_issue_els_plogi(vport, ndlp->nlp_DID, 0)) {
		lpfc_vport_set_state(vport, FC_VPORT_FAILED);
		lpfc_printf_vlog(vport, KERN_ERR, LOG_ELS,
				 "0252 Cannot issue NameServer login\n");
		return;
	}

	if (vport->cfg_fdmi_on) {
		ndlp_fdmi = mempool_alloc(phba->nlp_mem_pool,
					  GFP_KERNEL);
		if (ndlp_fdmi) {
			lpfc_nlp_init(vport, ndlp_fdmi, FDMI_DID);
			ndlp_fdmi->nlp_type |= NLP_FABRIC;
			ndlp_fdmi->nlp_state =
				NLP_STE_PLOGI_ISSUE;
			lpfc_issue_els_plogi(vport, ndlp_fdmi->nlp_DID,
					     0);
		}
	}
	return;
}

static void
lpfc_cmpl_reg_new_vport(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmb)
{
	struct lpfc_vport *vport = pmb->vport;
	struct Scsi_Host  *shost = lpfc_shost_from_vport(vport);
	struct lpfc_nodelist *ndlp = (struct lpfc_nodelist *) pmb->context2;
	MAILBOX_t *mb = &pmb->mb;

	spin_lock_irq(shost->host_lock);
	vport->fc_flag &= ~FC_VPORT_NEEDS_REG_VPI;
	spin_unlock_irq(shost->host_lock);

	if (mb->mbxStatus) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_MBOX,
				 "0915 Register VPI failed: 0x%x\n",
				 mb->mbxStatus);

		switch (mb->mbxStatus) {
		case 0x11:	/* unsupported feature */
		case 0x9603:	/* max_vpi exceeded */
			/* giving up on vport registration */
			lpfc_vport_set_state(vport, FC_VPORT_FAILED);
			spin_lock_irq(shost->host_lock);
			vport->fc_flag &= ~(FC_FABRIC | FC_PUBLIC_LOOP);
			spin_unlock_irq(shost->host_lock);
			lpfc_can_disctmo(vport);
			break;
		default:
			/* Try to recover from this error */
			lpfc_mbx_unreg_vpi(vport);
			spin_lock_irq(shost->host_lock);
			vport->fc_flag |= FC_VPORT_NEEDS_REG_VPI;
			spin_unlock_irq(shost->host_lock);
			lpfc_initial_fdisc(vport);
			break;
		}

	} else {
		if (vport == phba->pport)
			lpfc_issue_fabric_reglogin(vport);
		else
			lpfc_do_scr_ns_plogi(phba, vport);
	}

	/* Now, we decrement the ndlp reference count held for this
	 * callback function
	 */
	lpfc_nlp_put(ndlp);

	mempool_free(pmb, phba->mbox_mem_pool);
	return;
}

static void
lpfc_register_new_vport(struct lpfc_hba *phba, struct lpfc_vport *vport,
			struct lpfc_nodelist *ndlp)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	LPFC_MBOXQ_t *mbox;

	mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (mbox) {
		lpfc_reg_vpi(phba, vport->vpi, vport->fc_myDID, mbox);
		mbox->vport = vport;
		mbox->context2 = lpfc_nlp_get(ndlp);
		mbox->mbox_cmpl = lpfc_cmpl_reg_new_vport;
		if (lpfc_sli_issue_mbox(phba, mbox, MBX_NOWAIT)
		    == MBX_NOT_FINISHED) {
			/* mailbox command not success, decrement ndlp
			 * reference count for this command
			 */
			lpfc_nlp_put(ndlp);
			mempool_free(mbox, phba->mbox_mem_pool);

			lpfc_printf_vlog(vport, KERN_ERR, LOG_MBOX,
				"0253 Register VPI: Can't send mbox\n");
			goto mbox_err_exit;
		}
	} else {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_MBOX,
				 "0254 Register VPI: no memory\n");
		goto mbox_err_exit;
	}
	return;

mbox_err_exit:
	lpfc_vport_set_state(vport, FC_VPORT_FAILED);
	spin_lock_irq(shost->host_lock);
	vport->fc_flag &= ~FC_VPORT_NEEDS_REG_VPI;
	spin_unlock_irq(shost->host_lock);
	return;
}

static void
lpfc_cmpl_els_fdisc(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
		    struct lpfc_iocbq *rspiocb)
{
	struct lpfc_vport *vport = cmdiocb->vport;
	struct Scsi_Host  *shost = lpfc_shost_from_vport(vport);
	struct lpfc_nodelist *ndlp = (struct lpfc_nodelist *) cmdiocb->context1;
	struct lpfc_nodelist *np;
	struct lpfc_nodelist *next_np;
	IOCB_t *irsp = &rspiocb->iocb;
	struct lpfc_iocbq *piocb;

	lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS,
			 "0123 FDISC completes. x%x/x%x prevDID: x%x\n",
			 irsp->ulpStatus, irsp->un.ulpWord[4],
			 vport->fc_prevDID);
	/* Since all FDISCs are being single threaded, we
	 * must reset the discovery timer for ALL vports
	 * waiting to send FDISC when one completes.
	 */
	list_for_each_entry(piocb, &phba->fabric_iocb_list, list) {
		lpfc_set_disctmo(piocb->vport);
	}

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_CMD,
		"FDISC cmpl:      status:x%x/x%x prevdid:x%x",
		irsp->ulpStatus, irsp->un.ulpWord[4], vport->fc_prevDID);

	if (irsp->ulpStatus) {
		/* Check for retry */
		if (lpfc_els_retry(phba, cmdiocb, rspiocb))
			goto out;
		/* FDISC failed */
		lpfc_printf_vlog(vport, KERN_ERR, LOG_ELS,
				 "0124 FDISC failed. (%d/%d)\n",
				 irsp->ulpStatus, irsp->un.ulpWord[4]);
		if (vport->fc_vport->vport_state == FC_VPORT_INITIALIZING)
			lpfc_vport_set_state(vport, FC_VPORT_FAILED);

		lpfc_nlp_put(ndlp);
		/* giving up on FDISC. Cancel discovery timer */
		lpfc_can_disctmo(vport);
	} else {
		spin_lock_irq(shost->host_lock);
		vport->fc_flag |= FC_FABRIC;
		if (vport->phba->fc_topology == TOPOLOGY_LOOP)
			vport->fc_flag |=  FC_PUBLIC_LOOP;
		spin_unlock_irq(shost->host_lock);

		vport->fc_myDID = irsp->un.ulpWord[4] & Mask_DID;
		lpfc_vport_set_state(vport, FC_VPORT_ACTIVE);
		if ((vport->fc_prevDID != vport->fc_myDID) &&
			!(vport->fc_flag & FC_VPORT_NEEDS_REG_VPI)) {
			/* If our NportID changed, we need to ensure all
			 * remaining NPORTs get unreg_login'ed so we can
			 * issue unreg_vpi.
			 */
			list_for_each_entry_safe(np, next_np,
				&vport->fc_nodes, nlp_listp) {
				if (np->nlp_state != NLP_STE_NPR_NODE
				   || !(np->nlp_flag & NLP_NPR_ADISC))
					continue;
				spin_lock_irq(shost->host_lock);
				np->nlp_flag &= ~NLP_NPR_ADISC;
				spin_unlock_irq(shost->host_lock);
				lpfc_unreg_rpi(vport, np);
			}
			lpfc_mbx_unreg_vpi(vport);
			spin_lock_irq(shost->host_lock);
			vport->fc_flag |= FC_VPORT_NEEDS_REG_VPI;
			spin_unlock_irq(shost->host_lock);
		}

		if (vport->fc_flag & FC_VPORT_NEEDS_REG_VPI)
			lpfc_register_new_vport(phba, vport, ndlp);
		else
			lpfc_do_scr_ns_plogi(phba, vport);

		/* Unconditionaly kick off releasing fabric node for vports */
		lpfc_nlp_put(ndlp);
	}

out:
	lpfc_els_free_iocb(phba, cmdiocb);
}

static int
lpfc_issue_els_fdisc(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
		     uint8_t retry)
{
	struct lpfc_hba *phba = vport->phba;
	IOCB_t *icmd;
	struct lpfc_iocbq *elsiocb;
	struct serv_parm *sp;
	uint8_t *pcmd;
	uint16_t cmdsize;
	int did = ndlp->nlp_DID;
	int rc;

	cmdsize = (sizeof(uint32_t) + sizeof(struct serv_parm));
	elsiocb = lpfc_prep_els_iocb(vport, 1, cmdsize, retry, ndlp, did,
				     ELS_CMD_FDISC);
	if (!elsiocb) {
		lpfc_vport_set_state(vport, FC_VPORT_FAILED);
		lpfc_printf_vlog(vport, KERN_ERR, LOG_ELS,
				 "0255 Issue FDISC: no IOCB\n");
		return 1;
	}

	icmd = &elsiocb->iocb;
	icmd->un.elsreq64.myID = 0;
	icmd->un.elsreq64.fl = 1;

	/* For FDISC, Let FDISC rsp set the NPortID for this VPI */
	icmd->ulpCt_h = 1;
	icmd->ulpCt_l = 0;

	pcmd = (uint8_t *) (((struct lpfc_dmabuf *) elsiocb->context2)->virt);
	*((uint32_t *) (pcmd)) = ELS_CMD_FDISC;
	pcmd += sizeof(uint32_t); /* CSP Word 1 */
	memcpy(pcmd, &vport->phba->pport->fc_sparam, sizeof(struct serv_parm));
	sp = (struct serv_parm *) pcmd;
	/* Setup CSPs accordingly for Fabric */
	sp->cmn.e_d_tov = 0;
	sp->cmn.w2.r_a_tov = 0;
	sp->cls1.classValid = 0;
	sp->cls2.seqDelivery = 1;
	sp->cls3.seqDelivery = 1;

	pcmd += sizeof(uint32_t); /* CSP Word 2 */
	pcmd += sizeof(uint32_t); /* CSP Word 3 */
	pcmd += sizeof(uint32_t); /* CSP Word 4 */
	pcmd += sizeof(uint32_t); /* Port Name */
	memcpy(pcmd, &vport->fc_portname, 8);
	pcmd += sizeof(uint32_t); /* Node Name */
	pcmd += sizeof(uint32_t); /* Node Name */
	memcpy(pcmd, &vport->fc_nodename, 8);

	lpfc_set_disctmo(vport);

	phba->fc_stat.elsXmitFDISC++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_fdisc;

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_CMD,
		"Issue FDISC:     did:x%x",
		did, 0, 0);

	rc = lpfc_issue_fabric_iocb(phba, elsiocb);
	if (rc == IOCB_ERROR) {
		lpfc_els_free_iocb(phba, elsiocb);
		lpfc_vport_set_state(vport, FC_VPORT_FAILED);
		lpfc_printf_vlog(vport, KERN_ERR, LOG_ELS,
				 "0256 Issue FDISC: Cannot send IOCB\n");
		return 1;
	}
	lpfc_vport_set_state(vport, FC_VPORT_INITIALIZING);
	vport->port_state = LPFC_FDISC;
	return 0;
}

static void
lpfc_cmpl_els_npiv_logo(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
			struct lpfc_iocbq *rspiocb)
{
	struct lpfc_vport *vport = cmdiocb->vport;
	IOCB_t *irsp;

	irsp = &rspiocb->iocb;
	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_CMD,
		"LOGO npiv cmpl:  status:x%x/x%x did:x%x",
		irsp->ulpStatus, irsp->un.ulpWord[4], irsp->un.rcvels.remoteID);

	lpfc_els_free_iocb(phba, cmdiocb);
	vport->unreg_vpi_cmpl = VPORT_ERROR;
}

int
lpfc_issue_els_npiv_logo(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct lpfc_hba  *phba = vport->phba;
	struct lpfc_sli_ring *pring = &phba->sli.ring[LPFC_ELS_RING];
	IOCB_t *icmd;
	struct lpfc_iocbq *elsiocb;
	uint8_t *pcmd;
	uint16_t cmdsize;

	cmdsize = 2 * sizeof(uint32_t) + sizeof(struct lpfc_name);
	elsiocb = lpfc_prep_els_iocb(vport, 1, cmdsize, 0, ndlp, ndlp->nlp_DID,
				     ELS_CMD_LOGO);
	if (!elsiocb)
		return 1;

	icmd = &elsiocb->iocb;
	pcmd = (uint8_t *) (((struct lpfc_dmabuf *) elsiocb->context2)->virt);
	*((uint32_t *) (pcmd)) = ELS_CMD_LOGO;
	pcmd += sizeof(uint32_t);

	/* Fill in LOGO payload */
	*((uint32_t *) (pcmd)) = be32_to_cpu(vport->fc_myDID);
	pcmd += sizeof(uint32_t);
	memcpy(pcmd, &vport->fc_portname, sizeof(struct lpfc_name));

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_ELS_CMD,
		"Issue LOGO npiv  did:x%x flg:x%x",
		ndlp->nlp_DID, ndlp->nlp_flag, 0);

	elsiocb->iocb_cmpl = lpfc_cmpl_els_npiv_logo;
	spin_lock_irq(shost->host_lock);
	ndlp->nlp_flag |= NLP_LOGO_SND;
	spin_unlock_irq(shost->host_lock);
	if (lpfc_sli_issue_iocb(phba, pring, elsiocb, 0) == IOCB_ERROR) {
		spin_lock_irq(shost->host_lock);
		ndlp->nlp_flag &= ~NLP_LOGO_SND;
		spin_unlock_irq(shost->host_lock);
		lpfc_els_free_iocb(phba, elsiocb);
		return 1;
	}
	return 0;
}

void
lpfc_fabric_block_timeout(unsigned long ptr)
{
	struct lpfc_hba  *phba = (struct lpfc_hba *) ptr;
	unsigned long iflags;
	uint32_t tmo_posted;
	spin_lock_irqsave(&phba->pport->work_port_lock, iflags);
	tmo_posted = phba->pport->work_port_events & WORKER_FABRIC_BLOCK_TMO;
	if (!tmo_posted)
		phba->pport->work_port_events |= WORKER_FABRIC_BLOCK_TMO;
	spin_unlock_irqrestore(&phba->pport->work_port_lock, iflags);

	if (!tmo_posted) {
		spin_lock_irqsave(&phba->hbalock, iflags);
		if (phba->work_wait)
			lpfc_worker_wake_up(phba);
		spin_unlock_irqrestore(&phba->hbalock, iflags);
	}
}

static void
lpfc_resume_fabric_iocbs(struct lpfc_hba *phba)
{
	struct lpfc_iocbq *iocb;
	unsigned long iflags;
	int ret;
	struct lpfc_sli_ring *pring = &phba->sli.ring[LPFC_ELS_RING];
	IOCB_t *cmd;

repeat:
	iocb = NULL;
	spin_lock_irqsave(&phba->hbalock, iflags);
				/* Post any pending iocb to the SLI layer */
	if (atomic_read(&phba->fabric_iocb_count) == 0) {
		list_remove_head(&phba->fabric_iocb_list, iocb, typeof(*iocb),
				 list);
		if (iocb)
			atomic_inc(&phba->fabric_iocb_count);
	}
	spin_unlock_irqrestore(&phba->hbalock, iflags);
	if (iocb) {
		iocb->fabric_iocb_cmpl = iocb->iocb_cmpl;
		iocb->iocb_cmpl = lpfc_cmpl_fabric_iocb;
		iocb->iocb_flag |= LPFC_IO_FABRIC;

		lpfc_debugfs_disc_trc(iocb->vport, LPFC_DISC_TRC_ELS_CMD,
			"Fabric sched1:   ste:x%x",
			iocb->vport->port_state, 0, 0);

		ret = lpfc_sli_issue_iocb(phba, pring, iocb, 0);

		if (ret == IOCB_ERROR) {
			iocb->iocb_cmpl = iocb->fabric_iocb_cmpl;
			iocb->fabric_iocb_cmpl = NULL;
			iocb->iocb_flag &= ~LPFC_IO_FABRIC;
			cmd = &iocb->iocb;
			cmd->ulpStatus = IOSTAT_LOCAL_REJECT;
			cmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
			iocb->iocb_cmpl(phba, iocb, iocb);

			atomic_dec(&phba->fabric_iocb_count);
			goto repeat;
		}
	}

	return;
}

void
lpfc_unblock_fabric_iocbs(struct lpfc_hba *phba)
{
	clear_bit(FABRIC_COMANDS_BLOCKED, &phba->bit_flags);

	lpfc_resume_fabric_iocbs(phba);
	return;
}

static void
lpfc_block_fabric_iocbs(struct lpfc_hba *phba)
{
	int blocked;

	blocked = test_and_set_bit(FABRIC_COMANDS_BLOCKED, &phba->bit_flags);
				/* Start a timer to unblock fabric
				 * iocbs after 100ms
				 */
	if (!blocked)
		mod_timer(&phba->fabric_block_timer, jiffies + HZ/10 );

	return;
}

static void
lpfc_cmpl_fabric_iocb(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
	struct lpfc_iocbq *rspiocb)
{
	struct ls_rjt stat;

	if ((cmdiocb->iocb_flag & LPFC_IO_FABRIC) != LPFC_IO_FABRIC)
		BUG();

	switch (rspiocb->iocb.ulpStatus) {
		case IOSTAT_NPORT_RJT:
		case IOSTAT_FABRIC_RJT:
			if (rspiocb->iocb.un.ulpWord[4] & RJT_UNAVAIL_TEMP) {
				lpfc_block_fabric_iocbs(phba);
			}
			break;

		case IOSTAT_NPORT_BSY:
		case IOSTAT_FABRIC_BSY:
			lpfc_block_fabric_iocbs(phba);
			break;

		case IOSTAT_LS_RJT:
			stat.un.lsRjtError =
				be32_to_cpu(rspiocb->iocb.un.ulpWord[4]);
			if ((stat.un.b.lsRjtRsnCode == LSRJT_UNABLE_TPC) ||
				(stat.un.b.lsRjtRsnCode == LSRJT_LOGICAL_BSY))
				lpfc_block_fabric_iocbs(phba);
			break;
	}

	if (atomic_read(&phba->fabric_iocb_count) == 0)
		BUG();

	cmdiocb->iocb_cmpl = cmdiocb->fabric_iocb_cmpl;
	cmdiocb->fabric_iocb_cmpl = NULL;
	cmdiocb->iocb_flag &= ~LPFC_IO_FABRIC;
	cmdiocb->iocb_cmpl(phba, cmdiocb, rspiocb);

	atomic_dec(&phba->fabric_iocb_count);
	if (!test_bit(FABRIC_COMANDS_BLOCKED, &phba->bit_flags)) {
				/* Post any pending iocbs to HBA */
		    lpfc_resume_fabric_iocbs(phba);
	}
}

static int
lpfc_issue_fabric_iocb(struct lpfc_hba *phba, struct lpfc_iocbq *iocb)
{
	unsigned long iflags;
	struct lpfc_sli_ring *pring = &phba->sli.ring[LPFC_ELS_RING];
	int ready;
	int ret;

	if (atomic_read(&phba->fabric_iocb_count) > 1)
		BUG();

	spin_lock_irqsave(&phba->hbalock, iflags);
	ready = atomic_read(&phba->fabric_iocb_count) == 0 &&
		!test_bit(FABRIC_COMANDS_BLOCKED, &phba->bit_flags);

	spin_unlock_irqrestore(&phba->hbalock, iflags);
	if (ready) {
		iocb->fabric_iocb_cmpl = iocb->iocb_cmpl;
		iocb->iocb_cmpl = lpfc_cmpl_fabric_iocb;
		iocb->iocb_flag |= LPFC_IO_FABRIC;

		lpfc_debugfs_disc_trc(iocb->vport, LPFC_DISC_TRC_ELS_CMD,
			"Fabric sched2:   ste:x%x",
			iocb->vport->port_state, 0, 0);

		atomic_inc(&phba->fabric_iocb_count);
		ret = lpfc_sli_issue_iocb(phba, pring, iocb, 0);

		if (ret == IOCB_ERROR) {
			iocb->iocb_cmpl = iocb->fabric_iocb_cmpl;
			iocb->fabric_iocb_cmpl = NULL;
			iocb->iocb_flag &= ~LPFC_IO_FABRIC;
			atomic_dec(&phba->fabric_iocb_count);
		}
	} else {
		spin_lock_irqsave(&phba->hbalock, iflags);
		list_add_tail(&iocb->list, &phba->fabric_iocb_list);
		spin_unlock_irqrestore(&phba->hbalock, iflags);
		ret = IOCB_SUCCESS;
	}
	return ret;
}


static void lpfc_fabric_abort_vport(struct lpfc_vport *vport)
{
	LIST_HEAD(completions);
	struct lpfc_hba  *phba = vport->phba;
	struct lpfc_iocbq *tmp_iocb, *piocb;
	IOCB_t *cmd;

	spin_lock_irq(&phba->hbalock);
	list_for_each_entry_safe(piocb, tmp_iocb, &phba->fabric_iocb_list,
				 list) {

		if (piocb->vport != vport)
			continue;

		list_move_tail(&piocb->list, &completions);
	}
	spin_unlock_irq(&phba->hbalock);

	while (!list_empty(&completions)) {
		piocb = list_get_first(&completions, struct lpfc_iocbq, list);
		list_del_init(&piocb->list);

		cmd = &piocb->iocb;
		cmd->ulpStatus = IOSTAT_LOCAL_REJECT;
		cmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
		(piocb->iocb_cmpl) (phba, piocb, piocb);
	}
}

void lpfc_fabric_abort_nport(struct lpfc_nodelist *ndlp)
{
	LIST_HEAD(completions);
	struct lpfc_hba  *phba = ndlp->vport->phba;
	struct lpfc_iocbq *tmp_iocb, *piocb;
	struct lpfc_sli_ring *pring = &phba->sli.ring[LPFC_ELS_RING];
	IOCB_t *cmd;

	spin_lock_irq(&phba->hbalock);
	list_for_each_entry_safe(piocb, tmp_iocb, &phba->fabric_iocb_list,
				 list) {
		if ((lpfc_check_sli_ndlp(phba, pring, piocb, ndlp))) {

			list_move_tail(&piocb->list, &completions);
		}
	}
	spin_unlock_irq(&phba->hbalock);

	while (!list_empty(&completions)) {
		piocb = list_get_first(&completions, struct lpfc_iocbq, list);
		list_del_init(&piocb->list);

		cmd = &piocb->iocb;
		cmd->ulpStatus = IOSTAT_LOCAL_REJECT;
		cmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
		(piocb->iocb_cmpl) (phba, piocb, piocb);
	}
}

void lpfc_fabric_abort_hba(struct lpfc_hba *phba)
{
	LIST_HEAD(completions);
	struct lpfc_iocbq *piocb;
	IOCB_t *cmd;

	spin_lock_irq(&phba->hbalock);
	list_splice_init(&phba->fabric_iocb_list, &completions);
	spin_unlock_irq(&phba->hbalock);

	while (!list_empty(&completions)) {
		piocb = list_get_first(&completions, struct lpfc_iocbq, list);
		list_del_init(&piocb->list);

		cmd = &piocb->iocb;
		cmd->ulpStatus = IOSTAT_LOCAL_REJECT;
		cmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
		(piocb->iocb_cmpl) (phba, piocb, piocb);
	}
}


#if 0
void lpfc_fabric_abort_flogi(struct lpfc_hba *phba)
{
	LIST_HEAD(completions);
	struct lpfc_iocbq *tmp_iocb, *piocb;
	IOCB_t *cmd;
	struct lpfc_nodelist *ndlp;

	spin_lock_irq(&phba->hbalock);
	list_for_each_entry_safe(piocb, tmp_iocb, &phba->fabric_iocb_list,
				 list) {

		cmd = &piocb->iocb;
		ndlp = (struct lpfc_nodelist *) piocb->context1;
		if (cmd->ulpCommand == CMD_ELS_REQUEST64_CR &&
		    ndlp != NULL &&
		    ndlp->nlp_DID == Fabric_DID)
			list_move_tail(&piocb->list, &completions);
	}
	spin_unlock_irq(&phba->hbalock);

	while (!list_empty(&completions)) {
		piocb = list_get_first(&completions, struct lpfc_iocbq, list);
		list_del_init(&piocb->list);

		cmd = &piocb->iocb;
		cmd->ulpStatus = IOSTAT_LOCAL_REJECT;
		cmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
		(piocb->iocb_cmpl) (phba, piocb, piocb);
	}
}
#endif  /*  0  */


