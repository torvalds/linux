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
  * pm8001_tag_clear - clear the tags bitmap
  * @pm8001_ha: our hba struct
  * @tag: the found tag associated with the task
  */
static void pm8001_tag_clear(struct pm8001_hba_info *pm8001_ha, u32 tag)
{
	void *bitmap = pm8001_ha->tags;
	clear_bit(tag, bitmap);
}

void pm8001_tag_free(struct pm8001_hba_info *pm8001_ha, u32 tag)
{
	pm8001_tag_clear(pm8001_ha, tag);
}

static void pm8001_tag_set(struct pm8001_hba_info *pm8001_ha, u32 tag)
{
	void *bitmap = pm8001_ha->tags;
	set_bit(tag, bitmap);
}

/**
  * pm8001_tag_alloc - allocate a empty tag for task used.
  * @pm8001_ha: our hba struct
  * @tag_out: the found empty tag .
  */
inline int pm8001_tag_alloc(struct pm8001_hba_info *pm8001_ha, u32 *tag_out)
{
	unsigned int index, tag;
	void *bitmap = pm8001_ha->tags;

	index = find_first_zero_bit(bitmap, pm8001_ha->tags_num);
	tag = index;
	if (tag >= pm8001_ha->tags_num)
		return -SAS_QUEUE_FULL;
	pm8001_tag_set(pm8001_ha, tag);
	*tag_out = tag;
	return 0;
}

