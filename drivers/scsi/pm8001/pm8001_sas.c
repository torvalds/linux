/*
 * PMC-Sierra PM8001/8081/8088/8089 SAS/SATA based host adapters driver
 *
 * Copyright (c) 2008-2009 USI Co., Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 */

#include <linux/slab.h>
#include "pm8001_sas.h"
#include "pm80xx_tracepoints.h"

/**
 * pm8001_find_tag - from sas task to find out  tag that belongs to this task
 * @task: the task sent to the LLDD
 * @tag: the found tag associated with the task
 */
static int pm8001_find_tag(struct sas_task *task, u32 *tag)
{
	if (task->lldd_task) {
		struct pm8001_ccb_info *ccb;
		ccb = task->lldd_task;
		*tag = ccb->ccb_tag;
		return 1;
	}
	return 0;
}

/**
  * pm8001_tag_free - free the no more needed tag
  * @pm8001_ha: our hba struct
  * @tag: the found tag associated with the task
  */
void pm8001_tag_free(struct pm8001_hba_info *pm8001_ha, u32 tag)
{
	void *bitmap = pm8001_ha->rsvd_tags;
	unsigned long flags;

	if (tag >= PM8001_RESERVE_SLOT)
		return;

	spin_lock_irqsave(&pm8001_ha->bitmap_lock, flags);
	__clear_bit(tag, bitmap);
	spin_unlock_irqrestore(&pm8001_ha->bitmap_lock, flags);
}

/**
  * pm8001_tag_alloc - allocate a empty tag for task used.
  * @pm8001_ha: our hba struct
  * @tag_out: the found empty tag .
  */
int pm8001_tag_alloc(struct pm8001_hba_info *pm8001_ha, u32 *tag_out)
{
	void *bitmap = pm8001_ha->rsvd_tags;
	unsigned long flags;
	unsigned int tag;

	spin_lock_irqsave(&pm8001_ha->bitmap_lock, flags);
	tag = find_first_zero_bit(bitmap, PM8001_RESERVE_SLOT);
	if (tag >= PM8001_RESERVE_SLOT) {
		spin_unlock_irqrestore(&pm8001_ha->bitmap_lock, flags);
		return -SAS_QUEUE_FULL;
	}
	__set_bit(tag, bitmap);
	spin_unlock_irqrestore(&pm8001_ha->bitmap_lock, flags);

	/* reserved tags are in the lower region of the tagset */
	*tag_out = tag;
	return 0;
}

static void pm80xx_get_tag_opcodes(struct sas_task *task, int *ata_op,
								   int *ata_tag, bool *task_aborted)
{
	unsigned long flags;
	struct ata_queued_cmd *qc = NULL;

	*ata_op = 0;
	*ata_tag = -1;
	*task_aborted = false;

	if (!task)
		return;

	spin_lock_irqsave(&task->task_state_lock, flags);
	if (unlikely((task->task_state_flags & SAS_TASK_STATE_ABORTED)))
		*task_aborted = true;
	spin_unlock_irqrestore(&task->task_state_lock, flags);

	if (task->task_proto == SAS_PROTOCOL_STP) {
		// sas_ata_qc_issue path uses SAS_PROTOCOL_STP.
		// This only works for scsi + libsas + libata users.
		qc = task->uldd_task;
		if (qc) {
			*ata_op = qc->tf.command;
			*ata_tag = qc->tag;
		}
	}
}

void pm80xx_show_pending_commands(struct pm8001_hba_info *pm8001_ha,
				  struct pm8001_device *target_pm8001_dev)
{
	int i = 0, ata_op = 0, ata_tag = -1;
	struct pm8001_ccb_info *ccb = NULL;
	struct sas_task *task = NULL;
	struct pm8001_device *pm8001_dev = NULL;
	bool task_aborted;

	for (i = 0; i < pm8001_ha->ccb_count; i++) {
		ccb = &pm8001_ha->ccb_info[i];
		if (ccb->ccb_tag == PM8001_INVALID_TAG)
			continue;
		pm8001_dev = ccb->device;
		if (target_pm8001_dev && pm8001_dev &&
		    target_pm8001_dev != pm8001_dev)
			continue;
		task = ccb->task;
		pm80xx_get_tag_opcodes(task, &ata_op, &ata_tag, &task_aborted);
		pm8001_dbg(pm8001_ha, FAIL,
			"tag %#x, device %#x task %p task aborted %d ata opcode %#x ata tag %d\n",
			ccb->ccb_tag,
			(pm8001_dev ? pm8001_dev->device_id : 0),
			task, task_aborted,
			ata_op, ata_tag);
	}
}

/**
 * pm8001_mem_alloc - allocate memory for pm8001.
 * @pdev: pci device.
 * @virt_addr: the allocated virtual address
 * @pphys_addr: DMA address for this device
 * @pphys_addr_hi: the physical address high byte address.
 * @pphys_addr_lo: the physical address low byte address.
 * @mem_size: memory size.
 * @align: requested byte alignment
 */
int pm8001_mem_alloc(struct pci_dev *pdev, void **virt_addr,
	dma_addr_t *pphys_addr, u32 *pphys_addr_hi,
	u32 *pphys_addr_lo, u32 mem_size, u32 align)
{
	caddr_t mem_virt_alloc;
	dma_addr_t mem_dma_handle;
	u64 phys_align;
	u64 align_offset = 0;
	if (align)
		align_offset = (dma_addr_t)align - 1;
	mem_virt_alloc = dma_alloc_coherent(&pdev->dev, mem_size + align,
					    &mem_dma_handle, GFP_KERNEL);
	if (!mem_virt_alloc)
		return -ENOMEM;
	*pphys_addr = mem_dma_handle;
	phys_align = (*pphys_addr + align_offset) & ~align_offset;
	*virt_addr = (void *)mem_virt_alloc + phys_align - *pphys_addr;
	*pphys_addr_hi = upper_32_bits(phys_align);
	*pphys_addr_lo = lower_32_bits(phys_align);
	return 0;
}

