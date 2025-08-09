// SPDX-License-Identifier: GPL-2.0

#include <linux/stdarg.h>

#include <linux/ctype.h>
#include <linux/efi.h>
#include <linux/kernel.h>
#include <linux/kern_levels.h>
#include <asm/efi.h>
#include <asm/setup.h>

#include "efistub.h"

int efi_loglevel = LOGLEVEL_NOTICE;

/**
 * efi_char16_puts() - Write a UCS-2 encoded string to the console
 * @str:	UCS-2 encoded string
 */
void efi_char16_puts(efi_char16_t *str)
{
	efi_call_proto(efi_table_attr(efi_system_table, con_out),
		       output_string, str);
}

static
u32 utf8_to_utf32(const u8 **s8)
{
	u32 c32;
	u8 c0, cx;
	size_t clen, i;

	c0 = cx = *(*s8)++;
	/*
	 * The position of the most-significant 0 bit gives us the length of
	 * a multi-octet encoding.
	 */
	for (clen = 0; cx & 0x80; ++clen)
		cx <<= 1;
	/*
	 * If the 0 bit is in position 8, this is a valid single-octet
	 * encoding. If the 0 bit is in position 7 or positions 1-3, the
	 * encoding is invalid.
	 * In either case, we just return the first octet.
	 */
	if (clen < 2 || clen > 4)
		return c0;
	/* Get the bits from the first octet. */
	c32 = cx >> clen--;
	for (i = 0; i < clen; ++i) {
		/* Trailing octets must have 10 in most significant bits. */
		cx = (*s8)[i] ^ 0x80;
		if (cx & 0xc0)
			return c0;
		c32 = (c32 << 6) | cx;
	}
	/*
	 * Check for validity:
	 * - The character must be in the Unicode range.
	 * - It must not be a surrogate.
	 * - It must be encoded using the correct number of octets.
	 */
	if (c32 > 0x10ffff ||
	    (c32 & 0xf800) == 0xd800 ||
	    clen != (c32 >= 0x80) + (c32 >= 0x800) + (c32 >= 0x10000))
		return c0;
	*s8 += clen;
	return c32;
}

/**
 * efi_puts() - Write a UTF-8 encoded string to the console
 * @str:	UTF-8 encoded string
 */
void efi_puts(const char *str)
{
	efi_char16_t buf[128];
	size_t pos = 0, lim = ARRAY_SIZE(buf);
	const u8 *s8 = (const u8 *)str;
	u32 c32;

	while (*s8) {
		if (*s8 == '\n')
			buf[pos++] = L'\r';
		c32 = utf8_to_utf32(&s8);
		if (c32 < 0x10000) {
			/* Characters in plane 0 use a single word. */
			buf[pos++] = c32;
		} else {
			/*
			 * Characters in other planes encode into a surrogate
			 * pair.
			 */
			buf[pos++] = (0xd800 - (0x10000 >> 10)) + (c32 >> 10);
			buf[pos++] = 0xdc00 + (c32 & 0x3ff);
		}
		if (*s8 == '\0' || pos >= lim - 2) {
			buf[pos] = L'\0';
			efi_char16_puts(buf);
			pos = 0;
		}
	}
}

/**
 * efi_printk() - Print a kernel message
 * @fmt:	format string
 *
 * The first letter of the format string is used to determine the logging level
 * of the message. If the level is less then the current EFI logging level, the
 * message is suppressed. The message will be truncated to 255 bytes.
 *
 * Return:	number of printed characters
 */
int efi_printk(const char *fmt, ...)
{
	char printf_buf[256];
	va_list args;
	int printed;
	int loglevel = printk_get_level(fmt);

	switch (loglevel) {
	case '0' ... '9':
		loglevel -= '0';
		break;
	default:
		/*
		 * Use loglevel -1 for cases where we just want to print to
		 * the screen.
		 */
		loglevel = -1;
		break;
	}

	if (loglevel >= efi_loglevel)
		return 0;

	if (loglevel >= 0)
		efi_puts("EFI stub: ");

	fmt = printk_skip_level(fmt);

	va_start(args, fmt);
	printed = vsnprintf(printf_buf, sizeof(printf_buf), fmt, args);
	va_end(args);

	efi_puts(printf_buf);
	if (printed >= sizeof(printf_buf)) {
		efi_puts("[Message truncated]\n");
		return -1;
	}

	return printed;
}
