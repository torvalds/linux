/*
 * linux/arch/arm/mach-omap1/irq.c
 *
 * Interrupt handler for all OMAP boards
 *
 * Copyright (C) 2004 Nokia Corporation
 * Written by Tony Lindgren <tony@atomide.com>
 * Major cleanups by Juha Yrjölä <juha.yrjola@nokia.com>
 *
 * Completely re-written to support various OMAP chips with bank specific
 * interrupt handlers.
 *
 * Some snippets of the code taken from the older OMAP interrupt handler
 * Copyright (C) 2001 RidgeRun, Inc. Greg Lonnon <glonnon@ridgerun.com>
 *
 * GPIO interrupt handler moved to gpio.c by Juha Yrjola
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/interrupt.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/mach/irq.h>
#include <asm/arch/gpio.h>
#include <asm/arch/cpu.h>

#include <asm/io.h>

#define IRQ_BANK(irq) ((irq) >> 5)
#define IRQ_BIT(irq)  ((irq) & 0x1f)

struct omap_irq_bank {
	unsigned long base_reg;
	unsigned long trigger_map;
	unsigned long wake_enable;
};

static unsigned int irq_bank_count;
static struct omap_irq_bank *irq_banks;

static inline unsigned int irq_bank_readl(int bank, int offset)
{
	return omap_readl(irq_banks[bank].base_reg + offset);
}

static inline void irq_bank_writel(unsigned long value, int bank, int offset)
{
	omap_writel(value, irq_banks[bank].base_reg + offset);
}

static void omap_ack_irq(unsigned int irq)
{
	if (irq > 31)
		omap_writel(0x1, OMAP_IH2_BASE + IRQ_CONTROL_REG_OFFSET);

	omap_writel(0x1, OMAP_IH1_BASE + IRQ_CONTROL_REG_OFFSET);
}

static void omap_mask_irq(unsigned int irq)
{
	int bank = IRQ_BANK(irq);
	u32 l;

	l = omap_readl(irq_banks[bank].base_reg + IRQ_MIR_REG_OFFSET);
	l |= 1 << IRQ_BIT(irq);
	omap_writel(l, irq_banks[bank].base_reg + IRQ_MIR_REG_OFFSET);
}

static void omap_unmask_irq(unsigned int irq)
{
	int bank = IRQ_BANK(irq);
	u32 l;

	l = omap_readl(irq_banks[bank].base_reg + IRQ_MIR_REG_OFFSET);
	l &= ~(1 << IRQ_BIT(irq));
	omap_writel(l, irq_banks[bank].base_reg + IRQ_MIR_REG_OFFSET);
}

static void omap_mask_ack_irq(unsigned int irq)
{
	omap_mask_irq(irq);
	omap_ack_irq(irq);
}

static int omap_wake_irq(unsigned int irq, unsigned int enable)
{
	int bank = IRQ_BANK(irq);

	if (enable)
		irq_banks[bank].wake_enable |= IRQ_BIT(irq);
	else
		irq_banks[bank].wake_enable &= ~IRQ_BIT(irq);

	return 0;
}


/*
 * Allows tuning the IRQ type and priority
 *
 * NOTE: There is currently no OMAP fiq handler for Linux. Read the
 *	 mailing list threads on FIQ handlers if you are planning to
 *	 add a FIQ handler for OMAP.
 */
static void omap_irq_set_cfg(int irq, int fiq, int priority, int trigger)
{
	signed int bank;
	unsigned long val, offset;

	bank = IRQ_BANK(irq);
	/* FIQ is only available on bank 0 interrupts */
	fiq = bank ? 0 : (fiq & 0x1);
	val = fiq | ((priority & 0x1f) << 2) | ((trigger & 0x1) << 1);
	offset = IRQ_ILR0_REG_OFFSET + IRQ_BIT(irq) * 0x4;
	irq_bank_writel(val, bank, offset);
}

#ifdef CONFIG_ARCH_OMAP730
static struct omap_irq_bank omap730_irq_banks[] = {
	{ .base_reg = OMAP_IH1_BASE,		.trigger_map = 0xb3f8e22f },
	{ .base_reg = OMAP_IH2_BASE,		.trigger_map = 0xfdb9c1f2 },
	{ .base_reg = OMAP_IH2_BASE + 0x100,	.trigger_map = 0x800040f3 },
};
#endif

