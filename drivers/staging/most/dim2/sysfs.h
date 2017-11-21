// SPDX-License-Identifier: GPL-2.0
/*
 * sysfs.h - MediaLB sysfs information
 *
 * Copyright (C) 2015, Microchip Technology Germany II GmbH & Co. KG
 */

/* Author: Andrey Shvetsov <andrey.shvetsov@k2l.de> */

#ifndef DIM2_SYSFS_H
#define	DIM2_SYSFS_H

#include <linux/kobject.h>

struct medialb_bus {
	struct kobject kobj_group;
};

struct dim2_hdm;

int dim2_sysfs_probe(struct medialb_bus *bus, struct kobject *parent_kobj);
void dim2_sysfs_destroy(struct medialb_bus *bus);

/*
 * callback,
 * must deliver MediaLB state as true if locked or false if unlocked
 */
bool dim2_sysfs_get_state_cb(void);

#endif	/* DIM2_SYSFS_H */
