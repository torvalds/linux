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
#include <linux/interrupt.h>

#include <scsi/scsi_device.h>
#include <scsi/scsi_transport_fc.h>

#include <scsi/scsi.h>

#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_disc.h"
#include "lpfc_scsi.h"
#include "lpfc.h"
#include "lpfc_logmsg.h"
#include "lpfc_crtn.h"
#include "lpfc_compat.h"

/**********************************************/

/*                mailbox command             */
/**********************************************/
void
lpfc_dump_mem(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb, uint16_t offset)
{
	MAILBOX_t *mb;
	void *ctx;

	mb = &pmb->mb;
	ctx = pmb->context2;

	/* Setup to dump VPD region */
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));
	mb->mbxCommand = MBX_DUMP_MEMORY;
	mb->un.varDmp.cv = 1;
	mb->un.varDmp.type = DMP_NV_PARAMS;
	mb->un.varDmp.entry_index = offset;
	mb->un.varDmp.region_id = DMP_REGION_VPD;
	mb->un.varDmp.word_cnt = (DMP_RSP_SIZE / sizeof (uint32_t));
	mb->un.varDmp.co = 0;
	mb->un.varDmp.resp_offset = 0;
	pmb->context2 = ctx;
	mb->mbxOwner = OWN_HOST;
	return;
}

/**********************************************/
/*  lpfc_read_nv  Issue a READ NVPARAM        */
/*                mailbox command             */
/**********************************************/
void
lpfc_read_nv(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb)
{
	MAILBOX_t *mb;

	mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));
	mb->mbxCommand = MBX_READ_NV;
	mb->mbxOwner = OWN_HOST;
	return;
}

/**********************************************/
/*  lpfc_heart_beat  Issue a HEART_BEAT       */
/*                mailbox command             */
/**********************************************/
void
lpfc_heart_beat(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb)
{
	MAILBOX_t *mb;

	mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));
	mb->mbxCommand = MBX_HEARTBEAT;
	mb->mbxOwner = OWN_HOST;
	return;
}

/**********************************************/
/*  lpfc_read_la  Issue a READ LA             */
/*                mailbox command             */
/**********************************************/
int
lpfc_read_la(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb, struct lpfc_dmabuf *mp)
{
	MAILBOX_t *mb;
	struct lpfc_sli *psli;

	psli = &phba->sli;
	mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	INIT_LIST_HEAD(&mp->list);
	mb->mbxCommand = MBX_READ_LA64;
	mb->un.varReadLA.un.lilpBde64.tus.f.bdeSize = 128;
	mb->un.varReadLA.un.lilpBde64.addrHigh = putPaddrHigh(mp->phys);
	mb->un.varReadLA.un.lilpBde64.addrLow = putPaddrLow(mp->phys);

	/* Save address for later completion and set the owner to host so that
	 * the FW knows this mailbox is available for processing.
	 */
	pmb->context1 = (uint8_t *) mp;
	mb->mbxOwner = OWN_HOST;
	return (0);
}

/**********************************************/
/*  lpfc_clear_la  Issue a CLEAR LA           */
/*                 mailbox command            */
/**********************************************/
void
lpfc_clear_la(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb)
{
	MAILBOX_t *mb;

	mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	mb->un.varClearLA.eventTag = phba->fc_eventTag;
	mb->mbxCommand = MBX_CLEAR_LA;
	mb->mbxOwner = OWN_HOST;
	return;
}

/**************************************************/
/*  lpfc_config_link  Issue a CONFIG LINK         */
/*                    mailbox command             */
/**************************************************/
void
lpfc_config_link(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb)
{
	struct lpfc_vport  *vport = phba->pport;
	MAILBOX_t *mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	/* NEW_FEATURE
	 * SLI-2, Coalescing Response Feature.
	 */
	if (phba->cfg_cr_delay) {
		mb->un.varCfgLnk.cr = 1;
		mb->un.varCfgLnk.ci = 1;
		mb->un.varCfgLnk.cr_delay = phba->cfg_cr_delay;
		mb->un.varCfgLnk.cr_count = phba->cfg_cr_count;
	}

	mb->un.varCfgLnk.myId = vport->fc_myDID;
	mb->un.varCfgLnk.edtov = phba->fc_edtov;
	mb->un.varCfgLnk.arbtov = phba->fc_arbtov;
	mb->un.varCfgLnk.ratov = phba->fc_ratov;
	mb->un.varCfgLnk.rttov = phba->fc_rttov;
	mb->un.varCfgLnk.altov = phba->fc_altov;
	mb->un.varCfgLnk.crtov = phba->fc_crtov;
	mb->un.varCfgLnk.citov = phba->fc_citov;

	if (phba->cfg_ack0)
		mb->un.varCfgLnk.ack0_enable = 1;

	mb->mbxCommand = MBX_CONFIG_LINK;
	mb->mbxOwner = OWN_HOST;
	return;
}

