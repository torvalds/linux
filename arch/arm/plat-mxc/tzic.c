/*
 * Copyright (C)2004-2010 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/io.h>

#include <asm/mach/irq.h>

#include <mach/hardware.h>
#include <mach/common.h>

#include "irq-common.h"

/*
 *****************************************
 * TZIC Registers                        *
 *****************************************
 */

#define TZIC_INTCNTL	0x0000	/* Control register */
#define TZIC_INTTYPE	0x0004	/* Controller Type register */
#define TZIC_IMPID	0x0008	/* Distributor Implementer Identification */
#define TZIC_PRIOMASK	0x000C	/* Priority Mask Reg */
#define TZIC_SYNCCTRL	0x0010	/* Synchronizer Control register */
#define TZIC_DSMINT	0x0014	/* DSM interrupt Holdoffregister */
#define TZIC_INTSEC0(i)	(0x0080 + ((i) << 2)) /* Interrupt Security Reg 0 */
#define TZIC_ENSET0(i)	(0x0100 + ((i) << 2)) /* Enable Set Reg 0 */
#define TZIC_ENCLEAR0(i) (0x0180 + ((i) << 2)) /* Enable Clear Reg 0 */
#define TZIC_SRCSET0	0x0200	/* Source Set Register 0 */
#define TZIC_SRCCLAR0	0x0280	/* Source Clear Register 0 */
#define TZIC_PRIORITY0	0x0400	/* Priority Register 0 */
#define TZIC_PND0	0x0D00	/* Pending Register 0 */
#define TZIC_HIPND0	0x0D80	/* High Priority Pending Register */
#define TZIC_WAKEUP0(i)	(0x0E00 + ((i) << 2))	/* Wakeup Config Register */
#define TZIC_SWINT	0x0F00	/* Software Interrupt Rigger Register */
#define TZIC_ID0	0x0FD0	/* Indentification Register 0 */

void __iomem *tzic_base; /* Used as irq controller base in entry-macro.S */

#ifdef CONFIG_FIQ
static int tzic_set_irq_fiq(unsigned int irq, unsigned int type)
{
	unsigned int index, mask, value;

	index = irq >> 5;
	if (unlikely(index >= 4))
		return -EINVAL;
	mask = 1U << (irq & 0x1F);

	value = __raw_readl(tzic_base + TZIC_INTSEC0(index)) | mask;
	if (type)
		value &= ~mask;
	__raw_writel(value, tzic_base + TZIC_INTSEC0(index));

	return 0;
}
#endif

/**
 * tzic_mask_irq() - Disable interrupt source "d" in the TZIC
 *
 * @param  d            interrupt source
 */
static void tzic_mask_irq(struct irq_data *d)
{
	int index, off;

	index = d->irq >> 5;
	off = d->irq & 0x1F;
	__raw_writel(1 << off, tzic_base + TZIC_ENCLEAR0(index));
}

/**
 * tzic_unmask_irq() - Enable interrupt source "d" in the TZIC
 *
 * @param  d            interrupt source
 */
static void tzic_unmask_irq(struct irq_data *d)
{
	int index, off;

	index = d->irq >> 5;
	off = d->irq & 0x1F;
	__raw_writel(1 << off, tzic_base + TZIC_ENSET0(index));
}

static unsigned int wakeup_intr[4];

/**
 * tzic_set_wake_irq() - Set interrupt source "d" in the TZIC as a wake-up source.
 *
 * @param  d            interrupt source
 * @param  enable       enable as wake-up if equal to non-zero
 * 			disble as wake-up if equal to zero
 *
 * @return       This function returns 0 on success.
 */
static int tzic_set_wake_irq(struct irq_data *d, unsigned int enable)
{
	unsigned int index, off;

	index = d->irq >> 5;
	off = d->irq & 0x1F;

	if (index > 3)
		return -EINVAL;

	if (enable)
		wakeup_intr[index] |= (1 << off);
	else
		wakeup_intr[index] &= ~(1 << off);

	return 0;
}

static struct mxc_irq_chip mxc_tzic_chip = {
	.base = {
		.name = "MXC_TZIC",
		.irq_ack = tzic_mask_irq,
		.irq_mask = tzic_mask_irq,
		.irq_unmask = tzic_unmask_irq,
		.irq_set_wake = tzic_set_wake_irq,
	},
#ifdef CONFIG_FIQ
	.set_irq_fiq = tzic_set_irq_fiq,
#endif
};

/*
 * This function initializes the TZIC hardware and disables all the
 * interrupts. It registers the interrupt enable and disable functions
 * to the kernel for each interrupt source.
 */
void __init tzic_init_irq(void __iomem *irqbase)
{
	int i;

	tzic_base = irqbase;
	/* put the TZIC into the reset value with
	 * all interrupts disabled
	 */
	i = __raw_readl(tzic_base + TZIC_INTCNTL);

	__raw_writel(0x80010001, tzic_base + TZIC_INTCNTL);
	__raw_writel(0x1f, tzic_base + TZIC_PRIOMASK);
	__raw_writel(0x02, tzic_base + TZIC_SYNCCTRL);

	for (i = 0; i < 4; i++)
		__raw_writel(0xFFFFFFFF, tzic_base + TZIC_INTSEC0(i));

	/* disable all interrupts */
	for (i = 0; i < 4; i++)
		__raw_writel(0xFFFFFFFF, tzic_base + TZIC_ENCLEAR0(i));

	/* all IRQ no FIQ Warning :: No selection */

	for (i = 0; i < MXC_INTERNAL_IRQS; i++) {
		set_irq_chip(i, &mxc_tzic_chip.base);
		set_irq_handler(i, handle_level_irq);
		set_irq_flags(i, IRQF_VALID);
	}

#ifdef CONFIG_FIQ
	/* Initialize FIQ */
	init_FIQ();
#endif

	pr_info("TrustZone Interrupt Controller (TZIC) initialized\n");
}

/**
 * tzic_enable_wake() - enable wakeup interrupt
 *
 * @param is_idle		1 if called in idle loop (ENSET0 register);
 *				0 to be used when called from low power entry
 * @return			0 if successful; non-zero otherwise
 */
int tzic_enable_wake(int is_idle)
{
	unsigned int i, v;

	__raw_writel(1, tzic_base + TZIC_DSMINT);
	if (unlikely(__raw_readl(tzic_base + TZIC_DSMINT) == 0))
		return -EAGAIN;

	for (i = 0; i < 4; i++) {
		v = is_idle ? __raw_readl(tzic_base + TZIC_ENSET0(i)) :
			wakeup_intr[i];
		__raw_writel(v, tzic_base + TZIC_WAKEUP0(i));
	}

	return 0;
}
