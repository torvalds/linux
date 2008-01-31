/*
 *	Adaptec AAC series RAID controller driver
 *	(c) Copyright 2001 Red Hat Inc.	<alan@redhat.com>
 *
 * based on the old aacraid driver that is..
 * Adaptec aacraid device driver for Linux.
 *
 * Copyright (c) 2000-2007 Adaptec, Inc. (aacraid@adaptec.com)
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
 *  commsup.c
 *
 * Abstract: Contain all routines that are required for FSA host/adapter
 *    communication.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/completion.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/interrupt.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>
#include <asm/semaphore.h>

#include "aacraid.h"

/**
 *	fib_map_alloc		-	allocate the fib objects
 *	@dev: Adapter to allocate for
 *
 *	Allocate and map the shared PCI space for the FIB blocks used to
 *	talk to the Adaptec firmware.
 */

static int fib_map_alloc(struct aac_dev *dev)
{
	dprintk((KERN_INFO
	  "allocate hardware fibs pci_alloc_consistent(%p, %d * (%d + %d), %p)\n",
	  dev->pdev, dev->max_fib_size, dev->scsi_host_ptr->can_queue,
	  AAC_NUM_MGT_FIB, &dev->hw_fib_pa));
	if((dev->hw_fib_va = pci_alloc_consistent(dev->pdev, dev->max_fib_size
	  * (dev->scsi_host_ptr->can_queue + AAC_NUM_MGT_FIB),
	  &dev->hw_fib_pa))==NULL)
		return -ENOMEM;
	return 0;
}

/**
 *	aac_fib_map_free		-	free the fib objects
 *	@dev: Adapter to free
 *
 *	Free the PCI mappings and the memory allocated for FIB blocks
 *	on this adapter.
 */

void aac_fib_map_free(struct aac_dev *dev)
{
	pci_free_consistent(dev->pdev,
	  dev->max_fib_size * (dev->scsi_host_ptr->can_queue + AAC_NUM_MGT_FIB),
	  dev->hw_fib_va, dev->hw_fib_pa);
	dev->hw_fib_va = NULL;
	dev->hw_fib_pa = 0;
}

/**
 *	aac_fib_setup	-	setup the fibs
 *	@dev: Adapter to set up
 *
 *	Allocate the PCI space for the fibs, map it and then intialise the
 *	fib area, the unmapped fib data and also the free list
 */

int aac_fib_setup(struct aac_dev * dev)
{
	struct fib *fibptr;
	struct hw_fib *hw_fib;
	dma_addr_t hw_fib_pa;
	int i;

	while (((i = fib_map_alloc(dev)) == -ENOMEM)
	 && (dev->scsi_host_ptr->can_queue > (64 - AAC_NUM_MGT_FIB))) {
		dev->init->MaxIoCommands = cpu_to_le32((dev->scsi_host_ptr->can_queue + AAC_NUM_MGT_FIB) >> 1);
		dev->scsi_host_ptr->can_queue = le32_to_cpu(dev->init->MaxIoCommands) - AAC_NUM_MGT_FIB;
	}
	if (i<0)
		return -ENOMEM;

	hw_fib = dev->hw_fib_va;
	hw_fib_pa = dev->hw_fib_pa;
	memset(hw_fib, 0, dev->max_fib_size * (dev->scsi_host_ptr->can_queue + AAC_NUM_MGT_FIB));
	/*
	 *	Initialise the fibs
	 */
	for (i = 0, fibptr = &dev->fibs[i];
		i < (dev->scsi_host_ptr->can_queue + AAC_NUM_MGT_FIB);
		i++, fibptr++)
	{
		fibptr->dev = dev;
		fibptr->hw_fib_va = hw_fib;
		fibptr->data = (void *) fibptr->hw_fib_va->data;
		fibptr->next = fibptr+1;	/* Forward chain the fibs */
		init_MUTEX_LOCKED(&fibptr->event_wait);
		spin_lock_init(&fibptr->event_lock);
		hw_fib->header.XferState = cpu_to_le32(0xffffffff);
		hw_fib->header.SenderSize = cpu_to_le16(dev->max_fib_size);
		fibptr->hw_fib_pa = hw_fib_pa;
		hw_fib = (struct hw_fib *)((unsigned char *)hw_fib + dev->max_fib_size);
		hw_fib_pa = hw_fib_pa + dev->max_fib_size;
	}
	/*
	 *	Add the fib chain to the free list
	 */
	dev->fibs[dev->scsi_host_ptr->can_queue + AAC_NUM_MGT_FIB - 1].next = NULL;
	/*
	 *	Enable this to debug out of queue space
	 */
	dev->free_fib = &dev->fibs[0];
	return 0;
}

/**
 *	aac_fib_alloc	-	allocate a fib
 *	@dev: Adapter to allocate the fib for
 *
 *	Allocate a fib from the adapter fib pool. If the pool is empty we
 *	return NULL.
 */

struct fib *aac_fib_alloc(struct aac_dev *dev)
{
	struct fib * fibptr;
	unsigned long flags;
	spin_lock_irqsave(&dev->fib_lock, flags);
	fibptr = dev->free_fib;
	if(!fibptr){
		spin_unlock_irqrestore(&dev->fib_lock, flags);
		return fibptr;
	}
	dev->free_fib = fibptr->next;
	spin_unlock_irqrestore(&dev->fib_lock, flags);
	/*
	 *	Set the proper node type code and node byte size
	 */
	fibptr->type = FSAFS_NTC_FIB_CONTEXT;
	fibptr->size = sizeof(struct fib);
	/*
	 *	Null out fields that depend on being zero at the start of
	 *	each I/O
	 */
	fibptr->hw_fib_va->header.XferState = 0;
	fibptr->flags = 0;
	fibptr->callback = NULL;
	fibptr->callback_data = NULL;

	return fibptr;
}

/**
 *	aac_fib_free	-	free a fib
 *	@fibptr: fib to free up
 *
 *	Frees up a fib and places it on the appropriate queue
 */

void aac_fib_free(struct fib *fibptr)
{
	unsigned long flags;

	spin_lock_irqsave(&fibptr->dev->fib_lock, flags);
	if (unlikely(fibptr->flags & FIB_CONTEXT_FLAG_TIMED_OUT))
		aac_config.fib_timeouts++;
	if (fibptr->hw_fib_va->header.XferState != 0) {
		printk(KERN_WARNING "aac_fib_free, XferState != 0, fibptr = 0x%p, XferState = 0x%x\n",
			 (void*)fibptr,
			 le32_to_cpu(fibptr->hw_fib_va->header.XferState));
	}
	fibptr->next = fibptr->dev->free_fib;
	fibptr->dev->free_fib = fibptr;
	spin_unlock_irqrestore(&fibptr->dev->fib_lock, flags);
}

/**
 *	aac_fib_init	-	initialise a fib
 *	@fibptr: The fib to initialize
 *
 *	Set up the generic fib fields ready for use
 */

void aac_fib_init(struct fib *fibptr)
{
	struct hw_fib *hw_fib = fibptr->hw_fib_va;

	hw_fib->header.StructType = FIB_MAGIC;
	hw_fib->header.Size = cpu_to_le16(fibptr->dev->max_fib_size);
	hw_fib->header.XferState = cpu_to_le32(HostOwned | FibInitialized | FibEmpty | FastResponseCapable);
	hw_fib->header.SenderFibAddress = 0; /* Filled in later if needed */
	hw_fib->header.ReceiverFibAddress = cpu_to_le32(fibptr->hw_fib_pa);
	hw_fib->header.SenderSize = cpu_to_le16(fibptr->dev->max_fib_size);
}

/**
 *	fib_deallocate		-	deallocate a fib
 *	@fibptr: fib to deallocate
 *
 *	Will deallocate and return to the free pool the FIB pointed to by the
 *	caller.
 */

