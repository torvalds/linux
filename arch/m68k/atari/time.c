/*
 * linux/arch/m68k/atari/time.c
 *
 * Atari time and real time clock stuff
 *
 * Assembled of parts of former atari/config.c 97-12-18 by Roman Hodek
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/types.h>
#include <linux/mc146818rtc.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/rtc.h>
#include <linux/bcd.h>
#include <linux/clocksource.h>
#include <linux/delay.h>
#include <linux/export.h>

#include <asm/atariints.h>
#include <asm/machdep.h>

#include "atari.h"

DEFINE_SPINLOCK(rtc_lock);
EXPORT_SYMBOL_GPL(rtc_lock);

static u64 atari_read_clk(struct clocksource *cs);

static struct clocksource atari_clk = {
	.name   = "mfp",
	.rating = 100,
	.read   = atari_read_clk,
	.mask   = CLOCKSOURCE_MASK(32),
	.flags  = CLOCK_SOURCE_IS_CONTINUOUS,
};

static u32 clk_total;
static u8 last_timer_count;

static irqreturn_t mfp_timer_c_handler(int irq, void *dev_id)
{
	unsigned long flags;

	local_irq_save(flags);
	do {
		last_timer_count = st_mfp.tim_dt_c;
	} while (last_timer_count == 1);
	clk_total += INT_TICKS;
	legacy_timer_tick(1);
	timer_heartbeat();
	local_irq_restore(flags);

	return IRQ_HANDLED;
}

void __init
atari_sched_init(void)
{
    /* set Timer C data Register */
    st_mfp.tim_dt_c = INT_TICKS;
    /* start timer C, div = 1:100 */
    st_mfp.tim_ct_cd = (st_mfp.tim_ct_cd & 15) | 0x60;
    /* install interrupt service routine for MFP Timer C */
    if (request_irq(IRQ_MFP_TIMC, mfp_timer_c_handler, IRQF_TIMER, "timer",
                    NULL))
	pr_err("Couldn't register timer interrupt\n");

    clocksource_register_hz(&atari_clk, INT_CLK);
}

/* ++andreas: gettimeoffset fixed to check for pending interrupt */

static u64 atari_read_clk(struct clocksource *cs)
{
	unsigned long flags;
	u8 count;
	u32 ticks;

	local_irq_save(flags);
	/* Ensure that the count is monotonically decreasing, even though
	 * the result may briefly stop changing after counter wrap-around.
	 */
	count = min(st_mfp.tim_dt_c, last_timer_count);
	last_timer_count = count;

	ticks = INT_TICKS - count;
	ticks += clk_total;
	local_irq_restore(flags);

	return ticks;
}


static void mste_read(struct MSTE_RTC *val)
{
#define COPY(v) val->v=(mste_rtc.v & 0xf)
	do {
		COPY(sec_ones) ; COPY(sec_tens) ; COPY(min_ones) ;
		COPY(min_tens) ; COPY(hr_ones) ; COPY(hr_tens) ;
		COPY(weekday) ; COPY(day_ones) ; COPY(day_tens) ;
		COPY(mon_ones) ; COPY(mon_tens) ; COPY(year_ones) ;
		COPY(year_tens) ;
	/* prevent from reading the clock while it changed */
	} while (val->sec_ones != (mste_rtc.sec_ones & 0xf));
#undef COPY
}

static void mste_write(struct MSTE_RTC *val)
{
#define COPY(v) mste_rtc.v=val->v
	do {
		COPY(sec_ones) ; COPY(sec_tens) ; COPY(min_ones) ;
		COPY(min_tens) ; COPY(hr_ones) ; COPY(hr_tens) ;
		COPY(weekday) ; COPY(day_ones) ; COPY(day_tens) ;
		COPY(mon_ones) ; COPY(mon_tens) ; COPY(year_ones) ;
		COPY(year_tens) ;
	/* prevent from writing the clock while it changed */
	} while (val->sec_ones != (mste_rtc.sec_ones & 0xf));
#undef COPY
}

#define	RTC_READ(reg)				\
    ({	unsigned char	__val;			\
		(void) atari_writeb(reg,&tt_rtc.regsel);	\
		__val = tt_rtc.data;		\
		__val;				\
	})

#define	RTC_WRITE(reg,val)			\
    do {					\
		atari_writeb(reg,&tt_rtc.regsel);	\
		tt_rtc.data = (val);		\
	} while(0)


#define HWCLK_POLL_INTERVAL	5

