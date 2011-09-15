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

#define TZIC_NUM_IRQS 128

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
#else
#define tzic_set_irq_fiq NULL
#endif

static unsigned int *wakeup_intr[4];

static __init void tzic_init_gc(unsigned int irq_start)
{
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;
	int idx = irq_start >> 5;

	gc = irq_alloc_generic_chip("tzic", 1, irq_start, tzic_base,
				    handle_level_irq);
	gc->private = tzic_set_irq_fiq;
	gc->wake_enabled = IRQ_MSK(32);
	wakeup_intr[idx] = &gc->wake_active;

	ct = gc->chip_types;
	ct->chip.irq_mask = irq_gc_mask_disable_reg;
	ct->chip.irq_unmask = irq_gc_unmask_enable_reg;
	ct->chip.irq_set_wake = irq_gc_set_wake;
	ct->regs.disable = TZIC_ENCLEAR0(idx);
	ct->regs.enable = TZIC_ENSET0(idx);

	irq_setup_generic_chip(gc, IRQ_MSK(32), 0, IRQ_NOREQUEST, 0);
}

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

	for (i = 0; i < TZIC_NUM_IRQS; i += 32)
		tzic_init_gc(i);

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
			*wakeup_intr[i];
		__raw_writel(v, tzic_base + TZIC_WAKEUP0(i));
	}

	return 0;
}
