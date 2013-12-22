/*
*******************************************************************************
**        O.S   : Linux
**   FILE NAME  : arcmsr_hba.c
**        BY    : Nick Cheng
**   Description: SCSI RAID Device Driver for
**                ARECA RAID Host adapter
*******************************************************************************
** Copyright (C) 2002 - 2005, Areca Technology Corporation All rights reserved
**
**     Web site: www.areca.com.tw
**       E-mail: support@areca.com.tw
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
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/aer.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsicam.h>
#include "arcmsr.h"
MODULE_AUTHOR("Nick Cheng <support@areca.com.tw>");
MODULE_DESCRIPTION("ARECA (ARC11xx/12xx/16xx/1880) SATA/SAS RAID Host Bus Adapter");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(ARCMSR_DRIVER_VERSION);

#define	ARCMSR_SLEEPTIME	10
#define	ARCMSR_RETRYCOUNT	12

wait_queue_head_t wait_q;
static int arcmsr_iop_message_xfer(struct AdapterControlBlock *acb,
					struct scsi_cmnd *cmd);
static int arcmsr_iop_confirm(struct AdapterControlBlock *acb);
static int arcmsr_abort(struct scsi_cmnd *);
static int arcmsr_bus_reset(struct scsi_cmnd *);
static int arcmsr_bios_param(struct scsi_device *sdev,
		struct block_device *bdev, sector_t capacity, int *info);
static int arcmsr_queue_command(struct Scsi_Host *h, struct scsi_cmnd *cmd);
static int arcmsr_probe(struct pci_dev *pdev,
				const struct pci_device_id *id);
static void arcmsr_remove(struct pci_dev *pdev);
static void arcmsr_shutdown(struct pci_dev *pdev);
static void arcmsr_iop_init(struct AdapterControlBlock *acb);
static void arcmsr_free_ccb_pool(struct AdapterControlBlock *acb);
static u32 arcmsr_disable_outbound_ints(struct AdapterControlBlock *acb);
static void arcmsr_stop_adapter_bgrb(struct AdapterControlBlock *acb);
static void arcmsr_flush_hba_cache(struct AdapterControlBlock *acb);
static void arcmsr_flush_hbb_cache(struct AdapterControlBlock *acb);
static void arcmsr_request_device_map(unsigned long pacb);
static void arcmsr_request_hba_device_map(struct AdapterControlBlock *acb);
static void arcmsr_request_hbb_device_map(struct AdapterControlBlock *acb);
static void arcmsr_request_hbc_device_map(struct AdapterControlBlock *acb);
static void arcmsr_message_isr_bh_fn(struct work_struct *work);
static bool arcmsr_get_firmware_spec(struct AdapterControlBlock *acb);
static void arcmsr_start_adapter_bgrb(struct AdapterControlBlock *acb);
static void arcmsr_hbc_message_isr(struct AdapterControlBlock *pACB);
static void arcmsr_hardware_reset(struct AdapterControlBlock *acb);
static const char *arcmsr_info(struct Scsi_Host *);
static irqreturn_t arcmsr_interrupt(struct AdapterControlBlock *acb);
static int arcmsr_adjust_disk_queue_depth(struct scsi_device *sdev,
					  int queue_depth, int reason)
{
	if (reason != SCSI_QDEPTH_DEFAULT)
		return -EOPNOTSUPP;

	if (queue_depth > ARCMSR_MAX_CMD_PERLUN)
		queue_depth = ARCMSR_MAX_CMD_PERLUN;
	scsi_adjust_queue_depth(sdev, MSG_ORDERED_TAG, queue_depth);
	return queue_depth;
}

static struct scsi_host_template arcmsr_scsi_host_template = {
	.module			= THIS_MODULE,
	.name			= "ARCMSR ARECA SATA/SAS RAID Controller"
				ARCMSR_DRIVER_VERSION,
	.info			= arcmsr_info,
	.queuecommand		= arcmsr_queue_command,
	.eh_abort_handler		= arcmsr_abort,
	.eh_bus_reset_handler	= arcmsr_bus_reset,
	.bios_param		= arcmsr_bios_param,
	.change_queue_depth	= arcmsr_adjust_disk_queue_depth,
	.can_queue		= ARCMSR_MAX_FREECCB_NUM,
	.this_id			= ARCMSR_SCSI_INITIATOR_ID,
	.sg_tablesize	        	= ARCMSR_DEFAULT_SG_ENTRIES, 
	.max_sectors    	    	= ARCMSR_MAX_XFER_SECTORS_C, 
	.cmd_per_lun		= ARCMSR_MAX_CMD_PERLUN,
	.use_clustering		= ENABLE_CLUSTERING,
	.shost_attrs		= arcmsr_host_attrs,
	.no_write_same		= 1,
};
static struct pci_device_id arcmsr_device_id_table[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_ARECA, PCI_DEVICE_ID_ARECA_1110)},
	{PCI_DEVICE(PCI_VENDOR_ID_ARECA, PCI_DEVICE_ID_ARECA_1120)},
	{PCI_DEVICE(PCI_VENDOR_ID_ARECA, PCI_DEVICE_ID_ARECA_1130)},
	{PCI_DEVICE(PCI_VENDOR_ID_ARECA, PCI_DEVICE_ID_ARECA_1160)},
	{PCI_DEVICE(PCI_VENDOR_ID_ARECA, PCI_DEVICE_ID_ARECA_1170)},
	{PCI_DEVICE(PCI_VENDOR_ID_ARECA, PCI_DEVICE_ID_ARECA_1200)},
	{PCI_DEVICE(PCI_VENDOR_ID_ARECA, PCI_DEVICE_ID_ARECA_1201)},
	{PCI_DEVICE(PCI_VENDOR_ID_ARECA, PCI_DEVICE_ID_ARECA_1202)},
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
	{PCI_DEVICE(PCI_VENDOR_ID_ARECA, PCI_DEVICE_ID_ARECA_1880)},
	{0, 0}, /* Terminating entry */
};
MODULE_DEVICE_TABLE(pci, arcmsr_device_id_table);
static struct pci_driver arcmsr_pci_driver = {
	.name			= "arcmsr",
	.id_table			= arcmsr_device_id_table,
	.probe			= arcmsr_probe,
	.remove			= arcmsr_remove,
	.shutdown		= arcmsr_shutdown,
};
/*
****************************************************************************
****************************************************************************
*/

static void arcmsr_free_hbb_mu(struct AdapterControlBlock *acb)
{
	switch (acb->adapter_type) {
	case ACB_ADAPTER_TYPE_A:
	case ACB_ADAPTER_TYPE_C:
		break;
	case ACB_ADAPTER_TYPE_B:{
		dma_free_coherent(&acb->pdev->dev,
			sizeof(struct MessageUnit_B),
			acb->pmuB, acb->dma_coherent_handle_hbb_mu);
	}
	}
}

static bool arcmsr_remap_pciregion(struct AdapterControlBlock *acb)
{
	struct pci_dev *pdev = acb->pdev;
	switch (acb->adapter_type){
	case ACB_ADAPTER_TYPE_A:{
		acb->pmuA = ioremap(pci_resource_start(pdev,0), pci_resource_len(pdev,0));
		if (!acb->pmuA) {
			printk(KERN_NOTICE "arcmsr%d: memory mapping region fail \n", acb->host->host_no);
			return false;
		}
		break;
	}
	case ACB_ADAPTER_TYPE_B:{
		void __iomem *mem_base0, *mem_base1;
		mem_base0 = ioremap(pci_resource_start(pdev, 0), pci_resource_len(pdev, 0));
		if (!mem_base0) {
			printk(KERN_NOTICE "arcmsr%d: memory mapping region fail \n", acb->host->host_no);
			return false;
		}
		mem_base1 = ioremap(pci_resource_start(pdev, 2), pci_resource_len(pdev, 2));
		if (!mem_base1) {
			iounmap(mem_base0);
			printk(KERN_NOTICE "arcmsr%d: memory mapping region fail \n", acb->host->host_no);
			return false;
		}
		acb->mem_base0 = mem_base0;
		acb->mem_base1 = mem_base1;
		break;
	}
	case ACB_ADAPTER_TYPE_C:{
		acb->pmuC = ioremap_nocache(pci_resource_start(pdev, 1), pci_resource_len(pdev, 1));
		if (!acb->pmuC) {
			printk(KERN_NOTICE "arcmsr%d: memory mapping region fail \n", acb->host->host_no);
			return false;
		}
		if (readl(&acb->pmuC->outbound_doorbell) & ARCMSR_HBCMU_IOP2DRV_MESSAGE_CMD_DONE) {
			writel(ARCMSR_HBCMU_IOP2DRV_MESSAGE_CMD_DONE_DOORBELL_CLEAR, &acb->pmuC->outbound_doorbell_clear);/*clear interrupt*/
			return true;
		}
		break;
	}
	}
	return true;
}

static void arcmsr_unmap_pciregion(struct AdapterControlBlock *acb)
{
	switch (acb->adapter_type) {
	case ACB_ADAPTER_TYPE_A:{
		iounmap(acb->pmuA);
	}
	break;
	case ACB_ADAPTER_TYPE_B:{
		iounmap(acb->mem_base0);
		iounmap(acb->mem_base1);
	}

	break;
	case ACB_ADAPTER_TYPE_C:{
		iounmap(acb->pmuC);
	}
	}
}

