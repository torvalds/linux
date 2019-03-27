/*
 * Copyright (c) 1993, 1994, 1995, 1996, 1997, 1998
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the Computer Systems
 *	Engineering Group at Lawrence Berkeley Laboratory.
 * 4. Neither the name of the University nor of the Laboratory may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Utilities for message formatting used both by libpcap and rpcapd.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "ftmacros.h"

#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <pcap/pcap.h>

#include "portability.h"

#include "fmtutils.h"

/*
 * Generate an error message based on a format, arguments, and an
 * errno, with a message for the errno after the formatted output.
 */
void
pcap_fmt_errmsg_for_errno(char *errbuf, size_t errbuflen, int errnum,
    const char *fmt, ...)
{
	va_list ap;
	size_t msglen;
	char *p;
	size_t errbuflen_remaining;
#if defined(HAVE_STRERROR_S)
	errno_t err;
#elif defined(HAVE_STRERROR_R)
	int err;
#endif

	va_start(ap, fmt);
	pcap_vsnprintf(errbuf, errbuflen, fmt, ap);
	va_end(ap);
	msglen = strlen(errbuf);

	/*
	 * Do we have enough space to append ": "?
	 * Including the terminating '\0', that's 3 bytes.
	 */
	if (msglen + 3 > errbuflen) {
		/* No - just give them what we've produced. */
		return;
	}
	p = errbuf + msglen;
	errbuflen_remaining = errbuflen - msglen;
	*p++ = ':';
	*p++ = ' ';
	*p = '\0';
	msglen += 2;
	errbuflen_remaining -= 2;

	/*
	 * Now append the string for the error code.
	 */
#if defined(HAVE_STRERROR_S)
	err = strerror_s(p, errbuflen_remaining, errnum);
	if (err != 0) {
		/*
		 * It doesn't appear to be documented anywhere obvious
		 * what the error returns from strerror_s().
		 */
		pcap_snprintf(p, errbuflen_remaining, "Error %d", errnum);
	}
#elif defined(HAVE_STRERROR_R)
	err = strerror_r(errnum, p, errbuflen_remaining);
	if (err == EINVAL) {
		/*
		 * UNIX 03 says this isn't guaranteed to produce a
		 * fallback error message.
		 */
		pcap_snprintf(p, errbuflen_remaining, "Unknown error: %d",
		    errnum);
	} else if (err == ERANGE) {
		/*
		 * UNIX 03 says this isn't guaranteed to produce a
		 * fallback error message.
		 */
		pcap_snprintf(p, errbuflen_remaining,
		    "Message for error %d is too long", errnum);
	}
#else
	/*
	 * We have neither strerror_s() nor strerror_r(), so we're
	 * stuck with using pcap_strerror().
	 */
	pcap_snprintf(p, errbuflen_remaining, "%s", pcap_strerror(errnum));
#endif
}
