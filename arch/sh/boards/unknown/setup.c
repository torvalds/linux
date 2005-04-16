/*
 * linux/arch/sh/boards/unknown/setup.c
 *
 * Copyright (C) 2002 Paul Mundt
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * Setup code for an unknown machine (internal peripherials only)
 */

#include <linux/config.h>
#include <linux/init.h>

const char *get_system_type(void)
{
	return "Unknown";
}

void __init platform_setup(void)
{
}

