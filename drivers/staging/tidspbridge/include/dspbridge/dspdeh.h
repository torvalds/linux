/*
 * dspdeh.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Defines upper edge DEH functions required by all Bridge driver/DSP API
 * interface tables.
 *
 * Notes:
 *   Function comment headers reside with the function typedefs in dspdefs.h.
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

#ifndef DSPDEH_
#define DSPDEH_

#include <dspbridge/devdefs.h>

#include <dspbridge/dehdefs.h>

extern int bridge_deh_create(struct deh_mgr **ret_deh_mgr,
		struct dev_object *hdev_obj);

extern int bridge_deh_destroy(struct deh_mgr *deh_mgr);

extern int bridge_deh_get_info(struct deh_mgr *deh_mgr,
		struct dsp_errorinfo *pErrInfo);

extern int bridge_deh_register_notify(struct deh_mgr *deh_mgr,
		u32 event_mask,
		u32 notify_type,
		struct dsp_notification *hnotification);

extern void bridge_deh_notify(struct deh_mgr *deh_mgr,
		u32 ulEventMask, u32 dwErrInfo);

#endif /* DSPDEH_ */
