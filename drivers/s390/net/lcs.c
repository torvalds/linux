// SPDX-License-Identifier: GPL-2.0+
/*
 *  Linux for S/390 Lan Channel Station Network Driver
 *
 *  Copyright IBM Corp. 1999, 2009
 *  Author(s): Original Code written by
 *			DJ Barrow <djbarrow@de.ibm.com,barrow_dj@yahoo.com>
 *	       Rewritten by
 *			Frank Pavlic <fpavlic@de.ibm.com> and
 *			Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#define KMSG_COMPONENT		"lcs"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/if.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/fddidevice.h>
#include <linux/inetdevice.h>
#include <linux/in.h>
#include <linux/igmp.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <net/arp.h>
#include <net/ip.h>

#include <asm/debug.h>
#include <asm/idals.h>
#include <asm/timex.h>
#include <linux/device.h>
#include <asm/ccwgroup.h>

#include "lcs.h"


#if !defined(CONFIG_ETHERNET) && !defined(CONFIG_FDDI)
#error Cannot compile lcs.c without some net devices switched on.
#endif

/**
 * initialization string for output
 */

static char version[] __initdata = "LCS driver";

/**
  * the root device for lcs group devices
  */
static struct device *lcs_root_dev;

/**
 * Some prototypes.
 */
static void lcs_tasklet(unsigned long);
static void lcs_start_kernel_thread(struct work_struct *);
static void lcs_get_frames_cb(struct lcs_channel *, struct lcs_buffer *);
#ifdef CONFIG_IP_MULTICAST
static int lcs_send_delipm(struct lcs_card *, struct lcs_ipm_list *);
#endif /* CONFIG_IP_MULTICAST */
static int lcs_recovery(void *ptr);

/**
 * Debug Facility Stuff
 */
static char debug_buffer[255];
static debug_info_t *lcs_dbf_setup;
static debug_info_t *lcs_dbf_trace;

/**
 *  LCS Debug Facility functions
 */
static void
lcs_unregister_debug_facility(void)
{
	debug_unregister(lcs_dbf_setup);
	debug_unregister(lcs_dbf_trace);
}

static int
lcs_register_debug_facility(void)
{
	lcs_dbf_setup = debug_register("lcs_setup", 2, 1, 8);
	lcs_dbf_trace = debug_register("lcs_trace", 4, 1, 8);
	if (lcs_dbf_setup == NULL || lcs_dbf_trace == NULL) {
		pr_err("Not enough memory for debug facility.\n");
		lcs_unregister_debug_facility();
		return -ENOMEM;
	}
	debug_register_view(lcs_dbf_setup, &debug_hex_ascii_view);
	debug_set_level(lcs_dbf_setup, 2);
	debug_register_view(lcs_dbf_trace, &debug_hex_ascii_view);
	debug_set_level(lcs_dbf_trace, 2);
	return 0;
}

/**
 * Allocate io buffers.
 */
static int
lcs_alloc_channel(struct lcs_channel *channel)
{
	int cnt;

	LCS_DBF_TEXT(2, setup, "ichalloc");
	for (cnt = 0; cnt < LCS_NUM_BUFFS; cnt++) {
		/* alloc memory fo iobuffer */
		channel->iob[cnt].data =
			kzalloc(LCS_IOBUFFERSIZE, GFP_DMA | GFP_KERNEL);
		if (channel->iob[cnt].data == NULL)
			break;
		channel->iob[cnt].state = LCS_BUF_STATE_EMPTY;
	}
	if (cnt < LCS_NUM_BUFFS) {
		/* Not all io buffers could be allocated. */
		LCS_DBF_TEXT(2, setup, "echalloc");
		while (cnt-- > 0)
			kfree(channel->iob[cnt].data);
		return -ENOMEM;
	}
	return 0;
}

/**
 * Free io buffers.
 */
static void
lcs_free_channel(struct lcs_channel *channel)
{
	int cnt;

	LCS_DBF_TEXT(2, setup, "ichfree");
	for (cnt = 0; cnt < LCS_NUM_BUFFS; cnt++) {
		kfree(channel->iob[cnt].data);
		channel->iob[cnt].data = NULL;
	}
}

/*
 * Cleanup channel.
 */
static void
lcs_cleanup_channel(struct lcs_channel *channel)
{
	LCS_DBF_TEXT(3, setup, "cleanch");
	/* Kill write channel tasklets. */
	tasklet_kill(&channel->irq_tasklet);
	/* Free channel buffers. */
	lcs_free_channel(channel);
}

/**
 * LCS free memory for card and channels.
 */
static void
lcs_free_card(struct lcs_card *card)
{
	LCS_DBF_TEXT(2, setup, "remcard");
	LCS_DBF_HEX(2, setup, &card, sizeof(void*));
	kfree(card);
}

/**
 * LCS alloc memory for card and channels
 */
static struct lcs_card *
lcs_alloc_card(void)
{
	struct lcs_card *card;
	int rc;

	LCS_DBF_TEXT(2, setup, "alloclcs");

	card = kzalloc(sizeof(struct lcs_card), GFP_KERNEL | GFP_DMA);
	if (card == NULL)
		return NULL;
	card->lan_type = LCS_FRAME_TYPE_AUTO;
	card->pkt_seq = 0;
	card->lancmd_timeout = LCS_LANCMD_TIMEOUT_DEFAULT;
	/* Allocate io buffers for the read channel. */
	rc = lcs_alloc_channel(&card->read);
	if (rc){
		LCS_DBF_TEXT(2, setup, "iccwerr");
		lcs_free_card(card);
		return NULL;
	}
	/* Allocate io buffers for the write channel. */
	rc = lcs_alloc_channel(&card->write);
	if (rc) {
		LCS_DBF_TEXT(2, setup, "iccwerr");
		lcs_cleanup_channel(&card->read);
		lcs_free_card(card);
		return NULL;
	}

#ifdef CONFIG_IP_MULTICAST
	INIT_LIST_HEAD(&card->ipm_list);
#endif
	LCS_DBF_HEX(2, setup, &card, sizeof(void*));
	return card;
}

/*
 * Setup read channel.
 */
static void
lcs_setup_read_ccws(struct lcs_card *card)
{
	int cnt;

	LCS_DBF_TEXT(2, setup, "ireadccw");
	/* Setup read ccws. */
	memset(card->read.ccws, 0, sizeof (struct ccw1) * (LCS_NUM_BUFFS + 1));
	for (cnt = 0; cnt < LCS_NUM_BUFFS; cnt++) {
		card->read.ccws[cnt].cmd_code = LCS_CCW_READ;
		card->read.ccws[cnt].count = LCS_IOBUFFERSIZE;
		card->read.ccws[cnt].flags =
			CCW_FLAG_CC | CCW_FLAG_SLI | CCW_FLAG_PCI;
		/*
		 * Note: we have allocated the buffer with GFP_DMA, so
		 * we do not need to do set_normalized_cda.
		 */
		card->read.ccws[cnt].cda =
			(__u32) __pa(card->read.iob[cnt].data);
		((struct lcs_header *)
		 card->read.iob[cnt].data)->offset = LCS_ILLEGAL_OFFSET;
		card->read.iob[cnt].callback = lcs_get_frames_cb;
		card->read.iob[cnt].state = LCS_BUF_STATE_READY;
		card->read.iob[cnt].count = LCS_IOBUFFERSIZE;
	}
	card->read.ccws[0].flags &= ~CCW_FLAG_PCI;
	card->read.ccws[LCS_NUM_BUFFS - 1].flags &= ~CCW_FLAG_PCI;
	card->read.ccws[LCS_NUM_BUFFS - 1].flags |= CCW_FLAG_SUSPEND;
	/* Last ccw is a tic (transfer in channel). */
	card->read.ccws[LCS_NUM_BUFFS].cmd_code = LCS_CCW_TRANSFER;
	card->read.ccws[LCS_NUM_BUFFS].cda =
		(__u32) __pa(card->read.ccws);
	/* Setg initial state of the read channel. */
	card->read.state = LCS_CH_STATE_INIT;

	card->read.io_idx = 0;
	card->read.buf_idx = 0;
}

static void
lcs_setup_read(struct lcs_card *card)
{
	LCS_DBF_TEXT(3, setup, "initread");

	lcs_setup_read_ccws(card);
	/* Initialize read channel tasklet. */
	card->read.irq_tasklet.data = (unsigned long) &card->read;
	card->read.irq_tasklet.func = lcs_tasklet;
	/* Initialize waitqueue. */
	init_waitqueue_head(&card->read.wait_q);
}

/*
 * Setup write channel.
 */
static void
lcs_setup_write_ccws(struct lcs_card *card)
{
	int cnt;

	LCS_DBF_TEXT(3, setup, "iwritccw");
	/* Setup write ccws. */
	memset(card->write.ccws, 0, sizeof(struct ccw1) * (LCS_NUM_BUFFS + 1));
	for (cnt = 0; cnt < LCS_NUM_BUFFS; cnt++) {
		card->write.ccws[cnt].cmd_code = LCS_CCW_WRITE;
		card->write.ccws[cnt].count = 0;
		card->write.ccws[cnt].flags =
			CCW_FLAG_SUSPEND | CCW_FLAG_CC | CCW_FLAG_SLI;
		/*
		 * Note: we have allocated the buffer with GFP_DMA, so
		 * we do not need to do set_normalized_cda.
		 */
		card->write.ccws[cnt].cda =
			(__u32) __pa(card->write.iob[cnt].data);
	}
	/* Last ccw is a tic (transfer in channel). */
	card->write.ccws[LCS_NUM_BUFFS].cmd_code = LCS_CCW_TRANSFER;
	card->write.ccws[LCS_NUM_BUFFS].cda =
		(__u32) __pa(card->write.ccws);
	/* Set initial state of the write channel. */
	card->read.state = LCS_CH_STATE_INIT;

	card->write.io_idx = 0;
	card->write.buf_idx = 0;
}

static void
lcs_setup_write(struct lcs_card *card)
{
	LCS_DBF_TEXT(3, setup, "initwrit");

	lcs_setup_write_ccws(card);
	/* Initialize write channel tasklet. */
	card->write.irq_tasklet.data = (unsigned long) &card->write;
	card->write.irq_tasklet.func = lcs_tasklet;
	/* Initialize waitqueue. */
	init_waitqueue_head(&card->write.wait_q);
}

