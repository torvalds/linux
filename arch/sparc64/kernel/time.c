/* $Id: time.c,v 1.42 2002/01/23 14:33:55 davem Exp $
 * time.c: UltraSparc timer and TOD clock support.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1998 Eddie C. Dost   (ecd@skynet.be)
 *
 * Based largely on code which is:
 *
 * Copyright (C) 1996 Thomas K. Dyas (tdyas@eden.rutgers.edu)
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/mc146818rtc.h>
#include <linux/delay.h>
#include <linux/profile.h>
#include <linux/bcd.h>
#include <linux/jiffies.h>
#include <linux/cpufreq.h>
#include <linux/percpu.h>
#include <linux/profile.h>
#include <linux/miscdevice.h>
#include <linux/rtc.h>

#include <asm/oplib.h>
#include <asm/mostek.h>
#include <asm/timer.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/sbus.h>
#include <asm/fhc.h>
#include <asm/pbm.h>
#include <asm/ebus.h>
#include <asm/isa.h>
#include <asm/starfire.h>
#include <asm/smp.h>
#include <asm/sections.h>
#include <asm/cpudata.h>
#include <asm/uaccess.h>

DEFINE_SPINLOCK(mostek_lock);
DEFINE_SPINLOCK(rtc_lock);
void __iomem *mstk48t02_regs = NULL;
#ifdef CONFIG_PCI
unsigned long ds1287_regs = 0UL;
#endif

extern unsigned long wall_jiffies;

static void __iomem *mstk48t08_regs;
static void __iomem *mstk48t59_regs;

static int set_rtc_mmss(unsigned long);

#define TICK_PRIV_BIT	(1UL << 63)

#ifdef CONFIG_SMP
unsigned long profile_pc(struct pt_regs *regs)
{
	unsigned long pc = instruction_pointer(regs);

	if (in_lock_functions(pc))
		return regs->u_regs[UREG_RETPC];
	return pc;
}
EXPORT_SYMBOL(profile_pc);
#endif

static void tick_disable_protection(void)
{
	/* Set things up so user can access tick register for profiling
	 * purposes.  Also workaround BB_ERRATA_1 by doing a dummy
	 * read back of %tick after writing it.
	 */
	__asm__ __volatile__(
	"	ba,pt	%%xcc, 1f\n"
	"	 nop\n"
	"	.align	64\n"
	"1:	rd	%%tick, %%g2\n"
	"	add	%%g2, 6, %%g2\n"
	"	andn	%%g2, %0, %%g2\n"
	"	wrpr	%%g2, 0, %%tick\n"
	"	rdpr	%%tick, %%g0"
	: /* no outputs */
	: "r" (TICK_PRIV_BIT)
	: "g2");
}

static void tick_init_tick(unsigned long offset)
{
	tick_disable_protection();

	__asm__ __volatile__(
	"	rd	%%tick, %%g1\n"
	"	andn	%%g1, %1, %%g1\n"
	"	ba,pt	%%xcc, 1f\n"
	"	 add	%%g1, %0, %%g1\n"
	"	.align	64\n"
	"1:	wr	%%g1, 0x0, %%tick_cmpr\n"
	"	rd	%%tick_cmpr, %%g0"
	: /* no outputs */
	: "r" (offset), "r" (TICK_PRIV_BIT)
	: "g1");
}

static unsigned long tick_get_tick(void)
{
	unsigned long ret;

	__asm__ __volatile__("rd	%%tick, %0\n\t"
			     "mov	%0, %0"
			     : "=r" (ret));

	return ret & ~TICK_PRIV_BIT;
}

static unsigned long tick_get_compare(void)
{
	unsigned long ret;

	__asm__ __volatile__("rd	%%tick_cmpr, %0\n\t"
			     "mov	%0, %0"
			     : "=r" (ret));

	return ret;
}

static unsigned long tick_add_compare(unsigned long adj)
{
	unsigned long new_compare;

	/* Workaround for Spitfire Errata (#54 I think??), I discovered
	 * this via Sun BugID 4008234, mentioned in Solaris-2.5.1 patch
	 * number 103640.
	 *
	 * On Blackbird writes to %tick_cmpr can fail, the
	 * workaround seems to be to execute the wr instruction
	 * at the start of an I-cache line, and perform a dummy
	 * read back from %tick_cmpr right after writing to it. -DaveM
	 */
	__asm__ __volatile__("rd	%%tick_cmpr, %0\n\t"
			     "ba,pt	%%xcc, 1f\n\t"
			     " add	%0, %1, %0\n\t"
			     ".align	64\n"
			     "1:\n\t"
			     "wr	%0, 0, %%tick_cmpr\n\t"
			     "rd	%%tick_cmpr, %%g0"
			     : "=&r" (new_compare)
			     : "r" (adj));

	return new_compare;
}

static unsigned long tick_add_tick(unsigned long adj, unsigned long offset)
{
	unsigned long new_tick, tmp;

	/* Also need to handle Blackbird bug here too. */
	__asm__ __volatile__("rd	%%tick, %0\n\t"
			     "add	%0, %2, %0\n\t"
			     "wrpr	%0, 0, %%tick\n\t"
			     "andn	%0, %4, %1\n\t"
			     "ba,pt	%%xcc, 1f\n\t"
			     " add	%1, %3, %1\n\t"
			     ".align	64\n"
			     "1:\n\t"
			     "wr	%1, 0, %%tick_cmpr\n\t"
			     "rd	%%tick_cmpr, %%g0"
			     : "=&r" (new_tick), "=&r" (tmp)
			     : "r" (adj), "r" (offset), "r" (TICK_PRIV_BIT));

	return new_tick;
}

static struct sparc64_tick_ops tick_operations __read_mostly = {
	.init_tick	=	tick_init_tick,
	.get_tick	=	tick_get_tick,
	.get_compare	=	tick_get_compare,
	.add_tick	=	tick_add_tick,
	.add_compare	=	tick_add_compare,
	.softint_mask	=	1UL << 0,
};

struct sparc64_tick_ops *tick_ops __read_mostly = &tick_operations;

