/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2004-2008 Silicon Graphics, Inc.  All Rights Reserved.
 */

/*
 * Cross Partition Communication (XPC) channel support.
 *
 *	This is the part of XPC that manages the channels and
 *	sends/receives messages across them to/from other partitions.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/cache.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/completion.h>
#include <asm/sn/bte.h>
#include <asm/sn/sn_sal.h>
#include "xpc.h"

/*
 * Guarantee that the kzalloc'd memory is cacheline aligned.
 */
static void *
xpc_kzalloc_cacheline_aligned(size_t size, gfp_t flags, void **base)
{
	/* see if kzalloc will give us cachline aligned memory by default */
	*base = kzalloc(size, flags);
	if (*base == NULL)
		return NULL;

	if ((u64)*base == L1_CACHE_ALIGN((u64)*base))
		return *base;

	kfree(*base);

	/* nope, we'll have to do it ourselves */
	*base = kzalloc(size + L1_CACHE_BYTES, flags);
	if (*base == NULL)
		return NULL;

	return (void *)L1_CACHE_ALIGN((u64)*base);
}

/*
 * Set up the initial values for the XPartition Communication channels.
 */
static void
xpc_initialize_channels(struct xpc_partition *part, partid_t partid)
{
	int ch_number;
	struct xpc_channel *ch;

	for (ch_number = 0; ch_number < part->nchannels; ch_number++) {
		ch = &part->channels[ch_number];

		ch->partid = partid;
		ch->number = ch_number;
		ch->flags = XPC_C_DISCONNECTED;

		ch->local_GP = &part->local_GPs[ch_number];
		ch->local_openclose_args =
		    &part->local_openclose_args[ch_number];

		atomic_set(&ch->kthreads_assigned, 0);
		atomic_set(&ch->kthreads_idle, 0);
		atomic_set(&ch->kthreads_active, 0);

		atomic_set(&ch->references, 0);
		atomic_set(&ch->n_to_notify, 0);

		spin_lock_init(&ch->lock);
		mutex_init(&ch->msg_to_pull_mutex);
		init_completion(&ch->wdisconnect_wait);

		atomic_set(&ch->n_on_msg_allocate_wq, 0);
		init_waitqueue_head(&ch->msg_allocate_wq);
		init_waitqueue_head(&ch->idle_wq);
	}
}

/*
 * Setup the infrastructure necessary to support XPartition Communication
 * between the specified remote partition and the local one.
 */
enum xp_retval
xpc_setup_infrastructure(struct xpc_partition *part)
{
	int ret, cpuid;
	struct timer_list *timer;
	partid_t partid = XPC_PARTID(part);

	/*
	 * Zero out MOST of the entry for this partition. Only the fields
	 * starting with `nchannels' will be zeroed. The preceding fields must
	 * remain `viable' across partition ups and downs, since they may be
	 * referenced during this memset() operation.
	 */
	memset(&part->nchannels, 0, sizeof(struct xpc_partition) -
	       offsetof(struct xpc_partition, nchannels));

	/*
	 * Allocate all of the channel structures as a contiguous chunk of
	 * memory.
	 */
	part->channels = kzalloc(sizeof(struct xpc_channel) * XPC_NCHANNELS,
				 GFP_KERNEL);
	if (part->channels == NULL) {
		dev_err(xpc_chan, "can't get memory for channels\n");
		return xpNoMemory;
	}

	part->nchannels = XPC_NCHANNELS;

	/* allocate all the required GET/PUT values */

	part->local_GPs = xpc_kzalloc_cacheline_aligned(XPC_GP_SIZE,
							GFP_KERNEL,
							&part->local_GPs_base);
	if (part->local_GPs == NULL) {
		kfree(part->channels);
		part->channels = NULL;
		dev_err(xpc_chan, "can't get memory for local get/put "
			"values\n");
		return xpNoMemory;
	}

	part->remote_GPs = xpc_kzalloc_cacheline_aligned(XPC_GP_SIZE,
							 GFP_KERNEL,
							 &part->
							 remote_GPs_base);
	if (part->remote_GPs == NULL) {
		dev_err(xpc_chan, "can't get memory for remote get/put "
			"values\n");
		kfree(part->local_GPs_base);
		part->local_GPs = NULL;
		kfree(part->channels);
		part->channels = NULL;
		return xpNoMemory;
	}

	/* allocate all the required open and close args */

	part->local_openclose_args =
	    xpc_kzalloc_cacheline_aligned(XPC_OPENCLOSE_ARGS_SIZE, GFP_KERNEL,
					  &part->local_openclose_args_base);
	if (part->local_openclose_args == NULL) {
		dev_err(xpc_chan, "can't get memory for local connect args\n");
		kfree(part->remote_GPs_base);
		part->remote_GPs = NULL;
		kfree(part->local_GPs_base);
		part->local_GPs = NULL;
		kfree(part->channels);
		part->channels = NULL;
		return xpNoMemory;
	}

	part->remote_openclose_args =
	    xpc_kzalloc_cacheline_aligned(XPC_OPENCLOSE_ARGS_SIZE, GFP_KERNEL,
					  &part->remote_openclose_args_base);
	if (part->remote_openclose_args == NULL) {
		dev_err(xpc_chan, "can't get memory for remote connect args\n");
		kfree(part->local_openclose_args_base);
		part->local_openclose_args = NULL;
		kfree(part->remote_GPs_base);
		part->remote_GPs = NULL;
		kfree(part->local_GPs_base);
		part->local_GPs = NULL;
		kfree(part->channels);
		part->channels = NULL;
		return xpNoMemory;
	}

	xpc_initialize_channels(part, partid);

	atomic_set(&part->nchannels_active, 0);
	atomic_set(&part->nchannels_engaged, 0);

	/* local_IPI_amo were set to 0 by an earlier memset() */

	/* Initialize this partitions AMO_t structure */
	part->local_IPI_amo_va = xpc_IPI_init(partid);

	spin_lock_init(&part->IPI_lock);

	atomic_set(&part->channel_mgr_requests, 1);
	init_waitqueue_head(&part->channel_mgr_wq);

	sprintf(part->IPI_owner, "xpc%02d", partid);
	ret = request_irq(SGI_XPC_NOTIFY, xpc_notify_IRQ_handler, IRQF_SHARED,
			  part->IPI_owner, (void *)(u64)partid);
	if (ret != 0) {
		dev_err(xpc_chan, "can't register NOTIFY IRQ handler, "
			"errno=%d\n", -ret);
		kfree(part->remote_openclose_args_base);
		part->remote_openclose_args = NULL;
		kfree(part->local_openclose_args_base);
		part->local_openclose_args = NULL;
		kfree(part->remote_GPs_base);
		part->remote_GPs = NULL;
		kfree(part->local_GPs_base);
		part->local_GPs = NULL;
		kfree(part->channels);
		part->channels = NULL;
		return xpLackOfResources;
	}

	/* Setup a timer to check for dropped IPIs */
	timer = &part->dropped_IPI_timer;
	init_timer(timer);
	timer->function = (void (*)(unsigned long))xpc_dropped_IPI_check;
	timer->data = (unsigned long)part;
	timer->expires = jiffies + XPC_P_DROPPED_IPI_WAIT;
	add_timer(timer);

	/*
	 * With the setting of the partition setup_state to XPC_P_SETUP, we're
	 * declaring that this partition is ready to go.
	 */
	part->setup_state = XPC_P_SETUP;

	/*
	 * Setup the per partition specific variables required by the
	 * remote partition to establish channel connections with us.
	 *
	 * The setting of the magic # indicates that these per partition
	 * specific variables are ready to be used.
	 */
	xpc_vars_part[partid].GPs_pa = __pa(part->local_GPs);
	xpc_vars_part[partid].openclose_args_pa =
	    __pa(part->local_openclose_args);
	xpc_vars_part[partid].IPI_amo_pa = __pa(part->local_IPI_amo_va);
	cpuid = raw_smp_processor_id();	/* any CPU in this partition will do */
	xpc_vars_part[partid].IPI_nasid = cpuid_to_nasid(cpuid);
	xpc_vars_part[partid].IPI_phys_cpuid = cpu_physical_id(cpuid);
	xpc_vars_part[partid].nchannels = part->nchannels;
	xpc_vars_part[partid].magic = XPC_VP_MAGIC1;

	return xpSuccess;
}

/*
 * Create a wrapper that hides the underlying mechanism for pulling a cacheline
 * (or multiple cachelines) from a remote partition.
 *
 * src must be a cacheline aligned physical address on the remote partition.
 * dst must be a cacheline aligned virtual address on this partition.
 * cnt must be an cacheline sized
 */
static enum xp_retval
xpc_pull_remote_cachelines(struct xpc_partition *part, void *dst,
			   const void *src, size_t cnt)
{
	bte_result_t bte_ret;

	DBUG_ON((u64)src != L1_CACHE_ALIGN((u64)src));
	DBUG_ON((u64)dst != L1_CACHE_ALIGN((u64)dst));
	DBUG_ON(cnt != L1_CACHE_ALIGN(cnt));

	if (part->act_state == XPC_P_DEACTIVATING)
		return part->reason;

	bte_ret = xp_bte_copy((u64)src, (u64)dst, (u64)cnt,
			      (BTE_NORMAL | BTE_WACQUIRE), NULL);
	if (bte_ret == BTE_SUCCESS)
		return xpSuccess;

	dev_dbg(xpc_chan, "xp_bte_copy() from partition %d failed, ret=%d\n",
		XPC_PARTID(part), bte_ret);

	return xpc_map_bte_errors(bte_ret);
}

