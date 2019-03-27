/*
 * Copyright (c) 2006 - 2007 Kungliga Tekniska Högskolan
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

#include "kdc_locl.h"
#include <hex.h>
#include <rfc2459_asn1.h>
#include <hx509.h>

#ifdef KX509

/*
 *
 */

krb5_error_code
_kdc_try_kx509_request(void *ptr, size_t len, struct Kx509Request *req, size_t *size)
{
    if (len < 4)
	return -1;
    if (memcmp("\x00\x00\x02\x00", ptr, 4) != 0)
	return -1;
    return decode_Kx509Request(((unsigned char *)ptr) + 4, len - 4, req, size);
}

/*
 *
 */

static const unsigned char version_2_0[4] = {0 , 0, 2, 0};

static krb5_error_code
verify_req_hash(krb5_context context,
		const Kx509Request *req,
		krb5_keyblock *key)
{
    unsigned char digest[SHA_DIGEST_LENGTH];
    HMAC_CTX *ctx;

    if (req->pk_hash.length != sizeof(digest)) {
	krb5_set_error_message(context, KRB5KDC_ERR_PREAUTH_FAILED,
			       "pk-hash have wrong length: %lu",
			       (unsigned long)req->pk_hash.length);
	return KRB5KDC_ERR_PREAUTH_FAILED;
    }

    ctx = HMAC_CTX_new();
    if (ctx == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       "HMAC context malloc failed");
	return ENOMEM;
    }
    HMAC_Init_ex(ctx,
		 key->keyvalue.data, key->keyvalue.length,
		 EVP_sha1(), NULL);
    if (sizeof(digest) != HMAC_size(ctx))
	krb5_abortx(context, "runtime error, hmac buffer wrong size in kx509");
    HMAC_Update(ctx, version_2_0, sizeof(version_2_0));
    HMAC_Update(ctx, req->pk_key.data, req->pk_key.length);
    HMAC_Final(ctx, digest, 0);
    HMAC_CTX_free(ctx);

    if (memcmp(req->pk_hash.data, digest, sizeof(digest)) != 0) {
	krb5_set_error_message(context, KRB5KDC_ERR_PREAUTH_FAILED,
			       "pk-hash is not correct");
	return KRB5KDC_ERR_PREAUTH_FAILED;
    }
    return 0;
}

static krb5_error_code
calculate_reply_hash(krb5_context context,
		     krb5_keyblock *key,
		     Kx509Response *rep)
{
    krb5_error_code ret;
    HMAC_CTX *ctx;

    ctx = HMAC_CTX_new();
    if (ctx == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       "HMAC context malloc failed");
	return ENOMEM;
    }

    HMAC_Init_ex(ctx, key->keyvalue.data, key->keyvalue.length,
		 EVP_sha1(), NULL);
    ret = krb5_data_alloc(rep->hash, HMAC_size(ctx));
    if (ret) {
	HMAC_CTX_free(ctx);
	krb5_set_error_message(context, ENOMEM, "malloc: out of memory");
	return ENOMEM;
    }

    HMAC_Update(ctx, version_2_0, sizeof(version_2_0));
    if (rep->error_code) {
	int32_t t = *rep->error_code;
	do {
	    unsigned char p = (t & 0xff);
	    HMAC_Update(ctx, &p, 1);
	    t >>= 8;
	} while (t);
    }
    if (rep->certificate)
	HMAC_Update(ctx, rep->certificate->data, rep->certificate->length);
    if (rep->e_text)
	HMAC_Update(ctx, (unsigned char *)*rep->e_text, strlen(*rep->e_text));

    HMAC_Final(ctx, rep->hash->data, 0);
    HMAC_CTX_free(ctx);

    return 0;
}

/*
 * Build a certifate for `principal´ that will expire at `endtime´.
 */

