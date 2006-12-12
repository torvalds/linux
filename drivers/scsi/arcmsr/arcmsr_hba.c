/*
*******************************************************************************
**        O.S   : Linux
**   FILE NAME  : arcmsr_hba.c
**        BY    : Erich Chen
**   Description: SCSI RAID Device Driver for
**                ARECA RAID Host adapter
*******************************************************************************
** Copyright (C) 2002 - 2005, Areca Technology Corporation All rights reserved
**
**     Web site: www.areca.com.tw
**       E-mail: erich@areca.com.tw
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License version 2 as
** published by the Free Software Foundation.
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
*******************************************************************************
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES(INCLUDING,BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION)HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE)ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************
** For history of changes, see Documentation/scsi/ChangeLog.arcmsr
**     Firmware Specification, see Documentation/scsi/arcmsr_spec.txt
*******************************************************************************
*/
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/spinlock.h>
#include <linux/pci_ids.h>
#include <linux/interrupt.h>
#include <linux/moduleparam.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/timer.h>
#include <linux/pci.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsicam.h>
#include "arcmsr.h"

MODULE_AUTHOR("Erich Chen <erich@areca.com.tw>");
MODULE_DESCRIPTION("ARECA (ARC11xx/12xx) SATA RAID HOST Adapter");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(ARCMSR_DRIVER_VERSION);

static int arcmsr_iop_message_xfer(struct AdapterControlBlock *acb, struct scsi_cmnd *cmd);
static int arcmsr_abort(struct scsi_cmnd *);
static int arcmsr_bus_reset(struct scsi_cmnd *);
static int arcmsr_bios_param(struct scsi_device *sdev,
				struct block_device *bdev, sector_t capacity, int *info);
static int arcmsr_queue_command(struct scsi_cmnd * cmd,
				void (*done) (struct scsi_cmnd *));
static int arcmsr_probe(struct pci_dev *pdev,
				const struct pci_device_id *id);
static void arcmsr_remove(struct pci_dev *pdev);
static void arcmsr_shutdown(struct pci_dev *pdev);
static void arcmsr_iop_init(struct AdapterControlBlock *acb);
static void arcmsr_free_ccb_pool(struct AdapterControlBlock *acb);
static void arcmsr_stop_adapter_bgrb(struct AdapterControlBlock *acb);
static void arcmsr_flush_adapter_cache(struct AdapterControlBlock *acb);
static uint8_t arcmsr_wait_msgint_ready(struct AdapterControlBlock *acb);
static const char *arcmsr_info(struct Scsi_Host *);
static irqreturn_t arcmsr_interrupt(struct AdapterControlBlock *acb);

static int arcmsr_adjust_disk_queue_depth(struct scsi_device *sdev, int queue_depth)
{
	if (queue_depth > ARCMSR_MAX_CMD_PERLUN)
		queue_depth = ARCMSR_MAX_CMD_PERLUN;
	scsi_adjust_queue_depth(sdev, MSG_ORDERED_TAG, queue_depth);
	return queue_depth;
}

static struct scsi_host_template arcmsr_scsi_host_template = {
	.module			= THIS_MODULE,
	.name			= "ARCMSR ARECA SATA RAID HOST Adapter" ARCMSR_DRIVER_VERSION,
	.info			= arcmsr_info,
	.queuecommand		= arcmsr_queue_command,
	.eh_abort_handler	= arcmsr_abort,
	.eh_bus_reset_handler	= arcmsr_bus_reset,
	.bios_param		= arcmsr_bios_param,
	.change_queue_depth	= arcmsr_adjust_disk_queue_depth,
	.can_queue		= ARCMSR_MAX_OUTSTANDING_CMD,
	.this_id		= ARCMSR_SCSI_INITIATOR_ID,
	.sg_tablesize		= ARCMSR_MAX_SG_ENTRIES,
	.max_sectors    	= ARCMSR_MAX_XFER_SECTORS,
	.cmd_per_lun		= ARCMSR_MAX_CMD_PERLUN,
	.use_clustering		= ENABLE_CLUSTERING,
	.shost_attrs		= arcmsr_host_attrs,
};

static struct pci_device_id arcmsr_device_id_table[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_ARECA, PCI_DEVICE_ID_ARECA_1110)},
	{PCI_DEVICE(PCI_VENDOR_ID_ARECA, PCI_DEVICE_ID_ARECA_1120)},
	{PCI_DEVICE(PCI_VENDOR_ID_ARECA, PCI_DEVICE_ID_ARECA_1130)},
	{PCI_DEVICE(PCI_VENDOR_ID_ARECA, PCI_DEVICE_ID_ARECA_1160)},
	{PCI_DEVICE(PCI_VENDOR_ID_ARECA, PCI_DEVICE_ID_ARECA_1170)},
	{PCI_DEVICE(PCI_VENDOR_ID_ARECA, PCI_DEVICE_ID_ARECA_1210)},
	{PCI_DEVICE(PCI_VENDOR_ID_ARECA, PCI_DEVICE_ID_ARECA_1220)},
	{PCI_DEVICE(PCI_VENDOR_ID_ARECA, PCI_DEVICE_ID_ARECA_1230)},
	{PCI_DEVICE(PCI_VENDOR_ID_ARECA, PCI_DEVICE_ID_ARECA_1260)},
	{PCI_DEVICE(PCI_VENDOR_ID_ARECA, PCI_DEVICE_ID_ARECA_1270)},
	{PCI_DEVICE(PCI_VENDOR_ID_ARECA, PCI_DEVICE_ID_ARECA_1280)},
	{PCI_DEVICE(PCI_VENDOR_ID_ARECA, PCI_DEVICE_ID_ARECA_1380)},
	{PCI_DEVICE(PCI_VENDOR_ID_ARECA, PCI_DEVICE_ID_ARECA_1381)},
	{PCI_DEVICE(PCI_VENDOR_ID_ARECA, PCI_DEVICE_ID_ARECA_1680)},
	{PCI_DEVICE(PCI_VENDOR_ID_ARECA, PCI_DEVICE_ID_ARECA_1681)},
	{0, 0}, /* Terminating entry */
};
MODULE_DEVICE_TABLE(pci, arcmsr_device_id_table);
static struct pci_driver arcmsr_pci_driver = {
	.name			= "arcmsr",
	.id_table		= arcmsr_device_id_table,
	.probe			= arcmsr_probe,
	.remove			= arcmsr_remove,
	.shutdown		= arcmsr_shutdown
};

static irqreturn_t arcmsr_do_interrupt(int irq, void *dev_id)
{
	irqreturn_t handle_state;
	struct AdapterControlBlock *acb;
	unsigned long flags;

	acb = (struct AdapterControlBlock *)dev_id;

	spin_lock_irqsave(acb->host->host_lock, flags);
	handle_state = arcmsr_interrupt(acb);
	spin_unlock_irqrestore(acb->host->host_lock, flags);
	return handle_state;
}

static int arcmsr_bios_param(struct scsi_device *sdev,
		struct block_device *bdev, sector_t capacity, int *geom)
{
	int ret, heads, sectors, cylinders, total_capacity;
	unsigned char *buffer;/* return copy of block device's partition table */

	buffer = scsi_bios_ptable(bdev);
	if (buffer) {
		ret = scsi_partsize(buffer, capacity, &geom[2], &geom[0], &geom[1]);
		kfree(buffer);
		if (ret != -1)
			return ret;
	}
	total_capacity = capacity;
	heads = 64;
	sectors = 32;
	cylinders = total_capacity / (heads * sectors);
	if (cylinders > 1024) {
		heads = 255;
		sectors = 63;
		cylinders = total_capacity / (heads * sectors);
	}
	geom[0] = heads;
	geom[1] = sectors;
	geom[2] = cylinders;
	return 0;
}

