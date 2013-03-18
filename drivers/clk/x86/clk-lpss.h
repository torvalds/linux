/*
 * Intel Low Power Subsystem clock.
 *
 * Copyright (C) 2013, Intel Corporation
 * Authors: Mika Westerberg <mika.westerberg@linux.intel.com>
 *	    Heikki Krogerus <heikki.krogerus@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __CLK_LPSS_H
#define __CLK_LPSS_H

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/clk.h>

#ifdef CONFIG_ACPI
extern struct clk *clk_register_lpss_gate(const char *name,
					  const char *parent_name,
					  const char *hid, const char *uid,
					  unsigned offset);
#else
static inline struct clk *clk_register_lpss_gate(const char *name,
						 const char *parent_name,
						 const char *hid,
						 const char *uid,
						 unsigned offset)
{
	return ERR_PTR(-ENODEV);
}
#endif

#endif /* __CLK_LPSS_H */
