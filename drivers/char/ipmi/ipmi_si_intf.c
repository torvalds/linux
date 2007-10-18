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
 * Copyright 2006 IBM Corp., Christian Krafft <krafft@de.ibm.com>
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
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <asm/irq.h>
#include <linux/interrupt.h>
#include <linux/rcupdate.h>
#include <linux/ipmi_smi.h>
#include <asm/io.h>
#include "ipmi_si_sm.h"
#include <linux/init.h>
#include <linux/dmi.h>
#include <linux/string.h>
#include <linux/ctype.h>

#ifdef CONFIG_PPC_OF
#include <asm/of_device.h>
#include <asm/of_platform.h>
#endif

#define PFX "ipmi_si: "

/* Measure times between events in the driver. */
#undef DEBUG_TIMING

/* Call every 10 ms. */
#define SI_TIMEOUT_TIME_USEC	10000
#define SI_USEC_PER_JIFFY	(1000000/HZ)
#define SI_TIMEOUT_JIFFIES	(SI_TIMEOUT_TIME_USEC/SI_USEC_PER_JIFFY)
#define SI_SHORT_TIMEOUT_USEC  250 /* .25ms when the SM request a
                                       short timeout */

/* Bit for BMC global enables. */
#define IPMI_BMC_RCV_MSG_INTR     0x01
#define IPMI_BMC_EVT_MSG_INTR     0x02
#define IPMI_BMC_EVT_MSG_BUFF     0x04
#define IPMI_BMC_SYS_LOG          0x08

enum si_intf_state {
	SI_NORMAL,
	SI_GETTING_FLAGS,
	SI_GETTING_EVENTS,
	SI_CLEARING_FLAGS,
	SI_CLEARING_FLAGS_THEN_SET_IRQ,
	SI_GETTING_MESSAGES,
	SI_ENABLE_INTERRUPTS1,
	SI_ENABLE_INTERRUPTS2,
	SI_DISABLE_INTERRUPTS1,
	SI_DISABLE_INTERRUPTS2
	/* FIXME - add watchdog stuff. */
};

/* Some BT-specific defines we need here. */
#define IPMI_BT_INTMASK_REG		2
#define IPMI_BT_INTMASK_CLEAR_IRQ_BIT	2
#define IPMI_BT_INTMASK_ENABLE_IRQ_BIT	1

enum si_type {
    SI_KCS, SI_SMIC, SI_BT
};
static char *si_to_str[] = { "kcs", "smic", "bt" };

#define DEVICE_NAME "ipmi_si"

static struct device_driver ipmi_driver =
{
	.name = DEVICE_NAME,
	.bus = &platform_bus_type
};

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
	char *addr_source; /* ACPI, PCI, SMBIOS, hardcode, default. */
	void (*addr_source_cleanup)(struct smi_info *info);
	void *addr_source_data;

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

	/* From the get device id response... */
	struct ipmi_device_id device_id;

	/* Driver model stuff. */
	struct device *dev;
	struct platform_device *pdev;

	 /* True if we allocated the device, false if it came from
	  * someplace else (like PCI). */
	int dev_registered;

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

	struct list_head link;
};

#define SI_MAX_PARMS 4

static int force_kipmid[SI_MAX_PARMS];
static int num_force_kipmid;

static int unload_when_empty = 1;

static int try_smi_init(struct smi_info *smi);
static void cleanup_one_si(struct smi_info *to_clean);

static ATOMIC_NOTIFIER_HEAD(xaction_notifier_list);
static int register_xaction_notifier(struct notifier_block * nb)
{
	return atomic_notifier_chain_register(&xaction_notifier_list, nb);
}

static void deliver_recv_msg(struct smi_info *smi_info,
			     struct ipmi_smi_msg *msg)
{
	/* Deliver the message to the upper layer with the lock
           released. */
	spin_unlock(&(smi_info->si_lock));
	ipmi_smi_msg_received(smi_info->intf, msg);
	spin_lock(&(smi_info->si_lock));
}

static void return_hosed_msg(struct smi_info *smi_info, int cCode)
{
	struct ipmi_smi_msg *msg = smi_info->curr_msg;

	if (cCode < 0 || cCode > IPMI_ERR_UNSPECIFIED)
		cCode = IPMI_ERR_UNSPECIFIED;
	/* else use it as is */

	/* Make it a reponse */
	msg->rsp[0] = msg->data[0] | 4;
	msg->rsp[1] = msg->data[1];
	msg->rsp[2] = cCode;
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
	if (!list_empty(&(smi_info->hp_xmit_msgs))) {
		entry = smi_info->hp_xmit_msgs.next;
	} else if (!list_empty(&(smi_info->xmit_msgs))) {
		entry = smi_info->xmit_msgs.next;
	}

	if (!entry) {
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
		err = atomic_notifier_call_chain(&xaction_notifier_list,
				0, smi_info);
		if (err & NOTIFY_STOP_MASK) {
			rv = SI_SM_CALL_WITHOUT_DELAY;
			goto out;
		}
		err = smi_info->handlers->start_transaction(
			smi_info->si_sm,
			smi_info->curr_msg->data,
			smi_info->curr_msg->data_size);
		if (err) {
			return_hosed_msg(smi_info, err);
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

static void start_disable_irq(struct smi_info *smi_info)
{
	unsigned char msg[2];

	msg[0] = (IPMI_NETFN_APP_REQUEST << 2);
	msg[1] = IPMI_GET_BMC_GLOBAL_ENABLES_CMD;

	smi_info->handlers->start_transaction(smi_info->si_sm, msg, 2);
	smi_info->si_state = SI_DISABLE_INTERRUPTS1;
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
	if ((smi_info->irq) && (!smi_info->interrupt_disabled)) {
		start_disable_irq(smi_info);
		smi_info->interrupt_disabled = 1;
	}
}

static inline void enable_si_irq(struct smi_info *smi_info)
{
	if ((smi_info->irq) && (smi_info->interrupt_disabled)) {
		start_enable_irq(smi_info);
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
		if (!smi_info->curr_msg) {
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
		if (!smi_info->curr_msg) {
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
	} else if (smi_info->msg_flags & OEM_DATA_AVAIL &&
	           smi_info->oem_data_avail_handler) {
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
		if (!smi_info->curr_msg)
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
			msg[2] = (msg[3] |
				  IPMI_BMC_RCV_MSG_INTR |
				  IPMI_BMC_EVT_MSG_INTR);
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

	case SI_DISABLE_INTERRUPTS1:
	{
		unsigned char msg[4];

		/* We got the flags from the SMI, now handle them. */
		smi_info->handlers->get_result(smi_info->si_sm, msg, 4);
		if (msg[2] != 0) {
			printk(KERN_WARNING
			       "ipmi_si: Could not disable interrupts"
			       ", failed get.\n");
			smi_info->si_state = SI_NORMAL;
		} else {
			msg[0] = (IPMI_NETFN_APP_REQUEST << 2);
			msg[1] = IPMI_SET_BMC_GLOBAL_ENABLES_CMD;
			msg[2] = (msg[3] &
				  ~(IPMI_BMC_RCV_MSG_INTR |
				    IPMI_BMC_EVT_MSG_INTR));
			smi_info->handlers->start_transaction(
				smi_info->si_sm, msg, 3);
			smi_info->si_state = SI_DISABLE_INTERRUPTS2;
		}
		break;
	}

	case SI_DISABLE_INTERRUPTS2:
	{
		unsigned char msg[4];

		/* We got the flags from the SMI, now handle them. */
		smi_info->handlers->get_result(smi_info->si_sm, msg, 4);
		if (msg[2] != 0) {
			printk(KERN_WARNING
			       "ipmi_si: Could not disable interrupts"
			       ", failed set.\n");
		}
		smi_info->si_state = SI_NORMAL;
		break;
	}
	}
}

/* Called on timeouts and events.  Timeouts should pass the elapsed
   time, interrupts should pass in zero.  Must be called with
   si_lock held and interrupts disabled. */
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
			return_hosed_msg(smi_info, IPMI_ERR_UNSPECIFIED);
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
		atomic_set(&smi_info->req_events, 0);

		smi_info->curr_msg = ipmi_alloc_smi_msg();
		if (!smi_info->curr_msg)
			goto out;

		smi_info->curr_msg->data[0] = (IPMI_NETFN_APP_REQUEST << 2);
		smi_info->curr_msg->data[1] = IPMI_READ_EVENT_MSG_BUFFER_CMD;
		smi_info->curr_msg->data_size = 2;

		smi_info->handlers->start_transaction(
			smi_info->si_sm,
			smi_info->curr_msg->data,
			smi_info->curr_msg->data_size);
		smi_info->si_state = SI_GETTING_EVENTS;
		goto restart;
	}
 out:
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

	if (atomic_read(&smi_info->stop_operation)) {
		msg->rsp[0] = msg->data[0] | 4;
		msg->rsp[1] = msg->data[1];
		msg->rsp[2] = IPMI_ERR_UNSPECIFIED;
		msg->rsp_size = 3;
		deliver_recv_msg(smi_info, msg);
		return;
	}

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
		smi_result = smi_event_handler(smi_info, 0);
		spin_unlock_irqrestore(&(smi_info->si_lock), flags);
		if (smi_result == SI_SM_CALL_WITHOUT_DELAY) {
			/* do nothing */
		}
		else if (smi_result == SI_SM_CALL_WITH_DELAY)
			schedule();
		else
			schedule_timeout_interruptible(1);
	}
	return 0;
}


static void poll(void *send_info)
{
	struct smi_info *smi_info = send_info;
	unsigned long flags;

	/*
	 * Make sure there is some delay in the poll loop so we can
	 * drive time forward and timeout things.
	 */
	udelay(10);
	spin_lock_irqsave(&smi_info->si_lock, flags);
	smi_event_handler(smi_info, 10);
	spin_unlock_irqrestore(&smi_info->si_lock, flags);
}

static void request_events(void *send_info)
{
	struct smi_info *smi_info = send_info;

	if (atomic_read(&smi_info->stop_operation))
		return;

	atomic_set(&smi_info->req_events, 1);
}

static int initialized;

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

	if ((smi_info->irq) && (!smi_info->interrupt_disabled)) {
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
		spin_lock_irqsave(&smi_info->count_lock, flags);
		smi_info->short_timeouts++;
		spin_unlock_irqrestore(&smi_info->count_lock, flags);
		smi_info->si_timer.expires = jiffies + 1;
	} else {
		spin_lock_irqsave(&smi_info->count_lock, flags);
		smi_info->long_timeouts++;
		spin_unlock_irqrestore(&smi_info->count_lock, flags);
		smi_info->si_timer.expires = jiffies + SI_TIMEOUT_JIFFIES;
	}

 do_add_timer:
	add_timer(&(smi_info->si_timer));
}

static irqreturn_t si_irq_handler(int irq, void *data)
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

#ifdef DEBUG_TIMING
	do_gettimeofday(&t);
	printk("**Interrupt: %d.%9.9d\n", t.tv_sec, t.tv_usec);
#endif
	smi_event_handler(smi_info, 0);
	spin_unlock_irqrestore(&(smi_info->si_lock), flags);
	return IRQ_HANDLED;
}

