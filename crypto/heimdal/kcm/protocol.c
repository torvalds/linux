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

#include "kcm_locl.h"
#include <heimntlm.h>

static void
kcm_drop_default_cache(krb5_context context, kcm_client *client, char *name);


int
kcm_is_same_session(kcm_client *client, uid_t uid, pid_t session)
{
#if 0 /* XXX pppd is running in diffrent session the user */
    if (session != -1)
	return (client->session == session);
    else
#endif
	return  (client->uid == uid);
}

static krb5_error_code
kcm_op_noop(krb5_context context,
	    kcm_client *client,
	    kcm_operation opcode,
	    krb5_storage *request,
	    krb5_storage *response)
{
    KCM_LOG_REQUEST(context, client, opcode);

    return 0;
}

/*
 * Request:
 *	NameZ
 * Response:
 *	NameZ
 *
 */
static krb5_error_code
kcm_op_get_name(krb5_context context,
		kcm_client *client,
		kcm_operation opcode,
		krb5_storage *request,
		krb5_storage *response)

{
    krb5_error_code ret;
    char *name = NULL;
    kcm_ccache ccache;

    ret = krb5_ret_stringz(request, &name);
    if (ret)
	return ret;

    KCM_LOG_REQUEST_NAME(context, client, opcode, name);

    ret = kcm_ccache_resolve_client(context, client, opcode,
				    name, &ccache);
    if (ret) {
	free(name);
	return ret;
    }

    ret = krb5_store_stringz(response, ccache->name);
    if (ret) {
	kcm_release_ccache(context, ccache);
	free(name);
	return ret;
    }

    free(name);
    kcm_release_ccache(context, ccache);
    return 0;
}

/*
 * Request:
 *
 * Response:
 *	NameZ
 */
static krb5_error_code
kcm_op_gen_new(krb5_context context,
	       kcm_client *client,
	       kcm_operation opcode,
	       krb5_storage *request,
	       krb5_storage *response)
{
    krb5_error_code ret;
    char *name;

    KCM_LOG_REQUEST(context, client, opcode);

    name = kcm_ccache_nextid(client->pid, client->uid, client->gid);
    if (name == NULL) {
	return KRB5_CC_NOMEM;
    }

    ret = krb5_store_stringz(response, name);
    free(name);

    return ret;
}

/*
 * Request:
 *	NameZ
 *	Principal
 *
 * Response:
 *
 */
static krb5_error_code
kcm_op_initialize(krb5_context context,
		  kcm_client *client,
		  kcm_operation opcode,
		  krb5_storage *request,
		  krb5_storage *response)
{
    kcm_ccache ccache;
    krb5_principal principal;
    krb5_error_code ret;
    char *name;
#if 0
    kcm_event event;
#endif

    KCM_LOG_REQUEST(context, client, opcode);

    ret = krb5_ret_stringz(request, &name);
    if (ret)
	return ret;

    ret = krb5_ret_principal(request, &principal);
    if (ret) {
	free(name);
	return ret;
    }

    ret = kcm_ccache_new_client(context, client, name, &ccache);
    if (ret) {
	free(name);
	krb5_free_principal(context, principal);
	return ret;
    }

    ccache->client = principal;

    free(name);

#if 0
    /*
     * Create a new credentials cache. To mitigate DoS attacks we will
     * expire it in 30 minutes unless it has some credentials added
     * to it
     */

    event.fire_time = 30 * 60;
    event.expire_time = 0;
    event.backoff_time = 0;
    event.action = KCM_EVENT_DESTROY_EMPTY_CACHE;
    event.ccache = ccache;

    ret = kcm_enqueue_event_relative(context, &event);
#endif

    kcm_release_ccache(context, ccache);

    return ret;
}

/*
 * Request:
 *	NameZ
 *
 * Response:
 *
 */
static krb5_error_code
kcm_op_destroy(krb5_context context,
	       kcm_client *client,
	       kcm_operation opcode,
	       krb5_storage *request,
	       krb5_storage *response)
{
    krb5_error_code ret;
    char *name;

    ret = krb5_ret_stringz(request, &name);
    if (ret)
	return ret;

    KCM_LOG_REQUEST_NAME(context, client, opcode, name);

    ret = kcm_ccache_destroy_client(context, client, name);
    if (ret == 0)
	kcm_drop_default_cache(context, client, name);

    free(name);

    return ret;
}

/*
 * Request:
 *	NameZ
 *	Creds
 *
 * Response:
 *
 */
static krb5_error_code
kcm_op_store(krb5_context context,
	     kcm_client *client,
	     kcm_operation opcode,
	     krb5_storage *request,
	     krb5_storage *response)
{
    krb5_creds creds;
    krb5_error_code ret;
    kcm_ccache ccache;
    char *name;

    ret = krb5_ret_stringz(request, &name);
    if (ret)
	return ret;

    KCM_LOG_REQUEST_NAME(context, client, opcode, name);

    ret = krb5_ret_creds(request, &creds);
    if (ret) {
	free(name);
	return ret;
    }

    ret = kcm_ccache_resolve_client(context, client, opcode,
				    name, &ccache);
    if (ret) {
	free(name);
	krb5_free_cred_contents(context, &creds);
	return ret;
    }

    ret = kcm_ccache_store_cred(context, ccache, &creds, 0);
    if (ret) {
	free(name);
	krb5_free_cred_contents(context, &creds);
	kcm_release_ccache(context, ccache);
	return ret;
    }

    kcm_ccache_enqueue_default(context, ccache, &creds);

    free(name);
    kcm_release_ccache(context, ccache);

    return 0;
}

