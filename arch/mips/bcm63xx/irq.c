/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 Maxime Bizon <mbizon@freebox.fr>
 * Copyright (C) 2008 Nicolas Schichan <nschichan@freebox.fr>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <asm/irq_cpu.h>
#include <asm/mipsregs.h>
#include <bcm63xx_cpu.h>
#include <bcm63xx_regs.h>
#include <bcm63xx_io.h>
#include <bcm63xx_irq.h>

/*
 * dispatch internal devices IRQ (uart, enet, watchdog, ...). do not
 * prioritize any interrupt relatively to another. the static counter
 * will resume the loop where it ended the last time we left this
 * function.
 */
static void bcm63xx_irq_dispatch_internal(void)
{
	u32 pending;
	static int i;

	pending = bcm_perf_readl(PERF_IRQMASK_REG) &
		bcm_perf_readl(PERF_IRQSTAT_REG);

	if (!pending)
		return ;

	while (1) {
		int to_call = i;

		i = (i + 1) & 0x1f;
		if (pending & (1 << to_call)) {
			do_IRQ(to_call + IRQ_INTERNAL_BASE);
			break;
		}
	}
}

asmlinkage void plat_irq_dispatch(void)
{
	u32 cause;

	do {
		cause = read_c0_cause() & read_c0_status() & ST0_IM;

		if (!cause)
			break;

		if (cause & CAUSEF_IP7)
			do_IRQ(7);
		if (cause & CAUSEF_IP2)
			bcm63xx_irq_dispatch_internal();
		if (cause & CAUSEF_IP3)
			do_IRQ(IRQ_EXT_0);
		if (cause & CAUSEF_IP4)
			do_IRQ(IRQ_EXT_1);
		if (cause & CAUSEF_IP5)
			do_IRQ(IRQ_EXT_2);
		if (cause & CAUSEF_IP6)
			do_IRQ(IRQ_EXT_3);
	} while (1);
}

/*
 * internal IRQs operations: only mask/unmask on PERF irq mask
 * register.
 */
static inline void bcm63xx_internal_irq_mask(unsigned int irq)
{
	u32 mask;

	irq -= IRQ_INTERNAL_BASE;
	mask = bcm_perf_readl(PERF_IRQMASK_REG);
	mask &= ~(1 << irq);
	bcm_perf_writel(mask, PERF_IRQMASK_REG);
}

static void bcm63xx_internal_irq_unmask(unsigned int irq)
{
	u32 mask;

	irq -= IRQ_INTERNAL_BASE;
	mask = bcm_perf_readl(PERF_IRQMASK_REG);
	mask |= (1 << irq);
	bcm_perf_writel(mask, PERF_IRQMASK_REG);
}

static unsigned int bcm63xx_internal_irq_startup(unsigned int irq)
{
	bcm63xx_internal_irq_unmask(irq);
	return 0;
}

/*
 * external IRQs operations: mask/unmask and clear on PERF external
 * irq control register.
 */
static void bcm63xx_external_irq_mask(unsigned int irq)
{
	u32 reg;

	irq -= IRQ_EXT_BASE;
	reg = bcm_perf_readl(PERF_EXTIRQ_CFG_REG);
	reg &= ~EXTIRQ_CFG_MASK(irq);
	bcm_perf_writel(reg, PERF_EXTIRQ_CFG_REG);
}

static void bcm63xx_external_irq_unmask(unsigned int irq)
{
	u32 reg;

	irq -= IRQ_EXT_BASE;
	reg = bcm_perf_readl(PERF_EXTIRQ_CFG_REG);
	reg |= EXTIRQ_CFG_MASK(irq);
	bcm_perf_writel(reg, PERF_EXTIRQ_CFG_REG);
}

static void bcm63xx_external_irq_clear(unsigned int irq)
{
	u32 reg;

	irq -= IRQ_EXT_BASE;
	reg = bcm_perf_readl(PERF_EXTIRQ_CFG_REG);
	reg |= EXTIRQ_CFG_CLEAR(irq);
	bcm_perf_writel(reg, PERF_EXTIRQ_CFG_REG);
}

static unsigned int bcm63xx_external_irq_startup(unsigned int irq)
{
	set_c0_status(0x100 << (irq - IRQ_MIPS_BASE));
	irq_enable_hazard();
	bcm63xx_external_irq_unmask(irq);
	return 0;
}

