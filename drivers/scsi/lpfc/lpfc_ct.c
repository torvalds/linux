/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2004-2006 Emulex.  All rights reserved.           *
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
 * Fibre Channel SCSI LAN Device Driver CT support
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
#include "lpfc_disc.h"
#include "lpfc_scsi.h"
#include "lpfc.h"
#include "lpfc_logmsg.h"
#include "lpfc_crtn.h"
#include "lpfc_version.h"

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

/*
 * lpfc_ct_unsol_event
 */
void
lpfc_ct_unsol_event(struct lpfc_hba * phba,
		    struct lpfc_sli_ring * pring, struct lpfc_iocbq * piocbq)
{

	struct lpfc_iocbq *next_piocbq;
	struct lpfc_dmabuf *pmbuf = NULL;
	struct lpfc_dmabuf *matp, *next_matp;
	uint32_t ctx = 0, size = 0, cnt = 0;
	IOCB_t *icmd = &piocbq->iocb;
	IOCB_t *save_icmd = icmd;
	int i, go_exit = 0;
	struct list_head head;

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

	INIT_LIST_HEAD(&head);
	list_add_tail(&head, &piocbq->list);

	list_for_each_entry_safe(piocbq, next_piocbq, &head, list) {
		icmd = &piocbq->iocb;
		if (ctx == 0)
			ctx = (uint32_t) (icmd->ulpContext);
		if (icmd->ulpBdeCount == 0)
			continue;

		for (i = 0; i < icmd->ulpBdeCount; i++) {
			matp = lpfc_sli_ringpostbuf_get(phba, pring,
							getPaddr(icmd->un.
								 cont64[i].
								 addrHigh,
								 icmd->un.
								 cont64[i].
								 addrLow));
			if (!matp) {
				/* Insert lpfc log message here */
				lpfc_post_buffer(phba, pring, cnt, 1);
				go_exit = 1;
				goto ct_unsol_event_exit_piocbq;
			}

			/* Typically for Unsolicited CT requests */
			if (!pmbuf) {
				pmbuf = matp;
				INIT_LIST_HEAD(&pmbuf->list);
			} else
				list_add_tail(&matp->list, &pmbuf->list);

			size += icmd->un.cont64[i].tus.f.bdeSize;
			cnt++;
		}

		icmd->ulpBdeCount = 0;
	}

	lpfc_post_buffer(phba, pring, cnt, 1);
	if (save_icmd->ulpStatus) {
		go_exit = 1;
	}

ct_unsol_event_exit_piocbq:
	if (pmbuf) {
		list_for_each_entry_safe(matp, next_matp, &pmbuf->list, list) {
			lpfc_mbuf_free(phba, matp->virt, matp->phys);
			list_del(&matp->list);
			kfree(matp);
		}
		lpfc_mbuf_free(phba, pmbuf->virt, pmbuf->phys);
		kfree(pmbuf);
	}
	return;
}

static void
lpfc_free_ct_rsp(struct lpfc_hba * phba, struct lpfc_dmabuf * mlist)
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
lpfc_alloc_ct_rsp(struct lpfc_hba * phba, int cmdcode, struct ulp_bde64 * bpl,
		  uint32_t size, int *entries)
{
	struct lpfc_dmabuf *mlist = NULL;
	struct lpfc_dmabuf *mp;
	int cnt, i = 0;

	/* We get chucks of FCELSSIZE */
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

		if (cmdcode == be16_to_cpu(SLI_CTNS_GID_FT))
			mp->virt = lpfc_mbuf_alloc(phba, MEM_PRI, &(mp->phys));
		else
			mp->virt = lpfc_mbuf_alloc(phba, 0, &(mp->phys));

		if (!mp->virt) {
			kfree(mp);
			lpfc_free_ct_rsp(phba, mlist);
			return NULL;
		}

		/* Queue it to a linked list */
		if (!mlist)
			mlist = mp;
		else
			list_add_tail(&mp->list, &mlist->list);

		bpl->tus.f.bdeFlags = BUFF_USE_RCV;
		/* build buffer ptr list for IOCB */
		bpl->addrLow = le32_to_cpu( putPaddrLow(mp->phys) );
		bpl->addrHigh = le32_to_cpu( putPaddrHigh(mp->phys) );
		bpl->tus.f.bdeSize = (uint16_t) cnt;
		bpl->tus.w = le32_to_cpu(bpl->tus.w);
		bpl++;

		i++;
		size -= cnt;
	}

	*entries = i;
	return mlist;
}