static void stick_init_tick(unsigned long offset)
{
	/* Writes to the %tick and %stick register are not
	 * allowed on sun4v.  The Hypervisor controls that
	 * bit, per-strand.
	 */
	if (tlb_type != hypervisor) {
		tick_disable_protection();

		/* Let the user get at STICK too. */
		__asm__ __volatile__(
		"	rd	%%asr24, %%g2\n"
		"	andn	%%g2, %0, %%g2\n"
		"	wr	%%g2, 0, %%asr24"
		: /* no outputs */
		: "r" (TICK_PRIV_BIT)
		: "g1", "g2");
	}

	__asm__ __volatile__(
	"	rd	%%asr24, %%g1\n"
	"	andn	%%g1, %1, %%g1\n"
	"	add	%%g1, %0, %%g1\n"
	"	wr	%%g1, 0x0, %%asr25"
	: /* no outputs */
	: "r" (offset), "r" (TICK_PRIV_BIT)
	: "g1");
}

static unsigned long stick_get_tick(void)
{
	unsigned long ret;

	__asm__ __volatile__("rd	%%asr24, %0"
			     : "=r" (ret));

	return ret & ~TICK_PRIV_BIT;
}

static unsigned long stick_get_compare(void)
{
	unsigned long ret;

	__asm__ __volatile__("rd	%%asr25, %0"
			     : "=r" (ret));

	return ret;
}

static unsigned long stick_add_tick(unsigned long adj, unsigned long offset)
{
	unsigned long new_tick, tmp;

	__asm__ __volatile__("rd	%%asr24, %0\n\t"
			     "add	%0, %2, %0\n\t"
			     "wr	%0, 0, %%asr24\n\t"
			     "andn	%0, %4, %1\n\t"
			     "add	%1, %3, %1\n\t"
			     "wr	%1, 0, %%asr25"
			     : "=&r" (new_tick), "=&r" (tmp)
			     : "r" (adj), "r" (offset), "r" (TICK_PRIV_BIT));

	return new_tick;
}

static unsigned long stick_add_compare(unsigned long adj)
{
	unsigned long new_compare;

	__asm__ __volatile__("rd	%%asr25, %0\n\t"
			     "add	%0, %1, %0\n\t"
			     "wr	%0, 0, %%asr25"
			     : "=&r" (new_compare)
			     : "r" (adj));

	return new_compare;
}

static struct sparc64_tick_ops stick_operations __read_mostly = {
	.init_tick	=	stick_init_tick,
	.get_tick	=	stick_get_tick,
	.get_compare	=	stick_get_compare,
	.add_tick	=	stick_add_tick,
	.add_compare	=	stick_add_compare,
	.softint_mask	=	1UL << 16,
};

/* On Hummingbird the STICK/STICK_CMPR register is implemented
 * in I/O space.  There are two 64-bit registers each, the
 * first holds the low 32-bits of the value and the second holds
 * the high 32-bits.
 *
 * Since STICK is constantly updating, we have to access it carefully.
 *
 * The sequence we use to read is:
 * 1) read high
 * 2) read low
 * 3) read high again, if it rolled re-read both low and high again.
 *
 * Writing STICK safely is also tricky:
 * 1) write low to zero
 * 2) write high
 * 3) write low
 */
#define HBIRD_STICKCMP_ADDR	0x1fe0000f060UL
#define HBIRD_STICK_ADDR	0x1fe0000f070UL

static unsigned long __hbird_read_stick(void)
{
	unsigned long ret, tmp1, tmp2, tmp3;
	unsigned long addr = HBIRD_STICK_ADDR+8;

	__asm__ __volatile__("ldxa	[%1] %5, %2\n"
			     "1:\n\t"
			     "sub	%1, 0x8, %1\n\t"
			     "ldxa	[%1] %5, %3\n\t"
			     "add	%1, 0x8, %1\n\t"
			     "ldxa	[%1] %5, %4\n\t"
			     "cmp	%4, %2\n\t"
			     "bne,a,pn	%%xcc, 1b\n\t"
			     " mov	%4, %2\n\t"
			     "sllx	%4, 32, %4\n\t"
			     "or	%3, %4, %0\n\t"
			     : "=&r" (ret), "=&r" (addr),
			       "=&r" (tmp1), "=&r" (tmp2), "=&r" (tmp3)
			     : "i" (ASI_PHYS_BYPASS_EC_E), "1" (addr));

	return ret;
}

static unsigned long __hbird_read_compare(void)
{
	unsigned long low, high;
	unsigned long addr = HBIRD_STICKCMP_ADDR;

	__asm__ __volatile__("ldxa	[%2] %3, %0\n\t"
			     "add	%2, 0x8, %2\n\t"
			     "ldxa	[%2] %3, %1"
			     : "=&r" (low), "=&r" (high), "=&r" (addr)
			     : "i" (ASI_PHYS_BYPASS_EC_E), "2" (addr));

	return (high << 32UL) | low;
}

static void __hbird_write_stick(unsigned long val)
{
	unsigned long low = (val & 0xffffffffUL);
	unsigned long high = (val >> 32UL);
	unsigned long addr = HBIRD_STICK_ADDR;

	__asm__ __volatile__("stxa	%%g0, [%0] %4\n\t"
			     "add	%0, 0x8, %0\n\t"
			     "stxa	%3, [%0] %4\n\t"
			     "sub	%0, 0x8, %0\n\t"
			     "stxa	%2, [%0] %4"
			     : "=&r" (addr)
			     : "0" (addr), "r" (low), "r" (high),
			       "i" (ASI_PHYS_BYPASS_EC_E));
}

static void __hbird_write_compare(unsigned long val)
{
	unsigned long low = (val & 0xffffffffUL);
	unsigned long high = (val >> 32UL);
	unsigned long addr = HBIRD_STICKCMP_ADDR + 0x8UL;

	__asm__ __volatile__("stxa	%3, [%0] %4\n\t"
			     "sub	%0, 0x8, %0\n\t"
			     "stxa	%2, [%0] %4"
			     : "=&r" (addr)
			     : "0" (addr), "r" (low), "r" (high),
			       "i" (ASI_PHYS_BYPASS_EC_E));
}

