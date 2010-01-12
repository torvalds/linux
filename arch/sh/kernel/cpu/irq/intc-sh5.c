/*
 * arch/sh/kernel/cpu/irq/intc-sh5.c
 *
 * Interrupt Controller support for SH5 INTC.
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003  Paul Mundt
 *
 * Per-interrupt selective. IRLM=0 (Fixed priority) is not
 * supported being useless without a cascaded interrupt
 * controller.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/bitops.h>
#include <cpu/irq.h>
#include <asm/page.h>

/*
 * Maybe the generic Peripheral block could move to a more
 * generic include file. INTC Block will be defined here
 * and only here to make INTC self-contained in a single
 * file.
 */
#define	INTC_BLOCK_OFFSET	0x01000000

/* Base */
#define INTC_BASE		PHYS_PERIPHERAL_BLOCK + \
				INTC_BLOCK_OFFSET

/* Address */
#define INTC_ICR_SET		(intc_virt + 0x0)
#define INTC_ICR_CLEAR		(intc_virt + 0x8)
#define INTC_INTPRI_0		(intc_virt + 0x10)
#define INTC_INTSRC_0		(intc_virt + 0x50)
#define INTC_INTSRC_1		(intc_virt + 0x58)
#define INTC_INTREQ_0		(intc_virt + 0x60)
#define INTC_INTREQ_1		(intc_virt + 0x68)
#define INTC_INTENB_0		(intc_virt + 0x70)
#define INTC_INTENB_1		(intc_virt + 0x78)
#define INTC_INTDSB_0		(intc_virt + 0x80)
#define INTC_INTDSB_1		(intc_virt + 0x88)

#define INTC_ICR_IRLM		0x1
#define	INTC_INTPRI_PREGS	8		/* 8 Priority Registers */
#define	INTC_INTPRI_PPREG	8		/* 8 Priorities per Register */


/*
 * Mapper between the vector ordinal and the IRQ number
 * passed to kernel/device drivers.
 */
int intc_evt_to_irq[(0xE20/0x20)+1] = {
	-1, -1, -1, -1, -1, -1, -1, -1,	/* 0x000 - 0x0E0 */
	-1, -1, -1, -1, -1, -1, -1, -1,	/* 0x100 - 0x1E0 */
	 0,  0,  0,  0,  0,  1,  0,  0,	/* 0x200 - 0x2E0 */
	 2,  0,  0,  3,  0,  0,  0, -1,	/* 0x300 - 0x3E0 */
	32, 33, 34, 35, 36, 37, 38, -1,	/* 0x400 - 0x4E0 */
	-1, -1, -1, 63, -1, -1, -1, -1,	/* 0x500 - 0x5E0 */
	-1, -1, 18, 19, 20, 21, 22, -1,	/* 0x600 - 0x6E0 */
	39, 40, 41, 42, -1, -1, -1, -1,	/* 0x700 - 0x7E0 */
	 4,  5,  6,  7, -1, -1, -1, -1,	/* 0x800 - 0x8E0 */
	-1, -1, -1, -1, -1, -1, -1, -1,	/* 0x900 - 0x9E0 */
	12, 13, 14, 15, 16, 17, -1, -1,	/* 0xA00 - 0xAE0 */
	-1, -1, -1, -1, -1, -1, -1, -1,	/* 0xB00 - 0xBE0 */
	-1, -1, -1, -1, -1, -1, -1, -1,	/* 0xC00 - 0xCE0 */
	-1, -1, -1, -1, -1, -1, -1, -1,	/* 0xD00 - 0xDE0 */
	-1, -1				/* 0xE00 - 0xE20 */
};

static unsigned long intc_virt;

static unsigned int startup_intc_irq(unsigned int irq);
static void shutdown_intc_irq(unsigned int irq);
static void enable_intc_irq(unsigned int irq);
static void disable_intc_irq(unsigned int irq);
static void mask_and_ack_intc(unsigned int);
static void end_intc_irq(unsigned int irq);

static struct irq_chip intc_irq_type = {
	.name = "INTC",
	.startup = startup_intc_irq,
	.shutdown = shutdown_intc_irq,
	.enable = enable_intc_irq,
	.disable = disable_intc_irq,
	.ack = mask_and_ack_intc,
	.end = end_intc_irq
};

