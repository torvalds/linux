/*
 * Copyright (c) 2004, PADL Software Pty Ltd.
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
 * 3. Neither the name of PADL Software nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY PADL SOFTWARE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL PADL SOFTWARE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* $Id$ */

#ifndef SPNEGO_LOCL_H
#define SPNEGO_LOCL_H

#include <config.h>

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#include <roken.h>

#ifdef HAVE_PTHREAD_H
#include <pthread.h>
#endif

#include <gssapi.h>
#include <gssapi_krb5.h>
#include <gssapi_spnego.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif

#include <heim_threads.h>
#include <asn1_err.h>

#include <gssapi_mech.h>

#include "spnego_asn1.h"
#include "utils.h"
#include <der.h>

#include <heimbase.h>

#define ALLOC(X, N) (X) = calloc((N), sizeof(*(X)))

typedef struct {
	MechTypeList		initiator_mech_types;
	gss_OID			preferred_mech_type;
	gss_OID			negotiated_mech_type;
	gss_ctx_id_t		negotiated_ctx_id;
	OM_uint32		mech_flags;
	OM_uint32		mech_time_rec;
	gss_name_t		mech_src_name;
	unsigned int		open : 1;
	unsigned int		local : 1;
	unsigned int		require_mic : 1;
	unsigned int		verified_mic : 1;
	unsigned int		maybe_open : 1;
	HEIMDAL_MUTEX		ctx_id_mutex;

	gss_name_t		target_name;

	u_char			oidbuf[17];
 	size_t			oidlen;

} *gssspnego_ctx;

typedef struct {
	gss_OID_desc		type;
	gss_buffer_desc		value;
	gss_name_t		mech;
} *spnego_name;

extern gss_OID_desc _gss_spnego_mskrb_mechanism_oid_desc;
extern gss_OID_desc _gss_spnego_krb5_mechanism_oid_desc;

#include <spnego-private.h>

#endif /* SPNEGO_LOCL_H */
