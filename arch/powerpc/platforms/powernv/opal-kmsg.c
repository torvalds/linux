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
#include <linux/delay.h>

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
	s64 rc;

	/*
	 * Outside of a panic context the pollers will continue to run,
	 * so we don't need to do any special flushing.
	 */
	if (reason != KMSG_DUMP_PANIC)
		return;

	if (opal_check_token(OPAL_CONSOLE_FLUSH)) {
		do  {
			rc = OPAL_BUSY;
			while (rc == OPAL_BUSY || rc == OPAL_BUSY_EVENT) {
				rc = opal_console_flush(0);
				if (rc == OPAL_BUSY_EVENT) {
					mdelay(OPAL_BUSY_DELAY_MS);
					opal_poll_events(NULL);
				} else if (rc == OPAL_BUSY) {
					mdelay(OPAL_BUSY_DELAY_MS);
				}
			}
		} while (rc == OPAL_PARTIAL); /* More to flush */

	} else {
		__be64 evt;

		WARN_ONCE(1, "opal: OPAL_CONSOLE_FLUSH missing.\n");
		/*
		 * If OPAL_CONSOLE_FLUSH is not implemented in the firmware,
		 * the console can still be flushed by calling the polling
		 * function while it has OPAL_EVENT_CONSOLE_OUTPUT events.
		 */
		do {
			opal_poll_events(&evt);
		} while (be64_to_cpu(evt) & OPAL_EVENT_CONSOLE_OUTPUT);
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
