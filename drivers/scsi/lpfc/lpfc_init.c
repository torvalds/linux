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
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/pci.h>
#include <linux/spinlock.h>

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

static int lpfc_parse_vpd(struct lpfc_hba *, uint8_t *);
static void lpfc_get_hba_model_desc(struct lpfc_hba *, uint8_t *, uint8_t *);
static int lpfc_post_rcv_buf(struct lpfc_hba *);

static struct scsi_transport_template *lpfc_transport_template = NULL;
static DEFINE_IDR(lpfc_hba_index);

/************************************************************************/
/*                                                                      */
/*    lpfc_config_port_prep                                             */
/*    This routine will do LPFC initialization prior to the             */
/*    CONFIG_PORT mailbox command. This will be initialized             */
/*    as a SLI layer callback routine.                                  */
/*    This routine returns 0 on success or -ERESTART if it wants        */
/*    the SLI layer to reset the HBA and try again. Any                 */
/*    other return value indicates an error.                            */
/*                                                                      */
/************************************************************************/
int
lpfc_config_port_prep(struct lpfc_hba * phba)
{
	lpfc_vpd_t *vp = &phba->vpd;
	int i = 0, rc;
	LPFC_MBOXQ_t *pmb;
	MAILBOX_t *mb;
	char *lpfc_vpd_data = NULL;
	uint16_t offset = 0;
	static char licensed[56] =
		    "key unlock for use with gnu public licensed code only\0";

	pmb = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!pmb) {
		phba->hba_state = LPFC_HBA_ERROR;
		return -ENOMEM;
	}

	mb = &pmb->mb;
	phba->hba_state = LPFC_INIT_MBX_CMDS;

	if (lpfc_is_LC_HBA(phba->pcidev->device)) {
		uint32_t *ptext = (uint32_t *) licensed;

		for (i = 0; i < 56; i += sizeof (uint32_t), ptext++)
			*ptext = cpu_to_be32(*ptext);

		lpfc_read_nv(phba, pmb);
		memset((char*)mb->un.varRDnvp.rsvd3, 0,
			sizeof (mb->un.varRDnvp.rsvd3));
		memcpy((char*)mb->un.varRDnvp.rsvd3, licensed,
			 sizeof (licensed));

		rc = lpfc_sli_issue_mbox(phba, pmb, MBX_POLL);

		if (rc != MBX_SUCCESS) {
			lpfc_printf_log(phba,
					KERN_ERR,
					LOG_MBOX,
					"%d:0324 Config Port initialization "
					"error, mbxCmd x%x READ_NVPARM, "
					"mbxStatus x%x\n",
					phba->brd_no,
					mb->mbxCommand, mb->mbxStatus);
			mempool_free(pmb, phba->mbox_mem_pool);
			return -ERESTART;
		}
		memcpy(phba->wwnn, (char *)mb->un.varRDnvp.nodename,
		       sizeof (mb->un.varRDnvp.nodename));
	}

	/* Setup and issue mailbox READ REV command */
	lpfc_read_rev(phba, pmb);
	rc = lpfc_sli_issue_mbox(phba, pmb, MBX_POLL);
	if (rc != MBX_SUCCESS) {
		lpfc_printf_log(phba,
				KERN_ERR,
				LOG_INIT,
				"%d:0439 Adapter failed to init, mbxCmd x%x "
				"READ_REV, mbxStatus x%x\n",
				phba->brd_no,
				mb->mbxCommand, mb->mbxStatus);
		mempool_free( pmb, phba->mbox_mem_pool);
		return -ERESTART;
	}

	/* The HBA's current state is provided by the ProgType and rr fields.
	 * Read and check the value of these fields before continuing to config
	 * this port.
	 */
	if (mb->un.varRdRev.rr == 0 || mb->un.varRdRev.un.b.ProgType != 2) {
		/* Old firmware */
		vp->rev.rBit = 0;
		lpfc_printf_log(phba,
				KERN_ERR,
				LOG_INIT,
				"%d:0440 Adapter failed to init, mbxCmd x%x "
				"READ_REV detected outdated firmware"
				"Data: x%x\n",
				phba->brd_no,
				mb->mbxCommand, 0);
		mempool_free(pmb, phba->mbox_mem_pool);
		return -ERESTART;
	} else {
		vp->rev.rBit = 1;
		vp->rev.sli1FwRev = mb->un.varRdRev.sli1FwRev;
		memcpy(vp->rev.sli1FwName,
			(char*)mb->un.varRdRev.sli1FwName, 16);
		vp->rev.sli2FwRev = mb->un.varRdRev.sli2FwRev;
		memcpy(vp->rev.sli2FwName,
					(char *)mb->un.varRdRev.sli2FwName, 16);
	}

	/* Save information as VPD data */
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

	if (lpfc_is_LC_HBA(phba->pcidev->device))
		memcpy(phba->RandomData, (char *)&mb->un.varWords[24],
						sizeof (phba->RandomData));

	/* Get the default values for Model Name and Description */
	lpfc_get_hba_model_desc(phba, phba->ModelName, phba->ModelDesc);

	/* Get adapter VPD information */
	pmb->context2 = kmalloc(DMP_RSP_SIZE, GFP_KERNEL);
	if (!pmb->context2)
		goto out_free_mbox;
	lpfc_vpd_data = kmalloc(DMP_VPD_SIZE, GFP_KERNEL);
	if (!lpfc_vpd_data)
		goto out_free_context2;

	do {
		lpfc_dump_mem(phba, pmb, offset);
		rc = lpfc_sli_issue_mbox(phba, pmb, MBX_POLL);

		if (rc != MBX_SUCCESS) {
			lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
					"%d:0441 VPD not present on adapter, "
					"mbxCmd x%x DUMP VPD, mbxStatus x%x\n",
					phba->brd_no,
					mb->mbxCommand, mb->mbxStatus);
			kfree(lpfc_vpd_data);
			lpfc_vpd_data = NULL;
			break;
		}

		lpfc_sli_pcimem_bcopy(pmb->context2, lpfc_vpd_data + offset,
							mb->un.varDmp.word_cnt);
		offset += mb->un.varDmp.word_cnt;
	} while (mb->un.varDmp.word_cnt);
	lpfc_parse_vpd(phba, lpfc_vpd_data);

	kfree(lpfc_vpd_data);
out_free_context2:
	kfree(pmb->context2);
out_free_mbox:
	mempool_free(pmb, phba->mbox_mem_pool);
	return 0;
}

