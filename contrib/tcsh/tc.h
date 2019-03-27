/* $Header: /p/tcsh/cvsroot/tcsh/tc.h,v 3.8 2006/01/12 19:55:38 christos Exp $ */
/*
 * tc.h: Tcsh includes
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
#ifndef _h_tc
#define _h_tc

#ifndef _h_tc_const
/* Don't include it while we are making it. */
# include "tc.const.h"
#endif /* _h_tc_const */
#include "tc.os.h"
#include "tc.sig.h"
#include "tc.decls.h"

extern size_t tlength;

#define FMT_PROMPT	0
#define FMT_WHO		1
#define FMT_HISTORY	2
#define FMT_SCHED	3

struct strbuf {
    char *s;
    size_t len;			/* Valid characters */
    size_t size;		/* Allocated characters */
};

struct Strbuf {
    Char *s;
    size_t len;			/* Valid characters */
    size_t size;		/* Allocated characters */
};

/* We don't have explicit initializers for variables with static storage
   duration, so these values should be equivalent to default initialization. */
#define strbuf_INIT { NULL, 0, 0 }
#define Strbuf_INIT { NULL, 0, 0 }
extern const struct strbuf strbuf_init;
extern const struct Strbuf Strbuf_init;

/* A string vector in progress */
struct blk_buf
{
    Char **vec;
    size_t len;			/* Valid strings */
    size_t size;		/* Allocated space for string pointers */
};

#define BLK_BUF_INIT { NULL, 0, 0 }

#endif /* _h_tc */
