/*
 * arch/arm/plat-meson/include/plat/fiq_bridge.h
 *
 * Copyright (C) 2010-2014 Amlogic, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __PLAT_MESON_FIQ_BRIDGE_H
#define __PLAT_MESON_FIQ_BRIDGE_H

#include <linux/list.h>
#include <linux/irqreturn.h>
#include <linux/io.h>
#include <mach/am_regs.h>

typedef irqreturn_t (*bridge_handle_t)(int irq, void *dev_id);

typedef struct {
	bridge_handle_t		handle;
	u32			key;
	u32			active;
	const char*		name;
	struct list_head	list;
} bridge_item_t;

static LIST_HEAD(fiq_bridge_list);

#define BRIDGE_IRQ INT_TIMER_C
#define BRIDGE_IRQ_SET() WRITE_CBUS_REG(ISA_TIMERC, 1)

extern int fiq_bridge_pulse_trigger(bridge_item_t *c_item);
extern int register_fiq_bridge_handle(bridge_item_t *c_item);
extern int unregister_fiq_bridge_handle(bridge_item_t *c_item);

#endif //__PLAT_MESON_FIQ_BRIDGE_H