static int
lpfc_gen_req(struct lpfc_hba *phba, struct lpfc_dmabuf *bmp,
	     struct lpfc_dmabuf *inp, struct lpfc_dmabuf *outp,
	     void (*cmpl) (struct lpfc_hba *, struct lpfc_iocbq *,
		     struct lpfc_iocbq *),
	     struct lpfc_nodelist *ndlp, uint32_t usr_flg, uint32_t num_entry,
	     uint32_t tmo)
{

	struct lpfc_sli *psli = &phba->sli;
	struct lpfc_sli_ring *pring = &psli->ring[LPFC_ELS_RING];
	IOCB_t *icmd;
	struct lpfc_iocbq *geniocb;

	/* Allocate buffer for  command iocb */
	spin_lock_irq(phba->host->host_lock);
	geniocb = lpfc_sli_get_iocbq(phba);
	spin_unlock_irq(phba->host->host_lock);

	if (geniocb == NULL)
		return 1;

	icmd = &geniocb->iocb;
	icmd->un.genreq64.bdl.ulpIoTag32 = 0;
	icmd->un.genreq64.bdl.addrHigh = putPaddrHigh(bmp->phys);
	icmd->un.genreq64.bdl.addrLow = putPaddrLow(bmp->phys);
	icmd->un.genreq64.bdl.bdeFlags = BUFF_TYPE_BDL;
	icmd->un.genreq64.bdl.bdeSize = (num_entry * sizeof (struct ulp_bde64));

	if (usr_flg)
		geniocb->context3 = NULL;
	else
		geniocb->context3 = (uint8_t *) bmp;

	/* Save for completion so we can release these resources */
	geniocb->context1 = (uint8_t *) inp;
	geniocb->context2 = (uint8_t *) outp;

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

	/* Issue GEN REQ IOCB for NPORT <did> */
	lpfc_printf_log(phba, KERN_INFO, LOG_ELS,
			"%d:0119 Issue GEN REQ IOCB for NPORT x%x "
			"Data: x%x x%x\n", phba->brd_no, icmd->un.ulpWord[5],
			icmd->ulpIoTag, phba->hba_state);
	geniocb->iocb_cmpl = cmpl;
	geniocb->drvrTimeout = icmd->ulpTimeout + LPFC_DRVR_TIMEOUT;
	spin_lock_irq(phba->host->host_lock);
	if (lpfc_sli_issue_iocb(phba, pring, geniocb, 0) == IOCB_ERROR) {
		lpfc_sli_release_iocbq(phba, geniocb);
		spin_unlock_irq(phba->host->host_lock);
		return 1;
	}
	spin_unlock_irq(phba->host->host_lock);

	return 0;
}

static int
lpfc_ct_cmd(struct lpfc_hba *phba, struct lpfc_dmabuf *inmp,
	    struct lpfc_dmabuf *bmp, struct lpfc_nodelist *ndlp,
	    void (*cmpl) (struct lpfc_hba *, struct lpfc_iocbq *,
			  struct lpfc_iocbq *),
	    uint32_t rsp_size)
{
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

	status = lpfc_gen_req(phba, bmp, inmp, outmp, cmpl, ndlp, 0,
			      cnt+1, 0);
	if (status) {
		lpfc_free_ct_rsp(phba, outmp);
		return -ENOMEM;
	}
	return 0;
}

