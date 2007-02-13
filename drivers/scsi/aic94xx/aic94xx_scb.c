/*
 * Aic94xx SAS/SATA driver SCB management.
 *
 * Copyright (C) 2005 Adaptec, Inc.  All rights reserved.
 * Copyright (C) 2005 Luben Tuikov <luben_tuikov@adaptec.com>
 *
 * This file is licensed under GPLv2.
 *
 * This file is part of the aic94xx driver.
 *
 * The aic94xx driver is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2 of the
 * License.
 *
 * The aic94xx driver is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with the aic94xx driver; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <linux/pci.h>
#include <scsi/scsi_host.h>

#include "aic94xx.h"
#include "aic94xx_reg.h"
#include "aic94xx_hwi.h"
#include "aic94xx_seq.h"

#include "aic94xx_dump.h"

/* ---------- EMPTY SCB ---------- */

#define DL_PHY_MASK      7
#define BYTES_DMAED      0
#define PRIMITIVE_RECVD  0x08
#define PHY_EVENT        0x10
#define LINK_RESET_ERROR 0x18
#define TIMER_EVENT      0x20
#define REQ_TASK_ABORT   0xF0
#define REQ_DEVICE_RESET 0xF1
#define SIGNAL_NCQ_ERROR 0xF2
#define CLEAR_NCQ_ERROR  0xF3

#define PHY_EVENTS_STATUS (CURRENT_LOSS_OF_SIGNAL | CURRENT_OOB_DONE   \
			   | CURRENT_SPINUP_HOLD | CURRENT_GTO_TIMEOUT \
			   | CURRENT_OOB_ERROR)

static inline void get_lrate_mode(struct asd_phy *phy, u8 oob_mode)
{
	struct sas_phy *sas_phy = phy->sas_phy.phy;

	switch (oob_mode & 7) {
	case PHY_SPEED_60:
		/* FIXME: sas transport class doesn't have this */
		phy->sas_phy.linkrate = SAS_LINK_RATE_6_0_GBPS;
		phy->sas_phy.phy->negotiated_linkrate = SAS_LINK_RATE_6_0_GBPS;
		break;
	case PHY_SPEED_30:
		phy->sas_phy.linkrate = SAS_LINK_RATE_3_0_GBPS;
		phy->sas_phy.phy->negotiated_linkrate = SAS_LINK_RATE_3_0_GBPS;
		break;
	case PHY_SPEED_15:
		phy->sas_phy.linkrate = SAS_LINK_RATE_1_5_GBPS;
		phy->sas_phy.phy->negotiated_linkrate = SAS_LINK_RATE_1_5_GBPS;
		break;
	}
	sas_phy->negotiated_linkrate = phy->sas_phy.linkrate;
	sas_phy->maximum_linkrate_hw = SAS_LINK_RATE_3_0_GBPS;
	sas_phy->minimum_linkrate_hw = SAS_LINK_RATE_1_5_GBPS;
	sas_phy->maximum_linkrate = phy->phy_desc->max_sas_lrate;
	sas_phy->minimum_linkrate = phy->phy_desc->min_sas_lrate;

	if (oob_mode & SAS_MODE)
		phy->sas_phy.oob_mode = SAS_OOB_MODE;
	else if (oob_mode & SATA_MODE)
		phy->sas_phy.oob_mode = SATA_OOB_MODE;
}

static inline void asd_phy_event_tasklet(struct asd_ascb *ascb,
					 struct done_list_struct *dl)
{
	struct asd_ha_struct *asd_ha = ascb->ha;
	struct sas_ha_struct *sas_ha = &asd_ha->sas_ha;
	int phy_id = dl->status_block[0] & DL_PHY_MASK;
	struct asd_phy *phy = &asd_ha->phys[phy_id];

	u8 oob_status = dl->status_block[1] & PHY_EVENTS_STATUS;
	u8 oob_mode   = dl->status_block[2];

	switch (oob_status) {
	case CURRENT_LOSS_OF_SIGNAL:
		/* directly attached device was removed */
		ASD_DPRINTK("phy%d: device unplugged\n", phy_id);
		asd_turn_led(asd_ha, phy_id, 0);
		sas_phy_disconnected(&phy->sas_phy);
		sas_ha->notify_phy_event(&phy->sas_phy, PHYE_LOSS_OF_SIGNAL);
		break;
	case CURRENT_OOB_DONE:
		/* hot plugged device */
		asd_turn_led(asd_ha, phy_id, 1);
		get_lrate_mode(phy, oob_mode);
		ASD_DPRINTK("phy%d device plugged: lrate:0x%x, proto:0x%x\n",
			    phy_id, phy->sas_phy.linkrate, phy->sas_phy.iproto);
		sas_ha->notify_phy_event(&phy->sas_phy, PHYE_OOB_DONE);
		break;
	case CURRENT_SPINUP_HOLD:
		/* hot plug SATA, no COMWAKE sent */
		asd_turn_led(asd_ha, phy_id, 1);
		sas_ha->notify_phy_event(&phy->sas_phy, PHYE_SPINUP_HOLD);
		break;
	case CURRENT_GTO_TIMEOUT:
	case CURRENT_OOB_ERROR:
		ASD_DPRINTK("phy%d error while OOB: oob status:0x%x\n", phy_id,
			    dl->status_block[1]);
		asd_turn_led(asd_ha, phy_id, 0);
		sas_phy_disconnected(&phy->sas_phy);
		sas_ha->notify_phy_event(&phy->sas_phy, PHYE_OOB_ERROR);
		break;
	}
}

