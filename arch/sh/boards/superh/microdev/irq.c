/*
 * arch/sh/boards/superh/microdev/irq.c
 *
 * Copyright (C) 2003 Sean McGoogan (Sean.McGoogan@superh.com)
 *
 * SuperH SH4-202 MicroDev board support.
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/irq.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/mach/irq.h>

#define NUM_EXTERNAL_IRQS 16	/* IRL0 .. IRL15 */


static const struct {
	unsigned char fpgaIrq;
	unsigned char mapped;
	const char *name;
} fpgaIrqTable[NUM_EXTERNAL_IRQS] = {
	{ 0,				0,	"unused"   },		/* IRQ #0	IRL=15	0x200  */
	{ MICRODEV_FPGA_IRQ_KEYBOARD,	1,	"keyboard" },		/* IRQ #1	IRL=14	0x220  */
	{ MICRODEV_FPGA_IRQ_SERIAL1,	1,	"Serial #1"},		/* IRQ #2	IRL=13	0x240  */
	{ MICRODEV_FPGA_IRQ_ETHERNET,	1,	"Ethernet" },		/* IRQ #3	IRL=12	0x260  */
	{ MICRODEV_FPGA_IRQ_SERIAL2,	0,	"Serial #2"},		/* IRQ #4	IRL=11	0x280  */
	{ 0,				0,	"unused"   },		/* IRQ #5	IRL=10	0x2a0  */
	{ 0,				0,	"unused"   },		/* IRQ #6	IRL=9	0x2c0  */
	{ MICRODEV_FPGA_IRQ_USB_HC,	1,	"USB"	   },		/* IRQ #7	IRL=8	0x2e0  */
	{ MICRODEV_IRQ_PCI_INTA,	1,	"PCI INTA" },		/* IRQ #8	IRL=7	0x300  */
	{ MICRODEV_IRQ_PCI_INTB,	1,	"PCI INTB" },		/* IRQ #9	IRL=6	0x320  */
	{ MICRODEV_IRQ_PCI_INTC,	1,	"PCI INTC" },		/* IRQ #10	IRL=5	0x340  */
	{ MICRODEV_IRQ_PCI_INTD,	1,	"PCI INTD" },		/* IRQ #11	IRL=4	0x360  */
	{ MICRODEV_FPGA_IRQ_MOUSE,	1,	"mouse"    },		/* IRQ #12	IRL=3	0x380  */
	{ MICRODEV_FPGA_IRQ_IDE2,	1,	"IDE #2"   },		/* IRQ #13	IRL=2	0x3a0  */
	{ MICRODEV_FPGA_IRQ_IDE1,	1,	"IDE #1"   },		/* IRQ #14	IRL=1	0x3c0  */
	{ 0,				0,	"unused"   },		/* IRQ #15	IRL=0	0x3e0  */
};

#if (MICRODEV_LINUX_IRQ_KEYBOARD != 1)
#  error Inconsistancy in defining the IRQ# for Keyboard!
#endif

#if (MICRODEV_LINUX_IRQ_ETHERNET != 3)
#  error Inconsistancy in defining the IRQ# for Ethernet!
#endif

#if (MICRODEV_LINUX_IRQ_USB_HC != 7)
#  error Inconsistancy in defining the IRQ# for USB!
#endif

#if (MICRODEV_LINUX_IRQ_MOUSE != 12)
#  error Inconsistancy in defining the IRQ# for PS/2 Mouse!
#endif

#if (MICRODEV_LINUX_IRQ_IDE2 != 13)
#  error Inconsistancy in defining the IRQ# for secondary IDE!
#endif

#if (MICRODEV_LINUX_IRQ_IDE1 != 14)
#  error Inconsistancy in defining the IRQ# for primary IDE!
#endif

static void enable_microdev_irq(unsigned int irq);
static void disable_microdev_irq(unsigned int irq);

	/* shutdown is same as "disable" */
#define shutdown_microdev_irq disable_microdev_irq

static void mask_and_ack_microdev(unsigned int);
static void end_microdev_irq(unsigned int irq);

static unsigned int startup_microdev_irq(unsigned int irq)
{
	enable_microdev_irq(irq);
	return 0;		/* never anything pending */
}

