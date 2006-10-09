/*
 * Interrupt controller driver for Xilinx Virtex-II Pro.
 *
 * Author: MontaVista Software, Inc.
 *         source@mvista.com
 *
 * 2002-2004 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/init.h>
#include <linux/irq.h>
#include <asm/io.h>
#include <platforms/4xx/xparameters/xparameters.h>
#include <asm/ibm4xx.h>
#include <asm/machdep.h>

/* No one else should require these constants, so define them locally here. */
#define ISR 0			/* Interrupt Status Register */
#define IPR 1			/* Interrupt Pending Register */
#define IER 2			/* Interrupt Enable Register */
#define IAR 3			/* Interrupt Acknowledge Register */
#define SIE 4			/* Set Interrupt Enable bits */
#define CIE 5			/* Clear Interrupt Enable bits */
#define IVR 6			/* Interrupt Vector Register */
#define MER 7			/* Master Enable Register */

#if XPAR_XINTC_USE_DCR == 0
static volatile u32 *intc;
#define intc_out_be32(addr, mask)     out_be32((addr), (mask))
#define intc_in_be32(addr)            in_be32((addr))
#else
#define intc    XPAR_INTC_0_BASEADDR
#define intc_out_be32(addr, mask)     mtdcr((addr), (mask))
#define intc_in_be32(addr)            mfdcr((addr))
#endif

static void
xilinx_intc_enable(unsigned int irq)
{
	unsigned long mask = (0x00000001 << (irq & 31));
	pr_debug("enable: %d\n", irq);
	intc_out_be32(intc + SIE, mask);
}

static void
xilinx_intc_disable(unsigned int irq)
{
	unsigned long mask = (0x00000001 << (irq & 31));
	pr_debug("disable: %d\n", irq);
	intc_out_be32(intc + CIE, mask);
}

static void
xilinx_intc_disable_and_ack(unsigned int irq)
{
	unsigned long mask = (0x00000001 << (irq & 31));
	pr_debug("disable_and_ack: %d\n", irq);
	intc_out_be32(intc + CIE, mask);
	if (!(irq_desc[irq].status & IRQ_LEVEL))
		intc_out_be32(intc + IAR, mask);	/* ack edge triggered intr */
}

static void
xilinx_intc_end(unsigned int irq)
{
	unsigned long mask = (0x00000001 << (irq & 31));

	pr_debug("end: %d\n", irq);
	if (!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS))) {
		intc_out_be32(intc + SIE, mask);
		/* ack level sensitive intr */
		if (irq_desc[irq].status & IRQ_LEVEL)
			intc_out_be32(intc + IAR, mask);
	}
}

static struct hw_interrupt_type xilinx_intc = {
	.typename = "Xilinx Interrupt Controller",
	.enable = xilinx_intc_enable,
	.disable = xilinx_intc_disable,
	.ack = xilinx_intc_disable_and_ack,
	.end = xilinx_intc_end,
};

int
xilinx_pic_get_irq(void)
{
	int irq;

	/*
	 * NOTE: This function is the one that needs to be improved in
	 * order to handle multiple interrupt controllers.  It currently
	 * is hardcoded to check for interrupts only on the first INTC.
	 */

	irq = intc_in_be32(intc + IVR);
	if (irq != -1)
		irq = irq;

	pr_debug("get_irq: %d\n", irq);

	return (irq);
}

void __init
ppc4xx_pic_init(void)
{
	int i;

	/*
	 * NOTE: The assumption here is that NR_IRQS is 32 or less
	 * (NR_IRQS is 32 for PowerPC 405 cores by default).
	 */
#if (NR_IRQS > 32)
#error NR_IRQS > 32 not supported
#endif

#if XPAR_XINTC_USE_DCR == 0
	intc = ioremap(XPAR_INTC_0_BASEADDR, 32);

	printk(KERN_INFO "Xilinx INTC #0 at 0x%08lX mapped to 0x%08lX\n",
	       (unsigned long) XPAR_INTC_0_BASEADDR, (unsigned long) intc);
#else
	printk(KERN_INFO "Xilinx INTC #0 at 0x%08lX (DCR)\n",
	       (unsigned long) XPAR_INTC_0_BASEADDR);
#endif

	/*
	 * Disable all external interrupts until they are
	 * explicity requested.
	 */
	intc_out_be32(intc + IER, 0);

	/* Acknowledge any pending interrupts just in case. */
	intc_out_be32(intc + IAR, ~(u32) 0);

	/* Turn on the Master Enable. */
	intc_out_be32(intc + MER, 0x3UL);

	ppc_md.get_irq = xilinx_pic_get_irq;

	for (i = 0; i < NR_IRQS; ++i) {
		irq_desc[i].chip = &xilinx_intc;

		if (XPAR_INTC_0_KIND_OF_INTR & (0x00000001 << i))
			irq_desc[i].status &= ~IRQ_LEVEL;
		else
			irq_desc[i].status |= IRQ_LEVEL;
	}
}
