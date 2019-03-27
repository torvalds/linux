/*
 * /src/NTP/REPOSITORY/ntp4-dev/include/parse.h,v 4.12 2007/01/14 08:36:03 kardel RELEASE_20070114_A
 *
 * parse.h,v 4.12 2007/01/14 08:36:03 kardel RELEASE_20070114_A
 *
 * Copyright (c) 1995-2015 by Frank Kardel <kardel <AT> ntp.org>
 * Copyright (c) 1989-1994 by Frank Kardel, Friedrich-Alexander Universitaet Erlangen-Nuernberg, Germany
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#ifndef __PARSE_H__
#define __PARSE_H__
#if	!(defined(lint) || defined(__GNUC__))
  static char parsehrcsid[]="parse.h,v 4.12 2007/01/14 08:36:03 kardel RELEASE_20070114_A";
#endif

#include "ntp_types.h"

#include "parse_conf.h"

/*
 * we use the following datastructures in two modes
 * either in the NTP itself where we use NTP time stamps at some places
 * or in the kernel, where only struct timeval will be used.
 */
#undef PARSEKERNEL
#if defined(KERNEL) || defined(_KERNEL)
#ifndef PARSESTREAM
#define PARSESTREAM
#endif
#endif
#if defined(PARSESTREAM) && defined(HAVE_SYS_STREAM_H)
#define PARSEKERNEL
#endif
#ifdef PARSEKERNEL
#ifndef _KERNEL
extern caddr_t kmem_alloc (unsigned int);
extern caddr_t kmem_free (caddr_t, unsigned int);
extern unsigned int splx (unsigned int);
extern unsigned int splhigh (void);
extern unsigned int splclock (void);
#define MALLOC(_X_) (char *)kmem_alloc(_X_)
#define FREE(_X_, _Y_) kmem_free((caddr_t)_X_, _Y_)
#else
#include <sys/kmem.h>
#define MALLOC(_X_) (char *)kmem_alloc(_X_, KM_SLEEP)
#define FREE(_X_, _Y_) kmem_free((caddr_t)_X_, _Y_)
#endif
#else
#define MALLOC(_X_) malloc(_X_)
#define FREE(_X_, _Y_) free(_X_)
#endif

#if defined(PARSESTREAM) && defined(HAVE_SYS_STREAM_H)
#include <sys/stream.h>
#include <sys/stropts.h>
#else	/* STREAM */
#include <stdio.h>
#include "ntp_syslog.h"
#ifdef	DEBUG
#define DD_PARSE 5
#define DD_RAWDCF 4
#define parseprintf(LEVEL, ARGS) if (debug > LEVEL) printf ARGS
#else	/* DEBUG */
#define parseprintf(LEVEL, ARGS)
#endif	/* DEBUG */
#endif	/* PARSESTREAM */

#if defined(timercmp) && defined(__GNUC__)
#undef timercmp
#endif

#if !defined(timercmp)
#define	timercmp(tvp, uvp, cmp)	\
	((tvp)->tv_sec cmp (uvp)->tv_sec || \
	 ((tvp)->tv_sec == (uvp)->tv_sec && (tvp)->tv_usec cmp (uvp)->tv_usec))
#endif

#ifndef TIMES10
#define TIMES10(_X_)	(((_X_) << 3) + ((_X_) << 1))
#endif

/*
 * some constants useful for GPS time conversion
 */
#define GPSORIGIN       2524953600UL         /* NTP origin - GPS origin in seconds */
#define GPSWEEKS        1024                 /* number of weeks until the GPS epch rolls over */

/*
 * state flags
 */
#define PARSEB_POWERUP            0x00000001 /* no synchronisation */
#define PARSEB_NOSYNC             0x00000002 /* timecode currently not confirmed */

/*
 * time zone information
 */
#define PARSEB_ANNOUNCE           0x00000010 /* switch time zone warning (DST switch) */
#define PARSEB_DST                0x00000020 /* DST in effect */
#define PARSEB_UTC		  0x00000040 /* UTC time */

/*
 * leap information
 */
#define PARSEB_LEAPDEL		  0x00000100 /* LEAP deletion warning */
#define PARSEB_LEAPADD		  0x00000200 /* LEAP addition warning */
#define PARSEB_LEAPS		  0x00000300 /* LEAP warnings */
#define PARSEB_LEAPSECOND	  0x00000400 /* actual leap second */
/*
 * optional status information
 */
