/*
 *	Adaptec AAC series RAID controller driver
 *	(c) Copyright 2001 Red Hat Inc.
 *
 * based on the old aacraid driver that is..
 * Adaptec aacraid device driver for Linux.
 *
 * Copyright (c) 2000-2010 Adaptec, Inc.
 *               2010 PMC-Sierra, Inc. (aacraid@pmc-sierra.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Module Name:
 *  comminit.c
 *
 * Abstract: This supports the initialization of the host adapter commuication interface.
 *    This is a platform dependent module for the pci cyclone board.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <linux/mm.h>
#include <scsi/scsi_host.h>

#include "aacraid.h"

struct aac_common aac_config = {
	.irq_mod = 1
};

static inline int aac_is_msix_mode(struct aac_dev *dev)
{
	u32 status = 0;

	if (dev->pdev->device == PMC_DEVICE_S6 ||
		dev->pdev->device == PMC_DEVICE_S7 ||
		dev->pdev->device == PMC_DEVICE_S8) {
		status = src_readl(dev, MUnit.OMR);
	}
	return (status & AAC_INT_MODE_MSIX);
}

static inline void aac_change_to_intx(struct aac_dev *dev)
{
	aac_src_access_devreg(dev, AAC_DISABLE_MSIX);
	aac_src_access_devreg(dev, AAC_ENABLE_INTX);
}

static int aac_alloc_comm(struct aac_dev *dev, void **commaddr, unsigned long commsize, unsigned long commalign)
{
	unsigned char *base;
	unsigned long size, align;
	const unsigned long fibsize = dev->max_fib_size;
	const unsigned long printfbufsiz = 256;
	unsigned long host_rrq_size = 0;
	struct aac_init *init;
	dma_addr_t phys;
	unsigned long aac_max_hostphysmempages;

	if (dev->comm_interface == AAC_COMM_MESSAGE_TYPE1 ||
	    dev->comm_interface == AAC_COMM_MESSAGE_TYPE2)
		host_rrq_size = (dev->scsi_host_ptr->can_queue
			+ AAC_NUM_MGT_FIB) * sizeof(u32);
	size = fibsize + sizeof(struct aac_init) + commsize +
			commalign + printfbufsiz + host_rrq_size;
 
	base = pci_alloc_consistent(dev->pdev, size, &phys);

	if(base == NULL)
	{
		printk(KERN_ERR "aacraid: unable to create mapping.\n");
		return 0;
	}
	dev->comm_addr = (void *)base;
	dev->comm_phys = phys;
	dev->comm_size = size;
	
	if (dev->comm_interface == AAC_COMM_MESSAGE_TYPE1 ||
	    dev->comm_interface == AAC_COMM_MESSAGE_TYPE2) {
		dev->host_rrq = (u32 *)(base + fibsize);
		dev->host_rrq_pa = phys + fibsize;
		memset(dev->host_rrq, 0, host_rrq_size);
	}

	dev->init = (struct aac_init *)(base + fibsize + host_rrq_size);
	dev->init_pa = phys + fibsize + host_rrq_size;

	init = dev->init;

	init->InitStructRevision = cpu_to_le32(ADAPTER_INIT_STRUCT_REVISION);
	if (dev->max_fib_size != sizeof(struct hw_fib))
		init->InitStructRevision = cpu_to_le32(ADAPTER_INIT_STRUCT_REVISION_4);
	init->Sa_MSIXVectors = cpu_to_le32(Sa_MINIPORT_REVISION);
	init->fsrev = cpu_to_le32(dev->fsrev);

	/*
	 *	Adapter Fibs are the first thing allocated so that they
	 *	start page aligned
	 */
	dev->aif_base_va = (struct hw_fib *)base;
	
	init->AdapterFibsVirtualAddress = 0;
	init->AdapterFibsPhysicalAddress = cpu_to_le32((u32)phys);
	init->AdapterFibsSize = cpu_to_le32(fibsize);
	init->AdapterFibAlign = cpu_to_le32(sizeof(struct hw_fib));
	/*
	 * number of 4k pages of host physical memory. The aacraid fw needs
	 * this number to be less than 4gb worth of pages. New firmware doesn't
	 * have any issues with the mapping system, but older Firmware did, and
	 * had *troubles* dealing with the math overloading past 32 bits, thus
	 * we must limit this field.
	 */
	aac_max_hostphysmempages = dma_get_required_mask(&dev->pdev->dev) >> 12;
	if (aac_max_hostphysmempages < AAC_MAX_HOSTPHYSMEMPAGES)
		init->HostPhysMemPages = cpu_to_le32(aac_max_hostphysmempages);
	else
		init->HostPhysMemPages = cpu_to_le32(AAC_MAX_HOSTPHYSMEMPAGES);

	init->InitFlags = cpu_to_le32(INITFLAGS_DRIVER_USES_UTC_TIME |
		INITFLAGS_DRIVER_SUPPORTS_PM);
	init->MaxIoCommands = cpu_to_le32(dev->scsi_host_ptr->can_queue + AAC_NUM_MGT_FIB);
	init->MaxIoSize = cpu_to_le32(dev->scsi_host_ptr->max_sectors << 9);
	init->MaxFibSize = cpu_to_le32(dev->max_fib_size);
	init->MaxNumAif = cpu_to_le32(dev->max_num_aif);

	if (dev->comm_interface == AAC_COMM_MESSAGE) {
		init->InitFlags |= cpu_to_le32(INITFLAGS_NEW_COMM_SUPPORTED);
		dprintk((KERN_WARNING"aacraid: New Comm Interface enabled\n"));
	} else if (dev->comm_interface == AAC_COMM_MESSAGE_TYPE1) {
		init->InitStructRevision = cpu_to_le32(ADAPTER_INIT_STRUCT_REVISION_6);
		init->InitFlags |= cpu_to_le32(INITFLAGS_NEW_COMM_SUPPORTED |
			INITFLAGS_NEW_COMM_TYPE1_SUPPORTED | INITFLAGS_FAST_JBOD_SUPPORTED);
		init->HostRRQ_AddrHigh = cpu_to_le32((u32)((u64)dev->host_rrq_pa >> 32));
		init->HostRRQ_AddrLow = cpu_to_le32((u32)(dev->host_rrq_pa & 0xffffffff));
		dprintk((KERN_WARNING"aacraid: New Comm Interface type1 enabled\n"));
	} else if (dev->comm_interface == AAC_COMM_MESSAGE_TYPE2) {
		init->InitStructRevision = cpu_to_le32(ADAPTER_INIT_STRUCT_REVISION_7);
		init->InitFlags |= cpu_to_le32(INITFLAGS_NEW_COMM_SUPPORTED |
			INITFLAGS_NEW_COMM_TYPE2_SUPPORTED | INITFLAGS_FAST_JBOD_SUPPORTED);
		init->HostRRQ_AddrHigh = cpu_to_le32((u32)((u64)dev->host_rrq_pa >> 32));
		init->HostRRQ_AddrLow = cpu_to_le32((u32)(dev->host_rrq_pa & 0xffffffff));
		/* number of MSI-X */
		init->Sa_MSIXVectors = cpu_to_le32(dev->max_msix);
		dprintk((KERN_WARNING"aacraid: New Comm Interface type2 enabled\n"));
	}

	/*
	 * Increment the base address by the amount already used
	 */
	base = base + fibsize + host_rrq_size + sizeof(struct aac_init);
	phys = (dma_addr_t)((ulong)phys + fibsize + host_rrq_size +
		sizeof(struct aac_init));

	/*
	 *	Align the beginning of Headers to commalign
	 */
	align = (commalign - ((uintptr_t)(base) & (commalign - 1)));
	base = base + align;
	phys = phys + align;
	/*
	 *	Fill in addresses of the Comm Area Headers and Queues
	 */
	*commaddr = base;
	init->CommHeaderAddress = cpu_to_le32((u32)phys);
	/*
	 *	Increment the base address by the size of the CommArea
	 */
	base = base + commsize;
	phys = phys + commsize;
	/*
	 *	 Place the Printf buffer area after the Fast I/O comm area.
	 */
	dev->printfbuf = (void *)base;
	init->printfbuf = cpu_to_le32(phys);
	init->printfbufsiz = cpu_to_le32(printfbufsiz);
	memset(base, 0, printfbufsiz);
	return 1;
}
    
