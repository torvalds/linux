/* Copyright (c) 2012 - 2015 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

/* This driver lives in a spar partition, and registers to ethernet io
 * channels from the visorbus driver. It creates netdev devices and
 * forwards transmit to the IO channel and accepts rcvs from the IO
 * Partition via the IO channel.
 */

#include <linux/debugfs.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/kthread.h>

#include "visorbus.h"
#include "iochannel.h"

#define VISORNIC_INFINITE_RESPONSE_WAIT 0
#define VISORNICSOPENMAX 32
#define MAXDEVICES     16384

/* MAX_BUF = 64 lines x 32 MAXVNIC x 80 characters
 *         = 163840 bytes
 */
#define MAX_BUF 163840

static spinlock_t dev_num_pool_lock;
static void *dev_num_pool;	/**< pool to grab device numbers from */

static int visornic_probe(struct visor_device *dev);
static void visornic_remove(struct visor_device *dev);
static int visornic_pause(struct visor_device *dev,
			  visorbus_state_complete_func complete_func);
static int visornic_resume(struct visor_device *dev,
			   visorbus_state_complete_func complete_func);

/* DEBUGFS declarations */
static ssize_t info_debugfs_read(struct file *file, char __user *buf,
				 size_t len, loff_t *offset);
static ssize_t enable_ints_write(struct file *file, const char __user *buf,
				 size_t len, loff_t *ppos);
static struct dentry *visornic_debugfs_dir;
static const struct file_operations debugfs_info_fops = {
	.read = info_debugfs_read,
};

static const struct file_operations debugfs_enable_ints_fops = {
	.write = enable_ints_write,
};

static struct workqueue_struct *visornic_serverdown_workqueue;
static struct workqueue_struct *visornic_timeout_reset_workqueue;

/* GUIDS for director channel type supported by this driver.  */
static struct visor_channeltype_descriptor visornic_channel_types[] = {
	/* Note that the only channel type we expect to be reported by the
	 * bus driver is the SPAR_VNIC channel.
	 */
	{ SPAR_VNIC_CHANNEL_PROTOCOL_UUID, "ultravnic" },
	{ NULL_UUID_LE, NULL }
};

/* This is used to tell the visor bus driver which types of visor devices
 * we support, and what functions to call when a visor device that we support
 * is attached or removed.
 */
static struct visor_driver visornic_driver = {
	.name = "visornic",
	.version = "1.0.0.0",
	.vertag = NULL,
	.owner = THIS_MODULE,
	.channel_types = visornic_channel_types,
	.probe = visornic_probe,
	.remove = visornic_remove,
	.pause = visornic_pause,
	.resume = visornic_resume,
	.channel_interrupt = NULL,
};

struct visor_thread_info {
	struct task_struct *task;
	struct completion has_stopped;
	int id;
};

struct chanstat {
	unsigned long got_rcv;
	unsigned long got_enbdisack;
	unsigned long got_xmit_done;
	unsigned long xmit_fail;
	unsigned long sent_enbdis;
	unsigned long sent_promisc;
	unsigned long sent_post;
	unsigned long sent_xmit;
	unsigned long reject_count;
	unsigned long extra_rcvbufs_sent;
};

struct visornic_devdata {
	int devnum;
	int thread_wait_ms;
	unsigned short enabled;		/* 0 disabled 1 enabled to receive */
	unsigned short enab_dis_acked;	/* NET_RCV_ENABLE/DISABLE acked by
					 * IOPART
					 */
	struct visor_device *dev;
	char name[99];
	struct list_head list_all;   /* < link within list_all_devices list */
	struct kref kref;
	struct net_device *netdev;
	struct net_device_stats net_stats;
	atomic_t interrupt_rcvd;
	wait_queue_head_t rsp_queue;
	struct sk_buff **rcvbuf;
	u64 uniquenum; /* TODO figure out why not used */
	unsigned short old_flags;	/* flags as they were prior to
					 * set_multicast_list
					 */
	atomic_t usage;			/* count of users */
	int num_rcv_bufs;		/* indicates how many rcv buffers
					 * the vnic will post
					 */
	int num_rcv_bufs_could_not_alloc;
	atomic_t num_rcvbuf_in_iovm;
	unsigned long alloc_failed_in_if_needed_cnt;
	unsigned long alloc_failed_in_repost_rtn_cnt;
	int max_outstanding_net_xmits;   /* absolute max number of outstanding
					  * xmits - should never hit this
					  */
	int upper_threshold_net_xmits;   /* high water mark for calling
					  * netif_stop_queue()
					  */
	int lower_threshold_net_xmits;	 /* high water mark for calling
					  * netif_wake_queue()
					  */
	struct sk_buff_head xmitbufhead; /* xmitbufhead is the head of the
					  * xmit buffer list that have been
					  * sent to the IOPART end
					  */
	struct work_struct serverdown_completion;
	struct work_struct timeout_reset;
	struct uiscmdrsp *cmdrsp_rcv;	 /* cmdrsp_rcv is used for
					  * posting/unposting rcv buffers
					  */
	struct uiscmdrsp *xmit_cmdrsp;	 /* used to issue NET_XMIT - there is
					  * never more that one xmit in
					  * progress at a time
					  */
	bool server_down;		 /* IOPART is down */
	bool server_change_state;	 /* Processing SERVER_CHANGESTATE msg */
	struct dentry *eth_debugfs_dir;
	struct visor_thread_info threadinfo;
	u64 interrupts_rcvd;
	u64 interrupts_notme;
	u64 interrupts_disabled;
	u64 busy_cnt;
	spinlock_t priv_lock;  /* spinlock to access devdata structures */

	/* flow control counter */
	u64 flow_control_upper_hits;
	u64 flow_control_lower_hits;

	/* debug counters */
	unsigned long n_rcv0;			/* # rcvs of 0 buffers */
	unsigned long n_rcv1;			/* # rcvs of 1 buffers */
	unsigned long n_rcv2;			/* # rcvs of 2 buffers */
	unsigned long n_rcvx;			/* # rcvs of >2 buffers */
	unsigned long found_repost_rcvbuf_cnt;	/* # times we called
						 *   repost_rcvbuf_cnt
						 */
	unsigned long repost_found_skb_cnt;	/* # times found the skb */
	unsigned long n_repost_deficit;		/* # times we couldn't find
						 *   all of the rcv buffers
						 */
	unsigned long bad_rcv_buf;		/* # times we negleted to
						 * free the rcv skb because
						 * we didn't know where it
						 * came from
						 */
	unsigned long n_rcv_packets_not_accepted;/* # bogs rcv packets */

	int queuefullmsg_logged;
	struct chanstat chstat;
};

/* array of open devices maintained by open() and close() */
static struct net_device *num_visornic_open[VISORNICSOPENMAX];

/* List of all visornic_devdata structs,
 * linked via the list_all member
 */
static LIST_HEAD(list_all_devices);
static DEFINE_SPINLOCK(lock_all_devices);

/**
 *	visor_copy_fragsinfo_from_skb(
 *	@skb_in: skbuff that we are pulling the frags from
 *	@firstfraglen: length of first fragment in skb
 *	@frags_max: max len of frags array
 *	@frags: frags array filled in on output
 *
 *	Copy the fragment list in the SKB to a phys_info
 *	array that the IOPART understands.
 *	Return value indicates number of entries filled in frags
 *	Negative values indicate an error.
 */
static unsigned int
visor_copy_fragsinfo_from_skb(struct sk_buff *skb, unsigned int firstfraglen,
			      unsigned int frags_max,
			      struct phys_info frags[])
{
	unsigned int count = 0, ii, size, offset = 0, numfrags;

	numfrags = skb_shinfo(skb)->nr_frags;

	while (firstfraglen) {
		if (count == frags_max)
			return -EINVAL;

		frags[count].pi_pfn =
			page_to_pfn(virt_to_page(skb->data + offset));
		frags[count].pi_off =
			(unsigned long)(skb->data + offset) & PI_PAGE_MASK;
		size = min_t(unsigned int, firstfraglen,
			     PI_PAGE_SIZE - frags[count].pi_off);

		/* can take smallest of firstfraglen (what's left) OR
		 * bytes left in the page
		 */
		frags[count].pi_len = size;
		firstfraglen -= size;
		offset += size;
		count++;
	}
	if (numfrags) {
		if ((count + numfrags) > frags_max)
			return -EINVAL;

		for (ii = 0; ii < numfrags; ii++) {
			count = add_physinfo_entries(page_to_pfn(
				skb_frag_page(&skb_shinfo(skb)->frags[ii])),
					      skb_shinfo(skb)->frags[ii].
					      page_offset,
					      skb_shinfo(skb)->frags[ii].
					      size, count, frags_max, frags);
			if (!count)
				return -EIO;
		}
	}
	if (skb_shinfo(skb)->frag_list) {
		struct sk_buff *skbinlist;
		int c;

		for (skbinlist = skb_shinfo(skb)->frag_list; skbinlist;
		     skbinlist = skbinlist->next) {
			c = visor_copy_fragsinfo_from_skb(skbinlist,
							  skbinlist->len -
							  skbinlist->data_len,
							  frags_max - count,
							  &frags[count]);
			if (c < 0)
				return c;
			count += c;
		}
	}
	return count;
}