/*
 * Request:
 *	NameZ
 *	WhichFields
 *	MatchCreds
 *
 * Response:
 *	Creds
 *
 */
static krb5_error_code
kcm_op_retrieve(krb5_context context,
		kcm_client *client,
		kcm_operation opcode,
		krb5_storage *request,
		krb5_storage *response)
{
    uint32_t flags;
    krb5_creds mcreds;
    krb5_error_code ret;
    kcm_ccache ccache;
    char *name;
    krb5_creds *credp;
    int free_creds = 0;

    ret = krb5_ret_stringz(request, &name);
    if (ret)
	return ret;

    KCM_LOG_REQUEST_NAME(context, client, opcode, name);

    ret = krb5_ret_uint32(request, &flags);
    if (ret) {
	free(name);
	return ret;
    }

    ret = krb5_ret_creds_tag(request, &mcreds);
    if (ret) {
	free(name);
	return ret;
    }

    if (disallow_getting_krbtgt &&
	mcreds.server->name.name_string.len == 2 &&
	strcmp(mcreds.server->name.name_string.val[0], KRB5_TGS_NAME) == 0)
    {
	free(name);
	krb5_free_cred_contents(context, &mcreds);
	return KRB5_FCC_PERM;
    }

    ret = kcm_ccache_resolve_client(context, client, opcode,
				    name, &ccache);
    if (ret) {
	free(name);
	krb5_free_cred_contents(context, &mcreds);
	return ret;
    }

    ret = kcm_ccache_retrieve_cred(context, ccache, flags,
				   &mcreds, &credp);
    if (ret && ((flags & KRB5_GC_CACHED) == 0) &&
	!krb5_is_config_principal(context, mcreds.server)) {
	krb5_ccache_data ccdata;

	/* try and acquire */
	HEIMDAL_MUTEX_lock(&ccache->mutex);

	/* Fake up an internal ccache */
	kcm_internal_ccache(context, ccache, &ccdata);

	/* glue cc layer will store creds */
	ret = krb5_get_credentials(context, 0, &ccdata, &mcreds, &credp);
	if (ret == 0)
	    free_creds = 1;

	HEIMDAL_MUTEX_unlock(&ccache->mutex);
    }

    if (ret == 0) {
	ret = krb5_store_creds(response, credp);
    }

    free(name);
    krb5_free_cred_contents(context, &mcreds);
    kcm_release_ccache(context, ccache);

    if (free_creds)
	krb5_free_cred_contents(context, credp);

    return ret;
}

/*
 * Request:
 *	NameZ
 *
 * Response:
 *	Principal
 */
static krb5_error_code
kcm_op_get_principal(krb5_context context,
		     kcm_client *client,
		     kcm_operation opcode,
		     krb5_storage *request,
		     krb5_storage *response)
{
    krb5_error_code ret;
    kcm_ccache ccache;
    char *name;

    ret = krb5_ret_stringz(request, &name);
    if (ret)
	return ret;

    KCM_LOG_REQUEST_NAME(context, client, opcode, name);

    ret = kcm_ccache_resolve_client(context, client, opcode,
				    name, &ccache);
    if (ret) {
	free(name);
	return ret;
    }

    if (ccache->client == NULL)
	ret = KRB5_CC_NOTFOUND;
    else
	ret = krb5_store_principal(response, ccache->client);

    free(name);
    kcm_release_ccache(context, ccache);

    return 0;
}

/*
 * Request:
 *	NameZ
 *
 * Response:
 *	UUIDs
 *
 */
static krb5_error_code
kcm_op_get_cred_uuid_list(krb5_context context,
			  kcm_client *client,
			  kcm_operation opcode,
			  krb5_storage *request,
			  krb5_storage *response)
{
    struct kcm_creds *creds;
    krb5_error_code ret;
    kcm_ccache ccache;
    char *name;

    ret = krb5_ret_stringz(request, &name);
    if (ret)
	return ret;

    KCM_LOG_REQUEST_NAME(context, client, opcode, name);

    ret = kcm_ccache_resolve_client(context, client, opcode,
				    name, &ccache);
    free(name);
    if (ret)
	return ret;

    for (creds = ccache->creds ; creds ; creds = creds->next) {
	ssize_t sret;
	sret = krb5_storage_write(response, &creds->uuid, sizeof(creds->uuid));
	if (sret != sizeof(creds->uuid)) {
	    ret = ENOMEM;
	    break;
	}
    }

    kcm_release_ccache(context, ccache);

    return ret;
}

/*
 * Request:
 *	NameZ
 *	Cursor
 *
 * Response:
 *	Creds
 */
static krb5_error_code
kcm_op_get_cred_by_uuid(krb5_context context,
			kcm_client *client,
			kcm_operation opcode,
			krb5_storage *request,
			krb5_storage *response)
{
    krb5_error_code ret;
    kcm_ccache ccache;
    char *name;
    struct kcm_creds *c;
    kcmuuid_t uuid;
    ssize_t sret;

    ret = krb5_ret_stringz(request, &name);
    if (ret)
	return ret;

    KCM_LOG_REQUEST_NAME(context, client, opcode, name);

    ret = kcm_ccache_resolve_client(context, client, opcode,
				    name, &ccache);
    free(name);
    if (ret)
	return ret;

    sret = krb5_storage_read(request, &uuid, sizeof(uuid));
    if (sret != sizeof(uuid)) {
	kcm_release_ccache(context, ccache);
	krb5_clear_error_message(context);
	return KRB5_CC_IO;
    }

    c = kcm_ccache_find_cred_uuid(context, ccache, uuid);
    if (c == NULL) {
	kcm_release_ccache(context, ccache);
	return KRB5_CC_END;
    }

    HEIMDAL_MUTEX_lock(&ccache->mutex);
    ret = krb5_store_creds(response, &c->cred);
    HEIMDAL_MUTEX_unlock(&ccache->mutex);

    kcm_release_ccache(context, ccache);

    return ret;
}