static void fib_dealloc(struct fib * fibptr)
{
	struct hw_fib *hw_fib = fibptr->hw_fib_va;
	BUG_ON(hw_fib->header.StructType != FIB_MAGIC);
	hw_fib->header.XferState = 0;
}

/*
 *	Commuication primitives define and support the queuing method we use to
 *	support host to adapter commuication. All queue accesses happen through
 *	these routines and are the only routines which have a knowledge of the
 *	 how these queues are implemented.
 */

/**
 *	aac_get_entry		-	get a queue entry
 *	@dev: Adapter
 *	@qid: Queue Number
 *	@entry: Entry return
 *	@index: Index return
 *	@nonotify: notification control
 *
 *	With a priority the routine returns a queue entry if the queue has free entries. If the queue
 *	is full(no free entries) than no entry is returned and the function returns 0 otherwise 1 is
 *	returned.
 */

static int aac_get_entry (struct aac_dev * dev, u32 qid, struct aac_entry **entry, u32 * index, unsigned long *nonotify)
{
	struct aac_queue * q;
	unsigned long idx;

	/*
	 *	All of the queues wrap when they reach the end, so we check
	 *	to see if they have reached the end and if they have we just
	 *	set the index back to zero. This is a wrap. You could or off
	 *	the high bits in all updates but this is a bit faster I think.
	 */

	q = &dev->queues->queue[qid];

	idx = *index = le32_to_cpu(*(q->headers.producer));
	/* Interrupt Moderation, only interrupt for first two entries */
	if (idx != le32_to_cpu(*(q->headers.consumer))) {
		if (--idx == 0) {
			if (qid == AdapNormCmdQueue)
				idx = ADAP_NORM_CMD_ENTRIES;
			else
				idx = ADAP_NORM_RESP_ENTRIES;
		}
		if (idx != le32_to_cpu(*(q->headers.consumer)))
			*nonotify = 1;
	}

	if (qid == AdapNormCmdQueue) {
		if (*index >= ADAP_NORM_CMD_ENTRIES)
			*index = 0; /* Wrap to front of the Producer Queue. */
	} else {
		if (*index >= ADAP_NORM_RESP_ENTRIES)
			*index = 0; /* Wrap to front of the Producer Queue. */
	}

	/* Queue is full */
	if ((*index + 1) == le32_to_cpu(*(q->headers.consumer))) {
		printk(KERN_WARNING "Queue %d full, %u outstanding.\n",
				qid, q->numpending);
		return 0;
	} else {
		*entry = q->base + *index;
		return 1;
	}
}

/**
 *	aac_queue_get		-	get the next free QE
 *	@dev: Adapter
 *	@index: Returned index
 *	@priority: Priority of fib
 *	@fib: Fib to associate with the queue entry
 *	@wait: Wait if queue full
 *	@fibptr: Driver fib object to go with fib
 *	@nonotify: Don't notify the adapter
 *
 *	Gets the next free QE off the requested priorty adapter command
 *	queue and associates the Fib with the QE. The QE represented by
 *	index is ready to insert on the queue when this routine returns
 *	success.
 */

int aac_queue_get(struct aac_dev * dev, u32 * index, u32 qid, struct hw_fib * hw_fib, int wait, struct fib * fibptr, unsigned long *nonotify)
{
	struct aac_entry * entry = NULL;
	int map = 0;

	if (qid == AdapNormCmdQueue) {
		/*  if no entries wait for some if caller wants to */
		while (!aac_get_entry(dev, qid, &entry, index, nonotify)) {
			printk(KERN_ERR "GetEntries failed\n");
		}
		/*
		 *	Setup queue entry with a command, status and fib mapped
		 */
		entry->size = cpu_to_le32(le16_to_cpu(hw_fib->header.Size));
		map = 1;
	} else {
		while (!aac_get_entry(dev, qid, &entry, index, nonotify)) {
			/* if no entries wait for some if caller wants to */
		}
		/*
		 *	Setup queue entry with command, status and fib mapped
		 */
		entry->size = cpu_to_le32(le16_to_cpu(hw_fib->header.Size));
		entry->addr = hw_fib->header.SenderFibAddress;
			/* Restore adapters pointer to the FIB */
		hw_fib->header.ReceiverFibAddress = hw_fib->header.SenderFibAddress;	/* Let the adapter now where to find its data */
		map = 0;
	}
	/*
	 *	If MapFib is true than we need to map the Fib and put pointers
	 *	in the queue entry.
	 */
	if (map)
		entry->addr = cpu_to_le32(fibptr->hw_fib_pa);
	return 0;
}

/*
 *	Define the highest level of host to adapter communication routines.
 *	These routines will support host to adapter FS commuication. These
 *	routines have no knowledge of the commuication method used. This level
 *	sends and receives FIBs. This level has no knowledge of how these FIBs
 *	get passed back and forth.
 */

/**
 *	aac_fib_send	-	send a fib to the adapter
 *	@command: Command to send
 *	@fibptr: The fib
 *	@size: Size of fib data area
 *	@priority: Priority of Fib
 *	@wait: Async/sync select
 *	@reply: True if a reply is wanted
 *	@callback: Called with reply
 *	@callback_data: Passed to callback
 *
 *	Sends the requested FIB to the adapter and optionally will wait for a
 *	response FIB. If the caller does not wish to wait for a response than
 *	an event to wait on must be supplied. This event will be set when a
 *	response FIB is received from the adapter.
 */

