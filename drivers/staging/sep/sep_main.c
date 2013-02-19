/*
 *
 *  sep_main.c - Security Processor Driver main group of functions
 *
 *  Copyright(c) 2009-2011 Intel Corporation. All rights reserved.
 *  Contributions(c) 2009-2011 Discretix. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc., 59
 *  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 *  CONTACTS:
 *
 *  Mark Allyn		mark.a.allyn@intel.com
 *  Jayant Mangalampalli jayant.mangalampalli@intel.com
 *
 *  CHANGES:
 *
 *  2009.06.26	Initial publish
 *  2010.09.14  Upgrade to Medfield
 *  2011.01.21  Move to sep_main.c to allow for sep_crypto.c
 *  2011.02.22  Enable kernel crypto operation
 *
 *  Please note that this driver is based on information in the Discretix
 *  CryptoCell 5.2 Driver Implementation Guide; the Discretix CryptoCell 5.2
 *  Integration Intel Medfield appendix; the Discretix CryptoCell 5.2
 *  Linux Driver Integration Guide; and the Discretix CryptoCell 5.2 System
 *  Overview and Integration Guide.
 */
/* #define DEBUG */
/* #define SEP_PERF_DEBUG */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/ioctl.h>
#include <asm/current.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/pagemap.h>
#include <asm/cacheflush.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/async.h>
#include <linux/crypto.h>
#include <crypto/internal/hash.h>
#include <crypto/scatterwalk.h>
#include <crypto/sha.h>
#include <crypto/md5.h>
#include <crypto/aes.h>
#include <crypto/des.h>
#include <crypto/hash.h>

#include "sep_driver_hw_defs.h"
#include "sep_driver_config.h"
#include "sep_driver_api.h"
#include "sep_dev.h"
#include "sep_crypto.h"

#define CREATE_TRACE_POINTS
#include "sep_trace_events.h"

/*
 * Let's not spend cycles iterating over message
 * area contents if debugging not enabled
 */
#ifdef DEBUG
#define sep_dump_message(sep)	_sep_dump_message(sep)
#else
#define sep_dump_message(sep)
#endif

/**
 * Currently, there is only one SEP device per platform;
 * In event platforms in the future have more than one SEP
 * device, this will be a linked list
 */

struct sep_device *sep_dev;

/**
 * sep_queue_status_remove - Removes transaction from status queue
 * @sep: SEP device
 * @sep_queue_info: pointer to status queue
 *
 * This function will remove information about transaction from the queue.
 */
void sep_queue_status_remove(struct sep_device *sep,
				      struct sep_queue_info **queue_elem)
{
	unsigned long lck_flags;

	dev_dbg(&sep->pdev->dev, "[PID%d] sep_queue_status_remove\n",
		current->pid);

	if (!queue_elem || !(*queue_elem)) {
		dev_dbg(&sep->pdev->dev, "PID%d %s null\n",
					current->pid, __func__);
		return;
	}

	spin_lock_irqsave(&sep->sep_queue_lock, lck_flags);
	list_del(&(*queue_elem)->list);
	sep->sep_queue_num--;
	spin_unlock_irqrestore(&sep->sep_queue_lock, lck_flags);

	kfree(*queue_elem);
	*queue_elem = NULL;

	dev_dbg(&sep->pdev->dev, "[PID%d] sep_queue_status_remove return\n",
		current->pid);
	return;
}

/**
 * sep_queue_status_add - Adds transaction to status queue
 * @sep: SEP device
 * @opcode: transaction opcode
 * @size: input data size
 * @pid: pid of current process
 * @name: current process name
 * @name_len: length of name (current process)
 *
 * This function adds information about about transaction started to the status
 * queue.
 */
struct sep_queue_info *sep_queue_status_add(
						struct sep_device *sep,
						u32 opcode,
						u32 size,
						u32 pid,
						u8 *name, size_t name_len)
{
	unsigned long lck_flags;
	struct sep_queue_info *my_elem = NULL;

	my_elem = kzalloc(sizeof(struct sep_queue_info), GFP_KERNEL);

	if (!my_elem)
		return NULL;

	dev_dbg(&sep->pdev->dev, "[PID%d] kzalloc ok\n", current->pid);

	my_elem->data.opcode = opcode;
	my_elem->data.size = size;
	my_elem->data.pid = pid;

	if (name_len > TASK_COMM_LEN)
		name_len = TASK_COMM_LEN;

	memcpy(&my_elem->data.name, name, name_len);

	spin_lock_irqsave(&sep->sep_queue_lock, lck_flags);

	list_add_tail(&my_elem->list, &sep->sep_queue_status);
	sep->sep_queue_num++;

	spin_unlock_irqrestore(&sep->sep_queue_lock, lck_flags);

	return my_elem;
}

/**
 *	sep_allocate_dmatables_region - Allocates buf for the MLLI/DMA tables
 *	@sep: SEP device
 *	@dmatables_region: Destination pointer for the buffer
 *	@dma_ctx: DMA context for the transaction
 *	@table_count: Number of MLLI/DMA tables to create
 *	The buffer created will not work as-is for DMA operations,
 *	it needs to be copied over to the appropriate place in the
 *	shared area.
 */
static int sep_allocate_dmatables_region(struct sep_device *sep,
					 void **dmatables_region,
					 struct sep_dma_context *dma_ctx,
					 const u32 table_count)
{
	const size_t new_len =
		SYNCHRONIC_DMA_TABLES_AREA_SIZE_BYTES - 1;

	void *tmp_region = NULL;

	dev_dbg(&sep->pdev->dev, "[PID%d] dma_ctx = 0x%p\n",
				current->pid, dma_ctx);
	dev_dbg(&sep->pdev->dev, "[PID%d] dmatables_region = 0x%p\n",
				current->pid, dmatables_region);

	if (!dma_ctx || !dmatables_region) {
		dev_warn(&sep->pdev->dev,
			"[PID%d] dma context/region uninitialized\n",
			current->pid);
		return -EINVAL;
	}

	dev_dbg(&sep->pdev->dev, "[PID%d] newlen = 0x%08zX\n",
				current->pid, new_len);
	dev_dbg(&sep->pdev->dev, "[PID%d] oldlen = 0x%08X\n", current->pid,
				dma_ctx->dmatables_len);
	tmp_region = kzalloc(new_len + dma_ctx->dmatables_len, GFP_KERNEL);
	if (!tmp_region)
		return -ENOMEM;

	/* Were there any previous tables that need to be preserved ? */
	if (*dmatables_region) {
		memcpy(tmp_region, *dmatables_region, dma_ctx->dmatables_len);
		kfree(*dmatables_region);
		*dmatables_region = NULL;
	}

	*dmatables_region = tmp_region;

	dma_ctx->dmatables_len += new_len;

	return 0;
}

/**
 *	sep_wait_transaction - Used for synchronizing transactions
 *	@sep: SEP device
 */
int sep_wait_transaction(struct sep_device *sep)
{
	int error = 0;
	DEFINE_WAIT(wait);

	if (0 == test_and_set_bit(SEP_TRANSACTION_STARTED_LOCK_BIT,
				&sep->in_use_flags)) {
		dev_dbg(&sep->pdev->dev,
			"[PID%d] no transactions, returning\n",
				current->pid);
		goto end_function_setpid;
	}

	/*
	 * Looping needed even for exclusive waitq entries
	 * due to process wakeup latencies, previous process
	 * might have already created another transaction.
	 */
	for (;;) {
		/*
		 * Exclusive waitq entry, so that only one process is
		 * woken up from the queue at a time.
		 */
		prepare_to_wait_exclusive(&sep->event_transactions,
					  &wait,
					  TASK_INTERRUPTIBLE);
		if (0 == test_and_set_bit(SEP_TRANSACTION_STARTED_LOCK_BIT,
					  &sep->in_use_flags)) {
			dev_dbg(&sep->pdev->dev,
				"[PID%d] no transactions, breaking\n",
					current->pid);
			break;
		}
		dev_dbg(&sep->pdev->dev,
			"[PID%d] transactions ongoing, sleeping\n",
				current->pid);
		schedule();
		dev_dbg(&sep->pdev->dev, "[PID%d] woken up\n", current->pid);

		if (signal_pending(current)) {
			dev_dbg(&sep->pdev->dev, "[PID%d] received signal\n",
							current->pid);
			error = -EINTR;
			goto end_function;
		}
	}
end_function_setpid:
	/*
	 * The pid_doing_transaction indicates that this process
	 * now owns the facilities to perform a transaction with
	 * the SEP. While this process is performing a transaction,
	 * no other process who has the SEP device open can perform
	 * any transactions. This method allows more than one process
	 * to have the device open at any given time, which provides
	 * finer granularity for device utilization by multiple
	 * processes.
	 */
	/* Only one process is able to progress here at a time */
	sep->pid_doing_transaction = current->pid;

end_function:
	finish_wait(&sep->event_transactions, &wait);

	return error;
}

/**
 * sep_check_transaction_owner - Checks if current process owns transaction
 * @sep: SEP device
 */
static inline int sep_check_transaction_owner(struct sep_device *sep)
{
	dev_dbg(&sep->pdev->dev, "[PID%d] transaction pid = %d\n",
		current->pid,
		sep->pid_doing_transaction);

	if ((sep->pid_doing_transaction == 0) ||
		(current->pid != sep->pid_doing_transaction)) {
		return -EACCES;
	}

	/* We own the transaction */
	return 0;
}

#ifdef DEBUG

/**
 * sep_dump_message - dump the message that is pending
 * @sep: SEP device
 * This will only print dump if DEBUG is set; it does
 * follow kernel debug print enabling
 */
static void _sep_dump_message(struct sep_device *sep)
{
	int count;

	u32 *p = sep->shared_addr;

	for (count = 0; count < 10 * 4; count += 4)
		dev_dbg(&sep->pdev->dev,
			"[PID%d] Word %d of the message is %x\n",
				current->pid, count/4, *p++);
}

#endif

/**
 * sep_map_and_alloc_shared_area -allocate shared block
 * @sep: security processor
 * @size: size of shared area
 */
static int sep_map_and_alloc_shared_area(struct sep_device *sep)
{
	sep->shared_addr = dma_alloc_coherent(&sep->pdev->dev,
		sep->shared_size,
		&sep->shared_bus, GFP_KERNEL);

	if (!sep->shared_addr) {
		dev_dbg(&sep->pdev->dev,
			"[PID%d] shared memory dma_alloc_coherent failed\n",
				current->pid);
		return -ENOMEM;
	}
	dev_dbg(&sep->pdev->dev,
		"[PID%d] shared_addr %zx bytes @%p (bus %llx)\n",
				current->pid,
				sep->shared_size, sep->shared_addr,
				(unsigned long long)sep->shared_bus);
	return 0;
}

/**
 * sep_unmap_and_free_shared_area - free shared block
 * @sep: security processor
 */
static void sep_unmap_and_free_shared_area(struct sep_device *sep)
{
	dma_free_coherent(&sep->pdev->dev, sep->shared_size,
				sep->shared_addr, sep->shared_bus);
}

#ifdef DEBUG

/**
 * sep_shared_bus_to_virt - convert bus/virt addresses
 * @sep: pointer to struct sep_device
 * @bus_address: address to convert
 *
 * Returns virtual address inside the shared area according
 * to the bus address.
 */
static void *sep_shared_bus_to_virt(struct sep_device *sep,
						dma_addr_t bus_address)
{
	return sep->shared_addr + (bus_address - sep->shared_bus);
}

#endif

/**
 * sep_open - device open method
 * @inode: inode of SEP device
 * @filp: file handle to SEP device
 *
 * Open method for the SEP device. Called when userspace opens
 * the SEP device node.
 *
 * Returns zero on success otherwise an error code.
 */
static int sep_open(struct inode *inode, struct file *filp)
{
	struct sep_device *sep;
	struct sep_private_data *priv;

	dev_dbg(&sep_dev->pdev->dev, "[PID%d] open\n", current->pid);

	if (filp->f_flags & O_NONBLOCK)
		return -ENOTSUPP;

	/*
	 * Get the SEP device structure and use it for the
	 * private_data field in filp for other methods
	 */

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	sep = sep_dev;
	priv->device = sep;
	filp->private_data = priv;

	dev_dbg(&sep_dev->pdev->dev, "[PID%d] priv is 0x%p\n",
					current->pid, priv);

	/* Anyone can open; locking takes place at transaction level */
	return 0;
}

/**
 * sep_free_dma_table_data_handler - free DMA table
 * @sep: pointer to struct sep_device
 * @dma_ctx: dma context
 *
 * Handles the request to free DMA table for synchronic actions
 */
int sep_free_dma_table_data_handler(struct sep_device *sep,
					   struct sep_dma_context **dma_ctx)
{
	int count;
	int dcb_counter;
	/* Pointer to the current dma_resource struct */
	struct sep_dma_resource *dma;

	dev_dbg(&sep->pdev->dev,
		"[PID%d] sep_free_dma_table_data_handler\n",
			current->pid);

	if (!dma_ctx || !(*dma_ctx)) {
		/* No context or context already freed */
		dev_dbg(&sep->pdev->dev,
			"[PID%d] no DMA context or context already freed\n",
				current->pid);

		return 0;
	}

	dev_dbg(&sep->pdev->dev, "[PID%d] (*dma_ctx)->nr_dcb_creat 0x%x\n",
					current->pid,
					(*dma_ctx)->nr_dcb_creat);

	for (dcb_counter = 0;
	     dcb_counter < (*dma_ctx)->nr_dcb_creat; dcb_counter++) {
		dma = &(*dma_ctx)->dma_res_arr[dcb_counter];

		/* Unmap and free input map array */
		if (dma->in_map_array) {
			for (count = 0; count < dma->in_num_pages; count++) {
				dma_unmap_page(&sep->pdev->dev,
					dma->in_map_array[count].dma_addr,
					dma->in_map_array[count].size,
					DMA_TO_DEVICE);
			}
			kfree(dma->in_map_array);
		}

		/**
		 * Output is handled different. If
		 * this was a secure dma into restricted memory,
		 * then we skip this step altogether as restricted
		 * memory is not available to the o/s at all.
		 */
		if (((*dma_ctx)->secure_dma == false) &&
			(dma->out_map_array)) {

			for (count = 0; count < dma->out_num_pages; count++) {
				dma_unmap_page(&sep->pdev->dev,
					dma->out_map_array[count].dma_addr,
					dma->out_map_array[count].size,
					DMA_FROM_DEVICE);
			}
			kfree(dma->out_map_array);
		}

		/* Free page cache for output */
		if (dma->in_page_array) {
			for (count = 0; count < dma->in_num_pages; count++) {
				flush_dcache_page(dma->in_page_array[count]);
				page_cache_release(dma->in_page_array[count]);
			}
			kfree(dma->in_page_array);
		}

		/* Again, we do this only for non secure dma */
		if (((*dma_ctx)->secure_dma == false) &&
			(dma->out_page_array)) {

			for (count = 0; count < dma->out_num_pages; count++) {
				if (!PageReserved(dma->out_page_array[count]))

					SetPageDirty(dma->
					out_page_array[count]);

				flush_dcache_page(dma->out_page_array[count]);
				page_cache_release(dma->out_page_array[count]);
			}
			kfree(dma->out_page_array);
		}

		/**
		 * Note that here we use in_map_num_entries because we
		 * don't have a page array; the page array is generated
		 * only in the lock_user_pages, which is not called
		 * for kernel crypto, which is what the sg (scatter gather
		 * is used for exclusively)
		 */
		if (dma->src_sg) {
			dma_unmap_sg(&sep->pdev->dev, dma->src_sg,
				dma->in_map_num_entries, DMA_TO_DEVICE);
			dma->src_sg = NULL;
		}

		if (dma->dst_sg) {
			dma_unmap_sg(&sep->pdev->dev, dma->dst_sg,
				dma->in_map_num_entries, DMA_FROM_DEVICE);
			dma->dst_sg = NULL;
		}

		/* Reset all the values */
		dma->in_page_array = NULL;
		dma->out_page_array = NULL;
		dma->in_num_pages = 0;
		dma->out_num_pages = 0;
		dma->in_map_array = NULL;
		dma->out_map_array = NULL;
		dma->in_map_num_entries = 0;
		dma->out_map_num_entries = 0;
	}

	(*dma_ctx)->nr_dcb_creat = 0;
	(*dma_ctx)->num_lli_tables_created = 0;

	kfree(*dma_ctx);
	*dma_ctx = NULL;

	dev_dbg(&sep->pdev->dev,
		"[PID%d] sep_free_dma_table_data_handler end\n",
			current->pid);

	return 0;
}

/**
 * sep_end_transaction_handler - end transaction
 * @sep: pointer to struct sep_device
 * @dma_ctx: DMA context
 * @call_status: Call status
 *
 * This API handles the end transaction request.
 */
static int sep_end_transaction_handler(struct sep_device *sep,
				       struct sep_dma_context **dma_ctx,
				       struct sep_call_status *call_status,
				       struct sep_queue_info **my_queue_elem)
{
	dev_dbg(&sep->pdev->dev, "[PID%d] ending transaction\n", current->pid);

	/*
	 * Extraneous transaction clearing would mess up PM
	 * device usage counters and SEP would get suspended
	 * just before we send a command to SEP in the next
	 * transaction
	 * */
	if (sep_check_transaction_owner(sep)) {
		dev_dbg(&sep->pdev->dev, "[PID%d] not transaction owner\n",
						current->pid);
		return 0;
	}

	/* Update queue status */
	sep_queue_status_remove(sep, my_queue_elem);

	/* Check that all the DMA resources were freed */
	if (dma_ctx)
		sep_free_dma_table_data_handler(sep, dma_ctx);

	/* Reset call status for next transaction */
	if (call_status)
		call_status->status = 0;

	/* Clear the message area to avoid next transaction reading
	 * sensitive results from previous transaction */
	memset(sep->shared_addr, 0,
	       SEP_DRIVER_MESSAGE_SHARED_AREA_SIZE_IN_BYTES);

	/* start suspend delay */
#ifdef SEP_ENABLE_RUNTIME_PM
	if (sep->in_use) {
		sep->in_use = 0;
		pm_runtime_mark_last_busy(&sep->pdev->dev);
		pm_runtime_put_autosuspend(&sep->pdev->dev);
	}
#endif

	clear_bit(SEP_WORKING_LOCK_BIT, &sep->in_use_flags);
	sep->pid_doing_transaction = 0;

	/* Now it's safe for next process to proceed */
	dev_dbg(&sep->pdev->dev, "[PID%d] waking up next transaction\n",
					current->pid);
	clear_bit(SEP_TRANSACTION_STARTED_LOCK_BIT, &sep->in_use_flags);
	wake_up(&sep->event_transactions);

