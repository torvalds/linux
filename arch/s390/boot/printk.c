// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/stdarg.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <asm/stacktrace.h>
#include <asm/boot_data.h>
#include <asm/lowcore.h>
#include <asm/setup.h>
#include <asm/sclp.h>
#include <asm/uv.h>
#include "boot.h"

const char hex_asc[] = "0123456789abcdef";

static char *as_hex(char *dst, unsigned long val, int pad)
{
	char *p, *end = p = dst + max(pad, (int)__fls(val | 1) / 4 + 1);

	for (*p-- = 0; p >= dst; val >>= 4)
		*p-- = hex_asc[val & 0x0f];
	return end;
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

static noinline char *strsym(void *ip)
{
	static char buf[64];
	unsigned short off;
	unsigned short len;
	char *p;

	p = findsym((unsigned long)ip, &off, &len);
	if (p) {
		strncpy(buf, p, sizeof(buf));
		/* reserve 15 bytes for offset/len in symbol+0x1234/0x1234 */
		p = buf + strnlen(buf, sizeof(buf) - 15);
		strcpy(p, "+0x");
		p = as_hex(p + 3, off, 0);
		strcpy(p, "/0x");
		as_hex(p + 3, len, 0);
	} else {
		as_hex(buf, (unsigned long)ip, 16);
	}
	return buf;
}

void boot_printk(const char *fmt, ...)
{
	char buf[1024] = { 0 };
	char *end = buf + sizeof(buf) - 1; /* make sure buf is 0 terminated */
	unsigned long pad;
	char *p = buf;
	va_list args;

	va_start(args, fmt);
	for (; p < end && *fmt; fmt++) {
		if (*fmt != '%') {
			*p++ = *fmt;
			continue;
		}
		pad = isdigit(*++fmt) ? simple_strtol(fmt, (char **)&fmt, 10) : 0;
		switch (*fmt) {
		case 's':
			p = buf + strlcat(buf, va_arg(args, char *), sizeof(buf));
			break;
		case 'p':
			if (*++fmt != 'S')
				goto out;
			p = buf + strlcat(buf, strsym(va_arg(args, void *)), sizeof(buf));
			break;
		case 'l':
			if (*++fmt != 'x' || end - p <= max(sizeof(long) * 2, pad))
				goto out;
			p = as_hex(p, va_arg(args, unsigned long), pad);
			break;
		case 'x':
			if (end - p <= max(sizeof(int) * 2, pad))
				goto out;
			p = as_hex(p, va_arg(args, unsigned int), pad);
			break;
		default:
			goto out;
		}
	}
out:
	va_end(args);
	sclp_early_printk(buf);
}
