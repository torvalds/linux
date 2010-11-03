/*
 * _deh.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Private header for DEH module.
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

#ifndef _DEH_
#define _DEH_

#include <dspbridge/ntfy.h>
#include <dspbridge/dspdefs.h>

/* DEH Manager: only one created per board: */
struct deh_mgr {
	struct bridge_dev_context *hbridge_context;	/* Bridge context. */
	struct ntfy_object *ntfy_obj;	/* NTFY object */
};

int mmu_fault_isr(struct iommu *mmu);

#endif /* _DEH_ */