static int
lpfc_ns_rsp(struct lpfc_hba * phba, struct lpfc_dmabuf * mp, uint32_t Size)
{
	struct lpfc_sli_ct_request *Response =
		(struct lpfc_sli_ct_request *) mp->virt;
	struct lpfc_nodelist *ndlp = NULL;
	struct lpfc_nodelist *next_ndlp;
	struct lpfc_dmabuf *mlast, *next_mp;
	uint32_t *ctptr = (uint32_t *) & Response->un.gid.PortType;
	uint32_t Did;
	uint32_t CTentry;
	int Cnt;
	struct list_head head;

	lpfc_set_disctmo(phba);

	Cnt = Size  > FCELSSIZE ? FCELSSIZE : Size;

	list_add_tail(&head, &mp->list);
	list_for_each_entry_safe(mp, next_mp, &head, list) {
		mlast = mp;

		Size -= Cnt;

		if (!ctptr)
			ctptr = (uint32_t *) mlast->virt;
		else
			Cnt -= 16;	/* subtract length of CT header */

		/* Loop through entire NameServer list of DIDs */
		while (Cnt) {

			/* Get next DID from NameServer List */
			CTentry = *ctptr++;
			Did = ((be32_to_cpu(CTentry)) & Mask_DID);

			ndlp = NULL;
			if (Did != phba->fc_myDID) {
				/* Check for rscn processing or not */
				ndlp = lpfc_setup_disc_node(phba, Did);
			}
			/* Mark all node table entries that are in the
			   Nameserver */
			if (ndlp) {
				/* NameServer Rsp */
				lpfc_printf_log(phba, KERN_INFO, LOG_DISCOVERY,
						"%d:0238 Process x%x NameServer"
						" Rsp Data: x%x x%x x%x\n",
						phba->brd_no,
						Did, ndlp->nlp_flag,
						phba->fc_flag,
						phba->fc_rscn_id_cnt);
			} else {
				/* NameServer Rsp */
				lpfc_printf_log(phba,
						KERN_INFO,
						LOG_DISCOVERY,
						"%d:0239 Skip x%x NameServer "
						"Rsp Data: x%x x%x x%x\n",
						phba->brd_no,
						Did, Size, phba->fc_flag,
						phba->fc_rscn_id_cnt);
			}

			if (CTentry & (be32_to_cpu(SLI_CT_LAST_ENTRY)))
				goto nsout1;
			Cnt -= sizeof (uint32_t);
		}
		ctptr = NULL;

	}

nsout1:
	list_del(&head);

	/*
 	 * The driver has cycled through all Nports in the RSCN payload.
 	 * Complete the handling by cleaning up and marking the
 	 * current driver state.
 	 */
	if (phba->hba_state == LPFC_HBA_READY) {

		/*
		 * Switch ports that connect a loop of multiple targets need
		 * special consideration.  The driver wants to unregister the
		 * rpi only on the target that was pulled from the loop.  On
		 * RSCN, the driver wants to rediscover an NPort only if the
		 * driver flagged it as NLP_NPR_2B_DISC.  Provided adisc is
		 * not enabled and the NPort is not capable of retransmissions
		 * (FC Tape) prevent timing races with the scsi error handler by
		 * unregistering the Nport's RPI.  This action causes all
		 * outstanding IO to flush back to the midlayer.
		 */
		list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_npr_list,
					 nlp_listp) {
			if (!(ndlp->nlp_flag & NLP_NPR_2B_DISC) &&
			    (lpfc_rscn_payload_check(phba, ndlp->nlp_DID))) {
				if ((phba->cfg_use_adisc == 0) &&
				    !(ndlp->nlp_fcp_info &
				      NLP_FCP_2_DEVICE)) {
					lpfc_unreg_rpi(phba, ndlp);
					ndlp->nlp_flag &= ~NLP_NPR_ADISC;
				}
			}
		}
		lpfc_els_flush_rscn(phba);
		spin_lock_irq(phba->host->host_lock);
		phba->fc_flag |= FC_RSCN_MODE; /* we are still in RSCN mode */
		spin_unlock_irq(phba->host->host_lock);
	}
	return 0;
}