static krb5_error_code
build_certificate(krb5_context context,
		  krb5_kdc_configuration *config,
		  const krb5_data *key,
		  time_t endtime,
		  krb5_principal principal,
		  krb5_data *certificate)
{
    hx509_ca_tbs tbs = NULL;
    hx509_env env = NULL;
    hx509_cert cert = NULL;
    hx509_cert signer = NULL;
    int ret;

    if (krb5_principal_get_comp_string(context, principal, 1) != NULL) {
	kdc_log(context, config, 0, "Principal is not a user");
	return EINVAL;
    }

    ret = hx509_env_add(context->hx509ctx, &env, "principal-name",
			krb5_principal_get_comp_string(context, principal, 0));
    if (ret)
	goto out;

    {
	hx509_certs certs;
	hx509_query *q;

	ret = hx509_certs_init(context->hx509ctx, config->kx509_ca, 0,
			       NULL, &certs);
	if (ret) {
	    kdc_log(context, config, 0, "Failed to load CA %s",
		    config->kx509_ca);
	    goto out;
	}
	ret = hx509_query_alloc(context->hx509ctx, &q);
	if (ret) {
	    hx509_certs_free(&certs);
	    goto out;
	}

	hx509_query_match_option(q, HX509_QUERY_OPTION_PRIVATE_KEY);
	hx509_query_match_option(q, HX509_QUERY_OPTION_KU_KEYCERTSIGN);

	ret = hx509_certs_find(context->hx509ctx, certs, q, &signer);
	hx509_query_free(context->hx509ctx, q);
	hx509_certs_free(&certs);
	if (ret) {
	    kdc_log(context, config, 0, "Failed to find a CA in %s",
		    config->kx509_ca);
	    goto out;
	}
    }

    ret = hx509_ca_tbs_init(context->hx509ctx, &tbs);
    if (ret)
	goto out;

    {
	SubjectPublicKeyInfo spki;
	heim_any any;

	memset(&spki, 0, sizeof(spki));

	spki.subjectPublicKey.data = key->data;
	spki.subjectPublicKey.length = key->length * 8;

	ret = der_copy_oid(&asn1_oid_id_pkcs1_rsaEncryption,
			   &spki.algorithm.algorithm);

	any.data = "\x05\x00";
	any.length = 2;
	spki.algorithm.parameters = &any;

	ret = hx509_ca_tbs_set_spki(context->hx509ctx, tbs, &spki);
	der_free_oid(&spki.algorithm.algorithm);
	if (ret)
	    goto out;
    }

    {
	hx509_certs certs;
	hx509_cert template;

	ret = hx509_certs_init(context->hx509ctx, config->kx509_template, 0,
			       NULL, &certs);
	if (ret) {
	    kdc_log(context, config, 0, "Failed to load template %s",
		    config->kx509_template);
	    goto out;
	}
	ret = hx509_get_one_cert(context->hx509ctx, certs, &template);
	hx509_certs_free(&certs);
	if (ret) {
	    kdc_log(context, config, 0, "Failed to find template in %s",
		    config->kx509_template);
	    goto out;
	}
	ret = hx509_ca_tbs_set_template(context->hx509ctx, tbs,
					HX509_CA_TEMPLATE_SUBJECT|
					HX509_CA_TEMPLATE_KU|
					HX509_CA_TEMPLATE_EKU,
					template);
	hx509_cert_free(template);
	if (ret)
	    goto out;
    }

    hx509_ca_tbs_set_notAfter(context->hx509ctx, tbs, endtime);

    hx509_ca_tbs_subject_expand(context->hx509ctx, tbs, env);
    hx509_env_free(&env);

    ret = hx509_ca_sign(context->hx509ctx, tbs, signer, &cert);
    hx509_cert_free(signer);
    if (ret)
	goto out;

    hx509_ca_tbs_free(&tbs);

    ret = hx509_cert_binary(context->hx509ctx, cert, certificate);
    hx509_cert_free(cert);
    if (ret)
	goto out;

    return 0;
out:
    if (env)
	hx509_env_free(&env);
    if (tbs)
	hx509_ca_tbs_free(&tbs);
    if (signer)
	hx509_cert_free(signer);
    krb5_set_error_message(context, ret, "cert creation failed");
    return ret;
}

/*
 *
 */

