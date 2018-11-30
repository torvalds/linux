/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2017-2018 Broadcom. All Rights Reserved. The term *
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

#include <scsi/scsi_device.h>
#include <scsi/scsi_transport_fc.h>
#include <scsi/scsi.h>
#include <scsi/fc/fc_fs.h>

#include "lpfc_hw4.h"
#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_sli4.h"
#include "lpfc_nl.h"
#include "lpfc_disc.h"
#include "lpfc_scsi.h"
#include "lpfc.h"
#include "lpfc_logmsg.h"
#include "lpfc_crtn.h"
#include "lpfc_compat.h"

/**
 * lpfc_dump_static_vport - Dump HBA's static vport information.
 * @phba: pointer to lpfc hba data structure.
 * @pmb: pointer to the driver internal queue element for mailbox command.
 * @offset: offset for dumping vport info.
 *
 * The dump mailbox command provides a method for the device driver to obtain
 * various types of information from the HBA device.
 *
 * This routine prepares the mailbox command for dumping list of static
 * vports to be created.
 **/
int
lpfc_dump_static_vport(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmb,
		uint16_t offset)
{
	MAILBOX_t *mb;
	struct lpfc_dmabuf *mp;

	mb = &pmb->u.mb;

	/* Setup to dump vport info region */
	memset(pmb, 0, sizeof(LPFC_MBOXQ_t));
	mb->mbxCommand = MBX_DUMP_MEMORY;
	mb->un.varDmp.type = DMP_NV_PARAMS;
	mb->un.varDmp.entry_index = offset;
	mb->un.varDmp.region_id = DMP_REGION_VPORT;
	mb->mbxOwner = OWN_HOST;

	/* For SLI3 HBAs data is embedded in mailbox */
	if (phba->sli_rev != LPFC_SLI_REV4) {
		mb->un.varDmp.cv = 1;
		mb->un.varDmp.word_cnt = DMP_RSP_SIZE/sizeof(uint32_t);
		return 0;
	}

	/* For SLI4 HBAs driver need to allocate memory */
	mp = kmalloc(sizeof(struct lpfc_dmabuf), GFP_KERNEL);
	if (mp)
		mp->virt = lpfc_mbuf_alloc(phba, 0, &mp->phys);

	if (!mp || !mp->virt) {
		kfree(mp);
		lpfc_printf_log(phba, KERN_ERR, LOG_MBOX,
			"2605 lpfc_dump_static_vport: memory"
			" allocation failed\n");
		return 1;
	}
	memset(mp->virt, 0, LPFC_BPL_SIZE);
	INIT_LIST_HEAD(&mp->list);
	/* save address for completion */
	pmb->ctx_buf = (uint8_t *)mp;
	mb->un.varWords[3] = putPaddrLow(mp->phys);
	mb->un.varWords[4] = putPaddrHigh(mp->phys);
	mb->un.varDmp.sli4_length = sizeof(struct static_vport_info);

	return 0;
}

/**
 * lpfc_down_link - Bring down HBAs link.
 * @phba: pointer to lpfc hba data structure.
 * @pmb: pointer to the driver internal queue element for mailbox command.
 *
 * This routine prepares a mailbox command to bring down HBA link.
 **/
void
lpfc_down_link(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmb)
{
	MAILBOX_t *mb;
	memset(pmb, 0, sizeof(LPFC_MBOXQ_t));
	mb = &pmb->u.mb;
	mb->mbxCommand = MBX_DOWN_LINK;
	mb->mbxOwner = OWN_HOST;
}

/**
 * lpfc_dump_mem - Prepare a mailbox command for reading a region.
 * @phba: pointer to lpfc hba data structure.
 * @pmb: pointer to the driver internal queue element for mailbox command.
 * @offset: offset into the region.
 * @region_id: config region id.
 *
 * The dump mailbox command provides a method for the device driver to obtain
 * various types of information from the HBA device.
 *
 * This routine prepares the mailbox command for dumping HBA's config region.
 **/
void
lpfc_dump_mem(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmb, uint16_t offset,
		uint16_t region_id)
{
	MAILBOX_t *mb;
	void *ctx;

	mb = &pmb->u.mb;
	ctx = pmb->ctx_buf;

	/* Setup to dump VPD region */
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));
	mb->mbxCommand = MBX_DUMP_MEMORY;
	mb->un.varDmp.cv = 1;
	mb->un.varDmp.type = DMP_NV_PARAMS;
	mb->un.varDmp.entry_index = offset;
	mb->un.varDmp.region_id = region_id;
	mb->un.varDmp.word_cnt = (DMP_RSP_SIZE / sizeof (uint32_t));
	mb->un.varDmp.co = 0;
	mb->un.varDmp.resp_offset = 0;
	pmb->ctx_buf = ctx;
	mb->mbxOwner = OWN_HOST;
	return;
}

/**
 * lpfc_dump_wakeup_param - Prepare mailbox command for retrieving wakeup params
 * @phba: pointer to lpfc hba data structure.
 * @pmb: pointer to the driver internal queue element for mailbox command.
 *
 * This function create a dump memory mailbox command to dump wake up
 * parameters.
 */
void
lpfc_dump_wakeup_param(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmb)
{
	MAILBOX_t *mb;
	void *ctx;

	mb = &pmb->u.mb;
	/* Save context so that we can restore after memset */
	ctx = pmb->ctx_buf;

	/* Setup to dump VPD region */
	memset(pmb, 0, sizeof(LPFC_MBOXQ_t));
	mb->mbxCommand = MBX_DUMP_MEMORY;
	mb->mbxOwner = OWN_HOST;
	mb->un.varDmp.cv = 1;
	mb->un.varDmp.type = DMP_NV_PARAMS;
	if (phba->sli_rev < LPFC_SLI_REV4)
		mb->un.varDmp.entry_index = 0;
	mb->un.varDmp.region_id = WAKE_UP_PARMS_REGION_ID;
	mb->un.varDmp.word_cnt = WAKE_UP_PARMS_WORD_SIZE;
	mb->un.varDmp.co = 0;
	mb->un.varDmp.resp_offset = 0;
	pmb->ctx_buf = ctx;
	return;
}

/**
 * lpfc_read_nv - Prepare a mailbox command for reading HBA's NVRAM param
 * @phba: pointer to lpfc hba data structure.
 * @pmb: pointer to the driver internal queue element for mailbox command.
 *
 * The read NVRAM mailbox command returns the HBA's non-volatile parameters
 * that are used as defaults when the Fibre Channel link is brought on-line.
 *
 * This routine prepares the mailbox command for reading information stored
 * in the HBA's NVRAM. Specifically, the HBA's WWNN and WWPN.
 **/
void
lpfc_read_nv(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb)
{
	MAILBOX_t *mb;

	mb = &pmb->u.mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));
	mb->mbxCommand = MBX_READ_NV;
	mb->mbxOwner = OWN_HOST;
	return;
}

/**
 * lpfc_config_async - Prepare a mailbox command for enabling HBA async event
 * @phba: pointer to lpfc hba data structure.
 * @pmb: pointer to the driver internal queue element for mailbox command.
 * @ring: ring number for the asynchronous event to be configured.
 *
 * The asynchronous event enable mailbox command is used to enable the
 * asynchronous event posting via the ASYNC_STATUS_CN IOCB response and
 * specifies the default ring to which events are posted.
 *
 * This routine prepares the mailbox command for enabling HBA asynchronous
 * event support on a IOCB ring.
 **/
void
lpfc_config_async(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb,
		uint32_t ring)
{
	MAILBOX_t *mb;

	mb = &pmb->u.mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));
	mb->mbxCommand = MBX_ASYNCEVT_ENABLE;
	mb->un.varCfgAsyncEvent.ring = ring;
	mb->mbxOwner = OWN_HOST;
	return;
}

/**
 * lpfc_heart_beat - Prepare a mailbox command for heart beat
 * @phba: pointer to lpfc hba data structure.
 * @pmb: pointer to the driver internal queue element for mailbox command.
 *
 * The heart beat mailbox command is used to detect an unresponsive HBA, which
 * is defined as any device where no error attention is sent and both mailbox
 * and rings are not processed.
 *
 * This routine prepares the mailbox command for issuing a heart beat in the
 * form of mailbox command to the HBA. The timely completion of the heart
 * beat mailbox command indicates the health of the HBA.
 **/
void
lpfc_heart_beat(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb)
{
	MAILBOX_t *mb;

	mb = &pmb->u.mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));
	mb->mbxCommand = MBX_HEARTBEAT;
	mb->mbxOwner = OWN_HOST;
	return;
}

/**
 * lpfc_read_topology - Prepare a mailbox command for reading HBA topology
 * @phba: pointer to lpfc hba data structure.
 * @pmb: pointer to the driver internal queue element for mailbox command.
 * @mp: DMA buffer memory for reading the link attention information into.
 *
 * The read topology mailbox command is issued to read the link topology
 * information indicated by the HBA port when the Link Event bit of the Host
 * Attention (HSTATT) register is set to 1 (For SLI-3) or when an FC Link
 * Attention ACQE is received from the port (For SLI-4). A Link Event
 * Attention occurs based on an exception detected at the Fibre Channel link
 * interface.
 *
 * This routine prepares the mailbox command for reading HBA link topology
 * information. A DMA memory has been set aside and address passed to the
 * HBA through @mp for the HBA to DMA link attention information into the
 * memory as part of the execution of the mailbox command.
 *
 * Return codes
 *    0 - Success (currently always return 0)
 **/
int
lpfc_read_topology(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmb,
		   struct lpfc_dmabuf *mp)
{
	MAILBOX_t *mb;

	mb = &pmb->u.mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	INIT_LIST_HEAD(&mp->list);
	mb->mbxCommand = MBX_READ_TOPOLOGY;
	mb->un.varReadTop.lilpBde64.tus.f.bdeSize = LPFC_ALPA_MAP_SIZE;
	mb->un.varReadTop.lilpBde64.addrHigh = putPaddrHigh(mp->phys);
	mb->un.varReadTop.lilpBde64.addrLow = putPaddrLow(mp->phys);

	/* Save address for later completion and set the owner to host so that
	 * the FW knows this mailbox is available for processing.
	 */
	pmb->ctx_buf = (uint8_t *)mp;
	mb->mbxOwner = OWN_HOST;
	return (0);
}

/**
 * lpfc_clear_la - Prepare a mailbox command for clearing HBA link attention
 * @phba: pointer to lpfc hba data structure.
 * @pmb: pointer to the driver internal queue element for mailbox command.
 *
 * The clear link attention mailbox command is issued to clear the link event
 * attention condition indicated by the Link Event bit of the Host Attention
 * (HSTATT) register. The link event attention condition is cleared only if
 * the event tag specified matches that of the current link event counter.
 * The current event tag is read using the read link attention event mailbox
 * command.
 *
 * This routine prepares the mailbox command for clearing HBA link attention
 * information.
 **/
void
lpfc_clear_la(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb)
{
	MAILBOX_t *mb;

	mb = &pmb->u.mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	mb->un.varClearLA.eventTag = phba->fc_eventTag;
	mb->mbxCommand = MBX_CLEAR_LA;
	mb->mbxOwner = OWN_HOST;
	return;
}

/**
 * lpfc_config_link - Prepare a mailbox command for configuring link on a HBA
 * @phba: pointer to lpfc hba data structure.
 * @pmb: pointer to the driver internal queue element for mailbox command.
 *
 * The configure link mailbox command is used before the initialize link
 * mailbox command to override default value and to configure link-oriented
 * parameters such as DID address and various timers. Typically, this
 * command would be used after an F_Port login to set the returned DID address
 * and the fabric timeout values. This command is not valid before a configure
 * port command has configured the HBA port.
 *
 * This routine prepares the mailbox command for configuring link on a HBA.
 **/
void
lpfc_config_link(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb)
{
	struct lpfc_vport  *vport = phba->pport;
	MAILBOX_t *mb = &pmb->u.mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	/* NEW_FEATURE
	 * SLI-2, Coalescing Response Feature.
	 */
	if (phba->cfg_cr_delay && (phba->sli_rev < LPFC_SLI_REV4)) {
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
	mb->un.varCfgLnk.cscn = 0;
	if (phba->bbcredit_support && phba->cfg_enable_bbcr) {
		mb->un.varCfgLnk.cscn = 1;
		mb->un.varCfgLnk.bbscn = bf_get(lpfc_bbscn_def,
						 &phba->sli4_hba.bbscn_params);
	}

	if (phba->cfg_ack0 && (phba->sli_rev < LPFC_SLI_REV4))
		mb->un.varCfgLnk.ack0_enable = 1;

	mb->mbxCommand = MBX_CONFIG_LINK;
	mb->mbxOwner = OWN_HOST;
	return;
}

/**
 * lpfc_config_msi - Prepare a mailbox command for configuring msi-x
 * @phba: pointer to lpfc hba data structure.
 * @pmb: pointer to the driver internal queue element for mailbox command.
 *
 * The configure MSI-X mailbox command is used to configure the HBA's SLI-3
 * MSI-X multi-message interrupt vector association to interrupt attention
 * conditions.
 *
 * Return codes
 *    0 - Success
 *    -EINVAL - Failure
 **/
int
lpfc_config_msi(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmb)
{
	MAILBOX_t *mb = &pmb->u.mb;
	uint32_t attentionConditions[2];

	/* Sanity check */
	if (phba->cfg_use_msi != 2) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0475 Not configured for supporting MSI-X "
				"cfg_use_msi: 0x%x\n", phba->cfg_use_msi);
		return -EINVAL;
	}

	if (phba->sli_rev < 3) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0476 HBA not supporting SLI-3 or later "
				"SLI Revision: 0x%x\n", phba->sli_rev);
		return -EINVAL;
	}

	/* Clear mailbox command fields */
	memset(pmb, 0, sizeof(LPFC_MBOXQ_t));

	/*
	 * SLI-3, Message Signaled Interrupt Fearure.
	 */

	/* Multi-message attention configuration */
	attentionConditions[0] = (HA_R0ATT | HA_R1ATT | HA_R2ATT | HA_ERATT |
				  HA_LATT | HA_MBATT);
	attentionConditions[1] = 0;

	mb->un.varCfgMSI.attentionConditions[0] = attentionConditions[0];
	mb->un.varCfgMSI.attentionConditions[1] = attentionConditions[1];

	/*
	 * Set up message number to HA bit association
	 */
