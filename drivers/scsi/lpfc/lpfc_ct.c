/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2004-2008 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.emulex.com                                                  *
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

/*
 * Fibre Channel SCSI LAN Device Driver CT support: FC Generic Services FC-GS
 */

#include <linux/blkdev.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/utsname.h>

#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport_fc.h>

#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_nl.h"
#include "lpfc_disc.h"
#include "lpfc_scsi.h"
#include "lpfc.h"
#include "lpfc_logmsg.h"
#include "lpfc_crtn.h"
#include "lpfc_version.h"
#include "lpfc_vport.h"
#include "lpfc_debugfs.h"

#define HBA_PORTSPEED_UNKNOWN               0	/* Unknown - transceiver
						 * incapable of reporting */
#define HBA_PORTSPEED_1GBIT                 1	/* 1 GBit/sec */
#define HBA_PORTSPEED_2GBIT                 2	/* 2 GBit/sec */
#define HBA_PORTSPEED_4GBIT                 8   /* 4 GBit/sec */
#define HBA_PORTSPEED_8GBIT                16   /* 8 GBit/sec */
#define HBA_PORTSPEED_10GBIT                4	/* 10 GBit/sec */
#define HBA_PORTSPEED_NOT_NEGOTIATED        5	/* Speed not established */

#define FOURBYTES	4


static char *lpfc_release_version = LPFC_DRIVER_VERSION;

static void
lpfc_ct_ignore_hbq_buffer(struct lpfc_hba *phba, struct lpfc_iocbq *piocbq,
			  struct lpfc_dmabuf *mp, uint32_t size)
{
	if (!mp) {
		lpfc_printf_log(phba, KERN_INFO, LOG_ELS,
				"0146 Ignoring unsolicited CT No HBQ "
				"status = x%x\n",
				piocbq->iocb.ulpStatus);
	}
	lpfc_printf_log(phba, KERN_INFO, LOG_ELS,
			"0145 Ignoring unsolicted CT HBQ Size:%d "
			"status = x%x\n",
			size, piocbq->iocb.ulpStatus);
}

static void
lpfc_ct_unsol_buffer(struct lpfc_hba *phba, struct lpfc_iocbq *piocbq,
		     struct lpfc_dmabuf *mp, uint32_t size)
{
	lpfc_ct_ignore_hbq_buffer(phba, piocbq, mp, size);
}

void
lpfc_ct_unsol_event(struct lpfc_hba *phba, struct lpfc_sli_ring *pring,
		    struct lpfc_iocbq *piocbq)
{

	struct lpfc_dmabuf *mp = NULL;
	IOCB_t *icmd = &piocbq->iocb;
	int i;
	struct lpfc_iocbq *iocbq;
	dma_addr_t paddr;
	uint32_t size;
	struct list_head head;
	struct lpfc_dmabuf *bdeBuf;

	if (unlikely(icmd->ulpStatus == IOSTAT_NEED_BUFFER)) {
		lpfc_sli_hbqbuf_add_hbqs(phba, LPFC_ELS_HBQ);
	} else if ((icmd->ulpStatus == IOSTAT_LOCAL_REJECT) &&
		((icmd->un.ulpWord[4] & 0xff) == IOERR_RCV_BUFFER_WAITING)) {
		/* Not enough posted buffers; Try posting more buffers */
		phba->fc_stat.NoRcvBuf++;
		if (!(phba->sli3_options & LPFC_SLI3_HBQ_ENABLED))
			lpfc_post_buffer(phba, pring, 2);
		return;
	}

	/* If there are no BDEs associated with this IOCB,
	 * there is nothing to do.
	 */
	if (icmd->ulpBdeCount == 0)
		return;

	if (phba->sli3_options & LPFC_SLI3_HBQ_ENABLED) {
		INIT_LIST_HEAD(&head);
		list_add_tail(&head, &piocbq->list);
		list_for_each_entry(iocbq, &head, list) {
			icmd = &iocbq->iocb;
			if (icmd->ulpBdeCount == 0)
				continue;
			bdeBuf = iocbq->context2;
			iocbq->context2 = NULL;
			size  = icmd->un.cont64[0].tus.f.bdeSize;
			lpfc_ct_unsol_buffer(phba, piocbq, bdeBuf, size);
			lpfc_in_buf_free(phba, bdeBuf);
			if (icmd->ulpBdeCount == 2) {
				bdeBuf = iocbq->context3;
				iocbq->context3 = NULL;
				size  = icmd->unsli3.rcvsli3.bde2.tus.f.bdeSize;
				lpfc_ct_unsol_buffer(phba, piocbq, bdeBuf,
						     size);
				lpfc_in_buf_free(phba, bdeBuf);
			}
		}
		list_del(&head);
	} else {
		INIT_LIST_HEAD(&head);
		list_add_tail(&head, &piocbq->list);
		list_for_each_entry(iocbq, &head, list) {
			icmd = &iocbq->iocb;
			if (icmd->ulpBdeCount == 0)
				lpfc_ct_unsol_buffer(phba, iocbq, NULL, 0);
			for (i = 0; i < icmd->ulpBdeCount; i++) {
				paddr = getPaddr(icmd->un.cont64[i].addrHigh,
						 icmd->un.cont64[i].addrLow);
				mp = lpfc_sli_ringpostbuf_get(phba, pring,
							      paddr);
				size = icmd->un.cont64[i].tus.f.bdeSize;
				lpfc_ct_unsol_buffer(phba, iocbq, mp, size);
				lpfc_in_buf_free(phba, mp);
			}
			lpfc_post_buffer(phba, pring, i);
		}
		list_del(&head);
	}
}

static void
lpfc_free_ct_rsp(struct lpfc_hba *phba, struct lpfc_dmabuf *mlist)
{
	struct lpfc_dmabuf *mlast, *next_mlast;

	list_for_each_entry_safe(mlast, next_mlast, &mlist->list, list) {
		lpfc_mbuf_free(phba, mlast->virt, mlast->phys);
		list_del(&mlast->list);
		kfree(mlast);
	}
	lpfc_mbuf_free(phba, mlist->virt, mlist->phys);
	kfree(mlist);
	return;
}

static struct lpfc_dmabuf *
lpfc_alloc_ct_rsp(struct lpfc_hba *phba, int cmdcode, struct ulp_bde64 *bpl,
		  uint32_t size, int *entries)
{
	struct lpfc_dmabuf *mlist = NULL;
	struct lpfc_dmabuf *mp;
	int cnt, i = 0;

	/* We get chunks of FCELSSIZE */
	cnt = size > FCELSSIZE ? FCELSSIZE: size;

	while (size) {
		/* Allocate buffer for rsp payload */
		mp = kmalloc(sizeof(struct lpfc_dmabuf), GFP_KERNEL);
		if (!mp) {
			if (mlist)
				lpfc_free_ct_rsp(phba, mlist);
			return NULL;
		}

		INIT_LIST_HEAD(&mp->list);

		if (cmdcode == be16_to_cpu(SLI_CTNS_GID_FT) ||
		    cmdcode == be16_to_cpu(SLI_CTNS_GFF_ID))
			mp->virt = lpfc_mbuf_alloc(phba, MEM_PRI, &(mp->phys));
		else
			mp->virt = lpfc_mbuf_alloc(phba, 0, &(mp->phys));

		if (!mp->virt) {
			kfree(mp);
			if (mlist)
				lpfc_free_ct_rsp(phba, mlist);
			return NULL;
		}

		/* Queue it to a linked list */
		if (!mlist)
			mlist = mp;
		else
			list_add_tail(&mp->list, &mlist->list);

		bpl->tus.f.bdeFlags = BUFF_TYPE_BDE_64I;
		/* build buffer ptr list for IOCB */
		bpl->addrLow = le32_to_cpu(putPaddrLow(mp->phys) );
		bpl->addrHigh = le32_to_cpu(putPaddrHigh(mp->phys) );
		bpl->tus.f.bdeSize = (uint16_t) cnt;
		bpl->tus.w = le32_to_cpu(bpl->tus.w);
		bpl++;

		i++;
		size -= cnt;
	}

	*entries = i;
	return mlist;
}

int
lpfc_ct_free_iocb(struct lpfc_hba *phba, struct lpfc_iocbq *ctiocb)
{
	struct lpfc_dmabuf *buf_ptr;

	if (ctiocb->context_un.ndlp) {
		lpfc_nlp_put(ctiocb->context_un.ndlp);
		ctiocb->context_un.ndlp = NULL;
	}
	if (ctiocb->context1) {
		buf_ptr = (struct lpfc_dmabuf *) ctiocb->context1;
		lpfc_mbuf_free(phba, buf_ptr->virt, buf_ptr->phys);
		kfree(buf_ptr);
		ctiocb->context1 = NULL;
	}
	if (ctiocb->context2) {
		lpfc_free_ct_rsp(phba, (struct lpfc_dmabuf *) ctiocb->context2);
		ctiocb->context2 = NULL;
	}

	if (ctiocb->context3) {
		buf_ptr = (struct lpfc_dmabuf *) ctiocb->context3;
		lpfc_mbuf_free(phba, buf_ptr->virt, buf_ptr->phys);
		kfree(buf_ptr);
		ctiocb->context1 = NULL;
	}
	lpfc_sli_release_iocbq(phba, ctiocb);
	return 0;
}

