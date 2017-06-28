/*
 * Copyright (c) 2004 Evgeniy Polyakov <zbr@ioremap.net>
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
 */

#ifndef __W1_INT_H
#define __W1_INT_H

#include <linux/kernel.h>
#include <linux/device.h>

#include "w1.h"

int w1_add_master_device(struct w1_bus_master *);
void w1_remove_master_device(struct w1_bus_master *);
void __w1_remove_master_device(struct w1_master *);

#endif /* __W1_INT_H */