static int arcmsr_alloc_ccb_pool(struct AdapterControlBlock *acb)
{
	struct pci_dev *pdev = acb->pdev;
	struct MessageUnit __iomem *reg = acb->pmu;
	u32 ccb_phyaddr_hi32;
	void *dma_coherent;
	dma_addr_t dma_coherent_handle, dma_addr;
	struct CommandControlBlock *ccb_tmp;
	int i, j;

	dma_coherent = dma_alloc_coherent(&pdev->dev,
			ARCMSR_MAX_FREECCB_NUM *
			sizeof (struct CommandControlBlock) + 0x20,
			&dma_coherent_handle, GFP_KERNEL);
	if (!dma_coherent)
		return -ENOMEM;

	acb->dma_coherent = dma_coherent;
	acb->dma_coherent_handle = dma_coherent_handle;

	if (((unsigned long)dma_coherent & 0x1F)) {
		dma_coherent = dma_coherent +
			(0x20 - ((unsigned long)dma_coherent & 0x1F));
		dma_coherent_handle = dma_coherent_handle +
			(0x20 - ((unsigned long)dma_coherent_handle & 0x1F));
	}

	dma_addr = dma_coherent_handle;
	ccb_tmp = (struct CommandControlBlock *)dma_coherent;
	for (i = 0; i < ARCMSR_MAX_FREECCB_NUM; i++) {
		ccb_tmp->cdb_shifted_phyaddr = dma_addr >> 5;
		ccb_tmp->acb = acb;
		acb->pccb_pool[i] = ccb_tmp;
		list_add_tail(&ccb_tmp->list, &acb->ccb_free_list);
		dma_addr = dma_addr + sizeof (struct CommandControlBlock);
		ccb_tmp++;
	}

	acb->vir2phy_offset = (unsigned long)ccb_tmp -
			      (unsigned long)dma_addr;
	for (i = 0; i < ARCMSR_MAX_TARGETID; i++)
		for (j = 0; j < ARCMSR_MAX_TARGETLUN; j++)
			acb->devstate[i][j] = ARECA_RAID_GOOD;

	/*
	** here we need to tell iop 331 our ccb_tmp.HighPart
	** if ccb_tmp.HighPart is not zero
	*/
	ccb_phyaddr_hi32 = (uint32_t) ((dma_coherent_handle >> 16) >> 16);
	if (ccb_phyaddr_hi32 != 0) {
		writel(ARCMSR_SIGNATURE_SET_CONFIG, &reg->message_rwbuffer[0]);
		writel(ccb_phyaddr_hi32, &reg->message_rwbuffer[1]);
		writel(ARCMSR_INBOUND_MESG0_SET_CONFIG, &reg->inbound_msgaddr0);
		if (arcmsr_wait_msgint_ready(acb))
			printk(KERN_NOTICE "arcmsr%d: "
			       "'set ccb high part physical address' timeout\n",
				acb->host->host_no);
	}

	writel(readl(&reg->outbound_intmask) |
			ARCMSR_MU_OUTBOUND_ALL_INTMASKENABLE,
	       &reg->outbound_intmask);
	return 0;
}

static int arcmsr_probe(struct pci_dev *pdev,
	const struct pci_device_id *id)
{
	struct Scsi_Host *host;
	struct AdapterControlBlock *acb;
	uint8_t bus, dev_fun;
	int error;

	error = pci_enable_device(pdev);
	if (error)
		goto out;
	pci_set_master(pdev);

	host = scsi_host_alloc(&arcmsr_scsi_host_template,
			sizeof(struct AdapterControlBlock));
	if (!host) {
		error = -ENOMEM;
		goto out_disable_device;
	}
	acb = (struct AdapterControlBlock *)host->hostdata;
	memset(acb, 0, sizeof (struct AdapterControlBlock));

	error = pci_set_dma_mask(pdev, DMA_64BIT_MASK);
	if (error) {
		error = pci_set_dma_mask(pdev, DMA_32BIT_MASK);
		if (error) {
			printk(KERN_WARNING
			       "scsi%d: No suitable DMA mask available\n",
			       host->host_no);
			goto out_host_put;
		}
	}
	bus = pdev->bus->number;
	dev_fun = pdev->devfn;
	acb->host = host;
	acb->pdev = pdev;
	host->max_sectors = ARCMSR_MAX_XFER_SECTORS;
	host->max_lun = ARCMSR_MAX_TARGETLUN;
	host->max_id = ARCMSR_MAX_TARGETID;/*16:8*/
	host->max_cmd_len = 16;    /*this is issue of 64bit LBA, over 2T byte*/
	host->sg_tablesize = ARCMSR_MAX_SG_ENTRIES;
	host->can_queue = ARCMSR_MAX_FREECCB_NUM; /* max simultaneous cmds */
	host->cmd_per_lun = ARCMSR_MAX_CMD_PERLUN;
	host->this_id = ARCMSR_SCSI_INITIATOR_ID;
	host->unique_id = (bus << 8) | dev_fun;
	host->irq = pdev->irq;
	error = pci_request_regions(pdev, "arcmsr");
	if (error)
		goto out_host_put;

	acb->pmu = ioremap(pci_resource_start(pdev, 0),
			   pci_resource_len(pdev, 0));
	if (!acb->pmu) {
		printk(KERN_NOTICE "arcmsr%d: memory"
			" mapping region fail \n", acb->host->host_no);
		goto out_release_regions;
	}
	acb->acb_flags |= (ACB_F_MESSAGE_WQBUFFER_CLEARED |
			   ACB_F_MESSAGE_RQBUFFER_CLEARED |
			   ACB_F_MESSAGE_WQBUFFER_READED);
	acb->acb_flags &= ~ACB_F_SCSISTOPADAPTER;
	INIT_LIST_HEAD(&acb->ccb_free_list);

	error = arcmsr_alloc_ccb_pool(acb);
	if (error)
		goto out_iounmap;

	error = request_irq(pdev->irq, arcmsr_do_interrupt,
			SA_INTERRUPT | SA_SHIRQ, "arcmsr", acb);
	if (error)
		goto out_free_ccb_pool;

	arcmsr_iop_init(acb);
	pci_set_drvdata(pdev, host);

	error = scsi_add_host(host, &pdev->dev);
	if (error)
		goto out_free_irq;

	error = arcmsr_alloc_sysfs_attr(acb);
	if (error)
		goto out_free_sysfs;

	scsi_scan_host(host);
	return 0;
 out_free_sysfs:
 out_free_irq:
	free_irq(pdev->irq, acb);
 out_free_ccb_pool:
	arcmsr_free_ccb_pool(acb);
 out_iounmap:
	iounmap(acb->pmu);
 out_release_regions:
	pci_release_regions(pdev);
 out_host_put:
	scsi_host_put(host);
 out_disable_device:
	pci_disable_device(pdev);
 out:
	return error;
}

static void arcmsr_abort_allcmd(struct AdapterControlBlock *acb)
{
	struct MessageUnit __iomem *reg = acb->pmu;

	writel(ARCMSR_INBOUND_MESG0_ABORT_CMD, &reg->inbound_msgaddr0);
	if (arcmsr_wait_msgint_ready(acb))
		printk(KERN_NOTICE
			"arcmsr%d: wait 'abort all outstanding command' timeout \n"
			, acb->host->host_no);
}

static void arcmsr_pci_unmap_dma(struct CommandControlBlock *ccb)
{
	struct AdapterControlBlock *acb = ccb->acb;
	struct scsi_cmnd *pcmd = ccb->pcmd;

	if (pcmd->use_sg != 0) {
		struct scatterlist *sl;

		sl = (struct scatterlist *)pcmd->request_buffer;
		pci_unmap_sg(acb->pdev, sl, pcmd->use_sg, pcmd->sc_data_direction);
	}
	else if (pcmd->request_bufflen != 0)
		pci_unmap_single(acb->pdev,
			pcmd->SCp.dma_handle,
			pcmd->request_bufflen, pcmd->sc_data_direction);
}