static irqreturn_t si_bt_irq_handler(int irq, void *data)
{
	struct smi_info *smi_info = data;
	/* We need to clear the IRQ flag for the BT interface. */
	smi_info->io.outputb(&smi_info->io, IPMI_BT_INTMASK_REG,
			     IPMI_BT_INTMASK_CLEAR_IRQ_BIT
			     | IPMI_BT_INTMASK_ENABLE_IRQ_BIT);
	return si_irq_handler(irq, data);
}

static int smi_start_processing(void       *send_info,
				ipmi_smi_t intf)
{
	struct smi_info *new_smi = send_info;
	int             enable = 0;

	new_smi->intf = intf;

	/* Try to claim any interrupts. */
	if (new_smi->irq_setup)
		new_smi->irq_setup(new_smi);

	/* Set up the timer that drives the interface. */
	setup_timer(&new_smi->si_timer, smi_timeout, (long)new_smi);
	new_smi->last_timeout_jiffies = jiffies;
	mod_timer(&new_smi->si_timer, jiffies + SI_TIMEOUT_JIFFIES);

	/*
	 * Check if the user forcefully enabled the daemon.
	 */
	if (new_smi->intf_num < num_force_kipmid)
		enable = force_kipmid[new_smi->intf_num];
	/*
	 * The BT interface is efficient enough to not need a thread,
	 * and there is no need for a thread if we have interrupts.
	 */
 	else if ((new_smi->si_type != SI_BT) && (!new_smi->irq))
		enable = 1;

	if (enable) {
		new_smi->thread = kthread_run(ipmi_thread, new_smi,
					      "kipmi%d", new_smi->intf_num);
		if (IS_ERR(new_smi->thread)) {
			printk(KERN_NOTICE "ipmi_si_intf: Could not start"
			       " kernel thread due to error %ld, only using"
			       " timers to drive the interface\n",
			       PTR_ERR(new_smi->thread));
			new_smi->thread = NULL;
		}
	}

	return 0;
}

static void set_maintenance_mode(void *send_info, int enable)
{
	struct smi_info   *smi_info = send_info;

	if (!enable)
		atomic_set(&smi_info->req_events, 0);
}

static struct ipmi_smi_handlers handlers =
{
	.owner                  = THIS_MODULE,
	.start_processing       = smi_start_processing,
	.sender			= sender,
	.request_events		= request_events,
	.set_maintenance_mode   = set_maintenance_mode,
	.set_run_to_completion  = set_run_to_completion,
	.poll			= poll,
};

/* There can be 4 IO ports passed in (with or without IRQs), 4 addresses,
   a default IO port, and 1 ACPI/SPMI address.  That sets SI_MAX_DRIVERS */

static LIST_HEAD(smi_infos);
static DEFINE_MUTEX(smi_infos_lock);
static int smi_num; /* Used to sequence the SMIs */

#define DEFAULT_REGSPACING	1
#define DEFAULT_REGSIZE		1

static int           si_trydefaults = 1;
static char          *si_type[SI_MAX_PARMS];
#define MAX_SI_TYPE_STR 30
static char          si_type_str[MAX_SI_TYPE_STR];
static unsigned long addrs[SI_MAX_PARMS];
static unsigned int num_addrs;
static unsigned int  ports[SI_MAX_PARMS];
static unsigned int num_ports;
static int           irqs[SI_MAX_PARMS];
static unsigned int num_irqs;
static int           regspacings[SI_MAX_PARMS];
static unsigned int num_regspacings;
static int           regsizes[SI_MAX_PARMS];
static unsigned int num_regsizes;
static int           regshifts[SI_MAX_PARMS];
static unsigned int num_regshifts;
static int slave_addrs[SI_MAX_PARMS];
static unsigned int num_slave_addrs;

#define IPMI_IO_ADDR_SPACE  0
#define IPMI_MEM_ADDR_SPACE 1
static char *addr_space_to_str[] = { "i/o", "mem" };

static int hotmod_handler(const char *val, struct kernel_param *kp);

module_param_call(hotmod, hotmod_handler, NULL, NULL, 0200);
MODULE_PARM_DESC(hotmod, "Add and remove interfaces.  See"
		 " Documentation/IPMI.txt in the kernel sources for the"
		 " gory details.");