static void
lcs_set_allowed_threads(struct lcs_card *card, unsigned long threads)
{
	unsigned long flags;

	spin_lock_irqsave(&card->mask_lock, flags);
	card->thread_allowed_mask = threads;
	spin_unlock_irqrestore(&card->mask_lock, flags);
	wake_up(&card->wait_q);
}
static int lcs_threads_running(struct lcs_card *card, unsigned long threads)
{
        unsigned long flags;
        int rc = 0;

	spin_lock_irqsave(&card->mask_lock, flags);
        rc = (card->thread_running_mask & threads);
	spin_unlock_irqrestore(&card->mask_lock, flags);
        return rc;
}

static int
lcs_wait_for_threads(struct lcs_card *card, unsigned long threads)
{
        return wait_event_interruptible(card->wait_q,
                        lcs_threads_running(card, threads) == 0);
}

static int lcs_set_thread_start_bit(struct lcs_card *card, unsigned long thread)
{
        unsigned long flags;

	spin_lock_irqsave(&card->mask_lock, flags);
        if ( !(card->thread_allowed_mask & thread) ||
              (card->thread_start_mask & thread) ) {
                spin_unlock_irqrestore(&card->mask_lock, flags);
                return -EPERM;
        }
        card->thread_start_mask |= thread;
	spin_unlock_irqrestore(&card->mask_lock, flags);
        return 0;
}

static void
lcs_clear_thread_running_bit(struct lcs_card *card, unsigned long thread)
{
        unsigned long flags;

	spin_lock_irqsave(&card->mask_lock, flags);
        card->thread_running_mask &= ~thread;
	spin_unlock_irqrestore(&card->mask_lock, flags);
        wake_up(&card->wait_q);
}

static int __lcs_do_run_thread(struct lcs_card *card, unsigned long thread)
{
        unsigned long flags;
        int rc = 0;

	spin_lock_irqsave(&card->mask_lock, flags);
        if (card->thread_start_mask & thread){
                if ((card->thread_allowed_mask & thread) &&
                    !(card->thread_running_mask & thread)){
                        rc = 1;
                        card->thread_start_mask &= ~thread;
                        card->thread_running_mask |= thread;
                } else
                        rc = -EPERM;
        }
	spin_unlock_irqrestore(&card->mask_lock, flags);
        return rc;
}

static int
lcs_do_run_thread(struct lcs_card *card, unsigned long thread)
{
        int rc = 0;
        wait_event(card->wait_q,
                   (rc = __lcs_do_run_thread(card, thread)) >= 0);
        return rc;
}

static int
lcs_do_start_thread(struct lcs_card *card, unsigned long thread)
{
        unsigned long flags;
        int rc = 0;

	spin_lock_irqsave(&card->mask_lock, flags);
        LCS_DBF_TEXT_(4, trace, "  %02x%02x%02x",
                        (u8) card->thread_start_mask,
                        (u8) card->thread_allowed_mask,
                        (u8) card->thread_running_mask);
        rc = (card->thread_start_mask & thread);
	spin_unlock_irqrestore(&card->mask_lock, flags);
        return rc;
}

/**
 * Initialize channels,card and state machines.
 */
static void
lcs_setup_card(struct lcs_card *card)
{
	LCS_DBF_TEXT(2, setup, "initcard");
	LCS_DBF_HEX(2, setup, &card, sizeof(void*));

	lcs_setup_read(card);
	lcs_setup_write(card);
	/* Set cards initial state. */
	card->state = DEV_STATE_DOWN;
	card->tx_buffer = NULL;
	card->tx_emitted = 0;

	init_waitqueue_head(&card->wait_q);
	spin_lock_init(&card->lock);
	spin_lock_init(&card->ipm_lock);
	spin_lock_init(&card->mask_lock);
#ifdef CONFIG_IP_MULTICAST
	INIT_LIST_HEAD(&card->ipm_list);
#endif
	INIT_LIST_HEAD(&card->lancmd_waiters);
}

static void lcs_clear_multicast_list(struct lcs_card *card)
{
#ifdef	CONFIG_IP_MULTICAST
	struct lcs_ipm_list *ipm;
	unsigned long flags;

	/* Free multicast list. */
	LCS_DBF_TEXT(3, setup, "clmclist");
	spin_lock_irqsave(&card->ipm_lock, flags);
	while (!list_empty(&card->ipm_list)){
		ipm = list_entry(card->ipm_list.next,
				 struct lcs_ipm_list, list);
		list_del(&ipm->list);
		if (ipm->ipm_state != LCS_IPM_STATE_SET_REQUIRED){
			spin_unlock_irqrestore(&card->ipm_lock, flags);
			lcs_send_delipm(card, ipm);
			spin_lock_irqsave(&card->ipm_lock, flags);
		}
		kfree(ipm);
	}
	spin_unlock_irqrestore(&card->ipm_lock, flags);
#endif
}
/**
 * Cleanup channels,card and state machines.
 */
static void
lcs_cleanup_card(struct lcs_card *card)
{

	LCS_DBF_TEXT(3, setup, "cleancrd");
	LCS_DBF_HEX(2,setup,&card,sizeof(void*));

	if (card->dev != NULL)
		free_netdev(card->dev);
	/* Cleanup channels. */
	lcs_cleanup_channel(&card->write);
	lcs_cleanup_channel(&card->read);
}

/**
 * Start channel.
 */
static int
lcs_start_channel(struct lcs_channel *channel)
{
	unsigned long flags;
	int rc;

	LCS_DBF_TEXT_(4, trace,"ssch%s", dev_name(&channel->ccwdev->dev));
	spin_lock_irqsave(get_ccwdev_lock(channel->ccwdev), flags);
	rc = ccw_device_start(channel->ccwdev,
			      channel->ccws + channel->io_idx, 0, 0,
			      DOIO_DENY_PREFETCH | DOIO_ALLOW_SUSPEND);
	if (rc == 0)
		channel->state = LCS_CH_STATE_RUNNING;
	spin_unlock_irqrestore(get_ccwdev_lock(channel->ccwdev), flags);
	if (rc) {
		LCS_DBF_TEXT_(4,trace,"essh%s",
			      dev_name(&channel->ccwdev->dev));
		dev_err(&channel->ccwdev->dev,
			"Starting an LCS device resulted in an error,"
			" rc=%d!\n", rc);
	}
	return rc;
}

static int
lcs_clear_channel(struct lcs_channel *channel)
{
	unsigned long flags;
	int rc;

	LCS_DBF_TEXT(4,trace,"clearch");
	LCS_DBF_TEXT_(4, trace, "%s", dev_name(&channel->ccwdev->dev));
	spin_lock_irqsave(get_ccwdev_lock(channel->ccwdev), flags);
	rc = ccw_device_clear(channel->ccwdev, (addr_t) channel);
	spin_unlock_irqrestore(get_ccwdev_lock(channel->ccwdev), flags);
	if (rc) {
		LCS_DBF_TEXT_(4, trace, "ecsc%s",
			      dev_name(&channel->ccwdev->dev));
		return rc;
	}
	wait_event(channel->wait_q, (channel->state == LCS_CH_STATE_CLEARED));
	channel->state = LCS_CH_STATE_STOPPED;
	return rc;
}


/**
 * Stop channel.
 */
static int
lcs_stop_channel(struct lcs_channel *channel)
{
	unsigned long flags;
	int rc;

	if (channel->state == LCS_CH_STATE_STOPPED)
		return 0;
	LCS_DBF_TEXT(4,trace,"haltsch");
	LCS_DBF_TEXT_(4, trace, "%s", dev_name(&channel->ccwdev->dev));
	channel->state = LCS_CH_STATE_INIT;
	spin_lock_irqsave(get_ccwdev_lock(channel->ccwdev), flags);
	rc = ccw_device_halt(channel->ccwdev, (addr_t) channel);
	spin_unlock_irqrestore(get_ccwdev_lock(channel->ccwdev), flags);
	if (rc) {
		LCS_DBF_TEXT_(4, trace, "ehsc%s",
			      dev_name(&channel->ccwdev->dev));
		return rc;
	}
	/* Asynchronous halt initialted. Wait for its completion. */
	wait_event(channel->wait_q, (channel->state == LCS_CH_STATE_HALTED));
	lcs_clear_channel(channel);
	return 0;
}

/**
 * start read and write channel
 */
static int
lcs_start_channels(struct lcs_card *card)
{
	int rc;

	LCS_DBF_TEXT(2, trace, "chstart");
	/* start read channel */
	rc = lcs_start_channel(&card->read);
	if (rc)
		return rc;
	/* start write channel */
	rc = lcs_start_channel(&card->write);
	if (rc)
		lcs_stop_channel(&card->read);
	return rc;
}

/**
 * stop read and write channel
 */
static int
lcs_stop_channels(struct lcs_card *card)
{
	LCS_DBF_TEXT(2, trace, "chhalt");
	lcs_stop_channel(&card->read);
	lcs_stop_channel(&card->write);
	return 0;
}

/**
 * Get empty buffer.
 */
static struct lcs_buffer *
__lcs_get_buffer(struct lcs_channel *channel)
{
	int index;

	LCS_DBF_TEXT(5, trace, "_getbuff");
	index = channel->io_idx;
	do {
		if (channel->iob[index].state == LCS_BUF_STATE_EMPTY) {
			channel->iob[index].state = LCS_BUF_STATE_LOCKED;
			return channel->iob + index;
		}
		index = (index + 1) & (LCS_NUM_BUFFS - 1);
	} while (index != channel->io_idx);
	return NULL;
}

static struct lcs_buffer *
lcs_get_buffer(struct lcs_channel *channel)
{
	struct lcs_buffer *buffer;
	unsigned long flags;

	LCS_DBF_TEXT(5, trace, "getbuff");
	spin_lock_irqsave(get_ccwdev_lock(channel->ccwdev), flags);
	buffer = __lcs_get_buffer(channel);
	spin_unlock_irqrestore(get_ccwdev_lock(channel->ccwdev), flags);
	return buffer;
}

/**
 * Resume channel program if the channel is suspended.
 */
static int
__lcs_resume_channel(struct lcs_channel *channel)
{
	int rc;

	if (channel->state != LCS_CH_STATE_SUSPENDED)
		return 0;
	if (channel->ccws[channel->io_idx].flags & CCW_FLAG_SUSPEND)
		return 0;
	LCS_DBF_TEXT_(5, trace, "rsch%s", dev_name(&channel->ccwdev->dev));
	rc = ccw_device_resume(channel->ccwdev);
	if (rc) {
		LCS_DBF_TEXT_(4, trace, "ersc%s",
			      dev_name(&channel->ccwdev->dev));
		dev_err(&channel->ccwdev->dev,
			"Sending data from the LCS device to the LAN failed"
			" with rc=%d\n",rc);
	} else
		channel->state = LCS_CH_STATE_RUNNING;
	return rc;

}