/**
 *	visort_thread_start - starts thread for the device
 *	@thrinfo: The thread to start
 *	@threadfn: Function the thread starts
 *	@thrcontext: Context to pass to the thread, i.e. devdata
 *	@name:	string describing name of thread
 *
 *	Starts a thread for the device, currently only thread is
 *	process_incoming_rsps
 *	Returns 0 on success;
 */
static int visor_thread_start(struct visor_thread_info *thrinfo,
			      int (*threadfn)(void *),
			      void *thrcontext, char *name)
{
	/* used to stop the thread */
	init_completion(&thrinfo->has_stopped);
	thrinfo->task = kthread_run(threadfn, thrcontext, name);
	if (IS_ERR(thrinfo->task)) {
		thrinfo->id = 0;
		return -EINVAL;
	}
	thrinfo->id = thrinfo->task->pid;
	return 0;
}

/**
 *	visor_thread_stop - stop a thread for the device
 *	@thrinfo: The thread to stop
 *
 *	Stop the thread and wait for completion for a minute
 *	Returns void.
 */
static void visor_thread_stop(struct visor_thread_info *thrinfo)
{
	if (!thrinfo->id)
		return;	/* thread not running */

	kthread_stop(thrinfo->task);
	/* give up if the thread has NOT died in 1 minute */
	if (wait_for_completion_timeout(&thrinfo->has_stopped, 60 * HZ))
		thrinfo->id = 0;
}

/* DebugFS code */
static ssize_t info_debugfs_read(struct file *file, char __user *buf,
				 size_t len, loff_t *offset)
{
	int i;
	ssize_t bytes_read = 0;
	int str_pos = 0;
	struct visornic_devdata *devdata;
	char *vbuf;

	if (len > MAX_BUF)
		len = MAX_BUF;
	vbuf = kzalloc(len, GFP_KERNEL);
	if (!vbuf)
		return -ENOMEM;

	/* for each vnic channel
	 * dump out channel specific data
	 */
	for (i = 0; i < VISORNICSOPENMAX; i++) {
		if (!num_visornic_open[i])
			continue;

		devdata = netdev_priv(num_visornic_open[i]);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     "Vnic i = %d\n", i);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     "netdev = %s (0x%p), MAC Addr %pM\n",
				     num_visornic_open[i]->name,
				     num_visornic_open[i],
				     num_visornic_open[i]->dev_addr);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     "VisorNic Dev Info = 0x%p\n", devdata);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     " num_rcv_bufs = %d\n",
				     devdata->num_rcv_bufs);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     " max_oustanding_next_xmits = %d\n",
				    devdata->max_outstanding_net_xmits);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     " upper_threshold_net_xmits = %d\n",
				     devdata->upper_threshold_net_xmits);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     " lower_threshold_net_xmits = %d\n",
				     devdata->lower_threshold_net_xmits);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     " queuefullmsg_logged = %d\n",
				     devdata->queuefullmsg_logged);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     " chstat.got_rcv = %lu\n",
				     devdata->chstat.got_rcv);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     " chstat.got_enbdisack = %lu\n",
				     devdata->chstat.got_enbdisack);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     " chstat.got_xmit_done = %lu\n",
				     devdata->chstat.got_xmit_done);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     " chstat.xmit_fail = %lu\n",
				     devdata->chstat.xmit_fail);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     " chstat.sent_enbdis = %lu\n",
				     devdata->chstat.sent_enbdis);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     " chstat.sent_promisc = %lu\n",
				     devdata->chstat.sent_promisc);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     " chstat.sent_post = %lu\n",
				     devdata->chstat.sent_post);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     " chstat.sent_xmit = %lu\n",
				     devdata->chstat.sent_xmit);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     " chstat.reject_count = %lu\n",
				     devdata->chstat.reject_count);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     " chstat.extra_rcvbufs_sent = %lu\n",
				     devdata->chstat.extra_rcvbufs_sent);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     " n_rcv0 = %lu\n", devdata->n_rcv0);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     " n_rcv1 = %lu\n", devdata->n_rcv1);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     " n_rcv2 = %lu\n", devdata->n_rcv2);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     " n_rcvx = %lu\n", devdata->n_rcvx);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     " num_rcvbuf_in_iovm = %d\n",
				     atomic_read(&devdata->num_rcvbuf_in_iovm));
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     " alloc_failed_in_if_needed_cnt = %lu\n",
				     devdata->alloc_failed_in_if_needed_cnt);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     " alloc_failed_in_repost_rtn_cnt = %lu\n",
				     devdata->alloc_failed_in_repost_rtn_cnt);
		/* str_pos += scnprintf(vbuf + str_pos, len - str_pos,
		 *		     " inner_loop_limit_reached_cnt = %lu\n",
		 *		     devdata->inner_loop_limit_reached_cnt);
		 */
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     " found_repost_rcvbuf_cnt = %lu\n",
				     devdata->found_repost_rcvbuf_cnt);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     " repost_found_skb_cnt = %lu\n",
				     devdata->repost_found_skb_cnt);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     " n_repost_deficit = %lu\n",
				     devdata->n_repost_deficit);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     " bad_rcv_buf = %lu\n",
				     devdata->bad_rcv_buf);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     " n_rcv_packets_not_accepted = %lu\n",
				     devdata->n_rcv_packets_not_accepted);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     " interrupts_rcvd = %llu\n",
				     devdata->interrupts_rcvd);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     " interrupts_notme = %llu\n",
				     devdata->interrupts_notme);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     " interrupts_disabled = %llu\n",
				     devdata->interrupts_disabled);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     " busy_cnt = %llu\n",
				     devdata->busy_cnt);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     " flow_control_upper_hits = %llu\n",
				     devdata->flow_control_upper_hits);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     " flow_control_lower_hits = %llu\n",
				     devdata->flow_control_lower_hits);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     " thread_wait_ms = %d\n",
				     devdata->thread_wait_ms);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				     " netif_queue = %s\n",
				     netif_queue_stopped(devdata->netdev) ?
				     "stopped" : "running");
	}
	bytes_read = simple_read_from_buffer(buf, len, offset, vbuf, str_pos);
	kfree(vbuf);
	return bytes_read;
}

static ssize_t enable_ints_write(struct file *file,
				 const char __user *buffer,
				 size_t count, loff_t *ppos)
{
	char buf[4];
	int i, new_value;
	struct visornic_devdata *devdata;

	if (count >= ARRAY_SIZE(buf))
		return -EINVAL;

	buf[count] = '\0';
	if (copy_from_user(buf, buffer, count))
		return -EFAULT;

	i = kstrtoint(buf, 10, &new_value);
	if (i != 0)
		return -EFAULT;

	/* set all counts to new_value usually 0 */
	for (i = 0; i < VISORNICSOPENMAX; i++) {
		if (num_visornic_open[i]) {
			devdata = netdev_priv(num_visornic_open[i]);
			/* TODO update features bit in channel */
		}
	}

	return count;
}

/**
 *	visornic_serverdown_complete - IOPART went down, need to pause
 *				       device
 *	@work: Work queue it was scheduled on
 *
 *	The IO partition has gone down and we need to do some cleanup
 *	for when it comes back. Treat the IO partition as the link
 *	being down.
 *	Returns void.
 */
static void
visornic_serverdown_complete(struct work_struct *work)
{
	struct visornic_devdata *devdata;
	struct net_device *netdev;
	unsigned long flags;
	int i = 0, count = 0;

	devdata = container_of(work, struct visornic_devdata,
			       serverdown_completion);
	netdev = devdata->netdev;

	/* Stop using datachan */
	visor_thread_stop(&devdata->threadinfo);

	/* Inform Linux that the link is down */
	netif_carrier_off(netdev);
	netif_stop_queue(netdev);

	/* Free the skb for XMITs that haven't been serviced by the server
	 * We shouldn't have to inform Linux about these IOs because they
	 * are "lost in the ethernet"
	 */
	skb_queue_purge(&devdata->xmitbufhead);

	spin_lock_irqsave(&devdata->priv_lock, flags);
	/* free rcv buffers */
	for (i = 0; i < devdata->num_rcv_bufs; i++) {
		if (devdata->rcvbuf[i]) {
			kfree_skb(devdata->rcvbuf[i]);
			devdata->rcvbuf[i] = NULL;
			count++;
		}
	}
	atomic_set(&devdata->num_rcvbuf_in_iovm, 0);
	spin_unlock_irqrestore(&devdata->priv_lock, flags);

	devdata->server_down = true;
	devdata->server_change_state = false;
}

/**
 *	visornic_serverdown - Command has notified us that IOPARt is down
 *	@devdata: device that is being managed by IOPART
 *
 *	Schedule the work needed to handle the server down request. Make
 *	sure we haven't already handled the server change state event.
 *	Returns 0 if we scheduled the work, -EINVAL on error.
 */
static int
visornic_serverdown(struct visornic_devdata *devdata)
{
	if (!devdata->server_down && !devdata->server_change_state) {
		devdata->server_change_state = true;
		queue_work(visornic_serverdown_workqueue,
			   &devdata->serverdown_completion);
	} else if (devdata->server_change_state) {
		return -EINVAL;
	}
	return 0;
}