void pm8001_tag_init(struct pm8001_hba_info *pm8001_ha)
{
	int i;
	for (i = 0; i < pm8001_ha->tags_num; ++i)
		pm8001_tag_clear(pm8001_ha, i);
}

 /**
  * pm8001_mem_alloc - allocate memory for pm8001.
  * @pdev: pci device.
  * @virt_addr: the allocated virtual address
  * @pphys_addr_hi: the physical address high byte address.
  * @pphys_addr_lo: the physical address low byte address.
  * @mem_size: memory size.
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
	mem_virt_alloc =
		pci_alloc_consistent(pdev, mem_size + align, &mem_dma_handle);
	if (!mem_virt_alloc) {
		pm8001_printk("memory allocation error\n");
		return -1;
	}
	memset((void *)mem_virt_alloc, 0, mem_size+align);
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
	DECLARE_COMPLETION_ONSTACK(completion);
	unsigned long flags;
	pm8001_ha = sas_phy->ha->lldd_ha;
	pm8001_ha->phy[phy_id].enable_completion = &completion;
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
		if (pm8001_ha->phy[phy_id].phy_state == 0) {
			PM8001_CHIP_DISP->phy_start_req(pm8001_ha, phy_id);
			wait_for_completion(&completion);
		}
		PM8001_CHIP_DISP->phy_ctl_req(pm8001_ha, phy_id,
					      PHY_LINK_RESET);
		break;
	case PHY_FUNC_HARD_RESET:
		if (pm8001_ha->phy[phy_id].phy_state == 0) {
			PM8001_CHIP_DISP->phy_start_req(pm8001_ha, phy_id);
			wait_for_completion(&completion);
		}
		PM8001_CHIP_DISP->phy_ctl_req(pm8001_ha, phy_id,
					      PHY_HARD_RESET);
		break;
	case PHY_FUNC_LINK_RESET:
		if (pm8001_ha->phy[phy_id].phy_state == 0) {
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
			uint32_t *qp = (uint32_t *)(((char *)
				pm8001_ha->io_mem[2].memvirtaddr)
				+ 0x1034 + (0x4000 * (phy_id & 3)));

			phy->invalid_dword_count = qp[0];
			phy->running_disparity_error_count = qp[1];
			phy->loss_of_dword_sync_count = qp[3];
			phy->phy_reset_problem_count = qp[4];
		}
		if (pm8001_ha->chip_id == chip_8001)
			pm8001_bar4_shift(pm8001_ha, 0);
		spin_unlock_irqrestore(&pm8001_ha->lock, flags);
		return 0;
	default:
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
	pm8001_ha = sha->lldd_ha;
	/* SAS_RE_INITIALIZATION not available in SPCv/ve */
	if (pm8001_ha->chip_id == chip_8001)
		PM8001_CHIP_DISP->sas_re_init_req(pm8001_ha);
	for (i = 0; i < pm8001_ha->chip->n_phy; ++i)
		PM8001_CHIP_DISP->phy_start_req(pm8001_ha, i);
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
	if (qc) {
		if (qc->tf.command == ATA_CMD_FPDMA_WRITE ||
			qc->tf.command == ATA_CMD_FPDMA_READ) {
			*tag = qc->tag;
			return 1;
		}
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
  * pm8001_task_prep_ssp_tm - the dispatcher function, prepare task management data
  * @pm8001_ha: our hba card information
  * @ccb: the ccb which attached to TM
  * @tmf: the task management IU
  */
static int pm8001_task_prep_ssp_tm(struct pm8001_hba_info *pm8001_ha,
	struct pm8001_ccb_info *ccb, struct pm8001_tmf_task *tmf)
{
	return PM8001_CHIP_DISP->ssp_tm_req(pm8001_ha, ccb, tmf);
}

/**
  * pm8001_task_prep_ssp - the dispatcher function,prepare ssp data for ssp task
  * @pm8001_ha: our hba card information
  * @ccb: the ccb which attached to ssp task
  */
static int pm8001_task_prep_ssp(struct pm8001_hba_info *pm8001_ha,
	struct pm8001_ccb_info *ccb)
{
	return PM8001_CHIP_DISP->ssp_io_req(pm8001_ha, ccb);
}

 /* Find the local port id that's attached to this device */
static int sas_find_local_port_id(struct domain_device *dev)
{
	struct domain_device *pdev = dev->parent;

	/* Directly attached device */
	if (!pdev)
		return dev->port->id;
	while (pdev) {
		struct domain_device *pdev_p = pdev->parent;
		if (!pdev_p)
			return pdev->port->id;
		pdev = pdev->parent;
	}
	return 0;
}

/**
  * pm8001_task_exec - queue the task(ssp, smp && ata) to the hardware.
  * @task: the task to be execute.
  * @num: if can_queue great than 1, the task can be queued up. for SMP task,
  * we always execute one one time.
  * @gfp_flags: gfp_flags.
  * @is_tmf: if it is task management task.
  * @tmf: the task management IU
  */
#define DEV_IS_GONE(pm8001_dev)	\
	((!pm8001_dev || (pm8001_dev->dev_type == SAS_PHY_UNUSED)))
static int pm8001_task_exec(struct sas_task *task, const int num,
	gfp_t gfp_flags, int is_tmf, struct pm8001_tmf_task *tmf)
{
	struct domain_device *dev = task->dev;
	struct pm8001_hba_info *pm8001_ha;
	struct pm8001_device *pm8001_dev;
	struct pm8001_port *port = NULL;
	struct sas_task *t = task;
	struct pm8001_ccb_info *ccb;
	u32 tag = 0xdeadbeef, rc, n_elem = 0;
	u32 n = num;
	unsigned long flags = 0;

	if (!dev->port) {
		struct task_status_struct *tsm = &t->task_status;
		tsm->resp = SAS_TASK_UNDELIVERED;
		tsm->stat = SAS_PHY_DOWN;
		if (dev->dev_type != SAS_SATA_DEV)
			t->task_done(t);
		return 0;
	}
	pm8001_ha = pm8001_find_ha_by_dev(task->dev);
	PM8001_IO_DBG(pm8001_ha, pm8001_printk("pm8001_task_exec device \n "));
	spin_lock_irqsave(&pm8001_ha->lock, flags);
	do {
		dev = t->dev;
		pm8001_dev = dev->lldd_dev;
		port = &pm8001_ha->port[sas_find_local_port_id(dev)];
		if (DEV_IS_GONE(pm8001_dev) || !port->port_attached) {
			if (sas_protocol_ata(t->task_proto)) {
				struct task_status_struct *ts = &t->task_status;
				ts->resp = SAS_TASK_UNDELIVERED;
				ts->stat = SAS_PHY_DOWN;

				spin_unlock_irqrestore(&pm8001_ha->lock, flags);
				t->task_done(t);
				spin_lock_irqsave(&pm8001_ha->lock, flags);
				if (n > 1)
					t = list_entry(t->list.next,
							struct sas_task, list);
				continue;
			} else {
				struct task_status_struct *ts = &t->task_status;
				ts->resp = SAS_TASK_UNDELIVERED;
				ts->stat = SAS_PHY_DOWN;
				t->task_done(t);
				if (n > 1)
					t = list_entry(t->list.next,
							struct sas_task, list);
				continue;
			}
		}
		rc = pm8001_tag_alloc(pm8001_ha, &tag);
		if (rc)
			goto err_out;
		ccb = &pm8001_ha->ccb_info[tag];

		if (!sas_protocol_ata(t->task_proto)) {
			if (t->num_scatter) {
				n_elem = dma_map_sg(pm8001_ha->dev,
					t->scatter,
					t->num_scatter,
					t->data_dir);
				if (!n_elem) {
					rc = -ENOMEM;
					goto err_out_tag;
				}
			}
		} else {
			n_elem = t->num_scatter;
		}

		t->lldd_task = ccb;
		ccb->n_elem = n_elem;
		ccb->ccb_tag = tag;
		ccb->task = t;
		switch (t->task_proto) {
		case SAS_PROTOCOL_SMP:
			rc = pm8001_task_prep_smp(pm8001_ha, ccb);
			break;
		case SAS_PROTOCOL_SSP:
			if (is_tmf)
				rc = pm8001_task_prep_ssp_tm(pm8001_ha,
					ccb, tmf);
			else
				rc = pm8001_task_prep_ssp(pm8001_ha, ccb);
			break;
		case SAS_PROTOCOL_SATA:
		case SAS_PROTOCOL_STP:
		case SAS_PROTOCOL_SATA | SAS_PROTOCOL_STP:
			rc = pm8001_task_prep_ata(pm8001_ha, ccb);
			break;
		default:
			dev_printk(KERN_ERR, pm8001_ha->dev,
				"unknown sas_task proto: 0x%x\n",
				t->task_proto);
			rc = -EINVAL;
			break;
		}

		if (rc) {
			PM8001_IO_DBG(pm8001_ha,
				pm8001_printk("rc is %x\n", rc));
			goto err_out_tag;
		}
		/* TODO: select normal or high priority */
		spin_lock(&t->task_state_lock);
		t->task_state_flags |= SAS_TASK_AT_INITIATOR;
		spin_unlock(&t->task_state_lock);
		pm8001_dev->running_req++;
		if (n > 1)
			t = list_entry(t->list.next, struct sas_task, list);
	} while (--n);
	rc = 0;
	goto out_done;

err_out_tag:
	pm8001_tag_free(pm8001_ha, tag);
err_out:
	dev_printk(KERN_ERR, pm8001_ha->dev, "pm8001 exec failed[%d]!\n", rc);
	if (!sas_protocol_ata(t->task_proto))
		if (n_elem)
			dma_unmap_sg(pm8001_ha->dev, t->scatter, n_elem,
				t->data_dir);
out_done:
	spin_unlock_irqrestore(&pm8001_ha->lock, flags);
	return rc;
}

/**
  * pm8001_queue_command - register for upper layer used, all IO commands sent
  * to HBA are from this interface.
  * @task: the task to be execute.
  * @num: if can_queue great than 1, the task can be queued up. for SMP task,
  * we always execute one one time
  * @gfp_flags: gfp_flags
  */
int pm8001_queue_command(struct sas_task *task, const int num,
		gfp_t gfp_flags)
{
	return pm8001_task_exec(task, num, gfp_flags, 0, NULL);
}

void pm8001_ccb_free(struct pm8001_hba_info *pm8001_ha, u32 ccb_idx)
{
	pm8001_tag_clear(pm8001_ha, ccb_idx);
}

/**
  * pm8001_ccb_task_free - free the sg for ssp and smp command, free the ccb.
  * @pm8001_ha: our hba card information
  * @ccb: the ccb which attached to ssp task
  * @task: the task to be free.
  * @ccb_idx: ccb index.
  */
void pm8001_ccb_task_free(struct pm8001_hba_info *pm8001_ha,
	struct sas_task *task, struct pm8001_ccb_info *ccb, u32 ccb_idx)
{
	if (!ccb->task)
		return;
	if (!sas_protocol_ata(task->task_proto))
		if (ccb->n_elem)
			dma_unmap_sg(pm8001_ha->dev, task->scatter,
				task->num_scatter, task->data_dir);

	switch (task->task_proto) {
	case SAS_PROTOCOL_SMP:
		dma_unmap_sg(pm8001_ha->dev, &task->smp_task.smp_resp, 1,
			PCI_DMA_FROMDEVICE);
		dma_unmap_sg(pm8001_ha->dev, &task->smp_task.smp_req, 1,
			PCI_DMA_TODEVICE);
		break;

	case SAS_PROTOCOL_SATA:
	case SAS_PROTOCOL_STP:
	case SAS_PROTOCOL_SSP:
	default:
		/* do nothing */
		break;
	}
	task->lldd_task = NULL;
	ccb->task = NULL;
	ccb->ccb_tag = 0xFFFFFFFF;
	ccb->open_retry = 0;
	pm8001_ccb_free(pm8001_ha, ccb_idx);
}

 /**
  * pm8001_alloc_dev - find a empty pm8001_device
  * @pm8001_ha: our hba card information
  */
struct pm8001_device *pm8001_alloc_dev(struct pm8001_hba_info *pm8001_ha)
{
	u32 dev;
	for (dev = 0; dev < PM8001_MAX_DEVICES; dev++) {
		if (pm8001_ha->devices[dev].dev_type == SAS_PHY_UNUSED) {
			pm8001_ha->devices[dev].id = dev;
			return &pm8001_ha->devices[dev];
		}
	}
	if (dev == PM8001_MAX_DEVICES) {
		PM8001_FAIL_DBG(pm8001_ha,
			pm8001_printk("max support %d devices, ignore ..\n",
			PM8001_MAX_DEVICES));
	}
	return NULL;
}
/**
  * pm8001_find_dev - find a matching pm8001_device
  * @pm8001_ha: our hba card information
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
		PM8001_FAIL_DBG(pm8001_ha, pm8001_printk("NO MATCHING "
				"DEVICE FOUND !!!\n"));
	}
	return NULL;
}

static void pm8001_free_dev(struct pm8001_device *pm8001_dev)
{
	u32 id = pm8001_dev->id;
	memset(pm8001_dev, 0, sizeof(*pm8001_dev));
	pm8001_dev->id = id;
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
	if (parent_dev && DEV_IS_EXPANDER(parent_dev->dev_type)) {
		int phy_id;
		struct ex_phy *phy;
		for (phy_id = 0; phy_id < parent_dev->ex_dev.num_phys;
		phy_id++) {
			phy = &parent_dev->ex_dev.ex_phy[phy_id];
			if (SAS_ADDR(phy->attached_sas_addr)
				== SAS_ADDR(dev->sas_addr)) {
				pm8001_device->attached_phy = phy_id;
				break;
			}
		}
		if (phy_id == parent_dev->ex_dev.num_phys) {
			PM8001_FAIL_DBG(pm8001_ha,
			pm8001_printk("Error: no attached dev:%016llx"
			" at ex:%016llx.\n", SAS_ADDR(dev->sas_addr),
				SAS_ADDR(parent_dev->sas_addr)));
			res = -1;
		}
	} else {
		if (dev->dev_type == SAS_SATA_DEV) {
			pm8001_device->attached_phy =
				dev->rphy->identify.phy_identifier;
				flag = 1; /* directly sata*/
		}
	} /*register this device to HBA*/
	PM8001_DISC_DBG(pm8001_ha, pm8001_printk("Found device\n"));
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