static void arcmsr_ccb_complete(struct CommandControlBlock *ccb, int stand_flag)
{
	struct AdapterControlBlock *acb = ccb->acb;
	struct scsi_cmnd *pcmd = ccb->pcmd;

	arcmsr_pci_unmap_dma(ccb);
	if (stand_flag == 1)
		atomic_dec(&acb->ccboutstandingcount);
	ccb->startdone = ARCMSR_CCB_DONE;
	ccb->ccb_flags = 0;
	list_add_tail(&ccb->list, &acb->ccb_free_list);
	pcmd->scsi_done(pcmd);
}

static void arcmsr_remove(struct pci_dev *pdev)
{
	struct Scsi_Host *host = pci_get_drvdata(pdev);
	struct AdapterControlBlock *acb =
		(struct AdapterControlBlock *) host->hostdata;
	struct MessageUnit __iomem *reg = acb->pmu;
	int poll_count = 0;

	arcmsr_free_sysfs_attr(acb);
	scsi_remove_host(host);
	arcmsr_stop_adapter_bgrb(acb);
	arcmsr_flush_adapter_cache(acb);
	writel(readl(&reg->outbound_intmask) |
		ARCMSR_MU_OUTBOUND_ALL_INTMASKENABLE,
		&reg->outbound_intmask);
	acb->acb_flags |= ACB_F_SCSISTOPADAPTER;
	acb->acb_flags &= ~ACB_F_IOP_INITED;

	for (poll_count = 0; poll_count < 256; poll_count++) {
		if (!atomic_read(&acb->ccboutstandingcount))
			break;
		arcmsr_interrupt(acb);
		msleep(25);
	}

	if (atomic_read(&acb->ccboutstandingcount)) {
		int i;

		arcmsr_abort_allcmd(acb);
		for (i = 0; i < ARCMSR_MAX_OUTSTANDING_CMD; i++)
			readl(&reg->outbound_queueport);
		for (i = 0; i < ARCMSR_MAX_FREECCB_NUM; i++) {
			struct CommandControlBlock *ccb = acb->pccb_pool[i];
			if (ccb->startdone == ARCMSR_CCB_START) {
				ccb->startdone = ARCMSR_CCB_ABORTED;
				ccb->pcmd->result = DID_ABORT << 16;
				arcmsr_ccb_complete(ccb, 1);
			}
		}
	}

	free_irq(pdev->irq, acb);
	iounmap(acb->pmu);
	arcmsr_free_ccb_pool(acb);
	pci_release_regions(pdev);

	scsi_host_put(host);

	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
}

static void arcmsr_shutdown(struct pci_dev *pdev)
{
	struct Scsi_Host *host = pci_get_drvdata(pdev);
	struct AdapterControlBlock *acb =
		(struct AdapterControlBlock *)host->hostdata;

	arcmsr_stop_adapter_bgrb(acb);
	arcmsr_flush_adapter_cache(acb);
}

static int arcmsr_module_init(void)
{
	int error = 0;

	error = pci_register_driver(&arcmsr_pci_driver);
	return error;
}

static void arcmsr_module_exit(void)
{
	pci_unregister_driver(&arcmsr_pci_driver);
}
module_init(arcmsr_module_init);
module_exit(arcmsr_module_exit);

static u32 arcmsr_disable_outbound_ints(struct AdapterControlBlock *acb)
{
	struct MessageUnit __iomem *reg = acb->pmu;
	u32 orig_mask = readl(&reg->outbound_intmask);

	writel(orig_mask | ARCMSR_MU_OUTBOUND_ALL_INTMASKENABLE,
			&reg->outbound_intmask);
	return orig_mask;
}

static void arcmsr_enable_outbound_ints(struct AdapterControlBlock *acb,
		u32 orig_mask)
{
	struct MessageUnit __iomem *reg = acb->pmu;
	u32 mask;

	mask = orig_mask & ~(ARCMSR_MU_OUTBOUND_POSTQUEUE_INTMASKENABLE |
			     ARCMSR_MU_OUTBOUND_DOORBELL_INTMASKENABLE);
	writel(mask, &reg->outbound_intmask);
}

static void arcmsr_flush_adapter_cache(struct AdapterControlBlock *acb)
{
	struct MessageUnit __iomem *reg=acb->pmu;

	writel(ARCMSR_INBOUND_MESG0_FLUSH_CACHE, &reg->inbound_msgaddr0);
	if (arcmsr_wait_msgint_ready(acb))
		printk(KERN_NOTICE
			"arcmsr%d: wait 'flush adapter cache' timeout \n"
			, acb->host->host_no);
}

static void arcmsr_report_sense_info(struct CommandControlBlock *ccb)
{
	struct scsi_cmnd *pcmd = ccb->pcmd;
	struct SENSE_DATA *sensebuffer = (struct SENSE_DATA *)pcmd->sense_buffer;

	pcmd->result = DID_OK << 16;
	if (sensebuffer) {
		int sense_data_length =
			sizeof (struct SENSE_DATA) < sizeof (pcmd->sense_buffer)
			? sizeof (struct SENSE_DATA) : sizeof (pcmd->sense_buffer);
		memset(sensebuffer, 0, sizeof (pcmd->sense_buffer));
		memcpy(sensebuffer, ccb->arcmsr_cdb.SenseData, sense_data_length);
		sensebuffer->ErrorCode = SCSI_SENSE_CURRENT_ERRORS;
		sensebuffer->Valid = 1;
	}
}

static uint8_t arcmsr_wait_msgint_ready(struct AdapterControlBlock *acb)
{
	struct MessageUnit __iomem *reg = acb->pmu;
	uint32_t Index;
	uint8_t Retries = 0x00;

	do {
		for (Index = 0; Index < 100; Index++) {
			if (readl(&reg->outbound_intstatus)
				& ARCMSR_MU_OUTBOUND_MESSAGE0_INT) {
				writel(ARCMSR_MU_OUTBOUND_MESSAGE0_INT
					, &reg->outbound_intstatus);
				return 0x00;
			}
			msleep_interruptible(10);
		}/*max 1 seconds*/
	} while (Retries++ < 20);/*max 20 sec*/
	return 0xff;
}

static void arcmsr_build_ccb(struct AdapterControlBlock *acb,
	struct CommandControlBlock *ccb, struct scsi_cmnd *pcmd)
{
	struct ARCMSR_CDB *arcmsr_cdb = (struct ARCMSR_CDB *)&ccb->arcmsr_cdb;
	int8_t *psge = (int8_t *)&arcmsr_cdb->u;
	uint32_t address_lo, address_hi;
	int arccdbsize = 0x30;

