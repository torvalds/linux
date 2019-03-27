/*
 * Copyright (C) 2004, 2007  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 2000-2002  Internet Software Consortium.
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

/* $Id: os.c,v 1.8 2007/06/19 23:47:19 tbox Exp $ */

#include <windows.h>

#include <isc/os.h>

static BOOL bInit = FALSE;
static SYSTEM_INFO SystemInfo;

static void
initialize_action(void) {
	if (bInit)
		return;
	
	GetSystemInfo(&SystemInfo);
	bInit = TRUE;
}

unsigned int
isc_os_ncpus(void) {
	long ncpus = 1;
	initialize_action();
	ncpus = SystemInfo.dwNumberOfProcessors;
	if (ncpus <= 0)
		ncpus = 1;

	return ((unsigned int)ncpus);
}
