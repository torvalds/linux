/*
 * Copyright (c) 2005, PADL Software Pty Ltd.
 * All rights reserved.
 *
 * Portions Copyright (c) 2009 Apple Inc. All rights reserved.
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

#include "krb5_locl.h"

#ifdef HAVE_KCM
/*
 * Client library for Kerberos Credentials Manager (KCM) daemon
 */

#include "kcm.h"
#include <heim-ipc.h>

static krb5_error_code
kcm_set_kdc_offset(krb5_context, krb5_ccache, krb5_deltat);

static const char *kcm_ipc_name = "ANY:org.h5l.kcm";

typedef struct krb5_kcmcache {
    char *name;
} krb5_kcmcache;

typedef struct krb5_kcm_cursor {
    unsigned long offset;
    unsigned long length;
    kcmuuid_t *uuids;
} *krb5_kcm_cursor;


#define KCMCACHE(X)	((krb5_kcmcache *)(X)->data.data)
#define CACHENAME(X)	(KCMCACHE(X)->name)
#define KCMCURSOR(C)	((krb5_kcm_cursor)(C))

static HEIMDAL_MUTEX kcm_mutex = HEIMDAL_MUTEX_INITIALIZER;
static heim_ipc kcm_ipc = NULL;

static krb5_error_code
kcm_send_request(krb5_context context,
		 krb5_storage *request,
		 krb5_data *response_data)
{
    krb5_error_code ret = 0;
    krb5_data request_data;

    HEIMDAL_MUTEX_lock(&kcm_mutex);
    if (kcm_ipc == NULL)
	ret = heim_ipc_init_context(kcm_ipc_name, &kcm_ipc);
    HEIMDAL_MUTEX_unlock(&kcm_mutex);
    if (ret)
	return KRB5_CC_NOSUPP;

    ret = krb5_storage_to_data(request, &request_data);
    if (ret) {
	krb5_clear_error_message(context);
	return KRB5_CC_NOMEM;
    }

    ret = heim_ipc_call(kcm_ipc, &request_data, response_data, NULL);
    krb5_data_free(&request_data);

    if (ret) {
	krb5_clear_error_message(context);
	ret = KRB5_CC_NOSUPP;
    }

    return ret;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kcm_storage_request(krb5_context context,
			 uint16_t opcode,
			 krb5_storage **storage_p)
{
    krb5_storage *sp;
    krb5_error_code ret;

    *storage_p = NULL;

    sp = krb5_storage_emem();
    if (sp == NULL) {
	krb5_set_error_message(context, KRB5_CC_NOMEM, N_("malloc: out of memory", ""));
	return KRB5_CC_NOMEM;
    }

    /* Send MAJOR | VERSION | OPCODE */
    ret  = krb5_store_int8(sp, KCM_PROTOCOL_VERSION_MAJOR);
    if (ret)
	goto fail;
    ret = krb5_store_int8(sp, KCM_PROTOCOL_VERSION_MINOR);
    if (ret)
	goto fail;
    ret = krb5_store_int16(sp, opcode);
    if (ret)
	goto fail;

    *storage_p = sp;
 fail:
    if (ret) {
	krb5_set_error_message(context, ret,
			       N_("Failed to encode KCM request", ""));
	krb5_storage_free(sp);
    }

    return ret;
}

static krb5_error_code
kcm_alloc(krb5_context context, const char *name, krb5_ccache *id)
{
    krb5_kcmcache *k;

    k = malloc(sizeof(*k));
    if (k == NULL) {
	krb5_set_error_message(context, KRB5_CC_NOMEM,
			       N_("malloc: out of memory", ""));
	return KRB5_CC_NOMEM;
    }

    if (name != NULL) {
	k->name = strdup(name);
	if (k->name == NULL) {
	    free(k);
	    krb5_set_error_message(context, KRB5_CC_NOMEM,
				   N_("malloc: out of memory", ""));
	    return KRB5_CC_NOMEM;
	}
    } else
	k->name = NULL;

    (*id)->data.data = k;
    (*id)->data.length = sizeof(*k);

    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kcm_call(krb5_context context,
	      krb5_storage *request,
	      krb5_storage **response_p,
	      krb5_data *response_data_p)
{
    krb5_data response_data;
    krb5_error_code ret;
    int32_t status;
    krb5_storage *response;

    if (response_p != NULL)
	*response_p = NULL;

    krb5_data_zero(&response_data);

    ret = kcm_send_request(context, request, &response_data);
    if (ret)
	return ret;

    response = krb5_storage_from_data(&response_data);
    if (response == NULL) {
	krb5_data_free(&response_data);
	return KRB5_CC_IO;
    }

    ret = krb5_ret_int32(response, &status);
    if (ret) {
	krb5_storage_free(response);
	krb5_data_free(&response_data);
	return KRB5_CC_FORMAT;
    }

    if (status) {
	krb5_storage_free(response);
	krb5_data_free(&response_data);
	return status;
    }

    if (response_p != NULL) {
	*response_data_p = response_data;
	*response_p = response;

	return 0;
    }

    krb5_storage_free(response);
    krb5_data_free(&response_data);

    return 0;
}

static void
kcm_free(krb5_context context, krb5_ccache *id)
{
    krb5_kcmcache *k = KCMCACHE(*id);

    if (k != NULL) {
	if (k->name != NULL)
	    free(k->name);
	memset(k, 0, sizeof(*k));
	krb5_data_free(&(*id)->data);
    }
}

static const char *
kcm_get_name(krb5_context context,
	     krb5_ccache id)
{
    return CACHENAME(id);
}

static krb5_error_code
kcm_resolve(krb5_context context, krb5_ccache *id, const char *res)
{
    return kcm_alloc(context, res, id);
}

/*
 * Request:
 *
 * Response:
 *      NameZ
 */
static krb5_error_code
kcm_gen_new(krb5_context context, krb5_ccache *id)
{
    krb5_kcmcache *k;
    krb5_error_code ret;
    krb5_storage *request, *response;
    krb5_data response_data;

    ret = kcm_alloc(context, NULL, id);
    if (ret)
	return ret;

    k = KCMCACHE(*id);

    ret = krb5_kcm_storage_request(context, KCM_OP_GEN_NEW, &request);
    if (ret) {
	kcm_free(context, id);
	return ret;
    }

    ret = krb5_kcm_call(context, request, &response, &response_data);
    if (ret) {
	krb5_storage_free(request);
	kcm_free(context, id);
	return ret;
    }

    ret = krb5_ret_stringz(response, &k->name);
    if (ret)
	ret = KRB5_CC_IO;

    krb5_storage_free(request);
    krb5_storage_free(response);
    krb5_data_free(&response_data);

    if (ret)
	kcm_free(context, id);

    return ret;
}

/*
 * Request:
 *      NameZ
 *      Principal
 *
 * Response:
 *
 */
static krb5_error_code
kcm_initialize(krb5_context context,
	       krb5_ccache id,
	       krb5_principal primary_principal)
{
    krb5_error_code ret;
    krb5_kcmcache *k = KCMCACHE(id);
    krb5_storage *request;

    ret = krb5_kcm_storage_request(context, KCM_OP_INITIALIZE, &request);
    if (ret)
	return ret;

    ret = krb5_store_stringz(request, k->name);
    if (ret) {
	krb5_storage_free(request);
	return ret;
    }

    ret = krb5_store_principal(request, primary_principal);
    if (ret) {
	krb5_storage_free(request);
	return ret;
    }

    ret = krb5_kcm_call(context, request, NULL, NULL);

    krb5_storage_free(request);

    if (context->kdc_sec_offset)
	kcm_set_kdc_offset(context, id, context->kdc_sec_offset);

    return ret;
}

static krb5_error_code
kcm_close(krb5_context context,
	  krb5_ccache id)
{
    kcm_free(context, &id);
    return 0;
}

/*
 * Request:
 *      NameZ
 *
 * Response:
 *
 */
static krb5_error_code
kcm_destroy(krb5_context context,
	    krb5_ccache id)
{
    krb5_error_code ret;
    krb5_kcmcache *k = KCMCACHE(id);
    krb5_storage *request;

    ret = krb5_kcm_storage_request(context, KCM_OP_DESTROY, &request);
    if (ret)
	return ret;

    ret = krb5_store_stringz(request, k->name);
    if (ret) {
	krb5_storage_free(request);
	return ret;
    }

    ret = krb5_kcm_call(context, request, NULL, NULL);

    krb5_storage_free(request);
    return ret;
}

/*
 * Request:
 *      NameZ
 *      Creds
 *
 * Response:
 *
 */
static krb5_error_code
kcm_store_cred(krb5_context context,
	       krb5_ccache id,
	       krb5_creds *creds)
{
    krb5_error_code ret;
    krb5_kcmcache *k = KCMCACHE(id);
    krb5_storage *request;

    ret = krb5_kcm_storage_request(context, KCM_OP_STORE, &request);
    if (ret)
	return ret;

    ret = krb5_store_stringz(request, k->name);
    if (ret) {
	krb5_storage_free(request);
	return ret;
    }

    ret = krb5_store_creds(request, creds);
    if (ret) {
	krb5_storage_free(request);
	return ret;
    }

    ret = krb5_kcm_call(context, request, NULL, NULL);

    krb5_storage_free(request);
    return ret;
}

#if 0
/*
 * Request:
 *      NameZ
 *      WhichFields
 *      MatchCreds
 *
 * Response:
 *      Creds
 *
 */
static krb5_error_code
kcm_retrieve(krb5_context context,
	     krb5_ccache id,
	     krb5_flags which,
	     const krb5_creds *mcred,
	     krb5_creds *creds)
{
    krb5_error_code ret;
    krb5_kcmcache *k = KCMCACHE(id);
    krb5_storage *request, *response;
    krb5_data response_data;

    ret = krb5_kcm_storage_request(context, KCM_OP_RETRIEVE, &request);
    if (ret)
	return ret;

    ret = krb5_store_stringz(request, k->name);
    if (ret) {
	krb5_storage_free(request);
	return ret;
    }

    ret = krb5_store_int32(request, which);
    if (ret) {
	krb5_storage_free(request);
	return ret;
    }

    ret = krb5_store_creds_tag(request, rk_UNCONST(mcred));
    if (ret) {
	krb5_storage_free(request);
	return ret;
    }

    ret = krb5_kcm_call(context, request, &response, &response_data);
    if (ret) {
	krb5_storage_free(request);
	return ret;
    }

    ret = krb5_ret_creds(response, creds);
    if (ret)
	ret = KRB5_CC_IO;

    krb5_storage_free(request);
    krb5_storage_free(response);
    krb5_data_free(&response_data);

    return ret;
}
#endif

/*
 * Request:
 *      NameZ
 *
 * Response:
 *      Principal
 */
static krb5_error_code
kcm_get_principal(krb5_context context,
		  krb5_ccache id,
		  krb5_principal *principal)
{
    krb5_error_code ret;
    krb5_kcmcache *k = KCMCACHE(id);
    krb5_storage *request, *response;
    krb5_data response_data;

    ret = krb5_kcm_storage_request(context, KCM_OP_GET_PRINCIPAL, &request);
    if (ret)
	return ret;

    ret = krb5_store_stringz(request, k->name);
    if (ret) {
	krb5_storage_free(request);
	return ret;
    }

    ret = krb5_kcm_call(context, request, &response, &response_data);
    if (ret) {
	krb5_storage_free(request);
	return ret;
    }

    ret = krb5_ret_principal(response, principal);
    if (ret)
	ret = KRB5_CC_IO;

    krb5_storage_free(request);
    krb5_storage_free(response);
    krb5_data_free(&response_data);

    return ret;
}

/*
 * Request:
 *      NameZ
 *
 * Response:
 *      Cursor
 *
 */
static krb5_error_code
kcm_get_first (krb5_context context,
	       krb5_ccache id,
	       krb5_cc_cursor *cursor)
{
    krb5_error_code ret;
    krb5_kcm_cursor c;
    krb5_kcmcache *k = KCMCACHE(id);
    krb5_storage *request, *response;
    krb5_data response_data;

    ret = krb5_kcm_storage_request(context, KCM_OP_GET_CRED_UUID_LIST, &request);
    if (ret)
	return ret;

    ret = krb5_store_stringz(request, k->name);
    if (ret) {
	krb5_storage_free(request);
	return ret;
    }

    ret = krb5_kcm_call(context, request, &response, &response_data);
    krb5_storage_free(request);
    if (ret)
	return ret;

    c = calloc(1, sizeof(*c));
    if (c == NULL) {
	ret = ENOMEM;
	krb5_set_error_message(context, ret,
			       N_("malloc: out of memory", ""));
	return ret;
    }

    while (1) {
	ssize_t sret;
	kcmuuid_t uuid;
	void *ptr;

	sret = krb5_storage_read(response, &uuid, sizeof(uuid));
	if (sret == 0) {
	    ret = 0;
	    break;
	} else if (sret != sizeof(uuid)) {
	    ret = EINVAL;
	    break;
	}

	ptr = realloc(c->uuids, sizeof(c->uuids[0]) * (c->length + 1));
	if (ptr == NULL) {
	    free(c->uuids);
	    free(c);
	    krb5_set_error_message(context, ENOMEM,
				   N_("malloc: out of memory", ""));
	    return ENOMEM;
	}
	c->uuids = ptr;

	memcpy(&c->uuids[c->length], &uuid, sizeof(uuid));
	c->length += 1;
    }

    krb5_storage_free(response);
    krb5_data_free(&response_data);

    if (ret) {
        free(c->uuids);
        free(c);
	return ret;
    }

    *cursor = c;

    return 0;
}

/*
 * Request:
 *      NameZ
 *      Cursor
 *
 * Response:
 *      Creds
 */
static krb5_error_code
kcm_get_next (krb5_context context,
		krb5_ccache id,
		krb5_cc_cursor *cursor,
		krb5_creds *creds)
{
    krb5_error_code ret;
    krb5_kcmcache *k = KCMCACHE(id);
    krb5_kcm_cursor c = KCMCURSOR(*cursor);
    krb5_storage *request, *response;
    krb5_data response_data;
    ssize_t sret;

 again:

    if (c->offset >= c->length)
	return KRB5_CC_END;

    ret = krb5_kcm_storage_request(context, KCM_OP_GET_CRED_BY_UUID, &request);
    if (ret)
	return ret;

    ret = krb5_store_stringz(request, k->name);
    if (ret) {
	krb5_storage_free(request);
	return ret;
    }

    sret = krb5_storage_write(request,
			      &c->uuids[c->offset],
			      sizeof(c->uuids[c->offset]));
    c->offset++;
    if (sret != sizeof(c->uuids[c->offset])) {
	krb5_storage_free(request);
	krb5_clear_error_message(context);
	return ENOMEM;
    }

    ret = krb5_kcm_call(context, request, &response, &response_data);
    krb5_storage_free(request);
    if (ret == KRB5_CC_END) {
	goto again;
    }

    ret = krb5_ret_creds(response, creds);
    if (ret)
	ret = KRB5_CC_IO;

    krb5_storage_free(response);
    krb5_data_free(&response_data);

    return ret;
}

/*
 * Request:
 *      NameZ
 *      Cursor
 *
 * Response:
 *
 */
static krb5_error_code
kcm_end_get (krb5_context context,
	     krb5_ccache id,
	     krb5_cc_cursor *cursor)
{
    krb5_kcm_cursor c = KCMCURSOR(*cursor);

    free(c->uuids);
    free(c);

    *cursor = NULL;

    return 0;
}

/*
 * Request:
 *      NameZ
 *      WhichFields
 *      MatchCreds
 *
 * Response:
 *
 */
static krb5_error_code
kcm_remove_cred(krb5_context context,
		krb5_ccache id,
		krb5_flags which,
		krb5_creds *cred)
{
    krb5_error_code ret;
    krb5_kcmcache *k = KCMCACHE(id);
    krb5_storage *request;

    ret = krb5_kcm_storage_request(context, KCM_OP_REMOVE_CRED, &request);
    if (ret)
	return ret;

    ret = krb5_store_stringz(request, k->name);
    if (ret) {
	krb5_storage_free(request);
	return ret;
    }

    ret = krb5_store_int32(request, which);
    if (ret) {
	krb5_storage_free(request);
	return ret;
    }

    ret = krb5_store_creds_tag(request, cred);
    if (ret) {
	krb5_storage_free(request);
	return ret;
    }

    ret = krb5_kcm_call(context, request, NULL, NULL);

    krb5_storage_free(request);
    return ret;
}

static krb5_error_code
kcm_set_flags(krb5_context context,
	      krb5_ccache id,
	      krb5_flags flags)
{
    krb5_error_code ret;
    krb5_kcmcache *k = KCMCACHE(id);
    krb5_storage *request;

    ret = krb5_kcm_storage_request(context, KCM_OP_SET_FLAGS, &request);
    if (ret)
	return ret;

    ret = krb5_store_stringz(request, k->name);
    if (ret) {
	krb5_storage_free(request);
	return ret;
    }

    ret = krb5_store_int32(request, flags);
    if (ret) {
	krb5_storage_free(request);
	return ret;
    }

    ret = krb5_kcm_call(context, request, NULL, NULL);

    krb5_storage_free(request);
    return ret;
}

static int
kcm_get_version(krb5_context context,
		krb5_ccache id)
{
    return 0;
}

/*
 * Send nothing
 * get back list of uuids
 */

static krb5_error_code
kcm_get_cache_first(krb5_context context, krb5_cc_cursor *cursor)
{
    krb5_error_code ret;
    krb5_kcm_cursor c;
    krb5_storage *request, *response;
    krb5_data response_data;

    *cursor = NULL;

    c = calloc(1, sizeof(*c));
    if (c == NULL) {
	ret = ENOMEM;
	krb5_set_error_message(context, ret,
			       N_("malloc: out of memory", ""));
	goto out;
    }

    ret = krb5_kcm_storage_request(context, KCM_OP_GET_CACHE_UUID_LIST, &request);
    if (ret)
	goto out;

    ret = krb5_kcm_call(context, request, &response, &response_data);
    krb5_storage_free(request);
    if (ret)
	goto out;

    while (1) {
	ssize_t sret;
	kcmuuid_t uuid;
	void *ptr;

	sret = krb5_storage_read(response, &uuid, sizeof(uuid));
	if (sret == 0) {
	    ret = 0;
	    break;
	} else if (sret != sizeof(uuid)) {
	    ret = EINVAL;
	    goto out;
	}

	ptr = realloc(c->uuids, sizeof(c->uuids[0]) * (c->length + 1));
	if (ptr == NULL) {
	    ret = ENOMEM;
	    krb5_set_error_message(context, ret,
				   N_("malloc: out of memory", ""));
	    goto out;
	}
	c->uuids = ptr;

	memcpy(&c->uuids[c->length], &uuid, sizeof(uuid));
	c->length += 1;
    }

    krb5_storage_free(response);
    krb5_data_free(&response_data);

 out:
    if (ret && c) {
        free(c->uuids);
        free(c);
    } else
	*cursor = c;

    return ret;
}

/*
 * Send uuid
 * Recv cache name
 */

static krb5_error_code
kcm_get_cache_next(krb5_context context, krb5_cc_cursor cursor, const krb5_cc_ops *ops, krb5_ccache *id)
{
    krb5_error_code ret;
    krb5_kcm_cursor c = KCMCURSOR(cursor);
    krb5_storage *request, *response;
    krb5_data response_data;
    ssize_t sret;
    char *name;

    *id = NULL;

 again:

    if (c->offset >= c->length)
	return KRB5_CC_END;

    ret = krb5_kcm_storage_request(context, KCM_OP_GET_CACHE_BY_UUID, &request);
    if (ret)
	return ret;

    sret = krb5_storage_write(request,
			      &c->uuids[c->offset],
			      sizeof(c->uuids[c->offset]));
    c->offset++;
    if (sret != sizeof(c->uuids[c->offset])) {
	krb5_storage_free(request);
	krb5_clear_error_message(context);
	return ENOMEM;
    }

    ret = krb5_kcm_call(context, request, &response, &response_data);
    krb5_storage_free(request);
    if (ret == KRB5_CC_END)
	goto again;

    ret = krb5_ret_stringz(response, &name);
    krb5_storage_free(response);
    krb5_data_free(&response_data);

    if (ret == 0) {
	ret = _krb5_cc_allocate(context, ops, id);
	if (ret == 0)
	    ret = kcm_alloc(context, name, id);
	krb5_xfree(name);
    }

    return ret;
}

static krb5_error_code
kcm_get_cache_next_kcm(krb5_context context, krb5_cc_cursor cursor, krb5_ccache *id)
{
#ifndef KCM_IS_API_CACHE
    return kcm_get_cache_next(context, cursor, &krb5_kcm_ops, id);
#else
    return KRB5_CC_END;
#endif
}

static krb5_error_code
kcm_get_cache_next_api(krb5_context context, krb5_cc_cursor cursor, krb5_ccache *id)
{
    return kcm_get_cache_next(context, cursor, &krb5_akcm_ops, id);
}


static krb5_error_code
kcm_end_cache_get(krb5_context context, krb5_cc_cursor cursor)
{
    krb5_kcm_cursor c = KCMCURSOR(cursor);

    free(c->uuids);
    free(c);
    return 0;
}


static krb5_error_code
kcm_move(krb5_context context, krb5_ccache from, krb5_ccache to)
{
    krb5_error_code ret;
    krb5_kcmcache *oldk = KCMCACHE(from);
    krb5_kcmcache *newk = KCMCACHE(to);
    krb5_storage *request;

    ret = krb5_kcm_storage_request(context, KCM_OP_MOVE_CACHE, &request);
    if (ret)
	return ret;

    ret = krb5_store_stringz(request, oldk->name);
    if (ret) {
	krb5_storage_free(request);
	return ret;
    }

    ret = krb5_store_stringz(request, newk->name);
    if (ret) {
	krb5_storage_free(request);
	return ret;
    }
    ret = krb5_kcm_call(context, request, NULL, NULL);

    krb5_storage_free(request);
    return ret;
}

static krb5_error_code
kcm_get_default_name(krb5_context context, const krb5_cc_ops *ops,
		     const char *defstr, char **str)
{
    krb5_error_code ret;
    krb5_storage *request, *response;
    krb5_data response_data;
    char *name;

    *str = NULL;

    ret = krb5_kcm_storage_request(context, KCM_OP_GET_DEFAULT_CACHE, &request);
    if (ret)
	return ret;

    ret = krb5_kcm_call(context, request, &response, &response_data);
    krb5_storage_free(request);
    if (ret)
	return _krb5_expand_default_cc_name(context, defstr, str);

    ret = krb5_ret_stringz(response, &name);
    krb5_storage_free(response);
    krb5_data_free(&response_data);
    if (ret)
	return ret;

    asprintf(str, "%s:%s", ops->prefix, name);
    free(name);
    if (str == NULL)
	return ENOMEM;

    return 0;
}

static krb5_error_code
kcm_get_default_name_api(krb5_context context, char **str)
{
    return kcm_get_default_name(context, &krb5_akcm_ops,
				KRB5_DEFAULT_CCNAME_KCM_API, str);
}

static krb5_error_code
kcm_get_default_name_kcm(krb5_context context, char **str)
{
    return kcm_get_default_name(context, &krb5_kcm_ops,
				KRB5_DEFAULT_CCNAME_KCM_KCM, str);
}

static krb5_error_code
kcm_set_default(krb5_context context, krb5_ccache id)
{
    krb5_error_code ret;
    krb5_storage *request;
    krb5_kcmcache *k = KCMCACHE(id);

    ret = krb5_kcm_storage_request(context, KCM_OP_SET_DEFAULT_CACHE, &request);
    if (ret)
	return ret;

    ret = krb5_store_stringz(request, k->name);
    if (ret) {
	krb5_storage_free(request);
	return ret;
    }

    ret = krb5_kcm_call(context, request, NULL, NULL);
    krb5_storage_free(request);

    return ret;
}

static krb5_error_code
kcm_lastchange(krb5_context context, krb5_ccache id, krb5_timestamp *mtime)
{
    *mtime = time(NULL);
    return 0;
}

static krb5_error_code
kcm_set_kdc_offset(krb5_context context, krb5_ccache id, krb5_deltat kdc_offset)
{
    krb5_kcmcache *k = KCMCACHE(id);
    krb5_error_code ret;
    krb5_storage *request;

    ret = krb5_kcm_storage_request(context, KCM_OP_SET_KDC_OFFSET, &request);
    if (ret)
	return ret;

    ret = krb5_store_stringz(request, k->name);
    if (ret) {
	krb5_storage_free(request);
	return ret;
    }
    ret = krb5_store_int32(request, kdc_offset);
    if (ret) {
	krb5_storage_free(request);
	return ret;
    }

    ret = krb5_kcm_call(context, request, NULL, NULL);
    krb5_storage_free(request);

    return ret;
}

static krb5_error_code
kcm_get_kdc_offset(krb5_context context, krb5_ccache id, krb5_deltat *kdc_offset)
{
    krb5_kcmcache *k = KCMCACHE(id);
    krb5_error_code ret;
    krb5_storage *request, *response;
    krb5_data response_data;
    int32_t offset;

    ret = krb5_kcm_storage_request(context, KCM_OP_GET_KDC_OFFSET, &request);
    if (ret)
	return ret;

    ret = krb5_store_stringz(request, k->name);
    if (ret) {
	krb5_storage_free(request);
	return ret;
    }

    ret = krb5_kcm_call(context, request, &response, &response_data);
    krb5_storage_free(request);
    if (ret)
	return ret;

    ret = krb5_ret_int32(response, &offset);
    krb5_storage_free(response);
    krb5_data_free(&response_data);
    if (ret)
	return ret;

    *kdc_offset = offset;

    return 0;
}

/**
 * Variable containing the KCM based credential cache implemention.
 *
 * @ingroup krb5_ccache
 */

KRB5_LIB_VARIABLE const krb5_cc_ops krb5_kcm_ops = {
    KRB5_CC_OPS_VERSION,
    "KCM",
    kcm_get_name,
    kcm_resolve,
    kcm_gen_new,
    kcm_initialize,
    kcm_destroy,
    kcm_close,
    kcm_store_cred,
    NULL /* kcm_retrieve */,
    kcm_get_principal,
    kcm_get_first,
    kcm_get_next,
    kcm_end_get,
    kcm_remove_cred,
    kcm_set_flags,
    kcm_get_version,
    kcm_get_cache_first,
    kcm_get_cache_next_kcm,
    kcm_end_cache_get,
    kcm_move,
    kcm_get_default_name_kcm,
    kcm_set_default,
    kcm_lastchange,
    kcm_set_kdc_offset,
    kcm_get_kdc_offset
};

KRB5_LIB_VARIABLE const krb5_cc_ops krb5_akcm_ops = {
    KRB5_CC_OPS_VERSION,
    "API",
    kcm_get_name,
    kcm_resolve,
    kcm_gen_new,
    kcm_initialize,
    kcm_destroy,
    kcm_close,
    kcm_store_cred,
    NULL /* kcm_retrieve */,
    kcm_get_principal,
    kcm_get_first,
    kcm_get_next,
    kcm_end_get,
    kcm_remove_cred,
    kcm_set_flags,
    kcm_get_version,
    kcm_get_cache_first,
    kcm_get_cache_next_api,
    kcm_end_cache_get,
    kcm_move,
    kcm_get_default_name_api,
    kcm_set_default,
    kcm_lastchange,
    NULL,
    NULL
};


KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
_krb5_kcm_is_running(krb5_context context)
{
    krb5_error_code ret;
    krb5_ccache_data ccdata;
    krb5_ccache id = &ccdata;
    krb5_boolean running;

    ret = kcm_alloc(context, NULL, &id);
    if (ret)
	return 0;

    running = (_krb5_kcm_noop(context, id) == 0);

    kcm_free(context, &id);

    return running;
}

/*
 * Request:
 *
 * Response:
 *
 */
KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
_krb5_kcm_noop(krb5_context context,
	       krb5_ccache id)
{
    krb5_error_code ret;
    krb5_storage *request;

    ret = krb5_kcm_storage_request(context, KCM_OP_NOOP, &request);
    if (ret)
	return ret;

    ret = krb5_kcm_call(context, request, NULL, NULL);

    krb5_storage_free(request);
    return ret;
}


/*
 * Request:
 *      NameZ
 *      ServerPrincipalPresent
 *      ServerPrincipal OPTIONAL
 *      Key
 *
 * Repsonse:
 *
 */
KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
_krb5_kcm_get_initial_ticket(krb5_context context,
			     krb5_ccache id,
			     krb5_principal server,
			     krb5_keyblock *key)
{
    krb5_kcmcache *k = KCMCACHE(id);
    krb5_error_code ret;
    krb5_storage *request;

    ret = krb5_kcm_storage_request(context, KCM_OP_GET_INITIAL_TICKET, &request);
    if (ret)
	return ret;

    ret = krb5_store_stringz(request, k->name);
    if (ret) {
	krb5_storage_free(request);
	return ret;
    }

    ret = krb5_store_int8(request, (server == NULL) ? 0 : 1);
    if (ret) {
	krb5_storage_free(request);
	return ret;
    }

    if (server != NULL) {
	ret = krb5_store_principal(request, server);
	if (ret) {
	    krb5_storage_free(request);
	    return ret;
	}
    }

    ret = krb5_store_keyblock(request, *key);
    if (ret) {
	krb5_storage_free(request);
	return ret;
    }

    ret = krb5_kcm_call(context, request, NULL, NULL);

    krb5_storage_free(request);
    return ret;
}


/*
 * Request:
 *      NameZ
 *      KDCFlags
 *      EncryptionType
 *      ServerPrincipal
 *
 * Repsonse:
 *
 */
KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
_krb5_kcm_get_ticket(krb5_context context,
		     krb5_ccache id,
		     krb5_kdc_flags flags,
		     krb5_enctype enctype,
		     krb5_principal server)
{
    krb5_error_code ret;
    krb5_kcmcache *k = KCMCACHE(id);
    krb5_storage *request;

    ret = krb5_kcm_storage_request(context, KCM_OP_GET_TICKET, &request);
    if (ret)
	return ret;

    ret = krb5_store_stringz(request, k->name);
    if (ret) {
	krb5_storage_free(request);
	return ret;
    }

    ret = krb5_store_int32(request, flags.i);
    if (ret) {
	krb5_storage_free(request);
	return ret;
    }

    ret = krb5_store_int32(request, enctype);
    if (ret) {
	krb5_storage_free(request);
	return ret;
    }

    ret = krb5_store_principal(request, server);
    if (ret) {
	krb5_storage_free(request);
	return ret;
    }

    ret = krb5_kcm_call(context, request, NULL, NULL);

    krb5_storage_free(request);
    return ret;
}

#endif /* HAVE_KCM */
