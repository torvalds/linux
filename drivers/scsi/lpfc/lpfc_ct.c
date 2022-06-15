/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2017-2022 Broadcom. All Rights Reserved. The term *
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.     *
 * Copyright (C) 2004-2016 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.broadcom.com                                                *
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
#include <linux/slab.h>
#include <linux/utsname.h>

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
#include "lpfc_logmsg.h"
#include "lpfc_crtn.h"
#include "lpfc_version.h"
#include "lpfc_vport.h"
#include "lpfc_debugfs.h"

/* FDMI Port Speed definitions - FC-GS-7 */
#define HBA_PORTSPEED_1GFC		0x00000001	/* 1G FC */
#define HBA_PORTSPEED_2GFC		0x00000002	/* 2G FC */
#define HBA_PORTSPEED_4GFC		0x00000008	/* 4G FC */
#define HBA_PORTSPEED_10GFC		0x00000004	/* 10G FC */
#define HBA_PORTSPEED_8GFC		0x00000010	/* 8G FC */
#define HBA_PORTSPEED_16GFC		0x00000020	/* 16G FC */
#define HBA_PORTSPEED_32GFC		0x00000040	/* 32G FC */
#define HBA_PORTSPEED_20GFC		0x00000080	/* 20G FC */
#define HBA_PORTSPEED_40GFC		0x00000100	/* 40G FC */
#define HBA_PORTSPEED_128GFC		0x00000200	/* 128G FC */
#define HBA_PORTSPEED_64GFC		0x00000400	/* 64G FC */
#define HBA_PORTSPEED_256GFC		0x00000800	/* 256G FC */
#define HBA_PORTSPEED_UNKNOWN		0x00008000	/* Unknown */
#define HBA_PORTSPEED_10GE		0x00010000	/* 10G E */
#define HBA_PORTSPEED_40GE		0x00020000	/* 40G E */
#define HBA_PORTSPEED_100GE		0x00040000	/* 100G E */
#define HBA_PORTSPEED_25GE		0x00080000	/* 25G E */
#define HBA_PORTSPEED_50GE		0x00100000	/* 50G E */
#define HBA_PORTSPEED_400GE		0x00200000	/* 400G E */

#define FOURBYTES	4


static char *lpfc_release_version = LPFC_DRIVER_VERSION;
static void
lpfc_cmpl_ct_cmd_vmid(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
		      struct lpfc_iocbq *rspiocb);

static void
lpfc_ct_ignore_hbq_buffer(struct lpfc_hba *phba, struct lpfc_iocbq *piocbq,
			  struct lpfc_dmabuf *mp, uint32_t size)
{
	if (!mp) {
		lpfc_printf_log(phba, KERN_INFO, LOG_ELS,
				"0146 Ignoring unsolicited CT No HBQ "
				"status = x%x\n",
				get_job_ulpstatus(phba, piocbq));
	}
	lpfc_printf_log(phba, KERN_INFO, LOG_ELS,
			"0145 Ignoring unsolicted CT HBQ Size:%d "
			"status = x%x\n",
			size, get_job_ulpstatus(phba, piocbq));
}

static void
lpfc_ct_unsol_buffer(struct lpfc_hba *phba, struct lpfc_iocbq *piocbq,
		     struct lpfc_dmabuf *mp, uint32_t size)
{
	lpfc_ct_ignore_hbq_buffer(phba, piocbq, mp, size);
}

/**
 * lpfc_ct_unsol_cmpl : Completion callback function for unsol ct commands
 * @phba : pointer to lpfc hba data structure.
 * @cmdiocb : pointer to lpfc command iocb data structure.
 * @rspiocb : pointer to lpfc response iocb data structure.
 *
 * This routine is the callback function for issuing unsol ct reject command.
 * The memory allocated in the reject command path is freed up here.
 **/
static void
lpfc_ct_unsol_cmpl(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
		   struct lpfc_iocbq *rspiocb)
{
	struct lpfc_nodelist *ndlp;
	struct lpfc_dmabuf *mp, *bmp;

	ndlp = cmdiocb->ndlp;
	if (ndlp)
		lpfc_nlp_put(ndlp);

	mp = cmdiocb->rsp_dmabuf;
	bmp = cmdiocb->bpl_dmabuf;
	if (mp) {
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		kfree(mp);
		cmdiocb->rsp_dmabuf = NULL;
	}

	if (bmp) {
		lpfc_mbuf_free(phba, bmp->virt, bmp->phys);
		kfree(bmp);
		cmdiocb->bpl_dmabuf = NULL;
	}

	lpfc_sli_release_iocbq(phba, cmdiocb);
}

/**
 * lpfc_ct_reject_event - Issue reject for unhandled CT MIB commands
 * @ndlp: pointer to a node-list data structure.
 * @ct_req: pointer to the CT request data structure.
 * @ulp_context: context of received UNSOL CT command
 * @ox_id: ox_id of the UNSOL CT command
 *
 * This routine is invoked by the lpfc_ct_handle_mibreq routine for sending
 * a reject response. Reject response is sent for the unhandled commands.
 **/
static void
lpfc_ct_reject_event(struct lpfc_nodelist *ndlp,
		     struct lpfc_sli_ct_request *ct_req,
		     u16 ulp_context, u16 ox_id)
{
	struct lpfc_vport *vport = ndlp->vport;
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_sli_ct_request *ct_rsp;
	struct lpfc_iocbq *cmdiocbq = NULL;
	struct lpfc_dmabuf *bmp = NULL;
	struct lpfc_dmabuf *mp = NULL;
	struct ulp_bde64 *bpl;
	u8 rc = 0;
	u32 tmo;

	/* fill in BDEs for command */
	mp = kmalloc(sizeof(*mp), GFP_KERNEL);
	if (!mp) {
		rc = 1;
		goto ct_exit;
	}

	mp->virt = lpfc_mbuf_alloc(phba, MEM_PRI, &mp->phys);
	if (!mp->virt) {
		rc = 2;
		goto ct_free_mp;
	}

	/* Allocate buffer for Buffer ptr list */
	bmp = kmalloc(sizeof(*bmp), GFP_KERNEL);
	if (!bmp) {
		rc = 3;
		goto ct_free_mpvirt;
	}

	bmp->virt = lpfc_mbuf_alloc(phba, MEM_PRI, &bmp->phys);
	if (!bmp->virt) {
		rc = 4;
		goto ct_free_bmp;
	}

	INIT_LIST_HEAD(&mp->list);
	INIT_LIST_HEAD(&bmp->list);

	bpl = (struct ulp_bde64 *)bmp->virt;
	memset(bpl, 0, sizeof(struct ulp_bde64));
	bpl->addrHigh = le32_to_cpu(putPaddrHigh(mp->phys));
	bpl->addrLow = le32_to_cpu(putPaddrLow(mp->phys));
	bpl->tus.f.bdeFlags = BUFF_TYPE_BDE_64;
	bpl->tus.f.bdeSize = (LPFC_CT_PREAMBLE - 4);
	bpl->tus.w = le32_to_cpu(bpl->tus.w);

	ct_rsp = (struct lpfc_sli_ct_request *)mp->virt;
	memset(ct_rsp, 0, sizeof(struct lpfc_sli_ct_request));

	ct_rsp->RevisionId.bits.Revision = SLI_CT_REVISION;
	ct_rsp->RevisionId.bits.InId = 0;
	ct_rsp->FsType = ct_req->FsType;
	ct_rsp->FsSubType = ct_req->FsSubType;
	ct_rsp->CommandResponse.bits.Size = 0;
	ct_rsp->CommandResponse.bits.CmdRsp =
		cpu_to_be16(SLI_CT_RESPONSE_FS_RJT);
	ct_rsp->ReasonCode = SLI_CT_REQ_NOT_SUPPORTED;
	ct_rsp->Explanation = SLI_CT_NO_ADDITIONAL_EXPL;

	cmdiocbq = lpfc_sli_get_iocbq(phba);
	if (!cmdiocbq) {
		rc = 5;
		goto ct_free_bmpvirt;
	}

	if (phba->sli_rev == LPFC_SLI_REV4) {
		lpfc_sli_prep_xmit_seq64(phba, cmdiocbq, bmp,
					 phba->sli4_hba.rpi_ids[ndlp->nlp_rpi],
					 ox_id, 1, FC_RCTL_DD_SOL_CTL, 1,
					 CMD_XMIT_SEQUENCE64_WQE);
	} else {
		lpfc_sli_prep_xmit_seq64(phba, cmdiocbq, bmp, 0, ulp_context, 1,
					 FC_RCTL_DD_SOL_CTL, 1,
					 CMD_XMIT_SEQUENCE64_CX);
	}

	/* Save for completion so we can release these resources */
	cmdiocbq->rsp_dmabuf = mp;
	cmdiocbq->bpl_dmabuf = bmp;
	cmdiocbq->cmd_cmpl = lpfc_ct_unsol_cmpl;
	tmo = (3 * phba->fc_ratov);

	cmdiocbq->retry = 0;
	cmdiocbq->vport = vport;
	cmdiocbq->drvrTimeout = tmo + LPFC_DRVR_TIMEOUT;

	cmdiocbq->ndlp = lpfc_nlp_get(ndlp);
	if (!cmdiocbq->ndlp)
		goto ct_no_ndlp;

	rc = lpfc_sli_issue_iocb(phba, LPFC_ELS_RING, cmdiocbq, 0);
	if (rc) {
		lpfc_nlp_put(ndlp);
		goto ct_no_ndlp;
	}
	return;

ct_no_ndlp:
	rc = 6;
	lpfc_sli_release_iocbq(phba, cmdiocbq);
ct_free_bmpvirt:
	lpfc_mbuf_free(phba, bmp->virt, bmp->phys);
ct_free_bmp:
	kfree(bmp);
ct_free_mpvirt:
	lpfc_mbuf_free(phba, mp->virt, mp->phys);
ct_free_mp:
	kfree(mp);
ct_exit:
	lpfc_printf_vlog(vport, KERN_ERR, LOG_ELS,
			 "6440 Unsol CT: Rsp err %d Data: x%x\n",
			 rc, vport->fc_flag);
}

/**
 * lpfc_ct_handle_mibreq - Process an unsolicited CT MIB request data buffer
 * @phba: pointer to lpfc hba data structure.
 * @ctiocbq: pointer to lpfc CT command iocb data structure.
 *
 * This routine is used for processing the IOCB associated with a unsolicited
 * CT MIB request. It first determines whether there is an existing ndlp that
 * matches the DID from the unsolicited IOCB. If not, it will return.
 **/
static void
lpfc_ct_handle_mibreq(struct lpfc_hba *phba, struct lpfc_iocbq *ctiocbq)
{
	struct lpfc_sli_ct_request *ct_req;
	struct lpfc_nodelist *ndlp = NULL;
	struct lpfc_vport *vport = ctiocbq->vport;
	u32 ulp_status = get_job_ulpstatus(phba, ctiocbq);
	u32 ulp_word4 = get_job_word4(phba, ctiocbq);
	u32 did;
	u32 mi_cmd;

	did = bf_get(els_rsp64_sid, &ctiocbq->wqe.xmit_els_rsp);
	if (ulp_status) {
		lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS,
				 "6438 Unsol CT: status:x%x/x%x did : x%x\n",
				 ulp_status, ulp_word4, did);
		return;
	}

	/* Ignore traffic received during vport shutdown */
	if (vport->fc_flag & FC_UNLOADING)
		return;

	ndlp = lpfc_findnode_did(vport, did);
	if (!ndlp) {
		lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS,
				 "6439 Unsol CT: NDLP Not Found for DID : x%x",
				 did);
		return;
	}

	ct_req = (struct lpfc_sli_ct_request *)ctiocbq->cmd_dmabuf->virt;

	mi_cmd = ct_req->CommandResponse.bits.CmdRsp;
	lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS,
			 "6442 : MI Cmd : x%x Not Supported\n", mi_cmd);
	lpfc_ct_reject_event(ndlp, ct_req,
			     bf_get(wqe_ctxt_tag,
				    &ctiocbq->wqe.xmit_els_rsp.wqe_com),
			     bf_get(wqe_rcvoxid,
				    &ctiocbq->wqe.xmit_els_rsp.wqe_com));
}

/**
 * lpfc_ct_unsol_event - Process an unsolicited event from a ct sli ring
 * @phba: pointer to lpfc hba data structure.
 * @pring: pointer to a SLI ring.
 * @ctiocbq: pointer to lpfc ct iocb data structure.
 *
 * This routine is used to process an unsolicited event received from a SLI
 * (Service Level Interface) ring. The actual processing of the data buffer
 * associated with the unsolicited event is done by invoking appropriate routine
 * after properly set up the iocb buffer from the SLI ring on which the
 * unsolicited event was received.
 **/
void
lpfc_ct_unsol_event(struct lpfc_hba *phba, struct lpfc_sli_ring *pring,
		    struct lpfc_iocbq *ctiocbq)
{
	struct lpfc_dmabuf *mp = NULL;
	IOCB_t *icmd = &ctiocbq->iocb;
	int i;
	struct lpfc_iocbq *iocbq;
	struct lpfc_iocbq *iocb;
	dma_addr_t dma_addr;
	uint32_t size;
	struct list_head head;
	struct lpfc_sli_ct_request *ct_req;
	struct lpfc_dmabuf *bdeBuf1 = ctiocbq->cmd_dmabuf;
	struct lpfc_dmabuf *bdeBuf2 = ctiocbq->bpl_dmabuf;
	u32 status, parameter, bde_count = 0;
	struct lpfc_wcqe_complete *wcqe_cmpl = NULL;

	ctiocbq->cmd_dmabuf = NULL;
	ctiocbq->rsp_dmabuf = NULL;
	ctiocbq->bpl_dmabuf = NULL;

	wcqe_cmpl = &ctiocbq->wcqe_cmpl;
	status = get_job_ulpstatus(phba, ctiocbq);
	parameter = get_job_word4(phba, ctiocbq);
	if (phba->sli_rev == LPFC_SLI_REV4)
		bde_count = wcqe_cmpl->word3;
	else
		bde_count = icmd->ulpBdeCount;

	if (unlikely(status == IOSTAT_NEED_BUFFER)) {
		lpfc_sli_hbqbuf_add_hbqs(phba, LPFC_ELS_HBQ);
	} else if ((status == IOSTAT_LOCAL_REJECT) &&
		   ((parameter & IOERR_PARAM_MASK) ==
		   IOERR_RCV_BUFFER_WAITING)) {
		/* Not enough posted buffers; Try posting more buffers */
		phba->fc_stat.NoRcvBuf++;
		if (!(phba->sli3_options & LPFC_SLI3_HBQ_ENABLED))
			lpfc_sli3_post_buffer(phba, pring, 2);
		return;
	}

	/* If there are no BDEs associated
	 * with this IOCB, there is nothing to do.
	 */
	if (bde_count == 0)
		return;

	ctiocbq->cmd_dmabuf = bdeBuf1;
	if (bde_count == 2)
		ctiocbq->bpl_dmabuf = bdeBuf2;

	ct_req = (struct lpfc_sli_ct_request *)ctiocbq->cmd_dmabuf->virt;

	if (ct_req->FsType == SLI_CT_MANAGEMENT_SERVICE &&
	    ct_req->FsSubType == SLI_CT_MIB_Subtypes) {
		lpfc_ct_handle_mibreq(phba, ctiocbq);
	} else {
		if (!lpfc_bsg_ct_unsol_event(phba, pring, ctiocbq))
			return;
	}

	if (phba->sli3_options & LPFC_SLI3_HBQ_ENABLED) {
		INIT_LIST_HEAD(&head);
		list_add_tail(&head, &ctiocbq->list);
		list_for_each_entry(iocb, &head, list) {
			if (phba->sli_rev == LPFC_SLI_REV4)
				bde_count = iocb->wcqe_cmpl.word3;
			else
				bde_count = iocb->iocb.ulpBdeCount;

			if (!bde_count)
				continue;
			bdeBuf1 = iocb->cmd_dmabuf;
			iocb->cmd_dmabuf = NULL;
			if (phba->sli_rev == LPFC_SLI_REV4)
				size = iocb->wqe.gen_req.bde.tus.f.bdeSize;
			else
				size  = iocb->iocb.un.cont64[0].tus.f.bdeSize;
			lpfc_ct_unsol_buffer(phba, ctiocbq, bdeBuf1, size);
			lpfc_in_buf_free(phba, bdeBuf1);
			if (bde_count == 2) {
				bdeBuf2 = iocb->bpl_dmabuf;
				iocb->bpl_dmabuf = NULL;
				if (phba->sli_rev == LPFC_SLI_REV4)
					size = iocb->unsol_rcv_len;
				else
					size = iocb->iocb.unsli3.rcvsli3.bde2.tus.f.bdeSize;
				lpfc_ct_unsol_buffer(phba, ctiocbq, bdeBuf2,
						     size);
				lpfc_in_buf_free(phba, bdeBuf2);
			}
		}
		list_del(&head);
	} else {
		INIT_LIST_HEAD(&head);
		list_add_tail(&head, &ctiocbq->list);
		list_for_each_entry(iocbq, &head, list) {
			icmd = &iocbq->iocb;
			if (icmd->ulpBdeCount == 0)
				lpfc_ct_unsol_buffer(phba, iocbq, NULL, 0);
			for (i = 0; i < icmd->ulpBdeCount; i++) {
				dma_addr = getPaddr(icmd->un.cont64[i].addrHigh,
						    icmd->un.cont64[i].addrLow);
				mp = lpfc_sli_ringpostbuf_get(phba, pring,
							      dma_addr);
				size = icmd->un.cont64[i].tus.f.bdeSize;
				lpfc_ct_unsol_buffer(phba, iocbq, mp, size);
				lpfc_in_buf_free(phba, mp);
			}
			lpfc_sli3_post_buffer(phba, pring, i);
		}
		list_del(&head);
	}
}

