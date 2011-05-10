/*
 * ioobj.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Structure subcomponents of channel class library IO objects which
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

#ifndef IOOBJ_
#define IOOBJ_

#include <dspbridge/devdefs.h>
#include <dspbridge/dspdefs.h>

/*
 *  This struct is the first field in a io_mgr struct. Other, implementation
 *  specific fields follow this structure in memory.
 */
struct io_mgr_ {
	/* These must be the first fields in a io_mgr struct: */
	struct bridge_dev_context *bridge_context;	/* Bridge context. */
	/* Function interface to Bridge driver. */
	struct bridge_drv_interface *intf_fxns;
	struct dev_object *dev_obj;	/* Device this board represents. */
};

#endif /* IOOBJ_ */