/**
  * pm8001_find_ha_by_dev - from domain device which come from sas layer to
  * find out our hba struct.
  * @dev: the domain device which from sas layer.
  */
static
struct pm8001_hba_info *pm8001_find_ha_by_dev(struct domain_device *dev)
{
	struct sas_ha_struct *sha = dev->port->ha;
	struct pm8001_hba_info *pm8001_ha = sha->lldd_ha;
	return pm8001_ha;
}

/**
  * pm8001_phy_control - this function should be registered to
  * sas_domain_function_template to provide libsas used, note: this is just
  * control the HBA phy rather than other expander phy if you want control
  * other phy, you should use SMP command.
  * @sas_phy: which phy in HBA phys.
  * @func: the operation.
  * @funcdata: always NULL.
  */
int pm8001_phy_control(struct asd_sas_phy *sas_phy, enum phy_func func,
	void *funcdata)
{
	int rc = 0, phy_id = sas_phy->id;
	struct pm8001_hba_info *pm8001_ha = NULL;
	struct sas_phy_linkrates *rates;
	struct pm8001_phy *phy;
	DECLARE_COMPLETION_ONSTACK(completion);
	unsigned long flags;
	pm8001_ha = sas_phy->ha->lldd_ha;
	phy = &pm8001_ha->phy[phy_id];

	if (PM8001_CHIP_DISP->fatal_errors(pm8001_ha)) {
		/*
		 * If the controller is in fatal error state,
		 * we will not get a response from the controller
		 */
		pm8001_dbg(pm8001_ha, FAIL,
			   "Phy control failed due to fatal errors\n");
		return -EFAULT;
	}

	switch (func) {
	case PHY_FUNC_SET_LINK_RATE:
		rates = funcdata;
		if (rates->minimum_linkrate) {
			pm8001_ha->phy[phy_id].minimum_linkrate =
				rates->minimum_linkrate;
		}
		if (rates->maximum_linkrate) {
			pm8001_ha->phy[phy_id].maximum_linkrate =
				rates->maximum_linkrate;
		}
		if (pm8001_ha->phy[phy_id].phy_state ==  PHY_LINK_DISABLE) {
			pm8001_ha->phy[phy_id].enable_completion = &completion;
			PM8001_CHIP_DISP->phy_start_req(pm8001_ha, phy_id);
			wait_for_completion(&completion);
		}
		PM8001_CHIP_DISP->phy_ctl_req(pm8001_ha, phy_id,
					      PHY_LINK_RESET);
		break;
	case PHY_FUNC_HARD_RESET:
		if (pm8001_ha->phy[phy_id].phy_state == PHY_LINK_DISABLE) {
			pm8001_ha->phy[phy_id].enable_completion = &completion;
			PM8001_CHIP_DISP->phy_start_req(pm8001_ha, phy_id);
			wait_for_completion(&completion);
		}
		PM8001_CHIP_DISP->phy_ctl_req(pm8001_ha, phy_id,
					      PHY_HARD_RESET);
		break;
	case PHY_FUNC_LINK_RESET:
		if (pm8001_ha->phy[phy_id].phy_state == PHY_LINK_DISABLE) {
			pm8001_ha->phy[phy_id].enable_completion = &completion;
			PM8001_CHIP_DISP->phy_start_req(pm8001_ha, phy_id);
			wait_for_completion(&completion);
		}
		PM8001_CHIP_DISP->phy_ctl_req(pm8001_ha, phy_id,
					      PHY_LINK_RESET);
		break;
	case PHY_FUNC_RELEASE_SPINUP_HOLD:
		PM8001_CHIP_DISP->phy_ctl_req(pm8001_ha, phy_id,
					      PHY_LINK_RESET);
		break;
	case PHY_FUNC_DISABLE:
		if (pm8001_ha->chip_id != chip_8001) {
			if (pm8001_ha->phy[phy_id].phy_state ==
				PHY_STATE_LINK_UP_SPCV) {
				sas_phy_disconnected(&phy->sas_phy);
				sas_notify_phy_event(&phy->sas_phy,
					PHYE_LOSS_OF_SIGNAL, GFP_KERNEL);
				phy->phy_attached = 0;
			}
		} else {
			if (pm8001_ha->phy[phy_id].phy_state ==
				PHY_STATE_LINK_UP_SPC) {
				sas_phy_disconnected(&phy->sas_phy);
				sas_notify_phy_event(&phy->sas_phy,
					PHYE_LOSS_OF_SIGNAL, GFP_KERNEL);
				phy->phy_attached = 0;
			}
		}
		PM8001_CHIP_DISP->phy_stop_req(pm8001_ha, phy_id);
		break;
	case PHY_FUNC_GET_EVENTS:
		spin_lock_irqsave(&pm8001_ha->lock, flags);
		if (pm8001_ha->chip_id == chip_8001) {
			if (-1 == pm8001_bar4_shift(pm8001_ha,
					(phy_id < 4) ? 0x30000 : 0x40000)) {
				spin_unlock_irqrestore(&pm8001_ha->lock, flags);
				return -EINVAL;
			}
		}
		{
			struct sas_phy *phy = sas_phy->phy;
			u32 __iomem *qp = pm8001_ha->io_mem[2].memvirtaddr
				+ 0x1034 + (0x4000 * (phy_id & 3));

			phy->invalid_dword_count = readl(qp);
			phy->running_disparity_error_count = readl(&qp[1]);
			phy->loss_of_dword_sync_count = readl(&qp[3]);
			phy->phy_reset_problem_count = readl(&qp[4]);
		}
		if (pm8001_ha->chip_id == chip_8001)
			pm8001_bar4_shift(pm8001_ha, 0);
		spin_unlock_irqrestore(&pm8001_ha->lock, flags);
		return 0;
	default:
		pm8001_dbg(pm8001_ha, DEVIO, "func 0x%x\n", func);
		rc = -EOPNOTSUPP;
	}
	msleep(300);
	return rc;
}

