/* arch/arm/plat-s3c64xx/irq-pm.c
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *      Ben Dooks <ben@simtec.co.uk>
 *      http://armlinux.simtec.co.uk/
 *
 * S3C64XX - Interrupt handling Power Management
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/syscore_ops.h>
#include <linux/interrupt.h>
#include <linux/serial_core.h>
#include <linux/irq.h>
#include <linux/io.h>

#include <mach/map.h>

#include <plat/regs-serial.h>
#include <plat/regs-timer.h>
#include <mach/regs-gpio.h>
#include <plat/cpu.h>
#include <plat/pm.h>

/* We handled all the IRQ types in this code, to save having to make several
 * small files to handle each different type separately. Having the EINT_GRP
 * code here shouldn't be as much bloat as the IRQ table space needed when
 * they are enabled. The added benefit is we ensure that these registers are
 * in the same state as we suspended.
 */

static struct sleep_save irq_save[] = {
	SAVE_ITEM(S3C64XX_PRIORITY),
	SAVE_ITEM(S3C64XX_EINT0CON0),
	SAVE_ITEM(S3C64XX_EINT0CON1),
	SAVE_ITEM(S3C64XX_EINT0FLTCON0),
	SAVE_ITEM(S3C64XX_EINT0FLTCON1),
	SAVE_ITEM(S3C64XX_EINT0FLTCON2),
	SAVE_ITEM(S3C64XX_EINT0FLTCON3),
	SAVE_ITEM(S3C64XX_EINT0MASK),
	SAVE_ITEM(S3C64XX_TINT_CSTAT),
};

static struct irq_grp_save {
	u32	fltcon;
	u32	con;
	u32	mask;
} eint_grp_save[5];

static u32 irq_uart_mask[CONFIG_SERIAL_SAMSUNG_UARTS];

static int s3c64xx_irq_pm_suspend(void)
{
	struct irq_grp_save *grp = eint_grp_save;
	int i;

	S3C_PMDBG("%s: suspending IRQs\n", __func__);

	s3c_pm_do_save(irq_save, ARRAY_SIZE(irq_save));

	for (i = 0; i < CONFIG_SERIAL_SAMSUNG_UARTS; i++)
		irq_uart_mask[i] = __raw_readl(S3C_VA_UARTx(i) + S3C64XX_UINTM);

	for (i = 0; i < ARRAY_SIZE(eint_grp_save); i++, grp++) {
		grp->con = __raw_readl(S3C64XX_EINT12CON + (i * 4));
		grp->mask = __raw_readl(S3C64XX_EINT12MASK + (i * 4));
		grp->fltcon = __raw_readl(S3C64XX_EINT12FLTCON + (i * 4));
	}

	return 0;
}

static void s3c64xx_irq_pm_resume(void)
{
	struct irq_grp_save *grp = eint_grp_save;
	int i;

	S3C_PMDBG("%s: resuming IRQs\n", __func__);

	s3c_pm_do_restore(irq_save, ARRAY_SIZE(irq_save));

	for (i = 0; i < CONFIG_SERIAL_SAMSUNG_UARTS; i++)
		__raw_writel(irq_uart_mask[i], S3C_VA_UARTx(i) + S3C64XX_UINTM);

	for (i = 0; i < ARRAY_SIZE(eint_grp_save); i++, grp++) {
		__raw_writel(grp->con, S3C64XX_EINT12CON + (i * 4));
		__raw_writel(grp->mask, S3C64XX_EINT12MASK + (i * 4));
		__raw_writel(grp->fltcon, S3C64XX_EINT12FLTCON + (i * 4));
	}

	S3C_PMDBG("%s: IRQ configuration restored\n", __func__);
}

static struct syscore_ops s3c64xx_irq_syscore_ops = {
	.suspend = s3c64xx_irq_pm_suspend,
	.resume	 = s3c64xx_irq_pm_resume,
};

static __init int s3c64xx_syscore_init(void)
{
	register_syscore_ops(&s3c64xx_irq_syscore_ops);

	return 0;
}

core_initcall(s3c64xx_syscore_init);
