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

/**
 * @page page_revoke Revocation methods
 *
 * There are two revocation method for PKIX/X.509: CRL and OCSP.
 * Revocation is needed if the private key is lost and
 * stolen. Depending on how picky you are, you might want to make
 * revocation for destroyed private keys too (smartcard broken), but
 * that should not be a problem.
 *
 * CRL is a list of certifiates that have expired.
 *
 * OCSP is an online checking method where the requestor sends a list
 * of certificates to the OCSP server to return a signed reply if they
 * are valid or not. Some services sends a OCSP reply as part of the
 * hand-shake to make the revoktion decision simpler/faster for the
 * client.
 */

#include "hx_locl.h"

struct revoke_crl {
    char *path;
    time_t last_modfied;
    CRLCertificateList crl;
    int verified;
    int failed_verify;
};

struct revoke_ocsp {
    char *path;
    time_t last_modfied;
    OCSPBasicOCSPResponse ocsp;
    hx509_certs certs;
    hx509_cert signer;
};


struct hx509_revoke_ctx_data {
    unsigned int ref;
    struct {
	struct revoke_crl *val;
	size_t len;
    } crls;
    struct {
	struct revoke_ocsp *val;
	size_t len;
    } ocsps;
};

/**
 * Allocate a revokation context. Free with hx509_revoke_free().
 *
 * @param context A hx509 context.
 * @param ctx returns a newly allocated revokation context.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_revoke
 */

int
hx509_revoke_init(hx509_context context, hx509_revoke_ctx *ctx)
{
    *ctx = calloc(1, sizeof(**ctx));
    if (*ctx == NULL)
	return ENOMEM;

    (*ctx)->ref = 1;
    (*ctx)->crls.len = 0;
    (*ctx)->crls.val = NULL;
    (*ctx)->ocsps.len = 0;
    (*ctx)->ocsps.val = NULL;

    return 0;
}

hx509_revoke_ctx
_hx509_revoke_ref(hx509_revoke_ctx ctx)
{
    if (ctx == NULL)
	return NULL;
    if (ctx->ref == 0)
	_hx509_abort("revoke ctx refcount == 0 on ref");
    ctx->ref++;
    if (ctx->ref == UINT_MAX)
	_hx509_abort("revoke ctx refcount == UINT_MAX on ref");
    return ctx;
}

static void
free_ocsp(struct revoke_ocsp *ocsp)
{
    free(ocsp->path);
    free_OCSPBasicOCSPResponse(&ocsp->ocsp);
    hx509_certs_free(&ocsp->certs);
    hx509_cert_free(ocsp->signer);
}

/**
 * Free a hx509 revokation context.
 *
 * @param ctx context to be freed
 *
 * @ingroup hx509_revoke
 */

void
hx509_revoke_free(hx509_revoke_ctx *ctx)
{
    size_t i ;

    if (ctx == NULL || *ctx == NULL)
	return;

    if ((*ctx)->ref == 0)
	_hx509_abort("revoke ctx refcount == 0 on free");
    if (--(*ctx)->ref > 0)
	return;

    for (i = 0; i < (*ctx)->crls.len; i++) {
	free((*ctx)->crls.val[i].path);
	free_CRLCertificateList(&(*ctx)->crls.val[i].crl);
    }

    for (i = 0; i < (*ctx)->ocsps.len; i++)
	free_ocsp(&(*ctx)->ocsps.val[i]);
    free((*ctx)->ocsps.val);

    free((*ctx)->crls.val);

    memset(*ctx, 0, sizeof(**ctx));
    free(*ctx);
    *ctx = NULL;
}

static int
verify_ocsp(hx509_context context,
	    struct revoke_ocsp *ocsp,
	    time_t time_now,
	    hx509_certs certs,
	    hx509_cert parent)
{
    hx509_cert signer = NULL;
    hx509_query q;
    int ret;

    _hx509_query_clear(&q);

    /*
     * Need to match on issuer too in case there are two CA that have
     * issued the same name to a certificate. One example of this is
     * the www.openvalidation.org test's ocsp validator.
     */

    q.match = HX509_QUERY_MATCH_ISSUER_NAME;
    q.issuer_name = &_hx509_get_cert(parent)->tbsCertificate.issuer;

    switch(ocsp->ocsp.tbsResponseData.responderID.element) {
    case choice_OCSPResponderID_byName:
	q.match |= HX509_QUERY_MATCH_SUBJECT_NAME;
	q.subject_name = &ocsp->ocsp.tbsResponseData.responderID.u.byName;
	break;
    case choice_OCSPResponderID_byKey:
	q.match |= HX509_QUERY_MATCH_KEY_HASH_SHA1;
	q.keyhash_sha1 = &ocsp->ocsp.tbsResponseData.responderID.u.byKey;
	break;
    }

    ret = hx509_certs_find(context, certs, &q, &signer);
    if (ret && ocsp->certs)
	ret = hx509_certs_find(context, ocsp->certs, &q, &signer);
    if (ret)
	goto out;

    /*
     * If signer certificate isn't the CA certificate, lets check the
     * it is the CA that signed the signer certificate and the OCSP EKU
     * is set.
     */
    if (hx509_cert_cmp(signer, parent) != 0) {
	Certificate *p = _hx509_get_cert(parent);
	Certificate *s = _hx509_get_cert(signer);

	ret = _hx509_cert_is_parent_cmp(s, p, 0);
	if (ret != 0) {
	    ret = HX509_PARENT_NOT_CA;
	    hx509_set_error_string(context, 0, ret, "Revoke OCSP signer is "
				   "doesn't have CA as signer certificate");
	    goto out;
	}

	ret = _hx509_verify_signature_bitstring(context,
						parent,
						&s->signatureAlgorithm,
						&s->tbsCertificate._save,
						&s->signatureValue);
	if (ret) {
	    hx509_set_error_string(context, HX509_ERROR_APPEND, ret,
				   "OCSP signer signature invalid");
	    goto out;
	}

	ret = hx509_cert_check_eku(context, signer,
				   &asn1_oid_id_pkix_kp_OCSPSigning, 0);
	if (ret)
	    goto out;
    }

    ret = _hx509_verify_signature_bitstring(context,
					    signer,
					    &ocsp->ocsp.signatureAlgorithm,
					    &ocsp->ocsp.tbsResponseData._save,
					    &ocsp->ocsp.signature);
    if (ret) {
	hx509_set_error_string(context, HX509_ERROR_APPEND, ret,
			       "OCSP signature invalid");
	goto out;
    }

    ocsp->signer = signer;
    signer = NULL;
out:
    if (signer)
	hx509_cert_free(signer);

    return ret;
}

