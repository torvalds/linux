/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2004-2005 Emulex.  All rights reserved.           *
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

	mb->un.varCfgLnk.myId = phba->fc_myDID;
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
	}

	/* NEW_FEATURE
	 * Setting up the link speed
	 */
	vpd = &phba->vpd;
	if (vpd->rev.feaLevelHigh >= 0x02){
		switch(linkspeed){
			case LINK_SPEED_1G:
			case LINK_SPEED_2G:
			case LINK_SPEED_4G:
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
lpfc_read_sparam(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb)
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
		if (mp)
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

	/* save address for completion */
	pmb->context1 = mp;

	return (0);
}

/********************************************/
/*  lpfc_unreg_did  Issue a UNREG_DID       */
/*                  mailbox command         */
/********************************************/
void
lpfc_unreg_did(struct lpfc_hba * phba, uint32_t did, LPFC_MBOXQ_t * pmb)
{
	MAILBOX_t *mb;

	mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	mb->un.varUnregDID.did = did;

	mb->mbxCommand = MBX_UNREG_D_ID;
	mb->mbxOwner = OWN_HOST;
	return;
}

/***********************************************/

/*                  command to write slim      */
/***********************************************/
void
lpfc_set_slim(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb, uint32_t addr,
	      uint32_t value)
{
	MAILBOX_t *mb;

	mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	/* addr = 0x090597 is AUTO ABTS disable for ELS commands */
	/* addr = 0x052198 is DELAYED ABTS enable for ELS commands */

	/*
	 * Always turn on DELAYED ABTS for ELS timeouts
	 */
	if ((addr == 0x052198) && (value == 0))
		value = 1;

	mb->un.varWords[0] = addr;
	mb->un.varWords[1] = value;

	mb->mbxCommand = MBX_SET_SLIM;
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

/********************************************/
/*  lpfc_reg_login  Issue a REG_LOGIN       */
/*                  mailbox command         */
/********************************************/
int
lpfc_reg_login(struct lpfc_hba * phba,
	       uint32_t did, uint8_t * param, LPFC_MBOXQ_t * pmb, uint32_t flag)
{
	uint8_t *sparam;
	struct lpfc_dmabuf *mp;
	MAILBOX_t *mb;
	struct lpfc_sli *psli;

	psli = &phba->sli;
	mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	mb->un.varRegLogin.rpi = 0;
	mb->un.varRegLogin.did = did;
	mb->un.varWords[30] = flag;	/* Set flag to issue action on cmpl */

	mb->mbxOwner = OWN_HOST;

	/* Get a buffer to hold NPorts Service Parameters */
	if (((mp = kmalloc(sizeof (struct lpfc_dmabuf), GFP_KERNEL)) == NULL) ||
	    ((mp->virt = lpfc_mbuf_alloc(phba, 0, &(mp->phys))) == 0)) {
		if (mp)
			kfree(mp);

		mb->mbxCommand = MBX_REG_LOGIN64;
		/* REG_LOGIN: no buffers */
		lpfc_printf_log(phba,
			       KERN_WARNING,
			       LOG_MBOX,
			       "%d:0302 REG_LOGIN: no buffers Data x%x x%x\n",
			       phba->brd_no,
			       (uint32_t) did, (uint32_t) flag);
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
lpfc_unreg_login(struct lpfc_hba * phba, uint32_t rpi, LPFC_MBOXQ_t * pmb)
{
	MAILBOX_t *mb;

	mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	mb->un.varUnregLogin.rpi = (uint16_t) rpi;
	mb->un.varUnregLogin.rsvd1 = 0;

	mb->mbxCommand = MBX_UNREG_LOGIN;
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
	uint32_t iocbCnt;
	int i;

	pcbp->maxRing = (psli->num_rings - 1);

	iocbCnt = 0;
	for (i = 0; i < psli->num_rings; i++) {
		pring = &psli->ring[i];
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
		pring->cmdringaddr =
		    (void *)&phba->slim2p->IOCBs[iocbCnt];
		pcbp->rdsc[i].cmdEntries = pring->numCiocb;

		offset = (uint8_t *)&phba->slim2p->IOCBs[iocbCnt] -
			 (uint8_t *)phba->slim2p;
		pdma_addr = phba->slim2p_mapping + offset;
		pcbp->rdsc[i].cmdAddrHigh = putPaddrHigh(pdma_addr);
		pcbp->rdsc[i].cmdAddrLow = putPaddrLow(pdma_addr);
		iocbCnt += pring->numCiocb;

		/* Response ring setup for ring */
		pring->rspringaddr =
		    (void *)&phba->slim2p->IOCBs[iocbCnt];

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
	MAILBOX_t *mb;

	mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));
	mb->un.varRdRev.cv = 1;
	mb->mbxCommand = MBX_READ_REV;
	mb->mbxOwner = OWN_HOST;
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
lpfc_config_port(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb)
{
	MAILBOX_t *mb = &pmb->mb;
	dma_addr_t pdma_addr;
	uint32_t bar_low, bar_high;
	size_t offset;
	struct lpfc_hgp hgp;
	void __iomem *to_slim;

	memset(pmb, 0, sizeof(LPFC_MBOXQ_t));
	mb->mbxCommand = MBX_CONFIG_PORT;
	mb->mbxOwner = OWN_HOST;

	mb->un.varCfgPort.pcbLen = sizeof(PCB_t);

	offset = (uint8_t *)&phba->slim2p->pcb - (uint8_t *)phba->slim2p;
	pdma_addr = phba->slim2p_mapping + offset;
	mb->un.varCfgPort.pcbLow = putPaddrLow(pdma_addr);
	mb->un.varCfgPort.pcbHigh = putPaddrHigh(pdma_addr);

	/* Now setup pcb */
	phba->slim2p->pcb.type = TYPE_NATIVE_SLI2;
	phba->slim2p->pcb.feature = FEATURE_INITIAL_SLI2;

	/* Setup Mailbox pointers */
	phba->slim2p->pcb.mailBoxSize = sizeof(MAILBOX_t);
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


	/* mask off BAR0's flag bits 0 - 3 */
	phba->slim2p->pcb.hgpAddrLow = (bar_low & PCI_BASE_ADDRESS_MEM_MASK) +
					(SLIMOFF*sizeof(uint32_t));
	if (bar_low & PCI_BASE_ADDRESS_MEM_TYPE_64)
		phba->slim2p->pcb.hgpAddrHigh = bar_high;
	else
		phba->slim2p->pcb.hgpAddrHigh = 0;
	/* write HGP data to SLIM at the required longword offset */
	memset(&hgp, 0, sizeof(struct lpfc_hgp));
	to_slim = phba->MBslimaddr + (SLIMOFF*sizeof (uint32_t));
	lpfc_memcpy_to_slim(to_slim, &hgp, sizeof(struct lpfc_hgp));

	/* Setup Port Group ring pointer */
	offset = (uint8_t *)&phba->slim2p->mbx.us.s2.port -
		 (uint8_t *)phba->slim2p;
	pdma_addr = phba->slim2p_mapping + offset;
	phba->slim2p->pcb.pgpAddrHigh = putPaddrHigh(pdma_addr);
	phba->slim2p->pcb.pgpAddrLow = putPaddrLow(pdma_addr);

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
								sizeof (PCB_t));

	lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
		        "%d:0405 Service Level Interface (SLI) 2 selected\n",
		        phba->brd_no);
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

	list_remove_head((&psli->mboxq), mbq, LPFC_MBOXQ_t,
			 list);
	if (mbq) {
		psli->mboxq_cnt--;
	}

	return mbq;
}
