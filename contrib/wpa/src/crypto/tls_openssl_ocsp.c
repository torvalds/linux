/*
 * SSL/TLS interface functions for OpenSSL - BoringSSL OCSP
 * Copyright (c) 2004-2015, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#ifdef OPENSSL_IS_BORINGSSL
#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#endif /* OPENSSL_IS_BORINGSSL */

#include "common.h"
#include "tls_openssl.h"


#ifdef OPENSSL_IS_BORINGSSL

static void tls_show_errors(int level, const char *func, const char *txt)
{
	unsigned long err;

	wpa_printf(level, "OpenSSL: %s - %s %s",
		   func, txt, ERR_error_string(ERR_get_error(), NULL));

	while ((err = ERR_get_error())) {
		wpa_printf(MSG_INFO, "OpenSSL: pending error: %s",
			   ERR_error_string(err, NULL));
	}
}


/*
 * CertID ::= SEQUENCE {
 *     hashAlgorithm      AlgorithmIdentifier,
 *     issuerNameHash     OCTET STRING, -- Hash of Issuer's DN
 *     issuerKeyHash      OCTET STRING, -- Hash of Issuer's public key
 *     serialNumber       CertificateSerialNumber }
 */
typedef struct {
	X509_ALGOR *hashAlgorithm;
	ASN1_OCTET_STRING *issuerNameHash;
	ASN1_OCTET_STRING *issuerKeyHash;
	ASN1_INTEGER *serialNumber;
} CertID;

/*
 * ResponseBytes ::=       SEQUENCE {
 *     responseType   OBJECT IDENTIFIER,
 *     response       OCTET STRING }
 */
typedef struct {
	ASN1_OBJECT *responseType;
	ASN1_OCTET_STRING *response;
} ResponseBytes;

/*
 * OCSPResponse ::= SEQUENCE {
 *    responseStatus         OCSPResponseStatus,
 *    responseBytes          [0] EXPLICIT ResponseBytes OPTIONAL }
 */
typedef struct {
	ASN1_ENUMERATED *responseStatus;
	ResponseBytes *responseBytes;
} OCSPResponse;

ASN1_SEQUENCE(ResponseBytes) = {
	ASN1_SIMPLE(ResponseBytes, responseType, ASN1_OBJECT),
	ASN1_SIMPLE(ResponseBytes, response, ASN1_OCTET_STRING)
} ASN1_SEQUENCE_END(ResponseBytes);

ASN1_SEQUENCE(OCSPResponse) = {
	ASN1_SIMPLE(OCSPResponse, responseStatus, ASN1_ENUMERATED),
	ASN1_EXP_OPT(OCSPResponse, responseBytes, ResponseBytes, 0)
} ASN1_SEQUENCE_END(OCSPResponse);

IMPLEMENT_ASN1_FUNCTIONS(OCSPResponse);

/*
 * ResponderID ::= CHOICE {
 *    byName               [1] Name,
 *    byKey                [2] KeyHash }
 */
typedef struct {
	int type;
	union {
		X509_NAME *byName;
		ASN1_OCTET_STRING *byKey;
	} value;
} ResponderID;

/*
 * RevokedInfo ::= SEQUENCE {
 *     revocationTime              GeneralizedTime,
 *     revocationReason    [0]     EXPLICIT CRLReason OPTIONAL }
 */
typedef struct {
	ASN1_GENERALIZEDTIME *revocationTime;
	ASN1_ENUMERATED *revocationReason;
} RevokedInfo;

/*
 * CertStatus ::= CHOICE {
 *     good        [0]     IMPLICIT NULL,
 *     revoked     [1]     IMPLICIT RevokedInfo,
 *     unknown     [2]     IMPLICIT UnknownInfo }
 */
typedef struct {
	int type;
	union {
		ASN1_NULL *good;
		RevokedInfo *revoked;
		ASN1_NULL *unknown;
	} value;
} CertStatus;

/*
 * SingleResponse ::= SEQUENCE {
 *    certID                       CertID,
 *    certStatus                   CertStatus,
 *    thisUpdate                   GeneralizedTime,
 *    nextUpdate         [0]       EXPLICIT GeneralizedTime OPTIONAL,
 *    singleExtensions   [1]       EXPLICIT Extensions OPTIONAL }
 */
