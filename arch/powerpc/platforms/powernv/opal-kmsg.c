/*
 * kmsg dumper that ensures the OPAL console fully flushes panic messages
 *
 * Author: Russell Currey <ruscur@russell.cc>
 *
 * Copyright 2015 IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
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
static void force_opal_console_flush(struct kmsg_dumper *dumper,
				     enum kmsg_dump_reason reason)
{
	int i;
	int64_t ret;

	/*
	 * Outside of a panic context the pollers will continue to run,
	 * so we don't need to do any special flushing.
	 */
	if (reason != KMSG_DUMP_PANIC)
		return;

	if (opal_check_token(OPAL_CONSOLE_FLUSH)) {
		ret = opal_console_flush(0);

		if (ret == OPAL_UNSUPPORTED || ret == OPAL_PARAMETER)
			return;

		/* Incrementally flush until there's nothing left */
		while (opal_console_flush(0) != OPAL_SUCCESS);
	} else {
		/*
		 * If OPAL_CONSOLE_FLUSH is not implemented in the firmware,
		 * the console can still be flushed by calling the polling
		 * function enough times to flush the buffer.  We don't know
		 * how much output still needs to be flushed, but we can be
		 * generous since the kernel is in panic and doesn't need
		 * to do much else.
		 */
		printk(KERN_NOTICE "opal: OPAL_CONSOLE_FLUSH missing.\n");
		for (i = 0; i < 1024; i++) {
			opal_poll_events(NULL);
		}
	}
}

static struct kmsg_dumper opal_kmsg_dumper = {
	.dump = force_opal_console_flush
};

void __init opal_kmsg_init(void)
{
	int rc;

	/* Add our dumper to the list */
	rc = kmsg_dump_register(&opal_kmsg_dumper);
	if (rc != 0)
		pr_err("opal: kmsg_dump_register failed; returned %d\n", rc);
}
