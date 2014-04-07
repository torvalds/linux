/* linux/arch/arm/mach-s5p64x0/irq-pm.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * S5P64X0 - Interrupt handling Power Management
 *
 * Based on arch/arm/mach-s3c64xx/irq-pm.c by Ben Dooks
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/syscore_ops.h>
#include <linux/serial_core.h>
#include <linux/serial_s3c.h>
#include <linux/io.h>

#include <plat/pm.h>

#include <mach/regs-gpio.h>

static struct sleep_save irq_save[] = {
	SAVE_ITEM(S5P64X0_EINT0CON0),
	SAVE_ITEM(S5P64X0_EINT0FLTCON0),
	SAVE_ITEM(S5P64X0_EINT0FLTCON1),
	SAVE_ITEM(S5P64X0_EINT0MASK),
};

static struct irq_grp_save {
	u32	con;
	u32	fltcon;
	u32	mask;
} eint_grp_save[4];

#ifdef CONFIG_SERIAL_SAMSUNG
static u32 irq_uart_mask[CONFIG_SERIAL_SAMSUNG_UARTS];
#endif

static int s5p64x0_irq_pm_suspend(void)
{
	struct irq_grp_save *grp = eint_grp_save;
	int i;

	S3C_PMDBG("%s: suspending IRQs\n", __func__);

	s3c_pm_do_save(irq_save, ARRAY_SIZE(irq_save));

#ifdef CONFIG_SERIAL_SAMSUNG
	for (i = 0; i < CONFIG_SERIAL_SAMSUNG_UARTS; i++)
		irq_uart_mask[i] = __raw_readl(S3C_VA_UARTx(i) + S3C64XX_UINTM);
#endif

	for (i = 0; i < ARRAY_SIZE(eint_grp_save); i++, grp++) {
		grp->con = __raw_readl(S5P64X0_EINT12CON + (i * 4));
		grp->mask = __raw_readl(S5P64X0_EINT12MASK + (i * 4));
		grp->fltcon = __raw_readl(S5P64X0_EINT12FLTCON + (i * 4));
	}

	return 0;
}

static void s5p64x0_irq_pm_resume(void)
{
	struct irq_grp_save *grp = eint_grp_save;
	int i;

	S3C_PMDBG("%s: resuming IRQs\n", __func__);

	s3c_pm_do_restore(irq_save, ARRAY_SIZE(irq_save));

#ifdef CONFIG_SERIAL_SAMSUNG
	for (i = 0; i < CONFIG_SERIAL_SAMSUNG_UARTS; i++)
		__raw_writel(irq_uart_mask[i], S3C_VA_UARTx(i) + S3C64XX_UINTM);
#endif

	for (i = 0; i < ARRAY_SIZE(eint_grp_save); i++, grp++) {
		__raw_writel(grp->con, S5P64X0_EINT12CON + (i * 4));
		__raw_writel(grp->mask, S5P64X0_EINT12MASK + (i * 4));
		__raw_writel(grp->fltcon, S5P64X0_EINT12FLTCON + (i * 4));
	}

	S3C_PMDBG("%s: IRQ configuration restored\n", __func__);
}

static struct syscore_ops s5p64x0_irq_syscore_ops = {
	.suspend = s5p64x0_irq_pm_suspend,
	.resume  = s5p64x0_irq_pm_resume,
};

static int __init s5p64x0_syscore_init(void)
{
	register_syscore_ops(&s5p64x0_irq_syscore_ops);

	return 0;
}
core_initcall(s5p64x0_syscore_init);