void pm8001_task_done(struct sas_task *task)
{
	if (!del_timer(&task->slow_task->timer))
		return;
	complete(&task->slow_task->completion);
}

static void pm8001_tmf_timedout(unsigned long data)
{
	struct sas_task *task = (struct sas_task *)data;

	task->task_state_flags |= SAS_TASK_STATE_ABORTED;
	complete(&task->slow_task->completion);
}

#define PM8001_TASK_TIMEOUT 20
/**
  * pm8001_exec_internal_tmf_task - execute some task management commands.
  * @dev: the wanted device.
  * @tmf: which task management wanted to be take.
  * @para_len: para_len.
  * @parameter: ssp task parameter.
  *
  * when errors or exception happened, we may want to do something, for example
  * abort the issued task which result in this execption, it is done by calling
  * this function, note it is also with the task execute interface.
  */
static int pm8001_exec_internal_tmf_task(struct domain_device *dev,
	void *parameter, u32 para_len, struct pm8001_tmf_task *tmf)
{
	int res, retry;
	struct sas_task *task = NULL;
	struct pm8001_hba_info *pm8001_ha = pm8001_find_ha_by_dev(dev);

	for (retry = 0; retry < 3; retry++) {
		task = sas_alloc_slow_task(GFP_KERNEL);
		if (!task)
			return -ENOMEM;

		task->dev = dev;
		task->task_proto = dev->tproto;
		memcpy(&task->ssp_task, parameter, para_len);
		task->task_done = pm8001_task_done;
		task->slow_task->timer.data = (unsigned long)task;
		task->slow_task->timer.function = pm8001_tmf_timedout;
		task->slow_task->timer.expires = jiffies + PM8001_TASK_TIMEOUT*HZ;
		add_timer(&task->slow_task->timer);

		res = pm8001_task_exec(task, 1, GFP_KERNEL, 1, tmf);

		if (res) {
			del_timer(&task->slow_task->timer);
			PM8001_FAIL_DBG(pm8001_ha,
				pm8001_printk("Executing internal task "
				"failed\n"));
			goto ex_err;
		}
		wait_for_completion(&task->slow_task->completion);
		res = -TMF_RESP_FUNC_FAILED;
		/* Even TMF timed out, return direct. */
		if ((task->task_state_flags & SAS_TASK_STATE_ABORTED)) {
			if (!(task->task_state_flags & SAS_TASK_STATE_DONE)) {
				PM8001_FAIL_DBG(pm8001_ha,
					pm8001_printk("TMF task[%x]timeout.\n",
					tmf->tmf));
				goto ex_err;
			}
		}

		if (task->task_status.resp == SAS_TASK_COMPLETE &&
			task->task_status.stat == SAM_STAT_GOOD) {
			res = TMF_RESP_FUNC_COMPLETE;
			break;
		}

		if (task->task_status.resp == SAS_TASK_COMPLETE &&
		task->task_status.stat == SAS_DATA_UNDERRUN) {
			/* no error, but return the number of bytes of
			* underrun */
			res = task->task_status.residual;
			break;
		}

		if (task->task_status.resp == SAS_TASK_COMPLETE &&
			task->task_status.stat == SAS_DATA_OVERRUN) {
			PM8001_FAIL_DBG(pm8001_ha,
				pm8001_printk("Blocked task error.\n"));
			res = -EMSGSIZE;
			break;
		} else {
			PM8001_EH_DBG(pm8001_ha,
				pm8001_printk(" Task to dev %016llx response:"
				"0x%x status 0x%x\n",
				SAS_ADDR(dev->sas_addr),
				task->task_status.resp,
				task->task_status.stat));
			sas_free_task(task);
			task = NULL;
		}
	}
ex_err:
	BUG_ON(retry == 3 && task != NULL);
	sas_free_task(task);
	return res;
}