/************************************************************************/
/*                                                                      */
/*    lpfc_config_port_post                                             */
/*    This routine will do LPFC initialization after the                */
/*    CONFIG_PORT mailbox command. This will be initialized             */
/*    as a SLI layer callback routine.                                  */
/*    This routine returns 0 on success. Any other return value         */
/*    indicates an error.                                               */
/*                                                                      */
/************************************************************************/
int
lpfc_config_port_post(struct lpfc_hba * phba)
{
	LPFC_MBOXQ_t *pmb;
	MAILBOX_t *mb;
	struct lpfc_dmabuf *mp;
	struct lpfc_sli *psli = &phba->sli;
	uint32_t status, timeout;
	int i, j, rc;

	pmb = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!pmb) {
		phba->hba_state = LPFC_HBA_ERROR;
		return -ENOMEM;
	}
	mb = &pmb->mb;

	lpfc_config_link(phba, pmb);
	rc = lpfc_sli_issue_mbox(phba, pmb, MBX_POLL);
	if (rc != MBX_SUCCESS) {
		lpfc_printf_log(phba,
				KERN_ERR,
				LOG_INIT,
				"%d:0447 Adapter failed init, mbxCmd x%x "
				"CONFIG_LINK mbxStatus x%x\n",
				phba->brd_no,
				mb->mbxCommand, mb->mbxStatus);
		phba->hba_state = LPFC_HBA_ERROR;
		mempool_free( pmb, phba->mbox_mem_pool);
		return -EIO;
	}

	/* Get login parameters for NID.  */
	lpfc_read_sparam(phba, pmb);
	if (lpfc_sli_issue_mbox(phba, pmb, MBX_POLL) != MBX_SUCCESS) {
		lpfc_printf_log(phba,
				KERN_ERR,
				LOG_INIT,
				"%d:0448 Adapter failed init, mbxCmd x%x "
				"READ_SPARM mbxStatus x%x\n",
				phba->brd_no,
				mb->mbxCommand, mb->mbxStatus);
		phba->hba_state = LPFC_HBA_ERROR;
		mp = (struct lpfc_dmabuf *) pmb->context1;
		mempool_free( pmb, phba->mbox_mem_pool);
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		kfree(mp);
		return -EIO;
	}

	mp = (struct lpfc_dmabuf *) pmb->context1;

	memcpy(&phba->fc_sparam, mp->virt, sizeof (struct serv_parm));
	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
	pmb->context1 = NULL;

	memcpy(&phba->fc_nodename, &phba->fc_sparam.nodeName,
	       sizeof (struct lpfc_name));
	memcpy(&phba->fc_portname, &phba->fc_sparam.portName,
	       sizeof (struct lpfc_name));
	/* If no serial number in VPD data, use low 6 bytes of WWNN */
	/* This should be consolidated into parse_vpd ? - mr */
	if (phba->SerialNumber[0] == 0) {
		uint8_t *outptr;

		outptr = &phba->fc_nodename.u.s.IEEE[0];
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

	/* This should turn on DELAYED ABTS for ELS timeouts */
	lpfc_set_slim(phba, pmb, 0x052198, 0x1);
	if (lpfc_sli_issue_mbox(phba, pmb, MBX_POLL) != MBX_SUCCESS) {
		phba->hba_state = LPFC_HBA_ERROR;
		mempool_free( pmb, phba->mbox_mem_pool);
		return -EIO;
	}


	lpfc_read_config(phba, pmb);
	if (lpfc_sli_issue_mbox(phba, pmb, MBX_POLL) != MBX_SUCCESS) {
		lpfc_printf_log(phba,
				KERN_ERR,
				LOG_INIT,
				"%d:0453 Adapter failed to init, mbxCmd x%x "
				"READ_CONFIG, mbxStatus x%x\n",
				phba->brd_no,
				mb->mbxCommand, mb->mbxStatus);
		phba->hba_state = LPFC_HBA_ERROR;
		mempool_free( pmb, phba->mbox_mem_pool);
		return -EIO;
	}

	/* Reset the DFT_HBA_Q_DEPTH to the max xri  */
	if (phba->cfg_hba_queue_depth > (mb->un.varRdConfig.max_xri+1))
		phba->cfg_hba_queue_depth =
			mb->un.varRdConfig.max_xri + 1;

	phba->lmt = mb->un.varRdConfig.lmt;
	/* HBA is not 4GB capable, or HBA is not 2GB capable,
	don't let link speed ask for it */
	if ((((phba->lmt & LMT_4250_10bit) != LMT_4250_10bit) &&
		(phba->cfg_link_speed > LINK_SPEED_2G)) ||
		(((phba->lmt & LMT_2125_10bit) != LMT_2125_10bit) &&
		(phba->cfg_link_speed > LINK_SPEED_1G))) {
		/* Reset link speed to auto. 1G/2GB HBA cfg'd for 4G */
		lpfc_printf_log(phba,
			KERN_WARNING,
			LOG_LINK_EVENT,
			"%d:1302 Invalid speed for this board: "
			"Reset link speed to auto: x%x\n",
			phba->brd_no,
			phba->cfg_link_speed);
			phba->cfg_link_speed = LINK_SPEED_AUTO;
	}

	phba->hba_state = LPFC_LINK_DOWN;

	/* Only process IOCBs on ring 0 till hba_state is READY */
	if (psli->ring[psli->ip_ring].cmdringaddr)
		psli->ring[psli->ip_ring].flag |= LPFC_STOP_IOCB_EVENT;
	if (psli->ring[psli->fcp_ring].cmdringaddr)
		psli->ring[psli->fcp_ring].flag |= LPFC_STOP_IOCB_EVENT;
	if (psli->ring[psli->next_ring].cmdringaddr)
		psli->ring[psli->next_ring].flag |= LPFC_STOP_IOCB_EVENT;

	/* Post receive buffers for desired rings */
	lpfc_post_rcv_buf(phba);

	/* Enable appropriate host interrupts */
	spin_lock_irq(phba->host->host_lock);
	status = readl(phba->HCregaddr);
	status |= HC_MBINT_ENA | HC_ERINT_ENA | HC_LAINT_ENA;
	if (psli->num_rings > 0)
		status |= HC_R0INT_ENA;
	if (psli->num_rings > 1)
		status |= HC_R1INT_ENA;
	if (psli->num_rings > 2)
		status |= HC_R2INT_ENA;
	if (psli->num_rings > 3)
		status |= HC_R3INT_ENA;

	writel(status, phba->HCregaddr);
	readl(phba->HCregaddr); /* flush */
	spin_unlock_irq(phba->host->host_lock);

	/*
	 * Setup the ring 0 (els)  timeout handler
	 */
	timeout = phba->fc_ratov << 1;
	phba->els_tmofunc.expires = jiffies + HZ * timeout;
	add_timer(&phba->els_tmofunc);

	lpfc_init_link(phba, pmb, phba->cfg_topology, phba->cfg_link_speed);
	pmb->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
	if (lpfc_sli_issue_mbox(phba, pmb, MBX_NOWAIT) != MBX_SUCCESS) {
		lpfc_printf_log(phba,
				KERN_ERR,
				LOG_INIT,
				"%d:0454 Adapter failed to init, mbxCmd x%x "
				"INIT_LINK, mbxStatus x%x\n",
				phba->brd_no,
				mb->mbxCommand, mb->mbxStatus);

		/* Clear all interrupt enable conditions */
		writel(0, phba->HCregaddr);
		readl(phba->HCregaddr); /* flush */
		/* Clear all pending interrupts */
		writel(0xffffffff, phba->HAregaddr);
		readl(phba->HAregaddr); /* flush */

		phba->hba_state = LPFC_HBA_ERROR;
		mempool_free(pmb, phba->mbox_mem_pool);
		return -EIO;
	}
	/* MBOX buffer will be freed in mbox compl */

	i = 0;
	while ((phba->hba_state != LPFC_HBA_READY) ||
	       (phba->num_disc_nodes) || (phba->fc_prli_sent) ||
	       ((phba->fc_map_cnt == 0) && (i<2)) ||
	       (psli->sli_flag & LPFC_SLI_MBOX_ACTIVE)) {
		/* Check every second for 30 retries. */
		i++;
		if (i > 30) {
			break;
		}
		if ((i >= 15) && (phba->hba_state <= LPFC_LINK_DOWN)) {
			/* The link is down.  Set linkdown timeout */
			break;
		}

		/* Delay for 1 second to give discovery time to complete. */
		msleep(1000);

	}

	/* Since num_disc_nodes keys off of PLOGI, delay a bit to let
	 * any potential PRLIs to flush thru the SLI sub-system.
	 */
	msleep(50);

	return (0);
}