static void hbtick_init_tick(unsigned long offset)
{
	unsigned long val;

	tick_disable_protection();

	/* XXX This seems to be necessary to 'jumpstart' Hummingbird
	 * XXX into actually sending STICK interrupts.  I think because
	 * XXX of how we store %tick_cmpr in head.S this somehow resets the
	 * XXX {TICK + STICK} interrupt mux.  -DaveM
	 */
	__hbird_write_stick(__hbird_read_stick());

	val = __hbird_read_stick() & ~TICK_PRIV_BIT;
	__hbird_write_compare(val + offset);
}

static unsigned long hbtick_get_tick(void)
{
	return __hbird_read_stick() & ~TICK_PRIV_BIT;
}

static unsigned long hbtick_get_compare(void)
{
	return __hbird_read_compare();
}

static unsigned long hbtick_add_tick(unsigned long adj, unsigned long offset)
{
	unsigned long val;

	val = __hbird_read_stick() + adj;
	__hbird_write_stick(val);

	val &= ~TICK_PRIV_BIT;
	__hbird_write_compare(val + offset);

	return val;
}

static unsigned long hbtick_add_compare(unsigned long adj)
{
	unsigned long val = __hbird_read_compare() + adj;

	val &= ~TICK_PRIV_BIT;
	__hbird_write_compare(val);

	return val;
}

static struct sparc64_tick_ops hbtick_operations __read_mostly = {
	.init_tick	=	hbtick_init_tick,
	.get_tick	=	hbtick_get_tick,
	.get_compare	=	hbtick_get_compare,
	.add_tick	=	hbtick_add_tick,
	.add_compare	=	hbtick_add_compare,
	.softint_mask	=	1UL << 0,
};

/* timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "do_timer()" routine every clocktick
 *
 * NOTE: On SUN5 systems the ticker interrupt comes in using 2
 *       interrupts, one at level14 and one with softint bit 0.
 */
unsigned long timer_tick_offset __read_mostly;

static unsigned long timer_ticks_per_nsec_quotient __read_mostly;

#define TICK_SIZE (tick_nsec / 1000)

static inline void timer_check_rtc(void)
{
	/* last time the cmos clock got updated */
	static long last_rtc_update;

	/* Determine when to update the Mostek clock. */
	if (ntp_synced() &&
	    xtime.tv_sec > last_rtc_update + 660 &&
	    (xtime.tv_nsec / 1000) >= 500000 - ((unsigned) TICK_SIZE) / 2 &&
	    (xtime.tv_nsec / 1000) <= 500000 + ((unsigned) TICK_SIZE) / 2) {
		if (set_rtc_mmss(xtime.tv_sec) == 0)
			last_rtc_update = xtime.tv_sec;
		else
			last_rtc_update = xtime.tv_sec - 600;
			/* do it again in 60 s */
	}
}

irqreturn_t timer_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
	unsigned long ticks, compare, pstate;

	write_seqlock(&xtime_lock);

	do {
#ifndef CONFIG_SMP
		profile_tick(CPU_PROFILING, regs);
		update_process_times(user_mode(regs));
#endif
		do_timer(regs);

		/* Guarantee that the following sequences execute
		 * uninterrupted.
		 */
		__asm__ __volatile__("rdpr	%%pstate, %0\n\t"
				     "wrpr	%0, %1, %%pstate"
				     : "=r" (pstate)
				     : "i" (PSTATE_IE));

		compare = tick_ops->add_compare(timer_tick_offset);
		ticks = tick_ops->get_tick();

		/* Restore PSTATE_IE. */
		__asm__ __volatile__("wrpr	%0, 0x0, %%pstate"
				     : /* no outputs */
				     : "r" (pstate));
	} while (time_after_eq(ticks, compare));

	timer_check_rtc();

	write_sequnlock(&xtime_lock);

	return IRQ_HANDLED;
}

#ifdef CONFIG_SMP
void timer_tick_interrupt(struct pt_regs *regs)
{
	write_seqlock(&xtime_lock);

	do_timer(regs);

	timer_check_rtc();

	write_sequnlock(&xtime_lock);
}
#endif

/* Kick start a stopped clock (procedure from the Sun NVRAM/hostid FAQ). */
static void __init kick_start_clock(void)
{
	void __iomem *regs = mstk48t02_regs;
	u8 sec, tmp;
	int i, count;

	prom_printf("CLOCK: Clock was stopped. Kick start ");

	spin_lock_irq(&mostek_lock);

	/* Turn on the kick start bit to start the oscillator. */
	tmp = mostek_read(regs + MOSTEK_CREG);
	tmp |= MSTK_CREG_WRITE;
	mostek_write(regs + MOSTEK_CREG, tmp);
	tmp = mostek_read(regs + MOSTEK_SEC);
	tmp &= ~MSTK_STOP;
	mostek_write(regs + MOSTEK_SEC, tmp);
	tmp = mostek_read(regs + MOSTEK_HOUR);
	tmp |= MSTK_KICK_START;
	mostek_write(regs + MOSTEK_HOUR, tmp);
	tmp = mostek_read(regs + MOSTEK_CREG);
	tmp &= ~MSTK_CREG_WRITE;
	mostek_write(regs + MOSTEK_CREG, tmp);

	spin_unlock_irq(&mostek_lock);

	/* Delay to allow the clock oscillator to start. */
	sec = MSTK_REG_SEC(regs);
	for (i = 0; i < 3; i++) {
		while (sec == MSTK_REG_SEC(regs))
			for (count = 0; count < 100000; count++)
				/* nothing */ ;
		prom_printf(".");
		sec = MSTK_REG_SEC(regs);
	}
	prom_printf("\n");

	spin_lock_irq(&mostek_lock);

	/* Turn off kick start and set a "valid" time and date. */
	tmp = mostek_read(regs + MOSTEK_CREG);
	tmp |= MSTK_CREG_WRITE;
	mostek_write(regs + MOSTEK_CREG, tmp);
	tmp = mostek_read(regs + MOSTEK_HOUR);
	tmp &= ~MSTK_KICK_START;
	mostek_write(regs + MOSTEK_HOUR, tmp);
	MSTK_SET_REG_SEC(regs,0);
	MSTK_SET_REG_MIN(regs,0);
	MSTK_SET_REG_HOUR(regs,0);
	MSTK_SET_REG_DOW(regs,5);
	MSTK_SET_REG_DOM(regs,1);
	MSTK_SET_REG_MONTH(regs,8);
	MSTK_SET_REG_YEAR(regs,1996 - MSTK_YEAR_ZERO);
	tmp = mostek_read(regs + MOSTEK_CREG);
	tmp &= ~MSTK_CREG_WRITE;
	mostek_write(regs + MOSTEK_CREG, tmp);

	spin_unlock_irq(&mostek_lock);

	/* Ensure the kick start bit is off. If it isn't, turn it off. */
	while (mostek_read(regs + MOSTEK_HOUR) & MSTK_KICK_START) {
		prom_printf("CLOCK: Kick start still on!\n");

		spin_lock_irq(&mostek_lock);

		tmp = mostek_read(regs + MOSTEK_CREG);
		tmp |= MSTK_CREG_WRITE;
		mostek_write(regs + MOSTEK_CREG, tmp);

		tmp = mostek_read(regs + MOSTEK_HOUR);
		tmp &= ~MSTK_KICK_START;
		mostek_write(regs + MOSTEK_HOUR, tmp);

		tmp = mostek_read(regs + MOSTEK_CREG);
		tmp &= ~MSTK_CREG_WRITE;
		mostek_write(regs + MOSTEK_CREG, tmp);

		spin_unlock_irq(&mostek_lock);
	}

	prom_printf("CLOCK: Kick start procedure successful.\n");
}