/**********************************************/
/*  lpfc_init_link  Issue an INIT LINK        */
/*                  mailbox command           */
/**********************************************/
void
lpfc_init_link(struct lpfc_hba * phba,
	       LPFC_MBOXQ_t * pmb, uint32_t topology, uint32_t linkspeed)
{
	lpfc_vpd_t *vpd;
	struct lpfc_sli *psli;
	MAILBOX_t *mb;

	mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	psli = &phba->sli;
	switch (topology) {
	case FLAGS_TOPOLOGY_MODE_LOOP_PT:
		mb->un.varInitLnk.link_flags = FLAGS_TOPOLOGY_MODE_LOOP;
		mb->un.varInitLnk.link_flags |= FLAGS_TOPOLOGY_FAILOVER;
		break;
	case FLAGS_TOPOLOGY_MODE_PT_PT:
		mb->un.varInitLnk.link_flags = FLAGS_TOPOLOGY_MODE_PT_PT;
		break;
	case FLAGS_TOPOLOGY_MODE_LOOP:
		mb->un.varInitLnk.link_flags = FLAGS_TOPOLOGY_MODE_LOOP;
		break;
	case FLAGS_TOPOLOGY_MODE_PT_LOOP:
		mb->un.varInitLnk.link_flags = FLAGS_TOPOLOGY_MODE_PT_PT;
		mb->un.varInitLnk.link_flags |= FLAGS_TOPOLOGY_FAILOVER;
		break;
	case FLAGS_LOCAL_LB:
		mb->un.varInitLnk.link_flags = FLAGS_LOCAL_LB;
		break;
	}

	/* Enable asynchronous ABTS responses from firmware */
	mb->un.varInitLnk.link_flags |= FLAGS_IMED_ABORT;

	/* NEW_FEATURE
	 * Setting up the link speed
	 */
	vpd = &phba->vpd;
	if (vpd->rev.feaLevelHigh >= 0x02){
		switch(linkspeed){
			case LINK_SPEED_1G:
			case LINK_SPEED_2G:
			case LINK_SPEED_4G:
			case LINK_SPEED_8G:
				mb->un.varInitLnk.link_flags |=
							FLAGS_LINK_SPEED;
				mb->un.varInitLnk.link_speed = linkspeed;
			break;
			case LINK_SPEED_AUTO:
			default:
				mb->un.varInitLnk.link_speed =
							LINK_SPEED_AUTO;
			break;
		}

	}
	else
		mb->un.varInitLnk.link_speed = LINK_SPEED_AUTO;

	mb->mbxCommand = (volatile uint8_t)MBX_INIT_LINK;
	mb->mbxOwner = OWN_HOST;
	mb->un.varInitLnk.fabric_AL_PA = phba->fc_pref_ALPA;
	return;
}

