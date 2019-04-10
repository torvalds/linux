// SPDX-License-Identifier: GPL-2.0
/*
 * ip27-irq.c: Highlevel interrupt handling for IP27 architecture.
 *
 * Copyright (C) 1999, 2000 Ralf Baechle (ralf@gnu.org)
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 1999 - 2001 Kanoj Sarcar
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/bitops.h>

#include <asm/io.h>
#include <asm/irq_cpu.h>
#include <asm/pci/bridge.h>
#include <asm/sn/addrs.h>
#include <asm/sn/agent.h>
#include <asm/sn/arch.h>
#include <asm/sn/hub.h>
#include <asm/sn/intr.h>

struct hub_irq_data {
	struct bridge_controller *bc;
	u64	*irq_mask[2];
	cpuid_t	cpu;
	int	bit;
	int	pin;
};

static DECLARE_BITMAP(hub_irq_map, IP27_HUB_IRQ_COUNT);

static DEFINE_PER_CPU(unsigned long [2], irq_enable_mask);

static inline int alloc_level(void)
{
	int level;

again:
	level = find_first_zero_bit(hub_irq_map, IP27_HUB_IRQ_COUNT);
	if (level >= IP27_HUB_IRQ_COUNT)
		return -ENOSPC;

	if (test_and_set_bit(level, hub_irq_map))
		goto again;

	return level;
}

static void enable_hub_irq(struct irq_data *d)
{
	struct hub_irq_data *hd = irq_data_get_irq_chip_data(d);
	unsigned long *mask = per_cpu(irq_enable_mask, hd->cpu);

	set_bit(hd->bit, mask);
	__raw_writeq(mask[0], hd->irq_mask[0]);
	__raw_writeq(mask[1], hd->irq_mask[1]);
}

static void disable_hub_irq(struct irq_data *d)
{
	struct hub_irq_data *hd = irq_data_get_irq_chip_data(d);
	unsigned long *mask = per_cpu(irq_enable_mask, hd->cpu);

	clear_bit(hd->bit, mask);
	__raw_writeq(mask[0], hd->irq_mask[0]);
	__raw_writeq(mask[1], hd->irq_mask[1]);
}

static unsigned int startup_bridge_irq(struct irq_data *d)
{
	struct hub_irq_data *hd = irq_data_get_irq_chip_data(d);
	struct bridge_controller *bc;
	nasid_t nasid;
	u32 device;
	int pin;

	if (!hd)
		return -EINVAL;

	pin = hd->pin;
	bc = hd->bc;

	nasid = COMPACT_TO_NASID_NODEID(cpu_to_node(hd->cpu));
	bridge_write(bc, b_int_addr[pin].addr,
		     (0x20000 | hd->bit | (nasid << 8)));
	bridge_set(bc, b_int_enable, (1 << pin));
	bridge_set(bc, b_int_enable, 0x7ffffe00); /* more stuff in int_enable */

	/*
	 * Enable sending of an interrupt clear packt to the hub on a high to
	 * low transition of the interrupt pin.
	 *
	 * IRIX sets additional bits in the address which are documented as
	 * reserved in the bridge docs.
	 */
	bridge_set(bc, b_int_mode, (1UL << pin));

	/*
	 * We assume the bridge to have a 1:1 mapping between devices
	 * (slots) and intr pins.
	 */
	device = bridge_read(bc, b_int_device);
	device &= ~(7 << (pin*3));
	device |= (pin << (pin*3));
	bridge_write(bc, b_int_device, device);

	bridge_read(bc, b_wid_tflush);

	enable_hub_irq(d);

	return 0;	/* Never anything pending.  */
}

static void shutdown_bridge_irq(struct irq_data *d)
{
	struct hub_irq_data *hd = irq_data_get_irq_chip_data(d);
	struct bridge_controller *bc;
	int pin = hd->pin;

	if (!hd)
		return;

	disable_hub_irq(d);

	bc = hd->bc;
	bridge_clr(bc, b_int_enable, (1 << pin));
	bridge_read(bc, b_wid_tflush);
}

