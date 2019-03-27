/*
 * /src/NTP/REPOSITORY/ntp4-dev/libparse/clk_meinberg.c,v 4.12.2.1 2005/09/25 10:22:35 kardel RELEASE_20050925_A
 *
 * clk_meinberg.c,v 4.12.2.1 2005/09/25 10:22:35 kardel RELEASE_20050925_A
 *
 * Meinberg clock support
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#if defined(REFCLOCK) && defined(CLOCK_PARSE) && defined(CLOCK_MEINBERG)

#include "ntp_fp.h"
#include "ntp_unixtime.h"
#include "ntp_calendar.h"

#include "ntp_machine.h"

#include "parse.h"

#ifndef PARSESTREAM
#include <stdio.h>
#else
#include "sys/parsestreams.h"
#endif

#include "ntp_stdlib.h"

#include "ntp_stdlib.h"

#include "mbg_gps166.h"
#include "binio.h"
#include "ascii.h"

/*
 * The Meinberg receiver every second sends a datagram of the following form
 * (Standard Format)
 *
 *     <STX>D:<dd>.<mm>.<yy>;T:<w>;U:<hh>:<mm>:<ss>;<S><F><D><A><ETX>
 * pos:  0  00 00 0 00 0 11 111 1 111 12 2 22 2 22 2 2  2  3  3   3
 *       1  23 45 6 78 9 01 234 5 678 90 1 23 4 56 7 8  9  0  1   2
 * <STX>           = '\002' ASCII start of text
 * <ETX>           = '\003' ASCII end of text
 * <dd>,<mm>,<yy>  = day, month, year(2 digits!!)
 * <w>             = day of week (sunday= 0)
 * <hh>,<mm>,<ss>  = hour, minute, second
 * <S>             = '#' if never synced since powerup for DCF C51
 *                 = '#' if not PZF sychronisation available for PZF 535/509
 *                 = ' ' if ok
 * <F>             = '*' if time comes from internal quartz
 *                 = ' ' if completely synched
 * <D>             = 'S' if daylight saving time is active
 *                 = 'U' if time is represented in UTC
 *                 = ' ' if no special condition exists
 * <A>             = '!' during the hour preceeding an daylight saving time
 *                       start/end change
 *                 = 'A' leap second insert warning
 *                 = ' ' if no special condition exists
 *
 * Extended data format (PZFUERL for PZF type clocks)
 *
 *     <STX><dd>.<mm>.<yy>; <w>; <hh>:<mm>:<ss>; <U><S><F><D><A><L><R><ETX>
 * pos:  0   00 0 00 0 00 11 1 11 11 1 11 2 22 22 2  2  2  2  2  3  3   3
 *       1   23 4 56 7 89 01 2 34 56 7 89 0 12 34 5  6  7  8  9  0  1   2
 * <STX>           = '\002' ASCII start of text
 * <ETX>           = '\003' ASCII end of text
 * <dd>,<mm>,<yy>  = day, month, year(2 digits!!)
 * <w>             = day of week (sunday= 0)
 * <hh>,<mm>,<ss>  = hour, minute, second
 * <U>             = 'U' UTC time display
 * <S>             = '#' if never synced since powerup else ' ' for DCF C51
 *                   '#' if not PZF sychronisation available else ' ' for PZF 535/509
 * <F>             = '*' if time comes from internal quartz else ' '
 * <D>             = 'S' if daylight saving time is active else ' '
 * <A>             = '!' during the hour preceeding an daylight saving time
 *                       start/end change
 * <L>             = 'A' LEAP second announcement
 * <R>             = 'R' "call bit" used to signalize irregularities in the control facilities,
 *                   usually ' ', until 2003 indicated transmission via alternate antenna
 *
 * Meinberg GPS receivers
 *
 * For very old devices you must get the Uni-Erlangen firmware for the GPS receiver support
 * to work to full satisfaction !
 * With newer GPS receiver types the Uni Erlangen string format can be configured at the device.
 *
 *     <STX><dd>.<mm>.<yy>; <w>; <hh>:<mm>:<ss>; <+/-><00:00>; <U><S><F><D><A><L><R><L>; <position...><ETX>
 *
 *        000000000111111111122222222223333333333444444444455555555556666666
 *        123456789012345678901234567890123456789012345678901234567890123456
 *     \x0209.07.93; 5; 08:48:26; +00:00; #*S!A L; 49.5736N  11.0280E  373m\x03
 *
 *
 * <STX>           = '\002' ASCII start of text
 * <ETX>           = '\003' ASCII end of text
 * <dd>,<mm>,<yy>  = day, month, year(2 digits!!)
 * <w>             = day of week (sunday= 0)
 * <hh>,<mm>,<ss>  = hour, minute, second
 * <+/->,<00:00>   = offset to UTC
 * <S>             = '#' if never synced since powerup else ' '
 * <F>             = '*' if position is not confirmed else ' '
 * <D>             = 'S' if daylight saving time is active else ' '
 * <A>             = '!' during the hour preceeding an daylight saving time
 *                       start/end change
 * <L>             = 'A' LEAP second announcement
 * <R>             = 'R' "call bit" used to signalize irregularities in the control facilities,
 *                   usually ' ', until 2003 indicated transmission via alternate antenna
 *                   (reminiscent of PZF receivers)
 * <L>             = 'L' on 23:59:60
 *
 * Binary messages have a lead in for a fixed header of SOH
 */

