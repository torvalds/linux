/*
 * Copyright (C) 2006,2007 Felix Fietkau <nbd@openwrt.org>
 * Copyright (C) 2006,2007 Eugene Konev <ejka@openwrt.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>

#include <asm/irq_cpu.h>
#include <asm/mipsregs.h>
#include <asm/mach-ar7/ar7.h>

#define EXCEPT_OFFSET	0x80
#define PACE_OFFSET	0xA0
#define CHNLS_OFFSET	0x200

#define REG_OFFSET(irq, reg)	((irq) / 32 * 0x4 + reg * 0x10)
#define SEC_REG_OFFSET(reg)	(EXCEPT_OFFSET + reg * 0x8)
#define SEC_SR_OFFSET		(SEC_REG_OFFSET(0))	/* 0x80 */
#define CR_OFFSET(irq)		(REG_OFFSET(irq, 1))	/* 0x10 */
#define SEC_CR_OFFSET		(SEC_REG_OFFSET(1))	/* 0x88 */
#define ESR_OFFSET(irq)		(REG_OFFSET(irq, 2))	/* 0x20 */
#define SEC_ESR_OFFSET		(SEC_REG_OFFSET(2))	/* 0x90 */
#define ECR_OFFSET(irq)		(REG_OFFSET(irq, 3))	/* 0x30 */
#define SEC_ECR_OFFSET		(SEC_REG_OFFSET(3))	/* 0x98 */
#define PIR_OFFSET		(0x40)
#define MSR_OFFSET		(0x44)
#define PM_OFFSET(irq)		(REG_OFFSET(irq, 5))	/* 0x50 */
#define TM_OFFSET(irq)		(REG_OFFSET(irq, 6))	/* 0x60 */

#define REG(addr) ((u32 *)(KSEG1ADDR(AR7_REGS_IRQ) + addr))

#define CHNL_OFFSET(chnl) (CHNLS_OFFSET + (chnl * 4))

static int ar7_irq_base;

static void ar7_unmask_irq(unsigned int irq)
{
	writel(1 << ((irq - ar7_irq_base) % 32),
	       REG(ESR_OFFSET(irq - ar7_irq_base)));
}

static void ar7_mask_irq(unsigned int irq)
{
	writel(1 << ((irq - ar7_irq_base) % 32),
	       REG(ECR_OFFSET(irq - ar7_irq_base)));
}

static void ar7_ack_irq(unsigned int irq)
{
	writel(1 << ((irq - ar7_irq_base) % 32),
	       REG(CR_OFFSET(irq - ar7_irq_base)));
}

static void ar7_unmask_sec_irq(unsigned int irq)
{
	writel(1 << (irq - ar7_irq_base - 40), REG(SEC_ESR_OFFSET));
}

static void ar7_mask_sec_irq(unsigned int irq)
{
	writel(1 << (irq - ar7_irq_base - 40), REG(SEC_ECR_OFFSET));
}

static void ar7_ack_sec_irq(unsigned int irq)
{
	writel(1 << (irq - ar7_irq_base - 40), REG(SEC_CR_OFFSET));
}

static struct irq_chip ar7_irq_type = {
	.name = "AR7",
	.unmask = ar7_unmask_irq,
	.mask = ar7_mask_irq,
	.ack = ar7_ack_irq
};

static struct irq_chip ar7_sec_irq_type = {
	.name = "AR7",
	.unmask = ar7_unmask_sec_irq,
	.mask = ar7_mask_sec_irq,
	.ack = ar7_ack_sec_irq,
};

static struct irqaction ar7_cascade_action = {
	.handler = no_action,
	.name = "AR7 cascade interrupt"
};

static void __init ar7_irq_init(int base)
{
	int i;
	/*
	 * Disable interrupts and clear pending
	 */
	writel(0xffffffff, REG(ECR_OFFSET(0)));
	writel(0xff, REG(ECR_OFFSET(32)));
	writel(0xffffffff, REG(SEC_ECR_OFFSET));
	writel(0xffffffff, REG(CR_OFFSET(0)));
	writel(0xff, REG(CR_OFFSET(32)));
	writel(0xffffffff, REG(SEC_CR_OFFSET));

	ar7_irq_base = base;

	for (i = 0; i < 40; i++) {
		writel(i, REG(CHNL_OFFSET(i)));
		/* Primary IRQ's */
		set_irq_chip_and_handler(base + i, &ar7_irq_type,
					 handle_level_irq);
		/* Secondary IRQ's */
		if (i < 32)
			set_irq_chip_and_handler(base + i + 40,
						 &ar7_sec_irq_type,
						 handle_level_irq);
	}

	setup_irq(2, &ar7_cascade_action);
	setup_irq(ar7_irq_base, &ar7_cascade_action);
	set_c0_status(IE_IRQ0);
}

void __init arch_init_irq(void)
{
	mips_cpu_irq_init();
	ar7_irq_init(8);
}

static void ar7_cascade(void)
{
	u32 status;
	int i, irq;

	/* Primary IRQ's */
	irq = readl(REG(PIR_OFFSET)) & 0x3f;
	if (irq) {
		do_IRQ(ar7_irq_base + irq);
		return;
	}

	/* Secondary IRQ's are cascaded through primary '0' */
	writel(1, REG(CR_OFFSET(irq)));
	status = readl(REG(SEC_SR_OFFSET));
	for (i = 0; i < 32; i++) {
		if (status & 1) {
			do_IRQ(ar7_irq_base + i + 40);
			return;
		}
		status >>= 1;
	}

	spurious_interrupt();
}

asmlinkage void plat_irq_dispatch(void)
{
	unsigned int pending = read_c0_status() & read_c0_cause() & ST0_IM;
	if (pending & STATUSF_IP7)		/* cpu timer */
		do_IRQ(7);
	else if (pending & STATUSF_IP2)		/* int0 hardware line */
		ar7_cascade();
	else
		spurious_interrupt();
}