#define PARSEB_CALLBIT		  0x00001000 /* "call bit" used to signalize irregularities in the control facilities */
#define PARSEB_POSITION		  0x00002000 /* position available */
#define PARSEB_MESSAGE            0x00004000 /* addtitional message data */
/*
 * feature information
 */
#define PARSEB_S_LEAP		  0x00010000 /* supports LEAP */
#define PARSEB_S_CALLBIT	  0x00020000 /* supports callbit information */
#define PARSEB_S_PPS     	  0x00040000 /* supports PPS time stamping */
#define PARSEB_S_POSITION	  0x00080000 /* supports position information (GPS) */

/*
 * time stamp availability
 */
#define PARSEB_TIMECODE		  0x10000000 /* valid time code sample */
#define PARSEB_PPS		  0x20000000 /* valid PPS sample */

#define PARSE_TCINFO		(PARSEB_ANNOUNCE|PARSEB_POWERUP|PARSEB_NOSYNC|PARSEB_DST|\
				 PARSEB_UTC|PARSEB_LEAPS|PARSEB_CALLBIT|PARSEB_S_LEAP|\
				 PARSEB_S_LOCATION|PARSEB_TIMECODE|PARSEB_MESSAGE)

#define PARSE_POWERUP(x)        ((x) & PARSEB_POWERUP)
#define PARSE_NOSYNC(x)         (((x) & (PARSEB_POWERUP|PARSEB_NOSYNC)) == PARSEB_NOSYNC)
#define PARSE_SYNC(x)           (((x) & (PARSEB_POWERUP|PARSEB_NOSYNC)) == 0)
#define PARSE_ANNOUNCE(x)       ((x) & PARSEB_ANNOUNCE)
#define PARSE_DST(x)            ((x) & PARSEB_DST)
#define PARSE_UTC(x)		((x) & PARSEB_UTC)
#define PARSE_LEAPADD(x)	(PARSE_SYNC(x) && (((x) & PARSEB_LEAPS) == PARSEB_LEAPADD))
#define PARSE_LEAPDEL(x)	(PARSE_SYNC(x) && (((x) & PARSEB_LEAPS) == PARSEB_LEAPDEL))
#define PARSE_CALLBIT(x)	((x) & PARSEB_CALLBIT)
#define PARSE_LEAPSECOND(x)	(PARSE_SYNC(x) && ((x) & PARSEB_LEAP_SECOND))

#define PARSE_S_LEAP(x)		((x) & PARSEB_S_LEAP)
#define PARSE_S_CALLBIT(x)	((x) & PARSEB_S_CALLBIT)
#define PARSE_S_PPS(x)		((x) & PARSEB_S_PPS)
#define PARSE_S_POSITION(x)	((x) & PARSEB_S_POSITION)

#define PARSE_TIMECODE(x)	((x) & PARSEB_TIMECODE)
#define PARSE_PPS(x)		((x) & PARSEB_PPS)
#define PARSE_POSITION(x)	((x) & PARSEB_POSITION)
#define PARSE_MESSAGE(x)	((x) & PARSEB_MESSAGE)

/*
 * operation flags - lower nibble contains fudge flags
 */
#define PARSE_TRUSTTIME     CLK_FLAG1  /* use flag1 to indicate the time2 references mean the trust time */
#define PARSE_CLEAR         CLK_FLAG2  /* use flag2 to control pps on assert */
#define PARSE_PPSKERNEL     CLK_FLAG3  /* use flag3 to bind PPS to kernel */
#define PARSE_LEAP_DELETE   CLK_FLAG4  /* use flag4 to force leap deletion - only necessary when earth slows down */

#define PARSE_FIXED_FMT     0x10  /* fixed format */
#define PARSE_PPSCLOCK      0x20  /* try to get PPS time stamp via ppsclock ioctl */

/*
 * size of buffers
 */
#define PARSE_TCMAX	    400	  /* maximum addition data size */

typedef union
{
  struct timeval tv;		/* timeval - kernel view */
  l_fp           fp;		/* fixed point - ntp view */
} timestamp_t;

/*
 * standard time stamp structure
 */
