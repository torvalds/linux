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
#include "pm8001_chips.h"
#include "pm80xx_hwi.h"

static ulong logging_level = PM8001_FAIL_LOGGING | PM8001_IOERR_LOGGING;
module_param(logging_level, ulong, 0644);
MODULE_PARM_DESC(logging_level, " bits for enabling logging info.");

static ulong link_rate = LINKRATE_15 | LINKRATE_30 | LINKRATE_60 | LINKRATE_120;
module_param(link_rate, ulong, 0644);
MODULE_PARM_DESC(link_rate, "Enable link rate.\n"
		" 1: Link rate 1.5G\n"
		" 2: Link rate 3.0G\n"
		" 4: Link rate 6.0G\n"
		" 8: Link rate 12.0G\n");

static struct scsi_transport_template *pm8001_stt;
static int pm8001_init_ccb_tag(struct pm8001_hba_info *);

/*
 * chip info structure to identify chip key functionality as
 * encryption available/not, no of ports, hw specific function ref
 */
static const struct pm8001_chip_info pm8001_chips[] = {
	[chip_8001] = {0,  8, &pm8001_8001_dispatch,},
	[chip_8008] = {0,  8, &pm8001_80xx_dispatch,},
	[chip_8009] = {1,  8, &pm8001_80xx_dispatch,},
	[chip_8018] = {0,  16, &pm8001_80xx_dispatch,},
	[chip_8019] = {1,  16, &pm8001_80xx_dispatch,},
	[chip_8074] = {0,  8, &pm8001_80xx_dispatch,},
	[chip_8076] = {0,  16, &pm8001_80xx_dispatch,},
	[chip_8077] = {0,  16, &pm8001_80xx_dispatch,},
	[chip_8006] = {0,  16, &pm8001_80xx_dispatch,},
	[chip_8070] = {0,  8, &pm8001_80xx_dispatch,},
	[chip_8072] = {0,  16, &pm8001_80xx_dispatch,},
};
static int pm8001_id;

LIST_HEAD(hba_list);

struct workqueue_struct *pm8001_wq;

static void pm8001_map_queues(struct Scsi_Host *shost)
{
	struct sas_ha_struct *sha = SHOST_TO_SAS_HA(shost);
	struct pm8001_hba_info *pm8001_ha = sha->lldd_ha;
	struct blk_mq_queue_map *qmap = &shost->tag_set.map[HCTX_TYPE_DEFAULT];

	if (pm8001_ha->number_of_intr > 1)
		blk_mq_pci_map_queues(qmap, pm8001_ha->pdev, 1);

	return blk_mq_map_queues(qmap);
}

/*
 * The main structure which LLDD must register for scsi core.
 */
static struct scsi_host_template pm8001_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.proc_name		= DRV_NAME,
	.queuecommand		= sas_queuecommand,
	.dma_need_drain		= ata_scsi_dma_need_drain,
	.target_alloc		= sas_target_alloc,
	.slave_configure	= sas_slave_configure,
	.scan_finished		= pm8001_scan_finished,
	.scan_start		= pm8001_scan_start,
	.change_queue_depth	= sas_change_queue_depth,
	.bios_param		= sas_bios_param,
	.can_queue		= 1,
	.this_id		= -1,
	.sg_tablesize		= PM8001_MAX_DMA_SG,
	.max_sectors		= SCSI_DEFAULT_MAX_SECTORS,
	.eh_device_reset_handler = sas_eh_device_reset_handler,
	.eh_target_reset_handler = sas_eh_target_reset_handler,
	.slave_alloc		= sas_slave_alloc,
	.target_destroy		= sas_target_destroy,
	.ioctl			= sas_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl		= sas_ioctl,
#endif
	.shost_groups		= pm8001_host_groups,
	.track_queue_depth	= 1,
	.cmd_per_lun		= 32,
	.map_queues		= pm8001_map_queues,
};

/*
 * Sas layer call this function to execute specific task.
 */
static struct sas_domain_function_template pm8001_transport_ops = {
	.lldd_dev_found		= pm8001_dev_found,
	.lldd_dev_gone		= pm8001_dev_gone,

	.lldd_execute_task	= pm8001_queue_command,
	.lldd_control_phy	= pm8001_phy_control,

	.lldd_abort_task	= pm8001_abort_task,
	.lldd_abort_task_set	= sas_abort_task_set,
	.lldd_clear_task_set	= pm8001_clear_task_set,
	.lldd_I_T_nexus_reset   = pm8001_I_T_nexus_reset,
	.lldd_lu_reset		= pm8001_lu_reset,
	.lldd_query_task	= pm8001_query_task,
	.lldd_port_formed	= pm8001_port_formed,
	.lldd_tmf_exec_complete = pm8001_setds_completion,
	.lldd_tmf_aborted	= pm8001_tmf_aborted,
};

/**
 * pm8001_phy_init - initiate our adapter phys
 * @pm8001_ha: our hba structure.
 * @phy_id: phy id.
 */
static void pm8001_phy_init(struct pm8001_hba_info *pm8001_ha, int phy_id)
{
	struct pm8001_phy *phy = &pm8001_ha->phy[phy_id];
	struct asd_sas_phy *sas_phy = &phy->sas_phy;
	phy->phy_state = PHY_LINK_DISABLE;
	phy->pm8001_ha = pm8001_ha;
	phy->minimum_linkrate = SAS_LINK_RATE_1_5_GBPS;
	phy->maximum_linkrate = SAS_LINK_RATE_6_0_GBPS;
	sas_phy->enabled = (phy_id < pm8001_ha->chip->n_phy) ? 1 : 0;
	sas_phy->class = SAS;
	sas_phy->iproto = SAS_PROTOCOL_ALL;
	sas_phy->tproto = 0;
	sas_phy->type = PHY_TYPE_PHYSICAL;
	sas_phy->role = PHY_ROLE_INITIATOR;
	sas_phy->oob_mode = OOB_NOT_CONNECTED;
	sas_phy->linkrate = SAS_LINK_RATE_UNKNOWN;
	sas_phy->id = phy_id;
	sas_phy->sas_addr = (u8 *)&phy->dev_sas_addr;
	sas_phy->frame_rcvd = &phy->frame_rcvd[0];
	sas_phy->ha = (struct sas_ha_struct *)pm8001_ha->shost->hostdata;
	sas_phy->lldd_phy = phy;
}

/**
 * pm8001_free - free hba
 * @pm8001_ha:	our hba structure.
 */
static void pm8001_free(struct pm8001_hba_info *pm8001_ha)
{
	int i;

	if (!pm8001_ha)
		return;

	for (i = 0; i < USI_MAX_MEMCNT; i++) {
		if (pm8001_ha->memoryMap.region[i].virt_ptr != NULL) {
			dma_free_coherent(&pm8001_ha->pdev->dev,
				(pm8001_ha->memoryMap.region[i].total_len +
				pm8001_ha->memoryMap.region[i].alignment),
				pm8001_ha->memoryMap.region[i].virt_ptr,
				pm8001_ha->memoryMap.region[i].phys_addr);
			}
	}
	PM8001_CHIP_DISP->chip_iounmap(pm8001_ha);
	flush_workqueue(pm8001_wq);
	bitmap_free(pm8001_ha->rsvd_tags);
	kfree(pm8001_ha);
}

#ifdef PM8001_USE_TASKLET

/**
 * pm8001_tasklet() - tasklet for 64 msi-x interrupt handler
 * @opaque: the passed general host adapter struct
 * Note: pm8001_tasklet is common for pm8001 & pm80xx
 */
static void pm8001_tasklet(unsigned long opaque)
{
	struct pm8001_hba_info *pm8001_ha;
	struct isr_param *irq_vector;

	irq_vector = (struct isr_param *)opaque;
	pm8001_ha = irq_vector->drv_inst;
	if (unlikely(!pm8001_ha))
		BUG_ON(1);
	PM8001_CHIP_DISP->isr(pm8001_ha, irq_vector->irq_id);
}
#endif

/**
 * pm8001_interrupt_handler_msix - main MSIX interrupt handler.
 * It obtains the vector number and calls the equivalent bottom
 * half or services directly.
 * @irq: interrupt number
 * @opaque: the passed outbound queue/vector. Host structure is
 * retrieved from the same.
 */
