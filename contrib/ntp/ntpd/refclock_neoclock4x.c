/*
 *
 * Refclock_neoclock4x.c
 * - NeoClock4X driver for DCF77 or FIA Timecode
 *
 * Date: 2009-12-04 v1.16
 *
 * see http://www.linum.com/redir/jump/id=neoclock4x&action=redir
 * for details about the NeoClock4X device
 *
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#if defined(REFCLOCK) && (defined(CLOCK_NEOCLOCK4X))

#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <ctype.h>

#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_control.h"
#include "ntp_refclock.h"
#include "ntp_unixtime.h"
#include "ntp_stdlib.h"

#if defined HAVE_SYS_MODEM_H
# include <sys/modem.h>
# ifndef __QNXNTO__
#  define TIOCMSET MCSETA
#  define TIOCMGET MCGETA
#  define TIOCM_RTS MRTS
# endif
#endif

#ifdef HAVE_TERMIOS_H
# ifdef TERMIOS_NEEDS__SVID3
#  define _SVID3
# endif
# include <termios.h>
# ifdef TERMIOS_NEEDS__SVID3
#  undef _SVID3
# endif
#endif

#ifdef HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif

/*
 * NTP version 4.20 change the pp->msec field to pp->nsec.
 * To allow to support older ntp versions with this sourcefile
 * you can define NTP_PRE_420 to allow this driver to compile
 * with ntp version back to 4.1.2.
 *
 */
#if 0
#define NTP_PRE_420
#endif

/*
 * If you want the driver for whatever reason to not use
 * the TX line to send anything to your NeoClock4X
 * device you must tell the NTP refclock driver which
 * firmware you NeoClock4X device uses.
 *
 * If you want to enable this feature change the "#if 0"
 * line to "#if 1" and make sure that the defined firmware
 * matches the firmware off your NeoClock4X receiver!
 *
 */

#if 0
#define NEOCLOCK4X_FIRMWARE                NEOCLOCK4X_FIRMWARE_VERSION_A
#endif

/* at this time only firmware version A is known */
#define NEOCLOCK4X_FIRMWARE_VERSION_A      'A'

#define NEOCLOCK4X_TIMECODELEN 37

#define NEOCLOCK4X_OFFSET_SERIAL            3
#define NEOCLOCK4X_OFFSET_RADIOSIGNAL       9
#define NEOCLOCK4X_OFFSET_DAY              12
#define NEOCLOCK4X_OFFSET_MONTH            14
#define NEOCLOCK4X_OFFSET_YEAR             16
#define NEOCLOCK4X_OFFSET_HOUR             18
#define NEOCLOCK4X_OFFSET_MINUTE           20
#define NEOCLOCK4X_OFFSET_SECOND           22
#define NEOCLOCK4X_OFFSET_HSEC             24
#define NEOCLOCK4X_OFFSET_DOW              26
#define NEOCLOCK4X_OFFSET_TIMESOURCE       28
#define NEOCLOCK4X_OFFSET_DSTSTATUS        29
#define NEOCLOCK4X_OFFSET_QUARZSTATUS      30
#define NEOCLOCK4X_OFFSET_ANTENNA1         31
#define NEOCLOCK4X_OFFSET_ANTENNA2         33
#define NEOCLOCK4X_OFFSET_CRC              35

#define NEOCLOCK4X_DRIVER_VERSION          "1.16 (2009-12-04)"

#define NSEC_TO_MILLI                      1000000

struct neoclock4x_unit {
  l_fp	laststamp;	/* last receive timestamp */
  short	unit;		/* NTP refclock unit number */
  u_long polled;	/* flag to detect noreplies */
  char	leap_status;	/* leap second flag */
  int	recvnow;

  char  firmware[80];
  char  firmwaretag;
  char  serial[7];
  char  radiosignal[4];
  char  timesource;
  char  dststatus;
  char  quarzstatus;
  int   antenna1;
  int   antenna2;
  int   utc_year;
  int   utc_month;
  int   utc_day;
  int   utc_hour;
  int   utc_minute;
  int   utc_second;
  int   utc_msec;
};

static	int	neoclock4x_start	(int, struct peer *);
static	void	neoclock4x_shutdown	(int, struct peer *);
static	void	neoclock4x_receive	(struct recvbuf *);
static	void	neoclock4x_poll		(int, struct peer *);
static	void	neoclock4x_control	(int, const struct refclockstat *, struct refclockstat *, struct peer *);

static int	neol_atoi_len		(const char str[], int *, int);
static int	neol_hexatoi_len	(const char str[], int *, int);
static void	neol_jdn_to_ymd		(unsigned long, int *, int *, int *);
static void	neol_localtime		(unsigned long, int* , int*, int*, int*, int*, int*);
static unsigned long neol_mktime	(int, int, int, int, int, int);
#if !defined(NEOCLOCK4X_FIRMWARE)
static int	neol_query_firmware	(int, int, char *, size_t);
static int	neol_check_firmware	(int, const char*, char *);
#endif

