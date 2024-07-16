/*
 * Copyright (C) 2021 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the GPL.
 */

#ifndef DM_IO_TRACKER_H
#define DM_IO_TRACKER_H

#include <linux/jiffies.h>

struct dm_io_tracker {
	spinlock_t lock;

	/*
	 * Sectors of in-flight IO.
	 */
	sector_t in_flight;

	/*
	 * The time, in jiffies, when this device became idle
	 * (if it is indeed idle).
	 */
	unsigned long idle_time;
	unsigned long last_update_time;
};

static inline void dm_iot_init(struct dm_io_tracker *iot)
{
	spin_lock_init(&iot->lock);
	iot->in_flight = 0ul;
	iot->idle_time = 0ul;
	iot->last_update_time = jiffies;
}

static inline bool dm_iot_idle_for(struct dm_io_tracker *iot, unsigned long j)
{
	bool r = false;

	spin_lock_irq(&iot->lock);
	if (!iot->in_flight)
		r = time_after(jiffies, iot->idle_time + j);
	spin_unlock_irq(&iot->lock);

	return r;
}

static inline unsigned long dm_iot_idle_time(struct dm_io_tracker *iot)
{
	unsigned long r = 0;

	spin_lock_irq(&iot->lock);
	if (!iot->in_flight)
		r = jiffies - iot->idle_time;
	spin_unlock_irq(&iot->lock);

	return r;
}

static inline void dm_iot_io_begin(struct dm_io_tracker *iot, sector_t len)
{
	spin_lock_irq(&iot->lock);
	iot->in_flight += len;
	spin_unlock_irq(&iot->lock);
}

static inline void dm_iot_io_end(struct dm_io_tracker *iot, sector_t len)
{
	unsigned long flags;

	if (!len)
		return;

	spin_lock_irqsave(&iot->lock, flags);
	iot->in_flight -= len;
	if (!iot->in_flight)
		iot->idle_time = jiffies;
	spin_unlock_irqrestore(&iot->lock, flags);
}

#endif