static void setup_hub_mask(struct hub_irq_data *hd, const struct cpumask *mask)
{
	nasid_t nasid;
	int cpu;

	cpu = cpumask_first_and(mask, cpu_online_mask);
	nasid = COMPACT_TO_NASID_NODEID(cpu_to_node(cpu));
	hd->cpu = cpu;
	if (!cputoslice(cpu)) {
		hd->irq_mask[0] = REMOTE_HUB_PTR(nasid, PI_INT_MASK0_A);
		hd->irq_mask[1] = REMOTE_HUB_PTR(nasid, PI_INT_MASK1_A);
	} else {
		hd->irq_mask[0] = REMOTE_HUB_PTR(nasid, PI_INT_MASK0_B);
		hd->irq_mask[1] = REMOTE_HUB_PTR(nasid, PI_INT_MASK1_B);
	}

	/* Make sure it's not already pending when we connect it. */
	REMOTE_HUB_CLR_INTR(nasid, hd->bit);
}

static int set_affinity_hub_irq(struct irq_data *d, const struct cpumask *mask,
				bool force)
{
	struct hub_irq_data *hd = irq_data_get_irq_chip_data(d);

	if (!hd)
		return -EINVAL;

	if (irqd_is_started(d))
		disable_hub_irq(d);

	setup_hub_mask(hd, mask);

	if (irqd_is_started(d))
		startup_bridge_irq(d);

	irq_data_update_effective_affinity(d, cpumask_of(hd->cpu));

	return 0;
}

static struct irq_chip hub_irq_type = {
	.name		  = "HUB",
	.irq_startup	  = startup_bridge_irq,
	.irq_shutdown	  = shutdown_bridge_irq,
	.irq_mask	  = disable_hub_irq,
	.irq_unmask	  = enable_hub_irq,
	.irq_set_affinity = set_affinity_hub_irq,
};

int request_bridge_irq(struct bridge_controller *bc, int pin)
{
	struct hub_irq_data *hd;
	struct hub_data *hub;
	struct irq_desc *desc;
	int swlevel;
	int irq;

	hd = kzalloc(sizeof(*hd), GFP_KERNEL);
	if (!hd)
		return -ENOMEM;

	swlevel = alloc_level();
	if (unlikely(swlevel < 0)) {
		kfree(hd);
		return -EAGAIN;
	}
	irq = swlevel + IP27_HUB_IRQ_BASE;

	hd->bc = bc;
	hd->bit = swlevel;
	hd->pin = pin;
	irq_set_chip_data(irq, hd);

	/* use CPU connected to nearest hub */
	hub = hub_data(NASID_TO_COMPACT_NODEID(bc->nasid));
	setup_hub_mask(hd, &hub->h_cpus);

	desc = irq_to_desc(irq);
	desc->irq_common_data.node = bc->nasid;
	cpumask_copy(desc->irq_common_data.affinity, &hub->h_cpus);

	return irq;
}

void ip27_hub_irq_init(void)
{
	int i;

	for (i = IP27_HUB_IRQ_BASE;
	     i < (IP27_HUB_IRQ_BASE + IP27_HUB_IRQ_COUNT); i++)
		irq_set_chip_and_handler(i, &hub_irq_type, handle_level_irq);

	/*
	 * Some interrupts are reserved by hardware or by software convention.
	 * Mark these as reserved right away so they won't be used accidentally
	 * later.
	 */
	for (i = 0; i <= BASE_PCI_IRQ; i++)
		set_bit(i, hub_irq_map);

	set_bit(IP_PEND0_6_63, hub_irq_map);

	for (i = NI_BRDCAST_ERR_A; i <= MSC_PANIC_INTR; i++)
		set_bit(i, hub_irq_map);
}

