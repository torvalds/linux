/*
 *
 * Copyright (C) 2001 MontaVista Software, ppopov@mvista.com
 * Copied and modified Carsten Langgaard's time.c
 *
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
 *
 * ########################################################################
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 *
 * Setting up the clock on the MIPS boards.
 *
 * Update.  Always configure the kernel with CONFIG_NEW_TIME_C.  This
 * will use the user interface gettimeofday() functions from the
 * arch/mips/kernel/time.c, and we provide the clock interrupt processing
 * and the timer offset compute functions.  If CONFIG_PM is selected,
 * we also ensure the 32KHz timer is available.   -- Dan
 */

#include <linux/types.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/hardirq.h>

#include <asm/compiler.h>
#include <asm/mipsregs.h>
#include <asm/ptrace.h>
#include <asm/time.h>
#include <asm/div64.h>
#include <asm/mach-au1x00/au1000.h>

#include <linux/mc146818rtc.h>
#include <linux/timex.h>

extern void do_softirq(void);
extern volatile unsigned long wall_jiffies;
unsigned long missed_heart_beats = 0;

static unsigned long r4k_offset; /* Amount to increment compare reg each time */
static unsigned long r4k_cur;    /* What counter should be at next timer irq */
int	no_au1xxx_32khz;
extern int allow_au1k_wait; /* default off for CP0 Counter */

/* Cycle counter value at the previous timer interrupt.. */
static unsigned int timerhi = 0, timerlo = 0;

#ifdef CONFIG_PM
#if HZ < 100 || HZ > 1000
#error "unsupported HZ value! Must be in [100,1000]"
#endif
#define MATCH20_INC (328*100/HZ) /* magic number 328 is for HZ=100... */
extern void startup_match20_interrupt(irqreturn_t (*handler)(int, void *, struct pt_regs *));
static unsigned long last_pc0, last_match20;
#endif

static DEFINE_SPINLOCK(time_lock);

static inline void ack_r4ktimer(unsigned long newval)
{
	write_c0_compare(newval);
}

/*
 * There are a lot of conceptually broken versions of the MIPS timer interrupt
 * handler floating around.  This one is rather different, but the algorithm
 * is provably more robust.
 */
unsigned long wtimer;
void mips_timer_interrupt(struct pt_regs *regs)
{
	int irq = 63;
	unsigned long count;

	irq_enter();
	kstat_this_cpu.irqs[irq]++;

	if (r4k_offset == 0)
		goto null;

	do {
		count = read_c0_count();
		timerhi += (count < timerlo);   /* Wrap around */
		timerlo = count;

		kstat_this_cpu.irqs[irq]++;
		do_timer(regs);
#ifndef CONFIG_SMP
		update_process_times(user_mode(regs));
#endif
		r4k_cur += r4k_offset;
		ack_r4ktimer(r4k_cur);

	} while (((unsigned long)read_c0_count()
	         - r4k_cur) < 0x7fffffff);

	irq_exit();
	return;

null:
	ack_r4ktimer(0);
	irq_exit();
}

#ifdef CONFIG_PM
irqreturn_t counter0_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned long pc0;
	int time_elapsed;
	static int jiffie_drift = 0;

	if (au_readl(SYS_COUNTER_CNTRL) & SYS_CNTRL_M20) {
		/* should never happen! */
		printk(KERN_WARNING "counter 0 w status error\n");
		return IRQ_NONE;
	}

	pc0 = au_readl(SYS_TOYREAD);
	if (pc0 < last_match20) {
		/* counter overflowed */
		time_elapsed = (0xffffffff - last_match20) + pc0;
	}
	else {
		time_elapsed = pc0 - last_match20;
	}

	while (time_elapsed > 0) {
		do_timer(regs);
#ifndef CONFIG_SMP
		update_process_times(user_mode(regs));
#endif
		time_elapsed -= MATCH20_INC;
		last_match20 += MATCH20_INC;
		jiffie_drift++;
	}

	last_pc0 = pc0;
	au_writel(last_match20 + MATCH20_INC, SYS_TOYMATCH2);
	au_sync();

	/* our counter ticks at 10.009765625 ms/tick, we we're running
	 * almost 10uS too slow per tick.
	 */

	if (jiffie_drift >= 999) {
		jiffie_drift -= 999;
		do_timer(regs); /* increment jiffies by one */
#ifndef CONFIG_SMP
		update_process_times(user_mode(regs));
#endif
	}

	return IRQ_HANDLED;
}

/* When we wakeup from sleep, we have to "catch up" on all of the
 * timer ticks we have missed.
 */
void
wakeup_counter0_adjust(void)
{
	unsigned long pc0;
	int time_elapsed;

	pc0 = au_readl(SYS_TOYREAD);
	if (pc0 < last_match20) {
		/* counter overflowed */
		time_elapsed = (0xffffffff - last_match20) + pc0;
	}
	else {
		time_elapsed = pc0 - last_match20;
	}

	while (time_elapsed > 0) {
		time_elapsed -= MATCH20_INC;
		last_match20 += MATCH20_INC;
	}

	last_pc0 = pc0;
	au_writel(last_match20 + MATCH20_INC, SYS_TOYMATCH2);
	au_sync();

}