static int
pm8001_exec_internal_task_abort(struct pm8001_hba_info *pm8001_ha,
	struct pm8001_device *pm8001_dev, struct domain_device *dev, u32 flag,
	u32 task_tag)
{
	int res, retry;
	u32 ccb_tag;
	struct pm8001_ccb_info *ccb;
	struct sas_task *task = NULL;

	for (retry = 0; retry < 3; retry++) {
		task = sas_alloc_slow_task(GFP_KERNEL);
		if (!task)
			return -ENOMEM;

		task->dev = dev;
		task->task_proto = dev->tproto;
		task->task_done = pm8001_task_done;
		task->slow_task->timer.data = (unsigned long)task;
		task->slow_task->timer.function = pm8001_tmf_timedout;
		task->slow_task->timer.expires = jiffies + PM8001_TASK_TIMEOUT * HZ;
		add_timer(&task->slow_task->timer);

		res = pm8001_tag_alloc(pm8001_ha, &ccb_tag);
		if (res)
			return res;
		ccb = &pm8001_ha->ccb_info[ccb_tag];
		ccb->device = pm8001_dev;
		ccb->ccb_tag = ccb_tag;
		ccb->task = task;

		res = PM8001_CHIP_DISP->task_abort(pm8001_ha,
			pm8001_dev, flag, task_tag, ccb_tag);

		if (res) {
			del_timer(&task->slow_task->timer);
			PM8001_FAIL_DBG(pm8001_ha,
				pm8001_printk("Executing internal task "
				"failed\n"));
			goto ex_err;
		}
		wait_for_completion(&task->slow_task->completion);
		res = TMF_RESP_FUNC_FAILED;
		/* Even TMF timed out, return direct. */
		if ((task->task_state_flags & SAS_TASK_STATE_ABORTED)) {
			if (!(task->task_state_flags & SAS_TASK_STATE_DONE)) {
				PM8001_FAIL_DBG(pm8001_ha,
					pm8001_printk("TMF task timeout.\n"));
				goto ex_err;
			}
		}

		if (task->task_status.resp == SAS_TASK_COMPLETE &&
			task->task_status.stat == SAM_STAT_GOOD) {
			res = TMF_RESP_FUNC_COMPLETE;
			break;

		} else {
			PM8001_EH_DBG(pm8001_ha,
				pm8001_printk(" Task to dev %016llx response: "
					"0x%x status 0x%x\n",
				SAS_ADDR(dev->sas_addr),
				task->task_status.resp,
				task->task_status.stat));
			sas_free_task(task);
			task = NULL;
		}
	}
ex_err:
	BUG_ON(retry == 3 && task != NULL);
	sas_free_task(task);
	return res;
}

