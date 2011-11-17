/*
 * Debug support for HID Nintendo Wiimote devices
 * Copyright (c) 2011 David Herrmann
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/module.h>
#include <linux/spinlock.h>
#include "hid-wiimote.h"

struct wiimote_debug {
	struct wiimote_data *wdata;
};

int wiidebug_init(struct wiimote_data *wdata)
{
	struct wiimote_debug *dbg;
	unsigned long flags;

	dbg = kzalloc(sizeof(*dbg), GFP_KERNEL);
	if (!dbg)
		return -ENOMEM;

	dbg->wdata = wdata;

	spin_lock_irqsave(&wdata->state.lock, flags);
	wdata->debug = dbg;
	spin_unlock_irqrestore(&wdata->state.lock, flags);

	return 0;
}

void wiidebug_deinit(struct wiimote_data *wdata)
{
	struct wiimote_debug *dbg = wdata->debug;
	unsigned long flags;

	if (!dbg)
		return;

	spin_lock_irqsave(&wdata->state.lock, flags);
	wdata->debug = NULL;
	spin_unlock_irqrestore(&wdata->state.lock, flags);

	kfree(dbg);
}
