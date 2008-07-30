/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2008 Silicon Graphics, Inc.  All Rights Reserved.
 */

/*
 * Cross Partition Communication (XPC) sn2-based functions.
 *
 *     Architecture specific implementation of common functions.
 *
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <asm/uncached.h>
#include <asm/sn/sn_sal.h>
#include "xpc.h"

struct xpc_vars *xpc_vars;
static struct xpc_vars_part_sn2 *xpc_vars_part; /* >>> Add _sn2 suffix? */

static enum xp_retval
xpc_rsvd_page_init_sn2(struct xpc_rsvd_page *rp)
{
	AMO_t *amos_page;
	u64 nasid_array = 0;
	int i;
	int ret;

	xpc_vars = XPC_RP_VARS(rp);

	rp->sn.vars_pa = __pa(xpc_vars);

	/* vars_part array follows immediately after vars */
	xpc_vars_part = (struct xpc_vars_part_sn2 *)((u8 *)XPC_RP_VARS(rp) +
						     XPC_RP_VARS_SIZE);


	/*
	 * Before clearing xpc_vars, see if a page of AMOs had been previously
	 * allocated. If not we'll need to allocate one and set permissions
	 * so that cross-partition AMOs are allowed.
	 *
	 * The allocated AMO page needs MCA reporting to remain disabled after
	 * XPC has unloaded.  To make this work, we keep a copy of the pointer
	 * to this page (i.e., amos_page) in the struct xpc_vars structure,
	 * which is pointed to by the reserved page, and re-use that saved copy
	 * on subsequent loads of XPC. This AMO page is never freed, and its
	 * memory protections are never restricted.
	 */
	amos_page = xpc_vars->amos_page;
	if (amos_page == NULL) {
		amos_page = (AMO_t *)TO_AMO(uncached_alloc_page(0, 1));
		if (amos_page == NULL) {
			dev_err(xpc_part, "can't allocate page of AMOs\n");
			return xpNoMemory;
		}

		/*
		 * Open up AMO-R/W to cpu.  This is done for Shub 1.1 systems
		 * when xpc_allow_IPI_ops() is called via xpc_hb_init().
		 */
		if (!enable_shub_wars_1_1()) {
			ret = sn_change_memprotect(ia64_tpa((u64)amos_page),
						   PAGE_SIZE,
						   SN_MEMPROT_ACCESS_CLASS_1,
						   &nasid_array);
			if (ret != 0) {
				dev_err(xpc_part, "can't change memory "
					"protections\n");
				uncached_free_page(__IA64_UNCACHED_OFFSET |
						   TO_PHYS((u64)amos_page), 1);
				return xpSalError;
			}
		}
	}

	/* clear xpc_vars */
	memset(xpc_vars, 0, sizeof(struct xpc_vars));

	xpc_vars->version = XPC_V_VERSION;
	xpc_vars->act_nasid = cpuid_to_nasid(0);
	xpc_vars->act_phys_cpuid = cpu_physical_id(0);
	xpc_vars->vars_part_pa = __pa(xpc_vars_part);
	xpc_vars->amos_page_pa = ia64_tpa((u64)amos_page);
	xpc_vars->amos_page = amos_page;	/* save for next load of XPC */

	/* clear xpc_vars_part */
	memset((u64 *)xpc_vars_part, 0, sizeof(struct xpc_vars_part_sn2) *
	       xp_max_npartitions);

	/* initialize the activate IRQ related AMO variables */
	for (i = 0; i < xp_nasid_mask_words; i++)
		(void)xpc_IPI_init(XPC_ACTIVATE_IRQ_AMOS + i);

	/* initialize the engaged remote partitions related AMO variables */
	(void)xpc_IPI_init(XPC_ENGAGED_PARTITIONS_AMO);
	(void)xpc_IPI_init(XPC_DISENGAGE_REQUEST_AMO);

	return xpSuccess;
}

/*
 * Setup the infrastructure necessary to support XPartition Communication
 * between the specified remote partition and the local one.
 */
