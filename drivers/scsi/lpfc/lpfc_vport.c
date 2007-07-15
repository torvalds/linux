/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2004-2006 Emulex.  All rights reserved.           *
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
#include "lpfc_vport.h"

inline void lpfc_vport_set_state(struct lpfc_vport *vport,
				 enum fc_vport_state new_state)
{
	struct fc_vport *fc_vport = vport->fc_vport;

	if (fc_vport) {
		/*
		 * When the transport defines fc_vport_set state we will replace
		 * this code with the following line
		 */
		/* fc_vport_set_state(fc_vport, new_state); */
		if (new_state != FC_VPORT_INITIALIZING)
			fc_vport->vport_last_state = fc_vport->vport_state;
		fc_vport->vport_state = new_state;
	}

	/* for all the error states we will set the invternal state to FAILED */
	switch (new_state) {
	case FC_VPORT_NO_FABRIC_SUPP:
	case FC_VPORT_NO_FABRIC_RSCS:
	case FC_VPORT_FABRIC_LOGOUT:
	case FC_VPORT_FABRIC_REJ_WWN:
	case FC_VPORT_FAILED:
		vport->port_state = LPFC_VPORT_FAILED;
		break;
	case FC_VPORT_LINKDOWN:
		vport->port_state = LPFC_VPORT_UNKNOWN;
		break;
	default:
		/* do nothing */
		break;
	}
}

static int
lpfc_alloc_vpi(struct lpfc_hba *phba)
{
	int  vpi;

	spin_lock_irq(&phba->hbalock);
	/* Start at bit 1 because vpi zero is reserved for the physical port */
	vpi = find_next_zero_bit(phba->vpi_bmask, (phba->max_vpi + 1), 1);
	if (vpi > phba->max_vpi)
		vpi = 0;
	else
		set_bit(vpi, phba->vpi_bmask);
	spin_unlock_irq(&phba->hbalock);
	return vpi;
}

static void
lpfc_free_vpi(struct lpfc_hba *phba, int vpi)
{
	spin_lock_irq(&phba->hbalock);
	clear_bit(vpi, phba->vpi_bmask);
	spin_unlock_irq(&phba->hbalock);
}

static int
lpfc_vport_sparm(struct lpfc_hba *phba, struct lpfc_vport *vport)
{
	LPFC_MBOXQ_t *pmb;
	MAILBOX_t *mb;
	struct lpfc_dmabuf *mp;
	int  rc;

	pmb = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!pmb) {
		return -ENOMEM;
	}
	mb = &pmb->mb;

	lpfc_read_sparam(phba, pmb, vport->vpi);
	/*
	 * Grab buffer pointer and clear context1 so we can use
	 * lpfc_sli_issue_box_wait
	 */
	mp = (struct lpfc_dmabuf *) pmb->context1;
	pmb->context1 = NULL;

	pmb->vport = vport;
	rc = lpfc_sli_issue_mbox_wait(phba, pmb, phba->fc_ratov * 2);
	if (rc != MBX_SUCCESS) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT | LOG_VPORT,
				"%d (%d):1818 VPort failed init, mbxCmd x%x "
				"READ_SPARM mbxStatus x%x, rc = x%x\n",
				phba->brd_no, vport->vpi,
				mb->mbxCommand, mb->mbxStatus, rc);
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		kfree(mp);
		if (rc != MBX_TIMEOUT)
			mempool_free(pmb, phba->mbox_mem_pool);
		return -EIO;
	}

	memcpy(&vport->fc_sparam, mp->virt, sizeof (struct serv_parm));
	memcpy(&vport->fc_nodename, &vport->fc_sparam.nodeName,
	       sizeof (struct lpfc_name));
	memcpy(&vport->fc_portname, &vport->fc_sparam.portName,
	       sizeof (struct lpfc_name));

	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
	mempool_free(pmb, phba->mbox_mem_pool);

	return 0;
}

static int
lpfc_valid_wwn_format(struct lpfc_hba *phba, struct lpfc_name *wwn,
		      const char *name_type)
{
				/* ensure that IEEE format 1 addresses
				 * contain zeros in bits 59-48
				 */
	if (!((wwn->u.wwn[0] >> 4) == 1 &&
	      ((wwn->u.wwn[0] & 0xf) != 0 || (wwn->u.wwn[1] & 0xf) != 0)))
		return 1;

	lpfc_printf_log(phba, KERN_ERR, LOG_VPORT,
			"%d:1822 Invalid %s: %02x:%02x:%02x:%02x:"
			"%02x:%02x:%02x:%02x\n",
			phba->brd_no, name_type,
			wwn->u.wwn[0], wwn->u.wwn[1],
			wwn->u.wwn[2], wwn->u.wwn[3],
			wwn->u.wwn[4], wwn->u.wwn[5],
			wwn->u.wwn[6], wwn->u.wwn[7]);
	return 0;
}

