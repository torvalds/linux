/****************************************************************************/
/* 
 * linux/arch/sh/boards/snapgear/setup.c
 *
 * Copyright (C) 2002  David McCullough <davidm@snapgear.com>
 * Copyright (C) 2003  Paul Mundt <lethal@linux-sh.org>
 *
 * Based on files with the following comments:
 *
 *           Copyright (C) 2000  Kazumoto Kojima
 *
 *           Modified for 7751 Solution Engine by
 *           Ian da Silva and Jeremy Siegel, 2001.
 */
/****************************************************************************/

#include <linux/config.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/sched.h>

#include <asm/machvec.h>
#include <asm/mach/io.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/cpu/timer.h>

extern void (*board_time_init)(void);
extern void secureedge5410_rtc_init(void);
extern void pcibios_init(void);

/****************************************************************************/
/*
 * EraseConfig handling functions
 */

static irqreturn_t eraseconfig_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	volatile char dummy __attribute__((unused)) = * (volatile char *) 0xb8000000;

	printk("SnapGear: erase switch interrupt!\n");

	return IRQ_HANDLED;
}

static int __init eraseconfig_init(void)
{
	printk("SnapGear: EraseConfig init\n");
	/* Setup "EraseConfig" switch on external IRQ 0 */
	if (request_irq(IRL0_IRQ, eraseconfig_interrupt, SA_INTERRUPT,
				"Erase Config", NULL))
		printk("SnapGear: failed to register IRQ%d for Reset witch\n",
				IRL0_IRQ);
	else
		printk("SnapGear: registered EraseConfig switch on IRQ%d\n",
				IRL0_IRQ);
	return(0);
}

module_init(eraseconfig_init);

/****************************************************************************/
/*
 * Initialize IRQ setting
 *
 * IRL0 = erase switch
 * IRL1 = eth0
 * IRL2 = eth1
 * IRL3 = crypto
 */

static void __init init_snapgear_IRQ(void)
{
	/* enable individual interrupt mode for externals */
	ctrl_outw(ctrl_inw(INTC_ICR) | INTC_ICR_IRLM, INTC_ICR);

	printk("Setup SnapGear IRQ/IPR ...\n");

	make_ipr_irq(IRL0_IRQ, IRL0_IPR_ADDR, IRL0_IPR_POS, IRL0_PRIORITY);
	make_ipr_irq(IRL1_IRQ, IRL1_IPR_ADDR, IRL1_IPR_POS, IRL1_PRIORITY);
	make_ipr_irq(IRL2_IRQ, IRL2_IPR_ADDR, IRL2_IPR_POS, IRL2_PRIORITY);
	make_ipr_irq(IRL3_IRQ, IRL3_IPR_ADDR, IRL3_IPR_POS, IRL3_PRIORITY);
}

/****************************************************************************/
/*
 *	Fast poll interrupt simulator.
 */

/*
 * Leave all of the fast timer/fast poll stuff commented out for now, since
 * it's not clear whether it actually works or not. Since it wasn't being used
 * at all in 2.4, we'll assume it's not sane for 2.6 either.. -- PFM
 */
#if 0
#define FAST_POLL	1000
//#define FAST_POLL_INTR

#define FASTTIMER_IRQ   17
#define FASTTIMER_IPR_ADDR  INTC_IPRA
#define FASTTIMER_IPR_POS    2
#define FASTTIMER_PRIORITY   3

#ifdef FAST_POLL_INTR
#define TMU1_TCR_INIT	0x0020
#else
#define TMU1_TCR_INIT	0
#endif
#define TMU_TSTR_INIT	1
#define TMU1_TCR_CALIB	0x0000


#ifdef FAST_POLL_INTR
static void fast_timer_irq(int irq, void *dev_instance, struct pt_regs *regs)
{
	unsigned long timer_status;
    timer_status = ctrl_inw(TMU1_TCR);
	timer_status &= ~0x100;
	ctrl_outw(timer_status, TMU1_TCR);
}
#endif

/*
 * return the current ticks on the fast timer
 */

unsigned long fast_timer_count(void)
{
	return(ctrl_inl(TMU1_TCNT));
}

/*
 * setup a fast timer for profiling etc etc
 */

static void setup_fast_timer()
{
	unsigned long interval;

#ifdef FAST_POLL_INTR
	interval = (current_cpu_data.module_clock/4 + FAST_POLL/2) / FAST_POLL;

	make_ipr_irq(FASTTIMER_IRQ, FASTTIMER_IPR_ADDR, FASTTIMER_IPR_POS,
			FASTTIMER_PRIORITY);

	printk("SnapGear: %dHz fast timer on IRQ %d\n",FAST_POLL,FASTTIMER_IRQ);

	if (request_irq(FASTTIMER_IRQ, fast_timer_irq, 0, "SnapGear fast timer",
			NULL) != 0)
		printk("%s(%d): request_irq() failed?\n", __FILE__, __LINE__);
#else
	printk("SnapGear: fast timer running\n",FAST_POLL,FASTTIMER_IRQ);
	interval = 0xffffffff;
#endif

	ctrl_outb(ctrl_inb(TMU_TSTR) & ~0x2, TMU_TSTR); /* disable timer 1 */
	ctrl_outw(TMU1_TCR_INIT, TMU1_TCR);
	ctrl_outl(interval, TMU1_TCOR);
	ctrl_outl(interval, TMU1_TCNT);
	ctrl_outb(ctrl_inb(TMU_TSTR) | 0x2, TMU_TSTR); /* enable timer 1 */

	printk("Timer count 1 = 0x%x\n", fast_timer_count());
	udelay(1000);
	printk("Timer count 2 = 0x%x\n", fast_timer_count());
}
#endif

/****************************************************************************/

const char *get_system_type(void)
{
	return "SnapGear SecureEdge5410";
}

/*
 * The Machine Vector
 */

struct sh_machine_vector mv_snapgear __initmv = {
	.mv_nr_irqs		= 72,

	.mv_inb			= snapgear_inb,
	.mv_inw			= snapgear_inw,
	.mv_inl			= snapgear_inl,
	.mv_outb		= snapgear_outb,
	.mv_outw		= snapgear_outw,
	.mv_outl		= snapgear_outl,

	.mv_inb_p		= snapgear_inb_p,
	.mv_inw_p		= snapgear_inw,
	.mv_inl_p		= snapgear_inl,
	.mv_outb_p		= snapgear_outb_p,
	.mv_outw_p		= snapgear_outw,
	.mv_outl_p		= snapgear_outl,

	.mv_isa_port2addr	= snapgear_isa_port2addr,

	.mv_init_irq		= init_snapgear_IRQ,
};
ALIAS_MV(snapgear)

/*
 * Initialize the board
 */

int __init platform_setup(void)
{
	board_time_init = secureedge5410_rtc_init;

	return 0;
}

