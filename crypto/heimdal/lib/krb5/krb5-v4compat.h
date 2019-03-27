/*
 * Copyright (c) 1997 - 2003 Kungliga Tekniska Högskolan
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

#ifndef __KRB5_V4COMPAT_H__
#define __KRB5_V4COMPAT_H__

#include "krb_err.h"

/*
 * This file must only be included with v4 compat glue stuff in
 * heimdal sources.
 *
 * It MUST NOT be installed.
 */

#define		KRB_PROT_VERSION 	4

#define		AUTH_MSG_KDC_REQUEST			 (1<<1)
#define 	AUTH_MSG_KDC_REPLY			 (2<<1)
#define		AUTH_MSG_APPL_REQUEST			 (3<<1)
#define		AUTH_MSG_APPL_REQUEST_MUTUAL		 (4<<1)
#define		AUTH_MSG_ERR_REPLY			 (5<<1)
#define		AUTH_MSG_PRIVATE			 (6<<1)
#define		AUTH_MSG_SAFE				 (7<<1)
#define		AUTH_MSG_APPL_ERR			 (8<<1)
#define		AUTH_MSG_KDC_FORWARD			 (9<<1)
#define		AUTH_MSG_KDC_RENEW			(10<<1)
#define 	AUTH_MSG_DIE				(63<<1)

/* General definitions */
#define		KSUCCESS	0
#define		KFAILURE	255

/* */

#define		MAX_KTXT_LEN	1250

#define 	ANAME_SZ	40
#define		REALM_SZ	40
#define		SNAME_SZ	40
#define		INST_SZ		40

struct ktext {
    unsigned int length;		/* Length of the text */
    unsigned char dat[MAX_KTXT_LEN];	/* The data itself */
    uint32_t mbz;		/* zero to catch runaway strings */
};

struct credentials {
    char    service[ANAME_SZ];	/* Service name */
    char    instance[INST_SZ];	/* Instance */
    char    realm[REALM_SZ];	/* Auth domain */
    char    session[8];		/* Session key */
    int     lifetime;		/* Lifetime */
    int     kvno;		/* Key version number */
    struct ktext ticket_st;	/* The ticket itself */
    int32_t    issue_date;	/* The issue time */
    char    pname[ANAME_SZ];	/* Principal's name */
    char    pinst[INST_SZ];	/* Principal's instance */
};

#define TKTLIFENUMFIXED 64
#define TKTLIFEMINFIXED 0x80
#define TKTLIFEMAXFIXED 0xBF
#define TKTLIFENOEXPIRE 0xFF
#define MAXTKTLIFETIME	(30*24*3600)	/* 30 days */
#ifndef NEVERDATE
#define NEVERDATE ((time_t)0x7fffffffL)
#endif

#define		KERB_ERR_NULL_KEY	10

#define 	CLOCK_SKEW	5*60

#ifndef TKT_ROOT
#ifdef KRB5_USE_PATH_TOKENS
#define TKT_ROOT "%{TEMP}/tkt"
#else
#define TKT_ROOT "/tmp/tkt"
#endif
#endif

struct _krb5_krb_auth_data {
    int8_t  k_flags;		/* Flags from ticket */
    char    *pname;		/* Principal's name */
    char    *pinst;		/* His Instance */
    char    *prealm;		/* His Realm */
    uint32_t checksum;		/* Data checksum (opt) */
    krb5_keyblock session;	/* Session Key */
    unsigned char life;		/* Life of ticket */
    uint32_t time_sec;		/* Time ticket issued */
    uint32_t address;		/* Address in ticket */
};

KRB5_LIB_FUNCTION time_t KRB5_LIB_CALL
_krb5_krb_life_to_time (int, int);

KRB5_LIB_FUNCTION int KRB5_LIB_CALL
_krb5_krb_time_to_life (time_t, time_t);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
_krb5_krb_tf_setup (krb5_context, struct credentials *,
		    const char *, int);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
_krb5_krb_dest_tkt(krb5_context, const char *);

#define krb_time_to_life	_krb5_krb_time_to_life
#define krb_life_to_time	_krb5_krb_life_to_time

#endif /*  __KRB5_V4COMPAT_H__ */
