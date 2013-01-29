/* linux/arch/arm/plat-s3c24xx/irq-om.c
 *
 * Copyright (c) 2003-2004 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * S3C24XX - IRQ PM code
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/syscore_ops.h>

#include <plat/cpu.h>
#include <plat/pm.h>
#include <plat/irq.h>

#include <asm/irq.h>

/* state for IRQs over sleep */

/* default is to allow for EINT0..EINT15, and IRQ_RTC as wakeup sources
 *
 * set bit to 1 in allow bitfield to enable the wakeup settings on it
*/

unsigned long s3c_irqwake_intallow	= 1L << (IRQ_RTC - IRQ_EINT0) | 0xfL;
unsigned long s3c_irqwake_eintallow	= 0x0000fff0L;

int s3c_irq_wake(struct irq_data *data, unsigned int state)
{
	unsigned long irqbit = 1 << (data->irq - IRQ_EINT0);

	if (!(s3c_irqwake_intallow & irqbit))
		return -ENOENT;

	printk(KERN_INFO "wake %s for irq %d\n",
	       state ? "enabled" : "disabled", data->irq);

	if (!state)
		s3c_irqwake_intmask |= irqbit;
	else
		s3c_irqwake_intmask &= ~irqbit;

	return 0;
}

static struct sleep_save irq_save[] = {
	SAVE_ITEM(S3C2410_INTMSK),
	SAVE_ITEM(S3C2410_INTSUBMSK),
};

/* the extint values move between the s3c2410/s3c2440 and the s3c2412
 * so we use an array to hold them, and to calculate the address of
 * the register at run-time
*/

static unsigned long save_extint[3];
static unsigned long save_eintflt[4];
static unsigned long save_eintmask;

static int s3c24xx_irq_suspend(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(save_extint); i++)
		save_extint[i] = __raw_readl(S3C24XX_EXTINT0 + (i*4));

	for (i = 0; i < ARRAY_SIZE(save_eintflt); i++)
		save_eintflt[i] = __raw_readl(S3C24XX_EINFLT0 + (i*4));

	s3c_pm_do_save(irq_save, ARRAY_SIZE(irq_save));
	save_eintmask = __raw_readl(S3C24XX_EINTMASK);

	return 0;
}

static void s3c24xx_irq_resume(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(save_extint); i++)
		__raw_writel(save_extint[i], S3C24XX_EXTINT0 + (i*4));

	for (i = 0; i < ARRAY_SIZE(save_eintflt); i++)
		__raw_writel(save_eintflt[i], S3C24XX_EINFLT0 + (i*4));

	s3c_pm_do_restore(irq_save, ARRAY_SIZE(irq_save));
	__raw_writel(save_eintmask, S3C24XX_EINTMASK);
}

struct syscore_ops s3c24xx_irq_syscore_ops = {
	.suspend	= s3c24xx_irq_suspend,
	.resume		= s3c24xx_irq_resume,
};
