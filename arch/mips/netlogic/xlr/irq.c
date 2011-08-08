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

#include <asm/mipsregs.h>

#include <asm/netlogic/xlr/iomap.h>
#include <asm/netlogic/xlr/pic.h>
#include <asm/netlogic/xlr/xlr.h>

#include <asm/netlogic/interrupt.h>
#include <asm/netlogic/mips-extns.h>

static u64 nlm_irq_mask;
static DEFINE_SPINLOCK(nlm_pic_lock);

static void xlr_pic_enable(struct irq_data *d)
{
	nlm_reg_t *mmio = netlogic_io_mmio(NETLOGIC_IO_PIC_OFFSET);
	unsigned long flags;
	nlm_reg_t reg;
	int irq = d->irq;

	WARN(!PIC_IRQ_IS_IRT(irq), "Bad irq %d", irq);

	spin_lock_irqsave(&nlm_pic_lock, flags);
	reg = netlogic_read_reg(mmio, PIC_IRT_1_BASE + irq - PIC_IRQ_BASE);
	netlogic_write_reg(mmio, PIC_IRT_1_BASE + irq - PIC_IRQ_BASE,
			  reg | (1 << 6) | (1 << 30) | (1 << 31));
	spin_unlock_irqrestore(&nlm_pic_lock, flags);
}

static void xlr_pic_mask(struct irq_data *d)
{
	nlm_reg_t *mmio = netlogic_io_mmio(NETLOGIC_IO_PIC_OFFSET);
	unsigned long flags;
	nlm_reg_t reg;
	int irq = d->irq;

	WARN(!PIC_IRQ_IS_IRT(irq), "Bad irq %d", irq);

	spin_lock_irqsave(&nlm_pic_lock, flags);
	reg = netlogic_read_reg(mmio, PIC_IRT_1_BASE + irq - PIC_IRQ_BASE);
	netlogic_write_reg(mmio, PIC_IRT_1_BASE + irq - PIC_IRQ_BASE,
			  reg | (1 << 6) | (1 << 30) | (0 << 31));
	spin_unlock_irqrestore(&nlm_pic_lock, flags);
}

#ifdef CONFIG_PCI
/* Extra ACK needed for XLR on chip PCI controller */
static void xlr_pci_ack(struct irq_data *d)
{
	nlm_reg_t *pci_mmio = netlogic_io_mmio(NETLOGIC_IO_PCIX_OFFSET);

	netlogic_read_reg(pci_mmio, (0x140 >> 2));
}

/* Extra ACK needed for XLS on chip PCIe controller */
static void xls_pcie_ack(struct irq_data *d)
{
	nlm_reg_t *pcie_mmio_le = netlogic_io_mmio(NETLOGIC_IO_PCIE_1_OFFSET);

	switch (d->irq) {
	case PIC_PCIE_LINK0_IRQ:
		netlogic_write_reg(pcie_mmio_le, (0x90 >> 2), 0xffffffff);
		break;
	case PIC_PCIE_LINK1_IRQ:
		netlogic_write_reg(pcie_mmio_le, (0x94 >> 2), 0xffffffff);
		break;
	case PIC_PCIE_LINK2_IRQ:
		netlogic_write_reg(pcie_mmio_le, (0x190 >> 2), 0xffffffff);
		break;
	case PIC_PCIE_LINK3_IRQ:
		netlogic_write_reg(pcie_mmio_le, (0x194 >> 2), 0xffffffff);
		break;
	}
}

/* For XLS B silicon, the 3,4 PCI interrupts are different */
static void xls_pcie_ack_b(struct irq_data *d)
{
	nlm_reg_t *pcie_mmio_le = netlogic_io_mmio(NETLOGIC_IO_PCIE_1_OFFSET);

	switch (d->irq) {
	case PIC_PCIE_LINK0_IRQ:
		netlogic_write_reg(pcie_mmio_le, (0x90 >> 2), 0xffffffff);
		break;
	case PIC_PCIE_LINK1_IRQ:
		netlogic_write_reg(pcie_mmio_le, (0x94 >> 2), 0xffffffff);
		break;
	case PIC_PCIE_XLSB0_LINK2_IRQ:
		netlogic_write_reg(pcie_mmio_le, (0x190 >> 2), 0xffffffff);
		break;
	case PIC_PCIE_XLSB0_LINK3_IRQ:
		netlogic_write_reg(pcie_mmio_le, (0x194 >> 2), 0xffffffff);
		break;
	}
}
#endif

static void xlr_pic_ack(struct irq_data *d)
{
	unsigned long flags;
	nlm_reg_t *mmio;
	int irq = d->irq;
	void *hd = irq_data_get_irq_handler_data(d);

	WARN(!PIC_IRQ_IS_IRT(irq), "Bad irq %d", irq);

	if (hd) {
		void (*extra_ack)(void *) = hd;
		extra_ack(d);
	}
	mmio = netlogic_io_mmio(NETLOGIC_IO_PIC_OFFSET);
	spin_lock_irqsave(&nlm_pic_lock, flags);
	netlogic_write_reg(mmio, PIC_INT_ACK, (1 << (irq - PIC_IRQ_BASE)));
	spin_unlock_irqrestore(&nlm_pic_lock, flags);
}