/**
 *	alloc_rcv_buf	- alloc rcv buffer to be given to the IO Partition.
 *	@netdev: network adapter the rcv bufs are attached too.
 *
 *	Create an sk_buff (rcv_buf) that will be passed to the IO Partition
 *	so that it can write rcv data into our memory space.
 *	Return pointer to sk_buff
 */
static struct sk_buff *
alloc_rcv_buf(struct net_device *netdev)
{
	struct sk_buff *skb;

	/* NOTE: the first fragment in each rcv buffer is pointed to by
	 * rcvskb->data. For now all rcv buffers will be RCVPOST_BUF_SIZE
	 * in length, so the firstfrag is large enough to hold 1514.
	 */
	skb = alloc_skb(RCVPOST_BUF_SIZE, GFP_ATOMIC);
	if (!skb)
		return NULL;
	skb->dev = netdev;
	skb->len = RCVPOST_BUF_SIZE;
	/* current value of mtu doesn't come into play here; large
	 * packets will just end up using multiple rcv buffers all of
	 * same size
	 */
	skb->data_len = 0;      /* dev_alloc_skb already zeroes it out
				 * for clarification.
				 */
	return skb;
}

/**
 *	post_skb	- post a skb to the IO Partition.
 *	@cmdrsp: cmdrsp packet to be send to the IO Partition
 *	@devdata: visornic_devdata to post the skb too
 *	@skb: skb to give to the IO partition
 *
 *	Send the skb to the IO Partition.
 *	Returns void
 */
static inline void
post_skb(struct uiscmdrsp *cmdrsp,
	 struct visornic_devdata *devdata, struct sk_buff *skb)
{
	cmdrsp->net.buf = skb;
	cmdrsp->net.rcvpost.frag.pi_pfn = page_to_pfn(virt_to_page(skb->data));
	cmdrsp->net.rcvpost.frag.pi_off =
		(unsigned long)skb->data & PI_PAGE_MASK;
	cmdrsp->net.rcvpost.frag.pi_len = skb->len;
	cmdrsp->net.rcvpost.unique_num = devdata->uniquenum;

	if ((cmdrsp->net.rcvpost.frag.pi_off + skb->len) <= PI_PAGE_SIZE) {
		cmdrsp->net.type = NET_RCV_POST;
		cmdrsp->cmdtype = CMD_NET_TYPE;
		visorchannel_signalinsert(devdata->dev->visorchannel,
					  IOCHAN_TO_IOPART,
					  cmdrsp);
		atomic_inc(&devdata->num_rcvbuf_in_iovm);
		devdata->chstat.sent_post++;
	}
}

/**
 *	send_enbdis	- send NET_RCV_ENBDIS to IO Partition
 *	@netdev: netdevice we are enable/disable, used as context
 *		 return value
 *	@state: enable = 1/disable = 0
 *	@devdata: visornic device we are enabling/disabling
 *
 *	Send the enable/disable message to the IO Partition.
 *	Returns void
 */
static void
send_enbdis(struct net_device *netdev, int state,
	    struct visornic_devdata *devdata)
{
	devdata->cmdrsp_rcv->net.enbdis.enable = state;
	devdata->cmdrsp_rcv->net.enbdis.context = netdev;
	devdata->cmdrsp_rcv->net.type = NET_RCV_ENBDIS;
	devdata->cmdrsp_rcv->cmdtype = CMD_NET_TYPE;
	visorchannel_signalinsert(devdata->dev->visorchannel,
				  IOCHAN_TO_IOPART,
				  devdata->cmdrsp_rcv);
	devdata->chstat.sent_enbdis++;
}

/**
 *	visornic_disable_with_timeout - Disable network adapter
 *	@netdev: netdevice to disale
 *	@timeout: timeout to wait for disable
 *
 *	Disable the network adapter and inform the IO Partition that we
 *	are disabled, reclaim memory from rcv bufs.
 *	Returns 0 on success, negative for failure of IO Partition
 *	responding.
 *
 */
static int
visornic_disable_with_timeout(struct net_device *netdev, const int timeout)
{
	struct visornic_devdata *devdata = netdev_priv(netdev);
	int i;
	unsigned long flags;
	int wait = 0;

	/* stop the transmit queue so nothing more can be transmitted */
	netif_stop_queue(netdev);

	/* send a msg telling the other end we are stopping incoming pkts */
	spin_lock_irqsave(&devdata->priv_lock, flags);
	devdata->enabled = 0;
	devdata->enab_dis_acked = 0; /* must wait for ack */
	spin_unlock_irqrestore(&devdata->priv_lock, flags);

	/* send disable and wait for ack -- don't hold lock when sending
	 * disable because if the queue is full, insert might sleep.
	 */
	send_enbdis(netdev, 0, devdata);

	/* wait for ack to arrive before we try to free rcv buffers
	 * NOTE: the other end automatically unposts the rcv buffers when
	 * when it gets a disable.
	 */
	spin_lock_irqsave(&devdata->priv_lock, flags);
	while ((timeout == VISORNIC_INFINITE_RESPONSE_WAIT) ||
	       (wait < timeout)) {
		if (devdata->enab_dis_acked)
			break;
		if (devdata->server_down || devdata->server_change_state) {
			spin_unlock_irqrestore(&devdata->priv_lock, flags);
			return -EIO;
		}
		set_current_state(TASK_INTERRUPTIBLE);
		spin_unlock_irqrestore(&devdata->priv_lock, flags);
		wait += schedule_timeout(msecs_to_jiffies(10));
		spin_lock_irqsave(&devdata->priv_lock, flags);
	}

	/* Wait for usage to go to 1 (no other users) before freeing
	 * rcv buffers
	 */
	if (atomic_read(&devdata->usage) > 1) {
		while (1) {
			set_current_state(TASK_INTERRUPTIBLE);
			spin_unlock_irqrestore(&devdata->priv_lock, flags);
			schedule_timeout(msecs_to_jiffies(10));
			spin_lock_irqsave(&devdata->priv_lock, flags);
			if (atomic_read(&devdata->usage))
				break;
		}
	}

	/* we've set enabled to 0, so we can give up the lock. */
	spin_unlock_irqrestore(&devdata->priv_lock, flags);

	/* Free rcv buffers - other end has automatically unposed them on
	 * disable
	 */
	for (i = 0; i < devdata->num_rcv_bufs; i++) {
		if (devdata->rcvbuf[i]) {
			kfree_skb(devdata->rcvbuf[i]);
			devdata->rcvbuf[i] = NULL;
		}
	}

	/* remove references from array */
	for (i = 0; i < VISORNICSOPENMAX; i++)
		if (num_visornic_open[i] == netdev) {
			num_visornic_open[i] = NULL;
			break;
		}

	return 0;
}

/**
 *	init_rcv_bufs  -- initialize receive bufs and send them to the IO Part
 *	@netdev: struct netdevice
 *	@devdata: visornic_devdata
 *
 *	Allocate rcv buffers and post them to the IO Partition.
 *	Return 0 for success, and negative for failure.
 */
static int
init_rcv_bufs(struct net_device *netdev, struct visornic_devdata *devdata)
{
	int i, count;

	/* allocate fixed number of receive buffers to post to uisnic
	 * post receive buffers after we've allocated a required amount
	 */
	for (i = 0; i < devdata->num_rcv_bufs; i++) {
		devdata->rcvbuf[i] = alloc_rcv_buf(netdev);
		if (!devdata->rcvbuf[i])
			break; /* if we failed to allocate one let us stop */
	}
	if (i == 0) /* couldn't even allocate one -- bail out */
		return -ENOMEM;
	count = i;

	/* Ensure we can alloc 2/3rd of the requeested number of buffers.
	 * 2/3 is an arbitrary choice; used also in ndis init.c
	 */
	if (count < ((2 * devdata->num_rcv_bufs) / 3)) {
		/* free receive buffers we did alloc and then bail out */
		for (i = 0; i < count; i++) {
			kfree_skb(devdata->rcvbuf[i]);
			devdata->rcvbuf[i] = NULL;
		}
		return -ENOMEM;
	}

	/* post receive buffers to receive incoming input - without holding
	 * lock - we've not enabled nor started the queue so there shouldn't
	 * be any rcv or xmit activity
	 */
	for (i = 0; i < count; i++)
		post_skb(devdata->cmdrsp_rcv, devdata, devdata->rcvbuf[i]);

	return 0;
}

/**
 *	visornic_enable_with_timeout	- send enable to IO Part
 *	@netdev: struct net_device
 *	@timeout: Time to wait for the ACK from the enable
 *
 *	Sends enable to IOVM, inits, and posts receive buffers to IOVM
 *	timeout is defined in msecs (timeout of 0 specifies infinite wait)
 *	Return 0 for success, negavite for failure.
 */
