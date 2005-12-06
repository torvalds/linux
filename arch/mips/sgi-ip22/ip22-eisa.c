/*
 * Basic EISA bus support for the SGI Indigo-2.
 *
 * (C) 2002 Pascal Dameme <netinet@freesurf.fr>
 *      and Marc Zyngier <mzyngier@freesurf.fr>
 *
 * This code is released under both the GPL version 2 and BSD
 * licenses.  Either license may be used.
 *
 * This code offers a very basic support for this EISA bus present in
 * the SGI Indigo-2. It currently only supports PIO (forget about DMA
 * for the time being). This is enough for a low-end ethernet card,
 * but forget about your favorite SCSI card...
 *
 * TODO :
 * - Fix bugs...
 * - Add ISA support
 * - Add DMA (yeah, right...).
 * - Fix more bugs.
 */

#include <linux/config.h>
#include <linux/eisa.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mipsregs.h>
#include <asm/addrspace.h>
#include <asm/processor.h>
#include <asm/sgi/ioc.h>
#include <asm/sgi/mc.h>
#include <asm/sgi/ip22.h>

/* I2 has four EISA slots. */
#define IP22_EISA_MAX_SLOTS	  4
#define EISA_MAX_IRQ             16

#define EIU_MODE_REG     0x0001ffc0
#define EIU_STAT_REG     0x0001ffc4
#define EIU_PREMPT_REG   0x0001ffc8
#define EIU_QUIET_REG    0x0001ffcc
#define EIU_INTRPT_ACK   0x00010004

static char __init *decode_eisa_sig(unsigned long addr)
{
        static char sig_str[EISA_SIG_LEN];
	u8 sig[4];
        u16 rev;
	int i;

	for (i = 0; i < 4; i++) {
		sig[i] = inb (addr + i);

		if (!i && (sig[0] & 0x80))
			return NULL;
	}

	sig_str[0] = ((sig[0] >> 2) & 0x1f) + ('A' - 1);
	sig_str[1] = (((sig[0] & 3) << 3) | (sig[1] >> 5)) + ('A' - 1);
	sig_str[2] = (sig[1] & 0x1f) + ('A' - 1);
	rev = (sig[2] << 8) | sig[3];
	sprintf(sig_str + 3, "%04X", rev);

	return sig_str;
}

static irqreturn_t ip22_eisa_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	u8 eisa_irq;
	u8 dma1, dma2;

	eisa_irq = inb(EIU_INTRPT_ACK);
	dma1 = inb(EISA_DMA1_STATUS);
	dma2 = inb(EISA_DMA2_STATUS);

	if (eisa_irq < EISA_MAX_IRQ) {
		do_IRQ(eisa_irq, regs);
		return IRQ_HANDLED;
	}

	/* Oops, Bad Stuff Happened... */
	printk(KERN_ERR "eisa_irq %d out of bound\n", eisa_irq);

	outb(0x20, EISA_INT2_CTRL);
	outb(0x20, EISA_INT1_CTRL);
	return IRQ_NONE;
}

static void enable_eisa1_irq(unsigned int irq)
{
	unsigned long flags;
	u8 mask;

	local_irq_save(flags);

	mask = inb(EISA_INT1_MASK);
	mask &= ~((u8) (1 << irq));
	outb(mask, EISA_INT1_MASK);

	local_irq_restore(flags);
}

static unsigned int startup_eisa1_irq(unsigned int irq)
{
	u8 edge;

	/* Only use edge interrupts for EISA */

	edge = inb(EISA_INT1_EDGE_LEVEL);
	edge &= ~((u8) (1 << irq));
	outb(edge, EISA_INT1_EDGE_LEVEL);

	enable_eisa1_irq(irq);
	return 0;
}

static void disable_eisa1_irq(unsigned int irq)
{
	u8 mask;

	mask = inb(EISA_INT1_MASK);
	mask |= ((u8) (1 << irq));
	outb(mask, EISA_INT1_MASK);
}

#define shutdown_eisa1_irq	disable_eisa1_irq

static void mask_and_ack_eisa1_irq(unsigned int irq)
{
	disable_eisa1_irq(irq);

	outb(0x20, EISA_INT1_CTRL);
}

static void end_eisa1_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS)))
		enable_eisa1_irq(irq);
}

static struct hw_interrupt_type ip22_eisa1_irq_type = {
	.typename	= "IP22 EISA",
	.startup	= startup_eisa1_irq,
	.shutdown	= shutdown_eisa1_irq,
	.enable		= enable_eisa1_irq,
	.disable	= disable_eisa1_irq,
	.ack		= mask_and_ack_eisa1_irq,
	.end		= end_eisa1_irq,
};