struct refclock refclock_neoclock4x = {
  neoclock4x_start,	/* start up driver */
  neoclock4x_shutdown,	/* shut down driver */
  neoclock4x_poll,	/* transmit poll message */
  neoclock4x_control,
  noentry,		/* initialize driver (not used) */
  noentry,		/* not used */
  NOFLAGS			/* not used */
};

static int
neoclock4x_start(int unit,
		 struct peer *peer)
{
  struct neoclock4x_unit *up;
  struct refclockproc *pp;
  int fd;
  char dev[20];
  int sl232;
#if defined(HAVE_TERMIOS)
  struct termios termsettings;
#endif
#if !defined(NEOCLOCK4X_FIRMWARE)
  int tries;
#endif

  (void) snprintf(dev, sizeof(dev)-1, "/dev/neoclock4x-%d", unit);

  /* LDISC_STD, LDISC_RAW
   * Open serial port. Use CLK line discipline, if available.
   */
  fd = refclock_open(dev, B2400, LDISC_STD);
  if(fd <= 0)
    {
      return (0);
    }

#if defined(HAVE_TERMIOS)

#if 1
  if(tcgetattr(fd, &termsettings) < 0)
    {
      msyslog(LOG_CRIT, "NeoClock4X(%d): (tcgetattr) can't query serial port settings: %m", unit);
      (void) close(fd);
      return (0);
    }

  /* 2400 Baud 8N2 */
  termsettings.c_iflag = IGNBRK | IGNPAR | ICRNL;
  termsettings.c_oflag = 0;
  termsettings.c_cflag = CS8 | CSTOPB | CLOCAL | CREAD;
  (void)cfsetispeed(&termsettings, (u_int)B2400);
  (void)cfsetospeed(&termsettings, (u_int)B2400);

  if(tcsetattr(fd, TCSANOW, &termsettings) < 0)
    {
      msyslog(LOG_CRIT, "NeoClock4X(%d): (tcsetattr) can't set serial port 2400 8N2: %m", unit);
      (void) close(fd);
      return (0);
    }

#else
  if(tcgetattr(fd, &termsettings) < 0)
    {
      msyslog(LOG_CRIT, "NeoClock4X(%d): (tcgetattr) can't query serial port settings: %m", unit);
      (void) close(fd);
      return (0);
    }

  /* 2400 Baud 8N2 */
  termsettings.c_cflag &= ~PARENB;
  termsettings.c_cflag |= CSTOPB;
  termsettings.c_cflag &= ~CSIZE;
  termsettings.c_cflag |= CS8;

  if(tcsetattr(fd, TCSANOW, &termsettings) < 0)
    {
      msyslog(LOG_CRIT, "NeoClock4X(%d): (tcsetattr) can't set serial port 2400 8N2: %m", unit);
      (void) close(fd);
      return (0);
    }
#endif

#elif defined(HAVE_SYSV_TTYS)
  if(ioctl(fd, TCGETA, &termsettings) < 0)
    {
      msyslog(LOG_CRIT, "NeoClock4X(%d): (TCGETA) can't query serial port settings: %m", unit);
      (void) close(fd);
      return (0);
    }

  /* 2400 Baud 8N2 */
  termsettings.c_cflag &= ~PARENB;
  termsettings.c_cflag |= CSTOPB;
  termsettings.c_cflag &= ~CSIZE;
  termsettings.c_cflag |= CS8;

  if(ioctl(fd, TCSETA, &termsettings) < 0)
    {
      msyslog(LOG_CRIT, "NeoClock4X(%d): (TSGETA) can't set serial port 2400 8N2: %m", unit);
      (void) close(fd);
      return (0);
    }
#else
  msyslog(LOG_EMERG, "NeoClock4X(%d): don't know how to set port to 2400 8N2 with this OS!", unit);
  (void) close(fd);
  return (0);
#endif

#if defined(TIOCMSET) && (defined(TIOCM_RTS) || defined(CIOCM_RTS))
  /* turn on RTS, and DTR for power supply */
  /* NeoClock4x is powered from serial line */
  if(ioctl(fd, TIOCMGET, (caddr_t)&sl232) == -1)
    {
      msyslog(LOG_CRIT, "NeoClock4X(%d): can't query RTS/DTR state: %m", unit);
      (void) close(fd);
      return (0);
    }
#ifdef TIOCM_RTS
  sl232 = sl232 | TIOCM_DTR | TIOCM_RTS;	/* turn on RTS, and DTR for power supply */
#else
  sl232 = sl232 | CIOCM_DTR | CIOCM_RTS;	/* turn on RTS, and DTR for power supply */
#endif
  if(ioctl(fd, TIOCMSET, (caddr_t)&sl232) == -1)
    {
      msyslog(LOG_CRIT, "NeoClock4X(%d): can't set RTS/DTR to power neoclock4x: %m", unit);
      (void) close(fd);
      return (0);
    }
#else
  msyslog(LOG_EMERG, "NeoClock4X(%d): don't know how to set DTR/RTS to power NeoClock4X with this OS!",
	  unit);
  (void) close(fd);
  return (0);
#endif

