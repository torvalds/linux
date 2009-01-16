/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2004-2008 Silicon Graphics, Inc.  All Rights Reserved.
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
 *	  . Currently on sn2, we have no way to determine which nasid an IRQ
 *	    came from. Thus, xpc_send_IRQ_sn2() does a remote amo write
 *	    followed by an IPI. The amo indicates where data is to be pulled
 *	    from, so after the IPI arrives, the remote partition checks the amo
 *	    word. The IPI can actually arrive before the amo however, so other
 *	    code must periodically check for this case. Also, remote amo
 *	    operations do not reliably time out. Thus we do a remote PIO read
 *	    solely to know whether the remote partition is down and whether we
 *	    should stop sending IPIs to it. This remote PIO read operation is
 *	    set up in a special nofault region so SAL knows to ignore (and
 *	    cleanup) any errors due to the remote amo write, PIO read, and/or
 *	    PIO write operations.
 *
 *	    If/when new hardware solves this IPI problem, we should abandon
 *	    the current approach.
 *
 */

#include <linux/module.h>
#include <linux/sysctl.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/kdebug.h>
#include <linux/kthread.h>
#include "xpc.h"

/* define two XPC debug device structures to be used with dev_dbg() et al */

struct device_driver xpc_dbg_name = {
	.name = "xpc"
};

struct device xpc_part_dbg_subname = {
	.init_name = "",	/* set to "part" at xpc_init() time */
	.driver = &xpc_dbg_name
};

struct device xpc_chan_dbg_subname = {
	.init_name = "",	/* set to "chan" at xpc_init() time */
	.driver = &xpc_dbg_name
};

struct device *xpc_part = &xpc_part_dbg_subname;
struct device *xpc_chan = &xpc_chan_dbg_subname;

static int xpc_kdebug_ignore;

/* systune related variables for /proc/sys directories */

static int xpc_hb_interval = XPC_HB_DEFAULT_INTERVAL;
static int xpc_hb_min_interval = 1;
static int xpc_hb_max_interval = 10;

static int xpc_hb_check_interval = XPC_HB_CHECK_DEFAULT_INTERVAL;
static int xpc_hb_check_min_interval = 10;
static int xpc_hb_check_max_interval = 120;

int xpc_disengage_timelimit = XPC_DISENGAGE_DEFAULT_TIMELIMIT;
static int xpc_disengage_min_timelimit;	/* = 0 */
static int xpc_disengage_max_timelimit = 120;

static ctl_table xpc_sys_xpc_hb_dir[] = {
	{
	 .ctl_name = CTL_UNNUMBERED,
	 .procname = "hb_interval",
	 .data = &xpc_hb_interval,
	 .maxlen = sizeof(int),
	 .mode = 0644,
	 .proc_handler = &proc_dointvec_minmax,
	 .strategy = &sysctl_intvec,
	 .extra1 = &xpc_hb_min_interval,
	 .extra2 = &xpc_hb_max_interval},
	{
	 .ctl_name = CTL_UNNUMBERED,
	 .procname = "hb_check_interval",
	 .data = &xpc_hb_check_interval,
	 .maxlen = sizeof(int),
	 .mode = 0644,
	 .proc_handler = &proc_dointvec_minmax,
	 .strategy = &sysctl_intvec,
	 .extra1 = &xpc_hb_check_min_interval,
	 .extra2 = &xpc_hb_check_max_interval},
	{}
};
static ctl_table xpc_sys_xpc_dir[] = {
	{
	 .ctl_name = CTL_UNNUMBERED,
	 .procname = "hb",
	 .mode = 0555,
	 .child = xpc_sys_xpc_hb_dir},
	{
	 .ctl_name = CTL_UNNUMBERED,
	 .procname = "disengage_timelimit",
	 .data = &xpc_disengage_timelimit,
	 .maxlen = sizeof(int),
	 .mode = 0644,
	 .proc_handler = &proc_dointvec_minmax,
	 .strategy = &sysctl_intvec,
	 .extra1 = &xpc_disengage_min_timelimit,
	 .extra2 = &xpc_disengage_max_timelimit},
	{}
};
static ctl_table xpc_sys_dir[] = {
	{
	 .ctl_name = CTL_UNNUMBERED,
	 .procname = "xpc",
	 .mode = 0555,
	 .child = xpc_sys_xpc_dir},
	{}
};
static struct ctl_table_header *xpc_sysctl;

/* non-zero if any remote partition disengage was timed out */
int xpc_disengage_timedout;

/* #of activate IRQs received and not yet processed */
int xpc_activate_IRQ_rcvd;
DEFINE_SPINLOCK(xpc_activate_IRQ_rcvd_lock);

/* IRQ handler notifies this wait queue on receipt of an IRQ */
DECLARE_WAIT_QUEUE_HEAD(xpc_activate_IRQ_wq);

static unsigned long xpc_hb_check_timeout;
static struct timer_list xpc_hb_timer;
void *xpc_heartbeating_to_mask;

/* notification that the xpc_hb_checker thread has exited */
static DECLARE_COMPLETION(xpc_hb_checker_exited);

/* notification that the xpc_discovery thread has exited */
static DECLARE_COMPLETION(xpc_discovery_exited);

static void xpc_kthread_waitmsgs(struct xpc_partition *, struct xpc_channel *);