/*
 * Request:
 *	NameZ
 *	WhichFields
 *	MatchCreds
 *
 * Response:
 *
 */
static krb5_error_code
kcm_op_remove_cred(krb5_context context,
		   kcm_client *client,
		   kcm_operation opcode,
		   krb5_storage *request,
		   krb5_storage *response)
{
    uint32_t whichfields;
    krb5_creds mcreds;
    krb5_error_code ret;
    kcm_ccache ccache;
    char *name;

    ret = krb5_ret_stringz(request, &name);
    if (ret)
	return ret;

    KCM_LOG_REQUEST_NAME(context, client, opcode, name);

    ret = krb5_ret_uint32(request, &whichfields);
    if (ret) {
	free(name);
	return ret;
    }

    ret = krb5_ret_creds_tag(request, &mcreds);
    if (ret) {
	free(name);
	return ret;
    }

    ret = kcm_ccache_resolve_client(context, client, opcode,
				    name, &ccache);
    if (ret) {
	free(name);
	krb5_free_cred_contents(context, &mcreds);
	return ret;
    }

    ret = kcm_ccache_remove_cred(context, ccache, whichfields, &mcreds);

    /* XXX need to remove any events that match */

    free(name);
    krb5_free_cred_contents(context, &mcreds);
    kcm_release_ccache(context, ccache);

    return ret;
}

/*
 * Request:
 *	NameZ
 *	Flags
 *
 * Response:
 *
 */
static krb5_error_code
kcm_op_set_flags(krb5_context context,
		 kcm_client *client,
		 kcm_operation opcode,
		 krb5_storage *request,
		 krb5_storage *response)
{
    uint32_t flags;
    krb5_error_code ret;
    kcm_ccache ccache;
    char *name;

    ret = krb5_ret_stringz(request, &name);
    if (ret)
	return ret;

    KCM_LOG_REQUEST_NAME(context, client, opcode, name);

    ret = krb5_ret_uint32(request, &flags);
    if (ret) {
	free(name);
	return ret;
    }

    ret = kcm_ccache_resolve_client(context, client, opcode,
				    name, &ccache);
    if (ret) {
	free(name);
	return ret;
    }

    /* we don't really support any flags yet */
    free(name);
    kcm_release_ccache(context, ccache);

    return 0;
}

/*
 * Request:
 *	NameZ
 *	UID
 *	GID
 *
 * Response:
 *
 */
static krb5_error_code
kcm_op_chown(krb5_context context,
	     kcm_client *client,
	     kcm_operation opcode,
	     krb5_storage *request,
	     krb5_storage *response)
{
    uint32_t uid;
    uint32_t gid;
    krb5_error_code ret;
    kcm_ccache ccache;
    char *name;

    ret = krb5_ret_stringz(request, &name);
    if (ret)
	return ret;

    KCM_LOG_REQUEST_NAME(context, client, opcode, name);

    ret = krb5_ret_uint32(request, &uid);
    if (ret) {
	free(name);
	return ret;
    }

    ret = krb5_ret_uint32(request, &gid);
    if (ret) {
	free(name);
	return ret;
    }

    ret = kcm_ccache_resolve_client(context, client, opcode,
				    name, &ccache);
    if (ret) {
	free(name);
	return ret;
    }

    ret = kcm_chown(context, client, ccache, uid, gid);

    free(name);
    kcm_release_ccache(context, ccache);

    return ret;
}

/*
 * Request:
 *	NameZ
 *	Mode
 *
 * Response:
 *
 */
static krb5_error_code
kcm_op_chmod(krb5_context context,
	     kcm_client *client,
	     kcm_operation opcode,
	     krb5_storage *request,
	     krb5_storage *response)
{
    uint16_t mode;
    krb5_error_code ret;
    kcm_ccache ccache;
    char *name;

    ret = krb5_ret_stringz(request, &name);
    if (ret)
	return ret;

    KCM_LOG_REQUEST_NAME(context, client, opcode, name);

    ret = krb5_ret_uint16(request, &mode);
    if (ret) {
	free(name);
	return ret;
    }

    ret = kcm_ccache_resolve_client(context, client, opcode,
				    name, &ccache);
    if (ret) {
	free(name);
	return ret;
    }

    ret = kcm_chmod(context, client, ccache, mode);

    free(name);
    kcm_release_ccache(context, ccache);

    return ret;
}

/*
 * Protocol extensions for moving ticket acquisition responsibility
 * from client to KCM follow.
 */

/*
 * Request:
 *	NameZ
 *	ServerPrincipalPresent
 *	ServerPrincipal OPTIONAL
 *	Key
 *
 * Repsonse:
 *
 */