#ifdef __BIG_ENDIAN_BITFIELD
	/* RA0 (FCP Ring) */
	mb->un.varCfgMSI.messageNumberByHA[HA_R0_POS] = 1;
	/* RA1 (Other Protocol Extra Ring) */
	mb->un.varCfgMSI.messageNumberByHA[HA_R1_POS] = 1;
#else   /*  __LITTLE_ENDIAN_BITFIELD */
	/* RA0 (FCP Ring) */
	mb->un.varCfgMSI.messageNumberByHA[HA_R0_POS^3] = 1;
	/* RA1 (Other Protocol Extra Ring) */
	mb->un.varCfgMSI.messageNumberByHA[HA_R1_POS^3] = 1;
#endif
	/* Multi-message interrupt autoclear configuration*/
	mb->un.varCfgMSI.autoClearHA[0] = attentionConditions[0];
	mb->un.varCfgMSI.autoClearHA[1] = attentionConditions[1];

	/* For now, HBA autoclear does not work reliably, disable it */
	mb->un.varCfgMSI.autoClearHA[0] = 0;
	mb->un.varCfgMSI.autoClearHA[1] = 0;

	/* Set command and owner bit */
	mb->mbxCommand = MBX_CONFIG_MSI;
	mb->mbxOwner = OWN_HOST;

	return 0;
}

/**
 * lpfc_init_link - Prepare a mailbox command for initialize link on a HBA
 * @phba: pointer to lpfc hba data structure.
 * @pmb: pointer to the driver internal queue element for mailbox command.
 * @topology: the link topology for the link to be initialized to.
 * @linkspeed: the link speed for the link to be initialized to.
 *
 * The initialize link mailbox command is used to initialize the Fibre
 * Channel link. This command must follow a configure port command that
 * establishes the mode of operation.
 *
 * This routine prepares the mailbox command for initializing link on a HBA
 * with the specified link topology and speed.
 **/