/**
 * Make a buffer ready for processing.
 */
static void __lcs_ready_buffer_bits(struct lcs_channel *channel, int index)
{
	int prev, next;

	LCS_DBF_TEXT(5, trace, "rdybits");
	prev = (index - 1) & (LCS_NUM_BUFFS - 1);
	next = (index + 1) & (LCS_NUM_BUFFS - 1);
	/* Check if we may clear the suspend bit of this buffer. */
	if (channel->ccws[next].flags & CCW_FLAG_SUSPEND) {
		/* Check if we have to set the PCI bit. */
		if (!(channel->ccws[prev].flags & CCW_FLAG_SUSPEND))
			/* Suspend bit of the previous buffer is not set. */
			channel->ccws[index].flags |= CCW_FLAG_PCI;
		/* Suspend bit of the next buffer is set. */
		channel->ccws[index].flags &= ~CCW_FLAG_SUSPEND;
	}
}

static int
lcs_ready_buffer(struct lcs_channel *channel, struct lcs_buffer *buffer)
{
	unsigned long flags;
	int index, rc;

	LCS_DBF_TEXT(5, trace, "rdybuff");
	BUG_ON(buffer->state != LCS_BUF_STATE_LOCKED &&
	       buffer->state != LCS_BUF_STATE_PROCESSED);
	spin_lock_irqsave(get_ccwdev_lock(channel->ccwdev), flags);
	buffer->state = LCS_BUF_STATE_READY;
	index = buffer - channel->iob;
	/* Set length. */
	channel->ccws[index].count = buffer->count;
	/* Check relevant PCI/suspend bits. */
	__lcs_ready_buffer_bits(channel, index);
	rc = __lcs_resume_channel(channel);
	spin_unlock_irqrestore(get_ccwdev_lock(channel->ccwdev), flags);
	return rc;
}

/**
 * Mark the buffer as processed. Take care of the suspend bit
 * of the previous buffer. This function is called from
 * interrupt context, so the lock must not be taken.
 */
static int
__lcs_processed_buffer(struct lcs_channel *channel, struct lcs_buffer *buffer)
{
	int index, prev, next;

	LCS_DBF_TEXT(5, trace, "prcsbuff");
	BUG_ON(buffer->state != LCS_BUF_STATE_READY);
	buffer->state = LCS_BUF_STATE_PROCESSED;
	index = buffer - channel->iob;
	prev = (index - 1) & (LCS_NUM_BUFFS - 1);
	next = (index + 1) & (LCS_NUM_BUFFS - 1);
	/* Set the suspend bit and clear the PCI bit of this buffer. */
	channel->ccws[index].flags |= CCW_FLAG_SUSPEND;
	channel->ccws[index].flags &= ~CCW_FLAG_PCI;
	/* Check the suspend bit of the previous buffer. */
	if (channel->iob[prev].state == LCS_BUF_STATE_READY) {
		/*
		 * Previous buffer is in state ready. It might have
		 * happened in lcs_ready_buffer that the suspend bit
		 * has not been cleared to avoid an endless loop.
		 * Do it now.
		 */
		__lcs_ready_buffer_bits(channel, prev);
	}
	/* Clear PCI bit of next buffer. */
	channel->ccws[next].flags &= ~CCW_FLAG_PCI;
	return __lcs_resume_channel(channel);
}

/**
 * Put a processed buffer back to state empty.
 */
static void
lcs_release_buffer(struct lcs_channel *channel, struct lcs_buffer *buffer)
{
	unsigned long flags;

	LCS_DBF_TEXT(5, trace, "relbuff");
	BUG_ON(buffer->state != LCS_BUF_STATE_LOCKED &&
	       buffer->state != LCS_BUF_STATE_PROCESSED);
	spin_lock_irqsave(get_ccwdev_lock(channel->ccwdev), flags);
	buffer->state = LCS_BUF_STATE_EMPTY;
	spin_unlock_irqrestore(get_ccwdev_lock(channel->ccwdev), flags);
}

/**
 * Get buffer for a lan command.
 */
static struct lcs_buffer *
lcs_get_lancmd(struct lcs_card *card, int count)
{
	struct lcs_buffer *buffer;
	struct lcs_cmd *cmd;

	LCS_DBF_TEXT(4, trace, "getlncmd");
	/* Get buffer and wait if none is available. */
	wait_event(card->write.wait_q,
		   ((buffer = lcs_get_buffer(&card->write)) != NULL));
	count += sizeof(struct lcs_header);
	*(__u16 *)(buffer->data + count) = 0;
	buffer->count = count + sizeof(__u16);
	buffer->callback = lcs_release_buffer;
	cmd = (struct lcs_cmd *) buffer->data;
	cmd->offset = count;
	cmd->type = LCS_FRAME_TYPE_CONTROL;
	cmd->slot = 0;
	return buffer;
}


static void
lcs_get_reply(struct lcs_reply *reply)
{
	WARN_ON(atomic_read(&reply->refcnt) <= 0);
	atomic_inc(&reply->refcnt);
}

static void
lcs_put_reply(struct lcs_reply *reply)
{
        WARN_ON(atomic_read(&reply->refcnt) <= 0);
        if (atomic_dec_and_test(&reply->refcnt)) {
		kfree(reply);
	}

}

static struct lcs_reply *
lcs_alloc_reply(struct lcs_cmd *cmd)
{
	struct lcs_reply *reply;

	LCS_DBF_TEXT(4, trace, "getreply");

	reply = kzalloc(sizeof(struct lcs_reply), GFP_ATOMIC);
	if (!reply)
		return NULL;
	atomic_set(&reply->refcnt,1);
	reply->sequence_no = cmd->sequence_no;
	reply->received = 0;
	reply->rc = 0;
	init_waitqueue_head(&reply->wait_q);

	return reply;
}

/**
 * Notifier function for lancmd replies. Called from read irq.
 */
static void
lcs_notify_lancmd_waiters(struct lcs_card *card, struct lcs_cmd *cmd)
{
	struct list_head *l, *n;
	struct lcs_reply *reply;

	LCS_DBF_TEXT(4, trace, "notiwait");
	spin_lock(&card->lock);
	list_for_each_safe(l, n, &card->lancmd_waiters) {
		reply = list_entry(l, struct lcs_reply, list);
		if (reply->sequence_no == cmd->sequence_no) {
			lcs_get_reply(reply);
			list_del_init(&reply->list);
			if (reply->callback != NULL)
				reply->callback(card, cmd);
			reply->received = 1;
			reply->rc = cmd->return_code;
			wake_up(&reply->wait_q);
			lcs_put_reply(reply);
			break;
		}
	}
	spin_unlock(&card->lock);
}

/**
 * Emit buffer of a lan command.
 */
static void
lcs_lancmd_timeout(struct timer_list *t)
{
	struct lcs_reply *reply = from_timer(reply, t, timer);
	struct lcs_reply *list_reply, *r;
	unsigned long flags;

	LCS_DBF_TEXT(4, trace, "timeout");
	spin_lock_irqsave(&reply->card->lock, flags);
	list_for_each_entry_safe(list_reply, r,
				 &reply->card->lancmd_waiters,list) {
		if (reply == list_reply) {
			lcs_get_reply(reply);
			list_del_init(&reply->list);
			spin_unlock_irqrestore(&reply->card->lock, flags);
			reply->received = 1;
			reply->rc = -ETIME;
			wake_up(&reply->wait_q);
			lcs_put_reply(reply);
			return;
		}
	}
	spin_unlock_irqrestore(&reply->card->lock, flags);
}

static int
lcs_send_lancmd(struct lcs_card *card, struct lcs_buffer *buffer,
		void (*reply_callback)(struct lcs_card *, struct lcs_cmd *))
{
	struct lcs_reply *reply;
	struct lcs_cmd *cmd;
	unsigned long flags;
	int rc;

	LCS_DBF_TEXT(4, trace, "sendcmd");
	cmd = (struct lcs_cmd *) buffer->data;
	cmd->return_code = 0;
	cmd->sequence_no = card->sequence_no++;
	reply = lcs_alloc_reply(cmd);
	if (!reply)
		return -ENOMEM;
	reply->callback = reply_callback;
	reply->card = card;
	spin_lock_irqsave(&card->lock, flags);
	list_add_tail(&reply->list, &card->lancmd_waiters);
	spin_unlock_irqrestore(&card->lock, flags);

	buffer->callback = lcs_release_buffer;
	rc = lcs_ready_buffer(&card->write, buffer);
	if (rc)
		return rc;
	timer_setup(&reply->timer, lcs_lancmd_timeout, 0);
	mod_timer(&reply->timer, jiffies + HZ * card->lancmd_timeout);
	wait_event(reply->wait_q, reply->received);
	del_timer_sync(&reply->timer);
	LCS_DBF_TEXT_(4, trace, "rc:%d",reply->rc);
	rc = reply->rc;
	lcs_put_reply(reply);
	return rc ? -EIO : 0;
}

/**
 * LCS startup command
 */
static int
lcs_send_startup(struct lcs_card *card, __u8 initiator)
{
	struct lcs_buffer *buffer;
	struct lcs_cmd *cmd;

	LCS_DBF_TEXT(2, trace, "startup");
	buffer = lcs_get_lancmd(card, LCS_STD_CMD_SIZE);
	cmd = (struct lcs_cmd *) buffer->data;
	cmd->cmd_code = LCS_CMD_STARTUP;
	cmd->initiator = initiator;
	cmd->cmd.lcs_startup.buff_size = LCS_IOBUFFERSIZE;
	return lcs_send_lancmd(card, buffer, NULL);
}

/**
 * LCS shutdown command
 */
static int
lcs_send_shutdown(struct lcs_card *card)
{
	struct lcs_buffer *buffer;
	struct lcs_cmd *cmd;

	LCS_DBF_TEXT(2, trace, "shutdown");
	buffer = lcs_get_lancmd(card, LCS_STD_CMD_SIZE);
	cmd = (struct lcs_cmd *) buffer->data;
	cmd->cmd_code = LCS_CMD_SHUTDOWN;
	cmd->initiator = LCS_INITIATOR_TCPIP;
	return lcs_send_lancmd(card, buffer, NULL);
}

/**
 * LCS lanstat command
 */
static void
__lcs_lanstat_cb(struct lcs_card *card, struct lcs_cmd *cmd)
{
	LCS_DBF_TEXT(2, trace, "statcb");
	memcpy(card->mac, cmd->cmd.lcs_lanstat_cmd.mac_addr, LCS_MAC_LENGTH);
}