static int xpc_system_reboot(struct notifier_block *, unsigned long, void *);
static struct notifier_block xpc_reboot_notifier = {
	.notifier_call = xpc_system_reboot,
};

static int xpc_system_die(struct notifier_block *, unsigned long, void *);
static struct notifier_block xpc_die_notifier = {
	.notifier_call = xpc_system_die,
};

int (*xpc_setup_partitions_sn) (void);
enum xp_retval (*xpc_get_partition_rsvd_page_pa) (void *buf, u64 *cookie,
						  unsigned long *rp_pa,
						  size_t *len);
int (*xpc_setup_rsvd_page_sn) (struct xpc_rsvd_page *rp);
void (*xpc_heartbeat_init) (void);
void (*xpc_heartbeat_exit) (void);
void (*xpc_increment_heartbeat) (void);
void (*xpc_offline_heartbeat) (void);
void (*xpc_online_heartbeat) (void);
enum xp_retval (*xpc_get_remote_heartbeat) (struct xpc_partition *part);

enum xp_retval (*xpc_make_first_contact) (struct xpc_partition *part);
void (*xpc_notify_senders_of_disconnect) (struct xpc_channel *ch);
u64 (*xpc_get_chctl_all_flags) (struct xpc_partition *part);
enum xp_retval (*xpc_setup_msg_structures) (struct xpc_channel *ch);
void (*xpc_teardown_msg_structures) (struct xpc_channel *ch);
void (*xpc_process_msg_chctl_flags) (struct xpc_partition *part, int ch_number);
int (*xpc_n_of_deliverable_payloads) (struct xpc_channel *ch);
void *(*xpc_get_deliverable_payload) (struct xpc_channel *ch);

void (*xpc_request_partition_activation) (struct xpc_rsvd_page *remote_rp,
					  unsigned long remote_rp_pa,
					  int nasid);
void (*xpc_request_partition_reactivation) (struct xpc_partition *part);
void (*xpc_request_partition_deactivation) (struct xpc_partition *part);
void (*xpc_cancel_partition_deactivation_request) (struct xpc_partition *part);

void (*xpc_process_activate_IRQ_rcvd) (void);
enum xp_retval (*xpc_setup_ch_structures_sn) (struct xpc_partition *part);
void (*xpc_teardown_ch_structures_sn) (struct xpc_partition *part);

void (*xpc_indicate_partition_engaged) (struct xpc_partition *part);
int (*xpc_partition_engaged) (short partid);
int (*xpc_any_partition_engaged) (void);
void (*xpc_indicate_partition_disengaged) (struct xpc_partition *part);
void (*xpc_assume_partition_disengaged) (short partid);

void (*xpc_send_chctl_closerequest) (struct xpc_channel *ch,
				     unsigned long *irq_flags);
void (*xpc_send_chctl_closereply) (struct xpc_channel *ch,
				   unsigned long *irq_flags);
void (*xpc_send_chctl_openrequest) (struct xpc_channel *ch,
				    unsigned long *irq_flags);
void (*xpc_send_chctl_openreply) (struct xpc_channel *ch,
				  unsigned long *irq_flags);

void (*xpc_save_remote_msgqueue_pa) (struct xpc_channel *ch,
				     unsigned long msgqueue_pa);

enum xp_retval (*xpc_send_payload) (struct xpc_channel *ch, u32 flags,
				    void *payload, u16 payload_size,
				    u8 notify_type, xpc_notify_func func,
				    void *key);
void (*xpc_received_payload) (struct xpc_channel *ch, void *payload);

/*
 * Timer function to enforce the timelimit on the partition disengage.
 */
static void
xpc_timeout_partition_disengage(unsigned long data)
{
	struct xpc_partition *part = (struct xpc_partition *)data;

	DBUG_ON(time_is_after_jiffies(part->disengage_timeout));

	(void)xpc_partition_disengaged(part);

	DBUG_ON(part->disengage_timeout != 0);
	DBUG_ON(xpc_partition_engaged(XPC_PARTID(part)));
}

/*
 * Timer to produce the heartbeat.  The timer structures function is
 * already set when this is initially called.  A tunable is used to
 * specify when the next timeout should occur.
 */
static void
xpc_hb_beater(unsigned long dummy)
{
	xpc_increment_heartbeat();

	if (time_is_before_eq_jiffies(xpc_hb_check_timeout))
		wake_up_interruptible(&xpc_activate_IRQ_wq);

	xpc_hb_timer.expires = jiffies + (xpc_hb_interval * HZ);
	add_timer(&xpc_hb_timer);
}

static void
xpc_start_hb_beater(void)
{
	xpc_heartbeat_init();
	init_timer(&xpc_hb_timer);
	xpc_hb_timer.function = xpc_hb_beater;
	xpc_hb_beater(0);
}

static void
xpc_stop_hb_beater(void)
{
	del_timer_sync(&xpc_hb_timer);
	xpc_heartbeat_exit();
}

/*
 * At periodic intervals, scan through all active partitions and ensure
 * their heartbeat is still active.  If not, the partition is deactivated.
 */