static int
visornic_enable_with_timeout(struct net_device *netdev, const int timeout)
{
	int i;
	struct visornic_devdata *devdata = netdev_priv(netdev);
	unsigned long flags;
	int wait = 0;

	/* NOTE: the other end automatically unposts the rcv buffers when it
	 * gets a disable.
	 */
	i = init_rcv_bufs(netdev, devdata);
	if (i < 0)
		return i;

	spin_lock_irqsave(&devdata->priv_lock, flags);
	devdata->enabled = 1;

	/* now we're ready, let's send an ENB to uisnic but until we get
	 * an ACK back from uisnic, we'll drop the packets
	 */
	devdata->n_rcv_packets_not_accepted = 0;
	spin_unlock_irqrestore(&devdata->priv_lock, flags);

	/* send enable and wait for ack -- don't hold lock when sending enable
	 * because if the queue is full, insert might sleep.
	 */
	send_enbdis(netdev, 1, devdata);

	spin_lock_irqsave(&devdata->priv_lock, flags);
	while ((timeout == VISORNIC_INFINITE_RESPONSE_WAIT) ||
	       (wait < timeout)) {
		if (devdata->enab_dis_acked)
			break;
		if (devdata->server_down || devdata->server_change_state) {
			spin_unlock_irqrestore(&devdata->priv_lock, flags);
			return -EIO;
		}
		set_current_state(TASK_INTERRUPTIBLE);
		spin_unlock_irqrestore(&devdata->priv_lock, flags);
		wait += schedule_timeout(msecs_to_jiffies(10));
		spin_lock_irqsave(&devdata->priv_lock, flags);
	}

	spin_unlock_irqrestore(&devdata->priv_lock, flags);

	if (!devdata->enab_dis_acked)
		return -EIO;

	/* find an open slot in the array to save off VisorNic references
	 * for debug
	 */
	for (i = 0; i < VISORNICSOPENMAX; i++) {
		if (!num_visornic_open[i]) {
			num_visornic_open[i] = netdev;
			break;
		}
	}

	return 0;
}

/**
 *	visornic_timeout_reset	- handle xmit timeout resets
 *	@work	work item that scheduled the work
 *
 *	Transmit Timeouts are typically handled by resetting the
 *	device for our virtual NIC we will send a Disable and Enable
 *	to the IOVM. If it doesn't respond we will trigger a serverdown.
 */
static void
visornic_timeout_reset(struct work_struct *work)
{
	struct visornic_devdata *devdata;
	struct net_device *netdev;
	int response = 0;

	devdata = container_of(work, struct visornic_devdata, timeout_reset);
	netdev = devdata->netdev;

	netif_stop_queue(netdev);
	response = visornic_disable_with_timeout(netdev, 100);
	if (response)
		goto call_serverdown;

	response = visornic_enable_with_timeout(netdev, 100);
	if (response)
		goto call_serverdown;
	netif_wake_queue(netdev);

	return;

call_serverdown:
	visornic_serverdown(devdata);
}

/**
 *	visornic_open - Enable the visornic device and mark the queue started
 *	@netdev: netdevice to start
 *
 *      Enable the device and start the transmit queue.
 *      Return 0 for success
 */
static int
visornic_open(struct net_device *netdev)
{
	visornic_enable_with_timeout(netdev, VISORNIC_INFINITE_RESPONSE_WAIT);

	/* start the interface's transmit queue, allowing it to accept
	 * packets for transmission
	 */
	netif_start_queue(netdev);

	return 0;
}

/**
 *	visornic_close - Disables the visornic device and stops the queues
 *	@netdev: netdevice to start
 *
 *      Disable the device and stop the transmit queue.
 *      Return 0 for success
 */
static int
visornic_close(struct net_device *netdev)
{
	netif_stop_queue(netdev);
	visornic_disable_with_timeout(netdev, VISORNIC_INFINITE_RESPONSE_WAIT);

	return 0;
}

/**
 *	visornic_xmit - send a packet to the IO Partition
 *	@skb: Packet to be sent
 *	@netdev: net device the packet is being sent from
 *
 *	Convert the skb to a cmdrsp so the IO Partition can undersand it.
 *	Send the XMIT command to the IO Partition for processing. This
 *	function is protected from concurrent calls by a spinlock xmit_lock
 *	in the net_device struct, but as soon as the function returns it
 *	can be called again.
 *	Returns NETDEV_TX_OK for success, NETDEV_TX_BUSY for error.
 */
static int
visornic_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct visornic_devdata *devdata;
	int len, firstfraglen, padlen;
	struct uiscmdrsp *cmdrsp = NULL;
	unsigned long flags;

	devdata = netdev_priv(netdev);
	spin_lock_irqsave(&devdata->priv_lock, flags);

	if (netif_queue_stopped(netdev) || devdata->server_down ||
	    devdata->server_change_state) {
		spin_unlock_irqrestore(&devdata->priv_lock, flags);
		devdata->busy_cnt++;
		return NETDEV_TX_BUSY;
	}

	/* sk_buff struct is used to host network data throughout all the
	 * linux network subsystems
	 */
	len = skb->len;

	/* skb->len is the FULL length of data (including fragmentary portion)
	 * skb->data_len is the length of the fragment portion in frags
	 * skb->len - skb->data_len is size of the 1st fragment in skb->data
	 * calculate the length of the first fragment that skb->data is
	 * pointing to
	 */
	firstfraglen = skb->len - skb->data_len;
	if (firstfraglen < ETH_HEADER_SIZE) {
		spin_unlock_irqrestore(&devdata->priv_lock, flags);
		devdata->busy_cnt++;
		return NETDEV_TX_BUSY;
	}

	if ((len < ETH_MIN_PACKET_SIZE) &&
	    ((skb_end_pointer(skb) - skb->data) >= ETH_MIN_PACKET_SIZE)) {
		/* pad the packet out to minimum size */
		padlen = ETH_MIN_PACKET_SIZE - len;
		memset(&skb->data[len], 0, padlen);
		skb->tail += padlen;
		skb->len += padlen;
		len += padlen;
		firstfraglen += padlen;
	}

	cmdrsp = devdata->xmit_cmdrsp;
	/* clear cmdrsp */
	memset(cmdrsp, 0, SIZEOF_CMDRSP);
	cmdrsp->net.type = NET_XMIT;
	cmdrsp->cmdtype = CMD_NET_TYPE;

	/* save the pointer to skb -- we'll need it for completion */
	cmdrsp->net.buf = skb;

	if (((devdata->chstat.sent_xmit >= devdata->chstat.got_xmit_done) &&
	     (devdata->chstat.sent_xmit - devdata->chstat.got_xmit_done >=
	     devdata->max_outstanding_net_xmits)) ||
	     ((devdata->chstat.sent_xmit < devdata->chstat.got_xmit_done) &&
	     (ULONG_MAX - devdata->chstat.got_xmit_done +
	      devdata->chstat.sent_xmit >=
	      devdata->max_outstanding_net_xmits))) {
		/* too many NET_XMITs queued over to IOVM - need to wait
		 */
		devdata->chstat.reject_count++;
		if (!devdata->queuefullmsg_logged &&
		    ((devdata->chstat.reject_count & 0x3ff) == 1))
			devdata->queuefullmsg_logged = 1;
		netif_stop_queue(netdev);
		spin_unlock_irqrestore(&devdata->priv_lock, flags);
		devdata->busy_cnt++;
		return NETDEV_TX_BUSY;
	}
	if (devdata->queuefullmsg_logged)
		devdata->queuefullmsg_logged = 0;

	if (skb->ip_summed == CHECKSUM_UNNECESSARY) {
		cmdrsp->net.xmt.lincsum.valid = 1;
		cmdrsp->net.xmt.lincsum.protocol = skb->protocol;
		if (skb_transport_header(skb) > skb->data) {
			cmdrsp->net.xmt.lincsum.hrawoff =
				skb_transport_header(skb) - skb->data;
			cmdrsp->net.xmt.lincsum.hrawoff = 1;
		}
		if (skb_network_header(skb) > skb->data) {
			cmdrsp->net.xmt.lincsum.nhrawoff =
				skb_network_header(skb) - skb->data;
			cmdrsp->net.xmt.lincsum.nhrawoffv = 1;
		}
		cmdrsp->net.xmt.lincsum.csum = skb->csum;
	} else {
		cmdrsp->net.xmt.lincsum.valid = 0;
	}

	/* save off the length of the entire data packet */
	cmdrsp->net.xmt.len = len;

	/* copy ethernet header from first frag into ocmdrsp
	 * - everything else will be pass in frags & DMA'ed
	 */
	memcpy(cmdrsp->net.xmt.ethhdr, skb->data, ETH_HEADER_SIZE);
	/* copy frags info - from skb->data we need to only provide access
	 * beyond eth header
	 */
	cmdrsp->net.xmt.num_frags =
		visor_copy_fragsinfo_from_skb(skb, firstfraglen,
					      MAX_PHYS_INFO,
					      cmdrsp->net.xmt.frags);
	if (cmdrsp->net.xmt.num_frags == -1) {
		spin_unlock_irqrestore(&devdata->priv_lock, flags);
		devdata->busy_cnt++;
		return NETDEV_TX_BUSY;
	}

	if (!visorchannel_signalinsert(devdata->dev->visorchannel,
				       IOCHAN_TO_IOPART, cmdrsp)) {
		netif_stop_queue(netdev);
		spin_unlock_irqrestore(&devdata->priv_lock, flags);
		devdata->busy_cnt++;
		return NETDEV_TX_BUSY;
	}

	/* Track the skbs that have been sent to the IOVM for XMIT */
	skb_queue_head(&devdata->xmitbufhead, skb);

	/* set the last transmission start time
	 * linux doc says: Do not forget to update netdev->trans_start to
	 * jiffies after each new tx packet is given to the hardware.
	 */
	netdev->trans_start = jiffies;

	/* update xmt stats */
	devdata->net_stats.tx_packets++;
	devdata->net_stats.tx_bytes += skb->len;
	devdata->chstat.sent_xmit++;

	/* check to see if we have hit the high watermark for
	 * netif_stop_queue()
	 */
	if (((devdata->chstat.sent_xmit >= devdata->chstat.got_xmit_done) &&
	     (devdata->chstat.sent_xmit - devdata->chstat.got_xmit_done >=
	      devdata->upper_threshold_net_xmits)) ||
	    ((devdata->chstat.sent_xmit < devdata->chstat.got_xmit_done) &&
	     (ULONG_MAX - devdata->chstat.got_xmit_done +
	      devdata->chstat.sent_xmit >=
	      devdata->upper_threshold_net_xmits))) {
		/* too many NET_XMITs queued over to IOVM - need to wait */
		netif_stop_queue(netdev); /* calling stop queue - call
					   * netif_wake_queue() after lower
					   * threshold
					   */
		devdata->flow_control_upper_hits++;
	}
	spin_unlock_irqrestore(&devdata->priv_lock, flags);

	/* skb will be freed when we get back NET_XMIT_DONE */
	return NETDEV_TX_OK;
}

