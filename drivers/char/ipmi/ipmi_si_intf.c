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
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/notifier.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <asm/irq.h>
#include <linux/interrupt.h>
#include <linux/rcupdate.h>
#include <linux/ipmi.h>
#include <linux/ipmi_smi.h>
#include "ipmi_si.h"
#include <linux/string.h>
#include <linux/ctype.h>

#define PFX "ipmi_si: "

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
	SI_GETTING_MESSAGES,
	SI_CHECKING_ENABLES,
	SI_SETTING_ENABLES
	/* FIXME - add watchdog stuff. */
};

/* Some BT-specific defines we need here. */
#define IPMI_BT_INTMASK_REG		2
#define IPMI_BT_INTMASK_CLEAR_IRQ_BIT	2
#define IPMI_BT_INTMASK_ENABLE_IRQ_BIT	1

static const char * const si_to_str[] = { "invalid", "kcs", "smic", "bt" };

static int initialized;

/*
 * Indexes into stats[] in smi_info below.
 */
enum si_stat_indexes {
	/*
	 * Number of times the driver requested a timer while an operation
	 * was in progress.
	 */
	SI_STAT_short_timeouts = 0,

	/*
	 * Number of times the driver requested a timer while nothing was in
	 * progress.
	 */
	SI_STAT_long_timeouts,

	/* Number of times the interface was idle while being polled. */
	SI_STAT_idles,

	/* Number of interrupts the driver handled. */
	SI_STAT_interrupts,

	/* Number of time the driver got an ATTN from the hardware. */
	SI_STAT_attentions,

	/* Number of times the driver requested flags from the hardware. */
	SI_STAT_flag_fetches,

	/* Number of times the hardware didn't follow the state machine. */
	SI_STAT_hosed_count,

	/* Number of completed messages. */
	SI_STAT_complete_transactions,

	/* Number of IPMI events received from the hardware. */
	SI_STAT_events,

	/* Number of watchdog pretimeouts. */
	SI_STAT_watchdog_pretimeouts,

	/* Number of asynchronous messages received. */
	SI_STAT_incoming_messages,


	/* This *must* remain last, add new values above this. */
	SI_NUM_STATS
};

struct smi_info {
	int                    intf_num;
	ipmi_smi_t             intf;
	struct si_sm_data      *si_sm;
	const struct si_sm_handlers *handlers;
	spinlock_t             si_lock;
	struct ipmi_smi_msg    *waiting_msg;
	struct ipmi_smi_msg    *curr_msg;
	enum si_intf_state     si_state;

	/*
	 * Used to handle the various types of I/O that can occur with
	 * IPMI
	 */
	struct si_sm_io io;

	/*
	 * Per-OEM handler, called from handle_flags().  Returns 1
	 * when handle_flags() needs to be re-run or 0 indicating it
	 * set si_state itself.
	 */
	int (*oem_data_avail_handler)(struct smi_info *smi_info);

	/*
	 * Flags from the last GET_MSG_FLAGS command, used when an ATTN
	 * is set to hold the flags until we are done handling everything
	 * from the flags.
	 */
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

	/* Does the BMC have an event buffer? */
	bool		    has_event_buffer;

	/*
	 * If set to true, this will request events the next time the
	 * state machine is idle.
	 */
	atomic_t            req_events;

	/*
	 * If true, run the state machine to completion on every send
	 * call.  Generally used after a panic to make sure stuff goes
	 * out.
	 */
	bool                run_to_completion;

	/* The timer for this si. */
	struct timer_list   si_timer;

	/* This flag is set, if the timer can be set */
	bool		    timer_can_start;

	/* This flag is set, if the timer is running (timer_pending() isn't enough) */
	bool		    timer_running;

	/* The time (in jiffies) the last timeout occurred at. */
	unsigned long       last_timeout_jiffies;

	/* Are we waiting for the events, pretimeouts, received msgs? */
	atomic_t            need_watch;

	/*
	 * The driver will disable interrupts when it gets into a
	 * situation where it cannot handle messages due to lack of
	 * memory.  Once that situation clears up, it will re-enable
	 * interrupts.
	 */
	bool interrupt_disabled;

	/*
	 * Does the BMC support events?
	 */
	bool supports_event_msg_buff;

	/*
	 * Can we disable interrupts the global enables receive irq
	 * bit?  There are currently two forms of brokenness, some
	 * systems cannot disable the bit (which is technically within
	 * the spec but a bad idea) and some systems have the bit
	 * forced to zero even though interrupts work (which is
	 * clearly outside the spec).  The next bool tells which form
	 * of brokenness is present.
	 */
	bool cannot_disable_irq;

	/*
	 * Some systems are broken and cannot set the irq enable
	 * bit, even if they support interrupts.
	 */
	bool irq_enable_broken;

	/*
	 * Did we get an attention that we did not handle?
	 */
	bool got_attn;

	/* From the get device id response... */
	struct ipmi_device_id device_id;

	/* Default driver model device. */
	struct platform_device *pdev;

	/* Counters and things for the proc filesystem. */
	atomic_t stats[SI_NUM_STATS];

	struct task_struct *thread;

	struct list_head link;
};

