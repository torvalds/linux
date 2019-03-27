/*
 * Copyright (c) 2015 The TCPDUMP project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef netdissect_timeval_operations_h
#define netdissect_timeval_operations_h

/* Operations on timevals. */

#ifndef _MICRO_PER_SEC
#define _MICRO_PER_SEC 1000000
#endif

#ifndef _NANO_PER_SEC
#define _NANO_PER_SEC 1000000000
#endif

#define netdissect_timevalclear(tvp) ((tvp)->tv_sec = (tvp)->tv_usec = 0)

#define netdissect_timevalisset(tvp) ((tvp)->tv_sec || (tvp)->tv_usec)

#define netdissect_timevalcmp(tvp, uvp, cmp)      \
	(((tvp)->tv_sec == (uvp)->tv_sec) ?    \
	 ((tvp)->tv_usec cmp (uvp)->tv_usec) : \
	 ((tvp)->tv_sec cmp (uvp)->tv_sec))

#define netdissect_timevaladd(tvp, uvp, vvp, nano_prec)           \
	do {                                                      \
		(vvp)->tv_sec = (tvp)->tv_sec + (uvp)->tv_sec;    \
		(vvp)->tv_usec = (tvp)->tv_usec + (uvp)->tv_usec; \
		if (nano_prec) {                                  \
			if ((vvp)->tv_usec >= _NANO_PER_SEC) {    \
				(vvp)->tv_sec++;                  \
				(vvp)->tv_usec -= _NANO_PER_SEC;  \
			}                                         \
		} else {                                          \
			if ((vvp)->tv_usec >= _MICRO_PER_SEC) {   \
				(vvp)->tv_sec++;                  \
				(vvp)->tv_usec -= _MICRO_PER_SEC; \
			}                                         \
		}                                                 \
	} while (0)

#define netdissect_timevalsub(tvp, uvp, vvp, nano_prec)            \
	do {                                                       \
		(vvp)->tv_sec = (tvp)->tv_sec - (uvp)->tv_sec;     \
		(vvp)->tv_usec = (tvp)->tv_usec - (uvp)->tv_usec;  \
		if ((vvp)->tv_usec < 0) {                          \
		    (vvp)->tv_sec--;                               \
		    (vvp)->tv_usec += (nano_prec ? _NANO_PER_SEC : \
				       _MICRO_PER_SEC);            \
		}                                                  \
	} while (0)

#endif /* netdissect_timeval_operations_h */