module_param_named(trydefaults, si_trydefaults, bool, 0);
MODULE_PARM_DESC(trydefaults, "Setting this to 'false' will disable the"
		 " default scan of the KCS and SMIC interface at the standard"
		 " address");
module_param_string(type, si_type_str, MAX_SI_TYPE_STR, 0);
MODULE_PARM_DESC(type, "Defines the type of each interface, each"
		 " interface separated by commas.  The types are 'kcs',"
		 " 'smic', and 'bt'.  For example si_type=kcs,bt will set"
		 " the first interface to kcs and the second to bt");
module_param_array(addrs, ulong, &num_addrs, 0);
MODULE_PARM_DESC(addrs, "Sets the memory address of each interface, the"
		 " addresses separated by commas.  Only use if an interface"
		 " is in memory.  Otherwise, set it to zero or leave"
		 " it blank.");
module_param_array(ports, uint, &num_ports, 0);
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
module_param_array(force_kipmid, int, &num_force_kipmid, 0);
MODULE_PARM_DESC(force_kipmid, "Force the kipmi daemon to be enabled (1) or"
		 " disabled(0).  Normally the IPMI driver auto-detects"
		 " this, but the value may be overridden by this parm.");
module_param(unload_when_empty, int, 0);
MODULE_PARM_DESC(unload_when_empty, "Unload the module if no interfaces are"
		 " specified or found, default is 1.  Setting to 0"
		 " is useful for hot add of devices using hotmod.");


static void std_irq_cleanup(struct smi_info *info)
{
	if (info->si_type == SI_BT)
		/* Disable the interrupt in the BT interface. */
		info->io.outputb(&info->io, IPMI_BT_INTMASK_REG, 0);
	free_irq(info->irq, info);
}

static int std_irq_setup(struct smi_info *info)
{
	int rv;

	if (!info->irq)
		return 0;

	if (info->si_type == SI_BT) {
		rv = request_irq(info->irq,
				 si_bt_irq_handler,
				 IRQF_SHARED | IRQF_DISABLED,
				 DEVICE_NAME,
				 info);
		if (!rv)
			/* Enable the interrupt in the BT interface. */
			info->io.outputb(&info->io, IPMI_BT_INTMASK_REG,
					 IPMI_BT_INTMASK_ENABLE_IRQ_BIT);
	} else
		rv = request_irq(info->irq,
				 si_irq_handler,
				 IRQF_SHARED | IRQF_DISABLED,
				 DEVICE_NAME,
				 info);
	if (rv) {
		printk(KERN_WARNING
		       "ipmi_si: %s unable to claim interrupt %d,"
		       " running polled\n",
		       DEVICE_NAME, info->irq);
		info->irq = 0;
	} else {
		info->irq_cleanup = std_irq_cleanup;
		printk("  Using irq %d\n", info->irq);
	}

	return rv;
}

static unsigned char port_inb(struct si_sm_io *io, unsigned int offset)
{
	unsigned int addr = io->addr_data;

	return inb(addr + (offset * io->regspacing));
}

static void port_outb(struct si_sm_io *io, unsigned int offset,
		      unsigned char b)
{
	unsigned int addr = io->addr_data;

	outb(b, addr + (offset * io->regspacing));
}

static unsigned char port_inw(struct si_sm_io *io, unsigned int offset)
{
	unsigned int addr = io->addr_data;

	return (inw(addr + (offset * io->regspacing)) >> io->regshift) & 0xff;
}

static void port_outw(struct si_sm_io *io, unsigned int offset,
		      unsigned char b)
{
	unsigned int addr = io->addr_data;

	outw(b << io->regshift, addr + (offset * io->regspacing));
}

static unsigned char port_inl(struct si_sm_io *io, unsigned int offset)
{
	unsigned int addr = io->addr_data;

	return (inl(addr + (offset * io->regspacing)) >> io->regshift) & 0xff;
}

static void port_outl(struct si_sm_io *io, unsigned int offset,
		      unsigned char b)
{
	unsigned int addr = io->addr_data;

	outl(b << io->regshift, addr+(offset * io->regspacing));
}

static void port_cleanup(struct smi_info *info)
{
	unsigned int addr = info->io.addr_data;
	int          idx;

	if (addr) {
	  	for (idx = 0; idx < info->io_size; idx++) {
			release_region(addr + idx * info->io.regspacing,
				       info->io.regsize);
		}
	}
}

static int port_setup(struct smi_info *info)
{
	unsigned int addr = info->io.addr_data;
	int          idx;

	if (!addr)
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

	/* Some BIOSes reserve disjoint I/O regions in their ACPI
	 * tables.  This causes problems when trying to register the
	 * entire I/O region.  Therefore we must register each I/O
	 * port separately.
	 */
  	for (idx = 0; idx < info->io_size; idx++) {
		if (request_region(addr + idx * info->io.regspacing,
				   info->io.regsize, DEVICE_NAME) == NULL) {
			/* Undo allocations */
			while (idx--) {
				release_region(addr + idx * info->io.regspacing,
					       info->io.regsize);
			}
			return -EIO;
		}
	}
	return 0;
}

static unsigned char intf_mem_inb(struct si_sm_io *io, unsigned int offset)
{
	return readb((io->addr)+(offset * io->regspacing));
}

static void intf_mem_outb(struct si_sm_io *io, unsigned int offset,
		     unsigned char b)
{
	writeb(b, (io->addr)+(offset * io->regspacing));
}

static unsigned char intf_mem_inw(struct si_sm_io *io, unsigned int offset)
{
	return (readw((io->addr)+(offset * io->regspacing)) >> io->regshift)
		& 0xff;
}

static void intf_mem_outw(struct si_sm_io *io, unsigned int offset,
		     unsigned char b)
{
	writeb(b << io->regshift, (io->addr)+(offset * io->regspacing));
}

static unsigned char intf_mem_inl(struct si_sm_io *io, unsigned int offset)
{
	return (readl((io->addr)+(offset * io->regspacing)) >> io->regshift)
		& 0xff;
}

static void intf_mem_outl(struct si_sm_io *io, unsigned int offset,
		     unsigned char b)
{
	writel(b << io->regshift, (io->addr)+(offset * io->regspacing));
}

#ifdef readq
static unsigned char mem_inq(struct si_sm_io *io, unsigned int offset)
{
	return (readq((io->addr)+(offset * io->regspacing)) >> io->regshift)
		& 0xff;
}

static void mem_outq(struct si_sm_io *io, unsigned int offset,
		     unsigned char b)
{
	writeq(b << io->regshift, (io->addr)+(offset * io->regspacing));
}
#endif

static void mem_cleanup(struct smi_info *info)
{
	unsigned long addr = info->io.addr_data;
	int           mapsize;

	if (info->io.addr) {
		iounmap(info->io.addr);

		mapsize = ((info->io_size * info->io.regspacing)
			   - (info->io.regspacing - info->io.regsize));

		release_mem_region(addr, mapsize);
	}
}

