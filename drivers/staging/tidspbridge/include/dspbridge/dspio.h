/*
 * dspio.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Declares the upper edge IO functions required by all Bridge driver /DSP API
 * interface tables.
 *
 * Notes:
 *   Function comment headers reside in dspdefs.h.
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

#ifndef DSPIO_
#define DSPIO_

#include <dspbridge/devdefs.h>
#include <dspbridge/iodefs.h>

extern int bridge_io_create(struct io_mgr **io_man,
				   struct dev_object *hdev_obj,
				   const struct io_attrs *mgr_attrts);

extern int bridge_io_destroy(struct io_mgr *hio_mgr);

extern int bridge_io_on_loaded(struct io_mgr *hio_mgr);

extern int iva_io_on_loaded(struct io_mgr *hio_mgr);
extern int bridge_io_get_proc_load(struct io_mgr *hio_mgr,
				       struct dsp_procloadstat *proc_lstat);

#endif /* DSPIO_ */
