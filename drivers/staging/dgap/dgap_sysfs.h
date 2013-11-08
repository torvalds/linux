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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *	NOTE: THIS IS A SHARED HEADER. DO NOT CHANGE CODING STYLE!!!
 */

#ifndef __DGAP_SYSFS_H
#define __DGAP_SYSFS_H

#include "dgap_driver.h"

#include <linux/device.h>

struct board_t;
struct channel_t;
struct un_t;
struct pci_driver;
struct class_device;

extern void dgap_create_ports_sysfiles(struct board_t *bd); 
extern void dgap_remove_ports_sysfiles(struct board_t *bd);

extern void dgap_create_driver_sysfiles(struct pci_driver *);
extern void dgap_remove_driver_sysfiles(struct pci_driver *);

extern int dgap_tty_class_init(void);
extern int dgap_tty_class_destroy(void);

extern void dgap_create_tty_sysfs(struct un_t *un, struct device *c);
extern void dgap_remove_tty_sysfs(struct device *c);


#endif