/*--------------------------------------------------------------*/
/* Name:         csum()                                         */
/*                                                              */
/* Purpose:      Compute a checksum about a number of bytes     */
/*                                                              */
/* Input:        uchar *p    address of the first byte          */
/*               short n     the number of bytes                */
/*                                                              */
/* Output:       --                                             */
/*                                                              */
/* Ret val:      the checksum                                   */
/*+-------------------------------------------------------------*/

CSUM
mbg_csum(
	 unsigned char *p,
	 unsigned int n
	 )
{
  unsigned int sum = 0;
  unsigned int i;

  for ( i = 0; i < n; i++ )
    sum += *p++;

  return (CSUM) sum;

}  /* csum */

void
get_mbg_header(
	       unsigned char **bufpp,
	       GPS_MSG_HDR *headerp
	       )
{
  headerp->cmd = (GPS_CMD) get_lsb_short(bufpp);
  headerp->len = get_lsb_uint16(bufpp);
  headerp->data_csum = (CSUM) get_lsb_short(bufpp);
  headerp->hdr_csum  = (CSUM) get_lsb_short(bufpp);
}

static struct format meinberg_fmt[] =
{
	{
		{
			{ 3, 2},  {  6, 2}, {  9, 2},
			{ 18, 2}, { 21, 2}, { 24, 2},
			{ 14, 1}, { 27, 4}, { 29, 1},
		},
		(const unsigned char *)"\2D:  .  .  ;T: ;U:  .  .  ;    \3",
		0
	},
	{			/* special extended FAU Erlangen extended format */
		{
			{ 1, 2},  { 4,  2}, {  7, 2},
			{ 14, 2}, { 17, 2}, { 20, 2},
			{ 11, 1}, { 25, 4}, { 27, 1},
		},
		(const unsigned char *)"\2  .  .  ;  ;   :  :  ;        \3",
		MBG_EXTENDED
	},
	{			/* special extended FAU Erlangen GPS format */
		{
			{ 1,  2}, {  4, 2}, {  7, 2},
			{ 14, 2}, { 17, 2}, { 20, 2},
			{ 11, 1}, { 32, 7}, { 35, 1},
			{ 25, 2}, { 28, 2}, { 24, 1}
		},
		(const unsigned char *)"\2  .  .  ;  ;   :  :  ;    :  ;        ;   .         .       ",
		0
	}
};

static parse_cvt_fnc_t cvt_meinberg;
static parse_cvt_fnc_t cvt_mgps;
static parse_inp_fnc_t mbg_input;
static parse_inp_fnc_t gps_input;

struct msg_buf
{
  unsigned short len;		/* len to fill */
  unsigned short phase;		/* current input phase */
};

#define MBG_NONE	0	/* no data input */
#define MBG_HEADER	1	/* receiving header */
#define MBG_DATA	2	/* receiving data */
#define MBG_STRING      3	/* receiving standard data message */

clockformat_t clock_meinberg[] =
{
	{
		mbg_input,	/* normal input handling */
		cvt_meinberg,	/* Meinberg conversion */
		pps_one,	/* easy PPS monitoring */
		0,		/* conversion configuration */
		"Meinberg Standard", /* Meinberg simple format - beware */
		32,				/* string buffer */
		0		/* no private data (complete packets) */
	},
	{
		mbg_input,	/* normal input handling */
		cvt_meinberg,	/* Meinberg conversion */
		pps_one,	/* easy PPS monitoring */
		0,		/* conversion configuration */
		"Meinberg Extended", /* Meinberg enhanced format */
		32,		/* string buffer */
		0		/* no private data (complete packets) */
	},
	{
		gps_input,	/* no input handling */
		cvt_mgps,	/* Meinberg GPS receiver conversion */
		pps_one,	/* easy PPS monitoring */
		(void *)&meinberg_fmt[2], /* conversion configuration */
		"Meinberg GPS Extended",  /* Meinberg FAU GPS format */
		512,		/* string buffer */
		sizeof(struct msg_buf)	/* no private data (complete packets) */
	}
};

