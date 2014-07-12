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
#include <linux/spinlock.h>
#include <asm/irq_cpu.h>
#include <asm/mipsregs.h>
#include <bcm63xx_cpu.h>
#include <bcm63xx_regs.h>
#include <bcm63xx_io.h>
#include <bcm63xx_irq.h>


static DEFINE_SPINLOCK(ipic_lock);
static DEFINE_SPINLOCK(epic_lock);

static u32 irq_stat_addr[2];
static u32 irq_mask_addr[2];
static void (*dispatch_internal)(int cpu);
static int is_ext_irq_cascaded;
static unsigned int ext_irq_count;
static unsigned int ext_irq_start, ext_irq_end;
static unsigned int ext_irq_cfg_reg1, ext_irq_cfg_reg2;
static void (*internal_irq_mask)(unsigned int irq);
static void (*internal_irq_unmask)(unsigned int irq);


static inline u32 get_ext_irq_perf_reg(int irq)
{
	if (irq < 4)
		return ext_irq_cfg_reg1;
	return ext_irq_cfg_reg2;
}

static inline void handle_internal(int intbit)
{
	if (is_ext_irq_cascaded &&
	    intbit >= ext_irq_start && intbit <= ext_irq_end)
		do_IRQ(intbit - ext_irq_start + IRQ_EXTERNAL_BASE);
	else
		do_IRQ(intbit + IRQ_INTERNAL_BASE);
}

/*
 * dispatch internal devices IRQ (uart, enet, watchdog, ...). do not
 * prioritize any interrupt relatively to another. the static counter
 * will resume the loop where it ended the last time we left this
 * function.
 */

#define BUILD_IPIC_INTERNAL(width)					\
void __dispatch_internal_##width(int cpu)				\
{									\
	u32 pending[width / 32];					\
	unsigned int src, tgt;						\
	bool irqs_pending = false;					\
	static unsigned int i[2];					\
	unsigned int *next = &i[cpu];					\
	unsigned long flags;						\
									\
	/* read registers in reverse order */				\
	spin_lock_irqsave(&ipic_lock, flags);				\
	for (src = 0, tgt = (width / 32); src < (width / 32); src++) {	\
		u32 val;						\
									\
		val = bcm_readl(irq_stat_addr[cpu] + src * sizeof(u32)); \
		val &= bcm_readl(irq_mask_addr[cpu] + src * sizeof(u32)); \
		pending[--tgt] = val;					\
									\
		if (val)						\
			irqs_pending = true;				\
	}								\
	spin_unlock_irqrestore(&ipic_lock, flags);			\
									\
	if (!irqs_pending)						\
		return;							\
									\
	while (1) {							\
		unsigned int to_call = *next;				\
									\
		*next = (*next + 1) & (width - 1);			\
		if (pending[to_call / 32] & (1 << (to_call & 0x1f))) {	\
			handle_internal(to_call);			\
			break;						\
		}							\
	}								\
}									\
									\
static void __internal_irq_mask_##width(unsigned int irq)		\
{									\
	u32 val;							\
	unsigned reg = (irq / 32) ^ (width/32 - 1);			\
	unsigned bit = irq & 0x1f;					\
	unsigned long flags;						\
	int cpu;							\
									\
	spin_lock_irqsave(&ipic_lock, flags);				\
	for_each_present_cpu(cpu) {					\
		if (!irq_mask_addr[cpu])				\
			break;						\
									\
		val = bcm_readl(irq_mask_addr[cpu] + reg * sizeof(u32));\
		val &= ~(1 << bit);					\
		bcm_writel(val, irq_mask_addr[cpu] + reg * sizeof(u32));\
	}								\
	spin_unlock_irqrestore(&ipic_lock, flags);			\
}									\
									\
