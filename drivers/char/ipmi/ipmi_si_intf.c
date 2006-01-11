/*
 * ipmi_si.c
 *
 * The interface to the IPMI driver for the system interfaces (KCS, SMIC,
 * BT).
 *
 * Author: MontaVista Software, Inc.
 *         Corey Minyard <minyard@mvista.com>
 *         source@mvista.com
 *
 * Copyright 2002 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 *  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 *  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *  USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * This file holds the "policy" for the interface to the SMI state
 * machine.  It does the configuration, handles timers and interrupts,
 * and drives the real SMI state machine.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <asm/system.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/ioport.h>
#include <linux/notifier.h>
#include <linux/kthread.h>
#include <asm/irq.h>
#ifdef CONFIG_HIGH_RES_TIMERS
#include <linux/hrtime.h>
# if defined(schedule_next_int)
/* Old high-res timer code, do translations. */
#  define get_arch_cycles(a) quick_update_jiffies_sub(a)
#  define arch_cycles_per_jiffy cycles_per_jiffies
# endif
static inline void add_usec_to_timer(struct timer_list *t, long v)
{
	t->arch_cycle_expires += nsec_to_arch_cycle(v * 1000);
	while (t->arch_cycle_expires >= arch_cycles_per_jiffy)
	{
		t->expires++;
		t->arch_cycle_expires -= arch_cycles_per_jiffy;
	}
}
#endif
#include <linux/interrupt.h>
#include <linux/rcupdate.h>
#include <linux/ipmi_smi.h>
#include <asm/io.h>
#include "ipmi_si_sm.h"
#include <linux/init.h>
#include <linux/dmi.h>

/* Measure times between events in the driver. */
#undef DEBUG_TIMING

/* Call every 10 ms. */
#define SI_TIMEOUT_TIME_USEC	10000
#define SI_USEC_PER_JIFFY	(1000000/HZ)
#define SI_TIMEOUT_JIFFIES	(SI_TIMEOUT_TIME_USEC/SI_USEC_PER_JIFFY)
#define SI_SHORT_TIMEOUT_USEC  250 /* .25ms when the SM request a
                                       short timeout */

enum si_intf_state {
	SI_NORMAL,
	SI_GETTING_FLAGS,
	SI_GETTING_EVENTS,
	SI_CLEARING_FLAGS,
	SI_CLEARING_FLAGS_THEN_SET_IRQ,
	SI_GETTING_MESSAGES,
	SI_ENABLE_INTERRUPTS1,
	SI_ENABLE_INTERRUPTS2
	/* FIXME - add watchdog stuff. */
};

/* Some BT-specific defines we need here. */
#define IPMI_BT_INTMASK_REG		2
#define IPMI_BT_INTMASK_CLEAR_IRQ_BIT	2
#define IPMI_BT_INTMASK_ENABLE_IRQ_BIT	1

enum si_type {
    SI_KCS, SI_SMIC, SI_BT
};

struct ipmi_device_id {
	unsigned char device_id;
	unsigned char device_revision;
	unsigned char firmware_revision_1;
	unsigned char firmware_revision_2;
	unsigned char ipmi_version;
	unsigned char additional_device_support;
	unsigned char manufacturer_id[3];
	unsigned char product_id[2];
	unsigned char aux_firmware_revision[4];
} __attribute__((packed));

#define ipmi_version_major(v) ((v)->ipmi_version & 0xf)
#define ipmi_version_minor(v) ((v)->ipmi_version >> 4)

struct smi_info
{
	int                    intf_num;
	ipmi_smi_t             intf;
	struct si_sm_data      *si_sm;
	struct si_sm_handlers  *handlers;
	enum si_type           si_type;
	spinlock_t             si_lock;
	spinlock_t             msg_lock;
	struct list_head       xmit_msgs;
	struct list_head       hp_xmit_msgs;
	struct ipmi_smi_msg    *curr_msg;
	enum si_intf_state     si_state;

	/* Used to handle the various types of I/O that can occur with
           IPMI */
	struct si_sm_io io;
	int (*io_setup)(struct smi_info *info);
	void (*io_cleanup)(struct smi_info *info);
	int (*irq_setup)(struct smi_info *info);
	void (*irq_cleanup)(struct smi_info *info);
	unsigned int io_size;

	/* Per-OEM handler, called from handle_flags().
	   Returns 1 when handle_flags() needs to be re-run
	   or 0 indicating it set si_state itself.
	*/
	int (*oem_data_avail_handler)(struct smi_info *smi_info);

	/* Flags from the last GET_MSG_FLAGS command, used when an ATTN
	   is set to hold the flags until we are done handling everything
	   from the flags. */
#define RECEIVE_MSG_AVAIL	0x01
#define EVENT_MSG_BUFFER_FULL	0x02
#define WDT_PRE_TIMEOUT_INT	0x08
#define OEM0_DATA_AVAIL     0x20
#define OEM1_DATA_AVAIL     0x40
#define OEM2_DATA_AVAIL     0x80
#define OEM_DATA_AVAIL      (OEM0_DATA_AVAIL | \
                             OEM1_DATA_AVAIL | \
                             OEM2_DATA_AVAIL)
	unsigned char       msg_flags;

	/* If set to true, this will request events the next time the
	   state machine is idle. */
	atomic_t            req_events;

	/* If true, run the state machine to completion on every send
	   call.  Generally used after a panic to make sure stuff goes
	   out. */
	int                 run_to_completion;

	/* The I/O port of an SI interface. */
	int                 port;

	/* The space between start addresses of the two ports.  For
	   instance, if the first port is 0xca2 and the spacing is 4, then
	   the second port is 0xca6. */
	unsigned int        spacing;

	/* zero if no irq; */
	int                 irq;

	/* The timer for this si. */
	struct timer_list   si_timer;

	/* The time (in jiffies) the last timeout occurred at. */
	unsigned long       last_timeout_jiffies;

	/* Used to gracefully stop the timer without race conditions. */
	atomic_t            stop_operation;

	/* The driver will disable interrupts when it gets into a
	   situation where it cannot handle messages due to lack of
	   memory.  Once that situation clears up, it will re-enable
	   interrupts. */
	int interrupt_disabled;

	struct ipmi_device_id device_id;

	/* Slave address, could be reported from DMI. */
	unsigned char slave_addr;

	/* Counters and things for the proc filesystem. */
	spinlock_t count_lock;
	unsigned long short_timeouts;
	unsigned long long_timeouts;
	unsigned long timeout_restarts;
	unsigned long idles;
	unsigned long interrupts;
	unsigned long attentions;
	unsigned long flag_fetches;
	unsigned long hosed_count;
	unsigned long complete_transactions;
	unsigned long events;
	unsigned long watchdog_pretimeouts;
	unsigned long incoming_messages;

        struct task_struct *thread;
};

static struct notifier_block *xaction_notifier_list;
static int register_xaction_notifier(struct notifier_block * nb)
{
	return notifier_chain_register(&xaction_notifier_list, nb);
}

static void si_restart_short_timer(struct smi_info *smi_info);

static void deliver_recv_msg(struct smi_info *smi_info,
			     struct ipmi_smi_msg *msg)
{
	/* Deliver the message to the upper layer with the lock
           released. */
	spin_unlock(&(smi_info->si_lock));
	ipmi_smi_msg_received(smi_info->intf, msg);
	spin_lock(&(smi_info->si_lock));
}

static void return_hosed_msg(struct smi_info *smi_info)
{
	struct ipmi_smi_msg *msg = smi_info->curr_msg;

	/* Make it a reponse */
	msg->rsp[0] = msg->data[0] | 4;
	msg->rsp[1] = msg->data[1];
	msg->rsp[2] = 0xFF; /* Unknown error. */
	msg->rsp_size = 3;

	smi_info->curr_msg = NULL;
	deliver_recv_msg(smi_info, msg);
}

static enum si_sm_result start_next_msg(struct smi_info *smi_info)
{
	int              rv;
	struct list_head *entry = NULL;
#ifdef DEBUG_TIMING
	struct timeval t;
#endif

	/* No need to save flags, we aleady have interrupts off and we
	   already hold the SMI lock. */
	spin_lock(&(smi_info->msg_lock));

	/* Pick the high priority queue first. */
	if (! list_empty(&(smi_info->hp_xmit_msgs))) {
		entry = smi_info->hp_xmit_msgs.next;
	} else if (! list_empty(&(smi_info->xmit_msgs))) {
		entry = smi_info->xmit_msgs.next;
	}

	if (! entry) {
		smi_info->curr_msg = NULL;
		rv = SI_SM_IDLE;
	} else {
		int err;

		list_del(entry);
		smi_info->curr_msg = list_entry(entry,
						struct ipmi_smi_msg,
						link);
#ifdef DEBUG_TIMING
		do_gettimeofday(&t);
		printk("**Start2: %d.%9.9d\n", t.tv_sec, t.tv_usec);
#endif
		err = notifier_call_chain(&xaction_notifier_list, 0, smi_info);
		if (err & NOTIFY_STOP_MASK) {
			rv = SI_SM_CALL_WITHOUT_DELAY;
			goto out;
		}
		err = smi_info->handlers->start_transaction(
			smi_info->si_sm,
			smi_info->curr_msg->data,
			smi_info->curr_msg->data_size);
		if (err) {
			return_hosed_msg(smi_info);
		}

		rv = SI_SM_CALL_WITHOUT_DELAY;
	}
	out:
	spin_unlock(&(smi_info->msg_lock));

	return rv;
}

static void start_enable_irq(struct smi_info *smi_info)
{
	unsigned char msg[2];

	/* If we are enabling interrupts, we have to tell the
	   BMC to use them. */
	msg[0] = (IPMI_NETFN_APP_REQUEST << 2);
	msg[1] = IPMI_GET_BMC_GLOBAL_ENABLES_CMD;

	smi_info->handlers->start_transaction(smi_info->si_sm, msg, 2);
	smi_info->si_state = SI_ENABLE_INTERRUPTS1;
}

static void start_clear_flags(struct smi_info *smi_info)
{
	unsigned char msg[3];

	/* Make sure the watchdog pre-timeout flag is not set at startup. */
	msg[0] = (IPMI_NETFN_APP_REQUEST << 2);
	msg[1] = IPMI_CLEAR_MSG_FLAGS_CMD;
	msg[2] = WDT_PRE_TIMEOUT_INT;

	smi_info->handlers->start_transaction(smi_info->si_sm, msg, 3);
	smi_info->si_state = SI_CLEARING_FLAGS;
}

