/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2004-2005 Silicon Graphics, Inc.  All Rights Reserved.
 */


/*
 * Cross Partition Communication (XPC) support - standard version.
 *
 *	XPC provides a message passing capability that crosses partition
 *	boundaries. This module is made up of two parts:
 *
 *	    partition	This part detects the presence/absence of other
 *			partitions. It provides a heartbeat and monitors
 *			the heartbeats of other partitions.
 *
 *	    channel	This part manages the channels and sends/receives
 *			messages across them to/from other partitions.
 *
 *	There are a couple of additional functions residing in XP, which
 *	provide an interface to XPC for its users.
 *
 *
 *	Caveats:
 *
 *	  . We currently have no way to determine which nasid an IPI came
 *	    from. Thus, xpc_IPI_send() does a remote AMO write followed by
 *	    an IPI. The AMO indicates where data is to be pulled from, so
 *	    after the IPI arrives, the remote partition checks the AMO word.
 *	    The IPI can actually arrive before the AMO however, so other code
 *	    must periodically check for this case. Also, remote AMO operations
 *	    do not reliably time out. Thus we do a remote PIO read solely to
 *	    know whether the remote partition is down and whether we should
 *	    stop sending IPIs to it. This remote PIO read operation is set up
 *	    in a special nofault region so SAL knows to ignore (and cleanup)
 *	    any errors due to the remote AMO write, PIO read, and/or PIO
 *	    write operations.
 *
 *	    If/when new hardware solves this IPI problem, we should abandon
 *	    the current approach.
 *
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/syscalls.h>
#include <linux/cache.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <asm/sn/intr.h>
#include <asm/sn/sn_sal.h>
#include <asm/uaccess.h>
#include "xpc.h"


/* define two XPC debug device structures to be used with dev_dbg() et al */

struct device_driver xpc_dbg_name = {
	.name = "xpc"
};

struct device xpc_part_dbg_subname = {
	.bus_id = {0},		/* set to "part" at xpc_init() time */
	.driver = &xpc_dbg_name
};

struct device xpc_chan_dbg_subname = {
	.bus_id = {0},		/* set to "chan" at xpc_init() time */
	.driver = &xpc_dbg_name
};

struct device *xpc_part = &xpc_part_dbg_subname;
struct device *xpc_chan = &xpc_chan_dbg_subname;


/* systune related variables for /proc/sys directories */

static int xpc_hb_min = 1;
static int xpc_hb_max = 10;

static int xpc_hb_check_min = 10;
static int xpc_hb_check_max = 120;

static ctl_table xpc_sys_xpc_hb_dir[] = {
	{
		1,
		"hb_interval",
		&xpc_hb_interval,
		sizeof(int),
		0644,
		NULL,
		&proc_dointvec_minmax,
		&sysctl_intvec,
		NULL,
		&xpc_hb_min, &xpc_hb_max
	},
	{
		2,
		"hb_check_interval",
		&xpc_hb_check_interval,
		sizeof(int),
		0644,
		NULL,
		&proc_dointvec_minmax,
		&sysctl_intvec,
		NULL,
		&xpc_hb_check_min, &xpc_hb_check_max
	},
	{0}
};
static ctl_table xpc_sys_xpc_dir[] = {
	{
		1,
		"hb",
		NULL,
		0,
		0555,
		xpc_sys_xpc_hb_dir
	},
	{0}
};
static ctl_table xpc_sys_dir[] = {
	{
		1,
		"xpc",
		NULL,
		0,
		0555,
		xpc_sys_xpc_dir
	},
	{0}
};
static struct ctl_table_header *xpc_sysctl;


/* #of IRQs received */
static atomic_t xpc_act_IRQ_rcvd;

/* IRQ handler notifies this wait queue on receipt of an IRQ */
static DECLARE_WAIT_QUEUE_HEAD(xpc_act_IRQ_wq);

static unsigned long xpc_hb_check_timeout;

/* xpc_hb_checker thread exited notification */
static DECLARE_MUTEX_LOCKED(xpc_hb_checker_exited);

/* xpc_discovery thread exited notification */
static DECLARE_MUTEX_LOCKED(xpc_discovery_exited);