/**
 * lpfc_ct_handle_unsol_abort - ct upper level protocol abort handler
 * @phba: Pointer to HBA context object.
 * @dmabuf: pointer to a dmabuf that describes the FC sequence
 *
 * This function serves as the upper level protocol abort handler for CT
 * protocol.
 *
 * Return 1 if abort has been handled, 0 otherwise.
 **/
int
lpfc_ct_handle_unsol_abort(struct lpfc_hba *phba, struct hbq_dmabuf *dmabuf)
{
	int handled;

	/* CT upper level goes through BSG */
	handled = lpfc_bsg_ct_unsol_abort(phba, dmabuf);

	return handled;
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

	/* IOCBQ job structure gets cleaned during release.  Just release
	 * the dma buffers here.
	 */
	if (ctiocb->cmd_dmabuf) {
		buf_ptr = ctiocb->cmd_dmabuf;
		lpfc_mbuf_free(phba, buf_ptr->virt, buf_ptr->phys);
		kfree(buf_ptr);
		ctiocb->cmd_dmabuf = NULL;
	}
	if (ctiocb->rsp_dmabuf) {
		lpfc_free_ct_rsp(phba, ctiocb->rsp_dmabuf);
		ctiocb->rsp_dmabuf = NULL;
	}

	if (ctiocb->bpl_dmabuf) {
		buf_ptr = ctiocb->bpl_dmabuf;
		lpfc_mbuf_free(phba, buf_ptr->virt, buf_ptr->phys);
		kfree(buf_ptr);
		ctiocb->bpl_dmabuf = NULL;
	}
	lpfc_sli_release_iocbq(phba, ctiocb);
	return 0;
}

/*
 * lpfc_gen_req - Build and issue a GEN_REQUEST command  to the SLI Layer
 * @vport: pointer to a host virtual N_Port data structure.
 * @bmp: Pointer to BPL for SLI command
 * @inp: Pointer to data buffer for response data.
 * @outp: Pointer to data buffer that hold the CT command.
 * @cmpl: completion routine to call when command completes
 * @ndlp: Destination NPort nodelist entry
 *
 * This function as the final part for issuing a CT command.
 */
static int
lpfc_gen_req(struct lpfc_vport *vport, struct lpfc_dmabuf *bmp,
	     struct lpfc_dmabuf *inp, struct lpfc_dmabuf *outp,
	     void (*cmpl)(struct lpfc_hba *, struct lpfc_iocbq *,
			  struct lpfc_iocbq *),
	     struct lpfc_nodelist *ndlp, uint32_t event_tag, uint32_t num_entry,
	     uint32_t tmo, uint8_t retry)
{
	struct lpfc_hba  *phba = vport->phba;
	struct lpfc_iocbq *geniocb;
	int rc;
	u16 ulp_context;

	/* Allocate buffer for  command iocb */
	geniocb = lpfc_sli_get_iocbq(phba);

	if (geniocb == NULL)
		return 1;

	/* Update the num_entry bde count */
	geniocb->num_bdes = num_entry;

	geniocb->bpl_dmabuf = bmp;

	/* Save for completion so we can release these resources */
	geniocb->cmd_dmabuf = inp;
	geniocb->rsp_dmabuf = outp;

	geniocb->event_tag = event_tag;

	if (!tmo) {
		 /* FC spec states we need 3 * ratov for CT requests */
		tmo = (3 * phba->fc_ratov);
	}

	if (phba->sli_rev == LPFC_SLI_REV4)
		ulp_context = phba->sli4_hba.rpi_ids[ndlp->nlp_rpi];
	else
		ulp_context = ndlp->nlp_rpi;

	lpfc_sli_prep_gen_req(phba, geniocb, bmp, ulp_context, num_entry, tmo);

	/* Issue GEN REQ IOCB for NPORT <did> */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS,
			 "0119 Issue GEN REQ IOCB to NPORT x%x "
			 "Data: x%x x%x\n",
			 ndlp->nlp_DID, geniocb->iotag,
			 vport->port_state);
	geniocb->cmd_cmpl = cmpl;
	geniocb->drvrTimeout = tmo + LPFC_DRVR_TIMEOUT;
	geniocb->vport = vport;
	geniocb->retry = retry;
	geniocb->ndlp = lpfc_nlp_get(ndlp);
	if (!geniocb->ndlp)
		goto out;

	rc = lpfc_sli_issue_iocb(phba, LPFC_ELS_RING, geniocb, 0);
	if (rc == IOCB_ERROR) {
		lpfc_nlp_put(ndlp);
		goto out;
	}

	return 0;
out:
	lpfc_sli_release_iocbq(phba, geniocb);
	return 1;
}

/*
 * lpfc_ct_cmd - Build and issue a CT command
 * @vport: pointer to a host virtual N_Port data structure.
 * @inmp: Pointer to data buffer for response data.
 * @bmp: Pointer to BPL for SLI command
 * @ndlp: Destination NPort nodelist entry
 * @cmpl: completion routine to call when command completes
 *
 * This function is called for issuing a CT command.
 */
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
	/*
	 * Form the CT IOCB.  The total number of BDEs in this IOCB
	 * is the single command plus response count from
	 * lpfc_alloc_ct_rsp.
	 */
	cnt += 1;
	status = lpfc_gen_req(vport, bmp, inmp, outmp, cmpl, ndlp,
			phba->fc_eventTag, cnt, 0, retry);
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

	spin_lock_irqsave(&phba->port_list_lock, flags);
	list_for_each_entry(vport_curr, &phba->port_list, listentry) {
		if ((vport_curr->fc_myDID) && (vport_curr->fc_myDID == did)) {
			spin_unlock_irqrestore(&phba->port_list_lock, flags);
			return vport_curr;
		}
	}
	spin_unlock_irqrestore(&phba->port_list_lock, flags);
	return NULL;
}

static void
lpfc_prep_node_fc4type(struct lpfc_vport *vport, uint32_t Did, uint8_t fc4_type)
{
	struct lpfc_nodelist *ndlp;

	if ((vport->port_type != LPFC_NPIV_PORT) ||
	    !(vport->ct_flags & FC_CT_RFF_ID) || !vport->cfg_restrict_login) {

		ndlp = lpfc_setup_disc_node(vport, Did);

		if (ndlp) {
			lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_CT,
				"Parse GID_FTrsp: did:x%x flg:x%x x%x",
				Did, ndlp->nlp_flag, vport->fc_flag);

			/* By default, the driver expects to support FCP FC4 */
			if (fc4_type == FC_TYPE_FCP)
				ndlp->nlp_fc4_type |= NLP_FC4_FCP;

			if (fc4_type == FC_TYPE_NVME)
				ndlp->nlp_fc4_type |= NLP_FC4_NVME;

			lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
					 "0238 Process x%06x NameServer Rsp "
					 "Data: x%x x%x x%x x%x x%x\n", Did,
					 ndlp->nlp_flag, ndlp->nlp_fc4_type,
					 ndlp->nlp_state, vport->fc_flag,
					 vport->fc_rscn_id_cnt);

			/* if ndlp needs to be discovered and prior
			 * state of ndlp hit devloss, change state to
			 * allow rediscovery.
			 */
			if (ndlp->nlp_flag & NLP_NPR_2B_DISC &&
			    ndlp->nlp_state == NLP_STE_UNUSED_NODE) {
				lpfc_nlp_set_state(vport, ndlp,
						   NLP_STE_NPR_NODE);
			}
		} else {
			lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_CT,
				"Skip1 GID_FTrsp: did:x%x flg:x%x cnt:%d",
				Did, vport->fc_flag, vport->fc_rscn_id_cnt);

			lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
					 "0239 Skip x%06x NameServer Rsp "
					 "Data: x%x x%x x%px\n",
					 Did, vport->fc_flag,
					 vport->fc_rscn_id_cnt, ndlp);
		}
	} else {
		if (!(vport->fc_flag & FC_RSCN_MODE) ||
		    lpfc_rscn_payload_check(vport, Did)) {
			lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_CT,
				"Query GID_FTrsp: did:x%x flg:x%x cnt:%d",
				Did, vport->fc_flag, vport->fc_rscn_id_cnt);

			/*
			 * This NPortID was previously a FCP/NVMe target,
			 * Don't even bother to send GFF_ID.
			 */
			ndlp = lpfc_findnode_did(vport, Did);
			if (ndlp &&
			    (ndlp->nlp_type &
			    (NLP_FCP_TARGET | NLP_NVME_TARGET))) {
				if (fc4_type == FC_TYPE_FCP)
					ndlp->nlp_fc4_type |= NLP_FC4_FCP;
				if (fc4_type == FC_TYPE_NVME)
					ndlp->nlp_fc4_type |= NLP_FC4_NVME;
				lpfc_setup_disc_node(vport, Did);
			} else if (lpfc_ns_cmd(vport, SLI_CTNS_GFF_ID,
				   0, Did) == 0)
				vport->num_disc_nodes++;
			else
				lpfc_setup_disc_node(vport, Did);
		} else {
			lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_CT,
				"Skip2 GID_FTrsp: did:x%x flg:x%x cnt:%d",
				Did, vport->fc_flag, vport->fc_rscn_id_cnt);

			lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
					 "0245 Skip x%06x NameServer Rsp "
					 "Data: x%x x%x\n", Did,
					 vport->fc_flag,
					 vport->fc_rscn_id_cnt);
		}
	}
}

static void
lpfc_ns_rsp_audit_did(struct lpfc_vport *vport, uint32_t Did, uint8_t fc4_type)
{
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_nodelist *ndlp = NULL;
	char *str;

	if (phba->cfg_ns_query == LPFC_NS_QUERY_GID_FT)
		str = "GID_FT";
	else
		str = "GID_PT";
	lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
			 "6430 Process %s rsp for %08x type %x %s %s\n",
			 str, Did, fc4_type,
			 (fc4_type == FC_TYPE_FCP) ?  "FCP" : " ",
			 (fc4_type == FC_TYPE_NVME) ?  "NVME" : " ");
	/*
	 * To conserve rpi's, filter out addresses for other
	 * vports on the same physical HBAs.
	 */
	if (Did != vport->fc_myDID &&
	    (!lpfc_find_vport_by_did(phba, Did) ||
	     vport->cfg_peer_port_login)) {
		if (!phba->nvmet_support) {
			/* FCPI/NVMEI path. Process Did */
			lpfc_prep_node_fc4type(vport, Did, fc4_type);
			return;
		}
		/* NVMET path.  NVMET only cares about NVMEI nodes. */
		list_for_each_entry(ndlp, &vport->fc_nodes, nlp_listp) {
			if (ndlp->nlp_type != NLP_NVME_INITIATOR ||
			    ndlp->nlp_state != NLP_STE_UNMAPPED_NODE)
				continue;
			spin_lock_irq(&ndlp->lock);
			if (ndlp->nlp_DID == Did)
				ndlp->nlp_flag &= ~NLP_NVMET_RECOV;
			else
				ndlp->nlp_flag |= NLP_NVMET_RECOV;
			spin_unlock_irq(&ndlp->lock);
		}
	}
}

static int
lpfc_ns_rsp(struct lpfc_vport *vport, struct lpfc_dmabuf *mp, uint8_t fc4_type,
	    uint32_t Size)
{
	struct lpfc_sli_ct_request *Response =
		(struct lpfc_sli_ct_request *) mp->virt;
	struct lpfc_dmabuf *mlast, *next_mp;
	uint32_t *ctptr = (uint32_t *) & Response->un.gid.PortType;
	uint32_t Did, CTentry;
	int Cnt;
	struct list_head head;
	struct lpfc_nodelist *ndlp = NULL;

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
		while (Cnt >= sizeof(uint32_t)) {
			/* Get next DID from NameServer List */
			CTentry = *ctptr++;
			Did = ((be32_to_cpu(CTentry)) & Mask_DID);
			lpfc_ns_rsp_audit_did(vport, Did, fc4_type);
			if (CTentry & (cpu_to_be32(SLI_CT_LAST_ENTRY)))
				goto nsout1;

			Cnt -= sizeof(uint32_t);
		}
		ctptr = NULL;

	}

	/* All GID_FT entries processed.  If the driver is running in
	 * in target mode, put impacted nodes into recovery and drop
	 * the RPI to flush outstanding IO.
	 */
	if (vport->phba->nvmet_support) {
		list_for_each_entry(ndlp, &vport->fc_nodes, nlp_listp) {
			if (!(ndlp->nlp_flag & NLP_NVMET_RECOV))
				continue;
			lpfc_disc_state_machine(vport, ndlp, NULL,
						NLP_EVT_DEVICE_RECOVERY);
			spin_lock_irq(&ndlp->lock);
			ndlp->nlp_flag &= ~NLP_NVMET_RECOV;
			spin_unlock_irq(&ndlp->lock);
		}
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
	struct lpfc_dmabuf *outp;
	struct lpfc_dmabuf *inp;
	struct lpfc_sli_ct_request *CTrsp;
	struct lpfc_sli_ct_request *CTreq;
	struct lpfc_nodelist *ndlp;
	u32 ulp_status = get_job_ulpstatus(phba, rspiocb);
	u32 ulp_word4 = get_job_word4(phba, rspiocb);
	int rc, type;

	/* First save ndlp, before we overwrite it */
	ndlp = cmdiocb->ndlp;

	/* we pass cmdiocb to state machine which needs rspiocb as well */
	cmdiocb->rsp_iocb = rspiocb;
	inp = cmdiocb->cmd_dmabuf;
	outp = cmdiocb->rsp_dmabuf;

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_CT,
		 "GID_FT cmpl:     status:x%x/x%x rtry:%d",
		ulp_status, ulp_word4, vport->fc_ns_retry);

	/* Ignore response if link flipped after this request was made */
	if (cmdiocb->event_tag != phba->fc_eventTag) {
		lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
				 "9043 Event tag mismatch. Ignoring NS rsp\n");
		goto out;
	}

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
	if (lpfc_error_lost_link(ulp_status, ulp_word4)) {
		lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
				 "0226 NS query failed due to link event: "
				 "ulp_status x%x ulp_word4 x%x fc_flag x%x "
				 "port_state x%x gidft_inp x%x\n",
				 ulp_status, ulp_word4, vport->fc_flag,
				 vport->port_state, vport->gidft_inp);
		if (vport->fc_flag & FC_RSCN_MODE)
			lpfc_els_flush_rscn(vport);
		if (vport->gidft_inp)
			vport->gidft_inp--;
		goto out;
	}

	spin_lock_irq(shost->host_lock);
	if (vport->fc_flag & FC_RSCN_DEFERRED) {
		vport->fc_flag &= ~FC_RSCN_DEFERRED;
		spin_unlock_irq(shost->host_lock);

		/* This is a GID_FT completing so the gidft_inp counter was
		 * incremented before the GID_FT was issued to the wire.
		 */
		if (vport->gidft_inp)
			vport->gidft_inp--;

		/*
		 * Skip processing the NS response
		 * Re-issue the NS cmd
		 */
		lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS,
				 "0151 Process Deferred RSCN Data: x%x x%x\n",
				 vport->fc_flag, vport->fc_rscn_id_cnt);
		lpfc_els_handle_rscn(vport);

		goto out;
	}
	spin_unlock_irq(shost->host_lock);

	if (ulp_status) {
		/* Check for retry */
		if (vport->fc_ns_retry < LPFC_MAX_NS_RETRY) {
			if (ulp_status != IOSTAT_LOCAL_REJECT ||
			    (ulp_word4 & IOERR_PARAM_MASK) !=
			    IOERR_NO_RESOURCES)
				vport->fc_ns_retry++;

			type = lpfc_get_gidft_type(vport, cmdiocb);
			if (type == 0)
				goto out;

			/* CT command is being retried */
			rc = lpfc_ns_cmd(vport, SLI_CTNS_GID_FT,
					 vport->fc_ns_retry, type);
			if (rc == 0)
				goto out;
			else { /* Unable to send NS cmd */
				if (vport->gidft_inp)
					vport->gidft_inp--;
			}
		}
		if (vport->fc_flag & FC_RSCN_MODE)
			lpfc_els_flush_rscn(vport);
		lpfc_vport_set_state(vport, FC_VPORT_FAILED);
		lpfc_printf_vlog(vport, KERN_ERR, LOG_TRACE_EVENT,
				 "0257 GID_FT Query error: 0x%x 0x%x\n",
				 ulp_status, vport->fc_ns_retry);
	} else {
		/* Good status, continue checking */
		CTreq = (struct lpfc_sli_ct_request *) inp->virt;
		CTrsp = (struct lpfc_sli_ct_request *) outp->virt;
		if (CTrsp->CommandResponse.bits.CmdRsp ==
		    cpu_to_be16(SLI_CT_RESPONSE_FS_ACC)) {
			lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
					 "0208 NameServer Rsp Data: x%x x%x "
					 "x%x x%x sz x%x\n",
					 vport->fc_flag,
					 CTreq->un.gid.Fc4Type,
					 vport->num_disc_nodes,
					 vport->gidft_inp,
					 get_job_data_placed(phba, rspiocb));

			lpfc_ns_rsp(vport,
				    outp,
				    CTreq->un.gid.Fc4Type,
				    get_job_data_placed(phba, rspiocb));
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
			lpfc_printf_vlog(vport, KERN_ERR, LOG_TRACE_EVENT,
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
		if (vport->gidft_inp)
			vport->gidft_inp--;
	}

	lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
			 "4216 GID_FT cmpl inp %d disc %d\n",
			 vport->gidft_inp, vport->num_disc_nodes);

	/* Link up / RSCN discovery */
	if ((vport->num_disc_nodes == 0) &&
	    (vport->gidft_inp == 0)) {
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
	lpfc_ct_free_iocb(phba, cmdiocb);
	lpfc_nlp_put(ndlp);
	return;
}

