/*
 * SMP support for J2 processor
 *
 * Copyright (C) 2015-2016 Smart Energy Instruments, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <asm/cmpxchg.h>

DEFINE_PER_CPU(unsigned, j2_ipi_messages);

extern u32 *sh2_cpuid_addr;
static u32 *j2_ipi_trigger;
static int j2_ipi_irq;

static irqreturn_t j2_ipi_interrupt_handler(int irq, void *arg)
{
	unsigned cpu = hard_smp_processor_id();
	volatile unsigned *pmsg = &per_cpu(j2_ipi_messages, cpu);
	unsigned messages, i;

	do messages = *pmsg;
	while (cmpxchg(pmsg, messages, 0) != messages);

	if (!messages) return IRQ_NONE;

	for (i=0; i<SMP_MSG_NR; i++)
		if (messages & (1U<<i))
			smp_message_recv(i);

	return IRQ_HANDLED;
}

static void j2_smp_setup(void)
{
}

static void j2_prepare_cpus(unsigned int max_cpus)
{
	struct device_node *np;
	unsigned i, max = 1;

	np = of_find_compatible_node(NULL, NULL, "jcore,ipi-controller");
	if (!np)
		goto out;

	j2_ipi_irq = irq_of_parse_and_map(np, 0);
	j2_ipi_trigger = of_iomap(np, 0);
	if (!j2_ipi_irq || !j2_ipi_trigger)
		goto out;

	np = of_find_compatible_node(NULL, NULL, "jcore,cpuid-mmio");
	if (!np)
		goto out;

	sh2_cpuid_addr = of_iomap(np, 0);
	if (!sh2_cpuid_addr)
		goto out;

	if (request_irq(j2_ipi_irq, j2_ipi_interrupt_handler, IRQF_PERCPU,
			"ipi", (void *)j2_ipi_interrupt_handler) != 0)
		goto out;

	max = max_cpus;
out:
	/* Disable any cpus past max_cpus, or all secondaries if we didn't
	 * get the necessary resources to support SMP. */
	for (i=max; i<NR_CPUS; i++) {
		set_cpu_possible(i, false);
		set_cpu_present(i, false);
	}
}

static void j2_start_cpu(unsigned int cpu, unsigned long entry_point)
{
	struct device_node *np;
	u32 regs[2];
	void __iomem *release, *initpc;

	if (!cpu) return;

	np = of_get_cpu_node(cpu, NULL);
	if (!np) return;

	if (of_property_read_u32_array(np, "cpu-release-addr", regs, 2)) return;
	release = ioremap_nocache(regs[0], sizeof(u32));
	initpc = ioremap_nocache(regs[1], sizeof(u32));

	__raw_writel(entry_point, initpc);
	__raw_writel(1, release);

	iounmap(initpc);
	iounmap(release);

	pr_info("J2 SMP: requested start of cpu %u\n", cpu);
}

static unsigned int j2_smp_processor_id(void)
{
	return __raw_readl(sh2_cpuid_addr);
}

static void j2_send_ipi(unsigned int cpu, unsigned int message)
{
	volatile unsigned *pmsg;
	unsigned old;
	unsigned long val;

	/* There is only one IPI interrupt shared by all messages, so
	 * we keep a separate interrupt flag per message type in sw. */
	pmsg = &per_cpu(j2_ipi_messages, cpu);
	do old = *pmsg;
	while (cmpxchg(pmsg, old, old|(1U<<message)) != old);

	/* Generate the actual interrupt by writing to CCRn bit 28. */
	val = __raw_readl(j2_ipi_trigger + cpu);
	__raw_writel(val | (1U<<28), j2_ipi_trigger + cpu);
}

static struct plat_smp_ops j2_smp_ops = {
	.smp_setup		= j2_smp_setup,
	.prepare_cpus		= j2_prepare_cpus,
	.start_cpu		= j2_start_cpu,
	.smp_processor_id	= j2_smp_processor_id,
	.send_ipi		= j2_send_ipi,
	.cpu_die		= native_cpu_die,
	.cpu_disable		= native_cpu_disable,
	.play_dead		= native_play_dead,
};

CPU_METHOD_OF_DECLARE(j2_cpu_method, "jcore,spin-table", &j2_smp_ops);