static int
lcs_send_lanstat(struct lcs_card *card)
{
	struct lcs_buffer *buffer;
	struct lcs_cmd *cmd;

	LCS_DBF_TEXT(2,trace, "cmdstat");
	buffer = lcs_get_lancmd(card, LCS_STD_CMD_SIZE);
	cmd = (struct lcs_cmd *) buffer->data;
	/* Setup lanstat command. */
	cmd->cmd_code = LCS_CMD_LANSTAT;
	cmd->initiator = LCS_INITIATOR_TCPIP;
	cmd->cmd.lcs_std_cmd.lan_type = card->lan_type;
	cmd->cmd.lcs_std_cmd.portno = card->portno;
	return lcs_send_lancmd(card, buffer, __lcs_lanstat_cb);
}

/**
 * send stoplan command
 */
static int
lcs_send_stoplan(struct lcs_card *card, __u8 initiator)
{
	struct lcs_buffer *buffer;
	struct lcs_cmd *cmd;

	LCS_DBF_TEXT(2, trace, "cmdstpln");
	buffer = lcs_get_lancmd(card, LCS_STD_CMD_SIZE);
	cmd = (struct lcs_cmd *) buffer->data;
	cmd->cmd_code = LCS_CMD_STOPLAN;
	cmd->initiator = initiator;
	cmd->cmd.lcs_std_cmd.lan_type = card->lan_type;
	cmd->cmd.lcs_std_cmd.portno = card->portno;
	return lcs_send_lancmd(card, buffer, NULL);
}

/**
 * send startlan command
 */
static void
__lcs_send_startlan_cb(struct lcs_card *card, struct lcs_cmd *cmd)
{
	LCS_DBF_TEXT(2, trace, "srtlancb");
	card->lan_type = cmd->cmd.lcs_std_cmd.lan_type;
	card->portno = cmd->cmd.lcs_std_cmd.portno;
}

static int
lcs_send_startlan(struct lcs_card *card, __u8 initiator)
{
	struct lcs_buffer *buffer;
	struct lcs_cmd *cmd;

	LCS_DBF_TEXT(2, trace, "cmdstaln");
	buffer = lcs_get_lancmd(card, LCS_STD_CMD_SIZE);
	cmd = (struct lcs_cmd *) buffer->data;
	cmd->cmd_code = LCS_CMD_STARTLAN;
	cmd->initiator = initiator;
	cmd->cmd.lcs_std_cmd.lan_type = card->lan_type;
	cmd->cmd.lcs_std_cmd.portno = card->portno;
	return lcs_send_lancmd(card, buffer, __lcs_send_startlan_cb);
}

#ifdef CONFIG_IP_MULTICAST
/**
 * send setipm command (Multicast)
 */
static int
lcs_send_setipm(struct lcs_card *card,struct lcs_ipm_list *ipm_list)
{
	struct lcs_buffer *buffer;
	struct lcs_cmd *cmd;

	LCS_DBF_TEXT(2, trace, "cmdsetim");
	buffer = lcs_get_lancmd(card, LCS_MULTICAST_CMD_SIZE);
	cmd = (struct lcs_cmd *) buffer->data;
	cmd->cmd_code = LCS_CMD_SETIPM;
	cmd->initiator = LCS_INITIATOR_TCPIP;
	cmd->cmd.lcs_qipassist.lan_type = card->lan_type;
	cmd->cmd.lcs_qipassist.portno = card->portno;
	cmd->cmd.lcs_qipassist.version = 4;
	cmd->cmd.lcs_qipassist.num_ip_pairs = 1;
	memcpy(cmd->cmd.lcs_qipassist.lcs_ipass_ctlmsg.ip_mac_pair,
	       &ipm_list->ipm, sizeof (struct lcs_ip_mac_pair));
	LCS_DBF_TEXT_(2, trace, "%x",ipm_list->ipm.ip_addr);
	return lcs_send_lancmd(card, buffer, NULL);
}

/**
 * send delipm command (Multicast)
 */
static int
lcs_send_delipm(struct lcs_card *card,struct lcs_ipm_list *ipm_list)
{
	struct lcs_buffer *buffer;
	struct lcs_cmd *cmd;

	LCS_DBF_TEXT(2, trace, "cmddelim");
	buffer = lcs_get_lancmd(card, LCS_MULTICAST_CMD_SIZE);
	cmd = (struct lcs_cmd *) buffer->data;
	cmd->cmd_code = LCS_CMD_DELIPM;
	cmd->initiator = LCS_INITIATOR_TCPIP;
	cmd->cmd.lcs_qipassist.lan_type = card->lan_type;
	cmd->cmd.lcs_qipassist.portno = card->portno;
	cmd->cmd.lcs_qipassist.version = 4;
	cmd->cmd.lcs_qipassist.num_ip_pairs = 1;
	memcpy(cmd->cmd.lcs_qipassist.lcs_ipass_ctlmsg.ip_mac_pair,
	       &ipm_list->ipm, sizeof (struct lcs_ip_mac_pair));
	LCS_DBF_TEXT_(2, trace, "%x",ipm_list->ipm.ip_addr);
	return lcs_send_lancmd(card, buffer, NULL);
}

/**
 * check if multicast is supported by LCS
 */
static void
__lcs_check_multicast_cb(struct lcs_card *card, struct lcs_cmd *cmd)
{
	LCS_DBF_TEXT(2, trace, "chkmccb");
	card->ip_assists_supported =
		cmd->cmd.lcs_qipassist.ip_assists_supported;
	card->ip_assists_enabled =
		cmd->cmd.lcs_qipassist.ip_assists_enabled;
}

static int
lcs_check_multicast_support(struct lcs_card *card)
{
	struct lcs_buffer *buffer;
	struct lcs_cmd *cmd;
	int rc;

	LCS_DBF_TEXT(2, trace, "cmdqipa");
	/* Send query ipassist. */
	buffer = lcs_get_lancmd(card, LCS_STD_CMD_SIZE);
	cmd = (struct lcs_cmd *) buffer->data;
	cmd->cmd_code = LCS_CMD_QIPASSIST;
	cmd->initiator = LCS_INITIATOR_TCPIP;
	cmd->cmd.lcs_qipassist.lan_type = card->lan_type;
	cmd->cmd.lcs_qipassist.portno = card->portno;
	cmd->cmd.lcs_qipassist.version = 4;
	cmd->cmd.lcs_qipassist.num_ip_pairs = 1;
	rc = lcs_send_lancmd(card, buffer, __lcs_check_multicast_cb);
	if (rc != 0) {
		pr_err("Query IPAssist failed. Assuming unsupported!\n");
		return -EOPNOTSUPP;
	}
	if (card->ip_assists_supported & LCS_IPASS_MULTICAST_SUPPORT)
		return 0;
	return -EOPNOTSUPP;
}

/**
 * set or del multicast address on LCS card
 */
static void
lcs_fix_multicast_list(struct lcs_card *card)
{
	struct list_head failed_list;
	struct lcs_ipm_list *ipm, *tmp;
	unsigned long flags;
	int rc;

	LCS_DBF_TEXT(4,trace, "fixipm");
	INIT_LIST_HEAD(&failed_list);
	spin_lock_irqsave(&card->ipm_lock, flags);
list_modified:
	list_for_each_entry_safe(ipm, tmp, &card->ipm_list, list){
		switch (ipm->ipm_state) {
		case LCS_IPM_STATE_SET_REQUIRED:
			/* del from ipm_list so no one else can tamper with
			 * this entry */
			list_del_init(&ipm->list);
			spin_unlock_irqrestore(&card->ipm_lock, flags);
			rc = lcs_send_setipm(card, ipm);
			spin_lock_irqsave(&card->ipm_lock, flags);
			if (rc) {
				pr_info("Adding multicast address failed."
					" Table possibly full!\n");
				/* store ipm in failed list -> will be added
				 * to ipm_list again, so a retry will be done
				 * during the next call of this function */
				list_add_tail(&ipm->list, &failed_list);
			} else {
				ipm->ipm_state = LCS_IPM_STATE_ON_CARD;
				/* re-insert into ipm_list */
				list_add_tail(&ipm->list, &card->ipm_list);
			}
			goto list_modified;
		case LCS_IPM_STATE_DEL_REQUIRED:
			list_del(&ipm->list);
			spin_unlock_irqrestore(&card->ipm_lock, flags);
			lcs_send_delipm(card, ipm);
			spin_lock_irqsave(&card->ipm_lock, flags);
			kfree(ipm);
			goto list_modified;
		case LCS_IPM_STATE_ON_CARD:
			break;
		}
	}
	/* re-insert all entries from the failed_list into ipm_list */
	list_for_each_entry_safe(ipm, tmp, &failed_list, list)
		list_move_tail(&ipm->list, &card->ipm_list);

	spin_unlock_irqrestore(&card->ipm_lock, flags);
}

/**
 * get mac address for the relevant Multicast address
 */
static void
lcs_get_mac_for_ipm(__be32 ipm, char *mac, struct net_device *dev)
{
	LCS_DBF_TEXT(4,trace, "getmac");
	ip_eth_mc_map(ipm, mac);
}

/**
 * function called by net device to handle multicast address relevant things
 */
static void lcs_remove_mc_addresses(struct lcs_card *card,
				    struct in_device *in4_dev)
{
	struct ip_mc_list *im4;
	struct list_head *l;
	struct lcs_ipm_list *ipm;
	unsigned long flags;
	char buf[MAX_ADDR_LEN];

	LCS_DBF_TEXT(4, trace, "remmclst");
	spin_lock_irqsave(&card->ipm_lock, flags);
	list_for_each(l, &card->ipm_list) {
		ipm = list_entry(l, struct lcs_ipm_list, list);
		for (im4 = rcu_dereference(in4_dev->mc_list);
		     im4 != NULL; im4 = rcu_dereference(im4->next_rcu)) {
			lcs_get_mac_for_ipm(im4->multiaddr, buf, card->dev);
			if ( (ipm->ipm.ip_addr == im4->multiaddr) &&
			     (memcmp(buf, &ipm->ipm.mac_addr,
				     LCS_MAC_LENGTH) == 0) )
				break;
		}
		if (im4 == NULL)
			ipm->ipm_state = LCS_IPM_STATE_DEL_REQUIRED;
	}
	spin_unlock_irqrestore(&card->ipm_lock, flags);
}