static void __internal_irq_unmask_##width(unsigned int irq)		\
{									\
	u32 val;							\
	unsigned reg = (irq / 32) ^ (width/32 - 1);			\
	unsigned bit = irq & 0x1f;					\
	unsigned long flags;						\
	int cpu;							\
									\
	spin_lock_irqsave(&ipic_lock, flags);				\
	for_each_present_cpu(cpu) {					\
		if (!irq_mask_addr[cpu])				\
			break;						\
									\
		val = bcm_readl(irq_mask_addr[cpu] + reg * sizeof(u32));\
		if (cpu_online(cpu))					\
			val |= (1 << bit);				\
		else							\
			val &= ~(1 << bit);				\
		bcm_writel(val, irq_mask_addr[cpu] + reg * sizeof(u32));\
	}								\
	spin_unlock_irqrestore(&ipic_lock, flags);			\
}

BUILD_IPIC_INTERNAL(32);
BUILD_IPIC_INTERNAL(64);

asmlinkage void plat_irq_dispatch(void)
{
	u32 cause;

	do {
		cause = read_c0_cause() & read_c0_status() & ST0_IM;

		if (!cause)
			break;

		if (cause & CAUSEF_IP7)
			do_IRQ(7);
		if (cause & CAUSEF_IP0)
			do_IRQ(0);
		if (cause & CAUSEF_IP1)
			do_IRQ(1);
		if (cause & CAUSEF_IP2)
			dispatch_internal(0);
		if (is_ext_irq_cascaded) {
			if (cause & CAUSEF_IP3)
				dispatch_internal(1);
		} else {
			if (cause & CAUSEF_IP3)
				do_IRQ(IRQ_EXT_0);
			if (cause & CAUSEF_IP4)
				do_IRQ(IRQ_EXT_1);
			if (cause & CAUSEF_IP5)
				do_IRQ(IRQ_EXT_2);
			if (cause & CAUSEF_IP6)
				do_IRQ(IRQ_EXT_3);
		}
	} while (1);
}

/*
 * internal IRQs operations: only mask/unmask on PERF irq mask
 * register.
 */
static void bcm63xx_internal_irq_mask(struct irq_data *d)
{
	internal_irq_mask(d->irq - IRQ_INTERNAL_BASE);
}

static void bcm63xx_internal_irq_unmask(struct irq_data *d)
{
	internal_irq_unmask(d->irq - IRQ_INTERNAL_BASE);
}

/*
 * external IRQs operations: mask/unmask and clear on PERF external
 * irq control register.
 */
static void bcm63xx_external_irq_mask(struct irq_data *d)
{
	unsigned int irq = d->irq - IRQ_EXTERNAL_BASE;
	u32 reg, regaddr;
	unsigned long flags;

	regaddr = get_ext_irq_perf_reg(irq);
	spin_lock_irqsave(&epic_lock, flags);
	reg = bcm_perf_readl(regaddr);

	if (BCMCPU_IS_6348())
		reg &= ~EXTIRQ_CFG_MASK_6348(irq % 4);
	else
		reg &= ~EXTIRQ_CFG_MASK(irq % 4);

	bcm_perf_writel(reg, regaddr);
	spin_unlock_irqrestore(&epic_lock, flags);

	if (is_ext_irq_cascaded)
		internal_irq_mask(irq + ext_irq_start);
}

static void bcm63xx_external_irq_unmask(struct irq_data *d)
{
	unsigned int irq = d->irq - IRQ_EXTERNAL_BASE;
	u32 reg, regaddr;
	unsigned long flags;

	regaddr = get_ext_irq_perf_reg(irq);
	spin_lock_irqsave(&epic_lock, flags);
	reg = bcm_perf_readl(regaddr);

	if (BCMCPU_IS_6348())
		reg |= EXTIRQ_CFG_MASK_6348(irq % 4);
	else
		reg |= EXTIRQ_CFG_MASK(irq % 4);

	bcm_perf_writel(reg, regaddr);
	spin_unlock_irqrestore(&epic_lock, flags);

	if (is_ext_irq_cascaded)
		internal_irq_unmask(irq + ext_irq_start);
}