static int mem_setup(struct smi_info *info)
{
	unsigned long addr = info->io.addr_data;
	int           mapsize;

	if (!addr)
		return -ENODEV;

	info->io_cleanup = mem_cleanup;

	/* Figure out the actual readb/readw/readl/etc routine to use based
	   upon the register size. */
	switch (info->io.regsize) {
	case 1:
		info->io.inputb = intf_mem_inb;
		info->io.outputb = intf_mem_outb;
		break;
	case 2:
		info->io.inputb = intf_mem_inw;
		info->io.outputb = intf_mem_outw;
		break;
	case 4:
		info->io.inputb = intf_mem_inl;
		info->io.outputb = intf_mem_outl;
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

	if (request_mem_region(addr, mapsize, DEVICE_NAME) == NULL)
		return -EIO;

	info->io.addr = ioremap(addr, mapsize);
	if (info->io.addr == NULL) {
		release_mem_region(addr, mapsize);
		return -EIO;
	}
	return 0;
}

/*
 * Parms come in as <op1>[:op2[:op3...]].  ops are:
 *   add|remove,kcs|bt|smic,mem|i/o,<address>[,<opt1>[,<opt2>[,...]]]
 * Options are:
 *   rsp=<regspacing>
 *   rsi=<regsize>
 *   rsh=<regshift>
 *   irq=<irq>
 *   ipmb=<ipmb addr>
 */
enum hotmod_op { HM_ADD, HM_REMOVE };
struct hotmod_vals {
	char *name;
	int  val;
};
static struct hotmod_vals hotmod_ops[] = {
	{ "add",	HM_ADD },
	{ "remove",	HM_REMOVE },
	{ NULL }
};
static struct hotmod_vals hotmod_si[] = {
	{ "kcs",	SI_KCS },
	{ "smic",	SI_SMIC },
	{ "bt",		SI_BT },
	{ NULL }
};
static struct hotmod_vals hotmod_as[] = {
	{ "mem",	IPMI_MEM_ADDR_SPACE },
	{ "i/o",	IPMI_IO_ADDR_SPACE },
	{ NULL }
};

static int parse_str(struct hotmod_vals *v, int *val, char *name, char **curr)
{
	char *s;
	int  i;

	s = strchr(*curr, ',');
	if (!s) {
		printk(KERN_WARNING PFX "No hotmod %s given.\n", name);
		return -EINVAL;
	}
	*s = '\0';
	s++;
	for (i = 0; hotmod_ops[i].name; i++) {
		if (strcmp(*curr, v[i].name) == 0) {
			*val = v[i].val;
			*curr = s;
			return 0;
		}
	}

	printk(KERN_WARNING PFX "Invalid hotmod %s '%s'\n", name, *curr);
	return -EINVAL;
}

static int check_hotmod_int_op(const char *curr, const char *option,
			       const char *name, int *val)
{
	char *n;

	if (strcmp(curr, name) == 0) {
		if (!option) {
			printk(KERN_WARNING PFX
			       "No option given for '%s'\n",
			       curr);
			return -EINVAL;
		}
		*val = simple_strtoul(option, &n, 0);
		if ((*n != '\0') || (*option == '\0')) {
			printk(KERN_WARNING PFX
			       "Bad option given for '%s'\n",
			       curr);
			return -EINVAL;
		}
		return 1;
	}
	return 0;
}

static int hotmod_handler(const char *val, struct kernel_param *kp)
{
	char *str = kstrdup(val, GFP_KERNEL);
	int  rv;
	char *next, *curr, *s, *n, *o;
	enum hotmod_op op;
	enum si_type si_type;
	int  addr_space;
	unsigned long addr;
	int regspacing;
	int regsize;
	int regshift;
	int irq;
	int ipmb;
	int ival;
	int len;
	struct smi_info *info;

	if (!str)
		return -ENOMEM;

	/* Kill any trailing spaces, as we can get a "\n" from echo. */
	len = strlen(str);
	ival = len - 1;
	while ((ival >= 0) && isspace(str[ival])) {
		str[ival] = '\0';
		ival--;
	}

	for (curr = str; curr; curr = next) {
		regspacing = 1;
		regsize = 1;
		regshift = 0;
		irq = 0;
		ipmb = 0x20;

		next = strchr(curr, ':');
		if (next) {
			*next = '\0';
			next++;
		}

		rv = parse_str(hotmod_ops, &ival, "operation", &curr);
		if (rv)
			break;
		op = ival;

		rv = parse_str(hotmod_si, &ival, "interface type", &curr);
		if (rv)
			break;
		si_type = ival;

		rv = parse_str(hotmod_as, &addr_space, "address space", &curr);
		if (rv)
			break;

		s = strchr(curr, ',');
		if (s) {
			*s = '\0';
			s++;
		}
		addr = simple_strtoul(curr, &n, 0);
		if ((*n != '\0') || (*curr == '\0')) {
			printk(KERN_WARNING PFX "Invalid hotmod address"
			       " '%s'\n", curr);
			break;
		}

		while (s) {
			curr = s;
			s = strchr(curr, ',');
			if (s) {
				*s = '\0';
				s++;
			}
			o = strchr(curr, '=');
			if (o) {
				*o = '\0';
				o++;
			}
			rv = check_hotmod_int_op(curr, o, "rsp", &regspacing);
			if (rv < 0)
				goto out;
			else if (rv)
				continue;
			rv = check_hotmod_int_op(curr, o, "rsi", &regsize);
			if (rv < 0)
				goto out;
			else if (rv)
				continue;
			rv = check_hotmod_int_op(curr, o, "rsh", &regshift);
			if (rv < 0)
				goto out;
			else if (rv)
				continue;
			rv = check_hotmod_int_op(curr, o, "irq", &irq);
			if (rv < 0)
				goto out;
			else if (rv)
				continue;
			rv = check_hotmod_int_op(curr, o, "ipmb", &ipmb);
			if (rv < 0)
				goto out;
			else if (rv)
				continue;

			rv = -EINVAL;
			printk(KERN_WARNING PFX
			       "Invalid hotmod option '%s'\n",
			       curr);
			goto out;
		}

		if (op == HM_ADD) {
			info = kzalloc(sizeof(*info), GFP_KERNEL);
			if (!info) {
				rv = -ENOMEM;
				goto out;
			}

			info->addr_source = "hotmod";
			info->si_type = si_type;
			info->io.addr_data = addr;
			info->io.addr_type = addr_space;
			if (addr_space == IPMI_MEM_ADDR_SPACE)
				info->io_setup = mem_setup;
			else
				info->io_setup = port_setup;

			info->io.addr = NULL;
			info->io.regspacing = regspacing;
			if (!info->io.regspacing)
				info->io.regspacing = DEFAULT_REGSPACING;
			info->io.regsize = regsize;
			if (!info->io.regsize)
				info->io.regsize = DEFAULT_REGSPACING;
			info->io.regshift = regshift;
			info->irq = irq;
			if (info->irq)
				info->irq_setup = std_irq_setup;
			info->slave_addr = ipmb;

			try_smi_init(info);
		} else {
			/* remove */
			struct smi_info *e, *tmp_e;

			mutex_lock(&smi_infos_lock);
			list_for_each_entry_safe(e, tmp_e, &smi_infos, link) {
				if (e->io.addr_type != addr_space)
					continue;
				if (e->si_type != si_type)
					continue;
				if (e->io.addr_data == addr)
					cleanup_one_si(e);
			}
			mutex_unlock(&smi_infos_lock);
		}
	}
	rv = len;
 out:
	kfree(str);
	return rv;
}

static __devinit void hardcode_find_bmc(void)
{
	int             i;
	struct smi_info *info;

	for (i = 0; i < SI_MAX_PARMS; i++) {
		if (!ports[i] && !addrs[i])
			continue;

		info = kzalloc(sizeof(*info), GFP_KERNEL);
		if (!info)
			return;

		info->addr_source = "hardcoded";

		if (!si_type[i] || strcmp(si_type[i], "kcs") == 0) {
			info->si_type = SI_KCS;
		} else if (strcmp(si_type[i], "smic") == 0) {
			info->si_type = SI_SMIC;
		} else if (strcmp(si_type[i], "bt") == 0) {
			info->si_type = SI_BT;
		} else {
			printk(KERN_WARNING
			       "ipmi_si: Interface type specified "
			       "for interface %d, was invalid: %s\n",
			       i, si_type[i]);
			kfree(info);
			continue;
		}

		if (ports[i]) {
			/* An I/O port */
			info->io_setup = port_setup;
			info->io.addr_data = ports[i];
			info->io.addr_type = IPMI_IO_ADDR_SPACE;
		} else if (addrs[i]) {
			/* A memory port */
			info->io_setup = mem_setup;
			info->io.addr_data = addrs[i];
			info->io.addr_type = IPMI_MEM_ADDR_SPACE;
		} else {
			printk(KERN_WARNING
			       "ipmi_si: Interface type specified "
			       "for interface %d, "
			       "but port and address were not set or "
			       "set to zero.\n", i);
			kfree(info);
			continue;
		}

		info->io.addr = NULL;
		info->io.regspacing = regspacings[i];
		if (!info->io.regspacing)
			info->io.regspacing = DEFAULT_REGSPACING;
		info->io.regsize = regsizes[i];
		if (!info->io.regsize)
			info->io.regsize = DEFAULT_REGSPACING;
		info->io.regshift = regshifts[i];
		info->irq = irqs[i];
		if (info->irq)
			info->irq_setup = std_irq_setup;

		try_smi_init(info);
	}
}

#ifdef CONFIG_ACPI

#include <linux/acpi.h>

/* Once we get an ACPI failure, we don't try any more, because we go
   through the tables sequentially.  Once we don't find a table, there
   are no more. */
static int acpi_failure;

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

#ifdef DEBUG_TIMING
	do_gettimeofday(&t);
	printk("**ACPI_GPE: %d.%9.9d\n", t.tv_sec, t.tv_usec);
#endif
	smi_event_handler(smi_info, 0);
	spin_unlock_irqrestore(&(smi_info->si_lock), flags);

	return ACPI_INTERRUPT_HANDLED;
}

