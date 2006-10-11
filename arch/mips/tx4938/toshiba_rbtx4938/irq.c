/*
 * linux/arch/mips/tx4938/toshiba_rbtx4938/irq.c
 *
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
IRQ  Device

16   TX4938-CP0/00 Software 0
17   TX4938-CP0/01 Software 1
18   TX4938-CP0/02 Cascade TX4938-CP0
19   TX4938-CP0/03 Multiplexed -- do not use
20   TX4938-CP0/04 Multiplexed -- do not use
21   TX4938-CP0/05 Multiplexed -- do not use
22   TX4938-CP0/06 Multiplexed -- do not use
23   TX4938-CP0/07 CPU TIMER

24   TX4938-PIC/00
25   TX4938-PIC/01
26   TX4938-PIC/02 Cascade RBTX4938-IOC
27   TX4938-PIC/03 RBTX4938 RTL-8019AS Ethernet
28   TX4938-PIC/04
29   TX4938-PIC/05 TX4938 ETH1
30   TX4938-PIC/06 TX4938 ETH0
31   TX4938-PIC/07
32   TX4938-PIC/08 TX4938 SIO 0
33   TX4938-PIC/09 TX4938 SIO 1
34   TX4938-PIC/10 TX4938 DMA0
35   TX4938-PIC/11 TX4938 DMA1
36   TX4938-PIC/12 TX4938 DMA2
37   TX4938-PIC/13 TX4938 DMA3
38   TX4938-PIC/14
39   TX4938-PIC/15
40   TX4938-PIC/16 TX4938 PCIC
41   TX4938-PIC/17 TX4938 TMR0
42   TX4938-PIC/18 TX4938 TMR1
43   TX4938-PIC/19 TX4938 TMR2
44   TX4938-PIC/20
45   TX4938-PIC/21
46   TX4938-PIC/22 TX4938 PCIERR
47   TX4938-PIC/23
48   TX4938-PIC/24
49   TX4938-PIC/25
50   TX4938-PIC/26
51   TX4938-PIC/27
52   TX4938-PIC/28
53   TX4938-PIC/29
54   TX4938-PIC/30
55   TX4938-PIC/31 TX4938 SPI

56 RBTX4938-IOC/00 PCI-D
57 RBTX4938-IOC/01 PCI-C
58 RBTX4938-IOC/02 PCI-B
59 RBTX4938-IOC/03 PCI-A
60 RBTX4938-IOC/04 RTC
61 RBTX4938-IOC/05 ATA
62 RBTX4938-IOC/06 MODEM
63 RBTX4938-IOC/07 SWINT
*/
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/timex.h>
#include <asm/bootinfo.h>
#include <asm/page.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/processor.h>
#include <asm/reboot.h>
#include <asm/time.h>
#include <asm/wbflush.h>
#include <linux/bootmem.h>
#include <asm/tx4938/rbtx4938.h>

static unsigned int toshiba_rbtx4938_irq_ioc_startup(unsigned int irq);
static void toshiba_rbtx4938_irq_ioc_shutdown(unsigned int irq);
static void toshiba_rbtx4938_irq_ioc_enable(unsigned int irq);
static void toshiba_rbtx4938_irq_ioc_disable(unsigned int irq);
static void toshiba_rbtx4938_irq_ioc_mask_and_ack(unsigned int irq);
static void toshiba_rbtx4938_irq_ioc_end(unsigned int irq);

DEFINE_SPINLOCK(toshiba_rbtx4938_ioc_lock);

#define TOSHIBA_RBTX4938_IOC_NAME "RBTX4938-IOC"
static struct irq_chip toshiba_rbtx4938_irq_ioc_type = {
	.typename = TOSHIBA_RBTX4938_IOC_NAME,
	.startup = toshiba_rbtx4938_irq_ioc_startup,
	.shutdown = toshiba_rbtx4938_irq_ioc_shutdown,
	.enable = toshiba_rbtx4938_irq_ioc_enable,
	.disable = toshiba_rbtx4938_irq_ioc_disable,
	.ack = toshiba_rbtx4938_irq_ioc_mask_and_ack,
	.end = toshiba_rbtx4938_irq_ioc_end,
	.set_affinity = NULL
};

#define TOSHIBA_RBTX4938_IOC_INTR_ENAB 0xb7f02000
#define TOSHIBA_RBTX4938_IOC_INTR_STAT 0xb7f0200a