	return 0;
}


/**
 * sep_release - close a SEP device
 * @inode: inode of SEP device
 * @filp: file handle being closed
 *
 * Called on the final close of a SEP device.
 */
static int sep_release(struct inode *inode, struct file *filp)
{
	struct sep_private_data * const private_data = filp->private_data;
	struct sep_call_status *call_status = &private_data->call_status;
	struct sep_device *sep = private_data->device;
	struct sep_dma_context **dma_ctx = &private_data->dma_ctx;
	struct sep_queue_info **my_queue_elem = &private_data->my_queue_elem;

	dev_dbg(&sep->pdev->dev, "[PID%d] release\n", current->pid);

	sep_end_transaction_handler(sep, dma_ctx, call_status,
		my_queue_elem);

	kfree(filp->private_data);

	return 0;
}

/**
 * sep_mmap -  maps the shared area to user space
 * @filp: pointer to struct file
 * @vma: pointer to vm_area_struct
 *
 * Called on an mmap of our space via the normal SEP device
 */
static int sep_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct sep_private_data * const private_data = filp->private_data;
	struct sep_call_status *call_status = &private_data->call_status;
	struct sep_device *sep = private_data->device;
	struct sep_queue_info **my_queue_elem = &private_data->my_queue_elem;
	dma_addr_t bus_addr;
	unsigned long error = 0;

	dev_dbg(&sep->pdev->dev, "[PID%d] sep_mmap\n", current->pid);

	/* Set the transaction busy (own the device) */
	/*
	 * Problem for multithreaded applications is that here we're
	 * possibly going to sleep while holding a write lock on
	 * current->mm->mmap_sem, which will cause deadlock for ongoing
	 * transaction trying to create DMA tables
	 */
	error = sep_wait_transaction(sep);
	if (error)
		/* Interrupted by signal, don't clear transaction */
		goto end_function;

	/* Clear the message area to avoid next transaction reading
	 * sensitive results from previous transaction */
	memset(sep->shared_addr, 0,
	       SEP_DRIVER_MESSAGE_SHARED_AREA_SIZE_IN_BYTES);

	/*
	 * Check that the size of the mapped range is as the size of the message
	 * shared area
	 */
	if ((vma->vm_end - vma->vm_start) > SEP_DRIVER_MMMAP_AREA_SIZE) {
		error = -EINVAL;
		goto end_function_with_error;
	}

	dev_dbg(&sep->pdev->dev, "[PID%d] shared_addr is %p\n",
					current->pid, sep->shared_addr);

	/* Get bus address */
	bus_addr = sep->shared_bus;

	if (remap_pfn_range(vma, vma->vm_start, bus_addr >> PAGE_SHIFT,
		vma->vm_end - vma->vm_start, vma->vm_page_prot)) {
		dev_dbg(&sep->pdev->dev, "[PID%d] remap_pfn_range failed\n",
						current->pid);
		error = -EAGAIN;
		goto end_function_with_error;
	}

	/* Update call status */
	set_bit(SEP_LEGACY_MMAP_DONE_OFFSET, &call_status->status);

	goto end_function;

end_function_with_error:
	/* Clear our transaction */
	sep_end_transaction_handler(sep, NULL, call_status,
		my_queue_elem);

end_function:
	return error;
}

/**
 * sep_poll - poll handler
 * @filp:	pointer to struct file
 * @wait:	pointer to poll_table
 *
 * Called by the OS when the kernel is asked to do a poll on
 * a SEP file handle.
 */
static unsigned int sep_poll(struct file *filp, poll_table *wait)
{
	struct sep_private_data * const private_data = filp->private_data;
	struct sep_call_status *call_status = &private_data->call_status;
	struct sep_device *sep = private_data->device;
	u32 mask = 0;
	u32 retval = 0;
	u32 retval2 = 0;
	unsigned long lock_irq_flag;

	/* Am I the process that owns the transaction? */
	if (sep_check_transaction_owner(sep)) {
		dev_dbg(&sep->pdev->dev, "[PID%d] poll pid not owner\n",
						current->pid);
		mask = POLLERR;
		goto end_function;
	}

	/* Check if send command or send_reply were activated previously */
	if (0 == test_bit(SEP_LEGACY_SENDMSG_DONE_OFFSET,
			  &call_status->status)) {
		dev_warn(&sep->pdev->dev, "[PID%d] sendmsg not called\n",
						current->pid);
		mask = POLLERR;
		goto end_function;
	}


	/* Add the event to the polling wait table */
	dev_dbg(&sep->pdev->dev, "[PID%d] poll: calling wait sep_event\n",
					current->pid);

	poll_wait(filp, &sep->event_interrupt, wait);

	dev_dbg(&sep->pdev->dev,
		"[PID%d] poll: send_ct is %lx reply ct is %lx\n",
			current->pid, sep->send_ct, sep->reply_ct);

	/* Check if error occurred during poll */
	retval2 = sep_read_reg(sep, HW_HOST_SEP_HOST_GPR3_REG_ADDR);
	if ((retval2 != 0x0) && (retval2 != 0x8)) {
		dev_dbg(&sep->pdev->dev, "[PID%d] poll; poll error %x\n",
						current->pid, retval2);
		mask |= POLLERR;
		goto end_function;
	}

	spin_lock_irqsave(&sep->snd_rply_lck, lock_irq_flag);

	if (sep->send_ct == sep->reply_ct) {
		spin_unlock_irqrestore(&sep->snd_rply_lck, lock_irq_flag);
		retval = sep_read_reg(sep, HW_HOST_SEP_HOST_GPR2_REG_ADDR);
		dev_dbg(&sep->pdev->dev,
			"[PID%d] poll: data ready check (GPR2)  %x\n",
				current->pid, retval);

		/* Check if printf request  */
		if ((retval >> 30) & 0x1) {
			dev_dbg(&sep->pdev->dev,
				"[PID%d] poll: SEP printf request\n",
					current->pid);
			goto end_function;
		}

		/* Check if the this is SEP reply or request */
		if (retval >> 31) {
			dev_dbg(&sep->pdev->dev,
				"[PID%d] poll: SEP request\n",
					current->pid);
		} else {
			dev_dbg(&sep->pdev->dev,
				"[PID%d] poll: normal return\n",
					current->pid);
			sep_dump_message(sep);
			dev_dbg(&sep->pdev->dev,
				"[PID%d] poll; SEP reply POLLIN|POLLRDNORM\n",
					current->pid);
			mask |= POLLIN | POLLRDNORM;
		}
		set_bit(SEP_LEGACY_POLL_DONE_OFFSET, &call_status->status);
	} else {
		spin_unlock_irqrestore(&sep->snd_rply_lck, lock_irq_flag);
		dev_dbg(&sep->pdev->dev,
			"[PID%d] poll; no reply; returning mask of 0\n",
				current->pid);
		mask = 0;
	}

end_function:
	return mask;
}

/**
 * sep_time_address - address in SEP memory of time
 * @sep: SEP device we want the address from
 *
 * Return the address of the two dwords in memory used for time
 * setting.
 */
static u32 *sep_time_address(struct sep_device *sep)
{
	return sep->shared_addr +
		SEP_DRIVER_SYSTEM_TIME_MEMORY_OFFSET_IN_BYTES;
}

/**
 * sep_set_time - set the SEP time
 * @sep: the SEP we are setting the time for
 *
 * Calculates time and sets it at the predefined address.
 * Called with the SEP mutex held.
 */
static unsigned long sep_set_time(struct sep_device *sep)
{
	struct timeval time;
	u32 *time_addr;	/* Address of time as seen by the kernel */


	do_gettimeofday(&time);

	/* Set value in the SYSTEM MEMORY offset */
	time_addr = sep_time_address(sep);

	time_addr[0] = SEP_TIME_VAL_TOKEN;
	time_addr[1] = time.tv_sec;

	dev_dbg(&sep->pdev->dev, "[PID%d] time.tv_sec is %lu\n",
					current->pid, time.tv_sec);
	dev_dbg(&sep->pdev->dev, "[PID%d] time_addr is %p\n",
					current->pid, time_addr);
	dev_dbg(&sep->pdev->dev, "[PID%d] sep->shared_addr is %p\n",
					current->pid, sep->shared_addr);

	return time.tv_sec;
}

/**
 * sep_send_command_handler - kick off a command
 * @sep: SEP being signalled
 *
 * This function raises interrupt to SEP that signals that is has a new
 * command from the host
 *
 * Note that this function does fall under the ioctl lock
 */
int sep_send_command_handler(struct sep_device *sep)
{
	unsigned long lock_irq_flag;
	u32 *msg_pool;
	int error = 0;

	/* Basic sanity check; set msg pool to start of shared area */
	msg_pool = (u32 *)sep->shared_addr;
	msg_pool += 2;

	/* Look for start msg token */
	if (*msg_pool != SEP_START_MSG_TOKEN) {
		dev_warn(&sep->pdev->dev, "start message token not present\n");
		error = -EPROTO;
		goto end_function;
	}

	/* Do we have a reasonable size? */
	msg_pool += 1;
	if ((*msg_pool < 2) ||
		(*msg_pool > SEP_DRIVER_MAX_MESSAGE_SIZE_IN_BYTES)) {

		dev_warn(&sep->pdev->dev, "invalid message size\n");
		error = -EPROTO;
		goto end_function;
	}

	/* Does the command look reasonable? */
	msg_pool += 1;
	if (*msg_pool < 2) {
		dev_warn(&sep->pdev->dev, "invalid message opcode\n");
		error = -EPROTO;
		goto end_function;
	}

#if defined(CONFIG_PM_RUNTIME) && defined(SEP_ENABLE_RUNTIME_PM)
	dev_dbg(&sep->pdev->dev, "[PID%d] before pm sync status 0x%X\n",
					current->pid,
					sep->pdev->dev.power.runtime_status);
	sep->in_use = 1; /* device is about to be used */
	pm_runtime_get_sync(&sep->pdev->dev);
#endif

	if (test_and_set_bit(SEP_WORKING_LOCK_BIT, &sep->in_use_flags)) {
		error = -EPROTO;
		goto end_function;
	}
	sep->in_use = 1; /* device is about to be used */
	sep_set_time(sep);

	sep_dump_message(sep);

	/* Update counter */
	spin_lock_irqsave(&sep->snd_rply_lck, lock_irq_flag);
	sep->send_ct++;
	spin_unlock_irqrestore(&sep->snd_rply_lck, lock_irq_flag);

	dev_dbg(&sep->pdev->dev,
		"[PID%d] sep_send_command_handler send_ct %lx reply_ct %lx\n",
			current->pid, sep->send_ct, sep->reply_ct);

	/* Send interrupt to SEP */
	sep_write_reg(sep, HW_HOST_HOST_SEP_GPR0_REG_ADDR, 0x2);

end_function:
	return error;
}

/**
 *	sep_crypto_dma -
 *	@sep: pointer to struct sep_device
 *	@sg: pointer to struct scatterlist
 *	@direction:
 *	@dma_maps: pointer to place a pointer to array of dma maps
 *	 This is filled in; anything previous there will be lost
 *	 The structure for dma maps is sep_dma_map
 *	@returns number of dma maps on success; negative on error
 *
 *	This creates the dma table from the scatterlist
 *	It is used only for kernel crypto as it works with scatterlists
 *	representation of data buffers
 *
 */
static int sep_crypto_dma(
	struct sep_device *sep,
	struct scatterlist *sg,
	struct sep_dma_map **dma_maps,
	enum dma_data_direction direction)
{
	struct scatterlist *temp_sg;

	u32 count_segment;
	u32 count_mapped;
	struct sep_dma_map *sep_dma;
	int ct1;

	if (sg->length == 0)
		return 0;

	/* Count the segments */
	temp_sg = sg;
	count_segment = 0;
	while (temp_sg) {
		count_segment += 1;
		temp_sg = scatterwalk_sg_next(temp_sg);
	}
	dev_dbg(&sep->pdev->dev,
		"There are (hex) %x segments in sg\n", count_segment);

	/* DMA map segments */
	count_mapped = dma_map_sg(&sep->pdev->dev, sg,
		count_segment, direction);

	dev_dbg(&sep->pdev->dev,
		"There are (hex) %x maps in sg\n", count_mapped);

	if (count_mapped == 0) {
		dev_dbg(&sep->pdev->dev, "Cannot dma_map_sg\n");
		return -ENOMEM;
	}

	sep_dma = kmalloc(sizeof(struct sep_dma_map) *
		count_mapped, GFP_ATOMIC);

	if (sep_dma == NULL) {
		dev_dbg(&sep->pdev->dev, "Cannot allocate dma_maps\n");
		return -ENOMEM;
	}

	for_each_sg(sg, temp_sg, count_mapped, ct1) {
		sep_dma[ct1].dma_addr = sg_dma_address(temp_sg);
		sep_dma[ct1].size = sg_dma_len(temp_sg);
		dev_dbg(&sep->pdev->dev, "(all hex) map %x dma %lx len %lx\n",
			ct1, (unsigned long)sep_dma[ct1].dma_addr,
			(unsigned long)sep_dma[ct1].size);
		}

	*dma_maps = sep_dma;
	return count_mapped;

}

/**
 *	sep_crypto_lli -
 *	@sep: pointer to struct sep_device
 *	@sg: pointer to struct scatterlist
 *	@data_size: total data size
 *	@direction:
 *	@dma_maps: pointer to place a pointer to array of dma maps
 *	 This is filled in; anything previous there will be lost
 *	 The structure for dma maps is sep_dma_map
 *	@lli_maps: pointer to place a pointer to array of lli maps
 *	 This is filled in; anything previous there will be lost
 *	 The structure for dma maps is sep_dma_map
 *	@returns number of dma maps on success; negative on error
 *
 *	This creates the LLI table from the scatterlist
 *	It is only used for kernel crypto as it works exclusively
 *	with scatterlists (struct scatterlist) representation of
 *	data buffers
 */
static int sep_crypto_lli(
	struct sep_device *sep,
	struct scatterlist *sg,
	struct sep_dma_map **maps,
	struct sep_lli_entry **llis,
	u32 data_size,
	enum dma_data_direction direction)
{

	int ct1;
	struct sep_lli_entry *sep_lli;
	struct sep_dma_map *sep_map;

	int nbr_ents;

	nbr_ents = sep_crypto_dma(sep, sg, maps, direction);
	if (nbr_ents <= 0) {
		dev_dbg(&sep->pdev->dev, "crypto_dma failed %x\n",
			nbr_ents);
		return nbr_ents;
	}

	sep_map = *maps;

	sep_lli = kmalloc(sizeof(struct sep_lli_entry) * nbr_ents, GFP_ATOMIC);

	if (sep_lli == NULL) {
		dev_dbg(&sep->pdev->dev, "Cannot allocate lli_maps\n");

		kfree(*maps);
		*maps = NULL;
		return -ENOMEM;
	}

	for (ct1 = 0; ct1 < nbr_ents; ct1 += 1) {
		sep_lli[ct1].bus_address = (u32)sep_map[ct1].dma_addr;

		/* Maximum for page is total data size */
		if (sep_map[ct1].size > data_size)
			sep_map[ct1].size = data_size;

		sep_lli[ct1].block_size = (u32)sep_map[ct1].size;
	}

	*llis = sep_lli;
	return nbr_ents;
}

/**
 *	sep_lock_kernel_pages - map kernel pages for DMA
 *	@sep: pointer to struct sep_device
 *	@kernel_virt_addr: address of data buffer in kernel
 *	@data_size: size of data
 *	@lli_array_ptr: lli array
 *	@in_out_flag: input into device or output from device
 *
 *	This function locks all the physical pages of the kernel virtual buffer
 *	and construct a basic lli  array, where each entry holds the physical
 *	page address and the size that application data holds in this page
 *	This function is used only during kernel crypto mod calls from within
 *	the kernel (when ioctl is not used)
 *
 *	This is used only for kernel crypto. Kernel pages
 *	are handled differently as they are done via
 *	scatter gather lists (struct scatterlist)
 */
static int sep_lock_kernel_pages(struct sep_device *sep,
	unsigned long kernel_virt_addr,
	u32 data_size,
	struct sep_lli_entry **lli_array_ptr,
	int in_out_flag,
	struct sep_dma_context *dma_ctx)

{
	u32 num_pages;
	struct scatterlist *sg;

	/* Array of lli */
	struct sep_lli_entry *lli_array;
	/* Map array */
	struct sep_dma_map *map_array;

	enum dma_data_direction direction;

	lli_array = NULL;
	map_array = NULL;

	if (in_out_flag == SEP_DRIVER_IN_FLAG) {
		direction = DMA_TO_DEVICE;
		sg = dma_ctx->src_sg;
	} else {
		direction = DMA_FROM_DEVICE;
		sg = dma_ctx->dst_sg;
	}

	num_pages = sep_crypto_lli(sep, sg, &map_array, &lli_array,
		data_size, direction);

	if (num_pages <= 0) {
		dev_dbg(&sep->pdev->dev, "sep_crypto_lli returned error %x\n",
			num_pages);
		return -ENOMEM;
	}

	/* Put mapped kernel sg into kernel resource array */

	/* Set output params according to the in_out flag */
	if (in_out_flag == SEP_DRIVER_IN_FLAG) {
		*lli_array_ptr = lli_array;
		dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].in_num_pages =
								num_pages;
		dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].in_page_array =
								NULL;
		dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].in_map_array =
								map_array;
		dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].in_map_num_entries =
								num_pages;
		dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].src_sg =
			dma_ctx->src_sg;
	} else {
		*lli_array_ptr = lli_array;
		dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].out_num_pages =
								num_pages;
		dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].out_page_array =
								NULL;
		dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].out_map_array =
								map_array;
		dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].
					out_map_num_entries = num_pages;
		dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].dst_sg =
			dma_ctx->dst_sg;
	}

	return 0;
}

/**
 * sep_lock_user_pages - lock and map user pages for DMA
 * @sep: pointer to struct sep_device
 * @app_virt_addr: user memory data buffer
 * @data_size: size of data buffer
 * @lli_array_ptr: lli array
 * @in_out_flag: input or output to device
 *
 * This function locks all the physical pages of the application
 * virtual buffer and construct a basic lli  array, where each entry
 * holds the physical page address and the size that application
 * data holds in this physical pages
 */
static int sep_lock_user_pages(struct sep_device *sep,
	u32 app_virt_addr,
	u32 data_size,
	struct sep_lli_entry **lli_array_ptr,
	int in_out_flag,
	struct sep_dma_context *dma_ctx)