void
lpfc_init_link(struct lpfc_hba * phba,
	       LPFC_MBOXQ_t * pmb, uint32_t topology, uint32_t linkspeed)
{
	lpfc_vpd_t *vpd;
	MAILBOX_t *mb;

	mb = &pmb->u.mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

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

	if (phba->pcidev->device == PCI_DEVICE_ID_LANCER_G6_FC &&
		mb->un.varInitLnk.link_flags & FLAGS_TOPOLOGY_MODE_LOOP) {
		/* Failover is not tried for Lancer G6 */
		mb->un.varInitLnk.link_flags = FLAGS_TOPOLOGY_MODE_PT_PT;
		phba->cfg_topology = FLAGS_TOPOLOGY_MODE_PT_PT;
	}

	/* Enable asynchronous ABTS responses from firmware */
	mb->un.varInitLnk.link_flags |= FLAGS_IMED_ABORT;

	/* NEW_FEATURE
	 * Setting up the link speed
	 */
	vpd = &phba->vpd;
	if (vpd->rev.feaLevelHigh >= 0x02){
		switch(linkspeed){
		case LPFC_USER_LINK_SPEED_1G:
			mb->un.varInitLnk.link_flags |= FLAGS_LINK_SPEED;
			mb->un.varInitLnk.link_speed = LINK_SPEED_1G;
			break;
		case LPFC_USER_LINK_SPEED_2G:
			mb->un.varInitLnk.link_flags |=	FLAGS_LINK_SPEED;
			mb->un.varInitLnk.link_speed = LINK_SPEED_2G;
			break;
		case LPFC_USER_LINK_SPEED_4G:
			mb->un.varInitLnk.link_flags |=	FLAGS_LINK_SPEED;
			mb->un.varInitLnk.link_speed = LINK_SPEED_4G;
			break;
		case LPFC_USER_LINK_SPEED_8G:
			mb->un.varInitLnk.link_flags |=	FLAGS_LINK_SPEED;
			mb->un.varInitLnk.link_speed = LINK_SPEED_8G;
			break;
		case LPFC_USER_LINK_SPEED_10G:
			mb->un.varInitLnk.link_flags |=	FLAGS_LINK_SPEED;
			mb->un.varInitLnk.link_speed = LINK_SPEED_10G;
			break;
		case LPFC_USER_LINK_SPEED_16G:
			mb->un.varInitLnk.link_flags |=	FLAGS_LINK_SPEED;
			mb->un.varInitLnk.link_speed = LINK_SPEED_16G;
			break;
		case LPFC_USER_LINK_SPEED_32G:
			mb->un.varInitLnk.link_flags |= FLAGS_LINK_SPEED;
			mb->un.varInitLnk.link_speed = LINK_SPEED_32G;
			break;
		case LPFC_USER_LINK_SPEED_64G:
			mb->un.varInitLnk.link_flags |= FLAGS_LINK_SPEED;
			mb->un.varInitLnk.link_speed = LINK_SPEED_64G;
			break;
		case LPFC_USER_LINK_SPEED_AUTO:
		default:
			mb->un.varInitLnk.link_speed = LINK_SPEED_AUTO;
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

/**
 * lpfc_read_sparam - Prepare a mailbox command for reading HBA parameters
 * @phba: pointer to lpfc hba data structure.
 * @pmb: pointer to the driver internal queue element for mailbox command.
 * @vpi: virtual N_Port identifier.
 *
 * The read service parameter mailbox command is used to read the HBA port
 * service parameters. The service parameters are read into the buffer
 * specified directly by a BDE in the mailbox command. These service
 * parameters may then be used to build the payload of an N_Port/F_POrt
 * login request and reply (LOGI/ACC).
 *
 * This routine prepares the mailbox command for reading HBA port service
 * parameters. The DMA memory is allocated in this function and the addresses
 * are populated into the mailbox command for the HBA to DMA the service
 * parameters into.
 *
 * Return codes
 *    0 - Success
 *    1 - DMA memory allocation failed
 **/
int
lpfc_read_sparam(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmb, int vpi)
{
	struct lpfc_dmabuf *mp;
	MAILBOX_t *mb;

	mb = &pmb->u.mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	mb->mbxOwner = OWN_HOST;

	/* Get a buffer to hold the HBAs Service Parameters */

	mp = kmalloc(sizeof (struct lpfc_dmabuf), GFP_KERNEL);
	if (mp)
		mp->virt = lpfc_mbuf_alloc(phba, 0, &mp->phys);
	if (!mp || !mp->virt) {
		kfree(mp);
		mb->mbxCommand = MBX_READ_SPARM64;
		/* READ_SPARAM: no buffers */
		lpfc_printf_log(phba, KERN_WARNING, LOG_MBOX,
			        "0301 READ_SPARAM: no buffers\n");
		return (1);
	}
	INIT_LIST_HEAD(&mp->list);
	mb->mbxCommand = MBX_READ_SPARM64;
	mb->un.varRdSparm.un.sp64.tus.f.bdeSize = sizeof (struct serv_parm);
	mb->un.varRdSparm.un.sp64.addrHigh = putPaddrHigh(mp->phys);
	mb->un.varRdSparm.un.sp64.addrLow = putPaddrLow(mp->phys);
	if (phba->sli_rev >= LPFC_SLI_REV3)
		mb->un.varRdSparm.vpi = phba->vpi_ids[vpi];

	/* save address for completion */
	pmb->ctx_buf = mp;

	return (0);
}

/**
 * lpfc_unreg_did - Prepare a mailbox command for unregistering DID
 * @phba: pointer to lpfc hba data structure.
 * @vpi: virtual N_Port identifier.
 * @did: remote port identifier.
 * @pmb: pointer to the driver internal queue element for mailbox command.
 *
 * The unregister DID mailbox command is used to unregister an N_Port/F_Port
 * login for an unknown RPI by specifying the DID of a remote port. This
 * command frees an RPI context in the HBA port. This has the effect of
 * performing an implicit N_Port/F_Port logout.
 *
 * This routine prepares the mailbox command for unregistering a remote
 * N_Port/F_Port (DID) login.
 **/
void
lpfc_unreg_did(struct lpfc_hba * phba, uint16_t vpi, uint32_t did,
	       LPFC_MBOXQ_t * pmb)
{
	MAILBOX_t *mb;

	mb = &pmb->u.mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	mb->un.varUnregDID.did = did;
	mb->un.varUnregDID.vpi = vpi;
	if ((vpi != 0xffff) &&
	    (phba->sli_rev == LPFC_SLI_REV4))
		mb->un.varUnregDID.vpi = phba->vpi_ids[vpi];

	mb->mbxCommand = MBX_UNREG_D_ID;
	mb->mbxOwner = OWN_HOST;
	return;
}

/**
 * lpfc_read_config - Prepare a mailbox command for reading HBA configuration
 * @phba: pointer to lpfc hba data structure.
 * @pmb: pointer to the driver internal queue element for mailbox command.
 *
 * The read configuration mailbox command is used to read the HBA port
 * configuration parameters. This mailbox command provides a method for
 * seeing any parameters that may have changed via various configuration
 * mailbox commands.
 *
 * This routine prepares the mailbox command for reading out HBA configuration
 * parameters.
 **/
void
lpfc_read_config(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb)
{
	MAILBOX_t *mb;

	mb = &pmb->u.mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	mb->mbxCommand = MBX_READ_CONFIG;
	mb->mbxOwner = OWN_HOST;
	return;
}

/**
 * lpfc_read_lnk_stat - Prepare a mailbox command for reading HBA link stats
 * @phba: pointer to lpfc hba data structure.
 * @pmb: pointer to the driver internal queue element for mailbox command.
 *
 * The read link status mailbox command is used to read the link status from
 * the HBA. Link status includes all link-related error counters. These
 * counters are maintained by the HBA and originated in the link hardware
 * unit. Note that all of these counters wrap.
 *
 * This routine prepares the mailbox command for reading out HBA link status.
 **/
void
lpfc_read_lnk_stat(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb)
{
	MAILBOX_t *mb;

	mb = &pmb->u.mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	mb->mbxCommand = MBX_READ_LNK_STAT;
	mb->mbxOwner = OWN_HOST;
	return;
}

/**
 * lpfc_reg_rpi - Prepare a mailbox command for registering remote login
 * @phba: pointer to lpfc hba data structure.
 * @vpi: virtual N_Port identifier.
 * @did: remote port identifier.
 * @param: pointer to memory holding the server parameters.
 * @pmb: pointer to the driver internal queue element for mailbox command.
 * @rpi: the rpi to use in the registration (usually only used for SLI4.
 *
 * The registration login mailbox command is used to register an N_Port or
 * F_Port login. This registration allows the HBA to cache the remote N_Port
 * service parameters internally and thereby make the appropriate FC-2
 * decisions. The remote port service parameters are handed off by the driver
 * to the HBA using a descriptor entry that directly identifies a buffer in
 * host memory. In exchange, the HBA returns an RPI identifier.
 *
 * This routine prepares the mailbox command for registering remote port login.
 * The function allocates DMA buffer for passing the service parameters to the
 * HBA with the mailbox command.
 *
 * Return codes
 *    0 - Success
 *    1 - DMA memory allocation failed
 **/
int
lpfc_reg_rpi(struct lpfc_hba *phba, uint16_t vpi, uint32_t did,
	     uint8_t *param, LPFC_MBOXQ_t *pmb, uint16_t rpi)
{
	MAILBOX_t *mb = &pmb->u.mb;
	uint8_t *sparam;
	struct lpfc_dmabuf *mp;

	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	mb->un.varRegLogin.rpi = 0;
	if (phba->sli_rev == LPFC_SLI_REV4)
		mb->un.varRegLogin.rpi = phba->sli4_hba.rpi_ids[rpi];
	if (phba->sli_rev >= LPFC_SLI_REV3)
		mb->un.varRegLogin.vpi = phba->vpi_ids[vpi];
	mb->un.varRegLogin.did = did;
	mb->mbxOwner = OWN_HOST;
	/* Get a buffer to hold NPorts Service Parameters */
	mp = kmalloc(sizeof (struct lpfc_dmabuf), GFP_KERNEL);
	if (mp)
		mp->virt = lpfc_mbuf_alloc(phba, 0, &mp->phys);
	if (!mp || !mp->virt) {
		kfree(mp);
		mb->mbxCommand = MBX_REG_LOGIN64;
		/* REG_LOGIN: no buffers */
		lpfc_printf_log(phba, KERN_WARNING, LOG_MBOX,
				"0302 REG_LOGIN: no buffers, VPI:%d DID:x%x, "
				"rpi x%x\n", vpi, did, rpi);
		return 1;
	}
	INIT_LIST_HEAD(&mp->list);
	sparam = mp->virt;

	/* Copy param's into a new buffer */
	memcpy(sparam, param, sizeof (struct serv_parm));

	/* save address for completion */
	pmb->ctx_buf = (uint8_t *)mp;

	mb->mbxCommand = MBX_REG_LOGIN64;
	mb->un.varRegLogin.un.sp64.tus.f.bdeSize = sizeof (struct serv_parm);
	mb->un.varRegLogin.un.sp64.addrHigh = putPaddrHigh(mp->phys);
	mb->un.varRegLogin.un.sp64.addrLow = putPaddrLow(mp->phys);

	return 0;
}

/**
 * lpfc_unreg_login - Prepare a mailbox command for unregistering remote login
 * @phba: pointer to lpfc hba data structure.
 * @vpi: virtual N_Port identifier.
 * @rpi: remote port identifier
 * @pmb: pointer to the driver internal queue element for mailbox command.
 *
 * The unregistration login mailbox command is used to unregister an N_Port
 * or F_Port login. This command frees an RPI context in the HBA. It has the
 * effect of performing an implicit N_Port/F_Port logout.
 *
 * This routine prepares the mailbox command for unregistering remote port
 * login.
 *
 * For SLI4 ports, the rpi passed to this function must be the physical
 * rpi value, not the logical index.
 **/
void
lpfc_unreg_login(struct lpfc_hba *phba, uint16_t vpi, uint32_t rpi,
		 LPFC_MBOXQ_t * pmb)
{
	MAILBOX_t *mb;

	mb = &pmb->u.mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	mb->un.varUnregLogin.rpi = rpi;
	mb->un.varUnregLogin.rsvd1 = 0;
	if (phba->sli_rev >= LPFC_SLI_REV3)
		mb->un.varUnregLogin.vpi = phba->vpi_ids[vpi];

	mb->mbxCommand = MBX_UNREG_LOGIN;
	mb->mbxOwner = OWN_HOST;

	return;
}

/**
 * lpfc_sli4_unreg_all_rpis - unregister all RPIs for a vport on SLI4 HBA.
 * @vport: pointer to a vport object.
 *
 * This routine sends mailbox command to unregister all active RPIs for
 * a vport.
 **/
void
lpfc_sli4_unreg_all_rpis(struct lpfc_vport *vport)
{
	struct lpfc_hba  *phba  = vport->phba;
	LPFC_MBOXQ_t     *mbox;
	int rc;

	mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (mbox) {
		/*
		 * For SLI4 functions, the rpi field is overloaded for
		 * the vport context unreg all.  This routine passes
		 * 0 for the rpi field in lpfc_unreg_login for compatibility
		 * with SLI3 and then overrides the rpi field with the
		 * expected value for SLI4.
		 */
		lpfc_unreg_login(phba, vport->vpi, phba->vpi_ids[vport->vpi],
				 mbox);
		mbox->u.mb.un.varUnregLogin.rsvd1 = 0x4000;
		mbox->vport = vport;
		mbox->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
		mbox->ctx_ndlp = NULL;
		rc = lpfc_sli_issue_mbox(phba, mbox, MBX_NOWAIT);
		if (rc == MBX_NOT_FINISHED)
			mempool_free(mbox, phba->mbox_mem_pool);
	}
}

/**
 * lpfc_reg_vpi - Prepare a mailbox command for registering vport identifier
 * @phba: pointer to lpfc hba data structure.
 * @vpi: virtual N_Port identifier.
 * @sid: Fibre Channel S_ID (N_Port_ID assigned to a virtual N_Port).
 * @pmb: pointer to the driver internal queue element for mailbox command.
 *
 * The registration vport identifier mailbox command is used to activate a
 * virtual N_Port after it has acquired an N_Port_ID. The HBA validates the
 * N_Port_ID against the information in the selected virtual N_Port context
 * block and marks it active to allow normal processing of IOCB commands and
 * received unsolicited exchanges.
 *
 * This routine prepares the mailbox command for registering a virtual N_Port.
 **/
void
lpfc_reg_vpi(struct lpfc_vport *vport, LPFC_MBOXQ_t *pmb)
{
	MAILBOX_t *mb = &pmb->u.mb;
	struct lpfc_hba *phba = vport->phba;

	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));
	/*
	 * Set the re-reg VPI bit for f/w to update the MAC address.
	 */
	if ((phba->sli_rev == LPFC_SLI_REV4) &&
		!(vport->fc_flag & FC_VPORT_NEEDS_REG_VPI))
		mb->un.varRegVpi.upd = 1;

	mb->un.varRegVpi.vpi = phba->vpi_ids[vport->vpi];
	mb->un.varRegVpi.sid = vport->fc_myDID;
	if (phba->sli_rev == LPFC_SLI_REV4)
		mb->un.varRegVpi.vfi = phba->sli4_hba.vfi_ids[vport->vfi];
	else
		mb->un.varRegVpi.vfi = vport->vfi + vport->phba->vfi_base;
	memcpy(mb->un.varRegVpi.wwn, &vport->fc_portname,
	       sizeof(struct lpfc_name));
	mb->un.varRegVpi.wwn[0] = cpu_to_le32(mb->un.varRegVpi.wwn[0]);
	mb->un.varRegVpi.wwn[1] = cpu_to_le32(mb->un.varRegVpi.wwn[1]);

	mb->mbxCommand = MBX_REG_VPI;
	mb->mbxOwner = OWN_HOST;
	return;

}

/**
 * lpfc_unreg_vpi - Prepare a mailbox command for unregistering vport id
 * @phba: pointer to lpfc hba data structure.
 * @vpi: virtual N_Port identifier.
 * @pmb: pointer to the driver internal queue element for mailbox command.
 *
 * The unregistration vport identifier mailbox command is used to inactivate
 * a virtual N_Port. The driver must have logged out and unregistered all
 * remote N_Ports to abort any activity on the virtual N_Port. The HBA will
 * unregisters any default RPIs associated with the specified vpi, aborting
 * any active exchanges. The HBA will post the mailbox response after making
 * the virtual N_Port inactive.
 *
 * This routine prepares the mailbox command for unregistering a virtual
 * N_Port.
 **/
void
lpfc_unreg_vpi(struct lpfc_hba *phba, uint16_t vpi, LPFC_MBOXQ_t *pmb)
{
	MAILBOX_t *mb = &pmb->u.mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	if (phba->sli_rev == LPFC_SLI_REV3)
		mb->un.varUnregVpi.vpi = phba->vpi_ids[vpi];
	else if (phba->sli_rev >= LPFC_SLI_REV4)
		mb->un.varUnregVpi.sli4_vpi = phba->vpi_ids[vpi];

	mb->mbxCommand = MBX_UNREG_VPI;
	mb->mbxOwner = OWN_HOST;
	return;

}

/**
 * lpfc_config_pcb_setup - Set up IOCB rings in the Port Control Block (PCB)
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine sets up and initializes the IOCB rings in the Port Control
 * Block (PCB).
 **/
static void
lpfc_config_pcb_setup(struct lpfc_hba * phba)
{
	struct lpfc_sli *psli = &phba->sli;
	struct lpfc_sli_ring *pring;
	PCB_t *pcbp = phba->pcb;
	dma_addr_t pdma_addr;
	uint32_t offset;
	uint32_t iocbCnt = 0;
	int i;

	pcbp->maxRing = (psli->num_rings - 1);

	for (i = 0; i < psli->num_rings; i++) {
		pring = &psli->sli3_ring[i];

		pring->sli.sli3.sizeCiocb =
			phba->sli_rev == 3 ? SLI3_IOCB_CMD_SIZE :
							SLI2_IOCB_CMD_SIZE;
		pring->sli.sli3.sizeRiocb =
			phba->sli_rev == 3 ? SLI3_IOCB_RSP_SIZE :
							SLI2_IOCB_RSP_SIZE;
		/* A ring MUST have both cmd and rsp entries defined to be
		   valid */
		if ((pring->sli.sli3.numCiocb == 0) ||
			(pring->sli.sli3.numRiocb == 0)) {
			pcbp->rdsc[i].cmdEntries = 0;
			pcbp->rdsc[i].rspEntries = 0;
			pcbp->rdsc[i].cmdAddrHigh = 0;
			pcbp->rdsc[i].rspAddrHigh = 0;
			pcbp->rdsc[i].cmdAddrLow = 0;
			pcbp->rdsc[i].rspAddrLow = 0;
			pring->sli.sli3.cmdringaddr = NULL;
			pring->sli.sli3.rspringaddr = NULL;
			continue;
		}
		/* Command ring setup for ring */
		pring->sli.sli3.cmdringaddr = (void *)&phba->IOCBs[iocbCnt];
		pcbp->rdsc[i].cmdEntries = pring->sli.sli3.numCiocb;

		offset = (uint8_t *) &phba->IOCBs[iocbCnt] -
			 (uint8_t *) phba->slim2p.virt;
		pdma_addr = phba->slim2p.phys + offset;
		pcbp->rdsc[i].cmdAddrHigh = putPaddrHigh(pdma_addr);
		pcbp->rdsc[i].cmdAddrLow = putPaddrLow(pdma_addr);
		iocbCnt += pring->sli.sli3.numCiocb;

		/* Response ring setup for ring */
		pring->sli.sli3.rspringaddr = (void *) &phba->IOCBs[iocbCnt];

		pcbp->rdsc[i].rspEntries = pring->sli.sli3.numRiocb;
		offset = (uint8_t *)&phba->IOCBs[iocbCnt] -
			 (uint8_t *)phba->slim2p.virt;
		pdma_addr = phba->slim2p.phys + offset;
		pcbp->rdsc[i].rspAddrHigh = putPaddrHigh(pdma_addr);
		pcbp->rdsc[i].rspAddrLow = putPaddrLow(pdma_addr);
		iocbCnt += pring->sli.sli3.numRiocb;
	}
}

/**
 * lpfc_read_rev - Prepare a mailbox command for reading HBA revision
 * @phba: pointer to lpfc hba data structure.
 * @pmb: pointer to the driver internal queue element for mailbox command.
 *
 * The read revision mailbox command is used to read the revision levels of
 * the HBA components. These components include hardware units, resident
 * firmware, and available firmware. HBAs that supports SLI-3 mode of
 * operation provide different response information depending on the version
 * requested by the driver.
 *
 * This routine prepares the mailbox command for reading HBA revision
 * information.
 **/
void
lpfc_read_rev(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb)
{
	MAILBOX_t *mb = &pmb->u.mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));
	mb->un.varRdRev.cv = 1;
	mb->un.varRdRev.v3req = 1; /* Request SLI3 info */
	mb->mbxCommand = MBX_READ_REV;
	mb->mbxOwner = OWN_HOST;
	return;
}

void
lpfc_sli4_swap_str(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmb)
{
	MAILBOX_t *mb = &pmb->u.mb;
	struct lpfc_mqe *mqe;

	switch (mb->mbxCommand) {
	case  MBX_READ_REV:
		 mqe = &pmb->u.mqe;
		lpfc_sli_pcimem_bcopy(mqe->un.read_rev.fw_name,
				 mqe->un.read_rev.fw_name, 16);
		lpfc_sli_pcimem_bcopy(mqe->un.read_rev.ulp_fw_name,
				 mqe->un.read_rev.ulp_fw_name, 16);
		break;
	default:
		break;
	}
	return;
}

/**
 * lpfc_build_hbq_profile2 - Set up the HBQ Selection Profile 2
 * @hbqmb: pointer to the HBQ configuration data structure in mailbox command.
 * @hbq_desc: pointer to the HBQ selection profile descriptor.
 *
 * The Host Buffer Queue (HBQ) Selection Profile 2 specifies that the HBA
 * tests the incoming frames' R_CTL/TYPE fields with works 10:15 and performs
 * the Sequence Length Test using the fields in the Selection Profile 2
 * extension in words 20:31.
 **/
static void
lpfc_build_hbq_profile2(struct config_hbq_var *hbqmb,
			struct lpfc_hbq_init  *hbq_desc)
{
	hbqmb->profiles.profile2.seqlenbcnt = hbq_desc->seqlenbcnt;
	hbqmb->profiles.profile2.maxlen     = hbq_desc->maxlen;
	hbqmb->profiles.profile2.seqlenoff  = hbq_desc->seqlenoff;
}

/**
 * lpfc_build_hbq_profile3 - Set up the HBQ Selection Profile 3
 * @hbqmb: pointer to the HBQ configuration data structure in mailbox command.
 * @hbq_desc: pointer to the HBQ selection profile descriptor.
 *
 * The Host Buffer Queue (HBQ) Selection Profile 3 specifies that the HBA
 * tests the incoming frame's R_CTL/TYPE fields with words 10:15 and performs
 * the Sequence Length Test and Byte Field Test using the fields in the
 * Selection Profile 3 extension in words 20:31.
 **/
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

