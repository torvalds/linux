/***************************************************************************/

/*
 *	pit.c -- Freescale ColdFire PIT timer. Currently this type of
 *	         hardware timer only exists in the Freescale ColdFire
 *		 5270/5271, 5282 and other CPUs.
 *
 *	Copyright (C) 1999-2006, Greg Ungerer (gerg@snapgear.com)
 *	Copyright (C) 2001-2004, SnapGear Inc. (www.snapgear.com)
 *
 */

/***************************************************************************/

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/param.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/coldfire.h>
#include <asm/mcfpit.h>
#include <asm/mcfsim.h>

/***************************************************************************/

/*
 *	By default use timer1 as the system clock timer.
 */
#define	TA(a)	(MCF_IPSBAR + MCFPIT_BASE1 + (a))

/***************************************************************************/

void coldfire_pit_tick(void)
{
	unsigned short pcsr;

	/* Reset the ColdFire timer */
	pcsr = __raw_readw(TA(MCFPIT_PCSR));
	__raw_writew(pcsr | MCFPIT_PCSR_PIF, TA(MCFPIT_PCSR));
}

/***************************************************************************/

void coldfire_pit_init(irq_handler_t handler)
{
	volatile unsigned char *icrp;
	volatile unsigned long *imrp;

	request_irq(MCFINT_VECBASE + MCFINT_PIT1, handler, IRQF_DISABLED,
		"ColdFire Timer", NULL);

	icrp = (volatile unsigned char *) (MCF_IPSBAR + MCFICM_INTC0 +
		MCFINTC_ICR0 + MCFINT_PIT1);
	*icrp = ICR_INTRCONF;

	imrp = (volatile unsigned long *) (MCF_IPSBAR + MCFICM_INTC0 + MCFPIT_IMR);
	*imrp &= ~MCFPIT_IMR_IBIT;

	/* Set up PIT timer 1 as poll clock */
	__raw_writew(MCFPIT_PCSR_DISABLE, TA(MCFPIT_PCSR));
	__raw_writew(((MCF_CLK / 2) / 64) / HZ, TA(MCFPIT_PMR));
	__raw_writew(MCFPIT_PCSR_EN | MCFPIT_PCSR_PIE | MCFPIT_PCSR_OVW |
		MCFPIT_PCSR_RLD | MCFPIT_PCSR_CLK64, TA(MCFPIT_PCSR));
}

/***************************************************************************/

unsigned long coldfire_pit_offset(void)
{
	volatile unsigned long *ipr;
	unsigned long pmr, pcntr, offset;

	ipr = (volatile unsigned long *) (MCF_IPSBAR + MCFICM_INTC0 + MCFPIT_IMR);

	pmr = __raw_readw(TA(MCFPIT_PMR));
	pcntr = __raw_readw(TA(MCFPIT_PCNTR));

	/*
	 * If we are still in the first half of the upcount and a
	 * timer interupt is pending, then add on a ticks worth of time.
	 */
	offset = ((pmr - pcntr) * (1000000 / HZ)) / pmr;
	if ((offset < (1000000 / HZ / 2)) && (*ipr & MCFPIT_IMR_IBIT))
		offset += 1000000 / HZ;
	return offset;	
}

/***************************************************************************/