#define smi_inc_stat(smi, stat) \
	atomic_inc(&(smi)->stats[SI_STAT_ ## stat])
#define smi_get_stat(smi, stat) \
	((unsigned int) atomic_read(&(smi)->stats[SI_STAT_ ## stat]))

#define IPMI_MAX_INTFS 4
static int force_kipmid[IPMI_MAX_INTFS];
static int num_force_kipmid;

static unsigned int kipmid_max_busy_us[IPMI_MAX_INTFS];
static int num_max_busy_us;

static bool unload_when_empty = true;

static int try_smi_init(struct smi_info *smi);
static void cleanup_one_si(struct smi_info *to_clean);
static void cleanup_ipmi_si(void);

#ifdef DEBUG_TIMING
void debug_timestamp(char *msg)
{
	struct timespec64 t;

	getnstimeofday64(&t);
	pr_debug("**%s: %lld.%9.9ld\n", msg, (long long) t.tv_sec, t.tv_nsec);
}
#else
#define debug_timestamp(x)
#endif

static ATOMIC_NOTIFIER_HEAD(xaction_notifier_list);
static int register_xaction_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&xaction_notifier_list, nb);
}

static void deliver_recv_msg(struct smi_info *smi_info,
			     struct ipmi_smi_msg *msg)
{
	/* Deliver the message to the upper layer. */
	if (smi_info->intf)
		ipmi_smi_msg_received(smi_info->intf, msg);
	else
		ipmi_free_smi_msg(msg);
}

static void return_hosed_msg(struct smi_info *smi_info, int cCode)
{
	struct ipmi_smi_msg *msg = smi_info->curr_msg;

	if (cCode < 0 || cCode > IPMI_ERR_UNSPECIFIED)
		cCode = IPMI_ERR_UNSPECIFIED;
	/* else use it as is */

	/* Make it a response */
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

	if (!smi_info->waiting_msg) {
		smi_info->curr_msg = NULL;
		rv = SI_SM_IDLE;
	} else {
		int err;

		smi_info->curr_msg = smi_info->waiting_msg;
		smi_info->waiting_msg = NULL;
		debug_timestamp("Start2");
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
		if (err)
			return_hosed_msg(smi_info, err);

		rv = SI_SM_CALL_WITHOUT_DELAY;
	}
out:
	return rv;
}

static void smi_mod_timer(struct smi_info *smi_info, unsigned long new_val)
{
	if (!smi_info->timer_can_start)
		return;
	smi_info->last_timeout_jiffies = jiffies;
	mod_timer(&smi_info->si_timer, new_val);
	smi_info->timer_running = true;
}

/*
 * Start a new message and (re)start the timer and thread.
 */
static void start_new_msg(struct smi_info *smi_info, unsigned char *msg,
			  unsigned int size)
{
	smi_mod_timer(smi_info, jiffies + SI_TIMEOUT_JIFFIES);

	if (smi_info->thread)
		wake_up_process(smi_info->thread);

	smi_info->handlers->start_transaction(smi_info->si_sm, msg, size);
}

static void start_check_enables(struct smi_info *smi_info)
{
	unsigned char msg[2];

	msg[0] = (IPMI_NETFN_APP_REQUEST << 2);
	msg[1] = IPMI_GET_BMC_GLOBAL_ENABLES_CMD;

	start_new_msg(smi_info, msg, 2);
	smi_info->si_state = SI_CHECKING_ENABLES;
}

static void start_clear_flags(struct smi_info *smi_info)
{
	unsigned char msg[3];

	/* Make sure the watchdog pre-timeout flag is not set at startup. */
	msg[0] = (IPMI_NETFN_APP_REQUEST << 2);
	msg[1] = IPMI_CLEAR_MSG_FLAGS_CMD;
	msg[2] = WDT_PRE_TIMEOUT_INT;

	start_new_msg(smi_info, msg, 3);
	smi_info->si_state = SI_CLEARING_FLAGS;
}

static void start_getting_msg_queue(struct smi_info *smi_info)
{
	smi_info->curr_msg->data[0] = (IPMI_NETFN_APP_REQUEST << 2);
	smi_info->curr_msg->data[1] = IPMI_GET_MSG_CMD;
	smi_info->curr_msg->data_size = 2;

	start_new_msg(smi_info, smi_info->curr_msg->data,
		      smi_info->curr_msg->data_size);
	smi_info->si_state = SI_GETTING_MESSAGES;
}

static void start_getting_events(struct smi_info *smi_info)
{
	smi_info->curr_msg->data[0] = (IPMI_NETFN_APP_REQUEST << 2);
	smi_info->curr_msg->data[1] = IPMI_READ_EVENT_MSG_BUFFER_CMD;
	smi_info->curr_msg->data_size = 2;

	start_new_msg(smi_info, smi_info->curr_msg->data,
		      smi_info->curr_msg->data_size);
	smi_info->si_state = SI_GETTING_EVENTS;
}

/*
 * When we have a situtaion where we run out of memory and cannot
 * allocate messages, we just leave them in the BMC and run the system
 * polled until we can allocate some memory.  Once we have some
 * memory, we will re-enable the interrupt.
 *
 * Note that we cannot just use disable_irq(), since the interrupt may
 * be shared.
 */
static inline bool disable_si_irq(struct smi_info *smi_info)
{
	if ((smi_info->io.irq) && (!smi_info->interrupt_disabled)) {
		smi_info->interrupt_disabled = true;
		start_check_enables(smi_info);
		return true;
	}
	return false;
}

static inline bool enable_si_irq(struct smi_info *smi_info)
{
	if ((smi_info->io.irq) && (smi_info->interrupt_disabled)) {
		smi_info->interrupt_disabled = false;
		start_check_enables(smi_info);
		return true;
	}
	return false;
}

/*
 * Allocate a message.  If unable to allocate, start the interrupt
 * disable process and return NULL.  If able to allocate but
 * interrupts are disabled, free the message and return NULL after
 * starting the interrupt enable process.
 */
static struct ipmi_smi_msg *alloc_msg_handle_irq(struct smi_info *smi_info)
{
	struct ipmi_smi_msg *msg;

	msg = ipmi_alloc_smi_msg();
	if (!msg) {
		if (!disable_si_irq(smi_info))
			smi_info->si_state = SI_NORMAL;
	} else if (enable_si_irq(smi_info)) {
		ipmi_free_smi_msg(msg);
		msg = NULL;
	}
	return msg;
}

static void handle_flags(struct smi_info *smi_info)
{
retry:
	if (smi_info->msg_flags & WDT_PRE_TIMEOUT_INT) {
		/* Watchdog pre-timeout */
		smi_inc_stat(smi_info, watchdog_pretimeouts);

		start_clear_flags(smi_info);
		smi_info->msg_flags &= ~WDT_PRE_TIMEOUT_INT;
		if (smi_info->intf)
			ipmi_smi_watchdog_pretimeout(smi_info->intf);
	} else if (smi_info->msg_flags & RECEIVE_MSG_AVAIL) {
		/* Messages available. */
		smi_info->curr_msg = alloc_msg_handle_irq(smi_info);
		if (!smi_info->curr_msg)
			return;

		start_getting_msg_queue(smi_info);
	} else if (smi_info->msg_flags & EVENT_MSG_BUFFER_FULL) {
		/* Events available. */
		smi_info->curr_msg = alloc_msg_handle_irq(smi_info);
		if (!smi_info->curr_msg)
			return;

		start_getting_events(smi_info);
	} else if (smi_info->msg_flags & OEM_DATA_AVAIL &&
		   smi_info->oem_data_avail_handler) {
		if (smi_info->oem_data_avail_handler(smi_info))
			goto retry;
	} else
		smi_info->si_state = SI_NORMAL;
}

/*
 * Global enables we care about.
 */
#define GLOBAL_ENABLES_MASK (IPMI_BMC_EVT_MSG_BUFF | IPMI_BMC_RCV_MSG_INTR | \
			     IPMI_BMC_EVT_MSG_INTR)

static u8 current_global_enables(struct smi_info *smi_info, u8 base,
				 bool *irq_on)
{
	u8 enables = 0;

	if (smi_info->supports_event_msg_buff)
		enables |= IPMI_BMC_EVT_MSG_BUFF;

	if (((smi_info->io.irq && !smi_info->interrupt_disabled) ||
	     smi_info->cannot_disable_irq) &&
	    !smi_info->irq_enable_broken)
		enables |= IPMI_BMC_RCV_MSG_INTR;

	if (smi_info->supports_event_msg_buff &&
	    smi_info->io.irq && !smi_info->interrupt_disabled &&
	    !smi_info->irq_enable_broken)
		enables |= IPMI_BMC_EVT_MSG_INTR;

	*irq_on = enables & (IPMI_BMC_EVT_MSG_INTR | IPMI_BMC_RCV_MSG_INTR);

	return enables;
}

static void check_bt_irq(struct smi_info *smi_info, bool irq_on)
{
	u8 irqstate = smi_info->io.inputb(&smi_info->io, IPMI_BT_INTMASK_REG);

	irqstate &= IPMI_BT_INTMASK_ENABLE_IRQ_BIT;

	if ((bool)irqstate == irq_on)
		return;

	if (irq_on)
		smi_info->io.outputb(&smi_info->io, IPMI_BT_INTMASK_REG,
				     IPMI_BT_INTMASK_ENABLE_IRQ_BIT);
	else
		smi_info->io.outputb(&smi_info->io, IPMI_BT_INTMASK_REG, 0);
}

static void handle_transaction_done(struct smi_info *smi_info)
{
	struct ipmi_smi_msg *msg;

	debug_timestamp("Done");
	switch (smi_info->si_state) {
	case SI_NORMAL:
		if (!smi_info->curr_msg)
			break;

		smi_info->curr_msg->rsp_size
			= smi_info->handlers->get_result(
				smi_info->si_sm,
				smi_info->curr_msg->rsp,
				IPMI_MAX_MSG_LENGTH);

		/*
		 * Do this here becase deliver_recv_msg() releases the
		 * lock, and a new message can be put in during the
		 * time the lock is released.
		 */
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
			/* Error fetching flags, just give up for now. */
			smi_info->si_state = SI_NORMAL;
		} else if (len < 4) {
			/*
			 * Hmm, no flags.  That's technically illegal, but
			 * don't use uninitialized data.
			 */
			smi_info->si_state = SI_NORMAL;
		} else {
			smi_info->msg_flags = msg[3];
			handle_flags(smi_info);
		}
		break;
	}

	case SI_CLEARING_FLAGS:
	{
		unsigned char msg[3];

		/* We cleared the flags. */
		smi_info->handlers->get_result(smi_info->si_sm, msg, 3);
		if (msg[2] != 0) {
			/* Error clearing flags */
			dev_warn(smi_info->io.dev,
				 "Error clearing flags: %2.2x\n", msg[2]);
		}
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

		/*
		 * Do this here becase deliver_recv_msg() releases the
		 * lock, and a new message can be put in during the
		 * time the lock is released.
		 */
		msg = smi_info->curr_msg;
		smi_info->curr_msg = NULL;
		if (msg->rsp[2] != 0) {
			/* Error getting event, probably done. */
			msg->done(msg);

			/* Take off the event flag. */
			smi_info->msg_flags &= ~EVENT_MSG_BUFFER_FULL;
			handle_flags(smi_info);
		} else {
			smi_inc_stat(smi_info, events);

			/*
			 * Do this before we deliver the message
			 * because delivering the message releases the
			 * lock and something else can mess with the
			 * state.
			 */
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

		/*
		 * Do this here becase deliver_recv_msg() releases the
		 * lock, and a new message can be put in during the
		 * time the lock is released.
		 */
		msg = smi_info->curr_msg;
		smi_info->curr_msg = NULL;
		if (msg->rsp[2] != 0) {
			/* Error getting event, probably done. */
			msg->done(msg);

			/* Take off the msg flag. */
			smi_info->msg_flags &= ~RECEIVE_MSG_AVAIL;
			handle_flags(smi_info);
		} else {
			smi_inc_stat(smi_info, incoming_messages);

			/*
			 * Do this before we deliver the message
			 * because delivering the message releases the
			 * lock and something else can mess with the
			 * state.
			 */
			handle_flags(smi_info);

			deliver_recv_msg(smi_info, msg);
		}
		break;
	}

	case SI_CHECKING_ENABLES:
	{
		unsigned char msg[4];
		u8 enables;
		bool irq_on;

		/* We got the flags from the SMI, now handle them. */
		smi_info->handlers->get_result(smi_info->si_sm, msg, 4);
		if (msg[2] != 0) {
			dev_warn(smi_info->io.dev,
				 "Couldn't get irq info: %x.\n", msg[2]);
			dev_warn(smi_info->io.dev,
				 "Maybe ok, but ipmi might run very slowly.\n");
			smi_info->si_state = SI_NORMAL;
			break;
		}
		enables = current_global_enables(smi_info, 0, &irq_on);
		if (smi_info->io.si_type == SI_BT)
			/* BT has its own interrupt enable bit. */
			check_bt_irq(smi_info, irq_on);
		if (enables != (msg[3] & GLOBAL_ENABLES_MASK)) {
			/* Enables are not correct, fix them. */
			msg[0] = (IPMI_NETFN_APP_REQUEST << 2);
			msg[1] = IPMI_SET_BMC_GLOBAL_ENABLES_CMD;
			msg[2] = enables | (msg[3] & ~GLOBAL_ENABLES_MASK);
			smi_info->handlers->start_transaction(
				smi_info->si_sm, msg, 3);
			smi_info->si_state = SI_SETTING_ENABLES;
		} else if (smi_info->supports_event_msg_buff) {
			smi_info->curr_msg = ipmi_alloc_smi_msg();
			if (!smi_info->curr_msg) {
				smi_info->si_state = SI_NORMAL;
				break;
			}
			start_getting_events(smi_info);
		} else {
			smi_info->si_state = SI_NORMAL;
		}
		break;
	}

	case SI_SETTING_ENABLES:
	{
		unsigned char msg[4];

		smi_info->handlers->get_result(smi_info->si_sm, msg, 4);
		if (msg[2] != 0)
			dev_warn(smi_info->io.dev,
				 "Could not set the global enables: 0x%x.\n",
				 msg[2]);

		if (smi_info->supports_event_msg_buff) {
			smi_info->curr_msg = ipmi_alloc_smi_msg();
			if (!smi_info->curr_msg) {
				smi_info->si_state = SI_NORMAL;
				break;
			}
			start_getting_events(smi_info);
		} else {
			smi_info->si_state = SI_NORMAL;
		}
		break;
	}
	}
}

/*
 * Called on timeouts and events.  Timeouts should pass the elapsed
 * time, interrupts should pass in zero.  Must be called with
 * si_lock held and interrupts disabled.
 */
static enum si_sm_result smi_event_handler(struct smi_info *smi_info,
					   int time)
{
	enum si_sm_result si_sm_result;

restart:
	/*
	 * There used to be a loop here that waited a little while
	 * (around 25us) before giving up.  That turned out to be
	 * pointless, the minimum delays I was seeing were in the 300us
	 * range, which is far too long to wait in an interrupt.  So
	 * we just run until the state machine tells us something
	 * happened or it needs a delay.
	 */
	si_sm_result = smi_info->handlers->event(smi_info->si_sm, time);
	time = 0;
	while (si_sm_result == SI_SM_CALL_WITHOUT_DELAY)
		si_sm_result = smi_info->handlers->event(smi_info->si_sm, 0);

	if (si_sm_result == SI_SM_TRANSACTION_COMPLETE) {
		smi_inc_stat(smi_info, complete_transactions);

		handle_transaction_done(smi_info);
		goto restart;
	} else if (si_sm_result == SI_SM_HOSED) {
		smi_inc_stat(smi_info, hosed_count);

		/*
		 * Do the before return_hosed_msg, because that
		 * releases the lock.
		 */
		smi_info->si_state = SI_NORMAL;
		if (smi_info->curr_msg != NULL) {
			/*
			 * If we were handling a user message, format
			 * a response to send to the upper layer to
			 * tell it about the error.
			 */
			return_hosed_msg(smi_info, IPMI_ERR_UNSPECIFIED);
		}
		goto restart;
	}

	/*
	 * We prefer handling attn over new messages.  But don't do
	 * this if there is not yet an upper layer to handle anything.
	 */
	if (likely(smi_info->intf) &&
	    (si_sm_result == SI_SM_ATTN || smi_info->got_attn)) {
		unsigned char msg[2];

		if (smi_info->si_state != SI_NORMAL) {
			/*
			 * We got an ATTN, but we are doing something else.
			 * Handle the ATTN later.
			 */
			smi_info->got_attn = true;
		} else {
			smi_info->got_attn = false;
			smi_inc_stat(smi_info, attentions);

			/*
			 * Got a attn, send down a get message flags to see
			 * what's causing it.  It would be better to handle
			 * this in the upper layer, but due to the way
			 * interrupts work with the SMI, that's not really
			 * possible.
			 */
			msg[0] = (IPMI_NETFN_APP_REQUEST << 2);
			msg[1] = IPMI_GET_MSG_FLAGS_CMD;

			start_new_msg(smi_info, msg, 2);
			smi_info->si_state = SI_GETTING_FLAGS;
			goto restart;
		}
	}

	/* If we are currently idle, try to start the next message. */
	if (si_sm_result == SI_SM_IDLE) {
		smi_inc_stat(smi_info, idles);

		si_sm_result = start_next_msg(smi_info);
		if (si_sm_result != SI_SM_IDLE)
			goto restart;
	}

	if ((si_sm_result == SI_SM_IDLE)
	    && (atomic_read(&smi_info->req_events))) {
		/*
		 * We are idle and the upper layer requested that I fetch
		 * events, so do so.
		 */
		atomic_set(&smi_info->req_events, 0);

		/*
		 * Take this opportunity to check the interrupt and
		 * message enable state for the BMC.  The BMC can be
		 * asynchronously reset, and may thus get interrupts
		 * disable and messages disabled.
		 */
		if (smi_info->supports_event_msg_buff || smi_info->io.irq) {
			start_check_enables(smi_info);
		} else {
			smi_info->curr_msg = alloc_msg_handle_irq(smi_info);
			if (!smi_info->curr_msg)
				goto out;

			start_getting_events(smi_info);
		}
		goto restart;
	}

	if (si_sm_result == SI_SM_IDLE && smi_info->timer_running) {
		/* Ok it if fails, the timer will just go off. */
		if (del_timer(&smi_info->si_timer))
			smi_info->timer_running = false;
	}

out:
	return si_sm_result;
}

static void check_start_timer_thread(struct smi_info *smi_info)
{
	if (smi_info->si_state == SI_NORMAL && smi_info->curr_msg == NULL) {
		smi_mod_timer(smi_info, jiffies + SI_TIMEOUT_JIFFIES);

		if (smi_info->thread)
			wake_up_process(smi_info->thread);

		start_next_msg(smi_info);
		smi_event_handler(smi_info, 0);
	}
}

static void flush_messages(void *send_info)
{
	struct smi_info *smi_info = send_info;
	enum si_sm_result result;

	/*
	 * Currently, this function is called only in run-to-completion
	 * mode.  This means we are single-threaded, no need for locks.
	 */
	result = smi_event_handler(smi_info, 0);
	while (result != SI_SM_IDLE) {
		udelay(SI_SHORT_TIMEOUT_USEC);
		result = smi_event_handler(smi_info, SI_SHORT_TIMEOUT_USEC);
	}
}

static void sender(void                *send_info,
		   struct ipmi_smi_msg *msg)
{
	struct smi_info   *smi_info = send_info;
	unsigned long     flags;

	debug_timestamp("Enqueue");

	if (smi_info->run_to_completion) {
		/*
		 * If we are running to completion, start it.  Upper
		 * layer will call flush_messages to clear it out.
		 */
		smi_info->waiting_msg = msg;
		return;
	}

	spin_lock_irqsave(&smi_info->si_lock, flags);
	/*
	 * The following two lines don't need to be under the lock for
	 * the lock's sake, but they do need SMP memory barriers to
	 * avoid getting things out of order.  We are already claiming
	 * the lock, anyway, so just do it under the lock to avoid the
	 * ordering problem.
	 */
	BUG_ON(smi_info->waiting_msg);
	smi_info->waiting_msg = msg;
	check_start_timer_thread(smi_info);
	spin_unlock_irqrestore(&smi_info->si_lock, flags);
}

static void set_run_to_completion(void *send_info, bool i_run_to_completion)
{
	struct smi_info   *smi_info = send_info;

	smi_info->run_to_completion = i_run_to_completion;
	if (i_run_to_completion)
		flush_messages(smi_info);
}

/*
 * Use -1 in the nsec value of the busy waiting timespec to tell that
 * we are spinning in kipmid looking for something and not delaying
 * between checks
 */
static inline void ipmi_si_set_not_busy(struct timespec64 *ts)
{
	ts->tv_nsec = -1;
}
static inline int ipmi_si_is_busy(struct timespec64 *ts)
{
	return ts->tv_nsec != -1;
}

static inline int ipmi_thread_busy_wait(enum si_sm_result smi_result,
					const struct smi_info *smi_info,
					struct timespec64 *busy_until)
{
	unsigned int max_busy_us = 0;

	if (smi_info->intf_num < num_max_busy_us)
		max_busy_us = kipmid_max_busy_us[smi_info->intf_num];
	if (max_busy_us == 0 || smi_result != SI_SM_CALL_WITH_DELAY)
		ipmi_si_set_not_busy(busy_until);
	else if (!ipmi_si_is_busy(busy_until)) {
		getnstimeofday64(busy_until);
		timespec64_add_ns(busy_until, max_busy_us*NSEC_PER_USEC);
	} else {
		struct timespec64 now;

		getnstimeofday64(&now);
		if (unlikely(timespec64_compare(&now, busy_until) > 0)) {
			ipmi_si_set_not_busy(busy_until);
			return 0;
		}
	}
	return 1;
}


/*
 * A busy-waiting loop for speeding up IPMI operation.
 *
 * Lousy hardware makes this hard.  This is only enabled for systems
 * that are not BT and do not have interrupts.  It starts spinning
 * when an operation is complete or until max_busy tells it to stop
 * (if that is enabled).  See the paragraph on kimid_max_busy_us in
 * Documentation/IPMI.txt for details.
 */
static int ipmi_thread(void *data)
{
	struct smi_info *smi_info = data;
	unsigned long flags;
	enum si_sm_result smi_result;
	struct timespec64 busy_until;

	ipmi_si_set_not_busy(&busy_until);
	set_user_nice(current, MAX_NICE);
	while (!kthread_should_stop()) {
		int busy_wait;

		spin_lock_irqsave(&(smi_info->si_lock), flags);
		smi_result = smi_event_handler(smi_info, 0);

		/*
		 * If the driver is doing something, there is a possible
		 * race with the timer.  If the timer handler see idle,
		 * and the thread here sees something else, the timer
		 * handler won't restart the timer even though it is
		 * required.  So start it here if necessary.
		 */
		if (smi_result != SI_SM_IDLE && !smi_info->timer_running)
			smi_mod_timer(smi_info, jiffies + SI_TIMEOUT_JIFFIES);

		spin_unlock_irqrestore(&(smi_info->si_lock), flags);
		busy_wait = ipmi_thread_busy_wait(smi_result, smi_info,
						  &busy_until);
		if (smi_result == SI_SM_CALL_WITHOUT_DELAY)
			; /* do nothing */
		else if (smi_result == SI_SM_CALL_WITH_DELAY && busy_wait)
			schedule();
		else if (smi_result == SI_SM_IDLE) {
			if (atomic_read(&smi_info->need_watch)) {
				schedule_timeout_interruptible(100);
			} else {
				/* Wait to be woken up when we are needed. */
				__set_current_state(TASK_INTERRUPTIBLE);
				schedule();
			}
		} else
			schedule_timeout_interruptible(1);
	}
	return 0;
}


static void poll(void *send_info)
{
	struct smi_info *smi_info = send_info;
	unsigned long flags = 0;
	bool run_to_completion = smi_info->run_to_completion;

	/*
	 * Make sure there is some delay in the poll loop so we can
	 * drive time forward and timeout things.
	 */
	udelay(10);
	if (!run_to_completion)
		spin_lock_irqsave(&smi_info->si_lock, flags);
	smi_event_handler(smi_info, 10);
	if (!run_to_completion)
		spin_unlock_irqrestore(&smi_info->si_lock, flags);
}

static void request_events(void *send_info)
{
	struct smi_info *smi_info = send_info;

	if (!smi_info->has_event_buffer)
		return;

	atomic_set(&smi_info->req_events, 1);
}

static void set_need_watch(void *send_info, bool enable)
{
	struct smi_info *smi_info = send_info;
	unsigned long flags;

	atomic_set(&smi_info->need_watch, enable);
	spin_lock_irqsave(&smi_info->si_lock, flags);
	check_start_timer_thread(smi_info);
	spin_unlock_irqrestore(&smi_info->si_lock, flags);
}

static void smi_timeout(unsigned long data)
{
	struct smi_info   *smi_info = (struct smi_info *) data;
	enum si_sm_result smi_result;
	unsigned long     flags;
	unsigned long     jiffies_now;
	long              time_diff;
	long		  timeout;

	spin_lock_irqsave(&(smi_info->si_lock), flags);
	debug_timestamp("Timer");

	jiffies_now = jiffies;
	time_diff = (((long)jiffies_now - (long)smi_info->last_timeout_jiffies)
		     * SI_USEC_PER_JIFFY);
	smi_result = smi_event_handler(smi_info, time_diff);

	if ((smi_info->io.irq) && (!smi_info->interrupt_disabled)) {
		/* Running with interrupts, only do long timeouts. */
		timeout = jiffies + SI_TIMEOUT_JIFFIES;
		smi_inc_stat(smi_info, long_timeouts);
		goto do_mod_timer;
	}

	/*
	 * If the state machine asks for a short delay, then shorten
	 * the timer timeout.
	 */
	if (smi_result == SI_SM_CALL_WITH_DELAY) {
		smi_inc_stat(smi_info, short_timeouts);
		timeout = jiffies + 1;
	} else {
		smi_inc_stat(smi_info, long_timeouts);
		timeout = jiffies + SI_TIMEOUT_JIFFIES;
	}

do_mod_timer:
	if (smi_result != SI_SM_IDLE)
		smi_mod_timer(smi_info, timeout);
	else
		smi_info->timer_running = false;
	spin_unlock_irqrestore(&(smi_info->si_lock), flags);
}

irqreturn_t ipmi_si_irq_handler(int irq, void *data)
{
	struct smi_info *smi_info = data;
	unsigned long   flags;

	if (smi_info->io.si_type == SI_BT)
		/* We need to clear the IRQ flag for the BT interface. */
		smi_info->io.outputb(&smi_info->io, IPMI_BT_INTMASK_REG,
				     IPMI_BT_INTMASK_CLEAR_IRQ_BIT
				     | IPMI_BT_INTMASK_ENABLE_IRQ_BIT);

	spin_lock_irqsave(&(smi_info->si_lock), flags);

	smi_inc_stat(smi_info, interrupts);

	debug_timestamp("Interrupt");

	smi_event_handler(smi_info, 0);
	spin_unlock_irqrestore(&(smi_info->si_lock), flags);
	return IRQ_HANDLED;
}

static int smi_start_processing(void       *send_info,
				ipmi_smi_t intf)
{
	struct smi_info *new_smi = send_info;
	int             enable = 0;

	new_smi->intf = intf;

	/* Set up the timer that drives the interface. */
	setup_timer(&new_smi->si_timer, smi_timeout, (long)new_smi);
	new_smi->timer_can_start = true;
	smi_mod_timer(new_smi, jiffies + SI_TIMEOUT_JIFFIES);

	/* Try to claim any interrupts. */
	if (new_smi->io.irq_setup) {
		new_smi->io.irq_handler_data = new_smi;
		new_smi->io.irq_setup(&new_smi->io);
	}

	/*
	 * Check if the user forcefully enabled the daemon.
	 */
	if (new_smi->intf_num < num_force_kipmid)
		enable = force_kipmid[new_smi->intf_num];
	/*
	 * The BT interface is efficient enough to not need a thread,
	 * and there is no need for a thread if we have interrupts.
	 */
	else if ((new_smi->io.si_type != SI_BT) && (!new_smi->io.irq))
		enable = 1;

	if (enable) {
		new_smi->thread = kthread_run(ipmi_thread, new_smi,
					      "kipmi%d", new_smi->intf_num);
		if (IS_ERR(new_smi->thread)) {
			dev_notice(new_smi->io.dev, "Could not start"
				   " kernel thread due to error %ld, only using"
				   " timers to drive the interface\n",
				   PTR_ERR(new_smi->thread));
			new_smi->thread = NULL;
		}
	}

	return 0;
}

static int get_smi_info(void *send_info, struct ipmi_smi_info *data)
{
	struct smi_info *smi = send_info;

	data->addr_src = smi->io.addr_source;
	data->dev = smi->io.dev;
	data->addr_info = smi->io.addr_info;
	get_device(smi->io.dev);

	return 0;
}

static void set_maintenance_mode(void *send_info, bool enable)
{
	struct smi_info   *smi_info = send_info;

	if (!enable)
		atomic_set(&smi_info->req_events, 0);
}

static const struct ipmi_smi_handlers handlers = {
	.owner                  = THIS_MODULE,
	.start_processing       = smi_start_processing,
	.get_smi_info		= get_smi_info,
	.sender			= sender,
	.request_events		= request_events,
	.set_need_watch		= set_need_watch,
	.set_maintenance_mode   = set_maintenance_mode,
	.set_run_to_completion  = set_run_to_completion,
	.flush_messages		= flush_messages,
	.poll			= poll,
};

static LIST_HEAD(smi_infos);
static DEFINE_MUTEX(smi_infos_lock);
static int smi_num; /* Used to sequence the SMIs */

static const char * const addr_space_to_str[] = { "i/o", "mem" };

module_param_array(force_kipmid, int, &num_force_kipmid, 0);
MODULE_PARM_DESC(force_kipmid, "Force the kipmi daemon to be enabled (1) or"
		 " disabled(0).  Normally the IPMI driver auto-detects"
		 " this, but the value may be overridden by this parm.");
module_param(unload_when_empty, bool, 0);
MODULE_PARM_DESC(unload_when_empty, "Unload the module if no interfaces are"
		 " specified or found, default is 1.  Setting to 0"
		 " is useful for hot add of devices using hotmod.");
module_param_array(kipmid_max_busy_us, uint, &num_max_busy_us, 0644);
MODULE_PARM_DESC(kipmid_max_busy_us,
		 "Max time (in microseconds) to busy-wait for IPMI data before"
		 " sleeping. 0 (default) means to wait forever. Set to 100-500"
		 " if kipmid is using up a lot of CPU time.");

void ipmi_irq_finish_setup(struct si_sm_io *io)
{
	if (io->si_type == SI_BT)
		/* Enable the interrupt in the BT interface. */
		io->outputb(io, IPMI_BT_INTMASK_REG,
			    IPMI_BT_INTMASK_ENABLE_IRQ_BIT);
}

void ipmi_irq_start_cleanup(struct si_sm_io *io)
{
	if (io->si_type == SI_BT)
		/* Disable the interrupt in the BT interface. */
		io->outputb(io, IPMI_BT_INTMASK_REG, 0);
}

static void std_irq_cleanup(struct si_sm_io *io)
{
	ipmi_irq_start_cleanup(io);
	free_irq(io->irq, io->irq_handler_data);
}

int ipmi_std_irq_setup(struct si_sm_io *io)
{
	int rv;

	if (!io->irq)
		return 0;

	rv = request_irq(io->irq,
			 ipmi_si_irq_handler,
			 IRQF_SHARED,
			 DEVICE_NAME,
			 io->irq_handler_data);
	if (rv) {
		dev_warn(io->dev, "%s unable to claim interrupt %d,"
			 " running polled\n",
			 DEVICE_NAME, io->irq);
		io->irq = 0;
	} else {
		io->irq_cleanup = std_irq_cleanup;
		ipmi_irq_finish_setup(io);
		dev_info(io->dev, "Using irq %d\n", io->irq);
	}

	return rv;
}

static int wait_for_msg_done(struct smi_info *smi_info)
{
	enum si_sm_result     smi_result;

	smi_result = smi_info->handlers->event(smi_info->si_sm, 0);
	for (;;) {
		if (smi_result == SI_SM_CALL_WITH_DELAY ||
		    smi_result == SI_SM_CALL_WITH_TICK_DELAY) {
			schedule_timeout_uninterruptible(1);
			smi_result = smi_info->handlers->event(
				smi_info->si_sm, jiffies_to_usecs(1));
		} else if (smi_result == SI_SM_CALL_WITHOUT_DELAY) {
			smi_result = smi_info->handlers->event(
				smi_info->si_sm, 0);
		} else
			break;
	}
	if (smi_result == SI_SM_HOSED)
		/*
		 * We couldn't get the state machine to run, so whatever's at
		 * the port is probably not an IPMI SMI interface.
		 */
		return -ENODEV;

	return 0;
}

static int try_get_dev_id(struct smi_info *smi_info)
{
	unsigned char         msg[2];
	unsigned char         *resp;
	unsigned long         resp_len;
	int                   rv = 0;

	resp = kmalloc(IPMI_MAX_MSG_LENGTH, GFP_KERNEL);
	if (!resp)
		return -ENOMEM;

	/*
	 * Do a Get Device ID command, since it comes back with some
	 * useful info.
	 */
	msg[0] = IPMI_NETFN_APP_REQUEST << 2;
	msg[1] = IPMI_GET_DEVICE_ID_CMD;
	smi_info->handlers->start_transaction(smi_info->si_sm, msg, 2);

	rv = wait_for_msg_done(smi_info);
	if (rv)
		goto out;

	resp_len = smi_info->handlers->get_result(smi_info->si_sm,
						  resp, IPMI_MAX_MSG_LENGTH);

	/* Check and record info from the get device id, in case we need it. */
	rv = ipmi_demangle_device_id(resp[0] >> 2, resp[1],
			resp + 2, resp_len - 2, &smi_info->device_id);

out:
	kfree(resp);
	return rv;
}

static int get_global_enables(struct smi_info *smi_info, u8 *enables)
{
	unsigned char         msg[3];
	unsigned char         *resp;
	unsigned long         resp_len;
	int                   rv;

	resp = kmalloc(IPMI_MAX_MSG_LENGTH, GFP_KERNEL);
	if (!resp)
		return -ENOMEM;

	msg[0] = IPMI_NETFN_APP_REQUEST << 2;
	msg[1] = IPMI_GET_BMC_GLOBAL_ENABLES_CMD;
	smi_info->handlers->start_transaction(smi_info->si_sm, msg, 2);

	rv = wait_for_msg_done(smi_info);
	if (rv) {
		dev_warn(smi_info->io.dev,
			 "Error getting response from get global enables command: %d\n",
			 rv);
		goto out;
	}

	resp_len = smi_info->handlers->get_result(smi_info->si_sm,
						  resp, IPMI_MAX_MSG_LENGTH);

	if (resp_len < 4 ||
			resp[0] != (IPMI_NETFN_APP_REQUEST | 1) << 2 ||
			resp[1] != IPMI_GET_BMC_GLOBAL_ENABLES_CMD   ||
			resp[2] != 0) {
		dev_warn(smi_info->io.dev,
			 "Invalid return from get global enables command: %ld %x %x %x\n",
			 resp_len, resp[0], resp[1], resp[2]);
		rv = -EINVAL;
		goto out;
	} else {
		*enables = resp[3];
	}

out:
	kfree(resp);
	return rv;
}

/*
 * Returns 1 if it gets an error from the command.
 */
static int set_global_enables(struct smi_info *smi_info, u8 enables)
{
	unsigned char         msg[3];
	unsigned char         *resp;
	unsigned long         resp_len;
	int                   rv;

	resp = kmalloc(IPMI_MAX_MSG_LENGTH, GFP_KERNEL);
	if (!resp)
		return -ENOMEM;

	msg[0] = IPMI_NETFN_APP_REQUEST << 2;
	msg[1] = IPMI_SET_BMC_GLOBAL_ENABLES_CMD;
	msg[2] = enables;
	smi_info->handlers->start_transaction(smi_info->si_sm, msg, 3);

	rv = wait_for_msg_done(smi_info);
	if (rv) {
		dev_warn(smi_info->io.dev,
			 "Error getting response from set global enables command: %d\n",
			 rv);
		goto out;
	}

	resp_len = smi_info->handlers->get_result(smi_info->si_sm,
						  resp, IPMI_MAX_MSG_LENGTH);

	if (resp_len < 3 ||
			resp[0] != (IPMI_NETFN_APP_REQUEST | 1) << 2 ||
			resp[1] != IPMI_SET_BMC_GLOBAL_ENABLES_CMD) {
		dev_warn(smi_info->io.dev,
			 "Invalid return from set global enables command: %ld %x %x\n",
			 resp_len, resp[0], resp[1]);
		rv = -EINVAL;
		goto out;
	}

	if (resp[2] != 0)
		rv = 1;

out:
	kfree(resp);
	return rv;
}

/*
 * Some BMCs do not support clearing the receive irq bit in the global
 * enables (even if they don't support interrupts on the BMC).  Check
 * for this and handle it properly.
 */
static void check_clr_rcv_irq(struct smi_info *smi_info)
{
	u8 enables = 0;
	int rv;

	rv = get_global_enables(smi_info, &enables);
	if (!rv) {
		if ((enables & IPMI_BMC_RCV_MSG_INTR) == 0)
			/* Already clear, should work ok. */
			return;

		enables &= ~IPMI_BMC_RCV_MSG_INTR;
		rv = set_global_enables(smi_info, enables);
	}

	if (rv < 0) {
		dev_err(smi_info->io.dev,
			"Cannot check clearing the rcv irq: %d\n", rv);
		return;
	}

	if (rv) {
		/*
		 * An error when setting the event buffer bit means
		 * clearing the bit is not supported.
		 */
		dev_warn(smi_info->io.dev,
			 "The BMC does not support clearing the recv irq bit, compensating, but the BMC needs to be fixed.\n");
		smi_info->cannot_disable_irq = true;
	}
}

/*
 * Some BMCs do not support setting the interrupt bits in the global
 * enables even if they support interrupts.  Clearly bad, but we can
 * compensate.
 */
static void check_set_rcv_irq(struct smi_info *smi_info)
{
	u8 enables = 0;
	int rv;

	if (!smi_info->io.irq)
		return;

	rv = get_global_enables(smi_info, &enables);
	if (!rv) {
		enables |= IPMI_BMC_RCV_MSG_INTR;
		rv = set_global_enables(smi_info, enables);
	}

	if (rv < 0) {
		dev_err(smi_info->io.dev,
			"Cannot check setting the rcv irq: %d\n", rv);
		return;
	}

	if (rv) {
		/*
		 * An error when setting the event buffer bit means
		 * setting the bit is not supported.
		 */
		dev_warn(smi_info->io.dev,
			 "The BMC does not support setting the recv irq bit, compensating, but the BMC needs to be fixed.\n");
		smi_info->cannot_disable_irq = true;
		smi_info->irq_enable_broken = true;
	}
}

static int try_enable_event_buffer(struct smi_info *smi_info)
{
	unsigned char         msg[3];
	unsigned char         *resp;
	unsigned long         resp_len;
	int                   rv = 0;

	resp = kmalloc(IPMI_MAX_MSG_LENGTH, GFP_KERNEL);
	if (!resp)
		return -ENOMEM;

	msg[0] = IPMI_NETFN_APP_REQUEST << 2;
	msg[1] = IPMI_GET_BMC_GLOBAL_ENABLES_CMD;
	smi_info->handlers->start_transaction(smi_info->si_sm, msg, 2);

	rv = wait_for_msg_done(smi_info);
	if (rv) {
		pr_warn(PFX "Error getting response from get global enables command, the event buffer is not enabled.\n");
		goto out;
	}

	resp_len = smi_info->handlers->get_result(smi_info->si_sm,
						  resp, IPMI_MAX_MSG_LENGTH);

	if (resp_len < 4 ||
			resp[0] != (IPMI_NETFN_APP_REQUEST | 1) << 2 ||
			resp[1] != IPMI_GET_BMC_GLOBAL_ENABLES_CMD   ||
			resp[2] != 0) {
		pr_warn(PFX "Invalid return from get global enables command, cannot enable the event buffer.\n");
		rv = -EINVAL;
		goto out;
	}

	if (resp[3] & IPMI_BMC_EVT_MSG_BUFF) {
		/* buffer is already enabled, nothing to do. */
		smi_info->supports_event_msg_buff = true;
		goto out;
	}

	msg[0] = IPMI_NETFN_APP_REQUEST << 2;
	msg[1] = IPMI_SET_BMC_GLOBAL_ENABLES_CMD;
	msg[2] = resp[3] | IPMI_BMC_EVT_MSG_BUFF;
	smi_info->handlers->start_transaction(smi_info->si_sm, msg, 3);

	rv = wait_for_msg_done(smi_info);
	if (rv) {
		pr_warn(PFX "Error getting response from set global, enables command, the event buffer is not enabled.\n");
		goto out;
	}

	resp_len = smi_info->handlers->get_result(smi_info->si_sm,
						  resp, IPMI_MAX_MSG_LENGTH);

	if (resp_len < 3 ||
			resp[0] != (IPMI_NETFN_APP_REQUEST | 1) << 2 ||
			resp[1] != IPMI_SET_BMC_GLOBAL_ENABLES_CMD) {
		pr_warn(PFX "Invalid return from get global, enables command, not enable the event buffer.\n");
		rv = -EINVAL;
		goto out;
	}

	if (resp[2] != 0)
		/*
		 * An error when setting the event buffer bit means
		 * that the event buffer is not supported.
		 */
		rv = -ENOENT;
	else
		smi_info->supports_event_msg_buff = true;

out:
	kfree(resp);
	return rv;
}

#ifdef CONFIG_IPMI_PROC_INTERFACE
static int smi_type_proc_show(struct seq_file *m, void *v)
{
	struct smi_info *smi = m->private;

	seq_printf(m, "%s\n", si_to_str[smi->io.si_type]);

	return 0;
}

static int smi_type_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, smi_type_proc_show, PDE_DATA(inode));
}