int aac_fib_send(u16 command, struct fib *fibptr, unsigned long size,
		int priority, int wait, int reply, fib_callback callback,
		void *callback_data)
{
	struct aac_dev * dev = fibptr->dev;
	struct hw_fib * hw_fib = fibptr->hw_fib_va;
	unsigned long flags = 0;
	unsigned long qflags;

	if (!(hw_fib->header.XferState & cpu_to_le32(HostOwned)))
		return -EBUSY;
	/*
	 *	There are 5 cases with the wait and reponse requested flags.
	 *	The only invalid cases are if the caller requests to wait and
	 *	does not request a response and if the caller does not want a
	 *	response and the Fib is not allocated from pool. If a response
	 *	is not requesed the Fib will just be deallocaed by the DPC
	 *	routine when the response comes back from the adapter. No
	 *	further processing will be done besides deleting the Fib. We
	 *	will have a debug mode where the adapter can notify the host
	 *	it had a problem and the host can log that fact.
	 */
	fibptr->flags = 0;
	if (wait && !reply) {
		return -EINVAL;
	} else if (!wait && reply) {
		hw_fib->header.XferState |= cpu_to_le32(Async | ResponseExpected);
		FIB_COUNTER_INCREMENT(aac_config.AsyncSent);
	} else if (!wait && !reply) {
		hw_fib->header.XferState |= cpu_to_le32(NoResponseExpected);
		FIB_COUNTER_INCREMENT(aac_config.NoResponseSent);
	} else if (wait && reply) {
		hw_fib->header.XferState |= cpu_to_le32(ResponseExpected);
		FIB_COUNTER_INCREMENT(aac_config.NormalSent);
	}
	/*
	 *	Map the fib into 32bits by using the fib number
	 */

	hw_fib->header.SenderFibAddress = cpu_to_le32(((u32)(fibptr - dev->fibs)) << 2);
	hw_fib->header.SenderData = (u32)(fibptr - dev->fibs);
	/*
	 *	Set FIB state to indicate where it came from and if we want a
	 *	response from the adapter. Also load the command from the
	 *	caller.
	 *
	 *	Map the hw fib pointer as a 32bit value
	 */
	hw_fib->header.Command = cpu_to_le16(command);
	hw_fib->header.XferState |= cpu_to_le32(SentFromHost);
	fibptr->hw_fib_va->header.Flags = 0;	/* 0 the flags field - internal only*/
	/*
	 *	Set the size of the Fib we want to send to the adapter
	 */
	hw_fib->header.Size = cpu_to_le16(sizeof(struct aac_fibhdr) + size);
	if (le16_to_cpu(hw_fib->header.Size) > le16_to_cpu(hw_fib->header.SenderSize)) {
		return -EMSGSIZE;
	}
	/*
	 *	Get a queue entry connect the FIB to it and send an notify
	 *	the adapter a command is ready.
	 */
	hw_fib->header.XferState |= cpu_to_le32(NormalPriority);

	/*
	 *	Fill in the Callback and CallbackContext if we are not
	 *	going to wait.
	 */
	if (!wait) {
		fibptr->callback = callback;
		fibptr->callback_data = callback_data;
		fibptr->flags = FIB_CONTEXT_FLAG;
	}

	fibptr->done = 0;

	FIB_COUNTER_INCREMENT(aac_config.FibsSent);

	dprintk((KERN_DEBUG "Fib contents:.\n"));
	dprintk((KERN_DEBUG "  Command =               %d.\n", le32_to_cpu(hw_fib->header.Command)));
	dprintk((KERN_DEBUG "  SubCommand =            %d.\n", le32_to_cpu(((struct aac_query_mount *)fib_data(fibptr))->command)));
	dprintk((KERN_DEBUG "  XferState  =            %x.\n", le32_to_cpu(hw_fib->header.XferState)));
	dprintk((KERN_DEBUG "  hw_fib va being sent=%p\n",fibptr->hw_fib_va));
	dprintk((KERN_DEBUG "  hw_fib pa being sent=%lx\n",(ulong)fibptr->hw_fib_pa));
	dprintk((KERN_DEBUG "  fib being sent=%p\n",fibptr));

	if (!dev->queues)
		return -EBUSY;

	if(wait)
		spin_lock_irqsave(&fibptr->event_lock, flags);
	aac_adapter_deliver(fibptr);

	/*
	 *	If the caller wanted us to wait for response wait now.
	 */

	if (wait) {
		spin_unlock_irqrestore(&fibptr->event_lock, flags);
		/* Only set for first known interruptable command */
		if (wait < 0) {
			/*
			 * *VERY* Dangerous to time out a command, the
			 * assumption is made that we have no hope of
			 * functioning because an interrupt routing or other
			 * hardware failure has occurred.
			 */
			unsigned long count = 36000000L; /* 3 minutes */
			while (down_trylock(&fibptr->event_wait)) {
				int blink;
				if (--count == 0) {
					struct aac_queue * q = &dev->queues->queue[AdapNormCmdQueue];
					spin_lock_irqsave(q->lock, qflags);
					q->numpending--;
					spin_unlock_irqrestore(q->lock, qflags);
					if (wait == -1) {
	        				printk(KERN_ERR "aacraid: aac_fib_send: first asynchronous command timed out.\n"
						  "Usually a result of a PCI interrupt routing problem;\n"
						  "update mother board BIOS or consider utilizing one of\n"
						  "the SAFE mode kernel options (acpi, apic etc)\n");
					}
					return -ETIMEDOUT;
				}
				if ((blink = aac_adapter_check_health(dev)) > 0) {
					if (wait == -1) {
	        				printk(KERN_ERR "aacraid: aac_fib_send: adapter blinkLED 0x%x.\n"
						  "Usually a result of a serious unrecoverable hardware problem\n",
						  blink);
					}
					return -EFAULT;
				}
				udelay(5);
			}
		} else
			(void)down_interruptible(&fibptr->event_wait);
		spin_lock_irqsave(&fibptr->event_lock, flags);
		if (fibptr->done == 0) {
			fibptr->done = 2; /* Tell interrupt we aborted */
			spin_unlock_irqrestore(&fibptr->event_lock, flags);
			return -EINTR;
		}
		spin_unlock_irqrestore(&fibptr->event_lock, flags);
		BUG_ON(fibptr->done == 0);

		if(unlikely(fibptr->flags & FIB_CONTEXT_FLAG_TIMED_OUT))
			return -ETIMEDOUT;
		return 0;
	}
	/*
	 *	If the user does not want a response than return success otherwise
	 *	return pending
	 */
	if (reply)
		return -EINPROGRESS;
	else
		return 0;
}

/**
 *	aac_consumer_get	-	get the top of the queue
 *	@dev: Adapter
 *	@q: Queue
 *	@entry: Return entry
 *
 *	Will return a pointer to the entry on the top of the queue requested that
 *	we are a consumer of, and return the address of the queue entry. It does
 *	not change the state of the queue.
 */

int aac_consumer_get(struct aac_dev * dev, struct aac_queue * q, struct aac_entry **entry)
{
	u32 index;
	int status;
	if (le32_to_cpu(*q->headers.producer) == le32_to_cpu(*q->headers.consumer)) {
		status = 0;
	} else {
		/*
		 *	The consumer index must be wrapped if we have reached
		 *	the end of the queue, else we just use the entry
		 *	pointed to by the header index
		 */
		if (le32_to_cpu(*q->headers.consumer) >= q->entries)
			index = 0;
		else
			index = le32_to_cpu(*q->headers.consumer);
		*entry = q->base + index;
		status = 1;
	}
	return(status);
}

/**
 *	aac_consumer_free	-	free consumer entry
 *	@dev: Adapter
 *	@q: Queue
 *	@qid: Queue ident
 *
 *	Frees up the current top of the queue we are a consumer of. If the
 *	queue was full notify the producer that the queue is no longer full.
 */

void aac_consumer_free(struct aac_dev * dev, struct aac_queue *q, u32 qid)
{
	int wasfull = 0;
	u32 notify;

	if ((le32_to_cpu(*q->headers.producer)+1) == le32_to_cpu(*q->headers.consumer))
		wasfull = 1;

	if (le32_to_cpu(*q->headers.consumer) >= q->entries)
		*q->headers.consumer = cpu_to_le32(1);
	else
		*q->headers.consumer = cpu_to_le32(le32_to_cpu(*q->headers.consumer)+1);

	if (wasfull) {
		switch (qid) {

		case HostNormCmdQueue:
			notify = HostNormCmdNotFull;
			break;
		case HostNormRespQueue:
			notify = HostNormRespNotFull;
			break;
		default:
			BUG();
			return;
		}
		aac_adapter_notify(dev, notify);
	}
}

/**
 *	aac_fib_adapter_complete	-	complete adapter issued fib
 *	@fibptr: fib to complete
 *	@size: size of fib
 *
 *	Will do all necessary work to complete a FIB that was sent from
 *	the adapter.
 */

int aac_fib_adapter_complete(struct fib *fibptr, unsigned short size)
{
	struct hw_fib * hw_fib = fibptr->hw_fib_va;
	struct aac_dev * dev = fibptr->dev;
	struct aac_queue * q;
	unsigned long nointr = 0;
	unsigned long qflags;

	if (hw_fib->header.XferState == 0) {
		if (dev->comm_interface == AAC_COMM_MESSAGE)
			kfree (hw_fib);
		return 0;
	}
	/*
	 *	If we plan to do anything check the structure type first.
	 */
	if (hw_fib->header.StructType != FIB_MAGIC) {
		if (dev->comm_interface == AAC_COMM_MESSAGE)
			kfree (hw_fib);
		return -EINVAL;
	}
	/*
	 *	This block handles the case where the adapter had sent us a
	 *	command and we have finished processing the command. We
	 *	call completeFib when we are done processing the command
	 *	and want to send a response back to the adapter. This will
	 *	send the completed cdb to the adapter.
	 */
	if (hw_fib->header.XferState & cpu_to_le32(SentFromAdapter)) {
		if (dev->comm_interface == AAC_COMM_MESSAGE) {
			kfree (hw_fib);
		} else {
			u32 index;
			hw_fib->header.XferState |= cpu_to_le32(HostProcessed);
			if (size) {
				size += sizeof(struct aac_fibhdr);
				if (size > le16_to_cpu(hw_fib->header.SenderSize))
					return -EMSGSIZE;
				hw_fib->header.Size = cpu_to_le16(size);
			}
			q = &dev->queues->queue[AdapNormRespQueue];
			spin_lock_irqsave(q->lock, qflags);
			aac_queue_get(dev, &index, AdapNormRespQueue, hw_fib, 1, NULL, &nointr);
			*(q->headers.producer) = cpu_to_le32(index + 1);
			spin_unlock_irqrestore(q->lock, qflags);
			if (!(nointr & (int)aac_config.irq_mod))
				aac_adapter_notify(dev, AdapNormRespQueue);
		}
	} else {
		printk(KERN_WARNING "aac_fib_adapter_complete: "
			"Unknown xferstate detected.\n");
		BUG();
	}
	return 0;
}

