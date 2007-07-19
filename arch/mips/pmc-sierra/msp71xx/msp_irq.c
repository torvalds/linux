/*
 * IRQ vector handles
 *
 * Copyright (C) 1995, 1996, 1997, 2003 by Ralf Baechle
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/time.h>

#include <asm/irq_cpu.h>

#include <msp_int.h>

extern void msp_int_handle(void);

/* SLP bases systems */
extern void msp_slp_irq_init(void);
extern void msp_slp_irq_dispatch(void);

/* CIC based systems */
extern void msp_cic_irq_init(void);
extern void msp_cic_irq_dispatch(void);

/*
 * The PMC-Sierra MSP interrupts are arranged in a 3 level cascaded
 * hierarchical system.  The first level are the direct MIPS interrupts
 * and are assigned the interrupt range 0-7.  The second level is the SLM
 * interrupt controller and is assigned the range 8-39.  The third level
 * comprises the Peripherial block, the PCI block, the PCI MSI block and
 * the SLP.  The PCI interrupts and the SLP errors are handled by the
 * relevant subsystems so the core interrupt code needs only concern
 * itself with the Peripheral block.  These are assigned interrupts in
 * the range 40-71.
 */

asmlinkage void plat_irq_dispatch(struct pt_regs *regs)
{
	u32 pending;

	pending = read_c0_status() & read_c0_cause();

	/*
	 * jump to the correct interrupt routine
	 * These are arranged in priority order and the timer
	 * comes first!
	 */

#ifdef CONFIG_IRQ_MSP_CIC	/* break out the CIC stuff for now */
	if (pending & C_IRQ4)	/* do the peripherals first, that's the timer */
		msp_cic_irq_dispatch();

	else if (pending & C_IRQ0)
		do_IRQ(MSP_INT_MAC0);

	else if (pending & C_IRQ1)
		do_IRQ(MSP_INT_MAC1);

	else if (pending & C_IRQ2)
		do_IRQ(MSP_INT_USB);

	else if (pending & C_IRQ3)
		do_IRQ(MSP_INT_SAR);

	else if (pending & C_IRQ5)
		do_IRQ(MSP_INT_SEC);

#else
	if (pending & C_IRQ5)
		do_IRQ(MSP_INT_TIMER);

	else if (pending & C_IRQ0)
		do_IRQ(MSP_INT_MAC0);

	else if (pending & C_IRQ1)
		do_IRQ(MSP_INT_MAC1);

	else if (pending & C_IRQ3)
		do_IRQ(MSP_INT_VE);

	else if (pending & C_IRQ4)
		msp_slp_irq_dispatch();
#endif

	else if (pending & C_SW0)	/* do software after hardware */
		do_IRQ(MSP_INT_SW0);

	else if (pending & C_SW1)
		do_IRQ(MSP_INT_SW1);
}

static struct irqaction cascade_msp = {
	.handler = no_action,
	.name	 = "MSP cascade"
};


void __init arch_init_irq(void)
{
	/* initialize the 1st-level CPU based interrupt controller */
	mips_cpu_irq_init();

#ifdef CONFIG_IRQ_MSP_CIC
	msp_cic_irq_init();

	/* setup the cascaded interrupts */
	setup_irq(MSP_INT_CIC, &cascade_msp);
	setup_irq(MSP_INT_PER, &cascade_msp);
#else
	/* setup the 2nd-level SLP register based interrupt controller */
	msp_slp_irq_init();

	/* setup the cascaded SLP/PER interrupts */
	setup_irq(MSP_INT_SLP, &cascade_msp);
	setup_irq(MSP_INT_PER, &cascade_msp);
#endif
}