static struct timer_list xpc_hb_timer;


static void xpc_kthread_waitmsgs(struct xpc_partition *, struct xpc_channel *);


/*
 * Notify the heartbeat check thread that an IRQ has been received.
 */
static irqreturn_t
xpc_act_IRQ_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	atomic_inc(&xpc_act_IRQ_rcvd);
	wake_up_interruptible(&xpc_act_IRQ_wq);
	return IRQ_HANDLED;
}


/*
 * Timer to produce the heartbeat.  The timer structures function is
 * already set when this is initially called.  A tunable is used to
 * specify when the next timeout should occur.
 */
static void
xpc_hb_beater(unsigned long dummy)
{
	xpc_vars->heartbeat++;

	if (jiffies >= xpc_hb_check_timeout) {
		wake_up_interruptible(&xpc_act_IRQ_wq);
	}

	xpc_hb_timer.expires = jiffies + (xpc_hb_interval * HZ);
	add_timer(&xpc_hb_timer);
}


/*
 * This thread is responsible for nearly all of the partition
 * activation/deactivation.
 */
static int
xpc_hb_checker(void *ignore)
{
	int last_IRQ_count = 0;
	int new_IRQ_count;
	int force_IRQ=0;


	/* this thread was marked active by xpc_hb_init() */

	daemonize(XPC_HB_CHECK_THREAD_NAME);

	set_cpus_allowed(current, cpumask_of_cpu(XPC_HB_CHECK_CPU));

	xpc_hb_check_timeout = jiffies + (xpc_hb_check_interval * HZ);

	while (!(volatile int) xpc_exiting) {

		/* wait for IRQ or timeout */
		(void) wait_event_interruptible(xpc_act_IRQ_wq,
			    (last_IRQ_count < atomic_read(&xpc_act_IRQ_rcvd) ||
					jiffies >= xpc_hb_check_timeout ||
						(volatile int) xpc_exiting));

		dev_dbg(xpc_part, "woke up with %d ticks rem; %d IRQs have "
			"been received\n",
			(int) (xpc_hb_check_timeout - jiffies),
			atomic_read(&xpc_act_IRQ_rcvd) - last_IRQ_count);


		/* checking of remote heartbeats is skewed by IRQ handling */
		if (jiffies >= xpc_hb_check_timeout) {
			dev_dbg(xpc_part, "checking remote heartbeats\n");
			xpc_check_remote_hb();

			/*
			 * We need to periodically recheck to ensure no
			 * IPI/AMO pairs have been missed.  That check
			 * must always reset xpc_hb_check_timeout.
			 */
			force_IRQ = 1;
		}


		new_IRQ_count = atomic_read(&xpc_act_IRQ_rcvd);
		if (last_IRQ_count < new_IRQ_count || force_IRQ != 0) {
			force_IRQ = 0;

			dev_dbg(xpc_part, "found an IRQ to process; will be "
				"resetting xpc_hb_check_timeout\n");

			last_IRQ_count += xpc_identify_act_IRQ_sender();
			if (last_IRQ_count < new_IRQ_count) {
				/* retry once to help avoid missing AMO */
				(void) xpc_identify_act_IRQ_sender();
			}
			last_IRQ_count = new_IRQ_count;

			xpc_hb_check_timeout = jiffies +
					   (xpc_hb_check_interval * HZ);
		}
	}

	dev_dbg(xpc_part, "heartbeat checker is exiting\n");


	/* mark this thread as inactive */
	up(&xpc_hb_checker_exited);
	return 0;
}


/*
 * This thread will attempt to discover other partitions to activate
 * based on info provided by SAL. This new thread is short lived and
 * will exit once discovery is complete.
 */
static int
xpc_initiate_discovery(void *ignore)
{
	daemonize(XPC_DISCOVERY_THREAD_NAME);

	xpc_discovery();

	dev_dbg(xpc_part, "discovery thread is exiting\n");

	/* mark this thread as inactive */
	up(&xpc_discovery_exited);
	return 0;
}


