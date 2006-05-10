/*
 *	Adaptec AAC series RAID controller driver
 *	(c) Copyright 2001 Red Hat Inc.	<alan@redhat.com>
 *
 * based on the old aacraid driver that is..
 * Adaptec aacraid device driver for Linux.
 *
 * Copyright (c) 2000 Adaptec, Inc. (aacraid@adaptec.com)
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
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/completion.h>
#include <linux/mm.h>
#include <scsi/scsi_host.h>
#include <asm/semaphore.h>

#include "aacraid.h"

struct aac_common aac_config = {
	.irq_mod = 1
};

static int aac_alloc_comm(struct aac_dev *dev, void **commaddr, unsigned long commsize, unsigned long commalign)
{
	unsigned char *base;
	unsigned long size, align;
	const unsigned long fibsize = 4096;
	const unsigned long printfbufsiz = 256;
	struct aac_init *init;
	dma_addr_t phys;

	size = fibsize + sizeof(struct aac_init) + commsize + commalign + printfbufsiz;

 
	base = pci_alloc_consistent(dev->pdev, size, &phys);

	if(base == NULL)
	{
		printk(KERN_ERR "aacraid: unable to create mapping.\n");
		return 0;
	}
	dev->comm_addr = (void *)base;
	dev->comm_phys = phys;
	dev->comm_size = size;
	
	dev->init = (struct aac_init *)(base + fibsize);
	dev->init_pa = phys + fibsize;

	init = dev->init;

	init->InitStructRevision = cpu_to_le32(ADAPTER_INIT_STRUCT_REVISION);
	if (dev->max_fib_size != sizeof(struct hw_fib))
		init->InitStructRevision = cpu_to_le32(ADAPTER_INIT_STRUCT_REVISION_4);
	init->MiniPortRevision = cpu_to_le32(Sa_MINIPORT_REVISION);
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
	 * this number to be less than 4gb worth of pages. num_physpages is in
	 * system page units. New firmware doesn't have any issues with the
	 * mapping system, but older Firmware did, and had *troubles* dealing
	 * with the math overloading past 32 bits, thus we must limit this
	 * field.
	 *
	 * This assumes the memory is mapped zero->n, which isnt
	 * always true on real computers. It also has some slight problems
	 * with the GART on x86-64. I've btw never tried DMA from PCI space
	 * on this platform but don't be suprised if its problematic.
	 */
#ifndef CONFIG_GART_IOMMU
	if ((num_physpages << (PAGE_SHIFT - 12)) <= AAC_MAX_HOSTPHYSMEMPAGES) {
		init->HostPhysMemPages = 
			cpu_to_le32(num_physpages << (PAGE_SHIFT-12));
	} else 
#endif	
	{
		init->HostPhysMemPages = cpu_to_le32(AAC_MAX_HOSTPHYSMEMPAGES);
	}

	init->InitFlags = 0;
	if (dev->new_comm_interface) {
		init->InitFlags = cpu_to_le32(INITFLAGS_NEW_COMM_SUPPORTED);
		dprintk((KERN_WARNING"aacraid: New Comm Interface enabled\n"));
	}
	init->MaxIoCommands = cpu_to_le32(dev->scsi_host_ptr->can_queue + AAC_NUM_MGT_FIB);
	init->MaxIoSize = cpu_to_le32(dev->scsi_host_ptr->max_sectors << 9);
	init->MaxFibSize = cpu_to_le32(dev->max_fib_size);

	/*
	 * Increment the base address by the amount already used
	 */
	base = base + fibsize + sizeof(struct aac_init);
	phys = (dma_addr_t)((ulong)phys + fibsize + sizeof(struct aac_init));
	/*
	 *	Align the beginning of Headers to commalign
	 */
	align = (commalign - ((unsigned long)(base) & (commalign - 1)));
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
	q->numpending = 0;
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
	cmd->cid = cpu_to_le32(0xffffffff);

	status = aac_fib_send(ContainerCommand,
			  fibctx,
			  sizeof(struct aac_close),
			  FsaNormal,
			  -2 /* Timeout silently */, 1,
			  NULL, NULL);

	if (status == 0)
		aac_fib_complete(fibctx);
	aac_fib_free(fibctx);
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
	 *	Allocate the physically contigous space for the commuication
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

struct aac_dev *aac_init_adapter(struct aac_dev *dev)
{
	u32 status[5];
	struct Scsi_Host * host = dev->scsi_host_ptr;

	/*
	 *	Check the preferred comm settings, defaults from template.
	 */
	dev->max_fib_size = sizeof(struct hw_fib);
	dev->sg_tablesize = host->sg_tablesize = (dev->max_fib_size
		- sizeof(struct aac_fibhdr)
		- sizeof(struct aac_write) + sizeof(struct sgentry))
			/ sizeof(struct sgentry);
	dev->new_comm_interface = 0;
	dev->raw_io_64 = 0;
	if ((!aac_adapter_sync_cmd(dev, GET_ADAPTER_PROPERTIES,
		0, 0, 0, 0, 0, 0, status+0, status+1, status+2, NULL, NULL)) &&
	 		(status[0] == 0x00000001)) {
		if (status[1] & AAC_OPT_NEW_COMM_64)
			dev->raw_io_64 = 1;
		if (status[1] & AAC_OPT_NEW_COMM)
			dev->new_comm_interface = dev->a_ops.adapter_send != 0;
		if (dev->new_comm_interface && (status[2] > dev->base_size)) {
			iounmap(dev->regs.sa);
			dev->base_size = status[2];
			dprintk((KERN_DEBUG "ioremap(%lx,%d)\n",
			  host->base, status[2]));
			dev->regs.sa = ioremap(host->base, status[2]);
			if (dev->regs.sa == NULL) {
				/* remap failed, go back ... */
				dev->new_comm_interface = 0;
				dev->regs.sa = ioremap(host->base, 
						AAC_MIN_FOOTPRINT_SIZE);
				if (dev->regs.sa == NULL) {	
					printk(KERN_WARNING
					  "aacraid: unable to map adapter.\n");
					return NULL;
				}
			}
		}
	}
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
		dev->max_fib_size = status[1] & 0xFFFF;
		host->sg_tablesize = status[2] >> 16;
		dev->sg_tablesize = status[2] & 0xFFFF;
		host->can_queue = (status[3] & 0xFFFF) - AAC_NUM_MGT_FIB;
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

	/*
	 *	Ok now init the communication subsystem
	 */

	dev->queues = (struct aac_queue_block *) kmalloc(sizeof(struct aac_queue_block), GFP_KERNEL);
	if (dev->queues == NULL) {
		printk(KERN_ERR "Error could not allocate comm region.\n");
		return NULL;
	}
	memset(dev->queues, 0, sizeof(struct aac_queue_block));

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

	return dev;
}

    