/* If phys are enabled sparsely, this will do the right thing. */
static inline unsigned ord_phy(struct asd_ha_struct *asd_ha,
			       struct asd_phy *phy)
{
	u8 enabled_mask = asd_ha->hw_prof.enabled_phys;
	int i, k = 0;

	for_each_phy(enabled_mask, enabled_mask, i) {
		if (&asd_ha->phys[i] == phy)
			return k;
		k++;
	}
	return 0;
}

/**
 * asd_get_attached_sas_addr -- extract/generate attached SAS address
 * phy: pointer to asd_phy
 * sas_addr: pointer to buffer where the SAS address is to be written
 *
 * This function extracts the SAS address from an IDENTIFY frame
 * received.  If OOB is SATA, then a SAS address is generated from the
 * HA tables.
 *
 * LOCKING: the frame_rcvd_lock needs to be held since this parses the frame
 * buffer.
 */
static inline void asd_get_attached_sas_addr(struct asd_phy *phy, u8 *sas_addr)
{
	if (phy->sas_phy.frame_rcvd[0] == 0x34
	    && phy->sas_phy.oob_mode == SATA_OOB_MODE) {
		struct asd_ha_struct *asd_ha = phy->sas_phy.ha->lldd_ha;
		/* FIS device-to-host */
		u64 addr = be64_to_cpu(*(__be64 *)phy->phy_desc->sas_addr);

		addr += asd_ha->hw_prof.sata_name_base + ord_phy(asd_ha, phy);
		*(__be64 *)sas_addr = cpu_to_be64(addr);
	} else {
		struct sas_identify_frame *idframe =
			(void *) phy->sas_phy.frame_rcvd;
		memcpy(sas_addr, idframe->sas_addr, SAS_ADDR_SIZE);
	}
}

static void asd_form_port(struct asd_ha_struct *asd_ha, struct asd_phy *phy)
{
	int i;
	struct asd_port *free_port = NULL;
	struct asd_port *port;
	struct asd_sas_phy *sas_phy = &phy->sas_phy;
	unsigned long flags;

	spin_lock_irqsave(&asd_ha->asd_ports_lock, flags);
	if (!phy->asd_port) {
		for (i = 0; i < ASD_MAX_PHYS; i++) {
			port = &asd_ha->asd_ports[i];

			/* Check for wide port */
			if (port->num_phys > 0 &&
			    memcmp(port->sas_addr, sas_phy->sas_addr,
				   SAS_ADDR_SIZE) == 0 &&
			    memcmp(port->attached_sas_addr,
				   sas_phy->attached_sas_addr,
				   SAS_ADDR_SIZE) == 0) {
				break;
			}

			/* Find a free port */
			if (port->num_phys == 0 && free_port == NULL) {
				free_port = port;
			}
		}

		/* Use a free port if this doesn't form a wide port */
		if (i >= ASD_MAX_PHYS) {
			port = free_port;
			BUG_ON(!port);
			memcpy(port->sas_addr, sas_phy->sas_addr,
			       SAS_ADDR_SIZE);
			memcpy(port->attached_sas_addr,
			       sas_phy->attached_sas_addr,
			       SAS_ADDR_SIZE);
		}
		port->num_phys++;
		port->phy_mask |= (1U << sas_phy->id);
		phy->asd_port = port;
	}
	ASD_DPRINTK("%s: updating phy_mask 0x%x for phy%d\n",
		    __FUNCTION__, phy->asd_port->phy_mask, sas_phy->id);
	asd_update_port_links(asd_ha, phy);
	spin_unlock_irqrestore(&asd_ha->asd_ports_lock, flags);
}

static void asd_deform_port(struct asd_ha_struct *asd_ha, struct asd_phy *phy)
{
	struct asd_port *port = phy->asd_port;
	struct asd_sas_phy *sas_phy = &phy->sas_phy;
	unsigned long flags;

	spin_lock_irqsave(&asd_ha->asd_ports_lock, flags);
	if (port) {
		port->num_phys--;
		port->phy_mask &= ~(1U << sas_phy->id);
		phy->asd_port = NULL;
	}
	spin_unlock_irqrestore(&asd_ha->asd_ports_lock, flags);
}