/*
 * parse_cvt_fnc_t cvt_meinberg
 *
 * convert simple type format
 */
static u_long
cvt_meinberg(
	     unsigned char *buffer,
	     int            size,
	     struct format *unused,
	     clocktime_t   *clock_time,
	     void          *local
	     )
{
	struct format *format;

	/*
	 * select automagically correct data format
	 */
	if (Strok(buffer, meinberg_fmt[0].fixed_string))
	{
		format = &meinberg_fmt[0];
	}
	else
	{
		if (Strok(buffer, meinberg_fmt[1].fixed_string))
		{
			format = &meinberg_fmt[1];
		}
		else
		{
			return CVT_FAIL|CVT_BADFMT;
		}
	}

	/*
	 * collect data
	 */
	if (Stoi(&buffer[format->field_offsets[O_DAY].offset], &clock_time->day,
		 format->field_offsets[O_DAY].length) ||
	    Stoi(&buffer[format->field_offsets[O_MONTH].offset], &clock_time->month,
		 format->field_offsets[O_MONTH].length) ||
	    Stoi(&buffer[format->field_offsets[O_YEAR].offset], &clock_time->year,
		 format->field_offsets[O_YEAR].length) ||
	    Stoi(&buffer[format->field_offsets[O_HOUR].offset], &clock_time->hour,
		 format->field_offsets[O_HOUR].length) ||
	    Stoi(&buffer[format->field_offsets[O_MIN].offset], &clock_time->minute,
		 format->field_offsets[O_MIN].length) ||
	    Stoi(&buffer[format->field_offsets[O_SEC].offset], &clock_time->second,
		 format->field_offsets[O_SEC].length))
	{
		return CVT_FAIL|CVT_BADFMT;
	}
	else
	{
		unsigned char *f = &buffer[format->field_offsets[O_FLAGS].offset];

		clock_time->usecond = 0;
		clock_time->flags   = PARSEB_S_LEAP;

		if (clock_time->second == 60)
			clock_time->flags |= PARSEB_LEAPSECOND;

		/*
		 * in the extended timecode format we have also the
		 * indication that the timecode is in UTC
		 * for compatibilty reasons we start at the USUAL
		 * offset (POWERUP flag) and know that the UTC indication
		 * is the character before the powerup flag
		 */
		if ((format->flags & MBG_EXTENDED) && (f[-1] == 'U'))
		{
			/*
			 * timecode is in UTC
			 */
			clock_time->utcoffset = 0; /* UTC */
			clock_time->flags    |= PARSEB_UTC;
		}
		else
		{
			/*
			 * only calculate UTC offset if MET/MED is in time code
			 * or we have the old time code format, where we do not
			 * know whether it is UTC time or MET/MED
			 * pray that nobody switches to UTC in the *old* standard time code
			 * ROMS !!!! The new ROMS have 'U' at the ZONE field - good.
			 */
			switch (buffer[format->field_offsets[O_ZONE].offset])
			{
			case ' ':
				clock_time->utcoffset = -1*60*60; /* MET */
				break;

			case 'S':
				clock_time->utcoffset = -2*60*60; /* MED */
				break;

			case 'U':
				/*
				 * timecode is in UTC
				 */
				clock_time->utcoffset = 0;        /* UTC */
				clock_time->flags    |= PARSEB_UTC;
				break;

			default:
				return CVT_FAIL|CVT_BADFMT;
			}
		}

		/*
		 * gather status flags
		 */
		if (buffer[format->field_offsets[O_ZONE].offset] == 'S')
			clock_time->flags    |= PARSEB_DST;

		if (f[0] == '#')
			clock_time->flags |= PARSEB_POWERUP;

		if (f[1] == '*')
			clock_time->flags |= PARSEB_NOSYNC;

		if (f[3] == '!')
			clock_time->flags |= PARSEB_ANNOUNCE;

		/*
		 * oncoming leap second
		 * 'a' code not confirmed - earth is not
		 * expected to speed up
		 */
		if (f[3] == 'A')
			clock_time->flags |= PARSEB_LEAPADD;

		if (f[3] == 'a')
			clock_time->flags |= PARSEB_LEAPDEL;


		if (format->flags & MBG_EXTENDED)
		{
			clock_time->flags |= PARSEB_S_CALLBIT;

			/*
			 * DCF77 does not encode the direction -
			 * so we take the current default -
			 * earth slowing down
			 */
			clock_time->flags &= ~PARSEB_LEAPDEL;

			if (f[4] == 'A')
				clock_time->flags |= PARSEB_LEAPADD;

			if (f[5] == 'R')
				clock_time->flags |= PARSEB_CALLBIT;
		}
		return CVT_OK;
	}
}