static void bcm63xx_external_irq_clear(struct irq_data *d)
{
	unsigned int irq = d->irq - IRQ_EXTERNAL_BASE;
	u32 reg, regaddr;
	unsigned long flags;

	regaddr = get_ext_irq_perf_reg(irq);
	spin_lock_irqsave(&epic_lock, flags);
	reg = bcm_perf_readl(regaddr);

	if (BCMCPU_IS_6348())
		reg |= EXTIRQ_CFG_CLEAR_6348(irq % 4);
	else
		reg |= EXTIRQ_CFG_CLEAR(irq % 4);

	bcm_perf_writel(reg, regaddr);
	spin_unlock_irqrestore(&epic_lock, flags);
}

static int bcm63xx_external_irq_set_type(struct irq_data *d,
					 unsigned int flow_type)
{
	unsigned int irq = d->irq - IRQ_EXTERNAL_BASE;
	u32 reg, regaddr;
	int levelsense, sense, bothedge;
	unsigned long flags;

	flow_type &= IRQ_TYPE_SENSE_MASK;

	if (flow_type == IRQ_TYPE_NONE)
		flow_type = IRQ_TYPE_LEVEL_LOW;

	levelsense = sense = bothedge = 0;
	switch (flow_type) {
	case IRQ_TYPE_EDGE_BOTH:
		bothedge = 1;
		break;

	case IRQ_TYPE_EDGE_RISING:
		sense = 1;
		break;

	case IRQ_TYPE_EDGE_FALLING:
		break;

	case IRQ_TYPE_LEVEL_HIGH:
		levelsense = 1;
		sense = 1;
		break;

	case IRQ_TYPE_LEVEL_LOW:
		levelsense = 1;
		break;

	default:
		printk(KERN_ERR "bogus flow type combination given !\n");
		return -EINVAL;
	}

	regaddr = get_ext_irq_perf_reg(irq);
	spin_lock_irqsave(&epic_lock, flags);
	reg = bcm_perf_readl(regaddr);
	irq %= 4;

	switch (bcm63xx_get_cpu_id()) {
	case BCM6348_CPU_ID:
		if (levelsense)
			reg |= EXTIRQ_CFG_LEVELSENSE_6348(irq);
		else
			reg &= ~EXTIRQ_CFG_LEVELSENSE_6348(irq);
		if (sense)
			reg |= EXTIRQ_CFG_SENSE_6348(irq);
		else
			reg &= ~EXTIRQ_CFG_SENSE_6348(irq);
		if (bothedge)
			reg |= EXTIRQ_CFG_BOTHEDGE_6348(irq);
		else
			reg &= ~EXTIRQ_CFG_BOTHEDGE_6348(irq);
		break;

	case BCM3368_CPU_ID:
	case BCM6328_CPU_ID:
	case BCM6338_CPU_ID:
	case BCM6345_CPU_ID:
	case BCM6358_CPU_ID:
	case BCM6362_CPU_ID:
	case BCM6368_CPU_ID:
		if (levelsense)
			reg |= EXTIRQ_CFG_LEVELSENSE(irq);
		else
			reg &= ~EXTIRQ_CFG_LEVELSENSE(irq);
		if (sense)
			reg |= EXTIRQ_CFG_SENSE(irq);
		else
			reg &= ~EXTIRQ_CFG_SENSE(irq);
		if (bothedge)
			reg |= EXTIRQ_CFG_BOTHEDGE(irq);
		else
			reg &= ~EXTIRQ_CFG_BOTHEDGE(irq);
		break;
	default:
		BUG();
	}

	bcm_perf_writel(reg, regaddr);
	spin_unlock_irqrestore(&epic_lock, flags);

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
	.irq_ack	= bcm63xx_external_irq_clear,

	.irq_mask	= bcm63xx_external_irq_mask,
	.irq_unmask	= bcm63xx_external_irq_unmask,

	.irq_set_type	= bcm63xx_external_irq_set_type,
};