static irqreturn_t pm8001_interrupt_handler_msix(int irq, void *opaque)
{
	struct isr_param *irq_vector;
	struct pm8001_hba_info *pm8001_ha;
	irqreturn_t ret = IRQ_HANDLED;
	irq_vector = (struct isr_param *)opaque;
	pm8001_ha = irq_vector->drv_inst;

	if (unlikely(!pm8001_ha))
		return IRQ_NONE;
	if (!PM8001_CHIP_DISP->is_our_interrupt(pm8001_ha))
		return IRQ_NONE;
#ifdef PM8001_USE_TASKLET
	tasklet_schedule(&pm8001_ha->tasklet[irq_vector->irq_id]);
#else
	ret = PM8001_CHIP_DISP->isr(pm8001_ha, irq_vector->irq_id);
#endif
	return ret;
}

/**
 * pm8001_interrupt_handler_intx - main INTx interrupt handler.
 * @irq: interrupt number
 * @dev_id: sas_ha structure. The HBA is retrieved from sas_ha structure.
 */

static irqreturn_t pm8001_interrupt_handler_intx(int irq, void *dev_id)
{
	struct pm8001_hba_info *pm8001_ha;
	irqreturn_t ret = IRQ_HANDLED;
	struct sas_ha_struct *sha = dev_id;
	pm8001_ha = sha->lldd_ha;
	if (unlikely(!pm8001_ha))
		return IRQ_NONE;
	if (!PM8001_CHIP_DISP->is_our_interrupt(pm8001_ha))
		return IRQ_NONE;

#ifdef PM8001_USE_TASKLET
	tasklet_schedule(&pm8001_ha->tasklet[0]);
#else
	ret = PM8001_CHIP_DISP->isr(pm8001_ha, 0);
#endif
	return ret;
}

static u32 pm8001_setup_irq(struct pm8001_hba_info *pm8001_ha);
static u32 pm8001_request_irq(struct pm8001_hba_info *pm8001_ha);

/**
 * pm8001_alloc - initiate our hba structure and 6 DMAs area.
 * @pm8001_ha: our hba structure.
 * @ent: PCI device ID structure to match on
 */
static int pm8001_alloc(struct pm8001_hba_info *pm8001_ha,
			const struct pci_device_id *ent)
{
	int i, count = 0, rc = 0;
	u32 ci_offset, ib_offset, ob_offset, pi_offset;
	struct inbound_queue_table *ibq;
	struct outbound_queue_table *obq;

	spin_lock_init(&pm8001_ha->lock);
	spin_lock_init(&pm8001_ha->bitmap_lock);
	pm8001_dbg(pm8001_ha, INIT, "pm8001_alloc: PHY:%x\n",
		   pm8001_ha->chip->n_phy);

	/* Setup Interrupt */
	rc = pm8001_setup_irq(pm8001_ha);
	if (rc) {
		pm8001_dbg(pm8001_ha, FAIL,
			   "pm8001_setup_irq failed [ret: %d]\n", rc);
		goto err_out;
	}
	/* Request Interrupt */
	rc = pm8001_request_irq(pm8001_ha);
	if (rc)
		goto err_out;

	count = pm8001_ha->max_q_num;
	/* Queues are chosen based on the number of cores/msix availability */
	ib_offset = pm8001_ha->ib_offset  = USI_MAX_MEMCNT_BASE;
	ci_offset = pm8001_ha->ci_offset  = ib_offset + count;
	ob_offset = pm8001_ha->ob_offset  = ci_offset + count;
	pi_offset = pm8001_ha->pi_offset  = ob_offset + count;
	pm8001_ha->max_memcnt = pi_offset + count;

	for (i = 0; i < pm8001_ha->chip->n_phy; i++) {
		pm8001_phy_init(pm8001_ha, i);
		pm8001_ha->port[i].wide_port_phymap = 0;
		pm8001_ha->port[i].port_attached = 0;
		pm8001_ha->port[i].port_state = 0;
		INIT_LIST_HEAD(&pm8001_ha->port[i].list);
	}

	/* MPI Memory region 1 for AAP Event Log for fw */
	pm8001_ha->memoryMap.region[AAP1].num_elements = 1;
	pm8001_ha->memoryMap.region[AAP1].element_size = PM8001_EVENT_LOG_SIZE;
	pm8001_ha->memoryMap.region[AAP1].total_len = PM8001_EVENT_LOG_SIZE;
	pm8001_ha->memoryMap.region[AAP1].alignment = 32;

	/* MPI Memory region 2 for IOP Event Log for fw */
	pm8001_ha->memoryMap.region[IOP].num_elements = 1;
	pm8001_ha->memoryMap.region[IOP].element_size = PM8001_EVENT_LOG_SIZE;
	pm8001_ha->memoryMap.region[IOP].total_len = PM8001_EVENT_LOG_SIZE;
	pm8001_ha->memoryMap.region[IOP].alignment = 32;

	for (i = 0; i < count; i++) {
		ibq = &pm8001_ha->inbnd_q_tbl[i];
		spin_lock_init(&ibq->iq_lock);
		/* MPI Memory region 3 for consumer Index of inbound queues */
		pm8001_ha->memoryMap.region[ci_offset+i].num_elements = 1;
		pm8001_ha->memoryMap.region[ci_offset+i].element_size = 4;
		pm8001_ha->memoryMap.region[ci_offset+i].total_len = 4;
		pm8001_ha->memoryMap.region[ci_offset+i].alignment = 4;

		if ((ent->driver_data) != chip_8001) {
			/* MPI Memory region 5 inbound queues */
			pm8001_ha->memoryMap.region[ib_offset+i].num_elements =
						PM8001_MPI_QUEUE;
			pm8001_ha->memoryMap.region[ib_offset+i].element_size
								= 128;
			pm8001_ha->memoryMap.region[ib_offset+i].total_len =
						PM8001_MPI_QUEUE * 128;
			pm8001_ha->memoryMap.region[ib_offset+i].alignment
								= 128;
		} else {
			pm8001_ha->memoryMap.region[ib_offset+i].num_elements =
						PM8001_MPI_QUEUE;
			pm8001_ha->memoryMap.region[ib_offset+i].element_size
								= 64;
			pm8001_ha->memoryMap.region[ib_offset+i].total_len =
						PM8001_MPI_QUEUE * 64;
			pm8001_ha->memoryMap.region[ib_offset+i].alignment = 64;
		}
	}

	for (i = 0; i < count; i++) {
		obq = &pm8001_ha->outbnd_q_tbl[i];
		spin_lock_init(&obq->oq_lock);
		/* MPI Memory region 4 for producer Index of outbound queues */
		pm8001_ha->memoryMap.region[pi_offset+i].num_elements = 1;
		pm8001_ha->memoryMap.region[pi_offset+i].element_size = 4;
		pm8001_ha->memoryMap.region[pi_offset+i].total_len = 4;
		pm8001_ha->memoryMap.region[pi_offset+i].alignment = 4;

		if (ent->driver_data != chip_8001) {
			/* MPI Memory region 6 Outbound queues */
			pm8001_ha->memoryMap.region[ob_offset+i].num_elements =
						PM8001_MPI_QUEUE;
			pm8001_ha->memoryMap.region[ob_offset+i].element_size
								= 128;
			pm8001_ha->memoryMap.region[ob_offset+i].total_len =
						PM8001_MPI_QUEUE * 128;
			pm8001_ha->memoryMap.region[ob_offset+i].alignment
								= 128;
		} else {
			/* MPI Memory region 6 Outbound queues */
			pm8001_ha->memoryMap.region[ob_offset+i].num_elements =
						PM8001_MPI_QUEUE;
			pm8001_ha->memoryMap.region[ob_offset+i].element_size
								= 64;
			pm8001_ha->memoryMap.region[ob_offset+i].total_len =
						PM8001_MPI_QUEUE * 64;
			pm8001_ha->memoryMap.region[ob_offset+i].alignment = 64;
		}

	}
	/* Memory region write DMA*/
	pm8001_ha->memoryMap.region[NVMD].num_elements = 1;
	pm8001_ha->memoryMap.region[NVMD].element_size = 4096;
	pm8001_ha->memoryMap.region[NVMD].total_len = 4096;

	/* Memory region for fw flash */
	pm8001_ha->memoryMap.region[FW_FLASH].total_len = 4096;

	pm8001_ha->memoryMap.region[FORENSIC_MEM].num_elements = 1;
	pm8001_ha->memoryMap.region[FORENSIC_MEM].total_len = 0x10000;
	pm8001_ha->memoryMap.region[FORENSIC_MEM].element_size = 0x10000;
	pm8001_ha->memoryMap.region[FORENSIC_MEM].alignment = 0x10000;
	for (i = 0; i < pm8001_ha->max_memcnt; i++) {
		struct mpi_mem *region = &pm8001_ha->memoryMap.region[i];

		if (pm8001_mem_alloc(pm8001_ha->pdev,
				     &region->virt_ptr,
				     &region->phys_addr,
				     &region->phys_addr_hi,
				     &region->phys_addr_lo,
				     region->total_len,
				     region->alignment) != 0) {
			pm8001_dbg(pm8001_ha, FAIL, "Mem%d alloc failed\n", i);
			goto err_out;
		}
	}

	/* Memory region for devices*/
	pm8001_ha->devices = kzalloc(PM8001_MAX_DEVICES
				* sizeof(struct pm8001_device), GFP_KERNEL);
	if (!pm8001_ha->devices) {
		rc = -ENOMEM;
		goto err_out_nodev;
	}
	for (i = 0; i < PM8001_MAX_DEVICES; i++) {
		pm8001_ha->devices[i].dev_type = SAS_PHY_UNUSED;
		pm8001_ha->devices[i].id = i;
		pm8001_ha->devices[i].device_id = PM8001_MAX_DEVICES;
		atomic_set(&pm8001_ha->devices[i].running_req, 0);
	}
	pm8001_ha->flags = PM8001F_INIT_TIME;
	return 0;

err_out_nodev:
	for (i = 0; i < pm8001_ha->max_memcnt; i++) {
		if (pm8001_ha->memoryMap.region[i].virt_ptr != NULL) {
			dma_free_coherent(&pm8001_ha->pdev->dev,
				(pm8001_ha->memoryMap.region[i].total_len +
				pm8001_ha->memoryMap.region[i].alignment),
				pm8001_ha->memoryMap.region[i].virt_ptr,
				pm8001_ha->memoryMap.region[i].phys_addr);
		}
	}
err_out:
	return 1;
}