static const struct file_operations smi_type_proc_ops = {
	.open		= smi_type_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int smi_si_stats_proc_show(struct seq_file *m, void *v)
{
	struct smi_info *smi = m->private;

	seq_printf(m, "interrupts_enabled:    %d\n",
		       smi->io.irq && !smi->interrupt_disabled);
	seq_printf(m, "short_timeouts:        %u\n",
		       smi_get_stat(smi, short_timeouts));
	seq_printf(m, "long_timeouts:         %u\n",
		       smi_get_stat(smi, long_timeouts));
	seq_printf(m, "idles:                 %u\n",
		       smi_get_stat(smi, idles));
	seq_printf(m, "interrupts:            %u\n",
		       smi_get_stat(smi, interrupts));
	seq_printf(m, "attentions:            %u\n",
		       smi_get_stat(smi, attentions));
	seq_printf(m, "flag_fetches:          %u\n",
		       smi_get_stat(smi, flag_fetches));
	seq_printf(m, "hosed_count:           %u\n",
		       smi_get_stat(smi, hosed_count));
	seq_printf(m, "complete_transactions: %u\n",
		       smi_get_stat(smi, complete_transactions));
	seq_printf(m, "events:                %u\n",
		       smi_get_stat(smi, events));
	seq_printf(m, "watchdog_pretimeouts:  %u\n",
		       smi_get_stat(smi, watchdog_pretimeouts));
	seq_printf(m, "incoming_messages:     %u\n",
		       smi_get_stat(smi, incoming_messages));
	return 0;
}

static int smi_si_stats_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, smi_si_stats_proc_show, PDE_DATA(inode));
}