static void
lpfc_cmpl_ct_cmd_gid_pt(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
			struct lpfc_iocbq *rspiocb)
{
	struct lpfc_vport *vport = cmdiocb->vport;
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct lpfc_dmabuf *outp;
	struct lpfc_dmabuf *inp;
	struct lpfc_sli_ct_request *CTrsp;
	struct lpfc_sli_ct_request *CTreq;
	struct lpfc_nodelist *ndlp;
	u32 ulp_status = get_job_ulpstatus(phba, rspiocb);
	u32 ulp_word4 = get_job_word4(phba, rspiocb);
	int rc;

	/* First save ndlp, before we overwrite it */
	ndlp = cmdiocb->ndlp;

	/* we pass cmdiocb to state machine which needs rspiocb as well */
	cmdiocb->rsp_iocb = rspiocb;
	inp = cmdiocb->cmd_dmabuf;
	outp = cmdiocb->rsp_dmabuf;

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_CT,
			      "GID_PT cmpl:     status:x%x/x%x rtry:%d",
			      ulp_status, ulp_word4,
			      vport->fc_ns_retry);

	/* Ignore response if link flipped after this request was made */
	if (cmdiocb->event_tag != phba->fc_eventTag) {
		lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
				 "9044 Event tag mismatch. Ignoring NS rsp\n");
		goto out;
	}

	/* Don't bother processing response if vport is being torn down. */
	if (vport->load_flag & FC_UNLOADING) {
		if (vport->fc_flag & FC_RSCN_MODE)
			lpfc_els_flush_rscn(vport);
		goto out;
	}

	if (lpfc_els_chk_latt(vport)) {
		lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
				 "4108 Link event during NS query\n");
		if (vport->fc_flag & FC_RSCN_MODE)
			lpfc_els_flush_rscn(vport);
		lpfc_vport_set_state(vport, FC_VPORT_FAILED);
		goto out;
	}
	if (lpfc_error_lost_link(ulp_status, ulp_word4)) {
		lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
				 "4166 NS query failed due to link event: "
				 "ulp_status x%x ulp_word4 x%x fc_flag x%x "
				 "port_state x%x gidft_inp x%x\n",
				 ulp_status, ulp_word4, vport->fc_flag,
				 vport->port_state, vport->gidft_inp);
		if (vport->fc_flag & FC_RSCN_MODE)
			lpfc_els_flush_rscn(vport);
		if (vport->gidft_inp)
			vport->gidft_inp--;
		goto out;
	}

	spin_lock_irq(shost->host_lock);
	if (vport->fc_flag & FC_RSCN_DEFERRED) {
		vport->fc_flag &= ~FC_RSCN_DEFERRED;
		spin_unlock_irq(shost->host_lock);

		/* This is a GID_PT completing so the gidft_inp counter was
		 * incremented before the GID_PT was issued to the wire.
		 */
		if (vport->gidft_inp)
			vport->gidft_inp--;

		/*
		 * Skip processing the NS response
		 * Re-issue the NS cmd
		 */
		lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS,
				 "4167 Process Deferred RSCN Data: x%x x%x\n",
				 vport->fc_flag, vport->fc_rscn_id_cnt);
		lpfc_els_handle_rscn(vport);

		goto out;
	}
	spin_unlock_irq(shost->host_lock);

	if (ulp_status) {
		/* Check for retry */
		if (vport->fc_ns_retry < LPFC_MAX_NS_RETRY) {
			if (ulp_status != IOSTAT_LOCAL_REJECT ||
			    (ulp_word4 & IOERR_PARAM_MASK) !=
			    IOERR_NO_RESOURCES)
				vport->fc_ns_retry++;

			/* CT command is being retried */
			rc = lpfc_ns_cmd(vport, SLI_CTNS_GID_PT,
					 vport->fc_ns_retry, GID_PT_N_PORT);
			if (rc == 0)
				goto out;
			else { /* Unable to send NS cmd */
				if (vport->gidft_inp)
					vport->gidft_inp--;
			}
		}
		if (vport->fc_flag & FC_RSCN_MODE)
			lpfc_els_flush_rscn(vport);
		lpfc_vport_set_state(vport, FC_VPORT_FAILED);
		lpfc_printf_vlog(vport, KERN_ERR, LOG_TRACE_EVENT,
				 "4103 GID_FT Query error: 0x%x 0x%x\n",
				 ulp_status, vport->fc_ns_retry);
	} else {
		/* Good status, continue checking */
		CTreq = (struct lpfc_sli_ct_request *)inp->virt;
		CTrsp = (struct lpfc_sli_ct_request *)outp->virt;
		if (CTrsp->CommandResponse.bits.CmdRsp ==
		    cpu_to_be16(SLI_CT_RESPONSE_FS_ACC)) {
			lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
					 "4105 NameServer Rsp Data: x%x x%x "
					 "x%x x%x sz x%x\n",
					 vport->fc_flag,
					 CTreq->un.gid.Fc4Type,
					 vport->num_disc_nodes,
					 vport->gidft_inp,
					 get_job_data_placed(phba, rspiocb));

			lpfc_ns_rsp(vport,
				    outp,
				    CTreq->un.gid.Fc4Type,
				    get_job_data_placed(phba, rspiocb));
		} else if (CTrsp->CommandResponse.bits.CmdRsp ==
			   be16_to_cpu(SLI_CT_RESPONSE_FS_RJT)) {
			/* NameServer Rsp Error */
			if ((CTrsp->ReasonCode == SLI_CT_UNABLE_TO_PERFORM_REQ)
			    && (CTrsp->Explanation == SLI_CT_NO_FC4_TYPES)) {
				lpfc_printf_vlog(
					vport, KERN_INFO, LOG_DISCOVERY,
					"4106 No NameServer Entries "
					"Data: x%x x%x x%x x%x\n",
					CTrsp->CommandResponse.bits.CmdRsp,
					(uint32_t)CTrsp->ReasonCode,
					(uint32_t)CTrsp->Explanation,
					vport->fc_flag);

				lpfc_debugfs_disc_trc(
				vport, LPFC_DISC_TRC_CT,
				"GID_PT no entry  cmd:x%x rsn:x%x exp:x%x",
				(uint32_t)CTrsp->CommandResponse.bits.CmdRsp,
				(uint32_t)CTrsp->ReasonCode,
				(uint32_t)CTrsp->Explanation);
			} else {
				lpfc_printf_vlog(
					vport, KERN_INFO, LOG_DISCOVERY,
					"4107 NameServer Rsp Error "
					"Data: x%x x%x x%x x%x\n",
					CTrsp->CommandResponse.bits.CmdRsp,
					(uint32_t)CTrsp->ReasonCode,
					(uint32_t)CTrsp->Explanation,
					vport->fc_flag);

				lpfc_debugfs_disc_trc(
				vport, LPFC_DISC_TRC_CT,
				"GID_PT rsp err1  cmd:x%x rsn:x%x exp:x%x",
				(uint32_t)CTrsp->CommandResponse.bits.CmdRsp,
				(uint32_t)CTrsp->ReasonCode,
				(uint32_t)CTrsp->Explanation);
			}
		} else {
			/* NameServer Rsp Error */
			lpfc_printf_vlog(vport, KERN_ERR, LOG_TRACE_EVENT,
					 "4109 NameServer Rsp Error "
					 "Data: x%x x%x x%x x%x\n",
					 CTrsp->CommandResponse.bits.CmdRsp,
					 (uint32_t)CTrsp->ReasonCode,
					 (uint32_t)CTrsp->Explanation,
					 vport->fc_flag);

			lpfc_debugfs_disc_trc(
				vport, LPFC_DISC_TRC_CT,
				"GID_PT rsp err2  cmd:x%x rsn:x%x exp:x%x",
				(uint32_t)CTrsp->CommandResponse.bits.CmdRsp,
				(uint32_t)CTrsp->ReasonCode,
				(uint32_t)CTrsp->Explanation);
		}
		if (vport->gidft_inp)
			vport->gidft_inp--;
	}

	lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
			 "6450 GID_PT cmpl inp %d disc %d\n",
			 vport->gidft_inp, vport->num_disc_nodes);

	/* Link up / RSCN discovery */
	if ((vport->num_disc_nodes == 0) &&
	    (vport->gidft_inp == 0)) {
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
			} else {
				lpfc_els_flush_rscn(vport);
			}
		}

		lpfc_disc_start(vport);
	}
out:
	lpfc_ct_free_iocb(phba, cmdiocb);
	lpfc_nlp_put(ndlp);
}

static void
lpfc_cmpl_ct_cmd_gff_id(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
			struct lpfc_iocbq *rspiocb)
{
	struct lpfc_vport *vport = cmdiocb->vport;
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct lpfc_dmabuf *inp = cmdiocb->cmd_dmabuf;
	struct lpfc_dmabuf *outp = cmdiocb->rsp_dmabuf;
	struct lpfc_sli_ct_request *CTrsp;
	int did, rc, retry;
	uint8_t fbits;
	struct lpfc_nodelist *ndlp = NULL, *free_ndlp = NULL;
	u32 ulp_status = get_job_ulpstatus(phba, rspiocb);
	u32 ulp_word4 = get_job_word4(phba, rspiocb);

	did = ((struct lpfc_sli_ct_request *) inp->virt)->un.gff.PortId;
	did = be32_to_cpu(did);

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_CT,
		"GFF_ID cmpl:     status:x%x/x%x did:x%x",
		ulp_status, ulp_word4, did);

	/* Ignore response if link flipped after this request was made */
	if (cmdiocb->event_tag != phba->fc_eventTag) {
		lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
				 "9045 Event tag mismatch. Ignoring NS rsp\n");
		goto iocb_free;
	}

	if (ulp_status == IOSTAT_SUCCESS) {
		/* Good status, continue checking */
		CTrsp = (struct lpfc_sli_ct_request *) outp->virt;
		fbits = CTrsp->un.gff_acc.fbits[FCP_TYPE_FEATURE_OFFSET];

		lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
				 "6431 Process GFF_ID rsp for %08x "
				 "fbits %02x %s %s\n",
				 did, fbits,
				 (fbits & FC4_FEATURE_INIT) ? "Initiator" : " ",
				 (fbits & FC4_FEATURE_TARGET) ? "Target" : " ");

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
			if (ulp_status == IOSTAT_LOCAL_REJECT) {
				switch ((ulp_word4 &
					IOERR_PARAM_MASK)) {

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
					free_ndlp = cmdiocb->ndlp;
					lpfc_ct_free_iocb(phba, cmdiocb);
					lpfc_nlp_put(free_ndlp);
					return;
				}
			}
		}
		lpfc_printf_vlog(vport, KERN_ERR, LOG_TRACE_EVENT,
				 "0267 NameServer GFF Rsp "
				 "x%x Error (%d %d) Data: x%x x%x\n",
				 did, ulp_status, ulp_word4,
				 vport->fc_flag, vport->fc_rscn_id_cnt);
	}

	/* This is a target port, unregistered port, or the GFF_ID failed */
	ndlp = lpfc_setup_disc_node(vport, did);
	if (ndlp) {
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

	lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
			 "6451 GFF_ID cmpl inp %d disc %d\n",
			 vport->gidft_inp, vport->num_disc_nodes);

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

iocb_free:
	free_ndlp = cmdiocb->ndlp;
	lpfc_ct_free_iocb(phba, cmdiocb);
	lpfc_nlp_put(free_ndlp);
	return;
}

static void
lpfc_cmpl_ct_cmd_gft_id(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
			struct lpfc_iocbq *rspiocb)
{
	struct lpfc_vport *vport = cmdiocb->vport;
	struct lpfc_dmabuf *inp = cmdiocb->cmd_dmabuf;
	struct lpfc_dmabuf *outp = cmdiocb->rsp_dmabuf;
	struct lpfc_sli_ct_request *CTrsp;
	int did;
	struct lpfc_nodelist *ndlp = NULL;
	struct lpfc_nodelist *ns_ndlp = NULL;
	uint32_t fc4_data_0, fc4_data_1;
	u32 ulp_status = get_job_ulpstatus(phba, rspiocb);
	u32 ulp_word4 = get_job_word4(phba, rspiocb);

	did = ((struct lpfc_sli_ct_request *)inp->virt)->un.gft.PortId;
	did = be32_to_cpu(did);

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_CT,
			      "GFT_ID cmpl: status:x%x/x%x did:x%x",
			      ulp_status, ulp_word4, did);

	/* Ignore response if link flipped after this request was made */
	if ((uint32_t) cmdiocb->event_tag != phba->fc_eventTag) {
		lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
				 "9046 Event tag mismatch. Ignoring NS rsp\n");
		goto out;
	}

	/* Preserve the nameserver node to release the reference. */
	ns_ndlp = cmdiocb->ndlp;

	if (ulp_status == IOSTAT_SUCCESS) {
		/* Good status, continue checking */
		CTrsp = (struct lpfc_sli_ct_request *)outp->virt;
		fc4_data_0 = be32_to_cpu(CTrsp->un.gft_acc.fc4_types[0]);
		fc4_data_1 = be32_to_cpu(CTrsp->un.gft_acc.fc4_types[1]);

		lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
				 "6432 Process GFT_ID rsp for %08x "
				 "Data %08x %08x %s %s\n",
				 did, fc4_data_0, fc4_data_1,
				 (fc4_data_0 & LPFC_FC4_TYPE_BITMASK) ?
				  "FCP" : " ",
				 (fc4_data_1 & LPFC_FC4_TYPE_BITMASK) ?
				  "NVME" : " ");

		/* Lookup the NPort_ID queried in the GFT_ID and find the
		 * driver's local node.  It's an error if the driver
		 * doesn't have one.
		 */
		ndlp = lpfc_findnode_did(vport, did);
		if (ndlp) {
			/* The bitmask value for FCP and NVME FCP types is
			 * the same because they are 32 bits distant from
			 * each other in word0 and word0.
			 */
			if (fc4_data_0 & LPFC_FC4_TYPE_BITMASK)
				ndlp->nlp_fc4_type |= NLP_FC4_FCP;
			if (fc4_data_1 &  LPFC_FC4_TYPE_BITMASK)
				ndlp->nlp_fc4_type |= NLP_FC4_NVME;
			lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
					 "3064 Setting ndlp x%px, DID x%06x "
					 "with FC4 x%08x, Data: x%08x x%08x "
					 "%d\n",
					 ndlp, did, ndlp->nlp_fc4_type,
					 FC_TYPE_FCP, FC_TYPE_NVME,
					 ndlp->nlp_state);

			if (ndlp->nlp_state == NLP_STE_REG_LOGIN_ISSUE &&
			    ndlp->nlp_fc4_type) {
				ndlp->nlp_prev_state = NLP_STE_REG_LOGIN_ISSUE;

				lpfc_nlp_set_state(vport, ndlp,
						   NLP_STE_PRLI_ISSUE);
				lpfc_issue_els_prli(vport, ndlp, 0);
			} else if (!ndlp->nlp_fc4_type) {
				/* If fc4 type is still unknown, then LOGO */
				lpfc_printf_vlog(vport, KERN_INFO,
						 LOG_DISCOVERY,
						 "6443 Sending LOGO ndlp x%px,"
						 "DID x%06x with fc4_type: "
						 "x%08x, state: %d\n",
						 ndlp, did, ndlp->nlp_fc4_type,
						 ndlp->nlp_state);
				lpfc_issue_els_logo(vport, ndlp, 0);
				ndlp->nlp_prev_state = NLP_STE_REG_LOGIN_ISSUE;
				lpfc_nlp_set_state(vport, ndlp,
						   NLP_STE_NPR_NODE);
			}
		}
	} else
		lpfc_printf_vlog(vport, KERN_ERR, LOG_TRACE_EVENT,
				 "3065 GFT_ID failed x%08x\n", ulp_status);

out:
	lpfc_ct_free_iocb(phba, cmdiocb);
	lpfc_nlp_put(ns_ndlp);
}

static void
lpfc_cmpl_ct(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
	     struct lpfc_iocbq *rspiocb)
{
	struct lpfc_vport *vport = cmdiocb->vport;
	struct lpfc_dmabuf *inp;
	struct lpfc_dmabuf *outp;
	struct lpfc_sli_ct_request *CTrsp;
	struct lpfc_nodelist *ndlp;
	int cmdcode, rc;
	uint8_t retry;
	uint32_t latt;
	u32 ulp_status = get_job_ulpstatus(phba, rspiocb);
	u32 ulp_word4 = get_job_word4(phba, rspiocb);

	/* First save ndlp, before we overwrite it */
	ndlp = cmdiocb->ndlp;

	/* we pass cmdiocb to state machine which needs rspiocb as well */
	cmdiocb->rsp_iocb = rspiocb;