/**
 *	aac_fib_complete	-	fib completion handler
 *	@fib: FIB to complete
 *
 *	Will do all necessary work to complete a FIB.
 */

int aac_fib_complete(struct fib *fibptr)
{
	struct hw_fib * hw_fib = fibptr->hw_fib_va;

	/*
	 *	Check for a fib which has already been completed
	 */

	if (hw_fib->header.XferState == 0)
		return 0;
	/*
	 *	If we plan to do anything check the structure type first.
	 */

	if (hw_fib->header.StructType != FIB_MAGIC)
		return -EINVAL;
	/*
	 *	This block completes a cdb which orginated on the host and we
	 *	just need to deallocate the cdb or reinit it. At this point the
	 *	command is complete that we had sent to the adapter and this
	 *	cdb could be reused.
	 */
	if((hw_fib->header.XferState & cpu_to_le32(SentFromHost)) &&
		(hw_fib->header.XferState & cpu_to_le32(AdapterProcessed)))
	{
		fib_dealloc(fibptr);
	}
	else if(hw_fib->header.XferState & cpu_to_le32(SentFromHost))
	{
		/*
		 *	This handles the case when the host has aborted the I/O
		 *	to the adapter because the adapter is not responding
		 */
		fib_dealloc(fibptr);
	} else if(hw_fib->header.XferState & cpu_to_le32(HostOwned)) {
		fib_dealloc(fibptr);
	} else {
		BUG();
	}
	return 0;
}

/**
 *	aac_printf	-	handle printf from firmware
 *	@dev: Adapter
 *	@val: Message info
 *
 *	Print a message passed to us by the controller firmware on the
 *	Adaptec board
 */

void aac_printf(struct aac_dev *dev, u32 val)
{
	char *cp = dev->printfbuf;
	if (dev->printf_enabled)
	{
		int length = val & 0xffff;
		int level = (val >> 16) & 0xffff;

		/*
		 *	The size of the printfbuf is set in port.c
		 *	There is no variable or define for it
		 */
		if (length > 255)
			length = 255;
		if (cp[length] != 0)
			cp[length] = 0;
		if (level == LOG_AAC_HIGH_ERROR)
			printk(KERN_WARNING "%s:%s", dev->name, cp);
		else
			printk(KERN_INFO "%s:%s", dev->name, cp);
	}
	memset(cp, 0, 256);
}


/**
 *	aac_handle_aif		-	Handle a message from the firmware
 *	@dev: Which adapter this fib is from
 *	@fibptr: Pointer to fibptr from adapter
 *
 *	This routine handles a driver notify fib from the adapter and
 *	dispatches it to the appropriate routine for handling.
 */