/*
 * Establish first contact with the remote partititon. This involves pulling
 * the XPC per partition variables from the remote partition and waiting for
 * the remote partition to pull ours.
 */
static enum xpc_retval
xpc_make_first_contact(struct xpc_partition *part)
{
	enum xpc_retval ret;


	while ((ret = xpc_pull_remote_vars_part(part)) != xpcSuccess) {
		if (ret != xpcRetry) {
			XPC_DEACTIVATE_PARTITION(part, ret);
			return ret;
		}

		dev_dbg(xpc_chan, "waiting to make first contact with "
			"partition %d\n", XPC_PARTID(part));

		/* wait a 1/4 of a second or so */
		msleep_interruptible(250);

		if (part->act_state == XPC_P_DEACTIVATING) {
			return part->reason;
		}
	}

	return xpc_mark_partition_active(part);
}


/*
 * The first kthread assigned to a newly activated partition is the one
 * created by XPC HB with which it calls xpc_partition_up(). XPC hangs on to
 * that kthread until the partition is brought down, at which time that kthread
 * returns back to XPC HB. (The return of that kthread will signify to XPC HB
 * that XPC has dismantled all communication infrastructure for the associated
 * partition.) This kthread becomes the channel manager for that partition.
 *
 * Each active partition has a channel manager, who, besides connecting and
 * disconnecting channels, will ensure that each of the partition's connected
 * channels has the required number of assigned kthreads to get the work done.
 */
static void
xpc_channel_mgr(struct xpc_partition *part)
{
	while (part->act_state != XPC_P_DEACTIVATING ||
				atomic_read(&part->nchannels_active) > 0) {

		xpc_process_channel_activity(part);


		/*
		 * Wait until we've been requested to activate kthreads or
		 * all of the channel's message queues have been torn down or
		 * a signal is pending.
		 *
		 * The channel_mgr_requests is set to 1 after being awakened,
		 * This is done to prevent the channel mgr from making one pass
		 * through the loop for each request, since he will
		 * be servicing all the requests in one pass. The reason it's
		 * set to 1 instead of 0 is so that other kthreads will know
		 * that the channel mgr is running and won't bother trying to
		 * wake him up.
		 */
		atomic_dec(&part->channel_mgr_requests);
		(void) wait_event_interruptible(part->channel_mgr_wq,
				(atomic_read(&part->channel_mgr_requests) > 0 ||
				(volatile u64) part->local_IPI_amo != 0 ||
				((volatile u8) part->act_state ==
							XPC_P_DEACTIVATING &&
				atomic_read(&part->nchannels_active) == 0)));
		atomic_set(&part->channel_mgr_requests, 1);

		// >>> Does it need to wakeup periodically as well? In case we
		// >>> miscalculated the #of kthreads to wakeup or create?
	}
}


/*
 * When XPC HB determines that a partition has come up, it will create a new
 * kthread and that kthread will call this function to attempt to set up the
 * basic infrastructure used for Cross Partition Communication with the newly
 * upped partition.
 *
 * The kthread that was created by XPC HB and which setup the XPC
 * infrastructure will remain assigned to the partition until the partition
 * goes down. At which time the kthread will teardown the XPC infrastructure
 * and then exit.
 *
 * XPC HB will put the remote partition's XPC per partition specific variables
 * physical address into xpc_partitions[partid].remote_vars_part_pa prior to
 * calling xpc_partition_up().
 */
static void
xpc_partition_up(struct xpc_partition *part)
{
	DBUG_ON(part->channels != NULL);

	dev_dbg(xpc_chan, "activating partition %d\n", XPC_PARTID(part));

	if (xpc_setup_infrastructure(part) != xpcSuccess) {
		return;
	}

	/*
	 * The kthread that XPC HB called us with will become the
	 * channel manager for this partition. It will not return
	 * back to XPC HB until the partition's XPC infrastructure
	 * has been dismantled.
	 */

	(void) xpc_part_ref(part);	/* this will always succeed */

	if (xpc_make_first_contact(part) == xpcSuccess) {
		xpc_channel_mgr(part);
	}

	xpc_part_deref(part);

	xpc_teardown_infrastructure(part);
}


