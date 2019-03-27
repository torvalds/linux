/*
 * Copyright (c) 1994, 1995, 1996
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

#ifndef portability_h
#define	portability_h

/*
 * Helpers for portability between Windows and UN*X and between different
 * flavors of UN*X.
 */

#include "pcap/funcattrs.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef HAVE_STRLCPY
 /*
  * Macro that does the same thing as strlcpy().
  */
 #if defined(_MSC_VER) || defined(__MINGW32__)
  /*
   * strncpy_s() is supported at least back to Visual
   * Studio 2005.
   */
  #define strlcpy(x, y, z) \
	strncpy_s((x), (z), (y), _TRUNCATE)

 #else
  #define strlcpy(x, y, z) \
	(strncpy((x), (y), (z)), \
	 ((z) <= 0 ? 0 : ((x)[(z) - 1] = '\0')), \
	 (void) strlen((y)))
 #endif
#endif

#ifndef HAVE_STRLCAT
 /*
  * Macro that does the same thing as strlcat().
  */
 #if defined(_MSC_VER) || defined(__MINGW32__)
  /*
   * strncat_s() is supported at least back to Visual
   * Studio 2005.
   */
  #define strlcat(x, y, z) \
	strncat_s((x), (z), (y), _TRUNCATE)
 #else
  /*
   * ANSI C says strncat() always null-terminates its first argument,
   * so 1) we don't need to explicitly null-terminate the string
   * ourselves and 2) we need to leave room for the null terminator.
   */
  #define strlcat(x, y, z) \
	strncat((x), (y), (z) - strlen((x)) - 1)
 #endif
#endif

#ifdef _MSC_VER
  /*
   * If <crtdbg.h> has been included, and _DEBUG is defined, and
   * __STDC__ is zero, <crtdbg.h> will define strdup() to call
   * _strdup_dbg().  So if it's already defined, don't redefine
   * it.
   */
  #ifndef strdup
  #define strdup	_strdup
  #endif
#endif

/*
 * On Windows, snprintf(), with that name and with C99 behavior - i.e.,
 * guaranteeing that the formatted string is null-terminated - didn't
 * appear until Visual Studio 2015.  Prior to that, the C runtime had
 * only _snprintf(), which *doesn't* guarantee that the string is
 * null-terminated if it is truncated due to the buffer being too
 * small.  We therefore can't just define snprintf to be _snprintf
 * and define vsnprintf to be _vsnprintf, as we're relying on null-
 * termination of strings in all cases.
 *
 * We also want to allow this to be built with versions of Visual Studio
 * prior to VS 2015, so we can't rely on snprintf() being present.
 *
 * And we want to make sure that, if we support plugins in the future,
 * a routine with C99 snprintf() behavior will be available to them.
 * We also don't want it to collide with the C library snprintf() if
 * there is one.
 *
 * So we make pcap_snprintf() and pcap_vsnprintf() available, either by
 * #defining them to be snprintf or vsnprintf, respectively, or by
 * defining our own versions and exporting them.
 */
#ifdef HAVE_SNPRINTF
#define pcap_snprintf snprintf
#else
extern int pcap_snprintf(char *, size_t, PCAP_FORMAT_STRING(const char *), ...)
    PCAP_PRINTFLIKE(3, 4);
#endif

#ifdef HAVE_VSNPRINTF
#define pcap_vsnprintf vsnprintf
#else
extern int pcap_vsnprintf(char *, size_t, const char *, va_list ap);
#endif

#ifdef HAVE_STRTOK_R
  #define pcap_strtok_r	strtok_r
#else
  #ifdef _WIN32
    /*
     * Microsoft gives it a different name.
     */
    #define pcap_strtok_r	strtok_s
  #else
    /*
     * Define it ourselves.
     */
    #define NEED_STRTOK_R
    extern char *pcap_strtok_r(char *, const char *, char **);
  #endif
#endif /* HAVE_STRTOK_R */

#ifdef _WIN32
  #if !defined(__cplusplus)
    #define inline __inline
  #endif
#endif /* _WIN32 */

#ifdef __cplusplus
}
#endif

#endif