{
	int error = 0;
	u32 count;
	int result;
	/* The the page of the end address of the user space buffer */
	u32 end_page;
	/* The page of the start address of the user space buffer */
	u32 start_page;
	/* The range in pages */
	u32 num_pages;
	/* Array of pointers to page */
	struct page **page_array;
	/* Array of lli */
	struct sep_lli_entry *lli_array;
	/* Map array */
	struct sep_dma_map *map_array;

	/* Set start and end pages and num pages */
	end_page = (app_virt_addr + data_size - 1) >> PAGE_SHIFT;
	start_page = app_virt_addr >> PAGE_SHIFT;
	num_pages = end_page - start_page + 1;

	dev_dbg(&sep->pdev->dev,
		"[PID%d] lock user pages app_virt_addr is %x\n",
			current->pid, app_virt_addr);

	dev_dbg(&sep->pdev->dev, "[PID%d] data_size is (hex) %x\n",
					current->pid, data_size);
	dev_dbg(&sep->pdev->dev, "[PID%d] start_page is (hex) %x\n",
					current->pid, start_page);
	dev_dbg(&sep->pdev->dev, "[PID%d] end_page is (hex) %x\n",
					current->pid, end_page);
	dev_dbg(&sep->pdev->dev, "[PID%d] num_pages is (hex) %x\n",
					current->pid, num_pages);

	/* Allocate array of pages structure pointers */
	page_array = kmalloc_array(num_pages, sizeof(struct page *),
				   GFP_ATOMIC);
	if (!page_array) {
		error = -ENOMEM;
		goto end_function;
	}

	map_array = kmalloc_array(num_pages, sizeof(struct sep_dma_map),
				  GFP_ATOMIC);
	if (!map_array) {
		error = -ENOMEM;
		goto end_function_with_error1;
	}

	lli_array = kmalloc_array(num_pages, sizeof(struct sep_lli_entry),
				  GFP_ATOMIC);
	if (!lli_array) {
		error = -ENOMEM;
		goto end_function_with_error2;
	}

	/* Convert the application virtual address into a set of physical */
	down_read(&current->mm->mmap_sem);
	result = get_user_pages(current, current->mm, app_virt_addr,
		num_pages,
		((in_out_flag == SEP_DRIVER_IN_FLAG) ? 0 : 1),
		0, page_array, NULL);

	up_read(&current->mm->mmap_sem);

	/* Check the number of pages locked - if not all then exit with error */
	if (result != num_pages) {
		dev_warn(&sep->pdev->dev,
			"[PID%d] not all pages locked by get_user_pages, "
			"result 0x%X, num_pages 0x%X\n",
				current->pid, result, num_pages);
		error = -ENOMEM;
		goto end_function_with_error3;
	}

	dev_dbg(&sep->pdev->dev, "[PID%d] get_user_pages succeeded\n",
					current->pid);

	/*
	 * Fill the array using page array data and
	 * map the pages - this action will also flush the cache as needed
	 */
	for (count = 0; count < num_pages; count++) {
		/* Fill the map array */
		map_array[count].dma_addr =
			dma_map_page(&sep->pdev->dev, page_array[count],
			0, PAGE_SIZE, DMA_BIDIRECTIONAL);

		map_array[count].size = PAGE_SIZE;

		/* Fill the lli array entry */
		lli_array[count].bus_address = (u32)map_array[count].dma_addr;
		lli_array[count].block_size = PAGE_SIZE;

		dev_dbg(&sep->pdev->dev,
			"[PID%d] lli_array[%x].bus_address is %08lx, "
			"lli_array[%x].block_size is (hex) %x\n", current->pid,
			count, (unsigned long)lli_array[count].bus_address,
			count, lli_array[count].block_size);
	}

	/* Check the offset for the first page */
	lli_array[0].bus_address =
		lli_array[0].bus_address + (app_virt_addr & (~PAGE_MASK));

	/* Check that not all the data is in the first page only */
	if ((PAGE_SIZE - (app_virt_addr & (~PAGE_MASK))) >= data_size)
		lli_array[0].block_size = data_size;
	else
		lli_array[0].block_size =
			PAGE_SIZE - (app_virt_addr & (~PAGE_MASK));

		dev_dbg(&sep->pdev->dev,
			"[PID%d] After check if page 0 has all data\n",
			current->pid);
		dev_dbg(&sep->pdev->dev,
			"[PID%d] lli_array[0].bus_address is (hex) %08lx, "
			"lli_array[0].block_size is (hex) %x\n",
			current->pid,
			(unsigned long)lli_array[0].bus_address,
			lli_array[0].block_size);


	/* Check the size of the last page */
	if (num_pages > 1) {
		lli_array[num_pages - 1].block_size =
			(app_virt_addr + data_size) & (~PAGE_MASK);
		if (lli_array[num_pages - 1].block_size == 0)
			lli_array[num_pages - 1].block_size = PAGE_SIZE;

		dev_dbg(&sep->pdev->dev,
			"[PID%d] After last page size adjustment\n",
			current->pid);
		dev_dbg(&sep->pdev->dev,
			"[PID%d] lli_array[%x].bus_address is (hex) %08lx, "
			"lli_array[%x].block_size is (hex) %x\n",
			current->pid,
			num_pages - 1,
			(unsigned long)lli_array[num_pages - 1].bus_address,
			num_pages - 1,
			lli_array[num_pages - 1].block_size);
	}

	/* Set output params according to the in_out flag */
	if (in_out_flag == SEP_DRIVER_IN_FLAG) {
		*lli_array_ptr = lli_array;
		dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].in_num_pages =
								num_pages;
		dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].in_page_array =
								page_array;
		dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].in_map_array =
								map_array;
		dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].in_map_num_entries =
								num_pages;
		dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].src_sg = NULL;
	} else {
		*lli_array_ptr = lli_array;
		dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].out_num_pages =
								num_pages;
		dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].out_page_array =
								page_array;
		dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].out_map_array =
								map_array;
		dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].
					out_map_num_entries = num_pages;
		dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].dst_sg = NULL;
	}
	goto end_function;

end_function_with_error3:
	/* Free lli array */
	kfree(lli_array);

end_function_with_error2:
	kfree(map_array);

end_function_with_error1:
	/* Free page array */
	kfree(page_array);

end_function:
	return error;
}

/**
 *	sep_lli_table_secure_dma - get lli array for IMR addresses
 *	@sep: pointer to struct sep_device
 *	@app_virt_addr: user memory data buffer
 *	@data_size: size of data buffer
 *	@lli_array_ptr: lli array
 *	@in_out_flag: not used
 *	@dma_ctx: pointer to struct sep_dma_context
 *
 *	This function creates lli tables for outputting data to
 *	IMR memory, which is memory that cannot be accessed by the
 *	the x86 processor.
 */
static int sep_lli_table_secure_dma(struct sep_device *sep,
	u32 app_virt_addr,
	u32 data_size,
	struct sep_lli_entry **lli_array_ptr,
	int in_out_flag,
	struct sep_dma_context *dma_ctx)

{
	int error = 0;
	u32 count;
	/* The the page of the end address of the user space buffer */
	u32 end_page;
	/* The page of the start address of the user space buffer */
	u32 start_page;
	/* The range in pages */
	u32 num_pages;
	/* Array of lli */
	struct sep_lli_entry *lli_array;

	/* Set start and end pages and num pages */
	end_page = (app_virt_addr + data_size - 1) >> PAGE_SHIFT;
	start_page = app_virt_addr >> PAGE_SHIFT;
	num_pages = end_page - start_page + 1;

	dev_dbg(&sep->pdev->dev,
		"[PID%d] lock user pages  app_virt_addr is %x\n",
		current->pid, app_virt_addr);

	dev_dbg(&sep->pdev->dev, "[PID%d] data_size is (hex) %x\n",
		current->pid, data_size);
	dev_dbg(&sep->pdev->dev, "[PID%d] start_page is (hex) %x\n",
		current->pid, start_page);
	dev_dbg(&sep->pdev->dev, "[PID%d] end_page is (hex) %x\n",
		current->pid, end_page);
	dev_dbg(&sep->pdev->dev, "[PID%d] num_pages is (hex) %x\n",
		current->pid, num_pages);

	lli_array = kmalloc_array(num_pages, sizeof(struct sep_lli_entry),
				  GFP_ATOMIC);
	if (!lli_array)
		return -ENOMEM;

	/*
	 * Fill the lli_array
	 */
	start_page = start_page << PAGE_SHIFT;
	for (count = 0; count < num_pages; count++) {
		/* Fill the lli array entry */
		lli_array[count].bus_address = start_page;
		lli_array[count].block_size = PAGE_SIZE;

		start_page += PAGE_SIZE;

		dev_dbg(&sep->pdev->dev,
			"[PID%d] lli_array[%x].bus_address is %08lx, "
			"lli_array[%x].block_size is (hex) %x\n",
			current->pid,
			count, (unsigned long)lli_array[count].bus_address,
			count, lli_array[count].block_size);
	}

	/* Check the offset for the first page */
	lli_array[0].bus_address =
		lli_array[0].bus_address + (app_virt_addr & (~PAGE_MASK));

	/* Check that not all the data is in the first page only */
	if ((PAGE_SIZE - (app_virt_addr & (~PAGE_MASK))) >= data_size)
		lli_array[0].block_size = data_size;
	else
		lli_array[0].block_size =
			PAGE_SIZE - (app_virt_addr & (~PAGE_MASK));

	dev_dbg(&sep->pdev->dev,
		"[PID%d] After check if page 0 has all data\n"
		"lli_array[0].bus_address is (hex) %08lx, "
		"lli_array[0].block_size is (hex) %x\n",
		current->pid,
		(unsigned long)lli_array[0].bus_address,
		lli_array[0].block_size);

	/* Check the size of the last page */
	if (num_pages > 1) {
		lli_array[num_pages - 1].block_size =
			(app_virt_addr + data_size) & (~PAGE_MASK);
		if (lli_array[num_pages - 1].block_size == 0)
			lli_array[num_pages - 1].block_size = PAGE_SIZE;

		dev_dbg(&sep->pdev->dev,
			"[PID%d] After last page size adjustment\n"
			"lli_array[%x].bus_address is (hex) %08lx, "
			"lli_array[%x].block_size is (hex) %x\n",
			current->pid, num_pages - 1,
			(unsigned long)lli_array[num_pages - 1].bus_address,
			num_pages - 1,
			lli_array[num_pages - 1].block_size);
	}
	*lli_array_ptr = lli_array;
	dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].out_num_pages = num_pages;
	dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].out_page_array = NULL;
	dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].out_map_array = NULL;
	dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].out_map_num_entries = 0;

	return error;
}

/**
 * sep_calculate_lli_table_max_size - size the LLI table
 * @sep: pointer to struct sep_device
 * @lli_in_array_ptr
 * @num_array_entries
 * @last_table_flag
 *
 * This function calculates the size of data that can be inserted into
 * the lli table from this array, such that either the table is full
 * (all entries are entered), or there are no more entries in the
 * lli array
 */
static u32 sep_calculate_lli_table_max_size(struct sep_device *sep,
	struct sep_lli_entry *lli_in_array_ptr,
	u32 num_array_entries,
	u32 *last_table_flag)
{
	u32 counter;
	/* Table data size */
	u32 table_data_size = 0;
	/* Data size for the next table */
	u32 next_table_data_size;

	*last_table_flag = 0;

	/*
	 * Calculate the data in the out lli table till we fill the whole
	 * table or till the data has ended
	 */
	for (counter = 0;
		(counter < (SEP_DRIVER_ENTRIES_PER_TABLE_IN_SEP - 1)) &&
			(counter < num_array_entries); counter++)
		table_data_size += lli_in_array_ptr[counter].block_size;

	/*
	 * Check if we reached the last entry,
	 * meaning this ia the last table to build,
	 * and no need to check the block alignment
	 */
	if (counter == num_array_entries) {
		/* Set the last table flag */
		*last_table_flag = 1;
		goto end_function;
	}

	/*
	 * Calculate the data size of the next table.
	 * Stop if no entries left or if data size is more the DMA restriction
	 */
	next_table_data_size = 0;
	for (; counter < num_array_entries; counter++) {
		next_table_data_size += lli_in_array_ptr[counter].block_size;
		if (next_table_data_size >= SEP_DRIVER_MIN_DATA_SIZE_PER_TABLE)
			break;
	}

	/*
	 * Check if the next table data size is less then DMA rstriction.
	 * if it is - recalculate the current table size, so that the next
	 * table data size will be adaquete for DMA
	 */
	if (next_table_data_size &&
		next_table_data_size < SEP_DRIVER_MIN_DATA_SIZE_PER_TABLE)

		table_data_size -= (SEP_DRIVER_MIN_DATA_SIZE_PER_TABLE -
			next_table_data_size);

end_function:
	return table_data_size;
}

/**
 * sep_build_lli_table - build an lli array for the given table
 * @sep: pointer to struct sep_device
 * @lli_array_ptr: pointer to lli array
 * @lli_table_ptr: pointer to lli table
 * @num_processed_entries_ptr: pointer to number of entries
 * @num_table_entries_ptr: pointer to number of tables
 * @table_data_size: total data size
 *
 * Builds an lli table from the lli_array according to
 * the given size of data
 */
static void sep_build_lli_table(struct sep_device *sep,
	struct sep_lli_entry	*lli_array_ptr,
	struct sep_lli_entry	*lli_table_ptr,
	u32 *num_processed_entries_ptr,
	u32 *num_table_entries_ptr,
	u32 table_data_size)
{
	/* Current table data size */
	u32 curr_table_data_size;
	/* Counter of lli array entry */
	u32 array_counter;

	/* Init current table data size and lli array entry counter */
	curr_table_data_size = 0;
	array_counter = 0;
	*num_table_entries_ptr = 1;

	dev_dbg(&sep->pdev->dev,
		"[PID%d] build lli table table_data_size: (hex) %x\n",
			current->pid, table_data_size);

	/* Fill the table till table size reaches the needed amount */
	while (curr_table_data_size < table_data_size) {
		/* Update the number of entries in table */
		(*num_table_entries_ptr)++;

		lli_table_ptr->bus_address =
			cpu_to_le32(lli_array_ptr[array_counter].bus_address);

		lli_table_ptr->block_size =
			cpu_to_le32(lli_array_ptr[array_counter].block_size);

		curr_table_data_size += lli_array_ptr[array_counter].block_size;

		dev_dbg(&sep->pdev->dev,
			"[PID%d] lli_table_ptr is %p\n",
				current->pid, lli_table_ptr);
		dev_dbg(&sep->pdev->dev,
			"[PID%d] lli_table_ptr->bus_address: %08lx\n",
				current->pid,
				(unsigned long)lli_table_ptr->bus_address);

		dev_dbg(&sep->pdev->dev,
			"[PID%d] lli_table_ptr->block_size is (hex) %x\n",
				current->pid, lli_table_ptr->block_size);

		/* Check for overflow of the table data */
		if (curr_table_data_size > table_data_size) {
			dev_dbg(&sep->pdev->dev,
				"[PID%d] curr_table_data_size too large\n",
					current->pid);

			/* Update the size of block in the table */
			lli_table_ptr->block_size =
				cpu_to_le32(lli_table_ptr->block_size) -
				(curr_table_data_size - table_data_size);

			/* Update the physical address in the lli array */
			lli_array_ptr[array_counter].bus_address +=
			cpu_to_le32(lli_table_ptr->block_size);

			/* Update the block size left in the lli array */
			lli_array_ptr[array_counter].block_size =
				(curr_table_data_size - table_data_size);
		} else
			/* Advance to the next entry in the lli_array */
			array_counter++;

		dev_dbg(&sep->pdev->dev,
			"[PID%d] lli_table_ptr->bus_address is %08lx\n",
				current->pid,
				(unsigned long)lli_table_ptr->bus_address);
		dev_dbg(&sep->pdev->dev,
			"[PID%d] lli_table_ptr->block_size is (hex) %x\n",
				current->pid,
				lli_table_ptr->block_size);

		/* Move to the next entry in table */
		lli_table_ptr++;
	}

	/* Set the info entry to default */
	lli_table_ptr->bus_address = 0xffffffff;
	lli_table_ptr->block_size = 0;

	/* Set the output parameter */
	*num_processed_entries_ptr += array_counter;

}

/**
 * sep_shared_area_virt_to_bus - map shared area to bus address
 * @sep: pointer to struct sep_device
 * @virt_address: virtual address to convert
 *
 * This functions returns the physical address inside shared area according
 * to the virtual address. It can be either on the external RAM device
 * (ioremapped), or on the system RAM
 * This implementation is for the external RAM
 */
static dma_addr_t sep_shared_area_virt_to_bus(struct sep_device *sep,
	void *virt_address)
{
	dev_dbg(&sep->pdev->dev, "[PID%d] sh virt to phys v %p\n",
					current->pid, virt_address);
	dev_dbg(&sep->pdev->dev, "[PID%d] sh virt to phys p %08lx\n",
		current->pid,
		(unsigned long)
		sep->shared_bus + (virt_address - sep->shared_addr));

	return sep->shared_bus + (size_t)(virt_address - sep->shared_addr);
}

/**
 * sep_shared_area_bus_to_virt - map shared area bus address to kernel
 * @sep: pointer to struct sep_device
 * @bus_address: bus address to convert
 *
 * This functions returns the virtual address inside shared area
 * according to the physical address. It can be either on the
 * external RAM device (ioremapped), or on the system RAM
 * This implementation is for the external RAM
 */
static void *sep_shared_area_bus_to_virt(struct sep_device *sep,
	dma_addr_t bus_address)
{
	dev_dbg(&sep->pdev->dev, "[PID%d] shared bus to virt b=%lx v=%lx\n",
		current->pid,
		(unsigned long)bus_address, (unsigned long)(sep->shared_addr +
			(size_t)(bus_address - sep->shared_bus)));

	return sep->shared_addr	+ (size_t)(bus_address - sep->shared_bus);
}

/**
 * sep_debug_print_lli_tables - dump LLI table
 * @sep: pointer to struct sep_device
 * @lli_table_ptr: pointer to sep_lli_entry
 * @num_table_entries: number of entries
 * @table_data_size: total data size
 *
 * Walk the the list of the print created tables and print all the data
 */