	inp = cmdiocb->cmd_dmabuf;
	outp = cmdiocb->rsp_dmabuf;

	cmdcode = be16_to_cpu(((struct lpfc_sli_ct_request *) inp->virt)->
					CommandResponse.bits.CmdRsp);
	CTrsp = (struct lpfc_sli_ct_request *) outp->virt;

	latt = lpfc_els_chk_latt(vport);

	/* RFT request completes status <ulp_status> CmdRsp <CmdRsp> */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
			 "0209 CT Request completes, latt %d, "
			 "ulp_status x%x CmdRsp x%x, Context x%x, Tag x%x\n",
			 latt, ulp_status,
			 CTrsp->CommandResponse.bits.CmdRsp,
			 get_job_ulpcontext(phba, cmdiocb), cmdiocb->iotag);

	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_CT,
		"CT cmd cmpl:     status:x%x/x%x cmd:x%x",
		ulp_status, ulp_word4, cmdcode);

	if (ulp_status) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_TRACE_EVENT,
				 "0268 NS cmd x%x Error (x%x x%x)\n",
				 cmdcode, ulp_status, ulp_word4);

		if (ulp_status == IOSTAT_LOCAL_REJECT &&
		    (((ulp_word4 & IOERR_PARAM_MASK) ==
		      IOERR_SLI_DOWN) ||
		     ((ulp_word4 & IOERR_PARAM_MASK) ==
		      IOERR_SLI_ABORTED)))
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
	lpfc_ct_free_iocb(phba, cmdiocb);
	lpfc_nlp_put(ndlp);
	return;
}

static void
lpfc_cmpl_ct_cmd_rft_id(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
			struct lpfc_iocbq *rspiocb)
{
	struct lpfc_vport *vport = cmdiocb->vport;
	u32 ulp_status = get_job_ulpstatus(phba, rspiocb);

	if (ulp_status == IOSTAT_SUCCESS) {
		struct lpfc_dmabuf *outp;
		struct lpfc_sli_ct_request *CTrsp;

		outp = cmdiocb->rsp_dmabuf;
		CTrsp = (struct lpfc_sli_ct_request *)outp->virt;
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
	struct lpfc_vport *vport = cmdiocb->vport;
	u32 ulp_status = get_job_ulpstatus(phba, rspiocb);

	if (ulp_status == IOSTAT_SUCCESS) {
		struct lpfc_dmabuf *outp;
		struct lpfc_sli_ct_request *CTrsp;

		outp = cmdiocb->rsp_dmabuf;
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
	struct lpfc_vport *vport = cmdiocb->vport;
	u32 ulp_status = get_job_ulpstatus(phba, rspiocb);

	if (ulp_status == IOSTAT_SUCCESS) {
		struct lpfc_dmabuf *outp;
		struct lpfc_sli_ct_request *CTrsp;

		outp = cmdiocb->rsp_dmabuf;
		CTrsp = (struct lpfc_sli_ct_request *)outp->virt;
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
	struct lpfc_vport *vport = cmdiocb->vport;
	u32 ulp_status = get_job_ulpstatus(phba, rspiocb);

	if (ulp_status == IOSTAT_SUCCESS) {
		struct lpfc_dmabuf *outp;
		struct lpfc_sli_ct_request *CTrsp;

		outp = cmdiocb->rsp_dmabuf;
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
	struct lpfc_vport *vport = cmdiocb->vport;
	u32 ulp_status = get_job_ulpstatus(phba, rspiocb);

	if (ulp_status == IOSTAT_SUCCESS) {
		struct lpfc_dmabuf *outp;
		struct lpfc_sli_ct_request *CTrsp;

		outp = cmdiocb->rsp_dmabuf;
		CTrsp = (struct lpfc_sli_ct_request *)outp->virt;
		if (CTrsp->CommandResponse.bits.CmdRsp ==
		    be16_to_cpu(SLI_CT_RESPONSE_FS_ACC))
			vport->ct_flags |= FC_CT_RFF_ID;
	}
	lpfc_cmpl_ct(phba, cmdiocb, rspiocb);
	return;
}

/*
 * Although the symbolic port name is thought to be an integer
 * as of January 18, 2016, leave it as a string until more of
 * the record state becomes defined.
 */
int
lpfc_vport_symbolic_port_name(struct lpfc_vport *vport, char *symbol,
	size_t size)
{
	int n;

	/*
	 * Use the lpfc board number as the Symbolic Port
	 * Name object.  NPIV is not in play so this integer
	 * value is sufficient and unique per FC-ID.
	 */
	n = scnprintf(symbol, size, "%d", vport->phba->brd_no);
	return n;
}


int
lpfc_vport_symbolic_node_name(struct lpfc_vport *vport, char *symbol,
	size_t size)
{
	char fwrev[FW_REV_STR_SIZE] = {0};
	char tmp[MAXHOSTNAMELEN] = {0};

	memset(symbol, 0, size);

	scnprintf(tmp, sizeof(tmp), "Emulex %s", vport->phba->ModelName);
	if (strlcat(symbol, tmp, size) >= size)
		goto buffer_done;

	lpfc_decode_firmware_rev(vport->phba, fwrev, 0);
	scnprintf(tmp, sizeof(tmp), " FV%s", fwrev);
	if (strlcat(symbol, tmp, size) >= size)
		goto buffer_done;

	scnprintf(tmp, sizeof(tmp), " DV%s", lpfc_release_version);
	if (strlcat(symbol, tmp, size) >= size)
		goto buffer_done;

	scnprintf(tmp, sizeof(tmp), " HN:%s", vport->phba->os_host_name);
	if (strlcat(symbol, tmp, size) >= size)
		goto buffer_done;

	/* Note :- OS name is "Linux" */
	scnprintf(tmp, sizeof(tmp), " OS:%s", init_utsname()->sysname);
	strlcat(symbol, tmp, size);

buffer_done:
	return strnlen(symbol, size);

}

static uint32_t
lpfc_find_map_node(struct lpfc_vport *vport)
{
	struct lpfc_nodelist *ndlp, *next_ndlp;
	struct Scsi_Host  *shost;
	uint32_t cnt = 0;

	shost = lpfc_shost_from_vport(vport);
	spin_lock_irq(shost->host_lock);
	list_for_each_entry_safe(ndlp, next_ndlp, &vport->fc_nodes, nlp_listp) {
		if (ndlp->nlp_type & NLP_FABRIC)
			continue;
		if ((ndlp->nlp_state == NLP_STE_MAPPED_NODE) ||
		    (ndlp->nlp_state == NLP_STE_UNMAPPED_NODE))
			cnt++;
	}
	spin_unlock_irq(shost->host_lock);
	return cnt;
}

/*
 * This routine will return the FC4 Type associated with the CT
 * GID_FT command.
 */
int
lpfc_get_gidft_type(struct lpfc_vport *vport, struct lpfc_iocbq *cmdiocb)
{
	struct lpfc_sli_ct_request *CtReq;
	struct lpfc_dmabuf *mp;
	uint32_t type;

	mp = cmdiocb->cmd_dmabuf;
	if (mp == NULL)
		return 0;
	CtReq = (struct lpfc_sli_ct_request *)mp->virt;
	type = (uint32_t)CtReq->un.gid.Fc4Type;
	if ((type != SLI_CTPT_FCP) && (type != SLI_CTPT_NVME))
		return 0;
	return type;
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
	uint32_t *ptr;
	uint32_t rsp_size = 1024;
	size_t   size;
	int rc = 0;

	ndlp = lpfc_findnode_did(vport, NameServer_DID);
	if (!ndlp || ndlp->nlp_state != NLP_STE_UNMAPPED_NODE) {
		rc=1;
		goto ns_cmd_exit;
	}

	/* fill in BDEs for command */
	/* Allocate buffer for command payload */
	mp = kmalloc(sizeof(struct lpfc_dmabuf), GFP_KERNEL);
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
	bmp = kmalloc(sizeof(struct lpfc_dmabuf), GFP_KERNEL);
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
			 "0236 NameServer Req Data: x%x x%x x%x x%x\n",
			 cmdcode, vport->fc_flag, vport->fc_rscn_id_cnt,
			 context);

	bpl = (struct ulp_bde64 *) bmp->virt;
	memset(bpl, 0, sizeof(struct ulp_bde64));
	bpl->addrHigh = le32_to_cpu(putPaddrHigh(mp->phys) );
	bpl->addrLow = le32_to_cpu(putPaddrLow(mp->phys) );
	bpl->tus.f.bdeFlags = 0;
	if (cmdcode == SLI_CTNS_GID_FT)
		bpl->tus.f.bdeSize = GID_REQUEST_SZ;
	else if (cmdcode == SLI_CTNS_GID_PT)
		bpl->tus.f.bdeSize = GID_REQUEST_SZ;
	else if (cmdcode == SLI_CTNS_GFF_ID)
		bpl->tus.f.bdeSize = GFF_REQUEST_SZ;
	else if (cmdcode == SLI_CTNS_GFT_ID)
		bpl->tus.f.bdeSize = GFT_REQUEST_SZ;
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
	memset(CtReq, 0, sizeof(struct lpfc_sli_ct_request));
	CtReq->RevisionId.bits.Revision = SLI_CT_REVISION;
	CtReq->RevisionId.bits.InId = 0;
	CtReq->FsType = SLI_CT_DIRECTORY_SERVICE;
	CtReq->FsSubType = SLI_CT_DIRECTORY_NAME_SERVER;
	CtReq->CommandResponse.bits.Size = 0;
	switch (cmdcode) {
	case SLI_CTNS_GID_FT:
		CtReq->CommandResponse.bits.CmdRsp =
		    cpu_to_be16(SLI_CTNS_GID_FT);
		CtReq->un.gid.Fc4Type = context;

		if (vport->port_state < LPFC_NS_QRY)
			vport->port_state = LPFC_NS_QRY;
		lpfc_set_disctmo(vport);
		cmpl = lpfc_cmpl_ct_cmd_gid_ft;
		rsp_size = FC_MAX_NS_RSP;
		break;

	case SLI_CTNS_GID_PT:
		CtReq->CommandResponse.bits.CmdRsp =
		    cpu_to_be16(SLI_CTNS_GID_PT);
		CtReq->un.gid.PortType = context;

		if (vport->port_state < LPFC_NS_QRY)
			vport->port_state = LPFC_NS_QRY;
		lpfc_set_disctmo(vport);
		cmpl = lpfc_cmpl_ct_cmd_gid_pt;
		rsp_size = FC_MAX_NS_RSP;
		break;

	case SLI_CTNS_GFF_ID:
		CtReq->CommandResponse.bits.CmdRsp =
			cpu_to_be16(SLI_CTNS_GFF_ID);
		CtReq->un.gff.PortId = cpu_to_be32(context);
		cmpl = lpfc_cmpl_ct_cmd_gff_id;
		break;

	case SLI_CTNS_GFT_ID:
		CtReq->CommandResponse.bits.CmdRsp =
			cpu_to_be16(SLI_CTNS_GFT_ID);
		CtReq->un.gft.PortId = cpu_to_be32(context);
		cmpl = lpfc_cmpl_ct_cmd_gft_id;
		break;

	case SLI_CTNS_RFT_ID:
		vport->ct_flags &= ~FC_CT_RFT_ID;
		CtReq->CommandResponse.bits.CmdRsp =
		    cpu_to_be16(SLI_CTNS_RFT_ID);
		CtReq->un.rft.port_id = cpu_to_be32(vport->fc_myDID);

		/* Register Application Services type if vmid enabled. */
		if (phba->cfg_vmid_app_header)
			CtReq->un.rft.app_serv_reg =
				cpu_to_be32(RFT_APP_SERV_REG);

		/* Register FC4 FCP type if enabled.  */
		if (vport->cfg_enable_fc4_type == LPFC_ENABLE_BOTH ||
		    vport->cfg_enable_fc4_type == LPFC_ENABLE_FCP)
			CtReq->un.rft.fcp_reg = cpu_to_be32(RFT_FCP_REG);

		/* Register NVME type if enabled. */
		if (vport->cfg_enable_fc4_type == LPFC_ENABLE_BOTH ||
		    vport->cfg_enable_fc4_type == LPFC_ENABLE_NVME)
			CtReq->un.rft.nvme_reg = cpu_to_be32(RFT_NVME_REG);

		ptr = (uint32_t *)CtReq;
		lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
				 "6433 Issue RFT (%s %s %s): %08x %08x %08x "
				 "%08x %08x %08x %08x %08x\n",
				 CtReq->un.rft.fcp_reg ? "FCP" : " ",
				 CtReq->un.rft.nvme_reg ? "NVME" : " ",
				 CtReq->un.rft.app_serv_reg ? "APPS" : " ",
				 *ptr, *(ptr + 1), *(ptr + 2), *(ptr + 3),
				 *(ptr + 4), *(ptr + 5),
				 *(ptr + 6), *(ptr + 7));
		cmpl = lpfc_cmpl_ct_cmd_rft_id;
		break;

	case SLI_CTNS_RNN_ID:
		vport->ct_flags &= ~FC_CT_RNN_ID;
		CtReq->CommandResponse.bits.CmdRsp =
		    cpu_to_be16(SLI_CTNS_RNN_ID);
		CtReq->un.rnn.PortId = cpu_to_be32(vport->fc_myDID);
		memcpy(CtReq->un.rnn.wwnn,  &vport->fc_nodename,
		       sizeof(struct lpfc_name));
		cmpl = lpfc_cmpl_ct_cmd_rnn_id;
		break;

	case SLI_CTNS_RSPN_ID:
		vport->ct_flags &= ~FC_CT_RSPN_ID;
		CtReq->CommandResponse.bits.CmdRsp =
		    cpu_to_be16(SLI_CTNS_RSPN_ID);
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
		    cpu_to_be16(SLI_CTNS_RSNN_NN);
		memcpy(CtReq->un.rsnn.wwnn, &vport->fc_nodename,
		       sizeof(struct lpfc_name));
		size = sizeof(CtReq->un.rsnn.symbname);
		CtReq->un.rsnn.len =
			lpfc_vport_symbolic_node_name(vport,
			CtReq->un.rsnn.symbname, size);
		cmpl = lpfc_cmpl_ct_cmd_rsnn_nn;
		break;
	case SLI_CTNS_DA_ID:
		/* Implement DA_ID Nameserver request */
		CtReq->CommandResponse.bits.CmdRsp =
			cpu_to_be16(SLI_CTNS_DA_ID);
		CtReq->un.da_id.port_id = cpu_to_be32(vport->fc_myDID);
		cmpl = lpfc_cmpl_ct_cmd_da_id;
		break;
	case SLI_CTNS_RFF_ID:
		vport->ct_flags &= ~FC_CT_RFF_ID;
		CtReq->CommandResponse.bits.CmdRsp =
		    cpu_to_be16(SLI_CTNS_RFF_ID);
		CtReq->un.rff.PortId = cpu_to_be32(vport->fc_myDID);
		CtReq->un.rff.fbits = FC4_FEATURE_INIT;

		/* The driver always supports FC_TYPE_FCP.  However, the
		 * caller can specify NVME (type x28) as well.  But only
		 * these that FC4 type is supported.
		 */
		if (((vport->cfg_enable_fc4_type == LPFC_ENABLE_BOTH) ||
		     (vport->cfg_enable_fc4_type == LPFC_ENABLE_NVME)) &&
		    (context == FC_TYPE_NVME)) {
			if ((vport == phba->pport) && phba->nvmet_support) {
				CtReq->un.rff.fbits = (FC4_FEATURE_TARGET |
					FC4_FEATURE_NVME_DISC);
				lpfc_nvmet_update_targetport(phba);
			} else {
				lpfc_nvme_update_localport(vport);
			}
			CtReq->un.rff.type_code = context;

		} else if (((vport->cfg_enable_fc4_type == LPFC_ENABLE_BOTH) ||
			    (vport->cfg_enable_fc4_type == LPFC_ENABLE_FCP)) &&
			   (context == FC_TYPE_FCP))
			CtReq->un.rff.type_code = context;

		else
			goto ns_cmd_free_bmpvirt;

		ptr = (uint32_t *)CtReq;
		lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
				 "6434 Issue RFF (%s): %08x %08x %08x %08x "
				 "%08x %08x %08x %08x\n",
				 (context == FC_TYPE_NVME) ? "NVME" : "FCP",
				 *ptr, *(ptr + 1), *(ptr + 2), *(ptr + 3),
				 *(ptr + 4), *(ptr + 5),
				 *(ptr + 6), *(ptr + 7));
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

ns_cmd_free_bmpvirt:
	lpfc_mbuf_free(phba, bmp->virt, bmp->phys);
ns_cmd_free_bmp:
	kfree(bmp);
ns_cmd_free_mpvirt:
	lpfc_mbuf_free(phba, mp->virt, mp->phys);
ns_cmd_free_mp:
	kfree(mp);
ns_cmd_exit:
	lpfc_printf_vlog(vport, KERN_ERR, LOG_TRACE_EVENT,
			 "0266 Issue NameServer Req x%x err %d Data: x%x x%x\n",
			 cmdcode, rc, vport->fc_flag, vport->fc_rscn_id_cnt);
	return 1;
}

/**
 * lpfc_fdmi_rprt_defer - Check for any deferred FDMI RPRT commands
 * @phba: Pointer to HBA context object.
 * @mask: Initial port attributes mask
 *
 * This function checks to see if any vports have deferred their FDMI RPRT.
 * A vports RPRT may be deferred if it is issued before the primary ports
 * RHBA completes.
 */
static void
lpfc_fdmi_rprt_defer(struct lpfc_hba *phba, uint32_t mask)
{
	struct lpfc_vport **vports;
	struct lpfc_vport *vport;
	struct lpfc_nodelist *ndlp;
	int i;

	phba->hba_flag |= HBA_RHBA_CMPL;
	vports = lpfc_create_vport_work_array(phba);
	if (vports) {
		for (i = 0; i <= phba->max_vports && vports[i] != NULL; i++) {
			vport = vports[i];
			ndlp = lpfc_findnode_did(phba->pport, FDMI_DID);
			if (!ndlp)
				continue;
			if (vport->ct_flags & FC_CT_RPRT_DEFER) {
				vport->ct_flags &= ~FC_CT_RPRT_DEFER;
				vport->fdmi_port_mask = mask;
				lpfc_fdmi_cmd(vport, ndlp, SLI_MGMT_RPRT, 0);
			}
		}
	}
	lpfc_destroy_vport_work_array(phba, vports);
}

/**
 * lpfc_cmpl_ct_disc_fdmi - Handle a discovery FDMI completion
 * @phba: Pointer to HBA context object.
 * @cmdiocb: Pointer to the command IOCBQ.
 * @rspiocb: Pointer to the response IOCBQ.
 *
 * This function to handle the completion of a driver initiated FDMI
 * CT command issued during discovery.
 */
static void
lpfc_cmpl_ct_disc_fdmi(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
		       struct lpfc_iocbq *rspiocb)
{
	struct lpfc_vport *vport = cmdiocb->vport;
	struct lpfc_dmabuf *inp = cmdiocb->cmd_dmabuf;
	struct lpfc_dmabuf *outp = cmdiocb->rsp_dmabuf;
	struct lpfc_sli_ct_request *CTcmd = inp->virt;
	struct lpfc_sli_ct_request *CTrsp = outp->virt;
	uint16_t fdmi_cmd = CTcmd->CommandResponse.bits.CmdRsp;
	uint16_t fdmi_rsp = CTrsp->CommandResponse.bits.CmdRsp;
	struct lpfc_nodelist *ndlp, *free_ndlp = NULL;
	uint32_t latt, cmd, err;
	u32 ulp_status = get_job_ulpstatus(phba, rspiocb);
	u32 ulp_word4 = get_job_word4(phba, rspiocb);

	latt = lpfc_els_chk_latt(vport);
	lpfc_debugfs_disc_trc(vport, LPFC_DISC_TRC_CT,
		"FDMI cmpl:       status:x%x/x%x latt:%d",
		ulp_status, ulp_word4, latt);

	if (latt || ulp_status) {

		/* Look for a retryable error */
		if (ulp_status == IOSTAT_LOCAL_REJECT) {
			switch ((ulp_word4 & IOERR_PARAM_MASK)) {
			case IOERR_SLI_ABORTED:
			case IOERR_SLI_DOWN:
				/* Driver aborted this IO.  No retry as error
				 * is likely Offline->Online or some adapter
				 * error.  Recovery will try again.
				 */
				break;
			case IOERR_ABORT_IN_PROGRESS:
			case IOERR_SEQUENCE_TIMEOUT:
			case IOERR_ILLEGAL_FRAME:
			case IOERR_NO_RESOURCES:
			case IOERR_ILLEGAL_COMMAND:
				cmdiocb->retry++;
				if (cmdiocb->retry >= LPFC_FDMI_MAX_RETRY)
					break;

				/* Retry the same FDMI command */
				err = lpfc_sli_issue_iocb(phba, LPFC_ELS_RING,
							  cmdiocb, 0);
				if (err == IOCB_ERROR)
					break;
				return;
			default:
				break;
			}
		}

		lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
				 "0229 FDMI cmd %04x failed, latt = %d "
				 "ulp_status: x%x, rid x%x\n",
				 be16_to_cpu(fdmi_cmd), latt, ulp_status,
				 ulp_word4);
	}

	free_ndlp = cmdiocb->ndlp;
	lpfc_ct_free_iocb(phba, cmdiocb);
	lpfc_nlp_put(free_ndlp);

	ndlp = lpfc_findnode_did(vport, FDMI_DID);
	if (!ndlp)
		return;

	/* Check for a CT LS_RJT response */
	cmd =  be16_to_cpu(fdmi_cmd);
	if (fdmi_rsp == cpu_to_be16(SLI_CT_RESPONSE_FS_RJT)) {
		/* FDMI rsp failed */
		lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY | LOG_ELS,
				 "0220 FDMI cmd failed FS_RJT Data: x%x", cmd);

		/* Should we fallback to FDMI-2 / FDMI-1 ? */
		switch (cmd) {
		case SLI_MGMT_RHBA:
			if (vport->fdmi_hba_mask == LPFC_FDMI2_HBA_ATTR) {
				/* Fallback to FDMI-1 for HBA attributes */
				vport->fdmi_hba_mask = LPFC_FDMI1_HBA_ATTR;

				/* If HBA attributes are FDMI1, so should
				 * port attributes be for consistency.
				 */
				vport->fdmi_port_mask = LPFC_FDMI1_PORT_ATTR;
				/* Start over */
				lpfc_fdmi_cmd(vport, ndlp, SLI_MGMT_DHBA, 0);
			}
			return;

		case SLI_MGMT_RPRT:
			if (vport->port_type != LPFC_PHYSICAL_PORT) {
				ndlp = lpfc_findnode_did(phba->pport, FDMI_DID);
				if (!ndlp)
					return;
			}
			if (vport->fdmi_port_mask == LPFC_FDMI2_PORT_ATTR) {
				/* Fallback to FDMI-1 */
				vport->fdmi_port_mask = LPFC_FDMI1_PORT_ATTR;
				/* Start over */
				lpfc_fdmi_cmd(vport, ndlp, cmd, 0);
				return;
			}
			if (vport->fdmi_port_mask == LPFC_FDMI2_SMART_ATTR) {
				vport->fdmi_port_mask = LPFC_FDMI2_PORT_ATTR;
				/* Retry the same command */
				lpfc_fdmi_cmd(vport, ndlp, cmd, 0);
			}
			return;

		case SLI_MGMT_RPA:
			/* No retry on Vendor, RPA only done on physical port */
			if (phba->link_flag & LS_CT_VEN_RPA) {
				phba->link_flag &= ~LS_CT_VEN_RPA;
				if (phba->cmf_active_mode == LPFC_CFG_OFF)
					return;
				lpfc_printf_log(phba, KERN_WARNING,
						LOG_DISCOVERY | LOG_ELS,
						"6460 VEN FDMI RPA RJT\n");
				return;
			}
			if (vport->fdmi_port_mask == LPFC_FDMI2_PORT_ATTR) {
				/* Fallback to FDMI-1 */
				vport->fdmi_hba_mask = LPFC_FDMI1_HBA_ATTR;
				vport->fdmi_port_mask = LPFC_FDMI1_PORT_ATTR;
				/* Start over */
				lpfc_fdmi_cmd(vport, ndlp, SLI_MGMT_DHBA, 0);
				return;
			}
			if (vport->fdmi_port_mask == LPFC_FDMI2_SMART_ATTR) {
				vport->fdmi_port_mask = LPFC_FDMI2_PORT_ATTR;
				/* Retry the same command */
				lpfc_fdmi_cmd(vport, ndlp, cmd, 0);
			}
			return;
		}
	}