/*
 * Pull the remote per partition specific variables from the specified
 * partition.
 */
enum xp_retval
xpc_pull_remote_vars_part(struct xpc_partition *part)
{
	u8 buffer[L1_CACHE_BYTES * 2];
	struct xpc_vars_part *pulled_entry_cacheline =
	    (struct xpc_vars_part *)L1_CACHE_ALIGN((u64)buffer);
	struct xpc_vars_part *pulled_entry;
	u64 remote_entry_cacheline_pa, remote_entry_pa;
	partid_t partid = XPC_PARTID(part);
	enum xp_retval ret;

	/* pull the cacheline that contains the variables we're interested in */

	DBUG_ON(part->remote_vars_part_pa !=
		L1_CACHE_ALIGN(part->remote_vars_part_pa));
	DBUG_ON(sizeof(struct xpc_vars_part) != L1_CACHE_BYTES / 2);

	remote_entry_pa = part->remote_vars_part_pa +
	    sn_partition_id * sizeof(struct xpc_vars_part);

	remote_entry_cacheline_pa = (remote_entry_pa & ~(L1_CACHE_BYTES - 1));

	pulled_entry = (struct xpc_vars_part *)((u64)pulled_entry_cacheline +
						(remote_entry_pa &
						 (L1_CACHE_BYTES - 1)));

	ret = xpc_pull_remote_cachelines(part, pulled_entry_cacheline,
					 (void *)remote_entry_cacheline_pa,
					 L1_CACHE_BYTES);
	if (ret != xpSuccess) {
		dev_dbg(xpc_chan, "failed to pull XPC vars_part from "
			"partition %d, ret=%d\n", partid, ret);
		return ret;
	}

	/* see if they've been set up yet */

	if (pulled_entry->magic != XPC_VP_MAGIC1 &&
	    pulled_entry->magic != XPC_VP_MAGIC2) {

		if (pulled_entry->magic != 0) {
			dev_dbg(xpc_chan, "partition %d's XPC vars_part for "
				"partition %d has bad magic value (=0x%lx)\n",
				partid, sn_partition_id, pulled_entry->magic);
			return xpBadMagic;
		}

		/* they've not been initialized yet */
		return xpRetry;
	}

	if (xpc_vars_part[partid].magic == XPC_VP_MAGIC1) {

		/* validate the variables */

		if (pulled_entry->GPs_pa == 0 ||
		    pulled_entry->openclose_args_pa == 0 ||
		    pulled_entry->IPI_amo_pa == 0) {

			dev_err(xpc_chan, "partition %d's XPC vars_part for "
				"partition %d are not valid\n", partid,
				sn_partition_id);
			return xpInvalidAddress;
		}

		/* the variables we imported look to be valid */

		part->remote_GPs_pa = pulled_entry->GPs_pa;
		part->remote_openclose_args_pa =
		    pulled_entry->openclose_args_pa;
		part->remote_IPI_amo_va =
		    (AMO_t *)__va(pulled_entry->IPI_amo_pa);
		part->remote_IPI_nasid = pulled_entry->IPI_nasid;
		part->remote_IPI_phys_cpuid = pulled_entry->IPI_phys_cpuid;

		if (part->nchannels > pulled_entry->nchannels)
			part->nchannels = pulled_entry->nchannels;

		/* let the other side know that we've pulled their variables */

		xpc_vars_part[partid].magic = XPC_VP_MAGIC2;
	}

	if (pulled_entry->magic == XPC_VP_MAGIC1)
		return xpRetry;

	return xpSuccess;
}

/*
 * Get the IPI flags and pull the openclose args and/or remote GPs as needed.
 */
static u64
xpc_get_IPI_flags(struct xpc_partition *part)
{
	unsigned long irq_flags;
	u64 IPI_amo;
	enum xp_retval ret;

	/*
	 * See if there are any IPI flags to be handled.
	 */

	spin_lock_irqsave(&part->IPI_lock, irq_flags);
	IPI_amo = part->local_IPI_amo;
	if (IPI_amo != 0)
		part->local_IPI_amo = 0;

	spin_unlock_irqrestore(&part->IPI_lock, irq_flags);

	if (XPC_ANY_OPENCLOSE_IPI_FLAGS_SET(IPI_amo)) {
		ret = xpc_pull_remote_cachelines(part,
						 part->remote_openclose_args,
						 (void *)part->
						 remote_openclose_args_pa,
						 XPC_OPENCLOSE_ARGS_SIZE);
		if (ret != xpSuccess) {
			XPC_DEACTIVATE_PARTITION(part, ret);

			dev_dbg(xpc_chan, "failed to pull openclose args from "
				"partition %d, ret=%d\n", XPC_PARTID(part),
				ret);

			/* don't bother processing IPIs anymore */
			IPI_amo = 0;
		}
	}

	if (XPC_ANY_MSG_IPI_FLAGS_SET(IPI_amo)) {
		ret = xpc_pull_remote_cachelines(part, part->remote_GPs,
						 (void *)part->remote_GPs_pa,
						 XPC_GP_SIZE);
		if (ret != xpSuccess) {
			XPC_DEACTIVATE_PARTITION(part, ret);

			dev_dbg(xpc_chan, "failed to pull GPs from partition "
				"%d, ret=%d\n", XPC_PARTID(part), ret);

			/* don't bother processing IPIs anymore */
			IPI_amo = 0;
		}
	}

	return IPI_amo;
}

/*
 * Allocate the local message queue and the notify queue.
 */
static enum xp_retval
xpc_allocate_local_msgqueue(struct xpc_channel *ch)
{
	unsigned long irq_flags;
	int nentries;
	size_t nbytes;

	for (nentries = ch->local_nentries; nentries > 0; nentries--) {

		nbytes = nentries * ch->msg_size;
		ch->local_msgqueue = xpc_kzalloc_cacheline_aligned(nbytes,
								   GFP_KERNEL,
						      &ch->local_msgqueue_base);
		if (ch->local_msgqueue == NULL)
			continue;

		nbytes = nentries * sizeof(struct xpc_notify);
		ch->notify_queue = kzalloc(nbytes, GFP_KERNEL);
		if (ch->notify_queue == NULL) {
			kfree(ch->local_msgqueue_base);
			ch->local_msgqueue = NULL;
			continue;
		}

		spin_lock_irqsave(&ch->lock, irq_flags);
		if (nentries < ch->local_nentries) {
			dev_dbg(xpc_chan, "nentries=%d local_nentries=%d, "
				"partid=%d, channel=%d\n", nentries,
				ch->local_nentries, ch->partid, ch->number);

			ch->local_nentries = nentries;
		}
		spin_unlock_irqrestore(&ch->lock, irq_flags);
		return xpSuccess;
	}

	dev_dbg(xpc_chan, "can't get memory for local message queue and notify "
		"queue, partid=%d, channel=%d\n", ch->partid, ch->number);
	return xpNoMemory;
}

/*
 * Allocate the cached remote message queue.
 */
static enum xp_retval
xpc_allocate_remote_msgqueue(struct xpc_channel *ch)
{
	unsigned long irq_flags;
	int nentries;
	size_t nbytes;

	DBUG_ON(ch->remote_nentries <= 0);

	for (nentries = ch->remote_nentries; nentries > 0; nentries--) {

		nbytes = nentries * ch->msg_size;
		ch->remote_msgqueue = xpc_kzalloc_cacheline_aligned(nbytes,
								    GFP_KERNEL,
						     &ch->remote_msgqueue_base);
		if (ch->remote_msgqueue == NULL)
			continue;

		spin_lock_irqsave(&ch->lock, irq_flags);
		if (nentries < ch->remote_nentries) {
			dev_dbg(xpc_chan, "nentries=%d remote_nentries=%d, "
				"partid=%d, channel=%d\n", nentries,
				ch->remote_nentries, ch->partid, ch->number);

			ch->remote_nentries = nentries;
		}
		spin_unlock_irqrestore(&ch->lock, irq_flags);
		return xpSuccess;
	}

	dev_dbg(xpc_chan, "can't get memory for cached remote message queue, "
		"partid=%d, channel=%d\n", ch->partid, ch->number);
	return xpNoMemory;
}

/*
 * Allocate message queues and other stuff associated with a channel.
 *
 * Note: Assumes all of the channel sizes are filled in.
 */
static enum xp_retval
xpc_allocate_msgqueues(struct xpc_channel *ch)
{
	unsigned long irq_flags;
	enum xp_retval ret;

	DBUG_ON(ch->flags & XPC_C_SETUP);

	ret = xpc_allocate_local_msgqueue(ch);
	if (ret != xpSuccess)
		return ret;

	ret = xpc_allocate_remote_msgqueue(ch);
	if (ret != xpSuccess) {
		kfree(ch->local_msgqueue_base);
		ch->local_msgqueue = NULL;
		kfree(ch->notify_queue);
		ch->notify_queue = NULL;
		return ret;
	}

	spin_lock_irqsave(&ch->lock, irq_flags);
	ch->flags |= XPC_C_SETUP;
	spin_unlock_irqrestore(&ch->lock, irq_flags);

	return xpSuccess;
}

/*
 * Process a connect message from a remote partition.
 *
 * Note: xpc_process_connect() is expecting to be called with the
 * spin_lock_irqsave held and will leave it locked upon return.
 */
