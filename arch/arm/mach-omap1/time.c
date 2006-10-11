/*
 * linux/arch/arm/mach-omap1/time.c
 *
 * OMAP Timers
 *
 * Copyright (C) 2004 Nokia Corporation
 * Partial timer rewrite and additional dynamic tick timer support by
 * Tony Lindgen <tony@atomide.com> and
 * Tuukka Tikkanen <tuukka.tikkanen@elektrobit.com>
 *
 * MPU timer code based on the older MPU timer code for OMAP
 * Copyright (C) 2000 RidgeRun, Inc.
 * Author: Greg Lonnon <glonnon@ridgerun.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/spinlock.h>

#include <asm/system.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/leds.h>
#include <asm/irq.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>

struct sys_timer omap_timer;

/*
 * ---------------------------------------------------------------------------
 * MPU timer
 * ---------------------------------------------------------------------------
 */
#define OMAP_MPU_TIMER_BASE		OMAP_MPU_TIMER1_BASE
#define OMAP_MPU_TIMER_OFFSET		0x100

/* cycles to nsec conversions taken from arch/i386/kernel/timers/timer_tsc.c,
 * converted to use kHz by Kevin Hilman */
/* convert from cycles(64bits) => nanoseconds (64bits)
 *  basic equation:
 *		ns = cycles / (freq / ns_per_sec)
 *		ns = cycles * (ns_per_sec / freq)
 *		ns = cycles * (10^9 / (cpu_khz * 10^3))
 *		ns = cycles * (10^6 / cpu_khz)
 *
 *	Then we use scaling math (suggested by george at mvista.com) to get:
 *		ns = cycles * (10^6 * SC / cpu_khz / SC
 *		ns = cycles * cyc2ns_scale / SC
 *
 *	And since SC is a constant power of two, we can convert the div
 *  into a shift.
 *			-johnstul at us.ibm.com "math is hard, lets go shopping!"
 */
static unsigned long cyc2ns_scale;
#define CYC2NS_SCALE_FACTOR 10 /* 2^10, carefully chosen */

static inline void set_cyc2ns_scale(unsigned long cpu_khz)
{
	cyc2ns_scale = (1000000 << CYC2NS_SCALE_FACTOR)/cpu_khz;
}

static inline unsigned long long cycles_2_ns(unsigned long long cyc)
{
	return (cyc * cyc2ns_scale) >> CYC2NS_SCALE_FACTOR;
}

/*
 * MPU_TICKS_PER_SEC must be an even number, otherwise machinecycles_to_usecs
 * will break. On P2, the timer count rate is 6.5 MHz after programming PTV
 * with 0. This divides the 13MHz input by 2, and is undocumented.
 */
#if defined(CONFIG_MACH_OMAP_PERSEUS2) || defined(CONFIG_MACH_OMAP_FSAMPLE)
/* REVISIT: This ifdef construct should be replaced by a query to clock
 * framework to see if timer base frequency is 12.0, 13.0 or 19.2 MHz.
 */
#define MPU_TICKS_PER_SEC		(13000000 / 2)
#else
#define MPU_TICKS_PER_SEC		(12000000 / 2)
#endif

#define MPU_TIMER_TICK_PERIOD		((MPU_TICKS_PER_SEC / HZ) - 1)

typedef struct {
	u32 cntl;			/* CNTL_TIMER, R/W */
	u32 load_tim;			/* LOAD_TIM,   W */
	u32 read_tim;			/* READ_TIM,   R */
} omap_mpu_timer_regs_t;

#define omap_mpu_timer_base(n)						\
((volatile omap_mpu_timer_regs_t*)IO_ADDRESS(OMAP_MPU_TIMER_BASE +	\
				 (n)*OMAP_MPU_TIMER_OFFSET))

static inline unsigned long omap_mpu_timer_read(int nr)
{
	volatile omap_mpu_timer_regs_t* timer = omap_mpu_timer_base(nr);
	return timer->read_tim;
}