/************************************************************************/
/*                                                                      */
/*    lpfc_hba_down_prep                                                */
/*    This routine will do LPFC uninitialization before the             */
/*    HBA is reset when bringing down the SLI Layer. This will be       */
/*    initialized as a SLI layer callback routine.                      */
/*    This routine returns 0 on success. Any other return value         */
/*    indicates an error.                                               */
/*                                                                      */
/************************************************************************/
int
lpfc_hba_down_prep(struct lpfc_hba * phba)
{
	/* Disable interrupts */
	writel(0, phba->HCregaddr);
	readl(phba->HCregaddr); /* flush */

	/* Cleanup potential discovery resources */
	lpfc_els_flush_rscn(phba);
	lpfc_els_flush_cmd(phba);
	lpfc_disc_flush_list(phba);

	return (0);
}

/************************************************************************/
/*                                                                      */
/*    lpfc_handle_eratt                                                 */
/*    This routine will handle processing a Host Attention              */
/*    Error Status event. This will be initialized                      */
/*    as a SLI layer callback routine.                                  */
/*                                                                      */
/************************************************************************/
void
lpfc_handle_eratt(struct lpfc_hba * phba)
{
	struct lpfc_sli *psli = &phba->sli;
	struct lpfc_sli_ring  *pring;

	/*
	 * If a reset is sent to the HBA restore PCI configuration registers.
	 */
	if ( phba->hba_state == LPFC_INIT_START ) {
		mdelay(1);
		readl(phba->HCregaddr); /* flush */
		writel(0, phba->HCregaddr);
		readl(phba->HCregaddr); /* flush */

		/* Restore PCI cmd register */
		pci_write_config_word(phba->pcidev,
				      PCI_COMMAND, phba->pci_cfg_value);
	}

	if (phba->work_hs & HS_FFER6) {
		/* Re-establishing Link */
		lpfc_printf_log(phba, KERN_INFO, LOG_LINK_EVENT,
				"%d:1301 Re-establishing Link "
				"Data: x%x x%x x%x\n",
				phba->brd_no, phba->work_hs,
				phba->work_status[0], phba->work_status[1]);
		spin_lock_irq(phba->host->host_lock);
		phba->fc_flag |= FC_ESTABLISH_LINK;
		spin_unlock_irq(phba->host->host_lock);

		/*
		* Firmware stops when it triggled erratt with HS_FFER6.
		* That could cause the I/Os dropped by the firmware.
		* Error iocb (I/O) on txcmplq and let the SCSI layer
		* retry it after re-establishing link.
		*/
		pring = &psli->ring[psli->fcp_ring];
		lpfc_sli_abort_iocb_ring(phba, pring);


		/*
		 * There was a firmware error.  Take the hba offline and then
		 * attempt to restart it.
		 */
		lpfc_offline(phba);
		if (lpfc_online(phba) == 0) {	/* Initialize the HBA */
			mod_timer(&phba->fc_estabtmo, jiffies + HZ * 60);
			return;
		}
	} else {
		/* The if clause above forces this code path when the status
		 * failure is a value other than FFER6.  Do not call the offline
		 *  twice. This is the adapter hardware error path.
		 */
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"%d:0457 Adapter Hardware Error "
				"Data: x%x x%x x%x\n",
				phba->brd_no, phba->work_hs,
				phba->work_status[0], phba->work_status[1]);

		lpfc_offline(phba);

		/*
		 * Restart all traffic to this host.  Since the fc_transport
		 * block functions (future) were not called in lpfc_offline,
		 * don't call them here.
		 */
		scsi_unblock_requests(phba->host);
	}
}

/************************************************************************/
/*                                                                      */
/*    lpfc_handle_latt                                                  */
/*    This routine will handle processing a Host Attention              */
/*    Link Status event. This will be initialized                       */
/*    as a SLI layer callback routine.                                  */
/*                                                                      */
/************************************************************************/
void
lpfc_handle_latt(struct lpfc_hba * phba)
{
	struct lpfc_sli *psli = &phba->sli;
	LPFC_MBOXQ_t *pmb;
	volatile uint32_t control;
	struct lpfc_dmabuf *mp;
	int rc = -ENOMEM;

	pmb = (LPFC_MBOXQ_t *)mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!pmb)
		goto lpfc_handle_latt_err_exit;

	mp = kmalloc(sizeof(struct lpfc_dmabuf), GFP_KERNEL);
	if (!mp)
		goto lpfc_handle_latt_free_pmb;

	mp->virt = lpfc_mbuf_alloc(phba, 0, &mp->phys);
	if (!mp->virt)
		goto lpfc_handle_latt_free_mp;

	rc = -EIO;


	psli->slistat.link_event++;
	lpfc_read_la(phba, pmb, mp);
	pmb->mbox_cmpl = lpfc_mbx_cmpl_read_la;
	rc = lpfc_sli_issue_mbox (phba, pmb, (MBX_NOWAIT | MBX_STOP_IOCB));
	if (rc == MBX_NOT_FINISHED)
		goto lpfc_handle_latt_free_mp;

	/* Clear Link Attention in HA REG */
	spin_lock_irq(phba->host->host_lock);
	writel(HA_LATT, phba->HAregaddr);
	readl(phba->HAregaddr); /* flush */
	spin_unlock_irq(phba->host->host_lock);

	return;