	ccb->pcmd = pcmd;
	memset(arcmsr_cdb, 0, sizeof (struct ARCMSR_CDB));
	arcmsr_cdb->Bus = 0;
	arcmsr_cdb->TargetID = pcmd->device->id;
	arcmsr_cdb->LUN = pcmd->device->lun;
	arcmsr_cdb->Function = 1;
	arcmsr_cdb->CdbLength = (uint8_t)pcmd->cmd_len;
	arcmsr_cdb->Context = (unsigned long)arcmsr_cdb;
	memcpy(arcmsr_cdb->Cdb, pcmd->cmnd, pcmd->cmd_len);
	if (pcmd->use_sg) {
		int length, sgcount, i, cdb_sgcount = 0;
		struct scatterlist *sl;

		/* Get Scatter Gather List from scsiport. */
		sl = (struct scatterlist *) pcmd->request_buffer;
		sgcount = pci_map_sg(acb->pdev, sl, pcmd->use_sg,
				pcmd->sc_data_direction);
		/* map stor port SG list to our iop SG List. */
		for (i = 0; i < sgcount; i++) {
			/* Get the physical address of the current data pointer */
			length = cpu_to_le32(sg_dma_len(sl));
			address_lo = cpu_to_le32(dma_addr_lo32(sg_dma_address(sl)));
			address_hi = cpu_to_le32(dma_addr_hi32(sg_dma_address(sl)));
			if (address_hi == 0) {
				struct SG32ENTRY *pdma_sg = (struct SG32ENTRY *)psge;

				pdma_sg->address = address_lo;
				pdma_sg->length = length;
				psge += sizeof (struct SG32ENTRY);
				arccdbsize += sizeof (struct SG32ENTRY);
			} else {
				struct SG64ENTRY *pdma_sg = (struct SG64ENTRY *)psge;

				pdma_sg->addresshigh = address_hi;
				pdma_sg->address = address_lo;
				pdma_sg->length = length|IS_SG64_ADDR;
				psge += sizeof (struct SG64ENTRY);
				arccdbsize += sizeof (struct SG64ENTRY);
			}
			sl++;
			cdb_sgcount++;
		}
		arcmsr_cdb->sgcount = (uint8_t)cdb_sgcount;
		arcmsr_cdb->DataLength = pcmd->request_bufflen;
		if ( arccdbsize > 256)
			arcmsr_cdb->Flags |= ARCMSR_CDB_FLAG_SGL_BSIZE;
	} else if (pcmd->request_bufflen) {
		dma_addr_t dma_addr;
		dma_addr = pci_map_single(acb->pdev, pcmd->request_buffer,
				pcmd->request_bufflen, pcmd->sc_data_direction);
		pcmd->SCp.dma_handle = dma_addr;
		address_lo = cpu_to_le32(dma_addr_lo32(dma_addr));
		address_hi = cpu_to_le32(dma_addr_hi32(dma_addr));
		if (address_hi == 0) {
			struct  SG32ENTRY *pdma_sg = (struct SG32ENTRY *)psge;
			pdma_sg->address = address_lo;
			pdma_sg->length = pcmd->request_bufflen;
		} else {
			struct SG64ENTRY *pdma_sg = (struct SG64ENTRY *)psge;
			pdma_sg->addresshigh = address_hi;
			pdma_sg->address = address_lo;
			pdma_sg->length = pcmd->request_bufflen|IS_SG64_ADDR;
		}
		arcmsr_cdb->sgcount = 1;
		arcmsr_cdb->DataLength = pcmd->request_bufflen;
	}
	if (pcmd->sc_data_direction == DMA_TO_DEVICE ) {
		arcmsr_cdb->Flags |= ARCMSR_CDB_FLAG_WRITE;
		ccb->ccb_flags |= CCB_FLAG_WRITE;
	}
}

static void arcmsr_post_ccb(struct AdapterControlBlock *acb, struct CommandControlBlock *ccb)
{
	struct MessageUnit __iomem *reg = acb->pmu;
	uint32_t cdb_shifted_phyaddr = ccb->cdb_shifted_phyaddr;
	struct ARCMSR_CDB *arcmsr_cdb = (struct ARCMSR_CDB *)&ccb->arcmsr_cdb;

	atomic_inc(&acb->ccboutstandingcount);
	ccb->startdone = ARCMSR_CCB_START;
	if (arcmsr_cdb->Flags & ARCMSR_CDB_FLAG_SGL_BSIZE)
		writel(cdb_shifted_phyaddr | ARCMSR_CCBPOST_FLAG_SGL_BSIZE,
			&reg->inbound_queueport);
	else
		writel(cdb_shifted_phyaddr, &reg->inbound_queueport);
}

void arcmsr_post_Qbuffer(struct AdapterControlBlock *acb)
{
	struct MessageUnit __iomem *reg = acb->pmu;
	struct QBUFFER __iomem *pwbuffer = (struct QBUFFER __iomem *) &reg->message_wbuffer;
	uint8_t __iomem *iop_data = (uint8_t __iomem *) pwbuffer->data;
	int32_t allxfer_len = 0;

	if (acb->acb_flags & ACB_F_MESSAGE_WQBUFFER_READED) {
		acb->acb_flags &= (~ACB_F_MESSAGE_WQBUFFER_READED);
		while ((acb->wqbuf_firstindex != acb->wqbuf_lastindex)
			&& (allxfer_len < 124)) {
			writeb(acb->wqbuffer[acb->wqbuf_firstindex], iop_data);
			acb->wqbuf_firstindex++;
			acb->wqbuf_firstindex %= ARCMSR_MAX_QBUFFER;
			iop_data++;
			allxfer_len++;
		}
		writel(allxfer_len, &pwbuffer->data_len);
		writel(ARCMSR_INBOUND_DRIVER_DATA_WRITE_OK
			, &reg->inbound_doorbell);
	}
}

static void arcmsr_stop_adapter_bgrb(struct AdapterControlBlock *acb)
{
	struct MessageUnit __iomem *reg = acb->pmu;

	acb->acb_flags &= ~ACB_F_MSG_START_BGRB;
	writel(ARCMSR_INBOUND_MESG0_STOP_BGRB, &reg->inbound_msgaddr0);
	if (arcmsr_wait_msgint_ready(acb))
		printk(KERN_NOTICE
			"arcmsr%d: wait 'stop adapter background rebulid' timeout \n"
			, acb->host->host_no);
}

static void arcmsr_free_ccb_pool(struct AdapterControlBlock *acb)
{
	dma_free_coherent(&acb->pdev->dev,
		ARCMSR_MAX_FREECCB_NUM * sizeof (struct CommandControlBlock) + 0x20,
		acb->dma_coherent,
		acb->dma_coherent_handle);
}