/**
 * lpfc_build_hbq_profile5 - Set up the HBQ Selection Profile 5
 * @hbqmb: pointer to the HBQ configuration data structure in mailbox command.
 * @hbq_desc: pointer to the HBQ selection profile descriptor.
 *
 * The Host Buffer Queue (HBQ) Selection Profile 5 specifies a header HBQ. The
 * HBA tests the initial frame of an incoming sequence using the frame's
 * R_CTL/TYPE fields with words 10:15 and performs the Sequence Length Test
 * and Byte Field Test using the fields in the Selection Profile 5 extension
 * words 20:31.
 **/
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

/**
 * lpfc_config_hbq - Prepare a mailbox command for configuring an HBQ
 * @phba: pointer to lpfc hba data structure.
 * @id: HBQ identifier.
 * @hbq_desc: pointer to the HBA descriptor data structure.
 * @hbq_entry_index: index of the HBQ entry data structures.
 * @pmb: pointer to the driver internal queue element for mailbox command.
 *
 * The configure HBQ (Host Buffer Queue) mailbox command is used to configure
 * an HBQ. The configuration binds events that require buffers to a particular
 * ring and HBQ based on a selection profile.
 *
 * This routine prepares the mailbox command for configuring an HBQ.
 **/
void
lpfc_config_hbq(struct lpfc_hba *phba, uint32_t id,
		 struct lpfc_hbq_init *hbq_desc,
		uint32_t hbq_entry_index, LPFC_MBOXQ_t *pmb)
{
	int i;
	MAILBOX_t *mb = &pmb->u.mb;
	struct config_hbq_var *hbqmb = &mb->un.varCfgHbq;

	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));
	hbqmb->hbqId = id;
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

/**
 * lpfc_config_ring - Prepare a mailbox command for configuring an IOCB ring
 * @phba: pointer to lpfc hba data structure.
 * @ring:
 * @pmb: pointer to the driver internal queue element for mailbox command.
 *
 * The configure ring mailbox command is used to configure an IOCB ring. This
 * configuration binds from one to six of HBA RC_CTL/TYPE mask entries to the
 * ring. This is used to map incoming sequences to a particular ring whose
 * RC_CTL/TYPE mask entry matches that of the sequence. The driver should not
 * attempt to configure a ring whose number is greater than the number
 * specified in the Port Control Block (PCB). It is an error to issue the
 * configure ring command more than once with the same ring number. The HBA
 * returns an error if the driver attempts this.
 *
 * This routine prepares the mailbox command for configuring IOCB ring.
 **/
void
lpfc_config_ring(struct lpfc_hba * phba, int ring, LPFC_MBOXQ_t * pmb)
{
	int i;
	MAILBOX_t *mb = &pmb->u.mb;
	struct lpfc_sli *psli;
	struct lpfc_sli_ring *pring;

	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	mb->un.varCfgRing.ring = ring;
	mb->un.varCfgRing.maxOrigXchg = 0;
	mb->un.varCfgRing.maxRespXchg = 0;
	mb->un.varCfgRing.recvNotify = 1;

	psli = &phba->sli;
	pring = &psli->sli3_ring[ring];
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
		if (mb->un.varCfgRing.rrRegs[i].rval != FC_RCTL_ELS_REQ)
			mb->un.varCfgRing.rrRegs[i].rmask = 0xff;
		else
			mb->un.varCfgRing.rrRegs[i].rmask = 0xfe;
		mb->un.varCfgRing.rrRegs[i].tval = pring->prt[i].type;
		mb->un.varCfgRing.rrRegs[i].tmask = 0xff;
	}

	return;
}

/**
 * lpfc_config_port - Prepare a mailbox command for configuring port
 * @phba: pointer to lpfc hba data structure.
 * @pmb: pointer to the driver internal queue element for mailbox command.
 *
 * The configure port mailbox command is used to identify the Port Control
 * Block (PCB) in the driver memory. After this command is issued, the
 * driver must not access the mailbox in the HBA without first resetting
 * the HBA. The HBA may copy the PCB information to internal storage for
 * subsequent use; the driver can not change the PCB information unless it
 * resets the HBA.
 *
 * This routine prepares the mailbox command for configuring port.
 **/
void
lpfc_config_port(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmb)
{
	MAILBOX_t __iomem *mb_slim = (MAILBOX_t __iomem *) phba->MBslimaddr;
	MAILBOX_t *mb = &pmb->u.mb;
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

	offset = (uint8_t *)phba->pcb - (uint8_t *)phba->slim2p.virt;
	pdma_addr = phba->slim2p.phys + offset;
	mb->un.varCfgPort.pcbLow = putPaddrLow(pdma_addr);
	mb->un.varCfgPort.pcbHigh = putPaddrHigh(pdma_addr);

	/* Always Host Group Pointer is in SLIM */
	mb->un.varCfgPort.hps = 1;

	/* If HBA supports SLI=3 ask for it */

	if (phba->sli_rev == LPFC_SLI_REV3 && phba->vpd.sli3Feat.cerbm) {
		if (phba->cfg_enable_bg)
			mb->un.varCfgPort.cbg = 1; /* configure BlockGuard */
		if (phba->cfg_enable_dss)
			mb->un.varCfgPort.cdss = 1; /* Configure Security */
		mb->un.varCfgPort.cerbm = 1; /* Request HBQs */
		mb->un.varCfgPort.ccrp = 1; /* Command Ring Polling */
		mb->un.varCfgPort.max_hbq = lpfc_sli_hbq_count();
		if (phba->max_vpi && phba->cfg_enable_npiv &&
		    phba->vpd.sli3Feat.cmv) {
			mb->un.varCfgPort.max_vpi = LPFC_MAX_VPI;
			mb->un.varCfgPort.cmv = 1;
		} else
			mb->un.varCfgPort.max_vpi = phba->max_vpi = 0;
	} else
		phba->sli_rev = LPFC_SLI_REV2;
	mb->un.varCfgPort.sli_mode = phba->sli_rev;

	/* If this is an SLI3 port, configure async status notification. */
	if (phba->sli_rev == LPFC_SLI_REV3)
		mb->un.varCfgPort.casabt = 1;

	/* Now setup pcb */
	phba->pcb->type = TYPE_NATIVE_SLI2;
	phba->pcb->feature = FEATURE_INITIAL_SLI2;

	/* Setup Mailbox pointers */
	phba->pcb->mailBoxSize = sizeof(MAILBOX_t) + MAILBOX_EXT_SIZE;
	offset = (uint8_t *)phba->mbox - (uint8_t *)phba->slim2p.virt;
	pdma_addr = phba->slim2p.phys + offset;
	phba->pcb->mbAddrHigh = putPaddrHigh(pdma_addr);
	phba->pcb->mbAddrLow = putPaddrLow(pdma_addr);

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

	if (phba->cfg_hostmem_hgp && phba->sli_rev != 3) {
		phba->host_gp = &phba->mbox->us.s2.host[0];
		phba->hbq_put = NULL;
		offset = (uint8_t *)&phba->mbox->us.s2.host -
			(uint8_t *)phba->slim2p.virt;
		pdma_addr = phba->slim2p.phys + offset;
		phba->pcb->hgpAddrHigh = putPaddrHigh(pdma_addr);
		phba->pcb->hgpAddrLow = putPaddrLow(pdma_addr);
	} else {
		/* Always Host Group Pointer is in SLIM */
		mb->un.varCfgPort.hps = 1;

		if (phba->sli_rev == 3) {
			phba->host_gp = &mb_slim->us.s3.host[0];
			phba->hbq_put = &mb_slim->us.s3.hbq_put[0];
		} else {
			phba->host_gp = &mb_slim->us.s2.host[0];
			phba->hbq_put = NULL;
		}

		/* mask off BAR0's flag bits 0 - 3 */
		phba->pcb->hgpAddrLow = (bar_low & PCI_BASE_ADDRESS_MEM_MASK) +
			(void __iomem *)phba->host_gp -
			(void __iomem *)phba->MBslimaddr;
		if (bar_low & PCI_BASE_ADDRESS_MEM_TYPE_64)
			phba->pcb->hgpAddrHigh = bar_high;
		else
			phba->pcb->hgpAddrHigh = 0;
		/* write HGP data to SLIM at the required longword offset */
		memset(&hgp, 0, sizeof(struct lpfc_hgp));

		for (i = 0; i < phba->sli.num_rings; i++) {
			lpfc_memcpy_to_slim(phba->host_gp + i, &hgp,
				    sizeof(*phba->host_gp));
		}
	}

	/* Setup Port Group offset */
	if (phba->sli_rev == 3)
		pgp_offset = offsetof(struct lpfc_sli2_slim,
				      mbx.us.s3_pgp.port);
	else
		pgp_offset = offsetof(struct lpfc_sli2_slim, mbx.us.s2.port);
	pdma_addr = phba->slim2p.phys + pgp_offset;
	phba->pcb->pgpAddrHigh = putPaddrHigh(pdma_addr);
	phba->pcb->pgpAddrLow = putPaddrLow(pdma_addr);

	/* Use callback routine to setp rings in the pcb */
	lpfc_config_pcb_setup(phba);

	/* special handling for LC HBAs */
	if (lpfc_is_LC_HBA(phba->pcidev->device)) {
		uint32_t hbainit[5];

		lpfc_hba_init(phba, hbainit);

		memcpy(&mb->un.varCfgPort.hbainit, hbainit, 20);
	}

	/* Swap PCB if needed */
	lpfc_sli_pcimem_bcopy(phba->pcb, phba->pcb, sizeof(PCB_t));
}

/**
 * lpfc_kill_board - Prepare a mailbox command for killing board
 * @phba: pointer to lpfc hba data structure.
 * @pmb: pointer to the driver internal queue element for mailbox command.
 *
 * The kill board mailbox command is used to tell firmware to perform a
 * graceful shutdown of a channel on a specified board to prepare for reset.
 * When the kill board mailbox command is received, the ER3 bit is set to 1
 * in the Host Status register and the ER Attention bit is set to 1 in the
 * Host Attention register of the HBA function that received the kill board
 * command.
 *
 * This routine prepares the mailbox command for killing the board in
 * preparation for a graceful shutdown.
 **/
void
lpfc_kill_board(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb)
{
	MAILBOX_t *mb = &pmb->u.mb;

	memset(pmb, 0, sizeof(LPFC_MBOXQ_t));
	mb->mbxCommand = MBX_KILL_BOARD;
	mb->mbxOwner = OWN_HOST;
	return;
}

/**
 * lpfc_mbox_put - Put a mailbox cmd into the tail of driver's mailbox queue
 * @phba: pointer to lpfc hba data structure.
 * @mbq: pointer to the driver internal queue element for mailbox command.
 *
 * Driver maintains a internal mailbox command queue implemented as a linked
 * list. When a mailbox command is issued, it shall be put into the mailbox
 * command queue such that they shall be processed orderly as HBA can process
 * one mailbox command at a time.
 **/
void
lpfc_mbox_put(struct lpfc_hba * phba, LPFC_MBOXQ_t * mbq)
{
	struct lpfc_sli *psli;

	psli = &phba->sli;

	list_add_tail(&mbq->list, &psli->mboxq);

	psli->mboxq_cnt++;

	return;
}

/**
 * lpfc_mbox_get - Remove a mailbox cmd from the head of driver's mailbox queue
 * @phba: pointer to lpfc hba data structure.
 *
 * Driver maintains a internal mailbox command queue implemented as a linked
 * list. When a mailbox command is issued, it shall be put into the mailbox
 * command queue such that they shall be processed orderly as HBA can process
 * one mailbox command at a time. After HBA finished processing a mailbox
 * command, the driver will remove a pending mailbox command from the head of
 * the mailbox command queue and send to the HBA for processing.
 *
 * Return codes
 *    pointer to the driver internal queue element for mailbox command.
 **/
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

/**
 * __lpfc_mbox_cmpl_put - Put mailbox cmd into mailbox cmd complete list
 * @phba: pointer to lpfc hba data structure.
 * @mbq: pointer to the driver internal queue element for mailbox command.
 *
 * This routine put the completed mailbox command into the mailbox command
 * complete list. This is the unlocked version of the routine. The mailbox
 * complete list is used by the driver worker thread to process mailbox
 * complete callback functions outside the driver interrupt handler.
 **/
void
__lpfc_mbox_cmpl_put(struct lpfc_hba *phba, LPFC_MBOXQ_t *mbq)
{
	list_add_tail(&mbq->list, &phba->sli.mboxq_cmpl);
}

/**
 * lpfc_mbox_cmpl_put - Put mailbox command into mailbox command complete list
 * @phba: pointer to lpfc hba data structure.
 * @mbq: pointer to the driver internal queue element for mailbox command.
 *
 * This routine put the completed mailbox command into the mailbox command
 * complete list. This is the locked version of the routine. The mailbox
 * complete list is used by the driver worker thread to process mailbox
 * complete callback functions outside the driver interrupt handler.
 **/
void
lpfc_mbox_cmpl_put(struct lpfc_hba *phba, LPFC_MBOXQ_t *mbq)
{
	unsigned long iflag;

	/* This function expects to be called from interrupt context */
	spin_lock_irqsave(&phba->hbalock, iflag);
	__lpfc_mbox_cmpl_put(phba, mbq);
	spin_unlock_irqrestore(&phba->hbalock, iflag);
	return;
}

/**
 * lpfc_mbox_cmd_check - Check the validality of a mailbox command
 * @phba: pointer to lpfc hba data structure.
 * @mboxq: pointer to the driver internal queue element for mailbox command.
 *
 * This routine is to check whether a mailbox command is valid to be issued.
 * This check will be performed by both the mailbox issue API when a client
 * is to issue a mailbox command to the mailbox transport.
 *
 * Return 0 - pass the check, -ENODEV - fail the check
 **/