#define AIF_SNIFF_TIMEOUT	(30*HZ)
static void aac_handle_aif(struct aac_dev * dev, struct fib * fibptr)
{
	struct hw_fib * hw_fib = fibptr->hw_fib_va;
	struct aac_aifcmd * aifcmd = (struct aac_aifcmd *)hw_fib->data;
	u32 channel, id, lun, container;
	struct scsi_device *device;
	enum {
		NOTHING,
		DELETE,
		ADD,
		CHANGE
	} device_config_needed = NOTHING;

	/* Sniff for container changes */

	if (!dev || !dev->fsa_dev)
		return;
	container = channel = id = lun = (u32)-1;

	/*
	 *	We have set this up to try and minimize the number of
	 * re-configures that take place. As a result of this when
	 * certain AIF's come in we will set a flag waiting for another
	 * type of AIF before setting the re-config flag.
	 */
	switch (le32_to_cpu(aifcmd->command)) {
	case AifCmdDriverNotify:
		switch (le32_to_cpu(((__le32 *)aifcmd->data)[0])) {
		/*
		 *	Morph or Expand complete
		 */
		case AifDenMorphComplete:
		case AifDenVolumeExtendComplete:
			container = le32_to_cpu(((__le32 *)aifcmd->data)[1]);
			if (container >= dev->maximum_num_containers)
				break;

			/*
			 *	Find the scsi_device associated with the SCSI
			 * address. Make sure we have the right array, and if
			 * so set the flag to initiate a new re-config once we
			 * see an AifEnConfigChange AIF come through.
			 */

			if ((dev != NULL) && (dev->scsi_host_ptr != NULL)) {
				device = scsi_device_lookup(dev->scsi_host_ptr,
					CONTAINER_TO_CHANNEL(container),
					CONTAINER_TO_ID(container),
					CONTAINER_TO_LUN(container));
				if (device) {
					dev->fsa_dev[container].config_needed = CHANGE;
					dev->fsa_dev[container].config_waiting_on = AifEnConfigChange;
					dev->fsa_dev[container].config_waiting_stamp = jiffies;
					scsi_device_put(device);
				}
			}
		}

		/*
		 *	If we are waiting on something and this happens to be
		 * that thing then set the re-configure flag.
		 */
		if (container != (u32)-1) {
			if (container >= dev->maximum_num_containers)
				break;
			if ((dev->fsa_dev[container].config_waiting_on ==
			    le32_to_cpu(*(__le32 *)aifcmd->data)) &&
			 time_before(jiffies, dev->fsa_dev[container].config_waiting_stamp + AIF_SNIFF_TIMEOUT))
				dev->fsa_dev[container].config_waiting_on = 0;
		} else for (container = 0;
		    container < dev->maximum_num_containers; ++container) {
			if ((dev->fsa_dev[container].config_waiting_on ==
			    le32_to_cpu(*(__le32 *)aifcmd->data)) &&
			 time_before(jiffies, dev->fsa_dev[container].config_waiting_stamp + AIF_SNIFF_TIMEOUT))
				dev->fsa_dev[container].config_waiting_on = 0;
		}
		break;

	case AifCmdEventNotify:
		switch (le32_to_cpu(((__le32 *)aifcmd->data)[0])) {
		case AifEnBatteryEvent:
			dev->cache_protected =
				(((__le32 *)aifcmd->data)[1] == cpu_to_le32(3));
			break;
		/*
		 *	Add an Array.
		 */
		case AifEnAddContainer:
			container = le32_to_cpu(((__le32 *)aifcmd->data)[1]);
			if (container >= dev->maximum_num_containers)
				break;
			dev->fsa_dev[container].config_needed = ADD;
			dev->fsa_dev[container].config_waiting_on =
				AifEnConfigChange;
			dev->fsa_dev[container].config_waiting_stamp = jiffies;
			break;

		/*
		 *	Delete an Array.
		 */
		case AifEnDeleteContainer:
			container = le32_to_cpu(((__le32 *)aifcmd->data)[1]);
			if (container >= dev->maximum_num_containers)
				break;
			dev->fsa_dev[container].config_needed = DELETE;
			dev->fsa_dev[container].config_waiting_on =
				AifEnConfigChange;
			dev->fsa_dev[container].config_waiting_stamp = jiffies;
			break;

		/*
		 *	Container change detected. If we currently are not
		 * waiting on something else, setup to wait on a Config Change.
		 */
		case AifEnContainerChange:
			container = le32_to_cpu(((__le32 *)aifcmd->data)[1]);
			if (container >= dev->maximum_num_containers)
				break;
			if (dev->fsa_dev[container].config_waiting_on &&
			 time_before(jiffies, dev->fsa_dev[container].config_waiting_stamp + AIF_SNIFF_TIMEOUT))
				break;
			dev->fsa_dev[container].config_needed = CHANGE;
			dev->fsa_dev[container].config_waiting_on =
				AifEnConfigChange;
			dev->fsa_dev[container].config_waiting_stamp = jiffies;
			break;

		case AifEnConfigChange:
			break;

		case AifEnAddJBOD:
		case AifEnDeleteJBOD:
			container = le32_to_cpu(((__le32 *)aifcmd->data)[1]);
			if ((container >> 28))
				break;
			channel = (container >> 24) & 0xF;
			if (channel >= dev->maximum_num_channels)
				break;
			id = container & 0xFFFF;
			if (id >= dev->maximum_num_physicals)
				break;
			lun = (container >> 16) & 0xFF;
			channel = aac_phys_to_logical(channel);
			device_config_needed =
			  (((__le32 *)aifcmd->data)[0] ==
			    cpu_to_le32(AifEnAddJBOD)) ? ADD : DELETE;
			break;

		case AifEnEnclosureManagement:
			/*
			 * If in JBOD mode, automatic exposure of new
			 * physical target to be suppressed until configured.
			 */
			if (dev->jbod)
				break;
			switch (le32_to_cpu(((__le32 *)aifcmd->data)[3])) {
			case EM_DRIVE_INSERTION:
			case EM_DRIVE_REMOVAL:
				container = le32_to_cpu(
					((__le32 *)aifcmd->data)[2]);
				if ((container >> 28))
					break;
				channel = (container >> 24) & 0xF;
				if (channel >= dev->maximum_num_channels)
					break;
				id = container & 0xFFFF;
				lun = (container >> 16) & 0xFF;
				if (id >= dev->maximum_num_physicals) {
					/* legacy dev_t ? */
					if ((0x2000 <= id) || lun || channel ||
					  ((channel = (id >> 7) & 0x3F) >=
					  dev->maximum_num_channels))
						break;
					lun = (id >> 4) & 7;
					id &= 0xF;
				}
				channel = aac_phys_to_logical(channel);
				device_config_needed =
				  (((__le32 *)aifcmd->data)[3]
				    == cpu_to_le32(EM_DRIVE_INSERTION)) ?
				  ADD : DELETE;
				break;
			}
			break;
		}

		/*
		 *	If we are waiting on something and this happens to be
		 * that thing then set the re-configure flag.
		 */
		if (container != (u32)-1) {
			if (container >= dev->maximum_num_containers)
				break;
			if ((dev->fsa_dev[container].config_waiting_on ==
			    le32_to_cpu(*(__le32 *)aifcmd->data)) &&
			 time_before(jiffies, dev->fsa_dev[container].config_waiting_stamp + AIF_SNIFF_TIMEOUT))
				dev->fsa_dev[container].config_waiting_on = 0;
		} else for (container = 0;
		    container < dev->maximum_num_containers; ++container) {
			if ((dev->fsa_dev[container].config_waiting_on ==
			    le32_to_cpu(*(__le32 *)aifcmd->data)) &&
			 time_before(jiffies, dev->fsa_dev[container].config_waiting_stamp + AIF_SNIFF_TIMEOUT))
				dev->fsa_dev[container].config_waiting_on = 0;
		}
		break;

	case AifCmdJobProgress:
		/*
		 *	These are job progress AIF's. When a Clear is being
		 * done on a container it is initially created then hidden from
		 * the OS. When the clear completes we don't get a config
		 * change so we monitor the job status complete on a clear then
		 * wait for a container change.
		 */

		if (((__le32 *)aifcmd->data)[1] == cpu_to_le32(AifJobCtrZero) &&
		    (((__le32 *)aifcmd->data)[6] == ((__le32 *)aifcmd->data)[5] ||
		     ((__le32 *)aifcmd->data)[4] == cpu_to_le32(AifJobStsSuccess))) {
			for (container = 0;
			    container < dev->maximum_num_containers;
			    ++container) {
				/*
				 * Stomp on all config sequencing for all
				 * containers?
				 */
				dev->fsa_dev[container].config_waiting_on =
					AifEnContainerChange;
				dev->fsa_dev[container].config_needed = ADD;
				dev->fsa_dev[container].config_waiting_stamp =
					jiffies;
			}
		}
		if (((__le32 *)aifcmd->data)[1] == cpu_to_le32(AifJobCtrZero) &&
		    ((__le32 *)aifcmd->data)[6] == 0 &&
		    ((__le32 *)aifcmd->data)[4] == cpu_to_le32(AifJobStsRunning)) {
			for (container = 0;
			    container < dev->maximum_num_containers;
			    ++container) {
				/*
				 * Stomp on all config sequencing for all
				 * containers?
				 */
				dev->fsa_dev[container].config_waiting_on =
					AifEnContainerChange;
				dev->fsa_dev[container].config_needed = DELETE;
				dev->fsa_dev[container].config_waiting_stamp =
					jiffies;
			}
		}
		break;
	}

	if (device_config_needed == NOTHING)
	for (container = 0; container < dev->maximum_num_containers;
	    ++container) {
		if ((dev->fsa_dev[container].config_waiting_on == 0) &&
			(dev->fsa_dev[container].config_needed != NOTHING) &&
			time_before(jiffies, dev->fsa_dev[container].config_waiting_stamp + AIF_SNIFF_TIMEOUT)) {
			device_config_needed =
				dev->fsa_dev[container].config_needed;
			dev->fsa_dev[container].config_needed = NOTHING;
			channel = CONTAINER_TO_CHANNEL(container);
			id = CONTAINER_TO_ID(container);
			lun = CONTAINER_TO_LUN(container);
			break;
		}
	}
	if (device_config_needed == NOTHING)
		return;

	/*
	 *	If we decided that a re-configuration needs to be done,
	 * schedule it here on the way out the door, please close the door
	 * behind you.
	 */

	/*
	 *	Find the scsi_device associated with the SCSI address,
	 * and mark it as changed, invalidating the cache. This deals
	 * with changes to existing device IDs.
	 */

	if (!dev || !dev->scsi_host_ptr)
		return;
	/*
	 * force reload of disk info via aac_probe_container
	 */
	if ((channel == CONTAINER_CHANNEL) &&
	  (device_config_needed != NOTHING)) {
		if (dev->fsa_dev[container].valid == 1)
			dev->fsa_dev[container].valid = 2;
		aac_probe_container(dev, container);
	}
	device = scsi_device_lookup(dev->scsi_host_ptr, channel, id, lun);
	if (device) {
		switch (device_config_needed) {
		case DELETE:
			if (scsi_device_online(device)) {
				scsi_device_set_state(device, SDEV_OFFLINE);
				sdev_printk(KERN_INFO, device,
					"Device offlined - %s\n",
					(channel == CONTAINER_CHANNEL) ?
						"array deleted" :
						"enclosure services event");
			}
			break;
		case ADD:
			if (!scsi_device_online(device)) {
				sdev_printk(KERN_INFO, device,
					"Device online - %s\n",
					(channel == CONTAINER_CHANNEL) ?
						"array created" :
						"enclosure services event");
				scsi_device_set_state(device, SDEV_RUNNING);
			}
			/* FALLTHRU */
		case CHANGE:
			if ((channel == CONTAINER_CHANNEL)
			 && (!dev->fsa_dev[container].valid)) {
				if (!scsi_device_online(device))
					break;
				scsi_device_set_state(device, SDEV_OFFLINE);
				sdev_printk(KERN_INFO, device,
					"Device offlined - %s\n",
					"array failed");
				break;
			}
			scsi_rescan_device(&device->sdev_gendev);

		default:
			break;
		}
		scsi_device_put(device);
		device_config_needed = NOTHING;
	}
	if (device_config_needed == ADD)
		scsi_add_device(dev->scsi_host_ptr, channel, id, lun);
}

