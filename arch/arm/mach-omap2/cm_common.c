/*
 * OMAP2+ common Clock Management (CM) IP block functions
 *
 * Copyright (C) 2012 Texas Instruments, Inc.
 * Paul Walmsley <paul@pwsan.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * XXX This code should eventually be moved to a CM driver.
 */

#include <linux/kernel.h>
#include <linux/init.h>

#include "cm2xxx.h"
#include "cm3xxx.h"
#include "cm44xx.h"

/*
 * cm_ll_data: function pointers to SoC-specific implementations of
 * common CM functions
 */
static struct cm_ll_data null_cm_ll_data;
static struct cm_ll_data *cm_ll_data = &null_cm_ll_data;

/**
 * cm_register - register per-SoC low-level data with the CM
 * @cld: low-level per-SoC OMAP CM data & function pointers to register
 *
 * Register per-SoC low-level OMAP CM data and function pointers with
 * the OMAP CM common interface.  The caller must keep the data
 * pointed to by @cld valid until it calls cm_unregister() and
 * it returns successfully.  Returns 0 upon success, -EINVAL if @cld
 * is NULL, or -EEXIST if cm_register() has already been called
 * without an intervening cm_unregister().
 */
int cm_register(struct cm_ll_data *cld)
{
	if (!cld)
		return -EINVAL;

	if (cm_ll_data != &null_cm_ll_data)
		return -EEXIST;

	cm_ll_data = cld;

	return 0;
}

/**
 * cm_unregister - unregister per-SoC low-level data & function pointers
 * @cld: low-level per-SoC OMAP CM data & function pointers to unregister
 *
 * Unregister per-SoC low-level OMAP CM data and function pointers
 * that were previously registered with cm_register().  The
 * caller may not destroy any of the data pointed to by @cld until
 * this function returns successfully.  Returns 0 upon success, or
 * -EINVAL if @cld is NULL or if @cld does not match the struct
 * cm_ll_data * previously registered by cm_register().
 */
int cm_unregister(struct cm_ll_data *cld)
{
	if (!cld || cm_ll_data != cld)
		return -EINVAL;

	cm_ll_data = &null_cm_ll_data;

	return 0;
}