static inline void asd_bytes_dmaed_tasklet(struct asd_ascb *ascb,
					   struct done_list_struct *dl,
					   int edb_id, int phy_id)
{
	unsigned long flags;
	int edb_el = edb_id + ascb->edb_index;
	struct asd_dma_tok *edb = ascb->ha->seq.edb_arr[edb_el];
	struct asd_phy *phy = &ascb->ha->phys[phy_id];
	struct sas_ha_struct *sas_ha = phy->sas_phy.ha;
	u16 size = ((dl->status_block[3] & 7) << 8) | dl->status_block[2];

	size = min(size, (u16) sizeof(phy->frame_rcvd));

	spin_lock_irqsave(&phy->sas_phy.frame_rcvd_lock, flags);
	memcpy(phy->sas_phy.frame_rcvd, edb->vaddr, size);
	phy->sas_phy.frame_rcvd_size = size;
	asd_get_attached_sas_addr(phy, phy->sas_phy.attached_sas_addr);
	spin_unlock_irqrestore(&phy->sas_phy.frame_rcvd_lock, flags);
	asd_dump_frame_rcvd(phy, dl);
	asd_form_port(ascb->ha, phy);
	sas_ha->notify_port_event(&phy->sas_phy, PORTE_BYTES_DMAED);
}

static inline void asd_link_reset_err_tasklet(struct asd_ascb *ascb,
					      struct done_list_struct *dl,
					      int phy_id)
{
	struct asd_ha_struct *asd_ha = ascb->ha;
	struct sas_ha_struct *sas_ha = &asd_ha->sas_ha;
	struct asd_sas_phy *sas_phy = sas_ha->sas_phy[phy_id];
	struct asd_phy *phy = &asd_ha->phys[phy_id];
	u8 lr_error = dl->status_block[1];
	u8 retries_left = dl->status_block[2];

	switch (lr_error) {
	case 0:
		ASD_DPRINTK("phy%d: Receive ID timer expired\n", phy_id);
		break;
	case 1:
		ASD_DPRINTK("phy%d: Loss of signal\n", phy_id);
		break;
	case 2:
		ASD_DPRINTK("phy%d: Loss of dword sync\n", phy_id);
		break;
	case 3:
		ASD_DPRINTK("phy%d: Receive FIS timeout\n", phy_id);
		break;
	default:
		ASD_DPRINTK("phy%d: unknown link reset error code: 0x%x\n",
			    phy_id, lr_error);
		break;
	}

	asd_turn_led(asd_ha, phy_id, 0);
	sas_phy_disconnected(sas_phy);
	asd_deform_port(asd_ha, phy);
	sas_ha->notify_port_event(sas_phy, PORTE_LINK_RESET_ERR);

	if (retries_left == 0) {
		int num = 1;
		struct asd_ascb *cp = asd_ascb_alloc_list(ascb->ha, &num,
							  GFP_ATOMIC);
		if (!cp) {
			asd_printk("%s: out of memory\n", __FUNCTION__);
			goto out;
		}
		ASD_DPRINTK("phy%d: retries:0 performing link reset seq\n",
			    phy_id);
		asd_build_control_phy(cp, phy_id, ENABLE_PHY);
		if (asd_post_ascb_list(ascb->ha, cp, 1) != 0)
			asd_ascb_free(cp);
	}
out:
	;
}

static inline void asd_primitive_rcvd_tasklet(struct asd_ascb *ascb,
					      struct done_list_struct *dl,
					      int phy_id)
{
	unsigned long flags;
	struct sas_ha_struct *sas_ha = &ascb->ha->sas_ha;
	struct asd_sas_phy *sas_phy = sas_ha->sas_phy[phy_id];
	struct asd_ha_struct *asd_ha = ascb->ha;
	struct asd_phy *phy = &asd_ha->phys[phy_id];
	u8  reg  = dl->status_block[1];
	u32 cont = dl->status_block[2] << ((reg & 3)*8);

	reg &= ~3;
	switch (reg) {
	case LmPRMSTAT0BYTE0:
		switch (cont) {
		case LmBROADCH:
		case LmBROADRVCH0:
		case LmBROADRVCH1:
		case LmBROADSES:
			ASD_DPRINTK("phy%d: BROADCAST change received:%d\n",
				    phy_id, cont);
			spin_lock_irqsave(&sas_phy->sas_prim_lock, flags);
			sas_phy->sas_prim = ffs(cont);
			spin_unlock_irqrestore(&sas_phy->sas_prim_lock, flags);
			sas_ha->notify_port_event(sas_phy,PORTE_BROADCAST_RCVD);
			break;

		case LmUNKNOWNP:
			ASD_DPRINTK("phy%d: unknown BREAK\n", phy_id);
			break;

		default:
			ASD_DPRINTK("phy%d: primitive reg:0x%x, cont:0x%04x\n",
				    phy_id, reg, cont);
			break;
		}
		break;
	case LmPRMSTAT1BYTE0:
		switch (cont) {
		case LmHARDRST:
			ASD_DPRINTK("phy%d: HARD_RESET primitive rcvd\n",
				    phy_id);
			/* The sequencer disables all phys on that port.
			 * We have to re-enable the phys ourselves. */
			asd_deform_port(asd_ha, phy);
			sas_ha->notify_port_event(sas_phy, PORTE_HARD_RESET);
			break;

		default:
			ASD_DPRINTK("phy%d: primitive reg:0x%x, cont:0x%04x\n",
				    phy_id, reg, cont);
			break;
		}
		break;
	default:
		ASD_DPRINTK("unknown primitive register:0x%x\n",
			    dl->status_block[1]);
		break;
	}
}

