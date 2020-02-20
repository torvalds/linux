/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2017-2019 Broadcom. All Rights Reserved. The term *
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.  *
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
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/ctype.h>
#include <linux/aer.h>
#include <linux/slab.h>
#include <linux/firmware.h>
#include <linux/miscdevice.h>
#include <linux/percpu.h>
#include <linux/msi.h>
#include <linux/irq.h>
#include <linux/bitops.h>
#include <linux/crash_dump.h>
#include <linux/cpu.h>
#include <linux/cpuhotplug.h>

#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport_fc.h>
#include <scsi/scsi_tcq.h>
#include <scsi/fc/fc_fs.h>

#include <linux/nvme-fc-driver.h>

#include "lpfc_hw4.h"
#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_sli4.h"
#include "lpfc_nl.h"
#include "lpfc_disc.h"
#include "lpfc.h"
#include "lpfc_scsi.h"
#include "lpfc_nvme.h"
#include "lpfc_nvmet.h"
#include "lpfc_logmsg.h"
#include "lpfc_crtn.h"
#include "lpfc_vport.h"
#include "lpfc_version.h"
#include "lpfc_ids.h"

static enum cpuhp_state lpfc_cpuhp_state;
/* Used when mapping IRQ vectors in a driver centric manner */
static uint32_t lpfc_present_cpu;

static void __lpfc_cpuhp_remove(struct lpfc_hba *phba);
static void lpfc_cpuhp_remove(struct lpfc_hba *phba);
static void lpfc_cpuhp_add(struct lpfc_hba *phba);
static void lpfc_get_hba_model_desc(struct lpfc_hba *, uint8_t *, uint8_t *);
static int lpfc_post_rcv_buf(struct lpfc_hba *);
static int lpfc_sli4_queue_verify(struct lpfc_hba *);
static int lpfc_create_bootstrap_mbox(struct lpfc_hba *);
static int lpfc_setup_endian_order(struct lpfc_hba *);
static void lpfc_destroy_bootstrap_mbox(struct lpfc_hba *);
static void lpfc_free_els_sgl_list(struct lpfc_hba *);
static void lpfc_free_nvmet_sgl_list(struct lpfc_hba *);
static void lpfc_init_sgl_list(struct lpfc_hba *);
static int lpfc_init_active_sgl_array(struct lpfc_hba *);
static void lpfc_free_active_sgl(struct lpfc_hba *);
static int lpfc_hba_down_post_s3(struct lpfc_hba *phba);
static int lpfc_hba_down_post_s4(struct lpfc_hba *phba);
static int lpfc_sli4_cq_event_pool_create(struct lpfc_hba *);
static void lpfc_sli4_cq_event_pool_destroy(struct lpfc_hba *);
static void lpfc_sli4_cq_event_release_all(struct lpfc_hba *);
static void lpfc_sli4_disable_intr(struct lpfc_hba *);
static uint32_t lpfc_sli4_enable_intr(struct lpfc_hba *, uint32_t);
static void lpfc_sli4_oas_verify(struct lpfc_hba *phba);
static uint16_t lpfc_find_cpu_handle(struct lpfc_hba *, uint16_t, int);
static void lpfc_setup_bg(struct lpfc_hba *, struct Scsi_Host *);

static struct scsi_transport_template *lpfc_transport_template = NULL;
static struct scsi_transport_template *lpfc_vport_transport_template = NULL;
static DEFINE_IDR(lpfc_hba_index);
#define LPFC_NVMET_BUF_POST 254

/**
 * lpfc_config_port_prep - Perform lpfc initialization prior to config port
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine will do LPFC initialization prior to issuing the CONFIG_PORT
 * mailbox command. It retrieves the revision information from the HBA and
 * collects the Vital Product Data (VPD) about the HBA for preparing the
 * configuration of the HBA.
 *
 * Return codes:
 *   0 - success.
 *   -ERESTART - requests the SLI layer to reset the HBA and try again.
 *   Any other value - indicates an error.
 **/
int
lpfc_config_port_prep(struct lpfc_hba *phba)
{
	lpfc_vpd_t *vp = &phba->vpd;
	int i = 0, rc;
	LPFC_MBOXQ_t *pmb;
	MAILBOX_t *mb;
	char *lpfc_vpd_data = NULL;
	uint16_t offset = 0;
	static char licensed[56] =
		    "key unlock for use with gnu public licensed code only\0";
	static int init_key = 1;

	pmb = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!pmb) {
		phba->link_state = LPFC_HBA_ERROR;
		return -ENOMEM;
	}

	mb = &pmb->u.mb;
	phba->link_state = LPFC_INIT_MBX_CMDS;

	if (lpfc_is_LC_HBA(phba->pcidev->device)) {
		if (init_key) {
			uint32_t *ptext = (uint32_t *) licensed;

			for (i = 0; i < 56; i += sizeof (uint32_t), ptext++)
				*ptext = cpu_to_be32(*ptext);
			init_key = 0;
		}

		lpfc_read_nv(phba, pmb);
		memset((char*)mb->un.varRDnvp.rsvd3, 0,
			sizeof (mb->un.varRDnvp.rsvd3));
		memcpy((char*)mb->un.varRDnvp.rsvd3, licensed,
			 sizeof (licensed));

		rc = lpfc_sli_issue_mbox(phba, pmb, MBX_POLL);

		if (rc != MBX_SUCCESS) {
			lpfc_printf_log(phba, KERN_ERR, LOG_MBOX,
					"0324 Config Port initialization "
					"error, mbxCmd x%x READ_NVPARM, "
					"mbxStatus x%x\n",
					mb->mbxCommand, mb->mbxStatus);
			mempool_free(pmb, phba->mbox_mem_pool);
			return -ERESTART;
		}
		memcpy(phba->wwnn, (char *)mb->un.varRDnvp.nodename,
		       sizeof(phba->wwnn));
		memcpy(phba->wwpn, (char *)mb->un.varRDnvp.portname,
		       sizeof(phba->wwpn));
	}

	/*
	 * Clear all option bits except LPFC_SLI3_BG_ENABLED,
	 * which was already set in lpfc_get_cfgparam()
	 */
	phba->sli3_options &= (uint32_t)LPFC_SLI3_BG_ENABLED;

	/* Setup and issue mailbox READ REV command */
	lpfc_read_rev(phba, pmb);
	rc = lpfc_sli_issue_mbox(phba, pmb, MBX_POLL);
	if (rc != MBX_SUCCESS) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0439 Adapter failed to init, mbxCmd x%x "
				"READ_REV, mbxStatus x%x\n",
				mb->mbxCommand, mb->mbxStatus);
		mempool_free( pmb, phba->mbox_mem_pool);
		return -ERESTART;
	}


	/*
	 * The value of rr must be 1 since the driver set the cv field to 1.
	 * This setting requires the FW to set all revision fields.
	 */
	if (mb->un.varRdRev.rr == 0) {
		vp->rev.rBit = 0;
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0440 Adapter failed to init, READ_REV has "
				"missing revision information.\n");
		mempool_free(pmb, phba->mbox_mem_pool);
		return -ERESTART;
	}

	if (phba->sli_rev == 3 && !mb->un.varRdRev.v3rsp) {
		mempool_free(pmb, phba->mbox_mem_pool);
		return -EINVAL;
	}

	/* Save information as VPD data */
	vp->rev.rBit = 1;
	memcpy(&vp->sli3Feat, &mb->un.varRdRev.sli3Feat, sizeof(uint32_t));
	vp->rev.sli1FwRev = mb->un.varRdRev.sli1FwRev;
	memcpy(vp->rev.sli1FwName, (char*) mb->un.varRdRev.sli1FwName, 16);
	vp->rev.sli2FwRev = mb->un.varRdRev.sli2FwRev;
	memcpy(vp->rev.sli2FwName, (char *) mb->un.varRdRev.sli2FwName, 16);
	vp->rev.biuRev = mb->un.varRdRev.biuRev;
	vp->rev.smRev = mb->un.varRdRev.smRev;
	vp->rev.smFwRev = mb->un.varRdRev.un.smFwRev;
	vp->rev.endecRev = mb->un.varRdRev.endecRev;
	vp->rev.fcphHigh = mb->un.varRdRev.fcphHigh;
	vp->rev.fcphLow = mb->un.varRdRev.fcphLow;
	vp->rev.feaLevelHigh = mb->un.varRdRev.feaLevelHigh;
	vp->rev.feaLevelLow = mb->un.varRdRev.feaLevelLow;
	vp->rev.postKernRev = mb->un.varRdRev.postKernRev;
	vp->rev.opFwRev = mb->un.varRdRev.opFwRev;

	/* If the sli feature level is less then 9, we must
	 * tear down all RPIs and VPIs on link down if NPIV
	 * is enabled.
	 */
	if (vp->rev.feaLevelHigh < 9)
		phba->sli3_options |= LPFC_SLI3_VPORT_TEARDOWN;

	if (lpfc_is_LC_HBA(phba->pcidev->device))
		memcpy(phba->RandomData, (char *)&mb->un.varWords[24],
						sizeof (phba->RandomData));

	/* Get adapter VPD information */
	lpfc_vpd_data = kmalloc(DMP_VPD_SIZE, GFP_KERNEL);
	if (!lpfc_vpd_data)
		goto out_free_mbox;
	do {
		lpfc_dump_mem(phba, pmb, offset, DMP_REGION_VPD);
		rc = lpfc_sli_issue_mbox(phba, pmb, MBX_POLL);

		if (rc != MBX_SUCCESS) {
			lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
					"0441 VPD not present on adapter, "
					"mbxCmd x%x DUMP VPD, mbxStatus x%x\n",
					mb->mbxCommand, mb->mbxStatus);
			mb->un.varDmp.word_cnt = 0;
		}
		/* dump mem may return a zero when finished or we got a
		 * mailbox error, either way we are done.
		 */
		if (mb->un.varDmp.word_cnt == 0)
			break;
		if (mb->un.varDmp.word_cnt > DMP_VPD_SIZE - offset)
			mb->un.varDmp.word_cnt = DMP_VPD_SIZE - offset;
		lpfc_sli_pcimem_bcopy(((uint8_t *)mb) + DMP_RSP_OFFSET,
				      lpfc_vpd_data + offset,
				      mb->un.varDmp.word_cnt);
		offset += mb->un.varDmp.word_cnt;
	} while (mb->un.varDmp.word_cnt && offset < DMP_VPD_SIZE);
	lpfc_parse_vpd(phba, lpfc_vpd_data, offset);

	kfree(lpfc_vpd_data);
out_free_mbox:
	mempool_free(pmb, phba->mbox_mem_pool);
	return 0;
}

/**
 * lpfc_config_async_cmpl - Completion handler for config async event mbox cmd
 * @phba: pointer to lpfc hba data structure.
 * @pmboxq: pointer to the driver internal queue element for mailbox command.
 *
 * This is the completion handler for driver's configuring asynchronous event
 * mailbox command to the device. If the mailbox command returns successfully,
 * it will set internal async event support flag to 1; otherwise, it will
 * set internal async event support flag to 0.
 **/
static void
lpfc_config_async_cmpl(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmboxq)
{
	if (pmboxq->u.mb.mbxStatus == MBX_SUCCESS)
		phba->temp_sensor_support = 1;
	else
		phba->temp_sensor_support = 0;
	mempool_free(pmboxq, phba->mbox_mem_pool);
	return;
}

/**
 * lpfc_dump_wakeup_param_cmpl - dump memory mailbox command completion handler
 * @phba: pointer to lpfc hba data structure.
 * @pmboxq: pointer to the driver internal queue element for mailbox command.
 *
 * This is the completion handler for dump mailbox command for getting
 * wake up parameters. When this command complete, the response contain
 * Option rom version of the HBA. This function translate the version number
 * into a human readable string and store it in OptionROMVersion.
 **/
static void
lpfc_dump_wakeup_param_cmpl(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmboxq)
{
	struct prog_id *prg;
	uint32_t prog_id_word;
	char dist = ' ';
	/* character array used for decoding dist type. */
	char dist_char[] = "nabx";

	if (pmboxq->u.mb.mbxStatus != MBX_SUCCESS) {
		mempool_free(pmboxq, phba->mbox_mem_pool);
		return;
	}

	prg = (struct prog_id *) &prog_id_word;

	/* word 7 contain option rom version */
	prog_id_word = pmboxq->u.mb.un.varWords[7];

	/* Decode the Option rom version word to a readable string */
	if (prg->dist < 4)
		dist = dist_char[prg->dist];

	if ((prg->dist == 3) && (prg->num == 0))
		snprintf(phba->OptionROMVersion, 32, "%d.%d%d",
			prg->ver, prg->rev, prg->lev);
	else
		snprintf(phba->OptionROMVersion, 32, "%d.%d%d%c%d",
			prg->ver, prg->rev, prg->lev,
			dist, prg->num);
	mempool_free(pmboxq, phba->mbox_mem_pool);
	return;
}

/**
 * lpfc_update_vport_wwn - Updates the fc_nodename, fc_portname,
 *	cfg_soft_wwnn, cfg_soft_wwpn
 * @vport: pointer to lpfc vport data structure.
 *
 *
 * Return codes
 *   None.
 **/
void
lpfc_update_vport_wwn(struct lpfc_vport *vport)
{
	uint8_t vvvl = vport->fc_sparam.cmn.valid_vendor_ver_level;
	u32 *fawwpn_key = (u32 *)&vport->fc_sparam.un.vendorVersion[0];

	/* If the soft name exists then update it using the service params */
	if (vport->phba->cfg_soft_wwnn)
		u64_to_wwn(vport->phba->cfg_soft_wwnn,
			   vport->fc_sparam.nodeName.u.wwn);
	if (vport->phba->cfg_soft_wwpn)
		u64_to_wwn(vport->phba->cfg_soft_wwpn,
			   vport->fc_sparam.portName.u.wwn);

	/*
	 * If the name is empty or there exists a soft name
	 * then copy the service params name, otherwise use the fc name
	 */
	if (vport->fc_nodename.u.wwn[0] == 0 || vport->phba->cfg_soft_wwnn)
		memcpy(&vport->fc_nodename, &vport->fc_sparam.nodeName,
			sizeof(struct lpfc_name));
	else
		memcpy(&vport->fc_sparam.nodeName, &vport->fc_nodename,
			sizeof(struct lpfc_name));

	/*
	 * If the port name has changed, then set the Param changes flag
	 * to unreg the login
	 */
	if (vport->fc_portname.u.wwn[0] != 0 &&
		memcmp(&vport->fc_portname, &vport->fc_sparam.portName,
			sizeof(struct lpfc_name)))
		vport->vport_flag |= FAWWPN_PARAM_CHG;

	if (vport->fc_portname.u.wwn[0] == 0 ||
	    vport->phba->cfg_soft_wwpn ||
	    (vvvl == 1 && cpu_to_be32(*fawwpn_key) == FAPWWN_KEY_VENDOR) ||
	    vport->vport_flag & FAWWPN_SET) {
		memcpy(&vport->fc_portname, &vport->fc_sparam.portName,
			sizeof(struct lpfc_name));
		vport->vport_flag &= ~FAWWPN_SET;
		if (vvvl == 1 && cpu_to_be32(*fawwpn_key) == FAPWWN_KEY_VENDOR)
			vport->vport_flag |= FAWWPN_SET;
	}
	else
		memcpy(&vport->fc_sparam.portName, &vport->fc_portname,
			sizeof(struct lpfc_name));
}

/**
 * lpfc_config_port_post - Perform lpfc initialization after config port
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine will do LPFC initialization after the CONFIG_PORT mailbox
 * command call. It performs all internal resource and state setups on the
 * port: post IOCB buffers, enable appropriate host interrupt attentions,
 * ELS ring timers, etc.
 *
 * Return codes
 *   0 - success.
 *   Any other value - error.
 **/
int
lpfc_config_port_post(struct lpfc_hba *phba)
{
	struct lpfc_vport *vport = phba->pport;
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	LPFC_MBOXQ_t *pmb;
	MAILBOX_t *mb;
	struct lpfc_dmabuf *mp;
	struct lpfc_sli *psli = &phba->sli;
	uint32_t status, timeout;
	int i, j;
	int rc;

	spin_lock_irq(&phba->hbalock);
	/*
	 * If the Config port completed correctly the HBA is not
	 * over heated any more.
	 */
	if (phba->over_temp_state == HBA_OVER_TEMP)
		phba->over_temp_state = HBA_NORMAL_TEMP;
	spin_unlock_irq(&phba->hbalock);

	pmb = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!pmb) {
		phba->link_state = LPFC_HBA_ERROR;
		return -ENOMEM;
	}
	mb = &pmb->u.mb;

	/* Get login parameters for NID.  */
	rc = lpfc_read_sparam(phba, pmb, 0);
	if (rc) {
		mempool_free(pmb, phba->mbox_mem_pool);
		return -ENOMEM;
	}

	pmb->vport = vport;
	if (lpfc_sli_issue_mbox(phba, pmb, MBX_POLL) != MBX_SUCCESS) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0448 Adapter failed init, mbxCmd x%x "
				"READ_SPARM mbxStatus x%x\n",
				mb->mbxCommand, mb->mbxStatus);
		phba->link_state = LPFC_HBA_ERROR;
		mp = (struct lpfc_dmabuf *)pmb->ctx_buf;
		mempool_free(pmb, phba->mbox_mem_pool);
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		kfree(mp);
		return -EIO;
	}

	mp = (struct lpfc_dmabuf *)pmb->ctx_buf;

	memcpy(&vport->fc_sparam, mp->virt, sizeof (struct serv_parm));
	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
	pmb->ctx_buf = NULL;
	lpfc_update_vport_wwn(vport);

	/* Update the fc_host data structures with new wwn. */
	fc_host_node_name(shost) = wwn_to_u64(vport->fc_nodename.u.wwn);
	fc_host_port_name(shost) = wwn_to_u64(vport->fc_portname.u.wwn);
	fc_host_max_npiv_vports(shost) = phba->max_vpi;

	/* If no serial number in VPD data, use low 6 bytes of WWNN */
	/* This should be consolidated into parse_vpd ? - mr */
	if (phba->SerialNumber[0] == 0) {
		uint8_t *outptr;

		outptr = &vport->fc_nodename.u.s.IEEE[0];
		for (i = 0; i < 12; i++) {
			status = *outptr++;
			j = ((status & 0xf0) >> 4);
			if (j <= 9)
				phba->SerialNumber[i] =
				    (char)((uint8_t) 0x30 + (uint8_t) j);
			else
				phba->SerialNumber[i] =
				    (char)((uint8_t) 0x61 + (uint8_t) (j - 10));
			i++;
			j = (status & 0xf);
			if (j <= 9)
				phba->SerialNumber[i] =
				    (char)((uint8_t) 0x30 + (uint8_t) j);
			else
				phba->SerialNumber[i] =
				    (char)((uint8_t) 0x61 + (uint8_t) (j - 10));
		}
	}

	lpfc_read_config(phba, pmb);
	pmb->vport = vport;
	if (lpfc_sli_issue_mbox(phba, pmb, MBX_POLL) != MBX_SUCCESS) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0453 Adapter failed to init, mbxCmd x%x "
				"READ_CONFIG, mbxStatus x%x\n",
				mb->mbxCommand, mb->mbxStatus);
		phba->link_state = LPFC_HBA_ERROR;
		mempool_free( pmb, phba->mbox_mem_pool);
		return -EIO;
	}

	/* Check if the port is disabled */
	lpfc_sli_read_link_ste(phba);

	/* Reset the DFT_HBA_Q_DEPTH to the max xri  */
	i = (mb->un.varRdConfig.max_xri + 1);
	if (phba->cfg_hba_queue_depth > i) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_INIT,
				"3359 HBA queue depth changed from %d to %d\n",
				phba->cfg_hba_queue_depth, i);
		phba->cfg_hba_queue_depth = i;
	}

	/* Reset the DFT_LUN_Q_DEPTH to (max xri >> 3)  */
	i = (mb->un.varRdConfig.max_xri >> 3);
	if (phba->pport->cfg_lun_queue_depth > i) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_INIT,
				"3360 LUN queue depth changed from %d to %d\n",
				phba->pport->cfg_lun_queue_depth, i);
		phba->pport->cfg_lun_queue_depth = i;
	}

	phba->lmt = mb->un.varRdConfig.lmt;

	/* Get the default values for Model Name and Description */
	lpfc_get_hba_model_desc(phba, phba->ModelName, phba->ModelDesc);

	phba->link_state = LPFC_LINK_DOWN;

	/* Only process IOCBs on ELS ring till hba_state is READY */
	if (psli->sli3_ring[LPFC_EXTRA_RING].sli.sli3.cmdringaddr)
		psli->sli3_ring[LPFC_EXTRA_RING].flag |= LPFC_STOP_IOCB_EVENT;
	if (psli->sli3_ring[LPFC_FCP_RING].sli.sli3.cmdringaddr)
		psli->sli3_ring[LPFC_FCP_RING].flag |= LPFC_STOP_IOCB_EVENT;

	/* Post receive buffers for desired rings */
	if (phba->sli_rev != 3)
		lpfc_post_rcv_buf(phba);

	/*
	 * Configure HBA MSI-X attention conditions to messages if MSI-X mode
	 */
	if (phba->intr_type == MSIX) {
		rc = lpfc_config_msi(phba, pmb);
		if (rc) {
			mempool_free(pmb, phba->mbox_mem_pool);
			return -EIO;
		}
		rc = lpfc_sli_issue_mbox(phba, pmb, MBX_POLL);
		if (rc != MBX_SUCCESS) {
			lpfc_printf_log(phba, KERN_ERR, LOG_MBOX,
					"0352 Config MSI mailbox command "
					"failed, mbxCmd x%x, mbxStatus x%x\n",
					pmb->u.mb.mbxCommand,
					pmb->u.mb.mbxStatus);
			mempool_free(pmb, phba->mbox_mem_pool);
			return -EIO;
		}
	}

	spin_lock_irq(&phba->hbalock);
	/* Initialize ERATT handling flag */
	phba->hba_flag &= ~HBA_ERATT_HANDLED;

	/* Enable appropriate host interrupts */
	if (lpfc_readl(phba->HCregaddr, &status)) {
		spin_unlock_irq(&phba->hbalock);
		return -EIO;
	}
	status |= HC_MBINT_ENA | HC_ERINT_ENA | HC_LAINT_ENA;
	if (psli->num_rings > 0)
		status |= HC_R0INT_ENA;
	if (psli->num_rings > 1)
		status |= HC_R1INT_ENA;
	if (psli->num_rings > 2)
		status |= HC_R2INT_ENA;
	if (psli->num_rings > 3)
		status |= HC_R3INT_ENA;

	if ((phba->cfg_poll & ENABLE_FCP_RING_POLLING) &&
	    (phba->cfg_poll & DISABLE_FCP_RING_INT))
		status &= ~(HC_R0INT_ENA);

	writel(status, phba->HCregaddr);
	readl(phba->HCregaddr); /* flush */
	spin_unlock_irq(&phba->hbalock);

	/* Set up ring-0 (ELS) timer */
	timeout = phba->fc_ratov * 2;
	mod_timer(&vport->els_tmofunc,
		  jiffies + msecs_to_jiffies(1000 * timeout));
	/* Set up heart beat (HB) timer */
	mod_timer(&phba->hb_tmofunc,
		  jiffies + msecs_to_jiffies(1000 * LPFC_HB_MBOX_INTERVAL));
	phba->hb_outstanding = 0;
	phba->last_completion_time = jiffies;
	/* Set up error attention (ERATT) polling timer */
	mod_timer(&phba->eratt_poll,
		  jiffies + msecs_to_jiffies(1000 * phba->eratt_poll_interval));

	if (phba->hba_flag & LINK_DISABLED) {
		lpfc_printf_log(phba,
			KERN_ERR, LOG_INIT,
			"2598 Adapter Link is disabled.\n");
		lpfc_down_link(phba, pmb);
		pmb->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
		rc = lpfc_sli_issue_mbox(phba, pmb, MBX_NOWAIT);
		if ((rc != MBX_SUCCESS) && (rc != MBX_BUSY)) {
			lpfc_printf_log(phba,
			KERN_ERR, LOG_INIT,
			"2599 Adapter failed to issue DOWN_LINK"
			" mbox command rc 0x%x\n", rc);

			mempool_free(pmb, phba->mbox_mem_pool);
			return -EIO;
		}
	} else if (phba->cfg_suppress_link_up == LPFC_INITIALIZE_LINK) {
		mempool_free(pmb, phba->mbox_mem_pool);
		rc = phba->lpfc_hba_init_link(phba, MBX_NOWAIT);
		if (rc)
			return rc;
	}
	/* MBOX buffer will be freed in mbox compl */
	pmb = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!pmb) {
		phba->link_state = LPFC_HBA_ERROR;
		return -ENOMEM;
	}

	lpfc_config_async(phba, pmb, LPFC_ELS_RING);
	pmb->mbox_cmpl = lpfc_config_async_cmpl;
	pmb->vport = phba->pport;
	rc = lpfc_sli_issue_mbox(phba, pmb, MBX_NOWAIT);

	if ((rc != MBX_BUSY) && (rc != MBX_SUCCESS)) {
		lpfc_printf_log(phba,
				KERN_ERR,
				LOG_INIT,
				"0456 Adapter failed to issue "
				"ASYNCEVT_ENABLE mbox status x%x\n",
				rc);
		mempool_free(pmb, phba->mbox_mem_pool);
	}

	/* Get Option rom version */
	pmb = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!pmb) {
		phba->link_state = LPFC_HBA_ERROR;
		return -ENOMEM;
	}

	lpfc_dump_wakeup_param(phba, pmb);
	pmb->mbox_cmpl = lpfc_dump_wakeup_param_cmpl;
	pmb->vport = phba->pport;
	rc = lpfc_sli_issue_mbox(phba, pmb, MBX_NOWAIT);

	if ((rc != MBX_BUSY) && (rc != MBX_SUCCESS)) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT, "0435 Adapter failed "
				"to get Option ROM version status x%x\n", rc);
		mempool_free(pmb, phba->mbox_mem_pool);
	}

	return 0;
}

/**
 * lpfc_hba_init_link - Initialize the FC link
 * @phba: pointer to lpfc hba data structure.
 * @flag: mailbox command issue mode - either MBX_POLL or MBX_NOWAIT
 *
 * This routine will issue the INIT_LINK mailbox command call.
 * It is available to other drivers through the lpfc_hba data
 * structure for use as a delayed link up mechanism with the
 * module parameter lpfc_suppress_link_up.
 *
 * Return code
 *		0 - success
 *		Any other value - error
 **/
static int
lpfc_hba_init_link(struct lpfc_hba *phba, uint32_t flag)
{
	return lpfc_hba_init_link_fc_topology(phba, phba->cfg_topology, flag);
}

/**
 * lpfc_hba_init_link_fc_topology - Initialize FC link with desired topology
 * @phba: pointer to lpfc hba data structure.
 * @fc_topology: desired fc topology.
 * @flag: mailbox command issue mode - either MBX_POLL or MBX_NOWAIT
 *
 * This routine will issue the INIT_LINK mailbox command call.
 * It is available to other drivers through the lpfc_hba data
 * structure for use as a delayed link up mechanism with the
 * module parameter lpfc_suppress_link_up.
 *
 * Return code
 *              0 - success
 *              Any other value - error
 **/
int
lpfc_hba_init_link_fc_topology(struct lpfc_hba *phba, uint32_t fc_topology,
			       uint32_t flag)
{
	struct lpfc_vport *vport = phba->pport;
	LPFC_MBOXQ_t *pmb;
	MAILBOX_t *mb;
	int rc;

	pmb = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!pmb) {
		phba->link_state = LPFC_HBA_ERROR;
		return -ENOMEM;
	}
	mb = &pmb->u.mb;
	pmb->vport = vport;

	if ((phba->cfg_link_speed > LPFC_USER_LINK_SPEED_MAX) ||
	    ((phba->cfg_link_speed == LPFC_USER_LINK_SPEED_1G) &&
	     !(phba->lmt & LMT_1Gb)) ||
	    ((phba->cfg_link_speed == LPFC_USER_LINK_SPEED_2G) &&
	     !(phba->lmt & LMT_2Gb)) ||
	    ((phba->cfg_link_speed == LPFC_USER_LINK_SPEED_4G) &&
	     !(phba->lmt & LMT_4Gb)) ||
	    ((phba->cfg_link_speed == LPFC_USER_LINK_SPEED_8G) &&
	     !(phba->lmt & LMT_8Gb)) ||
	    ((phba->cfg_link_speed == LPFC_USER_LINK_SPEED_10G) &&
	     !(phba->lmt & LMT_10Gb)) ||
	    ((phba->cfg_link_speed == LPFC_USER_LINK_SPEED_16G) &&
	     !(phba->lmt & LMT_16Gb)) ||
	    ((phba->cfg_link_speed == LPFC_USER_LINK_SPEED_32G) &&
	     !(phba->lmt & LMT_32Gb)) ||
	    ((phba->cfg_link_speed == LPFC_USER_LINK_SPEED_64G) &&
	     !(phba->lmt & LMT_64Gb))) {
		/* Reset link speed to auto */
		lpfc_printf_log(phba, KERN_ERR, LOG_LINK_EVENT,
			"1302 Invalid speed for this board:%d "
			"Reset link speed to auto.\n",
			phba->cfg_link_speed);
			phba->cfg_link_speed = LPFC_USER_LINK_SPEED_AUTO;
	}
	lpfc_init_link(phba, pmb, fc_topology, phba->cfg_link_speed);
	pmb->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
	if (phba->sli_rev < LPFC_SLI_REV4)
		lpfc_set_loopback_flag(phba);
	rc = lpfc_sli_issue_mbox(phba, pmb, flag);
	if ((rc != MBX_BUSY) && (rc != MBX_SUCCESS)) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
			"0498 Adapter failed to init, mbxCmd x%x "
			"INIT_LINK, mbxStatus x%x\n",
			mb->mbxCommand, mb->mbxStatus);
		if (phba->sli_rev <= LPFC_SLI_REV3) {
			/* Clear all interrupt enable conditions */
			writel(0, phba->HCregaddr);
			readl(phba->HCregaddr); /* flush */
			/* Clear all pending interrupts */
			writel(0xffffffff, phba->HAregaddr);
			readl(phba->HAregaddr); /* flush */
		}
		phba->link_state = LPFC_HBA_ERROR;
		if (rc != MBX_BUSY || flag == MBX_POLL)
			mempool_free(pmb, phba->mbox_mem_pool);
		return -EIO;
	}
	phba->cfg_suppress_link_up = LPFC_INITIALIZE_LINK;
	if (flag == MBX_POLL)
		mempool_free(pmb, phba->mbox_mem_pool);

	return 0;
}

/**
 * lpfc_hba_down_link - this routine downs the FC link
 * @phba: pointer to lpfc hba data structure.
 * @flag: mailbox command issue mode - either MBX_POLL or MBX_NOWAIT
 *
 * This routine will issue the DOWN_LINK mailbox command call.
 * It is available to other drivers through the lpfc_hba data
 * structure for use to stop the link.
 *
 * Return code
 *		0 - success
 *		Any other value - error
 **/
static int
lpfc_hba_down_link(struct lpfc_hba *phba, uint32_t flag)
{
	LPFC_MBOXQ_t *pmb;
	int rc;

	pmb = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!pmb) {
		phba->link_state = LPFC_HBA_ERROR;
		return -ENOMEM;
	}

	lpfc_printf_log(phba,
		KERN_ERR, LOG_INIT,
		"0491 Adapter Link is disabled.\n");
	lpfc_down_link(phba, pmb);
	pmb->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
	rc = lpfc_sli_issue_mbox(phba, pmb, flag);
	if ((rc != MBX_SUCCESS) && (rc != MBX_BUSY)) {
		lpfc_printf_log(phba,
		KERN_ERR, LOG_INIT,
		"2522 Adapter failed to issue DOWN_LINK"
		" mbox command rc 0x%x\n", rc);

		mempool_free(pmb, phba->mbox_mem_pool);
		return -EIO;
	}
	if (flag == MBX_POLL)
		mempool_free(pmb, phba->mbox_mem_pool);

	return 0;
}

/**
 * lpfc_hba_down_prep - Perform lpfc uninitialization prior to HBA reset
 * @phba: pointer to lpfc HBA data structure.
 *
 * This routine will do LPFC uninitialization before the HBA is reset when
 * bringing down the SLI Layer.
 *
 * Return codes
 *   0 - success.
 *   Any other value - error.
 **/
int
lpfc_hba_down_prep(struct lpfc_hba *phba)
{
	struct lpfc_vport **vports;
	int i;

	if (phba->sli_rev <= LPFC_SLI_REV3) {
		/* Disable interrupts */
		writel(0, phba->HCregaddr);
		readl(phba->HCregaddr); /* flush */
	}

	if (phba->pport->load_flag & FC_UNLOADING)
		lpfc_cleanup_discovery_resources(phba->pport);
	else {
		vports = lpfc_create_vport_work_array(phba);
		if (vports != NULL)
			for (i = 0; i <= phba->max_vports &&
				vports[i] != NULL; i++)
				lpfc_cleanup_discovery_resources(vports[i]);
		lpfc_destroy_vport_work_array(phba, vports);
	}
	return 0;
}

/**
 * lpfc_sli4_free_sp_events - Cleanup sp_queue_events to free
 * rspiocb which got deferred
 *
 * @phba: pointer to lpfc HBA data structure.
 *
 * This routine will cleanup completed slow path events after HBA is reset
 * when bringing down the SLI Layer.
 *
 *
 * Return codes
 *   void.
 **/
static void
lpfc_sli4_free_sp_events(struct lpfc_hba *phba)
{
	struct lpfc_iocbq *rspiocbq;
	struct hbq_dmabuf *dmabuf;
	struct lpfc_cq_event *cq_event;

	spin_lock_irq(&phba->hbalock);
	phba->hba_flag &= ~HBA_SP_QUEUE_EVT;
	spin_unlock_irq(&phba->hbalock);

	while (!list_empty(&phba->sli4_hba.sp_queue_event)) {
		/* Get the response iocb from the head of work queue */
		spin_lock_irq(&phba->hbalock);
		list_remove_head(&phba->sli4_hba.sp_queue_event,
				 cq_event, struct lpfc_cq_event, list);
		spin_unlock_irq(&phba->hbalock);

		switch (bf_get(lpfc_wcqe_c_code, &cq_event->cqe.wcqe_cmpl)) {
		case CQE_CODE_COMPL_WQE:
			rspiocbq = container_of(cq_event, struct lpfc_iocbq,
						 cq_event);
			lpfc_sli_release_iocbq(phba, rspiocbq);
			break;
		case CQE_CODE_RECEIVE:
		case CQE_CODE_RECEIVE_V1:
			dmabuf = container_of(cq_event, struct hbq_dmabuf,
					      cq_event);
			lpfc_in_buf_free(phba, &dmabuf->dbuf);
		}
	}
}

/**
 * lpfc_hba_free_post_buf - Perform lpfc uninitialization after HBA reset
 * @phba: pointer to lpfc HBA data structure.
 *
 * This routine will cleanup posted ELS buffers after the HBA is reset
 * when bringing down the SLI Layer.
 *
 *
 * Return codes
 *   void.
 **/
static void
lpfc_hba_free_post_buf(struct lpfc_hba *phba)
{
	struct lpfc_sli *psli = &phba->sli;
	struct lpfc_sli_ring *pring;
	struct lpfc_dmabuf *mp, *next_mp;
	LIST_HEAD(buflist);
	int count;

	if (phba->sli3_options & LPFC_SLI3_HBQ_ENABLED)
		lpfc_sli_hbqbuf_free_all(phba);
	else {
		/* Cleanup preposted buffers on the ELS ring */
		pring = &psli->sli3_ring[LPFC_ELS_RING];
		spin_lock_irq(&phba->hbalock);
		list_splice_init(&pring->postbufq, &buflist);
		spin_unlock_irq(&phba->hbalock);

		count = 0;
		list_for_each_entry_safe(mp, next_mp, &buflist, list) {
			list_del(&mp->list);
			count++;
			lpfc_mbuf_free(phba, mp->virt, mp->phys);
			kfree(mp);
		}

		spin_lock_irq(&phba->hbalock);
		pring->postbufq_cnt -= count;
		spin_unlock_irq(&phba->hbalock);
	}
}

/**
 * lpfc_hba_clean_txcmplq - Perform lpfc uninitialization after HBA reset
 * @phba: pointer to lpfc HBA data structure.
 *
 * This routine will cleanup the txcmplq after the HBA is reset when bringing
 * down the SLI Layer.
 *
 * Return codes
 *   void
 **/
static void
lpfc_hba_clean_txcmplq(struct lpfc_hba *phba)
{
	struct lpfc_sli *psli = &phba->sli;
	struct lpfc_queue *qp = NULL;
	struct lpfc_sli_ring *pring;
	LIST_HEAD(completions);
	int i;
	struct lpfc_iocbq *piocb, *next_iocb;

	if (phba->sli_rev != LPFC_SLI_REV4) {
		for (i = 0; i < psli->num_rings; i++) {
			pring = &psli->sli3_ring[i];
			spin_lock_irq(&phba->hbalock);
			/* At this point in time the HBA is either reset or DOA
			 * Nothing should be on txcmplq as it will
			 * NEVER complete.
			 */
			list_splice_init(&pring->txcmplq, &completions);
			pring->txcmplq_cnt = 0;
			spin_unlock_irq(&phba->hbalock);

			lpfc_sli_abort_iocb_ring(phba, pring);
		}
		/* Cancel all the IOCBs from the completions list */
		lpfc_sli_cancel_iocbs(phba, &completions,
				      IOSTAT_LOCAL_REJECT, IOERR_SLI_ABORTED);
		return;
	}
	list_for_each_entry(qp, &phba->sli4_hba.lpfc_wq_list, wq_list) {
		pring = qp->pring;
		if (!pring)
			continue;
		spin_lock_irq(&pring->ring_lock);
		list_for_each_entry_safe(piocb, next_iocb,
					 &pring->txcmplq, list)
			piocb->iocb_flag &= ~LPFC_IO_ON_TXCMPLQ;
		list_splice_init(&pring->txcmplq, &completions);
		pring->txcmplq_cnt = 0;
		spin_unlock_irq(&pring->ring_lock);
		lpfc_sli_abort_iocb_ring(phba, pring);
	}
	/* Cancel all the IOCBs from the completions list */
	lpfc_sli_cancel_iocbs(phba, &completions,
			      IOSTAT_LOCAL_REJECT, IOERR_SLI_ABORTED);
}

/**
 * lpfc_hba_down_post_s3 - Perform lpfc uninitialization after HBA reset
	int i;
 * @phba: pointer to lpfc HBA data structure.
 *
 * This routine will do uninitialization after the HBA is reset when bring
 * down the SLI Layer.
 *
 * Return codes
 *   0 - success.
 *   Any other value - error.
 **/
static int
lpfc_hba_down_post_s3(struct lpfc_hba *phba)
{
	lpfc_hba_free_post_buf(phba);
	lpfc_hba_clean_txcmplq(phba);
	return 0;
}

/**
 * lpfc_hba_down_post_s4 - Perform lpfc uninitialization after HBA reset
 * @phba: pointer to lpfc HBA data structure.
 *
 * This routine will do uninitialization after the HBA is reset when bring
 * down the SLI Layer.
 *
 * Return codes
 *   0 - success.
 *   Any other value - error.
 **/
static int
lpfc_hba_down_post_s4(struct lpfc_hba *phba)
{
	struct lpfc_io_buf *psb, *psb_next;
	struct lpfc_nvmet_rcv_ctx *ctxp, *ctxp_next;
	struct lpfc_sli4_hdw_queue *qp;
	LIST_HEAD(aborts);
	LIST_HEAD(nvme_aborts);
	LIST_HEAD(nvmet_aborts);
	struct lpfc_sglq *sglq_entry = NULL;
	int cnt, idx;


	lpfc_sli_hbqbuf_free_all(phba);
	lpfc_hba_clean_txcmplq(phba);

	/* At this point in time the HBA is either reset or DOA. Either
	 * way, nothing should be on lpfc_abts_els_sgl_list, it needs to be
	 * on the lpfc_els_sgl_list so that it can either be freed if the
	 * driver is unloading or reposted if the driver is restarting
	 * the port.
	 */
	spin_lock_irq(&phba->hbalock);  /* required for lpfc_els_sgl_list and */
					/* scsl_buf_list */
	/* sgl_list_lock required because worker thread uses this
	 * list.
	 */
	spin_lock(&phba->sli4_hba.sgl_list_lock);
	list_for_each_entry(sglq_entry,
		&phba->sli4_hba.lpfc_abts_els_sgl_list, list)
		sglq_entry->state = SGL_FREED;

	list_splice_init(&phba->sli4_hba.lpfc_abts_els_sgl_list,
			&phba->sli4_hba.lpfc_els_sgl_list);


	spin_unlock(&phba->sli4_hba.sgl_list_lock);

	/* abts_xxxx_buf_list_lock required because worker thread uses this
	 * list.
	 */
	cnt = 0;
	for (idx = 0; idx < phba->cfg_hdw_queue; idx++) {
		qp = &phba->sli4_hba.hdwq[idx];

		spin_lock(&qp->abts_io_buf_list_lock);
		list_splice_init(&qp->lpfc_abts_io_buf_list,
				 &aborts);

		list_for_each_entry_safe(psb, psb_next, &aborts, list) {
			psb->pCmd = NULL;
			psb->status = IOSTAT_SUCCESS;
			cnt++;
		}
		spin_lock(&qp->io_buf_list_put_lock);
		list_splice_init(&aborts, &qp->lpfc_io_buf_list_put);
		qp->put_io_bufs += qp->abts_scsi_io_bufs;
		qp->put_io_bufs += qp->abts_nvme_io_bufs;
		qp->abts_scsi_io_bufs = 0;
		qp->abts_nvme_io_bufs = 0;
		spin_unlock(&qp->io_buf_list_put_lock);
		spin_unlock(&qp->abts_io_buf_list_lock);
	}
	spin_unlock_irq(&phba->hbalock);

	if (phba->cfg_enable_fc4_type & LPFC_ENABLE_NVME) {
		spin_lock_irq(&phba->sli4_hba.abts_nvmet_buf_list_lock);
		list_splice_init(&phba->sli4_hba.lpfc_abts_nvmet_ctx_list,
				 &nvmet_aborts);
		spin_unlock_irq(&phba->sli4_hba.abts_nvmet_buf_list_lock);
		list_for_each_entry_safe(ctxp, ctxp_next, &nvmet_aborts, list) {
			ctxp->flag &= ~(LPFC_NVMET_XBUSY | LPFC_NVMET_ABORT_OP);
			lpfc_nvmet_ctxbuf_post(phba, ctxp->ctxbuf);
		}
	}

	lpfc_sli4_free_sp_events(phba);
	return cnt;
}

/**
 * lpfc_hba_down_post - Wrapper func for hba down post routine
 * @phba: pointer to lpfc HBA data structure.
 *
 * This routine wraps the actual SLI3 or SLI4 routine for performing
 * uninitialization after the HBA is reset when bring down the SLI Layer.
 *
 * Return codes
 *   0 - success.
 *   Any other value - error.
 **/
int
lpfc_hba_down_post(struct lpfc_hba *phba)
{
	return (*phba->lpfc_hba_down_post)(phba);
}

/**
 * lpfc_hb_timeout - The HBA-timer timeout handler
 * @ptr: unsigned long holds the pointer to lpfc hba data structure.
 *
 * This is the HBA-timer timeout handler registered to the lpfc driver. When
 * this timer fires, a HBA timeout event shall be posted to the lpfc driver
 * work-port-events bitmap and the worker thread is notified. This timeout
 * event will be used by the worker thread to invoke the actual timeout
 * handler routine, lpfc_hb_timeout_handler. Any periodical operations will
 * be performed in the timeout handler and the HBA timeout event bit shall
 * be cleared by the worker thread after it has taken the event bitmap out.
 **/
static void
lpfc_hb_timeout(struct timer_list *t)
{
	struct lpfc_hba *phba;
	uint32_t tmo_posted;
	unsigned long iflag;

	phba = from_timer(phba, t, hb_tmofunc);

	/* Check for heart beat timeout conditions */
	spin_lock_irqsave(&phba->pport->work_port_lock, iflag);
	tmo_posted = phba->pport->work_port_events & WORKER_HB_TMO;
	if (!tmo_posted)
		phba->pport->work_port_events |= WORKER_HB_TMO;
	spin_unlock_irqrestore(&phba->pport->work_port_lock, iflag);

	/* Tell the worker thread there is work to do */
	if (!tmo_posted)
		lpfc_worker_wake_up(phba);
	return;
}

/**
 * lpfc_rrq_timeout - The RRQ-timer timeout handler
 * @ptr: unsigned long holds the pointer to lpfc hba data structure.
 *
 * This is the RRQ-timer timeout handler registered to the lpfc driver. When
 * this timer fires, a RRQ timeout event shall be posted to the lpfc driver
 * work-port-events bitmap and the worker thread is notified. This timeout
 * event will be used by the worker thread to invoke the actual timeout
 * handler routine, lpfc_rrq_handler. Any periodical operations will
 * be performed in the timeout handler and the RRQ timeout event bit shall
 * be cleared by the worker thread after it has taken the event bitmap out.
 **/
static void
lpfc_rrq_timeout(struct timer_list *t)
{
	struct lpfc_hba *phba;
	unsigned long iflag;

	phba = from_timer(phba, t, rrq_tmr);
	spin_lock_irqsave(&phba->pport->work_port_lock, iflag);
	if (!(phba->pport->load_flag & FC_UNLOADING))
		phba->hba_flag |= HBA_RRQ_ACTIVE;
	else
		phba->hba_flag &= ~HBA_RRQ_ACTIVE;
	spin_unlock_irqrestore(&phba->pport->work_port_lock, iflag);

	if (!(phba->pport->load_flag & FC_UNLOADING))
		lpfc_worker_wake_up(phba);
}

/**
 * lpfc_hb_mbox_cmpl - The lpfc heart-beat mailbox command callback function
 * @phba: pointer to lpfc hba data structure.
 * @pmboxq: pointer to the driver internal queue element for mailbox command.
 *
 * This is the callback function to the lpfc heart-beat mailbox command.
 * If configured, the lpfc driver issues the heart-beat mailbox command to
 * the HBA every LPFC_HB_MBOX_INTERVAL (current 5) seconds. At the time the
 * heart-beat mailbox command is issued, the driver shall set up heart-beat
 * timeout timer to LPFC_HB_MBOX_TIMEOUT (current 30) seconds and marks
 * heart-beat outstanding state. Once the mailbox command comes back and
 * no error conditions detected, the heart-beat mailbox command timer is
 * reset to LPFC_HB_MBOX_INTERVAL seconds and the heart-beat outstanding
 * state is cleared for the next heart-beat. If the timer expired with the
 * heart-beat outstanding state set, the driver will put the HBA offline.
 **/
static void
lpfc_hb_mbox_cmpl(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmboxq)
{
	unsigned long drvr_flag;

	spin_lock_irqsave(&phba->hbalock, drvr_flag);
	phba->hb_outstanding = 0;
	spin_unlock_irqrestore(&phba->hbalock, drvr_flag);

	/* Check and reset heart-beat timer is necessary */
	mempool_free(pmboxq, phba->mbox_mem_pool);
	if (!(phba->pport->fc_flag & FC_OFFLINE_MODE) &&
		!(phba->link_state == LPFC_HBA_ERROR) &&
		!(phba->pport->load_flag & FC_UNLOADING))
		mod_timer(&phba->hb_tmofunc,
			  jiffies +
			  msecs_to_jiffies(1000 * LPFC_HB_MBOX_INTERVAL));
	return;
}

static void
lpfc_hb_eq_delay_work(struct work_struct *work)
{
	struct lpfc_hba *phba = container_of(to_delayed_work(work),
					     struct lpfc_hba, eq_delay_work);
	struct lpfc_eq_intr_info *eqi, *eqi_new;
	struct lpfc_queue *eq, *eq_next;
	unsigned char *ena_delay = NULL;
	uint32_t usdelay;
	int i;

	if (!phba->cfg_auto_imax || phba->pport->load_flag & FC_UNLOADING)
		return;

	if (phba->link_state == LPFC_HBA_ERROR ||
	    phba->pport->fc_flag & FC_OFFLINE_MODE)
		goto requeue;

	ena_delay = kcalloc(phba->sli4_hba.num_possible_cpu, sizeof(*ena_delay),
			    GFP_KERNEL);
	if (!ena_delay)
		goto requeue;

	for (i = 0; i < phba->cfg_irq_chann; i++) {
		/* Get the EQ corresponding to the IRQ vector */
		eq = phba->sli4_hba.hba_eq_hdl[i].eq;
		if (!eq)
			continue;
		if (eq->q_mode || eq->q_flag & HBA_EQ_DELAY_CHK) {
			eq->q_flag &= ~HBA_EQ_DELAY_CHK;
			ena_delay[eq->last_cpu] = 1;
		}
	}

	for_each_present_cpu(i) {
		eqi = per_cpu_ptr(phba->sli4_hba.eq_info, i);
		if (ena_delay[i]) {
			usdelay = (eqi->icnt >> 10) * LPFC_EQ_DELAY_STEP;
			if (usdelay > LPFC_MAX_AUTO_EQ_DELAY)
				usdelay = LPFC_MAX_AUTO_EQ_DELAY;
		} else {
			usdelay = 0;
		}

		eqi->icnt = 0;

		list_for_each_entry_safe(eq, eq_next, &eqi->list, cpu_list) {
			if (unlikely(eq->last_cpu != i)) {
				eqi_new = per_cpu_ptr(phba->sli4_hba.eq_info,
						      eq->last_cpu);
				list_move_tail(&eq->cpu_list, &eqi_new->list);
				continue;
			}
			if (usdelay != eq->q_mode)
				lpfc_modify_hba_eq_delay(phba, eq->hdwq, 1,
							 usdelay);
		}
	}

	kfree(ena_delay);

requeue:
	queue_delayed_work(phba->wq, &phba->eq_delay_work,
			   msecs_to_jiffies(LPFC_EQ_DELAY_MSECS));
}

/**
 * lpfc_hb_mxp_handler - Multi-XRI pools handler to adjust XRI distribution
 * @phba: pointer to lpfc hba data structure.
 *
 * For each heartbeat, this routine does some heuristic methods to adjust
 * XRI distribution. The goal is to fully utilize free XRIs.
 **/
static void lpfc_hb_mxp_handler(struct lpfc_hba *phba)
{
	u32 i;
	u32 hwq_count;

	hwq_count = phba->cfg_hdw_queue;
	for (i = 0; i < hwq_count; i++) {
		/* Adjust XRIs in private pool */
		lpfc_adjust_pvt_pool_count(phba, i);

		/* Adjust high watermark */
		lpfc_adjust_high_watermark(phba, i);

#ifdef LPFC_MXP_STAT
		/* Snapshot pbl, pvt and busy count */
		lpfc_snapshot_mxp(phba, i);
#endif
	}
}

/**
 * lpfc_hb_timeout_handler - The HBA-timer timeout handler
 * @phba: pointer to lpfc hba data structure.
 *
 * This is the actual HBA-timer timeout handler to be invoked by the worker
 * thread whenever the HBA timer fired and HBA-timeout event posted. This
 * handler performs any periodic operations needed for the device. If such
 * periodic event has already been attended to either in the interrupt handler
 * or by processing slow-ring or fast-ring events within the HBA-timer
 * timeout window (LPFC_HB_MBOX_INTERVAL), this handler just simply resets
 * the timer for the next timeout period. If lpfc heart-beat mailbox command
 * is configured and there is no heart-beat mailbox command outstanding, a
 * heart-beat mailbox is issued and timer set properly. Otherwise, if there
 * has been a heart-beat mailbox command outstanding, the HBA shall be put
 * to offline.
 **/
void
lpfc_hb_timeout_handler(struct lpfc_hba *phba)
{
	struct lpfc_vport **vports;
	LPFC_MBOXQ_t *pmboxq;
	struct lpfc_dmabuf *buf_ptr;
	int retval, i;
	struct lpfc_sli *psli = &phba->sli;
	LIST_HEAD(completions);

	if (phba->cfg_xri_rebalancing) {
		/* Multi-XRI pools handler */
		lpfc_hb_mxp_handler(phba);
	}

	vports = lpfc_create_vport_work_array(phba);
	if (vports != NULL)
		for (i = 0; i <= phba->max_vports && vports[i] != NULL; i++) {
			lpfc_rcv_seq_check_edtov(vports[i]);
			lpfc_fdmi_num_disc_check(vports[i]);
		}
	lpfc_destroy_vport_work_array(phba, vports);

	if ((phba->link_state == LPFC_HBA_ERROR) ||
		(phba->pport->load_flag & FC_UNLOADING) ||
		(phba->pport->fc_flag & FC_OFFLINE_MODE))
		return;

	spin_lock_irq(&phba->pport->work_port_lock);

	if (time_after(phba->last_completion_time +
			msecs_to_jiffies(1000 * LPFC_HB_MBOX_INTERVAL),
			jiffies)) {
		spin_unlock_irq(&phba->pport->work_port_lock);
		if (!phba->hb_outstanding)
			mod_timer(&phba->hb_tmofunc,
				jiffies +
				msecs_to_jiffies(1000 * LPFC_HB_MBOX_INTERVAL));
		else
			mod_timer(&phba->hb_tmofunc,
				jiffies +
				msecs_to_jiffies(1000 * LPFC_HB_MBOX_TIMEOUT));
		return;
	}
	spin_unlock_irq(&phba->pport->work_port_lock);

	if (phba->elsbuf_cnt &&
		(phba->elsbuf_cnt == phba->elsbuf_prev_cnt)) {
		spin_lock_irq(&phba->hbalock);
		list_splice_init(&phba->elsbuf, &completions);
		phba->elsbuf_cnt = 0;
		phba->elsbuf_prev_cnt = 0;
		spin_unlock_irq(&phba->hbalock);

		while (!list_empty(&completions)) {
			list_remove_head(&completions, buf_ptr,
				struct lpfc_dmabuf, list);
			lpfc_mbuf_free(phba, buf_ptr->virt, buf_ptr->phys);
			kfree(buf_ptr);
		}
	}
	phba->elsbuf_prev_cnt = phba->elsbuf_cnt;

	/* If there is no heart beat outstanding, issue a heartbeat command */
	if (phba->cfg_enable_hba_heartbeat) {
		if (!phba->hb_outstanding) {
			if ((!(psli->sli_flag & LPFC_SLI_MBOX_ACTIVE)) &&
				(list_empty(&psli->mboxq))) {
				pmboxq = mempool_alloc(phba->mbox_mem_pool,
							GFP_KERNEL);
				if (!pmboxq) {
					mod_timer(&phba->hb_tmofunc,
						 jiffies +
						 msecs_to_jiffies(1000 *
						 LPFC_HB_MBOX_INTERVAL));
					return;
				}

				lpfc_heart_beat(phba, pmboxq);
				pmboxq->mbox_cmpl = lpfc_hb_mbox_cmpl;
				pmboxq->vport = phba->pport;
				retval = lpfc_sli_issue_mbox(phba, pmboxq,
						MBX_NOWAIT);

				if (retval != MBX_BUSY &&
					retval != MBX_SUCCESS) {
					mempool_free(pmboxq,
							phba->mbox_mem_pool);
					mod_timer(&phba->hb_tmofunc,
						jiffies +
						msecs_to_jiffies(1000 *
						LPFC_HB_MBOX_INTERVAL));
					return;
				}
				phba->skipped_hb = 0;
				phba->hb_outstanding = 1;
			} else if (time_before_eq(phba->last_completion_time,
					phba->skipped_hb)) {
				lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
					"2857 Last completion time not "
					" updated in %d ms\n",
					jiffies_to_msecs(jiffies
						 - phba->last_completion_time));
			} else
				phba->skipped_hb = jiffies;

			mod_timer(&phba->hb_tmofunc,
				 jiffies +
				 msecs_to_jiffies(1000 * LPFC_HB_MBOX_TIMEOUT));
			return;
		} else {
			/*
			* If heart beat timeout called with hb_outstanding set
			* we need to give the hb mailbox cmd a chance to
			* complete or TMO.
			*/
			lpfc_printf_log(phba, KERN_WARNING, LOG_INIT,
					"0459 Adapter heartbeat still out"
					"standing:last compl time was %d ms.\n",
					jiffies_to_msecs(jiffies
						 - phba->last_completion_time));
			mod_timer(&phba->hb_tmofunc,
				jiffies +
				msecs_to_jiffies(1000 * LPFC_HB_MBOX_TIMEOUT));
		}
	} else {
			mod_timer(&phba->hb_tmofunc,
				jiffies +
				msecs_to_jiffies(1000 * LPFC_HB_MBOX_INTERVAL));
	}
}

/**
 * lpfc_offline_eratt - Bring lpfc offline on hardware error attention
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is called to bring the HBA offline when HBA hardware error
 * other than Port Error 6 has been detected.
 **/
static void
lpfc_offline_eratt(struct lpfc_hba *phba)
{
	struct lpfc_sli   *psli = &phba->sli;

	spin_lock_irq(&phba->hbalock);
	psli->sli_flag &= ~LPFC_SLI_ACTIVE;
	spin_unlock_irq(&phba->hbalock);
	lpfc_offline_prep(phba, LPFC_MBX_NO_WAIT);

	lpfc_offline(phba);
	lpfc_reset_barrier(phba);
	spin_lock_irq(&phba->hbalock);
	lpfc_sli_brdreset(phba);
	spin_unlock_irq(&phba->hbalock);
	lpfc_hba_down_post(phba);
	lpfc_sli_brdready(phba, HS_MBRDY);
	lpfc_unblock_mgmt_io(phba);
	phba->link_state = LPFC_HBA_ERROR;
	return;
}

/**
 * lpfc_sli4_offline_eratt - Bring lpfc offline on SLI4 hardware error attention
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is called to bring a SLI4 HBA offline when HBA hardware error
 * other than Port Error 6 has been detected.
 **/
void
lpfc_sli4_offline_eratt(struct lpfc_hba *phba)
{
	spin_lock_irq(&phba->hbalock);
	phba->link_state = LPFC_HBA_ERROR;
	spin_unlock_irq(&phba->hbalock);

	lpfc_offline_prep(phba, LPFC_MBX_NO_WAIT);
	lpfc_sli_flush_io_rings(phba);
	lpfc_offline(phba);
	lpfc_hba_down_post(phba);
	lpfc_unblock_mgmt_io(phba);
}

/**
 * lpfc_handle_deferred_eratt - The HBA hardware deferred error handler
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to handle the deferred HBA hardware error
 * conditions. This type of error is indicated by HBA by setting ER1
 * and another ER bit in the host status register. The driver will
 * wait until the ER1 bit clears before handling the error condition.
 **/
static void
lpfc_handle_deferred_eratt(struct lpfc_hba *phba)
{
	uint32_t old_host_status = phba->work_hs;
	struct lpfc_sli *psli = &phba->sli;

	/* If the pci channel is offline, ignore possible errors,
	 * since we cannot communicate with the pci card anyway.
	 */
	if (pci_channel_offline(phba->pcidev)) {
		spin_lock_irq(&phba->hbalock);
		phba->hba_flag &= ~DEFER_ERATT;
		spin_unlock_irq(&phba->hbalock);
		return;
	}

	lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
		"0479 Deferred Adapter Hardware Error "
		"Data: x%x x%x x%x\n",
		phba->work_hs,
		phba->work_status[0], phba->work_status[1]);

	spin_lock_irq(&phba->hbalock);
	psli->sli_flag &= ~LPFC_SLI_ACTIVE;
	spin_unlock_irq(&phba->hbalock);


	/*
	 * Firmware stops when it triggred erratt. That could cause the I/Os
	 * dropped by the firmware. Error iocb (I/O) on txcmplq and let the
	 * SCSI layer retry it after re-establishing link.
	 */
	lpfc_sli_abort_fcp_rings(phba);

	/*
	 * There was a firmware error. Take the hba offline and then
	 * attempt to restart it.
	 */
	lpfc_offline_prep(phba, LPFC_MBX_WAIT);
	lpfc_offline(phba);

	/* Wait for the ER1 bit to clear.*/
	while (phba->work_hs & HS_FFER1) {
		msleep(100);
		if (lpfc_readl(phba->HSregaddr, &phba->work_hs)) {
			phba->work_hs = UNPLUG_ERR ;
			break;
		}
		/* If driver is unloading let the worker thread continue */
		if (phba->pport->load_flag & FC_UNLOADING) {
			phba->work_hs = 0;
			break;
		}
	}

	/*
	 * This is to ptrotect against a race condition in which
	 * first write to the host attention register clear the
	 * host status register.
	 */
	if ((!phba->work_hs) && (!(phba->pport->load_flag & FC_UNLOADING)))
		phba->work_hs = old_host_status & ~HS_FFER1;

	spin_lock_irq(&phba->hbalock);
	phba->hba_flag &= ~DEFER_ERATT;
	spin_unlock_irq(&phba->hbalock);
	phba->work_status[0] = readl(phba->MBslimaddr + 0xa8);
	phba->work_status[1] = readl(phba->MBslimaddr + 0xac);
}

static void
lpfc_board_errevt_to_mgmt(struct lpfc_hba *phba)
{
	struct lpfc_board_event_header board_event;
	struct Scsi_Host *shost;

	board_event.event_type = FC_REG_BOARD_EVENT;
	board_event.subcategory = LPFC_EVENT_PORTINTERR;
	shost = lpfc_shost_from_vport(phba->pport);
	fc_host_post_vendor_event(shost, fc_get_event_number(),
				  sizeof(board_event),
				  (char *) &board_event,
				  LPFC_NL_VENDOR_ID);
}

/**
 * lpfc_handle_eratt_s3 - The SLI3 HBA hardware error handler
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to handle the following HBA hardware error
 * conditions:
 * 1 - HBA error attention interrupt
 * 2 - DMA ring index out of range
 * 3 - Mailbox command came back as unknown
 **/
static void
lpfc_handle_eratt_s3(struct lpfc_hba *phba)
{
	struct lpfc_vport *vport = phba->pport;
	struct lpfc_sli   *psli = &phba->sli;
	uint32_t event_data;
	unsigned long temperature;
	struct temp_event temp_event_data;
	struct Scsi_Host  *shost;

	/* If the pci channel is offline, ignore possible errors,
	 * since we cannot communicate with the pci card anyway.
	 */
	if (pci_channel_offline(phba->pcidev)) {
		spin_lock_irq(&phba->hbalock);
		phba->hba_flag &= ~DEFER_ERATT;
		spin_unlock_irq(&phba->hbalock);
		return;
	}

	/* If resets are disabled then leave the HBA alone and return */
	if (!phba->cfg_enable_hba_reset)
		return;

	/* Send an internal error event to mgmt application */
	lpfc_board_errevt_to_mgmt(phba);

	if (phba->hba_flag & DEFER_ERATT)
		lpfc_handle_deferred_eratt(phba);

	if ((phba->work_hs & HS_FFER6) || (phba->work_hs & HS_FFER8)) {
		if (phba->work_hs & HS_FFER6)
			/* Re-establishing Link */
			lpfc_printf_log(phba, KERN_INFO, LOG_LINK_EVENT,
					"1301 Re-establishing Link "
					"Data: x%x x%x x%x\n",
					phba->work_hs, phba->work_status[0],
					phba->work_status[1]);
		if (phba->work_hs & HS_FFER8)
			/* Device Zeroization */
			lpfc_printf_log(phba, KERN_INFO, LOG_LINK_EVENT,
					"2861 Host Authentication device "
					"zeroization Data:x%x x%x x%x\n",
					phba->work_hs, phba->work_status[0],
					phba->work_status[1]);

		spin_lock_irq(&phba->hbalock);
		psli->sli_flag &= ~LPFC_SLI_ACTIVE;
		spin_unlock_irq(&phba->hbalock);

		/*
		* Firmware stops when it triggled erratt with HS_FFER6.
		* That could cause the I/Os dropped by the firmware.
		* Error iocb (I/O) on txcmplq and let the SCSI layer
		* retry it after re-establishing link.
		*/
		lpfc_sli_abort_fcp_rings(phba);

		/*
		 * There was a firmware error.  Take the hba offline and then
		 * attempt to restart it.
		 */
		lpfc_offline_prep(phba, LPFC_MBX_NO_WAIT);
		lpfc_offline(phba);
		lpfc_sli_brdrestart(phba);
		if (lpfc_online(phba) == 0) {	/* Initialize the HBA */
			lpfc_unblock_mgmt_io(phba);
			return;
		}
		lpfc_unblock_mgmt_io(phba);
	} else if (phba->work_hs & HS_CRIT_TEMP) {
		temperature = readl(phba->MBslimaddr + TEMPERATURE_OFFSET);
		temp_event_data.event_type = FC_REG_TEMPERATURE_EVENT;
		temp_event_data.event_code = LPFC_CRIT_TEMP;
		temp_event_data.data = (uint32_t)temperature;

		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0406 Adapter maximum temperature exceeded "
				"(%ld), taking this port offline "
				"Data: x%x x%x x%x\n",
				temperature, phba->work_hs,
				phba->work_status[0], phba->work_status[1]);

		shost = lpfc_shost_from_vport(phba->pport);
		fc_host_post_vendor_event(shost, fc_get_event_number(),
					  sizeof(temp_event_data),
					  (char *) &temp_event_data,
					  SCSI_NL_VID_TYPE_PCI
					  | PCI_VENDOR_ID_EMULEX);

		spin_lock_irq(&phba->hbalock);
		phba->over_temp_state = HBA_OVER_TEMP;
		spin_unlock_irq(&phba->hbalock);
		lpfc_offline_eratt(phba);

	} else {
		/* The if clause above forces this code path when the status
		 * failure is a value other than FFER6. Do not call the offline
		 * twice. This is the adapter hardware error path.
		 */
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0457 Adapter Hardware Error "
				"Data: x%x x%x x%x\n",
				phba->work_hs,
				phba->work_status[0], phba->work_status[1]);

		event_data = FC_REG_DUMP_EVENT;
		shost = lpfc_shost_from_vport(vport);
		fc_host_post_vendor_event(shost, fc_get_event_number(),
				sizeof(event_data), (char *) &event_data,
				SCSI_NL_VID_TYPE_PCI | PCI_VENDOR_ID_EMULEX);

		lpfc_offline_eratt(phba);
	}
	return;
}

/**
 * lpfc_sli4_port_sta_fn_reset - The SLI4 function reset due to port status reg
 * @phba: pointer to lpfc hba data structure.
 * @mbx_action: flag for mailbox shutdown action.
 *
 * This routine is invoked to perform an SLI4 port PCI function reset in
 * response to port status register polling attention. It waits for port
 * status register (ERR, RDY, RN) bits before proceeding with function reset.
 * During this process, interrupt vectors are freed and later requested
 * for handling possible port resource change.
 **/
static int
lpfc_sli4_port_sta_fn_reset(struct lpfc_hba *phba, int mbx_action,
			    bool en_rn_msg)
{
	int rc;
	uint32_t intr_mode;

	if (bf_get(lpfc_sli_intf_if_type, &phba->sli4_hba.sli_intf) >=
	    LPFC_SLI_INTF_IF_TYPE_2) {
		/*
		 * On error status condition, driver need to wait for port
		 * ready before performing reset.
		 */
		rc = lpfc_sli4_pdev_status_reg_wait(phba);
		if (rc)
			return rc;
	}

	/* need reset: attempt for port recovery */
	if (en_rn_msg)
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"2887 Reset Needed: Attempting Port "
				"Recovery...\n");
	lpfc_offline_prep(phba, mbx_action);
	lpfc_sli_flush_io_rings(phba);
	lpfc_offline(phba);
	/* release interrupt for possible resource change */
	lpfc_sli4_disable_intr(phba);
	rc = lpfc_sli_brdrestart(phba);
	if (rc) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"6309 Failed to restart board\n");
		return rc;
	}
	/* request and enable interrupt */
	intr_mode = lpfc_sli4_enable_intr(phba, phba->intr_mode);
	if (intr_mode == LPFC_INTR_ERROR) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"3175 Failed to enable interrupt\n");
		return -EIO;
	}
	phba->intr_mode = intr_mode;
	rc = lpfc_online(phba);
	if (rc == 0)
		lpfc_unblock_mgmt_io(phba);

	return rc;
}

/**
 * lpfc_handle_eratt_s4 - The SLI4 HBA hardware error handler
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to handle the SLI4 HBA hardware error attention
 * conditions.
 **/
static void
lpfc_handle_eratt_s4(struct lpfc_hba *phba)
{
	struct lpfc_vport *vport = phba->pport;
	uint32_t event_data;
	struct Scsi_Host *shost;
	uint32_t if_type;
	struct lpfc_register portstat_reg = {0};
	uint32_t reg_err1, reg_err2;
	uint32_t uerrlo_reg, uemasklo_reg;
	uint32_t smphr_port_status = 0, pci_rd_rc1, pci_rd_rc2;
	bool en_rn_msg = true;
	struct temp_event temp_event_data;
	struct lpfc_register portsmphr_reg;
	int rc, i;

	/* If the pci channel is offline, ignore possible errors, since
	 * we cannot communicate with the pci card anyway.
	 */
	if (pci_channel_offline(phba->pcidev)) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"3166 pci channel is offline\n");
		lpfc_sli4_offline_eratt(phba);
		return;
	}

	memset(&portsmphr_reg, 0, sizeof(portsmphr_reg));
	if_type = bf_get(lpfc_sli_intf_if_type, &phba->sli4_hba.sli_intf);
	switch (if_type) {
	case LPFC_SLI_INTF_IF_TYPE_0:
		pci_rd_rc1 = lpfc_readl(
				phba->sli4_hba.u.if_type0.UERRLOregaddr,
				&uerrlo_reg);
		pci_rd_rc2 = lpfc_readl(
				phba->sli4_hba.u.if_type0.UEMASKLOregaddr,
				&uemasklo_reg);
		/* consider PCI bus read error as pci_channel_offline */
		if (pci_rd_rc1 == -EIO && pci_rd_rc2 == -EIO)
			return;
		if (!(phba->hba_flag & HBA_RECOVERABLE_UE)) {
			lpfc_sli4_offline_eratt(phba);
			return;
		}
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"7623 Checking UE recoverable");

		for (i = 0; i < phba->sli4_hba.ue_to_sr / 1000; i++) {
			if (lpfc_readl(phba->sli4_hba.PSMPHRregaddr,
				       &portsmphr_reg.word0))
				continue;

			smphr_port_status = bf_get(lpfc_port_smphr_port_status,
						   &portsmphr_reg);
			if ((smphr_port_status & LPFC_PORT_SEM_MASK) ==
			    LPFC_PORT_SEM_UE_RECOVERABLE)
				break;
			/*Sleep for 1Sec, before checking SEMAPHORE */
			msleep(1000);
		}

		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"4827 smphr_port_status x%x : Waited %dSec",
				smphr_port_status, i);

		/* Recoverable UE, reset the HBA device */
		if ((smphr_port_status & LPFC_PORT_SEM_MASK) ==
		    LPFC_PORT_SEM_UE_RECOVERABLE) {
			for (i = 0; i < 20; i++) {
				msleep(1000);
				if (!lpfc_readl(phba->sli4_hba.PSMPHRregaddr,
				    &portsmphr_reg.word0) &&
				    (LPFC_POST_STAGE_PORT_READY ==
				     bf_get(lpfc_port_smphr_port_status,
				     &portsmphr_reg))) {
					rc = lpfc_sli4_port_sta_fn_reset(phba,
						LPFC_MBX_NO_WAIT, en_rn_msg);
					if (rc == 0)
						return;
					lpfc_printf_log(phba,
						KERN_ERR, LOG_INIT,
						"4215 Failed to recover UE");
					break;
				}
			}
		}
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"7624 Firmware not ready: Failing UE recovery,"
				" waited %dSec", i);
		phba->link_state = LPFC_HBA_ERROR;
		break;

	case LPFC_SLI_INTF_IF_TYPE_2:
	case LPFC_SLI_INTF_IF_TYPE_6:
		pci_rd_rc1 = lpfc_readl(
				phba->sli4_hba.u.if_type2.STATUSregaddr,
				&portstat_reg.word0);
		/* consider PCI bus read error as pci_channel_offline */
		if (pci_rd_rc1 == -EIO) {
			lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"3151 PCI bus read access failure: x%x\n",
				readl(phba->sli4_hba.u.if_type2.STATUSregaddr));
			lpfc_sli4_offline_eratt(phba);
			return;
		}
		reg_err1 = readl(phba->sli4_hba.u.if_type2.ERR1regaddr);
		reg_err2 = readl(phba->sli4_hba.u.if_type2.ERR2regaddr);
		if (bf_get(lpfc_sliport_status_oti, &portstat_reg)) {
			lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"2889 Port Overtemperature event, "
				"taking port offline Data: x%x x%x\n",
				reg_err1, reg_err2);

			phba->sfp_alarm |= LPFC_TRANSGRESSION_HIGH_TEMPERATURE;
			temp_event_data.event_type = FC_REG_TEMPERATURE_EVENT;
			temp_event_data.event_code = LPFC_CRIT_TEMP;
			temp_event_data.data = 0xFFFFFFFF;

			shost = lpfc_shost_from_vport(phba->pport);
			fc_host_post_vendor_event(shost, fc_get_event_number(),
						  sizeof(temp_event_data),
						  (char *)&temp_event_data,
						  SCSI_NL_VID_TYPE_PCI
						  | PCI_VENDOR_ID_EMULEX);

			spin_lock_irq(&phba->hbalock);
			phba->over_temp_state = HBA_OVER_TEMP;
			spin_unlock_irq(&phba->hbalock);
			lpfc_sli4_offline_eratt(phba);
			return;
		}
		if (reg_err1 == SLIPORT_ERR1_REG_ERR_CODE_2 &&
		    reg_err2 == SLIPORT_ERR2_REG_FW_RESTART) {
			lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
					"3143 Port Down: Firmware Update "
					"Detected\n");
			en_rn_msg = false;
		} else if (reg_err1 == SLIPORT_ERR1_REG_ERR_CODE_2 &&
			 reg_err2 == SLIPORT_ERR2_REG_FORCED_DUMP)
			lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
					"3144 Port Down: Debug Dump\n");
		else if (reg_err1 == SLIPORT_ERR1_REG_ERR_CODE_2 &&
			 reg_err2 == SLIPORT_ERR2_REG_FUNC_PROVISON)
			lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
					"3145 Port Down: Provisioning\n");

		/* If resets are disabled then leave the HBA alone and return */
		if (!phba->cfg_enable_hba_reset)
			return;

		/* Check port status register for function reset */
		rc = lpfc_sli4_port_sta_fn_reset(phba, LPFC_MBX_NO_WAIT,
				en_rn_msg);
		if (rc == 0) {
			/* don't report event on forced debug dump */
			if (reg_err1 == SLIPORT_ERR1_REG_ERR_CODE_2 &&
			    reg_err2 == SLIPORT_ERR2_REG_FORCED_DUMP)
				return;
			else
				break;
		}
		/* fall through for not able to recover */
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"3152 Unrecoverable error\n");
		phba->link_state = LPFC_HBA_ERROR;
		break;
	case LPFC_SLI_INTF_IF_TYPE_1:
	default:
		break;
	}
	lpfc_printf_log(phba, KERN_WARNING, LOG_INIT,
			"3123 Report dump event to upper layer\n");
	/* Send an internal error event to mgmt application */
	lpfc_board_errevt_to_mgmt(phba);

	event_data = FC_REG_DUMP_EVENT;
	shost = lpfc_shost_from_vport(vport);
	fc_host_post_vendor_event(shost, fc_get_event_number(),
				  sizeof(event_data), (char *) &event_data,
				  SCSI_NL_VID_TYPE_PCI | PCI_VENDOR_ID_EMULEX);
}

/**
 * lpfc_handle_eratt - Wrapper func for handling hba error attention
 * @phba: pointer to lpfc HBA data structure.
 *
 * This routine wraps the actual SLI3 or SLI4 hba error attention handling
 * routine from the API jump table function pointer from the lpfc_hba struct.
 *
 * Return codes
 *   0 - success.
 *   Any other value - error.
 **/
void
lpfc_handle_eratt(struct lpfc_hba *phba)
{
	(*phba->lpfc_handle_eratt)(phba);
}

/**
 * lpfc_handle_latt - The HBA link event handler
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked from the worker thread to handle a HBA host
 * attention link event. SLI3 only.
 **/
void
lpfc_handle_latt(struct lpfc_hba *phba)
{
	struct lpfc_vport *vport = phba->pport;
	struct lpfc_sli   *psli = &phba->sli;
	LPFC_MBOXQ_t *pmb;
	volatile uint32_t control;
	struct lpfc_dmabuf *mp;
	int rc = 0;

	pmb = (LPFC_MBOXQ_t *)mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!pmb) {
		rc = 1;
		goto lpfc_handle_latt_err_exit;
	}

	mp = kmalloc(sizeof(struct lpfc_dmabuf), GFP_KERNEL);
	if (!mp) {
		rc = 2;
		goto lpfc_handle_latt_free_pmb;
	}

	mp->virt = lpfc_mbuf_alloc(phba, 0, &mp->phys);
	if (!mp->virt) {
		rc = 3;
		goto lpfc_handle_latt_free_mp;
	}

	/* Cleanup any outstanding ELS commands */
	lpfc_els_flush_all_cmd(phba);

	psli->slistat.link_event++;
	lpfc_read_topology(phba, pmb, mp);
	pmb->mbox_cmpl = lpfc_mbx_cmpl_read_topology;
	pmb->vport = vport;
	/* Block ELS IOCBs until we have processed this mbox command */
	phba->sli.sli3_ring[LPFC_ELS_RING].flag |= LPFC_STOP_IOCB_EVENT;
	rc = lpfc_sli_issue_mbox (phba, pmb, MBX_NOWAIT);
	if (rc == MBX_NOT_FINISHED) {
		rc = 4;
		goto lpfc_handle_latt_free_mbuf;
	}

	/* Clear Link Attention in HA REG */
	spin_lock_irq(&phba->hbalock);
	writel(HA_LATT, phba->HAregaddr);
	readl(phba->HAregaddr); /* flush */
	spin_unlock_irq(&phba->hbalock);

	return;

lpfc_handle_latt_free_mbuf:
	phba->sli.sli3_ring[LPFC_ELS_RING].flag &= ~LPFC_STOP_IOCB_EVENT;
	lpfc_mbuf_free(phba, mp->virt, mp->phys);
lpfc_handle_latt_free_mp:
	kfree(mp);
lpfc_handle_latt_free_pmb:
	mempool_free(pmb, phba->mbox_mem_pool);
lpfc_handle_latt_err_exit:
	/* Enable Link attention interrupts */
	spin_lock_irq(&phba->hbalock);
	psli->sli_flag |= LPFC_PROCESS_LA;
	control = readl(phba->HCregaddr);
	control |= HC_LAINT_ENA;
	writel(control, phba->HCregaddr);
	readl(phba->HCregaddr); /* flush */

	/* Clear Link Attention in HA REG */
	writel(HA_LATT, phba->HAregaddr);
	readl(phba->HAregaddr); /* flush */
	spin_unlock_irq(&phba->hbalock);
	lpfc_linkdown(phba);
	phba->link_state = LPFC_HBA_ERROR;

	lpfc_printf_log(phba, KERN_ERR, LOG_MBOX,
		     "0300 LATT: Cannot issue READ_LA: Data:%d\n", rc);

	return;
}

/**
 * lpfc_parse_vpd - Parse VPD (Vital Product Data)
 * @phba: pointer to lpfc hba data structure.
 * @vpd: pointer to the vital product data.
 * @len: length of the vital product data in bytes.
 *
 * This routine parses the Vital Product Data (VPD). The VPD is treated as
 * an array of characters. In this routine, the ModelName, ProgramType, and
 * ModelDesc, etc. fields of the phba data structure will be populated.
 *
 * Return codes
 *   0 - pointer to the VPD passed in is NULL
 *   1 - success
 **/
int
lpfc_parse_vpd(struct lpfc_hba *phba, uint8_t *vpd, int len)
{
	uint8_t lenlo, lenhi;
	int Length;
	int i, j;
	int finished = 0;
	int index = 0;

	if (!vpd)
		return 0;

	/* Vital Product */
	lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
			"0455 Vital Product Data: x%x x%x x%x x%x\n",
			(uint32_t) vpd[0], (uint32_t) vpd[1], (uint32_t) vpd[2],
			(uint32_t) vpd[3]);
	while (!finished && (index < (len - 4))) {
		switch (vpd[index]) {
		case 0x82:
		case 0x91:
			index += 1;
			lenlo = vpd[index];
			index += 1;
			lenhi = vpd[index];
			index += 1;
			i = ((((unsigned short)lenhi) << 8) + lenlo);
			index += i;
			break;
		case 0x90:
			index += 1;
			lenlo = vpd[index];
			index += 1;
			lenhi = vpd[index];
			index += 1;
			Length = ((((unsigned short)lenhi) << 8) + lenlo);
			if (Length > len - index)
				Length = len - index;
			while (Length > 0) {
			/* Look for Serial Number */
			if ((vpd[index] == 'S') && (vpd[index+1] == 'N')) {
				index += 2;
				i = vpd[index];
				index += 1;
				j = 0;
				Length -= (3+i);
				while(i--) {
					phba->SerialNumber[j++] = vpd[index++];
					if (j == 31)
						break;
				}
				phba->SerialNumber[j] = 0;
				continue;
			}
			else if ((vpd[index] == 'V') && (vpd[index+1] == '1')) {
				phba->vpd_flag |= VPD_MODEL_DESC;
				index += 2;
				i = vpd[index];
				index += 1;
				j = 0;
				Length -= (3+i);
				while(i--) {
					phba->ModelDesc[j++] = vpd[index++];
					if (j == 255)
						break;
				}
				phba->ModelDesc[j] = 0;
				continue;
			}
			else if ((vpd[index] == 'V') && (vpd[index+1] == '2')) {
				phba->vpd_flag |= VPD_MODEL_NAME;
				index += 2;
				i = vpd[index];
				index += 1;
				j = 0;
				Length -= (3+i);
				while(i--) {
					phba->ModelName[j++] = vpd[index++];
					if (j == 79)
						break;
				}
				phba->ModelName[j] = 0;
				continue;
			}
			else if ((vpd[index] == 'V') && (vpd[index+1] == '3')) {
				phba->vpd_flag |= VPD_PROGRAM_TYPE;
				index += 2;
				i = vpd[index];
				index += 1;
				j = 0;
				Length -= (3+i);
				while(i--) {
					phba->ProgramType[j++] = vpd[index++];
					if (j == 255)
						break;
				}
				phba->ProgramType[j] = 0;
				continue;
			}
			else if ((vpd[index] == 'V') && (vpd[index+1] == '4')) {
				phba->vpd_flag |= VPD_PORT;
				index += 2;
				i = vpd[index];
				index += 1;
				j = 0;
				Length -= (3+i);
				while(i--) {
					if ((phba->sli_rev == LPFC_SLI_REV4) &&
					    (phba->sli4_hba.pport_name_sta ==
					     LPFC_SLI4_PPNAME_GET)) {
						j++;
						index++;
					} else
						phba->Port[j++] = vpd[index++];
					if (j == 19)
						break;
				}
				if ((phba->sli_rev != LPFC_SLI_REV4) ||
				    (phba->sli4_hba.pport_name_sta ==
				     LPFC_SLI4_PPNAME_NON))
					phba->Port[j] = 0;
				continue;
			}
			else {
				index += 2;
				i = vpd[index];
				index += 1;
				index += i;
				Length -= (3 + i);
			}
		}
		finished = 0;
		break;
		case 0x78:
			finished = 1;
			break;
		default:
			index ++;
			break;
		}
	}

	return(1);
}

/**
 * lpfc_get_hba_model_desc - Retrieve HBA device model name and description
 * @phba: pointer to lpfc hba data structure.
 * @mdp: pointer to the data structure to hold the derived model name.
 * @descp: pointer to the data structure to hold the derived description.
 *
 * This routine retrieves HBA's description based on its registered PCI device
 * ID. The @descp passed into this function points to an array of 256 chars. It
 * shall be returned with the model name, maximum speed, and the host bus type.
 * The @mdp passed into this function points to an array of 80 chars. When the
 * function returns, the @mdp will be filled with the model name.
 **/
static void
lpfc_get_hba_model_desc(struct lpfc_hba *phba, uint8_t *mdp, uint8_t *descp)
{
	lpfc_vpd_t *vp;
	uint16_t dev_id = phba->pcidev->device;
	int max_speed;
	int GE = 0;
	int oneConnect = 0; /* default is not a oneConnect */
	struct {
		char *name;
		char *bus;
		char *function;
	} m = {"<Unknown>", "", ""};

	if (mdp && mdp[0] != '\0'
		&& descp && descp[0] != '\0')
		return;

	if (phba->lmt & LMT_64Gb)
		max_speed = 64;
	else if (phba->lmt & LMT_32Gb)
		max_speed = 32;
	else if (phba->lmt & LMT_16Gb)
		max_speed = 16;
	else if (phba->lmt & LMT_10Gb)
		max_speed = 10;
	else if (phba->lmt & LMT_8Gb)
		max_speed = 8;
	else if (phba->lmt & LMT_4Gb)
		max_speed = 4;
	else if (phba->lmt & LMT_2Gb)
		max_speed = 2;
	else if (phba->lmt & LMT_1Gb)
		max_speed = 1;
	else
		max_speed = 0;

	vp = &phba->vpd;

	switch (dev_id) {
	case PCI_DEVICE_ID_FIREFLY:
		m = (typeof(m)){"LP6000", "PCI",
				"Obsolete, Unsupported Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_SUPERFLY:
		if (vp->rev.biuRev >= 1 && vp->rev.biuRev <= 3)
			m = (typeof(m)){"LP7000", "PCI", ""};
		else
			m = (typeof(m)){"LP7000E", "PCI", ""};
		m.function = "Obsolete, Unsupported Fibre Channel Adapter";
		break;
	case PCI_DEVICE_ID_DRAGONFLY:
		m = (typeof(m)){"LP8000", "PCI",
				"Obsolete, Unsupported Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_CENTAUR:
		if (FC_JEDEC_ID(vp->rev.biuRev) == CENTAUR_2G_JEDEC_ID)
			m = (typeof(m)){"LP9002", "PCI", ""};
		else
			m = (typeof(m)){"LP9000", "PCI", ""};
		m.function = "Obsolete, Unsupported Fibre Channel Adapter";
		break;
	case PCI_DEVICE_ID_RFLY:
		m = (typeof(m)){"LP952", "PCI",
				"Obsolete, Unsupported Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_PEGASUS:
		m = (typeof(m)){"LP9802", "PCI-X",
				"Obsolete, Unsupported Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_THOR:
		m = (typeof(m)){"LP10000", "PCI-X",
				"Obsolete, Unsupported Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_VIPER:
		m = (typeof(m)){"LPX1000",  "PCI-X",
				"Obsolete, Unsupported Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_PFLY:
		m = (typeof(m)){"LP982", "PCI-X",
				"Obsolete, Unsupported Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_TFLY:
		m = (typeof(m)){"LP1050", "PCI-X",
				"Obsolete, Unsupported Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_HELIOS:
		m = (typeof(m)){"LP11000", "PCI-X2",
				"Obsolete, Unsupported Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_HELIOS_SCSP:
		m = (typeof(m)){"LP11000-SP", "PCI-X2",
				"Obsolete, Unsupported Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_HELIOS_DCSP:
		m = (typeof(m)){"LP11002-SP",  "PCI-X2",
				"Obsolete, Unsupported Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_NEPTUNE:
		m = (typeof(m)){"LPe1000", "PCIe",
				"Obsolete, Unsupported Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_NEPTUNE_SCSP:
		m = (typeof(m)){"LPe1000-SP", "PCIe",
				"Obsolete, Unsupported Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_NEPTUNE_DCSP:
		m = (typeof(m)){"LPe1002-SP", "PCIe",
				"Obsolete, Unsupported Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_BMID:
		m = (typeof(m)){"LP1150", "PCI-X2", "Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_BSMB:
		m = (typeof(m)){"LP111", "PCI-X2",
				"Obsolete, Unsupported Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_ZEPHYR:
		m = (typeof(m)){"LPe11000", "PCIe", "Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_ZEPHYR_SCSP:
		m = (typeof(m)){"LPe11000", "PCIe", "Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_ZEPHYR_DCSP:
		m = (typeof(m)){"LP2105", "PCIe", "FCoE Adapter"};
		GE = 1;
		break;
	case PCI_DEVICE_ID_ZMID:
		m = (typeof(m)){"LPe1150", "PCIe", "Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_ZSMB:
		m = (typeof(m)){"LPe111", "PCIe", "Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_LP101:
		m = (typeof(m)){"LP101", "PCI-X",
				"Obsolete, Unsupported Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_LP10000S:
		m = (typeof(m)){"LP10000-S", "PCI",
				"Obsolete, Unsupported Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_LP11000S:
		m = (typeof(m)){"LP11000-S", "PCI-X2",
				"Obsolete, Unsupported Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_LPE11000S:
		m = (typeof(m)){"LPe11000-S", "PCIe",
				"Obsolete, Unsupported Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_SAT:
		m = (typeof(m)){"LPe12000", "PCIe", "Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_SAT_MID:
		m = (typeof(m)){"LPe1250", "PCIe", "Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_SAT_SMB:
		m = (typeof(m)){"LPe121", "PCIe", "Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_SAT_DCSP:
		m = (typeof(m)){"LPe12002-SP", "PCIe", "Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_SAT_SCSP:
		m = (typeof(m)){"LPe12000-SP", "PCIe", "Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_SAT_S:
		m = (typeof(m)){"LPe12000-S", "PCIe", "Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_HORNET:
		m = (typeof(m)){"LP21000", "PCIe",
				"Obsolete, Unsupported FCoE Adapter"};
		GE = 1;
		break;
	case PCI_DEVICE_ID_PROTEUS_VF:
		m = (typeof(m)){"LPev12000", "PCIe IOV",
				"Obsolete, Unsupported Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_PROTEUS_PF:
		m = (typeof(m)){"LPev12000", "PCIe IOV",
				"Obsolete, Unsupported Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_PROTEUS_S:
		m = (typeof(m)){"LPemv12002-S", "PCIe IOV",
				"Obsolete, Unsupported Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_TIGERSHARK:
		oneConnect = 1;
		m = (typeof(m)){"OCe10100", "PCIe", "FCoE"};
		break;
	case PCI_DEVICE_ID_TOMCAT:
		oneConnect = 1;
		m = (typeof(m)){"OCe11100", "PCIe", "FCoE"};
		break;
	case PCI_DEVICE_ID_FALCON:
		m = (typeof(m)){"LPSe12002-ML1-E", "PCIe",
				"EmulexSecure Fibre"};
		break;
	case PCI_DEVICE_ID_BALIUS:
		m = (typeof(m)){"LPVe12002", "PCIe Shared I/O",
				"Obsolete, Unsupported Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_LANCER_FC:
		m = (typeof(m)){"LPe16000", "PCIe", "Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_LANCER_FC_VF:
		m = (typeof(m)){"LPe16000", "PCIe",
				"Obsolete, Unsupported Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_LANCER_FCOE:
		oneConnect = 1;
		m = (typeof(m)){"OCe15100", "PCIe", "FCoE"};
		break;
	case PCI_DEVICE_ID_LANCER_FCOE_VF:
		oneConnect = 1;
		m = (typeof(m)){"OCe15100", "PCIe",
				"Obsolete, Unsupported FCoE"};
		break;
	case PCI_DEVICE_ID_LANCER_G6_FC:
		m = (typeof(m)){"LPe32000", "PCIe", "Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_LANCER_G7_FC:
		m = (typeof(m)){"LPe36000", "PCIe", "Fibre Channel Adapter"};
		break;
	case PCI_DEVICE_ID_SKYHAWK:
	case PCI_DEVICE_ID_SKYHAWK_VF:
		oneConnect = 1;
		m = (typeof(m)){"OCe14000", "PCIe", "FCoE"};
		break;
	default:
		m = (typeof(m)){"Unknown", "", ""};
		break;
	}

	if (mdp && mdp[0] == '\0')
		snprintf(mdp, 79,"%s", m.name);
	/*
	 * oneConnect hba requires special processing, they are all initiators
	 * and we put the port number on the end
	 */
	if (descp && descp[0] == '\0') {
		if (oneConnect)
			snprintf(descp, 255,
				"Emulex OneConnect %s, %s Initiator %s",
				m.name, m.function,
				phba->Port);
		else if (max_speed == 0)
			snprintf(descp, 255,
				"Emulex %s %s %s",
				m.name, m.bus, m.function);
		else
			snprintf(descp, 255,
				"Emulex %s %d%s %s %s",
				m.name, max_speed, (GE) ? "GE" : "Gb",
				m.bus, m.function);
	}
}

/**
 * lpfc_post_buffer - Post IOCB(s) with DMA buffer descriptor(s) to a IOCB ring
 * @phba: pointer to lpfc hba data structure.
 * @pring: pointer to a IOCB ring.
 * @cnt: the number of IOCBs to be posted to the IOCB ring.
 *
 * This routine posts a given number of IOCBs with the associated DMA buffer
 * descriptors specified by the cnt argument to the given IOCB ring.
 *
 * Return codes
 *   The number of IOCBs NOT able to be posted to the IOCB ring.
 **/
int
lpfc_post_buffer(struct lpfc_hba *phba, struct lpfc_sli_ring *pring, int cnt)
{
	IOCB_t *icmd;
	struct lpfc_iocbq *iocb;
	struct lpfc_dmabuf *mp1, *mp2;

	cnt += pring->missbufcnt;

	/* While there are buffers to post */
	while (cnt > 0) {
		/* Allocate buffer for  command iocb */
		iocb = lpfc_sli_get_iocbq(phba);
		if (iocb == NULL) {
			pring->missbufcnt = cnt;
			return cnt;
		}
		icmd = &iocb->iocb;

		/* 2 buffers can be posted per command */
		/* Allocate buffer to post */
		mp1 = kmalloc(sizeof (struct lpfc_dmabuf), GFP_KERNEL);
		if (mp1)
		    mp1->virt = lpfc_mbuf_alloc(phba, MEM_PRI, &mp1->phys);
		if (!mp1 || !mp1->virt) {
			kfree(mp1);
			lpfc_sli_release_iocbq(phba, iocb);
			pring->missbufcnt = cnt;
			return cnt;
		}

		INIT_LIST_HEAD(&mp1->list);
		/* Allocate buffer to post */
		if (cnt > 1) {
			mp2 = kmalloc(sizeof (struct lpfc_dmabuf), GFP_KERNEL);
			if (mp2)
				mp2->virt = lpfc_mbuf_alloc(phba, MEM_PRI,
							    &mp2->phys);
			if (!mp2 || !mp2->virt) {
				kfree(mp2);
				lpfc_mbuf_free(phba, mp1->virt, mp1->phys);
				kfree(mp1);
				lpfc_sli_release_iocbq(phba, iocb);
				pring->missbufcnt = cnt;
				return cnt;
			}

			INIT_LIST_HEAD(&mp2->list);
		} else {
			mp2 = NULL;
		}

		icmd->un.cont64[0].addrHigh = putPaddrHigh(mp1->phys);
		icmd->un.cont64[0].addrLow = putPaddrLow(mp1->phys);
		icmd->un.cont64[0].tus.f.bdeSize = FCELSSIZE;
		icmd->ulpBdeCount = 1;
		cnt--;
		if (mp2) {
			icmd->un.cont64[1].addrHigh = putPaddrHigh(mp2->phys);
			icmd->un.cont64[1].addrLow = putPaddrLow(mp2->phys);
			icmd->un.cont64[1].tus.f.bdeSize = FCELSSIZE;
			cnt--;
			icmd->ulpBdeCount = 2;
		}

		icmd->ulpCommand = CMD_QUE_RING_BUF64_CN;
		icmd->ulpLe = 1;

		if (lpfc_sli_issue_iocb(phba, pring->ringno, iocb, 0) ==
		    IOCB_ERROR) {
			lpfc_mbuf_free(phba, mp1->virt, mp1->phys);
			kfree(mp1);
			cnt++;
			if (mp2) {
				lpfc_mbuf_free(phba, mp2->virt, mp2->phys);
				kfree(mp2);
				cnt++;
			}
			lpfc_sli_release_iocbq(phba, iocb);
			pring->missbufcnt = cnt;
			return cnt;
		}
		lpfc_sli_ringpostbuf_put(phba, pring, mp1);
		if (mp2)
			lpfc_sli_ringpostbuf_put(phba, pring, mp2);
	}
	pring->missbufcnt = 0;
	return 0;
}

/**
 * lpfc_post_rcv_buf - Post the initial receive IOCB buffers to ELS ring
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine posts initial receive IOCB buffers to the ELS ring. The
 * current number of initial IOCB buffers specified by LPFC_BUF_RING0 is
 * set to 64 IOCBs. SLI3 only.
 *
 * Return codes
 *   0 - success (currently always success)
 **/
static int
lpfc_post_rcv_buf(struct lpfc_hba *phba)
{
	struct lpfc_sli *psli = &phba->sli;

	/* Ring 0, ELS / CT buffers */
	lpfc_post_buffer(phba, &psli->sli3_ring[LPFC_ELS_RING], LPFC_BUF_RING0);
	/* Ring 2 - FCP no buffers needed */

	return 0;
}

#define S(N,V) (((V)<<(N))|((V)>>(32-(N))))

/**
 * lpfc_sha_init - Set up initial array of hash table entries
 * @HashResultPointer: pointer to an array as hash table.
 *
 * This routine sets up the initial values to the array of hash table entries
 * for the LC HBAs.
 **/
static void
lpfc_sha_init(uint32_t * HashResultPointer)
{
	HashResultPointer[0] = 0x67452301;
	HashResultPointer[1] = 0xEFCDAB89;
	HashResultPointer[2] = 0x98BADCFE;
	HashResultPointer[3] = 0x10325476;
	HashResultPointer[4] = 0xC3D2E1F0;
}

/**
 * lpfc_sha_iterate - Iterate initial hash table with the working hash table
 * @HashResultPointer: pointer to an initial/result hash table.
 * @HashWorkingPointer: pointer to an working hash table.
 *
 * This routine iterates an initial hash table pointed by @HashResultPointer
 * with the values from the working hash table pointeed by @HashWorkingPointer.
 * The results are putting back to the initial hash table, returned through
 * the @HashResultPointer as the result hash table.
 **/
static void
lpfc_sha_iterate(uint32_t * HashResultPointer, uint32_t * HashWorkingPointer)
{
	int t;
	uint32_t TEMP;
	uint32_t A, B, C, D, E;
	t = 16;
	do {
		HashWorkingPointer[t] =
		    S(1,
		      HashWorkingPointer[t - 3] ^ HashWorkingPointer[t -
								     8] ^
		      HashWorkingPointer[t - 14] ^ HashWorkingPointer[t - 16]);
	} while (++t <= 79);
	t = 0;
	A = HashResultPointer[0];
	B = HashResultPointer[1];
	C = HashResultPointer[2];
	D = HashResultPointer[3];
	E = HashResultPointer[4];

	do {
		if (t < 20) {
			TEMP = ((B & C) | ((~B) & D)) + 0x5A827999;
		} else if (t < 40) {
			TEMP = (B ^ C ^ D) + 0x6ED9EBA1;
		} else if (t < 60) {
			TEMP = ((B & C) | (B & D) | (C & D)) + 0x8F1BBCDC;
		} else {
			TEMP = (B ^ C ^ D) + 0xCA62C1D6;
		}
		TEMP += S(5, A) + E + HashWorkingPointer[t];
		E = D;
		D = C;
		C = S(30, B);
		B = A;
		A = TEMP;
	} while (++t <= 79);

	HashResultPointer[0] += A;
	HashResultPointer[1] += B;
	HashResultPointer[2] += C;
	HashResultPointer[3] += D;
	HashResultPointer[4] += E;

}

/**
 * lpfc_challenge_key - Create challenge key based on WWPN of the HBA
 * @RandomChallenge: pointer to the entry of host challenge random number array.
 * @HashWorking: pointer to the entry of the working hash array.
 *
 * This routine calculates the working hash array referred by @HashWorking
 * from the challenge random numbers associated with the host, referred by
 * @RandomChallenge. The result is put into the entry of the working hash
 * array and returned by reference through @HashWorking.
 **/
static void
lpfc_challenge_key(uint32_t * RandomChallenge, uint32_t * HashWorking)
{
	*HashWorking = (*RandomChallenge ^ *HashWorking);
}

/**
 * lpfc_hba_init - Perform special handling for LC HBA initialization
 * @phba: pointer to lpfc hba data structure.
 * @hbainit: pointer to an array of unsigned 32-bit integers.
 *
 * This routine performs the special handling for LC HBA initialization.
 **/
void
lpfc_hba_init(struct lpfc_hba *phba, uint32_t *hbainit)
{
	int t;
	uint32_t *HashWorking;
	uint32_t *pwwnn = (uint32_t *) phba->wwnn;

	HashWorking = kcalloc(80, sizeof(uint32_t), GFP_KERNEL);
	if (!HashWorking)
		return;

	HashWorking[0] = HashWorking[78] = *pwwnn++;
	HashWorking[1] = HashWorking[79] = *pwwnn;

	for (t = 0; t < 7; t++)
		lpfc_challenge_key(phba->RandomData + t, HashWorking + t);

	lpfc_sha_init(hbainit);
	lpfc_sha_iterate(hbainit, HashWorking);
	kfree(HashWorking);
}

/**
 * lpfc_cleanup - Performs vport cleanups before deleting a vport
 * @vport: pointer to a virtual N_Port data structure.
 *
 * This routine performs the necessary cleanups before deleting the @vport.
 * It invokes the discovery state machine to perform necessary state
 * transitions and to release the ndlps associated with the @vport. Note,
 * the physical port is treated as @vport 0.
 **/
void
lpfc_cleanup(struct lpfc_vport *vport)
{
	struct lpfc_hba   *phba = vport->phba;
	struct lpfc_nodelist *ndlp, *next_ndlp;
	int i = 0;

	if (phba->link_state > LPFC_LINK_DOWN)
		lpfc_port_link_failure(vport);

	list_for_each_entry_safe(ndlp, next_ndlp, &vport->fc_nodes, nlp_listp) {
		if (!NLP_CHK_NODE_ACT(ndlp)) {
			ndlp = lpfc_enable_node(vport, ndlp,
						NLP_STE_UNUSED_NODE);
			if (!ndlp)
				continue;
			spin_lock_irq(&phba->ndlp_lock);
			NLP_SET_FREE_REQ(ndlp);
			spin_unlock_irq(&phba->ndlp_lock);
			/* Trigger the release of the ndlp memory */
			lpfc_nlp_put(ndlp);
			continue;
		}
		spin_lock_irq(&phba->ndlp_lock);
		if (NLP_CHK_FREE_REQ(ndlp)) {
			/* The ndlp should not be in memory free mode already */
			spin_unlock_irq(&phba->ndlp_lock);
			continue;
		} else
			/* Indicate request for freeing ndlp memory */
			NLP_SET_FREE_REQ(ndlp);
		spin_unlock_irq(&phba->ndlp_lock);

		if (vport->port_type != LPFC_PHYSICAL_PORT &&
		    ndlp->nlp_DID == Fabric_DID) {
			/* Just free up ndlp with Fabric_DID for vports */
			lpfc_nlp_put(ndlp);
			continue;
		}

		/* take care of nodes in unused state before the state
		 * machine taking action.
		 */
		if (ndlp->nlp_state == NLP_STE_UNUSED_NODE) {
			lpfc_nlp_put(ndlp);
			continue;
		}

		if (ndlp->nlp_type & NLP_FABRIC)
			lpfc_disc_state_machine(vport, ndlp, NULL,
					NLP_EVT_DEVICE_RECOVERY);

		lpfc_disc_state_machine(vport, ndlp, NULL,
					     NLP_EVT_DEVICE_RM);
	}

	/* At this point, ALL ndlp's should be gone
	 * because of the previous NLP_EVT_DEVICE_RM.
	 * Lets wait for this to happen, if needed.
	 */
	while (!list_empty(&vport->fc_nodes)) {
		if (i++ > 3000) {
			lpfc_printf_vlog(vport, KERN_ERR, LOG_DISCOVERY,
				"0233 Nodelist not empty\n");
			list_for_each_entry_safe(ndlp, next_ndlp,
						&vport->fc_nodes, nlp_listp) {
				lpfc_printf_vlog(ndlp->vport, KERN_ERR,
						LOG_NODE,
						"0282 did:x%x ndlp:x%px "
						"usgmap:x%x refcnt:%d\n",
						ndlp->nlp_DID, (void *)ndlp,
						ndlp->nlp_usg_map,
						kref_read(&ndlp->kref));
			}
			break;
		}

		/* Wait for any activity on ndlps to settle */
		msleep(10);
	}
	lpfc_cleanup_vports_rrqs(vport, NULL);
}

/**
 * lpfc_stop_vport_timers - Stop all the timers associated with a vport
 * @vport: pointer to a virtual N_Port data structure.
 *
 * This routine stops all the timers associated with a @vport. This function
 * is invoked before disabling or deleting a @vport. Note that the physical
 * port is treated as @vport 0.
 **/
void
lpfc_stop_vport_timers(struct lpfc_vport *vport)
{
	del_timer_sync(&vport->els_tmofunc);
	del_timer_sync(&vport->delayed_disc_tmo);
	lpfc_can_disctmo(vport);
	return;
}

/**
 * __lpfc_sli4_stop_fcf_redisc_wait_timer - Stop FCF rediscovery wait timer
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine stops the SLI4 FCF rediscover wait timer if it's on. The
 * caller of this routine should already hold the host lock.
 **/
void
__lpfc_sli4_stop_fcf_redisc_wait_timer(struct lpfc_hba *phba)
{
	/* Clear pending FCF rediscovery wait flag */
	phba->fcf.fcf_flag &= ~FCF_REDISC_PEND;

	/* Now, try to stop the timer */
	del_timer(&phba->fcf.redisc_wait);
}

/**
 * lpfc_sli4_stop_fcf_redisc_wait_timer - Stop FCF rediscovery wait timer
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine stops the SLI4 FCF rediscover wait timer if it's on. It
 * checks whether the FCF rediscovery wait timer is pending with the host
 * lock held before proceeding with disabling the timer and clearing the
 * wait timer pendig flag.
 **/
void
lpfc_sli4_stop_fcf_redisc_wait_timer(struct lpfc_hba *phba)
{
	spin_lock_irq(&phba->hbalock);
	if (!(phba->fcf.fcf_flag & FCF_REDISC_PEND)) {
		/* FCF rediscovery timer already fired or stopped */
		spin_unlock_irq(&phba->hbalock);
		return;
	}
	__lpfc_sli4_stop_fcf_redisc_wait_timer(phba);
	/* Clear failover in progress flags */
	phba->fcf.fcf_flag &= ~(FCF_DEAD_DISC | FCF_ACVL_DISC);
	spin_unlock_irq(&phba->hbalock);
}

/**
 * lpfc_stop_hba_timers - Stop all the timers associated with an HBA
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine stops all the timers associated with a HBA. This function is
 * invoked before either putting a HBA offline or unloading the driver.
 **/
void
lpfc_stop_hba_timers(struct lpfc_hba *phba)
{
	if (phba->pport)
		lpfc_stop_vport_timers(phba->pport);
	cancel_delayed_work_sync(&phba->eq_delay_work);
	del_timer_sync(&phba->sli.mbox_tmo);
	del_timer_sync(&phba->fabric_block_timer);
	del_timer_sync(&phba->eratt_poll);
	del_timer_sync(&phba->hb_tmofunc);
	if (phba->sli_rev == LPFC_SLI_REV4) {
		del_timer_sync(&phba->rrq_tmr);
		phba->hba_flag &= ~HBA_RRQ_ACTIVE;
	}
	phba->hb_outstanding = 0;

	switch (phba->pci_dev_grp) {
	case LPFC_PCI_DEV_LP:
		/* Stop any LightPulse device specific driver timers */
		del_timer_sync(&phba->fcp_poll_timer);
		break;
	case LPFC_PCI_DEV_OC:
		/* Stop any OneConnect device specific driver timers */
		lpfc_sli4_stop_fcf_redisc_wait_timer(phba);
		break;
	default:
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0297 Invalid device group (x%x)\n",
				phba->pci_dev_grp);
		break;
	}
	return;
}

/**
 * lpfc_block_mgmt_io - Mark a HBA's management interface as blocked
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine marks a HBA's management interface as blocked. Once the HBA's
 * management interface is marked as blocked, all the user space access to
 * the HBA, whether they are from sysfs interface or libdfc interface will
 * all be blocked. The HBA is set to block the management interface when the
 * driver prepares the HBA interface for online or offline.
 **/
static void
lpfc_block_mgmt_io(struct lpfc_hba *phba, int mbx_action)
{
	unsigned long iflag;
	uint8_t actcmd = MBX_HEARTBEAT;
	unsigned long timeout;

	spin_lock_irqsave(&phba->hbalock, iflag);
	phba->sli.sli_flag |= LPFC_BLOCK_MGMT_IO;
	spin_unlock_irqrestore(&phba->hbalock, iflag);
	if (mbx_action == LPFC_MBX_NO_WAIT)
		return;
	timeout = msecs_to_jiffies(LPFC_MBOX_TMO * 1000) + jiffies;
	spin_lock_irqsave(&phba->hbalock, iflag);
	if (phba->sli.mbox_active) {
		actcmd = phba->sli.mbox_active->u.mb.mbxCommand;
		/* Determine how long we might wait for the active mailbox
		 * command to be gracefully completed by firmware.
		 */
		timeout = msecs_to_jiffies(lpfc_mbox_tmo_val(phba,
				phba->sli.mbox_active) * 1000) + jiffies;
	}
	spin_unlock_irqrestore(&phba->hbalock, iflag);

	/* Wait for the outstnading mailbox command to complete */
	while (phba->sli.mbox_active) {
		/* Check active mailbox complete status every 2ms */
		msleep(2);
		if (time_after(jiffies, timeout)) {
			lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
				"2813 Mgmt IO is Blocked %x "
				"- mbox cmd %x still active\n",
				phba->sli.sli_flag, actcmd);
			break;
		}
	}
}

/**
 * lpfc_sli4_node_prep - Assign RPIs for active nodes.
 * @phba: pointer to lpfc hba data structure.
 *
 * Allocate RPIs for all active remote nodes. This is needed whenever
 * an SLI4 adapter is reset and the driver is not unloading. Its purpose
 * is to fixup the temporary rpi assignments.
 **/
void
lpfc_sli4_node_prep(struct lpfc_hba *phba)
{
	struct lpfc_nodelist  *ndlp, *next_ndlp;
	struct lpfc_vport **vports;
	int i, rpi;
	unsigned long flags;

	if (phba->sli_rev != LPFC_SLI_REV4)
		return;

	vports = lpfc_create_vport_work_array(phba);
	if (vports == NULL)
		return;

	for (i = 0; i <= phba->max_vports && vports[i] != NULL; i++) {
		if (vports[i]->load_flag & FC_UNLOADING)
			continue;

		list_for_each_entry_safe(ndlp, next_ndlp,
					 &vports[i]->fc_nodes,
					 nlp_listp) {
			if (!NLP_CHK_NODE_ACT(ndlp))
				continue;
			rpi = lpfc_sli4_alloc_rpi(phba);
			if (rpi == LPFC_RPI_ALLOC_ERROR) {
				spin_lock_irqsave(&phba->ndlp_lock, flags);
				NLP_CLR_NODE_ACT(ndlp);
				spin_unlock_irqrestore(&phba->ndlp_lock, flags);
				continue;
			}
			ndlp->nlp_rpi = rpi;
			lpfc_printf_vlog(ndlp->vport, KERN_INFO,
					 LOG_NODE | LOG_DISCOVERY,
					 "0009 Assign RPI x%x to ndlp x%px "
					 "DID:x%06x flg:x%x map:x%x\n",
					 ndlp->nlp_rpi, ndlp, ndlp->nlp_DID,
					 ndlp->nlp_flag, ndlp->nlp_usg_map);
		}
	}
	lpfc_destroy_vport_work_array(phba, vports);
}

/**
 * lpfc_create_expedite_pool - create expedite pool
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine moves a batch of XRIs from lpfc_io_buf_list_put of HWQ 0
 * to expedite pool. Mark them as expedite.
 **/
static void lpfc_create_expedite_pool(struct lpfc_hba *phba)
{
	struct lpfc_sli4_hdw_queue *qp;
	struct lpfc_io_buf *lpfc_ncmd;
	struct lpfc_io_buf *lpfc_ncmd_next;
	struct lpfc_epd_pool *epd_pool;
	unsigned long iflag;

	epd_pool = &phba->epd_pool;
	qp = &phba->sli4_hba.hdwq[0];

	spin_lock_init(&epd_pool->lock);
	spin_lock_irqsave(&qp->io_buf_list_put_lock, iflag);
	spin_lock(&epd_pool->lock);
	INIT_LIST_HEAD(&epd_pool->list);
	list_for_each_entry_safe(lpfc_ncmd, lpfc_ncmd_next,
				 &qp->lpfc_io_buf_list_put, list) {
		list_move_tail(&lpfc_ncmd->list, &epd_pool->list);
		lpfc_ncmd->expedite = true;
		qp->put_io_bufs--;
		epd_pool->count++;
		if (epd_pool->count >= XRI_BATCH)
			break;
	}
	spin_unlock(&epd_pool->lock);
	spin_unlock_irqrestore(&qp->io_buf_list_put_lock, iflag);
}

/**
 * lpfc_destroy_expedite_pool - destroy expedite pool
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine returns XRIs from expedite pool to lpfc_io_buf_list_put
 * of HWQ 0. Clear the mark.
 **/
static void lpfc_destroy_expedite_pool(struct lpfc_hba *phba)
{
	struct lpfc_sli4_hdw_queue *qp;
	struct lpfc_io_buf *lpfc_ncmd;
	struct lpfc_io_buf *lpfc_ncmd_next;
	struct lpfc_epd_pool *epd_pool;
	unsigned long iflag;

	epd_pool = &phba->epd_pool;
	qp = &phba->sli4_hba.hdwq[0];

	spin_lock_irqsave(&qp->io_buf_list_put_lock, iflag);
	spin_lock(&epd_pool->lock);
	list_for_each_entry_safe(lpfc_ncmd, lpfc_ncmd_next,
				 &epd_pool->list, list) {
		list_move_tail(&lpfc_ncmd->list,
			       &qp->lpfc_io_buf_list_put);
		lpfc_ncmd->flags = false;
		qp->put_io_bufs++;
		epd_pool->count--;
	}
	spin_unlock(&epd_pool->lock);
	spin_unlock_irqrestore(&qp->io_buf_list_put_lock, iflag);
}

/**
 * lpfc_create_multixri_pools - create multi-XRI pools
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine initialize public, private per HWQ. Then, move XRIs from
 * lpfc_io_buf_list_put to public pool. High and low watermark are also
 * Initialized.
 **/
void lpfc_create_multixri_pools(struct lpfc_hba *phba)
{
	u32 i, j;
	u32 hwq_count;
	u32 count_per_hwq;
	struct lpfc_io_buf *lpfc_ncmd;
	struct lpfc_io_buf *lpfc_ncmd_next;
	unsigned long iflag;
	struct lpfc_sli4_hdw_queue *qp;
	struct lpfc_multixri_pool *multixri_pool;
	struct lpfc_pbl_pool *pbl_pool;
	struct lpfc_pvt_pool *pvt_pool;

	lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
			"1234 num_hdw_queue=%d num_present_cpu=%d common_xri_cnt=%d\n",
			phba->cfg_hdw_queue, phba->sli4_hba.num_present_cpu,
			phba->sli4_hba.io_xri_cnt);

	if (phba->cfg_enable_fc4_type & LPFC_ENABLE_NVME)
		lpfc_create_expedite_pool(phba);

	hwq_count = phba->cfg_hdw_queue;
	count_per_hwq = phba->sli4_hba.io_xri_cnt / hwq_count;

	for (i = 0; i < hwq_count; i++) {
		multixri_pool = kzalloc(sizeof(*multixri_pool), GFP_KERNEL);

		if (!multixri_pool) {
			lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
					"1238 Failed to allocate memory for "
					"multixri_pool\n");

			if (phba->cfg_enable_fc4_type & LPFC_ENABLE_NVME)
				lpfc_destroy_expedite_pool(phba);

			j = 0;
			while (j < i) {
				qp = &phba->sli4_hba.hdwq[j];
				kfree(qp->p_multixri_pool);
				j++;
			}
			phba->cfg_xri_rebalancing = 0;
			return;
		}

		qp = &phba->sli4_hba.hdwq[i];
		qp->p_multixri_pool = multixri_pool;

		multixri_pool->xri_limit = count_per_hwq;
		multixri_pool->rrb_next_hwqid = i;

		/* Deal with public free xri pool */
		pbl_pool = &multixri_pool->pbl_pool;
		spin_lock_init(&pbl_pool->lock);
		spin_lock_irqsave(&qp->io_buf_list_put_lock, iflag);
		spin_lock(&pbl_pool->lock);
		INIT_LIST_HEAD(&pbl_pool->list);
		list_for_each_entry_safe(lpfc_ncmd, lpfc_ncmd_next,
					 &qp->lpfc_io_buf_list_put, list) {
			list_move_tail(&lpfc_ncmd->list, &pbl_pool->list);
			qp->put_io_bufs--;
			pbl_pool->count++;
		}
		lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
				"1235 Moved %d buffers from PUT list over to pbl_pool[%d]\n",
				pbl_pool->count, i);
		spin_unlock(&pbl_pool->lock);
		spin_unlock_irqrestore(&qp->io_buf_list_put_lock, iflag);

		/* Deal with private free xri pool */
		pvt_pool = &multixri_pool->pvt_pool;
		pvt_pool->high_watermark = multixri_pool->xri_limit / 2;
		pvt_pool->low_watermark = XRI_BATCH;
		spin_lock_init(&pvt_pool->lock);
		spin_lock_irqsave(&pvt_pool->lock, iflag);
		INIT_LIST_HEAD(&pvt_pool->list);
		pvt_pool->count = 0;
		spin_unlock_irqrestore(&pvt_pool->lock, iflag);
	}
}

/**
 * lpfc_destroy_multixri_pools - destroy multi-XRI pools
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine returns XRIs from public/private to lpfc_io_buf_list_put.
 **/
static void lpfc_destroy_multixri_pools(struct lpfc_hba *phba)
{
	u32 i;
	u32 hwq_count;
	struct lpfc_io_buf *lpfc_ncmd;
	struct lpfc_io_buf *lpfc_ncmd_next;
	unsigned long iflag;
	struct lpfc_sli4_hdw_queue *qp;
	struct lpfc_multixri_pool *multixri_pool;
	struct lpfc_pbl_pool *pbl_pool;
	struct lpfc_pvt_pool *pvt_pool;

	if (phba->cfg_enable_fc4_type & LPFC_ENABLE_NVME)
		lpfc_destroy_expedite_pool(phba);

	if (!(phba->pport->load_flag & FC_UNLOADING))
		lpfc_sli_flush_io_rings(phba);

	hwq_count = phba->cfg_hdw_queue;

	for (i = 0; i < hwq_count; i++) {
		qp = &phba->sli4_hba.hdwq[i];
		multixri_pool = qp->p_multixri_pool;
		if (!multixri_pool)
			continue;

		qp->p_multixri_pool = NULL;

		spin_lock_irqsave(&qp->io_buf_list_put_lock, iflag);

		/* Deal with public free xri pool */
		pbl_pool = &multixri_pool->pbl_pool;
		spin_lock(&pbl_pool->lock);

		lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
				"1236 Moving %d buffers from pbl_pool[%d] TO PUT list\n",
				pbl_pool->count, i);

		list_for_each_entry_safe(lpfc_ncmd, lpfc_ncmd_next,
					 &pbl_pool->list, list) {
			list_move_tail(&lpfc_ncmd->list,
				       &qp->lpfc_io_buf_list_put);
			qp->put_io_bufs++;
			pbl_pool->count--;
		}

		INIT_LIST_HEAD(&pbl_pool->list);
		pbl_pool->count = 0;

		spin_unlock(&pbl_pool->lock);

		/* Deal with private free xri pool */
		pvt_pool = &multixri_pool->pvt_pool;
		spin_lock(&pvt_pool->lock);

		lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
				"1237 Moving %d buffers from pvt_pool[%d] TO PUT list\n",
				pvt_pool->count, i);

		list_for_each_entry_safe(lpfc_ncmd, lpfc_ncmd_next,
					 &pvt_pool->list, list) {
			list_move_tail(&lpfc_ncmd->list,
				       &qp->lpfc_io_buf_list_put);
			qp->put_io_bufs++;
			pvt_pool->count--;
		}

		INIT_LIST_HEAD(&pvt_pool->list);
		pvt_pool->count = 0;

		spin_unlock(&pvt_pool->lock);
		spin_unlock_irqrestore(&qp->io_buf_list_put_lock, iflag);

		kfree(multixri_pool);
	}
}

/**
 * lpfc_online - Initialize and bring a HBA online
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine initializes the HBA and brings a HBA online. During this
 * process, the management interface is blocked to prevent user space access
 * to the HBA interfering with the driver initialization.
 *
 * Return codes
 *   0 - successful
 *   1 - failed
 **/
int
lpfc_online(struct lpfc_hba *phba)
{
	struct lpfc_vport *vport;
	struct lpfc_vport **vports;
	int i, error = 0;
	bool vpis_cleared = false;

	if (!phba)
		return 0;
	vport = phba->pport;

	if (!(vport->fc_flag & FC_OFFLINE_MODE))
		return 0;

	lpfc_printf_log(phba, KERN_WARNING, LOG_INIT,
			"0458 Bring Adapter online\n");

	lpfc_block_mgmt_io(phba, LPFC_MBX_WAIT);

	if (phba->sli_rev == LPFC_SLI_REV4) {
		if (lpfc_sli4_hba_setup(phba)) { /* Initialize SLI4 HBA */
			lpfc_unblock_mgmt_io(phba);
			return 1;
		}
		spin_lock_irq(&phba->hbalock);
		if (!phba->sli4_hba.max_cfg_param.vpi_used)
			vpis_cleared = true;
		spin_unlock_irq(&phba->hbalock);

		/* Reestablish the local initiator port.
		 * The offline process destroyed the previous lport.
		 */
		if (phba->cfg_enable_fc4_type & LPFC_ENABLE_NVME &&
				!phba->nvmet_support) {
			error = lpfc_nvme_create_localport(phba->pport);
			if (error)
				lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
					"6132 NVME restore reg failed "
					"on nvmei error x%x\n", error);
		}
	} else {
		lpfc_sli_queue_init(phba);
		if (lpfc_sli_hba_setup(phba)) {	/* Initialize SLI2/SLI3 HBA */
			lpfc_unblock_mgmt_io(phba);
			return 1;
		}
	}

	vports = lpfc_create_vport_work_array(phba);
	if (vports != NULL) {
		for (i = 0; i <= phba->max_vports && vports[i] != NULL; i++) {
			struct Scsi_Host *shost;
			shost = lpfc_shost_from_vport(vports[i]);
			spin_lock_irq(shost->host_lock);
			vports[i]->fc_flag &= ~FC_OFFLINE_MODE;
			if (phba->sli3_options & LPFC_SLI3_NPIV_ENABLED)
				vports[i]->fc_flag |= FC_VPORT_NEEDS_REG_VPI;
			if (phba->sli_rev == LPFC_SLI_REV4) {
				vports[i]->fc_flag |= FC_VPORT_NEEDS_INIT_VPI;
				if ((vpis_cleared) &&
				    (vports[i]->port_type !=
					LPFC_PHYSICAL_PORT))
					vports[i]->vpi = 0;
			}
			spin_unlock_irq(shost->host_lock);
		}
	}
	lpfc_destroy_vport_work_array(phba, vports);

	if (phba->cfg_xri_rebalancing)
		lpfc_create_multixri_pools(phba);

	lpfc_cpuhp_add(phba);

	lpfc_unblock_mgmt_io(phba);
	return 0;
}

/**
 * lpfc_unblock_mgmt_io - Mark a HBA's management interface to be not blocked
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine marks a HBA's management interface as not blocked. Once the
 * HBA's management interface is marked as not blocked, all the user space
 * access to the HBA, whether they are from sysfs interface or libdfc
 * interface will be allowed. The HBA is set to block the management interface
 * when the driver prepares the HBA interface for online or offline and then
 * set to unblock the management interface afterwards.
 **/
void
lpfc_unblock_mgmt_io(struct lpfc_hba * phba)
{
	unsigned long iflag;

	spin_lock_irqsave(&phba->hbalock, iflag);
	phba->sli.sli_flag &= ~LPFC_BLOCK_MGMT_IO;
	spin_unlock_irqrestore(&phba->hbalock, iflag);
}

/**
 * lpfc_offline_prep - Prepare a HBA to be brought offline
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to prepare a HBA to be brought offline. It performs
 * unregistration login to all the nodes on all vports and flushes the mailbox
 * queue to make it ready to be brought offline.
 **/
void
lpfc_offline_prep(struct lpfc_hba *phba, int mbx_action)
{
	struct lpfc_vport *vport = phba->pport;
	struct lpfc_nodelist  *ndlp, *next_ndlp;
	struct lpfc_vport **vports;
	struct Scsi_Host *shost;
	int i;

	if (vport->fc_flag & FC_OFFLINE_MODE)
		return;

	lpfc_block_mgmt_io(phba, mbx_action);

	lpfc_linkdown(phba);

	/* Issue an unreg_login to all nodes on all vports */
	vports = lpfc_create_vport_work_array(phba);
	if (vports != NULL) {
		for (i = 0; i <= phba->max_vports && vports[i] != NULL; i++) {
			if (vports[i]->load_flag & FC_UNLOADING)
				continue;
			shost = lpfc_shost_from_vport(vports[i]);
			spin_lock_irq(shost->host_lock);
			vports[i]->vpi_state &= ~LPFC_VPI_REGISTERED;
			vports[i]->fc_flag |= FC_VPORT_NEEDS_REG_VPI;
			vports[i]->fc_flag &= ~FC_VFI_REGISTERED;
			spin_unlock_irq(shost->host_lock);

			shost =	lpfc_shost_from_vport(vports[i]);
			list_for_each_entry_safe(ndlp, next_ndlp,
						 &vports[i]->fc_nodes,
						 nlp_listp) {
				if ((!NLP_CHK_NODE_ACT(ndlp)) ||
				    ndlp->nlp_state == NLP_STE_UNUSED_NODE) {
					/* Driver must assume RPI is invalid for
					 * any unused or inactive node.
					 */
					ndlp->nlp_rpi = LPFC_RPI_ALLOC_ERROR;
					continue;
				}

				if (ndlp->nlp_type & NLP_FABRIC) {
					lpfc_disc_state_machine(vports[i], ndlp,
						NULL, NLP_EVT_DEVICE_RECOVERY);
					lpfc_disc_state_machine(vports[i], ndlp,
						NULL, NLP_EVT_DEVICE_RM);
				}
				spin_lock_irq(shost->host_lock);
				ndlp->nlp_flag &= ~NLP_NPR_ADISC;
				spin_unlock_irq(shost->host_lock);
				/*
				 * Whenever an SLI4 port goes offline, free the
				 * RPI. Get a new RPI when the adapter port
				 * comes back online.
				 */
				if (phba->sli_rev == LPFC_SLI_REV4) {
					lpfc_printf_vlog(ndlp->vport, KERN_INFO,
						 LOG_NODE | LOG_DISCOVERY,
						 "0011 Free RPI x%x on "
						 "ndlp:x%px did x%x "
						 "usgmap:x%x\n",
						 ndlp->nlp_rpi, ndlp,
						 ndlp->nlp_DID,
						 ndlp->nlp_usg_map);
					lpfc_sli4_free_rpi(phba, ndlp->nlp_rpi);
					ndlp->nlp_rpi = LPFC_RPI_ALLOC_ERROR;
				}
				lpfc_unreg_rpi(vports[i], ndlp);
			}
		}
	}
	lpfc_destroy_vport_work_array(phba, vports);

	lpfc_sli_mbox_sys_shutdown(phba, mbx_action);

	if (phba->wq)
		flush_workqueue(phba->wq);
}

/**
 * lpfc_offline - Bring a HBA offline
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine actually brings a HBA offline. It stops all the timers
 * associated with the HBA, brings down the SLI layer, and eventually
 * marks the HBA as in offline state for the upper layer protocol.
 **/
void
lpfc_offline(struct lpfc_hba *phba)
{
	struct Scsi_Host  *shost;
	struct lpfc_vport **vports;
	int i;

	if (phba->pport->fc_flag & FC_OFFLINE_MODE)
		return;

	/* stop port and all timers associated with this hba */
	lpfc_stop_port(phba);

	/* Tear down the local and target port registrations.  The
	 * nvme transports need to cleanup.
	 */
	lpfc_nvmet_destroy_targetport(phba);
	lpfc_nvme_destroy_localport(phba->pport);

	vports = lpfc_create_vport_work_array(phba);
	if (vports != NULL)
		for (i = 0; i <= phba->max_vports && vports[i] != NULL; i++)
			lpfc_stop_vport_timers(vports[i]);
	lpfc_destroy_vport_work_array(phba, vports);
	lpfc_printf_log(phba, KERN_WARNING, LOG_INIT,
			"0460 Bring Adapter offline\n");
	/* Bring down the SLI Layer and cleanup.  The HBA is offline
	   now.  */
	lpfc_sli_hba_down(phba);
	spin_lock_irq(&phba->hbalock);
	phba->work_ha = 0;
	spin_unlock_irq(&phba->hbalock);
	vports = lpfc_create_vport_work_array(phba);
	if (vports != NULL)
		for (i = 0; i <= phba->max_vports && vports[i] != NULL; i++) {
			shost = lpfc_shost_from_vport(vports[i]);
			spin_lock_irq(shost->host_lock);
			vports[i]->work_port_events = 0;
			vports[i]->fc_flag |= FC_OFFLINE_MODE;
			spin_unlock_irq(shost->host_lock);
		}
	lpfc_destroy_vport_work_array(phba, vports);
	__lpfc_cpuhp_remove(phba);

	if (phba->cfg_xri_rebalancing)
		lpfc_destroy_multixri_pools(phba);
}

/**
 * lpfc_scsi_free - Free all the SCSI buffers and IOCBs from driver lists
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is to free all the SCSI buffers and IOCBs from the driver
 * list back to kernel. It is called from lpfc_pci_remove_one to free
 * the internal resources before the device is removed from the system.
 **/
static void
lpfc_scsi_free(struct lpfc_hba *phba)
{
	struct lpfc_io_buf *sb, *sb_next;

	if (!(phba->cfg_enable_fc4_type & LPFC_ENABLE_FCP))
		return;

	spin_lock_irq(&phba->hbalock);

	/* Release all the lpfc_scsi_bufs maintained by this host. */

	spin_lock(&phba->scsi_buf_list_put_lock);
	list_for_each_entry_safe(sb, sb_next, &phba->lpfc_scsi_buf_list_put,
				 list) {
		list_del(&sb->list);
		dma_pool_free(phba->lpfc_sg_dma_buf_pool, sb->data,
			      sb->dma_handle);
		kfree(sb);
		phba->total_scsi_bufs--;
	}
	spin_unlock(&phba->scsi_buf_list_put_lock);

	spin_lock(&phba->scsi_buf_list_get_lock);
	list_for_each_entry_safe(sb, sb_next, &phba->lpfc_scsi_buf_list_get,
				 list) {
		list_del(&sb->list);
		dma_pool_free(phba->lpfc_sg_dma_buf_pool, sb->data,
			      sb->dma_handle);
		kfree(sb);
		phba->total_scsi_bufs--;
	}
	spin_unlock(&phba->scsi_buf_list_get_lock);
	spin_unlock_irq(&phba->hbalock);
}

/**
 * lpfc_io_free - Free all the IO buffers and IOCBs from driver lists
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is to free all the IO buffers and IOCBs from the driver
 * list back to kernel. It is called from lpfc_pci_remove_one to free
 * the internal resources before the device is removed from the system.
 **/
void
lpfc_io_free(struct lpfc_hba *phba)
{
	struct lpfc_io_buf *lpfc_ncmd, *lpfc_ncmd_next;
	struct lpfc_sli4_hdw_queue *qp;
	int idx;

	for (idx = 0; idx < phba->cfg_hdw_queue; idx++) {
		qp = &phba->sli4_hba.hdwq[idx];
		/* Release all the lpfc_nvme_bufs maintained by this host. */
		spin_lock(&qp->io_buf_list_put_lock);
		list_for_each_entry_safe(lpfc_ncmd, lpfc_ncmd_next,
					 &qp->lpfc_io_buf_list_put,
					 list) {
			list_del(&lpfc_ncmd->list);
			qp->put_io_bufs--;
			dma_pool_free(phba->lpfc_sg_dma_buf_pool,
				      lpfc_ncmd->data, lpfc_ncmd->dma_handle);
			if (phba->cfg_xpsgl && !phba->nvmet_support)
				lpfc_put_sgl_per_hdwq(phba, lpfc_ncmd);
			lpfc_put_cmd_rsp_buf_per_hdwq(phba, lpfc_ncmd);
			kfree(lpfc_ncmd);
			qp->total_io_bufs--;
		}
		spin_unlock(&qp->io_buf_list_put_lock);

		spin_lock(&qp->io_buf_list_get_lock);
		list_for_each_entry_safe(lpfc_ncmd, lpfc_ncmd_next,
					 &qp->lpfc_io_buf_list_get,
					 list) {
			list_del(&lpfc_ncmd->list);
			qp->get_io_bufs--;
			dma_pool_free(phba->lpfc_sg_dma_buf_pool,
				      lpfc_ncmd->data, lpfc_ncmd->dma_handle);
			if (phba->cfg_xpsgl && !phba->nvmet_support)
				lpfc_put_sgl_per_hdwq(phba, lpfc_ncmd);
			lpfc_put_cmd_rsp_buf_per_hdwq(phba, lpfc_ncmd);
			kfree(lpfc_ncmd);
			qp->total_io_bufs--;
		}
		spin_unlock(&qp->io_buf_list_get_lock);
	}
}

/**
 * lpfc_sli4_els_sgl_update - update ELS xri-sgl sizing and mapping
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine first calculates the sizes of the current els and allocated
 * scsi sgl lists, and then goes through all sgls to updates the physical
 * XRIs assigned due to port function reset. During port initialization, the
 * current els and allocated scsi sgl lists are 0s.
 *
 * Return codes
 *   0 - successful (for now, it always returns 0)
 **/
int
lpfc_sli4_els_sgl_update(struct lpfc_hba *phba)
{
	struct lpfc_sglq *sglq_entry = NULL, *sglq_entry_next = NULL;
	uint16_t i, lxri, xri_cnt, els_xri_cnt;
	LIST_HEAD(els_sgl_list);
	int rc;

	/*
	 * update on pci function's els xri-sgl list
	 */
	els_xri_cnt = lpfc_sli4_get_els_iocb_cnt(phba);

	if (els_xri_cnt > phba->sli4_hba.els_xri_cnt) {
		/* els xri-sgl expanded */
		xri_cnt = els_xri_cnt - phba->sli4_hba.els_xri_cnt;
		lpfc_printf_log(phba, KERN_INFO, LOG_SLI,
				"3157 ELS xri-sgl count increased from "
				"%d to %d\n", phba->sli4_hba.els_xri_cnt,
				els_xri_cnt);
		/* allocate the additional els sgls */
		for (i = 0; i < xri_cnt; i++) {
			sglq_entry = kzalloc(sizeof(struct lpfc_sglq),
					     GFP_KERNEL);
			if (sglq_entry == NULL) {
				lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
						"2562 Failure to allocate an "
						"ELS sgl entry:%d\n", i);
				rc = -ENOMEM;
				goto out_free_mem;
			}
			sglq_entry->buff_type = GEN_BUFF_TYPE;
			sglq_entry->virt = lpfc_mbuf_alloc(phba, 0,
							   &sglq_entry->phys);
			if (sglq_entry->virt == NULL) {
				kfree(sglq_entry);
				lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
						"2563 Failure to allocate an "
						"ELS mbuf:%d\n", i);
				rc = -ENOMEM;
				goto out_free_mem;
			}
			sglq_entry->sgl = sglq_entry->virt;
			memset(sglq_entry->sgl, 0, LPFC_BPL_SIZE);
			sglq_entry->state = SGL_FREED;
			list_add_tail(&sglq_entry->list, &els_sgl_list);
		}
		spin_lock_irq(&phba->hbalock);
		spin_lock(&phba->sli4_hba.sgl_list_lock);
		list_splice_init(&els_sgl_list,
				 &phba->sli4_hba.lpfc_els_sgl_list);
		spin_unlock(&phba->sli4_hba.sgl_list_lock);
		spin_unlock_irq(&phba->hbalock);
	} else if (els_xri_cnt < phba->sli4_hba.els_xri_cnt) {
		/* els xri-sgl shrinked */
		xri_cnt = phba->sli4_hba.els_xri_cnt - els_xri_cnt;
		lpfc_printf_log(phba, KERN_INFO, LOG_SLI,
				"3158 ELS xri-sgl count decreased from "
				"%d to %d\n", phba->sli4_hba.els_xri_cnt,
				els_xri_cnt);
		spin_lock_irq(&phba->hbalock);
		spin_lock(&phba->sli4_hba.sgl_list_lock);
		list_splice_init(&phba->sli4_hba.lpfc_els_sgl_list,
				 &els_sgl_list);
		/* release extra els sgls from list */
		for (i = 0; i < xri_cnt; i++) {
			list_remove_head(&els_sgl_list,
					 sglq_entry, struct lpfc_sglq, list);
			if (sglq_entry) {
				__lpfc_mbuf_free(phba, sglq_entry->virt,
						 sglq_entry->phys);
				kfree(sglq_entry);
			}
		}
		list_splice_init(&els_sgl_list,
				 &phba->sli4_hba.lpfc_els_sgl_list);
		spin_unlock(&phba->sli4_hba.sgl_list_lock);
		spin_unlock_irq(&phba->hbalock);
	} else
		lpfc_printf_log(phba, KERN_INFO, LOG_SLI,
				"3163 ELS xri-sgl count unchanged: %d\n",
				els_xri_cnt);
	phba->sli4_hba.els_xri_cnt = els_xri_cnt;

	/* update xris to els sgls on the list */
	sglq_entry = NULL;
	sglq_entry_next = NULL;
	list_for_each_entry_safe(sglq_entry, sglq_entry_next,
				 &phba->sli4_hba.lpfc_els_sgl_list, list) {
		lxri = lpfc_sli4_next_xritag(phba);
		if (lxri == NO_XRI) {
			lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
					"2400 Failed to allocate xri for "
					"ELS sgl\n");
			rc = -ENOMEM;
			goto out_free_mem;
		}
		sglq_entry->sli4_lxritag = lxri;
		sglq_entry->sli4_xritag = phba->sli4_hba.xri_ids[lxri];
	}
	return 0;

out_free_mem:
	lpfc_free_els_sgl_list(phba);
	return rc;
}

/**
 * lpfc_sli4_nvmet_sgl_update - update xri-sgl sizing and mapping
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine first calculates the sizes of the current els and allocated
 * scsi sgl lists, and then goes through all sgls to updates the physical
 * XRIs assigned due to port function reset. During port initialization, the
 * current els and allocated scsi sgl lists are 0s.
 *
 * Return codes
 *   0 - successful (for now, it always returns 0)
 **/
int
lpfc_sli4_nvmet_sgl_update(struct lpfc_hba *phba)
{
	struct lpfc_sglq *sglq_entry = NULL, *sglq_entry_next = NULL;
	uint16_t i, lxri, xri_cnt, els_xri_cnt;
	uint16_t nvmet_xri_cnt;
	LIST_HEAD(nvmet_sgl_list);
	int rc;

	/*
	 * update on pci function's nvmet xri-sgl list
	 */
	els_xri_cnt = lpfc_sli4_get_els_iocb_cnt(phba);

	/* For NVMET, ALL remaining XRIs are dedicated for IO processing */
	nvmet_xri_cnt = phba->sli4_hba.max_cfg_param.max_xri - els_xri_cnt;
	if (nvmet_xri_cnt > phba->sli4_hba.nvmet_xri_cnt) {
		/* els xri-sgl expanded */
		xri_cnt = nvmet_xri_cnt - phba->sli4_hba.nvmet_xri_cnt;
		lpfc_printf_log(phba, KERN_INFO, LOG_SLI,
				"6302 NVMET xri-sgl cnt grew from %d to %d\n",
				phba->sli4_hba.nvmet_xri_cnt, nvmet_xri_cnt);
		/* allocate the additional nvmet sgls */
		for (i = 0; i < xri_cnt; i++) {
			sglq_entry = kzalloc(sizeof(struct lpfc_sglq),
					     GFP_KERNEL);
			if (sglq_entry == NULL) {
				lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
						"6303 Failure to allocate an "
						"NVMET sgl entry:%d\n", i);
				rc = -ENOMEM;
				goto out_free_mem;
			}
			sglq_entry->buff_type = NVMET_BUFF_TYPE;
			sglq_entry->virt = lpfc_nvmet_buf_alloc(phba, 0,
							   &sglq_entry->phys);
			if (sglq_entry->virt == NULL) {
				kfree(sglq_entry);
				lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
						"6304 Failure to allocate an "
						"NVMET buf:%d\n", i);
				rc = -ENOMEM;
				goto out_free_mem;
			}
			sglq_entry->sgl = sglq_entry->virt;
			memset(sglq_entry->sgl, 0,
			       phba->cfg_sg_dma_buf_size);
			sglq_entry->state = SGL_FREED;
			list_add_tail(&sglq_entry->list, &nvmet_sgl_list);
		}
		spin_lock_irq(&phba->hbalock);
		spin_lock(&phba->sli4_hba.sgl_list_lock);
		list_splice_init(&nvmet_sgl_list,
				 &phba->sli4_hba.lpfc_nvmet_sgl_list);
		spin_unlock(&phba->sli4_hba.sgl_list_lock);
		spin_unlock_irq(&phba->hbalock);
	} else if (nvmet_xri_cnt < phba->sli4_hba.nvmet_xri_cnt) {
		/* nvmet xri-sgl shrunk */
		xri_cnt = phba->sli4_hba.nvmet_xri_cnt - nvmet_xri_cnt;
		lpfc_printf_log(phba, KERN_INFO, LOG_SLI,
				"6305 NVMET xri-sgl count decreased from "
				"%d to %d\n", phba->sli4_hba.nvmet_xri_cnt,
				nvmet_xri_cnt);
		spin_lock_irq(&phba->hbalock);
		spin_lock(&phba->sli4_hba.sgl_list_lock);
		list_splice_init(&phba->sli4_hba.lpfc_nvmet_sgl_list,
				 &nvmet_sgl_list);
		/* release extra nvmet sgls from list */
		for (i = 0; i < xri_cnt; i++) {
			list_remove_head(&nvmet_sgl_list,
					 sglq_entry, struct lpfc_sglq, list);
			if (sglq_entry) {
				lpfc_nvmet_buf_free(phba, sglq_entry->virt,
						    sglq_entry->phys);
				kfree(sglq_entry);
			}
		}
		list_splice_init(&nvmet_sgl_list,
				 &phba->sli4_hba.lpfc_nvmet_sgl_list);
		spin_unlock(&phba->sli4_hba.sgl_list_lock);
		spin_unlock_irq(&phba->hbalock);
	} else
		lpfc_printf_log(phba, KERN_INFO, LOG_SLI,
				"6306 NVMET xri-sgl count unchanged: %d\n",
				nvmet_xri_cnt);
	phba->sli4_hba.nvmet_xri_cnt = nvmet_xri_cnt;

	/* update xris to nvmet sgls on the list */
	sglq_entry = NULL;
	sglq_entry_next = NULL;
	list_for_each_entry_safe(sglq_entry, sglq_entry_next,
				 &phba->sli4_hba.lpfc_nvmet_sgl_list, list) {
		lxri = lpfc_sli4_next_xritag(phba);
		if (lxri == NO_XRI) {
			lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
					"6307 Failed to allocate xri for "
					"NVMET sgl\n");
			rc = -ENOMEM;
			goto out_free_mem;
		}
		sglq_entry->sli4_lxritag = lxri;
		sglq_entry->sli4_xritag = phba->sli4_hba.xri_ids[lxri];
	}
	return 0;

out_free_mem:
	lpfc_free_nvmet_sgl_list(phba);
	return rc;
}

int
lpfc_io_buf_flush(struct lpfc_hba *phba, struct list_head *cbuf)
{
	LIST_HEAD(blist);
	struct lpfc_sli4_hdw_queue *qp;
	struct lpfc_io_buf *lpfc_cmd;
	struct lpfc_io_buf *iobufp, *prev_iobufp;
	int idx, cnt, xri, inserted;

	cnt = 0;
	for (idx = 0; idx < phba->cfg_hdw_queue; idx++) {
		qp = &phba->sli4_hba.hdwq[idx];
		spin_lock_irq(&qp->io_buf_list_get_lock);
		spin_lock(&qp->io_buf_list_put_lock);

		/* Take everything off the get and put lists */
		list_splice_init(&qp->lpfc_io_buf_list_get, &blist);
		list_splice(&qp->lpfc_io_buf_list_put, &blist);
		INIT_LIST_HEAD(&qp->lpfc_io_buf_list_get);
		INIT_LIST_HEAD(&qp->lpfc_io_buf_list_put);
		cnt += qp->get_io_bufs + qp->put_io_bufs;
		qp->get_io_bufs = 0;
		qp->put_io_bufs = 0;
		qp->total_io_bufs = 0;
		spin_unlock(&qp->io_buf_list_put_lock);
		spin_unlock_irq(&qp->io_buf_list_get_lock);
	}

	/*
	 * Take IO buffers off blist and put on cbuf sorted by XRI.
	 * This is because POST_SGL takes a sequential range of XRIs
	 * to post to the firmware.
	 */
	for (idx = 0; idx < cnt; idx++) {
		list_remove_head(&blist, lpfc_cmd, struct lpfc_io_buf, list);
		if (!lpfc_cmd)
			return cnt;
		if (idx == 0) {
			list_add_tail(&lpfc_cmd->list, cbuf);
			continue;
		}
		xri = lpfc_cmd->cur_iocbq.sli4_xritag;
		inserted = 0;
		prev_iobufp = NULL;
		list_for_each_entry(iobufp, cbuf, list) {
			if (xri < iobufp->cur_iocbq.sli4_xritag) {
				if (prev_iobufp)
					list_add(&lpfc_cmd->list,
						 &prev_iobufp->list);
				else
					list_add(&lpfc_cmd->list, cbuf);
				inserted = 1;
				break;
			}
			prev_iobufp = iobufp;
		}
		if (!inserted)
			list_add_tail(&lpfc_cmd->list, cbuf);
	}
	return cnt;
}

int
lpfc_io_buf_replenish(struct lpfc_hba *phba, struct list_head *cbuf)
{
	struct lpfc_sli4_hdw_queue *qp;
	struct lpfc_io_buf *lpfc_cmd;
	int idx, cnt;

	qp = phba->sli4_hba.hdwq;
	cnt = 0;
	while (!list_empty(cbuf)) {
		for (idx = 0; idx < phba->cfg_hdw_queue; idx++) {
			list_remove_head(cbuf, lpfc_cmd,
					 struct lpfc_io_buf, list);
			if (!lpfc_cmd)
				return cnt;
			cnt++;
			qp = &phba->sli4_hba.hdwq[idx];
			lpfc_cmd->hdwq_no = idx;
			lpfc_cmd->hdwq = qp;
			lpfc_cmd->cur_iocbq.wqe_cmpl = NULL;
			lpfc_cmd->cur_iocbq.iocb_cmpl = NULL;
			spin_lock(&qp->io_buf_list_put_lock);
			list_add_tail(&lpfc_cmd->list,
				      &qp->lpfc_io_buf_list_put);
			qp->put_io_bufs++;
			qp->total_io_bufs++;
			spin_unlock(&qp->io_buf_list_put_lock);
		}
	}
	return cnt;
}

/**
 * lpfc_sli4_io_sgl_update - update xri-sgl sizing and mapping
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine first calculates the sizes of the current els and allocated
 * scsi sgl lists, and then goes through all sgls to updates the physical
 * XRIs assigned due to port function reset. During port initialization, the
 * current els and allocated scsi sgl lists are 0s.
 *
 * Return codes
 *   0 - successful (for now, it always returns 0)
 **/
int
lpfc_sli4_io_sgl_update(struct lpfc_hba *phba)
{
	struct lpfc_io_buf *lpfc_ncmd = NULL, *lpfc_ncmd_next = NULL;
	uint16_t i, lxri, els_xri_cnt;
	uint16_t io_xri_cnt, io_xri_max;
	LIST_HEAD(io_sgl_list);
	int rc, cnt;

	/*
	 * update on pci function's allocated nvme xri-sgl list
	 */

	/* maximum number of xris available for nvme buffers */
	els_xri_cnt = lpfc_sli4_get_els_iocb_cnt(phba);
	io_xri_max = phba->sli4_hba.max_cfg_param.max_xri - els_xri_cnt;
	phba->sli4_hba.io_xri_max = io_xri_max;

	lpfc_printf_log(phba, KERN_INFO, LOG_SLI,
			"6074 Current allocated XRI sgl count:%d, "
			"maximum XRI count:%d\n",
			phba->sli4_hba.io_xri_cnt,
			phba->sli4_hba.io_xri_max);

	cnt = lpfc_io_buf_flush(phba, &io_sgl_list);

	if (phba->sli4_hba.io_xri_cnt > phba->sli4_hba.io_xri_max) {
		/* max nvme xri shrunk below the allocated nvme buffers */
		io_xri_cnt = phba->sli4_hba.io_xri_cnt -
					phba->sli4_hba.io_xri_max;
		/* release the extra allocated nvme buffers */
		for (i = 0; i < io_xri_cnt; i++) {
			list_remove_head(&io_sgl_list, lpfc_ncmd,
					 struct lpfc_io_buf, list);
			if (lpfc_ncmd) {
				dma_pool_free(phba->lpfc_sg_dma_buf_pool,
					      lpfc_ncmd->data,
					      lpfc_ncmd->dma_handle);
				kfree(lpfc_ncmd);
			}
		}
		phba->sli4_hba.io_xri_cnt -= io_xri_cnt;
	}

	/* update xris associated to remaining allocated nvme buffers */
	lpfc_ncmd = NULL;
	lpfc_ncmd_next = NULL;
	phba->sli4_hba.io_xri_cnt = cnt;
	list_for_each_entry_safe(lpfc_ncmd, lpfc_ncmd_next,
				 &io_sgl_list, list) {
		lxri = lpfc_sli4_next_xritag(phba);
		if (lxri == NO_XRI) {
			lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
					"6075 Failed to allocate xri for "
					"nvme buffer\n");
			rc = -ENOMEM;
			goto out_free_mem;
		}
		lpfc_ncmd->cur_iocbq.sli4_lxritag = lxri;
		lpfc_ncmd->cur_iocbq.sli4_xritag = phba->sli4_hba.xri_ids[lxri];
	}
	cnt = lpfc_io_buf_replenish(phba, &io_sgl_list);
	return 0;

out_free_mem:
	lpfc_io_free(phba);
	return rc;
}

/**
 * lpfc_new_io_buf - IO buffer allocator for HBA with SLI4 IF spec
 * @vport: The virtual port for which this call being executed.
 * @num_to_allocate: The requested number of buffers to allocate.
 *
 * This routine allocates nvme buffers for device with SLI-4 interface spec,
 * the nvme buffer contains all the necessary information needed to initiate
 * an I/O. After allocating up to @num_to_allocate IO buffers and put
 * them on a list, it post them to the port by using SGL block post.
 *
 * Return codes:
 *   int - number of IO buffers that were allocated and posted.
 *   0 = failure, less than num_to_alloc is a partial failure.
 **/
int
lpfc_new_io_buf(struct lpfc_hba *phba, int num_to_alloc)
{
	struct lpfc_io_buf *lpfc_ncmd;
	struct lpfc_iocbq *pwqeq;
	uint16_t iotag, lxri = 0;
	int bcnt, num_posted;
	LIST_HEAD(prep_nblist);
	LIST_HEAD(post_nblist);
	LIST_HEAD(nvme_nblist);

	phba->sli4_hba.io_xri_cnt = 0;
	for (bcnt = 0; bcnt < num_to_alloc; bcnt++) {
		lpfc_ncmd = kzalloc(sizeof(*lpfc_ncmd), GFP_KERNEL);
		if (!lpfc_ncmd)
			break;
		/*
		 * Get memory from the pci pool to map the virt space to
		 * pci bus space for an I/O. The DMA buffer includes the
		 * number of SGE's necessary to support the sg_tablesize.
		 */
		lpfc_ncmd->data = dma_pool_zalloc(phba->lpfc_sg_dma_buf_pool,
						  GFP_KERNEL,
						  &lpfc_ncmd->dma_handle);
		if (!lpfc_ncmd->data) {
			kfree(lpfc_ncmd);
			break;
		}

		if (phba->cfg_xpsgl && !phba->nvmet_support) {
			INIT_LIST_HEAD(&lpfc_ncmd->dma_sgl_xtra_list);
		} else {
			/*
			 * 4K Page alignment is CRITICAL to BlockGuard, double
			 * check to be sure.
			 */
			if ((phba->sli3_options & LPFC_SLI3_BG_ENABLED) &&
			    (((unsigned long)(lpfc_ncmd->data) &
			    (unsigned long)(SLI4_PAGE_SIZE - 1)) != 0)) {
				lpfc_printf_log(phba, KERN_ERR, LOG_FCP,
						"3369 Memory alignment err: "
						"addr=%lx\n",
						(unsigned long)lpfc_ncmd->data);
				dma_pool_free(phba->lpfc_sg_dma_buf_pool,
					      lpfc_ncmd->data,
					      lpfc_ncmd->dma_handle);
				kfree(lpfc_ncmd);
				break;
			}
		}

		INIT_LIST_HEAD(&lpfc_ncmd->dma_cmd_rsp_list);

		lxri = lpfc_sli4_next_xritag(phba);
		if (lxri == NO_XRI) {
			dma_pool_free(phba->lpfc_sg_dma_buf_pool,
				      lpfc_ncmd->data, lpfc_ncmd->dma_handle);
			kfree(lpfc_ncmd);
			break;
		}
		pwqeq = &lpfc_ncmd->cur_iocbq;

		/* Allocate iotag for lpfc_ncmd->cur_iocbq. */
		iotag = lpfc_sli_next_iotag(phba, pwqeq);
		if (iotag == 0) {
			dma_pool_free(phba->lpfc_sg_dma_buf_pool,
				      lpfc_ncmd->data, lpfc_ncmd->dma_handle);
			kfree(lpfc_ncmd);
			lpfc_printf_log(phba, KERN_ERR, LOG_NVME_IOERR,
					"6121 Failed to allocate IOTAG for"
					" XRI:0x%x\n", lxri);
			lpfc_sli4_free_xri(phba, lxri);
			break;
		}
		pwqeq->sli4_lxritag = lxri;
		pwqeq->sli4_xritag = phba->sli4_hba.xri_ids[lxri];
		pwqeq->context1 = lpfc_ncmd;

		/* Initialize local short-hand pointers. */
		lpfc_ncmd->dma_sgl = lpfc_ncmd->data;
		lpfc_ncmd->dma_phys_sgl = lpfc_ncmd->dma_handle;
		lpfc_ncmd->cur_iocbq.context1 = lpfc_ncmd;
		spin_lock_init(&lpfc_ncmd->buf_lock);

		/* add the nvme buffer to a post list */
		list_add_tail(&lpfc_ncmd->list, &post_nblist);
		phba->sli4_hba.io_xri_cnt++;
	}
	lpfc_printf_log(phba, KERN_INFO, LOG_NVME,
			"6114 Allocate %d out of %d requested new NVME "
			"buffers\n", bcnt, num_to_alloc);

	/* post the list of nvme buffer sgls to port if available */
	if (!list_empty(&post_nblist))
		num_posted = lpfc_sli4_post_io_sgl_list(
				phba, &post_nblist, bcnt);
	else
		num_posted = 0;

	return num_posted;
}

static uint64_t
lpfc_get_wwpn(struct lpfc_hba *phba)
{
	uint64_t wwn;
	int rc;
	LPFC_MBOXQ_t *mboxq;
	MAILBOX_t *mb;

	mboxq = (LPFC_MBOXQ_t *) mempool_alloc(phba->mbox_mem_pool,
						GFP_KERNEL);
	if (!mboxq)
		return (uint64_t)-1;

	/* First get WWN of HBA instance */
	lpfc_read_nv(phba, mboxq);
	rc = lpfc_sli_issue_mbox(phba, mboxq, MBX_POLL);
	if (rc != MBX_SUCCESS) {
		lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
				"6019 Mailbox failed , mbxCmd x%x "
				"READ_NV, mbxStatus x%x\n",
				bf_get(lpfc_mqe_command, &mboxq->u.mqe),
				bf_get(lpfc_mqe_status, &mboxq->u.mqe));
		mempool_free(mboxq, phba->mbox_mem_pool);
		return (uint64_t) -1;
	}
	mb = &mboxq->u.mb;
	memcpy(&wwn, (char *)mb->un.varRDnvp.portname, sizeof(uint64_t));
	/* wwn is WWPN of HBA instance */
	mempool_free(mboxq, phba->mbox_mem_pool);
	if (phba->sli_rev == LPFC_SLI_REV4)
		return be64_to_cpu(wwn);
	else
		return rol64(wwn, 32);
}

/**
 * lpfc_create_port - Create an FC port
 * @phba: pointer to lpfc hba data structure.
 * @instance: a unique integer ID to this FC port.
 * @dev: pointer to the device data structure.
 *
 * This routine creates a FC port for the upper layer protocol. The FC port
 * can be created on top of either a physical port or a virtual port provided
 * by the HBA. This routine also allocates a SCSI host data structure (shost)
 * and associates the FC port created before adding the shost into the SCSI
 * layer.
 *
 * Return codes
 *   @vport - pointer to the virtual N_Port data structure.
 *   NULL - port create failed.
 **/
struct lpfc_vport *
lpfc_create_port(struct lpfc_hba *phba, int instance, struct device *dev)
{
	struct lpfc_vport *vport;
	struct Scsi_Host  *shost = NULL;
	int error = 0;
	int i;
	uint64_t wwn;
	bool use_no_reset_hba = false;
	int rc;

	if (lpfc_no_hba_reset_cnt) {
		if (phba->sli_rev < LPFC_SLI_REV4 &&
		    dev == &phba->pcidev->dev) {
			/* Reset the port first */
			lpfc_sli_brdrestart(phba);
			rc = lpfc_sli_chipset_init(phba);
			if (rc)
				return NULL;
		}
		wwn = lpfc_get_wwpn(phba);
	}

	for (i = 0; i < lpfc_no_hba_reset_cnt; i++) {
		if (wwn == lpfc_no_hba_reset[i]) {
			lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
					"6020 Setting use_no_reset port=%llx\n",
					wwn);
			use_no_reset_hba = true;
			break;
		}
	}

	if (phba->cfg_enable_fc4_type & LPFC_ENABLE_FCP) {
		if (dev != &phba->pcidev->dev) {
			shost = scsi_host_alloc(&lpfc_vport_template,
						sizeof(struct lpfc_vport));
		} else {
			if (!use_no_reset_hba)
				shost = scsi_host_alloc(&lpfc_template,
						sizeof(struct lpfc_vport));
			else
				shost = scsi_host_alloc(&lpfc_template_no_hr,
						sizeof(struct lpfc_vport));
		}
	} else if (phba->cfg_enable_fc4_type & LPFC_ENABLE_NVME) {
		shost = scsi_host_alloc(&lpfc_template_nvme,
					sizeof(struct lpfc_vport));
	}
	if (!shost)
		goto out;

	vport = (struct lpfc_vport *) shost->hostdata;
	vport->phba = phba;
	vport->load_flag |= FC_LOADING;
	vport->fc_flag |= FC_VPORT_NEEDS_REG_VPI;
	vport->fc_rscn_flush = 0;
	lpfc_get_vport_cfgparam(vport);

	/* Adjust value in vport */
	vport->cfg_enable_fc4_type = phba->cfg_enable_fc4_type;

	shost->unique_id = instance;
	shost->max_id = LPFC_MAX_TARGET;
	shost->max_lun = vport->cfg_max_luns;
	shost->this_id = -1;
	shost->max_cmd_len = 16;

	if (phba->sli_rev == LPFC_SLI_REV4) {
		if (!phba->cfg_fcp_mq_threshold ||
		    phba->cfg_fcp_mq_threshold > phba->cfg_hdw_queue)
			phba->cfg_fcp_mq_threshold = phba->cfg_hdw_queue;

		shost->nr_hw_queues = min_t(int, 2 * num_possible_nodes(),
					    phba->cfg_fcp_mq_threshold);

		shost->dma_boundary =
			phba->sli4_hba.pc_sli4_params.sge_supp_len-1;

		if (phba->cfg_xpsgl && !phba->nvmet_support)
			shost->sg_tablesize = LPFC_MAX_SG_TABLESIZE;
		else
			shost->sg_tablesize = phba->cfg_scsi_seg_cnt;
	} else
		/* SLI-3 has a limited number of hardware queues (3),
		 * thus there is only one for FCP processing.
		 */
		shost->nr_hw_queues = 1;

	/*
	 * Set initial can_queue value since 0 is no longer supported and
	 * scsi_add_host will fail. This will be adjusted later based on the
	 * max xri value determined in hba setup.
	 */
	shost->can_queue = phba->cfg_hba_queue_depth - 10;
	if (dev != &phba->pcidev->dev) {
		shost->transportt = lpfc_vport_transport_template;
		vport->port_type = LPFC_NPIV_PORT;
	} else {
		shost->transportt = lpfc_transport_template;
		vport->port_type = LPFC_PHYSICAL_PORT;
	}

	/* Initialize all internally managed lists. */
	INIT_LIST_HEAD(&vport->fc_nodes);
	INIT_LIST_HEAD(&vport->rcv_buffer_list);
	spin_lock_init(&vport->work_port_lock);

	timer_setup(&vport->fc_disctmo, lpfc_disc_timeout, 0);

	timer_setup(&vport->els_tmofunc, lpfc_els_timeout, 0);

	timer_setup(&vport->delayed_disc_tmo, lpfc_delayed_disc_tmo, 0);

	if (phba->sli3_options & LPFC_SLI3_BG_ENABLED)
		lpfc_setup_bg(phba, shost);

	error = scsi_add_host_with_dma(shost, dev, &phba->pcidev->dev);
	if (error)
		goto out_put_shost;

	spin_lock_irq(&phba->port_list_lock);
	list_add_tail(&vport->listentry, &phba->port_list);
	spin_unlock_irq(&phba->port_list_lock);
	return vport;

out_put_shost:
	scsi_host_put(shost);
out:
	return NULL;
}

/**
 * destroy_port -  destroy an FC port
 * @vport: pointer to an lpfc virtual N_Port data structure.
 *
 * This routine destroys a FC port from the upper layer protocol. All the
 * resources associated with the port are released.
 **/
void
destroy_port(struct lpfc_vport *vport)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct lpfc_hba  *phba = vport->phba;

	lpfc_debugfs_terminate(vport);
	fc_remove_host(shost);
	scsi_remove_host(shost);

	spin_lock_irq(&phba->port_list_lock);
	list_del_init(&vport->listentry);
	spin_unlock_irq(&phba->port_list_lock);

	lpfc_cleanup(vport);
	return;
}

/**
 * lpfc_get_instance - Get a unique integer ID
 *
 * This routine allocates a unique integer ID from lpfc_hba_index pool. It
 * uses the kernel idr facility to perform the task.
 *
 * Return codes:
 *   instance - a unique integer ID allocated as the new instance.
 *   -1 - lpfc get instance failed.
 **/
int
lpfc_get_instance(void)
{
	int ret;

	ret = idr_alloc(&lpfc_hba_index, NULL, 0, 0, GFP_KERNEL);
	return ret < 0 ? -1 : ret;
}

/**
 * lpfc_scan_finished - method for SCSI layer to detect whether scan is done
 * @shost: pointer to SCSI host data structure.
 * @time: elapsed time of the scan in jiffies.
 *
 * This routine is called by the SCSI layer with a SCSI host to determine
 * whether the scan host is finished.
 *
 * Note: there is no scan_start function as adapter initialization will have
 * asynchronously kicked off the link initialization.
 *
 * Return codes
 *   0 - SCSI host scan is not over yet.
 *   1 - SCSI host scan is over.
 **/
int lpfc_scan_finished(struct Scsi_Host *shost, unsigned long time)
{
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	int stat = 0;

	spin_lock_irq(shost->host_lock);

	if (vport->load_flag & FC_UNLOADING) {
		stat = 1;
		goto finished;
	}
	if (time >= msecs_to_jiffies(30 * 1000)) {
		lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
				"0461 Scanning longer than 30 "
				"seconds.  Continuing initialization\n");
		stat = 1;
		goto finished;
	}
	if (time >= msecs_to_jiffies(15 * 1000) &&
	    phba->link_state <= LPFC_LINK_DOWN) {
		lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
				"0465 Link down longer than 15 "
				"seconds.  Continuing initialization\n");
		stat = 1;
		goto finished;
	}

	if (vport->port_state != LPFC_VPORT_READY)
		goto finished;
	if (vport->num_disc_nodes || vport->fc_prli_sent)
		goto finished;
	if (vport->fc_map_cnt == 0 && time < msecs_to_jiffies(2 * 1000))
		goto finished;
	if ((phba->sli.sli_flag & LPFC_SLI_MBOX_ACTIVE) != 0)
		goto finished;

	stat = 1;

finished:
	spin_unlock_irq(shost->host_lock);
	return stat;
}

static void lpfc_host_supported_speeds_set(struct Scsi_Host *shost)
{
	struct lpfc_vport *vport = (struct lpfc_vport *)shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	fc_host_supported_speeds(shost) = 0;
	if (phba->lmt & LMT_128Gb)
		fc_host_supported_speeds(shost) |= FC_PORTSPEED_128GBIT;
	if (phba->lmt & LMT_64Gb)
		fc_host_supported_speeds(shost) |= FC_PORTSPEED_64GBIT;
	if (phba->lmt & LMT_32Gb)
		fc_host_supported_speeds(shost) |= FC_PORTSPEED_32GBIT;
	if (phba->lmt & LMT_16Gb)
		fc_host_supported_speeds(shost) |= FC_PORTSPEED_16GBIT;
	if (phba->lmt & LMT_10Gb)
		fc_host_supported_speeds(shost) |= FC_PORTSPEED_10GBIT;
	if (phba->lmt & LMT_8Gb)
		fc_host_supported_speeds(shost) |= FC_PORTSPEED_8GBIT;
	if (phba->lmt & LMT_4Gb)
		fc_host_supported_speeds(shost) |= FC_PORTSPEED_4GBIT;
	if (phba->lmt & LMT_2Gb)
		fc_host_supported_speeds(shost) |= FC_PORTSPEED_2GBIT;
	if (phba->lmt & LMT_1Gb)
		fc_host_supported_speeds(shost) |= FC_PORTSPEED_1GBIT;
}

/**
 * lpfc_host_attrib_init - Initialize SCSI host attributes on a FC port
 * @shost: pointer to SCSI host data structure.
 *
 * This routine initializes a given SCSI host attributes on a FC port. The
 * SCSI host can be either on top of a physical port or a virtual port.
 **/
void lpfc_host_attrib_init(struct Scsi_Host *shost)
{
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	/*
	 * Set fixed host attributes.  Must done after lpfc_sli_hba_setup().
	 */

	fc_host_node_name(shost) = wwn_to_u64(vport->fc_nodename.u.wwn);
	fc_host_port_name(shost) = wwn_to_u64(vport->fc_portname.u.wwn);
	fc_host_supported_classes(shost) = FC_COS_CLASS3;

	memset(fc_host_supported_fc4s(shost), 0,
	       sizeof(fc_host_supported_fc4s(shost)));
	fc_host_supported_fc4s(shost)[2] = 1;
	fc_host_supported_fc4s(shost)[7] = 1;

	lpfc_vport_symbolic_node_name(vport, fc_host_symbolic_name(shost),
				 sizeof fc_host_symbolic_name(shost));

	lpfc_host_supported_speeds_set(shost);

	fc_host_maxframe_size(shost) =
		(((uint32_t) vport->fc_sparam.cmn.bbRcvSizeMsb & 0x0F) << 8) |
		(uint32_t) vport->fc_sparam.cmn.bbRcvSizeLsb;

	fc_host_dev_loss_tmo(shost) = vport->cfg_devloss_tmo;

	/* This value is also unchanging */
	memset(fc_host_active_fc4s(shost), 0,
	       sizeof(fc_host_active_fc4s(shost)));
	fc_host_active_fc4s(shost)[2] = 1;
	fc_host_active_fc4s(shost)[7] = 1;

	fc_host_max_npiv_vports(shost) = phba->max_vpi;
	spin_lock_irq(shost->host_lock);
	vport->load_flag &= ~FC_LOADING;
	spin_unlock_irq(shost->host_lock);
}

/**
 * lpfc_stop_port_s3 - Stop SLI3 device port
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to stop an SLI3 device port, it stops the device
 * from generating interrupts and stops the device driver's timers for the
 * device.
 **/
static void
lpfc_stop_port_s3(struct lpfc_hba *phba)
{
	/* Clear all interrupt enable conditions */
	writel(0, phba->HCregaddr);
	readl(phba->HCregaddr); /* flush */
	/* Clear all pending interrupts */
	writel(0xffffffff, phba->HAregaddr);
	readl(phba->HAregaddr); /* flush */

	/* Reset some HBA SLI setup states */
	lpfc_stop_hba_timers(phba);
	phba->pport->work_port_events = 0;
}

/**
 * lpfc_stop_port_s4 - Stop SLI4 device port
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to stop an SLI4 device port, it stops the device
 * from generating interrupts and stops the device driver's timers for the
 * device.
 **/
static void
lpfc_stop_port_s4(struct lpfc_hba *phba)
{
	/* Reset some HBA SLI4 setup states */
	lpfc_stop_hba_timers(phba);
	if (phba->pport)
		phba->pport->work_port_events = 0;
	phba->sli4_hba.intr_enable = 0;
}

/**
 * lpfc_stop_port - Wrapper function for stopping hba port
 * @phba: Pointer to HBA context object.
 *
 * This routine wraps the actual SLI3 or SLI4 hba stop port routine from
 * the API jump table function pointer from the lpfc_hba struct.
 **/
void
lpfc_stop_port(struct lpfc_hba *phba)
{
	phba->lpfc_stop_port(phba);

	if (phba->wq)
		flush_workqueue(phba->wq);
}

/**
 * lpfc_fcf_redisc_wait_start_timer - Start fcf rediscover wait timer
 * @phba: Pointer to hba for which this call is being executed.
 *
 * This routine starts the timer waiting for the FCF rediscovery to complete.
 **/
void
lpfc_fcf_redisc_wait_start_timer(struct lpfc_hba *phba)
{
	unsigned long fcf_redisc_wait_tmo =
		(jiffies + msecs_to_jiffies(LPFC_FCF_REDISCOVER_WAIT_TMO));
	/* Start fcf rediscovery wait period timer */
	mod_timer(&phba->fcf.redisc_wait, fcf_redisc_wait_tmo);
	spin_lock_irq(&phba->hbalock);
	/* Allow action to new fcf asynchronous event */
	phba->fcf.fcf_flag &= ~(FCF_AVAILABLE | FCF_SCAN_DONE);
	/* Mark the FCF rediscovery pending state */
	phba->fcf.fcf_flag |= FCF_REDISC_PEND;
	spin_unlock_irq(&phba->hbalock);
}

/**
 * lpfc_sli4_fcf_redisc_wait_tmo - FCF table rediscover wait timeout
 * @ptr: Map to lpfc_hba data structure pointer.
 *
 * This routine is invoked when waiting for FCF table rediscover has been
 * timed out. If new FCF record(s) has (have) been discovered during the
 * wait period, a new FCF event shall be added to the FCOE async event
 * list, and then worker thread shall be waked up for processing from the
 * worker thread context.
 **/
static void
lpfc_sli4_fcf_redisc_wait_tmo(struct timer_list *t)
{
	struct lpfc_hba *phba = from_timer(phba, t, fcf.redisc_wait);

	/* Don't send FCF rediscovery event if timer cancelled */
	spin_lock_irq(&phba->hbalock);
	if (!(phba->fcf.fcf_flag & FCF_REDISC_PEND)) {
		spin_unlock_irq(&phba->hbalock);
		return;
	}
	/* Clear FCF rediscovery timer pending flag */
	phba->fcf.fcf_flag &= ~FCF_REDISC_PEND;
	/* FCF rediscovery event to worker thread */
	phba->fcf.fcf_flag |= FCF_REDISC_EVT;
	spin_unlock_irq(&phba->hbalock);
	lpfc_printf_log(phba, KERN_INFO, LOG_FIP,
			"2776 FCF rediscover quiescent timer expired\n");
	/* wake up worker thread */
	lpfc_worker_wake_up(phba);
}

/**
 * lpfc_sli4_parse_latt_fault - Parse sli4 link-attention link fault code
 * @phba: pointer to lpfc hba data structure.
 * @acqe_link: pointer to the async link completion queue entry.
 *
 * This routine is to parse the SLI4 link-attention link fault code.
 **/
static void
lpfc_sli4_parse_latt_fault(struct lpfc_hba *phba,
			   struct lpfc_acqe_link *acqe_link)
{
	switch (bf_get(lpfc_acqe_link_fault, acqe_link)) {
	case LPFC_ASYNC_LINK_FAULT_NONE:
	case LPFC_ASYNC_LINK_FAULT_LOCAL:
	case LPFC_ASYNC_LINK_FAULT_REMOTE:
	case LPFC_ASYNC_LINK_FAULT_LR_LRR:
		break;
	default:
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0398 Unknown link fault code: x%x\n",
				bf_get(lpfc_acqe_link_fault, acqe_link));
		break;
	}
}

/**
 * lpfc_sli4_parse_latt_type - Parse sli4 link attention type
 * @phba: pointer to lpfc hba data structure.
 * @acqe_link: pointer to the async link completion queue entry.
 *
 * This routine is to parse the SLI4 link attention type and translate it
 * into the base driver's link attention type coding.
 *
 * Return: Link attention type in terms of base driver's coding.
 **/
static uint8_t
lpfc_sli4_parse_latt_type(struct lpfc_hba *phba,
			  struct lpfc_acqe_link *acqe_link)
{
	uint8_t att_type;

	switch (bf_get(lpfc_acqe_link_status, acqe_link)) {
	case LPFC_ASYNC_LINK_STATUS_DOWN:
	case LPFC_ASYNC_LINK_STATUS_LOGICAL_DOWN:
		att_type = LPFC_ATT_LINK_DOWN;
		break;
	case LPFC_ASYNC_LINK_STATUS_UP:
		/* Ignore physical link up events - wait for logical link up */
		att_type = LPFC_ATT_RESERVED;
		break;
	case LPFC_ASYNC_LINK_STATUS_LOGICAL_UP:
		att_type = LPFC_ATT_LINK_UP;
		break;
	default:
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0399 Invalid link attention type: x%x\n",
				bf_get(lpfc_acqe_link_status, acqe_link));
		att_type = LPFC_ATT_RESERVED;
		break;
	}
	return att_type;
}

/**
 * lpfc_sli_port_speed_get - Get sli3 link speed code to link speed
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is to get an SLI3 FC port's link speed in Mbps.
 *
 * Return: link speed in terms of Mbps.
 **/
uint32_t
lpfc_sli_port_speed_get(struct lpfc_hba *phba)
{
	uint32_t link_speed;

	if (!lpfc_is_link_up(phba))
		return 0;

	if (phba->sli_rev <= LPFC_SLI_REV3) {
		switch (phba->fc_linkspeed) {
		case LPFC_LINK_SPEED_1GHZ:
			link_speed = 1000;
			break;
		case LPFC_LINK_SPEED_2GHZ:
			link_speed = 2000;
			break;
		case LPFC_LINK_SPEED_4GHZ:
			link_speed = 4000;
			break;
		case LPFC_LINK_SPEED_8GHZ:
			link_speed = 8000;
			break;
		case LPFC_LINK_SPEED_10GHZ:
			link_speed = 10000;
			break;
		case LPFC_LINK_SPEED_16GHZ:
			link_speed = 16000;
			break;
		default:
			link_speed = 0;
		}
	} else {
		if (phba->sli4_hba.link_state.logical_speed)
			link_speed =
			      phba->sli4_hba.link_state.logical_speed;
		else
			link_speed = phba->sli4_hba.link_state.speed;
	}
	return link_speed;
}

/**
 * lpfc_sli4_port_speed_parse - Parse async evt link speed code to link speed
 * @phba: pointer to lpfc hba data structure.
 * @evt_code: asynchronous event code.
 * @speed_code: asynchronous event link speed code.
 *
 * This routine is to parse the giving SLI4 async event link speed code into
 * value of Mbps for the link speed.
 *
 * Return: link speed in terms of Mbps.
 **/
static uint32_t
lpfc_sli4_port_speed_parse(struct lpfc_hba *phba, uint32_t evt_code,
			   uint8_t speed_code)
{
	uint32_t port_speed;

	switch (evt_code) {
	case LPFC_TRAILER_CODE_LINK:
		switch (speed_code) {
		case LPFC_ASYNC_LINK_SPEED_ZERO:
			port_speed = 0;
			break;
		case LPFC_ASYNC_LINK_SPEED_10MBPS:
			port_speed = 10;
			break;
		case LPFC_ASYNC_LINK_SPEED_100MBPS:
			port_speed = 100;
			break;
		case LPFC_ASYNC_LINK_SPEED_1GBPS:
			port_speed = 1000;
			break;
		case LPFC_ASYNC_LINK_SPEED_10GBPS:
			port_speed = 10000;
			break;
		case LPFC_ASYNC_LINK_SPEED_20GBPS:
			port_speed = 20000;
			break;
		case LPFC_ASYNC_LINK_SPEED_25GBPS:
			port_speed = 25000;
			break;
		case LPFC_ASYNC_LINK_SPEED_40GBPS:
			port_speed = 40000;
			break;
		default:
			port_speed = 0;
		}
		break;
	case LPFC_TRAILER_CODE_FC:
		switch (speed_code) {
		case LPFC_FC_LA_SPEED_UNKNOWN:
			port_speed = 0;
			break;
		case LPFC_FC_LA_SPEED_1G:
			port_speed = 1000;
			break;
		case LPFC_FC_LA_SPEED_2G:
			port_speed = 2000;
			break;
		case LPFC_FC_LA_SPEED_4G:
			port_speed = 4000;
			break;
		case LPFC_FC_LA_SPEED_8G:
			port_speed = 8000;
			break;
		case LPFC_FC_LA_SPEED_10G:
			port_speed = 10000;
			break;
		case LPFC_FC_LA_SPEED_16G:
			port_speed = 16000;
			break;
		case LPFC_FC_LA_SPEED_32G:
			port_speed = 32000;
			break;
		case LPFC_FC_LA_SPEED_64G:
			port_speed = 64000;
			break;
		case LPFC_FC_LA_SPEED_128G:
			port_speed = 128000;
			break;
		default:
			port_speed = 0;
		}
		break;
	default:
		port_speed = 0;
	}
	return port_speed;
}

/**
 * lpfc_sli4_async_link_evt - Process the asynchronous FCoE link event
 * @phba: pointer to lpfc hba data structure.
 * @acqe_link: pointer to the async link completion queue entry.
 *
 * This routine is to handle the SLI4 asynchronous FCoE link event.
 **/
static void
lpfc_sli4_async_link_evt(struct lpfc_hba *phba,
			 struct lpfc_acqe_link *acqe_link)
{
	struct lpfc_dmabuf *mp;
	LPFC_MBOXQ_t *pmb;
	MAILBOX_t *mb;
	struct lpfc_mbx_read_top *la;
	uint8_t att_type;
	int rc;

	att_type = lpfc_sli4_parse_latt_type(phba, acqe_link);
	if (att_type != LPFC_ATT_LINK_DOWN && att_type != LPFC_ATT_LINK_UP)
		return;
	phba->fcoe_eventtag = acqe_link->event_tag;
	pmb = (LPFC_MBOXQ_t *)mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!pmb) {
		lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
				"0395 The mboxq allocation failed\n");
		return;
	}
	mp = kmalloc(sizeof(struct lpfc_dmabuf), GFP_KERNEL);
	if (!mp) {
		lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
				"0396 The lpfc_dmabuf allocation failed\n");
		goto out_free_pmb;
	}
	mp->virt = lpfc_mbuf_alloc(phba, 0, &mp->phys);
	if (!mp->virt) {
		lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
				"0397 The mbuf allocation failed\n");
		goto out_free_dmabuf;
	}

	/* Cleanup any outstanding ELS commands */
	lpfc_els_flush_all_cmd(phba);

	/* Block ELS IOCBs until we have done process link event */
	phba->sli4_hba.els_wq->pring->flag |= LPFC_STOP_IOCB_EVENT;

	/* Update link event statistics */
	phba->sli.slistat.link_event++;

	/* Create lpfc_handle_latt mailbox command from link ACQE */
	lpfc_read_topology(phba, pmb, mp);
	pmb->mbox_cmpl = lpfc_mbx_cmpl_read_topology;
	pmb->vport = phba->pport;

	/* Keep the link status for extra SLI4 state machine reference */
	phba->sli4_hba.link_state.speed =
			lpfc_sli4_port_speed_parse(phba, LPFC_TRAILER_CODE_LINK,
				bf_get(lpfc_acqe_link_speed, acqe_link));
	phba->sli4_hba.link_state.duplex =
				bf_get(lpfc_acqe_link_duplex, acqe_link);
	phba->sli4_hba.link_state.status =
				bf_get(lpfc_acqe_link_status, acqe_link);
	phba->sli4_hba.link_state.type =
				bf_get(lpfc_acqe_link_type, acqe_link);
	phba->sli4_hba.link_state.number =
				bf_get(lpfc_acqe_link_number, acqe_link);
	phba->sli4_hba.link_state.fault =
				bf_get(lpfc_acqe_link_fault, acqe_link);
	phba->sli4_hba.link_state.logical_speed =
			bf_get(lpfc_acqe_logical_link_speed, acqe_link) * 10;

	lpfc_printf_log(phba, KERN_INFO, LOG_SLI,
			"2900 Async FC/FCoE Link event - Speed:%dGBit "
			"duplex:x%x LA Type:x%x Port Type:%d Port Number:%d "
			"Logical speed:%dMbps Fault:%d\n",
			phba->sli4_hba.link_state.speed,
			phba->sli4_hba.link_state.topology,
			phba->sli4_hba.link_state.status,
			phba->sli4_hba.link_state.type,
			phba->sli4_hba.link_state.number,
			phba->sli4_hba.link_state.logical_speed,
			phba->sli4_hba.link_state.fault);
	/*
	 * For FC Mode: issue the READ_TOPOLOGY mailbox command to fetch
	 * topology info. Note: Optional for non FC-AL ports.
	 */
	if (!(phba->hba_flag & HBA_FCOE_MODE)) {
		rc = lpfc_sli_issue_mbox(phba, pmb, MBX_NOWAIT);
		if (rc == MBX_NOT_FINISHED)
			goto out_free_dmabuf;
		return;
	}
	/*
	 * For FCoE Mode: fill in all the topology information we need and call
	 * the READ_TOPOLOGY completion routine to continue without actually
	 * sending the READ_TOPOLOGY mailbox command to the port.
	 */
	/* Initialize completion status */
	mb = &pmb->u.mb;
	mb->mbxStatus = MBX_SUCCESS;

	/* Parse port fault information field */
	lpfc_sli4_parse_latt_fault(phba, acqe_link);

	/* Parse and translate link attention fields */
	la = (struct lpfc_mbx_read_top *) &pmb->u.mb.un.varReadTop;
	la->eventTag = acqe_link->event_tag;
	bf_set(lpfc_mbx_read_top_att_type, la, att_type);
	bf_set(lpfc_mbx_read_top_link_spd, la,
	       (bf_get(lpfc_acqe_link_speed, acqe_link)));

	/* Fake the the following irrelvant fields */
	bf_set(lpfc_mbx_read_top_topology, la, LPFC_TOPOLOGY_PT_PT);
	bf_set(lpfc_mbx_read_top_alpa_granted, la, 0);
	bf_set(lpfc_mbx_read_top_il, la, 0);
	bf_set(lpfc_mbx_read_top_pb, la, 0);
	bf_set(lpfc_mbx_read_top_fa, la, 0);
	bf_set(lpfc_mbx_read_top_mm, la, 0);

	/* Invoke the lpfc_handle_latt mailbox command callback function */
	lpfc_mbx_cmpl_read_topology(phba, pmb);

	return;

out_free_dmabuf:
	kfree(mp);
out_free_pmb:
	mempool_free(pmb, phba->mbox_mem_pool);
}

/**
 * lpfc_async_link_speed_to_read_top - Parse async evt link speed code to read
 * topology.
 * @phba: pointer to lpfc hba data structure.
 * @evt_code: asynchronous event code.
 * @speed_code: asynchronous event link speed code.
 *
 * This routine is to parse the giving SLI4 async event link speed code into
 * value of Read topology link speed.
 *
 * Return: link speed in terms of Read topology.
 **/
static uint8_t
lpfc_async_link_speed_to_read_top(struct lpfc_hba *phba, uint8_t speed_code)
{
	uint8_t port_speed;

	switch (speed_code) {
	case LPFC_FC_LA_SPEED_1G:
		port_speed = LPFC_LINK_SPEED_1GHZ;
		break;
	case LPFC_FC_LA_SPEED_2G:
		port_speed = LPFC_LINK_SPEED_2GHZ;
		break;
	case LPFC_FC_LA_SPEED_4G:
		port_speed = LPFC_LINK_SPEED_4GHZ;
		break;
	case LPFC_FC_LA_SPEED_8G:
		port_speed = LPFC_LINK_SPEED_8GHZ;
		break;
	case LPFC_FC_LA_SPEED_16G:
		port_speed = LPFC_LINK_SPEED_16GHZ;
		break;
	case LPFC_FC_LA_SPEED_32G:
		port_speed = LPFC_LINK_SPEED_32GHZ;
		break;
	case LPFC_FC_LA_SPEED_64G:
		port_speed = LPFC_LINK_SPEED_64GHZ;
		break;
	case LPFC_FC_LA_SPEED_128G:
		port_speed = LPFC_LINK_SPEED_128GHZ;
		break;
	case LPFC_FC_LA_SPEED_256G:
		port_speed = LPFC_LINK_SPEED_256GHZ;
		break;
	default:
		port_speed = 0;
		break;
	}

	return port_speed;
}

#define trunk_link_status(__idx)\
	bf_get(lpfc_acqe_fc_la_trunk_config_port##__idx, acqe_fc) ?\
	       ((phba->trunk_link.link##__idx.state == LPFC_LINK_UP) ?\
		"Link up" : "Link down") : "NA"
/* Did port __idx reported an error */
#define trunk_port_fault(__idx)\
	bf_get(lpfc_acqe_fc_la_trunk_config_port##__idx, acqe_fc) ?\
	       (port_fault & (1 << __idx) ? "YES" : "NO") : "NA"

static void
lpfc_update_trunk_link_status(struct lpfc_hba *phba,
			      struct lpfc_acqe_fc_la *acqe_fc)
{
	uint8_t port_fault = bf_get(lpfc_acqe_fc_la_trunk_linkmask, acqe_fc);
	uint8_t err = bf_get(lpfc_acqe_fc_la_trunk_fault, acqe_fc);

	phba->sli4_hba.link_state.speed =
		lpfc_sli4_port_speed_parse(phba, LPFC_TRAILER_CODE_FC,
				bf_get(lpfc_acqe_fc_la_speed, acqe_fc));

	phba->sli4_hba.link_state.logical_speed =
				bf_get(lpfc_acqe_fc_la_llink_spd, acqe_fc) * 10;
	/* We got FC link speed, convert to fc_linkspeed (READ_TOPOLOGY) */
	phba->fc_linkspeed =
		 lpfc_async_link_speed_to_read_top(
				phba,
				bf_get(lpfc_acqe_fc_la_speed, acqe_fc));

	if (bf_get(lpfc_acqe_fc_la_trunk_config_port0, acqe_fc)) {
		phba->trunk_link.link0.state =
			bf_get(lpfc_acqe_fc_la_trunk_link_status_port0, acqe_fc)
			? LPFC_LINK_UP : LPFC_LINK_DOWN;
		phba->trunk_link.link0.fault = port_fault & 0x1 ? err : 0;
	}
	if (bf_get(lpfc_acqe_fc_la_trunk_config_port1, acqe_fc)) {
		phba->trunk_link.link1.state =
			bf_get(lpfc_acqe_fc_la_trunk_link_status_port1, acqe_fc)
			? LPFC_LINK_UP : LPFC_LINK_DOWN;
		phba->trunk_link.link1.fault = port_fault & 0x2 ? err : 0;
	}
	if (bf_get(lpfc_acqe_fc_la_trunk_config_port2, acqe_fc)) {
		phba->trunk_link.link2.state =
			bf_get(lpfc_acqe_fc_la_trunk_link_status_port2, acqe_fc)
			? LPFC_LINK_UP : LPFC_LINK_DOWN;
		phba->trunk_link.link2.fault = port_fault & 0x4 ? err : 0;
	}
	if (bf_get(lpfc_acqe_fc_la_trunk_config_port3, acqe_fc)) {
		phba->trunk_link.link3.state =
			bf_get(lpfc_acqe_fc_la_trunk_link_status_port3, acqe_fc)
			? LPFC_LINK_UP : LPFC_LINK_DOWN;
		phba->trunk_link.link3.fault = port_fault & 0x8 ? err : 0;
	}

	lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
			"2910 Async FC Trunking Event - Speed:%d\n"
			"\tLogical speed:%d "
			"port0: %s port1: %s port2: %s port3: %s\n",
			phba->sli4_hba.link_state.speed,
			phba->sli4_hba.link_state.logical_speed,
			trunk_link_status(0), trunk_link_status(1),
			trunk_link_status(2), trunk_link_status(3));

	if (port_fault)
		lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
				"3202 trunk error:0x%x (%s) seen on port0:%s "
				/*
				 * SLI-4: We have only 0xA error codes
				 * defined as of now. print an appropriate
				 * message in case driver needs to be updated.
				 */
				"port1:%s port2:%s port3:%s\n", err, err > 0xA ?
				"UNDEFINED. update driver." : trunk_errmsg[err],
				trunk_port_fault(0), trunk_port_fault(1),
				trunk_port_fault(2), trunk_port_fault(3));
}


/**
 * lpfc_sli4_async_fc_evt - Process the asynchronous FC link event
 * @phba: pointer to lpfc hba data structure.
 * @acqe_fc: pointer to the async fc completion queue entry.
 *
 * This routine is to handle the SLI4 asynchronous FC event. It will simply log
 * that the event was received and then issue a read_topology mailbox command so
 * that the rest of the driver will treat it the same as SLI3.
 **/
static void
lpfc_sli4_async_fc_evt(struct lpfc_hba *phba, struct lpfc_acqe_fc_la *acqe_fc)
{
	struct lpfc_dmabuf *mp;
	LPFC_MBOXQ_t *pmb;
	MAILBOX_t *mb;
	struct lpfc_mbx_read_top *la;
	int rc;

	if (bf_get(lpfc_trailer_type, acqe_fc) !=
	    LPFC_FC_LA_EVENT_TYPE_FC_LINK) {
		lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
				"2895 Non FC link Event detected.(%d)\n",
				bf_get(lpfc_trailer_type, acqe_fc));
		return;
	}

	if (bf_get(lpfc_acqe_fc_la_att_type, acqe_fc) ==
	    LPFC_FC_LA_TYPE_TRUNKING_EVENT) {
		lpfc_update_trunk_link_status(phba, acqe_fc);
		return;
	}

	/* Keep the link status for extra SLI4 state machine reference */
	phba->sli4_hba.link_state.speed =
			lpfc_sli4_port_speed_parse(phba, LPFC_TRAILER_CODE_FC,
				bf_get(lpfc_acqe_fc_la_speed, acqe_fc));
	phba->sli4_hba.link_state.duplex = LPFC_ASYNC_LINK_DUPLEX_FULL;
	phba->sli4_hba.link_state.topology =
				bf_get(lpfc_acqe_fc_la_topology, acqe_fc);
	phba->sli4_hba.link_state.status =
				bf_get(lpfc_acqe_fc_la_att_type, acqe_fc);
	phba->sli4_hba.link_state.type =
				bf_get(lpfc_acqe_fc_la_port_type, acqe_fc);
	phba->sli4_hba.link_state.number =
				bf_get(lpfc_acqe_fc_la_port_number, acqe_fc);
	phba->sli4_hba.link_state.fault =
				bf_get(lpfc_acqe_link_fault, acqe_fc);

	if (bf_get(lpfc_acqe_fc_la_att_type, acqe_fc) ==
	    LPFC_FC_LA_TYPE_LINK_DOWN)
		phba->sli4_hba.link_state.logical_speed = 0;
	else if	(!phba->sli4_hba.conf_trunk)
		phba->sli4_hba.link_state.logical_speed =
				bf_get(lpfc_acqe_fc_la_llink_spd, acqe_fc) * 10;

	lpfc_printf_log(phba, KERN_INFO, LOG_SLI,
			"2896 Async FC event - Speed:%dGBaud Topology:x%x "
			"LA Type:x%x Port Type:%d Port Number:%d Logical speed:"
			"%dMbps Fault:%d\n",
			phba->sli4_hba.link_state.speed,
			phba->sli4_hba.link_state.topology,
			phba->sli4_hba.link_state.status,
			phba->sli4_hba.link_state.type,
			phba->sli4_hba.link_state.number,
			phba->sli4_hba.link_state.logical_speed,
			phba->sli4_hba.link_state.fault);
	pmb = (LPFC_MBOXQ_t *)mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!pmb) {
		lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
				"2897 The mboxq allocation failed\n");
		return;
	}
	mp = kmalloc(sizeof(struct lpfc_dmabuf), GFP_KERNEL);
	if (!mp) {
		lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
				"2898 The lpfc_dmabuf allocation failed\n");
		goto out_free_pmb;
	}
	mp->virt = lpfc_mbuf_alloc(phba, 0, &mp->phys);
	if (!mp->virt) {
		lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
				"2899 The mbuf allocation failed\n");
		goto out_free_dmabuf;
	}

	/* Cleanup any outstanding ELS commands */
	lpfc_els_flush_all_cmd(phba);

	/* Block ELS IOCBs until we have done process link event */
	phba->sli4_hba.els_wq->pring->flag |= LPFC_STOP_IOCB_EVENT;

	/* Update link event statistics */
	phba->sli.slistat.link_event++;

	/* Create lpfc_handle_latt mailbox command from link ACQE */
	lpfc_read_topology(phba, pmb, mp);
	pmb->mbox_cmpl = lpfc_mbx_cmpl_read_topology;
	pmb->vport = phba->pport;

	if (phba->sli4_hba.link_state.status != LPFC_FC_LA_TYPE_LINK_UP) {
		phba->link_flag &= ~(LS_MDS_LINK_DOWN | LS_MDS_LOOPBACK);

		switch (phba->sli4_hba.link_state.status) {
		case LPFC_FC_LA_TYPE_MDS_LINK_DOWN:
			phba->link_flag |= LS_MDS_LINK_DOWN;
			break;
		case LPFC_FC_LA_TYPE_MDS_LOOPBACK:
			phba->link_flag |= LS_MDS_LOOPBACK;
			break;
		default:
			break;
		}

		/* Initialize completion status */
		mb = &pmb->u.mb;
		mb->mbxStatus = MBX_SUCCESS;

		/* Parse port fault information field */
		lpfc_sli4_parse_latt_fault(phba, (void *)acqe_fc);

		/* Parse and translate link attention fields */
		la = (struct lpfc_mbx_read_top *)&pmb->u.mb.un.varReadTop;
		la->eventTag = acqe_fc->event_tag;

		if (phba->sli4_hba.link_state.status ==
		    LPFC_FC_LA_TYPE_UNEXP_WWPN) {
			bf_set(lpfc_mbx_read_top_att_type, la,
			       LPFC_FC_LA_TYPE_UNEXP_WWPN);
		} else {
			bf_set(lpfc_mbx_read_top_att_type, la,
			       LPFC_FC_LA_TYPE_LINK_DOWN);
		}
		/* Invoke the mailbox command callback function */
		lpfc_mbx_cmpl_read_topology(phba, pmb);

		return;
	}

	rc = lpfc_sli_issue_mbox(phba, pmb, MBX_NOWAIT);
	if (rc == MBX_NOT_FINISHED)
		goto out_free_dmabuf;
	return;

out_free_dmabuf:
	kfree(mp);
out_free_pmb:
	mempool_free(pmb, phba->mbox_mem_pool);
}

/**
 * lpfc_sli4_async_sli_evt - Process the asynchronous SLI link event
 * @phba: pointer to lpfc hba data structure.
 * @acqe_fc: pointer to the async SLI completion queue entry.
 *
 * This routine is to handle the SLI4 asynchronous SLI events.
 **/
static void
lpfc_sli4_async_sli_evt(struct lpfc_hba *phba, struct lpfc_acqe_sli *acqe_sli)
{
	char port_name;
	char message[128];
	uint8_t status;
	uint8_t evt_type;
	uint8_t operational = 0;
	struct temp_event temp_event_data;
	struct lpfc_acqe_misconfigured_event *misconfigured;
	struct Scsi_Host  *shost;
	struct lpfc_vport **vports;
	int rc, i;

	evt_type = bf_get(lpfc_trailer_type, acqe_sli);

	lpfc_printf_log(phba, KERN_INFO, LOG_SLI,
			"2901 Async SLI event - Type:%d, Event Data: x%08x "
			"x%08x x%08x x%08x\n", evt_type,
			acqe_sli->event_data1, acqe_sli->event_data2,
			acqe_sli->reserved, acqe_sli->trailer);

	port_name = phba->Port[0];
	if (port_name == 0x00)
		port_name = '?'; /* get port name is empty */

	switch (evt_type) {
	case LPFC_SLI_EVENT_TYPE_OVER_TEMP:
		temp_event_data.event_type = FC_REG_TEMPERATURE_EVENT;
		temp_event_data.event_code = LPFC_THRESHOLD_TEMP;
		temp_event_data.data = (uint32_t)acqe_sli->event_data1;

		lpfc_printf_log(phba, KERN_WARNING, LOG_SLI,
				"3190 Over Temperature:%d Celsius- Port Name %c\n",
				acqe_sli->event_data1, port_name);

		phba->sfp_warning |= LPFC_TRANSGRESSION_HIGH_TEMPERATURE;
		shost = lpfc_shost_from_vport(phba->pport);
		fc_host_post_vendor_event(shost, fc_get_event_number(),
					  sizeof(temp_event_data),
					  (char *)&temp_event_data,
					  SCSI_NL_VID_TYPE_PCI
					  | PCI_VENDOR_ID_EMULEX);
		break;
	case LPFC_SLI_EVENT_TYPE_NORM_TEMP:
		temp_event_data.event_type = FC_REG_TEMPERATURE_EVENT;
		temp_event_data.event_code = LPFC_NORMAL_TEMP;
		temp_event_data.data = (uint32_t)acqe_sli->event_data1;

		lpfc_printf_log(phba, KERN_INFO, LOG_SLI,
				"3191 Normal Temperature:%d Celsius - Port Name %c\n",
				acqe_sli->event_data1, port_name);

		shost = lpfc_shost_from_vport(phba->pport);
		fc_host_post_vendor_event(shost, fc_get_event_number(),
					  sizeof(temp_event_data),
					  (char *)&temp_event_data,
					  SCSI_NL_VID_TYPE_PCI
					  | PCI_VENDOR_ID_EMULEX);
		break;
	case LPFC_SLI_EVENT_TYPE_MISCONFIGURED:
		misconfigured = (struct lpfc_acqe_misconfigured_event *)
					&acqe_sli->event_data1;

		/* fetch the status for this port */
		switch (phba->sli4_hba.lnk_info.lnk_no) {
		case LPFC_LINK_NUMBER_0:
			status = bf_get(lpfc_sli_misconfigured_port0_state,
					&misconfigured->theEvent);
			operational = bf_get(lpfc_sli_misconfigured_port0_op,
					&misconfigured->theEvent);
			break;
		case LPFC_LINK_NUMBER_1:
			status = bf_get(lpfc_sli_misconfigured_port1_state,
					&misconfigured->theEvent);
			operational = bf_get(lpfc_sli_misconfigured_port1_op,
					&misconfigured->theEvent);
			break;
		case LPFC_LINK_NUMBER_2:
			status = bf_get(lpfc_sli_misconfigured_port2_state,
					&misconfigured->theEvent);
			operational = bf_get(lpfc_sli_misconfigured_port2_op,
					&misconfigured->theEvent);
			break;
		case LPFC_LINK_NUMBER_3:
			status = bf_get(lpfc_sli_misconfigured_port3_state,
					&misconfigured->theEvent);
			operational = bf_get(lpfc_sli_misconfigured_port3_op,
					&misconfigured->theEvent);
			break;
		default:
			lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
					"3296 "
					"LPFC_SLI_EVENT_TYPE_MISCONFIGURED "
					"event: Invalid link %d",
					phba->sli4_hba.lnk_info.lnk_no);
			return;
		}

		/* Skip if optic state unchanged */
		if (phba->sli4_hba.lnk_info.optic_state == status)
			return;

		switch (status) {
		case LPFC_SLI_EVENT_STATUS_VALID:
			sprintf(message, "Physical Link is functional");
			break;
		case LPFC_SLI_EVENT_STATUS_NOT_PRESENT:
			sprintf(message, "Optics faulted/incorrectly "
				"installed/not installed - Reseat optics, "
				"if issue not resolved, replace.");
			break;
		case LPFC_SLI_EVENT_STATUS_WRONG_TYPE:
			sprintf(message,
				"Optics of two types installed - Remove one "
				"optic or install matching pair of optics.");
			break;
		case LPFC_SLI_EVENT_STATUS_UNSUPPORTED:
			sprintf(message, "Incompatible optics - Replace with "
				"compatible optics for card to function.");
			break;
		case LPFC_SLI_EVENT_STATUS_UNQUALIFIED:
			sprintf(message, "Unqualified optics - Replace with "
				"Avago optics for Warranty and Technical "
				"Support - Link is%s operational",
				(operational) ? " not" : "");
			break;
		case LPFC_SLI_EVENT_STATUS_UNCERTIFIED:
			sprintf(message, "Uncertified optics - Replace with "
				"Avago-certified optics to enable link "
				"operation - Link is%s operational",
				(operational) ? " not" : "");
			break;
		default:
			/* firmware is reporting a status we don't know about */
			sprintf(message, "Unknown event status x%02x", status);
			break;
		}

		/* Issue READ_CONFIG mbox command to refresh supported speeds */
		rc = lpfc_sli4_read_config(phba);
		if (rc) {
			phba->lmt = 0;
			lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
					"3194 Unable to retrieve supported "
					"speeds, rc = 0x%x\n", rc);
		}
		vports = lpfc_create_vport_work_array(phba);
		if (vports != NULL) {
			for (i = 0; i <= phba->max_vports && vports[i] != NULL;
					i++) {
				shost = lpfc_shost_from_vport(vports[i]);
				lpfc_host_supported_speeds_set(shost);
			}
		}
		lpfc_destroy_vport_work_array(phba, vports);

		phba->sli4_hba.lnk_info.optic_state = status;
		lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
				"3176 Port Name %c %s\n", port_name, message);
		break;
	case LPFC_SLI_EVENT_TYPE_REMOTE_DPORT:
		lpfc_printf_log(phba, KERN_INFO, LOG_SLI,
				"3192 Remote DPort Test Initiated - "
				"Event Data1:x%08x Event Data2: x%08x\n",
				acqe_sli->event_data1, acqe_sli->event_data2);
		break;
	case LPFC_SLI_EVENT_TYPE_MISCONF_FAWWN:
		/* Misconfigured WWN. Reports that the SLI Port is configured
		 * to use FA-WWN, but the attached device doesn’t support it.
		 * No driver action is required.
		 * Event Data1 - N.A, Event Data2 - N.A
		 */
		lpfc_log_msg(phba, KERN_WARNING, LOG_SLI,
			     "2699 Misconfigured FA-WWN - Attached device does "
			     "not support FA-WWN\n");
		break;
	case LPFC_SLI_EVENT_TYPE_EEPROM_FAILURE:
		/* EEPROM failure. No driver action is required */
		lpfc_printf_log(phba, KERN_WARNING, LOG_SLI,
			     "2518 EEPROM failure - "
			     "Event Data1: x%08x Event Data2: x%08x\n",
			     acqe_sli->event_data1, acqe_sli->event_data2);
		break;
	default:
		lpfc_printf_log(phba, KERN_INFO, LOG_SLI,
				"3193 Unrecognized SLI event, type: 0x%x",
				evt_type);
		break;
	}
}

/**
 * lpfc_sli4_perform_vport_cvl - Perform clear virtual link on a vport
 * @vport: pointer to vport data structure.
 *
 * This routine is to perform Clear Virtual Link (CVL) on a vport in
 * response to a CVL event.
 *
 * Return the pointer to the ndlp with the vport if successful, otherwise
 * return NULL.
 **/
static struct lpfc_nodelist *
lpfc_sli4_perform_vport_cvl(struct lpfc_vport *vport)
{
	struct lpfc_nodelist *ndlp;
	struct Scsi_Host *shost;
	struct lpfc_hba *phba;

	if (!vport)
		return NULL;
	phba = vport->phba;
	if (!phba)
		return NULL;
	ndlp = lpfc_findnode_did(vport, Fabric_DID);
	if (!ndlp) {
		/* Cannot find existing Fabric ndlp, so allocate a new one */
		ndlp = lpfc_nlp_init(vport, Fabric_DID);
		if (!ndlp)
			return 0;
		/* Set the node type */
		ndlp->nlp_type |= NLP_FABRIC;
		/* Put ndlp onto node list */
		lpfc_enqueue_node(vport, ndlp);
	} else if (!NLP_CHK_NODE_ACT(ndlp)) {
		/* re-setup ndlp without removing from node list */
		ndlp = lpfc_enable_node(vport, ndlp, NLP_STE_UNUSED_NODE);
		if (!ndlp)
			return 0;
	}
	if ((phba->pport->port_state < LPFC_FLOGI) &&
		(phba->pport->port_state != LPFC_VPORT_FAILED))
		return NULL;
	/* If virtual link is not yet instantiated ignore CVL */
	if ((vport != phba->pport) && (vport->port_state < LPFC_FDISC)
		&& (vport->port_state != LPFC_VPORT_FAILED))
		return NULL;
	shost = lpfc_shost_from_vport(vport);
	if (!shost)
		return NULL;
	lpfc_linkdown_port(vport);
	lpfc_cleanup_pending_mbox(vport);
	spin_lock_irq(shost->host_lock);
	vport->fc_flag |= FC_VPORT_CVL_RCVD;
	spin_unlock_irq(shost->host_lock);

	return ndlp;
}

/**
 * lpfc_sli4_perform_all_vport_cvl - Perform clear virtual link on all vports
 * @vport: pointer to lpfc hba data structure.
 *
 * This routine is to perform Clear Virtual Link (CVL) on all vports in
 * response to a FCF dead event.
 **/
static void
lpfc_sli4_perform_all_vport_cvl(struct lpfc_hba *phba)
{
	struct lpfc_vport **vports;
	int i;

	vports = lpfc_create_vport_work_array(phba);
	if (vports)
		for (i = 0; i <= phba->max_vports && vports[i] != NULL; i++)
			lpfc_sli4_perform_vport_cvl(vports[i]);
	lpfc_destroy_vport_work_array(phba, vports);
}

/**
 * lpfc_sli4_async_fip_evt - Process the asynchronous FCoE FIP event
 * @phba: pointer to lpfc hba data structure.
 * @acqe_link: pointer to the async fcoe completion queue entry.
 *
 * This routine is to handle the SLI4 asynchronous fcoe event.
 **/
static void
lpfc_sli4_async_fip_evt(struct lpfc_hba *phba,
			struct lpfc_acqe_fip *acqe_fip)
{
	uint8_t event_type = bf_get(lpfc_trailer_type, acqe_fip);
	int rc;
	struct lpfc_vport *vport;
	struct lpfc_nodelist *ndlp;
	struct Scsi_Host  *shost;
	int active_vlink_present;
	struct lpfc_vport **vports;
	int i;

	phba->fc_eventTag = acqe_fip->event_tag;
	phba->fcoe_eventtag = acqe_fip->event_tag;
	switch (event_type) {
	case LPFC_FIP_EVENT_TYPE_NEW_FCF:
	case LPFC_FIP_EVENT_TYPE_FCF_PARAM_MOD:
		if (event_type == LPFC_FIP_EVENT_TYPE_NEW_FCF)
			lpfc_printf_log(phba, KERN_ERR, LOG_FIP |
					LOG_DISCOVERY,
					"2546 New FCF event, evt_tag:x%x, "
					"index:x%x\n",
					acqe_fip->event_tag,
					acqe_fip->index);
		else
			lpfc_printf_log(phba, KERN_WARNING, LOG_FIP |
					LOG_DISCOVERY,
					"2788 FCF param modified event, "
					"evt_tag:x%x, index:x%x\n",
					acqe_fip->event_tag,
					acqe_fip->index);
		if (phba->fcf.fcf_flag & FCF_DISCOVERY) {
			/*
			 * During period of FCF discovery, read the FCF
			 * table record indexed by the event to update
			 * FCF roundrobin failover eligible FCF bmask.
			 */
			lpfc_printf_log(phba, KERN_INFO, LOG_FIP |
					LOG_DISCOVERY,
					"2779 Read FCF (x%x) for updating "
					"roundrobin FCF failover bmask\n",
					acqe_fip->index);
			rc = lpfc_sli4_read_fcf_rec(phba, acqe_fip->index);
		}

		/* If the FCF discovery is in progress, do nothing. */
		spin_lock_irq(&phba->hbalock);
		if (phba->hba_flag & FCF_TS_INPROG) {
			spin_unlock_irq(&phba->hbalock);
			break;
		}
		/* If fast FCF failover rescan event is pending, do nothing */
		if (phba->fcf.fcf_flag & (FCF_REDISC_EVT | FCF_REDISC_PEND)) {
			spin_unlock_irq(&phba->hbalock);
			break;
		}

		/* If the FCF has been in discovered state, do nothing. */
		if (phba->fcf.fcf_flag & FCF_SCAN_DONE) {
			spin_unlock_irq(&phba->hbalock);
			break;
		}
		spin_unlock_irq(&phba->hbalock);

		/* Otherwise, scan the entire FCF table and re-discover SAN */
		lpfc_printf_log(phba, KERN_INFO, LOG_FIP | LOG_DISCOVERY,
				"2770 Start FCF table scan per async FCF "
				"event, evt_tag:x%x, index:x%x\n",
				acqe_fip->event_tag, acqe_fip->index);
		rc = lpfc_sli4_fcf_scan_read_fcf_rec(phba,
						     LPFC_FCOE_FCF_GET_FIRST);
		if (rc)
			lpfc_printf_log(phba, KERN_ERR, LOG_FIP | LOG_DISCOVERY,
					"2547 Issue FCF scan read FCF mailbox "
					"command failed (x%x)\n", rc);
		break;

	case LPFC_FIP_EVENT_TYPE_FCF_TABLE_FULL:
		lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
			"2548 FCF Table full count 0x%x tag 0x%x\n",
			bf_get(lpfc_acqe_fip_fcf_count, acqe_fip),
			acqe_fip->event_tag);
		break;

	case LPFC_FIP_EVENT_TYPE_FCF_DEAD:
		phba->fcoe_cvl_eventtag = acqe_fip->event_tag;
		lpfc_printf_log(phba, KERN_ERR, LOG_FIP | LOG_DISCOVERY,
			"2549 FCF (x%x) disconnected from network, "
			"tag:x%x\n", acqe_fip->index, acqe_fip->event_tag);
		/*
		 * If we are in the middle of FCF failover process, clear
		 * the corresponding FCF bit in the roundrobin bitmap.
		 */
		spin_lock_irq(&phba->hbalock);
		if ((phba->fcf.fcf_flag & FCF_DISCOVERY) &&
		    (phba->fcf.current_rec.fcf_indx != acqe_fip->index)) {
			spin_unlock_irq(&phba->hbalock);
			/* Update FLOGI FCF failover eligible FCF bmask */
			lpfc_sli4_fcf_rr_index_clear(phba, acqe_fip->index);
			break;
		}
		spin_unlock_irq(&phba->hbalock);

		/* If the event is not for currently used fcf do nothing */
		if (phba->fcf.current_rec.fcf_indx != acqe_fip->index)
			break;

		/*
		 * Otherwise, request the port to rediscover the entire FCF
		 * table for a fast recovery from case that the current FCF
		 * is no longer valid as we are not in the middle of FCF
		 * failover process already.
		 */
		spin_lock_irq(&phba->hbalock);
		/* Mark the fast failover process in progress */
		phba->fcf.fcf_flag |= FCF_DEAD_DISC;
		spin_unlock_irq(&phba->hbalock);

		lpfc_printf_log(phba, KERN_INFO, LOG_FIP | LOG_DISCOVERY,
				"2771 Start FCF fast failover process due to "
				"FCF DEAD event: evt_tag:x%x, fcf_index:x%x "
				"\n", acqe_fip->event_tag, acqe_fip->index);
		rc = lpfc_sli4_redisc_fcf_table(phba);
		if (rc) {
			lpfc_printf_log(phba, KERN_ERR, LOG_FIP |
					LOG_DISCOVERY,
					"2772 Issue FCF rediscover mailbox "
					"command failed, fail through to FCF "
					"dead event\n");
			spin_lock_irq(&phba->hbalock);
			phba->fcf.fcf_flag &= ~FCF_DEAD_DISC;
			spin_unlock_irq(&phba->hbalock);
			/*
			 * Last resort will fail over by treating this
			 * as a link down to FCF registration.
			 */
			lpfc_sli4_fcf_dead_failthrough(phba);
		} else {
			/* Reset FCF roundrobin bmask for new discovery */
			lpfc_sli4_clear_fcf_rr_bmask(phba);
			/*
			 * Handling fast FCF failover to a DEAD FCF event is
			 * considered equalivant to receiving CVL to all vports.
			 */
			lpfc_sli4_perform_all_vport_cvl(phba);
		}
		break;
	case LPFC_FIP_EVENT_TYPE_CVL:
		phba->fcoe_cvl_eventtag = acqe_fip->event_tag;
		lpfc_printf_log(phba, KERN_ERR, LOG_FIP | LOG_DISCOVERY,
			"2718 Clear Virtual Link Received for VPI 0x%x"
			" tag 0x%x\n", acqe_fip->index, acqe_fip->event_tag);

		vport = lpfc_find_vport_by_vpid(phba,
						acqe_fip->index);
		ndlp = lpfc_sli4_perform_vport_cvl(vport);
		if (!ndlp)
			break;
		active_vlink_present = 0;

		vports = lpfc_create_vport_work_array(phba);
		if (vports) {
			for (i = 0; i <= phba->max_vports && vports[i] != NULL;
					i++) {
				if ((!(vports[i]->fc_flag &
					FC_VPORT_CVL_RCVD)) &&
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
			shost = lpfc_shost_from_vport(vport);
			spin_lock_irq(shost->host_lock);
			ndlp->nlp_flag |= NLP_DELAY_TMO;
			spin_unlock_irq(shost->host_lock);
			ndlp->nlp_last_elscmd = ELS_CMD_FDISC;
			vport->port_state = LPFC_FDISC;
		} else {
			/*
			 * Otherwise, we request port to rediscover
			 * the entire FCF table for a fast recovery
			 * from possible case that the current FCF
			 * is no longer valid if we are not already
			 * in the FCF failover process.
			 */
			spin_lock_irq(&phba->hbalock);
			if (phba->fcf.fcf_flag & FCF_DISCOVERY) {
				spin_unlock_irq(&phba->hbalock);
				break;
			}
			/* Mark the fast failover process in progress */
			phba->fcf.fcf_flag |= FCF_ACVL_DISC;
			spin_unlock_irq(&phba->hbalock);
			lpfc_printf_log(phba, KERN_INFO, LOG_FIP |
					LOG_DISCOVERY,
					"2773 Start FCF failover per CVL, "
					"evt_tag:x%x\n", acqe_fip->event_tag);
			rc = lpfc_sli4_redisc_fcf_table(phba);
			if (rc) {
				lpfc_printf_log(phba, KERN_ERR, LOG_FIP |
						LOG_DISCOVERY,
						"2774 Issue FCF rediscover "
						"mailbox command failed, "
						"through to CVL event\n");
				spin_lock_irq(&phba->hbalock);
				phba->fcf.fcf_flag &= ~FCF_ACVL_DISC;
				spin_unlock_irq(&phba->hbalock);
				/*
				 * Last resort will be re-try on the
				 * the current registered FCF entry.
				 */
				lpfc_retry_pport_discovery(phba);
			} else
				/*
				 * Reset FCF roundrobin bmask for new
				 * discovery.
				 */
				lpfc_sli4_clear_fcf_rr_bmask(phba);
		}
		break;
	default:
		lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
			"0288 Unknown FCoE event type 0x%x event tag "
			"0x%x\n", event_type, acqe_fip->event_tag);
		break;
	}
}

/**
 * lpfc_sli4_async_dcbx_evt - Process the asynchronous dcbx event
 * @phba: pointer to lpfc hba data structure.
 * @acqe_link: pointer to the async dcbx completion queue entry.
 *
 * This routine is to handle the SLI4 asynchronous dcbx event.
 **/
static void
lpfc_sli4_async_dcbx_evt(struct lpfc_hba *phba,
			 struct lpfc_acqe_dcbx *acqe_dcbx)
{
	phba->fc_eventTag = acqe_dcbx->event_tag;
	lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
			"0290 The SLI4 DCBX asynchronous event is not "
			"handled yet\n");
}

/**
 * lpfc_sli4_async_grp5_evt - Process the asynchronous group5 event
 * @phba: pointer to lpfc hba data structure.
 * @acqe_link: pointer to the async grp5 completion queue entry.
 *
 * This routine is to handle the SLI4 asynchronous grp5 event. A grp5 event
 * is an asynchronous notified of a logical link speed change.  The Port
 * reports the logical link speed in units of 10Mbps.
 **/
static void
lpfc_sli4_async_grp5_evt(struct lpfc_hba *phba,
			 struct lpfc_acqe_grp5 *acqe_grp5)
{
	uint16_t prev_ll_spd;

	phba->fc_eventTag = acqe_grp5->event_tag;
	phba->fcoe_eventtag = acqe_grp5->event_tag;
	prev_ll_spd = phba->sli4_hba.link_state.logical_speed;
	phba->sli4_hba.link_state.logical_speed =
		(bf_get(lpfc_acqe_grp5_llink_spd, acqe_grp5)) * 10;
	lpfc_printf_log(phba, KERN_INFO, LOG_SLI,
			"2789 GRP5 Async Event: Updating logical link speed "
			"from %dMbps to %dMbps\n", prev_ll_spd,
			phba->sli4_hba.link_state.logical_speed);
}

/**
 * lpfc_sli4_async_event_proc - Process all the pending asynchronous event
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked by the worker thread to process all the pending
 * SLI4 asynchronous events.
 **/
void lpfc_sli4_async_event_proc(struct lpfc_hba *phba)
{
	struct lpfc_cq_event *cq_event;

	/* First, declare the async event has been handled */
	spin_lock_irq(&phba->hbalock);
	phba->hba_flag &= ~ASYNC_EVENT;
	spin_unlock_irq(&phba->hbalock);
	/* Now, handle all the async events */
	while (!list_empty(&phba->sli4_hba.sp_asynce_work_queue)) {
		/* Get the first event from the head of the event queue */
		spin_lock_irq(&phba->hbalock);
		list_remove_head(&phba->sli4_hba.sp_asynce_work_queue,
				 cq_event, struct lpfc_cq_event, list);
		spin_unlock_irq(&phba->hbalock);
		/* Process the asynchronous event */
		switch (bf_get(lpfc_trailer_code, &cq_event->cqe.mcqe_cmpl)) {
		case LPFC_TRAILER_CODE_LINK:
			lpfc_sli4_async_link_evt(phba,
						 &cq_event->cqe.acqe_link);
			break;
		case LPFC_TRAILER_CODE_FCOE:
			lpfc_sli4_async_fip_evt(phba, &cq_event->cqe.acqe_fip);
			break;
		case LPFC_TRAILER_CODE_DCBX:
			lpfc_sli4_async_dcbx_evt(phba,
						 &cq_event->cqe.acqe_dcbx);
			break;
		case LPFC_TRAILER_CODE_GRP5:
			lpfc_sli4_async_grp5_evt(phba,
						 &cq_event->cqe.acqe_grp5);
			break;
		case LPFC_TRAILER_CODE_FC:
			lpfc_sli4_async_fc_evt(phba, &cq_event->cqe.acqe_fc);
			break;
		case LPFC_TRAILER_CODE_SLI:
			lpfc_sli4_async_sli_evt(phba, &cq_event->cqe.acqe_sli);
			break;
		default:
			lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
					"1804 Invalid asynchronous event code: "
					"x%x\n", bf_get(lpfc_trailer_code,
					&cq_event->cqe.mcqe_cmpl));
			break;
		}
		/* Free the completion event processed to the free pool */
		lpfc_sli4_cq_event_release(phba, cq_event);
	}
}

/**
 * lpfc_sli4_fcf_redisc_event_proc - Process fcf table rediscovery event
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked by the worker thread to process FCF table
 * rediscovery pending completion event.
 **/
void lpfc_sli4_fcf_redisc_event_proc(struct lpfc_hba *phba)
{
	int rc;

	spin_lock_irq(&phba->hbalock);
	/* Clear FCF rediscovery timeout event */
	phba->fcf.fcf_flag &= ~FCF_REDISC_EVT;
	/* Clear driver fast failover FCF record flag */
	phba->fcf.failover_rec.flag = 0;
	/* Set state for FCF fast failover */
	phba->fcf.fcf_flag |= FCF_REDISC_FOV;
	spin_unlock_irq(&phba->hbalock);

	/* Scan FCF table from the first entry to re-discover SAN */
	lpfc_printf_log(phba, KERN_INFO, LOG_FIP | LOG_DISCOVERY,
			"2777 Start post-quiescent FCF table scan\n");
	rc = lpfc_sli4_fcf_scan_read_fcf_rec(phba, LPFC_FCOE_FCF_GET_FIRST);
	if (rc)
		lpfc_printf_log(phba, KERN_ERR, LOG_FIP | LOG_DISCOVERY,
				"2747 Issue FCF scan read FCF mailbox "
				"command failed 0x%x\n", rc);
}

/**
 * lpfc_api_table_setup - Set up per hba pci-device group func api jump table
 * @phba: pointer to lpfc hba data structure.
 * @dev_grp: The HBA PCI-Device group number.
 *
 * This routine is invoked to set up the per HBA PCI-Device group function
 * API jump table entries.
 *
 * Return: 0 if success, otherwise -ENODEV
 **/
int
lpfc_api_table_setup(struct lpfc_hba *phba, uint8_t dev_grp)
{
	int rc;

	/* Set up lpfc PCI-device group */
	phba->pci_dev_grp = dev_grp;

	/* The LPFC_PCI_DEV_OC uses SLI4 */
	if (dev_grp == LPFC_PCI_DEV_OC)
		phba->sli_rev = LPFC_SLI_REV4;

	/* Set up device INIT API function jump table */
	rc = lpfc_init_api_table_setup(phba, dev_grp);
	if (rc)
		return -ENODEV;
	/* Set up SCSI API function jump table */
	rc = lpfc_scsi_api_table_setup(phba, dev_grp);
	if (rc)
		return -ENODEV;
	/* Set up SLI API function jump table */
	rc = lpfc_sli_api_table_setup(phba, dev_grp);
	if (rc)
		return -ENODEV;
	/* Set up MBOX API function jump table */
	rc = lpfc_mbox_api_table_setup(phba, dev_grp);
	if (rc)
		return -ENODEV;

	return 0;
}

/**
 * lpfc_log_intr_mode - Log the active interrupt mode
 * @phba: pointer to lpfc hba data structure.
 * @intr_mode: active interrupt mode adopted.
 *
 * This routine it invoked to log the currently used active interrupt mode
 * to the device.
 **/
static void lpfc_log_intr_mode(struct lpfc_hba *phba, uint32_t intr_mode)
{
	switch (intr_mode) {
	case 0:
		lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
				"0470 Enable INTx interrupt mode.\n");
		break;
	case 1:
		lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
				"0481 Enabled MSI interrupt mode.\n");
		break;
	case 2:
		lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
				"0480 Enabled MSI-X interrupt mode.\n");
		break;
	default:
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0482 Illegal interrupt mode.\n");
		break;
	}
	return;
}

/**
 * lpfc_cpumask_of_node_init - initalizes cpumask of phba's NUMA node
 * @phba: Pointer to HBA context object.
 *
 **/
static void
lpfc_cpumask_of_node_init(struct lpfc_hba *phba)
{
	unsigned int cpu, numa_node;
	struct cpumask *numa_mask = &phba->sli4_hba.numa_mask;

	cpumask_clear(numa_mask);

	/* Check if we're a NUMA architecture */
	numa_node = dev_to_node(&phba->pcidev->dev);
	if (numa_node == NUMA_NO_NODE)
		return;

	for_each_possible_cpu(cpu)
		if (cpu_to_node(cpu) == numa_node)
			cpumask_set_cpu(cpu, numa_mask);
}

/**
 * lpfc_enable_pci_dev - Enable a generic PCI device.
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to enable the PCI device that is common to all
 * PCI devices.
 *
 * Return codes
 * 	0 - successful
 * 	other values - error
 **/
static int
lpfc_enable_pci_dev(struct lpfc_hba *phba)
{
	struct pci_dev *pdev;

	/* Obtain PCI device reference */
	if (!phba->pcidev)
		goto out_error;
	else
		pdev = phba->pcidev;
	/* Enable PCI device */
	if (pci_enable_device_mem(pdev))
		goto out_error;
	/* Request PCI resource for the device */
	if (pci_request_mem_regions(pdev, LPFC_DRIVER_NAME))
		goto out_disable_device;
	/* Set up device as PCI master and save state for EEH */
	pci_set_master(pdev);
	pci_try_set_mwi(pdev);
	pci_save_state(pdev);

	/* PCIe EEH recovery on powerpc platforms needs fundamental reset */
	if (pci_is_pcie(pdev))
		pdev->needs_freset = 1;

	return 0;

out_disable_device:
	pci_disable_device(pdev);
out_error:
	lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
			"1401 Failed to enable pci device\n");
	return -ENODEV;
}

/**
 * lpfc_disable_pci_dev - Disable a generic PCI device.
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to disable the PCI device that is common to all
 * PCI devices.
 **/
static void
lpfc_disable_pci_dev(struct lpfc_hba *phba)
{
	struct pci_dev *pdev;

	/* Obtain PCI device reference */
	if (!phba->pcidev)
		return;
	else
		pdev = phba->pcidev;
	/* Release PCI resource and disable PCI device */
	pci_release_mem_regions(pdev);
	pci_disable_device(pdev);

	return;
}

/**
 * lpfc_reset_hba - Reset a hba
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to reset a hba device. It brings the HBA
 * offline, performs a board restart, and then brings the board back
 * online. The lpfc_offline calls lpfc_sli_hba_down which will clean up
 * on outstanding mailbox commands.
 **/
void
lpfc_reset_hba(struct lpfc_hba *phba)
{
	/* If resets are disabled then set error state and return. */
	if (!phba->cfg_enable_hba_reset) {
		phba->link_state = LPFC_HBA_ERROR;
		return;
	}
	if (phba->sli.sli_flag & LPFC_SLI_ACTIVE)
		lpfc_offline_prep(phba, LPFC_MBX_WAIT);
	else
		lpfc_offline_prep(phba, LPFC_MBX_NO_WAIT);
	lpfc_offline(phba);
	lpfc_sli_brdrestart(phba);
	lpfc_online(phba);
	lpfc_unblock_mgmt_io(phba);
}

/**
 * lpfc_sli_sriov_nr_virtfn_get - Get the number of sr-iov virtual functions
 * @phba: pointer to lpfc hba data structure.
 *
 * This function enables the PCI SR-IOV virtual functions to a physical
 * function. It invokes the PCI SR-IOV api with the @nr_vfn provided to
 * enable the number of virtual functions to the physical function. As
 * not all devices support SR-IOV, the return code from the pci_enable_sriov()
 * API call does not considered as an error condition for most of the device.
 **/
uint16_t
lpfc_sli_sriov_nr_virtfn_get(struct lpfc_hba *phba)
{
	struct pci_dev *pdev = phba->pcidev;
	uint16_t nr_virtfn;
	int pos;

	pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_SRIOV);
	if (pos == 0)
		return 0;

	pci_read_config_word(pdev, pos + PCI_SRIOV_TOTAL_VF, &nr_virtfn);
	return nr_virtfn;
}

/**
 * lpfc_sli_probe_sriov_nr_virtfn - Enable a number of sr-iov virtual functions
 * @phba: pointer to lpfc hba data structure.
 * @nr_vfn: number of virtual functions to be enabled.
 *
 * This function enables the PCI SR-IOV virtual functions to a physical
 * function. It invokes the PCI SR-IOV api with the @nr_vfn provided to
 * enable the number of virtual functions to the physical function. As
 * not all devices support SR-IOV, the return code from the pci_enable_sriov()
 * API call does not considered as an error condition for most of the device.
 **/
int
lpfc_sli_probe_sriov_nr_virtfn(struct lpfc_hba *phba, int nr_vfn)
{
	struct pci_dev *pdev = phba->pcidev;
	uint16_t max_nr_vfn;
	int rc;

	max_nr_vfn = lpfc_sli_sriov_nr_virtfn_get(phba);
	if (nr_vfn > max_nr_vfn) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"3057 Requested vfs (%d) greater than "
				"supported vfs (%d)", nr_vfn, max_nr_vfn);
		return -EINVAL;
	}

	rc = pci_enable_sriov(pdev, nr_vfn);
	if (rc) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_INIT,
				"2806 Failed to enable sriov on this device "
				"with vfn number nr_vf:%d, rc:%d\n",
				nr_vfn, rc);
	} else
		lpfc_printf_log(phba, KERN_WARNING, LOG_INIT,
				"2807 Successful enable sriov on this device "
				"with vfn number nr_vf:%d\n", nr_vfn);
	return rc;
}

/**
 * lpfc_setup_driver_resource_phase1 - Phase1 etup driver internal resources.
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to set up the driver internal resources before the
 * device specific resource setup to support the HBA device it attached to.
 *
 * Return codes
 *	0 - successful
 *	other values - error
 **/
static int
lpfc_setup_driver_resource_phase1(struct lpfc_hba *phba)
{
	struct lpfc_sli *psli = &phba->sli;

	/*
	 * Driver resources common to all SLI revisions
	 */
	atomic_set(&phba->fast_event_count, 0);
	spin_lock_init(&phba->hbalock);

	/* Initialize ndlp management spinlock */
	spin_lock_init(&phba->ndlp_lock);

	/* Initialize port_list spinlock */
	spin_lock_init(&phba->port_list_lock);
	INIT_LIST_HEAD(&phba->port_list);

	INIT_LIST_HEAD(&phba->work_list);
	init_waitqueue_head(&phba->wait_4_mlo_m_q);

	/* Initialize the wait queue head for the kernel thread */
	init_waitqueue_head(&phba->work_waitq);

	lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
			"1403 Protocols supported %s %s %s\n",
			((phba->cfg_enable_fc4_type & LPFC_ENABLE_FCP) ?
				"SCSI" : " "),
			((phba->cfg_enable_fc4_type & LPFC_ENABLE_NVME) ?
				"NVME" : " "),
			(phba->nvmet_support ? "NVMET" : " "));

	/* Initialize the IO buffer list used by driver for SLI3 SCSI */
	spin_lock_init(&phba->scsi_buf_list_get_lock);
	INIT_LIST_HEAD(&phba->lpfc_scsi_buf_list_get);
	spin_lock_init(&phba->scsi_buf_list_put_lock);
	INIT_LIST_HEAD(&phba->lpfc_scsi_buf_list_put);

	/* Initialize the fabric iocb list */
	INIT_LIST_HEAD(&phba->fabric_iocb_list);

	/* Initialize list to save ELS buffers */
	INIT_LIST_HEAD(&phba->elsbuf);

	/* Initialize FCF connection rec list */
	INIT_LIST_HEAD(&phba->fcf_conn_rec_list);

	/* Initialize OAS configuration list */
	spin_lock_init(&phba->devicelock);
	INIT_LIST_HEAD(&phba->luns);

	/* MBOX heartbeat timer */
	timer_setup(&psli->mbox_tmo, lpfc_mbox_timeout, 0);
	/* Fabric block timer */
	timer_setup(&phba->fabric_block_timer, lpfc_fabric_block_timeout, 0);
	/* EA polling mode timer */
	timer_setup(&phba->eratt_poll, lpfc_poll_eratt, 0);
	/* Heartbeat timer */
	timer_setup(&phba->hb_tmofunc, lpfc_hb_timeout, 0);

	INIT_DELAYED_WORK(&phba->eq_delay_work, lpfc_hb_eq_delay_work);

	return 0;
}

/**
 * lpfc_sli_driver_resource_setup - Setup driver internal resources for SLI3 dev
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to set up the driver internal resources specific to
 * support the SLI-3 HBA device it attached to.
 *
 * Return codes
 * 0 - successful
 * other values - error
 **/
static int
lpfc_sli_driver_resource_setup(struct lpfc_hba *phba)
{
	int rc, entry_sz;

	/*
	 * Initialize timers used by driver
	 */

	/* FCP polling mode timer */
	timer_setup(&phba->fcp_poll_timer, lpfc_poll_timeout, 0);

	/* Host attention work mask setup */
	phba->work_ha_mask = (HA_ERATT | HA_MBATT | HA_LATT);
	phba->work_ha_mask |= (HA_RXMASK << (LPFC_ELS_RING * 4));

	/* Get all the module params for configuring this host */
	lpfc_get_cfgparam(phba);
	/* Set up phase-1 common device driver resources */

	rc = lpfc_setup_driver_resource_phase1(phba);
	if (rc)
		return -ENODEV;

	if (phba->pcidev->device == PCI_DEVICE_ID_HORNET) {
		phba->menlo_flag |= HBA_MENLO_SUPPORT;
		/* check for menlo minimum sg count */
		if (phba->cfg_sg_seg_cnt < LPFC_DEFAULT_MENLO_SG_SEG_CNT)
			phba->cfg_sg_seg_cnt = LPFC_DEFAULT_MENLO_SG_SEG_CNT;
	}

	if (!phba->sli.sli3_ring)
		phba->sli.sli3_ring = kcalloc(LPFC_SLI3_MAX_RING,
					      sizeof(struct lpfc_sli_ring),
					      GFP_KERNEL);
	if (!phba->sli.sli3_ring)
		return -ENOMEM;

	/*
	 * Since lpfc_sg_seg_cnt is module parameter, the sg_dma_buf_size
	 * used to create the sg_dma_buf_pool must be dynamically calculated.
	 */

	/* Initialize the host templates the configured values. */
	lpfc_vport_template.sg_tablesize = phba->cfg_sg_seg_cnt;
	lpfc_template_no_hr.sg_tablesize = phba->cfg_sg_seg_cnt;
	lpfc_template.sg_tablesize = phba->cfg_sg_seg_cnt;

	if (phba->sli_rev == LPFC_SLI_REV4)
		entry_sz = sizeof(struct sli4_sge);
	else
		entry_sz = sizeof(struct ulp_bde64);

	/* There are going to be 2 reserved BDEs: 1 FCP cmnd + 1 FCP rsp */
	if (phba->cfg_enable_bg) {
		/*
		 * The scsi_buf for a T10-DIF I/O will hold the FCP cmnd,
		 * the FCP rsp, and a BDE for each. Sice we have no control
		 * over how many protection data segments the SCSI Layer
		 * will hand us (ie: there could be one for every block
		 * in the IO), we just allocate enough BDEs to accomidate
		 * our max amount and we need to limit lpfc_sg_seg_cnt to
		 * minimize the risk of running out.
		 */
		phba->cfg_sg_dma_buf_size = sizeof(struct fcp_cmnd) +
			sizeof(struct fcp_rsp) +
			(LPFC_MAX_SG_SEG_CNT * entry_sz);

		if (phba->cfg_sg_seg_cnt > LPFC_MAX_SG_SEG_CNT_DIF)
			phba->cfg_sg_seg_cnt = LPFC_MAX_SG_SEG_CNT_DIF;

		/* Total BDEs in BPL for scsi_sg_list and scsi_sg_prot_list */
		phba->cfg_total_seg_cnt = LPFC_MAX_SG_SEG_CNT;
	} else {
		/*
		 * The scsi_buf for a regular I/O will hold the FCP cmnd,
		 * the FCP rsp, a BDE for each, and a BDE for up to
		 * cfg_sg_seg_cnt data segments.
		 */
		phba->cfg_sg_dma_buf_size = sizeof(struct fcp_cmnd) +
			sizeof(struct fcp_rsp) +
			((phba->cfg_sg_seg_cnt + 2) * entry_sz);

		/* Total BDEs in BPL for scsi_sg_list */
		phba->cfg_total_seg_cnt = phba->cfg_sg_seg_cnt + 2;
	}

	lpfc_printf_log(phba, KERN_INFO, LOG_INIT | LOG_FCP,
			"9088 sg_tablesize:%d dmabuf_size:%d total_bde:%d\n",
			phba->cfg_sg_seg_cnt, phba->cfg_sg_dma_buf_size,
			phba->cfg_total_seg_cnt);

	phba->max_vpi = LPFC_MAX_VPI;
	/* This will be set to correct value after config_port mbox */
	phba->max_vports = 0;

	/*
	 * Initialize the SLI Layer to run with lpfc HBAs.
	 */
	lpfc_sli_setup(phba);
	lpfc_sli_queue_init(phba);

	/* Allocate device driver memory */
	if (lpfc_mem_alloc(phba, BPL_ALIGN_SZ))
		return -ENOMEM;

	phba->lpfc_sg_dma_buf_pool =
		dma_pool_create("lpfc_sg_dma_buf_pool",
				&phba->pcidev->dev, phba->cfg_sg_dma_buf_size,
				BPL_ALIGN_SZ, 0);

	if (!phba->lpfc_sg_dma_buf_pool)
		goto fail_free_mem;

	phba->lpfc_cmd_rsp_buf_pool =
			dma_pool_create("lpfc_cmd_rsp_buf_pool",
					&phba->pcidev->dev,
					sizeof(struct fcp_cmnd) +
					sizeof(struct fcp_rsp),
					BPL_ALIGN_SZ, 0);

	if (!phba->lpfc_cmd_rsp_buf_pool)
		goto fail_free_dma_buf_pool;

	/*
	 * Enable sr-iov virtual functions if supported and configured
	 * through the module parameter.
	 */
	if (phba->cfg_sriov_nr_virtfn > 0) {
		rc = lpfc_sli_probe_sriov_nr_virtfn(phba,
						 phba->cfg_sriov_nr_virtfn);
		if (rc) {
			lpfc_printf_log(phba, KERN_WARNING, LOG_INIT,
					"2808 Requested number of SR-IOV "
					"virtual functions (%d) is not "
					"supported\n",
					phba->cfg_sriov_nr_virtfn);
			phba->cfg_sriov_nr_virtfn = 0;
		}
	}

	return 0;

fail_free_dma_buf_pool:
	dma_pool_destroy(phba->lpfc_sg_dma_buf_pool);
	phba->lpfc_sg_dma_buf_pool = NULL;
fail_free_mem:
	lpfc_mem_free(phba);
	return -ENOMEM;
}

/**
 * lpfc_sli_driver_resource_unset - Unset drvr internal resources for SLI3 dev
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to unset the driver internal resources set up
 * specific for supporting the SLI-3 HBA device it attached to.
 **/
static void
lpfc_sli_driver_resource_unset(struct lpfc_hba *phba)
{
	/* Free device driver memory allocated */
	lpfc_mem_free_all(phba);

	return;
}

/**
 * lpfc_sli4_driver_resource_setup - Setup drvr internal resources for SLI4 dev
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to set up the driver internal resources specific to
 * support the SLI-4 HBA device it attached to.
 *
 * Return codes
 * 	0 - successful
 * 	other values - error
 **/
static int
lpfc_sli4_driver_resource_setup(struct lpfc_hba *phba)
{
	LPFC_MBOXQ_t *mboxq;
	MAILBOX_t *mb;
	int rc, i, max_buf_size;
	uint8_t pn_page[LPFC_MAX_SUPPORTED_PAGES] = {0};
	struct lpfc_mqe *mqe;
	int longs;
	int extra;
	uint64_t wwn;
	u32 if_type;
	u32 if_fam;

	phba->sli4_hba.num_present_cpu = lpfc_present_cpu;
	phba->sli4_hba.num_possible_cpu = cpumask_last(cpu_possible_mask) + 1;
	phba->sli4_hba.curr_disp_cpu = 0;
	lpfc_cpumask_of_node_init(phba);

	/* Get all the module params for configuring this host */
	lpfc_get_cfgparam(phba);

	/* Set up phase-1 common device driver resources */
	rc = lpfc_setup_driver_resource_phase1(phba);
	if (rc)
		return -ENODEV;

	/* Before proceed, wait for POST done and device ready */
	rc = lpfc_sli4_post_status_check(phba);
	if (rc)
		return -ENODEV;

	/* Allocate all driver workqueues here */

	/* The lpfc_wq workqueue for deferred irq use */
	phba->wq = alloc_workqueue("lpfc_wq", WQ_MEM_RECLAIM, 0);

	/*
	 * Initialize timers used by driver
	 */

	timer_setup(&phba->rrq_tmr, lpfc_rrq_timeout, 0);

	/* FCF rediscover timer */
	timer_setup(&phba->fcf.redisc_wait, lpfc_sli4_fcf_redisc_wait_tmo, 0);

	/*
	 * Control structure for handling external multi-buffer mailbox
	 * command pass-through.
	 */
	memset((uint8_t *)&phba->mbox_ext_buf_ctx, 0,
		sizeof(struct lpfc_mbox_ext_buf_ctx));
	INIT_LIST_HEAD(&phba->mbox_ext_buf_ctx.ext_dmabuf_list);

	phba->max_vpi = LPFC_MAX_VPI;

	/* This will be set to correct value after the read_config mbox */
	phba->max_vports = 0;

	/* Program the default value of vlan_id and fc_map */
	phba->valid_vlan = 0;
	phba->fc_map[0] = LPFC_FCOE_FCF_MAP0;
	phba->fc_map[1] = LPFC_FCOE_FCF_MAP1;
	phba->fc_map[2] = LPFC_FCOE_FCF_MAP2;

	/*
	 * For SLI4, instead of using ring 0 (LPFC_FCP_RING) for FCP commands
	 * we will associate a new ring, for each EQ/CQ/WQ tuple.
	 * The WQ create will allocate the ring.
	 */

	/* Initialize buffer queue management fields */
	INIT_LIST_HEAD(&phba->hbqs[LPFC_ELS_HBQ].hbq_buffer_list);
	phba->hbqs[LPFC_ELS_HBQ].hbq_alloc_buffer = lpfc_sli4_rb_alloc;
	phba->hbqs[LPFC_ELS_HBQ].hbq_free_buffer = lpfc_sli4_rb_free;

	/*
	 * Initialize the SLI Layer to run with lpfc SLI4 HBAs.
	 */
	/* Initialize the Abort buffer list used by driver */
	spin_lock_init(&phba->sli4_hba.abts_io_buf_list_lock);
	INIT_LIST_HEAD(&phba->sli4_hba.lpfc_abts_io_buf_list);

	if (phba->cfg_enable_fc4_type & LPFC_ENABLE_NVME) {
		/* Initialize the Abort nvme buffer list used by driver */
		spin_lock_init(&phba->sli4_hba.abts_nvmet_buf_list_lock);
		INIT_LIST_HEAD(&phba->sli4_hba.lpfc_abts_nvmet_ctx_list);
		INIT_LIST_HEAD(&phba->sli4_hba.lpfc_nvmet_io_wait_list);
		spin_lock_init(&phba->sli4_hba.t_active_list_lock);
		INIT_LIST_HEAD(&phba->sli4_hba.t_active_ctx_list);
	}

	/* This abort list used by worker thread */
	spin_lock_init(&phba->sli4_hba.sgl_list_lock);
	spin_lock_init(&phba->sli4_hba.nvmet_io_wait_lock);

	/*
	 * Initialize driver internal slow-path work queues
	 */

	/* Driver internel slow-path CQ Event pool */
	INIT_LIST_HEAD(&phba->sli4_hba.sp_cqe_event_pool);
	/* Response IOCB work queue list */
	INIT_LIST_HEAD(&phba->sli4_hba.sp_queue_event);
	/* Asynchronous event CQ Event work queue list */
	INIT_LIST_HEAD(&phba->sli4_hba.sp_asynce_work_queue);
	/* Fast-path XRI aborted CQ Event work queue list */
	INIT_LIST_HEAD(&phba->sli4_hba.sp_fcp_xri_aborted_work_queue);
	/* Slow-path XRI aborted CQ Event work queue list */
	INIT_LIST_HEAD(&phba->sli4_hba.sp_els_xri_aborted_work_queue);
	/* Receive queue CQ Event work queue list */
	INIT_LIST_HEAD(&phba->sli4_hba.sp_unsol_work_queue);

	/* Initialize extent block lists. */
	INIT_LIST_HEAD(&phba->sli4_hba.lpfc_rpi_blk_list);
	INIT_LIST_HEAD(&phba->sli4_hba.lpfc_xri_blk_list);
	INIT_LIST_HEAD(&phba->sli4_hba.lpfc_vfi_blk_list);
	INIT_LIST_HEAD(&phba->lpfc_vpi_blk_list);

	/* Initialize mboxq lists. If the early init routines fail
	 * these lists need to be correctly initialized.
	 */
	INIT_LIST_HEAD(&phba->sli.mboxq);
	INIT_LIST_HEAD(&phba->sli.mboxq_cmpl);

	/* initialize optic_state to 0xFF */
	phba->sli4_hba.lnk_info.optic_state = 0xff;

	/* Allocate device driver memory */
	rc = lpfc_mem_alloc(phba, SGL_ALIGN_SZ);
	if (rc)
		return -ENOMEM;

	/* IF Type 2 ports get initialized now. */
	if (bf_get(lpfc_sli_intf_if_type, &phba->sli4_hba.sli_intf) >=
	    LPFC_SLI_INTF_IF_TYPE_2) {
		rc = lpfc_pci_function_reset(phba);
		if (unlikely(rc)) {
			rc = -ENODEV;
			goto out_free_mem;
		}
		phba->temp_sensor_support = 1;
	}

	/* Create the bootstrap mailbox command */
	rc = lpfc_create_bootstrap_mbox(phba);
	if (unlikely(rc))
		goto out_free_mem;

	/* Set up the host's endian order with the device. */
	rc = lpfc_setup_endian_order(phba);
	if (unlikely(rc))
		goto out_free_bsmbx;

	/* Set up the hba's configuration parameters. */
	rc = lpfc_sli4_read_config(phba);
	if (unlikely(rc))
		goto out_free_bsmbx;
	rc = lpfc_mem_alloc_active_rrq_pool_s4(phba);
	if (unlikely(rc))
		goto out_free_bsmbx;

	/* IF Type 0 ports get initialized now. */
	if (bf_get(lpfc_sli_intf_if_type, &phba->sli4_hba.sli_intf) ==
	    LPFC_SLI_INTF_IF_TYPE_0) {
		rc = lpfc_pci_function_reset(phba);
		if (unlikely(rc))
			goto out_free_bsmbx;
	}

	mboxq = (LPFC_MBOXQ_t *) mempool_alloc(phba->mbox_mem_pool,
						       GFP_KERNEL);
	if (!mboxq) {
		rc = -ENOMEM;
		goto out_free_bsmbx;
	}

	/* Check for NVMET being configured */
	phba->nvmet_support = 0;
	if (lpfc_enable_nvmet_cnt) {

		/* First get WWN of HBA instance */
		lpfc_read_nv(phba, mboxq);
		rc = lpfc_sli_issue_mbox(phba, mboxq, MBX_POLL);
		if (rc != MBX_SUCCESS) {
			lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
					"6016 Mailbox failed , mbxCmd x%x "
					"READ_NV, mbxStatus x%x\n",
					bf_get(lpfc_mqe_command, &mboxq->u.mqe),
					bf_get(lpfc_mqe_status, &mboxq->u.mqe));
			mempool_free(mboxq, phba->mbox_mem_pool);
			rc = -EIO;
			goto out_free_bsmbx;
		}
		mb = &mboxq->u.mb;
		memcpy(&wwn, (char *)mb->un.varRDnvp.nodename,
		       sizeof(uint64_t));
		wwn = cpu_to_be64(wwn);
		phba->sli4_hba.wwnn.u.name = wwn;
		memcpy(&wwn, (char *)mb->un.varRDnvp.portname,
		       sizeof(uint64_t));
		/* wwn is WWPN of HBA instance */
		wwn = cpu_to_be64(wwn);
		phba->sli4_hba.wwpn.u.name = wwn;

		/* Check to see if it matches any module parameter */
		for (i = 0; i < lpfc_enable_nvmet_cnt; i++) {
			if (wwn == lpfc_enable_nvmet[i]) {
#if (IS_ENABLED(CONFIG_NVME_TARGET_FC))
				if (lpfc_nvmet_mem_alloc(phba))
					break;

				phba->nvmet_support = 1; /* a match */

				lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
						"6017 NVME Target %016llx\n",
						wwn);
#else
				lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
						"6021 Can't enable NVME Target."
						" NVME_TARGET_FC infrastructure"
						" is not in kernel\n");
#endif
				/* Not supported for NVMET */
				phba->cfg_xri_rebalancing = 0;
				break;
			}
		}
	}

	lpfc_nvme_mod_param_dep(phba);

	/* Get the Supported Pages if PORT_CAPABILITIES is supported by port. */
	lpfc_supported_pages(mboxq);
	rc = lpfc_sli_issue_mbox(phba, mboxq, MBX_POLL);
	if (!rc) {
		mqe = &mboxq->u.mqe;
		memcpy(&pn_page[0], ((uint8_t *)&mqe->un.supp_pages.word3),
		       LPFC_MAX_SUPPORTED_PAGES);
		for (i = 0; i < LPFC_MAX_SUPPORTED_PAGES; i++) {
			switch (pn_page[i]) {
			case LPFC_SLI4_PARAMETERS:
				phba->sli4_hba.pc_sli4_params.supported = 1;
				break;
			default:
				break;
			}
		}
		/* Read the port's SLI4 Parameters capabilities if supported. */
		if (phba->sli4_hba.pc_sli4_params.supported)
			rc = lpfc_pc_sli4_params_get(phba, mboxq);
		if (rc) {
			mempool_free(mboxq, phba->mbox_mem_pool);
			rc = -EIO;
			goto out_free_bsmbx;
		}
	}

	/*
	 * Get sli4 parameters that override parameters from Port capabilities.
	 * If this call fails, it isn't critical unless the SLI4 parameters come
	 * back in conflict.
	 */
	rc = lpfc_get_sli4_parameters(phba, mboxq);
	if (rc) {
		if_type = bf_get(lpfc_sli_intf_if_type,
				 &phba->sli4_hba.sli_intf);
		if_fam = bf_get(lpfc_sli_intf_sli_family,
				&phba->sli4_hba.sli_intf);
		if (phba->sli4_hba.extents_in_use &&
		    phba->sli4_hba.rpi_hdrs_in_use) {
			lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"2999 Unsupported SLI4 Parameters "
				"Extents and RPI headers enabled.\n");
			if (if_type == LPFC_SLI_INTF_IF_TYPE_0 &&
			    if_fam ==  LPFC_SLI_INTF_FAMILY_BE2) {
				mempool_free(mboxq, phba->mbox_mem_pool);
				rc = -EIO;
				goto out_free_bsmbx;
			}
		}
		if (!(if_type == LPFC_SLI_INTF_IF_TYPE_0 &&
		      if_fam == LPFC_SLI_INTF_FAMILY_BE2)) {
			mempool_free(mboxq, phba->mbox_mem_pool);
			rc = -EIO;
			goto out_free_bsmbx;
		}
	}

	/*
	 * 1 for cmd, 1 for rsp, NVME adds an extra one
	 * for boundary conditions in its max_sgl_segment template.
	 */
	extra = 2;
	if (phba->cfg_enable_fc4_type & LPFC_ENABLE_NVME)
		extra++;

	/*
	 * It doesn't matter what family our adapter is in, we are
	 * limited to 2 Pages, 512 SGEs, for our SGL.
	 * There are going to be 2 reserved SGEs: 1 FCP cmnd + 1 FCP rsp
	 */
	max_buf_size = (2 * SLI4_PAGE_SIZE);

	/*
	 * Since lpfc_sg_seg_cnt is module param, the sg_dma_buf_size
	 * used to create the sg_dma_buf_pool must be calculated.
	 */
	if (phba->sli3_options & LPFC_SLI3_BG_ENABLED) {
		/* Both cfg_enable_bg and cfg_external_dif code paths */

		/*
		 * The scsi_buf for a T10-DIF I/O holds the FCP cmnd,
		 * the FCP rsp, and a SGE. Sice we have no control
		 * over how many protection segments the SCSI Layer
		 * will hand us (ie: there could be one for every block
		 * in the IO), just allocate enough SGEs to accomidate
		 * our max amount and we need to limit lpfc_sg_seg_cnt
		 * to minimize the risk of running out.
		 */
		phba->cfg_sg_dma_buf_size = sizeof(struct fcp_cmnd) +
				sizeof(struct fcp_rsp) + max_buf_size;

		/* Total SGEs for scsi_sg_list and scsi_sg_prot_list */
		phba->cfg_total_seg_cnt = LPFC_MAX_SGL_SEG_CNT;

		/*
		 * If supporting DIF, reduce the seg count for scsi to
		 * allow room for the DIF sges.
		 */
		if (phba->cfg_enable_bg &&
		    phba->cfg_sg_seg_cnt > LPFC_MAX_BG_SLI4_SEG_CNT_DIF)
			phba->cfg_scsi_seg_cnt = LPFC_MAX_BG_SLI4_SEG_CNT_DIF;
		else
			phba->cfg_scsi_seg_cnt = phba->cfg_sg_seg_cnt;

	} else {
		/*
		 * The scsi_buf for a regular I/O holds the FCP cmnd,
		 * the FCP rsp, a SGE for each, and a SGE for up to
		 * cfg_sg_seg_cnt data segments.
		 */
		phba->cfg_sg_dma_buf_size = sizeof(struct fcp_cmnd) +
				sizeof(struct fcp_rsp) +
				((phba->cfg_sg_seg_cnt + extra) *
				sizeof(struct sli4_sge));

		/* Total SGEs for scsi_sg_list */
		phba->cfg_total_seg_cnt = phba->cfg_sg_seg_cnt + extra;
		phba->cfg_scsi_seg_cnt = phba->cfg_sg_seg_cnt;

		/*
		 * NOTE: if (phba->cfg_sg_seg_cnt + extra) <= 256 we only
		 * need to post 1 page for the SGL.
		 */
	}

	if (phba->cfg_xpsgl && !phba->nvmet_support)
		phba->cfg_sg_dma_buf_size = LPFC_DEFAULT_XPSGL_SIZE;
	else if (phba->cfg_sg_dma_buf_size  <= LPFC_MIN_SG_SLI4_BUF_SZ)
		phba->cfg_sg_dma_buf_size = LPFC_MIN_SG_SLI4_BUF_SZ;
	else
		phba->cfg_sg_dma_buf_size =
				SLI4_PAGE_ALIGN(phba->cfg_sg_dma_buf_size);

	phba->border_sge_num = phba->cfg_sg_dma_buf_size /
			       sizeof(struct sli4_sge);

	/* Limit to LPFC_MAX_NVME_SEG_CNT for NVME. */
	if (phba->cfg_enable_fc4_type & LPFC_ENABLE_NVME) {
		if (phba->cfg_sg_seg_cnt > LPFC_MAX_NVME_SEG_CNT) {
			lpfc_printf_log(phba, KERN_INFO, LOG_NVME | LOG_INIT,
					"6300 Reducing NVME sg segment "
					"cnt to %d\n",
					LPFC_MAX_NVME_SEG_CNT);
			phba->cfg_nvme_seg_cnt = LPFC_MAX_NVME_SEG_CNT;
		} else
			phba->cfg_nvme_seg_cnt = phba->cfg_sg_seg_cnt;
	}

	/* Initialize the host templates with the updated values. */
	lpfc_vport_template.sg_tablesize = phba->cfg_scsi_seg_cnt;
	lpfc_template.sg_tablesize = phba->cfg_scsi_seg_cnt;
	lpfc_template_no_hr.sg_tablesize = phba->cfg_scsi_seg_cnt;

	lpfc_printf_log(phba, KERN_INFO, LOG_INIT | LOG_FCP,
			"9087 sg_seg_cnt:%d dmabuf_size:%d "
			"total:%d scsi:%d nvme:%d\n",
			phba->cfg_sg_seg_cnt, phba->cfg_sg_dma_buf_size,
			phba->cfg_total_seg_cnt,  phba->cfg_scsi_seg_cnt,
			phba->cfg_nvme_seg_cnt);

	if (phba->cfg_sg_dma_buf_size < SLI4_PAGE_SIZE)
		i = phba->cfg_sg_dma_buf_size;
	else
		i = SLI4_PAGE_SIZE;

	phba->lpfc_sg_dma_buf_pool =
			dma_pool_create("lpfc_sg_dma_buf_pool",
					&phba->pcidev->dev,
					phba->cfg_sg_dma_buf_size,
					i, 0);
	if (!phba->lpfc_sg_dma_buf_pool)
		goto out_free_bsmbx;

	phba->lpfc_cmd_rsp_buf_pool =
			dma_pool_create("lpfc_cmd_rsp_buf_pool",
					&phba->pcidev->dev,
					sizeof(struct fcp_cmnd) +
					sizeof(struct fcp_rsp),
					i, 0);
	if (!phba->lpfc_cmd_rsp_buf_pool)
		goto out_free_sg_dma_buf;

	mempool_free(mboxq, phba->mbox_mem_pool);

	/* Verify OAS is supported */
	lpfc_sli4_oas_verify(phba);

	/* Verify RAS support on adapter */
	lpfc_sli4_ras_init(phba);

	/* Verify all the SLI4 queues */
	rc = lpfc_sli4_queue_verify(phba);
	if (rc)
		goto out_free_cmd_rsp_buf;

	/* Create driver internal CQE event pool */
	rc = lpfc_sli4_cq_event_pool_create(phba);
	if (rc)
		goto out_free_cmd_rsp_buf;

	/* Initialize sgl lists per host */
	lpfc_init_sgl_list(phba);

	/* Allocate and initialize active sgl array */
	rc = lpfc_init_active_sgl_array(phba);
	if (rc) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"1430 Failed to initialize sgl list.\n");
		goto out_destroy_cq_event_pool;
	}
	rc = lpfc_sli4_init_rpi_hdrs(phba);
	if (rc) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"1432 Failed to initialize rpi headers.\n");
		goto out_free_active_sgl;
	}

	/* Allocate eligible FCF bmask memory for FCF roundrobin failover */
	longs = (LPFC_SLI4_FCF_TBL_INDX_MAX + BITS_PER_LONG - 1)/BITS_PER_LONG;
	phba->fcf.fcf_rr_bmask = kcalloc(longs, sizeof(unsigned long),
					 GFP_KERNEL);
	if (!phba->fcf.fcf_rr_bmask) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"2759 Failed allocate memory for FCF round "
				"robin failover bmask\n");
		rc = -ENOMEM;
		goto out_remove_rpi_hdrs;
	}

	phba->sli4_hba.hba_eq_hdl = kcalloc(phba->cfg_irq_chann,
					    sizeof(struct lpfc_hba_eq_hdl),
					    GFP_KERNEL);
	if (!phba->sli4_hba.hba_eq_hdl) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"2572 Failed allocate memory for "
				"fast-path per-EQ handle array\n");
		rc = -ENOMEM;
		goto out_free_fcf_rr_bmask;
	}

	phba->sli4_hba.cpu_map = kcalloc(phba->sli4_hba.num_possible_cpu,
					sizeof(struct lpfc_vector_map_info),
					GFP_KERNEL);
	if (!phba->sli4_hba.cpu_map) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"3327 Failed allocate memory for msi-x "
				"interrupt vector mapping\n");
		rc = -ENOMEM;
		goto out_free_hba_eq_hdl;
	}

	phba->sli4_hba.eq_info = alloc_percpu(struct lpfc_eq_intr_info);
	if (!phba->sli4_hba.eq_info) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"3321 Failed allocation for per_cpu stats\n");
		rc = -ENOMEM;
		goto out_free_hba_cpu_map;
	}
	/*
	 * Enable sr-iov virtual functions if supported and configured
	 * through the module parameter.
	 */
	if (phba->cfg_sriov_nr_virtfn > 0) {
		rc = lpfc_sli_probe_sriov_nr_virtfn(phba,
						 phba->cfg_sriov_nr_virtfn);
		if (rc) {
			lpfc_printf_log(phba, KERN_WARNING, LOG_INIT,
					"3020 Requested number of SR-IOV "
					"virtual functions (%d) is not "
					"supported\n",
					phba->cfg_sriov_nr_virtfn);
			phba->cfg_sriov_nr_virtfn = 0;
		}
	}

	return 0;

out_free_hba_cpu_map:
	kfree(phba->sli4_hba.cpu_map);
out_free_hba_eq_hdl:
	kfree(phba->sli4_hba.hba_eq_hdl);
out_free_fcf_rr_bmask:
	kfree(phba->fcf.fcf_rr_bmask);
out_remove_rpi_hdrs:
	lpfc_sli4_remove_rpi_hdrs(phba);
out_free_active_sgl:
	lpfc_free_active_sgl(phba);
out_destroy_cq_event_pool:
	lpfc_sli4_cq_event_pool_destroy(phba);
out_free_cmd_rsp_buf:
	dma_pool_destroy(phba->lpfc_cmd_rsp_buf_pool);
	phba->lpfc_cmd_rsp_buf_pool = NULL;
out_free_sg_dma_buf:
	dma_pool_destroy(phba->lpfc_sg_dma_buf_pool);
	phba->lpfc_sg_dma_buf_pool = NULL;
out_free_bsmbx:
	lpfc_destroy_bootstrap_mbox(phba);
out_free_mem:
	lpfc_mem_free(phba);
	return rc;
}

/**
 * lpfc_sli4_driver_resource_unset - Unset drvr internal resources for SLI4 dev
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to unset the driver internal resources set up
 * specific for supporting the SLI-4 HBA device it attached to.
 **/
static void
lpfc_sli4_driver_resource_unset(struct lpfc_hba *phba)
{
	struct lpfc_fcf_conn_entry *conn_entry, *next_conn_entry;

	free_percpu(phba->sli4_hba.eq_info);

	/* Free memory allocated for msi-x interrupt vector to CPU mapping */
	kfree(phba->sli4_hba.cpu_map);
	phba->sli4_hba.num_possible_cpu = 0;
	phba->sli4_hba.num_present_cpu = 0;
	phba->sli4_hba.curr_disp_cpu = 0;
	cpumask_clear(&phba->sli4_hba.numa_mask);

	/* Free memory allocated for fast-path work queue handles */
	kfree(phba->sli4_hba.hba_eq_hdl);

	/* Free the allocated rpi headers. */
	lpfc_sli4_remove_rpi_hdrs(phba);
	lpfc_sli4_remove_rpis(phba);

	/* Free eligible FCF index bmask */
	kfree(phba->fcf.fcf_rr_bmask);

	/* Free the ELS sgl list */
	lpfc_free_active_sgl(phba);
	lpfc_free_els_sgl_list(phba);
	lpfc_free_nvmet_sgl_list(phba);

	/* Free the completion queue EQ event pool */
	lpfc_sli4_cq_event_release_all(phba);
	lpfc_sli4_cq_event_pool_destroy(phba);

	/* Release resource identifiers. */
	lpfc_sli4_dealloc_resource_identifiers(phba);

	/* Free the bsmbx region. */
	lpfc_destroy_bootstrap_mbox(phba);

	/* Free the SLI Layer memory with SLI4 HBAs */
	lpfc_mem_free_all(phba);

	/* Free the current connect table */
	list_for_each_entry_safe(conn_entry, next_conn_entry,
		&phba->fcf_conn_rec_list, list) {
		list_del_init(&conn_entry->list);
		kfree(conn_entry);
	}

	return;
}

/**
 * lpfc_init_api_table_setup - Set up init api function jump table
 * @phba: The hba struct for which this call is being executed.
 * @dev_grp: The HBA PCI-Device group number.
 *
 * This routine sets up the device INIT interface API function jump table
 * in @phba struct.
 *
 * Returns: 0 - success, -ENODEV - failure.
 **/
int
lpfc_init_api_table_setup(struct lpfc_hba *phba, uint8_t dev_grp)
{
	phba->lpfc_hba_init_link = lpfc_hba_init_link;
	phba->lpfc_hba_down_link = lpfc_hba_down_link;
	phba->lpfc_selective_reset = lpfc_selective_reset;
	switch (dev_grp) {
	case LPFC_PCI_DEV_LP:
		phba->lpfc_hba_down_post = lpfc_hba_down_post_s3;
		phba->lpfc_handle_eratt = lpfc_handle_eratt_s3;
		phba->lpfc_stop_port = lpfc_stop_port_s3;
		break;
	case LPFC_PCI_DEV_OC:
		phba->lpfc_hba_down_post = lpfc_hba_down_post_s4;
		phba->lpfc_handle_eratt = lpfc_handle_eratt_s4;
		phba->lpfc_stop_port = lpfc_stop_port_s4;
		break;
	default:
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"1431 Invalid HBA PCI-device group: 0x%x\n",
				dev_grp);
		return -ENODEV;
		break;
	}
	return 0;
}

/**
 * lpfc_setup_driver_resource_phase2 - Phase2 setup driver internal resources.
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to set up the driver internal resources after the
 * device specific resource setup to support the HBA device it attached to.
 *
 * Return codes
 * 	0 - successful
 * 	other values - error
 **/
static int
lpfc_setup_driver_resource_phase2(struct lpfc_hba *phba)
{
	int error;

	/* Startup the kernel thread for this host adapter. */
	phba->worker_thread = kthread_run(lpfc_do_work, phba,
					  "lpfc_worker_%d", phba->brd_no);
	if (IS_ERR(phba->worker_thread)) {
		error = PTR_ERR(phba->worker_thread);
		return error;
	}

	return 0;
}

/**
 * lpfc_unset_driver_resource_phase2 - Phase2 unset driver internal resources.
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to unset the driver internal resources set up after
 * the device specific resource setup for supporting the HBA device it
 * attached to.
 **/
static void
lpfc_unset_driver_resource_phase2(struct lpfc_hba *phba)
{
	if (phba->wq) {
		flush_workqueue(phba->wq);
		destroy_workqueue(phba->wq);
		phba->wq = NULL;
	}

	/* Stop kernel worker thread */
	if (phba->worker_thread)
		kthread_stop(phba->worker_thread);
}

/**
 * lpfc_free_iocb_list - Free iocb list.
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to free the driver's IOCB list and memory.
 **/
void
lpfc_free_iocb_list(struct lpfc_hba *phba)
{
	struct lpfc_iocbq *iocbq_entry = NULL, *iocbq_next = NULL;

	spin_lock_irq(&phba->hbalock);
	list_for_each_entry_safe(iocbq_entry, iocbq_next,
				 &phba->lpfc_iocb_list, list) {
		list_del(&iocbq_entry->list);
		kfree(iocbq_entry);
		phba->total_iocbq_bufs--;
	}
	spin_unlock_irq(&phba->hbalock);

	return;
}

/**
 * lpfc_init_iocb_list - Allocate and initialize iocb list.
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to allocate and initizlize the driver's IOCB
 * list and set up the IOCB tag array accordingly.
 *
 * Return codes
 *	0 - successful
 *	other values - error
 **/
int
lpfc_init_iocb_list(struct lpfc_hba *phba, int iocb_count)
{
	struct lpfc_iocbq *iocbq_entry = NULL;
	uint16_t iotag;
	int i;

	/* Initialize and populate the iocb list per host.  */
	INIT_LIST_HEAD(&phba->lpfc_iocb_list);
	for (i = 0; i < iocb_count; i++) {
		iocbq_entry = kzalloc(sizeof(struct lpfc_iocbq), GFP_KERNEL);
		if (iocbq_entry == NULL) {
			printk(KERN_ERR "%s: only allocated %d iocbs of "
				"expected %d count. Unloading driver.\n",
				__func__, i, iocb_count);
			goto out_free_iocbq;
		}

		iotag = lpfc_sli_next_iotag(phba, iocbq_entry);
		if (iotag == 0) {
			kfree(iocbq_entry);
			printk(KERN_ERR "%s: failed to allocate IOTAG. "
				"Unloading driver.\n", __func__);
			goto out_free_iocbq;
		}
		iocbq_entry->sli4_lxritag = NO_XRI;
		iocbq_entry->sli4_xritag = NO_XRI;

		spin_lock_irq(&phba->hbalock);
		list_add(&iocbq_entry->list, &phba->lpfc_iocb_list);
		phba->total_iocbq_bufs++;
		spin_unlock_irq(&phba->hbalock);
	}

	return 0;

out_free_iocbq:
	lpfc_free_iocb_list(phba);

	return -ENOMEM;
}

/**
 * lpfc_free_sgl_list - Free a given sgl list.
 * @phba: pointer to lpfc hba data structure.
 * @sglq_list: pointer to the head of sgl list.
 *
 * This routine is invoked to free a give sgl list and memory.
 **/
void
lpfc_free_sgl_list(struct lpfc_hba *phba, struct list_head *sglq_list)
{
	struct lpfc_sglq *sglq_entry = NULL, *sglq_next = NULL;

	list_for_each_entry_safe(sglq_entry, sglq_next, sglq_list, list) {
		list_del(&sglq_entry->list);
		lpfc_mbuf_free(phba, sglq_entry->virt, sglq_entry->phys);
		kfree(sglq_entry);
	}
}

/**
 * lpfc_free_els_sgl_list - Free els sgl list.
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to free the driver's els sgl list and memory.
 **/
static void
lpfc_free_els_sgl_list(struct lpfc_hba *phba)
{
	LIST_HEAD(sglq_list);

	/* Retrieve all els sgls from driver list */
	spin_lock_irq(&phba->hbalock);
	spin_lock(&phba->sli4_hba.sgl_list_lock);
	list_splice_init(&phba->sli4_hba.lpfc_els_sgl_list, &sglq_list);
	spin_unlock(&phba->sli4_hba.sgl_list_lock);
	spin_unlock_irq(&phba->hbalock);

	/* Now free the sgl list */
	lpfc_free_sgl_list(phba, &sglq_list);
}

/**
 * lpfc_free_nvmet_sgl_list - Free nvmet sgl list.
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to free the driver's nvmet sgl list and memory.
 **/
static void
lpfc_free_nvmet_sgl_list(struct lpfc_hba *phba)
{
	struct lpfc_sglq *sglq_entry = NULL, *sglq_next = NULL;
	LIST_HEAD(sglq_list);

	/* Retrieve all nvmet sgls from driver list */
	spin_lock_irq(&phba->hbalock);
	spin_lock(&phba->sli4_hba.sgl_list_lock);
	list_splice_init(&phba->sli4_hba.lpfc_nvmet_sgl_list, &sglq_list);
	spin_unlock(&phba->sli4_hba.sgl_list_lock);
	spin_unlock_irq(&phba->hbalock);

	/* Now free the sgl list */
	list_for_each_entry_safe(sglq_entry, sglq_next, &sglq_list, list) {
		list_del(&sglq_entry->list);
		lpfc_nvmet_buf_free(phba, sglq_entry->virt, sglq_entry->phys);
		kfree(sglq_entry);
	}

	/* Update the nvmet_xri_cnt to reflect no current sgls.
	 * The next initialization cycle sets the count and allocates
	 * the sgls over again.
	 */
	phba->sli4_hba.nvmet_xri_cnt = 0;
}

/**
 * lpfc_init_active_sgl_array - Allocate the buf to track active ELS XRIs.
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to allocate the driver's active sgl memory.
 * This array will hold the sglq_entry's for active IOs.
 **/
static int
lpfc_init_active_sgl_array(struct lpfc_hba *phba)
{
	int size;
	size = sizeof(struct lpfc_sglq *);
	size *= phba->sli4_hba.max_cfg_param.max_xri;

	phba->sli4_hba.lpfc_sglq_active_list =
		kzalloc(size, GFP_KERNEL);
	if (!phba->sli4_hba.lpfc_sglq_active_list)
		return -ENOMEM;
	return 0;
}

/**
 * lpfc_free_active_sgl - Free the buf that tracks active ELS XRIs.
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to walk through the array of active sglq entries
 * and free all of the resources.
 * This is just a place holder for now.
 **/
static void
lpfc_free_active_sgl(struct lpfc_hba *phba)
{
	kfree(phba->sli4_hba.lpfc_sglq_active_list);
}

/**
 * lpfc_init_sgl_list - Allocate and initialize sgl list.
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to allocate and initizlize the driver's sgl
 * list and set up the sgl xritag tag array accordingly.
 *
 **/
static void
lpfc_init_sgl_list(struct lpfc_hba *phba)
{
	/* Initialize and populate the sglq list per host/VF. */
	INIT_LIST_HEAD(&phba->sli4_hba.lpfc_els_sgl_list);
	INIT_LIST_HEAD(&phba->sli4_hba.lpfc_abts_els_sgl_list);
	INIT_LIST_HEAD(&phba->sli4_hba.lpfc_nvmet_sgl_list);
	INIT_LIST_HEAD(&phba->sli4_hba.lpfc_abts_nvmet_ctx_list);

	/* els xri-sgl book keeping */
	phba->sli4_hba.els_xri_cnt = 0;

	/* nvme xri-buffer book keeping */
	phba->sli4_hba.io_xri_cnt = 0;
}

/**
 * lpfc_sli4_init_rpi_hdrs - Post the rpi header memory region to the port
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to post rpi header templates to the
 * port for those SLI4 ports that do not support extents.  This routine
 * posts a PAGE_SIZE memory region to the port to hold up to
 * PAGE_SIZE modulo 64 rpi context headers.  This is an initialization routine
 * and should be called only when interrupts are disabled.
 *
 * Return codes
 * 	0 - successful
 *	-ERROR - otherwise.
 **/
int
lpfc_sli4_init_rpi_hdrs(struct lpfc_hba *phba)
{
	int rc = 0;
	struct lpfc_rpi_hdr *rpi_hdr;

	INIT_LIST_HEAD(&phba->sli4_hba.lpfc_rpi_hdr_list);
	if (!phba->sli4_hba.rpi_hdrs_in_use)
		return rc;
	if (phba->sli4_hba.extents_in_use)
		return -EIO;

	rpi_hdr = lpfc_sli4_create_rpi_hdr(phba);
	if (!rpi_hdr) {
		lpfc_printf_log(phba, KERN_ERR, LOG_MBOX | LOG_SLI,
				"0391 Error during rpi post operation\n");
		lpfc_sli4_remove_rpis(phba);
		rc = -ENODEV;
	}

	return rc;
}

/**
 * lpfc_sli4_create_rpi_hdr - Allocate an rpi header memory region
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to allocate a single 4KB memory region to
 * support rpis and stores them in the phba.  This single region
 * provides support for up to 64 rpis.  The region is used globally
 * by the device.
 *
 * Returns:
 *   A valid rpi hdr on success.
 *   A NULL pointer on any failure.
 **/
struct lpfc_rpi_hdr *
lpfc_sli4_create_rpi_hdr(struct lpfc_hba *phba)
{
	uint16_t rpi_limit, curr_rpi_range;
	struct lpfc_dmabuf *dmabuf;
	struct lpfc_rpi_hdr *rpi_hdr;

	/*
	 * If the SLI4 port supports extents, posting the rpi header isn't
	 * required.  Set the expected maximum count and let the actual value
	 * get set when extents are fully allocated.
	 */
	if (!phba->sli4_hba.rpi_hdrs_in_use)
		return NULL;
	if (phba->sli4_hba.extents_in_use)
		return NULL;

	/* The limit on the logical index is just the max_rpi count. */
	rpi_limit = phba->sli4_hba.max_cfg_param.max_rpi;

	spin_lock_irq(&phba->hbalock);
	/*
	 * Establish the starting RPI in this header block.  The starting
	 * rpi is normalized to a zero base because the physical rpi is
	 * port based.
	 */
	curr_rpi_range = phba->sli4_hba.next_rpi;
	spin_unlock_irq(&phba->hbalock);

	/* Reached full RPI range */
	if (curr_rpi_range == rpi_limit)
		return NULL;

	/*
	 * First allocate the protocol header region for the port.  The
	 * port expects a 4KB DMA-mapped memory region that is 4K aligned.
	 */
	dmabuf = kzalloc(sizeof(struct lpfc_dmabuf), GFP_KERNEL);
	if (!dmabuf)
		return NULL;

	dmabuf->virt = dma_alloc_coherent(&phba->pcidev->dev,
					  LPFC_HDR_TEMPLATE_SIZE,
					  &dmabuf->phys, GFP_KERNEL);
	if (!dmabuf->virt) {
		rpi_hdr = NULL;
		goto err_free_dmabuf;
	}

	if (!IS_ALIGNED(dmabuf->phys, LPFC_HDR_TEMPLATE_SIZE)) {
		rpi_hdr = NULL;
		goto err_free_coherent;
	}

	/* Save the rpi header data for cleanup later. */
	rpi_hdr = kzalloc(sizeof(struct lpfc_rpi_hdr), GFP_KERNEL);
	if (!rpi_hdr)
		goto err_free_coherent;

	rpi_hdr->dmabuf = dmabuf;
	rpi_hdr->len = LPFC_HDR_TEMPLATE_SIZE;
	rpi_hdr->page_count = 1;
	spin_lock_irq(&phba->hbalock);

	/* The rpi_hdr stores the logical index only. */
	rpi_hdr->start_rpi = curr_rpi_range;
	rpi_hdr->next_rpi = phba->sli4_hba.next_rpi + LPFC_RPI_HDR_COUNT;
	list_add_tail(&rpi_hdr->list, &phba->sli4_hba.lpfc_rpi_hdr_list);

	spin_unlock_irq(&phba->hbalock);
	return rpi_hdr;

 err_free_coherent:
	dma_free_coherent(&phba->pcidev->dev, LPFC_HDR_TEMPLATE_SIZE,
			  dmabuf->virt, dmabuf->phys);
 err_free_dmabuf:
	kfree(dmabuf);
	return NULL;
}

/**
 * lpfc_sli4_remove_rpi_hdrs - Remove all rpi header memory regions
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to remove all memory resources allocated
 * to support rpis for SLI4 ports not supporting extents. This routine
 * presumes the caller has released all rpis consumed by fabric or port
 * logins and is prepared to have the header pages removed.
 **/
void
lpfc_sli4_remove_rpi_hdrs(struct lpfc_hba *phba)
{
	struct lpfc_rpi_hdr *rpi_hdr, *next_rpi_hdr;

	if (!phba->sli4_hba.rpi_hdrs_in_use)
		goto exit;

	list_for_each_entry_safe(rpi_hdr, next_rpi_hdr,
				 &phba->sli4_hba.lpfc_rpi_hdr_list, list) {
		list_del(&rpi_hdr->list);
		dma_free_coherent(&phba->pcidev->dev, rpi_hdr->len,
				  rpi_hdr->dmabuf->virt, rpi_hdr->dmabuf->phys);
		kfree(rpi_hdr->dmabuf);
		kfree(rpi_hdr);
	}
 exit:
	/* There are no rpis available to the port now. */
	phba->sli4_hba.next_rpi = 0;
}

/**
 * lpfc_hba_alloc - Allocate driver hba data structure for a device.
 * @pdev: pointer to pci device data structure.
 *
 * This routine is invoked to allocate the driver hba data structure for an
 * HBA device. If the allocation is successful, the phba reference to the
 * PCI device data structure is set.
 *
 * Return codes
 *      pointer to @phba - successful
 *      NULL - error
 **/
static struct lpfc_hba *
lpfc_hba_alloc(struct pci_dev *pdev)
{
	struct lpfc_hba *phba;

	/* Allocate memory for HBA structure */
	phba = kzalloc(sizeof(struct lpfc_hba), GFP_KERNEL);
	if (!phba) {
		dev_err(&pdev->dev, "failed to allocate hba struct\n");
		return NULL;
	}

	/* Set reference to PCI device in HBA structure */
	phba->pcidev = pdev;

	/* Assign an unused board number */
	phba->brd_no = lpfc_get_instance();
	if (phba->brd_no < 0) {
		kfree(phba);
		return NULL;
	}
	phba->eratt_poll_interval = LPFC_ERATT_POLL_INTERVAL;

	spin_lock_init(&phba->ct_ev_lock);
	INIT_LIST_HEAD(&phba->ct_ev_waiters);

	return phba;
}

/**
 * lpfc_hba_free - Free driver hba data structure with a device.
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to free the driver hba data structure with an
 * HBA device.
 **/
static void
lpfc_hba_free(struct lpfc_hba *phba)
{
	if (phba->sli_rev == LPFC_SLI_REV4)
		kfree(phba->sli4_hba.hdwq);

	/* Release the driver assigned board number */
	idr_remove(&lpfc_hba_index, phba->brd_no);

	/* Free memory allocated with sli3 rings */
	kfree(phba->sli.sli3_ring);
	phba->sli.sli3_ring = NULL;

	kfree(phba);
	return;
}

/**
 * lpfc_create_shost - Create hba physical port with associated scsi host.
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to create HBA physical port and associate a SCSI
 * host with it.
 *
 * Return codes
 *      0 - successful
 *      other values - error
 **/
static int
lpfc_create_shost(struct lpfc_hba *phba)
{
	struct lpfc_vport *vport;
	struct Scsi_Host  *shost;

	/* Initialize HBA FC structure */
	phba->fc_edtov = FF_DEF_EDTOV;
	phba->fc_ratov = FF_DEF_RATOV;
	phba->fc_altov = FF_DEF_ALTOV;
	phba->fc_arbtov = FF_DEF_ARBTOV;

	atomic_set(&phba->sdev_cnt, 0);
	vport = lpfc_create_port(phba, phba->brd_no, &phba->pcidev->dev);
	if (!vport)
		return -ENODEV;

	shost = lpfc_shost_from_vport(vport);
	phba->pport = vport;

	if (phba->nvmet_support) {
		/* Only 1 vport (pport) will support NVME target */
		phba->targetport = NULL;
		phba->cfg_enable_fc4_type = LPFC_ENABLE_NVME;
		lpfc_printf_log(phba, KERN_INFO, LOG_INIT | LOG_NVME_DISC,
				"6076 NVME Target Found\n");
	}

	lpfc_debugfs_initialize(vport);
	/* Put reference to SCSI host to driver's device private data */
	pci_set_drvdata(phba->pcidev, shost);

	/*
	 * At this point we are fully registered with PSA. In addition,
	 * any initial discovery should be completed.
	 */
	vport->load_flag |= FC_ALLOW_FDMI;
	if (phba->cfg_enable_SmartSAN ||
	    (phba->cfg_fdmi_on == LPFC_FDMI_SUPPORT)) {

		/* Setup appropriate attribute masks */
		vport->fdmi_hba_mask = LPFC_FDMI2_HBA_ATTR;
		if (phba->cfg_enable_SmartSAN)
			vport->fdmi_port_mask = LPFC_FDMI2_SMART_ATTR;
		else
			vport->fdmi_port_mask = LPFC_FDMI2_PORT_ATTR;
	}
	return 0;
}

/**
 * lpfc_destroy_shost - Destroy hba physical port with associated scsi host.
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to destroy HBA physical port and the associated
 * SCSI host.
 **/
static void
lpfc_destroy_shost(struct lpfc_hba *phba)
{
	struct lpfc_vport *vport = phba->pport;

	/* Destroy physical port that associated with the SCSI host */
	destroy_port(vport);

	return;
}

/**
 * lpfc_setup_bg - Setup Block guard structures and debug areas.
 * @phba: pointer to lpfc hba data structure.
 * @shost: the shost to be used to detect Block guard settings.
 *
 * This routine sets up the local Block guard protocol settings for @shost.
 * This routine also allocates memory for debugging bg buffers.
 **/
static void
lpfc_setup_bg(struct lpfc_hba *phba, struct Scsi_Host *shost)
{
	uint32_t old_mask;
	uint32_t old_guard;

	if (phba->cfg_prot_mask && phba->cfg_prot_guard) {
		lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
				"1478 Registering BlockGuard with the "
				"SCSI layer\n");

		old_mask = phba->cfg_prot_mask;
		old_guard = phba->cfg_prot_guard;

		/* Only allow supported values */
		phba->cfg_prot_mask &= (SHOST_DIF_TYPE1_PROTECTION |
			SHOST_DIX_TYPE0_PROTECTION |
			SHOST_DIX_TYPE1_PROTECTION);
		phba->cfg_prot_guard &= (SHOST_DIX_GUARD_IP |
					 SHOST_DIX_GUARD_CRC);

		/* DIF Type 1 protection for profiles AST1/C1 is end to end */
		if (phba->cfg_prot_mask == SHOST_DIX_TYPE1_PROTECTION)
			phba->cfg_prot_mask |= SHOST_DIF_TYPE1_PROTECTION;

		if (phba->cfg_prot_mask && phba->cfg_prot_guard) {
			if ((old_mask != phba->cfg_prot_mask) ||
				(old_guard != phba->cfg_prot_guard))
				lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
					"1475 Registering BlockGuard with the "
					"SCSI layer: mask %d  guard %d\n",
					phba->cfg_prot_mask,
					phba->cfg_prot_guard);

			scsi_host_set_prot(shost, phba->cfg_prot_mask);
			scsi_host_set_guard(shost, phba->cfg_prot_guard);
		} else
			lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"1479 Not Registering BlockGuard with the SCSI "
				"layer, Bad protection parameters: %d %d\n",
				old_mask, old_guard);
	}
}

/**
 * lpfc_post_init_setup - Perform necessary device post initialization setup.
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to perform all the necessary post initialization
 * setup for the device.
 **/
static void
lpfc_post_init_setup(struct lpfc_hba *phba)
{
	struct Scsi_Host  *shost;
	struct lpfc_adapter_event_header adapter_event;

	/* Get the default values for Model Name and Description */
	lpfc_get_hba_model_desc(phba, phba->ModelName, phba->ModelDesc);

	/*
	 * hba setup may have changed the hba_queue_depth so we need to
	 * adjust the value of can_queue.
	 */
	shost = pci_get_drvdata(phba->pcidev);
	shost->can_queue = phba->cfg_hba_queue_depth - 10;

	lpfc_host_attrib_init(shost);

	if (phba->cfg_poll & DISABLE_FCP_RING_INT) {
		spin_lock_irq(shost->host_lock);
		lpfc_poll_start_timer(phba);
		spin_unlock_irq(shost->host_lock);
	}

	lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
			"0428 Perform SCSI scan\n");
	/* Send board arrival event to upper layer */
	adapter_event.event_type = FC_REG_ADAPTER_EVENT;
	adapter_event.subcategory = LPFC_EVENT_ARRIVAL;
	fc_host_post_vendor_event(shost, fc_get_event_number(),
				  sizeof(adapter_event),
				  (char *) &adapter_event,
				  LPFC_NL_VENDOR_ID);
	return;
}

/**
 * lpfc_sli_pci_mem_setup - Setup SLI3 HBA PCI memory space.
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to set up the PCI device memory space for device
 * with SLI-3 interface spec.
 *
 * Return codes
 * 	0 - successful
 * 	other values - error
 **/
static int
lpfc_sli_pci_mem_setup(struct lpfc_hba *phba)
{
	struct pci_dev *pdev = phba->pcidev;
	unsigned long bar0map_len, bar2map_len;
	int i, hbq_count;
	void *ptr;
	int error;

	if (!pdev)
		return -ENODEV;

	/* Set the device DMA mask size */
	error = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (error)
		error = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (error)
		return error;
	error = -ENODEV;

	/* Get the bus address of Bar0 and Bar2 and the number of bytes
	 * required by each mapping.
	 */
	phba->pci_bar0_map = pci_resource_start(pdev, 0);
	bar0map_len = pci_resource_len(pdev, 0);

	phba->pci_bar2_map = pci_resource_start(pdev, 2);
	bar2map_len = pci_resource_len(pdev, 2);

	/* Map HBA SLIM to a kernel virtual address. */
	phba->slim_memmap_p = ioremap(phba->pci_bar0_map, bar0map_len);
	if (!phba->slim_memmap_p) {
		dev_printk(KERN_ERR, &pdev->dev,
			   "ioremap failed for SLIM memory.\n");
		goto out;
	}

	/* Map HBA Control Registers to a kernel virtual address. */
	phba->ctrl_regs_memmap_p = ioremap(phba->pci_bar2_map, bar2map_len);
	if (!phba->ctrl_regs_memmap_p) {
		dev_printk(KERN_ERR, &pdev->dev,
			   "ioremap failed for HBA control registers.\n");
		goto out_iounmap_slim;
	}

	/* Allocate memory for SLI-2 structures */
	phba->slim2p.virt = dma_alloc_coherent(&pdev->dev, SLI2_SLIM_SIZE,
					       &phba->slim2p.phys, GFP_KERNEL);
	if (!phba->slim2p.virt)
		goto out_iounmap;

	phba->mbox = phba->slim2p.virt + offsetof(struct lpfc_sli2_slim, mbx);
	phba->mbox_ext = (phba->slim2p.virt +
		offsetof(struct lpfc_sli2_slim, mbx_ext_words));
	phba->pcb = (phba->slim2p.virt + offsetof(struct lpfc_sli2_slim, pcb));
	phba->IOCBs = (phba->slim2p.virt +
		       offsetof(struct lpfc_sli2_slim, IOCBs));

	phba->hbqslimp.virt = dma_alloc_coherent(&pdev->dev,
						 lpfc_sli_hbq_size(),
						 &phba->hbqslimp.phys,
						 GFP_KERNEL);
	if (!phba->hbqslimp.virt)
		goto out_free_slim;

	hbq_count = lpfc_sli_hbq_count();
	ptr = phba->hbqslimp.virt;
	for (i = 0; i < hbq_count; ++i) {
		phba->hbqs[i].hbq_virt = ptr;
		INIT_LIST_HEAD(&phba->hbqs[i].hbq_buffer_list);
		ptr += (lpfc_hbq_defs[i]->entry_count *
			sizeof(struct lpfc_hbq_entry));
	}
	phba->hbqs[LPFC_ELS_HBQ].hbq_alloc_buffer = lpfc_els_hbq_alloc;
	phba->hbqs[LPFC_ELS_HBQ].hbq_free_buffer = lpfc_els_hbq_free;

	memset(phba->hbqslimp.virt, 0, lpfc_sli_hbq_size());

	phba->MBslimaddr = phba->slim_memmap_p;
	phba->HAregaddr = phba->ctrl_regs_memmap_p + HA_REG_OFFSET;
	phba->CAregaddr = phba->ctrl_regs_memmap_p + CA_REG_OFFSET;
	phba->HSregaddr = phba->ctrl_regs_memmap_p + HS_REG_OFFSET;
	phba->HCregaddr = phba->ctrl_regs_memmap_p + HC_REG_OFFSET;

	return 0;

out_free_slim:
	dma_free_coherent(&pdev->dev, SLI2_SLIM_SIZE,
			  phba->slim2p.virt, phba->slim2p.phys);
out_iounmap:
	iounmap(phba->ctrl_regs_memmap_p);
out_iounmap_slim:
	iounmap(phba->slim_memmap_p);
out:
	return error;
}

/**
 * lpfc_sli_pci_mem_unset - Unset SLI3 HBA PCI memory space.
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to unset the PCI device memory space for device
 * with SLI-3 interface spec.
 **/
static void
lpfc_sli_pci_mem_unset(struct lpfc_hba *phba)
{
	struct pci_dev *pdev;

	/* Obtain PCI device reference */
	if (!phba->pcidev)
		return;
	else
		pdev = phba->pcidev;

	/* Free coherent DMA memory allocated */
	dma_free_coherent(&pdev->dev, lpfc_sli_hbq_size(),
			  phba->hbqslimp.virt, phba->hbqslimp.phys);
	dma_free_coherent(&pdev->dev, SLI2_SLIM_SIZE,
			  phba->slim2p.virt, phba->slim2p.phys);

	/* I/O memory unmap */
	iounmap(phba->ctrl_regs_memmap_p);
	iounmap(phba->slim_memmap_p);

	return;
}

/**
 * lpfc_sli4_post_status_check - Wait for SLI4 POST done and check status
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to wait for SLI4 device Power On Self Test (POST)
 * done and check status.
 *
 * Return 0 if successful, otherwise -ENODEV.
 **/
int
lpfc_sli4_post_status_check(struct lpfc_hba *phba)
{
	struct lpfc_register portsmphr_reg, uerrlo_reg, uerrhi_reg;
	struct lpfc_register reg_data;
	int i, port_error = 0;
	uint32_t if_type;

	memset(&portsmphr_reg, 0, sizeof(portsmphr_reg));
	memset(&reg_data, 0, sizeof(reg_data));
	if (!phba->sli4_hba.PSMPHRregaddr)
		return -ENODEV;

	/* Wait up to 30 seconds for the SLI Port POST done and ready */
	for (i = 0; i < 3000; i++) {
		if (lpfc_readl(phba->sli4_hba.PSMPHRregaddr,
			&portsmphr_reg.word0) ||
			(bf_get(lpfc_port_smphr_perr, &portsmphr_reg))) {
			/* Port has a fatal POST error, break out */
			port_error = -ENODEV;
			break;
		}
		if (LPFC_POST_STAGE_PORT_READY ==
		    bf_get(lpfc_port_smphr_port_status, &portsmphr_reg))
			break;
		msleep(10);
	}

	/*
	 * If there was a port error during POST, then don't proceed with
	 * other register reads as the data may not be valid.  Just exit.
	 */
	if (port_error) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
			"1408 Port Failed POST - portsmphr=0x%x, "
			"perr=x%x, sfi=x%x, nip=x%x, ipc=x%x, scr1=x%x, "
			"scr2=x%x, hscratch=x%x, pstatus=x%x\n",
			portsmphr_reg.word0,
			bf_get(lpfc_port_smphr_perr, &portsmphr_reg),
			bf_get(lpfc_port_smphr_sfi, &portsmphr_reg),
			bf_get(lpfc_port_smphr_nip, &portsmphr_reg),
			bf_get(lpfc_port_smphr_ipc, &portsmphr_reg),
			bf_get(lpfc_port_smphr_scr1, &portsmphr_reg),
			bf_get(lpfc_port_smphr_scr2, &portsmphr_reg),
			bf_get(lpfc_port_smphr_host_scratch, &portsmphr_reg),
			bf_get(lpfc_port_smphr_port_status, &portsmphr_reg));
	} else {
		lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
				"2534 Device Info: SLIFamily=0x%x, "
				"SLIRev=0x%x, IFType=0x%x, SLIHint_1=0x%x, "
				"SLIHint_2=0x%x, FT=0x%x\n",
				bf_get(lpfc_sli_intf_sli_family,
				       &phba->sli4_hba.sli_intf),
				bf_get(lpfc_sli_intf_slirev,
				       &phba->sli4_hba.sli_intf),
				bf_get(lpfc_sli_intf_if_type,
				       &phba->sli4_hba.sli_intf),
				bf_get(lpfc_sli_intf_sli_hint1,
				       &phba->sli4_hba.sli_intf),
				bf_get(lpfc_sli_intf_sli_hint2,
				       &phba->sli4_hba.sli_intf),
				bf_get(lpfc_sli_intf_func_type,
				       &phba->sli4_hba.sli_intf));
		/*
		 * Check for other Port errors during the initialization
		 * process.  Fail the load if the port did not come up
		 * correctly.
		 */
		if_type = bf_get(lpfc_sli_intf_if_type,
				 &phba->sli4_hba.sli_intf);
		switch (if_type) {
		case LPFC_SLI_INTF_IF_TYPE_0:
			phba->sli4_hba.ue_mask_lo =
			      readl(phba->sli4_hba.u.if_type0.UEMASKLOregaddr);
			phba->sli4_hba.ue_mask_hi =
			      readl(phba->sli4_hba.u.if_type0.UEMASKHIregaddr);
			uerrlo_reg.word0 =
			      readl(phba->sli4_hba.u.if_type0.UERRLOregaddr);
			uerrhi_reg.word0 =
				readl(phba->sli4_hba.u.if_type0.UERRHIregaddr);
			if ((~phba->sli4_hba.ue_mask_lo & uerrlo_reg.word0) ||
			    (~phba->sli4_hba.ue_mask_hi & uerrhi_reg.word0)) {
				lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
						"1422 Unrecoverable Error "
						"Detected during POST "
						"uerr_lo_reg=0x%x, "
						"uerr_hi_reg=0x%x, "
						"ue_mask_lo_reg=0x%x, "
						"ue_mask_hi_reg=0x%x\n",
						uerrlo_reg.word0,
						uerrhi_reg.word0,
						phba->sli4_hba.ue_mask_lo,
						phba->sli4_hba.ue_mask_hi);
				port_error = -ENODEV;
			}
			break;
		case LPFC_SLI_INTF_IF_TYPE_2:
		case LPFC_SLI_INTF_IF_TYPE_6:
			/* Final checks.  The port status should be clean. */
			if (lpfc_readl(phba->sli4_hba.u.if_type2.STATUSregaddr,
				&reg_data.word0) ||
				(bf_get(lpfc_sliport_status_err, &reg_data) &&
				 !bf_get(lpfc_sliport_status_rn, &reg_data))) {
				phba->work_status[0] =
					readl(phba->sli4_hba.u.if_type2.
					      ERR1regaddr);
				phba->work_status[1] =
					readl(phba->sli4_hba.u.if_type2.
					      ERR2regaddr);
				lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
					"2888 Unrecoverable port error "
					"following POST: port status reg "
					"0x%x, port_smphr reg 0x%x, "
					"error 1=0x%x, error 2=0x%x\n",
					reg_data.word0,
					portsmphr_reg.word0,
					phba->work_status[0],
					phba->work_status[1]);
				port_error = -ENODEV;
			}
			break;
		case LPFC_SLI_INTF_IF_TYPE_1:
		default:
			break;
		}
	}
	return port_error;
}

/**
 * lpfc_sli4_bar0_register_memmap - Set up SLI4 BAR0 register memory map.
 * @phba: pointer to lpfc hba data structure.
 * @if_type:  The SLI4 interface type getting configured.
 *
 * This routine is invoked to set up SLI4 BAR0 PCI config space register
 * memory map.
 **/
static void
lpfc_sli4_bar0_register_memmap(struct lpfc_hba *phba, uint32_t if_type)
{
	switch (if_type) {
	case LPFC_SLI_INTF_IF_TYPE_0:
		phba->sli4_hba.u.if_type0.UERRLOregaddr =
			phba->sli4_hba.conf_regs_memmap_p + LPFC_UERR_STATUS_LO;
		phba->sli4_hba.u.if_type0.UERRHIregaddr =
			phba->sli4_hba.conf_regs_memmap_p + LPFC_UERR_STATUS_HI;
		phba->sli4_hba.u.if_type0.UEMASKLOregaddr =
			phba->sli4_hba.conf_regs_memmap_p + LPFC_UE_MASK_LO;
		phba->sli4_hba.u.if_type0.UEMASKHIregaddr =
			phba->sli4_hba.conf_regs_memmap_p + LPFC_UE_MASK_HI;
		phba->sli4_hba.SLIINTFregaddr =
			phba->sli4_hba.conf_regs_memmap_p + LPFC_SLI_INTF;
		break;
	case LPFC_SLI_INTF_IF_TYPE_2:
		phba->sli4_hba.u.if_type2.EQDregaddr =
			phba->sli4_hba.conf_regs_memmap_p +
						LPFC_CTL_PORT_EQ_DELAY_OFFSET;
		phba->sli4_hba.u.if_type2.ERR1regaddr =
			phba->sli4_hba.conf_regs_memmap_p +
						LPFC_CTL_PORT_ER1_OFFSET;
		phba->sli4_hba.u.if_type2.ERR2regaddr =
			phba->sli4_hba.conf_regs_memmap_p +
						LPFC_CTL_PORT_ER2_OFFSET;
		phba->sli4_hba.u.if_type2.CTRLregaddr =
			phba->sli4_hba.conf_regs_memmap_p +
						LPFC_CTL_PORT_CTL_OFFSET;
		phba->sli4_hba.u.if_type2.STATUSregaddr =
			phba->sli4_hba.conf_regs_memmap_p +
						LPFC_CTL_PORT_STA_OFFSET;
		phba->sli4_hba.SLIINTFregaddr =
			phba->sli4_hba.conf_regs_memmap_p + LPFC_SLI_INTF;
		phba->sli4_hba.PSMPHRregaddr =
			phba->sli4_hba.conf_regs_memmap_p +
						LPFC_CTL_PORT_SEM_OFFSET;
		phba->sli4_hba.RQDBregaddr =
			phba->sli4_hba.conf_regs_memmap_p +
						LPFC_ULP0_RQ_DOORBELL;
		phba->sli4_hba.WQDBregaddr =
			phba->sli4_hba.conf_regs_memmap_p +
						LPFC_ULP0_WQ_DOORBELL;
		phba->sli4_hba.CQDBregaddr =
			phba->sli4_hba.conf_regs_memmap_p + LPFC_EQCQ_DOORBELL;
		phba->sli4_hba.EQDBregaddr = phba->sli4_hba.CQDBregaddr;
		phba->sli4_hba.MQDBregaddr =
			phba->sli4_hba.conf_regs_memmap_p + LPFC_MQ_DOORBELL;
		phba->sli4_hba.BMBXregaddr =
			phba->sli4_hba.conf_regs_memmap_p + LPFC_BMBX;
		break;
	case LPFC_SLI_INTF_IF_TYPE_6:
		phba->sli4_hba.u.if_type2.EQDregaddr =
			phba->sli4_hba.conf_regs_memmap_p +
						LPFC_CTL_PORT_EQ_DELAY_OFFSET;
		phba->sli4_hba.u.if_type2.ERR1regaddr =
			phba->sli4_hba.conf_regs_memmap_p +
						LPFC_CTL_PORT_ER1_OFFSET;
		phba->sli4_hba.u.if_type2.ERR2regaddr =
			phba->sli4_hba.conf_regs_memmap_p +
						LPFC_CTL_PORT_ER2_OFFSET;
		phba->sli4_hba.u.if_type2.CTRLregaddr =
			phba->sli4_hba.conf_regs_memmap_p +
						LPFC_CTL_PORT_CTL_OFFSET;
		phba->sli4_hba.u.if_type2.STATUSregaddr =
			phba->sli4_hba.conf_regs_memmap_p +
						LPFC_CTL_PORT_STA_OFFSET;
		phba->sli4_hba.PSMPHRregaddr =
			phba->sli4_hba.conf_regs_memmap_p +
						LPFC_CTL_PORT_SEM_OFFSET;
		phba->sli4_hba.BMBXregaddr =
			phba->sli4_hba.conf_regs_memmap_p + LPFC_BMBX;
		break;
	case LPFC_SLI_INTF_IF_TYPE_1:
	default:
		dev_printk(KERN_ERR, &phba->pcidev->dev,
			   "FATAL - unsupported SLI4 interface type - %d\n",
			   if_type);
		break;
	}
}

/**
 * lpfc_sli4_bar1_register_memmap - Set up SLI4 BAR1 register memory map.
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to set up SLI4 BAR1 register memory map.
 **/
static void
lpfc_sli4_bar1_register_memmap(struct lpfc_hba *phba, uint32_t if_type)
{
	switch (if_type) {
	case LPFC_SLI_INTF_IF_TYPE_0:
		phba->sli4_hba.PSMPHRregaddr =
			phba->sli4_hba.ctrl_regs_memmap_p +
			LPFC_SLIPORT_IF0_SMPHR;
		phba->sli4_hba.ISRregaddr = phba->sli4_hba.ctrl_regs_memmap_p +
			LPFC_HST_ISR0;
		phba->sli4_hba.IMRregaddr = phba->sli4_hba.ctrl_regs_memmap_p +
			LPFC_HST_IMR0;
		phba->sli4_hba.ISCRregaddr = phba->sli4_hba.ctrl_regs_memmap_p +
			LPFC_HST_ISCR0;
		break;
	case LPFC_SLI_INTF_IF_TYPE_6:
		phba->sli4_hba.RQDBregaddr = phba->sli4_hba.drbl_regs_memmap_p +
			LPFC_IF6_RQ_DOORBELL;
		phba->sli4_hba.WQDBregaddr = phba->sli4_hba.drbl_regs_memmap_p +
			LPFC_IF6_WQ_DOORBELL;
		phba->sli4_hba.CQDBregaddr = phba->sli4_hba.drbl_regs_memmap_p +
			LPFC_IF6_CQ_DOORBELL;
		phba->sli4_hba.EQDBregaddr = phba->sli4_hba.drbl_regs_memmap_p +
			LPFC_IF6_EQ_DOORBELL;
		phba->sli4_hba.MQDBregaddr = phba->sli4_hba.drbl_regs_memmap_p +
			LPFC_IF6_MQ_DOORBELL;
		break;
	case LPFC_SLI_INTF_IF_TYPE_2:
	case LPFC_SLI_INTF_IF_TYPE_1:
	default:
		dev_err(&phba->pcidev->dev,
			   "FATAL - unsupported SLI4 interface type - %d\n",
			   if_type);
		break;
	}
}

/**
 * lpfc_sli4_bar2_register_memmap - Set up SLI4 BAR2 register memory map.
 * @phba: pointer to lpfc hba data structure.
 * @vf: virtual function number
 *
 * This routine is invoked to set up SLI4 BAR2 doorbell register memory map
 * based on the given viftual function number, @vf.
 *
 * Return 0 if successful, otherwise -ENODEV.
 **/
static int
lpfc_sli4_bar2_register_memmap(struct lpfc_hba *phba, uint32_t vf)
{
	if (vf > LPFC_VIR_FUNC_MAX)
		return -ENODEV;

	phba->sli4_hba.RQDBregaddr = (phba->sli4_hba.drbl_regs_memmap_p +
				vf * LPFC_VFR_PAGE_SIZE +
					LPFC_ULP0_RQ_DOORBELL);
	phba->sli4_hba.WQDBregaddr = (phba->sli4_hba.drbl_regs_memmap_p +
				vf * LPFC_VFR_PAGE_SIZE +
					LPFC_ULP0_WQ_DOORBELL);
	phba->sli4_hba.CQDBregaddr = (phba->sli4_hba.drbl_regs_memmap_p +
				vf * LPFC_VFR_PAGE_SIZE +
					LPFC_EQCQ_DOORBELL);
	phba->sli4_hba.EQDBregaddr = phba->sli4_hba.CQDBregaddr;
	phba->sli4_hba.MQDBregaddr = (phba->sli4_hba.drbl_regs_memmap_p +
				vf * LPFC_VFR_PAGE_SIZE + LPFC_MQ_DOORBELL);
	phba->sli4_hba.BMBXregaddr = (phba->sli4_hba.drbl_regs_memmap_p +
				vf * LPFC_VFR_PAGE_SIZE + LPFC_BMBX);
	return 0;
}

/**
 * lpfc_create_bootstrap_mbox - Create the bootstrap mailbox
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to create the bootstrap mailbox
 * region consistent with the SLI-4 interface spec.  This
 * routine allocates all memory necessary to communicate
 * mailbox commands to the port and sets up all alignment
 * needs.  No locks are expected to be held when calling
 * this routine.
 *
 * Return codes
 * 	0 - successful
 * 	-ENOMEM - could not allocated memory.
 **/
static int
lpfc_create_bootstrap_mbox(struct lpfc_hba *phba)
{
	uint32_t bmbx_size;
	struct lpfc_dmabuf *dmabuf;
	struct dma_address *dma_address;
	uint32_t pa_addr;
	uint64_t phys_addr;

	dmabuf = kzalloc(sizeof(struct lpfc_dmabuf), GFP_KERNEL);
	if (!dmabuf)
		return -ENOMEM;

	/*
	 * The bootstrap mailbox region is comprised of 2 parts
	 * plus an alignment restriction of 16 bytes.
	 */
	bmbx_size = sizeof(struct lpfc_bmbx_create) + (LPFC_ALIGN_16_BYTE - 1);
	dmabuf->virt = dma_alloc_coherent(&phba->pcidev->dev, bmbx_size,
					  &dmabuf->phys, GFP_KERNEL);
	if (!dmabuf->virt) {
		kfree(dmabuf);
		return -ENOMEM;
	}

	/*
	 * Initialize the bootstrap mailbox pointers now so that the register
	 * operations are simple later.  The mailbox dma address is required
	 * to be 16-byte aligned.  Also align the virtual memory as each
	 * maibox is copied into the bmbx mailbox region before issuing the
	 * command to the port.
	 */
	phba->sli4_hba.bmbx.dmabuf = dmabuf;
	phba->sli4_hba.bmbx.bmbx_size = bmbx_size;

	phba->sli4_hba.bmbx.avirt = PTR_ALIGN(dmabuf->virt,
					      LPFC_ALIGN_16_BYTE);
	phba->sli4_hba.bmbx.aphys = ALIGN(dmabuf->phys,
					      LPFC_ALIGN_16_BYTE);

	/*
	 * Set the high and low physical addresses now.  The SLI4 alignment
	 * requirement is 16 bytes and the mailbox is posted to the port
	 * as two 30-bit addresses.  The other data is a bit marking whether
	 * the 30-bit address is the high or low address.
	 * Upcast bmbx aphys to 64bits so shift instruction compiles
	 * clean on 32 bit machines.
	 */
	dma_address = &phba->sli4_hba.bmbx.dma_address;
	phys_addr = (uint64_t)phba->sli4_hba.bmbx.aphys;
	pa_addr = (uint32_t) ((phys_addr >> 34) & 0x3fffffff);
	dma_address->addr_hi = (uint32_t) ((pa_addr << 2) |
					   LPFC_BMBX_BIT1_ADDR_HI);

	pa_addr = (uint32_t) ((phba->sli4_hba.bmbx.aphys >> 4) & 0x3fffffff);
	dma_address->addr_lo = (uint32_t) ((pa_addr << 2) |
					   LPFC_BMBX_BIT1_ADDR_LO);
	return 0;
}

/**
 * lpfc_destroy_bootstrap_mbox - Destroy all bootstrap mailbox resources
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to teardown the bootstrap mailbox
 * region and release all host resources. This routine requires
 * the caller to ensure all mailbox commands recovered, no
 * additional mailbox comands are sent, and interrupts are disabled
 * before calling this routine.
 *
 **/
static void
lpfc_destroy_bootstrap_mbox(struct lpfc_hba *phba)
{
	dma_free_coherent(&phba->pcidev->dev,
			  phba->sli4_hba.bmbx.bmbx_size,
			  phba->sli4_hba.bmbx.dmabuf->virt,
			  phba->sli4_hba.bmbx.dmabuf->phys);

	kfree(phba->sli4_hba.bmbx.dmabuf);
	memset(&phba->sli4_hba.bmbx, 0, sizeof(struct lpfc_bmbx));
}

static const char * const lpfc_topo_to_str[] = {
	"Loop then P2P",
	"Loopback",
	"P2P Only",
	"Unsupported",
	"Loop Only",
	"Unsupported",
	"P2P then Loop",
};

/**
 * lpfc_map_topology - Map the topology read from READ_CONFIG
 * @phba: pointer to lpfc hba data structure.
 * @rdconf: pointer to read config data
 *
 * This routine is invoked to map the topology values as read
 * from the read config mailbox command. If the persistent
 * topology feature is supported, the firmware will provide the
 * saved topology information to be used in INIT_LINK
 *
 **/
#define	LINK_FLAGS_DEF	0x0
#define	LINK_FLAGS_P2P	0x1
#define	LINK_FLAGS_LOOP	0x2
static void
lpfc_map_topology(struct lpfc_hba *phba, struct lpfc_mbx_read_config *rd_config)
{
	u8 ptv, tf, pt;

	ptv = bf_get(lpfc_mbx_rd_conf_ptv, rd_config);
	tf = bf_get(lpfc_mbx_rd_conf_tf, rd_config);
	pt = bf_get(lpfc_mbx_rd_conf_pt, rd_config);

	lpfc_printf_log(phba, KERN_INFO, LOG_SLI,
			"2027 Read Config Data : ptv:0x%x, tf:0x%x pt:0x%x",
			 ptv, tf, pt);
	if (!ptv) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_SLI,
				"2019 FW does not support persistent topology "
				"Using driver parameter defined value [%s]",
				lpfc_topo_to_str[phba->cfg_topology]);
		return;
	}
	/* FW supports persistent topology - override module parameter value */
	phba->hba_flag |= HBA_PERSISTENT_TOPO;
	switch (phba->pcidev->device) {
	case PCI_DEVICE_ID_LANCER_G7_FC:
		if (tf || (pt == LINK_FLAGS_LOOP)) {
			/* Invalid values from FW - use driver params */
			phba->hba_flag &= ~HBA_PERSISTENT_TOPO;
		} else {
			/* Prism only supports PT2PT topology */
			phba->cfg_topology = FLAGS_TOPOLOGY_MODE_PT_PT;
		}
		break;
	case PCI_DEVICE_ID_LANCER_G6_FC:
		if (!tf) {
			phba->cfg_topology = ((pt == LINK_FLAGS_LOOP)
					? FLAGS_TOPOLOGY_MODE_LOOP
					: FLAGS_TOPOLOGY_MODE_PT_PT);
		} else {
			phba->hba_flag &= ~HBA_PERSISTENT_TOPO;
		}
		break;
	default:	/* G5 */
		if (tf) {
			/* If topology failover set - pt is '0' or '1' */
			phba->cfg_topology = (pt ? FLAGS_TOPOLOGY_MODE_PT_LOOP :
					      FLAGS_TOPOLOGY_MODE_LOOP_PT);
		} else {
			phba->cfg_topology = ((pt == LINK_FLAGS_P2P)
					? FLAGS_TOPOLOGY_MODE_PT_PT
					: FLAGS_TOPOLOGY_MODE_LOOP);
		}
		break;
	}
	if (phba->hba_flag & HBA_PERSISTENT_TOPO) {
		lpfc_printf_log(phba, KERN_INFO, LOG_SLI,
				"2020 Using persistent topology value [%s]",
				lpfc_topo_to_str[phba->cfg_topology]);
	} else {
		lpfc_printf_log(phba, KERN_WARNING, LOG_SLI,
				"2021 Invalid topology values from FW "
				"Using driver parameter defined value [%s]",
				lpfc_topo_to_str[phba->cfg_topology]);
	}
}

/**
 * lpfc_sli4_read_config - Get the config parameters.
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to read the configuration parameters from the HBA.
 * The configuration parameters are used to set the base and maximum values
 * for RPI's XRI's VPI's VFI's and FCFIs. These values also affect the resource
 * allocation for the port.
 *
 * Return codes
 * 	0 - successful
 * 	-ENOMEM - No available memory
 *      -EIO - The mailbox failed to complete successfully.
 **/
int
lpfc_sli4_read_config(struct lpfc_hba *phba)
{
	LPFC_MBOXQ_t *pmb;
	struct lpfc_mbx_read_config *rd_config;
	union  lpfc_sli4_cfg_shdr *shdr;
	uint32_t shdr_status, shdr_add_status;
	struct lpfc_mbx_get_func_cfg *get_func_cfg;
	struct lpfc_rsrc_desc_fcfcoe *desc;
	char *pdesc_0;
	uint16_t forced_link_speed;
	uint32_t if_type, qmin;
	int length, i, rc = 0, rc2;

	pmb = (LPFC_MBOXQ_t *) mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!pmb) {
		lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
				"2011 Unable to allocate memory for issuing "
				"SLI_CONFIG_SPECIAL mailbox command\n");
		return -ENOMEM;
	}

	lpfc_read_config(phba, pmb);

	rc = lpfc_sli_issue_mbox(phba, pmb, MBX_POLL);
	if (rc != MBX_SUCCESS) {
		lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
			"2012 Mailbox failed , mbxCmd x%x "
			"READ_CONFIG, mbxStatus x%x\n",
			bf_get(lpfc_mqe_command, &pmb->u.mqe),
			bf_get(lpfc_mqe_status, &pmb->u.mqe));
		rc = -EIO;
	} else {
		rd_config = &pmb->u.mqe.un.rd_config;
		if (bf_get(lpfc_mbx_rd_conf_lnk_ldv, rd_config)) {
			phba->sli4_hba.lnk_info.lnk_dv = LPFC_LNK_DAT_VAL;
			phba->sli4_hba.lnk_info.lnk_tp =
				bf_get(lpfc_mbx_rd_conf_lnk_type, rd_config);
			phba->sli4_hba.lnk_info.lnk_no =
				bf_get(lpfc_mbx_rd_conf_lnk_numb, rd_config);
			lpfc_printf_log(phba, KERN_INFO, LOG_SLI,
					"3081 lnk_type:%d, lnk_numb:%d\n",
					phba->sli4_hba.lnk_info.lnk_tp,
					phba->sli4_hba.lnk_info.lnk_no);
		} else
			lpfc_printf_log(phba, KERN_WARNING, LOG_SLI,
					"3082 Mailbox (x%x) returned ldv:x0\n",
					bf_get(lpfc_mqe_command, &pmb->u.mqe));
		if (bf_get(lpfc_mbx_rd_conf_bbscn_def, rd_config)) {
			phba->bbcredit_support = 1;
			phba->sli4_hba.bbscn_params.word0 = rd_config->word8;
		}

		phba->sli4_hba.conf_trunk =
			bf_get(lpfc_mbx_rd_conf_trunk, rd_config);
		phba->sli4_hba.extents_in_use =
			bf_get(lpfc_mbx_rd_conf_extnts_inuse, rd_config);
		phba->sli4_hba.max_cfg_param.max_xri =
			bf_get(lpfc_mbx_rd_conf_xri_count, rd_config);
		/* Reduce resource usage in kdump environment */
		if (is_kdump_kernel() &&
		    phba->sli4_hba.max_cfg_param.max_xri > 512)
			phba->sli4_hba.max_cfg_param.max_xri = 512;
		phba->sli4_hba.max_cfg_param.xri_base =
			bf_get(lpfc_mbx_rd_conf_xri_base, rd_config);
		phba->sli4_hba.max_cfg_param.max_vpi =
			bf_get(lpfc_mbx_rd_conf_vpi_count, rd_config);
		/* Limit the max we support */
		if (phba->sli4_hba.max_cfg_param.max_vpi > LPFC_MAX_VPORTS)
			phba->sli4_hba.max_cfg_param.max_vpi = LPFC_MAX_VPORTS;
		phba->sli4_hba.max_cfg_param.vpi_base =
			bf_get(lpfc_mbx_rd_conf_vpi_base, rd_config);
		phba->sli4_hba.max_cfg_param.max_rpi =
			bf_get(lpfc_mbx_rd_conf_rpi_count, rd_config);
		phba->sli4_hba.max_cfg_param.rpi_base =
			bf_get(lpfc_mbx_rd_conf_rpi_base, rd_config);
		phba->sli4_hba.max_cfg_param.max_vfi =
			bf_get(lpfc_mbx_rd_conf_vfi_count, rd_config);
		phba->sli4_hba.max_cfg_param.vfi_base =
			bf_get(lpfc_mbx_rd_conf_vfi_base, rd_config);
		phba->sli4_hba.max_cfg_param.max_fcfi =
			bf_get(lpfc_mbx_rd_conf_fcfi_count, rd_config);
		phba->sli4_hba.max_cfg_param.max_eq =
			bf_get(lpfc_mbx_rd_conf_eq_count, rd_config);
		phba->sli4_hba.max_cfg_param.max_rq =
			bf_get(lpfc_mbx_rd_conf_rq_count, rd_config);
		phba->sli4_hba.max_cfg_param.max_wq =
			bf_get(lpfc_mbx_rd_conf_wq_count, rd_config);
		phba->sli4_hba.max_cfg_param.max_cq =
			bf_get(lpfc_mbx_rd_conf_cq_count, rd_config);
		phba->lmt = bf_get(lpfc_mbx_rd_conf_lmt, rd_config);
		phba->sli4_hba.next_xri = phba->sli4_hba.max_cfg_param.xri_base;
		phba->vpi_base = phba->sli4_hba.max_cfg_param.vpi_base;
		phba->vfi_base = phba->sli4_hba.max_cfg_param.vfi_base;
		phba->max_vpi = (phba->sli4_hba.max_cfg_param.max_vpi > 0) ?
				(phba->sli4_hba.max_cfg_param.max_vpi - 1) : 0;
		phba->max_vports = phba->max_vpi;
		lpfc_map_topology(phba, rd_config);
		lpfc_printf_log(phba, KERN_INFO, LOG_SLI,
				"2003 cfg params Extents? %d "
				"XRI(B:%d M:%d), "
				"VPI(B:%d M:%d) "
				"VFI(B:%d M:%d) "
				"RPI(B:%d M:%d) "
				"FCFI:%d EQ:%d CQ:%d WQ:%d RQ:%d\n",
				phba->sli4_hba.extents_in_use,
				phba->sli4_hba.max_cfg_param.xri_base,
				phba->sli4_hba.max_cfg_param.max_xri,
				phba->sli4_hba.max_cfg_param.vpi_base,
				phba->sli4_hba.max_cfg_param.max_vpi,
				phba->sli4_hba.max_cfg_param.vfi_base,
				phba->sli4_hba.max_cfg_param.max_vfi,
				phba->sli4_hba.max_cfg_param.rpi_base,
				phba->sli4_hba.max_cfg_param.max_rpi,
				phba->sli4_hba.max_cfg_param.max_fcfi,
				phba->sli4_hba.max_cfg_param.max_eq,
				phba->sli4_hba.max_cfg_param.max_cq,
				phba->sli4_hba.max_cfg_param.max_wq,
				phba->sli4_hba.max_cfg_param.max_rq);

		/*
		 * Calculate queue resources based on how
		 * many WQ/CQ/EQs are available.
		 */
		qmin = phba->sli4_hba.max_cfg_param.max_wq;
		if (phba->sli4_hba.max_cfg_param.max_cq < qmin)
			qmin = phba->sli4_hba.max_cfg_param.max_cq;
		if (phba->sli4_hba.max_cfg_param.max_eq < qmin)
			qmin = phba->sli4_hba.max_cfg_param.max_eq;
		/*
		 * Whats left after this can go toward NVME / FCP.
		 * The minus 4 accounts for ELS, NVME LS, MBOX
		 * plus one extra. When configured for
		 * NVMET, FCP io channel WQs are not created.
		 */
		qmin -= 4;

		/* Check to see if there is enough for NVME */
		if ((phba->cfg_irq_chann > qmin) ||
		    (phba->cfg_hdw_queue > qmin)) {
			lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
					"2005 Reducing Queues: "
					"WQ %d CQ %d EQ %d: min %d: "
					"IRQ %d HDWQ %d\n",
					phba->sli4_hba.max_cfg_param.max_wq,
					phba->sli4_hba.max_cfg_param.max_cq,
					phba->sli4_hba.max_cfg_param.max_eq,
					qmin, phba->cfg_irq_chann,
					phba->cfg_hdw_queue);

			if (phba->cfg_irq_chann > qmin)
				phba->cfg_irq_chann = qmin;
			if (phba->cfg_hdw_queue > qmin)
				phba->cfg_hdw_queue = qmin;
		}
	}

	if (rc)
		goto read_cfg_out;

	/* Update link speed if forced link speed is supported */
	if_type = bf_get(lpfc_sli_intf_if_type, &phba->sli4_hba.sli_intf);
	if (if_type >= LPFC_SLI_INTF_IF_TYPE_2) {
		forced_link_speed =
			bf_get(lpfc_mbx_rd_conf_link_speed, rd_config);
		if (forced_link_speed) {
			phba->hba_flag |= HBA_FORCED_LINK_SPEED;

			switch (forced_link_speed) {
			case LINK_SPEED_1G:
				phba->cfg_link_speed =
					LPFC_USER_LINK_SPEED_1G;
				break;
			case LINK_SPEED_2G:
				phba->cfg_link_speed =
					LPFC_USER_LINK_SPEED_2G;
				break;
			case LINK_SPEED_4G:
				phba->cfg_link_speed =
					LPFC_USER_LINK_SPEED_4G;
				break;
			case LINK_SPEED_8G:
				phba->cfg_link_speed =
					LPFC_USER_LINK_SPEED_8G;
				break;
			case LINK_SPEED_10G:
				phba->cfg_link_speed =
					LPFC_USER_LINK_SPEED_10G;
				break;
			case LINK_SPEED_16G:
				phba->cfg_link_speed =
					LPFC_USER_LINK_SPEED_16G;
				break;
			case LINK_SPEED_32G:
				phba->cfg_link_speed =
					LPFC_USER_LINK_SPEED_32G;
				break;
			case LINK_SPEED_64G:
				phba->cfg_link_speed =
					LPFC_USER_LINK_SPEED_64G;
				break;
			case 0xffff:
				phba->cfg_link_speed =
					LPFC_USER_LINK_SPEED_AUTO;
				break;
			default:
				lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
						"0047 Unrecognized link "
						"speed : %d\n",
						forced_link_speed);
				phba->cfg_link_speed =
					LPFC_USER_LINK_SPEED_AUTO;
			}
		}
	}

	/* Reset the DFT_HBA_Q_DEPTH to the max xri  */
	length = phba->sli4_hba.max_cfg_param.max_xri -
			lpfc_sli4_get_els_iocb_cnt(phba);
	if (phba->cfg_hba_queue_depth > length) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_INIT,
				"3361 HBA queue depth changed from %d to %d\n",
				phba->cfg_hba_queue_depth, length);
		phba->cfg_hba_queue_depth = length;
	}

	if (bf_get(lpfc_sli_intf_if_type, &phba->sli4_hba.sli_intf) <
	    LPFC_SLI_INTF_IF_TYPE_2)
		goto read_cfg_out;

	/* get the pf# and vf# for SLI4 if_type 2 port */
	length = (sizeof(struct lpfc_mbx_get_func_cfg) -
		  sizeof(struct lpfc_sli4_cfg_mhdr));
	lpfc_sli4_config(phba, pmb, LPFC_MBOX_SUBSYSTEM_COMMON,
			 LPFC_MBOX_OPCODE_GET_FUNCTION_CONFIG,
			 length, LPFC_SLI4_MBX_EMBED);

	rc2 = lpfc_sli_issue_mbox(phba, pmb, MBX_POLL);
	shdr = (union lpfc_sli4_cfg_shdr *)
				&pmb->u.mqe.un.sli4_config.header.cfg_shdr;
	shdr_status = bf_get(lpfc_mbox_hdr_status, &shdr->response);
	shdr_add_status = bf_get(lpfc_mbox_hdr_add_status, &shdr->response);
	if (rc2 || shdr_status || shdr_add_status) {
		lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
				"3026 Mailbox failed , mbxCmd x%x "
				"GET_FUNCTION_CONFIG, mbxStatus x%x\n",
				bf_get(lpfc_mqe_command, &pmb->u.mqe),
				bf_get(lpfc_mqe_status, &pmb->u.mqe));
		goto read_cfg_out;
	}

	/* search for fc_fcoe resrouce descriptor */
	get_func_cfg = &pmb->u.mqe.un.get_func_cfg;

	pdesc_0 = (char *)&get_func_cfg->func_cfg.desc[0];
	desc = (struct lpfc_rsrc_desc_fcfcoe *)pdesc_0;
	length = bf_get(lpfc_rsrc_desc_fcfcoe_length, desc);
	if (length == LPFC_RSRC_DESC_TYPE_FCFCOE_V0_RSVD)
		length = LPFC_RSRC_DESC_TYPE_FCFCOE_V0_LENGTH;
	else if (length != LPFC_RSRC_DESC_TYPE_FCFCOE_V1_LENGTH)
		goto read_cfg_out;

	for (i = 0; i < LPFC_RSRC_DESC_MAX_NUM; i++) {
		desc = (struct lpfc_rsrc_desc_fcfcoe *)(pdesc_0 + length * i);
		if (LPFC_RSRC_DESC_TYPE_FCFCOE ==
		    bf_get(lpfc_rsrc_desc_fcfcoe_type, desc)) {
			phba->sli4_hba.iov.pf_number =
				bf_get(lpfc_rsrc_desc_fcfcoe_pfnum, desc);
			phba->sli4_hba.iov.vf_number =
				bf_get(lpfc_rsrc_desc_fcfcoe_vfnum, desc);
			break;
		}
	}

	if (i < LPFC_RSRC_DESC_MAX_NUM)
		lpfc_printf_log(phba, KERN_INFO, LOG_SLI,
				"3027 GET_FUNCTION_CONFIG: pf_number:%d, "
				"vf_number:%d\n", phba->sli4_hba.iov.pf_number,
				phba->sli4_hba.iov.vf_number);
	else
		lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
				"3028 GET_FUNCTION_CONFIG: failed to find "
				"Resource Descriptor:x%x\n",
				LPFC_RSRC_DESC_TYPE_FCFCOE);

read_cfg_out:
	mempool_free(pmb, phba->mbox_mem_pool);
	return rc;
}

/**
 * lpfc_setup_endian_order - Write endian order to an SLI4 if_type 0 port.
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to setup the port-side endian order when
 * the port if_type is 0.  This routine has no function for other
 * if_types.
 *
 * Return codes
 * 	0 - successful
 * 	-ENOMEM - No available memory
 *      -EIO - The mailbox failed to complete successfully.
 **/
static int
lpfc_setup_endian_order(struct lpfc_hba *phba)
{
	LPFC_MBOXQ_t *mboxq;
	uint32_t if_type, rc = 0;
	uint32_t endian_mb_data[2] = {HOST_ENDIAN_LOW_WORD0,
				      HOST_ENDIAN_HIGH_WORD1};

	if_type = bf_get(lpfc_sli_intf_if_type, &phba->sli4_hba.sli_intf);
	switch (if_type) {
	case LPFC_SLI_INTF_IF_TYPE_0:
		mboxq = (LPFC_MBOXQ_t *) mempool_alloc(phba->mbox_mem_pool,
						       GFP_KERNEL);
		if (!mboxq) {
			lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
					"0492 Unable to allocate memory for "
					"issuing SLI_CONFIG_SPECIAL mailbox "
					"command\n");
			return -ENOMEM;
		}

		/*
		 * The SLI4_CONFIG_SPECIAL mailbox command requires the first
		 * two words to contain special data values and no other data.
		 */
		memset(mboxq, 0, sizeof(LPFC_MBOXQ_t));
		memcpy(&mboxq->u.mqe, &endian_mb_data, sizeof(endian_mb_data));
		rc = lpfc_sli_issue_mbox(phba, mboxq, MBX_POLL);
		if (rc != MBX_SUCCESS) {
			lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
					"0493 SLI_CONFIG_SPECIAL mailbox "
					"failed with status x%x\n",
					rc);
			rc = -EIO;
		}
		mempool_free(mboxq, phba->mbox_mem_pool);
		break;
	case LPFC_SLI_INTF_IF_TYPE_6:
	case LPFC_SLI_INTF_IF_TYPE_2:
	case LPFC_SLI_INTF_IF_TYPE_1:
	default:
		break;
	}
	return rc;
}

/**
 * lpfc_sli4_queue_verify - Verify and update EQ counts
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to check the user settable queue counts for EQs.
 * After this routine is called the counts will be set to valid values that
 * adhere to the constraints of the system's interrupt vectors and the port's
 * queue resources.
 *
 * Return codes
 *      0 - successful
 *      -ENOMEM - No available memory
 **/
static int
lpfc_sli4_queue_verify(struct lpfc_hba *phba)
{
	/*
	 * Sanity check for configured queue parameters against the run-time
	 * device parameters
	 */

	if (phba->nvmet_support) {
		if (phba->cfg_hdw_queue < phba->cfg_nvmet_mrq)
			phba->cfg_nvmet_mrq = phba->cfg_hdw_queue;
		if (phba->cfg_nvmet_mrq > LPFC_NVMET_MRQ_MAX)
			phba->cfg_nvmet_mrq = LPFC_NVMET_MRQ_MAX;
	}

	lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
			"2574 IO channels: hdwQ %d IRQ %d MRQ: %d\n",
			phba->cfg_hdw_queue, phba->cfg_irq_chann,
			phba->cfg_nvmet_mrq);

	/* Get EQ depth from module parameter, fake the default for now */
	phba->sli4_hba.eq_esize = LPFC_EQE_SIZE_4B;
	phba->sli4_hba.eq_ecount = LPFC_EQE_DEF_COUNT;

	/* Get CQ depth from module parameter, fake the default for now */
	phba->sli4_hba.cq_esize = LPFC_CQE_SIZE;
	phba->sli4_hba.cq_ecount = LPFC_CQE_DEF_COUNT;
	return 0;
}

static int
lpfc_alloc_io_wq_cq(struct lpfc_hba *phba, int idx)
{
	struct lpfc_queue *qdesc;
	u32 wqesize;
	int cpu;

	cpu = lpfc_find_cpu_handle(phba, idx, LPFC_FIND_BY_HDWQ);
	/* Create Fast Path IO CQs */
	if (phba->enab_exp_wqcq_pages)
		/* Increase the CQ size when WQEs contain an embedded cdb */
		qdesc = lpfc_sli4_queue_alloc(phba, LPFC_EXPANDED_PAGE_SIZE,
					      phba->sli4_hba.cq_esize,
					      LPFC_CQE_EXP_COUNT, cpu);

	else
		qdesc = lpfc_sli4_queue_alloc(phba, LPFC_DEFAULT_PAGE_SIZE,
					      phba->sli4_hba.cq_esize,
					      phba->sli4_hba.cq_ecount, cpu);
	if (!qdesc) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
			"0499 Failed allocate fast-path IO CQ (%d)\n", idx);
		return 1;
	}
	qdesc->qe_valid = 1;
	qdesc->hdwq = idx;
	qdesc->chann = cpu;
	phba->sli4_hba.hdwq[idx].io_cq = qdesc;

	/* Create Fast Path IO WQs */
	if (phba->enab_exp_wqcq_pages) {
		/* Increase the WQ size when WQEs contain an embedded cdb */
		wqesize = (phba->fcp_embed_io) ?
			LPFC_WQE128_SIZE : phba->sli4_hba.wq_esize;
		qdesc = lpfc_sli4_queue_alloc(phba, LPFC_EXPANDED_PAGE_SIZE,
					      wqesize,
					      LPFC_WQE_EXP_COUNT, cpu);
	} else
		qdesc = lpfc_sli4_queue_alloc(phba, LPFC_DEFAULT_PAGE_SIZE,
					      phba->sli4_hba.wq_esize,
					      phba->sli4_hba.wq_ecount, cpu);

	if (!qdesc) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0503 Failed allocate fast-path IO WQ (%d)\n",
				idx);
		return 1;
	}
	qdesc->hdwq = idx;
	qdesc->chann = cpu;
	phba->sli4_hba.hdwq[idx].io_wq = qdesc;
	list_add_tail(&qdesc->wq_list, &phba->sli4_hba.lpfc_wq_list);
	return 0;
}

/**
 * lpfc_sli4_queue_create - Create all the SLI4 queues
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to allocate all the SLI4 queues for the FCoE HBA
 * operation. For each SLI4 queue type, the parameters such as queue entry
 * count (queue depth) shall be taken from the module parameter. For now,
 * we just use some constant number as place holder.
 *
 * Return codes
 *      0 - successful
 *      -ENOMEM - No availble memory
 *      -EIO - The mailbox failed to complete successfully.
 **/
int
lpfc_sli4_queue_create(struct lpfc_hba *phba)
{
	struct lpfc_queue *qdesc;
	int idx, cpu, eqcpu;
	struct lpfc_sli4_hdw_queue *qp;
	struct lpfc_vector_map_info *cpup;
	struct lpfc_vector_map_info *eqcpup;
	struct lpfc_eq_intr_info *eqi;

	/*
	 * Create HBA Record arrays.
	 * Both NVME and FCP will share that same vectors / EQs
	 */
	phba->sli4_hba.mq_esize = LPFC_MQE_SIZE;
	phba->sli4_hba.mq_ecount = LPFC_MQE_DEF_COUNT;
	phba->sli4_hba.wq_esize = LPFC_WQE_SIZE;
	phba->sli4_hba.wq_ecount = LPFC_WQE_DEF_COUNT;
	phba->sli4_hba.rq_esize = LPFC_RQE_SIZE;
	phba->sli4_hba.rq_ecount = LPFC_RQE_DEF_COUNT;
	phba->sli4_hba.eq_esize = LPFC_EQE_SIZE_4B;
	phba->sli4_hba.eq_ecount = LPFC_EQE_DEF_COUNT;
	phba->sli4_hba.cq_esize = LPFC_CQE_SIZE;
	phba->sli4_hba.cq_ecount = LPFC_CQE_DEF_COUNT;

	if (!phba->sli4_hba.hdwq) {
		phba->sli4_hba.hdwq = kcalloc(
			phba->cfg_hdw_queue, sizeof(struct lpfc_sli4_hdw_queue),
			GFP_KERNEL);
		if (!phba->sli4_hba.hdwq) {
			lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
					"6427 Failed allocate memory for "
					"fast-path Hardware Queue array\n");
			goto out_error;
		}
		/* Prepare hardware queues to take IO buffers */
		for (idx = 0; idx < phba->cfg_hdw_queue; idx++) {
			qp = &phba->sli4_hba.hdwq[idx];
			spin_lock_init(&qp->io_buf_list_get_lock);
			spin_lock_init(&qp->io_buf_list_put_lock);
			INIT_LIST_HEAD(&qp->lpfc_io_buf_list_get);
			INIT_LIST_HEAD(&qp->lpfc_io_buf_list_put);
			qp->get_io_bufs = 0;
			qp->put_io_bufs = 0;
			qp->total_io_bufs = 0;
			spin_lock_init(&qp->abts_io_buf_list_lock);
			INIT_LIST_HEAD(&qp->lpfc_abts_io_buf_list);
			qp->abts_scsi_io_bufs = 0;
			qp->abts_nvme_io_bufs = 0;
			INIT_LIST_HEAD(&qp->sgl_list);
			INIT_LIST_HEAD(&qp->cmd_rsp_buf_list);
			spin_lock_init(&qp->hdwq_lock);
		}
	}

	if (phba->cfg_enable_fc4_type & LPFC_ENABLE_NVME) {
		if (phba->nvmet_support) {
			phba->sli4_hba.nvmet_cqset = kcalloc(
					phba->cfg_nvmet_mrq,
					sizeof(struct lpfc_queue *),
					GFP_KERNEL);
			if (!phba->sli4_hba.nvmet_cqset) {
				lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
					"3121 Fail allocate memory for "
					"fast-path CQ set array\n");
				goto out_error;
			}
			phba->sli4_hba.nvmet_mrq_hdr = kcalloc(
					phba->cfg_nvmet_mrq,
					sizeof(struct lpfc_queue *),
					GFP_KERNEL);
			if (!phba->sli4_hba.nvmet_mrq_hdr) {
				lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
					"3122 Fail allocate memory for "
					"fast-path RQ set hdr array\n");
				goto out_error;
			}
			phba->sli4_hba.nvmet_mrq_data = kcalloc(
					phba->cfg_nvmet_mrq,
					sizeof(struct lpfc_queue *),
					GFP_KERNEL);
			if (!phba->sli4_hba.nvmet_mrq_data) {
				lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
					"3124 Fail allocate memory for "
					"fast-path RQ set data array\n");
				goto out_error;
			}
		}
	}

	INIT_LIST_HEAD(&phba->sli4_hba.lpfc_wq_list);

	/* Create HBA Event Queues (EQs) */
	for_each_present_cpu(cpu) {
		/* We only want to create 1 EQ per vector, even though
		 * multiple CPUs might be using that vector. so only
		 * selects the CPUs that are LPFC_CPU_FIRST_IRQ.
		 */
		cpup = &phba->sli4_hba.cpu_map[cpu];
		if (!(cpup->flag & LPFC_CPU_FIRST_IRQ))
			continue;

		/* Get a ptr to the Hardware Queue associated with this CPU */
		qp = &phba->sli4_hba.hdwq[cpup->hdwq];

		/* Allocate an EQ */
		qdesc = lpfc_sli4_queue_alloc(phba, LPFC_DEFAULT_PAGE_SIZE,
					      phba->sli4_hba.eq_esize,
					      phba->sli4_hba.eq_ecount, cpu);
		if (!qdesc) {
			lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
					"0497 Failed allocate EQ (%d)\n",
					cpup->hdwq);
			goto out_error;
		}
		qdesc->qe_valid = 1;
		qdesc->hdwq = cpup->hdwq;
		qdesc->chann = cpu; /* First CPU this EQ is affinitized to */
		qdesc->last_cpu = qdesc->chann;

		/* Save the allocated EQ in the Hardware Queue */
		qp->hba_eq = qdesc;

		eqi = per_cpu_ptr(phba->sli4_hba.eq_info, qdesc->last_cpu);
		list_add(&qdesc->cpu_list, &eqi->list);
	}

	/* Now we need to populate the other Hardware Queues, that share
	 * an IRQ vector, with the associated EQ ptr.
	 */
	for_each_present_cpu(cpu) {
		cpup = &phba->sli4_hba.cpu_map[cpu];

		/* Check for EQ already allocated in previous loop */
		if (cpup->flag & LPFC_CPU_FIRST_IRQ)
			continue;

		/* Check for multiple CPUs per hdwq */
		qp = &phba->sli4_hba.hdwq[cpup->hdwq];
		if (qp->hba_eq)
			continue;

		/* We need to share an EQ for this hdwq */
		eqcpu = lpfc_find_cpu_handle(phba, cpup->eq, LPFC_FIND_BY_EQ);
		eqcpup = &phba->sli4_hba.cpu_map[eqcpu];
		qp->hba_eq = phba->sli4_hba.hdwq[eqcpup->hdwq].hba_eq;
	}

	/* Allocate IO Path SLI4 CQ/WQs */
	for (idx = 0; idx < phba->cfg_hdw_queue; idx++) {
		if (lpfc_alloc_io_wq_cq(phba, idx))
			goto out_error;
	}

	if (phba->nvmet_support) {
		for (idx = 0; idx < phba->cfg_nvmet_mrq; idx++) {
			cpu = lpfc_find_cpu_handle(phba, idx,
						   LPFC_FIND_BY_HDWQ);
			qdesc = lpfc_sli4_queue_alloc(phba,
						      LPFC_DEFAULT_PAGE_SIZE,
						      phba->sli4_hba.cq_esize,
						      phba->sli4_hba.cq_ecount,
						      cpu);
			if (!qdesc) {
				lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
						"3142 Failed allocate NVME "
						"CQ Set (%d)\n", idx);
				goto out_error;
			}
			qdesc->qe_valid = 1;
			qdesc->hdwq = idx;
			qdesc->chann = cpu;
			phba->sli4_hba.nvmet_cqset[idx] = qdesc;
		}
	}

	/*
	 * Create Slow Path Completion Queues (CQs)
	 */

	cpu = lpfc_find_cpu_handle(phba, 0, LPFC_FIND_BY_EQ);
	/* Create slow-path Mailbox Command Complete Queue */
	qdesc = lpfc_sli4_queue_alloc(phba, LPFC_DEFAULT_PAGE_SIZE,
				      phba->sli4_hba.cq_esize,
				      phba->sli4_hba.cq_ecount, cpu);
	if (!qdesc) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0500 Failed allocate slow-path mailbox CQ\n");
		goto out_error;
	}
	qdesc->qe_valid = 1;
	phba->sli4_hba.mbx_cq = qdesc;

	/* Create slow-path ELS Complete Queue */
	qdesc = lpfc_sli4_queue_alloc(phba, LPFC_DEFAULT_PAGE_SIZE,
				      phba->sli4_hba.cq_esize,
				      phba->sli4_hba.cq_ecount, cpu);
	if (!qdesc) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0501 Failed allocate slow-path ELS CQ\n");
		goto out_error;
	}
	qdesc->qe_valid = 1;
	qdesc->chann = cpu;
	phba->sli4_hba.els_cq = qdesc;


	/*
	 * Create Slow Path Work Queues (WQs)
	 */

	/* Create Mailbox Command Queue */

	qdesc = lpfc_sli4_queue_alloc(phba, LPFC_DEFAULT_PAGE_SIZE,
				      phba->sli4_hba.mq_esize,
				      phba->sli4_hba.mq_ecount, cpu);
	if (!qdesc) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0505 Failed allocate slow-path MQ\n");
		goto out_error;
	}
	qdesc->chann = cpu;
	phba->sli4_hba.mbx_wq = qdesc;

	/*
	 * Create ELS Work Queues
	 */

	/* Create slow-path ELS Work Queue */
	qdesc = lpfc_sli4_queue_alloc(phba, LPFC_DEFAULT_PAGE_SIZE,
				      phba->sli4_hba.wq_esize,
				      phba->sli4_hba.wq_ecount, cpu);
	if (!qdesc) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0504 Failed allocate slow-path ELS WQ\n");
		goto out_error;
	}
	qdesc->chann = cpu;
	phba->sli4_hba.els_wq = qdesc;
	list_add_tail(&qdesc->wq_list, &phba->sli4_hba.lpfc_wq_list);

	if (phba->cfg_enable_fc4_type & LPFC_ENABLE_NVME) {
		/* Create NVME LS Complete Queue */
		qdesc = lpfc_sli4_queue_alloc(phba, LPFC_DEFAULT_PAGE_SIZE,
					      phba->sli4_hba.cq_esize,
					      phba->sli4_hba.cq_ecount, cpu);
		if (!qdesc) {
			lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
					"6079 Failed allocate NVME LS CQ\n");
			goto out_error;
		}
		qdesc->chann = cpu;
		qdesc->qe_valid = 1;
		phba->sli4_hba.nvmels_cq = qdesc;

		/* Create NVME LS Work Queue */
		qdesc = lpfc_sli4_queue_alloc(phba, LPFC_DEFAULT_PAGE_SIZE,
					      phba->sli4_hba.wq_esize,
					      phba->sli4_hba.wq_ecount, cpu);
		if (!qdesc) {
			lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
					"6080 Failed allocate NVME LS WQ\n");
			goto out_error;
		}
		qdesc->chann = cpu;
		phba->sli4_hba.nvmels_wq = qdesc;
		list_add_tail(&qdesc->wq_list, &phba->sli4_hba.lpfc_wq_list);
	}

	/*
	 * Create Receive Queue (RQ)
	 */

	/* Create Receive Queue for header */
	qdesc = lpfc_sli4_queue_alloc(phba, LPFC_DEFAULT_PAGE_SIZE,
				      phba->sli4_hba.rq_esize,
				      phba->sli4_hba.rq_ecount, cpu);
	if (!qdesc) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0506 Failed allocate receive HRQ\n");
		goto out_error;
	}
	phba->sli4_hba.hdr_rq = qdesc;

	/* Create Receive Queue for data */
	qdesc = lpfc_sli4_queue_alloc(phba, LPFC_DEFAULT_PAGE_SIZE,
				      phba->sli4_hba.rq_esize,
				      phba->sli4_hba.rq_ecount, cpu);
	if (!qdesc) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0507 Failed allocate receive DRQ\n");
		goto out_error;
	}
	phba->sli4_hba.dat_rq = qdesc;

	if ((phba->cfg_enable_fc4_type & LPFC_ENABLE_NVME) &&
	    phba->nvmet_support) {
		for (idx = 0; idx < phba->cfg_nvmet_mrq; idx++) {
			cpu = lpfc_find_cpu_handle(phba, idx,
						   LPFC_FIND_BY_HDWQ);
			/* Create NVMET Receive Queue for header */
			qdesc = lpfc_sli4_queue_alloc(phba,
						      LPFC_DEFAULT_PAGE_SIZE,
						      phba->sli4_hba.rq_esize,
						      LPFC_NVMET_RQE_DEF_COUNT,
						      cpu);
			if (!qdesc) {
				lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
						"3146 Failed allocate "
						"receive HRQ\n");
				goto out_error;
			}
			qdesc->hdwq = idx;
			phba->sli4_hba.nvmet_mrq_hdr[idx] = qdesc;

			/* Only needed for header of RQ pair */
			qdesc->rqbp = kzalloc_node(sizeof(*qdesc->rqbp),
						   GFP_KERNEL,
						   cpu_to_node(cpu));
			if (qdesc->rqbp == NULL) {
				lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
						"6131 Failed allocate "
						"Header RQBP\n");
				goto out_error;
			}

			/* Put list in known state in case driver load fails. */
			INIT_LIST_HEAD(&qdesc->rqbp->rqb_buffer_list);

			/* Create NVMET Receive Queue for data */
			qdesc = lpfc_sli4_queue_alloc(phba,
						      LPFC_DEFAULT_PAGE_SIZE,
						      phba->sli4_hba.rq_esize,
						      LPFC_NVMET_RQE_DEF_COUNT,
						      cpu);
			if (!qdesc) {
				lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
						"3156 Failed allocate "
						"receive DRQ\n");
				goto out_error;
			}
			qdesc->hdwq = idx;
			phba->sli4_hba.nvmet_mrq_data[idx] = qdesc;
		}
	}

	/* Clear NVME stats */
	if (phba->cfg_enable_fc4_type & LPFC_ENABLE_NVME) {
		for (idx = 0; idx < phba->cfg_hdw_queue; idx++) {
			memset(&phba->sli4_hba.hdwq[idx].nvme_cstat, 0,
			       sizeof(phba->sli4_hba.hdwq[idx].nvme_cstat));
		}
	}

	/* Clear SCSI stats */
	if (phba->cfg_enable_fc4_type & LPFC_ENABLE_FCP) {
		for (idx = 0; idx < phba->cfg_hdw_queue; idx++) {
			memset(&phba->sli4_hba.hdwq[idx].scsi_cstat, 0,
			       sizeof(phba->sli4_hba.hdwq[idx].scsi_cstat));
		}
	}

	return 0;

out_error:
	lpfc_sli4_queue_destroy(phba);
	return -ENOMEM;
}

static inline void
__lpfc_sli4_release_queue(struct lpfc_queue **qp)
{
	if (*qp != NULL) {
		lpfc_sli4_queue_free(*qp);
		*qp = NULL;
	}
}

static inline void
lpfc_sli4_release_queues(struct lpfc_queue ***qs, int max)
{
	int idx;

	if (*qs == NULL)
		return;

	for (idx = 0; idx < max; idx++)
		__lpfc_sli4_release_queue(&(*qs)[idx]);

	kfree(*qs);
	*qs = NULL;
}

static inline void
lpfc_sli4_release_hdwq(struct lpfc_hba *phba)
{
	struct lpfc_sli4_hdw_queue *hdwq;
	struct lpfc_queue *eq;
	uint32_t idx;

	hdwq = phba->sli4_hba.hdwq;

	/* Loop thru all Hardware Queues */
	for (idx = 0; idx < phba->cfg_hdw_queue; idx++) {
		/* Free the CQ/WQ corresponding to the Hardware Queue */
		lpfc_sli4_queue_free(hdwq[idx].io_cq);
		lpfc_sli4_queue_free(hdwq[idx].io_wq);
		hdwq[idx].io_cq = NULL;
		hdwq[idx].io_wq = NULL;
		if (phba->cfg_xpsgl && !phba->nvmet_support)
			lpfc_free_sgl_per_hdwq(phba, &hdwq[idx]);
		lpfc_free_cmd_rsp_buf_per_hdwq(phba, &hdwq[idx]);
	}
	/* Loop thru all IRQ vectors */
	for (idx = 0; idx < phba->cfg_irq_chann; idx++) {
		/* Free the EQ corresponding to the IRQ vector */
		eq = phba->sli4_hba.hba_eq_hdl[idx].eq;
		lpfc_sli4_queue_free(eq);
		phba->sli4_hba.hba_eq_hdl[idx].eq = NULL;
	}
}

/**
 * lpfc_sli4_queue_destroy - Destroy all the SLI4 queues
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to release all the SLI4 queues with the FCoE HBA
 * operation.
 *
 * Return codes
 *      0 - successful
 *      -ENOMEM - No available memory
 *      -EIO - The mailbox failed to complete successfully.
 **/
void
lpfc_sli4_queue_destroy(struct lpfc_hba *phba)
{
	/*
	 * Set FREE_INIT before beginning to free the queues.
	 * Wait until the users of queues to acknowledge to
	 * release queues by clearing FREE_WAIT.
	 */
	spin_lock_irq(&phba->hbalock);
	phba->sli.sli_flag |= LPFC_QUEUE_FREE_INIT;
	while (phba->sli.sli_flag & LPFC_QUEUE_FREE_WAIT) {
		spin_unlock_irq(&phba->hbalock);
		msleep(20);
		spin_lock_irq(&phba->hbalock);
	}
	spin_unlock_irq(&phba->hbalock);

	lpfc_sli4_cleanup_poll_list(phba);

	/* Release HBA eqs */
	if (phba->sli4_hba.hdwq)
		lpfc_sli4_release_hdwq(phba);

	if (phba->nvmet_support) {
		lpfc_sli4_release_queues(&phba->sli4_hba.nvmet_cqset,
					 phba->cfg_nvmet_mrq);

		lpfc_sli4_release_queues(&phba->sli4_hba.nvmet_mrq_hdr,
					 phba->cfg_nvmet_mrq);
		lpfc_sli4_release_queues(&phba->sli4_hba.nvmet_mrq_data,
					 phba->cfg_nvmet_mrq);
	}

	/* Release mailbox command work queue */
	__lpfc_sli4_release_queue(&phba->sli4_hba.mbx_wq);

	/* Release ELS work queue */
	__lpfc_sli4_release_queue(&phba->sli4_hba.els_wq);

	/* Release ELS work queue */
	__lpfc_sli4_release_queue(&phba->sli4_hba.nvmels_wq);

	/* Release unsolicited receive queue */
	__lpfc_sli4_release_queue(&phba->sli4_hba.hdr_rq);
	__lpfc_sli4_release_queue(&phba->sli4_hba.dat_rq);

	/* Release ELS complete queue */
	__lpfc_sli4_release_queue(&phba->sli4_hba.els_cq);

	/* Release NVME LS complete queue */
	__lpfc_sli4_release_queue(&phba->sli4_hba.nvmels_cq);

	/* Release mailbox command complete queue */
	__lpfc_sli4_release_queue(&phba->sli4_hba.mbx_cq);

	/* Everything on this list has been freed */
	INIT_LIST_HEAD(&phba->sli4_hba.lpfc_wq_list);

	/* Done with freeing the queues */
	spin_lock_irq(&phba->hbalock);
	phba->sli.sli_flag &= ~LPFC_QUEUE_FREE_INIT;
	spin_unlock_irq(&phba->hbalock);
}

int
lpfc_free_rq_buffer(struct lpfc_hba *phba, struct lpfc_queue *rq)
{
	struct lpfc_rqb *rqbp;
	struct lpfc_dmabuf *h_buf;
	struct rqb_dmabuf *rqb_buffer;

	rqbp = rq->rqbp;
	while (!list_empty(&rqbp->rqb_buffer_list)) {
		list_remove_head(&rqbp->rqb_buffer_list, h_buf,
				 struct lpfc_dmabuf, list);

		rqb_buffer = container_of(h_buf, struct rqb_dmabuf, hbuf);
		(rqbp->rqb_free_buffer)(phba, rqb_buffer);
		rqbp->buffer_count--;
	}
	return 1;
}

static int
lpfc_create_wq_cq(struct lpfc_hba *phba, struct lpfc_queue *eq,
	struct lpfc_queue *cq, struct lpfc_queue *wq, uint16_t *cq_map,
	int qidx, uint32_t qtype)
{
	struct lpfc_sli_ring *pring;
	int rc;

	if (!eq || !cq || !wq) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
			"6085 Fast-path %s (%d) not allocated\n",
			((eq) ? ((cq) ? "WQ" : "CQ") : "EQ"), qidx);
		return -ENOMEM;
	}

	/* create the Cq first */
	rc = lpfc_cq_create(phba, cq, eq,
			(qtype == LPFC_MBOX) ? LPFC_MCQ : LPFC_WCQ, qtype);
	if (rc) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
			"6086 Failed setup of CQ (%d), rc = 0x%x\n",
			qidx, (uint32_t)rc);
		return rc;
	}

	if (qtype != LPFC_MBOX) {
		/* Setup cq_map for fast lookup */
		if (cq_map)
			*cq_map = cq->queue_id;

		lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
			"6087 CQ setup: cq[%d]-id=%d, parent eq[%d]-id=%d\n",
			qidx, cq->queue_id, qidx, eq->queue_id);

		/* create the wq */
		rc = lpfc_wq_create(phba, wq, cq, qtype);
		if (rc) {
			lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"4618 Fail setup fastpath WQ (%d), rc = 0x%x\n",
				qidx, (uint32_t)rc);
			/* no need to tear down cq - caller will do so */
			return rc;
		}

		/* Bind this CQ/WQ to the NVME ring */
		pring = wq->pring;
		pring->sli.sli4.wqp = (void *)wq;
		cq->pring = pring;

		lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
			"2593 WQ setup: wq[%d]-id=%d assoc=%d, cq[%d]-id=%d\n",
			qidx, wq->queue_id, wq->assoc_qid, qidx, cq->queue_id);
	} else {
		rc = lpfc_mq_create(phba, wq, cq, LPFC_MBOX);
		if (rc) {
			lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0539 Failed setup of slow-path MQ: "
				"rc = 0x%x\n", rc);
			/* no need to tear down cq - caller will do so */
			return rc;
		}

		lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
			"2589 MBX MQ setup: wq-id=%d, parent cq-id=%d\n",
			phba->sli4_hba.mbx_wq->queue_id,
			phba->sli4_hba.mbx_cq->queue_id);
	}

	return 0;
}

/**
 * lpfc_setup_cq_lookup - Setup the CQ lookup table
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine will populate the cq_lookup table by all
 * available CQ queue_id's.
 **/
static void
lpfc_setup_cq_lookup(struct lpfc_hba *phba)
{
	struct lpfc_queue *eq, *childq;
	int qidx;

	memset(phba->sli4_hba.cq_lookup, 0,
	       (sizeof(struct lpfc_queue *) * (phba->sli4_hba.cq_max + 1)));
	/* Loop thru all IRQ vectors */
	for (qidx = 0; qidx < phba->cfg_irq_chann; qidx++) {
		/* Get the EQ corresponding to the IRQ vector */
		eq = phba->sli4_hba.hba_eq_hdl[qidx].eq;
		if (!eq)
			continue;
		/* Loop through all CQs associated with that EQ */
		list_for_each_entry(childq, &eq->child_list, list) {
			if (childq->queue_id > phba->sli4_hba.cq_max)
				continue;
			if (childq->subtype == LPFC_IO)
				phba->sli4_hba.cq_lookup[childq->queue_id] =
					childq;
		}
	}
}

/**
 * lpfc_sli4_queue_setup - Set up all the SLI4 queues
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to set up all the SLI4 queues for the FCoE HBA
 * operation.
 *
 * Return codes
 *      0 - successful
 *      -ENOMEM - No available memory
 *      -EIO - The mailbox failed to complete successfully.
 **/
int
lpfc_sli4_queue_setup(struct lpfc_hba *phba)
{
	uint32_t shdr_status, shdr_add_status;
	union lpfc_sli4_cfg_shdr *shdr;
	struct lpfc_vector_map_info *cpup;
	struct lpfc_sli4_hdw_queue *qp;
	LPFC_MBOXQ_t *mboxq;
	int qidx, cpu;
	uint32_t length, usdelay;
	int rc = -ENOMEM;

	/* Check for dual-ULP support */
	mboxq = (LPFC_MBOXQ_t *)mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!mboxq) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"3249 Unable to allocate memory for "
				"QUERY_FW_CFG mailbox command\n");
		return -ENOMEM;
	}
	length = (sizeof(struct lpfc_mbx_query_fw_config) -
		  sizeof(struct lpfc_sli4_cfg_mhdr));
	lpfc_sli4_config(phba, mboxq, LPFC_MBOX_SUBSYSTEM_COMMON,
			 LPFC_MBOX_OPCODE_QUERY_FW_CFG,
			 length, LPFC_SLI4_MBX_EMBED);

	rc = lpfc_sli_issue_mbox(phba, mboxq, MBX_POLL);

	shdr = (union lpfc_sli4_cfg_shdr *)
			&mboxq->u.mqe.un.sli4_config.header.cfg_shdr;
	shdr_status = bf_get(lpfc_mbox_hdr_status, &shdr->response);
	shdr_add_status = bf_get(lpfc_mbox_hdr_add_status, &shdr->response);
	if (shdr_status || shdr_add_status || rc) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"3250 QUERY_FW_CFG mailbox failed with status "
				"x%x add_status x%x, mbx status x%x\n",
				shdr_status, shdr_add_status, rc);
		if (rc != MBX_TIMEOUT)
			mempool_free(mboxq, phba->mbox_mem_pool);
		rc = -ENXIO;
		goto out_error;
	}

	phba->sli4_hba.fw_func_mode =
			mboxq->u.mqe.un.query_fw_cfg.rsp.function_mode;
	phba->sli4_hba.ulp0_mode = mboxq->u.mqe.un.query_fw_cfg.rsp.ulp0_mode;
	phba->sli4_hba.ulp1_mode = mboxq->u.mqe.un.query_fw_cfg.rsp.ulp1_mode;
	phba->sli4_hba.physical_port =
			mboxq->u.mqe.un.query_fw_cfg.rsp.physical_port;
	lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
			"3251 QUERY_FW_CFG: func_mode:x%x, ulp0_mode:x%x, "
			"ulp1_mode:x%x\n", phba->sli4_hba.fw_func_mode,
			phba->sli4_hba.ulp0_mode, phba->sli4_hba.ulp1_mode);

	if (rc != MBX_TIMEOUT)
		mempool_free(mboxq, phba->mbox_mem_pool);

	/*
	 * Set up HBA Event Queues (EQs)
	 */
	qp = phba->sli4_hba.hdwq;

	/* Set up HBA event queue */
	if (!qp) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"3147 Fast-path EQs not allocated\n");
		rc = -ENOMEM;
		goto out_error;
	}

	/* Loop thru all IRQ vectors */
	for (qidx = 0; qidx < phba->cfg_irq_chann; qidx++) {
		/* Create HBA Event Queues (EQs) in order */
		for_each_present_cpu(cpu) {
			cpup = &phba->sli4_hba.cpu_map[cpu];

			/* Look for the CPU thats using that vector with
			 * LPFC_CPU_FIRST_IRQ set.
			 */
			if (!(cpup->flag & LPFC_CPU_FIRST_IRQ))
				continue;
			if (qidx != cpup->eq)
				continue;

			/* Create an EQ for that vector */
			rc = lpfc_eq_create(phba, qp[cpup->hdwq].hba_eq,
					    phba->cfg_fcp_imax);
			if (rc) {
				lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
						"0523 Failed setup of fast-path"
						" EQ (%d), rc = 0x%x\n",
						cpup->eq, (uint32_t)rc);
				goto out_destroy;
			}

			/* Save the EQ for that vector in the hba_eq_hdl */
			phba->sli4_hba.hba_eq_hdl[cpup->eq].eq =
				qp[cpup->hdwq].hba_eq;

			lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
					"2584 HBA EQ setup: queue[%d]-id=%d\n",
					cpup->eq,
					qp[cpup->hdwq].hba_eq->queue_id);
		}
	}

	/* Loop thru all Hardware Queues */
	for (qidx = 0; qidx < phba->cfg_hdw_queue; qidx++) {
		cpu = lpfc_find_cpu_handle(phba, qidx, LPFC_FIND_BY_HDWQ);
		cpup = &phba->sli4_hba.cpu_map[cpu];

		/* Create the CQ/WQ corresponding to the Hardware Queue */
		rc = lpfc_create_wq_cq(phba,
				       phba->sli4_hba.hdwq[cpup->hdwq].hba_eq,
				       qp[qidx].io_cq,
				       qp[qidx].io_wq,
				       &phba->sli4_hba.hdwq[qidx].io_cq_map,
				       qidx,
				       LPFC_IO);
		if (rc) {
			lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
					"0535 Failed to setup fastpath "
					"IO WQ/CQ (%d), rc = 0x%x\n",
					qidx, (uint32_t)rc);
			goto out_destroy;
		}
	}

	/*
	 * Set up Slow Path Complete Queues (CQs)
	 */

	/* Set up slow-path MBOX CQ/MQ */

	if (!phba->sli4_hba.mbx_cq || !phba->sli4_hba.mbx_wq) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0528 %s not allocated\n",
				phba->sli4_hba.mbx_cq ?
				"Mailbox WQ" : "Mailbox CQ");
		rc = -ENOMEM;
		goto out_destroy;
	}

	rc = lpfc_create_wq_cq(phba, qp[0].hba_eq,
			       phba->sli4_hba.mbx_cq,
			       phba->sli4_hba.mbx_wq,
			       NULL, 0, LPFC_MBOX);
	if (rc) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
			"0529 Failed setup of mailbox WQ/CQ: rc = 0x%x\n",
			(uint32_t)rc);
		goto out_destroy;
	}
	if (phba->nvmet_support) {
		if (!phba->sli4_hba.nvmet_cqset) {
			lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
					"3165 Fast-path NVME CQ Set "
					"array not allocated\n");
			rc = -ENOMEM;
			goto out_destroy;
		}
		if (phba->cfg_nvmet_mrq > 1) {
			rc = lpfc_cq_create_set(phba,
					phba->sli4_hba.nvmet_cqset,
					qp,
					LPFC_WCQ, LPFC_NVMET);
			if (rc) {
				lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
						"3164 Failed setup of NVME CQ "
						"Set, rc = 0x%x\n",
						(uint32_t)rc);
				goto out_destroy;
			}
		} else {
			/* Set up NVMET Receive Complete Queue */
			rc = lpfc_cq_create(phba, phba->sli4_hba.nvmet_cqset[0],
					    qp[0].hba_eq,
					    LPFC_WCQ, LPFC_NVMET);
			if (rc) {
				lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
						"6089 Failed setup NVMET CQ: "
						"rc = 0x%x\n", (uint32_t)rc);
				goto out_destroy;
			}
			phba->sli4_hba.nvmet_cqset[0]->chann = 0;

			lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
					"6090 NVMET CQ setup: cq-id=%d, "
					"parent eq-id=%d\n",
					phba->sli4_hba.nvmet_cqset[0]->queue_id,
					qp[0].hba_eq->queue_id);
		}
	}

	/* Set up slow-path ELS WQ/CQ */
	if (!phba->sli4_hba.els_cq || !phba->sli4_hba.els_wq) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0530 ELS %s not allocated\n",
				phba->sli4_hba.els_cq ? "WQ" : "CQ");
		rc = -ENOMEM;
		goto out_destroy;
	}
	rc = lpfc_create_wq_cq(phba, qp[0].hba_eq,
			       phba->sli4_hba.els_cq,
			       phba->sli4_hba.els_wq,
			       NULL, 0, LPFC_ELS);
	if (rc) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0525 Failed setup of ELS WQ/CQ: rc = 0x%x\n",
				(uint32_t)rc);
		goto out_destroy;
	}
	lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
			"2590 ELS WQ setup: wq-id=%d, parent cq-id=%d\n",
			phba->sli4_hba.els_wq->queue_id,
			phba->sli4_hba.els_cq->queue_id);

	if (phba->cfg_enable_fc4_type & LPFC_ENABLE_NVME) {
		/* Set up NVME LS Complete Queue */
		if (!phba->sli4_hba.nvmels_cq || !phba->sli4_hba.nvmels_wq) {
			lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
					"6091 LS %s not allocated\n",
					phba->sli4_hba.nvmels_cq ? "WQ" : "CQ");
			rc = -ENOMEM;
			goto out_destroy;
		}
		rc = lpfc_create_wq_cq(phba, qp[0].hba_eq,
				       phba->sli4_hba.nvmels_cq,
				       phba->sli4_hba.nvmels_wq,
				       NULL, 0, LPFC_NVME_LS);
		if (rc) {
			lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
					"0526 Failed setup of NVVME LS WQ/CQ: "
					"rc = 0x%x\n", (uint32_t)rc);
			goto out_destroy;
		}

		lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
				"6096 ELS WQ setup: wq-id=%d, "
				"parent cq-id=%d\n",
				phba->sli4_hba.nvmels_wq->queue_id,
				phba->sli4_hba.nvmels_cq->queue_id);
	}

	/*
	 * Create NVMET Receive Queue (RQ)
	 */
	if (phba->nvmet_support) {
		if ((!phba->sli4_hba.nvmet_cqset) ||
		    (!phba->sli4_hba.nvmet_mrq_hdr) ||
		    (!phba->sli4_hba.nvmet_mrq_data)) {
			lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
					"6130 MRQ CQ Queues not "
					"allocated\n");
			rc = -ENOMEM;
			goto out_destroy;
		}
		if (phba->cfg_nvmet_mrq > 1) {
			rc = lpfc_mrq_create(phba,
					     phba->sli4_hba.nvmet_mrq_hdr,
					     phba->sli4_hba.nvmet_mrq_data,
					     phba->sli4_hba.nvmet_cqset,
					     LPFC_NVMET);
			if (rc) {
				lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
						"6098 Failed setup of NVMET "
						"MRQ: rc = 0x%x\n",
						(uint32_t)rc);
				goto out_destroy;
			}

		} else {
			rc = lpfc_rq_create(phba,
					    phba->sli4_hba.nvmet_mrq_hdr[0],
					    phba->sli4_hba.nvmet_mrq_data[0],
					    phba->sli4_hba.nvmet_cqset[0],
					    LPFC_NVMET);
			if (rc) {
				lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
						"6057 Failed setup of NVMET "
						"Receive Queue: rc = 0x%x\n",
						(uint32_t)rc);
				goto out_destroy;
			}

			lpfc_printf_log(
				phba, KERN_INFO, LOG_INIT,
				"6099 NVMET RQ setup: hdr-rq-id=%d, "
				"dat-rq-id=%d parent cq-id=%d\n",
				phba->sli4_hba.nvmet_mrq_hdr[0]->queue_id,
				phba->sli4_hba.nvmet_mrq_data[0]->queue_id,
				phba->sli4_hba.nvmet_cqset[0]->queue_id);

		}
	}

	if (!phba->sli4_hba.hdr_rq || !phba->sli4_hba.dat_rq) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0540 Receive Queue not allocated\n");
		rc = -ENOMEM;
		goto out_destroy;
	}

	rc = lpfc_rq_create(phba, phba->sli4_hba.hdr_rq, phba->sli4_hba.dat_rq,
			    phba->sli4_hba.els_cq, LPFC_USOL);
	if (rc) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0541 Failed setup of Receive Queue: "
				"rc = 0x%x\n", (uint32_t)rc);
		goto out_destroy;
	}

	lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
			"2592 USL RQ setup: hdr-rq-id=%d, dat-rq-id=%d "
			"parent cq-id=%d\n",
			phba->sli4_hba.hdr_rq->queue_id,
			phba->sli4_hba.dat_rq->queue_id,
			phba->sli4_hba.els_cq->queue_id);

	if (phba->cfg_fcp_imax)
		usdelay = LPFC_SEC_TO_USEC / phba->cfg_fcp_imax;
	else
		usdelay = 0;

	for (qidx = 0; qidx < phba->cfg_irq_chann;
	     qidx += LPFC_MAX_EQ_DELAY_EQID_CNT)
		lpfc_modify_hba_eq_delay(phba, qidx, LPFC_MAX_EQ_DELAY_EQID_CNT,
					 usdelay);

	if (phba->sli4_hba.cq_max) {
		kfree(phba->sli4_hba.cq_lookup);
		phba->sli4_hba.cq_lookup = kcalloc((phba->sli4_hba.cq_max + 1),
			sizeof(struct lpfc_queue *), GFP_KERNEL);
		if (!phba->sli4_hba.cq_lookup) {
			lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
					"0549 Failed setup of CQ Lookup table: "
					"size 0x%x\n", phba->sli4_hba.cq_max);
			rc = -ENOMEM;
			goto out_destroy;
		}
		lpfc_setup_cq_lookup(phba);
	}
	return 0;

out_destroy:
	lpfc_sli4_queue_unset(phba);
out_error:
	return rc;
}

/**
 * lpfc_sli4_queue_unset - Unset all the SLI4 queues
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to unset all the SLI4 queues with the FCoE HBA
 * operation.
 *
 * Return codes
 *      0 - successful
 *      -ENOMEM - No available memory
 *      -EIO - The mailbox failed to complete successfully.
 **/
void
lpfc_sli4_queue_unset(struct lpfc_hba *phba)
{
	struct lpfc_sli4_hdw_queue *qp;
	struct lpfc_queue *eq;
	int qidx;

	/* Unset mailbox command work queue */
	if (phba->sli4_hba.mbx_wq)
		lpfc_mq_destroy(phba, phba->sli4_hba.mbx_wq);

	/* Unset NVME LS work queue */
	if (phba->sli4_hba.nvmels_wq)
		lpfc_wq_destroy(phba, phba->sli4_hba.nvmels_wq);

	/* Unset ELS work queue */
	if (phba->sli4_hba.els_wq)
		lpfc_wq_destroy(phba, phba->sli4_hba.els_wq);

	/* Unset unsolicited receive queue */
	if (phba->sli4_hba.hdr_rq)
		lpfc_rq_destroy(phba, phba->sli4_hba.hdr_rq,
				phba->sli4_hba.dat_rq);

	/* Unset mailbox command complete queue */
	if (phba->sli4_hba.mbx_cq)
		lpfc_cq_destroy(phba, phba->sli4_hba.mbx_cq);

	/* Unset ELS complete queue */
	if (phba->sli4_hba.els_cq)
		lpfc_cq_destroy(phba, phba->sli4_hba.els_cq);

	/* Unset NVME LS complete queue */
	if (phba->sli4_hba.nvmels_cq)
		lpfc_cq_destroy(phba, phba->sli4_hba.nvmels_cq);

	if (phba->nvmet_support) {
		/* Unset NVMET MRQ queue */
		if (phba->sli4_hba.nvmet_mrq_hdr) {
			for (qidx = 0; qidx < phba->cfg_nvmet_mrq; qidx++)
				lpfc_rq_destroy(
					phba,
					phba->sli4_hba.nvmet_mrq_hdr[qidx],
					phba->sli4_hba.nvmet_mrq_data[qidx]);
		}

		/* Unset NVMET CQ Set complete queue */
		if (phba->sli4_hba.nvmet_cqset) {
			for (qidx = 0; qidx < phba->cfg_nvmet_mrq; qidx++)
				lpfc_cq_destroy(
					phba, phba->sli4_hba.nvmet_cqset[qidx]);
		}
	}

	/* Unset fast-path SLI4 queues */
	if (phba->sli4_hba.hdwq) {
		/* Loop thru all Hardware Queues */
		for (qidx = 0; qidx < phba->cfg_hdw_queue; qidx++) {
			/* Destroy the CQ/WQ corresponding to Hardware Queue */
			qp = &phba->sli4_hba.hdwq[qidx];
			lpfc_wq_destroy(phba, qp->io_wq);
			lpfc_cq_destroy(phba, qp->io_cq);
		}
		/* Loop thru all IRQ vectors */
		for (qidx = 0; qidx < phba->cfg_irq_chann; qidx++) {
			/* Destroy the EQ corresponding to the IRQ vector */
			eq = phba->sli4_hba.hba_eq_hdl[qidx].eq;
			lpfc_eq_destroy(phba, eq);
		}
	}

	kfree(phba->sli4_hba.cq_lookup);
	phba->sli4_hba.cq_lookup = NULL;
	phba->sli4_hba.cq_max = 0;
}

/**
 * lpfc_sli4_cq_event_pool_create - Create completion-queue event free pool
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to allocate and set up a pool of completion queue
 * events. The body of the completion queue event is a completion queue entry
 * CQE. For now, this pool is used for the interrupt service routine to queue
 * the following HBA completion queue events for the worker thread to process:
 *   - Mailbox asynchronous events
 *   - Receive queue completion unsolicited events
 * Later, this can be used for all the slow-path events.
 *
 * Return codes
 *      0 - successful
 *      -ENOMEM - No available memory
 **/
static int
lpfc_sli4_cq_event_pool_create(struct lpfc_hba *phba)
{
	struct lpfc_cq_event *cq_event;
	int i;

	for (i = 0; i < (4 * phba->sli4_hba.cq_ecount); i++) {
		cq_event = kmalloc(sizeof(struct lpfc_cq_event), GFP_KERNEL);
		if (!cq_event)
			goto out_pool_create_fail;
		list_add_tail(&cq_event->list,
			      &phba->sli4_hba.sp_cqe_event_pool);
	}
	return 0;

out_pool_create_fail:
	lpfc_sli4_cq_event_pool_destroy(phba);
	return -ENOMEM;
}

/**
 * lpfc_sli4_cq_event_pool_destroy - Free completion-queue event free pool
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to free the pool of completion queue events at
 * driver unload time. Note that, it is the responsibility of the driver
 * cleanup routine to free all the outstanding completion-queue events
 * allocated from this pool back into the pool before invoking this routine
 * to destroy the pool.
 **/
static void
lpfc_sli4_cq_event_pool_destroy(struct lpfc_hba *phba)
{
	struct lpfc_cq_event *cq_event, *next_cq_event;

	list_for_each_entry_safe(cq_event, next_cq_event,
				 &phba->sli4_hba.sp_cqe_event_pool, list) {
		list_del(&cq_event->list);
		kfree(cq_event);
	}
}

/**
 * __lpfc_sli4_cq_event_alloc - Allocate a completion-queue event from free pool
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is the lock free version of the API invoked to allocate a
 * completion-queue event from the free pool.
 *
 * Return: Pointer to the newly allocated completion-queue event if successful
 *         NULL otherwise.
 **/
struct lpfc_cq_event *
__lpfc_sli4_cq_event_alloc(struct lpfc_hba *phba)
{
	struct lpfc_cq_event *cq_event = NULL;

	list_remove_head(&phba->sli4_hba.sp_cqe_event_pool, cq_event,
			 struct lpfc_cq_event, list);
	return cq_event;
}

/**
 * lpfc_sli4_cq_event_alloc - Allocate a completion-queue event from free pool
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is the lock version of the API invoked to allocate a
 * completion-queue event from the free pool.
 *
 * Return: Pointer to the newly allocated completion-queue event if successful
 *         NULL otherwise.
 **/
struct lpfc_cq_event *
lpfc_sli4_cq_event_alloc(struct lpfc_hba *phba)
{
	struct lpfc_cq_event *cq_event;
	unsigned long iflags;

	spin_lock_irqsave(&phba->hbalock, iflags);
	cq_event = __lpfc_sli4_cq_event_alloc(phba);
	spin_unlock_irqrestore(&phba->hbalock, iflags);
	return cq_event;
}

/**
 * __lpfc_sli4_cq_event_release - Release a completion-queue event to free pool
 * @phba: pointer to lpfc hba data structure.
 * @cq_event: pointer to the completion queue event to be freed.
 *
 * This routine is the lock free version of the API invoked to release a
 * completion-queue event back into the free pool.
 **/
void
__lpfc_sli4_cq_event_release(struct lpfc_hba *phba,
			     struct lpfc_cq_event *cq_event)
{
	list_add_tail(&cq_event->list, &phba->sli4_hba.sp_cqe_event_pool);
}

/**
 * lpfc_sli4_cq_event_release - Release a completion-queue event to free pool
 * @phba: pointer to lpfc hba data structure.
 * @cq_event: pointer to the completion queue event to be freed.
 *
 * This routine is the lock version of the API invoked to release a
 * completion-queue event back into the free pool.
 **/
void
lpfc_sli4_cq_event_release(struct lpfc_hba *phba,
			   struct lpfc_cq_event *cq_event)
{
	unsigned long iflags;
	spin_lock_irqsave(&phba->hbalock, iflags);
	__lpfc_sli4_cq_event_release(phba, cq_event);
	spin_unlock_irqrestore(&phba->hbalock, iflags);
}

/**
 * lpfc_sli4_cq_event_release_all - Release all cq events to the free pool
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is to free all the pending completion-queue events to the
 * back into the free pool for device reset.
 **/
static void
lpfc_sli4_cq_event_release_all(struct lpfc_hba *phba)
{
	LIST_HEAD(cqelist);
	struct lpfc_cq_event *cqe;
	unsigned long iflags;

	/* Retrieve all the pending WCQEs from pending WCQE lists */
	spin_lock_irqsave(&phba->hbalock, iflags);
	/* Pending FCP XRI abort events */
	list_splice_init(&phba->sli4_hba.sp_fcp_xri_aborted_work_queue,
			 &cqelist);
	/* Pending ELS XRI abort events */
	list_splice_init(&phba->sli4_hba.sp_els_xri_aborted_work_queue,
			 &cqelist);
	/* Pending asynnc events */
	list_splice_init(&phba->sli4_hba.sp_asynce_work_queue,
			 &cqelist);
	spin_unlock_irqrestore(&phba->hbalock, iflags);

	while (!list_empty(&cqelist)) {
		list_remove_head(&cqelist, cqe, struct lpfc_cq_event, list);
		lpfc_sli4_cq_event_release(phba, cqe);
	}
}

/**
 * lpfc_pci_function_reset - Reset pci function.
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to request a PCI function reset. It will destroys
 * all resources assigned to the PCI function which originates this request.
 *
 * Return codes
 *      0 - successful
 *      -ENOMEM - No available memory
 *      -EIO - The mailbox failed to complete successfully.
 **/
int
lpfc_pci_function_reset(struct lpfc_hba *phba)
{
	LPFC_MBOXQ_t *mboxq;
	uint32_t rc = 0, if_type;
	uint32_t shdr_status, shdr_add_status;
	uint32_t rdy_chk;
	uint32_t port_reset = 0;
	union lpfc_sli4_cfg_shdr *shdr;
	struct lpfc_register reg_data;
	uint16_t devid;

	if_type = bf_get(lpfc_sli_intf_if_type, &phba->sli4_hba.sli_intf);
	switch (if_type) {
	case LPFC_SLI_INTF_IF_TYPE_0:
		mboxq = (LPFC_MBOXQ_t *) mempool_alloc(phba->mbox_mem_pool,
						       GFP_KERNEL);
		if (!mboxq) {
			lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
					"0494 Unable to allocate memory for "
					"issuing SLI_FUNCTION_RESET mailbox "
					"command\n");
			return -ENOMEM;
		}

		/* Setup PCI function reset mailbox-ioctl command */
		lpfc_sli4_config(phba, mboxq, LPFC_MBOX_SUBSYSTEM_COMMON,
				 LPFC_MBOX_OPCODE_FUNCTION_RESET, 0,
				 LPFC_SLI4_MBX_EMBED);
		rc = lpfc_sli_issue_mbox(phba, mboxq, MBX_POLL);
		shdr = (union lpfc_sli4_cfg_shdr *)
			&mboxq->u.mqe.un.sli4_config.header.cfg_shdr;
		shdr_status = bf_get(lpfc_mbox_hdr_status, &shdr->response);
		shdr_add_status = bf_get(lpfc_mbox_hdr_add_status,
					 &shdr->response);
		if (rc != MBX_TIMEOUT)
			mempool_free(mboxq, phba->mbox_mem_pool);
		if (shdr_status || shdr_add_status || rc) {
			lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
					"0495 SLI_FUNCTION_RESET mailbox "
					"failed with status x%x add_status x%x,"
					" mbx status x%x\n",
					shdr_status, shdr_add_status, rc);
			rc = -ENXIO;
		}
		break;
	case LPFC_SLI_INTF_IF_TYPE_2:
	case LPFC_SLI_INTF_IF_TYPE_6:
wait:
		/*
		 * Poll the Port Status Register and wait for RDY for
		 * up to 30 seconds. If the port doesn't respond, treat
		 * it as an error.
		 */
		for (rdy_chk = 0; rdy_chk < 1500; rdy_chk++) {
			if (lpfc_readl(phba->sli4_hba.u.if_type2.
				STATUSregaddr, &reg_data.word0)) {
				rc = -ENODEV;
				goto out;
			}
			if (bf_get(lpfc_sliport_status_rdy, &reg_data))
				break;
			msleep(20);
		}

		if (!bf_get(lpfc_sliport_status_rdy, &reg_data)) {
			phba->work_status[0] = readl(
				phba->sli4_hba.u.if_type2.ERR1regaddr);
			phba->work_status[1] = readl(
				phba->sli4_hba.u.if_type2.ERR2regaddr);
			lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
					"2890 Port not ready, port status reg "
					"0x%x error 1=0x%x, error 2=0x%x\n",
					reg_data.word0,
					phba->work_status[0],
					phba->work_status[1]);
			rc = -ENODEV;
			goto out;
		}

		if (!port_reset) {
			/*
			 * Reset the port now
			 */
			reg_data.word0 = 0;
			bf_set(lpfc_sliport_ctrl_end, &reg_data,
			       LPFC_SLIPORT_LITTLE_ENDIAN);
			bf_set(lpfc_sliport_ctrl_ip, &reg_data,
			       LPFC_SLIPORT_INIT_PORT);
			writel(reg_data.word0, phba->sli4_hba.u.if_type2.
			       CTRLregaddr);
			/* flush */
			pci_read_config_word(phba->pcidev,
					     PCI_DEVICE_ID, &devid);

			port_reset = 1;
			msleep(20);
			goto wait;
		} else if (bf_get(lpfc_sliport_status_rn, &reg_data)) {
			rc = -ENODEV;
			goto out;
		}
		break;

	case LPFC_SLI_INTF_IF_TYPE_1:
	default:
		break;
	}

out:
	/* Catch the not-ready port failure after a port reset. */
	if (rc) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"3317 HBA not functional: IP Reset Failed "
				"try: echo fw_reset > board_mode\n");
		rc = -ENODEV;
	}

	return rc;
}

/**
 * lpfc_sli4_pci_mem_setup - Setup SLI4 HBA PCI memory space.
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to set up the PCI device memory space for device
 * with SLI-4 interface spec.
 *
 * Return codes
 * 	0 - successful
 * 	other values - error
 **/
static int
lpfc_sli4_pci_mem_setup(struct lpfc_hba *phba)
{
	struct pci_dev *pdev = phba->pcidev;
	unsigned long bar0map_len, bar1map_len, bar2map_len;
	int error;
	uint32_t if_type;

	if (!pdev)
		return -ENODEV;

	/* Set the device DMA mask size */
	error = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (error)
		error = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (error)
		return error;

	/*
	 * The BARs and register set definitions and offset locations are
	 * dependent on the if_type.
	 */
	if (pci_read_config_dword(pdev, LPFC_SLI_INTF,
				  &phba->sli4_hba.sli_intf.word0)) {
		return -ENODEV;
	}

	/* There is no SLI3 failback for SLI4 devices. */
	if (bf_get(lpfc_sli_intf_valid, &phba->sli4_hba.sli_intf) !=
	    LPFC_SLI_INTF_VALID) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"2894 SLI_INTF reg contents invalid "
				"sli_intf reg 0x%x\n",
				phba->sli4_hba.sli_intf.word0);
		return -ENODEV;
	}

	if_type = bf_get(lpfc_sli_intf_if_type, &phba->sli4_hba.sli_intf);
	/*
	 * Get the bus address of SLI4 device Bar regions and the
	 * number of bytes required by each mapping. The mapping of the
	 * particular PCI BARs regions is dependent on the type of
	 * SLI4 device.
	 */
	if (pci_resource_start(pdev, PCI_64BIT_BAR0)) {
		phba->pci_bar0_map = pci_resource_start(pdev, PCI_64BIT_BAR0);
		bar0map_len = pci_resource_len(pdev, PCI_64BIT_BAR0);

		/*
		 * Map SLI4 PCI Config Space Register base to a kernel virtual
		 * addr
		 */
		phba->sli4_hba.conf_regs_memmap_p =
			ioremap(phba->pci_bar0_map, bar0map_len);
		if (!phba->sli4_hba.conf_regs_memmap_p) {
			dev_printk(KERN_ERR, &pdev->dev,
				   "ioremap failed for SLI4 PCI config "
				   "registers.\n");
			return -ENODEV;
		}
		phba->pci_bar0_memmap_p = phba->sli4_hba.conf_regs_memmap_p;
		/* Set up BAR0 PCI config space register memory map */
		lpfc_sli4_bar0_register_memmap(phba, if_type);
	} else {
		phba->pci_bar0_map = pci_resource_start(pdev, 1);
		bar0map_len = pci_resource_len(pdev, 1);
		if (if_type >= LPFC_SLI_INTF_IF_TYPE_2) {
			dev_printk(KERN_ERR, &pdev->dev,
			   "FATAL - No BAR0 mapping for SLI4, if_type 2\n");
			return -ENODEV;
		}
		phba->sli4_hba.conf_regs_memmap_p =
				ioremap(phba->pci_bar0_map, bar0map_len);
		if (!phba->sli4_hba.conf_regs_memmap_p) {
			dev_printk(KERN_ERR, &pdev->dev,
				"ioremap failed for SLI4 PCI config "
				"registers.\n");
			return -ENODEV;
		}
		lpfc_sli4_bar0_register_memmap(phba, if_type);
	}

	if (if_type == LPFC_SLI_INTF_IF_TYPE_0) {
		if (pci_resource_start(pdev, PCI_64BIT_BAR2)) {
			/*
			 * Map SLI4 if type 0 HBA Control Register base to a
			 * kernel virtual address and setup the registers.
			 */
			phba->pci_bar1_map = pci_resource_start(pdev,
								PCI_64BIT_BAR2);
			bar1map_len = pci_resource_len(pdev, PCI_64BIT_BAR2);
			phba->sli4_hba.ctrl_regs_memmap_p =
					ioremap(phba->pci_bar1_map,
						bar1map_len);
			if (!phba->sli4_hba.ctrl_regs_memmap_p) {
				dev_err(&pdev->dev,
					   "ioremap failed for SLI4 HBA "
					    "control registers.\n");
				error = -ENOMEM;
				goto out_iounmap_conf;
			}
			phba->pci_bar2_memmap_p =
					 phba->sli4_hba.ctrl_regs_memmap_p;
			lpfc_sli4_bar1_register_memmap(phba, if_type);
		} else {
			error = -ENOMEM;
			goto out_iounmap_conf;
		}
	}

	if ((if_type == LPFC_SLI_INTF_IF_TYPE_6) &&
	    (pci_resource_start(pdev, PCI_64BIT_BAR2))) {
		/*
		 * Map SLI4 if type 6 HBA Doorbell Register base to a kernel
		 * virtual address and setup the registers.
		 */
		phba->pci_bar1_map = pci_resource_start(pdev, PCI_64BIT_BAR2);
		bar1map_len = pci_resource_len(pdev, PCI_64BIT_BAR2);
		phba->sli4_hba.drbl_regs_memmap_p =
				ioremap(phba->pci_bar1_map, bar1map_len);
		if (!phba->sli4_hba.drbl_regs_memmap_p) {
			dev_err(&pdev->dev,
			   "ioremap failed for SLI4 HBA doorbell registers.\n");
			error = -ENOMEM;
			goto out_iounmap_conf;
		}
		phba->pci_bar2_memmap_p = phba->sli4_hba.drbl_regs_memmap_p;
		lpfc_sli4_bar1_register_memmap(phba, if_type);
	}

	if (if_type == LPFC_SLI_INTF_IF_TYPE_0) {
		if (pci_resource_start(pdev, PCI_64BIT_BAR4)) {
			/*
			 * Map SLI4 if type 0 HBA Doorbell Register base to
			 * a kernel virtual address and setup the registers.
			 */
			phba->pci_bar2_map = pci_resource_start(pdev,
								PCI_64BIT_BAR4);
			bar2map_len = pci_resource_len(pdev, PCI_64BIT_BAR4);
			phba->sli4_hba.drbl_regs_memmap_p =
					ioremap(phba->pci_bar2_map,
						bar2map_len);
			if (!phba->sli4_hba.drbl_regs_memmap_p) {
				dev_err(&pdev->dev,
					   "ioremap failed for SLI4 HBA"
					   " doorbell registers.\n");
				error = -ENOMEM;
				goto out_iounmap_ctrl;
			}
			phba->pci_bar4_memmap_p =
					phba->sli4_hba.drbl_regs_memmap_p;
			error = lpfc_sli4_bar2_register_memmap(phba, LPFC_VF0);
			if (error)
				goto out_iounmap_all;
		} else {
			error = -ENOMEM;
			goto out_iounmap_all;
		}
	}

	if (if_type == LPFC_SLI_INTF_IF_TYPE_6 &&
	    pci_resource_start(pdev, PCI_64BIT_BAR4)) {
		/*
		 * Map SLI4 if type 6 HBA DPP Register base to a kernel
		 * virtual address and setup the registers.
		 */
		phba->pci_bar2_map = pci_resource_start(pdev, PCI_64BIT_BAR4);
		bar2map_len = pci_resource_len(pdev, PCI_64BIT_BAR4);
		phba->sli4_hba.dpp_regs_memmap_p =
				ioremap(phba->pci_bar2_map, bar2map_len);
		if (!phba->sli4_hba.dpp_regs_memmap_p) {
			dev_err(&pdev->dev,
			   "ioremap failed for SLI4 HBA dpp registers.\n");
			error = -ENOMEM;
			goto out_iounmap_ctrl;
		}
		phba->pci_bar4_memmap_p = phba->sli4_hba.dpp_regs_memmap_p;
	}

	/* Set up the EQ/CQ register handeling functions now */
	switch (if_type) {
	case LPFC_SLI_INTF_IF_TYPE_0:
	case LPFC_SLI_INTF_IF_TYPE_2:
		phba->sli4_hba.sli4_eq_clr_intr = lpfc_sli4_eq_clr_intr;
		phba->sli4_hba.sli4_write_eq_db = lpfc_sli4_write_eq_db;
		phba->sli4_hba.sli4_write_cq_db = lpfc_sli4_write_cq_db;
		break;
	case LPFC_SLI_INTF_IF_TYPE_6:
		phba->sli4_hba.sli4_eq_clr_intr = lpfc_sli4_if6_eq_clr_intr;
		phba->sli4_hba.sli4_write_eq_db = lpfc_sli4_if6_write_eq_db;
		phba->sli4_hba.sli4_write_cq_db = lpfc_sli4_if6_write_cq_db;
		break;
	default:
		break;
	}

	return 0;

out_iounmap_all:
	iounmap(phba->sli4_hba.drbl_regs_memmap_p);
out_iounmap_ctrl:
	iounmap(phba->sli4_hba.ctrl_regs_memmap_p);
out_iounmap_conf:
	iounmap(phba->sli4_hba.conf_regs_memmap_p);

	return error;
}

/**
 * lpfc_sli4_pci_mem_unset - Unset SLI4 HBA PCI memory space.
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to unset the PCI device memory space for device
 * with SLI-4 interface spec.
 **/
static void
lpfc_sli4_pci_mem_unset(struct lpfc_hba *phba)
{
	uint32_t if_type;
	if_type = bf_get(lpfc_sli_intf_if_type, &phba->sli4_hba.sli_intf);

	switch (if_type) {
	case LPFC_SLI_INTF_IF_TYPE_0:
		iounmap(phba->sli4_hba.drbl_regs_memmap_p);
		iounmap(phba->sli4_hba.ctrl_regs_memmap_p);
		iounmap(phba->sli4_hba.conf_regs_memmap_p);
		break;
	case LPFC_SLI_INTF_IF_TYPE_2:
		iounmap(phba->sli4_hba.conf_regs_memmap_p);
		break;
	case LPFC_SLI_INTF_IF_TYPE_6:
		iounmap(phba->sli4_hba.drbl_regs_memmap_p);
		iounmap(phba->sli4_hba.conf_regs_memmap_p);
		break;
	case LPFC_SLI_INTF_IF_TYPE_1:
	default:
		dev_printk(KERN_ERR, &phba->pcidev->dev,
			   "FATAL - unsupported SLI4 interface type - %d\n",
			   if_type);
		break;
	}
}

/**
 * lpfc_sli_enable_msix - Enable MSI-X interrupt mode on SLI-3 device
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to enable the MSI-X interrupt vectors to device
 * with SLI-3 interface specs.
 *
 * Return codes
 *   0 - successful
 *   other values - error
 **/
static int
lpfc_sli_enable_msix(struct lpfc_hba *phba)
{
	int rc;
	LPFC_MBOXQ_t *pmb;

	/* Set up MSI-X multi-message vectors */
	rc = pci_alloc_irq_vectors(phba->pcidev,
			LPFC_MSIX_VECTORS, LPFC_MSIX_VECTORS, PCI_IRQ_MSIX);
	if (rc < 0) {
		lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
				"0420 PCI enable MSI-X failed (%d)\n", rc);
		goto vec_fail_out;
	}

	/*
	 * Assign MSI-X vectors to interrupt handlers
	 */

	/* vector-0 is associated to slow-path handler */
	rc = request_irq(pci_irq_vector(phba->pcidev, 0),
			 &lpfc_sli_sp_intr_handler, 0,
			 LPFC_SP_DRIVER_HANDLER_NAME, phba);
	if (rc) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_INIT,
				"0421 MSI-X slow-path request_irq failed "
				"(%d)\n", rc);
		goto msi_fail_out;
	}

	/* vector-1 is associated to fast-path handler */
	rc = request_irq(pci_irq_vector(phba->pcidev, 1),
			 &lpfc_sli_fp_intr_handler, 0,
			 LPFC_FP_DRIVER_HANDLER_NAME, phba);

	if (rc) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_INIT,
				"0429 MSI-X fast-path request_irq failed "
				"(%d)\n", rc);
		goto irq_fail_out;
	}

	/*
	 * Configure HBA MSI-X attention conditions to messages
	 */
	pmb = (LPFC_MBOXQ_t *) mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);

	if (!pmb) {
		rc = -ENOMEM;
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0474 Unable to allocate memory for issuing "
				"MBOX_CONFIG_MSI command\n");
		goto mem_fail_out;
	}
	rc = lpfc_config_msi(phba, pmb);
	if (rc)
		goto mbx_fail_out;
	rc = lpfc_sli_issue_mbox(phba, pmb, MBX_POLL);
	if (rc != MBX_SUCCESS) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_MBOX,
				"0351 Config MSI mailbox command failed, "
				"mbxCmd x%x, mbxStatus x%x\n",
				pmb->u.mb.mbxCommand, pmb->u.mb.mbxStatus);
		goto mbx_fail_out;
	}

	/* Free memory allocated for mailbox command */
	mempool_free(pmb, phba->mbox_mem_pool);
	return rc;

mbx_fail_out:
	/* Free memory allocated for mailbox command */
	mempool_free(pmb, phba->mbox_mem_pool);

mem_fail_out:
	/* free the irq already requested */
	free_irq(pci_irq_vector(phba->pcidev, 1), phba);

irq_fail_out:
	/* free the irq already requested */
	free_irq(pci_irq_vector(phba->pcidev, 0), phba);

msi_fail_out:
	/* Unconfigure MSI-X capability structure */
	pci_free_irq_vectors(phba->pcidev);

vec_fail_out:
	return rc;
}

/**
 * lpfc_sli_enable_msi - Enable MSI interrupt mode on SLI-3 device.
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to enable the MSI interrupt mode to device with
 * SLI-3 interface spec. The kernel function pci_enable_msi() is called to
 * enable the MSI vector. The device driver is responsible for calling the
 * request_irq() to register MSI vector with a interrupt the handler, which
 * is done in this function.
 *
 * Return codes
 * 	0 - successful
 * 	other values - error
 */
static int
lpfc_sli_enable_msi(struct lpfc_hba *phba)
{
	int rc;

	rc = pci_enable_msi(phba->pcidev);
	if (!rc)
		lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
				"0462 PCI enable MSI mode success.\n");
	else {
		lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
				"0471 PCI enable MSI mode failed (%d)\n", rc);
		return rc;
	}

	rc = request_irq(phba->pcidev->irq, lpfc_sli_intr_handler,
			 0, LPFC_DRIVER_NAME, phba);
	if (rc) {
		pci_disable_msi(phba->pcidev);
		lpfc_printf_log(phba, KERN_WARNING, LOG_INIT,
				"0478 MSI request_irq failed (%d)\n", rc);
	}
	return rc;
}

/**
 * lpfc_sli_enable_intr - Enable device interrupt to SLI-3 device.
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to enable device interrupt and associate driver's
 * interrupt handler(s) to interrupt vector(s) to device with SLI-3 interface
 * spec. Depends on the interrupt mode configured to the driver, the driver
 * will try to fallback from the configured interrupt mode to an interrupt
 * mode which is supported by the platform, kernel, and device in the order
 * of:
 * MSI-X -> MSI -> IRQ.
 *
 * Return codes
 *   0 - successful
 *   other values - error
 **/
static uint32_t
lpfc_sli_enable_intr(struct lpfc_hba *phba, uint32_t cfg_mode)
{
	uint32_t intr_mode = LPFC_INTR_ERROR;
	int retval;

	if (cfg_mode == 2) {
		/* Need to issue conf_port mbox cmd before conf_msi mbox cmd */
		retval = lpfc_sli_config_port(phba, LPFC_SLI_REV3);
		if (!retval) {
			/* Now, try to enable MSI-X interrupt mode */
			retval = lpfc_sli_enable_msix(phba);
			if (!retval) {
				/* Indicate initialization to MSI-X mode */
				phba->intr_type = MSIX;
				intr_mode = 2;
			}
		}
	}

	/* Fallback to MSI if MSI-X initialization failed */
	if (cfg_mode >= 1 && phba->intr_type == NONE) {
		retval = lpfc_sli_enable_msi(phba);
		if (!retval) {
			/* Indicate initialization to MSI mode */
			phba->intr_type = MSI;
			intr_mode = 1;
		}
	}

	/* Fallback to INTx if both MSI-X/MSI initalization failed */
	if (phba->intr_type == NONE) {
		retval = request_irq(phba->pcidev->irq, lpfc_sli_intr_handler,
				     IRQF_SHARED, LPFC_DRIVER_NAME, phba);
		if (!retval) {
			/* Indicate initialization to INTx mode */
			phba->intr_type = INTx;
			intr_mode = 0;
		}
	}
	return intr_mode;
}

/**
 * lpfc_sli_disable_intr - Disable device interrupt to SLI-3 device.
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to disable device interrupt and disassociate the
 * driver's interrupt handler(s) from interrupt vector(s) to device with
 * SLI-3 interface spec. Depending on the interrupt mode, the driver will
 * release the interrupt vector(s) for the message signaled interrupt.
 **/
static void
lpfc_sli_disable_intr(struct lpfc_hba *phba)
{
	int nr_irqs, i;

	if (phba->intr_type == MSIX)
		nr_irqs = LPFC_MSIX_VECTORS;
	else
		nr_irqs = 1;

	for (i = 0; i < nr_irqs; i++)
		free_irq(pci_irq_vector(phba->pcidev, i), phba);
	pci_free_irq_vectors(phba->pcidev);

	/* Reset interrupt management states */
	phba->intr_type = NONE;
	phba->sli.slistat.sli_intr = 0;
}

/**
 * lpfc_find_cpu_handle - Find the CPU that corresponds to the specified Queue
 * @phba: pointer to lpfc hba data structure.
 * @id: EQ vector index or Hardware Queue index
 * @match: LPFC_FIND_BY_EQ = match by EQ
 *         LPFC_FIND_BY_HDWQ = match by Hardware Queue
 * Return the CPU that matches the selection criteria
 */
static uint16_t
lpfc_find_cpu_handle(struct lpfc_hba *phba, uint16_t id, int match)
{
	struct lpfc_vector_map_info *cpup;
	int cpu;

	/* Loop through all CPUs */
	for_each_present_cpu(cpu) {
		cpup = &phba->sli4_hba.cpu_map[cpu];

		/* If we are matching by EQ, there may be multiple CPUs using
		 * using the same vector, so select the one with
		 * LPFC_CPU_FIRST_IRQ set.
		 */
		if ((match == LPFC_FIND_BY_EQ) &&
		    (cpup->flag & LPFC_CPU_FIRST_IRQ) &&
		    (cpup->eq == id))
			return cpu;

		/* If matching by HDWQ, select the first CPU that matches */
		if ((match == LPFC_FIND_BY_HDWQ) && (cpup->hdwq == id))
			return cpu;
	}
	return 0;
}

#ifdef CONFIG_X86
/**
 * lpfc_find_hyper - Determine if the CPU map entry is hyper-threaded
 * @phba: pointer to lpfc hba data structure.
 * @cpu: CPU map index
 * @phys_id: CPU package physical id
 * @core_id: CPU core id
 */
static int
lpfc_find_hyper(struct lpfc_hba *phba, int cpu,
		uint16_t phys_id, uint16_t core_id)
{
	struct lpfc_vector_map_info *cpup;
	int idx;

	for_each_present_cpu(idx) {
		cpup = &phba->sli4_hba.cpu_map[idx];
		/* Does the cpup match the one we are looking for */
		if ((cpup->phys_id == phys_id) &&
		    (cpup->core_id == core_id) &&
		    (cpu != idx))
			return 1;
	}
	return 0;
}
#endif

/*
 * lpfc_assign_eq_map_info - Assigns eq for vector_map structure
 * @phba: pointer to lpfc hba data structure.
 * @eqidx: index for eq and irq vector
 * @flag: flags to set for vector_map structure
 * @cpu: cpu used to index vector_map structure
 *
 * The routine assigns eq info into vector_map structure
 */
static inline void
lpfc_assign_eq_map_info(struct lpfc_hba *phba, uint16_t eqidx, uint16_t flag,
			unsigned int cpu)
{
	struct lpfc_vector_map_info *cpup = &phba->sli4_hba.cpu_map[cpu];
	struct lpfc_hba_eq_hdl *eqhdl = lpfc_get_eq_hdl(eqidx);

	cpup->eq = eqidx;
	cpup->flag |= flag;

	lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
			"3336 Set Affinity: CPU %d irq %d eq %d flag x%x\n",
			cpu, eqhdl->irq, cpup->eq, cpup->flag);
}

/**
 * lpfc_cpu_map_array_init - Initialize cpu_map structure
 * @phba: pointer to lpfc hba data structure.
 *
 * The routine initializes the cpu_map array structure
 */
static void
lpfc_cpu_map_array_init(struct lpfc_hba *phba)
{
	struct lpfc_vector_map_info *cpup;
	struct lpfc_eq_intr_info *eqi;
	int cpu;

	for_each_possible_cpu(cpu) {
		cpup = &phba->sli4_hba.cpu_map[cpu];
		cpup->phys_id = LPFC_VECTOR_MAP_EMPTY;
		cpup->core_id = LPFC_VECTOR_MAP_EMPTY;
		cpup->hdwq = LPFC_VECTOR_MAP_EMPTY;
		cpup->eq = LPFC_VECTOR_MAP_EMPTY;
		cpup->flag = 0;
		eqi = per_cpu_ptr(phba->sli4_hba.eq_info, cpu);
		INIT_LIST_HEAD(&eqi->list);
		eqi->icnt = 0;
	}
}

/**
 * lpfc_hba_eq_hdl_array_init - Initialize hba_eq_hdl structure
 * @phba: pointer to lpfc hba data structure.
 *
 * The routine initializes the hba_eq_hdl array structure
 */
static void
lpfc_hba_eq_hdl_array_init(struct lpfc_hba *phba)
{
	struct lpfc_hba_eq_hdl *eqhdl;
	int i;

	for (i = 0; i < phba->cfg_irq_chann; i++) {
		eqhdl = lpfc_get_eq_hdl(i);
		eqhdl->irq = LPFC_VECTOR_MAP_EMPTY;
		eqhdl->phba = phba;
	}
}

/**
 * lpfc_cpu_affinity_check - Check vector CPU affinity mappings
 * @phba: pointer to lpfc hba data structure.
 * @vectors: number of msix vectors allocated.
 *
 * The routine will figure out the CPU affinity assignment for every
 * MSI-X vector allocated for the HBA.
 * In addition, the CPU to IO channel mapping will be calculated
 * and the phba->sli4_hba.cpu_map array will reflect this.
 */
static void
lpfc_cpu_affinity_check(struct lpfc_hba *phba, int vectors)
{
	int i, cpu, idx, next_idx, new_cpu, start_cpu, first_cpu;
	int max_phys_id, min_phys_id;
	int max_core_id, min_core_id;
	struct lpfc_vector_map_info *cpup;
	struct lpfc_vector_map_info *new_cpup;
#ifdef CONFIG_X86
	struct cpuinfo_x86 *cpuinfo;
#endif

	max_phys_id = 0;
	min_phys_id = LPFC_VECTOR_MAP_EMPTY;
	max_core_id = 0;
	min_core_id = LPFC_VECTOR_MAP_EMPTY;

	/* Update CPU map with physical id and core id of each CPU */
	for_each_present_cpu(cpu) {
		cpup = &phba->sli4_hba.cpu_map[cpu];
#ifdef CONFIG_X86
		cpuinfo = &cpu_data(cpu);
		cpup->phys_id = cpuinfo->phys_proc_id;
		cpup->core_id = cpuinfo->cpu_core_id;
		if (lpfc_find_hyper(phba, cpu, cpup->phys_id, cpup->core_id))
			cpup->flag |= LPFC_CPU_MAP_HYPER;
#else
		/* No distinction between CPUs for other platforms */
		cpup->phys_id = 0;
		cpup->core_id = cpu;
#endif

		lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
				"3328 CPU %d physid %d coreid %d flag x%x\n",
				cpu, cpup->phys_id, cpup->core_id, cpup->flag);

		if (cpup->phys_id > max_phys_id)
			max_phys_id = cpup->phys_id;
		if (cpup->phys_id < min_phys_id)
			min_phys_id = cpup->phys_id;

		if (cpup->core_id > max_core_id)
			max_core_id = cpup->core_id;
		if (cpup->core_id < min_core_id)
			min_core_id = cpup->core_id;
	}

	/* After looking at each irq vector assigned to this pcidev, its
	 * possible to see that not ALL CPUs have been accounted for.
	 * Next we will set any unassigned (unaffinitized) cpu map
	 * entries to a IRQ on the same phys_id.
	 */
	first_cpu = cpumask_first(cpu_present_mask);
	start_cpu = first_cpu;

	for_each_present_cpu(cpu) {
		cpup = &phba->sli4_hba.cpu_map[cpu];

		/* Is this CPU entry unassigned */
		if (cpup->eq == LPFC_VECTOR_MAP_EMPTY) {
			/* Mark CPU as IRQ not assigned by the kernel */
			cpup->flag |= LPFC_CPU_MAP_UNASSIGN;

			/* If so, find a new_cpup thats on the the SAME
			 * phys_id as cpup. start_cpu will start where we
			 * left off so all unassigned entries don't get assgined
			 * the IRQ of the first entry.
			 */
			new_cpu = start_cpu;
			for (i = 0; i < phba->sli4_hba.num_present_cpu; i++) {
				new_cpup = &phba->sli4_hba.cpu_map[new_cpu];
				if (!(new_cpup->flag & LPFC_CPU_MAP_UNASSIGN) &&
				    (new_cpup->eq != LPFC_VECTOR_MAP_EMPTY) &&
				    (new_cpup->phys_id == cpup->phys_id))
					goto found_same;
				new_cpu = cpumask_next(
					new_cpu, cpu_present_mask);
				if (new_cpu == nr_cpumask_bits)
					new_cpu = first_cpu;
			}
			/* At this point, we leave the CPU as unassigned */
			continue;
found_same:
			/* We found a matching phys_id, so copy the IRQ info */
			cpup->eq = new_cpup->eq;

			/* Bump start_cpu to the next slot to minmize the
			 * chance of having multiple unassigned CPU entries
			 * selecting the same IRQ.
			 */
			start_cpu = cpumask_next(new_cpu, cpu_present_mask);
			if (start_cpu == nr_cpumask_bits)
				start_cpu = first_cpu;

			lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
					"3337 Set Affinity: CPU %d "
					"eq %d from peer cpu %d same "
					"phys_id (%d)\n",
					cpu, cpup->eq, new_cpu,
					cpup->phys_id);
		}
	}

	/* Set any unassigned cpu map entries to a IRQ on any phys_id */
	start_cpu = first_cpu;

	for_each_present_cpu(cpu) {
		cpup = &phba->sli4_hba.cpu_map[cpu];

		/* Is this entry unassigned */
		if (cpup->eq == LPFC_VECTOR_MAP_EMPTY) {
			/* Mark it as IRQ not assigned by the kernel */
			cpup->flag |= LPFC_CPU_MAP_UNASSIGN;

			/* If so, find a new_cpup thats on ANY phys_id
			 * as the cpup. start_cpu will start where we
			 * left off so all unassigned entries don't get
			 * assigned the IRQ of the first entry.
			 */
			new_cpu = start_cpu;
			for (i = 0; i < phba->sli4_hba.num_present_cpu; i++) {
				new_cpup = &phba->sli4_hba.cpu_map[new_cpu];
				if (!(new_cpup->flag & LPFC_CPU_MAP_UNASSIGN) &&
				    (new_cpup->eq != LPFC_VECTOR_MAP_EMPTY))
					goto found_any;
				new_cpu = cpumask_next(
					new_cpu, cpu_present_mask);
				if (new_cpu == nr_cpumask_bits)
					new_cpu = first_cpu;
			}
			/* We should never leave an entry unassigned */
			lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
					"3339 Set Affinity: CPU %d "
					"eq %d UNASSIGNED\n",
					cpup->hdwq, cpup->eq);
			continue;
found_any:
			/* We found an available entry, copy the IRQ info */
			cpup->eq = new_cpup->eq;

			/* Bump start_cpu to the next slot to minmize the
			 * chance of having multiple unassigned CPU entries
			 * selecting the same IRQ.
			 */
			start_cpu = cpumask_next(new_cpu, cpu_present_mask);
			if (start_cpu == nr_cpumask_bits)
				start_cpu = first_cpu;

			lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
					"3338 Set Affinity: CPU %d "
					"eq %d from peer cpu %d (%d/%d)\n",
					cpu, cpup->eq, new_cpu,
					new_cpup->phys_id, new_cpup->core_id);
		}
	}

	/* Assign hdwq indices that are unique across all cpus in the map
	 * that are also FIRST_CPUs.
	 */
	idx = 0;
	for_each_present_cpu(cpu) {
		cpup = &phba->sli4_hba.cpu_map[cpu];

		/* Only FIRST IRQs get a hdwq index assignment. */
		if (!(cpup->flag & LPFC_CPU_FIRST_IRQ))
			continue;

		/* 1 to 1, the first LPFC_CPU_FIRST_IRQ cpus to a unique hdwq */
		cpup->hdwq = idx;
		idx++;
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"3333 Set Affinity: CPU %d (phys %d core %d): "
				"hdwq %d eq %d flg x%x\n",
				cpu, cpup->phys_id, cpup->core_id,
				cpup->hdwq, cpup->eq, cpup->flag);
	}
	/* Associate a hdwq with each cpu_map entry
	 * This will be 1 to 1 - hdwq to cpu, unless there are less
	 * hardware queues then CPUs. For that case we will just round-robin
	 * the available hardware queues as they get assigned to CPUs.
	 * The next_idx is the idx from the FIRST_CPU loop above to account
	 * for irq_chann < hdwq.  The idx is used for round-robin assignments
	 * and needs to start at 0.
	 */
	next_idx = idx;
	start_cpu = 0;
	idx = 0;
	for_each_present_cpu(cpu) {
		cpup = &phba->sli4_hba.cpu_map[cpu];

		/* FIRST cpus are already mapped. */
		if (cpup->flag & LPFC_CPU_FIRST_IRQ)
			continue;

		/* If the cfg_irq_chann < cfg_hdw_queue, set the hdwq
		 * of the unassigned cpus to the next idx so that all
		 * hdw queues are fully utilized.
		 */
		if (next_idx < phba->cfg_hdw_queue) {
			cpup->hdwq = next_idx;
			next_idx++;
			continue;
		}

		/* Not a First CPU and all hdw_queues are used.  Reuse a
		 * Hardware Queue for another CPU, so be smart about it
		 * and pick one that has its IRQ/EQ mapped to the same phys_id
		 * (CPU package) and core_id.
		 */
		new_cpu = start_cpu;
		for (i = 0; i < phba->sli4_hba.num_present_cpu; i++) {
			new_cpup = &phba->sli4_hba.cpu_map[new_cpu];
			if (new_cpup->hdwq != LPFC_VECTOR_MAP_EMPTY &&
			    new_cpup->phys_id == cpup->phys_id &&
			    new_cpup->core_id == cpup->core_id) {
				goto found_hdwq;
			}
			new_cpu = cpumask_next(new_cpu, cpu_present_mask);
			if (new_cpu == nr_cpumask_bits)
				new_cpu = first_cpu;
		}

		/* If we can't match both phys_id and core_id,
		 * settle for just a phys_id match.
		 */
		new_cpu = start_cpu;
		for (i = 0; i < phba->sli4_hba.num_present_cpu; i++) {
			new_cpup = &phba->sli4_hba.cpu_map[new_cpu];
			if (new_cpup->hdwq != LPFC_VECTOR_MAP_EMPTY &&
			    new_cpup->phys_id == cpup->phys_id)
				goto found_hdwq;

			new_cpu = cpumask_next(new_cpu, cpu_present_mask);
			if (new_cpu == nr_cpumask_bits)
				new_cpu = first_cpu;
		}

		/* Otherwise just round robin on cfg_hdw_queue */
		cpup->hdwq = idx % phba->cfg_hdw_queue;
		idx++;
		goto logit;
 found_hdwq:
		/* We found an available entry, copy the IRQ info */
		start_cpu = cpumask_next(new_cpu, cpu_present_mask);
		if (start_cpu == nr_cpumask_bits)
			start_cpu = first_cpu;
		cpup->hdwq = new_cpup->hdwq;
 logit:
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"3335 Set Affinity: CPU %d (phys %d core %d): "
				"hdwq %d eq %d flg x%x\n",
				cpu, cpup->phys_id, cpup->core_id,
				cpup->hdwq, cpup->eq, cpup->flag);
	}

	/*
	 * Initialize the cpu_map slots for not-present cpus in case
	 * a cpu is hot-added. Perform a simple hdwq round robin assignment.
	 */
	idx = 0;
	for_each_possible_cpu(cpu) {
		cpup = &phba->sli4_hba.cpu_map[cpu];
		if (cpup->hdwq != LPFC_VECTOR_MAP_EMPTY)
			continue;

		cpup->hdwq = idx++ % phba->cfg_hdw_queue;
		lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
				"3340 Set Affinity: not present "
				"CPU %d hdwq %d\n",
				cpu, cpup->hdwq);
	}

	/* The cpu_map array will be used later during initialization
	 * when EQ / CQ / WQs are allocated and configured.
	 */
	return;
}

/**
 * lpfc_cpuhp_get_eq
 *
 * @phba:   pointer to lpfc hba data structure.
 * @cpu:    cpu going offline
 * @eqlist:
 */
static void
lpfc_cpuhp_get_eq(struct lpfc_hba *phba, unsigned int cpu,
		  struct list_head *eqlist)
{
	const struct cpumask *maskp;
	struct lpfc_queue *eq;
	cpumask_t tmp;
	u16 idx;

	for (idx = 0; idx < phba->cfg_irq_chann; idx++) {
		maskp = pci_irq_get_affinity(phba->pcidev, idx);
		if (!maskp)
			continue;
		/*
		 * if irq is not affinitized to the cpu going
		 * then we don't need to poll the eq attached
		 * to it.
		 */
		if (!cpumask_and(&tmp, maskp, cpumask_of(cpu)))
			continue;
		/* get the cpus that are online and are affini-
		 * tized to this irq vector.  If the count is
		 * more than 1 then cpuhp is not going to shut-
		 * down this vector.  Since this cpu has not
		 * gone offline yet, we need >1.
		 */
		cpumask_and(&tmp, maskp, cpu_online_mask);
		if (cpumask_weight(&tmp) > 1)
			continue;

		/* Now that we have an irq to shutdown, get the eq
		 * mapped to this irq.  Note: multiple hdwq's in
		 * the software can share an eq, but eventually
		 * only eq will be mapped to this vector
		 */
		eq = phba->sli4_hba.hba_eq_hdl[idx].eq;
		list_add(&eq->_poll_list, eqlist);
	}
}

static void __lpfc_cpuhp_remove(struct lpfc_hba *phba)
{
	if (phba->sli_rev != LPFC_SLI_REV4)
		return;

	cpuhp_state_remove_instance_nocalls(lpfc_cpuhp_state,
					    &phba->cpuhp);
	/*
	 * unregistering the instance doesn't stop the polling
	 * timer. Wait for the poll timer to retire.
	 */
	synchronize_rcu();
	del_timer_sync(&phba->cpuhp_poll_timer);
}

static void lpfc_cpuhp_remove(struct lpfc_hba *phba)
{
	if (phba->pport->fc_flag & FC_OFFLINE_MODE)
		return;

	__lpfc_cpuhp_remove(phba);
}

static void lpfc_cpuhp_add(struct lpfc_hba *phba)
{
	if (phba->sli_rev != LPFC_SLI_REV4)
		return;

	rcu_read_lock();

	if (!list_empty(&phba->poll_list)) {
		timer_setup(&phba->cpuhp_poll_timer, lpfc_sli4_poll_hbtimer, 0);
		mod_timer(&phba->cpuhp_poll_timer,
			  jiffies + msecs_to_jiffies(LPFC_POLL_HB));
	}

	rcu_read_unlock();

	cpuhp_state_add_instance_nocalls(lpfc_cpuhp_state,
					 &phba->cpuhp);
}

static int __lpfc_cpuhp_checks(struct lpfc_hba *phba, int *retval)
{
	if (phba->pport->load_flag & FC_UNLOADING) {
		*retval = -EAGAIN;
		return true;
	}

	if (phba->sli_rev != LPFC_SLI_REV4) {
		*retval = 0;
		return true;
	}

	/* proceed with the hotplug */
	return false;
}

/**
 * lpfc_irq_set_aff - set IRQ affinity
 * @eqhdl: EQ handle
 * @cpu: cpu to set affinity
 *
 **/
static inline void
lpfc_irq_set_aff(struct lpfc_hba_eq_hdl *eqhdl, unsigned int cpu)
{
	cpumask_clear(&eqhdl->aff_mask);
	cpumask_set_cpu(cpu, &eqhdl->aff_mask);
	irq_set_status_flags(eqhdl->irq, IRQ_NO_BALANCING);
	irq_set_affinity_hint(eqhdl->irq, &eqhdl->aff_mask);
}

/**
 * lpfc_irq_clear_aff - clear IRQ affinity
 * @eqhdl: EQ handle
 *
 **/
static inline void
lpfc_irq_clear_aff(struct lpfc_hba_eq_hdl *eqhdl)
{
	cpumask_clear(&eqhdl->aff_mask);
	irq_clear_status_flags(eqhdl->irq, IRQ_NO_BALANCING);
	irq_set_affinity_hint(eqhdl->irq, &eqhdl->aff_mask);
}

/**
 * lpfc_irq_rebalance - rebalances IRQ affinity according to cpuhp event
 * @phba: pointer to HBA context object.
 * @cpu: cpu going offline/online
 * @offline: true, cpu is going offline. false, cpu is coming online.
 *
 * If cpu is going offline, we'll try our best effort to find the next
 * online cpu on the phba's NUMA node and migrate all offlining IRQ affinities.
 *
 * If cpu is coming online, reaffinitize the IRQ back to the onlineng cpu.
 *
 * Note: Call only if cfg_irq_numa is enabled, otherwise rely on
 *	 PCI_IRQ_AFFINITY to auto-manage IRQ affinity.
 *
 **/
static void
lpfc_irq_rebalance(struct lpfc_hba *phba, unsigned int cpu, bool offline)
{
	struct lpfc_vector_map_info *cpup;
	struct cpumask *aff_mask;
	unsigned int cpu_select, cpu_next, idx;
	const struct cpumask *numa_mask;

	if (!phba->cfg_irq_numa)
		return;

	numa_mask = &phba->sli4_hba.numa_mask;

	if (!cpumask_test_cpu(cpu, numa_mask))
		return;

	cpup = &phba->sli4_hba.cpu_map[cpu];

	if (!(cpup->flag & LPFC_CPU_FIRST_IRQ))
		return;

	if (offline) {
		/* Find next online CPU on NUMA node */
		cpu_next = cpumask_next_wrap(cpu, numa_mask, cpu, true);
		cpu_select = lpfc_next_online_numa_cpu(numa_mask, cpu_next);

		/* Found a valid CPU */
		if ((cpu_select < nr_cpu_ids) && (cpu_select != cpu)) {
			/* Go through each eqhdl and ensure offlining
			 * cpu aff_mask is migrated
			 */
			for (idx = 0; idx < phba->cfg_irq_chann; idx++) {
				aff_mask = lpfc_get_aff_mask(idx);

				/* Migrate affinity */
				if (cpumask_test_cpu(cpu, aff_mask))
					lpfc_irq_set_aff(lpfc_get_eq_hdl(idx),
							 cpu_select);
			}
		} else {
			/* Rely on irqbalance if no online CPUs left on NUMA */
			for (idx = 0; idx < phba->cfg_irq_chann; idx++)
				lpfc_irq_clear_aff(lpfc_get_eq_hdl(idx));
		}
	} else {
		/* Migrate affinity back to this CPU */
		lpfc_irq_set_aff(lpfc_get_eq_hdl(cpup->eq), cpu);
	}
}

static int lpfc_cpu_offline(unsigned int cpu, struct hlist_node *node)
{
	struct lpfc_hba *phba = hlist_entry_safe(node, struct lpfc_hba, cpuhp);
	struct lpfc_queue *eq, *next;
	LIST_HEAD(eqlist);
	int retval;

	if (!phba) {
		WARN_ONCE(!phba, "cpu: %u. phba:NULL", raw_smp_processor_id());
		return 0;
	}

	if (__lpfc_cpuhp_checks(phba, &retval))
		return retval;

	lpfc_irq_rebalance(phba, cpu, true);

	lpfc_cpuhp_get_eq(phba, cpu, &eqlist);

	/* start polling on these eq's */
	list_for_each_entry_safe(eq, next, &eqlist, _poll_list) {
		list_del_init(&eq->_poll_list);
		lpfc_sli4_start_polling(eq);
	}

	return 0;
}

static int lpfc_cpu_online(unsigned int cpu, struct hlist_node *node)
{
	struct lpfc_hba *phba = hlist_entry_safe(node, struct lpfc_hba, cpuhp);
	struct lpfc_queue *eq, *next;
	unsigned int n;
	int retval;

	if (!phba) {
		WARN_ONCE(!phba, "cpu: %u. phba:NULL", raw_smp_processor_id());
		return 0;
	}

	if (__lpfc_cpuhp_checks(phba, &retval))
		return retval;

	lpfc_irq_rebalance(phba, cpu, false);

	list_for_each_entry_safe(eq, next, &phba->poll_list, _poll_list) {
		n = lpfc_find_cpu_handle(phba, eq->hdwq, LPFC_FIND_BY_HDWQ);
		if (n == cpu)
			lpfc_sli4_stop_polling(eq);
	}

	return 0;
}

/**
 * lpfc_sli4_enable_msix - Enable MSI-X interrupt mode to SLI-4 device
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to enable the MSI-X interrupt vectors to device
 * with SLI-4 interface spec.  It also allocates MSI-X vectors and maps them
 * to cpus on the system.
 *
 * When cfg_irq_numa is enabled, the adapter will only allocate vectors for
 * the number of cpus on the same numa node as this adapter.  The vectors are
 * allocated without requesting OS affinity mapping.  A vector will be
 * allocated and assigned to each online and offline cpu.  If the cpu is
 * online, then affinity will be set to that cpu.  If the cpu is offline, then
 * affinity will be set to the nearest peer cpu within the numa node that is
 * online.  If there are no online cpus within the numa node, affinity is not
 * assigned and the OS may do as it pleases. Note: cpu vector affinity mapping
 * is consistent with the way cpu online/offline is handled when cfg_irq_numa is
 * configured.
 *
 * If numa mode is not enabled and there is more than 1 vector allocated, then
 * the driver relies on the managed irq interface where the OS assigns vector to
 * cpu affinity.  The driver will then use that affinity mapping to setup its
 * cpu mapping table.
 *
 * Return codes
 * 0 - successful
 * other values - error
 **/
static int
lpfc_sli4_enable_msix(struct lpfc_hba *phba)
{
	int vectors, rc, index;
	char *name;
	const struct cpumask *numa_mask = NULL;
	unsigned int cpu = 0, cpu_cnt = 0, cpu_select = nr_cpu_ids;
	struct lpfc_hba_eq_hdl *eqhdl;
	const struct cpumask *maskp;
	bool first;
	unsigned int flags = PCI_IRQ_MSIX;

	/* Set up MSI-X multi-message vectors */
	vectors = phba->cfg_irq_chann;

	if (phba->cfg_irq_numa) {
		numa_mask = &phba->sli4_hba.numa_mask;
		cpu_cnt = cpumask_weight(numa_mask);
		vectors = min(phba->cfg_irq_chann, cpu_cnt);

		/* cpu: iterates over numa_mask including offline or online
		 * cpu_select: iterates over online numa_mask to set affinity
		 */
		cpu = cpumask_first(numa_mask);
		cpu_select = lpfc_next_online_numa_cpu(numa_mask, cpu);
	} else {
		flags |= PCI_IRQ_AFFINITY;
	}

	rc = pci_alloc_irq_vectors(phba->pcidev, 1, vectors, flags);
	if (rc < 0) {
		lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
				"0484 PCI enable MSI-X failed (%d)\n", rc);
		goto vec_fail_out;
	}
	vectors = rc;

	/* Assign MSI-X vectors to interrupt handlers */
	for (index = 0; index < vectors; index++) {
		eqhdl = lpfc_get_eq_hdl(index);
		name = eqhdl->handler_name;
		memset(name, 0, LPFC_SLI4_HANDLER_NAME_SZ);
		snprintf(name, LPFC_SLI4_HANDLER_NAME_SZ,
			 LPFC_DRIVER_HANDLER_NAME"%d", index);

		eqhdl->idx = index;
		rc = request_irq(pci_irq_vector(phba->pcidev, index),
			 &lpfc_sli4_hba_intr_handler, 0,
			 name, eqhdl);
		if (rc) {
			lpfc_printf_log(phba, KERN_WARNING, LOG_INIT,
					"0486 MSI-X fast-path (%d) "
					"request_irq failed (%d)\n", index, rc);
			goto cfg_fail_out;
		}

		eqhdl->irq = pci_irq_vector(phba->pcidev, index);

		if (phba->cfg_irq_numa) {
			/* If found a neighboring online cpu, set affinity */
			if (cpu_select < nr_cpu_ids)
				lpfc_irq_set_aff(eqhdl, cpu_select);

			/* Assign EQ to cpu_map */
			lpfc_assign_eq_map_info(phba, index,
						LPFC_CPU_FIRST_IRQ,
						cpu);

			/* Iterate to next offline or online cpu in numa_mask */
			cpu = cpumask_next(cpu, numa_mask);

			/* Find next online cpu in numa_mask to set affinity */
			cpu_select = lpfc_next_online_numa_cpu(numa_mask, cpu);
		} else if (vectors == 1) {
			cpu = cpumask_first(cpu_present_mask);
			lpfc_assign_eq_map_info(phba, index, LPFC_CPU_FIRST_IRQ,
						cpu);
		} else {
			maskp = pci_irq_get_affinity(phba->pcidev, index);

			first = true;
			/* Loop through all CPUs associated with vector index */
			for_each_cpu_and(cpu, maskp, cpu_present_mask) {
				/* If this is the first CPU thats assigned to
				 * this vector, set LPFC_CPU_FIRST_IRQ.
				 */
				lpfc_assign_eq_map_info(phba, index,
							first ?
							LPFC_CPU_FIRST_IRQ : 0,
							cpu);
				if (first)
					first = false;
			}
		}
	}

	if (vectors != phba->cfg_irq_chann) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"3238 Reducing IO channels to match number of "
				"MSI-X vectors, requested %d got %d\n",
				phba->cfg_irq_chann, vectors);
		if (phba->cfg_irq_chann > vectors)
			phba->cfg_irq_chann = vectors;
	}

	return rc;

cfg_fail_out:
	/* free the irq already requested */
	for (--index; index >= 0; index--) {
		eqhdl = lpfc_get_eq_hdl(index);
		lpfc_irq_clear_aff(eqhdl);
		irq_set_affinity_hint(eqhdl->irq, NULL);
		free_irq(eqhdl->irq, eqhdl);
	}

	/* Unconfigure MSI-X capability structure */
	pci_free_irq_vectors(phba->pcidev);

vec_fail_out:
	return rc;
}

/**
 * lpfc_sli4_enable_msi - Enable MSI interrupt mode to SLI-4 device
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to enable the MSI interrupt mode to device with
 * SLI-4 interface spec. The kernel function pci_alloc_irq_vectors() is
 * called to enable the MSI vector. The device driver is responsible for
 * calling the request_irq() to register MSI vector with a interrupt the
 * handler, which is done in this function.
 *
 * Return codes
 * 	0 - successful
 * 	other values - error
 **/
static int
lpfc_sli4_enable_msi(struct lpfc_hba *phba)
{
	int rc, index;
	unsigned int cpu;
	struct lpfc_hba_eq_hdl *eqhdl;

	rc = pci_alloc_irq_vectors(phba->pcidev, 1, 1,
				   PCI_IRQ_MSI | PCI_IRQ_AFFINITY);
	if (rc > 0)
		lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
				"0487 PCI enable MSI mode success.\n");
	else {
		lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
				"0488 PCI enable MSI mode failed (%d)\n", rc);
		return rc ? rc : -1;
	}

	rc = request_irq(phba->pcidev->irq, lpfc_sli4_intr_handler,
			 0, LPFC_DRIVER_NAME, phba);
	if (rc) {
		pci_free_irq_vectors(phba->pcidev);
		lpfc_printf_log(phba, KERN_WARNING, LOG_INIT,
				"0490 MSI request_irq failed (%d)\n", rc);
		return rc;
	}

	eqhdl = lpfc_get_eq_hdl(0);
	eqhdl->irq = pci_irq_vector(phba->pcidev, 0);

	cpu = cpumask_first(cpu_present_mask);
	lpfc_assign_eq_map_info(phba, 0, LPFC_CPU_FIRST_IRQ, cpu);

	for (index = 0; index < phba->cfg_irq_chann; index++) {
		eqhdl = lpfc_get_eq_hdl(index);
		eqhdl->idx = index;
	}

	return 0;
}

/**
 * lpfc_sli4_enable_intr - Enable device interrupt to SLI-4 device
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to enable device interrupt and associate driver's
 * interrupt handler(s) to interrupt vector(s) to device with SLI-4
 * interface spec. Depends on the interrupt mode configured to the driver,
 * the driver will try to fallback from the configured interrupt mode to an
 * interrupt mode which is supported by the platform, kernel, and device in
 * the order of:
 * MSI-X -> MSI -> IRQ.
 *
 * Return codes
 * 	0 - successful
 * 	other values - error
 **/
static uint32_t
lpfc_sli4_enable_intr(struct lpfc_hba *phba, uint32_t cfg_mode)
{
	uint32_t intr_mode = LPFC_INTR_ERROR;
	int retval, idx;

	if (cfg_mode == 2) {
		/* Preparation before conf_msi mbox cmd */
		retval = 0;
		if (!retval) {
			/* Now, try to enable MSI-X interrupt mode */
			retval = lpfc_sli4_enable_msix(phba);
			if (!retval) {
				/* Indicate initialization to MSI-X mode */
				phba->intr_type = MSIX;
				intr_mode = 2;
			}
		}
	}

	/* Fallback to MSI if MSI-X initialization failed */
	if (cfg_mode >= 1 && phba->intr_type == NONE) {
		retval = lpfc_sli4_enable_msi(phba);
		if (!retval) {
			/* Indicate initialization to MSI mode */
			phba->intr_type = MSI;
			intr_mode = 1;
		}
	}

	/* Fallback to INTx if both MSI-X/MSI initalization failed */
	if (phba->intr_type == NONE) {
		retval = request_irq(phba->pcidev->irq, lpfc_sli4_intr_handler,
				     IRQF_SHARED, LPFC_DRIVER_NAME, phba);
		if (!retval) {
			struct lpfc_hba_eq_hdl *eqhdl;
			unsigned int cpu;

			/* Indicate initialization to INTx mode */
			phba->intr_type = INTx;
			intr_mode = 0;

			eqhdl = lpfc_get_eq_hdl(0);
			eqhdl->irq = pci_irq_vector(phba->pcidev, 0);

			cpu = cpumask_first(cpu_present_mask);
			lpfc_assign_eq_map_info(phba, 0, LPFC_CPU_FIRST_IRQ,
						cpu);
			for (idx = 0; idx < phba->cfg_irq_chann; idx++) {
				eqhdl = lpfc_get_eq_hdl(idx);
				eqhdl->idx = idx;
			}
		}
	}
	return intr_mode;
}

/**
 * lpfc_sli4_disable_intr - Disable device interrupt to SLI-4 device
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to disable device interrupt and disassociate
 * the driver's interrupt handler(s) from interrupt vector(s) to device
 * with SLI-4 interface spec. Depending on the interrupt mode, the driver
 * will release the interrupt vector(s) for the message signaled interrupt.
 **/
static void
lpfc_sli4_disable_intr(struct lpfc_hba *phba)
{
	/* Disable the currently initialized interrupt mode */
	if (phba->intr_type == MSIX) {
		int index;
		struct lpfc_hba_eq_hdl *eqhdl;

		/* Free up MSI-X multi-message vectors */
		for (index = 0; index < phba->cfg_irq_chann; index++) {
			eqhdl = lpfc_get_eq_hdl(index);
			lpfc_irq_clear_aff(eqhdl);
			irq_set_affinity_hint(eqhdl->irq, NULL);
			free_irq(eqhdl->irq, eqhdl);
		}
	} else {
		free_irq(phba->pcidev->irq, phba);
	}

	pci_free_irq_vectors(phba->pcidev);

	/* Reset interrupt management states */
	phba->intr_type = NONE;
	phba->sli.slistat.sli_intr = 0;
}

/**
 * lpfc_unset_hba - Unset SLI3 hba device initialization
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is invoked to unset the HBA device initialization steps to
 * a device with SLI-3 interface spec.
 **/
static void
lpfc_unset_hba(struct lpfc_hba *phba)
{
	struct lpfc_vport *vport = phba->pport;
	struct Scsi_Host  *shost = lpfc_shost_from_vport(vport);

	spin_lock_irq(shost->host_lock);
	vport->load_flag |= FC_UNLOADING;
	spin_unlock_irq(shost->host_lock);

	kfree(phba->vpi_bmask);
	kfree(phba->vpi_ids);

	lpfc_stop_hba_timers(phba);

	phba->pport->work_port_events = 0;

	lpfc_sli_hba_down(phba);

	lpfc_sli_brdrestart(phba);

	lpfc_sli_disable_intr(phba);

	return;
}

/**
 * lpfc_sli4_xri_exchange_busy_wait - Wait for device XRI exchange busy
 * @phba: Pointer to HBA context object.
 *
 * This function is called in the SLI4 code path to wait for completion
 * of device's XRIs exchange busy. It will check the XRI exchange busy
 * on outstanding FCP and ELS I/Os every 10ms for up to 10 seconds; after
 * that, it will check the XRI exchange busy on outstanding FCP and ELS
 * I/Os every 30 seconds, log error message, and wait forever. Only when
 * all XRI exchange busy complete, the driver unload shall proceed with
 * invoking the function reset ioctl mailbox command to the CNA and the
 * the rest of the driver unload resource release.
 **/
static void
lpfc_sli4_xri_exchange_busy_wait(struct lpfc_hba *phba)
{
	struct lpfc_sli4_hdw_queue *qp;
	int idx, ccnt;
	int wait_time = 0;
	int io_xri_cmpl = 1;
	int nvmet_xri_cmpl = 1;
	int els_xri_cmpl = list_empty(&phba->sli4_hba.lpfc_abts_els_sgl_list);

	/* Driver just aborted IOs during the hba_unset process.  Pause
	 * here to give the HBA time to complete the IO and get entries
	 * into the abts lists.
	 */
	msleep(LPFC_XRI_EXCH_BUSY_WAIT_T1 * 5);

	/* Wait for NVME pending IO to flush back to transport. */
	if (phba->cfg_enable_fc4_type & LPFC_ENABLE_NVME)
		lpfc_nvme_wait_for_io_drain(phba);

	ccnt = 0;
	for (idx = 0; idx < phba->cfg_hdw_queue; idx++) {
		qp = &phba->sli4_hba.hdwq[idx];
		io_xri_cmpl = list_empty(&qp->lpfc_abts_io_buf_list);
		if (!io_xri_cmpl) /* if list is NOT empty */
			ccnt++;
	}
	if (ccnt)
		io_xri_cmpl = 0;

	if (phba->cfg_enable_fc4_type & LPFC_ENABLE_NVME) {
		nvmet_xri_cmpl =
			list_empty(&phba->sli4_hba.lpfc_abts_nvmet_ctx_list);
	}

	while (!els_xri_cmpl || !io_xri_cmpl || !nvmet_xri_cmpl) {
		if (wait_time > LPFC_XRI_EXCH_BUSY_WAIT_TMO) {
			if (!nvmet_xri_cmpl)
				lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
						"6424 NVMET XRI exchange busy "
						"wait time: %d seconds.\n",
						wait_time/1000);
			if (!io_xri_cmpl)
				lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
						"6100 IO XRI exchange busy "
						"wait time: %d seconds.\n",
						wait_time/1000);
			if (!els_xri_cmpl)
				lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
						"2878 ELS XRI exchange busy "
						"wait time: %d seconds.\n",
						wait_time/1000);
			msleep(LPFC_XRI_EXCH_BUSY_WAIT_T2);
			wait_time += LPFC_XRI_EXCH_BUSY_WAIT_T2;
		} else {
			msleep(LPFC_XRI_EXCH_BUSY_WAIT_T1);
			wait_time += LPFC_XRI_EXCH_BUSY_WAIT_T1;
		}

		ccnt = 0;
		for (idx = 0; idx < phba->cfg_hdw_queue; idx++) {
			qp = &phba->sli4_hba.hdwq[idx];
			io_xri_cmpl = list_empty(
			    &qp->lpfc_abts_io_buf_list);
			if (!io_xri_cmpl) /* if list is NOT empty */
				ccnt++;
		}
		if (ccnt)
			io_xri_cmpl = 0;

		if (phba->cfg_enable_fc4_type & LPFC_ENABLE_NVME) {
			nvmet_xri_cmpl = list_empty(
				&phba->sli4_hba.lpfc_abts_nvmet_ctx_list);
		}
		els_xri_cmpl =
			list_empty(&phba->sli4_hba.lpfc_abts_els_sgl_list);

	}
}

/**
 * lpfc_sli4_hba_unset - Unset the fcoe hba
 * @phba: Pointer to HBA context object.
 *
 * This function is called in the SLI4 code path to reset the HBA's FCoE
 * function. The caller is not required to hold any lock. This routine
 * issues PCI function reset mailbox command to reset the FCoE function.
 * At the end of the function, it calls lpfc_hba_down_post function to
 * free any pending commands.
 **/
static void
lpfc_sli4_hba_unset(struct lpfc_hba *phba)
{
	int wait_cnt = 0;
	LPFC_MBOXQ_t *mboxq;
	struct pci_dev *pdev = phba->pcidev;

	lpfc_stop_hba_timers(phba);
	if (phba->pport)
		phba->sli4_hba.intr_enable = 0;

	/*
	 * Gracefully wait out the potential current outstanding asynchronous
	 * mailbox command.
	 */

	/* First, block any pending async mailbox command from posted */
	spin_lock_irq(&phba->hbalock);
	phba->sli.sli_flag |= LPFC_SLI_ASYNC_MBX_BLK;
	spin_unlock_irq(&phba->hbalock);
	/* Now, trying to wait it out if we can */
	while (phba->sli.sli_flag & LPFC_SLI_MBOX_ACTIVE) {
		msleep(10);
		if (++wait_cnt > LPFC_ACTIVE_MBOX_WAIT_CNT)
			break;
	}
	/* Forcefully release the outstanding mailbox command if timed out */
	if (phba->sli.sli_flag & LPFC_SLI_MBOX_ACTIVE) {
		spin_lock_irq(&phba->hbalock);
		mboxq = phba->sli.mbox_active;
		mboxq->u.mb.mbxStatus = MBX_NOT_FINISHED;
		__lpfc_mbox_cmpl_put(phba, mboxq);
		phba->sli.sli_flag &= ~LPFC_SLI_MBOX_ACTIVE;
		phba->sli.mbox_active = NULL;
		spin_unlock_irq(&phba->hbalock);
	}

	/* Abort all iocbs associated with the hba */
	lpfc_sli_hba_iocb_abort(phba);

	/* Wait for completion of device XRI exchange busy */
	lpfc_sli4_xri_exchange_busy_wait(phba);

	/* per-phba callback de-registration for hotplug event */
	lpfc_cpuhp_remove(phba);

	/* Disable PCI subsystem interrupt */
	lpfc_sli4_disable_intr(phba);

	/* Disable SR-IOV if enabled */
	if (phba->cfg_sriov_nr_virtfn)
		pci_disable_sriov(pdev);

	/* Stop kthread signal shall trigger work_done one more time */
	kthread_stop(phba->worker_thread);

	/* Disable FW logging to host memory */
	lpfc_ras_stop_fwlog(phba);

	/* Unset the queues shared with the hardware then release all
	 * allocated resources.
	 */
	lpfc_sli4_queue_unset(phba);
	lpfc_sli4_queue_destroy(phba);

	/* Reset SLI4 HBA FCoE function */
	lpfc_pci_function_reset(phba);

	/* Free RAS DMA memory */
	if (phba->ras_fwlog.ras_enabled)
		lpfc_sli4_ras_dma_free(phba);

	/* Stop the SLI4 device port */
	if (phba->pport)
		phba->pport->work_port_events = 0;
}

 /**
 * lpfc_pc_sli4_params_get - Get the SLI4_PARAMS port capabilities.
 * @phba: Pointer to HBA context object.
 * @mboxq: Pointer to the mailboxq memory for the mailbox command response.
 *
 * This function is called in the SLI4 code path to read the port's
 * sli4 capabilities.
 *
 * This function may be be called from any context that can block-wait
 * for the completion.  The expectation is that this routine is called
 * typically from probe_one or from the online routine.
 **/
int
lpfc_pc_sli4_params_get(struct lpfc_hba *phba, LPFC_MBOXQ_t *mboxq)
{
	int rc;
	struct lpfc_mqe *mqe;
	struct lpfc_pc_sli4_params *sli4_params;
	uint32_t mbox_tmo;

	rc = 0;
	mqe = &mboxq->u.mqe;

	/* Read the port's SLI4 Parameters port capabilities */
	lpfc_pc_sli4_params(mboxq);
	if (!phba->sli4_hba.intr_enable)
		rc = lpfc_sli_issue_mbox(phba, mboxq, MBX_POLL);
	else {
		mbox_tmo = lpfc_mbox_tmo_val(phba, mboxq);
		rc = lpfc_sli_issue_mbox_wait(phba, mboxq, mbox_tmo);
	}

	if (unlikely(rc))
		return 1;

	sli4_params = &phba->sli4_hba.pc_sli4_params;
	sli4_params->if_type = bf_get(if_type, &mqe->un.sli4_params);
	sli4_params->sli_rev = bf_get(sli_rev, &mqe->un.sli4_params);
	sli4_params->sli_family = bf_get(sli_family, &mqe->un.sli4_params);
	sli4_params->featurelevel_1 = bf_get(featurelevel_1,
					     &mqe->un.sli4_params);
	sli4_params->featurelevel_2 = bf_get(featurelevel_2,
					     &mqe->un.sli4_params);
	sli4_params->proto_types = mqe->un.sli4_params.word3;
	sli4_params->sge_supp_len = mqe->un.sli4_params.sge_supp_len;
	sli4_params->if_page_sz = bf_get(if_page_sz, &mqe->un.sli4_params);
	sli4_params->rq_db_window = bf_get(rq_db_window, &mqe->un.sli4_params);
	sli4_params->loopbk_scope = bf_get(loopbk_scope, &mqe->un.sli4_params);
	sli4_params->eq_pages_max = bf_get(eq_pages, &mqe->un.sli4_params);
	sli4_params->eqe_size = bf_get(eqe_size, &mqe->un.sli4_params);
	sli4_params->cq_pages_max = bf_get(cq_pages, &mqe->un.sli4_params);
	sli4_params->cqe_size = bf_get(cqe_size, &mqe->un.sli4_params);
	sli4_params->mq_pages_max = bf_get(mq_pages, &mqe->un.sli4_params);
	sli4_params->mqe_size = bf_get(mqe_size, &mqe->un.sli4_params);
	sli4_params->mq_elem_cnt = bf_get(mq_elem_cnt, &mqe->un.sli4_params);
	sli4_params->wq_pages_max = bf_get(wq_pages, &mqe->un.sli4_params);
	sli4_params->wqe_size = bf_get(wqe_size, &mqe->un.sli4_params);
	sli4_params->rq_pages_max = bf_get(rq_pages, &mqe->un.sli4_params);
	sli4_params->rqe_size = bf_get(rqe_size, &mqe->un.sli4_params);
	sli4_params->hdr_pages_max = bf_get(hdr_pages, &mqe->un.sli4_params);
	sli4_params->hdr_size = bf_get(hdr_size, &mqe->un.sli4_params);
	sli4_params->hdr_pp_align = bf_get(hdr_pp_align, &mqe->un.sli4_params);
	sli4_params->sgl_pages_max = bf_get(sgl_pages, &mqe->un.sli4_params);
	sli4_params->sgl_pp_align = bf_get(sgl_pp_align, &mqe->un.sli4_params);

	/* Make sure that sge_supp_len can be handled by the driver */
	if (sli4_params->sge_supp_len > LPFC_MAX_SGE_SIZE)
		sli4_params->sge_supp_len = LPFC_MAX_SGE_SIZE;

	return rc;
}

/**
 * lpfc_get_sli4_parameters - Get the SLI4 Config PARAMETERS.
 * @phba: Pointer to HBA context object.
 * @mboxq: Pointer to the mailboxq memory for the mailbox command response.
 *
 * This function is called in the SLI4 code path to read the port's
 * sli4 capabilities.
 *
 * This function may be be called from any context that can block-wait
 * for the completion.  The expectation is that this routine is called
 * typically from probe_one or from the online routine.
 **/
int
lpfc_get_sli4_parameters(struct lpfc_hba *phba, LPFC_MBOXQ_t *mboxq)
{
	int rc;
	struct lpfc_mqe *mqe = &mboxq->u.mqe;
	struct lpfc_pc_sli4_params *sli4_params;
	uint32_t mbox_tmo;
	int length;
	bool exp_wqcq_pages = true;
	struct lpfc_sli4_parameters *mbx_sli4_parameters;

	/*
	 * By default, the driver assumes the SLI4 port requires RPI
	 * header postings.  The SLI4_PARAM response will correct this
	 * assumption.
	 */
	phba->sli4_hba.rpi_hdrs_in_use = 1;

	/* Read the port's SLI4 Config Parameters */
	length = (sizeof(struct lpfc_mbx_get_sli4_parameters) -
		  sizeof(struct lpfc_sli4_cfg_mhdr));
	lpfc_sli4_config(phba, mboxq, LPFC_MBOX_SUBSYSTEM_COMMON,
			 LPFC_MBOX_OPCODE_GET_SLI4_PARAMETERS,
			 length, LPFC_SLI4_MBX_EMBED);
	if (!phba->sli4_hba.intr_enable)
		rc = lpfc_sli_issue_mbox(phba, mboxq, MBX_POLL);
	else {
		mbox_tmo = lpfc_mbox_tmo_val(phba, mboxq);
		rc = lpfc_sli_issue_mbox_wait(phba, mboxq, mbox_tmo);
	}
	if (unlikely(rc))
		return rc;
	sli4_params = &phba->sli4_hba.pc_sli4_params;
	mbx_sli4_parameters = &mqe->un.get_sli4_parameters.sli4_parameters;
	sli4_params->if_type = bf_get(cfg_if_type, mbx_sli4_parameters);
	sli4_params->sli_rev = bf_get(cfg_sli_rev, mbx_sli4_parameters);
	sli4_params->sli_family = bf_get(cfg_sli_family, mbx_sli4_parameters);
	sli4_params->featurelevel_1 = bf_get(cfg_sli_hint_1,
					     mbx_sli4_parameters);
	sli4_params->featurelevel_2 = bf_get(cfg_sli_hint_2,
					     mbx_sli4_parameters);
	if (bf_get(cfg_phwq, mbx_sli4_parameters))
		phba->sli3_options |= LPFC_SLI4_PHWQ_ENABLED;
	else
		phba->sli3_options &= ~LPFC_SLI4_PHWQ_ENABLED;
	sli4_params->sge_supp_len = mbx_sli4_parameters->sge_supp_len;
	sli4_params->loopbk_scope = bf_get(loopbk_scope, mbx_sli4_parameters);
	sli4_params->oas_supported = bf_get(cfg_oas, mbx_sli4_parameters);
	sli4_params->cqv = bf_get(cfg_cqv, mbx_sli4_parameters);
	sli4_params->mqv = bf_get(cfg_mqv, mbx_sli4_parameters);
	sli4_params->wqv = bf_get(cfg_wqv, mbx_sli4_parameters);
	sli4_params->rqv = bf_get(cfg_rqv, mbx_sli4_parameters);
	sli4_params->eqav = bf_get(cfg_eqav, mbx_sli4_parameters);
	sli4_params->cqav = bf_get(cfg_cqav, mbx_sli4_parameters);
	sli4_params->wqsize = bf_get(cfg_wqsize, mbx_sli4_parameters);
	sli4_params->bv1s = bf_get(cfg_bv1s, mbx_sli4_parameters);
	sli4_params->pls = bf_get(cfg_pvl, mbx_sli4_parameters);
	sli4_params->sgl_pages_max = bf_get(cfg_sgl_page_cnt,
					    mbx_sli4_parameters);
	sli4_params->wqpcnt = bf_get(cfg_wqpcnt, mbx_sli4_parameters);
	sli4_params->sgl_pp_align = bf_get(cfg_sgl_pp_align,
					   mbx_sli4_parameters);
	phba->sli4_hba.extents_in_use = bf_get(cfg_ext, mbx_sli4_parameters);
	phba->sli4_hba.rpi_hdrs_in_use = bf_get(cfg_hdrr, mbx_sli4_parameters);

	/* Check for Extended Pre-Registered SGL support */
	phba->cfg_xpsgl = bf_get(cfg_xpsgl, mbx_sli4_parameters);

	/* Check for firmware nvme support */
	rc = (bf_get(cfg_nvme, mbx_sli4_parameters) &&
		     bf_get(cfg_xib, mbx_sli4_parameters));

	if (rc) {
		/* Save this to indicate the Firmware supports NVME */
		sli4_params->nvme = 1;

		/* Firmware NVME support, check driver FC4 NVME support */
		if (phba->cfg_enable_fc4_type == LPFC_ENABLE_FCP) {
			lpfc_printf_log(phba, KERN_INFO, LOG_INIT | LOG_NVME,
					"6133 Disabling NVME support: "
					"FC4 type not supported: x%x\n",
					phba->cfg_enable_fc4_type);
			goto fcponly;
		}
	} else {
		/* No firmware NVME support, check driver FC4 NVME support */
		sli4_params->nvme = 0;
		if (phba->cfg_enable_fc4_type & LPFC_ENABLE_NVME) {
			lpfc_printf_log(phba, KERN_ERR, LOG_INIT | LOG_NVME,
					"6101 Disabling NVME support: Not "
					"supported by firmware (%d %d) x%x\n",
					bf_get(cfg_nvme, mbx_sli4_parameters),
					bf_get(cfg_xib, mbx_sli4_parameters),
					phba->cfg_enable_fc4_type);
fcponly:
			phba->nvme_support = 0;
			phba->nvmet_support = 0;
			phba->cfg_nvmet_mrq = 0;
			phba->cfg_nvme_seg_cnt = 0;

			/* If no FC4 type support, move to just SCSI support */
			if (!(phba->cfg_enable_fc4_type & LPFC_ENABLE_FCP))
				return -ENODEV;
			phba->cfg_enable_fc4_type = LPFC_ENABLE_FCP;
		}
	}

	/* If the NVME FC4 type is enabled, scale the sg_seg_cnt to
	 * accommodate 512K and 1M IOs in a single nvme buf.
	 */
	if (phba->cfg_enable_fc4_type & LPFC_ENABLE_NVME)
		phba->cfg_sg_seg_cnt = LPFC_MAX_NVME_SEG_CNT;

	/* Only embed PBDE for if_type 6, PBDE support requires xib be set */
	if ((bf_get(lpfc_sli_intf_if_type, &phba->sli4_hba.sli_intf) !=
	    LPFC_SLI_INTF_IF_TYPE_6) || (!bf_get(cfg_xib, mbx_sli4_parameters)))
		phba->cfg_enable_pbde = 0;

	/*
	 * To support Suppress Response feature we must satisfy 3 conditions.
	 * lpfc_suppress_rsp module parameter must be set (default).
	 * In SLI4-Parameters Descriptor:
	 * Extended Inline Buffers (XIB) must be supported.
	 * Suppress Response IU Not Supported (SRIUNS) must NOT be supported
	 * (double negative).
	 */
	if (phba->cfg_suppress_rsp && bf_get(cfg_xib, mbx_sli4_parameters) &&
	    !(bf_get(cfg_nosr, mbx_sli4_parameters)))
		phba->sli.sli_flag |= LPFC_SLI_SUPPRESS_RSP;
	else
		phba->cfg_suppress_rsp = 0;

	if (bf_get(cfg_eqdr, mbx_sli4_parameters))
		phba->sli.sli_flag |= LPFC_SLI_USE_EQDR;

	/* Make sure that sge_supp_len can be handled by the driver */
	if (sli4_params->sge_supp_len > LPFC_MAX_SGE_SIZE)
		sli4_params->sge_supp_len = LPFC_MAX_SGE_SIZE;

	/*
	 * Check whether the adapter supports an embedded copy of the
	 * FCP CMD IU within the WQE for FCP_Ixxx commands. In order
	 * to use this option, 128-byte WQEs must be used.
	 */
	if (bf_get(cfg_ext_embed_cb, mbx_sli4_parameters))
		phba->fcp_embed_io = 1;
	else
		phba->fcp_embed_io = 0;

	lpfc_printf_log(phba, KERN_INFO, LOG_INIT | LOG_NVME,
			"6422 XIB %d PBDE %d: FCP %d NVME %d %d %d\n",
			bf_get(cfg_xib, mbx_sli4_parameters),
			phba->cfg_enable_pbde,
			phba->fcp_embed_io, phba->nvme_support,
			phba->cfg_nvme_embed_cmd, phba->cfg_suppress_rsp);

	if ((bf_get(lpfc_sli_intf_if_type, &phba->sli4_hba.sli_intf) ==
	    LPFC_SLI_INTF_IF_TYPE_2) &&
	    (bf_get(lpfc_sli_intf_sli_family, &phba->sli4_hba.sli_intf) ==
		 LPFC_SLI_INTF_FAMILY_LNCR_A0))
		exp_wqcq_pages = false;

	if ((bf_get(cfg_cqpsize, mbx_sli4_parameters) & LPFC_CQ_16K_PAGE_SZ) &&
	    (bf_get(cfg_wqpsize, mbx_sli4_parameters) & LPFC_WQ_16K_PAGE_SZ) &&
	    exp_wqcq_pages &&
	    (sli4_params->wqsize & LPFC_WQ_SZ128_SUPPORT))
		phba->enab_exp_wqcq_pages = 1;
	else
		phba->enab_exp_wqcq_pages = 0;
	/*
	 * Check if the SLI port supports MDS Diagnostics
	 */
	if (bf_get(cfg_mds_diags, mbx_sli4_parameters))
		phba->mds_diags_support = 1;
	else
		phba->mds_diags_support = 0;

	/*
	 * Check if the SLI port supports NSLER
	 */
	if (bf_get(cfg_nsler, mbx_sli4_parameters))
		phba->nsler = 1;
	else
		phba->nsler = 0;

	return 0;
}

/**
 * lpfc_pci_probe_one_s3 - PCI probe func to reg SLI-3 device to PCI subsystem.
 * @pdev: pointer to PCI device
 * @pid: pointer to PCI device identifier
 *
 * This routine is to be called to attach a device with SLI-3 interface spec
 * to the PCI subsystem. When an Emulex HBA with SLI-3 interface spec is
 * presented on PCI bus, the kernel PCI subsystem looks at PCI device-specific
 * information of the device and driver to see if the driver state that it can
 * support this kind of device. If the match is successful, the driver core
 * invokes this routine. If this routine determines it can claim the HBA, it
 * does all the initialization that it needs to do to handle the HBA properly.
 *
 * Return code
 * 	0 - driver can claim the device
 * 	negative value - driver can not claim the device
 **/
static int
lpfc_pci_probe_one_s3(struct pci_dev *pdev, const struct pci_device_id *pid)
{
	struct lpfc_hba   *phba;
	struct lpfc_vport *vport = NULL;
	struct Scsi_Host  *shost = NULL;
	int error;
	uint32_t cfg_mode, intr_mode;

	/* Allocate memory for HBA structure */
	phba = lpfc_hba_alloc(pdev);
	if (!phba)
		return -ENOMEM;

	/* Perform generic PCI device enabling operation */
	error = lpfc_enable_pci_dev(phba);
	if (error)
		goto out_free_phba;

	/* Set up SLI API function jump table for PCI-device group-0 HBAs */
	error = lpfc_api_table_setup(phba, LPFC_PCI_DEV_LP);
	if (error)
		goto out_disable_pci_dev;

	/* Set up SLI-3 specific device PCI memory space */
	error = lpfc_sli_pci_mem_setup(phba);
	if (error) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"1402 Failed to set up pci memory space.\n");
		goto out_disable_pci_dev;
	}

	/* Set up SLI-3 specific device driver resources */
	error = lpfc_sli_driver_resource_setup(phba);
	if (error) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"1404 Failed to set up driver resource.\n");
		goto out_unset_pci_mem_s3;
	}

	/* Initialize and populate the iocb list per host */

	error = lpfc_init_iocb_list(phba, LPFC_IOCB_LIST_CNT);
	if (error) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"1405 Failed to initialize iocb list.\n");
		goto out_unset_driver_resource_s3;
	}

	/* Set up common device driver resources */
	error = lpfc_setup_driver_resource_phase2(phba);
	if (error) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"1406 Failed to set up driver resource.\n");
		goto out_free_iocb_list;
	}

	/* Get the default values for Model Name and Description */
	lpfc_get_hba_model_desc(phba, phba->ModelName, phba->ModelDesc);

	/* Create SCSI host to the physical port */
	error = lpfc_create_shost(phba);
	if (error) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"1407 Failed to create scsi host.\n");
		goto out_unset_driver_resource;
	}

	/* Configure sysfs attributes */
	vport = phba->pport;
	error = lpfc_alloc_sysfs_attr(vport);
	if (error) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"1476 Failed to allocate sysfs attr\n");
		goto out_destroy_shost;
	}

	shost = lpfc_shost_from_vport(vport); /* save shost for error cleanup */
	/* Now, trying to enable interrupt and bring up the device */
	cfg_mode = phba->cfg_use_msi;
	while (true) {
		/* Put device to a known state before enabling interrupt */
		lpfc_stop_port(phba);
		/* Configure and enable interrupt */
		intr_mode = lpfc_sli_enable_intr(phba, cfg_mode);
		if (intr_mode == LPFC_INTR_ERROR) {
			lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
					"0431 Failed to enable interrupt.\n");
			error = -ENODEV;
			goto out_free_sysfs_attr;
		}
		/* SLI-3 HBA setup */
		if (lpfc_sli_hba_setup(phba)) {
			lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
					"1477 Failed to set up hba\n");
			error = -ENODEV;
			goto out_remove_device;
		}

		/* Wait 50ms for the interrupts of previous mailbox commands */
		msleep(50);
		/* Check active interrupts on message signaled interrupts */
		if (intr_mode == 0 ||
		    phba->sli.slistat.sli_intr > LPFC_MSIX_VECTORS) {
			/* Log the current active interrupt mode */
			phba->intr_mode = intr_mode;
			lpfc_log_intr_mode(phba, intr_mode);
			break;
		} else {
			lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
					"0447 Configure interrupt mode (%d) "
					"failed active interrupt test.\n",
					intr_mode);
			/* Disable the current interrupt mode */
			lpfc_sli_disable_intr(phba);
			/* Try next level of interrupt mode */
			cfg_mode = --intr_mode;
		}
	}

	/* Perform post initialization setup */
	lpfc_post_init_setup(phba);

	/* Check if there are static vports to be created. */
	lpfc_create_static_vport(phba);

	return 0;

out_remove_device:
	lpfc_unset_hba(phba);
out_free_sysfs_attr:
	lpfc_free_sysfs_attr(vport);
out_destroy_shost:
	lpfc_destroy_shost(phba);
out_unset_driver_resource:
	lpfc_unset_driver_resource_phase2(phba);
out_free_iocb_list:
	lpfc_free_iocb_list(phba);
out_unset_driver_resource_s3:
	lpfc_sli_driver_resource_unset(phba);
out_unset_pci_mem_s3:
	lpfc_sli_pci_mem_unset(phba);
out_disable_pci_dev:
	lpfc_disable_pci_dev(phba);
	if (shost)
		scsi_host_put(shost);
out_free_phba:
	lpfc_hba_free(phba);
	return error;
}

/**
 * lpfc_pci_remove_one_s3 - PCI func to unreg SLI-3 device from PCI subsystem.
 * @pdev: pointer to PCI device
 *
 * This routine is to be called to disattach a device with SLI-3 interface
 * spec from PCI subsystem. When an Emulex HBA with SLI-3 interface spec is
 * removed from PCI bus, it performs all the necessary cleanup for the HBA
 * device to be removed from the PCI subsystem properly.
 **/
static void
lpfc_pci_remove_one_s3(struct pci_dev *pdev)
{
	struct Scsi_Host  *shost = pci_get_drvdata(pdev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_vport **vports;
	struct lpfc_hba   *phba = vport->phba;
	int i;

	spin_lock_irq(&phba->hbalock);
	vport->load_flag |= FC_UNLOADING;
	spin_unlock_irq(&phba->hbalock);

	lpfc_free_sysfs_attr(vport);

	/* Release all the vports against this physical port */
	vports = lpfc_create_vport_work_array(phba);
	if (vports != NULL)
		for (i = 0; i <= phba->max_vports && vports[i] != NULL; i++) {
			if (vports[i]->port_type == LPFC_PHYSICAL_PORT)
				continue;
			fc_vport_terminate(vports[i]->fc_vport);
		}
	lpfc_destroy_vport_work_array(phba, vports);

	/* Remove FC host and then SCSI host with the physical port */
	fc_remove_host(shost);
	scsi_remove_host(shost);

	lpfc_cleanup(vport);

	/*
	 * Bring down the SLI Layer. This step disable all interrupts,
	 * clears the rings, discards all mailbox commands, and resets
	 * the HBA.
	 */

	/* HBA interrupt will be disabled after this call */
	lpfc_sli_hba_down(phba);
	/* Stop kthread signal shall trigger work_done one more time */
	kthread_stop(phba->worker_thread);
	/* Final cleanup of txcmplq and reset the HBA */
	lpfc_sli_brdrestart(phba);

	kfree(phba->vpi_bmask);
	kfree(phba->vpi_ids);

	lpfc_stop_hba_timers(phba);
	spin_lock_irq(&phba->port_list_lock);
	list_del_init(&vport->listentry);
	spin_unlock_irq(&phba->port_list_lock);

	lpfc_debugfs_terminate(vport);

	/* Disable SR-IOV if enabled */
	if (phba->cfg_sriov_nr_virtfn)
		pci_disable_sriov(pdev);

	/* Disable interrupt */
	lpfc_sli_disable_intr(phba);

	scsi_host_put(shost);

	/*
	 * Call scsi_free before mem_free since scsi bufs are released to their
	 * corresponding pools here.
	 */
	lpfc_scsi_free(phba);
	lpfc_free_iocb_list(phba);

	lpfc_mem_free_all(phba);

	dma_free_coherent(&pdev->dev, lpfc_sli_hbq_size(),
			  phba->hbqslimp.virt, phba->hbqslimp.phys);

	/* Free resources associated with SLI2 interface */
	dma_free_coherent(&pdev->dev, SLI2_SLIM_SIZE,
			  phba->slim2p.virt, phba->slim2p.phys);

	/* unmap adapter SLIM and Control Registers */
	iounmap(phba->ctrl_regs_memmap_p);
	iounmap(phba->slim_memmap_p);

	lpfc_hba_free(phba);

	pci_release_mem_regions(pdev);
	pci_disable_device(pdev);
}

/**
 * lpfc_pci_suspend_one_s3 - PCI func to suspend SLI-3 device for power mgmnt
 * @pdev: pointer to PCI device
 * @msg: power management message
 *
 * This routine is to be called from the kernel's PCI subsystem to support
 * system Power Management (PM) to device with SLI-3 interface spec. When
 * PM invokes this method, it quiesces the device by stopping the driver's
 * worker thread for the device, turning off device's interrupt and DMA,
 * and bring the device offline. Note that as the driver implements the
 * minimum PM requirements to a power-aware driver's PM support for the
 * suspend/resume -- all the possible PM messages (SUSPEND, HIBERNATE, FREEZE)
 * to the suspend() method call will be treated as SUSPEND and the driver will
 * fully reinitialize its device during resume() method call, the driver will
 * set device to PCI_D3hot state in PCI config space instead of setting it
 * according to the @msg provided by the PM.
 *
 * Return code
 * 	0 - driver suspended the device
 * 	Error otherwise
 **/
static int
lpfc_pci_suspend_one_s3(struct pci_dev *pdev, pm_message_t msg)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct lpfc_hba *phba = ((struct lpfc_vport *)shost->hostdata)->phba;

	lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
			"0473 PCI device Power Management suspend.\n");

	/* Bring down the device */
	lpfc_offline_prep(phba, LPFC_MBX_WAIT);
	lpfc_offline(phba);
	kthread_stop(phba->worker_thread);

	/* Disable interrupt from device */
	lpfc_sli_disable_intr(phba);

	/* Save device state to PCI config space */
	pci_save_state(pdev);
	pci_set_power_state(pdev, PCI_D3hot);

	return 0;
}

/**
 * lpfc_pci_resume_one_s3 - PCI func to resume SLI-3 device for power mgmnt
 * @pdev: pointer to PCI device
 *
 * This routine is to be called from the kernel's PCI subsystem to support
 * system Power Management (PM) to device with SLI-3 interface spec. When PM
 * invokes this method, it restores the device's PCI config space state and
 * fully reinitializes the device and brings it online. Note that as the
 * driver implements the minimum PM requirements to a power-aware driver's
 * PM for suspend/resume -- all the possible PM messages (SUSPEND, HIBERNATE,
 * FREEZE) to the suspend() method call will be treated as SUSPEND and the
 * driver will fully reinitialize its device during resume() method call,
 * the device will be set to PCI_D0 directly in PCI config space before
 * restoring the state.
 *
 * Return code
 * 	0 - driver suspended the device
 * 	Error otherwise
 **/
static int
lpfc_pci_resume_one_s3(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct lpfc_hba *phba = ((struct lpfc_vport *)shost->hostdata)->phba;
	uint32_t intr_mode;
	int error;

	lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
			"0452 PCI device Power Management resume.\n");

	/* Restore device state from PCI config space */
	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);

	/*
	 * As the new kernel behavior of pci_restore_state() API call clears
	 * device saved_state flag, need to save the restored state again.
	 */
	pci_save_state(pdev);

	if (pdev->is_busmaster)
		pci_set_master(pdev);

	/* Startup the kernel thread for this host adapter. */
	phba->worker_thread = kthread_run(lpfc_do_work, phba,
					"lpfc_worker_%d", phba->brd_no);
	if (IS_ERR(phba->worker_thread)) {
		error = PTR_ERR(phba->worker_thread);
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0434 PM resume failed to start worker "
				"thread: error=x%x.\n", error);
		return error;
	}

	/* Configure and enable interrupt */
	intr_mode = lpfc_sli_enable_intr(phba, phba->intr_mode);
	if (intr_mode == LPFC_INTR_ERROR) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0430 PM resume Failed to enable interrupt\n");
		return -EIO;
	} else
		phba->intr_mode = intr_mode;

	/* Restart HBA and bring it online */
	lpfc_sli_brdrestart(phba);
	lpfc_online(phba);

	/* Log the current active interrupt mode */
	lpfc_log_intr_mode(phba, phba->intr_mode);

	return 0;
}

/**
 * lpfc_sli_prep_dev_for_recover - Prepare SLI3 device for pci slot recover
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is called to prepare the SLI3 device for PCI slot recover. It
 * aborts all the outstanding SCSI I/Os to the pci device.
 **/
static void
lpfc_sli_prep_dev_for_recover(struct lpfc_hba *phba)
{
	lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
			"2723 PCI channel I/O abort preparing for recovery\n");

	/*
	 * There may be errored I/Os through HBA, abort all I/Os on txcmplq
	 * and let the SCSI mid-layer to retry them to recover.
	 */
	lpfc_sli_abort_fcp_rings(phba);
}

/**
 * lpfc_sli_prep_dev_for_reset - Prepare SLI3 device for pci slot reset
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is called to prepare the SLI3 device for PCI slot reset. It
 * disables the device interrupt and pci device, and aborts the internal FCP
 * pending I/Os.
 **/
static void
lpfc_sli_prep_dev_for_reset(struct lpfc_hba *phba)
{
	lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
			"2710 PCI channel disable preparing for reset\n");

	/* Block any management I/Os to the device */
	lpfc_block_mgmt_io(phba, LPFC_MBX_WAIT);

	/* Block all SCSI devices' I/Os on the host */
	lpfc_scsi_dev_block(phba);

	/* Flush all driver's outstanding SCSI I/Os as we are to reset */
	lpfc_sli_flush_io_rings(phba);

	/* stop all timers */
	lpfc_stop_hba_timers(phba);

	/* Disable interrupt and pci device */
	lpfc_sli_disable_intr(phba);
	pci_disable_device(phba->pcidev);
}

/**
 * lpfc_sli_prep_dev_for_perm_failure - Prepare SLI3 dev for pci slot disable
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is called to prepare the SLI3 device for PCI slot permanently
 * disabling. It blocks the SCSI transport layer traffic and flushes the FCP
 * pending I/Os.
 **/
static void
lpfc_sli_prep_dev_for_perm_failure(struct lpfc_hba *phba)
{
	lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
			"2711 PCI channel permanent disable for failure\n");
	/* Block all SCSI devices' I/Os on the host */
	lpfc_scsi_dev_block(phba);

	/* stop all timers */
	lpfc_stop_hba_timers(phba);

	/* Clean up all driver's outstanding SCSI I/Os */
	lpfc_sli_flush_io_rings(phba);
}

/**
 * lpfc_io_error_detected_s3 - Method for handling SLI-3 device PCI I/O error
 * @pdev: pointer to PCI device.
 * @state: the current PCI connection state.
 *
 * This routine is called from the PCI subsystem for I/O error handling to
 * device with SLI-3 interface spec. This function is called by the PCI
 * subsystem after a PCI bus error affecting this device has been detected.
 * When this function is invoked, it will need to stop all the I/Os and
 * interrupt(s) to the device. Once that is done, it will return
 * PCI_ERS_RESULT_NEED_RESET for the PCI subsystem to perform proper recovery
 * as desired.
 *
 * Return codes
 * 	PCI_ERS_RESULT_CAN_RECOVER - can be recovered with reset_link
 * 	PCI_ERS_RESULT_NEED_RESET - need to reset before recovery
 * 	PCI_ERS_RESULT_DISCONNECT - device could not be recovered
 **/
static pci_ers_result_t
lpfc_io_error_detected_s3(struct pci_dev *pdev, pci_channel_state_t state)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct lpfc_hba *phba = ((struct lpfc_vport *)shost->hostdata)->phba;

	switch (state) {
	case pci_channel_io_normal:
		/* Non-fatal error, prepare for recovery */
		lpfc_sli_prep_dev_for_recover(phba);
		return PCI_ERS_RESULT_CAN_RECOVER;
	case pci_channel_io_frozen:
		/* Fatal error, prepare for slot reset */
		lpfc_sli_prep_dev_for_reset(phba);
		return PCI_ERS_RESULT_NEED_RESET;
	case pci_channel_io_perm_failure:
		/* Permanent failure, prepare for device down */
		lpfc_sli_prep_dev_for_perm_failure(phba);
		return PCI_ERS_RESULT_DISCONNECT;
	default:
		/* Unknown state, prepare and request slot reset */
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0472 Unknown PCI error state: x%x\n", state);
		lpfc_sli_prep_dev_for_reset(phba);
		return PCI_ERS_RESULT_NEED_RESET;
	}
}

/**
 * lpfc_io_slot_reset_s3 - Method for restarting PCI SLI-3 device from scratch.
 * @pdev: pointer to PCI device.
 *
 * This routine is called from the PCI subsystem for error handling to
 * device with SLI-3 interface spec. This is called after PCI bus has been
 * reset to restart the PCI card from scratch, as if from a cold-boot.
 * During the PCI subsystem error recovery, after driver returns
 * PCI_ERS_RESULT_NEED_RESET, the PCI subsystem will perform proper error
 * recovery and then call this routine before calling the .resume method
 * to recover the device. This function will initialize the HBA device,
 * enable the interrupt, but it will just put the HBA to offline state
 * without passing any I/O traffic.
 *
 * Return codes
 * 	PCI_ERS_RESULT_RECOVERED - the device has been recovered
 * 	PCI_ERS_RESULT_DISCONNECT - device could not be recovered
 */
static pci_ers_result_t
lpfc_io_slot_reset_s3(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct lpfc_hba *phba = ((struct lpfc_vport *)shost->hostdata)->phba;
	struct lpfc_sli *psli = &phba->sli;
	uint32_t intr_mode;

	dev_printk(KERN_INFO, &pdev->dev, "recovering from a slot reset.\n");
	if (pci_enable_device_mem(pdev)) {
		printk(KERN_ERR "lpfc: Cannot re-enable "
			"PCI device after reset.\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}

	pci_restore_state(pdev);

	/*
	 * As the new kernel behavior of pci_restore_state() API call clears
	 * device saved_state flag, need to save the restored state again.
	 */
	pci_save_state(pdev);

	if (pdev->is_busmaster)
		pci_set_master(pdev);

	spin_lock_irq(&phba->hbalock);
	psli->sli_flag &= ~LPFC_SLI_ACTIVE;
	spin_unlock_irq(&phba->hbalock);

	/* Configure and enable interrupt */
	intr_mode = lpfc_sli_enable_intr(phba, phba->intr_mode);
	if (intr_mode == LPFC_INTR_ERROR) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0427 Cannot re-enable interrupt after "
				"slot reset.\n");
		return PCI_ERS_RESULT_DISCONNECT;
	} else
		phba->intr_mode = intr_mode;

	/* Take device offline, it will perform cleanup */
	lpfc_offline_prep(phba, LPFC_MBX_WAIT);
	lpfc_offline(phba);
	lpfc_sli_brdrestart(phba);

	/* Log the current active interrupt mode */
	lpfc_log_intr_mode(phba, phba->intr_mode);

	return PCI_ERS_RESULT_RECOVERED;
}

/**
 * lpfc_io_resume_s3 - Method for resuming PCI I/O operation on SLI-3 device.
 * @pdev: pointer to PCI device
 *
 * This routine is called from the PCI subsystem for error handling to device
 * with SLI-3 interface spec. It is called when kernel error recovery tells
 * the lpfc driver that it is ok to resume normal PCI operation after PCI bus
 * error recovery. After this call, traffic can start to flow from this device
 * again.
 */
static void
lpfc_io_resume_s3(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct lpfc_hba *phba = ((struct lpfc_vport *)shost->hostdata)->phba;

	/* Bring device online, it will be no-op for non-fatal error resume */
	lpfc_online(phba);
}

/**
 * lpfc_sli4_get_els_iocb_cnt - Calculate the # of ELS IOCBs to reserve
 * @phba: pointer to lpfc hba data structure.
 *
 * returns the number of ELS/CT IOCBs to reserve
 **/
int
lpfc_sli4_get_els_iocb_cnt(struct lpfc_hba *phba)
{
	int max_xri = phba->sli4_hba.max_cfg_param.max_xri;

	if (phba->sli_rev == LPFC_SLI_REV4) {
		if (max_xri <= 100)
			return 10;
		else if (max_xri <= 256)
			return 25;
		else if (max_xri <= 512)
			return 50;
		else if (max_xri <= 1024)
			return 100;
		else if (max_xri <= 1536)
			return 150;
		else if (max_xri <= 2048)
			return 200;
		else
			return 250;
	} else
		return 0;
}

/**
 * lpfc_sli4_get_iocb_cnt - Calculate the # of total IOCBs to reserve
 * @phba: pointer to lpfc hba data structure.
 *
 * returns the number of ELS/CT + NVMET IOCBs to reserve
 **/
int
lpfc_sli4_get_iocb_cnt(struct lpfc_hba *phba)
{
	int max_xri = lpfc_sli4_get_els_iocb_cnt(phba);

	if (phba->nvmet_support)
		max_xri += LPFC_NVMET_BUF_POST;
	return max_xri;
}


static int
lpfc_log_write_firmware_error(struct lpfc_hba *phba, uint32_t offset,
	uint32_t magic_number, uint32_t ftype, uint32_t fid, uint32_t fsize,
	const struct firmware *fw)
{
	int rc;

	/* Three cases:  (1) FW was not supported on the detected adapter.
	 * (2) FW update has been locked out administratively.
	 * (3) Some other error during FW update.
	 * In each case, an unmaskable message is written to the console
	 * for admin diagnosis.
	 */
	if (offset == ADD_STATUS_FW_NOT_SUPPORTED ||
	    (phba->pcidev->device == PCI_DEVICE_ID_LANCER_G6_FC &&
	     magic_number != MAGIC_NUMBER_G6) ||
	    (phba->pcidev->device == PCI_DEVICE_ID_LANCER_G7_FC &&
	     magic_number != MAGIC_NUMBER_G7)) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"3030 This firmware version is not supported on"
				" this HBA model. Device:%x Magic:%x Type:%x "
				"ID:%x Size %d %zd\n",
				phba->pcidev->device, magic_number, ftype, fid,
				fsize, fw->size);
		rc = -EINVAL;
	} else if (offset == ADD_STATUS_FW_DOWNLOAD_HW_DISABLED) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"3021 Firmware downloads have been prohibited "
				"by a system configuration setting on "
				"Device:%x Magic:%x Type:%x ID:%x Size %d "
				"%zd\n",
				phba->pcidev->device, magic_number, ftype, fid,
				fsize, fw->size);
		rc = -EACCES;
	} else {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"3022 FW Download failed. Add Status x%x "
				"Device:%x Magic:%x Type:%x ID:%x Size %d "
				"%zd\n",
				offset, phba->pcidev->device, magic_number,
				ftype, fid, fsize, fw->size);
		rc = -EIO;
	}
	return rc;
}

/**
 * lpfc_write_firmware - attempt to write a firmware image to the port
 * @fw: pointer to firmware image returned from request_firmware.
 * @context: pointer to firmware image returned from request_firmware.
 * @ret: return value this routine provides to the caller.
 *
 **/
static void
lpfc_write_firmware(const struct firmware *fw, void *context)
{
	struct lpfc_hba *phba = (struct lpfc_hba *)context;
	char fwrev[FW_REV_STR_SIZE];
	struct lpfc_grp_hdr *image;
	struct list_head dma_buffer_list;
	int i, rc = 0;
	struct lpfc_dmabuf *dmabuf, *next;
	uint32_t offset = 0, temp_offset = 0;
	uint32_t magic_number, ftype, fid, fsize;

	/* It can be null in no-wait mode, sanity check */
	if (!fw) {
		rc = -ENXIO;
		goto out;
	}
	image = (struct lpfc_grp_hdr *)fw->data;

	magic_number = be32_to_cpu(image->magic_number);
	ftype = bf_get_be32(lpfc_grp_hdr_file_type, image);
	fid = bf_get_be32(lpfc_grp_hdr_id, image);
	fsize = be32_to_cpu(image->size);

	INIT_LIST_HEAD(&dma_buffer_list);
	lpfc_decode_firmware_rev(phba, fwrev, 1);
	if (strncmp(fwrev, image->revision, strnlen(image->revision, 16))) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"3023 Updating Firmware, Current Version:%s "
				"New Version:%s\n",
				fwrev, image->revision);
		for (i = 0; i < LPFC_MBX_WR_CONFIG_MAX_BDE; i++) {
			dmabuf = kzalloc(sizeof(struct lpfc_dmabuf),
					 GFP_KERNEL);
			if (!dmabuf) {
				rc = -ENOMEM;
				goto release_out;
			}
			dmabuf->virt = dma_alloc_coherent(&phba->pcidev->dev,
							  SLI4_PAGE_SIZE,
							  &dmabuf->phys,
							  GFP_KERNEL);
			if (!dmabuf->virt) {
				kfree(dmabuf);
				rc = -ENOMEM;
				goto release_out;
			}
			list_add_tail(&dmabuf->list, &dma_buffer_list);
		}
		while (offset < fw->size) {
			temp_offset = offset;
			list_for_each_entry(dmabuf, &dma_buffer_list, list) {
				if (temp_offset + SLI4_PAGE_SIZE > fw->size) {
					memcpy(dmabuf->virt,
					       fw->data + temp_offset,
					       fw->size - temp_offset);
					temp_offset = fw->size;
					break;
				}
				memcpy(dmabuf->virt, fw->data + temp_offset,
				       SLI4_PAGE_SIZE);
				temp_offset += SLI4_PAGE_SIZE;
			}
			rc = lpfc_wr_object(phba, &dma_buffer_list,
				    (fw->size - offset), &offset);
			if (rc) {
				rc = lpfc_log_write_firmware_error(phba, offset,
								   magic_number,
								   ftype,
								   fid,
								   fsize,
								   fw);
				goto release_out;
			}
		}
		rc = offset;
	} else
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"3029 Skipped Firmware update, Current "
				"Version:%s New Version:%s\n",
				fwrev, image->revision);

release_out:
	list_for_each_entry_safe(dmabuf, next, &dma_buffer_list, list) {
		list_del(&dmabuf->list);
		dma_free_coherent(&phba->pcidev->dev, SLI4_PAGE_SIZE,
				  dmabuf->virt, dmabuf->phys);
		kfree(dmabuf);
	}
	release_firmware(fw);
out:
	if (rc < 0)
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"3062 Firmware update error, status %d.\n", rc);
	else
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"3024 Firmware update success: size %d.\n", rc);
}

/**
 * lpfc_sli4_request_firmware_update - Request linux generic firmware upgrade
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is called to perform Linux generic firmware upgrade on device
 * that supports such feature.
 **/
int
lpfc_sli4_request_firmware_update(struct lpfc_hba *phba, uint8_t fw_upgrade)
{
	uint8_t file_name[ELX_MODEL_NAME_SIZE];
	int ret;
	const struct firmware *fw;

	/* Only supported on SLI4 interface type 2 for now */
	if (bf_get(lpfc_sli_intf_if_type, &phba->sli4_hba.sli_intf) <
	    LPFC_SLI_INTF_IF_TYPE_2)
		return -EPERM;

	snprintf(file_name, ELX_MODEL_NAME_SIZE, "%s.grp", phba->ModelName);

	if (fw_upgrade == INT_FW_UPGRADE) {
		ret = request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
					file_name, &phba->pcidev->dev,
					GFP_KERNEL, (void *)phba,
					lpfc_write_firmware);
	} else if (fw_upgrade == RUN_FW_UPGRADE) {
		ret = request_firmware(&fw, file_name, &phba->pcidev->dev);
		if (!ret)
			lpfc_write_firmware(fw, (void *)phba);
	} else {
		ret = -EINVAL;
	}

	return ret;
}

/**
 * lpfc_pci_probe_one_s4 - PCI probe func to reg SLI-4 device to PCI subsys
 * @pdev: pointer to PCI device
 * @pid: pointer to PCI device identifier
 *
 * This routine is called from the kernel's PCI subsystem to device with
 * SLI-4 interface spec. When an Emulex HBA with SLI-4 interface spec is
 * presented on PCI bus, the kernel PCI subsystem looks at PCI device-specific
 * information of the device and driver to see if the driver state that it
 * can support this kind of device. If the match is successful, the driver
 * core invokes this routine. If this routine determines it can claim the HBA,
 * it does all the initialization that it needs to do to handle the HBA
 * properly.
 *
 * Return code
 * 	0 - driver can claim the device
 * 	negative value - driver can not claim the device
 **/
static int
lpfc_pci_probe_one_s4(struct pci_dev *pdev, const struct pci_device_id *pid)
{
	struct lpfc_hba   *phba;
	struct lpfc_vport *vport = NULL;
	struct Scsi_Host  *shost = NULL;
	int error;
	uint32_t cfg_mode, intr_mode;

	/* Allocate memory for HBA structure */
	phba = lpfc_hba_alloc(pdev);
	if (!phba)
		return -ENOMEM;

	/* Perform generic PCI device enabling operation */
	error = lpfc_enable_pci_dev(phba);
	if (error)
		goto out_free_phba;

	/* Set up SLI API function jump table for PCI-device group-1 HBAs */
	error = lpfc_api_table_setup(phba, LPFC_PCI_DEV_OC);
	if (error)
		goto out_disable_pci_dev;

	/* Set up SLI-4 specific device PCI memory space */
	error = lpfc_sli4_pci_mem_setup(phba);
	if (error) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"1410 Failed to set up pci memory space.\n");
		goto out_disable_pci_dev;
	}

	/* Set up SLI-4 Specific device driver resources */
	error = lpfc_sli4_driver_resource_setup(phba);
	if (error) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"1412 Failed to set up driver resource.\n");
		goto out_unset_pci_mem_s4;
	}

	INIT_LIST_HEAD(&phba->active_rrq_list);
	INIT_LIST_HEAD(&phba->fcf.fcf_pri_list);

	/* Set up common device driver resources */
	error = lpfc_setup_driver_resource_phase2(phba);
	if (error) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"1414 Failed to set up driver resource.\n");
		goto out_unset_driver_resource_s4;
	}

	/* Get the default values for Model Name and Description */
	lpfc_get_hba_model_desc(phba, phba->ModelName, phba->ModelDesc);

	/* Now, trying to enable interrupt and bring up the device */
	cfg_mode = phba->cfg_use_msi;

	/* Put device to a known state before enabling interrupt */
	phba->pport = NULL;
	lpfc_stop_port(phba);

	/* Init cpu_map array */
	lpfc_cpu_map_array_init(phba);

	/* Init hba_eq_hdl array */
	lpfc_hba_eq_hdl_array_init(phba);

	/* Configure and enable interrupt */
	intr_mode = lpfc_sli4_enable_intr(phba, cfg_mode);
	if (intr_mode == LPFC_INTR_ERROR) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0426 Failed to enable interrupt.\n");
		error = -ENODEV;
		goto out_unset_driver_resource;
	}
	/* Default to single EQ for non-MSI-X */
	if (phba->intr_type != MSIX) {
		phba->cfg_irq_chann = 1;
		if (phba->cfg_enable_fc4_type & LPFC_ENABLE_NVME) {
			if (phba->nvmet_support)
				phba->cfg_nvmet_mrq = 1;
		}
	}
	lpfc_cpu_affinity_check(phba, phba->cfg_irq_chann);

	/* Create SCSI host to the physical port */
	error = lpfc_create_shost(phba);
	if (error) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"1415 Failed to create scsi host.\n");
		goto out_disable_intr;
	}
	vport = phba->pport;
	shost = lpfc_shost_from_vport(vport); /* save shost for error cleanup */

	/* Configure sysfs attributes */
	error = lpfc_alloc_sysfs_attr(vport);
	if (error) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"1416 Failed to allocate sysfs attr\n");
		goto out_destroy_shost;
	}

	/* Set up SLI-4 HBA */
	if (lpfc_sli4_hba_setup(phba)) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"1421 Failed to set up hba\n");
		error = -ENODEV;
		goto out_free_sysfs_attr;
	}

	/* Log the current active interrupt mode */
	phba->intr_mode = intr_mode;
	lpfc_log_intr_mode(phba, intr_mode);

	/* Perform post initialization setup */
	lpfc_post_init_setup(phba);

	/* NVME support in FW earlier in the driver load corrects the
	 * FC4 type making a check for nvme_support unnecessary.
	 */
	if (phba->nvmet_support == 0) {
		if (phba->cfg_enable_fc4_type & LPFC_ENABLE_NVME) {
			/* Create NVME binding with nvme_fc_transport. This
			 * ensures the vport is initialized.  If the localport
			 * create fails, it should not unload the driver to
			 * support field issues.
			 */
			error = lpfc_nvme_create_localport(vport);
			if (error) {
				lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
						"6004 NVME registration "
						"failed, error x%x\n",
						error);
			}
		}
	}

	/* check for firmware upgrade or downgrade */
	if (phba->cfg_request_firmware_upgrade)
		lpfc_sli4_request_firmware_update(phba, INT_FW_UPGRADE);

	/* Check if there are static vports to be created. */
	lpfc_create_static_vport(phba);

	/* Enable RAS FW log support */
	lpfc_sli4_ras_setup(phba);

	INIT_LIST_HEAD(&phba->poll_list);
	cpuhp_state_add_instance_nocalls(lpfc_cpuhp_state, &phba->cpuhp);

	return 0;

out_free_sysfs_attr:
	lpfc_free_sysfs_attr(vport);
out_destroy_shost:
	lpfc_destroy_shost(phba);
out_disable_intr:
	lpfc_sli4_disable_intr(phba);
out_unset_driver_resource:
	lpfc_unset_driver_resource_phase2(phba);
out_unset_driver_resource_s4:
	lpfc_sli4_driver_resource_unset(phba);
out_unset_pci_mem_s4:
	lpfc_sli4_pci_mem_unset(phba);
out_disable_pci_dev:
	lpfc_disable_pci_dev(phba);
	if (shost)
		scsi_host_put(shost);
out_free_phba:
	lpfc_hba_free(phba);
	return error;
}

/**
 * lpfc_pci_remove_one_s4 - PCI func to unreg SLI-4 device from PCI subsystem
 * @pdev: pointer to PCI device
 *
 * This routine is called from the kernel's PCI subsystem to device with
 * SLI-4 interface spec. When an Emulex HBA with SLI-4 interface spec is
 * removed from PCI bus, it performs all the necessary cleanup for the HBA
 * device to be removed from the PCI subsystem properly.
 **/
static void
lpfc_pci_remove_one_s4(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_vport **vports;
	struct lpfc_hba *phba = vport->phba;
	int i;

	/* Mark the device unloading flag */
	spin_lock_irq(&phba->hbalock);
	vport->load_flag |= FC_UNLOADING;
	spin_unlock_irq(&phba->hbalock);

	/* Free the HBA sysfs attributes */
	lpfc_free_sysfs_attr(vport);

	/* Release all the vports against this physical port */
	vports = lpfc_create_vport_work_array(phba);
	if (vports != NULL)
		for (i = 0; i <= phba->max_vports && vports[i] != NULL; i++) {
			if (vports[i]->port_type == LPFC_PHYSICAL_PORT)
				continue;
			fc_vport_terminate(vports[i]->fc_vport);
		}
	lpfc_destroy_vport_work_array(phba, vports);

	/* Remove FC host and then SCSI host with the physical port */
	fc_remove_host(shost);
	scsi_remove_host(shost);

	/* Perform ndlp cleanup on the physical port.  The nvme and nvmet
	 * localports are destroyed after to cleanup all transport memory.
	 */
	lpfc_cleanup(vport);
	lpfc_nvmet_destroy_targetport(phba);
	lpfc_nvme_destroy_localport(vport);

	/* De-allocate multi-XRI pools */
	if (phba->cfg_xri_rebalancing)
		lpfc_destroy_multixri_pools(phba);

	/*
	 * Bring down the SLI Layer. This step disables all interrupts,
	 * clears the rings, discards all mailbox commands, and resets
	 * the HBA FCoE function.
	 */
	lpfc_debugfs_terminate(vport);

	lpfc_stop_hba_timers(phba);
	spin_lock_irq(&phba->port_list_lock);
	list_del_init(&vport->listentry);
	spin_unlock_irq(&phba->port_list_lock);

	/* Perform scsi free before driver resource_unset since scsi
	 * buffers are released to their corresponding pools here.
	 */
	lpfc_io_free(phba);
	lpfc_free_iocb_list(phba);
	lpfc_sli4_hba_unset(phba);

	lpfc_unset_driver_resource_phase2(phba);
	lpfc_sli4_driver_resource_unset(phba);

	/* Unmap adapter Control and Doorbell registers */
	lpfc_sli4_pci_mem_unset(phba);

	/* Release PCI resources and disable device's PCI function */
	scsi_host_put(shost);
	lpfc_disable_pci_dev(phba);

	/* Finally, free the driver's device data structure */
	lpfc_hba_free(phba);

	return;
}

/**
 * lpfc_pci_suspend_one_s4 - PCI func to suspend SLI-4 device for power mgmnt
 * @pdev: pointer to PCI device
 * @msg: power management message
 *
 * This routine is called from the kernel's PCI subsystem to support system
 * Power Management (PM) to device with SLI-4 interface spec. When PM invokes
 * this method, it quiesces the device by stopping the driver's worker
 * thread for the device, turning off device's interrupt and DMA, and bring
 * the device offline. Note that as the driver implements the minimum PM
 * requirements to a power-aware driver's PM support for suspend/resume -- all
 * the possible PM messages (SUSPEND, HIBERNATE, FREEZE) to the suspend()
 * method call will be treated as SUSPEND and the driver will fully
 * reinitialize its device during resume() method call, the driver will set
 * device to PCI_D3hot state in PCI config space instead of setting it
 * according to the @msg provided by the PM.
 *
 * Return code
 * 	0 - driver suspended the device
 * 	Error otherwise
 **/
static int
lpfc_pci_suspend_one_s4(struct pci_dev *pdev, pm_message_t msg)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct lpfc_hba *phba = ((struct lpfc_vport *)shost->hostdata)->phba;

	lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
			"2843 PCI device Power Management suspend.\n");

	/* Bring down the device */
	lpfc_offline_prep(phba, LPFC_MBX_WAIT);
	lpfc_offline(phba);
	kthread_stop(phba->worker_thread);

	/* Disable interrupt from device */
	lpfc_sli4_disable_intr(phba);
	lpfc_sli4_queue_destroy(phba);

	/* Save device state to PCI config space */
	pci_save_state(pdev);
	pci_set_power_state(pdev, PCI_D3hot);

	return 0;
}

/**
 * lpfc_pci_resume_one_s4 - PCI func to resume SLI-4 device for power mgmnt
 * @pdev: pointer to PCI device
 *
 * This routine is called from the kernel's PCI subsystem to support system
 * Power Management (PM) to device with SLI-4 interface spac. When PM invokes
 * this method, it restores the device's PCI config space state and fully
 * reinitializes the device and brings it online. Note that as the driver
 * implements the minimum PM requirements to a power-aware driver's PM for
 * suspend/resume -- all the possible PM messages (SUSPEND, HIBERNATE, FREEZE)
 * to the suspend() method call will be treated as SUSPEND and the driver
 * will fully reinitialize its device during resume() method call, the device
 * will be set to PCI_D0 directly in PCI config space before restoring the
 * state.
 *
 * Return code
 * 	0 - driver suspended the device
 * 	Error otherwise
 **/
static int
lpfc_pci_resume_one_s4(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct lpfc_hba *phba = ((struct lpfc_vport *)shost->hostdata)->phba;
	uint32_t intr_mode;
	int error;

	lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
			"0292 PCI device Power Management resume.\n");

	/* Restore device state from PCI config space */
	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);

	/*
	 * As the new kernel behavior of pci_restore_state() API call clears
	 * device saved_state flag, need to save the restored state again.
	 */
	pci_save_state(pdev);

	if (pdev->is_busmaster)
		pci_set_master(pdev);

	 /* Startup the kernel thread for this host adapter. */
	phba->worker_thread = kthread_run(lpfc_do_work, phba,
					"lpfc_worker_%d", phba->brd_no);
	if (IS_ERR(phba->worker_thread)) {
		error = PTR_ERR(phba->worker_thread);
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0293 PM resume failed to start worker "
				"thread: error=x%x.\n", error);
		return error;
	}

	/* Configure and enable interrupt */
	intr_mode = lpfc_sli4_enable_intr(phba, phba->intr_mode);
	if (intr_mode == LPFC_INTR_ERROR) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0294 PM resume Failed to enable interrupt\n");
		return -EIO;
	} else
		phba->intr_mode = intr_mode;

	/* Restart HBA and bring it online */
	lpfc_sli_brdrestart(phba);
	lpfc_online(phba);

	/* Log the current active interrupt mode */
	lpfc_log_intr_mode(phba, phba->intr_mode);

	return 0;
}

/**
 * lpfc_sli4_prep_dev_for_recover - Prepare SLI4 device for pci slot recover
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is called to prepare the SLI4 device for PCI slot recover. It
 * aborts all the outstanding SCSI I/Os to the pci device.
 **/
static void
lpfc_sli4_prep_dev_for_recover(struct lpfc_hba *phba)
{
	lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
			"2828 PCI channel I/O abort preparing for recovery\n");
	/*
	 * There may be errored I/Os through HBA, abort all I/Os on txcmplq
	 * and let the SCSI mid-layer to retry them to recover.
	 */
	lpfc_sli_abort_fcp_rings(phba);
}

/**
 * lpfc_sli4_prep_dev_for_reset - Prepare SLI4 device for pci slot reset
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is called to prepare the SLI4 device for PCI slot reset. It
 * disables the device interrupt and pci device, and aborts the internal FCP
 * pending I/Os.
 **/
static void
lpfc_sli4_prep_dev_for_reset(struct lpfc_hba *phba)
{
	lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
			"2826 PCI channel disable preparing for reset\n");

	/* Block any management I/Os to the device */
	lpfc_block_mgmt_io(phba, LPFC_MBX_NO_WAIT);

	/* Block all SCSI devices' I/Os on the host */
	lpfc_scsi_dev_block(phba);

	/* Flush all driver's outstanding I/Os as we are to reset */
	lpfc_sli_flush_io_rings(phba);

	/* stop all timers */
	lpfc_stop_hba_timers(phba);

	/* Disable interrupt and pci device */
	lpfc_sli4_disable_intr(phba);
	lpfc_sli4_queue_destroy(phba);
	pci_disable_device(phba->pcidev);
}

/**
 * lpfc_sli4_prep_dev_for_perm_failure - Prepare SLI4 dev for pci slot disable
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine is called to prepare the SLI4 device for PCI slot permanently
 * disabling. It blocks the SCSI transport layer traffic and flushes the FCP
 * pending I/Os.
 **/
static void
lpfc_sli4_prep_dev_for_perm_failure(struct lpfc_hba *phba)
{
	lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
			"2827 PCI channel permanent disable for failure\n");

	/* Block all SCSI devices' I/Os on the host */
	lpfc_scsi_dev_block(phba);

	/* stop all timers */
	lpfc_stop_hba_timers(phba);

	/* Clean up all driver's outstanding I/Os */
	lpfc_sli_flush_io_rings(phba);
}

/**
 * lpfc_io_error_detected_s4 - Method for handling PCI I/O error to SLI-4 device
 * @pdev: pointer to PCI device.
 * @state: the current PCI connection state.
 *
 * This routine is called from the PCI subsystem for error handling to device
 * with SLI-4 interface spec. This function is called by the PCI subsystem
 * after a PCI bus error affecting this device has been detected. When this
 * function is invoked, it will need to stop all the I/Os and interrupt(s)
 * to the device. Once that is done, it will return PCI_ERS_RESULT_NEED_RESET
 * for the PCI subsystem to perform proper recovery as desired.
 *
 * Return codes
 * 	PCI_ERS_RESULT_NEED_RESET - need to reset before recovery
 * 	PCI_ERS_RESULT_DISCONNECT - device could not be recovered
 **/
static pci_ers_result_t
lpfc_io_error_detected_s4(struct pci_dev *pdev, pci_channel_state_t state)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct lpfc_hba *phba = ((struct lpfc_vport *)shost->hostdata)->phba;

	switch (state) {
	case pci_channel_io_normal:
		/* Non-fatal error, prepare for recovery */
		lpfc_sli4_prep_dev_for_recover(phba);
		return PCI_ERS_RESULT_CAN_RECOVER;
	case pci_channel_io_frozen:
		/* Fatal error, prepare for slot reset */
		lpfc_sli4_prep_dev_for_reset(phba);
		return PCI_ERS_RESULT_NEED_RESET;
	case pci_channel_io_perm_failure:
		/* Permanent failure, prepare for device down */
		lpfc_sli4_prep_dev_for_perm_failure(phba);
		return PCI_ERS_RESULT_DISCONNECT;
	default:
		/* Unknown state, prepare and request slot reset */
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"2825 Unknown PCI error state: x%x\n", state);
		lpfc_sli4_prep_dev_for_reset(phba);
		return PCI_ERS_RESULT_NEED_RESET;
	}
}

/**
 * lpfc_io_slot_reset_s4 - Method for restart PCI SLI-4 device from scratch
 * @pdev: pointer to PCI device.
 *
 * This routine is called from the PCI subsystem for error handling to device
 * with SLI-4 interface spec. It is called after PCI bus has been reset to
 * restart the PCI card from scratch, as if from a cold-boot. During the
 * PCI subsystem error recovery, after the driver returns
 * PCI_ERS_RESULT_NEED_RESET, the PCI subsystem will perform proper error
 * recovery and then call this routine before calling the .resume method to
 * recover the device. This function will initialize the HBA device, enable
 * the interrupt, but it will just put the HBA to offline state without
 * passing any I/O traffic.
 *
 * Return codes
 * 	PCI_ERS_RESULT_RECOVERED - the device has been recovered
 * 	PCI_ERS_RESULT_DISCONNECT - device could not be recovered
 */
static pci_ers_result_t
lpfc_io_slot_reset_s4(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct lpfc_hba *phba = ((struct lpfc_vport *)shost->hostdata)->phba;
	struct lpfc_sli *psli = &phba->sli;
	uint32_t intr_mode;

	dev_printk(KERN_INFO, &pdev->dev, "recovering from a slot reset.\n");
	if (pci_enable_device_mem(pdev)) {
		printk(KERN_ERR "lpfc: Cannot re-enable "
			"PCI device after reset.\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}

	pci_restore_state(pdev);

	/*
	 * As the new kernel behavior of pci_restore_state() API call clears
	 * device saved_state flag, need to save the restored state again.
	 */
	pci_save_state(pdev);

	if (pdev->is_busmaster)
		pci_set_master(pdev);

	spin_lock_irq(&phba->hbalock);
	psli->sli_flag &= ~LPFC_SLI_ACTIVE;
	spin_unlock_irq(&phba->hbalock);

	/* Configure and enable interrupt */
	intr_mode = lpfc_sli4_enable_intr(phba, phba->intr_mode);
	if (intr_mode == LPFC_INTR_ERROR) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"2824 Cannot re-enable interrupt after "
				"slot reset.\n");
		return PCI_ERS_RESULT_DISCONNECT;
	} else
		phba->intr_mode = intr_mode;

	/* Log the current active interrupt mode */
	lpfc_log_intr_mode(phba, phba->intr_mode);

	return PCI_ERS_RESULT_RECOVERED;
}

/**
 * lpfc_io_resume_s4 - Method for resuming PCI I/O operation to SLI-4 device
 * @pdev: pointer to PCI device
 *
 * This routine is called from the PCI subsystem for error handling to device
 * with SLI-4 interface spec. It is called when kernel error recovery tells
 * the lpfc driver that it is ok to resume normal PCI operation after PCI bus
 * error recovery. After this call, traffic can start to flow from this device
 * again.
 **/
static void
lpfc_io_resume_s4(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct lpfc_hba *phba = ((struct lpfc_vport *)shost->hostdata)->phba;

	/*
	 * In case of slot reset, as function reset is performed through
	 * mailbox command which needs DMA to be enabled, this operation
	 * has to be moved to the io resume phase. Taking device offline
	 * will perform the necessary cleanup.
	 */
	if (!(phba->sli.sli_flag & LPFC_SLI_ACTIVE)) {
		/* Perform device reset */
		lpfc_offline_prep(phba, LPFC_MBX_WAIT);
		lpfc_offline(phba);
		lpfc_sli_brdrestart(phba);
		/* Bring the device back online */
		lpfc_online(phba);
	}
}

/**
 * lpfc_pci_probe_one - lpfc PCI probe func to reg dev to PCI subsystem
 * @pdev: pointer to PCI device
 * @pid: pointer to PCI device identifier
 *
 * This routine is to be registered to the kernel's PCI subsystem. When an
 * Emulex HBA device is presented on PCI bus, the kernel PCI subsystem looks
 * at PCI device-specific information of the device and driver to see if the
 * driver state that it can support this kind of device. If the match is
 * successful, the driver core invokes this routine. This routine dispatches
 * the action to the proper SLI-3 or SLI-4 device probing routine, which will
 * do all the initialization that it needs to do to handle the HBA device
 * properly.
 *
 * Return code
 * 	0 - driver can claim the device
 * 	negative value - driver can not claim the device
 **/
static int
lpfc_pci_probe_one(struct pci_dev *pdev, const struct pci_device_id *pid)
{
	int rc;
	struct lpfc_sli_intf intf;

	if (pci_read_config_dword(pdev, LPFC_SLI_INTF, &intf.word0))
		return -ENODEV;

	if ((bf_get(lpfc_sli_intf_valid, &intf) == LPFC_SLI_INTF_VALID) &&
	    (bf_get(lpfc_sli_intf_slirev, &intf) == LPFC_SLI_INTF_REV_SLI4))
		rc = lpfc_pci_probe_one_s4(pdev, pid);
	else
		rc = lpfc_pci_probe_one_s3(pdev, pid);

	return rc;
}

/**
 * lpfc_pci_remove_one - lpfc PCI func to unreg dev from PCI subsystem
 * @pdev: pointer to PCI device
 *
 * This routine is to be registered to the kernel's PCI subsystem. When an
 * Emulex HBA is removed from PCI bus, the driver core invokes this routine.
 * This routine dispatches the action to the proper SLI-3 or SLI-4 device
 * remove routine, which will perform all the necessary cleanup for the
 * device to be removed from the PCI subsystem properly.
 **/
static void
lpfc_pci_remove_one(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct lpfc_hba *phba = ((struct lpfc_vport *)shost->hostdata)->phba;

	switch (phba->pci_dev_grp) {
	case LPFC_PCI_DEV_LP:
		lpfc_pci_remove_one_s3(pdev);
		break;
	case LPFC_PCI_DEV_OC:
		lpfc_pci_remove_one_s4(pdev);
		break;
	default:
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"1424 Invalid PCI device group: 0x%x\n",
				phba->pci_dev_grp);
		break;
	}
	return;
}

/**
 * lpfc_pci_suspend_one - lpfc PCI func to suspend dev for power management
 * @pdev: pointer to PCI device
 * @msg: power management message
 *
 * This routine is to be registered to the kernel's PCI subsystem to support
 * system Power Management (PM). When PM invokes this method, it dispatches
 * the action to the proper SLI-3 or SLI-4 device suspend routine, which will
 * suspend the device.
 *
 * Return code
 * 	0 - driver suspended the device
 * 	Error otherwise
 **/
static int
lpfc_pci_suspend_one(struct pci_dev *pdev, pm_message_t msg)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct lpfc_hba *phba = ((struct lpfc_vport *)shost->hostdata)->phba;
	int rc = -ENODEV;

	switch (phba->pci_dev_grp) {
	case LPFC_PCI_DEV_LP:
		rc = lpfc_pci_suspend_one_s3(pdev, msg);
		break;
	case LPFC_PCI_DEV_OC:
		rc = lpfc_pci_suspend_one_s4(pdev, msg);
		break;
	default:
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"1425 Invalid PCI device group: 0x%x\n",
				phba->pci_dev_grp);
		break;
	}
	return rc;
}

/**
 * lpfc_pci_resume_one - lpfc PCI func to resume dev for power management
 * @pdev: pointer to PCI device
 *
 * This routine is to be registered to the kernel's PCI subsystem to support
 * system Power Management (PM). When PM invokes this method, it dispatches
 * the action to the proper SLI-3 or SLI-4 device resume routine, which will
 * resume the device.
 *
 * Return code
 * 	0 - driver suspended the device
 * 	Error otherwise
 **/
static int
lpfc_pci_resume_one(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct lpfc_hba *phba = ((struct lpfc_vport *)shost->hostdata)->phba;
	int rc = -ENODEV;

	switch (phba->pci_dev_grp) {
	case LPFC_PCI_DEV_LP:
		rc = lpfc_pci_resume_one_s3(pdev);
		break;
	case LPFC_PCI_DEV_OC:
		rc = lpfc_pci_resume_one_s4(pdev);
		break;
	default:
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"1426 Invalid PCI device group: 0x%x\n",
				phba->pci_dev_grp);
		break;
	}
	return rc;
}

/**
 * lpfc_io_error_detected - lpfc method for handling PCI I/O error
 * @pdev: pointer to PCI device.
 * @state: the current PCI connection state.
 *
 * This routine is registered to the PCI subsystem for error handling. This
 * function is called by the PCI subsystem after a PCI bus error affecting
 * this device has been detected. When this routine is invoked, it dispatches
 * the action to the proper SLI-3 or SLI-4 device error detected handling
 * routine, which will perform the proper error detected operation.
 *
 * Return codes
 * 	PCI_ERS_RESULT_NEED_RESET - need to reset before recovery
 * 	PCI_ERS_RESULT_DISCONNECT - device could not be recovered
 **/
static pci_ers_result_t
lpfc_io_error_detected(struct pci_dev *pdev, pci_channel_state_t state)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct lpfc_hba *phba = ((struct lpfc_vport *)shost->hostdata)->phba;
	pci_ers_result_t rc = PCI_ERS_RESULT_DISCONNECT;

	switch (phba->pci_dev_grp) {
	case LPFC_PCI_DEV_LP:
		rc = lpfc_io_error_detected_s3(pdev, state);
		break;
	case LPFC_PCI_DEV_OC:
		rc = lpfc_io_error_detected_s4(pdev, state);
		break;
	default:
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"1427 Invalid PCI device group: 0x%x\n",
				phba->pci_dev_grp);
		break;
	}
	return rc;
}

/**
 * lpfc_io_slot_reset - lpfc method for restart PCI dev from scratch
 * @pdev: pointer to PCI device.
 *
 * This routine is registered to the PCI subsystem for error handling. This
 * function is called after PCI bus has been reset to restart the PCI card
 * from scratch, as if from a cold-boot. When this routine is invoked, it
 * dispatches the action to the proper SLI-3 or SLI-4 device reset handling
 * routine, which will perform the proper device reset.
 *
 * Return codes
 * 	PCI_ERS_RESULT_RECOVERED - the device has been recovered
 * 	PCI_ERS_RESULT_DISCONNECT - device could not be recovered
 **/
static pci_ers_result_t
lpfc_io_slot_reset(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct lpfc_hba *phba = ((struct lpfc_vport *)shost->hostdata)->phba;
	pci_ers_result_t rc = PCI_ERS_RESULT_DISCONNECT;

	switch (phba->pci_dev_grp) {
	case LPFC_PCI_DEV_LP:
		rc = lpfc_io_slot_reset_s3(pdev);
		break;
	case LPFC_PCI_DEV_OC:
		rc = lpfc_io_slot_reset_s4(pdev);
		break;
	default:
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"1428 Invalid PCI device group: 0x%x\n",
				phba->pci_dev_grp);
		break;
	}
	return rc;
}

/**
 * lpfc_io_resume - lpfc method for resuming PCI I/O operation
 * @pdev: pointer to PCI device
 *
 * This routine is registered to the PCI subsystem for error handling. It
 * is called when kernel error recovery tells the lpfc driver that it is
 * OK to resume normal PCI operation after PCI bus error recovery. When
 * this routine is invoked, it dispatches the action to the proper SLI-3
 * or SLI-4 device io_resume routine, which will resume the device operation.
 **/
static void
lpfc_io_resume(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct lpfc_hba *phba = ((struct lpfc_vport *)shost->hostdata)->phba;

	switch (phba->pci_dev_grp) {
	case LPFC_PCI_DEV_LP:
		lpfc_io_resume_s3(pdev);
		break;
	case LPFC_PCI_DEV_OC:
		lpfc_io_resume_s4(pdev);
		break;
	default:
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"1429 Invalid PCI device group: 0x%x\n",
				phba->pci_dev_grp);
		break;
	}
	return;
}

/**
 * lpfc_sli4_oas_verify - Verify OAS is supported by this adapter
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine checks to see if OAS is supported for this adapter. If
 * supported, the configure Flash Optimized Fabric flag is set.  Otherwise,
 * the enable oas flag is cleared and the pool created for OAS device data
 * is destroyed.
 *
 **/
static void
lpfc_sli4_oas_verify(struct lpfc_hba *phba)
{

	if (!phba->cfg_EnableXLane)
		return;

	if (phba->sli4_hba.pc_sli4_params.oas_supported) {
		phba->cfg_fof = 1;
	} else {
		phba->cfg_fof = 0;
		mempool_destroy(phba->device_data_mem_pool);
		phba->device_data_mem_pool = NULL;
	}

	return;
}

/**
 * lpfc_sli4_ras_init - Verify RAS-FW log is supported by this adapter
 * @phba: pointer to lpfc hba data structure.
 *
 * This routine checks to see if RAS is supported by the adapter. Check the
 * function through which RAS support enablement is to be done.
 **/
void
lpfc_sli4_ras_init(struct lpfc_hba *phba)
{
	switch (phba->pcidev->device) {
	case PCI_DEVICE_ID_LANCER_G6_FC:
	case PCI_DEVICE_ID_LANCER_G7_FC:
		phba->ras_fwlog.ras_hwsupport = true;
		if (phba->cfg_ras_fwlog_func == PCI_FUNC(phba->pcidev->devfn) &&
		    phba->cfg_ras_fwlog_buffsize)
			phba->ras_fwlog.ras_enabled = true;
		else
			phba->ras_fwlog.ras_enabled = false;
		break;
	default:
		phba->ras_fwlog.ras_hwsupport = false;
	}
}


MODULE_DEVICE_TABLE(pci, lpfc_id_table);

static const struct pci_error_handlers lpfc_err_handler = {
	.error_detected = lpfc_io_error_detected,
	.slot_reset = lpfc_io_slot_reset,
	.resume = lpfc_io_resume,
};

static struct pci_driver lpfc_driver = {
	.name		= LPFC_DRIVER_NAME,
	.id_table	= lpfc_id_table,
	.probe		= lpfc_pci_probe_one,
	.remove		= lpfc_pci_remove_one,
	.shutdown	= lpfc_pci_remove_one,
	.suspend        = lpfc_pci_suspend_one,
	.resume		= lpfc_pci_resume_one,
	.err_handler    = &lpfc_err_handler,
};

static const struct file_operations lpfc_mgmt_fop = {
	.owner = THIS_MODULE,
};

static struct miscdevice lpfc_mgmt_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "lpfcmgmt",
	.fops = &lpfc_mgmt_fop,
};

/**
 * lpfc_init - lpfc module initialization routine
 *
 * This routine is to be invoked when the lpfc module is loaded into the
 * kernel. The special kernel macro module_init() is used to indicate the
 * role of this routine to the kernel as lpfc module entry point.
 *
 * Return codes
 *   0 - successful
 *   -ENOMEM - FC attach transport failed
 *   all others - failed
 */
static int __init
lpfc_init(void)
{
	int error = 0;

	printk(LPFC_MODULE_DESC "\n");
	printk(LPFC_COPYRIGHT "\n");

	error = misc_register(&lpfc_mgmt_dev);
	if (error)
		printk(KERN_ERR "Could not register lpfcmgmt device, "
			"misc_register returned with status %d", error);

	lpfc_transport_functions.vport_create = lpfc_vport_create;
	lpfc_transport_functions.vport_delete = lpfc_vport_delete;
	lpfc_transport_template =
				fc_attach_transport(&lpfc_transport_functions);
	if (lpfc_transport_template == NULL)
		return -ENOMEM;
	lpfc_vport_transport_template =
		fc_attach_transport(&lpfc_vport_transport_functions);
	if (lpfc_vport_transport_template == NULL) {
		fc_release_transport(lpfc_transport_template);
		return -ENOMEM;
	}
	lpfc_nvme_cmd_template();
	lpfc_nvmet_cmd_template();

	/* Initialize in case vector mapping is needed */
	lpfc_present_cpu = num_present_cpus();

	error = cpuhp_setup_state_multi(CPUHP_AP_ONLINE_DYN,
					"lpfc/sli4:online",
					lpfc_cpu_online, lpfc_cpu_offline);
	if (error < 0)
		goto cpuhp_failure;
	lpfc_cpuhp_state = error;

	error = pci_register_driver(&lpfc_driver);
	if (error)
		goto unwind;

	return error;

unwind:
	cpuhp_remove_multi_state(lpfc_cpuhp_state);
cpuhp_failure:
	fc_release_transport(lpfc_transport_template);
	fc_release_transport(lpfc_vport_transport_template);

	return error;
}

/**
 * lpfc_exit - lpfc module removal routine
 *
 * This routine is invoked when the lpfc module is removed from the kernel.
 * The special kernel macro module_exit() is used to indicate the role of
 * this routine to the kernel as lpfc module exit point.
 */
static void __exit
lpfc_exit(void)
{
	misc_deregister(&lpfc_mgmt_dev);
	pci_unregister_driver(&lpfc_driver);
	cpuhp_remove_multi_state(lpfc_cpuhp_state);
	fc_release_transport(lpfc_transport_template);
	fc_release_transport(lpfc_vport_transport_template);
	idr_destroy(&lpfc_hba_index);
}

module_init(lpfc_init);
module_exit(lpfc_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(LPFC_MODULE_DESC);
MODULE_AUTHOR("Broadcom");
MODULE_VERSION("0:" LPFC_DRIVER_VERSION);