/**
 * pm8001_ioremap - remap the pci high physical address to kernel virtual
 * address so that we can access them.
 * @pm8001_ha: our hba structure.
 */
static int pm8001_ioremap(struct pm8001_hba_info *pm8001_ha)
{
	u32 bar;
	u32 logicalBar = 0;
	struct pci_dev *pdev;

	pdev = pm8001_ha->pdev;
	/* map pci mem (PMC pci base 0-3)*/
	for (bar = 0; bar < PCI_STD_NUM_BARS; bar++) {
		/*
		** logical BARs for SPC:
		** bar 0 and 1 - logical BAR0
		** bar 2 and 3 - logical BAR1
		** bar4 - logical BAR2
		** bar5 - logical BAR3
		** Skip the appropriate assignments:
		*/
		if ((bar == 1) || (bar == 3))
			continue;
		if (pci_resource_flags(pdev, bar) & IORESOURCE_MEM) {
			pm8001_ha->io_mem[logicalBar].membase =
				pci_resource_start(pdev, bar);
			pm8001_ha->io_mem[logicalBar].memsize =
				pci_resource_len(pdev, bar);
			pm8001_ha->io_mem[logicalBar].memvirtaddr =
				ioremap(pm8001_ha->io_mem[logicalBar].membase,
				pm8001_ha->io_mem[logicalBar].memsize);
			if (!pm8001_ha->io_mem[logicalBar].memvirtaddr) {
				pm8001_dbg(pm8001_ha, INIT,
					"Failed to ioremap bar %d, logicalBar %d",
				   bar, logicalBar);
				return -ENOMEM;
			}
			pm8001_dbg(pm8001_ha, INIT,
				   "base addr %llx virt_addr=%llx len=%d\n",
				   (u64)pm8001_ha->io_mem[logicalBar].membase,
				   (u64)(unsigned long)
				   pm8001_ha->io_mem[logicalBar].memvirtaddr,
				   pm8001_ha->io_mem[logicalBar].memsize);
		} else {
			pm8001_ha->io_mem[logicalBar].membase	= 0;
			pm8001_ha->io_mem[logicalBar].memsize	= 0;
			pm8001_ha->io_mem[logicalBar].memvirtaddr = NULL;
		}
		logicalBar++;
	}
	return 0;
}

/**
 * pm8001_pci_alloc - initialize our ha card structure
 * @pdev: pci device.
 * @ent: ent
 * @shost: scsi host struct which has been initialized before.
 */
static struct pm8001_hba_info *pm8001_pci_alloc(struct pci_dev *pdev,
				 const struct pci_device_id *ent,
				struct Scsi_Host *shost)

{
	struct pm8001_hba_info *pm8001_ha;
	struct sas_ha_struct *sha = SHOST_TO_SAS_HA(shost);
	int j;

	pm8001_ha = sha->lldd_ha;
	if (!pm8001_ha)
		return NULL;

	pm8001_ha->pdev = pdev;
	pm8001_ha->dev = &pdev->dev;
	pm8001_ha->chip_id = ent->driver_data;
	pm8001_ha->chip = &pm8001_chips[pm8001_ha->chip_id];
	pm8001_ha->irq = pdev->irq;
	pm8001_ha->sas = sha;
	pm8001_ha->shost = shost;
	pm8001_ha->id = pm8001_id++;
	pm8001_ha->logging_level = logging_level;
	pm8001_ha->non_fatal_count = 0;
	if (link_rate >= 1 && link_rate <= 15)
		pm8001_ha->link_rate = (link_rate << 8);
	else {
		pm8001_ha->link_rate = LINKRATE_15 | LINKRATE_30 |
			LINKRATE_60 | LINKRATE_120;
		pm8001_dbg(pm8001_ha, FAIL,
			   "Setting link rate to default value\n");
	}
	sprintf(pm8001_ha->name, "%s%d", DRV_NAME, pm8001_ha->id);
	/* IOMB size is 128 for 8088/89 controllers */
	if (pm8001_ha->chip_id != chip_8001)
		pm8001_ha->iomb_size = IOMB_SIZE_SPCV;
	else
		pm8001_ha->iomb_size = IOMB_SIZE_SPC;

#ifdef PM8001_USE_TASKLET
	/* Tasklet for non msi-x interrupt handler */
	if ((!pdev->msix_cap || !pci_msi_enabled())
	    || (pm8001_ha->chip_id == chip_8001))
		tasklet_init(&pm8001_ha->tasklet[0], pm8001_tasklet,
			(unsigned long)&(pm8001_ha->irq_vector[0]));
	else
		for (j = 0; j < PM8001_MAX_MSIX_VEC; j++)
			tasklet_init(&pm8001_ha->tasklet[j], pm8001_tasklet,
				(unsigned long)&(pm8001_ha->irq_vector[j]));
#endif
	if (pm8001_ioremap(pm8001_ha))
		goto failed_pci_alloc;
	if (!pm8001_alloc(pm8001_ha, ent))
		return pm8001_ha;
failed_pci_alloc:
	pm8001_free(pm8001_ha);
	return NULL;
}

/**
 * pci_go_44 - pm8001 specified, its DMA is 44 bit rather than 64 bit
 * @pdev: pci device.
 */
static int pci_go_44(struct pci_dev *pdev)
{
	int rc;

	rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(44));
	if (rc) {
		rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		if (rc)
			dev_printk(KERN_ERR, &pdev->dev,
				"32-bit DMA enable failed\n");
	}
	return rc;
}

/**
 * pm8001_prep_sas_ha_init - allocate memory in general hba struct && init them.
 * @shost: scsi host which has been allocated outside.
 * @chip_info: our ha struct.
 */