/**
 * asd_invalidate_edb -- invalidate an EDB and if necessary post the ESCB
 * @ascb: pointer to Empty SCB
 * @edb_id: index [0,6] to the empty data buffer which is to be invalidated
 *
 * After an EDB has been invalidated, if all EDBs in this ESCB have been
 * invalidated, the ESCB is posted back to the sequencer.
 * Context is tasklet/IRQ.
 */
void asd_invalidate_edb(struct asd_ascb *ascb, int edb_id)
{
	struct asd_seq_data *seq = &ascb->ha->seq;
	struct empty_scb *escb = &ascb->scb->escb;
	struct sg_el     *eb   = &escb->eb[edb_id];
	struct asd_dma_tok *edb = seq->edb_arr[ascb->edb_index + edb_id];

	memset(edb->vaddr, 0, ASD_EDB_SIZE);
	eb->flags |= ELEMENT_NOT_VALID;
	escb->num_valid--;

	if (escb->num_valid == 0) {
		int i;
		/* ASD_DPRINTK("reposting escb: vaddr: 0x%p, "
			    "dma_handle: 0x%08llx, next: 0x%08llx, "
			    "index:%d, opcode:0x%02x\n",
			    ascb->dma_scb.vaddr,
			    (u64)ascb->dma_scb.dma_handle,
			    le64_to_cpu(ascb->scb->header.next_scb),
			    le16_to_cpu(ascb->scb->header.index),
			    ascb->scb->header.opcode);
		*/
		escb->num_valid = ASD_EDBS_PER_SCB;
		for (i = 0; i < ASD_EDBS_PER_SCB; i++)
			escb->eb[i].flags = 0;
		if (!list_empty(&ascb->list))
			list_del_init(&ascb->list);
		i = asd_post_escb_list(ascb->ha, ascb, 1);
		if (i)
			asd_printk("couldn't post escb, err:%d\n", i);
	}
}

static void escb_tasklet_complete(struct asd_ascb *ascb,
				  struct done_list_struct *dl)
{
	struct asd_ha_struct *asd_ha = ascb->ha;
	struct sas_ha_struct *sas_ha = &asd_ha->sas_ha;
	int edb = (dl->opcode & DL_PHY_MASK) - 1; /* [0xc1,0xc7] -> [0,6] */
	u8  sb_opcode = dl->status_block[0];
	int phy_id = sb_opcode & DL_PHY_MASK;
	struct asd_sas_phy *sas_phy = sas_ha->sas_phy[phy_id];
	struct asd_phy *phy = &asd_ha->phys[phy_id];

	if (edb > 6 || edb < 0) {
		ASD_DPRINTK("edb is 0x%x! dl->opcode is 0x%x\n",
			    edb, dl->opcode);
		ASD_DPRINTK("sb_opcode : 0x%x, phy_id: 0x%x\n",
			    sb_opcode, phy_id);
		ASD_DPRINTK("escb: vaddr: 0x%p, "
			    "dma_handle: 0x%llx, next: 0x%llx, "
			    "index:%d, opcode:0x%02x\n",
			    ascb->dma_scb.vaddr,
			    (unsigned long long)ascb->dma_scb.dma_handle,
			    (unsigned long long)
			    le64_to_cpu(ascb->scb->header.next_scb),
			    le16_to_cpu(ascb->scb->header.index),
			    ascb->scb->header.opcode);
	}