/*
 *
 */

static int
parse_ocsp_basic(const void *data, size_t length, OCSPBasicOCSPResponse *basic)
{
    OCSPResponse resp;
    size_t size;
    int ret;

    memset(basic, 0, sizeof(*basic));

    ret = decode_OCSPResponse(data, length, &resp, &size);
    if (ret)
	return ret;
    if (length != size) {
	free_OCSPResponse(&resp);
	return ASN1_EXTRA_DATA;
    }

    switch (resp.responseStatus) {
    case successful:
	break;
    default:
	free_OCSPResponse(&resp);
	return HX509_REVOKE_WRONG_DATA;
    }

    if (resp.responseBytes == NULL) {
	free_OCSPResponse(&resp);
	return EINVAL;
    }

    ret = der_heim_oid_cmp(&resp.responseBytes->responseType,
			   &asn1_oid_id_pkix_ocsp_basic);
    if (ret != 0) {
	free_OCSPResponse(&resp);
	return HX509_REVOKE_WRONG_DATA;
    }

    ret = decode_OCSPBasicOCSPResponse(resp.responseBytes->response.data,
				       resp.responseBytes->response.length,
				       basic,
				       &size);
    if (ret) {
	free_OCSPResponse(&resp);
	return ret;
    }
    if (size != resp.responseBytes->response.length) {
	free_OCSPResponse(&resp);
	free_OCSPBasicOCSPResponse(basic);
	return ASN1_EXTRA_DATA;
    }
    free_OCSPResponse(&resp);

    return 0;
}

/*
 *
 */

static int
load_ocsp(hx509_context context, struct revoke_ocsp *ocsp)
{
    OCSPBasicOCSPResponse basic;
    hx509_certs certs = NULL;
    size_t length;
    struct stat sb;
    void *data;
    int ret;

    ret = rk_undumpdata(ocsp->path, &data, &length);
    if (ret)
	return ret;

    ret = stat(ocsp->path, &sb);
    if (ret)
	return errno;

    ret = parse_ocsp_basic(data, length, &basic);
    rk_xfree(data);
    if (ret) {
	hx509_set_error_string(context, 0, ret,
			       "Failed to parse OCSP response");
	return ret;
    }

    if (basic.certs) {
	size_t i;

	ret = hx509_certs_init(context, "MEMORY:ocsp-certs", 0,
			       NULL, &certs);
	if (ret) {
	    free_OCSPBasicOCSPResponse(&basic);
	    return ret;
	}

	for (i = 0; i < basic.certs->len; i++) {
	    hx509_cert c;

	    ret = hx509_cert_init(context, &basic.certs->val[i], &c);
	    if (ret)
		continue;

	    ret = hx509_certs_add(context, certs, c);
	    hx509_cert_free(c);
	    if (ret)
		continue;
	}
    }

    ocsp->last_modfied = sb.st_mtime;

    free_OCSPBasicOCSPResponse(&ocsp->ocsp);
    hx509_certs_free(&ocsp->certs);
    hx509_cert_free(ocsp->signer);

    ocsp->ocsp = basic;
    ocsp->certs = certs;
    ocsp->signer = NULL;

    return 0;
}

/**
 * Add a OCSP file to the revokation context.
 *
 * @param context hx509 context
 * @param ctx hx509 revokation context
 * @param path path to file that is going to be added to the context.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_revoke
 */

int
hx509_revoke_add_ocsp(hx509_context context,
		      hx509_revoke_ctx ctx,
		      const char *path)
{
    void *data;
    int ret;
    size_t i;

    if (strncmp(path, "FILE:", 5) != 0) {
	hx509_set_error_string(context, 0, HX509_UNSUPPORTED_OPERATION,
			       "unsupport type in %s", path);
	return HX509_UNSUPPORTED_OPERATION;
    }

    path += 5;

    for (i = 0; i < ctx->ocsps.len; i++) {
	if (strcmp(ctx->ocsps.val[0].path, path) == 0)
	    return 0;
    }

    data = realloc(ctx->ocsps.val,
		   (ctx->ocsps.len + 1) * sizeof(ctx->ocsps.val[0]));
    if (data == NULL) {
	hx509_clear_error_string(context);
	return ENOMEM;
    }

    ctx->ocsps.val = data;

    memset(&ctx->ocsps.val[ctx->ocsps.len], 0,
	   sizeof(ctx->ocsps.val[0]));

    ctx->ocsps.val[ctx->ocsps.len].path = strdup(path);
    if (ctx->ocsps.val[ctx->ocsps.len].path == NULL) {
	hx509_clear_error_string(context);
	return ENOMEM;
    }

    ret = load_ocsp(context, &ctx->ocsps.val[ctx->ocsps.len]);
    if (ret) {
	free(ctx->ocsps.val[ctx->ocsps.len].path);
	return ret;
    }
    ctx->ocsps.len++;

    return ret;
}

