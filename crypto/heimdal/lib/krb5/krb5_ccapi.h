/*
 * Copyright (c) 2004 Kungliga Tekniska Högskolan
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

#ifndef KRB5_CCAPI_H
#define KRB5_CCAPI_H 1

#include <krb5-types.h>

 #ifdef __APPLE__
#pragma pack(push,2)
#endif

enum {
    cc_credentials_v5 = 2
};

enum {
    ccapi_version_3 = 3,
    ccapi_version_4 = 4
};

enum {
    ccNoError						= 0,

    ccIteratorEnd					= 201,
    ccErrBadParam,
    ccErrNoMem,
    ccErrInvalidContext,
    ccErrInvalidCCache,

    ccErrInvalidString,					/* 206 */
    ccErrInvalidCredentials,
    ccErrInvalidCCacheIterator,
    ccErrInvalidCredentialsIterator,
    ccErrInvalidLock,

    ccErrBadName,					/* 211 */
    ccErrBadCredentialsVersion,
    ccErrBadAPIVersion,
    ccErrContextLocked,
    ccErrContextUnlocked,

    ccErrCCacheLocked,					/* 216 */
    ccErrCCacheUnlocked,
    ccErrBadLockType,
    ccErrNeverDefault,
    ccErrCredentialsNotFound,

    ccErrCCacheNotFound,				/* 221 */
    ccErrContextNotFound,
    ccErrServerUnavailable,
    ccErrServerInsecure,
    ccErrServerCantBecomeUID,

    ccErrTimeOffsetNotSet				/* 226 */
};

typedef int32_t cc_int32;
typedef uint32_t cc_uint32;
typedef struct cc_context_t *cc_context_t;
typedef struct cc_ccache_t *cc_ccache_t;
typedef struct cc_ccache_iterator_t *cc_ccache_iterator_t;
typedef struct cc_credentials_v5_t cc_credentials_v5_t;
typedef struct cc_credentials_t *cc_credentials_t;
typedef struct cc_credentials_iterator_t *cc_credentials_iterator_t;
typedef struct cc_string_t *cc_string_t;
typedef cc_uint32 cc_time_t;

typedef struct cc_data {
    cc_uint32 type;
    cc_uint32 length;
    void *data;
} cc_data;

struct cc_credentials_v5_t {
    char *client;
    char *server;
    cc_data keyblock;
    cc_time_t authtime;
    cc_time_t starttime;
    cc_time_t endtime;
    cc_time_t renew_till;
    cc_uint32 is_skey;
    cc_uint32 ticket_flags;
#define	KRB5_CCAPI_TKT_FLG_FORWARDABLE			0x40000000
#define	KRB5_CCAPI_TKT_FLG_FORWARDED			0x20000000
#define	KRB5_CCAPI_TKT_FLG_PROXIABLE			0x10000000
#define	KRB5_CCAPI_TKT_FLG_PROXY			0x08000000
#define	KRB5_CCAPI_TKT_FLG_MAY_POSTDATE			0x04000000
#define	KRB5_CCAPI_TKT_FLG_POSTDATED			0x02000000
#define	KRB5_CCAPI_TKT_FLG_INVALID			0x01000000
#define	KRB5_CCAPI_TKT_FLG_RENEWABLE			0x00800000
#define	KRB5_CCAPI_TKT_FLG_INITIAL			0x00400000
#define	KRB5_CCAPI_TKT_FLG_PRE_AUTH			0x00200000
#define	KRB5_CCAPI_TKT_FLG_HW_AUTH			0x00100000
#define	KRB5_CCAPI_TKT_FLG_TRANSIT_POLICY_CHECKED	0x00080000
#define	KRB5_CCAPI_TKT_FLG_OK_AS_DELEGATE		0x00040000
#define	KRB5_CCAPI_TKT_FLG_ANONYMOUS			0x00020000
    cc_data **addresses;
    cc_data ticket;
    cc_data second_ticket;
    cc_data **authdata;
};


typedef struct cc_string_functions {
    cc_int32 (*release)(cc_string_t);
} cc_string_functions;

struct cc_string_t {
    const char *data;
    const cc_string_functions *func;
};

typedef struct cc_credentials_union {
    cc_int32 version;
    union {
	cc_credentials_v5_t* credentials_v5;
    } credentials;
} cc_credentials_union;

