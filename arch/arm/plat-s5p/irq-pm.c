/* linux/arch/arm/plat-s5p/irq-pm.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Based on arch/arm/plat-s3c24xx/irq-pm.c,
 * Copyright (c) 2003,2004 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/sysdev.h>

#include <plat/cpu.h>
#include <plat/irqs.h>
#include <plat/pm.h>
#include <mach/map.h>

#include <mach/regs-gpio.h>
#include <mach/regs-irq.h>

/* state for IRQs over sleep */

/* default is to allow for EINT0..EINT31, and IRQ_RTC_TIC, IRQ_RTC_ALARM,
 * as wakeup sources
 *
 * set bit to 1 in allow bitfield to enable the wakeup settings on it
*/

unsigned long s3c_irqwake_intallow	= 0x00000006L;
unsigned long s3c_irqwake_eintallow	= 0xffffffffL;

int s3c_irq_wake(unsigned int irqno, unsigned int state)
{
	unsigned long irqbit;

	switch (irqno) {
	case IRQ_RTC_TIC:
	case IRQ_RTC_ALARM:
		irqbit = 1 << (irqno + 1 - IRQ_RTC_ALARM);
		if (!state)
			s3c_irqwake_intmask |= irqbit;
		else
			s3c_irqwake_intmask &= ~irqbit;
		break;
	default:
		return -ENOENT;
	}
	return 0;
}

static struct sleep_save eint_save[] = {
	SAVE_ITEM(S5P_EINT_CON(0)),
	SAVE_ITEM(S5P_EINT_CON(1)),
	SAVE_ITEM(S5P_EINT_CON(2)),
	SAVE_ITEM(S5P_EINT_CON(3)),

	SAVE_ITEM(S5P_EINT_FLTCON(0)),
	SAVE_ITEM(S5P_EINT_FLTCON(1)),
	SAVE_ITEM(S5P_EINT_FLTCON(2)),
	SAVE_ITEM(S5P_EINT_FLTCON(3)),
	SAVE_ITEM(S5P_EINT_FLTCON(4)),
	SAVE_ITEM(S5P_EINT_FLTCON(5)),
	SAVE_ITEM(S5P_EINT_FLTCON(6)),
	SAVE_ITEM(S5P_EINT_FLTCON(7)),

	SAVE_ITEM(S5P_EINT_MASK(0)),
	SAVE_ITEM(S5P_EINT_MASK(1)),
	SAVE_ITEM(S5P_EINT_MASK(2)),
	SAVE_ITEM(S5P_EINT_MASK(3)),
};

int s3c24xx_irq_suspend(struct sys_device *dev, pm_message_t state)
{
	s3c_pm_do_save(eint_save, ARRAY_SIZE(eint_save));

	return 0;
}

int s3c24xx_irq_resume(struct sys_device *dev)
{
	s3c_pm_do_restore(eint_save, ARRAY_SIZE(eint_save));

	return 0;
}