static void
xpc_check_remote_hb(void)
{
	struct xpc_partition *part;
	short partid;
	enum xp_retval ret;

	for (partid = 0; partid < xp_max_npartitions; partid++) {

		if (xpc_exiting)
			break;

		if (partid == xp_partition_id)
			continue;

		part = &xpc_partitions[partid];

		if (part->act_state == XPC_P_AS_INACTIVE ||
		    part->act_state == XPC_P_AS_DEACTIVATING) {
			continue;
		}

		ret = xpc_get_remote_heartbeat(part);
		if (ret != xpSuccess)
			XPC_DEACTIVATE_PARTITION(part, ret);
	}
}

/*
 * This thread is responsible for nearly all of the partition
 * activation/deactivation.
 */
static int
xpc_hb_checker(void *ignore)
{
	int force_IRQ = 0;

	/* this thread was marked active by xpc_hb_init() */

	set_cpus_allowed_ptr(current, &cpumask_of_cpu(XPC_HB_CHECK_CPU));

	/* set our heartbeating to other partitions into motion */
	xpc_hb_check_timeout = jiffies + (xpc_hb_check_interval * HZ);
	xpc_start_hb_beater();

	while (!xpc_exiting) {

		dev_dbg(xpc_part, "woke up with %d ticks rem; %d IRQs have "
			"been received\n",
			(int)(xpc_hb_check_timeout - jiffies),
			xpc_activate_IRQ_rcvd);

		/* checking of remote heartbeats is skewed by IRQ handling */
		if (time_is_before_eq_jiffies(xpc_hb_check_timeout)) {
			xpc_hb_check_timeout = jiffies +
			    (xpc_hb_check_interval * HZ);

			dev_dbg(xpc_part, "checking remote heartbeats\n");
			xpc_check_remote_hb();

			/*
			 * On sn2 we need to periodically recheck to ensure no
			 * IRQ/amo pairs have been missed.
			 */
			if (is_shub())
				force_IRQ = 1;
		}

		/* check for outstanding IRQs */
		if (xpc_activate_IRQ_rcvd > 0 || force_IRQ != 0) {
			force_IRQ = 0;
			dev_dbg(xpc_part, "processing activate IRQs "
				"received\n");
			xpc_process_activate_IRQ_rcvd();
		}

		/* wait for IRQ or timeout */
		(void)wait_event_interruptible(xpc_activate_IRQ_wq,
					       (time_is_before_eq_jiffies(
						xpc_hb_check_timeout) ||
						xpc_activate_IRQ_rcvd > 0 ||
						xpc_exiting));
	}

	xpc_stop_hb_beater();

	dev_dbg(xpc_part, "heartbeat checker is exiting\n");

	/* mark this thread as having exited */
	complete(&xpc_hb_checker_exited);
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
	xpc_discovery();

	dev_dbg(xpc_part, "discovery thread is exiting\n");

	/* mark this thread as having exited */
	complete(&xpc_discovery_exited);
	return 0;
}

/*
 * The first kthread assigned to a newly activated partition is the one
 * created by XPC HB with which it calls xpc_activating(). XPC hangs on to
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
	while (part->act_state != XPC_P_AS_DEACTIVATING ||
	       atomic_read(&part->nchannels_active) > 0 ||
	       !xpc_partition_disengaged(part)) {

		xpc_process_sent_chctl_flags(part);

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
		(void)wait_event_interruptible(part->channel_mgr_wq,
				(atomic_read(&part->channel_mgr_requests) > 0 ||
				 part->chctl.all_flags != 0 ||
				 (part->act_state == XPC_P_AS_DEACTIVATING &&
				 atomic_read(&part->nchannels_active) == 0 &&
				 xpc_partition_disengaged(part))));
		atomic_set(&part->channel_mgr_requests, 1);
	}
}

/*
 * Guarantee that the kzalloc'd memory is cacheline aligned.
 */
void *
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
 * Setup the channel structures necessary to support XPartition Communication
 * between the specified remote partition and the local one.
 */
static enum xp_retval
xpc_setup_ch_structures(struct xpc_partition *part)
{
	enum xp_retval ret;
	int ch_number;
	struct xpc_channel *ch;
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

	/* allocate the remote open and close args */

	part->remote_openclose_args =
	    xpc_kzalloc_cacheline_aligned(XPC_OPENCLOSE_ARGS_SIZE,
					  GFP_KERNEL, &part->
					  remote_openclose_args_base);
	if (part->remote_openclose_args == NULL) {
		dev_err(xpc_chan, "can't get memory for remote connect args\n");
		ret = xpNoMemory;
		goto out_1;
	}

	part->chctl.all_flags = 0;
	spin_lock_init(&part->chctl_lock);

	atomic_set(&part->channel_mgr_requests, 1);
	init_waitqueue_head(&part->channel_mgr_wq);

	part->nchannels = XPC_MAX_NCHANNELS;

	atomic_set(&part->nchannels_active, 0);
	atomic_set(&part->nchannels_engaged, 0);

	for (ch_number = 0; ch_number < part->nchannels; ch_number++) {
		ch = &part->channels[ch_number];

		ch->partid = partid;
		ch->number = ch_number;
		ch->flags = XPC_C_DISCONNECTED;

		atomic_set(&ch->kthreads_assigned, 0);
		atomic_set(&ch->kthreads_idle, 0);
		atomic_set(&ch->kthreads_active, 0);

		atomic_set(&ch->references, 0);
		atomic_set(&ch->n_to_notify, 0);

		spin_lock_init(&ch->lock);
		init_completion(&ch->wdisconnect_wait);

		atomic_set(&ch->n_on_msg_allocate_wq, 0);
		init_waitqueue_head(&ch->msg_allocate_wq);
		init_waitqueue_head(&ch->idle_wq);
	}

	ret = xpc_setup_ch_structures_sn(part);
	if (ret != xpSuccess)
		goto out_2;

	/*
	 * With the setting of the partition setup_state to XPC_P_SS_SETUP,
	 * we're declaring that this partition is ready to go.
	 */
	part->setup_state = XPC_P_SS_SETUP;

	return xpSuccess;

	/* setup of ch structures failed */
out_2:
	kfree(part->remote_openclose_args_base);
	part->remote_openclose_args = NULL;
out_1:
	kfree(part->channels);
	part->channels = NULL;
	return ret;
}

