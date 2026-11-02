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

/**
 * efi_printk() - Print a kernel message
 * @fmt:	format string
 *
 * The first letter of the format string is used to determine the logging level
 * of the message. If the level is less then the current EFI logging level, the
 * message is suppressed. The message will be truncated to 255 characters
 * (ignoring surrogates).
 *
 * Return:	number of printed characters
 */
int efi_printk(const char *fmt, ...)
{
	efi_char16_t printf_buf[256];
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
		efi_char16_puts(L"EFI stub: ");

	fmt = printk_skip_level(fmt);

	va_start(args, fmt);
	printed = efi_vsnprintf(printf_buf, ARRAY_SIZE(printf_buf), fmt, args,
				true);
	va_end(args);

	efi_char16_puts(printf_buf);
	if (printed >= ARRAY_SIZE(printf_buf)) {
		efi_char16_puts(L"[Message truncated]\r\n");
		return -1;
	}

	return printed;
}