	/* Catch these before we mask off the sb_opcode bits */
	switch (sb_opcode) {
	case REQ_TASK_ABORT: {
		struct asd_ascb *a, *b;
		u16 tc_abort;
		struct domain_device *failed_dev = NULL;

		ASD_DPRINTK("%s: REQ_TASK_ABORT, reason=0x%X\n",
			    __FUNCTION__, dl->status_block[3]);

		/*
		 * Find the task that caused the abort and abort it first.
		 * The sequencer won't put anything on the done list until
		 * that happens.
		 */
		tc_abort = *((u16*)(&dl->status_block[1]));
		tc_abort = le16_to_cpu(tc_abort);

		list_for_each_entry_safe(a, b, &asd_ha->seq.pend_q, list) {
			struct sas_task *task = ascb->uldd_task;

			if (task && a->tc_index == tc_abort) {
				failed_dev = task->dev;
				sas_task_abort(task);
				break;
			}
		}

		if (!failed_dev) {
			ASD_DPRINTK("%s: Can't find task (tc=%d) to abort!\n",
				    __FUNCTION__, tc_abort);
			goto out;
		}

		/*
		 * Now abort everything else for that device (hba?) so
		 * that the EH will wake up and do something.
		 */
		list_for_each_entry_safe(a, b, &asd_ha->seq.pend_q, list) {
			struct sas_task *task = ascb->uldd_task;

			if (task &&
			    task->dev == failed_dev &&
			    a->tc_index != tc_abort)
				sas_task_abort(task);
		}

		goto out;
	}
	case REQ_DEVICE_RESET: {
		struct asd_ascb *a;
		u16 conn_handle;
		unsigned long flags;
		struct sas_task *last_dev_task = NULL;

		conn_handle = *((u16*)(&dl->status_block[1]));
		conn_handle = le16_to_cpu(conn_handle);

		ASD_DPRINTK("%s: REQ_DEVICE_RESET, reason=0x%X\n", __FUNCTION__,
			    dl->status_block[3]);

		/* Find the last pending task for the device... */
		list_for_each_entry(a, &asd_ha->seq.pend_q, list) {
			u16 x;
			struct domain_device *dev;
			struct sas_task *task = a->uldd_task;

			if (!task)
				continue;
			dev = task->dev;

			x = (unsigned long)dev->lldd_dev;
			if (x == conn_handle)
				last_dev_task = task;
		}

		if (!last_dev_task) {
			ASD_DPRINTK("%s: Device reset for idle device %d?\n",
				    __FUNCTION__, conn_handle);
			goto out;
		}

		/* ...and set the reset flag */
		spin_lock_irqsave(&last_dev_task->task_state_lock, flags);
		last_dev_task->task_state_flags |= SAS_TASK_NEED_DEV_RESET;
		spin_unlock_irqrestore(&last_dev_task->task_state_lock, flags);

		/* Kill all pending tasks for the device */
		list_for_each_entry(a, &asd_ha->seq.pend_q, list) {
			u16 x;
			struct domain_device *dev;
			struct sas_task *task = a->uldd_task;

			if (!task)
				continue;
			dev = task->dev;

			x = (unsigned long)dev->lldd_dev;
			if (x == conn_handle)
				sas_task_abort(task);
		}

		goto out;
	}
	case SIGNAL_NCQ_ERROR:
		ASD_DPRINTK("%s: SIGNAL_NCQ_ERROR\n", __FUNCTION__);
		goto out;
	case CLEAR_NCQ_ERROR:
		ASD_DPRINTK("%s: CLEAR_NCQ_ERROR\n", __FUNCTION__);
		goto out;
	}

	sb_opcode &= ~DL_PHY_MASK;

	switch (sb_opcode) {
	case BYTES_DMAED:
		ASD_DPRINTK("%s: phy%d: BYTES_DMAED\n", __FUNCTION__, phy_id);
		asd_bytes_dmaed_tasklet(ascb, dl, edb, phy_id);
		break;
	case PRIMITIVE_RECVD:
		ASD_DPRINTK("%s: phy%d: PRIMITIVE_RECVD\n", __FUNCTION__,
			    phy_id);
		asd_primitive_rcvd_tasklet(ascb, dl, phy_id);
		break;
	case PHY_EVENT:
		ASD_DPRINTK("%s: phy%d: PHY_EVENT\n", __FUNCTION__, phy_id);
		asd_phy_event_tasklet(ascb, dl);
		break;
	case LINK_RESET_ERROR:
		ASD_DPRINTK("%s: phy%d: LINK_RESET_ERROR\n", __FUNCTION__,
			    phy_id);
		asd_link_reset_err_tasklet(ascb, dl, phy_id);
		break;
	case TIMER_EVENT:
		ASD_DPRINTK("%s: phy%d: TIMER_EVENT, lost dw sync\n",
			    __FUNCTION__, phy_id);
		asd_turn_led(asd_ha, phy_id, 0);
		/* the device is gone */
		sas_phy_disconnected(sas_phy);
		asd_deform_port(asd_ha, phy);
		sas_ha->notify_port_event(sas_phy, PORTE_TIMER_EVENT);
		break;
	default:
		ASD_DPRINTK("%s: phy%d: unknown event:0x%x\n", __FUNCTION__,
			    phy_id, sb_opcode);
		ASD_DPRINTK("edb is 0x%x! dl->opcode is 0x%x\n",
			    edb, dl->opcode);
		ASD_DPRINTK("sb_opcode : 0x%x, phy_id: 0x%x\n",
			    sb_opcode, phy_id);
		ASD_DPRINTK("escb: vaddr: 0x%p, "
			    "dma_handle: 0x%llx, next: 0x%llx, "
			    "index:%d, opcode:0x%02x\n",
			    ascb->dma_scb.vaddr,
			    (unsigned long long)ascb->dma_scb.dma_handle,
			    (unsigned long long)
			    le64_to_cpu(ascb->scb->header.next_scb),
			    le16_to_cpu(ascb->scb->header.index),
			    ascb->scb->header.opcode);

		break;
	}
out:
	asd_invalidate_edb(ascb, edb);
}

