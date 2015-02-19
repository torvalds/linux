/*
 * arch/arm/mach-iop13xx/msi.c
 *
 * PCI MSI support for the iop13xx processor
 *
 * Copyright (c) 2006, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 */
#include <linux/pci.h>
#include <linux/msi.h>
#include <asm/mach/irq.h>
#include <asm/irq.h>
#include <mach/irqs.h>

/* IMIPR0 CP6 R8 Page 1
 */
static u32 read_imipr_0(void)
{
	u32 val;
	asm volatile("mrc p6, 0, %0, c8, c1, 0":"=r" (val));
	return val;
}
static void write_imipr_0(u32 val)
{
	asm volatile("mcr p6, 0, %0, c8, c1, 0"::"r" (val));
}

/* IMIPR1 CP6 R9 Page 1
 */
static u32 read_imipr_1(void)
{
	u32 val;
	asm volatile("mrc p6, 0, %0, c9, c1, 0":"=r" (val));
	return val;
}
static void write_imipr_1(u32 val)
{
	asm volatile("mcr p6, 0, %0, c9, c1, 0"::"r" (val));
}

/* IMIPR2 CP6 R10 Page 1
 */
static u32 read_imipr_2(void)
{
	u32 val;
	asm volatile("mrc p6, 0, %0, c10, c1, 0":"=r" (val));
	return val;
}
static void write_imipr_2(u32 val)
{
	asm volatile("mcr p6, 0, %0, c10, c1, 0"::"r" (val));
}

/* IMIPR3 CP6 R11 Page 1
 */
static u32 read_imipr_3(void)
{
	u32 val;
	asm volatile("mrc p6, 0, %0, c11, c1, 0":"=r" (val));
	return val;
}
static void write_imipr_3(u32 val)
{
	asm volatile("mcr p6, 0, %0, c11, c1, 0"::"r" (val));
}

static u32 (*read_imipr[])(void) = {
	read_imipr_0,
	read_imipr_1,
	read_imipr_2,
	read_imipr_3,
};

static void (*write_imipr[])(u32) = {
	write_imipr_0,
	write_imipr_1,
	write_imipr_2,
	write_imipr_3,
};

static void iop13xx_msi_handler(unsigned int irq, struct irq_desc *desc)
{
	int i, j;
	unsigned long status;

	/* read IMIPR registers and find any active interrupts,
	 * then call ISR for each active interrupt
	 */
	for (i = 0; i < ARRAY_SIZE(read_imipr); i++) {
		status = (read_imipr[i])();
		if (!status)
			continue;

		do {
			j = find_first_bit(&status, 32);
			(write_imipr[i])(1 << j); /* write back to clear bit */
			generic_handle_irq(IRQ_IOP13XX_MSI_0 + j + (32*i));
			status = (read_imipr[i])();
		} while (status);
	}
}

void __init iop13xx_msi_init(void)
{
	irq_set_chained_handler(IRQ_IOP13XX_INBD_MSI, iop13xx_msi_handler);
}

static void iop13xx_msi_nop(struct irq_data *d)
{
	return;
}

static struct irq_chip iop13xx_msi_chip = {
	.name = "PCI-MSI",
	.irq_ack = iop13xx_msi_nop,
	.irq_enable = pci_msi_unmask_irq,
	.irq_disable = pci_msi_mask_irq,
	.irq_mask = pci_msi_mask_irq,
	.irq_unmask = pci_msi_unmask_irq,
};

int arch_setup_msi_irq(struct pci_dev *pdev, struct msi_desc *desc)
{
	int id, irq = irq_alloc_desc_from(IRQ_IOP13XX_MSI_0, -1);
	struct msi_msg msg;

	if (irq < 0)
		return irq;

	if (irq >= NR_IOP13XX_IRQS) {
		irq_free_desc(irq);
		return -ENOSPC;
	}

	irq_set_msi_desc(irq, desc);

	msg.address_hi = 0x0;
	msg.address_lo = IOP13XX_MU_MIMR_PCI;

	id = iop13xx_cpu_id();
	msg.data = (id << IOP13XX_MU_MIMR_CORE_SELECT) | (irq & 0x7f);

	pci_write_msi_msg(irq, &msg);
	irq_set_chip_and_handler(irq, &iop13xx_msi_chip, handle_simple_irq);

	return 0;
}

void arch_teardown_msi_irq(unsigned int irq)
{
	irq_free_desc(irq);
}
