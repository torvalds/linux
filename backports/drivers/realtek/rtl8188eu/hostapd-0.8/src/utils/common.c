/*
 * wpa_supplicant/hostapd / common helper functions, etc.
 * Copyright (c) 2002-2007, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#include "includes.h"

#include "common.h"


static int hex2num(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}


int hex2byte(const char *hex)
{
	int a, b;
	a = hex2num(*hex++);
	if (a < 0)
		return -1;
	b = hex2num(*hex++);
	if (b < 0)
		return -1;
	return (a << 4) | b;
}


/**
 * hwaddr_aton - Convert ASCII string to MAC address (colon-delimited format)
 * @txt: MAC address as a string (e.g., "00:11:22:33:44:55")
 * @addr: Buffer for the MAC address (ETH_ALEN = 6 bytes)
 * Returns: 0 on success, -1 on failure (e.g., string not a MAC address)
 */
int hwaddr_aton(const char *txt, u8 *addr)
{
	int i;

	for (i = 0; i < 6; i++) {
		int a, b;

		a = hex2num(*txt++);
		if (a < 0)
			return -1;
		b = hex2num(*txt++);
		if (b < 0)
			return -1;
		*addr++ = (a << 4) | b;
		if (i < 5 && *txt++ != ':')
			return -1;
	}

	return 0;
}

/**
 * hwaddr_compact_aton - Convert ASCII string to MAC address (no colon delimitors format)
 * @txt: MAC address as a string (e.g., "001122334455")
 * @addr: Buffer for the MAC address (ETH_ALEN = 6 bytes)
 * Returns: 0 on success, -1 on failure (e.g., string not a MAC address)
 */
int hwaddr_compact_aton(const char *txt, u8 *addr)
{
	int i;

	for (i = 0; i < 6; i++) {
		int a, b;

		a = hex2num(*txt++);
		if (a < 0)
			return -1;
		b = hex2num(*txt++);
		if (b < 0)
			return -1;
		*addr++ = (a << 4) | b;
	}

	return 0;
}

/**
 * hwaddr_aton2 - Convert ASCII string to MAC address (in any known format)
 * @txt: MAC address as a string (e.g., 00:11:22:33:44:55 or 0011.2233.4455)
 * @addr: Buffer for the MAC address (ETH_ALEN = 6 bytes)
 * Returns: Characters used (> 0) on success, -1 on failure
 */
int hwaddr_aton2(const char *txt, u8 *addr)
{
	int i;
	const char *pos = txt;

	for (i = 0; i < 6; i++) {
		int a, b;

		while (*pos == ':' || *pos == '.' || *pos == '-')
			pos++;

		a = hex2num(*pos++);
		if (a < 0)
			return -1;
		b = hex2num(*pos++);
		if (b < 0)
			return -1;
		*addr++ = (a << 4) | b;
	}

	return pos - txt;
}


/**
 * hexstr2bin - Convert ASCII hex string into binary data
 * @hex: ASCII hex string (e.g., "01ab")
 * @buf: Buffer for the binary data
 * @len: Length of the text to convert in bytes (of buf); hex will be double
 * this size
 * Returns: 0 on success, -1 on failure (invalid hex string)
 */
int hexstr2bin(const char *hex, u8 *buf, size_t len)
{
	size_t i;
	int a;
	const char *ipos = hex;
	u8 *opos = buf;

	for (i = 0; i < len; i++) {
		a = hex2byte(ipos);
		if (a < 0)
			return -1;
		*opos++ = a;
		ipos += 2;
	}
	return 0;
}


/**
 * inc_byte_array - Increment arbitrary length byte array by one
 * @counter: Pointer to byte array
 * @len: Length of the counter in bytes
 *
 * This function increments the last byte of the counter by one and continues
 * rolling over to more significant bytes if the byte was incremented from
 * 0xff to 0x00.
 */
void inc_byte_array(u8 *counter, size_t len)
{
	int pos = len - 1;
	while (pos >= 0) {
		counter[pos]++;
		if (counter[pos] != 0)
			break;
		pos--;
	}
}


void wpa_get_ntp_timestamp(u8 *buf)
{
	struct os_time now;
	u32 sec, usec;
	be32 tmp;

	/* 64-bit NTP timestamp (time from 1900-01-01 00:00:00) */
	os_get_time(&now);
	sec = now.sec + 2208988800U; /* Epoch to 1900 */
	/* Estimate 2^32/10^6 = 4295 - 1/32 - 1/512 */
	usec = now.usec;
	usec = 4295 * usec - (usec >> 5) - (usec >> 9);
	tmp = host_to_be32(sec);
	os_memcpy(buf, (u8 *) &tmp, 4);
	tmp = host_to_be32(usec);
	os_memcpy(buf + 4, (u8 *) &tmp, 4);
}


static inline int _wpa_snprintf_hex(char *buf, size_t buf_size, const u8 *data,
				    size_t len, int uppercase)
{
	size_t i;
	char *pos = buf, *end = buf + buf_size;
	int ret;
	if (buf_size == 0)
		return 0;
	for (i = 0; i < len; i++) {
		ret = os_snprintf(pos, end - pos, uppercase ? "%02X" : "%02x",
				  data[i]);
		if (ret < 0 || ret >= end - pos) {
			end[-1] = '\0';
			return pos - buf;
		}
		pos += ret;
	}
	end[-1] = '\0';
	return pos - buf;
}