typedef struct {
	CertID *certID;
	CertStatus *certStatus;
	ASN1_GENERALIZEDTIME *thisUpdate;
	ASN1_GENERALIZEDTIME *nextUpdate;
	STACK_OF(X509_EXTENSION) *singleExtensions;
} SingleResponse;

/*
 * ResponseData ::= SEQUENCE {
 *   version              [0] EXPLICIT Version DEFAULT v1,
 *   responderID              ResponderID,
 *   producedAt               GeneralizedTime,
 *   responses                SEQUENCE OF SingleResponse,
 *   responseExtensions   [1] EXPLICIT Extensions OPTIONAL }
 */
typedef struct {
	ASN1_INTEGER *version;
	ResponderID *responderID;
	ASN1_GENERALIZEDTIME *producedAt;
	STACK_OF(SingleResponse) *responses;
	STACK_OF(X509_EXTENSION) *responseExtensions;
} ResponseData;

/*
 * BasicOCSPResponse       ::= SEQUENCE {
 *   tbsResponseData      ResponseData,
 *   signatureAlgorithm   AlgorithmIdentifier,
 *   signature            BIT STRING,
 *   certs                [0] EXPLICIT SEQUENCE OF Certificate OPTIONAL }
 */
typedef struct {
	ResponseData *tbsResponseData;
	X509_ALGOR *signatureAlgorithm;
	ASN1_BIT_STRING *signature;
	STACK_OF(X509) *certs;
} BasicOCSPResponse;

ASN1_SEQUENCE(CertID) = {
	ASN1_SIMPLE(CertID, hashAlgorithm, X509_ALGOR),
	ASN1_SIMPLE(CertID, issuerNameHash, ASN1_OCTET_STRING),
	ASN1_SIMPLE(CertID, issuerKeyHash, ASN1_OCTET_STRING),
	ASN1_SIMPLE(CertID, serialNumber, ASN1_INTEGER)
} ASN1_SEQUENCE_END(CertID);

ASN1_CHOICE(ResponderID) = {
	ASN1_EXP(ResponderID, value.byName, X509_NAME, 1),
	ASN1_EXP(ResponderID, value.byKey, ASN1_OCTET_STRING, 2)
} ASN1_CHOICE_END(ResponderID);

ASN1_SEQUENCE(RevokedInfo) = {
	ASN1_SIMPLE(RevokedInfo, revocationTime, ASN1_GENERALIZEDTIME),
	ASN1_EXP_OPT(RevokedInfo, revocationReason, ASN1_ENUMERATED, 0)
} ASN1_SEQUENCE_END(RevokedInfo);

ASN1_CHOICE(CertStatus) = {
	ASN1_IMP(CertStatus, value.good, ASN1_NULL, 0),
	ASN1_IMP(CertStatus, value.revoked, RevokedInfo, 1),
	ASN1_IMP(CertStatus, value.unknown, ASN1_NULL, 2)
} ASN1_CHOICE_END(CertStatus);

ASN1_SEQUENCE(SingleResponse) = {
	ASN1_SIMPLE(SingleResponse, certID, CertID),
	ASN1_SIMPLE(SingleResponse, certStatus, CertStatus),
	ASN1_SIMPLE(SingleResponse, thisUpdate, ASN1_GENERALIZEDTIME),
	ASN1_EXP_OPT(SingleResponse, nextUpdate, ASN1_GENERALIZEDTIME, 0),
	ASN1_EXP_SEQUENCE_OF_OPT(SingleResponse, singleExtensions,
				 X509_EXTENSION, 1)
} ASN1_SEQUENCE_END(SingleResponse);

ASN1_SEQUENCE(ResponseData) = {
	ASN1_EXP_OPT(ResponseData, version, ASN1_INTEGER, 0),
	ASN1_SIMPLE(ResponseData, responderID, ResponderID),
	ASN1_SIMPLE(ResponseData, producedAt, ASN1_GENERALIZEDTIME),
	ASN1_SEQUENCE_OF(ResponseData, responses, SingleResponse),
	ASN1_EXP_SEQUENCE_OF_OPT(ResponseData, responseExtensions,
				 X509_EXTENSION, 1)
} ASN1_SEQUENCE_END(ResponseData);

