/*
 * Copyright (c) 2014  Hauke Mehrtens <hauke@hauke-m.de>
 *
 * Backport functionality introduced in Linux 3.17.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/export.h>
#include <linux/ktime.h>

int bit_wait(void *word)
{
	schedule();
	return 0;
}
EXPORT_SYMBOL_GPL(bit_wait);

int bit_wait_io(void *word)
{
	io_schedule();
	return 0;
}
EXPORT_SYMBOL_GPL(bit_wait_io);

/**
 * ktime_get_raw - Returns the raw monotonic time in ktime_t format
 */
ktime_t ktime_get_raw(void)
{
	struct timespec ts;

	getrawmonotonic(&ts);
	return timespec_to_ktime(ts);
}
EXPORT_SYMBOL_GPL(ktime_get_raw);