static void aac_queue_init(struct aac_dev * dev, struct aac_queue * q, u32 *mem, int qsize)
{
	atomic_set(&q->numpending, 0);
	q->dev = dev;
	init_waitqueue_head(&q->cmdready);
	INIT_LIST_HEAD(&q->cmdq);
	init_waitqueue_head(&q->qfull);
	spin_lock_init(&q->lockdata);
	q->lock = &q->lockdata;
	q->headers.producer = (__le32 *)mem;
	q->headers.consumer = (__le32 *)(mem+1);
	*(q->headers.producer) = cpu_to_le32(qsize);
	*(q->headers.consumer) = cpu_to_le32(qsize);
	q->entries = qsize;
}

/**
 *	aac_send_shutdown		-	shutdown an adapter
 *	@dev: Adapter to shutdown
 *
 *	This routine will send a VM_CloseAll (shutdown) request to the adapter.
 */

int aac_send_shutdown(struct aac_dev * dev)
{
	struct fib * fibctx;
	struct aac_close *cmd;
	int status;

	fibctx = aac_fib_alloc(dev);
	if (!fibctx)
		return -ENOMEM;
	aac_fib_init(fibctx);

	cmd = (struct aac_close *) fib_data(fibctx);

	cmd->command = cpu_to_le32(VM_CloseAll);
	cmd->cid = cpu_to_le32(0xfffffffe);

	status = aac_fib_send(ContainerCommand,
			  fibctx,
			  sizeof(struct aac_close),
			  FsaNormal,
			  -2 /* Timeout silently */, 1,
			  NULL, NULL);

	if (status >= 0)
		aac_fib_complete(fibctx);
	/* FIB should be freed only after getting the response from the F/W */
	if (status != -ERESTARTSYS)
		aac_fib_free(fibctx);
	dev->adapter_shutdown = 1;
	if ((dev->pdev->device == PMC_DEVICE_S7 ||
	     dev->pdev->device == PMC_DEVICE_S8 ||
	     dev->pdev->device == PMC_DEVICE_S9) &&
	     dev->msi_enabled)
		aac_src_access_devreg(dev, AAC_ENABLE_INTX);
	return status;
}

