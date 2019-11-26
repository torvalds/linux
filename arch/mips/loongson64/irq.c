// SPDX-License-Identifier: GPL-2.0
#include <loongson.h>
#include <irq.h>
#include <linux/interrupt.h>
#include <linux/init.h>

#include <asm/irq_cpu.h>
#include <asm/i8259.h>
#include <asm/mipsregs.h>

#include "smp.h"

extern void loongson3_send_irq_by_ipi(int cpu, int irqs);

unsigned int irq_cpu[16] = {[0 ... 15] = -1};
unsigned int ht_irq[] = {0, 1, 3, 4, 5, 6, 7, 8, 12, 14, 15};
unsigned int local_irq = 1<<0 | 1<<1 | 1<<2 | 1<<7 | 1<<8 | 1<<12;

int plat_set_irq_affinity(struct irq_data *d, const struct cpumask *affinity,
			  bool force)
{
	unsigned int cpu;
	struct cpumask new_affinity;

	/* I/O devices are connected on package-0 */
	cpumask_copy(&new_affinity, affinity);
	for_each_cpu(cpu, affinity)
		if (cpu_data[cpu].package > 0)
			cpumask_clear_cpu(cpu, &new_affinity);

	if (cpumask_empty(&new_affinity))
		return -EINVAL;

	cpumask_copy(d->common->affinity, &new_affinity);

	return IRQ_SET_MASK_OK_NOCOPY;
}

static void ht_irqdispatch(void)
{
	unsigned int i, irq;
	struct irq_data *irqd;
	struct cpumask affinity;

	irq = LOONGSON_HT1_INT_VECTOR(0);
	LOONGSON_HT1_INT_VECTOR(0) = irq; /* Acknowledge the IRQs */

	for (i = 0; i < ARRAY_SIZE(ht_irq); i++) {
		if (!(irq & (0x1 << ht_irq[i])))
			continue;

		/* handled by local core */
		if (local_irq & (0x1 << ht_irq[i])) {
			do_IRQ(ht_irq[i]);
			continue;
		}

		irqd = irq_get_irq_data(ht_irq[i]);
		cpumask_and(&affinity, irqd->common->affinity, cpu_active_mask);
		if (cpumask_empty(&affinity)) {
			do_IRQ(ht_irq[i]);
			continue;
		}

		irq_cpu[ht_irq[i]] = cpumask_next(irq_cpu[ht_irq[i]], &affinity);
		if (irq_cpu[ht_irq[i]] >= nr_cpu_ids)
			irq_cpu[ht_irq[i]] = cpumask_first(&affinity);

		if (irq_cpu[ht_irq[i]] == 0) {
			do_IRQ(ht_irq[i]);
			continue;
		}

		/* balanced by other cores */
		loongson3_send_irq_by_ipi(irq_cpu[ht_irq[i]], (0x1 << ht_irq[i]));
	}
}

#define UNUSED_IPS (CAUSEF_IP5 | CAUSEF_IP4 | CAUSEF_IP1 | CAUSEF_IP0)

asmlinkage void plat_irq_dispatch(void)
{
	unsigned int pending;

	pending = read_c0_cause() & read_c0_status() & ST0_IM;

	if (pending & CAUSEF_IP7)
		do_IRQ(LOONGSON_TIMER_IRQ);
#if defined(CONFIG_SMP)
	if (pending & CAUSEF_IP6)
		loongson3_ipi_interrupt(NULL);
#endif
	if (pending & CAUSEF_IP3)
		ht_irqdispatch();
	if (pending & CAUSEF_IP2)
		do_IRQ(LOONGSON_UART_IRQ);
	if (pending & UNUSED_IPS) {
		pr_err("%s : spurious interrupt\n", __func__);
		spurious_interrupt();
	}
}

static inline void mask_loongson_irq(struct irq_data *d) { }
static inline void unmask_loongson_irq(struct irq_data *d) { }

 /* For MIPS IRQs which shared by all cores */
static struct irq_chip loongson_irq_chip = {
	.name		= "Loongson",
	.irq_ack	= mask_loongson_irq,
	.irq_mask	= mask_loongson_irq,
	.irq_mask_ack	= mask_loongson_irq,
	.irq_unmask	= unmask_loongson_irq,
	.irq_eoi	= unmask_loongson_irq,
};

void irq_router_init(void)
{
	int i;

	/* route LPC int to cpu core0 int 0 */
	LOONGSON_INT_ROUTER_LPC =
		LOONGSON_INT_COREx_INTy(loongson_sysconf.boot_cpu_id, 0);
	/* route HT1 int0 ~ int7 to cpu core0 INT1*/
	for (i = 0; i < 8; i++)
		LOONGSON_INT_ROUTER_HT1(i) =
			LOONGSON_INT_COREx_INTy(loongson_sysconf.boot_cpu_id, 1);
	/* enable HT1 interrupt */
	LOONGSON_HT1_INTN_EN(0) = 0xffffffff;
	/* enable router interrupt intenset */
	LOONGSON_INT_ROUTER_INTENSET =
		LOONGSON_INT_ROUTER_INTEN | (0xffff << 16) | 0x1 << 10;
}

void __init arch_init_irq(void)
{
	struct irq_chip *chip;

	clear_c0_status(ST0_IM | ST0_BEV);

	irq_router_init();
	mips_cpu_irq_init();
	init_i8259_irqs();
	chip = irq_get_chip(I8259A_IRQ_BASE);
	chip->irq_set_affinity = plat_set_irq_affinity;

	irq_set_chip_and_handler(LOONGSON_UART_IRQ,
			&loongson_irq_chip, handle_percpu_irq);
	irq_set_chip_and_handler(LOONGSON_BRIDGE_IRQ,
			&loongson_irq_chip, handle_percpu_irq);

	set_c0_status(STATUSF_IP2 | STATUSF_IP3 | STATUSF_IP6);
}

#ifdef CONFIG_HOTPLUG_CPU

void fixup_irqs(void)
{
	irq_cpu_offline();
	clear_c0_status(ST0_IM);
}

#endif