/*
 *
 */

static int
verify_crl(hx509_context context,
	   hx509_revoke_ctx ctx,
	   CRLCertificateList *crl,
	   time_t time_now,
	   hx509_certs certs,
	   hx509_cert parent)
{
    hx509_cert signer;
    hx509_query q;
    time_t t;
    int ret;

    t = _hx509_Time2time_t(&crl->tbsCertList.thisUpdate);
    if (t > time_now) {
	hx509_set_error_string(context, 0, HX509_CRL_USED_BEFORE_TIME,
			       "CRL used before time");
	return HX509_CRL_USED_BEFORE_TIME;
    }

    if (crl->tbsCertList.nextUpdate == NULL) {
	hx509_set_error_string(context, 0, HX509_CRL_INVALID_FORMAT,
			       "CRL missing nextUpdate");
	return HX509_CRL_INVALID_FORMAT;
    }

    t = _hx509_Time2time_t(crl->tbsCertList.nextUpdate);
    if (t < time_now) {
	hx509_set_error_string(context, 0, HX509_CRL_USED_AFTER_TIME,
			       "CRL used after time");
	return HX509_CRL_USED_AFTER_TIME;
    }

    _hx509_query_clear(&q);

    /*
     * If it's the signer have CRLSIGN bit set, use that as the signer
     * cert for the certificate, otherwise, search for a certificate.
     */
    if (_hx509_check_key_usage(context, parent, 1 << 6, FALSE) == 0) {
	signer = hx509_cert_ref(parent);
    } else {
	q.match = HX509_QUERY_MATCH_SUBJECT_NAME;
	q.match |= HX509_QUERY_KU_CRLSIGN;
	q.subject_name = &crl->tbsCertList.issuer;

	ret = hx509_certs_find(context, certs, &q, &signer);
	if (ret) {
	    hx509_set_error_string(context, HX509_ERROR_APPEND, ret,
				   "Failed to find certificate for CRL");
	    return ret;
	}
    }

    ret = _hx509_verify_signature_bitstring(context,
					    signer,
					    &crl->signatureAlgorithm,
					    &crl->tbsCertList._save,
					    &crl->signatureValue);
    if (ret) {
	hx509_set_error_string(context, HX509_ERROR_APPEND, ret,
			       "CRL signature invalid");
	goto out;
    }

    /*
     * If signer is not CA cert, need to check revoke status of this
     * CRL signing cert too, this include all parent CRL signer cert
     * up to the root *sigh*, assume root at least hve CERTSIGN flag
     * set.
     */
    while (_hx509_check_key_usage(context, signer, 1 << 5, TRUE)) {
	hx509_cert crl_parent;

	_hx509_query_clear(&q);

	q.match = HX509_QUERY_MATCH_SUBJECT_NAME;
	q.match |= HX509_QUERY_KU_CRLSIGN;
	q.subject_name = &_hx509_get_cert(signer)->tbsCertificate.issuer;

	ret = hx509_certs_find(context, certs, &q, &crl_parent);
	if (ret) {
	    hx509_set_error_string(context, HX509_ERROR_APPEND, ret,
				   "Failed to find parent of CRL signer");
	    goto out;
	}

	ret = hx509_revoke_verify(context,
				  ctx,
				  certs,
				  time_now,
				  signer,
				  crl_parent);
	hx509_cert_free(signer);
	signer = crl_parent;
	if (ret) {
	    hx509_set_error_string(context, HX509_ERROR_APPEND, ret,
				   "Failed to verify revoke "
				   "status of CRL signer");
	    goto out;
	}
    }

out:
    hx509_cert_free(signer);

    return ret;
}

static int
load_crl(const char *path, time_t *t, CRLCertificateList *crl)
{
    size_t length, size;
    struct stat sb;
    void *data;
    int ret;

    memset(crl, 0, sizeof(*crl));

    ret = rk_undumpdata(path, &data, &length);
    if (ret)
	return ret;

    ret = stat(path, &sb);
    if (ret)
	return errno;

    *t = sb.st_mtime;

    ret = decode_CRLCertificateList(data, length, crl, &size);
    rk_xfree(data);
    if (ret)
	return ret;

    /* check signature is aligned */
    if (crl->signatureValue.length & 7) {
	free_CRLCertificateList(crl);
	return HX509_CRYPTO_SIG_INVALID_FORMAT;
    }
    return 0;
}

/**
 * Add a CRL file to the revokation context.
 *
 * @param context hx509 context
 * @param ctx hx509 revokation context
 * @param path path to file that is going to be added to the context.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_revoke
 */