static int _aac_reset_adapter(struct aac_dev *aac, int forced)
{
	int index, quirks;
	int retval;
	struct Scsi_Host *host;
	struct scsi_device *dev;
	struct scsi_cmnd *command;
	struct scsi_cmnd *command_list;
	int jafo = 0;

	/*
	 * Assumptions:
	 *	- host is locked, unless called by the aacraid thread.
	 *	  (a matter of convenience, due to legacy issues surrounding
	 *	  eh_host_adapter_reset).
	 *	- in_reset is asserted, so no new i/o is getting to the
	 *	  card.
	 *	- The card is dead, or will be very shortly ;-/ so no new
	 *	  commands are completing in the interrupt service.
	 */
	host = aac->scsi_host_ptr;
	scsi_block_requests(host);
	aac_adapter_disable_int(aac);
	if (aac->thread->pid != current->pid) {
		spin_unlock_irq(host->host_lock);
		kthread_stop(aac->thread);
		jafo = 1;
	}

	/*
	 *	If a positive health, means in a known DEAD PANIC
	 * state and the adapter could be reset to `try again'.
	 */
	retval = aac_adapter_restart(aac, forced ? 0 : aac_adapter_check_health(aac));

	if (retval)
		goto out;

	/*
	 *	Loop through the fibs, close the synchronous FIBS
	 */
	for (retval = 1, index = 0; index < (aac->scsi_host_ptr->can_queue + AAC_NUM_MGT_FIB); index++) {
		struct fib *fib = &aac->fibs[index];
		if (!(fib->hw_fib_va->header.XferState & cpu_to_le32(NoResponseExpected | Async)) &&
		  (fib->hw_fib_va->header.XferState & cpu_to_le32(ResponseExpected))) {
			unsigned long flagv;
			spin_lock_irqsave(&fib->event_lock, flagv);
			up(&fib->event_wait);
			spin_unlock_irqrestore(&fib->event_lock, flagv);
			schedule();
			retval = 0;
		}
	}
	/* Give some extra time for ioctls to complete. */
	if (retval == 0)
		ssleep(2);
	index = aac->cardtype;

	/*
	 * Re-initialize the adapter, first free resources, then carefully
	 * apply the initialization sequence to come back again. Only risk
	 * is a change in Firmware dropping cache, it is assumed the caller
	 * will ensure that i/o is queisced and the card is flushed in that
	 * case.
	 */
	aac_fib_map_free(aac);
	pci_free_consistent(aac->pdev, aac->comm_size, aac->comm_addr, aac->comm_phys);
	aac->comm_addr = NULL;
	aac->comm_phys = 0;
	kfree(aac->queues);
	aac->queues = NULL;
	free_irq(aac->pdev->irq, aac);
	kfree(aac->fsa_dev);
	aac->fsa_dev = NULL;
	quirks = aac_get_driver_ident(index)->quirks;
	if (quirks & AAC_QUIRK_31BIT) {
		if (((retval = pci_set_dma_mask(aac->pdev, DMA_31BIT_MASK))) ||
		  ((retval = pci_set_consistent_dma_mask(aac->pdev, DMA_31BIT_MASK))))
			goto out;
	} else {
		if (((retval = pci_set_dma_mask(aac->pdev, DMA_32BIT_MASK))) ||
		  ((retval = pci_set_consistent_dma_mask(aac->pdev, DMA_32BIT_MASK))))
			goto out;
	}
	if ((retval = (*(aac_get_driver_ident(index)->init))(aac)))
		goto out;
	if (quirks & AAC_QUIRK_31BIT)
		if ((retval = pci_set_dma_mask(aac->pdev, DMA_32BIT_MASK)))
			goto out;
	if (jafo) {
		aac->thread = kthread_run(aac_command_thread, aac, aac->name);
		if (IS_ERR(aac->thread)) {
			retval = PTR_ERR(aac->thread);
			goto out;
		}
	}
	(void)aac_get_adapter_info(aac);
	if ((quirks & AAC_QUIRK_34SG) && (host->sg_tablesize > 34)) {
		host->sg_tablesize = 34;
		host->max_sectors = (host->sg_tablesize * 8) + 112;
	}
	if ((quirks & AAC_QUIRK_17SG) && (host->sg_tablesize > 17)) {
		host->sg_tablesize = 17;
		host->max_sectors = (host->sg_tablesize * 8) + 112;
	}
	aac_get_config_status(aac, 1);
	aac_get_containers(aac);
	/*
	 * This is where the assumption that the Adapter is quiesced
	 * is important.
	 */
	command_list = NULL;
	__shost_for_each_device(dev, host) {
		unsigned long flags;
		spin_lock_irqsave(&dev->list_lock, flags);
		list_for_each_entry(command, &dev->cmd_list, list)
			if (command->SCp.phase == AAC_OWNER_FIRMWARE) {
				command->SCp.buffer = (struct scatterlist *)command_list;
				command_list = command;
			}
		spin_unlock_irqrestore(&dev->list_lock, flags);
	}
	while ((command = command_list)) {
		command_list = (struct scsi_cmnd *)command->SCp.buffer;
		command->SCp.buffer = NULL;
		command->result = DID_OK << 16
		  | COMMAND_COMPLETE << 8
		  | SAM_STAT_TASK_SET_FULL;
		command->SCp.phase = AAC_OWNER_ERROR_HANDLER;
		command->scsi_done(command);
	}
	retval = 0;

out:
	aac->in_reset = 0;
	scsi_unblock_requests(host);
	if (jafo) {
		spin_lock_irq(host->host_lock);
	}
	return retval;
}

int aac_reset_adapter(struct aac_dev * aac, int forced)
{
	unsigned long flagv = 0;
	int retval;
	struct Scsi_Host * host;

	if (spin_trylock_irqsave(&aac->fib_lock, flagv) == 0)
		return -EBUSY;

	if (aac->in_reset) {
		spin_unlock_irqrestore(&aac->fib_lock, flagv);
		return -EBUSY;
	}
	aac->in_reset = 1;
	spin_unlock_irqrestore(&aac->fib_lock, flagv);

	/*
	 * Wait for all commands to complete to this specific
	 * target (block maximum 60 seconds). Although not necessary,
	 * it does make us a good storage citizen.
	 */
	host = aac->scsi_host_ptr;
	scsi_block_requests(host);
	if (forced < 2) for (retval = 60; retval; --retval) {
		struct scsi_device * dev;
		struct scsi_cmnd * command;
		int active = 0;

		__shost_for_each_device(dev, host) {
			spin_lock_irqsave(&dev->list_lock, flagv);
			list_for_each_entry(command, &dev->cmd_list, list) {
				if (command->SCp.phase == AAC_OWNER_FIRMWARE) {
					active++;
					break;
				}
			}
			spin_unlock_irqrestore(&dev->list_lock, flagv);
			if (active)
				break;

		}
		/*
		 * We can exit If all the commands are complete
		 */
		if (active == 0)
			break;
		ssleep(1);
	}

	/* Quiesce build, flush cache, write through mode */
	if (forced < 2)
		aac_send_shutdown(aac);
	spin_lock_irqsave(host->host_lock, flagv);
	retval = _aac_reset_adapter(aac, forced ? forced : ((aac_check_reset != 0) && (aac_check_reset != 1)));
	spin_unlock_irqrestore(host->host_lock, flagv);

	if ((forced < 2) && (retval == -ENODEV)) {
		/* Unwind aac_send_shutdown() IOP_RESET unsupported/disabled */
		struct fib * fibctx = aac_fib_alloc(aac);
		if (fibctx) {
			struct aac_pause *cmd;
			int status;

			aac_fib_init(fibctx);

			cmd = (struct aac_pause *) fib_data(fibctx);

			cmd->command = cpu_to_le32(VM_ContainerConfig);
			cmd->type = cpu_to_le32(CT_PAUSE_IO);
			cmd->timeout = cpu_to_le32(1);
			cmd->min = cpu_to_le32(1);
			cmd->noRescan = cpu_to_le32(1);
			cmd->count = cpu_to_le32(0);

			status = aac_fib_send(ContainerCommand,
			  fibctx,
			  sizeof(struct aac_pause),
			  FsaNormal,
			  -2 /* Timeout silently */, 1,
			  NULL, NULL);

			if (status >= 0)
				aac_fib_complete(fibctx);
			aac_fib_free(fibctx);
		}
	}

	return retval;
}

