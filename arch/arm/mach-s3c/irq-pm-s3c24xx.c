// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2003-2004 Simtec Electronics
//	Ben Dooks <ben@simtec.co.uk>
//	http://armlinux.simtec.co.uk/
//
// S3C24XX - IRQ PM code

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/syscore_ops.h>
#include <linux/io.h>

#include "cpu.h"
#include "pm.h"
#include <mach/map-base.h>
#include "map-s3c.h"

#include "regs-irq.h"
#include "regs-gpio.h"
#include "pm-core.h"

#include <asm/irq.h>

int s3c_irq_wake(struct irq_data *data, unsigned int state)
{
	unsigned long irqbit = 1 << data->hwirq;

	if (!(s3c_irqwake_intallow & irqbit))
		return -ENOENT;

	pr_info("wake %s for hwirq %lu\n",
		state ? "enabled" : "disabled", data->hwirq);

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

#ifdef CONFIG_CPU_S3C2416
static struct sleep_save s3c2416_irq_save[] = {
	SAVE_ITEM(S3C2416_INTMSK2),
};

static int s3c2416_irq_suspend(void)
{
	s3c_pm_do_save(s3c2416_irq_save, ARRAY_SIZE(s3c2416_irq_save));

	return 0;
}

static void s3c2416_irq_resume(void)
{
	s3c_pm_do_restore(s3c2416_irq_save, ARRAY_SIZE(s3c2416_irq_save));
}

struct syscore_ops s3c2416_irq_syscore_ops = {
	.suspend	= s3c2416_irq_suspend,
	.resume		= s3c2416_irq_resume,
};
#endif