  up = (struct neoclock4x_unit *) emalloc(sizeof(struct neoclock4x_unit));
  if(!(up))
    {
      msyslog(LOG_ERR, "NeoClock4X(%d): can't allocate memory for: %m",unit);
      (void) close(fd);
      return (0);
    }

  memset((char *)up, 0, sizeof(struct neoclock4x_unit));
  pp = peer->procptr;
  pp->clockdesc = "NeoClock4X";
  pp->unitptr = up;
  pp->io.clock_recv = neoclock4x_receive;
  pp->io.srcclock = peer;
  pp->io.datalen = 0;
  pp->io.fd = fd;
  /*
   * no fudge time is given by user!
   * use 169.583333 ms to compensate the serial line delay
   * formula is:
   * 2400 Baud / 11 bit = 218.18 charaters per second
   *  (NeoClock4X timecode len)
   */
  pp->fudgetime1 = (NEOCLOCK4X_TIMECODELEN * 11) / 2400.0;

  /*
   * Initialize miscellaneous variables
   */
  peer->precision = -10;
  memcpy((char *)&pp->refid, "neol", 4);

  up->leap_status = 0;
  up->unit = unit;
  strlcpy(up->firmware, "?", sizeof(up->firmware));
  up->firmwaretag = '?';
  strlcpy(up->serial, "?", sizeof(up->serial));
  strlcpy(up->radiosignal, "?", sizeof(up->radiosignal));
  up->timesource  = '?';
  up->dststatus   = '?';
  up->quarzstatus = '?';
  up->antenna1    = -1;
  up->antenna2    = -1;
  up->utc_year    = 0;
  up->utc_month   = 0;
  up->utc_day     = 0;
  up->utc_hour    = 0;
  up->utc_minute  = 0;
  up->utc_second  = 0;
  up->utc_msec    = 0;

#if defined(NEOCLOCK4X_FIRMWARE)
#if NEOCLOCK4X_FIRMWARE == NEOCLOCK4X_FIRMWARE_VERSION_A
  strlcpy(up->firmware, "(c) 2002 NEOL S.A. FRANCE / L0.01 NDF:A:* (compile time)",
	  sizeof(up->firmware));
  up->firmwaretag = 'A';
#else
  msyslog(LOG_EMERG, "NeoClock4X(%d): unknown firmware defined at compile time for NeoClock4X",
	  unit);
  (void) close(fd);
  pp->io.fd = -1;
  free(pp->unitptr);
  pp->unitptr = NULL;
  return (0);
#endif
#else
  for(tries=0; tries < 5; tries++)
    {
      NLOG(NLOG_CLOCKINFO)
	msyslog(LOG_INFO, "NeoClock4X(%d): checking NeoClock4X firmware version (%d/5)", unit, tries);
      /* wait 3 seconds for receiver to power up */
      sleep(3);
      if(neol_query_firmware(pp->io.fd, up->unit, up->firmware, sizeof(up->firmware)))
	{
	  break;
	}
    }

  /* can I handle this firmware version? */
  if(!neol_check_firmware(up->unit, up->firmware, &up->firmwaretag))
    {
      (void) close(fd);
      pp->io.fd = -1;
      free(pp->unitptr);
      pp->unitptr = NULL;
      return (0);
    }
#endif

  if(!io_addclock(&pp->io))
    {
      msyslog(LOG_ERR, "NeoClock4X(%d): error add peer to ntpd: %m", unit);
      (void) close(fd);
      pp->io.fd = -1;
      free(pp->unitptr);
      pp->unitptr = NULL;
      return (0);
    }

  NLOG(NLOG_CLOCKINFO)
    msyslog(LOG_INFO, "NeoClock4X(%d): receiver setup successful done", unit);

  return (1);
}

static void
neoclock4x_shutdown(int unit,
		   struct peer *peer)
{
  struct neoclock4x_unit *up;
  struct refclockproc *pp;
  int sl232;

