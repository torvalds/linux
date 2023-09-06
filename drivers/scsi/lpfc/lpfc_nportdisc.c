/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2017-2021 Broadcom. All Rights Reserved. The term *
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.     *
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
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/interrupt.h>

#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport_fc.h>
#include <scsi/fc/fc_fs.h>

#include "lpfc_hw4.h"
#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_sli4.h"
#include "lpfc_nl.h"
#include "lpfc_disc.h"
#include "lpfc.h"
#include "lpfc_scsi.h"
#include "lpfc_nvme.h"
#include "lpfc_logmsg.h"
#include "lpfc_crtn.h"
#include "lpfc_vport.h"
#include "lpfc_debugfs.h"


/* Called to verify a rcv'ed ADISC was intended for us. */
static int
lpfc_check_adisc(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
		 struct lpfc_name *nn, struct lpfc_name *pn)
{
	/* First, we MUST have a RPI registered */
	if (!(ndlp->nlp_flag & NLP_RPI_REGISTERED))
		return 0;

	/* Compare the ADISC rsp WWNN / WWPN matches our internal node
	 * table entry for that node.
	 */
	if (memcmp(nn, &ndlp->nlp_nodename, sizeof (struct lpfc_name)))
		return 0;

	if (memcmp(pn, &ndlp->nlp_portname, sizeof (struct lpfc_name)))
		return 0;

	/* we match, return success */
	return 1;
}

int
lpfc_check_sparm(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
		 struct serv_parm *sp, uint32_t class, int flogi)
{
	volatile struct serv_parm *hsp = &vport->fc_sparam;
	uint16_t hsp_value, ssp_value = 0;

	/*
	 * The receive data field size and buffer-to-buffer receive data field
	 * size entries are 16 bits but are represented as two 8-bit fields in
	 * the driver data structure to account for rsvd bits and other control
	 * bits.  Reconstruct and compare the fields as a 16-bit values before
	 * correcting the byte values.
	 */
	if (sp->cls1.classValid) {
		if (!flogi) {
			hsp_value = ((hsp->cls1.rcvDataSizeMsb << 8) |
				     hsp->cls1.rcvDataSizeLsb);
			ssp_value = ((sp->cls1.rcvDataSizeMsb << 8) |
				     sp->cls1.rcvDataSizeLsb);
			if (!ssp_value)
				goto bad_service_param;
			if (ssp_value > hsp_value) {
				sp->cls1.rcvDataSizeLsb =
					hsp->cls1.rcvDataSizeLsb;
				sp->cls1.rcvDataSizeMsb =
					hsp->cls1.rcvDataSizeMsb;
			}
		}
	} else if (class == CLASS1)
		goto bad_service_param;
	if (sp->cls2.classValid) {
		if (!flogi) {
			hsp_value = ((hsp->cls2.rcvDataSizeMsb << 8) |
				     hsp->cls2.rcvDataSizeLsb);
			ssp_value = ((sp->cls2.rcvDataSizeMsb << 8) |
				     sp->cls2.rcvDataSizeLsb);
			if (!ssp_value)
				goto bad_service_param;
			if (ssp_value > hsp_value) {
				sp->cls2.rcvDataSizeLsb =
					hsp->cls2.rcvDataSizeLsb;
				sp->cls2.rcvDataSizeMsb =
					hsp->cls2.rcvDataSizeMsb;
			}
		}
	} else if (class == CLASS2)
		goto bad_service_param;
	if (sp->cls3.classValid) {
		if (!flogi) {
			hsp_value = ((hsp->cls3.rcvDataSizeMsb << 8) |
				     hsp->cls3.rcvDataSizeLsb);
			ssp_value = ((sp->cls3.rcvDataSizeMsb << 8) |
				     sp->cls3.rcvDataSizeLsb);
			if (!ssp_value)
				goto bad_service_param;
			if (ssp_value > hsp_value) {
				sp->cls3.rcvDataSizeLsb =
					hsp->cls3.rcvDataSizeLsb;
				sp->cls3.rcvDataSizeMsb =
					hsp->cls3.rcvDataSizeMsb;
			}
		}
	} else if (class == CLASS3)
		goto bad_service_param;

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
bad_service_param:
	lpfc_printf_vlog(vport, KERN_ERR, LOG_TRACE_EVENT,
			 "0207 Device %x "
			 "(%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x) sent "
			 "invalid service parameters.  Ignoring device.\n",
			 ndlp->nlp_DID,
			 sp->nodeName.u.wwn[0], sp->nodeName.u.wwn[1],
			 sp->nodeName.u.wwn[2], sp->nodeName.u.wwn[3],
			 sp->nodeName.u.wwn[4], sp->nodeName.u.wwn[5],
			 sp->nodeName.u.wwn[6], sp->nodeName.u.wwn[7]);
	return 0;
}

static void *
lpfc_check_elscmpl_iocb(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
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
void
lpfc_els_abort(struct lpfc_hba *phba, struct lpfc_nodelist *ndlp)
{
	LIST_HEAD(abort_list);
	struct lpfc_sli_ring *pring;
	struct lpfc_iocbq *iocb, *next_iocb;

	pring = lpfc_phba_elsring(phba);

	/* In case of error recovery path, we might have a NULL pring here */
	if (unlikely(!pring))
		return;

	/* Abort outstanding I/O on NPort <nlp_DID> */
	lpfc_printf_vlog(ndlp->vport, KERN_INFO, LOG_DISCOVERY,
			 "2819 Abort outstanding I/O on NPort x%x "
			 "Data: x%x x%x x%x\n",
			 ndlp->nlp_DID, ndlp->nlp_flag, ndlp->nlp_state,
			 ndlp->nlp_rpi);
	/* Clean up all fabric IOs first.*/
	lpfc_fabric_abort_nport(ndlp);

	/*
	 * Lock the ELS ring txcmplq for SLI3/SLI4 and build a local list
	 * of all ELS IOs that need an ABTS.  The IOs need to stay on the
	 * txcmplq so that the abort operation completes them successfully.
	 */
	spin_lock_irq(&phba->hbalock);
	if (phba->sli_rev == LPFC_SLI_REV4)
		spin_lock(&pring->ring_lock);
	list_for_each_entry_safe(iocb, next_iocb, &pring->txcmplq, list) {
	/* Add to abort_list on on NDLP match. */
		if (lpfc_check_sli_ndlp(phba, pring, iocb, ndlp))
			list_add_tail(&iocb->dlist, &abort_list);
	}
	if (phba->sli_rev == LPFC_SLI_REV4)
		spin_unlock(&pring->ring_lock);
	spin_unlock_irq(&phba->hbalock);

	/* Abort the targeted IOs and remove them from the abort list. */
	list_for_each_entry_safe(iocb, next_iocb, &abort_list, dlist) {
			spin_lock_irq(&phba->hbalock);
			list_del_init(&iocb->dlist);
			lpfc_sli_issue_abort_iotag(phba, pring, iocb, NULL);
			spin_unlock_irq(&phba->hbalock);
	}
	/* Make sure HBA is alive */
	lpfc_issue_hb_tmo(phba);

	INIT_LIST_HEAD(&abort_list);

	/* Now process the txq */
	spin_lock_irq(&phba->hbalock);
	if (phba->sli_rev == LPFC_SLI_REV4)
		spin_lock(&pring->ring_lock);

	list_for_each_entry_safe(iocb, next_iocb, &pring->txq, list) {
		/* Check to see if iocb matches the nport we are looking for */
		if (lpfc_check_sli_ndlp(phba, pring, iocb, ndlp)) {
			list_del_init(&iocb->list);
			list_add_tail(&iocb->list, &abort_list);
		}
	}

	if (phba->sli_rev == LPFC_SLI_REV4)
		spin_unlock(&pring->ring_lock);
	spin_unlock_irq(&phba->hbalock);

	/* Cancel all the IOCBs from the completions list */
	lpfc_sli_cancel_iocbs(phba, &abort_list,
			      IOSTAT_LOCAL_REJECT, IOERR_SLI_ABORTED);

	lpfc_cancel_retry_delay_tmo(phba->pport, ndlp);
}

/* lpfc_defer_plogi_acc - Issue PLOGI ACC after reg_login completes
 * @phba: pointer to lpfc hba data structure.
 * @login_mbox: pointer to REG_RPI mailbox object
 *
 * The ACC for a rcv'ed PLOGI is deferred until AFTER the REG_RPI completes
 */
static void
lpfc_defer_plogi_acc(struct lpfc_hba *phba, LPFC_MBOXQ_t *login_mbox)
{
	struct lpfc_iocbq *save_iocb;
	struct lpfc_nodelist *ndlp;
	MAILBOX_t *mb = &login_mbox->u.mb;

	int rc;

	ndlp = login_mbox->ctx_ndlp;
	save_iocb = login_mbox->context3;

	if (mb->mbxStatus == MBX_SUCCESS) {
		/* Now that REG_RPI completed successfully,
		 * we can now proceed with sending the PLOGI ACC.
		 */
		rc = lpfc_els_rsp_acc(login_mbox->vport, ELS_CMD_PLOGI,
				      save_iocb, ndlp, NULL);
		if (rc) {
			lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
					"4576 PLOGI ACC fails pt2pt discovery: "
					"DID %x Data: %x\n", ndlp->nlp_DID, rc);
		}
	}

	/* Now process the REG_RPI cmpl */
	lpfc_mbx_cmpl_reg_login(phba, login_mbox);
	ndlp->nlp_flag &= ~NLP_ACC_REGLOGIN;
	kfree(save_iocb);
}

static int
lpfc_rcv_plogi(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
	       struct lpfc_iocbq *cmdiocb)
{
	struct lpfc_hba    *phba = vport->phba;
	struct lpfc_dmabuf *pcmd;
	struct lpfc_dmabuf *mp;
	uint64_t nlp_portwwn = 0;
	uint32_t *lp;
	IOCB_t *icmd;
	struct serv_parm *sp;
	uint32_t ed_tov;
	LPFC_MBOXQ_t *link_mbox;
	LPFC_MBOXQ_t *login_mbox;
	struct lpfc_iocbq *save_iocb;
	struct ls_rjt stat;
	uint32_t vid, flag;
	int rc;

