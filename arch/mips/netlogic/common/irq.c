/*
 * Copyright 2003-2011 NetLogic Microsystems, Inc. (NetLogic). All rights
 * reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the NetLogic
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETLOGIC ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL NETLOGIC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/irq.h>

#include <linux/irqdomain.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include <asm/errno.h>
#include <asm/signal.h>
#include <asm/ptrace.h>
#include <asm/mipsregs.h>
#include <asm/thread_info.h>

#include <asm/netlogic/mips-extns.h>
#include <asm/netlogic/interrupt.h>
#include <asm/netlogic/haldefs.h>
#include <asm/netlogic/common.h>

#if defined(CONFIG_CPU_XLP)
#include <asm/netlogic/xlp-hal/iomap.h>
#include <asm/netlogic/xlp-hal/xlp.h>
#include <asm/netlogic/xlp-hal/pic.h>
#elif defined(CONFIG_CPU_XLR)
#include <asm/netlogic/xlr/iomap.h>
#include <asm/netlogic/xlr/pic.h>
#include <asm/netlogic/xlr/fmn.h>
#else
#error "Unknown CPU"
#endif

#ifdef CONFIG_SMP
#define SMP_IRQ_MASK	((1ULL << IRQ_IPI_SMP_FUNCTION) | \
				 (1ULL << IRQ_IPI_SMP_RESCHEDULE))
#else
#define SMP_IRQ_MASK	0
#endif
#define PERCPU_IRQ_MASK (SMP_IRQ_MASK | (1ull << IRQ_TIMER) | \
				(1ull << IRQ_FMN))

struct nlm_pic_irq {
	void	(*extra_ack)(struct irq_data *);
	struct	nlm_soc_info *node;
	int	picirq;
	int	irt;
	int	flags;
};

static void xlp_pic_enable(struct irq_data *d)
{
	unsigned long flags;
	struct nlm_pic_irq *pd = irq_data_get_irq_chip_data(d);

	BUG_ON(!pd);
	spin_lock_irqsave(&pd->node->piclock, flags);
	nlm_pic_enable_irt(pd->node->picbase, pd->irt);
	spin_unlock_irqrestore(&pd->node->piclock, flags);
}

static void xlp_pic_disable(struct irq_data *d)
{
	struct nlm_pic_irq *pd = irq_data_get_irq_chip_data(d);
	unsigned long flags;

	BUG_ON(!pd);
	spin_lock_irqsave(&pd->node->piclock, flags);
	nlm_pic_disable_irt(pd->node->picbase, pd->irt);
	spin_unlock_irqrestore(&pd->node->piclock, flags);
}

static void xlp_pic_mask_ack(struct irq_data *d)
{
	struct nlm_pic_irq *pd = irq_data_get_irq_chip_data(d);

	clear_c0_eimr(pd->picirq);
	ack_c0_eirr(pd->picirq);
}

static void xlp_pic_unmask(struct irq_data *d)
{
	struct nlm_pic_irq *pd = irq_data_get_irq_chip_data(d);

	BUG_ON(!pd);

	if (pd->extra_ack)
		pd->extra_ack(d);

	/* re-enable the intr on this cpu */
	set_c0_eimr(pd->picirq);

	/* Ack is a single write, no need to lock */
	nlm_pic_ack(pd->node->picbase, pd->irt);
}

static struct irq_chip xlp_pic = {
	.name		= "XLP-PIC",
	.irq_enable	= xlp_pic_enable,
	.irq_disable	= xlp_pic_disable,
	.irq_mask_ack	= xlp_pic_mask_ack,
	.irq_unmask	= xlp_pic_unmask,
};

static void cpuintr_disable(struct irq_data *d)
{
	clear_c0_eimr(d->irq);
}

static void cpuintr_enable(struct irq_data *d)
{
	set_c0_eimr(d->irq);
}

static void cpuintr_ack(struct irq_data *d)
{
	ack_c0_eirr(d->irq);
}

/*
 * Chip definition for CPU originated interrupts(timer, msg) and
 * IPIs
 */
struct irq_chip nlm_cpu_intr = {
	.name		= "XLP-CPU-INTR",
	.irq_enable	= cpuintr_enable,
	.irq_disable	= cpuintr_disable,
	.irq_mask	= cpuintr_disable,
	.irq_ack	= cpuintr_ack,
	.irq_eoi	= cpuintr_enable,
};

static void __init nlm_init_percpu_irqs(void)
{
	int i;

	for (i = 0; i < PIC_IRT_FIRST_IRQ; i++)
		irq_set_chip_and_handler(i, &nlm_cpu_intr, handle_percpu_irq);
#ifdef CONFIG_SMP
	irq_set_chip_and_handler(IRQ_IPI_SMP_FUNCTION, &nlm_cpu_intr,
			 nlm_smp_function_ipi_handler);
	irq_set_chip_and_handler(IRQ_IPI_SMP_RESCHEDULE, &nlm_cpu_intr,
			 nlm_smp_resched_ipi_handler);
#endif
}


void nlm_setup_pic_irq(int node, int picirq, int irq, int irt)
{
	struct nlm_pic_irq *pic_data;
	int xirq;

	xirq = nlm_irq_to_xirq(node, irq);
	pic_data = kzalloc(sizeof(*pic_data), GFP_KERNEL);
	BUG_ON(pic_data == NULL);
	pic_data->irt = irt;
	pic_data->picirq = picirq;
	pic_data->node = nlm_get_node(node);
	irq_set_chip_and_handler(xirq, &xlp_pic, handle_level_irq);
	irq_set_chip_data(xirq, pic_data);
}

