/*
 * Hotspot 2.0 OSU client - EST client
 * Copyright (c) 2012-2014, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/pkcs7.h>
#include <openssl/rsa.h>
#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#ifdef OPENSSL_IS_BORINGSSL
#include <openssl/buf.h>
#endif /* OPENSSL_IS_BORINGSSL */

#include "common.h"
#include "utils/base64.h"
#include "utils/xml-utils.h"
#include "utils/http-utils.h"
#include "osu_client.h"


static int pkcs7_to_cert(struct hs20_osu_client *ctx, const u8 *pkcs7,
			 size_t len, char *pem_file, char *der_file)
{
#ifdef OPENSSL_IS_BORINGSSL
	CBS pkcs7_cbs;
#else /* OPENSSL_IS_BORINGSSL */
	PKCS7 *p7 = NULL;
	const unsigned char *p = pkcs7;
#endif /* OPENSSL_IS_BORINGSSL */
	STACK_OF(X509) *certs;
	int i, num, ret = -1;
	BIO *out = NULL;

#ifdef OPENSSL_IS_BORINGSSL
	certs = sk_X509_new_null();
	if (!certs)
		goto fail;
	CBS_init(&pkcs7_cbs, pkcs7, len);
	if (!PKCS7_get_certificates(certs, &pkcs7_cbs)) {
		wpa_printf(MSG_INFO, "Could not parse PKCS#7 object: %s",
			   ERR_error_string(ERR_get_error(), NULL));
		write_result(ctx, "Could not parse PKCS#7 object from EST");
		goto fail;
	}
#else /* OPENSSL_IS_BORINGSSL */
	p7 = d2i_PKCS7(NULL, &p, len);
	if (p7 == NULL) {
		wpa_printf(MSG_INFO, "Could not parse PKCS#7 object: %s",
			   ERR_error_string(ERR_get_error(), NULL));
		write_result(ctx, "Could not parse PKCS#7 object from EST");
		goto fail;
	}

	switch (OBJ_obj2nid(p7->type)) {
	case NID_pkcs7_signed:
		certs = p7->d.sign->cert;
		break;
	case NID_pkcs7_signedAndEnveloped:
		certs = p7->d.signed_and_enveloped->cert;
		break;
	default:
		certs = NULL;
		break;
	}
#endif /* OPENSSL_IS_BORINGSSL */

	if (!certs || ((num = sk_X509_num(certs)) == 0)) {
		wpa_printf(MSG_INFO, "No certificates found in PKCS#7 object");
		write_result(ctx, "No certificates found in PKCS#7 object");
		goto fail;
	}

	if (der_file) {
		FILE *f = fopen(der_file, "wb");
		if (f == NULL)
			goto fail;
		i2d_X509_fp(f, sk_X509_value(certs, 0));
		fclose(f);
	}

	if (pem_file) {
		out = BIO_new(BIO_s_file());
		if (out == NULL ||
		    BIO_write_filename(out, pem_file) <= 0)
			goto fail;

		for (i = 0; i < num; i++) {
			X509 *cert = sk_X509_value(certs, i);
			X509_print(out, cert);
			PEM_write_bio_X509(out, cert);
			BIO_puts(out, "\n");
		}
	}

	ret = 0;

fail:
#ifdef OPENSSL_IS_BORINGSSL
	if (certs)
		sk_X509_pop_free(certs, X509_free);
#else /* OPENSSL_IS_BORINGSSL */
	PKCS7_free(p7);
#endif /* OPENSSL_IS_BORINGSSL */
	if (out)
		BIO_free_all(out);

	return ret;
}