struct parsetime
{
  u_long  parse_status;	/* data status - CVT_OK, CVT_NONE, CVT_FAIL ... */
  timestamp_t	 parse_time;	/* PARSE timestamp */
  timestamp_t	 parse_stime;	/* telegram sample timestamp */
  timestamp_t	 parse_ptime;	/* PPS time stamp */
  long           parse_usecerror;	/* sampled usec error */
  u_long	 parse_state;	/* current receiver state */
  unsigned short parse_format;	/* format code */
  unsigned short parse_msglen;	/* length of message */
  unsigned char  parse_msg[PARSE_TCMAX]; /* original messages */
};

typedef struct parsetime parsetime_t;

/*---------- STREAMS interface ----------*/

#ifdef HAVE_SYS_STREAM_H
/*
 * ioctls
 */
#define PARSEIOC_ENABLE		(('D'<<8) + 'E')
#define PARSEIOC_DISABLE	(('D'<<8) + 'D')
#define PARSEIOC_SETFMT         (('D'<<8) + 'f')
#define PARSEIOC_GETFMT	        (('D'<<8) + 'F')
#define PARSEIOC_SETCS	        (('D'<<8) + 'C')
#define PARSEIOC_TIMECODE	(('D'<<8) + 'T')

#endif

/*------ IO handling flags (sorry) ------*/

#define PARSE_IO_CSIZE	0x00000003
#define PARSE_IO_CS5	0x00000000
#define PARSE_IO_CS6	0x00000001
#define PARSE_IO_CS7	0x00000002
#define PARSE_IO_CS8	0x00000003

/*
 * ioctl structure
 */
union parsectl
{
  struct parsegettc
    {
      u_long         parse_state;	/* last state */
      u_long         parse_badformat; /* number of bad packets since last query */
      unsigned short parse_format;/* last decoded format */
      unsigned short parse_count;	/* count of valid time code bytes */
      char           parse_buffer[PARSE_TCMAX+1]; /* timecode buffer */
    } parsegettc;

  struct parseformat
    {
      unsigned short parse_format;/* number of examined format */
      unsigned short parse_count;	/* count of valid string bytes */
      char           parse_buffer[PARSE_TCMAX+1]; /* format code string */
    } parseformat;

  struct parsesetcs
    {
      u_long         parse_cs;	/* character size (needed for stripping) */
    } parsesetcs;
};

typedef union parsectl parsectl_t;

/*------ for conversion routines --------*/

struct parse			/* parse module local data */
{
  int            parse_flags;	/* operation and current status flags */

  int		 parse_ioflags;	   /* io handling flags (5-8 Bit control currently) */

  /*
   * private data - fixed format only
   */
  unsigned short parse_plen;	/* length of private data */
  void          *parse_pdata;	/* private data pointer */

  /*
   * time code input buffer (from RS232 or PPS)
   */
  unsigned short parse_index;	/* current buffer index */
  char          *parse_data;    /* data buffer */
  unsigned short parse_dsize;	/* size of data buffer */
  unsigned short parse_lformat;	/* last format used */
  u_long         parse_lstate;	/* last state code */
  char          *parse_ldata;	/* last data buffer */
  unsigned short parse_ldsize;	/* last data buffer length */
  u_long         parse_badformat;	/* number of unparsable pakets */

  timestamp_t    parse_lastchar; /* last time a character was received */
  parsetime_t    parse_dtime;	/* external data prototype */
};

typedef struct parse parse_t;

struct clocktime		/* clock time broken up from time code */
{
  long day;
  long month;
  long year;
  long hour;
  long minute;
  long second;
  long usecond;
  long utcoffset;	/* in seconds */
  time_t utctime;	/* the actual time - alternative to date/time */
  u_long flags;		/* current clock status */
};

typedef struct clocktime clocktime_t;

/*
 * parser related return/error codes
 */
#define CVT_MASK	 (unsigned)0x0000000F /* conversion exit code */
#define   CVT_NONE	 (unsigned)0x00000001 /* format not applicable */
#define   CVT_FAIL	 (unsigned)0x00000002 /* conversion failed - error code returned */
#define   CVT_OK	 (unsigned)0x00000004 /* conversion succeeded */
#define   CVT_SKIP	 (unsigned)0x00000008 /* conversion succeeded */
#define CVT_ADDITIONAL   (unsigned)0x00000010 /* additional data is available */
#define CVT_BADFMT	 (unsigned)0x00000100 /* general format error - (unparsable) */
#define CVT_BADDATE      (unsigned)0x00000200 /* date field incorrect */
#define CVT_BADTIME	 (unsigned)0x00000400 /* time field incorrect */