int
hx509_revoke_add_crl(hx509_context context,
		     hx509_revoke_ctx ctx,
		     const char *path)
{
    void *data;
    size_t i;
    int ret;

    if (strncmp(path, "FILE:", 5) != 0) {
	hx509_set_error_string(context, 0, HX509_UNSUPPORTED_OPERATION,
			       "unsupport type in %s", path);
	return HX509_UNSUPPORTED_OPERATION;
    }


    path += 5;

    for (i = 0; i < ctx->crls.len; i++) {
	if (strcmp(ctx->crls.val[0].path, path) == 0)
	    return 0;
    }

    data = realloc(ctx->crls.val,
		   (ctx->crls.len + 1) * sizeof(ctx->crls.val[0]));
    if (data == NULL) {
	hx509_clear_error_string(context);
	return ENOMEM;
    }
    ctx->crls.val = data;

    memset(&ctx->crls.val[ctx->crls.len], 0, sizeof(ctx->crls.val[0]));

    ctx->crls.val[ctx->crls.len].path = strdup(path);
    if (ctx->crls.val[ctx->crls.len].path == NULL) {
	hx509_clear_error_string(context);
	return ENOMEM;
    }

    ret = load_crl(path,
		   &ctx->crls.val[ctx->crls.len].last_modfied,
		   &ctx->crls.val[ctx->crls.len].crl);
    if (ret) {
	free(ctx->crls.val[ctx->crls.len].path);
	return ret;
    }

    ctx->crls.len++;

    return ret;
}

/**
 * Check that a certificate is not expired according to a revokation
 * context. Also need the parent certificte to the check OCSP
 * parent identifier.
 *
 * @param context hx509 context
 * @param ctx hx509 revokation context
 * @param certs
 * @param now
 * @param cert
 * @param parent_cert
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_revoke
 */


int
hx509_revoke_verify(hx509_context context,
		    hx509_revoke_ctx ctx,
		    hx509_certs certs,
		    time_t now,
		    hx509_cert cert,
		    hx509_cert parent_cert)
{
    const Certificate *c = _hx509_get_cert(cert);
    const Certificate *p = _hx509_get_cert(parent_cert);
    unsigned long i, j, k;
    int ret;

    hx509_clear_error_string(context);

    for (i = 0; i < ctx->ocsps.len; i++) {
	struct revoke_ocsp *ocsp = &ctx->ocsps.val[i];
	struct stat sb;

	/* check this ocsp apply to this cert */

	/* check if there is a newer version of the file */
	ret = stat(ocsp->path, &sb);
	if (ret == 0 && ocsp->last_modfied != sb.st_mtime) {
	    ret = load_ocsp(context, ocsp);
	    if (ret)
		continue;
	}

	/* verify signature in ocsp if not already done */
	if (ocsp->signer == NULL) {
	    ret = verify_ocsp(context, ocsp, now, certs, parent_cert);
	    if (ret)
		continue;
	}

	for (j = 0; j < ocsp->ocsp.tbsResponseData.responses.len; j++) {
	    heim_octet_string os;

	    ret = der_heim_integer_cmp(&ocsp->ocsp.tbsResponseData.responses.val[j].certID.serialNumber,
				   &c->tbsCertificate.serialNumber);
	    if (ret != 0)
		continue;

	    /* verify issuer hashes hash */
	    ret = _hx509_verify_signature(context,
					  NULL,
					  &ocsp->ocsp.tbsResponseData.responses.val[i].certID.hashAlgorithm,
					  &c->tbsCertificate.issuer._save,
					  &ocsp->ocsp.tbsResponseData.responses.val[i].certID.issuerNameHash);
	    if (ret != 0)
		continue;

	    os.data = p->tbsCertificate.subjectPublicKeyInfo.subjectPublicKey.data;
	    os.length = p->tbsCertificate.subjectPublicKeyInfo.subjectPublicKey.length / 8;

	    ret = _hx509_verify_signature(context,
					  NULL,
					  &ocsp->ocsp.tbsResponseData.responses.val[j].certID.hashAlgorithm,
					  &os,
					  &ocsp->ocsp.tbsResponseData.responses.val[j].certID.issuerKeyHash);
	    if (ret != 0)
		continue;

	    switch (ocsp->ocsp.tbsResponseData.responses.val[j].certStatus.element) {
	    case choice_OCSPCertStatus_good:
		break;
	    case choice_OCSPCertStatus_revoked:
		hx509_set_error_string(context, 0,
				       HX509_CERT_REVOKED,
				       "Certificate revoked by issuer in OCSP");
		return HX509_CERT_REVOKED;
	    case choice_OCSPCertStatus_unknown:
		continue;
	    }

	    /* don't allow the update to be in the future */
	    if (ocsp->ocsp.tbsResponseData.responses.val[j].thisUpdate >
		now + context->ocsp_time_diff)
		continue;

	    /* don't allow the next update to be in the past */
	    if (ocsp->ocsp.tbsResponseData.responses.val[j].nextUpdate) {
		if (*ocsp->ocsp.tbsResponseData.responses.val[j].nextUpdate < now)
		    continue;
	    } /* else should force a refetch, but can we ? */

	    return 0;
	}
    }

    for (i = 0; i < ctx->crls.len; i++) {
	struct revoke_crl *crl = &ctx->crls.val[i];
	struct stat sb;
	int diff;

	/* check if cert.issuer == crls.val[i].crl.issuer */
	ret = _hx509_name_cmp(&c->tbsCertificate.issuer,
			      &crl->crl.tbsCertList.issuer, &diff);
	if (ret || diff)
	    continue;

	ret = stat(crl->path, &sb);
	if (ret == 0 && crl->last_modfied != sb.st_mtime) {
	    CRLCertificateList cl;

	    ret = load_crl(crl->path, &crl->last_modfied, &cl);
	    if (ret == 0) {
		free_CRLCertificateList(&crl->crl);
		crl->crl = cl;
		crl->verified = 0;
		crl->failed_verify = 0;
	    }
	}
	if (crl->failed_verify)
	    continue;

	/* verify signature in crl if not already done */
	if (crl->verified == 0) {
	    ret = verify_crl(context, ctx, &crl->crl, now, certs, parent_cert);
	    if (ret) {
		crl->failed_verify = 1;
		continue;
	    }
	    crl->verified = 1;
	}

	if (crl->crl.tbsCertList.crlExtensions) {
	    for (j = 0; j < crl->crl.tbsCertList.crlExtensions->len; j++) {
		if (crl->crl.tbsCertList.crlExtensions->val[j].critical) {
		    hx509_set_error_string(context, 0,
					   HX509_CRL_UNKNOWN_EXTENSION,
					   "Unknown CRL extension");
		    return HX509_CRL_UNKNOWN_EXTENSION;
		}
	    }
	}

	if (crl->crl.tbsCertList.revokedCertificates == NULL)
	    return 0;

	/* check if cert is in crl */
	for (j = 0; j < crl->crl.tbsCertList.revokedCertificates->len; j++) {
	    time_t t;

	    ret = der_heim_integer_cmp(&crl->crl.tbsCertList.revokedCertificates->val[j].userCertificate,
				       &c->tbsCertificate.serialNumber);
	    if (ret != 0)
		continue;

	    t = _hx509_Time2time_t(&crl->crl.tbsCertList.revokedCertificates->val[j].revocationDate);
	    if (t > now)
		continue;

	    if (crl->crl.tbsCertList.revokedCertificates->val[j].crlEntryExtensions)
		for (k = 0; k < crl->crl.tbsCertList.revokedCertificates->val[j].crlEntryExtensions->len; k++)
		    if (crl->crl.tbsCertList.revokedCertificates->val[j].crlEntryExtensions->val[k].critical)
			return HX509_CRL_UNKNOWN_EXTENSION;

	    hx509_set_error_string(context, 0,
				   HX509_CERT_REVOKED,
				   "Certificate revoked by issuer in CRL");
	    return HX509_CERT_REVOKED;
	}

	return 0;
    }


    if (context->flags & HX509_CTX_VERIFY_MISSING_OK)
	return 0;
    hx509_set_error_string(context, HX509_ERROR_APPEND,
			   HX509_REVOKE_STATUS_MISSING,
			   "No revoke status found for "
			   "certificates");
    return HX509_REVOKE_STATUS_MISSING;
}