static void acpi_gpe_irq_cleanup(struct smi_info *info)
{
	if (!info->irq)
		return;

	acpi_remove_gpe_handler(NULL, info->irq, &ipmi_acpi_gpe);
}

static int acpi_gpe_irq_setup(struct smi_info *info)
{
	acpi_status status;

	if (!info->irq)
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
		info->irq_cleanup = acpi_gpe_irq_cleanup;
		printk("  Using ACPI GPE %d\n", info->irq);
		return 0;
	}
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

static __devinit int try_init_acpi(struct SPMITable *spmi)
{
	struct smi_info  *info;
	u8 		 addr_space;

	if (spmi->IPMIlegacy != 1) {
	    printk(KERN_INFO "IPMI: Bad SPMI legacy %d\n", spmi->IPMIlegacy);
  	    return -ENODEV;
	}

	if (spmi->addr.space_id == ACPI_ADR_SPACE_SYSTEM_MEMORY)
		addr_space = IPMI_MEM_ADDR_SPACE;
	else
		addr_space = IPMI_IO_ADDR_SPACE;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		printk(KERN_ERR "ipmi_si: Could not allocate SI data (3)\n");
		return -ENOMEM;
	}

	info->addr_source = "ACPI";

	/* Figure out the interface type. */
	switch (spmi->InterfaceType)
	{
	case 1:	/* KCS */
		info->si_type = SI_KCS;
		break;
	case 2:	/* SMIC */
		info->si_type = SI_SMIC;
		break;
	case 3:	/* BT */
		info->si_type = SI_BT;
		break;
	default:
		printk(KERN_INFO "ipmi_si: Unknown ACPI/SPMI SI type %d\n",
			spmi->InterfaceType);
		kfree(info);
		return -EIO;
	}

	if (spmi->InterruptType & 1) {
		/* We've got a GPE interrupt. */
		info->irq = spmi->GPE;
		info->irq_setup = acpi_gpe_irq_setup;
	} else if (spmi->InterruptType & 2) {
		/* We've got an APIC/SAPIC interrupt. */
		info->irq = spmi->GlobalSystemInterrupt;
		info->irq_setup = std_irq_setup;
	} else {
		/* Use the default interrupt setting. */
		info->irq = 0;
		info->irq_setup = NULL;
	}

	if (spmi->addr.bit_width) {
		/* A (hopefully) properly formed register bit width. */
		info->io.regspacing = spmi->addr.bit_width / 8;
	} else {
		info->io.regspacing = DEFAULT_REGSPACING;
	}
	info->io.regsize = info->io.regspacing;
	info->io.regshift = spmi->addr.bit_offset;

	if (spmi->addr.space_id == ACPI_ADR_SPACE_SYSTEM_MEMORY) {
		info->io_setup = mem_setup;
		info->io.addr_type = IPMI_MEM_ADDR_SPACE;
	} else if (spmi->addr.space_id == ACPI_ADR_SPACE_SYSTEM_IO) {
		info->io_setup = port_setup;
		info->io.addr_type = IPMI_IO_ADDR_SPACE;
	} else {
		kfree(info);
		printk("ipmi_si: Unknown ACPI I/O Address type\n");
		return -EIO;
	}
	info->io.addr_data = spmi->addr.address;

	try_smi_init(info);

	return 0;
}

static __devinit void acpi_find_bmc(void)
{
	acpi_status      status;
	struct SPMITable *spmi;
	int              i;

	if (acpi_disabled)
		return;

	if (acpi_failure)
		return;

	for (i = 0; ; i++) {
		status = acpi_get_table(ACPI_SIG_SPMI, i+1,
					(struct acpi_table_header **)&spmi);
		if (status != AE_OK)
			return;

		try_init_acpi(spmi);
	}
}
#endif

#ifdef CONFIG_DMI
struct dmi_ipmi_data
{
	u8   		type;
	u8   		addr_space;
	unsigned long	base_addr;
	u8   		irq;
	u8              offset;
	u8              slave_addr;
};

static int __devinit decode_dmi(const struct dmi_header *dm,
				struct dmi_ipmi_data *dmi)
{
	const u8	*data = (const u8 *)dm;
	unsigned long  	base_addr;
	u8		reg_spacing;
	u8              len = dm->length;

	dmi->type = data[4];

	memcpy(&base_addr, data+8, sizeof(unsigned long));
	if (len >= 0x11) {
		if (base_addr & 1) {
			/* I/O */
			base_addr &= 0xFFFE;
			dmi->addr_space = IPMI_IO_ADDR_SPACE;
		}
		else {
			/* Memory */
			dmi->addr_space = IPMI_MEM_ADDR_SPACE;
		}
		/* If bit 4 of byte 0x10 is set, then the lsb for the address
		   is odd. */
		dmi->base_addr = base_addr | ((data[0x10] & 0x10) >> 4);

		dmi->irq = data[0x11];

		/* The top two bits of byte 0x10 hold the register spacing. */
		reg_spacing = (data[0x10] & 0xC0) >> 6;
		switch(reg_spacing){
		case 0x00: /* Byte boundaries */
		    dmi->offset = 1;
		    break;
		case 0x01: /* 32-bit boundaries */
		    dmi->offset = 4;
		    break;
		case 0x02: /* 16-byte boundaries */
		    dmi->offset = 16;
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
		dmi->base_addr = base_addr & 0xfffe;
		dmi->addr_space = IPMI_IO_ADDR_SPACE;
		dmi->offset = 1;
	}

	dmi->slave_addr = data[6];

	return 0;
}

static __devinit void try_init_dmi(struct dmi_ipmi_data *ipmi_data)
{
	struct smi_info *info;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		printk(KERN_ERR
		       "ipmi_si: Could not allocate SI data\n");
		return;
	}

	info->addr_source = "SMBIOS";