	/*
	 * On success, need to cycle thru FDMI registration for discovery
	 * DHBA -> DPRT -> RHBA -> RPA  (physical port)
	 * DPRT -> RPRT (vports)
	 */
	switch (cmd) {
	case SLI_MGMT_RHBA:
		/* Check for any RPRTs deferred till after RHBA completes */
		lpfc_fdmi_rprt_defer(phba, vport->fdmi_port_mask);

		lpfc_fdmi_cmd(vport, ndlp, SLI_MGMT_RPA, 0);
		break;

	case SLI_MGMT_DHBA:
		lpfc_fdmi_cmd(vport, ndlp, SLI_MGMT_DPRT, 0);
		break;

	case SLI_MGMT_DPRT:
		if (vport->port_type == LPFC_PHYSICAL_PORT) {
			lpfc_fdmi_cmd(vport, ndlp, SLI_MGMT_RHBA, 0);
		} else {
			ndlp = lpfc_findnode_did(phba->pport, FDMI_DID);
			if (!ndlp)
				return;

			/* Only issue a RPRT for the vport if the RHBA
			 * for the physical port completes successfully.
			 * We may have to defer the RPRT accordingly.
			 */
			if (phba->hba_flag & HBA_RHBA_CMPL) {
				lpfc_fdmi_cmd(vport, ndlp, SLI_MGMT_RPRT, 0);
			} else {
				lpfc_printf_vlog(vport, KERN_INFO,
						 LOG_DISCOVERY,
						 "6078 RPRT deferred\n");
				vport->ct_flags |= FC_CT_RPRT_DEFER;
			}
		}
		break;
	case SLI_MGMT_RPA:
		if (vport->port_type == LPFC_PHYSICAL_PORT &&
		    phba->sli4_hba.pc_sli4_params.mi_ver) {
			/* mi is only for the phyical port, no vports */
			if (phba->link_flag & LS_CT_VEN_RPA) {
				lpfc_printf_vlog(vport, KERN_INFO,
						 LOG_DISCOVERY | LOG_ELS |
						 LOG_CGN_MGMT,
						 "6449 VEN RPA FDMI Success\n");
				phba->link_flag &= ~LS_CT_VEN_RPA;
				break;
			}

			lpfc_printf_log(phba, KERN_INFO,
					LOG_DISCOVERY | LOG_CGN_MGMT,
					"6210 Issue Vendor MI FDMI %x\n",
					phba->sli4_hba.pc_sli4_params.mi_ver);

			/* CGN is only for the physical port, no vports */
			if (lpfc_fdmi_cmd(vport, ndlp, cmd,
					  LPFC_FDMI_VENDOR_ATTR_mi) == 0)
				phba->link_flag |= LS_CT_VEN_RPA;
			lpfc_printf_log(phba, KERN_INFO,
					LOG_DISCOVERY | LOG_ELS,
					"6458 Send MI FDMI:%x Flag x%x\n",
					phba->sli4_hba.pc_sli4_params.mi_ver,
					phba->link_flag);
		} else {
			lpfc_printf_log(phba, KERN_INFO,
					LOG_DISCOVERY | LOG_ELS,
					"6459 No FDMI VEN MI support - "
					"RPA Success\n");
		}
		break;
	}
	return;
}


/**
 * lpfc_fdmi_change_check - Check for changed FDMI parameters
 * @vport: pointer to a host virtual N_Port data structure.
 *
 * Check how many mapped NPorts we are connected to
 * Check if our hostname changed
 * Called from hbeat timeout routine to check if any FDMI parameters
 * changed. If so, re-register those Attributes.
 */
void
lpfc_fdmi_change_check(struct lpfc_vport *vport)
{
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_nodelist *ndlp;
	uint16_t cnt;

	if (!lpfc_is_link_up(phba))
		return;

	/* Must be connected to a Fabric */
	if (!(vport->fc_flag & FC_FABRIC))
		return;

	ndlp = lpfc_findnode_did(vport, FDMI_DID);
	if (!ndlp)
		return;

	/* Check if system hostname changed */
	if (strcmp(phba->os_host_name, init_utsname()->nodename)) {
		memset(phba->os_host_name, 0, sizeof(phba->os_host_name));
		scnprintf(phba->os_host_name, sizeof(phba->os_host_name), "%s",
			  init_utsname()->nodename);
		lpfc_ns_cmd(vport, SLI_CTNS_RSNN_NN, 0, 0);

		/* Since this effects multiple HBA and PORT attributes, we need
		 * de-register and go thru the whole FDMI registration cycle.
		 * DHBA -> DPRT -> RHBA -> RPA  (physical port)
		 * DPRT -> RPRT (vports)
		 */
		if (vport->port_type == LPFC_PHYSICAL_PORT) {
			/* For extra Vendor RPA */
			phba->link_flag &= ~LS_CT_VEN_RPA;
			lpfc_fdmi_cmd(vport, ndlp, SLI_MGMT_DHBA, 0);
		} else {
			ndlp = lpfc_findnode_did(phba->pport, FDMI_DID);
			if (!ndlp)
				return;
			lpfc_fdmi_cmd(vport, ndlp, SLI_MGMT_DPRT, 0);
		}

		/* Since this code path registers all the port attributes
		 * we can just return without further checking.
		 */
		return;
	}

	if (!(vport->fdmi_port_mask & LPFC_FDMI_PORT_ATTR_num_disc))
		return;

	/* Check if the number of mapped NPorts changed */
	cnt = lpfc_find_map_node(vport);
	if (cnt == vport->fdmi_num_disc)
		return;

	if (vport->port_type == LPFC_PHYSICAL_PORT) {
		lpfc_fdmi_cmd(vport, ndlp, SLI_MGMT_RPA,
			      LPFC_FDMI_PORT_ATTR_num_disc);
	} else {
		ndlp = lpfc_findnode_did(phba->pport, FDMI_DID);
		if (!ndlp)
			return;
		lpfc_fdmi_cmd(vport, ndlp, SLI_MGMT_RPRT,
			      LPFC_FDMI_PORT_ATTR_num_disc);
	}
}

/* Routines for all individual HBA attributes */
static int
lpfc_fdmi_hba_attr_wwnn(struct lpfc_vport *vport, struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t size;

	ae = &ad->AttrValue;
	memset(ae, 0, sizeof(*ae));

	memcpy(&ae->un.AttrWWN, &vport->fc_sparam.nodeName,
	       sizeof(struct lpfc_name));
	size = FOURBYTES + sizeof(struct lpfc_name);
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RHBA_NODENAME);
	return size;
}
static int
lpfc_fdmi_hba_attr_manufacturer(struct lpfc_vport *vport,
				struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t len, size;

	ae = &ad->AttrValue;
	memset(ae, 0, sizeof(*ae));

	/* This string MUST be consistent with other FC platforms
	 * supported by Broadcom.
	 */
	strncpy(ae->un.AttrString,
		"Emulex Corporation",
		       sizeof(ae->un.AttrString));
	len = strnlen(ae->un.AttrString,
			  sizeof(ae->un.AttrString));
	len += (len & 3) ? (4 - (len & 3)) : 4;
	size = FOURBYTES + len;
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RHBA_MANUFACTURER);
	return size;
}

static int
lpfc_fdmi_hba_attr_sn(struct lpfc_vport *vport, struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t len, size;

	ae = &ad->AttrValue;
	memset(ae, 0, sizeof(*ae));

	strncpy(ae->un.AttrString, phba->SerialNumber,
		sizeof(ae->un.AttrString));
	len = strnlen(ae->un.AttrString,
			  sizeof(ae->un.AttrString));
	len += (len & 3) ? (4 - (len & 3)) : 4;
	size = FOURBYTES + len;
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RHBA_SERIAL_NUMBER);
	return size;
}

static int
lpfc_fdmi_hba_attr_model(struct lpfc_vport *vport,
			 struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t len, size;

	ae = &ad->AttrValue;
	memset(ae, 0, sizeof(*ae));

	strncpy(ae->un.AttrString, phba->ModelName,
		sizeof(ae->un.AttrString));
	len = strnlen(ae->un.AttrString, sizeof(ae->un.AttrString));
	len += (len & 3) ? (4 - (len & 3)) : 4;
	size = FOURBYTES + len;
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RHBA_MODEL);
	return size;
}

static int
lpfc_fdmi_hba_attr_description(struct lpfc_vport *vport,
			       struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t len, size;

	ae = &ad->AttrValue;
	memset(ae, 0, sizeof(*ae));

	strncpy(ae->un.AttrString, phba->ModelDesc,
		sizeof(ae->un.AttrString));
	len = strnlen(ae->un.AttrString,
				  sizeof(ae->un.AttrString));
	len += (len & 3) ? (4 - (len & 3)) : 4;
	size = FOURBYTES + len;
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RHBA_MODEL_DESCRIPTION);
	return size;
}

static int
lpfc_fdmi_hba_attr_hdw_ver(struct lpfc_vport *vport,
			   struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_hba *phba = vport->phba;
	lpfc_vpd_t *vp = &phba->vpd;
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t i, j, incr, size;

	ae = &ad->AttrValue;
	memset(ae, 0, sizeof(*ae));

	/* Convert JEDEC ID to ascii for hardware version */
	incr = vp->rev.biuRev;
	for (i = 0; i < 8; i++) {
		j = (incr & 0xf);
		if (j <= 9)
			ae->un.AttrString[7 - i] =
			    (char)((uint8_t) 0x30 +
				   (uint8_t) j);
		else
			ae->un.AttrString[7 - i] =
			    (char)((uint8_t) 0x61 +
				   (uint8_t) (j - 10));
		incr = (incr >> 4);
	}
	size = FOURBYTES + 8;
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RHBA_HARDWARE_VERSION);
	return size;
}

static int
lpfc_fdmi_hba_attr_drvr_ver(struct lpfc_vport *vport,
			    struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t len, size;

	ae = &ad->AttrValue;
	memset(ae, 0, sizeof(*ae));

	strncpy(ae->un.AttrString, lpfc_release_version,
		sizeof(ae->un.AttrString));
	len = strnlen(ae->un.AttrString,
			  sizeof(ae->un.AttrString));
	len += (len & 3) ? (4 - (len & 3)) : 4;
	size = FOURBYTES + len;
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RHBA_DRIVER_VERSION);
	return size;
}

