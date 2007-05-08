/*
 * firmware.c - firmware subsystem hoohaw.
 *
 * Copyright (c) 2002-3 Patrick Mochel
 * Copyright (c) 2002-3 Open Source Development Labs
 *
 * This file is released under the GPLv2
 *
 */

#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>

#include "base.h"

static decl_subsys(firmware, NULL, NULL);

int firmware_register(struct kset *s)
{
	kobj_set_kset_s(s, firmware_subsys);
	return subsystem_register(s);
}

void firmware_unregister(struct kset *s)
{
	subsystem_unregister(s);
}

int __init firmware_init(void)
{
	return subsystem_register(&firmware_subsys);
}

EXPORT_SYMBOL_GPL(firmware_register);
EXPORT_SYMBOL_GPL(firmware_unregister);
