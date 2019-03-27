/*
 * parseutil.c - parse utilities for string and wire conversion
 *
 * (c) NLnet Labs, 2004-2006
 *
 * See the file LICENSE for the license
 */
/**
 * \file
 *
 * Utility functions for parsing, base32(DNS variant) and base64 encoding
 * and decoding, Hex, Time units, Escape codes.
 */

#include "config.h"
#include "sldns/parseutil.h"
#include <sys/time.h>
#include <time.h>
#include <ctype.h>

sldns_lookup_table *
sldns_lookup_by_name(sldns_lookup_table *table, const char *name)
{
        while (table->name != NULL) {
                if (strcasecmp(name, table->name) == 0)
                        return table;
                table++;
        }
        return NULL;
}

sldns_lookup_table *
sldns_lookup_by_id(sldns_lookup_table *table, int id)
{
        while (table->name != NULL) {
                if (table->id == id)
                        return table;
                table++;
        }
        return NULL;
}

/* Number of days per month (except for February in leap years). */
static const int mdays[] = {
	31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

#define LDNS_MOD(x,y) (((x) % (y) < 0) ? ((x) % (y) + (y)) : ((x) % (y)))
#define LDNS_DIV(x,y) (((x) % (y) < 0) ? ((x) / (y) -  1 ) : ((x) / (y)))

static int
is_leap_year(int year)
{
	return LDNS_MOD(year,   4) == 0 && (LDNS_MOD(year, 100) != 0 
	    || LDNS_MOD(year, 400) == 0);
}

static int
leap_days(int y1, int y2)
{
	--y1;
	--y2;
	return (LDNS_DIV(y2,   4) - LDNS_DIV(y1,   4)) - 
	       (LDNS_DIV(y2, 100) - LDNS_DIV(y1, 100)) +
	       (LDNS_DIV(y2, 400) - LDNS_DIV(y1, 400));
}

/*
 * Code adapted from Python 2.4.1 sources (Lib/calendar.py).
 */
time_t
sldns_mktime_from_utc(const struct tm *tm)
{
	int year = 1900 + tm->tm_year;
	time_t days = 365 * ((time_t) year - 1970) + leap_days(1970, year);
	time_t hours;
	time_t minutes;
	time_t seconds;
	int i;

	for (i = 0; i < tm->tm_mon; ++i) {
		days += mdays[i];
	}
	if (tm->tm_mon > 1 && is_leap_year(year)) {
		++days;
	}
	days += tm->tm_mday - 1;

	hours = days * 24 + tm->tm_hour;
	minutes = hours * 60 + tm->tm_min;
	seconds = minutes * 60 + tm->tm_sec;

	return seconds;
}

#if SIZEOF_TIME_T <= 4

static void
sldns_year_and_yday_from_days_since_epoch(int64_t days, struct tm *result)
{
	int year = 1970;
	int new_year;

	while (days < 0 || days >= (int64_t) (is_leap_year(year) ? 366 : 365)) {
		new_year = year + (int) LDNS_DIV(days, 365);
		days -= (new_year - year) * 365;
		days -= leap_days(year, new_year);
		year  = new_year;
	}
	result->tm_year = year;
	result->tm_yday = (int) days;
}

/* Number of days per month in a leap year. */
static const int leap_year_mdays[] = {
	31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

static void
sldns_mon_and_mday_from_year_and_yday(struct tm *result)
{
	int idays = result->tm_yday;
	const int *mon_lengths = is_leap_year(result->tm_year) ? 
					leap_year_mdays : mdays;

	result->tm_mon = 0;
	while  (idays >= mon_lengths[result->tm_mon]) {
		idays -= mon_lengths[result->tm_mon++];
	}
	result->tm_mday = idays + 1;
}

static void
sldns_wday_from_year_and_yday(struct tm *result)
{
	result->tm_wday = 4 /* 1-1-1970 was a thursday */
			+ LDNS_MOD((result->tm_year - 1970), 7) * LDNS_MOD(365, 7)
			+ leap_days(1970, result->tm_year)
			+ result->tm_yday;
	result->tm_wday = LDNS_MOD(result->tm_wday, 7);
	if (result->tm_wday < 0) {
		result->tm_wday += 7;
	}
}

static struct tm *
sldns_gmtime64_r(int64_t clock, struct tm *result)
{
	result->tm_isdst = 0;
	result->tm_sec   = (int) LDNS_MOD(clock, 60);
	clock            =       LDNS_DIV(clock, 60);
	result->tm_min   = (int) LDNS_MOD(clock, 60);
	clock            =       LDNS_DIV(clock, 60);
	result->tm_hour  = (int) LDNS_MOD(clock, 24);
	clock            =       LDNS_DIV(clock, 24);

	sldns_year_and_yday_from_days_since_epoch(clock, result);
	sldns_mon_and_mday_from_year_and_yday(result);
	sldns_wday_from_year_and_yday(result);
	result->tm_year -= 1900;

	return result;
}

#endif /* SIZEOF_TIME_T <= 4 */

static int64_t
sldns_serial_arithmetics_time(int32_t time, time_t now)
{
	int32_t offset = time - (int32_t) now;
	return (int64_t) now + offset;
}

struct tm *
sldns_serial_arithmetics_gmtime_r(int32_t time, time_t now, struct tm *result)
{
#if SIZEOF_TIME_T <= 4
	int64_t secs_since_epoch = sldns_serial_arithmetics_time(time, now);
	return  sldns_gmtime64_r(secs_since_epoch, result);
#else
	time_t  secs_since_epoch = sldns_serial_arithmetics_time(time, now);
	return  gmtime_r(&secs_since_epoch, result);
#endif
}

int
sldns_hexdigit_to_int(char ch)
{
	switch (ch) {
	case '0': return 0;
	case '1': return 1;
	case '2': return 2;
	case '3': return 3;
	case '4': return 4;
	case '5': return 5;
	case '6': return 6;
	case '7': return 7;
	case '8': return 8;
	case '9': return 9;
	case 'a': case 'A': return 10;
	case 'b': case 'B': return 11;
	case 'c': case 'C': return 12;
	case 'd': case 'D': return 13;
	case 'e': case 'E': return 14;
	case 'f': case 'F': return 15;
	default:
		return -1;
	}
}

uint32_t
sldns_str2period(const char *nptr, const char **endptr)
{
	int sign = 0;
	uint32_t i = 0;
	uint32_t seconds = 0;

	for(*endptr = nptr; **endptr; (*endptr)++) {
		switch (**endptr) {
			case ' ':
			case '\t':
				break;
			case '-':
				if(sign == 0) {
					sign = -1;
				} else {
					return seconds;
				}
				break;
			case '+':
				if(sign == 0) {
					sign = 1;
				} else {
					return seconds;
				}
				break;
			case 's':
			case 'S':
				seconds += i;
				i = 0;
				break;
			case 'm':
			case 'M':
				seconds += i * 60;
				i = 0;
				break;
			case 'h':
			case 'H':
				seconds += i * 60 * 60;
				i = 0;
				break;
			case 'd':
			case 'D':
				seconds += i * 60 * 60 * 24;
				i = 0;
				break;
			case 'w':
			case 'W':
				seconds += i * 60 * 60 * 24 * 7;
				i = 0;
				break;
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				i *= 10;
				i += (**endptr - '0');
				break;
			default:
				seconds += i;
				/* disregard signedness */
				return seconds;
		}
	}
	seconds += i;
	/* disregard signedness */
	return seconds;
}

int
sldns_parse_escape(uint8_t *ch_p, const char** str_p)
{
	uint16_t val;

	if ((*str_p)[0] && isdigit((unsigned char)(*str_p)[0]) &&
	    (*str_p)[1] && isdigit((unsigned char)(*str_p)[1]) &&
	    (*str_p)[2] && isdigit((unsigned char)(*str_p)[2])) {

		val = (uint16_t)(((*str_p)[0] - '0') * 100 +
				 ((*str_p)[1] - '0') *  10 +
				 ((*str_p)[2] - '0'));

		if (val > 255) {
			goto error;
		}
		*ch_p = (uint8_t)val;
		*str_p += 3;
		return 1;

	} else if ((*str_p)[0] && !isdigit((unsigned char)(*str_p)[0])) {

		*ch_p = (uint8_t)*(*str_p)++;
		return 1;
	}
error:
	*str_p = NULL;
	return 0; /* LDNS_WIREPARSE_ERR_SYNTAX_BAD_ESCAPE */
}

/** parse one character, with escape codes */
int
sldns_parse_char(uint8_t *ch_p, const char** str_p)
{
	switch (**str_p) {

	case '\0':	return 0;

	case '\\':	*str_p += 1;
			return sldns_parse_escape(ch_p, str_p);

	default:	*ch_p = (uint8_t)*(*str_p)++;
			return 1;
	}
}

size_t sldns_b32_ntop_calculate_size(size_t src_data_length)
{
	return src_data_length == 0 ? 0 : ((src_data_length - 1) / 5 + 1) * 8;
}

size_t sldns_b32_ntop_calculate_size_no_padding(size_t src_data_length)
{
	return ((src_data_length + 3) * 8 / 5) - 4;
}

static int
sldns_b32_ntop_base(const uint8_t* src, size_t src_sz, char* dst, size_t dst_sz,
	int extended_hex, int add_padding)
{
	size_t ret_sz;
	const char* b32 = extended_hex ?  "0123456789abcdefghijklmnopqrstuv"
					: "abcdefghijklmnopqrstuvwxyz234567";

	size_t c = 0; /* c is used to carry partial base32 character over 
		       * byte boundaries for sizes with a remainder.
		       * (i.e. src_sz % 5 != 0)
		       */

	ret_sz = add_padding ? sldns_b32_ntop_calculate_size(src_sz)
			     : sldns_b32_ntop_calculate_size_no_padding(src_sz);
	
	/* Do we have enough space? */
	if (dst_sz < ret_sz + 1)
		return -1;

	/* We know the size; terminate the string */
	dst[ret_sz] = '\0';

	/* First process all chunks of five */
	while (src_sz >= 5) {
		/* 00000... ........ ........ ........ ........ */
		dst[0] = b32[(src[0]       ) >> 3];

		/* .....111 11...... ........ ........ ........ */
		dst[1] = b32[(src[0] & 0x07) << 2 | src[1] >> 6];

		/* ........ ..22222. ........ ........ ........ */
		dst[2] = b32[(src[1] & 0x3e) >> 1];

		/* ........ .......3 3333.... ........ ........ */
		dst[3] = b32[(src[1] & 0x01) << 4 | src[2] >> 4];

		/* ........ ........ ....4444 4....... ........ */
		dst[4] = b32[(src[2] & 0x0f) << 1 | src[3] >> 7];

		/* ........ ........ ........ .55555.. ........ */
		dst[5] = b32[(src[3] & 0x7c) >> 2];

		/* ........ ........ ........ ......66 666..... */
		dst[6] = b32[(src[3] & 0x03) << 3 | src[4] >> 5];

		/* ........ ........ ........ ........ ...77777 */
		dst[7] = b32[(src[4] & 0x1f)     ];

		src_sz -= 5;
		src    += 5;
		dst    += 8;
	}
	/* Process what remains */
	switch (src_sz) {
	case 4: /* ........ ........ ........ ......66 666..... */
		dst[6] = b32[(src[3] & 0x03) << 3];

		/* ........ ........ ........ .55555.. ........ */
		dst[5] = b32[(src[3] & 0x7c) >> 2];

		/* ........ ........ ....4444 4....... ........ */
			 c =  src[3]         >> 7 ;
		/* fallthrough */
	case 3: dst[4] = b32[(src[2] & 0x0f) << 1 | c];

		/* ........ .......3 3333.... ........ ........ */
			 c =  src[2]         >> 4 ;
		/* fallthrough */
	case 2:	dst[3] = b32[(src[1] & 0x01) << 4 | c];

		/* ........ ..22222. ........ ........ ........ */
		dst[2] = b32[(src[1] & 0x3e) >> 1];

		/* .....111 11...... ........ ........ ........ */
			 c =  src[1]         >> 6 ;
		/* fallthrough */
	case 1:	dst[1] = b32[(src[0] & 0x07) << 2 | c];

		/* 00000... ........ ........ ........ ........ */
		dst[0] = b32[ src[0]         >> 3];
	}
	/* Add padding */
	if (add_padding) {
		switch (src_sz) {
			case 1: dst[2] = '=';
				dst[3] = '=';
				/* fallthrough */
			case 2: dst[4] = '=';
				/* fallthrough */
			case 3: dst[5] = '=';
				dst[6] = '=';
				/* fallthrough */
			case 4: dst[7] = '=';
		}
	}
	return (int)ret_sz;
}

int 
sldns_b32_ntop(const uint8_t* src, size_t src_sz, char* dst, size_t dst_sz)
{
	return sldns_b32_ntop_base(src, src_sz, dst, dst_sz, 0, 1);
}

int 
sldns_b32_ntop_extended_hex(const uint8_t* src, size_t src_sz,
		char* dst, size_t dst_sz)
{
	return sldns_b32_ntop_base(src, src_sz, dst, dst_sz, 1, 1);
}

size_t sldns_b32_pton_calculate_size(size_t src_text_length)
{
	return src_text_length * 5 / 8;
}

static int
sldns_b32_pton_base(const char* src, size_t src_sz, uint8_t* dst, size_t dst_sz,
	int extended_hex, int check_padding)
{
	size_t i = 0;
	char ch = '\0';
	uint8_t buf[8];
	uint8_t* start = dst;

	while (src_sz) {
		/* Collect 8 characters in buf (if possible) */
		for (i = 0; i < 8; i++) {

			do {
				ch = *src++;
				--src_sz;

			} while (isspace((unsigned char)ch) && src_sz > 0);

			if (ch == '=' || ch == '\0')
				break;

			else if (extended_hex)

				if (ch >= '0' && ch <= '9')
					buf[i] = (uint8_t)ch - '0';
				else if (ch >= 'a' && ch <= 'v')
					buf[i] = (uint8_t)ch - 'a' + 10;
				else if (ch >= 'A' && ch <= 'V')
					buf[i] = (uint8_t)ch - 'A' + 10;
				else
					return -1;

			else if (ch >= 'a' && ch <= 'z')
				buf[i] = (uint8_t)ch - 'a';
			else if (ch >= 'A' && ch <= 'Z')
				buf[i] = (uint8_t)ch - 'A';
			else if (ch >= '2' && ch <= '7')
				buf[i] = (uint8_t)ch - '2' + 26;
			else
				return -1;
		}
		/* Less that 8 characters. We're done. */
		if (i < 8)
			break;

		/* Enough space available at the destination? */
		if (dst_sz < 5)
			return -1;

		/* 00000... ........ ........ ........ ........ */
		/* .....111 11...... ........ ........ ........ */
		dst[0] = buf[0] << 3 | buf[1] >> 2;

		/* .....111 11...... ........ ........ ........ */
		/* ........ ..22222. ........ ........ ........ */
		/* ........ .......3 3333.... ........ ........ */
		dst[1] = buf[1] << 6 | buf[2] << 1 | buf[3] >> 4;

		/* ........ .......3 3333.... ........ ........ */
		/* ........ ........ ....4444 4....... ........ */
		dst[2] = buf[3] << 4 | buf[4] >> 1;

		/* ........ ........ ....4444 4....... ........ */
		/* ........ ........ ........ .55555.. ........ */
		/* ........ ........ ........ ......66 666..... */
		dst[3] = buf[4] << 7 | buf[5] << 2 | buf[6] >> 3;

		/* ........ ........ ........ ......66 666..... */
		/* ........ ........ ........ ........ ...77777 */
		dst[4] = buf[6] << 5 | buf[7];

		dst += 5;
		dst_sz -= 5;
	}
	/* Not ending on a eight byte boundary? */
	if (i > 0 && i < 8) {

		/* Enough space available at the destination? */
		if (dst_sz < (i + 1) / 2)
			return -1;

		switch (i) {
		case 7: /* ........ ........ ........ ......66 666..... */
			/* ........ ........ ........ .55555.. ........ */
			/* ........ ........ ....4444 4....... ........ */
			dst[3] = buf[4] << 7 | buf[5] << 2 | buf[6] >> 3;
			/* fallthrough */

		case 5: /* ........ ........ ....4444 4....... ........ */
			/* ........ .......3 3333.... ........ ........ */
			dst[2] = buf[3] << 4 | buf[4] >> 1;
			/* fallthrough */

		case 4: /* ........ .......3 3333.... ........ ........ */
			/* ........ ..22222. ........ ........ ........ */
			/* .....111 11...... ........ ........ ........ */
			dst[1] = buf[1] << 6 | buf[2] << 1 | buf[3] >> 4;
			/* fallthrough */

		case 2: /* .....111 11...... ........ ........ ........ */
			/* 00000... ........ ........ ........ ........ */
			dst[0] = buf[0] << 3 | buf[1] >> 2;

			break;

		default:
			return -1;
		}
		dst += (i + 1) / 2;

		if (check_padding) {
			/* Check remaining padding characters */
			if (ch != '=')
				return -1;

			/* One down, 8 - i - 1 more to come... */
			for (i = 8 - i - 1; i > 0; i--) {

				do {
					if (src_sz == 0)
						return -1;
					ch = *src++;
					src_sz--;

				} while (isspace((unsigned char)ch));

				if (ch != '=')
					return -1;
			}
		}
	}
	return dst - start;
}

int
sldns_b32_pton(const char* src, size_t src_sz, uint8_t* dst, size_t dst_sz)
{
	return sldns_b32_pton_base(src, src_sz, dst, dst_sz, 0, 1);
}

int
sldns_b32_pton_extended_hex(const char* src, size_t src_sz, 
		uint8_t* dst, size_t dst_sz)
{
	return sldns_b32_pton_base(src, src_sz, dst, dst_sz, 1, 1);
}

size_t sldns_b64_ntop_calculate_size(size_t srcsize)
{
	return ((((srcsize + 2) / 3) * 4) + 1);
}

/* RFC 1521, section 5.2.
 *
 * The encoding process represents 24-bit groups of input bits as output
 * strings of 4 encoded characters. Proceeding from left to right, a
 * 24-bit input group is formed by concatenating 3 8-bit input groups.
 * These 24 bits are then treated as 4 concatenated 6-bit groups, each
 * of which is translated into a single digit in the base64 alphabet.
 *
 * This routine does not insert spaces or linebreaks after 76 characters.
 */
int sldns_b64_ntop(uint8_t const *src, size_t srclength,
	char *target, size_t targsize)
{
	const char* b64 =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	const char pad64 = '=';
	size_t i = 0, o = 0;
	if(targsize < sldns_b64_ntop_calculate_size(srclength))
		return -1;
	/* whole chunks: xxxxxxyy yyyyzzzz zzwwwwww */
	while(i+3 <= srclength) {
		if(o+4 > targsize) return -1;
		target[o] = b64[src[i] >> 2];
		target[o+1] = b64[ ((src[i]&0x03)<<4) | (src[i+1]>>4) ];
		target[o+2] = b64[ ((src[i+1]&0x0f)<<2) | (src[i+2]>>6) ];
		target[o+3] = b64[ (src[i+2]&0x3f) ];
		i += 3;
		o += 4;
	}
	/* remainder */
	switch(srclength - i) {
	case 2:
		/* two at end, converted into A B C = */
		target[o] = b64[src[i] >> 2];
		target[o+1] = b64[ ((src[i]&0x03)<<4) | (src[i+1]>>4) ];
		target[o+2] = b64[ ((src[i+1]&0x0f)<<2) ];
		target[o+3] = pad64;
		/* i += 2; */
		o += 4;
		break;
	case 1:
		/* one at end, converted into A B = = */
		target[o] = b64[src[i] >> 2];
		target[o+1] = b64[ ((src[i]&0x03)<<4) ];
		target[o+2] = pad64;
		target[o+3] = pad64;
		/* i += 1; */
		o += 4;
		break;
	case 0:
	default:
		/* nothing */
		break;
	}
	/* assert: i == srclength */
	if(o+1 > targsize) return -1;
	target[o] = 0;
	return (int)o;
}

size_t sldns_b64_pton_calculate_size(size_t srcsize)
{
	return (((((srcsize + 3) / 4) * 3)) + 1);
}

int sldns_b64_pton(char const *src, uint8_t *target, size_t targsize)
{
	const uint8_t pad64 = 64; /* is 64th in the b64 array */
	const char* s = src;
	uint8_t in[4];
	size_t o = 0, incount = 0;

	while(*s) {
		/* skip any character that is not base64 */
		/* conceptually we do:
		const char* b64 =      pad'=' is appended to array
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";
		const char* d = strchr(b64, *s++);
		and use d-b64;
		*/
		char d = *s++;
		if(d <= 'Z' && d >= 'A')
			d -= 'A';
		else if(d <= 'z' && d >= 'a')
			d = d - 'a' + 26;
		else if(d <= '9' && d >= '0')
			d = d - '0' + 52;
		else if(d == '+')
			d = 62;
		else if(d == '/')
			d = 63;
		else if(d == '=')
			d = 64;
		else	continue;
		in[incount++] = (uint8_t)d;
		if(incount != 4)
			continue;
		/* process whole block of 4 characters into 3 output bytes */
		if(in[3] == pad64 && in[2] == pad64) { /* A B = = */
			if(o+1 > targsize)
				return -1;
			target[o] = (in[0]<<2) | ((in[1]&0x30)>>4);
			o += 1;
			break; /* we are done */
		} else if(in[3] == pad64) { /* A B C = */
			if(o+2 > targsize)
				return -1;
			target[o] = (in[0]<<2) | ((in[1]&0x30)>>4);
			target[o+1]= ((in[1]&0x0f)<<4) | ((in[2]&0x3c)>>2);
			o += 2;
			break; /* we are done */
		} else {
			if(o+3 > targsize)
				return -1;
			/* write xxxxxxyy yyyyzzzz zzwwwwww */
			target[o] = (in[0]<<2) | ((in[1]&0x30)>>4);
			target[o+1]= ((in[1]&0x0f)<<4) | ((in[2]&0x3c)>>2);
			target[o+2]= ((in[2]&0x03)<<6) | in[3];
			o += 3;
		}
		incount = 0;
	}
	return (int)o;
}