int
lpfc_mbox_cmd_check(struct lpfc_hba *phba, LPFC_MBOXQ_t *mboxq)
{
	/* Mailbox command that have a completion handler must also have a
	 * vport specified.
	 */
	if (mboxq->mbox_cmpl && mboxq->mbox_cmpl != lpfc_sli_def_mbox_cmpl &&
	    mboxq->mbox_cmpl != lpfc_sli_wake_mbox_wait) {
		if (!mboxq->vport) {
			lpfc_printf_log(phba, KERN_ERR, LOG_MBOX | LOG_VPORT,
					"1814 Mbox x%x failed, no vport\n",
					mboxq->u.mb.mbxCommand);
			dump_stack();
			return -ENODEV;
		}
	}
	return 0;
}

/**
 * lpfc_mbox_dev_check - Check the device state for issuing a mailbox command
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is to check whether the HBA device is ready for posting a
 * mailbox command. It is used by the mailbox transport API at the time the
 * to post a mailbox command to the device.
 *
 * Return 0 - pass the check, -ENODEV - fail the check
 **/
int
lpfc_mbox_dev_check(struct lpfc_hba *phba)
{
	/* If the PCI channel is in offline state, do not issue mbox */
	if (unlikely(pci_channel_offline(phba->pcidev)))
		return -ENODEV;

	/* If the HBA is in error state, do not issue mbox */
	if (phba->link_state == LPFC_HBA_ERROR)
		return -ENODEV;

	return 0;
}

/**
 * lpfc_mbox_tmo_val - Retrieve mailbox command timeout value
 * @phba: pointer to lpfc hba data structure.
 * @cmd: mailbox command code.
 *
 * This routine retrieves the proper timeout value according to the mailbox
 * command code.
 *
 * Return codes
 *    Timeout value to be used for the given mailbox command
 **/
int
lpfc_mbox_tmo_val(struct lpfc_hba *phba, LPFC_MBOXQ_t *mboxq)
{
	MAILBOX_t *mbox = &mboxq->u.mb;
	uint8_t subsys, opcode;

	switch (mbox->mbxCommand) {
	case MBX_WRITE_NV:	/* 0x03 */
	case MBX_DUMP_MEMORY:	/* 0x17 */
	case MBX_UPDATE_CFG:	/* 0x1B */
	case MBX_DOWN_LOAD:	/* 0x1C */
	case MBX_DEL_LD_ENTRY:	/* 0x1D */
	case MBX_WRITE_VPARMS:	/* 0x32 */
	case MBX_LOAD_AREA:	/* 0x81 */
	case MBX_WRITE_WWN:     /* 0x98 */
	case MBX_LOAD_EXP_ROM:	/* 0x9C */
	case MBX_ACCESS_VDATA:	/* 0xA5 */
		return LPFC_MBOX_TMO_FLASH_CMD;
	case MBX_SLI4_CONFIG:	/* 0x9b */
		subsys = lpfc_sli_config_mbox_subsys_get(phba, mboxq);
		opcode = lpfc_sli_config_mbox_opcode_get(phba, mboxq);
		if (subsys == LPFC_MBOX_SUBSYSTEM_COMMON) {
			switch (opcode) {
			case LPFC_MBOX_OPCODE_READ_OBJECT:
			case LPFC_MBOX_OPCODE_WRITE_OBJECT:
			case LPFC_MBOX_OPCODE_READ_OBJECT_LIST:
			case LPFC_MBOX_OPCODE_DELETE_OBJECT:
			case LPFC_MBOX_OPCODE_GET_PROFILE_LIST:
			case LPFC_MBOX_OPCODE_SET_ACT_PROFILE:
			case LPFC_MBOX_OPCODE_GET_PROFILE_CONFIG:
			case LPFC_MBOX_OPCODE_SET_PROFILE_CONFIG:
			case LPFC_MBOX_OPCODE_GET_FACTORY_PROFILE_CONFIG:
			case LPFC_MBOX_OPCODE_GET_PROFILE_CAPACITIES:
			case LPFC_MBOX_OPCODE_SEND_ACTIVATION:
			case LPFC_MBOX_OPCODE_RESET_LICENSES:
			case LPFC_MBOX_OPCODE_SET_BOOT_CONFIG:
			case LPFC_MBOX_OPCODE_GET_VPD_DATA:
			case LPFC_MBOX_OPCODE_SET_PHYSICAL_LINK_CONFIG:
				return LPFC_MBOX_SLI4_CONFIG_EXTENDED_TMO;
			}
		}
		if (subsys == LPFC_MBOX_SUBSYSTEM_FCOE) {
			switch (opcode) {
			case LPFC_MBOX_OPCODE_FCOE_SET_FCLINK_SETTINGS:
				return LPFC_MBOX_SLI4_CONFIG_EXTENDED_TMO;
			}
		}
		return LPFC_MBOX_SLI4_CONFIG_TMO;
	}
	return LPFC_MBOX_TMO;
}

/**
 * lpfc_sli4_mbx_sge_set - Set a sge entry in non-embedded mailbox command
 * @mbox: pointer to lpfc mbox command.
 * @sgentry: sge entry index.
 * @phyaddr: physical address for the sge
 * @length: Length of the sge.
 *
 * This routine sets up an entry in the non-embedded mailbox command at the sge
 * index location.
 **/
void
lpfc_sli4_mbx_sge_set(struct lpfcMboxq *mbox, uint32_t sgentry,
		      dma_addr_t phyaddr, uint32_t length)
{
	struct lpfc_mbx_nembed_cmd *nembed_sge;

	nembed_sge = (struct lpfc_mbx_nembed_cmd *)
				&mbox->u.mqe.un.nembed_cmd;
	nembed_sge->sge[sgentry].pa_lo = putPaddrLow(phyaddr);
	nembed_sge->sge[sgentry].pa_hi = putPaddrHigh(phyaddr);
	nembed_sge->sge[sgentry].length = length;
}

/**
 * lpfc_sli4_mbx_sge_get - Get a sge entry from non-embedded mailbox command
 * @mbox: pointer to lpfc mbox command.
 * @sgentry: sge entry index.
 *
 * This routine gets an entry from the non-embedded mailbox command at the sge
 * index location.
 **/
void
lpfc_sli4_mbx_sge_get(struct lpfcMboxq *mbox, uint32_t sgentry,
		      struct lpfc_mbx_sge *sge)
{
	struct lpfc_mbx_nembed_cmd *nembed_sge;

	nembed_sge = (struct lpfc_mbx_nembed_cmd *)
				&mbox->u.mqe.un.nembed_cmd;
	sge->pa_lo = nembed_sge->sge[sgentry].pa_lo;
	sge->pa_hi = nembed_sge->sge[sgentry].pa_hi;
	sge->length = nembed_sge->sge[sgentry].length;
}

/**
 * lpfc_sli4_mbox_cmd_free - Free a sli4 mailbox command
 * @phba: pointer to lpfc hba data structure.
 * @mbox: pointer to lpfc mbox command.
 *
 * This routine frees SLI4 specific mailbox command for sending IOCTL command.
 **/
void
lpfc_sli4_mbox_cmd_free(struct lpfc_hba *phba, struct lpfcMboxq *mbox)
{
	struct lpfc_mbx_sli4_config *sli4_cfg;
	struct lpfc_mbx_sge sge;
	dma_addr_t phyaddr;
	uint32_t sgecount, sgentry;

	sli4_cfg = &mbox->u.mqe.un.sli4_config;

	/* For embedded mbox command, just free the mbox command */
	if (bf_get(lpfc_mbox_hdr_emb, &sli4_cfg->header.cfg_mhdr)) {
		mempool_free(mbox, phba->mbox_mem_pool);
		return;
	}

	/* For non-embedded mbox command, we need to free the pages first */
	sgecount = bf_get(lpfc_mbox_hdr_sge_cnt, &sli4_cfg->header.cfg_mhdr);
	/* There is nothing we can do if there is no sge address array */
	if (unlikely(!mbox->sge_array)) {
		mempool_free(mbox, phba->mbox_mem_pool);
		return;
	}
	/* Each non-embedded DMA memory was allocated in the length of a page */
	for (sgentry = 0; sgentry < sgecount; sgentry++) {
		lpfc_sli4_mbx_sge_get(mbox, sgentry, &sge);
		phyaddr = getPaddr(sge.pa_hi, sge.pa_lo);
		dma_free_coherent(&phba->pcidev->dev, SLI4_PAGE_SIZE,
				  mbox->sge_array->addr[sgentry], phyaddr);
	}
	/* Free the sge address array memory */
	kfree(mbox->sge_array);
	/* Finally, free the mailbox command itself */
	mempool_free(mbox, phba->mbox_mem_pool);
}

/**
 * lpfc_sli4_config - Initialize the  SLI4 Config Mailbox command
 * @phba: pointer to lpfc hba data structure.
 * @mbox: pointer to lpfc mbox command.
 * @subsystem: The sli4 config sub mailbox subsystem.
 * @opcode: The sli4 config sub mailbox command opcode.
 * @length: Length of the sli4 config mailbox command (including sub-header).
 *
 * This routine sets up the header fields of SLI4 specific mailbox command
 * for sending IOCTL command.
 *
 * Return: the actual length of the mbox command allocated (mostly useful
 *         for none embedded mailbox command).
 **/
int
lpfc_sli4_config(struct lpfc_hba *phba, struct lpfcMboxq *mbox,
		 uint8_t subsystem, uint8_t opcode, uint32_t length, bool emb)
{
	struct lpfc_mbx_sli4_config *sli4_config;
	union lpfc_sli4_cfg_shdr *cfg_shdr = NULL;
	uint32_t alloc_len;
	uint32_t resid_len;
	uint32_t pagen, pcount;
	void *viraddr;
	dma_addr_t phyaddr;

	/* Set up SLI4 mailbox command header fields */
	memset(mbox, 0, sizeof(*mbox));
	bf_set(lpfc_mqe_command, &mbox->u.mqe, MBX_SLI4_CONFIG);

	/* Set up SLI4 ioctl command header fields */
	sli4_config = &mbox->u.mqe.un.sli4_config;

	/* Setup for the embedded mbox command */
	if (emb) {
		/* Set up main header fields */
		bf_set(lpfc_mbox_hdr_emb, &sli4_config->header.cfg_mhdr, 1);
		sli4_config->header.cfg_mhdr.payload_length = length;
		/* Set up sub-header fields following main header */
		bf_set(lpfc_mbox_hdr_opcode,
			&sli4_config->header.cfg_shdr.request, opcode);
		bf_set(lpfc_mbox_hdr_subsystem,
			&sli4_config->header.cfg_shdr.request, subsystem);
		sli4_config->header.cfg_shdr.request.request_length =
			length - LPFC_MBX_CMD_HDR_LENGTH;
		return length;
	}

	/* Setup for the non-embedded mbox command */
	pcount = (SLI4_PAGE_ALIGN(length))/SLI4_PAGE_SIZE;
	pcount = (pcount > LPFC_SLI4_MBX_SGE_MAX_PAGES) ?
				LPFC_SLI4_MBX_SGE_MAX_PAGES : pcount;
	/* Allocate record for keeping SGE virtual addresses */
	mbox->sge_array = kzalloc(sizeof(struct lpfc_mbx_nembed_sge_virt),
				  GFP_KERNEL);
	if (!mbox->sge_array) {
		lpfc_printf_log(phba, KERN_ERR, LOG_MBOX,
				"2527 Failed to allocate non-embedded SGE "
				"array.\n");
		return 0;
	}
	for (pagen = 0, alloc_len = 0; pagen < pcount; pagen++) {
		/* The DMA memory is always allocated in the length of a
		 * page even though the last SGE might not fill up to a
		 * page, this is used as a priori size of SLI4_PAGE_SIZE for
		 * the later DMA memory free.
		 */
		viraddr = dma_zalloc_coherent(&phba->pcidev->dev,
					      SLI4_PAGE_SIZE, &phyaddr,
					      GFP_KERNEL);
		/* In case of malloc fails, proceed with whatever we have */
		if (!viraddr)
			break;
		mbox->sge_array->addr[pagen] = viraddr;
		/* Keep the first page for later sub-header construction */
		if (pagen == 0)
			cfg_shdr = (union lpfc_sli4_cfg_shdr *)viraddr;
		resid_len = length - alloc_len;
		if (resid_len > SLI4_PAGE_SIZE) {
			lpfc_sli4_mbx_sge_set(mbox, pagen, phyaddr,
					      SLI4_PAGE_SIZE);
			alloc_len += SLI4_PAGE_SIZE;
		} else {
			lpfc_sli4_mbx_sge_set(mbox, pagen, phyaddr,
					      resid_len);
			alloc_len = length;
		}
	}

	/* Set up main header fields in mailbox command */
	sli4_config->header.cfg_mhdr.payload_length = alloc_len;
	bf_set(lpfc_mbox_hdr_sge_cnt, &sli4_config->header.cfg_mhdr, pagen);

	/* Set up sub-header fields into the first page */
	if (pagen > 0) {
		bf_set(lpfc_mbox_hdr_opcode, &cfg_shdr->request, opcode);
		bf_set(lpfc_mbox_hdr_subsystem, &cfg_shdr->request, subsystem);
		cfg_shdr->request.request_length =
				alloc_len - sizeof(union  lpfc_sli4_cfg_shdr);
	}
	/* The sub-header is in DMA memory, which needs endian converstion */
	if (cfg_shdr)
		lpfc_sli_pcimem_bcopy(cfg_shdr, cfg_shdr,
				      sizeof(union  lpfc_sli4_cfg_shdr));
	return alloc_len;
}