static int
xpc_activating(void *__partid)
{
	partid_t partid = (u64) __partid;
	struct xpc_partition *part = &xpc_partitions[partid];
	unsigned long irq_flags;
	struct sched_param param = { sched_priority: MAX_RT_PRIO - 1 };
	int ret;


	DBUG_ON(partid <= 0 || partid >= XP_MAX_PARTITIONS);

	spin_lock_irqsave(&part->act_lock, irq_flags);

	if (part->act_state == XPC_P_DEACTIVATING) {
		part->act_state = XPC_P_INACTIVE;
		spin_unlock_irqrestore(&part->act_lock, irq_flags);
		part->remote_rp_pa = 0;
		return 0;
	}

	/* indicate the thread is activating */
	DBUG_ON(part->act_state != XPC_P_ACTIVATION_REQ);
	part->act_state = XPC_P_ACTIVATING;

	XPC_SET_REASON(part, 0, 0);
	spin_unlock_irqrestore(&part->act_lock, irq_flags);

	dev_dbg(xpc_part, "bringing partition %d up\n", partid);

	daemonize("xpc%02d", partid);

	/*
	 * This thread needs to run at a realtime priority to prevent a
	 * significant performance degradation.
	 */
	ret = sched_setscheduler(current, SCHED_FIFO, &param);
	if (ret != 0) {
		dev_warn(xpc_part, "unable to set pid %d to a realtime "
			"priority, ret=%d\n", current->pid, ret);
	}

	/* allow this thread and its children to run on any CPU */
	set_cpus_allowed(current, CPU_MASK_ALL);

	/*
	 * Register the remote partition's AMOs with SAL so it can handle
	 * and cleanup errors within that address range should the remote
	 * partition go down. We don't unregister this range because it is
	 * difficult to tell when outstanding writes to the remote partition
	 * are finished and thus when it is safe to unregister. This should
	 * not result in wasted space in the SAL xp_addr_region table because
	 * we should get the same page for remote_amos_page_pa after module
	 * reloads and system reboots.
	 */
	if (sn_register_xp_addr_region(part->remote_amos_page_pa,
							PAGE_SIZE, 1) < 0) {
		dev_warn(xpc_part, "xpc_partition_up(%d) failed to register "
			"xp_addr region\n", partid);

		spin_lock_irqsave(&part->act_lock, irq_flags);
		part->act_state = XPC_P_INACTIVE;
		XPC_SET_REASON(part, xpcPhysAddrRegFailed, __LINE__);
		spin_unlock_irqrestore(&part->act_lock, irq_flags);
		part->remote_rp_pa = 0;
		return 0;
	}

	XPC_ALLOW_HB(partid, xpc_vars);
	xpc_IPI_send_activated(part);


	/*
	 * xpc_partition_up() holds this thread and marks this partition as
	 * XPC_P_ACTIVE by calling xpc_hb_mark_active().
	 */
	(void) xpc_partition_up(part);

	xpc_mark_partition_inactive(part);

	if (part->reason == xpcReactivating) {
		/* interrupting ourselves results in activating partition */
		xpc_IPI_send_reactivate(part);
	}

	return 0;
}


void
xpc_activate_partition(struct xpc_partition *part)
{
	partid_t partid = XPC_PARTID(part);
	unsigned long irq_flags;
	pid_t pid;


	spin_lock_irqsave(&part->act_lock, irq_flags);

	pid = kernel_thread(xpc_activating, (void *) ((u64) partid), 0);

	DBUG_ON(part->act_state != XPC_P_INACTIVE);

	if (pid > 0) {
		part->act_state = XPC_P_ACTIVATION_REQ;
		XPC_SET_REASON(part, xpcCloneKThread, __LINE__);
	} else {
		XPC_SET_REASON(part, xpcCloneKThreadFailed, __LINE__);
	}

	spin_unlock_irqrestore(&part->act_lock, irq_flags);
}