struct ocsp_add_ctx {
    OCSPTBSRequest *req;
    hx509_certs certs;
    const AlgorithmIdentifier *digest;
    hx509_cert parent;
};

static int
add_to_req(hx509_context context, void *ptr, hx509_cert cert)
{
    struct ocsp_add_ctx *ctx = ptr;
    OCSPInnerRequest *one;
    hx509_cert parent = NULL;
    Certificate *p, *c = _hx509_get_cert(cert);
    heim_octet_string os;
    int ret;
    hx509_query q;
    void *d;

    d = realloc(ctx->req->requestList.val,
		sizeof(ctx->req->requestList.val[0]) *
		(ctx->req->requestList.len + 1));
    if (d == NULL)
	return ENOMEM;
    ctx->req->requestList.val = d;

    one = &ctx->req->requestList.val[ctx->req->requestList.len];
    memset(one, 0, sizeof(*one));

    _hx509_query_clear(&q);

    q.match |= HX509_QUERY_FIND_ISSUER_CERT;
    q.subject = c;

    ret = hx509_certs_find(context, ctx->certs, &q, &parent);
    if (ret)
	goto out;

    if (ctx->parent) {
	if (hx509_cert_cmp(ctx->parent, parent) != 0) {
	    ret = HX509_REVOKE_NOT_SAME_PARENT;
	    hx509_set_error_string(context, 0, ret,
				   "Not same parent certifate as "
				   "last certificate in request");
	    goto out;
	}
    } else
	ctx->parent = hx509_cert_ref(parent);

    p = _hx509_get_cert(parent);

    ret = copy_AlgorithmIdentifier(ctx->digest, &one->reqCert.hashAlgorithm);
    if (ret)
	goto out;

    ret = _hx509_create_signature(context,
				  NULL,
				  &one->reqCert.hashAlgorithm,
				  &c->tbsCertificate.issuer._save,
				  NULL,
				  &one->reqCert.issuerNameHash);
    if (ret)
	goto out;

    os.data = p->tbsCertificate.subjectPublicKeyInfo.subjectPublicKey.data;
    os.length =
	p->tbsCertificate.subjectPublicKeyInfo.subjectPublicKey.length / 8;

    ret = _hx509_create_signature(context,
				  NULL,
				  &one->reqCert.hashAlgorithm,
				  &os,
				  NULL,
				  &one->reqCert.issuerKeyHash);
    if (ret)
	goto out;

    ret = copy_CertificateSerialNumber(&c->tbsCertificate.serialNumber,
				       &one->reqCert.serialNumber);
    if (ret)
	goto out;

    ctx->req->requestList.len++;
out:
    hx509_cert_free(parent);
    if (ret) {
	free_OCSPInnerRequest(one);
	memset(one, 0, sizeof(*one));
    }

    return ret;
}

/**
 * Create an OCSP request for a set of certificates.
 *
 * @param context a hx509 context
 * @param reqcerts list of certificates to request ocsp data for
 * @param pool certificate pool to use when signing
 * @param signer certificate to use to sign the request
 * @param digest the signing algorithm in the request, if NULL use the
 * default signature algorithm,
 * @param request the encoded request, free with free_heim_octet_string().
 * @param nonce nonce in the request, free with free_heim_octet_string().
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_revoke
 */