/**
 * lpfc_sli4_mbox_rsrc_extent - Initialize the opcode resource extent.
 * @phba: pointer to lpfc hba data structure.
 * @mbox: pointer to an allocated lpfc mbox resource.
 * @exts_count: the number of extents, if required, to allocate.
 * @rsrc_type: the resource extent type.
 * @emb: true if LPFC_SLI4_MBX_EMBED. false if LPFC_SLI4_MBX_NEMBED.
 *
 * This routine completes the subcommand header for SLI4 resource extent
 * mailbox commands.  It is called after lpfc_sli4_config.  The caller must
 * pass an allocated mailbox and the attributes required to initialize the
 * mailbox correctly.
 *
 * Return: the actual length of the mbox command allocated.
 **/
int
lpfc_sli4_mbox_rsrc_extent(struct lpfc_hba *phba, struct lpfcMboxq *mbox,
			   uint16_t exts_count, uint16_t rsrc_type, bool emb)
{
	uint8_t opcode = 0;
	struct lpfc_mbx_nembed_rsrc_extent *n_rsrc_extnt = NULL;
	void *virtaddr = NULL;

	/* Set up SLI4 ioctl command header fields */
	if (emb == LPFC_SLI4_MBX_NEMBED) {
		/* Get the first SGE entry from the non-embedded DMA memory */
		virtaddr = mbox->sge_array->addr[0];
		if (virtaddr == NULL)
			return 1;
		n_rsrc_extnt = (struct lpfc_mbx_nembed_rsrc_extent *) virtaddr;
	}

	/*
	 * The resource type is common to all extent Opcodes and resides in the
	 * same position.
	 */
	if (emb == LPFC_SLI4_MBX_EMBED)
		bf_set(lpfc_mbx_alloc_rsrc_extents_type,
		       &mbox->u.mqe.un.alloc_rsrc_extents.u.req,
		       rsrc_type);
	else {
		/* This is DMA data.  Byteswap is required. */
		bf_set(lpfc_mbx_alloc_rsrc_extents_type,
		       n_rsrc_extnt, rsrc_type);
		lpfc_sli_pcimem_bcopy(&n_rsrc_extnt->word4,
				      &n_rsrc_extnt->word4,
				      sizeof(uint32_t));
	}

	/* Complete the initialization for the particular Opcode. */
	opcode = lpfc_sli_config_mbox_opcode_get(phba, mbox);
	switch (opcode) {
	case LPFC_MBOX_OPCODE_ALLOC_RSRC_EXTENT:
		if (emb == LPFC_SLI4_MBX_EMBED)
			bf_set(lpfc_mbx_alloc_rsrc_extents_cnt,
			       &mbox->u.mqe.un.alloc_rsrc_extents.u.req,
			       exts_count);
		else
			bf_set(lpfc_mbx_alloc_rsrc_extents_cnt,
			       n_rsrc_extnt, exts_count);
		break;
	case LPFC_MBOX_OPCODE_GET_ALLOC_RSRC_EXTENT:
	case LPFC_MBOX_OPCODE_GET_RSRC_EXTENT_INFO:
	case LPFC_MBOX_OPCODE_DEALLOC_RSRC_EXTENT:
		/* Initialization is complete.*/
		break;
	default:
		lpfc_printf_log(phba, KERN_ERR, LOG_MBOX,
				"2929 Resource Extent Opcode x%x is "
				"unsupported\n", opcode);
		return 1;
	}

	return 0;
}

/**
 * lpfc_sli_config_mbox_subsys_get - Get subsystem from a sli_config mbox cmd
 * @phba: pointer to lpfc hba data structure.
 * @mbox: pointer to lpfc mbox command queue entry.
 *
 * This routine gets the subsystem from a SLI4 specific SLI_CONFIG mailbox
 * command. If the mailbox command is not MBX_SLI4_CONFIG (0x9B) or if the
 * sub-header is not present, subsystem LPFC_MBOX_SUBSYSTEM_NA (0x0) shall
 * be returned.
 **/
uint8_t
lpfc_sli_config_mbox_subsys_get(struct lpfc_hba *phba, LPFC_MBOXQ_t *mbox)
{
	struct lpfc_mbx_sli4_config *sli4_cfg;
	union lpfc_sli4_cfg_shdr *cfg_shdr;

	if (mbox->u.mb.mbxCommand != MBX_SLI4_CONFIG)
		return LPFC_MBOX_SUBSYSTEM_NA;
	sli4_cfg = &mbox->u.mqe.un.sli4_config;

	/* For embedded mbox command, get opcode from embedded sub-header*/
	if (bf_get(lpfc_mbox_hdr_emb, &sli4_cfg->header.cfg_mhdr)) {
		cfg_shdr = &mbox->u.mqe.un.sli4_config.header.cfg_shdr;
		return bf_get(lpfc_mbox_hdr_subsystem, &cfg_shdr->request);
	}

	/* For non-embedded mbox command, get opcode from first dma page */
	if (unlikely(!mbox->sge_array))
		return LPFC_MBOX_SUBSYSTEM_NA;
	cfg_shdr = (union lpfc_sli4_cfg_shdr *)mbox->sge_array->addr[0];
	return bf_get(lpfc_mbox_hdr_subsystem, &cfg_shdr->request);
}

/**
 * lpfc_sli_config_mbox_opcode_get - Get opcode from a sli_config mbox cmd
 * @phba: pointer to lpfc hba data structure.
 * @mbox: pointer to lpfc mbox command queue entry.
 *
 * This routine gets the opcode from a SLI4 specific SLI_CONFIG mailbox
 * command. If the mailbox command is not MBX_SLI4_CONFIG (0x9B) or if
 * the sub-header is not present, opcode LPFC_MBOX_OPCODE_NA (0x0) be
 * returned.
 **/
uint8_t
lpfc_sli_config_mbox_opcode_get(struct lpfc_hba *phba, LPFC_MBOXQ_t *mbox)
{
	struct lpfc_mbx_sli4_config *sli4_cfg;
	union lpfc_sli4_cfg_shdr *cfg_shdr;

	if (mbox->u.mb.mbxCommand != MBX_SLI4_CONFIG)
		return LPFC_MBOX_OPCODE_NA;
	sli4_cfg = &mbox->u.mqe.un.sli4_config;

	/* For embedded mbox command, get opcode from embedded sub-header*/
	if (bf_get(lpfc_mbox_hdr_emb, &sli4_cfg->header.cfg_mhdr)) {
		cfg_shdr = &mbox->u.mqe.un.sli4_config.header.cfg_shdr;
		return bf_get(lpfc_mbox_hdr_opcode, &cfg_shdr->request);
	}

	/* For non-embedded mbox command, get opcode from first dma page */
	if (unlikely(!mbox->sge_array))
		return LPFC_MBOX_OPCODE_NA;
	cfg_shdr = (union lpfc_sli4_cfg_shdr *)mbox->sge_array->addr[0];
	return bf_get(lpfc_mbox_hdr_opcode, &cfg_shdr->request);
}

/**
 * lpfc_sli4_mbx_read_fcf_rec - Allocate and construct read fcf mbox cmd
 * @phba: pointer to lpfc hba data structure.
 * @fcf_index: index to fcf table.
 *
 * This routine routine allocates and constructs non-embedded mailbox command
 * for reading a FCF table entry referred by @fcf_index.
 *
 * Return: pointer to the mailbox command constructed if successful, otherwise
 * NULL.
 **/
int
lpfc_sli4_mbx_read_fcf_rec(struct lpfc_hba *phba,
			   struct lpfcMboxq *mboxq,
			   uint16_t fcf_index)
{
	void *virt_addr;
	uint8_t *bytep;
	struct lpfc_mbx_sge sge;
	uint32_t alloc_len, req_len;
	struct lpfc_mbx_read_fcf_tbl *read_fcf;

	if (!mboxq)
		return -ENOMEM;

	req_len = sizeof(struct fcf_record) +
		  sizeof(union lpfc_sli4_cfg_shdr) + 2 * sizeof(uint32_t);

	/* Set up READ_FCF SLI4_CONFIG mailbox-ioctl command */
	alloc_len = lpfc_sli4_config(phba, mboxq, LPFC_MBOX_SUBSYSTEM_FCOE,
			LPFC_MBOX_OPCODE_FCOE_READ_FCF_TABLE, req_len,
			LPFC_SLI4_MBX_NEMBED);

	if (alloc_len < req_len) {
		lpfc_printf_log(phba, KERN_ERR, LOG_MBOX,
				"0291 Allocated DMA memory size (x%x) is "
				"less than the requested DMA memory "
				"size (x%x)\n", alloc_len, req_len);
		return -ENOMEM;
	}

	/* Get the first SGE entry from the non-embedded DMA memory. This
	 * routine only uses a single SGE.
	 */
	lpfc_sli4_mbx_sge_get(mboxq, 0, &sge);
	virt_addr = mboxq->sge_array->addr[0];
	read_fcf = (struct lpfc_mbx_read_fcf_tbl *)virt_addr;

	/* Set up command fields */
	bf_set(lpfc_mbx_read_fcf_tbl_indx, &read_fcf->u.request, fcf_index);
	/* Perform necessary endian conversion */
	bytep = virt_addr + sizeof(union lpfc_sli4_cfg_shdr);
	lpfc_sli_pcimem_bcopy(bytep, bytep, sizeof(uint32_t));

	return 0;
}

/**
 * lpfc_request_features: Configure SLI4 REQUEST_FEATURES mailbox
 * @mboxq: pointer to lpfc mbox command.
 *
 * This routine sets up the mailbox for an SLI4 REQUEST_FEATURES
 * mailbox command.
 **/
void
lpfc_request_features(struct lpfc_hba *phba, struct lpfcMboxq *mboxq)
{
	/* Set up SLI4 mailbox command header fields */
	memset(mboxq, 0, sizeof(LPFC_MBOXQ_t));
	bf_set(lpfc_mqe_command, &mboxq->u.mqe, MBX_SLI4_REQ_FTRS);

	/* Set up host requested features. */
	bf_set(lpfc_mbx_rq_ftr_rq_fcpi, &mboxq->u.mqe.un.req_ftrs, 1);
	bf_set(lpfc_mbx_rq_ftr_rq_perfh, &mboxq->u.mqe.un.req_ftrs, 1);

	/* Enable DIF (block guard) only if configured to do so. */
	if (phba->cfg_enable_bg)
		bf_set(lpfc_mbx_rq_ftr_rq_dif, &mboxq->u.mqe.un.req_ftrs, 1);

	/* Enable NPIV only if configured to do so. */
	if (phba->max_vpi && phba->cfg_enable_npiv)
		bf_set(lpfc_mbx_rq_ftr_rq_npiv, &mboxq->u.mqe.un.req_ftrs, 1);

	if (phba->nvmet_support) {
		bf_set(lpfc_mbx_rq_ftr_rq_mrqp, &mboxq->u.mqe.un.req_ftrs, 1);
		/* iaab/iaar NOT set for now */
		 bf_set(lpfc_mbx_rq_ftr_rq_iaab, &mboxq->u.mqe.un.req_ftrs, 0);
		 bf_set(lpfc_mbx_rq_ftr_rq_iaar, &mboxq->u.mqe.un.req_ftrs, 0);
	}
	return;
}

/**
 * lpfc_init_vfi - Initialize the INIT_VFI mailbox command
 * @mbox: pointer to lpfc mbox command to initialize.
 * @vport: Vport associated with the VF.
 *
 * This routine initializes @mbox to all zeros and then fills in the mailbox
 * fields from @vport. INIT_VFI configures virtual fabrics identified by VFI
 * in the context of an FCF. The driver issues this command to setup a VFI
 * before issuing a FLOGI to login to the VSAN. The driver should also issue a
 * REG_VFI after a successful VSAN login.
 **/
void
lpfc_init_vfi(struct lpfcMboxq *mbox, struct lpfc_vport *vport)
{
	struct lpfc_mbx_init_vfi *init_vfi;

	memset(mbox, 0, sizeof(*mbox));
	mbox->vport = vport;
	init_vfi = &mbox->u.mqe.un.init_vfi;
	bf_set(lpfc_mqe_command, &mbox->u.mqe, MBX_INIT_VFI);
	bf_set(lpfc_init_vfi_vr, init_vfi, 1);
	bf_set(lpfc_init_vfi_vt, init_vfi, 1);
	bf_set(lpfc_init_vfi_vp, init_vfi, 1);
	bf_set(lpfc_init_vfi_vfi, init_vfi,
	       vport->phba->sli4_hba.vfi_ids[vport->vfi]);
	bf_set(lpfc_init_vfi_vpi, init_vfi,
	       vport->phba->vpi_ids[vport->vpi]);
	bf_set(lpfc_init_vfi_fcfi, init_vfi,
	       vport->phba->fcf.fcfi);
}