/* This is just for debugging to set the timer for a sleep delay.
*/
void
wakeup_counter0_set(int ticks)
{
	unsigned long pc0;

	pc0 = au_readl(SYS_TOYREAD);
	last_pc0 = pc0;
	au_writel(last_match20 + (MATCH20_INC * ticks), SYS_TOYMATCH2);
	au_sync();
}
#endif

/* I haven't found anyone that doesn't use a 12 MHz source clock,
 * but just in case.....
 */
#ifdef CONFIG_AU1000_SRC_CLK
#define AU1000_SRC_CLK	CONFIG_AU1000_SRC_CLK
#else
#define AU1000_SRC_CLK	12000000
#endif

/*
 * We read the real processor speed from the PLL.  This is important
 * because it is more accurate than computing it from the 32KHz
 * counter, if it exists.  If we don't have an accurate processor
 * speed, all of the peripherals that derive their clocks based on
 * this advertised speed will introduce error and sometimes not work
 * properly.  This function is futher convoluted to still allow configurations
 * to do that in case they have really, really old silicon with a
 * write-only PLL register, that we need the 32KHz when power management
 * "wait" is enabled, and we need to detect if the 32KHz isn't present
 * but requested......got it? :-)		-- Dan
 */
unsigned long cal_r4koff(void)
{
	unsigned long count;
	unsigned long cpu_speed;
	unsigned long flags;
	unsigned long counter;

	spin_lock_irqsave(&time_lock, flags);

	/* Power management cares if we don't have a 32KHz counter.
	*/
	no_au1xxx_32khz = 0;
	counter = au_readl(SYS_COUNTER_CNTRL);
	if (counter & SYS_CNTRL_E0) {
		int trim_divide = 16;

		au_writel(counter | SYS_CNTRL_EN1, SYS_COUNTER_CNTRL);

		while (au_readl(SYS_COUNTER_CNTRL) & SYS_CNTRL_T1S);
		/* RTC now ticks at 32.768/16 kHz */
		au_writel(trim_divide-1, SYS_RTCTRIM);
		while (au_readl(SYS_COUNTER_CNTRL) & SYS_CNTRL_T1S);

		while (au_readl(SYS_COUNTER_CNTRL) & SYS_CNTRL_C1S);
		au_writel (0, SYS_TOYWRITE);
		while (au_readl(SYS_COUNTER_CNTRL) & SYS_CNTRL_C1S);

#if defined(CONFIG_AU1000_USE32K)
		{
			unsigned long start, end;

			start = au_readl(SYS_RTCREAD);
			start += 2;
			/* wait for the beginning of a new tick
			*/
			while (au_readl(SYS_RTCREAD) < start);

			/* Start r4k counter.
			*/
			write_c0_count(0);

			/* Wait 0.5 seconds.
			*/
			end = start + (32768 / trim_divide)/2;

			while (end > au_readl(SYS_RTCREAD));

			count = read_c0_count();
			cpu_speed = count * 2;
		}
#else
		cpu_speed = (au_readl(SYS_CPUPLL) & 0x0000003f) *
			AU1000_SRC_CLK;
		count = cpu_speed / 2;
#endif
	}
	else {
		/* The 32KHz oscillator isn't running, so assume there
		 * isn't one and grab the processor speed from the PLL.
		 * NOTE: some old silicon doesn't allow reading the PLL.
		 */
		cpu_speed = (au_readl(SYS_CPUPLL) & 0x0000003f) * AU1000_SRC_CLK;
		count = cpu_speed / 2;
		no_au1xxx_32khz = 1;
	}
	mips_hpt_frequency = count;
	// Equation: Baudrate = CPU / (SD * 2 * CLKDIV * 16)
	set_au1x00_uart_baud_base(cpu_speed / (2 * ((int)(au_readl(SYS_POWERCTRL)&0x03) + 2) * 16));
	spin_unlock_irqrestore(&time_lock, flags);
	return (cpu_speed / HZ);
}

/* This is for machines which generate the exact clock. */
#define USECS_PER_JIFFY (1000000/HZ)
#define USECS_PER_JIFFY_FRAC (0x100000000LL*1000000/HZ&0xffffffff)

static unsigned long
div64_32(unsigned long v1, unsigned long v2, unsigned long v3)
{
	unsigned long r0;
	do_div64_32(r0, v1, v2, v3);
	return r0;
}