static int irlm;		/* IRL mode */

static unsigned int startup_intc_irq(unsigned int irq)
{
	enable_intc_irq(irq);
	return 0; /* never anything pending */
}

static void shutdown_intc_irq(unsigned int irq)
{
	disable_intc_irq(irq);
}

static void enable_intc_irq(unsigned int irq)
{
	unsigned long reg;
	unsigned long bitmask;

	if ((irq <= IRQ_IRL3) && (irlm == NO_PRIORITY))
		printk("Trying to use straight IRL0-3 with an encoding platform.\n");

	if (irq < 32) {
		reg = INTC_INTENB_0;
		bitmask = 1 << irq;
	} else {
		reg = INTC_INTENB_1;
		bitmask = 1 << (irq - 32);
	}

	ctrl_outl(bitmask, reg);
}

static void disable_intc_irq(unsigned int irq)
{
	unsigned long reg;
	unsigned long bitmask;

	if (irq < 32) {
		reg = INTC_INTDSB_0;
		bitmask = 1 << irq;
	} else {
		reg = INTC_INTDSB_1;
		bitmask = 1 << (irq - 32);
	}

	ctrl_outl(bitmask, reg);
}

static void mask_and_ack_intc(unsigned int irq)
{
	disable_intc_irq(irq);
}

static void end_intc_irq(unsigned int irq)
{
	enable_intc_irq(irq);
}

void __init plat_irq_setup(void)
{
	unsigned long long __dummy0, __dummy1=~0x00000000100000f0;
	unsigned long reg;
	int i;

	intc_virt = (unsigned long)ioremap_nocache(INTC_BASE, 1024);
	if (!intc_virt) {
		panic("Unable to remap INTC\n");
	}


	/* Set default: per-line enable/disable, priority driven ack/eoi */
	for (i = 0; i < NR_INTC_IRQS; i++)
		set_irq_chip_and_handler(i, &intc_irq_type, handle_level_irq);


	/* Disable all interrupts and set all priorities to 0 to avoid trouble */
	ctrl_outl(-1, INTC_INTDSB_0);
	ctrl_outl(-1, INTC_INTDSB_1);

	for (reg = INTC_INTPRI_0, i = 0; i < INTC_INTPRI_PREGS; i++, reg += 8)
		ctrl_outl( NO_PRIORITY, reg);


#ifdef CONFIG_SH_CAYMAN
	{
		unsigned long data;

		/* Set IRLM */
		/* If all the priorities are set to 'no priority', then
		 * assume we are using encoded mode.
		 */
		irlm = platform_int_priority[IRQ_IRL0] +
		       platform_int_priority[IRQ_IRL1] +
		       platform_int_priority[IRQ_IRL2] +
		       platform_int_priority[IRQ_IRL3];
		if (irlm == NO_PRIORITY) {
			/* IRLM = 0 */
			reg = INTC_ICR_CLEAR;
			i = IRQ_INTA;
			printk("Trying to use encoded IRL0-3. IRLs unsupported.\n");
		} else {
			/* IRLM = 1 */
			reg = INTC_ICR_SET;
			i = IRQ_IRL0;
		}
		ctrl_outl(INTC_ICR_IRLM, reg);

		/* Set interrupt priorities according to platform description */
		for (data = 0, reg = INTC_INTPRI_0; i < NR_INTC_IRQS; i++) {
			data |= platform_int_priority[i] <<
				((i % INTC_INTPRI_PPREG) * 4);
			if ((i % INTC_INTPRI_PPREG) == (INTC_INTPRI_PPREG - 1)) {
				/* Upon the 7th, set Priority Register */
				ctrl_outl(data, reg);
				data = 0;
				reg += 8;
			}
		}
	}
#endif

	/*
	 * And now let interrupts come in.
	 * sti() is not enough, we need to
	 * lower priority, too.
	 */
        __asm__ __volatile__("getcon    " __SR ", %0\n\t"
                             "and       %0, %1, %0\n\t"
                             "putcon    %0, " __SR "\n\t"
                             : "=&r" (__dummy0)
                             : "r" (__dummy1));
}
