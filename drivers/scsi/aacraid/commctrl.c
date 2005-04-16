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
 *  commctrl.c
 *
 * Abstract: Contains all routines for control of the AFA comm layer
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
#include <linux/dma-mapping.h>
#include <linux/blkdev.h>
#include <asm/semaphore.h>
#include <asm/uaccess.h>

#include "aacraid.h"

/**
 *	ioctl_send_fib	-	send a FIB from userspace
 *	@dev:	adapter is being processed
 *	@arg:	arguments to the ioctl call
 *	
 *	This routine sends a fib to the adapter on behalf of a user level
 *	program.
 */
 
static int ioctl_send_fib(struct aac_dev * dev, void __user *arg)
{
	struct hw_fib * kfib;
	struct fib *fibptr;

	fibptr = fib_alloc(dev);
	if(fibptr == NULL)
		return -ENOMEM;
		
	kfib = fibptr->hw_fib;
	/*
	 *	First copy in the header so that we can check the size field.
	 */
	if (copy_from_user((void *)kfib, arg, sizeof(struct aac_fibhdr))) {
		fib_free(fibptr);
		return -EFAULT;
	}
	/*
	 *	Since we copy based on the fib header size, make sure that we
	 *	will not overrun the buffer when we copy the memory. Return
	 *	an error if we would.
	 */
	if (le16_to_cpu(kfib->header.Size) > 
			sizeof(struct hw_fib) - sizeof(struct aac_fibhdr)) {
		fib_free(fibptr);
		return -EINVAL;
	}

	if (copy_from_user(kfib, arg, le16_to_cpu(kfib->header.Size) +
				sizeof(struct aac_fibhdr))) {
		fib_free(fibptr);
		return -EFAULT;
	}

	if (kfib->header.Command == cpu_to_le32(TakeABreakPt)) {
		aac_adapter_interrupt(dev);
		/*
		 * Since we didn't really send a fib, zero out the state to allow 
		 * cleanup code not to assert.
		 */
		kfib->header.XferState = 0;
	} else {
		int retval = fib_send(kfib->header.Command, fibptr,
				le16_to_cpu(kfib->header.Size) , FsaNormal,
				1, 1, NULL, NULL);
		if (retval) {
			fib_free(fibptr);
			return retval;
		}
		if (fib_complete(fibptr) != 0) {
			fib_free(fibptr);
			return -EINVAL;
		}
	}
	/*
	 *	Make sure that the size returned by the adapter (which includes
	 *	the header) is less than or equal to the size of a fib, so we
	 *	don't corrupt application data. Then copy that size to the user
	 *	buffer. (Don't try to add the header information again, since it
	 *	was already included by the adapter.)
	 */

	if (copy_to_user(arg, (void *)kfib, kfib->header.Size)) {
		fib_free(fibptr);
		return -EFAULT;
	}
	fib_free(fibptr);
	return 0;
}

/**
 *	open_getadapter_fib	-	Get the next fib
 *
 *	This routine will get the next Fib, if available, from the AdapterFibContext
 *	passed in from the user.
 */

static int open_getadapter_fib(struct aac_dev * dev, void __user *arg)
{
	struct aac_fib_context * fibctx;
	int status;

	fibctx = kmalloc(sizeof(struct aac_fib_context), GFP_KERNEL);
	if (fibctx == NULL) {
		status = -ENOMEM;
	} else {
		unsigned long flags;
		struct list_head * entry;
		struct aac_fib_context * context;

		fibctx->type = FSAFS_NTC_GET_ADAPTER_FIB_CONTEXT;
		fibctx->size = sizeof(struct aac_fib_context);
 		/*
		 *	Yes yes, I know this could be an index, but we have a
		 * better guarantee of uniqueness for the locked loop below.
		 * Without the aid of a persistent history, this also helps
		 * reduce the chance that the opaque context would be reused.
		 */
		fibctx->unique = (u32)((ulong)fibctx & 0xFFFFFFFF);
		/*
		 *	Initialize the mutex used to wait for the next AIF.
		 */
		init_MUTEX_LOCKED(&fibctx->wait_sem);
		fibctx->wait = 0;
		/*
		 *	Initialize the fibs and set the count of fibs on
		 *	the list to 0.
		 */
		fibctx->count = 0;
		INIT_LIST_HEAD(&fibctx->fib_list);
		fibctx->jiffies = jiffies/HZ;
		/*
		 *	Now add this context onto the adapter's 
		 *	AdapterFibContext list.
		 */
		spin_lock_irqsave(&dev->fib_lock, flags);
		/* Ensure that we have a unique identifier */
		entry = dev->fib_list.next;
		while (entry != &dev->fib_list) {
			context = list_entry(entry, struct aac_fib_context, next);
			if (context->unique == fibctx->unique) {
				/* Not unique (32 bits) */
				fibctx->unique++;
				entry = dev->fib_list.next;
			} else {
				entry = entry->next;
			}
		}
		list_add_tail(&fibctx->next, &dev->fib_list);
		spin_unlock_irqrestore(&dev->fib_lock, flags);
		if (copy_to_user(arg,  &fibctx->unique, 
						sizeof(fibctx->unique))) {
			status = -EFAULT;
		} else {
			status = 0;
		}	
	}
	return status;
}