int atari_mste_hwclk( int op, struct rtc_time *t )
{
    int hour, year;
    int hr24=0;
    struct MSTE_RTC val;

    mste_rtc.mode=(mste_rtc.mode | 1);
    hr24=mste_rtc.mon_tens & 1;
    mste_rtc.mode=(mste_rtc.mode & ~1);

    if (op) {
        /* write: prepare values */

        val.sec_ones = t->tm_sec % 10;
        val.sec_tens = t->tm_sec / 10;
        val.min_ones = t->tm_min % 10;
        val.min_tens = t->tm_min / 10;
        hour = t->tm_hour;
        if (!hr24) {
	    if (hour > 11)
		hour += 20 - 12;
	    if (hour == 0 || hour == 20)
		hour += 12;
        }
        val.hr_ones = hour % 10;
        val.hr_tens = hour / 10;
        val.day_ones = t->tm_mday % 10;
        val.day_tens = t->tm_mday / 10;
        val.mon_ones = (t->tm_mon+1) % 10;
        val.mon_tens = (t->tm_mon+1) / 10;
        year = t->tm_year - 80;
        val.year_ones = year % 10;
        val.year_tens = year / 10;
        val.weekday = t->tm_wday;
        mste_write(&val);
        mste_rtc.mode=(mste_rtc.mode | 1);
        val.year_ones = (year % 4);	/* leap year register */
        mste_rtc.mode=(mste_rtc.mode & ~1);
    }
    else {
        mste_read(&val);
        t->tm_sec = val.sec_ones + val.sec_tens * 10;
        t->tm_min = val.min_ones + val.min_tens * 10;
        hour = val.hr_ones + val.hr_tens * 10;
	if (!hr24) {
	    if (hour == 12 || hour == 12 + 20)
		hour -= 12;
	    if (hour >= 20)
                hour += 12 - 20;
        }
	t->tm_hour = hour;
	t->tm_mday = val.day_ones + val.day_tens * 10;
        t->tm_mon  = val.mon_ones + val.mon_tens * 10 - 1;
        t->tm_year = val.year_ones + val.year_tens * 10 + 80;
        t->tm_wday = val.weekday;
    }
    return 0;
}

int atari_tt_hwclk( int op, struct rtc_time *t )
{
    int sec=0, min=0, hour=0, day=0, mon=0, year=0, wday=0;
    unsigned long	flags;
    unsigned char	ctrl;
    int pm = 0;

    ctrl = RTC_READ(RTC_CONTROL); /* control registers are
                                   * independent from the UIP */

    if (op) {
        /* write: prepare values */

        sec  = t->tm_sec;
        min  = t->tm_min;
        hour = t->tm_hour;
        day  = t->tm_mday;
        mon  = t->tm_mon + 1;
        year = t->tm_year - atari_rtc_year_offset;
        wday = t->tm_wday + (t->tm_wday >= 0);

        if (!(ctrl & RTC_24H)) {
	    if (hour > 11) {
		pm = 0x80;
		if (hour != 12)
		    hour -= 12;
	    }
	    else if (hour == 0)
		hour = 12;
        }

        if (!(ctrl & RTC_DM_BINARY)) {
	    sec = bin2bcd(sec);
	    min = bin2bcd(min);
	    hour = bin2bcd(hour);
	    day = bin2bcd(day);
	    mon = bin2bcd(mon);
	    year = bin2bcd(year);
	    if (wday >= 0)
		wday = bin2bcd(wday);
        }
    }

    /* Reading/writing the clock registers is a bit critical due to
     * the regular update cycle of the RTC. While an update is in
     * progress, registers 0..9 shouldn't be touched.
     * The problem is solved like that: If an update is currently in
     * progress (the UIP bit is set), the process sleeps for a while
     * (50ms). This really should be enough, since the update cycle
     * normally needs 2 ms.
     * If the UIP bit reads as 0, we have at least 244 usecs until the
     * update starts. This should be enough... But to be sure,
     * additionally the RTC_SET bit is set to prevent an update cycle.
     */

    while( RTC_READ(RTC_FREQ_SELECT) & RTC_UIP ) {
	if (in_atomic() || irqs_disabled())
	    mdelay(1);
	else
	    schedule_timeout_interruptible(HWCLK_POLL_INTERVAL);
    }

    local_irq_save(flags);
    RTC_WRITE( RTC_CONTROL, ctrl | RTC_SET );
    if (!op) {
        sec  = RTC_READ( RTC_SECONDS );
        min  = RTC_READ( RTC_MINUTES );
        hour = RTC_READ( RTC_HOURS );
        day  = RTC_READ( RTC_DAY_OF_MONTH );
        mon  = RTC_READ( RTC_MONTH );
        year = RTC_READ( RTC_YEAR );
        wday = RTC_READ( RTC_DAY_OF_WEEK );
    }
    else {
        RTC_WRITE( RTC_SECONDS, sec );
        RTC_WRITE( RTC_MINUTES, min );
        RTC_WRITE( RTC_HOURS, hour + pm);
        RTC_WRITE( RTC_DAY_OF_MONTH, day );
        RTC_WRITE( RTC_MONTH, mon );
        RTC_WRITE( RTC_YEAR, year );
        if (wday >= 0) RTC_WRITE( RTC_DAY_OF_WEEK, wday );
    }
    RTC_WRITE( RTC_CONTROL, ctrl & ~RTC_SET );
    local_irq_restore(flags);

    if (!op) {
        /* read: adjust values */

        if (hour & 0x80) {
	    hour &= ~0x80;
	    pm = 1;
	}

	if (!(ctrl & RTC_DM_BINARY)) {
	    sec = bcd2bin(sec);
	    min = bcd2bin(min);
	    hour = bcd2bin(hour);
	    day = bcd2bin(day);
	    mon = bcd2bin(mon);
	    year = bcd2bin(year);
	    wday = bcd2bin(wday);
        }

        if (!(ctrl & RTC_24H)) {
	    if (!pm && hour == 12)
		hour = 0;
	    else if (pm && hour != 12)
		hour += 12;
        }

        t->tm_sec  = sec;
        t->tm_min  = min;
        t->tm_hour = hour;
        t->tm_mday = day;
        t->tm_mon  = mon - 1;
        t->tm_year = year + atari_rtc_year_offset;
        t->tm_wday = wday - 1;
    }

    return( 0 );
}