/**
  * pm8001_dev_gone_notify - see the comments for "pm8001_dev_found_notify"
  * @dev: the device structure which sas layer used.
  */
static void pm8001_dev_gone_notify(struct domain_device *dev)
{
	unsigned long flags = 0;
	u32 tag;
	struct pm8001_hba_info *pm8001_ha;
	struct pm8001_device *pm8001_dev = dev->lldd_dev;

	pm8001_ha = pm8001_find_ha_by_dev(dev);
	spin_lock_irqsave(&pm8001_ha->lock, flags);
	pm8001_tag_alloc(pm8001_ha, &tag);
	if (pm8001_dev) {
		u32 device_id = pm8001_dev->device_id;

		PM8001_DISC_DBG(pm8001_ha,
			pm8001_printk("found dev[%d:%x] is gone.\n",
			pm8001_dev->device_id, pm8001_dev->dev_type));
		if (pm8001_dev->running_req) {
			spin_unlock_irqrestore(&pm8001_ha->lock, flags);
			pm8001_exec_internal_task_abort(pm8001_ha, pm8001_dev ,
				dev, 1, 0);
			spin_lock_irqsave(&pm8001_ha->lock, flags);
		}
		PM8001_CHIP_DISP->dereg_dev_req(pm8001_ha, device_id);
		pm8001_free_dev(pm8001_dev);
	} else {
		PM8001_DISC_DBG(pm8001_ha,
			pm8001_printk("Found dev has gone.\n"));
	}
	dev->lldd_dev = NULL;
	spin_unlock_irqrestore(&pm8001_ha->lock, flags);
}