/**
 *	next_getadapter_fib	-	get the next fib
 *	@dev: adapter to use
 *	@arg: ioctl argument
 *	
 * 	This routine will get the next Fib, if available, from the AdapterFibContext
 *	passed in from the user.
 */

static int next_getadapter_fib(struct aac_dev * dev, void __user *arg)
{
	struct fib_ioctl f;
	struct fib *fib;
	struct aac_fib_context *fibctx;
	int status;
	struct list_head * entry;
	unsigned long flags;
	
	if(copy_from_user((void *)&f, arg, sizeof(struct fib_ioctl)))
		return -EFAULT;
	/*
	 *	Verify that the HANDLE passed in was a valid AdapterFibContext
	 *
	 *	Search the list of AdapterFibContext addresses on the adapter
	 *	to be sure this is a valid address
	 */
	entry = dev->fib_list.next;
	fibctx = NULL;

	while (entry != &dev->fib_list) {
		fibctx = list_entry(entry, struct aac_fib_context, next);
		/*
		 *	Extract the AdapterFibContext from the Input parameters.
		 */
		if (fibctx->unique == f.fibctx) {   /* We found a winner */
			break;
		}
		entry = entry->next;
		fibctx = NULL;
	}
	if (!fibctx) {
		dprintk ((KERN_INFO "Fib Context not found\n"));
		return -EINVAL;
	}

	if((fibctx->type != FSAFS_NTC_GET_ADAPTER_FIB_CONTEXT) ||
		 (fibctx->size != sizeof(struct aac_fib_context))) {
		dprintk ((KERN_INFO "Fib Context corrupt?\n"));
		return -EINVAL;
	}
	status = 0;
	spin_lock_irqsave(&dev->fib_lock, flags);
	/*
	 *	If there are no fibs to send back, then either wait or return
	 *	-EAGAIN
	 */
return_fib:
	if (!list_empty(&fibctx->fib_list)) {
		struct list_head * entry;
		/*
		 *	Pull the next fib from the fibs
		 */
		entry = fibctx->fib_list.next;
		list_del(entry);
		
		fib = list_entry(entry, struct fib, fiblink);
		fibctx->count--;
		spin_unlock_irqrestore(&dev->fib_lock, flags);
		if (copy_to_user(f.fib, fib->hw_fib, sizeof(struct hw_fib))) {
			kfree(fib->hw_fib);
			kfree(fib);
			return -EFAULT;
		}	
		/*
		 *	Free the space occupied by this copy of the fib.
		 */
		kfree(fib->hw_fib);
		kfree(fib);
		status = 0;
		fibctx->jiffies = jiffies/HZ;
	} else {
		spin_unlock_irqrestore(&dev->fib_lock, flags);
		if (f.wait) {
			if(down_interruptible(&fibctx->wait_sem) < 0) {
				status = -EINTR;
			} else {
				/* Lock again and retry */
				spin_lock_irqsave(&dev->fib_lock, flags);
				goto return_fib;
			}
		} else {
			status = -EAGAIN;
		}	
	}
	return status;
}

