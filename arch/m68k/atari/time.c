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

#include <asm/atariints.h>

void __init
atari_sched_init(irqreturn_t (*timer_routine)(int, void *, struct pt_regs *))
{
    /* set Timer C data Register */
    mfp.tim_dt_c = INT_TICKS;
    /* start timer C, div = 1:100 */
    mfp.tim_ct_cd = (mfp.tim_ct_cd & 15) | 0x60;
    /* install interrupt service routine for MFP Timer C */
    request_irq(IRQ_MFP_TIMC, timer_routine, IRQ_TYPE_SLOW,
                "timer", timer_routine);
}

/* ++andreas: gettimeoffset fixed to check for pending interrupt */

#define TICK_SIZE 10000

/* This is always executed with interrupts disabled.  */
unsigned long atari_gettimeoffset (void)
{
  unsigned long ticks, offset = 0;

  /* read MFP timer C current value */
  ticks = mfp.tim_dt_c;
  /* The probability of underflow is less than 2% */
  if (ticks > INT_TICKS - INT_TICKS / 50)
    /* Check for pending timer interrupt */
    if (mfp.int_pn_b & (1 << 5))
      offset = TICK_SIZE;

  ticks = INT_TICKS - ticks;
  ticks = ticks * 10000L / INT_TICKS;

  return ticks + offset;
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
            BIN_TO_BCD(sec);
            BIN_TO_BCD(min);
            BIN_TO_BCD(hour);
            BIN_TO_BCD(day);
            BIN_TO_BCD(mon);
            BIN_TO_BCD(year);
            if (wday >= 0) BIN_TO_BCD(wday);
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
        current->state = TASK_INTERRUPTIBLE;
        schedule_timeout(HWCLK_POLL_INTERVAL);
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
            BCD_TO_BIN(sec);
            BCD_TO_BIN(min);
            BCD_TO_BIN(hour);
            BCD_TO_BIN(day);
            BCD_TO_BIN(mon);
            BCD_TO_BIN(year);
            BCD_TO_BIN(wday);
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


int atari_mste_set_clock_mmss (unsigned long nowtime)
{
    short real_seconds = nowtime % 60, real_minutes = (nowtime / 60) % 60;
    struct MSTE_RTC val;
    unsigned char rtc_minutes;

    mste_read(&val);
    rtc_minutes= val.min_ones + val.min_tens * 10;
    if ((rtc_minutes < real_minutes
         ? real_minutes - rtc_minutes
         : rtc_minutes - real_minutes) < 30)
    {
        val.sec_ones = real_seconds % 10;
        val.sec_tens = real_seconds / 10;
        val.min_ones = real_minutes % 10;
        val.min_tens = real_minutes / 10;
        mste_write(&val);
    }
    else
        return -1;
    return 0;
}

int atari_tt_set_clock_mmss (unsigned long nowtime)
{
    int retval = 0;
    short real_seconds = nowtime % 60, real_minutes = (nowtime / 60) % 60;
    unsigned char save_control, save_freq_select, rtc_minutes;

    save_control = RTC_READ (RTC_CONTROL); /* tell the clock it's being set */
    RTC_WRITE (RTC_CONTROL, save_control | RTC_SET);

    save_freq_select = RTC_READ (RTC_FREQ_SELECT); /* stop and reset prescaler */
    RTC_WRITE (RTC_FREQ_SELECT, save_freq_select | RTC_DIV_RESET2);

    rtc_minutes = RTC_READ (RTC_MINUTES);
    if (!(save_control & RTC_DM_BINARY))
        BCD_TO_BIN (rtc_minutes);

    /* Since we're only adjusting minutes and seconds, don't interfere
       with hour overflow.  This avoids messing with unknown time zones
       but requires your RTC not to be off by more than 30 minutes.  */
    if ((rtc_minutes < real_minutes
         ? real_minutes - rtc_minutes
         : rtc_minutes - real_minutes) < 30)
        {
            if (!(save_control & RTC_DM_BINARY))
                {
                    BIN_TO_BCD (real_seconds);
                    BIN_TO_BCD (real_minutes);
                }
            RTC_WRITE (RTC_SECONDS, real_seconds);
            RTC_WRITE (RTC_MINUTES, real_minutes);
        }
    else
        retval = -1;

    RTC_WRITE (RTC_FREQ_SELECT, save_freq_select);
    RTC_WRITE (RTC_CONTROL, save_control);
    return retval;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  tab-width: 8
 * End:
 */