void pm8001_dev_gone(struct domain_device *dev)
{
	pm8001_dev_gone_notify(dev);
}

static int pm8001_issue_ssp_tmf(struct domain_device *dev,
	u8 *lun, struct pm8001_tmf_task *tmf)
{
	struct sas_ssp_task ssp_task;
	if (!(dev->tproto & SAS_PROTOCOL_SSP))
		return TMF_RESP_FUNC_ESUPP;

	strncpy((u8 *)&ssp_task.LUN, lun, 8);
	return pm8001_exec_internal_tmf_task(dev, &ssp_task, sizeof(ssp_task),
		tmf);
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
		u32 tag;
		struct pm8001_ccb_info *ccb = &pm8001_ha->ccb_info[i];

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
		tag = ccb->ccb_tag;
		if (!tag || (tag == 0xFFFFFFFF))
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
			pm8001_dev->running_req--;
		spin_lock_irqsave(&task->task_state_lock, flags1);
		task->task_state_flags &= ~SAS_TASK_STATE_PENDING;
		task->task_state_flags &= ~SAS_TASK_AT_INITIATOR;
		task->task_state_flags |= SAS_TASK_STATE_DONE;
		if (unlikely((task->task_state_flags
				& SAS_TASK_STATE_ABORTED))) {
			spin_unlock_irqrestore(&task->task_state_lock,
				flags1);
			pm8001_ccb_task_free(pm8001_ha, task, ccb, tag);
		} else {
			spin_unlock_irqrestore(&task->task_state_lock,
				flags1);
			pm8001_ccb_task_free(pm8001_ha, task, ccb, tag);
			mb();/* in order to force CPU ordering */
			spin_unlock_irqrestore(&pm8001_ha->lock, flags);
			task->task_done(task);
			spin_lock_irqsave(&pm8001_ha->lock, flags);
		}
	}

	spin_unlock_irqrestore(&pm8001_ha->lock, flags);
}

/**
  * Standard mandates link reset for ATA  (type 0) and hard reset for
  * SSP (type 1) , only for RECOVERY
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
		DECLARE_COMPLETION_ONSTACK(completion_setstate);
		if (scsi_is_sas_phy_local(phy)) {
			rc = 0;
			goto out;
		}
		rc = sas_phy_reset(phy, 1);
		msleep(2000);
		rc = pm8001_exec_internal_task_abort(pm8001_ha, pm8001_dev ,
			dev, 1, 0);
		pm8001_dev->setds_completion = &completion_setstate;
		rc = PM8001_CHIP_DISP->set_dev_state_req(pm8001_ha,
			pm8001_dev, 0x01);
		wait_for_completion(&completion_setstate);
	} else {
		rc = sas_phy_reset(phy, 1);
		msleep(2000);
	}
	PM8001_EH_DBG(pm8001_ha, pm8001_printk(" for device[%x]:rc=%d\n",
		pm8001_dev->device_id, rc));
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
	u32 device_id = 0;

	if (!dev || !dev->lldd_dev)
		return -1;

	pm8001_dev = dev->lldd_dev;
	device_id = pm8001_dev->device_id;
	pm8001_ha = pm8001_find_ha_by_dev(dev);

	PM8001_EH_DBG(pm8001_ha,
			pm8001_printk("I_T_Nexus handler invoked !!"));

	phy = sas_get_local_phy(dev);

	if (dev_is_sata(dev)) {
		DECLARE_COMPLETION_ONSTACK(completion_setstate);
		if (scsi_is_sas_phy_local(phy)) {
			rc = 0;
			goto out;
		}
		/* send internal ssp/sata/smp abort command to FW */
		rc = pm8001_exec_internal_task_abort(pm8001_ha, pm8001_dev ,
							dev, 1, 0);
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
		rc = pm8001_exec_internal_task_abort(pm8001_ha, pm8001_dev ,
							dev, 1, 0);
		msleep(100);

		/* deregister the target device */
		pm8001_dev_gone_notify(dev);
		msleep(200);

		/*send phy reset to hard reset target */
		rc = sas_phy_reset(phy, 1);
		msleep(2000);
	}
	PM8001_EH_DBG(pm8001_ha, pm8001_printk(" for device[%x]:rc=%d\n",
		pm8001_dev->device_id, rc));
