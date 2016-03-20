/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001, 06 by Ralf Baechle (ralf@linux-mips.org)
 * Copyright (C) 2001 MIPS Technologies, Inc.
 */
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/pm.h>
#include <linux/types.h>
#include <linux/reboot.h>
#include <linux/delay.h>

#include <asm/reboot.h>

/*
 * Urgs ...  Too many MIPS machines to handle this in a generic way.
 * So handle all using function pointers to machine specific
 * functions.
 */
void (*_machine_restart)(char *command);
void (*_machine_halt)(void);
void (*pm_power_off)(void);

EXPORT_SYMBOL(pm_power_off);

void machine_restart(char *command)
{
	if (_machine_restart)
		_machine_restart(command);

#ifdef CONFIG_SMP
	preempt_disable();
	smp_send_stop();
#endif
	do_kernel_restart(command);
	mdelay(1000);
	pr_emerg("Reboot failed -- System halted\n");
	local_irq_disable();
	while (1);
}

void machine_halt(void)
{
	if (_machine_halt)
		_machine_halt();

#ifdef CONFIG_SMP
	preempt_disable();
	smp_send_stop();
#endif
	local_irq_disable();
	while (1);
}

void machine_power_off(void)
{
	if (pm_power_off)
		pm_power_off();

#ifdef CONFIG_SMP
	preempt_disable();
	smp_send_stop();
#endif
	local_irq_disable();
	while (1);
}