  if(NULL != peer)
    {
      pp = peer->procptr;
      if(pp != NULL)
        {
          up = pp->unitptr;
          if(up != NULL)
            {
              if(-1 !=  pp->io.fd)
                {
#if defined(TIOCMSET) && (defined(TIOCM_RTS) || defined(CIOCM_RTS))
                  /* turn on RTS, and DTR for power supply */
                  /* NeoClock4x is powered from serial line */
                  if(ioctl(pp->io.fd, TIOCMGET, (caddr_t)&sl232) == -1)
                    {
                      msyslog(LOG_CRIT, "NeoClock4X(%d): can't query RTS/DTR state: %m",
                              unit);
                    }
#ifdef TIOCM_RTS
                  /* turn on RTS, and DTR for power supply */
                  sl232 &= ~(TIOCM_DTR | TIOCM_RTS);
#else
                  /* turn on RTS, and DTR for power supply */
                  sl232 &= ~(CIOCM_DTR | CIOCM_RTS);
#endif
                  if(ioctl(pp->io.fd, TIOCMSET, (caddr_t)&sl232) == -1)
                    {
                      msyslog(LOG_CRIT, "NeoClock4X(%d): can't set RTS/DTR to power neoclock4x: %m",
                              unit);
                    }
#endif
                  io_closeclock(&pp->io);
                }
              free(up);
              pp->unitptr = NULL;
            }
        }
    }

  msyslog(LOG_ERR, "NeoClock4X(%d): shutdown", unit);

  NLOG(NLOG_CLOCKINFO)
    msyslog(LOG_INFO, "NeoClock4X(%d): receiver shutdown done", unit);
}