/* When we have a situtaion where we run out of memory and cannot
   allocate messages, we just leave them in the BMC and run the system
   polled until we can allocate some memory.  Once we have some
   memory, we will re-enable the interrupt. */
static inline void disable_si_irq(struct smi_info *smi_info)
{
	if ((smi_info->irq) && (! smi_info->interrupt_disabled)) {
		disable_irq_nosync(smi_info->irq);
		smi_info->interrupt_disabled = 1;
	}
}

static inline void enable_si_irq(struct smi_info *smi_info)
{
	if ((smi_info->irq) && (smi_info->interrupt_disabled)) {
		enable_irq(smi_info->irq);
		smi_info->interrupt_disabled = 0;
	}
}

static void handle_flags(struct smi_info *smi_info)
{
 retry:
	if (smi_info->msg_flags & WDT_PRE_TIMEOUT_INT) {
		/* Watchdog pre-timeout */
		spin_lock(&smi_info->count_lock);
		smi_info->watchdog_pretimeouts++;
		spin_unlock(&smi_info->count_lock);

		start_clear_flags(smi_info);
		smi_info->msg_flags &= ~WDT_PRE_TIMEOUT_INT;
		spin_unlock(&(smi_info->si_lock));
		ipmi_smi_watchdog_pretimeout(smi_info->intf);
		spin_lock(&(smi_info->si_lock));
	} else if (smi_info->msg_flags & RECEIVE_MSG_AVAIL) {
		/* Messages available. */
		smi_info->curr_msg = ipmi_alloc_smi_msg();
		if (! smi_info->curr_msg) {
			disable_si_irq(smi_info);
			smi_info->si_state = SI_NORMAL;
			return;
		}
		enable_si_irq(smi_info);

		smi_info->curr_msg->data[0] = (IPMI_NETFN_APP_REQUEST << 2);
		smi_info->curr_msg->data[1] = IPMI_GET_MSG_CMD;
		smi_info->curr_msg->data_size = 2;

		smi_info->handlers->start_transaction(
			smi_info->si_sm,
			smi_info->curr_msg->data,
			smi_info->curr_msg->data_size);
		smi_info->si_state = SI_GETTING_MESSAGES;
	} else if (smi_info->msg_flags & EVENT_MSG_BUFFER_FULL) {
		/* Events available. */
		smi_info->curr_msg = ipmi_alloc_smi_msg();
		if (! smi_info->curr_msg) {
			disable_si_irq(smi_info);
			smi_info->si_state = SI_NORMAL;
			return;
		}
		enable_si_irq(smi_info);

		smi_info->curr_msg->data[0] = (IPMI_NETFN_APP_REQUEST << 2);
		smi_info->curr_msg->data[1] = IPMI_READ_EVENT_MSG_BUFFER_CMD;
		smi_info->curr_msg->data_size = 2;

		smi_info->handlers->start_transaction(
			smi_info->si_sm,
			smi_info->curr_msg->data,
			smi_info->curr_msg->data_size);
		smi_info->si_state = SI_GETTING_EVENTS;
	} else if (smi_info->msg_flags & OEM_DATA_AVAIL) {
		if (smi_info->oem_data_avail_handler)
			if (smi_info->oem_data_avail_handler(smi_info))
				goto retry;
	} else {
		smi_info->si_state = SI_NORMAL;
	}
}

static void handle_transaction_done(struct smi_info *smi_info)
{
	struct ipmi_smi_msg *msg;
#ifdef DEBUG_TIMING
	struct timeval t;

	do_gettimeofday(&t);
	printk("**Done: %d.%9.9d\n", t.tv_sec, t.tv_usec);
#endif
	switch (smi_info->si_state) {
	case SI_NORMAL:
		if (! smi_info->curr_msg)
			break;

		smi_info->curr_msg->rsp_size
			= smi_info->handlers->get_result(
				smi_info->si_sm,
				smi_info->curr_msg->rsp,
				IPMI_MAX_MSG_LENGTH);

		/* Do this here becase deliver_recv_msg() releases the
		   lock, and a new message can be put in during the
		   time the lock is released. */
		msg = smi_info->curr_msg;
		smi_info->curr_msg = NULL;
		deliver_recv_msg(smi_info, msg);
		break;

	case SI_GETTING_FLAGS:
	{
		unsigned char msg[4];
		unsigned int  len;

		/* We got the flags from the SMI, now handle them. */
		len = smi_info->handlers->get_result(smi_info->si_sm, msg, 4);
		if (msg[2] != 0) {
			/* Error fetching flags, just give up for
			   now. */
			smi_info->si_state = SI_NORMAL;
		} else if (len < 4) {
			/* Hmm, no flags.  That's technically illegal, but
			   don't use uninitialized data. */
			smi_info->si_state = SI_NORMAL;
		} else {
			smi_info->msg_flags = msg[3];
			handle_flags(smi_info);
		}
		break;
	}

	case SI_CLEARING_FLAGS:
	case SI_CLEARING_FLAGS_THEN_SET_IRQ:
	{
		unsigned char msg[3];

		/* We cleared the flags. */
		smi_info->handlers->get_result(smi_info->si_sm, msg, 3);
		if (msg[2] != 0) {
			/* Error clearing flags */
			printk(KERN_WARNING
			       "ipmi_si: Error clearing flags: %2.2x\n",
			       msg[2]);
		}
		if (smi_info->si_state == SI_CLEARING_FLAGS_THEN_SET_IRQ)
			start_enable_irq(smi_info);
		else
			smi_info->si_state = SI_NORMAL;
		break;
	}

	case SI_GETTING_EVENTS:
	{
		smi_info->curr_msg->rsp_size
			= smi_info->handlers->get_result(
				smi_info->si_sm,
				smi_info->curr_msg->rsp,
				IPMI_MAX_MSG_LENGTH);

		/* Do this here becase deliver_recv_msg() releases the
		   lock, and a new message can be put in during the
		   time the lock is released. */
		msg = smi_info->curr_msg;
		smi_info->curr_msg = NULL;
		if (msg->rsp[2] != 0) {
			/* Error getting event, probably done. */
			msg->done(msg);

			/* Take off the event flag. */
			smi_info->msg_flags &= ~EVENT_MSG_BUFFER_FULL;
			handle_flags(smi_info);
		} else {
			spin_lock(&smi_info->count_lock);
			smi_info->events++;
			spin_unlock(&smi_info->count_lock);

			/* Do this before we deliver the message
			   because delivering the message releases the
			   lock and something else can mess with the
			   state. */
			handle_flags(smi_info);

			deliver_recv_msg(smi_info, msg);
		}
		break;
	}

	case SI_GETTING_MESSAGES:
	{
		smi_info->curr_msg->rsp_size
			= smi_info->handlers->get_result(
				smi_info->si_sm,
				smi_info->curr_msg->rsp,
				IPMI_MAX_MSG_LENGTH);

		/* Do this here becase deliver_recv_msg() releases the
		   lock, and a new message can be put in during the
		   time the lock is released. */
		msg = smi_info->curr_msg;
		smi_info->curr_msg = NULL;
		if (msg->rsp[2] != 0) {
			/* Error getting event, probably done. */
			msg->done(msg);

			/* Take off the msg flag. */
			smi_info->msg_flags &= ~RECEIVE_MSG_AVAIL;
			handle_flags(smi_info);
		} else {
			spin_lock(&smi_info->count_lock);
			smi_info->incoming_messages++;
			spin_unlock(&smi_info->count_lock);

			/* Do this before we deliver the message
			   because delivering the message releases the
			   lock and something else can mess with the
			   state. */
			handle_flags(smi_info);

			deliver_recv_msg(smi_info, msg);
		}
		break;
	}

	case SI_ENABLE_INTERRUPTS1:
	{
		unsigned char msg[4];

		/* We got the flags from the SMI, now handle them. */
		smi_info->handlers->get_result(smi_info->si_sm, msg, 4);
		if (msg[2] != 0) {
			printk(KERN_WARNING
			       "ipmi_si: Could not enable interrupts"
			       ", failed get, using polled mode.\n");
			smi_info->si_state = SI_NORMAL;
		} else {
			msg[0] = (IPMI_NETFN_APP_REQUEST << 2);
			msg[1] = IPMI_SET_BMC_GLOBAL_ENABLES_CMD;
			msg[2] = msg[3] | 1; /* enable msg queue int */
			smi_info->handlers->start_transaction(
				smi_info->si_sm, msg, 3);
			smi_info->si_state = SI_ENABLE_INTERRUPTS2;
		}
		break;
	}

	case SI_ENABLE_INTERRUPTS2:
	{
		unsigned char msg[4];

		/* We got the flags from the SMI, now handle them. */
		smi_info->handlers->get_result(smi_info->si_sm, msg, 4);
		if (msg[2] != 0) {
			printk(KERN_WARNING
			       "ipmi_si: Could not enable interrupts"
			       ", failed set, using polled mode.\n");
		}
		smi_info->si_state = SI_NORMAL;
		break;
	}
	}
}

/* Called on timeouts and events.  Timeouts should pass the elapsed
   time, interrupts should pass in zero. */