/**
 * wpa_snprintf_hex - Print data as a hex string into a buffer
 * @buf: Memory area to use as the output buffer
 * @buf_size: Maximum buffer size in bytes (should be at least 2 * len + 1)
 * @data: Data to be printed
 * @len: Length of data in bytes
 * Returns: Number of bytes written
 */
int wpa_snprintf_hex(char *buf, size_t buf_size, const u8 *data, size_t len)
{
	return _wpa_snprintf_hex(buf, buf_size, data, len, 0);
}


/**
 * wpa_snprintf_hex_uppercase - Print data as a upper case hex string into buf
 * @buf: Memory area to use as the output buffer
 * @buf_size: Maximum buffer size in bytes (should be at least 2 * len + 1)
 * @data: Data to be printed
 * @len: Length of data in bytes
 * Returns: Number of bytes written
 */
int wpa_snprintf_hex_uppercase(char *buf, size_t buf_size, const u8 *data,
			       size_t len)
{
	return _wpa_snprintf_hex(buf, buf_size, data, len, 1);
}


#ifdef CONFIG_ANSI_C_EXTRA

#ifdef _WIN32_WCE
void perror(const char *s)
{
	wpa_printf(MSG_ERROR, "%s: GetLastError: %d",
		   s, (int) GetLastError());
}
#endif /* _WIN32_WCE */


int optind = 1;
int optopt;
char *optarg;

int getopt(int argc, char *const argv[], const char *optstring)
{
	static int optchr = 1;
	char *cp;

	if (optchr == 1) {
		if (optind >= argc) {
			/* all arguments processed */
			return EOF;
		}

		if (argv[optind][0] != '-' || argv[optind][1] == '\0') {
			/* no option characters */
			return EOF;
		}
	}

	if (os_strcmp(argv[optind], "--") == 0) {
		/* no more options */
		optind++;
		return EOF;
	}

	optopt = argv[optind][optchr];
	cp = os_strchr(optstring, optopt);
	if (cp == NULL || optopt == ':') {
		if (argv[optind][++optchr] == '\0') {
			optchr = 1;
			optind++;
		}
		return '?';
	}

	if (cp[1] == ':') {
		/* Argument required */
		optchr = 1;
		if (argv[optind][optchr + 1]) {
			/* No space between option and argument */
			optarg = &argv[optind++][optchr + 1];
		} else if (++optind >= argc) {
			/* option requires an argument */
			return '?';
		} else {
			/* Argument in the next argv */
			optarg = argv[optind++];
		}
	} else {
		/* No argument */
		if (argv[optind][++optchr] == '\0') {
			optchr = 1;
			optind++;
		}
		optarg = NULL;
	}
	return *cp;
}
#endif /* CONFIG_ANSI_C_EXTRA */


#ifdef CONFIG_NATIVE_WINDOWS
/**
 * wpa_unicode2ascii_inplace - Convert unicode string into ASCII
 * @str: Pointer to string to convert
 *
 * This function converts a unicode string to ASCII using the same
 * buffer for output. If UNICODE is not set, the buffer is not
 * modified.
 */
void wpa_unicode2ascii_inplace(TCHAR *str)
{
#ifdef UNICODE
	char *dst = (char *) str;
	while (*str)
		*dst++ = (char) *str++;
	*dst = '\0';
#endif /* UNICODE */
}


TCHAR * wpa_strdup_tchar(const char *str)
{
#ifdef UNICODE
	TCHAR *buf;
	buf = os_malloc((strlen(str) + 1) * sizeof(TCHAR));
	if (buf == NULL)
		return NULL;
	wsprintf(buf, L"%S", str);
	return buf;
#else /* UNICODE */
	return os_strdup(str);
#endif /* UNICODE */
}
#endif /* CONFIG_NATIVE_WINDOWS */


/**
 * wpa_ssid_txt - Convert SSID to a printable string
 * @ssid: SSID (32-octet string)
 * @ssid_len: Length of ssid in octets
 * Returns: Pointer to a printable string
 *
 * This function can be used to convert SSIDs into printable form. In most
 * cases, SSIDs do not use unprintable characters, but IEEE 802.11 standard
 * does not limit the used character set, so anything could be used in an SSID.
 *
 * This function uses a static buffer, so only one call can be used at the
 * time, i.e., this is not re-entrant and the returned buffer must be used
 * before calling this again.
 */
const char * wpa_ssid_txt(const u8 *ssid, size_t ssid_len)
{
	static char ssid_txt[33];
	char *pos;

	if (ssid_len > 32)
		ssid_len = 32;
	os_memcpy(ssid_txt, ssid, ssid_len);
	ssid_txt[ssid_len] = '\0';
	for (pos = ssid_txt; *pos != '\0'; pos++) {
		if ((u8) *pos < 32 || (u8) *pos >= 127)
			*pos = '_';
	}
	return ssid_txt;
}


void * __hide_aliasing_typecast(void *foo)
{
	return foo;
}