	memset(&stat, 0, sizeof (struct ls_rjt));
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;
	lp = (uint32_t *) pcmd->virt;
	sp = (struct serv_parm *) ((uint8_t *) lp + sizeof (uint32_t));
	if (wwn_to_u64(sp->portName.u.wwn) == 0) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_TRACE_EVENT,
				 "0140 PLOGI Reject: invalid pname\n");
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_INVALID_PNAME;
		lpfc_els_rsp_reject(vport, stat.un.lsRjtError, cmdiocb, ndlp,
			NULL);
		return 0;
	}
	if (wwn_to_u64(sp->nodeName.u.wwn) == 0) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_TRACE_EVENT,
				 "0141 PLOGI Reject: invalid nname\n");
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_INVALID_NNAME;
		lpfc_els_rsp_reject(vport, stat.un.lsRjtError, cmdiocb, ndlp,
			NULL);
		return 0;
	}

	nlp_portwwn = wwn_to_u64(ndlp->nlp_portname.u.wwn);
	if ((lpfc_check_sparm(vport, ndlp, sp, CLASS3, 0) == 0)) {
		/* Reject this request because invalid parameters */
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_SPARM_OPTIONS;
		lpfc_els_rsp_reject(vport, stat.un.lsRjtError, cmdiocb, ndlp,
			NULL);
		return 0;
	}
	icmd = &cmdiocb->iocb;

	/* PLOGI chkparm OK */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS,
			 "0114 PLOGI chkparm OK Data: x%x x%x x%x "
			 "x%x x%x x%x\n",
			 ndlp->nlp_DID, ndlp->nlp_state, ndlp->nlp_flag,
			 ndlp->nlp_rpi, vport->port_state,
			 vport->fc_flag);

	if (vport->cfg_fcp_class == 2 && sp->cls2.classValid)
		ndlp->nlp_fcp_info |= CLASS2;
	else
		ndlp->nlp_fcp_info |= CLASS3;

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
	/* if already logged in, do implicit logout */
	switch (ndlp->nlp_state) {
	case  NLP_STE_NPR_NODE:
		if (!(ndlp->nlp_flag & NLP_NPR_ADISC))
			break;
		fallthrough;
	case  NLP_STE_REG_LOGIN_ISSUE:
	case  NLP_STE_PRLI_ISSUE:
	case  NLP_STE_UNMAPPED_NODE:
	case  NLP_STE_MAPPED_NODE:
		/* For initiators, lpfc_plogi_confirm_nport skips fabric did.
		 * For target mode, execute implicit logo.
		 * Fabric nodes go into NPR.
		 */
		if (!(ndlp->nlp_type & NLP_FABRIC) &&
		    !(phba->nvmet_support)) {
			/* Clear ndlp info, since follow up PRLI may have
			 * updated ndlp information
			 */
			ndlp->nlp_type &= ~(NLP_FCP_TARGET | NLP_FCP_INITIATOR);
			ndlp->nlp_type &= ~(NLP_NVME_TARGET | NLP_NVME_INITIATOR);
			ndlp->nlp_fcp_info &= ~NLP_FCP_2_DEVICE;
			ndlp->nlp_nvme_info &= ~NLP_NVME_NSLER;
			ndlp->nlp_flag &= ~NLP_FIRSTBURST;

			lpfc_els_rsp_acc(vport, ELS_CMD_PLOGI, cmdiocb,
					 ndlp, NULL);
			return 1;
		}
		if (nlp_portwwn != 0 &&
		    nlp_portwwn != wwn_to_u64(sp->portName.u.wwn))
			lpfc_printf_vlog(vport, KERN_ERR, LOG_TRACE_EVENT,
					 "0143 PLOGI recv'd from DID: x%x "
					 "WWPN changed: old %llx new %llx\n",
					 ndlp->nlp_DID,
					 (unsigned long long)nlp_portwwn,
					 (unsigned long long)
					 wwn_to_u64(sp->portName.u.wwn));

		/* Notify transport of connectivity loss to trigger cleanup. */
		if (phba->nvmet_support &&
		    ndlp->nlp_state == NLP_STE_UNMAPPED_NODE)
			lpfc_nvmet_invalidate_host(phba, ndlp);

		ndlp->nlp_prev_state = ndlp->nlp_state;
		/* rport needs to be unregistered first */
		lpfc_nlp_set_state(vport, ndlp, NLP_STE_NPR_NODE);
		break;
	}

	ndlp->nlp_type &= ~(NLP_FCP_TARGET | NLP_FCP_INITIATOR);
	ndlp->nlp_type &= ~(NLP_NVME_TARGET | NLP_NVME_INITIATOR);
	ndlp->nlp_fcp_info &= ~NLP_FCP_2_DEVICE;
	ndlp->nlp_nvme_info &= ~NLP_NVME_NSLER;
	ndlp->nlp_flag &= ~NLP_FIRSTBURST;

	login_mbox = NULL;
	link_mbox = NULL;
	save_iocb = NULL;

	/* Check for Nport to NPort pt2pt protocol */
	if ((vport->fc_flag & FC_PT2PT) &&
	    !(vport->fc_flag & FC_PT2PT_PLOGI)) {
		/* rcv'ed PLOGI decides what our NPortId will be */
		vport->fc_myDID = icmd->un.rcvels.parmRo;

		/* If there is an outstanding FLOGI, abort it now.
		 * The remote NPort is not going to ACC our FLOGI
		 * if its already issuing a PLOGI for pt2pt mode.
		 * This indicates our FLOGI was dropped; however, we
		 * must have ACCed the remote NPorts FLOGI to us
		 * to make it here.
		 */
		if (phba->hba_flag & HBA_FLOGI_OUTSTANDING)
			lpfc_els_abort_flogi(phba);

		ed_tov = be32_to_cpu(sp->cmn.e_d_tov);
		if (sp->cmn.edtovResolution) {
			/* E_D_TOV ticks are in nanoseconds */
			ed_tov = (phba->fc_edtov + 999999) / 1000000;
		}

		/*
		 * For pt-to-pt, use the larger EDTOV
		 * RATOV = 2 * EDTOV
		 */
		if (ed_tov > phba->fc_edtov)
			phba->fc_edtov = ed_tov;
		phba->fc_ratov = (2 * phba->fc_edtov) / 1000;

		memcpy(&phba->fc_fabparam, sp, sizeof(struct serv_parm));

		/* Issue CONFIG_LINK for SLI3 or REG_VFI for SLI4,
		 * to account for updated TOV's / parameters
		 */
		if (phba->sli_rev == LPFC_SLI_REV4)
			lpfc_issue_reg_vfi(vport);
		else {
			link_mbox = mempool_alloc(phba->mbox_mem_pool,
						  GFP_KERNEL);
			if (!link_mbox)
				goto out;
			lpfc_config_link(phba, link_mbox);
			link_mbox->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
			link_mbox->vport = vport;
			link_mbox->ctx_ndlp = ndlp;

			rc = lpfc_sli_issue_mbox(phba, link_mbox, MBX_NOWAIT);
			if (rc == MBX_NOT_FINISHED) {
				mempool_free(link_mbox, phba->mbox_mem_pool);
				goto out;
			}
		}

		lpfc_can_disctmo(vport);
	}

	ndlp->nlp_flag &= ~NLP_SUPPRESS_RSP;
	if ((phba->sli.sli_flag & LPFC_SLI_SUPPRESS_RSP) &&
	    sp->cmn.valid_vendor_ver_level) {
		vid = be32_to_cpu(sp->un.vv.vid);
		flag = be32_to_cpu(sp->un.vv.flags);
		if ((vid == LPFC_VV_EMLX_ID) && (flag & LPFC_VV_SUPPRESS_RSP))
			ndlp->nlp_flag |= NLP_SUPPRESS_RSP;
	}

	login_mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!login_mbox)
		goto out;

	save_iocb = kzalloc(sizeof(*save_iocb), GFP_KERNEL);
	if (!save_iocb)
		goto out;

	/* Save info from cmd IOCB to be used in rsp after all mbox completes */
	memcpy((uint8_t *)save_iocb, (uint8_t *)cmdiocb,
	       sizeof(struct lpfc_iocbq));

	/* Registering an existing RPI behaves differently for SLI3 vs SLI4 */
	if (phba->sli_rev == LPFC_SLI_REV4)
		lpfc_unreg_rpi(vport, ndlp);

	/* Issue REG_LOGIN first, before ACCing the PLOGI, thus we will
	 * always be deferring the ACC.
	 */
	rc = lpfc_reg_rpi(phba, vport->vpi, icmd->un.rcvels.remoteID,
			    (uint8_t *)sp, login_mbox, ndlp->nlp_rpi);
	if (rc)
		goto out;

	login_mbox->mbox_cmpl = lpfc_mbx_cmpl_reg_login;
	login_mbox->vport = vport;

	/*
	 * If there is an outstanding PLOGI issued, abort it before
	 * sending ACC rsp for received PLOGI. If pending plogi
	 * is not canceled here, the plogi will be rejected by
	 * remote port and will be retried. On a configuration with
	 * single discovery thread, this will cause a huge delay in
	 * discovery. Also this will cause multiple state machines
	 * running in parallel for this node.
	 * This only applies to a fabric environment.
	 */
	if ((ndlp->nlp_state == NLP_STE_PLOGI_ISSUE) &&
	    (vport->fc_flag & FC_FABRIC)) {
		/* software abort outstanding PLOGI */
		lpfc_els_abort(phba, ndlp);
	}

	if ((vport->port_type == LPFC_NPIV_PORT &&
	     vport->cfg_restrict_login)) {

		/* no deferred ACC */
		kfree(save_iocb);

		/* This is an NPIV SLI4 instance that does not need to register
		 * a default RPI.
		 */
		if (phba->sli_rev == LPFC_SLI_REV4) {
			mp = (struct lpfc_dmabuf *)login_mbox->ctx_buf;
			if (mp) {
				lpfc_mbuf_free(phba, mp->virt, mp->phys);
				kfree(mp);
			}
			mempool_free(login_mbox, phba->mbox_mem_pool);
			login_mbox = NULL;
		} else {
			/* In order to preserve RPIs, we want to cleanup
			 * the default RPI the firmware created to rcv
			 * this ELS request. The only way to do this is
			 * to register, then unregister the RPI.
			 */
			spin_lock_irq(&ndlp->lock);
			ndlp->nlp_flag |= (NLP_RM_DFLT_RPI | NLP_ACC_REGLOGIN |
					   NLP_RCV_PLOGI);
			spin_unlock_irq(&ndlp->lock);
		}

		stat.un.b.lsRjtRsnCode = LSRJT_INVALID_CMD;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_NOTHING_MORE;
		rc = lpfc_els_rsp_reject(vport, stat.un.lsRjtError, cmdiocb,
			ndlp, login_mbox);
		if (rc)
			mempool_free(login_mbox, phba->mbox_mem_pool);
		return 1;
	}

	/* So the order here should be:
	 * SLI3 pt2pt
	 *   Issue CONFIG_LINK mbox
	 *   CONFIG_LINK cmpl
	 * SLI4 pt2pt
	 *   Issue REG_VFI mbox
	 *   REG_VFI cmpl
	 * SLI4
	 *   Issue UNREG RPI mbx
	 *   UNREG RPI cmpl
	 * Issue REG_RPI mbox
	 * REG RPI cmpl
	 * Issue PLOGI ACC
	 * PLOGI ACC cmpl
	 */
	login_mbox->mbox_cmpl = lpfc_defer_plogi_acc;
	login_mbox->ctx_ndlp = lpfc_nlp_get(ndlp);
	login_mbox->context3 = save_iocb; /* For PLOGI ACC */

	spin_lock_irq(&ndlp->lock);
	ndlp->nlp_flag |= (NLP_ACC_REGLOGIN | NLP_RCV_PLOGI);
	spin_unlock_irq(&ndlp->lock);

	/* Start the ball rolling by issuing REG_LOGIN here */
	rc = lpfc_sli_issue_mbox(phba, login_mbox, MBX_NOWAIT);
	if (rc == MBX_NOT_FINISHED)
		goto out;
	lpfc_nlp_set_state(vport, ndlp, NLP_STE_REG_LOGIN_ISSUE);

	return 1;
out:
	kfree(save_iocb);
	if (login_mbox)
		mempool_free(login_mbox, phba->mbox_mem_pool);

	stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
	stat.un.b.lsRjtRsnCodeExp = LSEXP_OUT_OF_RESOURCE;
	lpfc_els_rsp_reject(vport, stat.un.lsRjtError, cmdiocb, ndlp, NULL);
	return 0;
}

/**
 * lpfc_mbx_cmpl_resume_rpi - Resume RPI completion routine
 * @phba: pointer to lpfc hba data structure.
 * @mboxq: pointer to mailbox object
 *
 * This routine is invoked to issue a completion to a rcv'ed
 * ADISC or PDISC after the paused RPI has been resumed.
 **/
static void
lpfc_mbx_cmpl_resume_rpi(struct lpfc_hba *phba, LPFC_MBOXQ_t *mboxq)
{
	struct lpfc_vport *vport;
	struct lpfc_iocbq *elsiocb;
	struct lpfc_nodelist *ndlp;
	uint32_t cmd;

	elsiocb = (struct lpfc_iocbq *)mboxq->ctx_buf;
	ndlp = (struct lpfc_nodelist *)mboxq->ctx_ndlp;
	vport = mboxq->vport;
	cmd = elsiocb->drvrTimeout;

	if (cmd == ELS_CMD_ADISC) {
		lpfc_els_rsp_adisc_acc(vport, elsiocb, ndlp);
	} else {
		lpfc_els_rsp_acc(vport, ELS_CMD_PLOGI, elsiocb,
			ndlp, NULL);
	}

	/* This nlp_put pairs with lpfc_sli4_resume_rpi */
	lpfc_nlp_put(ndlp);

	kfree(elsiocb);
	mempool_free(mboxq, phba->mbox_mem_pool);
}

static int
lpfc_rcv_padisc(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
		struct lpfc_iocbq *cmdiocb)
{
	struct lpfc_iocbq  *elsiocb;
	struct lpfc_dmabuf *pcmd;
	struct serv_parm   *sp;
	struct lpfc_name   *pnn, *ppn;
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
	if (icmd->ulpStatus == 0 && lpfc_check_adisc(vport, ndlp, pnn, ppn)) {

		/*
		 * As soon as  we send ACC, the remote NPort can
		 * start sending us data. Thus, for SLI4 we must
		 * resume the RPI before the ACC goes out.
		 */
		if (vport->phba->sli_rev == LPFC_SLI_REV4) {
			elsiocb = kmalloc(sizeof(struct lpfc_iocbq),
				GFP_KERNEL);
			if (elsiocb) {

				/* Save info from cmd IOCB used in rsp */
				memcpy((uint8_t *)elsiocb, (uint8_t *)cmdiocb,
					sizeof(struct lpfc_iocbq));

				/* Save the ELS cmd */
				elsiocb->drvrTimeout = cmd;

				lpfc_sli4_resume_rpi(ndlp,
					lpfc_mbx_cmpl_resume_rpi, elsiocb);
				goto out;
			}
		}

		if (cmd == ELS_CMD_ADISC) {
			lpfc_els_rsp_adisc_acc(vport, cmdiocb, ndlp);
		} else {
			lpfc_els_rsp_acc(vport, ELS_CMD_PLOGI, cmdiocb,
				ndlp, NULL);
		}
out:
		/* If we are authenticated, move to the proper state.
		 * It is possible an ADISC arrived and the remote nport
		 * is already in MAPPED or UNMAPPED state.  Catch this
		 * condition and don't set the nlp_state again because
		 * it causes an unnecessary transport unregister/register.
		 *
		 * Nodes marked for ADISC will move MAPPED or UNMAPPED state
		 * after issuing ADISC
		 */
		if (ndlp->nlp_type & (NLP_FCP_TARGET | NLP_NVME_TARGET)) {
			if ((ndlp->nlp_state != NLP_STE_MAPPED_NODE) &&
			    !(ndlp->nlp_flag & NLP_NPR_ADISC))
				lpfc_nlp_set_state(vport, ndlp,
						   NLP_STE_MAPPED_NODE);
		}

		return 1;
	}
	/* Reject this request because invalid parameters */
	stat.un.b.lsRjtRsvd0 = 0;
	stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
	stat.un.b.lsRjtRsnCodeExp = LSEXP_SPARM_OPTIONS;
	stat.un.b.vendorUnique = 0;
	lpfc_els_rsp_reject(vport, stat.un.lsRjtError, cmdiocb, ndlp, NULL);