static enum si_sm_result smi_event_handler(struct smi_info *smi_info,
					   int time)
{
	enum si_sm_result si_sm_result;

 restart:
	/* There used to be a loop here that waited a little while
	   (around 25us) before giving up.  That turned out to be
	   pointless, the minimum delays I was seeing were in the 300us
	   range, which is far too long to wait in an interrupt.  So
	   we just run until the state machine tells us something
	   happened or it needs a delay. */
	si_sm_result = smi_info->handlers->event(smi_info->si_sm, time);
	time = 0;
	while (si_sm_result == SI_SM_CALL_WITHOUT_DELAY)
	{
		si_sm_result = smi_info->handlers->event(smi_info->si_sm, 0);
	}

	if (si_sm_result == SI_SM_TRANSACTION_COMPLETE)
	{
		spin_lock(&smi_info->count_lock);
		smi_info->complete_transactions++;
		spin_unlock(&smi_info->count_lock);

		handle_transaction_done(smi_info);
		si_sm_result = smi_info->handlers->event(smi_info->si_sm, 0);
	}
	else if (si_sm_result == SI_SM_HOSED)
	{
		spin_lock(&smi_info->count_lock);
		smi_info->hosed_count++;
		spin_unlock(&smi_info->count_lock);

		/* Do the before return_hosed_msg, because that
		   releases the lock. */
		smi_info->si_state = SI_NORMAL;
		if (smi_info->curr_msg != NULL) {
			/* If we were handling a user message, format
                           a response to send to the upper layer to
                           tell it about the error. */
			return_hosed_msg(smi_info);
		}
		si_sm_result = smi_info->handlers->event(smi_info->si_sm, 0);
	}

	/* We prefer handling attn over new messages. */
	if (si_sm_result == SI_SM_ATTN)
	{
		unsigned char msg[2];

		spin_lock(&smi_info->count_lock);
		smi_info->attentions++;
		spin_unlock(&smi_info->count_lock);

		/* Got a attn, send down a get message flags to see
                   what's causing it.  It would be better to handle
                   this in the upper layer, but due to the way
                   interrupts work with the SMI, that's not really
                   possible. */
		msg[0] = (IPMI_NETFN_APP_REQUEST << 2);
		msg[1] = IPMI_GET_MSG_FLAGS_CMD;

		smi_info->handlers->start_transaction(
			smi_info->si_sm, msg, 2);
		smi_info->si_state = SI_GETTING_FLAGS;
		goto restart;
	}

	/* If we are currently idle, try to start the next message. */
	if (si_sm_result == SI_SM_IDLE) {
		spin_lock(&smi_info->count_lock);
		smi_info->idles++;
		spin_unlock(&smi_info->count_lock);

		si_sm_result = start_next_msg(smi_info);
		if (si_sm_result != SI_SM_IDLE)
			goto restart;
        }

	if ((si_sm_result == SI_SM_IDLE)
	    && (atomic_read(&smi_info->req_events)))
	{
		/* We are idle and the upper layer requested that I fetch
		   events, so do so. */
		unsigned char msg[2];

		spin_lock(&smi_info->count_lock);
		smi_info->flag_fetches++;
		spin_unlock(&smi_info->count_lock);

		atomic_set(&smi_info->req_events, 0);
		msg[0] = (IPMI_NETFN_APP_REQUEST << 2);
		msg[1] = IPMI_GET_MSG_FLAGS_CMD;

		smi_info->handlers->start_transaction(
			smi_info->si_sm, msg, 2);
		smi_info->si_state = SI_GETTING_FLAGS;
		goto restart;
	}

	return si_sm_result;
}

static void sender(void                *send_info,
		   struct ipmi_smi_msg *msg,
		   int                 priority)
{
	struct smi_info   *smi_info = send_info;
	enum si_sm_result result;
	unsigned long     flags;
#ifdef DEBUG_TIMING
	struct timeval    t;
#endif

	spin_lock_irqsave(&(smi_info->msg_lock), flags);
#ifdef DEBUG_TIMING
	do_gettimeofday(&t);
	printk("**Enqueue: %d.%9.9d\n", t.tv_sec, t.tv_usec);
#endif

	if (smi_info->run_to_completion) {
		/* If we are running to completion, then throw it in
		   the list and run transactions until everything is
		   clear.  Priority doesn't matter here. */
		list_add_tail(&(msg->link), &(smi_info->xmit_msgs));

		/* We have to release the msg lock and claim the smi
		   lock in this case, because of race conditions. */
		spin_unlock_irqrestore(&(smi_info->msg_lock), flags);

		spin_lock_irqsave(&(smi_info->si_lock), flags);
		result = smi_event_handler(smi_info, 0);
		while (result != SI_SM_IDLE) {
			udelay(SI_SHORT_TIMEOUT_USEC);
			result = smi_event_handler(smi_info,
						   SI_SHORT_TIMEOUT_USEC);
		}
		spin_unlock_irqrestore(&(smi_info->si_lock), flags);
		return;
	} else {
		if (priority > 0) {
			list_add_tail(&(msg->link), &(smi_info->hp_xmit_msgs));
		} else {
			list_add_tail(&(msg->link), &(smi_info->xmit_msgs));
		}
	}
	spin_unlock_irqrestore(&(smi_info->msg_lock), flags);

	spin_lock_irqsave(&(smi_info->si_lock), flags);
	if ((smi_info->si_state == SI_NORMAL)
	    && (smi_info->curr_msg == NULL))
	{
		start_next_msg(smi_info);
		si_restart_short_timer(smi_info);
	}
	spin_unlock_irqrestore(&(smi_info->si_lock), flags);
}

static void set_run_to_completion(void *send_info, int i_run_to_completion)
{
	struct smi_info   *smi_info = send_info;
	enum si_sm_result result;
	unsigned long     flags;

	spin_lock_irqsave(&(smi_info->si_lock), flags);

	smi_info->run_to_completion = i_run_to_completion;
	if (i_run_to_completion) {
		result = smi_event_handler(smi_info, 0);
		while (result != SI_SM_IDLE) {
			udelay(SI_SHORT_TIMEOUT_USEC);
			result = smi_event_handler(smi_info,
						   SI_SHORT_TIMEOUT_USEC);
		}
	}

	spin_unlock_irqrestore(&(smi_info->si_lock), flags);
}

static int ipmi_thread(void *data)
{
	struct smi_info *smi_info = data;
	unsigned long flags;
	enum si_sm_result smi_result;

	set_user_nice(current, 19);
	while (!kthread_should_stop()) {
		spin_lock_irqsave(&(smi_info->si_lock), flags);
		smi_result=smi_event_handler(smi_info, 0);
		spin_unlock_irqrestore(&(smi_info->si_lock), flags);
		if (smi_result == SI_SM_CALL_WITHOUT_DELAY) {
			/* do nothing */
		}
		else if (smi_result == SI_SM_CALL_WITH_DELAY)
			udelay(1);
		else
			schedule_timeout_interruptible(1);
	}
	return 0;
}


static void poll(void *send_info)
{
	struct smi_info *smi_info = send_info;

	smi_event_handler(smi_info, 0);
}

static void request_events(void *send_info)
{
	struct smi_info *smi_info = send_info;

	atomic_set(&smi_info->req_events, 1);
}

static int initialized = 0;

/* Must be called with interrupts off and with the si_lock held. */
static void si_restart_short_timer(struct smi_info *smi_info)
{
#if defined(CONFIG_HIGH_RES_TIMERS)
	unsigned long flags;
	unsigned long jiffies_now;
	unsigned long seq;

	if (del_timer(&(smi_info->si_timer))) {
		/* If we don't delete the timer, then it will go off
		   immediately, anyway.  So we only process if we
		   actually delete the timer. */

		do {
			seq = read_seqbegin_irqsave(&xtime_lock, flags);
			jiffies_now = jiffies;
			smi_info->si_timer.expires = jiffies_now;
			smi_info->si_timer.arch_cycle_expires
				= get_arch_cycles(jiffies_now);
		} while (read_seqretry_irqrestore(&xtime_lock, seq, flags));

		add_usec_to_timer(&smi_info->si_timer, SI_SHORT_TIMEOUT_USEC);

		add_timer(&(smi_info->si_timer));
		spin_lock_irqsave(&smi_info->count_lock, flags);
		smi_info->timeout_restarts++;
		spin_unlock_irqrestore(&smi_info->count_lock, flags);
	}
#endif
}

static void smi_timeout(unsigned long data)
{
	struct smi_info   *smi_info = (struct smi_info *) data;
	enum si_sm_result smi_result;
	unsigned long     flags;
	unsigned long     jiffies_now;
	long              time_diff;
#ifdef DEBUG_TIMING
	struct timeval    t;
#endif

	if (atomic_read(&smi_info->stop_operation))
		return;

	spin_lock_irqsave(&(smi_info->si_lock), flags);
#ifdef DEBUG_TIMING
	do_gettimeofday(&t);
	printk("**Timer: %d.%9.9d\n", t.tv_sec, t.tv_usec);
#endif
	jiffies_now = jiffies;
	time_diff = (((long)jiffies_now - (long)smi_info->last_timeout_jiffies)
		     * SI_USEC_PER_JIFFY);
	smi_result = smi_event_handler(smi_info, time_diff);

	spin_unlock_irqrestore(&(smi_info->si_lock), flags);

	smi_info->last_timeout_jiffies = jiffies_now;

	if ((smi_info->irq) && (! smi_info->interrupt_disabled)) {
		/* Running with interrupts, only do long timeouts. */
		smi_info->si_timer.expires = jiffies + SI_TIMEOUT_JIFFIES;
		spin_lock_irqsave(&smi_info->count_lock, flags);
		smi_info->long_timeouts++;
		spin_unlock_irqrestore(&smi_info->count_lock, flags);
		goto do_add_timer;
	}

	/* If the state machine asks for a short delay, then shorten
           the timer timeout. */
	if (smi_result == SI_SM_CALL_WITH_DELAY) {
#if defined(CONFIG_HIGH_RES_TIMERS)
		unsigned long seq;
#endif
		spin_lock_irqsave(&smi_info->count_lock, flags);
		smi_info->short_timeouts++;
		spin_unlock_irqrestore(&smi_info->count_lock, flags);
#if defined(CONFIG_HIGH_RES_TIMERS)
		do {
			seq = read_seqbegin_irqsave(&xtime_lock, flags);
			smi_info->si_timer.expires = jiffies;
			smi_info->si_timer.arch_cycle_expires
				= get_arch_cycles(smi_info->si_timer.expires);
		} while (read_seqretry_irqrestore(&xtime_lock, seq, flags));
		add_usec_to_timer(&smi_info->si_timer, SI_SHORT_TIMEOUT_USEC);
#else
		smi_info->si_timer.expires = jiffies + 1;
#endif
	} else {
		spin_lock_irqsave(&smi_info->count_lock, flags);
		smi_info->long_timeouts++;
		spin_unlock_irqrestore(&smi_info->count_lock, flags);
		smi_info->si_timer.expires = jiffies + SI_TIMEOUT_JIFFIES;
#if defined(CONFIG_HIGH_RES_TIMERS)
		smi_info->si_timer.arch_cycle_expires = 0;
#endif
	}

 do_add_timer:
	add_timer(&(smi_info->si_timer));
}

static irqreturn_t si_irq_handler(int irq, void *data, struct pt_regs *regs)
{
	struct smi_info *smi_info = data;
	unsigned long   flags;
#ifdef DEBUG_TIMING
	struct timeval  t;
#endif

	spin_lock_irqsave(&(smi_info->si_lock), flags);

	spin_lock(&smi_info->count_lock);
	smi_info->interrupts++;
	spin_unlock(&smi_info->count_lock);

	if (atomic_read(&smi_info->stop_operation))
		goto out;

#ifdef DEBUG_TIMING
	do_gettimeofday(&t);
	printk("**Interrupt: %d.%9.9d\n", t.tv_sec, t.tv_usec);
#endif
	smi_event_handler(smi_info, 0);
 out:
	spin_unlock_irqrestore(&(smi_info->si_lock), flags);
	return IRQ_HANDLED;
}

