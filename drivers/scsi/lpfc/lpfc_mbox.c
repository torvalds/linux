/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2004-2008 Emulex.  All rights reserved.           *
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
#include "lpfc_nl.h"
#include "lpfc_disc.h"
#include "lpfc_scsi.h"
#include "lpfc.h"
#include "lpfc_logmsg.h"
#include "lpfc_crtn.h"
#include "lpfc_compat.h"

/**
 * lpfc_dump_mem - Prepare a mailbox command for retrieving HBA's VPD memory
 * @phba: pointer to lpfc hba data structure.
 * @pmb: pointer to the driver internal queue element for mailbox command.
 * @offset: offset for dumping VPD memory mailbox command.
 *
 * The dump mailbox command provides a method for the device driver to obtain
 * various types of information from the HBA device.
 *
 * This routine prepares the mailbox command for dumping HBA Vital Product
 * Data (VPD) memory. This mailbox command is to be used for retrieving a
 * portion (DMP_RSP_SIZE bytes) of a HBA's VPD from the HBA at an address
 * offset specified by the offset parameter.
 **/
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

	mb = &pmb->mb;
	/* Save context so that we can restore after memset */
	ctx = pmb->context2;

	/* Setup to dump VPD region */
	memset(pmb, 0, sizeof(LPFC_MBOXQ_t));
	mb->mbxCommand = MBX_DUMP_MEMORY;
	mb->mbxOwner = OWN_HOST;
	mb->un.varDmp.cv = 1;
	mb->un.varDmp.type = DMP_NV_PARAMS;
	mb->un.varDmp.entry_index = 0;
	mb->un.varDmp.region_id = WAKE_UP_PARMS_REGION_ID;
	mb->un.varDmp.word_cnt = WAKE_UP_PARMS_WORD_SIZE;
	mb->un.varDmp.co = 0;
	mb->un.varDmp.resp_offset = 0;
	pmb->context2 = ctx;
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

	mb = &pmb->mb;
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

	mb = &pmb->mb;
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

	mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));
	mb->mbxCommand = MBX_HEARTBEAT;
	mb->mbxOwner = OWN_HOST;
	return;
}

/**
 * lpfc_read_la - Prepare a mailbox command for reading HBA link attention
 * @phba: pointer to lpfc hba data structure.
 * @pmb: pointer to the driver internal queue element for mailbox command.
 * @mp: DMA buffer memory for reading the link attention information into.
 *
 * The read link attention mailbox command is issued to read the Link Event
 * Attention information indicated by the HBA port when the Link Event bit
 * of the Host Attention (HSTATT) register is set to 1. A Link Event
 * Attention occurs based on an exception detected at the Fibre Channel link
 * interface.
 *
 * This routine prepares the mailbox command for reading HBA link attention
 * information. A DMA memory has been set aside and address passed to the
 * HBA through @mp for the HBA to DMA link attention information into the
 * memory as part of the execution of the mailbox command.
 *
 * Return codes
 *    0 - Success (currently always return 0)
 **/
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

	mb = &pmb->mb;
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
	MAILBOX_t *mb = &pmb->mb;
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
	struct lpfc_sli *psli;

	psli = &phba->sli;
	mb = &pmb->mb;
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
	mb->un.varRdSparm.vpi = vpi;

	/* save address for completion */
	pmb->context1 = mp;

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

	mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	mb->un.varUnregDID.did = did;
	mb->un.varUnregDID.vpi = vpi;

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

	mb = &pmb->mb;
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

	mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	mb->mbxCommand = MBX_READ_LNK_STAT;
	mb->mbxOwner = OWN_HOST;
	return;
}