static void sep_debug_print_lli_tables(struct sep_device *sep,
	struct sep_lli_entry *lli_table_ptr,
	unsigned long num_table_entries,
	unsigned long table_data_size)
{
#ifdef DEBUG
	unsigned long table_count = 1;
	unsigned long entries_count = 0;

	dev_dbg(&sep->pdev->dev, "[PID%d] sep_debug_print_lli_tables start\n",
					current->pid);
	if (num_table_entries == 0) {
		dev_dbg(&sep->pdev->dev, "[PID%d] no table to print\n",
			current->pid);
		return;
	}

	while ((unsigned long) lli_table_ptr->bus_address != 0xffffffff) {
		dev_dbg(&sep->pdev->dev,
			"[PID%d] lli table %08lx, "
			"table_data_size is (hex) %lx\n",
				current->pid, table_count, table_data_size);
		dev_dbg(&sep->pdev->dev,
			"[PID%d] num_table_entries is (hex) %lx\n",
				current->pid, num_table_entries);

		/* Print entries of the table (without info entry) */
		for (entries_count = 0; entries_count < num_table_entries;
			entries_count++, lli_table_ptr++) {

			dev_dbg(&sep->pdev->dev,
				"[PID%d] lli_table_ptr address is %08lx\n",
				current->pid,
				(unsigned long) lli_table_ptr);

			dev_dbg(&sep->pdev->dev,
				"[PID%d] phys address is %08lx "
				"block size is (hex) %x\n", current->pid,
				(unsigned long)lli_table_ptr->bus_address,
				lli_table_ptr->block_size);
		}

		/* Point to the info entry */
		lli_table_ptr--;

		dev_dbg(&sep->pdev->dev,
			"[PID%d] phys lli_table_ptr->block_size "
			"is (hex) %x\n",
			current->pid,
			lli_table_ptr->block_size);

		dev_dbg(&sep->pdev->dev,
			"[PID%d] phys lli_table_ptr->physical_address "
			"is %08lx\n",
			current->pid,
			(unsigned long)lli_table_ptr->bus_address);


		table_data_size = lli_table_ptr->block_size & 0xffffff;
		num_table_entries = (lli_table_ptr->block_size >> 24) & 0xff;

		dev_dbg(&sep->pdev->dev,
			"[PID%d] phys table_data_size is "
			"(hex) %lx num_table_entries is"
			" %lx bus_address is%lx\n",
				current->pid,
				table_data_size,
				num_table_entries,
				(unsigned long)lli_table_ptr->bus_address);

		if ((unsigned long)lli_table_ptr->bus_address != 0xffffffff)
			lli_table_ptr = (struct sep_lli_entry *)
				sep_shared_bus_to_virt(sep,
				(unsigned long)lli_table_ptr->bus_address);

		table_count++;
	}
	dev_dbg(&sep->pdev->dev, "[PID%d] sep_debug_print_lli_tables end\n",
					current->pid);
#endif
}


/**
 * sep_prepare_empty_lli_table - create a blank LLI table
 * @sep: pointer to struct sep_device
 * @lli_table_addr_ptr: pointer to lli table
 * @num_entries_ptr: pointer to number of entries
 * @table_data_size_ptr: point to table data size
 * @dmatables_region: Optional buffer for DMA tables
 * @dma_ctx: DMA context
 *
 * This function creates empty lli tables when there is no data
 */
static void sep_prepare_empty_lli_table(struct sep_device *sep,
		dma_addr_t *lli_table_addr_ptr,
		u32 *num_entries_ptr,
		u32 *table_data_size_ptr,
		void **dmatables_region,
		struct sep_dma_context *dma_ctx)
{
	struct sep_lli_entry *lli_table_ptr;

	/* Find the area for new table */
	lli_table_ptr =
		(struct sep_lli_entry *)(sep->shared_addr +
		SYNCHRONIC_DMA_TABLES_AREA_OFFSET_BYTES +
		dma_ctx->num_lli_tables_created * sizeof(struct sep_lli_entry) *
			SEP_DRIVER_ENTRIES_PER_TABLE_IN_SEP);

	if (dmatables_region && *dmatables_region)
		lli_table_ptr = *dmatables_region;

	lli_table_ptr->bus_address = 0;
	lli_table_ptr->block_size = 0;

	lli_table_ptr++;
	lli_table_ptr->bus_address = 0xFFFFFFFF;
	lli_table_ptr->block_size = 0;

	/* Set the output parameter value */
	*lli_table_addr_ptr = sep->shared_bus +
		SYNCHRONIC_DMA_TABLES_AREA_OFFSET_BYTES +
		dma_ctx->num_lli_tables_created *
		sizeof(struct sep_lli_entry) *
		SEP_DRIVER_ENTRIES_PER_TABLE_IN_SEP;

	/* Set the num of entries and table data size for empty table */
	*num_entries_ptr = 2;
	*table_data_size_ptr = 0;

	/* Update the number of created tables */
	dma_ctx->num_lli_tables_created++;
}

/**
 * sep_prepare_input_dma_table - prepare input DMA mappings
 * @sep: pointer to struct sep_device
 * @data_size:
 * @block_size:
 * @lli_table_ptr:
 * @num_entries_ptr:
 * @table_data_size_ptr:
 * @is_kva: set for kernel data (kernel crypt io call)
 *
 * This function prepares only input DMA table for synchronic symmetric
 * operations (HASH)
 * Note that all bus addresses that are passed to the SEP
 * are in 32 bit format; the SEP is a 32 bit device
 */
static int sep_prepare_input_dma_table(struct sep_device *sep,
	unsigned long app_virt_addr,
	u32 data_size,
	u32 block_size,
	dma_addr_t *lli_table_ptr,
	u32 *num_entries_ptr,
	u32 *table_data_size_ptr,
	bool is_kva,
	void **dmatables_region,
	struct sep_dma_context *dma_ctx
)
{
	int error = 0;
	/* Pointer to the info entry of the table - the last entry */
	struct sep_lli_entry *info_entry_ptr;
	/* Array of pointers to page */
	struct sep_lli_entry *lli_array_ptr;
	/* Points to the first entry to be processed in the lli_in_array */
	u32 current_entry = 0;
	/* Num entries in the virtual buffer */
	u32 sep_lli_entries = 0;
	/* Lli table pointer */
	struct sep_lli_entry *in_lli_table_ptr;
	/* The total data in one table */
	u32 table_data_size = 0;
	/* Flag for last table */
	u32 last_table_flag = 0;
	/* Number of entries in lli table */
	u32 num_entries_in_table = 0;
	/* Next table address */
	void *lli_table_alloc_addr = NULL;
	void *dma_lli_table_alloc_addr = NULL;
	void *dma_in_lli_table_ptr = NULL;

	dev_dbg(&sep->pdev->dev,
		"[PID%d] prepare intput dma tbl data size: (hex) %x\n",
		current->pid, data_size);

	dev_dbg(&sep->pdev->dev, "[PID%d] block_size is (hex) %x\n",
					current->pid, block_size);

	/* Initialize the pages pointers */
	dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].in_page_array = NULL;
	dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].in_num_pages = 0;

	/* Set the kernel address for first table to be allocated */
	lli_table_alloc_addr = (void *)(sep->shared_addr +
		SYNCHRONIC_DMA_TABLES_AREA_OFFSET_BYTES +
		dma_ctx->num_lli_tables_created * sizeof(struct sep_lli_entry) *
		SEP_DRIVER_ENTRIES_PER_TABLE_IN_SEP);

	if (data_size == 0) {
		if (dmatables_region) {
			error = sep_allocate_dmatables_region(sep,
						dmatables_region,
						dma_ctx,
						1);
			if (error)
				return error;
		}
		/* Special case  - create meptu table - 2 entries, zero data */
		sep_prepare_empty_lli_table(sep, lli_table_ptr,
				num_entries_ptr, table_data_size_ptr,
				dmatables_region, dma_ctx);
		goto update_dcb_counter;
	}

	/* Check if the pages are in Kernel Virtual Address layout */
	if (is_kva == true)
		error = sep_lock_kernel_pages(sep, app_virt_addr,
			data_size, &lli_array_ptr, SEP_DRIVER_IN_FLAG,
			dma_ctx);
	else
		/*
		 * Lock the pages of the user buffer
		 * and translate them to pages
		 */
		error = sep_lock_user_pages(sep, app_virt_addr,
			data_size, &lli_array_ptr, SEP_DRIVER_IN_FLAG,
			dma_ctx);

	if (error)
		goto end_function;

	dev_dbg(&sep->pdev->dev,
		"[PID%d] output sep_in_num_pages is (hex) %x\n",
		current->pid,
		dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].in_num_pages);

	current_entry = 0;
	info_entry_ptr = NULL;

	sep_lli_entries =
		dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].in_num_pages;

	dma_lli_table_alloc_addr = lli_table_alloc_addr;
	if (dmatables_region) {
		error = sep_allocate_dmatables_region(sep,
					dmatables_region,
					dma_ctx,
					sep_lli_entries);
		if (error)
			return error;
		lli_table_alloc_addr = *dmatables_region;
	}

	/* Loop till all the entries in in array are processed */
	while (current_entry < sep_lli_entries) {

		/* Set the new input and output tables */
		in_lli_table_ptr =
			(struct sep_lli_entry *)lli_table_alloc_addr;
		dma_in_lli_table_ptr =
			(struct sep_lli_entry *)dma_lli_table_alloc_addr;

		lli_table_alloc_addr += sizeof(struct sep_lli_entry) *
			SEP_DRIVER_ENTRIES_PER_TABLE_IN_SEP;
		dma_lli_table_alloc_addr += sizeof(struct sep_lli_entry) *
			SEP_DRIVER_ENTRIES_PER_TABLE_IN_SEP;

		if (dma_lli_table_alloc_addr >
			((void *)sep->shared_addr +
			SYNCHRONIC_DMA_TABLES_AREA_OFFSET_BYTES +
			SYNCHRONIC_DMA_TABLES_AREA_SIZE_BYTES)) {

			error = -ENOMEM;
			goto end_function_error;

		}

		/* Update the number of created tables */
		dma_ctx->num_lli_tables_created++;

		/* Calculate the maximum size of data for input table */
		table_data_size = sep_calculate_lli_table_max_size(sep,
			&lli_array_ptr[current_entry],
			(sep_lli_entries - current_entry),
			&last_table_flag);

		/*
		 * If this is not the last table -
		 * then align it to the block size
		 */
		if (!last_table_flag)
			table_data_size =
				(table_data_size / block_size) * block_size;

		dev_dbg(&sep->pdev->dev,
			"[PID%d] output table_data_size is (hex) %x\n",
				current->pid,
				table_data_size);

		/* Construct input lli table */
		sep_build_lli_table(sep, &lli_array_ptr[current_entry],
			in_lli_table_ptr,
			&current_entry, &num_entries_in_table, table_data_size);

		if (info_entry_ptr == NULL) {

			/* Set the output parameters to physical addresses */
			*lli_table_ptr = sep_shared_area_virt_to_bus(sep,
				dma_in_lli_table_ptr);
			*num_entries_ptr = num_entries_in_table;
			*table_data_size_ptr = table_data_size;

			dev_dbg(&sep->pdev->dev,
				"[PID%d] output lli_table_in_ptr is %08lx\n",
				current->pid,
				(unsigned long)*lli_table_ptr);

		} else {
			/* Update the info entry of the previous in table */
			info_entry_ptr->bus_address =
				sep_shared_area_virt_to_bus(sep,
							dma_in_lli_table_ptr);
			info_entry_ptr->block_size =
				((num_entries_in_table) << 24) |
				(table_data_size);
		}
		/* Save the pointer to the info entry of the current tables */
		info_entry_ptr = in_lli_table_ptr + num_entries_in_table - 1;
	}
	/* Print input tables */
	if (!dmatables_region) {
		sep_debug_print_lli_tables(sep, (struct sep_lli_entry *)
			sep_shared_area_bus_to_virt(sep, *lli_table_ptr),
			*num_entries_ptr, *table_data_size_ptr);
	}

	/* The array of the pages */
	kfree(lli_array_ptr);

update_dcb_counter:
	/* Update DCB counter */
	dma_ctx->nr_dcb_creat++;
	goto end_function;

end_function_error:
	/* Free all the allocated resources */
	kfree(dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].in_map_array);
	dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].in_map_array = NULL;
	kfree(lli_array_ptr);
	kfree(dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].in_page_array);
	dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].in_page_array = NULL;

end_function:
	return error;

}

/**
 * sep_construct_dma_tables_from_lli - prepare AES/DES mappings
 * @sep: pointer to struct sep_device
 * @lli_in_array:
 * @sep_in_lli_entries:
 * @lli_out_array:
 * @sep_out_lli_entries
 * @block_size
 * @lli_table_in_ptr
 * @lli_table_out_ptr
 * @in_num_entries_ptr
 * @out_num_entries_ptr
 * @table_data_size_ptr
 *
 * This function creates the input and output DMA tables for
 * symmetric operations (AES/DES) according to the block
 * size from LLI arays
 * Note that all bus addresses that are passed to the SEP
 * are in 32 bit format; the SEP is a 32 bit device
 */
static int sep_construct_dma_tables_from_lli(
	struct sep_device *sep,
	struct sep_lli_entry *lli_in_array,
	u32	sep_in_lli_entries,
	struct sep_lli_entry *lli_out_array,
	u32	sep_out_lli_entries,
	u32	block_size,
	dma_addr_t *lli_table_in_ptr,
	dma_addr_t *lli_table_out_ptr,
	u32	*in_num_entries_ptr,
	u32	*out_num_entries_ptr,
	u32	*table_data_size_ptr,
	void	**dmatables_region,
	struct sep_dma_context *dma_ctx)
{
	/* Points to the area where next lli table can be allocated */
	void *lli_table_alloc_addr = NULL;
	/*
	 * Points to the area in shared region where next lli table
	 * can be allocated
	 */
	void *dma_lli_table_alloc_addr = NULL;
	/* Input lli table in dmatables_region or shared region */
	struct sep_lli_entry *in_lli_table_ptr = NULL;
	/* Input lli table location in the shared region */
	struct sep_lli_entry *dma_in_lli_table_ptr = NULL;
	/* Output lli table in dmatables_region or shared region */
	struct sep_lli_entry *out_lli_table_ptr = NULL;
	/* Output lli table location in the shared region */
	struct sep_lli_entry *dma_out_lli_table_ptr = NULL;
	/* Pointer to the info entry of the table - the last entry */
	struct sep_lli_entry *info_in_entry_ptr = NULL;
	/* Pointer to the info entry of the table - the last entry */
	struct sep_lli_entry *info_out_entry_ptr = NULL;
	/* Points to the first entry to be processed in the lli_in_array */
	u32 current_in_entry = 0;
	/* Points to the first entry to be processed in the lli_out_array */
	u32 current_out_entry = 0;
	/* Max size of the input table */
	u32 in_table_data_size = 0;
	/* Max size of the output table */
	u32 out_table_data_size = 0;
	/* Flag te signifies if this is the last tables build */
	u32 last_table_flag = 0;
	/* The data size that should be in table */
	u32 table_data_size = 0;
	/* Number of entries in the input table */
	u32 num_entries_in_table = 0;
	/* Number of entries in the output table */
	u32 num_entries_out_table = 0;

	if (!dma_ctx) {
		dev_warn(&sep->pdev->dev, "DMA context uninitialized\n");
		return -EINVAL;
	}

	/* Initiate to point after the message area */
	lli_table_alloc_addr = (void *)(sep->shared_addr +
		SYNCHRONIC_DMA_TABLES_AREA_OFFSET_BYTES +
		(dma_ctx->num_lli_tables_created *
		(sizeof(struct sep_lli_entry) *
		SEP_DRIVER_ENTRIES_PER_TABLE_IN_SEP)));
	dma_lli_table_alloc_addr = lli_table_alloc_addr;

	if (dmatables_region) {
		/* 2 for both in+out table */
		if (sep_allocate_dmatables_region(sep,
					dmatables_region,
					dma_ctx,
					2*sep_in_lli_entries))
			return -ENOMEM;
		lli_table_alloc_addr = *dmatables_region;
	}

	/* Loop till all the entries in in array are not processed */
	while (current_in_entry < sep_in_lli_entries) {
		/* Set the new input and output tables */
		in_lli_table_ptr =
			(struct sep_lli_entry *)lli_table_alloc_addr;
		dma_in_lli_table_ptr =
			(struct sep_lli_entry *)dma_lli_table_alloc_addr;

		lli_table_alloc_addr += sizeof(struct sep_lli_entry) *
			SEP_DRIVER_ENTRIES_PER_TABLE_IN_SEP;
		dma_lli_table_alloc_addr += sizeof(struct sep_lli_entry) *
			SEP_DRIVER_ENTRIES_PER_TABLE_IN_SEP;

		/* Set the first output tables */
		out_lli_table_ptr =
			(struct sep_lli_entry *)lli_table_alloc_addr;
		dma_out_lli_table_ptr =
			(struct sep_lli_entry *)dma_lli_table_alloc_addr;

		/* Check if the DMA table area limit was overrun */
		if ((dma_lli_table_alloc_addr + sizeof(struct sep_lli_entry) *
			SEP_DRIVER_ENTRIES_PER_TABLE_IN_SEP) >
			((void *)sep->shared_addr +
			SYNCHRONIC_DMA_TABLES_AREA_OFFSET_BYTES +
			SYNCHRONIC_DMA_TABLES_AREA_SIZE_BYTES)) {

			dev_warn(&sep->pdev->dev, "dma table limit overrun\n");
			return -ENOMEM;
		}

		/* Update the number of the lli tables created */
		dma_ctx->num_lli_tables_created += 2;

		lli_table_alloc_addr += sizeof(struct sep_lli_entry) *
			SEP_DRIVER_ENTRIES_PER_TABLE_IN_SEP;
		dma_lli_table_alloc_addr += sizeof(struct sep_lli_entry) *
			SEP_DRIVER_ENTRIES_PER_TABLE_IN_SEP;

		/* Calculate the maximum size of data for input table */
		in_table_data_size =
			sep_calculate_lli_table_max_size(sep,
			&lli_in_array[current_in_entry],
			(sep_in_lli_entries - current_in_entry),
			&last_table_flag);

		/* Calculate the maximum size of data for output table */
		out_table_data_size =
			sep_calculate_lli_table_max_size(sep,
			&lli_out_array[current_out_entry],
			(sep_out_lli_entries - current_out_entry),
			&last_table_flag);

		if (!last_table_flag) {
			in_table_data_size = (in_table_data_size /
				block_size) * block_size;
			out_table_data_size = (out_table_data_size /
				block_size) * block_size;
		}

		table_data_size = in_table_data_size;
		if (table_data_size > out_table_data_size)
			table_data_size = out_table_data_size;

		dev_dbg(&sep->pdev->dev,
			"[PID%d] construct tables from lli"
			" in_table_data_size is (hex) %x\n", current->pid,
			in_table_data_size);

		dev_dbg(&sep->pdev->dev,
			"[PID%d] construct tables from lli"
			"out_table_data_size is (hex) %x\n", current->pid,
			out_table_data_size);

		/* Construct input lli table */
		sep_build_lli_table(sep, &lli_in_array[current_in_entry],
			in_lli_table_ptr,
			&current_in_entry,
			&num_entries_in_table,
			table_data_size);

		/* Construct output lli table */
		sep_build_lli_table(sep, &lli_out_array[current_out_entry],
			out_lli_table_ptr,
			&current_out_entry,
			&num_entries_out_table,
			table_data_size);

		/* If info entry is null - this is the first table built */
		if (info_in_entry_ptr == NULL || info_out_entry_ptr == NULL) {
			/* Set the output parameters to physical addresses */
			*lli_table_in_ptr =
			sep_shared_area_virt_to_bus(sep, dma_in_lli_table_ptr);

			*in_num_entries_ptr = num_entries_in_table;

			*lli_table_out_ptr =
				sep_shared_area_virt_to_bus(sep,
				dma_out_lli_table_ptr);

			*out_num_entries_ptr = num_entries_out_table;
			*table_data_size_ptr = table_data_size;

			dev_dbg(&sep->pdev->dev,
				"[PID%d] output lli_table_in_ptr is %08lx\n",
				current->pid,
				(unsigned long)*lli_table_in_ptr);
			dev_dbg(&sep->pdev->dev,
				"[PID%d] output lli_table_out_ptr is %08lx\n",
				current->pid,
				(unsigned long)*lli_table_out_ptr);
		} else {
			/* Update the info entry of the previous in table */
			info_in_entry_ptr->bus_address =
				sep_shared_area_virt_to_bus(sep,
				dma_in_lli_table_ptr);

			info_in_entry_ptr->block_size =
				((num_entries_in_table) << 24) |
				(table_data_size);

			/* Update the info entry of the previous in table */
			info_out_entry_ptr->bus_address =
				sep_shared_area_virt_to_bus(sep,
				dma_out_lli_table_ptr);

			info_out_entry_ptr->block_size =
				((num_entries_out_table) << 24) |
				(table_data_size);

			dev_dbg(&sep->pdev->dev,
				"[PID%d] output lli_table_in_ptr:%08lx %08x\n",
				current->pid,
				(unsigned long)info_in_entry_ptr->bus_address,
				info_in_entry_ptr->block_size);

			dev_dbg(&sep->pdev->dev,
				"[PID%d] output lli_table_out_ptr:"
				"%08lx  %08x\n",
				current->pid,
				(unsigned long)info_out_entry_ptr->bus_address,
				info_out_entry_ptr->block_size);
		}

		/* Save the pointer to the info entry of the current tables */
		info_in_entry_ptr = in_lli_table_ptr +
			num_entries_in_table - 1;
		info_out_entry_ptr = out_lli_table_ptr +
			num_entries_out_table - 1;

		dev_dbg(&sep->pdev->dev,
			"[PID%d] output num_entries_out_table is %x\n",
			current->pid,
			(u32)num_entries_out_table);
		dev_dbg(&sep->pdev->dev,
			"[PID%d] output info_in_entry_ptr is %lx\n",
			current->pid,
			(unsigned long)info_in_entry_ptr);
		dev_dbg(&sep->pdev->dev,
			"[PID%d] output info_out_entry_ptr is %lx\n",
			current->pid,
			(unsigned long)info_out_entry_ptr);
	}

