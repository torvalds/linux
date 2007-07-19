/*
 *  PS3 System Manager core.
 *
 *  Copyright (C) 2007 Sony Computer Entertainment Inc.
 *  Copyright 2007 Sony Corp.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <asm/ps3.h>

/**
 * Staticly linked routines that allow late binding of a loaded sys-manager
 * module.
 */

static struct ps3_sys_manager_ops ps3_sys_manager_ops;

/**
 * ps3_register_sys_manager_ops - Bind ps3_sys_manager_ops to a module.
 * @ops: struct ps3_sys_manager_ops.
 *
 * To be called from ps3_sys_manager_probe() and ps3_sys_manager_remove() to
 * register call back ops for power control.  Copies data to the static
 * variable ps3_sys_manager_ops.
 */

void ps3_sys_manager_register_ops(const struct ps3_sys_manager_ops *ops)
{
	BUG_ON(!ops);
	BUG_ON(!ops->dev);
	ps3_sys_manager_ops = ops ? *ops : ps3_sys_manager_ops;
}
EXPORT_SYMBOL_GPL(ps3_sys_manager_register_ops);

void ps3_sys_manager_power_off(void)
{
	if (ps3_sys_manager_ops.power_off)
		ps3_sys_manager_ops.power_off(ps3_sys_manager_ops.dev);

	printk(KERN_EMERG "System Halted, OK to turn off power\n");
	local_irq_disable();
	while (1)
		(void)0;
}

void ps3_sys_manager_restart(void)
{
	if (ps3_sys_manager_ops.restart)
		ps3_sys_manager_ops.restart(ps3_sys_manager_ops.dev);

	printk(KERN_EMERG "System Halted, OK to turn off power\n");
	local_irq_disable();
	while (1)
		(void)0;
}