/* Return nonzero if the clock chip battery is low. */
static int __init has_low_battery(void)
{
	void __iomem *regs = mstk48t02_regs;
	u8 data1, data2;

	spin_lock_irq(&mostek_lock);

	data1 = mostek_read(regs + MOSTEK_EEPROM);	/* Read some data. */
	mostek_write(regs + MOSTEK_EEPROM, ~data1);	/* Write back the complement. */
	data2 = mostek_read(regs + MOSTEK_EEPROM);	/* Read back the complement. */
	mostek_write(regs + MOSTEK_EEPROM, data1);	/* Restore original value. */

	spin_unlock_irq(&mostek_lock);

	return (data1 == data2);	/* Was the write blocked? */
}

/* Probe for the real time clock chip. */
static void __init set_system_time(void)
{
	unsigned int year, mon, day, hour, min, sec;
	void __iomem *mregs = mstk48t02_regs;
#ifdef CONFIG_PCI
	unsigned long dregs = ds1287_regs;
#else
	unsigned long dregs = 0UL;
#endif
	u8 tmp;

	if (!mregs && !dregs) {
		prom_printf("Something wrong, clock regs not mapped yet.\n");
		prom_halt();
	}		

	if (mregs) {
		spin_lock_irq(&mostek_lock);

		/* Traditional Mostek chip. */
		tmp = mostek_read(mregs + MOSTEK_CREG);
		tmp |= MSTK_CREG_READ;
		mostek_write(mregs + MOSTEK_CREG, tmp);

		sec = MSTK_REG_SEC(mregs);
		min = MSTK_REG_MIN(mregs);
		hour = MSTK_REG_HOUR(mregs);
		day = MSTK_REG_DOM(mregs);
		mon = MSTK_REG_MONTH(mregs);
		year = MSTK_CVT_YEAR( MSTK_REG_YEAR(mregs) );
	} else {
		/* Dallas 12887 RTC chip. */

		do {
			sec  = CMOS_READ(RTC_SECONDS);
			min  = CMOS_READ(RTC_MINUTES);
			hour = CMOS_READ(RTC_HOURS);
			day  = CMOS_READ(RTC_DAY_OF_MONTH);
			mon  = CMOS_READ(RTC_MONTH);
			year = CMOS_READ(RTC_YEAR);
		} while (sec != CMOS_READ(RTC_SECONDS));

		if (!(CMOS_READ(RTC_CONTROL) & RTC_DM_BINARY) || RTC_ALWAYS_BCD) {
			BCD_TO_BIN(sec);
			BCD_TO_BIN(min);
			BCD_TO_BIN(hour);
			BCD_TO_BIN(day);
			BCD_TO_BIN(mon);
			BCD_TO_BIN(year);
		}
		if ((year += 1900) < 1970)
			year += 100;
	}

	xtime.tv_sec = mktime(year, mon, day, hour, min, sec);
	xtime.tv_nsec = (INITIAL_JIFFIES % HZ) * (NSEC_PER_SEC / HZ);
	set_normalized_timespec(&wall_to_monotonic,
 	                        -xtime.tv_sec, -xtime.tv_nsec);

	if (mregs) {
		tmp = mostek_read(mregs + MOSTEK_CREG);
		tmp &= ~MSTK_CREG_READ;
		mostek_write(mregs + MOSTEK_CREG, tmp);

		spin_unlock_irq(&mostek_lock);
	}
}

/* davem suggests we keep this within the 4M locked kernel image */
static u32 starfire_get_time(void)
{
	static char obp_gettod[32];
	static u32 unix_tod;

	sprintf(obp_gettod, "h# %08x unix-gettod",
		(unsigned int) (long) &unix_tod);
	prom_feval(obp_gettod);

	return unix_tod;
}

static int starfire_set_time(u32 val)
{
	/* Do nothing, time is set using the service processor
	 * console on this platform.
	 */
	return 0;
}

static u32 hypervisor_get_time(void)
{
	register unsigned long func asm("%o5");
	register unsigned long arg0 asm("%o0");
	register unsigned long arg1 asm("%o1");
	int retries = 10000;

retry:
	func = HV_FAST_TOD_GET;
	arg0 = 0;
	arg1 = 0;
	__asm__ __volatile__("ta	%6"
			     : "=&r" (func), "=&r" (arg0), "=&r" (arg1)
			     : "0" (func), "1" (arg0), "2" (arg1),
			       "i" (HV_FAST_TRAP));
	if (arg0 == HV_EOK)
		return arg1;
	if (arg0 == HV_EWOULDBLOCK) {
		if (--retries > 0) {
			udelay(100);
			goto retry;
		}
		printk(KERN_WARNING "SUN4V: tod_get() timed out.\n");
		return 0;
	}
	printk(KERN_WARNING "SUN4V: tod_get() not supported.\n");
	return 0;
}