lpfc_handle_latt_free_mp:
	kfree(mp);
lpfc_handle_latt_free_pmb:
	kfree(pmb);
lpfc_handle_latt_err_exit:
	/* Enable Link attention interrupts */
	spin_lock_irq(phba->host->host_lock);
	psli->sli_flag |= LPFC_PROCESS_LA;
	control = readl(phba->HCregaddr);
	control |= HC_LAINT_ENA;
	writel(control, phba->HCregaddr);
	readl(phba->HCregaddr); /* flush */

	/* Clear Link Attention in HA REG */
	writel(HA_LATT, phba->HAregaddr);
	readl(phba->HAregaddr); /* flush */
	spin_unlock_irq(phba->host->host_lock);
	lpfc_linkdown(phba);
	phba->hba_state = LPFC_HBA_ERROR;

	/* The other case is an error from issue_mbox */
	if (rc == -ENOMEM)
		lpfc_printf_log(phba,
				KERN_WARNING,
				LOG_MBOX,
			        "%d:0300 READ_LA: no buffers\n",
				phba->brd_no);

	return;
}

/************************************************************************/
/*                                                                      */
/*   lpfc_parse_vpd                                                     */
/*   This routine will parse the VPD data                               */
/*                                                                      */
/************************************************************************/
static int
lpfc_parse_vpd(struct lpfc_hba * phba, uint8_t * vpd)
{
	uint8_t lenlo, lenhi;
	uint32_t Length;
	int i, j;
	int finished = 0;
	int index = 0;

	if (!vpd)
		return 0;

	/* Vital Product */
	lpfc_printf_log(phba,
			KERN_INFO,
			LOG_INIT,
			"%d:0455 Vital Product Data: x%x x%x x%x x%x\n",
			phba->brd_no,
			(uint32_t) vpd[0], (uint32_t) vpd[1], (uint32_t) vpd[2],
			(uint32_t) vpd[3]);
	do {
		switch (vpd[index]) {
		case 0x82:
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
				phba->Port[j++] = vpd[index++];
				if (j == 19)
					break;
				}
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
	} while (!finished && (index < 108));

	return(1);
}

static void
lpfc_get_hba_model_desc(struct lpfc_hba * phba, uint8_t * mdp, uint8_t * descp)
{
	lpfc_vpd_t *vp;
	uint32_t id;
	char str[16];

	vp = &phba->vpd;
	pci_read_config_dword(phba->pcidev, PCI_VENDOR_ID, &id);

	switch ((id >> 16) & 0xffff) {
	case PCI_DEVICE_ID_FIREFLY:
		strcpy(str, "LP6000 1");
		break;
	case PCI_DEVICE_ID_SUPERFLY:
		if (vp->rev.biuRev >= 1 && vp->rev.biuRev <= 3)
			strcpy(str, "LP7000 1");
		else
			strcpy(str, "LP7000E 1");
		break;
	case PCI_DEVICE_ID_DRAGONFLY:
		strcpy(str, "LP8000 1");
		break;
	case PCI_DEVICE_ID_CENTAUR:
		if (FC_JEDEC_ID(vp->rev.biuRev) == CENTAUR_2G_JEDEC_ID)
			strcpy(str, "LP9002 2");
		else
			strcpy(str, "LP9000 1");
		break;
	case PCI_DEVICE_ID_RFLY:
		strcpy(str, "LP952 2");
		break;
	case PCI_DEVICE_ID_PEGASUS:
		strcpy(str, "LP9802 2");
		break;
	case PCI_DEVICE_ID_THOR:
		strcpy(str, "LP10000 2");
		break;
	case PCI_DEVICE_ID_VIPER:
		strcpy(str, "LPX1000 10");
		break;
	case PCI_DEVICE_ID_PFLY:
		strcpy(str, "LP982 2");
		break;
	case PCI_DEVICE_ID_TFLY:
		strcpy(str, "LP1050 2");
		break;
	case PCI_DEVICE_ID_HELIOS:
		strcpy(str, "LP11000 4");
		break;
	case PCI_DEVICE_ID_BMID:
		strcpy(str, "LP1150 4");
		break;
	case PCI_DEVICE_ID_BSMB:
		strcpy(str, "LP111 4");
		break;
	case PCI_DEVICE_ID_ZEPHYR:
		strcpy(str, "LP11000e 4");
		break;
	case PCI_DEVICE_ID_ZMID:
		strcpy(str, "LP1150e 4");
		break;
	case PCI_DEVICE_ID_ZSMB:
		strcpy(str, "LP111e 4");
		break;
	case PCI_DEVICE_ID_LP101:
		strcpy(str, "LP101 2");
		break;
	case PCI_DEVICE_ID_LP10000S:
		strcpy(str, "LP10000-S 2");
		break;
	default:
		memset(str, 0, 16);
		break;
	}
	if (mdp)
		sscanf(str, "%s", mdp);
	if (descp)
		sprintf(descp, "Emulex LightPulse %s Gigabit PCI Fibre "
			"Channel Adapter", str);
}