static int pm8001_prep_sas_ha_init(struct Scsi_Host *shost,
				   const struct pm8001_chip_info *chip_info)
{
	int phy_nr, port_nr;
	struct asd_sas_phy **arr_phy;
	struct asd_sas_port **arr_port;
	struct sas_ha_struct *sha = SHOST_TO_SAS_HA(shost);

	phy_nr = chip_info->n_phy;
	port_nr = phy_nr;
	memset(sha, 0x00, sizeof(*sha));
	arr_phy = kcalloc(phy_nr, sizeof(void *), GFP_KERNEL);
	if (!arr_phy)
		goto exit;
	arr_port = kcalloc(port_nr, sizeof(void *), GFP_KERNEL);
	if (!arr_port)
		goto exit_free2;

	sha->sas_phy = arr_phy;
	sha->sas_port = arr_port;
	sha->lldd_ha = kzalloc(sizeof(struct pm8001_hba_info), GFP_KERNEL);
	if (!sha->lldd_ha)
		goto exit_free1;

	shost->transportt = pm8001_stt;
	shost->max_id = PM8001_MAX_DEVICES;
	shost->unique_id = pm8001_id;
	shost->max_cmd_len = 16;
	return 0;
exit_free1:
	kfree(arr_port);
exit_free2:
	kfree(arr_phy);
exit:
	return -1;
}

/**
 * pm8001_post_sas_ha_init - initialize general hba struct defined in libsas
 * @shost: scsi host which has been allocated outside
 * @chip_info: our ha struct.
 */
static void  pm8001_post_sas_ha_init(struct Scsi_Host *shost,
				     const struct pm8001_chip_info *chip_info)
{
	int i = 0;
	struct pm8001_hba_info *pm8001_ha;
	struct sas_ha_struct *sha = SHOST_TO_SAS_HA(shost);

	pm8001_ha = sha->lldd_ha;
	for (i = 0; i < chip_info->n_phy; i++) {
		sha->sas_phy[i] = &pm8001_ha->phy[i].sas_phy;
		sha->sas_port[i] = &pm8001_ha->port[i].sas_port;
		sha->sas_phy[i]->sas_addr =
			(u8 *)&pm8001_ha->phy[i].dev_sas_addr;
	}
	sha->sas_ha_name = DRV_NAME;
	sha->dev = pm8001_ha->dev;
	sha->strict_wide_ports = 1;
	sha->lldd_module = THIS_MODULE;
	sha->sas_addr = &pm8001_ha->sas_addr[0];
	sha->num_phys = chip_info->n_phy;
	sha->core.shost = shost;
}

/**
 * pm8001_init_sas_add - initialize sas address
 * @pm8001_ha: our ha struct.
 *
 * Currently we just set the fixed SAS address to our HBA, for manufacture,
 * it should read from the EEPROM
 */
static void pm8001_init_sas_add(struct pm8001_hba_info *pm8001_ha)
{
	u8 i, j;
	u8 sas_add[8];
#ifdef PM8001_READ_VPD
	/* For new SPC controllers WWN is stored in flash vpd
	*  For SPC/SPCve controllers WWN is stored in EEPROM
	*  For Older SPC WWN is stored in NVMD
	*/
	DECLARE_COMPLETION_ONSTACK(completion);
	struct pm8001_ioctl_payload payload;
	u16 deviceid;
	int rc;

	pci_read_config_word(pm8001_ha->pdev, PCI_DEVICE_ID, &deviceid);
	pm8001_ha->nvmd_completion = &completion;

	if (pm8001_ha->chip_id == chip_8001) {
		if (deviceid == 0x8081 || deviceid == 0x0042) {
			payload.minor_function = 4;
			payload.rd_length = 4096;
		} else {
			payload.minor_function = 0;
			payload.rd_length = 128;
		}
	} else if ((pm8001_ha->chip_id == chip_8070 ||
			pm8001_ha->chip_id == chip_8072) &&
			pm8001_ha->pdev->subsystem_vendor == PCI_VENDOR_ID_ATTO) {
		payload.minor_function = 4;
		payload.rd_length = 4096;
	} else {
		payload.minor_function = 1;
		payload.rd_length = 4096;
	}
	payload.offset = 0;
	payload.func_specific = kzalloc(payload.rd_length, GFP_KERNEL);
	if (!payload.func_specific) {
		pm8001_dbg(pm8001_ha, INIT, "mem alloc fail\n");
		return;
	}
	rc = PM8001_CHIP_DISP->get_nvmd_req(pm8001_ha, &payload);
	if (rc) {
		kfree(payload.func_specific);
		pm8001_dbg(pm8001_ha, INIT, "nvmd failed\n");
		return;
	}
	wait_for_completion(&completion);

	for (i = 0, j = 0; i <= 7; i++, j++) {
		if (pm8001_ha->chip_id == chip_8001) {
			if (deviceid == 0x8081)
				pm8001_ha->sas_addr[j] =
					payload.func_specific[0x704 + i];
			else if (deviceid == 0x0042)
				pm8001_ha->sas_addr[j] =
					payload.func_specific[0x010 + i];
		} else if ((pm8001_ha->chip_id == chip_8070 ||
				pm8001_ha->chip_id == chip_8072) &&
				pm8001_ha->pdev->subsystem_vendor == PCI_VENDOR_ID_ATTO) {
			pm8001_ha->sas_addr[j] =
					payload.func_specific[0x010 + i];
		} else
			pm8001_ha->sas_addr[j] =
					payload.func_specific[0x804 + i];
	}
	memcpy(sas_add, pm8001_ha->sas_addr, SAS_ADDR_SIZE);
	for (i = 0; i < pm8001_ha->chip->n_phy; i++) {
		if (i && ((i % 4) == 0))
			sas_add[7] = sas_add[7] + 4;
		memcpy(&pm8001_ha->phy[i].dev_sas_addr,
			sas_add, SAS_ADDR_SIZE);
		pm8001_dbg(pm8001_ha, INIT, "phy %d sas_addr = %016llx\n", i,
			   pm8001_ha->phy[i].dev_sas_addr);
	}
	kfree(payload.func_specific);
#else
	for (i = 0; i < pm8001_ha->chip->n_phy; i++) {
		pm8001_ha->phy[i].dev_sas_addr = 0x50010c600047f9d0ULL;
		pm8001_ha->phy[i].dev_sas_addr =
			cpu_to_be64((u64)
				(*(u64 *)&pm8001_ha->phy[i].dev_sas_addr));
	}
	memcpy(pm8001_ha->sas_addr, &pm8001_ha->phy[0].dev_sas_addr,
		SAS_ADDR_SIZE);
#endif
}

/*
 * pm8001_get_phy_settings_info : Read phy setting values.
 * @pm8001_ha : our hba.
 */
static int pm8001_get_phy_settings_info(struct pm8001_hba_info *pm8001_ha)
{

#ifdef PM8001_READ_VPD
	/*OPTION ROM FLASH read for the SPC cards */
	DECLARE_COMPLETION_ONSTACK(completion);
	struct pm8001_ioctl_payload payload;
	int rc;

	pm8001_ha->nvmd_completion = &completion;
	/* SAS ADDRESS read from flash / EEPROM */
	payload.minor_function = 6;
	payload.offset = 0;
	payload.rd_length = 4096;
	payload.func_specific = kzalloc(4096, GFP_KERNEL);
	if (!payload.func_specific)
		return -ENOMEM;
	/* Read phy setting values from flash */
	rc = PM8001_CHIP_DISP->get_nvmd_req(pm8001_ha, &payload);
	if (rc) {
		kfree(payload.func_specific);
		pm8001_dbg(pm8001_ha, INIT, "nvmd failed\n");
		return -ENOMEM;
	}
	wait_for_completion(&completion);
	pm8001_set_phy_profile(pm8001_ha, sizeof(u8), payload.func_specific);
	kfree(payload.func_specific);
#endif
	return 0;
}

struct pm8001_mpi3_phy_pg_trx_config {
	u32 LaneLosCfg;
	u32 LanePgaCfg1;
	u32 LanePisoCfg1;
	u32 LanePisoCfg2;
	u32 LanePisoCfg3;
	u32 LanePisoCfg4;
	u32 LanePisoCfg5;
	u32 LanePisoCfg6;
	u32 LaneBctCtrl;
};

