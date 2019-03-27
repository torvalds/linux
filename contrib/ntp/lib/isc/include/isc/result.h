/*
 * Copyright (C) 2004-2009, 2012  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1998-2001, 2003  Internet Software Consortium.
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

/* $Id$ */

#ifndef ISC_RESULT_H
#define ISC_RESULT_H 1

/*! \file isc/result.h */

#include <isc/lang.h>
#include <isc/types.h>

#define ISC_R_SUCCESS			0	/*%< success */
#define ISC_R_NOMEMORY			1	/*%< out of memory */
#define ISC_R_TIMEDOUT			2	/*%< timed out */
#define ISC_R_NOTHREADS			3	/*%< no available threads */
#define ISC_R_ADDRNOTAVAIL		4	/*%< address not available */
#define ISC_R_ADDRINUSE			5	/*%< address in use */
#define ISC_R_NOPERM			6	/*%< permission denied */
#define ISC_R_NOCONN			7	/*%< no pending connections */
#define ISC_R_NETUNREACH		8	/*%< network unreachable */
#define ISC_R_HOSTUNREACH		9	/*%< host unreachable */
#define ISC_R_NETDOWN			10	/*%< network down */
#define ISC_R_HOSTDOWN			11	/*%< host down */
#define ISC_R_CONNREFUSED		12	/*%< connection refused */
#define ISC_R_NORESOURCES		13	/*%< not enough free resources */
#define ISC_R_EOF			14	/*%< end of file */
#define ISC_R_BOUND			15	/*%< socket already bound */
#define ISC_R_RELOAD			16	/*%< reload */
#define ISC_R_SUSPEND	      ISC_R_RELOAD	/*%< alias of 'reload' */
#define ISC_R_LOCKBUSY			17	/*%< lock busy */
#define ISC_R_EXISTS			18	/*%< already exists */
#define ISC_R_NOSPACE			19	/*%< ran out of space */
#define ISC_R_CANCELED			20	/*%< operation canceled */
#define ISC_R_NOTBOUND			21	/*%< socket is not bound */
#define ISC_R_SHUTTINGDOWN		22	/*%< shutting down */
#define ISC_R_NOTFOUND			23	/*%< not found */
#define ISC_R_UNEXPECTEDEND		24	/*%< unexpected end of input */
#define ISC_R_FAILURE			25	/*%< generic failure */
#define ISC_R_IOERROR			26	/*%< I/O error */
#define ISC_R_NOTIMPLEMENTED		27	/*%< not implemented */
#define ISC_R_UNBALANCED		28	/*%< unbalanced parentheses */
#define ISC_R_NOMORE			29	/*%< no more */
#define ISC_R_INVALIDFILE		30	/*%< invalid file */
#define ISC_R_BADBASE64			31	/*%< bad base64 encoding */
#define ISC_R_UNEXPECTEDTOKEN		32	/*%< unexpected token */
#define ISC_R_QUOTA			33	/*%< quota reached */
#define ISC_R_UNEXPECTED		34	/*%< unexpected error */
#define ISC_R_ALREADYRUNNING		35	/*%< already running */
#define ISC_R_IGNORE			36	/*%< ignore */
#define ISC_R_MASKNONCONTIG             37	/*%< addr mask not contiguous */
#define ISC_R_FILENOTFOUND		38	/*%< file not found */
#define ISC_R_FILEEXISTS		39	/*%< file already exists */
#define ISC_R_NOTCONNECTED		40	/*%< socket is not connected */
#define ISC_R_RANGE			41	/*%< out of range */
#define ISC_R_NOENTROPY			42	/*%< out of entropy */
#define ISC_R_MULTICAST			43	/*%< invalid use of multicast */
#define ISC_R_NOTFILE			44	/*%< not a file */
#define ISC_R_NOTDIRECTORY		45	/*%< not a directory */
#define ISC_R_QUEUEFULL			46	/*%< queue is full */
#define ISC_R_FAMILYMISMATCH		47	/*%< address family mismatch */
#define ISC_R_FAMILYNOSUPPORT		48	/*%< AF not supported */
#define ISC_R_BADHEX			49	/*%< bad hex encoding */
#define ISC_R_TOOMANYOPENFILES		50	/*%< too many open files */
#define ISC_R_NOTBLOCKING		51	/*%< not blocking */
#define ISC_R_UNBALANCEDQUOTES		52	/*%< unbalanced quotes */
#define ISC_R_INPROGRESS		53	/*%< operation in progress */
#define ISC_R_CONNECTIONRESET		54	/*%< connection reset */
#define ISC_R_SOFTQUOTA			55	/*%< soft quota reached */
#define ISC_R_BADNUMBER			56	/*%< not a valid number */
#define ISC_R_DISABLED			57	/*%< disabled */
#define ISC_R_MAXSIZE			58	/*%< max size */
#define ISC_R_BADADDRESSFORM		59	/*%< invalid address format */
#define ISC_R_BADBASE32			60	/*%< bad base32 encoding */
#define ISC_R_UNSET			61	/*%< unset */

/*% Not a result code: the number of results. */
#define ISC_R_NRESULTS 			62

ISC_LANG_BEGINDECLS

const char *
isc_result_totext(isc_result_t);
/*%<
 * Convert an isc_result_t into a string message describing the result.
 */

isc_result_t
isc_result_register(unsigned int base, unsigned int nresults,
		    const char **text, isc_msgcat_t *msgcat, int set);

ISC_LANG_ENDDECLS

#endif /* ISC_RESULT_H */