static void
lpfc_cmpl_ct_cmd_gid_ft(struct lpfc_hba * phba, struct lpfc_iocbq * cmdiocb,
			struct lpfc_iocbq * rspiocb)
{
	IOCB_t *irsp;
	struct lpfc_sli *psli;
	struct lpfc_dmabuf *bmp;
	struct lpfc_dmabuf *inp;
	struct lpfc_dmabuf *outp;
	struct lpfc_nodelist *ndlp;
	struct lpfc_sli_ct_request *CTrsp;

	psli = &phba->sli;
	/* we pass cmdiocb to state machine which needs rspiocb as well */
	cmdiocb->context_un.rsp_iocb = rspiocb;

	inp = (struct lpfc_dmabuf *) cmdiocb->context1;
	outp = (struct lpfc_dmabuf *) cmdiocb->context2;
	bmp = (struct lpfc_dmabuf *) cmdiocb->context3;

	irsp = &rspiocb->iocb;
	if (irsp->ulpStatus) {
		if ((irsp->ulpStatus == IOSTAT_LOCAL_REJECT) &&
			((irsp->un.ulpWord[4] == IOERR_SLI_DOWN) ||
			 (irsp->un.ulpWord[4] == IOERR_SLI_ABORTED))) {
			goto out;
		}

		/* Check for retry */
		if (phba->fc_ns_retry < LPFC_MAX_NS_RETRY) {
			phba->fc_ns_retry++;
			/* CT command is being retried */
			ndlp =
			    lpfc_findnode_did(phba, NLP_SEARCH_UNMAPPED,
					      NameServer_DID);
			if (ndlp) {
				if (lpfc_ns_cmd(phba, ndlp, SLI_CTNS_GID_FT) ==
				    0) {
					goto out;
				}
			}
		}
	} else {
		/* Good status, continue checking */
		CTrsp = (struct lpfc_sli_ct_request *) outp->virt;
		if (CTrsp->CommandResponse.bits.CmdRsp ==
		    be16_to_cpu(SLI_CT_RESPONSE_FS_ACC)) {
			lpfc_printf_log(phba, KERN_INFO, LOG_DISCOVERY,
					"%d:0239 NameServer Rsp "
					"Data: x%x\n",
					phba->brd_no,
					phba->fc_flag);
			lpfc_ns_rsp(phba, outp,
				    (uint32_t) (irsp->un.genreq64.bdl.bdeSize));
		} else if (CTrsp->CommandResponse.bits.CmdRsp ==
			   be16_to_cpu(SLI_CT_RESPONSE_FS_RJT)) {
			/* NameServer Rsp Error */
			lpfc_printf_log(phba, KERN_INFO, LOG_DISCOVERY,
					"%d:0240 NameServer Rsp Error "
					"Data: x%x x%x x%x x%x\n",
					phba->brd_no,
					CTrsp->CommandResponse.bits.CmdRsp,
					(uint32_t) CTrsp->ReasonCode,
					(uint32_t) CTrsp->Explanation,
					phba->fc_flag);
		} else {
			/* NameServer Rsp Error */
			lpfc_printf_log(phba,
					KERN_INFO,
					LOG_DISCOVERY,
					"%d:0241 NameServer Rsp Error "
					"Data: x%x x%x x%x x%x\n",
					phba->brd_no,
					CTrsp->CommandResponse.bits.CmdRsp,
					(uint32_t) CTrsp->ReasonCode,
					(uint32_t) CTrsp->Explanation,
					phba->fc_flag);
		}
	}
	/* Link up / RSCN discovery */
	lpfc_disc_start(phba);
out:
	lpfc_free_ct_rsp(phba, outp);
	lpfc_mbuf_free(phba, inp->virt, inp->phys);
	lpfc_mbuf_free(phba, bmp->virt, bmp->phys);
	kfree(inp);
	kfree(bmp);
	spin_lock_irq(phba->host->host_lock);
	lpfc_sli_release_iocbq(phba, cmdiocb);
	spin_unlock_irq(phba->host->host_lock);
	return;
}

