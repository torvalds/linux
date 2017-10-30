/*
 *  ec.c - ACPI Embedded Controller Driver (v3)
 *
 *  Copyright (C) 2001-2015 Intel Corporation
 *    Author: 2014, 2015 Lv Zheng <lv.zheng@intel.com>
 *            2006, 2007 Alexey Starikovskiy <alexey.y.starikovskiy@intel.com>
 *            2006       Denis Sadykov <denis.m.sadykov@intel.com>
 *            2004       Luming Yu <luming.yu@intel.com>
 *            2001, 2002 Andy Grover <andrew.grover@intel.com>
 *            2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *  Copyright (C) 2008      Alexey Starikovskiy <astarikovskiy@suse.de>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

/* Uncomment next line to get verbose printout */
/* #define DEBUG */
#define pr_fmt(fmt) "ACPI: EC: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <asm/io.h>

#include "internal.h"

#define ACPI_EC_CLASS			"embedded_controller"
#define ACPI_EC_DEVICE_NAME		"Embedded Controller"
#define ACPI_EC_FILE_INFO		"info"

/* EC status register */
#define ACPI_EC_FLAG_OBF	0x01	/* Output buffer full */
#define ACPI_EC_FLAG_IBF	0x02	/* Input buffer full */
#define ACPI_EC_FLAG_CMD	0x08	/* Input buffer contains a command */
#define ACPI_EC_FLAG_BURST	0x10	/* burst mode */
#define ACPI_EC_FLAG_SCI	0x20	/* EC-SCI occurred */

/*
 * The SCI_EVT clearing timing is not defined by the ACPI specification.
 * This leads to lots of practical timing issues for the host EC driver.
 * The following variations are defined (from the target EC firmware's
 * perspective):
 * STATUS: After indicating SCI_EVT edge triggered IRQ to the host, the
 *         target can clear SCI_EVT at any time so long as the host can see
 *         the indication by reading the status register (EC_SC). So the
 *         host should re-check SCI_EVT after the first time the SCI_EVT
 *         indication is seen, which is the same time the query request
 *         (QR_EC) is written to the command register (EC_CMD). SCI_EVT set
 *         at any later time could indicate another event. Normally such
 *         kind of EC firmware has implemented an event queue and will
 *         return 0x00 to indicate "no outstanding event".
 * QUERY: After seeing the query request (QR_EC) written to the command
 *        register (EC_CMD) by the host and having prepared the responding
 *        event value in the data register (EC_DATA), the target can safely
 *        clear SCI_EVT because the target can confirm that the current
 *        event is being handled by the host. The host then should check
 *        SCI_EVT right after reading the event response from the data
 *        register (EC_DATA).
 * EVENT: After seeing the event response read from the data register
 *        (EC_DATA) by the host, the target can clear SCI_EVT. As the
 *        target requires time to notice the change in the data register
 *        (EC_DATA), the host may be required to wait additional guarding
 *        time before checking the SCI_EVT again. Such guarding may not be
 *        necessary if the host is notified via another IRQ.
 */
#define ACPI_EC_EVT_TIMING_STATUS	0x00
#define ACPI_EC_EVT_TIMING_QUERY	0x01
#define ACPI_EC_EVT_TIMING_EVENT	0x02

/* EC commands */
enum ec_command {
	ACPI_EC_COMMAND_READ = 0x80,
	ACPI_EC_COMMAND_WRITE = 0x81,
	ACPI_EC_BURST_ENABLE = 0x82,
	ACPI_EC_BURST_DISABLE = 0x83,
	ACPI_EC_COMMAND_QUERY = 0x84,
};

#define ACPI_EC_DELAY		500	/* Wait 500ms max. during EC ops */
#define ACPI_EC_UDELAY_GLK	1000	/* Wait 1ms max. to get global lock */
#define ACPI_EC_UDELAY_POLL	550	/* Wait 1ms for EC transaction polling */
#define ACPI_EC_CLEAR_MAX	100	/* Maximum number of events to query
					 * when trying to clear the EC */
#define ACPI_EC_MAX_QUERIES	16	/* Maximum number of parallel queries */

enum {
	EC_FLAGS_QUERY_ENABLED,		/* Query is enabled */
	EC_FLAGS_QUERY_PENDING,		/* Query is pending */
	EC_FLAGS_QUERY_GUARDING,	/* Guard for SCI_EVT check */
	EC_FLAGS_GPE_HANDLER_INSTALLED,	/* GPE handler installed */
	EC_FLAGS_EC_HANDLER_INSTALLED,	/* OpReg handler installed */
	EC_FLAGS_EVT_HANDLER_INSTALLED, /* _Qxx handlers installed */
	EC_FLAGS_STARTED,		/* Driver is started */
	EC_FLAGS_STOPPED,		/* Driver is stopped */
	EC_FLAGS_GPE_MASKED,		/* GPE masked */
};

#define ACPI_EC_COMMAND_POLL		0x01 /* Available for command byte */
#define ACPI_EC_COMMAND_COMPLETE	0x02 /* Completed last byte */

/* ec.c is compiled in acpi namespace so this shows up as acpi.ec_delay param */
static unsigned int ec_delay __read_mostly = ACPI_EC_DELAY;
module_param(ec_delay, uint, 0644);
MODULE_PARM_DESC(ec_delay, "Timeout(ms) waited until an EC command completes");

static unsigned int ec_max_queries __read_mostly = ACPI_EC_MAX_QUERIES;
module_param(ec_max_queries, uint, 0644);
MODULE_PARM_DESC(ec_max_queries, "Maximum parallel _Qxx evaluations");

static bool ec_busy_polling __read_mostly;
module_param(ec_busy_polling, bool, 0644);
MODULE_PARM_DESC(ec_busy_polling, "Use busy polling to advance EC transaction");

static unsigned int ec_polling_guard __read_mostly = ACPI_EC_UDELAY_POLL;
module_param(ec_polling_guard, uint, 0644);
MODULE_PARM_DESC(ec_polling_guard, "Guard time(us) between EC accesses in polling modes");

static unsigned int ec_event_clearing __read_mostly = ACPI_EC_EVT_TIMING_QUERY;

/*
 * If the number of false interrupts per one transaction exceeds
 * this threshold, will think there is a GPE storm happened and
 * will disable the GPE for normal transaction.
 */
static unsigned int ec_storm_threshold  __read_mostly = 8;
module_param(ec_storm_threshold, uint, 0644);
MODULE_PARM_DESC(ec_storm_threshold, "Maxim false GPE numbers not considered as GPE storm");

static bool ec_freeze_events __read_mostly = false;
module_param(ec_freeze_events, bool, 0644);
MODULE_PARM_DESC(ec_freeze_events, "Disabling event handling during suspend/resume");

static bool ec_no_wakeup __read_mostly;
module_param(ec_no_wakeup, bool, 0644);
MODULE_PARM_DESC(ec_no_wakeup, "Do not wake up from suspend-to-idle");

struct acpi_ec_query_handler {
	struct list_head node;
	acpi_ec_query_func func;
	acpi_handle handle;
	void *data;
	u8 query_bit;
	struct kref kref;
};

struct transaction {
	const u8 *wdata;
	u8 *rdata;
	unsigned short irq_count;
	u8 command;
	u8 wi;
	u8 ri;
	u8 wlen;
	u8 rlen;
	u8 flags;
};

struct acpi_ec_query {
	struct transaction transaction;
	struct work_struct work;
	struct acpi_ec_query_handler *handler;
};

static int acpi_ec_query(struct acpi_ec *ec, u8 *data);
static void advance_transaction(struct acpi_ec *ec);
static void acpi_ec_event_handler(struct work_struct *work);
static void acpi_ec_event_processor(struct work_struct *work);

struct acpi_ec *boot_ec, *first_ec;
EXPORT_SYMBOL(first_ec);
static bool boot_ec_is_ecdt = false;
static struct workqueue_struct *ec_query_wq;

static int EC_FLAGS_QUERY_HANDSHAKE; /* Needs QR_EC issued when SCI_EVT set */
static int EC_FLAGS_CORRECT_ECDT; /* Needs ECDT port address correction */
static int EC_FLAGS_IGNORE_DSDT_GPE; /* Needs ECDT GPE as correction setting */

/* --------------------------------------------------------------------------
 *                           Logging/Debugging
 * -------------------------------------------------------------------------- */

/*
 * Splitters used by the developers to track the boundary of the EC
 * handling processes.
 */
#ifdef DEBUG
#define EC_DBG_SEP	" "
#define EC_DBG_DRV	"+++++"
#define EC_DBG_STM	"====="
#define EC_DBG_REQ	"*****"
#define EC_DBG_EVT	"#####"
#else
#define EC_DBG_SEP	""
#define EC_DBG_DRV
#define EC_DBG_STM
#define EC_DBG_REQ
#define EC_DBG_EVT
#endif

