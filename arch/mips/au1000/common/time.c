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
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/hardirq.h>

#include <asm/compiler.h>
#include <asm/mipsregs.h>
#include <asm/time.h>
#include <asm/div64.h>
#include <asm/mach-au1x00/au1000.h>

#include <linux/mc146818rtc.h>
#include <linux/timex.h>

static unsigned long r4k_offset; /* Amount to increment compare reg each time */
static unsigned long r4k_cur;    /* What counter should be at next timer irq */
int	no_au1xxx_32khz;
extern int allow_au1k_wait; /* default off for CP0 Counter */

#ifdef CONFIG_PM
#if HZ < 100 || HZ > 1000
#error "unsupported HZ value! Must be in [100,1000]"
#endif
#define MATCH20_INC (328*100/HZ) /* magic number 328 is for HZ=100... */
extern void startup_match20_interrupt(irq_handler_t handler);
static unsigned long last_pc0, last_match20;
#endif

static DEFINE_SPINLOCK(time_lock);

unsigned long wtimer;

#ifdef CONFIG_PM
irqreturn_t counter0_irq(int irq, void *dev_id)
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
		do_timer(1);
#ifndef CONFIG_SMP
		update_process_times(user_mode(get_irq_regs()));
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
		do_timer(1); /* increment jiffies by one */
#ifndef CONFIG_SMP
		update_process_times(user_mode(get_irq_regs()));
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
#define AU1000_SRC_CLK	12000000

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
		au_writel(0, SYS_TOYWRITE);
		while (au_readl(SYS_COUNTER_CNTRL) & SYS_CNTRL_C1S);

		cpu_speed = (au_readl(SYS_CPUPLL) & 0x0000003f) *
			AU1000_SRC_CLK;
	}
	else {
		/* The 32KHz oscillator isn't running, so assume there
		 * isn't one and grab the processor speed from the PLL.
		 * NOTE: some old silicon doesn't allow reading the PLL.
		 */
		cpu_speed = (au_readl(SYS_CPUPLL) & 0x0000003f) * AU1000_SRC_CLK;
		no_au1xxx_32khz = 1;
	}
	mips_hpt_frequency = cpu_speed;
	// Equation: Baudrate = CPU / (SD * 2 * CLKDIV * 16)
	set_au1x00_uart_baud_base(cpu_speed / (2 * ((int)(au_readl(SYS_POWERCTRL)&0x03) + 2) * 16));
	spin_unlock_irqrestore(&time_lock, flags);
	return (cpu_speed / HZ);
}

void __init plat_timer_setup(struct irqaction *irq)
{
	unsigned int est_freq;

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

		/* We can use the real 'wait' instruction.
		*/
		allow_au1k_wait = 1;
	}

#endif
}
