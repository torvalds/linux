// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * kmsg dumper that ensures the OPAL console fully flushes panic messages
 *
 * Author: Russell Currey <ruscur@russell.cc>
 *
 * Copyright 2015 IBM Corporation.
 */

#include <linux/kmsg_dump.h>

#include <asm/opal.h>
#include <asm/opal-api.h>

/*
 * Console output is controlled by OPAL firmware.  The kernel regularly calls
 * OPAL_POLL_EVENTS, which flushes some console output.  In a panic state,
 * however, the kernel no longer calls OPAL_POLL_EVENTS and the panic message
 * may not be completely printed.  This function does not actually dump the
 * message, it just ensures that OPAL completely flushes the console buffer.
 */
static void kmsg_dump_opal_console_flush(struct kmsg_dumper *dumper,
				     struct kmsg_dump_detail *detail)
{
	/*
	 * Outside of a panic context the pollers will continue to run,
	 * so we don't need to do any special flushing.
	 */
	if (detail->reason != KMSG_DUMP_PANIC)
		return;

	opal_flush_console(0);
}

static struct kmsg_dumper opal_kmsg_dumper = {
	.dump = kmsg_dump_opal_console_flush
};

void __init opal_kmsg_init(void)
{
	int rc;

	/* Add our dumper to the list */
	rc = kmsg_dump_register(&opal_kmsg_dumper);
	if (rc != 0)
		pr_err("opal: kmsg_dump_register failed; returned %d\n", rc);
}