static irqreturn_t si_bt_irq_handler(int irq, void *data, struct pt_regs *regs)
{
	struct smi_info *smi_info = data;
	/* We need to clear the IRQ flag for the BT interface. */
	smi_info->io.outputb(&smi_info->io, IPMI_BT_INTMASK_REG,
			     IPMI_BT_INTMASK_CLEAR_IRQ_BIT
			     | IPMI_BT_INTMASK_ENABLE_IRQ_BIT);
	return si_irq_handler(irq, data, regs);
}


static struct ipmi_smi_handlers handlers =
{
	.owner                  = THIS_MODULE,
	.sender			= sender,
	.request_events		= request_events,
	.set_run_to_completion  = set_run_to_completion,
	.poll			= poll,
};

/* There can be 4 IO ports passed in (with or without IRQs), 4 addresses,
   a default IO port, and 1 ACPI/SPMI address.  That sets SI_MAX_DRIVERS */

#define SI_MAX_PARMS 4
#define SI_MAX_DRIVERS ((SI_MAX_PARMS * 2) + 2)
static struct smi_info *smi_infos[SI_MAX_DRIVERS] =
{ NULL, NULL, NULL, NULL };

#define DEVICE_NAME "ipmi_si"

#define DEFAULT_KCS_IO_PORT	0xca2
#define DEFAULT_SMIC_IO_PORT	0xca9
#define DEFAULT_BT_IO_PORT	0xe4
#define DEFAULT_REGSPACING	1

static int           si_trydefaults = 1;
static char          *si_type[SI_MAX_PARMS];
#define MAX_SI_TYPE_STR 30
static char          si_type_str[MAX_SI_TYPE_STR];
static unsigned long addrs[SI_MAX_PARMS];
static int num_addrs;
static unsigned int  ports[SI_MAX_PARMS];
static int num_ports;
static int           irqs[SI_MAX_PARMS];
static int num_irqs;
static int           regspacings[SI_MAX_PARMS];
static int num_regspacings = 0;
static int           regsizes[SI_MAX_PARMS];
static int num_regsizes = 0;
static int           regshifts[SI_MAX_PARMS];
static int num_regshifts = 0;
static int slave_addrs[SI_MAX_PARMS];
static int num_slave_addrs = 0;


module_param_named(trydefaults, si_trydefaults, bool, 0);
MODULE_PARM_DESC(trydefaults, "Setting this to 'false' will disable the"
		 " default scan of the KCS and SMIC interface at the standard"
		 " address");
module_param_string(type, si_type_str, MAX_SI_TYPE_STR, 0);
MODULE_PARM_DESC(type, "Defines the type of each interface, each"
		 " interface separated by commas.  The types are 'kcs',"
		 " 'smic', and 'bt'.  For example si_type=kcs,bt will set"
		 " the first interface to kcs and the second to bt");
module_param_array(addrs, long, &num_addrs, 0);
MODULE_PARM_DESC(addrs, "Sets the memory address of each interface, the"
		 " addresses separated by commas.  Only use if an interface"
		 " is in memory.  Otherwise, set it to zero or leave"
		 " it blank.");
module_param_array(ports, int, &num_ports, 0);
MODULE_PARM_DESC(ports, "Sets the port address of each interface, the"
		 " addresses separated by commas.  Only use if an interface"
		 " is a port.  Otherwise, set it to zero or leave"
		 " it blank.");
module_param_array(irqs, int, &num_irqs, 0);
MODULE_PARM_DESC(irqs, "Sets the interrupt of each interface, the"
		 " addresses separated by commas.  Only use if an interface"
		 " has an interrupt.  Otherwise, set it to zero or leave"
		 " it blank.");
module_param_array(regspacings, int, &num_regspacings, 0);
MODULE_PARM_DESC(regspacings, "The number of bytes between the start address"
		 " and each successive register used by the interface.  For"
		 " instance, if the start address is 0xca2 and the spacing"
		 " is 2, then the second address is at 0xca4.  Defaults"
		 " to 1.");
module_param_array(regsizes, int, &num_regsizes, 0);
MODULE_PARM_DESC(regsizes, "The size of the specific IPMI register in bytes."
		 " This should generally be 1, 2, 4, or 8 for an 8-bit,"
		 " 16-bit, 32-bit, or 64-bit register.  Use this if you"
		 " the 8-bit IPMI register has to be read from a larger"
		 " register.");
module_param_array(regshifts, int, &num_regshifts, 0);
MODULE_PARM_DESC(regshifts, "The amount to shift the data read from the."
		 " IPMI register, in bits.  For instance, if the data"
		 " is read from a 32-bit word and the IPMI data is in"
		 " bit 8-15, then the shift would be 8");
module_param_array(slave_addrs, int, &num_slave_addrs, 0);
MODULE_PARM_DESC(slave_addrs, "Set the default IPMB slave address for"
		 " the controller.  Normally this is 0x20, but can be"
		 " overridden by this parm.  This is an array indexed"
		 " by interface number.");


#define IPMI_MEM_ADDR_SPACE 1
#define IPMI_IO_ADDR_SPACE  2

#if defined(CONFIG_ACPI) || defined(CONFIG_DMI) || defined(CONFIG_PCI)
static int is_new_interface(int intf, u8 addr_space, unsigned long base_addr)
{
	int i;

	for (i = 0; i < SI_MAX_PARMS; ++i) {
		/* Don't check our address. */
		if (i == intf)
			continue;
		if (si_type[i] != NULL) {
			if ((addr_space == IPMI_MEM_ADDR_SPACE &&
			     base_addr == addrs[i]) ||
			    (addr_space == IPMI_IO_ADDR_SPACE &&
			     base_addr == ports[i]))
				return 0;
		}
		else
			break;
	}

	return 1;
}
#endif

static int std_irq_setup(struct smi_info *info)
{
	int rv;

	if (! info->irq)
		return 0;

	if (info->si_type == SI_BT) {
		rv = request_irq(info->irq,
				 si_bt_irq_handler,
				 SA_INTERRUPT,
				 DEVICE_NAME,
				 info);
		if (! rv)
			/* Enable the interrupt in the BT interface. */
			info->io.outputb(&info->io, IPMI_BT_INTMASK_REG,
					 IPMI_BT_INTMASK_ENABLE_IRQ_BIT);
	} else
		rv = request_irq(info->irq,
				 si_irq_handler,
				 SA_INTERRUPT,
				 DEVICE_NAME,
				 info);
	if (rv) {
		printk(KERN_WARNING
		       "ipmi_si: %s unable to claim interrupt %d,"
		       " running polled\n",
		       DEVICE_NAME, info->irq);
		info->irq = 0;
	} else {
		printk("  Using irq %d\n", info->irq);
	}

	return rv;
}

static void std_irq_cleanup(struct smi_info *info)
{
	if (! info->irq)
		return;

	if (info->si_type == SI_BT)
		/* Disable the interrupt in the BT interface. */
		info->io.outputb(&info->io, IPMI_BT_INTMASK_REG, 0);
	free_irq(info->irq, info);
}

static unsigned char port_inb(struct si_sm_io *io, unsigned int offset)
{
	unsigned int *addr = io->info;

	return inb((*addr)+(offset*io->regspacing));
}

static void port_outb(struct si_sm_io *io, unsigned int offset,
		      unsigned char b)
{
	unsigned int *addr = io->info;

	outb(b, (*addr)+(offset * io->regspacing));
}

static unsigned char port_inw(struct si_sm_io *io, unsigned int offset)
{
	unsigned int *addr = io->info;

	return (inw((*addr)+(offset * io->regspacing)) >> io->regshift) & 0xff;
}

static void port_outw(struct si_sm_io *io, unsigned int offset,
		      unsigned char b)
{
	unsigned int *addr = io->info;

	outw(b << io->regshift, (*addr)+(offset * io->regspacing));
}

static unsigned char port_inl(struct si_sm_io *io, unsigned int offset)
{
	unsigned int *addr = io->info;

	return (inl((*addr)+(offset * io->regspacing)) >> io->regshift) & 0xff;
}

static void port_outl(struct si_sm_io *io, unsigned int offset,
		      unsigned char b)
{
	unsigned int *addr = io->info;

	outl(b << io->regshift, (*addr)+(offset * io->regspacing));
}

static void port_cleanup(struct smi_info *info)
{
	unsigned int *addr = info->io.info;
	int           mapsize;

	if (addr && (*addr)) {
		mapsize = ((info->io_size * info->io.regspacing)
			   - (info->io.regspacing - info->io.regsize));

		release_region (*addr, mapsize);
	}
	kfree(info);
}

static int port_setup(struct smi_info *info)
{
	unsigned int *addr = info->io.info;
	int           mapsize;

	if (! addr || (! *addr))
		return -ENODEV;

	info->io_cleanup = port_cleanup;

	/* Figure out the actual inb/inw/inl/etc routine to use based
	   upon the register size. */
	switch (info->io.regsize) {
	case 1:
		info->io.inputb = port_inb;
		info->io.outputb = port_outb;
		break;
	case 2:
		info->io.inputb = port_inw;
		info->io.outputb = port_outw;
		break;
	case 4:
		info->io.inputb = port_inl;
		info->io.outputb = port_outl;
		break;
	default:
		printk("ipmi_si: Invalid register size: %d\n",
		       info->io.regsize);
		return -EINVAL;
	}

	/* Calculate the total amount of memory to claim.  This is an
	 * unusual looking calculation, but it avoids claiming any
	 * more memory than it has to.  It will claim everything
	 * between the first address to the end of the last full
	 * register. */
	mapsize = ((info->io_size * info->io.regspacing)
		   - (info->io.regspacing - info->io.regsize));

	if (request_region(*addr, mapsize, DEVICE_NAME) == NULL)
		return -EIO;
	return 0;
}

