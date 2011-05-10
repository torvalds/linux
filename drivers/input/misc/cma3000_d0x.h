/*
 * VTI CMA3000_D0x Accelerometer driver
 *
 * Copyright (C) 2010 Texas Instruments
 * Author: Hemanth V <hemanthv@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _INPUT_CMA3000_H
#define _INPUT_CMA3000_H

#include <linux/types.h>
#include <linux/input.h>

struct device;
struct cma3000_accl_data;

struct cma3000_bus_ops {
	u16 bustype;
	u8 ctrl_mod;
	int (*read)(struct device *, u8, char *);
	int (*write)(struct device *, u8, u8, char *);
};

struct cma3000_accl_data *cma3000_init(struct device *dev, int irq,
					const struct cma3000_bus_ops *bops);
void cma3000_exit(struct cma3000_accl_data *);
void cma3000_suspend(struct cma3000_accl_data *);
void cma3000_resume(struct cma3000_accl_data *);

#endif
