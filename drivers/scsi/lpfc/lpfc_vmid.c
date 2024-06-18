/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2017-2023 Broadcom. All Rights Reserved. The term *
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

#include <linux/interrupt.h>
#include <linux/dma-direction.h>

#include <scsi/scsi_transport_fc.h>

#include "lpfc_hw4.h"
#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_sli4.h"
#include "lpfc_nl.h"
#include "lpfc_disc.h"
#include "lpfc.h"
#include "lpfc_crtn.h"


/*
 * lpfc_get_vmid_from_hashtable - search the UUID in the hash table
 * @vport: The virtual port for which this call is being executed.
 * @hash: calculated hash value
 * @buf: uuid associated with the VE
 * Return the VMID entry associated with the UUID
 * Make sure to acquire the appropriate lock before invoking this routine.
 */
struct lpfc_vmid *lpfc_get_vmid_from_hashtable(struct lpfc_vport *vport,
					       u32 hash, u8 *buf)
{
	struct lpfc_vmid *vmp;

	hash_for_each_possible(vport->hash_table, vmp, hnode, hash) {
		if (memcmp(&vmp->host_vmid[0], buf, 16) == 0)
			return vmp;
	}
	return NULL;
}

/*
 * lpfc_put_vmid_in_hashtable - put the VMID in the hash table
 * @vport: The virtual port for which this call is being executed.
 * @hash - calculated hash value
 * @vmp: Pointer to a VMID entry representing a VM sending I/O
 *
 * This routine will insert the newly acquired VMID entity in the hash table.
 * Make sure to acquire the appropriate lock before invoking this routine.
 */
static void
lpfc_put_vmid_in_hashtable(struct lpfc_vport *vport, u32 hash,
			   struct lpfc_vmid *vmp)
{
	hash_add(vport->hash_table, &vmp->hnode, hash);
}

/*
 * lpfc_vmid_hash_fn - create a hash value of the UUID
 * @vmid: uuid associated with the VE
 * @len: length of the VMID string
 * Returns the calculated hash value
 */
int lpfc_vmid_hash_fn(const char *vmid, int len)
{
	int c;
	int hash = 0;

	if (len == 0)
		return 0;
	while (len--) {
		c = *vmid++;
		if (c >= 'A' && c <= 'Z')
			c += 'a' - 'A';

		hash = (hash + (c << LPFC_VMID_HASH_SHIFT) +
			(c >> LPFC_VMID_HASH_SHIFT)) * 19;
	}

	return hash & LPFC_VMID_HASH_MASK;
}

/*
 * lpfc_vmid_update_entry - update the vmid entry in the hash table
 * @vport: The virtual port for which this call is being executed.
 * @iodir: io direction
 * @vmp: Pointer to a VMID entry representing a VM sending I/O
 * @tag: VMID tag
 */
static void lpfc_vmid_update_entry(struct lpfc_vport *vport,
				   enum dma_data_direction iodir,
				   struct lpfc_vmid *vmp,
				   union lpfc_vmid_io_tag *tag)
{
	u64 *lta;

	if (vport->phba->pport->vmid_flag & LPFC_VMID_TYPE_PRIO)
		tag->cs_ctl_vmid = vmp->un.cs_ctl_vmid;
	else if (vport->phba->cfg_vmid_app_header)
		tag->app_id = vmp->un.app_id;

	if (iodir == DMA_TO_DEVICE)
		vmp->io_wr_cnt++;
	else if (iodir == DMA_FROM_DEVICE)
		vmp->io_rd_cnt++;

	/* update the last access timestamp in the table */
	lta = per_cpu_ptr(vmp->last_io_time, raw_smp_processor_id());
	*lta = jiffies;
}

static void lpfc_vmid_assign_cs_ctl(struct lpfc_vport *vport,
				    struct lpfc_vmid *vmid)
{
	u32 hash;
	struct lpfc_vmid *pvmid;