static int try_init_port(int intf_num, struct smi_info **new_info)
{
	struct smi_info *info;

	if (! ports[intf_num])
		return -ENODEV;

	if (! is_new_interface(intf_num, IPMI_IO_ADDR_SPACE,
			      ports[intf_num]))
		return -ENODEV;

	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (! info) {
		printk(KERN_ERR "ipmi_si: Could not allocate SI data (1)\n");
		return -ENOMEM;
	}
	memset(info, 0, sizeof(*info));

	info->io_setup = port_setup;
	info->io.info = &(ports[intf_num]);
	info->io.addr = NULL;
	info->io.regspacing = regspacings[intf_num];
	if (! info->io.regspacing)
		info->io.regspacing = DEFAULT_REGSPACING;
	info->io.regsize = regsizes[intf_num];
	if (! info->io.regsize)
		info->io.regsize = DEFAULT_REGSPACING;
	info->io.regshift = regshifts[intf_num];
	info->irq = 0;
	info->irq_setup = NULL;
	*new_info = info;

	if (si_type[intf_num] == NULL)
		si_type[intf_num] = "kcs";

	printk("ipmi_si: Trying \"%s\" at I/O port 0x%x\n",
	       si_type[intf_num], ports[intf_num]);
	return 0;
}

static unsigned char mem_inb(struct si_sm_io *io, unsigned int offset)
{
	return readb((io->addr)+(offset * io->regspacing));
}

static void mem_outb(struct si_sm_io *io, unsigned int offset,
		     unsigned char b)
{
	writeb(b, (io->addr)+(offset * io->regspacing));
}

static unsigned char mem_inw(struct si_sm_io *io, unsigned int offset)
{
	return (readw((io->addr)+(offset * io->regspacing)) >> io->regshift)
		&& 0xff;
}

static void mem_outw(struct si_sm_io *io, unsigned int offset,
		     unsigned char b)
{
	writeb(b << io->regshift, (io->addr)+(offset * io->regspacing));
}

static unsigned char mem_inl(struct si_sm_io *io, unsigned int offset)
{
	return (readl((io->addr)+(offset * io->regspacing)) >> io->regshift)
		&& 0xff;
}

static void mem_outl(struct si_sm_io *io, unsigned int offset,
		     unsigned char b)
{
	writel(b << io->regshift, (io->addr)+(offset * io->regspacing));
}

#ifdef readq
static unsigned char mem_inq(struct si_sm_io *io, unsigned int offset)
{
	return (readq((io->addr)+(offset * io->regspacing)) >> io->regshift)
		&& 0xff;
}

static void mem_outq(struct si_sm_io *io, unsigned int offset,
		     unsigned char b)
{
	writeq(b << io->regshift, (io->addr)+(offset * io->regspacing));
}
#endif

static void mem_cleanup(struct smi_info *info)
{
	unsigned long *addr = info->io.info;
	int           mapsize;

	if (info->io.addr) {
		iounmap(info->io.addr);

		mapsize = ((info->io_size * info->io.regspacing)
			   - (info->io.regspacing - info->io.regsize));

		release_mem_region(*addr, mapsize);
	}
	kfree(info);
}

static int mem_setup(struct smi_info *info)
{
	unsigned long *addr = info->io.info;
	int           mapsize;

	if (! addr || (! *addr))
		return -ENODEV;

	info->io_cleanup = mem_cleanup;

	/* Figure out the actual readb/readw/readl/etc routine to use based
	   upon the register size. */
	switch (info->io.regsize) {
	case 1:
		info->io.inputb = mem_inb;
		info->io.outputb = mem_outb;
		break;
	case 2:
		info->io.inputb = mem_inw;
		info->io.outputb = mem_outw;
		break;
	case 4:
		info->io.inputb = mem_inl;
		info->io.outputb = mem_outl;
		break;
#ifdef readq
	case 8:
		info->io.inputb = mem_inq;
		info->io.outputb = mem_outq;
		break;
#endif
	default:
		printk("ipmi_si: Invalid register size: %d\n",
		       info->io.regsize);
		return -EINVAL;
	}

	/* Calculate the total amount of memory to claim.  This is an
	 * unusual looking calculation, but it avoids claiming any
	 * more memory than it has to.  It will claim everything
	 * between the first address to the end of the last full
	 * register. */
	mapsize = ((info->io_size * info->io.regspacing)
		   - (info->io.regspacing - info->io.regsize));

	if (request_mem_region(*addr, mapsize, DEVICE_NAME) == NULL)
		return -EIO;

	info->io.addr = ioremap(*addr, mapsize);
	if (info->io.addr == NULL) {
		release_mem_region(*addr, mapsize);
		return -EIO;
	}
	return 0;
}

static int try_init_mem(int intf_num, struct smi_info **new_info)
{
	struct smi_info *info;

	if (! addrs[intf_num])
		return -ENODEV;

	if (! is_new_interface(intf_num, IPMI_MEM_ADDR_SPACE,
			      addrs[intf_num]))
		return -ENODEV;

	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (! info) {
		printk(KERN_ERR "ipmi_si: Could not allocate SI data (2)\n");
		return -ENOMEM;
	}
	memset(info, 0, sizeof(*info));

	info->io_setup = mem_setup;
	info->io.info = &addrs[intf_num];
	info->io.addr = NULL;
	info->io.regspacing = regspacings[intf_num];
	if (! info->io.regspacing)
		info->io.regspacing = DEFAULT_REGSPACING;
	info->io.regsize = regsizes[intf_num];
	if (! info->io.regsize)
		info->io.regsize = DEFAULT_REGSPACING;
	info->io.regshift = regshifts[intf_num];
	info->irq = 0;
	info->irq_setup = NULL;
	*new_info = info;

	if (si_type[intf_num] == NULL)
		si_type[intf_num] = "kcs";

	printk("ipmi_si: Trying \"%s\" at memory address 0x%lx\n",
	       si_type[intf_num], addrs[intf_num]);
	return 0;
}


#ifdef CONFIG_ACPI

#include <linux/acpi.h>

/* Once we get an ACPI failure, we don't try any more, because we go
   through the tables sequentially.  Once we don't find a table, there
   are no more. */
static int acpi_failure = 0;

/* For GPE-type interrupts. */
static u32 ipmi_acpi_gpe(void *context)
{
	struct smi_info *smi_info = context;
	unsigned long   flags;
#ifdef DEBUG_TIMING
	struct timeval t;
#endif

	spin_lock_irqsave(&(smi_info->si_lock), flags);

	spin_lock(&smi_info->count_lock);
	smi_info->interrupts++;
	spin_unlock(&smi_info->count_lock);

	if (atomic_read(&smi_info->stop_operation))
		goto out;

#ifdef DEBUG_TIMING
	do_gettimeofday(&t);
	printk("**ACPI_GPE: %d.%9.9d\n", t.tv_sec, t.tv_usec);
#endif
	smi_event_handler(smi_info, 0);
 out:
	spin_unlock_irqrestore(&(smi_info->si_lock), flags);

	return ACPI_INTERRUPT_HANDLED;
}

static int acpi_gpe_irq_setup(struct smi_info *info)
{
	acpi_status status;

	if (! info->irq)
		return 0;

	/* FIXME - is level triggered right? */
	status = acpi_install_gpe_handler(NULL,
					  info->irq,
					  ACPI_GPE_LEVEL_TRIGGERED,
					  &ipmi_acpi_gpe,
					  info);
	if (status != AE_OK) {
		printk(KERN_WARNING
		       "ipmi_si: %s unable to claim ACPI GPE %d,"
		       " running polled\n",
		       DEVICE_NAME, info->irq);
		info->irq = 0;
		return -EINVAL;
	} else {
		printk("  Using ACPI GPE %d\n", info->irq);
		return 0;
	}
}

static void acpi_gpe_irq_cleanup(struct smi_info *info)
{
	if (! info->irq)
		return;

	acpi_remove_gpe_handler(NULL, info->irq, &ipmi_acpi_gpe);
}

/*
 * Defined at
 * http://h21007.www2.hp.com/dspp/files/unprotected/devresource/Docs/TechPapers/IA64/hpspmi.pdf
 */
struct SPMITable {
	s8	Signature[4];
	u32	Length;
	u8	Revision;
	u8	Checksum;
	s8	OEMID[6];
	s8	OEMTableID[8];
	s8	OEMRevision[4];
	s8	CreatorID[4];
	s8	CreatorRevision[4];
	u8	InterfaceType;
	u8	IPMIlegacy;
	s16	SpecificationRevision;

	/*
	 * Bit 0 - SCI interrupt supported
	 * Bit 1 - I/O APIC/SAPIC
	 */
	u8	InterruptType;

	/* If bit 0 of InterruptType is set, then this is the SCI
           interrupt in the GPEx_STS register. */
	u8	GPE;

	s16	Reserved;

	/* If bit 1 of InterruptType is set, then this is the I/O
           APIC/SAPIC interrupt. */
	u32	GlobalSystemInterrupt;

	/* The actual register address. */
	struct acpi_generic_address addr;

	u8	UID[4];

	s8      spmi_id[1]; /* A '\0' terminated array starts here. */
};