/**
 * pm8001_get_internal_phy_settings - Retrieves the internal PHY settings
 * @pm8001_ha : our adapter
 * @phycfg : PHY config page to populate
 */
static
void pm8001_get_internal_phy_settings(struct pm8001_hba_info *pm8001_ha,
		struct pm8001_mpi3_phy_pg_trx_config *phycfg)
{
	phycfg->LaneLosCfg   = 0x00000132;
	phycfg->LanePgaCfg1  = 0x00203949;
	phycfg->LanePisoCfg1 = 0x000000FF;
	phycfg->LanePisoCfg2 = 0xFF000001;
	phycfg->LanePisoCfg3 = 0xE7011300;
	phycfg->LanePisoCfg4 = 0x631C40C0;
	phycfg->LanePisoCfg5 = 0xF8102036;
	phycfg->LanePisoCfg6 = 0xF74A1000;
	phycfg->LaneBctCtrl  = 0x00FB33F8;
}

/**
 * pm8001_get_external_phy_settings - Retrieves the external PHY settings
 * @pm8001_ha : our adapter
 * @phycfg : PHY config page to populate
 */
static
void pm8001_get_external_phy_settings(struct pm8001_hba_info *pm8001_ha,
		struct pm8001_mpi3_phy_pg_trx_config *phycfg)
{
	phycfg->LaneLosCfg   = 0x00000132;
	phycfg->LanePgaCfg1  = 0x00203949;
	phycfg->LanePisoCfg1 = 0x000000FF;
	phycfg->LanePisoCfg2 = 0xFF000001;
	phycfg->LanePisoCfg3 = 0xE7011300;
	phycfg->LanePisoCfg4 = 0x63349140;
	phycfg->LanePisoCfg5 = 0xF8102036;
	phycfg->LanePisoCfg6 = 0xF80D9300;
	phycfg->LaneBctCtrl  = 0x00FB33F8;
}

/**
 * pm8001_get_phy_mask - Retrieves the mask that denotes if a PHY is int/ext
 * @pm8001_ha : our adapter
 * @phymask : The PHY mask
 */
static
void pm8001_get_phy_mask(struct pm8001_hba_info *pm8001_ha, int *phymask)
{
	switch (pm8001_ha->pdev->subsystem_device) {
	case 0x0070: /* H1280 - 8 external 0 internal */
	case 0x0072: /* H12F0 - 16 external 0 internal */
		*phymask = 0x0000;
		break;

	case 0x0071: /* H1208 - 0 external 8 internal */
	case 0x0073: /* H120F - 0 external 16 internal */
		*phymask = 0xFFFF;
		break;

	case 0x0080: /* H1244 - 4 external 4 internal */
		*phymask = 0x00F0;
		break;

	case 0x0081: /* H1248 - 4 external 8 internal */
		*phymask = 0x0FF0;
		break;

	case 0x0082: /* H1288 - 8 external 8 internal */
		*phymask = 0xFF00;
		break;

	default:
		pm8001_dbg(pm8001_ha, INIT,
			   "Unknown subsystem device=0x%.04x\n",
			   pm8001_ha->pdev->subsystem_device);
	}
}

/**
 * pm8001_set_phy_settings_ven_117c_12G() - Configure ATTO 12Gb PHY settings
 * @pm8001_ha : our adapter
 */
static
int pm8001_set_phy_settings_ven_117c_12G(struct pm8001_hba_info *pm8001_ha)
{
	struct pm8001_mpi3_phy_pg_trx_config phycfg_int;
	struct pm8001_mpi3_phy_pg_trx_config phycfg_ext;
	int phymask = 0;
	int i = 0;

	memset(&phycfg_int, 0, sizeof(phycfg_int));
	memset(&phycfg_ext, 0, sizeof(phycfg_ext));

	pm8001_get_internal_phy_settings(pm8001_ha, &phycfg_int);
	pm8001_get_external_phy_settings(pm8001_ha, &phycfg_ext);
	pm8001_get_phy_mask(pm8001_ha, &phymask);

	for (i = 0; i < pm8001_ha->chip->n_phy; i++) {
		if (phymask & (1 << i)) {/* Internal PHY */
			pm8001_set_phy_profile_single(pm8001_ha, i,
					sizeof(phycfg_int) / sizeof(u32),
					(u32 *)&phycfg_int);

		} else { /* External PHY */
			pm8001_set_phy_profile_single(pm8001_ha, i,
					sizeof(phycfg_ext) / sizeof(u32),
					(u32 *)&phycfg_ext);
		}
	}

	return 0;
}

/**
 * pm8001_configure_phy_settings - Configures PHY settings based on vendor ID.
 * @pm8001_ha : our hba.
 */
static int pm8001_configure_phy_settings(struct pm8001_hba_info *pm8001_ha)
{
	switch (pm8001_ha->pdev->subsystem_vendor) {
	case PCI_VENDOR_ID_ATTO:
		if (pm8001_ha->pdev->device == 0x0042) /* 6Gb */
			return 0;
		else
			return pm8001_set_phy_settings_ven_117c_12G(pm8001_ha);

	case PCI_VENDOR_ID_ADAPTEC2:
	case 0:
		return 0;

	default:
		return pm8001_get_phy_settings_info(pm8001_ha);
	}
}

#ifdef PM8001_USE_MSIX
/**
 * pm8001_setup_msix - enable MSI-X interrupt
 * @pm8001_ha: our ha struct.
 */
static u32 pm8001_setup_msix(struct pm8001_hba_info *pm8001_ha)
{
	unsigned int allocated_irq_vectors;
	int rc;

	/* SPCv controllers supports 64 msi-x */
	if (pm8001_ha->chip_id == chip_8001) {
		rc = pci_alloc_irq_vectors(pm8001_ha->pdev, 1, 1,
					   PCI_IRQ_MSIX);
	} else {
		/*
		 * Queue index #0 is used always for housekeeping, so don't
		 * include in the affinity spreading.
		 */
		struct irq_affinity desc = {
			.pre_vectors = 1,
		};
		rc = pci_alloc_irq_vectors_affinity(
				pm8001_ha->pdev, 2, PM8001_MAX_MSIX_VEC,
				PCI_IRQ_MSIX | PCI_IRQ_AFFINITY, &desc);
	}

	allocated_irq_vectors = rc;
	if (rc < 0)
		return rc;

	/* Assigns the number of interrupts */
	pm8001_ha->number_of_intr = allocated_irq_vectors;

	/* Maximum queue number updating in HBA structure */
	pm8001_ha->max_q_num = allocated_irq_vectors;

	pm8001_dbg(pm8001_ha, INIT,
		   "pci_alloc_irq_vectors request ret:%d no of intr %d\n",
		   rc, pm8001_ha->number_of_intr);
	return 0;
}

static u32 pm8001_request_msix(struct pm8001_hba_info *pm8001_ha)
{
	u32 i = 0, j = 0;
	int flag = 0, rc = 0;
	int nr_irqs = pm8001_ha->number_of_intr;

	if (pm8001_ha->chip_id != chip_8001)
		flag &= ~IRQF_SHARED;

	pm8001_dbg(pm8001_ha, INIT,
		   "pci_enable_msix request number of intr %d\n",
		   pm8001_ha->number_of_intr);

	if (nr_irqs > ARRAY_SIZE(pm8001_ha->intr_drvname))
		nr_irqs = ARRAY_SIZE(pm8001_ha->intr_drvname);

	for (i = 0; i < nr_irqs; i++) {
		snprintf(pm8001_ha->intr_drvname[i],
			sizeof(pm8001_ha->intr_drvname[0]),
			"%s-%d", pm8001_ha->name, i);
		pm8001_ha->irq_vector[i].irq_id = i;
		pm8001_ha->irq_vector[i].drv_inst = pm8001_ha;

		rc = request_irq(pci_irq_vector(pm8001_ha->pdev, i),
			pm8001_interrupt_handler_msix, flag,
			pm8001_ha->intr_drvname[i],
			&(pm8001_ha->irq_vector[i]));
		if (rc) {
			for (j = 0; j < i; j++) {
				free_irq(pci_irq_vector(pm8001_ha->pdev, i),
					&(pm8001_ha->irq_vector[i]));
			}
			pci_free_irq_vectors(pm8001_ha->pdev);
			break;
		}
	}

	return rc;
}
#endif