/**********************************************/
/*  lpfc_read_sparam  Issue a READ SPARAM     */
/*                    mailbox command         */
/**********************************************/
int
lpfc_read_sparam(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmb, int vpi)
{
	struct lpfc_dmabuf *mp;
	MAILBOX_t *mb;
	struct lpfc_sli *psli;

	psli = &phba->sli;
	mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	mb->mbxOwner = OWN_HOST;

	/* Get a buffer to hold the HBAs Service Parameters */

	if (((mp = kmalloc(sizeof (struct lpfc_dmabuf), GFP_KERNEL)) == 0) ||
	    ((mp->virt = lpfc_mbuf_alloc(phba, 0, &(mp->phys))) == 0)) {
		kfree(mp);
		mb->mbxCommand = MBX_READ_SPARM64;
		/* READ_SPARAM: no buffers */
		lpfc_printf_log(phba,
			        KERN_WARNING,
			        LOG_MBOX,
			        "%d:0301 READ_SPARAM: no buffers\n",
			        phba->brd_no);
		return (1);
	}
	INIT_LIST_HEAD(&mp->list);
	mb->mbxCommand = MBX_READ_SPARM64;
	mb->un.varRdSparm.un.sp64.tus.f.bdeSize = sizeof (struct serv_parm);
	mb->un.varRdSparm.un.sp64.addrHigh = putPaddrHigh(mp->phys);
	mb->un.varRdSparm.un.sp64.addrLow = putPaddrLow(mp->phys);
	mb->un.varRdSparm.vpi = vpi;

	/* save address for completion */
	pmb->context1 = mp;

	return (0);
}

/********************************************/
/*  lpfc_unreg_did  Issue a UNREG_DID       */
/*                  mailbox command         */
/********************************************/
void
lpfc_unreg_did(struct lpfc_hba * phba, uint16_t vpi, uint32_t did,
	       LPFC_MBOXQ_t * pmb)
{
	MAILBOX_t *mb;

	mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	mb->un.varUnregDID.did = did;
	mb->un.varUnregDID.vpi = vpi;

	mb->mbxCommand = MBX_UNREG_D_ID;
	mb->mbxOwner = OWN_HOST;
	return;
}

/**********************************************/
/*  lpfc_read_nv  Issue a READ CONFIG         */
/*                mailbox command             */
/**********************************************/
void
lpfc_read_config(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb)
{
	MAILBOX_t *mb;

	mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	mb->mbxCommand = MBX_READ_CONFIG;
	mb->mbxOwner = OWN_HOST;
	return;
}

/*************************************************/
/*  lpfc_read_lnk_stat  Issue a READ LINK STATUS */
/*                mailbox command                */
/*************************************************/
void
lpfc_read_lnk_stat(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb)
{
	MAILBOX_t *mb;

	mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	mb->mbxCommand = MBX_READ_LNK_STAT;
	mb->mbxOwner = OWN_HOST;
	return;
}

/********************************************/
/*  lpfc_reg_login  Issue a REG_LOGIN       */
/*                  mailbox command         */
/********************************************/
int
lpfc_reg_login(struct lpfc_hba *phba, uint16_t vpi, uint32_t did,
	       uint8_t *param, LPFC_MBOXQ_t *pmb, uint32_t flag)
{
	MAILBOX_t *mb = &pmb->mb;
	uint8_t *sparam;
	struct lpfc_dmabuf *mp;

	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	mb->un.varRegLogin.rpi = 0;
	mb->un.varRegLogin.vpi = vpi;
	mb->un.varRegLogin.did = did;
	mb->un.varWords[30] = flag;	/* Set flag to issue action on cmpl */

	mb->mbxOwner = OWN_HOST;

	/* Get a buffer to hold NPorts Service Parameters */
	if (((mp = kmalloc(sizeof (struct lpfc_dmabuf), GFP_KERNEL)) == NULL) ||
	    ((mp->virt = lpfc_mbuf_alloc(phba, 0, &(mp->phys))) == 0)) {
		kfree(mp);
		mb->mbxCommand = MBX_REG_LOGIN64;
		/* REG_LOGIN: no buffers */
		lpfc_printf_log(phba, KERN_WARNING, LOG_MBOX,
				"%d (%d):0302 REG_LOGIN: no buffers, DID x%x, "
				"flag x%x\n",
				phba->brd_no, vpi, did, flag);
		return (1);
	}
	INIT_LIST_HEAD(&mp->list);
	sparam = mp->virt;

	/* Copy param's into a new buffer */
	memcpy(sparam, param, sizeof (struct serv_parm));

	/* save address for completion */
	pmb->context1 = (uint8_t *) mp;

	mb->mbxCommand = MBX_REG_LOGIN64;
	mb->un.varRegLogin.un.sp64.tus.f.bdeSize = sizeof (struct serv_parm);
	mb->un.varRegLogin.un.sp64.addrHigh = putPaddrHigh(mp->phys);
	mb->un.varRegLogin.un.sp64.addrLow = putPaddrLow(mp->phys);

	return (0);
}

