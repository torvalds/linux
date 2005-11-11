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

#include <linux/config.h>
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

#ifdef CONFIG_OMAP_MPU_TIMER

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
#ifdef CONFIG_MACH_OMAP_PERSEUS2
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
static irqreturn_t omap_mpu_timer_interrupt(int irq, void *dev_id,
					struct pt_regs *regs)
{
	unsigned long now, latency;

	write_seqlock(&xtime_lock);
	now = 0 - omap_mpu_timer_read(0);
	latency = MPU_TICKS_PER_SEC / HZ - omap_mpu_timer_read(1);
	omap_mpu_timer_last = now - latency;
	timer_tick(regs);
	write_sequnlock(&xtime_lock);

	return IRQ_HANDLED;
}

static struct irqaction omap_mpu_timer_irq = {
	.name		= "mpu timer",
	.flags		= SA_INTERRUPT | SA_TIMER,
	.handler	= omap_mpu_timer_interrupt,
};

static unsigned long omap_mpu_timer1_overflows;
static irqreturn_t omap_mpu_timer1_interrupt(int irq, void *dev_id,
					     struct pt_regs *regs)
{
	omap_mpu_timer1_overflows++;
	return IRQ_HANDLED;
}

static struct irqaction omap_mpu_timer1_irq = {
	.name		= "mpu timer1 overflow",
	.flags		= SA_INTERRUPT,
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
#endif	/* CONFIG_OMAP_MPU_TIMER */

#ifdef CONFIG_OMAP_32K_TIMER

#ifdef CONFIG_ARCH_OMAP15XX
#error OMAP 32KHz timer does not currently work on 15XX!
#endif

/*
 * ---------------------------------------------------------------------------
 * 32KHz OS timer
 *
 * This currently works only on 16xx, as 1510 does not have the continuous
 * 32KHz synchronous timer. The 32KHz synchronous timer is used to keep track
 * of time in addition to the 32KHz OS timer. Using only the 32KHz OS timer
 * on 1510 would be possible, but the timer would not be as accurate as
 * with the 32KHz synchronized timer.
 * ---------------------------------------------------------------------------
 */
#define OMAP_32K_TIMER_BASE		0xfffb9000
#define OMAP_32K_TIMER_CR		0x08
#define OMAP_32K_TIMER_TVR		0x00
#define OMAP_32K_TIMER_TCR		0x04

#define OMAP_32K_TICKS_PER_HZ		(32768 / HZ)

/*
 * TRM says 1 / HZ = ( TVR + 1) / 32768, so TRV = (32768 / HZ) - 1
 * so with HZ = 100, TVR = 327.68.
 */
#define OMAP_32K_TIMER_TICK_PERIOD	((32768 / HZ) - 1)
#define TIMER_32K_SYNCHRONIZED		0xfffbc410

#define JIFFIES_TO_HW_TICKS(nr_jiffies, clock_rate)			\
				(((nr_jiffies) * (clock_rate)) / HZ)

static inline void omap_32k_timer_write(int val, int reg)
{
	omap_writew(val, reg + OMAP_32K_TIMER_BASE);
}

static inline unsigned long omap_32k_timer_read(int reg)
{
	return omap_readl(reg + OMAP_32K_TIMER_BASE) & 0xffffff;
}

/*
 * The 32KHz synchronized timer is an additional timer on 16xx.
 * It is always running.
 */
static inline unsigned long omap_32k_sync_timer_read(void)
{
	return omap_readl(TIMER_32K_SYNCHRONIZED);
}

static inline void omap_32k_timer_start(unsigned long load_val)
{
	omap_32k_timer_write(load_val, OMAP_32K_TIMER_TVR);
	omap_32k_timer_write(0x0f, OMAP_32K_TIMER_CR);
}

static inline void omap_32k_timer_stop(void)
{
	omap_32k_timer_write(0x0, OMAP_32K_TIMER_CR);
}

/*
 * Rounds down to nearest usec. Note that this will overflow for larger values.
 */
static inline unsigned long omap_32k_ticks_to_usecs(unsigned long ticks_32k)
{
	return (ticks_32k * 5*5*5*5*5*5) >> 9;
}

/*
 * Rounds down to nearest nsec.
 */
static inline unsigned long long
omap_32k_ticks_to_nsecs(unsigned long ticks_32k)
{
	return (unsigned long long) ticks_32k * 1000 * 5*5*5*5*5*5 >> 9;
}

static unsigned long omap_32k_last_tick = 0;

/*
 * Returns elapsed usecs since last 32k timer interrupt
 */
static unsigned long omap_32k_timer_gettimeoffset(void)
{
	unsigned long now = omap_32k_sync_timer_read();
	return omap_32k_ticks_to_usecs(now - omap_32k_last_tick);
}

/*
 * Returns current time from boot in nsecs. It's OK for this to wrap
 * around for now, as it's just a relative time stamp.
 */
unsigned long long sched_clock(void)
{
	return omap_32k_ticks_to_nsecs(omap_32k_sync_timer_read());
}

/*
 * Timer interrupt for 32KHz timer. When dynamic tick is enabled, this
 * function is also called from other interrupts to remove latency
 * issues with dynamic tick. In the dynamic tick case, we need to lock
 * with irqsave.
 */
static irqreturn_t omap_32k_timer_interrupt(int irq, void *dev_id,
					    struct pt_regs *regs)
{
	unsigned long flags;
	unsigned long now;