/**
 *	visornic_get_stats - returns net_stats of the visornic device
 *	@netdev: netdevice
 *
 *	Returns the net_device_stats for the device
 */
static struct net_device_stats *
visornic_get_stats(struct net_device *netdev)
{
	struct visornic_devdata *devdata = netdev_priv(netdev);

	return &devdata->net_stats;
}

/**
 *	visornic_ioctl - ioctl function for netdevice.
 *	@netdev: netdevice
 *	@ifr: ignored
 *	@cmd: ignored
 *
 *	Currently not supported.
 *	Returns EOPNOTSUPP
 */
static int
visornic_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	return -EOPNOTSUPP;
}

/**
 *	visornic_change_mtu - changes mtu of device.
 *	@netdev: netdevice
 *	@new_mtu: value of new mtu
 *
 *	MTU cannot be changed by system, must be changed via
 *	CONTROLVM message. All vnics and pnics in a switch have
 *	to have the same MTU for everything to work.
 *	Currently not supported.
 *	Returns EINVAL
 */
static int
visornic_change_mtu(struct net_device *netdev, int new_mtu)
{
	return -EINVAL;
}

/**
 *	visornic_set_multi - changes mtu of device.
 *	@netdev: netdevice
 *
 *	Only flag we support currently is IFF_PROMISC
 *	Returns void
 */
static void
visornic_set_multi(struct net_device *netdev)
{
	struct uiscmdrsp *cmdrsp;
	struct visornic_devdata *devdata = netdev_priv(netdev);

	/* any filtering changes */
	if (devdata->old_flags != netdev->flags) {
		if ((netdev->flags & IFF_PROMISC) !=
		    (devdata->old_flags & IFF_PROMISC)) {
			cmdrsp = kmalloc(SIZEOF_CMDRSP, GFP_ATOMIC);
			if (!cmdrsp)
				return;
			cmdrsp->cmdtype = CMD_NET_TYPE;
			cmdrsp->net.type = NET_RCV_PROMISC;
			cmdrsp->net.enbdis.context = netdev;
			cmdrsp->net.enbdis.enable =
				(netdev->flags & IFF_PROMISC);
			visorchannel_signalinsert(devdata->dev->visorchannel,
						  IOCHAN_TO_IOPART,
						  cmdrsp);
			kfree(cmdrsp);
		}
		devdata->old_flags = netdev->flags;
	}
}

/**
 *	visornic_xmit_timeout - request to timeout the xmit
 *	@netdev
 *
 *	Queue the work and return. Make sure we have not already
 *	been informed the IO Partition is gone, if it is gone
 *	we will already timeout the xmits.
 */
static void
visornic_xmit_timeout(struct net_device *netdev)
{
	struct visornic_devdata *devdata = netdev_priv(netdev);
	unsigned long flags;

	spin_lock_irqsave(&devdata->priv_lock, flags);
	/* Ensure that a ServerDown message hasn't been received */
	if (!devdata->enabled ||
	    (devdata->server_down && !devdata->server_change_state)) {
		spin_unlock_irqrestore(&devdata->priv_lock, flags);
		return;
	}
	spin_unlock_irqrestore(&devdata->priv_lock, flags);

	queue_work(visornic_timeout_reset_workqueue, &devdata->timeout_reset);
}

/**
 *	repost_return	- repost rcv bufs that have come back
 *	@cmdrsp: io channel command struct to post
 *	@devdata: visornic devdata for the device
 *	@skb: skb
 *	@netdev: netdevice
 *
 *	Repost rcv buffers that have been returned to us when
 *	we are finished with them.
 *	Returns 0 for success, -1 for error.
 */
static inline int
repost_return(struct uiscmdrsp *cmdrsp, struct visornic_devdata *devdata,
	      struct sk_buff *skb, struct net_device *netdev)
{
	struct net_pkt_rcv copy;
	int i = 0, cc, numreposted;
	int found_skb = 0;
	int status = 0;

	copy = cmdrsp->net.rcv;
	switch (copy.numrcvbufs) {
	case 0:
		devdata->n_rcv0++;
		break;
	case 1:
		devdata->n_rcv1++;
		break;
	case 2:
		devdata->n_rcv2++;
		break;
	default:
		devdata->n_rcvx++;
		break;
	}
	for (cc = 0, numreposted = 0; cc < copy.numrcvbufs; cc++) {
		for (i = 0; i < devdata->num_rcv_bufs; i++) {
			if (devdata->rcvbuf[i] != copy.rcvbuf[cc])
				continue;

			if ((skb) && devdata->rcvbuf[i] == skb) {
				devdata->found_repost_rcvbuf_cnt++;
				found_skb = 1;
				devdata->repost_found_skb_cnt++;
			}
			devdata->rcvbuf[i] = alloc_rcv_buf(netdev);
			if (!devdata->rcvbuf[i]) {
				devdata->num_rcv_bufs_could_not_alloc++;
				devdata->alloc_failed_in_repost_rtn_cnt++;
				status = -ENOMEM;
				break;
			}
			post_skb(cmdrsp, devdata, devdata->rcvbuf[i]);
			numreposted++;
			break;
		}
	}
	if (numreposted != copy.numrcvbufs) {
		devdata->n_repost_deficit++;
		status = -EINVAL;
	}
	if (skb) {
		if (found_skb) {
			kfree_skb(skb);
		} else {
			status = -EINVAL;
			devdata->bad_rcv_buf++;
		}
	}
	atomic_dec(&devdata->usage);
	return status;
}

/**
 *	visornic_rx - Handle receive packets coming back from IO Part
 *	@cmdrsp: Receive packet returned from IO Part
 *
 *	Got a receive packet back from the IO Part, handle it and send
 *	it up the stack.
 *	Returns void
 */