ASN1_SEQUENCE(BasicOCSPResponse) = {
	ASN1_SIMPLE(BasicOCSPResponse, tbsResponseData, ResponseData),
	ASN1_SIMPLE(BasicOCSPResponse, signatureAlgorithm, X509_ALGOR),
	ASN1_SIMPLE(BasicOCSPResponse, signature, ASN1_BIT_STRING),
	ASN1_EXP_SEQUENCE_OF_OPT(BasicOCSPResponse, certs, X509, 0)
} ASN1_SEQUENCE_END(BasicOCSPResponse);

IMPLEMENT_ASN1_FUNCTIONS(BasicOCSPResponse);

#define sk_SingleResponse_num(sk) \
sk_num(CHECKED_CAST(_STACK *, STACK_OF(SingleResponse) *, sk))

#define sk_SingleResponse_value(sk, i) \
	((SingleResponse *)						\
	 sk_value(CHECKED_CAST(_STACK *, STACK_OF(SingleResponse) *, sk), (i)))


static char * mem_bio_to_str(BIO *out)
{
	char *txt;
	size_t rlen;
	int res;

	rlen = BIO_ctrl_pending(out);
	txt = os_malloc(rlen + 1);
	if (!txt) {
		BIO_free(out);
		return NULL;
	}

	res = BIO_read(out, txt, rlen);
	BIO_free(out);
	if (res < 0) {
		os_free(txt);
		return NULL;
	}

	txt[res] = '\0';
	return txt;
}


static char * generalizedtime_str(ASN1_GENERALIZEDTIME *t)
{
	BIO *out;

	out = BIO_new(BIO_s_mem());
	if (!out)
		return NULL;

	if (!ASN1_GENERALIZEDTIME_print(out, t)) {
		BIO_free(out);
		return NULL;
	}

	return mem_bio_to_str(out);
}


static char * responderid_str(ResponderID *rid)
{
	BIO *out;

	out = BIO_new(BIO_s_mem());
	if (!out)
		return NULL;

	switch (rid->type) {
	case 0:
		X509_NAME_print_ex(out, rid->value.byName, 0, XN_FLAG_ONELINE);
		break;
	case 1:
		i2a_ASN1_STRING(out, rid->value.byKey, V_ASN1_OCTET_STRING);
		break;
	default:
		BIO_free(out);
		return NULL;
	}

	return mem_bio_to_str(out);
}


static char * octet_string_str(ASN1_OCTET_STRING *o)
{
	BIO *out;

	out = BIO_new(BIO_s_mem());
	if (!out)
		return NULL;

	i2a_ASN1_STRING(out, o, V_ASN1_OCTET_STRING);
	return mem_bio_to_str(out);
}


static char * integer_str(ASN1_INTEGER *i)
{
	BIO *out;

	out = BIO_new(BIO_s_mem());
	if (!out)
		return NULL;

	i2a_ASN1_INTEGER(out, i);
	return mem_bio_to_str(out);
}


static char * algor_str(X509_ALGOR *alg)
{
	BIO *out;

	out = BIO_new(BIO_s_mem());
	if (!out)
		return NULL;

	i2a_ASN1_OBJECT(out, alg->algorithm);
	return mem_bio_to_str(out);
}


static char * extensions_str(const char *title, STACK_OF(X509_EXTENSION) *ext)
{
	BIO *out;

	if (!ext)
		return NULL;

	out = BIO_new(BIO_s_mem());
	if (!out)
		return NULL;

	if (!X509V3_extensions_print(out, title, ext, 0, 0)) {
		BIO_free(out);
		return NULL;
	}
	return mem_bio_to_str(out);
}