int aac_close_fib_context(struct aac_dev * dev, struct aac_fib_context * fibctx)
{
	struct fib *fib;

	/*
	 *	First free any FIBs that have not been consumed.
	 */
	while (!list_empty(&fibctx->fib_list)) {
		struct list_head * entry;
		/*
		 *	Pull the next fib from the fibs
		 */
		entry = fibctx->fib_list.next;
		list_del(entry);
		fib = list_entry(entry, struct fib, fiblink);
		fibctx->count--;
		/*
		 *	Free the space occupied by this copy of the fib.
		 */
		kfree(fib->hw_fib);
		kfree(fib);
	}
	/*
	 *	Remove the Context from the AdapterFibContext List
	 */
	list_del(&fibctx->next);
	/*
	 *	Invalidate context
	 */
	fibctx->type = 0;
	/*
	 *	Free the space occupied by the Context
	 */
	kfree(fibctx);
	return 0;
}

/**
 *	close_getadapter_fib	-	close down user fib context
 *	@dev: adapter
 *	@arg: ioctl arguments
 *
 *	This routine will close down the fibctx passed in from the user.
 */
 
static int close_getadapter_fib(struct aac_dev * dev, void __user *arg)
{
	struct aac_fib_context *fibctx;
	int status;
	unsigned long flags;
	struct list_head * entry;

	/*
	 *	Verify that the HANDLE passed in was a valid AdapterFibContext
	 *
	 *	Search the list of AdapterFibContext addresses on the adapter
	 *	to be sure this is a valid address
	 */

	entry = dev->fib_list.next;
	fibctx = NULL;

	while(entry != &dev->fib_list) {
		fibctx = list_entry(entry, struct aac_fib_context, next);
		/*
		 *	Extract the fibctx from the input parameters
		 */
		if (fibctx->unique == (u32)(unsigned long)arg) {   
			/* We found a winner */
			break;
		}
		entry = entry->next;
		fibctx = NULL;
	}

	if (!fibctx)
		return 0; /* Already gone */

	if((fibctx->type != FSAFS_NTC_GET_ADAPTER_FIB_CONTEXT) ||
		 (fibctx->size != sizeof(struct aac_fib_context)))
		return -EINVAL;
	spin_lock_irqsave(&dev->fib_lock, flags);
	status = aac_close_fib_context(dev, fibctx);
	spin_unlock_irqrestore(&dev->fib_lock, flags);
	return status;
}

/**
 *	check_revision	-	close down user fib context
 *	@dev: adapter
 *	@arg: ioctl arguments
 *
 *	This routine returns the driver version.
 *      Under Linux, there have been no version incompatibilities, so this is 
 *      simple!
 */

static int check_revision(struct aac_dev *dev, void __user *arg)
{
	struct revision response;

	response.compat = 1;
	response.version = dev->adapter_info.kernelrev;
	response.build = dev->adapter_info.kernelbuild;

	if (copy_to_user(arg, &response, sizeof(response)))
		return -EFAULT;
	return 0;
}

/**
 *
 * aac_send_raw_scb
 *
 */