static struct lcs_ipm_list *lcs_check_addr_entry(struct lcs_card *card,
						 struct ip_mc_list *im4,
						 char *buf)
{
	struct lcs_ipm_list *tmp, *ipm = NULL;
	struct list_head *l;
	unsigned long flags;

	LCS_DBF_TEXT(4, trace, "chkmcent");
	spin_lock_irqsave(&card->ipm_lock, flags);
	list_for_each(l, &card->ipm_list) {
		tmp = list_entry(l, struct lcs_ipm_list, list);
		if ( (tmp->ipm.ip_addr == im4->multiaddr) &&
		     (memcmp(buf, &tmp->ipm.mac_addr,
			     LCS_MAC_LENGTH) == 0) ) {
			ipm = tmp;
			break;
		}
	}
	spin_unlock_irqrestore(&card->ipm_lock, flags);
	return ipm;
}

static void lcs_set_mc_addresses(struct lcs_card *card,
				 struct in_device *in4_dev)
{

	struct ip_mc_list *im4;
	struct lcs_ipm_list *ipm;
	char buf[MAX_ADDR_LEN];
	unsigned long flags;

	LCS_DBF_TEXT(4, trace, "setmclst");
	for (im4 = rcu_dereference(in4_dev->mc_list); im4 != NULL;
	     im4 = rcu_dereference(im4->next_rcu)) {
		lcs_get_mac_for_ipm(im4->multiaddr, buf, card->dev);
		ipm = lcs_check_addr_entry(card, im4, buf);
		if (ipm != NULL)
			continue;	/* Address already in list. */
		ipm = kzalloc(sizeof(struct lcs_ipm_list), GFP_ATOMIC);
		if (ipm == NULL) {
			pr_info("Not enough memory to add"
				" new multicast entry!\n");
			break;
		}
		memcpy(&ipm->ipm.mac_addr, buf, LCS_MAC_LENGTH);
		ipm->ipm.ip_addr = im4->multiaddr;
		ipm->ipm_state = LCS_IPM_STATE_SET_REQUIRED;
		spin_lock_irqsave(&card->ipm_lock, flags);
		LCS_DBF_HEX(2,trace,&ipm->ipm.ip_addr,4);
		list_add(&ipm->list, &card->ipm_list);
		spin_unlock_irqrestore(&card->ipm_lock, flags);
	}
}

static int
lcs_register_mc_addresses(void *data)
{
	struct lcs_card *card;
	struct in_device *in4_dev;

	card = (struct lcs_card *) data;

	if (!lcs_do_run_thread(card, LCS_SET_MC_THREAD))
		return 0;
	LCS_DBF_TEXT(4, trace, "regmulti");

	in4_dev = in_dev_get(card->dev);
	if (in4_dev == NULL)
		goto out;
	rcu_read_lock();
	lcs_remove_mc_addresses(card,in4_dev);
	lcs_set_mc_addresses(card, in4_dev);
	rcu_read_unlock();
	in_dev_put(in4_dev);

	netif_carrier_off(card->dev);
	netif_tx_disable(card->dev);
	wait_event(card->write.wait_q,
			(card->write.state != LCS_CH_STATE_RUNNING));
	lcs_fix_multicast_list(card);
	if (card->state == DEV_STATE_UP) {
		netif_carrier_on(card->dev);
		netif_wake_queue(card->dev);
	}
out:
	lcs_clear_thread_running_bit(card, LCS_SET_MC_THREAD);
	return 0;
}
#endif /* CONFIG_IP_MULTICAST */

/**
 * function called by net device to
 * handle multicast address relevant things
 */
static void
lcs_set_multicast_list(struct net_device *dev)
{
#ifdef CONFIG_IP_MULTICAST
        struct lcs_card *card;

        LCS_DBF_TEXT(4, trace, "setmulti");
        card = (struct lcs_card *) dev->ml_priv;

        if (!lcs_set_thread_start_bit(card, LCS_SET_MC_THREAD))
		schedule_work(&card->kernel_thread_starter);
#endif /* CONFIG_IP_MULTICAST */
}

static long
lcs_check_irb_error(struct ccw_device *cdev, struct irb *irb)
{
	if (!IS_ERR(irb))
		return 0;

	switch (PTR_ERR(irb)) {
	case -EIO:
		dev_warn(&cdev->dev,
			"An I/O-error occurred on the LCS device\n");
		LCS_DBF_TEXT(2, trace, "ckirberr");
		LCS_DBF_TEXT_(2, trace, "  rc%d", -EIO);
		break;
	case -ETIMEDOUT:
		dev_warn(&cdev->dev,
			"A command timed out on the LCS device\n");
		LCS_DBF_TEXT(2, trace, "ckirberr");
		LCS_DBF_TEXT_(2, trace, "  rc%d", -ETIMEDOUT);
		break;
	default:
		dev_warn(&cdev->dev,
			"An error occurred on the LCS device, rc=%ld\n",
			PTR_ERR(irb));
		LCS_DBF_TEXT(2, trace, "ckirberr");
		LCS_DBF_TEXT(2, trace, "  rc???");
	}
	return PTR_ERR(irb);
}

static int
lcs_get_problem(struct ccw_device *cdev, struct irb *irb)
{
	int dstat, cstat;
	char *sense;

	sense = (char *) irb->ecw;
	cstat = irb->scsw.cmd.cstat;
	dstat = irb->scsw.cmd.dstat;

	if (cstat & (SCHN_STAT_CHN_CTRL_CHK | SCHN_STAT_INTF_CTRL_CHK |
		     SCHN_STAT_CHN_DATA_CHK | SCHN_STAT_CHAIN_CHECK |
		     SCHN_STAT_PROT_CHECK   | SCHN_STAT_PROG_CHECK)) {
		LCS_DBF_TEXT(2, trace, "CGENCHK");
		return 1;
	}
	if (dstat & DEV_STAT_UNIT_CHECK) {
		if (sense[LCS_SENSE_BYTE_1] &
		    LCS_SENSE_RESETTING_EVENT) {
			LCS_DBF_TEXT(2, trace, "REVIND");
			return 1;
		}
		if (sense[LCS_SENSE_BYTE_0] &
		    LCS_SENSE_CMD_REJECT) {
			LCS_DBF_TEXT(2, trace, "CMDREJ");
			return 0;
		}
		if ((!sense[LCS_SENSE_BYTE_0]) &&
		    (!sense[LCS_SENSE_BYTE_1]) &&
		    (!sense[LCS_SENSE_BYTE_2]) &&
		    (!sense[LCS_SENSE_BYTE_3])) {
			LCS_DBF_TEXT(2, trace, "ZEROSEN");
			return 0;
		}
		LCS_DBF_TEXT(2, trace, "DGENCHK");
		return 1;
	}
	return 0;
}

static void
lcs_schedule_recovery(struct lcs_card *card)
{
	LCS_DBF_TEXT(2, trace, "startrec");
	if (!lcs_set_thread_start_bit(card, LCS_RECOVERY_THREAD))
		schedule_work(&card->kernel_thread_starter);
}

/**
 * IRQ Handler for LCS channels
 */
static void
lcs_irq(struct ccw_device *cdev, unsigned long intparm, struct irb *irb)
{
	struct lcs_card *card;
	struct lcs_channel *channel;
	int rc, index;
	int cstat, dstat;

	if (lcs_check_irb_error(cdev, irb))
		return;

	card = CARD_FROM_DEV(cdev);
	if (card->read.ccwdev == cdev)
		channel = &card->read;
	else
		channel = &card->write;

	cstat = irb->scsw.cmd.cstat;
	dstat = irb->scsw.cmd.dstat;
	LCS_DBF_TEXT_(5, trace, "Rint%s", dev_name(&cdev->dev));
	LCS_DBF_TEXT_(5, trace, "%4x%4x", irb->scsw.cmd.cstat,
		      irb->scsw.cmd.dstat);
	LCS_DBF_TEXT_(5, trace, "%4x%4x", irb->scsw.cmd.fctl,
		      irb->scsw.cmd.actl);

	/* Check for channel and device errors presented */
	rc = lcs_get_problem(cdev, irb);
	if (rc || (dstat & DEV_STAT_UNIT_EXCEP)) {
		dev_warn(&cdev->dev,
			"The LCS device stopped because of an error,"
			" dstat=0x%X, cstat=0x%X \n",
			    dstat, cstat);
		if (rc) {
			channel->state = LCS_CH_STATE_ERROR;
		}
	}
	if (channel->state == LCS_CH_STATE_ERROR) {
		lcs_schedule_recovery(card);
		wake_up(&card->wait_q);
		return;
	}
	/* How far in the ccw chain have we processed? */
	if ((channel->state != LCS_CH_STATE_INIT) &&
	    (irb->scsw.cmd.fctl & SCSW_FCTL_START_FUNC) &&
	    (irb->scsw.cmd.cpa != 0)) {
		index = (struct ccw1 *) __va((addr_t) irb->scsw.cmd.cpa)
			- channel->ccws;
		if ((irb->scsw.cmd.actl & SCSW_ACTL_SUSPENDED) ||
		    (irb->scsw.cmd.cstat & SCHN_STAT_PCI))
			/* Bloody io subsystem tells us lies about cpa... */
			index = (index - 1) & (LCS_NUM_BUFFS - 1);
		while (channel->io_idx != index) {
			__lcs_processed_buffer(channel,
					       channel->iob + channel->io_idx);
			channel->io_idx =
				(channel->io_idx + 1) & (LCS_NUM_BUFFS - 1);
		}
	}

	if ((irb->scsw.cmd.dstat & DEV_STAT_DEV_END) ||
	    (irb->scsw.cmd.dstat & DEV_STAT_CHN_END) ||
	    (irb->scsw.cmd.dstat & DEV_STAT_UNIT_CHECK))
		/* Mark channel as stopped. */
		channel->state = LCS_CH_STATE_STOPPED;
	else if (irb->scsw.cmd.actl & SCSW_ACTL_SUSPENDED)
		/* CCW execution stopped on a suspend bit. */
		channel->state = LCS_CH_STATE_SUSPENDED;
	if (irb->scsw.cmd.fctl & SCSW_FCTL_HALT_FUNC) {
		if (irb->scsw.cmd.cc != 0) {
			ccw_device_halt(channel->ccwdev, (addr_t) channel);
			return;
		}
		/* The channel has been stopped by halt_IO. */
		channel->state = LCS_CH_STATE_HALTED;
	}
	if (irb->scsw.cmd.fctl & SCSW_FCTL_CLEAR_FUNC)
		channel->state = LCS_CH_STATE_CLEARED;
	/* Do the rest in the tasklet. */
	tasklet_schedule(&channel->irq_tasklet);
}

/**
 * Tasklet for IRQ handler
 */