static void
xpc_process_connect(struct xpc_channel *ch, unsigned long *irq_flags)
{
	enum xp_retval ret;

	DBUG_ON(!spin_is_locked(&ch->lock));

	if (!(ch->flags & XPC_C_OPENREQUEST) ||
	    !(ch->flags & XPC_C_ROPENREQUEST)) {
		/* nothing more to do for now */
		return;
	}
	DBUG_ON(!(ch->flags & XPC_C_CONNECTING));

	if (!(ch->flags & XPC_C_SETUP)) {
		spin_unlock_irqrestore(&ch->lock, *irq_flags);
		ret = xpc_allocate_msgqueues(ch);
		spin_lock_irqsave(&ch->lock, *irq_flags);

		if (ret != xpSuccess)
			XPC_DISCONNECT_CHANNEL(ch, ret, irq_flags);

		if (ch->flags & (XPC_C_CONNECTED | XPC_C_DISCONNECTING))
			return;

		DBUG_ON(!(ch->flags & XPC_C_SETUP));
		DBUG_ON(ch->local_msgqueue == NULL);
		DBUG_ON(ch->remote_msgqueue == NULL);
	}

	if (!(ch->flags & XPC_C_OPENREPLY)) {
		ch->flags |= XPC_C_OPENREPLY;
		xpc_IPI_send_openreply(ch, irq_flags);
	}

	if (!(ch->flags & XPC_C_ROPENREPLY))
		return;

	DBUG_ON(ch->remote_msgqueue_pa == 0);

	ch->flags = (XPC_C_CONNECTED | XPC_C_SETUP);	/* clear all else */

	dev_info(xpc_chan, "channel %d to partition %d connected\n",
		 ch->number, ch->partid);

	spin_unlock_irqrestore(&ch->lock, *irq_flags);
	xpc_create_kthreads(ch, 1, 0);
	spin_lock_irqsave(&ch->lock, *irq_flags);
}

/*
 * Notify those who wanted to be notified upon delivery of their message.
 */
static void
xpc_notify_senders(struct xpc_channel *ch, enum xp_retval reason, s64 put)
{
	struct xpc_notify *notify;
	u8 notify_type;
	s64 get = ch->w_remote_GP.get - 1;

	while (++get < put && atomic_read(&ch->n_to_notify) > 0) {

		notify = &ch->notify_queue[get % ch->local_nentries];

		/*
		 * See if the notify entry indicates it was associated with
		 * a message who's sender wants to be notified. It is possible
		 * that it is, but someone else is doing or has done the
		 * notification.
		 */
		notify_type = notify->type;
		if (notify_type == 0 ||
		    cmpxchg(&notify->type, notify_type, 0) != notify_type) {
			continue;
		}

		DBUG_ON(notify_type != XPC_N_CALL);

		atomic_dec(&ch->n_to_notify);

		if (notify->func != NULL) {
			dev_dbg(xpc_chan, "notify->func() called, notify=0x%p, "
				"msg_number=%ld, partid=%d, channel=%d\n",
				(void *)notify, get, ch->partid, ch->number);

			notify->func(reason, ch->partid, ch->number,
				     notify->key);

			dev_dbg(xpc_chan, "notify->func() returned, "
				"notify=0x%p, msg_number=%ld, partid=%d, "
				"channel=%d\n", (void *)notify, get,
				ch->partid, ch->number);
		}
	}
}

/*
 * Free up message queues and other stuff that were allocated for the specified
 * channel.
 *
 * Note: ch->reason and ch->reason_line are left set for debugging purposes,
 * they're cleared when XPC_C_DISCONNECTED is cleared.
 */
static void
xpc_free_msgqueues(struct xpc_channel *ch)
{
	DBUG_ON(!spin_is_locked(&ch->lock));
	DBUG_ON(atomic_read(&ch->n_to_notify) != 0);

	ch->remote_msgqueue_pa = 0;
	ch->func = NULL;
	ch->key = NULL;
	ch->msg_size = 0;
	ch->local_nentries = 0;
	ch->remote_nentries = 0;
	ch->kthreads_assigned_limit = 0;
	ch->kthreads_idle_limit = 0;

	ch->local_GP->get = 0;
	ch->local_GP->put = 0;
	ch->remote_GP.get = 0;
	ch->remote_GP.put = 0;
	ch->w_local_GP.get = 0;
	ch->w_local_GP.put = 0;
	ch->w_remote_GP.get = 0;
	ch->w_remote_GP.put = 0;
	ch->next_msg_to_pull = 0;

	if (ch->flags & XPC_C_SETUP) {
		ch->flags &= ~XPC_C_SETUP;

		dev_dbg(xpc_chan, "ch->flags=0x%x, partid=%d, channel=%d\n",
			ch->flags, ch->partid, ch->number);

		kfree(ch->local_msgqueue_base);
		ch->local_msgqueue = NULL;
		kfree(ch->remote_msgqueue_base);
		ch->remote_msgqueue = NULL;
		kfree(ch->notify_queue);
		ch->notify_queue = NULL;
	}
}

/*
 * spin_lock_irqsave() is expected to be held on entry.
 */
static void
xpc_process_disconnect(struct xpc_channel *ch, unsigned long *irq_flags)
{
	struct xpc_partition *part = &xpc_partitions[ch->partid];
	u32 channel_was_connected = (ch->flags & XPC_C_WASCONNECTED);

	DBUG_ON(!spin_is_locked(&ch->lock));

	if (!(ch->flags & XPC_C_DISCONNECTING))
		return;

	DBUG_ON(!(ch->flags & XPC_C_CLOSEREQUEST));

	/* make sure all activity has settled down first */

	if (atomic_read(&ch->kthreads_assigned) > 0 ||
	    atomic_read(&ch->references) > 0) {
		return;
	}
	DBUG_ON((ch->flags & XPC_C_CONNECTEDCALLOUT_MADE) &&
		!(ch->flags & XPC_C_DISCONNECTINGCALLOUT_MADE));

	if (part->act_state == XPC_P_DEACTIVATING) {
		/* can't proceed until the other side disengages from us */
		if (xpc_partition_engaged(1UL << ch->partid))
			return;

	} else {

		/* as long as the other side is up do the full protocol */

		if (!(ch->flags & XPC_C_RCLOSEREQUEST))
			return;

		if (!(ch->flags & XPC_C_CLOSEREPLY)) {
			ch->flags |= XPC_C_CLOSEREPLY;
			xpc_IPI_send_closereply(ch, irq_flags);
		}

		if (!(ch->flags & XPC_C_RCLOSEREPLY))
			return;
	}

	/* wake those waiting for notify completion */
	if (atomic_read(&ch->n_to_notify) > 0) {
		/* >>> we do callout while holding ch->lock */
		xpc_notify_senders(ch, ch->reason, ch->w_local_GP.put);
	}

	/* both sides are disconnected now */

	if (ch->flags & XPC_C_DISCONNECTINGCALLOUT_MADE) {
		spin_unlock_irqrestore(&ch->lock, *irq_flags);
		xpc_disconnect_callout(ch, xpDisconnected);
		spin_lock_irqsave(&ch->lock, *irq_flags);
	}

	/* it's now safe to free the channel's message queues */
	xpc_free_msgqueues(ch);

	/* mark disconnected, clear all other flags except XPC_C_WDISCONNECT */
	ch->flags = (XPC_C_DISCONNECTED | (ch->flags & XPC_C_WDISCONNECT));

	atomic_dec(&part->nchannels_active);

	if (channel_was_connected) {
		dev_info(xpc_chan, "channel %d to partition %d disconnected, "
			 "reason=%d\n", ch->number, ch->partid, ch->reason);
	}

	if (ch->flags & XPC_C_WDISCONNECT) {
		/* we won't lose the CPU since we're holding ch->lock */
		complete(&ch->wdisconnect_wait);
	} else if (ch->delayed_IPI_flags) {
		if (part->act_state != XPC_P_DEACTIVATING) {
			/* time to take action on any delayed IPI flags */
			spin_lock(&part->IPI_lock);
			XPC_SET_IPI_FLAGS(part->local_IPI_amo, ch->number,
					  ch->delayed_IPI_flags);
			spin_unlock(&part->IPI_lock);
		}
		ch->delayed_IPI_flags = 0;
	}
}

/*
 * Process a change in the channel's remote connection state.
 */