	/* 1 sec timeout */
	mod_timer(&ndlp->nlp_delayfunc, jiffies + msecs_to_jiffies(1000));

	spin_lock_irq(&ndlp->lock);
	ndlp->nlp_flag |= NLP_DELAY_TMO;
	spin_unlock_irq(&ndlp->lock);
	ndlp->nlp_last_elscmd = ELS_CMD_PLOGI;
	ndlp->nlp_prev_state = ndlp->nlp_state;
	lpfc_nlp_set_state(vport, ndlp, NLP_STE_NPR_NODE);
	return 0;
}

static int
lpfc_rcv_logo(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
	      struct lpfc_iocbq *cmdiocb, uint32_t els_cmd)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct lpfc_hba    *phba = vport->phba;
	struct lpfc_vport **vports;
	int i, active_vlink_present = 0 ;

	/* Put ndlp in NPR state with 1 sec timeout for plogi, ACC logo */
	/* Only call LOGO ACC for first LOGO, this avoids sending unnecessary
	 * PLOGIs during LOGO storms from a device.
	 */
	spin_lock_irq(&ndlp->lock);
	ndlp->nlp_flag |= NLP_LOGO_ACC;
	spin_unlock_irq(&ndlp->lock);
	if (els_cmd == ELS_CMD_PRLO)
		lpfc_els_rsp_acc(vport, ELS_CMD_PRLO, cmdiocb, ndlp, NULL);
	else
		lpfc_els_rsp_acc(vport, ELS_CMD_ACC, cmdiocb, ndlp, NULL);

	/* This clause allows the initiator to ACC the LOGO back to the
	 * Fabric Domain Controller.  It does deliberately skip all other
	 * steps because some fabrics send RDP requests after logging out
	 * from the initiator.
	 */
	if (ndlp->nlp_type & NLP_FABRIC &&
	    ((ndlp->nlp_DID & WELL_KNOWN_DID_MASK) != WELL_KNOWN_DID_MASK))
		return 0;

	/* Notify transport of connectivity loss to trigger cleanup. */
	if (phba->nvmet_support &&
	    ndlp->nlp_state == NLP_STE_UNMAPPED_NODE)
		lpfc_nvmet_invalidate_host(phba, ndlp);

	if (ndlp->nlp_DID == Fabric_DID) {
		if (vport->port_state <= LPFC_FDISC ||
		    vport->fc_flag & FC_PT2PT)
			goto out;
		lpfc_linkdown_port(vport);
		spin_lock_irq(shost->host_lock);
		vport->fc_flag |= FC_VPORT_LOGO_RCVD;
		spin_unlock_irq(shost->host_lock);
		vports = lpfc_create_vport_work_array(phba);
		if (vports) {
			for (i = 0; i <= phba->max_vports && vports[i] != NULL;
					i++) {
				if ((!(vports[i]->fc_flag &
					FC_VPORT_LOGO_RCVD)) &&
					(vports[i]->port_state > LPFC_FDISC)) {
					active_vlink_present = 1;
					break;
				}
			}
			lpfc_destroy_vport_work_array(phba, vports);
		}

		/*
		 * Don't re-instantiate if vport is marked for deletion.
		 * If we are here first then vport_delete is going to wait
		 * for discovery to complete.
		 */
		if (!(vport->load_flag & FC_UNLOADING) &&
					active_vlink_present) {
			/*
			 * If there are other active VLinks present,
			 * re-instantiate the Vlink using FDISC.
			 */
			mod_timer(&ndlp->nlp_delayfunc,
				  jiffies + msecs_to_jiffies(1000));
			spin_lock_irq(&ndlp->lock);
			ndlp->nlp_flag |= NLP_DELAY_TMO;
			spin_unlock_irq(&ndlp->lock);
			ndlp->nlp_last_elscmd = ELS_CMD_FDISC;
			vport->port_state = LPFC_FDISC;
		} else {
			spin_lock_irq(shost->host_lock);
			phba->pport->fc_flag &= ~FC_LOGO_RCVD_DID_CHNG;
			spin_unlock_irq(shost->host_lock);
			lpfc_retry_pport_discovery(phba);
		}
	} else if ((!(ndlp->nlp_type & NLP_FABRIC) &&
		((ndlp->nlp_type & NLP_FCP_TARGET) ||
		(ndlp->nlp_type & NLP_NVME_TARGET) ||
		(vport->fc_flag & FC_PT2PT))) ||
		(ndlp->nlp_state == NLP_STE_ADISC_ISSUE)) {
		/* Only try to re-login if this is NOT a Fabric Node
		 * AND the remote NPORT is a FCP/NVME Target or we
		 * are in pt2pt mode. NLP_STE_ADISC_ISSUE is a special
		 * case for LOGO as a response to ADISC behavior.
		 */
		mod_timer(&ndlp->nlp_delayfunc,
			  jiffies + msecs_to_jiffies(1000 * 1));
		spin_lock_irq(&ndlp->lock);
		ndlp->nlp_flag |= NLP_DELAY_TMO;
		spin_unlock_irq(&ndlp->lock);

		ndlp->nlp_last_elscmd = ELS_CMD_PLOGI;
	}
out:
	/* Unregister from backend, could have been skipped due to ADISC */
	lpfc_nlp_unreg_node(vport, ndlp);

	ndlp->nlp_prev_state = ndlp->nlp_state;
	lpfc_nlp_set_state(vport, ndlp, NLP_STE_NPR_NODE);

	spin_lock_irq(&ndlp->lock);
	ndlp->nlp_flag &= ~NLP_NPR_ADISC;
	spin_unlock_irq(&ndlp->lock);
	/* The driver has to wait until the ACC completes before it continues
	 * processing the LOGO.  The action will resume in
	 * lpfc_cmpl_els_logo_acc routine. Since part of processing includes an
	 * unreg_login, the driver waits so the ACC does not get aborted.
	 */
	return 0;
}

static uint32_t
lpfc_rcv_prli_support_check(struct lpfc_vport *vport,
			    struct lpfc_nodelist *ndlp,
			    struct lpfc_iocbq *cmdiocb)
{
	struct ls_rjt stat;
	uint32_t *payload;
	uint32_t cmd;

	payload = ((struct lpfc_dmabuf *)cmdiocb->context2)->virt;
	cmd = *payload;
	if (vport->phba->nvmet_support) {
		/* Must be a NVME PRLI */
		if (cmd ==  ELS_CMD_PRLI)
			goto out;
	} else {
		/* Initiator mode. */
		if (!vport->nvmei_support && (cmd == ELS_CMD_NVMEPRLI))
			goto out;
	}
	return 1;
out:
	lpfc_printf_vlog(vport, KERN_WARNING, LOG_NVME_DISC,
			 "6115 Rcv PRLI (%x) check failed: ndlp rpi %d "
			 "state x%x flags x%x\n",
			 cmd, ndlp->nlp_rpi, ndlp->nlp_state,
			 ndlp->nlp_flag);
	memset(&stat, 0, sizeof(struct ls_rjt));
	stat.un.b.lsRjtRsnCode = LSRJT_CMD_UNSUPPORTED;
	stat.un.b.lsRjtRsnCodeExp = LSEXP_REQ_UNSUPPORTED;
	lpfc_els_rsp_reject(vport, stat.un.lsRjtError, cmdiocb,
			    ndlp, NULL);
	return 0;
}

static void
lpfc_rcv_prli(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
	      struct lpfc_iocbq *cmdiocb)
{
	struct lpfc_hba  *phba = vport->phba;
	struct lpfc_dmabuf *pcmd;
	uint32_t *lp;
	PRLI *npr;
	struct fc_rport *rport = ndlp->rport;
	u32 roles;

	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;
	lp = (uint32_t *) pcmd->virt;
	npr = (PRLI *) ((uint8_t *) lp + sizeof (uint32_t));

	if ((npr->prliType == PRLI_FCP_TYPE) ||
	    (npr->prliType == PRLI_NVME_TYPE)) {
		if (npr->initiatorFunc) {
			if (npr->prliType == PRLI_FCP_TYPE)
				ndlp->nlp_type |= NLP_FCP_INITIATOR;
			if (npr->prliType == PRLI_NVME_TYPE)
				ndlp->nlp_type |= NLP_NVME_INITIATOR;
		}
		if (npr->targetFunc) {
			if (npr->prliType == PRLI_FCP_TYPE)
				ndlp->nlp_type |= NLP_FCP_TARGET;
			if (npr->prliType == PRLI_NVME_TYPE)
				ndlp->nlp_type |= NLP_NVME_TARGET;
			if (npr->writeXferRdyDis)
				ndlp->nlp_flag |= NLP_FIRSTBURST;
		}
		if (npr->Retry && ndlp->nlp_type &
					(NLP_FCP_INITIATOR | NLP_FCP_TARGET))
			ndlp->nlp_fcp_info |= NLP_FCP_2_DEVICE;

		if (npr->Retry && phba->nsler &&
		    ndlp->nlp_type & (NLP_NVME_INITIATOR | NLP_NVME_TARGET))
			ndlp->nlp_nvme_info |= NLP_NVME_NSLER;


		/* If this driver is in nvme target mode, set the ndlp's fc4
		 * type to NVME provided the PRLI response claims NVME FC4
		 * type.  Target mode does not issue gft_id so doesn't get
		 * the fc4 type set until now.
		 */
		if (phba->nvmet_support && (npr->prliType == PRLI_NVME_TYPE)) {
			ndlp->nlp_fc4_type |= NLP_FC4_NVME;
			lpfc_nlp_set_state(vport, ndlp, NLP_STE_UNMAPPED_NODE);
		}

		/* Fabric Controllers send FCP PRLI as an initiator but should
		 * not get recognized as FCP type and registered with transport.
		 */
		if (npr->prliType == PRLI_FCP_TYPE &&
		    !(ndlp->nlp_type & NLP_FABRIC))
			ndlp->nlp_fc4_type |= NLP_FC4_FCP;
	}
	if (rport) {
		/* We need to update the rport role values */
		roles = FC_RPORT_ROLE_UNKNOWN;
		if (ndlp->nlp_type & NLP_FCP_INITIATOR)
			roles |= FC_RPORT_ROLE_FCP_INITIATOR;
		if (ndlp->nlp_type & NLP_FCP_TARGET)
			roles |= FC_RPORT_ROLE_FCP_TARGET;

		lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_RPORT,
			"rport rolechg:   role:x%x did:x%x flg:x%x",
			roles, ndlp->nlp_DID, ndlp->nlp_flag);

		if (vport->cfg_enable_fc4_type != LPFC_ENABLE_NVME)
			fc_remote_port_rolechg(rport, roles);
	}
}

static uint32_t
lpfc_disc_set_adisc(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp)
{
	if (!(ndlp->nlp_flag & NLP_RPI_REGISTERED)) {
		spin_lock_irq(&ndlp->lock);
		ndlp->nlp_flag &= ~NLP_NPR_ADISC;
		spin_unlock_irq(&ndlp->lock);
		return 0;
	}

	if (!(vport->fc_flag & FC_PT2PT)) {
		/* Check config parameter use-adisc or FCP-2 */
		if (vport->cfg_use_adisc && ((vport->fc_flag & FC_RSCN_MODE) ||
		    ((ndlp->nlp_fcp_info & NLP_FCP_2_DEVICE) &&
		     (ndlp->nlp_type & NLP_FCP_TARGET)))) {
			spin_lock_irq(&ndlp->lock);
			ndlp->nlp_flag |= NLP_NPR_ADISC;
			spin_unlock_irq(&ndlp->lock);
			return 1;
		}
	}

	spin_lock_irq(&ndlp->lock);
	ndlp->nlp_flag &= ~NLP_NPR_ADISC;
	spin_unlock_irq(&ndlp->lock);
	lpfc_unreg_rpi(vport, ndlp);
	return 0;
}

/**
 * lpfc_release_rpi - Release a RPI by issuing unreg_login mailbox cmd.
 * @phba : Pointer to lpfc_hba structure.
 * @vport: Pointer to lpfc_vport structure.
 * @ndlp: Pointer to lpfc_nodelist structure.
 * @rpi  : rpi to be release.
 *
 * This function will send a unreg_login mailbox command to the firmware
 * to release a rpi.
 **/
static void
lpfc_release_rpi(struct lpfc_hba *phba, struct lpfc_vport *vport,
		 struct lpfc_nodelist *ndlp, uint16_t rpi)
{
	LPFC_MBOXQ_t *pmb;
	int rc;

	/* If there is already an UNREG in progress for this ndlp,
	 * no need to queue up another one.
	 */
	if (ndlp->nlp_flag & NLP_UNREG_INP) {
		lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
				 "1435 release_rpi SKIP UNREG x%x on "
				 "NPort x%x deferred x%x  flg x%x "
				 "Data: x%px\n",
				 ndlp->nlp_rpi, ndlp->nlp_DID,
				 ndlp->nlp_defer_did,
				 ndlp->nlp_flag, ndlp);
		return;
	}

	pmb = (LPFC_MBOXQ_t *) mempool_alloc(phba->mbox_mem_pool,
			GFP_KERNEL);
	if (!pmb)
		lpfc_printf_vlog(vport, KERN_ERR, LOG_TRACE_EVENT,
				 "2796 mailbox memory allocation failed \n");
	else {
		lpfc_unreg_login(phba, vport->vpi, rpi, pmb);
		pmb->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
		pmb->vport = vport;
		pmb->ctx_ndlp = lpfc_nlp_get(ndlp);
		if (!pmb->ctx_ndlp) {
			mempool_free(pmb, phba->mbox_mem_pool);
			return;
		}

		if (((ndlp->nlp_DID & Fabric_DID_MASK) != Fabric_DID_MASK) &&
		    (!(vport->fc_flag & FC_OFFLINE_MODE)))
			ndlp->nlp_flag |= NLP_UNREG_INP;

		lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
				 "1437 release_rpi UNREG x%x "
				 "on NPort x%x flg x%x\n",
				 ndlp->nlp_rpi, ndlp->nlp_DID, ndlp->nlp_flag);

		rc = lpfc_sli_issue_mbox(phba, pmb, MBX_NOWAIT);
		if (rc == MBX_NOT_FINISHED)
			mempool_free(pmb, phba->mbox_mem_pool);
	}
}