/**
  * pm8001_scan_start - we should enable all HBA phys by sending the phy_start
  * command to HBA.
  * @shost: the scsi host data.
  */
void pm8001_scan_start(struct Scsi_Host *shost)
{
	int i;
	struct pm8001_hba_info *pm8001_ha;
	struct sas_ha_struct *sha = SHOST_TO_SAS_HA(shost);
	DECLARE_COMPLETION_ONSTACK(completion);
	pm8001_ha = sha->lldd_ha;
	/* SAS_RE_INITIALIZATION not available in SPCv/ve */
	if (pm8001_ha->chip_id == chip_8001)
		PM8001_CHIP_DISP->sas_re_init_req(pm8001_ha);
	for (i = 0; i < pm8001_ha->chip->n_phy; ++i) {
		pm8001_ha->phy[i].enable_completion = &completion;
		PM8001_CHIP_DISP->phy_start_req(pm8001_ha, i);
		wait_for_completion(&completion);
		msleep(300);
	}
}

int pm8001_scan_finished(struct Scsi_Host *shost, unsigned long time)
{
	struct sas_ha_struct *ha = SHOST_TO_SAS_HA(shost);

	/* give the phy enabling interrupt event time to come in (1s
	* is empirically about all it takes) */
	if (time < HZ)
		return 0;
	/* Wait for discovery to finish */
	sas_drain_work(ha);
	return 1;
}

/**
  * pm8001_task_prep_smp - the dispatcher function, prepare data for smp task
  * @pm8001_ha: our hba card information
  * @ccb: the ccb which attached to smp task
  */
static int pm8001_task_prep_smp(struct pm8001_hba_info *pm8001_ha,
	struct pm8001_ccb_info *ccb)
{
	return PM8001_CHIP_DISP->smp_req(pm8001_ha, ccb);
}

u32 pm8001_get_ncq_tag(struct sas_task *task, u32 *tag)
{
	struct ata_queued_cmd *qc = task->uldd_task;

	if (qc && ata_is_ncq(qc->tf.protocol)) {
		*tag = qc->tag;
		return 1;
	}

	return 0;
}

/**
  * pm8001_task_prep_ata - the dispatcher function, prepare data for sata task
  * @pm8001_ha: our hba card information
  * @ccb: the ccb which attached to sata task
  */
static int pm8001_task_prep_ata(struct pm8001_hba_info *pm8001_ha,
	struct pm8001_ccb_info *ccb)
{
	return PM8001_CHIP_DISP->sata_req(pm8001_ha, ccb);
}

/**
  * pm8001_task_prep_internal_abort - the dispatcher function, prepare data
  *				      for internal abort task
  * @pm8001_ha: our hba card information
  * @ccb: the ccb which attached to sata task
  */
static int pm8001_task_prep_internal_abort(struct pm8001_hba_info *pm8001_ha,
					   struct pm8001_ccb_info *ccb)
{
	return PM8001_CHIP_DISP->task_abort(pm8001_ha, ccb);
}

/**
  * pm8001_task_prep_ssp_tm - the dispatcher function, prepare task management data
  * @pm8001_ha: our hba card information
  * @ccb: the ccb which attached to TM
  * @tmf: the task management IU
  */
static int pm8001_task_prep_ssp_tm(struct pm8001_hba_info *pm8001_ha,
	struct pm8001_ccb_info *ccb, struct sas_tmf_task *tmf)
{
	return PM8001_CHIP_DISP->ssp_tm_req(pm8001_ha, ccb, tmf);
}

/**
  * pm8001_task_prep_ssp - the dispatcher function, prepare ssp data for ssp task
  * @pm8001_ha: our hba card information
  * @ccb: the ccb which attached to ssp task
  */
static int pm8001_task_prep_ssp(struct pm8001_hba_info *pm8001_ha,
	struct pm8001_ccb_info *ccb)
{
	return PM8001_CHIP_DISP->ssp_io_req(pm8001_ha, ccb);
}

#define DEV_IS_GONE(pm8001_dev)	\
	((!pm8001_dev || (pm8001_dev->dev_type == SAS_PHY_UNUSED)))


static int pm8001_deliver_command(struct pm8001_hba_info *pm8001_ha,
				  struct pm8001_ccb_info *ccb)
{
	struct sas_task *task = ccb->task;
	enum sas_protocol task_proto = task->task_proto;
	struct sas_tmf_task *tmf = task->tmf;
	int is_tmf = !!tmf;

	switch (task_proto) {
	case SAS_PROTOCOL_SMP:
		return pm8001_task_prep_smp(pm8001_ha, ccb);
	case SAS_PROTOCOL_SSP:
		if (is_tmf)
			return pm8001_task_prep_ssp_tm(pm8001_ha, ccb, tmf);
		return pm8001_task_prep_ssp(pm8001_ha, ccb);
	case SAS_PROTOCOL_SATA:
	case SAS_PROTOCOL_STP:
		return pm8001_task_prep_ata(pm8001_ha, ccb);
	case SAS_PROTOCOL_INTERNAL_ABORT:
		return pm8001_task_prep_internal_abort(pm8001_ha, ccb);
	default:
		dev_err(pm8001_ha->dev, "unknown sas_task proto: 0x%x\n",
			task_proto);
	}

	return -EINVAL;
}

/**
  * pm8001_queue_command - register for upper layer used, all IO commands sent
  * to HBA are from this interface.
  * @task: the task to be execute.
  * @gfp_flags: gfp_flags
  */