/**
 * lpfc_reg_login - Prepare a mailbox command for registering remote login
 * @phba: pointer to lpfc hba data structure.
 * @vpi: virtual N_Port identifier.
 * @did: remote port identifier.
 * @param: pointer to memory holding the server parameters.
 * @pmb: pointer to the driver internal queue element for mailbox command.
 * @flag: action flag to be passed back for the complete function.
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
	mp = kmalloc(sizeof (struct lpfc_dmabuf), GFP_KERNEL);
	if (mp)
		mp->virt = lpfc_mbuf_alloc(phba, 0, &mp->phys);
	if (!mp || !mp->virt) {
		kfree(mp);
		mb->mbxCommand = MBX_REG_LOGIN64;
		/* REG_LOGIN: no buffers */
		lpfc_printf_log(phba, KERN_WARNING, LOG_MBOX,
				"0302 REG_LOGIN: no buffers, VPI:%d DID:x%x, "
				"flag x%x\n", vpi, did, flag);
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
 **/
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
	MAILBOX_t *mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	mb->un.varUnregVpi.vpi = vpi;

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
		pring->cmdringaddr = (void *)&phba->IOCBs[iocbCnt];
		pcbp->rdsc[i].cmdEntries = pring->numCiocb;

		offset = (uint8_t *) &phba->IOCBs[iocbCnt] -
			 (uint8_t *) phba->slim2p.virt;
		pdma_addr = phba->slim2p.phys + offset;
		pcbp->rdsc[i].cmdAddrHigh = putPaddrHigh(pdma_addr);
		pcbp->rdsc[i].cmdAddrLow = putPaddrLow(pdma_addr);
		iocbCnt += pring->numCiocb;

		/* Response ring setup for ring */
		pring->rspringaddr = (void *) &phba->IOCBs[iocbCnt];

		pcbp->rdsc[i].rspEntries = pring->numRiocb;
		offset = (uint8_t *)&phba->IOCBs[iocbCnt] -
			 (uint8_t *)phba->slim2p.virt;
		pdma_addr = phba->slim2p.phys + offset;
		pcbp->rdsc[i].rspAddrHigh = putPaddrHigh(pdma_addr);
		pcbp->rdsc[i].rspAddrLow = putPaddrLow(pdma_addr);
		iocbCnt += pring->numRiocb;
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
	MAILBOX_t *mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));
	mb->un.varRdRev.cv = 1;
	mb->un.varRdRev.v3req = 1; /* Request SLI3 info */
	mb->mbxCommand = MBX_READ_REV;
	mb->mbxOwner = OWN_HOST;
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
	MAILBOX_t *mb = &pmb->mb;
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

	offset = (uint8_t *)phba->pcb - (uint8_t *)phba->slim2p.virt;
	pdma_addr = phba->slim2p.phys + offset;
	mb->un.varCfgPort.pcbLow = putPaddrLow(pdma_addr);
	mb->un.varCfgPort.pcbHigh = putPaddrHigh(pdma_addr);

	/* Always Host Group Pointer is in SLIM */
	mb->un.varCfgPort.hps = 1;

	/* If HBA supports SLI=3 ask for it */

	if (phba->sli_rev == 3 && phba->vpd.sli3Feat.cerbm) {
		if (phba->cfg_enable_bg)
			mb->un.varCfgPort.cbg = 1; /* configure BlockGuard */
		mb->un.varCfgPort.cerbm = 1; /* Request HBQs */
		mb->un.varCfgPort.ccrp = 1; /* Command Ring Polling */
		mb->un.varCfgPort.cinb = 1; /* Interrupt Notification Block */
		mb->un.varCfgPort.max_hbq = lpfc_sli_hbq_count();
		if (phba->max_vpi && phba->cfg_enable_npiv &&
		    phba->vpd.sli3Feat.cmv) {
			mb->un.varCfgPort.max_vpi = phba->max_vpi;
			mb->un.varCfgPort.cmv = 1;
		} else
			mb->un.varCfgPort.max_vpi = phba->max_vpi = 0;
	} else
		phba->sli_rev = 2;
	mb->un.varCfgPort.sli_mode = phba->sli_rev;

	/* Now setup pcb */
	phba->pcb->type = TYPE_NATIVE_SLI2;
	phba->pcb->feature = FEATURE_INITIAL_SLI2;

	/* Setup Mailbox pointers */
	phba->pcb->mailBoxSize = sizeof(MAILBOX_t);
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

	for (i=0; i < phba->sli.num_rings; i++) {
		lpfc_memcpy_to_slim(phba->host_gp + i, &hgp,
				    sizeof(*phba->host_gp));
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
	MAILBOX_t *mb = &pmb->mb;

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
 * lpfc_mbox_cmpl_put - Put mailbox command into mailbox command complete list
 * @phba: pointer to lpfc hba data structure.
 * @mbq: pointer to the driver internal queue element for mailbox command.
 *
 * This routine put the completed mailbox command into the mailbox command
 * complete list. This routine is called from driver interrupt handler
 * context.The mailbox complete list is used by the driver worker thread
 * to process mailbox complete callback functions outside the driver interrupt
 * handler.
 **/
void
lpfc_mbox_cmpl_put(struct lpfc_hba * phba, LPFC_MBOXQ_t * mbq)
{
	unsigned long iflag;

	/* This function expects to be called from interrupt context */
	spin_lock_irqsave(&phba->hbalock, iflag);
	list_add_tail(&mbq->list, &phba->sli.mboxq_cmpl);
	spin_unlock_irqrestore(&phba->hbalock, iflag);
	return;
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
lpfc_mbox_tmo_val(struct lpfc_hba *phba, int cmd)
{
	switch (cmd) {
	case MBX_WRITE_NV:	/* 0x03 */
	case MBX_UPDATE_CFG:	/* 0x1B */
	case MBX_DOWN_LOAD:	/* 0x1C */
	case MBX_DEL_LD_ENTRY:	/* 0x1D */
	case MBX_LOAD_AREA:	/* 0x81 */
	case MBX_WRITE_WWN:     /* 0x98 */
	case MBX_LOAD_EXP_ROM:	/* 0x9C */
		return LPFC_MBOX_TMO_FLASH_CMD;
	}
	return LPFC_MBOX_TMO;
}