static int
lpfc_gen_req(struct lpfc_vport *vport, struct lpfc_dmabuf *bmp,
	     struct lpfc_dmabuf *inp, struct lpfc_dmabuf *outp,
	     void (*cmpl) (struct lpfc_hba *, struct lpfc_iocbq *,
		     struct lpfc_iocbq *),
	     struct lpfc_nodelist *ndlp, uint32_t usr_flg, uint32_t num_entry,
	     uint32_t tmo, uint8_t retry)
{
	struct lpfc_hba  *phba = vport->phba;
	struct lpfc_sli  *psli = &phba->sli;
	struct lpfc_sli_ring *pring = &psli->ring[LPFC_ELS_RING];
	IOCB_t *icmd;
	struct lpfc_iocbq *geniocb;
	int rc;

	/* Allocate buffer for  command iocb */
	geniocb = lpfc_sli_get_iocbq(phba);

	if (geniocb == NULL)
		return 1;

	icmd = &geniocb->iocb;
	icmd->un.genreq64.bdl.ulpIoTag32 = 0;
	icmd->un.genreq64.bdl.addrHigh = putPaddrHigh(bmp->phys);
	icmd->un.genreq64.bdl.addrLow = putPaddrLow(bmp->phys);
	icmd->un.genreq64.bdl.bdeFlags = BUFF_TYPE_BLP_64;
	icmd->un.genreq64.bdl.bdeSize = (num_entry * sizeof (struct ulp_bde64));

	if (usr_flg)
		geniocb->context3 = NULL;
	else
		geniocb->context3 = (uint8_t *) bmp;

	/* Save for completion so we can release these resources */
	geniocb->context1 = (uint8_t *) inp;
	geniocb->context2 = (uint8_t *) outp;
	geniocb->context_un.ndlp = lpfc_nlp_get(ndlp);

	/* Fill in payload, bp points to frame payload */
	icmd->ulpCommand = CMD_GEN_REQUEST64_CR;

	/* Fill in rest of iocb */
	icmd->un.genreq64.w5.hcsw.Fctl = (SI | LA);
	icmd->un.genreq64.w5.hcsw.Dfctl = 0;
	icmd->un.genreq64.w5.hcsw.Rctl = FC_UNSOL_CTL;
	icmd->un.genreq64.w5.hcsw.Type = FC_COMMON_TRANSPORT_ULP;

	if (!tmo) {
		 /* FC spec states we need 3 * ratov for CT requests */
		tmo = (3 * phba->fc_ratov);
	}
	icmd->ulpTimeout = tmo;
	icmd->ulpBdeCount = 1;
	icmd->ulpLe = 1;
	icmd->ulpClass = CLASS3;
	icmd->ulpContext = ndlp->nlp_rpi;

	if (phba->sli3_options & LPFC_SLI3_NPIV_ENABLED) {
		/* For GEN_REQUEST64_CR, use the RPI */
		icmd->ulpCt_h = 0;
		icmd->ulpCt_l = 0;
	}

	/* Issue GEN REQ IOCB for NPORT <did> */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS,
			 "0119 Issue GEN REQ IOCB to NPORT x%x "
			 "Data: x%x x%x\n",
			 ndlp->nlp_DID, icmd->ulpIoTag,
			 vport->port_state);
	geniocb->iocb_cmpl = cmpl;
	geniocb->drvrTimeout = icmd->ulpTimeout + LPFC_DRVR_TIMEOUT;
	geniocb->vport = vport;
	geniocb->retry = retry;
	rc = lpfc_sli_issue_iocb(phba, pring, geniocb, 0);

	if (rc == IOCB_ERROR) {
		lpfc_sli_release_iocbq(phba, geniocb);
		return 1;
	}

	return 0;
}

static int
lpfc_ct_cmd(struct lpfc_vport *vport, struct lpfc_dmabuf *inmp,
	    struct lpfc_dmabuf *bmp, struct lpfc_nodelist *ndlp,
	    void (*cmpl) (struct lpfc_hba *, struct lpfc_iocbq *,
			  struct lpfc_iocbq *),
	    uint32_t rsp_size, uint8_t retry)
{
	struct lpfc_hba  *phba = vport->phba;
	struct ulp_bde64 *bpl = (struct ulp_bde64 *) bmp->virt;
	struct lpfc_dmabuf *outmp;
	int cnt = 0, status;
	int cmdcode = ((struct lpfc_sli_ct_request *) inmp->virt)->
		CommandResponse.bits.CmdRsp;

	bpl++;			/* Skip past ct request */

	/* Put buffer(s) for ct rsp in bpl */
	outmp = lpfc_alloc_ct_rsp(phba, cmdcode, bpl, rsp_size, &cnt);
	if (!outmp)
		return -ENOMEM;

	status = lpfc_gen_req(vport, bmp, inmp, outmp, cmpl, ndlp, 0,
			      cnt+1, 0, retry);
	if (status) {
		lpfc_free_ct_rsp(phba, outmp);
		return -ENOMEM;
	}
	return 0;
}

struct lpfc_vport *
lpfc_find_vport_by_did(struct lpfc_hba *phba, uint32_t did) {
	struct lpfc_vport *vport_curr;
	unsigned long flags;

	spin_lock_irqsave(&phba->hbalock, flags);
	list_for_each_entry(vport_curr, &phba->port_list, listentry) {
		if ((vport_curr->fc_myDID) && (vport_curr->fc_myDID == did)) {
			spin_unlock_irqrestore(&phba->hbalock, flags);
			return vport_curr;
		}
	}
	spin_unlock_irqrestore(&phba->hbalock, flags);
	return NULL;
}

static int
lpfc_ns_rsp(struct lpfc_vport *vport, struct lpfc_dmabuf *mp, uint32_t Size)
{
	struct lpfc_hba  *phba = vport->phba;
	struct lpfc_sli_ct_request *Response =
		(struct lpfc_sli_ct_request *) mp->virt;
	struct lpfc_nodelist *ndlp = NULL;
	struct lpfc_dmabuf *mlast, *next_mp;
	uint32_t *ctptr = (uint32_t *) & Response->un.gid.PortType;
	uint32_t Did, CTentry;
	int Cnt;
	struct list_head head;

	lpfc_set_disctmo(vport);
	vport->num_disc_nodes = 0;
	vport->fc_ns_retry = 0;


	list_add_tail(&head, &mp->list);
	list_for_each_entry_safe(mp, next_mp, &head, list) {
		mlast = mp;

		Cnt = Size  > FCELSSIZE ? FCELSSIZE : Size;

		Size -= Cnt;

		if (!ctptr) {
			ctptr = (uint32_t *) mlast->virt;
		} else
			Cnt -= 16;	/* subtract length of CT header */

		/* Loop through entire NameServer list of DIDs */
		while (Cnt >= sizeof (uint32_t)) {
			/* Get next DID from NameServer List */
			CTentry = *ctptr++;
			Did = ((be32_to_cpu(CTentry)) & Mask_DID);

			ndlp = NULL;

			/*
			 * Check for rscn processing or not
			 * To conserve rpi's, filter out addresses for other
			 * vports on the same physical HBAs.
			 */
			if ((Did != vport->fc_myDID) &&
			    ((lpfc_find_vport_by_did(phba, Did) == NULL) ||
			     vport->cfg_peer_port_login)) {
				if ((vport->port_type != LPFC_NPIV_PORT) ||
				    (!(vport->ct_flags & FC_CT_RFF_ID)) ||
				    (!vport->cfg_restrict_login)) {
					ndlp = lpfc_setup_disc_node(vport, Did);
					if (ndlp && NLP_CHK_NODE_ACT(ndlp)) {
						lpfc_debugfs_disc_trc(vport,
						LPFC_DISC_TRC_CT,
						"Parse GID_FTrsp: "
						"did:x%x flg:x%x x%x",
						Did, ndlp->nlp_flag,
						vport->fc_flag);

						lpfc_printf_vlog(vport,
							KERN_INFO,
							LOG_DISCOVERY,
							"0238 Process "
							"x%x NameServer Rsp"
							"Data: x%x x%x x%x\n",
							Did, ndlp->nlp_flag,
							vport->fc_flag,
							vport->fc_rscn_id_cnt);
					} else {
						lpfc_debugfs_disc_trc(vport,
						LPFC_DISC_TRC_CT,
						"Skip1 GID_FTrsp: "
						"did:x%x flg:x%x cnt:%d",
						Did, vport->fc_flag,
						vport->fc_rscn_id_cnt);

						lpfc_printf_vlog(vport,
							KERN_INFO,
							LOG_DISCOVERY,
							"0239 Skip x%x "
							"NameServer Rsp Data: "
							"x%x x%x\n",
							Did, vport->fc_flag,
							vport->fc_rscn_id_cnt);
					}

				} else {
					if (!(vport->fc_flag & FC_RSCN_MODE) ||
					(lpfc_rscn_payload_check(vport, Did))) {
						lpfc_debugfs_disc_trc(vport,
						LPFC_DISC_TRC_CT,
						"Query GID_FTrsp: "
						"did:x%x flg:x%x cnt:%d",
						Did, vport->fc_flag,
						vport->fc_rscn_id_cnt);

						/* This NPortID was previously
						 * a FCP target, * Don't even
						 * bother to send GFF_ID.
						 */
						ndlp = lpfc_findnode_did(vport,
							Did);
						if (ndlp &&
						    NLP_CHK_NODE_ACT(ndlp)
						    && (ndlp->nlp_type &
						     NLP_FCP_TARGET))
							lpfc_setup_disc_node
								(vport, Did);
						else if (lpfc_ns_cmd(vport,
							SLI_CTNS_GFF_ID,
							0, Did) == 0)
							vport->num_disc_nodes++;
					}
					else {
						lpfc_debugfs_disc_trc(vport,
						LPFC_DISC_TRC_CT,
						"Skip2 GID_FTrsp: "
						"did:x%x flg:x%x cnt:%d",
						Did, vport->fc_flag,
						vport->fc_rscn_id_cnt);

						lpfc_printf_vlog(vport,
							KERN_INFO,
							LOG_DISCOVERY,
							"0245 Skip x%x "
							"NameServer Rsp Data: "
							"x%x x%x\n",
							Did, vport->fc_flag,
							vport->fc_rscn_id_cnt);
					}
				}
			}
			if (CTentry & (be32_to_cpu(SLI_CT_LAST_ENTRY)))
				goto nsout1;
			Cnt -= sizeof (uint32_t);
		}
		ctptr = NULL;

	}

nsout1:
	list_del(&head);
	return 0;
}