/*
 * Handle the receipt of a SGI_XPC_NOTIFY IRQ by seeing whether the specified
 * partition actually sent it. Since SGI_XPC_NOTIFY IRQs may be shared by more
 * than one partition, we use an AMO_t structure per partition to indicate
 * whether a partition has sent an IPI or not.  >>> If it has, then wake up the
 * associated kthread to handle it.
 *
 * All SGI_XPC_NOTIFY IRQs received by XPC are the result of IPIs sent by XPC
 * running on other partitions.
 *
 * Noteworthy Arguments:
 *
 *	irq - Interrupt ReQuest number. NOT USED.
 *
 *	dev_id - partid of IPI's potential sender.
 *
 *	regs - processor's context before the processor entered
 *	       interrupt code. NOT USED.
 */
irqreturn_t
xpc_notify_IRQ_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	partid_t partid = (partid_t) (u64) dev_id;
	struct xpc_partition *part = &xpc_partitions[partid];


	DBUG_ON(partid <= 0 || partid >= XP_MAX_PARTITIONS);

	if (xpc_part_ref(part)) {
		xpc_check_for_channel_activity(part);

		xpc_part_deref(part);
	}
	return IRQ_HANDLED;
}


/*
 * Check to see if xpc_notify_IRQ_handler() dropped any IPIs on the floor
 * because the write to their associated IPI amo completed after the IRQ/IPI
 * was received.
 */
void
xpc_dropped_IPI_check(struct xpc_partition *part)
{
	if (xpc_part_ref(part)) {
		xpc_check_for_channel_activity(part);

		part->dropped_IPI_timer.expires = jiffies +
							XPC_P_DROPPED_IPI_WAIT;
		add_timer(&part->dropped_IPI_timer);
		xpc_part_deref(part);
	}
}


void
xpc_activate_kthreads(struct xpc_channel *ch, int needed)
{
	int idle = atomic_read(&ch->kthreads_idle);
	int assigned = atomic_read(&ch->kthreads_assigned);
	int wakeup;


	DBUG_ON(needed <= 0);

	if (idle > 0) {
		wakeup = (needed > idle) ? idle : needed;
		needed -= wakeup;

		dev_dbg(xpc_chan, "wakeup %d idle kthreads, partid=%d, "
			"channel=%d\n", wakeup, ch->partid, ch->number);

		/* only wakeup the requested number of kthreads */
		wake_up_nr(&ch->idle_wq, wakeup);
	}

	if (needed <= 0) {
		return;
	}

	if (needed + assigned > ch->kthreads_assigned_limit) {
		needed = ch->kthreads_assigned_limit - assigned;
		// >>>should never be less than 0
		if (needed <= 0) {
			return;
		}
	}

	dev_dbg(xpc_chan, "create %d new kthreads, partid=%d, channel=%d\n",
		needed, ch->partid, ch->number);

	xpc_create_kthreads(ch, needed);
}


/*
 * This function is where XPC's kthreads wait for messages to deliver.
 */
static void
xpc_kthread_waitmsgs(struct xpc_partition *part, struct xpc_channel *ch)
{
	do {
		/* deliver messages to their intended recipients */

		while ((volatile s64) ch->w_local_GP.get <
				(volatile s64) ch->w_remote_GP.put &&
					!((volatile u32) ch->flags &
						XPC_C_DISCONNECTING)) {
			xpc_deliver_msg(ch);
		}

		if (atomic_inc_return(&ch->kthreads_idle) >
						ch->kthreads_idle_limit) {
			/* too many idle kthreads on this channel */
			atomic_dec(&ch->kthreads_idle);
			break;
		}

		dev_dbg(xpc_chan, "idle kthread calling "
			"wait_event_interruptible_exclusive()\n");

		(void) wait_event_interruptible_exclusive(ch->idle_wq,
				((volatile s64) ch->w_local_GP.get <
					(volatile s64) ch->w_remote_GP.put ||
				((volatile u32) ch->flags &
						XPC_C_DISCONNECTING)));

		atomic_dec(&ch->kthreads_idle);

	} while (!((volatile u32) ch->flags & XPC_C_DISCONNECTING));
}


