/*
 * Copyright (C) 2000,2001,2004 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

/*
 * These are routines to set up and handle interrupts from the
 * bcm1480 general purpose timer 0.  We're using the timer as a
 * system clock, so we set it up to run at 100 Hz.  On every
 * interrupt, we update our idea of what the time of day is,
 * then call do_timer() in the architecture-independent kernel
 * code to do general bookkeeping (e.g. update jiffies, run
 * bottom halves, etc.)
 */
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/kernel_stat.h>

#include <asm/irq.h>
#include <asm/addrspace.h>
#include <asm/time.h>
#include <asm/io.h>

#include <asm/sibyte/bcm1480_regs.h>
#include <asm/sibyte/sb1250_regs.h>
#include <asm/sibyte/bcm1480_int.h>
#include <asm/sibyte/bcm1480_scd.h>

#include <asm/sibyte/sb1250.h>


#define IMR_IP2_VAL	K_BCM1480_INT_MAP_I0
#define IMR_IP3_VAL	K_BCM1480_INT_MAP_I1
#define IMR_IP4_VAL	K_BCM1480_INT_MAP_I2

#ifdef CONFIG_SIMULATION
#define BCM1480_HPT_VALUE	50000
#else
#define BCM1480_HPT_VALUE	1000000
#endif

extern int bcm1480_steal_irq(int irq);

void bcm1480_time_init(void)
{
	int cpu = smp_processor_id();
	int irq = K_BCM1480_INT_TIMER_0+cpu;

	/* Only have 4 general purpose timers */
	if (cpu > 3) {
		BUG();
	}

	bcm1480_mask_irq(cpu, irq);

	/* Map the timer interrupt to ip[4] of this cpu */
	__raw_writeq(IMR_IP4_VAL, IOADDR(A_BCM1480_IMR_REGISTER(cpu, R_BCM1480_IMR_INTERRUPT_MAP_BASE_H)
	      + (irq<<3)));

	/* the general purpose timer ticks at 1 Mhz independent of the rest of the system */
	/* Disable the timer and set up the count */
	__raw_writeq(0, IOADDR(A_SCD_TIMER_REGISTER(cpu, R_SCD_TIMER_CFG)));
	__raw_writeq(
		BCM1480_HPT_VALUE/HZ
		, IOADDR(A_SCD_TIMER_REGISTER(cpu, R_SCD_TIMER_INIT)));

	/* Set the timer running */
	__raw_writeq(M_SCD_TIMER_ENABLE|M_SCD_TIMER_MODE_CONTINUOUS,
	      IOADDR(A_SCD_TIMER_REGISTER(cpu, R_SCD_TIMER_CFG)));

	bcm1480_unmask_irq(cpu, irq);
	bcm1480_steal_irq(irq);
	/*
	 * This interrupt is "special" in that it doesn't use the request_irq
	 * way to hook the irq line.  The timer interrupt is initialized early
	 * enough to make this a major pain, and it's also firing enough to
	 * warrant a bit of special case code.  bcm1480_timer_interrupt is
	 * called directly from irq_handler.S when IP[4] is set during an
	 * interrupt
	 */
}

void bcm1480_timer_interrupt(void)
{
	int cpu = smp_processor_id();
	int irq = K_BCM1480_INT_TIMER_0 + cpu;

	/* Reset the timer */
	__raw_writeq(M_SCD_TIMER_ENABLE|M_SCD_TIMER_MODE_CONTINUOUS,
	      IOADDR(A_SCD_TIMER_REGISTER(cpu, R_SCD_TIMER_CFG)));

	if (cpu == 0) {
		/*
		 * CPU 0 handles the global timer interrupt job
		 */
		ll_timer_interrupt(irq);
	}
	else {
		/*
		 * other CPUs should just do profiling and process accounting
		 */
		ll_local_timer_interrupt(irq);
	}
}

static cycle_t bcm1480_hpt_read(void)
{
	/* We assume this function is called xtime_lock held. */
	unsigned long count =
		__raw_readq(IOADDR(A_SCD_TIMER_REGISTER(0, R_SCD_TIMER_CNT)));
	return (jiffies + 1) * (BCM1480_HPT_VALUE / HZ) - count;
}

void __init bcm1480_hpt_setup(void)
{
	clocksource_mips.read = bcm1480_hpt_read;
	mips_hpt_frequency = BCM1480_HPT_VALUE;
}