static void
lcs_tasklet(unsigned long data)
{
	unsigned long flags;
	struct lcs_channel *channel;
	struct lcs_buffer *iob;
	int buf_idx;

	channel = (struct lcs_channel *) data;
	LCS_DBF_TEXT_(5, trace, "tlet%s", dev_name(&channel->ccwdev->dev));

	/* Check for processed buffers. */
	iob = channel->iob;
	buf_idx = channel->buf_idx;
	while (iob[buf_idx].state == LCS_BUF_STATE_PROCESSED) {
		/* Do the callback thing. */
		if (iob[buf_idx].callback != NULL)
			iob[buf_idx].callback(channel, iob + buf_idx);
		buf_idx = (buf_idx + 1) & (LCS_NUM_BUFFS - 1);
	}
	channel->buf_idx = buf_idx;

	if (channel->state == LCS_CH_STATE_STOPPED)
		lcs_start_channel(channel);
	spin_lock_irqsave(get_ccwdev_lock(channel->ccwdev), flags);
	if (channel->state == LCS_CH_STATE_SUSPENDED &&
	    channel->iob[channel->io_idx].state == LCS_BUF_STATE_READY)
		__lcs_resume_channel(channel);
	spin_unlock_irqrestore(get_ccwdev_lock(channel->ccwdev), flags);

	/* Something happened on the channel. Wake up waiters. */
	wake_up(&channel->wait_q);
}

/**
 * Finish current tx buffer and make it ready for transmit.
 */
static void
__lcs_emit_txbuffer(struct lcs_card *card)
{
	LCS_DBF_TEXT(5, trace, "emittx");
	*(__u16 *)(card->tx_buffer->data + card->tx_buffer->count) = 0;
	card->tx_buffer->count += 2;
	lcs_ready_buffer(&card->write, card->tx_buffer);
	card->tx_buffer = NULL;
	card->tx_emitted++;
}

/**
 * Callback for finished tx buffers.
 */
static void
lcs_txbuffer_cb(struct lcs_channel *channel, struct lcs_buffer *buffer)
{
	struct lcs_card *card;

	LCS_DBF_TEXT(5, trace, "txbuffcb");
	/* Put buffer back to pool. */
	lcs_release_buffer(channel, buffer);
	card = container_of(channel, struct lcs_card, write);
	if (netif_queue_stopped(card->dev) && netif_carrier_ok(card->dev))
		netif_wake_queue(card->dev);
	spin_lock(&card->lock);
	card->tx_emitted--;
	if (card->tx_emitted <= 0 && card->tx_buffer != NULL)
		/*
		 * Last running tx buffer has finished. Submit partially
		 * filled current buffer.
		 */
		__lcs_emit_txbuffer(card);
	spin_unlock(&card->lock);
}

/**
 * Packet transmit function called by network stack
 */
static int
__lcs_start_xmit(struct lcs_card *card, struct sk_buff *skb,
		 struct net_device *dev)
{
	struct lcs_header *header;
	int rc = NETDEV_TX_OK;

	LCS_DBF_TEXT(5, trace, "hardxmit");
	if (skb == NULL) {
		card->stats.tx_dropped++;
		card->stats.tx_errors++;
		return NETDEV_TX_OK;
	}
	if (card->state != DEV_STATE_UP) {
		dev_kfree_skb(skb);
		card->stats.tx_dropped++;
		card->stats.tx_errors++;
		card->stats.tx_carrier_errors++;
		return NETDEV_TX_OK;
	}
	if (skb->protocol == htons(ETH_P_IPV6)) {
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}
	netif_stop_queue(card->dev);
	spin_lock(&card->lock);
	if (card->tx_buffer != NULL &&
	    card->tx_buffer->count + sizeof(struct lcs_header) +
	    skb->len + sizeof(u16) > LCS_IOBUFFERSIZE)
		/* skb too big for current tx buffer. */
		__lcs_emit_txbuffer(card);
	if (card->tx_buffer == NULL) {
		/* Get new tx buffer */
		card->tx_buffer = lcs_get_buffer(&card->write);
		if (card->tx_buffer == NULL) {
			card->stats.tx_dropped++;
			rc = NETDEV_TX_BUSY;
			goto out;
		}
		card->tx_buffer->callback = lcs_txbuffer_cb;
		card->tx_buffer->count = 0;
	}
	header = (struct lcs_header *)
		(card->tx_buffer->data + card->tx_buffer->count);
	card->tx_buffer->count += skb->len + sizeof(struct lcs_header);
	header->offset = card->tx_buffer->count;
	header->type = card->lan_type;
	header->slot = card->portno;
	skb_copy_from_linear_data(skb, header + 1, skb->len);
	spin_unlock(&card->lock);
	card->stats.tx_bytes += skb->len;
	card->stats.tx_packets++;
	dev_kfree_skb(skb);
	netif_wake_queue(card->dev);
	spin_lock(&card->lock);
	if (card->tx_emitted <= 0 && card->tx_buffer != NULL)
		/* If this is the first tx buffer emit it immediately. */
		__lcs_emit_txbuffer(card);
out:
	spin_unlock(&card->lock);
	return rc;
}

static int
lcs_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct lcs_card *card;
	int rc;

	LCS_DBF_TEXT(5, trace, "pktxmit");
	card = (struct lcs_card *) dev->ml_priv;
	rc = __lcs_start_xmit(card, skb, dev);
	return rc;
}

/**
 * send startlan and lanstat command to make LCS device ready
 */
static int
lcs_startlan_auto(struct lcs_card *card)
{
	int rc;

	LCS_DBF_TEXT(2, trace, "strtauto");
#ifdef CONFIG_ETHERNET
	card->lan_type = LCS_FRAME_TYPE_ENET;
	rc = lcs_send_startlan(card, LCS_INITIATOR_TCPIP);
	if (rc == 0)
		return 0;

#endif
#ifdef CONFIG_FDDI
	card->lan_type = LCS_FRAME_TYPE_FDDI;
	rc = lcs_send_startlan(card, LCS_INITIATOR_TCPIP);
	if (rc == 0)
		return 0;
#endif
	return -EIO;
}

static int
lcs_startlan(struct lcs_card *card)
{
	int rc, i;

	LCS_DBF_TEXT(2, trace, "startlan");
	rc = 0;
	if (card->portno != LCS_INVALID_PORT_NO) {
		if (card->lan_type == LCS_FRAME_TYPE_AUTO)
			rc = lcs_startlan_auto(card);
		else
			rc = lcs_send_startlan(card, LCS_INITIATOR_TCPIP);
	} else {
                for (i = 0; i <= 16; i++) {
                        card->portno = i;
                        if (card->lan_type != LCS_FRAME_TYPE_AUTO)
                                rc = lcs_send_startlan(card,
                                                       LCS_INITIATOR_TCPIP);
                        else
                                /* autodetecting lan type */
                                rc = lcs_startlan_auto(card);
                        if (rc == 0)
                                break;
                }
        }
	if (rc == 0)
		return lcs_send_lanstat(card);
	return rc;
}

/**
 * LCS detect function
 * setup channels and make them I/O ready
 */
static int
lcs_detect(struct lcs_card *card)
{
	int rc = 0;

	LCS_DBF_TEXT(2, setup, "lcsdetct");
	/* start/reset card */
	if (card->dev)
		netif_stop_queue(card->dev);
	rc = lcs_stop_channels(card);
	if (rc == 0) {
		rc = lcs_start_channels(card);
		if (rc == 0) {
			rc = lcs_send_startup(card, LCS_INITIATOR_TCPIP);
			if (rc == 0)
				rc = lcs_startlan(card);
		}
	}
	if (rc == 0) {
		card->state = DEV_STATE_UP;
	} else {
		card->state = DEV_STATE_DOWN;
		card->write.state = LCS_CH_STATE_INIT;
		card->read.state =  LCS_CH_STATE_INIT;
	}
	return rc;
}

/**
 * LCS Stop card
 */
static int
lcs_stopcard(struct lcs_card *card)
{
	int rc;

	LCS_DBF_TEXT(3, setup, "stopcard");

	if (card->read.state != LCS_CH_STATE_STOPPED &&
	    card->write.state != LCS_CH_STATE_STOPPED &&
	    card->read.state != LCS_CH_STATE_ERROR &&
	    card->write.state != LCS_CH_STATE_ERROR &&
	    card->state == DEV_STATE_UP) {
		lcs_clear_multicast_list(card);
		rc = lcs_send_stoplan(card,LCS_INITIATOR_TCPIP);
		rc = lcs_send_shutdown(card);
	}
	rc = lcs_stop_channels(card);
	card->state = DEV_STATE_DOWN;

	return rc;
}

/**
 * Kernel Thread helper functions for LGW initiated commands
 */
static void
lcs_start_kernel_thread(struct work_struct *work)
{
	struct lcs_card *card = container_of(work, struct lcs_card, kernel_thread_starter);
	LCS_DBF_TEXT(5, trace, "krnthrd");
	if (lcs_do_start_thread(card, LCS_RECOVERY_THREAD))
		kthread_run(lcs_recovery, card, "lcs_recover");
#ifdef CONFIG_IP_MULTICAST
	if (lcs_do_start_thread(card, LCS_SET_MC_THREAD))
		kthread_run(lcs_register_mc_addresses, card, "regipm");
#endif
}

/**
 * Process control frames.
 */
static void
lcs_get_control(struct lcs_card *card, struct lcs_cmd *cmd)
{
	LCS_DBF_TEXT(5, trace, "getctrl");
	if (cmd->initiator == LCS_INITIATOR_LGW) {
		switch(cmd->cmd_code) {
		case LCS_CMD_STARTUP:
		case LCS_CMD_STARTLAN:
			lcs_schedule_recovery(card);
			break;
		case LCS_CMD_STOPLAN:
			pr_warn("Stoplan for %s initiated by LGW\n",
				card->dev->name);
			if (card->dev)
				netif_carrier_off(card->dev);
			break;
		default:
			LCS_DBF_TEXT(5, trace, "noLGWcmd");
			break;
		}
	} else
		lcs_notify_lancmd_waiters(card, cmd);
}

/**
 * Unpack network packet.
 */
static void
lcs_get_skb(struct lcs_card *card, char *skb_data, unsigned int skb_len)
{
	struct sk_buff *skb;

	LCS_DBF_TEXT(5, trace, "getskb");
	if (card->dev == NULL ||
	    card->state != DEV_STATE_UP)
		/* The card isn't up. Ignore the packet. */
		return;

	skb = dev_alloc_skb(skb_len);
	if (skb == NULL) {
		dev_err(&card->dev->dev,
			" Allocating a socket buffer to interface %s failed\n",
			  card->dev->name);
		card->stats.rx_dropped++;
		return;
	}
	skb_put_data(skb, skb_data, skb_len);
	skb->protocol =	card->lan_type_trans(skb, card->dev);
	card->stats.rx_bytes += skb_len;
	card->stats.rx_packets++;
	if (skb->protocol == htons(ETH_P_802_2))
		*((__u32 *)skb->cb) = ++card->pkt_seq;
	netif_rx(skb);
}