static const struct file_operations smi_si_stats_proc_ops = {
	.open		= smi_si_stats_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int smi_params_proc_show(struct seq_file *m, void *v)
{
	struct smi_info *smi = m->private;

	seq_printf(m,
		   "%s,%s,0x%lx,rsp=%d,rsi=%d,rsh=%d,irq=%d,ipmb=%d\n",
		   si_to_str[smi->io.si_type],
		   addr_space_to_str[smi->io.addr_type],
		   smi->io.addr_data,
		   smi->io.regspacing,
		   smi->io.regsize,
		   smi->io.regshift,
		   smi->io.irq,
		   smi->io.slave_addr);

	return 0;
}

static int smi_params_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, smi_params_proc_show, PDE_DATA(inode));
}

static const struct file_operations smi_params_proc_ops = {
	.open		= smi_params_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif

#define IPMI_SI_ATTR(name) \
static ssize_t ipmi_##name##_show(struct device *dev,			\
				  struct device_attribute *attr,	\
				  char *buf)				\
{									\
	struct smi_info *smi_info = dev_get_drvdata(dev);		\
									\
	return snprintf(buf, 10, "%u\n", smi_get_stat(smi_info, name));	\
}									\
static DEVICE_ATTR(name, S_IRUGO, ipmi_##name##_show, NULL)

static ssize_t ipmi_type_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct smi_info *smi_info = dev_get_drvdata(dev);

	return snprintf(buf, 10, "%s\n", si_to_str[smi_info->io.si_type]);
}
static DEVICE_ATTR(type, S_IRUGO, ipmi_type_show, NULL);

