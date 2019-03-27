/*
 * Copyright (C) 2004, 2007  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 2001  Internet Software Consortium.
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

/* $Id: bindevt.h,v 1.6 2007/06/19 23:47:20 tbox Exp $ */

#ifndef ISC_BINDEVT_H
#define ISC_BINDEVT_H 1

/*
 * This is used for the event log for both logging the messages and
 * later on by the event viewer when looking at the events
 */

/*
 * Values are 32 bit values layed out as follows:
 *
 *   3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
 *   1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 *  +---+-+-+-----------------------+-------------------------------+
 *  |Sev|C|R|     Facility          |               Code            |
 *  +---+-+-+-----------------------+-------------------------------+
 *
 *  where
 *
 *      Sev - is the severity code
 *
 *          00 - Success
 *          01 - Informational
 *          10 - Warning
 *          11 - Error
 *
 *      C - is the Customer code flag
 *
 *      R - is a reserved bit
 *
 *      Facility - is the facility code
 *
 *      Code - is the facility's status code
 *
 *
 * Define the facility codes
 */
 

/*
 * Define the severity codes
 */


/*
 * MessageId: BIND_ERR_MSG
 *
 * MessageText:
 *
 *  %1
 */
#define BIND_ERR_MSG		((DWORD)0xC0000001L)

/*
 * MessageId: BIND_WARN_MSG
 *
 * MessageText:
 *
 *  %1
 */
#define BIND_WARN_MSG		((DWORD)0x80000002L)

/*
 * MessageId: BIND_INFO_MSG
 *
 * MessageText:
 *
 *  %1
 */
#define BIND_INFO_MSG		((DWORD)0x40000003L)

#endif /* ISC_BINDEVT_H */