static int try_init_acpi(int intf_num, struct smi_info **new_info)
{
	struct smi_info  *info;
	acpi_status      status;
	struct SPMITable *spmi;
	char             *io_type;
	u8 		 addr_space;

	if (acpi_disabled)
		return -ENODEV;

	if (acpi_failure)
		return -ENODEV;

	status = acpi_get_firmware_table("SPMI", intf_num+1,
					 ACPI_LOGICAL_ADDRESSING,
					 (struct acpi_table_header **) &spmi);
	if (status != AE_OK) {
		acpi_failure = 1;
		return -ENODEV;
	}

	if (spmi->IPMIlegacy != 1) {
	    printk(KERN_INFO "IPMI: Bad SPMI legacy %d\n", spmi->IPMIlegacy);
  	    return -ENODEV;
	}

	if (spmi->addr.address_space_id == ACPI_ADR_SPACE_SYSTEM_MEMORY)
		addr_space = IPMI_MEM_ADDR_SPACE;
	else
		addr_space = IPMI_IO_ADDR_SPACE;
	if (! is_new_interface(-1, addr_space, spmi->addr.address))
		return -ENODEV;

	if (! spmi->addr.register_bit_width) {
		acpi_failure = 1;
		return -ENODEV;
	}

	/* Figure out the interface type. */
	switch (spmi->InterfaceType)
	{
	case 1:	/* KCS */
		si_type[intf_num] = "kcs";
		break;

	case 2:	/* SMIC */
		si_type[intf_num] = "smic";
		break;

	case 3:	/* BT */
		si_type[intf_num] = "bt";
		break;

	default:
		printk(KERN_INFO "ipmi_si: Unknown ACPI/SPMI SI type %d\n",
			spmi->InterfaceType);
		return -EIO;
	}

	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (! info) {
		printk(KERN_ERR "ipmi_si: Could not allocate SI data (3)\n");
		return -ENOMEM;
	}
	memset(info, 0, sizeof(*info));

	if (spmi->InterruptType & 1) {
		/* We've got a GPE interrupt. */
		info->irq = spmi->GPE;
		info->irq_setup = acpi_gpe_irq_setup;
		info->irq_cleanup = acpi_gpe_irq_cleanup;
	} else if (spmi->InterruptType & 2) {
		/* We've got an APIC/SAPIC interrupt. */
		info->irq = spmi->GlobalSystemInterrupt;
		info->irq_setup = std_irq_setup;
		info->irq_cleanup = std_irq_cleanup;
	} else {
		/* Use the default interrupt setting. */
		info->irq = 0;
		info->irq_setup = NULL;
	}

	if (spmi->addr.register_bit_width) {
		/* A (hopefully) properly formed register bit width. */
		regspacings[intf_num] = spmi->addr.register_bit_width / 8;
		info->io.regspacing = spmi->addr.register_bit_width / 8;
	} else {
		/* Some broken systems get this wrong and set the value
		 * to zero.  Assume it is the default spacing.  If that
		 * is wrong, too bad, the vendor should fix the tables. */
		regspacings[intf_num] = DEFAULT_REGSPACING;
		info->io.regspacing = DEFAULT_REGSPACING;
	}
	regsizes[intf_num] = regspacings[intf_num];
	info->io.regsize = regsizes[intf_num];
	regshifts[intf_num] = spmi->addr.register_bit_offset;
	info->io.regshift = regshifts[intf_num];

	if (spmi->addr.address_space_id == ACPI_ADR_SPACE_SYSTEM_MEMORY) {
		io_type = "memory";
		info->io_setup = mem_setup;
		addrs[intf_num] = spmi->addr.address;
		info->io.info = &(addrs[intf_num]);
	} else if (spmi->addr.address_space_id == ACPI_ADR_SPACE_SYSTEM_IO) {
		io_type = "I/O";
		info->io_setup = port_setup;
		ports[intf_num] = spmi->addr.address;
		info->io.info = &(ports[intf_num]);
	} else {
		kfree(info);
		printk("ipmi_si: Unknown ACPI I/O Address type\n");
		return -EIO;
	}

	*new_info = info;

	printk("ipmi_si: ACPI/SPMI specifies \"%s\" %s SI @ 0x%lx\n",
	       si_type[intf_num], io_type, (unsigned long) spmi->addr.address);
	return 0;
}
#endif

#ifdef CONFIG_DMI
typedef struct dmi_ipmi_data
{
	u8   		type;
	u8   		addr_space;
	unsigned long	base_addr;
	u8   		irq;
	u8              offset;
	u8              slave_addr;
} dmi_ipmi_data_t;

static dmi_ipmi_data_t dmi_data[SI_MAX_DRIVERS];
static int dmi_data_entries;

static int __init decode_dmi(struct dmi_header *dm, int intf_num)
{
	u8              *data = (u8 *)dm;
	unsigned long  	base_addr;
	u8		reg_spacing;
	u8              len = dm->length;
	dmi_ipmi_data_t *ipmi_data = dmi_data+intf_num;

	ipmi_data->type = data[4];

	memcpy(&base_addr, data+8, sizeof(unsigned long));
	if (len >= 0x11) {
		if (base_addr & 1) {
			/* I/O */
			base_addr &= 0xFFFE;
			ipmi_data->addr_space = IPMI_IO_ADDR_SPACE;
		}
		else {
			/* Memory */
			ipmi_data->addr_space = IPMI_MEM_ADDR_SPACE;
		}
		/* If bit 4 of byte 0x10 is set, then the lsb for the address
		   is odd. */
		ipmi_data->base_addr = base_addr | ((data[0x10] & 0x10) >> 4);

		ipmi_data->irq = data[0x11];

		/* The top two bits of byte 0x10 hold the register spacing. */
		reg_spacing = (data[0x10] & 0xC0) >> 6;
		switch(reg_spacing){
		case 0x00: /* Byte boundaries */
		    ipmi_data->offset = 1;
		    break;
		case 0x01: /* 32-bit boundaries */
		    ipmi_data->offset = 4;
		    break;
		case 0x02: /* 16-byte boundaries */
		    ipmi_data->offset = 16;
		    break;
		default:
		    /* Some other interface, just ignore it. */
		    return -EIO;
		}
	} else {
		/* Old DMI spec. */
		/* Note that technically, the lower bit of the base
		 * address should be 1 if the address is I/O and 0 if
		 * the address is in memory.  So many systems get that
		 * wrong (and all that I have seen are I/O) so we just
		 * ignore that bit and assume I/O.  Systems that use
		 * memory should use the newer spec, anyway. */
		ipmi_data->base_addr = base_addr & 0xfffe;
		ipmi_data->addr_space = IPMI_IO_ADDR_SPACE;
		ipmi_data->offset = 1;
	}

	ipmi_data->slave_addr = data[6];

	if (is_new_interface(-1, ipmi_data->addr_space,ipmi_data->base_addr)) {
		dmi_data_entries++;
		return 0;
	}

	memset(ipmi_data, 0, sizeof(dmi_ipmi_data_t));

	return -1;
}

static void __init dmi_find_bmc(void)
{
	struct dmi_device *dev = NULL;
	int               intf_num = 0;

	while ((dev = dmi_find_device(DMI_DEV_TYPE_IPMI, NULL, dev))) {
		if (intf_num >= SI_MAX_DRIVERS)
			break;

		decode_dmi((struct dmi_header *) dev->device_data, intf_num++);
	}
}

static int try_init_smbios(int intf_num, struct smi_info **new_info)
{
	struct smi_info *info;
	dmi_ipmi_data_t *ipmi_data = dmi_data+intf_num;
	char            *io_type;

	if (intf_num >= dmi_data_entries)
		return -ENODEV;

	switch (ipmi_data->type) {
		case 0x01: /* KCS */
			si_type[intf_num] = "kcs";
			break;
		case 0x02: /* SMIC */
			si_type[intf_num] = "smic";
			break;
		case 0x03: /* BT */
			si_type[intf_num] = "bt";
			break;
		default:
			return -EIO;
	}

	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (! info) {
		printk(KERN_ERR "ipmi_si: Could not allocate SI data (4)\n");
		return -ENOMEM;
	}
	memset(info, 0, sizeof(*info));

	if (ipmi_data->addr_space == 1) {
		io_type = "memory";
		info->io_setup = mem_setup;
		addrs[intf_num] = ipmi_data->base_addr;
		info->io.info = &(addrs[intf_num]);
	} else if (ipmi_data->addr_space == 2) {
		io_type = "I/O";
		info->io_setup = port_setup;
		ports[intf_num] = ipmi_data->base_addr;
		info->io.info = &(ports[intf_num]);
	} else {
		kfree(info);
		printk("ipmi_si: Unknown SMBIOS I/O Address type.\n");
		return -EIO;
	}

	regspacings[intf_num] = ipmi_data->offset;
	info->io.regspacing = regspacings[intf_num];
	if (! info->io.regspacing)
		info->io.regspacing = DEFAULT_REGSPACING;
	info->io.regsize = DEFAULT_REGSPACING;
	info->io.regshift = regshifts[intf_num];

	info->slave_addr = ipmi_data->slave_addr;

	irqs[intf_num] = ipmi_data->irq;

	*new_info = info;

	printk("ipmi_si: Found SMBIOS-specified state machine at %s"
	       " address 0x%lx, slave address 0x%x\n",
	       io_type, (unsigned long)ipmi_data->base_addr,
	       ipmi_data->slave_addr);
	return 0;
}
#endif /* CONFIG_DMI */

#ifdef CONFIG_PCI

#define PCI_ERMC_CLASSCODE  0x0C0700
#define PCI_HP_VENDOR_ID    0x103C
#define PCI_MMC_DEVICE_ID   0x121A
#define PCI_MMC_ADDR_CW     0x10

/* Avoid more than one attempt to probe pci smic. */
static int pci_smic_checked = 0;

static int find_pci_smic(int intf_num, struct smi_info **new_info)
{
	struct smi_info  *info;
	int              error;
	struct pci_dev   *pci_dev = NULL;
	u16    		 base_addr;
	int              fe_rmc = 0;

	if (pci_smic_checked)
		return -ENODEV;

	pci_smic_checked = 1;

	pci_dev = pci_get_device(PCI_HP_VENDOR_ID, PCI_MMC_DEVICE_ID, NULL);
	if (! pci_dev) {
		pci_dev = pci_get_class(PCI_ERMC_CLASSCODE, NULL);
		if (pci_dev && (pci_dev->subsystem_vendor == PCI_HP_VENDOR_ID))
			fe_rmc = 1;
		else
			return -ENODEV;
	}

	error = pci_read_config_word(pci_dev, PCI_MMC_ADDR_CW, &base_addr);
	if (error)
	{
		pci_dev_put(pci_dev);
		printk(KERN_ERR
		       "ipmi_si: pci_read_config_word() failed (%d).\n",
		       error);
		return -ENODEV;
	}

	/* Bit 0: 1 specifies programmed I/O, 0 specifies memory mapped I/O */
	if (! (base_addr & 0x0001))
	{
		pci_dev_put(pci_dev);
		printk(KERN_ERR
		       "ipmi_si: memory mapped I/O not supported for PCI"
		       " smic.\n");
		return -ENODEV;
	}

	base_addr &= 0xFFFE;
	if (! fe_rmc)
		/* Data register starts at base address + 1 in eRMC */
		++base_addr;

	if (! is_new_interface(-1, IPMI_IO_ADDR_SPACE, base_addr)) {
		pci_dev_put(pci_dev);
		return -ENODEV;
	}

	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (! info) {
		pci_dev_put(pci_dev);
		printk(KERN_ERR "ipmi_si: Could not allocate SI data (5)\n");
		return -ENOMEM;
	}
	memset(info, 0, sizeof(*info));

	info->io_setup = port_setup;
	ports[intf_num] = base_addr;
	info->io.info = &(ports[intf_num]);
	info->io.regspacing = regspacings[intf_num];
	if (! info->io.regspacing)
		info->io.regspacing = DEFAULT_REGSPACING;
	info->io.regsize = DEFAULT_REGSPACING;
	info->io.regshift = regshifts[intf_num];

	*new_info = info;

	irqs[intf_num] = pci_dev->irq;
	si_type[intf_num] = "smic";

	printk("ipmi_si: Found PCI SMIC at I/O address 0x%lx\n",
		(long unsigned int) base_addr);

	pci_dev_put(pci_dev);
	return 0;
}
#endif /* CONFIG_PCI */