int asd_init_post_escbs(struct asd_ha_struct *asd_ha)
{
	struct asd_seq_data *seq = &asd_ha->seq;
	int i;

	for (i = 0; i < seq->num_escbs; i++)
		seq->escb_arr[i]->tasklet_complete = escb_tasklet_complete;

	ASD_DPRINTK("posting %d escbs\n", i);
	return asd_post_escb_list(asd_ha, seq->escb_arr[0], seq->num_escbs);
}

/* ---------- CONTROL PHY ---------- */

#define CONTROL_PHY_STATUS (CURRENT_DEVICE_PRESENT | CURRENT_OOB_DONE   \
			    | CURRENT_SPINUP_HOLD | CURRENT_GTO_TIMEOUT \
			    | CURRENT_OOB_ERROR)

/**
 * control_phy_tasklet_complete -- tasklet complete for CONTROL PHY ascb
 * @ascb: pointer to an ascb
 * @dl: pointer to the done list entry
 *
 * This function completes a CONTROL PHY scb and frees the ascb.
 * A note on LEDs:
 *  - an LED blinks if there is IO though it,
 *  - if a device is connected to the LED, it is lit,
 *  - if no device is connected to the LED, is is dimmed (off).
 */
static void control_phy_tasklet_complete(struct asd_ascb *ascb,
					 struct done_list_struct *dl)
{
	struct asd_ha_struct *asd_ha = ascb->ha;
	struct scb *scb = ascb->scb;
	struct control_phy *control_phy = &scb->control_phy;
	u8 phy_id = control_phy->phy_id;
	struct asd_phy *phy = &ascb->ha->phys[phy_id];

	u8 status     = dl->status_block[0];
	u8 oob_status = dl->status_block[1];
	u8 oob_mode   = dl->status_block[2];
	/* u8 oob_signals= dl->status_block[3]; */

	if (status != 0) {
		ASD_DPRINTK("%s: phy%d status block opcode:0x%x\n",
			    __FUNCTION__, phy_id, status);
		goto out;
	}

	switch (control_phy->sub_func) {
	case DISABLE_PHY:
		asd_ha->hw_prof.enabled_phys &= ~(1 << phy_id);
		asd_turn_led(asd_ha, phy_id, 0);
		asd_control_led(asd_ha, phy_id, 0);
		ASD_DPRINTK("%s: disable phy%d\n", __FUNCTION__, phy_id);
		break;

	case ENABLE_PHY:
		asd_control_led(asd_ha, phy_id, 1);
		if (oob_status & CURRENT_OOB_DONE) {
			asd_ha->hw_prof.enabled_phys |= (1 << phy_id);
			get_lrate_mode(phy, oob_mode);
			asd_turn_led(asd_ha, phy_id, 1);
			ASD_DPRINTK("%s: phy%d, lrate:0x%x, proto:0x%x\n",
				    __FUNCTION__, phy_id,phy->sas_phy.linkrate,
				    phy->sas_phy.iproto);
		} else if (oob_status & CURRENT_SPINUP_HOLD) {
			asd_ha->hw_prof.enabled_phys |= (1 << phy_id);
			asd_turn_led(asd_ha, phy_id, 1);
			ASD_DPRINTK("%s: phy%d, spinup hold\n", __FUNCTION__,
				    phy_id);
		} else if (oob_status & CURRENT_ERR_MASK) {
			asd_turn_led(asd_ha, phy_id, 0);
			ASD_DPRINTK("%s: phy%d: error: oob status:0x%02x\n",
				    __FUNCTION__, phy_id, oob_status);
		} else if (oob_status & (CURRENT_HOT_PLUG_CNCT
					 | CURRENT_DEVICE_PRESENT))  {
			asd_ha->hw_prof.enabled_phys |= (1 << phy_id);
			asd_turn_led(asd_ha, phy_id, 1);
			ASD_DPRINTK("%s: phy%d: hot plug or device present\n",
				    __FUNCTION__, phy_id);
		} else {
			asd_ha->hw_prof.enabled_phys |= (1 << phy_id);
			asd_turn_led(asd_ha, phy_id, 0);
			ASD_DPRINTK("%s: phy%d: no device present: "
				    "oob_status:0x%x\n",
				    __FUNCTION__, phy_id, oob_status);
		}
		break;
	case RELEASE_SPINUP_HOLD:
	case PHY_NO_OP:
	case EXECUTE_HARD_RESET:
		ASD_DPRINTK("%s: phy%d: sub_func:0x%x\n", __FUNCTION__,
			    phy_id, control_phy->sub_func);
		/* XXX finish */
		break;
	default:
		ASD_DPRINTK("%s: phy%d: sub_func:0x%x?\n", __FUNCTION__,
			    phy_id, control_phy->sub_func);
		break;
	}
out:
	asd_ascb_free(ascb);
}

