/* check_y2k.c -- test ntp code constructs for Y2K correctness 	Y2KFixes [*/

  /*
	Code invoked by `make check`. Not part of ntpd and not to be
	installed.

	On any code I even wonder about, I've cut and pasted the code
	here and ran it as a test case just to be sure.

	For code not in "ntpd" proper, we have tried to call most 
	repaired functions from herein to properly test them
	(something never done before!). This has found several bugs,
	not normal Y2K bugs, that will strike in Y2K so repair them
	we did.

	Program exits with 0 on success, 1 on Y2K failure (stdout messages).
	Exit of 2 indicates internal logic bug detected OR failure of
	what should be our correct formulas.

	While "make check" should only check logic for source within that
	specific directory, this check goes outside the scope of the local
	directory.  It's not a perfect world (besides, there is a lot of
	interdependence here, and it really needs to be tested in
	a controled order).
   */

/* { definitions lifted from ntpd.c to allow us to complie with 
     "#include ntp.h".  I have not taken the time to reduce the clutter. */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "ntpd.h"

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif
#include <stdio.h>
#include <errno.h>
#ifndef SYS_WINNT
# if !defined(VMS)	/*wjm*/
#  include <sys/param.h>
# endif /* VMS */
# if HAVE_SYS_SIGNAL_H
#  include <sys/signal.h>
# endif /* HAVE_SYS_SIGNAL_H */
# include <sys/signal.h>
# ifdef HAVE_SYS_IOCTL_H
#  include <sys/ioctl.h>
# endif /* HAVE_SYS_IOCTL_H */
# if !defined(VMS)	/*wjm*/
#  include <sys/resource.h>
# endif /* VMS */
#else
# include <signal.h>
# include <process.h>
# include <io.h>
# include "../libntp/log.h"
#endif /* SYS_WINNT */
#if defined(HAVE_RTPRIO)
# ifdef HAVE_SYS_RESOURCE_H
#  include <sys/resource.h>
# endif
# ifdef HAVE_SYS_LOCK_H
#  include <sys/lock.h>
# endif
# include <sys/rtprio.h>
#else
# ifdef HAVE_PLOCK
#  ifdef HAVE_SYS_LOCK_H
#	include <sys/lock.h>
#  endif
# endif
#endif
#if defined(HAVE_SCHED_SETSCHEDULER)
# ifdef HAVE_SCHED_H
#  include <sched.h>
# else
#  ifdef HAVE_SYS_SCHED_H
#   include <sys/sched.h>
#  endif
# endif
#endif
#if defined(HAVE_SYS_MMAN_H)
# include <sys/mman.h>
#endif

#ifdef HAVE_TERMIOS_H
# include <termios.h>
#endif

#ifdef SYS_DOMAINOS
# include <apollo/base.h>
#endif /* SYS_DOMAINOS */

/* } end definitions lifted from ntpd.c */

#include "ntp_calendar.h"
#include "parse.h"

#define GoodLeap(Year) (((Year)%4 || (!((Year)%100) && (Year)%400)) ? 0 : 13 )

char const *progname = "check_y2k";

long
Days ( int Year )		/* return number of days since year "0" */
{
    long  Return;
		/* this is a known to be good algorithm */
    Return = Year * 365;	/* first aproximation to the value */
    if ( Year >= 1 )
    {		/* see notes in libparse/parse.c if you want a PROPER
		 * **generic algorithm. */
	Return += (Year+3) / 4;		/* add in (too many) leap days */
	Return -= (Year-1) / 100;	/* reduce by (too many) centurys */
	Return += (Year-1) / 400;	/* get final answer */
    }

    return Return;
}

static int  year0 = 1900;	/* sarting year for NTP time */
static int  yearend;		/* ending year we test for NTP time.
				    * 32-bit systems: through 2036, the
				      **year in which NTP time overflows.
				    * 64-bit systems: a reasonable upper
				      **limit (well, maybe somewhat beyond
				      **reasonable, but well before the
				      **max time, by which time the earth
				      **will be dead.) */
static time_t Time;
static struct tm LocalTime;