/**************************************************/
/*   lpfc_post_buffer                             */
/*                                                */
/*   This routine will post count buffers to the  */
/*   ring with the QUE_RING_BUF_CN command. This  */
/*   allows 3 buffers / command to be posted.     */
/*   Returns the number of buffers NOT posted.    */
/**************************************************/
int
lpfc_post_buffer(struct lpfc_hba * phba, struct lpfc_sli_ring * pring, int cnt,
		 int type)
{
	IOCB_t *icmd;
	struct list_head *lpfc_iocb_list = &phba->lpfc_iocb_list;
	struct lpfc_iocbq *iocb = NULL;
	struct lpfc_dmabuf *mp1, *mp2;

	cnt += pring->missbufcnt;

	/* While there are buffers to post */
	while (cnt > 0) {
		/* Allocate buffer for  command iocb */
		spin_lock_irq(phba->host->host_lock);
		list_remove_head(lpfc_iocb_list, iocb, struct lpfc_iocbq, list);
		spin_unlock_irq(phba->host->host_lock);
		if (iocb == NULL) {
			pring->missbufcnt = cnt;
			return cnt;
		}
		memset(iocb, 0, sizeof (struct lpfc_iocbq));
		icmd = &iocb->iocb;

		/* 2 buffers can be posted per command */
		/* Allocate buffer to post */
		mp1 = kmalloc(sizeof (struct lpfc_dmabuf), GFP_KERNEL);
		if (mp1)
		    mp1->virt = lpfc_mbuf_alloc(phba, MEM_PRI,
						&mp1->phys);
		if (mp1 == 0 || mp1->virt == 0) {
			if (mp1)
				kfree(mp1);
			spin_lock_irq(phba->host->host_lock);
			list_add_tail(&iocb->list, lpfc_iocb_list);
			spin_unlock_irq(phba->host->host_lock);
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
			if (mp2 == 0 || mp2->virt == 0) {
				if (mp2)
					kfree(mp2);
				lpfc_mbuf_free(phba, mp1->virt, mp1->phys);
				kfree(mp1);
				spin_lock_irq(phba->host->host_lock);
				list_add_tail(&iocb->list, lpfc_iocb_list);
				spin_unlock_irq(phba->host->host_lock);
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

		spin_lock_irq(phba->host->host_lock);
		if (lpfc_sli_issue_iocb(phba, pring, iocb, 0) == IOCB_ERROR) {
			lpfc_mbuf_free(phba, mp1->virt, mp1->phys);
			kfree(mp1);
			cnt++;
			if (mp2) {
				lpfc_mbuf_free(phba, mp2->virt, mp2->phys);
				kfree(mp2);
				cnt++;
			}
			list_add_tail(&iocb->list, lpfc_iocb_list);
			pring->missbufcnt = cnt;
			spin_unlock_irq(phba->host->host_lock);
			return cnt;
		}
		spin_unlock_irq(phba->host->host_lock);
		lpfc_sli_ringpostbuf_put(phba, pring, mp1);
		if (mp2) {
			lpfc_sli_ringpostbuf_put(phba, pring, mp2);
		}
	}
	pring->missbufcnt = 0;
	return 0;
}

/************************************************************************/
/*                                                                      */
/*   lpfc_post_rcv_buf                                                  */
/*   This routine post initial rcv buffers to the configured rings      */
/*                                                                      */
/************************************************************************/
static int
lpfc_post_rcv_buf(struct lpfc_hba * phba)
{
	struct lpfc_sli *psli = &phba->sli;

	/* Ring 0, ELS / CT buffers */
	lpfc_post_buffer(phba, &psli->ring[LPFC_ELS_RING], LPFC_BUF_RING0, 1);
	/* Ring 2 - FCP no buffers needed */

	return 0;
}

#define S(N,V) (((V)<<(N))|((V)>>(32-(N))))

/************************************************************************/
/*                                                                      */
/*   lpfc_sha_init                                                      */
/*                                                                      */
/************************************************************************/
static void
lpfc_sha_init(uint32_t * HashResultPointer)
{
	HashResultPointer[0] = 0x67452301;
	HashResultPointer[1] = 0xEFCDAB89;
	HashResultPointer[2] = 0x98BADCFE;
	HashResultPointer[3] = 0x10325476;
	HashResultPointer[4] = 0xC3D2E1F0;
}

/************************************************************************/
/*                                                                      */
/*   lpfc_sha_iterate                                                   */
/*                                                                      */
/************************************************************************/
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

/************************************************************************/
/*                                                                      */
/*   lpfc_challenge_key                                                 */
/*                                                                      */
/************************************************************************/
static void
lpfc_challenge_key(uint32_t * RandomChallenge, uint32_t * HashWorking)
{
	*HashWorking = (*RandomChallenge ^ *HashWorking);
}

/************************************************************************/
/*                                                                      */
/*   lpfc_hba_init                                                      */
/*                                                                      */
/************************************************************************/
void
lpfc_hba_init(struct lpfc_hba *phba, uint32_t *hbainit)
{
	int t;
	uint32_t *HashWorking;
	uint32_t *pwwnn = phba->wwnn;

	HashWorking = kmalloc(80 * sizeof(uint32_t), GFP_KERNEL);
	if (!HashWorking)
		return;

	memset(HashWorking, 0, (80 * sizeof(uint32_t)));
	HashWorking[0] = HashWorking[78] = *pwwnn++;
	HashWorking[1] = HashWorking[79] = *pwwnn;

	for (t = 0; t < 7; t++)
		lpfc_challenge_key(phba->RandomData + t, HashWorking + t);

	lpfc_sha_init(hbainit);
	lpfc_sha_iterate(hbainit, HashWorking);
	kfree(HashWorking);
}

static void
lpfc_cleanup(struct lpfc_hba * phba, uint32_t save_bind)
{
	struct lpfc_nodelist *ndlp, *next_ndlp;

	/* clean up phba - lpfc specific */
	lpfc_can_disctmo(phba);
	list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_nlpunmap_list,
				nlp_listp) {
		lpfc_nlp_remove(phba, ndlp);
	}

	list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_nlpmap_list,
				 nlp_listp) {
		lpfc_nlp_remove(phba, ndlp);
	}

	list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_unused_list,
				nlp_listp) {
		lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
	}

	list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_plogi_list,
				nlp_listp) {
		lpfc_nlp_remove(phba, ndlp);
	}

	list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_adisc_list,
				nlp_listp) {
		lpfc_nlp_remove(phba, ndlp);
	}

	list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_reglogin_list,
				nlp_listp) {
		lpfc_nlp_remove(phba, ndlp);
	}

	list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_prli_list,
				nlp_listp) {
		lpfc_nlp_remove(phba, ndlp);
	}

	list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_npr_list,
				nlp_listp) {
		lpfc_nlp_remove(phba, ndlp);
	}

	INIT_LIST_HEAD(&phba->fc_nlpmap_list);
	INIT_LIST_HEAD(&phba->fc_nlpunmap_list);
	INIT_LIST_HEAD(&phba->fc_unused_list);
	INIT_LIST_HEAD(&phba->fc_plogi_list);
	INIT_LIST_HEAD(&phba->fc_adisc_list);
	INIT_LIST_HEAD(&phba->fc_reglogin_list);
	INIT_LIST_HEAD(&phba->fc_prli_list);
	INIT_LIST_HEAD(&phba->fc_npr_list);

	phba->fc_map_cnt   = 0;
	phba->fc_unmap_cnt = 0;
	phba->fc_plogi_cnt = 0;
	phba->fc_adisc_cnt = 0;
	phba->fc_reglogin_cnt = 0;
	phba->fc_prli_cnt  = 0;
	phba->fc_npr_cnt   = 0;
	phba->fc_unused_cnt= 0;
	return;
}

static void
lpfc_establish_link_tmo(unsigned long ptr)
{
	struct lpfc_hba *phba = (struct lpfc_hba *)ptr;
	unsigned long iflag;


	/* Re-establishing Link, timer expired */
	lpfc_printf_log(phba, KERN_ERR, LOG_LINK_EVENT,
			"%d:1300 Re-establishing Link, timer expired "
			"Data: x%x x%x\n",
			phba->brd_no, phba->fc_flag, phba->hba_state);
	spin_lock_irqsave(phba->host->host_lock, iflag);
	phba->fc_flag &= ~FC_ESTABLISH_LINK;
	spin_unlock_irqrestore(phba->host->host_lock, iflag);
}

