/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 Keith M Wesolowski
 * Copyright (C) 2001 Paul Mundt
 * Copyright (C) 2003 Guido Guenther <agx@sigxcpu.org>
 */

#include <linux/compiler.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/panic_notifier.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/notifier.h>
#include <linux/delay.h>
#include <linux/rtc/ds1685.h>
#include <linux/interrupt.h>
#include <linux/pm.h>

#include <asm/addrspace.h>
#include <asm/irq.h>
#include <asm/reboot.h>
#include <asm/wbflush.h>
#include <asm/ip32/mace.h>
#include <asm/ip32/crime.h>
#include <asm/ip32/ip32_ints.h>

#define POWERDOWN_TIMEOUT	120
/*
 * Blink frequency during reboot grace period and when panicked.
 */
#define POWERDOWN_FREQ		(HZ / 4)
#define PANIC_FREQ		(HZ / 8)

extern struct platform_device ip32_rtc_device;

static struct timer_list power_timer, blink_timer;
static unsigned long blink_timer_timeout;
static int has_panicked, shutting_down;

static __noreturn void ip32_poweroff(void *data)
{
	void (*poweroff_func)(struct platform_device *) =
		symbol_get(ds1685_rtc_poweroff);

#ifdef CONFIG_MODULES
	/* If the first __symbol_get failed, our module wasn't loaded. */
	if (!poweroff_func) {
		request_module("rtc-ds1685");
		poweroff_func = symbol_get(ds1685_rtc_poweroff);
	}
#endif

	if (!poweroff_func)
		pr_emerg("RTC not available for power-off.  Spinning forever ...\n");
	else {
		(*poweroff_func)((struct platform_device *)data);
		symbol_put(ds1685_rtc_poweroff);
	}

	unreachable();
}

static void ip32_machine_restart(char *cmd) __noreturn;
static void ip32_machine_restart(char *cmd)
{
	msleep(20);
	crime->control = CRIME_CONTROL_HARD_RESET;
	unreachable();
}

static void blink_timeout(struct timer_list *unused)
{
	unsigned long led = mace->perif.ctrl.misc ^ MACEISA_LED_RED;
	mace->perif.ctrl.misc = led;
	mod_timer(&blink_timer, jiffies + blink_timer_timeout);
}

static void ip32_machine_halt(void)
{
	ip32_poweroff(&ip32_rtc_device);
}

static void power_timeout(struct timer_list *unused)
{
	ip32_poweroff(&ip32_rtc_device);
}

void ip32_prepare_poweroff(void)
{
	if (has_panicked)
		return;

	if (shutting_down || kill_cad_pid(SIGINT, 1)) {
		/* No init process or button pressed twice.  */
		ip32_poweroff(&ip32_rtc_device);
	}

	shutting_down = 1;
	blink_timer_timeout = POWERDOWN_FREQ;
	blink_timeout(&blink_timer);

	timer_setup(&power_timer, power_timeout, 0);
	power_timer.expires = jiffies + POWERDOWN_TIMEOUT * HZ;
	add_timer(&power_timer);
}

static int panic_event(struct notifier_block *this, unsigned long event,
		       void *ptr)
{
	unsigned long led;

	if (has_panicked)
		return NOTIFY_DONE;
	has_panicked = 1;

	/* turn off the green LED */
	led = mace->perif.ctrl.misc | MACEISA_LED_GREEN;
	mace->perif.ctrl.misc = led;

	blink_timer_timeout = PANIC_FREQ;
	blink_timeout(&blink_timer);

	return NOTIFY_DONE;
}

static struct notifier_block panic_block = {
	.notifier_call = panic_event,
};

static __init int ip32_reboot_setup(void)
{
	/* turn on the green led only */
	unsigned long led = mace->perif.ctrl.misc;
	led |= MACEISA_LED_RED;
	led &= ~MACEISA_LED_GREEN;
	mace->perif.ctrl.misc = led;

	_machine_restart = ip32_machine_restart;
	_machine_halt = ip32_machine_halt;
	pm_power_off = ip32_machine_halt;

	timer_setup(&blink_timer, blink_timeout, 0);
	atomic_notifier_chain_register(&panic_notifier_list, &panic_block);

	return 0;
}

subsys_initcall(ip32_reboot_setup);
