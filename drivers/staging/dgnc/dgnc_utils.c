// SPDX-License-Identifier: GPL-2.0
#include <linux/tty.h>
#include <linux/sched/signal.h>
#include "dgnc_utils.h"

/**
 * dgnc_ms_sleep - Put the driver to sleep
 * @ms - milliseconds to sleep
 *
 * Return: 0 if timed out, if interrupted by a signal return signal.
 */
int dgnc_ms_sleep(ulong ms)
{
	__set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout((ms * HZ) / 1000);
	return signal_pending(current);
}