static void
neoclock4x_receive(struct recvbuf *rbufp)
{
  struct neoclock4x_unit *up;
  struct refclockproc *pp;
  struct peer *peer;
  unsigned long calc_utc;
  int day;
  int month;	/* ddd conversion */
  int c;
  int dsec;
  unsigned char calc_chksum;
  int recv_chksum;

  peer = rbufp->recv_peer;
  pp = peer->procptr;
  up = pp->unitptr;

  /* wait till poll interval is reached */
  if(0 == up->recvnow)
    return;

  /* reset poll interval flag */
  up->recvnow = 0;

  /* read last received timecode */
  pp->lencode = refclock_gtlin(rbufp, pp->a_lastcode, BMAX, &pp->lastrec);
  pp->leap = LEAP_NOWARNING;

  if(NEOCLOCK4X_TIMECODELEN != pp->lencode)
    {
      NLOG(NLOG_CLOCKEVENT)
	msyslog(LOG_WARNING, "NeoClock4X(%d): received data has invalid length, expected %d bytes, received %d bytes: %s",
		up->unit, NEOCLOCK4X_TIMECODELEN, pp->lencode, pp->a_lastcode);
      refclock_report(peer, CEVNT_BADREPLY);
      return;
    }

  neol_hexatoi_len(&pp->a_lastcode[NEOCLOCK4X_OFFSET_CRC], &recv_chksum, 2);

  /* calculate checksum */
  calc_chksum = 0;
  for(c=0; c < NEOCLOCK4X_OFFSET_CRC; c++)
    {
      calc_chksum += pp->a_lastcode[c];
    }
  if(recv_chksum != calc_chksum)
    {
      NLOG(NLOG_CLOCKEVENT)
	msyslog(LOG_WARNING, "NeoClock4X(%d): received data has invalid chksum: %s",
		up->unit, pp->a_lastcode);
      refclock_report(peer, CEVNT_BADREPLY);
      return;
    }

  /* Allow synchronization even is quartz clock is
   * never initialized.
   * WARNING: This is dangerous!
   */
  up->quarzstatus = pp->a_lastcode[NEOCLOCK4X_OFFSET_QUARZSTATUS];
  if(0==(pp->sloppyclockflag & CLK_FLAG2))
    {
      if('I' != up->quarzstatus)
	{
	  NLOG(NLOG_CLOCKEVENT)
	    msyslog(LOG_NOTICE, "NeoClock4X(%d): quartz clock is not initialized: %s",
		    up->unit, pp->a_lastcode);
	  pp->leap = LEAP_NOTINSYNC;
	  refclock_report(peer, CEVNT_BADDATE);
	  return;
	}
    }
  if('I' != up->quarzstatus)
    {
      NLOG(NLOG_CLOCKEVENT)
	msyslog(LOG_NOTICE, "NeoClock4X(%d): using uninitialized quartz clock for time synchronization: %s",
		up->unit, pp->a_lastcode);
    }

  /*
   * If NeoClock4X is not synchronized to a radio clock
   * check if we're allowed to synchronize with the quartz
   * clock.
   */
  up->timesource = pp->a_lastcode[NEOCLOCK4X_OFFSET_TIMESOURCE];
  if(0==(pp->sloppyclockflag & CLK_FLAG2))
    {
      if('A' != up->timesource)
	{
	  /* not allowed to sync with quartz clock */
	  if(0==(pp->sloppyclockflag & CLK_FLAG1))
	    {
	      refclock_report(peer, CEVNT_BADTIME);
	      pp->leap = LEAP_NOTINSYNC;
	      return;
	    }
	}
    }

  /* this should only used when first install is done */
  if(pp->sloppyclockflag & CLK_FLAG4)
    {
      msyslog(LOG_DEBUG, "NeoClock4X(%d): received data: %s",
	      up->unit, pp->a_lastcode);
    }

  /* 123456789012345678901234567890123456789012345 */
  /* S/N123456DCF1004021010001202ASX1213CR\r\n */

  neol_atoi_len(&pp->a_lastcode[NEOCLOCK4X_OFFSET_YEAR], &pp->year, 2);
  neol_atoi_len(&pp->a_lastcode[NEOCLOCK4X_OFFSET_MONTH], &month, 2);
  neol_atoi_len(&pp->a_lastcode[NEOCLOCK4X_OFFSET_DAY], &day, 2);
  neol_atoi_len(&pp->a_lastcode[NEOCLOCK4X_OFFSET_HOUR], &pp->hour, 2);
  neol_atoi_len(&pp->a_lastcode[NEOCLOCK4X_OFFSET_MINUTE], &pp->minute, 2);
  neol_atoi_len(&pp->a_lastcode[NEOCLOCK4X_OFFSET_SECOND], &pp->second, 2);
  neol_atoi_len(&pp->a_lastcode[NEOCLOCK4X_OFFSET_HSEC], &dsec, 2);
#if defined(NTP_PRE_420)
  pp->msec = dsec * 10; /* convert 1/100s from neoclock to real miliseconds */
#else
  pp->nsec = dsec * 10 * NSEC_TO_MILLI; /* convert 1/100s from neoclock to nanoseconds */
#endif

  memcpy(up->radiosignal, &pp->a_lastcode[NEOCLOCK4X_OFFSET_RADIOSIGNAL], 3);
  up->radiosignal[3] = 0;
  memcpy(up->serial, &pp->a_lastcode[NEOCLOCK4X_OFFSET_SERIAL], 6);
  up->serial[6] = 0;
  up->dststatus = pp->a_lastcode[NEOCLOCK4X_OFFSET_DSTSTATUS];
  neol_hexatoi_len(&pp->a_lastcode[NEOCLOCK4X_OFFSET_ANTENNA1], &up->antenna1, 2);
  neol_hexatoi_len(&pp->a_lastcode[NEOCLOCK4X_OFFSET_ANTENNA2], &up->antenna2, 2);

  /*
    Validate received values at least enough to prevent internal
    array-bounds problems, etc.
  */
  if((pp->hour < 0) || (pp->hour > 23) ||
     (pp->minute < 0) || (pp->minute > 59) ||
     (pp->second < 0) || (pp->second > 60) /*Allow for leap seconds.*/ ||
     (day < 1) || (day > 31) ||
     (month < 1) || (month > 12) ||
     (pp->year < 0) || (pp->year > 99)) {
    /* Data out of range. */
    NLOG(NLOG_CLOCKEVENT)
      msyslog(LOG_WARNING, "NeoClock4X(%d): date/time out of range: %s",
	      up->unit, pp->a_lastcode);
    refclock_report(peer, CEVNT_BADDATE);
    return;
  }

  /* Year-2000 check not needed anymore. Same problem
   * will arise at 2099 but what should we do...?
   *
   * wrap 2-digit date into 4-digit
   *
   * if(pp->year < YEAR_PIVOT)
   * {
   *   pp->year += 100;
   * }
  */
  pp->year += 2000;

  /* adjust NeoClock4X local time to UTC */
  calc_utc = neol_mktime(pp->year, month, day, pp->hour, pp->minute, pp->second);
  calc_utc -= 3600;
  /* adjust NeoClock4X daylight saving time if needed */
  if('S' == up->dststatus)
    calc_utc -= 3600;
  neol_localtime(calc_utc, &pp->year, &month, &day, &pp->hour, &pp->minute, &pp->second);

  /*
    some preparations
  */
  pp->day = ymd2yd(pp->year, month, day);
  pp->leap = 0;

  if(pp->sloppyclockflag & CLK_FLAG4)
    {
      msyslog(LOG_DEBUG, "NeoClock4X(%d): calculated UTC date/time: %04d-%02d-%02d %02d:%02d:%02d.%03ld",
	      up->unit,
	      pp->year, month, day,
	      pp->hour, pp->minute, pp->second,
#if defined(NTP_PRE_420)
              pp->msec
#else
              pp->nsec/NSEC_TO_MILLI
#endif
              );
    }

  up->utc_year   = pp->year;
  up->utc_month  = month;
  up->utc_day    = day;
  up->utc_hour   = pp->hour;
  up->utc_minute = pp->minute;
  up->utc_second = pp->second;
#if defined(NTP_PRE_420)
  up->utc_msec   = pp->msec;
#else
  up->utc_msec   = pp->nsec/NSEC_TO_MILLI;
#endif

  if(!refclock_process(pp))
    {
      NLOG(NLOG_CLOCKEVENT)
	msyslog(LOG_WARNING, "NeoClock4X(%d): refclock_process failed!", up->unit);
      refclock_report(peer, CEVNT_FAULT);
      return;
    }
  refclock_receive(peer);

  /* report good status */
  refclock_report(peer, CEVNT_NOMINAL);

  record_clock_stats(&peer->srcadr, pp->a_lastcode);
}