static ssize_t ipmi_interrupts_enabled_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct smi_info *smi_info = dev_get_drvdata(dev);
	int enabled = smi_info->io.irq && !smi_info->interrupt_disabled;

	return snprintf(buf, 10, "%d\n", enabled);
}
static DEVICE_ATTR(interrupts_enabled, S_IRUGO,
		   ipmi_interrupts_enabled_show, NULL);

IPMI_SI_ATTR(short_timeouts);
IPMI_SI_ATTR(long_timeouts);
IPMI_SI_ATTR(idles);
IPMI_SI_ATTR(interrupts);
IPMI_SI_ATTR(attentions);
IPMI_SI_ATTR(flag_fetches);
IPMI_SI_ATTR(hosed_count);
IPMI_SI_ATTR(complete_transactions);
IPMI_SI_ATTR(events);
IPMI_SI_ATTR(watchdog_pretimeouts);
IPMI_SI_ATTR(incoming_messages);

static ssize_t ipmi_params_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct smi_info *smi_info = dev_get_drvdata(dev);

	return snprintf(buf, 200,
			"%s,%s,0x%lx,rsp=%d,rsi=%d,rsh=%d,irq=%d,ipmb=%d\n",
			si_to_str[smi_info->io.si_type],
			addr_space_to_str[smi_info->io.addr_type],
			smi_info->io.addr_data,
			smi_info->io.regspacing,
			smi_info->io.regsize,
			smi_info->io.regshift,
			smi_info->io.irq,
			smi_info->io.slave_addr);
}
static DEVICE_ATTR(params, S_IRUGO, ipmi_params_show, NULL);