static irqreturn_t arcmsr_interrupt(struct AdapterControlBlock *acb)
{
	struct MessageUnit __iomem *reg = acb->pmu;
	struct CommandControlBlock *ccb;
	uint32_t flag_ccb, outbound_intstatus, outbound_doorbell;

	outbound_intstatus = readl(&reg->outbound_intstatus)
		& acb->outbound_int_enable;
	writel(outbound_intstatus, &reg->outbound_intstatus);
	if (outbound_intstatus & ARCMSR_MU_OUTBOUND_DOORBELL_INT) {
		outbound_doorbell = readl(&reg->outbound_doorbell);
		writel(outbound_doorbell, &reg->outbound_doorbell);
		if (outbound_doorbell & ARCMSR_OUTBOUND_IOP331_DATA_WRITE_OK) {
			struct QBUFFER __iomem * prbuffer =
				(struct QBUFFER __iomem *) &reg->message_rbuffer;
			uint8_t __iomem * iop_data = (uint8_t __iomem *)prbuffer->data;
			int32_t my_empty_len, iop_len, rqbuf_firstindex, rqbuf_lastindex;

			rqbuf_lastindex = acb->rqbuf_lastindex;
			rqbuf_firstindex = acb->rqbuf_firstindex;
			iop_len = readl(&prbuffer->data_len);
			my_empty_len = (rqbuf_firstindex - rqbuf_lastindex - 1)
					&(ARCMSR_MAX_QBUFFER - 1);
			if (my_empty_len >= iop_len) {
				while (iop_len > 0) {
					acb->rqbuffer[acb->rqbuf_lastindex] = readb(iop_data);
					acb->rqbuf_lastindex++;
					acb->rqbuf_lastindex %= ARCMSR_MAX_QBUFFER;
					iop_data++;
					iop_len--;
				}
				writel(ARCMSR_INBOUND_DRIVER_DATA_READ_OK,
					&reg->inbound_doorbell);
			} else
				acb->acb_flags |= ACB_F_IOPDATA_OVERFLOW;
		}
		if (outbound_doorbell & ARCMSR_OUTBOUND_IOP331_DATA_READ_OK) {
			acb->acb_flags |= ACB_F_MESSAGE_WQBUFFER_READED;
			if (acb->wqbuf_firstindex != acb->wqbuf_lastindex) {
				struct QBUFFER __iomem * pwbuffer =
						(struct QBUFFER __iomem *) &reg->message_wbuffer;
				uint8_t __iomem * iop_data = (uint8_t __iomem *) pwbuffer->data;
				int32_t allxfer_len = 0;

				acb->acb_flags &= (~ACB_F_MESSAGE_WQBUFFER_READED);
				while ((acb->wqbuf_firstindex != acb->wqbuf_lastindex)
					&& (allxfer_len < 124)) {
					writeb(acb->wqbuffer[acb->wqbuf_firstindex], iop_data);
					acb->wqbuf_firstindex++;
					acb->wqbuf_firstindex %= ARCMSR_MAX_QBUFFER;
					iop_data++;
					allxfer_len++;
				}
				writel(allxfer_len, &pwbuffer->data_len);
				writel(ARCMSR_INBOUND_DRIVER_DATA_WRITE_OK,
					&reg->inbound_doorbell);
			}
			if (acb->wqbuf_firstindex == acb->wqbuf_lastindex)
				acb->acb_flags |= ACB_F_MESSAGE_WQBUFFER_CLEARED;
		}
	}
	if (outbound_intstatus & ARCMSR_MU_OUTBOUND_POSTQUEUE_INT) {
		int id, lun;
		/*
		****************************************************************
		**               areca cdb command done
		****************************************************************
		*/
		while (1) {
			if ((flag_ccb = readl(&reg->outbound_queueport)) == 0xFFFFFFFF)
				break;/*chip FIFO no ccb for completion already*/
			/* check if command done with no error*/
			ccb = (struct CommandControlBlock *)(acb->vir2phy_offset +
				(flag_ccb << 5));
			if ((ccb->acb != acb) || (ccb->startdone != ARCMSR_CCB_START)) {
				if (ccb->startdone == ARCMSR_CCB_ABORTED) {
					struct scsi_cmnd *abortcmd=ccb->pcmd;
					if (abortcmd) {
					abortcmd->result |= DID_ABORT >> 16;
					arcmsr_ccb_complete(ccb, 1);
					printk(KERN_NOTICE
						"arcmsr%d: ccb='0x%p' isr got aborted command \n"
						, acb->host->host_no, ccb);
					}
					continue;
				}
				printk(KERN_NOTICE
					"arcmsr%d: isr get an illegal ccb command done acb='0x%p'"
					"ccb='0x%p' ccbacb='0x%p' startdone = 0x%x"
					" ccboutstandingcount=%d \n"
					, acb->host->host_no
					, acb
					, ccb
					, ccb->acb
					, ccb->startdone
					, atomic_read(&acb->ccboutstandingcount));
				continue;
			}
			id = ccb->pcmd->device->id;
			lun = ccb->pcmd->device->lun;
			if (!(flag_ccb & ARCMSR_CCBREPLY_FLAG_ERROR)) {
				if (acb->devstate[id][lun] == ARECA_RAID_GONE)
					acb->devstate[id][lun] = ARECA_RAID_GOOD;
				ccb->pcmd->result = DID_OK << 16;
				arcmsr_ccb_complete(ccb, 1);
			} else {
				switch(ccb->arcmsr_cdb.DeviceStatus) {
				case ARCMSR_DEV_SELECT_TIMEOUT: {
						acb->devstate[id][lun] = ARECA_RAID_GONE;
						ccb->pcmd->result = DID_TIME_OUT << 16;
						arcmsr_ccb_complete(ccb, 1);
					}
					break;
				case ARCMSR_DEV_ABORTED:
				case ARCMSR_DEV_INIT_FAIL: {
						acb->devstate[id][lun] = ARECA_RAID_GONE;
						ccb->pcmd->result = DID_BAD_TARGET << 16;
						arcmsr_ccb_complete(ccb, 1);
					}
					break;
				case ARCMSR_DEV_CHECK_CONDITION: {
						acb->devstate[id][lun] = ARECA_RAID_GOOD;
						arcmsr_report_sense_info(ccb);
						arcmsr_ccb_complete(ccb, 1);
					}
					break;
				default:
					printk(KERN_NOTICE
						"arcmsr%d: scsi id=%d lun=%d"
						" isr get command error done,"
						"but got unknown DeviceStatus = 0x%x \n"
						, acb->host->host_no
						, id
						, lun
						, ccb->arcmsr_cdb.DeviceStatus);
						acb->devstate[id][lun] = ARECA_RAID_GONE;
						ccb->pcmd->result = DID_NO_CONNECT << 16;
						arcmsr_ccb_complete(ccb, 1);
					break;
				}
			}
		}/*drain reply FIFO*/
	}
	if (!(outbound_intstatus & ARCMSR_MU_OUTBOUND_HANDLE_INT))
		return IRQ_NONE;
	return IRQ_HANDLED;
}

static void arcmsr_iop_parking(struct AdapterControlBlock *acb)
{
	if (acb) {
		/* stop adapter background rebuild */
		if (acb->acb_flags & ACB_F_MSG_START_BGRB) {
			acb->acb_flags &= ~ACB_F_MSG_START_BGRB;
			arcmsr_stop_adapter_bgrb(acb);
			arcmsr_flush_adapter_cache(acb);
		}
	}
}

