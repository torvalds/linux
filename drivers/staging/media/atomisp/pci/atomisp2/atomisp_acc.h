/*
 * Support for Clovertrail PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2012 Intel Corporation. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 */

#ifndef __ATOMISP_ACC_H__
#define __ATOMISP_ACC_H__

#include "../../include/linux/atomisp.h"
#include "atomisp_internal.h"

#include "ia_css_types.h"

/*
 * Interface functions for AtomISP driver acceleration API implementation.
 */

struct atomisp_sub_device;

void atomisp_acc_cleanup(struct atomisp_device *isp);

/*
 * Free up any allocated resources.
 * Must be called each time when the device is closed.
 * Note that there isn't corresponding open() call;
 * this function may be called sequentially multiple times.
 * Must be called to free up resources before driver is unloaded.
 */
void atomisp_acc_release(struct atomisp_sub_device *asd);

/* Load acceleration binary. DEPRECATED. */
int atomisp_acc_load(struct atomisp_sub_device *asd,
		     struct atomisp_acc_fw_load *fw);

/* Load acceleration binary with specified properties */
int atomisp_acc_load_to_pipe(struct atomisp_sub_device *asd,
			     struct atomisp_acc_fw_load_to_pipe *fw);

/* Unload specified acceleration binary */
int atomisp_acc_unload(struct atomisp_sub_device *asd,
		       unsigned int *handle);

/*
 * Map a memory region into ISP memory space.
 */
int atomisp_acc_map(struct atomisp_sub_device *asd,
		    struct atomisp_acc_map *map);

/*
 * Unmap a mapped memory region.
 */
int atomisp_acc_unmap(struct atomisp_sub_device *asd,
		      struct atomisp_acc_map *map);

/*
 * Set acceleration binary argument to a previously mapped memory region.
 */
int atomisp_acc_s_mapped_arg(struct atomisp_sub_device *asd,
			     struct atomisp_acc_s_mapped_arg *arg);


/*
 * Start acceleration.
 * Return immediately, acceleration is left running in background.
 * Specify either acceleration binary or pipeline which to start.
 */
int atomisp_acc_start(struct atomisp_sub_device *asd,
		      unsigned int *handle);

/*
 * Wait until acceleration finishes.
 * This MUST be called after each acceleration has been started.
 * Specify either acceleration binary or pipeline handle.
 */
int atomisp_acc_wait(struct atomisp_sub_device *asd,
		     unsigned int *handle);

/*
 * Used by ISR to notify ACC stage finished.
 * This is internally used and does not export as IOCTL.
 */
void atomisp_acc_done(struct atomisp_sub_device *asd, unsigned int handle);

/*
 * Appends the loaded acceleration binary extensions to the
 * current ISP mode. Must be called just before atomisp_css_start().
 */
int atomisp_acc_load_extensions(struct atomisp_sub_device *asd);

/*
 * Must be called after streaming is stopped:
 * unloads any loaded acceleration extensions.
 */
void atomisp_acc_unload_extensions(struct atomisp_sub_device *asd);

/*
 * Set acceleration firmware flags.
 */
int atomisp_acc_set_state(struct atomisp_sub_device *asd,
			  struct atomisp_acc_state *arg);

/*
 * Get acceleration firmware flags.
 */
int atomisp_acc_get_state(struct atomisp_sub_device *asd,
			  struct atomisp_acc_state *arg);

#endif /* __ATOMISP_ACC_H__ */