static krb5_error_code
kcm_op_get_initial_ticket(krb5_context context,
			  kcm_client *client,
			  kcm_operation opcode,
			  krb5_storage *request,
			  krb5_storage *response)
{
    krb5_error_code ret;
    kcm_ccache ccache;
    char *name;
    int8_t not_tgt = 0;
    krb5_principal server = NULL;
    krb5_keyblock key;

    krb5_keyblock_zero(&key);

    ret = krb5_ret_stringz(request, &name);
    if (ret)
	return ret;

    KCM_LOG_REQUEST_NAME(context, client, opcode, name);

    ret = krb5_ret_int8(request, &not_tgt);
    if (ret) {
	free(name);
	return ret;
    }

    if (not_tgt) {
	ret = krb5_ret_principal(request, &server);
	if (ret) {
	    free(name);
	    return ret;
	}
    }

    ret = krb5_ret_keyblock(request, &key);
    if (ret) {
	free(name);
	if (server != NULL)
	    krb5_free_principal(context, server);
	return ret;
    }

    ret = kcm_ccache_resolve_client(context, client, opcode,
				    name, &ccache);
    if (ret == 0) {
	HEIMDAL_MUTEX_lock(&ccache->mutex);

	if (ccache->server != NULL) {
	    krb5_free_principal(context, ccache->server);
	    ccache->server = NULL;
	}

	krb5_free_keyblock(context, &ccache->key.keyblock);

	ccache->server = server;
	ccache->key.keyblock = key;
    	ccache->flags |= KCM_FLAGS_USE_CACHED_KEY;

	ret = kcm_ccache_enqueue_default(context, ccache, NULL);
	if (ret) {
	    ccache->server = NULL;
	    krb5_keyblock_zero(&ccache->key.keyblock);
	    ccache->flags &= ~(KCM_FLAGS_USE_CACHED_KEY);
	}

	HEIMDAL_MUTEX_unlock(&ccache->mutex);
    }

    free(name);

    if (ret != 0) {
	krb5_free_principal(context, server);
	krb5_free_keyblock(context, &key);
    }

    kcm_release_ccache(context, ccache);

    return ret;
}

/*
 * Request:
 *	NameZ
 *	ServerPrincipal
 *	KDCFlags
 *	EncryptionType
 *
 * Repsonse:
 *
 */
static krb5_error_code
kcm_op_get_ticket(krb5_context context,
		  kcm_client *client,
		  kcm_operation opcode,
		  krb5_storage *request,
		  krb5_storage *response)
{
    krb5_error_code ret;
    kcm_ccache ccache;
    char *name;
    krb5_principal server = NULL;
    krb5_ccache_data ccdata;
    krb5_creds in, *out;
    krb5_kdc_flags flags;

    memset(&in, 0, sizeof(in));

    ret = krb5_ret_stringz(request, &name);
    if (ret)
	return ret;

    KCM_LOG_REQUEST_NAME(context, client, opcode, name);

    ret = krb5_ret_uint32(request, &flags.i);
    if (ret) {
	free(name);
	return ret;
    }

    ret = krb5_ret_int32(request, &in.session.keytype);
    if (ret) {
	free(name);
	return ret;
    }

    ret = krb5_ret_principal(request, &server);
    if (ret) {
	free(name);
	return ret;
    }

    ret = kcm_ccache_resolve_client(context, client, opcode,
				    name, &ccache);
    if (ret) {
	krb5_free_principal(context, server);
	free(name);
	return ret;
    }

    HEIMDAL_MUTEX_lock(&ccache->mutex);

    /* Fake up an internal ccache */
    kcm_internal_ccache(context, ccache, &ccdata);

    in.client = ccache->client;
    in.server = server;
    in.times.endtime = 0;

    /* glue cc layer will store creds */
    ret = krb5_get_credentials_with_flags(context, 0, flags,
					  &ccdata, &in, &out);

    HEIMDAL_MUTEX_unlock(&ccache->mutex);

    krb5_free_principal(context, server);

    if (ret == 0)
	krb5_free_cred_contents(context, out);

    kcm_release_ccache(context, ccache);
    free(name);

    return ret;
}

/*
 * Request:
 *	OldNameZ
 *	NewNameZ
 *
 * Repsonse:
 *
 */
static krb5_error_code
kcm_op_move_cache(krb5_context context,
		  kcm_client *client,
		  kcm_operation opcode,
		  krb5_storage *request,
		  krb5_storage *response)
{
    krb5_error_code ret;
    kcm_ccache oldid, newid;
    char *oldname, *newname;

    ret = krb5_ret_stringz(request, &oldname);
    if (ret)
	return ret;

    KCM_LOG_REQUEST_NAME(context, client, opcode, oldname);

    ret = krb5_ret_stringz(request, &newname);
    if (ret) {
	free(oldname);
	return ret;
    }

    /* move to ourself is simple, done! */
    if (strcmp(oldname, newname) == 0) {
	free(oldname);
	free(newname);
	return 0;
    }

    ret = kcm_ccache_resolve_client(context, client, opcode, oldname, &oldid);
    if (ret) {
	free(oldname);
	free(newname);
	return ret;
    }

    /* Check if new credential cache exists, if not create one. */
    ret = kcm_ccache_resolve_client(context, client, opcode, newname, &newid);
    if (ret == KRB5_FCC_NOFILE)
	ret = kcm_ccache_new_client(context, client, newname, &newid);
    free(newname);

    if (ret) {
	free(oldname);
	kcm_release_ccache(context, oldid);
	return ret;
    }

    HEIMDAL_MUTEX_lock(&oldid->mutex);
    HEIMDAL_MUTEX_lock(&newid->mutex);

    /* move content */
    {
	kcm_ccache_data tmp;

#define MOVE(n,o,f) { tmp.f = n->f ; n->f = o->f; o->f = tmp.f; }

	MOVE(newid, oldid, flags);
	MOVE(newid, oldid, client);
	MOVE(newid, oldid, server);
	MOVE(newid, oldid, creds);
	MOVE(newid, oldid, tkt_life);
	MOVE(newid, oldid, renew_life);
	MOVE(newid, oldid, key);
	MOVE(newid, oldid, kdc_offset);
#undef MOVE
    }

    HEIMDAL_MUTEX_unlock(&oldid->mutex);
    HEIMDAL_MUTEX_unlock(&newid->mutex);

    kcm_release_ccache(context, oldid);
    kcm_release_ccache(context, newid);

    ret = kcm_ccache_destroy_client(context, client, oldname);
    if (ret == 0)
	kcm_drop_default_cache(context, client, oldname);

    free(oldname);

    return ret;
}