static int
lpfc_stop_timer(struct lpfc_hba * phba)
{
	struct lpfc_sli *psli = &phba->sli;

	/* Instead of a timer, this has been converted to a
	 * deferred procedding list.
	 */
	while (!list_empty(&phba->freebufList)) {

		struct lpfc_dmabuf *mp = NULL;

		list_remove_head((&phba->freebufList), mp,
				 struct lpfc_dmabuf, list);
		if (mp) {
			lpfc_mbuf_free(phba, mp->virt, mp->phys);
			kfree(mp);
		}
	}

	del_timer_sync(&phba->fc_estabtmo);
	del_timer_sync(&phba->fc_disctmo);
	del_timer_sync(&phba->fc_fdmitmo);
	del_timer_sync(&phba->els_tmofunc);
	psli = &phba->sli;
	del_timer_sync(&psli->mbox_tmo);
	return(1);
}

int
lpfc_online(struct lpfc_hba * phba)
{
	if (!phba)
		return 0;

	if (!(phba->fc_flag & FC_OFFLINE_MODE))
		return 0;

	lpfc_printf_log(phba,
		       KERN_WARNING,
		       LOG_INIT,
		       "%d:0458 Bring Adapter online\n",
		       phba->brd_no);

	if (!lpfc_sli_queue_setup(phba))
		return 1;

	if (lpfc_sli_hba_setup(phba))	/* Initialize the HBA */
		return 1;

	spin_lock_irq(phba->host->host_lock);
	phba->fc_flag &= ~FC_OFFLINE_MODE;
	spin_unlock_irq(phba->host->host_lock);

	/*
	 * Restart all traffic to this host.  Since the fc_transport block
	 * functions (future) were not called in lpfc_offline, don't call them
	 * here.
	 */
	scsi_unblock_requests(phba->host);
	return 0;
}

int
lpfc_offline(struct lpfc_hba * phba)
{
	struct lpfc_sli_ring *pring;
	struct lpfc_sli *psli;
	unsigned long iflag;
	int i = 0;

	if (!phba)
		return 0;

	if (phba->fc_flag & FC_OFFLINE_MODE)
		return 0;

	/*
	 * Don't call the fc_transport block api (future).  The device is
	 * going offline and causing a timer to fire in the midlayer is
	 * unproductive.  Just block all new requests until the driver
	 * comes back online.
	 */
	scsi_block_requests(phba->host);
	psli = &phba->sli;
	pring = &psli->ring[psli->fcp_ring];

	lpfc_linkdown(phba);

	/* The linkdown event takes 30 seconds to timeout. */
	while (pring->txcmplq_cnt) {
		mdelay(10);
		if (i++ > 3000)
			break;
	}

	/* stop all timers associated with this hba */
	lpfc_stop_timer(phba);
	phba->work_hba_events = 0;

	lpfc_printf_log(phba,
		       KERN_WARNING,
		       LOG_INIT,
		       "%d:0460 Bring Adapter offline\n",
		       phba->brd_no);

	/* Bring down the SLI Layer and cleanup.  The HBA is offline
	   now.  */
	lpfc_sli_hba_down(phba);
	lpfc_cleanup(phba, 1);
	spin_lock_irqsave(phba->host->host_lock, iflag);
	phba->fc_flag |= FC_OFFLINE_MODE;
	spin_unlock_irqrestore(phba->host->host_lock, iflag);
	return 0;
}

/******************************************************************************
* Function name: lpfc_scsi_free
*
* Description: Called from lpfc_pci_remove_one free internal driver resources
*
******************************************************************************/
static int
lpfc_scsi_free(struct lpfc_hba * phba)
{
	struct lpfc_scsi_buf *sb, *sb_next;
	struct lpfc_iocbq *io, *io_next;

	spin_lock_irq(phba->host->host_lock);
	/* Release all the lpfc_scsi_bufs maintained by this host. */
	list_for_each_entry_safe(sb, sb_next, &phba->lpfc_scsi_buf_list, list) {
		list_del(&sb->list);
		pci_pool_free(phba->lpfc_scsi_dma_buf_pool, sb->data,
								sb->dma_handle);
		kfree(sb);
		phba->total_scsi_bufs--;
	}

	/* Release all the lpfc_iocbq entries maintained by this host. */
	list_for_each_entry_safe(io, io_next, &phba->lpfc_iocb_list, list) {
		list_del(&io->list);
		kfree(io);
		phba->total_iocbq_bufs--;
	}

	spin_unlock_irq(phba->host->host_lock);

	return 0;
}


