// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/stdarg.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <asm/stacktrace.h>
#include <asm/boot_data.h>
#include <asm/sections.h>
#include <asm/lowcore.h>
#include <asm/setup.h>
#include <asm/sclp.h>
#include <asm/uv.h>
#include "boot.h"

int boot_console_loglevel = CONFIG_CONSOLE_LOGLEVEL_DEFAULT;
bool boot_ignore_loglevel;
char __bootdata(boot_rb)[PAGE_SIZE * 2];
bool __bootdata(boot_earlyprintk);
size_t __bootdata(boot_rb_off);
char __bootdata(bootdebug_filter)[128];
bool __bootdata(bootdebug);

static void boot_rb_add(const char *str, size_t len)
{
	/* leave double '\0' in the end */
	size_t avail = sizeof(boot_rb) - boot_rb_off - 1;

	/* store strings separated by '\0' */
	if (len + 1 > avail)
		boot_rb_off = 0;
	strcpy(boot_rb + boot_rb_off, str);
	boot_rb_off += len + 1;
}

static void print_rb_entry(const char *str)
{
	sclp_early_printk(printk_skip_level(str));
}

static bool debug_messages_printed(void)
{
	return boot_earlyprintk && (boot_ignore_loglevel || boot_console_loglevel > LOGLEVEL_DEBUG);
}

void boot_rb_dump(void)
{
	if (debug_messages_printed())
		return;
	sclp_early_printk("Boot messages ring buffer:\n");
	boot_rb_foreach(print_rb_entry);
}

const char hex_asc[] = "0123456789abcdef";

static char *as_hex(char *dst, unsigned long val, int pad)
{
	char *p = dst + max(pad, (int)__fls(val | 1) / 4 + 1);

	for (*p-- = '\0'; p >= dst; val >>= 4)
		*p-- = hex_asc[val & 0x0f];
	return dst;
}

#define MAX_NUMLEN 21
static char *as_dec(char *buf, unsigned long val, bool is_signed)
{
	bool negative = false;
	char *p = buf + MAX_NUMLEN;

	if (is_signed && (long)val < 0) {
		val = (val == LONG_MIN ? LONG_MIN : -(long)val);
		negative = true;
	}

	*--p = '\0';
	do {
		*--p = '0' + (val % 10);
		val /= 10;
	} while (val);

	if (negative)
		*--p = '-';
	return p;
}

static ssize_t strpad(char *dst, size_t dst_size, const char *src,
		      int _pad, bool zero_pad, bool decimal)
{
	ssize_t len = strlen(src), pad = _pad;
	char *p = dst;

	if (max(len, abs(pad)) >= dst_size)
		return -E2BIG;

	if (pad > len) {
		if (decimal && zero_pad && *src == '-') {
			*p++ = '-';
			src++;
			len--;
			pad--;
		}
		memset(p, zero_pad ? '0' : ' ', pad - len);
		p += pad - len;
	}
	memcpy(p, src, len);
	p += len;
	if (pad < 0 && -pad > len) {
		memset(p, ' ', -pad - len);
		p += -pad - len;
	}
	*p = '\0';
	return p - dst;
}

static char *symstart(char *p)
{
	while (*p)
		p--;
	return p + 1;
}

static noinline char *findsym(unsigned long ip, unsigned short *off, unsigned short *len)
{
	/* symbol entries are in a form "10000 c4 startup\0" */
	char *a = _decompressor_syms_start;
	char *b = _decompressor_syms_end;
	unsigned long start;
	unsigned long size;
	char *pivot;
	char *endp;

	while (a < b) {
		pivot = symstart(a + (b - a) / 2);
		start = simple_strtoull(pivot, &endp, 16);
		size = simple_strtoull(endp + 1, &endp, 16);
		if (ip < start) {
			b = pivot;
			continue;
		}
		if (ip > start + size) {
			a = pivot + strlen(pivot) + 1;
			continue;
		}
		*off = ip - start;
		*len = size;
		return endp + 1;
	}
	return NULL;
}

#define MAX_SYMLEN 64
static noinline char *strsym(char *buf, void *ip)
{
	unsigned short off;
	unsigned short len;
	char *p;

	p = findsym((unsigned long)ip, &off, &len);
	if (p) {
		strncpy(buf, p, MAX_SYMLEN);
		/* reserve 15 bytes for offset/len in symbol+0x1234/0x1234 */
		p = buf + strnlen(buf, MAX_SYMLEN - 15);
		strcpy(p, "+0x");
		as_hex(p + 3, off, 0);
		strcat(p, "/0x");
		as_hex(p + strlen(p), len, 0);
	} else {
		as_hex(buf, (unsigned long)ip, 16);
	}
	return buf;
}