	/* Print input tables */
	if (!dmatables_region) {
		sep_debug_print_lli_tables(
			sep,
			(struct sep_lli_entry *)
			sep_shared_area_bus_to_virt(sep, *lli_table_in_ptr),
			*in_num_entries_ptr,
			*table_data_size_ptr);
	}

	/* Print output tables */
	if (!dmatables_region) {
		sep_debug_print_lli_tables(
			sep,
			(struct sep_lli_entry *)
			sep_shared_area_bus_to_virt(sep, *lli_table_out_ptr),
			*out_num_entries_ptr,
			*table_data_size_ptr);
	}

	return 0;
}

/**
 * sep_prepare_input_output_dma_table - prepare DMA I/O table
 * @app_virt_in_addr:
 * @app_virt_out_addr:
 * @data_size:
 * @block_size:
 * @lli_table_in_ptr:
 * @lli_table_out_ptr:
 * @in_num_entries_ptr:
 * @out_num_entries_ptr:
 * @table_data_size_ptr:
 * @is_kva: set for kernel data; used only for kernel crypto module
 *
 * This function builds input and output DMA tables for synchronic
 * symmetric operations (AES, DES, HASH). It also checks that each table
 * is of the modular block size
 * Note that all bus addresses that are passed to the SEP
 * are in 32 bit format; the SEP is a 32 bit device
 */
static int sep_prepare_input_output_dma_table(struct sep_device *sep,
	unsigned long app_virt_in_addr,
	unsigned long app_virt_out_addr,
	u32 data_size,
	u32 block_size,
	dma_addr_t *lli_table_in_ptr,
	dma_addr_t *lli_table_out_ptr,
	u32 *in_num_entries_ptr,
	u32 *out_num_entries_ptr,
	u32 *table_data_size_ptr,
	bool is_kva,
	void **dmatables_region,
	struct sep_dma_context *dma_ctx)

{
	int error = 0;
	/* Array of pointers of page */
	struct sep_lli_entry *lli_in_array;
	/* Array of pointers of page */
	struct sep_lli_entry *lli_out_array;

	if (!dma_ctx) {
		error = -EINVAL;
		goto end_function;
	}

	if (data_size == 0) {
		/* Prepare empty table for input and output */
		if (dmatables_region) {
			error = sep_allocate_dmatables_region(
					sep,
					dmatables_region,
					dma_ctx,
					2);
		  if (error)
			goto end_function;
		}
		sep_prepare_empty_lli_table(sep, lli_table_in_ptr,
			in_num_entries_ptr, table_data_size_ptr,
			dmatables_region, dma_ctx);

		sep_prepare_empty_lli_table(sep, lli_table_out_ptr,
			out_num_entries_ptr, table_data_size_ptr,
			dmatables_region, dma_ctx);

		goto update_dcb_counter;
	}

	/* Initialize the pages pointers */
	dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].in_page_array = NULL;
	dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].out_page_array = NULL;

	/* Lock the pages of the buffer and translate them to pages */
	if (is_kva == true) {
		dev_dbg(&sep->pdev->dev, "[PID%d] Locking kernel input pages\n",
						current->pid);
		error = sep_lock_kernel_pages(sep, app_virt_in_addr,
				data_size, &lli_in_array, SEP_DRIVER_IN_FLAG,
				dma_ctx);
		if (error) {
			dev_warn(&sep->pdev->dev,
				"[PID%d] sep_lock_kernel_pages for input "
				"virtual buffer failed\n", current->pid);

			goto end_function;
		}

		dev_dbg(&sep->pdev->dev, "[PID%d] Locking kernel output pages\n",
						current->pid);
		error = sep_lock_kernel_pages(sep, app_virt_out_addr,
				data_size, &lli_out_array, SEP_DRIVER_OUT_FLAG,
				dma_ctx);

		if (error) {
			dev_warn(&sep->pdev->dev,
				"[PID%d] sep_lock_kernel_pages for output "
				"virtual buffer failed\n", current->pid);

			goto end_function_free_lli_in;
		}

	}

	else {
		dev_dbg(&sep->pdev->dev, "[PID%d] Locking user input pages\n",
						current->pid);
		error = sep_lock_user_pages(sep, app_virt_in_addr,
				data_size, &lli_in_array, SEP_DRIVER_IN_FLAG,
				dma_ctx);
		if (error) {
			dev_warn(&sep->pdev->dev,
				"[PID%d] sep_lock_user_pages for input "
				"virtual buffer failed\n", current->pid);

			goto end_function;
		}

		if (dma_ctx->secure_dma == true) {
			/* secure_dma requires use of non accessible memory */
			dev_dbg(&sep->pdev->dev, "[PID%d] in secure_dma\n",
				current->pid);
			error = sep_lli_table_secure_dma(sep,
				app_virt_out_addr, data_size, &lli_out_array,
				SEP_DRIVER_OUT_FLAG, dma_ctx);
			if (error) {
				dev_warn(&sep->pdev->dev,
					"[PID%d] secure dma table setup "
					" for output virtual buffer failed\n",
					current->pid);

				goto end_function_free_lli_in;
			}
		} else {
			/* For normal, non-secure dma */
			dev_dbg(&sep->pdev->dev, "[PID%d] not in secure_dma\n",
				current->pid);

			dev_dbg(&sep->pdev->dev,
				"[PID%d] Locking user output pages\n",
				current->pid);

			error = sep_lock_user_pages(sep, app_virt_out_addr,
				data_size, &lli_out_array, SEP_DRIVER_OUT_FLAG,
				dma_ctx);

			if (error) {
				dev_warn(&sep->pdev->dev,
					"[PID%d] sep_lock_user_pages"
					" for output virtual buffer failed\n",
					current->pid);

				goto end_function_free_lli_in;
			}
		}
	}

	dev_dbg(&sep->pdev->dev,
		"[PID%d] After lock; prep input output dma table sep_in_num_pages is (hex) %x\n",
		current->pid,
		dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].in_num_pages);

	dev_dbg(&sep->pdev->dev, "[PID%d] sep_out_num_pages is (hex) %x\n",
		current->pid,
		dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].out_num_pages);

	dev_dbg(&sep->pdev->dev,
		"[PID%d] SEP_DRIVER_ENTRIES_PER_TABLE_IN_SEP is (hex) %x\n",
		current->pid, SEP_DRIVER_ENTRIES_PER_TABLE_IN_SEP);

	/* Call the function that creates table from the lli arrays */
	dev_dbg(&sep->pdev->dev, "[PID%d] calling create table from lli\n",
					current->pid);
	error = sep_construct_dma_tables_from_lli(
			sep, lli_in_array,
			dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].
								in_num_pages,
			lli_out_array,
			dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].
								out_num_pages,
			block_size, lli_table_in_ptr, lli_table_out_ptr,
			in_num_entries_ptr, out_num_entries_ptr,
			table_data_size_ptr, dmatables_region, dma_ctx);

	if (error) {
		dev_warn(&sep->pdev->dev,
			"[PID%d] sep_construct_dma_tables_from_lli failed\n",
			current->pid);
		goto end_function_with_error;
	}

	kfree(lli_out_array);
	kfree(lli_in_array);

update_dcb_counter:
	/* Update DCB counter */
	dma_ctx->nr_dcb_creat++;

	goto end_function;

end_function_with_error:
	kfree(dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].out_map_array);
	dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].out_map_array = NULL;
	kfree(dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].out_page_array);
	dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].out_page_array = NULL;
	kfree(lli_out_array);


end_function_free_lli_in:
	kfree(dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].in_map_array);
	dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].in_map_array = NULL;
	kfree(dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].in_page_array);
	dma_ctx->dma_res_arr[dma_ctx->nr_dcb_creat].in_page_array = NULL;
	kfree(lli_in_array);

end_function:

	return error;

}

/**
 * sep_prepare_input_output_dma_table_in_dcb - prepare control blocks
 * @app_in_address: unsigned long; for data buffer in (user space)
 * @app_out_address: unsigned long; for data buffer out (user space)
 * @data_in_size: u32; for size of data
 * @block_size: u32; for block size
 * @tail_block_size: u32; for size of tail block
 * @isapplet: bool; to indicate external app
 * @is_kva: bool; kernel buffer; only used for kernel crypto module
 * @secure_dma; indicates whether this is secure_dma using IMR
 *
 * This function prepares the linked DMA tables and puts the
 * address for the linked list of tables inta a DCB (data control
 * block) the address of which is known by the SEP hardware
 * Note that all bus addresses that are passed to the SEP
 * are in 32 bit format; the SEP is a 32 bit device
 */
int sep_prepare_input_output_dma_table_in_dcb(struct sep_device *sep,
	unsigned long  app_in_address,
	unsigned long  app_out_address,
	u32  data_in_size,
	u32  block_size,
	u32  tail_block_size,
	bool isapplet,
	bool	is_kva,
	bool	secure_dma,
	struct sep_dcblock *dcb_region,
	void **dmatables_region,
	struct sep_dma_context **dma_ctx,
	struct scatterlist *src_sg,
	struct scatterlist *dst_sg)
{
	int error = 0;
	/* Size of tail */
	u32 tail_size = 0;
	/* Address of the created DCB table */
	struct sep_dcblock *dcb_table_ptr = NULL;
	/* The physical address of the first input DMA table */
	dma_addr_t in_first_mlli_address = 0;
	/* Number of entries in the first input DMA table */
	u32  in_first_num_entries = 0;
	/* The physical address of the first output DMA table */
	dma_addr_t  out_first_mlli_address = 0;
	/* Number of entries in the first output DMA table */
	u32  out_first_num_entries = 0;
	/* Data in the first input/output table */
	u32  first_data_size = 0;

	dev_dbg(&sep->pdev->dev, "[PID%d] app_in_address %lx\n",
		current->pid, app_in_address);

	dev_dbg(&sep->pdev->dev, "[PID%d] app_out_address %lx\n",
		current->pid, app_out_address);

	dev_dbg(&sep->pdev->dev, "[PID%d] data_in_size %x\n",
		current->pid, data_in_size);

	dev_dbg(&sep->pdev->dev, "[PID%d] block_size %x\n",
		current->pid, block_size);

	dev_dbg(&sep->pdev->dev, "[PID%d] tail_block_size %x\n",
		current->pid, tail_block_size);

	dev_dbg(&sep->pdev->dev, "[PID%d] isapplet %x\n",
		current->pid, isapplet);

	dev_dbg(&sep->pdev->dev, "[PID%d] is_kva %x\n",
		current->pid, is_kva);

	dev_dbg(&sep->pdev->dev, "[PID%d] src_sg %p\n",
		current->pid, src_sg);

	dev_dbg(&sep->pdev->dev, "[PID%d] dst_sg %p\n",
		current->pid, dst_sg);

	if (!dma_ctx) {
		dev_warn(&sep->pdev->dev, "[PID%d] no DMA context pointer\n",
						current->pid);
		error = -EINVAL;
		goto end_function;
	}

	if (*dma_ctx) {
		/* In case there are multiple DCBs for this transaction */
		dev_dbg(&sep->pdev->dev, "[PID%d] DMA context already set\n",
						current->pid);
	} else {
		*dma_ctx = kzalloc(sizeof(**dma_ctx), GFP_KERNEL);
		if (!(*dma_ctx)) {
			dev_dbg(&sep->pdev->dev,
				"[PID%d] Not enough memory for DMA context\n",
				current->pid);
		  error = -ENOMEM;
		  goto end_function;
		}
		dev_dbg(&sep->pdev->dev,
			"[PID%d] Created DMA context addr at 0x%p\n",
			current->pid, *dma_ctx);
	}

	(*dma_ctx)->secure_dma = secure_dma;

	/* these are for kernel crypto only */
	(*dma_ctx)->src_sg = src_sg;
	(*dma_ctx)->dst_sg = dst_sg;

	if ((*dma_ctx)->nr_dcb_creat == SEP_MAX_NUM_SYNC_DMA_OPS) {
		/* No more DCBs to allocate */
		dev_dbg(&sep->pdev->dev, "[PID%d] no more DCBs available\n",
						current->pid);
		error = -ENOSPC;
		goto end_function_error;
	}

	/* Allocate new DCB */
	if (dcb_region) {
		dcb_table_ptr = dcb_region;
	} else {
		dcb_table_ptr = (struct sep_dcblock *)(sep->shared_addr +
			SEP_DRIVER_SYSTEM_DCB_MEMORY_OFFSET_IN_BYTES +
			((*dma_ctx)->nr_dcb_creat *
						sizeof(struct sep_dcblock)));
	}

	/* Set the default values in the DCB */
	dcb_table_ptr->input_mlli_address = 0;
	dcb_table_ptr->input_mlli_num_entries = 0;
	dcb_table_ptr->input_mlli_data_size = 0;
	dcb_table_ptr->output_mlli_address = 0;
	dcb_table_ptr->output_mlli_num_entries = 0;
	dcb_table_ptr->output_mlli_data_size = 0;
	dcb_table_ptr->tail_data_size = 0;
	dcb_table_ptr->out_vr_tail_pt = 0;

	if (isapplet == true) {

		/* Check if there is enough data for DMA operation */
		if (data_in_size < SEP_DRIVER_MIN_DATA_SIZE_PER_TABLE) {
			if (is_kva == true) {
				error = -ENODEV;
				goto end_function_error;
			} else {
				if (copy_from_user(dcb_table_ptr->tail_data,
					(void __user *)app_in_address,
					data_in_size)) {
					error = -EFAULT;
					goto end_function_error;
				}
			}

			dcb_table_ptr->tail_data_size = data_in_size;

			/* Set the output user-space address for mem2mem op */
			if (app_out_address)
				dcb_table_ptr->out_vr_tail_pt =
				(aligned_u64)app_out_address;

			/*
			 * Update both data length parameters in order to avoid
			 * second data copy and allow building of empty mlli
			 * tables
			 */
			tail_size = 0x0;
			data_in_size = 0x0;

		} else {
			if (!app_out_address) {
				tail_size = data_in_size % block_size;
				if (!tail_size) {
					if (tail_block_size == block_size)
						tail_size = block_size;
				}
			} else {
				tail_size = 0;
			}
		}
		if (tail_size) {
			if (tail_size > sizeof(dcb_table_ptr->tail_data))
				return -EINVAL;
			if (is_kva == true) {
				error = -ENODEV;
				goto end_function_error;
			} else {
				/* We have tail data - copy it to DCB */
				if (copy_from_user(dcb_table_ptr->tail_data,
					(void __user *)(app_in_address +
					data_in_size - tail_size), tail_size)) {
					error = -EFAULT;
					goto end_function_error;
				}
			}
			if (app_out_address)
				/*
				 * Calculate the output address
				 * according to tail data size
				 */
				dcb_table_ptr->out_vr_tail_pt =
					(aligned_u64)app_out_address +
					data_in_size - tail_size;

			/* Save the real tail data size */
			dcb_table_ptr->tail_data_size = tail_size;
			/*
			 * Update the data size without the tail
			 * data size AKA data for the dma
			 */
			data_in_size = (data_in_size - tail_size);
		}
	}
	/* Check if we need to build only input table or input/output */
	if (app_out_address) {
		/* Prepare input/output tables */
		error = sep_prepare_input_output_dma_table(sep,
				app_in_address,
				app_out_address,
				data_in_size,
				block_size,
				&in_first_mlli_address,
				&out_first_mlli_address,
				&in_first_num_entries,
				&out_first_num_entries,
				&first_data_size,
				is_kva,
				dmatables_region,
				*dma_ctx);
	} else {
		/* Prepare input tables */
		error = sep_prepare_input_dma_table(sep,
				app_in_address,
				data_in_size,
				block_size,
				&in_first_mlli_address,
				&in_first_num_entries,
				&first_data_size,
				is_kva,
				dmatables_region,
				*dma_ctx);
	}