int est_load_cacerts(struct hs20_osu_client *ctx, const char *url)
{
	char *buf, *resp;
	size_t buflen;
	unsigned char *pkcs7;
	size_t pkcs7_len, resp_len;
	int res;

	buflen = os_strlen(url) + 100;
	buf = os_malloc(buflen);
	if (buf == NULL)
		return -1;

	os_snprintf(buf, buflen, "%s/cacerts", url);
	wpa_printf(MSG_INFO, "Download EST cacerts from %s", buf);
	write_summary(ctx, "Download EST cacerts from %s", buf);
	ctx->no_osu_cert_validation = 1;
	http_ocsp_set(ctx->http, 1);
	res = http_download_file(ctx->http, buf, "Cert/est-cacerts.txt",
				 ctx->ca_fname);
	http_ocsp_set(ctx->http,
		      (ctx->workarounds & WORKAROUND_OCSP_OPTIONAL) ? 1 : 2);
	ctx->no_osu_cert_validation = 0;
	if (res < 0) {
		wpa_printf(MSG_INFO, "Failed to download EST cacerts from %s",
			   buf);
		write_result(ctx, "Failed to download EST cacerts from %s",
			     buf);
		os_free(buf);
		return -1;
	}
	os_free(buf);

	resp = os_readfile("Cert/est-cacerts.txt", &resp_len);
	if (resp == NULL) {
		wpa_printf(MSG_INFO, "Could not read Cert/est-cacerts.txt");
		write_result(ctx, "Could not read EST cacerts");
		return -1;
	}

	pkcs7 = base64_decode((unsigned char *) resp, resp_len, &pkcs7_len);
	if (pkcs7 && pkcs7_len < resp_len / 2) {
		wpa_printf(MSG_INFO, "Too short base64 decode (%u bytes; downloaded %u bytes) - assume this was binary",
			   (unsigned int) pkcs7_len, (unsigned int) resp_len);
		os_free(pkcs7);
		pkcs7 = NULL;
	}
	if (pkcs7 == NULL) {
		wpa_printf(MSG_INFO, "EST workaround - Could not decode base64, assume this is DER encoded PKCS7");
		pkcs7 = os_malloc(resp_len);
		if (pkcs7) {
			os_memcpy(pkcs7, resp, resp_len);
			pkcs7_len = resp_len;
		}
	}
	os_free(resp);

	if (pkcs7 == NULL) {
		wpa_printf(MSG_INFO, "Could not fetch PKCS7 cacerts");
		write_result(ctx, "Could not fetch EST PKCS#7 cacerts");
		return -1;
	}

	res = pkcs7_to_cert(ctx, pkcs7, pkcs7_len, "Cert/est-cacerts.pem",
			    NULL);
	os_free(pkcs7);
	if (res < 0) {
		wpa_printf(MSG_INFO, "Could not parse CA certs from PKCS#7 cacerts response");
		write_result(ctx, "Could not parse CA certs from EST PKCS#7 cacerts response");
		return -1;
	}
	unlink("Cert/est-cacerts.txt");

	return 0;
}


/*
 * CsrAttrs ::= SEQUENCE SIZE (0..MAX) OF AttrOrOID
 *
 * AttrOrOID ::= CHOICE {
 *   oid OBJECT IDENTIFIER,
 *   attribute Attribute }
 *
 * Attribute ::= SEQUENCE {
 *   type OBJECT IDENTIFIER,
 *   values SET SIZE(1..MAX) OF OBJECT IDENTIFIER }
 */

typedef struct {
	ASN1_OBJECT *type;
	STACK_OF(ASN1_OBJECT) *values;
} Attribute;

typedef struct {
	int type;
	union {
		ASN1_OBJECT *oid;
		Attribute *attribute;
	} d;
} AttrOrOID;

typedef struct {
	int type;
	STACK_OF(AttrOrOID) *attrs;
} CsrAttrs;

ASN1_SEQUENCE(Attribute) = {
	ASN1_SIMPLE(Attribute, type, ASN1_OBJECT),
	ASN1_SET_OF(Attribute, values, ASN1_OBJECT)
} ASN1_SEQUENCE_END(Attribute);

ASN1_CHOICE(AttrOrOID) = {
	ASN1_SIMPLE(AttrOrOID, d.oid, ASN1_OBJECT),
	ASN1_SIMPLE(AttrOrOID, d.attribute, Attribute)
} ASN1_CHOICE_END(AttrOrOID);

ASN1_CHOICE(CsrAttrs) = {
	ASN1_SEQUENCE_OF(CsrAttrs, attrs, AttrOrOID)
} ASN1_CHOICE_END(CsrAttrs);

IMPLEMENT_ASN1_FUNCTIONS(CsrAttrs);


static void add_csrattrs_oid(struct hs20_osu_client *ctx, ASN1_OBJECT *oid,
			     STACK_OF(X509_EXTENSION) *exts)
{
	char txt[100];
	int res;

	if (!oid)
		return;

	res = OBJ_obj2txt(txt, sizeof(txt), oid, 1);
	if (res < 0 || res >= (int) sizeof(txt))
		return;

	if (os_strcmp(txt, "1.2.840.113549.1.9.7") == 0) {
		wpa_printf(MSG_INFO, "TODO: csrattr challengePassword");
	} else if (os_strcmp(txt, "1.2.840.113549.1.1.11") == 0) {
		wpa_printf(MSG_INFO, "csrattr sha256WithRSAEncryption");
	} else {
		wpa_printf(MSG_INFO, "Ignore unsupported csrattr oid %s", txt);
	}
}