static struct attribute *ipmi_si_dev_attrs[] = {
	&dev_attr_type.attr,
	&dev_attr_interrupts_enabled.attr,
	&dev_attr_short_timeouts.attr,
	&dev_attr_long_timeouts.attr,
	&dev_attr_idles.attr,
	&dev_attr_interrupts.attr,
	&dev_attr_attentions.attr,
	&dev_attr_flag_fetches.attr,
	&dev_attr_hosed_count.attr,
	&dev_attr_complete_transactions.attr,
	&dev_attr_events.attr,
	&dev_attr_watchdog_pretimeouts.attr,
	&dev_attr_incoming_messages.attr,
	&dev_attr_params.attr,
	NULL
};

static const struct attribute_group ipmi_si_dev_attr_group = {
	.attrs		= ipmi_si_dev_attrs,
};

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
		} else if (ipmi_version_major(id) < 1 ||
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

	/* Make it a response */
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
	    smi_info->io.si_type == SI_BT)
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

static void check_for_broken_irqs(struct smi_info *smi_info)
{
	check_clr_rcv_irq(smi_info);
	check_set_rcv_irq(smi_info);
}

static inline void stop_timer_and_thread(struct smi_info *smi_info)
{
	if (smi_info->thread != NULL)
		kthread_stop(smi_info->thread);

	smi_info->timer_can_start = false;
	if (smi_info->timer_running)
		del_timer_sync(&smi_info->si_timer);
}