static int
lpfc_fdmi_hba_attr_rom_ver(struct lpfc_vport *vport,
			   struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t len, size;

	ae = &ad->AttrValue;
	memset(ae, 0, sizeof(*ae));

	if (phba->sli_rev == LPFC_SLI_REV4)
		lpfc_decode_firmware_rev(phba, ae->un.AttrString, 1);
	else
		strncpy(ae->un.AttrString, phba->OptionROMVersion,
			sizeof(ae->un.AttrString));
	len = strnlen(ae->un.AttrString,
			  sizeof(ae->un.AttrString));
	len += (len & 3) ? (4 - (len & 3)) : 4;
	size = FOURBYTES + len;
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RHBA_OPTION_ROM_VERSION);
	return size;
}

static int
lpfc_fdmi_hba_attr_fmw_ver(struct lpfc_vport *vport,
			   struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t len, size;

	ae = &ad->AttrValue;
	memset(ae, 0, sizeof(*ae));

	lpfc_decode_firmware_rev(phba, ae->un.AttrString, 1);
	len = strnlen(ae->un.AttrString,
			  sizeof(ae->un.AttrString));
	len += (len & 3) ? (4 - (len & 3)) : 4;
	size = FOURBYTES + len;
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RHBA_FIRMWARE_VERSION);
	return size;
}

static int
lpfc_fdmi_hba_attr_os_ver(struct lpfc_vport *vport,
			  struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t len, size;

	ae = &ad->AttrValue;
	memset(ae, 0, sizeof(*ae));

	snprintf(ae->un.AttrString, sizeof(ae->un.AttrString), "%s %s %s",
		 init_utsname()->sysname,
		 init_utsname()->release,
		 init_utsname()->version);

	len = strnlen(ae->un.AttrString, sizeof(ae->un.AttrString));
	len += (len & 3) ? (4 - (len & 3)) : 4;
	size = FOURBYTES + len;
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RHBA_OS_NAME_VERSION);
	return size;
}

static int
lpfc_fdmi_hba_attr_ct_len(struct lpfc_vport *vport,
			  struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t size;

	ae = &ad->AttrValue;

	ae->un.AttrInt =  cpu_to_be32(LPFC_MAX_CT_SIZE);
	size = FOURBYTES + sizeof(uint32_t);
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RHBA_MAX_CT_PAYLOAD_LEN);
	return size;
}

static int
lpfc_fdmi_hba_attr_symbolic_name(struct lpfc_vport *vport,
				 struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t len, size;

	ae = &ad->AttrValue;
	memset(ae, 0, sizeof(*ae));

	len = lpfc_vport_symbolic_node_name(vport,
				ae->un.AttrString, 256);
	len += (len & 3) ? (4 - (len & 3)) : 4;
	size = FOURBYTES + len;
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RHBA_SYM_NODENAME);
	return size;
}

static int
lpfc_fdmi_hba_attr_vendor_info(struct lpfc_vport *vport,
			       struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t size;

	ae = &ad->AttrValue;

	/* Nothing is defined for this currently */
	ae->un.AttrInt =  cpu_to_be32(0);
	size = FOURBYTES + sizeof(uint32_t);
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RHBA_VENDOR_INFO);
	return size;
}

static int
lpfc_fdmi_hba_attr_num_ports(struct lpfc_vport *vport,
			     struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t size;

	ae = &ad->AttrValue;

	/* Each driver instance corresponds to a single port */
	ae->un.AttrInt =  cpu_to_be32(1);
	size = FOURBYTES + sizeof(uint32_t);
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RHBA_NUM_PORTS);
	return size;
}

static int
lpfc_fdmi_hba_attr_fabric_wwnn(struct lpfc_vport *vport,
			       struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t size;

	ae = &ad->AttrValue;
	memset(ae, 0, sizeof(*ae));

	memcpy(&ae->un.AttrWWN, &vport->fabric_nodename,
	       sizeof(struct lpfc_name));
	size = FOURBYTES + sizeof(struct lpfc_name);
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RHBA_FABRIC_WWNN);
	return size;
}

static int
lpfc_fdmi_hba_attr_bios_ver(struct lpfc_vport *vport,
			    struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t len, size;

	ae = &ad->AttrValue;
	memset(ae, 0, sizeof(*ae));

	strlcat(ae->un.AttrString, phba->BIOSVersion,
		sizeof(ae->un.AttrString));
	len = strnlen(ae->un.AttrString,
			  sizeof(ae->un.AttrString));
	len += (len & 3) ? (4 - (len & 3)) : 4;
	size = FOURBYTES + len;
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RHBA_BIOS_VERSION);
	return size;
}

static int
lpfc_fdmi_hba_attr_bios_state(struct lpfc_vport *vport,
			      struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t size;

	ae = &ad->AttrValue;

	/* Driver doesn't have access to this information */
	ae->un.AttrInt =  cpu_to_be32(0);
	size = FOURBYTES + sizeof(uint32_t);
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RHBA_BIOS_STATE);
	return size;
}

static int
lpfc_fdmi_hba_attr_vendor_id(struct lpfc_vport *vport,
			     struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t len, size;

	ae = &ad->AttrValue;
	memset(ae, 0, sizeof(*ae));

	strncpy(ae->un.AttrString, "EMULEX",
		sizeof(ae->un.AttrString));
	len = strnlen(ae->un.AttrString,
			  sizeof(ae->un.AttrString));
	len += (len & 3) ? (4 - (len & 3)) : 4;
	size = FOURBYTES + len;
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RHBA_VENDOR_ID);
	return size;
}

/* Routines for all individual PORT attributes */
static int
lpfc_fdmi_port_attr_fc4type(struct lpfc_vport *vport,
			    struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_hba   *phba = vport->phba;
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t size;

	ae = &ad->AttrValue;
	memset(ae, 0, sizeof(*ae));

	ae->un.AttrTypes[2] = 0x01; /* Type 0x8 - FCP */
	ae->un.AttrTypes[7] = 0x01; /* Type 0x20 - CT */

	/* Check to see if Firmware supports NVME and on physical port */
	if ((phba->sli_rev == LPFC_SLI_REV4) && (vport == phba->pport) &&
	    phba->sli4_hba.pc_sli4_params.nvme)
		ae->un.AttrTypes[6] = 0x01; /* Type 0x28 - NVME */

	size = FOURBYTES + 32;
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RPRT_SUPPORTED_FC4_TYPES);
	return size;
}

static int
lpfc_fdmi_port_attr_support_speed(struct lpfc_vport *vport,
				  struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_hba   *phba = vport->phba;
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t size;
	u32 tcfg;
	u8 i, cnt;

	ae = &ad->AttrValue;

	ae->un.AttrInt = 0;
	if (!(phba->hba_flag & HBA_FCOE_MODE)) {
		cnt = 0;
		if (phba->sli_rev == LPFC_SLI_REV4) {
			tcfg = phba->sli4_hba.conf_trunk;
			for (i = 0; i < 4; i++, tcfg >>= 1)
				if (tcfg & 1)
					cnt++;
		}

		if (cnt > 2) { /* 4 lane trunk group */
			if (phba->lmt & LMT_64Gb)
				ae->un.AttrInt |= HBA_PORTSPEED_256GFC;
			if (phba->lmt & LMT_32Gb)
				ae->un.AttrInt |= HBA_PORTSPEED_128GFC;
			if (phba->lmt & LMT_16Gb)
				ae->un.AttrInt |= HBA_PORTSPEED_64GFC;
		} else if (cnt) { /* 2 lane trunk group */
			if (phba->lmt & LMT_128Gb)
				ae->un.AttrInt |= HBA_PORTSPEED_256GFC;
			if (phba->lmt & LMT_64Gb)
				ae->un.AttrInt |= HBA_PORTSPEED_128GFC;
			if (phba->lmt & LMT_32Gb)
				ae->un.AttrInt |= HBA_PORTSPEED_64GFC;
			if (phba->lmt & LMT_16Gb)
				ae->un.AttrInt |= HBA_PORTSPEED_32GFC;
		} else {
			if (phba->lmt & LMT_256Gb)
				ae->un.AttrInt |= HBA_PORTSPEED_256GFC;
			if (phba->lmt & LMT_128Gb)
				ae->un.AttrInt |= HBA_PORTSPEED_128GFC;
			if (phba->lmt & LMT_64Gb)
				ae->un.AttrInt |= HBA_PORTSPEED_64GFC;
			if (phba->lmt & LMT_32Gb)
				ae->un.AttrInt |= HBA_PORTSPEED_32GFC;
			if (phba->lmt & LMT_16Gb)
				ae->un.AttrInt |= HBA_PORTSPEED_16GFC;
			if (phba->lmt & LMT_10Gb)
				ae->un.AttrInt |= HBA_PORTSPEED_10GFC;
			if (phba->lmt & LMT_8Gb)
				ae->un.AttrInt |= HBA_PORTSPEED_8GFC;
			if (phba->lmt & LMT_4Gb)
				ae->un.AttrInt |= HBA_PORTSPEED_4GFC;
			if (phba->lmt & LMT_2Gb)
				ae->un.AttrInt |= HBA_PORTSPEED_2GFC;
			if (phba->lmt & LMT_1Gb)
				ae->un.AttrInt |= HBA_PORTSPEED_1GFC;
		}
	} else {
		/* FCoE links support only one speed */
		switch (phba->fc_linkspeed) {
		case LPFC_ASYNC_LINK_SPEED_10GBPS:
			ae->un.AttrInt = HBA_PORTSPEED_10GE;
			break;
		case LPFC_ASYNC_LINK_SPEED_25GBPS:
			ae->un.AttrInt = HBA_PORTSPEED_25GE;
			break;
		case LPFC_ASYNC_LINK_SPEED_40GBPS:
			ae->un.AttrInt = HBA_PORTSPEED_40GE;
			break;
		case LPFC_ASYNC_LINK_SPEED_100GBPS:
			ae->un.AttrInt = HBA_PORTSPEED_100GE;
			break;
		}
	}
	ae->un.AttrInt = cpu_to_be32(ae->un.AttrInt);
	size = FOURBYTES + sizeof(uint32_t);
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RPRT_SUPPORTED_SPEED);
	return size;
}

static int
lpfc_fdmi_port_attr_speed(struct lpfc_vport *vport,
			  struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_hba   *phba = vport->phba;
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t size;

	ae = &ad->AttrValue;

	if (!(phba->hba_flag & HBA_FCOE_MODE)) {
		switch (phba->fc_linkspeed) {
		case LPFC_LINK_SPEED_1GHZ:
			ae->un.AttrInt = HBA_PORTSPEED_1GFC;
			break;
		case LPFC_LINK_SPEED_2GHZ:
			ae->un.AttrInt = HBA_PORTSPEED_2GFC;
			break;
		case LPFC_LINK_SPEED_4GHZ:
			ae->un.AttrInt = HBA_PORTSPEED_4GFC;
			break;
		case LPFC_LINK_SPEED_8GHZ:
			ae->un.AttrInt = HBA_PORTSPEED_8GFC;
			break;
		case LPFC_LINK_SPEED_10GHZ:
			ae->un.AttrInt = HBA_PORTSPEED_10GFC;
			break;
		case LPFC_LINK_SPEED_16GHZ:
			ae->un.AttrInt = HBA_PORTSPEED_16GFC;
			break;
		case LPFC_LINK_SPEED_32GHZ:
			ae->un.AttrInt = HBA_PORTSPEED_32GFC;
			break;
		case LPFC_LINK_SPEED_64GHZ:
			ae->un.AttrInt = HBA_PORTSPEED_64GFC;
			break;
		case LPFC_LINK_SPEED_128GHZ:
			ae->un.AttrInt = HBA_PORTSPEED_128GFC;
			break;
		case LPFC_LINK_SPEED_256GHZ:
			ae->un.AttrInt = HBA_PORTSPEED_256GFC;
			break;
		default:
			ae->un.AttrInt = HBA_PORTSPEED_UNKNOWN;
			break;
		}
	} else {
		switch (phba->fc_linkspeed) {
		case LPFC_ASYNC_LINK_SPEED_10GBPS:
			ae->un.AttrInt = HBA_PORTSPEED_10GE;
			break;
		case LPFC_ASYNC_LINK_SPEED_25GBPS:
			ae->un.AttrInt = HBA_PORTSPEED_25GE;
			break;
		case LPFC_ASYNC_LINK_SPEED_40GBPS:
			ae->un.AttrInt = HBA_PORTSPEED_40GE;
			break;
		case LPFC_ASYNC_LINK_SPEED_100GBPS:
			ae->un.AttrInt = HBA_PORTSPEED_100GE;
			break;
		default:
			ae->un.AttrInt = HBA_PORTSPEED_UNKNOWN;
			break;
		}
	}

	ae->un.AttrInt = cpu_to_be32(ae->un.AttrInt);
	size = FOURBYTES + sizeof(uint32_t);
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RPRT_PORT_SPEED);
	return size;
}

static int
lpfc_fdmi_port_attr_max_frame(struct lpfc_vport *vport,
			      struct lpfc_fdmi_attr_def *ad)
{
	struct serv_parm *hsp;
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t size;

	ae = &ad->AttrValue;

	hsp = (struct serv_parm *)&vport->fc_sparam;
	ae->un.AttrInt = (((uint32_t) hsp->cmn.bbRcvSizeMsb & 0x0F) << 8) |
			  (uint32_t) hsp->cmn.bbRcvSizeLsb;
	ae->un.AttrInt = cpu_to_be32(ae->un.AttrInt);
	size = FOURBYTES + sizeof(uint32_t);
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RPRT_MAX_FRAME_SIZE);
	return size;
}

static int
lpfc_fdmi_port_attr_os_devname(struct lpfc_vport *vport,
			       struct lpfc_fdmi_attr_def *ad)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t len, size;

	ae = &ad->AttrValue;
	memset(ae, 0, sizeof(*ae));

	snprintf(ae->un.AttrString, sizeof(ae->un.AttrString),
		 "/sys/class/scsi_host/host%d", shost->host_no);
	len = strnlen((char *)ae->un.AttrString,
			  sizeof(ae->un.AttrString));
	len += (len & 3) ? (4 - (len & 3)) : 4;
	size = FOURBYTES + len;
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RPRT_OS_DEVICE_NAME);
	return size;
}

static int
lpfc_fdmi_port_attr_host_name(struct lpfc_vport *vport,
			      struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t len, size;

	ae = &ad->AttrValue;
	memset(ae, 0, sizeof(*ae));

	scnprintf(ae->un.AttrString, sizeof(ae->un.AttrString), "%s",
		  vport->phba->os_host_name);

	len = strnlen(ae->un.AttrString, sizeof(ae->un.AttrString));
	len += (len & 3) ? (4 - (len & 3)) : 4;
	size = FOURBYTES + len;
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RPRT_HOST_NAME);
	return size;
}

static int
lpfc_fdmi_port_attr_wwnn(struct lpfc_vport *vport,
			 struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t size;

	ae = &ad->AttrValue;
	memset(ae, 0, sizeof(*ae));

	memcpy(&ae->un.AttrWWN, &vport->fc_sparam.nodeName,
	       sizeof(struct lpfc_name));
	size = FOURBYTES + sizeof(struct lpfc_name);
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RPRT_NODENAME);
	return size;
}

static int
lpfc_fdmi_port_attr_wwpn(struct lpfc_vport *vport,
			 struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t size;

	ae = &ad->AttrValue;
	memset(ae, 0, sizeof(*ae));

	memcpy(&ae->un.AttrWWN, &vport->fc_sparam.portName,
	       sizeof(struct lpfc_name));
	size = FOURBYTES + sizeof(struct lpfc_name);
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RPRT_PORTNAME);
	return size;
}

static int
lpfc_fdmi_port_attr_symbolic_name(struct lpfc_vport *vport,
				  struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t len, size;

	ae = &ad->AttrValue;
	memset(ae, 0, sizeof(*ae));

	len = lpfc_vport_symbolic_port_name(vport, ae->un.AttrString, 256);
	len += (len & 3) ? (4 - (len & 3)) : 4;
	size = FOURBYTES + len;
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RPRT_SYM_PORTNAME);
	return size;
}

static int
lpfc_fdmi_port_attr_port_type(struct lpfc_vport *vport,
			      struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t size;

	ae = &ad->AttrValue;
	if (phba->fc_topology == LPFC_TOPOLOGY_LOOP)
		ae->un.AttrInt =  cpu_to_be32(LPFC_FDMI_PORTTYPE_NLPORT);
	else
		ae->un.AttrInt =  cpu_to_be32(LPFC_FDMI_PORTTYPE_NPORT);
	size = FOURBYTES + sizeof(uint32_t);
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RPRT_PORT_TYPE);
	return size;
}

static int
lpfc_fdmi_port_attr_class(struct lpfc_vport *vport,
			  struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t size;

	ae = &ad->AttrValue;
	ae->un.AttrInt = cpu_to_be32(FC_COS_CLASS2 | FC_COS_CLASS3);
	size = FOURBYTES + sizeof(uint32_t);
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RPRT_SUPPORTED_CLASS);
	return size;
}