static void
visornic_rx(struct uiscmdrsp *cmdrsp)
{
	struct visornic_devdata *devdata;
	struct sk_buff *skb, *prev, *curr;
	struct net_device *netdev;
	int cc, currsize, off, status;
	struct ethhdr *eth;
	unsigned long flags;
#ifdef DEBUG
	struct phys_info testfrags[MAX_PHYS_INFO];
#endif

	/* post new rcv buf to the other end using the cmdrsp we have at hand
	 * post it without holding lock - but we'll use the signal lock to
	 * synchronize the queue insert the cmdrsp that contains the net.rcv
	 * is the one we are using to repost, so copy the info we need from it.
	 */
	skb = cmdrsp->net.buf;
	netdev = skb->dev;

	if (!netdev) {
		/* We must have previously downed this network device and
		 * this skb and device is no longer valid. This also means
		 * the skb reference was removed from devdata->rcvbuf so no
		 * need to search for it.
		 * All we can do is free the skb and return.
		 * Note: We crash if we try to log this here.
		 */
		kfree_skb(skb);
		return;
	}

	devdata = netdev_priv(netdev);

	spin_lock_irqsave(&devdata->priv_lock, flags);
	atomic_dec(&devdata->num_rcvbuf_in_iovm);

	/* update rcv stats - call it with priv_lock held */
	devdata->net_stats.rx_packets++;
	devdata->net_stats.rx_bytes = skb->len;

	atomic_inc(&devdata->usage);	/* don't want a close to happen before
					 *  we're done here
					 */

	/* set length to how much was ACTUALLY received -
	 * NOTE: rcv_done_len includes actual length of data rcvd
	 * including ethhdr
	 */
	skb->len = cmdrsp->net.rcv.rcv_done_len;

	/* test enabled while holding lock */
	if (!(devdata->enabled && devdata->enab_dis_acked)) {
		/* don't process it unless we're in enable mode and until
		 * we've gotten an ACK saying the other end got our RCV enable
		 */
		spin_unlock_irqrestore(&devdata->priv_lock, flags);
		repost_return(cmdrsp, devdata, skb, netdev);
		return;
	}

	spin_unlock_irqrestore(&devdata->priv_lock, flags);

	/* when skb was allocated, skb->dev, skb->data, skb->len and
	 * skb->data_len were setup. AND, data has already put into the
	 * skb (both first frag and in frags pages)
	 * NOTE: firstfragslen is the amount of data in skb->data and that
	 * which is not in nr_frags or frag_list. This is now simply
	 * RCVPOST_BUF_SIZE. bump tail to show how much data is in
	 * firstfrag & set data_len to show rest see if we have to chain
	 * frag_list.
	 */
	if (skb->len > RCVPOST_BUF_SIZE) {	/* do PRECAUTIONARY check */
		if (cmdrsp->net.rcv.numrcvbufs < 2) {
			if (repost_return(cmdrsp, devdata, skb, netdev) < 0)
				dev_err(&devdata->netdev->dev,
					"repost_return failed");
			return;
		}
		/* length rcvd is greater than firstfrag in this skb rcv buf  */
		skb->tail += RCVPOST_BUF_SIZE;	/* amount in skb->data */
		skb->data_len = skb->len - RCVPOST_BUF_SIZE;	/* amount that
								   will be in
								   frag_list */
	} else {
		/* data fits in this skb - no chaining - do
		 * PRECAUTIONARY check
		 */
		if (cmdrsp->net.rcv.numrcvbufs != 1) {	/* should be 1 */
			if (repost_return(cmdrsp, devdata, skb, netdev) < 0)
				dev_err(&devdata->netdev->dev,
					"repost_return failed");
			return;
		}
		skb->tail += skb->len;
		skb->data_len = 0;	/* nothing rcvd in frag_list */
	}
	off = skb_tail_pointer(skb) - skb->data;

	/* amount we bumped tail by in the head skb
	 * it is used to calculate the size of each chained skb below
	 * it is also used to index into bufline to continue the copy
	 * (for chansocktwopc)
	 * if necessary chain the rcv skbs together.
	 * NOTE: index 0 has the same as cmdrsp->net.rcv.skb; we need to
	 * chain the rest to that one.
	 * - do PRECAUTIONARY check
	 */
	if (cmdrsp->net.rcv.rcvbuf[0] != skb) {
		if (repost_return(cmdrsp, devdata, skb, netdev) < 0)
			dev_err(&devdata->netdev->dev, "repost_return failed");
		return;
	}

	if (cmdrsp->net.rcv.numrcvbufs > 1) {
		/* chain the various rcv buffers into the skb's frag_list. */
		/* Note: off was initialized above  */
		for (cc = 1, prev = NULL;
		     cc < cmdrsp->net.rcv.numrcvbufs; cc++) {
			curr = (struct sk_buff *)cmdrsp->net.rcv.rcvbuf[cc];
			curr->next = NULL;
			if (!prev)	/* start of list- set head */
				skb_shinfo(skb)->frag_list = curr;
			else
				prev->next = curr;
			prev = curr;

			/* should we set skb->len and skb->data_len for each
			 * buffer being chained??? can't hurt!
			 */
			currsize = min(skb->len - off,
				       (unsigned int)RCVPOST_BUF_SIZE);
			curr->len = currsize;
			curr->tail += currsize;
			curr->data_len = 0;
			off += currsize;
		}
#ifdef DEBUG
		/* assert skb->len == off */
		if (skb->len != off) {
			dev_err(&devdata->netdev->dev,
				"%s something wrong; skb->len:%d != off:%d\n",
				netdev->name, skb->len, off);
		}
		/* test code */
		cc = util_copy_fragsinfo_from_skb("rcvchaintest", skb,
						  RCVPOST_BUF_SIZE,
						  MAX_PHYS_INFO, testfrags);
		if (cc != cmdrsp->net.rcv.numrcvbufs) {
			dev_err(&devdata->netdev->dev,
				"**** %s Something wrong; rcvd chain length %d different from one we calculated %d\n",
				netdev->name, cmdrsp->net.rcv.numrcvbufs, cc);
		}
		for (i = 0; i < cc; i++) {
			dev_inf(&devdata->netdev->dev,
				"test:RCVPOST_BUF_SIZE:%d[%d] pfn:%llu off:0x%x len:%d\n",
				RCVPOST_BUF_SIZE, i, testfrags[i].pi_pfn,
				testfrags[i].pi_off, testfrags[i].pi_len);
		}
#endif
	}

	/* set up packet's protocl type using ethernet header - this
	 * sets up skb->pkt_type & it also PULLS out the eth header
	 */
	skb->protocol = eth_type_trans(skb, netdev);

	eth = eth_hdr(skb);

	skb->csum = 0;
	skb->ip_summed = CHECKSUM_NONE;

	do {
		if (netdev->flags & IFF_PROMISC)
			break;	/* accept all packets */
		if (skb->pkt_type == PACKET_BROADCAST) {
			if (netdev->flags & IFF_BROADCAST)
				break;	/* accept all broadcast packets */
		} else if (skb->pkt_type == PACKET_MULTICAST) {
			if ((netdev->flags & IFF_MULTICAST) &&
			    (netdev_mc_count(netdev))) {
				struct netdev_hw_addr *ha;
				int found_mc = 0;

				/* only accept multicast packets that we can
				 * find in our multicast address list
				 */
				netdev_for_each_mc_addr(ha, netdev) {
					if (ether_addr_equal(eth->h_dest,
							     ha->addr)) {
						found_mc = 1;
						break;
					}
				}
				if (found_mc)
					break;	/* accept packet, dest
						   matches a multicast
						   address */
			}
		} else if (skb->pkt_type == PACKET_HOST) {
			break;	/* accept packet, h_dest must match vnic
				   mac address */
		} else if (skb->pkt_type == PACKET_OTHERHOST) {
			/* something is not right */
			dev_err(&devdata->netdev->dev,
				"**** FAILED to deliver rcv packet to OS; name:%s Dest:%pM VNIC:%pM\n",
				netdev->name, eth->h_dest, netdev->dev_addr);
		}
		/* drop packet - don't forward it up to OS */
		devdata->n_rcv_packets_not_accepted++;
		repost_return(cmdrsp, devdata, skb, netdev);
		return;
	} while (0);

	status = netif_rx(skb);
	/* netif_rx returns various values, but "in practice most drivers
	 * ignore the return value
	 */

	skb = NULL;
	/*
	 * whether the packet got dropped or handled, the skb is freed by
	 * kernel code, so we shouldn't free it. but we should repost a
	 * new rcv buffer.
	 */
	repost_return(cmdrsp, devdata, skb, netdev);
}

/**
 *	devdata_initialize	- Initialize devdata structure
 *	@devdata: visornic_devdata structure to initialize
 *	#dev: visorbus_deviced it belongs to
 *
 *	Setup initial values for the visornic based on channel and default
 *	values.
 *	Returns a pointer to the devdata if successful, else NULL
 */
static struct visornic_devdata *
devdata_initialize(struct visornic_devdata *devdata, struct visor_device *dev)
{
	int devnum = -1;

	if (!devdata)
		return NULL;
	memset(devdata, '\0', sizeof(struct visornic_devdata));
	spin_lock(&dev_num_pool_lock);
	devnum = find_first_zero_bit(dev_num_pool, MAXDEVICES);
	set_bit(devnum, dev_num_pool);
	spin_unlock(&dev_num_pool_lock);
	if (devnum == MAXDEVICES)
		devnum = -1;
	if (devnum < 0) {
		kfree(devdata);
		return NULL;
	}
	devdata->devnum = devnum;
	devdata->dev = dev;
	strncpy(devdata->name, dev_name(&dev->device), sizeof(devdata->name));
	kref_init(&devdata->kref);
	spin_lock(&lock_all_devices);
	list_add_tail(&devdata->list_all, &list_all_devices);
	spin_unlock(&lock_all_devices);
	return devdata;
}

/**
 *	devdata_release	- Frees up a devdata
 *	@mykref: kref to the devdata
 *
 *	Frees up a devdata.
 *	Returns void
 */
static void devdata_release(struct kref *mykref)
{
	struct visornic_devdata *devdata =
		container_of(mykref, struct visornic_devdata, kref);

	spin_lock(&dev_num_pool_lock);
	clear_bit(devdata->devnum, dev_num_pool);
	spin_unlock(&dev_num_pool_lock);
	spin_lock(&lock_all_devices);
	list_del(&devdata->list_all);
	spin_unlock(&lock_all_devices);
	kfree(devdata);
}

static const struct net_device_ops visornic_dev_ops = {
	.ndo_open = visornic_open,
	.ndo_stop = visornic_close,
	.ndo_start_xmit = visornic_xmit,
	.ndo_get_stats = visornic_get_stats,
	.ndo_do_ioctl = visornic_ioctl,
	.ndo_change_mtu = visornic_change_mtu,
	.ndo_tx_timeout = visornic_xmit_timeout,
	.ndo_set_rx_mode = visornic_set_multi,
};

/**
 *	send_rcv_posts_if_needed
 *	@devdata: visornic device
 *
 *	Send receive buffers to the IO Partition.
 *	Returns void
 */