static void add_csrattrs_ext_req(struct hs20_osu_client *ctx,
				 STACK_OF(ASN1_OBJECT) *values,
				 STACK_OF(X509_EXTENSION) *exts)
{
	char txt[100];
	int i, num, res;

	num = sk_ASN1_OBJECT_num(values);
	for (i = 0; i < num; i++) {
		ASN1_OBJECT *oid = sk_ASN1_OBJECT_value(values, i);

		res = OBJ_obj2txt(txt, sizeof(txt), oid, 1);
		if (res < 0 || res >= (int) sizeof(txt))
			continue;

		if (os_strcmp(txt, "1.3.6.1.1.1.1.22") == 0) {
			wpa_printf(MSG_INFO, "TODO: extReq macAddress");
		} else if (os_strcmp(txt, "1.3.6.1.4.1.40808.1.1.3") == 0) {
			wpa_printf(MSG_INFO, "TODO: extReq imei");
		} else if (os_strcmp(txt, "1.3.6.1.4.1.40808.1.1.4") == 0) {
			wpa_printf(MSG_INFO, "TODO: extReq meid");
		} else if (os_strcmp(txt, "1.3.6.1.4.1.40808.1.1.5") == 0) {
			wpa_printf(MSG_INFO, "TODO: extReq DevId");
		} else {
			wpa_printf(MSG_INFO, "Ignore unsupported cstattr extensionsRequest %s",
				   txt);
		}
	}
}


static void add_csrattrs_attr(struct hs20_osu_client *ctx, Attribute *attr,
			      STACK_OF(X509_EXTENSION) *exts)
{
	char txt[100], txt2[100];
	int i, num, res;

	if (!attr || !attr->type || !attr->values)
		return;

	res = OBJ_obj2txt(txt, sizeof(txt), attr->type, 1);
	if (res < 0 || res >= (int) sizeof(txt))
		return;

	if (os_strcmp(txt, "1.2.840.113549.1.9.14") == 0) {
		add_csrattrs_ext_req(ctx, attr->values, exts);
		return;
	}

	num = sk_ASN1_OBJECT_num(attr->values);
	for (i = 0; i < num; i++) {
		ASN1_OBJECT *oid = sk_ASN1_OBJECT_value(attr->values, i);

		res = OBJ_obj2txt(txt2, sizeof(txt2), oid, 1);
		if (res < 0 || res >= (int) sizeof(txt2))
			continue;

		wpa_printf(MSG_INFO, "Ignore unsupported cstattr::attr %s oid %s",
			   txt, txt2);
	}
}


static void add_csrattrs(struct hs20_osu_client *ctx, CsrAttrs *csrattrs,
			 STACK_OF(X509_EXTENSION) *exts)
{
	int i, num;

	if (!csrattrs || ! csrattrs->attrs)
		return;

#ifdef OPENSSL_IS_BORINGSSL
	num = sk_num(CHECKED_CAST(_STACK *, STACK_OF(AttrOrOID) *,
				  csrattrs->attrs));
	for (i = 0; i < num; i++) {
		AttrOrOID *ao = sk_value(
			CHECKED_CAST(_STACK *, const STACK_OF(AttrOrOID) *,
				     csrattrs->attrs), i);
		switch (ao->type) {
		case 0:
			add_csrattrs_oid(ctx, ao->d.oid, exts);
			break;
		case 1:
			add_csrattrs_attr(ctx, ao->d.attribute, exts);
			break;
		}
	}
#else /* OPENSSL_IS_BORINGSSL */
	num = SKM_sk_num(AttrOrOID, csrattrs->attrs);
	for (i = 0; i < num; i++) {
		AttrOrOID *ao = SKM_sk_value(AttrOrOID, csrattrs->attrs, i);
		switch (ao->type) {
		case 0:
			add_csrattrs_oid(ctx, ao->d.oid, exts);
			break;
		case 1:
			add_csrattrs_attr(ctx, ao->d.attribute, exts);
			break;
		}
	}
#endif /* OPENSSL_IS_BORINGSSL */
}