static int __devinit
lpfc_pci_probe_one(struct pci_dev *pdev, const struct pci_device_id *pid)
{
	struct Scsi_Host *host;
	struct lpfc_hba  *phba;
	struct lpfc_sli  *psli;
	struct lpfc_iocbq *iocbq_entry = NULL, *iocbq_next = NULL;
	unsigned long bar0map_len, bar2map_len;
	int error = -ENODEV, retval;
	int i;

	if (pci_enable_device(pdev))
		goto out;
	if (pci_request_regions(pdev, LPFC_DRIVER_NAME))
		goto out_disable_device;

	host = scsi_host_alloc(&lpfc_template, sizeof (struct lpfc_hba));
	if (!host)
		goto out_release_regions;

	phba = (struct lpfc_hba*)host->hostdata;
	memset(phba, 0, sizeof (struct lpfc_hba));
	phba->host = host;

	phba->fc_flag |= FC_LOADING;
	phba->pcidev = pdev;

	/* Assign an unused board number */
	if (!idr_pre_get(&lpfc_hba_index, GFP_KERNEL))
		goto out_put_host;

	error = idr_get_new(&lpfc_hba_index, NULL, &phba->brd_no);
	if (error)
		goto out_put_host;

	host->unique_id = phba->brd_no;

	INIT_LIST_HEAD(&phba->ctrspbuflist);
	INIT_LIST_HEAD(&phba->rnidrspbuflist);
	INIT_LIST_HEAD(&phba->freebufList);

	/* Initialize timers used by driver */
	init_timer(&phba->fc_estabtmo);
	phba->fc_estabtmo.function = lpfc_establish_link_tmo;
	phba->fc_estabtmo.data = (unsigned long)phba;
	init_timer(&phba->fc_disctmo);
	phba->fc_disctmo.function = lpfc_disc_timeout;
	phba->fc_disctmo.data = (unsigned long)phba;

	init_timer(&phba->fc_fdmitmo);
	phba->fc_fdmitmo.function = lpfc_fdmi_tmo;
	phba->fc_fdmitmo.data = (unsigned long)phba;
	init_timer(&phba->els_tmofunc);
	phba->els_tmofunc.function = lpfc_els_timeout;
	phba->els_tmofunc.data = (unsigned long)phba;
	psli = &phba->sli;
	init_timer(&psli->mbox_tmo);
	psli->mbox_tmo.function = lpfc_mbox_timeout;
	psli->mbox_tmo.data = (unsigned long)phba;

	/*
	 * Get all the module params for configuring this host and then
	 * establish the host parameters.
	 */
	lpfc_get_cfgparam(phba);

	host->max_id = LPFC_MAX_TARGET;
	host->max_lun = phba->cfg_max_luns;
	host->this_id = -1;

	/* Initialize all internally managed lists. */
	INIT_LIST_HEAD(&phba->fc_nlpmap_list);
	INIT_LIST_HEAD(&phba->fc_nlpunmap_list);
	INIT_LIST_HEAD(&phba->fc_unused_list);
	INIT_LIST_HEAD(&phba->fc_plogi_list);
	INIT_LIST_HEAD(&phba->fc_adisc_list);
	INIT_LIST_HEAD(&phba->fc_reglogin_list);
	INIT_LIST_HEAD(&phba->fc_prli_list);
	INIT_LIST_HEAD(&phba->fc_npr_list);


	pci_set_master(pdev);
	retval = pci_set_mwi(pdev);
	if (retval)
		dev_printk(KERN_WARNING, &pdev->dev,
			   "Warning: pci_set_mwi returned %d\n", retval);

	if (pci_set_dma_mask(phba->pcidev, DMA_64BIT_MASK) != 0)
		if (pci_set_dma_mask(phba->pcidev, DMA_32BIT_MASK) != 0)
			goto out_idr_remove;

	/*
	 * Get the bus address of Bar0 and Bar2 and the number of bytes
	 * required by each mapping.
	 */
	phba->pci_bar0_map = pci_resource_start(phba->pcidev, 0);
	bar0map_len        = pci_resource_len(phba->pcidev, 0);

	phba->pci_bar2_map = pci_resource_start(phba->pcidev, 2);
	bar2map_len        = pci_resource_len(phba->pcidev, 2);

	/* Map HBA SLIM and Control Registers to a kernel virtual address. */
	phba->slim_memmap_p      = ioremap(phba->pci_bar0_map, bar0map_len);
	phba->ctrl_regs_memmap_p = ioremap(phba->pci_bar2_map, bar2map_len);

	/* Allocate memory for SLI-2 structures */
	phba->slim2p = dma_alloc_coherent(&phba->pcidev->dev, SLI2_SLIM_SIZE,
					  &phba->slim2p_mapping, GFP_KERNEL);
	if (!phba->slim2p)
		goto out_iounmap;


	/* Initialize the SLI Layer to run with lpfc HBAs. */
	lpfc_sli_setup(phba);
	lpfc_sli_queue_setup(phba);

	error = lpfc_mem_alloc(phba);
	if (error)
		goto out_free_slim;

	/* Initialize and populate the iocb list per host.  */
	INIT_LIST_HEAD(&phba->lpfc_iocb_list);
	for (i = 0; i < LPFC_IOCB_LIST_CNT; i++) {
		iocbq_entry = kmalloc(sizeof(struct lpfc_iocbq), GFP_KERNEL);
		if (iocbq_entry == NULL) {
			printk(KERN_ERR "%s: only allocated %d iocbs of "
				"expected %d count. Unloading driver.\n",
				__FUNCTION__, i, LPFC_IOCB_LIST_CNT);
			error = -ENOMEM;
			goto out_free_iocbq;
		}

		memset(iocbq_entry, 0, sizeof(struct lpfc_iocbq));
		spin_lock_irq(phba->host->host_lock);
		list_add(&iocbq_entry->list, &phba->lpfc_iocb_list);
		phba->total_iocbq_bufs++;
		spin_unlock_irq(phba->host->host_lock);
	}

	/* Initialize HBA structure */
	phba->fc_edtov = FF_DEF_EDTOV;
	phba->fc_ratov = FF_DEF_RATOV;
	phba->fc_altov = FF_DEF_ALTOV;
	phba->fc_arbtov = FF_DEF_ARBTOV;

	INIT_LIST_HEAD(&phba->work_list);
	phba->work_ha_mask = (HA_ERATT|HA_MBATT|HA_LATT);
	phba->work_ha_mask |= (HA_RXMASK << (LPFC_ELS_RING * 4));

	/* Startup the kernel thread for this host adapter. */
	phba->worker_thread = kthread_run(lpfc_do_work, phba,
				       "lpfc_worker_%d", phba->brd_no);
	if (IS_ERR(phba->worker_thread)) {
		error = PTR_ERR(phba->worker_thread);
		goto out_free_iocbq;
	}

	/* We can rely on a queue depth attribute only after SLI HBA setup */
	host->can_queue = phba->cfg_hba_queue_depth - 10;

	/* Tell the midlayer we support 16 byte commands */
	host->max_cmd_len = 16;

	/* Initialize the list of scsi buffers used by driver for scsi IO. */
	INIT_LIST_HEAD(&phba->lpfc_scsi_buf_list);

	host->transportt = lpfc_transport_template;
	host->hostdata[0] = (unsigned long)phba;
	pci_set_drvdata(pdev, host);
	error = scsi_add_host(host, &pdev->dev);
	if (error)
		goto out_kthread_stop;

	error = lpfc_alloc_sysfs_attr(phba);
	if (error)
		goto out_kthread_stop;

	error =	request_irq(phba->pcidev->irq, lpfc_intr_handler, SA_SHIRQ,
							LPFC_DRIVER_NAME, phba);
	if (error) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
			"%d:0451 Enable interrupt handler failed\n",
			phba->brd_no);
		goto out_free_sysfs_attr;
	}
	phba->MBslimaddr = phba->slim_memmap_p;
	phba->HAregaddr = phba->ctrl_regs_memmap_p + HA_REG_OFFSET;
	phba->CAregaddr = phba->ctrl_regs_memmap_p + CA_REG_OFFSET;
	phba->HSregaddr = phba->ctrl_regs_memmap_p + HS_REG_OFFSET;
	phba->HCregaddr = phba->ctrl_regs_memmap_p + HC_REG_OFFSET;

	error = lpfc_sli_hba_setup(phba);
	if (error)
		goto out_free_irq;

	/*
	 * set fixed host attributes
	 * Must done after lpfc_sli_hba_setup()
	 */

	fc_host_node_name(host) = wwn_to_u64(phba->fc_nodename.u.wwn);
	fc_host_port_name(host) = wwn_to_u64(phba->fc_portname.u.wwn);
	fc_host_supported_classes(host) = FC_COS_CLASS3;

	memset(fc_host_supported_fc4s(host), 0,
		sizeof(fc_host_supported_fc4s(host)));
	fc_host_supported_fc4s(host)[2] = 1;
	fc_host_supported_fc4s(host)[7] = 1;

	lpfc_get_hba_sym_node_name(phba, fc_host_symbolic_name(host));

	fc_host_supported_speeds(host) = 0;
	switch (FC_JEDEC_ID(phba->vpd.rev.biuRev)) {
	case VIPER_JEDEC_ID:
		fc_host_supported_speeds(host) |= FC_PORTSPEED_10GBIT;
		break;
	case HELIOS_JEDEC_ID:
		fc_host_supported_speeds(host) |= FC_PORTSPEED_4GBIT;
		/* Fall through */
	case CENTAUR_2G_JEDEC_ID:
	case PEGASUS_JEDEC_ID:
	case THOR_JEDEC_ID:
		fc_host_supported_speeds(host) |= FC_PORTSPEED_2GBIT;
		/* Fall through */
	default:
		fc_host_supported_speeds(host) = FC_PORTSPEED_1GBIT;
	}

	fc_host_maxframe_size(host) =
		((((uint32_t) phba->fc_sparam.cmn.bbRcvSizeMsb & 0x0F) << 8) |
		 (uint32_t) phba->fc_sparam.cmn.bbRcvSizeLsb);

	/* This value is also unchanging */
	memset(fc_host_active_fc4s(host), 0,
		sizeof(fc_host_active_fc4s(host)));
	fc_host_active_fc4s(host)[2] = 1;
	fc_host_active_fc4s(host)[7] = 1;

	spin_lock_irq(phba->host->host_lock);
	phba->fc_flag &= ~FC_LOADING;
	spin_unlock_irq(phba->host->host_lock);
	return 0;

