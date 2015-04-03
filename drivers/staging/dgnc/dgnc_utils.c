#include <linux/tty.h>
#include <linux/sched.h>
#include "dgnc_utils.h"
#include "digi.h"

/*
 * dgnc_ms_sleep()
 *
 * Put the driver to sleep for x ms's
 *
 * Returns 0 if timed out, !0 (showing signal) if interrupted by a signal.
 */
int dgnc_ms_sleep(ulong ms)
{
	__set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout((ms * HZ) / 1000);
	return signal_pending(current);
}
