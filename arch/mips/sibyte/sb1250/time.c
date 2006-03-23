/*
 * Copyright (C) 2000, 2001 Broadcom Corporation
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
 * sb1250 general purpose timer 0.  We're using the timer as a
 * system clock, so we set it up to run at 100 Hz.  On every
 * interrupt, we update our idea of what the time of day is,
 * then call do_timer() in the architecture-independent kernel
 * code to do general bookkeeping (e.g. update jiffies, run
 * bottom halves, etc.)
 */
#include <linux/config.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/kernel_stat.h>

#include <asm/irq.h>
#include <asm/ptrace.h>
#include <asm/addrspace.h>
#include <asm/time.h>
#include <asm/io.h>

#include <asm/sibyte/sb1250.h>
#include <asm/sibyte/sb1250_regs.h>
#include <asm/sibyte/sb1250_int.h>
#include <asm/sibyte/sb1250_scd.h>


#define IMR_IP2_VAL	K_INT_MAP_I0
#define IMR_IP3_VAL	K_INT_MAP_I1
#define IMR_IP4_VAL	K_INT_MAP_I2

#define SB1250_HPT_NUM		3
#define SB1250_HPT_VALUE	M_SCD_TIMER_CNT /* max value */
#define SB1250_HPT_SHIFT	((sizeof(unsigned int)*8)-V_SCD_TIMER_WIDTH)


extern int sb1250_steal_irq(int irq);

static unsigned int sb1250_hpt_read(void);
static void sb1250_hpt_init(unsigned int);

static unsigned int hpt_offset;

void __init sb1250_hpt_setup(void)
{
	int cpu = smp_processor_id();

	if (!cpu) {
		/* Setup hpt using timer #3 but do not enable irq for it */
		__raw_writeq(0, IOADDR(A_SCD_TIMER_REGISTER(SB1250_HPT_NUM, R_SCD_TIMER_CFG)));
		__raw_writeq(SB1250_HPT_VALUE,
			     IOADDR(A_SCD_TIMER_REGISTER(SB1250_HPT_NUM, R_SCD_TIMER_INIT)));
		__raw_writeq(M_SCD_TIMER_ENABLE | M_SCD_TIMER_MODE_CONTINUOUS,
			     IOADDR(A_SCD_TIMER_REGISTER(SB1250_HPT_NUM, R_SCD_TIMER_CFG)));

		/*
		 * we need to fill 32 bits, so just use the upper 23 bits and pretend
		 * the timer is going 512Mhz instead of 1Mhz
		 */
		mips_hpt_frequency = V_SCD_TIMER_FREQ << SB1250_HPT_SHIFT;
		mips_hpt_init = sb1250_hpt_init;
		mips_hpt_read = sb1250_hpt_read;
	}
}


void sb1250_time_init(void)
{
	int cpu = smp_processor_id();
	int irq = K_INT_TIMER_0+cpu;

	/* Only have 4 general purpose timers, and we use last one as hpt */
	if (cpu > 2) {
		BUG();
	}

	sb1250_mask_irq(cpu, irq);

	/* Map the timer interrupt to ip[4] of this cpu */
	__raw_writeq(IMR_IP4_VAL,
		     IOADDR(A_IMR_REGISTER(cpu, R_IMR_INTERRUPT_MAP_BASE) +
			    (irq << 3)));

	/* the general purpose timer ticks at 1 Mhz independent if the rest of the system */
	/* Disable the timer and set up the count */
	__raw_writeq(0, IOADDR(A_SCD_TIMER_REGISTER(cpu, R_SCD_TIMER_CFG)));
#ifdef CONFIG_SIMULATION
	__raw_writeq((50000 / HZ) - 1,
		     IOADDR(A_SCD_TIMER_REGISTER(cpu, R_SCD_TIMER_INIT)));
#else
	__raw_writeq((V_SCD_TIMER_FREQ / HZ) - 1,
		     IOADDR(A_SCD_TIMER_REGISTER(cpu, R_SCD_TIMER_INIT)));
#endif

	/* Set the timer running */
	__raw_writeq(M_SCD_TIMER_ENABLE | M_SCD_TIMER_MODE_CONTINUOUS,
		     IOADDR(A_SCD_TIMER_REGISTER(cpu, R_SCD_TIMER_CFG)));

	sb1250_unmask_irq(cpu, irq);
	sb1250_steal_irq(irq);
	/*
	 * This interrupt is "special" in that it doesn't use the request_irq
	 * way to hook the irq line.  The timer interrupt is initialized early
	 * enough to make this a major pain, and it's also firing enough to
	 * warrant a bit of special case code.  sb1250_timer_interrupt is
	 * called directly from irq_handler.S when IP[4] is set during an
	 * interrupt
	 */
}

void sb1250_timer_interrupt(struct pt_regs *regs)
{
	int cpu = smp_processor_id();
	int irq = K_INT_TIMER_0 + cpu;

	/* ACK interrupt */
	____raw_writeq(M_SCD_TIMER_ENABLE | M_SCD_TIMER_MODE_CONTINUOUS,
		       IOADDR(A_SCD_TIMER_REGISTER(cpu, R_SCD_TIMER_CFG)));

	if (cpu == 0) {
		/*
		 * CPU 0 handles the global timer interrupt job
		 */
		ll_timer_interrupt(irq, regs);
	}
	else {
		/*
		 * other CPUs should just do profiling and process accounting
		 */
		ll_local_timer_interrupt(irq, regs);
	}
}

/*
 * The HPT is free running from SB1250_HPT_VALUE down to 0 then starts over
 * again. There's no easy way to set to a specific value so store init value
 * in hpt_offset and subtract each time.
 *
 * Note: Timer isn't full 32bits so shift it into the upper part making
 *       it appear to run at a higher frequency.
 */
static unsigned int sb1250_hpt_read(void)
{
	unsigned int count;

	count = G_SCD_TIMER_CNT(__raw_readq(IOADDR(A_SCD_TIMER_REGISTER(SB1250_HPT_NUM, R_SCD_TIMER_CNT))));

	count = (SB1250_HPT_VALUE - count) << SB1250_HPT_SHIFT;

	return count - hpt_offset;
}

static void sb1250_hpt_init(unsigned int count)
{
	hpt_offset = count;
	return;
}