static int generate_csr(struct hs20_osu_client *ctx, char *key_pem,
			char *csr_pem, char *est_req, char *old_cert,
			CsrAttrs *csrattrs)
{
	EVP_PKEY_CTX *pctx = NULL;
	EVP_PKEY *pkey = NULL;
	RSA *rsa;
	X509_REQ *req = NULL;
	int ret = -1;
	unsigned int val;
	X509_NAME *subj = NULL;
	char name[100];
	STACK_OF(X509_EXTENSION) *exts = NULL;
	X509_EXTENSION *ex;
	BIO *out;
	CONF *ctmp = NULL;

	wpa_printf(MSG_INFO, "Generate RSA private key");
	write_summary(ctx, "Generate RSA private key");
	pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
	if (!pctx)
		return -1;

	if (EVP_PKEY_keygen_init(pctx) <= 0)
		goto fail;

	if (EVP_PKEY_CTX_set_rsa_keygen_bits(pctx, 2048) <= 0)
		goto fail;

	if (EVP_PKEY_keygen(pctx, &pkey) <= 0)
		goto fail;
	EVP_PKEY_CTX_free(pctx);
	pctx = NULL;

	rsa = EVP_PKEY_get1_RSA(pkey);
	if (rsa == NULL)
		goto fail;

	if (key_pem) {
		FILE *f = fopen(key_pem, "wb");
		if (f == NULL)
			goto fail;
		if (!PEM_write_RSAPrivateKey(f, rsa, NULL, NULL, 0, NULL,
					     NULL)) {
			wpa_printf(MSG_INFO, "Could not write private key: %s",
				   ERR_error_string(ERR_get_error(), NULL));
			fclose(f);
			goto fail;
		}
		fclose(f);
	}

	wpa_printf(MSG_INFO, "Generate CSR");
	write_summary(ctx, "Generate CSR");
	req = X509_REQ_new();
	if (req == NULL)
		goto fail;

	if (old_cert) {
		FILE *f;
		X509 *cert;
		int res;

		f = fopen(old_cert, "r");
		if (f == NULL)
			goto fail;
		cert = PEM_read_X509(f, NULL, NULL, NULL);
		fclose(f);

		if (cert == NULL)
			goto fail;
		res = X509_REQ_set_subject_name(req,
						X509_get_subject_name(cert));
		X509_free(cert);
		if (!res)
			goto fail;
	} else {
		os_get_random((u8 *) &val, sizeof(val));
		os_snprintf(name, sizeof(name), "cert-user-%u", val);
		subj = X509_NAME_new();
		if (subj == NULL ||
		    !X509_NAME_add_entry_by_txt(subj, "CN", MBSTRING_ASC,
						(unsigned char *) name,
						-1, -1, 0) ||
		    !X509_REQ_set_subject_name(req, subj))
			goto fail;
		X509_NAME_free(subj);
		subj = NULL;
	}

	if (!X509_REQ_set_pubkey(req, pkey))
		goto fail;

	exts = sk_X509_EXTENSION_new_null();
	if (!exts)
		goto fail;

	ex = X509V3_EXT_nconf_nid(ctmp, NULL, NID_basic_constraints,
				  "CA:FALSE");
	if (ex == NULL ||
	    !sk_X509_EXTENSION_push(exts, ex))
		goto fail;

	ex = X509V3_EXT_nconf_nid(ctmp, NULL, NID_key_usage,
				  "nonRepudiation,digitalSignature,keyEncipherment");
	if (ex == NULL ||
	    !sk_X509_EXTENSION_push(exts, ex))
		goto fail;

	ex = X509V3_EXT_nconf_nid(ctmp, NULL, NID_ext_key_usage,
				  "1.3.6.1.4.1.40808.1.1.2");
	if (ex == NULL ||
	    !sk_X509_EXTENSION_push(exts, ex))
		goto fail;

	add_csrattrs(ctx, csrattrs, exts);

	if (!X509_REQ_add_extensions(req, exts))
		goto fail;
	sk_X509_EXTENSION_pop_free(exts, X509_EXTENSION_free);
	exts = NULL;

	if (!X509_REQ_sign(req, pkey, EVP_sha256()))
		goto fail;

	out = BIO_new(BIO_s_mem());
	if (out) {
		char *txt;
		size_t rlen;

#if !defined(ANDROID) || !defined(OPENSSL_IS_BORINGSSL)
		X509_REQ_print(out, req);
#endif
		rlen = BIO_ctrl_pending(out);
		txt = os_malloc(rlen + 1);
		if (txt) {
			int res = BIO_read(out, txt, rlen);
			if (res > 0) {
				txt[res] = '\0';
				wpa_printf(MSG_MSGDUMP, "OpenSSL: Certificate request:\n%s",
					   txt);
			}
			os_free(txt);
		}
		BIO_free(out);
	}

	if (csr_pem) {
		FILE *f = fopen(csr_pem, "w");
		if (f == NULL)
			goto fail;
#if !defined(ANDROID) || !defined(OPENSSL_IS_BORINGSSL)
		X509_REQ_print_fp(f, req);
#endif
		if (!PEM_write_X509_REQ(f, req)) {
			fclose(f);
			goto fail;
		}
		fclose(f);
	}

	if (est_req) {
		BIO *mem = BIO_new(BIO_s_mem());
		BUF_MEM *ptr;
		char *pos, *end, *buf_end;
		FILE *f;

		if (mem == NULL)
			goto fail;
		if (!PEM_write_bio_X509_REQ(mem, req)) {
			BIO_free(mem);
			goto fail;
		}

		BIO_get_mem_ptr(mem, &ptr);
		pos = ptr->data;
		buf_end = pos + ptr->length;

		/* Remove START/END lines */
		while (pos < buf_end && *pos != '\n')
			pos++;
		if (pos == buf_end) {
			BIO_free(mem);
			goto fail;
		}
		pos++;

		end = pos;
		while (end < buf_end && *end != '-')
			end++;

		f = fopen(est_req, "w");
		if (f == NULL) {
			BIO_free(mem);
			goto fail;
		}
		fwrite(pos, end - pos, 1, f);
		fclose(f);

		BIO_free(mem);
	}

	ret = 0;
fail:
	if (exts)
		sk_X509_EXTENSION_pop_free(exts, X509_EXTENSION_free);
	if (subj)
		X509_NAME_free(subj);
	if (req)
		X509_REQ_free(req);
	if (pkey)
		EVP_PKEY_free(pkey);
	if (pctx)
		EVP_PKEY_CTX_free(pctx);
	return ret;
}