int
hx509_ocsp_request(hx509_context context,
		   hx509_certs reqcerts,
		   hx509_certs pool,
		   hx509_cert signer,
		   const AlgorithmIdentifier *digest,
		   heim_octet_string *request,
		   heim_octet_string *nonce)
{
    OCSPRequest req;
    size_t size;
    int ret;
    struct ocsp_add_ctx ctx;
    Extensions *es;

    memset(&req, 0, sizeof(req));

    if (digest == NULL)
	digest = _hx509_crypto_default_digest_alg;

    ctx.req = &req.tbsRequest;
    ctx.certs = pool;
    ctx.digest = digest;
    ctx.parent = NULL;

    ret = hx509_certs_iter_f(context, reqcerts, add_to_req, &ctx);
    hx509_cert_free(ctx.parent);
    if (ret)
	goto out;

    if (nonce) {
	req.tbsRequest.requestExtensions =
	    calloc(1, sizeof(*req.tbsRequest.requestExtensions));
	if (req.tbsRequest.requestExtensions == NULL) {
	    ret = ENOMEM;
	    goto out;
	}

	es = req.tbsRequest.requestExtensions;

	es->val = calloc(es->len, sizeof(es->val[0]));
	if (es->val == NULL) {
	    ret = ENOMEM;
	    goto out;
	}
	es->len = 1;
	ret = der_copy_oid(&asn1_oid_id_pkix_ocsp_nonce, &es->val[0].extnID);
	if (ret) {
	    free_OCSPRequest(&req);
	    return ret;
	}

	es->val[0].extnValue.data = malloc(10);
	if (es->val[0].extnValue.data == NULL) {
	    ret = ENOMEM;
	    goto out;
	}
	es->val[0].extnValue.length = 10;

	ret = RAND_bytes(es->val[0].extnValue.data,
			 es->val[0].extnValue.length);
	if (ret != 1) {
	    ret = HX509_CRYPTO_INTERNAL_ERROR;
	    goto out;
	}
	ret = der_copy_octet_string(nonce, &es->val[0].extnValue);
	if (ret) {
	    ret = ENOMEM;
	    goto out;
	}
    }

    ASN1_MALLOC_ENCODE(OCSPRequest, request->data, request->length,
		       &req, &size, ret);
    free_OCSPRequest(&req);
    if (ret)
	goto out;
    if (size != request->length)
	_hx509_abort("internal ASN.1 encoder error");

    return 0;

out:
    free_OCSPRequest(&req);
    return ret;
}

static char *
printable_time(time_t t)
{
    static char s[128];
    char *p;
    if ((p = ctime(&t)) == NULL)
       strlcpy(s, "?", sizeof(s));
    else {
       strlcpy(s, p + 4, sizeof(s));
       s[20] = 0;
    }
    return s;
}

/**
 * Print the OCSP reply stored in a file.
 *
 * @param context a hx509 context
 * @param path path to a file with a OCSP reply
 * @param out the out FILE descriptor to print the reply on
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_revoke
 */

int
hx509_revoke_ocsp_print(hx509_context context, const char *path, FILE *out)
{
    struct revoke_ocsp ocsp;
    int ret;
    size_t i;

    if (out == NULL)
	out = stdout;

    memset(&ocsp, 0, sizeof(ocsp));

    ocsp.path = strdup(path);
    if (ocsp.path == NULL)
	return ENOMEM;

    ret = load_ocsp(context, &ocsp);
    if (ret) {
	free_ocsp(&ocsp);
	return ret;
    }

    fprintf(out, "signer: ");

    switch(ocsp.ocsp.tbsResponseData.responderID.element) {
    case choice_OCSPResponderID_byName: {
	hx509_name n;
	char *s;
	_hx509_name_from_Name(&ocsp.ocsp.tbsResponseData.responderID.u.byName, &n);
	hx509_name_to_string(n, &s);
	hx509_name_free(&n);
	fprintf(out, " byName: %s\n", s);
	free(s);
	break;
    }
    case choice_OCSPResponderID_byKey: {
	char *s;
	hex_encode(ocsp.ocsp.tbsResponseData.responderID.u.byKey.data,
		   ocsp.ocsp.tbsResponseData.responderID.u.byKey.length,
		   &s);
	fprintf(out, " byKey: %s\n", s);
	free(s);
	break;
    }
    default:
	_hx509_abort("choice_OCSPResponderID unknown");
	break;
    }

    fprintf(out, "producedAt: %s\n",
	    printable_time(ocsp.ocsp.tbsResponseData.producedAt));

    fprintf(out, "replies: %d\n", ocsp.ocsp.tbsResponseData.responses.len);

    for (i = 0; i < ocsp.ocsp.tbsResponseData.responses.len; i++) {
	const char *status;
	switch (ocsp.ocsp.tbsResponseData.responses.val[i].certStatus.element) {
	case choice_OCSPCertStatus_good:
	    status = "good";
	    break;
	case choice_OCSPCertStatus_revoked:
	    status = "revoked";
	    break;
	case choice_OCSPCertStatus_unknown:
	    status = "unknown";
	    break;
	default:
	    status = "element unknown";
	}

	fprintf(out, "\t%zu. status: %s\n", i, status);

	fprintf(out, "\tthisUpdate: %s\n",
		printable_time(ocsp.ocsp.tbsResponseData.responses.val[i].thisUpdate));
	if (ocsp.ocsp.tbsResponseData.responses.val[i].nextUpdate)
	    fprintf(out, "\tproducedAt: %s\n",
		    printable_time(ocsp.ocsp.tbsResponseData.responses.val[i].thisUpdate));

    }

    fprintf(out, "appended certs:\n");
    if (ocsp.certs)
	ret = hx509_certs_iter_f(context, ocsp.certs, hx509_ci_print_names, out);

    free_ocsp(&ocsp);
    return ret;
}

