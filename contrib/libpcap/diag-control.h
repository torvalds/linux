/* -*- Mode: c; tab-width: 8; indent-tabs-mode: 1; c-basic-offset: 8; -*- */
/*
 * Copyright (c) 1993, 1994, 1995, 1996, 1997
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

#ifndef _diag_control_h
#define _diag_control_h

#include "pcap/compiler-tests.h"

#ifndef _MSC_VER
  /*
   * Clang and GCC both support this way of putting pragmas into #defines.
   * We don't use it unless we have a compiler that supports it; the
   * warning-suppressing pragmas differ between Clang and GCC, so we test
   * for both of those separately.
   */
  #define PCAP_DO_PRAGMA(x) _Pragma (#x)
#endif

/*
 * Suppress Flex warnings.
 */
#if defined(_MSC_VER)
  /*
   * This is Microsoft Visual Studio; we can use __pragma(warning(disable:XXXX))
   * and __pragma(warning(push/pop)).
   *
   * Suppress signed-vs-unsigned comparison, narrowing, and unreachable
   * code warnings.
   */
  #define DIAG_OFF_FLEX \
    __pragma(warning(push)) \
    __pragma(warning(disable:4127)) \
    __pragma(warning(disable:4242)) \
    __pragma(warning(disable:4244)) \
    __pragma(warning(disable:4702))
  #define DIAG_ON_FLEX  __pragma(warning(pop))
#elif PCAP_IS_AT_LEAST_CLANG_VERSION(2,8)
  /*
   * This is Clang 2.8 or later; we can use "clang diagnostic
   * ignored -Wxxx" and "clang diagnostic push/pop".
   *
   * Suppress -Wdocumentation warnings; GCC doesn't support -Wdocumentation,
   * at least according to the GCC 7.3 documentation.  Apparently, Flex
   * generates code that upsets at least some versions of Clang's
   * -Wdocumentation.
   */
  #define DIAG_OFF_FLEX \
    PCAP_DO_PRAGMA(clang diagnostic push) \
    PCAP_DO_PRAGMA(clang diagnostic ignored "-Wsign-compare") \
    PCAP_DO_PRAGMA(clang diagnostic ignored "-Wdocumentation") \
    PCAP_DO_PRAGMA(clang diagnostic ignored "-Wmissing-noreturn") \
    PCAP_DO_PRAGMA(clang diagnostic ignored "-Wunused-parameter") \
    PCAP_DO_PRAGMA(clang diagnostic ignored "-Wunreachable-code")
  #define DIAG_ON_FLEX \
    PCAP_DO_PRAGMA(clang diagnostic pop)
#elif PCAP_IS_AT_LEAST_GNUC_VERSION(4,6)
  /*
   * This is GCC 4.6 or later, or a compiler claiming to be that.
   * We can use "GCC diagnostic ignored -Wxxx" (introduced in 4.2)
   * and "GCC diagnostic push/pop" (introduced in 4.6).
   */
  #define DIAG_OFF_FLEX \
    PCAP_DO_PRAGMA(GCC diagnostic push) \
    PCAP_DO_PRAGMA(GCC diagnostic ignored "-Wsign-compare") \
    PCAP_DO_PRAGMA(GCC diagnostic ignored "-Wunused-parameter") \
    PCAP_DO_PRAGMA(GCC diagnostic ignored "-Wunreachable-code")
  #define DIAG_ON_FLEX \
    PCAP_DO_PRAGMA(GCC diagnostic pop)
#else
  /*
   * Neither Visual Studio, nor Clang 2.8 or later, nor GCC 4.6 or later
   * or a compiler claiming to be that; there's nothing we know of that
   * we can do.
   */
  #define DIAG_OFF_FLEX
  #define DIAG_ON_FLEX
#endif

