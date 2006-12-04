/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 Keith M Wesolowski
 * Copyright (C) 2001 Paul Mundt
 * Copyright (C) 2003 Guido Guenther <agx@sigxcpu.org>
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/notifier.h>
#include <linux/delay.h>
#include <linux/ds17287rtc.h>
#include <linux/interrupt.h>
#include <linux/pm.h>

#include <asm/addrspace.h>
#include <asm/irq.h>
#include <asm/reboot.h>
#include <asm/system.h>
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

static struct timer_list power_timer, blink_timer, debounce_timer;
static int has_panicked, shuting_down;

static void ip32_machine_restart(char *command) __attribute__((noreturn));
static void ip32_machine_halt(void) __attribute__((noreturn));
static void ip32_machine_power_off(void) __attribute__((noreturn));

static void ip32_machine_restart(char *cmd)
{
	crime->control = CRIME_CONTROL_HARD_RESET;
	while (1);
}

static inline void ip32_machine_halt(void)
{
	ip32_machine_power_off();
}

static void ip32_machine_power_off(void)
{
	volatile unsigned char reg_a, xctrl_a, xctrl_b;

	disable_irq(MACEISA_RTC_IRQ);
	reg_a = CMOS_READ(RTC_REG_A);

	/* setup for kickstart & wake-up (DS12287 Ref. Man. p. 19) */
	reg_a &= ~DS_REGA_DV2;
	reg_a |= DS_REGA_DV1;

	CMOS_WRITE(reg_a | DS_REGA_DV0, RTC_REG_A);
	wbflush();
	xctrl_b = CMOS_READ(DS_B1_XCTRL4B)
		   | DS_XCTRL4B_ABE | DS_XCTRL4B_KFE;
	CMOS_WRITE(xctrl_b, DS_B1_XCTRL4B);
	xctrl_a = CMOS_READ(DS_B1_XCTRL4A) & ~DS_XCTRL4A_IFS;
	CMOS_WRITE(xctrl_a, DS_B1_XCTRL4A);
	wbflush();
	/* adios amigos... */
	CMOS_WRITE(xctrl_a | DS_XCTRL4A_PAB, DS_B1_XCTRL4A);
	CMOS_WRITE(reg_a, RTC_REG_A);
	wbflush();
	while (1);
}

static void power_timeout(unsigned long data)
{
	ip32_machine_power_off();
}

static void blink_timeout(unsigned long data)
{
	unsigned long led = mace->perif.ctrl.misc ^ MACEISA_LED_RED;
	mace->perif.ctrl.misc = led;
	mod_timer(&blink_timer, jiffies + data);
}

static void debounce(unsigned long data)
{
	volatile unsigned char reg_a, reg_c, xctrl_a;

	reg_c = CMOS_READ(RTC_INTR_FLAGS);
	CMOS_WRITE(reg_a | DS_REGA_DV0, RTC_REG_A);
	wbflush();
	xctrl_a = CMOS_READ(DS_B1_XCTRL4A);
	if ((xctrl_a & DS_XCTRL4A_IFS) || (reg_c & RTC_IRQF )) {
		/* Interrupt still being sent. */
		debounce_timer.expires = jiffies + 50;
		add_timer(&debounce_timer);

		/* clear interrupt source */
		CMOS_WRITE(xctrl_a & ~DS_XCTRL4A_IFS, DS_B1_XCTRL4A);
		CMOS_WRITE(reg_a & ~DS_REGA_DV0, RTC_REG_A);
		return;
	}
	CMOS_WRITE(reg_a & ~DS_REGA_DV0, RTC_REG_A);

	if (has_panicked)
		ip32_machine_restart(NULL);

	enable_irq(MACEISA_RTC_IRQ);
}

static inline void ip32_power_button(void)
{
	if (has_panicked)
		return;

	if (shuting_down || kill_cad_pid(SIGINT, 1)) {
		/* No init process or button pressed twice.  */
		ip32_machine_power_off();
	}

	shuting_down = 1;
	blink_timer.data = POWERDOWN_FREQ;
	blink_timeout(POWERDOWN_FREQ);

	init_timer(&power_timer);
	power_timer.function = power_timeout;
	power_timer.expires = jiffies + POWERDOWN_TIMEOUT * HZ;
	add_timer(&power_timer);
}

static irqreturn_t ip32_rtc_int(int irq, void *dev_id)
{
	volatile unsigned char reg_c;

	reg_c = CMOS_READ(RTC_INTR_FLAGS);
	if (!(reg_c & RTC_IRQF)) {
		printk(KERN_WARNING
			"%s: RTC IRQ without RTC_IRQF\n", __FUNCTION__);
	}
	/* Wait until interrupt goes away */
	disable_irq(MACEISA_RTC_IRQ);
	init_timer(&debounce_timer);
	debounce_timer.function = debounce;
	debounce_timer.expires = jiffies + 50;
	add_timer(&debounce_timer);

	printk(KERN_DEBUG "Power button pressed\n");
	ip32_power_button();
	return IRQ_HANDLED;
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

	blink_timer.data = PANIC_FREQ;
	blink_timeout(PANIC_FREQ);

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
	pm_power_off = ip32_machine_power_off;

	init_timer(&blink_timer);
	blink_timer.function = blink_timeout;
	atomic_notifier_chain_register(&panic_notifier_list, &panic_block);

	request_irq(MACEISA_RTC_IRQ, ip32_rtc_int, 0, "rtc", NULL);

	return 0;
}

subsys_initcall(ip32_reboot_setup);