#define Error(year) if ( (year)>=2036 && LocalTime.tm_year < 110 ) \
	Warnings++; else Fatals++

int
main( void )
{
    int Fatals;
    int Warnings;
    int  year;

    Time = time( (time_t *)NULL )
#ifdef TESTTIMEOFFSET
		+ test_time_offset
#endif
	;
    LocalTime = *localtime( &Time );

    year = ( sizeof( u_long ) > 4 ) 	/* save max span using year as temp */
		? ( 400 * 3 ) 		/* three greater gregorian cycles */
		: ((int)(0x7FFFFFFF / 365.242 / 24/60/60)* 2 ); /*32-bit limit*/
			/* NOTE: will automacially expand test years on
			 * 64 bit machines.... this may cause some of the
			 * existing ntp logic to fail for years beyond
			 * 2036 (the current 32-bit limit). If all checks
			 * fail ONLY beyond year 2036 you may ignore such
			 * errors, at least for a decade or so. */
    yearend = year0 + year;

    puts( " internal self check" );
  {		/* verify our own logic used to verify repairs */
    unsigned long days;

    if ( year0 >= yearend )
    {
	fprintf( stdout, "year0=%d NOT LESS THAN yearend=%d  (span=%d)\n",
		(int)year0, (int)yearend, (int)year );
	exit(2);
    }

   {
    int  save_year;

    save_year = LocalTime.tm_year;	/* save current year */

    year = 1980;
    LocalTime.tm_year = year - 1900;
    Fatals = Warnings = 0;
    Error(year);		/* should increment Fatals */
    if ( Fatals == 0 ) 
    {
	fprintf( stdout, 
	    "%4d: %s(%d): FATAL DID NOT INCREMENT  (Fatals=%d Warnings=%d)\n",
	    (int)year, __FILE__, __LINE__, (int)Fatals, (int)Warnings );
	exit(2);
    }

    year = 2100;		/* test year > limit but CURRENT year < limit */
    Fatals = Warnings = 0;
    Error(year);		/* should increment Fatals */
    if ( Warnings == 0 ) 
    {
	fprintf( stdout, 
	    "%4d: %s(%d): WARNING DID NOT INCREMENT  (Fatals=%d Warnings=%d)\n",
	    (int)year, __FILE__, __LINE__, (int)Fatals, (int)Warnings );
	exit(2);
    }
    Fatals = Warnings = 0;
    LocalTime.tm_year = year - 1900;	/* everything > limit */
    Error(1980);		/* should increment Fatals */
    if ( Fatals == 0 ) 
    {
	fprintf( stdout, 
	    "%4d: %s(%d): FATALS DID NOT INCREMENT  (Fatals=%d Warnings=%d)\n",
	    (int)year, __FILE__, __LINE__, (int)Fatals, (int)Warnings );
	exit(2);
    }

    LocalTime.tm_year = save_year;
   }

    days = 365+1;		/* days in year 0 + 1 more day */
    for ( year = 1; year <= 2500; year++ )
    {
	long   Test;
	Test = Days( year );
	if ( days != Test )
	{
	    fprintf( stdout, "%04d: Days() DAY COUNT ERROR: s/b=%ld was=%ld\n", 
		year, (long)days, (long)Test );
	    exit(2);		/* would throw off many other tests */
	}

	Test = julian0(year);		/* compare with julian0() macro */
	if ( days != Test )
	{
	    fprintf( stdout, "%04d: julian0() DAY COUNT ERROR: s/b=%ld was=%ld\n", 
		year, (long)days, (long)Test );
	    exit(2);		/* would throw off many other tests */
	}

	days += 365;
	if ( isleap_4(year) ) days++;
    }

    if ( isleap_4(1999) )
    {
	fprintf( stdout, "isleap_4(1999) REPORTED TRUE\n" );
	exit(2);
    }
    if ( !isleap_4(2000) )
    {
	fprintf( stdout, "isleap_4(2000) REPORTED FALSE\n" );
	exit(2);
    }
    if ( isleap_4(2001) )
    {
	fprintf( stdout, "isleap_4(1999) REPORTED TRUE\n" );
	exit(2);
    }

    if ( !isleap_tm(2000-1900) )
    {
	fprintf( stdout, "isleap_tm(100) REPORTED FALSE\n" );
	exit(2);
    }
  }

    Fatals = Warnings = 0;

    puts( " include/ntp.h" );
  {		/* test our new isleap_*() #define "functions" */
    
    for ( year = 1400; year <= 2200; year++ )
    {
	int  LeapSw;
	int  IsLeapSw;

	LeapSw = GoodLeap(year);
	IsLeapSw = isleap_4(year);

	if ( !!LeapSw != !!IsLeapSw )
	{
	    Error(year);
	    fprintf( stdout, 
		"  %4d %2d %3d *** ERROR\n", year, LeapSw, IsLeapSw );
	    break;
	}

	IsLeapSw = isleap_tm(year-1900);

	if ( !!LeapSw != !!IsLeapSw )
	{
	    Error(year);
	    fprintf( stdout, 
		"  %4d %2d %3d *** ERROR\n", year, LeapSw, IsLeapSw );
	    break;
	}
    }
  }

    puts( " include/ntp_calendar.h" );
  {		/* I belive this is good, but just to be sure... */

	/* we are testing this #define */
#define is_leapyear(y) (y%4 == 0 && !(y%100 == 0 && !(y%400 == 0)))

    for ( year = 1400; year <= 2200; year++ )
    {
	int  LeapSw;

	LeapSw = GoodLeap(year);

	if ( !(!LeapSw) != !(!is_leapyear(year)) )
	{
	    Error(year);
	    fprintf( stdout, 
		"  %4d %2d *** ERROR\n", year, LeapSw );
	    break;
	}
    }
  }   


    puts( " libparse/parse.c" );
  { 
    long Days1970;	/* days from 1900 to 1970 */

    struct ParseTime	/* womp up a test structure to all cut/paste code */
    {
       int   year;
    } Clock_Time, *clock_time;

    clock_time = &Clock_Time;

	/* first test this #define */
#define days_per_year(x)  ((x) % 4 ? 365 : ((x % 400) ? ((x % 100) ? 366 : 365) : 366))

    for ( year = 1400; year <= 2200; year++ )
    {
	int  LeapSw;
	int  DayCnt;

	LeapSw = GoodLeap(year);
	DayCnt = (int)days_per_year(year);

	if ( ( LeapSw ? 366 : 365 ) != DayCnt )
	{
	    Error(year);
	    fprintf( stdout, 
		    "  days_per_year() %4d %2d %3d *** ERROR\n", 
		    year, LeapSw, DayCnt );
	    break;
	}
    }

    /* test (what is now julian0) calculations */

    Days1970 = Days( 1970 );	/* get days since 1970 using a known good */

    for ( year = 1970; year < yearend; year++ )
    {				
	unsigned long t;
	long DaysYear ;

	clock_time->year = year;

	/* here is the code we are testing, cut and pasted out of the source */
#if 0		/* old BUGGY code that has Y2K (and many other) failures */
	    /* ghealton: this logic FAILED with great frequency when run
	     * over a period of time, including for year 2000. True, it
	     * had more successes than failures, but that's not really good
	     * enough for critical time distribution software.
	     * It is so awful I wonder if it has had a history of failure 
	     * and fixes? */
        t =  (clock_time->year - 1970) * 365;
        t += (clock_time->year >> 2) - (1970 >> 2);
        t -= clock_time->year / 100 - 1970 / 100;
        t += clock_time->year / 400 - 1970 / 400;

		/* (immediate feare of rounding errors on integer
		 * **divisions proved well founded) */

#else
	/* my replacement, based on Days() above */
	t = julian0(year) - julian0(1970);
#endif

	/* compare result in t against trusted calculations */
	DaysYear = Days( year );	/* get days to this year */
	if ( t != DaysYear - Days1970 )
	{
	    Error(year);
	    fprintf( stdout, 
		"  %4d 1970=%-8ld %4d=%-8ld %-3ld  t=%-8ld  *** ERROR ***\n",
		  year,      (long)Days1970,
				 year,
				     (long)DaysYear,
					   (long)(DaysYear - Days1970),
						   (long)t );
	}
    }

#if 1		/* { */
   {
    debug = 1;			/* enable debugging */
    for ( year = 1970; year < yearend; year++ )
    {		/* (limited by theory unix 2038 related bug lives by, but
		 * ends in yearend) */
	clocktime_t  ct;
	time_t	     Observed;
	time_t	     Expected;
	u_long       Flag;
	unsigned long t;

	ct.day = 1;
	ct.month = 1;
	ct.year = year;
	ct.hour = ct.minute = ct.second = ct.usecond = 0;
	ct.utcoffset = 0;
	ct.utctime = 0;
	ct.flags = 0;

	Flag = 0;
 	Observed = parse_to_unixtime( &ct, &Flag );
	if ( ct.year != year )
	{
	    fprintf( stdout, 
	       "%04d: parse_to_unixtime(,%d) CORRUPTED ct.year: was %d\n",
	       (int)year, (int)Flag, (int)ct.year );
	    Error(year);
	    break;
	}
	t = julian0(year) - julian0(1970);	/* Julian day from 1970 */
	Expected = t * 24 * 60 * 60;
	if ( Observed != Expected  ||  Flag )
	{   /* time difference */
	    fprintf( stdout, 
	       "%04d: parse_to_unixtime(,%d) FAILURE: was=%lu s/b=%lu  (%ld)\n",
	       year, (int)Flag, 
	       (unsigned long)Observed, (unsigned long)Expected,
	       ((long)Observed - (long)Expected) );
	    Error(year);
	    break;
	}

	if ( year >= YEAR_PIVOT+1900 )
	{
	    /* check year % 100 code we put into parse_to_unixtime() */
	    ct.utctime = 0;
	    ct.year = year % 100;
	    Flag = 0;

	    Observed = parse_to_unixtime( &ct, &Flag );

	    if ( Observed != Expected  ||  Flag )
	    {   /* time difference */
		fprintf( stdout, 
"%04d: parse_to_unixtime(%d,%d) FAILURE: was=%lu s/b=%lu  (%ld)\n",
		   year, (int)ct.year, (int)Flag, 
		   (unsigned long)Observed, (unsigned long)Expected,
		   ((long)Observed - (long)Expected) );
		Error(year);
		break;
	    }

	    /* check year - 1900 code we put into parse_to_unixtime() */
	    ct.utctime = 0;
	    ct.year = year - 1900;
	    Flag = 0;

	    Observed = parse_to_unixtime( &ct, &Flag );

	    if ( Observed != Expected  ||  Flag )
	    {   /* time difference */
		fprintf( stdout, 
"%04d: parse_to_unixtime(%d,%d) FAILURE: was=%lu s/b=%lu  (%ld)\n",
		   year, (int)ct.year, (int)Flag, 
		   (unsigned long)Observed, (unsigned long)Expected,
		   ((long)Observed - (long)Expected) );
		Error(year);
		break;
	    }


	}
    }
#endif		/* } */
   }
  }

    puts( " libntp/caljulian.c" );
  {		/* test caljulian() */
    struct	calendar  ot;
    u_long ntp_time;		/* NTP time */

    year = year0;		/* calculate the basic year */
    printf( "  starting year %04d\n", (int)year0 );
    printf( "  ending year   %04d\n", (int)yearend );


    ntp_time = julian0( year0 );		/* NTP starts in 1900-01-01 */
#if DAY_NTP_STARTS == 693596
    ntp_time -= 365;		/* BIAS required for successful test */
#endif
    if ( DAY_NTP_STARTS != ntp_time )
    {
	Error(year);
	fprintf( stdout, 
		"%04d: DAY_NTP_STARTS (%ld) NOT TRUE VALUE OF %ld (%ld)\n",
		(int)year0,
		(long)DAY_NTP_STARTS,  (long)ntp_time,
		(long)DAY_NTP_STARTS - (long)ntp_time );
    }

    for ( ; year < yearend; year++ )
    {
	
	/* 01-01 for the current year */
	ntp_time = Days( year ) - Days( year0 );  /* days into NTP time */
	ntp_time *= 24 * 60 * 60;	/* convert into seconds */
	caljulian( ntp_time, &ot );	/* convert January 1 */
	if ( ot.year  != year
	  || ot.month != 1
	  || ot.monthday != 1 )
	{
	    Error(year);
	    fprintf( stdout, "%lu: EXPECTED %04d-01-01: FOUND %04d-%02d-%02d\n",
			(unsigned long)ntp_time,
			year, 
			(int)ot.year, (int)ot.month, (int)ot.monthday );
	    break;
	}

	ntp_time += (31 + 28-1) * ( 24 * 60 * 60 );	/* advance to 02-28 */
	caljulian( ntp_time, &ot );	/* convert Feb 28 */
	if ( ot.year  != year
	  || ot.month != 2
	  || ot.monthday != 28 )
	{
	    Error(year);
	    fprintf( stdout, "%lu: EXPECTED %04d-02-28: FOUND %04d-%02d-%02d\n",
			(unsigned long)ntp_time,
			year, 
			(int)ot.year, (int)ot.month, (int)ot.monthday );
	    break;
	}

      {
	int    m;		/* expected month */
	int    d;		/* expected day */

	m = isleap_4(year) ?  2 : 3;
	d = isleap_4(year) ? 29 : 1;

	ntp_time += ( 24 * 60 * 60 );	/* advance to the next day */
	caljulian( ntp_time, &ot );	/* convert this day */
	if ( ot.year  != year
	  || ot.month != m
	  || ot.monthday != d )
	{
	    Error(year);
	    fprintf( stdout, "%lu: EXPECTED %04d-%02d-%02d: FOUND %04d-%02d-%02d\n",
			(unsigned long)ntp_time,
			year, m, d, 
			(int)ot.year, (int)ot.month, (int)ot.monthday );
	    break;
	}

      }
    }
  }

    puts( " libntp/caltontp.c" );
  {		/* test caltontp() */
    struct	calendar  ot;
    u_long      ntp_time;		/* NTP time */

    year = year0;		/* calculate the basic year */
    printf( "  starting year %04d\n", (int)year0 );
    printf( "  ending year   %04d\n", (int)yearend );


    for ( ; year < yearend; year++ )
    {
	u_long  ObservedNtp;
	
	/* 01-01 for the current year */
	ot.year = year;
	ot.month = ot.monthday = 1; 	/* unused, but set anyway JIC */
	ot.yearday = 1;		/* this is the magic value used by caltontp() */
	ot.hour = ot.minute = ot.second = 0;

	ntp_time = Days( year ) - Days( year0 );  /* days into NTP time */
	ntp_time *= 24 * 60 * 60;	/* convert into seconds */
	ObservedNtp = caltontp( &ot );
	if ( ntp_time != ObservedNtp )
	{
	    Error(year);
	    fprintf( stdout, "%d: EXPECTED %lu: FOUND %lu (%ld)\n",
			(int)year,
			(unsigned long)ntp_time, (unsigned long)ObservedNtp ,
			(long)ntp_time - (long)ObservedNtp );

	    break;
	}

	/* now call caljulian as a type of failsafe supercheck */
	caljulian( ObservedNtp, &ot );	/* convert January 1 */
	if ( ot.year  != year
	  || ot.month != 1
	  || ot.monthday != 1 )
	{
	    Error(year);
	    fprintf( stdout, "%lu: caljulian FAILSAFE EXPECTED %04d-01-01: FOUND %04d-%02d-%02d\n",
			(unsigned long)ObservedNtp,
			year, 
			(int)ot.year, (int)ot.month, (int)ot.monthday );
	    break;
	}
    }
  }

   if ( Warnings > 0 )
       fprintf( stdout, "%d WARNINGS\n",  Warnings );
   if ( Fatals > 0 )
       fprintf( stdout, "%d FATAL ERRORS\n",  Fatals );
   return Fatals ? 1 : 0;
}
							/* Y2KFixes ] */