/**
 * lpfc_reg_vfi - Initialize the REG_VFI mailbox command
 * @mbox: pointer to lpfc mbox command to initialize.
 * @vport: vport associated with the VF.
 * @phys: BDE DMA bus address used to send the service parameters to the HBA.
 *
 * This routine initializes @mbox to all zeros and then fills in the mailbox
 * fields from @vport, and uses @buf as a DMAable buffer to send the vport's
 * fc service parameters to the HBA for this VFI. REG_VFI configures virtual
 * fabrics identified by VFI in the context of an FCF.
 **/
void
lpfc_reg_vfi(struct lpfcMboxq *mbox, struct lpfc_vport *vport, dma_addr_t phys)
{
	struct lpfc_mbx_reg_vfi *reg_vfi;
	struct lpfc_hba *phba = vport->phba;
	uint8_t bbscn_fabric = 0, bbscn_max = 0, bbscn_def = 0;

	memset(mbox, 0, sizeof(*mbox));
	reg_vfi = &mbox->u.mqe.un.reg_vfi;
	bf_set(lpfc_mqe_command, &mbox->u.mqe, MBX_REG_VFI);
	bf_set(lpfc_reg_vfi_vp, reg_vfi, 1);
	bf_set(lpfc_reg_vfi_vfi, reg_vfi,
	       phba->sli4_hba.vfi_ids[vport->vfi]);
	bf_set(lpfc_reg_vfi_fcfi, reg_vfi, phba->fcf.fcfi);
	bf_set(lpfc_reg_vfi_vpi, reg_vfi, phba->vpi_ids[vport->vpi]);
	memcpy(reg_vfi->wwn, &vport->fc_portname, sizeof(struct lpfc_name));
	reg_vfi->wwn[0] = cpu_to_le32(reg_vfi->wwn[0]);
	reg_vfi->wwn[1] = cpu_to_le32(reg_vfi->wwn[1]);
	reg_vfi->e_d_tov = phba->fc_edtov;
	reg_vfi->r_a_tov = phba->fc_ratov;
	if (phys) {
		reg_vfi->bde.addrHigh = putPaddrHigh(phys);
		reg_vfi->bde.addrLow = putPaddrLow(phys);
		reg_vfi->bde.tus.f.bdeSize = sizeof(vport->fc_sparam);
		reg_vfi->bde.tus.f.bdeFlags = BUFF_TYPE_BDE_64;
	}
	bf_set(lpfc_reg_vfi_nport_id, reg_vfi, vport->fc_myDID);

	/* Only FC supports upd bit */
	if ((phba->sli4_hba.lnk_info.lnk_tp == LPFC_LNK_TYPE_FC) &&
	    (vport->fc_flag & FC_VFI_REGISTERED) &&
	    (!phba->fc_topology_changed))
		bf_set(lpfc_reg_vfi_upd, reg_vfi, 1);

	bf_set(lpfc_reg_vfi_bbcr, reg_vfi, 0);
	bf_set(lpfc_reg_vfi_bbscn, reg_vfi, 0);
	bbscn_fabric = (phba->fc_fabparam.cmn.bbRcvSizeMsb >> 4) & 0xF;

	if (phba->bbcredit_support && phba->cfg_enable_bbcr  &&
	    bbscn_fabric != 0) {
		bbscn_max = bf_get(lpfc_bbscn_max,
				   &phba->sli4_hba.bbscn_params);
		if (bbscn_fabric <= bbscn_max) {
			bbscn_def = bf_get(lpfc_bbscn_def,
					   &phba->sli4_hba.bbscn_params);

			if (bbscn_fabric > bbscn_def)
				bf_set(lpfc_reg_vfi_bbscn, reg_vfi,
				       bbscn_fabric);
			else
				bf_set(lpfc_reg_vfi_bbscn, reg_vfi, bbscn_def);

			bf_set(lpfc_reg_vfi_bbcr, reg_vfi, 1);
		}
	}
	lpfc_printf_vlog(vport, KERN_INFO, LOG_MBOX,
			"3134 Register VFI, mydid:x%x, fcfi:%d, "
			" vfi:%d, vpi:%d, fc_pname:%x%x fc_flag:x%x"
			" port_state:x%x topology chg:%d bbscn_fabric :%d\n",
			vport->fc_myDID,
			phba->fcf.fcfi,
			phba->sli4_hba.vfi_ids[vport->vfi],
			phba->vpi_ids[vport->vpi],
			reg_vfi->wwn[0], reg_vfi->wwn[1], vport->fc_flag,
			vport->port_state, phba->fc_topology_changed,
			bbscn_fabric);
}

/**
 * lpfc_init_vpi - Initialize the INIT_VPI mailbox command
 * @phba: pointer to the hba structure to init the VPI for.
 * @mbox: pointer to lpfc mbox command to initialize.
 * @vpi: VPI to be initialized.
 *
 * The INIT_VPI mailbox command supports virtual N_Ports. The driver uses the
 * command to activate a virtual N_Port. The HBA assigns a MAC address to use
 * with the virtual N Port.  The SLI Host issues this command before issuing a
 * FDISC to connect to the Fabric. The SLI Host should issue a REG_VPI after a
 * successful virtual NPort login.
 **/
void
lpfc_init_vpi(struct lpfc_hba *phba, struct lpfcMboxq *mbox, uint16_t vpi)
{
	memset(mbox, 0, sizeof(*mbox));
	bf_set(lpfc_mqe_command, &mbox->u.mqe, MBX_INIT_VPI);
	bf_set(lpfc_init_vpi_vpi, &mbox->u.mqe.un.init_vpi,
	       phba->vpi_ids[vpi]);
	bf_set(lpfc_init_vpi_vfi, &mbox->u.mqe.un.init_vpi,
	       phba->sli4_hba.vfi_ids[phba->pport->vfi]);
}

/**
 * lpfc_unreg_vfi - Initialize the UNREG_VFI mailbox command
 * @mbox: pointer to lpfc mbox command to initialize.
 * @vport: vport associated with the VF.
 *
 * The UNREG_VFI mailbox command causes the SLI Host to put a virtual fabric
 * (logical NPort) into the inactive state. The SLI Host must have logged out
 * and unregistered all remote N_Ports to abort any activity on the virtual
 * fabric. The SLI Port posts the mailbox response after marking the virtual
 * fabric inactive.
 **/
void
lpfc_unreg_vfi(struct lpfcMboxq *mbox, struct lpfc_vport *vport)
{
	memset(mbox, 0, sizeof(*mbox));
	bf_set(lpfc_mqe_command, &mbox->u.mqe, MBX_UNREG_VFI);
	bf_set(lpfc_unreg_vfi_vfi, &mbox->u.mqe.un.unreg_vfi,
	       vport->phba->sli4_hba.vfi_ids[vport->vfi]);
}

/**
 * lpfc_sli4_dump_cfg_rg23 - Dump sli4 port config region 23
 * @phba: pointer to the hba structure containing.
 * @mbox: pointer to lpfc mbox command to initialize.
 *
 * This function create a SLI4 dump mailbox command to dump configure
 * region 23.
 **/
int
lpfc_sli4_dump_cfg_rg23(struct lpfc_hba *phba, struct lpfcMboxq *mbox)
{
	struct lpfc_dmabuf *mp = NULL;
	MAILBOX_t *mb;

	memset(mbox, 0, sizeof(*mbox));
	mb = &mbox->u.mb;

	mp = kmalloc(sizeof(struct lpfc_dmabuf), GFP_KERNEL);
	if (mp)
		mp->virt = lpfc_mbuf_alloc(phba, 0, &mp->phys);

	if (!mp || !mp->virt) {
		kfree(mp);
		/* dump config region 23 failed to allocate memory */
		lpfc_printf_log(phba, KERN_WARNING, LOG_MBOX,
			"2569 lpfc dump config region 23: memory"
			" allocation failed\n");
		return 1;
	}

	memset(mp->virt, 0, LPFC_BPL_SIZE);
	INIT_LIST_HEAD(&mp->list);

	/* save address for completion */
	mbox->ctx_buf = (uint8_t *)mp;

	mb->mbxCommand = MBX_DUMP_MEMORY;
	mb->un.varDmp.type = DMP_NV_PARAMS;
	mb->un.varDmp.region_id = DMP_REGION_23;
	mb->un.varDmp.sli4_length = DMP_RGN23_SIZE;
	mb->un.varWords[3] = putPaddrLow(mp->phys);
	mb->un.varWords[4] = putPaddrHigh(mp->phys);
	return 0;
}

static void
lpfc_mbx_cmpl_rdp_link_stat(struct lpfc_hba *phba, LPFC_MBOXQ_t *mboxq)
{
	MAILBOX_t *mb;
	int rc = FAILURE;
	struct lpfc_rdp_context *rdp_context =
			(struct lpfc_rdp_context *)(mboxq->ctx_ndlp);

	mb = &mboxq->u.mb;
	if (mb->mbxStatus)
		goto mbx_failed;

	memcpy(&rdp_context->link_stat, &mb->un.varRdLnk, sizeof(READ_LNK_VAR));

	rc = SUCCESS;

mbx_failed:
	lpfc_sli4_mbox_cmd_free(phba, mboxq);
	rdp_context->cmpl(phba, rdp_context, rc);
}

static void
lpfc_mbx_cmpl_rdp_page_a2(struct lpfc_hba *phba, LPFC_MBOXQ_t *mbox)
{
	struct lpfc_dmabuf *mp = (struct lpfc_dmabuf *)mbox->ctx_buf;
	struct lpfc_rdp_context *rdp_context =
			(struct lpfc_rdp_context *)(mbox->ctx_ndlp);

	if (bf_get(lpfc_mqe_status, &mbox->u.mqe))
		goto error_mbuf_free;

	lpfc_sli_bemem_bcopy(mp->virt, &rdp_context->page_a2,
				DMP_SFF_PAGE_A2_SIZE);

	/* We don't need dma buffer for link stat. */
	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);

	memset(mbox, 0, sizeof(*mbox));
	lpfc_read_lnk_stat(phba, mbox);
	mbox->vport = rdp_context->ndlp->vport;
	mbox->mbox_cmpl = lpfc_mbx_cmpl_rdp_link_stat;
	mbox->ctx_ndlp = (struct lpfc_rdp_context *)rdp_context;
	if (lpfc_sli_issue_mbox(phba, mbox, MBX_NOWAIT) == MBX_NOT_FINISHED)
		goto error_cmd_free;

	return;

error_mbuf_free:
	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
error_cmd_free:
	lpfc_sli4_mbox_cmd_free(phba, mbox);
	rdp_context->cmpl(phba, rdp_context, FAILURE);
}

void
lpfc_mbx_cmpl_rdp_page_a0(struct lpfc_hba *phba, LPFC_MBOXQ_t *mbox)
{
	int rc;
	struct lpfc_dmabuf *mp = (struct lpfc_dmabuf *)(mbox->ctx_buf);
	struct lpfc_rdp_context *rdp_context =
			(struct lpfc_rdp_context *)(mbox->ctx_ndlp);

	if (bf_get(lpfc_mqe_status, &mbox->u.mqe))
		goto error;

	lpfc_sli_bemem_bcopy(mp->virt, &rdp_context->page_a0,
				DMP_SFF_PAGE_A0_SIZE);

	memset(mbox, 0, sizeof(*mbox));

	memset(mp->virt, 0, DMP_SFF_PAGE_A2_SIZE);
	INIT_LIST_HEAD(&mp->list);

	/* save address for completion */
	mbox->ctx_buf = mp;
	mbox->vport = rdp_context->ndlp->vport;

	bf_set(lpfc_mqe_command, &mbox->u.mqe, MBX_DUMP_MEMORY);
	bf_set(lpfc_mbx_memory_dump_type3_type,
		&mbox->u.mqe.un.mem_dump_type3, DMP_LMSD);
	bf_set(lpfc_mbx_memory_dump_type3_link,
		&mbox->u.mqe.un.mem_dump_type3, phba->sli4_hba.physical_port);
	bf_set(lpfc_mbx_memory_dump_type3_page_no,
		&mbox->u.mqe.un.mem_dump_type3, DMP_PAGE_A2);
	bf_set(lpfc_mbx_memory_dump_type3_length,
		&mbox->u.mqe.un.mem_dump_type3, DMP_SFF_PAGE_A2_SIZE);
	mbox->u.mqe.un.mem_dump_type3.addr_lo = putPaddrLow(mp->phys);
	mbox->u.mqe.un.mem_dump_type3.addr_hi = putPaddrHigh(mp->phys);

	mbox->mbox_cmpl = lpfc_mbx_cmpl_rdp_page_a2;
	mbox->ctx_ndlp = (struct lpfc_rdp_context *)rdp_context;
	rc = lpfc_sli_issue_mbox(phba, mbox, MBX_NOWAIT);
	if (rc == MBX_NOT_FINISHED)
		goto error;

	return;

error:
	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
	lpfc_sli4_mbox_cmd_free(phba, mbox);
	rdp_context->cmpl(phba, rdp_context, FAILURE);
}


/*
 * lpfc_sli4_dump_sfp_pagea0 - Dump sli4 read SFP Diagnostic.
 * @phba: pointer to the hba structure containing.
 * @mbox: pointer to lpfc mbox command to initialize.
 *
 * This function create a SLI4 dump mailbox command to dump configure
 * type 3 page 0xA0.
 */