#ifdef CONFIG_ARCH_OMAP15XX
static struct omap_irq_bank omap1510_irq_banks[] = {
	{ .base_reg = OMAP_IH1_BASE,		.trigger_map = 0xb3febfff },
	{ .base_reg = OMAP_IH2_BASE,		.trigger_map = 0xffbfffed },
};
static struct omap_irq_bank omap310_irq_banks[] = {
	{ .base_reg = OMAP_IH1_BASE,		.trigger_map = 0xb3faefc3 },
	{ .base_reg = OMAP_IH2_BASE,		.trigger_map = 0x65b3c061 },
};
#endif

#if defined(CONFIG_ARCH_OMAP16XX)

static struct omap_irq_bank omap1610_irq_banks[] = {
	{ .base_reg = OMAP_IH1_BASE,		.trigger_map = 0xb3fefe8f },
	{ .base_reg = OMAP_IH2_BASE,		.trigger_map = 0xfdb7c1fd },
	{ .base_reg = OMAP_IH2_BASE + 0x100,	.trigger_map = 0xffffb7ff },
	{ .base_reg = OMAP_IH2_BASE + 0x200,	.trigger_map = 0xffffffff },
};
#endif

static struct irq_chip omap_irq_chip = {
	.name		= "MPU",
	.ack		= omap_mask_ack_irq,
	.mask		= omap_mask_irq,
	.unmask		= omap_unmask_irq,
	.set_wake	= omap_wake_irq,
};

void __init omap_init_irq(void)
{
	int i, j;

#ifdef CONFIG_ARCH_OMAP730
	if (cpu_is_omap730()) {
		irq_banks = omap730_irq_banks;
		irq_bank_count = ARRAY_SIZE(omap730_irq_banks);
	}
#endif
#ifdef CONFIG_ARCH_OMAP15XX
	if (cpu_is_omap1510()) {
		irq_banks = omap1510_irq_banks;
		irq_bank_count = ARRAY_SIZE(omap1510_irq_banks);
	}
	if (cpu_is_omap310()) {
		irq_banks = omap310_irq_banks;
		irq_bank_count = ARRAY_SIZE(omap310_irq_banks);
	}
#endif
#if defined(CONFIG_ARCH_OMAP16XX)
	if (cpu_is_omap16xx()) {
		irq_banks = omap1610_irq_banks;
		irq_bank_count = ARRAY_SIZE(omap1610_irq_banks);
	}
#endif
	printk("Total of %i interrupts in %i interrupt banks\n",
	       irq_bank_count * 32, irq_bank_count);

	/* Mask and clear all interrupts */
	for (i = 0; i < irq_bank_count; i++) {
		irq_bank_writel(~0x0, i, IRQ_MIR_REG_OFFSET);
		irq_bank_writel(0x0, i, IRQ_ITR_REG_OFFSET);
	}

	/* Clear any pending interrupts */
	irq_bank_writel(0x03, 0, IRQ_CONTROL_REG_OFFSET);
	irq_bank_writel(0x03, 1, IRQ_CONTROL_REG_OFFSET);

	/* Enable interrupts in global mask */
	if (cpu_is_omap730()) {
		irq_bank_writel(0x0, 0, IRQ_GMR_REG_OFFSET);
	}

	/* Install the interrupt handlers for each bank */
	for (i = 0; i < irq_bank_count; i++) {
		for (j = i * 32; j < (i + 1) * 32; j++) {
			int irq_trigger;

			irq_trigger = irq_banks[i].trigger_map >> IRQ_BIT(j);
			omap_irq_set_cfg(j, 0, 0, irq_trigger);

			set_irq_chip(j, &omap_irq_chip);
			set_irq_handler(j, handle_level_irq);
			set_irq_flags(j, IRQF_VALID);
		}
	}

	/* Unmask level 2 handler */

	if (cpu_is_omap730())
		omap_unmask_irq(INT_730_IH2_IRQ);
	else if (cpu_is_omap15xx())
		omap_unmask_irq(INT_1510_IH2_IRQ);
	else if (cpu_is_omap16xx())
		omap_unmask_irq(INT_1610_IH2_IRQ);
}