static int
xpc_daemonize_kthread(void *args)
{
	partid_t partid = XPC_UNPACK_ARG1(args);
	u16 ch_number = XPC_UNPACK_ARG2(args);
	struct xpc_partition *part = &xpc_partitions[partid];
	struct xpc_channel *ch;
	int n_needed;


	daemonize("xpc%02dc%d", partid, ch_number);

	dev_dbg(xpc_chan, "kthread starting, partid=%d, channel=%d\n",
		partid, ch_number);

	ch = &part->channels[ch_number];

	if (!(ch->flags & XPC_C_DISCONNECTING)) {
		DBUG_ON(!(ch->flags & XPC_C_CONNECTED));

		/* let registerer know that connection has been established */

		if (atomic_read(&ch->kthreads_assigned) == 1) {
			xpc_connected_callout(ch);

			/*
			 * It is possible that while the callout was being
			 * made that the remote partition sent some messages.
			 * If that is the case, we may need to activate
			 * additional kthreads to help deliver them. We only
			 * need one less than total #of messages to deliver.
			 */
			n_needed = ch->w_remote_GP.put - ch->w_local_GP.get - 1;
			if (n_needed > 0 &&
					!(ch->flags & XPC_C_DISCONNECTING)) {
				xpc_activate_kthreads(ch, n_needed);
			}
		}

		xpc_kthread_waitmsgs(part, ch);
	}

	if (atomic_dec_return(&ch->kthreads_assigned) == 0 &&
			((ch->flags & XPC_C_CONNECTCALLOUT) ||
				(ch->reason != xpcUnregistering &&
					ch->reason != xpcOtherUnregistering))) {
		xpc_disconnected_callout(ch);
	}


	xpc_msgqueue_deref(ch);

	dev_dbg(xpc_chan, "kthread exiting, partid=%d, channel=%d\n",
		partid, ch_number);

	xpc_part_deref(part);
	return 0;
}


/*
 * For each partition that XPC has established communications with, there is
 * a minimum of one kernel thread assigned to perform any operation that
 * may potentially sleep or block (basically the callouts to the asynchronous
 * functions registered via xpc_connect()).
 *
 * Additional kthreads are created and destroyed by XPC as the workload
 * demands.
 *
 * A kthread is assigned to one of the active channels that exists for a given
 * partition.
 */
void
xpc_create_kthreads(struct xpc_channel *ch, int needed)
{
	unsigned long irq_flags;
	pid_t pid;
	u64 args = XPC_PACK_ARGS(ch->partid, ch->number);


	while (needed-- > 0) {
		pid = kernel_thread(xpc_daemonize_kthread, (void *) args, 0);
		if (pid < 0) {
			/* the fork failed */

			if (atomic_read(&ch->kthreads_assigned) <
						ch->kthreads_idle_limit) {
				/*
				 * Flag this as an error only if we have an
				 * insufficient #of kthreads for the channel
				 * to function.
				 *
				 * No xpc_msgqueue_ref() is needed here since
				 * the channel mgr is doing this.
				 */
				spin_lock_irqsave(&ch->lock, irq_flags);
				XPC_DISCONNECT_CHANNEL(ch, xpcLackOfResources,
								&irq_flags);
				spin_unlock_irqrestore(&ch->lock, irq_flags);
			}
			break;
		}

		/*
		 * The following is done on behalf of the newly created
		 * kthread. That kthread is responsible for doing the
		 * counterpart to the following before it exits.
		 */
		(void) xpc_part_ref(&xpc_partitions[ch->partid]);
		xpc_msgqueue_ref(ch);
		atomic_inc(&ch->kthreads_assigned);
		ch->kthreads_created++;	// >>> temporary debug only!!!
	}
}


void
xpc_disconnect_wait(int ch_number)
{
	partid_t partid;
	struct xpc_partition *part;
	struct xpc_channel *ch;


	/* now wait for all callouts to the caller's function to cease */
	for (partid = 1; partid < XP_MAX_PARTITIONS; partid++) {
		part = &xpc_partitions[partid];

		if (xpc_part_ref(part)) {
			ch = &part->channels[ch_number];

// >>> how do we keep from falling into the window between our check and going
// >>> down and coming back up where sema is re-inited?
			if (ch->flags & XPC_C_SETUP) {
				(void) down(&ch->teardown_sema);
			}

			xpc_part_deref(part);
		}
	}
}