int
lpfc_sli4_dump_page_a0(struct lpfc_hba *phba, struct lpfcMboxq *mbox)
{
	struct lpfc_dmabuf *mp = NULL;

	memset(mbox, 0, sizeof(*mbox));

	mp = kmalloc(sizeof(struct lpfc_dmabuf), GFP_KERNEL);
	if (mp)
		mp->virt = lpfc_mbuf_alloc(phba, 0, &mp->phys);
	if (!mp || !mp->virt) {
		kfree(mp);
		lpfc_printf_log(phba, KERN_WARNING, LOG_MBOX,
			"3569 dump type 3 page 0xA0 allocation failed\n");
		return 1;
	}

	memset(mp->virt, 0, LPFC_BPL_SIZE);
	INIT_LIST_HEAD(&mp->list);

	bf_set(lpfc_mqe_command, &mbox->u.mqe, MBX_DUMP_MEMORY);
	/* save address for completion */
	mbox->ctx_buf = mp;

	bf_set(lpfc_mbx_memory_dump_type3_type,
		&mbox->u.mqe.un.mem_dump_type3, DMP_LMSD);
	bf_set(lpfc_mbx_memory_dump_type3_link,
		&mbox->u.mqe.un.mem_dump_type3, phba->sli4_hba.physical_port);
	bf_set(lpfc_mbx_memory_dump_type3_page_no,
		&mbox->u.mqe.un.mem_dump_type3, DMP_PAGE_A0);
	bf_set(lpfc_mbx_memory_dump_type3_length,
		&mbox->u.mqe.un.mem_dump_type3, DMP_SFF_PAGE_A0_SIZE);
	mbox->u.mqe.un.mem_dump_type3.addr_lo = putPaddrLow(mp->phys);
	mbox->u.mqe.un.mem_dump_type3.addr_hi = putPaddrHigh(mp->phys);

	return 0;
}

/**
 * lpfc_reg_fcfi - Initialize the REG_FCFI mailbox command
 * @phba: pointer to the hba structure containing the FCF index and RQ ID.
 * @mbox: pointer to lpfc mbox command to initialize.
 *
 * The REG_FCFI mailbox command supports Fibre Channel Forwarders (FCFs). The
 * SLI Host uses the command to activate an FCF after it has acquired FCF
 * information via a READ_FCF mailbox command. This mailbox command also is used
 * to indicate where received unsolicited frames from this FCF will be sent. By
 * default this routine will set up the FCF to forward all unsolicited frames
 * the the RQ ID passed in the @phba. This can be overridden by the caller for
 * more complicated setups.
 **/
void
lpfc_reg_fcfi(struct lpfc_hba *phba, struct lpfcMboxq *mbox)
{
	struct lpfc_mbx_reg_fcfi *reg_fcfi;

	memset(mbox, 0, sizeof(*mbox));
	reg_fcfi = &mbox->u.mqe.un.reg_fcfi;
	bf_set(lpfc_mqe_command, &mbox->u.mqe, MBX_REG_FCFI);
	if (phba->nvmet_support == 0) {
		bf_set(lpfc_reg_fcfi_rq_id0, reg_fcfi,
		       phba->sli4_hba.hdr_rq->queue_id);
		/* Match everything - rq_id0 */
		bf_set(lpfc_reg_fcfi_type_match0, reg_fcfi, 0);
		bf_set(lpfc_reg_fcfi_type_mask0, reg_fcfi, 0);
		bf_set(lpfc_reg_fcfi_rctl_match0, reg_fcfi, 0);
		bf_set(lpfc_reg_fcfi_rctl_mask0, reg_fcfi, 0);

		bf_set(lpfc_reg_fcfi_rq_id1, reg_fcfi, REG_FCF_INVALID_QID);

		/* addr mode is bit wise inverted value of fcf addr_mode */
		bf_set(lpfc_reg_fcfi_mam, reg_fcfi,
		       (~phba->fcf.addr_mode) & 0x3);
	} else {
		/* This is ONLY for NVMET MRQ == 1 */
		if (phba->cfg_nvmet_mrq != 1)
			return;

		bf_set(lpfc_reg_fcfi_rq_id0, reg_fcfi,
		       phba->sli4_hba.nvmet_mrq_hdr[0]->queue_id);
		/* Match type FCP - rq_id0 */
		bf_set(lpfc_reg_fcfi_type_match0, reg_fcfi, FC_TYPE_FCP);
		bf_set(lpfc_reg_fcfi_type_mask0, reg_fcfi, 0xff);
		bf_set(lpfc_reg_fcfi_rctl_match0, reg_fcfi,
		       FC_RCTL_DD_UNSOL_CMD);

		bf_set(lpfc_reg_fcfi_rq_id1, reg_fcfi,
		       phba->sli4_hba.hdr_rq->queue_id);
		/* Match everything else - rq_id1 */
		bf_set(lpfc_reg_fcfi_type_match1, reg_fcfi, 0);
		bf_set(lpfc_reg_fcfi_type_mask1, reg_fcfi, 0);
		bf_set(lpfc_reg_fcfi_rctl_match1, reg_fcfi, 0);
		bf_set(lpfc_reg_fcfi_rctl_mask1, reg_fcfi, 0);
	}
	bf_set(lpfc_reg_fcfi_rq_id2, reg_fcfi, REG_FCF_INVALID_QID);
	bf_set(lpfc_reg_fcfi_rq_id3, reg_fcfi, REG_FCF_INVALID_QID);
	bf_set(lpfc_reg_fcfi_info_index, reg_fcfi,
	       phba->fcf.current_rec.fcf_indx);
	if (phba->fcf.current_rec.vlan_id != LPFC_FCOE_NULL_VID) {
		bf_set(lpfc_reg_fcfi_vv, reg_fcfi, 1);
		bf_set(lpfc_reg_fcfi_vlan_tag, reg_fcfi,
		       phba->fcf.current_rec.vlan_id);
	}
}

/**
 * lpfc_reg_fcfi_mrq - Initialize the REG_FCFI_MRQ mailbox command
 * @phba: pointer to the hba structure containing the FCF index and RQ ID.
 * @mbox: pointer to lpfc mbox command to initialize.
 * @mode: 0 to register FCFI, 1 to register MRQs
 *
 * The REG_FCFI_MRQ mailbox command supports Fibre Channel Forwarders (FCFs).
 * The SLI Host uses the command to activate an FCF after it has acquired FCF
 * information via a READ_FCF mailbox command. This mailbox command also is used
 * to indicate where received unsolicited frames from this FCF will be sent. By
 * default this routine will set up the FCF to forward all unsolicited frames
 * the the RQ ID passed in the @phba. This can be overridden by the caller for
 * more complicated setups.
 **/
void
lpfc_reg_fcfi_mrq(struct lpfc_hba *phba, struct lpfcMboxq *mbox, int mode)
{
	struct lpfc_mbx_reg_fcfi_mrq *reg_fcfi;

	/* This is ONLY for MRQ */
	if (phba->cfg_nvmet_mrq <= 1)
		return;

	memset(mbox, 0, sizeof(*mbox));
	reg_fcfi = &mbox->u.mqe.un.reg_fcfi_mrq;
	bf_set(lpfc_mqe_command, &mbox->u.mqe, MBX_REG_FCFI_MRQ);
	if (mode == 0) {
		bf_set(lpfc_reg_fcfi_mrq_info_index, reg_fcfi,
		       phba->fcf.current_rec.fcf_indx);
		if (phba->fcf.current_rec.vlan_id != LPFC_FCOE_NULL_VID) {
			bf_set(lpfc_reg_fcfi_mrq_vv, reg_fcfi, 1);
			bf_set(lpfc_reg_fcfi_mrq_vlan_tag, reg_fcfi,
			       phba->fcf.current_rec.vlan_id);
		}
		return;
	}

	bf_set(lpfc_reg_fcfi_mrq_rq_id0, reg_fcfi,
	       phba->sli4_hba.nvmet_mrq_hdr[0]->queue_id);
	/* Match NVME frames of type FCP (protocol NVME) - rq_id0 */
	bf_set(lpfc_reg_fcfi_mrq_type_match0, reg_fcfi, FC_TYPE_FCP);
	bf_set(lpfc_reg_fcfi_mrq_type_mask0, reg_fcfi, 0xff);
	bf_set(lpfc_reg_fcfi_mrq_rctl_match0, reg_fcfi, FC_RCTL_DD_UNSOL_CMD);
	bf_set(lpfc_reg_fcfi_mrq_rctl_mask0, reg_fcfi, 0xff);
	bf_set(lpfc_reg_fcfi_mrq_ptc0, reg_fcfi, 1);
	bf_set(lpfc_reg_fcfi_mrq_pt0, reg_fcfi, 1);

	bf_set(lpfc_reg_fcfi_mrq_policy, reg_fcfi, 3); /* NVME connection id */
	bf_set(lpfc_reg_fcfi_mrq_mode, reg_fcfi, 1);
	bf_set(lpfc_reg_fcfi_mrq_filter, reg_fcfi, 1); /* rq_id0 */
	bf_set(lpfc_reg_fcfi_mrq_npairs, reg_fcfi, phba->cfg_nvmet_mrq);

	bf_set(lpfc_reg_fcfi_mrq_rq_id1, reg_fcfi,
	       phba->sli4_hba.hdr_rq->queue_id);
	/* Match everything - rq_id1 */
	bf_set(lpfc_reg_fcfi_mrq_type_match1, reg_fcfi, 0);
	bf_set(lpfc_reg_fcfi_mrq_type_mask1, reg_fcfi, 0);
	bf_set(lpfc_reg_fcfi_mrq_rctl_match1, reg_fcfi, 0);
	bf_set(lpfc_reg_fcfi_mrq_rctl_mask1, reg_fcfi, 0);

	bf_set(lpfc_reg_fcfi_mrq_rq_id2, reg_fcfi, REG_FCF_INVALID_QID);
	bf_set(lpfc_reg_fcfi_mrq_rq_id3, reg_fcfi, REG_FCF_INVALID_QID);
}

/**
 * lpfc_unreg_fcfi - Initialize the UNREG_FCFI mailbox command
 * @mbox: pointer to lpfc mbox command to initialize.
 * @fcfi: FCFI to be unregistered.
 *
 * The UNREG_FCFI mailbox command supports Fibre Channel Forwarders (FCFs).
 * The SLI Host uses the command to inactivate an FCFI.
 **/
void
lpfc_unreg_fcfi(struct lpfcMboxq *mbox, uint16_t fcfi)
{
	memset(mbox, 0, sizeof(*mbox));
	bf_set(lpfc_mqe_command, &mbox->u.mqe, MBX_UNREG_FCFI);
	bf_set(lpfc_unreg_fcfi, &mbox->u.mqe.un.unreg_fcfi, fcfi);
}

/**
 * lpfc_resume_rpi - Initialize the RESUME_RPI mailbox command
 * @mbox: pointer to lpfc mbox command to initialize.
 * @ndlp: The nodelist structure that describes the RPI to resume.
 *
 * The RESUME_RPI mailbox command is used to restart I/O to an RPI after a
 * link event.
 **/
void
lpfc_resume_rpi(struct lpfcMboxq *mbox, struct lpfc_nodelist *ndlp)
{
	struct lpfc_hba *phba = ndlp->phba;
	struct lpfc_mbx_resume_rpi *resume_rpi;

	memset(mbox, 0, sizeof(*mbox));
	resume_rpi = &mbox->u.mqe.un.resume_rpi;
	bf_set(lpfc_mqe_command, &mbox->u.mqe, MBX_RESUME_RPI);
	bf_set(lpfc_resume_rpi_index, resume_rpi,
	       phba->sli4_hba.rpi_ids[ndlp->nlp_rpi]);
	bf_set(lpfc_resume_rpi_ii, resume_rpi, RESUME_INDEX_RPI);
	resume_rpi->event_tag = ndlp->phba->fc_eventTag;
}

/**
 * lpfc_supported_pages - Initialize the PORT_CAPABILITIES supported pages
 *                        mailbox command.
 * @mbox: pointer to lpfc mbox command to initialize.
 *
 * The PORT_CAPABILITIES supported pages mailbox command is issued to
 * retrieve the particular feature pages supported by the port.
 **/
void
lpfc_supported_pages(struct lpfcMboxq *mbox)
{
	struct lpfc_mbx_supp_pages *supp_pages;

	memset(mbox, 0, sizeof(*mbox));
	supp_pages = &mbox->u.mqe.un.supp_pages;
	bf_set(lpfc_mqe_command, &mbox->u.mqe, MBX_PORT_CAPABILITIES);
	bf_set(cpn, supp_pages, LPFC_SUPP_PAGES);
}

/**
 * lpfc_pc_sli4_params - Initialize the PORT_CAPABILITIES SLI4 Params mbox cmd.
 * @mbox: pointer to lpfc mbox command to initialize.
 *
 * The PORT_CAPABILITIES SLI4 parameters mailbox command is issued to
 * retrieve the particular SLI4 features supported by the port.
 **/
void
lpfc_pc_sli4_params(struct lpfcMboxq *mbox)
{
	struct lpfc_mbx_pc_sli4_params *sli4_params;

	memset(mbox, 0, sizeof(*mbox));
	sli4_params = &mbox->u.mqe.un.sli4_params;
	bf_set(lpfc_mqe_command, &mbox->u.mqe, MBX_PORT_CAPABILITIES);
	bf_set(cpn, sli4_params, LPFC_SLI4_PARAMETERS);
}