static int arcmsr_iop_message_xfer(struct AdapterControlBlock *acb, struct scsi_cmnd *cmd)
{
	struct MessageUnit __iomem *reg = acb->pmu;
	struct CMD_MESSAGE_FIELD *pcmdmessagefld;
	int retvalue = 0, transfer_len = 0;
	char *buffer;
	uint32_t controlcode = (uint32_t ) cmd->cmnd[5] << 24 |
						(uint32_t ) cmd->cmnd[6] << 16 |
						(uint32_t ) cmd->cmnd[7] << 8  |
						(uint32_t ) cmd->cmnd[8];
					/* 4 bytes: Areca io control code */
	if (cmd->use_sg) {
		struct scatterlist *sg = (struct scatterlist *)cmd->request_buffer;

		buffer = kmap_atomic(sg->page, KM_IRQ0) + sg->offset;
		if (cmd->use_sg > 1) {
			retvalue = ARCMSR_MESSAGE_FAIL;
			goto message_out;
		}
		transfer_len += sg->length;
	} else {
		buffer = cmd->request_buffer;
		transfer_len = cmd->request_bufflen;
	}
	if (transfer_len > sizeof(struct CMD_MESSAGE_FIELD)) {
		retvalue = ARCMSR_MESSAGE_FAIL;
		goto message_out;
	}
	pcmdmessagefld = (struct CMD_MESSAGE_FIELD *) buffer;
	switch(controlcode) {
	case ARCMSR_MESSAGE_READ_RQBUFFER: {
			unsigned long *ver_addr;
			dma_addr_t buf_handle;
			uint8_t *pQbuffer, *ptmpQbuffer;
			int32_t allxfer_len = 0;

			ver_addr = pci_alloc_consistent(acb->pdev, 1032, &buf_handle);
			if (!ver_addr) {
				retvalue = ARCMSR_MESSAGE_FAIL;
				goto message_out;
			}
			ptmpQbuffer = (uint8_t *) ver_addr;
			while ((acb->rqbuf_firstindex != acb->rqbuf_lastindex)
				&& (allxfer_len < 1031)) {
				pQbuffer = &acb->rqbuffer[acb->rqbuf_firstindex];
				memcpy(ptmpQbuffer, pQbuffer, 1);
				acb->rqbuf_firstindex++;
				acb->rqbuf_firstindex %= ARCMSR_MAX_QBUFFER;
				ptmpQbuffer++;
				allxfer_len++;
			}
			if (acb->acb_flags & ACB_F_IOPDATA_OVERFLOW) {
				struct QBUFFER __iomem * prbuffer = (struct QBUFFER __iomem *)
							&reg->message_rbuffer;
				uint8_t __iomem * iop_data = (uint8_t __iomem *)prbuffer->data;
				int32_t iop_len;

				acb->acb_flags &= ~ACB_F_IOPDATA_OVERFLOW;
				iop_len = readl(&prbuffer->data_len);
				while (iop_len > 0) {
					acb->rqbuffer[acb->rqbuf_lastindex] = readb(iop_data);
					acb->rqbuf_lastindex++;
					acb->rqbuf_lastindex %= ARCMSR_MAX_QBUFFER;
					iop_data++;
					iop_len--;
				}
				writel(ARCMSR_INBOUND_DRIVER_DATA_READ_OK,
						&reg->inbound_doorbell);
			}
			memcpy(pcmdmessagefld->messagedatabuffer,
				(uint8_t *)ver_addr, allxfer_len);
			pcmdmessagefld->cmdmessage.Length = allxfer_len;
			pcmdmessagefld->cmdmessage.ReturnCode = ARCMSR_MESSAGE_RETURNCODE_OK;
			pci_free_consistent(acb->pdev, 1032, ver_addr, buf_handle);
		}
		break;
	case ARCMSR_MESSAGE_WRITE_WQBUFFER: {
			unsigned long *ver_addr;
			dma_addr_t buf_handle;
			int32_t my_empty_len, user_len, wqbuf_firstindex, wqbuf_lastindex;
			uint8_t *pQbuffer, *ptmpuserbuffer;

			ver_addr = pci_alloc_consistent(acb->pdev, 1032, &buf_handle);
			if (!ver_addr) {
				retvalue = ARCMSR_MESSAGE_FAIL;
				goto message_out;
			}
			ptmpuserbuffer = (uint8_t *)ver_addr;
			user_len = pcmdmessagefld->cmdmessage.Length;
			memcpy(ptmpuserbuffer, pcmdmessagefld->messagedatabuffer, user_len);
			wqbuf_lastindex = acb->wqbuf_lastindex;
			wqbuf_firstindex = acb->wqbuf_firstindex;
			if (wqbuf_lastindex != wqbuf_firstindex) {
				struct SENSE_DATA *sensebuffer =
					(struct SENSE_DATA *)cmd->sense_buffer;
				arcmsr_post_Qbuffer(acb);
				/* has error report sensedata */
				sensebuffer->ErrorCode = 0x70;
				sensebuffer->SenseKey = ILLEGAL_REQUEST;
				sensebuffer->AdditionalSenseLength = 0x0A;
				sensebuffer->AdditionalSenseCode = 0x20;
				sensebuffer->Valid = 1;
				retvalue = ARCMSR_MESSAGE_FAIL;
			} else {
				my_empty_len = (wqbuf_firstindex-wqbuf_lastindex - 1)
						&(ARCMSR_MAX_QBUFFER - 1);
				if (my_empty_len >= user_len) {
					while (user_len > 0) {
						pQbuffer =
						&acb->wqbuffer[acb->wqbuf_lastindex];
						memcpy(pQbuffer, ptmpuserbuffer, 1);
						acb->wqbuf_lastindex++;
						acb->wqbuf_lastindex %= ARCMSR_MAX_QBUFFER;
						ptmpuserbuffer++;
						user_len--;
					}
					if (acb->acb_flags & ACB_F_MESSAGE_WQBUFFER_CLEARED) {
						acb->acb_flags &=
							~ACB_F_MESSAGE_WQBUFFER_CLEARED;
						arcmsr_post_Qbuffer(acb);
					}
				} else {
					/* has error report sensedata */
					struct SENSE_DATA *sensebuffer =
						(struct SENSE_DATA *)cmd->sense_buffer;
					sensebuffer->ErrorCode = 0x70;
					sensebuffer->SenseKey = ILLEGAL_REQUEST;
					sensebuffer->AdditionalSenseLength = 0x0A;
					sensebuffer->AdditionalSenseCode = 0x20;
					sensebuffer->Valid = 1;
					retvalue = ARCMSR_MESSAGE_FAIL;
				}
			}
			pci_free_consistent(acb->pdev, 1032, ver_addr, buf_handle);
		}
		break;
	case ARCMSR_MESSAGE_CLEAR_RQBUFFER: {
			uint8_t *pQbuffer = acb->rqbuffer;

			if (acb->acb_flags & ACB_F_IOPDATA_OVERFLOW) {
				acb->acb_flags &= ~ACB_F_IOPDATA_OVERFLOW;
				writel(ARCMSR_INBOUND_DRIVER_DATA_READ_OK,
					&reg->inbound_doorbell);
			}
			acb->acb_flags |= ACB_F_MESSAGE_RQBUFFER_CLEARED;
			acb->rqbuf_firstindex = 0;
			acb->rqbuf_lastindex = 0;
			memset(pQbuffer, 0, ARCMSR_MAX_QBUFFER);
			pcmdmessagefld->cmdmessage.ReturnCode =
				ARCMSR_MESSAGE_RETURNCODE_OK;
		}
		break;
	case ARCMSR_MESSAGE_CLEAR_WQBUFFER: {
			uint8_t *pQbuffer = acb->wqbuffer;

			if (acb->acb_flags & ACB_F_IOPDATA_OVERFLOW) {
				acb->acb_flags &= ~ACB_F_IOPDATA_OVERFLOW;
				writel(ARCMSR_INBOUND_DRIVER_DATA_READ_OK
						, &reg->inbound_doorbell);
			}
			acb->acb_flags |=
				(ACB_F_MESSAGE_WQBUFFER_CLEARED |
					ACB_F_MESSAGE_WQBUFFER_READED);
			acb->wqbuf_firstindex = 0;
			acb->wqbuf_lastindex = 0;
			memset(pQbuffer, 0, ARCMSR_MAX_QBUFFER);
			pcmdmessagefld->cmdmessage.ReturnCode =
				ARCMSR_MESSAGE_RETURNCODE_OK;
		}
		break;
	case ARCMSR_MESSAGE_CLEAR_ALLQBUFFER: {
			uint8_t *pQbuffer;

			if (acb->acb_flags & ACB_F_IOPDATA_OVERFLOW) {
				acb->acb_flags &= ~ACB_F_IOPDATA_OVERFLOW;
				writel(ARCMSR_INBOUND_DRIVER_DATA_READ_OK
						, &reg->inbound_doorbell);
			}
			acb->acb_flags |=
				(ACB_F_MESSAGE_WQBUFFER_CLEARED
				| ACB_F_MESSAGE_RQBUFFER_CLEARED
				| ACB_F_MESSAGE_WQBUFFER_READED);
			acb->rqbuf_firstindex = 0;
			acb->rqbuf_lastindex = 0;
			acb->wqbuf_firstindex = 0;
			acb->wqbuf_lastindex = 0;
			pQbuffer = acb->rqbuffer;
			memset(pQbuffer, 0, sizeof (struct QBUFFER));
			pQbuffer = acb->wqbuffer;
			memset(pQbuffer, 0, sizeof (struct QBUFFER));
			pcmdmessagefld->cmdmessage.ReturnCode = ARCMSR_MESSAGE_RETURNCODE_OK;
		}
		break;
	case ARCMSR_MESSAGE_RETURN_CODE_3F: {
			pcmdmessagefld->cmdmessage.ReturnCode = ARCMSR_MESSAGE_RETURNCODE_3F;
		}
		break;
	case ARCMSR_MESSAGE_SAY_HELLO: {
			int8_t * hello_string = "Hello! I am ARCMSR";

			memcpy(pcmdmessagefld->messagedatabuffer, hello_string
				, (int16_t)strlen(hello_string));
			pcmdmessagefld->cmdmessage.ReturnCode = ARCMSR_MESSAGE_RETURNCODE_OK;
		}
		break;
	case ARCMSR_MESSAGE_SAY_GOODBYE:
		arcmsr_iop_parking(acb);
		break;
	case ARCMSR_MESSAGE_FLUSH_ADAPTER_CACHE:
		arcmsr_flush_adapter_cache(acb);
		break;
	default:
		retvalue = ARCMSR_MESSAGE_FAIL;
	}
 message_out:
	if (cmd->use_sg) {
		struct scatterlist *sg;

		sg = (struct scatterlist *) cmd->request_buffer;
		kunmap_atomic(buffer - sg->offset, KM_IRQ0);
	}
	return retvalue;
}

static struct CommandControlBlock *arcmsr_get_freeccb(struct AdapterControlBlock *acb)
{
	struct list_head *head = &acb->ccb_free_list;
	struct CommandControlBlock *ccb = NULL;

	if (!list_empty(head)) {
		ccb = list_entry(head->next, struct CommandControlBlock, list);
		list_del(head->next);
	}
	return ccb;
}