static void
xpc_do_exit(void)
{
	partid_t partid;
	int active_part_count;
	struct xpc_partition *part;


	/* now it's time to eliminate our heartbeat */
	del_timer_sync(&xpc_hb_timer);
	xpc_vars->heartbeating_to_mask = 0;

	/* indicate to others that our reserved page is uninitialized */
	xpc_rsvd_page->vars_pa = 0;

	/*
	 * Ignore all incoming interrupts. Without interupts the heartbeat
	 * checker won't activate any new partitions that may come up.
	 */
	free_irq(SGI_XPC_ACTIVATE, NULL);

	/*
	 * Cause the heartbeat checker and the discovery threads to exit.
	 * We don't want them attempting to activate new partitions as we
	 * try to deactivate the existing ones.
	 */
	xpc_exiting = 1;
	wake_up_interruptible(&xpc_act_IRQ_wq);

	/* wait for the heartbeat checker thread to mark itself inactive */
	down(&xpc_hb_checker_exited);

	/* wait for the discovery thread to mark itself inactive */
	down(&xpc_discovery_exited);


	msleep_interruptible(300);


	/* wait for all partitions to become inactive */

	do {
		active_part_count = 0;

		for (partid = 1; partid < XP_MAX_PARTITIONS; partid++) {
			part = &xpc_partitions[partid];
			if (part->act_state != XPC_P_INACTIVE) {
				active_part_count++;

				XPC_DEACTIVATE_PARTITION(part, xpcUnloading);
			}
		}

		if (active_part_count)
			msleep_interruptible(300);
	} while (active_part_count > 0);


	/* close down protections for IPI operations */
	xpc_restrict_IPI_ops();


	/* clear the interface to XPC's functions */
	xpc_clear_interface();

	if (xpc_sysctl) {
		unregister_sysctl_table(xpc_sysctl);
	}
}