/*
 * parse_inp_fnc_t mbg_input
 *
 * grab data from input stream
 */
static u_long
mbg_input(
	  parse_t      *parseio,
	  char         ch,
	  timestamp_t  *tstamp
	  )
{
	unsigned int rtc;

	parseprintf(DD_PARSE, ("mbg_input(0x%p, 0x%x, ...)\n", (void*)parseio, ch));

	switch (ch)
	{
	case STX:
		parseprintf(DD_PARSE, ("mbg_input: STX seen\n"));

		parseio->parse_index = 1;
		parseio->parse_data[0] = ch;
		parseio->parse_dtime.parse_stime = *tstamp; /* collect timestamp */
		return PARSE_INP_SKIP;

	case ETX:
		parseprintf(DD_PARSE, ("mbg_input: ETX seen\n"));
		if ((rtc = parse_addchar(parseio, ch)) == PARSE_INP_SKIP)
			return parse_end(parseio);
		else
			return rtc;

	default:
		return parse_addchar(parseio, ch);
	}
}


/*
 * parse_cvt_fnc_t cvt_mgps
 *
 * convert Meinberg GPS format
 */
static u_long
cvt_mgps(
	 unsigned char *buffer,
	 int            size,
	 struct format *format,
	 clocktime_t   *clock_time,
	 void          *local
	)
{
	if (!Strok(buffer, format->fixed_string))
	{
		return cvt_meinberg(buffer, size, format, clock_time, local);
	}
	else
	{
		if (Stoi(&buffer[format->field_offsets[O_DAY].offset], &clock_time->day,
			 format->field_offsets[O_DAY].length) ||
		    Stoi(&buffer[format->field_offsets[O_MONTH].offset], &clock_time->month,
			 format->field_offsets[O_MONTH].length) ||
		    Stoi(&buffer[format->field_offsets[O_YEAR].offset], &clock_time->year,
			 format->field_offsets[O_YEAR].length) ||
		    Stoi(&buffer[format->field_offsets[O_HOUR].offset], &clock_time->hour,
			 format->field_offsets[O_HOUR].length) ||
		    Stoi(&buffer[format->field_offsets[O_MIN].offset], &clock_time->minute,
			 format->field_offsets[O_MIN].length) ||
		    Stoi(&buffer[format->field_offsets[O_SEC].offset], &clock_time->second,
			 format->field_offsets[O_SEC].length))
		{
			return CVT_FAIL|CVT_BADFMT;
		}
		else
		{
			long h;
			unsigned char *f = &buffer[format->field_offsets[O_FLAGS].offset];

			clock_time->flags = PARSEB_S_LEAP|PARSEB_S_POSITION;

			clock_time->usecond = 0;

			/*
			 * calculate UTC offset
			 */
			if (Stoi(&buffer[format->field_offsets[O_UTCHOFFSET].offset], &h,
				 format->field_offsets[O_UTCHOFFSET].length))
			{
				return CVT_FAIL|CVT_BADFMT;
			}
			else
			{
				if (Stoi(&buffer[format->field_offsets[O_UTCMOFFSET].offset], &clock_time->utcoffset,
					 format->field_offsets[O_UTCMOFFSET].length))
				{
					return CVT_FAIL|CVT_BADFMT;
				}

				clock_time->utcoffset += TIMES60(h);
				clock_time->utcoffset  = TIMES60(clock_time->utcoffset);

				if (buffer[format->field_offsets[O_UTCSOFFSET].offset] != '-')
				{
					clock_time->utcoffset = -clock_time->utcoffset;
				}
			}

			/*
			 * gather status flags
			 */
			if (buffer[format->field_offsets[O_ZONE].offset] == 'S')
			    clock_time->flags    |= PARSEB_DST;

			if (clock_time->utcoffset == 0)
			    clock_time->flags |= PARSEB_UTC;

			/*
			 * no sv's seen - no time & position
			 */
			if (f[0] == '#')
			    clock_time->flags |= PARSEB_POWERUP;

			/*
			 * at least one sv seen - time (for last position)
			 */
			if (f[1] == '*')
			    clock_time->flags |= PARSEB_NOSYNC;
			else
			    if (!(clock_time->flags & PARSEB_POWERUP))
				clock_time->flags |= PARSEB_POSITION;

			/*
			 * oncoming zone switch
			 */
			if (f[3] == '!')
			    clock_time->flags |= PARSEB_ANNOUNCE;

			/*
			 * oncoming leap second
			 * 'a' code not confirmed - earth is not
			 * expected to speed up
			 */
			if (f[4] == 'A')
			    clock_time->flags |= PARSEB_LEAPADD;

			if (f[4] == 'a')
			    clock_time->flags |= PARSEB_LEAPDEL;

			/*
			 * f[5] == ' '
			 */

			/*
			 * this is the leap second
			 */
			if ((f[6] == 'L') || (clock_time->second == 60))
			    clock_time->flags |= PARSEB_LEAPSECOND;

			return CVT_OK;
		}
	}
}