static void arcmsr_handle_virtual_command(struct AdapterControlBlock *acb,
		struct scsi_cmnd *cmd)
{
	switch (cmd->cmnd[0]) {
	case INQUIRY: {
		unsigned char inqdata[36];
		char *buffer;

		if (cmd->device->lun) {
			cmd->result = (DID_TIME_OUT << 16);
			cmd->scsi_done(cmd);
			return;
		}
		inqdata[0] = TYPE_PROCESSOR;
		/* Periph Qualifier & Periph Dev Type */
		inqdata[1] = 0;
		/* rem media bit & Dev Type Modifier */
		inqdata[2] = 0;
		/* ISO,ECMA,& ANSI versions */
		inqdata[4] = 31;
		/* length of additional data */
		strncpy(&inqdata[8], "Areca   ", 8);
		/* Vendor Identification */
		strncpy(&inqdata[16], "RAID controller ", 16);
		/* Product Identification */
		strncpy(&inqdata[32], "R001", 4); /* Product Revision */
		if (cmd->use_sg) {
			struct scatterlist *sg;

			sg = (struct scatterlist *) cmd->request_buffer;
			buffer = kmap_atomic(sg->page, KM_IRQ0) + sg->offset;
		} else {
			buffer = cmd->request_buffer;
		}
		memcpy(buffer, inqdata, sizeof(inqdata));
		if (cmd->use_sg) {
			struct scatterlist *sg;

			sg = (struct scatterlist *) cmd->request_buffer;
			kunmap_atomic(buffer - sg->offset, KM_IRQ0);
		}
		cmd->scsi_done(cmd);
	}
	break;
	case WRITE_BUFFER:
	case READ_BUFFER: {
		if (arcmsr_iop_message_xfer(acb, cmd))
			cmd->result = (DID_ERROR << 16);
		cmd->scsi_done(cmd);
	}
	break;
	default:
		cmd->scsi_done(cmd);
	}
}

static int arcmsr_queue_command(struct scsi_cmnd *cmd,
	void (* done)(struct scsi_cmnd *))
{
	struct Scsi_Host *host = cmd->device->host;
	struct AdapterControlBlock *acb =
		(struct AdapterControlBlock *) host->hostdata;
	struct CommandControlBlock *ccb;
	int target = cmd->device->id;
	int lun = cmd->device->lun;

	cmd->scsi_done = done;
	cmd->host_scribble = NULL;
	cmd->result = 0;
	if (acb->acb_flags & ACB_F_BUS_RESET) {
		printk(KERN_NOTICE "arcmsr%d: bus reset"
			" and return busy \n"
			, acb->host->host_no);
		return SCSI_MLQUEUE_HOST_BUSY;
	}
	if(target == 16) {
		/* virtual device for iop message transfer */
		arcmsr_handle_virtual_command(acb, cmd);
		return 0;
	}
	if (acb->devstate[target][lun] == ARECA_RAID_GONE) {
		uint8_t block_cmd;

		block_cmd = cmd->cmnd[0] & 0x0f;
		if (block_cmd == 0x08 || block_cmd == 0x0a) {
			printk(KERN_NOTICE
				"arcmsr%d: block 'read/write'"
				"command with gone raid volume"
				" Cmd=%2x, TargetId=%d, Lun=%d \n"
				, acb->host->host_no
				, cmd->cmnd[0]
				, target, lun);
			cmd->result = (DID_NO_CONNECT << 16);
			cmd->scsi_done(cmd);
			return 0;
		}
	}
	if (atomic_read(&acb->ccboutstandingcount) >=
			ARCMSR_MAX_OUTSTANDING_CMD)
		return SCSI_MLQUEUE_HOST_BUSY;

	ccb = arcmsr_get_freeccb(acb);
	if (!ccb)
		return SCSI_MLQUEUE_HOST_BUSY;
	arcmsr_build_ccb(acb, ccb, cmd);
	arcmsr_post_ccb(acb, ccb);
	return 0;
}

static void arcmsr_get_firmware_spec(struct AdapterControlBlock *acb)
{
	struct MessageUnit __iomem *reg = acb->pmu;
	char *acb_firm_model = acb->firm_model;
	char *acb_firm_version = acb->firm_version;
	char __iomem *iop_firm_model = (char __iomem *) &reg->message_rwbuffer[15];
	char __iomem *iop_firm_version = (char __iomem *) &reg->message_rwbuffer[17];
	int count;

	writel(ARCMSR_INBOUND_MESG0_GET_CONFIG, &reg->inbound_msgaddr0);
	if (arcmsr_wait_msgint_ready(acb))
		printk(KERN_NOTICE
			"arcmsr%d: wait "
			"'get adapter firmware miscellaneous data' timeout \n"
			, acb->host->host_no);
	count = 8;
	while (count) {
		*acb_firm_model = readb(iop_firm_model);
		acb_firm_model++;
		iop_firm_model++;
		count--;
	}
	count = 16;
	while (count) {
		*acb_firm_version = readb(iop_firm_version);
		acb_firm_version++;
		iop_firm_version++;
		count--;
	}
	printk(KERN_INFO
		"ARECA RAID ADAPTER%d: FIRMWARE VERSION %s \n"
		, acb->host->host_no
		, acb->firm_version);
	acb->firm_request_len = readl(&reg->message_rwbuffer[1]);
	acb->firm_numbers_queue = readl(&reg->message_rwbuffer[2]);
	acb->firm_sdram_size = readl(&reg->message_rwbuffer[3]);
	acb->firm_hd_channels = readl(&reg->message_rwbuffer[4]);
}

static void arcmsr_polling_ccbdone(struct AdapterControlBlock *acb,
	struct CommandControlBlock *poll_ccb)
{
	struct MessageUnit __iomem *reg = acb->pmu;
	struct CommandControlBlock *ccb;
	uint32_t flag_ccb, outbound_intstatus, poll_ccb_done = 0, poll_count = 0;
	int id, lun;

 polling_ccb_retry:
	poll_count++;
	outbound_intstatus = readl(&reg->outbound_intstatus)
					& acb->outbound_int_enable;
	writel(outbound_intstatus, &reg->outbound_intstatus);/*clear interrupt*/
	while (1) {
		if ((flag_ccb = readl(&reg->outbound_queueport)) == 0xFFFFFFFF) {
			if (poll_ccb_done)
				break;
			else {
				msleep(25);
				if (poll_count > 100)
					break;
				goto polling_ccb_retry;
			}
		}
		ccb = (struct CommandControlBlock *)
			(acb->vir2phy_offset + (flag_ccb << 5));
		if ((ccb->acb != acb) ||
			(ccb->startdone != ARCMSR_CCB_START)) {
			if ((ccb->startdone == ARCMSR_CCB_ABORTED) ||
				(ccb == poll_ccb)) {
				printk(KERN_NOTICE
					"arcmsr%d: scsi id=%d lun=%d ccb='0x%p'"
					" poll command abort successfully \n"
					, acb->host->host_no
					, ccb->pcmd->device->id
					, ccb->pcmd->device->lun
					, ccb);
				ccb->pcmd->result = DID_ABORT << 16;
				arcmsr_ccb_complete(ccb, 1);
				poll_ccb_done = 1;
				continue;
			}
			printk(KERN_NOTICE
				"arcmsr%d: polling get an illegal ccb"
				" command done ccb='0x%p'"
				"ccboutstandingcount=%d \n"
				, acb->host->host_no
				, ccb
				, atomic_read(&acb->ccboutstandingcount));
			continue;
		}
		id = ccb->pcmd->device->id;
		lun = ccb->pcmd->device->lun;
		if (!(flag_ccb & ARCMSR_CCBREPLY_FLAG_ERROR)) {
			if (acb->devstate[id][lun] == ARECA_RAID_GONE)
				acb->devstate[id][lun] = ARECA_RAID_GOOD;
			ccb->pcmd->result = DID_OK << 16;
			arcmsr_ccb_complete(ccb, 1);
		} else {
			switch(ccb->arcmsr_cdb.DeviceStatus) {
			case ARCMSR_DEV_SELECT_TIMEOUT: {
					acb->devstate[id][lun] = ARECA_RAID_GONE;
					ccb->pcmd->result = DID_TIME_OUT << 16;
					arcmsr_ccb_complete(ccb, 1);
				}
				break;
			case ARCMSR_DEV_ABORTED:
			case ARCMSR_DEV_INIT_FAIL: {
					acb->devstate[id][lun] = ARECA_RAID_GONE;
					ccb->pcmd->result = DID_BAD_TARGET << 16;
					arcmsr_ccb_complete(ccb, 1);
				}
				break;
			case ARCMSR_DEV_CHECK_CONDITION: {
					acb->devstate[id][lun] = ARECA_RAID_GOOD;
					arcmsr_report_sense_info(ccb);
					arcmsr_ccb_complete(ccb, 1);
				}
				break;
			default:
				printk(KERN_NOTICE
					"arcmsr%d: scsi id=%d lun=%d"
					" polling and getting command error done"
					"but got unknown DeviceStatus = 0x%x \n"
					, acb->host->host_no
					, id
					, lun
					, ccb->arcmsr_cdb.DeviceStatus);
				acb->devstate[id][lun] = ARECA_RAID_GONE;
				ccb->pcmd->result = DID_BAD_TARGET << 16;
				arcmsr_ccb_complete(ccb, 1);
				break;
			}
		}
	}
}