/*
 * This chip definition handles interrupts routed thru the XLR
 * hardware PIC, currently IRQs 8-39 are mapped to hardware intr
 * 0-31 wired the XLR PIC
 */
static struct irq_chip xlr_pic = {
	.name		= "XLR-PIC",
	.irq_enable	= xlr_pic_enable,
	.irq_mask	= xlr_pic_mask,
	.irq_ack	= xlr_pic_ack,
};

static void rsvd_irq_handler(struct irq_data *d)
{
	WARN(d->irq >= PIC_IRQ_BASE, "Bad irq %d", d->irq);
}

/*
 * Chip definition for CPU originated interrupts(timer, msg) and
 * IPIs
 */
struct irq_chip nlm_cpu_intr = {
	.name		= "XLR-CPU-INTR",
	.irq_enable	= rsvd_irq_handler,
	.irq_mask	= rsvd_irq_handler,
	.irq_ack	= rsvd_irq_handler,
};

void __init init_xlr_irqs(void)
{
	nlm_reg_t *mmio = netlogic_io_mmio(NETLOGIC_IO_PIC_OFFSET);
	uint32_t thread_mask = 1;
	int level, i;

	pr_info("Interrupt thread mask [%x]\n", thread_mask);
	for (i = 0; i < PIC_NUM_IRTS; i++) {
		level = PIC_IRQ_IS_EDGE_TRIGGERED(i);

		/* Bind all PIC irqs to boot cpu */
		netlogic_write_reg(mmio, PIC_IRT_0_BASE + i, thread_mask);

		/*
		 * Use local scheduling and high polarity for all IRTs
		 * Invalidate all IRTs, by default
		 */
		netlogic_write_reg(mmio, PIC_IRT_1_BASE + i,
				(level << 30) | (1 << 6) | (PIC_IRQ_BASE + i));
	}

	/* Make all IRQs as level triggered by default */
	for (i = 0; i < NR_IRQS; i++) {
		if (PIC_IRQ_IS_IRT(i))
			irq_set_chip_and_handler(i, &xlr_pic, handle_level_irq);
		else
			irq_set_chip_and_handler(i, &nlm_cpu_intr,
						handle_percpu_irq);
	}
#ifdef CONFIG_SMP
	irq_set_chip_and_handler(IRQ_IPI_SMP_FUNCTION, &nlm_cpu_intr,
			 nlm_smp_function_ipi_handler);
	irq_set_chip_and_handler(IRQ_IPI_SMP_RESCHEDULE, &nlm_cpu_intr,
			 nlm_smp_resched_ipi_handler);
	nlm_irq_mask |=
	    ((1ULL << IRQ_IPI_SMP_FUNCTION) | (1ULL << IRQ_IPI_SMP_RESCHEDULE));
#endif

#ifdef CONFIG_PCI
	/*
	 * For PCI interrupts, we need to ack the PIC controller too, overload
	 * irq handler data to do this
	 */
	if (nlm_chip_is_xls()) {
		if (nlm_chip_is_xls_b()) {
			irq_set_handler_data(PIC_PCIE_LINK0_IRQ,
							xls_pcie_ack_b);
			irq_set_handler_data(PIC_PCIE_LINK1_IRQ,
							xls_pcie_ack_b);
			irq_set_handler_data(PIC_PCIE_XLSB0_LINK2_IRQ,
							xls_pcie_ack_b);
			irq_set_handler_data(PIC_PCIE_XLSB0_LINK3_IRQ,
							xls_pcie_ack_b);
		} else {
			irq_set_handler_data(PIC_PCIE_LINK0_IRQ, xls_pcie_ack);
			irq_set_handler_data(PIC_PCIE_LINK1_IRQ, xls_pcie_ack);
			irq_set_handler_data(PIC_PCIE_LINK2_IRQ, xls_pcie_ack);
			irq_set_handler_data(PIC_PCIE_LINK3_IRQ, xls_pcie_ack);
		}
	} else {
		/* XLR PCI controller ACK */
		irq_set_handler_data(PIC_PCIE_XLSB0_LINK3_IRQ, xlr_pci_ack);
	}
#endif
	/* unmask all PIC related interrupts. If no handler is installed by the
	 * drivers, it'll just ack the interrupt and return
	 */
	for (i = PIC_IRT_FIRST_IRQ; i <= PIC_IRT_LAST_IRQ; i++)
		nlm_irq_mask |= (1ULL << i);

	nlm_irq_mask |= (1ULL << IRQ_TIMER);
}

void __init arch_init_irq(void)
{
	/* Initialize the irq descriptors */
	init_xlr_irqs();
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
	if (!eirr)
		return;

	/* no need of EIRR here, writing compare clears interrupt */
	if (eirr & (1 << IRQ_TIMER)) {
		do_IRQ(IRQ_TIMER);
		return;
	}

	/* use dcltz: optimize below code */
	for (i = 63; i != -1; i--) {
		if (eirr & (1ULL << i))
			break;
	}
	if (i == -1) {
		pr_err("no interrupt !!\n");
		return;
	}

	/* Ack eirr */
	write_c0_eirr(1ULL << i);

	do_IRQ(i);
}