static irqreturn_t arcmsr_do_interrupt(int irq, void *dev_id)
{
	irqreturn_t handle_state;
	struct AdapterControlBlock *acb = dev_id;

	handle_state = arcmsr_interrupt(acb);
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

static void arcmsr_define_adapter_type(struct AdapterControlBlock *acb)
{
	struct pci_dev *pdev = acb->pdev;
	u16 dev_id;
	pci_read_config_word(pdev, PCI_DEVICE_ID, &dev_id);
	acb->dev_id = dev_id;
	switch (dev_id) {
	case 0x1880: {
		acb->adapter_type = ACB_ADAPTER_TYPE_C;
		}
		break;
	case 0x1201: {
		acb->adapter_type = ACB_ADAPTER_TYPE_B;
		}
		break;

	default: acb->adapter_type = ACB_ADAPTER_TYPE_A;
	}
}

static uint8_t arcmsr_hba_wait_msgint_ready(struct AdapterControlBlock *acb)
{
	struct MessageUnit_A __iomem *reg = acb->pmuA;
	int i;

	for (i = 0; i < 2000; i++) {
		if (readl(&reg->outbound_intstatus) &
				ARCMSR_MU_OUTBOUND_MESSAGE0_INT) {
			writel(ARCMSR_MU_OUTBOUND_MESSAGE0_INT,
				&reg->outbound_intstatus);
			return true;
		}
		msleep(10);
	} /* max 20 seconds */

	return false;
}

static uint8_t arcmsr_hbb_wait_msgint_ready(struct AdapterControlBlock *acb)
{
	struct MessageUnit_B *reg = acb->pmuB;
	int i;

	for (i = 0; i < 2000; i++) {
		if (readl(reg->iop2drv_doorbell)
			& ARCMSR_IOP2DRV_MESSAGE_CMD_DONE) {
			writel(ARCMSR_MESSAGE_INT_CLEAR_PATTERN,
					reg->iop2drv_doorbell);
			writel(ARCMSR_DRV2IOP_END_OF_INTERRUPT,
					reg->drv2iop_doorbell);
			return true;
		}
		msleep(10);
	} /* max 20 seconds */

	return false;
}

static uint8_t arcmsr_hbc_wait_msgint_ready(struct AdapterControlBlock *pACB)
{
	struct MessageUnit_C *phbcmu = (struct MessageUnit_C *)pACB->pmuC;
	int i;

	for (i = 0; i < 2000; i++) {
		if (readl(&phbcmu->outbound_doorbell)
				& ARCMSR_HBCMU_IOP2DRV_MESSAGE_CMD_DONE) {
			writel(ARCMSR_HBCMU_IOP2DRV_MESSAGE_CMD_DONE_DOORBELL_CLEAR,
				&phbcmu->outbound_doorbell_clear); /*clear interrupt*/
			return true;
		}
		msleep(10);
	} /* max 20 seconds */

	return false;
}

static void arcmsr_flush_hba_cache(struct AdapterControlBlock *acb)
{
	struct MessageUnit_A __iomem *reg = acb->pmuA;
	int retry_count = 30;
	writel(ARCMSR_INBOUND_MESG0_FLUSH_CACHE, &reg->inbound_msgaddr0);
	do {
		if (arcmsr_hba_wait_msgint_ready(acb))
			break;
		else {
			retry_count--;
			printk(KERN_NOTICE "arcmsr%d: wait 'flush adapter cache' \
			timeout, retry count down = %d \n", acb->host->host_no, retry_count);
		}
	} while (retry_count != 0);
}

static void arcmsr_flush_hbb_cache(struct AdapterControlBlock *acb)
{
	struct MessageUnit_B *reg = acb->pmuB;
	int retry_count = 30;
	writel(ARCMSR_MESSAGE_FLUSH_CACHE, reg->drv2iop_doorbell);
	do {
		if (arcmsr_hbb_wait_msgint_ready(acb))
			break;
		else {
			retry_count--;
			printk(KERN_NOTICE "arcmsr%d: wait 'flush adapter cache' \
			timeout,retry count down = %d \n", acb->host->host_no, retry_count);
		}
	} while (retry_count != 0);
}

static void arcmsr_flush_hbc_cache(struct AdapterControlBlock *pACB)
{
	struct MessageUnit_C *reg = (struct MessageUnit_C *)pACB->pmuC;
	int retry_count = 30;/* enlarge wait flush adapter cache time: 10 minute */
	writel(ARCMSR_INBOUND_MESG0_FLUSH_CACHE, &reg->inbound_msgaddr0);
	writel(ARCMSR_HBCMU_DRV2IOP_MESSAGE_CMD_DONE, &reg->inbound_doorbell);
	do {
		if (arcmsr_hbc_wait_msgint_ready(pACB)) {
			break;
		} else {
			retry_count--;
			printk(KERN_NOTICE "arcmsr%d: wait 'flush adapter cache' \
			timeout,retry count down = %d \n", pACB->host->host_no, retry_count);
		}
	} while (retry_count != 0);
	return;
}
static void arcmsr_flush_adapter_cache(struct AdapterControlBlock *acb)
{
	switch (acb->adapter_type) {

	case ACB_ADAPTER_TYPE_A: {
		arcmsr_flush_hba_cache(acb);
		}
		break;

	case ACB_ADAPTER_TYPE_B: {
		arcmsr_flush_hbb_cache(acb);
		}
		break;
	case ACB_ADAPTER_TYPE_C: {
		arcmsr_flush_hbc_cache(acb);
		}
	}
}

static int arcmsr_alloc_ccb_pool(struct AdapterControlBlock *acb)
{
	struct pci_dev *pdev = acb->pdev;
	void *dma_coherent;
	dma_addr_t dma_coherent_handle;
	struct CommandControlBlock *ccb_tmp;
	int i = 0, j = 0;
	dma_addr_t cdb_phyaddr;
	unsigned long roundup_ccbsize;
	unsigned long max_xfer_len;
	unsigned long max_sg_entrys;
	uint32_t  firm_config_version;

	for (i = 0; i < ARCMSR_MAX_TARGETID; i++)
		for (j = 0; j < ARCMSR_MAX_TARGETLUN; j++)
			acb->devstate[i][j] = ARECA_RAID_GONE;

	max_xfer_len = ARCMSR_MAX_XFER_LEN;
	max_sg_entrys = ARCMSR_DEFAULT_SG_ENTRIES;
	firm_config_version = acb->firm_cfg_version;
	if((firm_config_version & 0xFF) >= 3){
		max_xfer_len = (ARCMSR_CDB_SG_PAGE_LENGTH << ((firm_config_version >> 8) & 0xFF)) * 1024;/* max 4M byte */
		max_sg_entrys = (max_xfer_len/4096);
	}
	acb->host->max_sectors = max_xfer_len/512;
	acb->host->sg_tablesize = max_sg_entrys;
	roundup_ccbsize = roundup(sizeof(struct CommandControlBlock) + (max_sg_entrys - 1) * sizeof(struct SG64ENTRY), 32);
	acb->uncache_size = roundup_ccbsize * ARCMSR_MAX_FREECCB_NUM;
	dma_coherent = dma_alloc_coherent(&pdev->dev, acb->uncache_size, &dma_coherent_handle, GFP_KERNEL);
	if(!dma_coherent){
		printk(KERN_NOTICE "arcmsr%d: dma_alloc_coherent got error\n", acb->host->host_no);
		return -ENOMEM;
	}
	acb->dma_coherent = dma_coherent;
	acb->dma_coherent_handle = dma_coherent_handle;
	memset(dma_coherent, 0, acb->uncache_size);
	ccb_tmp = dma_coherent;
	acb->vir2phy_offset = (unsigned long)dma_coherent - (unsigned long)dma_coherent_handle;
	for(i = 0; i < ARCMSR_MAX_FREECCB_NUM; i++){
		cdb_phyaddr = dma_coherent_handle + offsetof(struct CommandControlBlock, arcmsr_cdb);
		ccb_tmp->cdb_phyaddr_pattern = ((acb->adapter_type == ACB_ADAPTER_TYPE_C) ? cdb_phyaddr : (cdb_phyaddr >> 5));
		acb->pccb_pool[i] = ccb_tmp;
		ccb_tmp->acb = acb;
		INIT_LIST_HEAD(&ccb_tmp->list);
		list_add_tail(&ccb_tmp->list, &acb->ccb_free_list);
		ccb_tmp = (struct CommandControlBlock *)((unsigned long)ccb_tmp + roundup_ccbsize);
		dma_coherent_handle = dma_coherent_handle + roundup_ccbsize;
	}
	return 0;
}

static void arcmsr_message_isr_bh_fn(struct work_struct *work) 
{
	struct AdapterControlBlock *acb = container_of(work,struct AdapterControlBlock, arcmsr_do_message_isr_bh);
	switch (acb->adapter_type) {
		case ACB_ADAPTER_TYPE_A: {

			struct MessageUnit_A __iomem *reg  = acb->pmuA;
			char *acb_dev_map = (char *)acb->device_map;
			uint32_t __iomem *signature = (uint32_t __iomem*) (&reg->message_rwbuffer[0]);
			char __iomem *devicemap = (char __iomem*) (&reg->message_rwbuffer[21]);
			int target, lun;
			struct scsi_device *psdev;
			char diff;

			atomic_inc(&acb->rq_map_token);
			if (readl(signature) == ARCMSR_SIGNATURE_GET_CONFIG) {
				for(target = 0; target < ARCMSR_MAX_TARGETID -1; target++) {
					diff = (*acb_dev_map)^readb(devicemap);
					if (diff != 0) {
						char temp;
						*acb_dev_map = readb(devicemap);
						temp =*acb_dev_map;
						for(lun = 0; lun < ARCMSR_MAX_TARGETLUN; lun++) {
							if((temp & 0x01)==1 && (diff & 0x01) == 1) {	
								scsi_add_device(acb->host, 0, target, lun);
							}else if((temp & 0x01) == 0 && (diff & 0x01) == 1) {
								psdev = scsi_device_lookup(acb->host, 0, target, lun);
								if (psdev != NULL ) {
									scsi_remove_device(psdev);
									scsi_device_put(psdev);
								}
							}
							temp >>= 1;
							diff >>= 1;
						}
					}
					devicemap++;
					acb_dev_map++;
				}
			}
			break;
		}

		case ACB_ADAPTER_TYPE_B: {
			struct MessageUnit_B *reg  = acb->pmuB;
			char *acb_dev_map = (char *)acb->device_map;
			uint32_t __iomem *signature = (uint32_t __iomem*)(&reg->message_rwbuffer[0]);
			char __iomem *devicemap = (char __iomem*)(&reg->message_rwbuffer[21]);
			int target, lun;
			struct scsi_device *psdev;
			char diff;

			atomic_inc(&acb->rq_map_token);
			if (readl(signature) == ARCMSR_SIGNATURE_GET_CONFIG) {
				for(target = 0; target < ARCMSR_MAX_TARGETID -1; target++) {
					diff = (*acb_dev_map)^readb(devicemap);
					if (diff != 0) {
						char temp;
						*acb_dev_map = readb(devicemap);
						temp =*acb_dev_map;
						for(lun = 0; lun < ARCMSR_MAX_TARGETLUN; lun++) {
							if((temp & 0x01)==1 && (diff & 0x01) == 1) {	
								scsi_add_device(acb->host, 0, target, lun);
							}else if((temp & 0x01) == 0 && (diff & 0x01) == 1) {
								psdev = scsi_device_lookup(acb->host, 0, target, lun);
								if (psdev != NULL ) {
									scsi_remove_device(psdev);
									scsi_device_put(psdev);
								}
							}
							temp >>= 1;
							diff >>= 1;
						}
					}
					devicemap++;
					acb_dev_map++;
				}
			}
		}
		break;
		case ACB_ADAPTER_TYPE_C: {
			struct MessageUnit_C *reg  = acb->pmuC;
			char *acb_dev_map = (char *)acb->device_map;
			uint32_t __iomem *signature = (uint32_t __iomem *)(&reg->msgcode_rwbuffer[0]);
			char __iomem *devicemap = (char __iomem *)(&reg->msgcode_rwbuffer[21]);
			int target, lun;
			struct scsi_device *psdev;
			char diff;

			atomic_inc(&acb->rq_map_token);
			if (readl(signature) == ARCMSR_SIGNATURE_GET_CONFIG) {
				for (target = 0; target < ARCMSR_MAX_TARGETID - 1; target++) {
					diff = (*acb_dev_map)^readb(devicemap);
					if (diff != 0) {
						char temp;
						*acb_dev_map = readb(devicemap);
						temp = *acb_dev_map;
						for (lun = 0; lun < ARCMSR_MAX_TARGETLUN; lun++) {
							if ((temp & 0x01) == 1 && (diff & 0x01) == 1) {
								scsi_add_device(acb->host, 0, target, lun);
							} else if ((temp & 0x01) == 0 && (diff & 0x01) == 1) {
								psdev = scsi_device_lookup(acb->host, 0, target, lun);
								if (psdev != NULL) {
									scsi_remove_device(psdev);
									scsi_device_put(psdev);
								}
							}
							temp >>= 1;
							diff >>= 1;
						}
					}
					devicemap++;
					acb_dev_map++;
				}
			}
		}
	}
}

static int arcmsr_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct Scsi_Host *host;
	struct AdapterControlBlock *acb;
	uint8_t bus,dev_fun;
	int error;
	error = pci_enable_device(pdev);
	if(error){
		return -ENODEV;
	}
	host = scsi_host_alloc(&arcmsr_scsi_host_template, sizeof(struct AdapterControlBlock));
	if(!host){
    		goto pci_disable_dev;
	}
	error = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if(error){
		error = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if(error){
			printk(KERN_WARNING
			       "scsi%d: No suitable DMA mask available\n",
			       host->host_no);
			goto scsi_host_release;
		}
	}
	init_waitqueue_head(&wait_q);
	bus = pdev->bus->number;
	dev_fun = pdev->devfn;
	acb = (struct AdapterControlBlock *) host->hostdata;
	memset(acb,0,sizeof(struct AdapterControlBlock));
	acb->pdev = pdev;
	acb->host = host;
	host->max_lun = ARCMSR_MAX_TARGETLUN;
	host->max_id = ARCMSR_MAX_TARGETID;		/*16:8*/
	host->max_cmd_len = 16;	 			/*this is issue of 64bit LBA ,over 2T byte*/
	host->can_queue = ARCMSR_MAX_FREECCB_NUM;	/* max simultaneous cmds */		
	host->cmd_per_lun = ARCMSR_MAX_CMD_PERLUN;	    
	host->this_id = ARCMSR_SCSI_INITIATOR_ID;
	host->unique_id = (bus << 8) | dev_fun;
	pci_set_drvdata(pdev, host);
	pci_set_master(pdev);
	error = pci_request_regions(pdev, "arcmsr");
	if(error){
		goto scsi_host_release;
	}
	spin_lock_init(&acb->eh_lock);
	spin_lock_init(&acb->ccblist_lock);
	acb->acb_flags |= (ACB_F_MESSAGE_WQBUFFER_CLEARED |
			ACB_F_MESSAGE_RQBUFFER_CLEARED |
			ACB_F_MESSAGE_WQBUFFER_READED);
	acb->acb_flags &= ~ACB_F_SCSISTOPADAPTER;
	INIT_LIST_HEAD(&acb->ccb_free_list);
	arcmsr_define_adapter_type(acb);
	error = arcmsr_remap_pciregion(acb);
	if(!error){
		goto pci_release_regs;
	}
	error = arcmsr_get_firmware_spec(acb);
	if(!error){
		goto unmap_pci_region;
	}
	error = arcmsr_alloc_ccb_pool(acb);
	if(error){
		goto free_hbb_mu;
	}
	arcmsr_iop_init(acb);
	error = scsi_add_host(host, &pdev->dev);
	if(error){
		goto RAID_controller_stop;
	}
	error = request_irq(pdev->irq, arcmsr_do_interrupt, IRQF_SHARED, "arcmsr", acb);
	if(error){
		goto scsi_host_remove;
	}
	host->irq = pdev->irq;
    	scsi_scan_host(host);
	INIT_WORK(&acb->arcmsr_do_message_isr_bh, arcmsr_message_isr_bh_fn);
	atomic_set(&acb->rq_map_token, 16);
	atomic_set(&acb->ante_token_value, 16);
	acb->fw_flag = FW_NORMAL;
	init_timer(&acb->eternal_timer);
	acb->eternal_timer.expires = jiffies + msecs_to_jiffies(6 * HZ);
	acb->eternal_timer.data = (unsigned long) acb;
	acb->eternal_timer.function = &arcmsr_request_device_map;
	add_timer(&acb->eternal_timer);
	if(arcmsr_alloc_sysfs_attr(acb))
		goto out_free_sysfs;
	return 0;
out_free_sysfs:
scsi_host_remove:
	scsi_remove_host(host);
RAID_controller_stop:
	arcmsr_stop_adapter_bgrb(acb);
	arcmsr_flush_adapter_cache(acb);
	arcmsr_free_ccb_pool(acb);
free_hbb_mu:
	arcmsr_free_hbb_mu(acb);
unmap_pci_region:
	arcmsr_unmap_pciregion(acb);
pci_release_regs:
	pci_release_regions(pdev);
scsi_host_release:
	scsi_host_put(host);
pci_disable_dev:
	pci_disable_device(pdev);
	return -ENODEV;
}

static uint8_t arcmsr_abort_hba_allcmd(struct AdapterControlBlock *acb)
{
	struct MessageUnit_A __iomem *reg = acb->pmuA;
	writel(ARCMSR_INBOUND_MESG0_ABORT_CMD, &reg->inbound_msgaddr0);
	if (!arcmsr_hba_wait_msgint_ready(acb)) {
		printk(KERN_NOTICE
			"arcmsr%d: wait 'abort all outstanding command' timeout \n"
			, acb->host->host_no);
		return false;
	}
	return true;
}

static uint8_t arcmsr_abort_hbb_allcmd(struct AdapterControlBlock *acb)
{
	struct MessageUnit_B *reg = acb->pmuB;

	writel(ARCMSR_MESSAGE_ABORT_CMD, reg->drv2iop_doorbell);
	if (!arcmsr_hbb_wait_msgint_ready(acb)) {
		printk(KERN_NOTICE
			"arcmsr%d: wait 'abort all outstanding command' timeout \n"
			, acb->host->host_no);
		return false;
	}
	return true;
}
static uint8_t arcmsr_abort_hbc_allcmd(struct AdapterControlBlock *pACB)
{
	struct MessageUnit_C *reg = (struct MessageUnit_C *)pACB->pmuC;
	writel(ARCMSR_INBOUND_MESG0_ABORT_CMD, &reg->inbound_msgaddr0);
	writel(ARCMSR_HBCMU_DRV2IOP_MESSAGE_CMD_DONE, &reg->inbound_doorbell);
	if (!arcmsr_hbc_wait_msgint_ready(pACB)) {
		printk(KERN_NOTICE
			"arcmsr%d: wait 'abort all outstanding command' timeout \n"
			, pACB->host->host_no);
		return false;
	}
	return true;
}
static uint8_t arcmsr_abort_allcmd(struct AdapterControlBlock *acb)
{
	uint8_t rtnval = 0;
	switch (acb->adapter_type) {
	case ACB_ADAPTER_TYPE_A: {
		rtnval = arcmsr_abort_hba_allcmd(acb);
		}
		break;

	case ACB_ADAPTER_TYPE_B: {
		rtnval = arcmsr_abort_hbb_allcmd(acb);
		}
		break;

	case ACB_ADAPTER_TYPE_C: {
		rtnval = arcmsr_abort_hbc_allcmd(acb);
		}
	}
	return rtnval;
}

static bool arcmsr_hbb_enable_driver_mode(struct AdapterControlBlock *pacb)
{
	struct MessageUnit_B *reg = pacb->pmuB;
	writel(ARCMSR_MESSAGE_START_DRIVER_MODE, reg->drv2iop_doorbell);
	if (!arcmsr_hbb_wait_msgint_ready(pacb)) {
		printk(KERN_ERR "arcmsr%d: can't set driver mode. \n", pacb->host->host_no);
		return false;
	}
    	return true;
}

static void arcmsr_pci_unmap_dma(struct CommandControlBlock *ccb)
{
	struct scsi_cmnd *pcmd = ccb->pcmd;

	scsi_dma_unmap(pcmd);
}

static void arcmsr_ccb_complete(struct CommandControlBlock *ccb)
{
	struct AdapterControlBlock *acb = ccb->acb;
	struct scsi_cmnd *pcmd = ccb->pcmd;
	unsigned long flags;
	atomic_dec(&acb->ccboutstandingcount);
	arcmsr_pci_unmap_dma(ccb);
	ccb->startdone = ARCMSR_CCB_DONE;
	spin_lock_irqsave(&acb->ccblist_lock, flags);
	list_add_tail(&ccb->list, &acb->ccb_free_list);
	spin_unlock_irqrestore(&acb->ccblist_lock, flags);
	pcmd->scsi_done(pcmd);
}

static void arcmsr_report_sense_info(struct CommandControlBlock *ccb)
{

	struct scsi_cmnd *pcmd = ccb->pcmd;
	struct SENSE_DATA *sensebuffer = (struct SENSE_DATA *)pcmd->sense_buffer;
	pcmd->result = DID_OK << 16;
	if (sensebuffer) {
		int sense_data_length =
			sizeof(struct SENSE_DATA) < SCSI_SENSE_BUFFERSIZE
			? sizeof(struct SENSE_DATA) : SCSI_SENSE_BUFFERSIZE;
		memset(sensebuffer, 0, SCSI_SENSE_BUFFERSIZE);
		memcpy(sensebuffer, ccb->arcmsr_cdb.SenseData, sense_data_length);
		sensebuffer->ErrorCode = SCSI_SENSE_CURRENT_ERRORS;
		sensebuffer->Valid = 1;
	}
}

static u32 arcmsr_disable_outbound_ints(struct AdapterControlBlock *acb)
{
	u32 orig_mask = 0;
	switch (acb->adapter_type) {	
	case ACB_ADAPTER_TYPE_A : {
		struct MessageUnit_A __iomem *reg = acb->pmuA;
		orig_mask = readl(&reg->outbound_intmask);
		writel(orig_mask|ARCMSR_MU_OUTBOUND_ALL_INTMASKENABLE, \
						&reg->outbound_intmask);
		}
		break;
	case ACB_ADAPTER_TYPE_B : {
		struct MessageUnit_B *reg = acb->pmuB;
		orig_mask = readl(reg->iop2drv_doorbell_mask);
		writel(0, reg->iop2drv_doorbell_mask);
		}
		break;
	case ACB_ADAPTER_TYPE_C:{
		struct MessageUnit_C *reg = (struct MessageUnit_C *)acb->pmuC;
		/* disable all outbound interrupt */
		orig_mask = readl(&reg->host_int_mask); /* disable outbound message0 int */
		writel(orig_mask|ARCMSR_HBCMU_ALL_INTMASKENABLE, &reg->host_int_mask);
		}
		break;
	}
	return orig_mask;
}

