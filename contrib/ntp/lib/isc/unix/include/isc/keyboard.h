/*
 * Copyright (C) 2004, 2005, 2007  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 2000, 2001  Internet Software Consortium.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id: keyboard.h,v 1.11 2007/06/19 23:47:19 tbox Exp $ */

#ifndef ISC_KEYBOARD_H
#define ISC_KEYBOARD_H 1

/*! \file */

#include <termios.h>

#include <isc/lang.h>
#include <isc/result.h>

ISC_LANG_BEGINDECLS

typedef struct {
	int fd;
	struct termios saved_mode;
	isc_result_t result;
} isc_keyboard_t;

isc_result_t
isc_keyboard_open(isc_keyboard_t *keyboard);

isc_result_t
isc_keyboard_close(isc_keyboard_t *keyboard, unsigned int sleepseconds);

isc_result_t
isc_keyboard_getchar(isc_keyboard_t *keyboard, unsigned char *cp);

isc_boolean_t
isc_keyboard_canceled(isc_keyboard_t *keyboard);

ISC_LANG_ENDDECLS

#endif /* ISC_KEYBOARD_H */