static uint32_t
lpfc_disc_illegal(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
		  void *arg, uint32_t evt)
{
	struct lpfc_hba *phba;
	LPFC_MBOXQ_t *pmb = (LPFC_MBOXQ_t *) arg;
	uint16_t rpi;

	phba = vport->phba;
	/* Release the RPI if reglogin completing */
	if (!(phba->pport->load_flag & FC_UNLOADING) &&
		(evt == NLP_EVT_CMPL_REG_LOGIN) &&
		(!pmb->u.mb.mbxStatus)) {
		rpi = pmb->u.mb.un.varWords[0];
		lpfc_release_rpi(phba, vport, ndlp, rpi);
	}
	lpfc_printf_vlog(vport, KERN_ERR, LOG_TRACE_EVENT,
			 "0271 Illegal State Transition: node x%x "
			 "event x%x, state x%x Data: x%x x%x\n",
			 ndlp->nlp_DID, evt, ndlp->nlp_state, ndlp->nlp_rpi,
			 ndlp->nlp_flag);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_cmpl_plogi_illegal(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
		  void *arg, uint32_t evt)
{
	/* This transition is only legal if we previously
	 * rcv'ed a PLOGI. Since we don't want 2 discovery threads
	 * working on the same NPortID, do nothing for this thread
	 * to stop it.
	 */
	if (!(ndlp->nlp_flag & NLP_RCV_PLOGI)) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_TRACE_EVENT,
				 "0272 Illegal State Transition: node x%x "
				 "event x%x, state x%x Data: x%x x%x\n",
				  ndlp->nlp_DID, evt, ndlp->nlp_state,
				  ndlp->nlp_rpi, ndlp->nlp_flag);
	}
	return ndlp->nlp_state;
}

/* Start of Discovery State Machine routines */

static uint32_t
lpfc_rcv_plogi_unused_node(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			   void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;

	if (lpfc_rcv_plogi(vport, ndlp, cmdiocb)) {
		return ndlp->nlp_state;
	}
	return NLP_STE_FREED_NODE;
}

static uint32_t
lpfc_rcv_els_unused_node(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			 void *arg, uint32_t evt)
{
	lpfc_issue_els_logo(vport, ndlp, 0);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_logo_unused_node(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			  void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb = (struct lpfc_iocbq *) arg;

	spin_lock_irq(&ndlp->lock);
	ndlp->nlp_flag |= NLP_LOGO_ACC;
	spin_unlock_irq(&ndlp->lock);
	lpfc_els_rsp_acc(vport, ELS_CMD_ACC, cmdiocb, ndlp, NULL);

	return ndlp->nlp_state;
}

static uint32_t
lpfc_cmpl_logo_unused_node(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			   void *arg, uint32_t evt)
{
	return NLP_STE_FREED_NODE;
}

static uint32_t
lpfc_device_rm_unused_node(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			   void *arg, uint32_t evt)
{
	return NLP_STE_FREED_NODE;
}

static uint32_t
lpfc_device_recov_unused_node(struct lpfc_vport *vport,
			struct lpfc_nodelist *ndlp,
			   void *arg, uint32_t evt)
{
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_plogi_plogi_issue(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			   void *arg, uint32_t evt)
{
	struct Scsi_Host   *shost = lpfc_shost_from_vport(vport);
	struct lpfc_hba   *phba = vport->phba;
	struct lpfc_iocbq *cmdiocb = arg;
	struct lpfc_dmabuf *pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;
	uint32_t *lp = (uint32_t *) pcmd->virt;
	struct serv_parm *sp = (struct serv_parm *) (lp + 1);
	struct ls_rjt stat;
	int port_cmp;

	memset(&stat, 0, sizeof (struct ls_rjt));

	/* For a PLOGI, we only accept if our portname is less
	 * than the remote portname.
	 */
	phba->fc_stat.elsLogiCol++;
	port_cmp = memcmp(&vport->fc_portname, &sp->portName,
			  sizeof(struct lpfc_name));

	if (port_cmp >= 0) {
		/* Reject this request because the remote node will accept
		   ours */
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_CMD_IN_PROGRESS;
		lpfc_els_rsp_reject(vport, stat.un.lsRjtError, cmdiocb, ndlp,
			NULL);
	} else {
		if (lpfc_rcv_plogi(vport, ndlp, cmdiocb) &&
		    (ndlp->nlp_flag & NLP_NPR_2B_DISC) &&
		    (vport->num_disc_nodes)) {
			spin_lock_irq(&ndlp->lock);
			ndlp->nlp_flag &= ~NLP_NPR_2B_DISC;
			spin_unlock_irq(&ndlp->lock);
			/* Check if there are more PLOGIs to be sent */
			lpfc_more_plogi(vport);
			if (vport->num_disc_nodes == 0) {
				spin_lock_irq(shost->host_lock);
				vport->fc_flag &= ~FC_NDISC_ACTIVE;
				spin_unlock_irq(shost->host_lock);
				lpfc_can_disctmo(vport);
				lpfc_end_rscn(vport);
			}
		}
	} /* If our portname was less */

	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_prli_plogi_issue(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			  void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb = (struct lpfc_iocbq *) arg;
	struct ls_rjt     stat;

	memset(&stat, 0, sizeof (struct ls_rjt));
	stat.un.b.lsRjtRsnCode = LSRJT_LOGICAL_BSY;
	stat.un.b.lsRjtRsnCodeExp = LSEXP_NOTHING_MORE;
	lpfc_els_rsp_reject(vport, stat.un.lsRjtError, cmdiocb, ndlp, NULL);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_logo_plogi_issue(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			  void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb = (struct lpfc_iocbq *) arg;

	/* Retrieve RPI from LOGO IOCB. RPI is used for CMD_ABORT_XRI_CN */
	if (vport->phba->sli_rev == LPFC_SLI_REV3)
		ndlp->nlp_rpi = cmdiocb->iocb.ulpIoTag;
				/* software abort outstanding PLOGI */
	lpfc_els_abort(vport->phba, ndlp);

	lpfc_rcv_logo(vport, ndlp, cmdiocb, ELS_CMD_LOGO);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_els_plogi_issue(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			 void *arg, uint32_t evt)
{
	struct lpfc_hba   *phba = vport->phba;
	struct lpfc_iocbq *cmdiocb = (struct lpfc_iocbq *) arg;

	/* software abort outstanding PLOGI */
	lpfc_els_abort(phba, ndlp);

	if (evt == NLP_EVT_RCV_LOGO) {
		lpfc_els_rsp_acc(vport, ELS_CMD_ACC, cmdiocb, ndlp, NULL);
	} else {
		lpfc_issue_els_logo(vport, ndlp, 0);
	}

	/* Put ndlp in npr state set plogi timer for 1 sec */
	mod_timer(&ndlp->nlp_delayfunc, jiffies + msecs_to_jiffies(1000 * 1));
	spin_lock_irq(&ndlp->lock);
	ndlp->nlp_flag |= NLP_DELAY_TMO;
	spin_unlock_irq(&ndlp->lock);
	ndlp->nlp_last_elscmd = ELS_CMD_PLOGI;
	ndlp->nlp_prev_state = NLP_STE_PLOGI_ISSUE;
	lpfc_nlp_set_state(vport, ndlp, NLP_STE_NPR_NODE);

	return ndlp->nlp_state;
}

static uint32_t
lpfc_cmpl_plogi_plogi_issue(struct lpfc_vport *vport,
			    struct lpfc_nodelist *ndlp,
			    void *arg,
			    uint32_t evt)
{
	struct lpfc_hba    *phba = vport->phba;
	struct lpfc_iocbq  *cmdiocb, *rspiocb;
	struct lpfc_dmabuf *pcmd, *prsp, *mp;
	uint32_t *lp;
	uint32_t vid, flag;
	IOCB_t *irsp;
	struct serv_parm *sp;
	uint32_t ed_tov;
	LPFC_MBOXQ_t *mbox;
	int rc;

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

	prsp = list_get_first(&pcmd->list, struct lpfc_dmabuf, list);
	if (!prsp)
		goto out;

	lp = (uint32_t *) prsp->virt;
	sp = (struct serv_parm *) ((uint8_t *) lp + sizeof (uint32_t));

	/* Some switches have FDMI servers returning 0 for WWN */
	if ((ndlp->nlp_DID != FDMI_DID) &&
		(wwn_to_u64(sp->portName.u.wwn) == 0 ||
		wwn_to_u64(sp->nodeName.u.wwn) == 0)) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_TRACE_EVENT,
				 "0142 PLOGI RSP: Invalid WWN.\n");
		goto out;
	}
	if (!lpfc_check_sparm(vport, ndlp, sp, CLASS3, 0))
		goto out;
	/* PLOGI chkparm OK */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS,
			 "0121 PLOGI chkparm OK Data: x%x x%x x%x x%x\n",
			 ndlp->nlp_DID, ndlp->nlp_state,
			 ndlp->nlp_flag, ndlp->nlp_rpi);
	if (vport->cfg_fcp_class == 2 && (sp->cls2.classValid))
		ndlp->nlp_fcp_info |= CLASS2;
	else
		ndlp->nlp_fcp_info |= CLASS3;

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

	if ((vport->fc_flag & FC_PT2PT) &&
	    (vport->fc_flag & FC_PT2PT_PLOGI)) {
		ed_tov = be32_to_cpu(sp->cmn.e_d_tov);
		if (sp->cmn.edtovResolution) {
			/* E_D_TOV ticks are in nanoseconds */
			ed_tov = (phba->fc_edtov + 999999) / 1000000;
		}

		ndlp->nlp_flag &= ~NLP_SUPPRESS_RSP;
		if ((phba->sli.sli_flag & LPFC_SLI_SUPPRESS_RSP) &&
		    sp->cmn.valid_vendor_ver_level) {
			vid = be32_to_cpu(sp->un.vv.vid);
			flag = be32_to_cpu(sp->un.vv.flags);
			if ((vid == LPFC_VV_EMLX_ID) &&
			    (flag & LPFC_VV_SUPPRESS_RSP))
				ndlp->nlp_flag |= NLP_SUPPRESS_RSP;
		}

		/*
		 * Use the larger EDTOV
		 * RATOV = 2 * EDTOV for pt-to-pt
		 */
		if (ed_tov > phba->fc_edtov)
			phba->fc_edtov = ed_tov;
		phba->fc_ratov = (2 * phba->fc_edtov) / 1000;

		memcpy(&phba->fc_fabparam, sp, sizeof(struct serv_parm));

		/* Issue config_link / reg_vfi to account for updated TOV's */
		if (phba->sli_rev == LPFC_SLI_REV4) {
			lpfc_issue_reg_vfi(vport);
		} else {
			mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
			if (!mbox) {
				lpfc_printf_vlog(vport, KERN_ERR,
						 LOG_TRACE_EVENT,
						 "0133 PLOGI: no memory "
						 "for config_link "
						 "Data: x%x x%x x%x x%x\n",
						 ndlp->nlp_DID, ndlp->nlp_state,
						 ndlp->nlp_flag, ndlp->nlp_rpi);
				goto out;
			}

			lpfc_config_link(phba, mbox);

			mbox->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
			mbox->vport = vport;
			rc = lpfc_sli_issue_mbox(phba, mbox, MBX_NOWAIT);
			if (rc == MBX_NOT_FINISHED) {
				mempool_free(mbox, phba->mbox_mem_pool);
				goto out;
			}
		}
	}

	lpfc_unreg_rpi(vport, ndlp);

	mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!mbox) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_TRACE_EVENT,
				 "0018 PLOGI: no memory for reg_login "
				 "Data: x%x x%x x%x x%x\n",
				 ndlp->nlp_DID, ndlp->nlp_state,
				 ndlp->nlp_flag, ndlp->nlp_rpi);
		goto out;
	}

	if (lpfc_reg_rpi(phba, vport->vpi, irsp->un.elsreq64.remoteID,
			 (uint8_t *) sp, mbox, ndlp->nlp_rpi) == 0) {
		switch (ndlp->nlp_DID) {
		case NameServer_DID:
			mbox->mbox_cmpl = lpfc_mbx_cmpl_ns_reg_login;
			/* Fabric Controller Node needs these parameters. */
			memcpy(&ndlp->fc_sparam, sp, sizeof(struct serv_parm));
			break;
		case FDMI_DID:
			mbox->mbox_cmpl = lpfc_mbx_cmpl_fdmi_reg_login;
			break;
		default:
			ndlp->nlp_flag |= NLP_REG_LOGIN_SEND;
			mbox->mbox_cmpl = lpfc_mbx_cmpl_reg_login;
		}

		mbox->ctx_ndlp = lpfc_nlp_get(ndlp);
		if (!mbox->ctx_ndlp)
			goto out;

		mbox->vport = vport;
		if (lpfc_sli_issue_mbox(phba, mbox, MBX_NOWAIT)
		    != MBX_NOT_FINISHED) {
			lpfc_nlp_set_state(vport, ndlp,
					   NLP_STE_REG_LOGIN_ISSUE);
			return ndlp->nlp_state;
		}
		if (ndlp->nlp_flag & NLP_REG_LOGIN_SEND)
			ndlp->nlp_flag &= ~NLP_REG_LOGIN_SEND;
		/* decrement node reference count to the failed mbox
		 * command
		 */
		lpfc_nlp_put(ndlp);
		mp = (struct lpfc_dmabuf *)mbox->ctx_buf;
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		kfree(mp);
		mempool_free(mbox, phba->mbox_mem_pool);

		lpfc_printf_vlog(vport, KERN_ERR, LOG_TRACE_EVENT,
				 "0134 PLOGI: cannot issue reg_login "
				 "Data: x%x x%x x%x x%x\n",
				 ndlp->nlp_DID, ndlp->nlp_state,
				 ndlp->nlp_flag, ndlp->nlp_rpi);
	} else {
		mempool_free(mbox, phba->mbox_mem_pool);

		lpfc_printf_vlog(vport, KERN_ERR, LOG_TRACE_EVENT,
				 "0135 PLOGI: cannot format reg_login "
				 "Data: x%x x%x x%x x%x\n",
				 ndlp->nlp_DID, ndlp->nlp_state,
				 ndlp->nlp_flag, ndlp->nlp_rpi);
	}