krb5_error_code
_kdc_do_kx509(krb5_context context,
	      krb5_kdc_configuration *config,
	      const struct Kx509Request *req, krb5_data *reply,
	      const char *from, struct sockaddr *addr)
{
    krb5_error_code ret;
    krb5_ticket *ticket = NULL;
    krb5_flags ap_req_options;
    krb5_auth_context ac = NULL;
    krb5_keytab id = NULL;
    krb5_principal sprincipal = NULL, cprincipal = NULL;
    char *cname = NULL;
    Kx509Response rep;
    size_t size;
    krb5_keyblock *key = NULL;

    krb5_data_zero(reply);
    memset(&rep, 0, sizeof(rep));

    if(!config->enable_kx509) {
	kdc_log(context, config, 0,
		"Rejected kx509 request (disabled) from %s", from);
	return KRB5KDC_ERR_POLICY;
    }

    kdc_log(context, config, 0, "Kx509 request from %s", from);

    ret = krb5_kt_resolve(context, "HDB:", &id);
    if (ret) {
	kdc_log(context, config, 0, "Can't open database for digest");
	goto out;
    }

    ret = krb5_rd_req(context,
		      &ac,
		      &req->authenticator,
		      NULL,
		      id,
		      &ap_req_options,
		      &ticket);
    if (ret)
	goto out;

    ret = krb5_ticket_get_client(context, ticket, &cprincipal);
    if (ret)
	goto out;

    ret = krb5_unparse_name(context, cprincipal, &cname);
    if (ret)
	goto out;

    /* verify server principal */

    ret = krb5_sname_to_principal(context, NULL, "kca_service",
				  KRB5_NT_UNKNOWN, &sprincipal);
    if (ret)
	goto out;

    {
	krb5_principal principal = NULL;

	ret = krb5_ticket_get_server(context, ticket, &principal);
	if (ret)
	    goto out;

	ret = krb5_principal_compare(context, sprincipal, principal);
	krb5_free_principal(context, principal);
	if (ret != TRUE) {
	    char *expected, *used;

	    ret = krb5_unparse_name(context, sprincipal, &expected);
	    if (ret)
		goto out;
	    ret = krb5_unparse_name(context, principal, &used);
	    if (ret) {
		krb5_xfree(expected);
		goto out;
	    }

	    ret = KRB5KDC_ERR_SERVER_NOMATCH;
	    krb5_set_error_message(context, ret,
				   "User %s used wrong Kx509 service "
				   "principal, expected: %s, used %s",
				   cname, expected, used);
	    krb5_xfree(expected);
	    krb5_xfree(used);
	    goto out;
	}
    }

    ret = krb5_auth_con_getkey(context, ac, &key);
    if (ret == 0 && key == NULL)
	ret = KRB5KDC_ERR_NULL_KEY;
    if (ret) {
	krb5_set_error_message(context, ret, "Kx509 can't get session key");
	goto out;
    }

    ret = verify_req_hash(context, req, key);
    if (ret)
	goto out;

    /* Verify that the key is encoded RSA key */
    {
	RSAPublicKey key;
	size_t size;

	ret = decode_RSAPublicKey(req->pk_key.data, req->pk_key.length,
				  &key, &size);
	if (ret)
	    goto out;
	free_RSAPublicKey(&key);
	if (size != req->pk_key.length) {
	    ret = ASN1_EXTRA_DATA;
	    goto out;
	}
    }

    ALLOC(rep.certificate);
    if (rep.certificate == NULL)
	goto out;
    krb5_data_zero(rep.certificate);
    ALLOC(rep.hash);
    if (rep.hash == NULL)
	goto out;
    krb5_data_zero(rep.hash);

    ret = build_certificate(context, config, &req->pk_key,
			    krb5_ticket_get_endtime(context, ticket),
			    cprincipal, rep.certificate);
    if (ret)
	goto out;

    ret = calculate_reply_hash(context, key, &rep);
    if (ret)
	goto out;

    /*
     * Encode reply, [ version | Kx509Response ]
     */

    {
	krb5_data data;

	ASN1_MALLOC_ENCODE(Kx509Response, data.data, data.length, &rep,
			   &size, ret);
	if (ret) {
	    krb5_set_error_message(context, ret, "Failed to encode kx509 reply");
	    goto out;
	}
	if (size != data.length)
	    krb5_abortx(context, "ASN1 internal error");

	ret = krb5_data_alloc(reply, data.length + sizeof(version_2_0));
	if (ret) {
	    free(data.data);
	    goto out;
	}
	memcpy(reply->data, version_2_0, sizeof(version_2_0));
	memcpy(((unsigned char *)reply->data) + sizeof(version_2_0),
	       data.data, data.length);
	free(data.data);
    }

    kdc_log(context, config, 0, "Successful Kx509 request for %s", cname);

out:
    if (ac)
	krb5_auth_con_free(context, ac);
    if (ret)
	krb5_warn(context, ret, "Kx509 request from %s failed", from);
    if (ticket)
	krb5_free_ticket(context, ticket);
    if (id)
	krb5_kt_close(context, id);
    if (sprincipal)
	krb5_free_principal(context, sprincipal);
    if (cprincipal)
	krb5_free_principal(context, cprincipal);
    if (key)
	krb5_free_keyblock (context, key);
    if (cname)
	free(cname);
    free_Kx509Response(&rep);

    return 0;
}

#endif /* KX509 */