static void
neoclock4x_poll(int unit,
		struct peer *peer)
{
  struct neoclock4x_unit *up;
  struct refclockproc *pp;

  pp = peer->procptr;
  up = pp->unitptr;

  pp->polls++;
  up->recvnow = 1;
}

static void
neoclock4x_control(int unit,
		   const struct refclockstat *in,
		   struct refclockstat *out,
		   struct peer *peer)
{
  struct neoclock4x_unit *up;
  struct refclockproc *pp;

  if(NULL == peer)
    {
      msyslog(LOG_ERR, "NeoClock4X(%d): control: unit invalid/inactive", unit);
      return;
    }

  pp = peer->procptr;
  if(NULL == pp)
    {
      msyslog(LOG_ERR, "NeoClock4X(%d): control: unit invalid/inactive", unit);
      return;
    }

  up = pp->unitptr;
  if(NULL == up)
    {
      msyslog(LOG_ERR, "NeoClock4X(%d): control: unit invalid/inactive", unit);
      return;
    }

  if(NULL != in)
    {
      /* check to see if a user supplied time offset is given */
      if(in->haveflags & CLK_HAVETIME1)
	{
	  pp->fudgetime1 = in->fudgetime1;
	  NLOG(NLOG_CLOCKINFO)
	    msyslog(LOG_NOTICE, "NeoClock4X(%d): using fudgetime1 with %0.5fs from ntp.conf.",
		    unit, pp->fudgetime1);
	}

      /* notify */
      if(pp->sloppyclockflag & CLK_FLAG1)
	{
	  NLOG(NLOG_CLOCKINFO)
	    msyslog(LOG_NOTICE, "NeoClock4X(%d): quartz clock is used to synchronize time if radio clock has no reception.", unit);
	}
      else
	{
	  NLOG(NLOG_CLOCKINFO)
	    msyslog(LOG_NOTICE, "NeoClock4X(%d): time is only adjusted with radio signal reception.", unit);
	}
    }

  if(NULL != out)
    {
      char *tt;
      char tmpbuf[80];

      out->kv_list = (struct ctl_var *)0;
      out->type    = REFCLK_NEOCLOCK4X;

      snprintf(tmpbuf, sizeof(tmpbuf)-1,
	       "%04d-%02d-%02d %02d:%02d:%02d.%03d",
	       up->utc_year, up->utc_month, up->utc_day,
	       up->utc_hour, up->utc_minute, up->utc_second,
	       up->utc_msec);
      tt = add_var(&out->kv_list, sizeof(tmpbuf)-1, RO|DEF);
      snprintf(tt, sizeof(tmpbuf)-1, "calc_utc=\"%s\"", tmpbuf);

      tt = add_var(&out->kv_list, 40, RO|DEF);
      snprintf(tt, 39, "radiosignal=\"%s\"", up->radiosignal);
      tt = add_var(&out->kv_list, 40, RO|DEF);
      snprintf(tt, 39, "antenna1=\"%d\"", up->antenna1);
      tt = add_var(&out->kv_list, 40, RO|DEF);
      snprintf(tt, 39, "antenna2=\"%d\"", up->antenna2);
      tt = add_var(&out->kv_list, 40, RO|DEF);
      if('A' == up->timesource)
	snprintf(tt, 39, "timesource=\"radio\"");
      else if('C' == up->timesource)
	snprintf(tt, 39, "timesource=\"quartz\"");
      else
	snprintf(tt, 39, "timesource=\"unknown\"");
      tt = add_var(&out->kv_list, 40, RO|DEF);
      if('I' == up->quarzstatus)
	snprintf(tt, 39, "quartzstatus=\"synchronized\"");
      else if('X' == up->quarzstatus)
        snprintf(tt, 39, "quartzstatus=\"not synchronized\"");
      else
	snprintf(tt, 39, "quartzstatus=\"unknown\"");
      tt = add_var(&out->kv_list, 40, RO|DEF);
      if('S' == up->dststatus)
        snprintf(tt, 39, "dststatus=\"summer\"");
      else if('W' == up->dststatus)
        snprintf(tt, 39, "dststatus=\"winter\"");
      else
        snprintf(tt, 39, "dststatus=\"unknown\"");
      tt = add_var(&out->kv_list, 80, RO|DEF);
      snprintf(tt, 79, "firmware=\"%s\"", up->firmware);
      tt = add_var(&out->kv_list, 40, RO|DEF);
      snprintf(tt, 39, "firmwaretag=\"%c\"", up->firmwaretag);
      tt = add_var(&out->kv_list, 80, RO|DEF);
      snprintf(tt, 79, "driver version=\"%s\"", NEOCLOCK4X_DRIVER_VERSION);
      tt = add_var(&out->kv_list, 80, RO|DEF);
      snprintf(tt, 79, "serialnumber=\"%s\"", up->serial);
    }
}