static int
lpfc_fdmi_port_attr_fabric_wwpn(struct lpfc_vport *vport,
				struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t size;

	ae = &ad->AttrValue;
	memset(ae, 0, sizeof(*ae));

	memcpy(&ae->un.AttrWWN, &vport->fabric_portname,
	       sizeof(struct lpfc_name));
	size = FOURBYTES + sizeof(struct lpfc_name);
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RPRT_FABRICNAME);
	return size;
}

static int
lpfc_fdmi_port_attr_active_fc4type(struct lpfc_vport *vport,
				   struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t size;

	ae = &ad->AttrValue;
	memset(ae, 0, sizeof(*ae));

	ae->un.AttrTypes[2] = 0x01; /* Type 0x8 - FCP */
	ae->un.AttrTypes[7] = 0x01; /* Type 0x20 - CT */

	/* Check to see if NVME is configured or not */
	if (vport == phba->pport &&
	    phba->cfg_enable_fc4_type & LPFC_ENABLE_NVME)
		ae->un.AttrTypes[6] = 0x1; /* Type 0x28 - NVME */

	size = FOURBYTES + 32;
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RPRT_ACTIVE_FC4_TYPES);
	return size;
}

static int
lpfc_fdmi_port_attr_port_state(struct lpfc_vport *vport,
			       struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t size;

	ae = &ad->AttrValue;
	/* Link Up - operational */
	ae->un.AttrInt =  cpu_to_be32(LPFC_FDMI_PORTSTATE_ONLINE);
	size = FOURBYTES + sizeof(uint32_t);
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RPRT_PORT_STATE);
	return size;
}

static int
lpfc_fdmi_port_attr_num_disc(struct lpfc_vport *vport,
			     struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t size;

	ae = &ad->AttrValue;
	vport->fdmi_num_disc = lpfc_find_map_node(vport);
	ae->un.AttrInt = cpu_to_be32(vport->fdmi_num_disc);
	size = FOURBYTES + sizeof(uint32_t);
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RPRT_DISC_PORT);
	return size;
}

static int
lpfc_fdmi_port_attr_nportid(struct lpfc_vport *vport,
			    struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t size;

	ae = &ad->AttrValue;
	ae->un.AttrInt =  cpu_to_be32(vport->fc_myDID);
	size = FOURBYTES + sizeof(uint32_t);
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RPRT_PORT_ID);
	return size;
}

static int
lpfc_fdmi_smart_attr_service(struct lpfc_vport *vport,
			     struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t len, size;

	ae = &ad->AttrValue;
	memset(ae, 0, sizeof(*ae));

	strncpy(ae->un.AttrString, "Smart SAN Initiator",
		sizeof(ae->un.AttrString));
	len = strnlen(ae->un.AttrString,
			  sizeof(ae->un.AttrString));
	len += (len & 3) ? (4 - (len & 3)) : 4;
	size = FOURBYTES + len;
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RPRT_SMART_SERVICE);
	return size;
}

static int
lpfc_fdmi_smart_attr_guid(struct lpfc_vport *vport,
			  struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t size;

	ae = &ad->AttrValue;
	memset(ae, 0, sizeof(*ae));

	memcpy(&ae->un.AttrString, &vport->fc_sparam.nodeName,
	       sizeof(struct lpfc_name));
	memcpy((((uint8_t *)&ae->un.AttrString) +
		sizeof(struct lpfc_name)),
		&vport->fc_sparam.portName, sizeof(struct lpfc_name));
	size = FOURBYTES + (2 * sizeof(struct lpfc_name));
	ad->AttrLen =  cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RPRT_SMART_GUID);
	return size;
}

static int
lpfc_fdmi_smart_attr_version(struct lpfc_vport *vport,
			     struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t len, size;

	ae = &ad->AttrValue;
	memset(ae, 0, sizeof(*ae));

	strncpy(ae->un.AttrString, "Smart SAN Version 2.0",
		sizeof(ae->un.AttrString));
	len = strnlen(ae->un.AttrString,
			  sizeof(ae->un.AttrString));
	len += (len & 3) ? (4 - (len & 3)) : 4;
	size = FOURBYTES + len;
	ad->AttrLen =  cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RPRT_SMART_VERSION);
	return size;
}

static int
lpfc_fdmi_smart_attr_model(struct lpfc_vport *vport,
			   struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t len, size;

	ae = &ad->AttrValue;
	memset(ae, 0, sizeof(*ae));

	strncpy(ae->un.AttrString, phba->ModelName,
		sizeof(ae->un.AttrString));
	len = strnlen(ae->un.AttrString, sizeof(ae->un.AttrString));
	len += (len & 3) ? (4 - (len & 3)) : 4;
	size = FOURBYTES + len;
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RPRT_SMART_MODEL);
	return size;
}

static int
lpfc_fdmi_smart_attr_port_info(struct lpfc_vport *vport,
			       struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t size;

	ae = &ad->AttrValue;

	/* SRIOV (type 3) is not supported */
	if (vport->vpi)
		ae->un.AttrInt =  cpu_to_be32(2);  /* NPIV */
	else
		ae->un.AttrInt =  cpu_to_be32(1);  /* Physical */
	size = FOURBYTES + sizeof(uint32_t);
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RPRT_SMART_PORT_INFO);
	return size;
}

static int
lpfc_fdmi_smart_attr_qos(struct lpfc_vport *vport,
			 struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t size;

	ae = &ad->AttrValue;
	ae->un.AttrInt =  cpu_to_be32(0);
	size = FOURBYTES + sizeof(uint32_t);
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RPRT_SMART_QOS);
	return size;
}

static int
lpfc_fdmi_smart_attr_security(struct lpfc_vport *vport,
			      struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t size;

	ae = &ad->AttrValue;
	ae->un.AttrInt =  cpu_to_be32(1);
	size = FOURBYTES + sizeof(uint32_t);
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RPRT_SMART_SECURITY);
	return size;
}

static int
lpfc_fdmi_vendor_attr_mi(struct lpfc_vport *vport,
			  struct lpfc_fdmi_attr_def *ad)
{
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_fdmi_attr_entry *ae;
	uint32_t len, size;
	char mibrevision[16];

	ae = (struct lpfc_fdmi_attr_entry *)&ad->AttrValue;
	memset(ae, 0, 256);
	sprintf(mibrevision, "ELXE2EM:%04d",
		phba->sli4_hba.pc_sli4_params.mi_ver);
	strncpy(ae->un.AttrString, &mibrevision[0], sizeof(ae->un.AttrString));
	len = strnlen(ae->un.AttrString, sizeof(ae->un.AttrString));
	len += (len & 3) ? (4 - (len & 3)) : 4;
	size = FOURBYTES + len;
	ad->AttrLen = cpu_to_be16(size);
	ad->AttrType = cpu_to_be16(RPRT_VENDOR_MI);
	return size;
}

/* RHBA attribute jump table */
int (*lpfc_fdmi_hba_action[])
	(struct lpfc_vport *vport, struct lpfc_fdmi_attr_def *ad) = {
	/* Action routine                 Mask bit     Attribute type */
	lpfc_fdmi_hba_attr_wwnn,	  /* bit0     RHBA_NODENAME           */
	lpfc_fdmi_hba_attr_manufacturer,  /* bit1     RHBA_MANUFACTURER       */
	lpfc_fdmi_hba_attr_sn,		  /* bit2     RHBA_SERIAL_NUMBER      */
	lpfc_fdmi_hba_attr_model,	  /* bit3     RHBA_MODEL              */
	lpfc_fdmi_hba_attr_description,	  /* bit4     RHBA_MODEL_DESCRIPTION  */
	lpfc_fdmi_hba_attr_hdw_ver,	  /* bit5     RHBA_HARDWARE_VERSION   */
	lpfc_fdmi_hba_attr_drvr_ver,	  /* bit6     RHBA_DRIVER_VERSION     */
	lpfc_fdmi_hba_attr_rom_ver,	  /* bit7     RHBA_OPTION_ROM_VERSION */
	lpfc_fdmi_hba_attr_fmw_ver,	  /* bit8     RHBA_FIRMWARE_VERSION   */
	lpfc_fdmi_hba_attr_os_ver,	  /* bit9     RHBA_OS_NAME_VERSION    */
	lpfc_fdmi_hba_attr_ct_len,	  /* bit10    RHBA_MAX_CT_PAYLOAD_LEN */
	lpfc_fdmi_hba_attr_symbolic_name, /* bit11    RHBA_SYM_NODENAME       */
	lpfc_fdmi_hba_attr_vendor_info,	  /* bit12    RHBA_VENDOR_INFO        */
	lpfc_fdmi_hba_attr_num_ports,	  /* bit13    RHBA_NUM_PORTS          */
	lpfc_fdmi_hba_attr_fabric_wwnn,	  /* bit14    RHBA_FABRIC_WWNN        */
	lpfc_fdmi_hba_attr_bios_ver,	  /* bit15    RHBA_BIOS_VERSION       */
	lpfc_fdmi_hba_attr_bios_state,	  /* bit16    RHBA_BIOS_STATE         */
	lpfc_fdmi_hba_attr_vendor_id,	  /* bit17    RHBA_VENDOR_ID          */
};

/* RPA / RPRT attribute jump table */
int (*lpfc_fdmi_port_action[])
	(struct lpfc_vport *vport, struct lpfc_fdmi_attr_def *ad) = {
	/* Action routine                   Mask bit   Attribute type */
	lpfc_fdmi_port_attr_fc4type,        /* bit0   RPRT_SUPPORT_FC4_TYPES  */
	lpfc_fdmi_port_attr_support_speed,  /* bit1   RPRT_SUPPORTED_SPEED    */
	lpfc_fdmi_port_attr_speed,          /* bit2   RPRT_PORT_SPEED         */
	lpfc_fdmi_port_attr_max_frame,      /* bit3   RPRT_MAX_FRAME_SIZE     */
	lpfc_fdmi_port_attr_os_devname,     /* bit4   RPRT_OS_DEVICE_NAME     */
	lpfc_fdmi_port_attr_host_name,      /* bit5   RPRT_HOST_NAME          */
	lpfc_fdmi_port_attr_wwnn,           /* bit6   RPRT_NODENAME           */
	lpfc_fdmi_port_attr_wwpn,           /* bit7   RPRT_PORTNAME           */
	lpfc_fdmi_port_attr_symbolic_name,  /* bit8   RPRT_SYM_PORTNAME       */
	lpfc_fdmi_port_attr_port_type,      /* bit9   RPRT_PORT_TYPE          */
	lpfc_fdmi_port_attr_class,          /* bit10  RPRT_SUPPORTED_CLASS    */
	lpfc_fdmi_port_attr_fabric_wwpn,    /* bit11  RPRT_FABRICNAME         */
	lpfc_fdmi_port_attr_active_fc4type, /* bit12  RPRT_ACTIVE_FC4_TYPES   */
	lpfc_fdmi_port_attr_port_state,     /* bit13  RPRT_PORT_STATE         */
	lpfc_fdmi_port_attr_num_disc,       /* bit14  RPRT_DISC_PORT          */
	lpfc_fdmi_port_attr_nportid,        /* bit15  RPRT_PORT_ID            */
	lpfc_fdmi_smart_attr_service,       /* bit16  RPRT_SMART_SERVICE      */
	lpfc_fdmi_smart_attr_guid,          /* bit17  RPRT_SMART_GUID         */
	lpfc_fdmi_smart_attr_version,       /* bit18  RPRT_SMART_VERSION      */
	lpfc_fdmi_smart_attr_model,         /* bit19  RPRT_SMART_MODEL        */
	lpfc_fdmi_smart_attr_port_info,     /* bit20  RPRT_SMART_PORT_INFO    */
	lpfc_fdmi_smart_attr_qos,           /* bit21  RPRT_SMART_QOS          */
	lpfc_fdmi_smart_attr_security,      /* bit22  RPRT_SMART_SECURITY     */
	lpfc_fdmi_vendor_attr_mi,           /* bit23  RPRT_VENDOR_MI          */
};

/**
 * lpfc_fdmi_cmd - Build and send a FDMI cmd to the specified NPort
 * @vport: pointer to a host virtual N_Port data structure.
 * @ndlp: ndlp to send FDMI cmd to (if NULL use FDMI_DID)
 * @cmdcode: FDMI command to send
 * @new_mask: Mask of HBA or PORT Attributes to send
 *
 * Builds and sends a FDMI command using the CT subsystem.
 */
int
lpfc_fdmi_cmd(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
	      int cmdcode, uint32_t new_mask)
{
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_dmabuf *mp, *bmp;
	struct lpfc_sli_ct_request *CtReq;
	struct ulp_bde64 *bpl;
	uint32_t bit_pos;
	uint32_t size;
	uint32_t rsp_size;
	uint32_t mask;
	struct lpfc_fdmi_reg_hba *rh;
	struct lpfc_fdmi_port_entry *pe;
	struct lpfc_fdmi_reg_portattr *pab = NULL;
	struct lpfc_fdmi_attr_block *ab = NULL;
	int  (*func)(struct lpfc_vport *vport, struct lpfc_fdmi_attr_def *ad);
	void (*cmpl)(struct lpfc_hba *, struct lpfc_iocbq *,
		     struct lpfc_iocbq *);

	if (!ndlp)
		return 0;

	cmpl = lpfc_cmpl_ct_disc_fdmi; /* called from discovery */

	/* fill in BDEs for command */
	/* Allocate buffer for command payload */
	mp = kmalloc(sizeof(struct lpfc_dmabuf), GFP_KERNEL);
	if (!mp)
		goto fdmi_cmd_exit;

	mp->virt = lpfc_mbuf_alloc(phba, 0, &(mp->phys));
	if (!mp->virt)
		goto fdmi_cmd_free_mp;

	/* Allocate buffer for Buffer ptr list */
	bmp = kmalloc(sizeof(struct lpfc_dmabuf), GFP_KERNEL);
	if (!bmp)
		goto fdmi_cmd_free_mpvirt;

	bmp->virt = lpfc_mbuf_alloc(phba, 0, &(bmp->phys));
	if (!bmp->virt)
		goto fdmi_cmd_free_bmp;

	INIT_LIST_HEAD(&mp->list);
	INIT_LIST_HEAD(&bmp->list);

	/* FDMI request */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
			 "0218 FDMI Request x%x mask x%x Data: x%x x%x x%x\n",
			 cmdcode, new_mask, vport->fdmi_port_mask,
			 vport->fc_flag, vport->port_state);

	CtReq = (struct lpfc_sli_ct_request *)mp->virt;

	/* First populate the CT_IU preamble */
	memset(CtReq, 0, sizeof(struct lpfc_sli_ct_request));
	CtReq->RevisionId.bits.Revision = SLI_CT_REVISION;
	CtReq->RevisionId.bits.InId = 0;

	CtReq->FsType = SLI_CT_MANAGEMENT_SERVICE;
	CtReq->FsSubType = SLI_CT_FDMI_Subtypes;

	CtReq->CommandResponse.bits.CmdRsp = cpu_to_be16(cmdcode);
	rsp_size = LPFC_BPL_SIZE;
	size = 0;

	/* Next fill in the specific FDMI cmd information */
	switch (cmdcode) {
	case SLI_MGMT_RHAT:
	case SLI_MGMT_RHBA:
		rh = (struct lpfc_fdmi_reg_hba *)&CtReq->un.PortID;
		/* HBA Identifier */
		memcpy(&rh->hi.PortName, &phba->pport->fc_sparam.portName,
		       sizeof(struct lpfc_name));

		if (cmdcode == SLI_MGMT_RHBA) {
			/* Registered Port List */
			/* One entry (port) per adapter */
			rh->rpl.EntryCnt = cpu_to_be32(1);
			memcpy(&rh->rpl.pe.PortName,
			       &phba->pport->fc_sparam.portName,
			       sizeof(struct lpfc_name));

			/* point to the HBA attribute block */
			size = 2 * sizeof(struct lpfc_name) +
				FOURBYTES;
		} else {
			size = sizeof(struct lpfc_name);
		}
		ab = (struct lpfc_fdmi_attr_block *)((uint8_t *)rh + size);
		ab->EntryCnt = 0;
		size += FOURBYTES;
		bit_pos = 0;
		if (new_mask)
			mask = new_mask;
		else
			mask = vport->fdmi_hba_mask;

		/* Mask will dictate what attributes to build in the request */
		while (mask) {
			if (mask & 0x1) {
				func = lpfc_fdmi_hba_action[bit_pos];
				size += func(vport,
					     (struct lpfc_fdmi_attr_def *)
					     ((uint8_t *)rh + size));
				ab->EntryCnt++;
				if ((size + 256) >
				    (LPFC_BPL_SIZE - LPFC_CT_PREAMBLE))
					goto hba_out;
			}
			mask = mask >> 1;
			bit_pos++;
		}
hba_out:
		ab->EntryCnt = cpu_to_be32(ab->EntryCnt);
		/* Total size */
		size = GID_REQUEST_SZ - 4 + size;
		break;

	case SLI_MGMT_RPRT:
		if (vport->port_type != LPFC_PHYSICAL_PORT) {
			ndlp = lpfc_findnode_did(phba->pport, FDMI_DID);
			if (!ndlp)
				return 0;
		}
		fallthrough;
	case SLI_MGMT_RPA:
		pab = (struct lpfc_fdmi_reg_portattr *)&CtReq->un.PortID;
		if (cmdcode == SLI_MGMT_RPRT) {
			rh = (struct lpfc_fdmi_reg_hba *)pab;
			/* HBA Identifier */
			memcpy(&rh->hi.PortName,
			       &phba->pport->fc_sparam.portName,
			       sizeof(struct lpfc_name));
			pab = (struct lpfc_fdmi_reg_portattr *)
				((uint8_t *)pab +  sizeof(struct lpfc_name));
		}

		memcpy((uint8_t *)&pab->PortName,
		       (uint8_t *)&vport->fc_sparam.portName,
		       sizeof(struct lpfc_name));
		size += sizeof(struct lpfc_name) + FOURBYTES;
		pab->ab.EntryCnt = 0;
		bit_pos = 0;
		if (new_mask)
			mask = new_mask;
		else
			mask = vport->fdmi_port_mask;

		/* Mask will dictate what attributes to build in the request */
		while (mask) {
			if (mask & 0x1) {
				func = lpfc_fdmi_port_action[bit_pos];
				size += func(vport,
					     (struct lpfc_fdmi_attr_def *)
					     ((uint8_t *)pab + size));
				pab->ab.EntryCnt++;
				if ((size + 256) >
				    (LPFC_BPL_SIZE - LPFC_CT_PREAMBLE))
					goto port_out;
			}
			mask = mask >> 1;
			bit_pos++;
		}
port_out:
		pab->ab.EntryCnt = cpu_to_be32(pab->ab.EntryCnt);
		/* Total size */
		if (cmdcode == SLI_MGMT_RPRT)
			size += sizeof(struct lpfc_name);
		size = GID_REQUEST_SZ - 4 + size;
		break;

	case SLI_MGMT_GHAT:
	case SLI_MGMT_GRPL:
		rsp_size = FC_MAX_NS_RSP;
		fallthrough;
	case SLI_MGMT_DHBA:
	case SLI_MGMT_DHAT:
		pe = (struct lpfc_fdmi_port_entry *)&CtReq->un.PortID;
		memcpy((uint8_t *)&pe->PortName,
		       (uint8_t *)&vport->fc_sparam.portName,
		       sizeof(struct lpfc_name));
		size = GID_REQUEST_SZ - 4 + sizeof(struct lpfc_name);
		break;

	case SLI_MGMT_GPAT:
	case SLI_MGMT_GPAS:
		rsp_size = FC_MAX_NS_RSP;
		fallthrough;
	case SLI_MGMT_DPRT:
		if (vport->port_type != LPFC_PHYSICAL_PORT) {
			ndlp = lpfc_findnode_did(phba->pport, FDMI_DID);
			if (!ndlp)
				return 0;
		}
		fallthrough;
	case SLI_MGMT_DPA:
		pe = (struct lpfc_fdmi_port_entry *)&CtReq->un.PortID;
		memcpy((uint8_t *)&pe->PortName,
		       (uint8_t *)&vport->fc_sparam.portName,
		       sizeof(struct lpfc_name));
		size = GID_REQUEST_SZ - 4 + sizeof(struct lpfc_name);
		break;
	case SLI_MGMT_GRHL:
		size = GID_REQUEST_SZ - 4;
		break;
	default:
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_DISCOVERY,
				 "0298 FDMI cmdcode x%x not supported\n",
				 cmdcode);
		goto fdmi_cmd_free_bmpvirt;
	}
	CtReq->CommandResponse.bits.Size = cpu_to_be16(rsp_size);

	bpl = (struct ulp_bde64 *)bmp->virt;
	bpl->addrHigh = le32_to_cpu(putPaddrHigh(mp->phys));
	bpl->addrLow = le32_to_cpu(putPaddrLow(mp->phys));
	bpl->tus.f.bdeFlags = 0;
	bpl->tus.f.bdeSize = size;

	/*
	 * The lpfc_ct_cmd/lpfc_get_req shall increment ndlp reference count
	 * to hold ndlp reference for the corresponding callback function.
	 */
	if (!lpfc_ct_cmd(vport, mp, bmp, ndlp, cmpl, rsp_size, 0))
		return 0;

