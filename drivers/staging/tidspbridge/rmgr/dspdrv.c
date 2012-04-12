/*
 * dspdrv.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Interface to allocate and free bridge resources.
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

/*  ----------------------------------- Host OS */
#include <linux/types.h>
#include <dspbridge/host_os.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/dbdefs.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/drv.h>
#include <dspbridge/dev.h>
#include <dspbridge/dspapi.h>

/*  ----------------------------------- Resource Manager */
#include <dspbridge/mgr.h>

/*  ----------------------------------- This */
#include <dspbridge/dspdrv.h>

/*
 *  ======== dsp_init ========
 *  	Allocates bridge resources. Loads a base image onto DSP, if specified.
 */
u32 dsp_init(u32 *init_status)
{
	char dev_node[MAXREGPATHLENGTH] = "TIOMAP1510";
	int status = -EPERM;
	struct drv_object *drv_obj = NULL;
	u32 device_node;
	u32 device_node_string;

	if (!api_init())
		goto func_cont;

	status = drv_create(&drv_obj);
	if (status) {
		api_exit();
		goto func_cont;
	}

	/* End drv_create */
	/* Request Resources */
	status = drv_request_resources((u32) &dev_node, &device_node_string);
	if (!status) {
		/* Attempt to Start the Device */
		status = dev_start_device((struct cfg_devnode *)
					  device_node_string);
		if (status)
			(void)drv_release_resources
			    ((u32) device_node_string, drv_obj);
	} else {
		dev_dbg(bridge, "%s: drv_request_resources Failed\n", __func__);
		status = -EPERM;
	}

	/* Unwind whatever was loaded */
	if (status) {
		/* irrespective of the status of dev_remove_device we conitinue
		 * unloading. Get the Driver Object iterate through and remove.
		 * Reset the status to E_FAIL to avoid going through
		 * api_init_complete2. */
		for (device_node = drv_get_first_dev_extension();
		     device_node != 0;
		     device_node = drv_get_next_dev_extension(device_node)) {
			(void)dev_remove_device((struct cfg_devnode *)
						device_node);
			(void)drv_release_resources((u32) device_node, drv_obj);
		}
		/* Remove the Driver Object */
		(void)drv_destroy(drv_obj);
		drv_obj = NULL;
		api_exit();
		dev_dbg(bridge, "%s: Logical device failed init\n", __func__);
	}			/* Unwinding the loaded drivers */
func_cont:
	/* Attempt to Start the Board */
	if (!status) {
		/* BRD_AutoStart could fail if the dsp execuetable is not the
		 * correct one. We should not propagate that error
		 * into the device loader. */
		(void)api_init_complete2();
	} else {
		dev_dbg(bridge, "%s: Failed\n", __func__);
	}			/* End api_init_complete2 */
	*init_status = status;
	/* Return the Driver Object */
	return (u32) drv_obj;
}

/*
 *  ======== dsp_deinit ========
 *  	Frees the resources allocated for bridge.
 */
bool dsp_deinit(u32 device_context)
{
	bool ret = true;
	u32 device_node;
	struct mgr_object *mgr_obj = NULL;
	struct drv_data *drv_datap = dev_get_drvdata(bridge);

	while ((device_node = drv_get_first_dev_extension()) != 0) {
		(void)dev_remove_device((struct cfg_devnode *)device_node);

		(void)drv_release_resources((u32) device_node,
					(struct drv_object *)device_context);
	}

	(void)drv_destroy((struct drv_object *)device_context);

	/* Get the Manager Object from driver data
	 * MGR Destroy will unload the DCD dll */
	if (drv_datap && drv_datap->mgr_object) {
		mgr_obj = drv_datap->mgr_object;
		(void)mgr_destroy(mgr_obj);
	} else {
		pr_err("%s: Failed to retrieve the object handle\n", __func__);
	}

	api_exit();

	return ret;
}
