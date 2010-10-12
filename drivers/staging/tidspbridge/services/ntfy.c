/*
 * ntfy.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Manage lists of notification events.
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/*  ----------------------------------- This */
#include <dspbridge/ntfy.h>

int dsp_notifier_event(struct notifier_block *this, unsigned long event,
			   void *data)
{
	struct  ntfy_event *ne = container_of(this, struct ntfy_event,
							noti_block);
	if (ne->event & event)
		sync_set_event(&ne->sync_obj);
	return NOTIFY_OK;
}