static int ocsp_resp_valid(ASN1_GENERALIZEDTIME *thisupd,
			   ASN1_GENERALIZEDTIME *nextupd)
{
	time_t now, tmp;

	if (!ASN1_GENERALIZEDTIME_check(thisupd)) {
		wpa_printf(MSG_DEBUG,
			   "OpenSSL: Invalid OCSP response thisUpdate");
		return 0;
	}

	time(&now);
	tmp = now + 5 * 60; /* allow five minute clock difference */
	if (X509_cmp_time(thisupd, &tmp) > 0) {
		wpa_printf(MSG_DEBUG, "OpenSSL: OCSP response not yet valid");
		return 0;
	}

	if (!nextupd)
		return 1; /* OK - no limit on response age */

	if (!ASN1_GENERALIZEDTIME_check(nextupd)) {
		wpa_printf(MSG_DEBUG,
			   "OpenSSL: Invalid OCSP response nextUpdate");
		return 0;
	}

	tmp = now - 5 * 60; /* allow five minute clock difference */
	if (X509_cmp_time(nextupd, &tmp) < 0) {
		wpa_printf(MSG_DEBUG, "OpenSSL: OCSP response expired");
		return 0;
	}

	if (ASN1_STRING_cmp(nextupd, thisupd) < 0) {
		wpa_printf(MSG_DEBUG,
			   "OpenSSL: OCSP response nextUpdate before thisUpdate");
		return 0;
	}

	/* Both thisUpdate and nextUpdate are valid */
	return -1;
}


static int issuer_match(X509 *cert, X509 *issuer, CertID *certid)
{
	X509_NAME *iname;
	ASN1_BIT_STRING *ikey;
	const EVP_MD *dgst;
	unsigned int len;
	unsigned char md[EVP_MAX_MD_SIZE];
	ASN1_OCTET_STRING *hash;
	char *txt;

	dgst = EVP_get_digestbyobj(certid->hashAlgorithm->algorithm);
	if (!dgst) {
		wpa_printf(MSG_DEBUG,
			   "OpenSSL: Could not find matching hash algorithm for OCSP");
		return -1;
	}

	iname = X509_get_issuer_name(cert);
	if (!X509_NAME_digest(iname, dgst, md, &len))
		return -1;
	hash = ASN1_OCTET_STRING_new();
	if (!hash)
		return -1;
	if (!ASN1_OCTET_STRING_set(hash, md, len)) {
		ASN1_OCTET_STRING_free(hash);
		return -1;
	}

	txt = octet_string_str(hash);
	if (txt) {
		wpa_printf(MSG_DEBUG, "OpenSSL: calculated issuerNameHash: %s",
			   txt);
		os_free(txt);
	}

	if (ASN1_OCTET_STRING_cmp(certid->issuerNameHash, hash)) {
		ASN1_OCTET_STRING_free(hash);
		return -1;
	}

	ikey = X509_get0_pubkey_bitstr(issuer);
	if (!ikey ||
	    !EVP_Digest(ikey->data, ikey->length, md, &len, dgst, NULL) ||
	    !ASN1_OCTET_STRING_set(hash, md, len)) {
		ASN1_OCTET_STRING_free(hash);
		return -1;
	}

	txt = octet_string_str(hash);
	if (txt) {
		wpa_printf(MSG_DEBUG, "OpenSSL: calculated issuerKeyHash: %s",
			   txt);
		os_free(txt);
	}

	if (ASN1_OCTET_STRING_cmp(certid->issuerKeyHash, hash)) {
		ASN1_OCTET_STRING_free(hash);
		return -1;
	}

	ASN1_OCTET_STRING_free(hash);
	return 0;
}


static X509 * ocsp_find_signer(STACK_OF(X509) *certs, ResponderID *rid)
{
	unsigned int i;
	unsigned char hash[SHA_DIGEST_LENGTH];

	if (rid->type == 0) {
		/* byName */
		return X509_find_by_subject(certs, rid->value.byName);
	}

	/* byKey */
	if (rid->value.byKey->length != SHA_DIGEST_LENGTH)
		return NULL;
	for (i = 0; i < sk_X509_num(certs); i++) {
		X509 *x = sk_X509_value(certs, i);

		X509_pubkey_digest(x, EVP_sha1(), hash, NULL);
		if (os_memcmp(rid->value.byKey->data, hash,
			      SHA_DIGEST_LENGTH) == 0)
			return x;
	}

	return NULL;
}