void nlm_set_pic_extra_ack(int node, int irq, void (*xack)(struct irq_data *))
{
	struct nlm_pic_irq *pic_data;
	int xirq;

	xirq = nlm_irq_to_xirq(node, irq);
	pic_data = irq_get_chip_data(xirq);
	if (WARN_ON(!pic_data))
		return;
	pic_data->extra_ack = xack;
}

static void nlm_init_node_irqs(int node)
{
	struct nlm_soc_info *nodep;
	int i, irt;

	pr_info("Init IRQ for node %d\n", node);
	nodep = nlm_get_node(node);
	nodep->irqmask = PERCPU_IRQ_MASK;
	for (i = PIC_IRT_FIRST_IRQ; i <= PIC_IRT_LAST_IRQ; i++) {
		irt = nlm_irq_to_irt(i);
		if (irt == -1)		/* unused irq */
			continue;
		nodep->irqmask |= 1ull << i;
		if (irt == -2)		/* not a direct PIC irq */
			continue;

		nlm_pic_init_irt(nodep->picbase, irt, i,
				node * nlm_threads_per_node(), 0);
		nlm_setup_pic_irq(node, i, i, irt);
	}
}

void nlm_smp_irq_init(int hwtid)
{
	int cpu, node;

	cpu = hwtid % nlm_threads_per_node();
	node = hwtid / nlm_threads_per_node();

	if (cpu == 0 && node != 0)
		nlm_init_node_irqs(node);
	write_c0_eimr(nlm_get_node(node)->irqmask);
}

asmlinkage void plat_irq_dispatch(void)
{
	uint64_t eirr;
	int i, node;

	node = nlm_nodeid();
	eirr = read_c0_eirr_and_eimr();
	if (eirr == 0)
		return;

	i = __ffs64(eirr);
	/* per-CPU IRQs don't need translation */
	if (i < PIC_IRQ_BASE) {
		do_IRQ(i);
		return;
	}

#if defined(CONFIG_PCI_MSI) && defined(CONFIG_CPU_XLP)
	/* PCI interrupts need a second level dispatch for MSI bits */
	if (i >= PIC_PCIE_LINK_MSI_IRQ(0) && i <= PIC_PCIE_LINK_MSI_IRQ(3)) {
		nlm_dispatch_msi(node, i);
		return;
	}
	if (i >= PIC_PCIE_MSIX_IRQ(0) && i <= PIC_PCIE_MSIX_IRQ(3)) {
		nlm_dispatch_msix(node, i);
		return;
	}

#endif
	/* top level irq handling */
	do_IRQ(nlm_irq_to_xirq(node, i));
}

#ifdef CONFIG_CPU_XLP
static int __init xlp_of_pic_init(struct device_node *node,
					struct device_node *parent)
{
	const int n_picirqs = PIC_IRT_LAST_IRQ - PIC_IRQ_BASE + 1;
	struct irq_domain *xlp_pic_domain;
	struct resource res;
	int socid, ret, bus;

	/* we need a hack to get the PIC's SoC chip id */
	ret = of_address_to_resource(node, 0, &res);
	if (ret < 0) {
		pr_err("PIC %pOFn: reg property not found!\n", node);
		return -EINVAL;
	}

	if (cpu_is_xlp9xx()) {
		bus = (res.start >> 20) & 0xf;
		for (socid = 0; socid < NLM_NR_NODES; socid++) {
			if (!nlm_node_present(socid))
				continue;
			if (nlm_get_node(socid)->socbus == bus)
				break;
		}
		if (socid == NLM_NR_NODES) {
			pr_err("PIC %pOFn: Node mapping for bus %d not found!\n",
					node, bus);
			return -EINVAL;
		}
	} else {
		socid = (res.start >> 18) & 0x3;
		if (!nlm_node_present(socid)) {
			pr_err("PIC %pOFn: node %d does not exist!\n",
							node, socid);
			return -EINVAL;
		}
	}

	if (!nlm_node_present(socid)) {
		pr_err("PIC %pOFn: node %d does not exist!\n", node, socid);
		return -EINVAL;
	}

	xlp_pic_domain = irq_domain_add_legacy(node, n_picirqs,
		nlm_irq_to_xirq(socid, PIC_IRQ_BASE), PIC_IRQ_BASE,
		&irq_domain_simple_ops, NULL);
	if (xlp_pic_domain == NULL) {
		pr_err("PIC %pOFn: Creating legacy domain failed!\n", node);
		return -EINVAL;
	}
	pr_info("Node %d: IRQ domain created for PIC@%pR\n", socid, &res);
	return 0;
}

static struct of_device_id __initdata xlp_pic_irq_ids[] = {
	{ .compatible = "netlogic,xlp-pic", .data = xlp_of_pic_init },
	{},
};
#endif

void __init arch_init_irq(void)
{
	/* Initialize the irq descriptors */
	nlm_init_percpu_irqs();
	nlm_init_node_irqs(0);
	write_c0_eimr(nlm_current_node()->irqmask);
#if defined(CONFIG_CPU_XLR)
	nlm_setup_fmn_irq();
#endif
#ifdef CONFIG_CPU_XLP
	of_irq_init(xlp_pic_irq_ids);
#endif
}