static struct smi_info *find_dup_si(struct smi_info *info)
{
	struct smi_info *e;

	list_for_each_entry(e, &smi_infos, link) {
		if (e->io.addr_type != info->io.addr_type)
			continue;
		if (e->io.addr_data == info->io.addr_data) {
			/*
			 * This is a cheap hack, ACPI doesn't have a defined
			 * slave address but SMBIOS does.  Pick it up from
			 * any source that has it available.
			 */
			if (info->io.slave_addr && !e->io.slave_addr)
				e->io.slave_addr = info->io.slave_addr;
			return e;
		}
	}

	return NULL;
}

int ipmi_si_add_smi(struct si_sm_io *io)
{
	int rv = 0;
	struct smi_info *new_smi, *dup;

	if (!io->io_setup) {
		if (io->addr_type == IPMI_IO_ADDR_SPACE) {
			io->io_setup = ipmi_si_port_setup;
		} else if (io->addr_type == IPMI_MEM_ADDR_SPACE) {
			io->io_setup = ipmi_si_mem_setup;
		} else {
			return -EINVAL;
		}
	}

	new_smi = kzalloc(sizeof(*new_smi), GFP_KERNEL);
	if (!new_smi)
		return -ENOMEM;
	spin_lock_init(&new_smi->si_lock);

	new_smi->io = *io;

	mutex_lock(&smi_infos_lock);
	dup = find_dup_si(new_smi);
	if (dup) {
		if (new_smi->io.addr_source == SI_ACPI &&
		    dup->io.addr_source == SI_SMBIOS) {
			/* We prefer ACPI over SMBIOS. */
			dev_info(dup->io.dev,
				 "Removing SMBIOS-specified %s state machine in favor of ACPI\n",
				 si_to_str[new_smi->io.si_type]);
			cleanup_one_si(dup);
		} else {
			dev_info(new_smi->io.dev,
				 "%s-specified %s state machine: duplicate\n",
				 ipmi_addr_src_to_str(new_smi->io.addr_source),
				 si_to_str[new_smi->io.si_type]);
			rv = -EBUSY;
			kfree(new_smi);
			goto out_err;
		}
	}

	pr_info(PFX "Adding %s-specified %s state machine\n",
		ipmi_addr_src_to_str(new_smi->io.addr_source),
		si_to_str[new_smi->io.si_type]);

	/* So we know not to free it unless we have allocated one. */
	new_smi->intf = NULL;
	new_smi->si_sm = NULL;
	new_smi->handlers = NULL;

	list_add_tail(&new_smi->link, &smi_infos);

	if (initialized) {
		rv = try_smi_init(new_smi);
		if (rv) {
			mutex_unlock(&smi_infos_lock);
			cleanup_one_si(new_smi);
			return rv;
		}
	}
out_err:
	mutex_unlock(&smi_infos_lock);
	return rv;
}

/*
 * Try to start up an interface.  Must be called with smi_infos_lock
 * held, primarily to keep smi_num consistent, we only one to do these
 * one at a time.
 */
