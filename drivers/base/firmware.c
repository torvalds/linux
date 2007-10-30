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

static struct kset *firmware_kset;

int firmware_register(struct kset *s)
{
	s->kobj.kset = firmware_kset;
	s->kobj.ktype = NULL;
	return subsystem_register(s);
}

void firmware_unregister(struct kset *s)
{
	subsystem_unregister(s);
}

int __init firmware_init(void)
{
	firmware_kset = kset_create_and_add("firmware", NULL, NULL);
	if (!firmware_kset)
		return -ENOMEM;
	return 0;
}

EXPORT_SYMBOL_GPL(firmware_register);
EXPORT_SYMBOL_GPL(firmware_unregister);