static int hypervisor_set_time(u32 secs)
{
	register unsigned long func asm("%o5");
	register unsigned long arg0 asm("%o0");
	int retries = 10000;

retry:
	func = HV_FAST_TOD_SET;
	arg0 = secs;
	__asm__ __volatile__("ta	%4"
			     : "=&r" (func), "=&r" (arg0)
			     : "0" (func), "1" (arg0),
			       "i" (HV_FAST_TRAP));
	if (arg0 == HV_EOK)
		return 0;
	if (arg0 == HV_EWOULDBLOCK) {
		if (--retries > 0) {
			udelay(100);
			goto retry;
		}
		printk(KERN_WARNING "SUN4V: tod_set() timed out.\n");
		return -EAGAIN;
	}
	printk(KERN_WARNING "SUN4V: tod_set() not supported.\n");
	return -EOPNOTSUPP;
}

void __init clock_probe(void)
{
	struct linux_prom_registers clk_reg[2];
	char model[128];
	int node, busnd = -1, err;
	unsigned long flags;
	struct linux_central *cbus;
#ifdef CONFIG_PCI
	struct linux_ebus *ebus = NULL;
	struct sparc_isa_bridge *isa_br = NULL;
#endif
	static int invoked;

	if (invoked)
		return;
	invoked = 1;


	if (this_is_starfire) {
		xtime.tv_sec = starfire_get_time();
		xtime.tv_nsec = (INITIAL_JIFFIES % HZ) * (NSEC_PER_SEC / HZ);
		set_normalized_timespec(&wall_to_monotonic,
		                        -xtime.tv_sec, -xtime.tv_nsec);
		return;
	}
	if (tlb_type == hypervisor) {
		xtime.tv_sec = hypervisor_get_time();
		xtime.tv_nsec = (INITIAL_JIFFIES % HZ) * (NSEC_PER_SEC / HZ);
		set_normalized_timespec(&wall_to_monotonic,
		                        -xtime.tv_sec, -xtime.tv_nsec);
		return;
	}

	local_irq_save(flags);

	cbus = central_bus;
	if (cbus != NULL)
		busnd = central_bus->child->prom_node;

	/* Check FHC Central then EBUSs then ISA bridges then SBUSs.
	 * That way we handle the presence of multiple properly.
	 *
	 * As a special case, machines with Central must provide the
	 * timer chip there.
	 */
#ifdef CONFIG_PCI
	if (ebus_chain != NULL) {
		ebus = ebus_chain;
		if (busnd == -1)
			busnd = ebus->prom_node;
	}
	if (isa_chain != NULL) {
		isa_br = isa_chain;
		if (busnd == -1)
			busnd = isa_br->prom_node;
	}
#endif
	if (sbus_root != NULL && busnd == -1)
		busnd = sbus_root->prom_node;

	if (busnd == -1) {
		prom_printf("clock_probe: problem, cannot find bus to search.\n");
		prom_halt();
	}

	node = prom_getchild(busnd);

	while (1) {
		if (!node)
			model[0] = 0;
		else
			prom_getstring(node, "model", model, sizeof(model));
		if (strcmp(model, "mk48t02") &&
		    strcmp(model, "mk48t08") &&
		    strcmp(model, "mk48t59") &&
		    strcmp(model, "m5819") &&
		    strcmp(model, "m5819p") &&
		    strcmp(model, "m5823") &&
		    strcmp(model, "ds1287")) {
			if (cbus != NULL) {
				prom_printf("clock_probe: Central bus lacks timer chip.\n");
				prom_halt();
			}

		   	if (node != 0)
				node = prom_getsibling(node);
#ifdef CONFIG_PCI
			while ((node == 0) && ebus != NULL) {
				ebus = ebus->next;
				if (ebus != NULL) {
					busnd = ebus->prom_node;
					node = prom_getchild(busnd);
				}
			}
			while ((node == 0) && isa_br != NULL) {
				isa_br = isa_br->next;
				if (isa_br != NULL) {
					busnd = isa_br->prom_node;
					node = prom_getchild(busnd);
				}
			}
#endif
			if (node == 0) {
				prom_printf("clock_probe: Cannot find timer chip\n");
				prom_halt();
			}
			continue;
		}

		err = prom_getproperty(node, "reg", (char *)clk_reg,
				       sizeof(clk_reg));
		if(err == -1) {
			prom_printf("clock_probe: Cannot get Mostek reg property\n");
			prom_halt();
		}

		if (cbus != NULL) {
			apply_fhc_ranges(central_bus->child, clk_reg, 1);
			apply_central_ranges(central_bus, clk_reg, 1);
		}
#ifdef CONFIG_PCI
		else if (ebus != NULL) {
			struct linux_ebus_device *edev;

			for_each_ebusdev(edev, ebus)
				if (edev->prom_node == node)
					break;
			if (edev == NULL) {
				if (isa_chain != NULL)
					goto try_isa_clock;
				prom_printf("%s: Mostek not probed by EBUS\n",
					    __FUNCTION__);
				prom_halt();
			}

			if (!strcmp(model, "ds1287") ||
			    !strcmp(model, "m5819") ||
			    !strcmp(model, "m5819p") ||
			    !strcmp(model, "m5823")) {
				ds1287_regs = edev->resource[0].start;
			} else {
				mstk48t59_regs = (void __iomem *)
					edev->resource[0].start;
				mstk48t02_regs = mstk48t59_regs + MOSTEK_48T59_48T02;
			}
			break;
		}
		else if (isa_br != NULL) {
			struct sparc_isa_device *isadev;

try_isa_clock:
			for_each_isadev(isadev, isa_br)
				if (isadev->prom_node == node)
					break;
			if (isadev == NULL) {
				prom_printf("%s: Mostek not probed by ISA\n");
				prom_halt();
			}
			if (!strcmp(model, "ds1287") ||
			    !strcmp(model, "m5819") ||
			    !strcmp(model, "m5819p") ||
			    !strcmp(model, "m5823")) {
				ds1287_regs = isadev->resource.start;
			} else {
				mstk48t59_regs = (void __iomem *)
					isadev->resource.start;
				mstk48t02_regs = mstk48t59_regs + MOSTEK_48T59_48T02;
			}
			break;
		}
#endif
		else {
			if (sbus_root->num_sbus_ranges) {
				int nranges = sbus_root->num_sbus_ranges;
				int rngc;

				for (rngc = 0; rngc < nranges; rngc++)
					if (clk_reg[0].which_io ==
					    sbus_root->sbus_ranges[rngc].ot_child_space)
						break;
				if (rngc == nranges) {
					prom_printf("clock_probe: Cannot find ranges for "
						    "clock regs.\n");
					prom_halt();
				}
				clk_reg[0].which_io =
					sbus_root->sbus_ranges[rngc].ot_parent_space;
				clk_reg[0].phys_addr +=
					sbus_root->sbus_ranges[rngc].ot_parent_base;
			}
		}

		if(model[5] == '0' && model[6] == '2') {
			mstk48t02_regs = (void __iomem *)
				(((u64)clk_reg[0].phys_addr) |
				 (((u64)clk_reg[0].which_io)<<32UL));
		} else if(model[5] == '0' && model[6] == '8') {
			mstk48t08_regs = (void __iomem *)
				(((u64)clk_reg[0].phys_addr) |
				 (((u64)clk_reg[0].which_io)<<32UL));
			mstk48t02_regs = mstk48t08_regs + MOSTEK_48T08_48T02;
		} else {
			mstk48t59_regs = (void __iomem *)
				(((u64)clk_reg[0].phys_addr) |
				 (((u64)clk_reg[0].which_io)<<32UL));
			mstk48t02_regs = mstk48t59_regs + MOSTEK_48T59_48T02;
		}
		break;
	}

	if (mstk48t02_regs != NULL) {
		/* Report a low battery voltage condition. */
		if (has_low_battery())
			prom_printf("NVRAM: Low battery voltage!\n");

		/* Kick start the clock if it is completely stopped. */
		if (mostek_read(mstk48t02_regs + MOSTEK_SEC) & MSTK_STOP)
			kick_start_clock();
	}

	set_system_time();
	
	local_irq_restore(flags);
}