static void
lpfc_cmpl_ct_cmd_gid_ft(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
			struct lpfc_iocbq *rspiocb)
{
	struct lpfc_vport *vport = cmdiocb->vport;
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	IOCB_t *irsp;
	struct lpfc_dmabuf *bmp;
	struct lpfc_dmabuf *outp;
	struct lpfc_sli_ct_request *CTrsp;
	struct lpfc_nodelist *ndlp;
	int rc;

	/* First save ndlp, before we overwrite it */
	ndlp = cmdiocb->context_un.ndlp;

	/* we pass cmdiocb to state machine which needs rspiocb as well */
	cmdiocb->context_un.rsp_iocb = rspiocb;

	outp = (struct lpfc_dmabuf *) cmdiocb->context2;
	bmp = (struct lpfc_dmabuf *) cmdiocb->context3;
	irsp = &rspiocb->iocb;

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_CT,
		 "GID_FT cmpl:     status:x%x/x%x rtry:%d",
		irsp->ulpStatus, irsp->un.ulpWord[4], vport->fc_ns_retry);

	/* Don't bother processing response if vport is being torn down. */
	if (vport->load_flag & FC_UNLOADING) {
		if (vport->fc_flag & FC_RSCN_MODE)
			lpfc_els_flush_rscn(vport);
		goto out;
	}

	if (lpfc_els_chk_latt(vport)) {
		lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
				 "0216 Link event during NS query\n");
		if (vport->fc_flag & FC_RSCN_MODE)
			lpfc_els_flush_rscn(vport);
		lpfc_vport_set_state(vport, FC_VPORT_FAILED);
		goto out;
	}
	if (lpfc_error_lost_link(irsp)) {
		lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
				 "0226 NS query failed due to link event\n");
		if (vport->fc_flag & FC_RSCN_MODE)
			lpfc_els_flush_rscn(vport);
		goto out;
	}
	if (irsp->ulpStatus) {
		/* Check for retry */
		if (vport->fc_ns_retry < LPFC_MAX_NS_RETRY) {
			if (irsp->ulpStatus != IOSTAT_LOCAL_REJECT ||
			    irsp->un.ulpWord[4] != IOERR_NO_RESOURCES)
				vport->fc_ns_retry++;

			/* CT command is being retried */
			rc = lpfc_ns_cmd(vport, SLI_CTNS_GID_FT,
					 vport->fc_ns_retry, 0);
			if (rc == 0)
				goto out;
		}
		if (vport->fc_flag & FC_RSCN_MODE)
			lpfc_els_flush_rscn(vport);
		lpfc_vport_set_state(vport, FC_VPORT_FAILED);
		lpfc_printf_vlog(vport, KERN_ERR, LOG_ELS,
				 "0257 GID_FT Query error: 0x%x 0x%x\n",
				 irsp->ulpStatus, vport->fc_ns_retry);
	} else {
		/* Good status, continue checking */
		CTrsp = (struct lpfc_sli_ct_request *) outp->virt;
		if (CTrsp->CommandResponse.bits.CmdRsp ==
		    be16_to_cpu(SLI_CT_RESPONSE_FS_ACC)) {
			lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
					 "0208 NameServer Rsp Data: x%x\n",
					 vport->fc_flag);
			lpfc_ns_rsp(vport, outp,
				    (uint32_t) (irsp->un.genreq64.bdl.bdeSize));
		} else if (CTrsp->CommandResponse.bits.CmdRsp ==
			   be16_to_cpu(SLI_CT_RESPONSE_FS_RJT)) {
			/* NameServer Rsp Error */
			if ((CTrsp->ReasonCode == SLI_CT_UNABLE_TO_PERFORM_REQ)
			    && (CTrsp->Explanation == SLI_CT_NO_FC4_TYPES)) {
				lpfc_printf_vlog(vport, KERN_INFO,
					LOG_DISCOVERY,
					"0269 No NameServer Entries "
					"Data: x%x x%x x%x x%x\n",
					CTrsp->CommandResponse.bits.CmdRsp,
					(uint32_t) CTrsp->ReasonCode,
					(uint32_t) CTrsp->Explanation,
					vport->fc_flag);

				lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_CT,
				"GID_FT no entry  cmd:x%x rsn:x%x exp:x%x",
				(uint32_t)CTrsp->CommandResponse.bits.CmdRsp,
				(uint32_t) CTrsp->ReasonCode,
				(uint32_t) CTrsp->Explanation);
			} else {
				lpfc_printf_vlog(vport, KERN_INFO,
					LOG_DISCOVERY,
					"0240 NameServer Rsp Error "
					"Data: x%x x%x x%x x%x\n",
					CTrsp->CommandResponse.bits.CmdRsp,
					(uint32_t) CTrsp->ReasonCode,
					(uint32_t) CTrsp->Explanation,
					vport->fc_flag);

				lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_CT,
				"GID_FT rsp err1  cmd:x%x rsn:x%x exp:x%x",
				(uint32_t)CTrsp->CommandResponse.bits.CmdRsp,
				(uint32_t) CTrsp->ReasonCode,
				(uint32_t) CTrsp->Explanation);
			}


		} else {
			/* NameServer Rsp Error */
			lpfc_printf_vlog(vport, KERN_ERR, LOG_DISCOVERY,
					"0241 NameServer Rsp Error "
					"Data: x%x x%x x%x x%x\n",
					CTrsp->CommandResponse.bits.CmdRsp,
					(uint32_t) CTrsp->ReasonCode,
					(uint32_t) CTrsp->Explanation,
					vport->fc_flag);

			lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_CT,
				"GID_FT rsp err2  cmd:x%x rsn:x%x exp:x%x",
				(uint32_t)CTrsp->CommandResponse.bits.CmdRsp,
				(uint32_t) CTrsp->ReasonCode,
				(uint32_t) CTrsp->Explanation);
		}
	}
	/* Link up / RSCN discovery */
	if (vport->num_disc_nodes == 0) {
		/*
		 * The driver has cycled through all Nports in the RSCN payload.
		 * Complete the handling by cleaning up and marking the
		 * current driver state.
		 */
		if (vport->port_state >= LPFC_DISC_AUTH) {
			if (vport->fc_flag & FC_RSCN_MODE) {
				lpfc_els_flush_rscn(vport);
				spin_lock_irq(shost->host_lock);
				vport->fc_flag |= FC_RSCN_MODE; /* RSCN still */
				spin_unlock_irq(shost->host_lock);
			}
			else
				lpfc_els_flush_rscn(vport);
		}

		lpfc_disc_start(vport);
	}
out:
	cmdiocb->context_un.ndlp = ndlp; /* Now restore ndlp for free */
	lpfc_ct_free_iocb(phba, cmdiocb);
	return;
}

static void
lpfc_cmpl_ct_cmd_gff_id(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
			struct lpfc_iocbq *rspiocb)
{
	struct lpfc_vport *vport = cmdiocb->vport;
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	IOCB_t *irsp = &rspiocb->iocb;
	struct lpfc_dmabuf *inp = (struct lpfc_dmabuf *) cmdiocb->context1;
	struct lpfc_dmabuf *outp = (struct lpfc_dmabuf *) cmdiocb->context2;
	struct lpfc_sli_ct_request *CTrsp;
	int did, rc, retry;
	uint8_t fbits;
	struct lpfc_nodelist *ndlp;