static void
lpfc_cmpl_ct_cmd_rft_id(struct lpfc_hba * phba, struct lpfc_iocbq * cmdiocb,
			struct lpfc_iocbq * rspiocb)
{
	struct lpfc_sli *psli;
	struct lpfc_dmabuf *bmp;
	struct lpfc_dmabuf *inp;
	struct lpfc_dmabuf *outp;
	IOCB_t *irsp;
	struct lpfc_sli_ct_request *CTrsp;

	psli = &phba->sli;
	/* we pass cmdiocb to state machine which needs rspiocb as well */
	cmdiocb->context_un.rsp_iocb = rspiocb;

	inp = (struct lpfc_dmabuf *) cmdiocb->context1;
	outp = (struct lpfc_dmabuf *) cmdiocb->context2;
	bmp = (struct lpfc_dmabuf *) cmdiocb->context3;
	irsp = &rspiocb->iocb;

	CTrsp = (struct lpfc_sli_ct_request *) outp->virt;

	/* RFT request completes status <ulpStatus> CmdRsp <CmdRsp> */
	lpfc_printf_log(phba, KERN_INFO, LOG_DISCOVERY,
			"%d:0209 RFT request completes ulpStatus x%x "
			"CmdRsp x%x\n", phba->brd_no, irsp->ulpStatus,
			CTrsp->CommandResponse.bits.CmdRsp);

	lpfc_free_ct_rsp(phba, outp);
	lpfc_mbuf_free(phba, inp->virt, inp->phys);
	lpfc_mbuf_free(phba, bmp->virt, bmp->phys);
	kfree(inp);
	kfree(bmp);
	spin_lock_irq(phba->host->host_lock);
	lpfc_sli_release_iocbq(phba, cmdiocb);
	spin_unlock_irq(phba->host->host_lock);
	return;
}

static void
lpfc_cmpl_ct_cmd_rnn_id(struct lpfc_hba * phba, struct lpfc_iocbq * cmdiocb,
			struct lpfc_iocbq * rspiocb)
{
	lpfc_cmpl_ct_cmd_rft_id(phba, cmdiocb, rspiocb);
	return;
}

static void
lpfc_cmpl_ct_cmd_rsnn_nn(struct lpfc_hba * phba, struct lpfc_iocbq * cmdiocb,
			 struct lpfc_iocbq * rspiocb)
{
	lpfc_cmpl_ct_cmd_rft_id(phba, cmdiocb, rspiocb);
	return;
}

void
lpfc_get_hba_sym_node_name(struct lpfc_hba * phba, uint8_t * symbp)
{
	char fwrev[16];

	lpfc_decode_firmware_rev(phba, fwrev, 0);

	if (phba->Port[0]) {
		sprintf(symbp, "Emulex %s Port %s FV%s DV%s", phba->ModelName,
			phba->Port, fwrev, lpfc_release_version);
	} else {
		sprintf(symbp, "Emulex %s FV%s DV%s", phba->ModelName,
			fwrev, lpfc_release_version);
	}
}

/*
 * lpfc_ns_cmd
 * Description:
 *    Issue Cmd to NameServer
 *       SLI_CTNS_GID_FT
 *       LI_CTNS_RFT_ID
 */