static int try_init_plug_and_play(int intf_num, struct smi_info **new_info)
{
#ifdef CONFIG_PCI
	if (find_pci_smic(intf_num, new_info) == 0)
		return 0;
#endif
	/* Include other methods here. */

	return -ENODEV;
}


static int try_get_dev_id(struct smi_info *smi_info)
{
	unsigned char      msg[2];
	unsigned char      *resp;
	unsigned long      resp_len;
	enum si_sm_result smi_result;
	int               rv = 0;

	resp = kmalloc(IPMI_MAX_MSG_LENGTH, GFP_KERNEL);
	if (! resp)
		return -ENOMEM;

	/* Do a Get Device ID command, since it comes back with some
	   useful info. */
	msg[0] = IPMI_NETFN_APP_REQUEST << 2;
	msg[1] = IPMI_GET_DEVICE_ID_CMD;
	smi_info->handlers->start_transaction(smi_info->si_sm, msg, 2);

	smi_result = smi_info->handlers->event(smi_info->si_sm, 0);
	for (;;)
	{
		if (smi_result == SI_SM_CALL_WITH_DELAY ||
		    smi_result == SI_SM_CALL_WITH_TICK_DELAY) {
			schedule_timeout_uninterruptible(1);
			smi_result = smi_info->handlers->event(
				smi_info->si_sm, 100);
		}
		else if (smi_result == SI_SM_CALL_WITHOUT_DELAY)
		{
			smi_result = smi_info->handlers->event(
				smi_info->si_sm, 0);
		}
		else
			break;
	}
	if (smi_result == SI_SM_HOSED) {
		/* We couldn't get the state machine to run, so whatever's at
		   the port is probably not an IPMI SMI interface. */
		rv = -ENODEV;
		goto out;
	}

	/* Otherwise, we got some data. */
	resp_len = smi_info->handlers->get_result(smi_info->si_sm,
						  resp, IPMI_MAX_MSG_LENGTH);
	if (resp_len < 6) {
		/* That's odd, it should be longer. */
		rv = -EINVAL;
		goto out;
	}

	if ((resp[1] != IPMI_GET_DEVICE_ID_CMD) || (resp[2] != 0)) {
		/* That's odd, it shouldn't be able to fail. */
		rv = -EINVAL;
		goto out;
	}

	/* Record info from the get device id, in case we need it. */
	memcpy(&smi_info->device_id, &resp[3],
	       min_t(unsigned long, resp_len-3, sizeof(smi_info->device_id)));

 out:
	kfree(resp);
	return rv;
}

static int type_file_read_proc(char *page, char **start, off_t off,
			       int count, int *eof, void *data)
{
	char            *out = (char *) page;
	struct smi_info *smi = data;

	switch (smi->si_type) {
	    case SI_KCS:
		return sprintf(out, "kcs\n");
	    case SI_SMIC:
		return sprintf(out, "smic\n");
	    case SI_BT:
		return sprintf(out, "bt\n");
	    default:
		return 0;
	}
}

static int stat_file_read_proc(char *page, char **start, off_t off,
			       int count, int *eof, void *data)
{
	char            *out = (char *) page;
	struct smi_info *smi = data;

	out += sprintf(out, "interrupts_enabled:    %d\n",
		       smi->irq && ! smi->interrupt_disabled);
	out += sprintf(out, "short_timeouts:        %ld\n",
		       smi->short_timeouts);
	out += sprintf(out, "long_timeouts:         %ld\n",
		       smi->long_timeouts);
	out += sprintf(out, "timeout_restarts:      %ld\n",
		       smi->timeout_restarts);
	out += sprintf(out, "idles:                 %ld\n",
		       smi->idles);
	out += sprintf(out, "interrupts:            %ld\n",
		       smi->interrupts);
	out += sprintf(out, "attentions:            %ld\n",
		       smi->attentions);
	out += sprintf(out, "flag_fetches:          %ld\n",
		       smi->flag_fetches);
	out += sprintf(out, "hosed_count:           %ld\n",
		       smi->hosed_count);
	out += sprintf(out, "complete_transactions: %ld\n",
		       smi->complete_transactions);
	out += sprintf(out, "events:                %ld\n",
		       smi->events);
	out += sprintf(out, "watchdog_pretimeouts:  %ld\n",
		       smi->watchdog_pretimeouts);
	out += sprintf(out, "incoming_messages:     %ld\n",
		       smi->incoming_messages);

	return (out - ((char *) page));
}

/*
 * oem_data_avail_to_receive_msg_avail
 * @info - smi_info structure with msg_flags set
 *
 * Converts flags from OEM_DATA_AVAIL to RECEIVE_MSG_AVAIL
 * Returns 1 indicating need to re-run handle_flags().
 */
static int oem_data_avail_to_receive_msg_avail(struct smi_info *smi_info)
{
	smi_info->msg_flags = ((smi_info->msg_flags & ~OEM_DATA_AVAIL) |
			      	RECEIVE_MSG_AVAIL);
	return 1;
}

/*
 * setup_dell_poweredge_oem_data_handler
 * @info - smi_info.device_id must be populated
 *
 * Systems that match, but have firmware version < 1.40 may assert
 * OEM0_DATA_AVAIL on their own, without being told via Set Flags that
 * it's safe to do so.  Such systems will de-assert OEM1_DATA_AVAIL
 * upon receipt of IPMI_GET_MSG_CMD, so we should treat these flags
 * as RECEIVE_MSG_AVAIL instead.
 *
 * As Dell has no plans to release IPMI 1.5 firmware that *ever*
 * assert the OEM[012] bits, and if it did, the driver would have to
 * change to handle that properly, we don't actually check for the
 * firmware version.
 * Device ID = 0x20                BMC on PowerEdge 8G servers
 * Device Revision = 0x80
 * Firmware Revision1 = 0x01       BMC version 1.40
 * Firmware Revision2 = 0x40       BCD encoded
 * IPMI Version = 0x51             IPMI 1.5
 * Manufacturer ID = A2 02 00      Dell IANA
 *
 * Additionally, PowerEdge systems with IPMI < 1.5 may also assert
 * OEM0_DATA_AVAIL and needs to be treated as RECEIVE_MSG_AVAIL.
 *
 */
#define DELL_POWEREDGE_8G_BMC_DEVICE_ID  0x20
#define DELL_POWEREDGE_8G_BMC_DEVICE_REV 0x80
#define DELL_POWEREDGE_8G_BMC_IPMI_VERSION 0x51
#define DELL_IANA_MFR_ID {0xA2, 0x02, 0x00}
static void setup_dell_poweredge_oem_data_handler(struct smi_info *smi_info)
{
	struct ipmi_device_id *id = &smi_info->device_id;
	const char mfr[3]=DELL_IANA_MFR_ID;
	if (! memcmp(mfr, id->manufacturer_id, sizeof(mfr))) {
		if (id->device_id       == DELL_POWEREDGE_8G_BMC_DEVICE_ID  &&
		    id->device_revision == DELL_POWEREDGE_8G_BMC_DEVICE_REV &&
		    id->ipmi_version    == DELL_POWEREDGE_8G_BMC_IPMI_VERSION) {
			smi_info->oem_data_avail_handler =
				oem_data_avail_to_receive_msg_avail;
		}
		else if (ipmi_version_major(id) < 1 ||
			 (ipmi_version_major(id) == 1 &&
			  ipmi_version_minor(id) < 5)) {
			smi_info->oem_data_avail_handler =
				oem_data_avail_to_receive_msg_avail;
		}
	}
}

#define CANNOT_RETURN_REQUESTED_LENGTH 0xCA
static void return_hosed_msg_badsize(struct smi_info *smi_info)
{
	struct ipmi_smi_msg *msg = smi_info->curr_msg;

	/* Make it a reponse */
	msg->rsp[0] = msg->data[0] | 4;
	msg->rsp[1] = msg->data[1];
	msg->rsp[2] = CANNOT_RETURN_REQUESTED_LENGTH;
	msg->rsp_size = 3;
	smi_info->curr_msg = NULL;
	deliver_recv_msg(smi_info, msg);
}

/*
 * dell_poweredge_bt_xaction_handler
 * @info - smi_info.device_id must be populated
 *
 * Dell PowerEdge servers with the BT interface (x6xx and 1750) will
 * not respond to a Get SDR command if the length of the data
 * requested is exactly 0x3A, which leads to command timeouts and no
 * data returned.  This intercepts such commands, and causes userspace
 * callers to try again with a different-sized buffer, which succeeds.
 */

#define STORAGE_NETFN 0x0A
#define STORAGE_CMD_GET_SDR 0x23
static int dell_poweredge_bt_xaction_handler(struct notifier_block *self,
					     unsigned long unused,
					     void *in)
{
	struct smi_info *smi_info = in;
	unsigned char *data = smi_info->curr_msg->data;
	unsigned int size   = smi_info->curr_msg->data_size;
	if (size >= 8 &&
	    (data[0]>>2) == STORAGE_NETFN &&
	    data[1] == STORAGE_CMD_GET_SDR &&
	    data[7] == 0x3A) {
		return_hosed_msg_badsize(smi_info);
		return NOTIFY_STOP;
	}
	return NOTIFY_DONE;
}

static struct notifier_block dell_poweredge_bt_xaction_notifier = {
	.notifier_call	= dell_poweredge_bt_xaction_handler,
};

/*
 * setup_dell_poweredge_bt_xaction_handler
 * @info - smi_info.device_id must be filled in already
 *
 * Fills in smi_info.device_id.start_transaction_pre_hook
 * when we know what function to use there.
 */
static void
setup_dell_poweredge_bt_xaction_handler(struct smi_info *smi_info)
{
	struct ipmi_device_id *id = &smi_info->device_id;
	const char mfr[3]=DELL_IANA_MFR_ID;
 	if (! memcmp(mfr, id->manufacturer_id, sizeof(mfr)) &&
	    smi_info->si_type == SI_BT)
		register_xaction_notifier(&dell_poweredge_bt_xaction_notifier);
}

/*
 * setup_oem_data_handler
 * @info - smi_info.device_id must be filled in already
 *
 * Fills in smi_info.device_id.oem_data_available_handler
 * when we know what function to use there.
 */

static void setup_oem_data_handler(struct smi_info *smi_info)
{
	setup_dell_poweredge_oem_data_handler(smi_info);
}

static void setup_xaction_handlers(struct smi_info *smi_info)
{
	setup_dell_poweredge_bt_xaction_handler(smi_info);
}