	did = ((struct lpfc_sli_ct_request *) inp->virt)->un.gff.PortId;
	did = be32_to_cpu(did);

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_CT,
		"GFF_ID cmpl:     status:x%x/x%x did:x%x",
		irsp->ulpStatus, irsp->un.ulpWord[4], did);

	if (irsp->ulpStatus == IOSTAT_SUCCESS) {
		/* Good status, continue checking */
		CTrsp = (struct lpfc_sli_ct_request *) outp->virt;
		fbits = CTrsp->un.gff_acc.fbits[FCP_TYPE_FEATURE_OFFSET];

		if (CTrsp->CommandResponse.bits.CmdRsp ==
		    be16_to_cpu(SLI_CT_RESPONSE_FS_ACC)) {
			if ((fbits & FC4_FEATURE_INIT) &&
			    !(fbits & FC4_FEATURE_TARGET)) {
				lpfc_printf_vlog(vport, KERN_INFO,
						 LOG_DISCOVERY,
						 "0270 Skip x%x GFF "
						 "NameServer Rsp Data: (init) "
						 "x%x x%x\n", did, fbits,
						 vport->fc_rscn_id_cnt);
				goto out;
			}
		}
	}
	else {
		/* Check for retry */
		if (cmdiocb->retry < LPFC_MAX_NS_RETRY) {
			retry = 1;
			if (irsp->ulpStatus == IOSTAT_LOCAL_REJECT) {
				switch (irsp->un.ulpWord[4]) {
				case IOERR_NO_RESOURCES:
					/* We don't increment the retry
					 * count for this case.
					 */
					break;
				case IOERR_LINK_DOWN:
				case IOERR_SLI_ABORTED:
				case IOERR_SLI_DOWN:
					retry = 0;
					break;
				default:
					cmdiocb->retry++;
				}
			}
			else
				cmdiocb->retry++;

			if (retry) {
				/* CT command is being retried */
				rc = lpfc_ns_cmd(vport, SLI_CTNS_GFF_ID,
					 cmdiocb->retry, did);
				if (rc == 0) {
					/* success */
					lpfc_ct_free_iocb(phba, cmdiocb);
					return;
				}
			}
		}
		lpfc_printf_vlog(vport, KERN_ERR, LOG_DISCOVERY,
				 "0267 NameServer GFF Rsp "
				 "x%x Error (%d %d) Data: x%x x%x\n",
				 did, irsp->ulpStatus, irsp->un.ulpWord[4],
				 vport->fc_flag, vport->fc_rscn_id_cnt);
	}

	/* This is a target port, unregistered port, or the GFF_ID failed */
	ndlp = lpfc_setup_disc_node(vport, did);
	if (ndlp && NLP_CHK_NODE_ACT(ndlp)) {
		lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
				 "0242 Process x%x GFF "
				 "NameServer Rsp Data: x%x x%x x%x\n",
				 did, ndlp->nlp_flag, vport->fc_flag,
				 vport->fc_rscn_id_cnt);
	} else {
		lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
				 "0243 Skip x%x GFF "
				 "NameServer Rsp Data: x%x x%x\n", did,
				 vport->fc_flag, vport->fc_rscn_id_cnt);
	}
out:
	/* Link up / RSCN discovery */
	if (vport->num_disc_nodes)
		vport->num_disc_nodes--;
	if (vport->num_disc_nodes == 0) {
		/*
		 * The driver has cycled through all Nports in the RSCN payload.
		 * Complete the handling by cleaning up and marking the
		 * current driver state.
		 */
		if (vport->port_state >= LPFC_DISC_AUTH) {
			if (vport->fc_flag & FC_RSCN_MODE) {
				lpfc_els_flush_rscn(vport);
				spin_lock_irq(shost->host_lock);
				vport->fc_flag |= FC_RSCN_MODE; /* RSCN still */
				spin_unlock_irq(shost->host_lock);
			}
			else
				lpfc_els_flush_rscn(vport);
		}
		lpfc_disc_start(vport);
	}
	lpfc_ct_free_iocb(phba, cmdiocb);
	return;
}


static void
lpfc_cmpl_ct(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
	     struct lpfc_iocbq *rspiocb)
{
	struct lpfc_vport *vport = cmdiocb->vport;
	struct lpfc_dmabuf *inp;
	struct lpfc_dmabuf *outp;
	IOCB_t *irsp;
	struct lpfc_sli_ct_request *CTrsp;
	struct lpfc_nodelist *ndlp;
	int cmdcode, rc;
	uint8_t retry;
	uint32_t latt;

	/* First save ndlp, before we overwrite it */
	ndlp = cmdiocb->context_un.ndlp;

	/* we pass cmdiocb to state machine which needs rspiocb as well */
	cmdiocb->context_un.rsp_iocb = rspiocb;

	inp = (struct lpfc_dmabuf *) cmdiocb->context1;
	outp = (struct lpfc_dmabuf *) cmdiocb->context2;
	irsp = &rspiocb->iocb;

	cmdcode = be16_to_cpu(((struct lpfc_sli_ct_request *) inp->virt)->
					CommandResponse.bits.CmdRsp);
	CTrsp = (struct lpfc_sli_ct_request *) outp->virt;

	latt = lpfc_els_chk_latt(vport);

	/* RFT request completes status <ulpStatus> CmdRsp <CmdRsp> */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
			 "0209 CT Request completes, latt %d, "
			 "ulpStatus x%x CmdRsp x%x, Context x%x, Tag x%x\n",
			 latt, irsp->ulpStatus,
			 CTrsp->CommandResponse.bits.CmdRsp,
			 cmdiocb->iocb.ulpContext, cmdiocb->iocb.ulpIoTag);

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_CT,
		"CT cmd cmpl:     status:x%x/x%x cmd:x%x",
		irsp->ulpStatus, irsp->un.ulpWord[4], cmdcode);

	if (irsp->ulpStatus) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_DISCOVERY,
				 "0268 NS cmd %x Error (%d %d)\n",
				 cmdcode, irsp->ulpStatus, irsp->un.ulpWord[4]);

		if ((irsp->ulpStatus == IOSTAT_LOCAL_REJECT) &&
			((irsp->un.ulpWord[4] == IOERR_SLI_DOWN) ||
			 (irsp->un.ulpWord[4] == IOERR_SLI_ABORTED)))
			goto out;

		retry = cmdiocb->retry;
		if (retry >= LPFC_MAX_NS_RETRY)
			goto out;

		retry++;
		lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
				 "0250 Retrying NS cmd %x\n", cmdcode);
		rc = lpfc_ns_cmd(vport, cmdcode, retry, 0);
		if (rc == 0)
			goto out;
	}

out:
	cmdiocb->context_un.ndlp = ndlp; /* Now restore ndlp for free */
	lpfc_ct_free_iocb(phba, cmdiocb);
	return;
}

static void
lpfc_cmpl_ct_cmd_rft_id(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
			struct lpfc_iocbq *rspiocb)
{
	IOCB_t *irsp = &rspiocb->iocb;
	struct lpfc_vport *vport = cmdiocb->vport;

	if (irsp->ulpStatus == IOSTAT_SUCCESS) {
		struct lpfc_dmabuf *outp;
		struct lpfc_sli_ct_request *CTrsp;

		outp = (struct lpfc_dmabuf *) cmdiocb->context2;
		CTrsp = (struct lpfc_sli_ct_request *) outp->virt;
		if (CTrsp->CommandResponse.bits.CmdRsp ==
		    be16_to_cpu(SLI_CT_RESPONSE_FS_ACC))
			vport->ct_flags |= FC_CT_RFT_ID;
	}
	lpfc_cmpl_ct(phba, cmdiocb, rspiocb);
	return;
}

static void
lpfc_cmpl_ct_cmd_rnn_id(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
			struct lpfc_iocbq *rspiocb)
{
	IOCB_t *irsp = &rspiocb->iocb;
	struct lpfc_vport *vport = cmdiocb->vport;

	if (irsp->ulpStatus == IOSTAT_SUCCESS) {
		struct lpfc_dmabuf *outp;
		struct lpfc_sli_ct_request *CTrsp;

		outp = (struct lpfc_dmabuf *) cmdiocb->context2;
		CTrsp = (struct lpfc_sli_ct_request *) outp->virt;
		if (CTrsp->CommandResponse.bits.CmdRsp ==
		    be16_to_cpu(SLI_CT_RESPONSE_FS_ACC))
			vport->ct_flags |= FC_CT_RNN_ID;
	}
	lpfc_cmpl_ct(phba, cmdiocb, rspiocb);
	return;
}

static void
lpfc_cmpl_ct_cmd_rspn_id(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
			 struct lpfc_iocbq *rspiocb)
{
	IOCB_t *irsp = &rspiocb->iocb;
	struct lpfc_vport *vport = cmdiocb->vport;