out_free_irq:
	lpfc_stop_timer(phba);
	phba->work_hba_events = 0;
	free_irq(phba->pcidev->irq, phba);
out_free_sysfs_attr:
	lpfc_free_sysfs_attr(phba);
out_kthread_stop:
	kthread_stop(phba->worker_thread);
out_free_iocbq:
	list_for_each_entry_safe(iocbq_entry, iocbq_next,
						&phba->lpfc_iocb_list, list) {
		spin_lock_irq(phba->host->host_lock);
		kfree(iocbq_entry);
		phba->total_iocbq_bufs--;
		spin_unlock_irq(phba->host->host_lock);
	}
	lpfc_mem_free(phba);
out_free_slim:
	dma_free_coherent(&pdev->dev, SLI2_SLIM_SIZE, phba->slim2p,
							phba->slim2p_mapping);
out_iounmap:
	iounmap(phba->ctrl_regs_memmap_p);
	iounmap(phba->slim_memmap_p);
out_idr_remove:
	idr_remove(&lpfc_hba_index, phba->brd_no);
out_put_host:
	scsi_host_put(host);
out_release_regions:
	pci_release_regions(pdev);
out_disable_device:
	pci_disable_device(pdev);
out:
	return error;
}

static void __devexit
lpfc_pci_remove_one(struct pci_dev *pdev)
{
	struct Scsi_Host   *host = pci_get_drvdata(pdev);
	struct lpfc_hba    *phba = (struct lpfc_hba *)host->hostdata[0];
	unsigned long iflag;

	lpfc_free_sysfs_attr(phba);

	spin_lock_irqsave(phba->host->host_lock, iflag);
	phba->fc_flag |= FC_UNLOADING;

	spin_unlock_irqrestore(phba->host->host_lock, iflag);

	fc_remove_host(phba->host);
	scsi_remove_host(phba->host);

	kthread_stop(phba->worker_thread);

	/*
	 * Bring down the SLI Layer. This step disable all interrupts,
	 * clears the rings, discards all mailbox commands, and resets
	 * the HBA.
	 */
	lpfc_sli_hba_down(phba);

	/* Release the irq reservation */
	free_irq(phba->pcidev->irq, phba);

	lpfc_cleanup(phba, 0);
	lpfc_stop_timer(phba);
	phba->work_hba_events = 0;

	/*
	 * Call scsi_free before mem_free since scsi bufs are released to their
	 * corresponding pools here.
	 */
	lpfc_scsi_free(phba);
	lpfc_mem_free(phba);

	/* Free resources associated with SLI2 interface */
	dma_free_coherent(&pdev->dev, SLI2_SLIM_SIZE,
			  phba->slim2p, phba->slim2p_mapping);

	/* unmap adapter SLIM and Control Registers */
	iounmap(phba->ctrl_regs_memmap_p);
	iounmap(phba->slim_memmap_p);

	pci_release_regions(phba->pcidev);
	pci_disable_device(phba->pcidev);

	idr_remove(&lpfc_hba_index, phba->brd_no);
	scsi_host_put(phba->host);

	pci_set_drvdata(pdev, NULL);
}

static struct pci_device_id lpfc_id_table[] = {
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_VIPER,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_FIREFLY,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_THOR,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_PEGASUS,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_CENTAUR,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_DRAGONFLY,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_SUPERFLY,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_RFLY,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_PFLY,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_HELIOS,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_BMID,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_BSMB,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_ZEPHYR,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_ZMID,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_ZSMB,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_TFLY,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_LP101,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_LP10000S,
		PCI_ANY_ID, PCI_ANY_ID, },
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, lpfc_id_table);

static struct pci_driver lpfc_driver = {
	.name		= LPFC_DRIVER_NAME,
	.id_table	= lpfc_id_table,
	.probe		= lpfc_pci_probe_one,
	.remove		= __devexit_p(lpfc_pci_remove_one),
};

static int __init
lpfc_init(void)
{
	int error = 0;

	printk(LPFC_MODULE_DESC "\n");
	printk(LPFC_COPYRIGHT "\n");

	lpfc_transport_template =
				fc_attach_transport(&lpfc_transport_functions);
	if (!lpfc_transport_template)
		return -ENOMEM;
	error = pci_register_driver(&lpfc_driver);
	if (error)
		fc_release_transport(lpfc_transport_template);

	return error;
}

static void __exit
lpfc_exit(void)
{
	pci_unregister_driver(&lpfc_driver);
	fc_release_transport(lpfc_transport_template);
}

module_init(lpfc_init);
module_exit(lpfc_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(LPFC_MODULE_DESC);
MODULE_AUTHOR("Emulex Corporation - tech.support@emulex.com");
MODULE_VERSION("0:" LPFC_DRIVER_VERSION);