/*
 * This code is unnecessarily complex, because we do
 * intr enabling. Basically, once we grab the set of intrs we need
 * to service, we must mask _all_ these interrupts; firstly, to make
 * sure the same intr does not intr again, causing recursion that
 * can lead to stack overflow. Secondly, we can not just mask the
 * one intr we are do_IRQing, because the non-masked intrs in the
 * first set might intr again, causing multiple servicings of the
 * same intr. This effect is mostly seen for intercpu intrs.
 * Kanoj 05.13.00
 */

static void ip27_do_irq_mask0(struct irq_desc *desc)
{
	cpuid_t cpu = smp_processor_id();
	unsigned long *mask = per_cpu(irq_enable_mask, cpu);
	u64 pend0;

	/* copied from Irix intpend0() */
	pend0 = LOCAL_HUB_L(PI_INT_PEND0);

	pend0 &= mask[0];		/* Pick intrs we should look at */
	if (!pend0)
		return;

#ifdef CONFIG_SMP
	if (pend0 & (1UL << CPU_RESCHED_A_IRQ)) {
		LOCAL_HUB_CLR_INTR(CPU_RESCHED_A_IRQ);
		scheduler_ipi();
	} else if (pend0 & (1UL << CPU_RESCHED_B_IRQ)) {
		LOCAL_HUB_CLR_INTR(CPU_RESCHED_B_IRQ);
		scheduler_ipi();
	} else if (pend0 & (1UL << CPU_CALL_A_IRQ)) {
		LOCAL_HUB_CLR_INTR(CPU_CALL_A_IRQ);
		generic_smp_call_function_interrupt();
	} else if (pend0 & (1UL << CPU_CALL_B_IRQ)) {
		LOCAL_HUB_CLR_INTR(CPU_CALL_B_IRQ);
		generic_smp_call_function_interrupt();
	} else
#endif
		generic_handle_irq(__ffs(pend0) + IP27_HUB_IRQ_BASE);

	LOCAL_HUB_L(PI_INT_PEND0);
}

static void ip27_do_irq_mask1(struct irq_desc *desc)
{
	cpuid_t cpu = smp_processor_id();
	unsigned long *mask = per_cpu(irq_enable_mask, cpu);
	u64 pend1;

	/* copied from Irix intpend0() */
	pend1 = LOCAL_HUB_L(PI_INT_PEND1);

	pend1 &= mask[1];		/* Pick intrs we should look at */
	if (!pend1)
		return;

	generic_handle_irq(__ffs(pend1) + IP27_HUB_IRQ_BASE + 64);

	LOCAL_HUB_L(PI_INT_PEND1);
}

void install_ipi(void)
{
	int cpu = smp_processor_id();
	unsigned long *mask = per_cpu(irq_enable_mask, cpu);
	int slice = LOCAL_HUB_L(PI_CPU_NUM);
	int resched, call;

	resched = CPU_RESCHED_A_IRQ + slice;
	set_bit(resched, mask);
	LOCAL_HUB_CLR_INTR(resched);

	call = CPU_CALL_A_IRQ + slice;
	set_bit(call, mask);
	LOCAL_HUB_CLR_INTR(call);

	if (slice == 0) {
		LOCAL_HUB_S(PI_INT_MASK0_A, mask[0]);
		LOCAL_HUB_S(PI_INT_MASK1_A, mask[1]);
	} else {
		LOCAL_HUB_S(PI_INT_MASK0_B, mask[0]);
		LOCAL_HUB_S(PI_INT_MASK1_B, mask[1]);
	}
}

void __init arch_init_irq(void)
{
	mips_cpu_irq_init();
	ip27_hub_irq_init();

	irq_set_percpu_devid(IP27_HUB_PEND0_IRQ);
	irq_set_chained_handler(IP27_HUB_PEND0_IRQ, ip27_do_irq_mask0);
	irq_set_percpu_devid(IP27_HUB_PEND1_IRQ);
	irq_set_chained_handler(IP27_HUB_PEND1_IRQ, ip27_do_irq_mask1);
}