out:
	sas_put_local_phy(phy);

	return rc;
}
/* mandatory SAM-3, the task reset the specified LUN*/
int pm8001_lu_reset(struct domain_device *dev, u8 *lun)
{
	int rc = TMF_RESP_FUNC_FAILED;
	struct pm8001_tmf_task tmf_task;
	struct pm8001_device *pm8001_dev = dev->lldd_dev;
	struct pm8001_hba_info *pm8001_ha = pm8001_find_ha_by_dev(dev);
	if (dev_is_sata(dev)) {
		struct sas_phy *phy = sas_get_local_phy(dev);
		rc = pm8001_exec_internal_task_abort(pm8001_ha, pm8001_dev ,
			dev, 1, 0);
		rc = sas_phy_reset(phy, 1);
		sas_put_local_phy(phy);
		rc = PM8001_CHIP_DISP->set_dev_state_req(pm8001_ha,
			pm8001_dev, 0x01);
		msleep(2000);
	} else {
		tmf_task.tmf = TMF_LU_RESET;
		rc = pm8001_issue_ssp_tmf(dev, lun, &tmf_task);
	}
	/* If failed, fall-through I_T_Nexus reset */
	PM8001_EH_DBG(pm8001_ha, pm8001_printk("for device[%x]:rc=%d\n",
		pm8001_dev->device_id, rc));
	return rc;
}

/* optional SAM-3 */
int pm8001_query_task(struct sas_task *task)
{
	u32 tag = 0xdeadbeef;
	int i = 0;
	struct scsi_lun lun;
	struct pm8001_tmf_task tmf_task;
	int rc = TMF_RESP_FUNC_FAILED;
	if (unlikely(!task || !task->lldd_task || !task->dev))
		return rc;

	if (task->task_proto & SAS_PROTOCOL_SSP) {
		struct scsi_cmnd *cmnd = task->uldd_task;
		struct domain_device *dev = task->dev;
		struct pm8001_hba_info *pm8001_ha =
			pm8001_find_ha_by_dev(dev);

		int_to_scsilun(cmnd->device->lun, &lun);
		rc = pm8001_find_tag(task, &tag);
		if (rc == 0) {
			rc = TMF_RESP_FUNC_FAILED;
			return rc;
		}
		PM8001_EH_DBG(pm8001_ha, pm8001_printk("Query:["));
		for (i = 0; i < 16; i++)
			printk(KERN_INFO "%02x ", cmnd->cmnd[i]);
		printk(KERN_INFO "]\n");
		tmf_task.tmf = 	TMF_QUERY_TASK;
		tmf_task.tag_of_task_to_be_managed = tag;

		rc = pm8001_issue_ssp_tmf(dev, lun.scsi_lun, &tmf_task);
		switch (rc) {
		/* The task is still in Lun, release it then */
		case TMF_RESP_FUNC_SUCC:
			PM8001_EH_DBG(pm8001_ha,
				pm8001_printk("The task is still in Lun\n"));
			break;
		/* The task is not in Lun or failed, reset the phy */
		case TMF_RESP_FUNC_FAILED:
		case TMF_RESP_FUNC_COMPLETE:
			PM8001_EH_DBG(pm8001_ha,
			pm8001_printk("The task is not in Lun or failed,"
			" reset the phy\n"));
			break;
		}
	}
	pm8001_printk(":rc= %d\n", rc);
	return rc;
}

