/*
 * Copyright (c) 2006 Kungliga Tekniska Högskolan
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

#include "krb5_locl.h"
#include "digest_asn1.h"

#ifndef HEIMDAL_SMALLER

struct krb5_digest_data {
    char *cbtype;
    char *cbbinding;

    DigestInit init;
    DigestInitReply initReply;
    DigestRequest request;
    DigestResponse response;
};

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_alloc(krb5_context context, krb5_digest *digest)
{
    krb5_digest d;

    d = calloc(1, sizeof(*d));
    if (d == NULL) {
	*digest = NULL;
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    *digest = d;

    return 0;
}

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_digest_free(krb5_digest digest)
{
    if (digest == NULL)
	return;
    free_DigestInit(&digest->init);
    free_DigestInitReply(&digest->initReply);
    free_DigestRequest(&digest->request);
    free_DigestResponse(&digest->response);
    memset(digest, 0, sizeof(*digest));
    free(digest);
    return;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_set_server_cb(krb5_context context,
			  krb5_digest digest,
			  const char *type,
			  const char *binding)
{
    if (digest->init.channel) {
	krb5_set_error_message(context, EINVAL,
			       N_("server channel binding already set", ""));
	return EINVAL;
    }
    digest->init.channel = calloc(1, sizeof(*digest->init.channel));
    if (digest->init.channel == NULL)
	goto error;

    digest->init.channel->cb_type = strdup(type);
    if (digest->init.channel->cb_type == NULL)
	goto error;

    digest->init.channel->cb_binding = strdup(binding);
    if (digest->init.channel->cb_binding == NULL)
	goto error;
    return 0;
 error:
    if (digest->init.channel) {
	free(digest->init.channel->cb_type);
	free(digest->init.channel->cb_binding);
	free(digest->init.channel);
	digest->init.channel = NULL;
    }
    krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
    return ENOMEM;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_set_type(krb5_context context,
		     krb5_digest digest,
		     const char *type)
{
    if (digest->init.type) {
	krb5_set_error_message(context, EINVAL, "client type already set");
	return EINVAL;
    }
    digest->init.type = strdup(type);
    if (digest->init.type == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_set_hostname(krb5_context context,
			 krb5_digest digest,
			 const char *hostname)
{
    if (digest->init.hostname) {
	krb5_set_error_message(context, EINVAL, "server hostname already set");
	return EINVAL;
    }
    digest->init.hostname = malloc(sizeof(*digest->init.hostname));
    if (digest->init.hostname == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    *digest->init.hostname = strdup(hostname);
    if (*digest->init.hostname == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	free(digest->init.hostname);
	digest->init.hostname = NULL;
	return ENOMEM;
    }
    return 0;
}

KRB5_LIB_FUNCTION const char * KRB5_LIB_CALL
krb5_digest_get_server_nonce(krb5_context context,
			     krb5_digest digest)
{
    return digest->initReply.nonce;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_set_server_nonce(krb5_context context,
			     krb5_digest digest,
			     const char *nonce)
{
    if (digest->request.serverNonce) {
	krb5_set_error_message(context, EINVAL, N_("nonce already set", ""));
	return EINVAL;
    }
    digest->request.serverNonce = strdup(nonce);
    if (digest->request.serverNonce == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    return 0;
}

KRB5_LIB_FUNCTION const char * KRB5_LIB_CALL
krb5_digest_get_opaque(krb5_context context,
		       krb5_digest digest)
{
    return digest->initReply.opaque;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_set_opaque(krb5_context context,
		       krb5_digest digest,
		       const char *opaque)
{
    if (digest->request.opaque) {
	krb5_set_error_message(context, EINVAL, "opaque already set");
	return EINVAL;
    }
    digest->request.opaque = strdup(opaque);
    if (digest->request.opaque == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    return 0;
}

KRB5_LIB_FUNCTION const char * KRB5_LIB_CALL
krb5_digest_get_identifier(krb5_context context,
			   krb5_digest digest)
{
    if (digest->initReply.identifier == NULL)
	return NULL;
    return *digest->initReply.identifier;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_set_identifier(krb5_context context,
			   krb5_digest digest,
			   const char *id)
{
    if (digest->request.identifier) {
	krb5_set_error_message(context, EINVAL, N_("identifier already set", ""));
	return EINVAL;
    }
    digest->request.identifier = calloc(1, sizeof(*digest->request.identifier));
    if (digest->request.identifier == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    *digest->request.identifier = strdup(id);
    if (*digest->request.identifier == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	free(digest->request.identifier);
	digest->request.identifier = NULL;
	return ENOMEM;
    }
    return 0;
}

static krb5_error_code
digest_request(krb5_context context,
	       krb5_realm realm,
	       krb5_ccache ccache,
	       krb5_key_usage usage,
	       const DigestReqInner *ireq,
	       DigestRepInner *irep)
{
    DigestREQ req;
    DigestREP rep;
    krb5_error_code ret;
    krb5_data data, data2;
    size_t size = 0;
    krb5_crypto crypto = NULL;
    krb5_auth_context ac = NULL;
    krb5_principal principal = NULL;
    krb5_ccache id = NULL;
    krb5_realm r = NULL;

    krb5_data_zero(&data);
    krb5_data_zero(&data2);
    memset(&req, 0, sizeof(req));
    memset(&rep, 0, sizeof(rep));

    if (ccache == NULL) {
	ret = krb5_cc_default(context, &id);
	if (ret)
	    goto out;
    } else
	id = ccache;

    if (realm == NULL) {
	ret = krb5_get_default_realm(context, &r);
	if (ret)
	    goto out;
    } else
	r = realm;

    /*
     *
     */

    ret = krb5_make_principal(context, &principal,
			      r, KRB5_DIGEST_NAME, r, NULL);
    if (ret)
	goto out;

    ASN1_MALLOC_ENCODE(DigestReqInner, data.data, data.length,
		       ireq, &size, ret);
    if (ret) {
	krb5_set_error_message(context, ret,
			       N_("Failed to encode digest inner request", ""));
	goto out;
    }
    if (size != data.length)
	krb5_abortx(context, "ASN.1 internal encoder error");

    ret = krb5_mk_req_exact(context, &ac,
			    AP_OPTS_USE_SUBKEY|AP_OPTS_MUTUAL_REQUIRED,
			    principal, NULL, id, &req.apReq);
    if (ret)
	goto out;

    {
	krb5_keyblock *key;

	ret = krb5_auth_con_getlocalsubkey(context, ac, &key);
	if (ret)
	    goto out;
	if (key == NULL) {
	    ret = EINVAL;
	    krb5_set_error_message(context, ret,
				   N_("Digest failed to get local subkey", ""));
	    goto out;
	}

	ret = krb5_crypto_init(context, key, 0, &crypto);
	krb5_free_keyblock (context, key);
	if (ret)
	    goto out;
    }

    ret = krb5_encrypt_EncryptedData(context, crypto, usage,
				     data.data, data.length, 0,
				     &req.innerReq);
    if (ret)
	goto out;

    krb5_data_free(&data);

    ASN1_MALLOC_ENCODE(DigestREQ, data.data, data.length,
		       &req, &size, ret);
    if (ret) {
	krb5_set_error_message(context, ret,
			       N_("Failed to encode DigestREQest", ""));
	goto out;
    }
    if (size != data.length)
	krb5_abortx(context, "ASN.1 internal encoder error");

    ret = krb5_sendto_kdc(context, &data, &r, &data2);
    if (ret)
	goto out;

    ret = decode_DigestREP(data2.data, data2.length, &rep, NULL);
    if (ret) {
	krb5_set_error_message(context, ret,
			       N_("Failed to parse digest response", ""));
	goto out;
    }

    {
	krb5_ap_rep_enc_part *repl;

	ret = krb5_rd_rep(context, ac, &rep.apRep, &repl);
	if (ret)
	    goto out;

	krb5_free_ap_rep_enc_part(context, repl);
    }
    {
	krb5_keyblock *key;

	ret = krb5_auth_con_getremotesubkey(context, ac, &key);
	if (ret)
	    goto out;
	if (key == NULL) {
	    ret = EINVAL;
	    krb5_set_error_message(context, ret,
				   N_("Digest reply have no remote subkey", ""));
	    goto out;
	}

	krb5_crypto_destroy(context, crypto);
	ret = krb5_crypto_init(context, key, 0, &crypto);
	krb5_free_keyblock (context, key);
	if (ret)
	    goto out;
    }

    krb5_data_free(&data);
    ret = krb5_decrypt_EncryptedData(context, crypto, usage,
				     &rep.innerRep, &data);
    if (ret)
	goto out;

    ret = decode_DigestRepInner(data.data, data.length, irep, NULL);
    if (ret) {
	krb5_set_error_message(context, ret,
			       N_("Failed to decode digest inner reply", ""));
	goto out;
    }

 out:
    if (ccache == NULL && id)
	krb5_cc_close(context, id);
    if (realm == NULL && r)
	free(r);
    if (crypto)
	krb5_crypto_destroy(context, crypto);
    if (ac)
	krb5_auth_con_free(context, ac);
    if (principal)
	krb5_free_principal(context, principal);

    krb5_data_free(&data);
    krb5_data_free(&data2);

    free_DigestREQ(&req);
    free_DigestREP(&rep);

    return ret;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_init_request(krb5_context context,
			 krb5_digest digest,
			 krb5_realm realm,
			 krb5_ccache ccache)
{
    DigestReqInner ireq;
    DigestRepInner irep;
    krb5_error_code ret;

    memset(&ireq, 0, sizeof(ireq));
    memset(&irep, 0, sizeof(irep));

    if (digest->init.type == NULL) {
	krb5_set_error_message(context, EINVAL,
			       N_("Type missing from init req", ""));
	return EINVAL;
    }

    ireq.element = choice_DigestReqInner_init;
    ireq.u.init = digest->init;

    ret = digest_request(context, realm, ccache,
			 KRB5_KU_DIGEST_ENCRYPT, &ireq, &irep);
    if (ret)
	goto out;

    if (irep.element == choice_DigestRepInner_error) {
	ret = irep.u.error.code;
	krb5_set_error_message(context, ret, N_("Digest init error: %s", ""),
			       irep.u.error.reason);
	goto out;
    }

    if (irep.element != choice_DigestRepInner_initReply) {
	ret = EINVAL;
	krb5_set_error_message(context, ret,
			       N_("digest reply not an initReply", ""));
	goto out;
    }

    ret = copy_DigestInitReply(&irep.u.initReply, &digest->initReply);
    if (ret) {
	krb5_set_error_message(context, ret,
			       N_("Failed to copy initReply", ""));
	goto out;
    }

 out:
    free_DigestRepInner(&irep);

    return ret;
}


KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_set_client_nonce(krb5_context context,
			     krb5_digest digest,
			     const char *nonce)
{
    if (digest->request.clientNonce) {
	krb5_set_error_message(context, EINVAL,
			       N_("clientNonce already set", ""));
	return EINVAL;
    }
    digest->request.clientNonce =
	calloc(1, sizeof(*digest->request.clientNonce));
    if (digest->request.clientNonce == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    *digest->request.clientNonce = strdup(nonce);
    if (*digest->request.clientNonce == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	free(digest->request.clientNonce);
	digest->request.clientNonce = NULL;
	return ENOMEM;
    }
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_set_digest(krb5_context context,
		       krb5_digest digest,
		       const char *dgst)
{
    if (digest->request.digest) {
	krb5_set_error_message(context, EINVAL,
			       N_("digest already set", ""));
	return EINVAL;
    }
    digest->request.digest = strdup(dgst);
    if (digest->request.digest == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_set_username(krb5_context context,
			 krb5_digest digest,
			 const char *username)
{
    if (digest->request.username) {
	krb5_set_error_message(context, EINVAL, "username already set");
	return EINVAL;
    }
    digest->request.username = strdup(username);
    if (digest->request.username == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_set_authid(krb5_context context,
		       krb5_digest digest,
		       const char *authid)
{
    if (digest->request.authid) {
	krb5_set_error_message(context, EINVAL, "authid already set");
	return EINVAL;
    }
    digest->request.authid = malloc(sizeof(*digest->request.authid));
    if (digest->request.authid == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    *digest->request.authid = strdup(authid);
    if (*digest->request.authid == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	free(digest->request.authid);
	digest->request.authid = NULL;
	return ENOMEM;
    }
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_set_authentication_user(krb5_context context,
				    krb5_digest digest,
				    krb5_principal authentication_user)
{
    krb5_error_code ret;

    if (digest->request.authentication_user) {
	krb5_set_error_message(context, EINVAL,
			       N_("authentication_user already set", ""));
	return EINVAL;
    }
    ret = krb5_copy_principal(context,
			      authentication_user,
			      &digest->request.authentication_user);
    if (ret)
	return ret;
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_set_realm(krb5_context context,
		      krb5_digest digest,
		      const char *realm)
{
    if (digest->request.realm) {
	krb5_set_error_message(context, EINVAL, "realm already set");
	return EINVAL;
    }
    digest->request.realm = malloc(sizeof(*digest->request.realm));
    if (digest->request.realm == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    *digest->request.realm = strdup(realm);
    if (*digest->request.realm == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	free(digest->request.realm);
	digest->request.realm = NULL;
	return ENOMEM;
    }
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_set_method(krb5_context context,
		       krb5_digest digest,
		       const char *method)
{
    if (digest->request.method) {
	krb5_set_error_message(context, EINVAL,
			       N_("method already set", ""));
	return EINVAL;
    }
    digest->request.method = malloc(sizeof(*digest->request.method));
    if (digest->request.method == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    *digest->request.method = strdup(method);
    if (*digest->request.method == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	free(digest->request.method);
	digest->request.method = NULL;
	return ENOMEM;
    }
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_set_uri(krb5_context context,
		    krb5_digest digest,
		    const char *uri)
{
    if (digest->request.uri) {
	krb5_set_error_message(context, EINVAL, N_("uri already set", ""));
	return EINVAL;
    }
    digest->request.uri = malloc(sizeof(*digest->request.uri));
    if (digest->request.uri == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    *digest->request.uri = strdup(uri);
    if (*digest->request.uri == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	free(digest->request.uri);
	digest->request.uri = NULL;
	return ENOMEM;
    }
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_set_nonceCount(krb5_context context,
			   krb5_digest digest,
			   const char *nonce_count)
{
    if (digest->request.nonceCount) {
	krb5_set_error_message(context, EINVAL,
			       N_("nonceCount already set", ""));
	return EINVAL;
    }
    digest->request.nonceCount =
	malloc(sizeof(*digest->request.nonceCount));
    if (digest->request.nonceCount == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    *digest->request.nonceCount = strdup(nonce_count);
    if (*digest->request.nonceCount == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	free(digest->request.nonceCount);
	digest->request.nonceCount = NULL;
	return ENOMEM;
    }
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_set_qop(krb5_context context,
		    krb5_digest digest,
		    const char *qop)
{
    if (digest->request.qop) {
	krb5_set_error_message(context, EINVAL, "qop already set");
	return EINVAL;
    }
    digest->request.qop = malloc(sizeof(*digest->request.qop));
    if (digest->request.qop == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    *digest->request.qop = strdup(qop);
    if (*digest->request.qop == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	free(digest->request.qop);
	digest->request.qop = NULL;
	return ENOMEM;
    }
    return 0;
}

KRB5_LIB_FUNCTION int KRB5_LIB_CALL
krb5_digest_set_responseData(krb5_context context,
			     krb5_digest digest,
			     const char *response)
{
    digest->request.responseData = strdup(response);
    if (digest->request.responseData == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_request(krb5_context context,
		    krb5_digest digest,
		    krb5_realm realm,
		    krb5_ccache ccache)
{
    DigestReqInner ireq;
    DigestRepInner irep;
    krb5_error_code ret;

    memset(&ireq, 0, sizeof(ireq));
    memset(&irep, 0, sizeof(irep));

    ireq.element = choice_DigestReqInner_digestRequest;
    ireq.u.digestRequest = digest->request;

    if (digest->request.type == NULL) {
	if (digest->init.type == NULL) {
	    krb5_set_error_message(context, EINVAL,
				   N_("Type missing from req", ""));
	    return EINVAL;
	}
	ireq.u.digestRequest.type = digest->init.type;
    }

    if (ireq.u.digestRequest.digest == NULL) {
	static char md5[] = "md5";
	ireq.u.digestRequest.digest = md5;
    }

    ret = digest_request(context, realm, ccache,
			 KRB5_KU_DIGEST_ENCRYPT, &ireq, &irep);
    if (ret)
	return ret;

    if (irep.element == choice_DigestRepInner_error) {
	ret = irep.u.error.code;
	krb5_set_error_message(context, ret,
			       N_("Digest response error: %s", ""),
			       irep.u.error.reason);
	goto out;
    }

    if (irep.element != choice_DigestRepInner_response) {
	krb5_set_error_message(context, EINVAL,
			       N_("digest reply not an DigestResponse", ""));
	ret = EINVAL;
	goto out;
    }

    ret = copy_DigestResponse(&irep.u.response, &digest->response);
    if (ret) {
	krb5_set_error_message(context, ret,
			       N_("Failed to copy initReply,", ""));
	goto out;
    }

 out:
    free_DigestRepInner(&irep);

    return ret;
}

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_digest_rep_get_status(krb5_context context,
			   krb5_digest digest)
{
    return digest->response.success ? TRUE : FALSE;
}

KRB5_LIB_FUNCTION const char * KRB5_LIB_CALL
krb5_digest_get_rsp(krb5_context context,
		    krb5_digest digest)
{
    if (digest->response.rsp == NULL)
	return NULL;
    return *digest->response.rsp;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_get_tickets(krb5_context context,
			krb5_digest digest,
			Ticket **tickets)
{
    *tickets = NULL;
    return 0;
}


KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_get_client_binding(krb5_context context,
			       krb5_digest digest,
			       char **type,
			       char **binding)
{
    if (digest->response.channel) {
	*type = strdup(digest->response.channel->cb_type);
	*binding = strdup(digest->response.channel->cb_binding);
	if (*type == NULL || *binding == NULL) {
	    free(*type);
	    free(*binding);
	    krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	    return ENOMEM;
	}
    } else {
	*type = NULL;
	*binding = NULL;
    }
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_get_session_key(krb5_context context,
			    krb5_digest digest,
			    krb5_data *data)
{
    krb5_error_code ret;

    krb5_data_zero(data);
    if (digest->response.session_key == NULL)
	return 0;
    ret = der_copy_octet_string(digest->response.session_key, data);
    if (ret)
	krb5_clear_error_message(context);

    return ret;
}

struct krb5_ntlm_data {
    NTLMInit init;
    NTLMInitReply initReply;
    NTLMRequest request;
    NTLMResponse response;
};

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ntlm_alloc(krb5_context context,
		krb5_ntlm *ntlm)
{
    *ntlm = calloc(1, sizeof(**ntlm));
    if (*ntlm == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ntlm_free(krb5_context context, krb5_ntlm ntlm)
{
    free_NTLMInit(&ntlm->init);
    free_NTLMInitReply(&ntlm->initReply);
    free_NTLMRequest(&ntlm->request);
    free_NTLMResponse(&ntlm->response);
    memset(ntlm, 0, sizeof(*ntlm));
    free(ntlm);
    return 0;
}


KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ntlm_init_request(krb5_context context,
		       krb5_ntlm ntlm,
		       krb5_realm realm,
		       krb5_ccache ccache,
		       uint32_t flags,
		       const char *hostname,
		       const char *domainname)
{
    DigestReqInner ireq;
    DigestRepInner irep;
    krb5_error_code ret;

    memset(&ireq, 0, sizeof(ireq));
    memset(&irep, 0, sizeof(irep));

    ntlm->init.flags = flags;
    if (hostname) {
	ALLOC(ntlm->init.hostname, 1);
	*ntlm->init.hostname = strdup(hostname);
    }
    if (domainname) {
	ALLOC(ntlm->init.domain, 1);
	*ntlm->init.domain = strdup(domainname);
    }

    ireq.element = choice_DigestReqInner_ntlmInit;
    ireq.u.ntlmInit = ntlm->init;

    ret = digest_request(context, realm, ccache,
			 KRB5_KU_DIGEST_ENCRYPT, &ireq, &irep);
    if (ret)
	goto out;

    if (irep.element == choice_DigestRepInner_error) {
	ret = irep.u.error.code;
	krb5_set_error_message(context, ret, N_("Digest init error: %s", ""),
			       irep.u.error.reason);
	goto out;
    }

    if (irep.element != choice_DigestRepInner_ntlmInitReply) {
	ret = EINVAL;
	krb5_set_error_message(context, ret,
			       N_("ntlm reply not an initReply", ""));
	goto out;
    }

    ret = copy_NTLMInitReply(&irep.u.ntlmInitReply, &ntlm->initReply);
    if (ret) {
	krb5_set_error_message(context, ret,
			       N_("Failed to copy initReply", ""));
	goto out;
    }

 out:
    free_DigestRepInner(&irep);

    return ret;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ntlm_init_get_flags(krb5_context context,
			 krb5_ntlm ntlm,
			 uint32_t *flags)
{
    *flags = ntlm->initReply.flags;
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ntlm_init_get_challange(krb5_context context,
			     krb5_ntlm ntlm,
			     krb5_data *challange)
{
    krb5_error_code ret;

    ret = der_copy_octet_string(&ntlm->initReply.challange, challange);
    if (ret)
	krb5_clear_error_message(context);

    return ret;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ntlm_init_get_opaque(krb5_context context,
			  krb5_ntlm ntlm,
			  krb5_data *opaque)
{
    krb5_error_code ret;

    ret = der_copy_octet_string(&ntlm->initReply.opaque, opaque);
    if (ret)
	krb5_clear_error_message(context);

    return ret;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ntlm_init_get_targetname(krb5_context context,
			      krb5_ntlm ntlm,
			      char **name)
{
    *name = strdup(ntlm->initReply.targetname);
    if (*name == NULL) {
	krb5_clear_error_message(context);
	return ENOMEM;
    }
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ntlm_init_get_targetinfo(krb5_context context,
			      krb5_ntlm ntlm,
			      krb5_data *data)
{
    krb5_error_code ret;

    if (ntlm->initReply.targetinfo == NULL) {
	krb5_data_zero(data);
	return 0;
    }

    ret = krb5_data_copy(data,
			 ntlm->initReply.targetinfo->data,
			 ntlm->initReply.targetinfo->length);
    if (ret) {
	krb5_clear_error_message(context);
	return ret;
    }
    return 0;
}


KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ntlm_request(krb5_context context,
		  krb5_ntlm ntlm,
		  krb5_realm realm,
		  krb5_ccache ccache)
{
    DigestReqInner ireq;
    DigestRepInner irep;
    krb5_error_code ret;

    memset(&ireq, 0, sizeof(ireq));
    memset(&irep, 0, sizeof(irep));

    ireq.element = choice_DigestReqInner_ntlmRequest;
    ireq.u.ntlmRequest = ntlm->request;

    ret = digest_request(context, realm, ccache,
			 KRB5_KU_DIGEST_ENCRYPT, &ireq, &irep);
    if (ret)
	return ret;

    if (irep.element == choice_DigestRepInner_error) {
	ret = irep.u.error.code;
	krb5_set_error_message(context, ret,
			       N_("NTLM response error: %s", ""),
			       irep.u.error.reason);
	goto out;
    }

    if (irep.element != choice_DigestRepInner_ntlmResponse) {
	ret = EINVAL;
	krb5_set_error_message(context, ret,
			       N_("NTLM reply not an NTLMResponse", ""));
	goto out;
    }

    ret = copy_NTLMResponse(&irep.u.ntlmResponse, &ntlm->response);
    if (ret) {
	krb5_set_error_message(context, ret,
			       N_("Failed to copy NTLMResponse", ""));
	goto out;
    }

 out:
    free_DigestRepInner(&irep);

    return ret;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ntlm_req_set_flags(krb5_context context,
			krb5_ntlm ntlm,
			uint32_t flags)
{
    ntlm->request.flags = flags;
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ntlm_req_set_username(krb5_context context,
			   krb5_ntlm ntlm,
			   const char *username)
{
    ntlm->request.username = strdup(username);
    if (ntlm->request.username == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ntlm_req_set_targetname(krb5_context context,
			     krb5_ntlm ntlm,
			     const char *targetname)
{
    ntlm->request.targetname = strdup(targetname);
    if (ntlm->request.targetname == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ntlm_req_set_lm(krb5_context context,
		     krb5_ntlm ntlm,
		     void *hash, size_t len)
{
    ntlm->request.lm.data = malloc(len);
    if (ntlm->request.lm.data == NULL && len != 0) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    ntlm->request.lm.length = len;
    memcpy(ntlm->request.lm.data, hash, len);
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ntlm_req_set_ntlm(krb5_context context,
		       krb5_ntlm ntlm,
		       void *hash, size_t len)
{
    ntlm->request.ntlm.data = malloc(len);
    if (ntlm->request.ntlm.data == NULL && len != 0) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    ntlm->request.ntlm.length = len;
    memcpy(ntlm->request.ntlm.data, hash, len);
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ntlm_req_set_opaque(krb5_context context,
			 krb5_ntlm ntlm,
			 krb5_data *opaque)
{
    ntlm->request.opaque.data = malloc(opaque->length);
    if (ntlm->request.opaque.data == NULL && opaque->length != 0) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    ntlm->request.opaque.length = opaque->length;
    memcpy(ntlm->request.opaque.data, opaque->data, opaque->length);
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ntlm_req_set_session(krb5_context context,
			  krb5_ntlm ntlm,
			  void *sessionkey, size_t length)
{
    ntlm->request.sessionkey = calloc(1, sizeof(*ntlm->request.sessionkey));
    if (ntlm->request.sessionkey == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    ntlm->request.sessionkey->data = malloc(length);
    if (ntlm->request.sessionkey->data == NULL && length != 0) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    memcpy(ntlm->request.sessionkey->data, sessionkey, length);
    ntlm->request.sessionkey->length = length;
    return 0;
}

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_ntlm_rep_get_status(krb5_context context,
			 krb5_ntlm ntlm)
{
    return ntlm->response.success ? TRUE : FALSE;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ntlm_rep_get_sessionkey(krb5_context context,
			     krb5_ntlm ntlm,
			     krb5_data *data)
{
    if (ntlm->response.sessionkey == NULL) {
	krb5_set_error_message(context, EINVAL,
			       N_("no ntlm session key", ""));
	return EINVAL;
    }
    krb5_clear_error_message(context);
    return krb5_data_copy(data,
			  ntlm->response.sessionkey->data,
			  ntlm->response.sessionkey->length);
}

/**
 * Get the supported/allowed mechanism for this principal.
 *
 * @param context A Keberos context.
 * @param realm The realm of the KDC.
 * @param ccache The credential cache to use when talking to the KDC.
 * @param flags The supported mechanism.
 *
 * @return Return an error code or 0.
 *
 * @ingroup krb5_digest
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_probe(krb5_context context,
		  krb5_realm realm,
		  krb5_ccache ccache,
		  unsigned *flags)
{
    DigestReqInner ireq;
    DigestRepInner irep;
    krb5_error_code ret;

    memset(&ireq, 0, sizeof(ireq));
    memset(&irep, 0, sizeof(irep));

    ireq.element = choice_DigestReqInner_supportedMechs;

    ret = digest_request(context, realm, ccache,
			 KRB5_KU_DIGEST_ENCRYPT, &ireq, &irep);
    if (ret)
	goto out;

    if (irep.element == choice_DigestRepInner_error) {
	ret = irep.u.error.code;
	krb5_set_error_message(context, ret, "Digest probe error: %s",
			       irep.u.error.reason);
	goto out;
    }

    if (irep.element != choice_DigestRepInner_supportedMechs) {
	ret = EINVAL;
	krb5_set_error_message(context, ret, "Digest reply not an probe");
	goto out;
    }

    *flags = DigestTypes2int(irep.u.supportedMechs);

 out:
    free_DigestRepInner(&irep);

    return ret;
}

#endif /* HEIMDAL_SMALLER */