int
lpfc_ns_cmd(struct lpfc_hba * phba, struct lpfc_nodelist * ndlp, int cmdcode)
{
	struct lpfc_dmabuf *mp, *bmp;
	struct lpfc_sli_ct_request *CtReq;
	struct ulp_bde64 *bpl;
	void (*cmpl) (struct lpfc_hba *, struct lpfc_iocbq *,
		      struct lpfc_iocbq *) = NULL;
	uint32_t rsp_size = 1024;

	/* fill in BDEs for command */
	/* Allocate buffer for command payload */
	mp = kmalloc(sizeof (struct lpfc_dmabuf), GFP_KERNEL);
	if (!mp)
		goto ns_cmd_exit;

	INIT_LIST_HEAD(&mp->list);
	mp->virt = lpfc_mbuf_alloc(phba, MEM_PRI, &(mp->phys));
	if (!mp->virt)
		goto ns_cmd_free_mp;

	/* Allocate buffer for Buffer ptr list */
	bmp = kmalloc(sizeof (struct lpfc_dmabuf), GFP_KERNEL);
	if (!bmp)
		goto ns_cmd_free_mpvirt;

	INIT_LIST_HEAD(&bmp->list);
	bmp->virt = lpfc_mbuf_alloc(phba, MEM_PRI, &(bmp->phys));
	if (!bmp->virt)
		goto ns_cmd_free_bmp;

	/* NameServer Req */
	lpfc_printf_log(phba,
			KERN_INFO,
			LOG_DISCOVERY,
			"%d:0236 NameServer Req Data: x%x x%x x%x\n",
			phba->brd_no, cmdcode, phba->fc_flag,
			phba->fc_rscn_id_cnt);

	bpl = (struct ulp_bde64 *) bmp->virt;
	memset(bpl, 0, sizeof(struct ulp_bde64));
	bpl->addrHigh = le32_to_cpu( putPaddrHigh(mp->phys) );
	bpl->addrLow = le32_to_cpu( putPaddrLow(mp->phys) );
	bpl->tus.f.bdeFlags = 0;
	if (cmdcode == SLI_CTNS_GID_FT)
		bpl->tus.f.bdeSize = GID_REQUEST_SZ;
	else if (cmdcode == SLI_CTNS_RFT_ID)
		bpl->tus.f.bdeSize = RFT_REQUEST_SZ;
	else if (cmdcode == SLI_CTNS_RNN_ID)
		bpl->tus.f.bdeSize = RNN_REQUEST_SZ;
	else if (cmdcode == SLI_CTNS_RSNN_NN)
		bpl->tus.f.bdeSize = RSNN_REQUEST_SZ;
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
		if (phba->hba_state < LPFC_HBA_READY)
			phba->hba_state = LPFC_NS_QRY;
		lpfc_set_disctmo(phba);
		cmpl = lpfc_cmpl_ct_cmd_gid_ft;
		rsp_size = FC_MAX_NS_RSP;
		break;

	case SLI_CTNS_RFT_ID:
		CtReq->CommandResponse.bits.CmdRsp =
		    be16_to_cpu(SLI_CTNS_RFT_ID);
		CtReq->un.rft.PortId = be32_to_cpu(phba->fc_myDID);
		CtReq->un.rft.fcpReg = 1;
		cmpl = lpfc_cmpl_ct_cmd_rft_id;
		break;

	case SLI_CTNS_RNN_ID:
		CtReq->CommandResponse.bits.CmdRsp =
		    be16_to_cpu(SLI_CTNS_RNN_ID);
		CtReq->un.rnn.PortId = be32_to_cpu(phba->fc_myDID);
		memcpy(CtReq->un.rnn.wwnn,  &phba->fc_nodename,
		       sizeof (struct lpfc_name));
		cmpl = lpfc_cmpl_ct_cmd_rnn_id;
		break;

	case SLI_CTNS_RSNN_NN:
		CtReq->CommandResponse.bits.CmdRsp =
		    be16_to_cpu(SLI_CTNS_RSNN_NN);
		memcpy(CtReq->un.rsnn.wwnn, &phba->fc_nodename,
		       sizeof (struct lpfc_name));
		lpfc_get_hba_sym_node_name(phba, CtReq->un.rsnn.symbname);
		CtReq->un.rsnn.len = strlen(CtReq->un.rsnn.symbname);
		cmpl = lpfc_cmpl_ct_cmd_rsnn_nn;
		break;
	}

	if (!lpfc_ct_cmd(phba, mp, bmp, ndlp, cmpl, rsp_size))
		/* On success, The cmpl function will free the buffers */
		return 0;

	lpfc_mbuf_free(phba, bmp->virt, bmp->phys);
