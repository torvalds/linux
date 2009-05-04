/*
 * linux/arch/arm/mach-omap2/irq.c
 *
 * Interrupt handler for OMAP2 boards.
 *
 * Copyright (C) 2005 Nokia Corporation
 * Author: Paul Mundt <paul.mundt@nokia.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <mach/hardware.h>
#include <asm/mach/irq.h>


/* selected INTC register offsets */

#define INTC_REVISION		0x0000
#define INTC_SYSCONFIG		0x0010
#define INTC_SYSSTATUS		0x0014
#define INTC_SIR		0x0040
#define INTC_CONTROL		0x0048
#define INTC_MIR_CLEAR0		0x0088
#define INTC_MIR_SET0		0x008c
#define INTC_PENDING_IRQ0	0x0098

/* Number of IRQ state bits in each MIR register */
#define IRQ_BITS_PER_REG	32

/*
 * OMAP2 has a number of different interrupt controllers, each interrupt
 * controller is identified as its own "bank". Register definitions are
 * fairly consistent for each bank, but not all registers are implemented
 * for each bank.. when in doubt, consult the TRM.
 */
static struct omap_irq_bank {
	void __iomem *base_reg;
	unsigned int nr_irqs;
} __attribute__ ((aligned(4))) irq_banks[] = {
	{
		/* MPU INTC */
		.base_reg	= 0,
		.nr_irqs	= 96,
	},
};

/* INTC bank register get/set */

static void intc_bank_write_reg(u32 val, struct omap_irq_bank *bank, u16 reg)
{
	__raw_writel(val, bank->base_reg + reg);
}

static u32 intc_bank_read_reg(struct omap_irq_bank *bank, u16 reg)
{
	return __raw_readl(bank->base_reg + reg);
}

static int previous_irq;

/*
 * On 34xx we can get occasional spurious interrupts if the ack from
 * an interrupt handler does not get posted before we unmask. Warn about
 * the interrupt handlers that need to flush posted writes.
 */
static int omap_check_spurious(unsigned int irq)
{
	u32 sir, spurious;

	sir = intc_bank_read_reg(&irq_banks[0], INTC_SIR);
	spurious = sir >> 6;

	if (spurious > 1) {
		printk(KERN_WARNING "Spurious irq %i: 0x%08x, please flush "
					"posted write for irq %i\n",
					irq, sir, previous_irq);
		return spurious;
	}

	return 0;
}

/* XXX: FIQ and additional INTC support (only MPU at the moment) */
static void omap_ack_irq(unsigned int irq)
{
	intc_bank_write_reg(0x1, &irq_banks[0], INTC_CONTROL);
}

static void omap_mask_irq(unsigned int irq)
{
	int offset = irq & (~(IRQ_BITS_PER_REG - 1));

	if (cpu_is_omap34xx()) {
		int spurious = 0;

		/*
		 * INT_34XX_GPT12_IRQ is also the spurious irq. Maybe because
		 * it is the highest irq number?
		 */
		if (irq == INT_34XX_GPT12_IRQ)
			spurious = omap_check_spurious(irq);

		if (!spurious)
			previous_irq = irq;
	}

	irq &= (IRQ_BITS_PER_REG - 1);

	intc_bank_write_reg(1 << irq, &irq_banks[0], INTC_MIR_SET0 + offset);
}

static void omap_unmask_irq(unsigned int irq)
{
	int offset = irq & (~(IRQ_BITS_PER_REG - 1));

	irq &= (IRQ_BITS_PER_REG - 1);

	intc_bank_write_reg(1 << irq, &irq_banks[0], INTC_MIR_CLEAR0 + offset);
}

static void omap_mask_ack_irq(unsigned int irq)
{
	omap_mask_irq(irq);
	omap_ack_irq(irq);
}

static struct irq_chip omap_irq_chip = {
	.name	= "INTC",
	.ack	= omap_mask_ack_irq,
	.mask	= omap_mask_irq,
	.unmask	= omap_unmask_irq,
	.disable = omap_mask_irq,
};

static void __init omap_irq_bank_init_one(struct omap_irq_bank *bank)
{
	unsigned long tmp;

	tmp = intc_bank_read_reg(bank, INTC_REVISION) & 0xff;
	printk(KERN_INFO "IRQ: Found an INTC at 0x%p "
			 "(revision %ld.%ld) with %d interrupts\n",
			 bank->base_reg, tmp >> 4, tmp & 0xf, bank->nr_irqs);

	tmp = intc_bank_read_reg(bank, INTC_SYSCONFIG);
	tmp |= 1 << 1;	/* soft reset */
	intc_bank_write_reg(tmp, bank, INTC_SYSCONFIG);

	while (!(intc_bank_read_reg(bank, INTC_SYSSTATUS) & 0x1))
		/* Wait for reset to complete */;

	/* Enable autoidle */
	intc_bank_write_reg(1 << 0, bank, INTC_SYSCONFIG);
}

void __init omap_init_irq(void)
{
	unsigned long nr_of_irqs = 0;
	unsigned int nr_banks = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(irq_banks); i++) {
		struct omap_irq_bank *bank = irq_banks + i;

		if (cpu_is_omap24xx())
			bank->base_reg = OMAP2_IO_ADDRESS(OMAP24XX_IC_BASE);
		else if (cpu_is_omap34xx())
			bank->base_reg = OMAP2_IO_ADDRESS(OMAP34XX_IC_BASE);

		omap_irq_bank_init_one(bank);

		nr_of_irqs += bank->nr_irqs;
		nr_banks++;
	}

	printk(KERN_INFO "Total of %ld interrupts on %d active controller%s\n",
	       nr_of_irqs, nr_banks, nr_banks > 1 ? "s" : "");

	for (i = 0; i < nr_of_irqs; i++) {
		set_irq_chip(i, &omap_irq_chip);
		set_irq_handler(i, handle_level_irq);
		set_irq_flags(i, IRQF_VALID);
	}
}