static krb5_error_code
kcm_op_get_cache_uuid_list(krb5_context context,
			   kcm_client *client,
			   kcm_operation opcode,
			   krb5_storage *request,
			   krb5_storage *response)
{
    KCM_LOG_REQUEST(context, client, opcode);

    return kcm_ccache_get_uuids(context, client, opcode, response);
}

static krb5_error_code
kcm_op_get_cache_by_uuid(krb5_context context,
			 kcm_client *client,
			 kcm_operation opcode,
			 krb5_storage *request,
			 krb5_storage *response)
{
    krb5_error_code ret;
    kcmuuid_t uuid;
    ssize_t sret;
    kcm_ccache cache;

    KCM_LOG_REQUEST(context, client, opcode);

    sret = krb5_storage_read(request, &uuid, sizeof(uuid));
    if (sret != sizeof(uuid)) {
	krb5_clear_error_message(context);
	return KRB5_CC_IO;
    }

    ret = kcm_ccache_resolve_by_uuid(context, uuid, &cache);
    if (ret)
	return ret;

    ret = kcm_access(context, client, opcode, cache);
    if (ret)
	ret = KRB5_FCC_NOFILE;

    if (ret == 0)
	ret = krb5_store_stringz(response, cache->name);

    kcm_release_ccache(context, cache);

    return ret;
}

struct kcm_default_cache *default_caches;

static krb5_error_code
kcm_op_get_default_cache(krb5_context context,
			 kcm_client *client,
			 kcm_operation opcode,
			 krb5_storage *request,
			 krb5_storage *response)
{
    struct kcm_default_cache *c;
    krb5_error_code ret;
    const char *name = NULL;
    char *n = NULL;

    KCM_LOG_REQUEST(context, client, opcode);

    for (c = default_caches; c != NULL; c = c->next) {
	if (kcm_is_same_session(client, c->uid, c->session)) {
	    name = c->name;
	    break;
	}
    }
    if (name == NULL)
	name = n = kcm_ccache_first_name(client);

    if (name == NULL) {
	asprintf(&n, "%d", (int)client->uid);
	name = n;
    }
    if (name == NULL)
	return ENOMEM;
    ret = krb5_store_stringz(response, name);
    if (n)
	free(n);
    return ret;
}

static void
kcm_drop_default_cache(krb5_context context, kcm_client *client, char *name)
{
    struct kcm_default_cache **c;

    for (c = &default_caches; *c != NULL; c = &(*c)->next) {
	if (!kcm_is_same_session(client, (*c)->uid, (*c)->session))
	    continue;
	if (strcmp((*c)->name, name) == 0) {
	    struct kcm_default_cache *h = *c;
	    *c = (*c)->next;
	    free(h->name);
	    free(h);
	    break;
	}
    }
}

static krb5_error_code
kcm_op_set_default_cache(krb5_context context,
			 kcm_client *client,
			 kcm_operation opcode,
			 krb5_storage *request,
			 krb5_storage *response)
{
    struct kcm_default_cache *c;
    krb5_error_code ret;
    char *name;

    ret = krb5_ret_stringz(request, &name);
    if (ret)
	return ret;

    KCM_LOG_REQUEST_NAME(context, client, opcode, name);

    for (c = default_caches; c != NULL; c = c->next) {
	if (kcm_is_same_session(client, c->uid, c->session))
	    break;
    }
    if (c == NULL) {
	c = malloc(sizeof(*c));
	if (c == NULL)
	    return ENOMEM;
	c->session = client->session;
	c->uid = client->uid;
	c->name = strdup(name);

	c->next = default_caches;
	default_caches = c;
    } else {
	free(c->name);
	c->name = strdup(name);
    }

    return 0;
}

static krb5_error_code
kcm_op_get_kdc_offset(krb5_context context,
		      kcm_client *client,
		      kcm_operation opcode,
		      krb5_storage *request,
		      krb5_storage *response)
{
    krb5_error_code ret;
    kcm_ccache ccache;
    char *name;

    ret = krb5_ret_stringz(request, &name);
    if (ret)
	return ret;

    KCM_LOG_REQUEST_NAME(context, client, opcode, name);

    ret = kcm_ccache_resolve_client(context, client, opcode, name, &ccache);
    free(name);
    if (ret)
	return ret;

    HEIMDAL_MUTEX_lock(&ccache->mutex);
    ret = krb5_store_int32(response, ccache->kdc_offset);
    HEIMDAL_MUTEX_unlock(&ccache->mutex);

    kcm_release_ccache(context, ccache);

    return ret;
}

static krb5_error_code
kcm_op_set_kdc_offset(krb5_context context,
		      kcm_client *client,
		      kcm_operation opcode,
		      krb5_storage *request,
		      krb5_storage *response)
{
    krb5_error_code ret;
    kcm_ccache ccache;
    int32_t offset;
    char *name;

    ret = krb5_ret_stringz(request, &name);
    if (ret)
	return ret;

    KCM_LOG_REQUEST_NAME(context, client, opcode, name);

    ret = krb5_ret_int32(request, &offset);
    if (ret) {
	free(name);
	return ret;
    }

    ret = kcm_ccache_resolve_client(context, client, opcode, name, &ccache);
    free(name);
    if (ret)
	return ret;

    HEIMDAL_MUTEX_lock(&ccache->mutex);
    ccache->kdc_offset = offset;
    HEIMDAL_MUTEX_unlock(&ccache->mutex);

    kcm_release_ccache(context, ccache);

    return ret;
}