/**
 * Verify that the certificate is part of the OCSP reply and it's not
 * expired. Doesn't verify signature the OCSP reply or it's done by a
 * authorized sender, that is assumed to be already done.
 *
 * @param context a hx509 context
 * @param now the time right now, if 0, use the current time.
 * @param cert the certificate to verify
 * @param flags flags control the behavior
 * @param data pointer to the encode ocsp reply
 * @param length the length of the encode ocsp reply
 * @param expiration return the time the OCSP will expire and need to
 * be rechecked.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_verify
 */

int
hx509_ocsp_verify(hx509_context context,
		  time_t now,
		  hx509_cert cert,
		  int flags,
		  const void *data, size_t length,
		  time_t *expiration)
{
    const Certificate *c = _hx509_get_cert(cert);
    OCSPBasicOCSPResponse basic;
    int ret;
    size_t i;

    if (now == 0)
	now = time(NULL);

    *expiration = 0;

    ret = parse_ocsp_basic(data, length, &basic);
    if (ret) {
	hx509_set_error_string(context, 0, ret,
			       "Failed to parse OCSP response");
	return ret;
    }

    for (i = 0; i < basic.tbsResponseData.responses.len; i++) {

	ret = der_heim_integer_cmp(&basic.tbsResponseData.responses.val[i].certID.serialNumber,
			       &c->tbsCertificate.serialNumber);
	if (ret != 0)
	    continue;

	/* verify issuer hashes hash */
	ret = _hx509_verify_signature(context,
				      NULL,
				      &basic.tbsResponseData.responses.val[i].certID.hashAlgorithm,
				      &c->tbsCertificate.issuer._save,
				      &basic.tbsResponseData.responses.val[i].certID.issuerNameHash);
	if (ret != 0)
	    continue;

	switch (basic.tbsResponseData.responses.val[i].certStatus.element) {
	case choice_OCSPCertStatus_good:
	    break;
	case choice_OCSPCertStatus_revoked:
	case choice_OCSPCertStatus_unknown:
	    continue;
	}

	/* don't allow the update to be in the future */
	if (basic.tbsResponseData.responses.val[i].thisUpdate >
	    now + context->ocsp_time_diff)
	    continue;

	/* don't allow the next update to be in the past */
	if (basic.tbsResponseData.responses.val[i].nextUpdate) {
	    if (*basic.tbsResponseData.responses.val[i].nextUpdate < now)
		continue;
	    *expiration = *basic.tbsResponseData.responses.val[i].nextUpdate;
	} else
	    *expiration = now;

	free_OCSPBasicOCSPResponse(&basic);
	return 0;
    }

    free_OCSPBasicOCSPResponse(&basic);

    {
	hx509_name name;
	char *subject;

	ret = hx509_cert_get_subject(cert, &name);
	if (ret) {
	    hx509_clear_error_string(context);
	    goto out;
	}
	ret = hx509_name_to_string(name, &subject);
	hx509_name_free(&name);
	if (ret) {
	    hx509_clear_error_string(context);
	    goto out;
	}
	hx509_set_error_string(context, 0, HX509_CERT_NOT_IN_OCSP,
			       "Certificate %s not in OCSP response "
			       "or not good",
			       subject);
	free(subject);
    }
out:
    return HX509_CERT_NOT_IN_OCSP;
}

struct hx509_crl {
    hx509_certs revoked;
    time_t expire;
};

/**
 * Create a CRL context. Use hx509_crl_free() to free the CRL context.
 *
 * @param context a hx509 context.
 * @param crl return pointer to a newly allocated CRL context.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_verify
 */

int
hx509_crl_alloc(hx509_context context, hx509_crl *crl)
{
    int ret;

    *crl = calloc(1, sizeof(**crl));
    if (*crl == NULL) {
	hx509_set_error_string(context, 0, ENOMEM, "out of memory");
	return ENOMEM;
    }

    ret = hx509_certs_init(context, "MEMORY:crl", 0, NULL, &(*crl)->revoked);
    if (ret) {
	free(*crl);
	*crl = NULL;
	return ret;
    }
    (*crl)->expire = 0;
    return ret;
}

/**
 * Add revoked certificate to an CRL context.
 *
 * @param context a hx509 context.
 * @param crl the CRL to add the revoked certificate to.
 * @param certs keyset of certificate to revoke.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_verify
 */

int
hx509_crl_add_revoked_certs(hx509_context context,
			    hx509_crl crl,
			    hx509_certs certs)
{
    return hx509_certs_merge(context, crl->revoked, certs);
}

/**
 * Set the lifetime of a CRL context.
 *
 * @param context a hx509 context.
 * @param crl a CRL context
 * @param delta delta time the certificate is valid, library adds the
 * current time to this.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_verify
 */

int
hx509_crl_lifetime(hx509_context context, hx509_crl crl, int delta)
{
    crl->expire = time(NULL) + delta;
    return 0;
}

/**
 * Free a CRL context.
 *
 * @param context a hx509 context.
 * @param crl a CRL context to free.
 *
 * @ingroup hx509_verify
 */

