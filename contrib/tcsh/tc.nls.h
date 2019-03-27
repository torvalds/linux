/* $Header: /p/tcsh/cvsroot/tcsh/tc.nls.h,v 3.17 2015/06/06 21:19:08 christos Exp $ */
/*
 * tc.nls.h: NLS support
 *
 */
/*-
 * Copyright (c) 1980, 1991 The Regents of the University of California.
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
 */
#ifndef _h_tc_nls
#define _h_tc_nls

#ifdef WIDE_STRINGS
extern int NLSWidth(Char);
extern int NLSStringWidth (const Char *);
#else
# define NLSStringWidth(s) Strlen(s)
# define NLSWidth(c) 1
#endif

extern Char *NLSChangeCase (const Char *, int);
extern int NLSClassify (Char, int, int);

#define NLSCLASS_CTRL		(-1)
#define NLSCLASS_TAB		(-2)
#define NLSCLASS_NL		(-3)
#define NLSCLASS_ILLEGAL	(-4)
#define NLSCLASS_ILLEGAL2	(-5)
#define NLSCLASS_ILLEGAL3	(-6)
#define NLSCLASS_ILLEGAL4	(-7)
#define NLSCLASS_ILLEGAL5	(-8)

#define NLSCLASS_ILLEGAL_SIZE(x) (-(x) - (-(NLSCLASS_ILLEGAL) - 1))

#endif