static void bcm63xx_external_irq_shutdown(unsigned int irq)
{
	bcm63xx_external_irq_mask(irq);
	clear_c0_status(0x100 << (irq - IRQ_MIPS_BASE));
	irq_disable_hazard();
}

static int bcm63xx_external_irq_set_type(unsigned int irq,
					 unsigned int flow_type)
{
	u32 reg;
	struct irq_desc *desc = irq_desc + irq;

	irq -= IRQ_EXT_BASE;

	flow_type &= IRQ_TYPE_SENSE_MASK;

	if (flow_type == IRQ_TYPE_NONE)
		flow_type = IRQ_TYPE_LEVEL_LOW;

	reg = bcm_perf_readl(PERF_EXTIRQ_CFG_REG);
	switch (flow_type) {
	case IRQ_TYPE_EDGE_BOTH:
		reg &= ~EXTIRQ_CFG_LEVELSENSE(irq);
		reg |= EXTIRQ_CFG_BOTHEDGE(irq);
		break;

	case IRQ_TYPE_EDGE_RISING:
		reg &= ~EXTIRQ_CFG_LEVELSENSE(irq);
		reg |= EXTIRQ_CFG_SENSE(irq);
		reg &= ~EXTIRQ_CFG_BOTHEDGE(irq);
		break;

	case IRQ_TYPE_EDGE_FALLING:
		reg &= ~EXTIRQ_CFG_LEVELSENSE(irq);
		reg &= ~EXTIRQ_CFG_SENSE(irq);
		reg &= ~EXTIRQ_CFG_BOTHEDGE(irq);
		break;

	case IRQ_TYPE_LEVEL_HIGH:
		reg |= EXTIRQ_CFG_LEVELSENSE(irq);
		reg |= EXTIRQ_CFG_SENSE(irq);
		break;

	case IRQ_TYPE_LEVEL_LOW:
		reg |= EXTIRQ_CFG_LEVELSENSE(irq);
		reg &= ~EXTIRQ_CFG_SENSE(irq);
		break;

	default:
		printk(KERN_ERR "bogus flow type combination given !\n");
		return -EINVAL;
	}
	bcm_perf_writel(reg, PERF_EXTIRQ_CFG_REG);

	if (flow_type & (IRQ_TYPE_LEVEL_LOW | IRQ_TYPE_LEVEL_HIGH))  {
		desc->status |= IRQ_LEVEL;
		desc->handle_irq = handle_level_irq;
	} else {
		desc->handle_irq = handle_edge_irq;
	}

	return 0;
}

static struct irq_chip bcm63xx_internal_irq_chip = {
	.name		= "bcm63xx_ipic",
	.startup	= bcm63xx_internal_irq_startup,
	.shutdown	= bcm63xx_internal_irq_mask,

	.mask		= bcm63xx_internal_irq_mask,
	.mask_ack	= bcm63xx_internal_irq_mask,
	.unmask		= bcm63xx_internal_irq_unmask,
};

static struct irq_chip bcm63xx_external_irq_chip = {
	.name		= "bcm63xx_epic",
	.startup	= bcm63xx_external_irq_startup,
	.shutdown	= bcm63xx_external_irq_shutdown,

	.ack		= bcm63xx_external_irq_clear,

	.mask		= bcm63xx_external_irq_mask,
	.unmask		= bcm63xx_external_irq_unmask,

	.set_type	= bcm63xx_external_irq_set_type,
};

static struct irqaction cpu_ip2_cascade_action = {
	.handler	= no_action,
	.name		= "cascade_ip2",
};

void __init arch_init_irq(void)
{
	int i;

	mips_cpu_irq_init();
	for (i = IRQ_INTERNAL_BASE; i < NR_IRQS; ++i)
		set_irq_chip_and_handler(i, &bcm63xx_internal_irq_chip,
					 handle_level_irq);

	for (i = IRQ_EXT_BASE; i < IRQ_EXT_BASE + 4; ++i)
		set_irq_chip_and_handler(i, &bcm63xx_external_irq_chip,
					 handle_edge_irq);

	setup_irq(IRQ_MIPS_BASE + 2, &cpu_ip2_cascade_action);
}