static void enable_eisa2_irq(unsigned int irq)
{
	unsigned long flags;
	u8 mask;

	local_irq_save(flags);

	mask = inb(EISA_INT2_MASK);
	mask &= ~((u8) (1 << (irq - 8)));
	outb(mask, EISA_INT2_MASK);

	local_irq_restore(flags);
}

static unsigned int startup_eisa2_irq(unsigned int irq)
{
	u8 edge;

	/* Only use edge interrupts for EISA */

	edge = inb(EISA_INT2_EDGE_LEVEL);
	edge &= ~((u8) (1 << (irq - 8)));
	outb(edge, EISA_INT2_EDGE_LEVEL);

	enable_eisa2_irq(irq);
	return 0;
}

static void disable_eisa2_irq(unsigned int irq)
{
	u8 mask;

	mask = inb(EISA_INT2_MASK);
	mask |= ((u8) (1 << (irq - 8)));
	outb(mask, EISA_INT2_MASK);
}

#define shutdown_eisa2_irq	disable_eisa2_irq

static void mask_and_ack_eisa2_irq(unsigned int irq)
{
	disable_eisa2_irq(irq);

	outb(0x20, EISA_INT2_CTRL);
}

static void end_eisa2_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS)))
		enable_eisa2_irq(irq);
}

static struct hw_interrupt_type ip22_eisa2_irq_type = {
	.typename	= "IP22 EISA",
	.startup	= startup_eisa2_irq,
	.shutdown	= shutdown_eisa2_irq,
	.enable		= enable_eisa2_irq,
	.disable	= disable_eisa2_irq,
	.ack		= mask_and_ack_eisa2_irq,
	.end		= end_eisa2_irq,
};

static struct irqaction eisa_action = {
	.handler	= ip22_eisa_intr,
	.name		= "EISA",
};

static struct irqaction cascade_action = {
	.handler	= no_action,
	.name		= "EISA cascade",
};

int __init ip22_eisa_init(void)
{
	int i, c;
	char *str;

	if (!(sgimc->systemid & SGIMC_SYSID_EPRESENT)) {
		printk(KERN_INFO "EISA: bus not present.\n");
		return 1;
	}

	printk(KERN_INFO "EISA: Probing bus...\n");
	for (c = 0, i = 1; i <= IP22_EISA_MAX_SLOTS; i++) {
		if ((str = decode_eisa_sig(0x1000 * i + EISA_VENDOR_ID_OFFSET))) {
			printk(KERN_INFO "EISA: slot %d : %s detected.\n",
			       i, str);
			c++;
		}
	}
	printk(KERN_INFO "EISA: Detected %d card%s.\n", c, c < 2 ? "" : "s");
#ifdef CONFIG_ISA
	printk(KERN_INFO "ISA support compiled in.\n");
#endif

	/* Warning : BlackMagicAhead(tm).
	   Please wave your favorite dead chicken over the busses */

	/* First say hello to the EIU */
	outl(0x0000FFFF, EIU_PREMPT_REG);
	outl(1, EIU_QUIET_REG);
	outl(0x40f3c07F, EIU_MODE_REG);

	/* Now be nice to the EISA chipset */
	outb(1, EISA_EXT_NMI_RESET_CTRL);
	udelay(50);	/* Wait long enough for the dust to settle */
	outb(0, EISA_EXT_NMI_RESET_CTRL);
	outb(0x11, EISA_INT1_CTRL);
	outb(0x11, EISA_INT2_CTRL);
	outb(0, EISA_INT1_MASK);
	outb(8, EISA_INT2_MASK);
	outb(4, EISA_INT1_MASK);
	outb(2, EISA_INT2_MASK);
	outb(1, EISA_INT1_MASK);
	outb(1, EISA_INT2_MASK);
	outb(0xfb, EISA_INT1_MASK);
	outb(0xff, EISA_INT2_MASK);
	outb(0, EISA_DMA2_WRITE_SINGLE);

	for (i = SGINT_EISA; i < (SGINT_EISA + EISA_MAX_IRQ); i++) {
		irq_desc[i].status = IRQ_DISABLED;
		irq_desc[i].action = 0;
		irq_desc[i].depth = 1;
		if (i < (SGINT_EISA + 8))
			irq_desc[i].handler = &ip22_eisa1_irq_type;
		else
			irq_desc[i].handler = &ip22_eisa2_irq_type;
	}

	/* Cannot use request_irq because of kmalloc not being ready at such
	 * an early stage. Yes, I've been bitten... */
	setup_irq(SGI_EISA_IRQ, &eisa_action);
	setup_irq(SGINT_EISA + 2, &cascade_action);

	EISA_bus = 1;
	return 0;
}