enum ocsp_result check_ocsp_resp(SSL_CTX *ssl_ctx, SSL *ssl, X509 *cert,
				 X509 *issuer, X509 *issuer_issuer)
{
	const uint8_t *resp_data;
	size_t resp_len;
	OCSPResponse *resp;
	int status;
	ResponseBytes *bytes;
	const u8 *basic_data;
	size_t basic_len;
	BasicOCSPResponse *basic;
	ResponseData *rd;
	char *txt;
	int i, num;
	unsigned int j, num_resp;
	SingleResponse *matching_resp = NULL, *cmp_sresp;
	enum ocsp_result result = OCSP_INVALID;
	X509_STORE *store;
	STACK_OF(X509) *untrusted = NULL, *certs = NULL, *chain = NULL;
	X509_STORE_CTX ctx;
	X509 *signer, *tmp_cert;
	int signer_trusted = 0;
	EVP_PKEY *skey;
	int ret;
	char buf[256];

	txt = integer_str(X509_get_serialNumber(cert));
	if (txt) {
		wpa_printf(MSG_DEBUG,
			   "OpenSSL: Searching OCSP response for peer certificate serialNumber: %s", txt);
		os_free(txt);
	}

	SSL_get0_ocsp_response(ssl, &resp_data, &resp_len);
	if (resp_data == NULL || resp_len == 0) {
		wpa_printf(MSG_DEBUG, "OpenSSL: No OCSP response received");
		return OCSP_NO_RESPONSE;
	}

	wpa_hexdump(MSG_DEBUG, "OpenSSL: OCSP response", resp_data, resp_len);

	resp = d2i_OCSPResponse(NULL, &resp_data, resp_len);
	if (!resp) {
		wpa_printf(MSG_INFO, "OpenSSL: Failed to parse OCSPResponse");
		return OCSP_INVALID;
	}

	status = ASN1_ENUMERATED_get(resp->responseStatus);
	if (status != 0) {
		wpa_printf(MSG_INFO, "OpenSSL: OCSP responder error %d",
			   status);
		return OCSP_INVALID;
	}

	bytes = resp->responseBytes;

	if (!bytes ||
	    OBJ_obj2nid(bytes->responseType) != NID_id_pkix_OCSP_basic) {
		wpa_printf(MSG_INFO,
			   "OpenSSL: Could not find BasicOCSPResponse");
		return OCSP_INVALID;
	}

	basic_data = ASN1_STRING_data(bytes->response);
	basic_len = ASN1_STRING_length(bytes->response);
	wpa_hexdump(MSG_DEBUG, "OpenSSL: BasicOCSPResponse",
		    basic_data, basic_len);

	basic = d2i_BasicOCSPResponse(NULL, &basic_data, basic_len);
	if (!basic) {
		wpa_printf(MSG_INFO,
			   "OpenSSL: Could not parse BasicOCSPResponse");
		OCSPResponse_free(resp);
		return OCSP_INVALID;
	}

	rd = basic->tbsResponseData;

	if (basic->certs) {
		untrusted = sk_X509_dup(basic->certs);
		if (!untrusted)
			goto fail;

		num = sk_X509_num(basic->certs);
		for (i = 0; i < num; i++) {
			X509 *extra_cert;

			extra_cert = sk_X509_value(basic->certs, i);
			X509_NAME_oneline(X509_get_subject_name(extra_cert),
					  buf, sizeof(buf));
			wpa_printf(MSG_DEBUG,
				   "OpenSSL: BasicOCSPResponse cert %s", buf);

			if (!sk_X509_push(untrusted, extra_cert)) {
				wpa_printf(MSG_DEBUG,
					   "OpenSSL: Could not add certificate to the untrusted stack");
			}
		}
	}

	store = SSL_CTX_get_cert_store(ssl_ctx);
	if (issuer) {
		if (X509_STORE_add_cert(store, issuer) != 1) {
			tls_show_errors(MSG_INFO, __func__,
					"OpenSSL: Could not add issuer to certificate store");
		}
		certs = sk_X509_new_null();
		if (certs) {
			tmp_cert = X509_dup(issuer);
			if (tmp_cert && !sk_X509_push(certs, tmp_cert)) {
				tls_show_errors(
					MSG_INFO, __func__,
					"OpenSSL: Could not add issuer to OCSP responder trust store");
				X509_free(tmp_cert);
				sk_X509_free(certs);
				certs = NULL;
			}
			if (certs && issuer_issuer) {
				tmp_cert = X509_dup(issuer_issuer);
				if (tmp_cert &&
				    !sk_X509_push(certs, tmp_cert)) {
					tls_show_errors(
						MSG_INFO, __func__,
						"OpenSSL: Could not add issuer's issuer to OCSP responder trust store");
					X509_free(tmp_cert);
				}
			}
		}
	}

	signer = ocsp_find_signer(certs, rd->responderID);
	if (!signer)
		signer = ocsp_find_signer(untrusted, rd->responderID);
	else
		signer_trusted = 1;
	if (!signer) {
		wpa_printf(MSG_DEBUG,
			   "OpenSSL: Could not find OCSP signer certificate");
		goto fail;
	}

	skey = X509_get_pubkey(signer);
	if (!skey) {
		wpa_printf(MSG_DEBUG,
			   "OpenSSL: Could not get OCSP signer public key");
		goto fail;
	}
	if (ASN1_item_verify(ASN1_ITEM_rptr(ResponseData),
			     basic->signatureAlgorithm, basic->signature,
			     basic->tbsResponseData, skey) <= 0) {
		wpa_printf(MSG_DEBUG,
			   "OpenSSL: BasicOCSPResponse signature is invalid");
		goto fail;
	}

	X509_NAME_oneline(X509_get_subject_name(signer), buf, sizeof(buf));
	wpa_printf(MSG_DEBUG,
		   "OpenSSL: Found OCSP signer certificate %s and verified BasicOCSPResponse signature",
		   buf);

	if (!X509_STORE_CTX_init(&ctx, store, signer, untrusted))
		goto fail;
	X509_STORE_CTX_set_purpose(&ctx, X509_PURPOSE_OCSP_HELPER);
	ret = X509_verify_cert(&ctx);
	chain = X509_STORE_CTX_get1_chain(&ctx);
	X509_STORE_CTX_cleanup(&ctx);
	if (ret <= 0) {
		wpa_printf(MSG_DEBUG,
			   "OpenSSL: Could not validate OCSP signer certificate");
		goto fail;
	}

	if (!chain || sk_X509_num(chain) <= 0) {
		wpa_printf(MSG_DEBUG, "OpenSSL: No OCSP signer chain found");
		goto fail;
	}

	if (!signer_trusted) {
		X509_check_purpose(signer, -1, 0);
		if ((signer->ex_flags & EXFLAG_XKUSAGE) &&
		    (signer->ex_xkusage & XKU_OCSP_SIGN)) {
			wpa_printf(MSG_DEBUG,
				   "OpenSSL: OCSP signer certificate delegation OK");
		} else {
			tmp_cert = sk_X509_value(chain, sk_X509_num(chain) - 1);
			if (X509_check_trust(tmp_cert, NID_OCSP_sign, 0) !=
			    X509_TRUST_TRUSTED) {
				wpa_printf(MSG_DEBUG,
					   "OpenSSL: OCSP signer certificate not trusted");
				result = OCSP_NO_RESPONSE;
				goto fail;
			}
		}
	}

	wpa_printf(MSG_DEBUG, "OpenSSL: OCSP version: %lu",
		   ASN1_INTEGER_get(rd->version));

	txt = responderid_str(rd->responderID);
	if (txt) {
		wpa_printf(MSG_DEBUG, "OpenSSL: OCSP responderID: %s",
			   txt);
		os_free(txt);
	}

	txt = generalizedtime_str(rd->producedAt);
	if (txt) {
		wpa_printf(MSG_DEBUG, "OpenSSL: OCSP producedAt: %s",
			   txt);
		os_free(txt);
	}

	num_resp = sk_SingleResponse_num(rd->responses);
	if (num_resp == 0) {
		wpa_printf(MSG_DEBUG,
			   "OpenSSL: No OCSP SingleResponse within BasicOCSPResponse");
		result = OCSP_NO_RESPONSE;
		goto fail;
	}
	cmp_sresp = sk_SingleResponse_value(rd->responses, 0);
	for (j = 0; j < num_resp; j++) {
		SingleResponse *sresp;
		CertID *cid1, *cid2;

		sresp = sk_SingleResponse_value(rd->responses, j);
		wpa_printf(MSG_DEBUG, "OpenSSL: OCSP SingleResponse %u/%u",
			   j + 1, num_resp);

		txt = algor_str(sresp->certID->hashAlgorithm);
		if (txt) {
			wpa_printf(MSG_DEBUG,
				   "OpenSSL: certID hashAlgorithm: %s", txt);
			os_free(txt);
		}

		txt = octet_string_str(sresp->certID->issuerNameHash);
		if (txt) {
			wpa_printf(MSG_DEBUG,
				   "OpenSSL: certID issuerNameHash: %s", txt);
			os_free(txt);
		}

		txt = octet_string_str(sresp->certID->issuerKeyHash);
		if (txt) {
			wpa_printf(MSG_DEBUG,
				   "OpenSSL: certID issuerKeyHash: %s", txt);
			os_free(txt);
		}

		txt = integer_str(sresp->certID->serialNumber);
		if (txt) {
			wpa_printf(MSG_DEBUG,
				   "OpenSSL: certID serialNumber: %s", txt);
			os_free(txt);
		}

		switch (sresp->certStatus->type) {
		case 0:
			wpa_printf(MSG_DEBUG, "OpenSSL: certStatus: good");
			break;
		case 1:
			wpa_printf(MSG_DEBUG, "OpenSSL: certStatus: revoked");
			break;
		default:
			wpa_printf(MSG_DEBUG, "OpenSSL: certStatus: unknown");
			break;
		}

		txt = generalizedtime_str(sresp->thisUpdate);
		if (txt) {
			wpa_printf(MSG_DEBUG, "OpenSSL: thisUpdate: %s", txt);
			os_free(txt);
		}

		if (sresp->nextUpdate) {
			txt = generalizedtime_str(sresp->nextUpdate);
			if (txt) {
				wpa_printf(MSG_DEBUG, "OpenSSL: nextUpdate: %s",
					   txt);
				os_free(txt);
			}
		}

		txt = extensions_str("singleExtensions",
				     sresp->singleExtensions);
		if (txt) {
			wpa_printf(MSG_DEBUG, "OpenSSL: %s", txt);
			os_free(txt);
		}

		cid1 = cmp_sresp->certID;
		cid2 = sresp->certID;
		if (j > 0 &&
		    (OBJ_cmp(cid1->hashAlgorithm->algorithm,
			     cid2->hashAlgorithm->algorithm) != 0 ||
		     ASN1_OCTET_STRING_cmp(cid1->issuerNameHash,
					   cid2->issuerNameHash) != 0 ||
		     ASN1_OCTET_STRING_cmp(cid1->issuerKeyHash,
					   cid2->issuerKeyHash) != 0)) {
			wpa_printf(MSG_DEBUG,
				   "OpenSSL: Different OCSP response issuer information between SingleResponse values within BasicOCSPResponse");
			goto fail;
		}

		if (!matching_resp && issuer &&
		    ASN1_INTEGER_cmp(sresp->certID->serialNumber,
				     X509_get_serialNumber(cert)) == 0 &&
		    issuer_match(cert, issuer, sresp->certID) == 0) {
			wpa_printf(MSG_DEBUG,
				   "OpenSSL: This response matches peer certificate");
			matching_resp = sresp;
		}
	}

	txt = extensions_str("responseExtensions", rd->responseExtensions);
	if (txt) {
		wpa_printf(MSG_DEBUG, "OpenSSL: %s", txt);
		os_free(txt);
	}

	if (!matching_resp) {
		wpa_printf(MSG_DEBUG,
			   "OpenSSL: Could not find OCSP response that matches the peer certificate");
		result = OCSP_NO_RESPONSE;
		goto fail;
	}

	if (!ocsp_resp_valid(matching_resp->thisUpdate,
			     matching_resp->nextUpdate)) {
		wpa_printf(MSG_DEBUG,
			   "OpenSSL: OCSP response not valid at this time");
		goto fail;
	}

	if (matching_resp->certStatus->type == 1) {
		wpa_printf(MSG_DEBUG,
			   "OpenSSL: OCSP response indicated that the peer certificate has been revoked");
		result = OCSP_REVOKED;
		goto fail;
	}

	if (matching_resp->certStatus->type != 0) {
		wpa_printf(MSG_DEBUG,
			   "OpenSSL: OCSP response did not indicate good status");
		result = OCSP_NO_RESPONSE;
		goto fail;
	}

	/* OCSP response indicated the certificate is good. */
	result = OCSP_GOOD;
fail:
	sk_X509_pop_free(chain, X509_free);
	sk_X509_free(untrusted);
	sk_X509_pop_free(certs, X509_free);
	BasicOCSPResponse_free(basic);
	OCSPResponse_free(resp);

	return result;
}

#endif /* OPENSSL_IS_BORINGSSL */
