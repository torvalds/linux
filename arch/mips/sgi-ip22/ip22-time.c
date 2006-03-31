/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Time operations for IP22 machines. Original code may come from
 * Ralf Baechle or David S. Miller (sorry guys, i'm really not sure)
 *
 * Copyright (C) 2001 by Ladislav Michl
 * Copyright (C) 2003 Ralf Baechle (ralf@linux-mips.org)
 */
#include <linux/bcd.h>
#include <linux/ds1286.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/time.h>

#include <asm/cpu.h>
#include <asm/mipsregs.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/time.h>
#include <asm/sgialib.h>
#include <asm/sgi/ioc.h>
#include <asm/sgi/hpc3.h>
#include <asm/sgi/ip22.h>

/*
 * note that mktime uses month from 1 to 12 while to_tm
 * uses 0 to 11.
 */
static unsigned long indy_rtc_get_time(void)
{
	unsigned int yrs, mon, day, hrs, min, sec;
	unsigned int save_control;
	unsigned long flags;

	spin_lock_irqsave(&rtc_lock, flags);
	save_control = hpc3c0->rtcregs[RTC_CMD] & 0xff;
	hpc3c0->rtcregs[RTC_CMD] = save_control | RTC_TE;

	sec = BCD2BIN(hpc3c0->rtcregs[RTC_SECONDS] & 0xff);
	min = BCD2BIN(hpc3c0->rtcregs[RTC_MINUTES] & 0xff);
	hrs = BCD2BIN(hpc3c0->rtcregs[RTC_HOURS] & 0x3f);
	day = BCD2BIN(hpc3c0->rtcregs[RTC_DATE] & 0xff);
	mon = BCD2BIN(hpc3c0->rtcregs[RTC_MONTH] & 0x1f);
	yrs = BCD2BIN(hpc3c0->rtcregs[RTC_YEAR] & 0xff);

	hpc3c0->rtcregs[RTC_CMD] = save_control;
	spin_unlock_irqrestore(&rtc_lock, flags);

	if (yrs < 45)
		yrs += 30;
	if ((yrs += 40) < 70)
		yrs += 100;

	return mktime(yrs + 1900, mon, day, hrs, min, sec);
}

static int indy_rtc_set_time(unsigned long tim)
{
	struct rtc_time tm;
	unsigned int save_control;
	unsigned long flags;

	to_tm(tim, &tm);

	tm.tm_mon += 1;		/* tm_mon starts at zero */
	tm.tm_year -= 1940;
	if (tm.tm_year >= 100)
		tm.tm_year -= 100;

	spin_lock_irqsave(&rtc_lock, flags);
	save_control = hpc3c0->rtcregs[RTC_CMD] & 0xff;
	hpc3c0->rtcregs[RTC_CMD] = save_control | RTC_TE;

	hpc3c0->rtcregs[RTC_YEAR] = BIN2BCD(tm.tm_sec);
	hpc3c0->rtcregs[RTC_MONTH] = BIN2BCD(tm.tm_mon);
	hpc3c0->rtcregs[RTC_DATE] = BIN2BCD(tm.tm_mday);
	hpc3c0->rtcregs[RTC_HOURS] = BIN2BCD(tm.tm_hour);
	hpc3c0->rtcregs[RTC_MINUTES] = BIN2BCD(tm.tm_min);
	hpc3c0->rtcregs[RTC_SECONDS] = BIN2BCD(tm.tm_sec);
	hpc3c0->rtcregs[RTC_HUNDREDTH_SECOND] = 0;

	hpc3c0->rtcregs[RTC_CMD] = save_control;
	spin_unlock_irqrestore(&rtc_lock, flags);

	return 0;
}

static unsigned long dosample(void)
{
	u32 ct0, ct1;
	volatile u8 msb, lsb;

	/* Start the counter. */
	sgint->tcword = (SGINT_TCWORD_CNT2 | SGINT_TCWORD_CALL |
			 SGINT_TCWORD_MRGEN);
	sgint->tcnt2 = SGINT_TCSAMP_COUNTER & 0xff;
	sgint->tcnt2 = SGINT_TCSAMP_COUNTER >> 8;

	/* Get initial counter invariant */
	ct0 = read_c0_count();

	/* Latch and spin until top byte of counter2 is zero */
	do {
		sgint->tcword = SGINT_TCWORD_CNT2 | SGINT_TCWORD_CLAT;
		lsb = sgint->tcnt2;
		msb = sgint->tcnt2;
		ct1 = read_c0_count();
	} while (msb);

	/* Stop the counter. */
	sgint->tcword = (SGINT_TCWORD_CNT2 | SGINT_TCWORD_CALL |
			 SGINT_TCWORD_MSWST);
	/*
	 * Return the difference, this is how far the r4k counter increments
	 * for every 1/HZ seconds. We round off the nearest 1 MHz of master
	 * clock (= 1000000 / HZ / 2).
	 */
	/*return (ct1 - ct0 + (500000/HZ/2)) / (500000/HZ) * (500000/HZ);*/
	return (ct1 - ct0) / (500000/HZ) * (500000/HZ);
}

/*
 * Here we need to calibrate the cycle counter to at least be close.
 */
static __init void indy_time_init(void)
{
	unsigned long r4k_ticks[3];
	unsigned long r4k_tick;

	/*
	 * Figure out the r4k offset, the algorithm is very simple and works in
	 * _all_ cases as long as the 8254 counter register itself works ok (as
	 * an interrupt driving timer it does not because of bug, this is why
	 * we are using the onchip r4k counter/compare register to serve this
	 * purpose, but for r4k_offset calculation it will work ok for us).
	 * There are other very complicated ways of performing this calculation
	 * but this one works just fine so I am not going to futz around. ;-)
	 */
	printk(KERN_INFO "Calibrating system timer... ");
	dosample();	/* Prime cache. */
	dosample();	/* Prime cache. */
	/* Zero is NOT an option. */
	do {
		r4k_ticks[0] = dosample();
	} while (!r4k_ticks[0]);
	do {
		r4k_ticks[1] = dosample();
	} while (!r4k_ticks[1]);

	if (r4k_ticks[0] != r4k_ticks[1]) {
		printk("warning: timer counts differ, retrying... ");
		r4k_ticks[2] = dosample();
		if (r4k_ticks[2] == r4k_ticks[0]
		    || r4k_ticks[2] == r4k_ticks[1])
			r4k_tick = r4k_ticks[2];
		else {
			printk("disagreement, using average... ");
			r4k_tick = (r4k_ticks[0] + r4k_ticks[1]
				   + r4k_ticks[2]) / 3;
		}
	} else
		r4k_tick = r4k_ticks[0];

	printk("%d [%d.%04d MHz CPU]\n", (int) r4k_tick,
		(int) (r4k_tick / (500000 / HZ)),
		(int) (r4k_tick % (500000 / HZ)));

	mips_hpt_frequency = r4k_tick * HZ;
}

/* Generic SGI handler for (spurious) 8254 interrupts */
void indy_8254timer_irq(struct pt_regs *regs)
{
	int irq = SGI_8254_0_IRQ;
	ULONG cnt;
	char c;

	irq_enter();
	kstat_this_cpu.irqs[irq]++;
	printk(KERN_ALERT "Oops, got 8254 interrupt.\n");
	ArcRead(0, &c, 1, &cnt);
	ArcEnterInteractiveMode();
	irq_exit();
}

void indy_r4k_timer_interrupt(struct pt_regs *regs)
{
	int irq = SGI_TIMER_IRQ;

	irq_enter();
	kstat_this_cpu.irqs[irq]++;
	timer_interrupt(irq, NULL, regs);
	irq_exit();
}

extern int setup_irq(unsigned int irq, struct irqaction *irqaction);

static void indy_timer_setup(struct irqaction *irq)
{
	/* over-write the handler, we use our own way */
	irq->handler = no_action;

	/* setup irqaction */
	setup_irq(SGI_TIMER_IRQ, irq);
}

void __init ip22_time_init(void)
{
	/* setup hookup functions */
	rtc_mips_get_time = indy_rtc_get_time;
	rtc_mips_set_time = indy_rtc_set_time;

	board_time_init = indy_time_init;
	board_timer_setup = indy_timer_setup;
}
