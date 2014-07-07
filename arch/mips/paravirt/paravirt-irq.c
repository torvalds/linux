/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2013 Cavium, Inc.
 */

#include <linux/interrupt.h>
#include <linux/cpumask.h>
#include <linux/kernel.h>
#include <linux/mutex.h>

#include <asm/io.h>

#define MBOX_BITS_PER_CPU 2

static int cpunum_for_cpu(int cpu)
{
#ifdef CONFIG_SMP
	return cpu_logical_map(cpu);
#else
	return get_ebase_cpunum();
#endif
}

struct core_chip_data {
	struct mutex core_irq_mutex;
	bool current_en;
	bool desired_en;
	u8 bit;
};

static struct core_chip_data irq_core_chip_data[8];

static void irq_core_ack(struct irq_data *data)
{
	struct core_chip_data *cd = irq_data_get_irq_chip_data(data);
	unsigned int bit = cd->bit;

	/*
	 * We don't need to disable IRQs to make these atomic since
	 * they are already disabled earlier in the low level
	 * interrupt code.
	 */
	clear_c0_status(0x100 << bit);
	/* The two user interrupts must be cleared manually. */
	if (bit < 2)
		clear_c0_cause(0x100 << bit);
}

static void irq_core_eoi(struct irq_data *data)
{
	struct core_chip_data *cd = irq_data_get_irq_chip_data(data);

	/*
	 * We don't need to disable IRQs to make these atomic since
	 * they are already disabled earlier in the low level
	 * interrupt code.
	 */
	set_c0_status(0x100 << cd->bit);
}

static void irq_core_set_enable_local(void *arg)
{
	struct irq_data *data = arg;
	struct core_chip_data *cd = irq_data_get_irq_chip_data(data);
	unsigned int mask = 0x100 << cd->bit;

	/*
	 * Interrupts are already disabled, so these are atomic.
	 */
	if (cd->desired_en)
		set_c0_status(mask);
	else
		clear_c0_status(mask);

}

static void irq_core_disable(struct irq_data *data)
{
	struct core_chip_data *cd = irq_data_get_irq_chip_data(data);
	cd->desired_en = false;
}

static void irq_core_enable(struct irq_data *data)
{
	struct core_chip_data *cd = irq_data_get_irq_chip_data(data);
	cd->desired_en = true;
}

static void irq_core_bus_lock(struct irq_data *data)
{
	struct core_chip_data *cd = irq_data_get_irq_chip_data(data);

	mutex_lock(&cd->core_irq_mutex);
}

static void irq_core_bus_sync_unlock(struct irq_data *data)
{
	struct core_chip_data *cd = irq_data_get_irq_chip_data(data);

	if (cd->desired_en != cd->current_en) {
		on_each_cpu(irq_core_set_enable_local, data, 1);
		cd->current_en = cd->desired_en;
	}

	mutex_unlock(&cd->core_irq_mutex);
}

static struct irq_chip irq_chip_core = {
	.name = "Core",
	.irq_enable = irq_core_enable,
	.irq_disable = irq_core_disable,
	.irq_ack = irq_core_ack,
	.irq_eoi = irq_core_eoi,
	.irq_bus_lock = irq_core_bus_lock,
	.irq_bus_sync_unlock = irq_core_bus_sync_unlock,

	.irq_cpu_online = irq_core_eoi,
	.irq_cpu_offline = irq_core_ack,
	.flags = IRQCHIP_ONOFFLINE_ENABLED,
};

static void __init irq_init_core(void)
{
	int i;
	int irq;
	struct core_chip_data *cd;

	/* Start with a clean slate */
	clear_c0_status(ST0_IM);
	clear_c0_cause(CAUSEF_IP0 | CAUSEF_IP1);

	for (i = 0; i < ARRAY_SIZE(irq_core_chip_data); i++) {
		cd = irq_core_chip_data + i;
		cd->current_en = false;
		cd->desired_en = false;
		cd->bit = i;
		mutex_init(&cd->core_irq_mutex);

		irq = MIPS_CPU_IRQ_BASE + i;

		switch (i) {
		case 0: /* SW0 */
		case 1: /* SW1 */
		case 5: /* IP5 */
		case 6: /* IP6 */
		case 7: /* IP7 */
			irq_set_chip_data(irq, cd);
			irq_set_chip_and_handler(irq, &irq_chip_core,
						 handle_percpu_irq);
			break;
		default:
			break;
		}
	}
}

