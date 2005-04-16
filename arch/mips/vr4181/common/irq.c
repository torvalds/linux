/*
 * Copyright (C) 2001 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 * Copyright (C) 2005 Ralf Baechle (ralf@linux-mips.org)
 *
 * linux/arch/mips/vr4181/common/irq.c
 *	Completely re-written to use the new irq.c
 *
 * Credits to Bradley D. LaRonde and Michael Klar for writing the original
 * irq.c file which was derived from the common irq.c file.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/random.h>

#include <asm/irq.h>
#include <asm/mipsregs.h>
#include <asm/gdb-stub.h>

#include <asm/vr4181/vr4181.h>

/*
 * Strategy:
 *
 * We essentially have three irq controllers, CPU, system, and gpio.
 *
 * CPU irq controller is taken care by arch/mips/kernel/irq_cpu.c and
 * CONFIG_IRQ_CPU config option.
 *
 * We here provide sys_irq and gpio_irq controller code.
 */

static int sys_irq_base;
static int gpio_irq_base;

/* ---------------------- sys irq ------------------------ */
static void
sys_irq_enable(unsigned int irq)
{
	irq -= sys_irq_base;
	if (irq < 16) {
		*VR4181_MSYSINT1REG |= (u16)(1 << irq);
	} else {
		irq -= 16;
		*VR4181_MSYSINT2REG |= (u16)(1 << irq);
	}
}

static void
sys_irq_disable(unsigned int irq)
{
	irq -= sys_irq_base;
	if (irq < 16) {
		*VR4181_MSYSINT1REG &= ~((u16)(1 << irq));
	} else {
		irq -= 16;
		*VR4181_MSYSINT2REG &= ~((u16)(1 << irq));
	}

}

static unsigned int
sys_irq_startup(unsigned int irq)
{
	sys_irq_enable(irq);
	return 0;
}

#define sys_irq_shutdown	sys_irq_disable
#define sys_irq_ack		sys_irq_disable

static void
sys_irq_end(unsigned int irq)
{
	if(!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS)))
		sys_irq_enable(irq);
}

static hw_irq_controller sys_irq_controller = {
	"vr4181_sys_irq",
	sys_irq_startup,
	sys_irq_shutdown,
	sys_irq_enable,
	sys_irq_disable,
	sys_irq_ack,
	sys_irq_end,
	NULL			/* no affinity stuff for UP */
};

/* ---------------------- gpio irq ------------------------ */
/* gpio irq lines use reverse logic */
static void
gpio_irq_enable(unsigned int irq)
{
	irq -= gpio_irq_base;
	*VR4181_GPINTMSK &= ~((u16)(1 << irq));
}

static void
gpio_irq_disable(unsigned int irq)
{
	irq -= gpio_irq_base;
	*VR4181_GPINTMSK |= (u16)(1 << irq);
}

static unsigned int
gpio_irq_startup(unsigned int irq)
{
	gpio_irq_enable(irq);

	irq -= gpio_irq_base;
	*VR4181_GPINTEN |= (u16)(1 << irq );

	return 0;
}

static void
gpio_irq_shutdown(unsigned int irq)
{
	gpio_irq_disable(irq);

	irq -= gpio_irq_base;
	*VR4181_GPINTEN &= ~((u16)(1 << irq ));
}

static void
gpio_irq_ack(unsigned int irq)
{
	u16 irqtype;
	u16 irqshift;

	gpio_irq_disable(irq);

	/* we clear interrupt if it is edge triggered */
	irq -= gpio_irq_base;
	if (irq < 8) {
		irqtype = *VR4181_GPINTTYPL;
		irqshift = 2 << (irq*2);
	} else {
		irqtype = *VR4181_GPINTTYPH;
		irqshift = 2 << ((irq-8)*2);
	}
	if ( ! (irqtype & irqshift) ) {
		*VR4181_GPINTSTAT = (u16) (1 << irq);
	}
}

static void
gpio_irq_end(unsigned int irq)
{
	if(!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS)))
		gpio_irq_enable(irq);
}

static hw_irq_controller gpio_irq_controller = {
	"vr4181_gpio_irq",
	gpio_irq_startup,
	gpio_irq_shutdown,
	gpio_irq_enable,
	gpio_irq_disable,
	gpio_irq_ack,
	gpio_irq_end,
	NULL			/* no affinity stuff for UP */
};

/* ---------------------  IRQ init stuff ---------------------- */

extern asmlinkage void vr4181_handle_irq(void);
extern void breakpoint(void);
extern int setup_irq(unsigned int irq, struct irqaction *irqaction);
extern void mips_cpu_irq_init(u32 irq_base);

static struct irqaction cascade =
	{ no_action, SA_INTERRUPT, CPU_MASK_NONE, "cascade", NULL, NULL };
static struct irqaction reserved =
	{ no_action, SA_INTERRUPT, CPU_MASK_NONE, "cascade", NULL, NULL };

void __init arch_init_irq(void)
{
	int i;

	set_except_vector(0, vr4181_handle_irq);

	/* init CPU irqs */
	mips_cpu_irq_init(VR4181_CPU_IRQ_BASE);

	/* init sys irqs */
	sys_irq_base = VR4181_SYS_IRQ_BASE;
	for (i=sys_irq_base; i < sys_irq_base + VR4181_NUM_SYS_IRQ; i++) {
		irq_desc[i].status = IRQ_DISABLED;
		irq_desc[i].action = NULL;
		irq_desc[i].depth = 1;
		irq_desc[i].handler = &sys_irq_controller;
	}

	/* init gpio irqs */
	gpio_irq_base = VR4181_GPIO_IRQ_BASE;
	for (i=gpio_irq_base; i < gpio_irq_base + VR4181_NUM_GPIO_IRQ; i++) {
		irq_desc[i].status = IRQ_DISABLED;
		irq_desc[i].action = NULL;
		irq_desc[i].depth = 1;
		irq_desc[i].handler = &gpio_irq_controller;
	}

	/* Default all ICU IRQs to off ... */
	*VR4181_MSYSINT1REG = 0;
	*VR4181_MSYSINT2REG = 0;

	/* We initialize the level 2 ICU registers to all bits disabled. */
	*VR4181_MPIUINTREG = 0;
	*VR4181_MAIUINTREG = 0;
	*VR4181_MKIUINTREG = 0;

	/* disable all GPIO intrs */
	*VR4181_GPINTMSK = 0xffff;

	/* vector handler.  What these do is register the IRQ as non-sharable */
	setup_irq(VR4181_IRQ_INT0, &cascade);
	setup_irq(VR4181_IRQ_GIU, &cascade);

	/*
	 * RTC interrupts are interesting.  They have two destinations.
	 * One is at sys irq controller, and the other is at CPU IP3 and IP4.
	 * RTC timer is used as system timer.
	 * We enable them here, but timer routine will register later
	 * with CPU IP3/IP4.
	 */
	setup_irq(VR4181_IRQ_RTCL1, &reserved);
	setup_irq(VR4181_IRQ_RTCL2, &reserved);
}