int aac_check_health(struct aac_dev * aac)
{
	int BlinkLED;
	unsigned long time_now, flagv = 0;
	struct list_head * entry;
	struct Scsi_Host * host;

	/* Extending the scope of fib_lock slightly to protect aac->in_reset */
	if (spin_trylock_irqsave(&aac->fib_lock, flagv) == 0)
		return 0;

	if (aac->in_reset || !(BlinkLED = aac_adapter_check_health(aac))) {
		spin_unlock_irqrestore(&aac->fib_lock, flagv);
		return 0; /* OK */
	}

	aac->in_reset = 1;

	/* Fake up an AIF:
	 *	aac_aifcmd.command = AifCmdEventNotify = 1
	 *	aac_aifcmd.seqnum = 0xFFFFFFFF
	 *	aac_aifcmd.data[0] = AifEnExpEvent = 23
	 *	aac_aifcmd.data[1] = AifExeFirmwarePanic = 3
	 *	aac.aifcmd.data[2] = AifHighPriority = 3
	 *	aac.aifcmd.data[3] = BlinkLED
	 */

	time_now = jiffies/HZ;
	entry = aac->fib_list.next;

	/*
	 * For each Context that is on the
	 * fibctxList, make a copy of the
	 * fib, and then set the event to wake up the
	 * thread that is waiting for it.
	 */
	while (entry != &aac->fib_list) {
		/*
		 * Extract the fibctx
		 */
		struct aac_fib_context *fibctx = list_entry(entry, struct aac_fib_context, next);
		struct hw_fib * hw_fib;
		struct fib * fib;
		/*
		 * Check if the queue is getting
		 * backlogged
		 */
		if (fibctx->count > 20) {
			/*
			 * It's *not* jiffies folks,
			 * but jiffies / HZ, so do not
			 * panic ...
			 */
			u32 time_last = fibctx->jiffies;
			/*
			 * Has it been > 2 minutes
			 * since the last read off
			 * the queue?
			 */
			if ((time_now - time_last) > aif_timeout) {
				entry = entry->next;
				aac_close_fib_context(aac, fibctx);
				continue;
			}
		}
		/*
		 * Warning: no sleep allowed while
		 * holding spinlock
		 */
		hw_fib = kzalloc(sizeof(struct hw_fib), GFP_ATOMIC);
		fib = kzalloc(sizeof(struct fib), GFP_ATOMIC);
		if (fib && hw_fib) {
			struct aac_aifcmd * aif;

			fib->hw_fib_va = hw_fib;
			fib->dev = aac;
			aac_fib_init(fib);
			fib->type = FSAFS_NTC_FIB_CONTEXT;
			fib->size = sizeof (struct fib);
			fib->data = hw_fib->data;
			aif = (struct aac_aifcmd *)hw_fib->data;
			aif->command = cpu_to_le32(AifCmdEventNotify);
			aif->seqnum = cpu_to_le32(0xFFFFFFFF);
			((__le32 *)aif->data)[0] = cpu_to_le32(AifEnExpEvent);
			((__le32 *)aif->data)[1] = cpu_to_le32(AifExeFirmwarePanic);
			((__le32 *)aif->data)[2] = cpu_to_le32(AifHighPriority);
			((__le32 *)aif->data)[3] = cpu_to_le32(BlinkLED);

			/*
			 * Put the FIB onto the
			 * fibctx's fibs
			 */
			list_add_tail(&fib->fiblink, &fibctx->fib_list);
			fibctx->count++;
			/*
			 * Set the event to wake up the
			 * thread that will waiting.
			 */
			up(&fibctx->wait_sem);
		} else {
			printk(KERN_WARNING "aifd: didn't allocate NewFib.\n");
			kfree(fib);
			kfree(hw_fib);
		}
		entry = entry->next;
	}

	spin_unlock_irqrestore(&aac->fib_lock, flagv);

	if (BlinkLED < 0) {
		printk(KERN_ERR "%s: Host adapter dead %d\n", aac->name, BlinkLED);
		goto out;
	}

	printk(KERN_ERR "%s: Host adapter BLINK LED 0x%x\n", aac->name, BlinkLED);

	if (!aac_check_reset || ((aac_check_reset != 1) &&
		(aac->supplement_adapter_info.SupportedOptions2 &
			AAC_OPTION_IGNORE_RESET)))
		goto out;
	host = aac->scsi_host_ptr;
	if (aac->thread->pid != current->pid)
		spin_lock_irqsave(host->host_lock, flagv);
	BlinkLED = _aac_reset_adapter(aac, aac_check_reset != 1);
	if (aac->thread->pid != current->pid)
		spin_unlock_irqrestore(host->host_lock, flagv);
	return BlinkLED;

out:
	aac->in_reset = 0;
	return BlinkLED;
}


/**
 *	aac_command_thread	-	command processing thread
 *	@dev: Adapter to monitor
 *
 *	Waits on the commandready event in it's queue. When the event gets set
 *	it will pull FIBs off it's queue. It will continue to pull FIBs off
 *	until the queue is empty. When the queue is empty it will wait for
 *	more FIBs.
 */