struct kcm_ntlm_cred {
    kcmuuid_t uuid;
    char *user;
    char *domain;
    krb5_data nthash;
    uid_t uid;
    pid_t session;
    struct kcm_ntlm_cred *next;
};

static struct kcm_ntlm_cred *ntlm_head;

static void
free_cred(struct kcm_ntlm_cred *cred)
{
    free(cred->user);
    free(cred->domain);
    krb5_data_free(&cred->nthash);
    free(cred);
}


/*
 * name
 * domain
 * ntlm hash
 *
 * Reply:
 *   uuid
 */

static struct kcm_ntlm_cred *
find_ntlm_cred(const char *user, const char *domain, kcm_client *client)
{
    struct kcm_ntlm_cred *c;

    for (c = ntlm_head; c != NULL; c = c->next)
	if ((user[0] == '\0' || strcmp(user, c->user) == 0) &&
	    (domain == NULL || strcmp(domain, c->domain) == 0) &&
	    kcm_is_same_session(client, c->uid, c->session))
	    return c;

    return NULL;
}

static krb5_error_code
kcm_op_add_ntlm_cred(krb5_context context,
		     kcm_client *client,
		     kcm_operation opcode,
		     krb5_storage *request,
		     krb5_storage *response)
{
    struct kcm_ntlm_cred *cred, *c;
    krb5_error_code ret;

    cred = calloc(1, sizeof(*cred));
    if (cred == NULL)
	return ENOMEM;

    RAND_bytes(cred->uuid, sizeof(cred->uuid));

    ret = krb5_ret_stringz(request, &cred->user);
    if (ret)
	goto error;

    ret = krb5_ret_stringz(request, &cred->domain);
    if (ret)
	goto error;

    ret = krb5_ret_data(request, &cred->nthash);
    if (ret)
	goto error;

    /* search for dups */
    c = find_ntlm_cred(cred->user, cred->domain, client);
    if (c) {
	krb5_data hash = c->nthash;
	c->nthash = cred->nthash;
	cred->nthash = hash;
	free_cred(cred);
	cred = c;
    } else {
	cred->next = ntlm_head;
	ntlm_head = cred;
    }

    cred->uid = client->uid;
    cred->session = client->session;

    /* write response */
    (void)krb5_storage_write(response, &cred->uuid, sizeof(cred->uuid));

    return 0;

 error:
    free_cred(cred);

    return ret;
}

/*
 * { "HAVE_NTLM_CRED",		NULL },
 *
 * input:
 *  name
 *  domain
 */

static krb5_error_code
kcm_op_have_ntlm_cred(krb5_context context,
		     kcm_client *client,
		     kcm_operation opcode,
		     krb5_storage *request,
		     krb5_storage *response)
{
    struct kcm_ntlm_cred *c;
    char *user = NULL, *domain = NULL;
    krb5_error_code ret;

    ret = krb5_ret_stringz(request, &user);
    if (ret)
	goto error;

    ret = krb5_ret_stringz(request, &domain);
    if (ret)
	goto error;

    if (domain[0] == '\0') {
	free(domain);
	domain = NULL;
    }

    c = find_ntlm_cred(user, domain, client);
    if (c == NULL)
	ret = ENOENT;

 error:
    free(user);
    if (domain)
	free(domain);

    return ret;
}

/*
 * { "DEL_NTLM_CRED",		NULL },
 *
 * input:
 *  name
 *  domain
 */

static krb5_error_code
kcm_op_del_ntlm_cred(krb5_context context,
		     kcm_client *client,
		     kcm_operation opcode,
		     krb5_storage *request,
		     krb5_storage *response)
{
    struct kcm_ntlm_cred **cp, *c;
    char *user = NULL, *domain = NULL;
    krb5_error_code ret;

    ret = krb5_ret_stringz(request, &user);
    if (ret)
	goto error;

    ret = krb5_ret_stringz(request, &domain);
    if (ret)
	goto error;

    for (cp = &ntlm_head; *cp != NULL; cp = &(*cp)->next) {
	if (strcmp(user, (*cp)->user) == 0 && strcmp(domain, (*cp)->domain) == 0 &&
	    kcm_is_same_session(client, (*cp)->uid, (*cp)->session))
	{
	    c = *cp;
	    *cp = c->next;

	    free_cred(c);
	    break;
	}
    }

 error:
    free(user);
    free(domain);

    return ret;
}

/*
 * { "DO_NTLM_AUTH",		NULL },
 *
 * input:
 *  name:string
 *  domain:string
 *  type2:data
 *
 * reply:
 *  type3:data
 *  flags:int32
 *  session-key:data
 */

#define NTLM_FLAG_SESSIONKEY 1
#define NTLM_FLAG_NTLM2_SESSION 2
#define NTLM_FLAG_KEYEX 4