static void arcmsr_report_ccb_state(struct AdapterControlBlock *acb, 
			struct CommandControlBlock *ccb, bool error)
{
	uint8_t id, lun;
	id = ccb->pcmd->device->id;
	lun = ccb->pcmd->device->lun;
	if (!error) {
		if (acb->devstate[id][lun] == ARECA_RAID_GONE)
			acb->devstate[id][lun] = ARECA_RAID_GOOD;
		ccb->pcmd->result = DID_OK << 16;
		arcmsr_ccb_complete(ccb);
	}else{
		switch (ccb->arcmsr_cdb.DeviceStatus) {
		case ARCMSR_DEV_SELECT_TIMEOUT: {
			acb->devstate[id][lun] = ARECA_RAID_GONE;
			ccb->pcmd->result = DID_NO_CONNECT << 16;
			arcmsr_ccb_complete(ccb);
			}
			break;

		case ARCMSR_DEV_ABORTED:

		case ARCMSR_DEV_INIT_FAIL: {
			acb->devstate[id][lun] = ARECA_RAID_GONE;
			ccb->pcmd->result = DID_BAD_TARGET << 16;
			arcmsr_ccb_complete(ccb);
			}
			break;

		case ARCMSR_DEV_CHECK_CONDITION: {
			acb->devstate[id][lun] = ARECA_RAID_GOOD;
			arcmsr_report_sense_info(ccb);
			arcmsr_ccb_complete(ccb);
			}
			break;

		default:
			printk(KERN_NOTICE
				"arcmsr%d: scsi id = %d lun = %d isr get command error done, \
				but got unknown DeviceStatus = 0x%x \n"
				, acb->host->host_no
				, id
				, lun
				, ccb->arcmsr_cdb.DeviceStatus);
				acb->devstate[id][lun] = ARECA_RAID_GONE;
				ccb->pcmd->result = DID_NO_CONNECT << 16;
				arcmsr_ccb_complete(ccb);
			break;
		}
	}
}

static void arcmsr_drain_donequeue(struct AdapterControlBlock *acb, struct CommandControlBlock *pCCB, bool error)
{
	int id, lun;
	if ((pCCB->acb != acb) || (pCCB->startdone != ARCMSR_CCB_START)) {
		if (pCCB->startdone == ARCMSR_CCB_ABORTED) {
			struct scsi_cmnd *abortcmd = pCCB->pcmd;
			if (abortcmd) {
				id = abortcmd->device->id;
				lun = abortcmd->device->lun;				
				abortcmd->result |= DID_ABORT << 16;
				arcmsr_ccb_complete(pCCB);
				printk(KERN_NOTICE "arcmsr%d: pCCB ='0x%p' isr got aborted command \n",
				acb->host->host_no, pCCB);
			}
			return;
		}
		printk(KERN_NOTICE "arcmsr%d: isr get an illegal ccb command \
				done acb = '0x%p'"
				"ccb = '0x%p' ccbacb = '0x%p' startdone = 0x%x"
				" ccboutstandingcount = %d \n"
				, acb->host->host_no
				, acb
				, pCCB
				, pCCB->acb
				, pCCB->startdone
				, atomic_read(&acb->ccboutstandingcount));
		  return;
	}
	arcmsr_report_ccb_state(acb, pCCB, error);
}

static void arcmsr_done4abort_postqueue(struct AdapterControlBlock *acb)
{
	int i = 0;
	uint32_t flag_ccb;
	struct ARCMSR_CDB *pARCMSR_CDB;
	bool error;
	struct CommandControlBlock *pCCB;
	switch (acb->adapter_type) {

	case ACB_ADAPTER_TYPE_A: {
		struct MessageUnit_A __iomem *reg = acb->pmuA;
		uint32_t outbound_intstatus;
		outbound_intstatus = readl(&reg->outbound_intstatus) &
					acb->outbound_int_enable;
		/*clear and abort all outbound posted Q*/
		writel(outbound_intstatus, &reg->outbound_intstatus);/*clear interrupt*/
		while(((flag_ccb = readl(&reg->outbound_queueport)) != 0xFFFFFFFF)
				&& (i++ < ARCMSR_MAX_OUTSTANDING_CMD)) {
			pARCMSR_CDB = (struct ARCMSR_CDB *)(acb->vir2phy_offset + (flag_ccb << 5));/*frame must be 32 bytes aligned*/
			pCCB = container_of(pARCMSR_CDB, struct CommandControlBlock, arcmsr_cdb);
			error = (flag_ccb & ARCMSR_CCBREPLY_FLAG_ERROR_MODE0) ? true : false;
			arcmsr_drain_donequeue(acb, pCCB, error);
		}
		}
		break;

	case ACB_ADAPTER_TYPE_B: {
		struct MessageUnit_B *reg = acb->pmuB;
		/*clear all outbound posted Q*/
		writel(ARCMSR_DOORBELL_INT_CLEAR_PATTERN, reg->iop2drv_doorbell); /* clear doorbell interrupt */
		for (i = 0; i < ARCMSR_MAX_HBB_POSTQUEUE; i++) {
			if ((flag_ccb = readl(&reg->done_qbuffer[i])) != 0) {
				writel(0, &reg->done_qbuffer[i]);
				pARCMSR_CDB = (struct ARCMSR_CDB *)(acb->vir2phy_offset+(flag_ccb << 5));/*frame must be 32 bytes aligned*/
				pCCB = container_of(pARCMSR_CDB, struct CommandControlBlock, arcmsr_cdb);
				error = (flag_ccb & ARCMSR_CCBREPLY_FLAG_ERROR_MODE0) ? true : false;
				arcmsr_drain_donequeue(acb, pCCB, error);
			}
			reg->post_qbuffer[i] = 0;
		}
		reg->doneq_index = 0;
		reg->postq_index = 0;
		}
		break;
	case ACB_ADAPTER_TYPE_C: {
		struct MessageUnit_C *reg = acb->pmuC;
		struct  ARCMSR_CDB *pARCMSR_CDB;
		uint32_t flag_ccb, ccb_cdb_phy;
		bool error;
		struct CommandControlBlock *pCCB;
		while ((readl(&reg->host_int_status) & ARCMSR_HBCMU_OUTBOUND_POSTQUEUE_ISR) && (i++ < ARCMSR_MAX_OUTSTANDING_CMD)) {
			/*need to do*/
			flag_ccb = readl(&reg->outbound_queueport_low);
			ccb_cdb_phy = (flag_ccb & 0xFFFFFFF0);
			pARCMSR_CDB = (struct  ARCMSR_CDB *)(acb->vir2phy_offset+ccb_cdb_phy);/*frame must be 32 bytes aligned*/
			pCCB = container_of(pARCMSR_CDB, struct CommandControlBlock, arcmsr_cdb);
			error = (flag_ccb & ARCMSR_CCBREPLY_FLAG_ERROR_MODE1) ? true : false;
			arcmsr_drain_donequeue(acb, pCCB, error);
		}
	}
	}
}
static void arcmsr_remove(struct pci_dev *pdev)
{
	struct Scsi_Host *host = pci_get_drvdata(pdev);
	struct AdapterControlBlock *acb =
		(struct AdapterControlBlock *) host->hostdata;
	int poll_count = 0;
	arcmsr_free_sysfs_attr(acb);
	scsi_remove_host(host);
	flush_work(&acb->arcmsr_do_message_isr_bh);
	del_timer_sync(&acb->eternal_timer);
	arcmsr_disable_outbound_ints(acb);
	arcmsr_stop_adapter_bgrb(acb);
	arcmsr_flush_adapter_cache(acb);	
	acb->acb_flags |= ACB_F_SCSISTOPADAPTER;
	acb->acb_flags &= ~ACB_F_IOP_INITED;

	for (poll_count = 0; poll_count < ARCMSR_MAX_OUTSTANDING_CMD; poll_count++){
		if (!atomic_read(&acb->ccboutstandingcount))
			break;
		arcmsr_interrupt(acb);/* FIXME: need spinlock */
		msleep(25);
	}

	if (atomic_read(&acb->ccboutstandingcount)) {
		int i;

		arcmsr_abort_allcmd(acb);
		arcmsr_done4abort_postqueue(acb);
		for (i = 0; i < ARCMSR_MAX_FREECCB_NUM; i++) {
			struct CommandControlBlock *ccb = acb->pccb_pool[i];
			if (ccb->startdone == ARCMSR_CCB_START) {
				ccb->startdone = ARCMSR_CCB_ABORTED;
				ccb->pcmd->result = DID_ABORT << 16;
				arcmsr_ccb_complete(ccb);
			}
		}
	}
	free_irq(pdev->irq, acb);
	arcmsr_free_ccb_pool(acb);
	arcmsr_free_hbb_mu(acb);
	arcmsr_unmap_pciregion(acb);
	pci_release_regions(pdev);
	scsi_host_put(host);
	pci_disable_device(pdev);
}

static void arcmsr_shutdown(struct pci_dev *pdev)
{
	struct Scsi_Host *host = pci_get_drvdata(pdev);
	struct AdapterControlBlock *acb =
		(struct AdapterControlBlock *)host->hostdata;
	del_timer_sync(&acb->eternal_timer);
	arcmsr_disable_outbound_ints(acb);
	flush_work(&acb->arcmsr_do_message_isr_bh);
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

static void arcmsr_enable_outbound_ints(struct AdapterControlBlock *acb,
						u32 intmask_org)
{
	u32 mask;
	switch (acb->adapter_type) {

	case ACB_ADAPTER_TYPE_A: {
		struct MessageUnit_A __iomem *reg = acb->pmuA;
		mask = intmask_org & ~(ARCMSR_MU_OUTBOUND_POSTQUEUE_INTMASKENABLE |
			     ARCMSR_MU_OUTBOUND_DOORBELL_INTMASKENABLE|
			     ARCMSR_MU_OUTBOUND_MESSAGE0_INTMASKENABLE);
		writel(mask, &reg->outbound_intmask);
		acb->outbound_int_enable = ~(intmask_org & mask) & 0x000000ff;
		}
		break;

	case ACB_ADAPTER_TYPE_B: {
		struct MessageUnit_B *reg = acb->pmuB;
		mask = intmask_org | (ARCMSR_IOP2DRV_DATA_WRITE_OK |
			ARCMSR_IOP2DRV_DATA_READ_OK |
			ARCMSR_IOP2DRV_CDB_DONE |
			ARCMSR_IOP2DRV_MESSAGE_CMD_DONE);
		writel(mask, reg->iop2drv_doorbell_mask);
		acb->outbound_int_enable = (intmask_org | mask) & 0x0000000f;
		}
		break;
	case ACB_ADAPTER_TYPE_C: {
		struct MessageUnit_C *reg = acb->pmuC;
		mask = ~(ARCMSR_HBCMU_UTILITY_A_ISR_MASK | ARCMSR_HBCMU_OUTBOUND_DOORBELL_ISR_MASK|ARCMSR_HBCMU_OUTBOUND_POSTQUEUE_ISR_MASK);
		writel(intmask_org & mask, &reg->host_int_mask);
		acb->outbound_int_enable = ~(intmask_org & mask) & 0x0000000f;
		}
	}
}

static int arcmsr_build_ccb(struct AdapterControlBlock *acb,
	struct CommandControlBlock *ccb, struct scsi_cmnd *pcmd)
{
	struct ARCMSR_CDB *arcmsr_cdb = (struct ARCMSR_CDB *)&ccb->arcmsr_cdb;
	int8_t *psge = (int8_t *)&arcmsr_cdb->u;
	__le32 address_lo, address_hi;
	int arccdbsize = 0x30;
	__le32 length = 0;
	int i;
	struct scatterlist *sg;
	int nseg;
	ccb->pcmd = pcmd;
	memset(arcmsr_cdb, 0, sizeof(struct ARCMSR_CDB));
	arcmsr_cdb->TargetID = pcmd->device->id;
	arcmsr_cdb->LUN = pcmd->device->lun;
	arcmsr_cdb->Function = 1;
	arcmsr_cdb->Context = 0;
	memcpy(arcmsr_cdb->Cdb, pcmd->cmnd, pcmd->cmd_len);

	nseg = scsi_dma_map(pcmd);
	if (unlikely(nseg > acb->host->sg_tablesize || nseg < 0))
		return FAILED;
	scsi_for_each_sg(pcmd, sg, nseg, i) {
		/* Get the physical address of the current data pointer */
		length = cpu_to_le32(sg_dma_len(sg));
		address_lo = cpu_to_le32(dma_addr_lo32(sg_dma_address(sg)));
		address_hi = cpu_to_le32(dma_addr_hi32(sg_dma_address(sg)));
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
			pdma_sg->length = length|cpu_to_le32(IS_SG64_ADDR);
			psge += sizeof (struct SG64ENTRY);
			arccdbsize += sizeof (struct SG64ENTRY);
		}
	}
	arcmsr_cdb->sgcount = (uint8_t)nseg;
	arcmsr_cdb->DataLength = scsi_bufflen(pcmd);
	arcmsr_cdb->msgPages = arccdbsize/0x100 + (arccdbsize % 0x100 ? 1 : 0);
	if ( arccdbsize > 256)
		arcmsr_cdb->Flags |= ARCMSR_CDB_FLAG_SGL_BSIZE;
	if (pcmd->sc_data_direction == DMA_TO_DEVICE)
		arcmsr_cdb->Flags |= ARCMSR_CDB_FLAG_WRITE;
	ccb->arc_cdb_size = arccdbsize;
	return SUCCESS;
}

static void arcmsr_post_ccb(struct AdapterControlBlock *acb, struct CommandControlBlock *ccb)
{
	uint32_t cdb_phyaddr_pattern = ccb->cdb_phyaddr_pattern;
	struct ARCMSR_CDB *arcmsr_cdb = (struct ARCMSR_CDB *)&ccb->arcmsr_cdb;
	atomic_inc(&acb->ccboutstandingcount);
	ccb->startdone = ARCMSR_CCB_START;
	switch (acb->adapter_type) {
	case ACB_ADAPTER_TYPE_A: {
		struct MessageUnit_A __iomem *reg = acb->pmuA;

		if (arcmsr_cdb->Flags & ARCMSR_CDB_FLAG_SGL_BSIZE)
			writel(cdb_phyaddr_pattern | ARCMSR_CCBPOST_FLAG_SGL_BSIZE,
			&reg->inbound_queueport);
		else {
				writel(cdb_phyaddr_pattern, &reg->inbound_queueport);
		}
		}
		break;

	case ACB_ADAPTER_TYPE_B: {
		struct MessageUnit_B *reg = acb->pmuB;
		uint32_t ending_index, index = reg->postq_index;

		ending_index = ((index + 1) % ARCMSR_MAX_HBB_POSTQUEUE);
		writel(0, &reg->post_qbuffer[ending_index]);
		if (arcmsr_cdb->Flags & ARCMSR_CDB_FLAG_SGL_BSIZE) {
			writel(cdb_phyaddr_pattern | ARCMSR_CCBPOST_FLAG_SGL_BSIZE,\
						 &reg->post_qbuffer[index]);
		} else {
			writel(cdb_phyaddr_pattern, &reg->post_qbuffer[index]);
		}
		index++;
		index %= ARCMSR_MAX_HBB_POSTQUEUE;/*if last index number set it to 0 */
		reg->postq_index = index;
		writel(ARCMSR_DRV2IOP_CDB_POSTED, reg->drv2iop_doorbell);
		}
		break;
	case ACB_ADAPTER_TYPE_C: {
		struct MessageUnit_C *phbcmu = (struct MessageUnit_C *)acb->pmuC;
		uint32_t ccb_post_stamp, arc_cdb_size;

		arc_cdb_size = (ccb->arc_cdb_size > 0x300) ? 0x300 : ccb->arc_cdb_size;
		ccb_post_stamp = (cdb_phyaddr_pattern | ((arc_cdb_size - 1) >> 6) | 1);
		if (acb->cdb_phyaddr_hi32) {
			writel(acb->cdb_phyaddr_hi32, &phbcmu->inbound_queueport_high);
			writel(ccb_post_stamp, &phbcmu->inbound_queueport_low);
		} else {
			writel(ccb_post_stamp, &phbcmu->inbound_queueport_low);
		}
		}
	}
}

static void arcmsr_stop_hba_bgrb(struct AdapterControlBlock *acb)
{
	struct MessageUnit_A __iomem *reg = acb->pmuA;
	acb->acb_flags &= ~ACB_F_MSG_START_BGRB;
	writel(ARCMSR_INBOUND_MESG0_STOP_BGRB, &reg->inbound_msgaddr0);
	if (!arcmsr_hba_wait_msgint_ready(acb)) {
		printk(KERN_NOTICE
			"arcmsr%d: wait 'stop adapter background rebulid' timeout \n"
			, acb->host->host_no);
	}
}