static void __iomem *mips_irq_chip;
#define MIPS_IRQ_CHIP_NUM_BITS 0
#define MIPS_IRQ_CHIP_REGS 8

static int mips_irq_cpu_stride;
static int mips_irq_chip_reg_raw;
static int mips_irq_chip_reg_src;
static int mips_irq_chip_reg_en;
static int mips_irq_chip_reg_raw_w1s;
static int mips_irq_chip_reg_raw_w1c;
static int mips_irq_chip_reg_en_w1s;
static int mips_irq_chip_reg_en_w1c;

static void irq_pci_enable(struct irq_data *data)
{
	u32 mask = 1u << data->irq;

	__raw_writel(mask, mips_irq_chip + mips_irq_chip_reg_en_w1s);
}

static void irq_pci_disable(struct irq_data *data)
{
	u32 mask = 1u << data->irq;

	__raw_writel(mask, mips_irq_chip + mips_irq_chip_reg_en_w1c);
}

static void irq_pci_ack(struct irq_data *data)
{
}

static void irq_pci_mask(struct irq_data *data)
{
	u32 mask = 1u << data->irq;

	__raw_writel(mask, mips_irq_chip + mips_irq_chip_reg_en_w1c);
}

static void irq_pci_unmask(struct irq_data *data)
{
	u32 mask = 1u << data->irq;

	__raw_writel(mask, mips_irq_chip + mips_irq_chip_reg_en_w1s);
}

static struct irq_chip irq_chip_pci = {
	.name = "PCI",
	.irq_enable = irq_pci_enable,
	.irq_disable = irq_pci_disable,
	.irq_ack = irq_pci_ack,
	.irq_mask = irq_pci_mask,
	.irq_unmask = irq_pci_unmask,
};

static void irq_mbox_all(struct irq_data *data,  void __iomem *base)
{
	int cpu;
	unsigned int mbox = data->irq - MIPS_IRQ_MBOX0;
	u32 mask;

	WARN_ON(mbox >= MBOX_BITS_PER_CPU);

	for_each_online_cpu(cpu) {
		unsigned int cpuid = cpunum_for_cpu(cpu);
		mask = 1 << (cpuid * MBOX_BITS_PER_CPU + mbox);
		__raw_writel(mask, base + (cpuid * mips_irq_cpu_stride));
	}
}

static void irq_mbox_enable(struct irq_data *data)
{
	irq_mbox_all(data, mips_irq_chip + mips_irq_chip_reg_en_w1s + sizeof(u32));
}

static void irq_mbox_disable(struct irq_data *data)
{
	irq_mbox_all(data, mips_irq_chip + mips_irq_chip_reg_en_w1c + sizeof(u32));
}

static void irq_mbox_ack(struct irq_data *data)
{
	u32 mask;
	unsigned int mbox = data->irq - MIPS_IRQ_MBOX0;

	WARN_ON(mbox >= MBOX_BITS_PER_CPU);

	mask = 1 << (get_ebase_cpunum() * MBOX_BITS_PER_CPU + mbox);
	__raw_writel(mask, mips_irq_chip + mips_irq_chip_reg_raw_w1c + sizeof(u32));
}

void irq_mbox_ipi(int cpu, unsigned int actions)
{
	unsigned int cpuid = cpunum_for_cpu(cpu);
	u32 mask;

	WARN_ON(actions >= (1 << MBOX_BITS_PER_CPU));

	mask = actions << (cpuid * MBOX_BITS_PER_CPU);
	__raw_writel(mask, mips_irq_chip + mips_irq_chip_reg_raw_w1s + sizeof(u32));
}

