#include <loongson.h>
#include <irq.h>
#include <linux/interrupt.h>
#include <linux/module.h>

#include <asm/irq_cpu.h>
#include <asm/i8259.h>
#include <asm/mipsregs.h>

#include "smp.h"

unsigned int ht_irq[] = {1, 3, 4, 5, 6, 7, 8, 12, 14, 15};

static void ht_irqdispatch(void)
{
	unsigned int i, irq;

	irq = LOONGSON_HT1_INT_VECTOR(0);
	LOONGSON_HT1_INT_VECTOR(0) = irq; /* Acknowledge the IRQs */

	for (i = 0; i < ARRAY_SIZE(ht_irq); i++) {
		if (irq & (0x1 << ht_irq[i]))
			do_IRQ(ht_irq[i]);
	}
}

void mach_irq_dispatch(unsigned int pending)
{
	if (pending & CAUSEF_IP7)
		do_IRQ(LOONGSON_TIMER_IRQ);
#if defined(CONFIG_SMP)
	else if (pending & CAUSEF_IP6)
		loongson3_ipi_interrupt(NULL);
#endif
	else if (pending & CAUSEF_IP3)
		ht_irqdispatch();
	else if (pending & CAUSEF_IP2)
		do_IRQ(LOONGSON_UART_IRQ);
	else {
		pr_err("%s : spurious interrupt\n", __func__);
		spurious_interrupt();
	}
}

static struct irqaction cascade_irqaction = {
	.handler = no_action,
	.name = "cascade",
};

static inline void mask_loongson_irq(struct irq_data *d)
{
	clear_c0_status(0x100 << (d->irq - MIPS_CPU_IRQ_BASE));
	irq_disable_hazard();

	/* Workaround: UART IRQ may deliver to any core */
	if (d->irq == LOONGSON_UART_IRQ) {
		int cpu = smp_processor_id();
		int node_id = cpu / loongson_sysconf.cores_per_node;
		int core_id = cpu % loongson_sysconf.cores_per_node;
		u64 intenclr_addr = smp_group[node_id] |
			(u64)(&LOONGSON_INT_ROUTER_INTENCLR);
		u64 introuter_lpc_addr = smp_group[node_id] |
			(u64)(&LOONGSON_INT_ROUTER_LPC);

		*(volatile u32 *)intenclr_addr = 1 << 10;
		*(volatile u8 *)introuter_lpc_addr = 0x10 + (1<<core_id);
	}
}

static inline void unmask_loongson_irq(struct irq_data *d)
{
	/* Workaround: UART IRQ may deliver to any core */
	if (d->irq == LOONGSON_UART_IRQ) {
		int cpu = smp_processor_id();
		int node_id = cpu / loongson_sysconf.cores_per_node;
		int core_id = cpu % loongson_sysconf.cores_per_node;
		u64 intenset_addr = smp_group[node_id] |
			(u64)(&LOONGSON_INT_ROUTER_INTENSET);
		u64 introuter_lpc_addr = smp_group[node_id] |
			(u64)(&LOONGSON_INT_ROUTER_LPC);

		*(volatile u32 *)intenset_addr = 1 << 10;
		*(volatile u8 *)introuter_lpc_addr = 0x10 + (1<<core_id);
	}

	set_c0_status(0x100 << (d->irq - MIPS_CPU_IRQ_BASE));
	irq_enable_hazard();
}

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
	LOONGSON_INT_ROUTER_LPC = LOONGSON_INT_CORE0_INT0;
	/* route HT1 int0 ~ int7 to cpu core0 INT1*/
	for (i = 0; i < 8; i++)
		LOONGSON_INT_ROUTER_HT1(i) = LOONGSON_INT_CORE0_INT1;
	/* enable HT1 interrupt */
	LOONGSON_HT1_INTN_EN(0) = 0xffffffff;
	/* enable router interrupt intenset */
	LOONGSON_INT_ROUTER_INTENSET =
		LOONGSON_INT_ROUTER_INTEN | (0xffff << 16) | 0x1 << 10;
}

void __init mach_init_irq(void)
{
	clear_c0_status(ST0_IM | ST0_BEV);

	irq_router_init();
	mips_cpu_irq_init();
	init_i8259_irqs();
	irq_set_chip_and_handler(LOONGSON_UART_IRQ,
			&loongson_irq_chip, handle_level_irq);

	/* setup HT1 irq */
	setup_irq(LOONGSON_HT1_IRQ, &cascade_irqaction);

	set_c0_status(STATUSF_IP2 | STATUSF_IP6);
}

#ifdef CONFIG_HOTPLUG_CPU

void fixup_irqs(void)
{
	irq_cpu_offline();
	clear_c0_status(ST0_IM);
}

#endif
