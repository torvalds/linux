/*
 * Copyright 2003 Digi International (www.digi.com)
 *	Scott H Kilau <Scott_Kilau at digi dot com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 */

#ifndef __DGNC_SYSFS_H
#define __DGNC_SYSFS_H

#include <linux/device.h>
#include "dgnc_driver.h"

struct dgnc_board;
struct channel_t;
struct un_t;
struct pci_driver;
struct class_device;

void dgnc_create_ports_sysfiles(struct dgnc_board *bd);
void dgnc_remove_ports_sysfiles(struct dgnc_board *bd);

void dgnc_create_driver_sysfiles(struct pci_driver *);
void dgnc_remove_driver_sysfiles(struct pci_driver *);

int dgnc_tty_class_init(void);
int dgnc_tty_class_destroy(void);

void dgnc_create_tty_sysfs(struct un_t *un, struct device *c);
void dgnc_remove_tty_sysfs(struct device *c);

#endif