int pm8001_queue_command(struct sas_task *task, gfp_t gfp_flags)
{
	struct task_status_struct *ts = &task->task_status;
	enum sas_protocol task_proto = task->task_proto;
	struct domain_device *dev = task->dev;
	struct pm8001_device *pm8001_dev = dev->lldd_dev;
	bool internal_abort = sas_is_internal_abort(task);
	struct pm8001_hba_info *pm8001_ha;
	struct pm8001_port *port = NULL;
	struct pm8001_ccb_info *ccb;
	unsigned long flags;
	u32 n_elem = 0;
	int rc = 0;

	if (!internal_abort && !dev->port) {
		ts->resp = SAS_TASK_UNDELIVERED;
		ts->stat = SAS_PHY_DOWN;
		if (dev->dev_type != SAS_SATA_DEV)
			task->task_done(task);
		return 0;
	}

	pm8001_ha = pm8001_find_ha_by_dev(dev);
	if (pm8001_ha->controller_fatal_error) {
		ts->resp = SAS_TASK_UNDELIVERED;
		task->task_done(task);
		return 0;
	}

	pm8001_dbg(pm8001_ha, IO, "pm8001_task_exec device\n");

	spin_lock_irqsave(&pm8001_ha->lock, flags);

	pm8001_dev = dev->lldd_dev;
	port = pm8001_ha->phy[pm8001_dev->attached_phy].port;

	if (!internal_abort &&
	    (DEV_IS_GONE(pm8001_dev) || !port || !port->port_attached)) {
		ts->resp = SAS_TASK_UNDELIVERED;
		ts->stat = SAS_PHY_DOWN;
		if (sas_protocol_ata(task_proto)) {
			spin_unlock_irqrestore(&pm8001_ha->lock, flags);
			task->task_done(task);
			spin_lock_irqsave(&pm8001_ha->lock, flags);
		} else {
			task->task_done(task);
		}
		rc = -ENODEV;
		goto err_out;
	}

	ccb = pm8001_ccb_alloc(pm8001_ha, pm8001_dev, task);
	if (!ccb) {
		rc = -SAS_QUEUE_FULL;
		goto err_out;
	}

	if (!sas_protocol_ata(task_proto)) {
		if (task->num_scatter) {
			n_elem = dma_map_sg(pm8001_ha->dev, task->scatter,
					    task->num_scatter, task->data_dir);
			if (!n_elem) {
				rc = -ENOMEM;
				goto err_out_ccb;
			}
		}
	} else {
		n_elem = task->num_scatter;
	}

	task->lldd_task = ccb;
	ccb->n_elem = n_elem;

	atomic_inc(&pm8001_dev->running_req);

	rc = pm8001_deliver_command(pm8001_ha, ccb);
	if (rc) {
		atomic_dec(&pm8001_dev->running_req);
		if (!sas_protocol_ata(task_proto) && n_elem)
			dma_unmap_sg(pm8001_ha->dev, task->scatter,
				     task->num_scatter, task->data_dir);
err_out_ccb:
		pm8001_ccb_free(pm8001_ha, ccb);

err_out:
		pm8001_dbg(pm8001_ha, IO, "pm8001_task_exec failed[%d]!\n", rc);
	}

	spin_unlock_irqrestore(&pm8001_ha->lock, flags);

	return rc;
}

/**
  * pm8001_ccb_task_free - free the sg for ssp and smp command, free the ccb.
  * @pm8001_ha: our hba card information
  * @ccb: the ccb which attached to ssp task to free
  */
void pm8001_ccb_task_free(struct pm8001_hba_info *pm8001_ha,
			  struct pm8001_ccb_info *ccb)
{
	struct sas_task *task = ccb->task;
	struct ata_queued_cmd *qc;
	struct pm8001_device *pm8001_dev;

	if (!task)
		return;

	if (!sas_protocol_ata(task->task_proto) && ccb->n_elem)
		dma_unmap_sg(pm8001_ha->dev, task->scatter,
			     task->num_scatter, task->data_dir);

	switch (task->task_proto) {
	case SAS_PROTOCOL_SMP:
		dma_unmap_sg(pm8001_ha->dev, &task->smp_task.smp_resp, 1,
			DMA_FROM_DEVICE);
		dma_unmap_sg(pm8001_ha->dev, &task->smp_task.smp_req, 1,
			DMA_TO_DEVICE);
		break;

	case SAS_PROTOCOL_SATA:
	case SAS_PROTOCOL_STP:
	case SAS_PROTOCOL_SSP:
	default:
		/* do nothing */
		break;
	}

	if (sas_protocol_ata(task->task_proto)) {
		/* For SCSI/ATA commands uldd_task points to ata_queued_cmd */
		qc = task->uldd_task;
		pm8001_dev = ccb->device;
		trace_pm80xx_request_complete(pm8001_ha->id,
			pm8001_dev ? pm8001_dev->attached_phy : PM8001_MAX_PHYS,
			ccb->ccb_tag, 0 /* ctlr_opcode not known */,
			qc ? qc->tf.command : 0, // ata opcode
			pm8001_dev ? atomic_read(&pm8001_dev->running_req) : -1);
	}

	task->lldd_task = NULL;
	pm8001_ccb_free(pm8001_ha, ccb);
}

static void pm8001_init_dev(struct pm8001_device *pm8001_dev, int id)
{
	pm8001_dev->id = id;
	pm8001_dev->device_id = PM8001_MAX_DEVICES;
	atomic_set(&pm8001_dev->running_req, 0);
}

/**
 * pm8001_alloc_dev - find a empty pm8001_device
 * @pm8001_ha: our hba card information
 */
static struct pm8001_device *pm8001_alloc_dev(struct pm8001_hba_info *pm8001_ha)
{
	u32 dev;
	for (dev = 0; dev < PM8001_MAX_DEVICES; dev++) {
		struct pm8001_device *pm8001_dev = &pm8001_ha->devices[dev];

		if (pm8001_dev->dev_type == SAS_PHY_UNUSED) {
			pm8001_init_dev(pm8001_dev, dev);
			return pm8001_dev;
		}
	}
	if (dev == PM8001_MAX_DEVICES) {
		pm8001_dbg(pm8001_ha, FAIL,
			   "max support %d devices, ignore ..\n",
			   PM8001_MAX_DEVICES);
	}
	return NULL;
}
/**
  * pm8001_find_dev - find a matching pm8001_device
  * @pm8001_ha: our hba card information
  * @device_id: device ID to match against
  */