#ifdef YYBYACC
  /*
   * Berkeley YACC.
   *
   * It generates a global declaration of yylval, or the appropriately
   * prefixed version of yylval, in grammar.h, *even though it's been
   * told to generate a pure parser, meaning it doesn't have any global
   * variables*.  Bison doesn't do this.
   *
   * That causes a warning due to the local declaration in the parser
   * shadowing the global declaration.
   *
   * So, if the compiler warns about that, we turn off -Wshadow warnings.
   */
  #if defined(_MSC_VER)
    /*
     * This is Microsoft Visual Studio; we can use
     * __pragma(warning(disable:XXXX)) and __pragma(warning(push/pop)).
     *
     * Suppress unreachable code warnings.
     */
    #define DIAG_OFF_BISON_BYACC \
      __pragma(warning(push)) \
      __pragma(warning(disable:4702))
    #define DIAG_ON_BISON_BYACC  __pragma(warning(pop))
  #elif PCAP_IS_AT_LEAST_CLANG_VERSION(2,8)
    /*
     * This is Clang 2.8 or later; we can use "clang diagnostic
     * ignored -Wxxx" and "clang diagnostic push/pop".
     */
    #define DIAG_OFF_BISON_BYACC \
      PCAP_DO_PRAGMA(clang diagnostic push) \
      PCAP_DO_PRAGMA(clang diagnostic ignored "-Wshadow") \
      PCAP_DO_PRAGMA(clang diagnostic ignored "-Wunreachable-code")
    #define DIAG_ON_BISON_BYACC \
      PCAP_DO_PRAGMA(clang diagnostic pop)
  #elif PCAP_IS_AT_LEAST_GNUC_VERSION(4,6)
    /*
     * This is GCC 4.6 or later, or a compiler claiming to be that.
     * We can use "GCC diagnostic ignored -Wxxx" (introduced in 4.2)
     * and "GCC diagnostic push/pop" (introduced in 4.6).
     */
    #define DIAG_OFF_BISON_BYACC \
      PCAP_DO_PRAGMA(GCC diagnostic push) \
      PCAP_DO_PRAGMA(GCC diagnostic ignored "-Wshadow") \
      PCAP_DO_PRAGMA(GCC diagnostic ignored "-Wunreachable-code")
    #define DIAG_ON_BISON_BYACC \
      PCAP_DO_PRAGMA(GCC diagnostic pop)
  #else
    /*
     * Neither Clang 2.8 or later nor GCC 4.6 or later or a compiler
     * claiming to be that; there's nothing we know of that we can do.
     */
    #define DIAG_OFF_BISON_BYACC
    #define DIAG_ON_BISON_BYACC
  #endif
#else
  /*
   * Bison.
   */
  #if defined(_MSC_VER)
    /*
     * This is Microsoft Visual Studio; we can use
     * __pragma(warning(disable:XXXX)) and __pragma(warning(push/pop)).
     *
     * Suppress some /Wall warnings.
     */
    #define DIAG_OFF_BISON_BYACC \
      __pragma(warning(push)) \
      __pragma(warning(disable:4127)) \
      __pragma(warning(disable:4242)) \
      __pragma(warning(disable:4244)) \
      __pragma(warning(disable:4702))
    #define DIAG_ON_BISON_BYACC  __pragma(warning(pop))
  #elif PCAP_IS_AT_LEAST_CLANG_VERSION(2,8)
    /*
     * This is Clang 2.8 or later; we can use "clang diagnostic
     * ignored -Wxxx" and "clang diagnostic push/pop".
     */
    #define DIAG_OFF_BISON_BYACC \
      PCAP_DO_PRAGMA(clang diagnostic push) \
      PCAP_DO_PRAGMA(clang diagnostic ignored "-Wunreachable-code")
    #define DIAG_ON_BISON_BYACC \
      PCAP_DO_PRAGMA(clang diagnostic pop)
  #elif PCAP_IS_AT_LEAST_GNUC_VERSION(4,6)
    /*
     * This is GCC 4.6 or later, or a compiler claiming to be that.
     * We can use "GCC diagnostic ignored -Wxxx" (introduced in 4.2)
     * and "GCC diagnostic push/pop" (introduced in 4.6).
     */
    #define DIAG_OFF_BISON_BYACC \
      PCAP_DO_PRAGMA(GCC diagnostic push) \
      PCAP_DO_PRAGMA(GCC diagnostic ignored "-Wunreachable-code")
    #define DIAG_ON_BISON_BYACC \
      PCAP_DO_PRAGMA(GCC diagnostic pop)
  #else
    /*
     * Neither Clang 2.8 or later nor GCC 4.6 or later or a compiler
     * claiming to be that; there's nothing we know of that we can do.
     */
    #define DIAG_OFF_BISON_BYACC
    #define DIAG_ON_BISON_BYACC
  #endif
#endif

#endif /* _diag_control_h */