/* This is gets the master TICK_INT timer going. */
static unsigned long sparc64_init_timers(void)
{
	unsigned long clock;
	int node;
#ifdef CONFIG_SMP
	extern void smp_tick_init(void);
#endif

	if (tlb_type == spitfire) {
		unsigned long ver, manuf, impl;

		__asm__ __volatile__ ("rdpr %%ver, %0"
				      : "=&r" (ver));
		manuf = ((ver >> 48) & 0xffff);
		impl = ((ver >> 32) & 0xffff);
		if (manuf == 0x17 && impl == 0x13) {
			/* Hummingbird, aka Ultra-IIe */
			tick_ops = &hbtick_operations;
			node = prom_root_node;
			clock = prom_getint(node, "stick-frequency");
		} else {
			tick_ops = &tick_operations;
			cpu_find_by_instance(0, &node, NULL);
			clock = prom_getint(node, "clock-frequency");
		}
	} else {
		tick_ops = &stick_operations;
		node = prom_root_node;
		clock = prom_getint(node, "stick-frequency");
	}
	timer_tick_offset = clock / HZ;

#ifdef CONFIG_SMP
	smp_tick_init();
#endif

	return clock;
}

static void sparc64_start_timers(void)
{
	unsigned long pstate;

	/* Guarantee that the following sequences execute
	 * uninterrupted.
	 */
	__asm__ __volatile__("rdpr	%%pstate, %0\n\t"
			     "wrpr	%0, %1, %%pstate"
			     : "=r" (pstate)
			     : "i" (PSTATE_IE));

	tick_ops->init_tick(timer_tick_offset);

	/* Restore PSTATE_IE. */
	__asm__ __volatile__("wrpr	%0, 0x0, %%pstate"
			     : /* no outputs */
			     : "r" (pstate));

	local_irq_enable();
}

struct freq_table {
	unsigned long clock_tick_ref;
	unsigned int ref_freq;
};
static DEFINE_PER_CPU(struct freq_table, sparc64_freq_table) = { 0, 0 };

unsigned long sparc64_get_clock_tick(unsigned int cpu)
{
	struct freq_table *ft = &per_cpu(sparc64_freq_table, cpu);

	if (ft->clock_tick_ref)
		return ft->clock_tick_ref;
	return cpu_data(cpu).clock_tick;
}

#ifdef CONFIG_CPU_FREQ

static int sparc64_cpufreq_notifier(struct notifier_block *nb, unsigned long val,
				    void *data)
{
	struct cpufreq_freqs *freq = data;
	unsigned int cpu = freq->cpu;
	struct freq_table *ft = &per_cpu(sparc64_freq_table, cpu);

	if (!ft->ref_freq) {
		ft->ref_freq = freq->old;
		ft->clock_tick_ref = cpu_data(cpu).clock_tick;
	}
	if ((val == CPUFREQ_PRECHANGE  && freq->old < freq->new) ||
	    (val == CPUFREQ_POSTCHANGE && freq->old > freq->new) ||
	    (val == CPUFREQ_RESUMECHANGE)) {
		cpu_data(cpu).clock_tick =
			cpufreq_scale(ft->clock_tick_ref,
				      ft->ref_freq,
				      freq->new);
	}

	return 0;
}

static struct notifier_block sparc64_cpufreq_notifier_block = {
	.notifier_call	= sparc64_cpufreq_notifier
};

#endif /* CONFIG_CPU_FREQ */

static struct time_interpolator sparc64_cpu_interpolator = {
	.source		=	TIME_SOURCE_CPU,
	.shift		=	16,
	.mask		=	0xffffffffffffffffLL
};