static void irq_mbox_cpu_onoffline(struct irq_data *data,  void __iomem *base)
{
	unsigned int mbox = data->irq - MIPS_IRQ_MBOX0;
	unsigned int cpuid = get_ebase_cpunum();
	u32 mask;

	WARN_ON(mbox >= MBOX_BITS_PER_CPU);

	mask = 1 << (cpuid * MBOX_BITS_PER_CPU + mbox);
	__raw_writel(mask, base + (cpuid * mips_irq_cpu_stride));

}

static void irq_mbox_cpu_online(struct irq_data *data)
{
	irq_mbox_cpu_onoffline(data, mips_irq_chip + mips_irq_chip_reg_en_w1s + sizeof(u32));
}

static void irq_mbox_cpu_offline(struct irq_data *data)
{
	irq_mbox_cpu_onoffline(data, mips_irq_chip + mips_irq_chip_reg_en_w1c + sizeof(u32));
}

static struct irq_chip irq_chip_mbox = {
	.name = "MBOX",
	.irq_enable = irq_mbox_enable,
	.irq_disable = irq_mbox_disable,
	.irq_ack = irq_mbox_ack,
	.irq_cpu_online = irq_mbox_cpu_online,
	.irq_cpu_offline = irq_mbox_cpu_offline,
	.flags = IRQCHIP_ONOFFLINE_ENABLED,
};

static void __init irq_pci_init(void)
{
	int i, stride;
	u32 num_bits;

	mips_irq_chip = ioremap(0x1e010000, 4096);

	num_bits = __raw_readl(mips_irq_chip + MIPS_IRQ_CHIP_NUM_BITS);
	stride = 8 * (1 + ((num_bits - 1) / 64));


	pr_notice("mips_irq_chip: %u bits, reg stride: %d\n", num_bits, stride);
	mips_irq_chip_reg_raw		= MIPS_IRQ_CHIP_REGS + 0 * stride;
	mips_irq_chip_reg_raw_w1s	= MIPS_IRQ_CHIP_REGS + 1 * stride;
	mips_irq_chip_reg_raw_w1c	= MIPS_IRQ_CHIP_REGS + 2 * stride;
	mips_irq_chip_reg_src		= MIPS_IRQ_CHIP_REGS + 3 * stride;
	mips_irq_chip_reg_en		= MIPS_IRQ_CHIP_REGS + 4 * stride;
	mips_irq_chip_reg_en_w1s	= MIPS_IRQ_CHIP_REGS + 5 * stride;
	mips_irq_chip_reg_en_w1c	= MIPS_IRQ_CHIP_REGS + 6 * stride;
	mips_irq_cpu_stride		= stride * 4;

	for (i = 0; i < 4; i++)
		irq_set_chip_and_handler(i + MIPS_IRQ_PCIA, &irq_chip_pci, handle_level_irq);

	for (i = 0; i < 2; i++)
		irq_set_chip_and_handler(i + MIPS_IRQ_MBOX0, &irq_chip_mbox, handle_percpu_irq);


	set_c0_status(STATUSF_IP2);
}

static void irq_pci_dispatch(void)
{
	unsigned int cpuid = get_ebase_cpunum();
	u32 en;

	en = __raw_readl(mips_irq_chip + mips_irq_chip_reg_src +
			(cpuid * mips_irq_cpu_stride));

	if (!en) {
		en = __raw_readl(mips_irq_chip + mips_irq_chip_reg_src + (cpuid * mips_irq_cpu_stride) + sizeof(u32));
		en = (en >> (2 * cpuid)) & 3;

		if (!en)
			spurious_interrupt();
		else
			do_IRQ(__ffs(en) + MIPS_IRQ_MBOX0);	/* MBOX type */
	} else {
		do_IRQ(__ffs(en));
	}
}


void __init arch_init_irq(void)
{
	irq_init_core();
	irq_pci_init();
}

asmlinkage void plat_irq_dispatch(void)
{
	unsigned int pending = read_c0_cause() & read_c0_status() & ST0_IM;
	int ip;

	if (unlikely(!pending)) {
		spurious_interrupt();
		return;
	}

	ip = ffs(pending) - 1 - STATUSB_IP0;
	if (ip == 2)
		irq_pci_dispatch();
	else
		do_IRQ(MIPS_CPU_IRQ_BASE + ip);
}
