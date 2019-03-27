/*
 * Copyright (c) 2002 - 2005 NetGroup, Politecnico di Torino (Italy)
 * Copyright (c) 2005 - 2009 CACE Technologies, Inc. Davis (California)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Politecnico di Torino nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef pcap_pcap_inttypes_h
#define pcap_pcap_inttypes_h

/*
 * Get the integer types and PRi[doux]64 values from C99 <inttypes.h>
 * defined, by hook or by crook.
 */
#if defined(_MSC_VER)
  /*
   * Compiler is MSVC.
   */
  #if _MSC_VER >= 1800
    /*
     * VS 2013 or newer; we have <inttypes.h>.
     */
    #include <inttypes.h>
  #else
    /*
     * Earlier VS; we have to define this stuff ourselves.
     */
    typedef unsigned char uint8_t;
    typedef signed char int8_t;
    typedef unsigned short uint16_t;
    typedef signed short int16_t;
    typedef unsigned int uint32_t;
    typedef signed int int32_t;
    #ifdef _MSC_EXTENSIONS
      typedef unsigned _int64 uint64_t;
      typedef _int64 int64_t;
    #else /* _MSC_EXTENSIONS */
      typedef unsigned long long uint64_t;
      typedef long long int64_t;
    #endif
  #endif

  /*
   * These may be defined by <inttypes.h>.
   *
   * XXX - for MSVC, we always want the _MSC_EXTENSIONS versions.
   * What about other compilers?  If, as the MinGW Web site says MinGW
   * does, the other compilers just use Microsoft's run-time library,
   * then they should probably use the _MSC_EXTENSIONS even if the
   * compiler doesn't define _MSC_EXTENSIONS.
   *
   * XXX - we currently aren't using any of these, but this allows
   * their use in the future.
   */
  #ifndef PRId64
    #ifdef _MSC_EXTENSIONS
      #define PRId64	"I64d"
    #else
      #define PRId64	"lld"
    #endif
  #endif /* PRId64 */

  #ifndef PRIo64
    #ifdef _MSC_EXTENSIONS
      #define PRIo64	"I64o"
    #else
      #define PRIo64	"llo"
    #endif
  #endif /* PRIo64 */

  #ifndef PRIx64
    #ifdef _MSC_EXTENSIONS
      #define PRIx64	"I64x"
    #else
      #define PRIx64	"llx"
    #endif
  #endif

  #ifndef PRIu64
    #ifdef _MSC_EXTENSIONS
      #define PRIu64	"I64u"
    #else
      #define PRIu64	"llu"
    #endif
  #endif
#elif defined(__MINGW32__) || !defined(_WIN32)
  /*
   * Compiler is MinGW or target is UN*X or MS-DOS.  Just use
   * <inttypes.h>.
   */
  #include <inttypes.h>
#endif

#endif /* pcap/pcap-inttypes.h */