int aac_send_raw_srb(struct aac_dev* dev, void __user * arg)
{
	struct fib* srbfib;
	int status;
	struct aac_srb *srbcmd;
	struct aac_srb __user *user_srb = arg;
	struct aac_srb_reply __user *user_reply;
	struct aac_srb_reply* reply;
	u32 fibsize = 0;
	u32 flags = 0;
	s32 rcode = 0;
	u32 data_dir;
	void __user *sg_user[32];
	void *sg_list[32];
	u32   sg_indx = 0;
	u32 byte_count = 0;
	u32 actual_fibsize = 0;
	int i;


	if (!capable(CAP_SYS_ADMIN)){
		printk(KERN_DEBUG"aacraid: No permission to send raw srb\n"); 
		return -EPERM;
	}
	/*
	 *	Allocate and initialize a Fib then setup a BlockWrite command
	 */
	if (!(srbfib = fib_alloc(dev))) {
		return -1;
	}
	fib_init(srbfib);

	srbcmd = (struct aac_srb*) fib_data(srbfib);

	if(copy_from_user(&fibsize, &user_srb->count,sizeof(u32))){
		printk(KERN_DEBUG"aacraid: Could not copy data size from user\n"); 
		rcode = -EFAULT;
		goto cleanup;
	}

	if (fibsize > FIB_DATA_SIZE_IN_BYTES) {
		rcode = -EINVAL;
		goto cleanup;
	}

	if(copy_from_user(srbcmd, user_srb,fibsize)){
		printk(KERN_DEBUG"aacraid: Could not copy srb from user\n"); 
		rcode = -EFAULT;
		goto cleanup;
	}

	user_reply = arg+fibsize;

	flags = srbcmd->flags;
	// Fix up srb for endian and force some values
	srbcmd->function = cpu_to_le32(SRBF_ExecuteScsi);	// Force this
	srbcmd->channel  = cpu_to_le32(srbcmd->channel);
	srbcmd->id	 = cpu_to_le32(srbcmd->id);
	srbcmd->lun      = cpu_to_le32(srbcmd->lun);
	srbcmd->flags    = cpu_to_le32(srbcmd->flags);
	srbcmd->timeout  = cpu_to_le32(srbcmd->timeout);
	srbcmd->retry_limit =cpu_to_le32(0); // Obsolete parameter
	srbcmd->cdb_size = cpu_to_le32(srbcmd->cdb_size);
	
	switch (srbcmd->flags & (SRB_DataIn | SRB_DataOut)) {
	case SRB_DataOut:
		data_dir = DMA_TO_DEVICE;
		break;
	case (SRB_DataIn | SRB_DataOut):
		data_dir = DMA_BIDIRECTIONAL;
		break;
	case SRB_DataIn:
		data_dir = DMA_FROM_DEVICE;
		break;
	default:
		data_dir = DMA_NONE;
	}
	if (dev->dac_support == 1) {
		struct sgmap64* psg = (struct sgmap64*)&srbcmd->sg;
		byte_count = 0;

		/*
		 * This should also catch if user used the 32 bit sgmap
		 */
		actual_fibsize = sizeof(struct aac_srb) - 
			sizeof(struct sgentry) + ((srbcmd->sg.count & 0xff) * 
			 	sizeof(struct sgentry64));
		if(actual_fibsize != fibsize){ // User made a mistake - should not continue
			printk(KERN_DEBUG"aacraid: Bad Size specified in Raw SRB command\n");
			rcode = -EINVAL;
			goto cleanup;
		}
		if ((data_dir == DMA_NONE) && psg->count) { 
			printk(KERN_DEBUG"aacraid: SG with no direction specified in Raw SRB command\n");
			rcode = -EINVAL;
			goto cleanup;
		}

		for (i = 0; i < psg->count; i++) {
			dma_addr_t addr; 
			u64 le_addr;
			void* p;
			p = kmalloc(psg->sg[i].count,GFP_KERNEL|__GFP_DMA);
			if(p == 0) {
				printk(KERN_DEBUG"aacraid: Could not allocate SG buffer - size = %d buffer number %d of %d\n",
				psg->sg[i].count,i,psg->count);
				rcode = -ENOMEM;
				goto cleanup;
			}
			sg_user[i] = (void __user *)psg->sg[i].addr;
			sg_list[i] = p; // save so we can clean up later
			sg_indx = i;

			if( flags & SRB_DataOut ){
				if(copy_from_user(p,sg_user[i],psg->sg[i].count)){
					printk(KERN_DEBUG"aacraid: Could not copy sg data from user\n"); 
					rcode = -EFAULT;
					goto cleanup;
				}
			}
			addr = pci_map_single(dev->pdev, p, psg->sg[i].count, data_dir);

			le_addr = cpu_to_le64(addr);
			psg->sg[i].addr[1] = (u32)(le_addr>>32);
			psg->sg[i].addr[0] = (u32)(le_addr & 0xffffffff);
			psg->sg[i].count = cpu_to_le32(psg->sg[i].count);  
			byte_count += psg->sg[i].count;
		}

		srbcmd->count = cpu_to_le32(byte_count);
		status = fib_send(ScsiPortCommand64, srbfib, actual_fibsize, FsaNormal, 1, 1,NULL,NULL);
	} else {
		struct sgmap* psg = &srbcmd->sg;
		byte_count = 0;

		actual_fibsize = sizeof (struct aac_srb) + 
			(((le32_to_cpu(srbcmd->sg.count) & 0xff) - 1) * 
			 sizeof (struct sgentry));
		if(actual_fibsize != fibsize){ // User made a mistake - should not continue
			printk(KERN_DEBUG"aacraid: Bad Size specified in Raw SRB command\n");
			rcode = -EINVAL;
			goto cleanup;
		}
		if ((data_dir == DMA_NONE) && psg->count) {
			printk(KERN_DEBUG"aacraid: SG with no direction specified in Raw SRB command\n");
			rcode = -EINVAL;
			goto cleanup;
		}
		for (i = 0; i < psg->count; i++) {
			dma_addr_t addr; 
			void* p;
			p = kmalloc(psg->sg[i].count,GFP_KERNEL);
			if(p == 0) {
				printk(KERN_DEBUG"aacraid: Could not allocate SG buffer - size = %d buffer number %d of %d\n",
				psg->sg[i].count,i,psg->count);
				rcode = -ENOMEM;
				goto cleanup;
			}
			sg_user[i] = (void __user *)(psg->sg[i].addr);
			sg_list[i] = p; // save so we can clean up later
			sg_indx = i;

			if( flags & SRB_DataOut ){
				if(copy_from_user(p,sg_user[i],psg->sg[i].count)){
					printk(KERN_DEBUG"aacraid: Could not copy sg data from user\n"); 
					rcode = -EFAULT;
					goto cleanup;
				}
			}
			addr = pci_map_single(dev->pdev, p, psg->sg[i].count, data_dir);

			psg->sg[i].addr = cpu_to_le32(addr);
			psg->sg[i].count = cpu_to_le32(psg->sg[i].count);  
			byte_count += psg->sg[i].count;
		}
		srbcmd->count = cpu_to_le32(byte_count);
		status = fib_send(ScsiPortCommand, srbfib, actual_fibsize, FsaNormal, 1, 1, NULL, NULL);
	}

	if (status != 0){
		printk(KERN_DEBUG"aacraid: Could not send raw srb fib to hba\n"); 
		rcode = -1;
		goto cleanup;
	}

	if( flags & SRB_DataIn ) {
		for(i = 0 ; i <= sg_indx; i++){
			if(copy_to_user(sg_user[i],sg_list[i],le32_to_cpu(srbcmd->sg.sg[i].count))){
				printk(KERN_DEBUG"aacraid: Could not copy sg data to user\n"); 
				rcode = -EFAULT;
				goto cleanup;

			}
		}
	}

	reply = (struct aac_srb_reply *) fib_data(srbfib);
	if(copy_to_user(user_reply,reply,sizeof(struct aac_srb_reply))){
		printk(KERN_DEBUG"aacraid: Could not copy reply to user\n"); 
		rcode = -EFAULT;
		goto cleanup;
	}

cleanup:
	for(i=0; i <= sg_indx; i++){
		kfree(sg_list[i]);
	}
	fib_complete(srbfib);
	fib_free(srbfib);

	return rcode;
}


