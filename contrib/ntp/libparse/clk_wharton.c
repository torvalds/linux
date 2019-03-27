/*
 * /src/NTP/ntp4-dev/libparse/clk_wharton.c,v 4.2 2004/11/14 15:29:41 kardel RELEASE_20050508_A
 *  
 * clk_wharton.c,v 4.2 2004/11/14 15:29:41 kardel RELEASE_20050508_A
 *
 * From Philippe De Muyter <phdm@macqel.be>, 1999
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#if defined(REFCLOCK) && defined(CLOCK_PARSE) && defined(CLOCK_WHARTON_400A)
/*
 * Support for WHARTON 400A Series clock + 404.2 serial interface.
 *
 * Copyright (C) 1999, 2000 by Philippe De Muyter <phdm@macqel.be>
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 * 
 */

#include "ntp_fp.h"
#include "ascii.h"
#include "parse.h"

#ifndef PARSESTREAM
#include "ntp_stdlib.h"
#include <stdio.h>
#else
#include "sys/parsestreams.h"
extern void printf (const char *, ...);
#endif

/*
 * In private e-mail alastair@wharton.co.uk said :
 * "If you are going to use the 400A and 404.2 system [for ntp] I recommend
 * that you set the 400A to output the message every second.  The start of
 * transmission of the first byte of the message is synchronised to the
 * second edge."
 * The WHARTON 400A Series is able to send date/time serial messages
 * in 7 output formats.  We use format 1 here because it is the shortest.
 * For use with this driver, the WHARTON 400A Series clock must be set-up
 * as follows :
 *					Programmable	Selected
 *					Option No	Option
 *	BST or CET display		3		9 or 11
 *	No external controller		7		0
 *	Serial Output Format 1		9		1
 *	Baud rate 9600 bps		10		96
 *	Bit length 8 bits		11		8
 *	Parity even			12		E
 *
 * WHARTON 400A Series output format 1 is as follows :
 * 
 * Timestamp	STXssmmhhDDMMYYSETX
 * Pos		0  12345678901234
 *		0  00000000011111
 *
 *	STX	start transmission (ASCII 0x02)
 *	ETX	end transmission (ASCII 0x03)
 *	ss	Second expressed in reversed decimal (units then tens)
 *	mm	Minute expressed in reversed decimal
 *	hh	Hour expressed in reversed decimal
 *	DD	Day of month expressed in reversed decimal
 *	MM	Month expressed in reversed decimal (January is 1)
 *	YY	Year (without century) expressed in reversed decimal
 *	S	Status byte : 0x30 +
 *			bit 0	0 = MSF source		1 = DCF source
 *			bit 1	0 = Winter time		1 = Summer time
 *			bit 2	0 = not synchronised	1 = synchronised
 *			bit 3	0 = no early warning	1 = early warning
 * 
 */

static parse_cvt_fnc_t cvt_wharton_400a;
static parse_inp_fnc_t inp_wharton_400a;

/*
 * parse_cvt_fnc_t cvt_wharton_400a
 * 
 * convert simple type format
 */
static          u_long
cvt_wharton_400a(
	unsigned char *buffer,
	int            size,
	struct format *format,
	clocktime_t   *clock_time,
	void          *local
	)
{
	int	i;

	/* The given `size' includes a terminating null-character. */
	if (size != 15 || buffer[0] != STX || buffer[14] != ETX
	    || buffer[13] < '0' || buffer[13] > ('0' + 0xf))
		return CVT_NONE;
	for (i = 1; i < 13; i += 1)
		if (buffer[i] < '0' || buffer[i] > '9')
			return CVT_NONE;
	clock_time->second = (buffer[2] - '0') * 10 + buffer[1] - '0';
	clock_time->minute = (buffer[4] - '0') * 10 + buffer[3] - '0';
	clock_time->hour   = (buffer[6] - '0') * 10 + buffer[5] - '0';
	clock_time->day    = (buffer[8] - '0') * 10 + buffer[7] - '0';
	clock_time->month  = (buffer[10] - '0') * 10 + buffer[9] - '0';
	clock_time->year   = (buffer[12] - '0') * 10 + buffer[11] - '0';
	clock_time->usecond = 0;
	if (buffer[13] & 0x1) /* We have CET time */
		clock_time->utcoffset = -1*60*60;
	else		/* We have BST time */
		clock_time->utcoffset = 0;
	if (buffer[13] & 0x2) {
		clock_time->flags |= PARSEB_DST;
		clock_time->utcoffset += -1*60*60;
	}
	if (!(buffer[13] & 0x4))
		clock_time->flags |= PARSEB_NOSYNC;
	if (buffer[13] & 0x8)
		clock_time->flags |= PARSEB_ANNOUNCE;

	return CVT_OK;
}

/*
 * parse_inp_fnc_t inp_wharton_400a
 *
 * grab data from input stream
 */
static u_long
inp_wharton_400a(
	      parse_t      *parseio,
	      char         ch,
	      timestamp_t  *tstamp
	      )
{
	unsigned int rtc;
	
	parseprintf(DD_PARSE, ("inp_wharton_400a(0x%p, 0x%x, ...)\n", (void*)parseio, ch));
	
	switch (ch)
	{
	case STX:
		parseprintf(DD_PARSE, ("inp_wharton_400a: STX seen\n"));
		
		parseio->parse_index = 1;
		parseio->parse_data[0] = ch;
		parseio->parse_dtime.parse_stime = *tstamp; /* collect timestamp */
		return PARSE_INP_SKIP;
	  
	case ETX:
		parseprintf(DD_PARSE, ("inp_wharton_400a: ETX seen\n"));
		if ((rtc = parse_addchar(parseio, ch)) == PARSE_INP_SKIP)
			return parse_end(parseio);
		else
			return rtc;

	default:
		return parse_addchar(parseio, ch);
	}
}

clockformat_t   clock_wharton_400a =
{
	inp_wharton_400a,	/* input handling function */
	cvt_wharton_400a,	/* conversion function */
	0,			/* no PPS monitoring */
	0,			/* conversion configuration */
	"WHARTON 400A Series clock Output Format 1",	/* String format name */
	15,			/* string buffer */
	0			/* no private data (complete packets) */
};

#else /* not (REFCLOCK && CLOCK_PARSE && CLOCK_WHARTON_400A) */
int clk_wharton_400a_bs;
#endif /* not (REFCLOCK && CLOCK_PARSE && CLOCK_WHARTON_400A) */

/*
 * clk_wharton.c,v
 * Revision 4.1  1999/02/28 15:27:24  kardel
 * wharton clock integration
 *
 */