/*
 * Teardown the channel structures necessary to support XPartition Communication
 * between the specified remote partition and the local one.
 */
static void
xpc_teardown_ch_structures(struct xpc_partition *part)
{
	DBUG_ON(atomic_read(&part->nchannels_engaged) != 0);
	DBUG_ON(atomic_read(&part->nchannels_active) != 0);

	/*
	 * Make this partition inaccessible to local processes by marking it
	 * as no longer setup. Then wait before proceeding with the teardown
	 * until all existing references cease.
	 */
	DBUG_ON(part->setup_state != XPC_P_SS_SETUP);
	part->setup_state = XPC_P_SS_WTEARDOWN;

	wait_event(part->teardown_wq, (atomic_read(&part->references) == 0));

	/* now we can begin tearing down the infrastructure */

	xpc_teardown_ch_structures_sn(part);

	kfree(part->remote_openclose_args_base);
	part->remote_openclose_args = NULL;
	kfree(part->channels);
	part->channels = NULL;

	part->setup_state = XPC_P_SS_TORNDOWN;
}

/*
 * When XPC HB determines that a partition has come up, it will create a new
 * kthread and that kthread will call this function to attempt to set up the
 * basic infrastructure used for Cross Partition Communication with the newly
 * upped partition.
 *
 * The kthread that was created by XPC HB and which setup the XPC
 * infrastructure will remain assigned to the partition becoming the channel
 * manager for that partition until the partition is deactivating, at which
 * time the kthread will teardown the XPC infrastructure and then exit.
 */
static int
xpc_activating(void *__partid)
{
	short partid = (u64)__partid;
	struct xpc_partition *part = &xpc_partitions[partid];
	unsigned long irq_flags;

	DBUG_ON(partid < 0 || partid >= xp_max_npartitions);

	spin_lock_irqsave(&part->act_lock, irq_flags);

	if (part->act_state == XPC_P_AS_DEACTIVATING) {
		part->act_state = XPC_P_AS_INACTIVE;
		spin_unlock_irqrestore(&part->act_lock, irq_flags);
		part->remote_rp_pa = 0;
		return 0;
	}

	/* indicate the thread is activating */
	DBUG_ON(part->act_state != XPC_P_AS_ACTIVATION_REQ);
	part->act_state = XPC_P_AS_ACTIVATING;

	XPC_SET_REASON(part, 0, 0);
	spin_unlock_irqrestore(&part->act_lock, irq_flags);

	dev_dbg(xpc_part, "activating partition %d\n", partid);

	xpc_allow_hb(partid);

	if (xpc_setup_ch_structures(part) == xpSuccess) {
		(void)xpc_part_ref(part);	/* this will always succeed */

		if (xpc_make_first_contact(part) == xpSuccess) {
			xpc_mark_partition_active(part);
			xpc_channel_mgr(part);
			/* won't return until partition is deactivating */
		}

		xpc_part_deref(part);
		xpc_teardown_ch_structures(part);
	}

	xpc_disallow_hb(partid);
	xpc_mark_partition_inactive(part);

	if (part->reason == xpReactivating) {
		/* interrupting ourselves results in activating partition */
		xpc_request_partition_reactivation(part);
	}

	return 0;
}