struct cc_credentials_functions {
    cc_int32 (*release)(cc_credentials_t);
    cc_int32 (*compare)(cc_credentials_t, cc_credentials_t, cc_uint32*);
};

struct cc_credentials_t {
    const cc_credentials_union* data;
    const struct cc_credentials_functions* func;
};

struct cc_credentials_iterator_functions {
    cc_int32 (*release)(cc_credentials_iterator_t);
    cc_int32 (*next)(cc_credentials_iterator_t, cc_credentials_t*);
};

struct cc_credentials_iterator_t {
    const struct cc_credentials_iterator_functions *func;
};

struct cc_ccache_iterator_functions {
    cc_int32 (*release) (cc_ccache_iterator_t);
    cc_int32 (*next)(cc_ccache_iterator_t, cc_ccache_t*);
};

struct cc_ccache_iterator_t {
    const struct cc_ccache_iterator_functions* func;
};

typedef struct cc_ccache_functions {
    cc_int32 (*release)(cc_ccache_t);
    cc_int32 (*destroy)(cc_ccache_t);
    cc_int32 (*set_default)(cc_ccache_t);
    cc_int32 (*get_credentials_version)(cc_ccache_t, cc_uint32*);
    cc_int32 (*get_name)(cc_ccache_t, cc_string_t*);
    cc_int32 (*get_principal)(cc_ccache_t, cc_uint32, cc_string_t*);
    cc_int32 (*set_principal)(cc_ccache_t, cc_uint32, const char*);
    cc_int32 (*store_credentials)(cc_ccache_t, const cc_credentials_union*);
    cc_int32 (*remove_credentials)(cc_ccache_t, cc_credentials_t);
    cc_int32 (*new_credentials_iterator)(cc_ccache_t,
					 cc_credentials_iterator_t*);
    cc_int32 (*move)(cc_ccache_t, cc_ccache_t);
    cc_int32 (*lock)(cc_ccache_t, cc_uint32, cc_uint32);
    cc_int32 (*unlock)(cc_ccache_t);
    cc_int32 (*get_last_default_time)(cc_ccache_t, cc_time_t*);
    cc_int32 (*get_change_time)(cc_ccache_t, cc_time_t*);
    cc_int32 (*compare)(cc_ccache_t, cc_ccache_t, cc_uint32*);
    cc_int32 (*get_kdc_time_offset)(cc_ccache_t, cc_int32, cc_time_t *);
    cc_int32 (*set_kdc_time_offset)(cc_ccache_t, cc_int32, cc_time_t);
    cc_int32 (*clear_kdc_time_offset)(cc_ccache_t, cc_int32);
} cc_ccache_functions;

struct cc_ccache_t {
    const cc_ccache_functions *func;
};

struct  cc_context_functions {
    cc_int32 (*release)(cc_context_t);
    cc_int32 (*get_change_time)(cc_context_t, cc_time_t *);
    cc_int32 (*get_default_ccache_name)(cc_context_t, cc_string_t*);
    cc_int32 (*open_ccache)(cc_context_t, const char*, cc_ccache_t *);
    cc_int32 (*open_default_ccache)(cc_context_t, cc_ccache_t*);
    cc_int32 (*create_ccache)(cc_context_t,const char*, cc_uint32,
			      const char*, cc_ccache_t*);
    cc_int32 (*create_default_ccache)(cc_context_t, cc_uint32,
				      const char*, cc_ccache_t*);
    cc_int32 (*create_new_ccache)(cc_context_t, cc_uint32,
				  const char*, cc_ccache_t*);
    cc_int32 (*new_ccache_iterator)(cc_context_t, cc_ccache_iterator_t*);
    cc_int32 (*lock)(cc_context_t, cc_uint32, cc_uint32);
    cc_int32 (*unlock)(cc_context_t);
    cc_int32 (*compare)(cc_context_t, cc_context_t, cc_uint32*);
};

struct cc_context_t {
    const struct cc_context_functions* func;
};

typedef cc_int32
(*cc_initialize_func)(cc_context_t*, cc_int32, cc_int32 *, char const **);

#ifdef __APPLE__
#pragma pack(pop)
#endif


#endif /* KRB5_CCAPI_H */