static u32 pm8001_setup_irq(struct pm8001_hba_info *pm8001_ha)
{
	struct pci_dev *pdev;

	pdev = pm8001_ha->pdev;

#ifdef PM8001_USE_MSIX
	if (pci_find_capability(pdev, PCI_CAP_ID_MSIX))
		return pm8001_setup_msix(pm8001_ha);
	pm8001_dbg(pm8001_ha, INIT, "MSIX not supported!!!\n");
#endif
	return 0;
}

/**
 * pm8001_request_irq - register interrupt
 * @pm8001_ha: our ha struct.
 */
static u32 pm8001_request_irq(struct pm8001_hba_info *pm8001_ha)
{
	struct pci_dev *pdev;
	int rc;

	pdev = pm8001_ha->pdev;

#ifdef PM8001_USE_MSIX
	if (pdev->msix_cap && pci_msi_enabled())
		return pm8001_request_msix(pm8001_ha);
	else {
		pm8001_dbg(pm8001_ha, INIT, "MSIX not supported!!!\n");
		goto intx;
	}
#endif

intx:
	/* initialize the INT-X interrupt */
	pm8001_ha->irq_vector[0].irq_id = 0;
	pm8001_ha->irq_vector[0].drv_inst = pm8001_ha;
	rc = request_irq(pdev->irq, pm8001_interrupt_handler_intx, IRQF_SHARED,
		pm8001_ha->name, SHOST_TO_SAS_HA(pm8001_ha->shost));
	return rc;
}

/**
 * pm8001_pci_probe - probe supported device
 * @pdev: pci device which kernel has been prepared for.
 * @ent: pci device id
 *
 * This function is the main initialization function, when register a new
 * pci driver it is invoked, all struct and hardware initialization should be
 * done here, also, register interrupt.
 */
static int pm8001_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *ent)
{
	unsigned int rc;
	u32	pci_reg;
	u8	i = 0;
	struct pm8001_hba_info *pm8001_ha;
	struct Scsi_Host *shost = NULL;
	const struct pm8001_chip_info *chip;
	struct sas_ha_struct *sha;

	dev_printk(KERN_INFO, &pdev->dev,
		"pm80xx: driver version %s\n", DRV_VERSION);
	rc = pci_enable_device(pdev);
	if (rc)
		goto err_out_enable;
	pci_set_master(pdev);
	/*
	 * Enable pci slot busmaster by setting pci command register.
	 * This is required by FW for Cyclone card.
	 */

	pci_read_config_dword(pdev, PCI_COMMAND, &pci_reg);
	pci_reg |= 0x157;
	pci_write_config_dword(pdev, PCI_COMMAND, pci_reg);
	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc)
		goto err_out_disable;
	rc = pci_go_44(pdev);
	if (rc)
		goto err_out_regions;

	shost = scsi_host_alloc(&pm8001_sht, sizeof(void *));
	if (!shost) {
		rc = -ENOMEM;
		goto err_out_regions;
	}
	chip = &pm8001_chips[ent->driver_data];
	sha = kzalloc(sizeof(struct sas_ha_struct), GFP_KERNEL);
	if (!sha) {
		rc = -ENOMEM;
		goto err_out_free_host;
	}
	SHOST_TO_SAS_HA(shost) = sha;

	rc = pm8001_prep_sas_ha_init(shost, chip);
	if (rc) {
		rc = -ENOMEM;
		goto err_out_free;
	}
	pci_set_drvdata(pdev, SHOST_TO_SAS_HA(shost));
	/* ent->driver variable is used to differentiate between controllers */
	pm8001_ha = pm8001_pci_alloc(pdev, ent, shost);
	if (!pm8001_ha) {
		rc = -ENOMEM;
		goto err_out_free;
	}

	PM8001_CHIP_DISP->chip_soft_rst(pm8001_ha);
	rc = PM8001_CHIP_DISP->chip_init(pm8001_ha);
	if (rc) {
		pm8001_dbg(pm8001_ha, FAIL,
			   "chip_init failed [ret: %d]\n", rc);
		goto err_out_ha_free;
	}

	rc = pm8001_init_ccb_tag(pm8001_ha);
	if (rc)
		goto err_out_enable;


	PM8001_CHIP_DISP->chip_post_init(pm8001_ha);

	if (pm8001_ha->number_of_intr > 1) {
		shost->nr_hw_queues = pm8001_ha->number_of_intr - 1;
		/*
		 * For now, ensure we're not sent too many commands by setting
		 * host_tagset. This is also required if we start using request
		 * tag.
		 */
		shost->host_tagset = 1;
	}

	rc = scsi_add_host(shost, &pdev->dev);
	if (rc)
		goto err_out_ha_free;

	PM8001_CHIP_DISP->interrupt_enable(pm8001_ha, 0);
	if (pm8001_ha->chip_id != chip_8001) {
		for (i = 1; i < pm8001_ha->number_of_intr; i++)
			PM8001_CHIP_DISP->interrupt_enable(pm8001_ha, i);
		/* setup thermal configuration. */
		pm80xx_set_thermal_config(pm8001_ha);
	}

	pm8001_init_sas_add(pm8001_ha);
	/* phy setting support for motherboard controller */
	rc = pm8001_configure_phy_settings(pm8001_ha);
	if (rc)
		goto err_out_shost;

	pm8001_post_sas_ha_init(shost, chip);
	rc = sas_register_ha(SHOST_TO_SAS_HA(shost));
	if (rc) {
		pm8001_dbg(pm8001_ha, FAIL,
			   "sas_register_ha failed [ret: %d]\n", rc);
		goto err_out_shost;
	}
	list_add_tail(&pm8001_ha->list, &hba_list);
	pm8001_ha->flags = PM8001F_RUN_TIME;
	scsi_scan_host(pm8001_ha->shost);
	return 0;

err_out_shost:
	scsi_remove_host(pm8001_ha->shost);
err_out_ha_free:
	pm8001_free(pm8001_ha);
err_out_free:
	kfree(sha);
err_out_free_host:
	scsi_host_put(shost);
err_out_regions:
	pci_release_regions(pdev);
err_out_disable:
	pci_disable_device(pdev);
err_out_enable:
	return rc;
}

/**
 * pm8001_init_ccb_tag - allocate memory to CCB and tag.
 * @pm8001_ha: our hba card information.
 */
static int pm8001_init_ccb_tag(struct pm8001_hba_info *pm8001_ha)
{
	struct Scsi_Host *shost = pm8001_ha->shost;
	struct device *dev = pm8001_ha->dev;
	u32 max_out_io, ccb_count;
	int i;

	max_out_io = pm8001_ha->main_cfg_tbl.pm80xx_tbl.max_out_io;
	ccb_count = min_t(int, PM8001_MAX_CCB, max_out_io);

	shost->can_queue = ccb_count - PM8001_RESERVE_SLOT;

	pm8001_ha->rsvd_tags = bitmap_zalloc(PM8001_RESERVE_SLOT, GFP_KERNEL);
	if (!pm8001_ha->rsvd_tags)
		goto err_out;

	/* Memory region for ccb_info*/
	pm8001_ha->ccb_count = ccb_count;
	pm8001_ha->ccb_info =
		kcalloc(ccb_count, sizeof(struct pm8001_ccb_info), GFP_KERNEL);
	if (!pm8001_ha->ccb_info) {
		pm8001_dbg(pm8001_ha, FAIL,
			   "Unable to allocate memory for ccb\n");
		goto err_out_noccb;
	}
	for (i = 0; i < ccb_count; i++) {
		pm8001_ha->ccb_info[i].buf_prd = dma_alloc_coherent(dev,
				sizeof(struct pm8001_prd) * PM8001_MAX_DMA_SG,
				&pm8001_ha->ccb_info[i].ccb_dma_handle,
				GFP_KERNEL);
		if (!pm8001_ha->ccb_info[i].buf_prd) {
			pm8001_dbg(pm8001_ha, FAIL,
				   "ccb prd memory allocation error\n");
			goto err_out;
		}
		pm8001_ha->ccb_info[i].task = NULL;
		pm8001_ha->ccb_info[i].ccb_tag = PM8001_INVALID_TAG;
		pm8001_ha->ccb_info[i].device = NULL;
	}

	return 0;

err_out_noccb:
	kfree(pm8001_ha->devices);
err_out:
	return -ENOMEM;
}