void
xpc_activate_partition(struct xpc_partition *part)
{
	short partid = XPC_PARTID(part);
	unsigned long irq_flags;
	struct task_struct *kthread;

	spin_lock_irqsave(&part->act_lock, irq_flags);

	DBUG_ON(part->act_state != XPC_P_AS_INACTIVE);

	part->act_state = XPC_P_AS_ACTIVATION_REQ;
	XPC_SET_REASON(part, xpCloneKThread, __LINE__);

	spin_unlock_irqrestore(&part->act_lock, irq_flags);

	kthread = kthread_run(xpc_activating, (void *)((u64)partid), "xpc%02d",
			      partid);
	if (IS_ERR(kthread)) {
		spin_lock_irqsave(&part->act_lock, irq_flags);
		part->act_state = XPC_P_AS_INACTIVE;
		XPC_SET_REASON(part, xpCloneKThreadFailed, __LINE__);
		spin_unlock_irqrestore(&part->act_lock, irq_flags);
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

	if (needed <= 0)
		return;

	if (needed + assigned > ch->kthreads_assigned_limit) {
		needed = ch->kthreads_assigned_limit - assigned;
		if (needed <= 0)
			return;
	}

	dev_dbg(xpc_chan, "create %d new kthreads, partid=%d, channel=%d\n",
		needed, ch->partid, ch->number);

	xpc_create_kthreads(ch, needed, 0);
}

/*
 * This function is where XPC's kthreads wait for messages to deliver.
 */
static void
xpc_kthread_waitmsgs(struct xpc_partition *part, struct xpc_channel *ch)
{
	do {
		/* deliver messages to their intended recipients */

		while (xpc_n_of_deliverable_payloads(ch) > 0 &&
		       !(ch->flags & XPC_C_DISCONNECTING)) {
			xpc_deliver_payload(ch);
		}

		if (atomic_inc_return(&ch->kthreads_idle) >
		    ch->kthreads_idle_limit) {
			/* too many idle kthreads on this channel */
			atomic_dec(&ch->kthreads_idle);
			break;
		}

		dev_dbg(xpc_chan, "idle kthread calling "
			"wait_event_interruptible_exclusive()\n");

		(void)wait_event_interruptible_exclusive(ch->idle_wq,
				(xpc_n_of_deliverable_payloads(ch) > 0 ||
				 (ch->flags & XPC_C_DISCONNECTING)));

		atomic_dec(&ch->kthreads_idle);

	} while (!(ch->flags & XPC_C_DISCONNECTING));
}

static int
xpc_kthread_start(void *args)
{
	short partid = XPC_UNPACK_ARG1(args);
	u16 ch_number = XPC_UNPACK_ARG2(args);
	struct xpc_partition *part = &xpc_partitions[partid];
	struct xpc_channel *ch;
	int n_needed;
	unsigned long irq_flags;

	dev_dbg(xpc_chan, "kthread starting, partid=%d, channel=%d\n",
		partid, ch_number);

	ch = &part->channels[ch_number];

	if (!(ch->flags & XPC_C_DISCONNECTING)) {

		/* let registerer know that connection has been established */

		spin_lock_irqsave(&ch->lock, irq_flags);
		if (!(ch->flags & XPC_C_CONNECTEDCALLOUT)) {
			ch->flags |= XPC_C_CONNECTEDCALLOUT;
			spin_unlock_irqrestore(&ch->lock, irq_flags);

			xpc_connected_callout(ch);

			spin_lock_irqsave(&ch->lock, irq_flags);
			ch->flags |= XPC_C_CONNECTEDCALLOUT_MADE;
			spin_unlock_irqrestore(&ch->lock, irq_flags);

			/*
			 * It is possible that while the callout was being
			 * made that the remote partition sent some messages.
			 * If that is the case, we may need to activate
			 * additional kthreads to help deliver them. We only
			 * need one less than total #of messages to deliver.
			 */
			n_needed = xpc_n_of_deliverable_payloads(ch) - 1;
			if (n_needed > 0 && !(ch->flags & XPC_C_DISCONNECTING))
				xpc_activate_kthreads(ch, n_needed);

		} else {
			spin_unlock_irqrestore(&ch->lock, irq_flags);
		}

		xpc_kthread_waitmsgs(part, ch);
	}

	/* let registerer know that connection is disconnecting */

	spin_lock_irqsave(&ch->lock, irq_flags);
	if ((ch->flags & XPC_C_CONNECTEDCALLOUT_MADE) &&
	    !(ch->flags & XPC_C_DISCONNECTINGCALLOUT)) {
		ch->flags |= XPC_C_DISCONNECTINGCALLOUT;
		spin_unlock_irqrestore(&ch->lock, irq_flags);

		xpc_disconnect_callout(ch, xpDisconnecting);

		spin_lock_irqsave(&ch->lock, irq_flags);
		ch->flags |= XPC_C_DISCONNECTINGCALLOUT_MADE;
	}
	spin_unlock_irqrestore(&ch->lock, irq_flags);

	if (atomic_dec_return(&ch->kthreads_assigned) == 0 &&
	    atomic_dec_return(&part->nchannels_engaged) == 0) {
		xpc_indicate_partition_disengaged(part);
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
xpc_create_kthreads(struct xpc_channel *ch, int needed,
		    int ignore_disconnecting)
{
	unsigned long irq_flags;
	u64 args = XPC_PACK_ARGS(ch->partid, ch->number);
	struct xpc_partition *part = &xpc_partitions[ch->partid];
	struct task_struct *kthread;

	while (needed-- > 0) {

		/*
		 * The following is done on behalf of the newly created
		 * kthread. That kthread is responsible for doing the
		 * counterpart to the following before it exits.
		 */
		if (ignore_disconnecting) {
			if (!atomic_inc_not_zero(&ch->kthreads_assigned)) {
				/* kthreads assigned had gone to zero */
				BUG_ON(!(ch->flags &
					 XPC_C_DISCONNECTINGCALLOUT_MADE));
				break;
			}

		} else if (ch->flags & XPC_C_DISCONNECTING) {
			break;

		} else if (atomic_inc_return(&ch->kthreads_assigned) == 1 &&
			   atomic_inc_return(&part->nchannels_engaged) == 1) {
				xpc_indicate_partition_engaged(part);
		}
		(void)xpc_part_ref(part);
		xpc_msgqueue_ref(ch);

		kthread = kthread_run(xpc_kthread_start, (void *)args,
				      "xpc%02dc%d", ch->partid, ch->number);
		if (IS_ERR(kthread)) {
			/* the fork failed */

			/*
			 * NOTE: if (ignore_disconnecting &&
			 * !(ch->flags & XPC_C_DISCONNECTINGCALLOUT)) is true,
			 * then we'll deadlock if all other kthreads assigned
			 * to this channel are blocked in the channel's
			 * registerer, because the only thing that will unblock
			 * them is the xpDisconnecting callout that this
			 * failed kthread_run() would have made.
			 */

			if (atomic_dec_return(&ch->kthreads_assigned) == 0 &&
			    atomic_dec_return(&part->nchannels_engaged) == 0) {
				xpc_indicate_partition_disengaged(part);
			}
			xpc_msgqueue_deref(ch);
			xpc_part_deref(part);

			if (atomic_read(&ch->kthreads_assigned) <
			    ch->kthreads_idle_limit) {
				/*
				 * Flag this as an error only if we have an
				 * insufficient #of kthreads for the channel
				 * to function.
				 */
				spin_lock_irqsave(&ch->lock, irq_flags);
				XPC_DISCONNECT_CHANNEL(ch, xpLackOfResources,
						       &irq_flags);
				spin_unlock_irqrestore(&ch->lock, irq_flags);
			}
			break;
		}
	}
}

void
xpc_disconnect_wait(int ch_number)
{
	unsigned long irq_flags;
	short partid;
	struct xpc_partition *part;
	struct xpc_channel *ch;
	int wakeup_channel_mgr;

	/* now wait for all callouts to the caller's function to cease */
	for (partid = 0; partid < xp_max_npartitions; partid++) {
		part = &xpc_partitions[partid];

		if (!xpc_part_ref(part))
			continue;

		ch = &part->channels[ch_number];

		if (!(ch->flags & XPC_C_WDISCONNECT)) {
			xpc_part_deref(part);
			continue;
		}

		wait_for_completion(&ch->wdisconnect_wait);

		spin_lock_irqsave(&ch->lock, irq_flags);
		DBUG_ON(!(ch->flags & XPC_C_DISCONNECTED));
		wakeup_channel_mgr = 0;

		if (ch->delayed_chctl_flags) {
			if (part->act_state != XPC_P_AS_DEACTIVATING) {
				spin_lock(&part->chctl_lock);
				part->chctl.flags[ch->number] |=
				    ch->delayed_chctl_flags;
				spin_unlock(&part->chctl_lock);
				wakeup_channel_mgr = 1;
			}
			ch->delayed_chctl_flags = 0;
		}

		ch->flags &= ~XPC_C_WDISCONNECT;
		spin_unlock_irqrestore(&ch->lock, irq_flags);

		if (wakeup_channel_mgr)
			xpc_wakeup_channel_mgr(part);

		xpc_part_deref(part);
	}
}

static int
xpc_setup_partitions(void)
{
	short partid;
	struct xpc_partition *part;

	xpc_partitions = kzalloc(sizeof(struct xpc_partition) *
				 xp_max_npartitions, GFP_KERNEL);
	if (xpc_partitions == NULL) {
		dev_err(xpc_part, "can't get memory for partition structure\n");
		return -ENOMEM;
	}

	/*
	 * The first few fields of each entry of xpc_partitions[] need to
	 * be initialized now so that calls to xpc_connect() and
	 * xpc_disconnect() can be made prior to the activation of any remote
	 * partition. NOTE THAT NONE OF THE OTHER FIELDS BELONGING TO THESE
	 * ENTRIES ARE MEANINGFUL UNTIL AFTER AN ENTRY'S CORRESPONDING
	 * PARTITION HAS BEEN ACTIVATED.
	 */
	for (partid = 0; partid < xp_max_npartitions; partid++) {
		part = &xpc_partitions[partid];

		DBUG_ON((u64)part != L1_CACHE_ALIGN((u64)part));

		part->activate_IRQ_rcvd = 0;
		spin_lock_init(&part->act_lock);
		part->act_state = XPC_P_AS_INACTIVE;
		XPC_SET_REASON(part, 0, 0);

		init_timer(&part->disengage_timer);
		part->disengage_timer.function =
		    xpc_timeout_partition_disengage;
		part->disengage_timer.data = (unsigned long)part;

		part->setup_state = XPC_P_SS_UNSET;
		init_waitqueue_head(&part->teardown_wq);
		atomic_set(&part->references, 0);
	}

	return xpc_setup_partitions_sn();
}

static void
xpc_teardown_partitions(void)
{
	kfree(xpc_partitions);
}

static void
xpc_do_exit(enum xp_retval reason)
{
	short partid;
	int active_part_count, printed_waiting_msg = 0;
	struct xpc_partition *part;
	unsigned long printmsg_time, disengage_timeout = 0;

	/* a 'rmmod XPC' and a 'reboot' cannot both end up here together */
	DBUG_ON(xpc_exiting == 1);

	/*
	 * Let the heartbeat checker thread and the discovery thread
	 * (if one is running) know that they should exit. Also wake up
	 * the heartbeat checker thread in case it's sleeping.
	 */
	xpc_exiting = 1;
	wake_up_interruptible(&xpc_activate_IRQ_wq);

	/* wait for the discovery thread to exit */
	wait_for_completion(&xpc_discovery_exited);

	/* wait for the heartbeat checker thread to exit */
	wait_for_completion(&xpc_hb_checker_exited);

	/* sleep for a 1/3 of a second or so */
	(void)msleep_interruptible(300);

	/* wait for all partitions to become inactive */

	printmsg_time = jiffies + (XPC_DEACTIVATE_PRINTMSG_INTERVAL * HZ);
	xpc_disengage_timedout = 0;

	do {
		active_part_count = 0;

		for (partid = 0; partid < xp_max_npartitions; partid++) {
			part = &xpc_partitions[partid];

			if (xpc_partition_disengaged(part) &&
			    part->act_state == XPC_P_AS_INACTIVE) {
				continue;
			}

			active_part_count++;

			XPC_DEACTIVATE_PARTITION(part, reason);

			if (part->disengage_timeout > disengage_timeout)
				disengage_timeout = part->disengage_timeout;
		}

		if (xpc_any_partition_engaged()) {
			if (time_is_before_jiffies(printmsg_time)) {
				dev_info(xpc_part, "waiting for remote "
					 "partitions to deactivate, timeout in "
					 "%ld seconds\n", (disengage_timeout -
					 jiffies) / HZ);
				printmsg_time = jiffies +
				    (XPC_DEACTIVATE_PRINTMSG_INTERVAL * HZ);
				printed_waiting_msg = 1;
			}

		} else if (active_part_count > 0) {
			if (printed_waiting_msg) {
				dev_info(xpc_part, "waiting for local partition"
					 " to deactivate\n");
				printed_waiting_msg = 0;
			}

		} else {
			if (!xpc_disengage_timedout) {
				dev_info(xpc_part, "all partitions have "
					 "deactivated\n");
			}
			break;
		}

		/* sleep for a 1/3 of a second or so */
		(void)msleep_interruptible(300);

	} while (1);

	DBUG_ON(xpc_any_partition_engaged());
	DBUG_ON(xpc_any_hbs_allowed() != 0);

	xpc_teardown_rsvd_page();

	if (reason == xpUnloading) {
		(void)unregister_die_notifier(&xpc_die_notifier);
		(void)unregister_reboot_notifier(&xpc_reboot_notifier);
	}

	/* clear the interface to XPC's functions */
	xpc_clear_interface();

	if (xpc_sysctl)
		unregister_sysctl_table(xpc_sysctl);

	xpc_teardown_partitions();

	if (is_shub())
		xpc_exit_sn2();
	else if (is_uv())
		xpc_exit_uv();
}

/*
 * This function is called when the system is being rebooted.
 */
static int
xpc_system_reboot(struct notifier_block *nb, unsigned long event, void *unused)
{
	enum xp_retval reason;

	switch (event) {
	case SYS_RESTART:
		reason = xpSystemReboot;
		break;
	case SYS_HALT:
		reason = xpSystemHalt;
		break;
	case SYS_POWER_OFF:
		reason = xpSystemPoweroff;
		break;
	default:
		reason = xpSystemGoingDown;
	}

	xpc_do_exit(reason);
	return NOTIFY_DONE;
}

/*
 * Notify other partitions to deactivate from us by first disengaging from all
 * references to our memory.
 */
static void
xpc_die_deactivate(void)
{
	struct xpc_partition *part;
	short partid;
	int any_engaged;
	long keep_waiting;
	long wait_to_print;

	/* keep xpc_hb_checker thread from doing anything (just in case) */
	xpc_exiting = 1;

	xpc_disallow_all_hbs();	/*indicate we're deactivated */

	for (partid = 0; partid < xp_max_npartitions; partid++) {
		part = &xpc_partitions[partid];

		if (xpc_partition_engaged(partid) ||
		    part->act_state != XPC_P_AS_INACTIVE) {
			xpc_request_partition_deactivation(part);
			xpc_indicate_partition_disengaged(part);
		}
	}

	/*
	 * Though we requested that all other partitions deactivate from us,
	 * we only wait until they've all disengaged or we've reached the
	 * defined timelimit.
	 *
	 * Given that one iteration through the following while-loop takes
	 * approximately 200 microseconds, calculate the #of loops to take
	 * before bailing and the #of loops before printing a waiting message.
	 */
	keep_waiting = xpc_disengage_timelimit * 1000 * 5;
	wait_to_print = XPC_DEACTIVATE_PRINTMSG_INTERVAL * 1000 * 5;

	while (1) {
		any_engaged = xpc_any_partition_engaged();
		if (!any_engaged) {
			dev_info(xpc_part, "all partitions have deactivated\n");
			break;
		}

		if (!keep_waiting--) {
			for (partid = 0; partid < xp_max_npartitions;
			     partid++) {
				if (xpc_partition_engaged(partid)) {
					dev_info(xpc_part, "deactivate from "
						 "remote partition %d timed "
						 "out\n", partid);
				}
			}
			break;
		}

		if (!wait_to_print--) {
			dev_info(xpc_part, "waiting for remote partitions to "
				 "deactivate, timeout in %ld seconds\n",
				 keep_waiting / (1000 * 5));
			wait_to_print = XPC_DEACTIVATE_PRINTMSG_INTERVAL *
			    1000 * 5;
		}

		udelay(200);
	}
}

/*
 * This function is called when the system is being restarted or halted due
 * to some sort of system failure. If this is the case we need to notify the
 * other partitions to disengage from all references to our memory.
 * This function can also be called when our heartbeater could be offlined
 * for a time. In this case we need to notify other partitions to not worry
 * about the lack of a heartbeat.
 */
static int
xpc_system_die(struct notifier_block *nb, unsigned long event, void *unused)
{
#ifdef CONFIG_IA64		/* !!! temporary kludge */
	switch (event) {
	case DIE_MACHINE_RESTART:
	case DIE_MACHINE_HALT:
		xpc_die_deactivate();
		break;

	case DIE_KDEBUG_ENTER:
		/* Should lack of heartbeat be ignored by other partitions? */
		if (!xpc_kdebug_ignore)
			break;

		/* fall through */
	case DIE_MCA_MONARCH_ENTER:
	case DIE_INIT_MONARCH_ENTER:
		xpc_offline_heartbeat();
		break;

	case DIE_KDEBUG_LEAVE:
		/* Is lack of heartbeat being ignored by other partitions? */
		if (!xpc_kdebug_ignore)
			break;

		/* fall through */
	case DIE_MCA_MONARCH_LEAVE:
	case DIE_INIT_MONARCH_LEAVE:
		xpc_online_heartbeat();
		break;
	}
#else
	xpc_die_deactivate();
#endif

	return NOTIFY_DONE;
}

int __init
xpc_init(void)
{
	int ret;
	struct task_struct *kthread;

	dev_set_name(xpc_part, "part");
	dev_set_name(xpc_chan, "chan");

	if (is_shub()) {
		/*
		 * The ia64-sn2 architecture supports at most 64 partitions.
		 * And the inability to unregister remote amos restricts us
		 * further to only support exactly 64 partitions on this
		 * architecture, no less.
		 */
		if (xp_max_npartitions != 64) {
			dev_err(xpc_part, "max #of partitions not set to 64\n");
			ret = -EINVAL;
		} else {
			ret = xpc_init_sn2();
		}

	} else if (is_uv()) {
		ret = xpc_init_uv();

	} else {
		ret = -ENODEV;
	}

	if (ret != 0)
		return ret;

	ret = xpc_setup_partitions();
	if (ret != 0) {
		dev_err(xpc_part, "can't get memory for partition structure\n");
		goto out_1;
	}

	xpc_sysctl = register_sysctl_table(xpc_sys_dir);

	/*
	 * Fill the partition reserved page with the information needed by
	 * other partitions to discover we are alive and establish initial
	 * communications.
	 */
	ret = xpc_setup_rsvd_page();
	if (ret != 0) {
		dev_err(xpc_part, "can't setup our reserved page\n");
		goto out_2;
	}

	/* add ourselves to the reboot_notifier_list */
	ret = register_reboot_notifier(&xpc_reboot_notifier);
	if (ret != 0)
		dev_warn(xpc_part, "can't register reboot notifier\n");

	/* add ourselves to the die_notifier list */
	ret = register_die_notifier(&xpc_die_notifier);
	if (ret != 0)
		dev_warn(xpc_part, "can't register die notifier\n");

	/*
	 * The real work-horse behind xpc.  This processes incoming
	 * interrupts and monitors remote heartbeats.
	 */
	kthread = kthread_run(xpc_hb_checker, NULL, XPC_HB_CHECK_THREAD_NAME);
	if (IS_ERR(kthread)) {
		dev_err(xpc_part, "failed while forking hb check thread\n");
		ret = -EBUSY;
		goto out_3;
	}

	/*
	 * Startup a thread that will attempt to discover other partitions to
	 * activate based on info provided by SAL. This new thread is short
	 * lived and will exit once discovery is complete.
	 */
	kthread = kthread_run(xpc_initiate_discovery, NULL,
			      XPC_DISCOVERY_THREAD_NAME);
	if (IS_ERR(kthread)) {
		dev_err(xpc_part, "failed while forking discovery thread\n");

		/* mark this new thread as a non-starter */
		complete(&xpc_discovery_exited);

		xpc_do_exit(xpUnloading);
		return -EBUSY;
	}

	/* set the interface to point at XPC's functions */
	xpc_set_interface(xpc_initiate_connect, xpc_initiate_disconnect,
			  xpc_initiate_send, xpc_initiate_send_notify,
			  xpc_initiate_received, xpc_initiate_partid_to_nasids);

	return 0;

	/* initialization was not successful */
out_3:
	xpc_teardown_rsvd_page();

	(void)unregister_die_notifier(&xpc_die_notifier);
	(void)unregister_reboot_notifier(&xpc_reboot_notifier);
out_2:
	if (xpc_sysctl)
		unregister_sysctl_table(xpc_sysctl);

	xpc_teardown_partitions();
out_1:
	if (is_shub())
		xpc_exit_sn2();
	else if (is_uv())
		xpc_exit_uv();
	return ret;
}

module_init(xpc_init);

void __exit
xpc_exit(void)
{
	xpc_do_exit(xpUnloading);
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

module_param(xpc_disengage_timelimit, int, 0);
MODULE_PARM_DESC(xpc_disengage_timelimit, "Number of seconds to wait "
		 "for disengage to complete.");

module_param(xpc_kdebug_ignore, int, 0);
MODULE_PARM_DESC(xpc_kdebug_ignore, "Should lack of heartbeat be ignored by "
		 "other partitions when dropping into kdebug.");