	write_seqlock_irqsave(&xtime_lock, flags);
	now = omap_32k_sync_timer_read();

	while (now - omap_32k_last_tick >= OMAP_32K_TICKS_PER_HZ) {
		omap_32k_last_tick += OMAP_32K_TICKS_PER_HZ;
		timer_tick(regs);
	}

	/* Restart timer so we don't drift off due to modulo or dynamic tick.
	 * By default we program the next timer to be continuous to avoid
	 * latencies during high system load. During dynamic tick operation the
	 * continuous timer can be overridden from pm_idle to be longer.
	 */
	omap_32k_timer_start(omap_32k_last_tick + OMAP_32K_TICKS_PER_HZ - now);
	write_sequnlock_irqrestore(&xtime_lock, flags);

	return IRQ_HANDLED;
}

#ifdef CONFIG_NO_IDLE_HZ
/*
 * Programs the next timer interrupt needed. Called when dynamic tick is
 * enabled, and to reprogram the ticks to skip from pm_idle. Note that
 * we can keep the timer continuous, and don't need to set it to run in
 * one-shot mode. This is because the timer will get reprogrammed again
 * after next interrupt.
 */
void omap_32k_timer_reprogram(unsigned long next_tick)
{
	omap_32k_timer_start(JIFFIES_TO_HW_TICKS(next_tick, 32768) + 1);
}

static struct irqaction omap_32k_timer_irq;
extern struct timer_update_handler timer_update;

static int omap_32k_timer_enable_dyn_tick(void)
{
	/* No need to reprogram timer, just use the next interrupt */
	return 0;
}

static int omap_32k_timer_disable_dyn_tick(void)
{
	omap_32k_timer_start(OMAP_32K_TIMER_TICK_PERIOD);
	return 0;
}

static struct dyn_tick_timer omap_dyn_tick_timer = {
	.enable		= omap_32k_timer_enable_dyn_tick,
	.disable	= omap_32k_timer_disable_dyn_tick,
	.reprogram	= omap_32k_timer_reprogram,
	.handler	= omap_32k_timer_interrupt,
};
#endif	/* CONFIG_NO_IDLE_HZ */

static struct irqaction omap_32k_timer_irq = {
	.name		= "32KHz timer",
	.flags		= SA_INTERRUPT | SA_TIMER,
	.handler	= omap_32k_timer_interrupt,
};

static __init void omap_init_32k_timer(void)
{

#ifdef CONFIG_NO_IDLE_HZ
	omap_timer.dyn_tick = &omap_dyn_tick_timer;
#endif

	setup_irq(INT_OS_TIMER, &omap_32k_timer_irq);
	omap_timer.offset  = omap_32k_timer_gettimeoffset;
	omap_32k_last_tick = omap_32k_sync_timer_read();
	omap_32k_timer_start(OMAP_32K_TIMER_TICK_PERIOD);
}
#endif	/* CONFIG_OMAP_32K_TIMER */

/*
 * ---------------------------------------------------------------------------
 * Timer initialization
 * ---------------------------------------------------------------------------
 */
static void __init omap_timer_init(void)
{
#if defined(CONFIG_OMAP_MPU_TIMER)
	omap_init_mpu_timer();
#elif defined(CONFIG_OMAP_32K_TIMER)
	omap_init_32k_timer();
#else
#error No system timer selected in Kconfig!
#endif
}

struct sys_timer omap_timer = {
	.init		= omap_timer_init,
	.offset		= NULL,		/* Initialized later */
};