/**********************************************/
/*  lpfc_unreg_login  Issue a UNREG_LOGIN     */
/*                    mailbox command         */
/**********************************************/
void
lpfc_unreg_login(struct lpfc_hba *phba, uint16_t vpi, uint32_t rpi,
		 LPFC_MBOXQ_t * pmb)
{
	MAILBOX_t *mb;

	mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	mb->un.varUnregLogin.rpi = (uint16_t) rpi;
	mb->un.varUnregLogin.rsvd1 = 0;
	mb->un.varUnregLogin.vpi = vpi;

	mb->mbxCommand = MBX_UNREG_LOGIN;
	mb->mbxOwner = OWN_HOST;
	return;
}

/**************************************************/
/*  lpfc_reg_vpi   Issue a REG_VPI                */
/*                    mailbox command             */
/**************************************************/
void
lpfc_reg_vpi(struct lpfc_hba *phba, uint16_t vpi, uint32_t sid,
	     LPFC_MBOXQ_t *pmb)
{
	MAILBOX_t *mb = &pmb->mb;

	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	mb->un.varRegVpi.vpi = vpi;
	mb->un.varRegVpi.sid = sid;

	mb->mbxCommand = MBX_REG_VPI;
	mb->mbxOwner = OWN_HOST;
	return;

}

/**************************************************/
/*  lpfc_unreg_vpi   Issue a UNREG_VNPI           */
/*                    mailbox command             */
/**************************************************/
void
lpfc_unreg_vpi(struct lpfc_hba *phba, uint16_t vpi, LPFC_MBOXQ_t *pmb)
{
	MAILBOX_t *mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	mb->un.varUnregVpi.vpi = vpi;

	mb->mbxCommand = MBX_UNREG_VPI;
	mb->mbxOwner = OWN_HOST;
	return;

}

static void
lpfc_config_pcb_setup(struct lpfc_hba * phba)
{
	struct lpfc_sli *psli = &phba->sli;
	struct lpfc_sli_ring *pring;
	PCB_t *pcbp = &phba->slim2p->pcb;
	dma_addr_t pdma_addr;
	uint32_t offset;
	uint32_t iocbCnt = 0;
	int i;

	pcbp->maxRing = (psli->num_rings - 1);

	for (i = 0; i < psli->num_rings; i++) {
		pring = &psli->ring[i];

		pring->sizeCiocb = phba->sli_rev == 3 ? SLI3_IOCB_CMD_SIZE:
							SLI2_IOCB_CMD_SIZE;
		pring->sizeRiocb = phba->sli_rev == 3 ? SLI3_IOCB_RSP_SIZE:
							SLI2_IOCB_RSP_SIZE;
		/* A ring MUST have both cmd and rsp entries defined to be
		   valid */
		if ((pring->numCiocb == 0) || (pring->numRiocb == 0)) {
			pcbp->rdsc[i].cmdEntries = 0;
			pcbp->rdsc[i].rspEntries = 0;
			pcbp->rdsc[i].cmdAddrHigh = 0;
			pcbp->rdsc[i].rspAddrHigh = 0;
			pcbp->rdsc[i].cmdAddrLow = 0;
			pcbp->rdsc[i].rspAddrLow = 0;
			pring->cmdringaddr = NULL;
			pring->rspringaddr = NULL;
			continue;
		}
		/* Command ring setup for ring */
		pring->cmdringaddr = (void *) &phba->slim2p->IOCBs[iocbCnt];
		pcbp->rdsc[i].cmdEntries = pring->numCiocb;

		offset = (uint8_t *) &phba->slim2p->IOCBs[iocbCnt] -
			 (uint8_t *) phba->slim2p;
		pdma_addr = phba->slim2p_mapping + offset;
		pcbp->rdsc[i].cmdAddrHigh = putPaddrHigh(pdma_addr);
		pcbp->rdsc[i].cmdAddrLow = putPaddrLow(pdma_addr);
		iocbCnt += pring->numCiocb;

		/* Response ring setup for ring */
		pring->rspringaddr = (void *) &phba->slim2p->IOCBs[iocbCnt];

		pcbp->rdsc[i].rspEntries = pring->numRiocb;
		offset = (uint8_t *)&phba->slim2p->IOCBs[iocbCnt] -
			 (uint8_t *)phba->slim2p;
		pdma_addr = phba->slim2p_mapping + offset;
		pcbp->rdsc[i].rspAddrHigh = putPaddrHigh(pdma_addr);
		pcbp->rdsc[i].rspAddrLow = putPaddrLow(pdma_addr);
		iocbCnt += pring->numRiocb;
	}
}