static void
xpc_process_openclose_IPI(struct xpc_partition *part, int ch_number,
			  u8 IPI_flags)
{
	unsigned long irq_flags;
	struct xpc_openclose_args *args =
	    &part->remote_openclose_args[ch_number];
	struct xpc_channel *ch = &part->channels[ch_number];
	enum xp_retval reason;

	spin_lock_irqsave(&ch->lock, irq_flags);

again:

	if ((ch->flags & XPC_C_DISCONNECTED) &&
	    (ch->flags & XPC_C_WDISCONNECT)) {
		/*
		 * Delay processing IPI flags until thread waiting disconnect
		 * has had a chance to see that the channel is disconnected.
		 */
		ch->delayed_IPI_flags |= IPI_flags;
		spin_unlock_irqrestore(&ch->lock, irq_flags);
		return;
	}

	if (IPI_flags & XPC_IPI_CLOSEREQUEST) {

		dev_dbg(xpc_chan, "XPC_IPI_CLOSEREQUEST (reason=%d) received "
			"from partid=%d, channel=%d\n", args->reason,
			ch->partid, ch->number);

		/*
		 * If RCLOSEREQUEST is set, we're probably waiting for
		 * RCLOSEREPLY. We should find it and a ROPENREQUEST packed
		 * with this RCLOSEREQUEST in the IPI_flags.
		 */

		if (ch->flags & XPC_C_RCLOSEREQUEST) {
			DBUG_ON(!(ch->flags & XPC_C_DISCONNECTING));
			DBUG_ON(!(ch->flags & XPC_C_CLOSEREQUEST));
			DBUG_ON(!(ch->flags & XPC_C_CLOSEREPLY));
			DBUG_ON(ch->flags & XPC_C_RCLOSEREPLY);

			DBUG_ON(!(IPI_flags & XPC_IPI_CLOSEREPLY));
			IPI_flags &= ~XPC_IPI_CLOSEREPLY;
			ch->flags |= XPC_C_RCLOSEREPLY;

			/* both sides have finished disconnecting */
			xpc_process_disconnect(ch, &irq_flags);
			DBUG_ON(!(ch->flags & XPC_C_DISCONNECTED));
			goto again;
		}

		if (ch->flags & XPC_C_DISCONNECTED) {
			if (!(IPI_flags & XPC_IPI_OPENREQUEST)) {
				if ((XPC_GET_IPI_FLAGS(part->local_IPI_amo,
						       ch_number) &
				     XPC_IPI_OPENREQUEST)) {

					DBUG_ON(ch->delayed_IPI_flags != 0);
					spin_lock(&part->IPI_lock);
					XPC_SET_IPI_FLAGS(part->local_IPI_amo,
							  ch_number,
							  XPC_IPI_CLOSEREQUEST);
					spin_unlock(&part->IPI_lock);
				}
				spin_unlock_irqrestore(&ch->lock, irq_flags);
				return;
			}

			XPC_SET_REASON(ch, 0, 0);
			ch->flags &= ~XPC_C_DISCONNECTED;

			atomic_inc(&part->nchannels_active);
			ch->flags |= (XPC_C_CONNECTING | XPC_C_ROPENREQUEST);
		}

		IPI_flags &= ~(XPC_IPI_OPENREQUEST | XPC_IPI_OPENREPLY);

		/*
		 * The meaningful CLOSEREQUEST connection state fields are:
		 *      reason = reason connection is to be closed
		 */

		ch->flags |= XPC_C_RCLOSEREQUEST;

		if (!(ch->flags & XPC_C_DISCONNECTING)) {
			reason = args->reason;
			if (reason <= xpSuccess || reason > xpUnknownReason)
				reason = xpUnknownReason;
			else if (reason == xpUnregistering)
				reason = xpOtherUnregistering;

			XPC_DISCONNECT_CHANNEL(ch, reason, &irq_flags);

			DBUG_ON(IPI_flags & XPC_IPI_CLOSEREPLY);
			spin_unlock_irqrestore(&ch->lock, irq_flags);
			return;
		}

		xpc_process_disconnect(ch, &irq_flags);
	}

	if (IPI_flags & XPC_IPI_CLOSEREPLY) {

		dev_dbg(xpc_chan, "XPC_IPI_CLOSEREPLY received from partid=%d,"
			" channel=%d\n", ch->partid, ch->number);

		if (ch->flags & XPC_C_DISCONNECTED) {
			DBUG_ON(part->act_state != XPC_P_DEACTIVATING);
			spin_unlock_irqrestore(&ch->lock, irq_flags);
			return;
		}

		DBUG_ON(!(ch->flags & XPC_C_CLOSEREQUEST));

		if (!(ch->flags & XPC_C_RCLOSEREQUEST)) {
			if ((XPC_GET_IPI_FLAGS(part->local_IPI_amo, ch_number)
			     & XPC_IPI_CLOSEREQUEST)) {

				DBUG_ON(ch->delayed_IPI_flags != 0);
				spin_lock(&part->IPI_lock);
				XPC_SET_IPI_FLAGS(part->local_IPI_amo,
						  ch_number,
						  XPC_IPI_CLOSEREPLY);
				spin_unlock(&part->IPI_lock);
			}
			spin_unlock_irqrestore(&ch->lock, irq_flags);
			return;
		}

		ch->flags |= XPC_C_RCLOSEREPLY;

		if (ch->flags & XPC_C_CLOSEREPLY) {
			/* both sides have finished disconnecting */
			xpc_process_disconnect(ch, &irq_flags);
		}
	}

	if (IPI_flags & XPC_IPI_OPENREQUEST) {

		dev_dbg(xpc_chan, "XPC_IPI_OPENREQUEST (msg_size=%d, "
			"local_nentries=%d) received from partid=%d, "
			"channel=%d\n", args->msg_size, args->local_nentries,
			ch->partid, ch->number);

		if (part->act_state == XPC_P_DEACTIVATING ||
		    (ch->flags & XPC_C_ROPENREQUEST)) {
			spin_unlock_irqrestore(&ch->lock, irq_flags);
			return;
		}

		if (ch->flags & (XPC_C_DISCONNECTING | XPC_C_WDISCONNECT)) {
			ch->delayed_IPI_flags |= XPC_IPI_OPENREQUEST;
			spin_unlock_irqrestore(&ch->lock, irq_flags);
			return;
		}
		DBUG_ON(!(ch->flags & (XPC_C_DISCONNECTED |
				       XPC_C_OPENREQUEST)));
		DBUG_ON(ch->flags & (XPC_C_ROPENREQUEST | XPC_C_ROPENREPLY |
				     XPC_C_OPENREPLY | XPC_C_CONNECTED));

		/*
		 * The meaningful OPENREQUEST connection state fields are:
		 *      msg_size = size of channel's messages in bytes
		 *      local_nentries = remote partition's local_nentries
		 */
		if (args->msg_size == 0 || args->local_nentries == 0) {
			/* assume OPENREQUEST was delayed by mistake */
			spin_unlock_irqrestore(&ch->lock, irq_flags);
			return;
		}

		ch->flags |= (XPC_C_ROPENREQUEST | XPC_C_CONNECTING);
		ch->remote_nentries = args->local_nentries;

		if (ch->flags & XPC_C_OPENREQUEST) {
			if (args->msg_size != ch->msg_size) {
				XPC_DISCONNECT_CHANNEL(ch, xpUnequalMsgSizes,
						       &irq_flags);
				spin_unlock_irqrestore(&ch->lock, irq_flags);
				return;
			}
		} else {
			ch->msg_size = args->msg_size;

			XPC_SET_REASON(ch, 0, 0);
			ch->flags &= ~XPC_C_DISCONNECTED;

			atomic_inc(&part->nchannels_active);
		}

		xpc_process_connect(ch, &irq_flags);
	}

	if (IPI_flags & XPC_IPI_OPENREPLY) {

		dev_dbg(xpc_chan, "XPC_IPI_OPENREPLY (local_msgqueue_pa=0x%lx, "
			"local_nentries=%d, remote_nentries=%d) received from "
			"partid=%d, channel=%d\n", args->local_msgqueue_pa,
			args->local_nentries, args->remote_nentries,
			ch->partid, ch->number);

		if (ch->flags & (XPC_C_DISCONNECTING | XPC_C_DISCONNECTED)) {
			spin_unlock_irqrestore(&ch->lock, irq_flags);
			return;
		}
		if (!(ch->flags & XPC_C_OPENREQUEST)) {
			XPC_DISCONNECT_CHANNEL(ch, xpOpenCloseError,
					       &irq_flags);
			spin_unlock_irqrestore(&ch->lock, irq_flags);
			return;
		}

		DBUG_ON(!(ch->flags & XPC_C_ROPENREQUEST));
		DBUG_ON(ch->flags & XPC_C_CONNECTED);

		/*
		 * The meaningful OPENREPLY connection state fields are:
		 *      local_msgqueue_pa = physical address of remote
		 *                          partition's local_msgqueue
		 *      local_nentries = remote partition's local_nentries
		 *      remote_nentries = remote partition's remote_nentries
		 */
		DBUG_ON(args->local_msgqueue_pa == 0);
		DBUG_ON(args->local_nentries == 0);
		DBUG_ON(args->remote_nentries == 0);

		ch->flags |= XPC_C_ROPENREPLY;
		ch->remote_msgqueue_pa = args->local_msgqueue_pa;

		if (args->local_nentries < ch->remote_nentries) {
			dev_dbg(xpc_chan, "XPC_IPI_OPENREPLY: new "
				"remote_nentries=%d, old remote_nentries=%d, "
				"partid=%d, channel=%d\n",
				args->local_nentries, ch->remote_nentries,
				ch->partid, ch->number);

			ch->remote_nentries = args->local_nentries;
		}
		if (args->remote_nentries < ch->local_nentries) {
			dev_dbg(xpc_chan, "XPC_IPI_OPENREPLY: new "
				"local_nentries=%d, old local_nentries=%d, "
				"partid=%d, channel=%d\n",
				args->remote_nentries, ch->local_nentries,
				ch->partid, ch->number);

			ch->local_nentries = args->remote_nentries;
		}

		xpc_process_connect(ch, &irq_flags);
	}

	spin_unlock_irqrestore(&ch->lock, irq_flags);
}

/*
 * Attempt to establish a channel connection to a remote partition.
 */