	if (irsp->ulpStatus == IOSTAT_SUCCESS) {
		struct lpfc_dmabuf *outp;
		struct lpfc_sli_ct_request *CTrsp;

		outp = (struct lpfc_dmabuf *) cmdiocb->context2;
		CTrsp = (struct lpfc_sli_ct_request *) outp->virt;
		if (CTrsp->CommandResponse.bits.CmdRsp ==
		    be16_to_cpu(SLI_CT_RESPONSE_FS_ACC))
			vport->ct_flags |= FC_CT_RSPN_ID;
	}
	lpfc_cmpl_ct(phba, cmdiocb, rspiocb);
	return;
}

static void
lpfc_cmpl_ct_cmd_rsnn_nn(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
			 struct lpfc_iocbq *rspiocb)
{
	IOCB_t *irsp = &rspiocb->iocb;
	struct lpfc_vport *vport = cmdiocb->vport;

	if (irsp->ulpStatus == IOSTAT_SUCCESS) {
		struct lpfc_dmabuf *outp;
		struct lpfc_sli_ct_request *CTrsp;

		outp = (struct lpfc_dmabuf *) cmdiocb->context2;
		CTrsp = (struct lpfc_sli_ct_request *) outp->virt;
		if (CTrsp->CommandResponse.bits.CmdRsp ==
		    be16_to_cpu(SLI_CT_RESPONSE_FS_ACC))
			vport->ct_flags |= FC_CT_RSNN_NN;
	}
	lpfc_cmpl_ct(phba, cmdiocb, rspiocb);
	return;
}

static void
lpfc_cmpl_ct_cmd_da_id(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
 struct lpfc_iocbq *rspiocb)
{
	struct lpfc_vport *vport = cmdiocb->vport;

	/* even if it fails we will act as though it succeeded. */
	vport->ct_flags = 0;
	lpfc_cmpl_ct(phba, cmdiocb, rspiocb);
	return;
}

static void
lpfc_cmpl_ct_cmd_rff_id(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
			struct lpfc_iocbq *rspiocb)
{
	IOCB_t *irsp = &rspiocb->iocb;
	struct lpfc_vport *vport = cmdiocb->vport;

	if (irsp->ulpStatus == IOSTAT_SUCCESS) {
		struct lpfc_dmabuf *outp;
		struct lpfc_sli_ct_request *CTrsp;

		outp = (struct lpfc_dmabuf *) cmdiocb->context2;
		CTrsp = (struct lpfc_sli_ct_request *) outp->virt;
		if (CTrsp->CommandResponse.bits.CmdRsp ==
		    be16_to_cpu(SLI_CT_RESPONSE_FS_ACC))
			vport->ct_flags |= FC_CT_RFF_ID;
	}
	lpfc_cmpl_ct(phba, cmdiocb, rspiocb);
	return;
}

int
lpfc_vport_symbolic_port_name(struct lpfc_vport *vport, char *symbol,
	size_t size)
{
	int n;
	uint8_t *wwn = vport->phba->wwpn;

	n = snprintf(symbol, size,
		     "Emulex PPN-%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
		     wwn[0], wwn[1], wwn[2], wwn[3],
		     wwn[4], wwn[5], wwn[6], wwn[7]);

	if (vport->port_type == LPFC_PHYSICAL_PORT)
		return n;

	if (n < size)
		n += snprintf(symbol + n, size - n, " VPort-%d", vport->vpi);

	if (n < size &&
	    strlen(vport->fc_vport->symbolic_name))
		n += snprintf(symbol + n, size - n, " VName-%s",
			      vport->fc_vport->symbolic_name);
	return n;
}

int
lpfc_vport_symbolic_node_name(struct lpfc_vport *vport, char *symbol,
	size_t size)
{
	char fwrev[16];
	int n;

	lpfc_decode_firmware_rev(vport->phba, fwrev, 0);

	n = snprintf(symbol, size, "Emulex %s FV%s DV%s",
		vport->phba->ModelName, fwrev, lpfc_release_version);
	return n;
}

/*
 * lpfc_ns_cmd
 * Description:
 *    Issue Cmd to NameServer
 *       SLI_CTNS_GID_FT
 *       LI_CTNS_RFT_ID
 */
int
lpfc_ns_cmd(struct lpfc_vport *vport, int cmdcode,
	    uint8_t retry, uint32_t context)
{
	struct lpfc_nodelist * ndlp;
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_dmabuf *mp, *bmp;
	struct lpfc_sli_ct_request *CtReq;
	struct ulp_bde64 *bpl;
	void (*cmpl) (struct lpfc_hba *, struct lpfc_iocbq *,
		      struct lpfc_iocbq *) = NULL;
	uint32_t rsp_size = 1024;
	size_t   size;
	int rc = 0;

	ndlp = lpfc_findnode_did(vport, NameServer_DID);
	if (!ndlp || !NLP_CHK_NODE_ACT(ndlp)
	    || ndlp->nlp_state != NLP_STE_UNMAPPED_NODE) {
		rc=1;
		goto ns_cmd_exit;
	}

	/* fill in BDEs for command */
	/* Allocate buffer for command payload */
	mp = kmalloc(sizeof (struct lpfc_dmabuf), GFP_KERNEL);
	if (!mp) {
		rc=2;
		goto ns_cmd_exit;
	}

	INIT_LIST_HEAD(&mp->list);
	mp->virt = lpfc_mbuf_alloc(phba, MEM_PRI, &(mp->phys));
	if (!mp->virt) {
		rc=3;
		goto ns_cmd_free_mp;
	}

	/* Allocate buffer for Buffer ptr list */
	bmp = kmalloc(sizeof (struct lpfc_dmabuf), GFP_KERNEL);
	if (!bmp) {
		rc=4;
		goto ns_cmd_free_mpvirt;
	}

	INIT_LIST_HEAD(&bmp->list);
	bmp->virt = lpfc_mbuf_alloc(phba, MEM_PRI, &(bmp->phys));
	if (!bmp->virt) {
		rc=5;
		goto ns_cmd_free_bmp;
	}

	/* NameServer Req */
	lpfc_printf_vlog(vport, KERN_INFO ,LOG_DISCOVERY,
			 "0236 NameServer Req Data: x%x x%x x%x\n",
			 cmdcode, vport->fc_flag, vport->fc_rscn_id_cnt);

	bpl = (struct ulp_bde64 *) bmp->virt;
	memset(bpl, 0, sizeof(struct ulp_bde64));
	bpl->addrHigh = le32_to_cpu(putPaddrHigh(mp->phys) );
	bpl->addrLow = le32_to_cpu(putPaddrLow(mp->phys) );
	bpl->tus.f.bdeFlags = 0;
	if (cmdcode == SLI_CTNS_GID_FT)
		bpl->tus.f.bdeSize = GID_REQUEST_SZ;
	else if (cmdcode == SLI_CTNS_GFF_ID)
		bpl->tus.f.bdeSize = GFF_REQUEST_SZ;
	else if (cmdcode == SLI_CTNS_RFT_ID)
		bpl->tus.f.bdeSize = RFT_REQUEST_SZ;
	else if (cmdcode == SLI_CTNS_RNN_ID)
		bpl->tus.f.bdeSize = RNN_REQUEST_SZ;
	else if (cmdcode == SLI_CTNS_RSPN_ID)
		bpl->tus.f.bdeSize = RSPN_REQUEST_SZ;
	else if (cmdcode == SLI_CTNS_RSNN_NN)
		bpl->tus.f.bdeSize = RSNN_REQUEST_SZ;
	else if (cmdcode == SLI_CTNS_DA_ID)
		bpl->tus.f.bdeSize = DA_ID_REQUEST_SZ;
	else if (cmdcode == SLI_CTNS_RFF_ID)
		bpl->tus.f.bdeSize = RFF_REQUEST_SZ;
	else
		bpl->tus.f.bdeSize = 0;
	bpl->tus.w = le32_to_cpu(bpl->tus.w);

	CtReq = (struct lpfc_sli_ct_request *) mp->virt;
	memset(CtReq, 0, sizeof (struct lpfc_sli_ct_request));
	CtReq->RevisionId.bits.Revision = SLI_CT_REVISION;
	CtReq->RevisionId.bits.InId = 0;
	CtReq->FsType = SLI_CT_DIRECTORY_SERVICE;
	CtReq->FsSubType = SLI_CT_DIRECTORY_NAME_SERVER;
	CtReq->CommandResponse.bits.Size = 0;
	switch (cmdcode) {
	case SLI_CTNS_GID_FT:
		CtReq->CommandResponse.bits.CmdRsp =
		    be16_to_cpu(SLI_CTNS_GID_FT);
		CtReq->un.gid.Fc4Type = SLI_CTPT_FCP;
		if (vport->port_state < LPFC_NS_QRY)
			vport->port_state = LPFC_NS_QRY;
		lpfc_set_disctmo(vport);
		cmpl = lpfc_cmpl_ct_cmd_gid_ft;
		rsp_size = FC_MAX_NS_RSP;
		break;

	case SLI_CTNS_GFF_ID:
		CtReq->CommandResponse.bits.CmdRsp =
			be16_to_cpu(SLI_CTNS_GFF_ID);
		CtReq->un.gff.PortId = cpu_to_be32(context);
		cmpl = lpfc_cmpl_ct_cmd_gff_id;
		break;

	case SLI_CTNS_RFT_ID:
		vport->ct_flags &= ~FC_CT_RFT_ID;
		CtReq->CommandResponse.bits.CmdRsp =
		    be16_to_cpu(SLI_CTNS_RFT_ID);
		CtReq->un.rft.PortId = cpu_to_be32(vport->fc_myDID);
		CtReq->un.rft.fcpReg = 1;
		cmpl = lpfc_cmpl_ct_cmd_rft_id;
		break;

	case SLI_CTNS_RNN_ID:
		vport->ct_flags &= ~FC_CT_RNN_ID;
		CtReq->CommandResponse.bits.CmdRsp =
		    be16_to_cpu(SLI_CTNS_RNN_ID);
		CtReq->un.rnn.PortId = cpu_to_be32(vport->fc_myDID);
		memcpy(CtReq->un.rnn.wwnn,  &vport->fc_nodename,
		       sizeof (struct lpfc_name));
		cmpl = lpfc_cmpl_ct_cmd_rnn_id;
		break;

	case SLI_CTNS_RSPN_ID:
		vport->ct_flags &= ~FC_CT_RSPN_ID;
		CtReq->CommandResponse.bits.CmdRsp =
		    be16_to_cpu(SLI_CTNS_RSPN_ID);
		CtReq->un.rspn.PortId = cpu_to_be32(vport->fc_myDID);
		size = sizeof(CtReq->un.rspn.symbname);
		CtReq->un.rspn.len =
			lpfc_vport_symbolic_port_name(vport,
			CtReq->un.rspn.symbname, size);
		cmpl = lpfc_cmpl_ct_cmd_rspn_id;
		break;
	case SLI_CTNS_RSNN_NN:
		vport->ct_flags &= ~FC_CT_RSNN_NN;
		CtReq->CommandResponse.bits.CmdRsp =
		    be16_to_cpu(SLI_CTNS_RSNN_NN);
		memcpy(CtReq->un.rsnn.wwnn, &vport->fc_nodename,
		       sizeof (struct lpfc_name));
		size = sizeof(CtReq->un.rsnn.symbname);
		CtReq->un.rsnn.len =
			lpfc_vport_symbolic_node_name(vport,
			CtReq->un.rsnn.symbname, size);
		cmpl = lpfc_cmpl_ct_cmd_rsnn_nn;
		break;
	case SLI_CTNS_DA_ID:
		/* Implement DA_ID Nameserver request */
		CtReq->CommandResponse.bits.CmdRsp =
			be16_to_cpu(SLI_CTNS_DA_ID);
		CtReq->un.da_id.port_id = cpu_to_be32(vport->fc_myDID);
		cmpl = lpfc_cmpl_ct_cmd_da_id;
		break;
	case SLI_CTNS_RFF_ID:
		vport->ct_flags &= ~FC_CT_RFF_ID;
		CtReq->CommandResponse.bits.CmdRsp =
		    be16_to_cpu(SLI_CTNS_RFF_ID);
		CtReq->un.rff.PortId = cpu_to_be32(vport->fc_myDID);;
		CtReq->un.rff.fbits = FC4_FEATURE_INIT;
		CtReq->un.rff.type_code = FC_FCP_DATA;
		cmpl = lpfc_cmpl_ct_cmd_rff_id;
		break;
	}
	/* The lpfc_ct_cmd/lpfc_get_req shall increment ndlp reference count
	 * to hold ndlp reference for the corresponding callback function.
	 */
	if (!lpfc_ct_cmd(vport, mp, bmp, ndlp, cmpl, rsp_size, retry)) {
		/* On success, The cmpl function will free the buffers */
		lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_CT,
			"Issue CT cmd:    cmd:x%x did:x%x",
			cmdcode, ndlp->nlp_DID, 0);
		return 0;
	}
	rc=6;

	/* Decrement ndlp reference count to release ndlp reference held
	 * for the failed command's callback function.
	 */
	lpfc_nlp_put(ndlp);

	lpfc_mbuf_free(phba, bmp->virt, bmp->phys);