static void
send_rcv_posts_if_needed(struct visornic_devdata *devdata)
{
	int i;
	struct net_device *netdev;
	struct uiscmdrsp *cmdrsp = devdata->cmdrsp_rcv;
	int cur_num_rcv_bufs_to_alloc, rcv_bufs_allocated;

	/* don't do this until vnic is marked ready */
	if (!(devdata->enabled && devdata->enab_dis_acked))
		return;

	netdev = devdata->netdev;
	rcv_bufs_allocated = 0;
	/* this code is trying to prevent getting stuck here forever,
	 * but still retry it if you cant allocate them all this time.
	 */
	cur_num_rcv_bufs_to_alloc = devdata->num_rcv_bufs_could_not_alloc;
	while (cur_num_rcv_bufs_to_alloc > 0) {
		cur_num_rcv_bufs_to_alloc--;
		for (i = 0; i < devdata->num_rcv_bufs; i++) {
			if (devdata->rcvbuf[i])
				continue;
			devdata->rcvbuf[i] = alloc_rcv_buf(netdev);
			if (!devdata->rcvbuf[i]) {
				devdata->alloc_failed_in_if_needed_cnt++;
				break;
			}
			rcv_bufs_allocated++;
			post_skb(cmdrsp, devdata, devdata->rcvbuf[i]);
			devdata->chstat.extra_rcvbufs_sent++;
		}
	}
	devdata->num_rcv_bufs_could_not_alloc -= rcv_bufs_allocated;
}

/**
 *	draing_queue	- drains the response queue
 *	@cmdrsp: io channel command response message
 *	@devdata: visornic device to drain
 *
 *	Drain the respones queue of any responses from the IO partition.
 *	Process the responses as we get them.
 *	Returns when response queue is empty or when the threadd stops.
 */
static void
drain_queue(struct uiscmdrsp *cmdrsp, struct visornic_devdata *devdata)
{
	unsigned long flags;
	struct net_device *netdev;

	/* drain queue */
	while (1) {
		/* TODO: CLIENT ACQUIRE -- Don't really need this at the
		 * moment */
		if (!visorchannel_signalremove(devdata->dev->visorchannel,
					       IOCHAN_FROM_IOPART,
					       cmdrsp))
			break; /* queue empty */

		switch (cmdrsp->net.type) {
		case NET_RCV:
			devdata->chstat.got_rcv++;
			/* process incoming packet */
			visornic_rx(cmdrsp);
			break;
		case NET_XMIT_DONE:
			spin_lock_irqsave(&devdata->priv_lock, flags);
			devdata->chstat.got_xmit_done++;
			if (cmdrsp->net.xmtdone.xmt_done_result)
				devdata->chstat.xmit_fail++;
			/* only call queue wake if we stopped it */
			netdev = ((struct sk_buff *)cmdrsp->net.buf)->dev;
			/* ASSERT netdev == vnicinfo->netdev; */
			if ((netdev == devdata->netdev) &&
			    netif_queue_stopped(netdev)) {
				/* check to see if we have crossed
				 * the lower watermark for
				 * netif_wake_queue()
				 */
				if (((devdata->chstat.sent_xmit >=
				    devdata->chstat.got_xmit_done) &&
				    (devdata->chstat.sent_xmit -
				    devdata->chstat.got_xmit_done <=
				    devdata->lower_threshold_net_xmits)) ||
				    ((devdata->chstat.sent_xmit <
				    devdata->chstat.got_xmit_done) &&
				    (ULONG_MAX - devdata->chstat.got_xmit_done
				    + devdata->chstat.sent_xmit <=
				    devdata->lower_threshold_net_xmits))) {
					/* enough NET_XMITs completed
					 * so can restart netif queue
					 */
					netif_wake_queue(netdev);
					devdata->flow_control_lower_hits++;
				}
			}
			skb_unlink(cmdrsp->net.buf, &devdata->xmitbufhead);
			spin_unlock_irqrestore(&devdata->priv_lock, flags);
			kfree_skb(cmdrsp->net.buf);
			break;
		case NET_RCV_ENBDIS_ACK:
			devdata->chstat.got_enbdisack++;
			netdev = (struct net_device *)
			cmdrsp->net.enbdis.context;
			spin_lock_irqsave(&devdata->priv_lock, flags);
			devdata->enab_dis_acked = 1;
			spin_unlock_irqrestore(&devdata->priv_lock, flags);

			if (devdata->server_down &&
			    devdata->server_change_state) {
				/* Inform Linux that the link is up */
				devdata->server_down = false;
				devdata->server_change_state = false;
				netif_wake_queue(netdev);
				netif_carrier_on(netdev);
			}
			break;
		case NET_CONNECT_STATUS:
			netdev = devdata->netdev;
			if (cmdrsp->net.enbdis.enable == 1) {
				spin_lock_irqsave(&devdata->priv_lock, flags);
				devdata->enabled = cmdrsp->net.enbdis.enable;
				spin_unlock_irqrestore(&devdata->priv_lock,
						       flags);
				netif_wake_queue(netdev);
				netif_carrier_on(netdev);
			} else {
				netif_stop_queue(netdev);
				netif_carrier_off(netdev);
				spin_lock_irqsave(&devdata->priv_lock, flags);
				devdata->enabled = cmdrsp->net.enbdis.enable;
				spin_unlock_irqrestore(&devdata->priv_lock,
						       flags);
			}
			break;
		default:
			break;
		}
		/* cmdrsp is now available for reuse  */

		if (kthread_should_stop())
			break;
	}
}

/**
 *	process_incoming_rsps	- Checks the status of the response queue.
 *	@v: void pointer to the visronic devdata
 *
 *	Main function of the vnic_incoming thread. Peridocially check the
 *	response queue and drain it if needed.
 *	Returns when thread has stopped.
 */
static int
process_incoming_rsps(void *v)
{
	struct visornic_devdata *devdata = v;
	struct uiscmdrsp *cmdrsp = NULL;
	const int SZ = SIZEOF_CMDRSP;

	cmdrsp = kmalloc(SZ, GFP_ATOMIC);
	if (!cmdrsp)
		complete_and_exit(&devdata->threadinfo.has_stopped, 0);

	while (1) {
		wait_event_interruptible_timeout(
			devdata->rsp_queue, (atomic_read(
					     &devdata->interrupt_rcvd) == 1),
				msecs_to_jiffies(devdata->thread_wait_ms));

		/* periodically check to see if there are any rcf bufs which
		 * need to get sent to the IOSP. This can only happen if
		 * we run out of memory when trying to allocate skbs.
		 */
		atomic_set(&devdata->interrupt_rcvd, 0);
		send_rcv_posts_if_needed(devdata);
		drain_queue(cmdrsp, devdata);
		if (kthread_should_stop())
			break;
	}

	kfree(cmdrsp);
	complete_and_exit(&devdata->threadinfo.has_stopped, 0);
}

/**
 *	visornic_probe	- probe function for visornic devices
 *	@dev: The visor device discovered
 *
 *	Called when visorbus discovers a visornic device on its
 *	bus. It creates a new visornic ethernet adapter.
 *	Returns 0 or negative for error.
 */
static int visornic_probe(struct visor_device *dev)
{
	struct visornic_devdata *devdata = NULL;
	struct net_device *netdev = NULL;
	int err;
	int channel_offset = 0;
	u64 features;

	netdev = alloc_etherdev(sizeof(struct visornic_devdata));
	if (!netdev)
		return -ENOMEM;

	netdev->netdev_ops = &visornic_dev_ops;
	netdev->watchdog_timeo = (5 * HZ);
	netdev->dev.parent = &dev->device;

	/* Get MAC adddress from channel and read it into the device. */
	netdev->addr_len = ETH_ALEN;
	channel_offset = offsetof(struct spar_io_channel_protocol,
				  vnic.macaddr);
	err = visorbus_read_channel(dev, channel_offset, netdev->dev_addr,
				    ETH_ALEN);
	if (err < 0)
		goto cleanup_netdev;

	devdata = devdata_initialize(netdev_priv(netdev), dev);
	if (!devdata) {
		err = -ENOMEM;
		goto cleanup_netdev;
	}

	devdata->netdev = netdev;
	init_waitqueue_head(&devdata->rsp_queue);
	spin_lock_init(&devdata->priv_lock);
	devdata->enabled = 0; /* not yet */
	atomic_set(&devdata->usage, 1);

	/* Setup rcv bufs */
	channel_offset = offsetof(struct spar_io_channel_protocol,
				  vnic.num_rcv_bufs);
	err = visorbus_read_channel(dev, channel_offset,
				    &devdata->num_rcv_bufs, 4);
	if (err)
		goto cleanup_netdev;

	devdata->rcvbuf = kmalloc(sizeof(struct sk_buff *) *
				  devdata->num_rcv_bufs, GFP_KERNEL);
	if (!devdata->rcvbuf) {
		err = -ENOMEM;
		goto cleanup_rcvbuf;
	}

	/* set the net_xmit outstanding threshold */
	/* always leave two slots open but you should have 3 at a minimum */
	devdata->max_outstanding_net_xmits =
		max(3, ((devdata->num_rcv_bufs / 3) - 2));
	devdata->upper_threshold_net_xmits =
		max(2, devdata->max_outstanding_net_xmits - 1);
	devdata->lower_threshold_net_xmits =
		max(1, devdata->max_outstanding_net_xmits / 2);

	skb_queue_head_init(&devdata->xmitbufhead);

	/* create a cmdrsp we can use to post and unpost rcv buffers */
	devdata->cmdrsp_rcv = kmalloc(SIZEOF_CMDRSP, GFP_ATOMIC);
	if (!devdata->cmdrsp_rcv) {
		err = -ENOMEM;
		goto cleanup_cmdrsp_rcv;
	}
	devdata->xmit_cmdrsp = kmalloc(SIZEOF_CMDRSP, GFP_ATOMIC);
	if (!devdata->xmit_cmdrsp) {
		err = -ENOMEM;
		goto cleanup_xmit_cmdrsp;
	}
	INIT_WORK(&devdata->serverdown_completion,
		  visornic_serverdown_complete);
	INIT_WORK(&devdata->timeout_reset, visornic_timeout_reset);
	devdata->server_down = false;
	devdata->server_change_state = false;

	/*set the default mtu */
	channel_offset = offsetof(struct spar_io_channel_protocol,
				  vnic.mtu);
	err = visorbus_read_channel(dev, channel_offset, &netdev->mtu, 4);
	if (err)
		goto cleanup_xmit_cmdrsp;

	/* TODO: Setup Interrupt information */
	/* Let's start our threads to get responses */
	channel_offset = offsetof(struct spar_io_channel_protocol,
				  channel_header.features);
	err = visorbus_read_channel(dev, channel_offset, &features, 8);
	if (err)
		goto cleanup_xmit_cmdrsp;

	features |= ULTRA_IO_CHANNEL_IS_POLLING;
	err = visorbus_write_channel(dev, channel_offset, &features, 8);
	if (err)
		goto cleanup_xmit_cmdrsp;

	devdata->thread_wait_ms = 2;
	visor_thread_start(&devdata->threadinfo, process_incoming_rsps,
			   devdata, "vnic_incoming");

	err = register_netdev(netdev);
	if (err)
		goto cleanup_thread_stop;

	/* create debgug/sysfs directories */
	devdata->eth_debugfs_dir = debugfs_create_dir(netdev->name,
						      visornic_debugfs_dir);
	if (!devdata->eth_debugfs_dir) {
		err = -ENOMEM;
		goto cleanup_thread_stop;
	}

	return 0;

cleanup_thread_stop:
	visor_thread_stop(&devdata->threadinfo);

cleanup_xmit_cmdrsp:
	kfree(devdata->xmit_cmdrsp);

cleanup_cmdrsp_rcv:
	kfree(devdata->cmdrsp_rcv);

cleanup_rcvbuf:
	kfree(devdata->rcvbuf);

cleanup_netdev:
	free_netdev(netdev);
	return err;
}