/**
 *	aac_comm_init	-	Initialise FSA data structures
 *	@dev:	Adapter to initialise
 *
 *	Initializes the data structures that are required for the FSA commuication
 *	interface to operate. 
 *	Returns
 *		1 - if we were able to init the commuication interface.
 *		0 - If there were errors initing. This is a fatal error.
 */
 
static int aac_comm_init(struct aac_dev * dev)
{
	unsigned long hdrsize = (sizeof(u32) * NUMBER_OF_COMM_QUEUES) * 2;
	unsigned long queuesize = sizeof(struct aac_entry) * TOTAL_QUEUE_ENTRIES;
	u32 *headers;
	struct aac_entry * queues;
	unsigned long size;
	struct aac_queue_block * comm = dev->queues;
	/*
	 *	Now allocate and initialize the zone structures used as our 
	 *	pool of FIB context records.  The size of the zone is based
	 *	on the system memory size.  We also initialize the mutex used
	 *	to protect the zone.
	 */
	spin_lock_init(&dev->fib_lock);

	/*
	 *	Allocate the physically contiguous space for the commuication
	 *	queue headers. 
	 */

	size = hdrsize + queuesize;

	if (!aac_alloc_comm(dev, (void * *)&headers, size, QUEUE_ALIGNMENT))
		return -ENOMEM;

	queues = (struct aac_entry *)(((ulong)headers) + hdrsize);

	/* Adapter to Host normal priority Command queue */ 
	comm->queue[HostNormCmdQueue].base = queues;
	aac_queue_init(dev, &comm->queue[HostNormCmdQueue], headers, HOST_NORM_CMD_ENTRIES);
	queues += HOST_NORM_CMD_ENTRIES;
	headers += 2;

	/* Adapter to Host high priority command queue */
	comm->queue[HostHighCmdQueue].base = queues;
	aac_queue_init(dev, &comm->queue[HostHighCmdQueue], headers, HOST_HIGH_CMD_ENTRIES);
    
	queues += HOST_HIGH_CMD_ENTRIES;
	headers +=2;

	/* Host to adapter normal priority command queue */
	comm->queue[AdapNormCmdQueue].base = queues;
	aac_queue_init(dev, &comm->queue[AdapNormCmdQueue], headers, ADAP_NORM_CMD_ENTRIES);
    
	queues += ADAP_NORM_CMD_ENTRIES;
	headers += 2;

	/* host to adapter high priority command queue */
	comm->queue[AdapHighCmdQueue].base = queues;
	aac_queue_init(dev, &comm->queue[AdapHighCmdQueue], headers, ADAP_HIGH_CMD_ENTRIES);
    
	queues += ADAP_HIGH_CMD_ENTRIES;
	headers += 2;

	/* adapter to host normal priority response queue */
	comm->queue[HostNormRespQueue].base = queues;
	aac_queue_init(dev, &comm->queue[HostNormRespQueue], headers, HOST_NORM_RESP_ENTRIES);
	queues += HOST_NORM_RESP_ENTRIES;
	headers += 2;

	/* adapter to host high priority response queue */
	comm->queue[HostHighRespQueue].base = queues;
	aac_queue_init(dev, &comm->queue[HostHighRespQueue], headers, HOST_HIGH_RESP_ENTRIES);
   
	queues += HOST_HIGH_RESP_ENTRIES;
	headers += 2;

	/* host to adapter normal priority response queue */
	comm->queue[AdapNormRespQueue].base = queues;
	aac_queue_init(dev, &comm->queue[AdapNormRespQueue], headers, ADAP_NORM_RESP_ENTRIES);

	queues += ADAP_NORM_RESP_ENTRIES;
	headers += 2;
	
	/* host to adapter high priority response queue */ 
	comm->queue[AdapHighRespQueue].base = queues;
	aac_queue_init(dev, &comm->queue[AdapHighRespQueue], headers, ADAP_HIGH_RESP_ENTRIES);

	comm->queue[AdapNormCmdQueue].lock = comm->queue[HostNormRespQueue].lock;
	comm->queue[AdapHighCmdQueue].lock = comm->queue[HostHighRespQueue].lock;
	comm->queue[AdapNormRespQueue].lock = comm->queue[HostNormCmdQueue].lock;
	comm->queue[AdapHighRespQueue].lock = comm->queue[HostHighCmdQueue].lock;

	return 0;
}