static int
lpfc_unique_wwpn(struct lpfc_hba *phba, struct lpfc_vport *new_vport)
{
	struct lpfc_vport *vport;

	list_for_each_entry(vport, &phba->port_list, listentry) {
		if (vport == new_vport)
			continue;
		/* If they match, return not unique */
		if (memcmp(&vport->fc_sparam.portName,
			&new_vport->fc_sparam.portName,
			sizeof(struct lpfc_name)) == 0)
			return 0;
	}
	return 1;
}

int
lpfc_vport_create(struct fc_vport *fc_vport, bool disable)
{
	struct lpfc_nodelist *ndlp;
	struct lpfc_vport *pport =
		(struct lpfc_vport *) fc_vport->shost->hostdata;
	struct lpfc_hba   *phba = pport->phba;
	struct lpfc_vport *vport = NULL;
	int instance;
	int vpi;
	int rc = VPORT_ERROR;

	if ((phba->sli_rev < 3) ||
		!(phba->sli3_options & LPFC_SLI3_NPIV_ENABLED)) {
		lpfc_printf_log(phba, KERN_ERR, LOG_VPORT,
				"%d:1808 Create VPORT failed: "
				"NPIV is not enabled: SLImode:%d\n",
				phba->brd_no, phba->sli_rev);
		rc = VPORT_INVAL;
		goto error_out;
	}

	vpi = lpfc_alloc_vpi(phba);
	if (vpi == 0) {
		lpfc_printf_log(phba, KERN_ERR, LOG_VPORT,
				"%d:1809 Create VPORT failed: "
				"Max VPORTs (%d) exceeded\n",
				phba->brd_no, phba->max_vpi);
		rc = VPORT_NORESOURCES;
		goto error_out;
	}


	/* Assign an unused board number */
	if ((instance = lpfc_get_instance()) < 0) {
		lpfc_printf_log(phba, KERN_ERR, LOG_VPORT,
				"%d:1810 Create VPORT failed: Cannot get "
				"instance number\n", phba->brd_no);
		lpfc_free_vpi(phba, vpi);
		rc = VPORT_NORESOURCES;
		goto error_out;
	}

	vport = lpfc_create_port(phba, instance, fc_vport);
	if (!vport) {
		lpfc_printf_log(phba, KERN_ERR, LOG_VPORT,
				"%d:1811 Create VPORT failed: vpi x%x\n",
				phba->brd_no, vpi);
		lpfc_free_vpi(phba, vpi);
		rc = VPORT_NORESOURCES;
		goto error_out;
	}

	vport->vpi = vpi;
	lpfc_debugfs_initialize(vport);

	if (lpfc_vport_sparm(phba, vport)) {
		lpfc_printf_log(phba, KERN_ERR, LOG_VPORT,
				"%d:1813 Create VPORT failed: vpi:%d "
				"Cannot get sparam\n",
				phba->brd_no, vpi);
		lpfc_free_vpi(phba, vpi);
		destroy_port(vport);
		rc = VPORT_NORESOURCES;
		goto error_out;
	}

	memcpy(vport->fc_portname.u.wwn, vport->fc_sparam.portName.u.wwn, 8);
	memcpy(vport->fc_nodename.u.wwn, vport->fc_sparam.nodeName.u.wwn, 8);

	if (fc_vport->node_name != 0)
		u64_to_wwn(fc_vport->node_name, vport->fc_nodename.u.wwn);
	if (fc_vport->port_name != 0)
		u64_to_wwn(fc_vport->port_name, vport->fc_portname.u.wwn);

	memcpy(&vport->fc_sparam.portName, vport->fc_portname.u.wwn, 8);
	memcpy(&vport->fc_sparam.nodeName, vport->fc_nodename.u.wwn, 8);

	if (!lpfc_valid_wwn_format(phba, &vport->fc_sparam.nodeName, "WWNN") ||
	    !lpfc_valid_wwn_format(phba, &vport->fc_sparam.portName, "WWPN")) {
		lpfc_printf_log(phba, KERN_ERR, LOG_VPORT,
				"%d:1821 Create VPORT failed: vpi:%d "
				"Invalid WWN format\n",
				phba->brd_no, vpi);
		lpfc_free_vpi(phba, vpi);
		destroy_port(vport);
		rc = VPORT_INVAL;
		goto error_out;
	}

	if (!lpfc_unique_wwpn(phba, vport)) {
		lpfc_printf_log(phba, KERN_ERR, LOG_VPORT,
				"%d:1823 Create VPORT failed: vpi:%d "
				"Duplicate WWN on HBA\n",
				phba->brd_no, vpi);
		lpfc_free_vpi(phba, vpi);
		destroy_port(vport);
		rc = VPORT_INVAL;
		goto error_out;
	}

	*(struct lpfc_vport **)fc_vport->dd_data = vport;
	vport->fc_vport = fc_vport;

	if ((phba->link_state < LPFC_LINK_UP) ||
	    (phba->fc_topology == TOPOLOGY_LOOP)) {
		lpfc_vport_set_state(vport, FC_VPORT_LINKDOWN);
		rc = VPORT_OK;
		goto out;
	}

	if (disable) {
		rc = VPORT_OK;
		goto out;
	}

	/* Use the Physical nodes Fabric NDLP to determine if the link is
	 * up and ready to FDISC.
	 */
	ndlp = lpfc_findnode_did(phba->pport, Fabric_DID);
	if (ndlp && ndlp->nlp_state == NLP_STE_UNMAPPED_NODE) {
		if (phba->link_flag & LS_NPIV_FAB_SUPPORTED) {
			lpfc_set_disctmo(vport);
			lpfc_initial_fdisc(vport);
		} else {
			lpfc_vport_set_state(vport, FC_VPORT_NO_FABRIC_SUPP);
			lpfc_printf_log(phba, KERN_ERR, LOG_ELS,
					"%d (%d):0262 No NPIV Fabric "
					"support\n",
					phba->brd_no, vport->vpi);
		}
	} else {
		lpfc_vport_set_state(vport, FC_VPORT_FAILED);
	}
	rc = VPORT_OK;

out:
	lpfc_host_attrib_init(lpfc_shost_from_vport(vport));
error_out:
	return rc;
}

