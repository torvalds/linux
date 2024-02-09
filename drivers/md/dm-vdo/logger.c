// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "logger.h"

#include <linux/delay.h>
#include <linux/hardirq.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/sched.h>

#include "errors.h"
#include "thread-device.h"
#include "thread-utils.h"

struct priority_name {
	const char *name;
	const int priority;
};

static const struct priority_name PRIORITIES[] = {
	{ "ALERT", UDS_LOG_ALERT },
	{ "CRITICAL", UDS_LOG_CRIT },
	{ "CRIT", UDS_LOG_CRIT },
	{ "DEBUG", UDS_LOG_DEBUG },
	{ "EMERGENCY", UDS_LOG_EMERG },
	{ "EMERG", UDS_LOG_EMERG },
	{ "ERROR", UDS_LOG_ERR },
	{ "ERR", UDS_LOG_ERR },
	{ "INFO", UDS_LOG_INFO },
	{ "NOTICE", UDS_LOG_NOTICE },
	{ "PANIC", UDS_LOG_EMERG },
	{ "WARN", UDS_LOG_WARNING },
	{ "WARNING", UDS_LOG_WARNING },
	{ NULL, -1 },
};

static const char *const PRIORITY_STRINGS[] = {
	"EMERGENCY",
	"ALERT",
	"CRITICAL",
	"ERROR",
	"WARN",
	"NOTICE",
	"INFO",
	"DEBUG",
};

static int log_level = UDS_LOG_INFO;

int uds_get_log_level(void)
{
	return log_level;
}

void uds_set_log_level(int new_log_level)
{
	log_level = new_log_level;
}

int uds_log_string_to_priority(const char *string)
{
	int i;

	for (i = 0; PRIORITIES[i].name != NULL; i++) {
		if (strcasecmp(string, PRIORITIES[i].name) == 0)
			return PRIORITIES[i].priority;
	}

	return UDS_LOG_INFO;
}

const char *uds_log_priority_to_string(int priority)
{
	if ((priority < 0) || (priority >= (int) ARRAY_SIZE(PRIORITY_STRINGS)))
		return "unknown";

	return PRIORITY_STRINGS[priority];
}

static const char *get_current_interrupt_type(void)
{
	if (in_nmi())
		return "NMI";

	if (in_irq())
		return "HI";

	if (in_softirq())
		return "SI";

	return "INTR";
}

/**
 * emit_log_message_to_kernel() - Emit a log message to the kernel at the specified priority.
 *
 * @priority: The priority at which to log the message
 * @fmt: The format string of the message
 */
static void emit_log_message_to_kernel(int priority, const char *fmt, ...)
{
	va_list args;
	struct va_format vaf;

	if (priority > uds_get_log_level())
		return;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;

	switch (priority) {
	case UDS_LOG_EMERG:
	case UDS_LOG_ALERT:
	case UDS_LOG_CRIT:
		pr_crit("%pV", &vaf);
		break;
	case UDS_LOG_ERR:
		pr_err("%pV", &vaf);
		break;
	case UDS_LOG_WARNING:
		pr_warn("%pV", &vaf);
		break;
	case UDS_LOG_NOTICE:
	case UDS_LOG_INFO:
		pr_info("%pV", &vaf);
		break;
	case UDS_LOG_DEBUG:
		pr_debug("%pV", &vaf);
		break;
	default:
		printk(KERN_DEFAULT "%pV", &vaf);
		break;
	}

	va_end(args);
}

/**
 * emit_log_message() - Emit a log message to the kernel log in a format suited to the current
 *                      thread context.
 *
 * Context info formats:
 *
 * interrupt:           uds[NMI]: blah
 * kvdo thread:         kvdo12:foobarQ: blah
 * thread w/device id:  kvdo12:myprog: blah
 * other thread:        uds: myprog: blah
 *
 * Fields: module name, interrupt level, process name, device ID.
 *
 * @priority: the priority at which to log the message
 * @module: The name of the module doing the logging
 * @prefix: The prefix of the log message
 * @vaf1: The first message format descriptor
 * @vaf2: The second message format descriptor
 */