/**
 * LCS main routine to get packets and lancmd replies from the buffers
 */
static void
lcs_get_frames_cb(struct lcs_channel *channel, struct lcs_buffer *buffer)
{
	struct lcs_card *card;
	struct lcs_header *lcs_hdr;
	__u16 offset;

	LCS_DBF_TEXT(5, trace, "lcsgtpkt");
	lcs_hdr = (struct lcs_header *) buffer->data;
	if (lcs_hdr->offset == LCS_ILLEGAL_OFFSET) {
		LCS_DBF_TEXT(4, trace, "-eiogpkt");
		return;
	}
	card = container_of(channel, struct lcs_card, read);
	offset = 0;
	while (lcs_hdr->offset != 0) {
		if (lcs_hdr->offset <= 0 ||
		    lcs_hdr->offset > LCS_IOBUFFERSIZE ||
		    lcs_hdr->offset < offset) {
			/* Offset invalid. */
			card->stats.rx_length_errors++;
			card->stats.rx_errors++;
			return;
		}
		/* What kind of frame is it? */
		if (lcs_hdr->type == LCS_FRAME_TYPE_CONTROL)
			/* Control frame. */
			lcs_get_control(card, (struct lcs_cmd *) lcs_hdr);
		else if (lcs_hdr->type == LCS_FRAME_TYPE_ENET ||
			 lcs_hdr->type == LCS_FRAME_TYPE_TR ||
			 lcs_hdr->type == LCS_FRAME_TYPE_FDDI)
			/* Normal network packet. */
			lcs_get_skb(card, (char *)(lcs_hdr + 1),
				    lcs_hdr->offset - offset -
				    sizeof(struct lcs_header));
		else
			/* Unknown frame type. */
			; // FIXME: error message ?
		/* Proceed to next frame. */
		offset = lcs_hdr->offset;
		lcs_hdr->offset = LCS_ILLEGAL_OFFSET;
		lcs_hdr = (struct lcs_header *) (buffer->data + offset);
	}
	/* The buffer is now empty. Make it ready again. */
	lcs_ready_buffer(&card->read, buffer);
}

/**
 * get network statistics for ifconfig and other user programs
 */
static struct net_device_stats *
lcs_getstats(struct net_device *dev)
{
	struct lcs_card *card;

	LCS_DBF_TEXT(4, trace, "netstats");
	card = (struct lcs_card *) dev->ml_priv;
	return &card->stats;
}

/**
 * stop lcs device
 * This function will be called by user doing ifconfig xxx down
 */
static int
lcs_stop_device(struct net_device *dev)
{
	struct lcs_card *card;
	int rc;

	LCS_DBF_TEXT(2, trace, "stopdev");
	card   = (struct lcs_card *) dev->ml_priv;
	netif_carrier_off(dev);
	netif_tx_disable(dev);
	dev->flags &= ~IFF_UP;
	wait_event(card->write.wait_q,
		(card->write.state != LCS_CH_STATE_RUNNING));
	rc = lcs_stopcard(card);
	if (rc)
		dev_err(&card->dev->dev,
			" Shutting down the LCS device failed\n");
	return rc;
}

/**
 * start lcs device and make it runnable
 * This function will be called by user doing ifconfig xxx up
 */
static int
lcs_open_device(struct net_device *dev)
{
	struct lcs_card *card;
	int rc;

	LCS_DBF_TEXT(2, trace, "opendev");
	card = (struct lcs_card *) dev->ml_priv;
	/* initialize statistics */
	rc = lcs_detect(card);
	if (rc) {
		pr_err("Error in opening device!\n");

	} else {
		dev->flags |= IFF_UP;
		netif_carrier_on(dev);
		netif_wake_queue(dev);
		card->state = DEV_STATE_UP;
	}
	return rc;
}

/**
 * show function for portno called by cat or similar things
 */
static ssize_t
lcs_portno_show (struct device *dev, struct device_attribute *attr, char *buf)
{
        struct lcs_card *card;

	card = dev_get_drvdata(dev);

        if (!card)
                return 0;

        return sprintf(buf, "%d\n", card->portno);
}

/**
 * store the value which is piped to file portno
 */
static ssize_t
lcs_portno_store (struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
        struct lcs_card *card;
	int rc;
	s16 value;

	card = dev_get_drvdata(dev);

        if (!card)
                return 0;

	rc = kstrtos16(buf, 0, &value);
	if (rc)
		return -EINVAL;
        /* TODO: sanity checks */
        card->portno = value;

        return count;

}

static DEVICE_ATTR(portno, 0644, lcs_portno_show, lcs_portno_store);

static const char *lcs_type[] = {
	"not a channel",
	"2216 parallel",
	"2216 channel",
	"OSA LCS card",
	"unknown channel type",
	"unsupported channel type",
};

static ssize_t
lcs_type_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ccwgroup_device *cgdev;

	cgdev = to_ccwgroupdev(dev);
	if (!cgdev)
		return -ENODEV;

	return sprintf(buf, "%s\n", lcs_type[cgdev->cdev[0]->id.driver_info]);
}

static DEVICE_ATTR(type, 0444, lcs_type_show, NULL);

static ssize_t
lcs_timeout_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct lcs_card *card;

	card = dev_get_drvdata(dev);

	return card ? sprintf(buf, "%u\n", card->lancmd_timeout) : 0;
}

static ssize_t
lcs_timeout_store (struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
        struct lcs_card *card;
	unsigned int value;
	int rc;

	card = dev_get_drvdata(dev);

        if (!card)
                return 0;

	rc = kstrtouint(buf, 0, &value);
	if (rc)
		return -EINVAL;
        /* TODO: sanity checks */
        card->lancmd_timeout = value;

        return count;

}

static DEVICE_ATTR(lancmd_timeout, 0644, lcs_timeout_show, lcs_timeout_store);

static ssize_t
lcs_dev_recover_store(struct device *dev, struct device_attribute *attr,
		      const char *buf, size_t count)
{
	struct lcs_card *card = dev_get_drvdata(dev);
	char *tmp;
	int i;

	if (!card)
		return -EINVAL;
	if (card->state != DEV_STATE_UP)
		return -EPERM;
	i = simple_strtoul(buf, &tmp, 16);
	if (i == 1)
		lcs_schedule_recovery(card);
	return count;
}

static DEVICE_ATTR(recover, 0200, NULL, lcs_dev_recover_store);

static struct attribute * lcs_attrs[] = {
	&dev_attr_portno.attr,
	&dev_attr_type.attr,
	&dev_attr_lancmd_timeout.attr,
	&dev_attr_recover.attr,
	NULL,
};
static struct attribute_group lcs_attr_group = {
	.attrs = lcs_attrs,
};
static const struct attribute_group *lcs_attr_groups[] = {
	&lcs_attr_group,
	NULL,
};
static const struct device_type lcs_devtype = {
	.name = "lcs",
	.groups = lcs_attr_groups,
};

/**
 * lcs_probe_device is called on establishing a new ccwgroup_device.
 */
static int
lcs_probe_device(struct ccwgroup_device *ccwgdev)
{
	struct lcs_card *card;

	if (!get_device(&ccwgdev->dev))
		return -ENODEV;

	LCS_DBF_TEXT(2, setup, "add_dev");
        card = lcs_alloc_card();
        if (!card) {
		LCS_DBF_TEXT_(2, setup, "  rc%d", -ENOMEM);
		put_device(&ccwgdev->dev);
                return -ENOMEM;
        }
	dev_set_drvdata(&ccwgdev->dev, card);
	ccwgdev->cdev[0]->handler = lcs_irq;
	ccwgdev->cdev[1]->handler = lcs_irq;
	card->gdev = ccwgdev;
	INIT_WORK(&card->kernel_thread_starter, lcs_start_kernel_thread);
	card->thread_start_mask = 0;
	card->thread_allowed_mask = 0;
	card->thread_running_mask = 0;
	ccwgdev->dev.type = &lcs_devtype;

	return 0;
}

static int
lcs_register_netdev(struct ccwgroup_device *ccwgdev)
{
	struct lcs_card *card;

	LCS_DBF_TEXT(2, setup, "regnetdv");
	card = dev_get_drvdata(&ccwgdev->dev);
	if (card->dev->reg_state != NETREG_UNINITIALIZED)
		return 0;
	SET_NETDEV_DEV(card->dev, &ccwgdev->dev);
	return register_netdev(card->dev);
}

/**
 * lcs_new_device will be called by setting the group device online.
 */
static const struct net_device_ops lcs_netdev_ops = {
	.ndo_open		= lcs_open_device,
	.ndo_stop		= lcs_stop_device,
	.ndo_get_stats		= lcs_getstats,
	.ndo_start_xmit		= lcs_start_xmit,
};

static const struct net_device_ops lcs_mc_netdev_ops = {
	.ndo_open		= lcs_open_device,
	.ndo_stop		= lcs_stop_device,
	.ndo_get_stats		= lcs_getstats,
	.ndo_start_xmit		= lcs_start_xmit,
	.ndo_set_rx_mode	= lcs_set_multicast_list,
};

