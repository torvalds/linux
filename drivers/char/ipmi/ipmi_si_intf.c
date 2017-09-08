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
#include <linux/pci.h>
#include <linux/ioport.h>
#include <linux/notifier.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <asm/irq.h>
#include <linux/interrupt.h>
#include <linux/rcupdate.h>
#include <linux/ipmi.h>
#include <linux/ipmi_smi.h>
#include <asm/io.h>
#include "ipmi_si_sm.h"
#include "ipmi_dmi.h"
#include <linux/dmi.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/acpi.h>

#ifdef CONFIG_PARISC
#include <asm/hardware.h>	/* for register_parisc_driver() stuff */
#include <asm/parisc-device.h>
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

enum si_type {
	SI_KCS, SI_SMIC, SI_BT
};

static const char * const si_to_str[] = { "kcs", "smic", "bt" };

#define DEVICE_NAME "ipmi_si"

static struct platform_driver ipmi_driver;

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
	enum si_type           si_type;
	spinlock_t             si_lock;
	struct ipmi_smi_msg    *waiting_msg;
	struct ipmi_smi_msg    *curr_msg;
	enum si_intf_state     si_state;

	/*
	 * Used to handle the various types of I/O that can occur with
	 * IPMI
	 */
	struct si_sm_io io;
	int (*io_setup)(struct smi_info *info);
	void (*io_cleanup)(struct smi_info *info);
	int (*irq_setup)(struct smi_info *info);
	void (*irq_cleanup)(struct smi_info *info);
	unsigned int io_size;
	enum ipmi_addr_src addr_source; /* ACPI, PCI, SMBIOS, hardcode, etc. */
	void (*addr_source_cleanup)(struct smi_info *info);
	void *addr_source_data;

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

	/* The I/O port of an SI interface. */
	int                 port;

	/*
	 * The space between start addresses of the two ports.  For
	 * instance, if the first port is 0xca2 and the spacing is 4, then
	 * the second port is 0xca6.
	 */
	unsigned int        spacing;

	/* zero if no irq; */
	int                 irq;

	/* The timer for this si. */
	struct timer_list   si_timer;

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

	/* Driver model stuff. */
	struct device *dev;
	struct platform_device *pdev;

	/*
	 * True if we allocated the device, false if it came from
	 * someplace else (like PCI).
	 */
	bool dev_registered;

	/* Slave address, could be reported from DMI. */
	unsigned char slave_addr;

	/* Counters and things for the proc filesystem. */
	atomic_t stats[SI_NUM_STATS];

	struct task_struct *thread;

	struct list_head link;
	union ipmi_smi_info_union addr_info;
};

