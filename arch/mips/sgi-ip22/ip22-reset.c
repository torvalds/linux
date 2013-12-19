/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1997, 1998, 2001, 03, 05, 06 by Ralf Baechle
 */
#include <linux/linkage.h>
#include <linux/init.h>
#include <linux/ds1286.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/notifier.h>
#include <linux/pm.h>
#include <linux/timer.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/reboot.h>
#include <asm/sgialib.h>
#include <asm/sgi/ioc.h>
#include <asm/sgi/hpc3.h>
#include <asm/sgi/mc.h>
#include <asm/sgi/ip22.h>

/*
 * Just powerdown if init hasn't done after POWERDOWN_TIMEOUT seconds.
 * I'm not sure if this feature is a good idea, for now it's here just to
 * make the power button make behave just like under IRIX.
 */
#define POWERDOWN_TIMEOUT	120

/*
 * Blink frequency during reboot grace period and when panicked.
 */
#define POWERDOWN_FREQ		(HZ / 4)
#define PANIC_FREQ		(HZ / 8)

static struct timer_list power_timer, blink_timer, debounce_timer;

#define MACHINE_PANICED		1
#define MACHINE_SHUTTING_DOWN	2

static int machine_state;

static void __noreturn sgi_machine_power_off(void)
{
	unsigned int tmp;

	local_irq_disable();

	/* Disable watchdog */
	tmp = hpc3c0->rtcregs[RTC_CMD] & 0xff;
	hpc3c0->rtcregs[RTC_CMD] = tmp | RTC_WAM;
	hpc3c0->rtcregs[RTC_WSEC] = 0;
	hpc3c0->rtcregs[RTC_WHSEC] = 0;

	while (1) {
		sgioc->panel = ~SGIOC_PANEL_POWERON;
		/* Good bye cruel world ...  */

		/* If we're still running, we probably got sent an alarm
		   interrupt.  Read the flag to clear it.  */
		tmp = hpc3c0->rtcregs[RTC_HOURS_ALARM];
	}
}

static void __noreturn sgi_machine_restart(char *command)
{
	if (machine_state & MACHINE_SHUTTING_DOWN)
		sgi_machine_power_off();
	sgimc->cpuctrl0 |= SGIMC_CCTRL0_SYSINIT;
	while (1);
}

static void __noreturn sgi_machine_halt(void)
{
	if (machine_state & MACHINE_SHUTTING_DOWN)
		sgi_machine_power_off();
	ArcEnterInteractiveMode();
}

static void power_timeout(unsigned long data)
{
	sgi_machine_power_off();
}

static void blink_timeout(unsigned long data)
{
	/* XXX fix this for fullhouse  */
	sgi_ioc_reset ^= (SGIOC_RESET_LC0OFF|SGIOC_RESET_LC1OFF);
	sgioc->reset = sgi_ioc_reset;

	mod_timer(&blink_timer, jiffies + data);
}

static void debounce(unsigned long data)
{
	del_timer(&debounce_timer);
	if (sgint->istat1 & SGINT_ISTAT1_PWR) {
		/* Interrupt still being sent. */
		debounce_timer.expires = jiffies + (HZ / 20); /* 0.05s	*/
		add_timer(&debounce_timer);

		sgioc->panel = SGIOC_PANEL_POWERON | SGIOC_PANEL_POWERINTR |
			       SGIOC_PANEL_VOLDNINTR | SGIOC_PANEL_VOLDNHOLD |
			       SGIOC_PANEL_VOLUPINTR | SGIOC_PANEL_VOLUPHOLD;

		return;
	}

	if (machine_state & MACHINE_PANICED)
		sgimc->cpuctrl0 |= SGIMC_CCTRL0_SYSINIT;

	enable_irq(SGI_PANEL_IRQ);
}

static inline void power_button(void)
{
	if (machine_state & MACHINE_PANICED)
		return;

	if ((machine_state & MACHINE_SHUTTING_DOWN) ||
			kill_cad_pid(SIGINT, 1)) {
		/* No init process or button pressed twice.  */
		sgi_machine_power_off();
	}

	machine_state |= MACHINE_SHUTTING_DOWN;
	blink_timer.data = POWERDOWN_FREQ;
	blink_timeout(POWERDOWN_FREQ);

	init_timer(&power_timer);
	power_timer.function = power_timeout;
	power_timer.expires = jiffies + POWERDOWN_TIMEOUT * HZ;
	add_timer(&power_timer);
}

static irqreturn_t panel_int(int irq, void *dev_id)
{
	unsigned int buttons;

	buttons = sgioc->panel;
	sgioc->panel = SGIOC_PANEL_POWERON | SGIOC_PANEL_POWERINTR;

	if (sgint->istat1 & SGINT_ISTAT1_PWR) {
		/* Wait until interrupt goes away */
		disable_irq_nosync(SGI_PANEL_IRQ);
		init_timer(&debounce_timer);
		debounce_timer.function = debounce;
		debounce_timer.expires = jiffies + 5;
		add_timer(&debounce_timer);
	}

	/* Power button was pressed
	 * ioc.ps page 22: "The Panel Register is called Power Control by Full
	 * House. Only lowest 2 bits are used. Guiness uses upper four bits
	 * for volume control". This is not true, all bits are pulled high
	 * on fullhouse */
	if (!(buttons & SGIOC_PANEL_POWERINTR))
		power_button();

	return IRQ_HANDLED;
}

static int panic_event(struct notifier_block *this, unsigned long event,
		      void *ptr)
{
	if (machine_state & MACHINE_PANICED)
		return NOTIFY_DONE;
	machine_state |= MACHINE_PANICED;

	blink_timer.data = PANIC_FREQ;
	blink_timeout(PANIC_FREQ);

	return NOTIFY_DONE;
}

static struct notifier_block panic_block = {
	.notifier_call	= panic_event,
};

static int __init reboot_setup(void)
{
	int res;

	_machine_restart = sgi_machine_restart;
	_machine_halt = sgi_machine_halt;
	pm_power_off = sgi_machine_power_off;

	res = request_irq(SGI_PANEL_IRQ, panel_int, 0, "Front Panel", NULL);
	if (res) {
		printk(KERN_ERR "Allocation of front panel IRQ failed\n");
		return res;
	}

	init_timer(&blink_timer);
	blink_timer.function = blink_timeout;
	atomic_notifier_chain_register(&panic_notifier_list, &panic_block);

	return 0;
}

subsys_initcall(reboot_setup);