void aac_define_int_mode(struct aac_dev *dev)
{
	int i, msi_count, min_msix;

	msi_count = i = 0;
	/* max. vectors from GET_COMM_PREFERRED_SETTINGS */
	if (dev->max_msix == 0 ||
	    dev->pdev->device == PMC_DEVICE_S6 ||
	    dev->sync_mode) {
		dev->max_msix = 1;
		dev->vector_cap =
			dev->scsi_host_ptr->can_queue +
			AAC_NUM_MGT_FIB;
		return;
	}

	/* Don't bother allocating more MSI-X vectors than cpus */
	msi_count = min(dev->max_msix,
		(unsigned int)num_online_cpus());

	dev->max_msix = msi_count;

	if (msi_count > AAC_MAX_MSIX)
		msi_count = AAC_MAX_MSIX;

	for (i = 0; i < msi_count; i++)
		dev->msixentry[i].entry = i;

	if (msi_count > 1 &&
	    pci_find_capability(dev->pdev, PCI_CAP_ID_MSIX)) {
		min_msix = 2;
		i = pci_enable_msix_range(dev->pdev,
				    dev->msixentry,
				    min_msix,
				    msi_count);
		if (i > 0) {
			dev->msi_enabled = 1;
			msi_count = i;
		} else {
			dev->msi_enabled = 0;
			printk(KERN_ERR "%s%d: MSIX not supported!! Will try MSI 0x%x.\n",
					dev->name, dev->id, i);
		}
	}

	if (!dev->msi_enabled) {
		msi_count = 1;
		i = pci_enable_msi(dev->pdev);

		if (!i) {
			dev->msi_enabled = 1;
			dev->msi = 1;
		} else {
			printk(KERN_ERR "%s%d: MSI not supported!! Will try INTx 0x%x.\n",
					dev->name, dev->id, i);
		}
	}

	if (!dev->msi_enabled)
		dev->max_msix = msi_count = 1;
	else {
		if (dev->max_msix > msi_count)
			dev->max_msix = msi_count;
	}
	dev->vector_cap =
		(dev->scsi_host_ptr->can_queue + AAC_NUM_MGT_FIB) /
		msi_count;
}
struct aac_dev *aac_init_adapter(struct aac_dev *dev)
{
	u32 status[5];
	struct Scsi_Host * host = dev->scsi_host_ptr;
	extern int aac_sync_mode;

