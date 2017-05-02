/*
 * ARC ARConnect (MultiCore IP) support (formerly known as MCIP)
 *
 * Copyright (C) 2013 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/smp.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/spinlock.h>
#include <soc/arc/mcip.h>
#include <asm/irqflags-arcv2.h>
#include <asm/setup.h>

static DEFINE_RAW_SPINLOCK(mcip_lock);

#ifdef CONFIG_SMP

static char smp_cpuinfo_buf[128];

static void mcip_setup_per_cpu(int cpu)
{
	smp_ipi_irq_setup(cpu, IPI_IRQ);
	smp_ipi_irq_setup(cpu, SOFTIRQ_IRQ);
}

static void mcip_ipi_send(int cpu)
{
	unsigned long flags;
	int ipi_was_pending;

	/* ARConnect can only send IPI to others */
	if (unlikely(cpu == raw_smp_processor_id())) {
		arc_softirq_trigger(SOFTIRQ_IRQ);
		return;
	}

	raw_spin_lock_irqsave(&mcip_lock, flags);

	/*
	 * If receiver already has a pending interrupt, elide sending this one.
	 * Linux cross core calling works well with concurrent IPIs
	 * coalesced into one
	 * see arch/arc/kernel/smp.c: ipi_send_msg_one()
	 */
	__mcip_cmd(CMD_INTRPT_READ_STATUS, cpu);
	ipi_was_pending = read_aux_reg(ARC_REG_MCIP_READBACK);
	if (!ipi_was_pending)
		__mcip_cmd(CMD_INTRPT_GENERATE_IRQ, cpu);

	raw_spin_unlock_irqrestore(&mcip_lock, flags);
}

static void mcip_ipi_clear(int irq)
{
	unsigned int cpu, c;
	unsigned long flags;

	if (unlikely(irq == SOFTIRQ_IRQ)) {
		arc_softirq_clear(irq);
		return;
	}

	raw_spin_lock_irqsave(&mcip_lock, flags);

	/* Who sent the IPI */
	__mcip_cmd(CMD_INTRPT_CHECK_SOURCE, 0);

	cpu = read_aux_reg(ARC_REG_MCIP_READBACK);	/* 1,2,4,8... */

	/*
	 * In rare case, multiple concurrent IPIs sent to same target can
	 * possibly be coalesced by MCIP into 1 asserted IRQ, so @cpus can be
	 * "vectored" (multiple bits sets) as opposed to typical single bit
	 */
	do {
		c = __ffs(cpu);			/* 0,1,2,3 */
		__mcip_cmd(CMD_INTRPT_GENERATE_ACK, c);
		cpu &= ~(1U << c);
	} while (cpu);

	raw_spin_unlock_irqrestore(&mcip_lock, flags);
}

static void mcip_probe_n_setup(void)
{
	struct mcip_bcr mp;

	READ_BCR(ARC_REG_MCIP_BCR, mp);

	sprintf(smp_cpuinfo_buf,
		"Extn [SMP]\t: ARConnect (v%d): %d cores with %s%s%s%s\n",
		mp.ver, mp.num_cores,
		IS_AVAIL1(mp.ipi, "IPI "),
		IS_AVAIL1(mp.idu, "IDU "),
		IS_AVAIL1(mp.dbg, "DEBUG "),
		IS_AVAIL1(mp.gfrc, "GFRC"));

	cpuinfo_arc700[0].extn.gfrc = mp.gfrc;

	if (mp.dbg) {
		__mcip_cmd_data(CMD_DEBUG_SET_SELECT, 0, 0xf);
		__mcip_cmd_data(CMD_DEBUG_SET_MASK, 0xf, 0xf);
	}
}

struct plat_smp_ops plat_smp_ops = {
	.info		= smp_cpuinfo_buf,
	.init_early_smp	= mcip_probe_n_setup,
	.init_per_cpu	= mcip_setup_per_cpu,
	.ipi_send	= mcip_ipi_send,
	.ipi_clear	= mcip_ipi_clear,
};

#endif

/***************************************************************************
 * ARCv2 Interrupt Distribution Unit (IDU)
 *
 * Connects external "COMMON" IRQs to core intc, providing:
 *  -dynamic routing (IRQ affinity)
 *  -load balancing (Round Robin interrupt distribution)
 *  -1:N distribution
 *
 * It physically resides in the MCIP hw block
 */

#include <linux/irqchip.h>
#include <linux/of.h>
#include <linux/of_irq.h>

/*
 * Set the DEST for @cmn_irq to @cpu_mask (1 bit per core)
 */
static void idu_set_dest(unsigned int cmn_irq, unsigned int cpu_mask)
{
	__mcip_cmd_data(CMD_IDU_SET_DEST, cmn_irq, cpu_mask);
}

static void idu_set_mode(unsigned int cmn_irq, unsigned int lvl,
			   unsigned int distr)
{
	union {
		unsigned int word;
		struct {
			unsigned int distr:2, pad:2, lvl:1, pad2:27;
		};
	} data;

	data.distr = distr;
	data.lvl = lvl;
	__mcip_cmd_data(CMD_IDU_SET_MODE, cmn_irq, data.word);
}