static void arcmsr_stop_hbb_bgrb(struct AdapterControlBlock *acb)
{
	struct MessageUnit_B *reg = acb->pmuB;
	acb->acb_flags &= ~ACB_F_MSG_START_BGRB;
	writel(ARCMSR_MESSAGE_STOP_BGRB, reg->drv2iop_doorbell);

	if (!arcmsr_hbb_wait_msgint_ready(acb)) {
		printk(KERN_NOTICE
			"arcmsr%d: wait 'stop adapter background rebulid' timeout \n"
			, acb->host->host_no);
	}
}

static void arcmsr_stop_hbc_bgrb(struct AdapterControlBlock *pACB)
{
	struct MessageUnit_C *reg = (struct MessageUnit_C *)pACB->pmuC;
	pACB->acb_flags &= ~ACB_F_MSG_START_BGRB;
	writel(ARCMSR_INBOUND_MESG0_STOP_BGRB, &reg->inbound_msgaddr0);
	writel(ARCMSR_HBCMU_DRV2IOP_MESSAGE_CMD_DONE, &reg->inbound_doorbell);
	if (!arcmsr_hbc_wait_msgint_ready(pACB)) {
		printk(KERN_NOTICE
			"arcmsr%d: wait 'stop adapter background rebulid' timeout \n"
			, pACB->host->host_no);
	}
	return;
}
static void arcmsr_stop_adapter_bgrb(struct AdapterControlBlock *acb)
{
	switch (acb->adapter_type) {
	case ACB_ADAPTER_TYPE_A: {
		arcmsr_stop_hba_bgrb(acb);
		}
		break;

	case ACB_ADAPTER_TYPE_B: {
		arcmsr_stop_hbb_bgrb(acb);
		}
		break;
	case ACB_ADAPTER_TYPE_C: {
		arcmsr_stop_hbc_bgrb(acb);
		}
	}
}

static void arcmsr_free_ccb_pool(struct AdapterControlBlock *acb)
{
	dma_free_coherent(&acb->pdev->dev, acb->uncache_size, acb->dma_coherent, acb->dma_coherent_handle);
}

void arcmsr_iop_message_read(struct AdapterControlBlock *acb)
{
	switch (acb->adapter_type) {
	case ACB_ADAPTER_TYPE_A: {
		struct MessageUnit_A __iomem *reg = acb->pmuA;
		writel(ARCMSR_INBOUND_DRIVER_DATA_READ_OK, &reg->inbound_doorbell);
		}
		break;

	case ACB_ADAPTER_TYPE_B: {
		struct MessageUnit_B *reg = acb->pmuB;
		writel(ARCMSR_DRV2IOP_DATA_READ_OK, reg->drv2iop_doorbell);
		}
		break;
	case ACB_ADAPTER_TYPE_C: {
		struct MessageUnit_C __iomem *reg = acb->pmuC;
		writel(ARCMSR_HBCMU_DRV2IOP_DATA_READ_OK, &reg->inbound_doorbell);
		}
	}
}

static void arcmsr_iop_message_wrote(struct AdapterControlBlock *acb)
{
	switch (acb->adapter_type) {
	case ACB_ADAPTER_TYPE_A: {
		struct MessageUnit_A __iomem *reg = acb->pmuA;
		/*
		** push inbound doorbell tell iop, driver data write ok
		** and wait reply on next hwinterrupt for next Qbuffer post
		*/
		writel(ARCMSR_INBOUND_DRIVER_DATA_WRITE_OK, &reg->inbound_doorbell);
		}
		break;

	case ACB_ADAPTER_TYPE_B: {
		struct MessageUnit_B *reg = acb->pmuB;
		/*
		** push inbound doorbell tell iop, driver data write ok
		** and wait reply on next hwinterrupt for next Qbuffer post
		*/
		writel(ARCMSR_DRV2IOP_DATA_WRITE_OK, reg->drv2iop_doorbell);
		}
		break;
	case ACB_ADAPTER_TYPE_C: {
		struct MessageUnit_C __iomem *reg = acb->pmuC;
		/*
		** push inbound doorbell tell iop, driver data write ok
		** and wait reply on next hwinterrupt for next Qbuffer post
		*/
		writel(ARCMSR_HBCMU_DRV2IOP_DATA_WRITE_OK, &reg->inbound_doorbell);
		}
		break;
	}
}

struct QBUFFER __iomem *arcmsr_get_iop_rqbuffer(struct AdapterControlBlock *acb)
{
	struct QBUFFER __iomem *qbuffer = NULL;
	switch (acb->adapter_type) {

	case ACB_ADAPTER_TYPE_A: {
		struct MessageUnit_A __iomem *reg = acb->pmuA;
		qbuffer = (struct QBUFFER __iomem *)&reg->message_rbuffer;
		}
		break;

	case ACB_ADAPTER_TYPE_B: {
		struct MessageUnit_B *reg = acb->pmuB;
		qbuffer = (struct QBUFFER __iomem *)reg->message_rbuffer;
		}
		break;
	case ACB_ADAPTER_TYPE_C: {
		struct MessageUnit_C *phbcmu = (struct MessageUnit_C *)acb->pmuC;
		qbuffer = (struct QBUFFER __iomem *)&phbcmu->message_rbuffer;
		}
	}
	return qbuffer;
}

static struct QBUFFER __iomem *arcmsr_get_iop_wqbuffer(struct AdapterControlBlock *acb)
{
	struct QBUFFER __iomem *pqbuffer = NULL;
	switch (acb->adapter_type) {

	case ACB_ADAPTER_TYPE_A: {
		struct MessageUnit_A __iomem *reg = acb->pmuA;
		pqbuffer = (struct QBUFFER __iomem *) &reg->message_wbuffer;
		}
		break;

	case ACB_ADAPTER_TYPE_B: {
		struct MessageUnit_B  *reg = acb->pmuB;
		pqbuffer = (struct QBUFFER __iomem *)reg->message_wbuffer;
		}
		break;
	case ACB_ADAPTER_TYPE_C: {
		struct MessageUnit_C *reg = (struct MessageUnit_C *)acb->pmuC;
		pqbuffer = (struct QBUFFER __iomem *)&reg->message_wbuffer;
	}

	}
	return pqbuffer;
}

static void arcmsr_iop2drv_data_wrote_handle(struct AdapterControlBlock *acb)
{
	struct QBUFFER __iomem *prbuffer;
	struct QBUFFER *pQbuffer;
	uint8_t __iomem *iop_data;
	int32_t my_empty_len, iop_len, rqbuf_firstindex, rqbuf_lastindex;
	rqbuf_lastindex = acb->rqbuf_lastindex;
	rqbuf_firstindex = acb->rqbuf_firstindex;
	prbuffer = arcmsr_get_iop_rqbuffer(acb);
	iop_data = (uint8_t __iomem *)prbuffer->data;
	iop_len = prbuffer->data_len;
	my_empty_len = (rqbuf_firstindex - rqbuf_lastindex - 1) & (ARCMSR_MAX_QBUFFER - 1);

	if (my_empty_len >= iop_len)
	{
		while (iop_len > 0) {
			pQbuffer = (struct QBUFFER *)&acb->rqbuffer[rqbuf_lastindex];
			memcpy(pQbuffer, iop_data, 1);
			rqbuf_lastindex++;
			rqbuf_lastindex %= ARCMSR_MAX_QBUFFER;
			iop_data++;
			iop_len--;
		}
		acb->rqbuf_lastindex = rqbuf_lastindex;
		arcmsr_iop_message_read(acb);
	}

	else {
		acb->acb_flags |= ACB_F_IOPDATA_OVERFLOW;
	}
}

static void arcmsr_iop2drv_data_read_handle(struct AdapterControlBlock *acb)
{
	acb->acb_flags |= ACB_F_MESSAGE_WQBUFFER_READED;
	if (acb->wqbuf_firstindex != acb->wqbuf_lastindex) {
		uint8_t *pQbuffer;
		struct QBUFFER __iomem *pwbuffer;
		uint8_t __iomem *iop_data;
		int32_t allxfer_len = 0;

		acb->acb_flags &= (~ACB_F_MESSAGE_WQBUFFER_READED);
		pwbuffer = arcmsr_get_iop_wqbuffer(acb);
		iop_data = (uint8_t __iomem *)pwbuffer->data;

		while ((acb->wqbuf_firstindex != acb->wqbuf_lastindex) && \
							(allxfer_len < 124)) {
			pQbuffer = &acb->wqbuffer[acb->wqbuf_firstindex];
			memcpy(iop_data, pQbuffer, 1);
			acb->wqbuf_firstindex++;
			acb->wqbuf_firstindex %= ARCMSR_MAX_QBUFFER;
			iop_data++;
			allxfer_len++;
		}
		pwbuffer->data_len = allxfer_len;

		arcmsr_iop_message_wrote(acb);
	}

	if (acb->wqbuf_firstindex == acb->wqbuf_lastindex) {
		acb->acb_flags |= ACB_F_MESSAGE_WQBUFFER_CLEARED;
	}
}

static void arcmsr_hba_doorbell_isr(struct AdapterControlBlock *acb)
{
	uint32_t outbound_doorbell;
	struct MessageUnit_A __iomem *reg = acb->pmuA;
	outbound_doorbell = readl(&reg->outbound_doorbell);
	writel(outbound_doorbell, &reg->outbound_doorbell);
	if (outbound_doorbell & ARCMSR_OUTBOUND_IOP331_DATA_WRITE_OK) {
		arcmsr_iop2drv_data_wrote_handle(acb);
	}

	if (outbound_doorbell & ARCMSR_OUTBOUND_IOP331_DATA_READ_OK) {
		arcmsr_iop2drv_data_read_handle(acb);
	}
}
static void arcmsr_hbc_doorbell_isr(struct AdapterControlBlock *pACB)
{
	uint32_t outbound_doorbell;
	struct MessageUnit_C *reg = (struct MessageUnit_C *)pACB->pmuC;
	/*
	*******************************************************************
	**  Maybe here we need to check wrqbuffer_lock is lock or not
	**  DOORBELL: din! don!
	**  check if there are any mail need to pack from firmware
	*******************************************************************
	*/
	outbound_doorbell = readl(&reg->outbound_doorbell);
	writel(outbound_doorbell, &reg->outbound_doorbell_clear);/*clear interrupt*/
	if (outbound_doorbell & ARCMSR_HBCMU_IOP2DRV_DATA_WRITE_OK) {
		arcmsr_iop2drv_data_wrote_handle(pACB);
	}
	if (outbound_doorbell & ARCMSR_HBCMU_IOP2DRV_DATA_READ_OK) {
		arcmsr_iop2drv_data_read_handle(pACB);
	}
	if (outbound_doorbell & ARCMSR_HBCMU_IOP2DRV_MESSAGE_CMD_DONE) {
		arcmsr_hbc_message_isr(pACB);    /* messenger of "driver to iop commands" */
	}
	return;
}
static void arcmsr_hba_postqueue_isr(struct AdapterControlBlock *acb)
{
	uint32_t flag_ccb;
	struct MessageUnit_A __iomem *reg = acb->pmuA;
	struct ARCMSR_CDB *pARCMSR_CDB;
	struct CommandControlBlock *pCCB;
	bool error;
	while ((flag_ccb = readl(&reg->outbound_queueport)) != 0xFFFFFFFF) {
		pARCMSR_CDB = (struct ARCMSR_CDB *)(acb->vir2phy_offset + (flag_ccb << 5));/*frame must be 32 bytes aligned*/
		pCCB = container_of(pARCMSR_CDB, struct CommandControlBlock, arcmsr_cdb);
		error = (flag_ccb & ARCMSR_CCBREPLY_FLAG_ERROR_MODE0) ? true : false;
		arcmsr_drain_donequeue(acb, pCCB, error);
	}
}
static void arcmsr_hbb_postqueue_isr(struct AdapterControlBlock *acb)
{
	uint32_t index;
	uint32_t flag_ccb;
	struct MessageUnit_B *reg = acb->pmuB;
	struct ARCMSR_CDB *pARCMSR_CDB;
	struct CommandControlBlock *pCCB;
	bool error;
	index = reg->doneq_index;
	while ((flag_ccb = readl(&reg->done_qbuffer[index])) != 0) {
		writel(0, &reg->done_qbuffer[index]);
		pARCMSR_CDB = (struct ARCMSR_CDB *)(acb->vir2phy_offset+(flag_ccb << 5));/*frame must be 32 bytes aligned*/
		pCCB = container_of(pARCMSR_CDB, struct CommandControlBlock, arcmsr_cdb);
		error = (flag_ccb & ARCMSR_CCBREPLY_FLAG_ERROR_MODE0) ? true : false;
		arcmsr_drain_donequeue(acb, pCCB, error);
		index++;
		index %= ARCMSR_MAX_HBB_POSTQUEUE;
		reg->doneq_index = index;
	}
}

static void arcmsr_hbc_postqueue_isr(struct AdapterControlBlock *acb)
{
	struct MessageUnit_C *phbcmu;
	struct ARCMSR_CDB *arcmsr_cdb;
	struct CommandControlBlock *ccb;
	uint32_t flag_ccb, ccb_cdb_phy, throttling = 0;
	int error;

	phbcmu = (struct MessageUnit_C *)acb->pmuC;
	/* areca cdb command done */
	/* Use correct offset and size for syncing */

	while (readl(&phbcmu->host_int_status) &
	ARCMSR_HBCMU_OUTBOUND_POSTQUEUE_ISR){
	/* check if command done with no error*/
	flag_ccb = readl(&phbcmu->outbound_queueport_low);
	ccb_cdb_phy = (flag_ccb & 0xFFFFFFF0);/*frame must be 32 bytes aligned*/
	arcmsr_cdb = (struct ARCMSR_CDB *)(acb->vir2phy_offset + ccb_cdb_phy);
	ccb = container_of(arcmsr_cdb, struct CommandControlBlock, arcmsr_cdb);
	error = (flag_ccb & ARCMSR_CCBREPLY_FLAG_ERROR_MODE1) ? true : false;
	/* check if command done with no error */
	arcmsr_drain_donequeue(acb, ccb, error);
	if (throttling == ARCMSR_HBC_ISR_THROTTLING_LEVEL) {
		writel(ARCMSR_HBCMU_DRV2IOP_POSTQUEUE_THROTTLING, &phbcmu->inbound_doorbell);
		break;
	}
	throttling++;
	}
}
/*
**********************************************************************************
** Handle a message interrupt
**
** The only message interrupt we expect is in response to a query for the current adapter config.  
** We want this in order to compare the drivemap so that we can detect newly-attached drives.
**********************************************************************************
*/
static void arcmsr_hba_message_isr(struct AdapterControlBlock *acb)
{
	struct MessageUnit_A *reg  = acb->pmuA;
	/*clear interrupt and message state*/
	writel(ARCMSR_MU_OUTBOUND_MESSAGE0_INT, &reg->outbound_intstatus);
	schedule_work(&acb->arcmsr_do_message_isr_bh);
}
static void arcmsr_hbb_message_isr(struct AdapterControlBlock *acb)
{
	struct MessageUnit_B *reg  = acb->pmuB;

	/*clear interrupt and message state*/
	writel(ARCMSR_MESSAGE_INT_CLEAR_PATTERN, reg->iop2drv_doorbell);
	schedule_work(&acb->arcmsr_do_message_isr_bh);
}
/*
**********************************************************************************
** Handle a message interrupt
**
** The only message interrupt we expect is in response to a query for the
** current adapter config.
** We want this in order to compare the drivemap so that we can detect newly-attached drives.
**********************************************************************************
*/
static void arcmsr_hbc_message_isr(struct AdapterControlBlock *acb)
{
	struct MessageUnit_C *reg  = acb->pmuC;
	/*clear interrupt and message state*/
	writel(ARCMSR_HBCMU_IOP2DRV_MESSAGE_CMD_DONE_DOORBELL_CLEAR, &reg->outbound_doorbell_clear);
	schedule_work(&acb->arcmsr_do_message_isr_bh);
}