static struct hw_interrupt_type microdev_irq_type = {
	.typename = "MicroDev-IRQ",
	.startup = startup_microdev_irq,
	.shutdown = shutdown_microdev_irq,
	.enable = enable_microdev_irq,
	.disable = disable_microdev_irq,
	.ack = mask_and_ack_microdev,
	.end = end_microdev_irq
};

static void disable_microdev_irq(unsigned int irq)
{
	unsigned int flags;
	unsigned int fpgaIrq;

	if (irq >= NUM_EXTERNAL_IRQS) return;
	if (!fpgaIrqTable[irq].mapped) return;

	fpgaIrq = fpgaIrqTable[irq].fpgaIrq;

		/* disable interrupts */
	local_irq_save(flags);

		/* disable interupts on the FPGA INTC register */
	ctrl_outl(MICRODEV_FPGA_INTC_MASK(fpgaIrq), MICRODEV_FPGA_INTDSB_REG);

		/* restore interrupts */
	local_irq_restore(flags);
}

static void enable_microdev_irq(unsigned int irq)
{
	unsigned long priorityReg, priorities, pri;
	unsigned int flags;
	unsigned int fpgaIrq;


	if (irq >= NUM_EXTERNAL_IRQS) return;
	if (!fpgaIrqTable[irq].mapped) return;

	pri = 15 - irq;

	fpgaIrq = fpgaIrqTable[irq].fpgaIrq;
	priorityReg = MICRODEV_FPGA_INTPRI_REG(fpgaIrq);

		/* disable interrupts */
	local_irq_save(flags);

		/* set priority for the interrupt */
	priorities = ctrl_inl(priorityReg);
	priorities &= ~MICRODEV_FPGA_INTPRI_MASK(fpgaIrq);
	priorities |= MICRODEV_FPGA_INTPRI_LEVEL(fpgaIrq, pri);
	ctrl_outl(priorities, priorityReg);

		/* enable interupts on the FPGA INTC register */
	ctrl_outl(MICRODEV_FPGA_INTC_MASK(fpgaIrq), MICRODEV_FPGA_INTENB_REG);

		/* restore interrupts */
	local_irq_restore(flags);
}

	/* This functions sets the desired irq handler to be a MicroDev type */
static void __init make_microdev_irq(unsigned int irq)
{
	disable_irq_nosync(irq);
	irq_desc[irq].handler = &microdev_irq_type;
	disable_microdev_irq(irq);
}

static void mask_and_ack_microdev(unsigned int irq)
{
	disable_microdev_irq(irq);
}

static void end_microdev_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
	{
		enable_microdev_irq(irq);
	}
}

extern void __init init_microdev_irq(void)
{
	int i;

		/* disable interupts on the FPGA INTC register */
	ctrl_outl(~0ul, MICRODEV_FPGA_INTDSB_REG);

	for (i = 0; i < NUM_EXTERNAL_IRQS; i++)
	{
		make_microdev_irq(i);
	}
}

extern void microdev_print_fpga_intc_status(void)
{
	volatile unsigned int * const intenb = (unsigned int*)MICRODEV_FPGA_INTENB_REG;
	volatile unsigned int * const intdsb = (unsigned int*)MICRODEV_FPGA_INTDSB_REG;
	volatile unsigned int * const intpria = (unsigned int*)MICRODEV_FPGA_INTPRI_REG(0);
	volatile unsigned int * const intprib = (unsigned int*)MICRODEV_FPGA_INTPRI_REG(8);
	volatile unsigned int * const intpric = (unsigned int*)MICRODEV_FPGA_INTPRI_REG(16);
	volatile unsigned int * const intprid = (unsigned int*)MICRODEV_FPGA_INTPRI_REG(24);
	volatile unsigned int * const intsrc = (unsigned int*)MICRODEV_FPGA_INTSRC_REG;
	volatile unsigned int * const intreq = (unsigned int*)MICRODEV_FPGA_INTREQ_REG;

	printk("-------------------------- microdev_print_fpga_intc_status() ------------------\n");
	printk("FPGA_INTENB = 0x%08x\n", *intenb);
	printk("FPGA_INTDSB = 0x%08x\n", *intdsb);
	printk("FPGA_INTSRC = 0x%08x\n", *intsrc);
	printk("FPGA_INTREQ = 0x%08x\n", *intreq);
	printk("FPGA_INTPRI[3..0] = %08x:%08x:%08x:%08x\n", *intprid, *intpric, *intprib, *intpria);
	printk("-------------------------------------------------------------------------------\n");
}