static void pm8001_pci_remove(struct pci_dev *pdev)
{
	struct sas_ha_struct *sha = pci_get_drvdata(pdev);
	struct pm8001_hba_info *pm8001_ha;
	int i, j;
	pm8001_ha = sha->lldd_ha;
	sas_unregister_ha(sha);
	sas_remove_host(pm8001_ha->shost);
	list_del(&pm8001_ha->list);
	PM8001_CHIP_DISP->interrupt_disable(pm8001_ha, 0xFF);
	PM8001_CHIP_DISP->chip_soft_rst(pm8001_ha);

#ifdef PM8001_USE_MSIX
	for (i = 0; i < pm8001_ha->number_of_intr; i++)
		synchronize_irq(pci_irq_vector(pdev, i));
	for (i = 0; i < pm8001_ha->number_of_intr; i++)
		free_irq(pci_irq_vector(pdev, i), &pm8001_ha->irq_vector[i]);
	pci_free_irq_vectors(pdev);
#else
	free_irq(pm8001_ha->irq, sha);
#endif
#ifdef PM8001_USE_TASKLET
	/* For non-msix and msix interrupts */
	if ((!pdev->msix_cap || !pci_msi_enabled()) ||
	    (pm8001_ha->chip_id == chip_8001))
		tasklet_kill(&pm8001_ha->tasklet[0]);
	else
		for (j = 0; j < PM8001_MAX_MSIX_VEC; j++)
			tasklet_kill(&pm8001_ha->tasklet[j]);
#endif
	scsi_host_put(pm8001_ha->shost);

	for (i = 0; i < pm8001_ha->ccb_count; i++) {
		dma_free_coherent(&pm8001_ha->pdev->dev,
			sizeof(struct pm8001_prd) * PM8001_MAX_DMA_SG,
			pm8001_ha->ccb_info[i].buf_prd,
			pm8001_ha->ccb_info[i].ccb_dma_handle);
	}
	kfree(pm8001_ha->ccb_info);
	kfree(pm8001_ha->devices);

	pm8001_free(pm8001_ha);
	kfree(sha->sas_phy);
	kfree(sha->sas_port);
	kfree(sha);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

/**
 * pm8001_pci_suspend - power management suspend main entry point
 * @dev: Device struct
 *
 * Return: 0 on success, anything else on error.
 */
static int __maybe_unused pm8001_pci_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct sas_ha_struct *sha = pci_get_drvdata(pdev);
	struct pm8001_hba_info *pm8001_ha = sha->lldd_ha;
	int  i, j;
	sas_suspend_ha(sha);
	flush_workqueue(pm8001_wq);
	scsi_block_requests(pm8001_ha->shost);
	if (!pdev->pm_cap) {
		dev_err(dev, " PCI PM not supported\n");
		return -ENODEV;
	}
	PM8001_CHIP_DISP->interrupt_disable(pm8001_ha, 0xFF);
	PM8001_CHIP_DISP->chip_soft_rst(pm8001_ha);
#ifdef PM8001_USE_MSIX
	for (i = 0; i < pm8001_ha->number_of_intr; i++)
		synchronize_irq(pci_irq_vector(pdev, i));
	for (i = 0; i < pm8001_ha->number_of_intr; i++)
		free_irq(pci_irq_vector(pdev, i), &pm8001_ha->irq_vector[i]);
	pci_free_irq_vectors(pdev);
#else
	free_irq(pm8001_ha->irq, sha);
#endif
#ifdef PM8001_USE_TASKLET
	/* For non-msix and msix interrupts */
	if ((!pdev->msix_cap || !pci_msi_enabled()) ||
	    (pm8001_ha->chip_id == chip_8001))
		tasklet_kill(&pm8001_ha->tasklet[0]);
	else
		for (j = 0; j < PM8001_MAX_MSIX_VEC; j++)
			tasklet_kill(&pm8001_ha->tasklet[j]);
#endif
	pm8001_info(pm8001_ha, "pdev=0x%p, slot=%s, entering "
		      "suspended state\n", pdev,
		      pm8001_ha->name);
	return 0;
}

/**
 * pm8001_pci_resume - power management resume main entry point
 * @dev: Device struct
 *
 * Return: 0 on success, anything else on error.
 */
static int __maybe_unused pm8001_pci_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct sas_ha_struct *sha = pci_get_drvdata(pdev);
	struct pm8001_hba_info *pm8001_ha;
	int rc;
	u8 i = 0, j;
	DECLARE_COMPLETION_ONSTACK(completion);

	pm8001_ha = sha->lldd_ha;

	pm8001_info(pm8001_ha,
		    "pdev=0x%p, slot=%s, resuming from previous operating state [D%d]\n",
		    pdev, pm8001_ha->name, pdev->current_state);

	rc = pci_go_44(pdev);
	if (rc)
		goto err_out_disable;
	sas_prep_resume_ha(sha);
	/* chip soft rst only for spc */
	if (pm8001_ha->chip_id == chip_8001) {
		PM8001_CHIP_DISP->chip_soft_rst(pm8001_ha);
		pm8001_dbg(pm8001_ha, INIT, "chip soft reset successful\n");
	}
	rc = PM8001_CHIP_DISP->chip_init(pm8001_ha);
	if (rc)
		goto err_out_disable;

	/* disable all the interrupt bits */
	PM8001_CHIP_DISP->interrupt_disable(pm8001_ha, 0xFF);

	rc = pm8001_request_irq(pm8001_ha);
	if (rc)
		goto err_out_disable;
#ifdef PM8001_USE_TASKLET
	/*  Tasklet for non msi-x interrupt handler */
	if ((!pdev->msix_cap || !pci_msi_enabled()) ||
	    (pm8001_ha->chip_id == chip_8001))
		tasklet_init(&pm8001_ha->tasklet[0], pm8001_tasklet,
			(unsigned long)&(pm8001_ha->irq_vector[0]));
	else
		for (j = 0; j < PM8001_MAX_MSIX_VEC; j++)
			tasklet_init(&pm8001_ha->tasklet[j], pm8001_tasklet,
				(unsigned long)&(pm8001_ha->irq_vector[j]));
#endif
	PM8001_CHIP_DISP->interrupt_enable(pm8001_ha, 0);
	if (pm8001_ha->chip_id != chip_8001) {
		for (i = 1; i < pm8001_ha->number_of_intr; i++)
			PM8001_CHIP_DISP->interrupt_enable(pm8001_ha, i);
	}

	/* Chip documentation for the 8070 and 8072 SPCv    */
	/* states that a 500ms minimum delay is required    */
	/* before issuing commands. Otherwise, the firmware */
	/* will enter an unrecoverable state.               */

	if (pm8001_ha->chip_id == chip_8070 ||
		pm8001_ha->chip_id == chip_8072) {
		mdelay(500);
	}

	/* Spin up the PHYs */

	pm8001_ha->flags = PM8001F_RUN_TIME;
	for (i = 0; i < pm8001_ha->chip->n_phy; i++) {
		pm8001_ha->phy[i].enable_completion = &completion;
		PM8001_CHIP_DISP->phy_start_req(pm8001_ha, i);
		wait_for_completion(&completion);
	}
	sas_resume_ha(sha);
	return 0;

err_out_disable:
	scsi_remove_host(pm8001_ha->shost);

	return rc;
}

/* update of pci device, vendor id and driver data with
 * unique value for each of the controller
 */
