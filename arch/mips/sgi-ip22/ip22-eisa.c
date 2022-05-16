/*
 * Basic EISA bus support for the SGI Indigo-2.
 *
 * (C) 2002 Pascal Dameme <netinet@freesurf.fr>
 *	and Marc Zyngier <mzyngier@freesurf.fr>
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
#include <asm/i8259.h>

/* I2 has four EISA slots. */
#define IP22_EISA_MAX_SLOTS	  4
#define EISA_MAX_IRQ		 16

#define EIU_MODE_REG	 0x0001ffc0
#define EIU_STAT_REG	 0x0001ffc4
#define EIU_PREMPT_REG	 0x0001ffc8
#define EIU_QUIET_REG	 0x0001ffcc
#define EIU_INTRPT_ACK	 0x00010004

static char __init *decode_eisa_sig(unsigned long addr)
{
	static char sig_str[EISA_SIG_LEN] __initdata;
	u8 sig[4];
	u16 rev;
	int i;

	for (i = 0; i < 4; i++) {
		sig[i] = inb(addr + i);

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

static irqreturn_t ip22_eisa_intr(int irq, void *dev_id)
{
	u8 eisa_irq = inb(EIU_INTRPT_ACK);

	inb(EISA_DMA1_STATUS);
	inb(EISA_DMA2_STATUS);

	if (eisa_irq < EISA_MAX_IRQ) {
		do_IRQ(eisa_irq);
		return IRQ_HANDLED;
	}

	/* Oops, Bad Stuff Happened... */
	printk(KERN_ERR "eisa_irq %d out of bound\n", eisa_irq);

	outb(0x20, EISA_INT2_CTRL);
	outb(0x20, EISA_INT1_CTRL);

	return IRQ_NONE;
}

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
	outb(0, EISA_DMA2_WRITE_SINGLE);

	init_i8259_irqs();

	if (request_irq(SGI_EISA_IRQ, ip22_eisa_intr, 0, "EISA", NULL))
		pr_err("Failed to request irq %d (EISA)\n", SGI_EISA_IRQ);

	EISA_bus = 1;
	return 0;
}