out:
	if (ndlp->nlp_DID == NameServer_DID) {
		lpfc_vport_set_state(vport, FC_VPORT_FAILED);
		lpfc_printf_vlog(vport, KERN_ERR, LOG_TRACE_EVENT,
				 "0261 Cannot Register NameServer login\n");
	}

	/*
	** In case the node reference counter does not go to zero, ensure that
	** the stale state for the node is not processed.
	*/

	ndlp->nlp_prev_state = ndlp->nlp_state;
	lpfc_nlp_set_state(vport, ndlp, NLP_STE_NPR_NODE);
	return NLP_STE_FREED_NODE;
}

static uint32_t
lpfc_cmpl_logo_plogi_issue(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			   void *arg, uint32_t evt)
{
	return ndlp->nlp_state;
}

static uint32_t
lpfc_cmpl_reglogin_plogi_issue(struct lpfc_vport *vport,
	struct lpfc_nodelist *ndlp, void *arg, uint32_t evt)
{
	struct lpfc_hba *phba;
	LPFC_MBOXQ_t *pmb = (LPFC_MBOXQ_t *) arg;
	MAILBOX_t *mb = &pmb->u.mb;
	uint16_t rpi;

	phba = vport->phba;
	/* Release the RPI */
	if (!(phba->pport->load_flag & FC_UNLOADING) &&
		!mb->mbxStatus) {
		rpi = pmb->u.mb.un.varWords[0];
		lpfc_release_rpi(phba, vport, ndlp, rpi);
	}
	return ndlp->nlp_state;
}

static uint32_t
lpfc_device_rm_plogi_issue(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			   void *arg, uint32_t evt)
{
	if (ndlp->nlp_flag & NLP_NPR_2B_DISC) {
		spin_lock_irq(&ndlp->lock);
		ndlp->nlp_flag |= NLP_NODEV_REMOVE;
		spin_unlock_irq(&ndlp->lock);
		return ndlp->nlp_state;
	} else {
		/* software abort outstanding PLOGI */
		lpfc_els_abort(vport->phba, ndlp);

		lpfc_drop_node(vport, ndlp);
		return NLP_STE_FREED_NODE;
	}
}