static int
neol_hexatoi_len(const char str[],
		 int *result,
		 int maxlen)
{
  int hexdigit;
  int i;
  int n = 0;

  for(i=0; isxdigit((unsigned char)str[i]) && i < maxlen; i++)
    {
      hexdigit = isdigit((unsigned char)str[i]) ? toupper((unsigned char)str[i]) - '0' : toupper((unsigned char)str[i]) - 'A' + 10;
      n = 16 * n + hexdigit;
    }
  *result = n;
  return (n);
}

static int
neol_atoi_len(const char str[],
		  int *result,
		  int maxlen)
{
  int digit;
  int i;
  int n = 0;

  for(i=0; isdigit((unsigned char)str[i]) && i < maxlen; i++)
    {
      digit = str[i] - '0';
      n = 10 * n + digit;
    }
  *result = n;
  return (n);
}

/* Converts Gregorian date to seconds since 1970-01-01 00:00:00.
 * Assumes input in normal date format, i.e. 1980-12-31 23:59:59
 * => year=1980, mon=12, day=31, hour=23, min=59, sec=59.
 *
 * [For the Julian calendar (which was used in Russia before 1917,
 * Britain & colonies before 1752, anywhere else before 1582,
 * and is still in use by some communities) leave out the
 * -year/100+year/400 terms, and add 10.]
 *
 * This algorithm was first published by Gauss (I think).
 *
 * WARNING: this function will overflow on 2106-02-07 06:28:16 on
 * machines were long is 32-bit! (However, as time_t is signed, we
 * will already get problems at other places on 2038-01-19 03:14:08)
 */
static unsigned long
neol_mktime(int year,
	    int mon,
	    int day,
	    int hour,
	    int min,
	    int sec)
{
  if (0 >= (int) (mon -= 2)) {    /* 1..12 . 11,12,1..10 */
    mon += 12;      /* Puts Feb last since it has leap day */
    year -= 1;
  }
  return (((
            (unsigned long)(year/4 - year/100 + year/400 + 367*mon/12 + day) +
            year*365 - 719499
            )*24 + hour /* now have hours */
           )*60 + min /* now have minutes */
          )*60 + sec; /* finally seconds */
}

static void
neol_localtime(unsigned long utc,
	       int* year,
	       int* month,
	       int* day,
	       int* hour,
	       int* min,
	       int* sec)
{
  *sec = utc % 60;
  utc /= 60;
  *min = utc % 60;
  utc /= 60;
  *hour = utc % 24;
  utc /= 24;

  /*             JDN Date 1/1/1970 */
  neol_jdn_to_ymd(utc + 2440588L, year, month, day);
}

static void
neol_jdn_to_ymd(unsigned long jdn,
		int *yy,
		int *mm,
		int *dd)
{
  unsigned long x, z, m, d, y;
  unsigned long daysPer400Years = 146097UL;
  unsigned long fudgedDaysPer4000Years = 1460970UL + 31UL;

  x = jdn + 68569UL;
  z = 4UL * x / daysPer400Years;
  x = x - (daysPer400Years * z + 3UL) / 4UL;
  y = 4000UL * (x + 1) / fudgedDaysPer4000Years;
  x = x - 1461UL * y / 4UL + 31UL;
  m = 80UL * x / 2447UL;
  d = x - 2447UL * m / 80UL;
  x = m / 11UL;
  m = m + 2UL - 12UL * x;
  y = 100UL * (z - 49UL) + y + x;

  *yy = (int)y;
  *mm = (int)m;
  *dd = (int)d;
}