static void arcmsr_iop_init(struct AdapterControlBlock *acb)
{
	struct MessageUnit __iomem *reg = acb->pmu;
	uint32_t intmask_org, mask, outbound_doorbell, firmware_state = 0;

	do {
		firmware_state = readl(&reg->outbound_msgaddr1);
	} while (!(firmware_state & ARCMSR_OUTBOUND_MESG1_FIRMWARE_OK));
	intmask_org = readl(&reg->outbound_intmask)
			| ARCMSR_MU_OUTBOUND_MESSAGE0_INTMASKENABLE;
	arcmsr_get_firmware_spec(acb);

	acb->acb_flags |= ACB_F_MSG_START_BGRB;
	writel(ARCMSR_INBOUND_MESG0_START_BGRB, &reg->inbound_msgaddr0);
	if (arcmsr_wait_msgint_ready(acb)) {
		printk(KERN_NOTICE "arcmsr%d: "
			"wait 'start adapter background rebulid' timeout\n",
			acb->host->host_no);
	}

	outbound_doorbell = readl(&reg->outbound_doorbell);
	writel(outbound_doorbell, &reg->outbound_doorbell);
	writel(ARCMSR_INBOUND_DRIVER_DATA_READ_OK, &reg->inbound_doorbell);
	mask = ~(ARCMSR_MU_OUTBOUND_POSTQUEUE_INTMASKENABLE
			| ARCMSR_MU_OUTBOUND_DOORBELL_INTMASKENABLE);
	writel(intmask_org & mask, &reg->outbound_intmask);
	acb->outbound_int_enable = ~(intmask_org & mask) & 0x000000ff;
	acb->acb_flags |= ACB_F_IOP_INITED;
}

static void arcmsr_iop_reset(struct AdapterControlBlock *acb)
{
	struct MessageUnit __iomem *reg = acb->pmu;
	struct CommandControlBlock *ccb;
	uint32_t intmask_org;
	int i = 0;

	if (atomic_read(&acb->ccboutstandingcount) != 0) {
		/* talk to iop 331 outstanding command aborted */
		arcmsr_abort_allcmd(acb);
		/* wait for 3 sec for all command aborted*/
		msleep_interruptible(3000);
		/* disable all outbound interrupt */
		intmask_org = arcmsr_disable_outbound_ints(acb);
		/* clear all outbound posted Q */
		for (i = 0; i < ARCMSR_MAX_OUTSTANDING_CMD; i++)
			readl(&reg->outbound_queueport);
		for (i = 0; i < ARCMSR_MAX_FREECCB_NUM; i++) {
			ccb = acb->pccb_pool[i];
			if ((ccb->startdone == ARCMSR_CCB_START) ||
				(ccb->startdone == ARCMSR_CCB_ABORTED)) {
				ccb->startdone = ARCMSR_CCB_ABORTED;
				ccb->pcmd->result = DID_ABORT << 16;
				arcmsr_ccb_complete(ccb, 1);
			}
		}
		/* enable all outbound interrupt */
		arcmsr_enable_outbound_ints(acb, intmask_org);
	}
	atomic_set(&acb->ccboutstandingcount, 0);
}

static int arcmsr_bus_reset(struct scsi_cmnd *cmd)
{
	struct AdapterControlBlock *acb =
		(struct AdapterControlBlock *)cmd->device->host->hostdata;
	int i;

	acb->num_resets++;
	acb->acb_flags |= ACB_F_BUS_RESET;
	for (i = 0; i < 400; i++) {
		if (!atomic_read(&acb->ccboutstandingcount))
			break;
		arcmsr_interrupt(acb);
		msleep(25);
	}
	arcmsr_iop_reset(acb);
	acb->acb_flags &= ~ACB_F_BUS_RESET;
	return SUCCESS;
}

static void arcmsr_abort_one_cmd(struct AdapterControlBlock *acb,
		struct CommandControlBlock *ccb)
{
	u32 intmask;

	ccb->startdone = ARCMSR_CCB_ABORTED;

	/*
	** Wait for 3 sec for all command done.
	*/
	msleep_interruptible(3000);

	intmask = arcmsr_disable_outbound_ints(acb);
	arcmsr_polling_ccbdone(acb, ccb);
	arcmsr_enable_outbound_ints(acb, intmask);
}

static int arcmsr_abort(struct scsi_cmnd *cmd)
{
	struct AdapterControlBlock *acb =
		(struct AdapterControlBlock *)cmd->device->host->hostdata;
	int i = 0;

	printk(KERN_NOTICE
		"arcmsr%d: abort device command of scsi id=%d lun=%d \n",
		acb->host->host_no, cmd->device->id, cmd->device->lun);
	acb->num_aborts++;

	/*
	************************************************
	** the all interrupt service routine is locked
	** we need to handle it as soon as possible and exit
	************************************************
	*/
	if (!atomic_read(&acb->ccboutstandingcount))
		return SUCCESS;

	for (i = 0; i < ARCMSR_MAX_FREECCB_NUM; i++) {
		struct CommandControlBlock *ccb = acb->pccb_pool[i];
		if (ccb->startdone == ARCMSR_CCB_START && ccb->pcmd == cmd) {
			arcmsr_abort_one_cmd(acb, ccb);
			break;
		}
	}

	return SUCCESS;
}

static const char *arcmsr_info(struct Scsi_Host *host)
{
	struct AdapterControlBlock *acb =
		(struct AdapterControlBlock *) host->hostdata;
	static char buf[256];
	char *type;
	int raid6 = 1;

	switch (acb->pdev->device) {
	case PCI_DEVICE_ID_ARECA_1110:
	case PCI_DEVICE_ID_ARECA_1210:
		raid6 = 0;
		/*FALLTHRU*/
	case PCI_DEVICE_ID_ARECA_1120:
	case PCI_DEVICE_ID_ARECA_1130:
	case PCI_DEVICE_ID_ARECA_1160:
	case PCI_DEVICE_ID_ARECA_1170:
	case PCI_DEVICE_ID_ARECA_1220:
	case PCI_DEVICE_ID_ARECA_1230:
	case PCI_DEVICE_ID_ARECA_1260:
	case PCI_DEVICE_ID_ARECA_1270:
	case PCI_DEVICE_ID_ARECA_1280:
		type = "SATA";
		break;
	case PCI_DEVICE_ID_ARECA_1380:
	case PCI_DEVICE_ID_ARECA_1381:
	case PCI_DEVICE_ID_ARECA_1680:
	case PCI_DEVICE_ID_ARECA_1681:
		type = "SAS";
		break;
	default:
		type = "X-TYPE";
		break;
	}
	sprintf(buf, "Areca %s Host Adapter RAID Controller%s\n        %s",
			type, raid6 ? "( RAID6 capable)" : "",
			ARCMSR_DRIVER_VERSION);
	return buf;
}