/*  mandatory SAM-3, still need free task/ccb info, abord the specified task */
int pm8001_abort_task(struct sas_task *task)
{
	unsigned long flags;
	u32 tag = 0xdeadbeef;
	u32 device_id;
	struct domain_device *dev ;
	struct pm8001_hba_info *pm8001_ha = NULL;
	struct pm8001_ccb_info *ccb;
	struct scsi_lun lun;
	struct pm8001_device *pm8001_dev;
	struct pm8001_tmf_task tmf_task;
	int rc = TMF_RESP_FUNC_FAILED;
	if (unlikely(!task || !task->lldd_task || !task->dev))
		return rc;
	spin_lock_irqsave(&task->task_state_lock, flags);
	if (task->task_state_flags & SAS_TASK_STATE_DONE) {
		spin_unlock_irqrestore(&task->task_state_lock, flags);
		rc = TMF_RESP_FUNC_COMPLETE;
		goto out;
	}
	spin_unlock_irqrestore(&task->task_state_lock, flags);
	if (task->task_proto & SAS_PROTOCOL_SSP) {
		struct scsi_cmnd *cmnd = task->uldd_task;
		dev = task->dev;
		ccb = task->lldd_task;
		pm8001_dev = dev->lldd_dev;
		pm8001_ha = pm8001_find_ha_by_dev(dev);
		int_to_scsilun(cmnd->device->lun, &lun);
		rc = pm8001_find_tag(task, &tag);
		if (rc == 0) {
			printk(KERN_INFO "No such tag in %s\n", __func__);
			rc = TMF_RESP_FUNC_FAILED;
			return rc;
		}
		device_id = pm8001_dev->device_id;
		PM8001_EH_DBG(pm8001_ha,
			pm8001_printk("abort io to deviceid= %d\n", device_id));
		tmf_task.tmf = TMF_ABORT_TASK;
		tmf_task.tag_of_task_to_be_managed = tag;
		rc = pm8001_issue_ssp_tmf(dev, lun.scsi_lun, &tmf_task);
		pm8001_exec_internal_task_abort(pm8001_ha, pm8001_dev,
			pm8001_dev->sas_device, 0, tag);
	} else if (task->task_proto & SAS_PROTOCOL_SATA ||
		task->task_proto & SAS_PROTOCOL_STP) {
		dev = task->dev;
		pm8001_dev = dev->lldd_dev;
		pm8001_ha = pm8001_find_ha_by_dev(dev);
		rc = pm8001_find_tag(task, &tag);
		if (rc == 0) {
			printk(KERN_INFO "No such tag in %s\n", __func__);
			rc = TMF_RESP_FUNC_FAILED;
			return rc;
		}
		rc = pm8001_exec_internal_task_abort(pm8001_ha, pm8001_dev,
			pm8001_dev->sas_device, 0, tag);
	} else if (task->task_proto & SAS_PROTOCOL_SMP) {
		/* SMP */
		dev = task->dev;
		pm8001_dev = dev->lldd_dev;
		pm8001_ha = pm8001_find_ha_by_dev(dev);
		rc = pm8001_find_tag(task, &tag);
		if (rc == 0) {
			printk(KERN_INFO "No such tag in %s\n", __func__);
			rc = TMF_RESP_FUNC_FAILED;
			return rc;
		}
		rc = pm8001_exec_internal_task_abort(pm8001_ha, pm8001_dev,
			pm8001_dev->sas_device, 0, tag);

	}
out:
	if (rc != TMF_RESP_FUNC_COMPLETE)
		pm8001_printk("rc= %d\n", rc);
	return rc;
}

int pm8001_abort_task_set(struct domain_device *dev, u8 *lun)
{
	int rc = TMF_RESP_FUNC_FAILED;
	struct pm8001_tmf_task tmf_task;

	tmf_task.tmf = TMF_ABORT_TASK_SET;
	rc = pm8001_issue_ssp_tmf(dev, lun, &tmf_task);
	return rc;
}

int pm8001_clear_aca(struct domain_device *dev, u8 *lun)
{
	int rc = TMF_RESP_FUNC_FAILED;
	struct pm8001_tmf_task tmf_task;

	tmf_task.tmf = TMF_CLEAR_ACA;
	rc = pm8001_issue_ssp_tmf(dev, lun, &tmf_task);

	return rc;
}

int pm8001_clear_task_set(struct domain_device *dev, u8 *lun)
{
	int rc = TMF_RESP_FUNC_FAILED;
	struct pm8001_tmf_task tmf_task;
	struct pm8001_device *pm8001_dev = dev->lldd_dev;
	struct pm8001_hba_info *pm8001_ha = pm8001_find_ha_by_dev(dev);

	PM8001_EH_DBG(pm8001_ha,
		pm8001_printk("I_T_L_Q clear task set[%x]\n",
		pm8001_dev->device_id));
	tmf_task.tmf = TMF_CLEAR_TASK_SET;
	rc = pm8001_issue_ssp_tmf(dev, lun, &tmf_task);
	return rc;
}