static krb5_error_code
kcm_op_do_ntlm(krb5_context context,
	       kcm_client *client,
	       kcm_operation opcode,
	       krb5_storage *request,
	       krb5_storage *response)
{
    struct kcm_ntlm_cred *c;
    struct ntlm_type2 type2;
    struct ntlm_type3 type3;
    char *user = NULL, *domain = NULL;
    struct ntlm_buf ndata, sessionkey;
    krb5_data data;
    krb5_error_code ret;
    uint32_t flags = 0;

    memset(&type2, 0, sizeof(type2));
    memset(&type3, 0, sizeof(type3));
    sessionkey.data = NULL;
    sessionkey.length = 0;

    ret = krb5_ret_stringz(request, &user);
    if (ret)
	goto error;

    ret = krb5_ret_stringz(request, &domain);
    if (ret)
	goto error;

    if (domain[0] == '\0') {
	free(domain);
	domain = NULL;
    }

    c = find_ntlm_cred(user, domain, client);
    if (c == NULL) {
	ret = EINVAL;
	goto error;
    }

    ret = krb5_ret_data(request, &data);
    if (ret)
	goto error;

    ndata.data = data.data;
    ndata.length = data.length;

    ret = heim_ntlm_decode_type2(&ndata, &type2);
    krb5_data_free(&data);
    if (ret)
	goto error;

    if (domain && strcmp(domain, type2.targetname) == 0) {
	ret = EINVAL;
	goto error;
    }

    type3.username = c->user;
    type3.flags = type2.flags;
    type3.targetname = type2.targetname;
    type3.ws = rk_UNCONST("workstation");

    /*
     * NTLM Version 1 if no targetinfo buffer.
     */

    if (1 || type2.targetinfo.length == 0) {
	struct ntlm_buf sessionkey;

	if (type2.flags & NTLM_NEG_NTLM2_SESSION) {
	    unsigned char nonce[8];

	    if (RAND_bytes(nonce, sizeof(nonce)) != 1) {
		ret = EINVAL;
		goto error;
	    }

	    ret = heim_ntlm_calculate_ntlm2_sess(nonce,
						 type2.challenge,
						 c->nthash.data,
						 &type3.lm,
						 &type3.ntlm);
	} else {
	    ret = heim_ntlm_calculate_ntlm1(c->nthash.data,
					    c->nthash.length,
					    type2.challenge,
					    &type3.ntlm);

	}
	if (ret)
	    goto error;

	ret = heim_ntlm_build_ntlm1_master(c->nthash.data,
					   c->nthash.length,
					   &sessionkey,
					   &type3.sessionkey);
	if (ret) {
	    if (type3.lm.data)
		free(type3.lm.data);
	    if (type3.ntlm.data)
		free(type3.ntlm.data);
	    goto error;
	}

	free(sessionkey.data);
	if (ret) {
	    if (type3.lm.data)
		free(type3.lm.data);
	    if (type3.ntlm.data)
		free(type3.ntlm.data);
	    goto error;
	}
	flags |= NTLM_FLAG_SESSIONKEY;
#if 0
    } else {
	struct ntlm_buf sessionkey;
	unsigned char ntlmv2[16];
	struct ntlm_targetinfo ti;

	/* verify infotarget */

	ret = heim_ntlm_decode_targetinfo(&type2.targetinfo, 1, &ti);
	if(ret) {
	    _gss_ntlm_delete_sec_context(minor_status,
					 context_handle, NULL);
	    *minor_status = ret;
	    return GSS_S_FAILURE;
	}

	if (ti.domainname && strcmp(ti.domainname, name->domain) != 0) {
	    _gss_ntlm_delete_sec_context(minor_status,
					 context_handle, NULL);
	    *minor_status = EINVAL;
	    return GSS_S_FAILURE;
	}

	ret = heim_ntlm_calculate_ntlm2(ctx->client->key.data,
					ctx->client->key.length,
					type3.username,
					name->domain,
					type2.challenge,
					&type2.targetinfo,
					ntlmv2,
					&type3.ntlm);
	if (ret) {
	    _gss_ntlm_delete_sec_context(minor_status,
					 context_handle, NULL);
	    *minor_status = ret;
	    return GSS_S_FAILURE;
	}

	ret = heim_ntlm_build_ntlm1_master(ntlmv2, sizeof(ntlmv2),
					   &sessionkey,
					   &type3.sessionkey);
	memset(ntlmv2, 0, sizeof(ntlmv2));
	if (ret) {
	    _gss_ntlm_delete_sec_context(minor_status,
					 context_handle, NULL);
	    *minor_status = ret;
	    return GSS_S_FAILURE;
	}

	flags |= NTLM_FLAG_NTLM2_SESSION |
	         NTLM_FLAG_SESSION;

	if (type3.flags & NTLM_NEG_KEYEX)
	    flags |= NTLM_FLAG_KEYEX;

	ret = krb5_data_copy(&ctx->sessionkey,
			     sessionkey.data, sessionkey.length);
	free(sessionkey.data);
	if (ret) {
	    _gss_ntlm_delete_sec_context(minor_status,
					 context_handle, NULL);
	    *minor_status = ret;
	    return GSS_S_FAILURE;
	}
#endif
    }

#if 0
    if (flags & NTLM_FLAG_NTLM2_SESSION) {
	_gss_ntlm_set_key(&ctx->u.v2.send, 0, (ctx->flags & NTLM_NEG_KEYEX),
			  ctx->sessionkey.data,
			  ctx->sessionkey.length);
	_gss_ntlm_set_key(&ctx->u.v2.recv, 1, (ctx->flags & NTLM_NEG_KEYEX),
			  ctx->sessionkey.data,
			  ctx->sessionkey.length);
    } else {
	flags |= NTLM_FLAG_SESSION;
	RC4_set_key(&ctx->u.v1.crypto_recv.key,
		    ctx->sessionkey.length,
		    ctx->sessionkey.data);
	RC4_set_key(&ctx->u.v1.crypto_send.key,
		    ctx->sessionkey.length,
		    ctx->sessionkey.data);
    }
#endif

    ret = heim_ntlm_encode_type3(&type3, &ndata);
    if (ret)
	goto error;

    data.data = ndata.data;
    data.length = ndata.length;
    ret = krb5_store_data(response, data);
    heim_ntlm_free_buf(&ndata);
    if (ret) goto error;

    ret = krb5_store_int32(response, flags);
    if (ret) goto error;

    data.data = sessionkey.data;
    data.length = sessionkey.length;

    ret = krb5_store_data(response, data);
    if (ret) goto error;

 error:
    free(type3.username);
    heim_ntlm_free_type2(&type2);
    free(user);
    if (domain)
	free(domain);

    return ret;
}


