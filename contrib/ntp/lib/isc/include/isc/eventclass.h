/*
 * Copyright (C) 2004, 2005, 2007  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1998-2001  Internet Software Consortium.
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

/* $Id: eventclass.h,v 1.18 2007/06/19 23:47:18 tbox Exp $ */

#ifndef ISC_EVENTCLASS_H
#define ISC_EVENTCLASS_H 1

/*! \file isc/eventclass.h
 ***** Registry of Predefined Event Type Classes
 *****/

/*%
 * An event class is an unsigned 16 bit number.  Each class may contain up
 * to 65536 events.  An event type is formed by adding the event number
 * within the class to the class number.
 *
 */

#define ISC_EVENTCLASS(eclass)		((eclass) << 16)

/*@{*/
/*!
 * Classes < 1024 are reserved for ISC use.
 * Event classes >= 1024 and <= 65535 are reserved for application use.
 */

#define	ISC_EVENTCLASS_TASK		ISC_EVENTCLASS(0)
#define	ISC_EVENTCLASS_TIMER		ISC_EVENTCLASS(1)
#define	ISC_EVENTCLASS_SOCKET		ISC_EVENTCLASS(2)
#define	ISC_EVENTCLASS_FILE		ISC_EVENTCLASS(3)
#define	ISC_EVENTCLASS_DNS		ISC_EVENTCLASS(4)
#define	ISC_EVENTCLASS_APP		ISC_EVENTCLASS(5)
#define	ISC_EVENTCLASS_OMAPI		ISC_EVENTCLASS(6)
#define	ISC_EVENTCLASS_RATELIMITER	ISC_EVENTCLASS(7)
#define	ISC_EVENTCLASS_ISCCC		ISC_EVENTCLASS(8)
/*@}*/

#endif /* ISC_EVENTCLASS_H */