static uint32_t
lpfc_device_recov_plogi_issue(struct lpfc_vport *vport,
			      struct lpfc_nodelist *ndlp,
			      void *arg,
			      uint32_t evt)
{
	struct lpfc_hba  *phba = vport->phba;

	/* Don't do anything that will mess up processing of the
	 * previous RSCN.
	 */
	if (vport->fc_flag & FC_RSCN_DEFERRED)
		return ndlp->nlp_state;

	/* software abort outstanding PLOGI */
	lpfc_els_abort(phba, ndlp);

	ndlp->nlp_prev_state = NLP_STE_PLOGI_ISSUE;
	lpfc_nlp_set_state(vport, ndlp, NLP_STE_NPR_NODE);
	spin_lock_irq(&ndlp->lock);
	ndlp->nlp_flag &= ~(NLP_NODEV_REMOVE | NLP_NPR_2B_DISC);
	spin_unlock_irq(&ndlp->lock);

	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_plogi_adisc_issue(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			   void *arg, uint32_t evt)
{
	struct lpfc_hba   *phba = vport->phba;
	struct lpfc_iocbq *cmdiocb;

	/* software abort outstanding ADISC */
	lpfc_els_abort(phba, ndlp);

	cmdiocb = (struct lpfc_iocbq *) arg;

	if (lpfc_rcv_plogi(vport, ndlp, cmdiocb)) {
		if (ndlp->nlp_flag & NLP_NPR_2B_DISC) {
			spin_lock_irq(&ndlp->lock);
			ndlp->nlp_flag &= ~NLP_NPR_2B_DISC;
			spin_unlock_irq(&ndlp->lock);
			if (vport->num_disc_nodes)
				lpfc_more_adisc(vport);
		}
		return ndlp->nlp_state;
	}
	ndlp->nlp_prev_state = NLP_STE_ADISC_ISSUE;
	lpfc_issue_els_plogi(vport, ndlp->nlp_DID, 0);
	lpfc_nlp_set_state(vport, ndlp, NLP_STE_PLOGI_ISSUE);

	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_prli_adisc_issue(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			  void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb = (struct lpfc_iocbq *) arg;

	if (lpfc_rcv_prli_support_check(vport, ndlp, cmdiocb))
		lpfc_els_rsp_prli_acc(vport, cmdiocb, ndlp);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_logo_adisc_issue(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			  void *arg, uint32_t evt)
{
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_iocbq *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;

	/* software abort outstanding ADISC */
	lpfc_els_abort(phba, ndlp);

	lpfc_rcv_logo(vport, ndlp, cmdiocb, ELS_CMD_LOGO);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_padisc_adisc_issue(struct lpfc_vport *vport,
			    struct lpfc_nodelist *ndlp,
			    void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;

	lpfc_rcv_padisc(vport, ndlp, cmdiocb);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_prlo_adisc_issue(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			  void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;

	/* Treat like rcv logo */
	lpfc_rcv_logo(vport, ndlp, cmdiocb, ELS_CMD_PRLO);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_cmpl_adisc_adisc_issue(struct lpfc_vport *vport,
			    struct lpfc_nodelist *ndlp,
			    void *arg, uint32_t evt)
{
	struct lpfc_hba   *phba = vport->phba;
	struct lpfc_iocbq *cmdiocb, *rspiocb;
	IOCB_t *irsp;
	ADISC *ap;
	int rc;

	cmdiocb = (struct lpfc_iocbq *) arg;
	rspiocb = cmdiocb->context_un.rsp_iocb;

	ap = (ADISC *)lpfc_check_elscmpl_iocb(phba, cmdiocb, rspiocb);
	irsp = &rspiocb->iocb;

	if ((irsp->ulpStatus) ||
	    (!lpfc_check_adisc(vport, ndlp, &ap->nodeName, &ap->portName))) {
		/* 1 sec timeout */
		mod_timer(&ndlp->nlp_delayfunc,
			  jiffies + msecs_to_jiffies(1000));
		spin_lock_irq(&ndlp->lock);
		ndlp->nlp_flag |= NLP_DELAY_TMO;
		spin_unlock_irq(&ndlp->lock);
		ndlp->nlp_last_elscmd = ELS_CMD_PLOGI;

		ndlp->nlp_prev_state = NLP_STE_ADISC_ISSUE;
		lpfc_nlp_set_state(vport, ndlp, NLP_STE_NPR_NODE);
		lpfc_unreg_rpi(vport, ndlp);
		return ndlp->nlp_state;
	}

	if (phba->sli_rev == LPFC_SLI_REV4) {
		rc = lpfc_sli4_resume_rpi(ndlp, NULL, NULL);
		if (rc) {
			/* Stay in state and retry. */
			ndlp->nlp_prev_state = NLP_STE_ADISC_ISSUE;
			return ndlp->nlp_state;
		}
	}

	if (ndlp->nlp_type & NLP_FCP_TARGET)
		ndlp->nlp_fc4_type |= NLP_FC4_FCP;

	if (ndlp->nlp_type & NLP_NVME_TARGET)
		ndlp->nlp_fc4_type |= NLP_FC4_NVME;

	if (ndlp->nlp_type & (NLP_FCP_TARGET | NLP_NVME_TARGET)) {
		ndlp->nlp_prev_state = NLP_STE_ADISC_ISSUE;
		lpfc_nlp_set_state(vport, ndlp, NLP_STE_MAPPED_NODE);
	} else {
		ndlp->nlp_prev_state = NLP_STE_ADISC_ISSUE;
		lpfc_nlp_set_state(vport, ndlp, NLP_STE_UNMAPPED_NODE);
	}

	return ndlp->nlp_state;
}

static uint32_t
lpfc_device_rm_adisc_issue(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			   void *arg, uint32_t evt)
{
	if (ndlp->nlp_flag & NLP_NPR_2B_DISC) {
		spin_lock_irq(&ndlp->lock);
		ndlp->nlp_flag |= NLP_NODEV_REMOVE;
		spin_unlock_irq(&ndlp->lock);
		return ndlp->nlp_state;
	} else {
		/* software abort outstanding ADISC */
		lpfc_els_abort(vport->phba, ndlp);

		lpfc_drop_node(vport, ndlp);
		return NLP_STE_FREED_NODE;
	}
}

static uint32_t
lpfc_device_recov_adisc_issue(struct lpfc_vport *vport,
			      struct lpfc_nodelist *ndlp,
			      void *arg,
			      uint32_t evt)
{
	struct lpfc_hba  *phba = vport->phba;

	/* Don't do anything that will mess up processing of the
	 * previous RSCN.
	 */
	if (vport->fc_flag & FC_RSCN_DEFERRED)
		return ndlp->nlp_state;

	/* software abort outstanding ADISC */
	lpfc_els_abort(phba, ndlp);

	ndlp->nlp_prev_state = NLP_STE_ADISC_ISSUE;
	lpfc_nlp_set_state(vport, ndlp, NLP_STE_NPR_NODE);
	spin_lock_irq(&ndlp->lock);
	ndlp->nlp_flag &= ~(NLP_NODEV_REMOVE | NLP_NPR_2B_DISC);
	spin_unlock_irq(&ndlp->lock);
	lpfc_disc_set_adisc(vport, ndlp);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_plogi_reglogin_issue(struct lpfc_vport *vport,
			      struct lpfc_nodelist *ndlp,
			      void *arg,
			      uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb = (struct lpfc_iocbq *) arg;

	lpfc_rcv_plogi(vport, ndlp, cmdiocb);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_prli_reglogin_issue(struct lpfc_vport *vport,
			     struct lpfc_nodelist *ndlp,
			     void *arg,
			     uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb = (struct lpfc_iocbq *) arg;
	struct ls_rjt     stat;

	if (!lpfc_rcv_prli_support_check(vport, ndlp, cmdiocb)) {
		return ndlp->nlp_state;
	}
	if (vport->phba->nvmet_support) {
		/* NVME Target mode.  Handle and respond to the PRLI and
		 * transition to UNMAPPED provided the RPI has completed
		 * registration.
		 */
		if (ndlp->nlp_flag & NLP_RPI_REGISTERED) {
			lpfc_rcv_prli(vport, ndlp, cmdiocb);
			lpfc_els_rsp_prli_acc(vport, cmdiocb, ndlp);
		} else {
			/* RPI registration has not completed. Reject the PRLI
			 * to prevent an illegal state transition when the
			 * rpi registration does complete.
			 */
			memset(&stat, 0, sizeof(struct ls_rjt));
			stat.un.b.lsRjtRsnCode = LSRJT_LOGICAL_BSY;
			stat.un.b.lsRjtRsnCodeExp = LSEXP_NOTHING_MORE;
			lpfc_els_rsp_reject(vport, stat.un.lsRjtError, cmdiocb,
					    ndlp, NULL);
			return ndlp->nlp_state;
		}
	} else {
		/* Initiator mode. */
		lpfc_els_rsp_prli_acc(vport, cmdiocb, ndlp);
	}
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_logo_reglogin_issue(struct lpfc_vport *vport,
			     struct lpfc_nodelist *ndlp,
			     void *arg,
			     uint32_t evt)
{
	struct lpfc_hba   *phba = vport->phba;
	struct lpfc_iocbq *cmdiocb = (struct lpfc_iocbq *) arg;
	LPFC_MBOXQ_t	  *mb;
	LPFC_MBOXQ_t	  *nextmb;
	struct lpfc_dmabuf *mp;
	struct lpfc_nodelist *ns_ndlp;

	cmdiocb = (struct lpfc_iocbq *) arg;

	/* cleanup any ndlp on mbox q waiting for reglogin cmpl */
	if ((mb = phba->sli.mbox_active)) {
		if ((mb->u.mb.mbxCommand == MBX_REG_LOGIN64) &&
		   (ndlp == (struct lpfc_nodelist *)mb->ctx_ndlp)) {
			ndlp->nlp_flag &= ~NLP_REG_LOGIN_SEND;
			lpfc_nlp_put(ndlp);
			mb->ctx_ndlp = NULL;
			mb->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
		}
	}

	spin_lock_irq(&phba->hbalock);
	list_for_each_entry_safe(mb, nextmb, &phba->sli.mboxq, list) {
		if ((mb->u.mb.mbxCommand == MBX_REG_LOGIN64) &&
		   (ndlp == (struct lpfc_nodelist *)mb->ctx_ndlp)) {
			mp = (struct lpfc_dmabuf *)(mb->ctx_buf);
			if (mp) {
				__lpfc_mbuf_free(phba, mp->virt, mp->phys);
				kfree(mp);
			}
			ndlp->nlp_flag &= ~NLP_REG_LOGIN_SEND;
			lpfc_nlp_put(ndlp);
			list_del(&mb->list);
			phba->sli.mboxq_cnt--;
			mempool_free(mb, phba->mbox_mem_pool);
		}
	}
	spin_unlock_irq(&phba->hbalock);

	/* software abort if any GID_FT is outstanding */
	if (vport->cfg_enable_fc4_type != LPFC_ENABLE_FCP) {
		ns_ndlp = lpfc_findnode_did(vport, NameServer_DID);
		if (ns_ndlp)
			lpfc_els_abort(phba, ns_ndlp);
	}

	lpfc_rcv_logo(vport, ndlp, cmdiocb, ELS_CMD_LOGO);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_padisc_reglogin_issue(struct lpfc_vport *vport,
			       struct lpfc_nodelist *ndlp,
			       void *arg,
			       uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb = (struct lpfc_iocbq *) arg;

	lpfc_rcv_padisc(vport, ndlp, cmdiocb);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_prlo_reglogin_issue(struct lpfc_vport *vport,
			     struct lpfc_nodelist *ndlp,
			     void *arg,
			     uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;
	lpfc_els_rsp_acc(vport, ELS_CMD_PRLO, cmdiocb, ndlp, NULL);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_cmpl_reglogin_reglogin_issue(struct lpfc_vport *vport,
				  struct lpfc_nodelist *ndlp,
				  void *arg,
				  uint32_t evt)
{
	struct lpfc_hba *phba = vport->phba;
	LPFC_MBOXQ_t *pmb = (LPFC_MBOXQ_t *) arg;
	MAILBOX_t *mb = &pmb->u.mb;
	uint32_t did  = mb->un.varWords[1];

	if (mb->mbxStatus) {
		/* RegLogin failed */
		lpfc_printf_vlog(vport, KERN_ERR, LOG_TRACE_EVENT,
				 "0246 RegLogin failed Data: x%x x%x x%x x%x "
				 "x%x\n",
				 did, mb->mbxStatus, vport->port_state,
				 mb->un.varRegLogin.vpi,
				 mb->un.varRegLogin.rpi);
		/*
		 * If RegLogin failed due to lack of HBA resources do not
		 * retry discovery.
		 */
		if (mb->mbxStatus == MBXERR_RPI_FULL) {
			ndlp->nlp_prev_state = NLP_STE_REG_LOGIN_ISSUE;
			lpfc_nlp_set_state(vport, ndlp, NLP_STE_NPR_NODE);
			return ndlp->nlp_state;
		}

		/* Put ndlp in npr state set plogi timer for 1 sec */
		mod_timer(&ndlp->nlp_delayfunc,
			  jiffies + msecs_to_jiffies(1000 * 1));
		spin_lock_irq(&ndlp->lock);
		ndlp->nlp_flag |= NLP_DELAY_TMO;
		spin_unlock_irq(&ndlp->lock);
		ndlp->nlp_last_elscmd = ELS_CMD_PLOGI;

		lpfc_issue_els_logo(vport, ndlp, 0);
		return ndlp->nlp_state;
	}

	/* SLI4 ports have preallocated logical rpis. */
	if (phba->sli_rev < LPFC_SLI_REV4)
		ndlp->nlp_rpi = mb->un.varWords[0];

	ndlp->nlp_flag |= NLP_RPI_REGISTERED;

	/* Only if we are not a fabric nport do we issue PRLI */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
			 "3066 RegLogin Complete on x%x x%x x%x\n",
			 did, ndlp->nlp_type, ndlp->nlp_fc4_type);
	if (!(ndlp->nlp_type & NLP_FABRIC) &&
	    (phba->nvmet_support == 0)) {
		/* The driver supports FCP and NVME concurrently.  If the
		 * ndlp's nlp_fc4_type is still zero, the driver doesn't
		 * know what PRLI to send yet.  Figure that out now and
		 * call PRLI depending on the outcome.
		 */
		if (vport->fc_flag & FC_PT2PT) {
			/* If we are pt2pt, there is no Fabric to determine
			 * the FC4 type of the remote nport. So if NVME
			 * is configured try it.
			 */
			ndlp->nlp_fc4_type |= NLP_FC4_FCP;
			if ((!(vport->fc_flag & FC_PT2PT_NO_NVME)) &&
			    (vport->cfg_enable_fc4_type == LPFC_ENABLE_BOTH ||
			    vport->cfg_enable_fc4_type == LPFC_ENABLE_NVME)) {
				ndlp->nlp_fc4_type |= NLP_FC4_NVME;
				/* We need to update the localport also */
				lpfc_nvme_update_localport(vport);
			}

		} else if (phba->fc_topology == LPFC_TOPOLOGY_LOOP) {
			ndlp->nlp_fc4_type |= NLP_FC4_FCP;

		} else if (ndlp->nlp_fc4_type == 0) {
			/* If we are only configured for FCP, the driver
			 * should just issue PRLI for FCP. Otherwise issue
			 * GFT_ID to determine if remote port supports NVME.
			 */
			if (vport->cfg_enable_fc4_type != LPFC_ENABLE_FCP) {
				lpfc_ns_cmd(vport, SLI_CTNS_GFT_ID, 0,
					    ndlp->nlp_DID);
				return ndlp->nlp_state;
			}
			ndlp->nlp_fc4_type = NLP_FC4_FCP;
		}

		ndlp->nlp_prev_state = NLP_STE_REG_LOGIN_ISSUE;
		lpfc_nlp_set_state(vport, ndlp, NLP_STE_PRLI_ISSUE);
		if (lpfc_issue_els_prli(vport, ndlp, 0)) {
			lpfc_issue_els_logo(vport, ndlp, 0);
			ndlp->nlp_prev_state = NLP_STE_REG_LOGIN_ISSUE;
			lpfc_nlp_set_state(vport, ndlp, NLP_STE_NPR_NODE);
		}
	} else {
		if ((vport->fc_flag & FC_PT2PT) && phba->nvmet_support)
			phba->targetport->port_id = vport->fc_myDID;

		/* Only Fabric ports should transition. NVME target
		 * must complete PRLI.
		 */
		if (ndlp->nlp_type & NLP_FABRIC) {
			ndlp->nlp_fc4_type &= ~NLP_FC4_FCP;
			ndlp->nlp_prev_state = NLP_STE_REG_LOGIN_ISSUE;
			lpfc_nlp_set_state(vport, ndlp, NLP_STE_UNMAPPED_NODE);
		}
	}
	return ndlp->nlp_state;
}

static uint32_t
lpfc_device_rm_reglogin_issue(struct lpfc_vport *vport,
			      struct lpfc_nodelist *ndlp,
			      void *arg,
			      uint32_t evt)
{
	if (ndlp->nlp_flag & NLP_NPR_2B_DISC) {
		spin_lock_irq(&ndlp->lock);
		ndlp->nlp_flag |= NLP_NODEV_REMOVE;
		spin_unlock_irq(&ndlp->lock);
		return ndlp->nlp_state;
	} else {
		lpfc_drop_node(vport, ndlp);
		return NLP_STE_FREED_NODE;
	}
}

static uint32_t
lpfc_device_recov_reglogin_issue(struct lpfc_vport *vport,
				 struct lpfc_nodelist *ndlp,
				 void *arg,
				 uint32_t evt)
{
	/* Don't do anything that will mess up processing of the
	 * previous RSCN.
	 */
	if (vport->fc_flag & FC_RSCN_DEFERRED)
		return ndlp->nlp_state;

	ndlp->nlp_prev_state = NLP_STE_REG_LOGIN_ISSUE;
	lpfc_nlp_set_state(vport, ndlp, NLP_STE_NPR_NODE);
	spin_lock_irq(&ndlp->lock);

	/* If we are a target we won't immediately transition into PRLI,
	 * so if REG_LOGIN already completed we don't need to ignore it.
	 */
	if (!(ndlp->nlp_flag & NLP_RPI_REGISTERED) ||
	    !vport->phba->nvmet_support)
		ndlp->nlp_flag |= NLP_IGNR_REG_CMPL;

	ndlp->nlp_flag &= ~(NLP_NODEV_REMOVE | NLP_NPR_2B_DISC);
	spin_unlock_irq(&ndlp->lock);
	lpfc_disc_set_adisc(vport, ndlp);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_plogi_prli_issue(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			  void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;

	lpfc_rcv_plogi(vport, ndlp, cmdiocb);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_prli_prli_issue(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			 void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb = (struct lpfc_iocbq *) arg;

	if (!lpfc_rcv_prli_support_check(vport, ndlp, cmdiocb))
		return ndlp->nlp_state;
	lpfc_rcv_prli(vport, ndlp, cmdiocb);
	lpfc_els_rsp_prli_acc(vport, cmdiocb, ndlp);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_logo_prli_issue(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			 void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb = (struct lpfc_iocbq *) arg;

	/* Software abort outstanding PRLI before sending acc */
	lpfc_els_abort(vport->phba, ndlp);

	lpfc_rcv_logo(vport, ndlp, cmdiocb, ELS_CMD_LOGO);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_padisc_prli_issue(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			   void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb = (struct lpfc_iocbq *) arg;

	lpfc_rcv_padisc(vport, ndlp, cmdiocb);
	return ndlp->nlp_state;
}

/* This routine is envoked when we rcv a PRLO request from a nport
 * we are logged into.  We should send back a PRLO rsp setting the
 * appropriate bits.
 * NEXT STATE = PRLI_ISSUE
 */
static uint32_t
lpfc_rcv_prlo_prli_issue(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			 void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb = (struct lpfc_iocbq *) arg;

	lpfc_els_rsp_acc(vport, ELS_CMD_PRLO, cmdiocb, ndlp, NULL);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_cmpl_prli_prli_issue(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			  void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb, *rspiocb;
	struct lpfc_hba   *phba = vport->phba;
	IOCB_t *irsp;
	PRLI *npr;
	struct lpfc_nvme_prli *nvpr;
	void *temp_ptr;

	cmdiocb = (struct lpfc_iocbq *) arg;
	rspiocb = cmdiocb->context_un.rsp_iocb;

	/* A solicited PRLI is either FCP or NVME.  The PRLI cmd/rsp
	 * format is different so NULL the two PRLI types so that the
	 * driver correctly gets the correct context.
	 */
	npr = NULL;
	nvpr = NULL;
	temp_ptr = lpfc_check_elscmpl_iocb(phba, cmdiocb, rspiocb);
	if (cmdiocb->iocb_flag & LPFC_PRLI_FCP_REQ)
		npr = (PRLI *) temp_ptr;
	else if (cmdiocb->iocb_flag & LPFC_PRLI_NVME_REQ)
		nvpr = (struct lpfc_nvme_prli *) temp_ptr;

	irsp = &rspiocb->iocb;
	if (irsp->ulpStatus) {
		if ((vport->port_type == LPFC_NPIV_PORT) &&
		    vport->cfg_restrict_login) {
			goto out;
		}

		/* Adjust the nlp_type accordingly if the PRLI failed */
		if (npr)
			ndlp->nlp_fc4_type &= ~NLP_FC4_FCP;
		if (nvpr)
			ndlp->nlp_fc4_type &= ~NLP_FC4_NVME;

		/* We can't set the DSM state till BOTH PRLIs complete */
		goto out_err;
	}

	if (npr && (npr->acceptRspCode == PRLI_REQ_EXECUTED) &&
	    (npr->prliType == PRLI_FCP_TYPE)) {
		lpfc_printf_vlog(vport, KERN_INFO, LOG_NVME_DISC,
				 "6028 FCP NPR PRLI Cmpl Init %d Target %d\n",
				 npr->initiatorFunc,
				 npr->targetFunc);
		if (npr->initiatorFunc)
			ndlp->nlp_type |= NLP_FCP_INITIATOR;
		if (npr->targetFunc) {
			ndlp->nlp_type |= NLP_FCP_TARGET;
			if (npr->writeXferRdyDis)
				ndlp->nlp_flag |= NLP_FIRSTBURST;
		}
		if (npr->Retry)
			ndlp->nlp_fcp_info |= NLP_FCP_2_DEVICE;

	} else if (nvpr &&
		   (bf_get_be32(prli_acc_rsp_code, nvpr) ==
		    PRLI_REQ_EXECUTED) &&
		   (bf_get_be32(prli_type_code, nvpr) ==
		    PRLI_NVME_TYPE)) {

		/* Complete setting up the remote ndlp personality. */
		if (bf_get_be32(prli_init, nvpr))
			ndlp->nlp_type |= NLP_NVME_INITIATOR;

		if (phba->nsler && bf_get_be32(prli_nsler, nvpr) &&
		    bf_get_be32(prli_conf, nvpr))

			ndlp->nlp_nvme_info |= NLP_NVME_NSLER;
		else
			ndlp->nlp_nvme_info &= ~NLP_NVME_NSLER;

		/* Target driver cannot solicit NVME FB. */
		if (bf_get_be32(prli_tgt, nvpr)) {
			/* Complete the nvme target roles.  The transport
			 * needs to know if the rport is capable of
			 * discovery in addition to its role.
			 */
			ndlp->nlp_type |= NLP_NVME_TARGET;
			if (bf_get_be32(prli_disc, nvpr))
				ndlp->nlp_type |= NLP_NVME_DISCOVERY;

			/*
			 * If prli_fba is set, the Target supports FirstBurst.
			 * If prli_fb_sz is 0, the FirstBurst size is unlimited,
			 * otherwise it defines the actual size supported by
			 * the NVME Target.
			 */
			if ((bf_get_be32(prli_fba, nvpr) == 1) &&
			    (phba->cfg_nvme_enable_fb) &&
			    (!phba->nvmet_support)) {
				/* Both sides support FB. The target's first
				 * burst size is a 512 byte encoded value.
				 */
				ndlp->nlp_flag |= NLP_FIRSTBURST;
				ndlp->nvme_fb_size = bf_get_be32(prli_fb_sz,
								 nvpr);

				/* Expressed in units of 512 bytes */
				if (ndlp->nvme_fb_size)
					ndlp->nvme_fb_size <<=
						LPFC_NVME_FB_SHIFT;
				else
					ndlp->nvme_fb_size = LPFC_NVME_MAX_FB;
			}
		}

		lpfc_printf_vlog(vport, KERN_INFO, LOG_NVME_DISC,
				 "6029 NVME PRLI Cmpl w1 x%08x "
				 "w4 x%08x w5 x%08x flag x%x, "
				 "fcp_info x%x nlp_type x%x\n",
				 be32_to_cpu(nvpr->word1),
				 be32_to_cpu(nvpr->word4),
				 be32_to_cpu(nvpr->word5),
				 ndlp->nlp_flag, ndlp->nlp_fcp_info,
				 ndlp->nlp_type);
	}
	if (!(ndlp->nlp_type & NLP_FCP_TARGET) &&
	    (vport->port_type == LPFC_NPIV_PORT) &&
	     vport->cfg_restrict_login) {
out:
		spin_lock_irq(&ndlp->lock);
		ndlp->nlp_flag |= NLP_TARGET_REMOVE;
		spin_unlock_irq(&ndlp->lock);
		lpfc_issue_els_logo(vport, ndlp, 0);

		ndlp->nlp_prev_state = NLP_STE_PRLI_ISSUE;
		lpfc_nlp_set_state(vport, ndlp, NLP_STE_NPR_NODE);
		return ndlp->nlp_state;
	}

out_err:
	/* The ndlp state cannot move to MAPPED or UNMAPPED before all PRLIs
	 * are complete.
	 */
	if (ndlp->fc4_prli_sent == 0) {
		ndlp->nlp_prev_state = NLP_STE_PRLI_ISSUE;
		if (ndlp->nlp_type & (NLP_FCP_TARGET | NLP_NVME_TARGET))
			lpfc_nlp_set_state(vport, ndlp, NLP_STE_MAPPED_NODE);
		else if (ndlp->nlp_type &
			 (NLP_FCP_INITIATOR | NLP_NVME_INITIATOR))
			lpfc_nlp_set_state(vport, ndlp, NLP_STE_UNMAPPED_NODE);
	} else
		lpfc_printf_vlog(vport,
				 KERN_INFO, LOG_ELS,
				 "3067 PRLI's still outstanding "
				 "on x%06x - count %d, Pend Node Mode "
				 "transition...\n",
				 ndlp->nlp_DID, ndlp->fc4_prli_sent);

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
lpfc_device_rm_prli_issue(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			  void *arg, uint32_t evt)
{
	if (ndlp->nlp_flag & NLP_NPR_2B_DISC) {
		spin_lock_irq(&ndlp->lock);
		ndlp->nlp_flag |= NLP_NODEV_REMOVE;
		spin_unlock_irq(&ndlp->lock);
		return ndlp->nlp_state;
	} else {
		/* software abort outstanding PLOGI */
		lpfc_els_abort(vport->phba, ndlp);

		lpfc_drop_node(vport, ndlp);
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
lpfc_device_recov_prli_issue(struct lpfc_vport *vport,
			     struct lpfc_nodelist *ndlp,
			     void *arg,
			     uint32_t evt)
{
	struct lpfc_hba  *phba = vport->phba;

	/* Don't do anything that will mess up processing of the
	 * previous RSCN.
	 */
	if (vport->fc_flag & FC_RSCN_DEFERRED)
		return ndlp->nlp_state;

	/* software abort outstanding PRLI */
	lpfc_els_abort(phba, ndlp);

	ndlp->nlp_prev_state = NLP_STE_PRLI_ISSUE;
	lpfc_nlp_set_state(vport, ndlp, NLP_STE_NPR_NODE);
	spin_lock_irq(&ndlp->lock);
	ndlp->nlp_flag &= ~(NLP_NODEV_REMOVE | NLP_NPR_2B_DISC);
	spin_unlock_irq(&ndlp->lock);
	lpfc_disc_set_adisc(vport, ndlp);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_plogi_logo_issue(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			  void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb = (struct lpfc_iocbq *)arg;
	struct ls_rjt     stat;

	memset(&stat, 0, sizeof(struct ls_rjt));
	stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
	stat.un.b.lsRjtRsnCodeExp = LSEXP_NOTHING_MORE;
	lpfc_els_rsp_reject(vport, stat.un.lsRjtError, cmdiocb, ndlp, NULL);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_prli_logo_issue(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			 void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb = (struct lpfc_iocbq *)arg;
	struct ls_rjt     stat;

	memset(&stat, 0, sizeof(struct ls_rjt));
	stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
	stat.un.b.lsRjtRsnCodeExp = LSEXP_NOTHING_MORE;
	lpfc_els_rsp_reject(vport, stat.un.lsRjtError, cmdiocb, ndlp, NULL);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_logo_logo_issue(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			 void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb = (struct lpfc_iocbq *)arg;

	spin_lock_irq(&ndlp->lock);
	ndlp->nlp_flag |= NLP_LOGO_ACC;
	spin_unlock_irq(&ndlp->lock);
	lpfc_els_rsp_acc(vport, ELS_CMD_ACC, cmdiocb, ndlp, NULL);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_padisc_logo_issue(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			   void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb = (struct lpfc_iocbq *)arg;
	struct ls_rjt     stat;

	memset(&stat, 0, sizeof(struct ls_rjt));
	stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
	stat.un.b.lsRjtRsnCodeExp = LSEXP_NOTHING_MORE;
	lpfc_els_rsp_reject(vport, stat.un.lsRjtError, cmdiocb, ndlp, NULL);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_prlo_logo_issue(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			 void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb = (struct lpfc_iocbq *)arg;
	struct ls_rjt     stat;

	memset(&stat, 0, sizeof(struct ls_rjt));
	stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
	stat.un.b.lsRjtRsnCodeExp = LSEXP_NOTHING_MORE;
	lpfc_els_rsp_reject(vport, stat.un.lsRjtError, cmdiocb, ndlp, NULL);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_cmpl_logo_logo_issue(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			  void *arg, uint32_t evt)
{
	ndlp->nlp_prev_state = NLP_STE_LOGO_ISSUE;
	lpfc_nlp_set_state(vport, ndlp, NLP_STE_NPR_NODE);
	spin_lock_irq(&ndlp->lock);
	ndlp->nlp_flag &= ~(NLP_NODEV_REMOVE | NLP_NPR_2B_DISC);
	spin_unlock_irq(&ndlp->lock);
	lpfc_disc_set_adisc(vport, ndlp);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_device_rm_logo_issue(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			  void *arg, uint32_t evt)
{
	/*
	 * DevLoss has timed out and is calling for Device Remove.
	 * In this case, abort the LOGO and cleanup the ndlp
	 */

	lpfc_unreg_rpi(vport, ndlp);
	/* software abort outstanding PLOGI */
	lpfc_els_abort(vport->phba, ndlp);
	lpfc_drop_node(vport, ndlp);
	return NLP_STE_FREED_NODE;
}

static uint32_t
lpfc_device_recov_logo_issue(struct lpfc_vport *vport,
			     struct lpfc_nodelist *ndlp,
			     void *arg, uint32_t evt)
{
	/*
	 * Device Recovery events have no meaning for a node with a LOGO
	 * outstanding.  The LOGO has to complete first and handle the
	 * node from that point.
	 */
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_plogi_unmap_node(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			  void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb = (struct lpfc_iocbq *) arg;

	lpfc_rcv_plogi(vport, ndlp, cmdiocb);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_prli_unmap_node(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			 void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb = (struct lpfc_iocbq *) arg;

	if (!lpfc_rcv_prli_support_check(vport, ndlp, cmdiocb))
		return ndlp->nlp_state;

	lpfc_rcv_prli(vport, ndlp, cmdiocb);
	lpfc_els_rsp_prli_acc(vport, cmdiocb, ndlp);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_logo_unmap_node(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			 void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb = (struct lpfc_iocbq *) arg;

	lpfc_rcv_logo(vport, ndlp, cmdiocb, ELS_CMD_LOGO);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_padisc_unmap_node(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			   void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb = (struct lpfc_iocbq *) arg;

	lpfc_rcv_padisc(vport, ndlp, cmdiocb);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_prlo_unmap_node(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			 void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb = (struct lpfc_iocbq *) arg;

	lpfc_els_rsp_acc(vport, ELS_CMD_PRLO, cmdiocb, ndlp, NULL);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_device_rm_unmap_node(struct lpfc_vport *vport,
			  struct lpfc_nodelist *ndlp,
			  void *arg,
			  uint32_t evt)
{
	lpfc_drop_node(vport, ndlp);
	return NLP_STE_FREED_NODE;
}

static uint32_t
lpfc_device_recov_unmap_node(struct lpfc_vport *vport,
			     struct lpfc_nodelist *ndlp,
			     void *arg,
			     uint32_t evt)
{
	ndlp->nlp_prev_state = NLP_STE_UNMAPPED_NODE;
	lpfc_nlp_set_state(vport, ndlp, NLP_STE_NPR_NODE);
	spin_lock_irq(&ndlp->lock);
	ndlp->nlp_flag &= ~(NLP_NODEV_REMOVE | NLP_NPR_2B_DISC);
	ndlp->nlp_fc4_type &= ~(NLP_FC4_FCP | NLP_FC4_NVME);
	spin_unlock_irq(&ndlp->lock);
	lpfc_disc_set_adisc(vport, ndlp);

	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_plogi_mapped_node(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			   void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb = (struct lpfc_iocbq *) arg;

	lpfc_rcv_plogi(vport, ndlp, cmdiocb);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_prli_mapped_node(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			  void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb = (struct lpfc_iocbq *) arg;

	if (!lpfc_rcv_prli_support_check(vport, ndlp, cmdiocb))
		return ndlp->nlp_state;
	lpfc_els_rsp_prli_acc(vport, cmdiocb, ndlp);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_logo_mapped_node(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			  void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb = (struct lpfc_iocbq *) arg;

	lpfc_rcv_logo(vport, ndlp, cmdiocb, ELS_CMD_LOGO);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_padisc_mapped_node(struct lpfc_vport *vport,
			    struct lpfc_nodelist *ndlp,
			    void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb = (struct lpfc_iocbq *) arg;

	lpfc_rcv_padisc(vport, ndlp, cmdiocb);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_prlo_mapped_node(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			  void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb = (struct lpfc_iocbq *) arg;

	/* flush the target */
	lpfc_sli_abort_iocb(vport, ndlp->nlp_sid, 0, LPFC_CTX_TGT);

	/* Treat like rcv logo */
	lpfc_rcv_logo(vport, ndlp, cmdiocb, ELS_CMD_PRLO);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_device_recov_mapped_node(struct lpfc_vport *vport,
			      struct lpfc_nodelist *ndlp,
			      void *arg,
			      uint32_t evt)
{
	lpfc_disc_set_adisc(vport, ndlp);

	ndlp->nlp_prev_state = NLP_STE_MAPPED_NODE;
	lpfc_nlp_set_state(vport, ndlp, NLP_STE_NPR_NODE);
	spin_lock_irq(&ndlp->lock);
	ndlp->nlp_flag &= ~(NLP_NODEV_REMOVE | NLP_NPR_2B_DISC);
	ndlp->nlp_fc4_type &= ~(NLP_FC4_FCP | NLP_FC4_NVME);
	spin_unlock_irq(&ndlp->lock);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_plogi_npr_node(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb  = (struct lpfc_iocbq *) arg;

	/* Ignore PLOGI if we have an outstanding LOGO */
	if (ndlp->nlp_flag & (NLP_LOGO_SND | NLP_LOGO_ACC))
		return ndlp->nlp_state;
	if (lpfc_rcv_plogi(vport, ndlp, cmdiocb)) {
		lpfc_cancel_retry_delay_tmo(vport, ndlp);
		spin_lock_irq(&ndlp->lock);
		ndlp->nlp_flag &= ~(NLP_NPR_ADISC | NLP_NPR_2B_DISC);
		spin_unlock_irq(&ndlp->lock);
	} else if (!(ndlp->nlp_flag & NLP_NPR_2B_DISC)) {
		/* send PLOGI immediately, move to PLOGI issue state */
		if (!(ndlp->nlp_flag & NLP_DELAY_TMO)) {
			ndlp->nlp_prev_state = NLP_STE_NPR_NODE;
			lpfc_nlp_set_state(vport, ndlp, NLP_STE_PLOGI_ISSUE);
			lpfc_issue_els_plogi(vport, ndlp->nlp_DID, 0);
		}
	}
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_prli_npr_node(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
		       void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb = (struct lpfc_iocbq *) arg;
	struct ls_rjt     stat;

	memset(&stat, 0, sizeof (struct ls_rjt));
	stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
	stat.un.b.lsRjtRsnCodeExp = LSEXP_NOTHING_MORE;
	lpfc_els_rsp_reject(vport, stat.un.lsRjtError, cmdiocb, ndlp, NULL);

	if (!(ndlp->nlp_flag & NLP_DELAY_TMO)) {
		/*
		 * ADISC nodes will be handled in regular discovery path after
		 * receiving response from NS.
		 *
		 * For other nodes, Send PLOGI to trigger an implicit LOGO.
		 */
		if (!(ndlp->nlp_flag & NLP_NPR_ADISC)) {
			ndlp->nlp_prev_state = NLP_STE_NPR_NODE;
			lpfc_nlp_set_state(vport, ndlp, NLP_STE_PLOGI_ISSUE);
			lpfc_issue_els_plogi(vport, ndlp->nlp_DID, 0);
		}
	}
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_logo_npr_node(struct lpfc_vport *vport,  struct lpfc_nodelist *ndlp,
		       void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb = (struct lpfc_iocbq *) arg;

	lpfc_rcv_logo(vport, ndlp, cmdiocb, ELS_CMD_LOGO);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_padisc_npr_node(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			 void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb = (struct lpfc_iocbq *) arg;

	lpfc_rcv_padisc(vport, ndlp, cmdiocb);
	/*
	 * Do not start discovery if discovery is about to start
	 * or discovery in progress for this node. Starting discovery
	 * here will affect the counting of discovery threads.
	 */
	if (!(ndlp->nlp_flag & NLP_DELAY_TMO) &&
	    !(ndlp->nlp_flag & NLP_NPR_2B_DISC)) {
		/*
		 * ADISC nodes will be handled in regular discovery path after
		 * receiving response from NS.
		 *
		 * For other nodes, Send PLOGI to trigger an implicit LOGO.
		 */
		if (!(ndlp->nlp_flag & NLP_NPR_ADISC)) {
			ndlp->nlp_prev_state = NLP_STE_NPR_NODE;
			lpfc_nlp_set_state(vport, ndlp, NLP_STE_PLOGI_ISSUE);
			lpfc_issue_els_plogi(vport, ndlp->nlp_DID, 0);
		}
	}
	return ndlp->nlp_state;
}

static uint32_t
lpfc_rcv_prlo_npr_node(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
		       void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb = (struct lpfc_iocbq *) arg;

	spin_lock_irq(&ndlp->lock);
	ndlp->nlp_flag |= NLP_LOGO_ACC;
	spin_unlock_irq(&ndlp->lock);

	lpfc_els_rsp_acc(vport, ELS_CMD_ACC, cmdiocb, ndlp, NULL);

	if ((ndlp->nlp_flag & NLP_DELAY_TMO) == 0) {
		mod_timer(&ndlp->nlp_delayfunc,
			  jiffies + msecs_to_jiffies(1000 * 1));
		spin_lock_irq(&ndlp->lock);
		ndlp->nlp_flag |= NLP_DELAY_TMO;
		ndlp->nlp_flag &= ~NLP_NPR_ADISC;
		spin_unlock_irq(&ndlp->lock);
		ndlp->nlp_last_elscmd = ELS_CMD_PLOGI;
	} else {
		spin_lock_irq(&ndlp->lock);
		ndlp->nlp_flag &= ~NLP_NPR_ADISC;
		spin_unlock_irq(&ndlp->lock);
	}
	return ndlp->nlp_state;
}

static uint32_t
lpfc_cmpl_plogi_npr_node(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			 void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb, *rspiocb;
	IOCB_t *irsp;

	cmdiocb = (struct lpfc_iocbq *) arg;
	rspiocb = cmdiocb->context_un.rsp_iocb;

	irsp = &rspiocb->iocb;
	if (irsp->ulpStatus) {
		return NLP_STE_FREED_NODE;
	}
	return ndlp->nlp_state;
}

static uint32_t
lpfc_cmpl_prli_npr_node(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb, *rspiocb;
	IOCB_t *irsp;

	cmdiocb = (struct lpfc_iocbq *) arg;
	rspiocb = cmdiocb->context_un.rsp_iocb;

	irsp = &rspiocb->iocb;
	if (irsp->ulpStatus && (ndlp->nlp_flag & NLP_NODEV_REMOVE)) {
		lpfc_drop_node(vport, ndlp);
		return NLP_STE_FREED_NODE;
	}
	return ndlp->nlp_state;
}

static uint32_t
lpfc_cmpl_logo_npr_node(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			void *arg, uint32_t evt)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);

	/* For the fabric port just clear the fc flags. */
	if (ndlp->nlp_DID == Fabric_DID) {
		spin_lock_irq(shost->host_lock);
		vport->fc_flag &= ~(FC_FABRIC | FC_PUBLIC_LOOP);
		spin_unlock_irq(shost->host_lock);
	}
	lpfc_unreg_rpi(vport, ndlp);
	return ndlp->nlp_state;
}

static uint32_t
lpfc_cmpl_adisc_npr_node(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			 void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb, *rspiocb;
	IOCB_t *irsp;

	cmdiocb = (struct lpfc_iocbq *) arg;
	rspiocb = cmdiocb->context_un.rsp_iocb;

	irsp = &rspiocb->iocb;
	if (irsp->ulpStatus && (ndlp->nlp_flag & NLP_NODEV_REMOVE)) {
		lpfc_drop_node(vport, ndlp);
		return NLP_STE_FREED_NODE;
	}
	return ndlp->nlp_state;
}

static uint32_t
lpfc_cmpl_reglogin_npr_node(struct lpfc_vport *vport,
			    struct lpfc_nodelist *ndlp,
			    void *arg, uint32_t evt)
{
	LPFC_MBOXQ_t *pmb = (LPFC_MBOXQ_t *) arg;
	MAILBOX_t    *mb = &pmb->u.mb;

	if (!mb->mbxStatus) {
		/* SLI4 ports have preallocated logical rpis. */
		if (vport->phba->sli_rev < LPFC_SLI_REV4)
			ndlp->nlp_rpi = mb->un.varWords[0];
		ndlp->nlp_flag |= NLP_RPI_REGISTERED;
		if (ndlp->nlp_flag & NLP_LOGO_ACC) {
			lpfc_unreg_rpi(vport, ndlp);
		}
	} else {
		if (ndlp->nlp_flag & NLP_NODEV_REMOVE) {
			lpfc_drop_node(vport, ndlp);
			return NLP_STE_FREED_NODE;
		}
	}
	return ndlp->nlp_state;
}

static uint32_t
lpfc_device_rm_npr_node(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			void *arg, uint32_t evt)
{
	if (ndlp->nlp_flag & NLP_NPR_2B_DISC) {
		spin_lock_irq(&ndlp->lock);
		ndlp->nlp_flag |= NLP_NODEV_REMOVE;
		spin_unlock_irq(&ndlp->lock);
		return ndlp->nlp_state;
	}
	lpfc_drop_node(vport, ndlp);
	return NLP_STE_FREED_NODE;
}

static uint32_t
lpfc_device_recov_npr_node(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			   void *arg, uint32_t evt)
{
	/* Don't do anything that will mess up processing of the
	 * previous RSCN.
	 */
	if (vport->fc_flag & FC_RSCN_DEFERRED)
		return ndlp->nlp_state;

	lpfc_cancel_retry_delay_tmo(vport, ndlp);
	spin_lock_irq(&ndlp->lock);
	ndlp->nlp_flag &= ~(NLP_NODEV_REMOVE | NLP_NPR_2B_DISC);
	ndlp->nlp_fc4_type &= ~(NLP_FC4_FCP | NLP_FC4_NVME);
	spin_unlock_irq(&ndlp->lock);
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
     (struct lpfc_vport *, struct lpfc_nodelist *, void *, uint32_t) = {
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
	lpfc_device_recov_unused_node,	/* DEVICE_RECOVERY */

	lpfc_rcv_plogi_plogi_issue,	/* RCV_PLOGI   PLOGI_ISSUE    */
	lpfc_rcv_prli_plogi_issue,	/* RCV_PRLI        */
	lpfc_rcv_logo_plogi_issue,	/* RCV_LOGO        */
	lpfc_rcv_els_plogi_issue,	/* RCV_ADISC       */
	lpfc_rcv_els_plogi_issue,	/* RCV_PDISC       */
	lpfc_rcv_els_plogi_issue,	/* RCV_PRLO        */
	lpfc_cmpl_plogi_plogi_issue,	/* CMPL_PLOGI      */
	lpfc_disc_illegal,		/* CMPL_PRLI       */
	lpfc_cmpl_logo_plogi_issue,	/* CMPL_LOGO       */
	lpfc_disc_illegal,		/* CMPL_ADISC      */
	lpfc_cmpl_reglogin_plogi_issue,/* CMPL_REG_LOGIN  */
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
	lpfc_cmpl_plogi_illegal,	/* CMPL_PLOGI      */
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
	lpfc_cmpl_plogi_illegal,	/* CMPL_PLOGI      */
	lpfc_cmpl_prli_prli_issue,	/* CMPL_PRLI       */
	lpfc_disc_illegal,		/* CMPL_LOGO       */
	lpfc_disc_illegal,		/* CMPL_ADISC      */
	lpfc_disc_illegal,		/* CMPL_REG_LOGIN  */
	lpfc_device_rm_prli_issue,	/* DEVICE_RM       */
	lpfc_device_recov_prli_issue,	/* DEVICE_RECOVERY */

	lpfc_rcv_plogi_logo_issue,	/* RCV_PLOGI   LOGO_ISSUE     */
	lpfc_rcv_prli_logo_issue,	/* RCV_PRLI        */
	lpfc_rcv_logo_logo_issue,	/* RCV_LOGO        */
	lpfc_rcv_padisc_logo_issue,	/* RCV_ADISC       */
	lpfc_rcv_padisc_logo_issue,	/* RCV_PDISC       */
	lpfc_rcv_prlo_logo_issue,	/* RCV_PRLO        */
	lpfc_cmpl_plogi_illegal,	/* CMPL_PLOGI      */
	lpfc_disc_illegal,		/* CMPL_PRLI       */
	lpfc_cmpl_logo_logo_issue,	/* CMPL_LOGO       */
	lpfc_disc_illegal,		/* CMPL_ADISC      */
	lpfc_disc_illegal,		/* CMPL_REG_LOGIN  */
	lpfc_device_rm_logo_issue,	/* DEVICE_RM       */
	lpfc_device_recov_logo_issue,	/* DEVICE_RECOVERY */

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
	lpfc_device_rm_unmap_node,	/* DEVICE_RM       */
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
lpfc_disc_state_machine(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			void *arg, uint32_t evt)
{
	uint32_t cur_state, rc;
	uint32_t(*func) (struct lpfc_vport *, struct lpfc_nodelist *, void *,
			 uint32_t);
	uint32_t got_ndlp = 0;
	uint32_t data1;

	if (lpfc_nlp_get(ndlp))
		got_ndlp = 1;

	cur_state = ndlp->nlp_state;

	data1 = (((uint32_t)ndlp->nlp_fc4_type << 16) |
		((uint32_t)ndlp->nlp_type));
	/* DSM in event <evt> on NPort <nlp_DID> in state <cur_state> */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
			 "0211 DSM in event x%x on NPort x%x in "
			 "state %d rpi x%x Data: x%x x%x\n",
			 evt, ndlp->nlp_DID, cur_state, ndlp->nlp_rpi,
			 ndlp->nlp_flag, data1);

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_DSM,
		 "DSM in:          evt:%d ste:%d did:x%x",
		evt, cur_state, ndlp->nlp_DID);

	func = lpfc_disc_action[(cur_state * NLP_EVT_MAX_EVENT) + evt];
	rc = (func) (vport, ndlp, arg, evt);

	/* DSM out state <rc> on NPort <nlp_DID> */
	if (got_ndlp) {
		data1 = (((uint32_t)ndlp->nlp_fc4_type << 16) |
			((uint32_t)ndlp->nlp_type));
		lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
			 "0212 DSM out state %d on NPort x%x "
			 "rpi x%x Data: x%x x%x\n",
			 rc, ndlp->nlp_DID, ndlp->nlp_rpi, ndlp->nlp_flag,
			 data1);

		lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_DSM,
			"DSM out:         ste:%d did:x%x flg:x%x",
			rc, ndlp->nlp_DID, ndlp->nlp_flag);
		/* Decrement the ndlp reference count held for this function */
		lpfc_nlp_put(ndlp);
	} else {
		lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
			"0213 DSM out state %d on NPort free\n", rc);

		lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_DSM,
			"DSM out:         ste:%d did:x%x flg:x%x",
			rc, 0, 0);
	}

	return rc;
}