/* The quotient formula is taken from the IA64 port. */
#define SPARC64_NSEC_PER_CYC_SHIFT	30UL
void __init time_init(void)
{
	unsigned long clock = sparc64_init_timers();

	sparc64_cpu_interpolator.frequency = clock;
	register_time_interpolator(&sparc64_cpu_interpolator);

	/* Now that the interpolator is registered, it is
	 * safe to start the timer ticking.
	 */
	sparc64_start_timers();

	timer_ticks_per_nsec_quotient =
		(((NSEC_PER_SEC << SPARC64_NSEC_PER_CYC_SHIFT) +
		  (clock / 2)) / clock);

#ifdef CONFIG_CPU_FREQ
	cpufreq_register_notifier(&sparc64_cpufreq_notifier_block,
				  CPUFREQ_TRANSITION_NOTIFIER);
#endif
}

unsigned long long sched_clock(void)
{
	unsigned long ticks = tick_ops->get_tick();

	return (ticks * timer_ticks_per_nsec_quotient)
		>> SPARC64_NSEC_PER_CYC_SHIFT;
}

static int set_rtc_mmss(unsigned long nowtime)
{
	int real_seconds, real_minutes, chip_minutes;
	void __iomem *mregs = mstk48t02_regs;
#ifdef CONFIG_PCI
	unsigned long dregs = ds1287_regs;
#else
	unsigned long dregs = 0UL;
#endif
	unsigned long flags;
	u8 tmp;

	/* 
	 * Not having a register set can lead to trouble.
	 * Also starfire doesn't have a tod clock.
	 */
	if (!mregs && !dregs) 
		return -1;

	if (mregs) {
		spin_lock_irqsave(&mostek_lock, flags);

		/* Read the current RTC minutes. */
		tmp = mostek_read(mregs + MOSTEK_CREG);
		tmp |= MSTK_CREG_READ;
		mostek_write(mregs + MOSTEK_CREG, tmp);

		chip_minutes = MSTK_REG_MIN(mregs);

		tmp = mostek_read(mregs + MOSTEK_CREG);
		tmp &= ~MSTK_CREG_READ;
		mostek_write(mregs + MOSTEK_CREG, tmp);

		/*
		 * since we're only adjusting minutes and seconds,
		 * don't interfere with hour overflow. This avoids
		 * messing with unknown time zones but requires your
		 * RTC not to be off by more than 15 minutes
		 */
		real_seconds = nowtime % 60;
		real_minutes = nowtime / 60;
		if (((abs(real_minutes - chip_minutes) + 15)/30) & 1)
			real_minutes += 30;	/* correct for half hour time zone */
		real_minutes %= 60;

		if (abs(real_minutes - chip_minutes) < 30) {
			tmp = mostek_read(mregs + MOSTEK_CREG);
			tmp |= MSTK_CREG_WRITE;
			mostek_write(mregs + MOSTEK_CREG, tmp);

			MSTK_SET_REG_SEC(mregs,real_seconds);
			MSTK_SET_REG_MIN(mregs,real_minutes);

			tmp = mostek_read(mregs + MOSTEK_CREG);
			tmp &= ~MSTK_CREG_WRITE;
			mostek_write(mregs + MOSTEK_CREG, tmp);

			spin_unlock_irqrestore(&mostek_lock, flags);

			return 0;
		} else {
			spin_unlock_irqrestore(&mostek_lock, flags);

			return -1;
		}
	} else {
		int retval = 0;
		unsigned char save_control, save_freq_select;

		/* Stolen from arch/i386/kernel/time.c, see there for
		 * credits and descriptive comments.
		 */
		spin_lock_irqsave(&rtc_lock, flags);
		save_control = CMOS_READ(RTC_CONTROL); /* tell the clock it's being set */
		CMOS_WRITE((save_control|RTC_SET), RTC_CONTROL);

		save_freq_select = CMOS_READ(RTC_FREQ_SELECT); /* stop and reset prescaler */
		CMOS_WRITE((save_freq_select|RTC_DIV_RESET2), RTC_FREQ_SELECT);

		chip_minutes = CMOS_READ(RTC_MINUTES);
		if (!(save_control & RTC_DM_BINARY) || RTC_ALWAYS_BCD)
			BCD_TO_BIN(chip_minutes);
		real_seconds = nowtime % 60;
		real_minutes = nowtime / 60;
		if (((abs(real_minutes - chip_minutes) + 15)/30) & 1)
			real_minutes += 30;
		real_minutes %= 60;

		if (abs(real_minutes - chip_minutes) < 30) {
			if (!(save_control & RTC_DM_BINARY) || RTC_ALWAYS_BCD) {
				BIN_TO_BCD(real_seconds);
				BIN_TO_BCD(real_minutes);
			}
			CMOS_WRITE(real_seconds,RTC_SECONDS);
			CMOS_WRITE(real_minutes,RTC_MINUTES);
		} else {
			printk(KERN_WARNING
			       "set_rtc_mmss: can't update from %d to %d\n",
			       chip_minutes, real_minutes);
			retval = -1;
		}

		CMOS_WRITE(save_control, RTC_CONTROL);
		CMOS_WRITE(save_freq_select, RTC_FREQ_SELECT);
		spin_unlock_irqrestore(&rtc_lock, flags);

		return retval;
	}
}

#define RTC_IS_OPEN		0x01	/* means /dev/rtc is in use	*/
static unsigned char mini_rtc_status;	/* bitmapped status byte.	*/

/* months start at 0 now */
static unsigned char days_in_mo[] =
{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

#define FEBRUARY	2
#define	STARTOFTIME	1970
#define SECDAY		86400L
#define SECYR		(SECDAY * 365)
#define	leapyear(year)		((year) % 4 == 0 && \
				 ((year) % 100 != 0 || (year) % 400 == 0))
#define	days_in_year(a) 	(leapyear(a) ? 366 : 365)
#define	days_in_month(a) 	(month_days[(a) - 1])