static enum xp_retval
xpc_setup_infrastructure_sn2(struct xpc_partition *part)
{
	enum xp_retval retval;
	int ret;
	int cpuid;
	int ch_number;
	struct xpc_channel *ch;
	struct timer_list *timer;
	short partid = XPC_PARTID(part);

	/*
	 * Allocate all of the channel structures as a contiguous chunk of
	 * memory.
	 */
	DBUG_ON(part->channels != NULL);
	part->channels = kzalloc(sizeof(struct xpc_channel) * XPC_MAX_NCHANNELS,
				 GFP_KERNEL);
	if (part->channels == NULL) {
		dev_err(xpc_chan, "can't get memory for channels\n");
		return xpNoMemory;
	}

	/* allocate all the required GET/PUT values */

	part->local_GPs = xpc_kzalloc_cacheline_aligned(XPC_GP_SIZE,
							GFP_KERNEL,
							&part->local_GPs_base);
	if (part->local_GPs == NULL) {
		dev_err(xpc_chan, "can't get memory for local get/put "
			"values\n");
		retval = xpNoMemory;
		goto out_1;
	}

	part->remote_GPs = xpc_kzalloc_cacheline_aligned(XPC_GP_SIZE,
							 GFP_KERNEL,
							 &part->
							 remote_GPs_base);
	if (part->remote_GPs == NULL) {
		dev_err(xpc_chan, "can't get memory for remote get/put "
			"values\n");
		retval = xpNoMemory;
		goto out_2;
	}

	part->remote_GPs_pa = 0;

	/* allocate all the required open and close args */

	part->local_openclose_args =
	    xpc_kzalloc_cacheline_aligned(XPC_OPENCLOSE_ARGS_SIZE, GFP_KERNEL,
					  &part->local_openclose_args_base);
	if (part->local_openclose_args == NULL) {
		dev_err(xpc_chan, "can't get memory for local connect args\n");
		retval = xpNoMemory;
		goto out_3;
	}

	part->remote_openclose_args =
	    xpc_kzalloc_cacheline_aligned(XPC_OPENCLOSE_ARGS_SIZE, GFP_KERNEL,
					  &part->remote_openclose_args_base);
	if (part->remote_openclose_args == NULL) {
		dev_err(xpc_chan, "can't get memory for remote connect args\n");
		retval = xpNoMemory;
		goto out_4;
	}

	part->remote_openclose_args_pa = 0;

	part->local_IPI_amo_va = xpc_IPI_init(partid);
	part->local_IPI_amo = 0;
	spin_lock_init(&part->IPI_lock);

	part->remote_IPI_nasid = 0;
	part->remote_IPI_phys_cpuid = 0;
	part->remote_IPI_amo_va = NULL;

	atomic_set(&part->channel_mgr_requests, 1);
	init_waitqueue_head(&part->channel_mgr_wq);

	sprintf(part->IPI_owner, "xpc%02d", partid);
	ret = request_irq(SGI_XPC_NOTIFY, xpc_notify_IRQ_handler, IRQF_SHARED,
			  part->IPI_owner, (void *)(u64)partid);
	if (ret != 0) {
		dev_err(xpc_chan, "can't register NOTIFY IRQ handler, "
			"errno=%d\n", -ret);
		retval = xpLackOfResources;
		goto out_5;
	}

	/* Setup a timer to check for dropped IPIs */
	timer = &part->dropped_IPI_timer;
	init_timer(timer);
	timer->function = (void (*)(unsigned long))xpc_dropped_IPI_check;
	timer->data = (unsigned long)part;
	timer->expires = jiffies + XPC_P_DROPPED_IPI_WAIT_INTERVAL;
	add_timer(timer);

	part->nchannels = XPC_MAX_NCHANNELS;

	atomic_set(&part->nchannels_active, 0);
	atomic_set(&part->nchannels_engaged, 0);

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

	/* setup of infrastructure failed */
out_5:
	kfree(part->remote_openclose_args_base);
	part->remote_openclose_args = NULL;
out_4:
	kfree(part->local_openclose_args_base);
	part->local_openclose_args = NULL;
out_3:
	kfree(part->remote_GPs_base);
	part->remote_GPs = NULL;
out_2:
	kfree(part->local_GPs_base);
	part->local_GPs = NULL;
out_1:
	kfree(part->channels);
	part->channels = NULL;
	return retval;
}

/*
 * Teardown the infrastructure necessary to support XPartition Communication
 * between the specified remote partition and the local one.
 */