	switch (ipmi_data->type) {
	case 0x01: /* KCS */
		info->si_type = SI_KCS;
		break;
	case 0x02: /* SMIC */
		info->si_type = SI_SMIC;
		break;
	case 0x03: /* BT */
		info->si_type = SI_BT;
		break;
	default:
		kfree(info);
		return;
	}

	switch (ipmi_data->addr_space) {
	case IPMI_MEM_ADDR_SPACE:
		info->io_setup = mem_setup;
		info->io.addr_type = IPMI_MEM_ADDR_SPACE;
		break;

	case IPMI_IO_ADDR_SPACE:
		info->io_setup = port_setup;
		info->io.addr_type = IPMI_IO_ADDR_SPACE;
		break;

	default:
		kfree(info);
		printk(KERN_WARNING
		       "ipmi_si: Unknown SMBIOS I/O Address type: %d.\n",
		       ipmi_data->addr_space);
		return;
	}
	info->io.addr_data = ipmi_data->base_addr;

	info->io.regspacing = ipmi_data->offset;
	if (!info->io.regspacing)
		info->io.regspacing = DEFAULT_REGSPACING;
	info->io.regsize = DEFAULT_REGSPACING;
	info->io.regshift = 0;

	info->slave_addr = ipmi_data->slave_addr;

	info->irq = ipmi_data->irq;
	if (info->irq)
		info->irq_setup = std_irq_setup;

	try_smi_init(info);
}

static void __devinit dmi_find_bmc(void)
{
	const struct dmi_device *dev = NULL;
	struct dmi_ipmi_data data;
	int                  rv;

	while ((dev = dmi_find_device(DMI_DEV_TYPE_IPMI, NULL, dev))) {
		memset(&data, 0, sizeof(data));
		rv = decode_dmi((const struct dmi_header *) dev->device_data,
				&data);
		if (!rv)
			try_init_dmi(&data);
	}
}
#endif /* CONFIG_DMI */

#ifdef CONFIG_PCI

#define PCI_ERMC_CLASSCODE		0x0C0700
#define PCI_ERMC_CLASSCODE_MASK		0xffffff00
#define PCI_ERMC_CLASSCODE_TYPE_MASK	0xff
#define PCI_ERMC_CLASSCODE_TYPE_SMIC	0x00
#define PCI_ERMC_CLASSCODE_TYPE_KCS	0x01
#define PCI_ERMC_CLASSCODE_TYPE_BT	0x02

#define PCI_HP_VENDOR_ID    0x103C
#define PCI_MMC_DEVICE_ID   0x121A
#define PCI_MMC_ADDR_CW     0x10

static void ipmi_pci_cleanup(struct smi_info *info)
{
	struct pci_dev *pdev = info->addr_source_data;

	pci_disable_device(pdev);
}

static int __devinit ipmi_pci_probe(struct pci_dev *pdev,
				    const struct pci_device_id *ent)
{
	int rv;
	int class_type = pdev->class & PCI_ERMC_CLASSCODE_TYPE_MASK;
	struct smi_info *info;
	int first_reg_offset = 0;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->addr_source = "PCI";

	switch (class_type) {
	case PCI_ERMC_CLASSCODE_TYPE_SMIC:
		info->si_type = SI_SMIC;
		break;

	case PCI_ERMC_CLASSCODE_TYPE_KCS:
		info->si_type = SI_KCS;
		break;

	case PCI_ERMC_CLASSCODE_TYPE_BT:
		info->si_type = SI_BT;
		break;

	default:
		kfree(info);
		printk(KERN_INFO "ipmi_si: %s: Unknown IPMI type: %d\n",
		       pci_name(pdev), class_type);
		return -ENOMEM;
	}

	rv = pci_enable_device(pdev);
	if (rv) {
		printk(KERN_ERR "ipmi_si: %s: couldn't enable PCI device\n",
		       pci_name(pdev));
		kfree(info);
		return rv;
	}

	info->addr_source_cleanup = ipmi_pci_cleanup;
	info->addr_source_data = pdev;

	if (pdev->subsystem_vendor == PCI_HP_VENDOR_ID)
		first_reg_offset = 1;

	if (pci_resource_flags(pdev, 0) & IORESOURCE_IO) {
		info->io_setup = port_setup;
		info->io.addr_type = IPMI_IO_ADDR_SPACE;
	} else {
		info->io_setup = mem_setup;
		info->io.addr_type = IPMI_MEM_ADDR_SPACE;
	}
	info->io.addr_data = pci_resource_start(pdev, 0);

	info->io.regspacing = DEFAULT_REGSPACING;
	info->io.regsize = DEFAULT_REGSPACING;
	info->io.regshift = 0;

	info->irq = pdev->irq;
	if (info->irq)
		info->irq_setup = std_irq_setup;

	info->dev = &pdev->dev;
	pci_set_drvdata(pdev, info);

	return try_smi_init(info);
}

static void __devexit ipmi_pci_remove(struct pci_dev *pdev)
{
	struct smi_info *info = pci_get_drvdata(pdev);
	cleanup_one_si(info);
}

#ifdef CONFIG_PM
static int ipmi_pci_suspend(struct pci_dev *pdev, pm_message_t state)
{
	return 0;
}

static int ipmi_pci_resume(struct pci_dev *pdev)
{
	return 0;
}
#endif