static inline int printk_loglevel(const char *buf)
{
	if (buf[0] == KERN_SOH_ASCII && buf[1]) {
		switch (buf[1]) {
		case '0' ... '7':
			return buf[1] - '0';
		}
	}
	return MESSAGE_LOGLEVEL_DEFAULT;
}

static void boot_console_earlyprintk(const char *buf)
{
	int level = printk_loglevel(buf);

	/* always print emergency messages */
	if (level > LOGLEVEL_EMERG && !boot_earlyprintk)
		return;
	buf = printk_skip_level(buf);
	/* print debug messages only when bootdebug is enabled */
	if (level == LOGLEVEL_DEBUG && (!bootdebug || !bootdebug_filter_match(skip_timestamp(buf))))
		return;
	if (boot_ignore_loglevel || level < boot_console_loglevel)
		sclp_early_printk(buf);
}

static char *add_timestamp(char *buf)
{
#ifdef CONFIG_PRINTK_TIME
	union tod_clock *boot_clock = (union tod_clock *)&get_lowcore()->boot_clock;
	unsigned long ns = tod_to_ns(get_tod_clock() - boot_clock->tod);
	char ts[MAX_NUMLEN];

	*buf++ = '[';
	buf += strpad(buf, MAX_NUMLEN, as_dec(ts, ns / NSEC_PER_SEC, 0), 5, 0, 0);
	*buf++ = '.';
	buf += strpad(buf, MAX_NUMLEN, as_dec(ts, (ns % NSEC_PER_SEC) / NSEC_PER_USEC, 0), 6, 1, 0);
	*buf++ = ']';
	*buf++ = ' ';
#endif
	return buf;
}

#define va_arg_len_type(args, lenmod, typemod)				\
	((lenmod == 'l') ? va_arg(args, typemod long) :			\
	 (lenmod == 'h') ? (typemod short)va_arg(args, typemod int) :	\
	 (lenmod == 'H') ? (typemod char)va_arg(args, typemod int) :	\
	 (lenmod == 'z') ? va_arg(args, typemod long) :			\
			   va_arg(args, typemod int))

int boot_printk(const char *fmt, ...)
{
	char buf[1024] = { 0 };
	char *end = buf + sizeof(buf) - 1; /* make sure buf is 0 terminated */
	bool zero_pad, decimal;
	char *strval, *p = buf;
	char valbuf[MAX(MAX_SYMLEN, MAX_NUMLEN)];
	va_list args;
	char lenmod;
	ssize_t len;
	int pad;

	*p++ = KERN_SOH_ASCII;
	*p++ = printk_get_level(fmt) ?: '0' + MESSAGE_LOGLEVEL_DEFAULT;
	p = add_timestamp(p);
	fmt = printk_skip_level(fmt);

	va_start(args, fmt);
	for (; p < end && *fmt; fmt++) {
		if (*fmt != '%') {
			*p++ = *fmt;
			continue;
		}
		if (*++fmt == '%') {
			*p++ = '%';
			continue;
		}
		zero_pad = (*fmt == '0');
		pad = simple_strtol(fmt, (char **)&fmt, 10);
		lenmod = (*fmt == 'h' || *fmt == 'l' || *fmt == 'z') ? *fmt++ : 0;
		if (lenmod == 'h' && *fmt == 'h') {
			lenmod = 'H';
			fmt++;
		}
		decimal = false;
		switch (*fmt) {
		case 's':
			if (lenmod)
				goto out;
			strval = va_arg(args, char *);
			zero_pad = false;
			break;
		case 'p':
			if (*++fmt != 'S' || lenmod)
				goto out;
			strval = strsym(valbuf, va_arg(args, void *));
			zero_pad = false;
			break;
		case 'd':
		case 'i':
			strval = as_dec(valbuf, va_arg_len_type(args, lenmod, signed), 1);
			decimal = true;
			break;
		case 'u':
			strval = as_dec(valbuf, va_arg_len_type(args, lenmod, unsigned), 0);
			break;
		case 'x':
			strval = as_hex(valbuf, va_arg_len_type(args, lenmod, unsigned), 0);
			break;
		default:
			goto out;
		}
		len = strpad(p, end - p, strval, pad, zero_pad, decimal);
		if (len == -E2BIG)
			break;
		p += len;
	}
out:
	va_end(args);
	len = strlen(buf);
	if (len) {
		boot_rb_add(buf, len);
		boot_console_earlyprintk(buf);
	}
	return len;
}