	if (vport->port_type == LPFC_PHYSICAL_PORT) {
		vmid->un.cs_ctl_vmid = lpfc_vmid_get_cs_ctl(vport);
	} else {
		hash = lpfc_vmid_hash_fn(vmid->host_vmid, vmid->vmid_len);
		pvmid =
		    lpfc_get_vmid_from_hashtable(vport->phba->pport, hash,
						 vmid->host_vmid);
		if (pvmid)
			vmid->un.cs_ctl_vmid = pvmid->un.cs_ctl_vmid;
		else
			vmid->un.cs_ctl_vmid = lpfc_vmid_get_cs_ctl(vport);
	}
}

/*
 * lpfc_vmid_get_appid - get the VMID associated with the UUID
 * @vport: The virtual port for which this call is being executed.
 * @uuid: UUID associated with the VE
 * @cmd: address of scsi_cmd descriptor
 * @iodir: io direction
 * @tag: VMID tag
 * Returns status of the function
 */
int lpfc_vmid_get_appid(struct lpfc_vport *vport, char *uuid,
			enum dma_data_direction iodir,
			union lpfc_vmid_io_tag *tag)
{
	struct lpfc_vmid *vmp = NULL;
	int hash, len, rc = -EPERM, i;

	/* check if QFPA is complete */
	if (lpfc_vmid_is_type_priority_tag(vport) &&
	    !(vport->vmid_flag & LPFC_VMID_QFPA_CMPL) &&
	    (vport->vmid_flag & LPFC_VMID_ISSUE_QFPA)) {
		vport->work_port_events |= WORKER_CHECK_VMID_ISSUE_QFPA;
		return -EAGAIN;
	}

	/* search if the UUID has already been mapped to the VMID */
	len = strlen(uuid);
	hash = lpfc_vmid_hash_fn(uuid, len);

	/* search for the VMID in the table */
	read_lock(&vport->vmid_lock);
	vmp = lpfc_get_vmid_from_hashtable(vport, hash, uuid);