int
disable_vport(struct fc_vport *fc_vport)
{
	struct lpfc_vport *vport = *(struct lpfc_vport **)fc_vport->dd_data;
	struct lpfc_hba   *phba = vport->phba;
	struct lpfc_nodelist *ndlp = NULL, *next_ndlp = NULL;
	long timeout;

	ndlp = lpfc_findnode_did(vport, Fabric_DID);
	if (ndlp && phba->link_state >= LPFC_LINK_UP) {
		vport->unreg_vpi_cmpl = VPORT_INVAL;
		timeout = msecs_to_jiffies(phba->fc_ratov * 2000);
		if (!lpfc_issue_els_npiv_logo(vport, ndlp))
			while (vport->unreg_vpi_cmpl == VPORT_INVAL && timeout)
				timeout = schedule_timeout(timeout);
	}

	lpfc_sli_host_down(vport);

	/* Mark all nodes for discovery so we can remove them by
	 * calling lpfc_cleanup_rpis(vport, 1)
	 */
	list_for_each_entry_safe(ndlp, next_ndlp, &vport->fc_nodes, nlp_listp) {
		if (ndlp->nlp_state == NLP_STE_UNUSED_NODE)
			continue;
		lpfc_disc_state_machine(vport, ndlp, NULL,
					NLP_EVT_DEVICE_RECOVERY);
	}
	lpfc_cleanup_rpis(vport, 1);

	lpfc_stop_vport_timers(vport);
	lpfc_unreg_all_rpis(vport);
	lpfc_unreg_default_rpis(vport);
	/*
	 * Completion of unreg_vpi (lpfc_mbx_cmpl_unreg_vpi) does the
	 * scsi_host_put() to release the vport.
	 */
	lpfc_mbx_unreg_vpi(vport);

	lpfc_vport_set_state(vport, FC_VPORT_DISABLED);
	return VPORT_OK;
}

int
enable_vport(struct fc_vport *fc_vport)
{
	struct lpfc_vport *vport = *(struct lpfc_vport **)fc_vport->dd_data;
	struct lpfc_hba   *phba = vport->phba;
	struct lpfc_nodelist *ndlp = NULL;

	if ((phba->link_state < LPFC_LINK_UP) ||
	    (phba->fc_topology == TOPOLOGY_LOOP)) {
		lpfc_vport_set_state(vport, FC_VPORT_LINKDOWN);
		return VPORT_OK;
	}

	vport->load_flag |= FC_LOADING;
	vport->fc_flag |= FC_VPORT_NEEDS_REG_VPI;

	/* Use the Physical nodes Fabric NDLP to determine if the link is
	 * up and ready to FDISC.
	 */
	ndlp = lpfc_findnode_did(phba->pport, Fabric_DID);
	if (ndlp && ndlp->nlp_state == NLP_STE_UNMAPPED_NODE) {
		if (phba->link_flag & LS_NPIV_FAB_SUPPORTED) {
			lpfc_set_disctmo(vport);
			lpfc_initial_fdisc(vport);
		} else {
			lpfc_vport_set_state(vport, FC_VPORT_NO_FABRIC_SUPP);
			lpfc_printf_log(phba, KERN_ERR, LOG_ELS,
					"%d (%d):0264 No NPIV Fabric "
					"support\n",
					phba->brd_no, vport->vpi);
		}
	} else {
		lpfc_vport_set_state(vport, FC_VPORT_FAILED);
	}

	return VPORT_OK;
}