fdmi_cmd_free_bmpvirt:
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

/**
 * lpfc_delayed_disc_tmo - Timeout handler for delayed discovery timer.
 * @t: Context object of the timer.
 *
 * This function set the WORKER_DELAYED_DISC_TMO flag and wake up
 * the worker thread.
 **/
void
lpfc_delayed_disc_tmo(struct timer_list *t)
{
	struct lpfc_vport *vport = from_timer(vport, t, delayed_disc_tmo);
	struct lpfc_hba   *phba = vport->phba;
	uint32_t tmo_posted;
	unsigned long iflag;

	spin_lock_irqsave(&vport->work_port_lock, iflag);
	tmo_posted = vport->work_port_events & WORKER_DELAYED_DISC_TMO;
	if (!tmo_posted)
		vport->work_port_events |= WORKER_DELAYED_DISC_TMO;
	spin_unlock_irqrestore(&vport->work_port_lock, iflag);

	if (!tmo_posted)
		lpfc_worker_wake_up(phba);
	return;
}

/**
 * lpfc_delayed_disc_timeout_handler - Function called by worker thread to
 *      handle delayed discovery.
 * @vport: pointer to a host virtual N_Port data structure.
 *
 * This function start nport discovery of the vport.
 **/
void
lpfc_delayed_disc_timeout_handler(struct lpfc_vport *vport)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);

	spin_lock_irq(shost->host_lock);
	if (!(vport->fc_flag & FC_DISC_DELAYED)) {
		spin_unlock_irq(shost->host_lock);
		return;
	}
	vport->fc_flag &= ~FC_DISC_DELAYED;
	spin_unlock_irq(shost->host_lock);

	lpfc_do_scr_ns_plogi(vport->phba, vport);
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

	if (phba->sli_rev == LPFC_SLI_REV4)
		snprintf(fwrevision, FW_REV_STR_SIZE, "%s", vp->rev.opFwName);
	else if (vp->rev.rBit) {
		if (psli->sli_flag & LPFC_SLI_ACTIVE)
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
		case 3:
			c = 'X';
			break;
		default:
			c = 0;
			break;
		}
		b4 = (rev & 0x0000000f);

		if (psli->sli_flag & LPFC_SLI_ACTIVE)
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

		sprintf(fwrevision, "%d.%d%d%c%d", b1, b2, b3, c, b4);
	}
	return;
}

static void
lpfc_cmpl_ct_cmd_vmid(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
		      struct lpfc_iocbq *rspiocb)
{
	struct lpfc_vport *vport = cmdiocb->vport;
	struct lpfc_dmabuf *inp = cmdiocb->cmd_dmabuf;
	struct lpfc_dmabuf *outp = cmdiocb->rsp_dmabuf;
	struct lpfc_sli_ct_request *ctcmd = inp->virt;
	struct lpfc_sli_ct_request *ctrsp = outp->virt;
	u16 rsp = ctrsp->CommandResponse.bits.CmdRsp;
	struct app_id_object *app;
	u32 cmd, hash, bucket;
	struct lpfc_vmid *vmp, *cur;
	u8 *data = outp->virt;
	int i;

	cmd = be16_to_cpu(ctcmd->CommandResponse.bits.CmdRsp);
	if (cmd == SLI_CTAS_DALLAPP_ID)
		lpfc_ct_free_iocb(phba, cmdiocb);

	if (lpfc_els_chk_latt(vport) || get_job_ulpstatus(phba, rspiocb)) {
		if (cmd != SLI_CTAS_DALLAPP_ID)
			return;
	}
	/* Check for a CT LS_RJT response */
	if (rsp == be16_to_cpu(SLI_CT_RESPONSE_FS_RJT)) {
		if (cmd != SLI_CTAS_DALLAPP_ID)
			lpfc_printf_vlog(vport, KERN_DEBUG, LOG_DISCOVERY,
					 "3306 VMID FS_RJT Data: x%x x%x x%x\n",
					 cmd, ctrsp->ReasonCode,
					 ctrsp->Explanation);
		if ((cmd != SLI_CTAS_DALLAPP_ID) ||
		    (ctrsp->ReasonCode != SLI_CT_UNABLE_TO_PERFORM_REQ) ||
		    (ctrsp->Explanation != SLI_CT_APP_ID_NOT_AVAILABLE)) {
			/* If DALLAPP_ID failed retry later */
			if (cmd == SLI_CTAS_DALLAPP_ID)
				vport->load_flag |= FC_DEREGISTER_ALL_APP_ID;
			return;
		}
	}

	switch (cmd) {
	case SLI_CTAS_RAPP_IDENT:
		app = (struct app_id_object *)(RAPP_IDENT_OFFSET + data);
		lpfc_printf_vlog(vport, KERN_DEBUG, LOG_DISCOVERY,
				 "6712 RAPP_IDENT app id %d  port id x%x id "
				 "len %d\n", be32_to_cpu(app->app_id),
				 be32_to_cpu(app->port_id),
				 app->obj.entity_id_len);

		if (app->obj.entity_id_len == 0 || app->port_id == 0)
			return;

		hash = lpfc_vmid_hash_fn(app->obj.entity_id,
					 app->obj.entity_id_len);
		vmp = lpfc_get_vmid_from_hashtable(vport, hash,
						  app->obj.entity_id);
		if (vmp) {
			write_lock(&vport->vmid_lock);
			vmp->un.app_id = be32_to_cpu(app->app_id);
			vmp->flag |= LPFC_VMID_REGISTERED;
			vmp->flag &= ~LPFC_VMID_REQ_REGISTER;
			write_unlock(&vport->vmid_lock);
			/* Set IN USE flag */
			vport->vmid_flag |= LPFC_VMID_IN_USE;
		} else {
			lpfc_printf_vlog(vport, KERN_DEBUG, LOG_DISCOVERY,
					 "6901 No entry found %s hash %d\n",
					 app->obj.entity_id, hash);
		}
		break;
	case SLI_CTAS_DAPP_IDENT:
		app = (struct app_id_object *)(DAPP_IDENT_OFFSET + data);
		lpfc_printf_vlog(vport, KERN_DEBUG, LOG_DISCOVERY,
				 "6713 DAPP_IDENT app id %d  port id x%x\n",
				 be32_to_cpu(app->app_id),
				 be32_to_cpu(app->port_id));
		break;
	case SLI_CTAS_DALLAPP_ID:
		lpfc_printf_vlog(vport, KERN_DEBUG, LOG_DISCOVERY,
				 "8856 Deregistered all app ids\n");
		read_lock(&vport->vmid_lock);
		for (i = 0; i < phba->cfg_max_vmid; i++) {
			vmp = &vport->vmid[i];
			if (vmp->flag != LPFC_VMID_SLOT_FREE)
				memset(vmp, 0, sizeof(struct lpfc_vmid));
		}
		read_unlock(&vport->vmid_lock);
		/* for all elements in the hash table */
		if (!hash_empty(vport->hash_table))
			hash_for_each(vport->hash_table, bucket, cur, hnode)
				hash_del(&cur->hnode);
		vport->load_flag |= FC_ALLOW_VMID;
		break;
	default:
		lpfc_printf_vlog(vport, KERN_DEBUG, LOG_DISCOVERY,
				 "8857 Invalid command code\n");
	}
}

/**
 * lpfc_vmid_cmd - Build and send a FDMI cmd to the specified NPort
 * @vport: pointer to a host virtual N_Port data structure.
 * @cmdcode: application server command code to send
 * @vmid: pointer to vmid info structure
 *
 * Builds and sends a FDMI command using the CT subsystem.
 */
int
lpfc_vmid_cmd(struct lpfc_vport *vport,
	      int cmdcode, struct lpfc_vmid *vmid)
{
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_dmabuf *mp, *bmp;
	struct lpfc_sli_ct_request *ctreq;
	struct ulp_bde64 *bpl;
	u32 size;
	u32 rsp_size;
	u8 *data;
	struct lpfc_vmid_rapp_ident_list *rap;
	struct lpfc_vmid_dapp_ident_list *dap;
	u8 retry = 0;
	struct lpfc_nodelist *ndlp;

	void (*cmpl)(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
		     struct lpfc_iocbq *rspiocb);

	ndlp = lpfc_findnode_did(vport, FDMI_DID);
	if (!ndlp || ndlp->nlp_state != NLP_STE_UNMAPPED_NODE)
		return 0;

	cmpl = lpfc_cmpl_ct_cmd_vmid;

	/* fill in BDEs for command */
	/* Allocate buffer for command payload */
	mp = kmalloc(sizeof(*mp), GFP_KERNEL);
	if (!mp)
		goto vmid_free_mp_exit;

	mp->virt = lpfc_mbuf_alloc(phba, 0, &mp->phys);
	if (!mp->virt)
		goto vmid_free_mp_virt_exit;

	/* Allocate buffer for Buffer ptr list */
	bmp = kmalloc(sizeof(*bmp), GFP_KERNEL);
	if (!bmp)
		goto vmid_free_bmp_exit;

	bmp->virt = lpfc_mbuf_alloc(phba, 0, &bmp->phys);
	if (!bmp->virt)
		goto vmid_free_bmp_virt_exit;

	INIT_LIST_HEAD(&mp->list);
	INIT_LIST_HEAD(&bmp->list);

	lpfc_printf_vlog(vport, KERN_INFO, LOG_DISCOVERY,
			 "3275 VMID Request Data: x%x x%x x%x\n",
			 vport->fc_flag, vport->port_state, cmdcode);
	ctreq = (struct lpfc_sli_ct_request *)mp->virt;
	data = mp->virt;
	/* First populate the CT_IU preamble */
	memset(data, 0, LPFC_BPL_SIZE);
	ctreq->RevisionId.bits.Revision = SLI_CT_REVISION;
	ctreq->RevisionId.bits.InId = 0;

	ctreq->FsType = SLI_CT_MANAGEMENT_SERVICE;
	ctreq->FsSubType = SLI_CT_APP_SEV_Subtypes;

	ctreq->CommandResponse.bits.CmdRsp = cpu_to_be16(cmdcode);
	rsp_size = LPFC_BPL_SIZE;
	size = 0;

	switch (cmdcode) {
	case SLI_CTAS_RAPP_IDENT:
		lpfc_printf_vlog(vport, KERN_DEBUG, LOG_DISCOVERY,
				 "1329 RAPP_IDENT for %s\n", vmid->host_vmid);
		ctreq->un.PortID = cpu_to_be32(vport->fc_myDID);
		rap = (struct lpfc_vmid_rapp_ident_list *)
			(DAPP_IDENT_OFFSET + data);
		rap->no_of_objects = cpu_to_be32(1);
		rap->obj[0].entity_id_len = vmid->vmid_len;
		memcpy(rap->obj[0].entity_id, vmid->host_vmid, vmid->vmid_len);
		size = RAPP_IDENT_OFFSET +
			sizeof(struct lpfc_vmid_rapp_ident_list);
		retry = 1;
		break;

	case SLI_CTAS_GALLAPPIA_ID:
		ctreq->un.PortID = cpu_to_be32(vport->fc_myDID);
		size = GALLAPPIA_ID_SIZE;
		break;

	case SLI_CTAS_DAPP_IDENT:
		lpfc_printf_vlog(vport, KERN_DEBUG, LOG_DISCOVERY,
				 "1469 DAPP_IDENT for %s\n", vmid->host_vmid);
		ctreq->un.PortID = cpu_to_be32(vport->fc_myDID);
		dap = (struct lpfc_vmid_dapp_ident_list *)
			(DAPP_IDENT_OFFSET + data);
		dap->no_of_objects = cpu_to_be32(1);
		dap->obj[0].entity_id_len = vmid->vmid_len;
		memcpy(dap->obj[0].entity_id, vmid->host_vmid, vmid->vmid_len);
		size = DAPP_IDENT_OFFSET +
			sizeof(struct lpfc_vmid_dapp_ident_list);
		write_lock(&vport->vmid_lock);
		vmid->flag &= ~LPFC_VMID_REGISTERED;
		write_unlock(&vport->vmid_lock);
		retry = 1;
		break;

	case SLI_CTAS_DALLAPP_ID:
		ctreq->un.PortID = cpu_to_be32(vport->fc_myDID);
		size = DALLAPP_ID_SIZE;
		break;

	default:
		lpfc_printf_vlog(vport, KERN_DEBUG, LOG_DISCOVERY,
				 "7062 VMID cmdcode x%x not supported\n",
				 cmdcode);
		goto vmid_free_all_mem;
	}

	ctreq->CommandResponse.bits.Size = cpu_to_be16(rsp_size);

	bpl = (struct ulp_bde64 *)bmp->virt;
	bpl->addrHigh = putPaddrHigh(mp->phys);
	bpl->addrLow = putPaddrLow(mp->phys);
	bpl->tus.f.bdeFlags = 0;
	bpl->tus.f.bdeSize = size;

	/* The lpfc_ct_cmd/lpfc_get_req shall increment ndlp reference count
	 * to hold ndlp reference for the corresponding callback function.
	 */
	if (!lpfc_ct_cmd(vport, mp, bmp, ndlp, cmpl, rsp_size, retry))
		return 0;

 vmid_free_all_mem:
	lpfc_mbuf_free(phba, bmp->virt, bmp->phys);
 vmid_free_bmp_virt_exit:
	kfree(bmp);
 vmid_free_bmp_exit:
	lpfc_mbuf_free(phba, mp->virt, mp->phys);
 vmid_free_mp_virt_exit:
	kfree(mp);
 vmid_free_mp_exit:

	/* Issue CT request failed */
	lpfc_printf_vlog(vport, KERN_DEBUG, LOG_DISCOVERY,
			 "3276 VMID CT request failed Data: x%x\n", cmdcode);
	return -EIO;
}