static void
xpc_teardown_infrastructure_sn2(struct xpc_partition *part)
{
	short partid = XPC_PARTID(part);

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
 * Create a wrapper that hides the underlying mechanism for pulling a cacheline
 * (or multiple cachelines) from a remote partition.
 *
 * src must be a cacheline aligned physical address on the remote partition.
 * dst must be a cacheline aligned virtual address on this partition.
 * cnt must be cacheline sized
 */
/* >>> Replace this function by call to xp_remote_memcpy() or bte_copy()? */
static enum xp_retval
xpc_pull_remote_cachelines_sn2(struct xpc_partition *part, void *dst,
			       const void *src, size_t cnt)
{
	enum xp_retval ret;

	DBUG_ON((u64)src != L1_CACHE_ALIGN((u64)src));
	DBUG_ON((u64)dst != L1_CACHE_ALIGN((u64)dst));
	DBUG_ON(cnt != L1_CACHE_ALIGN(cnt));

	if (part->act_state == XPC_P_DEACTIVATING)
		return part->reason;

	ret = xp_remote_memcpy(dst, src, cnt);
	if (ret != xpSuccess) {
		dev_dbg(xpc_chan, "xp_remote_memcpy() from partition %d failed,"
			" ret=%d\n", XPC_PARTID(part), ret);
	}
	return ret;
}

/*
 * Pull the remote per partition specific variables from the specified
 * partition.
 */
static enum xp_retval
xpc_pull_remote_vars_part_sn2(struct xpc_partition *part)
{
	u8 buffer[L1_CACHE_BYTES * 2];
	struct xpc_vars_part_sn2 *pulled_entry_cacheline =
	    (struct xpc_vars_part_sn2 *)L1_CACHE_ALIGN((u64)buffer);
	struct xpc_vars_part_sn2 *pulled_entry;
	u64 remote_entry_cacheline_pa, remote_entry_pa;
	short partid = XPC_PARTID(part);
	enum xp_retval ret;

	/* pull the cacheline that contains the variables we're interested in */

	DBUG_ON(part->remote_vars_part_pa !=
		L1_CACHE_ALIGN(part->remote_vars_part_pa));
	DBUG_ON(sizeof(struct xpc_vars_part_sn2) != L1_CACHE_BYTES / 2);

	remote_entry_pa = part->remote_vars_part_pa +
	    sn_partition_id * sizeof(struct xpc_vars_part_sn2);

	remote_entry_cacheline_pa = (remote_entry_pa & ~(L1_CACHE_BYTES - 1));

	pulled_entry = (struct xpc_vars_part_sn2 *)((u64)pulled_entry_cacheline
						    + (remote_entry_pa &
						    (L1_CACHE_BYTES - 1)));

	ret = xpc_pull_remote_cachelines_sn2(part, pulled_entry_cacheline,
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
 * Establish first contact with the remote partititon. This involves pulling
 * the XPC per partition variables from the remote partition and waiting for
 * the remote partition to pull ours.
 */
static enum xp_retval
xpc_make_first_contact_sn2(struct xpc_partition *part)
{
	enum xp_retval ret;

	while ((ret = xpc_pull_remote_vars_part_sn2(part)) != xpSuccess) {
		if (ret != xpRetry) {
			XPC_DEACTIVATE_PARTITION(part, ret);
			return ret;
		}

		dev_dbg(xpc_part, "waiting to make first contact with "
			"partition %d\n", XPC_PARTID(part));

		/* wait a 1/4 of a second or so */
		(void)msleep_interruptible(250);

		if (part->act_state == XPC_P_DEACTIVATING)
			return part->reason;
	}

	return xpSuccess;
}

/*
 * Get the IPI flags and pull the openclose args and/or remote GPs as needed.
 */
static u64
xpc_get_IPI_flags_sn2(struct xpc_partition *part)
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
		ret = xpc_pull_remote_cachelines_sn2(part,
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
		ret = xpc_pull_remote_cachelines_sn2(part, part->remote_GPs,
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

static struct xpc_msg *
xpc_pull_remote_msg_sn2(struct xpc_channel *ch, s64 get)
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

		ret = xpc_pull_remote_cachelines_sn2(part, msg, remote_msg,
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
xpc_get_deliverable_msg_sn2(struct xpc_channel *ch)
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

			msg = xpc_pull_remote_msg_sn2(ch, get);

			DBUG_ON(msg != NULL && msg->number != get);
			DBUG_ON(msg != NULL && (msg->flags & XPC_M_DONE));
			DBUG_ON(msg != NULL && !(msg->flags & XPC_M_READY));

			break;
		}

	} while (1);

	return msg;
}

void
xpc_init_sn2(void)
{
	xpc_rsvd_page_init = xpc_rsvd_page_init_sn2;
	xpc_setup_infrastructure = xpc_setup_infrastructure_sn2;
	xpc_teardown_infrastructure = xpc_teardown_infrastructure_sn2;
	xpc_make_first_contact = xpc_make_first_contact_sn2;
	xpc_get_IPI_flags = xpc_get_IPI_flags_sn2;
	xpc_get_deliverable_msg = xpc_get_deliverable_msg_sn2;
}

void
xpc_exit_sn2(void)
{
}
