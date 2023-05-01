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
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/io.h>

#include <asm/irq.h>
#include <asm/exception.h>
#include <asm/mach/irq.h>

#include "soc.h"
#include "hardware.h"
#include "common.h"

#define IRQ_BANK(irq) ((irq) >> 5)
#define IRQ_BIT(irq)  ((irq) & 0x1f)

struct omap_irq_bank {
	unsigned long base_reg;
	void __iomem *va;
	unsigned long trigger_map;
	unsigned long wake_enable;
};

static u32 omap_l2_irq;
static unsigned int irq_bank_count;
static struct omap_irq_bank *irq_banks;
static struct irq_domain *domain;

static inline unsigned int irq_bank_readl(int bank, int offset)
{
	return readl_relaxed(irq_banks[bank].va + offset);
}
static inline void irq_bank_writel(unsigned long value, int bank, int offset)
{
	writel_relaxed(value, irq_banks[bank].va + offset);
}

static void omap_ack_irq(int irq)
{
	if (irq > 31)
		writel_relaxed(0x1, irq_banks[1].va + IRQ_CONTROL_REG_OFFSET);

	writel_relaxed(0x1, irq_banks[0].va + IRQ_CONTROL_REG_OFFSET);
}

static void omap_mask_ack_irq(struct irq_data *d)
{
	struct irq_chip_type *ct = irq_data_get_chip_type(d);

	ct->chip.irq_mask(d);
	omap_ack_irq(d->irq);
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

asmlinkage void __exception_irq_entry omap1_handle_irq(struct pt_regs *regs)
{
	void __iomem *l1 = irq_banks[0].va;
	void __iomem *l2 = irq_banks[1].va;
	u32 irqnr;

	do {
		irqnr = readl_relaxed(l1 + IRQ_ITR_REG_OFFSET);
		irqnr &= ~(readl_relaxed(l1 + IRQ_MIR_REG_OFFSET) & 0xffffffff);
		if (!irqnr)
			break;

		irqnr = readl_relaxed(l1 + IRQ_SIR_FIQ_REG_OFFSET);
		if (irqnr)
			goto irq;

		irqnr = readl_relaxed(l1 + IRQ_SIR_IRQ_REG_OFFSET);
		if (irqnr == omap_l2_irq) {
			irqnr = readl_relaxed(l2 + IRQ_SIR_IRQ_REG_OFFSET);
			if (irqnr)
				irqnr += 32;
		}
irq:
		if (irqnr)
			generic_handle_domain_irq(domain, irqnr);
		else
			break;
	} while (irqnr);
}

static __init void
omap_alloc_gc(void __iomem *base, unsigned int irq_start, unsigned int num)
{
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;

	gc = irq_alloc_generic_chip("MPU", 1, irq_start, base,
				    handle_level_irq);
	ct = gc->chip_types;
	ct->chip.irq_ack = omap_mask_ack_irq;
	ct->chip.irq_mask = irq_gc_mask_set_bit;
	ct->chip.irq_unmask = irq_gc_mask_clr_bit;
	ct->chip.irq_set_wake = irq_gc_set_wake;
	ct->regs.mask = IRQ_MIR_REG_OFFSET;
	irq_setup_generic_chip(gc, IRQ_MSK(num), IRQ_GC_INIT_MASK_CACHE,
			       IRQ_NOREQUEST | IRQ_NOPROBE, 0);
}

void __init omap1_init_irq(void)
{
	struct irq_chip_type *ct;
	struct irq_data *d = NULL;
	int i, j, irq_base;
	unsigned long nr_irqs;

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

	for (i = 0; i < irq_bank_count; i++) {
		irq_banks[i].va = ioremap(irq_banks[i].base_reg, 0xff);
		if (WARN_ON(!irq_banks[i].va))
			return;
	}

	nr_irqs = irq_bank_count * 32;

	irq_base = irq_alloc_descs(-1, 0, nr_irqs, 0);
	if (irq_base < 0) {
		pr_warn("Couldn't allocate IRQ numbers\n");
		irq_base = 0;
	}
	omap_l2_irq = irq_base;
	omap_l2_irq -= NR_IRQS_LEGACY;

	domain = irq_domain_add_legacy(NULL, nr_irqs, irq_base, 0,
				       &irq_domain_simple_ops, NULL);

	pr_info("Total of %lu interrupts in %i interrupt banks\n",
		nr_irqs, irq_bank_count);

	/* Mask and clear all interrupts */
	for (i = 0; i < irq_bank_count; i++) {
		irq_bank_writel(~0x0, i, IRQ_MIR_REG_OFFSET);
		irq_bank_writel(0x0, i, IRQ_ITR_REG_OFFSET);
	}

	/* Clear any pending interrupts */
	irq_bank_writel(0x03, 0, IRQ_CONTROL_REG_OFFSET);
	irq_bank_writel(0x03, 1, IRQ_CONTROL_REG_OFFSET);

	/* Install the interrupt handlers for each bank */
	for (i = 0; i < irq_bank_count; i++) {
		for (j = i * 32; j < (i + 1) * 32; j++) {
			int irq_trigger;

			irq_trigger = irq_banks[i].trigger_map >> IRQ_BIT(j);
			omap_irq_set_cfg(j, 0, 0, irq_trigger);
			irq_clear_status_flags(j, IRQ_NOREQUEST);
		}
		omap_alloc_gc(irq_banks[i].va, irq_base + i * 32, 32);
	}

	/* Unmask level 2 handler */
	d = irq_get_irq_data(irq_find_mapping(domain, omap_l2_irq));
	if (d) {
		ct = irq_data_get_chip_type(d);
		ct->chip.irq_unmask(d);
	}
}