static int arcmsr_handle_hba_isr(struct AdapterControlBlock *acb)
{
	uint32_t outbound_intstatus;
	struct MessageUnit_A __iomem *reg = acb->pmuA;
	outbound_intstatus = readl(&reg->outbound_intstatus) &
		acb->outbound_int_enable;
	if (!(outbound_intstatus & ARCMSR_MU_OUTBOUND_HANDLE_INT))	{
		return 1;
	}
	writel(outbound_intstatus, &reg->outbound_intstatus);
	if (outbound_intstatus & ARCMSR_MU_OUTBOUND_DOORBELL_INT)	{
		arcmsr_hba_doorbell_isr(acb);
	}
	if (outbound_intstatus & ARCMSR_MU_OUTBOUND_POSTQUEUE_INT) {
		arcmsr_hba_postqueue_isr(acb);
	}
	if(outbound_intstatus & ARCMSR_MU_OUTBOUND_MESSAGE0_INT) 	{
		/* messenger of "driver to iop commands" */
		arcmsr_hba_message_isr(acb);
	}
	return 0;
}

static int arcmsr_handle_hbb_isr(struct AdapterControlBlock *acb)
{
	uint32_t outbound_doorbell;
	struct MessageUnit_B *reg = acb->pmuB;
	outbound_doorbell = readl(reg->iop2drv_doorbell) &
				acb->outbound_int_enable;
	if (!outbound_doorbell)
		return 1;

	writel(~outbound_doorbell, reg->iop2drv_doorbell);
	/*in case the last action of doorbell interrupt clearance is cached,
	this action can push HW to write down the clear bit*/
	readl(reg->iop2drv_doorbell);
	writel(ARCMSR_DRV2IOP_END_OF_INTERRUPT, reg->drv2iop_doorbell);
	if (outbound_doorbell & ARCMSR_IOP2DRV_DATA_WRITE_OK) {
		arcmsr_iop2drv_data_wrote_handle(acb);
	}
	if (outbound_doorbell & ARCMSR_IOP2DRV_DATA_READ_OK) {
		arcmsr_iop2drv_data_read_handle(acb);
	}
	if (outbound_doorbell & ARCMSR_IOP2DRV_CDB_DONE) {
		arcmsr_hbb_postqueue_isr(acb);
	}
	if(outbound_doorbell & ARCMSR_IOP2DRV_MESSAGE_CMD_DONE) {
		/* messenger of "driver to iop commands" */
		arcmsr_hbb_message_isr(acb);
	}
	return 0;
}

static int arcmsr_handle_hbc_isr(struct AdapterControlBlock *pACB)
{
	uint32_t host_interrupt_status;
	struct MessageUnit_C *phbcmu = (struct MessageUnit_C *)pACB->pmuC;
	/*
	*********************************************
	**   check outbound intstatus
	*********************************************
	*/
	host_interrupt_status = readl(&phbcmu->host_int_status);
	if (!host_interrupt_status) {
		/*it must be share irq*/
		return 1;
	}
	/* MU ioctl transfer doorbell interrupts*/
	if (host_interrupt_status & ARCMSR_HBCMU_OUTBOUND_DOORBELL_ISR) {
		arcmsr_hbc_doorbell_isr(pACB);   /* messenger of "ioctl message read write" */
	}
	/* MU post queue interrupts*/
	if (host_interrupt_status & ARCMSR_HBCMU_OUTBOUND_POSTQUEUE_ISR) {
		arcmsr_hbc_postqueue_isr(pACB);  /* messenger of "scsi commands" */
	}
	return 0;
}
static irqreturn_t arcmsr_interrupt(struct AdapterControlBlock *acb)
{
	switch (acb->adapter_type) {
	case ACB_ADAPTER_TYPE_A: {
		if (arcmsr_handle_hba_isr(acb)) {
			return IRQ_NONE;
		}
		}
		break;

	case ACB_ADAPTER_TYPE_B: {
		if (arcmsr_handle_hbb_isr(acb)) {
			return IRQ_NONE;
		}
		}
		break;
	 case ACB_ADAPTER_TYPE_C: {
		if (arcmsr_handle_hbc_isr(acb)) {
			return IRQ_NONE;
		}
		}
	}
	return IRQ_HANDLED;
}

static void arcmsr_iop_parking(struct AdapterControlBlock *acb)
{
	if (acb) {
		/* stop adapter background rebuild */
		if (acb->acb_flags & ACB_F_MSG_START_BGRB) {
			uint32_t intmask_org;
			acb->acb_flags &= ~ACB_F_MSG_START_BGRB;
			intmask_org = arcmsr_disable_outbound_ints(acb);
			arcmsr_stop_adapter_bgrb(acb);
			arcmsr_flush_adapter_cache(acb);
			arcmsr_enable_outbound_ints(acb, intmask_org);
		}
	}
}

void arcmsr_post_ioctldata2iop(struct AdapterControlBlock *acb)
{
	int32_t wqbuf_firstindex, wqbuf_lastindex;
	uint8_t *pQbuffer;
	struct QBUFFER __iomem *pwbuffer;
	uint8_t __iomem *iop_data;
	int32_t allxfer_len = 0;
	pwbuffer = arcmsr_get_iop_wqbuffer(acb);
	iop_data = (uint8_t __iomem *)pwbuffer->data;
	if (acb->acb_flags & ACB_F_MESSAGE_WQBUFFER_READED) {
		acb->acb_flags &= (~ACB_F_MESSAGE_WQBUFFER_READED);
		wqbuf_firstindex = acb->wqbuf_firstindex;
		wqbuf_lastindex = acb->wqbuf_lastindex;
		while ((wqbuf_firstindex != wqbuf_lastindex) && (allxfer_len < 124)) {
			pQbuffer = &acb->wqbuffer[wqbuf_firstindex];
			memcpy(iop_data, pQbuffer, 1);
			wqbuf_firstindex++;
			wqbuf_firstindex %= ARCMSR_MAX_QBUFFER;
			iop_data++;
			allxfer_len++;
		}
		acb->wqbuf_firstindex = wqbuf_firstindex;
		pwbuffer->data_len = allxfer_len;
		arcmsr_iop_message_wrote(acb);
	}
}

static int arcmsr_iop_message_xfer(struct AdapterControlBlock *acb,
					struct scsi_cmnd *cmd)
{
	struct CMD_MESSAGE_FIELD *pcmdmessagefld;
	int retvalue = 0, transfer_len = 0;
	char *buffer;
	struct scatterlist *sg;
	uint32_t controlcode = (uint32_t ) cmd->cmnd[5] << 24 |
						(uint32_t ) cmd->cmnd[6] << 16 |
						(uint32_t ) cmd->cmnd[7] << 8  |
						(uint32_t ) cmd->cmnd[8];
						/* 4 bytes: Areca io control code */
	sg = scsi_sglist(cmd);
	buffer = kmap_atomic(sg_page(sg)) + sg->offset;
	if (scsi_sg_count(cmd) > 1) {
		retvalue = ARCMSR_MESSAGE_FAIL;
		goto message_out;
	}
	transfer_len += sg->length;

	if (transfer_len > sizeof(struct CMD_MESSAGE_FIELD)) {
		retvalue = ARCMSR_MESSAGE_FAIL;
		goto message_out;
	}
	pcmdmessagefld = (struct CMD_MESSAGE_FIELD *) buffer;
	switch(controlcode) {

	case ARCMSR_MESSAGE_READ_RQBUFFER: {
		unsigned char *ver_addr;
		uint8_t *pQbuffer, *ptmpQbuffer;
		int32_t allxfer_len = 0;

		ver_addr = kmalloc(1032, GFP_ATOMIC);
		if (!ver_addr) {
			retvalue = ARCMSR_MESSAGE_FAIL;
			goto message_out;
		}
				
		ptmpQbuffer = ver_addr;
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

			struct QBUFFER __iomem *prbuffer;
			uint8_t __iomem *iop_data;
			int32_t iop_len;

			acb->acb_flags &= ~ACB_F_IOPDATA_OVERFLOW;
			prbuffer = arcmsr_get_iop_rqbuffer(acb);
			iop_data = prbuffer->data;
			iop_len = readl(&prbuffer->data_len);
			while (iop_len > 0) {
				acb->rqbuffer[acb->rqbuf_lastindex] = readb(iop_data);
				acb->rqbuf_lastindex++;
				acb->rqbuf_lastindex %= ARCMSR_MAX_QBUFFER;
				iop_data++;
				iop_len--;
			}
			arcmsr_iop_message_read(acb);
		}
		memcpy(pcmdmessagefld->messagedatabuffer, ver_addr, allxfer_len);
		pcmdmessagefld->cmdmessage.Length = allxfer_len;
		if(acb->fw_flag == FW_DEADLOCK) {
			pcmdmessagefld->cmdmessage.ReturnCode = ARCMSR_MESSAGE_RETURNCODE_BUS_HANG_ON;
		}else{
			pcmdmessagefld->cmdmessage.ReturnCode = ARCMSR_MESSAGE_RETURNCODE_OK;
		}
		kfree(ver_addr);
		}
		break;