static enum xp_retval
xpc_connect_channel(struct xpc_channel *ch)
{
	unsigned long irq_flags;
	struct xpc_registration *registration = &xpc_registrations[ch->number];

	if (mutex_trylock(&registration->mutex) == 0)
		return xpRetry;

	if (!XPC_CHANNEL_REGISTERED(ch->number)) {
		mutex_unlock(&registration->mutex);
		return xpUnregistered;
	}

	spin_lock_irqsave(&ch->lock, irq_flags);

	DBUG_ON(ch->flags & XPC_C_CONNECTED);
	DBUG_ON(ch->flags & XPC_C_OPENREQUEST);

	if (ch->flags & XPC_C_DISCONNECTING) {
		spin_unlock_irqrestore(&ch->lock, irq_flags);
		mutex_unlock(&registration->mutex);
		return ch->reason;
	}

	/* add info from the channel connect registration to the channel */

	ch->kthreads_assigned_limit = registration->assigned_limit;
	ch->kthreads_idle_limit = registration->idle_limit;
	DBUG_ON(atomic_read(&ch->kthreads_assigned) != 0);
	DBUG_ON(atomic_read(&ch->kthreads_idle) != 0);
	DBUG_ON(atomic_read(&ch->kthreads_active) != 0);

	ch->func = registration->func;
	DBUG_ON(registration->func == NULL);
	ch->key = registration->key;

	ch->local_nentries = registration->nentries;

	if (ch->flags & XPC_C_ROPENREQUEST) {
		if (registration->msg_size != ch->msg_size) {
			/* the local and remote sides aren't the same */

			/*
			 * Because XPC_DISCONNECT_CHANNEL() can block we're
			 * forced to up the registration sema before we unlock
			 * the channel lock. But that's okay here because we're
			 * done with the part that required the registration
			 * sema. XPC_DISCONNECT_CHANNEL() requires that the
			 * channel lock be locked and will unlock and relock
			 * the channel lock as needed.
			 */
			mutex_unlock(&registration->mutex);
			XPC_DISCONNECT_CHANNEL(ch, xpUnequalMsgSizes,
					       &irq_flags);
			spin_unlock_irqrestore(&ch->lock, irq_flags);
			return xpUnequalMsgSizes;
		}
	} else {
		ch->msg_size = registration->msg_size;

		XPC_SET_REASON(ch, 0, 0);
		ch->flags &= ~XPC_C_DISCONNECTED;

		atomic_inc(&xpc_partitions[ch->partid].nchannels_active);
	}

	mutex_unlock(&registration->mutex);

	/* initiate the connection */

	ch->flags |= (XPC_C_OPENREQUEST | XPC_C_CONNECTING);
	xpc_IPI_send_openrequest(ch, &irq_flags);

	xpc_process_connect(ch, &irq_flags);

	spin_unlock_irqrestore(&ch->lock, irq_flags);

	return xpSuccess;
}

/*
 * Clear some of the msg flags in the local message queue.
 */
static inline void
xpc_clear_local_msgqueue_flags(struct xpc_channel *ch)
{
	struct xpc_msg *msg;
	s64 get;

	get = ch->w_remote_GP.get;
	do {
		msg = (struct xpc_msg *)((u64)ch->local_msgqueue +
					 (get % ch->local_nentries) *
					 ch->msg_size);
		msg->flags = 0;
	} while (++get < ch->remote_GP.get);
}

/*
 * Clear some of the msg flags in the remote message queue.
 */
static inline void
xpc_clear_remote_msgqueue_flags(struct xpc_channel *ch)
{
	struct xpc_msg *msg;
	s64 put;

	put = ch->w_remote_GP.put;
	do {
		msg = (struct xpc_msg *)((u64)ch->remote_msgqueue +
					 (put % ch->remote_nentries) *
					 ch->msg_size);
		msg->flags = 0;
	} while (++put < ch->remote_GP.put);
}

static void
xpc_process_msg_IPI(struct xpc_partition *part, int ch_number)
{
	struct xpc_channel *ch = &part->channels[ch_number];
	int nmsgs_sent;

	ch->remote_GP = part->remote_GPs[ch_number];

	/* See what, if anything, has changed for each connected channel */

	xpc_msgqueue_ref(ch);

	if (ch->w_remote_GP.get == ch->remote_GP.get &&
	    ch->w_remote_GP.put == ch->remote_GP.put) {
		/* nothing changed since GPs were last pulled */
		xpc_msgqueue_deref(ch);
		return;
	}

	if (!(ch->flags & XPC_C_CONNECTED)) {
		xpc_msgqueue_deref(ch);
		return;
	}

	/*
	 * First check to see if messages recently sent by us have been
	 * received by the other side. (The remote GET value will have
	 * changed since we last looked at it.)
	 */

	if (ch->w_remote_GP.get != ch->remote_GP.get) {

		/*
		 * We need to notify any senders that want to be notified
		 * that their sent messages have been received by their
		 * intended recipients. We need to do this before updating
		 * w_remote_GP.get so that we don't allocate the same message
		 * queue entries prematurely (see xpc_allocate_msg()).
		 */
		if (atomic_read(&ch->n_to_notify) > 0) {
			/*
			 * Notify senders that messages sent have been
			 * received and delivered by the other side.
			 */
			xpc_notify_senders(ch, xpMsgDelivered,
					   ch->remote_GP.get);
		}

		/*
		 * Clear msg->flags in previously sent messages, so that
		 * they're ready for xpc_allocate_msg().
		 */
		xpc_clear_local_msgqueue_flags(ch);

		ch->w_remote_GP.get = ch->remote_GP.get;

		dev_dbg(xpc_chan, "w_remote_GP.get changed to %ld, partid=%d, "
			"channel=%d\n", ch->w_remote_GP.get, ch->partid,
			ch->number);

		/*
		 * If anyone was waiting for message queue entries to become
		 * available, wake them up.
		 */
		if (atomic_read(&ch->n_on_msg_allocate_wq) > 0)
			wake_up(&ch->msg_allocate_wq);
	}

	/*
	 * Now check for newly sent messages by the other side. (The remote
	 * PUT value will have changed since we last looked at it.)
	 */

	if (ch->w_remote_GP.put != ch->remote_GP.put) {
		/*
		 * Clear msg->flags in previously received messages, so that
		 * they're ready for xpc_get_deliverable_msg().
		 */
		xpc_clear_remote_msgqueue_flags(ch);

		ch->w_remote_GP.put = ch->remote_GP.put;

		dev_dbg(xpc_chan, "w_remote_GP.put changed to %ld, partid=%d, "
			"channel=%d\n", ch->w_remote_GP.put, ch->partid,
			ch->number);

		nmsgs_sent = ch->w_remote_GP.put - ch->w_local_GP.get;
		if (nmsgs_sent > 0) {
			dev_dbg(xpc_chan, "msgs waiting to be copied and "
				"delivered=%d, partid=%d, channel=%d\n",
				nmsgs_sent, ch->partid, ch->number);

			if (ch->flags & XPC_C_CONNECTEDCALLOUT_MADE)
				xpc_activate_kthreads(ch, nmsgs_sent);
		}
	}

	xpc_msgqueue_deref(ch);
}

void
xpc_process_channel_activity(struct xpc_partition *part)
{
	unsigned long irq_flags;
	u64 IPI_amo, IPI_flags;
	struct xpc_channel *ch;
	int ch_number;
	u32 ch_flags;

	IPI_amo = xpc_get_IPI_flags(part);

	/*
	 * Initiate channel connections for registered channels.
	 *
	 * For each connected channel that has pending messages activate idle
	 * kthreads and/or create new kthreads as needed.
	 */

	for (ch_number = 0; ch_number < part->nchannels; ch_number++) {
		ch = &part->channels[ch_number];

		/*
		 * Process any open or close related IPI flags, and then deal
		 * with connecting or disconnecting the channel as required.
		 */

		IPI_flags = XPC_GET_IPI_FLAGS(IPI_amo, ch_number);

		if (XPC_ANY_OPENCLOSE_IPI_FLAGS_SET(IPI_flags))
			xpc_process_openclose_IPI(part, ch_number, IPI_flags);

		ch_flags = ch->flags;	/* need an atomic snapshot of flags */

		if (ch_flags & XPC_C_DISCONNECTING) {
			spin_lock_irqsave(&ch->lock, irq_flags);
			xpc_process_disconnect(ch, &irq_flags);
			spin_unlock_irqrestore(&ch->lock, irq_flags);
			continue;
		}

		if (part->act_state == XPC_P_DEACTIVATING)
			continue;

		if (!(ch_flags & XPC_C_CONNECTED)) {
			if (!(ch_flags & XPC_C_OPENREQUEST)) {
				DBUG_ON(ch_flags & XPC_C_SETUP);
				(void)xpc_connect_channel(ch);
			} else {
				spin_lock_irqsave(&ch->lock, irq_flags);
				xpc_process_connect(ch, &irq_flags);
				spin_unlock_irqrestore(&ch->lock, irq_flags);
			}
			continue;
		}

		/*
		 * Process any message related IPI flags, this may involve the
		 * activation of kthreads to deliver any pending messages sent
		 * from the other partition.
		 */

		if (XPC_ANY_MSG_IPI_FLAGS_SET(IPI_flags))
			xpc_process_msg_IPI(part, ch_number);
	}
}

/*
 * XPC's heartbeat code calls this function to inform XPC that a partition is
 * going down.  XPC responds by tearing down the XPartition Communication
 * infrastructure used for the just downed partition.
 *
 * XPC's heartbeat code will never call this function and xpc_partition_up()
 * at the same time. Nor will it ever make multiple calls to either function
 * at the same time.
 */
void
xpc_partition_going_down(struct xpc_partition *part, enum xp_retval reason)
{
	unsigned long irq_flags;
	int ch_number;
	struct xpc_channel *ch;

	dev_dbg(xpc_chan, "deactivating partition %d, reason=%d\n",
		XPC_PARTID(part), reason);

	if (!xpc_part_ref(part)) {
		/* infrastructure for this partition isn't currently set up */
		return;
	}

	/* disconnect channels associated with the partition going down */

	for (ch_number = 0; ch_number < part->nchannels; ch_number++) {
		ch = &part->channels[ch_number];

		xpc_msgqueue_ref(ch);
		spin_lock_irqsave(&ch->lock, irq_flags);

		XPC_DISCONNECT_CHANNEL(ch, reason, &irq_flags);

		spin_unlock_irqrestore(&ch->lock, irq_flags);
		xpc_msgqueue_deref(ch);
	}

	xpc_wakeup_channel_mgr(part);

	xpc_part_deref(part);
}