struct pm8001_device *pm8001_find_dev(struct pm8001_hba_info *pm8001_ha,
					u32 device_id)
{
	u32 dev;
	for (dev = 0; dev < PM8001_MAX_DEVICES; dev++) {
		if (pm8001_ha->devices[dev].device_id == device_id)
			return &pm8001_ha->devices[dev];
	}
	if (dev == PM8001_MAX_DEVICES) {
		pm8001_dbg(pm8001_ha, FAIL, "NO MATCHING DEVICE FOUND !!!\n");
	}
	return NULL;
}

void pm8001_free_dev(struct pm8001_device *pm8001_dev)
{
	memset(pm8001_dev, 0, sizeof(*pm8001_dev));
	pm8001_dev->dev_type = SAS_PHY_UNUSED;
	pm8001_dev->device_id = PM8001_MAX_DEVICES;
	pm8001_dev->sas_device = NULL;
}

/**
  * pm8001_dev_found_notify - libsas notify a device is found.
  * @dev: the device structure which sas layer used.
  *
  * when libsas find a sas domain device, it should tell the LLDD that
  * device is found, and then LLDD register this device to HBA firmware
  * by the command "OPC_INB_REG_DEV", after that the HBA will assign a
  * device ID(according to device's sas address) and returned it to LLDD. From
  * now on, we communicate with HBA FW with the device ID which HBA assigned
  * rather than sas address. it is the necessary step for our HBA but it is
  * the optional for other HBA driver.
  */
static int pm8001_dev_found_notify(struct domain_device *dev)
{
	unsigned long flags = 0;
	int res = 0;
	struct pm8001_hba_info *pm8001_ha = NULL;
	struct domain_device *parent_dev = dev->parent;
	struct pm8001_device *pm8001_device;
	DECLARE_COMPLETION_ONSTACK(completion);
	u32 flag = 0;
	pm8001_ha = pm8001_find_ha_by_dev(dev);
	spin_lock_irqsave(&pm8001_ha->lock, flags);

	pm8001_device = pm8001_alloc_dev(pm8001_ha);
	if (!pm8001_device) {
		res = -1;
		goto found_out;
	}
	pm8001_device->sas_device = dev;
	dev->lldd_dev = pm8001_device;
	pm8001_device->dev_type = dev->dev_type;
	pm8001_device->dcompletion = &completion;
	if (parent_dev && dev_is_expander(parent_dev->dev_type)) {
		int phy_id;

		phy_id = sas_find_attached_phy_id(&parent_dev->ex_dev, dev);
		if (phy_id < 0) {
			pm8001_dbg(pm8001_ha, FAIL,
				   "Error: no attached dev:%016llx at ex:%016llx.\n",
				   SAS_ADDR(dev->sas_addr),
				   SAS_ADDR(parent_dev->sas_addr));
			res = phy_id;
		} else {
			pm8001_device->attached_phy = phy_id;
		}
	} else {
		if (dev->dev_type == SAS_SATA_DEV) {
			pm8001_device->attached_phy =
				dev->rphy->identify.phy_identifier;
			flag = 1; /* directly sata */
		}
	} /*register this device to HBA*/
	pm8001_dbg(pm8001_ha, DISC, "Found device\n");
	PM8001_CHIP_DISP->reg_dev_req(pm8001_ha, pm8001_device, flag);
	spin_unlock_irqrestore(&pm8001_ha->lock, flags);
	wait_for_completion(&completion);
	if (dev->dev_type == SAS_END_DEVICE)
		msleep(50);
	pm8001_ha->flags = PM8001F_RUN_TIME;
	return 0;
found_out:
	spin_unlock_irqrestore(&pm8001_ha->lock, flags);
	return res;
}

int pm8001_dev_found(struct domain_device *dev)
{
	return pm8001_dev_found_notify(dev);
}

#define PM8001_TASK_TIMEOUT 20

/**
  * pm8001_dev_gone_notify - see the comments for "pm8001_dev_found_notify"
  * @dev: the device structure which sas layer used.
  */
static void pm8001_dev_gone_notify(struct domain_device *dev)
{
	unsigned long flags = 0;
	struct pm8001_hba_info *pm8001_ha;
	struct pm8001_device *pm8001_dev = dev->lldd_dev;

	pm8001_ha = pm8001_find_ha_by_dev(dev);
	spin_lock_irqsave(&pm8001_ha->lock, flags);
	if (pm8001_dev) {
		u32 device_id = pm8001_dev->device_id;

		pm8001_dbg(pm8001_ha, DISC, "found dev[%d:%x] is gone.\n",
			   pm8001_dev->device_id, pm8001_dev->dev_type);
		if (atomic_read(&pm8001_dev->running_req)) {
			spin_unlock_irqrestore(&pm8001_ha->lock, flags);
			sas_execute_internal_abort_dev(dev, 0, NULL);
			while (atomic_read(&pm8001_dev->running_req))
				msleep(20);
			spin_lock_irqsave(&pm8001_ha->lock, flags);
		}
		PM8001_CHIP_DISP->dereg_dev_req(pm8001_ha, device_id);
		pm8001_free_dev(pm8001_dev);
	} else {
		pm8001_dbg(pm8001_ha, DISC, "Found dev has gone.\n");
	}
	dev->lldd_dev = NULL;
	spin_unlock_irqrestore(&pm8001_ha->lock, flags);
}

void pm8001_dev_gone(struct domain_device *dev)
{
	pm8001_dev_gone_notify(dev);
}

