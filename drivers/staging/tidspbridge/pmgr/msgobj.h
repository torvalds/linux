/*
 * msgobj.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Structure subcomponents of channel class library msg_ctrl objects which
 * are exposed to DSP API from Bridge driver.
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

#ifndef MSGOBJ_
#define MSGOBJ_

#include <dspbridge/dspdefs.h>

#include <dspbridge/msgdefs.h>

/*
 *  This struct is the first field in a msg_mgr struct. Other, implementation
 *  specific fields follow this structure in memory.
 */
struct msg_mgr_ {
	/* The first field must match that in _msg_sm.h */

	/* Function interface to Bridge driver. */
	struct bridge_drv_interface *intf_fxns;
};

#endif /* MSGOBJ_ */