	/*
	 *	Check the preferred comm settings, defaults from template.
	 */
	dev->management_fib_count = 0;
	spin_lock_init(&dev->manage_lock);
	spin_lock_init(&dev->sync_lock);
	spin_lock_init(&dev->iq_lock);
	dev->max_fib_size = sizeof(struct hw_fib);
	dev->sg_tablesize = host->sg_tablesize = (dev->max_fib_size
		- sizeof(struct aac_fibhdr)
		- sizeof(struct aac_write) + sizeof(struct sgentry))
			/ sizeof(struct sgentry);
	dev->comm_interface = AAC_COMM_PRODUCER;
	dev->raw_io_interface = dev->raw_io_64 = 0;


	/*
	 * Enable INTX mode, if not done already Enabled
	 */
	if (aac_is_msix_mode(dev)) {
		aac_change_to_intx(dev);
		dev_info(&dev->pdev->dev, "Changed firmware to INTX mode");
	}

	if ((!aac_adapter_sync_cmd(dev, GET_ADAPTER_PROPERTIES,
		0, 0, 0, 0, 0, 0,
		status+0, status+1, status+2, status+3, NULL)) &&
	 		(status[0] == 0x00000001)) {
		dev->doorbell_mask = status[3];
		if (status[1] & le32_to_cpu(AAC_OPT_NEW_COMM_64))
			dev->raw_io_64 = 1;
		dev->sync_mode = aac_sync_mode;
		if (dev->a_ops.adapter_comm &&
			(status[1] & le32_to_cpu(AAC_OPT_NEW_COMM))) {
				dev->comm_interface = AAC_COMM_MESSAGE;
				dev->raw_io_interface = 1;
			if ((status[1] & le32_to_cpu(AAC_OPT_NEW_COMM_TYPE1))) {
				/* driver supports TYPE1 (Tupelo) */
				dev->comm_interface = AAC_COMM_MESSAGE_TYPE1;
			} else if ((status[1] & le32_to_cpu(AAC_OPT_NEW_COMM_TYPE2))) {
				/* driver supports TYPE2 (Denali) */
				dev->comm_interface = AAC_COMM_MESSAGE_TYPE2;
			} else if ((status[1] & le32_to_cpu(AAC_OPT_NEW_COMM_TYPE4)) ||
				  (status[1] & le32_to_cpu(AAC_OPT_NEW_COMM_TYPE3))) {
				/* driver doesn't TYPE3 and TYPE4 */
				/* switch to sync. mode */
				dev->comm_interface = AAC_COMM_MESSAGE_TYPE2;
				dev->sync_mode = 1;
			}
		}
		if ((dev->comm_interface == AAC_COMM_MESSAGE) &&
		    (status[2] > dev->base_size)) {
			aac_adapter_ioremap(dev, 0);
			dev->base_size = status[2];
			if (aac_adapter_ioremap(dev, status[2])) {
				/* remap failed, go back ... */
				dev->comm_interface = AAC_COMM_PRODUCER;
				if (aac_adapter_ioremap(dev, AAC_MIN_FOOTPRINT_SIZE)) {
					printk(KERN_WARNING
					  "aacraid: unable to map adapter.\n");
					return NULL;
				}
			}
		}
	}
	dev->max_msix = 0;
	dev->msi_enabled = 0;
	dev->adapter_shutdown = 0;
	if ((!aac_adapter_sync_cmd(dev, GET_COMM_PREFERRED_SETTINGS,
	  0, 0, 0, 0, 0, 0,
	  status+0, status+1, status+2, status+3, status+4))
	 && (status[0] == 0x00000001)) {
		/*
		 *	status[1] >> 16		maximum command size in KB
		 *	status[1] & 0xFFFF	maximum FIB size
		 *	status[2] >> 16		maximum SG elements to driver
		 *	status[2] & 0xFFFF	maximum SG elements from driver
		 *	status[3] & 0xFFFF	maximum number FIBs outstanding
		 */
		host->max_sectors = (status[1] >> 16) << 1;
		/* Multiple of 32 for PMC */
		dev->max_fib_size = status[1] & 0xFFE0;
		host->sg_tablesize = status[2] >> 16;
		dev->sg_tablesize = status[2] & 0xFFFF;
		if (dev->pdev->device == PMC_DEVICE_S7 ||
		    dev->pdev->device == PMC_DEVICE_S8 ||
		    dev->pdev->device == PMC_DEVICE_S9)
			host->can_queue = ((status[3] >> 16) ? (status[3] >> 16) :
				(status[3] & 0xFFFF)) - AAC_NUM_MGT_FIB;
		else
			host->can_queue = (status[3] & 0xFFFF) - AAC_NUM_MGT_FIB;
		dev->max_num_aif = status[4] & 0xFFFF;
		/*
		 *	NOTE:
		 *	All these overrides are based on a fixed internal
		 *	knowledge and understanding of existing adapters,
		 *	acbsize should be set with caution.
		 */
		if (acbsize == 512) {
			host->max_sectors = AAC_MAX_32BIT_SGBCOUNT;
			dev->max_fib_size = 512;
			dev->sg_tablesize = host->sg_tablesize
			  = (512 - sizeof(struct aac_fibhdr)
			    - sizeof(struct aac_write) + sizeof(struct sgentry))
			     / sizeof(struct sgentry);
			host->can_queue = AAC_NUM_IO_FIB;
		} else if (acbsize == 2048) {
			host->max_sectors = 512;
			dev->max_fib_size = 2048;
			host->sg_tablesize = 65;
			dev->sg_tablesize = 81;
			host->can_queue = 512 - AAC_NUM_MGT_FIB;
		} else if (acbsize == 4096) {
			host->max_sectors = 1024;
			dev->max_fib_size = 4096;
			host->sg_tablesize = 129;
			dev->sg_tablesize = 166;
			host->can_queue = 256 - AAC_NUM_MGT_FIB;
		} else if (acbsize == 8192) {
			host->max_sectors = 2048;
			dev->max_fib_size = 8192;
			host->sg_tablesize = 257;
			dev->sg_tablesize = 337;
			host->can_queue = 128 - AAC_NUM_MGT_FIB;
		} else if (acbsize > 0) {
			printk("Illegal acbsize=%d ignored\n", acbsize);
		}
	}
	{

		if (numacb > 0) {
			if (numacb < host->can_queue)
				host->can_queue = numacb;
			else
				printk("numacb=%d ignored\n", numacb);
		}
	}

	if (host->can_queue > AAC_NUM_IO_FIB)
		host->can_queue = AAC_NUM_IO_FIB;

	if (dev->pdev->device == PMC_DEVICE_S6 ||
	    dev->pdev->device == PMC_DEVICE_S7 ||
	    dev->pdev->device == PMC_DEVICE_S8 ||
	    dev->pdev->device == PMC_DEVICE_S9)
		aac_define_int_mode(dev);
	/*
	 *	Ok now init the communication subsystem
	 */

	dev->queues = kzalloc(sizeof(struct aac_queue_block), GFP_KERNEL);
	if (dev->queues == NULL) {
		printk(KERN_ERR "Error could not allocate comm region.\n");
		return NULL;
	}

	if (aac_comm_init(dev)<0){
		kfree(dev->queues);
		return NULL;
	}
	/*
	 *	Initialize the list of fibs
	 */
	if (aac_fib_setup(dev) < 0) {
		kfree(dev->queues);
		return NULL;
	}
		
	INIT_LIST_HEAD(&dev->fib_list);
	INIT_LIST_HEAD(&dev->sync_fib_list);

	return dev;
}