static int month_days[12] = {
	31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/*
 * This only works for the Gregorian calendar - i.e. after 1752 (in the UK)
 */
static void GregorianDay(struct rtc_time * tm)
{
	int leapsToDate;
	int lastYear;
	int day;
	int MonthOffset[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };

	lastYear = tm->tm_year - 1;

	/*
	 * Number of leap corrections to apply up to end of last year
	 */
	leapsToDate = lastYear / 4 - lastYear / 100 + lastYear / 400;

	/*
	 * This year is a leap year if it is divisible by 4 except when it is
	 * divisible by 100 unless it is divisible by 400
	 *
	 * e.g. 1904 was a leap year, 1900 was not, 1996 is, and 2000 was
	 */
	day = tm->tm_mon > 2 && leapyear(tm->tm_year);

	day += lastYear*365 + leapsToDate + MonthOffset[tm->tm_mon-1] +
		   tm->tm_mday;

	tm->tm_wday = day % 7;
}

static void to_tm(int tim, struct rtc_time *tm)
{
	register int    i;
	register long   hms, day;

	day = tim / SECDAY;
	hms = tim % SECDAY;

	/* Hours, minutes, seconds are easy */
	tm->tm_hour = hms / 3600;
	tm->tm_min = (hms % 3600) / 60;
	tm->tm_sec = (hms % 3600) % 60;

	/* Number of years in days */
	for (i = STARTOFTIME; day >= days_in_year(i); i++)
		day -= days_in_year(i);
	tm->tm_year = i;

	/* Number of months in days left */
	if (leapyear(tm->tm_year))
		days_in_month(FEBRUARY) = 29;
	for (i = 1; day >= days_in_month(i); i++)
		day -= days_in_month(i);
	days_in_month(FEBRUARY) = 28;
	tm->tm_mon = i;

	/* Days are what is left over (+1) from all that. */
	tm->tm_mday = day + 1;

	/*
	 * Determine the day of week
	 */
	GregorianDay(tm);
}

/* Both Starfire and SUN4V give us seconds since Jan 1st, 1970,
 * aka Unix time.  So we have to convert to/from rtc_time.
 */
static inline void mini_get_rtc_time(struct rtc_time *time)
{
	unsigned long flags;
	u32 seconds;

	spin_lock_irqsave(&rtc_lock, flags);
	seconds = 0;
	if (this_is_starfire)
		seconds = starfire_get_time();
	else if (tlb_type == hypervisor)
		seconds = hypervisor_get_time();
	spin_unlock_irqrestore(&rtc_lock, flags);

	to_tm(seconds, time);
	time->tm_year -= 1900;
	time->tm_mon -= 1;
}

static inline int mini_set_rtc_time(struct rtc_time *time)
{
	u32 seconds = mktime(time->tm_year + 1900, time->tm_mon + 1,
			     time->tm_mday, time->tm_hour,
			     time->tm_min, time->tm_sec);
	unsigned long flags;
	int err;

	spin_lock_irqsave(&rtc_lock, flags);
	err = -ENODEV;
	if (this_is_starfire)
		err = starfire_set_time(seconds);
	else  if (tlb_type == hypervisor)
		err = hypervisor_set_time(seconds);
	spin_unlock_irqrestore(&rtc_lock, flags);

	return err;
}

static int mini_rtc_ioctl(struct inode *inode, struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	struct rtc_time wtime;
	void __user *argp = (void __user *)arg;

	switch (cmd) {

	case RTC_PLL_GET:
		return -EINVAL;

	case RTC_PLL_SET:
		return -EINVAL;

	case RTC_UIE_OFF:	/* disable ints from RTC updates.	*/
		return 0;

	case RTC_UIE_ON:	/* enable ints for RTC updates.	*/
	        return -EINVAL;

	case RTC_RD_TIME:	/* Read the time/date from RTC	*/
		/* this doesn't get week-day, who cares */
		memset(&wtime, 0, sizeof(wtime));
		mini_get_rtc_time(&wtime);

		return copy_to_user(argp, &wtime, sizeof(wtime)) ? -EFAULT : 0;

	case RTC_SET_TIME:	/* Set the RTC */
	    {
		int year;
		unsigned char leap_yr;

		if (!capable(CAP_SYS_TIME))
			return -EACCES;

		if (copy_from_user(&wtime, argp, sizeof(wtime)))
			return -EFAULT;

		year = wtime.tm_year + 1900;
		leap_yr = ((!(year % 4) && (year % 100)) ||
			   !(year % 400));

		if ((wtime.tm_mon < 0 || wtime.tm_mon > 11) || (wtime.tm_mday < 1))
			return -EINVAL;

		if (wtime.tm_mday < 0 || wtime.tm_mday >
		    (days_in_mo[wtime.tm_mon] + ((wtime.tm_mon == 1) && leap_yr)))
			return -EINVAL;

		if (wtime.tm_hour < 0 || wtime.tm_hour >= 24 ||
		    wtime.tm_min < 0 || wtime.tm_min >= 60 ||
		    wtime.tm_sec < 0 || wtime.tm_sec >= 60)
			return -EINVAL;

		return mini_set_rtc_time(&wtime);
	    }
	}

	return -EINVAL;
}

static int mini_rtc_open(struct inode *inode, struct file *file)
{
	if (mini_rtc_status & RTC_IS_OPEN)
		return -EBUSY;

	mini_rtc_status |= RTC_IS_OPEN;

	return 0;
}

static int mini_rtc_release(struct inode *inode, struct file *file)
{
	mini_rtc_status &= ~RTC_IS_OPEN;
	return 0;
}


static struct file_operations mini_rtc_fops = {
	.owner		= THIS_MODULE,
	.ioctl		= mini_rtc_ioctl,
	.open		= mini_rtc_open,
	.release	= mini_rtc_release,
};

static struct miscdevice rtc_mini_dev =
{
	.minor		= RTC_MINOR,
	.name		= "rtc",
	.fops		= &mini_rtc_fops,
};

static int __init rtc_mini_init(void)
{
	int retval;

	if (tlb_type != hypervisor && !this_is_starfire)
		return -ENODEV;

	printk(KERN_INFO "Mini RTC Driver\n");

	retval = misc_register(&rtc_mini_dev);
	if (retval < 0)
		return retval;

	return 0;
}

static void __exit rtc_mini_exit(void)
{
	misc_deregister(&rtc_mini_dev);
}


module_init(rtc_mini_init);
module_exit(rtc_mini_exit);