/* retry commands by ha, by task and/or by device */
void pm8001_open_reject_retry(
	struct pm8001_hba_info *pm8001_ha,
	struct sas_task *task_to_close,
	struct pm8001_device *device_to_close)
{
	int i;
	unsigned long flags;

	if (pm8001_ha == NULL)
		return;

	spin_lock_irqsave(&pm8001_ha->lock, flags);

	for (i = 0; i < PM8001_MAX_CCB; i++) {
		struct sas_task *task;
		struct task_status_struct *ts;
		struct pm8001_device *pm8001_dev;
		unsigned long flags1;
		struct pm8001_ccb_info *ccb = &pm8001_ha->ccb_info[i];

		if (ccb->ccb_tag == PM8001_INVALID_TAG)
			continue;

		pm8001_dev = ccb->device;
		if (!pm8001_dev || (pm8001_dev->dev_type == SAS_PHY_UNUSED))
			continue;
		if (!device_to_close) {
			uintptr_t d = (uintptr_t)pm8001_dev
					- (uintptr_t)&pm8001_ha->devices;
			if (((d % sizeof(*pm8001_dev)) != 0)
			 || ((d / sizeof(*pm8001_dev)) >= PM8001_MAX_DEVICES))
				continue;
		} else if (pm8001_dev != device_to_close)
			continue;
		task = ccb->task;
		if (!task || !task->task_done)
			continue;
		if (task_to_close && (task != task_to_close))
			continue;
		ts = &task->task_status;
		ts->resp = SAS_TASK_COMPLETE;
		/* Force the midlayer to retry */
		ts->stat = SAS_OPEN_REJECT;
		ts->open_rej_reason = SAS_OREJ_RSVD_RETRY;
		if (pm8001_dev)
			atomic_dec(&pm8001_dev->running_req);
		spin_lock_irqsave(&task->task_state_lock, flags1);
		task->task_state_flags &= ~SAS_TASK_STATE_PENDING;
		task->task_state_flags |= SAS_TASK_STATE_DONE;
		if (unlikely((task->task_state_flags
				& SAS_TASK_STATE_ABORTED))) {
			spin_unlock_irqrestore(&task->task_state_lock,
				flags1);
			pm8001_ccb_task_free(pm8001_ha, ccb);
		} else {
			spin_unlock_irqrestore(&task->task_state_lock,
				flags1);
			pm8001_ccb_task_free(pm8001_ha, ccb);
			mb();/* in order to force CPU ordering */
			spin_unlock_irqrestore(&pm8001_ha->lock, flags);
			task->task_done(task);
			spin_lock_irqsave(&pm8001_ha->lock, flags);
		}
	}

	spin_unlock_irqrestore(&pm8001_ha->lock, flags);
}

/**
 * pm8001_I_T_nexus_reset() - reset the initiator/target connection
 * @dev: the device structure for the device to reset.
 *
 * Standard mandates link reset for ATA (type 0) and hard reset for
 * SSP (type 1), only for RECOVERY
 */
int pm8001_I_T_nexus_reset(struct domain_device *dev)
{
	int rc = TMF_RESP_FUNC_FAILED;
	struct pm8001_device *pm8001_dev;
	struct pm8001_hba_info *pm8001_ha;
	struct sas_phy *phy;

	if (!dev || !dev->lldd_dev)
		return -ENODEV;

	pm8001_dev = dev->lldd_dev;
	pm8001_ha = pm8001_find_ha_by_dev(dev);
	phy = sas_get_local_phy(dev);

	if (dev_is_sata(dev)) {
		if (scsi_is_sas_phy_local(phy)) {
			rc = 0;
			goto out;
		}
		rc = sas_phy_reset(phy, 1);
		if (rc) {
			pm8001_dbg(pm8001_ha, EH,
				   "phy reset failed for device %x\n"
				   "with rc %d\n", pm8001_dev->device_id, rc);
			rc = TMF_RESP_FUNC_FAILED;
			goto out;
		}
		msleep(2000);
		rc = sas_execute_internal_abort_dev(dev, 0, NULL);
		if (rc) {
			pm8001_dbg(pm8001_ha, EH, "task abort failed %x\n"
				   "with rc %d\n", pm8001_dev->device_id, rc);
			rc = TMF_RESP_FUNC_FAILED;
		}
	} else {
		rc = sas_phy_reset(phy, 1);
		msleep(2000);
	}
	pm8001_dbg(pm8001_ha, EH, " for device[%x]:rc=%d\n",
		   pm8001_dev->device_id, rc);
 out:
	sas_put_local_phy(phy);
	return rc;
}