static struct pci_device_id pm8001_pci_table[] = {
	{ PCI_VDEVICE(PMC_Sierra, 0x8001), chip_8001 },
	{ PCI_VDEVICE(PMC_Sierra, 0x8006), chip_8006 },
	{ PCI_VDEVICE(ADAPTEC2, 0x8006), chip_8006 },
	{ PCI_VDEVICE(ATTO, 0x0042), chip_8001 },
	/* Support for SPC/SPCv/SPCve controllers */
	{ PCI_VDEVICE(ADAPTEC2, 0x8001), chip_8001 },
	{ PCI_VDEVICE(PMC_Sierra, 0x8008), chip_8008 },
	{ PCI_VDEVICE(ADAPTEC2, 0x8008), chip_8008 },
	{ PCI_VDEVICE(PMC_Sierra, 0x8018), chip_8018 },
	{ PCI_VDEVICE(ADAPTEC2, 0x8018), chip_8018 },
	{ PCI_VDEVICE(PMC_Sierra, 0x8009), chip_8009 },
	{ PCI_VDEVICE(ADAPTEC2, 0x8009), chip_8009 },
	{ PCI_VDEVICE(PMC_Sierra, 0x8019), chip_8019 },
	{ PCI_VDEVICE(ADAPTEC2, 0x8019), chip_8019 },
	{ PCI_VDEVICE(PMC_Sierra, 0x8074), chip_8074 },
	{ PCI_VDEVICE(ADAPTEC2, 0x8074), chip_8074 },
	{ PCI_VDEVICE(PMC_Sierra, 0x8076), chip_8076 },
	{ PCI_VDEVICE(ADAPTEC2, 0x8076), chip_8076 },
	{ PCI_VDEVICE(PMC_Sierra, 0x8077), chip_8077 },
	{ PCI_VDEVICE(ADAPTEC2, 0x8077), chip_8077 },
	{ PCI_VENDOR_ID_ADAPTEC2, 0x8081,
		PCI_VENDOR_ID_ADAPTEC2, 0x0400, 0, 0, chip_8001 },
	{ PCI_VENDOR_ID_ADAPTEC2, 0x8081,
		PCI_VENDOR_ID_ADAPTEC2, 0x0800, 0, 0, chip_8001 },
	{ PCI_VENDOR_ID_ADAPTEC2, 0x8088,
		PCI_VENDOR_ID_ADAPTEC2, 0x0008, 0, 0, chip_8008 },
	{ PCI_VENDOR_ID_ADAPTEC2, 0x8088,
		PCI_VENDOR_ID_ADAPTEC2, 0x0800, 0, 0, chip_8008 },
	{ PCI_VENDOR_ID_ADAPTEC2, 0x8089,
		PCI_VENDOR_ID_ADAPTEC2, 0x0008, 0, 0, chip_8009 },
	{ PCI_VENDOR_ID_ADAPTEC2, 0x8089,
		PCI_VENDOR_ID_ADAPTEC2, 0x0800, 0, 0, chip_8009 },
	{ PCI_VENDOR_ID_ADAPTEC2, 0x8088,
		PCI_VENDOR_ID_ADAPTEC2, 0x0016, 0, 0, chip_8018 },
	{ PCI_VENDOR_ID_ADAPTEC2, 0x8088,
		PCI_VENDOR_ID_ADAPTEC2, 0x1600, 0, 0, chip_8018 },
	{ PCI_VENDOR_ID_ADAPTEC2, 0x8089,
		PCI_VENDOR_ID_ADAPTEC2, 0x0016, 0, 0, chip_8019 },
	{ PCI_VENDOR_ID_ADAPTEC2, 0x8089,
		PCI_VENDOR_ID_ADAPTEC2, 0x1600, 0, 0, chip_8019 },
	{ PCI_VENDOR_ID_ADAPTEC2, 0x8074,
		PCI_VENDOR_ID_ADAPTEC2, 0x0800, 0, 0, chip_8074 },
	{ PCI_VENDOR_ID_ADAPTEC2, 0x8076,
		PCI_VENDOR_ID_ADAPTEC2, 0x1600, 0, 0, chip_8076 },
	{ PCI_VENDOR_ID_ADAPTEC2, 0x8077,
		PCI_VENDOR_ID_ADAPTEC2, 0x1600, 0, 0, chip_8077 },
	{ PCI_VENDOR_ID_ADAPTEC2, 0x8074,
		PCI_VENDOR_ID_ADAPTEC2, 0x0008, 0, 0, chip_8074 },
	{ PCI_VENDOR_ID_ADAPTEC2, 0x8076,
		PCI_VENDOR_ID_ADAPTEC2, 0x0016, 0, 0, chip_8076 },
	{ PCI_VENDOR_ID_ADAPTEC2, 0x8077,
		PCI_VENDOR_ID_ADAPTEC2, 0x0016, 0, 0, chip_8077 },
	{ PCI_VENDOR_ID_ADAPTEC2, 0x8076,
		PCI_VENDOR_ID_ADAPTEC2, 0x0808, 0, 0, chip_8076 },
	{ PCI_VENDOR_ID_ADAPTEC2, 0x8077,
		PCI_VENDOR_ID_ADAPTEC2, 0x0808, 0, 0, chip_8077 },
	{ PCI_VENDOR_ID_ADAPTEC2, 0x8074,
		PCI_VENDOR_ID_ADAPTEC2, 0x0404, 0, 0, chip_8074 },
	{ PCI_VENDOR_ID_ATTO, 0x8070,
		PCI_VENDOR_ID_ATTO, 0x0070, 0, 0, chip_8070 },
	{ PCI_VENDOR_ID_ATTO, 0x8070,
		PCI_VENDOR_ID_ATTO, 0x0071, 0, 0, chip_8070 },
	{ PCI_VENDOR_ID_ATTO, 0x8072,
		PCI_VENDOR_ID_ATTO, 0x0072, 0, 0, chip_8072 },
	{ PCI_VENDOR_ID_ATTO, 0x8072,
		PCI_VENDOR_ID_ATTO, 0x0073, 0, 0, chip_8072 },
	{ PCI_VENDOR_ID_ATTO, 0x8070,
		PCI_VENDOR_ID_ATTO, 0x0080, 0, 0, chip_8070 },
	{ PCI_VENDOR_ID_ATTO, 0x8072,
		PCI_VENDOR_ID_ATTO, 0x0081, 0, 0, chip_8072 },
	{ PCI_VENDOR_ID_ATTO, 0x8072,
		PCI_VENDOR_ID_ATTO, 0x0082, 0, 0, chip_8072 },
	{} /* terminate list */
};

static SIMPLE_DEV_PM_OPS(pm8001_pci_pm_ops,
			 pm8001_pci_suspend,
			 pm8001_pci_resume);

static struct pci_driver pm8001_pci_driver = {
	.name		= DRV_NAME,
	.id_table	= pm8001_pci_table,
	.probe		= pm8001_pci_probe,
	.remove		= pm8001_pci_remove,
	.driver.pm	= &pm8001_pci_pm_ops,
};

/**
 *	pm8001_init - initialize scsi transport template
 */
static int __init pm8001_init(void)
{
	int rc = -ENOMEM;

	pm8001_wq = alloc_workqueue("pm80xx", 0, 0);
	if (!pm8001_wq)
		goto err;

	pm8001_id = 0;
	pm8001_stt = sas_domain_attach_transport(&pm8001_transport_ops);
	if (!pm8001_stt)
		goto err_wq;
	rc = pci_register_driver(&pm8001_pci_driver);
	if (rc)
		goto err_tp;
	return 0;

err_tp:
	sas_release_transport(pm8001_stt);
err_wq:
	destroy_workqueue(pm8001_wq);
err:
	return rc;
}

static void __exit pm8001_exit(void)
{
	pci_unregister_driver(&pm8001_pci_driver);
	sas_release_transport(pm8001_stt);
	destroy_workqueue(pm8001_wq);
}

module_init(pm8001_init);
module_exit(pm8001_exit);

MODULE_AUTHOR("Jack Wang <jack_wang@usish.com>");
MODULE_AUTHOR("Anand Kumar Santhanam <AnandKumar.Santhanam@pmcs.com>");
MODULE_AUTHOR("Sangeetha Gnanasekaran <Sangeetha.Gnanasekaran@pmcs.com>");
MODULE_AUTHOR("Nikith Ganigarakoppal <Nikith.Ganigarakoppal@pmcs.com>");
MODULE_DESCRIPTION(
		"PMC-Sierra PM8001/8006/8081/8088/8089/8074/8076/8077/8070/8072 "
		"SAS/SATA controller driver");
MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, pm8001_pci_table);