struct aac_pci_info {
        u32 bus;
        u32 slot;
};


int aac_get_pci_info(struct aac_dev* dev, void __user *arg)
{
        struct aac_pci_info pci_info;

	pci_info.bus = dev->pdev->bus->number;
	pci_info.slot = PCI_SLOT(dev->pdev->devfn);

       if (copy_to_user(arg, &pci_info, sizeof(struct aac_pci_info))) {
		printk(KERN_DEBUG "aacraid: Could not copy pci info\n");
               return -EFAULT;
	}
        return 0;
 }
 

int aac_do_ioctl(struct aac_dev * dev, int cmd, void __user *arg)
{
	int status;
	
	/*
	 *	HBA gets first crack
	 */
	 
	status = aac_dev_ioctl(dev, cmd, arg);
	if(status != -ENOTTY)
		return status;

	switch (cmd) {
	case FSACTL_MINIPORT_REV_CHECK:
		status = check_revision(dev, arg);
		break;
	case FSACTL_SENDFIB:
		status = ioctl_send_fib(dev, arg);
		break;
	case FSACTL_OPEN_GET_ADAPTER_FIB:
		status = open_getadapter_fib(dev, arg);
		break;
	case FSACTL_GET_NEXT_ADAPTER_FIB:
		status = next_getadapter_fib(dev, arg);
		break;
	case FSACTL_CLOSE_GET_ADAPTER_FIB:
		status = close_getadapter_fib(dev, arg);
		break;
	case FSACTL_SEND_RAW_SRB:
		status = aac_send_raw_srb(dev,arg);
		break;
	case FSACTL_GET_PCI_INFO:
		status = aac_get_pci_info(dev,arg);
		break;
	default:
		status = -ENOTTY;
	  	break;	
	}
	return status;
}