static inline void wait_for_timer_and_thread(struct smi_info *smi_info)
{
	if (smi_info->thread != NULL && smi_info->thread != ERR_PTR(-ENOMEM))
		kthread_stop(smi_info->thread);
	del_timer_sync(&smi_info->si_timer);
}

/* Returns 0 if initialized, or negative on an error. */
static int init_one_smi(int intf_num, struct smi_info **smi)
{
	int		rv;
	struct smi_info *new_smi;


	rv = try_init_mem(intf_num, &new_smi);
	if (rv)
		rv = try_init_port(intf_num, &new_smi);
#ifdef CONFIG_ACPI
	if (rv && si_trydefaults)
		rv = try_init_acpi(intf_num, &new_smi);
#endif
#ifdef CONFIG_DMI
	if (rv && si_trydefaults)
		rv = try_init_smbios(intf_num, &new_smi);
#endif
	if (rv && si_trydefaults)
		rv = try_init_plug_and_play(intf_num, &new_smi);

	if (rv)
		return rv;

	/* So we know not to free it unless we have allocated one. */
	new_smi->intf = NULL;
	new_smi->si_sm = NULL;
	new_smi->handlers = NULL;

	if (! new_smi->irq_setup) {
		new_smi->irq = irqs[intf_num];
		new_smi->irq_setup = std_irq_setup;
		new_smi->irq_cleanup = std_irq_cleanup;
	}

	/* Default to KCS if no type is specified. */
	if (si_type[intf_num] == NULL) {
		if (si_trydefaults)
			si_type[intf_num] = "kcs";
		else {
			rv = -EINVAL;
			goto out_err;
		}
	}

	/* Set up the state machine to use. */
	if (strcmp(si_type[intf_num], "kcs") == 0) {
		new_smi->handlers = &kcs_smi_handlers;
		new_smi->si_type = SI_KCS;
	} else if (strcmp(si_type[intf_num], "smic") == 0) {
		new_smi->handlers = &smic_smi_handlers;
		new_smi->si_type = SI_SMIC;
	} else if (strcmp(si_type[intf_num], "bt") == 0) {
		new_smi->handlers = &bt_smi_handlers;
		new_smi->si_type = SI_BT;
	} else {
		/* No support for anything else yet. */
		rv = -EIO;
		goto out_err;
	}

	/* Allocate the state machine's data and initialize it. */
	new_smi->si_sm = kmalloc(new_smi->handlers->size(), GFP_KERNEL);
	if (! new_smi->si_sm) {
		printk(" Could not allocate state machine memory\n");
		rv = -ENOMEM;
		goto out_err;
	}
	new_smi->io_size = new_smi->handlers->init_data(new_smi->si_sm,
							&new_smi->io);

	/* Now that we know the I/O size, we can set up the I/O. */
	rv = new_smi->io_setup(new_smi);
	if (rv) {
		printk(" Could not set up I/O space\n");
		goto out_err;
	}

	spin_lock_init(&(new_smi->si_lock));
	spin_lock_init(&(new_smi->msg_lock));
	spin_lock_init(&(new_smi->count_lock));

	/* Do low-level detection first. */
	if (new_smi->handlers->detect(new_smi->si_sm)) {
		rv = -ENODEV;
		goto out_err;
	}

	/* Attempt a get device id command.  If it fails, we probably
           don't have a SMI here. */
	rv = try_get_dev_id(new_smi);
	if (rv)
		goto out_err;

	setup_oem_data_handler(new_smi);
	setup_xaction_handlers(new_smi);

	/* Try to claim any interrupts. */
	new_smi->irq_setup(new_smi);

	INIT_LIST_HEAD(&(new_smi->xmit_msgs));
	INIT_LIST_HEAD(&(new_smi->hp_xmit_msgs));
	new_smi->curr_msg = NULL;
	atomic_set(&new_smi->req_events, 0);
	new_smi->run_to_completion = 0;

	new_smi->interrupt_disabled = 0;
	atomic_set(&new_smi->stop_operation, 0);
	new_smi->intf_num = intf_num;

	/* Start clearing the flags before we enable interrupts or the
	   timer to avoid racing with the timer. */
	start_clear_flags(new_smi);
	/* IRQ is defined to be set when non-zero. */
	if (new_smi->irq)
		new_smi->si_state = SI_CLEARING_FLAGS_THEN_SET_IRQ;

	/* The ipmi_register_smi() code does some operations to
	   determine the channel information, so we must be ready to
	   handle operations before it is called.  This means we have
	   to stop the timer if we get an error after this point. */
	init_timer(&(new_smi->si_timer));
	new_smi->si_timer.data = (long) new_smi;
	new_smi->si_timer.function = smi_timeout;
	new_smi->last_timeout_jiffies = jiffies;
	new_smi->si_timer.expires = jiffies + SI_TIMEOUT_JIFFIES;

	add_timer(&(new_smi->si_timer));
 	if (new_smi->si_type != SI_BT)
		new_smi->thread = kthread_run(ipmi_thread, new_smi,
					      "kipmi%d", new_smi->intf_num);

	rv = ipmi_register_smi(&handlers,
			       new_smi,
			       ipmi_version_major(&new_smi->device_id),
			       ipmi_version_minor(&new_smi->device_id),
			       new_smi->slave_addr,
			       &(new_smi->intf));
	if (rv) {
		printk(KERN_ERR
		       "ipmi_si: Unable to register device: error %d\n",
		       rv);
		goto out_err_stop_timer;
	}

	rv = ipmi_smi_add_proc_entry(new_smi->intf, "type",
				     type_file_read_proc, NULL,
				     new_smi, THIS_MODULE);
	if (rv) {
		printk(KERN_ERR
		       "ipmi_si: Unable to create proc entry: %d\n",
		       rv);
		goto out_err_stop_timer;
	}

	rv = ipmi_smi_add_proc_entry(new_smi->intf, "si_stats",
				     stat_file_read_proc, NULL,
				     new_smi, THIS_MODULE);
	if (rv) {
		printk(KERN_ERR
		       "ipmi_si: Unable to create proc entry: %d\n",
		       rv);
		goto out_err_stop_timer;
	}

	*smi = new_smi;

	printk(" IPMI %s interface initialized\n", si_type[intf_num]);

	return 0;

 out_err_stop_timer:
	atomic_inc(&new_smi->stop_operation);
	wait_for_timer_and_thread(new_smi);

 out_err:
	if (new_smi->intf)
		ipmi_unregister_smi(new_smi->intf);

	new_smi->irq_cleanup(new_smi);

	/* Wait until we know that we are out of any interrupt
	   handlers might have been running before we freed the
	   interrupt. */
	synchronize_sched();

	if (new_smi->si_sm) {
		if (new_smi->handlers)
			new_smi->handlers->cleanup(new_smi->si_sm);
		kfree(new_smi->si_sm);
	}
	if (new_smi->io_cleanup)
		new_smi->io_cleanup(new_smi);

	return rv;
}

static __init int init_ipmi_si(void)
{
	int  rv = 0;
	int  pos = 0;
	int  i;
	char *str;

	if (initialized)
		return 0;
	initialized = 1;

	/* Parse out the si_type string into its components. */
	str = si_type_str;
	if (*str != '\0') {
		for (i = 0; (i < SI_MAX_PARMS) && (*str != '\0'); i++) {
			si_type[i] = str;
			str = strchr(str, ',');
			if (str) {
				*str = '\0';
				str++;
			} else {
				break;
			}
		}
	}

	printk(KERN_INFO "IPMI System Interface driver.\n");

#ifdef CONFIG_DMI
	dmi_find_bmc();
#endif

	rv = init_one_smi(0, &(smi_infos[pos]));
	if (rv && ! ports[0] && si_trydefaults) {
		/* If we are trying defaults and the initial port is
                   not set, then set it. */
		si_type[0] = "kcs";
		ports[0] = DEFAULT_KCS_IO_PORT;
		rv = init_one_smi(0, &(smi_infos[pos]));
		if (rv) {
			/* No KCS - try SMIC */
			si_type[0] = "smic";
			ports[0] = DEFAULT_SMIC_IO_PORT;
			rv = init_one_smi(0, &(smi_infos[pos]));
		}
		if (rv) {
			/* No SMIC - try BT */
			si_type[0] = "bt";
			ports[0] = DEFAULT_BT_IO_PORT;
			rv = init_one_smi(0, &(smi_infos[pos]));
		}
	}
	if (rv == 0)
		pos++;

	for (i = 1; i < SI_MAX_PARMS; i++) {
		rv = init_one_smi(i, &(smi_infos[pos]));
		if (rv == 0)
			pos++;
	}

	if (smi_infos[0] == NULL) {
		printk("ipmi_si: Unable to find any System Interface(s)\n");
		return -ENODEV;
	}

	return 0;
}
module_init(init_ipmi_si);

static void __exit cleanup_one_si(struct smi_info *to_clean)
{
	int           rv;
	unsigned long flags;

	if (! to_clean)
		return;

	/* Tell the timer and interrupt handlers that we are shutting
	   down. */
	spin_lock_irqsave(&(to_clean->si_lock), flags);
	spin_lock(&(to_clean->msg_lock));

	atomic_inc(&to_clean->stop_operation);
	to_clean->irq_cleanup(to_clean);

	spin_unlock(&(to_clean->msg_lock));
	spin_unlock_irqrestore(&(to_clean->si_lock), flags);

	/* Wait until we know that we are out of any interrupt
	   handlers might have been running before we freed the
	   interrupt. */
	synchronize_sched();

	wait_for_timer_and_thread(to_clean);

	/* Interrupts and timeouts are stopped, now make sure the
	   interface is in a clean state. */
	while (to_clean->curr_msg || (to_clean->si_state != SI_NORMAL)) {
		poll(to_clean);
		schedule_timeout_uninterruptible(1);
	}

	rv = ipmi_unregister_smi(to_clean->intf);
	if (rv) {
		printk(KERN_ERR
		       "ipmi_si: Unable to unregister device: errno=%d\n",
		       rv);
	}

	to_clean->handlers->cleanup(to_clean->si_sm);

	kfree(to_clean->si_sm);

	if (to_clean->io_cleanup)
		to_clean->io_cleanup(to_clean);
}

static __exit void cleanup_ipmi_si(void)
{
	int i;

	if (! initialized)
		return;

	for (i = 0; i < SI_MAX_DRIVERS; i++) {
		cleanup_one_si(smi_infos[i]);
	}
}
module_exit(cleanup_ipmi_si);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Corey Minyard <minyard@mvista.com>");
MODULE_DESCRIPTION("Interface to the IPMI driver for the KCS, SMIC, and BT system interfaces.");