static int
lcs_new_device(struct ccwgroup_device *ccwgdev)
{
	struct  lcs_card *card;
	struct net_device *dev=NULL;
	enum lcs_dev_states recover_state;
	int rc;

	card = dev_get_drvdata(&ccwgdev->dev);
	if (!card)
		return -ENODEV;

	LCS_DBF_TEXT(2, setup, "newdev");
	LCS_DBF_HEX(3, setup, &card, sizeof(void*));
	card->read.ccwdev  = ccwgdev->cdev[0];
	card->write.ccwdev = ccwgdev->cdev[1];

	recover_state = card->state;
	rc = ccw_device_set_online(card->read.ccwdev);
	if (rc)
		goto out_err;
	rc = ccw_device_set_online(card->write.ccwdev);
	if (rc)
		goto out_werr;

	LCS_DBF_TEXT(3, setup, "lcsnewdv");

	lcs_setup_card(card);
	rc = lcs_detect(card);
	if (rc) {
		LCS_DBF_TEXT(2, setup, "dtctfail");
		dev_err(&ccwgdev->dev,
			"Detecting a network adapter for LCS devices"
			" failed with rc=%d (0x%x)\n", rc, rc);
		lcs_stopcard(card);
		goto out;
	}
	if (card->dev) {
		LCS_DBF_TEXT(2, setup, "samedev");
		LCS_DBF_HEX(3, setup, &card, sizeof(void*));
		goto netdev_out;
	}
	switch (card->lan_type) {
#ifdef CONFIG_ETHERNET
	case LCS_FRAME_TYPE_ENET:
		card->lan_type_trans = eth_type_trans;
		dev = alloc_etherdev(0);
		break;
#endif
#ifdef CONFIG_FDDI
	case LCS_FRAME_TYPE_FDDI:
		card->lan_type_trans = fddi_type_trans;
		dev = alloc_fddidev(0);
		break;
#endif
	default:
		LCS_DBF_TEXT(3, setup, "errinit");
		pr_err(" Initialization failed\n");
		goto out;
	}
	if (!dev)
		goto out;
	card->dev = dev;
	card->dev->ml_priv = card;
	card->dev->netdev_ops = &lcs_netdev_ops;
	memcpy(card->dev->dev_addr, card->mac, LCS_MAC_LENGTH);
#ifdef CONFIG_IP_MULTICAST
	if (!lcs_check_multicast_support(card))
		card->dev->netdev_ops = &lcs_mc_netdev_ops;
#endif
netdev_out:
	lcs_set_allowed_threads(card,0xffffffff);
	if (recover_state == DEV_STATE_RECOVER) {
		lcs_set_multicast_list(card->dev);
		card->dev->flags |= IFF_UP;
		netif_carrier_on(card->dev);
		netif_wake_queue(card->dev);
		card->state = DEV_STATE_UP;
	} else {
		lcs_stopcard(card);
	}

	if (lcs_register_netdev(ccwgdev) != 0)
		goto out;

	/* Print out supported assists: IPv6 */
	pr_info("LCS device %s %s IPv6 support\n", card->dev->name,
		(card->ip_assists_supported & LCS_IPASS_IPV6_SUPPORT) ?
		"with" : "without");
	/* Print out supported assist: Multicast */
	pr_info("LCS device %s %s Multicast support\n", card->dev->name,
		(card->ip_assists_supported & LCS_IPASS_MULTICAST_SUPPORT) ?
		"with" : "without");
	return 0;
out:

	ccw_device_set_offline(card->write.ccwdev);
out_werr:
	ccw_device_set_offline(card->read.ccwdev);
out_err:
	return -ENODEV;
}

/**
 * lcs_shutdown_device, called when setting the group device offline.
 */
static int
__lcs_shutdown_device(struct ccwgroup_device *ccwgdev, int recovery_mode)
{
	struct lcs_card *card;
	enum lcs_dev_states recover_state;
	int ret = 0, ret2 = 0, ret3 = 0;

	LCS_DBF_TEXT(3, setup, "shtdndev");
	card = dev_get_drvdata(&ccwgdev->dev);
	if (!card)
		return -ENODEV;
	if (recovery_mode == 0) {
		lcs_set_allowed_threads(card, 0);
		if (lcs_wait_for_threads(card, LCS_SET_MC_THREAD))
			return -ERESTARTSYS;
	}
	LCS_DBF_HEX(3, setup, &card, sizeof(void*));
	recover_state = card->state;

	ret = lcs_stop_device(card->dev);
	ret2 = ccw_device_set_offline(card->read.ccwdev);
	ret3 = ccw_device_set_offline(card->write.ccwdev);
	if (!ret)
		ret = (ret2) ? ret2 : ret3;
	if (ret)
		LCS_DBF_TEXT_(3, setup, "1err:%d", ret);
	if (recover_state == DEV_STATE_UP) {
		card->state = DEV_STATE_RECOVER;
	}
	return 0;
}

static int
lcs_shutdown_device(struct ccwgroup_device *ccwgdev)
{
	return __lcs_shutdown_device(ccwgdev, 0);
}

/**
 * drive lcs recovery after startup and startlan initiated by Lan Gateway
 */
static int
lcs_recovery(void *ptr)
{
	struct lcs_card *card;
	struct ccwgroup_device *gdev;
        int rc;

	card = (struct lcs_card *) ptr;

	LCS_DBF_TEXT(4, trace, "recover1");
	if (!lcs_do_run_thread(card, LCS_RECOVERY_THREAD))
		return 0;
	LCS_DBF_TEXT(4, trace, "recover2");
	gdev = card->gdev;
	dev_warn(&gdev->dev,
		"A recovery process has been started for the LCS device\n");
	rc = __lcs_shutdown_device(gdev, 1);
	rc = lcs_new_device(gdev);
	if (!rc)
		pr_info("Device %s successfully recovered!\n",
			card->dev->name);
	else
		pr_info("Device %s could not be recovered!\n",
			card->dev->name);
	lcs_clear_thread_running_bit(card, LCS_RECOVERY_THREAD);
	return 0;
}

/**
 * lcs_remove_device, free buffers and card
 */
static void
lcs_remove_device(struct ccwgroup_device *ccwgdev)
{
	struct lcs_card *card;

	card = dev_get_drvdata(&ccwgdev->dev);
	if (!card)
		return;

	LCS_DBF_TEXT(3, setup, "remdev");
	LCS_DBF_HEX(3, setup, &card, sizeof(void*));
	if (ccwgdev->state == CCWGROUP_ONLINE) {
		lcs_shutdown_device(ccwgdev);
	}
	if (card->dev)
		unregister_netdev(card->dev);
	lcs_cleanup_card(card);
	lcs_free_card(card);
	dev_set_drvdata(&ccwgdev->dev, NULL);
	put_device(&ccwgdev->dev);
}

static int lcs_pm_suspend(struct lcs_card *card)
{
	if (card->dev)
		netif_device_detach(card->dev);
	lcs_set_allowed_threads(card, 0);
	lcs_wait_for_threads(card, 0xffffffff);
	if (card->state != DEV_STATE_DOWN)
		__lcs_shutdown_device(card->gdev, 1);
	return 0;
}

static int lcs_pm_resume(struct lcs_card *card)
{
	int rc = 0;

	if (card->state == DEV_STATE_RECOVER)
		rc = lcs_new_device(card->gdev);
	if (card->dev)
		netif_device_attach(card->dev);
	if (rc) {
		dev_warn(&card->gdev->dev, "The lcs device driver "
			"failed to recover the device\n");
	}
	return rc;
}

static int lcs_prepare(struct ccwgroup_device *gdev)
{
	return 0;
}

static void lcs_complete(struct ccwgroup_device *gdev)
{
	return;
}

static int lcs_freeze(struct ccwgroup_device *gdev)
{
	struct lcs_card *card = dev_get_drvdata(&gdev->dev);
	return lcs_pm_suspend(card);
}

static int lcs_thaw(struct ccwgroup_device *gdev)
{
	struct lcs_card *card = dev_get_drvdata(&gdev->dev);
	return lcs_pm_resume(card);
}

static int lcs_restore(struct ccwgroup_device *gdev)
{
	struct lcs_card *card = dev_get_drvdata(&gdev->dev);
	return lcs_pm_resume(card);
}

static struct ccw_device_id lcs_ids[] = {
	{CCW_DEVICE(0x3088, 0x08), .driver_info = lcs_channel_type_parallel},
	{CCW_DEVICE(0x3088, 0x1f), .driver_info = lcs_channel_type_2216},
	{CCW_DEVICE(0x3088, 0x60), .driver_info = lcs_channel_type_osa2},
	{},
};
MODULE_DEVICE_TABLE(ccw, lcs_ids);

static struct ccw_driver lcs_ccw_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "lcs",
	},
	.ids	= lcs_ids,
	.probe	= ccwgroup_probe_ccwdev,
	.remove	= ccwgroup_remove_ccwdev,
	.int_class = IRQIO_LCS,
};

/**
 * LCS ccwgroup driver registration
 */
static struct ccwgroup_driver lcs_group_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "lcs",
	},
	.ccw_driver  = &lcs_ccw_driver,
	.setup	     = lcs_probe_device,
	.remove      = lcs_remove_device,
	.set_online  = lcs_new_device,
	.set_offline = lcs_shutdown_device,
	.prepare     = lcs_prepare,
	.complete    = lcs_complete,
	.freeze	     = lcs_freeze,
	.thaw	     = lcs_thaw,
	.restore     = lcs_restore,
};

static ssize_t group_store(struct device_driver *ddrv, const char *buf,
			   size_t count)
{
	int err;
	err = ccwgroup_create_dev(lcs_root_dev, &lcs_group_driver, 2, buf);
	return err ? err : count;
}
static DRIVER_ATTR_WO(group);

static struct attribute *lcs_drv_attrs[] = {
	&driver_attr_group.attr,
	NULL,
};
static struct attribute_group lcs_drv_attr_group = {
	.attrs = lcs_drv_attrs,
};
static const struct attribute_group *lcs_drv_attr_groups[] = {
	&lcs_drv_attr_group,
	NULL,
};

/**
 *  LCS Module/Kernel initialization function
 */
static int
__init lcs_init_module(void)
{
	int rc;

	pr_info("Loading %s\n", version);
	rc = lcs_register_debug_facility();
	LCS_DBF_TEXT(0, setup, "lcsinit");
	if (rc)
		goto out_err;
	lcs_root_dev = root_device_register("lcs");
	rc = PTR_ERR_OR_ZERO(lcs_root_dev);
	if (rc)
		goto register_err;
	rc = ccw_driver_register(&lcs_ccw_driver);
	if (rc)
		goto ccw_err;
	lcs_group_driver.driver.groups = lcs_drv_attr_groups;
	rc = ccwgroup_driver_register(&lcs_group_driver);
	if (rc)
		goto ccwgroup_err;
	return 0;

ccwgroup_err:
	ccw_driver_unregister(&lcs_ccw_driver);
ccw_err:
	root_device_unregister(lcs_root_dev);
register_err:
	lcs_unregister_debug_facility();
out_err:
	pr_err("Initializing the lcs device driver failed\n");
	return rc;
}


/**
 *  LCS module cleanup function
 */
static void
__exit lcs_cleanup_module(void)
{
	pr_info("Terminating lcs module.\n");
	LCS_DBF_TEXT(0, trace, "cleanup");
	ccwgroup_driver_unregister(&lcs_group_driver);
	ccw_driver_unregister(&lcs_ccw_driver);
	root_device_unregister(lcs_root_dev);
	lcs_unregister_debug_facility();
}

module_init(lcs_init_module);
module_exit(lcs_cleanup_module);

MODULE_AUTHOR("Frank Pavlic <fpavlic@de.ibm.com>");
MODULE_LICENSE("GPL");

