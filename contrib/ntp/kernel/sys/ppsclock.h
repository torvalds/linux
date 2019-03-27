/*
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66.
 *
 * Copyright (c) 1992 The Regents of the University of California.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
 * 4. The name of the University may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
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

#ifndef TIOCGPPSEV
#define PPSCLOCKSTR	"ppsclock"

#ifndef HAVE_STRUCT_PPSCLOCKEV
struct ppsclockev {
	struct timeval tv;
	u_int serial;
};
#endif

#if defined(__STDC__) || defined(SYS_HPUX)
#ifdef	_IOR
#define CIOGETEV        _IOR('C', 0, struct ppsclockev)	/* get last pps event */
#else	/* XXX SOLARIS is different */
#define	CIO	('C'<<8)
#define CIOGETEV        (CIO|0)		/* get last pps event */
#endif	/* _IOR */
#else	/* __STDC__ */
#ifdef	_IOR
#define CIOGETEV        _IOR(C, 0, struct ppsclockev)	/* get last pps event */
#else	/* XXX SOLARIS is different */
#define	CIO	('C'<<8)
#define CIOGETEV        (CIO|0)		/* get last pps event */
#endif	/* _IOR */
#endif	/* __STDC__ */
#else
#define CIOGETEV TIOCGPPSEV
#endif