/*
 * Teardown the infrastructure necessary to support XPartition Communication
 * between the specified remote partition and the local one.
 */
void
xpc_teardown_infrastructure(struct xpc_partition *part)
{
	partid_t partid = XPC_PARTID(part);

	/*
	 * We start off by making this partition inaccessible to local
	 * processes by marking it as no longer setup. Then we make it
	 * inaccessible to remote processes by clearing the XPC per partition
	 * specific variable's magic # (which indicates that these variables
	 * are no longer valid) and by ignoring all XPC notify IPIs sent to
	 * this partition.
	 */

	DBUG_ON(atomic_read(&part->nchannels_engaged) != 0);
	DBUG_ON(atomic_read(&part->nchannels_active) != 0);
	DBUG_ON(part->setup_state != XPC_P_SETUP);
	part->setup_state = XPC_P_WTEARDOWN;

	xpc_vars_part[partid].magic = 0;

	free_irq(SGI_XPC_NOTIFY, (void *)(u64)partid);

	/*
	 * Before proceeding with the teardown we have to wait until all
	 * existing references cease.
	 */
	wait_event(part->teardown_wq, (atomic_read(&part->references) == 0));

	/* now we can begin tearing down the infrastructure */

	part->setup_state = XPC_P_TORNDOWN;

	/* in case we've still got outstanding timers registered... */
	del_timer_sync(&part->dropped_IPI_timer);

	kfree(part->remote_openclose_args_base);
	part->remote_openclose_args = NULL;
	kfree(part->local_openclose_args_base);
	part->local_openclose_args = NULL;
	kfree(part->remote_GPs_base);
	part->remote_GPs = NULL;
	kfree(part->local_GPs_base);
	part->local_GPs = NULL;
	kfree(part->channels);
	part->channels = NULL;
	part->local_IPI_amo_va = NULL;
}

/*
 * Called by XP at the time of channel connection registration to cause
 * XPC to establish connections to all currently active partitions.
 */
void
xpc_initiate_connect(int ch_number)
{
	partid_t partid;
	struct xpc_partition *part;
	struct xpc_channel *ch;

	DBUG_ON(ch_number < 0 || ch_number >= XPC_NCHANNELS);

	for (partid = 1; partid < XP_MAX_PARTITIONS; partid++) {
		part = &xpc_partitions[partid];

		if (xpc_part_ref(part)) {
			ch = &part->channels[ch_number];

			/*
			 * Initiate the establishment of a connection on the
			 * newly registered channel to the remote partition.
			 */
			xpc_wakeup_channel_mgr(part);
			xpc_part_deref(part);
		}
	}
}

void
xpc_connected_callout(struct xpc_channel *ch)
{
	/* let the registerer know that a connection has been established */

	if (ch->func != NULL) {
		dev_dbg(xpc_chan, "ch->func() called, reason=xpConnected, "
			"partid=%d, channel=%d\n", ch->partid, ch->number);

		ch->func(xpConnected, ch->partid, ch->number,
			 (void *)(u64)ch->local_nentries, ch->key);

		dev_dbg(xpc_chan, "ch->func() returned, reason=xpConnected, "
			"partid=%d, channel=%d\n", ch->partid, ch->number);
	}
}

/*
 * Called by XP at the time of channel connection unregistration to cause
 * XPC to teardown all current connections for the specified channel.
 *
 * Before returning xpc_initiate_disconnect() will wait until all connections
 * on the specified channel have been closed/torndown. So the caller can be
 * assured that they will not be receiving any more callouts from XPC to the
 * function they registered via xpc_connect().
 *
 * Arguments:
 *
 *	ch_number - channel # to unregister.
 */
void
xpc_initiate_disconnect(int ch_number)
{
	unsigned long irq_flags;
	partid_t partid;
	struct xpc_partition *part;
	struct xpc_channel *ch;

	DBUG_ON(ch_number < 0 || ch_number >= XPC_NCHANNELS);

	/* initiate the channel disconnect for every active partition */
	for (partid = 1; partid < XP_MAX_PARTITIONS; partid++) {
		part = &xpc_partitions[partid];

		if (xpc_part_ref(part)) {
			ch = &part->channels[ch_number];
			xpc_msgqueue_ref(ch);

			spin_lock_irqsave(&ch->lock, irq_flags);

			if (!(ch->flags & XPC_C_DISCONNECTED)) {
				ch->flags |= XPC_C_WDISCONNECT;

				XPC_DISCONNECT_CHANNEL(ch, xpUnregistering,
						       &irq_flags);
			}

			spin_unlock_irqrestore(&ch->lock, irq_flags);

			xpc_msgqueue_deref(ch);
			xpc_part_deref(part);
		}
	}

	xpc_disconnect_wait(ch_number);
}

/*
 * To disconnect a channel, and reflect it back to all who may be waiting.
 *
 * An OPEN is not allowed until XPC_C_DISCONNECTING is cleared by
 * xpc_process_disconnect(), and if set, XPC_C_WDISCONNECT is cleared by
 * xpc_disconnect_wait().
 *
 * THE CHANNEL IS TO BE LOCKED BY THE CALLER AND WILL REMAIN LOCKED UPON RETURN.
 */
void
xpc_disconnect_channel(const int line, struct xpc_channel *ch,
		       enum xp_retval reason, unsigned long *irq_flags)
{
	u32 channel_was_connected = (ch->flags & XPC_C_CONNECTED);

	DBUG_ON(!spin_is_locked(&ch->lock));

	if (ch->flags & (XPC_C_DISCONNECTING | XPC_C_DISCONNECTED))
		return;

	DBUG_ON(!(ch->flags & (XPC_C_CONNECTING | XPC_C_CONNECTED)));

	dev_dbg(xpc_chan, "reason=%d, line=%d, partid=%d, channel=%d\n",
		reason, line, ch->partid, ch->number);

	XPC_SET_REASON(ch, reason, line);

	ch->flags |= (XPC_C_CLOSEREQUEST | XPC_C_DISCONNECTING);
	/* some of these may not have been set */
	ch->flags &= ~(XPC_C_OPENREQUEST | XPC_C_OPENREPLY |
		       XPC_C_ROPENREQUEST | XPC_C_ROPENREPLY |
		       XPC_C_CONNECTING | XPC_C_CONNECTED);

	xpc_IPI_send_closerequest(ch, irq_flags);

	if (channel_was_connected)
		ch->flags |= XPC_C_WASCONNECTED;

	spin_unlock_irqrestore(&ch->lock, *irq_flags);

	/* wake all idle kthreads so they can exit */
	if (atomic_read(&ch->kthreads_idle) > 0) {
		wake_up_all(&ch->idle_wq);

	} else if ((ch->flags & XPC_C_CONNECTEDCALLOUT_MADE) &&
		   !(ch->flags & XPC_C_DISCONNECTINGCALLOUT)) {
		/* start a kthread that will do the xpDisconnecting callout */
		xpc_create_kthreads(ch, 1, 1);
	}

	/* wake those waiting to allocate an entry from the local msg queue */
	if (atomic_read(&ch->n_on_msg_allocate_wq) > 0)
		wake_up(&ch->msg_allocate_wq);

	spin_lock_irqsave(&ch->lock, *irq_flags);
}

void
xpc_disconnect_callout(struct xpc_channel *ch, enum xp_retval reason)
{
	/*
	 * Let the channel's registerer know that the channel is being
	 * disconnected. We don't want to do this if the registerer was never
	 * informed of a connection being made.
	 */

	if (ch->func != NULL) {
		dev_dbg(xpc_chan, "ch->func() called, reason=%d, partid=%d, "
			"channel=%d\n", reason, ch->partid, ch->number);

		ch->func(reason, ch->partid, ch->number, NULL, ch->key);

		dev_dbg(xpc_chan, "ch->func() returned, reason=%d, partid=%d, "
			"channel=%d\n", reason, ch->partid, ch->number);
	}
}

/*
 * Wait for a message entry to become available for the specified channel,
 * but don't wait any longer than 1 jiffy.
 */
static enum xp_retval
xpc_allocate_msg_wait(struct xpc_channel *ch)
{
	enum xp_retval ret;

	if (ch->flags & XPC_C_DISCONNECTING) {
		DBUG_ON(ch->reason == xpInterrupted);
		return ch->reason;
	}

	atomic_inc(&ch->n_on_msg_allocate_wq);
	ret = interruptible_sleep_on_timeout(&ch->msg_allocate_wq, 1);
	atomic_dec(&ch->n_on_msg_allocate_wq);

	if (ch->flags & XPC_C_DISCONNECTING) {
		ret = ch->reason;
		DBUG_ON(ch->reason == xpInterrupted);
	} else if (ret == 0) {
		ret = xpTimeout;
	} else {
		ret = xpInterrupted;
	}

	return ret;
}

/*
 * Allocate an entry for a message from the message queue associated with the
 * specified channel.
 */
static enum xp_retval
xpc_allocate_msg(struct xpc_channel *ch, u32 flags,
		 struct xpc_msg **address_of_msg)
{
	struct xpc_msg *msg;
	enum xp_retval ret;
	s64 put;

	/* this reference will be dropped in xpc_send_msg() */
	xpc_msgqueue_ref(ch);

	if (ch->flags & XPC_C_DISCONNECTING) {
		xpc_msgqueue_deref(ch);
		return ch->reason;
	}
	if (!(ch->flags & XPC_C_CONNECTED)) {
		xpc_msgqueue_deref(ch);
		return xpNotConnected;
	}

