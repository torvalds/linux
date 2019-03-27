/*	$NetBSD: vis.h,v 1.16 2005/09/13 01:44:32 christos Exp $	*/

/*-
 * Copyright (c) 1990, 1993
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
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 *
 *	@(#)vis.h	8.1 (Berkeley) 6/2/93
 */

#ifndef _VIS_H_
#define	_VIS_H_

#ifndef ROKEN_LIB_FUNCTION
#ifdef _WIN32
#define ROKEN_LIB_FUNCTION
#define ROKEN_LIB_CALL     __cdecl
#else
#define ROKEN_LIB_FUNCTION
#define ROKEN_LIB_CALL
#endif
#endif

#include <sys/types.h>

#include <roken.h>

/*
 * to select alternate encoding format
 */
#define	VIS_OCTAL	0x01	/* use octal \ddd format */
#define	VIS_CSTYLE	0x02	/* use \[nrft0..] where appropiate */

/*
 * to alter set of characters encoded (default is to encode all
 * non-graphic except space, tab, and newline).
 */
#define	VIS_SP		0x04	/* also encode space */
#define	VIS_TAB		0x08	/* also encode tab */
#define	VIS_NL		0x10	/* also encode newline */
#define	VIS_WHITE	(VIS_SP | VIS_TAB | VIS_NL)
#define	VIS_SAFE	0x20	/* only encode "unsafe" characters */

/*
 * other
 */
#define	VIS_NOSLASH	0x40	/* inhibit printing '\' */
#define	VIS_HTTPSTYLE	0x80	/* http-style escape % HEX HEX */

/*
 * unvis return codes
 */
#define	UNVIS_VALID	 1	/* character valid */
#define	UNVIS_VALIDPUSH	 2	/* character valid, push back passed char */
#define	UNVIS_NOCHAR	 3	/* valid sequence, no character produced */
#define	UNVIS_SYNBAD	-1	/* unrecognized escape sequence */
#define	UNVIS_ERROR	-2	/* decoder in unknown state (unrecoverable) */

/*
 * unvis flags
 */
#define	UNVIS_END	1	/* no more characters */

ROKEN_CPP_START

ROKEN_LIB_FUNCTION char * ROKEN_LIB_CALL
	rk_vis(char *, int, int, int);
ROKEN_LIB_FUNCTION char * ROKEN_LIB_CALL
	rk_svis(char *, int, int, int, const char *);
ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
	rk_strvis(char *, const char *, int);
ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
	rk_strsvis(char *, const char *, int, const char *);
ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
	rk_strvisx(char *, const char *, size_t, int);
ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
	rk_strsvisx(char *, const char *, size_t, int, const char *);
ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
	rk_strunvis(char *, const char *);
ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
	rk_strunvisx(char *, const char *, int);
ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
	rk_unvis(char *, int, int *, int);

ROKEN_CPP_END

#ifndef HAVE_VIS
#undef vis
#define vis(a,b,c,d) rk_vis(a,b,c,d)
#endif

#ifndef HAVE_SVIS
#undef svis
#define svis(a,b,c,d,e) rk_svis(a,b,c,d,e)
#endif

#ifndef HAVE_STRVIS
#undef strvis
#define strvis(a,b,c) rk_strvis(a,b,c)
#endif

#ifndef HAVE_STRSVIS
#undef strsvis
#define strsvis(a,b,c,d) rk_strsvis(a,b,c,d)
#endif

#ifndef HAVE_STRVISX
#undef strvisx
#define strvisx(a,b,c,d) rk_strvisx(a,b,c,d)
#endif

#ifndef HAVE_STRSVISX
#undef strsvisx
#define strsvisx(a,b,c,d,e) rk_strsvisx(a,b,c,d,e)
#endif

#ifndef HAVE_STRUNVIS
#undef strunvis
#define strunvis(a,b) rk_strunvis(a,b)
#endif


#ifndef HAVE_UNVIS
#undef unvis
#define unvis(a,b,c,d) rk_unvis(a,b,c,d)
#endif

#endif /* !_VIS_H_ */