#if !defined(NEOCLOCK4X_FIRMWARE)
static int
neol_query_firmware(int fd,
		    int unit,
		    char *firmware,
		    size_t maxlen)
{
  char tmpbuf[256];
  size_t len;
  int lastsearch;
  unsigned char c;
  int last_c_was_crlf;
  int last_crlf_conv_len;
  int init;
  int read_errors;
  int flag = 0;
  int chars_read;

  /* wait a little bit */
  sleep(1);
  if(-1 != write(fd, "V", 1))
    {
      /* wait a little bit */
      sleep(1);
      memset(tmpbuf, 0x00, sizeof(tmpbuf));

      len = 0;
      lastsearch = 0;
      last_c_was_crlf = 0;
      last_crlf_conv_len = 0;
      init = 1;
      read_errors = 0;
      chars_read = 0;
      for(;;)
	{
	  if(read_errors > 5)
	    {
	      msyslog(LOG_ERR, "NeoClock4X(%d): can't read firmware version (timeout)", unit);
	      strlcpy(tmpbuf, "unknown due to timeout", sizeof(tmpbuf));
	      break;
	    }
          if(chars_read > 500)
            {
	      msyslog(LOG_ERR, "NeoClock4X(%d): can't read firmware version (garbage)", unit);
	      strlcpy(tmpbuf, "unknown due to garbage input", sizeof(tmpbuf));
	      break;
            }
	  if(-1 == read(fd, &c, 1))
	    {
              if(EAGAIN != errno)
                {
                  msyslog(LOG_DEBUG, "NeoClock4x(%d): read: %m", unit);
                  read_errors++;
                }
              else
                {
                  sleep(1);
                }
	      continue;
	    }
          else
            {
              chars_read++;
            }

	  if(init)
	    {
	      if(0xA9 != c) /* wait for (c) char in input stream */
		continue;

	      strlcpy(tmpbuf, "(c)", sizeof(tmpbuf));
	      len = 3;
	      init = 0;
	      continue;
	    }

#if 0
	  msyslog(LOG_NOTICE, "NeoClock4X(%d): firmware %c = %02Xh", unit, c, c);
#endif

	  if(0x0A == c || 0x0D == c)
	    {
	      if(last_c_was_crlf)
		{
		  char *ptr;
		  ptr = strstr(&tmpbuf[lastsearch], "S/N");
		  if(NULL != ptr)
		    {
		      tmpbuf[last_crlf_conv_len] = 0;
		      flag = 1;
		      break;
		    }
		  /* convert \n to / */
		  last_crlf_conv_len = len;
		  tmpbuf[len++] = ' ';
		  tmpbuf[len++] = '/';
		  tmpbuf[len++] = ' ';
		  lastsearch = len;
		}
	      last_c_was_crlf = 1;
	    }
	  else
	    {
	      last_c_was_crlf = 0;
	      if(0x00 != c)
		tmpbuf[len++] = (char) c;
	    }
	  tmpbuf[len] = '\0';
	  if (len > sizeof(tmpbuf)-5)
	    break;
	}
    }
  else
    {
      msyslog(LOG_ERR, "NeoClock4X(%d): can't query firmware version", unit);
      strlcpy(tmpbuf, "unknown error", sizeof(tmpbuf));
    }
  if (strlcpy(firmware, tmpbuf, maxlen) >= maxlen)
    strlcpy(firmware, "buffer too small", maxlen);

  if(flag)
    {
      NLOG(NLOG_CLOCKINFO)
	msyslog(LOG_INFO, "NeoClock4X(%d): firmware version: %s", unit, firmware);

      if(strstr(firmware, "/R2"))
	{
	  msyslog(LOG_INFO, "NeoClock4X(%d): Your NeoClock4X uses the new R2 firmware release. Please note the changed LED behaviour.", unit);
	}

    }

  return (flag);
}

static int
neol_check_firmware(int unit,
                    const char *firmware,
                    char *firmwaretag)
{
  char *ptr;

  *firmwaretag = '?';
  ptr = strstr(firmware, "NDF:");
  if(NULL != ptr)
    {
      if((strlen(firmware) - strlen(ptr)) >= 7)
        {
          if(':' == *(ptr+5) && '*' == *(ptr+6))
            *firmwaretag = *(ptr+4);
        }
    }

  if('A' != *firmwaretag)
    {
      msyslog(LOG_CRIT, "NeoClock4X(%d): firmware version \"%c\" not supported with this driver version!", unit, *firmwaretag);
      return (0);
    }

  return (1);
}
#endif

#else
int refclock_neoclock4x_bs;
#endif /* REFCLOCK */

/*
 * History:
 * refclock_neoclock4x.c
 *
 * 2002/04/27 cjh
 * Revision 1.0  first release
 *
 * 2002/07/15 cjh
 * preparing for bitkeeper reposity
 *
 * 2002/09/09 cjh
 * Revision 1.1
 * - don't assume sprintf returns an int anymore
 * - change the way the firmware version is read
 * - some customers would like to put a device called
 *   data diode to the NeoClock4X device to disable
 *   the write line. We need to now the firmware
 *   version even in this case. We made a compile time
 *   definition in this case. The code was previously
 *   only available on request.
 *
 * 2003/01/08 cjh
 * Revision 1.11
 * - changing xprinf to xnprinf to avoid buffer overflows
 * - change some logic
 * - fixed memory leaks if drivers can't initialize
 *
 * 2003/01/10 cjh
 * Revision 1.12
 * - replaced ldiv
 * - add code to support FreeBSD
 *
 * 2003/07/07 cjh
 * Revision 1.13
 * - fix reporting of clock status
 *   changes. previously a bad clock
 *   status was never reset.
 *
 * 2004/04/07 cjh
 * Revision 1.14
 * - open serial port in a way
 *   AIX and some other OS can
 *   handle much better
 *
 * 2006/01/11 cjh
 * Revision 1.15
 * - remove some unsued #ifdefs
 * - fix nsec calculation, closes #499
 *
 * 2009/12/04 cjh
 * Revision 1.16
 * - change license to ntp COPYRIGHT notice. This should allow Debian
 *   to add this refclock driver in further releases.
 * - detect R2 hardware
 *
 */






