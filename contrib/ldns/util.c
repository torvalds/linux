/*
 * util.c
 *
 * some general memory functions
 *
 * a Net::DNS like library for C
 *
 * (c) NLnet Labs, 2004-2006
 *
 * See the file LICENSE for the license
 */

#include <ldns/config.h>

#include <ldns/rdata.h>
#include <ldns/rr.h>
#include <ldns/util.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <ctype.h>

#ifdef HAVE_SSL
#include <openssl/rand.h>
#endif

ldns_lookup_table *
ldns_lookup_by_name(ldns_lookup_table *table, const char *name)
{
	while (table->name != NULL) {
		if (strcasecmp(name, table->name) == 0)
			return table;
		table++;
	}
	return NULL;
}

ldns_lookup_table *
ldns_lookup_by_id(ldns_lookup_table *table, int id)
{
	while (table->name != NULL) {
		if (table->id == id)
			return table;
		table++;
	}
	return NULL;
}

int
ldns_get_bit(uint8_t bits[], size_t index)
{
	/*
	 * The bits are counted from left to right, so bit #0 is the
	 * left most bit.
	 */
	return (int) (bits[index / 8] & (1 << (7 - index % 8)));
}

int
ldns_get_bit_r(uint8_t bits[], size_t index)
{
	/*
	 * The bits are counted from right to left, so bit #0 is the
	 * right most bit.
	 */
	return (int) bits[index / 8] & (1 << (index % 8));
}

void
ldns_set_bit(uint8_t *byte, int bit_nr, bool value)
{
	/*
	 * The bits are counted from right to left, so bit #0 is the
	 * right most bit.
	 */
	if (bit_nr >= 0 && bit_nr < 8) {
		if (value) {
			*byte = *byte | (0x01 << bit_nr);
		} else {
			*byte = *byte & ~(0x01 << bit_nr);
		}
	}
}

int
ldns_hexdigit_to_int(char ch)
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

char
ldns_int_to_hexdigit(int i)
{
	switch (i) {
	case 0: return '0';
	case 1: return '1';
	case 2: return '2';
	case 3: return '3';
	case 4: return '4';
	case 5: return '5';
	case 6: return '6';
	case 7: return '7';
	case 8: return '8';
	case 9: return '9';
	case 10: return 'a';
	case 11: return 'b';
	case 12: return 'c';
	case 13: return 'd';
	case 14: return 'e';
	case 15: return 'f';
	default:
		abort();
	}
}

int
ldns_hexstring_to_data(uint8_t *data, const char *str)
{
	size_t i;

	if (!str || !data) {
		return -1;
	}

	if (strlen(str) % 2 != 0) {
		return -2;
	}

	for (i = 0; i < strlen(str) / 2; i++) {
		data[i] =
			16 * (uint8_t) ldns_hexdigit_to_int(str[i*2]) +
			(uint8_t) ldns_hexdigit_to_int(str[i*2 + 1]);
	}

	return (int) i;
}

