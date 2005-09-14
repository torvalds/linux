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
#include <asm/irq.h>
#include <asm/mipsregs.h>
#include <asm/addrspace.h>
#include <asm/processor.h>
#include <asm/sgi/ioc.h>
#include <asm/sgi/mc.h>
#include <asm/sgi/ip22.h>

#define EISA_MAX_SLOTS		  4
#define EISA_MAX_IRQ             16

#define EISA_TO_PHYS(x)  (0x00080000 | (x))
#define EISA_TO_KSEG1(x) ((void *) KSEG1ADDR(EISA_TO_PHYS((x))))

#define EIU_MODE_REG     0x0009ffc0
#define EIU_STAT_REG     0x0009ffc4
#define EIU_PREMPT_REG   0x0009ffc8
#define EIU_QUIET_REG    0x0009ffcc
#define EIU_INTRPT_ACK   0x00090004

#define EISA_DMA1_STATUS            8
#define EISA_INT1_CTRL           0x20
#define EISA_INT1_MASK           0x21
#define EISA_INT2_CTRL           0xA0
#define EISA_INT2_MASK           0xA1
#define EISA_DMA2_STATUS         0xD0
#define EISA_DMA2_WRITE_SINGLE   0xD4
#define EISA_EXT_NMI_RESET_CTRL 0x461
#define EISA_INT1_EDGE_LEVEL    0x4D0
#define EISA_INT2_EDGE_LEVEL    0x4D1
#define EISA_VENDOR_ID_OFFSET   0xC80

#define EIU_WRITE_32(x,y) { *((u32 *) KSEG1ADDR(x)) = (u32) (y); mb(); }
#define EIU_READ_8(x) *((u8 *) KSEG1ADDR(x))
#define EISA_WRITE_8(x,y) { *((u8 *) EISA_TO_KSEG1(x)) = (u8) (y); mb(); }
#define EISA_READ_8(x) *((u8 *) EISA_TO_KSEG1(x))

static char *decode_eisa_sig(u8 * sig)
{
	static char sig_str[8];
	u16 rev;

	if (sig[0] & 0x80)
		return NULL;

	sig_str[0] = ((sig[0] >> 2) & 0x1f) + ('A' - 1);
	sig_str[1] = (((sig[0] & 3) << 3) | (sig[1] >> 5)) + ('A' - 1);
	sig_str[2] = (sig[1] & 0x1f) + ('A' - 1);
	rev = (sig[2] << 8) | sig[3];
	sprintf(sig_str + 3, "%04X", rev);

	return sig_str;
}

static void ip22_eisa_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	u8 eisa_irq;
	u8 dma1, dma2;

	eisa_irq = EIU_READ_8(EIU_INTRPT_ACK);
	dma1 = EISA_READ_8(EISA_DMA1_STATUS);
	dma2 = EISA_READ_8(EISA_DMA2_STATUS);

	if (eisa_irq >= EISA_MAX_IRQ) {
		/* Oops, Bad Stuff Happened... */
		printk(KERN_ERR "eisa_irq %d out of bound\n", eisa_irq);

		EISA_WRITE_8(EISA_INT2_CTRL, 0x20);
		EISA_WRITE_8(EISA_INT1_CTRL, 0x20);
	} else
		do_IRQ(eisa_irq, regs);
}

static void enable_eisa1_irq(unsigned int irq)
{
	unsigned long flags;
	u8 mask;

	local_irq_save(flags);

	mask = EISA_READ_8(EISA_INT1_MASK);
	mask &= ~((u8) (1 << irq));
	EISA_WRITE_8(EISA_INT1_MASK, mask);

	local_irq_restore(flags);
}

static unsigned int startup_eisa1_irq(unsigned int irq)
{
	u8 edge;

	/* Only use edge interrupts for EISA */

	edge = EISA_READ_8(EISA_INT1_EDGE_LEVEL);
	edge &= ~((u8) (1 << irq));
	EISA_WRITE_8(EISA_INT1_EDGE_LEVEL, edge);

	enable_eisa1_irq(irq);
	return 0;
}

