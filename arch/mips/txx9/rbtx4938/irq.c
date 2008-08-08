/*
 * Toshiba RBTX4938 specific interrupt handlers
 * Copyright (C) 2000-2001 Toshiba Corporation
 *
 * 2003-2005 (c) MontaVista Software, Inc. This file is licensed under the
 * terms of the GNU General Public License version 2. This program is
 * licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * Support for TX4938 in 2.6 - Manish Lachwani (mlachwani@mvista.com)
 */

/*
 * MIPS_CPU_IRQ_BASE+00 Software 0
 * MIPS_CPU_IRQ_BASE+01 Software 1
 * MIPS_CPU_IRQ_BASE+02 Cascade TX4938-CP0
 * MIPS_CPU_IRQ_BASE+03 Multiplexed -- do not use
 * MIPS_CPU_IRQ_BASE+04 Multiplexed -- do not use
 * MIPS_CPU_IRQ_BASE+05 Multiplexed -- do not use
 * MIPS_CPU_IRQ_BASE+06 Multiplexed -- do not use
 * MIPS_CPU_IRQ_BASE+07 CPU TIMER
 *
 * TXX9_IRQ_BASE+00
 * TXX9_IRQ_BASE+01
 * TXX9_IRQ_BASE+02 Cascade RBTX4938-IOC
 * TXX9_IRQ_BASE+03 RBTX4938 RTL-8019AS Ethernet
 * TXX9_IRQ_BASE+04
 * TXX9_IRQ_BASE+05 TX4938 ETH1
 * TXX9_IRQ_BASE+06 TX4938 ETH0
 * TXX9_IRQ_BASE+07
 * TXX9_IRQ_BASE+08 TX4938 SIO 0
 * TXX9_IRQ_BASE+09 TX4938 SIO 1
 * TXX9_IRQ_BASE+10 TX4938 DMA0
 * TXX9_IRQ_BASE+11 TX4938 DMA1
 * TXX9_IRQ_BASE+12 TX4938 DMA2
 * TXX9_IRQ_BASE+13 TX4938 DMA3
 * TXX9_IRQ_BASE+14
 * TXX9_IRQ_BASE+15
 * TXX9_IRQ_BASE+16 TX4938 PCIC
 * TXX9_IRQ_BASE+17 TX4938 TMR0
 * TXX9_IRQ_BASE+18 TX4938 TMR1
 * TXX9_IRQ_BASE+19 TX4938 TMR2
 * TXX9_IRQ_BASE+20
 * TXX9_IRQ_BASE+21
 * TXX9_IRQ_BASE+22 TX4938 PCIERR
 * TXX9_IRQ_BASE+23
 * TXX9_IRQ_BASE+24
 * TXX9_IRQ_BASE+25
 * TXX9_IRQ_BASE+26
 * TXX9_IRQ_BASE+27
 * TXX9_IRQ_BASE+28
 * TXX9_IRQ_BASE+29
 * TXX9_IRQ_BASE+30
 * TXX9_IRQ_BASE+31 TX4938 SPI
 *
 * RBTX4938_IRQ_IOC+00 PCI-D
 * RBTX4938_IRQ_IOC+01 PCI-C
 * RBTX4938_IRQ_IOC+02 PCI-B
 * RBTX4938_IRQ_IOC+03 PCI-A
 * RBTX4938_IRQ_IOC+04 RTC
 * RBTX4938_IRQ_IOC+05 ATA
 * RBTX4938_IRQ_IOC+06 MODEM
 * RBTX4938_IRQ_IOC+07 SWINT
 */
#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/mipsregs.h>
#include <asm/txx9/generic.h>
#include <asm/txx9/rbtx4938.h>

static void toshiba_rbtx4938_irq_ioc_enable(unsigned int irq);
static void toshiba_rbtx4938_irq_ioc_disable(unsigned int irq);

#define TOSHIBA_RBTX4938_IOC_NAME "RBTX4938-IOC"
static struct irq_chip toshiba_rbtx4938_irq_ioc_type = {
	.name = TOSHIBA_RBTX4938_IOC_NAME,
	.ack = toshiba_rbtx4938_irq_ioc_disable,
	.mask = toshiba_rbtx4938_irq_ioc_disable,
	.mask_ack = toshiba_rbtx4938_irq_ioc_disable,
	.unmask = toshiba_rbtx4938_irq_ioc_enable,
};

static int toshiba_rbtx4938_irq_nested(int sw_irq)
{
	u8 level3;

	level3 = readb(rbtx4938_imstat_addr);
	if (level3)
		/* must use fls so onboard ATA has priority */
		sw_irq = RBTX4938_IRQ_IOC + fls(level3) - 1;
	return sw_irq;
}

static void __init
toshiba_rbtx4938_irq_ioc_init(void)
{
	int i;

	for (i = RBTX4938_IRQ_IOC;
	     i < RBTX4938_IRQ_IOC + RBTX4938_NR_IRQ_IOC; i++)
		set_irq_chip_and_handler(i, &toshiba_rbtx4938_irq_ioc_type,
					 handle_level_irq);

	set_irq_chained_handler(RBTX4938_IRQ_IOCINT, handle_simple_irq);
}

static void
toshiba_rbtx4938_irq_ioc_enable(unsigned int irq)
{
	unsigned char v;

	v = readb(rbtx4938_imask_addr);
	v |= (1 << (irq - RBTX4938_IRQ_IOC));
	writeb(v, rbtx4938_imask_addr);
	mmiowb();
}

static void
toshiba_rbtx4938_irq_ioc_disable(unsigned int irq)
{
	unsigned char v;

	v = readb(rbtx4938_imask_addr);
	v &= ~(1 << (irq - RBTX4938_IRQ_IOC));
	writeb(v, rbtx4938_imask_addr);
	mmiowb();
}

static int rbtx4938_irq_dispatch(int pending)
{
	int irq;

	if (pending & STATUSF_IP7)
		irq = MIPS_CPU_IRQ_BASE + 7;
	else if (pending & STATUSF_IP2) {
		irq = txx9_irq();
		if (irq == RBTX4938_IRQ_IOCINT)
			irq = toshiba_rbtx4938_irq_nested(irq);
	} else if (pending & STATUSF_IP1)
		irq = MIPS_CPU_IRQ_BASE + 0;
	else if (pending & STATUSF_IP0)
		irq = MIPS_CPU_IRQ_BASE + 1;
	else
		irq = -1;
	return irq;
}

void __init rbtx4938_irq_setup(void)
{
	txx9_irq_dispatch = rbtx4938_irq_dispatch;
	/* Now, interrupt control disabled, */
	/* all IRC interrupts are masked, */
	/* all IRC interrupt mode are Low Active. */

	/* mask all IOC interrupts */
	writeb(0, rbtx4938_imask_addr);

	/* clear SoftInt interrupts */
	writeb(0, rbtx4938_softint_addr);
	tx4938_irq_init();
	toshiba_rbtx4938_irq_ioc_init();
	/* Onboard 10M Ether: High Active */
	set_irq_type(RBTX4938_IRQ_ETHER, IRQF_TRIGGER_HIGH);
}