int aac_command_thread(void *data)
{
	struct aac_dev *dev = data;
	struct hw_fib *hw_fib, *hw_newfib;
	struct fib *fib, *newfib;
	struct aac_fib_context *fibctx;
	unsigned long flags;
	DECLARE_WAITQUEUE(wait, current);
	unsigned long next_jiffies = jiffies + HZ;
	unsigned long next_check_jiffies = next_jiffies;
	long difference = HZ;

	/*
	 *	We can only have one thread per adapter for AIF's.
	 */
	if (dev->aif_thread)
		return -EINVAL;

	/*
	 *	Let the DPC know it has a place to send the AIF's to.
	 */
	dev->aif_thread = 1;
	add_wait_queue(&dev->queues->queue[HostNormCmdQueue].cmdready, &wait);
	set_current_state(TASK_INTERRUPTIBLE);
	dprintk ((KERN_INFO "aac_command_thread start\n"));
	while (1) {
		spin_lock_irqsave(dev->queues->queue[HostNormCmdQueue].lock, flags);
		while(!list_empty(&(dev->queues->queue[HostNormCmdQueue].cmdq))) {
			struct list_head *entry;
			struct aac_aifcmd * aifcmd;

			set_current_state(TASK_RUNNING);

			entry = dev->queues->queue[HostNormCmdQueue].cmdq.next;
			list_del(entry);

			spin_unlock_irqrestore(dev->queues->queue[HostNormCmdQueue].lock, flags);
			fib = list_entry(entry, struct fib, fiblink);
			/*
			 *	We will process the FIB here or pass it to a
			 *	worker thread that is TBD. We Really can't
			 *	do anything at this point since we don't have
			 *	anything defined for this thread to do.
			 */
			hw_fib = fib->hw_fib_va;
			memset(fib, 0, sizeof(struct fib));
			fib->type = FSAFS_NTC_FIB_CONTEXT;
			fib->size = sizeof(struct fib);
			fib->hw_fib_va = hw_fib;
			fib->data = hw_fib->data;
			fib->dev = dev;
			/*
			 *	We only handle AifRequest fibs from the adapter.
			 */
			aifcmd = (struct aac_aifcmd *) hw_fib->data;
			if (aifcmd->command == cpu_to_le32(AifCmdDriverNotify)) {
				/* Handle Driver Notify Events */
				aac_handle_aif(dev, fib);
				*(__le32 *)hw_fib->data = cpu_to_le32(ST_OK);
				aac_fib_adapter_complete(fib, (u16)sizeof(u32));
			} else {
				/* The u32 here is important and intended. We are using
				   32bit wrapping time to fit the adapter field */

				u32 time_now, time_last;
				unsigned long flagv;
				unsigned num;
				struct hw_fib ** hw_fib_pool, ** hw_fib_p;
				struct fib ** fib_pool, ** fib_p;

				/* Sniff events */
				if ((aifcmd->command ==
				     cpu_to_le32(AifCmdEventNotify)) ||
				    (aifcmd->command ==
				     cpu_to_le32(AifCmdJobProgress))) {
					aac_handle_aif(dev, fib);
				}

				time_now = jiffies/HZ;

				/*
				 * Warning: no sleep allowed while
				 * holding spinlock. We take the estimate
				 * and pre-allocate a set of fibs outside the
				 * lock.
				 */
				num = le32_to_cpu(dev->init->AdapterFibsSize)
				    / sizeof(struct hw_fib); /* some extra */
				spin_lock_irqsave(&dev->fib_lock, flagv);
				entry = dev->fib_list.next;
				while (entry != &dev->fib_list) {
					entry = entry->next;
					++num;
				}
				spin_unlock_irqrestore(&dev->fib_lock, flagv);
				hw_fib_pool = NULL;
				fib_pool = NULL;
				if (num
				 && ((hw_fib_pool = kmalloc(sizeof(struct hw_fib *) * num, GFP_KERNEL)))
				 && ((fib_pool = kmalloc(sizeof(struct fib *) * num, GFP_KERNEL)))) {
					hw_fib_p = hw_fib_pool;
					fib_p = fib_pool;
					while (hw_fib_p < &hw_fib_pool[num]) {
						if (!(*(hw_fib_p++) = kmalloc(sizeof(struct hw_fib), GFP_KERNEL))) {
							--hw_fib_p;
							break;
						}
						if (!(*(fib_p++) = kmalloc(sizeof(struct fib), GFP_KERNEL))) {
							kfree(*(--hw_fib_p));
							break;
						}
					}
					if ((num = hw_fib_p - hw_fib_pool) == 0) {
						kfree(fib_pool);
						fib_pool = NULL;
						kfree(hw_fib_pool);
						hw_fib_pool = NULL;
					}
				} else {
					kfree(hw_fib_pool);
					hw_fib_pool = NULL;
				}
				spin_lock_irqsave(&dev->fib_lock, flagv);
				entry = dev->fib_list.next;
				/*
				 * For each Context that is on the
				 * fibctxList, make a copy of the
				 * fib, and then set the event to wake up the
				 * thread that is waiting for it.
				 */
				hw_fib_p = hw_fib_pool;
				fib_p = fib_pool;
				while (entry != &dev->fib_list) {
					/*
					 * Extract the fibctx
					 */
					fibctx = list_entry(entry, struct aac_fib_context, next);
					/*
					 * Check if the queue is getting
					 * backlogged
					 */
					if (fibctx->count > 20)
					{
						/*
						 * It's *not* jiffies folks,
						 * but jiffies / HZ so do not
						 * panic ...
						 */
						time_last = fibctx->jiffies;
						/*
						 * Has it been > 2 minutes
						 * since the last read off
						 * the queue?
						 */
						if ((time_now - time_last) > aif_timeout) {
							entry = entry->next;
							aac_close_fib_context(dev, fibctx);
							continue;
						}
					}
					/*
					 * Warning: no sleep allowed while
					 * holding spinlock
					 */
					if (hw_fib_p < &hw_fib_pool[num]) {
						hw_newfib = *hw_fib_p;
						*(hw_fib_p++) = NULL;
						newfib = *fib_p;
						*(fib_p++) = NULL;
						/*
						 * Make the copy of the FIB
						 */
						memcpy(hw_newfib, hw_fib, sizeof(struct hw_fib));
						memcpy(newfib, fib, sizeof(struct fib));
						newfib->hw_fib_va = hw_newfib;
						/*
						 * Put the FIB onto the
						 * fibctx's fibs
						 */
						list_add_tail(&newfib->fiblink, &fibctx->fib_list);
						fibctx->count++;
						/*
						 * Set the event to wake up the
						 * thread that is waiting.
						 */
						up(&fibctx->wait_sem);
					} else {
						printk(KERN_WARNING "aifd: didn't allocate NewFib.\n");
					}
					entry = entry->next;
				}
				/*
				 *	Set the status of this FIB
				 */
				*(__le32 *)hw_fib->data = cpu_to_le32(ST_OK);
				aac_fib_adapter_complete(fib, sizeof(u32));
				spin_unlock_irqrestore(&dev->fib_lock, flagv);
				/* Free up the remaining resources */
				hw_fib_p = hw_fib_pool;
				fib_p = fib_pool;
				while (hw_fib_p < &hw_fib_pool[num]) {
					kfree(*hw_fib_p);
					kfree(*fib_p);
					++fib_p;
					++hw_fib_p;
				}
				kfree(hw_fib_pool);
				kfree(fib_pool);
			}
			kfree(fib);
			spin_lock_irqsave(dev->queues->queue[HostNormCmdQueue].lock, flags);
		}
		/*
		 *	There are no more AIF's
		 */
		spin_unlock_irqrestore(dev->queues->queue[HostNormCmdQueue].lock, flags);

		/*
		 *	Background activity
		 */
		if ((time_before(next_check_jiffies,next_jiffies))
		 && ((difference = next_check_jiffies - jiffies) <= 0)) {
			next_check_jiffies = next_jiffies;
			if (aac_check_health(dev) == 0) {
				difference = ((long)(unsigned)check_interval)
					   * HZ;
				next_check_jiffies = jiffies + difference;
			} else if (!dev->queues)
				break;
		}
		if (!time_before(next_check_jiffies,next_jiffies)
		 && ((difference = next_jiffies - jiffies) <= 0)) {
			struct timeval now;
			int ret;

			/* Don't even try to talk to adapter if its sick */
			ret = aac_check_health(dev);
			if (!ret && !dev->queues)
				break;
			next_check_jiffies = jiffies
					   + ((long)(unsigned)check_interval)
					   * HZ;
			do_gettimeofday(&now);

			/* Synchronize our watches */
			if (((1000000 - (1000000 / HZ)) > now.tv_usec)
			 && (now.tv_usec > (1000000 / HZ)))
				difference = (((1000000 - now.tv_usec) * HZ)
				  + 500000) / 1000000;
			else if (ret == 0) {
				struct fib *fibptr;

				if ((fibptr = aac_fib_alloc(dev))) {
					__le32 *info;

					aac_fib_init(fibptr);

					info = (__le32 *) fib_data(fibptr);
					if (now.tv_usec > 500000)
						++now.tv_sec;

					*info = cpu_to_le32(now.tv_sec);

					(void)aac_fib_send(SendHostTime,
						fibptr,
						sizeof(*info),
						FsaNormal,
						1, 1,
						NULL,
						NULL);
					aac_fib_complete(fibptr);
					aac_fib_free(fibptr);
				}
				difference = (long)(unsigned)update_interval*HZ;
			} else {
				/* retry shortly */
				difference = 10 * HZ;
			}
			next_jiffies = jiffies + difference;
			if (time_before(next_check_jiffies,next_jiffies))
				difference = next_check_jiffies - jiffies;
		}
		if (difference <= 0)
			difference = 1;
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(difference);

		if (kthread_should_stop())
			break;
	}
	if (dev->queues)
		remove_wait_queue(&dev->queues->queue[HostNormCmdQueue].cmdready, &wait);
	dev->aif_thread = 0;
	return 0;
}