void
lpfc_read_rev(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb)
{
	MAILBOX_t *mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));
	mb->un.varRdRev.cv = 1;
	mb->un.varRdRev.v3req = 1; /* Request SLI3 info */
	mb->mbxCommand = MBX_READ_REV;
	mb->mbxOwner = OWN_HOST;
	return;
}

static void
lpfc_build_hbq_profile2(struct config_hbq_var *hbqmb,
			struct lpfc_hbq_init  *hbq_desc)
{
	hbqmb->profiles.profile2.seqlenbcnt = hbq_desc->seqlenbcnt;
	hbqmb->profiles.profile2.maxlen     = hbq_desc->maxlen;
	hbqmb->profiles.profile2.seqlenoff  = hbq_desc->seqlenoff;
}

static void
lpfc_build_hbq_profile3(struct config_hbq_var *hbqmb,
			struct lpfc_hbq_init  *hbq_desc)
{
	hbqmb->profiles.profile3.seqlenbcnt = hbq_desc->seqlenbcnt;
	hbqmb->profiles.profile3.maxlen     = hbq_desc->maxlen;
	hbqmb->profiles.profile3.cmdcodeoff = hbq_desc->cmdcodeoff;
	hbqmb->profiles.profile3.seqlenoff  = hbq_desc->seqlenoff;
	memcpy(&hbqmb->profiles.profile3.cmdmatch, hbq_desc->cmdmatch,
	       sizeof(hbqmb->profiles.profile3.cmdmatch));
}

static void
lpfc_build_hbq_profile5(struct config_hbq_var *hbqmb,
			struct lpfc_hbq_init  *hbq_desc)
{
	hbqmb->profiles.profile5.seqlenbcnt = hbq_desc->seqlenbcnt;
	hbqmb->profiles.profile5.maxlen     = hbq_desc->maxlen;
	hbqmb->profiles.profile5.cmdcodeoff = hbq_desc->cmdcodeoff;
	hbqmb->profiles.profile5.seqlenoff  = hbq_desc->seqlenoff;
	memcpy(&hbqmb->profiles.profile5.cmdmatch, hbq_desc->cmdmatch,
	       sizeof(hbqmb->profiles.profile5.cmdmatch));
}