/*
* This function handle the IT_NEXUS_XXX event or completion
* status code for SSP/SATA/SMP I/O request.
*/
int pm8001_I_T_nexus_event_handler(struct domain_device *dev)
{
	int rc = TMF_RESP_FUNC_FAILED;
	struct pm8001_device *pm8001_dev;
	struct pm8001_hba_info *pm8001_ha;
	struct sas_phy *phy;

	if (!dev || !dev->lldd_dev)
		return -1;

	pm8001_dev = dev->lldd_dev;
	pm8001_ha = pm8001_find_ha_by_dev(dev);

	pm8001_dbg(pm8001_ha, EH, "I_T_Nexus handler invoked !!\n");

	phy = sas_get_local_phy(dev);

	if (dev_is_sata(dev)) {
		DECLARE_COMPLETION_ONSTACK(completion_setstate);
		if (scsi_is_sas_phy_local(phy)) {
			rc = 0;
			goto out;
		}
		/* send internal ssp/sata/smp abort command to FW */
		sas_execute_internal_abort_dev(dev, 0, NULL);
		msleep(100);

		/* deregister the target device */
		pm8001_dev_gone_notify(dev);
		msleep(200);

		/*send phy reset to hard reset target */
		rc = sas_phy_reset(phy, 1);
		msleep(2000);
		pm8001_dev->setds_completion = &completion_setstate;

		wait_for_completion(&completion_setstate);
	} else {
		/* send internal ssp/sata/smp abort command to FW */
		sas_execute_internal_abort_dev(dev, 0, NULL);
		msleep(100);

		/* deregister the target device */
		pm8001_dev_gone_notify(dev);
		msleep(200);

		/*send phy reset to hard reset target */
		rc = sas_phy_reset(phy, 1);
		msleep(2000);
	}
	pm8001_dbg(pm8001_ha, EH, " for device[%x]:rc=%d\n",
		   pm8001_dev->device_id, rc);
out:
	sas_put_local_phy(phy);

	return rc;
}
/* mandatory SAM-3, the task reset the specified LUN*/
int pm8001_lu_reset(struct domain_device *dev, u8 *lun)
{
	int rc = TMF_RESP_FUNC_FAILED;
	struct pm8001_device *pm8001_dev = dev->lldd_dev;
	struct pm8001_hba_info *pm8001_ha = pm8001_find_ha_by_dev(dev);
	DECLARE_COMPLETION_ONSTACK(completion_setstate);

	if (PM8001_CHIP_DISP->fatal_errors(pm8001_ha)) {
		/*
		 * If the controller is in fatal error state,
		 * we will not get a response from the controller
		 */
		pm8001_dbg(pm8001_ha, FAIL,
			   "LUN reset failed due to fatal errors\n");
		return rc;
	}

	if (dev_is_sata(dev)) {
		struct sas_phy *phy = sas_get_local_phy(dev);
		sas_execute_internal_abort_dev(dev, 0, NULL);
		rc = sas_phy_reset(phy, 1);
		sas_put_local_phy(phy);
		pm8001_dev->setds_completion = &completion_setstate;
		rc = PM8001_CHIP_DISP->set_dev_state_req(pm8001_ha,
			pm8001_dev, DS_OPERATIONAL);
		wait_for_completion(&completion_setstate);
	} else {
		rc = sas_lu_reset(dev, lun);
	}
	/* If failed, fall-through I_T_Nexus reset */
	pm8001_dbg(pm8001_ha, EH, "for device[%x]:rc=%d\n",
		   pm8001_dev->device_id, rc);
	return rc;
}

/* optional SAM-3 */
int pm8001_query_task(struct sas_task *task)
{
	u32 tag = 0xdeadbeef;
	int rc = TMF_RESP_FUNC_FAILED;
	if (unlikely(!task || !task->lldd_task || !task->dev))
		return rc;

	if (task->task_proto & SAS_PROTOCOL_SSP) {
		struct scsi_cmnd *cmnd = task->uldd_task;
		struct domain_device *dev = task->dev;
		struct pm8001_hba_info *pm8001_ha =
			pm8001_find_ha_by_dev(dev);

		rc = pm8001_find_tag(task, &tag);
		if (rc == 0) {
			rc = TMF_RESP_FUNC_FAILED;
			return rc;
		}
		pm8001_dbg(pm8001_ha, EH, "Query:[%16ph]\n", cmnd->cmnd);

		rc = sas_query_task(task, tag);
		switch (rc) {
		/* The task is still in Lun, release it then */
		case TMF_RESP_FUNC_SUCC:
			pm8001_dbg(pm8001_ha, EH,
				   "The task is still in Lun\n");
			break;
		/* The task is not in Lun or failed, reset the phy */
		case TMF_RESP_FUNC_FAILED:
		case TMF_RESP_FUNC_COMPLETE:
			pm8001_dbg(pm8001_ha, EH,
				   "The task is not in Lun or failed, reset the phy\n");
			break;
		}
	}
	pr_err("pm80xx: rc= %d\n", rc);
	return rc;
}

