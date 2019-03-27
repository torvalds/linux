/*
 * Copyright (C) 2004-2007, 2009  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1999-2001  Internet Software Consortium.
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

/* $Id: resultclass.h,v 1.20 2009/09/02 23:48:03 tbox Exp $ */

#ifndef ISC_RESULTCLASS_H
#define ISC_RESULTCLASS_H 1


/*! \file isc/resultclass.h
 * \brief Registry of Predefined Result Type Classes
 *
 * A result class number is an unsigned 16 bit number.  Each class may
 * contain up to 65536 results.  A result code is formed by adding the
 * result number within the class to the class number multiplied by 65536.
 *
 * Classes < 1024 are reserved for ISC use.
 * Result classes >= 1024 and <= 65535 are reserved for application use.
 */

#define ISC_RESULTCLASS_FROMNUM(num)		((num) << 16)
#define ISC_RESULTCLASS_TONUM(rclass)		((rclass) >> 16)
#define ISC_RESULTCLASS_SIZE			65536
#define ISC_RESULTCLASS_INCLASS(rclass, result) \
	((rclass) == ((result) & 0xFFFF0000))


#define	ISC_RESULTCLASS_ISC		ISC_RESULTCLASS_FROMNUM(0)
#define	ISC_RESULTCLASS_DNS		ISC_RESULTCLASS_FROMNUM(1)
#define	ISC_RESULTCLASS_DST		ISC_RESULTCLASS_FROMNUM(2)
#define	ISC_RESULTCLASS_DNSRCODE	ISC_RESULTCLASS_FROMNUM(3)
#define	ISC_RESULTCLASS_OMAPI		ISC_RESULTCLASS_FROMNUM(4)
#define	ISC_RESULTCLASS_ISCCC		ISC_RESULTCLASS_FROMNUM(5)
#define	ISC_RESULTCLASS_DHCP		ISC_RESULTCLASS_FROMNUM(6)


#endif /* ISC_RESULTCLASS_H */
