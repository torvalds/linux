/*
 * ue_deh.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Implements upper edge DSP exception handling (DEH) functions.
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 * Copyright (C) 2010 Felipe Contreras
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <plat/dmtimer.h>

#include <dspbridge/dbdefs.h>
#include <dspbridge/dspdeh.h>
#include <dspbridge/dev.h>
#include "_tiomap.h"
#include "_deh.h"

#include <dspbridge/io_sm.h>
#include <dspbridge/drv.h>
#include <dspbridge/wdt.h>

int bridge_deh_create(struct deh_mgr **ret_deh,
		struct dev_object *hdev_obj)
{
	int status;
	struct deh_mgr *deh;
	struct bridge_dev_context *hbridge_context = NULL;

	/*  Message manager will be created when a file is loaded, since
	 *  size of message buffer in shared memory is configurable in
	 *  the base image. */
	/* Get Bridge context info. */
	dev_get_bridge_context(hdev_obj, &hbridge_context);
	/* Allocate IO manager object: */
	deh = kzalloc(sizeof(*deh), GFP_KERNEL);
	if (!deh) {
		status = -ENOMEM;
		goto err;
	}

	/* Create an NTFY object to manage notifications */
	deh->ntfy_obj = kmalloc(sizeof(struct ntfy_object), GFP_KERNEL);
	if (!deh->ntfy_obj) {
		status = -ENOMEM;
		goto err;
	}
	ntfy_init(deh->ntfy_obj);

	/* Fill in context structure */
	deh->hbridge_context = hbridge_context;

	*ret_deh = deh;
	return 0;

err:
	bridge_deh_destroy(deh);
	*ret_deh = NULL;
	return status;
}

int bridge_deh_destroy(struct deh_mgr *deh)
{
	if (!deh)
		return -EFAULT;

	/* If notification object exists, delete it */
	if (deh->ntfy_obj) {
		ntfy_delete(deh->ntfy_obj);
		kfree(deh->ntfy_obj);
	}

	/* Deallocate the DEH manager object */
	kfree(deh);

	return 0;
}

int bridge_deh_register_notify(struct deh_mgr *deh, u32 event_mask,
		u32 notify_type,
		struct dsp_notification *hnotification)
{
	if (!deh)
		return -EFAULT;

	if (event_mask)
		return ntfy_register(deh->ntfy_obj, hnotification,
				event_mask, notify_type);
	else
		return ntfy_unregister(deh->ntfy_obj, hnotification);
}

static inline const char *event_to_string(int event)
{
	switch (event) {
	case DSP_SYSERROR: return "DSP_SYSERROR"; break;
	case DSP_MMUFAULT: return "DSP_MMUFAULT"; break;
	case DSP_PWRERROR: return "DSP_PWRERROR"; break;
	case DSP_WDTOVERFLOW: return "DSP_WDTOVERFLOW"; break;
	default: return "unkown event"; break;
	}
}

void bridge_deh_notify(struct deh_mgr *deh, int event, int info)
{
	struct bridge_dev_context *dev_context;
	const char *str = event_to_string(event);

	if (!deh)
		return;

	dev_dbg(bridge, "%s: device exception", __func__);
	dev_context = deh->hbridge_context;

	switch (event) {
	case DSP_SYSERROR:
		dev_err(bridge, "%s: %s, info=0x%x", __func__,
				str, info);
#ifdef CONFIG_TIDSPBRIDGE_BACKTRACE
		dump_dl_modules(dev_context);
		dump_dsp_stack(dev_context);
#endif
		break;
	case DSP_MMUFAULT:
		dev_err(bridge, "%s: %s, addr=0x%x", __func__, str, info);
		break;
	default:
		dev_err(bridge, "%s: %s", __func__, str);
		break;
	}

	/* Filter subsequent notifications when an error occurs */
	if (dev_context->dw_brd_state != BRD_ERROR) {
		ntfy_notify(deh->ntfy_obj, event);
#ifdef CONFIG_TIDSPBRIDGE_RECOVERY
		bridge_recover_schedule();
#endif
	}

	/* Set the Board state as ERROR */
	dev_context->dw_brd_state = BRD_ERROR;
	/* Disable all the clocks that were enabled by DSP */
	dsp_clock_disable_all(dev_context->dsp_per_clks);
	/*
	 * Avoid the subsequent WDT if it happens once,
	 * also if fatal error occurs.
	 */
	dsp_wdt_enable(false);
}