const char *
ldns_version(void)
{
	return (char*)LDNS_VERSION;
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
ldns_mktime_from_utc(const struct tm *tm)
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

time_t
mktime_from_utc(const struct tm *tm)
{
	return ldns_mktime_from_utc(tm);
}

#if SIZEOF_TIME_T <= 4

static void
ldns_year_and_yday_from_days_since_epoch(int64_t days, struct tm *result)
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
ldns_mon_and_mday_from_year_and_yday(struct tm *result)
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
ldns_wday_from_year_and_yday(struct tm *result)
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
ldns_gmtime64_r(int64_t clock, struct tm *result)
{
	result->tm_isdst = 0;
	result->tm_sec   = (int) LDNS_MOD(clock, 60);
	clock            =       LDNS_DIV(clock, 60);
	result->tm_min   = (int) LDNS_MOD(clock, 60);
	clock            =       LDNS_DIV(clock, 60);
	result->tm_hour  = (int) LDNS_MOD(clock, 24);
	clock            =       LDNS_DIV(clock, 24);

	ldns_year_and_yday_from_days_since_epoch(clock, result);
	ldns_mon_and_mday_from_year_and_yday(result);
	ldns_wday_from_year_and_yday(result);
	result->tm_year -= 1900;

	return result;
}

#endif /* SIZEOF_TIME_T <= 4 */

static int64_t
ldns_serial_arithmitics_time(int32_t time, time_t now)
{
	int32_t offset = time - (int32_t) now;
	return (int64_t) now + offset;
}


struct tm *
ldns_serial_arithmitics_gmtime_r(int32_t time, time_t now, struct tm *result)
{
#if SIZEOF_TIME_T <= 4
	int64_t secs_since_epoch = ldns_serial_arithmitics_time(time, now);
	return  ldns_gmtime64_r(secs_since_epoch, result);
#else
	time_t  secs_since_epoch = ldns_serial_arithmitics_time(time, now);
	return  gmtime_r(&secs_since_epoch, result);
#endif
}

/**
 * Init the random source
 * applications should call this if they need entropy data within ldns
 * If openSSL is available, it is automatically seeded from /dev/urandom
 * or /dev/random
 *
 * If you need more entropy, or have no openssl available, this function
 * MUST be called at the start of the program
 *
 * If openssl *is* available, this function just adds more entropy
 **/
int
ldns_init_random(FILE *fd, unsigned int size)
{
	/* if fp is given, seed srandom with data from file
	   otherwise use /dev/urandom */
	FILE *rand_f;
	uint8_t *seed;
	size_t read = 0;
	unsigned int seed_i;
	struct timeval tv;

	/* we'll need at least sizeof(unsigned int) bytes for the
	   standard prng seed */
	if (size < (unsigned int) sizeof(seed_i)){
		size = (unsigned int) sizeof(seed_i);
	}

	seed = LDNS_XMALLOC(uint8_t, size);
        if(!seed) {
		return 1;
        }

	if (!fd) {
		if ((rand_f = fopen("/dev/urandom", "r")) == NULL) {
			/* no readable /dev/urandom, try /dev/random */
			if ((rand_f = fopen("/dev/random", "r")) == NULL) {
				/* no readable /dev/random either, and no entropy
				   source given. we'll have to improvise */
				for (read = 0; read < size; read++) {
					gettimeofday(&tv, NULL);
					seed[read] = (uint8_t) (tv.tv_usec % 256);
				}
			} else {
				read = fread(seed, 1, size, rand_f);
			}
		} else {
			read = fread(seed, 1, size, rand_f);
		}
	} else {
		rand_f = fd;
		read = fread(seed, 1, size, rand_f);
	}

	if (read < size) {
		LDNS_FREE(seed);
		if (!fd) fclose(rand_f);
		return 1;
	} else {
#ifdef HAVE_SSL
		/* Seed the OpenSSL prng (most systems have it seeded
		   automatically, in that case this call just adds entropy */
		RAND_seed(seed, (int) size);
#else
		/* Seed the standard prng, only uses the first
		 * unsigned sizeof(unsiged int) bytes found in the entropy pool
		 */
		memcpy(&seed_i, seed, sizeof(seed_i));
		srandom(seed_i);
#endif
		LDNS_FREE(seed);
	}

	if (!fd) {
                if (rand_f) fclose(rand_f);
	}

	return 0;
}

/**
 * Get random number.
 *
 */
uint16_t
ldns_get_random(void)
{
        uint16_t rid = 0;
#ifdef HAVE_SSL
        if (RAND_bytes((unsigned char*)&rid, 2) != 1) {
                rid = (uint16_t) random();
        }
#else
        rid = (uint16_t) random();
#endif
	return rid;
}

/*
 * BubbleBabble code taken from OpenSSH
 * Copyright (c) 2001 Carsten Raskgaard.  All rights reserved.
 */
char *
ldns_bubblebabble(uint8_t *data, size_t len)
{
	char vowels[] = { 'a', 'e', 'i', 'o', 'u', 'y' };
	char consonants[] = { 'b', 'c', 'd', 'f', 'g', 'h', 'k', 'l', 'm',
	    'n', 'p', 'r', 's', 't', 'v', 'z', 'x' };
	size_t i, j = 0, rounds, seed = 1;
	char *retval;

	rounds = (len / 2) + 1;
	retval = LDNS_XMALLOC(char, rounds * 6);
	if(!retval) return NULL;
	retval[j++] = 'x';
	for (i = 0; i < rounds; i++) {
		size_t idx0, idx1, idx2, idx3, idx4;
		if ((i + 1 < rounds) || (len % 2 != 0)) {
			idx0 = (((((size_t)(data[2 * i])) >> 6) & 3) +
			    seed) % 6;
			idx1 = (((size_t)(data[2 * i])) >> 2) & 15;
			idx2 = ((((size_t)(data[2 * i])) & 3) +
			    (seed / 6)) % 6;
			retval[j++] = vowels[idx0];
			retval[j++] = consonants[idx1];
			retval[j++] = vowels[idx2];
			if ((i + 1) < rounds) {
				idx3 = (((size_t)(data[(2 * i) + 1])) >> 4) & 15;
				idx4 = (((size_t)(data[(2 * i) + 1]))) & 15;
				retval[j++] = consonants[idx3];
				retval[j++] = '-';
				retval[j++] = consonants[idx4];
				seed = ((seed * 5) +
				    ((((size_t)(data[2 * i])) * 7) +
				    ((size_t)(data[(2 * i) + 1])))) % 36;
			}
		} else {
			idx0 = seed % 6;
			idx1 = 16;
			idx2 = seed / 6;
			retval[j++] = vowels[idx0];
			retval[j++] = consonants[idx1];
			retval[j++] = vowels[idx2];
		}
	}
	retval[j++] = 'x';
	retval[j++] = '\0';
	return retval;
}

/*
 * For backwards compatibility, because we have always exported this symbol.
 */
#ifdef HAVE_B64_NTOP
int ldns_b64_ntop(const uint8_t* src, size_t srclength,
		char *target, size_t targsize);
{
	return b64_ntop(src, srclength, target, targsize);
}
#endif

/*
 * For backwards compatibility, because we have always exported this symbol.
 */
#ifdef HAVE_B64_PTON
int ldns_b64_pton(const char* src, uint8_t *target, size_t targsize)
{
	return b64_pton(src, target, targsize);
}
#endif


static int
ldns_b32_ntop_base(const uint8_t* src, size_t src_sz,
		char* dst, size_t dst_sz,
		bool extended_hex, bool add_padding)
{
	size_t ret_sz;
	const char* b32 = extended_hex ? "0123456789abcdefghijklmnopqrstuv"
	                               : "abcdefghijklmnopqrstuvwxyz234567";

	size_t c = 0; /* c is used to carry partial base32 character over 
	               * byte boundaries for sizes with a remainder.
		       * (i.e. src_sz % 5 != 0)
		       */

	ret_sz = add_padding ? ldns_b32_ntop_calculate_size(src_sz)
	                     : ldns_b32_ntop_calculate_size_no_padding(src_sz);
	
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
	case 3: dst[4] = b32[(src[2] & 0x0f) << 1 | c];

		/* ........ .......3 3333.... ........ ........ */
			 c =  src[2]         >> 4 ;
	case 2:	dst[3] = b32[(src[1] & 0x01) << 4 | c];

		/* ........ ..22222. ........ ........ ........ */
		dst[2] = b32[(src[1] & 0x3e) >> 1];

		/* .....111 11...... ........ ........ ........ */
	                 c =  src[1]         >> 6 ;
	case 1:	dst[1] = b32[(src[0] & 0x07) << 2 | c];

		/* 00000... ........ ........ ........ ........ */
		dst[0] = b32[ src[0]         >> 3];
	}
	/* Add padding */
	if (add_padding) {
		switch (src_sz) {
			case 1: dst[2] = '=';
				dst[3] = '=';
			case 2: dst[4] = '=';
			case 3: dst[5] = '=';
				dst[6] = '=';
			case 4: dst[7] = '=';
		}
	}
	return (int)ret_sz;
}

