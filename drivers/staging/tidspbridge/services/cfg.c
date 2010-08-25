/*
 * cfg.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Implementation of platform specific config services.
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

#include <linux/types.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/dbdefs.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>

/*  ----------------------------------- OS Adaptation Layer */

/*  ----------------------------------- This */
#include <dspbridge/cfg.h>
#include <dspbridge/drv.h>

/*
 *  ======== cfg_set_object ========
 *  Purpose:
 *      Store the Driver Object handle
 */
int cfg_set_object(u32 value, u8 dw_type)
{
	int status = -EINVAL;
	struct drv_data *drv_datap = dev_get_drvdata(bridge);

	if (!drv_datap)
		return -EPERM;

	switch (dw_type) {
	case (REG_DRV_OBJECT):
		drv_datap->drv_object = (void *)value;
		status = 0;
		break;
	case (REG_MGR_OBJECT):
		drv_datap->mgr_object = (void *)value;
		status = 0;
		break;
	default:
		break;
	}
	if (status)
		pr_err("%s: Failed, status 0x%x\n", __func__, status);
	return status;
}