/*
 * return codes used by special input parsers
 */
#define PARSE_INP_SKIP  0x00	/* discard data - may have been consumed */
#define PARSE_INP_TIME  0x01	/* time code assembled */
#define PARSE_INP_PARSE 0x02	/* parse data using normal algorithm */
#define PARSE_INP_DATA  0x04	/* additional data to pass up */
#define PARSE_INP_SYNTH 0x08	/* just pass up synthesized time */

/*
 * PPS edge info
 */
#define SYNC_ZERO	0x00
#define SYNC_ONE	0x01

typedef u_long parse_inp_fnc_t(parse_t *, char, timestamp_t *);
typedef u_long parse_cvt_fnc_t(unsigned char *, int, struct format *, clocktime_t *, void *);
typedef u_long parse_pps_fnc_t(parse_t *, int, timestamp_t *);

struct clockformat
{
  /* special input protocol - implies fixed format */
  parse_inp_fnc_t *input;
  /* conversion routine */
  parse_cvt_fnc_t *convert;
  /* routine for handling RS232 sync events (time stamps) */
  /* PPS input routine */
  parse_pps_fnc_t *syncpps;
  /* time code synthesizer */

  void           *data;		/* local parameters */
  const char     *name;		/* clock format name */
  unsigned short  length;	/* maximum length of data packet */
  unsigned short  plen;		/* length of private data - implies fixed format */
};

typedef struct clockformat clockformat_t;

/*
 * parse interface
 */
extern int  parse_ioinit (parse_t *);
extern void parse_ioend (parse_t *);
extern int  parse_ioread (parse_t *, char, timestamp_t *);
extern int  parse_iopps (parse_t *, int, timestamp_t *);
extern void parse_iodone (parse_t *);
extern int  parse_timecode (parsectl_t *, parse_t *);
extern int  parse_getfmt (parsectl_t *, parse_t *);
extern int  parse_setfmt (parsectl_t *, parse_t *);
extern int  parse_setcs (parsectl_t *, parse_t *);

extern unsigned int parse_restart (parse_t *, char);
extern unsigned int parse_addchar (parse_t *, char);
extern unsigned int parse_end (parse_t *);

extern int Strok (const unsigned char *, const unsigned char *);
extern int Stoi (const unsigned char *, long *, int);

extern time_t parse_to_unixtime (clocktime_t *, u_long *);
extern u_long updatetimeinfo (parse_t *, u_long);
extern void syn_simple (parse_t *, timestamp_t *, struct format *, u_long);
extern parse_pps_fnc_t pps_simple;
extern parse_pps_fnc_t pps_one;
extern parse_pps_fnc_t pps_zero;
extern int parse_timedout (parse_t *, timestamp_t *, struct timeval *);

#endif

/*
 * History:
 *
 * parse.h,v
 * Revision 4.12  2007/01/14 08:36:03  kardel
 * make timestamp union anonymous to avoid conflicts with
 * some OSes that choose to create a nameing conflic here.
 *
 * Revision 4.11  2005/06/25 10:58:45  kardel
 * add missing log keywords
 *
 * Revision 4.5  1998/08/09 22:23:32  kardel
 * 4.0.73e2 adjustments
 *
 * Revision 4.4  1998/06/14 21:09:27  kardel
 * Sun acc cleanup
 *
 * Revision 4.3  1998/06/13 11:49:25  kardel
 * STREAM macro gone in favor of HAVE_SYS_STREAM_H
 *
 * Revision 4.2  1998/06/12 15:14:25  kardel
 * fixed prototypes
 *
 * Revision 4.1  1998/05/24 10:07:59  kardel
 * removed old data structure cruft (new input model)
 * new PARSE_INP* macros for input handling
 * removed old SYNC_* macros from old input model
 * (struct clockformat): removed old parse functions in favor of the
 * new input model
 * updated prototypes
 *
 * form V3 3.31 - log info deleted 1998/04/11 kardel
 */
