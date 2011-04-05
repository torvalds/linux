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
#include <linux/irq.h>
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
static inline void bcm63xx_internal_irq_mask(struct irq_data *d)
{
	unsigned int irq = d->irq - IRQ_INTERNAL_BASE;
	u32 mask;

	mask = bcm_perf_readl(PERF_IRQMASK_REG);
	mask &= ~(1 << irq);
	bcm_perf_writel(mask, PERF_IRQMASK_REG);
}

static void bcm63xx_internal_irq_unmask(struct irq_data *d)
{
	unsigned int irq = d->irq - IRQ_INTERNAL_BASE;
	u32 mask;

	mask = bcm_perf_readl(PERF_IRQMASK_REG);
	mask |= (1 << irq);
	bcm_perf_writel(mask, PERF_IRQMASK_REG);
}

/*
 * external IRQs operations: mask/unmask and clear on PERF external
 * irq control register.
 */
static void bcm63xx_external_irq_mask(struct irq_data *d)
{
	unsigned int irq = d->irq - IRQ_EXT_BASE;
	u32 reg;

	reg = bcm_perf_readl(PERF_EXTIRQ_CFG_REG);
	reg &= ~EXTIRQ_CFG_MASK(irq);
	bcm_perf_writel(reg, PERF_EXTIRQ_CFG_REG);
}

static void bcm63xx_external_irq_unmask(struct irq_data *d)
{
	unsigned int irq = d->irq - IRQ_EXT_BASE;
	u32 reg;

	reg = bcm_perf_readl(PERF_EXTIRQ_CFG_REG);
	reg |= EXTIRQ_CFG_MASK(irq);
	bcm_perf_writel(reg, PERF_EXTIRQ_CFG_REG);
}

static void bcm63xx_external_irq_clear(struct irq_data *d)
{
	unsigned int irq = d->irq - IRQ_EXT_BASE;
	u32 reg;

	reg = bcm_perf_readl(PERF_EXTIRQ_CFG_REG);
	reg |= EXTIRQ_CFG_CLEAR(irq);
	bcm_perf_writel(reg, PERF_EXTIRQ_CFG_REG);
}

static unsigned int bcm63xx_external_irq_startup(struct irq_data *d)
{
	set_c0_status(0x100 << (d->irq - IRQ_MIPS_BASE));
	irq_enable_hazard();
	bcm63xx_external_irq_unmask(d);
	return 0;
}

static void bcm63xx_external_irq_shutdown(struct irq_data *d)
{
	bcm63xx_external_irq_mask(d);
	clear_c0_status(0x100 << (d->irq - IRQ_MIPS_BASE));
	irq_disable_hazard();
}

static int bcm63xx_external_irq_set_type(struct irq_data *d,
					 unsigned int flow_type)
{
	unsigned int irq = d->irq - IRQ_EXT_BASE;
	u32 reg;

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

	irqd_set_trigger_type(d, flow_type);
	if (flow_type & (IRQ_TYPE_LEVEL_LOW | IRQ_TYPE_LEVEL_HIGH))
		__irq_set_handler_locked(d->irq, handle_level_irq);
	else
		__irq_set_handler_locked(d->irq, handle_edge_irq);

	return IRQ_SET_MASK_OK_NOCOPY;
}

static struct irq_chip bcm63xx_internal_irq_chip = {
	.name		= "bcm63xx_ipic",
	.irq_mask	= bcm63xx_internal_irq_mask,
	.irq_unmask	= bcm63xx_internal_irq_unmask,
};

static struct irq_chip bcm63xx_external_irq_chip = {
	.name		= "bcm63xx_epic",
	.irq_startup	= bcm63xx_external_irq_startup,
	.irq_shutdown	= bcm63xx_external_irq_shutdown,

	.irq_ack	= bcm63xx_external_irq_clear,

	.irq_mask	= bcm63xx_external_irq_mask,
	.irq_unmask	= bcm63xx_external_irq_unmask,

	.irq_set_type	= bcm63xx_external_irq_set_type,
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
		irq_set_chip_and_handler(i, &bcm63xx_internal_irq_chip,
					 handle_level_irq);

	for (i = IRQ_EXT_BASE; i < IRQ_EXT_BASE + 4; ++i)
		irq_set_chip_and_handler(i, &bcm63xx_external_irq_chip,
					 handle_edge_irq);

	setup_irq(IRQ_MIPS_BASE + 2, &cpu_ip2_cascade_action);
}