void
lpfc_config_hbq(struct lpfc_hba *phba, struct lpfc_hbq_init *hbq_desc,
		uint32_t hbq_entry_index, LPFC_MBOXQ_t *pmb)
{
	int i;
	MAILBOX_t *mb = &pmb->mb;
	struct config_hbq_var *hbqmb = &mb->un.varCfgHbq;

	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));
	hbqmb->entry_count = hbq_desc->entry_count;   /* # entries in HBQ */
	hbqmb->recvNotify = hbq_desc->rn;             /* Receive
						       * Notification */
	hbqmb->numMask    = hbq_desc->mask_count;     /* # R_CTL/TYPE masks
						       * # in words 0-19 */
	hbqmb->profile    = hbq_desc->profile;	      /* Selection profile:
						       * 0 = all,
						       * 7 = logentry */
	hbqmb->ringMask   = hbq_desc->ring_mask;      /* Binds HBQ to a ring
						       * e.g. Ring0=b0001,
						       * ring2=b0100 */
	hbqmb->headerLen  = hbq_desc->headerLen;      /* 0 if not profile 4
						       * or 5 */
	hbqmb->logEntry   = hbq_desc->logEntry;       /* Set to 1 if this
						       * HBQ will be used
						       * for LogEntry
						       * buffers */
	hbqmb->hbqaddrLow = putPaddrLow(phba->hbqslimp.phys) +
		hbq_entry_index * sizeof(struct lpfc_hbq_entry);
	hbqmb->hbqaddrHigh = putPaddrHigh(phba->hbqslimp.phys);

	mb->mbxCommand = MBX_CONFIG_HBQ;
	mb->mbxOwner = OWN_HOST;

				/* Copy info for profiles 2,3,5. Other
				 * profiles this area is reserved
				 */
	if (hbq_desc->profile == 2)
		lpfc_build_hbq_profile2(hbqmb, hbq_desc);
	else if (hbq_desc->profile == 3)
		lpfc_build_hbq_profile3(hbqmb, hbq_desc);
	else if (hbq_desc->profile == 5)
		lpfc_build_hbq_profile5(hbqmb, hbq_desc);

	/* Return if no rctl / type masks for this HBQ */
	if (!hbq_desc->mask_count)
		return;

	/* Otherwise we setup specific rctl / type masks for this HBQ */
	for (i = 0; i < hbq_desc->mask_count; i++) {
		hbqmb->hbqMasks[i].tmatch = hbq_desc->hbqMasks[i].tmatch;
		hbqmb->hbqMasks[i].tmask  = hbq_desc->hbqMasks[i].tmask;
		hbqmb->hbqMasks[i].rctlmatch = hbq_desc->hbqMasks[i].rctlmatch;
		hbqmb->hbqMasks[i].rctlmask  = hbq_desc->hbqMasks[i].rctlmask;
	}

	return;
}



void
lpfc_config_ring(struct lpfc_hba * phba, int ring, LPFC_MBOXQ_t * pmb)
{
	int i;
	MAILBOX_t *mb = &pmb->mb;
	struct lpfc_sli *psli;
	struct lpfc_sli_ring *pring;

	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	mb->un.varCfgRing.ring = ring;
	mb->un.varCfgRing.maxOrigXchg = 0;
	mb->un.varCfgRing.maxRespXchg = 0;
	mb->un.varCfgRing.recvNotify = 1;

	psli = &phba->sli;
	pring = &psli->ring[ring];
	mb->un.varCfgRing.numMask = pring->num_mask;
	mb->mbxCommand = MBX_CONFIG_RING;
	mb->mbxOwner = OWN_HOST;

	/* Is this ring configured for a specific profile */
	if (pring->prt[0].profile) {
		mb->un.varCfgRing.profile = pring->prt[0].profile;
		return;
	}

	/* Otherwise we setup specific rctl / type masks for this ring */
	for (i = 0; i < pring->num_mask; i++) {
		mb->un.varCfgRing.rrRegs[i].rval = pring->prt[i].rctl;
		if (mb->un.varCfgRing.rrRegs[i].rval != FC_ELS_REQ)
			mb->un.varCfgRing.rrRegs[i].rmask = 0xff;
		else
			mb->un.varCfgRing.rrRegs[i].rmask = 0xfe;
		mb->un.varCfgRing.rrRegs[i].tval = pring->prt[i].type;
		mb->un.varCfgRing.rrRegs[i].tmask = 0xff;
	}

	return;
}