int 
ldns_b32_ntop(const uint8_t* src, size_t src_sz, char* dst, size_t dst_sz)
{
	return ldns_b32_ntop_base(src, src_sz, dst, dst_sz, false, true);
}

int 
ldns_b32_ntop_extended_hex(const uint8_t* src, size_t src_sz,
		char* dst, size_t dst_sz)
{
	return ldns_b32_ntop_base(src, src_sz, dst, dst_sz, true, true);
}

#ifndef HAVE_B32_NTOP

int 
b32_ntop(const uint8_t* src, size_t src_sz, char* dst, size_t dst_sz)
{
	return ldns_b32_ntop_base(src, src_sz, dst, dst_sz, false, true);
}

int 
b32_ntop_extended_hex(const uint8_t* src, size_t src_sz,
		char* dst, size_t dst_sz)
{
	return ldns_b32_ntop_base(src, src_sz, dst, dst_sz, true, true);
}

#endif /* ! HAVE_B32_NTOP */

static int
ldns_b32_pton_base(const char* src, size_t src_sz,
		uint8_t* dst, size_t dst_sz,
		bool extended_hex, bool check_padding)
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

		case 5: /* ........ ........ ....4444 4....... ........ */
			/* ........ .......3 3333.... ........ ........ */
			dst[2] = buf[3] << 4 | buf[4] >> 1;

		case 4: /* ........ .......3 3333.... ........ ........ */
			/* ........ ..22222. ........ ........ ........ */
			/* .....111 11...... ........ ........ ........ */
			dst[1] = buf[1] << 6 | buf[2] << 1 | buf[3] >> 4;

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
ldns_b32_pton(const char* src, size_t src_sz, uint8_t* dst, size_t dst_sz)
{
	return ldns_b32_pton_base(src, src_sz, dst, dst_sz, false, true);
}

int
ldns_b32_pton_extended_hex(const char* src, size_t src_sz, 
		uint8_t* dst, size_t dst_sz)
{
	return ldns_b32_pton_base(src, src_sz, dst, dst_sz, true, true);
}

#ifndef HAVE_B32_PTON

int
b32_pton(const char* src, size_t src_sz, uint8_t* dst, size_t dst_sz)
{
	return ldns_b32_pton_base(src, src_sz, dst, dst_sz, false, true);
}

int
b32_pton_extended_hex(const char* src, size_t src_sz, 
		uint8_t* dst, size_t dst_sz)
{
	return ldns_b32_pton_base(src, src_sz, dst, dst_sz, true, true);
}

#endif /* ! HAVE_B32_PTON */