static struct pci_device_id ipmi_pci_devices[] = {
	{ PCI_DEVICE(PCI_HP_VENDOR_ID, PCI_MMC_DEVICE_ID) },
	{ PCI_DEVICE_CLASS(PCI_ERMC_CLASSCODE, PCI_ERMC_CLASSCODE_MASK) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, ipmi_pci_devices);

static struct pci_driver ipmi_pci_driver = {
        .name =         DEVICE_NAME,
        .id_table =     ipmi_pci_devices,
        .probe =        ipmi_pci_probe,
        .remove =       __devexit_p(ipmi_pci_remove),
#ifdef CONFIG_PM
        .suspend =      ipmi_pci_suspend,
        .resume =       ipmi_pci_resume,
#endif
};
#endif /* CONFIG_PCI */


#ifdef CONFIG_PPC_OF
static int __devinit ipmi_of_probe(struct of_device *dev,
			 const struct of_device_id *match)
{
	struct smi_info *info;
	struct resource resource;
	const int *regsize, *regspacing, *regshift;
	struct device_node *np = dev->node;
	int ret;
	int proplen;

	dev_info(&dev->dev, PFX "probing via device tree\n");

	ret = of_address_to_resource(np, 0, &resource);
	if (ret) {
		dev_warn(&dev->dev, PFX "invalid address from OF\n");
		return ret;
	}

	regsize = of_get_property(np, "reg-size", &proplen);
	if (regsize && proplen != 4) {
		dev_warn(&dev->dev, PFX "invalid regsize from OF\n");
		return -EINVAL;
	}

	regspacing = of_get_property(np, "reg-spacing", &proplen);
	if (regspacing && proplen != 4) {
		dev_warn(&dev->dev, PFX "invalid regspacing from OF\n");
		return -EINVAL;
	}

	regshift = of_get_property(np, "reg-shift", &proplen);
	if (regshift && proplen != 4) {
		dev_warn(&dev->dev, PFX "invalid regshift from OF\n");
		return -EINVAL;
	}

	info = kzalloc(sizeof(*info), GFP_KERNEL);

	if (!info) {
		dev_err(&dev->dev,
			PFX "could not allocate memory for OF probe\n");
		return -ENOMEM;
	}

	info->si_type		= (enum si_type) match->data;
	info->addr_source	= "device-tree";
	info->io_setup		= mem_setup;
	info->irq_setup		= std_irq_setup;

	info->io.addr_type	= IPMI_MEM_ADDR_SPACE;
	info->io.addr_data	= resource.start;

	info->io.regsize	= regsize ? *regsize : DEFAULT_REGSIZE;
	info->io.regspacing	= regspacing ? *regspacing : DEFAULT_REGSPACING;
	info->io.regshift	= regshift ? *regshift : 0;

	info->irq		= irq_of_parse_and_map(dev->node, 0);
	info->dev		= &dev->dev;

	dev_dbg(&dev->dev, "addr 0x%lx regsize %d spacing %d irq %x\n",
		info->io.addr_data, info->io.regsize, info->io.regspacing,
		info->irq);

	dev->dev.driver_data = (void*) info;

	return try_smi_init(info);
}

static int __devexit ipmi_of_remove(struct of_device *dev)
{
	cleanup_one_si(dev->dev.driver_data);
	return 0;
}

static struct of_device_id ipmi_match[] =
{
	{ .type = "ipmi", .compatible = "ipmi-kcs",  .data = (void *)(unsigned long) SI_KCS },
	{ .type = "ipmi", .compatible = "ipmi-smic", .data = (void *)(unsigned long) SI_SMIC },
	{ .type = "ipmi", .compatible = "ipmi-bt",   .data = (void *)(unsigned long) SI_BT },
	{},
};

static struct of_platform_driver ipmi_of_platform_driver =
{
	.name		= "ipmi",
	.match_table	= ipmi_match,
	.probe		= ipmi_of_probe,
	.remove		= __devexit_p(ipmi_of_remove),
};
#endif /* CONFIG_PPC_OF */


static int try_get_dev_id(struct smi_info *smi_info)
{
	unsigned char         msg[2];
	unsigned char         *resp;
	unsigned long         resp_len;
	enum si_sm_result     smi_result;
	int                   rv = 0;

	resp = kmalloc(IPMI_MAX_MSG_LENGTH, GFP_KERNEL);
	if (!resp)
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

	/* Check and record info from the get device id, in case we need it. */
	rv = ipmi_demangle_device_id(resp, resp_len, &smi_info->device_id);

 out:
	kfree(resp);
	return rv;
}

static int type_file_read_proc(char *page, char **start, off_t off,
			       int count, int *eof, void *data)
{
	struct smi_info *smi = data;

	return sprintf(page, "%s\n", si_to_str[smi->si_type]);
}

static int stat_file_read_proc(char *page, char **start, off_t off,
			       int count, int *eof, void *data)
{
	char            *out = (char *) page;
	struct smi_info *smi = data;

	out += sprintf(out, "interrupts_enabled:    %d\n",
		       smi->irq && !smi->interrupt_disabled);
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

	return out - page;
}

static int param_read_proc(char *page, char **start, off_t off,
			   int count, int *eof, void *data)
{
	struct smi_info *smi = data;

	return sprintf(page,
		       "%s,%s,0x%lx,rsp=%d,rsi=%d,rsh=%d,irq=%d,ipmb=%d\n",
		       si_to_str[smi->si_type],
		       addr_space_to_str[smi->io.addr_type],
		       smi->io.addr_data,
		       smi->io.regspacing,
		       smi->io.regsize,
		       smi->io.regshift,
		       smi->irq,
		       smi->slave_addr);
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
#define DELL_IANA_MFR_ID 0x0002a2
static void setup_dell_poweredge_oem_data_handler(struct smi_info *smi_info)
{
	struct ipmi_device_id *id = &smi_info->device_id;
	if (id->manufacturer_id == DELL_IANA_MFR_ID) {
		if (id->device_id       == DELL_POWEREDGE_8G_BMC_DEVICE_ID  &&
		    id->device_revision == DELL_POWEREDGE_8G_BMC_DEVICE_REV &&
		    id->ipmi_version   == DELL_POWEREDGE_8G_BMC_IPMI_VERSION) {
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
	if (id->manufacturer_id == DELL_IANA_MFR_ID &&
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
	if (smi_info->intf) {
		/* The timer and thread are only running if the
		   interface has been started up and registered. */
		if (smi_info->thread != NULL)
			kthread_stop(smi_info->thread);
		del_timer_sync(&smi_info->si_timer);
	}
}

static __devinitdata struct ipmi_default_vals
{
	int type;
	int port;
} ipmi_defaults[] =
{
	{ .type = SI_KCS, .port = 0xca2 },
	{ .type = SI_SMIC, .port = 0xca9 },
	{ .type = SI_BT, .port = 0xe4 },
	{ .port = 0 }
};

static __devinit void default_find_bmc(void)
{
	struct smi_info *info;
	int             i;

	for (i = 0; ; i++) {
		if (!ipmi_defaults[i].port)
			break;

		info = kzalloc(sizeof(*info), GFP_KERNEL);
		if (!info)
			return;

#ifdef CONFIG_PPC_MERGE
		if (check_legacy_ioport(ipmi_defaults[i].port))
			continue;
#endif

		info->addr_source = NULL;

		info->si_type = ipmi_defaults[i].type;
		info->io_setup = port_setup;
		info->io.addr_data = ipmi_defaults[i].port;
		info->io.addr_type = IPMI_IO_ADDR_SPACE;

		info->io.addr = NULL;
		info->io.regspacing = DEFAULT_REGSPACING;
		info->io.regsize = DEFAULT_REGSPACING;
		info->io.regshift = 0;

		if (try_smi_init(info) == 0) {
			/* Found one... */
			printk(KERN_INFO "ipmi_si: Found default %s state"
			       " machine at %s address 0x%lx\n",
			       si_to_str[info->si_type],
			       addr_space_to_str[info->io.addr_type],
			       info->io.addr_data);
			return;
		}
	}
}

static int is_new_interface(struct smi_info *info)
{
	struct smi_info *e;

	list_for_each_entry(e, &smi_infos, link) {
		if (e->io.addr_type != info->io.addr_type)
			continue;
		if (e->io.addr_data == info->io.addr_data)
			return 0;
	}

	return 1;
}

static int try_smi_init(struct smi_info *new_smi)
{
	int rv;

	if (new_smi->addr_source) {
		printk(KERN_INFO "ipmi_si: Trying %s-specified %s state"
		       " machine at %s address 0x%lx, slave address 0x%x,"
		       " irq %d\n",
		       new_smi->addr_source,
		       si_to_str[new_smi->si_type],
		       addr_space_to_str[new_smi->io.addr_type],
		       new_smi->io.addr_data,
		       new_smi->slave_addr, new_smi->irq);
	}

	mutex_lock(&smi_infos_lock);
	if (!is_new_interface(new_smi)) {
		printk(KERN_WARNING "ipmi_si: duplicate interface\n");
		rv = -EBUSY;
		goto out_err;
	}

	/* So we know not to free it unless we have allocated one. */
	new_smi->intf = NULL;
	new_smi->si_sm = NULL;
	new_smi->handlers = NULL;

	switch (new_smi->si_type) {
	case SI_KCS:
		new_smi->handlers = &kcs_smi_handlers;
		break;

	case SI_SMIC:
		new_smi->handlers = &smic_smi_handlers;
		break;

	case SI_BT:
		new_smi->handlers = &bt_smi_handlers;
		break;

	default:
		/* No support for anything else yet. */
		rv = -EIO;
		goto out_err;
	}

	/* Allocate the state machine's data and initialize it. */
	new_smi->si_sm = kmalloc(new_smi->handlers->size(), GFP_KERNEL);
	if (!new_smi->si_sm) {
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
		if (new_smi->addr_source)
			printk(KERN_INFO "ipmi_si: Interface detection"
			       " failed\n");
		rv = -ENODEV;
		goto out_err;
	}

	/* Attempt a get device id command.  If it fails, we probably
           don't have a BMC here. */
	rv = try_get_dev_id(new_smi);
	if (rv) {
		if (new_smi->addr_source)
			printk(KERN_INFO "ipmi_si: There appears to be no BMC"
			       " at this location\n");
		goto out_err;
	}

	setup_oem_data_handler(new_smi);
	setup_xaction_handlers(new_smi);

	INIT_LIST_HEAD(&(new_smi->xmit_msgs));
	INIT_LIST_HEAD(&(new_smi->hp_xmit_msgs));
	new_smi->curr_msg = NULL;
	atomic_set(&new_smi->req_events, 0);
	new_smi->run_to_completion = 0;

	new_smi->interrupt_disabled = 0;
	atomic_set(&new_smi->stop_operation, 0);
	new_smi->intf_num = smi_num;
	smi_num++;

	/* Start clearing the flags before we enable interrupts or the
	   timer to avoid racing with the timer. */
	start_clear_flags(new_smi);
	/* IRQ is defined to be set when non-zero. */
	if (new_smi->irq)
		new_smi->si_state = SI_CLEARING_FLAGS_THEN_SET_IRQ;

	if (!new_smi->dev) {
		/* If we don't already have a device from something
		 * else (like PCI), then register a new one. */
		new_smi->pdev = platform_device_alloc("ipmi_si",
						      new_smi->intf_num);
		if (rv) {
			printk(KERN_ERR
			       "ipmi_si_intf:"
			       " Unable to allocate platform device\n");
			goto out_err;
		}
		new_smi->dev = &new_smi->pdev->dev;
		new_smi->dev->driver = &ipmi_driver;

		rv = platform_device_add(new_smi->pdev);
		if (rv) {
			printk(KERN_ERR
			       "ipmi_si_intf:"
			       " Unable to register system interface device:"
			       " %d\n",
			       rv);
			goto out_err;
		}
		new_smi->dev_registered = 1;
	}

	rv = ipmi_register_smi(&handlers,
			       new_smi,
			       &new_smi->device_id,
			       new_smi->dev,
			       "bmc",
			       new_smi->slave_addr);
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

	rv = ipmi_smi_add_proc_entry(new_smi->intf, "params",
				     param_read_proc, NULL,
				     new_smi, THIS_MODULE);
	if (rv) {
		printk(KERN_ERR
		       "ipmi_si: Unable to create proc entry: %d\n",
		       rv);
		goto out_err_stop_timer;
	}

	list_add_tail(&new_smi->link, &smi_infos);

	mutex_unlock(&smi_infos_lock);

	printk(KERN_INFO "IPMI %s interface initialized\n",si_to_str[new_smi->si_type]);

	return 0;

 out_err_stop_timer:
	atomic_inc(&new_smi->stop_operation);
	wait_for_timer_and_thread(new_smi);

 out_err:
	if (new_smi->intf)
		ipmi_unregister_smi(new_smi->intf);

	if (new_smi->irq_cleanup)
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
	if (new_smi->addr_source_cleanup)
		new_smi->addr_source_cleanup(new_smi);
	if (new_smi->io_cleanup)
		new_smi->io_cleanup(new_smi);

	if (new_smi->dev_registered)
		platform_device_unregister(new_smi->pdev);

	kfree(new_smi);

	mutex_unlock(&smi_infos_lock);

	return rv;
}

static __devinit int init_ipmi_si(void)
{
	int  i;
	char *str;
	int  rv;

	if (initialized)
		return 0;
	initialized = 1;

	/* Register the device drivers. */
	rv = driver_register(&ipmi_driver);
	if (rv) {
		printk(KERN_ERR
		       "init_ipmi_si: Unable to register driver: %d\n",
		       rv);
		return rv;
	}


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

	hardcode_find_bmc();

#ifdef CONFIG_DMI
	dmi_find_bmc();
#endif

#ifdef CONFIG_ACPI
	acpi_find_bmc();
#endif

#ifdef CONFIG_PCI
	rv = pci_register_driver(&ipmi_pci_driver);
	if (rv){
		printk(KERN_ERR
		       "init_ipmi_si: Unable to register PCI driver: %d\n",
		       rv);
	}
#endif

#ifdef CONFIG_PPC_OF
	of_register_platform_driver(&ipmi_of_platform_driver);
#endif

	if (si_trydefaults) {
		mutex_lock(&smi_infos_lock);
		if (list_empty(&smi_infos)) {
			/* No BMC was found, try defaults. */
			mutex_unlock(&smi_infos_lock);
			default_find_bmc();
		} else {
			mutex_unlock(&smi_infos_lock);
		}
	}

	mutex_lock(&smi_infos_lock);
	if (unload_when_empty && list_empty(&smi_infos)) {
		mutex_unlock(&smi_infos_lock);
#ifdef CONFIG_PCI
		pci_unregister_driver(&ipmi_pci_driver);
#endif

#ifdef CONFIG_PPC_OF
		of_unregister_platform_driver(&ipmi_of_platform_driver);
#endif
		driver_unregister(&ipmi_driver);
		printk("ipmi_si: Unable to find any System Interface(s)\n");
		return -ENODEV;
	} else {
		mutex_unlock(&smi_infos_lock);
		return 0;
	}
}
module_init(init_ipmi_si);

static void cleanup_one_si(struct smi_info *to_clean)
{
	int           rv;
	unsigned long flags;

	if (!to_clean)
		return;

	list_del(&to_clean->link);

	/* Tell the driver that we are shutting down. */
	atomic_inc(&to_clean->stop_operation);

	/* Make sure the timer and thread are stopped and will not run
	   again. */
	wait_for_timer_and_thread(to_clean);

	/* Timeouts are stopped, now make sure the interrupts are off
	   for the device.  A little tricky with locks to make sure
	   there are no races. */
	spin_lock_irqsave(&to_clean->si_lock, flags);
	while (to_clean->curr_msg || (to_clean->si_state != SI_NORMAL)) {
		spin_unlock_irqrestore(&to_clean->si_lock, flags);
		poll(to_clean);
		schedule_timeout_uninterruptible(1);
		spin_lock_irqsave(&to_clean->si_lock, flags);
	}
	disable_si_irq(to_clean);
	spin_unlock_irqrestore(&to_clean->si_lock, flags);
	while (to_clean->curr_msg || (to_clean->si_state != SI_NORMAL)) {
		poll(to_clean);
		schedule_timeout_uninterruptible(1);
	}

	/* Clean up interrupts and make sure that everything is done. */
	if (to_clean->irq_cleanup)
		to_clean->irq_cleanup(to_clean);
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

	if (to_clean->addr_source_cleanup)
		to_clean->addr_source_cleanup(to_clean);
	if (to_clean->io_cleanup)
		to_clean->io_cleanup(to_clean);

	if (to_clean->dev_registered)
		platform_device_unregister(to_clean->pdev);

	kfree(to_clean);
}

static __exit void cleanup_ipmi_si(void)
{
	struct smi_info *e, *tmp_e;

	if (!initialized)
		return;

#ifdef CONFIG_PCI
	pci_unregister_driver(&ipmi_pci_driver);
#endif

#ifdef CONFIG_PPC_OF
	of_unregister_platform_driver(&ipmi_of_platform_driver);
#endif

	mutex_lock(&smi_infos_lock);
	list_for_each_entry_safe(e, tmp_e, &smi_infos, link)
		cleanup_one_si(e);
	mutex_unlock(&smi_infos_lock);

	driver_unregister(&ipmi_driver);
}
module_exit(cleanup_ipmi_si);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Corey Minyard <minyard@mvista.com>");
MODULE_DESCRIPTION("Interface to the IPMI driver for the KCS, SMIC, and BT system interfaces.");