	/*
	 * Get the next available message entry from the local message queue.
	 * If none are available, we'll make sure that we grab the latest
	 * GP values.
	 */
	ret = xpTimeout;

	while (1) {

		put = ch->w_local_GP.put;
		rmb();	/* guarantee that .put loads before .get */
		if (put - ch->w_remote_GP.get < ch->local_nentries) {

			/* There are available message entries. We need to try
			 * to secure one for ourselves. We'll do this by trying
			 * to increment w_local_GP.put as long as someone else
			 * doesn't beat us to it. If they do, we'll have to
			 * try again.
			 */
			if (cmpxchg(&ch->w_local_GP.put, put, put + 1) == put) {
				/* we got the entry referenced by put */
				break;
			}
			continue;	/* try again */
		}

		/*
		 * There aren't any available msg entries at this time.
		 *
		 * In waiting for a message entry to become available,
		 * we set a timeout in case the other side is not
		 * sending completion IPIs. This lets us fake an IPI
		 * that will cause the IPI handler to fetch the latest
		 * GP values as if an IPI was sent by the other side.
		 */
		if (ret == xpTimeout)
			xpc_IPI_send_local_msgrequest(ch);

		if (flags & XPC_NOWAIT) {
			xpc_msgqueue_deref(ch);
			return xpNoWait;
		}

		ret = xpc_allocate_msg_wait(ch);
		if (ret != xpInterrupted && ret != xpTimeout) {
			xpc_msgqueue_deref(ch);
			return ret;
		}
	}

	/* get the message's address and initialize it */
	msg = (struct xpc_msg *)((u64)ch->local_msgqueue +
				 (put % ch->local_nentries) * ch->msg_size);

	DBUG_ON(msg->flags != 0);
	msg->number = put;

	dev_dbg(xpc_chan, "w_local_GP.put changed to %ld; msg=0x%p, "
		"msg_number=%ld, partid=%d, channel=%d\n", put + 1,
		(void *)msg, msg->number, ch->partid, ch->number);

	*address_of_msg = msg;

	return xpSuccess;
}

/*
 * Allocate an entry for a message from the message queue associated with the
 * specified channel. NOTE that this routine can sleep waiting for a message
 * entry to become available. To not sleep, pass in the XPC_NOWAIT flag.
 *
 * Arguments:
 *
 *	partid - ID of partition to which the channel is connected.
 *	ch_number - channel #.
 *	flags - see xpc.h for valid flags.
 *	payload - address of the allocated payload area pointer (filled in on
 * 	          return) in which the user-defined message is constructed.
 */
enum xp_retval
xpc_initiate_allocate(partid_t partid, int ch_number, u32 flags, void **payload)
{
	struct xpc_partition *part = &xpc_partitions[partid];
	enum xp_retval ret = xpUnknownReason;
	struct xpc_msg *msg = NULL;

	DBUG_ON(partid <= 0 || partid >= XP_MAX_PARTITIONS);
	DBUG_ON(ch_number < 0 || ch_number >= part->nchannels);

	*payload = NULL;

	if (xpc_part_ref(part)) {
		ret = xpc_allocate_msg(&part->channels[ch_number], flags, &msg);
		xpc_part_deref(part);

		if (msg != NULL)
			*payload = &msg->payload;
	}

	return ret;
}

/*
 * Now we actually send the messages that are ready to be sent by advancing
 * the local message queue's Put value and then send an IPI to the recipient
 * partition.
 */
static void
xpc_send_msgs(struct xpc_channel *ch, s64 initial_put)
{
	struct xpc_msg *msg;
	s64 put = initial_put + 1;
	int send_IPI = 0;

	while (1) {

		while (1) {
			if (put == ch->w_local_GP.put)
				break;

			msg = (struct xpc_msg *)((u64)ch->local_msgqueue +
						 (put % ch->local_nentries) *
						 ch->msg_size);

			if (!(msg->flags & XPC_M_READY))
				break;

			put++;
		}

		if (put == initial_put) {
			/* nothing's changed */
			break;
		}

		if (cmpxchg_rel(&ch->local_GP->put, initial_put, put) !=
		    initial_put) {
			/* someone else beat us to it */
			DBUG_ON(ch->local_GP->put < initial_put);
			break;
		}

		/* we just set the new value of local_GP->put */

		dev_dbg(xpc_chan, "local_GP->put changed to %ld, partid=%d, "
			"channel=%d\n", put, ch->partid, ch->number);

		send_IPI = 1;

		/*
		 * We need to ensure that the message referenced by
		 * local_GP->put is not XPC_M_READY or that local_GP->put
		 * equals w_local_GP.put, so we'll go have a look.
		 */
		initial_put = put;
	}

	if (send_IPI)
		xpc_IPI_send_msgrequest(ch);
}

/*
 * Common code that does the actual sending of the message by advancing the
 * local message queue's Put value and sends an IPI to the partition the
 * message is being sent to.
 */
static enum xp_retval
xpc_send_msg(struct xpc_channel *ch, struct xpc_msg *msg, u8 notify_type,
	     xpc_notify_func func, void *key)
{
	enum xp_retval ret = xpSuccess;
	struct xpc_notify *notify = notify;
	s64 put, msg_number = msg->number;

	DBUG_ON(notify_type == XPC_N_CALL && func == NULL);
	DBUG_ON((((u64)msg - (u64)ch->local_msgqueue) / ch->msg_size) !=
		msg_number % ch->local_nentries);
	DBUG_ON(msg->flags & XPC_M_READY);

	if (ch->flags & XPC_C_DISCONNECTING) {
		/* drop the reference grabbed in xpc_allocate_msg() */
		xpc_msgqueue_deref(ch);
		return ch->reason;
	}

	if (notify_type != 0) {
		/*
		 * Tell the remote side to send an ACK interrupt when the
		 * message has been delivered.
		 */
		msg->flags |= XPC_M_INTERRUPT;

		atomic_inc(&ch->n_to_notify);

		notify = &ch->notify_queue[msg_number % ch->local_nentries];
		notify->func = func;
		notify->key = key;
		notify->type = notify_type;

		/* >>> is a mb() needed here? */

		if (ch->flags & XPC_C_DISCONNECTING) {
			/*
			 * An error occurred between our last error check and
			 * this one. We will try to clear the type field from
			 * the notify entry. If we succeed then
			 * xpc_disconnect_channel() didn't already process
			 * the notify entry.
			 */
			if (cmpxchg(&notify->type, notify_type, 0) ==
			    notify_type) {
				atomic_dec(&ch->n_to_notify);
				ret = ch->reason;
			}

			/* drop the reference grabbed in xpc_allocate_msg() */
			xpc_msgqueue_deref(ch);
			return ret;
		}
	}

	msg->flags |= XPC_M_READY;

	/*
	 * The preceding store of msg->flags must occur before the following
	 * load of ch->local_GP->put.
	 */
	mb();

	/* see if the message is next in line to be sent, if so send it */

	put = ch->local_GP->put;
	if (put == msg_number)
		xpc_send_msgs(ch, put);

	/* drop the reference grabbed in xpc_allocate_msg() */
	xpc_msgqueue_deref(ch);
	return ret;
}

/*
 * Send a message previously allocated using xpc_initiate_allocate() on the
 * specified channel connected to the specified partition.
 *
 * This routine will not wait for the message to be received, nor will
 * notification be given when it does happen. Once this routine has returned
 * the message entry allocated via xpc_initiate_allocate() is no longer
 * accessable to the caller.
 *
 * This routine, although called by users, does not call xpc_part_ref() to
 * ensure that the partition infrastructure is in place. It relies on the
 * fact that we called xpc_msgqueue_ref() in xpc_allocate_msg().
 *
 * Arguments:
 *
 *	partid - ID of partition to which the channel is connected.
 *	ch_number - channel # to send message on.
 *	payload - pointer to the payload area allocated via
 *			xpc_initiate_allocate().
 */
enum xp_retval
xpc_initiate_send(partid_t partid, int ch_number, void *payload)
{
	struct xpc_partition *part = &xpc_partitions[partid];
	struct xpc_msg *msg = XPC_MSG_ADDRESS(payload);
	enum xp_retval ret;

	dev_dbg(xpc_chan, "msg=0x%p, partid=%d, channel=%d\n", (void *)msg,
		partid, ch_number);

	DBUG_ON(partid <= 0 || partid >= XP_MAX_PARTITIONS);
	DBUG_ON(ch_number < 0 || ch_number >= part->nchannels);
	DBUG_ON(msg == NULL);

	ret = xpc_send_msg(&part->channels[ch_number], msg, 0, NULL, NULL);

	return ret;
}

/*
 * Send a message previously allocated using xpc_initiate_allocate on the
 * specified channel connected to the specified partition.
 *
 * This routine will not wait for the message to be sent. Once this routine
 * has returned the message entry allocated via xpc_initiate_allocate() is no
 * longer accessable to the caller.
 *
 * Once the remote end of the channel has received the message, the function
 * passed as an argument to xpc_initiate_send_notify() will be called. This
 * allows the sender to free up or re-use any buffers referenced by the
 * message, but does NOT mean the message has been processed at the remote
 * end by a receiver.
 *
 * If this routine returns an error, the caller's function will NOT be called.
 *
 * This routine, although called by users, does not call xpc_part_ref() to
 * ensure that the partition infrastructure is in place. It relies on the
 * fact that we called xpc_msgqueue_ref() in xpc_allocate_msg().
 *
 * Arguments:
 *
 *	partid - ID of partition to which the channel is connected.
 *	ch_number - channel # to send message on.
 *	payload - pointer to the payload area allocated via
 *			xpc_initiate_allocate().
 *	func - function to call with asynchronous notification of message
 *		  receipt. THIS FUNCTION MUST BE NON-BLOCKING.
 *	key - user-defined key to be passed to the function when it's called.
 */