static int try_smi_init(struct smi_info *new_smi)
{
	int rv = 0;
	int i;
	char *init_name = NULL;

	pr_info(PFX "Trying %s-specified %s state machine at %s address 0x%lx, slave address 0x%x, irq %d\n",
		ipmi_addr_src_to_str(new_smi->io.addr_source),
		si_to_str[new_smi->io.si_type],
		addr_space_to_str[new_smi->io.addr_type],
		new_smi->io.addr_data,
		new_smi->io.slave_addr, new_smi->io.irq);

	switch (new_smi->io.si_type) {
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

	new_smi->intf_num = smi_num;

	/* Do this early so it's available for logs. */
	if (!new_smi->io.dev) {
		init_name = kasprintf(GFP_KERNEL, "ipmi_si.%d",
				      new_smi->intf_num);

		/*
		 * If we don't already have a device from something
		 * else (like PCI), then register a new one.
		 */
		new_smi->pdev = platform_device_alloc("ipmi_si",
						      new_smi->intf_num);
		if (!new_smi->pdev) {
			pr_err(PFX "Unable to allocate platform device\n");
			goto out_err;
		}
		new_smi->io.dev = &new_smi->pdev->dev;
		new_smi->io.dev->driver = &ipmi_platform_driver.driver;
		/* Nulled by device_add() */
		new_smi->io.dev->init_name = init_name;
	}

	/* Allocate the state machine's data and initialize it. */
	new_smi->si_sm = kmalloc(new_smi->handlers->size(), GFP_KERNEL);
	if (!new_smi->si_sm) {
		rv = -ENOMEM;
		goto out_err;
	}
	new_smi->io.io_size = new_smi->handlers->init_data(new_smi->si_sm,
							   &new_smi->io);

	/* Now that we know the I/O size, we can set up the I/O. */
	rv = new_smi->io.io_setup(&new_smi->io);
	if (rv) {
		dev_err(new_smi->io.dev, "Could not set up I/O space\n");
		goto out_err;
	}

	/* Do low-level detection first. */
	if (new_smi->handlers->detect(new_smi->si_sm)) {
		if (new_smi->io.addr_source)
			dev_err(new_smi->io.dev,
				"Interface detection failed\n");
		rv = -ENODEV;
		goto out_err;
	}

	/*
	 * Attempt a get device id command.  If it fails, we probably
	 * don't have a BMC here.
	 */
	rv = try_get_dev_id(new_smi);
	if (rv) {
		if (new_smi->io.addr_source)
			dev_err(new_smi->io.dev,
			       "There appears to be no BMC at this location\n");
		goto out_err;
	}

	setup_oem_data_handler(new_smi);
	setup_xaction_handlers(new_smi);
	check_for_broken_irqs(new_smi);

	new_smi->waiting_msg = NULL;
	new_smi->curr_msg = NULL;
	atomic_set(&new_smi->req_events, 0);
	new_smi->run_to_completion = false;
	for (i = 0; i < SI_NUM_STATS; i++)
		atomic_set(&new_smi->stats[i], 0);

	new_smi->interrupt_disabled = true;
	atomic_set(&new_smi->need_watch, 0);

	rv = try_enable_event_buffer(new_smi);
	if (rv == 0)
		new_smi->has_event_buffer = true;

	/*
	 * Start clearing the flags before we enable interrupts or the
	 * timer to avoid racing with the timer.
	 */
	start_clear_flags(new_smi);

	/*
	 * IRQ is defined to be set when non-zero.  req_events will
	 * cause a global flags check that will enable interrupts.
	 */
	if (new_smi->io.irq) {
		new_smi->interrupt_disabled = false;
		atomic_set(&new_smi->req_events, 1);
	}

	if (new_smi->pdev) {
		rv = platform_device_add(new_smi->pdev);
		if (rv) {
			dev_err(new_smi->io.dev,
				"Unable to register system interface device: %d\n",
				rv);
			goto out_err;
		}
	}

	dev_set_drvdata(new_smi->io.dev, new_smi);
	rv = device_add_group(new_smi->io.dev, &ipmi_si_dev_attr_group);
	if (rv) {
		dev_err(new_smi->io.dev,
			"Unable to add device attributes: error %d\n",
			rv);
		goto out_err_stop_timer;
	}

	rv = ipmi_register_smi(&handlers,
			       new_smi,
			       new_smi->io.dev,
			       new_smi->io.slave_addr);
	if (rv) {
		dev_err(new_smi->io.dev,
			"Unable to register device: error %d\n",
			rv);
		goto out_err_remove_attrs;
	}

#ifdef CONFIG_IPMI_PROC_INTERFACE
	rv = ipmi_smi_add_proc_entry(new_smi->intf, "type",
				     &smi_type_proc_ops,
				     new_smi);
	if (rv) {
		dev_err(new_smi->io.dev,
			"Unable to create proc entry: %d\n", rv);
		goto out_err_stop_timer;
	}

	rv = ipmi_smi_add_proc_entry(new_smi->intf, "si_stats",
				     &smi_si_stats_proc_ops,
				     new_smi);
	if (rv) {
		dev_err(new_smi->io.dev,
			"Unable to create proc entry: %d\n", rv);
		goto out_err_stop_timer;
	}

	rv = ipmi_smi_add_proc_entry(new_smi->intf, "params",
				     &smi_params_proc_ops,
				     new_smi);
	if (rv) {
		dev_err(new_smi->io.dev,
			"Unable to create proc entry: %d\n", rv);
		goto out_err_stop_timer;
	}
#endif

	/* Don't increment till we know we have succeeded. */
	smi_num++;

	dev_info(new_smi->io.dev, "IPMI %s interface initialized\n",
		 si_to_str[new_smi->io.si_type]);

	WARN_ON(new_smi->io.dev->init_name != NULL);
	kfree(init_name);

	return 0;

out_err_remove_attrs:
	device_remove_group(new_smi->io.dev, &ipmi_si_dev_attr_group);
	dev_set_drvdata(new_smi->io.dev, NULL);

out_err_stop_timer:
	stop_timer_and_thread(new_smi);

out_err:
	new_smi->interrupt_disabled = true;

	if (new_smi->intf) {
		ipmi_smi_t intf = new_smi->intf;
		new_smi->intf = NULL;
		ipmi_unregister_smi(intf);
	}

	if (new_smi->io.irq_cleanup) {
		new_smi->io.irq_cleanup(&new_smi->io);
		new_smi->io.irq_cleanup = NULL;
	}

	/*
	 * Wait until we know that we are out of any interrupt
	 * handlers might have been running before we freed the
	 * interrupt.
	 */
	synchronize_sched();

	if (new_smi->si_sm) {
		if (new_smi->handlers)
			new_smi->handlers->cleanup(new_smi->si_sm);
		kfree(new_smi->si_sm);
		new_smi->si_sm = NULL;
	}
	if (new_smi->io.addr_source_cleanup) {
		new_smi->io.addr_source_cleanup(&new_smi->io);
		new_smi->io.addr_source_cleanup = NULL;
	}
	if (new_smi->io.io_cleanup) {
		new_smi->io.io_cleanup(&new_smi->io);
		new_smi->io.io_cleanup = NULL;
	}

	if (new_smi->pdev) {
		platform_device_unregister(new_smi->pdev);
		new_smi->pdev = NULL;
	} else if (new_smi->pdev) {
		platform_device_put(new_smi->pdev);
	}

	kfree(init_name);

	return rv;
}

static int init_ipmi_si(void)
{
	struct smi_info *e;
	enum ipmi_addr_src type = SI_INVALID;

	if (initialized)
		return 0;

	pr_info("IPMI System Interface driver.\n");

	/* If the user gave us a device, they presumably want us to use it */
	if (!ipmi_si_hardcode_find_bmc())
		goto do_scan;

	ipmi_si_platform_init();

	ipmi_si_pci_init();

	ipmi_si_parisc_init();

	/* We prefer devices with interrupts, but in the case of a machine
	   with multiple BMCs we assume that there will be several instances
	   of a given type so if we succeed in registering a type then also
	   try to register everything else of the same type */
do_scan:
	mutex_lock(&smi_infos_lock);
	list_for_each_entry(e, &smi_infos, link) {
		/* Try to register a device if it has an IRQ and we either
		   haven't successfully registered a device yet or this
		   device has the same type as one we successfully registered */
		if (e->io.irq && (!type || e->io.addr_source == type)) {
			if (!try_smi_init(e)) {
				type = e->io.addr_source;
			}
		}
	}

	/* type will only have been set if we successfully registered an si */
	if (type)
		goto skip_fallback_noirq;

	/* Fall back to the preferred device */

	list_for_each_entry(e, &smi_infos, link) {
		if (!e->io.irq && (!type || e->io.addr_source == type)) {
			if (!try_smi_init(e)) {
				type = e->io.addr_source;
			}
		}
	}

skip_fallback_noirq:
	initialized = 1;
	mutex_unlock(&smi_infos_lock);

	if (type)
		return 0;

	mutex_lock(&smi_infos_lock);
	if (unload_when_empty && list_empty(&smi_infos)) {
		mutex_unlock(&smi_infos_lock);
		cleanup_ipmi_si();
		pr_warn(PFX "Unable to find any System Interface(s)\n");
		return -ENODEV;
	} else {
		mutex_unlock(&smi_infos_lock);
		return 0;
	}
}
module_init(init_ipmi_si);

static void cleanup_one_si(struct smi_info *to_clean)
{
	int           rv = 0;

	if (!to_clean)
		return;

	if (to_clean->intf) {
		ipmi_smi_t intf = to_clean->intf;

		to_clean->intf = NULL;
		rv = ipmi_unregister_smi(intf);
		if (rv) {
			pr_err(PFX "Unable to unregister device: errno=%d\n",
			       rv);
		}
	}

	device_remove_group(to_clean->io.dev, &ipmi_si_dev_attr_group);
	dev_set_drvdata(to_clean->io.dev, NULL);

	list_del(&to_clean->link);

	/*
	 * Make sure that interrupts, the timer and the thread are
	 * stopped and will not run again.
	 */
	if (to_clean->io.irq_cleanup)
		to_clean->io.irq_cleanup(&to_clean->io);
	stop_timer_and_thread(to_clean);

	/*
	 * Timeouts are stopped, now make sure the interrupts are off
	 * in the BMC.  Note that timers and CPU interrupts are off,
	 * so no need for locks.
	 */
	while (to_clean->curr_msg || (to_clean->si_state != SI_NORMAL)) {
		poll(to_clean);
		schedule_timeout_uninterruptible(1);
	}
	if (to_clean->handlers)
		disable_si_irq(to_clean);
	while (to_clean->curr_msg || (to_clean->si_state != SI_NORMAL)) {
		poll(to_clean);
		schedule_timeout_uninterruptible(1);
	}

	if (to_clean->handlers)
		to_clean->handlers->cleanup(to_clean->si_sm);

	kfree(to_clean->si_sm);

	if (to_clean->io.addr_source_cleanup)
		to_clean->io.addr_source_cleanup(&to_clean->io);
	if (to_clean->io.io_cleanup)
		to_clean->io.io_cleanup(&to_clean->io);

	if (to_clean->pdev)
		platform_device_unregister(to_clean->pdev);

	kfree(to_clean);
}

int ipmi_si_remove_by_dev(struct device *dev)
{
	struct smi_info *e;
	int rv = -ENOENT;

	mutex_lock(&smi_infos_lock);
	list_for_each_entry(e, &smi_infos, link) {
		if (e->io.dev == dev) {
			cleanup_one_si(e);
			rv = 0;
			break;
		}
	}
	mutex_unlock(&smi_infos_lock);

	return rv;
}

void ipmi_si_remove_by_data(int addr_space, enum si_type si_type,
			    unsigned long addr)
{
	/* remove */
	struct smi_info *e, *tmp_e;

	mutex_lock(&smi_infos_lock);
	list_for_each_entry_safe(e, tmp_e, &smi_infos, link) {
		if (e->io.addr_type != addr_space)
			continue;
		if (e->io.si_type != si_type)
			continue;
		if (e->io.addr_data == addr)
			cleanup_one_si(e);
	}
	mutex_unlock(&smi_infos_lock);
}

static void cleanup_ipmi_si(void)
{
	struct smi_info *e, *tmp_e;

	if (!initialized)
		return;

	ipmi_si_pci_shutdown();

	ipmi_si_parisc_shutdown();

	ipmi_si_platform_shutdown();

	mutex_lock(&smi_infos_lock);
	list_for_each_entry_safe(e, tmp_e, &smi_infos, link)
		cleanup_one_si(e);
	mutex_unlock(&smi_infos_lock);
}
module_exit(cleanup_ipmi_si);

MODULE_ALIAS("platform:dmi-ipmi-si");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Corey Minyard <minyard@mvista.com>");
MODULE_DESCRIPTION("Interface to the IPMI driver for the KCS, SMIC, and BT"
		   " system interfaces.");