/*
 * parse_inp_fnc_t gps_input
 *
 * grep binary data from input stream
 */
static u_long
gps_input(
	  parse_t      *parseio,
	  char ch,
	  timestamp_t  *tstamp
	  )
{
  CSUM calc_csum;                    /* used to compare the incoming csums */
  GPS_MSG_HDR header;
  struct msg_buf *msg_buf;

  msg_buf = (struct msg_buf *)parseio->parse_pdata;

  parseprintf(DD_PARSE, ("gps_input(0x%p, 0x%x, ...)\n", (void*)parseio, ch));

  if (!msg_buf)
    return PARSE_INP_SKIP;

  if ( msg_buf->phase == MBG_NONE )
    {                  /* not receiving yet */
      switch (ch)
	{
	case SOH:
	  parseprintf(DD_PARSE, ("gps_input: SOH seen\n"));

	  msg_buf->len = sizeof( header ); /* prepare to receive msg header */
	  msg_buf->phase = MBG_HEADER; /* receiving header */
	  break;

	case STX:
	  parseprintf(DD_PARSE, ("gps_input: STX seen\n"));

	  msg_buf->len = 0;
	  msg_buf->phase = MBG_STRING; /* prepare to receive ASCII ETX delimited message */
	  parseio->parse_index = 1;
	  parseio->parse_data[0] = ch;
	  break;

	default:
	  return PARSE_INP_SKIP;	/* keep searching */
	}

      parseio->parse_dtime.parse_msglen = 1; /* reset buffer pointer */
      parseio->parse_dtime.parse_msg[0] = ch; /* fill in first character */
      parseio->parse_dtime.parse_stime  = *tstamp; /* collect timestamp */
      return PARSE_INP_SKIP;
    }

  /* SOH/STX has already been received */

  /* save incoming character in both buffers if needbe */
  if ((msg_buf->phase == MBG_STRING) &&
      (parseio->parse_index < parseio->parse_dsize))
    parseio->parse_data[parseio->parse_index++] = ch;

  parseio->parse_dtime.parse_msg[parseio->parse_dtime.parse_msglen++] = ch;

  if (parseio->parse_dtime.parse_msglen > sizeof(parseio->parse_dtime.parse_msg))
    {
      msg_buf->phase = MBG_NONE; /* buffer overflow - discard */
      parseio->parse_data[parseio->parse_index] = '\0';
      memcpy(parseio->parse_ldata, parseio->parse_data, (unsigned)(parseio->parse_index+1));
      parseio->parse_ldsize = parseio->parse_index;
      return PARSE_INP_DATA;
    }

  switch (msg_buf->phase)
    {
    case MBG_HEADER:
    case MBG_DATA:
      msg_buf->len--;

      if ( msg_buf->len )               /* transfer not complete */
	return PARSE_INP_SKIP;

      parseprintf(DD_PARSE, ("gps_input: %s complete\n", (msg_buf->phase == MBG_DATA) ? "data" : "header"));

      break;

    case MBG_STRING:
      if ((ch == ETX) || (parseio->parse_index >= parseio->parse_dsize))
	{
	  msg_buf->phase = MBG_NONE;
	  parseprintf(DD_PARSE, ("gps_input: string complete\n"));
	  parseio->parse_data[parseio->parse_index] = '\0';
	  memcpy(parseio->parse_ldata, parseio->parse_data, (unsigned)(parseio->parse_index+1));
	  parseio->parse_ldsize = parseio->parse_index;
	  parseio->parse_index = 0;
	  return PARSE_INP_TIME;
	}
      else
	{
	  return PARSE_INP_SKIP;
	}
    }

  /* cnt == 0, so the header or the whole message is complete */

  if ( msg_buf->phase == MBG_HEADER )
    {         /* header complete now */
      unsigned char *datap = parseio->parse_dtime.parse_msg + 1;

      get_mbg_header(&datap, &header);

      parseprintf(DD_PARSE, ("gps_input: header: cmd 0x%x, len %d, dcsum 0x%x, hcsum 0x%x\n",
			     (int)header.cmd, (int)header.len, (int)header.data_csum,
			     (int)header.hdr_csum));


      calc_csum = mbg_csum( (unsigned char *) parseio->parse_dtime.parse_msg + 1, (unsigned short)6 );

      if ( calc_csum != header.hdr_csum )
	{
	  parseprintf(DD_PARSE, ("gps_input: header checksum mismatch expected 0x%x, got 0x%x\n",
				 (int)calc_csum, (int)mbg_csum( (unsigned char *) parseio->parse_dtime.parse_msg, (unsigned short)6 )));

	  msg_buf->phase = MBG_NONE;  /* back to hunting mode */
	  return PARSE_INP_DATA;      /* invalid header checksum received - pass up for detection */
	}

      if ((header.len == 0)  ||       /* no data to wait for */
	  (header.len >= (sizeof (parseio->parse_dtime.parse_msg) - sizeof(header) - 1)))	/* blows anything we have space for */
	{
	  msg_buf->phase = MBG_NONE;  /* back to hunting mode */
	  return (header.len == 0) ? PARSE_INP_DATA : PARSE_INP_SKIP; /* message complete/throwaway */
	}

      parseprintf(DD_PARSE, ("gps_input: expecting %d bytes of data message\n", (int)header.len));

      msg_buf->len   = header.len;/* save number of bytes to wait for */
      msg_buf->phase = MBG_DATA;      /* flag header already complete */
      return PARSE_INP_SKIP;
    }

  parseprintf(DD_PARSE, ("gps_input: message data complete\n"));

  /* Header and data have been received. The header checksum has been */
  /* checked */

  msg_buf->phase = MBG_NONE;	      /* back to hunting mode */
  return PARSE_INP_DATA;              /* message complete, must be evaluated */
}