static unsigned long do_fast_cp0_gettimeoffset(void)
{
	u32 count;
	unsigned long res, tmp;
	unsigned long r0;

	/* Last jiffy when do_fast_gettimeoffset() was called. */
	static unsigned long last_jiffies=0;
	unsigned long quotient;

	/*
	 * Cached "1/(clocks per usec)*2^32" value.
	 * It has to be recalculated once each jiffy.
	 */
	static unsigned long cached_quotient=0;

	tmp = jiffies;

	quotient = cached_quotient;

	if (tmp && last_jiffies != tmp) {
		last_jiffies = tmp;
		if (last_jiffies != 0) {
			r0 = div64_32(timerhi, timerlo, tmp);
			quotient = div64_32(USECS_PER_JIFFY, USECS_PER_JIFFY_FRAC, r0);
			cached_quotient = quotient;
		}
	}

	/* Get last timer tick in absolute kernel time */
	count = read_c0_count();

	/* .. relative to previous jiffy (32 bits is enough) */
	count -= timerlo;

	__asm__("multu\t%1,%2\n\t"
		"mfhi\t%0"
		: "=r" (res)
		: "r" (count), "r" (quotient)
		: "hi", "lo", GCC_REG_ACCUM);

	/*
	 * Due to possible jiffies inconsistencies, we need to check
	 * the result so that we'll get a timer that is monotonic.
	 */
	if (res >= USECS_PER_JIFFY)
		res = USECS_PER_JIFFY-1;

	return res;
}

#ifdef CONFIG_PM
static unsigned long do_fast_pm_gettimeoffset(void)
{
	unsigned long pc0;
	unsigned long offset;

	pc0 = au_readl(SYS_TOYREAD);
	au_sync();
	offset = pc0 - last_pc0;
	if (offset > 2*MATCH20_INC) {
		printk("huge offset %x, last_pc0 %x last_match20 %x pc0 %x\n",
				(unsigned)offset, (unsigned)last_pc0,
				(unsigned)last_match20, (unsigned)pc0);
	}
	offset = (unsigned long)((offset * 305) / 10);
	return offset;
}
#endif

void au1xxx_timer_setup(struct irqaction *irq)
{
        unsigned int est_freq;
	extern unsigned long (*do_gettimeoffset)(void);

	printk("calculating r4koff... ");
	r4k_offset = cal_r4koff();
	printk("%08lx(%d)\n", r4k_offset, (int) r4k_offset);

	//est_freq = 2*r4k_offset*HZ;
	est_freq = r4k_offset*HZ;
	est_freq += 5000;    /* round */
	est_freq -= est_freq%10000;
	printk("CPU frequency %d.%02d MHz\n", est_freq/1000000,
	       (est_freq%1000000)*100/1000000);
 	set_au1x00_speed(est_freq);
 	set_au1x00_lcd_clock(); // program the LCD clock

	r4k_cur = (read_c0_count() + r4k_offset);
	write_c0_compare(r4k_cur);

#ifdef CONFIG_PM
	/*
	 * setup counter 0, since it keeps ticking after a
	 * 'wait' instruction has been executed. The CP0 timer and
	 * counter 1 do NOT continue running after 'wait'
	 *
	 * It's too early to call request_irq() here, so we handle
	 * counter 0 interrupt as a special irq and it doesn't show
	 * up under /proc/interrupts.
	 *
	 * Check to ensure we really have a 32KHz oscillator before
	 * we do this.
	 */
	if (no_au1xxx_32khz) {
		unsigned int c0_status;

		printk("WARNING: no 32KHz clock found.\n");
		do_gettimeoffset = do_fast_cp0_gettimeoffset;

		/* Ensure we get CPO_COUNTER interrupts.
		*/
		c0_status = read_c0_status();
		c0_status |= IE_IRQ5;
		write_c0_status(c0_status);
	}
	else {
		while (au_readl(SYS_COUNTER_CNTRL) & SYS_CNTRL_C0S);
		au_writel(0, SYS_TOYWRITE);
		while (au_readl(SYS_COUNTER_CNTRL) & SYS_CNTRL_C0S);

		au_writel(au_readl(SYS_WAKEMSK) | (1<<8), SYS_WAKEMSK);
		au_writel(~0, SYS_WAKESRC);
		au_sync();
		while (au_readl(SYS_COUNTER_CNTRL) & SYS_CNTRL_M20);

		/* setup match20 to interrupt once every HZ */
		last_pc0 = last_match20 = au_readl(SYS_TOYREAD);
		au_writel(last_match20 + MATCH20_INC, SYS_TOYMATCH2);
		au_sync();
		while (au_readl(SYS_COUNTER_CNTRL) & SYS_CNTRL_M20);
		startup_match20_interrupt(counter0_irq);

		do_gettimeoffset = do_fast_pm_gettimeoffset;

		/* We can use the real 'wait' instruction.
		*/
		allow_au1k_wait = 1;
	}

#else
	/* We have to do this here instead of in timer_init because
	 * the generic code in arch/mips/kernel/time.c will write
	 * over our function pointer.
	 */
	do_gettimeoffset = do_fast_cp0_gettimeoffset;
#endif
}

void __init au1xxx_time_init(void)
{
}
