/*
 * lttng-calibrate.c
 *
 * Copyright 2011 (c) - Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * LTTng probe calibration.
 *
 * Dual LGPL v2.1/GPL v2 license.
 */

#include "ltt-debugfs-abi.h"
#include "ltt-events.h"

noinline
void lttng_calibrate_kretprobe(void)
{
	asm volatile ("");
}

int lttng_calibrate(struct lttng_kernel_calibrate *calibrate)
{
	switch (calibrate->type) {
	case LTTNG_KERNEL_CALIBRATE_KRETPROBE:
		lttng_calibrate_kretprobe();
		break;
	default:
		return -EINVAL;
	}
	return 0;
}