	if (error) {
		dev_warn(&sep->pdev->dev,
			"prepare DMA table call failed "
			"from prepare DCB call\n");
		goto end_function_error;
	}

	/* Set the DCB values */
	dcb_table_ptr->input_mlli_address = in_first_mlli_address;
	dcb_table_ptr->input_mlli_num_entries = in_first_num_entries;
	dcb_table_ptr->input_mlli_data_size = first_data_size;
	dcb_table_ptr->output_mlli_address = out_first_mlli_address;
	dcb_table_ptr->output_mlli_num_entries = out_first_num_entries;
	dcb_table_ptr->output_mlli_data_size = first_data_size;

	goto end_function;

end_function_error:
	kfree(*dma_ctx);
	*dma_ctx = NULL;

end_function:
	return error;

}


/**
 * sep_free_dma_tables_and_dcb - free DMA tables and DCBs
 * @sep: pointer to struct sep_device
 * @isapplet: indicates external application (used for kernel access)
 * @is_kva: indicates kernel addresses (only used for kernel crypto)
 *
 * This function frees the DMA tables and DCB
 */
static int sep_free_dma_tables_and_dcb(struct sep_device *sep, bool isapplet,
	bool is_kva, struct sep_dma_context **dma_ctx)
{
	struct sep_dcblock *dcb_table_ptr;
	unsigned long pt_hold;
	void *tail_pt;

	int i = 0;
	int error = 0;
	int error_temp = 0;

	dev_dbg(&sep->pdev->dev, "[PID%d] sep_free_dma_tables_and_dcb\n",
					current->pid);

	if (((*dma_ctx)->secure_dma == false) && (isapplet == true)) {
		dev_dbg(&sep->pdev->dev, "[PID%d] handling applet\n",
			current->pid);

		/* Tail stuff is only for non secure_dma */
		/* Set pointer to first DCB table */
		dcb_table_ptr = (struct sep_dcblock *)
			(sep->shared_addr +
			SEP_DRIVER_SYSTEM_DCB_MEMORY_OFFSET_IN_BYTES);

		/**
		 * Go over each DCB and see if
		 * tail pointer must be updated
		 */
		for (i = 0; dma_ctx && *dma_ctx &&
			i < (*dma_ctx)->nr_dcb_creat; i++, dcb_table_ptr++) {
			if (dcb_table_ptr->out_vr_tail_pt) {
				pt_hold = (unsigned long)dcb_table_ptr->
					out_vr_tail_pt;
				tail_pt = (void *)pt_hold;
				if (is_kva == true) {
					error = -ENODEV;
					break;
				} else {
					error_temp = copy_to_user(
						(void __user *)tail_pt,
						dcb_table_ptr->tail_data,
						dcb_table_ptr->tail_data_size);
				}
				if (error_temp) {
					/* Release the DMA resource */
					error = -EFAULT;
					break;
				}
			}
		}
	}

	/* Free the output pages, if any */
	sep_free_dma_table_data_handler(sep, dma_ctx);

	dev_dbg(&sep->pdev->dev, "[PID%d] sep_free_dma_tables_and_dcb end\n",
					current->pid);

	return error;
}

/**
 * sep_prepare_dcb_handler - prepare a control block
 * @sep: pointer to struct sep_device
 * @arg: pointer to user parameters
 * @secure_dma: indicate whether we are using secure_dma on IMR
 *
 * This function will retrieve the RAR buffer physical addresses, type
 * & size corresponding to the RAR handles provided in the buffers vector.
 */
static int sep_prepare_dcb_handler(struct sep_device *sep, unsigned long arg,
				   bool secure_dma,
				   struct sep_dma_context **dma_ctx)
{
	int error;
	/* Command arguments */
	static struct build_dcb_struct command_args;

	/* Get the command arguments */
	if (copy_from_user(&command_args, (void __user *)arg,
					sizeof(struct build_dcb_struct))) {
		error = -EFAULT;
		goto end_function;
	}

	dev_dbg(&sep->pdev->dev,
		"[PID%d] prep dcb handler app_in_address is %08llx\n",
			current->pid, command_args.app_in_address);
	dev_dbg(&sep->pdev->dev,
		"[PID%d] app_out_address is %08llx\n",
			current->pid, command_args.app_out_address);
	dev_dbg(&sep->pdev->dev,
		"[PID%d] data_size is %x\n",
			current->pid, command_args.data_in_size);
	dev_dbg(&sep->pdev->dev,
		"[PID%d] block_size is %x\n",
			current->pid, command_args.block_size);
	dev_dbg(&sep->pdev->dev,
		"[PID%d] tail block_size is %x\n",
			current->pid, command_args.tail_block_size);
	dev_dbg(&sep->pdev->dev,
		"[PID%d] is_applet is %x\n",
			current->pid, command_args.is_applet);

	if (!command_args.app_in_address) {
		dev_warn(&sep->pdev->dev,
			"[PID%d] null app_in_address\n", current->pid);
		error = -EINVAL;
		goto end_function;
	}

	error = sep_prepare_input_output_dma_table_in_dcb(sep,
			(unsigned long)command_args.app_in_address,
			(unsigned long)command_args.app_out_address,
			command_args.data_in_size, command_args.block_size,
			command_args.tail_block_size,
			command_args.is_applet, false,
			secure_dma, NULL, NULL, dma_ctx, NULL, NULL);

end_function:
	return error;

}

/**
 * sep_free_dcb_handler - free control block resources
 * @sep: pointer to struct sep_device
 *
 * This function frees the DCB resources and updates the needed
 * user-space buffers.
 */
static int sep_free_dcb_handler(struct sep_device *sep,
				struct sep_dma_context **dma_ctx)
{
	if (!dma_ctx || !(*dma_ctx)) {
		dev_dbg(&sep->pdev->dev,
			"[PID%d] no dma context defined, nothing to free\n",
			current->pid);
		return -EINVAL;
	}

	dev_dbg(&sep->pdev->dev, "[PID%d] free dcbs num of DCBs %x\n",
		current->pid,
		(*dma_ctx)->nr_dcb_creat);

	return sep_free_dma_tables_and_dcb(sep, false, false, dma_ctx);
}

/**
 * sep_ioctl - ioctl handler for sep device
 * @filp: pointer to struct file
 * @cmd: command
 * @arg: pointer to argument structure
 *
 * Implement the ioctl methods available on the SEP device.
 */
static long sep_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct sep_private_data * const private_data = filp->private_data;
	struct sep_call_status *call_status = &private_data->call_status;
	struct sep_device *sep = private_data->device;
	struct sep_dma_context **dma_ctx = &private_data->dma_ctx;
	struct sep_queue_info **my_queue_elem = &private_data->my_queue_elem;
	int error = 0;

	dev_dbg(&sep->pdev->dev, "[PID%d] ioctl cmd 0x%x\n",
		current->pid, cmd);
	dev_dbg(&sep->pdev->dev, "[PID%d] dma context addr 0x%p\n",
		current->pid, *dma_ctx);

	/* Make sure we own this device */
	error = sep_check_transaction_owner(sep);
	if (error) {
		dev_dbg(&sep->pdev->dev, "[PID%d] ioctl pid is not owner\n",
			current->pid);
		goto end_function;
	}

	/* Check that sep_mmap has been called before */
	if (0 == test_bit(SEP_LEGACY_MMAP_DONE_OFFSET,
				&call_status->status)) {
		dev_dbg(&sep->pdev->dev,
			"[PID%d] mmap not called\n", current->pid);
		error = -EPROTO;
		goto end_function;
	}

	/* Check that the command is for SEP device */
	if (_IOC_TYPE(cmd) != SEP_IOC_MAGIC_NUMBER) {
		error = -ENOTTY;
		goto end_function;
	}

	switch (cmd) {
	case SEP_IOCSENDSEPCOMMAND:
		dev_dbg(&sep->pdev->dev,
			"[PID%d] SEP_IOCSENDSEPCOMMAND start\n",
			current->pid);
		if (1 == test_bit(SEP_LEGACY_SENDMSG_DONE_OFFSET,
				  &call_status->status)) {
			dev_warn(&sep->pdev->dev,
				"[PID%d] send msg already done\n",
				current->pid);
			error = -EPROTO;
			goto end_function;
		}
		/* Send command to SEP */
		error = sep_send_command_handler(sep);
		if (!error)
			set_bit(SEP_LEGACY_SENDMSG_DONE_OFFSET,
				&call_status->status);
		dev_dbg(&sep->pdev->dev,
			"[PID%d] SEP_IOCSENDSEPCOMMAND end\n",
			current->pid);
		break;
	case SEP_IOCENDTRANSACTION:
		dev_dbg(&sep->pdev->dev,
			"[PID%d] SEP_IOCENDTRANSACTION start\n",
			current->pid);
		error = sep_end_transaction_handler(sep, dma_ctx, call_status,
						    my_queue_elem);
		dev_dbg(&sep->pdev->dev,
			"[PID%d] SEP_IOCENDTRANSACTION end\n",
			current->pid);
		break;
	case SEP_IOCPREPAREDCB:
		dev_dbg(&sep->pdev->dev,
			"[PID%d] SEP_IOCPREPAREDCB start\n",
			current->pid);
	case SEP_IOCPREPAREDCB_SECURE_DMA:
		dev_dbg(&sep->pdev->dev,
			"[PID%d] SEP_IOCPREPAREDCB_SECURE_DMA start\n",
			current->pid);
		if (1 == test_bit(SEP_LEGACY_SENDMSG_DONE_OFFSET,
				  &call_status->status)) {
			dev_dbg(&sep->pdev->dev,
				"[PID%d] dcb prep needed before send msg\n",
				current->pid);
			error = -EPROTO;
			goto end_function;
		}

		if (!arg) {
			dev_dbg(&sep->pdev->dev,
				"[PID%d] dcb null arg\n", current->pid);
			error = -EINVAL;
			goto end_function;
		}

		if (cmd == SEP_IOCPREPAREDCB) {
			/* No secure dma */
			dev_dbg(&sep->pdev->dev,
				"[PID%d] SEP_IOCPREPAREDCB (no secure_dma)\n",
				current->pid);

			error = sep_prepare_dcb_handler(sep, arg, false,
				dma_ctx);
		} else {
			/* Secure dma */
			dev_dbg(&sep->pdev->dev,
				"[PID%d] SEP_IOC_POC (with secure_dma)\n",
				current->pid);

			error = sep_prepare_dcb_handler(sep, arg, true,
				dma_ctx);
		}
		dev_dbg(&sep->pdev->dev, "[PID%d] dcb's end\n",
			current->pid);
		break;
	case SEP_IOCFREEDCB:
		dev_dbg(&sep->pdev->dev, "[PID%d] SEP_IOCFREEDCB start\n",
			current->pid);
	case SEP_IOCFREEDCB_SECURE_DMA:
		dev_dbg(&sep->pdev->dev,
			"[PID%d] SEP_IOCFREEDCB_SECURE_DMA start\n",
			current->pid);
		error = sep_free_dcb_handler(sep, dma_ctx);
		dev_dbg(&sep->pdev->dev, "[PID%d] SEP_IOCFREEDCB end\n",
			current->pid);
		break;
	default:
		error = -ENOTTY;
		dev_dbg(&sep->pdev->dev, "[PID%d] default end\n",
			current->pid);
		break;
	}

end_function:
	dev_dbg(&sep->pdev->dev, "[PID%d] ioctl end\n", current->pid);

	return error;
}

/**
 * sep_inthandler - interrupt handler for sep device
 * @irq: interrupt
 * @dev_id: device id
 */
static irqreturn_t sep_inthandler(int irq, void *dev_id)
{
	unsigned long lock_irq_flag;
	u32 reg_val, reg_val2 = 0;
	struct sep_device *sep = dev_id;
	irqreturn_t int_error = IRQ_HANDLED;

	/* Are we in power save? */
#if defined(CONFIG_PM_RUNTIME) && defined(SEP_ENABLE_RUNTIME_PM)
	if (sep->pdev->dev.power.runtime_status != RPM_ACTIVE) {
		dev_dbg(&sep->pdev->dev, "interrupt during pwr save\n");
		return IRQ_NONE;
	}
#endif

	if (test_bit(SEP_WORKING_LOCK_BIT, &sep->in_use_flags) == 0) {
		dev_dbg(&sep->pdev->dev, "interrupt while nobody using sep\n");
		return IRQ_NONE;
	}

	/* Read the IRR register to check if this is SEP interrupt */
	reg_val = sep_read_reg(sep, HW_HOST_IRR_REG_ADDR);

	dev_dbg(&sep->pdev->dev, "sep int: IRR REG val: %x\n", reg_val);

	if (reg_val & (0x1 << 13)) {

		/* Lock and update the counter of reply messages */
		spin_lock_irqsave(&sep->snd_rply_lck, lock_irq_flag);
		sep->reply_ct++;
		spin_unlock_irqrestore(&sep->snd_rply_lck, lock_irq_flag);

		dev_dbg(&sep->pdev->dev, "sep int: send_ct %lx reply_ct %lx\n",
					sep->send_ct, sep->reply_ct);

		/* Is this a kernel client request */
		if (sep->in_kernel) {
			tasklet_schedule(&sep->finish_tasklet);
			goto finished_interrupt;
		}

		/* Is this printf or daemon request? */
		reg_val2 = sep_read_reg(sep, HW_HOST_SEP_HOST_GPR2_REG_ADDR);
		dev_dbg(&sep->pdev->dev,
			"SEP Interrupt - GPR2 is %08x\n", reg_val2);

		clear_bit(SEP_WORKING_LOCK_BIT, &sep->in_use_flags);

		if ((reg_val2 >> 30) & 0x1) {
			dev_dbg(&sep->pdev->dev, "int: printf request\n");
		} else if (reg_val2 >> 31) {
			dev_dbg(&sep->pdev->dev, "int: daemon request\n");
		} else {
			dev_dbg(&sep->pdev->dev, "int: SEP reply\n");
			wake_up(&sep->event_interrupt);
		}
	} else {
		dev_dbg(&sep->pdev->dev, "int: not SEP interrupt\n");
		int_error = IRQ_NONE;
	}

finished_interrupt:

	if (int_error == IRQ_HANDLED)
		sep_write_reg(sep, HW_HOST_ICR_REG_ADDR, reg_val);

	return int_error;
}

/**
 * sep_reconfig_shared_area - reconfigure shared area
 * @sep: pointer to struct sep_device
 *
 * Reconfig the shared area between HOST and SEP - needed in case
 * the DX_CC_Init function was called before OS loading.
 */
static int sep_reconfig_shared_area(struct sep_device *sep)
{
	int ret_val;

	/* use to limit waiting for SEP */
	unsigned long end_time;

	/* Send the new SHARED MESSAGE AREA to the SEP */
	dev_dbg(&sep->pdev->dev, "reconfig shared; sending %08llx to sep\n",
				(unsigned long long)sep->shared_bus);

	sep_write_reg(sep, HW_HOST_HOST_SEP_GPR1_REG_ADDR, sep->shared_bus);

	/* Poll for SEP response */
	ret_val = sep_read_reg(sep, HW_HOST_SEP_HOST_GPR1_REG_ADDR);

	end_time = jiffies + (WAIT_TIME * HZ);

	while ((time_before(jiffies, end_time)) && (ret_val != 0xffffffff) &&
		(ret_val != sep->shared_bus))
		ret_val = sep_read_reg(sep, HW_HOST_SEP_HOST_GPR1_REG_ADDR);

	/* Check the return value (register) */
	if (ret_val != sep->shared_bus) {
		dev_warn(&sep->pdev->dev, "could not reconfig shared area\n");
		dev_warn(&sep->pdev->dev, "result was %x\n", ret_val);
		ret_val = -ENOMEM;
	} else
		ret_val = 0;

	dev_dbg(&sep->pdev->dev, "reconfig shared area end\n");

	return ret_val;
}

/**
 *	sep_activate_dcb_dmatables_context - Takes DCB & DMA tables
 *						contexts into use
 *	@sep: SEP device
 *	@dcb_region: DCB region copy
 *	@dmatables_region: MLLI/DMA tables copy
 *	@dma_ctx: DMA context for current transaction
 */
ssize_t sep_activate_dcb_dmatables_context(struct sep_device *sep,
					struct sep_dcblock **dcb_region,
					void **dmatables_region,
					struct sep_dma_context *dma_ctx)
{
	void *dmaregion_free_start = NULL;
	void *dmaregion_free_end = NULL;
	void *dcbregion_free_start = NULL;
	void *dcbregion_free_end = NULL;
	ssize_t error = 0;

	dev_dbg(&sep->pdev->dev, "[PID%d] activating dcb/dma region\n",
		current->pid);

	if (1 > dma_ctx->nr_dcb_creat) {
		dev_warn(&sep->pdev->dev,
			 "[PID%d] invalid number of dcbs to activate 0x%08X\n",
			 current->pid, dma_ctx->nr_dcb_creat);
		error = -EINVAL;
		goto end_function;
	}

	dmaregion_free_start = sep->shared_addr
				+ SYNCHRONIC_DMA_TABLES_AREA_OFFSET_BYTES;
	dmaregion_free_end = dmaregion_free_start
				+ SYNCHRONIC_DMA_TABLES_AREA_SIZE_BYTES - 1;

	if (dmaregion_free_start
	     + dma_ctx->dmatables_len > dmaregion_free_end) {
		error = -ENOMEM;
		goto end_function;
	}
	memcpy(dmaregion_free_start,
	       *dmatables_region,
	       dma_ctx->dmatables_len);
	/* Free MLLI table copy */
	kfree(*dmatables_region);
	*dmatables_region = NULL;

	/* Copy thread's DCB  table copy to DCB table region */
	dcbregion_free_start = sep->shared_addr +
				SEP_DRIVER_SYSTEM_DCB_MEMORY_OFFSET_IN_BYTES;
	dcbregion_free_end = dcbregion_free_start +
				(SEP_MAX_NUM_SYNC_DMA_OPS *
					sizeof(struct sep_dcblock)) - 1;

	if (dcbregion_free_start
	     + (dma_ctx->nr_dcb_creat * sizeof(struct sep_dcblock))
	     > dcbregion_free_end) {
		error = -ENOMEM;
		goto end_function;
	}

	memcpy(dcbregion_free_start,
	       *dcb_region,
	       dma_ctx->nr_dcb_creat * sizeof(struct sep_dcblock));

	/* Print the tables */
	dev_dbg(&sep->pdev->dev, "activate: input table\n");
	sep_debug_print_lli_tables(sep,
		(struct sep_lli_entry *)sep_shared_area_bus_to_virt(sep,
		(*dcb_region)->input_mlli_address),
		(*dcb_region)->input_mlli_num_entries,
		(*dcb_region)->input_mlli_data_size);