static inline void set_speed_mask(u8 *speed_mask, struct asd_phy_desc *pd)
{
	/* disable all speeds, then enable defaults */
	*speed_mask = SAS_SPEED_60_DIS | SAS_SPEED_30_DIS | SAS_SPEED_15_DIS
		| SATA_SPEED_30_DIS | SATA_SPEED_15_DIS;

	switch (pd->max_sas_lrate) {
	case SAS_LINK_RATE_6_0_GBPS:
		*speed_mask &= ~SAS_SPEED_60_DIS;
	default:
	case SAS_LINK_RATE_3_0_GBPS:
		*speed_mask &= ~SAS_SPEED_30_DIS;
	case SAS_LINK_RATE_1_5_GBPS:
		*speed_mask &= ~SAS_SPEED_15_DIS;
	}

	switch (pd->min_sas_lrate) {
	case SAS_LINK_RATE_6_0_GBPS:
		*speed_mask |= SAS_SPEED_30_DIS;
	case SAS_LINK_RATE_3_0_GBPS:
		*speed_mask |= SAS_SPEED_15_DIS;
	default:
	case SAS_LINK_RATE_1_5_GBPS:
		/* nothing to do */
		;
	}

	switch (pd->max_sata_lrate) {
	case SAS_LINK_RATE_3_0_GBPS:
		*speed_mask &= ~SATA_SPEED_30_DIS;
	default:
	case SAS_LINK_RATE_1_5_GBPS:
		*speed_mask &= ~SATA_SPEED_15_DIS;
	}

	switch (pd->min_sata_lrate) {
	case SAS_LINK_RATE_3_0_GBPS:
		*speed_mask |= SATA_SPEED_15_DIS;
	default:
	case SAS_LINK_RATE_1_5_GBPS:
		/* nothing to do */
		;
	}
}

/**
 * asd_build_control_phy -- build a CONTROL PHY SCB
 * @ascb: pointer to an ascb
 * @phy_id: phy id to control, integer
 * @subfunc: subfunction, what to actually to do the phy
 *
 * This function builds a CONTROL PHY scb.  No allocation of any kind
 * is performed. @ascb is allocated with the list function.
 * The caller can override the ascb->tasklet_complete to point
 * to its own callback function.  It must call asd_ascb_free()
 * at its tasklet complete function.
 * See the default implementation.
 */
void asd_build_control_phy(struct asd_ascb *ascb, int phy_id, u8 subfunc)
{
	struct asd_phy *phy = &ascb->ha->phys[phy_id];
	struct scb *scb = ascb->scb;
	struct control_phy *control_phy = &scb->control_phy;

	scb->header.opcode = CONTROL_PHY;
	control_phy->phy_id = (u8) phy_id;
	control_phy->sub_func = subfunc;

	switch (subfunc) {
	case EXECUTE_HARD_RESET:  /* 0x81 */
	case ENABLE_PHY:          /* 0x01 */
		/* decide hot plug delay */
		control_phy->hot_plug_delay = HOTPLUG_DELAY_TIMEOUT;

		/* decide speed mask */
		set_speed_mask(&control_phy->speed_mask, phy->phy_desc);

		/* initiator port settings are in the hi nibble */
		if (phy->sas_phy.role == PHY_ROLE_INITIATOR)
			control_phy->port_type = SAS_PROTO_ALL << 4;
		else if (phy->sas_phy.role == PHY_ROLE_TARGET)
			control_phy->port_type = SAS_PROTO_ALL;
		else
			control_phy->port_type =
				(SAS_PROTO_ALL << 4) | SAS_PROTO_ALL;

		/* link reset retries, this should be nominal */
		control_phy->link_reset_retries = 10;

	case RELEASE_SPINUP_HOLD: /* 0x02 */
		/* decide the func_mask */
		control_phy->func_mask = FUNCTION_MASK_DEFAULT;
		if (phy->phy_desc->flags & ASD_SATA_SPINUP_HOLD)
			control_phy->func_mask &= ~SPINUP_HOLD_DIS;
		else
			control_phy->func_mask |= SPINUP_HOLD_DIS;
	}

	control_phy->conn_handle = cpu_to_le16(0xFFFF);

	ascb->tasklet_complete = control_phy_tasklet_complete;
}

