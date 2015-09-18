/*
 *	w1_family.h
 *
 * Copyright (c) 2004 Evgeniy Polyakov <zbr@ioremap.net>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef __W1_FAMILY_H
#define __W1_FAMILY_H

#include <linux/types.h>
#include <linux/device.h>
#include <linux/atomic.h>

#define W1_FAMILY_DEFAULT	0
#define W1_FAMILY_BQ27000	0x01
#define W1_FAMILY_SMEM_01	0x01
#define W1_FAMILY_SMEM_81	0x81
#define W1_THERM_DS18S20 	0x10
#define W1_FAMILY_DS28E04	0x1C
#define W1_COUNTER_DS2423	0x1D
#define W1_THERM_DS1822  	0x22
#define W1_EEPROM_DS2433  	0x23
#define W1_THERM_DS18B20 	0x28
#define W1_FAMILY_DS2408	0x29
#define W1_EEPROM_DS2431	0x2D
#define W1_FAMILY_DS2760	0x30
#define W1_FAMILY_DS2780	0x32
#define W1_FAMILY_DS2413	0x3A
#define W1_FAMILY_DS2406	0x12
#define W1_THERM_DS1825		0x3B
#define W1_FAMILY_DS2781	0x3D
#define W1_THERM_DS28EA00	0x42

#define MAXNAMELEN		32

struct w1_slave;

/**
 * struct w1_family_ops - operations for a family type
 * @add_slave: add_slave
 * @remove_slave: remove_slave
 * @groups: sysfs group
 */
struct w1_family_ops
{
	int  (* add_slave)(struct w1_slave *);
	void (* remove_slave)(struct w1_slave *);
	const struct attribute_group **groups;
};

/**
 * struct w1_family - reference counted family structure.
 * @family_entry:	family linked list
 * @fid:		8 bit family identifier
 * @fops:		operations for this family
 * @refcnt:		reference counter
 */
struct w1_family
{
	struct list_head	family_entry;
	u8			fid;

	struct w1_family_ops	*fops;

	atomic_t		refcnt;
};

extern spinlock_t w1_flock;

void w1_family_put(struct w1_family *);
void __w1_family_get(struct w1_family *);
struct w1_family * w1_family_registered(u8);
void w1_unregister_family(struct w1_family *);
int w1_register_family(struct w1_family *);

#endif /* __W1_FAMILY_H */
