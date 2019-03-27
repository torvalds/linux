/*
 * Copyright (c) 2002 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* $Id$ */

#ifndef __ndbm_wrap_h__
#define __ndbm_wrap_h__

#include <stdio.h>
#include <sys/types.h>

#ifndef ROKEN_LIB_FUNCTION
#ifdef _WIN32
#define ROKEN_LIB_FUNCTION
#define ROKEN_LIB_CALL     __cdecl
#else
#define ROKEN_LIB_FUNCTION
#define ROKEN_LIB_CALL
#endif
#endif

#ifndef dbm_rename
#define dbm_rename(X)	__roken_ ## X
#endif

#define dbm_open	dbm_rename(dbm_open)
#define dbm_close	dbm_rename(dbm_close)
#define dbm_delete	dbm_rename(dbm_delete)
#define dbm_fetch	dbm_rename(dbm_fetch)
#define dbm_get		dbm_rename(dbm_get)
#define dbm_firstkey	dbm_rename(dbm_firstkey)
#define dbm_nextkey	dbm_rename(dbm_nextkey)
#define dbm_store	dbm_rename(dbm_store)
#define dbm_error	dbm_rename(dbm_error)
#define dbm_clearerr	dbm_rename(dbm_clearerr)

#define datum		dbm_rename(datum)

typedef struct {
    void *dptr;
    size_t dsize;
} datum;

#define DBM_REPLACE 1
typedef struct DBM DBM;

#if 0
typedef struct {
    int dummy;
} DBM;
#endif

ROKEN_LIB_FUNCTION int   ROKEN_LIB_CALL dbm_clearerr (DBM*);
ROKEN_LIB_FUNCTION void  ROKEN_LIB_CALL dbm_close (DBM*);
ROKEN_LIB_FUNCTION int   ROKEN_LIB_CALL dbm_delete (DBM*, datum);
ROKEN_LIB_FUNCTION int   ROKEN_LIB_CALL dbm_error (DBM*);
ROKEN_LIB_FUNCTION datum ROKEN_LIB_CALL dbm_fetch (DBM*, datum);
ROKEN_LIB_FUNCTION datum ROKEN_LIB_CALL dbm_firstkey (DBM*);
ROKEN_LIB_FUNCTION datum ROKEN_LIB_CALL dbm_nextkey (DBM*);
ROKEN_LIB_FUNCTION DBM*  ROKEN_LIB_CALL dbm_open (const char*, int, mode_t);
ROKEN_LIB_FUNCTION int   ROKEN_LIB_CALL dbm_store (DBM*, datum, datum, int);

#endif /* __ndbm_wrap_h__ */