static struct irqaction cpu_ip2_cascade_action = {
	.handler	= no_action,
	.name		= "cascade_ip2",
	.flags		= IRQF_NO_THREAD,
};

#ifdef CONFIG_SMP
static struct irqaction cpu_ip3_cascade_action = {
	.handler	= no_action,
	.name		= "cascade_ip3",
	.flags		= IRQF_NO_THREAD,
};
#endif

static struct irqaction cpu_ext_cascade_action = {
	.handler	= no_action,
	.name		= "cascade_extirq",
	.flags		= IRQF_NO_THREAD,
};

static void bcm63xx_init_irq(void)
{
	int irq_bits;

	irq_stat_addr[0] = bcm63xx_regset_address(RSET_PERF);
	irq_mask_addr[0] = bcm63xx_regset_address(RSET_PERF);
	irq_stat_addr[1] = bcm63xx_regset_address(RSET_PERF);
	irq_mask_addr[1] = bcm63xx_regset_address(RSET_PERF);

	switch (bcm63xx_get_cpu_id()) {
	case BCM3368_CPU_ID:
		irq_stat_addr[0] += PERF_IRQSTAT_3368_REG;
		irq_mask_addr[0] += PERF_IRQMASK_3368_REG;
		irq_stat_addr[1] = 0;
		irq_stat_addr[1] = 0;
		irq_bits = 32;
		ext_irq_count = 4;
		ext_irq_cfg_reg1 = PERF_EXTIRQ_CFG_REG_3368;
		break;
	case BCM6328_CPU_ID:
		irq_stat_addr[0] += PERF_IRQSTAT_6328_REG(0);
		irq_mask_addr[0] += PERF_IRQMASK_6328_REG(0);
		irq_stat_addr[1] += PERF_IRQSTAT_6328_REG(1);
		irq_stat_addr[1] += PERF_IRQMASK_6328_REG(1);
		irq_bits = 64;
		ext_irq_count = 4;
		is_ext_irq_cascaded = 1;
		ext_irq_start = BCM_6328_EXT_IRQ0 - IRQ_INTERNAL_BASE;
		ext_irq_end = BCM_6328_EXT_IRQ3 - IRQ_INTERNAL_BASE;
		ext_irq_cfg_reg1 = PERF_EXTIRQ_CFG_REG_6328;
		break;
	case BCM6338_CPU_ID:
		irq_stat_addr[0] += PERF_IRQSTAT_6338_REG;
		irq_mask_addr[0] += PERF_IRQMASK_6338_REG;
		irq_stat_addr[1] = 0;
		irq_mask_addr[1] = 0;
		irq_bits = 32;
		ext_irq_count = 4;
		ext_irq_cfg_reg1 = PERF_EXTIRQ_CFG_REG_6338;
		break;
	case BCM6345_CPU_ID:
		irq_stat_addr[0] += PERF_IRQSTAT_6345_REG;
		irq_mask_addr[0] += PERF_IRQMASK_6345_REG;
		irq_stat_addr[1] = 0;
		irq_mask_addr[1] = 0;
		irq_bits = 32;
		ext_irq_count = 4;
		ext_irq_cfg_reg1 = PERF_EXTIRQ_CFG_REG_6345;
		break;
	case BCM6348_CPU_ID:
		irq_stat_addr[0] += PERF_IRQSTAT_6348_REG;
		irq_mask_addr[0] += PERF_IRQMASK_6348_REG;
		irq_stat_addr[1] = 0;
		irq_mask_addr[1] = 0;
		irq_bits = 32;
		ext_irq_count = 4;
		ext_irq_cfg_reg1 = PERF_EXTIRQ_CFG_REG_6348;
		break;
	case BCM6358_CPU_ID:
		irq_stat_addr[0] += PERF_IRQSTAT_6358_REG(0);
		irq_mask_addr[0] += PERF_IRQMASK_6358_REG(0);
		irq_stat_addr[1] += PERF_IRQSTAT_6358_REG(1);
		irq_mask_addr[1] += PERF_IRQMASK_6358_REG(1);
		irq_bits = 32;
		ext_irq_count = 4;
		is_ext_irq_cascaded = 1;
		ext_irq_start = BCM_6358_EXT_IRQ0 - IRQ_INTERNAL_BASE;
		ext_irq_end = BCM_6358_EXT_IRQ3 - IRQ_INTERNAL_BASE;
		ext_irq_cfg_reg1 = PERF_EXTIRQ_CFG_REG_6358;
		break;
	case BCM6362_CPU_ID:
		irq_stat_addr[0] += PERF_IRQSTAT_6362_REG(0);
		irq_mask_addr[0] += PERF_IRQMASK_6362_REG(0);
		irq_stat_addr[1] += PERF_IRQSTAT_6362_REG(1);
		irq_mask_addr[1] += PERF_IRQMASK_6362_REG(1);
		irq_bits = 64;
		ext_irq_count = 4;
		is_ext_irq_cascaded = 1;
		ext_irq_start = BCM_6362_EXT_IRQ0 - IRQ_INTERNAL_BASE;
		ext_irq_end = BCM_6362_EXT_IRQ3 - IRQ_INTERNAL_BASE;
		ext_irq_cfg_reg1 = PERF_EXTIRQ_CFG_REG_6362;
		break;
	case BCM6368_CPU_ID:
		irq_stat_addr[0] += PERF_IRQSTAT_6368_REG(0);
		irq_mask_addr[0] += PERF_IRQMASK_6368_REG(0);
		irq_stat_addr[1] += PERF_IRQSTAT_6368_REG(1);
		irq_mask_addr[1] += PERF_IRQMASK_6368_REG(1);
		irq_bits = 64;
		ext_irq_count = 6;
		is_ext_irq_cascaded = 1;
		ext_irq_start = BCM_6368_EXT_IRQ0 - IRQ_INTERNAL_BASE;
		ext_irq_end = BCM_6368_EXT_IRQ5 - IRQ_INTERNAL_BASE;
		ext_irq_cfg_reg1 = PERF_EXTIRQ_CFG_REG_6368;
		ext_irq_cfg_reg2 = PERF_EXTIRQ_CFG_REG2_6368;
		break;
	default:
		BUG();
	}

	if (irq_bits == 32) {
		dispatch_internal = __dispatch_internal_32;
		internal_irq_mask = __internal_irq_mask_32;
		internal_irq_unmask = __internal_irq_unmask_32;
	} else {
		dispatch_internal = __dispatch_internal_64;
		internal_irq_mask = __internal_irq_mask_64;
		internal_irq_unmask = __internal_irq_unmask_64;
	}
}

void __init arch_init_irq(void)
{
	int i;

	bcm63xx_init_irq();
	mips_cpu_irq_init();
	for (i = IRQ_INTERNAL_BASE; i < NR_IRQS; ++i)
		irq_set_chip_and_handler(i, &bcm63xx_internal_irq_chip,
					 handle_level_irq);

	for (i = IRQ_EXTERNAL_BASE; i < IRQ_EXTERNAL_BASE + ext_irq_count; ++i)
		irq_set_chip_and_handler(i, &bcm63xx_external_irq_chip,
					 handle_edge_irq);

	if (!is_ext_irq_cascaded) {
		for (i = 3; i < 3 + ext_irq_count; ++i)
			setup_irq(MIPS_CPU_IRQ_BASE + i, &cpu_ext_cascade_action);
	}

	setup_irq(MIPS_CPU_IRQ_BASE + 2, &cpu_ip2_cascade_action);
#ifdef CONFIG_SMP
	if (is_ext_irq_cascaded)
		setup_irq(MIPS_CPU_IRQ_BASE + 3, &cpu_ip3_cascade_action);
#endif
}