static inline void omap_mpu_timer_start(int nr, unsigned long load_val)
{
	volatile omap_mpu_timer_regs_t* timer = omap_mpu_timer_base(nr);

	timer->cntl = MPU_TIMER_CLOCK_ENABLE;
	udelay(1);
	timer->load_tim = load_val;
        udelay(1);
	timer->cntl = (MPU_TIMER_CLOCK_ENABLE | MPU_TIMER_AR | MPU_TIMER_ST);
}

unsigned long omap_mpu_timer_ticks_to_usecs(unsigned long nr_ticks)
{
	unsigned long long nsec;

	nsec = cycles_2_ns((unsigned long long)nr_ticks);
	return (unsigned long)nsec / 1000;
}

/*
 * Last processed system timer interrupt
 */
static unsigned long omap_mpu_timer_last = 0;

/*
 * Returns elapsed usecs since last system timer interrupt
 */
static unsigned long omap_mpu_timer_gettimeoffset(void)
{
	unsigned long now = 0 - omap_mpu_timer_read(0);
	unsigned long elapsed = now - omap_mpu_timer_last;

	return omap_mpu_timer_ticks_to_usecs(elapsed);
}

/*
 * Elapsed time between interrupts is calculated using timer0.
 * Latency during the interrupt is calculated using timer1.
 * Both timer0 and timer1 are counting at 6MHz (P2 6.5MHz).
 */
static irqreturn_t omap_mpu_timer_interrupt(int irq, void *dev_id)
{
	unsigned long now, latency;

	write_seqlock(&xtime_lock);
	now = 0 - omap_mpu_timer_read(0);
	latency = MPU_TICKS_PER_SEC / HZ - omap_mpu_timer_read(1);
	omap_mpu_timer_last = now - latency;
	timer_tick();
	write_sequnlock(&xtime_lock);

	return IRQ_HANDLED;
}

static struct irqaction omap_mpu_timer_irq = {
	.name		= "mpu timer",
	.flags		= IRQF_DISABLED | IRQF_TIMER,
	.handler	= omap_mpu_timer_interrupt,
};

static unsigned long omap_mpu_timer1_overflows;
static irqreturn_t omap_mpu_timer1_interrupt(int irq, void *dev_id)
{
	omap_mpu_timer1_overflows++;
	return IRQ_HANDLED;
}

static struct irqaction omap_mpu_timer1_irq = {
	.name		= "mpu timer1 overflow",
	.flags		= IRQF_DISABLED,
	.handler	= omap_mpu_timer1_interrupt,
};

static __init void omap_init_mpu_timer(void)
{
	set_cyc2ns_scale(MPU_TICKS_PER_SEC / 1000);
	omap_timer.offset = omap_mpu_timer_gettimeoffset;
	setup_irq(INT_TIMER1, &omap_mpu_timer1_irq);
	setup_irq(INT_TIMER2, &omap_mpu_timer_irq);
	omap_mpu_timer_start(0, 0xffffffff);
	omap_mpu_timer_start(1, MPU_TIMER_TICK_PERIOD);
}

/*
 * Scheduler clock - returns current time in nanosec units.
 */
unsigned long long sched_clock(void)
{
	unsigned long ticks = 0 - omap_mpu_timer_read(0);
	unsigned long long ticks64;

	ticks64 = omap_mpu_timer1_overflows;
	ticks64 <<= 32;
	ticks64 |= ticks;

	return cycles_2_ns(ticks64);
}

/*
 * ---------------------------------------------------------------------------
 * Timer initialization
 * ---------------------------------------------------------------------------
 */
static void __init omap_timer_init(void)
{
	omap_init_mpu_timer();
}

struct sys_timer omap_timer = {
	.init		= omap_timer_init,
	.offset		= NULL,		/* Initialized later */
};
