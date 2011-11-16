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
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/irq.h>

#include <asm/errno.h>
#include <asm/signal.h>
#include <asm/system.h>
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
#else
#error "Unknown CPU"
#endif
/*
 * These are the routines that handle all the low level interrupt stuff.
 * Actions handled here are: initialization of the interrupt map, requesting of
 * interrupt lines by handlers, dispatching if interrupts to handlers, probing
 * for interrupt lines
 */

/* Globals */
static uint64_t nlm_irq_mask;
static DEFINE_SPINLOCK(nlm_pic_lock);

static void xlp_pic_enable(struct irq_data *d)
{
	unsigned long flags;
	int irt;

	irt = nlm_irq_to_irt(d->irq);
	if (irt == -1)
		return;
	spin_lock_irqsave(&nlm_pic_lock, flags);
	nlm_pic_enable_irt(nlm_pic_base, irt);
	spin_unlock_irqrestore(&nlm_pic_lock, flags);
}

static void xlp_pic_disable(struct irq_data *d)
{
	unsigned long flags;
	int irt;

	irt = nlm_irq_to_irt(d->irq);
	if (irt == -1)
		return;
	spin_lock_irqsave(&nlm_pic_lock, flags);
	nlm_pic_disable_irt(nlm_pic_base, irt);
	spin_unlock_irqrestore(&nlm_pic_lock, flags);
}

static void xlp_pic_mask_ack(struct irq_data *d)
{
	uint64_t mask = 1ull << d->irq;

	write_c0_eirr(mask);            /* ack by writing EIRR */
}

static void xlp_pic_unmask(struct irq_data *d)
{
	void *hd = irq_data_get_irq_handler_data(d);
	int irt;

	irt = nlm_irq_to_irt(d->irq);
	if (irt == -1)
		return;

	if (hd) {
		void (*extra_ack)(void *) = hd;
		extra_ack(d);
	}
	/* Ack is a single write, no need to lock */
	nlm_pic_ack(nlm_pic_base, irt);
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
	uint64_t eimr;
	uint64_t mask = 1ull << d->irq;

	eimr = read_c0_eimr();
	write_c0_eimr(eimr & ~mask);
}

static void cpuintr_enable(struct irq_data *d)
{
	uint64_t eimr;
	uint64_t mask = 1ull << d->irq;

	eimr = read_c0_eimr();
	write_c0_eimr(eimr | mask);
}

static void cpuintr_ack(struct irq_data *d)
{
	uint64_t mask = 1ull << d->irq;

	write_c0_eirr(mask);
}

static void cpuintr_nop(struct irq_data *d)
{
	WARN(d->irq >= PIC_IRQ_BASE, "Bad irq %d", d->irq);
}

/*
 * Chip definition for CPU originated interrupts(timer, msg) and
 * IPIs
 */
struct irq_chip nlm_cpu_intr = {
	.name		= "XLP-CPU-INTR",
	.irq_enable	= cpuintr_enable,
	.irq_disable	= cpuintr_disable,
	.irq_mask	= cpuintr_nop,
	.irq_ack	= cpuintr_nop,
	.irq_eoi	= cpuintr_ack,
};

void __init init_nlm_common_irqs(void)
{
	int i, irq, irt;

	for (i = 0; i < PIC_IRT_FIRST_IRQ; i++)
		irq_set_chip_and_handler(i, &nlm_cpu_intr, handle_percpu_irq);

	for (i = PIC_IRT_FIRST_IRQ; i <= PIC_IRT_LAST_IRQ ; i++)
		irq_set_chip_and_handler(i, &xlp_pic, handle_level_irq);

#ifdef CONFIG_SMP
	irq_set_chip_and_handler(IRQ_IPI_SMP_FUNCTION, &nlm_cpu_intr,
			 nlm_smp_function_ipi_handler);
	irq_set_chip_and_handler(IRQ_IPI_SMP_RESCHEDULE, &nlm_cpu_intr,
			 nlm_smp_resched_ipi_handler);
	nlm_irq_mask |=
	    ((1ULL << IRQ_IPI_SMP_FUNCTION) | (1ULL << IRQ_IPI_SMP_RESCHEDULE));
#endif

	for (irq = PIC_IRT_FIRST_IRQ; irq <= PIC_IRT_LAST_IRQ; irq++) {
		irt = nlm_irq_to_irt(irq);
		if (irt == -1)
			continue;
		nlm_irq_mask |= (1ULL << irq);
		nlm_pic_init_irt(nlm_pic_base, irt, irq, 0);
	}

	nlm_irq_mask |= (1ULL << IRQ_TIMER);
}

void __init arch_init_irq(void)
{
	/* Initialize the irq descriptors */
	init_nlm_common_irqs();

	write_c0_eimr(nlm_irq_mask);
}

void __cpuinit nlm_smp_irq_init(void)
{
	/* set interrupt mask for non-zero cpus */
	write_c0_eimr(nlm_irq_mask);
}

asmlinkage void plat_irq_dispatch(void)
{
	uint64_t eirr;
	int i;

	eirr = read_c0_eirr() & read_c0_eimr();
	if (eirr & (1 << IRQ_TIMER)) {
		do_IRQ(IRQ_TIMER);
		return;
	}

	i = __ilog2_u64(eirr);
	if (i == -1)
		return;

	do_IRQ(i);
}