/**
 *	host_side_disappeared	- IO part is gone.
 *	@devdata: device object
 *
 *	IO partition servicing this device is gone, do cleanup
 *	Returns void.
 */
static void host_side_disappeared(struct visornic_devdata *devdata)
{
	unsigned long flags;

	spin_lock_irqsave(&devdata->priv_lock, flags);
	sprintf(devdata->name, "<dev#%d-history>", devdata->devnum);
	devdata->dev = NULL;   /* indicate device destroyed */
	spin_unlock_irqrestore(&devdata->priv_lock, flags);
}

/**
 *	visornic_remove		- Called when visornic dev goes away
 *	@dev: visornic device that is being removed
 *
 *	Called when DEVICE_DESTROY gets called to remove device.
 *	Returns void
 */
static void visornic_remove(struct visor_device *dev)
{
	struct visornic_devdata *devdata = dev_get_drvdata(&dev->device);

	if (!devdata)
		return;
	dev_set_drvdata(&dev->device, NULL);
	host_side_disappeared(devdata);
	kref_put(&devdata->kref, devdata_release);
}

/**
 *	visornic_pause		- Called when IO Part disappears
 *	@dev: visornic device that is being serviced
 *	@complete_func: call when finished.
 *
 *	Called when the IO Partition has gone down. Need to free
 *	up resources and wait for IO partition to come back. Mark
 *	link as down and don't attempt any DMA. When we have freed
 *	memory call the complete_func so that Command knows we are
 *	done. If we don't call complete_func, IO part will never
 *	come back.
 *	Returns 0 for success.
 */
static int visornic_pause(struct visor_device *dev,
			  visorbus_state_complete_func complete_func)
{
	struct visornic_devdata *devdata = dev_get_drvdata(&dev->device);

	visornic_serverdown(devdata);
	complete_func(dev, 0);
	return 0;
}

/**
 *	visornic_resume		- Called when IO part has recovered
 *	@dev: visornic device that is being serviced
 *	@compelte_func: call when finished
 *
 *	Called when the IO partition has recovered. Reestablish
 *	connection to the IO part and set the link up. Okay to do
 *	DMA again.
 *	Returns 0 for success.
 */
static int visornic_resume(struct visor_device *dev,
			   visorbus_state_complete_func complete_func)
{
	struct visornic_devdata *devdata;
	struct net_device *netdev;
	unsigned long flags;

	devdata = dev_get_drvdata(&dev->device);
	if (!devdata)
		return -EINVAL;

	netdev = devdata->netdev;

	if (devdata->server_down && !devdata->server_change_state) {
		devdata->server_change_state = true;
		/* Must transition channel to ATTACHED state BEFORE
		 * we can start using the device again.
		 * TODO: State transitions
		 */
		visor_thread_start(&devdata->threadinfo, process_incoming_rsps,
				   devdata, "vnic_incoming");
		init_rcv_bufs(netdev, devdata);
		spin_lock_irqsave(&devdata->priv_lock, flags);
		devdata->enabled = 1;

		/* Now we're ready, let's send an ENB to uisnic but until
		 * we get an ACK back from uisnic, we'll drop the packets
		 */
		devdata->enab_dis_acked = 0;
		spin_unlock_irqrestore(&devdata->priv_lock, flags);

		/* send enable and wait for ack - don't hold lock when
		 * sending enable because if the queue if sull, insert
		 * might sleep.
		 */
		send_enbdis(netdev, 1, devdata);
	} else if (devdata->server_change_state) {
		return -EIO;
	}

	complete_func(dev, 0);
	return 0;
}

/**
 *	visornic_init	- Init function
 *
 *	Init function for the visornic driver. Do initial driver setup
 *	and wait for devices.
 *	Returns 0 for success, negative for error.
 */
static int visornic_init(void)
{
	struct dentry *ret;
	int err = -ENOMEM;

	/* create workqueue for serverdown completion */
	visornic_serverdown_workqueue =
		create_singlethread_workqueue("visornic_serverdown");
	if (!visornic_serverdown_workqueue)
		return -ENOMEM;

	/* create workqueue for tx timeout reset */
	visornic_timeout_reset_workqueue =
		create_singlethread_workqueue("visornic_timeout_reset");
	if (!visornic_timeout_reset_workqueue)
		return -ENOMEM;

	visornic_debugfs_dir = debugfs_create_dir("visornic", NULL);
	if (!visornic_debugfs_dir)
		return err;

	ret = debugfs_create_file("info", S_IRUSR, visornic_debugfs_dir, NULL,
				  &debugfs_info_fops);
	if (!ret)
		goto cleanup_debugfs;
	ret = debugfs_create_file("enable_ints", S_IWUSR, visornic_debugfs_dir,
				  NULL, &debugfs_enable_ints_fops);
	if (!ret)
		goto cleanup_debugfs;

	/* create workqueue for serverdown completion */
	visornic_serverdown_workqueue =
		create_singlethread_workqueue("visornic_serverdown");
	if (!visornic_serverdown_workqueue)
		goto cleanup_debugfs;

	/* create workqueue for tx timeout reset */
	visornic_timeout_reset_workqueue =
		create_singlethread_workqueue("visornic_timeout_reset");
	if (!visornic_timeout_reset_workqueue)
		goto cleanup_workqueue;

	spin_lock_init(&dev_num_pool_lock);
	dev_num_pool = kzalloc(BITS_TO_LONGS(MAXDEVICES), GFP_KERNEL);
	if (!dev_num_pool)
		goto cleanup_workqueue;

	visorbus_register_visor_driver(&visornic_driver);
	return 0;

cleanup_workqueue:
	flush_workqueue(visornic_serverdown_workqueue);
	destroy_workqueue(visornic_serverdown_workqueue);
	if (visornic_timeout_reset_workqueue) {
		flush_workqueue(visornic_timeout_reset_workqueue);
		destroy_workqueue(visornic_timeout_reset_workqueue);
	}
cleanup_debugfs:
	debugfs_remove_recursive(visornic_debugfs_dir);

	return err;
}

/**
 *	visornic_cleanup	- driver exit routine
 *
 *	Unregister driver from the bus and free up memory.
 */
static void visornic_cleanup(void)
{
	if (visornic_serverdown_workqueue) {
		flush_workqueue(visornic_serverdown_workqueue);
		destroy_workqueue(visornic_serverdown_workqueue);
	}
	if (visornic_timeout_reset_workqueue) {
		flush_workqueue(visornic_timeout_reset_workqueue);
		destroy_workqueue(visornic_timeout_reset_workqueue);
	}
	debugfs_remove_recursive(visornic_debugfs_dir);

	visorbus_unregister_visor_driver(&visornic_driver);
	kfree(dev_num_pool);
	dev_num_pool = NULL;
}

module_init(visornic_init);
module_exit(visornic_cleanup);

MODULE_AUTHOR("Unisys");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("sPAR nic driver for sparlinux: ver 1.0.0.0");
MODULE_VERSION("1.0.0.0");