#define ec_log_raw(fmt, ...) \
	pr_info(fmt "\n", ##__VA_ARGS__)
#define ec_dbg_raw(fmt, ...) \
	pr_debug(fmt "\n", ##__VA_ARGS__)
#define ec_log(filter, fmt, ...) \
	ec_log_raw(filter EC_DBG_SEP fmt EC_DBG_SEP filter, ##__VA_ARGS__)
#define ec_dbg(filter, fmt, ...) \
	ec_dbg_raw(filter EC_DBG_SEP fmt EC_DBG_SEP filter, ##__VA_ARGS__)

#define ec_log_drv(fmt, ...) \
	ec_log(EC_DBG_DRV, fmt, ##__VA_ARGS__)
#define ec_dbg_drv(fmt, ...) \
	ec_dbg(EC_DBG_DRV, fmt, ##__VA_ARGS__)
#define ec_dbg_stm(fmt, ...) \
	ec_dbg(EC_DBG_STM, fmt, ##__VA_ARGS__)
#define ec_dbg_req(fmt, ...) \
	ec_dbg(EC_DBG_REQ, fmt, ##__VA_ARGS__)
#define ec_dbg_evt(fmt, ...) \
	ec_dbg(EC_DBG_EVT, fmt, ##__VA_ARGS__)
#define ec_dbg_ref(ec, fmt, ...) \
	ec_dbg_raw("%lu: " fmt, ec->reference_count, ## __VA_ARGS__)

/* --------------------------------------------------------------------------
 *                           Device Flags
 * -------------------------------------------------------------------------- */

static bool acpi_ec_started(struct acpi_ec *ec)
{
	return test_bit(EC_FLAGS_STARTED, &ec->flags) &&
	       !test_bit(EC_FLAGS_STOPPED, &ec->flags);
}

static bool acpi_ec_event_enabled(struct acpi_ec *ec)
{
	/*
	 * There is an OSPM early stage logic. During the early stages
	 * (boot/resume), OSPMs shouldn't enable the event handling, only
	 * the EC transactions are allowed to be performed.
	 */
	if (!test_bit(EC_FLAGS_QUERY_ENABLED, &ec->flags))
		return false;
	/*
	 * However, disabling the event handling is experimental for late
	 * stage (suspend), and is controlled by the boot parameter of
	 * "ec_freeze_events":
	 * 1. true:  The EC event handling is disabled before entering
	 *           the noirq stage.
	 * 2. false: The EC event handling is automatically disabled as
	 *           soon as the EC driver is stopped.
	 */
	if (ec_freeze_events)
		return acpi_ec_started(ec);
	else
		return test_bit(EC_FLAGS_STARTED, &ec->flags);
}

static bool acpi_ec_flushed(struct acpi_ec *ec)
{
	return ec->reference_count == 1;
}

/* --------------------------------------------------------------------------
 *                           EC Registers
 * -------------------------------------------------------------------------- */

static inline u8 acpi_ec_read_status(struct acpi_ec *ec)
{
	u8 x = inb(ec->command_addr);

	ec_dbg_raw("EC_SC(R) = 0x%2.2x "
		   "SCI_EVT=%d BURST=%d CMD=%d IBF=%d OBF=%d",
		   x,
		   !!(x & ACPI_EC_FLAG_SCI),
		   !!(x & ACPI_EC_FLAG_BURST),
		   !!(x & ACPI_EC_FLAG_CMD),
		   !!(x & ACPI_EC_FLAG_IBF),
		   !!(x & ACPI_EC_FLAG_OBF));
	return x;
}

static inline u8 acpi_ec_read_data(struct acpi_ec *ec)
{
	u8 x = inb(ec->data_addr);

	ec->timestamp = jiffies;
	ec_dbg_raw("EC_DATA(R) = 0x%2.2x", x);
	return x;
}

static inline void acpi_ec_write_cmd(struct acpi_ec *ec, u8 command)
{
	ec_dbg_raw("EC_SC(W) = 0x%2.2x", command);
	outb(command, ec->command_addr);
	ec->timestamp = jiffies;
}

static inline void acpi_ec_write_data(struct acpi_ec *ec, u8 data)
{
	ec_dbg_raw("EC_DATA(W) = 0x%2.2x", data);
	outb(data, ec->data_addr);
	ec->timestamp = jiffies;
}

#if defined(DEBUG) || defined(CONFIG_DYNAMIC_DEBUG)
static const char *acpi_ec_cmd_string(u8 cmd)
{
	switch (cmd) {
	case 0x80:
		return "RD_EC";
	case 0x81:
		return "WR_EC";
	case 0x82:
		return "BE_EC";
	case 0x83:
		return "BD_EC";
	case 0x84:
		return "QR_EC";
	}
	return "UNKNOWN";
}
#else
#define acpi_ec_cmd_string(cmd)		"UNDEF"
#endif

/* --------------------------------------------------------------------------
 *                           GPE Registers
 * -------------------------------------------------------------------------- */

static inline bool acpi_ec_is_gpe_raised(struct acpi_ec *ec)
{
	acpi_event_status gpe_status = 0;

	(void)acpi_get_gpe_status(NULL, ec->gpe, &gpe_status);
	return (gpe_status & ACPI_EVENT_FLAG_STATUS_SET) ? true : false;
}

static inline void acpi_ec_enable_gpe(struct acpi_ec *ec, bool open)
{
	if (open)
		acpi_enable_gpe(NULL, ec->gpe);
	else {
		BUG_ON(ec->reference_count < 1);
		acpi_set_gpe(NULL, ec->gpe, ACPI_GPE_ENABLE);
	}
	if (acpi_ec_is_gpe_raised(ec)) {
		/*
		 * On some platforms, EN=1 writes cannot trigger GPE. So
		 * software need to manually trigger a pseudo GPE event on
		 * EN=1 writes.
		 */
		ec_dbg_raw("Polling quirk");
		advance_transaction(ec);
	}
}

static inline void acpi_ec_disable_gpe(struct acpi_ec *ec, bool close)
{
	if (close)
		acpi_disable_gpe(NULL, ec->gpe);
	else {
		BUG_ON(ec->reference_count < 1);
		acpi_set_gpe(NULL, ec->gpe, ACPI_GPE_DISABLE);
	}
}

static inline void acpi_ec_clear_gpe(struct acpi_ec *ec)
{
	/*
	 * GPE STS is a W1C register, which means:
	 * 1. Software can clear it without worrying about clearing other
	 *    GPEs' STS bits when the hardware sets them in parallel.
	 * 2. As long as software can ensure only clearing it when it is
	 *    set, hardware won't set it in parallel.
	 * So software can clear GPE in any contexts.
	 * Warning: do not move the check into advance_transaction() as the
	 * EC commands will be sent without GPE raised.
	 */
	if (!acpi_ec_is_gpe_raised(ec))
		return;
	acpi_clear_gpe(NULL, ec->gpe);
}

/* --------------------------------------------------------------------------
 *                           Transaction Management
 * -------------------------------------------------------------------------- */

static void acpi_ec_submit_request(struct acpi_ec *ec)
{
	ec->reference_count++;
	if (test_bit(EC_FLAGS_GPE_HANDLER_INSTALLED, &ec->flags) &&
	    ec->reference_count == 1)
		acpi_ec_enable_gpe(ec, true);
}

static void acpi_ec_complete_request(struct acpi_ec *ec)
{
	bool flushed = false;

	ec->reference_count--;
	if (test_bit(EC_FLAGS_GPE_HANDLER_INSTALLED, &ec->flags) &&
	    ec->reference_count == 0)
		acpi_ec_disable_gpe(ec, true);
	flushed = acpi_ec_flushed(ec);
	if (flushed)
		wake_up(&ec->wait);
}

static void acpi_ec_mask_gpe(struct acpi_ec *ec)
{
	if (!test_bit(EC_FLAGS_GPE_MASKED, &ec->flags)) {
		acpi_ec_disable_gpe(ec, false);
		ec_dbg_drv("Polling enabled");
		set_bit(EC_FLAGS_GPE_MASKED, &ec->flags);
	}
}

static void acpi_ec_unmask_gpe(struct acpi_ec *ec)
{
	if (test_bit(EC_FLAGS_GPE_MASKED, &ec->flags)) {
		clear_bit(EC_FLAGS_GPE_MASKED, &ec->flags);
		acpi_ec_enable_gpe(ec, false);
		ec_dbg_drv("Polling disabled");
	}
}

/*
 * acpi_ec_submit_flushable_request() - Increase the reference count unless
 *                                      the flush operation is not in
 *                                      progress
 * @ec: the EC device
 *
 * This function must be used before taking a new action that should hold
 * the reference count.  If this function returns false, then the action
 * must be discarded or it will prevent the flush operation from being
 * completed.
 */
static bool acpi_ec_submit_flushable_request(struct acpi_ec *ec)
{
	if (!acpi_ec_started(ec))
		return false;
	acpi_ec_submit_request(ec);
	return true;
}

static void acpi_ec_submit_query(struct acpi_ec *ec)
{
	acpi_ec_mask_gpe(ec);
	if (!acpi_ec_event_enabled(ec))
		return;
	if (!test_and_set_bit(EC_FLAGS_QUERY_PENDING, &ec->flags)) {
		ec_dbg_evt("Command(%s) submitted/blocked",
			   acpi_ec_cmd_string(ACPI_EC_COMMAND_QUERY));
		ec->nr_pending_queries++;
		schedule_work(&ec->work);
	}
}

static void acpi_ec_complete_query(struct acpi_ec *ec)
{
	if (test_and_clear_bit(EC_FLAGS_QUERY_PENDING, &ec->flags))
		ec_dbg_evt("Command(%s) unblocked",
			   acpi_ec_cmd_string(ACPI_EC_COMMAND_QUERY));
	acpi_ec_unmask_gpe(ec);
}

static inline void __acpi_ec_enable_event(struct acpi_ec *ec)
{
	if (!test_and_set_bit(EC_FLAGS_QUERY_ENABLED, &ec->flags))
		ec_log_drv("event unblocked");
	if (!test_bit(EC_FLAGS_QUERY_PENDING, &ec->flags))
		advance_transaction(ec);
}

static inline void __acpi_ec_disable_event(struct acpi_ec *ec)
{
	if (test_and_clear_bit(EC_FLAGS_QUERY_ENABLED, &ec->flags))
		ec_log_drv("event blocked");
}

static void acpi_ec_enable_event(struct acpi_ec *ec)
{
	unsigned long flags;

	spin_lock_irqsave(&ec->lock, flags);
	if (acpi_ec_started(ec))
		__acpi_ec_enable_event(ec);
	spin_unlock_irqrestore(&ec->lock, flags);
}

#ifdef CONFIG_PM_SLEEP
static bool acpi_ec_query_flushed(struct acpi_ec *ec)
{
	bool flushed;
	unsigned long flags;

	spin_lock_irqsave(&ec->lock, flags);
	flushed = !ec->nr_pending_queries;
	spin_unlock_irqrestore(&ec->lock, flags);
	return flushed;
}

static void __acpi_ec_flush_event(struct acpi_ec *ec)
{
	/*
	 * When ec_freeze_events is true, we need to flush events in
	 * the proper position before entering the noirq stage.
	 */
	wait_event(ec->wait, acpi_ec_query_flushed(ec));
	if (ec_query_wq)
		flush_workqueue(ec_query_wq);
}

static void acpi_ec_disable_event(struct acpi_ec *ec)
{
	unsigned long flags;

	spin_lock_irqsave(&ec->lock, flags);
	__acpi_ec_disable_event(ec);
	spin_unlock_irqrestore(&ec->lock, flags);
	__acpi_ec_flush_event(ec);
}

void acpi_ec_flush_work(void)
{
	if (first_ec)
		__acpi_ec_flush_event(first_ec);

	flush_scheduled_work();
}
#endif /* CONFIG_PM_SLEEP */

static bool acpi_ec_guard_event(struct acpi_ec *ec)
{
	bool guarded = true;
	unsigned long flags;

	spin_lock_irqsave(&ec->lock, flags);
	/*
	 * If firmware SCI_EVT clearing timing is "event", we actually
	 * don't know when the SCI_EVT will be cleared by firmware after
	 * evaluating _Qxx, so we need to re-check SCI_EVT after waiting an
	 * acceptable period.
	 *
	 * The guarding period begins when EC_FLAGS_QUERY_PENDING is
	 * flagged, which means SCI_EVT check has just been performed.
	 * But if the current transaction is ACPI_EC_COMMAND_QUERY, the
	 * guarding should have already been performed (via
	 * EC_FLAGS_QUERY_GUARDING) and should not be applied so that the
	 * ACPI_EC_COMMAND_QUERY transaction can be transitioned into
	 * ACPI_EC_COMMAND_POLL state immediately.
	 */
	if (ec_event_clearing == ACPI_EC_EVT_TIMING_STATUS ||
	    ec_event_clearing == ACPI_EC_EVT_TIMING_QUERY ||
	    !test_bit(EC_FLAGS_QUERY_PENDING, &ec->flags) ||
	    (ec->curr && ec->curr->command == ACPI_EC_COMMAND_QUERY))
		guarded = false;
	spin_unlock_irqrestore(&ec->lock, flags);
	return guarded;
}

static int ec_transaction_polled(struct acpi_ec *ec)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&ec->lock, flags);
	if (ec->curr && (ec->curr->flags & ACPI_EC_COMMAND_POLL))
		ret = 1;
	spin_unlock_irqrestore(&ec->lock, flags);
	return ret;
}

static int ec_transaction_completed(struct acpi_ec *ec)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&ec->lock, flags);
	if (ec->curr && (ec->curr->flags & ACPI_EC_COMMAND_COMPLETE))
		ret = 1;
	spin_unlock_irqrestore(&ec->lock, flags);
	return ret;
}

static inline void ec_transaction_transition(struct acpi_ec *ec, unsigned long flag)
{
	ec->curr->flags |= flag;
	if (ec->curr->command == ACPI_EC_COMMAND_QUERY) {
		if (ec_event_clearing == ACPI_EC_EVT_TIMING_STATUS &&
		    flag == ACPI_EC_COMMAND_POLL)
			acpi_ec_complete_query(ec);
		if (ec_event_clearing == ACPI_EC_EVT_TIMING_QUERY &&
		    flag == ACPI_EC_COMMAND_COMPLETE)
			acpi_ec_complete_query(ec);
		if (ec_event_clearing == ACPI_EC_EVT_TIMING_EVENT &&
		    flag == ACPI_EC_COMMAND_COMPLETE)
			set_bit(EC_FLAGS_QUERY_GUARDING, &ec->flags);
	}
}

static void advance_transaction(struct acpi_ec *ec)
{
	struct transaction *t;
	u8 status;
	bool wakeup = false;

	ec_dbg_stm("%s (%d)", in_interrupt() ? "IRQ" : "TASK",
		   smp_processor_id());
	/*
	 * By always clearing STS before handling all indications, we can
	 * ensure a hardware STS 0->1 change after this clearing can always
	 * trigger a GPE interrupt.
	 */
	acpi_ec_clear_gpe(ec);
	status = acpi_ec_read_status(ec);
	t = ec->curr;
	/*
	 * Another IRQ or a guarded polling mode advancement is detected,
	 * the next QR_EC submission is then allowed.
	 */
	if (!t || !(t->flags & ACPI_EC_COMMAND_POLL)) {
		if (ec_event_clearing == ACPI_EC_EVT_TIMING_EVENT &&
		    (!ec->nr_pending_queries ||
		     test_bit(EC_FLAGS_QUERY_GUARDING, &ec->flags))) {
			clear_bit(EC_FLAGS_QUERY_GUARDING, &ec->flags);
			acpi_ec_complete_query(ec);
		}
	}
	if (!t)
		goto err;
	if (t->flags & ACPI_EC_COMMAND_POLL) {
		if (t->wlen > t->wi) {
			if ((status & ACPI_EC_FLAG_IBF) == 0)
				acpi_ec_write_data(ec, t->wdata[t->wi++]);
			else
				goto err;
		} else if (t->rlen > t->ri) {
			if ((status & ACPI_EC_FLAG_OBF) == 1) {
				t->rdata[t->ri++] = acpi_ec_read_data(ec);
				if (t->rlen == t->ri) {
					ec_transaction_transition(ec, ACPI_EC_COMMAND_COMPLETE);
					if (t->command == ACPI_EC_COMMAND_QUERY)
						ec_dbg_evt("Command(%s) completed by hardware",
							   acpi_ec_cmd_string(ACPI_EC_COMMAND_QUERY));
					wakeup = true;
				}
			} else
				goto err;
		} else if (t->wlen == t->wi &&
			   (status & ACPI_EC_FLAG_IBF) == 0) {
			ec_transaction_transition(ec, ACPI_EC_COMMAND_COMPLETE);
			wakeup = true;
		}
		goto out;
	} else {
		if (EC_FLAGS_QUERY_HANDSHAKE &&
		    !(status & ACPI_EC_FLAG_SCI) &&
		    (t->command == ACPI_EC_COMMAND_QUERY)) {
			ec_transaction_transition(ec, ACPI_EC_COMMAND_POLL);
			t->rdata[t->ri++] = 0x00;
			ec_transaction_transition(ec, ACPI_EC_COMMAND_COMPLETE);
			ec_dbg_evt("Command(%s) completed by software",
				   acpi_ec_cmd_string(ACPI_EC_COMMAND_QUERY));
			wakeup = true;
		} else if ((status & ACPI_EC_FLAG_IBF) == 0) {
			acpi_ec_write_cmd(ec, t->command);
			ec_transaction_transition(ec, ACPI_EC_COMMAND_POLL);
		} else
			goto err;
		goto out;
	}
err:
	/*
	 * If SCI bit is set, then don't think it's a false IRQ
	 * otherwise will take a not handled IRQ as a false one.
	 */
	if (!(status & ACPI_EC_FLAG_SCI)) {
		if (in_interrupt() && t) {
			if (t->irq_count < ec_storm_threshold)
				++t->irq_count;
			/* Allow triggering on 0 threshold */
			if (t->irq_count == ec_storm_threshold)
				acpi_ec_mask_gpe(ec);
		}
	}
out:
	if (status & ACPI_EC_FLAG_SCI)
		acpi_ec_submit_query(ec);
	if (wakeup && in_interrupt())
		wake_up(&ec->wait);
}

static void start_transaction(struct acpi_ec *ec)
{
	ec->curr->irq_count = ec->curr->wi = ec->curr->ri = 0;
	ec->curr->flags = 0;
}

static int ec_guard(struct acpi_ec *ec)
{
	unsigned long guard = usecs_to_jiffies(ec->polling_guard);
	unsigned long timeout = ec->timestamp + guard;

	/* Ensure guarding period before polling EC status */
	do {
		if (ec->busy_polling) {
			/* Perform busy polling */
			if (ec_transaction_completed(ec))
				return 0;
			udelay(jiffies_to_usecs(guard));
		} else {
			/*
			 * Perform wait polling
			 * 1. Wait the transaction to be completed by the
			 *    GPE handler after the transaction enters
			 *    ACPI_EC_COMMAND_POLL state.
			 * 2. A special guarding logic is also required
			 *    for event clearing mode "event" before the
			 *    transaction enters ACPI_EC_COMMAND_POLL
			 *    state.
			 */
			if (!ec_transaction_polled(ec) &&
			    !acpi_ec_guard_event(ec))
				break;
			if (wait_event_timeout(ec->wait,
					       ec_transaction_completed(ec),
					       guard))
				return 0;
		}
	} while (time_before(jiffies, timeout));
	return -ETIME;
}

static int ec_poll(struct acpi_ec *ec)
{
	unsigned long flags;
	int repeat = 5; /* number of command restarts */

	while (repeat--) {
		unsigned long delay = jiffies +
			msecs_to_jiffies(ec_delay);
		do {
			if (!ec_guard(ec))
				return 0;
			spin_lock_irqsave(&ec->lock, flags);
			advance_transaction(ec);
			spin_unlock_irqrestore(&ec->lock, flags);
		} while (time_before(jiffies, delay));
		pr_debug("controller reset, restart transaction\n");
		spin_lock_irqsave(&ec->lock, flags);
		start_transaction(ec);
		spin_unlock_irqrestore(&ec->lock, flags);
	}
	return -ETIME;
}

static int acpi_ec_transaction_unlocked(struct acpi_ec *ec,
					struct transaction *t)
{
	unsigned long tmp;
	int ret = 0;

	/* start transaction */
	spin_lock_irqsave(&ec->lock, tmp);
	/* Enable GPE for command processing (IBF=0/OBF=1) */
	if (!acpi_ec_submit_flushable_request(ec)) {
		ret = -EINVAL;
		goto unlock;
	}
	ec_dbg_ref(ec, "Increase command");
	/* following two actions should be kept atomic */
	ec->curr = t;
	ec_dbg_req("Command(%s) started", acpi_ec_cmd_string(t->command));
	start_transaction(ec);
	spin_unlock_irqrestore(&ec->lock, tmp);

	ret = ec_poll(ec);

	spin_lock_irqsave(&ec->lock, tmp);
	if (t->irq_count == ec_storm_threshold)
		acpi_ec_unmask_gpe(ec);
	ec_dbg_req("Command(%s) stopped", acpi_ec_cmd_string(t->command));
	ec->curr = NULL;
	/* Disable GPE for command processing (IBF=0/OBF=1) */
	acpi_ec_complete_request(ec);
	ec_dbg_ref(ec, "Decrease command");
unlock:
	spin_unlock_irqrestore(&ec->lock, tmp);
	return ret;
}

static int acpi_ec_transaction(struct acpi_ec *ec, struct transaction *t)
{
	int status;
	u32 glk;

	if (!ec || (!t) || (t->wlen && !t->wdata) || (t->rlen && !t->rdata))
		return -EINVAL;
	if (t->rdata)
		memset(t->rdata, 0, t->rlen);

	mutex_lock(&ec->mutex);
	if (ec->global_lock) {
		status = acpi_acquire_global_lock(ACPI_EC_UDELAY_GLK, &glk);
		if (ACPI_FAILURE(status)) {
			status = -ENODEV;
			goto unlock;
		}
	}

	status = acpi_ec_transaction_unlocked(ec, t);

	if (ec->global_lock)
		acpi_release_global_lock(glk);
unlock:
	mutex_unlock(&ec->mutex);
	return status;
}

static int acpi_ec_burst_enable(struct acpi_ec *ec)
{
	u8 d;
	struct transaction t = {.command = ACPI_EC_BURST_ENABLE,
				.wdata = NULL, .rdata = &d,
				.wlen = 0, .rlen = 1};

	return acpi_ec_transaction(ec, &t);
}

static int acpi_ec_burst_disable(struct acpi_ec *ec)
{
	struct transaction t = {.command = ACPI_EC_BURST_DISABLE,
				.wdata = NULL, .rdata = NULL,
				.wlen = 0, .rlen = 0};

	return (acpi_ec_read_status(ec) & ACPI_EC_FLAG_BURST) ?
				acpi_ec_transaction(ec, &t) : 0;
}

static int acpi_ec_read(struct acpi_ec *ec, u8 address, u8 *data)
{
	int result;
	u8 d;
	struct transaction t = {.command = ACPI_EC_COMMAND_READ,
				.wdata = &address, .rdata = &d,
				.wlen = 1, .rlen = 1};

	result = acpi_ec_transaction(ec, &t);
	*data = d;
	return result;
}

static int acpi_ec_write(struct acpi_ec *ec, u8 address, u8 data)
{
	u8 wdata[2] = { address, data };
	struct transaction t = {.command = ACPI_EC_COMMAND_WRITE,
				.wdata = wdata, .rdata = NULL,
				.wlen = 2, .rlen = 0};

	return acpi_ec_transaction(ec, &t);
}

int ec_read(u8 addr, u8 *val)
{
	int err;
	u8 temp_data;

	if (!first_ec)
		return -ENODEV;

	err = acpi_ec_read(first_ec, addr, &temp_data);

	if (!err) {
		*val = temp_data;
		return 0;
	}
	return err;
}
EXPORT_SYMBOL(ec_read);

int ec_write(u8 addr, u8 val)
{
	int err;

	if (!first_ec)
		return -ENODEV;

	err = acpi_ec_write(first_ec, addr, val);

	return err;
}
EXPORT_SYMBOL(ec_write);

int ec_transaction(u8 command,
		   const u8 *wdata, unsigned wdata_len,
		   u8 *rdata, unsigned rdata_len)
{
	struct transaction t = {.command = command,
				.wdata = wdata, .rdata = rdata,
				.wlen = wdata_len, .rlen = rdata_len};

	if (!first_ec)
		return -ENODEV;

	return acpi_ec_transaction(first_ec, &t);
}
EXPORT_SYMBOL(ec_transaction);

/* Get the handle to the EC device */
acpi_handle ec_get_handle(void)
{
	if (!first_ec)
		return NULL;
	return first_ec->handle;
}
EXPORT_SYMBOL(ec_get_handle);

static void acpi_ec_start(struct acpi_ec *ec, bool resuming)
{
	unsigned long flags;

	spin_lock_irqsave(&ec->lock, flags);
	if (!test_and_set_bit(EC_FLAGS_STARTED, &ec->flags)) {
		ec_dbg_drv("Starting EC");
		/* Enable GPE for event processing (SCI_EVT=1) */
		if (!resuming) {
			acpi_ec_submit_request(ec);
			ec_dbg_ref(ec, "Increase driver");
		}
		ec_log_drv("EC started");
	}
	spin_unlock_irqrestore(&ec->lock, flags);
}

static bool acpi_ec_stopped(struct acpi_ec *ec)
{
	unsigned long flags;
	bool flushed;

	spin_lock_irqsave(&ec->lock, flags);
	flushed = acpi_ec_flushed(ec);
	spin_unlock_irqrestore(&ec->lock, flags);
	return flushed;
}

static void acpi_ec_stop(struct acpi_ec *ec, bool suspending)
{
	unsigned long flags;

	spin_lock_irqsave(&ec->lock, flags);
	if (acpi_ec_started(ec)) {
		ec_dbg_drv("Stopping EC");
		set_bit(EC_FLAGS_STOPPED, &ec->flags);
		spin_unlock_irqrestore(&ec->lock, flags);
		wait_event(ec->wait, acpi_ec_stopped(ec));
		spin_lock_irqsave(&ec->lock, flags);
		/* Disable GPE for event processing (SCI_EVT=1) */
		if (!suspending) {
			acpi_ec_complete_request(ec);
			ec_dbg_ref(ec, "Decrease driver");
		} else if (!ec_freeze_events)
			__acpi_ec_disable_event(ec);
		clear_bit(EC_FLAGS_STARTED, &ec->flags);
		clear_bit(EC_FLAGS_STOPPED, &ec->flags);
		ec_log_drv("EC stopped");
	}
	spin_unlock_irqrestore(&ec->lock, flags);
}

static void acpi_ec_enter_noirq(struct acpi_ec *ec)
{
	unsigned long flags;

	spin_lock_irqsave(&ec->lock, flags);
	ec->busy_polling = true;
	ec->polling_guard = 0;
	ec_log_drv("interrupt blocked");
	spin_unlock_irqrestore(&ec->lock, flags);
}

static void acpi_ec_leave_noirq(struct acpi_ec *ec)
{
	unsigned long flags;

	spin_lock_irqsave(&ec->lock, flags);
	ec->busy_polling = ec_busy_polling;
	ec->polling_guard = ec_polling_guard;
	ec_log_drv("interrupt unblocked");
	spin_unlock_irqrestore(&ec->lock, flags);
}

void acpi_ec_block_transactions(void)
{
	struct acpi_ec *ec = first_ec;

	if (!ec)
		return;

	mutex_lock(&ec->mutex);
	/* Prevent transactions from being carried out */
	acpi_ec_stop(ec, true);
	mutex_unlock(&ec->mutex);
}

void acpi_ec_unblock_transactions(void)
{
	/*
	 * Allow transactions to happen again (this function is called from
	 * atomic context during wakeup, so we don't need to acquire the mutex).
	 */
	if (first_ec)
		acpi_ec_start(first_ec, true);
}

/* --------------------------------------------------------------------------
                                Event Management
   -------------------------------------------------------------------------- */
static struct acpi_ec_query_handler *
acpi_ec_get_query_handler(struct acpi_ec_query_handler *handler)
{
	if (handler)
		kref_get(&handler->kref);
	return handler;
}

static struct acpi_ec_query_handler *
acpi_ec_get_query_handler_by_value(struct acpi_ec *ec, u8 value)
{
	struct acpi_ec_query_handler *handler;
	bool found = false;

	mutex_lock(&ec->mutex);
	list_for_each_entry(handler, &ec->list, node) {
		if (value == handler->query_bit) {
			found = true;
			break;
		}
	}
	mutex_unlock(&ec->mutex);
	return found ? acpi_ec_get_query_handler(handler) : NULL;
}

static void acpi_ec_query_handler_release(struct kref *kref)
{
	struct acpi_ec_query_handler *handler =
		container_of(kref, struct acpi_ec_query_handler, kref);

	kfree(handler);
}

static void acpi_ec_put_query_handler(struct acpi_ec_query_handler *handler)
{
	kref_put(&handler->kref, acpi_ec_query_handler_release);
}

int acpi_ec_add_query_handler(struct acpi_ec *ec, u8 query_bit,
			      acpi_handle handle, acpi_ec_query_func func,
			      void *data)
{
	struct acpi_ec_query_handler *handler =
	    kzalloc(sizeof(struct acpi_ec_query_handler), GFP_KERNEL);

	if (!handler)
		return -ENOMEM;

	handler->query_bit = query_bit;
	handler->handle = handle;
	handler->func = func;
	handler->data = data;
	mutex_lock(&ec->mutex);
	kref_init(&handler->kref);
	list_add(&handler->node, &ec->list);
	mutex_unlock(&ec->mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(acpi_ec_add_query_handler);

static void acpi_ec_remove_query_handlers(struct acpi_ec *ec,
					  bool remove_all, u8 query_bit)
{
	struct acpi_ec_query_handler *handler, *tmp;
	LIST_HEAD(free_list);

	mutex_lock(&ec->mutex);
	list_for_each_entry_safe(handler, tmp, &ec->list, node) {
		if (remove_all || query_bit == handler->query_bit) {
			list_del_init(&handler->node);
			list_add(&handler->node, &free_list);
		}
	}
	mutex_unlock(&ec->mutex);
	list_for_each_entry_safe(handler, tmp, &free_list, node)
		acpi_ec_put_query_handler(handler);
}

void acpi_ec_remove_query_handler(struct acpi_ec *ec, u8 query_bit)
{
	acpi_ec_remove_query_handlers(ec, false, query_bit);
}
EXPORT_SYMBOL_GPL(acpi_ec_remove_query_handler);

static struct acpi_ec_query *acpi_ec_create_query(u8 *pval)
{
	struct acpi_ec_query *q;
	struct transaction *t;

	q = kzalloc(sizeof (struct acpi_ec_query), GFP_KERNEL);
	if (!q)
		return NULL;
	INIT_WORK(&q->work, acpi_ec_event_processor);
	t = &q->transaction;
	t->command = ACPI_EC_COMMAND_QUERY;
	t->rdata = pval;
	t->rlen = 1;
	return q;
}

static void acpi_ec_delete_query(struct acpi_ec_query *q)
{
	if (q) {
		if (q->handler)
			acpi_ec_put_query_handler(q->handler);
		kfree(q);
	}
}

static void acpi_ec_event_processor(struct work_struct *work)
{
	struct acpi_ec_query *q = container_of(work, struct acpi_ec_query, work);
	struct acpi_ec_query_handler *handler = q->handler;

	ec_dbg_evt("Query(0x%02x) started", handler->query_bit);
	if (handler->func)
		handler->func(handler->data);
	else if (handler->handle)
		acpi_evaluate_object(handler->handle, NULL, NULL, NULL);
	ec_dbg_evt("Query(0x%02x) stopped", handler->query_bit);
	acpi_ec_delete_query(q);
}

static int acpi_ec_query(struct acpi_ec *ec, u8 *data)
{
	u8 value = 0;
	int result;
	struct acpi_ec_query *q;

	q = acpi_ec_create_query(&value);
	if (!q)
		return -ENOMEM;

	/*
	 * Query the EC to find out which _Qxx method we need to evaluate.
	 * Note that successful completion of the query causes the ACPI_EC_SCI
	 * bit to be cleared (and thus clearing the interrupt source).
	 */
	result = acpi_ec_transaction(ec, &q->transaction);
	if (!value)
		result = -ENODATA;
	if (result)
		goto err_exit;

	q->handler = acpi_ec_get_query_handler_by_value(ec, value);
	if (!q->handler) {
		result = -ENODATA;
		goto err_exit;
	}

	/*
	 * It is reported that _Qxx are evaluated in a parallel way on
	 * Windows:
	 * https://bugzilla.kernel.org/show_bug.cgi?id=94411
	 *
	 * Put this log entry before schedule_work() in order to make
	 * it appearing before any other log entries occurred during the
	 * work queue execution.
	 */
	ec_dbg_evt("Query(0x%02x) scheduled", value);
	if (!queue_work(ec_query_wq, &q->work)) {
		ec_dbg_evt("Query(0x%02x) overlapped", value);
		result = -EBUSY;
	}

err_exit:
	if (result)
		acpi_ec_delete_query(q);
	if (data)
		*data = value;
	return result;
}

static void acpi_ec_check_event(struct acpi_ec *ec)
{
	unsigned long flags;

	if (ec_event_clearing == ACPI_EC_EVT_TIMING_EVENT) {
		if (ec_guard(ec)) {
			spin_lock_irqsave(&ec->lock, flags);
			/*
			 * Take care of the SCI_EVT unless no one else is
			 * taking care of it.
			 */
			if (!ec->curr)
				advance_transaction(ec);
			spin_unlock_irqrestore(&ec->lock, flags);
		}
	}
}

static void acpi_ec_event_handler(struct work_struct *work)
{
	unsigned long flags;
	struct acpi_ec *ec = container_of(work, struct acpi_ec, work);

	ec_dbg_evt("Event started");

	spin_lock_irqsave(&ec->lock, flags);
	while (ec->nr_pending_queries) {
		spin_unlock_irqrestore(&ec->lock, flags);
		(void)acpi_ec_query(ec, NULL);
		spin_lock_irqsave(&ec->lock, flags);
		ec->nr_pending_queries--;
		/*
		 * Before exit, make sure that this work item can be
		 * scheduled again. There might be QR_EC failures, leaving
		 * EC_FLAGS_QUERY_PENDING uncleared and preventing this work
		 * item from being scheduled again.
		 */
		if (!ec->nr_pending_queries) {
			if (ec_event_clearing == ACPI_EC_EVT_TIMING_STATUS ||
			    ec_event_clearing == ACPI_EC_EVT_TIMING_QUERY)
				acpi_ec_complete_query(ec);
		}
	}
	spin_unlock_irqrestore(&ec->lock, flags);

	ec_dbg_evt("Event stopped");

	acpi_ec_check_event(ec);
}

static u32 acpi_ec_gpe_handler(acpi_handle gpe_device,
	u32 gpe_number, void *data)
{
	unsigned long flags;
	struct acpi_ec *ec = data;

	spin_lock_irqsave(&ec->lock, flags);
	advance_transaction(ec);
	spin_unlock_irqrestore(&ec->lock, flags);
	return ACPI_INTERRUPT_HANDLED;
}

/* --------------------------------------------------------------------------
 *                           Address Space Management
 * -------------------------------------------------------------------------- */

static acpi_status
acpi_ec_space_handler(u32 function, acpi_physical_address address,
		      u32 bits, u64 *value64,
		      void *handler_context, void *region_context)
{
	struct acpi_ec *ec = handler_context;
	int result = 0, i, bytes = bits / 8;
	u8 *value = (u8 *)value64;

	if ((address > 0xFF) || !value || !handler_context)
		return AE_BAD_PARAMETER;

	if (function != ACPI_READ && function != ACPI_WRITE)
		return AE_BAD_PARAMETER;

	if (ec->busy_polling || bits > 8)
		acpi_ec_burst_enable(ec);

	for (i = 0; i < bytes; ++i, ++address, ++value)
		result = (function == ACPI_READ) ?
			acpi_ec_read(ec, address, value) :
			acpi_ec_write(ec, address, *value);

	if (ec->busy_polling || bits > 8)
		acpi_ec_burst_disable(ec);

	switch (result) {
	case -EINVAL:
		return AE_BAD_PARAMETER;
	case -ENODEV:
		return AE_NOT_FOUND;
	case -ETIME:
		return AE_TIME;
	default:
		return AE_OK;
	}
}

/* --------------------------------------------------------------------------
 *                             Driver Interface
 * -------------------------------------------------------------------------- */

static acpi_status
ec_parse_io_ports(struct acpi_resource *resource, void *context);

static void acpi_ec_free(struct acpi_ec *ec)
{
	if (first_ec == ec)
		first_ec = NULL;
	if (boot_ec == ec)
		boot_ec = NULL;
	kfree(ec);
}

static struct acpi_ec *acpi_ec_alloc(void)
{
	struct acpi_ec *ec = kzalloc(sizeof(struct acpi_ec), GFP_KERNEL);

	if (!ec)
		return NULL;
	mutex_init(&ec->mutex);
	init_waitqueue_head(&ec->wait);
	INIT_LIST_HEAD(&ec->list);
	spin_lock_init(&ec->lock);
	INIT_WORK(&ec->work, acpi_ec_event_handler);
	ec->timestamp = jiffies;
	ec->busy_polling = true;
	ec->polling_guard = 0;
	return ec;
}

static acpi_status
acpi_ec_register_query_methods(acpi_handle handle, u32 level,
			       void *context, void **return_value)
{
	char node_name[5];
	struct acpi_buffer buffer = { sizeof(node_name), node_name };
	struct acpi_ec *ec = context;
	int value = 0;
	acpi_status status;

	status = acpi_get_name(handle, ACPI_SINGLE_NAME, &buffer);

	if (ACPI_SUCCESS(status) && sscanf(node_name, "_Q%x", &value) == 1)
		acpi_ec_add_query_handler(ec, value, handle, NULL, NULL);
	return AE_OK;
}

static acpi_status
ec_parse_device(acpi_handle handle, u32 Level, void *context, void **retval)
{
	acpi_status status;
	unsigned long long tmp = 0;
	struct acpi_ec *ec = context;

	/* clear addr values, ec_parse_io_ports depend on it */
	ec->command_addr = ec->data_addr = 0;

	status = acpi_walk_resources(handle, METHOD_NAME__CRS,
				     ec_parse_io_ports, ec);
	if (ACPI_FAILURE(status))
		return status;
	if (ec->data_addr == 0 || ec->command_addr == 0)
		return AE_OK;

	if (boot_ec && boot_ec_is_ecdt && EC_FLAGS_IGNORE_DSDT_GPE) {
		/*
		 * Always inherit the GPE number setting from the ECDT
		 * EC.
		 */
		ec->gpe = boot_ec->gpe;
	} else {
		/* Get GPE bit assignment (EC events). */
		/* TODO: Add support for _GPE returning a package */
		status = acpi_evaluate_integer(handle, "_GPE", NULL, &tmp);
		if (ACPI_FAILURE(status))
			return status;
		ec->gpe = tmp;
	}
	/* Use the global lock for all EC transactions? */
	tmp = 0;
	acpi_evaluate_integer(handle, "_GLK", NULL, &tmp);
	ec->global_lock = tmp;
	ec->handle = handle;
	return AE_CTRL_TERMINATE;
}

/*
 * Note: This function returns an error code only when the address space
 *       handler is not installed, which means "not able to handle
 *       transactions".
 */
static int ec_install_handlers(struct acpi_ec *ec, bool handle_events)
{
	acpi_status status;

	acpi_ec_start(ec, false);

	if (!test_bit(EC_FLAGS_EC_HANDLER_INSTALLED, &ec->flags)) {
		acpi_ec_enter_noirq(ec);
		status = acpi_install_address_space_handler(ec->handle,
							    ACPI_ADR_SPACE_EC,
							    &acpi_ec_space_handler,
							    NULL, ec);
		if (ACPI_FAILURE(status)) {
			if (status == AE_NOT_FOUND) {
				/*
				 * Maybe OS fails in evaluating the _REG
				 * object. The AE_NOT_FOUND error will be
				 * ignored and OS * continue to initialize
				 * EC.
				 */
				pr_err("Fail in evaluating the _REG object"
					" of EC device. Broken bios is suspected.\n");
			} else {
				acpi_ec_stop(ec, false);
				return -ENODEV;
			}
		}
		set_bit(EC_FLAGS_EC_HANDLER_INSTALLED, &ec->flags);
	}

	if (!handle_events)
		return 0;

	if (!test_bit(EC_FLAGS_EVT_HANDLER_INSTALLED, &ec->flags)) {
		/* Find and register all query methods */
		acpi_walk_namespace(ACPI_TYPE_METHOD, ec->handle, 1,
				    acpi_ec_register_query_methods,
				    NULL, ec, NULL);
		set_bit(EC_FLAGS_EVT_HANDLER_INSTALLED, &ec->flags);
	}
	if (!test_bit(EC_FLAGS_GPE_HANDLER_INSTALLED, &ec->flags)) {
		status = acpi_install_gpe_raw_handler(NULL, ec->gpe,
					  ACPI_GPE_EDGE_TRIGGERED,
					  &acpi_ec_gpe_handler, ec);
		/* This is not fatal as we can poll EC events */
		if (ACPI_SUCCESS(status)) {
			set_bit(EC_FLAGS_GPE_HANDLER_INSTALLED, &ec->flags);
			acpi_ec_leave_noirq(ec);
			if (test_bit(EC_FLAGS_STARTED, &ec->flags) &&
			    ec->reference_count >= 1)
				acpi_ec_enable_gpe(ec, true);

			/* EC is fully operational, allow queries */
			acpi_ec_enable_event(ec);
		}
	}

	return 0;
}

static void ec_remove_handlers(struct acpi_ec *ec)
{
	if (test_bit(EC_FLAGS_EC_HANDLER_INSTALLED, &ec->flags)) {
		if (ACPI_FAILURE(acpi_remove_address_space_handler(ec->handle,
					ACPI_ADR_SPACE_EC, &acpi_ec_space_handler)))
			pr_err("failed to remove space handler\n");
		clear_bit(EC_FLAGS_EC_HANDLER_INSTALLED, &ec->flags);
	}

	/*
	 * Stops handling the EC transactions after removing the operation
	 * region handler. This is required because _REG(DISCONNECT)
	 * invoked during the removal can result in new EC transactions.
	 *
	 * Flushes the EC requests and thus disables the GPE before
	 * removing the GPE handler. This is required by the current ACPICA
	 * GPE core. ACPICA GPE core will automatically disable a GPE when
	 * it is indicated but there is no way to handle it. So the drivers
	 * must disable the GPEs prior to removing the GPE handlers.
	 */
	acpi_ec_stop(ec, false);

	if (test_bit(EC_FLAGS_GPE_HANDLER_INSTALLED, &ec->flags)) {
		if (ACPI_FAILURE(acpi_remove_gpe_handler(NULL, ec->gpe,
					&acpi_ec_gpe_handler)))
			pr_err("failed to remove gpe handler\n");
		clear_bit(EC_FLAGS_GPE_HANDLER_INSTALLED, &ec->flags);
	}
	if (test_bit(EC_FLAGS_EVT_HANDLER_INSTALLED, &ec->flags)) {
		acpi_ec_remove_query_handlers(ec, true, 0);
		clear_bit(EC_FLAGS_EVT_HANDLER_INSTALLED, &ec->flags);
	}
}

static int acpi_ec_setup(struct acpi_ec *ec, bool handle_events)
{
	int ret;

	ret = ec_install_handlers(ec, handle_events);
	if (ret)
		return ret;

	/* First EC capable of handling transactions */
	if (!first_ec) {
		first_ec = ec;
		acpi_handle_info(first_ec->handle, "Used as first EC\n");
	}

	acpi_handle_info(ec->handle,
			 "GPE=0x%lx, EC_CMD/EC_SC=0x%lx, EC_DATA=0x%lx\n",
			 ec->gpe, ec->command_addr, ec->data_addr);
	return ret;
}

static int acpi_config_boot_ec(struct acpi_ec *ec, acpi_handle handle,
			       bool handle_events, bool is_ecdt)
{
	int ret;

	/*
	 * Changing the ACPI handle results in a re-configuration of the
	 * boot EC. And if it happens after the namespace initialization,
	 * it causes _REG evaluations.
	 */
	if (boot_ec && boot_ec->handle != handle)
		ec_remove_handlers(boot_ec);

	/* Unset old boot EC */
	if (boot_ec != ec)
		acpi_ec_free(boot_ec);

	/*
	 * ECDT device creation is split into acpi_ec_ecdt_probe() and
	 * acpi_ec_ecdt_start(). This function takes care of completing the
	 * ECDT parsing logic as the handle update should be performed
	 * between the installation/uninstallation of the handlers.
	 */
	if (ec->handle != handle)
		ec->handle = handle;

	ret = acpi_ec_setup(ec, handle_events);
	if (ret)
		return ret;

	/* Set new boot EC */
	if (!boot_ec) {
		boot_ec = ec;
		boot_ec_is_ecdt = is_ecdt;
	}

	acpi_handle_info(boot_ec->handle,
			 "Used as boot %s EC to handle transactions%s\n",
			 is_ecdt ? "ECDT" : "DSDT",
			 handle_events ? " and events" : "");
	return ret;
}

static bool acpi_ec_ecdt_get_handle(acpi_handle *phandle)
{
	struct acpi_table_ecdt *ecdt_ptr;
	acpi_status status;
	acpi_handle handle;

	status = acpi_get_table(ACPI_SIG_ECDT, 1,
				(struct acpi_table_header **)&ecdt_ptr);
	if (ACPI_FAILURE(status))
		return false;

	status = acpi_get_handle(NULL, ecdt_ptr->id, &handle);
	if (ACPI_FAILURE(status))
		return false;

	*phandle = handle;
	return true;
}

static bool acpi_is_boot_ec(struct acpi_ec *ec)
{
	if (!boot_ec)
		return false;
	if (ec->command_addr == boot_ec->command_addr &&
	    ec->data_addr == boot_ec->data_addr)
		return true;
	return false;
}

static int acpi_ec_add(struct acpi_device *device)
{
	struct acpi_ec *ec = NULL;
	int ret;

	strcpy(acpi_device_name(device), ACPI_EC_DEVICE_NAME);
	strcpy(acpi_device_class(device), ACPI_EC_CLASS);

	ec = acpi_ec_alloc();
	if (!ec)
		return -ENOMEM;
	if (ec_parse_device(device->handle, 0, ec, NULL) !=
		AE_CTRL_TERMINATE) {
			ret = -EINVAL;
			goto err_alloc;
	}

	if (acpi_is_boot_ec(ec)) {
		boot_ec_is_ecdt = false;
		/*
		 * Trust PNP0C09 namespace location rather than ECDT ID.
		 *
		 * But trust ECDT GPE rather than _GPE because of ASUS quirks,
		 * so do not change boot_ec->gpe to ec->gpe.
		 */
		boot_ec->handle = ec->handle;
		acpi_handle_debug(ec->handle, "duplicated.\n");
		acpi_ec_free(ec);
		ec = boot_ec;
		ret = acpi_config_boot_ec(ec, ec->handle, true, false);
	} else
		ret = acpi_ec_setup(ec, true);
	if (ret)
		goto err_query;

	device->driver_data = ec;

	ret = !!request_region(ec->data_addr, 1, "EC data");
	WARN(!ret, "Could not request EC data io port 0x%lx", ec->data_addr);
	ret = !!request_region(ec->command_addr, 1, "EC cmd");
	WARN(!ret, "Could not request EC cmd io port 0x%lx", ec->command_addr);

	/* Reprobe devices depending on the EC */
	acpi_walk_dep_device_list(ec->handle);
	acpi_handle_debug(ec->handle, "enumerated.\n");
	return 0;

err_query:
	if (ec != boot_ec)
		acpi_ec_remove_query_handlers(ec, true, 0);
err_alloc:
	if (ec != boot_ec)
		acpi_ec_free(ec);
	return ret;
}

static int acpi_ec_remove(struct acpi_device *device)
{
	struct acpi_ec *ec;

	if (!device)
		return -EINVAL;

	ec = acpi_driver_data(device);
	release_region(ec->data_addr, 1);
	release_region(ec->command_addr, 1);
	device->driver_data = NULL;
	if (ec != boot_ec) {
		ec_remove_handlers(ec);
		acpi_ec_free(ec);
	}
	return 0;
}

static acpi_status
ec_parse_io_ports(struct acpi_resource *resource, void *context)
{
	struct acpi_ec *ec = context;

	if (resource->type != ACPI_RESOURCE_TYPE_IO)
		return AE_OK;

	/*
	 * The first address region returned is the data port, and
	 * the second address region returned is the status/command
	 * port.
	 */
	if (ec->data_addr == 0)
		ec->data_addr = resource->data.io.minimum;
	else if (ec->command_addr == 0)
		ec->command_addr = resource->data.io.minimum;
	else
		return AE_CTRL_TERMINATE;

	return AE_OK;
}

static const struct acpi_device_id ec_device_ids[] = {
	{"PNP0C09", 0},
	{"", 0},
};

/*
 * This function is not Windows-compatible as Windows never enumerates the
 * namespace EC before the main ACPI device enumeration process. It is
 * retained for historical reason and will be deprecated in the future.
 */
int __init acpi_ec_dsdt_probe(void)
{
	acpi_status status;
	struct acpi_ec *ec;
	int ret;

	/*
	 * If a platform has ECDT, there is no need to proceed as the
	 * following probe is not a part of the ACPI device enumeration,
	 * executing _STA is not safe, and thus this probe may risk of
	 * picking up an invalid EC device.
	 */
	if (boot_ec)
		return -ENODEV;

	ec = acpi_ec_alloc();
	if (!ec)
		return -ENOMEM;
	/*
	 * At this point, the namespace is initialized, so start to find
	 * the namespace objects.
	 */
	status = acpi_get_devices(ec_device_ids[0].id,
				  ec_parse_device, ec, NULL);
	if (ACPI_FAILURE(status) || !ec->handle) {
		ret = -ENODEV;
		goto error;
	}
	/*
	 * When the DSDT EC is available, always re-configure boot EC to
	 * have _REG evaluated. _REG can only be evaluated after the
	 * namespace initialization.
	 * At this point, the GPE is not fully initialized, so do not to
	 * handle the events.
	 */
	ret = acpi_config_boot_ec(ec, ec->handle, false, false);
error:
	if (ret)
		acpi_ec_free(ec);
	return ret;
}

/*
 * If the DSDT EC is not functioning, we still need to prepare a fully
 * functioning ECDT EC first in order to handle the events.
 * https://bugzilla.kernel.org/show_bug.cgi?id=115021
 */
static int __init acpi_ec_ecdt_start(void)
{
	acpi_handle handle;

	if (!boot_ec)
		return -ENODEV;
	/* In case acpi_ec_ecdt_start() is called after acpi_ec_add() */
	if (!boot_ec_is_ecdt)
		return -ENODEV;

	/*
	 * At this point, the namespace and the GPE is initialized, so
	 * start to find the namespace objects and handle the events.
	 *
	 * Note: ec->handle can be valid if this function is called after
	 * acpi_ec_add(), hence the fast path.
	 */
	if (boot_ec->handle != ACPI_ROOT_OBJECT)
		handle = boot_ec->handle;
	else if (!acpi_ec_ecdt_get_handle(&handle))
		return -ENODEV;
	return acpi_config_boot_ec(boot_ec, handle, true, true);
}

#if 0
/*
 * Some EC firmware variations refuses to respond QR_EC when SCI_EVT is not
 * set, for which case, we complete the QR_EC without issuing it to the
 * firmware.
 * https://bugzilla.kernel.org/show_bug.cgi?id=82611
 * https://bugzilla.kernel.org/show_bug.cgi?id=97381
 */
static int ec_flag_query_handshake(const struct dmi_system_id *id)
{
	pr_debug("Detected the EC firmware requiring QR_EC issued when SCI_EVT set\n");
	EC_FLAGS_QUERY_HANDSHAKE = 1;
	return 0;
}
#endif

/*
 * Some ECDTs contain wrong register addresses.
 * MSI MS-171F
 * https://bugzilla.kernel.org/show_bug.cgi?id=12461
 */
static int ec_correct_ecdt(const struct dmi_system_id *id)
{
	pr_debug("Detected system needing ECDT address correction.\n");
	EC_FLAGS_CORRECT_ECDT = 1;
	return 0;
}

/*
 * Some DSDTs contain wrong GPE setting.
 * Asus FX502VD/VE, GL702VMK, X550VXK, X580VD
 * https://bugzilla.kernel.org/show_bug.cgi?id=195651
 */
static int ec_honor_ecdt_gpe(const struct dmi_system_id *id)
{
	pr_debug("Detected system needing ignore DSDT GPE setting.\n");
	EC_FLAGS_IGNORE_DSDT_GPE = 1;
	return 0;
}

static const struct dmi_system_id ec_dmi_table[] __initconst = {
	{
	ec_correct_ecdt, "MSI MS-171F", {
	DMI_MATCH(DMI_SYS_VENDOR, "Micro-Star"),
	DMI_MATCH(DMI_PRODUCT_NAME, "MS-171F"),}, NULL},
	{
	ec_honor_ecdt_gpe, "ASUS FX502VD", {
	DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
	DMI_MATCH(DMI_PRODUCT_NAME, "FX502VD"),}, NULL},
	{
	ec_honor_ecdt_gpe, "ASUS FX502VE", {
	DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
	DMI_MATCH(DMI_PRODUCT_NAME, "FX502VE"),}, NULL},
	{
	ec_honor_ecdt_gpe, "ASUS GL702VMK", {
	DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
	DMI_MATCH(DMI_PRODUCT_NAME, "GL702VMK"),}, NULL},
	{
	ec_honor_ecdt_gpe, "ASUS X550VXK", {
	DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
	DMI_MATCH(DMI_PRODUCT_NAME, "X550VXK"),}, NULL},
	{
	ec_honor_ecdt_gpe, "ASUS X580VD", {
	DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
	DMI_MATCH(DMI_PRODUCT_NAME, "X580VD"),}, NULL},
	{},
};

int __init acpi_ec_ecdt_probe(void)
{
	int ret;
	acpi_status status;
	struct acpi_table_ecdt *ecdt_ptr;
	struct acpi_ec *ec;

	ec = acpi_ec_alloc();
	if (!ec)
		return -ENOMEM;
	/*
	 * Generate a boot ec context
	 */
	dmi_check_system(ec_dmi_table);
	status = acpi_get_table(ACPI_SIG_ECDT, 1,
				(struct acpi_table_header **)&ecdt_ptr);
	if (ACPI_FAILURE(status)) {
		ret = -ENODEV;
		goto error;
	}

	if (!ecdt_ptr->control.address || !ecdt_ptr->data.address) {
		/*
		 * Asus X50GL:
		 * https://bugzilla.kernel.org/show_bug.cgi?id=11880
		 */
		ret = -ENODEV;
		goto error;
	}

	if (EC_FLAGS_CORRECT_ECDT) {
		ec->command_addr = ecdt_ptr->data.address;
		ec->data_addr = ecdt_ptr->control.address;
	} else {
		ec->command_addr = ecdt_ptr->control.address;
		ec->data_addr = ecdt_ptr->data.address;
	}
	ec->gpe = ecdt_ptr->gpe;

	/*
	 * At this point, the namespace is not initialized, so do not find
	 * the namespace objects, or handle the events.
	 */
	ret = acpi_config_boot_ec(ec, ACPI_ROOT_OBJECT, false, true);
error:
	if (ret)
		acpi_ec_free(ec);
	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int acpi_ec_suspend(struct device *dev)
{
	struct acpi_ec *ec =
		acpi_driver_data(to_acpi_device(dev));

	if (acpi_sleep_no_ec_events() && ec_freeze_events)
		acpi_ec_disable_event(ec);
	return 0;
}

static int acpi_ec_suspend_noirq(struct device *dev)
{
	struct acpi_ec *ec = acpi_driver_data(to_acpi_device(dev));

	/*
	 * The SCI handler doesn't run at this point, so the GPE can be
	 * masked at the low level without side effects.
	 */
	if (ec_no_wakeup && test_bit(EC_FLAGS_STARTED, &ec->flags) &&
	    ec->reference_count >= 1)
		acpi_set_gpe(NULL, ec->gpe, ACPI_GPE_DISABLE);

	return 0;
}

static int acpi_ec_resume_noirq(struct device *dev)
{
	struct acpi_ec *ec = acpi_driver_data(to_acpi_device(dev));

	if (ec_no_wakeup && test_bit(EC_FLAGS_STARTED, &ec->flags) &&
	    ec->reference_count >= 1)
		acpi_set_gpe(NULL, ec->gpe, ACPI_GPE_ENABLE);

	return 0;
}

static int acpi_ec_resume(struct device *dev)
{
	struct acpi_ec *ec =
		acpi_driver_data(to_acpi_device(dev));

	acpi_ec_enable_event(ec);
	return 0;
}
#endif

static const struct dev_pm_ops acpi_ec_pm = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(acpi_ec_suspend_noirq, acpi_ec_resume_noirq)
	SET_SYSTEM_SLEEP_PM_OPS(acpi_ec_suspend, acpi_ec_resume)
};

static int param_set_event_clearing(const char *val, struct kernel_param *kp)
{
	int result = 0;

	if (!strncmp(val, "status", sizeof("status") - 1)) {
		ec_event_clearing = ACPI_EC_EVT_TIMING_STATUS;
		pr_info("Assuming SCI_EVT clearing on EC_SC accesses\n");
	} else if (!strncmp(val, "query", sizeof("query") - 1)) {
		ec_event_clearing = ACPI_EC_EVT_TIMING_QUERY;
		pr_info("Assuming SCI_EVT clearing on QR_EC writes\n");
	} else if (!strncmp(val, "event", sizeof("event") - 1)) {
		ec_event_clearing = ACPI_EC_EVT_TIMING_EVENT;
		pr_info("Assuming SCI_EVT clearing on event reads\n");
	} else
		result = -EINVAL;
	return result;
}

static int param_get_event_clearing(char *buffer, struct kernel_param *kp)
{
	switch (ec_event_clearing) {
	case ACPI_EC_EVT_TIMING_STATUS:
		return sprintf(buffer, "status");
	case ACPI_EC_EVT_TIMING_QUERY:
		return sprintf(buffer, "query");
	case ACPI_EC_EVT_TIMING_EVENT:
		return sprintf(buffer, "event");
	default:
		return sprintf(buffer, "invalid");
	}
	return 0;
}

module_param_call(ec_event_clearing, param_set_event_clearing, param_get_event_clearing,
		  NULL, 0644);
MODULE_PARM_DESC(ec_event_clearing, "Assumed SCI_EVT clearing timing");

static struct acpi_driver acpi_ec_driver = {
	.name = "ec",
	.class = ACPI_EC_CLASS,
	.ids = ec_device_ids,
	.ops = {
		.add = acpi_ec_add,
		.remove = acpi_ec_remove,
		},
	.drv.pm = &acpi_ec_pm,
};

static inline int acpi_ec_query_init(void)
{
	if (!ec_query_wq) {
		ec_query_wq = alloc_workqueue("kec_query", 0,
					      ec_max_queries);
		if (!ec_query_wq)
			return -ENODEV;
	}
	return 0;
}

static inline void acpi_ec_query_exit(void)
{
	if (ec_query_wq) {
		destroy_workqueue(ec_query_wq);
		ec_query_wq = NULL;
	}
}

int __init acpi_ec_init(void)
{
	int result;
	int ecdt_fail, dsdt_fail;

	/* register workqueue for _Qxx evaluations */
	result = acpi_ec_query_init();
	if (result)
		return result;

	/* Drivers must be started after acpi_ec_query_init() */
	dsdt_fail = acpi_bus_register_driver(&acpi_ec_driver);
	ecdt_fail = acpi_ec_ecdt_start();
	return ecdt_fail && dsdt_fail ? -ENODEV : 0;
}

/* EC driver currently not unloadable */
#if 0
static void __exit acpi_ec_exit(void)
{

	acpi_bus_unregister_driver(&acpi_ec_driver);
	acpi_ec_query_exit();
}
#endif	/* 0 */
