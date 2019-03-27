/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef APR_LIB_H
#define APR_LIB_H

/**
 * @file apr_lib.h
 * This is collection of oddballs that didn't fit anywhere else,
 * and might move to more appropriate headers with the release
 * of APR 1.0.
 * @brief APR general purpose library routines
 */

#include "apr.h"
#include "apr_errno.h"

#if APR_HAVE_CTYPE_H
#include <ctype.h>
#endif
#if APR_HAVE_STDARG_H
#include <stdarg.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @defgroup apr_lib General Purpose Library Routines
 * @ingroup APR 
 * This is collection of oddballs that didn't fit anywhere else,
 * and might move to more appropriate headers with the release
 * of APR 1.0.
 * @{
 */

/** A constant representing a 'large' string. */
#define HUGE_STRING_LEN 8192

/*
 * Define the structures used by the APR general-purpose library.
 */

/** @see apr_vformatter_buff_t */
typedef struct apr_vformatter_buff_t apr_vformatter_buff_t;

/**
 * Structure used by the variable-formatter routines.
 */
struct apr_vformatter_buff_t {
    /** The current position */
    char *curpos;
    /** The end position of the format string */
    char *endpos;
};

/**
 * return the final element of the pathname
 * @param pathname The path to get the final element of
 * @return the final element of the path
 * @remark
 * <PRE>
 * For example:
 *                 "/foo/bar/gum"    -> "gum"
 *                 "/foo/bar/gum/"   -> ""
 *                 "gum"             -> "gum"
 *                 "bs\\path\\stuff" -> "stuff"
 * </PRE>
 */
APR_DECLARE(const char *) apr_filepath_name_get(const char *pathname);

/**
 * apr_killpg
 * Small utility macros to make things easier to read.  Not usually a
 * goal, to be sure..
 */

#ifdef WIN32
#define apr_killpg(x, y)
#else /* WIN32 */
#ifdef NO_KILLPG
#define apr_killpg(x, y)        (kill (-(x), (y)))
#else /* NO_KILLPG */
#define apr_killpg(x, y)        (killpg ((x), (y)))
#endif /* NO_KILLPG */
#endif /* WIN32 */

/**
 * apr_vformatter() is a generic printf-style formatting routine
 * with some extensions.
 * @param flush_func The function to call when the buffer is full
 * @param c The buffer to write to
 * @param fmt The format string
 * @param ap The arguments to use to fill out the format string.
 *
 * @remark
 * <PRE>
 * The extensions are:
 *
 * - %%pA takes a struct in_addr *, and prints it as a.b.c.d
 * - %%pI takes an apr_sockaddr_t * and prints it as a.b.c.d:port or
 * \[ipv6-address\]:port
 * - %%pT takes an apr_os_thread_t * and prints it in decimal
 * ('0' is printed if !APR_HAS_THREADS)
 * - %%pt takes an apr_os_thread_t * and prints it in hexadecimal
 * ('0' is printed if !APR_HAS_THREADS)
 * - %%pm takes an apr_status_t * and prints the appropriate error
 * string (from apr_strerror) corresponding to that error code.
 * - %%pp takes a void * and outputs it in hex
 * - %%pB takes a apr_uint32_t * as bytes and outputs it's apr_strfsize
 * - %%pF same as above, but takes a apr_off_t *
 * - %%pS same as above, but takes a apr_size_t *
 *
 * %%pA, %%pI, %%pT, %%pp are available from APR 1.0.0 onwards (and in 0.9.x).
 * %%pt is only available from APR 1.2.0 onwards.
 * %%pm, %%pB, %%pF and %%pS are only available from APR 1.3.0 onwards.
 *
 * The %%p hacks are to force gcc's printf warning code to skip
 * over a pointer argument without complaining.  This does
 * mean that the ANSI-style %%p (output a void * in hex format) won't
 * work as expected at all, but that seems to be a fair trade-off
 * for the increased robustness of having printf-warnings work.
 *
 * Additionally, apr_vformatter allows for arbitrary output methods
 * using the apr_vformatter_buff and flush_func.
 *
 * The apr_vformatter_buff has two elements curpos and endpos.
 * curpos is where apr_vformatter will write the next byte of output.
 * It proceeds writing output to curpos, and updating curpos, until
 * either the end of output is reached, or curpos == endpos (i.e. the
 * buffer is full).
 *
 * If the end of output is reached, apr_vformatter returns the
 * number of bytes written.
 *
 * When the buffer is full, the flush_func is called.  The flush_func
 * can return -1 to indicate that no further output should be attempted,
 * and apr_vformatter will return immediately with -1.  Otherwise
 * the flush_func should flush the buffer in whatever manner is
 * appropriate, re apr_pool_t nitialize curpos and endpos, and return 0.
 *
 * Note that flush_func is only invoked as a result of attempting to
 * write another byte at curpos when curpos >= endpos.  So for
 * example, it's possible when the output exactly matches the buffer
 * space available that curpos == endpos will be true when
 * apr_vformatter returns.
 *
 * apr_vformatter does not call out to any other code, it is entirely
 * self-contained.  This allows the callers to do things which are
 * otherwise "unsafe".  For example, apr_psprintf uses the "scratch"
 * space at the unallocated end of a block, and doesn't actually
 * complete the allocation until apr_vformatter returns.  apr_psprintf
 * would be completely broken if apr_vformatter were to call anything
 * that used this same pool.  Similarly http_bprintf() uses the "scratch"
 * space at the end of its output buffer, and doesn't actually note
 * that the space is in use until it either has to flush the buffer
 * or until apr_vformatter returns.
 * </PRE>
 */
APR_DECLARE(int) apr_vformatter(int (*flush_func)(apr_vformatter_buff_t *b),
			        apr_vformatter_buff_t *c, const char *fmt,
			        va_list ap);

/**
 * Display a prompt and read in the password from stdin.
 * @param prompt The prompt to display
 * @param pwbuf Buffer to store the password
 * @param bufsize The length of the password buffer.
 * @remark If the password entered must be truncated to fit in
 * the provided buffer, APR_ENAMETOOLONG will be returned.
 * Note that the bufsize paramater is passed by reference for no
 * reason; its value will never be modified by the apr_password_get()
 * function.
 */
APR_DECLARE(apr_status_t) apr_password_get(const char *prompt, char *pwbuf, 
                                           apr_size_t *bufsize);

/** @} */

/**
 * @defgroup apr_ctype ctype functions
 * These macros allow correct support of 8-bit characters on systems which
 * support 8-bit characters.  Pretty dumb how the cast is required, but
 * that's legacy libc for ya.  These new macros do not support EOF like
 * the standard macros do.  Tough.
 * @{
 */
/** @see isalnum */
#define apr_isalnum(c) (isalnum(((unsigned char)(c))))
/** @see isalpha */
#define apr_isalpha(c) (isalpha(((unsigned char)(c))))
/** @see iscntrl */
#define apr_iscntrl(c) (iscntrl(((unsigned char)(c))))
/** @see isdigit */
#define apr_isdigit(c) (isdigit(((unsigned char)(c))))
/** @see isgraph */
#define apr_isgraph(c) (isgraph(((unsigned char)(c))))
/** @see islower*/
#define apr_islower(c) (islower(((unsigned char)(c))))
/** @see isascii */
#ifdef isascii
#define apr_isascii(c) (isascii(((unsigned char)(c))))
#else
#define apr_isascii(c) (((c) & ~0x7f)==0)
#endif
/** @see isprint */
#define apr_isprint(c) (isprint(((unsigned char)(c))))
/** @see ispunct */
#define apr_ispunct(c) (ispunct(((unsigned char)(c))))
/** @see isspace */
#define apr_isspace(c) (isspace(((unsigned char)(c))))
/** @see isupper */
#define apr_isupper(c) (isupper(((unsigned char)(c))))
/** @see isxdigit */
#define apr_isxdigit(c) (isxdigit(((unsigned char)(c))))
/** @see tolower */
#define apr_tolower(c) (tolower(((unsigned char)(c))))
/** @see toupper */
#define apr_toupper(c) (toupper(((unsigned char)(c))))

/** @} */

#ifdef __cplusplus
}
#endif

#endif	/* ! APR_LIB_H */