ns_cmd_free_bmp:
	kfree(bmp);
ns_cmd_free_mpvirt:
	lpfc_mbuf_free(phba, mp->virt, mp->phys);
ns_cmd_free_mp:
	kfree(mp);
ns_cmd_exit:
	lpfc_printf_vlog(vport, KERN_ERR, LOG_DISCOVERY,
			 "0266 Issue NameServer Req x%x err %d Data: x%x x%x\n",
			 cmdcode, rc, vport->fc_flag, vport->fc_rscn_id_cnt);
	return 1;
}

static void
lpfc_cmpl_ct_cmd_fdmi(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
		      struct lpfc_iocbq * rspiocb)
{
	struct lpfc_dmabuf *inp = cmdiocb->context1;
	struct lpfc_dmabuf *outp = cmdiocb->context2;
	struct lpfc_sli_ct_request *CTrsp = outp->virt;
	struct lpfc_sli_ct_request *CTcmd = inp->virt;
	struct lpfc_nodelist *ndlp;
	uint16_t fdmi_cmd = CTcmd->CommandResponse.bits.CmdRsp;
	uint16_t fdmi_rsp = CTrsp->CommandResponse.bits.CmdRsp;
	struct lpfc_vport *vport = cmdiocb->vport;
	IOCB_t *irsp = &rspiocb->iocb;
	uint32_t latt;

	latt = lpfc_els_chk_latt(vport);

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_CT,
		"FDMI cmpl:       status:x%x/x%x latt:%d",
		irsp->ulpStatus, irsp->un.ulpWord[4], latt);

	if (latt || irsp->ulpStatus) {
		lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
				 "0229 FDMI cmd %04x failed, latt = %d "
				 "ulpStatus: x%x, rid x%x\n",
				 be16_to_cpu(fdmi_cmd), latt, irsp->ulpStatus,
				 irsp->un.ulpWord[4]);
		lpfc_ct_free_iocb(phba, cmdiocb);
		return;
	}

	ndlp = lpfc_findnode_did(vport, FDMI_DID);
	if (!ndlp || !NLP_CHK_NODE_ACT(ndlp))
		goto fail_out;

	if (fdmi_rsp == be16_to_cpu(SLI_CT_RESPONSE_FS_RJT)) {
		/* FDMI rsp failed */
		lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
				 "0220 FDMI rsp failed Data: x%x\n",
				 be16_to_cpu(fdmi_cmd));
	}

	switch (be16_to_cpu(fdmi_cmd)) {
	case SLI_MGMT_RHBA:
		lpfc_fdmi_cmd(vport, ndlp, SLI_MGMT_RPA);
		break;

	case SLI_MGMT_RPA:
		break;

	case SLI_MGMT_DHBA:
		lpfc_fdmi_cmd(vport, ndlp, SLI_MGMT_DPRT);
		break;

	case SLI_MGMT_DPRT:
		lpfc_fdmi_cmd(vport, ndlp, SLI_MGMT_RHBA);
		break;
	}

fail_out:
	lpfc_ct_free_iocb(phba, cmdiocb);
	return;
}

