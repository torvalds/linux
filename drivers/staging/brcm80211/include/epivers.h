/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _epivers_h_
#define _epivers_h_

#define	EPI_MAJOR_VERSION	5

#define	EPI_MINOR_VERSION	75

#define	EPI_RC_NUMBER		11

#define	EPI_INCREMENTAL_NUMBER	0

#define EPI_BUILD_NUMBER	1

#define	EPI_VERSION		{ 5, 75, 11, 0 }

#ifdef BCMSDIO
/* EPI_VERSION_NUM must match FW version */
#define	EPI_VERSION_NUM		0x054b0c00
#else
#define	EPI_VERSION_NUM		0x054b0b00
#endif

#define EPI_VERSION_DEV		5.75.11

/* Driver Version String, ASCII, 32 chars max */
#define	EPI_VERSION_STR		"5.75.11"

#endif				/* _epivers_h_ */