int est_build_csr(struct hs20_osu_client *ctx, const char *url)
{
	char *buf;
	size_t buflen;
	int res;
	char old_cert_buf[200];
	char *old_cert = NULL;
	CsrAttrs *csrattrs = NULL;

	buflen = os_strlen(url) + 100;
	buf = os_malloc(buflen);
	if (buf == NULL)
		return -1;

	os_snprintf(buf, buflen, "%s/csrattrs", url);
	wpa_printf(MSG_INFO, "Download csrattrs from %s", buf);
	write_summary(ctx, "Download EST csrattrs from %s", buf);
	ctx->no_osu_cert_validation = 1;
	http_ocsp_set(ctx->http, 1);
	res = http_download_file(ctx->http, buf, "Cert/est-csrattrs.txt",
				 ctx->ca_fname);
	http_ocsp_set(ctx->http,
		      (ctx->workarounds & WORKAROUND_OCSP_OPTIONAL) ? 1 : 2);
	ctx->no_osu_cert_validation = 0;
	os_free(buf);
	if (res < 0) {
		wpa_printf(MSG_INFO, "Failed to download EST csrattrs - assume no extra attributes are needed");
	} else {
		size_t resp_len;
		char *resp;
		unsigned char *attrs;
		const unsigned char *pos;
		size_t attrs_len;

		resp = os_readfile("Cert/est-csrattrs.txt", &resp_len);
		if (resp == NULL) {
			wpa_printf(MSG_INFO, "Could not read csrattrs");
			return -1;
		}

		attrs = base64_decode((unsigned char *) resp, resp_len,
				      &attrs_len);
		os_free(resp);

		if (attrs == NULL) {
			wpa_printf(MSG_INFO, "Could not base64 decode csrattrs");
			return -1;
		}
		unlink("Cert/est-csrattrs.txt");

		pos = attrs;
		csrattrs = d2i_CsrAttrs(NULL, &pos, attrs_len);
		os_free(attrs);
		if (csrattrs == NULL) {
			wpa_printf(MSG_INFO, "Failed to parse csrattrs ASN.1");
			/* Continue assuming no additional requirements */
		}
	}

	if (ctx->client_cert_present) {
		os_snprintf(old_cert_buf, sizeof(old_cert_buf),
			    "SP/%s/client-cert.pem", ctx->fqdn);
		old_cert = old_cert_buf;
	}

	res = generate_csr(ctx, "Cert/privkey-plain.pem", "Cert/est-req.pem",
			   "Cert/est-req.b64", old_cert, csrattrs);
	if (csrattrs)
		CsrAttrs_free(csrattrs);

	return res;
}