int
lpfc_fdmi_cmd(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp, int cmdcode)
{
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_dmabuf *mp, *bmp;
	struct lpfc_sli_ct_request *CtReq;
	struct ulp_bde64 *bpl;
	uint32_t size;
	REG_HBA *rh;
	PORT_ENTRY *pe;
	REG_PORT_ATTRIBUTE *pab;
	ATTRIBUTE_BLOCK *ab;
	ATTRIBUTE_ENTRY *ae;
	void (*cmpl) (struct lpfc_hba *, struct lpfc_iocbq *,
		      struct lpfc_iocbq *);


	/* fill in BDEs for command */
	/* Allocate buffer for command payload */
	mp = kmalloc(sizeof (struct lpfc_dmabuf), GFP_KERNEL);
	if (!mp)
		goto fdmi_cmd_exit;

	mp->virt = lpfc_mbuf_alloc(phba, 0, &(mp->phys));
	if (!mp->virt)
		goto fdmi_cmd_free_mp;

	/* Allocate buffer for Buffer ptr list */
	bmp = kmalloc(sizeof (struct lpfc_dmabuf), GFP_KERNEL);
	if (!bmp)
		goto fdmi_cmd_free_mpvirt;

	bmp->virt = lpfc_mbuf_alloc(phba, 0, &(bmp->phys));
	if (!bmp->virt)
		goto fdmi_cmd_free_bmp;

	INIT_LIST_HEAD(&mp->list);
	INIT_LIST_HEAD(&bmp->list);

	/* FDMI request */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
			 "0218 FDMI Request Data: x%x x%x x%x\n",
			 vport->fc_flag, vport->port_state, cmdcode);
	CtReq = (struct lpfc_sli_ct_request *) mp->virt;

	memset(CtReq, 0, sizeof(struct lpfc_sli_ct_request));
	CtReq->RevisionId.bits.Revision = SLI_CT_REVISION;
	CtReq->RevisionId.bits.InId = 0;

	CtReq->FsType = SLI_CT_MANAGEMENT_SERVICE;
	CtReq->FsSubType = SLI_CT_FDMI_Subtypes;
	size = 0;

	switch (cmdcode) {
	case SLI_MGMT_RHBA:
		{
			lpfc_vpd_t *vp = &phba->vpd;
			uint32_t i, j, incr;
			int len;

			CtReq->CommandResponse.bits.CmdRsp =
			    be16_to_cpu(SLI_MGMT_RHBA);
			CtReq->CommandResponse.bits.Size = 0;
			rh = (REG_HBA *) & CtReq->un.PortID;
			memcpy(&rh->hi.PortName, &vport->fc_sparam.portName,
			       sizeof (struct lpfc_name));
			/* One entry (port) per adapter */
			rh->rpl.EntryCnt = be32_to_cpu(1);
			memcpy(&rh->rpl.pe, &vport->fc_sparam.portName,
			       sizeof (struct lpfc_name));

			/* point to the HBA attribute block */
			size = 2 * sizeof (struct lpfc_name) + FOURBYTES;
			ab = (ATTRIBUTE_BLOCK *) ((uint8_t *) rh + size);
			ab->EntryCnt = 0;

			/* Point to the beginning of the first HBA attribute
			   entry */
			/* #1 HBA attribute entry */
			size += FOURBYTES;
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh + size);
			ae->ad.bits.AttrType = be16_to_cpu(NODE_NAME);
			ae->ad.bits.AttrLen =  be16_to_cpu(FOURBYTES
						+ sizeof (struct lpfc_name));
			memcpy(&ae->un.NodeName, &vport->fc_sparam.nodeName,
			       sizeof (struct lpfc_name));
			ab->EntryCnt++;
			size += FOURBYTES + sizeof (struct lpfc_name);

			/* #2 HBA attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh + size);
			ae->ad.bits.AttrType = be16_to_cpu(MANUFACTURER);
			strcpy(ae->un.Manufacturer, "Emulex Corporation");
			len = strlen(ae->un.Manufacturer);
			len += (len & 3) ? (4 - (len & 3)) : 4;
			ae->ad.bits.AttrLen = be16_to_cpu(FOURBYTES + len);
			ab->EntryCnt++;
			size += FOURBYTES + len;

			/* #3 HBA attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh + size);
			ae->ad.bits.AttrType = be16_to_cpu(SERIAL_NUMBER);
			strcpy(ae->un.SerialNumber, phba->SerialNumber);
			len = strlen(ae->un.SerialNumber);
			len += (len & 3) ? (4 - (len & 3)) : 4;
			ae->ad.bits.AttrLen = be16_to_cpu(FOURBYTES + len);
			ab->EntryCnt++;
			size += FOURBYTES + len;

			/* #4 HBA attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh + size);
			ae->ad.bits.AttrType = be16_to_cpu(MODEL);
			strcpy(ae->un.Model, phba->ModelName);
			len = strlen(ae->un.Model);
			len += (len & 3) ? (4 - (len & 3)) : 4;
			ae->ad.bits.AttrLen = be16_to_cpu(FOURBYTES + len);
			ab->EntryCnt++;
			size += FOURBYTES + len;

			/* #5 HBA attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh + size);
			ae->ad.bits.AttrType = be16_to_cpu(MODEL_DESCRIPTION);
			strcpy(ae->un.ModelDescription, phba->ModelDesc);
			len = strlen(ae->un.ModelDescription);
			len += (len & 3) ? (4 - (len & 3)) : 4;
			ae->ad.bits.AttrLen = be16_to_cpu(FOURBYTES + len);
			ab->EntryCnt++;
			size += FOURBYTES + len;

			/* #6 HBA attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh + size);
			ae->ad.bits.AttrType = be16_to_cpu(HARDWARE_VERSION);
			ae->ad.bits.AttrLen = be16_to_cpu(FOURBYTES + 8);
			/* Convert JEDEC ID to ascii for hardware version */
			incr = vp->rev.biuRev;
			for (i = 0; i < 8; i++) {
				j = (incr & 0xf);
				if (j <= 9)
					ae->un.HardwareVersion[7 - i] =
					    (char)((uint8_t) 0x30 +
						   (uint8_t) j);
				else
					ae->un.HardwareVersion[7 - i] =
					    (char)((uint8_t) 0x61 +
						   (uint8_t) (j - 10));
				incr = (incr >> 4);
			}
			ab->EntryCnt++;
			size += FOURBYTES + 8;

			/* #7 HBA attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh + size);
			ae->ad.bits.AttrType = be16_to_cpu(DRIVER_VERSION);
			strcpy(ae->un.DriverVersion, lpfc_release_version);
			len = strlen(ae->un.DriverVersion);
			len += (len & 3) ? (4 - (len & 3)) : 4;
			ae->ad.bits.AttrLen = be16_to_cpu(FOURBYTES + len);
			ab->EntryCnt++;
			size += FOURBYTES + len;

			/* #8 HBA attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh + size);
			ae->ad.bits.AttrType = be16_to_cpu(OPTION_ROM_VERSION);
			strcpy(ae->un.OptionROMVersion, phba->OptionROMVersion);
			len = strlen(ae->un.OptionROMVersion);
			len += (len & 3) ? (4 - (len & 3)) : 4;
			ae->ad.bits.AttrLen = be16_to_cpu(FOURBYTES + len);
			ab->EntryCnt++;
			size += FOURBYTES + len;

			/* #9 HBA attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh + size);
			ae->ad.bits.AttrType = be16_to_cpu(FIRMWARE_VERSION);
			lpfc_decode_firmware_rev(phba, ae->un.FirmwareVersion,
				1);
			len = strlen(ae->un.FirmwareVersion);
			len += (len & 3) ? (4 - (len & 3)) : 4;
			ae->ad.bits.AttrLen = be16_to_cpu(FOURBYTES + len);
			ab->EntryCnt++;
			size += FOURBYTES + len;

			/* #10 HBA attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh + size);
			ae->ad.bits.AttrType = be16_to_cpu(OS_NAME_VERSION);
			sprintf(ae->un.OsNameVersion, "%s %s %s",
				init_utsname()->sysname,
				init_utsname()->release,
				init_utsname()->version);
			len = strlen(ae->un.OsNameVersion);
			len += (len & 3) ? (4 - (len & 3)) : 4;
			ae->ad.bits.AttrLen = be16_to_cpu(FOURBYTES + len);
			ab->EntryCnt++;
			size += FOURBYTES + len;

			/* #11 HBA attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh + size);
			ae->ad.bits.AttrType = be16_to_cpu(MAX_CT_PAYLOAD_LEN);
			ae->ad.bits.AttrLen = be16_to_cpu(FOURBYTES + 4);
			ae->un.MaxCTPayloadLen = (65 * 4096);
			ab->EntryCnt++;
			size += FOURBYTES + 4;

			ab->EntryCnt = be32_to_cpu(ab->EntryCnt);
			/* Total size */
			size = GID_REQUEST_SZ - 4 + size;
		}
		break;

	case SLI_MGMT_RPA:
		{
			lpfc_vpd_t *vp;
			struct serv_parm *hsp;
			int len;

			vp = &phba->vpd;

			CtReq->CommandResponse.bits.CmdRsp =
			    be16_to_cpu(SLI_MGMT_RPA);
			CtReq->CommandResponse.bits.Size = 0;
			pab = (REG_PORT_ATTRIBUTE *) & CtReq->un.PortID;
			size = sizeof (struct lpfc_name) + FOURBYTES;
			memcpy((uint8_t *) & pab->PortName,
			       (uint8_t *) & vport->fc_sparam.portName,
			       sizeof (struct lpfc_name));
			pab->ab.EntryCnt = 0;

			/* #1 Port attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) pab + size);
			ae->ad.bits.AttrType = be16_to_cpu(SUPPORTED_FC4_TYPES);
			ae->ad.bits.AttrLen = be16_to_cpu(FOURBYTES + 32);
			ae->un.SupportFC4Types[2] = 1;
			ae->un.SupportFC4Types[7] = 1;
			pab->ab.EntryCnt++;
			size += FOURBYTES + 32;

			/* #2 Port attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) pab + size);
			ae->ad.bits.AttrType = be16_to_cpu(SUPPORTED_SPEED);
			ae->ad.bits.AttrLen = be16_to_cpu(FOURBYTES + 4);

			ae->un.SupportSpeed = 0;
			if (phba->lmt & LMT_10Gb)
				ae->un.SupportSpeed = HBA_PORTSPEED_10GBIT;
			if (phba->lmt & LMT_8Gb)
				ae->un.SupportSpeed |= HBA_PORTSPEED_8GBIT;
			if (phba->lmt & LMT_4Gb)
				ae->un.SupportSpeed |= HBA_PORTSPEED_4GBIT;
			if (phba->lmt & LMT_2Gb)
				ae->un.SupportSpeed |= HBA_PORTSPEED_2GBIT;
			if (phba->lmt & LMT_1Gb)
				ae->un.SupportSpeed |= HBA_PORTSPEED_1GBIT;

			pab->ab.EntryCnt++;
			size += FOURBYTES + 4;

			/* #3 Port attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) pab + size);
			ae->ad.bits.AttrType = be16_to_cpu(PORT_SPEED);
			ae->ad.bits.AttrLen = be16_to_cpu(FOURBYTES + 4);
			switch(phba->fc_linkspeed) {
				case LA_1GHZ_LINK:
					ae->un.PortSpeed = HBA_PORTSPEED_1GBIT;
				break;
				case LA_2GHZ_LINK:
					ae->un.PortSpeed = HBA_PORTSPEED_2GBIT;
				break;
				case LA_4GHZ_LINK:
					ae->un.PortSpeed = HBA_PORTSPEED_4GBIT;
				break;
				case LA_8GHZ_LINK:
					ae->un.PortSpeed = HBA_PORTSPEED_8GBIT;
				break;
				default:
					ae->un.PortSpeed =
						HBA_PORTSPEED_UNKNOWN;
				break;
			}
			pab->ab.EntryCnt++;
			size += FOURBYTES + 4;

			/* #4 Port attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) pab + size);
			ae->ad.bits.AttrType = be16_to_cpu(MAX_FRAME_SIZE);
			ae->ad.bits.AttrLen = be16_to_cpu(FOURBYTES + 4);
			hsp = (struct serv_parm *) & vport->fc_sparam;
			ae->un.MaxFrameSize =
			    (((uint32_t) hsp->cmn.
			      bbRcvSizeMsb) << 8) | (uint32_t) hsp->cmn.
			    bbRcvSizeLsb;
			pab->ab.EntryCnt++;
			size += FOURBYTES + 4;

			/* #5 Port attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) pab + size);
			ae->ad.bits.AttrType = be16_to_cpu(OS_DEVICE_NAME);
			strcpy((char *)ae->un.OsDeviceName, LPFC_DRIVER_NAME);
			len = strlen((char *)ae->un.OsDeviceName);
			len += (len & 3) ? (4 - (len & 3)) : 4;
			ae->ad.bits.AttrLen = be16_to_cpu(FOURBYTES + len);
			pab->ab.EntryCnt++;
			size += FOURBYTES + len;

			if (vport->cfg_fdmi_on == 2) {
				/* #6 Port attribute entry */
				ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) pab +
							  size);
				ae->ad.bits.AttrType = be16_to_cpu(HOST_NAME);
				sprintf(ae->un.HostName, "%s",
					init_utsname()->nodename);
				len = strlen(ae->un.HostName);
				len += (len & 3) ? (4 - (len & 3)) : 4;
				ae->ad.bits.AttrLen =
				    be16_to_cpu(FOURBYTES + len);
				pab->ab.EntryCnt++;
				size += FOURBYTES + len;
			}

			pab->ab.EntryCnt = be32_to_cpu(pab->ab.EntryCnt);
			/* Total size */
			size = GID_REQUEST_SZ - 4 + size;
		}
		break;

	case SLI_MGMT_DHBA:
		CtReq->CommandResponse.bits.CmdRsp = be16_to_cpu(SLI_MGMT_DHBA);
		CtReq->CommandResponse.bits.Size = 0;
		pe = (PORT_ENTRY *) & CtReq->un.PortID;
		memcpy((uint8_t *) & pe->PortName,
		       (uint8_t *) & vport->fc_sparam.portName,
		       sizeof (struct lpfc_name));
		size = GID_REQUEST_SZ - 4 + sizeof (struct lpfc_name);
		break;

	case SLI_MGMT_DPRT:
		CtReq->CommandResponse.bits.CmdRsp = be16_to_cpu(SLI_MGMT_DPRT);
		CtReq->CommandResponse.bits.Size = 0;
		pe = (PORT_ENTRY *) & CtReq->un.PortID;
		memcpy((uint8_t *) & pe->PortName,
		       (uint8_t *) & vport->fc_sparam.portName,
		       sizeof (struct lpfc_name));
		size = GID_REQUEST_SZ - 4 + sizeof (struct lpfc_name);
		break;
	}

	bpl = (struct ulp_bde64 *) bmp->virt;
	bpl->addrHigh = le32_to_cpu(putPaddrHigh(mp->phys) );
	bpl->addrLow = le32_to_cpu(putPaddrLow(mp->phys) );
	bpl->tus.f.bdeFlags = 0;
	bpl->tus.f.bdeSize = size;
	bpl->tus.w = le32_to_cpu(bpl->tus.w);

	cmpl = lpfc_cmpl_ct_cmd_fdmi;

	/* The lpfc_ct_cmd/lpfc_get_req shall increment ndlp reference count
	 * to hold ndlp reference for the corresponding callback function.
	 */
	if (!lpfc_ct_cmd(vport, mp, bmp, ndlp, cmpl, FC_MAX_NS_RSP, 0))
		return 0;

	/* Decrement ndlp reference count to release ndlp reference held
	 * for the failed command's callback function.
	 */
	lpfc_nlp_put(ndlp);

	lpfc_mbuf_free(phba, bmp->virt, bmp->phys);