	dev_dbg(&sep->pdev->dev, "activate: output table\n");
	sep_debug_print_lli_tables(sep,
		(struct sep_lli_entry *)sep_shared_area_bus_to_virt(sep,
		(*dcb_region)->output_mlli_address),
		(*dcb_region)->output_mlli_num_entries,
		(*dcb_region)->output_mlli_data_size);

	dev_dbg(&sep->pdev->dev,
		 "[PID%d] printing activated tables\n", current->pid);

end_function:
	kfree(*dmatables_region);
	*dmatables_region = NULL;

	kfree(*dcb_region);
	*dcb_region = NULL;

	return error;
}

/**
 *	sep_create_dcb_dmatables_context - Creates DCB & MLLI/DMA table context
 *	@sep: SEP device
 *	@dcb_region: DCB region buf to create for current transaction
 *	@dmatables_region: MLLI/DMA tables buf to create for current transaction
 *	@dma_ctx: DMA context buf to create for current transaction
 *	@user_dcb_args: User arguments for DCB/MLLI creation
 *	@num_dcbs: Number of DCBs to create
 *	@secure_dma: Indicate use of IMR restricted memory secure dma
 */
static ssize_t sep_create_dcb_dmatables_context(struct sep_device *sep,
			struct sep_dcblock **dcb_region,
			void **dmatables_region,
			struct sep_dma_context **dma_ctx,
			const struct build_dcb_struct __user *user_dcb_args,
			const u32 num_dcbs, bool secure_dma)
{
	int error = 0;
	int i = 0;
	struct build_dcb_struct *dcb_args = NULL;

	dev_dbg(&sep->pdev->dev, "[PID%d] creating dcb/dma region\n",
		current->pid);

	if (!dcb_region || !dma_ctx || !dmatables_region || !user_dcb_args) {
		error = -EINVAL;
		goto end_function;
	}

	if (SEP_MAX_NUM_SYNC_DMA_OPS < num_dcbs) {
		dev_warn(&sep->pdev->dev,
			 "[PID%d] invalid number of dcbs 0x%08X\n",
			 current->pid, num_dcbs);
		error = -EINVAL;
		goto end_function;
	}

	dcb_args = kcalloc(num_dcbs, sizeof(struct build_dcb_struct),
			   GFP_KERNEL);
	if (!dcb_args) {
		error = -ENOMEM;
		goto end_function;
	}

	if (copy_from_user(dcb_args,
			user_dcb_args,
			num_dcbs * sizeof(struct build_dcb_struct))) {
		error = -EFAULT;
		goto end_function;
	}

	/* Allocate thread-specific memory for DCB */
	*dcb_region = kzalloc(num_dcbs * sizeof(struct sep_dcblock),
			      GFP_KERNEL);
	if (!(*dcb_region)) {
		error = -ENOMEM;
		goto end_function;
	}

	/* Prepare DCB and MLLI table into the allocated regions */
	for (i = 0; i < num_dcbs; i++) {
		error = sep_prepare_input_output_dma_table_in_dcb(sep,
				(unsigned long)dcb_args[i].app_in_address,
				(unsigned long)dcb_args[i].app_out_address,
				dcb_args[i].data_in_size,
				dcb_args[i].block_size,
				dcb_args[i].tail_block_size,
				dcb_args[i].is_applet,
				false, secure_dma,
				*dcb_region, dmatables_region,
				dma_ctx,
				NULL,
				NULL);
		if (error) {
			dev_warn(&sep->pdev->dev,
				 "[PID%d] dma table creation failed\n",
				 current->pid);
			goto end_function;
		}

		if (dcb_args[i].app_in_address != 0)
			(*dma_ctx)->input_data_len += dcb_args[i].data_in_size;
	}

end_function:
	kfree(dcb_args);
	return error;

}

/**
 *	sep_create_dcb_dmatables_context_kernel - Creates DCB & MLLI/DMA table context
 *      for kernel crypto
 *	@sep: SEP device
 *	@dcb_region: DCB region buf to create for current transaction
 *	@dmatables_region: MLLI/DMA tables buf to create for current transaction
 *	@dma_ctx: DMA context buf to create for current transaction
 *	@user_dcb_args: User arguments for DCB/MLLI creation
 *	@num_dcbs: Number of DCBs to create
 *	This does that same thing as sep_create_dcb_dmatables_context
 *	except that it is used only for the kernel crypto operation. It is
 *	separate because there is no user data involved; the dcb data structure
 *	is specific for kernel crypto (build_dcb_struct_kernel)
 */
int sep_create_dcb_dmatables_context_kernel(struct sep_device *sep,
			struct sep_dcblock **dcb_region,
			void **dmatables_region,
			struct sep_dma_context **dma_ctx,
			const struct build_dcb_struct_kernel *dcb_data,
			const u32 num_dcbs)
{
	int error = 0;
	int i = 0;

	dev_dbg(&sep->pdev->dev, "[PID%d] creating dcb/dma region\n",
		current->pid);

	if (!dcb_region || !dma_ctx || !dmatables_region || !dcb_data) {
		error = -EINVAL;
		goto end_function;
	}

	if (SEP_MAX_NUM_SYNC_DMA_OPS < num_dcbs) {
		dev_warn(&sep->pdev->dev,
			 "[PID%d] invalid number of dcbs 0x%08X\n",
			 current->pid, num_dcbs);
		error = -EINVAL;
		goto end_function;
	}

	dev_dbg(&sep->pdev->dev, "[PID%d] num_dcbs is %d\n",
		current->pid, num_dcbs);

	/* Allocate thread-specific memory for DCB */
	*dcb_region = kzalloc(num_dcbs * sizeof(struct sep_dcblock),
			      GFP_KERNEL);
	if (!(*dcb_region)) {
		error = -ENOMEM;
		goto end_function;
	}

	/* Prepare DCB and MLLI table into the allocated regions */
	for (i = 0; i < num_dcbs; i++) {
		error = sep_prepare_input_output_dma_table_in_dcb(sep,
				(unsigned long)dcb_data->app_in_address,
				(unsigned long)dcb_data->app_out_address,
				dcb_data->data_in_size,
				dcb_data->block_size,
				dcb_data->tail_block_size,
				dcb_data->is_applet,
				true,
				false,
				*dcb_region, dmatables_region,
				dma_ctx,
				dcb_data->src_sg,
				dcb_data->dst_sg);
		if (error) {
			dev_warn(&sep->pdev->dev,
				 "[PID%d] dma table creation failed\n",
				 current->pid);
			goto end_function;
		}
	}

end_function:
	return error;

}

/**
 *	sep_activate_msgarea_context - Takes the message area context into use
 *	@sep: SEP device
 *	@msg_region: Message area context buf
 *	@msg_len: Message area context buffer size
 */
static ssize_t sep_activate_msgarea_context(struct sep_device *sep,
					    void **msg_region,
					    const size_t msg_len)
{
	dev_dbg(&sep->pdev->dev, "[PID%d] activating msg region\n",
		current->pid);

	if (!msg_region || !(*msg_region) ||
	    SEP_DRIVER_MESSAGE_SHARED_AREA_SIZE_IN_BYTES < msg_len) {
		dev_warn(&sep->pdev->dev,
			 "[PID%d] invalid act msgarea len 0x%08zX\n",
			 current->pid, msg_len);
		return -EINVAL;
	}

	memcpy(sep->shared_addr, *msg_region, msg_len);

	return 0;
}

/**
 *	sep_create_msgarea_context - Creates message area context
 *	@sep: SEP device
 *	@msg_region: Msg area region buf to create for current transaction
 *	@msg_user: Content for msg area region from user
 *	@msg_len: Message area size
 */
static ssize_t sep_create_msgarea_context(struct sep_device *sep,
					  void **msg_region,
					  const void __user *msg_user,
					  const size_t msg_len)
{
	int error = 0;

	dev_dbg(&sep->pdev->dev, "[PID%d] creating msg region\n",
		current->pid);

	if (!msg_region ||
	    !msg_user ||
	    SEP_DRIVER_MAX_MESSAGE_SIZE_IN_BYTES < msg_len ||
	    SEP_DRIVER_MIN_MESSAGE_SIZE_IN_BYTES > msg_len) {
		dev_warn(&sep->pdev->dev,
			 "[PID%d] invalid creat msgarea len 0x%08zX\n",
			 current->pid, msg_len);
		error = -EINVAL;
		goto end_function;
	}

	/* Allocate thread-specific memory for message buffer */
	*msg_region = kzalloc(msg_len, GFP_KERNEL);
	if (!(*msg_region)) {
		error = -ENOMEM;
		goto end_function;
	}

	/* Copy input data to write() to allocated message buffer */
	if (copy_from_user(*msg_region, msg_user, msg_len)) {
		error = -EFAULT;
		goto end_function;
	}

end_function:
	if (error && msg_region) {
		kfree(*msg_region);
		*msg_region = NULL;
	}

	return error;
}


/**
 *	sep_read - Returns results of an operation for fastcall interface
 *	@filp: File pointer
 *	@buf_user: User buffer for storing results
 *	@count_user: User buffer size
 *	@offset: File offset, not supported
 *
 *	The implementation does not support reading in chunks, all data must be
 *	consumed during a single read system call.
 */
static ssize_t sep_read(struct file *filp,
			char __user *buf_user, size_t count_user,
			loff_t *offset)
{
	struct sep_private_data * const private_data = filp->private_data;
	struct sep_call_status *call_status = &private_data->call_status;
	struct sep_device *sep = private_data->device;
	struct sep_dma_context **dma_ctx = &private_data->dma_ctx;
	struct sep_queue_info **my_queue_elem = &private_data->my_queue_elem;
	ssize_t error = 0, error_tmp = 0;

	/* Am I the process that owns the transaction? */
	error = sep_check_transaction_owner(sep);
	if (error) {
		dev_dbg(&sep->pdev->dev, "[PID%d] read pid is not owner\n",
			current->pid);
		goto end_function;
	}

	/* Checks that user has called necessary apis */
	if (0 == test_bit(SEP_FASTCALL_WRITE_DONE_OFFSET,
			&call_status->status)) {
		dev_warn(&sep->pdev->dev,
			 "[PID%d] fastcall write not called\n",
			 current->pid);
		error = -EPROTO;
		goto end_function_error;
	}

	if (!buf_user) {
		dev_warn(&sep->pdev->dev,
			 "[PID%d] null user buffer\n",
			 current->pid);
		error = -EINVAL;
		goto end_function_error;
	}


	/* Wait for SEP to finish */
	wait_event(sep->event_interrupt,
		   test_bit(SEP_WORKING_LOCK_BIT,
			    &sep->in_use_flags) == 0);

	sep_dump_message(sep);

	dev_dbg(&sep->pdev->dev, "[PID%d] count_user = 0x%08zX\n",
		current->pid, count_user);

	/* In case user has allocated bigger buffer */
	if (count_user > SEP_DRIVER_MESSAGE_SHARED_AREA_SIZE_IN_BYTES)
		count_user = SEP_DRIVER_MESSAGE_SHARED_AREA_SIZE_IN_BYTES;

	if (copy_to_user(buf_user, sep->shared_addr, count_user)) {
		error = -EFAULT;
		goto end_function_error;
	}

	dev_dbg(&sep->pdev->dev, "[PID%d] read succeeded\n", current->pid);
	error = count_user;

end_function_error:
	/* Copy possible tail data to user and free DCB and MLLIs */
	error_tmp = sep_free_dcb_handler(sep, dma_ctx);
	if (error_tmp)
		dev_warn(&sep->pdev->dev, "[PID%d] dcb free failed\n",
			current->pid);

	/* End the transaction, wakeup pending ones */
	error_tmp = sep_end_transaction_handler(sep, dma_ctx, call_status,
		my_queue_elem);
	if (error_tmp)
		dev_warn(&sep->pdev->dev,
			 "[PID%d] ending transaction failed\n",
			 current->pid);

end_function:
	return error;
}

/**
 *	sep_fastcall_args_get - Gets fastcall params from user
 *	sep: SEP device
 *	@args: Parameters buffer
 *	@buf_user: User buffer for operation parameters
 *	@count_user: User buffer size
 */
static inline ssize_t sep_fastcall_args_get(struct sep_device *sep,
					    struct sep_fastcall_hdr *args,
					    const char __user *buf_user,
					    const size_t count_user)
{
	ssize_t error = 0;
	size_t actual_count = 0;

	if (!buf_user) {
		dev_warn(&sep->pdev->dev,
			 "[PID%d] null user buffer\n",
			 current->pid);
		error = -EINVAL;
		goto end_function;
	}

	if (count_user < sizeof(struct sep_fastcall_hdr)) {
		dev_warn(&sep->pdev->dev,
			 "[PID%d] too small message size 0x%08zX\n",
			 current->pid, count_user);
		error = -EINVAL;
		goto end_function;
	}


	if (copy_from_user(args, buf_user, sizeof(struct sep_fastcall_hdr))) {
		error = -EFAULT;
		goto end_function;
	}

	if (SEP_FC_MAGIC != args->magic) {
		dev_warn(&sep->pdev->dev,
			 "[PID%d] invalid fastcall magic 0x%08X\n",
			 current->pid, args->magic);
		error = -EINVAL;
		goto end_function;
	}

	dev_dbg(&sep->pdev->dev, "[PID%d] fastcall hdr num of DCBs 0x%08X\n",
		current->pid, args->num_dcbs);
	dev_dbg(&sep->pdev->dev, "[PID%d] fastcall hdr msg len 0x%08X\n",
		current->pid, args->msg_len);

	if (SEP_DRIVER_MAX_MESSAGE_SIZE_IN_BYTES < args->msg_len ||
	    SEP_DRIVER_MIN_MESSAGE_SIZE_IN_BYTES > args->msg_len) {
		dev_warn(&sep->pdev->dev,
			 "[PID%d] invalid message length\n",
			 current->pid);
		error = -EINVAL;
		goto end_function;
	}

	actual_count = sizeof(struct sep_fastcall_hdr)
			+ args->msg_len
			+ (args->num_dcbs * sizeof(struct build_dcb_struct));

	if (actual_count != count_user) {
		dev_warn(&sep->pdev->dev,
			 "[PID%d] inconsistent message "
			 "sizes 0x%08zX vs 0x%08zX\n",
			 current->pid, actual_count, count_user);
		error = -EMSGSIZE;
		goto end_function;
	}

end_function:
	return error;
}

/**
 *	sep_write - Starts an operation for fastcall interface
 *	@filp: File pointer
 *	@buf_user: User buffer for operation parameters
 *	@count_user: User buffer size
 *	@offset: File offset, not supported
 *
 *	The implementation does not support writing in chunks,
 *	all data must be given during a single write system call.
 */
static ssize_t sep_write(struct file *filp,
			 const char __user *buf_user, size_t count_user,
			 loff_t *offset)
{
	struct sep_private_data * const private_data = filp->private_data;
	struct sep_call_status *call_status = &private_data->call_status;
	struct sep_device *sep = private_data->device;
	struct sep_dma_context *dma_ctx = NULL;
	struct sep_fastcall_hdr call_hdr = {0};
	void *msg_region = NULL;
	void *dmatables_region = NULL;
	struct sep_dcblock *dcb_region = NULL;
	ssize_t error = 0;
	struct sep_queue_info *my_queue_elem = NULL;
	bool my_secure_dma; /* are we using secure_dma (IMR)? */

	dev_dbg(&sep->pdev->dev, "[PID%d] sep dev is 0x%p\n",
		current->pid, sep);
	dev_dbg(&sep->pdev->dev, "[PID%d] private_data is 0x%p\n",
		current->pid, private_data);

	error = sep_fastcall_args_get(sep, &call_hdr, buf_user, count_user);
	if (error)
		goto end_function;

	buf_user += sizeof(struct sep_fastcall_hdr);

	if (call_hdr.secure_dma == 0)
		my_secure_dma = false;
	else
		my_secure_dma = true;

	/*
	 * Controlling driver memory usage by limiting amount of
	 * buffers created. Only SEP_DOUBLEBUF_USERS_LIMIT number
	 * of threads can progress further at a time
	 */
	dev_dbg(&sep->pdev->dev,
		"[PID%d] waiting for double buffering region access\n",
		current->pid);
	error = down_interruptible(&sep->sep_doublebuf);
	dev_dbg(&sep->pdev->dev, "[PID%d] double buffering region start\n",
					current->pid);
	if (error) {
		/* Signal received */
		goto end_function_error;
	}


	/*
	 * Prepare contents of the shared area regions for
	 * the operation into temporary buffers
	 */
	if (0 < call_hdr.num_dcbs) {
		error = sep_create_dcb_dmatables_context(sep,
				&dcb_region,
				&dmatables_region,
				&dma_ctx,
				(const struct build_dcb_struct __user *)
					buf_user,
				call_hdr.num_dcbs, my_secure_dma);
		if (error)
			goto end_function_error_doublebuf;

		buf_user += call_hdr.num_dcbs * sizeof(struct build_dcb_struct);
	}

	error = sep_create_msgarea_context(sep,
					   &msg_region,
					   buf_user,
					   call_hdr.msg_len);
	if (error)
		goto end_function_error_doublebuf;

	dev_dbg(&sep->pdev->dev, "[PID%d] updating queue status\n",
							current->pid);
	my_queue_elem = sep_queue_status_add(sep,
				((struct sep_msgarea_hdr *)msg_region)->opcode,
				(dma_ctx) ? dma_ctx->input_data_len : 0,
				     current->pid,
				     current->comm, sizeof(current->comm));

	if (!my_queue_elem) {
		dev_dbg(&sep->pdev->dev,
			"[PID%d] updating queue status error\n", current->pid);
		error = -ENOMEM;
		goto end_function_error_doublebuf;
	}

	/* Wait until current process gets the transaction */
	error = sep_wait_transaction(sep);

	if (error) {
		/* Interrupted by signal, don't clear transaction */
		dev_dbg(&sep->pdev->dev, "[PID%d] interrupted by signal\n",
			current->pid);
		sep_queue_status_remove(sep, &my_queue_elem);
		goto end_function_error_doublebuf;
	}

	dev_dbg(&sep->pdev->dev, "[PID%d] saving queue element\n",
		current->pid);
	private_data->my_queue_elem = my_queue_elem;

	/* Activate shared area regions for the transaction */
	error = sep_activate_msgarea_context(sep, &msg_region,
					     call_hdr.msg_len);
	if (error)
		goto end_function_error_clear_transact;

	sep_dump_message(sep);

	if (0 < call_hdr.num_dcbs) {
		error = sep_activate_dcb_dmatables_context(sep,
				&dcb_region,
				&dmatables_region,
				dma_ctx);
		if (error)
			goto end_function_error_clear_transact;
	}

	/* Send command to SEP */
	error = sep_send_command_handler(sep);
	if (error)
		goto end_function_error_clear_transact;

	/* Store DMA context for the transaction */
	private_data->dma_ctx = dma_ctx;
	/* Update call status */
	set_bit(SEP_FASTCALL_WRITE_DONE_OFFSET, &call_status->status);
	error = count_user;

	up(&sep->sep_doublebuf);
	dev_dbg(&sep->pdev->dev, "[PID%d] double buffering region end\n",
		current->pid);

	goto end_function;

end_function_error_clear_transact:
	sep_end_transaction_handler(sep, &dma_ctx, call_status,
						&private_data->my_queue_elem);

end_function_error_doublebuf:
	up(&sep->sep_doublebuf);
	dev_dbg(&sep->pdev->dev, "[PID%d] double buffering region end\n",
		current->pid);

end_function_error:
	if (dma_ctx)
		sep_free_dma_table_data_handler(sep, &dma_ctx);

end_function:
	kfree(dcb_region);
	kfree(dmatables_region);
	kfree(msg_region);

	return error;
}
/**
 *	sep_seek - Handler for seek system call
 *	@filp: File pointer
 *	@offset: File offset
 *	@origin: Options for offset
 *
 *	Fastcall interface does not support seeking, all reads
 *	and writes are from/to offset zero
 */