	/* if found, check if its already registered  */
	if (vmp  && vmp->flag & LPFC_VMID_REGISTERED) {
		read_unlock(&vport->vmid_lock);
		lpfc_vmid_update_entry(vport, iodir, vmp, tag);
		rc = 0;
	} else if (vmp && (vmp->flag & LPFC_VMID_REQ_REGISTER ||
			   vmp->flag & LPFC_VMID_DE_REGISTER)) {
		/* else if register or dereg request has already been sent */
		/* Hence VMID tag will not be added for this I/O */
		read_unlock(&vport->vmid_lock);
		rc = -EBUSY;
	} else {
		/* The VMID was not found in the hashtable. At this point, */
		/* drop the read lock first before proceeding further */
		read_unlock(&vport->vmid_lock);
		/* start the process to obtain one as per the */
		/* type of the VMID indicated */
		write_lock(&vport->vmid_lock);
		vmp = lpfc_get_vmid_from_hashtable(vport, hash, uuid);

		/* while the read lock was released, in case the entry was */
		/* added by other context or is in process of being added */
		if (vmp && vmp->flag & LPFC_VMID_REGISTERED) {
			lpfc_vmid_update_entry(vport, iodir, vmp, tag);
			write_unlock(&vport->vmid_lock);
			return 0;
		} else if (vmp && vmp->flag & LPFC_VMID_REQ_REGISTER) {
			write_unlock(&vport->vmid_lock);
			return -EBUSY;
		}

		/* else search and allocate a free slot in the hash table */
		if (vport->cur_vmid_cnt < vport->max_vmid) {
			for (i = 0; i < vport->max_vmid; i++) {
				vmp = vport->vmid + i;
				if (vmp->flag == LPFC_VMID_SLOT_FREE)
					break;
			}
			if (i == vport->max_vmid)
				vmp = NULL;
		} else {
			vmp = NULL;
		}

		if (!vmp) {
			write_unlock(&vport->vmid_lock);
			return -ENOMEM;
		}

		/* Add the vmid and register */
		lpfc_put_vmid_in_hashtable(vport, hash, vmp);
		vmp->vmid_len = len;
		memcpy(vmp->host_vmid, uuid, vmp->vmid_len);
		vmp->io_rd_cnt = 0;
		vmp->io_wr_cnt = 0;
		vmp->flag = LPFC_VMID_SLOT_USED;

		vmp->delete_inactive =
			vport->vmid_inactivity_timeout ? 1 : 0;

		/* if type priority tag, get next available VMID */
		if (vport->phba->pport->vmid_flag & LPFC_VMID_TYPE_PRIO)
			lpfc_vmid_assign_cs_ctl(vport, vmp);

		/* allocate the per cpu variable for holding */
		/* the last access time stamp only if VMID is enabled */
		if (!vmp->last_io_time)
			vmp->last_io_time = alloc_percpu_gfp(u64, GFP_ATOMIC);
		if (!vmp->last_io_time) {
			hash_del(&vmp->hnode);
			vmp->flag = LPFC_VMID_SLOT_FREE;
			write_unlock(&vport->vmid_lock);
			return -EIO;
		}

		write_unlock(&vport->vmid_lock);

		/* complete transaction with switch */
		if (vport->phba->pport->vmid_flag & LPFC_VMID_TYPE_PRIO)
			rc = lpfc_vmid_uvem(vport, vmp, true);
		else if (vport->phba->cfg_vmid_app_header)
			rc = lpfc_vmid_cmd(vport, SLI_CTAS_RAPP_IDENT, vmp);
		if (!rc) {
			write_lock(&vport->vmid_lock);
			vport->cur_vmid_cnt++;
			vmp->flag |= LPFC_VMID_REQ_REGISTER;
			write_unlock(&vport->vmid_lock);
		} else {
			write_lock(&vport->vmid_lock);
			hash_del(&vmp->hnode);
			vmp->flag = LPFC_VMID_SLOT_FREE;
			free_percpu(vmp->last_io_time);
			write_unlock(&vport->vmid_lock);
			return -EIO;
		}

		/* finally, enable the idle timer once */
		if (!(vport->phba->pport->vmid_flag & LPFC_VMID_TIMER_ENBLD)) {
			mod_timer(&vport->phba->inactive_vmid_poll,
				  jiffies +
				  msecs_to_jiffies(1000 * LPFC_VMID_TIMER));
			vport->phba->pport->vmid_flag |= LPFC_VMID_TIMER_ENBLD;
		}
	}
	return rc;
}

/*
 * lpfc_reinit_vmid - reinitializes the vmid data structure
 * @vport: pointer to vport data structure
 *
 * This routine reinitializes the vmid post flogi completion
 *
 * Return codes
 *	None
 */
void
lpfc_reinit_vmid(struct lpfc_vport *vport)
{
	u32 bucket, i, cpu;
	struct lpfc_vmid *cur;
	struct lpfc_vmid *vmp = NULL;
	struct hlist_node *tmp;

	write_lock(&vport->vmid_lock);
	vport->cur_vmid_cnt = 0;

	for (i = 0; i < vport->max_vmid; i++) {
		vmp = &vport->vmid[i];
		vmp->flag = LPFC_VMID_SLOT_FREE;
		memset(vmp->host_vmid, 0, sizeof(vmp->host_vmid));
		vmp->io_rd_cnt = 0;
		vmp->io_wr_cnt = 0;

		if (vmp->last_io_time)
			for_each_possible_cpu(cpu)
				*per_cpu_ptr(vmp->last_io_time, cpu) = 0;
	}

	/* for all elements in the hash table */
	if (!hash_empty(vport->hash_table))
		hash_for_each_safe(vport->hash_table, bucket, tmp, cur, hnode)
			hash_del(&cur->hnode);
	vport->vmid_flag = 0;
	write_unlock(&vport->vmid_lock);
}