static void disable_eisa1_irq(unsigned int irq)
{
	u8 mask;

	mask = EISA_READ_8(EISA_INT1_MASK);
	mask |= ((u8) (1 << irq));
	EISA_WRITE_8(EISA_INT1_MASK, mask);
}

#define shutdown_eisa1_irq	disable_eisa1_irq

static void mask_and_ack_eisa1_irq(unsigned int irq)
{
	disable_eisa1_irq(irq);

	EISA_WRITE_8(EISA_INT1_CTRL, 0x20);
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

	mask = EISA_READ_8(EISA_INT2_MASK);
	mask &= ~((u8) (1 << (irq - 8)));
	EISA_WRITE_8(EISA_INT2_MASK, mask);

	local_irq_restore(flags);
}

static unsigned int startup_eisa2_irq(unsigned int irq)
{
	u8 edge;

	/* Only use edge interrupts for EISA */

	edge = EISA_READ_8(EISA_INT2_EDGE_LEVEL);
	edge &= ~((u8) (1 << (irq - 8)));
	EISA_WRITE_8(EISA_INT2_EDGE_LEVEL, edge);

	enable_eisa2_irq(irq);
	return 0;
}

static void disable_eisa2_irq(unsigned int irq)
{
	u8 mask;

	mask = EISA_READ_8(EISA_INT2_MASK);
	mask |= ((u8) (1 << (irq - 8)));
	EISA_WRITE_8(EISA_INT2_MASK, mask);
}

#define shutdown_eisa2_irq	disable_eisa2_irq

static void mask_and_ack_eisa2_irq(unsigned int irq)
{
	disable_eisa2_irq(irq);

	EISA_WRITE_8(EISA_INT2_CTRL, 0x20);
	EISA_WRITE_8(EISA_INT1_CTRL, 0x20);
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
	u8 *slot_addr;

	if (!(sgimc->systemid & SGIMC_SYSID_EPRESENT)) {
		printk(KERN_INFO "EISA: bus not present.\n");
		return 1;
	}

	printk(KERN_INFO "EISA: Probing bus...\n");
	for (c = 0, i = 1; i <= EISA_MAX_SLOTS; i++) {
		slot_addr =
		    (u8 *) EISA_TO_KSEG1((0x1000 * i) +
					 EISA_VENDOR_ID_OFFSET);
		if ((str = decode_eisa_sig(slot_addr))) {
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
	EIU_WRITE_32(EIU_PREMPT_REG, 0x0000FFFF);
	EIU_WRITE_32(EIU_QUIET_REG, 1);
	EIU_WRITE_32(EIU_MODE_REG, 0x40f3c07F);

	/* Now be nice to the EISA chipset */
	EISA_WRITE_8(EISA_EXT_NMI_RESET_CTRL, 1);
	for (i = 0; i < 10000; i++);	/* Wait long enough for the dust to settle */
	EISA_WRITE_8(EISA_EXT_NMI_RESET_CTRL, 0);
	EISA_WRITE_8(EISA_INT1_CTRL, 0x11);
	EISA_WRITE_8(EISA_INT2_CTRL, 0x11);
	EISA_WRITE_8(EISA_INT1_MASK, 0);
	EISA_WRITE_8(EISA_INT2_MASK, 8);
	EISA_WRITE_8(EISA_INT1_MASK, 4);
	EISA_WRITE_8(EISA_INT2_MASK, 2);
	EISA_WRITE_8(EISA_INT1_MASK, 1);
	EISA_WRITE_8(EISA_INT2_MASK, 1);
	EISA_WRITE_8(EISA_INT1_MASK, 0xfb);
	EISA_WRITE_8(EISA_INT2_MASK, 0xff);
	EISA_WRITE_8(EISA_DMA2_WRITE_SINGLE, 0);

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