int
toshiba_rbtx4938_irq_nested(int sw_irq)
{
	u8 level3;

	level3 = reg_rd08(TOSHIBA_RBTX4938_IOC_INTR_STAT) & 0xff;
	if (level3) {
		/* must use fls so onboard ATA has priority */
		sw_irq = TOSHIBA_RBTX4938_IRQ_IOC_BEG + fls(level3) - 1;
	}

	wbflush();
	return sw_irq;
}

static struct irqaction toshiba_rbtx4938_irq_ioc_action = {
	.handler = no_action,
	.flags = 0,
	.mask = CPU_MASK_NONE,
	.name = TOSHIBA_RBTX4938_IOC_NAME,
};

/**********************************************************************************/
/* Functions for ioc                                                              */
/**********************************************************************************/
static void __init
toshiba_rbtx4938_irq_ioc_init(void)
{
	int i;

	for (i = TOSHIBA_RBTX4938_IRQ_IOC_BEG;
	     i <= TOSHIBA_RBTX4938_IRQ_IOC_END; i++) {
		irq_desc[i].status = IRQ_DISABLED;
		irq_desc[i].action = 0;
		irq_desc[i].depth = 3;
		irq_desc[i].chip = &toshiba_rbtx4938_irq_ioc_type;
	}

	setup_irq(RBTX4938_IRQ_IOCINT,
		  &toshiba_rbtx4938_irq_ioc_action);
}

static unsigned int
toshiba_rbtx4938_irq_ioc_startup(unsigned int irq)
{
	toshiba_rbtx4938_irq_ioc_enable(irq);

	return 0;
}

static void
toshiba_rbtx4938_irq_ioc_shutdown(unsigned int irq)
{
	toshiba_rbtx4938_irq_ioc_disable(irq);
}

static void
toshiba_rbtx4938_irq_ioc_enable(unsigned int irq)
{
	unsigned long flags;
	volatile unsigned char v;

	spin_lock_irqsave(&toshiba_rbtx4938_ioc_lock, flags);

	v = TX4938_RD08(TOSHIBA_RBTX4938_IOC_INTR_ENAB);
	v |= (1 << (irq - TOSHIBA_RBTX4938_IRQ_IOC_BEG));
	TX4938_WR08(TOSHIBA_RBTX4938_IOC_INTR_ENAB, v);
	mmiowb();
	TX4938_RD08(TOSHIBA_RBTX4938_IOC_INTR_ENAB);

	spin_unlock_irqrestore(&toshiba_rbtx4938_ioc_lock, flags);
}

static void
toshiba_rbtx4938_irq_ioc_disable(unsigned int irq)
{
	unsigned long flags;
	volatile unsigned char v;

	spin_lock_irqsave(&toshiba_rbtx4938_ioc_lock, flags);

	v = TX4938_RD08(TOSHIBA_RBTX4938_IOC_INTR_ENAB);
	v &= ~(1 << (irq - TOSHIBA_RBTX4938_IRQ_IOC_BEG));
	TX4938_WR08(TOSHIBA_RBTX4938_IOC_INTR_ENAB, v);
	mmiowb();
	TX4938_RD08(TOSHIBA_RBTX4938_IOC_INTR_ENAB);

	spin_unlock_irqrestore(&toshiba_rbtx4938_ioc_lock, flags);
}

static void
toshiba_rbtx4938_irq_ioc_mask_and_ack(unsigned int irq)
{
	toshiba_rbtx4938_irq_ioc_disable(irq);
}

static void
toshiba_rbtx4938_irq_ioc_end(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS))) {
		toshiba_rbtx4938_irq_ioc_enable(irq);
	}
}

extern void __init txx9_spi_irqinit(int irc_irq);

void __init arch_init_irq(void)
{
	extern void tx4938_irq_init(void);

	/* Now, interrupt control disabled, */
	/* all IRC interrupts are masked, */
	/* all IRC interrupt mode are Low Active. */

	/* mask all IOC interrupts */
	*rbtx4938_imask_ptr = 0;

	/* clear SoftInt interrupts */
	*rbtx4938_softint_ptr = 0;
	tx4938_irq_init();
	toshiba_rbtx4938_irq_ioc_init();
	/* Onboard 10M Ether: High Active */
	TX4938_WR(TX4938_MKA(TX4938_IRC_IRDM0), 0x00000040);

	if (tx4938_ccfgptr->pcfg & TX4938_PCFG_SPI_SEL) {
		txx9_spi_irqinit(RBTX4938_IRQ_IRC_SPI);
        }

	wbflush();
}