ns_cmd_free_bmp:
	kfree(bmp);
ns_cmd_free_mpvirt:
	lpfc_mbuf_free(phba, mp->virt, mp->phys);
ns_cmd_free_mp:
	kfree(mp);
ns_cmd_exit:
	return 1;
}

static void
lpfc_cmpl_ct_cmd_fdmi(struct lpfc_hba * phba,
		      struct lpfc_iocbq * cmdiocb, struct lpfc_iocbq * rspiocb)
{
	struct lpfc_dmabuf *bmp = cmdiocb->context3;
	struct lpfc_dmabuf *inp = cmdiocb->context1;
	struct lpfc_dmabuf *outp = cmdiocb->context2;
	struct lpfc_sli_ct_request *CTrsp = outp->virt;
	struct lpfc_sli_ct_request *CTcmd = inp->virt;
	struct lpfc_nodelist *ndlp;
	uint16_t fdmi_cmd = CTcmd->CommandResponse.bits.CmdRsp;
	uint16_t fdmi_rsp = CTrsp->CommandResponse.bits.CmdRsp;

	ndlp = lpfc_findnode_did(phba, NLP_SEARCH_ALL, FDMI_DID);
	if (fdmi_rsp == be16_to_cpu(SLI_CT_RESPONSE_FS_RJT)) {
		/* FDMI rsp failed */
		lpfc_printf_log(phba,
			        KERN_INFO,
			        LOG_DISCOVERY,
			        "%d:0220 FDMI rsp failed Data: x%x\n",
			        phba->brd_no,
			       be16_to_cpu(fdmi_cmd));
	}

	switch (be16_to_cpu(fdmi_cmd)) {
	case SLI_MGMT_RHBA:
		lpfc_fdmi_cmd(phba, ndlp, SLI_MGMT_RPA);
		break;

	case SLI_MGMT_RPA:
		break;

	case SLI_MGMT_DHBA:
		lpfc_fdmi_cmd(phba, ndlp, SLI_MGMT_DPRT);
		break;

	case SLI_MGMT_DPRT:
		lpfc_fdmi_cmd(phba, ndlp, SLI_MGMT_RHBA);
		break;
	}

