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

#include <asm/compiler.h>
#include <asm/idle.h>
#include <asm/mipsregs.h>
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

static void machine_hang(void)
{
	/*
	 * We're hanging the system so we don't want to be interrupted anymore.
	 * Any interrupt handlers that ran would at best be useless & at worst
	 * go awry because the system isn't in a functional state.
	 */
	local_irq_disable();

	/*
	 * Mask all interrupts, giving us a better chance of remaining in the
	 * low power wait state.
	 */
	clear_c0_status(ST0_IM);

	while (true) {
		if (cpu_has_mips_r) {
			/*
			 * We know that the wait instruction is supported so
			 * make use of it directly, leaving interrupts
			 * disabled.
			 */
			asm volatile(
				".set	push\n\t"
				".set	" MIPS_ISA_ARCH_LEVEL "\n\t"
				"wait\n\t"
				".set	pop");
		} else if (cpu_wait) {
			/*
			 * Try the cpu_wait() callback. This isn't ideal since
			 * it'll re-enable interrupts, but that ought to be
			 * harmless given that they're all masked.
			 */
			cpu_wait();
			local_irq_disable();
		} else {
			/*
			 * We're going to burn some power running round the
			 * loop, but we don't really have a choice. This isn't
			 * a path we should expect to run for long during
			 * typical use anyway.
			 */
		}

		/*
		 * In most modern MIPS CPUs interrupts will cause the wait
		 * instruction to graduate even when disabled, and in some
		 * cases even when masked. In order to prevent a timer
		 * interrupt from continuously taking us out of the low power
		 * wait state, we clear any pending timer interrupt here.
		 */
		if (cpu_has_counter)
			write_c0_compare(0);
	}
}

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
	machine_hang();
}

void machine_halt(void)
{
	if (_machine_halt)
		_machine_halt();

#ifdef CONFIG_SMP
	preempt_disable();
	smp_send_stop();
#endif
	machine_hang();
}

void machine_power_off(void)
{
	do_kernel_power_off();

#ifdef CONFIG_SMP
	preempt_disable();
	smp_send_stop();
#endif
	machine_hang();
}