static void emit_log_message(int priority, const char *module, const char *prefix,
			     const struct va_format *vaf1, const struct va_format *vaf2)
{
	int device_instance;

	/*
	 * In interrupt context, identify the interrupt type and module. Ignore the process/thread
	 * since it could be anything.
	 */
	if (in_interrupt()) {
		const char *type = get_current_interrupt_type();

		emit_log_message_to_kernel(priority, "%s[%s]: %s%pV%pV\n", module, type,
					   prefix, vaf1, vaf2);
		return;
	}

	/* Not at interrupt level; we have a process we can look at, and might have a device ID. */
	device_instance = uds_get_thread_device_id();
	if (device_instance >= 0) {
		emit_log_message_to_kernel(priority, "%s%u:%s: %s%pV%pV\n", module,
					   device_instance, current->comm, prefix, vaf1,
					   vaf2);
		return;
	}

	/*
	 * If it's a kernel thread and the module name is a prefix of its name, assume it is ours
	 * and only identify the thread.
	 */
	if (((current->flags & PF_KTHREAD) != 0) &&
	    (strncmp(module, current->comm, strlen(module)) == 0)) {
		emit_log_message_to_kernel(priority, "%s: %s%pV%pV\n", current->comm,
					   prefix, vaf1, vaf2);
		return;
	}

	/* Identify the module and the process. */
	emit_log_message_to_kernel(priority, "%s: %s: %s%pV%pV\n", module, current->comm,
				   prefix, vaf1, vaf2);
}

/*
 * uds_log_embedded_message() - Log a message embedded within another message.
 * @priority: the priority at which to log the message
 * @module: the name of the module doing the logging
 * @prefix: optional string prefix to message, may be NULL
 * @fmt1: format of message first part (required)
 * @args1: arguments for message first part (required)
 * @fmt2: format of message second part
 */
void uds_log_embedded_message(int priority, const char *module, const char *prefix,
			      const char *fmt1, va_list args1, const char *fmt2, ...)
{
	va_list args1_copy;
	va_list args2;
	struct va_format vaf1, vaf2;

	va_start(args2, fmt2);

	if (module == NULL)
		module = UDS_LOGGING_MODULE_NAME;

	if (prefix == NULL)
		prefix = "";

	/*
	 * It is implementation dependent whether va_list is defined as an array type that decays
	 * to a pointer when passed as an argument. Copy args1 and args2 with va_copy so that vaf1
	 * and vaf2 get proper va_list pointers irrespective of how va_list is defined.
	 */
	va_copy(args1_copy, args1);
	vaf1.fmt = fmt1;
	vaf1.va = &args1_copy;

	vaf2.fmt = fmt2;
	vaf2.va = &args2;

	emit_log_message(priority, module, prefix, &vaf1, &vaf2);

	va_end(args1_copy);
	va_end(args2);
}

int uds_vlog_strerror(int priority, int errnum, const char *module, const char *format,
		      va_list args)
{
	char errbuf[UDS_MAX_ERROR_MESSAGE_SIZE];
	const char *message = uds_string_error(errnum, errbuf, sizeof(errbuf));

	uds_log_embedded_message(priority, module, NULL, format, args, ": %s (%d)",
				 message, errnum);
	return errnum;
}

int __uds_log_strerror(int priority, int errnum, const char *module, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	uds_vlog_strerror(priority, errnum, module, format, args);
	va_end(args);
	return errnum;
}

void uds_log_backtrace(int priority)
{
	if (priority > uds_get_log_level())
		return;

	dump_stack();
}

void __uds_log_message(int priority, const char *module, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	uds_log_embedded_message(priority, module, NULL, format, args, "%s", "");
	va_end(args);
}

/*
 * Sleep or delay a few milliseconds in an attempt to allow the log buffers to be flushed lest they
 * be overrun.
 */
void uds_pause_for_logger(void)
{
	fsleep(4000);
}
