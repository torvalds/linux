// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "logger.h"

#include <asm/current.h>
#include <linux/delay.h>
#include <linux/hardirq.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/sched.h>

#include "errors.h"
#include "thread-device.h"
#include "thread-utils.h"

int vdo_log_level = VDO_LOG_DEFAULT;

int vdo_get_log_level(void)
{
	int log_level_latch = READ_ONCE(vdo_log_level);

	if (unlikely(log_level_latch > VDO_LOG_MAX)) {
		log_level_latch = VDO_LOG_DEFAULT;
		WRITE_ONCE(vdo_log_level, log_level_latch);
	}
	return log_level_latch;
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

	if (priority > vdo_get_log_level())
		return;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;

	switch (priority) {
	case VDO_LOG_EMERG:
	case VDO_LOG_ALERT:
	case VDO_LOG_CRIT:
		pr_crit("%pV", &vaf);
		break;
	case VDO_LOG_ERR:
		pr_err("%pV", &vaf);
		break;
	case VDO_LOG_WARNING:
		pr_warn("%pV", &vaf);
		break;
	case VDO_LOG_NOTICE:
	case VDO_LOG_INFO:
		pr_info("%pV", &vaf);
		break;
	case VDO_LOG_DEBUG:
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
	device_instance = vdo_get_thread_device_id();
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
 * vdo_log_embedded_message() - Log a message embedded within another message.
 * @priority: the priority at which to log the message
 * @module: the name of the module doing the logging
 * @prefix: optional string prefix to message, may be NULL
 * @fmt1: format of message first part (required)
 * @args1: arguments for message first part (required)
 * @fmt2: format of message second part
 */
void vdo_log_embedded_message(int priority, const char *module, const char *prefix,
			      const char *fmt1, va_list args1, const char *fmt2, ...)
{
	va_list args1_copy;
	va_list args2;
	struct va_format vaf1, vaf2;

	va_start(args2, fmt2);

	if (module == NULL)
		module = VDO_LOGGING_MODULE_NAME;

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

int vdo_vlog_strerror(int priority, int errnum, const char *module, const char *format,
		      va_list args)
{
	char errbuf[VDO_MAX_ERROR_MESSAGE_SIZE];
	const char *message = uds_string_error(errnum, errbuf, sizeof(errbuf));

	vdo_log_embedded_message(priority, module, NULL, format, args, ": %s (%d)",
				 message, errnum);
	return errnum;
}

int __vdo_log_strerror(int priority, int errnum, const char *module, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	vdo_vlog_strerror(priority, errnum, module, format, args);
	va_end(args);
	return errnum;
}

void vdo_log_backtrace(int priority)
{
	if (priority > vdo_get_log_level())
		return;

	dump_stack();
}

void __vdo_log_message(int priority, const char *module, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	vdo_log_embedded_message(priority, module, NULL, format, args, "%s", "");
	va_end(args);
}

/*
 * Sleep or delay a few milliseconds in an attempt to allow the log buffers to be flushed lest they
 * be overrun.
 */
void vdo_pause_for_logger(void)
{
	fsleep(4000);
}