enum xp_retval
xpc_initiate_send_notify(partid_t partid, int ch_number, void *payload,
			 xpc_notify_func func, void *key)
{
	struct xpc_partition *part = &xpc_partitions[partid];
	struct xpc_msg *msg = XPC_MSG_ADDRESS(payload);
	enum xp_retval ret;

	dev_dbg(xpc_chan, "msg=0x%p, partid=%d, channel=%d\n", (void *)msg,
		partid, ch_number);

	DBUG_ON(partid <= 0 || partid >= XP_MAX_PARTITIONS);
	DBUG_ON(ch_number < 0 || ch_number >= part->nchannels);
	DBUG_ON(msg == NULL);
	DBUG_ON(func == NULL);

	ret = xpc_send_msg(&part->channels[ch_number], msg, XPC_N_CALL,
			   func, key);
	return ret;
}

static struct xpc_msg *
xpc_pull_remote_msg(struct xpc_channel *ch, s64 get)
{
	struct xpc_partition *part = &xpc_partitions[ch->partid];
	struct xpc_msg *remote_msg, *msg;
	u32 msg_index, nmsgs;
	u64 msg_offset;
	enum xp_retval ret;

	if (mutex_lock_interruptible(&ch->msg_to_pull_mutex) != 0) {
		/* we were interrupted by a signal */
		return NULL;
	}

	while (get >= ch->next_msg_to_pull) {

		/* pull as many messages as are ready and able to be pulled */

		msg_index = ch->next_msg_to_pull % ch->remote_nentries;

		DBUG_ON(ch->next_msg_to_pull >= ch->w_remote_GP.put);
		nmsgs = ch->w_remote_GP.put - ch->next_msg_to_pull;
		if (msg_index + nmsgs > ch->remote_nentries) {
			/* ignore the ones that wrap the msg queue for now */
			nmsgs = ch->remote_nentries - msg_index;
		}

		msg_offset = msg_index * ch->msg_size;
		msg = (struct xpc_msg *)((u64)ch->remote_msgqueue + msg_offset);
		remote_msg = (struct xpc_msg *)(ch->remote_msgqueue_pa +
						msg_offset);

		ret = xpc_pull_remote_cachelines(part, msg, remote_msg,
						 nmsgs * ch->msg_size);
		if (ret != xpSuccess) {

			dev_dbg(xpc_chan, "failed to pull %d msgs starting with"
				" msg %ld from partition %d, channel=%d, "
				"ret=%d\n", nmsgs, ch->next_msg_to_pull,
				ch->partid, ch->number, ret);

			XPC_DEACTIVATE_PARTITION(part, ret);

			mutex_unlock(&ch->msg_to_pull_mutex);
			return NULL;
		}

		ch->next_msg_to_pull += nmsgs;
	}

	mutex_unlock(&ch->msg_to_pull_mutex);

	/* return the message we were looking for */
	msg_offset = (get % ch->remote_nentries) * ch->msg_size;
	msg = (struct xpc_msg *)((u64)ch->remote_msgqueue + msg_offset);

	return msg;
}

/*
 * Get a message to be delivered.
 */
static struct xpc_msg *
xpc_get_deliverable_msg(struct xpc_channel *ch)
{
	struct xpc_msg *msg = NULL;
	s64 get;

	do {
		if (ch->flags & XPC_C_DISCONNECTING)
			break;

		get = ch->w_local_GP.get;
		rmb();	/* guarantee that .get loads before .put */
		if (get == ch->w_remote_GP.put)
			break;

		/* There are messages waiting to be pulled and delivered.
		 * We need to try to secure one for ourselves. We'll do this
		 * by trying to increment w_local_GP.get and hope that no one
		 * else beats us to it. If they do, we'll we'll simply have
		 * to try again for the next one.
		 */

		if (cmpxchg(&ch->w_local_GP.get, get, get + 1) == get) {
			/* we got the entry referenced by get */

			dev_dbg(xpc_chan, "w_local_GP.get changed to %ld, "
				"partid=%d, channel=%d\n", get + 1,
				ch->partid, ch->number);

			/* pull the message from the remote partition */

			msg = xpc_pull_remote_msg(ch, get);

			DBUG_ON(msg != NULL && msg->number != get);
			DBUG_ON(msg != NULL && (msg->flags & XPC_M_DONE));
			DBUG_ON(msg != NULL && !(msg->flags & XPC_M_READY));

			break;
		}

	} while (1);

	return msg;
}

/*
 * Deliver a message to its intended recipient.
 */
void
xpc_deliver_msg(struct xpc_channel *ch)
{
	struct xpc_msg *msg;

	msg = xpc_get_deliverable_msg(ch);
	if (msg != NULL) {

		/*
		 * This ref is taken to protect the payload itself from being
		 * freed before the user is finished with it, which the user
		 * indicates by calling xpc_initiate_received().
		 */
		xpc_msgqueue_ref(ch);

		atomic_inc(&ch->kthreads_active);

		if (ch->func != NULL) {
			dev_dbg(xpc_chan, "ch->func() called, msg=0x%p, "
				"msg_number=%ld, partid=%d, channel=%d\n",
				(void *)msg, msg->number, ch->partid,
				ch->number);

			/* deliver the message to its intended recipient */
			ch->func(xpMsgReceived, ch->partid, ch->number,
				 &msg->payload, ch->key);

			dev_dbg(xpc_chan, "ch->func() returned, msg=0x%p, "
				"msg_number=%ld, partid=%d, channel=%d\n",
				(void *)msg, msg->number, ch->partid,
				ch->number);
		}

		atomic_dec(&ch->kthreads_active);
	}
}

/*
 * Now we actually acknowledge the messages that have been delivered and ack'd
 * by advancing the cached remote message queue's Get value and if requested
 * send an IPI to the message sender's partition.
 */
static void
xpc_acknowledge_msgs(struct xpc_channel *ch, s64 initial_get, u8 msg_flags)
{
	struct xpc_msg *msg;
	s64 get = initial_get + 1;
	int send_IPI = 0;

	while (1) {

		while (1) {
			if (get == ch->w_local_GP.get)
				break;

			msg = (struct xpc_msg *)((u64)ch->remote_msgqueue +
						 (get % ch->remote_nentries) *
						 ch->msg_size);

			if (!(msg->flags & XPC_M_DONE))
				break;

			msg_flags |= msg->flags;
			get++;
		}

		if (get == initial_get) {
			/* nothing's changed */
			break;
		}

		if (cmpxchg_rel(&ch->local_GP->get, initial_get, get) !=
		    initial_get) {
			/* someone else beat us to it */
			DBUG_ON(ch->local_GP->get <= initial_get);
			break;
		}

		/* we just set the new value of local_GP->get */

		dev_dbg(xpc_chan, "local_GP->get changed to %ld, partid=%d, "
			"channel=%d\n", get, ch->partid, ch->number);

		send_IPI = (msg_flags & XPC_M_INTERRUPT);

		/*
		 * We need to ensure that the message referenced by
		 * local_GP->get is not XPC_M_DONE or that local_GP->get
		 * equals w_local_GP.get, so we'll go have a look.
		 */
		initial_get = get;
	}

	if (send_IPI)
		xpc_IPI_send_msgrequest(ch);
}

/*
 * Acknowledge receipt of a delivered message.
 *
 * If a message has XPC_M_INTERRUPT set, send an interrupt to the partition
 * that sent the message.
 *
 * This function, although called by users, does not call xpc_part_ref() to
 * ensure that the partition infrastructure is in place. It relies on the
 * fact that we called xpc_msgqueue_ref() in xpc_deliver_msg().
 *
 * Arguments:
 *
 *	partid - ID of partition to which the channel is connected.
 *	ch_number - channel # message received on.
 *	payload - pointer to the payload area allocated via
 *			xpc_initiate_allocate().
 */
void
xpc_initiate_received(partid_t partid, int ch_number, void *payload)
{
	struct xpc_partition *part = &xpc_partitions[partid];
	struct xpc_channel *ch;
	struct xpc_msg *msg = XPC_MSG_ADDRESS(payload);
	s64 get, msg_number = msg->number;

	DBUG_ON(partid <= 0 || partid >= XP_MAX_PARTITIONS);
	DBUG_ON(ch_number < 0 || ch_number >= part->nchannels);

	ch = &part->channels[ch_number];

	dev_dbg(xpc_chan, "msg=0x%p, msg_number=%ld, partid=%d, channel=%d\n",
		(void *)msg, msg_number, ch->partid, ch->number);

	DBUG_ON((((u64)msg - (u64)ch->remote_msgqueue) / ch->msg_size) !=
		msg_number % ch->remote_nentries);
	DBUG_ON(msg->flags & XPC_M_DONE);

	msg->flags |= XPC_M_DONE;

	/*
	 * The preceding store of msg->flags must occur before the following
	 * load of ch->local_GP->get.
	 */
	mb();

	/*
	 * See if this message is next in line to be acknowledged as having
	 * been delivered.
	 */
	get = ch->local_GP->get;
	if (get == msg_number)
		xpc_acknowledge_msgs(ch, get, msg->flags);

	/* the call to xpc_msgqueue_ref() was done by xpc_deliver_msg()  */
	xpc_msgqueue_deref(ch);
}