#else /* not (REFCLOCK && CLOCK_PARSE && CLOCK_MEINBERG) */
int clk_meinberg_bs;
#endif /* not (REFCLOCK && CLOCK_PARSE && CLOCK_MEINBERG) */

/*
 * History:
 *
 * clk_meinberg.c,v
 * Revision 4.12.2.1  2005/09/25 10:22:35  kardel
 * cleanup buffer bounds
 *
 * Revision 4.12  2005/04/16 17:32:10  kardel
 * update copyright
 *
 * Revision 4.11  2004/11/14 15:29:41  kardel
 * support PPSAPI, upgrade Copyright to Berkeley style
 *
 * Revision 4.8  1999/11/28 09:13:50  kardel
 * RECON_4_0_98F
 *
 * Revision 4.7  1999/02/21 11:09:14  kardel
 * cleanup
 *
 * Revision 4.6  1998/06/14 21:09:36  kardel
 * Sun acc cleanup
 *
 * Revision 4.5  1998/06/13 15:18:54  kardel
 * fix mem*() to b*() function macro emulation
 *
 * Revision 4.4  1998/06/13 12:03:23  kardel
 * fix SYSV clock name clash
 *
 * Revision 4.3  1998/06/12 15:22:28  kardel
 * fix prototypes
 *
 * Revision 4.2  1998/05/24 16:14:42  kardel
 * support current Meinberg standard data formats
 *
 * Revision 4.1  1998/05/24 09:39:52  kardel
 * implementation of the new IO handling model
 *
 * Revision 4.0  1998/04/10 19:45:29  kardel
 * Start 4.0 release version numbering
 *
 * from V3 3.23 - log info deleted 1998/04/11 kardel
 *
 */