/*
 * { "GET_NTLM_UUID_LIST",	NULL }
 *
 * reply:
 *   1 user domain
 *   0 [ end of list ]
 */

static krb5_error_code
kcm_op_get_ntlm_user_list(krb5_context context,
			  kcm_client *client,
			  kcm_operation opcode,
			  krb5_storage *request,
			  krb5_storage *response)
{
    struct kcm_ntlm_cred *c;
    krb5_error_code ret;

    for (c = ntlm_head; c != NULL; c = c->next) {
	if (!kcm_is_same_session(client, c->uid, c->session))
	    continue;

	ret = krb5_store_uint32(response, 1);
	if (ret)
	    return ret;
	ret = krb5_store_stringz(response, c->user);
	if (ret)
	    return ret;
	ret = krb5_store_stringz(response, c->domain);
	if (ret)
	    return ret;
    }
    return krb5_store_uint32(response, 0);
}

/*
 *
 */

static struct kcm_op kcm_ops[] = {
    { "NOOP", 			kcm_op_noop },
    { "GET_NAME",		kcm_op_get_name },
    { "RESOLVE",		kcm_op_noop },
    { "GEN_NEW", 		kcm_op_gen_new },
    { "INITIALIZE",		kcm_op_initialize },
    { "DESTROY",		kcm_op_destroy },
    { "STORE",			kcm_op_store },
    { "RETRIEVE",		kcm_op_retrieve },
    { "GET_PRINCIPAL",		kcm_op_get_principal },
    { "GET_CRED_UUID_LIST",	kcm_op_get_cred_uuid_list },
    { "GET_CRED_BY_UUID",	kcm_op_get_cred_by_uuid },
    { "REMOVE_CRED",		kcm_op_remove_cred },
    { "SET_FLAGS",		kcm_op_set_flags },
    { "CHOWN",			kcm_op_chown },
    { "CHMOD",			kcm_op_chmod },
    { "GET_INITIAL_TICKET",	kcm_op_get_initial_ticket },
    { "GET_TICKET",		kcm_op_get_ticket },
    { "MOVE_CACHE",		kcm_op_move_cache },
    { "GET_CACHE_UUID_LIST",	kcm_op_get_cache_uuid_list },
    { "GET_CACHE_BY_UUID",	kcm_op_get_cache_by_uuid },
    { "GET_DEFAULT_CACHE",      kcm_op_get_default_cache },
    { "SET_DEFAULT_CACHE",      kcm_op_set_default_cache },
    { "GET_KDC_OFFSET",      	kcm_op_get_kdc_offset },
    { "SET_KDC_OFFSET",      	kcm_op_set_kdc_offset },
    { "ADD_NTLM_CRED",		kcm_op_add_ntlm_cred },
    { "HAVE_USER_CRED",		kcm_op_have_ntlm_cred },
    { "DEL_NTLM_CRED",		kcm_op_del_ntlm_cred },
    { "DO_NTLM_AUTH",		kcm_op_do_ntlm },
    { "GET_NTLM_USER_LIST",	kcm_op_get_ntlm_user_list }
};


const char *
kcm_op2string(kcm_operation opcode)
{
    if (opcode >= sizeof(kcm_ops)/sizeof(kcm_ops[0]))
	return "Unknown operation";

    return kcm_ops[opcode].name;
}

krb5_error_code
kcm_dispatch(krb5_context context,
	     kcm_client *client,
	     krb5_data *req_data,
	     krb5_data *resp_data)
{
    krb5_error_code ret;
    kcm_method method;
    krb5_storage *req_sp = NULL;
    krb5_storage *resp_sp = NULL;
    uint16_t opcode;

    resp_sp = krb5_storage_emem();
    if (resp_sp == NULL) {
	return ENOMEM;
    }

    if (client->pid == -1) {
	kcm_log(0, "Client had invalid process number");
	ret = KRB5_FCC_INTERNAL;
	goto out;
    }

    req_sp = krb5_storage_from_data(req_data);
    if (req_sp == NULL) {
	kcm_log(0, "Process %d: failed to initialize storage from data",
		client->pid);
	ret = KRB5_CC_IO;
	goto out;
    }

    ret = krb5_ret_uint16(req_sp, &opcode);
    if (ret) {
	kcm_log(0, "Process %d: didn't send a message", client->pid);
	goto out;
    }

    if (opcode >= sizeof(kcm_ops)/sizeof(kcm_ops[0])) {
	kcm_log(0, "Process %d: invalid operation code %d",
		client->pid, opcode);
	ret = KRB5_FCC_INTERNAL;
	goto out;
    }
    method = kcm_ops[opcode].method;
    if (method == NULL) {
	kcm_log(0, "Process %d: operation code %s not implemented",
		client->pid, kcm_op2string(opcode));
	ret = KRB5_FCC_INTERNAL;
	goto out;
    }

    /* seek past place for status code */
    krb5_storage_seek(resp_sp, 4, SEEK_SET);

    ret = (*method)(context, client, opcode, req_sp, resp_sp);

out:
    if (req_sp != NULL) {
	krb5_storage_free(req_sp);
    }

    krb5_storage_seek(resp_sp, 0, SEEK_SET);
    krb5_store_int32(resp_sp, ret);

    ret = krb5_storage_to_data(resp_sp, resp_data);
    krb5_storage_free(resp_sp);

    return ret;
}