void
lpfc_config_port(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmb)
{
	MAILBOX_t __iomem *mb_slim = (MAILBOX_t __iomem *) phba->MBslimaddr;
	MAILBOX_t *mb = &pmb->mb;
	dma_addr_t pdma_addr;
	uint32_t bar_low, bar_high;
	size_t offset;
	struct lpfc_hgp hgp;
	int i;
	uint32_t pgp_offset;

	memset(pmb, 0, sizeof(LPFC_MBOXQ_t));
	mb->mbxCommand = MBX_CONFIG_PORT;
	mb->mbxOwner = OWN_HOST;

	mb->un.varCfgPort.pcbLen = sizeof(PCB_t);

	offset = (uint8_t *)&phba->slim2p->pcb - (uint8_t *)phba->slim2p;
	pdma_addr = phba->slim2p_mapping + offset;
	mb->un.varCfgPort.pcbLow = putPaddrLow(pdma_addr);
	mb->un.varCfgPort.pcbHigh = putPaddrHigh(pdma_addr);

	/* If HBA supports SLI=3 ask for it */

	if (phba->sli_rev == 3 && phba->vpd.sli3Feat.cerbm) {
		mb->un.varCfgPort.cerbm = 1; /* Request HBQs */
		mb->un.varCfgPort.max_hbq = 1; /* Requesting 2 HBQs */
		if (phba->max_vpi && phba->cfg_npiv_enable &&
		    phba->vpd.sli3Feat.cmv) {
			mb->un.varCfgPort.max_vpi = phba->max_vpi;
			mb->un.varCfgPort.cmv = 1;
			phba->sli3_options |= LPFC_SLI3_NPIV_ENABLED;
		} else
			mb->un.varCfgPort.max_vpi = phba->max_vpi = 0;
	} else
		phba->sli_rev = 2;
	mb->un.varCfgPort.sli_mode = phba->sli_rev;

	/* Now setup pcb */
	phba->slim2p->pcb.type = TYPE_NATIVE_SLI2;
	phba->slim2p->pcb.feature = FEATURE_INITIAL_SLI2;

	/* Setup Mailbox pointers */
	phba->slim2p->pcb.mailBoxSize = offsetof(MAILBOX_t, us) +
		sizeof(struct sli2_desc);
	offset = (uint8_t *)&phba->slim2p->mbx - (uint8_t *)phba->slim2p;
	pdma_addr = phba->slim2p_mapping + offset;
	phba->slim2p->pcb.mbAddrHigh = putPaddrHigh(pdma_addr);
	phba->slim2p->pcb.mbAddrLow = putPaddrLow(pdma_addr);

	/*
	 * Setup Host Group ring pointer.
	 *
	 * For efficiency reasons, the ring get/put pointers can be
	 * placed in adapter memory (SLIM) rather than in host memory.
	 * This allows firmware to avoid PCI reads/writes when updating
	 * and checking pointers.
	 *
	 * The firmware recognizes the use of SLIM memory by comparing
	 * the address of the get/put pointers structure with that of
	 * the SLIM BAR (BAR0).
	 *
	 * Caution: be sure to use the PCI config space value of BAR0/BAR1
	 * (the hardware's view of the base address), not the OS's
	 * value of pci_resource_start() as the OS value may be a cookie
	 * for ioremap/iomap.
	 */


	pci_read_config_dword(phba->pcidev, PCI_BASE_ADDRESS_0, &bar_low);
	pci_read_config_dword(phba->pcidev, PCI_BASE_ADDRESS_1, &bar_high);

	/*
	 * Set up HGP - Port Memory
	 *
	 * The port expects the host get/put pointers to reside in memory
	 * following the "non-diagnostic" mode mailbox (32 words, 0x80 bytes)
	 * area of SLIM.  In SLI-2 mode, there's an additional 16 reserved
	 * words (0x40 bytes).  This area is not reserved if HBQs are
	 * configured in SLI-3.
	 *
	 * CR0Put    - SLI2(no HBQs) = 0xc0, With HBQs = 0x80
	 * RR0Get                      0xc4              0x84
	 * CR1Put                      0xc8              0x88
	 * RR1Get                      0xcc              0x8c
	 * CR2Put                      0xd0              0x90
	 * RR2Get                      0xd4              0x94
	 * CR3Put                      0xd8              0x98
	 * RR3Get                      0xdc              0x9c
	 *
	 * Reserved                    0xa0-0xbf
	 *    If HBQs configured:
	 *                         HBQ 0 Put ptr  0xc0
	 *                         HBQ 1 Put ptr  0xc4
	 *                         HBQ 2 Put ptr  0xc8
	 *                         ......
	 *                         HBQ(M-1)Put Pointer 0xc0+(M-1)*4
	 *
	 */

	if (phba->sli_rev == 3) {
		phba->host_gp = &mb_slim->us.s3.host[0];
		phba->hbq_put = &mb_slim->us.s3.hbq_put[0];
	} else {
		phba->host_gp = &mb_slim->us.s2.host[0];
		phba->hbq_put = NULL;
	}

	/* mask off BAR0's flag bits 0 - 3 */
	phba->slim2p->pcb.hgpAddrLow = (bar_low & PCI_BASE_ADDRESS_MEM_MASK) +
		(void __iomem *) phba->host_gp -
		(void __iomem *)phba->MBslimaddr;
	if (bar_low & PCI_BASE_ADDRESS_MEM_TYPE_64)
		phba->slim2p->pcb.hgpAddrHigh = bar_high;
	else
		phba->slim2p->pcb.hgpAddrHigh = 0;
	/* write HGP data to SLIM at the required longword offset */
	memset(&hgp, 0, sizeof(struct lpfc_hgp));

	for (i=0; i < phba->sli.num_rings; i++) {
		lpfc_memcpy_to_slim(phba->host_gp + i, &hgp,
				    sizeof(*phba->host_gp));
	}

	/* Setup Port Group ring pointer */
	if (phba->sli_rev == 3)
		pgp_offset = (uint8_t *)&phba->slim2p->mbx.us.s3_pgp.port -
			(uint8_t *)phba->slim2p;
	else
		pgp_offset = (uint8_t *)&phba->slim2p->mbx.us.s2.port -
			(uint8_t *)phba->slim2p;

	pdma_addr = phba->slim2p_mapping + pgp_offset;
	phba->slim2p->pcb.pgpAddrHigh = putPaddrHigh(pdma_addr);
	phba->slim2p->pcb.pgpAddrLow = putPaddrLow(pdma_addr);
	phba->hbq_get = &phba->slim2p->mbx.us.s3_pgp.hbq_get[0];

	/* Use callback routine to setp rings in the pcb */
	lpfc_config_pcb_setup(phba);

	/* special handling for LC HBAs */
	if (lpfc_is_LC_HBA(phba->pcidev->device)) {
		uint32_t hbainit[5];

		lpfc_hba_init(phba, hbainit);

		memcpy(&mb->un.varCfgPort.hbainit, hbainit, 20);
	}

	/* Swap PCB if needed */
	lpfc_sli_pcimem_bcopy(&phba->slim2p->pcb, &phba->slim2p->pcb,
			      sizeof(PCB_t));
}

