// SPDX-License-Identifier: GPL-2.0-only
/*
 *  PS3 System Manager core.
 *
 *  Copyright (C) 2007 Sony Computer Entertainment Inc.
 *  Copyright 2007 Sony Corp.
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <asm/lv1call.h>
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
	ps3_sys_manager_ops = *ops;
}
EXPORT_SYMBOL_GPL(ps3_sys_manager_register_ops);

void __noreturn ps3_sys_manager_power_off(void)
{
	if (ps3_sys_manager_ops.power_off)
		ps3_sys_manager_ops.power_off(ps3_sys_manager_ops.dev);

	ps3_sys_manager_halt();
}

void __noreturn ps3_sys_manager_restart(void)
{
	if (ps3_sys_manager_ops.restart)
		ps3_sys_manager_ops.restart(ps3_sys_manager_ops.dev);

	ps3_sys_manager_halt();
}

void __noreturn ps3_sys_manager_halt(void)
{
	pr_emerg("System Halted, OK to turn off power\n");
	local_irq_disable();
	while (1)
		lv1_pause(1);
}