int est_simple_enroll(struct hs20_osu_client *ctx, const char *url,
		      const char *user, const char *pw)
{
	char *buf, *resp, *req, *req2;
	size_t buflen, resp_len, len, pkcs7_len;
	unsigned char *pkcs7;
	char client_cert_buf[200];
	char client_key_buf[200];
	const char *client_cert = NULL, *client_key = NULL;
	int res;

	req = os_readfile("Cert/est-req.b64", &len);
	if (req == NULL) {
		wpa_printf(MSG_INFO, "Could not read Cert/req.b64");
		return -1;
	}
	req2 = os_realloc(req, len + 1);
	if (req2 == NULL) {
		os_free(req);
		return -1;
	}
	req2[len] = '\0';
	req = req2;
	wpa_printf(MSG_DEBUG, "EST simpleenroll request: %s", req);

	buflen = os_strlen(url) + 100;
	buf = os_malloc(buflen);
	if (buf == NULL) {
		os_free(req);
		return -1;
	}

	if (ctx->client_cert_present) {
		os_snprintf(buf, buflen, "%s/simplereenroll", url);
		os_snprintf(client_cert_buf, sizeof(client_cert_buf),
			    "SP/%s/client-cert.pem", ctx->fqdn);
		client_cert = client_cert_buf;
		os_snprintf(client_key_buf, sizeof(client_key_buf),
			    "SP/%s/client-key.pem", ctx->fqdn);
		client_key = client_key_buf;
	} else
		os_snprintf(buf, buflen, "%s/simpleenroll", url);
	wpa_printf(MSG_INFO, "EST simpleenroll URL: %s", buf);
	write_summary(ctx, "EST simpleenroll URL: %s", buf);
	ctx->no_osu_cert_validation = 1;
	http_ocsp_set(ctx->http, 1);
	resp = http_post(ctx->http, buf, req, "application/pkcs10",
			 "Content-Transfer-Encoding: base64",
			 ctx->ca_fname, user, pw, client_cert, client_key,
			 &resp_len);
	http_ocsp_set(ctx->http,
		      (ctx->workarounds & WORKAROUND_OCSP_OPTIONAL) ? 1 : 2);
	ctx->no_osu_cert_validation = 0;
	os_free(buf);
	if (resp == NULL) {
		wpa_printf(MSG_INFO, "EST certificate enrollment failed");
		write_result(ctx, "EST certificate enrollment failed");
		return -1;
	}
	wpa_printf(MSG_DEBUG, "EST simpleenroll response: %s", resp);

	pkcs7 = base64_decode((unsigned char *) resp, resp_len, &pkcs7_len);
	if (pkcs7 == NULL) {
		wpa_printf(MSG_INFO, "EST workaround - Could not decode base64, assume this is DER encoded PKCS7");
		pkcs7 = os_malloc(resp_len);
		if (pkcs7) {
			os_memcpy(pkcs7, resp, resp_len);
			pkcs7_len = resp_len;
		}
	}
	os_free(resp);

	if (pkcs7 == NULL) {
		wpa_printf(MSG_INFO, "Failed to parse simpleenroll base64 response");
		write_result(ctx, "Failed to parse EST simpleenroll base64 response");
		return -1;
	}

	res = pkcs7_to_cert(ctx, pkcs7, pkcs7_len, "Cert/est_cert.pem",
			    "Cert/est_cert.der");
	os_free(pkcs7);

	if (res < 0) {
		wpa_printf(MSG_INFO, "EST: Failed to extract certificate from PKCS7 file");
		write_result(ctx, "EST: Failed to extract certificate from EST PKCS7 file");
		return -1;
	}

	wpa_printf(MSG_INFO, "EST simple%senroll completed successfully",
		   ctx->client_cert_present ? "re" : "");
	write_summary(ctx, "EST simple%senroll completed successfully",
		      ctx->client_cert_present ? "re" : "");

	return 0;
}