fdmi_cmd_free_bmp:
	kfree(bmp);
fdmi_cmd_free_mpvirt:
	lpfc_mbuf_free(phba, mp->virt, mp->phys);
fdmi_cmd_free_mp:
	kfree(mp);
fdmi_cmd_exit:
	/* Issue FDMI request failed */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
			 "0244 Issue FDMI request failed Data: x%x\n",
			 cmdcode);
	return 1;
}

void
lpfc_fdmi_tmo(unsigned long ptr)
{
	struct lpfc_vport *vport = (struct lpfc_vport *)ptr;
	struct lpfc_hba   *phba = vport->phba;
	uint32_t tmo_posted;
	unsigned long iflag;

	spin_lock_irqsave(&vport->work_port_lock, iflag);
	tmo_posted = vport->work_port_events & WORKER_FDMI_TMO;
	if (!tmo_posted)
		vport->work_port_events |= WORKER_FDMI_TMO;
	spin_unlock_irqrestore(&vport->work_port_lock, iflag);

	if (!tmo_posted)
		lpfc_worker_wake_up(phba);
	return;
}

void
lpfc_fdmi_timeout_handler(struct lpfc_vport *vport)
{
	struct lpfc_nodelist *ndlp;

	ndlp = lpfc_findnode_did(vport, FDMI_DID);
	if (ndlp && NLP_CHK_NODE_ACT(ndlp)) {
		if (init_utsname()->nodename[0] != '\0')
			lpfc_fdmi_cmd(vport, ndlp, SLI_MGMT_DHBA);
		else
			mod_timer(&vport->fc_fdmitmo, jiffies + HZ * 60);
	}
	return;
}

void
lpfc_decode_firmware_rev(struct lpfc_hba *phba, char *fwrevision, int flag)
{
	struct lpfc_sli *psli = &phba->sli;
	lpfc_vpd_t *vp = &phba->vpd;
	uint32_t b1, b2, b3, b4, i, rev;
	char c;
	uint32_t *ptr, str[4];
	uint8_t *fwname;

	if (vp->rev.rBit) {
		if (psli->sli_flag & LPFC_SLI2_ACTIVE)
			rev = vp->rev.sli2FwRev;
		else
			rev = vp->rev.sli1FwRev;

		b1 = (rev & 0x0000f000) >> 12;
		b2 = (rev & 0x00000f00) >> 8;
		b3 = (rev & 0x000000c0) >> 6;
		b4 = (rev & 0x00000030) >> 4;

		switch (b4) {
		case 0:
			c = 'N';
			break;
		case 1:
			c = 'A';
			break;
		case 2:
			c = 'B';
			break;
		default:
			c = 0;
			break;
		}
		b4 = (rev & 0x0000000f);

		if (psli->sli_flag & LPFC_SLI2_ACTIVE)
			fwname = vp->rev.sli2FwName;
		else
			fwname = vp->rev.sli1FwName;

		for (i = 0; i < 16; i++)
			if (fwname[i] == 0x20)
				fwname[i] = 0;

		ptr = (uint32_t*)fwname;

		for (i = 0; i < 3; i++)
			str[i] = be32_to_cpu(*ptr++);

		if (c == 0) {
			if (flag)
				sprintf(fwrevision, "%d.%d%d (%s)",
					b1, b2, b3, (char *)str);
			else
				sprintf(fwrevision, "%d.%d%d", b1,
					b2, b3);
		} else {
			if (flag)
				sprintf(fwrevision, "%d.%d%d%c%d (%s)",
					b1, b2, b3, c,
					b4, (char *)str);
			else
				sprintf(fwrevision, "%d.%d%d%c%d",
					b1, b2, b3, c, b4);
		}
	} else {
		rev = vp->rev.smFwRev;

		b1 = (rev & 0xff000000) >> 24;
		b2 = (rev & 0x00f00000) >> 20;
		b3 = (rev & 0x000f0000) >> 16;
		c  = (rev & 0x0000ff00) >> 8;
		b4 = (rev & 0x000000ff);

		if (flag)
			sprintf(fwrevision, "%d.%d%d%c%d ", b1,
				b2, b3, c, b4);
		else
			sprintf(fwrevision, "%d.%d%d%c%d ", b1,
				b2, b3, c, b4);
	}
	return;
}
