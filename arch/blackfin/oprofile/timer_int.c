/*
 * File:         arch/blackfin/oprofile/timer_int.c
 * Based on:
 * Author:       Michael Kang
 *
 * Created:
 * Description:
 *
 * Modified:
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
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
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/init.h>
#include <linux/smp.h>
#include <linux/irq.h>
#include <linux/oprofile.h>
#include <linux/ptrace.h>

static void enable_sys_timer0()
{
}
static void disable_sys_timer0()
{
}

static irqreturn_t sys_timer0_int_handler(int irq, void *dev_id,
					  struct pt_regs *regs)
{
	oprofile_add_sample(regs, 0);
	return IRQ_HANDLED;
}

static int sys_timer0_start(void)
{
	enable_sys_timer0();
	return request_irq(IVG11, sys_timer0_int_handler, 0, "sys_timer0", NULL);
}

static void sys_timer0_stop(void)
{
	disable_sys_timer();
}

int __init sys_timer0_init(struct oprofile_operations *ops)
{
	extern int nmi_active;

	if (nmi_active <= 0)
		return -ENODEV;

	ops->start = timer_start;
	ops->stop = timer_stop;
	ops->cpu_type = "timer";
	printk(KERN_INFO "oprofile: using NMI timer interrupt.\n");
	return 0;
}