int
lpfc_vport_disable(struct fc_vport *fc_vport, bool disable)
{
	if (disable)
		return disable_vport(fc_vport);
	else
		return enable_vport(fc_vport);
}


int
lpfc_vport_delete(struct fc_vport *fc_vport)
{
	struct lpfc_nodelist *ndlp = NULL;
	struct lpfc_nodelist *next_ndlp;
	struct Scsi_Host *shost = (struct Scsi_Host *) fc_vport->shost;
	struct lpfc_vport *vport = *(struct lpfc_vport **)fc_vport->dd_data;
	struct lpfc_hba   *phba = vport->phba;
	long timeout;
	int rc = VPORT_ERROR;

	/*
	 * This is a bit of a mess.  We want to ensure the shost doesn't get
	 * torn down until we're done with the embedded lpfc_vport structure.
	 *
	 * Beyond holding a reference for this function, we also need a
	 * reference for outstanding I/O requests we schedule during delete
	 * processing.  But once we scsi_remove_host() we can no longer obtain
	 * a reference through scsi_host_get().
	 *
	 * So we take two references here.  We release one reference at the
	 * bottom of the function -- after delinking the vport.  And we
	 * release the other at the completion of the unreg_vpi that get's
	 * initiated after we've disposed of all other resources associated
	 * with the port.
	 */
	if (!scsi_host_get(shost) || !scsi_host_get(shost))
		return VPORT_INVAL;

	if (vport->port_type == LPFC_PHYSICAL_PORT) {
		lpfc_printf_log(phba, KERN_ERR, LOG_VPORT,
				"%d:1812 vport_delete failed: Cannot delete "
				"physical host\n", phba->brd_no);
		goto out;
	}

	vport->load_flag |= FC_UNLOADING;

	kfree(vport->vname);
	lpfc_debugfs_terminate(vport);
	fc_remove_host(lpfc_shost_from_vport(vport));
	scsi_remove_host(lpfc_shost_from_vport(vport));

	ndlp = lpfc_findnode_did(phba->pport, Fabric_DID);
	if (ndlp && ndlp->nlp_state == NLP_STE_UNMAPPED_NODE &&
		phba->link_state >= LPFC_LINK_UP) {

		/* First look for the Fabric ndlp */
		ndlp = lpfc_findnode_did(vport, Fabric_DID);
		if (!ndlp) {
			/* Cannot find existing Fabric ndlp, allocate one */
			ndlp = mempool_alloc(phba->nlp_mem_pool, GFP_KERNEL);
			if (!ndlp)
				goto skip_logo;
			lpfc_nlp_init(vport, ndlp, Fabric_DID);
		} else {
			lpfc_dequeue_node(vport, ndlp);
		}
		vport->unreg_vpi_cmpl = VPORT_INVAL;
		timeout = msecs_to_jiffies(phba->fc_ratov * 2000);
		if (!lpfc_issue_els_npiv_logo(vport, ndlp))
			while (vport->unreg_vpi_cmpl == VPORT_INVAL && timeout)
				timeout = schedule_timeout(timeout);
	}

skip_logo:
	lpfc_sli_host_down(vport);

	list_for_each_entry_safe(ndlp, next_ndlp, &vport->fc_nodes, nlp_listp) {
		lpfc_disc_state_machine(vport, ndlp, NULL,
					     NLP_EVT_DEVICE_RECOVERY);
		lpfc_disc_state_machine(vport, ndlp, NULL,
					     NLP_EVT_DEVICE_RM);
	}

	lpfc_stop_vport_timers(vport);
	lpfc_unreg_all_rpis(vport);
	lpfc_unreg_default_rpis(vport);
	/*
	 * Completion of unreg_vpi (lpfc_mbx_cmpl_unreg_vpi) does the
	 * scsi_host_put() to release the vport.
	 */
	lpfc_mbx_unreg_vpi(vport);

	lpfc_free_vpi(phba, vport->vpi);
	vport->work_port_events = 0;
	spin_lock_irq(&phba->hbalock);
	list_del_init(&vport->listentry);
	spin_unlock_irq(&phba->hbalock);

	rc = VPORT_OK;
out:
	scsi_host_put(shost);
	return rc;
}


EXPORT_SYMBOL(lpfc_vport_create);
EXPORT_SYMBOL(lpfc_vport_delete);