#define smi_inc_stat(smi, stat) \
	atomic_inc(&(smi)->stats[SI_STAT_ ## stat])
#define smi_get_stat(smi, stat) \
	((unsigned int) atomic_read(&(smi)->stats[SI_STAT_ ## stat]))

#define SI_MAX_PARMS 4

static int force_kipmid[SI_MAX_PARMS];
static int num_force_kipmid;
#ifdef CONFIG_PCI
static bool pci_registered;
#endif
#ifdef CONFIG_PARISC
static bool parisc_registered;
#endif

static unsigned int kipmid_max_busy_us[SI_MAX_PARMS];
static int num_max_busy_us;

static bool unload_when_empty = true;

static int add_smi(struct smi_info *smi);
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

static void start_check_enables(struct smi_info *smi_info, bool start_timer)
{
	unsigned char msg[2];

	msg[0] = (IPMI_NETFN_APP_REQUEST << 2);
	msg[1] = IPMI_GET_BMC_GLOBAL_ENABLES_CMD;

	if (start_timer)
		start_new_msg(smi_info, msg, 2);
	else
		smi_info->handlers->start_transaction(smi_info->si_sm, msg, 2);
	smi_info->si_state = SI_CHECKING_ENABLES;
}

static void start_clear_flags(struct smi_info *smi_info, bool start_timer)
{
	unsigned char msg[3];

	/* Make sure the watchdog pre-timeout flag is not set at startup. */
	msg[0] = (IPMI_NETFN_APP_REQUEST << 2);
	msg[1] = IPMI_CLEAR_MSG_FLAGS_CMD;
	msg[2] = WDT_PRE_TIMEOUT_INT;

	if (start_timer)
		start_new_msg(smi_info, msg, 3);
	else
		smi_info->handlers->start_transaction(smi_info->si_sm, msg, 3);
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
static inline bool disable_si_irq(struct smi_info *smi_info, bool start_timer)
{
	if ((smi_info->irq) && (!smi_info->interrupt_disabled)) {
		smi_info->interrupt_disabled = true;
		start_check_enables(smi_info, start_timer);
		return true;
	}
	return false;
}

static inline bool enable_si_irq(struct smi_info *smi_info)
{
	if ((smi_info->irq) && (smi_info->interrupt_disabled)) {
		smi_info->interrupt_disabled = false;
		start_check_enables(smi_info, true);
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
		if (!disable_si_irq(smi_info, true))
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

		start_clear_flags(smi_info, true);
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

	if (((smi_info->irq && !smi_info->interrupt_disabled) ||
	     smi_info->cannot_disable_irq) &&
	    !smi_info->irq_enable_broken)
		enables |= IPMI_BMC_RCV_MSG_INTR;

	if (smi_info->supports_event_msg_buff &&
	    smi_info->irq && !smi_info->interrupt_disabled &&
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
			dev_warn(smi_info->dev,
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
			dev_warn(smi_info->dev,
				 "Couldn't get irq info: %x.\n", msg[2]);
			dev_warn(smi_info->dev,
				 "Maybe ok, but ipmi might run very slowly.\n");
			smi_info->si_state = SI_NORMAL;
			break;
		}
		enables = current_global_enables(smi_info, 0, &irq_on);
		if (smi_info->si_type == SI_BT)
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
			dev_warn(smi_info->dev,
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
		if (smi_info->supports_event_msg_buff || smi_info->irq) {
			start_check_enables(smi_info, true);
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

static int initialized;

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

	if ((smi_info->irq) && (!smi_info->interrupt_disabled)) {
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

static irqreturn_t si_irq_handler(int irq, void *data)
{
	struct smi_info *smi_info = data;
	unsigned long   flags;

	spin_lock_irqsave(&(smi_info->si_lock), flags);

	smi_inc_stat(smi_info, interrupts);

	debug_timestamp("Interrupt");

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

	/* Set up the timer that drives the interface. */
	setup_timer(&new_smi->si_timer, smi_timeout, (long)new_smi);
	smi_mod_timer(new_smi, jiffies + SI_TIMEOUT_JIFFIES);

	/* Try to claim any interrupts. */
	if (new_smi->irq_setup)
		new_smi->irq_setup(new_smi);

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
			dev_notice(new_smi->dev, "Could not start"
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

	data->addr_src = smi->addr_source;
	data->dev = smi->dev;
	data->addr_info = smi->addr_info;
	get_device(smi->dev);

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

/*
 * There can be 4 IO ports passed in (with or without IRQs), 4 addresses,
 * a default IO port, and 1 ACPI/SPMI address.  That sets SI_MAX_DRIVERS.
 */

static LIST_HEAD(smi_infos);
static DEFINE_MUTEX(smi_infos_lock);
static int smi_num; /* Used to sequence the SMIs */

#define DEFAULT_REGSPACING	1
#define DEFAULT_REGSIZE		1

#ifdef CONFIG_ACPI
static bool          si_tryacpi = true;
#endif
#ifdef CONFIG_DMI
static bool          si_trydmi = true;
#endif
static bool          si_tryplatform = true;
#ifdef CONFIG_PCI
static bool          si_trypci = true;
#endif
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
static int slave_addrs[SI_MAX_PARMS]; /* Leaving 0 chooses the default value */
static unsigned int num_slave_addrs;

#define IPMI_IO_ADDR_SPACE  0
#define IPMI_MEM_ADDR_SPACE 1
static const char * const addr_space_to_str[] = { "i/o", "mem" };

static int hotmod_handler(const char *val, struct kernel_param *kp);

module_param_call(hotmod, hotmod_handler, NULL, NULL, 0200);
MODULE_PARM_DESC(hotmod, "Add and remove interfaces.  See"
		 " Documentation/IPMI.txt in the kernel sources for the"
		 " gory details.");

#ifdef CONFIG_ACPI
module_param_named(tryacpi, si_tryacpi, bool, 0);
MODULE_PARM_DESC(tryacpi, "Setting this to zero will disable the"
		 " default scan of the interfaces identified via ACPI");
#endif
#ifdef CONFIG_DMI
module_param_named(trydmi, si_trydmi, bool, 0);
MODULE_PARM_DESC(trydmi, "Setting this to zero will disable the"
		 " default scan of the interfaces identified via DMI");
#endif
module_param_named(tryplatform, si_tryplatform, bool, 0);
MODULE_PARM_DESC(tryplatform, "Setting this to zero will disable the"
		 " default scan of the interfaces identified via platform"
		 " interfaces like openfirmware");
#ifdef CONFIG_PCI
module_param_named(trypci, si_trypci, bool, 0);
MODULE_PARM_DESC(trypci, "Setting this to zero will disable the"
		 " default scan of the interfaces identified via pci");
#endif
module_param_string(type, si_type_str, MAX_SI_TYPE_STR, 0);
MODULE_PARM_DESC(type, "Defines the type of each interface, each"
		 " interface separated by commas.  The types are 'kcs',"
		 " 'smic', and 'bt'.  For example si_type=kcs,bt will set"
		 " the first interface to kcs and the second to bt");
module_param_hw_array(addrs, ulong, iomem, &num_addrs, 0);
MODULE_PARM_DESC(addrs, "Sets the memory address of each interface, the"
		 " addresses separated by commas.  Only use if an interface"
		 " is in memory.  Otherwise, set it to zero or leave"
		 " it blank.");
module_param_hw_array(ports, uint, ioport, &num_ports, 0);
MODULE_PARM_DESC(ports, "Sets the port address of each interface, the"
		 " addresses separated by commas.  Only use if an interface"
		 " is a port.  Otherwise, set it to zero or leave"
		 " it blank.");
module_param_hw_array(irqs, int, irq, &num_irqs, 0);
MODULE_PARM_DESC(irqs, "Sets the interrupt of each interface, the"
		 " addresses separated by commas.  Only use if an interface"
		 " has an interrupt.  Otherwise, set it to zero or leave"
		 " it blank.");
module_param_hw_array(regspacings, int, other, &num_regspacings, 0);
MODULE_PARM_DESC(regspacings, "The number of bytes between the start address"
		 " and each successive register used by the interface.  For"
		 " instance, if the start address is 0xca2 and the spacing"
		 " is 2, then the second address is at 0xca4.  Defaults"
		 " to 1.");
module_param_hw_array(regsizes, int, other, &num_regsizes, 0);
MODULE_PARM_DESC(regsizes, "The size of the specific IPMI register in bytes."
		 " This should generally be 1, 2, 4, or 8 for an 8-bit,"
		 " 16-bit, 32-bit, or 64-bit register.  Use this if you"
		 " the 8-bit IPMI register has to be read from a larger"
		 " register.");
module_param_hw_array(regshifts, int, other, &num_regshifts, 0);
MODULE_PARM_DESC(regshifts, "The amount to shift the data read from the."
		 " IPMI register, in bits.  For instance, if the data"
		 " is read from a 32-bit word and the IPMI data is in"
		 " bit 8-15, then the shift would be 8");
module_param_hw_array(slave_addrs, int, other, &num_slave_addrs, 0);
MODULE_PARM_DESC(slave_addrs, "Set the default IPMB slave address for"
		 " the controller.  Normally this is 0x20, but can be"
		 " overridden by this parm.  This is an array indexed"
		 " by interface number.");
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
				 IRQF_SHARED,
				 DEVICE_NAME,
				 info);
		if (!rv)
			/* Enable the interrupt in the BT interface. */
			info->io.outputb(&info->io, IPMI_BT_INTMASK_REG,
					 IPMI_BT_INTMASK_ENABLE_IRQ_BIT);
	} else
		rv = request_irq(info->irq,
				 si_irq_handler,
				 IRQF_SHARED,
				 DEVICE_NAME,
				 info);
	if (rv) {
		dev_warn(info->dev, "%s unable to claim interrupt %d,"
			 " running polled\n",
			 DEVICE_NAME, info->irq);
		info->irq = 0;
	} else {
		info->irq_cleanup = std_irq_cleanup;
		dev_info(info->dev, "Using irq %d\n", info->irq);
	}

	return rv;
}

static unsigned char port_inb(const struct si_sm_io *io, unsigned int offset)
{
	unsigned int addr = io->addr_data;

	return inb(addr + (offset * io->regspacing));
}

static void port_outb(const struct si_sm_io *io, unsigned int offset,
		      unsigned char b)
{
	unsigned int addr = io->addr_data;

	outb(b, addr + (offset * io->regspacing));
}

static unsigned char port_inw(const struct si_sm_io *io, unsigned int offset)
{
	unsigned int addr = io->addr_data;

	return (inw(addr + (offset * io->regspacing)) >> io->regshift) & 0xff;
}

static void port_outw(const struct si_sm_io *io, unsigned int offset,
		      unsigned char b)
{
	unsigned int addr = io->addr_data;

	outw(b << io->regshift, addr + (offset * io->regspacing));
}

static unsigned char port_inl(const struct si_sm_io *io, unsigned int offset)
{
	unsigned int addr = io->addr_data;

	return (inl(addr + (offset * io->regspacing)) >> io->regshift) & 0xff;
}

static void port_outl(const struct si_sm_io *io, unsigned int offset,
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
		for (idx = 0; idx < info->io_size; idx++)
			release_region(addr + idx * info->io.regspacing,
				       info->io.regsize);
	}
}

static int port_setup(struct smi_info *info)
{
	unsigned int addr = info->io.addr_data;
	int          idx;

	if (!addr)
		return -ENODEV;

	info->io_cleanup = port_cleanup;

	/*
	 * Figure out the actual inb/inw/inl/etc routine to use based
	 * upon the register size.
	 */
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
		dev_warn(info->dev, "Invalid register size: %d\n",
			 info->io.regsize);
		return -EINVAL;
	}

	/*
	 * Some BIOSes reserve disjoint I/O regions in their ACPI
	 * tables.  This causes problems when trying to register the
	 * entire I/O region.  Therefore we must register each I/O
	 * port separately.
	 */
	for (idx = 0; idx < info->io_size; idx++) {
		if (request_region(addr + idx * info->io.regspacing,
				   info->io.regsize, DEVICE_NAME) == NULL) {
			/* Undo allocations */
			while (idx--)
				release_region(addr + idx * info->io.regspacing,
					       info->io.regsize);
			return -EIO;
		}
	}
	return 0;
}

static unsigned char intf_mem_inb(const struct si_sm_io *io,
				  unsigned int offset)
{
	return readb((io->addr)+(offset * io->regspacing));
}

static void intf_mem_outb(const struct si_sm_io *io, unsigned int offset,
			  unsigned char b)
{
	writeb(b, (io->addr)+(offset * io->regspacing));
}

static unsigned char intf_mem_inw(const struct si_sm_io *io,
				  unsigned int offset)
{
	return (readw((io->addr)+(offset * io->regspacing)) >> io->regshift)
		& 0xff;
}

static void intf_mem_outw(const struct si_sm_io *io, unsigned int offset,
			  unsigned char b)
{
	writeb(b << io->regshift, (io->addr)+(offset * io->regspacing));
}

static unsigned char intf_mem_inl(const struct si_sm_io *io,
				  unsigned int offset)
{
	return (readl((io->addr)+(offset * io->regspacing)) >> io->regshift)
		& 0xff;
}

static void intf_mem_outl(const struct si_sm_io *io, unsigned int offset,
			  unsigned char b)
{
	writel(b << io->regshift, (io->addr)+(offset * io->regspacing));
}

#ifdef readq
static unsigned char mem_inq(const struct si_sm_io *io, unsigned int offset)
{
	return (readq((io->addr)+(offset * io->regspacing)) >> io->regshift)
		& 0xff;
}

static void mem_outq(const struct si_sm_io *io, unsigned int offset,
		     unsigned char b)
{
	writeq(b << io->regshift, (io->addr)+(offset * io->regspacing));
}
#endif

static void mem_region_cleanup(struct smi_info *info, int num)
{
	unsigned long addr = info->io.addr_data;
	int idx;

	for (idx = 0; idx < num; idx++)
		release_mem_region(addr + idx * info->io.regspacing,
				   info->io.regsize);
}

static void mem_cleanup(struct smi_info *info)
{
	if (info->io.addr) {
		iounmap(info->io.addr);
		mem_region_cleanup(info, info->io_size);
	}
}

static int mem_setup(struct smi_info *info)
{
	unsigned long addr = info->io.addr_data;
	int           mapsize, idx;

	if (!addr)
		return -ENODEV;

	info->io_cleanup = mem_cleanup;

	/*
	 * Figure out the actual readb/readw/readl/etc routine to use based
	 * upon the register size.
	 */
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
		dev_warn(info->dev, "Invalid register size: %d\n",
			 info->io.regsize);
		return -EINVAL;
	}

	/*
	 * Some BIOSes reserve disjoint memory regions in their ACPI
	 * tables.  This causes problems when trying to request the
	 * entire region.  Therefore we must request each register
	 * separately.
	 */
	for (idx = 0; idx < info->io_size; idx++) {
		if (request_mem_region(addr + idx * info->io.regspacing,
				       info->io.regsize, DEVICE_NAME) == NULL) {
			/* Undo allocations */
			mem_region_cleanup(info, idx);
			return -EIO;
		}
	}

	/*
	 * Calculate the total amount of memory to claim.  This is an
	 * unusual looking calculation, but it avoids claiming any
	 * more memory than it has to.  It will claim everything
	 * between the first address to the end of the last full
	 * register.
	 */
	mapsize = ((info->io_size * info->io.regspacing)
		   - (info->io.regspacing - info->io.regsize));
	info->io.addr = ioremap(addr, mapsize);
	if (info->io.addr == NULL) {
		mem_region_cleanup(info, info->io_size);
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
	const char *name;
	const int  val;
};

static const struct hotmod_vals hotmod_ops[] = {
	{ "add",	HM_ADD },
	{ "remove",	HM_REMOVE },
	{ NULL }
};

static const struct hotmod_vals hotmod_si[] = {
	{ "kcs",	SI_KCS },
	{ "smic",	SI_SMIC },
	{ "bt",		SI_BT },
	{ NULL }
};

static const struct hotmod_vals hotmod_as[] = {
	{ "mem",	IPMI_MEM_ADDR_SPACE },
	{ "i/o",	IPMI_IO_ADDR_SPACE },
	{ NULL }
};

static int parse_str(const struct hotmod_vals *v, int *val, char *name,
		     char **curr)
{
	char *s;
	int  i;

	s = strchr(*curr, ',');
	if (!s) {
		pr_warn(PFX "No hotmod %s given.\n", name);
		return -EINVAL;
	}
	*s = '\0';
	s++;
	for (i = 0; v[i].name; i++) {
		if (strcmp(*curr, v[i].name) == 0) {
			*val = v[i].val;
			*curr = s;
			return 0;
		}
	}

	pr_warn(PFX "Invalid hotmod %s '%s'\n", name, *curr);
	return -EINVAL;
}

static int check_hotmod_int_op(const char *curr, const char *option,
			       const char *name, int *val)
{
	char *n;

	if (strcmp(curr, name) == 0) {
		if (!option) {
			pr_warn(PFX "No option given for '%s'\n", curr);
			return -EINVAL;
		}
		*val = simple_strtoul(option, &n, 0);
		if ((*n != '\0') || (*option == '\0')) {
			pr_warn(PFX "Bad option given for '%s'\n", curr);
			return -EINVAL;
		}
		return 1;
	}
	return 0;
}

static struct smi_info *smi_info_alloc(void)
{
	struct smi_info *info = kzalloc(sizeof(*info), GFP_KERNEL);

	if (info)
		spin_lock_init(&info->si_lock);
	return info;
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
		ipmb = 0; /* Choose the default if not specified */

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
			pr_warn(PFX "Invalid hotmod address '%s'\n", curr);
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
			pr_warn(PFX "Invalid hotmod option '%s'\n", curr);
			goto out;
		}

		if (op == HM_ADD) {
			info = smi_info_alloc();
			if (!info) {
				rv = -ENOMEM;
				goto out;
			}

			info->addr_source = SI_HOTMOD;
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
				info->io.regsize = DEFAULT_REGSIZE;
			info->io.regshift = regshift;
			info->irq = irq;
			if (info->irq)
				info->irq_setup = std_irq_setup;
			info->slave_addr = ipmb;

			rv = add_smi(info);
			if (rv) {
				kfree(info);
				goto out;
			}
			mutex_lock(&smi_infos_lock);
			rv = try_smi_init(info);
			mutex_unlock(&smi_infos_lock);
			if (rv) {
				cleanup_one_si(info);
				goto out;
			}
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

static int hardcode_find_bmc(void)
{
	int ret = -ENODEV;
	int             i;
	struct smi_info *info;

	for (i = 0; i < SI_MAX_PARMS; i++) {
		if (!ports[i] && !addrs[i])
			continue;

		info = smi_info_alloc();
		if (!info)
			return -ENOMEM;

		info->addr_source = SI_HARDCODED;
		pr_info(PFX "probing via hardcoded address\n");

		if (!si_type[i] || strcmp(si_type[i], "kcs") == 0) {
			info->si_type = SI_KCS;
		} else if (strcmp(si_type[i], "smic") == 0) {
			info->si_type = SI_SMIC;
		} else if (strcmp(si_type[i], "bt") == 0) {
			info->si_type = SI_BT;
		} else {
			pr_warn(PFX "Interface type specified for interface %d, was invalid: %s\n",
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
			pr_warn(PFX "Interface type specified for interface %d, but port and address were not set or set to zero.\n",
				i);
			kfree(info);
			continue;
		}

		info->io.addr = NULL;
		info->io.regspacing = regspacings[i];
		if (!info->io.regspacing)
			info->io.regspacing = DEFAULT_REGSPACING;
		info->io.regsize = regsizes[i];
		if (!info->io.regsize)
			info->io.regsize = DEFAULT_REGSIZE;
		info->io.regshift = regshifts[i];
		info->irq = irqs[i];
		if (info->irq)
			info->irq_setup = std_irq_setup;
		info->slave_addr = slave_addrs[i];

		if (!add_smi(info)) {
			mutex_lock(&smi_infos_lock);
			if (try_smi_init(info))
				cleanup_one_si(info);
			mutex_unlock(&smi_infos_lock);
			ret = 0;
		} else {
			kfree(info);
		}
	}
	return ret;
}

#ifdef CONFIG_ACPI

/*
 * Once we get an ACPI failure, we don't try any more, because we go
 * through the tables sequentially.  Once we don't find a table, there
 * are no more.
 */
static int acpi_failure;

/* For GPE-type interrupts. */
static u32 ipmi_acpi_gpe(acpi_handle gpe_device,
	u32 gpe_number, void *context)
{
	struct smi_info *smi_info = context;
	unsigned long   flags;

	spin_lock_irqsave(&(smi_info->si_lock), flags);

	smi_inc_stat(smi_info, interrupts);

	debug_timestamp("ACPI_GPE");

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

	status = acpi_install_gpe_handler(NULL,
					  info->irq,
					  ACPI_GPE_LEVEL_TRIGGERED,
					  &ipmi_acpi_gpe,
					  info);
	if (status != AE_OK) {
		dev_warn(info->dev, "%s unable to claim ACPI GPE %d,"
			 " running polled\n", DEVICE_NAME, info->irq);
		info->irq = 0;
		return -EINVAL;
	} else {
		info->irq_cleanup = acpi_gpe_irq_cleanup;
		dev_info(info->dev, "Using ACPI GPE %d\n", info->irq);
		return 0;
	}
}

/*
 * Defined at
 * http://h21007.www2.hp.com/portal/download/files/unprot/hpspmi.pdf
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

	/*
	 * If bit 0 of InterruptType is set, then this is the SCI
	 * interrupt in the GPEx_STS register.
	 */
	u8	GPE;

	s16	Reserved;

	/*
	 * If bit 1 of InterruptType is set, then this is the I/O
	 * APIC/SAPIC interrupt.
	 */
	u32	GlobalSystemInterrupt;

	/* The actual register address. */
	struct acpi_generic_address addr;

	u8	UID[4];

	s8      spmi_id[1]; /* A '\0' terminated array starts here. */
};

static int try_init_spmi(struct SPMITable *spmi)
{
	struct smi_info  *info;
	int rv;

	if (spmi->IPMIlegacy != 1) {
		pr_info(PFX "Bad SPMI legacy %d\n", spmi->IPMIlegacy);
		return -ENODEV;
	}

	info = smi_info_alloc();
	if (!info) {
		pr_err(PFX "Could not allocate SI data (3)\n");
		return -ENOMEM;
	}

	info->addr_source = SI_SPMI;
	pr_info(PFX "probing via SPMI\n");

	/* Figure out the interface type. */
	switch (spmi->InterfaceType) {
	case 1:	/* KCS */
		info->si_type = SI_KCS;
		break;
	case 2:	/* SMIC */
		info->si_type = SI_SMIC;
		break;
	case 3:	/* BT */
		info->si_type = SI_BT;
		break;
	case 4: /* SSIF, just ignore */
		kfree(info);
		return -EIO;
	default:
		pr_info(PFX "Unknown ACPI/SPMI SI type %d\n",
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
		pr_warn(PFX "Unknown ACPI I/O Address type\n");
		return -EIO;
	}
	info->io.addr_data = spmi->addr.address;

	pr_info("ipmi_si: SPMI: %s %#lx regsize %d spacing %d irq %d\n",
		(info->io.addr_type == IPMI_IO_ADDR_SPACE) ? "io" : "mem",
		info->io.addr_data, info->io.regsize, info->io.regspacing,
		info->irq);

	rv = add_smi(info);
	if (rv)
		kfree(info);

	return rv;
}

static void spmi_find_bmc(void)
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

		try_init_spmi(spmi);
	}
}
#endif

#if defined(CONFIG_DMI) || defined(CONFIG_ACPI)
struct resource *ipmi_get_info_from_resources(struct platform_device *pdev,
					      struct smi_info *info)
{
	struct resource *res, *res_second;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (res) {
		info->io_setup = port_setup;
		info->io.addr_type = IPMI_IO_ADDR_SPACE;
	} else {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (res) {
			info->io_setup = mem_setup;
			info->io.addr_type = IPMI_MEM_ADDR_SPACE;
		}
	}
	if (!res) {
		dev_err(&pdev->dev, "no I/O or memory address\n");
		return NULL;
	}
	info->io.addr_data = res->start;

	info->io.regspacing = DEFAULT_REGSPACING;
	res_second = platform_get_resource(pdev,
			       (info->io.addr_type == IPMI_IO_ADDR_SPACE) ?
					IORESOURCE_IO : IORESOURCE_MEM,
			       1);
	if (res_second) {
		if (res_second->start > info->io.addr_data)
			info->io.regspacing =
				res_second->start - info->io.addr_data;
	}
	info->io.regsize = DEFAULT_REGSIZE;
	info->io.regshift = 0;

	return res;
}

#endif

#ifdef CONFIG_DMI
static int dmi_ipmi_probe(struct platform_device *pdev)
{
	struct smi_info *info;
	u8 type, slave_addr;
	int rv;

	if (!si_trydmi)
		return -ENODEV;

	rv = device_property_read_u8(&pdev->dev, "ipmi-type", &type);
	if (rv)
		return -ENODEV;

	info = smi_info_alloc();
	if (!info) {
		pr_err(PFX "Could not allocate SI data\n");
		return -ENOMEM;
	}

	info->addr_source = SI_SMBIOS;
	pr_info(PFX "probing via SMBIOS\n");

	switch (type) {
	case IPMI_DMI_TYPE_KCS:
		info->si_type = SI_KCS;
		break;
	case IPMI_DMI_TYPE_SMIC:
		info->si_type = SI_SMIC;
		break;
	case IPMI_DMI_TYPE_BT:
		info->si_type = SI_BT;
		break;
	default:
		kfree(info);
		return -EINVAL;
	}

	if (!ipmi_get_info_from_resources(pdev, info)) {
		rv = -EINVAL;
		goto err_free;
	}

	rv = device_property_read_u8(&pdev->dev, "slave-addr", &slave_addr);
	if (rv) {
		dev_warn(&pdev->dev, "device has no slave-addr property");
		info->slave_addr = 0x20;
	} else {
		info->slave_addr = slave_addr;
	}

	info->irq = platform_get_irq(pdev, 0);
	if (info->irq > 0)
		info->irq_setup = std_irq_setup;
	else
		info->irq = 0;

	info->dev = &pdev->dev;

	pr_info("ipmi_si: SMBIOS: %s %#lx regsize %d spacing %d irq %d\n",
		(info->io.addr_type == IPMI_IO_ADDR_SPACE) ? "io" : "mem",
		info->io.addr_data, info->io.regsize, info->io.regspacing,
		info->irq);

	if (add_smi(info))
		kfree(info);

	return 0;

err_free:
	kfree(info);
	return rv;
}
#else
static int dmi_ipmi_probe(struct platform_device *pdev)
{
	return -ENODEV;
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

static int ipmi_pci_probe_regspacing(struct smi_info *info)
{
	if (info->si_type == SI_KCS) {
		unsigned char	status;
		int		regspacing;

		info->io.regsize = DEFAULT_REGSIZE;
		info->io.regshift = 0;
		info->io_size = 2;
		info->handlers = &kcs_smi_handlers;

		/* detect 1, 4, 16byte spacing */
		for (regspacing = DEFAULT_REGSPACING; regspacing <= 16;) {
			info->io.regspacing = regspacing;
			if (info->io_setup(info)) {
				dev_err(info->dev,
					"Could not setup I/O space\n");
				return DEFAULT_REGSPACING;
			}
			/* write invalid cmd */
			info->io.outputb(&info->io, 1, 0x10);
			/* read status back */
			status = info->io.inputb(&info->io, 1);
			info->io_cleanup(info);
			if (status)
				return regspacing;
			regspacing *= 4;
		}
	}
	return DEFAULT_REGSPACING;
}

static int ipmi_pci_probe(struct pci_dev *pdev,
				    const struct pci_device_id *ent)
{
	int rv;
	int class_type = pdev->class & PCI_ERMC_CLASSCODE_TYPE_MASK;
	struct smi_info *info;

	info = smi_info_alloc();
	if (!info)
		return -ENOMEM;

	info->addr_source = SI_PCI;
	dev_info(&pdev->dev, "probing via PCI");

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
		dev_info(&pdev->dev, "Unknown IPMI type: %d\n", class_type);
		return -ENOMEM;
	}

	rv = pci_enable_device(pdev);
	if (rv) {
		dev_err(&pdev->dev, "couldn't enable PCI device\n");
		kfree(info);
		return rv;
	}

	info->addr_source_cleanup = ipmi_pci_cleanup;
	info->addr_source_data = pdev;

	if (pci_resource_flags(pdev, 0) & IORESOURCE_IO) {
		info->io_setup = port_setup;
		info->io.addr_type = IPMI_IO_ADDR_SPACE;
	} else {
		info->io_setup = mem_setup;
		info->io.addr_type = IPMI_MEM_ADDR_SPACE;
	}
	info->io.addr_data = pci_resource_start(pdev, 0);

	info->io.regspacing = ipmi_pci_probe_regspacing(info);
	info->io.regsize = DEFAULT_REGSIZE;
	info->io.regshift = 0;

	info->irq = pdev->irq;
	if (info->irq)
		info->irq_setup = std_irq_setup;

	info->dev = &pdev->dev;
	pci_set_drvdata(pdev, info);

	dev_info(&pdev->dev, "%pR regsize %d spacing %d irq %d\n",
		&pdev->resource[0], info->io.regsize, info->io.regspacing,
		info->irq);

	rv = add_smi(info);
	if (rv) {
		kfree(info);
		pci_disable_device(pdev);
	}

	return rv;
}

static void ipmi_pci_remove(struct pci_dev *pdev)
{
	struct smi_info *info = pci_get_drvdata(pdev);
	cleanup_one_si(info);
}

static const struct pci_device_id ipmi_pci_devices[] = {
	{ PCI_DEVICE(PCI_HP_VENDOR_ID, PCI_MMC_DEVICE_ID) },
	{ PCI_DEVICE_CLASS(PCI_ERMC_CLASSCODE, PCI_ERMC_CLASSCODE_MASK) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, ipmi_pci_devices);

static struct pci_driver ipmi_pci_driver = {
	.name =         DEVICE_NAME,
	.id_table =     ipmi_pci_devices,
	.probe =        ipmi_pci_probe,
	.remove =       ipmi_pci_remove,
};
#endif /* CONFIG_PCI */

#ifdef CONFIG_OF
static const struct of_device_id of_ipmi_match[] = {
	{ .type = "ipmi", .compatible = "ipmi-kcs",
	  .data = (void *)(unsigned long) SI_KCS },
	{ .type = "ipmi", .compatible = "ipmi-smic",
	  .data = (void *)(unsigned long) SI_SMIC },
	{ .type = "ipmi", .compatible = "ipmi-bt",
	  .data = (void *)(unsigned long) SI_BT },
	{},
};
MODULE_DEVICE_TABLE(of, of_ipmi_match);

static int of_ipmi_probe(struct platform_device *dev)
{
	const struct of_device_id *match;
	struct smi_info *info;
	struct resource resource;
	const __be32 *regsize, *regspacing, *regshift;
	struct device_node *np = dev->dev.of_node;
	int ret;
	int proplen;

	dev_info(&dev->dev, "probing via device tree\n");

	match = of_match_device(of_ipmi_match, &dev->dev);
	if (!match)
		return -ENODEV;

	if (!of_device_is_available(np))
		return -EINVAL;

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

	info = smi_info_alloc();

	if (!info) {
		dev_err(&dev->dev,
			"could not allocate memory for OF probe\n");
		return -ENOMEM;
	}

	info->si_type		= (enum si_type) match->data;
	info->addr_source	= SI_DEVICETREE;
	info->irq_setup		= std_irq_setup;

	if (resource.flags & IORESOURCE_IO) {
		info->io_setup		= port_setup;
		info->io.addr_type	= IPMI_IO_ADDR_SPACE;
	} else {
		info->io_setup		= mem_setup;
		info->io.addr_type	= IPMI_MEM_ADDR_SPACE;
	}

	info->io.addr_data	= resource.start;

	info->io.regsize	= regsize ? be32_to_cpup(regsize) : DEFAULT_REGSIZE;
	info->io.regspacing	= regspacing ? be32_to_cpup(regspacing) : DEFAULT_REGSPACING;
	info->io.regshift	= regshift ? be32_to_cpup(regshift) : 0;

	info->irq		= irq_of_parse_and_map(dev->dev.of_node, 0);
	info->dev		= &dev->dev;

	dev_dbg(&dev->dev, "addr 0x%lx regsize %d spacing %d irq %d\n",
		info->io.addr_data, info->io.regsize, info->io.regspacing,
		info->irq);

	dev_set_drvdata(&dev->dev, info);

	ret = add_smi(info);
	if (ret) {
		kfree(info);
		return ret;
	}
	return 0;
}
#else
#define of_ipmi_match NULL
static int of_ipmi_probe(struct platform_device *dev)
{
	return -ENODEV;
}
#endif

#ifdef CONFIG_ACPI
static int find_slave_address(struct smi_info *info, int slave_addr)
{
#ifdef CONFIG_IPMI_DMI_DECODE
	if (!slave_addr) {
		int type = -1;
		u32 flags = IORESOURCE_IO;

		switch (info->si_type) {
		case SI_KCS:
			type = IPMI_DMI_TYPE_KCS;
			break;
		case SI_BT:
			type = IPMI_DMI_TYPE_BT;
			break;
		case SI_SMIC:
			type = IPMI_DMI_TYPE_SMIC;
			break;
		}

		if (info->io.addr_type == IPMI_MEM_ADDR_SPACE)
			flags = IORESOURCE_MEM;

		slave_addr = ipmi_dmi_get_slave_addr(type, flags,
						     info->io.addr_data);
	}
#endif

	return slave_addr;
}

static int acpi_ipmi_probe(struct platform_device *dev)
{
	struct smi_info *info;
	acpi_handle handle;
	acpi_status status;
	unsigned long long tmp;
	struct resource *res;
	int rv = -EINVAL;

	if (!si_tryacpi)
		return -ENODEV;

	handle = ACPI_HANDLE(&dev->dev);
	if (!handle)
		return -ENODEV;

	info = smi_info_alloc();
	if (!info)
		return -ENOMEM;

	info->addr_source = SI_ACPI;
	dev_info(&dev->dev, PFX "probing via ACPI\n");

	info->addr_info.acpi_info.acpi_handle = handle;

	/* _IFT tells us the interface type: KCS, BT, etc */
	status = acpi_evaluate_integer(handle, "_IFT", NULL, &tmp);
	if (ACPI_FAILURE(status)) {
		dev_err(&dev->dev, "Could not find ACPI IPMI interface type\n");
		goto err_free;
	}

	switch (tmp) {
	case 1:
		info->si_type = SI_KCS;
		break;
	case 2:
		info->si_type = SI_SMIC;
		break;
	case 3:
		info->si_type = SI_BT;
		break;
	case 4: /* SSIF, just ignore */
		rv = -ENODEV;
		goto err_free;
	default:
		dev_info(&dev->dev, "unknown IPMI type %lld\n", tmp);
		goto err_free;
	}

	res = ipmi_get_info_from_resources(dev, info);
	if (!res) {
		rv = -EINVAL;
		goto err_free;
	}

	/* If _GPE exists, use it; otherwise use standard interrupts */
	status = acpi_evaluate_integer(handle, "_GPE", NULL, &tmp);
	if (ACPI_SUCCESS(status)) {
		info->irq = tmp;
		info->irq_setup = acpi_gpe_irq_setup;
	} else {
		int irq = platform_get_irq(dev, 0);

		if (irq > 0) {
			info->irq = irq;
			info->irq_setup = std_irq_setup;
		}
	}

	info->slave_addr = find_slave_address(info, info->slave_addr);

	info->dev = &dev->dev;
	platform_set_drvdata(dev, info);

	dev_info(info->dev, "%pR regsize %d spacing %d irq %d\n",
		 res, info->io.regsize, info->io.regspacing,
		 info->irq);

	rv = add_smi(info);
	if (rv)
		kfree(info);

	return rv;

err_free:
	kfree(info);
	return rv;
}

static const struct acpi_device_id acpi_ipmi_match[] = {
	{ "IPI0001", 0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, acpi_ipmi_match);
#else
static int acpi_ipmi_probe(struct platform_device *dev)
{
	return -ENODEV;
}
#endif

static int ipmi_probe(struct platform_device *dev)
{
	if (of_ipmi_probe(dev) == 0)
		return 0;

	if (acpi_ipmi_probe(dev) == 0)
		return 0;

	return dmi_ipmi_probe(dev);
}

static int ipmi_remove(struct platform_device *dev)
{
	struct smi_info *info = dev_get_drvdata(&dev->dev);

	cleanup_one_si(info);
	return 0;
}

static struct platform_driver ipmi_driver = {
	.driver = {
		.name = DEVICE_NAME,
		.of_match_table = of_ipmi_match,
		.acpi_match_table = ACPI_PTR(acpi_ipmi_match),
	},
	.probe		= ipmi_probe,
	.remove		= ipmi_remove,
};

#ifdef CONFIG_PARISC
static int __init ipmi_parisc_probe(struct parisc_device *dev)
{
	struct smi_info *info;
	int rv;

	info = smi_info_alloc();

	if (!info) {
		dev_err(&dev->dev,
			"could not allocate memory for PARISC probe\n");
		return -ENOMEM;
	}

	info->si_type		= SI_KCS;
	info->addr_source	= SI_DEVICETREE;
	info->io_setup		= mem_setup;
	info->io.addr_type	= IPMI_MEM_ADDR_SPACE;
	info->io.addr_data	= dev->hpa.start;
	info->io.regsize	= 1;
	info->io.regspacing	= 1;
	info->io.regshift	= 0;
	info->irq		= 0; /* no interrupt */
	info->irq_setup		= NULL;
	info->dev		= &dev->dev;

	dev_dbg(&dev->dev, "addr 0x%lx\n", info->io.addr_data);

	dev_set_drvdata(&dev->dev, info);

	rv = add_smi(info);
	if (rv) {
		kfree(info);
		return rv;
	}

	return 0;
}

static int __exit ipmi_parisc_remove(struct parisc_device *dev)
{
	cleanup_one_si(dev_get_drvdata(&dev->dev));
	return 0;
}

static const struct parisc_device_id ipmi_parisc_tbl[] __initconst = {
	{ HPHW_MC, HVERSION_REV_ANY_ID, 0x004, 0xC0 },
	{ 0, }
};

MODULE_DEVICE_TABLE(parisc, ipmi_parisc_tbl);

static struct parisc_driver ipmi_parisc_driver __refdata = {
	.name =		"ipmi",
	.id_table =	ipmi_parisc_tbl,
	.probe =	ipmi_parisc_probe,
	.remove =	__exit_p(ipmi_parisc_remove),
};
#endif /* CONFIG_PARISC */

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
	rv = ipmi_demangle_device_id(resp, resp_len, &smi_info->device_id);

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
		dev_warn(smi_info->dev,
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
		dev_warn(smi_info->dev,
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
		dev_warn(smi_info->dev,
			 "Error getting response from set global enables command: %d\n",
			 rv);
		goto out;
	}

	resp_len = smi_info->handlers->get_result(smi_info->si_sm,
						  resp, IPMI_MAX_MSG_LENGTH);

	if (resp_len < 3 ||
			resp[0] != (IPMI_NETFN_APP_REQUEST | 1) << 2 ||
			resp[1] != IPMI_SET_BMC_GLOBAL_ENABLES_CMD) {
		dev_warn(smi_info->dev,
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
		dev_err(smi_info->dev,
			"Cannot check clearing the rcv irq: %d\n", rv);
		return;
	}

	if (rv) {
		/*
		 * An error when setting the event buffer bit means
		 * clearing the bit is not supported.
		 */
		dev_warn(smi_info->dev,
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

	if (!smi_info->irq)
		return;

	rv = get_global_enables(smi_info, &enables);
	if (!rv) {
		enables |= IPMI_BMC_RCV_MSG_INTR;
		rv = set_global_enables(smi_info, enables);
	}

	if (rv < 0) {
		dev_err(smi_info->dev,
			"Cannot check setting the rcv irq: %d\n", rv);
		return;
	}

	if (rv) {
		/*
		 * An error when setting the event buffer bit means
		 * setting the bit is not supported.
		 */
		dev_warn(smi_info->dev,
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

static int smi_type_proc_show(struct seq_file *m, void *v)
{
	struct smi_info *smi = m->private;

	seq_printf(m, "%s\n", si_to_str[smi->si_type]);

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
		       smi->irq && !smi->interrupt_disabled);
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
		   si_to_str[smi->si_type],
		   addr_space_to_str[smi->io.addr_type],
		   smi->io.addr_data,
		   smi->io.regspacing,
		   smi->io.regsize,
		   smi->io.regshift,
		   smi->irq,
		   smi->slave_addr);

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

static void check_for_broken_irqs(struct smi_info *smi_info)
{
	check_clr_rcv_irq(smi_info);
	check_set_rcv_irq(smi_info);
}

static inline void wait_for_timer_and_thread(struct smi_info *smi_info)
{
	if (smi_info->thread != NULL)
		kthread_stop(smi_info->thread);
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
			if (info->slave_addr && !e->slave_addr)
				e->slave_addr = info->slave_addr;
			return e;
		}
	}

	return NULL;
}

static int add_smi(struct smi_info *new_smi)
{
	int rv = 0;
	struct smi_info *dup;

	mutex_lock(&smi_infos_lock);
	dup = find_dup_si(new_smi);
	if (dup) {
		if (new_smi->addr_source == SI_ACPI &&
		    dup->addr_source == SI_SMBIOS) {
			/* We prefer ACPI over SMBIOS. */
			dev_info(dup->dev,
				 "Removing SMBIOS-specified %s state machine in favor of ACPI\n",
				 si_to_str[new_smi->si_type]);
			cleanup_one_si(dup);
		} else {
			dev_info(new_smi->dev,
				 "%s-specified %s state machine: duplicate\n",
				 ipmi_addr_src_to_str(new_smi->addr_source),
				 si_to_str[new_smi->si_type]);
			rv = -EBUSY;
			goto out_err;
		}
	}

	pr_info(PFX "Adding %s-specified %s state machine\n",
		ipmi_addr_src_to_str(new_smi->addr_source),
		si_to_str[new_smi->si_type]);

	/* So we know not to free it unless we have allocated one. */
	new_smi->intf = NULL;
	new_smi->si_sm = NULL;
	new_smi->handlers = NULL;

	list_add_tail(&new_smi->link, &smi_infos);

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
		ipmi_addr_src_to_str(new_smi->addr_source),
		si_to_str[new_smi->si_type],
		addr_space_to_str[new_smi->io.addr_type],
		new_smi->io.addr_data,
		new_smi->slave_addr, new_smi->irq);

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

	new_smi->intf_num = smi_num;

	/* Do this early so it's available for logs. */
	if (!new_smi->dev) {
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
		new_smi->dev = &new_smi->pdev->dev;
		new_smi->dev->driver = &ipmi_driver.driver;
		/* Nulled by device_add() */
		new_smi->dev->init_name = init_name;
	}

	/* Allocate the state machine's data and initialize it. */
	new_smi->si_sm = kmalloc(new_smi->handlers->size(), GFP_KERNEL);
	if (!new_smi->si_sm) {
		pr_err(PFX "Could not allocate state machine memory\n");
		rv = -ENOMEM;
		goto out_err;
	}
	new_smi->io_size = new_smi->handlers->init_data(new_smi->si_sm,
							&new_smi->io);

	/* Now that we know the I/O size, we can set up the I/O. */
	rv = new_smi->io_setup(new_smi);
	if (rv) {
		dev_err(new_smi->dev, "Could not set up I/O space\n");
		goto out_err;
	}

	/* Do low-level detection first. */
	if (new_smi->handlers->detect(new_smi->si_sm)) {
		if (new_smi->addr_source)
			dev_err(new_smi->dev, "Interface detection failed\n");
		rv = -ENODEV;
		goto out_err;
	}

	/*
	 * Attempt a get device id command.  If it fails, we probably
	 * don't have a BMC here.
	 */
	rv = try_get_dev_id(new_smi);
	if (rv) {
		if (new_smi->addr_source)
			dev_err(new_smi->dev, "There appears to be no BMC at this location\n");
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
	start_clear_flags(new_smi, false);

	/*
	 * IRQ is defined to be set when non-zero.  req_events will
	 * cause a global flags check that will enable interrupts.
	 */
	if (new_smi->irq) {
		new_smi->interrupt_disabled = false;
		atomic_set(&new_smi->req_events, 1);
	}

	if (new_smi->pdev) {
		rv = platform_device_add(new_smi->pdev);
		if (rv) {
			dev_err(new_smi->dev,
				"Unable to register system interface device: %d\n",
				rv);
			goto out_err;
		}
		new_smi->dev_registered = true;
	}

	rv = ipmi_register_smi(&handlers,
			       new_smi,
			       &new_smi->device_id,
			       new_smi->dev,
			       new_smi->slave_addr);
	if (rv) {
		dev_err(new_smi->dev, "Unable to register device: error %d\n",
			rv);
		goto out_err_stop_timer;
	}

	rv = ipmi_smi_add_proc_entry(new_smi->intf, "type",
				     &smi_type_proc_ops,
				     new_smi);
	if (rv) {
		dev_err(new_smi->dev, "Unable to create proc entry: %d\n", rv);
		goto out_err_stop_timer;
	}

	rv = ipmi_smi_add_proc_entry(new_smi->intf, "si_stats",
				     &smi_si_stats_proc_ops,
				     new_smi);
	if (rv) {
		dev_err(new_smi->dev, "Unable to create proc entry: %d\n", rv);
		goto out_err_stop_timer;
	}

	rv = ipmi_smi_add_proc_entry(new_smi->intf, "params",
				     &smi_params_proc_ops,
				     new_smi);
	if (rv) {
		dev_err(new_smi->dev, "Unable to create proc entry: %d\n", rv);
		goto out_err_stop_timer;
	}

	/* Don't increment till we know we have succeeded. */
	smi_num++;

	dev_info(new_smi->dev, "IPMI %s interface initialized\n",
		 si_to_str[new_smi->si_type]);

	WARN_ON(new_smi->dev->init_name != NULL);
	kfree(init_name);

	return 0;

out_err_stop_timer:
	wait_for_timer_and_thread(new_smi);

out_err:
	new_smi->interrupt_disabled = true;

	if (new_smi->intf) {
		ipmi_smi_t intf = new_smi->intf;
		new_smi->intf = NULL;
		ipmi_unregister_smi(intf);
	}

	if (new_smi->irq_cleanup) {
		new_smi->irq_cleanup(new_smi);
		new_smi->irq_cleanup = NULL;
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
	if (new_smi->addr_source_cleanup) {
		new_smi->addr_source_cleanup(new_smi);
		new_smi->addr_source_cleanup = NULL;
	}
	if (new_smi->io_cleanup) {
		new_smi->io_cleanup(new_smi);
		new_smi->io_cleanup = NULL;
	}

	if (new_smi->dev_registered) {
		platform_device_unregister(new_smi->pdev);
		new_smi->dev_registered = false;
		new_smi->pdev = NULL;
	} else if (new_smi->pdev) {
		platform_device_put(new_smi->pdev);
		new_smi->pdev = NULL;
	}

	kfree(init_name);

	return rv;
}

static int init_ipmi_si(void)
{
	int  i;
	char *str;
	int  rv;
	struct smi_info *e;
	enum ipmi_addr_src type = SI_INVALID;

	if (initialized)
		return 0;
	initialized = 1;

	if (si_tryplatform) {
		rv = platform_driver_register(&ipmi_driver);
		if (rv) {
			pr_err(PFX "Unable to register driver: %d\n", rv);
			return rv;
		}
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

	pr_info("IPMI System Interface driver.\n");

	/* If the user gave us a device, they presumably want us to use it */
	if (!hardcode_find_bmc())
		return 0;

#ifdef CONFIG_PCI
	if (si_trypci) {
		rv = pci_register_driver(&ipmi_pci_driver);
		if (rv)
			pr_err(PFX "Unable to register PCI driver: %d\n", rv);
		else
			pci_registered = true;
	}
#endif

#ifdef CONFIG_ACPI
	if (si_tryacpi)
		spmi_find_bmc();
#endif

#ifdef CONFIG_PARISC
	register_parisc_driver(&ipmi_parisc_driver);
	parisc_registered = true;
#endif

	/* We prefer devices with interrupts, but in the case of a machine
	   with multiple BMCs we assume that there will be several instances
	   of a given type so if we succeed in registering a type then also
	   try to register everything else of the same type */

	mutex_lock(&smi_infos_lock);
	list_for_each_entry(e, &smi_infos, link) {
		/* Try to register a device if it has an IRQ and we either
		   haven't successfully registered a device yet or this
		   device has the same type as one we successfully registered */
		if (e->irq && (!type || e->addr_source == type)) {
			if (!try_smi_init(e)) {
				type = e->addr_source;
			}
		}
	}

	/* type will only have been set if we successfully registered an si */
	if (type) {
		mutex_unlock(&smi_infos_lock);
		return 0;
	}

	/* Fall back to the preferred device */

	list_for_each_entry(e, &smi_infos, link) {
		if (!e->irq && (!type || e->addr_source == type)) {
			if (!try_smi_init(e)) {
				type = e->addr_source;
			}
		}
	}
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

	if (to_clean->dev)
		dev_set_drvdata(to_clean->dev, NULL);

	list_del(&to_clean->link);

	/*
	 * Make sure that interrupts, the timer and the thread are
	 * stopped and will not run again.
	 */
	if (to_clean->irq_cleanup)
		to_clean->irq_cleanup(to_clean);
	wait_for_timer_and_thread(to_clean);

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
		disable_si_irq(to_clean, false);
	while (to_clean->curr_msg || (to_clean->si_state != SI_NORMAL)) {
		poll(to_clean);
		schedule_timeout_uninterruptible(1);
	}

	if (to_clean->handlers)
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

static void cleanup_ipmi_si(void)
{
	struct smi_info *e, *tmp_e;

	if (!initialized)
		return;

#ifdef CONFIG_PCI
	if (pci_registered)
		pci_unregister_driver(&ipmi_pci_driver);
#endif
#ifdef CONFIG_PARISC
	if (parisc_registered)
		unregister_parisc_driver(&ipmi_parisc_driver);
#endif

	platform_driver_unregister(&ipmi_driver);

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