	case ARCMSR_MESSAGE_WRITE_WQBUFFER: {
		unsigned char *ver_addr;
		int32_t my_empty_len, user_len, wqbuf_firstindex, wqbuf_lastindex;
		uint8_t *pQbuffer, *ptmpuserbuffer;

		ver_addr = kmalloc(1032, GFP_ATOMIC);
		if (!ver_addr) {
			retvalue = ARCMSR_MESSAGE_FAIL;
			goto message_out;
		}
		if(acb->fw_flag == FW_DEADLOCK) {
			pcmdmessagefld->cmdmessage.ReturnCode = 
			ARCMSR_MESSAGE_RETURNCODE_BUS_HANG_ON;
		}else{
			pcmdmessagefld->cmdmessage.ReturnCode = 
			ARCMSR_MESSAGE_RETURNCODE_OK;
		}
		ptmpuserbuffer = ver_addr;
		user_len = pcmdmessagefld->cmdmessage.Length;
		memcpy(ptmpuserbuffer, pcmdmessagefld->messagedatabuffer, user_len);
		wqbuf_lastindex = acb->wqbuf_lastindex;
		wqbuf_firstindex = acb->wqbuf_firstindex;
		if (wqbuf_lastindex != wqbuf_firstindex) {
			struct SENSE_DATA *sensebuffer =
				(struct SENSE_DATA *)cmd->sense_buffer;
			arcmsr_post_ioctldata2iop(acb);
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
					arcmsr_post_ioctldata2iop(acb);
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
			kfree(ver_addr);
		}
		break;

	case ARCMSR_MESSAGE_CLEAR_RQBUFFER: {
		uint8_t *pQbuffer = acb->rqbuffer;
		if (acb->acb_flags & ACB_F_IOPDATA_OVERFLOW) {
			acb->acb_flags &= ~ACB_F_IOPDATA_OVERFLOW;
			arcmsr_iop_message_read(acb);
		}
		acb->acb_flags |= ACB_F_MESSAGE_RQBUFFER_CLEARED;
		acb->rqbuf_firstindex = 0;
		acb->rqbuf_lastindex = 0;
		memset(pQbuffer, 0, ARCMSR_MAX_QBUFFER);
		if(acb->fw_flag == FW_DEADLOCK) {
			pcmdmessagefld->cmdmessage.ReturnCode =
			ARCMSR_MESSAGE_RETURNCODE_BUS_HANG_ON;
		}else{
			pcmdmessagefld->cmdmessage.ReturnCode =
			ARCMSR_MESSAGE_RETURNCODE_OK;
		}
		}
		break;

	case ARCMSR_MESSAGE_CLEAR_WQBUFFER: {
		uint8_t *pQbuffer = acb->wqbuffer;
		if(acb->fw_flag == FW_DEADLOCK) {
			pcmdmessagefld->cmdmessage.ReturnCode =
			ARCMSR_MESSAGE_RETURNCODE_BUS_HANG_ON;
		}else{
			pcmdmessagefld->cmdmessage.ReturnCode =
			ARCMSR_MESSAGE_RETURNCODE_OK;
		}

		if (acb->acb_flags & ACB_F_IOPDATA_OVERFLOW) {
			acb->acb_flags &= ~ACB_F_IOPDATA_OVERFLOW;
			arcmsr_iop_message_read(acb);
		}
		acb->acb_flags |=
			(ACB_F_MESSAGE_WQBUFFER_CLEARED |
				ACB_F_MESSAGE_WQBUFFER_READED);
		acb->wqbuf_firstindex = 0;
		acb->wqbuf_lastindex = 0;
		memset(pQbuffer, 0, ARCMSR_MAX_QBUFFER);
		}
		break;

	case ARCMSR_MESSAGE_CLEAR_ALLQBUFFER: {
		uint8_t *pQbuffer;

		if (acb->acb_flags & ACB_F_IOPDATA_OVERFLOW) {
			acb->acb_flags &= ~ACB_F_IOPDATA_OVERFLOW;
			arcmsr_iop_message_read(acb);
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
		memset(pQbuffer, 0, sizeof(struct QBUFFER));
		pQbuffer = acb->wqbuffer;
		memset(pQbuffer, 0, sizeof(struct QBUFFER));
		if(acb->fw_flag == FW_DEADLOCK) {
			pcmdmessagefld->cmdmessage.ReturnCode =
			ARCMSR_MESSAGE_RETURNCODE_BUS_HANG_ON;
		}else{
			pcmdmessagefld->cmdmessage.ReturnCode =
			ARCMSR_MESSAGE_RETURNCODE_OK;
		}
		}
		break;

	case ARCMSR_MESSAGE_RETURN_CODE_3F: {
		if(acb->fw_flag == FW_DEADLOCK) {
			pcmdmessagefld->cmdmessage.ReturnCode =
			ARCMSR_MESSAGE_RETURNCODE_BUS_HANG_ON;
		}else{
			pcmdmessagefld->cmdmessage.ReturnCode =
			ARCMSR_MESSAGE_RETURNCODE_3F;
		}
		break;
		}
	case ARCMSR_MESSAGE_SAY_HELLO: {
		int8_t *hello_string = "Hello! I am ARCMSR";
		if(acb->fw_flag == FW_DEADLOCK) {
			pcmdmessagefld->cmdmessage.ReturnCode =
			ARCMSR_MESSAGE_RETURNCODE_BUS_HANG_ON;
		}else{
			pcmdmessagefld->cmdmessage.ReturnCode =
			ARCMSR_MESSAGE_RETURNCODE_OK;
		}
		memcpy(pcmdmessagefld->messagedatabuffer, hello_string
			, (int16_t)strlen(hello_string));
		}
		break;

	case ARCMSR_MESSAGE_SAY_GOODBYE:
		if(acb->fw_flag == FW_DEADLOCK) {
			pcmdmessagefld->cmdmessage.ReturnCode =
			ARCMSR_MESSAGE_RETURNCODE_BUS_HANG_ON;
		}
		arcmsr_iop_parking(acb);
		break;

	case ARCMSR_MESSAGE_FLUSH_ADAPTER_CACHE:
		if(acb->fw_flag == FW_DEADLOCK) {
			pcmdmessagefld->cmdmessage.ReturnCode =
			ARCMSR_MESSAGE_RETURNCODE_BUS_HANG_ON;
		}
		arcmsr_flush_adapter_cache(acb);
		break;

	default:
		retvalue = ARCMSR_MESSAGE_FAIL;
	}
	message_out:
	sg = scsi_sglist(cmd);
	kunmap_atomic(buffer - sg->offset);
	return retvalue;
}

static struct CommandControlBlock *arcmsr_get_freeccb(struct AdapterControlBlock *acb)
{
	struct list_head *head = &acb->ccb_free_list;
	struct CommandControlBlock *ccb = NULL;
	unsigned long flags;
	spin_lock_irqsave(&acb->ccblist_lock, flags);
	if (!list_empty(head)) {
		ccb = list_entry(head->next, struct CommandControlBlock, list);
		list_del_init(&ccb->list);
	}else{
		spin_unlock_irqrestore(&acb->ccblist_lock, flags);
		return 0;
	}
	spin_unlock_irqrestore(&acb->ccblist_lock, flags);
	return ccb;
}

static void arcmsr_handle_virtual_command(struct AdapterControlBlock *acb,
		struct scsi_cmnd *cmd)
{
	switch (cmd->cmnd[0]) {
	case INQUIRY: {
		unsigned char inqdata[36];
		char *buffer;
		struct scatterlist *sg;

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
		/* ISO, ECMA, & ANSI versions */
		inqdata[4] = 31;
		/* length of additional data */
		strncpy(&inqdata[8], "Areca   ", 8);
		/* Vendor Identification */
		strncpy(&inqdata[16], "RAID controller ", 16);
		/* Product Identification */
		strncpy(&inqdata[32], "R001", 4); /* Product Revision */

		sg = scsi_sglist(cmd);
		buffer = kmap_atomic(sg_page(sg)) + sg->offset;

		memcpy(buffer, inqdata, sizeof(inqdata));
		sg = scsi_sglist(cmd);
		kunmap_atomic(buffer - sg->offset);

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

static int arcmsr_queue_command_lck(struct scsi_cmnd *cmd,
	void (* done)(struct scsi_cmnd *))
{
	struct Scsi_Host *host = cmd->device->host;
	struct AdapterControlBlock *acb = (struct AdapterControlBlock *) host->hostdata;
	struct CommandControlBlock *ccb;
	int target = cmd->device->id;
	int lun = cmd->device->lun;
	uint8_t scsicmd = cmd->cmnd[0];
	cmd->scsi_done = done;
	cmd->host_scribble = NULL;
	cmd->result = 0;
	if ((scsicmd == SYNCHRONIZE_CACHE) ||(scsicmd == SEND_DIAGNOSTIC)){
		if(acb->devstate[target][lun] == ARECA_RAID_GONE) {
    			cmd->result = (DID_NO_CONNECT << 16);
		}
		cmd->scsi_done(cmd);
		return 0;
	}
	if (target == 16) {
		/* virtual device for iop message transfer */
		arcmsr_handle_virtual_command(acb, cmd);
		return 0;
	}
	if (atomic_read(&acb->ccboutstandingcount) >=
			ARCMSR_MAX_OUTSTANDING_CMD)
		return SCSI_MLQUEUE_HOST_BUSY;
	ccb = arcmsr_get_freeccb(acb);
	if (!ccb)
		return SCSI_MLQUEUE_HOST_BUSY;
	if (arcmsr_build_ccb( acb, ccb, cmd ) == FAILED) {
		cmd->result = (DID_ERROR << 16) | (RESERVATION_CONFLICT << 1);
		cmd->scsi_done(cmd);
		return 0;
	}
	arcmsr_post_ccb(acb, ccb);
	return 0;
}

static DEF_SCSI_QCMD(arcmsr_queue_command)

static bool arcmsr_get_hba_config(struct AdapterControlBlock *acb)
{
	struct MessageUnit_A __iomem *reg = acb->pmuA;
	char *acb_firm_model = acb->firm_model;
	char *acb_firm_version = acb->firm_version;
	char *acb_device_map = acb->device_map;
	char __iomem *iop_firm_model = (char __iomem *)(&reg->message_rwbuffer[15]);
	char __iomem *iop_firm_version = (char __iomem *)(&reg->message_rwbuffer[17]);
	char __iomem *iop_device_map = (char __iomem *)(&reg->message_rwbuffer[21]);
	int count;
	writel(ARCMSR_INBOUND_MESG0_GET_CONFIG, &reg->inbound_msgaddr0);
	if (!arcmsr_hba_wait_msgint_ready(acb)) {
		printk(KERN_NOTICE "arcmsr%d: wait 'get adapter firmware \
			miscellaneous data' timeout \n", acb->host->host_no);
		return false;
	}
	count = 8;
	while (count){
		*acb_firm_model = readb(iop_firm_model);
		acb_firm_model++;
		iop_firm_model++;
		count--;
	}

	count = 16;
	while (count){
		*acb_firm_version = readb(iop_firm_version);
		acb_firm_version++;
		iop_firm_version++;
		count--;
	}

	count=16;
	while(count){
		*acb_device_map = readb(iop_device_map);
		acb_device_map++;
		iop_device_map++;
		count--;
	}
	printk(KERN_NOTICE "Areca RAID Controller%d: F/W %s & Model %s\n", 
		acb->host->host_no,
		acb->firm_version,
		acb->firm_model);
	acb->signature = readl(&reg->message_rwbuffer[0]);
	acb->firm_request_len = readl(&reg->message_rwbuffer[1]);
	acb->firm_numbers_queue = readl(&reg->message_rwbuffer[2]);
	acb->firm_sdram_size = readl(&reg->message_rwbuffer[3]);
	acb->firm_hd_channels = readl(&reg->message_rwbuffer[4]);
	acb->firm_cfg_version = readl(&reg->message_rwbuffer[25]);  /*firm_cfg_version,25,100-103*/
	return true;
}
static bool arcmsr_get_hbb_config(struct AdapterControlBlock *acb)
{
	struct MessageUnit_B *reg = acb->pmuB;
	struct pci_dev *pdev = acb->pdev;
	void *dma_coherent;
	dma_addr_t dma_coherent_handle;
	char *acb_firm_model = acb->firm_model;
	char *acb_firm_version = acb->firm_version;
	char *acb_device_map = acb->device_map;
	char __iomem *iop_firm_model;
	/*firm_model,15,60-67*/
	char __iomem *iop_firm_version;
	/*firm_version,17,68-83*/
	char __iomem *iop_device_map;
	/*firm_version,21,84-99*/
	int count;
	dma_coherent = dma_alloc_coherent(&pdev->dev, sizeof(struct MessageUnit_B), &dma_coherent_handle, GFP_KERNEL);
	if (!dma_coherent){
		printk(KERN_NOTICE "arcmsr%d: dma_alloc_coherent got error for hbb mu\n", acb->host->host_no);
		return false;
	}
	acb->dma_coherent_handle_hbb_mu = dma_coherent_handle;
	reg = (struct MessageUnit_B *)dma_coherent;
	acb->pmuB = reg;
	reg->drv2iop_doorbell= (uint32_t __iomem *)((unsigned long)acb->mem_base0 + ARCMSR_DRV2IOP_DOORBELL);
	reg->drv2iop_doorbell_mask = (uint32_t __iomem *)((unsigned long)acb->mem_base0 + ARCMSR_DRV2IOP_DOORBELL_MASK);
	reg->iop2drv_doorbell = (uint32_t __iomem *)((unsigned long)acb->mem_base0 + ARCMSR_IOP2DRV_DOORBELL);
	reg->iop2drv_doorbell_mask = (uint32_t __iomem *)((unsigned long)acb->mem_base0 + ARCMSR_IOP2DRV_DOORBELL_MASK);
	reg->message_wbuffer = (uint32_t __iomem *)((unsigned long)acb->mem_base1 + ARCMSR_MESSAGE_WBUFFER);
	reg->message_rbuffer =  (uint32_t __iomem *)((unsigned long)acb->mem_base1 + ARCMSR_MESSAGE_RBUFFER);
	reg->message_rwbuffer = (uint32_t __iomem *)((unsigned long)acb->mem_base1 + ARCMSR_MESSAGE_RWBUFFER);
	iop_firm_model = (char __iomem *)(&reg->message_rwbuffer[15]);	/*firm_model,15,60-67*/
	iop_firm_version = (char __iomem *)(&reg->message_rwbuffer[17]);	/*firm_version,17,68-83*/
	iop_device_map = (char __iomem *)(&reg->message_rwbuffer[21]);	/*firm_version,21,84-99*/

	writel(ARCMSR_MESSAGE_GET_CONFIG, reg->drv2iop_doorbell);
	if (!arcmsr_hbb_wait_msgint_ready(acb)) {
		printk(KERN_NOTICE "arcmsr%d: wait 'get adapter firmware \
			miscellaneous data' timeout \n", acb->host->host_no);
		return false;
	}
	count = 8;
	while (count){
		*acb_firm_model = readb(iop_firm_model);
		acb_firm_model++;
		iop_firm_model++;
		count--;
	}
	count = 16;
	while (count){
		*acb_firm_version = readb(iop_firm_version);
		acb_firm_version++;
		iop_firm_version++;
		count--;
	}

	count = 16;
	while(count){
		*acb_device_map = readb(iop_device_map);
		acb_device_map++;
		iop_device_map++;
		count--;
	}
	
	printk(KERN_NOTICE "Areca RAID Controller%d: F/W %s & Model %s\n",
		acb->host->host_no,
		acb->firm_version,
		acb->firm_model);

	acb->signature = readl(&reg->message_rwbuffer[1]);
	/*firm_signature,1,00-03*/
	acb->firm_request_len = readl(&reg->message_rwbuffer[2]);
	/*firm_request_len,1,04-07*/
	acb->firm_numbers_queue = readl(&reg->message_rwbuffer[3]);
	/*firm_numbers_queue,2,08-11*/
	acb->firm_sdram_size = readl(&reg->message_rwbuffer[4]);
	/*firm_sdram_size,3,12-15*/
	acb->firm_hd_channels = readl(&reg->message_rwbuffer[5]);
	/*firm_ide_channels,4,16-19*/
	acb->firm_cfg_version = readl(&reg->message_rwbuffer[25]);  /*firm_cfg_version,25,100-103*/
	/*firm_ide_channels,4,16-19*/
	return true;
}

static bool arcmsr_get_hbc_config(struct AdapterControlBlock *pACB)
{
	uint32_t intmask_org, Index, firmware_state = 0;
	struct MessageUnit_C *reg = pACB->pmuC;
	char *acb_firm_model = pACB->firm_model;
	char *acb_firm_version = pACB->firm_version;
	char *iop_firm_model = (char *)(&reg->msgcode_rwbuffer[15]);    /*firm_model,15,60-67*/
	char *iop_firm_version = (char *)(&reg->msgcode_rwbuffer[17]);  /*firm_version,17,68-83*/
	int count;
	/* disable all outbound interrupt */
	intmask_org = readl(&reg->host_int_mask); /* disable outbound message0 int */
	writel(intmask_org|ARCMSR_HBCMU_ALL_INTMASKENABLE, &reg->host_int_mask);
	/* wait firmware ready */
	do {
		firmware_state = readl(&reg->outbound_msgaddr1);
	} while ((firmware_state & ARCMSR_HBCMU_MESSAGE_FIRMWARE_OK) == 0);
	/* post "get config" instruction */
	writel(ARCMSR_INBOUND_MESG0_GET_CONFIG, &reg->inbound_msgaddr0);
	writel(ARCMSR_HBCMU_DRV2IOP_MESSAGE_CMD_DONE, &reg->inbound_doorbell);
	/* wait message ready */
	for (Index = 0; Index < 2000; Index++) {
		if (readl(&reg->outbound_doorbell) & ARCMSR_HBCMU_IOP2DRV_MESSAGE_CMD_DONE) {
			writel(ARCMSR_HBCMU_IOP2DRV_MESSAGE_CMD_DONE_DOORBELL_CLEAR, &reg->outbound_doorbell_clear);/*clear interrupt*/
			break;
		}
		udelay(10);
	} /*max 1 seconds*/
	if (Index >= 2000) {
		printk(KERN_NOTICE "arcmsr%d: wait 'get adapter firmware \
			miscellaneous data' timeout \n", pACB->host->host_no);
		return false;
	}
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
	printk(KERN_NOTICE "Areca RAID Controller%d: F/W %s & Model %s\n",
		pACB->host->host_no,
		pACB->firm_version,
		pACB->firm_model);
	pACB->firm_request_len = readl(&reg->msgcode_rwbuffer[1]);   /*firm_request_len,1,04-07*/
	pACB->firm_numbers_queue = readl(&reg->msgcode_rwbuffer[2]); /*firm_numbers_queue,2,08-11*/
	pACB->firm_sdram_size = readl(&reg->msgcode_rwbuffer[3]);    /*firm_sdram_size,3,12-15*/
	pACB->firm_hd_channels = readl(&reg->msgcode_rwbuffer[4]);  /*firm_ide_channels,4,16-19*/
	pACB->firm_cfg_version = readl(&reg->msgcode_rwbuffer[25]);  /*firm_cfg_version,25,100-103*/
	/*all interrupt service will be enable at arcmsr_iop_init*/
	return true;
}
static bool arcmsr_get_firmware_spec(struct AdapterControlBlock *acb)
{
	if (acb->adapter_type == ACB_ADAPTER_TYPE_A)
		return arcmsr_get_hba_config(acb);
	else if (acb->adapter_type == ACB_ADAPTER_TYPE_B)
		return arcmsr_get_hbb_config(acb);
	else
		return arcmsr_get_hbc_config(acb);
}

static int arcmsr_polling_hba_ccbdone(struct AdapterControlBlock *acb,
	struct CommandControlBlock *poll_ccb)
{
	struct MessageUnit_A __iomem *reg = acb->pmuA;
	struct CommandControlBlock *ccb;
	struct ARCMSR_CDB *arcmsr_cdb;
	uint32_t flag_ccb, outbound_intstatus, poll_ccb_done = 0, poll_count = 0;
	int rtn;
	bool error;
	polling_hba_ccb_retry:
	poll_count++;
	outbound_intstatus = readl(&reg->outbound_intstatus) & acb->outbound_int_enable;
	writel(outbound_intstatus, &reg->outbound_intstatus);/*clear interrupt*/
	while (1) {
		if ((flag_ccb = readl(&reg->outbound_queueport)) == 0xFFFFFFFF) {
			if (poll_ccb_done){
				rtn = SUCCESS;
				break;
			}else {
				msleep(25);
				if (poll_count > 100){
					rtn = FAILED;
					break;
				}
				goto polling_hba_ccb_retry;
			}
		}
		arcmsr_cdb = (struct ARCMSR_CDB *)(acb->vir2phy_offset + (flag_ccb << 5));
		ccb = container_of(arcmsr_cdb, struct CommandControlBlock, arcmsr_cdb);
		poll_ccb_done = (ccb == poll_ccb) ? 1:0;
		if ((ccb->acb != acb) || (ccb->startdone != ARCMSR_CCB_START)) {
			if ((ccb->startdone == ARCMSR_CCB_ABORTED) || (ccb == poll_ccb)) {
				printk(KERN_NOTICE "arcmsr%d: scsi id = %d lun = %d ccb = '0x%p'"
					" poll command abort successfully \n"
					, acb->host->host_no
					, ccb->pcmd->device->id
					, ccb->pcmd->device->lun
					, ccb);
				ccb->pcmd->result = DID_ABORT << 16;
				arcmsr_ccb_complete(ccb);
				continue;
			}
			printk(KERN_NOTICE "arcmsr%d: polling get an illegal ccb"
				" command done ccb = '0x%p'"
				"ccboutstandingcount = %d \n"
				, acb->host->host_no
				, ccb
				, atomic_read(&acb->ccboutstandingcount));
			continue;
		}
		error = (flag_ccb & ARCMSR_CCBREPLY_FLAG_ERROR_MODE0) ? true : false;
		arcmsr_report_ccb_state(acb, ccb, error);
	}
	return rtn;
}

static int arcmsr_polling_hbb_ccbdone(struct AdapterControlBlock *acb,
					struct CommandControlBlock *poll_ccb)
{
	struct MessageUnit_B *reg = acb->pmuB;
	struct ARCMSR_CDB *arcmsr_cdb;
	struct CommandControlBlock *ccb;
	uint32_t flag_ccb, poll_ccb_done = 0, poll_count = 0;
	int index, rtn;
	bool error;
	polling_hbb_ccb_retry:

	poll_count++;
	/* clear doorbell interrupt */
	writel(ARCMSR_DOORBELL_INT_CLEAR_PATTERN, reg->iop2drv_doorbell);
	while(1){
		index = reg->doneq_index;
		if ((flag_ccb = readl(&reg->done_qbuffer[index])) == 0) {
			if (poll_ccb_done){
				rtn = SUCCESS;
				break;
			}else {
				msleep(25);
				if (poll_count > 100){
					rtn = FAILED;
					break;
				}
				goto polling_hbb_ccb_retry;
			}
		}
		writel(0, &reg->done_qbuffer[index]);
		index++;
		/*if last index number set it to 0 */
		index %= ARCMSR_MAX_HBB_POSTQUEUE;
		reg->doneq_index = index;
		/* check if command done with no error*/
		arcmsr_cdb = (struct ARCMSR_CDB *)(acb->vir2phy_offset + (flag_ccb << 5));
		ccb = container_of(arcmsr_cdb, struct CommandControlBlock, arcmsr_cdb);
		poll_ccb_done = (ccb == poll_ccb) ? 1:0;
		if ((ccb->acb != acb) || (ccb->startdone != ARCMSR_CCB_START)) {
			if ((ccb->startdone == ARCMSR_CCB_ABORTED) || (ccb == poll_ccb)) {
				printk(KERN_NOTICE "arcmsr%d: scsi id = %d lun = %d ccb = '0x%p'"
					" poll command abort successfully \n"
					,acb->host->host_no
					,ccb->pcmd->device->id
					,ccb->pcmd->device->lun
					,ccb);
				ccb->pcmd->result = DID_ABORT << 16;
				arcmsr_ccb_complete(ccb);
				continue;
			}
			printk(KERN_NOTICE "arcmsr%d: polling get an illegal ccb"
				" command done ccb = '0x%p'"
				"ccboutstandingcount = %d \n"
				, acb->host->host_no
				, ccb
				, atomic_read(&acb->ccboutstandingcount));
			continue;
		} 
		error = (flag_ccb & ARCMSR_CCBREPLY_FLAG_ERROR_MODE0) ? true : false;
		arcmsr_report_ccb_state(acb, ccb, error);
	}
	return rtn;
}

static int arcmsr_polling_hbc_ccbdone(struct AdapterControlBlock *acb, struct CommandControlBlock *poll_ccb)
{
	struct MessageUnit_C *reg = (struct MessageUnit_C *)acb->pmuC;
	uint32_t flag_ccb, ccb_cdb_phy;
	struct ARCMSR_CDB *arcmsr_cdb;
	bool error;
	struct CommandControlBlock *pCCB;
	uint32_t poll_ccb_done = 0, poll_count = 0;
	int rtn;
polling_hbc_ccb_retry:
	poll_count++;
	while (1) {
		if ((readl(&reg->host_int_status) & ARCMSR_HBCMU_OUTBOUND_POSTQUEUE_ISR) == 0) {
			if (poll_ccb_done) {
				rtn = SUCCESS;
				break;
			} else {
				msleep(25);
				if (poll_count > 100) {
					rtn = FAILED;
					break;
				}
				goto polling_hbc_ccb_retry;
			}
		}
		flag_ccb = readl(&reg->outbound_queueport_low);
		ccb_cdb_phy = (flag_ccb & 0xFFFFFFF0);
		arcmsr_cdb = (struct ARCMSR_CDB *)(acb->vir2phy_offset + ccb_cdb_phy);/*frame must be 32 bytes aligned*/
		pCCB = container_of(arcmsr_cdb, struct CommandControlBlock, arcmsr_cdb);
		poll_ccb_done = (pCCB == poll_ccb) ? 1 : 0;
		/* check ifcommand done with no error*/
		if ((pCCB->acb != acb) || (pCCB->startdone != ARCMSR_CCB_START)) {
			if (pCCB->startdone == ARCMSR_CCB_ABORTED) {
				printk(KERN_NOTICE "arcmsr%d: scsi id = %d lun = %d ccb = '0x%p'"
					" poll command abort successfully \n"
					, acb->host->host_no
					, pCCB->pcmd->device->id
					, pCCB->pcmd->device->lun
					, pCCB);
					pCCB->pcmd->result = DID_ABORT << 16;
					arcmsr_ccb_complete(pCCB);
				continue;
			}
			printk(KERN_NOTICE "arcmsr%d: polling get an illegal ccb"
				" command done ccb = '0x%p'"
				"ccboutstandingcount = %d \n"
				, acb->host->host_no
				, pCCB
				, atomic_read(&acb->ccboutstandingcount));
			continue;
		}
		error = (flag_ccb & ARCMSR_CCBREPLY_FLAG_ERROR_MODE1) ? true : false;
		arcmsr_report_ccb_state(acb, pCCB, error);
	}
	return rtn;
}
static int arcmsr_polling_ccbdone(struct AdapterControlBlock *acb,
					struct CommandControlBlock *poll_ccb)
{
	int rtn = 0;
	switch (acb->adapter_type) {

	case ACB_ADAPTER_TYPE_A: {
		rtn = arcmsr_polling_hba_ccbdone(acb, poll_ccb);
		}
		break;

	case ACB_ADAPTER_TYPE_B: {
		rtn = arcmsr_polling_hbb_ccbdone(acb, poll_ccb);
		}
		break;
	case ACB_ADAPTER_TYPE_C: {
		rtn = arcmsr_polling_hbc_ccbdone(acb, poll_ccb);
		}
	}
	return rtn;
}

static int arcmsr_iop_confirm(struct AdapterControlBlock *acb)
{
	uint32_t cdb_phyaddr, cdb_phyaddr_hi32;
	dma_addr_t dma_coherent_handle;
	/*
	********************************************************************
	** here we need to tell iop 331 our freeccb.HighPart
	** if freeccb.HighPart is not zero
	********************************************************************
	*/
	dma_coherent_handle = acb->dma_coherent_handle;
	cdb_phyaddr = (uint32_t)(dma_coherent_handle);
	cdb_phyaddr_hi32 = (uint32_t)((cdb_phyaddr >> 16) >> 16);
	acb->cdb_phyaddr_hi32 = cdb_phyaddr_hi32;
	/*
	***********************************************************************
	**    if adapter type B, set window of "post command Q"
	***********************************************************************
	*/
	switch (acb->adapter_type) {

	case ACB_ADAPTER_TYPE_A: {
		if (cdb_phyaddr_hi32 != 0) {
			struct MessageUnit_A __iomem *reg = acb->pmuA;
			uint32_t intmask_org;
			intmask_org = arcmsr_disable_outbound_ints(acb);
			writel(ARCMSR_SIGNATURE_SET_CONFIG, \
						&reg->message_rwbuffer[0]);
			writel(cdb_phyaddr_hi32, &reg->message_rwbuffer[1]);
			writel(ARCMSR_INBOUND_MESG0_SET_CONFIG, \
							&reg->inbound_msgaddr0);
			if (!arcmsr_hba_wait_msgint_ready(acb)) {
				printk(KERN_NOTICE "arcmsr%d: ""set ccb high \
				part physical address timeout\n",
				acb->host->host_no);
				return 1;
			}
			arcmsr_enable_outbound_ints(acb, intmask_org);
		}
		}
		break;

	case ACB_ADAPTER_TYPE_B: {
		unsigned long post_queue_phyaddr;
		uint32_t __iomem *rwbuffer;

		struct MessageUnit_B *reg = acb->pmuB;
		uint32_t intmask_org;
		intmask_org = arcmsr_disable_outbound_ints(acb);
		reg->postq_index = 0;
		reg->doneq_index = 0;
		writel(ARCMSR_MESSAGE_SET_POST_WINDOW, reg->drv2iop_doorbell);
		if (!arcmsr_hbb_wait_msgint_ready(acb)) {
			printk(KERN_NOTICE "arcmsr%d:can not set diver mode\n", \
				acb->host->host_no);
			return 1;
		}
		post_queue_phyaddr = acb->dma_coherent_handle_hbb_mu;
		rwbuffer = reg->message_rwbuffer;
		/* driver "set config" signature */
		writel(ARCMSR_SIGNATURE_SET_CONFIG, rwbuffer++);
		/* normal should be zero */
		writel(cdb_phyaddr_hi32, rwbuffer++);
		/* postQ size (256 + 8)*4	 */
		writel(post_queue_phyaddr, rwbuffer++);
		/* doneQ size (256 + 8)*4	 */
		writel(post_queue_phyaddr + 1056, rwbuffer++);
		/* ccb maxQ size must be --> [(256 + 8)*4]*/
		writel(1056, rwbuffer);

		writel(ARCMSR_MESSAGE_SET_CONFIG, reg->drv2iop_doorbell);
		if (!arcmsr_hbb_wait_msgint_ready(acb)) {
			printk(KERN_NOTICE "arcmsr%d: 'set command Q window' \
			timeout \n",acb->host->host_no);
			return 1;
		}
		arcmsr_hbb_enable_driver_mode(acb);
		arcmsr_enable_outbound_ints(acb, intmask_org);
		}
		break;
	case ACB_ADAPTER_TYPE_C: {
		if (cdb_phyaddr_hi32 != 0) {
			struct MessageUnit_C *reg = (struct MessageUnit_C *)acb->pmuC;

			printk(KERN_NOTICE "arcmsr%d: cdb_phyaddr_hi32=0x%x\n",
					acb->adapter_index, cdb_phyaddr_hi32);
			writel(ARCMSR_SIGNATURE_SET_CONFIG, &reg->msgcode_rwbuffer[0]);
			writel(cdb_phyaddr_hi32, &reg->msgcode_rwbuffer[1]);
			writel(ARCMSR_INBOUND_MESG0_SET_CONFIG, &reg->inbound_msgaddr0);
			writel(ARCMSR_HBCMU_DRV2IOP_MESSAGE_CMD_DONE, &reg->inbound_doorbell);
			if (!arcmsr_hbc_wait_msgint_ready(acb)) {
				printk(KERN_NOTICE "arcmsr%d: 'set command Q window' \
				timeout \n", acb->host->host_no);
				return 1;
			}
		}
		}
	}
	return 0;
}

static void arcmsr_wait_firmware_ready(struct AdapterControlBlock *acb)
{
	uint32_t firmware_state = 0;
	switch (acb->adapter_type) {

	case ACB_ADAPTER_TYPE_A: {
		struct MessageUnit_A __iomem *reg = acb->pmuA;
		do {
			firmware_state = readl(&reg->outbound_msgaddr1);
		} while ((firmware_state & ARCMSR_OUTBOUND_MESG1_FIRMWARE_OK) == 0);
		}
		break;

	case ACB_ADAPTER_TYPE_B: {
		struct MessageUnit_B *reg = acb->pmuB;
		do {
			firmware_state = readl(reg->iop2drv_doorbell);
		} while ((firmware_state & ARCMSR_MESSAGE_FIRMWARE_OK) == 0);
		writel(ARCMSR_DRV2IOP_END_OF_INTERRUPT, reg->drv2iop_doorbell);
		}
		break;
	case ACB_ADAPTER_TYPE_C: {
		struct MessageUnit_C *reg = (struct MessageUnit_C *)acb->pmuC;
		do {
			firmware_state = readl(&reg->outbound_msgaddr1);
		} while ((firmware_state & ARCMSR_HBCMU_MESSAGE_FIRMWARE_OK) == 0);
		}
	}
}

static void arcmsr_request_hba_device_map(struct AdapterControlBlock *acb)
{
	struct MessageUnit_A __iomem *reg = acb->pmuA;
	if (unlikely(atomic_read(&acb->rq_map_token) == 0) || ((acb->acb_flags & ACB_F_BUS_RESET) != 0 ) || ((acb->acb_flags & ACB_F_ABORT) != 0 )){
		mod_timer(&acb->eternal_timer, jiffies + msecs_to_jiffies(6 * HZ));
		return;
	} else {
		acb->fw_flag = FW_NORMAL;
		if (atomic_read(&acb->ante_token_value) == atomic_read(&acb->rq_map_token)){
			atomic_set(&acb->rq_map_token, 16);
		}
		atomic_set(&acb->ante_token_value, atomic_read(&acb->rq_map_token));
		if (atomic_dec_and_test(&acb->rq_map_token)) {
			mod_timer(&acb->eternal_timer, jiffies + msecs_to_jiffies(6 * HZ));
			return;
		}
		writel(ARCMSR_INBOUND_MESG0_GET_CONFIG, &reg->inbound_msgaddr0);
		mod_timer(&acb->eternal_timer, jiffies + msecs_to_jiffies(6 * HZ));
	}
	return;
}

static void arcmsr_request_hbb_device_map(struct AdapterControlBlock *acb)
{
	struct MessageUnit_B __iomem *reg = acb->pmuB;
	if (unlikely(atomic_read(&acb->rq_map_token) == 0) || ((acb->acb_flags & ACB_F_BUS_RESET) != 0 ) || ((acb->acb_flags & ACB_F_ABORT) != 0 )){
		mod_timer(&acb->eternal_timer, jiffies + msecs_to_jiffies(6 * HZ));
		return;
	} else {
		acb->fw_flag = FW_NORMAL;
		if (atomic_read(&acb->ante_token_value) == atomic_read(&acb->rq_map_token)) {
			atomic_set(&acb->rq_map_token, 16);
		}
		atomic_set(&acb->ante_token_value, atomic_read(&acb->rq_map_token));
		if (atomic_dec_and_test(&acb->rq_map_token)) {
			mod_timer(&acb->eternal_timer, jiffies + msecs_to_jiffies(6 * HZ));
			return;
		}
		writel(ARCMSR_MESSAGE_GET_CONFIG, reg->drv2iop_doorbell);
		mod_timer(&acb->eternal_timer, jiffies + msecs_to_jiffies(6 * HZ));
	}
	return;
}

static void arcmsr_request_hbc_device_map(struct AdapterControlBlock *acb)
{
	struct MessageUnit_C __iomem *reg = acb->pmuC;
	if (unlikely(atomic_read(&acb->rq_map_token) == 0) || ((acb->acb_flags & ACB_F_BUS_RESET) != 0) || ((acb->acb_flags & ACB_F_ABORT) != 0)) {
		mod_timer(&acb->eternal_timer, jiffies + msecs_to_jiffies(6 * HZ));
		return;
	} else {
		acb->fw_flag = FW_NORMAL;
		if (atomic_read(&acb->ante_token_value) == atomic_read(&acb->rq_map_token)) {
			atomic_set(&acb->rq_map_token, 16);
		}
		atomic_set(&acb->ante_token_value, atomic_read(&acb->rq_map_token));
		if (atomic_dec_and_test(&acb->rq_map_token)) {
			mod_timer(&acb->eternal_timer, jiffies + msecs_to_jiffies(6 * HZ));
			return;
		}
		writel(ARCMSR_INBOUND_MESG0_GET_CONFIG, &reg->inbound_msgaddr0);
		writel(ARCMSR_HBCMU_DRV2IOP_MESSAGE_CMD_DONE, &reg->inbound_doorbell);
		mod_timer(&acb->eternal_timer, jiffies + msecs_to_jiffies(6 * HZ));
	}
	return;
}

static void arcmsr_request_device_map(unsigned long pacb)
{
	struct AdapterControlBlock *acb = (struct AdapterControlBlock *)pacb;
	switch (acb->adapter_type) {
		case ACB_ADAPTER_TYPE_A: {
			arcmsr_request_hba_device_map(acb);
		}
		break;
		case ACB_ADAPTER_TYPE_B: {
			arcmsr_request_hbb_device_map(acb);
		}
		break;
		case ACB_ADAPTER_TYPE_C: {
			arcmsr_request_hbc_device_map(acb);
		}
	}
}

static void arcmsr_start_hba_bgrb(struct AdapterControlBlock *acb)
{
	struct MessageUnit_A __iomem *reg = acb->pmuA;
	acb->acb_flags |= ACB_F_MSG_START_BGRB;
	writel(ARCMSR_INBOUND_MESG0_START_BGRB, &reg->inbound_msgaddr0);
	if (!arcmsr_hba_wait_msgint_ready(acb)) {
		printk(KERN_NOTICE "arcmsr%d: wait 'start adapter background \
				rebulid' timeout \n", acb->host->host_no);
	}
}

static void arcmsr_start_hbb_bgrb(struct AdapterControlBlock *acb)
{
	struct MessageUnit_B *reg = acb->pmuB;
	acb->acb_flags |= ACB_F_MSG_START_BGRB;
	writel(ARCMSR_MESSAGE_START_BGRB, reg->drv2iop_doorbell);
	if (!arcmsr_hbb_wait_msgint_ready(acb)) {
		printk(KERN_NOTICE "arcmsr%d: wait 'start adapter background \
				rebulid' timeout \n",acb->host->host_no);
	}
}

static void arcmsr_start_hbc_bgrb(struct AdapterControlBlock *pACB)
{
	struct MessageUnit_C *phbcmu = (struct MessageUnit_C *)pACB->pmuC;
	pACB->acb_flags |= ACB_F_MSG_START_BGRB;
	writel(ARCMSR_INBOUND_MESG0_START_BGRB, &phbcmu->inbound_msgaddr0);
	writel(ARCMSR_HBCMU_DRV2IOP_MESSAGE_CMD_DONE, &phbcmu->inbound_doorbell);
	if (!arcmsr_hbc_wait_msgint_ready(pACB)) {
		printk(KERN_NOTICE "arcmsr%d: wait 'start adapter background \
				rebulid' timeout \n", pACB->host->host_no);
	}
	return;
}
static void arcmsr_start_adapter_bgrb(struct AdapterControlBlock *acb)
{
	switch (acb->adapter_type) {
	case ACB_ADAPTER_TYPE_A:
		arcmsr_start_hba_bgrb(acb);
		break;
	case ACB_ADAPTER_TYPE_B:
		arcmsr_start_hbb_bgrb(acb);
		break;
	case ACB_ADAPTER_TYPE_C:
		arcmsr_start_hbc_bgrb(acb);
	}
}

static void arcmsr_clear_doorbell_queue_buffer(struct AdapterControlBlock *acb)
{
	switch (acb->adapter_type) {
	case ACB_ADAPTER_TYPE_A: {
		struct MessageUnit_A __iomem *reg = acb->pmuA;
		uint32_t outbound_doorbell;
		/* empty doorbell Qbuffer if door bell ringed */
		outbound_doorbell = readl(&reg->outbound_doorbell);
		/*clear doorbell interrupt */
		writel(outbound_doorbell, &reg->outbound_doorbell);
		writel(ARCMSR_INBOUND_DRIVER_DATA_READ_OK, &reg->inbound_doorbell);
		}
		break;

	case ACB_ADAPTER_TYPE_B: {
		struct MessageUnit_B *reg = acb->pmuB;
		/*clear interrupt and message state*/
		writel(ARCMSR_MESSAGE_INT_CLEAR_PATTERN, reg->iop2drv_doorbell);
		writel(ARCMSR_DRV2IOP_DATA_READ_OK, reg->drv2iop_doorbell);
		/* let IOP know data has been read */
		}
		break;
	case ACB_ADAPTER_TYPE_C: {
		struct MessageUnit_C *reg = (struct MessageUnit_C *)acb->pmuC;
		uint32_t outbound_doorbell;
		/* empty doorbell Qbuffer if door bell ringed */
		outbound_doorbell = readl(&reg->outbound_doorbell);
		writel(outbound_doorbell, &reg->outbound_doorbell_clear);
		writel(ARCMSR_HBCMU_DRV2IOP_DATA_READ_OK, &reg->inbound_doorbell);
		}
	}
}

static void arcmsr_enable_eoi_mode(struct AdapterControlBlock *acb)
{
	switch (acb->adapter_type) {
	case ACB_ADAPTER_TYPE_A:
		return;
	case ACB_ADAPTER_TYPE_B:
		{
			struct MessageUnit_B *reg = acb->pmuB;
			writel(ARCMSR_MESSAGE_ACTIVE_EOI_MODE, reg->drv2iop_doorbell);
			if (!arcmsr_hbb_wait_msgint_ready(acb)) {
				printk(KERN_NOTICE "ARCMSR IOP enables EOI_MODE TIMEOUT");
				return;
			}
		}
		break;
	case ACB_ADAPTER_TYPE_C:
		return;
	}
	return;
}

static void arcmsr_hardware_reset(struct AdapterControlBlock *acb)
{
	uint8_t value[64];
	int i, count = 0;
	struct MessageUnit_A __iomem *pmuA = acb->pmuA;
	struct MessageUnit_C __iomem *pmuC = acb->pmuC;

	/* backup pci config data */
	printk(KERN_NOTICE "arcmsr%d: executing hw bus reset .....\n", acb->host->host_no);
	for (i = 0; i < 64; i++) {
		pci_read_config_byte(acb->pdev, i, &value[i]);
	}
	/* hardware reset signal */
	if ((acb->dev_id == 0x1680)) {
		writel(ARCMSR_ARC1680_BUS_RESET, &pmuA->reserved1[0]);
	} else if ((acb->dev_id == 0x1880)) {
		do {
			count++;
			writel(0xF, &pmuC->write_sequence);
			writel(0x4, &pmuC->write_sequence);
			writel(0xB, &pmuC->write_sequence);
			writel(0x2, &pmuC->write_sequence);
			writel(0x7, &pmuC->write_sequence);
			writel(0xD, &pmuC->write_sequence);
		} while (((readl(&pmuC->host_diagnostic) & ARCMSR_ARC1880_DiagWrite_ENABLE) == 0) && (count < 5));
		writel(ARCMSR_ARC1880_RESET_ADAPTER, &pmuC->host_diagnostic);
	} else {
		pci_write_config_byte(acb->pdev, 0x84, 0x20);
	}
	msleep(2000);
	/* write back pci config data */
	for (i = 0; i < 64; i++) {
		pci_write_config_byte(acb->pdev, i, value[i]);
	}
	msleep(1000);
	return;
}
static void arcmsr_iop_init(struct AdapterControlBlock *acb)
{
	uint32_t intmask_org;
	/* disable all outbound interrupt */
	intmask_org = arcmsr_disable_outbound_ints(acb);
	arcmsr_wait_firmware_ready(acb);
	arcmsr_iop_confirm(acb);
	/*start background rebuild*/
	arcmsr_start_adapter_bgrb(acb);
	/* empty doorbell Qbuffer if door bell ringed */
	arcmsr_clear_doorbell_queue_buffer(acb);
	arcmsr_enable_eoi_mode(acb);
	/* enable outbound Post Queue,outbound doorbell Interrupt */
	arcmsr_enable_outbound_ints(acb, intmask_org);
	acb->acb_flags |= ACB_F_IOP_INITED;
}

static uint8_t arcmsr_iop_reset(struct AdapterControlBlock *acb)
{
	struct CommandControlBlock *ccb;
	uint32_t intmask_org;
	uint8_t rtnval = 0x00;
	int i = 0;
	unsigned long flags;

	if (atomic_read(&acb->ccboutstandingcount) != 0) {
		/* disable all outbound interrupt */
		intmask_org = arcmsr_disable_outbound_ints(acb);
		/* talk to iop 331 outstanding command aborted */
		rtnval = arcmsr_abort_allcmd(acb);
		/* clear all outbound posted Q */
		arcmsr_done4abort_postqueue(acb);
		for (i = 0; i < ARCMSR_MAX_FREECCB_NUM; i++) {
			ccb = acb->pccb_pool[i];
			if (ccb->startdone == ARCMSR_CCB_START) {
				scsi_dma_unmap(ccb->pcmd);
				ccb->startdone = ARCMSR_CCB_DONE;
				ccb->ccb_flags = 0;
				spin_lock_irqsave(&acb->ccblist_lock, flags);
				list_add_tail(&ccb->list, &acb->ccb_free_list);
				spin_unlock_irqrestore(&acb->ccblist_lock, flags);
			}
		}
		atomic_set(&acb->ccboutstandingcount, 0);
		/* enable all outbound interrupt */
		arcmsr_enable_outbound_ints(acb, intmask_org);
		return rtnval;
	}
	return rtnval;
}

static int arcmsr_bus_reset(struct scsi_cmnd *cmd)
{
	struct AdapterControlBlock *acb;
	uint32_t intmask_org, outbound_doorbell;
	int retry_count = 0;
	int rtn = FAILED;
	acb = (struct AdapterControlBlock *) cmd->device->host->hostdata;
	printk(KERN_ERR "arcmsr: executing bus reset eh.....num_resets = %d, num_aborts = %d \n", acb->num_resets, acb->num_aborts);
	acb->num_resets++;

	switch(acb->adapter_type){
		case ACB_ADAPTER_TYPE_A:{
			if (acb->acb_flags & ACB_F_BUS_RESET){
				long timeout;
				printk(KERN_ERR "arcmsr: there is an  bus reset eh proceeding.......\n");
				timeout = wait_event_timeout(wait_q, (acb->acb_flags & ACB_F_BUS_RESET) == 0, 220*HZ);
				if (timeout) {
					return SUCCESS;
				}
			}
			acb->acb_flags |= ACB_F_BUS_RESET;
			if (!arcmsr_iop_reset(acb)) {
				struct MessageUnit_A __iomem *reg;
				reg = acb->pmuA;
				arcmsr_hardware_reset(acb);
				acb->acb_flags &= ~ACB_F_IOP_INITED;
sleep_again:
				ssleep(ARCMSR_SLEEPTIME);
				if ((readl(&reg->outbound_msgaddr1) & ARCMSR_OUTBOUND_MESG1_FIRMWARE_OK) == 0) {
					printk(KERN_ERR "arcmsr%d: waiting for hw bus reset return, retry=%d\n", acb->host->host_no, retry_count);
					if (retry_count > ARCMSR_RETRYCOUNT) {
						acb->fw_flag = FW_DEADLOCK;
						printk(KERN_ERR "arcmsr%d: waiting for hw bus reset return, RETRY TERMINATED!!\n", acb->host->host_no);
						return FAILED;
					}
					retry_count++;
					goto sleep_again;
				}
				acb->acb_flags |= ACB_F_IOP_INITED;
				/* disable all outbound interrupt */
				intmask_org = arcmsr_disable_outbound_ints(acb);
				arcmsr_get_firmware_spec(acb);
				arcmsr_start_adapter_bgrb(acb);
				/* clear Qbuffer if door bell ringed */
				outbound_doorbell = readl(&reg->outbound_doorbell);
				writel(outbound_doorbell, &reg->outbound_doorbell); /*clear interrupt */
   				writel(ARCMSR_INBOUND_DRIVER_DATA_READ_OK, &reg->inbound_doorbell);
				/* enable outbound Post Queue,outbound doorbell Interrupt */
				arcmsr_enable_outbound_ints(acb, intmask_org);
				atomic_set(&acb->rq_map_token, 16);
				atomic_set(&acb->ante_token_value, 16);
				acb->fw_flag = FW_NORMAL;
				mod_timer(&acb->eternal_timer, jiffies + msecs_to_jiffies(6 * HZ));
				acb->acb_flags &= ~ACB_F_BUS_RESET;
				rtn = SUCCESS;
				printk(KERN_ERR "arcmsr: scsi  bus reset eh returns with success\n");
			} else {
				acb->acb_flags &= ~ACB_F_BUS_RESET;
				atomic_set(&acb->rq_map_token, 16);
				atomic_set(&acb->ante_token_value, 16);
				acb->fw_flag = FW_NORMAL;
				mod_timer(&acb->eternal_timer, jiffies + msecs_to_jiffies(6*HZ));
				rtn = SUCCESS;
			}
			break;
		}
		case ACB_ADAPTER_TYPE_B:{
			acb->acb_flags |= ACB_F_BUS_RESET;
			if (!arcmsr_iop_reset(acb)) {
				acb->acb_flags &= ~ACB_F_BUS_RESET;
				rtn = FAILED;
			} else {
				acb->acb_flags &= ~ACB_F_BUS_RESET;
				atomic_set(&acb->rq_map_token, 16);
				atomic_set(&acb->ante_token_value, 16);
				acb->fw_flag = FW_NORMAL;
				mod_timer(&acb->eternal_timer, jiffies + msecs_to_jiffies(6 * HZ));
				rtn = SUCCESS;
			}
			break;
		}
		case ACB_ADAPTER_TYPE_C:{
			if (acb->acb_flags & ACB_F_BUS_RESET) {
				long timeout;
				printk(KERN_ERR "arcmsr: there is an bus reset eh proceeding.......\n");
				timeout = wait_event_timeout(wait_q, (acb->acb_flags & ACB_F_BUS_RESET) == 0, 220*HZ);
				if (timeout) {
					return SUCCESS;
				}
			}
			acb->acb_flags |= ACB_F_BUS_RESET;
			if (!arcmsr_iop_reset(acb)) {
				struct MessageUnit_C __iomem *reg;
				reg = acb->pmuC;
				arcmsr_hardware_reset(acb);
				acb->acb_flags &= ~ACB_F_IOP_INITED;
sleep:
				ssleep(ARCMSR_SLEEPTIME);
				if ((readl(&reg->host_diagnostic) & 0x04) != 0) {
					printk(KERN_ERR "arcmsr%d: waiting for hw bus reset return, retry=%d\n", acb->host->host_no, retry_count);
					if (retry_count > ARCMSR_RETRYCOUNT) {
						acb->fw_flag = FW_DEADLOCK;
						printk(KERN_ERR "arcmsr%d: waiting for hw bus reset return, RETRY TERMINATED!!\n", acb->host->host_no);
						return FAILED;
					}
					retry_count++;
					goto sleep;
				}
				acb->acb_flags |= ACB_F_IOP_INITED;
				/* disable all outbound interrupt */
				intmask_org = arcmsr_disable_outbound_ints(acb);
				arcmsr_get_firmware_spec(acb);
				arcmsr_start_adapter_bgrb(acb);
				/* clear Qbuffer if door bell ringed */
				outbound_doorbell = readl(&reg->outbound_doorbell);
				writel(outbound_doorbell, &reg->outbound_doorbell_clear); /*clear interrupt */
				writel(ARCMSR_HBCMU_DRV2IOP_DATA_READ_OK, &reg->inbound_doorbell);
				/* enable outbound Post Queue,outbound doorbell Interrupt */
				arcmsr_enable_outbound_ints(acb, intmask_org);
				atomic_set(&acb->rq_map_token, 16);
				atomic_set(&acb->ante_token_value, 16);
				acb->fw_flag = FW_NORMAL;
				mod_timer(&acb->eternal_timer, jiffies + msecs_to_jiffies(6 * HZ));
				acb->acb_flags &= ~ACB_F_BUS_RESET;
				rtn = SUCCESS;
				printk(KERN_ERR "arcmsr: scsi bus reset eh returns with success\n");
			} else {
				acb->acb_flags &= ~ACB_F_BUS_RESET;
				atomic_set(&acb->rq_map_token, 16);
				atomic_set(&acb->ante_token_value, 16);
				acb->fw_flag = FW_NORMAL;
				mod_timer(&acb->eternal_timer, jiffies + msecs_to_jiffies(6*HZ));
				rtn = SUCCESS;
			}
			break;
		}
	}
	return rtn;
}

static int arcmsr_abort_one_cmd(struct AdapterControlBlock *acb,
		struct CommandControlBlock *ccb)
{
	int rtn;
	rtn = arcmsr_polling_ccbdone(acb, ccb);
	return rtn;
}

static int arcmsr_abort(struct scsi_cmnd *cmd)
{
	struct AdapterControlBlock *acb =
		(struct AdapterControlBlock *)cmd->device->host->hostdata;
	int i = 0;
	int rtn = FAILED;
	printk(KERN_NOTICE
		"arcmsr%d: abort device command of scsi id = %d lun = %d \n",
		acb->host->host_no, cmd->device->id, cmd->device->lun);
	acb->acb_flags |= ACB_F_ABORT;
	acb->num_aborts++;
	/*
	************************************************
	** the all interrupt service routine is locked
	** we need to handle it as soon as possible and exit
	************************************************
	*/
	if (!atomic_read(&acb->ccboutstandingcount))
		return rtn;

	for (i = 0; i < ARCMSR_MAX_FREECCB_NUM; i++) {
		struct CommandControlBlock *ccb = acb->pccb_pool[i];
		if (ccb->startdone == ARCMSR_CCB_START && ccb->pcmd == cmd) {
			ccb->startdone = ARCMSR_CCB_ABORTED;
			rtn = arcmsr_abort_one_cmd(acb, ccb);
			break;
		}
	}
	acb->acb_flags &= ~ACB_F_ABORT;
	return rtn;
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
	case PCI_DEVICE_ID_ARECA_1200:
	case PCI_DEVICE_ID_ARECA_1202:
	case PCI_DEVICE_ID_ARECA_1210:
		raid6 = 0;
		/*FALLTHRU*/
	case PCI_DEVICE_ID_ARECA_1120:
	case PCI_DEVICE_ID_ARECA_1130:
	case PCI_DEVICE_ID_ARECA_1160:
	case PCI_DEVICE_ID_ARECA_1170:
	case PCI_DEVICE_ID_ARECA_1201:
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
	case PCI_DEVICE_ID_ARECA_1880:
		type = "SAS";
		break;
	default:
		type = "X-TYPE";
		break;
	}
	sprintf(buf, "Areca %s Host Adapter RAID Controller%s\n %s",
			type, raid6 ? "( RAID6 capable)" : "",
			ARCMSR_DRIVER_VERSION);
	return buf;
}