int __init
xpc_init(void)
{
	int ret;
	partid_t partid;
	struct xpc_partition *part;
	pid_t pid;


	if (!ia64_platform_is("sn2")) {
		return -ENODEV;
	}

	/*
	 * xpc_remote_copy_buffer is used as a temporary buffer for bte_copy'ng
	 * both a partition's reserved page and its XPC variables. Its size was
	 * based on the size of a reserved page. So we need to ensure that the
	 * XPC variables will fit as well.
	 */
	if (XPC_VARS_ALIGNED_SIZE > XPC_RSVD_PAGE_ALIGNED_SIZE) {
		dev_err(xpc_part, "xpc_remote_copy_buffer is not big enough\n");
		return -EPERM;
	}
	DBUG_ON((u64) xpc_remote_copy_buffer !=
				L1_CACHE_ALIGN((u64) xpc_remote_copy_buffer));

	snprintf(xpc_part->bus_id, BUS_ID_SIZE, "part");
	snprintf(xpc_chan->bus_id, BUS_ID_SIZE, "chan");

	xpc_sysctl = register_sysctl_table(xpc_sys_dir, 1);

	/*
	 * The first few fields of each entry of xpc_partitions[] need to
	 * be initialized now so that calls to xpc_connect() and
	 * xpc_disconnect() can be made prior to the activation of any remote
	 * partition. NOTE THAT NONE OF THE OTHER FIELDS BELONGING TO THESE
	 * ENTRIES ARE MEANINGFUL UNTIL AFTER AN ENTRY'S CORRESPONDING
	 * PARTITION HAS BEEN ACTIVATED.
	 */
	for (partid = 1; partid < XP_MAX_PARTITIONS; partid++) {
		part = &xpc_partitions[partid];

		DBUG_ON((u64) part != L1_CACHE_ALIGN((u64) part));

		part->act_IRQ_rcvd = 0;
		spin_lock_init(&part->act_lock);
		part->act_state = XPC_P_INACTIVE;
		XPC_SET_REASON(part, 0, 0);
		part->setup_state = XPC_P_UNSET;
		init_waitqueue_head(&part->teardown_wq);
		atomic_set(&part->references, 0);
	}

	/*
	 * Open up protections for IPI operations (and AMO operations on
	 * Shub 1.1 systems).
	 */
	xpc_allow_IPI_ops();

	/*
	 * Interrupts being processed will increment this atomic variable and
	 * awaken the heartbeat thread which will process the interrupts.
	 */
	atomic_set(&xpc_act_IRQ_rcvd, 0);

	/*
	 * This is safe to do before the xpc_hb_checker thread has started
	 * because the handler releases a wait queue.  If an interrupt is
	 * received before the thread is waiting, it will not go to sleep,
	 * but rather immediately process the interrupt.
	 */
	ret = request_irq(SGI_XPC_ACTIVATE, xpc_act_IRQ_handler, 0,
							"xpc hb", NULL);
	if (ret != 0) {
		dev_err(xpc_part, "can't register ACTIVATE IRQ handler, "
			"errno=%d\n", -ret);

		xpc_restrict_IPI_ops();

		if (xpc_sysctl) {
			unregister_sysctl_table(xpc_sysctl);
		}
		return -EBUSY;
	}

	/*
	 * Fill the partition reserved page with the information needed by
	 * other partitions to discover we are alive and establish initial
	 * communications.
	 */
	xpc_rsvd_page = xpc_rsvd_page_init();
	if (xpc_rsvd_page == NULL) {
		dev_err(xpc_part, "could not setup our reserved page\n");

		free_irq(SGI_XPC_ACTIVATE, NULL);
		xpc_restrict_IPI_ops();

		if (xpc_sysctl) {
			unregister_sysctl_table(xpc_sysctl);
		}
		return -EBUSY;
	}


	/*
	 * Set the beating to other partitions into motion.  This is
	 * the last requirement for other partitions' discovery to
	 * initiate communications with us.
	 */
	init_timer(&xpc_hb_timer);
	xpc_hb_timer.function = xpc_hb_beater;
	xpc_hb_beater(0);


	/*
	 * The real work-horse behind xpc.  This processes incoming
	 * interrupts and monitors remote heartbeats.
	 */
	pid = kernel_thread(xpc_hb_checker, NULL, 0);
	if (pid < 0) {
		dev_err(xpc_part, "failed while forking hb check thread\n");

		/* indicate to others that our reserved page is uninitialized */
		xpc_rsvd_page->vars_pa = 0;

		del_timer_sync(&xpc_hb_timer);
		free_irq(SGI_XPC_ACTIVATE, NULL);
		xpc_restrict_IPI_ops();

		if (xpc_sysctl) {
			unregister_sysctl_table(xpc_sysctl);
		}
		return -EBUSY;
	}


	/*
	 * Startup a thread that will attempt to discover other partitions to
	 * activate based on info provided by SAL. This new thread is short
	 * lived and will exit once discovery is complete.
	 */
	pid = kernel_thread(xpc_initiate_discovery, NULL, 0);
	if (pid < 0) {
		dev_err(xpc_part, "failed while forking discovery thread\n");

		/* mark this new thread as a non-starter */
		up(&xpc_discovery_exited);

		xpc_do_exit();
		return -EBUSY;
	}


	/* set the interface to point at XPC's functions */
	xpc_set_interface(xpc_initiate_connect, xpc_initiate_disconnect,
			  xpc_initiate_allocate, xpc_initiate_send,
			  xpc_initiate_send_notify, xpc_initiate_received,
			  xpc_initiate_partid_to_nasids);

	return 0;
}
module_init(xpc_init);


void __exit
xpc_exit(void)
{
	xpc_do_exit();
}
module_exit(xpc_exit);


MODULE_AUTHOR("Silicon Graphics, Inc.");
MODULE_DESCRIPTION("Cross Partition Communication (XPC) support");
MODULE_LICENSE("GPL");

module_param(xpc_hb_interval, int, 0);
MODULE_PARM_DESC(xpc_hb_interval, "Number of seconds between "
		"heartbeat increments.");

module_param(xpc_hb_check_interval, int, 0);
MODULE_PARM_DESC(xpc_hb_check_interval, "Number of seconds between "
		"heartbeat checks.");