void
hx509_crl_free(hx509_context context, hx509_crl *crl)
{
    if (*crl == NULL)
	return;
    hx509_certs_free(&(*crl)->revoked);
    memset(*crl, 0, sizeof(**crl));
    free(*crl);
    *crl = NULL;
}

static int
add_revoked(hx509_context context, void *ctx, hx509_cert cert)
{
    TBSCRLCertList *c = ctx;
    unsigned int num;
    void *ptr;
    int ret;

    num = c->revokedCertificates->len;
    ptr = realloc(c->revokedCertificates->val,
		  (num + 1) * sizeof(c->revokedCertificates->val[0]));
    if (ptr == NULL) {
	hx509_clear_error_string(context);
	return ENOMEM;
    }
    c->revokedCertificates->val = ptr;

    ret = hx509_cert_get_serialnumber(cert,
				      &c->revokedCertificates->val[num].userCertificate);
    if (ret) {
	hx509_clear_error_string(context);
	return ret;
    }
    c->revokedCertificates->val[num].revocationDate.element =
	choice_Time_generalTime;
    c->revokedCertificates->val[num].revocationDate.u.generalTime =
	time(NULL) - 3600 * 24;
    c->revokedCertificates->val[num].crlEntryExtensions = NULL;

    c->revokedCertificates->len++;

    return 0;
}

/**
 * Sign a CRL and return an encode certificate.
 *
 * @param context a hx509 context.
 * @param signer certificate to sign the CRL with
 * @param crl the CRL to sign
 * @param os return the signed and encoded CRL, free with
 * free_heim_octet_string()
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_verify
 */

int
hx509_crl_sign(hx509_context context,
	       hx509_cert signer,
	       hx509_crl crl,
	       heim_octet_string *os)
{
    const AlgorithmIdentifier *sigalg = _hx509_crypto_default_sig_alg;
    CRLCertificateList c;
    size_t size;
    int ret;
    hx509_private_key signerkey;

    memset(&c, 0, sizeof(c));

    signerkey = _hx509_cert_private_key(signer);
    if (signerkey == NULL) {
	ret = HX509_PRIVATE_KEY_MISSING;
	hx509_set_error_string(context, 0, ret,
			       "Private key missing for CRL signing");
	return ret;
    }

    c.tbsCertList.version = malloc(sizeof(*c.tbsCertList.version));
    if (c.tbsCertList.version == NULL) {
	hx509_set_error_string(context, 0, ENOMEM, "out of memory");
	return ENOMEM;
    }

    *c.tbsCertList.version = 1;

    ret = copy_AlgorithmIdentifier(sigalg, &c.tbsCertList.signature);
    if (ret) {
	hx509_clear_error_string(context);
	goto out;
    }

    ret = copy_Name(&_hx509_get_cert(signer)->tbsCertificate.issuer,
		    &c.tbsCertList.issuer);
    if (ret) {
	hx509_clear_error_string(context);
	goto out;
    }

    c.tbsCertList.thisUpdate.element = choice_Time_generalTime;
    c.tbsCertList.thisUpdate.u.generalTime = time(NULL) - 24 * 3600;

    c.tbsCertList.nextUpdate = malloc(sizeof(*c.tbsCertList.nextUpdate));
    if (c.tbsCertList.nextUpdate == NULL) {
	hx509_set_error_string(context, 0, ENOMEM, "out of memory");
	ret = ENOMEM;
	goto out;
    }

    {
	time_t next = crl->expire;
	if (next == 0)
	    next = time(NULL) + 24 * 3600 * 365;

	c.tbsCertList.nextUpdate->element = choice_Time_generalTime;
	c.tbsCertList.nextUpdate->u.generalTime = next;
    }

    c.tbsCertList.revokedCertificates =
	calloc(1, sizeof(*c.tbsCertList.revokedCertificates));
    if (c.tbsCertList.revokedCertificates == NULL) {
	hx509_set_error_string(context, 0, ENOMEM, "out of memory");
	ret = ENOMEM;
	goto out;
    }
    c.tbsCertList.crlExtensions = NULL;

    ret = hx509_certs_iter_f(context, crl->revoked, add_revoked, &c.tbsCertList);
    if (ret)
	goto out;

    /* if not revoked certs, remove OPTIONAL entry */
    if (c.tbsCertList.revokedCertificates->len == 0) {
	free(c.tbsCertList.revokedCertificates);
	c.tbsCertList.revokedCertificates = NULL;
    }

    ASN1_MALLOC_ENCODE(TBSCRLCertList, os->data, os->length,
		       &c.tbsCertList, &size, ret);
    if (ret) {
	hx509_set_error_string(context, 0, ret, "failed to encode tbsCRL");
	goto out;
    }
    if (size != os->length)
	_hx509_abort("internal ASN.1 encoder error");


    ret = _hx509_create_signature_bitstring(context,
					    signerkey,
					    sigalg,
					    os,
					    &c.signatureAlgorithm,
					    &c.signatureValue);
    free(os->data);
    if (ret) {
	hx509_set_error_string(context, 0, ret, "Failed to sign CRL");
	goto out;
    }

    ASN1_MALLOC_ENCODE(CRLCertificateList, os->data, os->length,
		       &c, &size, ret);
    if (ret) {
	hx509_set_error_string(context, 0, ret, "failed to encode CRL");
	goto out;
    }
    if (size != os->length)
	_hx509_abort("internal ASN.1 encoder error");

    free_CRLCertificateList(&c);

    return 0;

out:
    free_CRLCertificateList(&c);
    return ret;
}