void
lpfc_kill_board(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb)
{
	MAILBOX_t *mb = &pmb->mb;

	memset(pmb, 0, sizeof(LPFC_MBOXQ_t));
	mb->mbxCommand = MBX_KILL_BOARD;
	mb->mbxOwner = OWN_HOST;
	return;
}

void
lpfc_mbox_put(struct lpfc_hba * phba, LPFC_MBOXQ_t * mbq)
{
	struct lpfc_sli *psli;

	psli = &phba->sli;

	list_add_tail(&mbq->list, &psli->mboxq);

	psli->mboxq_cnt++;

	return;
}

LPFC_MBOXQ_t *
lpfc_mbox_get(struct lpfc_hba * phba)
{
	LPFC_MBOXQ_t *mbq = NULL;
	struct lpfc_sli *psli = &phba->sli;

	list_remove_head((&psli->mboxq), mbq, LPFC_MBOXQ_t, list);
	if (mbq)
		psli->mboxq_cnt--;

	return mbq;
}

void
lpfc_mbox_cmpl_put(struct lpfc_hba * phba, LPFC_MBOXQ_t * mbq)
{
	/* This function expects to be called from interupt context */
	spin_lock(&phba->hbalock);
	list_add_tail(&mbq->list, &phba->sli.mboxq_cmpl);
	spin_unlock(&phba->hbalock);
	return;
}

int
lpfc_mbox_tmo_val(struct lpfc_hba *phba, int cmd)
{
	switch (cmd) {
	case MBX_WRITE_NV:	/* 0x03 */
	case MBX_UPDATE_CFG:	/* 0x1B */
	case MBX_DOWN_LOAD:	/* 0x1C */
	case MBX_DEL_LD_ENTRY:	/* 0x1D */
	case MBX_LOAD_AREA:	/* 0x81 */
	case MBX_FLASH_WR_ULA:  /* 0x98 */
	case MBX_LOAD_EXP_ROM:	/* 0x9C */
		return LPFC_MBOX_TMO_FLASH_CMD;
	}
	return LPFC_MBOX_TMO;
}
