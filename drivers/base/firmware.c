/*
 * firmware.c - firmware subsystem hoohaw.
 *
 * Copyright (c) 2002-3 Patrick Mochel
 * Copyright (c) 2002-3 Open Source Development Labs
 * Copyright (c) 2007 Greg Kroah-Hartman <gregkh@suse.de>
 * Copyright (c) 2007 Novell Inc.
 *
 * This file is released under the GPLv2
 */
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>

#include "base.h"

struct kset *firmware_kset;
EXPORT_SYMBOL_GPL(firmware_kset);

int __init firmware_init(void)
{
	firmware_kset = kset_create_and_add("firmware", NULL, NULL);
	if (!firmware_kset)
		return -ENOMEM;
	return 0;
}