static loff_t sep_seek(struct file *filp, loff_t offset, int origin)
{
	return -ENOSYS;
}



/**
 * sep_file_operations - file operation on sep device
 * @sep_ioctl:	ioctl handler from user space call
 * @sep_poll:	poll handler
 * @sep_open:	handles sep device open request
 * @sep_release:handles sep device release request
 * @sep_mmap:	handles memory mapping requests
 * @sep_read:	handles read request on sep device
 * @sep_write:	handles write request on sep device
 * @sep_seek:	handles seek request on sep device
 */
static const struct file_operations sep_file_operations = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = sep_ioctl,
	.poll = sep_poll,
	.open = sep_open,
	.release = sep_release,
	.mmap = sep_mmap,
	.read = sep_read,
	.write = sep_write,
	.llseek = sep_seek,
};

/**
 * sep_sysfs_read - read sysfs entry per gives arguments
 * @filp: file pointer
 * @kobj: kobject pointer
 * @attr: binary file attributes
 * @buf: read to this buffer
 * @pos: offset to read
 * @count: amount of data to read
 *
 * This function is to read sysfs entries for sep driver per given arguments.
 */
static ssize_t
sep_sysfs_read(struct file *filp, struct kobject *kobj,
		struct bin_attribute *attr,
		char *buf, loff_t pos, size_t count)
{
	unsigned long lck_flags;
	size_t nleft = count;
	struct sep_device *sep = sep_dev;
	struct sep_queue_info *queue_elem = NULL;
	u32 queue_num = 0;
	u32 i = 1;

	spin_lock_irqsave(&sep->sep_queue_lock, lck_flags);

	queue_num = sep->sep_queue_num;
	if (queue_num > SEP_DOUBLEBUF_USERS_LIMIT)
		queue_num = SEP_DOUBLEBUF_USERS_LIMIT;


	if (count < sizeof(queue_num)
			+ (queue_num * sizeof(struct sep_queue_data))) {
		spin_unlock_irqrestore(&sep->sep_queue_lock, lck_flags);
		return -EINVAL;
	}

	memcpy(buf, &queue_num, sizeof(queue_num));
	buf += sizeof(queue_num);
	nleft -= sizeof(queue_num);

	list_for_each_entry(queue_elem, &sep->sep_queue_status, list) {
		if (i++ > queue_num)
			break;

		memcpy(buf, &queue_elem->data, sizeof(queue_elem->data));
		nleft -= sizeof(queue_elem->data);
		buf += sizeof(queue_elem->data);
	}
	spin_unlock_irqrestore(&sep->sep_queue_lock, lck_flags);

	return count - nleft;
}

/**
 * bin_attributes - defines attributes for queue_status
 * @attr: attributes (name & permissions)
 * @read: function pointer to read this file
 * @size: maxinum size of binary attribute
 */
static const struct bin_attribute queue_status = {
	.attr = {.name = "queue_status", .mode = 0444},
	.read = sep_sysfs_read,
	.size = sizeof(u32)
		+ (SEP_DOUBLEBUF_USERS_LIMIT * sizeof(struct sep_queue_data)),
};

/**
 * sep_register_driver_with_fs - register misc devices
 * @sep: pointer to struct sep_device
 *
 * This function registers the driver with the file system
 */
static int sep_register_driver_with_fs(struct sep_device *sep)
{
	int ret_val;

	sep->miscdev_sep.minor = MISC_DYNAMIC_MINOR;
	sep->miscdev_sep.name = SEP_DEV_NAME;
	sep->miscdev_sep.fops = &sep_file_operations;

	ret_val = misc_register(&sep->miscdev_sep);
	if (ret_val) {
		dev_warn(&sep->pdev->dev, "misc reg fails for SEP %x\n",
			ret_val);
		return ret_val;
	}

	ret_val = device_create_bin_file(sep->miscdev_sep.this_device,
								&queue_status);
	if (ret_val) {
		dev_warn(&sep->pdev->dev, "sysfs attribute1 fails for SEP %x\n",
			ret_val);
		return ret_val;
	}

	return ret_val;
}


/**
 *sep_probe - probe a matching PCI device
 *@pdev:	pci_device
 *@ent:	pci_device_id
 *
 *Attempt to set up and configure a SEP device that has been
 *discovered by the PCI layer. Allocates all required resources.
 */
static int sep_probe(struct pci_dev *pdev,
	const struct pci_device_id *ent)
{
	int error = 0;
	struct sep_device *sep = NULL;

	if (sep_dev != NULL) {
		dev_dbg(&pdev->dev, "only one SEP supported.\n");
		return -EBUSY;
	}

	/* Enable the device */
	error = pci_enable_device(pdev);
	if (error) {
		dev_warn(&pdev->dev, "error enabling pci device\n");
		goto end_function;
	}

	/* Allocate the sep_device structure for this device */
	sep_dev = kzalloc(sizeof(struct sep_device), GFP_ATOMIC);
	if (sep_dev == NULL) {
		error = -ENOMEM;
		goto end_function_disable_device;
	}

	/*
	 * We're going to use another variable for actually
	 * working with the device; this way, if we have
	 * multiple devices in the future, it would be easier
	 * to make appropriate changes
	 */
	sep = sep_dev;

	sep->pdev = pci_dev_get(pdev);

	init_waitqueue_head(&sep->event_transactions);
	init_waitqueue_head(&sep->event_interrupt);
	spin_lock_init(&sep->snd_rply_lck);
	spin_lock_init(&sep->sep_queue_lock);
	sema_init(&sep->sep_doublebuf, SEP_DOUBLEBUF_USERS_LIMIT);

	INIT_LIST_HEAD(&sep->sep_queue_status);

	dev_dbg(&sep->pdev->dev,
		"sep probe: PCI obtained, device being prepared\n");

	/* Set up our register area */
	sep->reg_physical_addr = pci_resource_start(sep->pdev, 0);
	if (!sep->reg_physical_addr) {
		dev_warn(&sep->pdev->dev, "Error getting register start\n");
		error = -ENODEV;
		goto end_function_free_sep_dev;
	}

	sep->reg_physical_end = pci_resource_end(sep->pdev, 0);
	if (!sep->reg_physical_end) {
		dev_warn(&sep->pdev->dev, "Error getting register end\n");
		error = -ENODEV;
		goto end_function_free_sep_dev;
	}

	sep->reg_addr = ioremap_nocache(sep->reg_physical_addr,
		(size_t)(sep->reg_physical_end - sep->reg_physical_addr + 1));
	if (!sep->reg_addr) {
		dev_warn(&sep->pdev->dev, "Error getting register virtual\n");
		error = -ENODEV;
		goto end_function_free_sep_dev;
	}

	dev_dbg(&sep->pdev->dev,
		"Register area start %llx end %llx virtual %p\n",
		(unsigned long long)sep->reg_physical_addr,
		(unsigned long long)sep->reg_physical_end,
		sep->reg_addr);

	/* Allocate the shared area */
	sep->shared_size = SEP_DRIVER_MESSAGE_SHARED_AREA_SIZE_IN_BYTES +
		SYNCHRONIC_DMA_TABLES_AREA_SIZE_BYTES +
		SEP_DRIVER_DATA_POOL_SHARED_AREA_SIZE_IN_BYTES +
		SEP_DRIVER_STATIC_AREA_SIZE_IN_BYTES +
		SEP_DRIVER_SYSTEM_DATA_MEMORY_SIZE_IN_BYTES;

	if (sep_map_and_alloc_shared_area(sep)) {
		error = -ENOMEM;
		/* Allocation failed */
		goto end_function_error;
	}

	/* Clear ICR register */
	sep_write_reg(sep, HW_HOST_ICR_REG_ADDR, 0xFFFFFFFF);

	/* Set the IMR register - open only GPR 2 */
	sep_write_reg(sep, HW_HOST_IMR_REG_ADDR, (~(0x1 << 13)));

	/* Read send/receive counters from SEP */
	sep->reply_ct = sep_read_reg(sep, HW_HOST_SEP_HOST_GPR2_REG_ADDR);
	sep->reply_ct &= 0x3FFFFFFF;
	sep->send_ct = sep->reply_ct;

	/* Get the interrupt line */
	error = request_irq(pdev->irq, sep_inthandler, IRQF_SHARED,
		"sep_driver", sep);

	if (error)
		goto end_function_deallocate_sep_shared_area;

	/* The new chip requires a shared area reconfigure */
	error = sep_reconfig_shared_area(sep);
	if (error)
		goto end_function_free_irq;

	sep->in_use = 1;

	/* Finally magic up the device nodes */
	/* Register driver with the fs */
	error = sep_register_driver_with_fs(sep);

	if (error) {
		dev_err(&sep->pdev->dev, "error registering dev file\n");
		goto end_function_free_irq;
	}

	sep->in_use = 0; /* through touching the device */
#ifdef SEP_ENABLE_RUNTIME_PM
	pm_runtime_put_noidle(&sep->pdev->dev);
	pm_runtime_allow(&sep->pdev->dev);
	pm_runtime_set_autosuspend_delay(&sep->pdev->dev,
		SUSPEND_DELAY);
	pm_runtime_use_autosuspend(&sep->pdev->dev);
	pm_runtime_mark_last_busy(&sep->pdev->dev);
	sep->power_save_setup = 1;
#endif
	/* register kernel crypto driver */
#if defined(CONFIG_CRYPTO) || defined(CONFIG_CRYPTO_MODULE)
	error = sep_crypto_setup();
	if (error) {
		dev_err(&sep->pdev->dev, "crypto setup failed\n");
		goto end_function_free_irq;
	}
#endif
	goto end_function;

end_function_free_irq:
	free_irq(pdev->irq, sep);

end_function_deallocate_sep_shared_area:
	/* De-allocate shared area */
	sep_unmap_and_free_shared_area(sep);

end_function_error:
	iounmap(sep->reg_addr);

end_function_free_sep_dev:
	pci_dev_put(sep_dev->pdev);
	kfree(sep_dev);
	sep_dev = NULL;

end_function_disable_device:
	pci_disable_device(pdev);

end_function:
	return error;
}

/**
 * sep_remove -	handles removing device from pci subsystem
 * @pdev:	pointer to pci device
 *
 * This function will handle removing our sep device from pci subsystem on exit
 * or unloading this module. It should free up all used resources, and unmap if
 * any memory regions mapped.
 */
static void sep_remove(struct pci_dev *pdev)
{
	struct sep_device *sep = sep_dev;

	/* Unregister from fs */
	misc_deregister(&sep->miscdev_sep);

	/* Unregister from kernel crypto */
#if defined(CONFIG_CRYPTO) || defined(CONFIG_CRYPTO_MODULE)
	sep_crypto_takedown();
#endif
	/* Free the irq */
	free_irq(sep->pdev->irq, sep);

	/* Free the shared area  */
	sep_unmap_and_free_shared_area(sep_dev);
	iounmap(sep_dev->reg_addr);

#ifdef SEP_ENABLE_RUNTIME_PM
	if (sep->in_use) {
		sep->in_use = 0;
		pm_runtime_forbid(&sep->pdev->dev);
		pm_runtime_get_noresume(&sep->pdev->dev);
	}
#endif
	pci_dev_put(sep_dev->pdev);
	kfree(sep_dev);
	sep_dev = NULL;
}

/* Initialize struct pci_device_id for our driver */
static DEFINE_PCI_DEVICE_TABLE(sep_pci_id_tbl) = {
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x0826)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x08e9)},
	{0}
};

/* Export our pci_device_id structure to user space */
MODULE_DEVICE_TABLE(pci, sep_pci_id_tbl);

#ifdef SEP_ENABLE_RUNTIME_PM

/**
 * sep_pm_resume - rsume routine while waking up from S3 state
 * @dev:	pointer to sep device
 *
 * This function is to be used to wake up sep driver while system awakes from S3
 * state i.e. suspend to ram. The RAM in intact.
 * Notes - revisit with more understanding of pm, ICR/IMR & counters.
 */
static int sep_pci_resume(struct device *dev)
{
	struct sep_device *sep = sep_dev;

	dev_dbg(&sep->pdev->dev, "pci resume called\n");

	if (sep->power_state == SEP_DRIVER_POWERON)
		return 0;

	/* Clear ICR register */
	sep_write_reg(sep, HW_HOST_ICR_REG_ADDR, 0xFFFFFFFF);

	/* Set the IMR register - open only GPR 2 */
	sep_write_reg(sep, HW_HOST_IMR_REG_ADDR, (~(0x1 << 13)));

	/* Read send/receive counters from SEP */
	sep->reply_ct = sep_read_reg(sep, HW_HOST_SEP_HOST_GPR2_REG_ADDR);
	sep->reply_ct &= 0x3FFFFFFF;
	sep->send_ct = sep->reply_ct;

	sep->power_state = SEP_DRIVER_POWERON;

	return 0;
}

/**
 * sep_pm_suspend - suspend routine while going to S3 state
 * @dev:	pointer to sep device
 *
 * This function is to be used to suspend sep driver while system goes to S3
 * state i.e. suspend to ram. The RAM in intact and ON during this suspend.
 * Notes - revisit with more understanding of pm, ICR/IMR
 */
static int sep_pci_suspend(struct device *dev)
{
	struct sep_device *sep = sep_dev;

	dev_dbg(&sep->pdev->dev, "pci suspend called\n");
	if (sep->in_use == 1)
		return -EAGAIN;

	sep->power_state = SEP_DRIVER_POWEROFF;

	/* Clear ICR register */
	sep_write_reg(sep, HW_HOST_ICR_REG_ADDR, 0xFFFFFFFF);

	/* Set the IMR to block all */
	sep_write_reg(sep, HW_HOST_IMR_REG_ADDR, 0xFFFFFFFF);

	return 0;
}

/**
 * sep_pm_runtime_resume - runtime resume routine
 * @dev:	pointer to sep device
 *
 * Notes - revisit with more understanding of pm, ICR/IMR & counters
 */
static int sep_pm_runtime_resume(struct device *dev)
{

	u32 retval2;
	u32 delay_count;
	struct sep_device *sep = sep_dev;

	dev_dbg(&sep->pdev->dev, "pm runtime resume called\n");

	/**
	 * Wait until the SCU boot is ready
	 * This is done by iterating SCU_DELAY_ITERATION (10
	 * microseconds each) up to SCU_DELAY_MAX (50) times.
	 * This bit can be set in a random time that is less
	 * than 500 microseconds after each power resume
	 */
	retval2 = 0;
	delay_count = 0;
	while ((!retval2) && (delay_count < SCU_DELAY_MAX)) {
		retval2 = sep_read_reg(sep, HW_HOST_SEP_HOST_GPR3_REG_ADDR);
		retval2 &= 0x00000008;
		if (!retval2) {
			udelay(SCU_DELAY_ITERATION);
			delay_count += 1;
		}
	}

	if (!retval2) {
		dev_warn(&sep->pdev->dev, "scu boot bit not set at resume\n");
		return -EINVAL;
	}

	/* Clear ICR register */
	sep_write_reg(sep, HW_HOST_ICR_REG_ADDR, 0xFFFFFFFF);

	/* Set the IMR register - open only GPR 2 */
	sep_write_reg(sep, HW_HOST_IMR_REG_ADDR, (~(0x1 << 13)));

	/* Read send/receive counters from SEP */
	sep->reply_ct = sep_read_reg(sep, HW_HOST_SEP_HOST_GPR2_REG_ADDR);
	sep->reply_ct &= 0x3FFFFFFF;
	sep->send_ct = sep->reply_ct;

	return 0;
}

/**
 * sep_pm_runtime_suspend - runtime suspend routine
 * @dev:	pointer to sep device
 *
 * Notes - revisit with more understanding of pm
 */
static int sep_pm_runtime_suspend(struct device *dev)
{
	struct sep_device *sep = sep_dev;

	dev_dbg(&sep->pdev->dev, "pm runtime suspend called\n");

	/* Clear ICR register */
	sep_write_reg(sep, HW_HOST_ICR_REG_ADDR, 0xFFFFFFFF);
	return 0;
}

/**
 * sep_pm - power management for sep driver
 * @sep_pm_runtime_resume:	resume- no communication with cpu & main memory
 * @sep_pm_runtime_suspend:	suspend- no communication with cpu & main memory
 * @sep_pci_suspend:		suspend - main memory is still ON
 * @sep_pci_resume:		resume - main memory is still ON
 */
static const struct dev_pm_ops sep_pm = {
	.runtime_resume = sep_pm_runtime_resume,
	.runtime_suspend = sep_pm_runtime_suspend,
	.resume = sep_pci_resume,
	.suspend = sep_pci_suspend,
};
#endif /* SEP_ENABLE_RUNTIME_PM */

/**
 * sep_pci_driver - registers this device with pci subsystem
 * @name:	name identifier for this driver
 * @sep_pci_id_tbl:	pointer to struct pci_device_id table
 * @sep_probe:	pointer to probe function in PCI driver
 * @sep_remove:	pointer to remove function in PCI driver
 */
static struct pci_driver sep_pci_driver = {
#ifdef SEP_ENABLE_RUNTIME_PM
	.driver = {
		.pm = &sep_pm,
	},
#endif
	.name = "sep_sec_driver",
	.id_table = sep_pci_id_tbl,
	.probe = sep_probe,
	.remove = sep_remove
};

module_pci_driver(sep_pci_driver);
MODULE_LICENSE("GPL");