/* ---------- INITIATE LINK ADM TASK ---------- */

static void link_adm_tasklet_complete(struct asd_ascb *ascb,
				      struct done_list_struct *dl)
{
	u8 opcode = dl->opcode;
	struct initiate_link_adm *link_adm = &ascb->scb->link_adm;
	u8 phy_id = link_adm->phy_id;

	if (opcode != TC_NO_ERROR) {
		asd_printk("phy%d: link adm task 0x%x completed with error "
			   "0x%x\n", phy_id, link_adm->sub_func, opcode);
	}
	ASD_DPRINTK("phy%d: link adm task 0x%x: 0x%x\n",
		    phy_id, link_adm->sub_func, opcode);

	asd_ascb_free(ascb);
}

void asd_build_initiate_link_adm_task(struct asd_ascb *ascb, int phy_id,
				      u8 subfunc)
{
	struct scb *scb = ascb->scb;
	struct initiate_link_adm *link_adm = &scb->link_adm;

	scb->header.opcode = INITIATE_LINK_ADM_TASK;

	link_adm->phy_id = phy_id;
	link_adm->sub_func = subfunc;
	link_adm->conn_handle = cpu_to_le16(0xFFFF);

	ascb->tasklet_complete = link_adm_tasklet_complete;
}

/* ---------- SCB timer ---------- */

/**
 * asd_ascb_timedout -- called when a pending SCB's timer has expired
 * @data: unsigned long, a pointer to the ascb in question
 *
 * This is the default timeout function which does the most necessary.
 * Upper layers can implement their own timeout function, say to free
 * resources they have with this SCB, and then call this one at the
 * end of their timeout function.  To do this, one should initialize
 * the ascb->timer.{function, data, expires} prior to calling the post
 * funcion.  The timer is started by the post function.
 */
void asd_ascb_timedout(unsigned long data)
{
	struct asd_ascb *ascb = (void *) data;
	struct asd_seq_data *seq = &ascb->ha->seq;
	unsigned long flags;

	ASD_DPRINTK("scb:0x%x timed out\n", ascb->scb->header.opcode);

	spin_lock_irqsave(&seq->pend_q_lock, flags);
	seq->pending--;
	list_del_init(&ascb->list);
	spin_unlock_irqrestore(&seq->pend_q_lock, flags);

	asd_ascb_free(ascb);
}

/* ---------- CONTROL PHY ---------- */

/* Given the spec value, return a driver value. */
static const int phy_func_table[] = {
	[PHY_FUNC_NOP]        = PHY_NO_OP,
	[PHY_FUNC_LINK_RESET] = ENABLE_PHY,
	[PHY_FUNC_HARD_RESET] = EXECUTE_HARD_RESET,
	[PHY_FUNC_DISABLE]    = DISABLE_PHY,
	[PHY_FUNC_RELEASE_SPINUP_HOLD] = RELEASE_SPINUP_HOLD,
};

int asd_control_phy(struct asd_sas_phy *phy, enum phy_func func, void *arg)
{
	struct asd_ha_struct *asd_ha = phy->ha->lldd_ha;
	struct asd_phy_desc *pd = asd_ha->phys[phy->id].phy_desc;
	struct asd_ascb *ascb;
	struct sas_phy_linkrates *rates;
	int res = 1;

	switch (func) {
	case PHY_FUNC_CLEAR_ERROR_LOG:
		return -ENOSYS;
	case PHY_FUNC_SET_LINK_RATE:
		rates = arg;
		if (rates->minimum_linkrate) {
			pd->min_sas_lrate = rates->minimum_linkrate;
			pd->min_sata_lrate = rates->minimum_linkrate;
		}
		if (rates->maximum_linkrate) {
			pd->max_sas_lrate = rates->maximum_linkrate;
			pd->max_sata_lrate = rates->maximum_linkrate;
		}
		func = PHY_FUNC_LINK_RESET;
		break;
	default:
		break;
	}

	ascb = asd_ascb_alloc_list(asd_ha, &res, GFP_KERNEL);
	if (!ascb)
		return -ENOMEM;

	asd_build_control_phy(ascb, phy->id, phy_func_table[func]);
	res = asd_post_ascb_list(asd_ha, ascb , 1);
	if (res)
		asd_ascb_free(ascb);

	return res;
}