static void idu_irq_mask_raw(irq_hw_number_t hwirq)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&mcip_lock, flags);
	__mcip_cmd_data(CMD_IDU_SET_MASK, hwirq, 1);
	raw_spin_unlock_irqrestore(&mcip_lock, flags);
}

static void idu_irq_mask(struct irq_data *data)
{
	idu_irq_mask_raw(data->hwirq);
}

static void idu_irq_unmask(struct irq_data *data)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&mcip_lock, flags);
	__mcip_cmd_data(CMD_IDU_SET_MASK, data->hwirq, 0);
	raw_spin_unlock_irqrestore(&mcip_lock, flags);
}

static int
idu_irq_set_affinity(struct irq_data *data, const struct cpumask *cpumask,
		     bool force)
{
	unsigned long flags;
	cpumask_t online;
	unsigned int destination_bits;
	unsigned int distribution_mode;

	/* errout if no online cpu per @cpumask */
	if (!cpumask_and(&online, cpumask, cpu_online_mask))
		return -EINVAL;

	raw_spin_lock_irqsave(&mcip_lock, flags);

	destination_bits = cpumask_bits(&online)[0];
	idu_set_dest(data->hwirq, destination_bits);

	if (ffs(destination_bits) == fls(destination_bits))
		distribution_mode = IDU_M_DISTRI_DEST;
	else
		distribution_mode = IDU_M_DISTRI_RR;

	idu_set_mode(data->hwirq, IDU_M_TRIG_LEVEL, distribution_mode);

	raw_spin_unlock_irqrestore(&mcip_lock, flags);

	return IRQ_SET_MASK_OK;
}

static void idu_irq_enable(struct irq_data *data)
{
	/*
	 * By default send all common interrupts to all available online CPUs.
	 * The affinity of common interrupts in IDU must be set manually since
	 * in some cases the kernel will not call irq_set_affinity() by itself:
	 *   1. When the kernel is not configured with support of SMP.
	 *   2. When the kernel is configured with support of SMP but upper
	 *      interrupt controllers does not support setting of the affinity
	 *      and cannot propagate it to IDU.
	 */
	idu_irq_set_affinity(data, cpu_online_mask, false);
	idu_irq_unmask(data);
}

static struct irq_chip idu_irq_chip = {
	.name			= "MCIP IDU Intc",
	.irq_mask		= idu_irq_mask,
	.irq_unmask		= idu_irq_unmask,
	.irq_enable		= idu_irq_enable,
#ifdef CONFIG_SMP
	.irq_set_affinity       = idu_irq_set_affinity,
#endif

};

static void idu_cascade_isr(struct irq_desc *desc)
{
	struct irq_domain *idu_domain = irq_desc_get_handler_data(desc);
	struct irq_chip *core_chip = irq_desc_get_chip(desc);
	irq_hw_number_t core_hwirq = irqd_to_hwirq(irq_desc_get_irq_data(desc));
	irq_hw_number_t idu_hwirq = core_hwirq - FIRST_EXT_IRQ;

	chained_irq_enter(core_chip, desc);
	generic_handle_irq(irq_find_mapping(idu_domain, idu_hwirq));
	chained_irq_exit(core_chip, desc);
}

static int idu_irq_map(struct irq_domain *d, unsigned int virq, irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(virq, &idu_irq_chip, handle_level_irq);
	irq_set_status_flags(virq, IRQ_MOVE_PCNTXT);

	return 0;
}

static const struct irq_domain_ops idu_irq_ops = {
	.xlate	= irq_domain_xlate_onecell,
	.map	= idu_irq_map,
};

/*
 * [16, 23]: Statically assigned always private-per-core (Timers, WDT, IPI)
 * [24, 23+C]: If C > 0 then "C" common IRQs
 * [24+C, N]: Not statically assigned, private-per-core
 */


static int __init
idu_of_init(struct device_node *intc, struct device_node *parent)
{
	struct irq_domain *domain;
	int nr_irqs;
	int i, virq;
	struct mcip_bcr mp;
	struct mcip_idu_bcr idu_bcr;

	READ_BCR(ARC_REG_MCIP_BCR, mp);

	if (!mp.idu)
		panic("IDU not detected, but DeviceTree using it");

	READ_BCR(ARC_REG_MCIP_IDU_BCR, idu_bcr);
	nr_irqs = mcip_idu_bcr_to_nr_irqs(idu_bcr);

	pr_info("MCIP: IDU supports %u common irqs\n", nr_irqs);

	domain = irq_domain_add_linear(intc, nr_irqs, &idu_irq_ops, NULL);

	/* Parent interrupts (core-intc) are already mapped */

	for (i = 0; i < nr_irqs; i++) {
		/* Mask all common interrupts by default */
		idu_irq_mask_raw(i);

		/*
		 * Return parent uplink IRQs (towards core intc) 24,25,.....
		 * this step has been done before already
		 * however we need it to get the parent virq and set IDU handler
		 * as first level isr
		 */
		virq = irq_create_mapping(NULL, i + FIRST_EXT_IRQ);
		BUG_ON(!virq);
		irq_set_chained_handler_and_data(virq, idu_cascade_isr, domain);
	}

	__mcip_cmd(CMD_IDU_ENABLE, 0);

	return 0;
}
IRQCHIP_DECLARE(arcv2_idu_intc, "snps,archs-idu-intc", idu_of_init);