	lpfc_free_ct_rsp(phba, outp);
	lpfc_mbuf_free(phba, inp->virt, inp->phys);
	lpfc_mbuf_free(phba, bmp->virt, bmp->phys);
	kfree(inp);
	kfree(bmp);
	spin_lock_irq(phba->host->host_lock);
	lpfc_sli_release_iocbq(phba, cmdiocb);
	spin_unlock_irq(phba->host->host_lock);
	return;
}
int
lpfc_fdmi_cmd(struct lpfc_hba * phba, struct lpfc_nodelist * ndlp, int cmdcode)
{
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
	lpfc_printf_log(phba,
		        KERN_INFO,
		        LOG_DISCOVERY,
		        "%d:0218 FDMI Request Data: x%x x%x x%x\n",
		        phba->brd_no,
		       phba->fc_flag, phba->hba_state, cmdcode);

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
			memcpy(&rh->hi.PortName, &phba->fc_sparam.portName,
			       sizeof (struct lpfc_name));
			/* One entry (port) per adapter */
			rh->rpl.EntryCnt = be32_to_cpu(1);
			memcpy(&rh->rpl.pe, &phba->fc_sparam.portName,
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
			memcpy(&ae->un.NodeName, &phba->fc_sparam.nodeName,
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
				system_utsname.sysname, system_utsname.release,
				system_utsname.version);
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
			       (uint8_t *) & phba->fc_sparam.portName,
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
			hsp = (struct serv_parm *) & phba->fc_sparam;
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

			if (phba->cfg_fdmi_on == 2) {
				/* #6 Port attribute entry */
				ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) pab +
							  size);
				ae->ad.bits.AttrType = be16_to_cpu(HOST_NAME);
				sprintf(ae->un.HostName, "%s",
					system_utsname.nodename);
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
		       (uint8_t *) & phba->fc_sparam.portName,
		       sizeof (struct lpfc_name));
		size = GID_REQUEST_SZ - 4 + sizeof (struct lpfc_name);
		break;

	case SLI_MGMT_DPRT:
		CtReq->CommandResponse.bits.CmdRsp = be16_to_cpu(SLI_MGMT_DPRT);
		CtReq->CommandResponse.bits.Size = 0;
		pe = (PORT_ENTRY *) & CtReq->un.PortID;
		memcpy((uint8_t *) & pe->PortName,
		       (uint8_t *) & phba->fc_sparam.portName,
		       sizeof (struct lpfc_name));
		size = GID_REQUEST_SZ - 4 + sizeof (struct lpfc_name);
		break;
	}

	bpl = (struct ulp_bde64 *) bmp->virt;
	bpl->addrHigh = le32_to_cpu( putPaddrHigh(mp->phys) );
	bpl->addrLow = le32_to_cpu( putPaddrLow(mp->phys) );
	bpl->tus.f.bdeFlags = 0;
	bpl->tus.f.bdeSize = size;
	bpl->tus.w = le32_to_cpu(bpl->tus.w);

	cmpl = lpfc_cmpl_ct_cmd_fdmi;

	if (!lpfc_ct_cmd(phba, mp, bmp, ndlp, cmpl, FC_MAX_NS_RSP))
		return 0;

	lpfc_mbuf_free(phba, bmp->virt, bmp->phys);
fdmi_cmd_free_bmp:
	kfree(bmp);
fdmi_cmd_free_mpvirt:
	lpfc_mbuf_free(phba, mp->virt, mp->phys);
fdmi_cmd_free_mp:
	kfree(mp);
fdmi_cmd_exit:
	/* Issue FDMI request failed */
	lpfc_printf_log(phba,
		        KERN_INFO,
		        LOG_DISCOVERY,
		        "%d:0244 Issue FDMI request failed Data: x%x\n",
		        phba->brd_no,
			cmdcode);
	return 1;
}

void
lpfc_fdmi_tmo(unsigned long ptr)
{
	struct lpfc_hba *phba = (struct lpfc_hba *)ptr;
	unsigned long iflag;

	spin_lock_irqsave(phba->host->host_lock, iflag);
	if (!(phba->work_hba_events & WORKER_FDMI_TMO)) {
		phba->work_hba_events |= WORKER_FDMI_TMO;
		if (phba->work_wait)
			wake_up(phba->work_wait);
	}
	spin_unlock_irqrestore(phba->host->host_lock,iflag);
}

void
lpfc_fdmi_tmo_handler(struct lpfc_hba *phba)
{
	struct lpfc_nodelist *ndlp;

	ndlp = lpfc_findnode_did(phba, NLP_SEARCH_ALL, FDMI_DID);
	if (ndlp) {
		if (system_utsname.nodename[0] != '\0') {
			lpfc_fdmi_cmd(phba, ndlp, SLI_MGMT_DHBA);
		} else {
			mod_timer(&phba->fc_fdmitmo, jiffies + HZ * 60);
		}
	}
	return;
}


void
lpfc_decode_firmware_rev(struct lpfc_hba * phba, char *fwrevision, int flag)
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
