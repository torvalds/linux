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

#ifndef ftmacros_h
#define	ftmacros_h

/*
 * Define some feature test macros to make sure that everything we want
 * to be declared gets declared.
 *
 * On some UN*Xes we need to force strtok_r() to be declared.
 * We do *NOT* want to define _POSIX_C_SOURCE, as that tends
 * to make non-POSIX APIs that we use unavailable.
 * XXX - is there no portable way to say "please pollute the
 * namespace to the maximum extent possible"?
 */
#if defined(sun) || defined(__sun)
  #define __EXTENSIONS__

  /*
   * We also need to define _XPG4_2 in order to get
   * the Single UNIX Specification version of
   * recvmsg().
   */
  #define _XPG4_2
#elif defined(_hpux) || defined(hpux) || defined(__hpux)
  #define _REENTRANT

  /*
   * We need this to get the versions of socket functions that
   * use socklen_t.  Define it only if it's not already defined,
   * so we don't get redefiniton warnings.
   */
  #ifndef _XOPEN_SOURCE_EXTENDED
    #define _XOPEN_SOURCE_EXTENDED
  #endif

  /*
   * XXX - the list of PA-RISC options for GCC makes it sound as if
   * building code that uses a particular vintage of UNIX API/ABI
   * is complicated:
   *
   *    https://gcc.gnu.org/onlinedocs/gcc/HPPA-Options.html
   *
   * See the description of the -munix flag.
   *
   * We probably want libpcap to work with programs built for any
   * UN*X standard.  I'm not sure whether that's possible and, if
   * it is, what sort of stuff it'd have to do.
   *
   * It might also be a requirement that we build with a special
   * flag to allow the library to be used with threaded code, at
   * least with HP's C compiler; hopefully doing so won't make it
   * *not* work with *un*-threaded code.
   */
#elif defined(__linux__) || defined(linux) || defined(__linux)
  /*
   * We can't turn _GNU_SOURCE on because some versions of GNU Libc
   * will give the GNU version of strerror_r(), which returns a
   * string pointer and doesn't necessarily fill in the buffer,
   * rather than the standard version of strerror_r(), which
   * returns 0 or an errno and always fills in the buffer.  We
   * require both of the latter behaviors.
   *
   * So we try turning everything else on that we can.  This includes
   * defining _XOPEN_SOURCE as 600, because we want to force crypt()
   * to be declared on systems that use GNU libc, such as most Linux
   * distributions.
   */
  #define _POSIX_C_SOURCE 200809L
  #define _XOPEN_SOURCE 600

  /*
   * We turn on both _DEFAULT_SOURCE and _BSD_SOURCE to try to get
   * the BSD u_XXX types, such as u_int and u_short, defined.  We
   * define _DEFAULT_SOURCE first, so that newer versions of GNU libc
   * don't whine about _BSD_SOURCE being deprecated; we still have
   * to define _BSD_SOURCE to handle older versions of GNU libc that
   * don't support _DEFAULT_SOURCE.
   */
  #define _DEFAULT_SOURCE
  #define _BSD_SOURCE
#endif

#endif