/*  mandatory SAM-3, still need free task/ccb info, abort the specified task */
int pm8001_abort_task(struct sas_task *task)
{
	struct pm8001_ccb_info *ccb = task->lldd_task;
	unsigned long flags;
	u32 tag;
	struct domain_device *dev ;
	struct pm8001_hba_info *pm8001_ha;
	struct pm8001_device *pm8001_dev;
	int rc = TMF_RESP_FUNC_FAILED, ret;
	u32 phy_id, port_id;
	struct sas_task_slow slow_task;

	if (!task->lldd_task || !task->dev)
		return TMF_RESP_FUNC_FAILED;

	dev = task->dev;
	pm8001_dev = dev->lldd_dev;
	pm8001_ha = pm8001_find_ha_by_dev(dev);
	phy_id = pm8001_dev->attached_phy;

	if (PM8001_CHIP_DISP->fatal_errors(pm8001_ha)) {
		// If the controller is seeing fatal errors
		// abort task will not get a response from the controller
		return TMF_RESP_FUNC_FAILED;
	}

	ret = pm8001_find_tag(task, &tag);
	if (ret == 0) {
		pm8001_info(pm8001_ha, "no tag for task:%p\n", task);
		return TMF_RESP_FUNC_FAILED;
	}
	spin_lock_irqsave(&task->task_state_lock, flags);
	if (task->task_state_flags & SAS_TASK_STATE_DONE) {
		spin_unlock_irqrestore(&task->task_state_lock, flags);
		return TMF_RESP_FUNC_COMPLETE;
	}
	task->task_state_flags |= SAS_TASK_STATE_ABORTED;
	if (task->slow_task == NULL) {
		init_completion(&slow_task.completion);
		task->slow_task = &slow_task;
	}
	spin_unlock_irqrestore(&task->task_state_lock, flags);
	if (task->task_proto & SAS_PROTOCOL_SSP) {
		rc = sas_abort_task(task, tag);
		sas_execute_internal_abort_single(dev, tag, 0, NULL);
	} else if (task->task_proto & SAS_PROTOCOL_SATA ||
		task->task_proto & SAS_PROTOCOL_STP) {
		if (pm8001_ha->chip_id == chip_8006) {
			DECLARE_COMPLETION_ONSTACK(completion_reset);
			DECLARE_COMPLETION_ONSTACK(completion);
			struct pm8001_phy *phy = pm8001_ha->phy + phy_id;
			port_id = phy->port->port_id;

			/* 1. Set Device state as Recovery */
			pm8001_dev->setds_completion = &completion;
			PM8001_CHIP_DISP->set_dev_state_req(pm8001_ha,
				pm8001_dev, DS_IN_RECOVERY);
			wait_for_completion(&completion);

			/* 2. Send Phy Control Hard Reset */
			reinit_completion(&completion);
			phy->port_reset_status = PORT_RESET_TMO;
			phy->reset_success = false;
			phy->enable_completion = &completion;
			phy->reset_completion = &completion_reset;
			ret = PM8001_CHIP_DISP->phy_ctl_req(pm8001_ha, phy_id,
				PHY_HARD_RESET);
			if (ret) {
				phy->enable_completion = NULL;
				phy->reset_completion = NULL;
				goto out;
			}

			/* In the case of the reset timeout/fail we still
			 * abort the command at the firmware. The assumption
			 * here is that the drive is off doing something so
			 * that it's not processing requests, and we want to
			 * avoid getting a completion for this and either
			 * leaking the task in libsas or losing the race and
			 * getting a double free.
			 */
			pm8001_dbg(pm8001_ha, MSG,
				   "Waiting for local phy ctl\n");
			ret = wait_for_completion_timeout(&completion,
					PM8001_TASK_TIMEOUT * HZ);
			if (!ret || !phy->reset_success) {
				phy->enable_completion = NULL;
				phy->reset_completion = NULL;
			} else {
				/* 3. Wait for Port Reset complete or
				 * Port reset TMO
				 */
				pm8001_dbg(pm8001_ha, MSG,
					   "Waiting for Port reset\n");
				ret = wait_for_completion_timeout(
					&completion_reset,
					PM8001_TASK_TIMEOUT * HZ);
				if (!ret)
					phy->reset_completion = NULL;
				WARN_ON(phy->port_reset_status ==
						PORT_RESET_TMO);
				if (phy->port_reset_status == PORT_RESET_TMO) {
					pm8001_dev_gone_notify(dev);
					PM8001_CHIP_DISP->hw_event_ack_req(
						pm8001_ha, 0,
						0x07, /*HW_EVENT_PHY_DOWN ack*/
						port_id, phy_id, 0, 0);
					goto out;
				}
			}

			/*
			 * 4. SATA Abort ALL
			 * we wait for the task to be aborted so that the task
			 * is removed from the ccb. on success the caller is
			 * going to free the task.
			 */
			ret = sas_execute_internal_abort_dev(dev, 0, NULL);
			if (ret)
				goto out;
			ret = wait_for_completion_timeout(
				&task->slow_task->completion,
				PM8001_TASK_TIMEOUT * HZ);
			if (!ret)
				goto out;

			/* 5. Set Device State as Operational */
			reinit_completion(&completion);
			pm8001_dev->setds_completion = &completion;
			PM8001_CHIP_DISP->set_dev_state_req(pm8001_ha,
				pm8001_dev, DS_OPERATIONAL);
			wait_for_completion(&completion);
		} else {
			/*
			 * Ensure that if we see a completion for the ccb
			 * associated with the task which we are trying to
			 * abort then we should not touch the sas_task as it
			 * may race with libsas freeing it when return here.
			 */
			ccb->task = NULL;
			ret = sas_execute_internal_abort_single(dev, tag, 0, NULL);
		}
		rc = TMF_RESP_FUNC_COMPLETE;
	} else if (task->task_proto & SAS_PROTOCOL_SMP) {
		/* SMP */
		rc = sas_execute_internal_abort_single(dev, tag, 0, NULL);

	}
out:
	spin_lock_irqsave(&task->task_state_lock, flags);
	if (task->slow_task == &slow_task)
		task->slow_task = NULL;
	spin_unlock_irqrestore(&task->task_state_lock, flags);
	if (rc != TMF_RESP_FUNC_COMPLETE)
		pm8001_info(pm8001_ha, "rc= %d\n", rc);
	return rc;
}

int pm8001_clear_task_set(struct domain_device *dev, u8 *lun)
{
	struct pm8001_device *pm8001_dev = dev->lldd_dev;
	struct pm8001_hba_info *pm8001_ha = pm8001_find_ha_by_dev(dev);

	pm8001_dbg(pm8001_ha, EH, "I_T_L_Q clear task set[%x]\n",
		   pm8001_dev->device_id);
	return sas_clear_task_set(dev, lun);
}

void pm8001_port_formed(struct asd_sas_phy *sas_phy)
{
	struct sas_ha_struct *sas_ha = sas_phy->ha;
	struct pm8001_hba_info *pm8001_ha = sas_ha->lldd_ha;
	struct pm8001_phy *phy = sas_phy->lldd_phy;
	struct asd_sas_port *sas_port = sas_phy->port;
	struct pm8001_port *port = phy->port;

	if (!sas_port) {
		pm8001_dbg(pm8001_ha, FAIL, "Received null port\n");
		return;
	}
	sas_port->lldd_port = port;
}

void pm8001_setds_completion(struct domain_device *dev)
{
	struct pm8001_hba_info *pm8001_ha = pm8001_find_ha_by_dev(dev);
	struct pm8001_device *pm8001_dev = dev->lldd_dev;
	DECLARE_COMPLETION_ONSTACK(completion_setstate);

	if (pm8001_ha->chip_id != chip_8001) {
		pm8001_dev->setds_completion = &completion_setstate;
		PM8001_CHIP_DISP->set_dev_state_req(pm8001_ha,
			pm8001_dev, DS_OPERATIONAL);
		wait_for_completion(&completion_setstate);
	}
}

void pm8001_tmf_aborted(struct sas_task *task)
{
	struct pm8001_ccb_info *ccb = task->lldd_task;

	if (ccb)
		ccb->task = NULL;
}
