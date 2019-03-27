/*
 * HTTP wrapper for libcurl
 * Copyright (c) 2012-2014, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"
#include <curl/curl.h>
#ifdef EAP_TLS_OPENSSL
#include <openssl/ssl.h>
#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/x509v3.h>

#ifdef SSL_set_tlsext_status_type
#ifndef OPENSSL_NO_TLSEXT
#define HAVE_OCSP
#include <openssl/err.h>
#include <openssl/ocsp.h>
#endif /* OPENSSL_NO_TLSEXT */
#endif /* SSL_set_tlsext_status_type */
#endif /* EAP_TLS_OPENSSL */

#include "common.h"
#include "xml-utils.h"
#include "http-utils.h"
#ifdef EAP_TLS_OPENSSL
#include "crypto/tls_openssl.h"
#endif /* EAP_TLS_OPENSSL */


struct http_ctx {
	void *ctx;
	struct xml_node_ctx *xml;
	CURL *curl;
	struct curl_slist *curl_hdr;
	char *svc_address;
	char *svc_ca_fname;
	char *svc_username;
	char *svc_password;
	char *svc_client_cert;
	char *svc_client_key;
	char *curl_buf;
	size_t curl_buf_len;

	int (*cert_cb)(void *ctx, struct http_cert *cert);
	void *cert_cb_ctx;

	enum {
		NO_OCSP, OPTIONAL_OCSP, MANDATORY_OCSP
	} ocsp;
	X509 *peer_cert;
	X509 *peer_issuer;
	X509 *peer_issuer_issuer;

	const char *last_err;
};


static void clear_curl(struct http_ctx *ctx)
{
	if (ctx->curl) {
		curl_easy_cleanup(ctx->curl);
		ctx->curl = NULL;
	}
	if (ctx->curl_hdr) {
		curl_slist_free_all(ctx->curl_hdr);
		ctx->curl_hdr = NULL;
	}
}


static void clone_str(char **dst, const char *src)
{
	os_free(*dst);
	if (src)
		*dst = os_strdup(src);
	else
		*dst = NULL;
}


static void debug_dump(struct http_ctx *ctx, const char *title,
		       const char *buf, size_t len)
{
	char *txt;
	size_t i;

	for (i = 0; i < len; i++) {
		if (buf[i] < 32 && buf[i] != '\t' && buf[i] != '\n' &&
		    buf[i] != '\r') {
			wpa_hexdump_ascii(MSG_MSGDUMP, title, buf, len);
			return;
		}
	}

	txt = os_malloc(len + 1);
	if (txt == NULL)
		return;
	os_memcpy(txt, buf, len);
	txt[len] = '\0';
	while (len > 0) {
		len--;
		if (txt[len] == '\n' || txt[len] == '\r')
			txt[len] = '\0';
		else
			break;
	}
	wpa_printf(MSG_MSGDUMP, "%s[%s]", title, txt);
	os_free(txt);
}


static int curl_cb_debug(CURL *curl, curl_infotype info, char *buf, size_t len,
			 void *userdata)
{
	struct http_ctx *ctx = userdata;
	switch (info) {
	case CURLINFO_TEXT:
		debug_dump(ctx, "CURLINFO_TEXT", buf, len);
		break;
	case CURLINFO_HEADER_IN:
		debug_dump(ctx, "CURLINFO_HEADER_IN", buf, len);
		break;
	case CURLINFO_HEADER_OUT:
		debug_dump(ctx, "CURLINFO_HEADER_OUT", buf, len);
		break;
	case CURLINFO_DATA_IN:
		debug_dump(ctx, "CURLINFO_DATA_IN", buf, len);
		break;
	case CURLINFO_DATA_OUT:
		debug_dump(ctx, "CURLINFO_DATA_OUT", buf, len);
		break;
	case CURLINFO_SSL_DATA_IN:
		wpa_printf(MSG_DEBUG, "debug - CURLINFO_SSL_DATA_IN - %d",
			   (int) len);
		break;
	case CURLINFO_SSL_DATA_OUT:
		wpa_printf(MSG_DEBUG, "debug - CURLINFO_SSL_DATA_OUT - %d",
			   (int) len);
		break;
	case CURLINFO_END:
		wpa_printf(MSG_DEBUG, "debug - CURLINFO_END - %d",
			   (int) len);
		break;
	}
	return 0;
}


static size_t curl_cb_write(void *ptr, size_t size, size_t nmemb,
			    void *userdata)
{
	struct http_ctx *ctx = userdata;
	char *n;
	n = os_realloc(ctx->curl_buf, ctx->curl_buf_len + size * nmemb + 1);
	if (n == NULL)
		return 0;
	ctx->curl_buf = n;
	os_memcpy(n + ctx->curl_buf_len, ptr, size * nmemb);
	n[ctx->curl_buf_len + size * nmemb] = '\0';
	ctx->curl_buf_len += size * nmemb;
	return size * nmemb;
}


#ifdef EAP_TLS_OPENSSL

static void debug_dump_cert(const char *title, X509 *cert)
{
	BIO *out;
	char *txt;
	size_t rlen;

	out = BIO_new(BIO_s_mem());
	if (!out)
		return;

	X509_print_ex(out, cert, XN_FLAG_COMPAT, X509_FLAG_COMPAT);
	rlen = BIO_ctrl_pending(out);
	txt = os_malloc(rlen + 1);
	if (txt) {
		int res = BIO_read(out, txt, rlen);
		if (res > 0) {
			txt[res] = '\0';
			wpa_printf(MSG_MSGDUMP, "%s:\n%s", title, txt);
		}
		os_free(txt);
	}
	BIO_free(out);
}


static void add_alt_name_othername(struct http_ctx *ctx, struct http_cert *cert,
				   OTHERNAME *o)
{
	char txt[100];
	int res;
	struct http_othername *on;
	ASN1_TYPE *val;

	on = os_realloc_array(cert->othername, cert->num_othername + 1,
			      sizeof(struct http_othername));
	if (on == NULL)
		return;
	cert->othername = on;
	on = &on[cert->num_othername];
	os_memset(on, 0, sizeof(*on));

	res = OBJ_obj2txt(txt, sizeof(txt), o->type_id, 1);
	if (res < 0 || res >= (int) sizeof(txt))
		return;

	on->oid = os_strdup(txt);
	if (on->oid == NULL)
		return;

	val = o->value;
	on->data = val->value.octet_string->data;
	on->len = val->value.octet_string->length;

	cert->num_othername++;
}


static void add_alt_name_dns(struct http_ctx *ctx, struct http_cert *cert,
			     ASN1_STRING *name)
{
	char *buf;
	char **n;

	buf = NULL;
	if (ASN1_STRING_to_UTF8((unsigned char **) &buf, name) < 0)
		return;

	n = os_realloc_array(cert->dnsname, cert->num_dnsname + 1,
			     sizeof(char *));
	if (n == NULL)
		return;

	cert->dnsname = n;
	n[cert->num_dnsname] = buf;
	cert->num_dnsname++;
}


static void add_alt_name(struct http_ctx *ctx, struct http_cert *cert,
			 const GENERAL_NAME *name)
{
	switch (name->type) {
	case GEN_OTHERNAME:
		add_alt_name_othername(ctx, cert, name->d.otherName);
		break;
	case GEN_DNS:
		add_alt_name_dns(ctx, cert, name->d.dNSName);
		break;
	}
}


static void add_alt_names(struct http_ctx *ctx, struct http_cert *cert,
			  GENERAL_NAMES *names)
{
	int num, i;

	num = sk_GENERAL_NAME_num(names);
	for (i = 0; i < num; i++) {
		const GENERAL_NAME *name;
		name = sk_GENERAL_NAME_value(names, i);
		add_alt_name(ctx, cert, name);
	}
}


/* RFC 3709 */

typedef struct {
	X509_ALGOR *hashAlg;
	ASN1_OCTET_STRING *hashValue;
} HashAlgAndValue;

typedef struct {
	STACK_OF(HashAlgAndValue) *refStructHash;
	STACK_OF(ASN1_IA5STRING) *refStructURI;
} LogotypeReference;

typedef struct {
	ASN1_IA5STRING *mediaType;
	STACK_OF(HashAlgAndValue) *logotypeHash;
	STACK_OF(ASN1_IA5STRING) *logotypeURI;
} LogotypeDetails;

typedef struct {
	int type;
	union {
		ASN1_INTEGER *numBits;
		ASN1_INTEGER *tableSize;
	} d;
} LogotypeImageResolution;

typedef struct {
	ASN1_INTEGER *type; /* LogotypeImageType ::= INTEGER */
	ASN1_INTEGER *fileSize;
	ASN1_INTEGER *xSize;
	ASN1_INTEGER *ySize;
	LogotypeImageResolution *resolution;
	ASN1_IA5STRING *language;
} LogotypeImageInfo;

typedef struct {
	LogotypeDetails *imageDetails;
	LogotypeImageInfo *imageInfo;
} LogotypeImage;

typedef struct {
	ASN1_INTEGER *fileSize;
	ASN1_INTEGER *playTime;
	ASN1_INTEGER *channels;
	ASN1_INTEGER *sampleRate;
	ASN1_IA5STRING *language;
} LogotypeAudioInfo;

typedef struct {
	LogotypeDetails *audioDetails;
	LogotypeAudioInfo *audioInfo;
} LogotypeAudio;

typedef struct {
	STACK_OF(LogotypeImage) *image;
	STACK_OF(LogotypeAudio) *audio;
} LogotypeData;

typedef struct {
	int type;
	union {
		LogotypeData *direct;
		LogotypeReference *indirect;
	} d;
} LogotypeInfo;

typedef struct {
	ASN1_OBJECT *logotypeType;
	LogotypeInfo *info;
} OtherLogotypeInfo;

typedef struct {
	STACK_OF(LogotypeInfo) *communityLogos;
	LogotypeInfo *issuerLogo;
	LogotypeInfo *subjectLogo;
	STACK_OF(OtherLogotypeInfo) *otherLogos;
} LogotypeExtn;

ASN1_SEQUENCE(HashAlgAndValue) = {
	ASN1_SIMPLE(HashAlgAndValue, hashAlg, X509_ALGOR),
	ASN1_SIMPLE(HashAlgAndValue, hashValue, ASN1_OCTET_STRING)
} ASN1_SEQUENCE_END(HashAlgAndValue);

ASN1_SEQUENCE(LogotypeReference) = {
	ASN1_SEQUENCE_OF(LogotypeReference, refStructHash, HashAlgAndValue),
	ASN1_SEQUENCE_OF(LogotypeReference, refStructURI, ASN1_IA5STRING)
} ASN1_SEQUENCE_END(LogotypeReference);

ASN1_SEQUENCE(LogotypeDetails) = {
	ASN1_SIMPLE(LogotypeDetails, mediaType, ASN1_IA5STRING),
	ASN1_SEQUENCE_OF(LogotypeDetails, logotypeHash, HashAlgAndValue),
	ASN1_SEQUENCE_OF(LogotypeDetails, logotypeURI, ASN1_IA5STRING)
} ASN1_SEQUENCE_END(LogotypeDetails);

ASN1_CHOICE(LogotypeImageResolution) = {
	ASN1_IMP(LogotypeImageResolution, d.numBits, ASN1_INTEGER, 1),
	ASN1_IMP(LogotypeImageResolution, d.tableSize, ASN1_INTEGER, 2)
} ASN1_CHOICE_END(LogotypeImageResolution);

ASN1_SEQUENCE(LogotypeImageInfo) = {
	ASN1_IMP_OPT(LogotypeImageInfo, type, ASN1_INTEGER, 0),
	ASN1_SIMPLE(LogotypeImageInfo, fileSize, ASN1_INTEGER),
	ASN1_SIMPLE(LogotypeImageInfo, xSize, ASN1_INTEGER),
	ASN1_SIMPLE(LogotypeImageInfo, ySize, ASN1_INTEGER),
	ASN1_OPT(LogotypeImageInfo, resolution, LogotypeImageResolution),
	ASN1_IMP_OPT(LogotypeImageInfo, language, ASN1_IA5STRING, 4),
} ASN1_SEQUENCE_END(LogotypeImageInfo);

ASN1_SEQUENCE(LogotypeImage) = {
	ASN1_SIMPLE(LogotypeImage, imageDetails, LogotypeDetails),
	ASN1_OPT(LogotypeImage, imageInfo, LogotypeImageInfo)
} ASN1_SEQUENCE_END(LogotypeImage);

ASN1_SEQUENCE(LogotypeAudioInfo) = {
	ASN1_SIMPLE(LogotypeAudioInfo, fileSize, ASN1_INTEGER),
	ASN1_SIMPLE(LogotypeAudioInfo, playTime, ASN1_INTEGER),
	ASN1_SIMPLE(LogotypeAudioInfo, channels, ASN1_INTEGER),
	ASN1_IMP_OPT(LogotypeAudioInfo, sampleRate, ASN1_INTEGER, 3),
	ASN1_IMP_OPT(LogotypeAudioInfo, language, ASN1_IA5STRING, 4)
} ASN1_SEQUENCE_END(LogotypeAudioInfo);

ASN1_SEQUENCE(LogotypeAudio) = {
	ASN1_SIMPLE(LogotypeAudio, audioDetails, LogotypeDetails),
	ASN1_OPT(LogotypeAudio, audioInfo, LogotypeAudioInfo)
} ASN1_SEQUENCE_END(LogotypeAudio);

ASN1_SEQUENCE(LogotypeData) = {
	ASN1_SEQUENCE_OF_OPT(LogotypeData, image, LogotypeImage),
	ASN1_IMP_SEQUENCE_OF_OPT(LogotypeData, audio, LogotypeAudio, 1)
} ASN1_SEQUENCE_END(LogotypeData);

ASN1_CHOICE(LogotypeInfo) = {
	ASN1_IMP(LogotypeInfo, d.direct, LogotypeData, 0),
	ASN1_IMP(LogotypeInfo, d.indirect, LogotypeReference, 1)
} ASN1_CHOICE_END(LogotypeInfo);

ASN1_SEQUENCE(OtherLogotypeInfo) = {
	ASN1_SIMPLE(OtherLogotypeInfo, logotypeType, ASN1_OBJECT),
	ASN1_SIMPLE(OtherLogotypeInfo, info, LogotypeInfo)
} ASN1_SEQUENCE_END(OtherLogotypeInfo);

ASN1_SEQUENCE(LogotypeExtn) = {
	ASN1_EXP_SEQUENCE_OF_OPT(LogotypeExtn, communityLogos, LogotypeInfo, 0),
	ASN1_EXP_OPT(LogotypeExtn, issuerLogo, LogotypeInfo, 1),
	ASN1_EXP_OPT(LogotypeExtn, issuerLogo, LogotypeInfo, 2),
	ASN1_EXP_SEQUENCE_OF_OPT(LogotypeExtn, otherLogos, OtherLogotypeInfo, 3)
} ASN1_SEQUENCE_END(LogotypeExtn);

IMPLEMENT_ASN1_FUNCTIONS(LogotypeExtn);

#ifdef OPENSSL_IS_BORINGSSL
#define sk_LogotypeInfo_num(st) \
sk_num(CHECKED_CAST(_STACK *, STACK_OF(LogotypeInfo) *, (st)))
#define sk_LogotypeInfo_value(st, i) (LogotypeInfo *) \
sk_value(CHECKED_CAST(_STACK *, const STACK_OF(LogotypeInfo) *, (st)), (i))
#define sk_LogotypeImage_num(st) \
sk_num(CHECKED_CAST(_STACK *, STACK_OF(LogotypeImage) *, (st)))
#define sk_LogotypeImage_value(st, i) (LogotypeImage *) \
sk_value(CHECKED_CAST(_STACK *, const STACK_OF(LogotypeImage) *, (st)), (i))
#define sk_LogotypeAudio_num(st) \
sk_num(CHECKED_CAST(_STACK *, STACK_OF(LogotypeAudio) *, (st)))
#define sk_LogotypeAudio_value(st, i) (LogotypeAudio *) \
sk_value(CHECK_CAST(_STACK *, const STACK_OF(LogotypeAudio) *, (st)), (i))
#define sk_HashAlgAndValue_num(st) \
sk_num(CHECKED_CAST(_STACK *, STACK_OF(HashAlgAndValue) *, (st)))
#define sk_HashAlgAndValue_value(st, i) (HashAlgAndValue *) \
sk_value(CHECKED_CAST(_STACK *, const STACK_OF(HashAlgAndValue) *, (st)), (i))
#define sk_ASN1_IA5STRING_num(st) \
sk_num(CHECKED_CAST(_STACK *, STACK_OF(ASN1_IA5STRING) *, (st)))
#define sk_ASN1_IA5STRING_value(st, i) (ASN1_IA5STRING *) \
sk_value(CHECKED_CAST(_STACK *, const STACK_OF(ASN1_IA5STRING) *, (st)), (i))
#else /* OPENSSL_IS_BORINGSSL */
#define sk_LogotypeInfo_num(st) SKM_sk_num(LogotypeInfo, (st))
#define sk_LogotypeInfo_value(st, i) SKM_sk_value(LogotypeInfo, (st), (i))
#define sk_LogotypeImage_num(st) SKM_sk_num(LogotypeImage, (st))
#define sk_LogotypeImage_value(st, i) SKM_sk_value(LogotypeImage, (st), (i))
#define sk_LogotypeAudio_num(st) SKM_sk_num(LogotypeAudio, (st))
#define sk_LogotypeAudio_value(st, i) SKM_sk_value(LogotypeAudio, (st), (i))
#define sk_HashAlgAndValue_num(st) SKM_sk_num(HashAlgAndValue, (st))
#define sk_HashAlgAndValue_value(st, i) SKM_sk_value(HashAlgAndValue, (st), (i))
#define sk_ASN1_IA5STRING_num(st) SKM_sk_num(ASN1_IA5STRING, (st))
#define sk_ASN1_IA5STRING_value(st, i) SKM_sk_value(ASN1_IA5STRING, (st), (i))
#endif /* OPENSSL_IS_BORINGSSL */


static void add_logo(struct http_ctx *ctx, struct http_cert *hcert,
		     HashAlgAndValue *hash, ASN1_IA5STRING *uri)
{
	char txt[100];
	int res, len;
	struct http_logo *n;

	if (hash == NULL || uri == NULL)
		return;

	res = OBJ_obj2txt(txt, sizeof(txt), hash->hashAlg->algorithm, 1);
	if (res < 0 || res >= (int) sizeof(txt))
		return;

	n = os_realloc_array(hcert->logo, hcert->num_logo + 1,
			     sizeof(struct http_logo));
	if (n == NULL)
		return;
	hcert->logo = n;
	n = &hcert->logo[hcert->num_logo];
	os_memset(n, 0, sizeof(*n));

	n->alg_oid = os_strdup(txt);
	if (n->alg_oid == NULL)
		return;

	n->hash_len = ASN1_STRING_length(hash->hashValue);
	n->hash = os_memdup(ASN1_STRING_data(hash->hashValue), n->hash_len);
	if (n->hash == NULL) {
		os_free(n->alg_oid);
		return;
	}

	len = ASN1_STRING_length(uri);
	n->uri = os_malloc(len + 1);
	if (n->uri == NULL) {
		os_free(n->alg_oid);
		os_free(n->hash);
		return;
	}
	os_memcpy(n->uri, ASN1_STRING_data(uri), len);
	n->uri[len] = '\0';

	hcert->num_logo++;
}


static void add_logo_direct(struct http_ctx *ctx, struct http_cert *hcert,
			    LogotypeData *data)
{
	int i, num;

	if (data->image == NULL)
		return;

	num = sk_LogotypeImage_num(data->image);
	for (i = 0; i < num; i++) {
		LogotypeImage *image;
		LogotypeDetails *details;
		int j, hash_num, uri_num;
		HashAlgAndValue *found_hash = NULL;

		image = sk_LogotypeImage_value(data->image, i);
		if (image == NULL)
			continue;

		details = image->imageDetails;
		if (details == NULL)
			continue;

		hash_num = sk_HashAlgAndValue_num(details->logotypeHash);
		for (j = 0; j < hash_num; j++) {
			HashAlgAndValue *hash;
			char txt[100];
			int res;
			hash = sk_HashAlgAndValue_value(details->logotypeHash,
							j);
			if (hash == NULL)
				continue;
			res = OBJ_obj2txt(txt, sizeof(txt),
					  hash->hashAlg->algorithm, 1);
			if (res < 0 || res >= (int) sizeof(txt))
				continue;
			if (os_strcmp(txt, "2.16.840.1.101.3.4.2.1") == 0) {
				found_hash = hash;
				break;
			}
		}

		if (!found_hash) {
			wpa_printf(MSG_DEBUG, "OpenSSL: No SHA256 hash found for the logo");
			continue;
		}

		uri_num = sk_ASN1_IA5STRING_num(details->logotypeURI);
		for (j = 0; j < uri_num; j++) {
			ASN1_IA5STRING *uri;
			uri = sk_ASN1_IA5STRING_value(details->logotypeURI, j);
			add_logo(ctx, hcert, found_hash, uri);
		}
	}
}


static void add_logo_indirect(struct http_ctx *ctx, struct http_cert *hcert,
			      LogotypeReference *ref)
{
	int j, hash_num, uri_num;

	hash_num = sk_HashAlgAndValue_num(ref->refStructHash);
	uri_num = sk_ASN1_IA5STRING_num(ref->refStructURI);
	if (hash_num != uri_num) {
		wpa_printf(MSG_INFO, "Unexpected LogotypeReference array size difference %d != %d",
			   hash_num, uri_num);
		return;
	}

	for (j = 0; j < hash_num; j++) {
		HashAlgAndValue *hash;
		ASN1_IA5STRING *uri;
		hash = sk_HashAlgAndValue_value(ref->refStructHash, j);
		uri = sk_ASN1_IA5STRING_value(ref->refStructURI, j);
		add_logo(ctx, hcert, hash, uri);
	}
}


static void i2r_HashAlgAndValue(HashAlgAndValue *hash, BIO *out, int indent)
{
	int i;
	const unsigned char *data;

	BIO_printf(out, "%*shashAlg: ", indent, "");
	i2a_ASN1_OBJECT(out, hash->hashAlg->algorithm);
	BIO_printf(out, "\n");

	BIO_printf(out, "%*shashValue: ", indent, "");
	data = hash->hashValue->data;
	for (i = 0; i < hash->hashValue->length; i++)
		BIO_printf(out, "%s%02x", i > 0 ? ":" : "", data[i]);
	BIO_printf(out, "\n");
}

static void i2r_LogotypeDetails(LogotypeDetails *details, BIO *out, int indent)
{
	int i, num;

	BIO_printf(out, "%*sLogotypeDetails\n", indent, "");
	if (details->mediaType) {
		BIO_printf(out, "%*smediaType: ", indent, "");
		ASN1_STRING_print(out, details->mediaType);
		BIO_printf(out, "\n");
	}

	num = details->logotypeHash ?
		sk_HashAlgAndValue_num(details->logotypeHash) : 0;
	for (i = 0; i < num; i++) {
		HashAlgAndValue *hash;
		hash = sk_HashAlgAndValue_value(details->logotypeHash, i);
		i2r_HashAlgAndValue(hash, out, indent);
	}

	num = details->logotypeURI ?
		sk_ASN1_IA5STRING_num(details->logotypeURI) : 0;
	for (i = 0; i < num; i++) {
		ASN1_IA5STRING *uri;
		uri = sk_ASN1_IA5STRING_value(details->logotypeURI, i);
		BIO_printf(out, "%*slogotypeURI: ", indent, "");
		ASN1_STRING_print(out, uri);
		BIO_printf(out, "\n");
	}
}

static void i2r_LogotypeImageInfo(LogotypeImageInfo *info, BIO *out, int indent)
{
	long val;

	BIO_printf(out, "%*sLogotypeImageInfo\n", indent, "");
	if (info->type) {
		val = ASN1_INTEGER_get(info->type);
		BIO_printf(out, "%*stype: %ld\n", indent, "", val);
	} else {
		BIO_printf(out, "%*stype: default (1)\n", indent, "");
	}
	val = ASN1_INTEGER_get(info->fileSize);
	BIO_printf(out, "%*sfileSize: %ld\n", indent, "", val);
	val = ASN1_INTEGER_get(info->xSize);
	BIO_printf(out, "%*sxSize: %ld\n", indent, "", val);
	val = ASN1_INTEGER_get(info->ySize);
	BIO_printf(out, "%*sySize: %ld\n", indent, "", val);
	if (info->resolution) {
		BIO_printf(out, "%*sresolution [%d]\n", indent, "",
			   info->resolution->type);
		switch (info->resolution->type) {
		case 0:
			val = ASN1_INTEGER_get(info->resolution->d.numBits);
			BIO_printf(out, "%*snumBits: %ld\n", indent, "", val);
			break;
		case 1:
			val = ASN1_INTEGER_get(info->resolution->d.tableSize);
			BIO_printf(out, "%*stableSize: %ld\n", indent, "", val);
			break;
		}
	}
	if (info->language) {
		BIO_printf(out, "%*slanguage: ", indent, "");
		ASN1_STRING_print(out, info->language);
		BIO_printf(out, "\n");
	}
}

static void i2r_LogotypeImage(LogotypeImage *image, BIO *out, int indent)
{
	BIO_printf(out, "%*sLogotypeImage\n", indent, "");
	if (image->imageDetails) {
		i2r_LogotypeDetails(image->imageDetails, out, indent + 4);
	}
	if (image->imageInfo) {
		i2r_LogotypeImageInfo(image->imageInfo, out, indent + 4);
	}
}

static void i2r_LogotypeData(LogotypeData *data, const char *title, BIO *out,
			     int indent)
{
	int i, num;

	BIO_printf(out, "%*s%s - LogotypeData\n", indent, "", title);

	num = data->image ? sk_LogotypeImage_num(data->image) : 0;
	for (i = 0; i < num; i++) {
		LogotypeImage *image = sk_LogotypeImage_value(data->image, i);
		i2r_LogotypeImage(image, out, indent + 4);
	}

	num = data->audio ? sk_LogotypeAudio_num(data->audio) : 0;
	for (i = 0; i < num; i++) {
		BIO_printf(out, "%*saudio: TODO\n", indent, "");
	}
}

static void i2r_LogotypeReference(LogotypeReference *ref, const char *title,
				  BIO *out, int indent)
{
	int i, hash_num, uri_num;

	BIO_printf(out, "%*s%s - LogotypeReference\n", indent, "", title);

	hash_num = ref->refStructHash ?
		sk_HashAlgAndValue_num(ref->refStructHash) : 0;
	uri_num = ref->refStructURI ?
		sk_ASN1_IA5STRING_num(ref->refStructURI) : 0;
	if (hash_num != uri_num) {
		BIO_printf(out, "%*sUnexpected LogotypeReference array size difference %d != %d\n",
			   indent, "", hash_num, uri_num);
		return;
	}

	for (i = 0; i < hash_num; i++) {
		HashAlgAndValue *hash;
		ASN1_IA5STRING *uri;

		hash = sk_HashAlgAndValue_value(ref->refStructHash, i);
		i2r_HashAlgAndValue(hash, out, indent);

		uri = sk_ASN1_IA5STRING_value(ref->refStructURI, i);
		BIO_printf(out, "%*srefStructURI: ", indent, "");
		ASN1_STRING_print(out, uri);
		BIO_printf(out, "\n");
	}
}

static void i2r_LogotypeInfo(LogotypeInfo *info, const char *title, BIO *out,
			     int indent)
{
	switch (info->type) {
	case 0:
		i2r_LogotypeData(info->d.direct, title, out, indent);
		break;
	case 1:
		i2r_LogotypeReference(info->d.indirect, title, out, indent);
		break;
	}
}

static void debug_print_logotypeext(LogotypeExtn *logo)
{
	BIO *out;
	int i, num;
	int indent = 0;

	out = BIO_new_fp(stdout, BIO_NOCLOSE);
	if (out == NULL)
		return;

	if (logo->communityLogos) {
		num = sk_LogotypeInfo_num(logo->communityLogos);
		for (i = 0; i < num; i++) {
			LogotypeInfo *info;
			info = sk_LogotypeInfo_value(logo->communityLogos, i);
			i2r_LogotypeInfo(info, "communityLogo", out, indent);
		}
	}

	if (logo->issuerLogo) {
		i2r_LogotypeInfo(logo->issuerLogo, "issuerLogo", out, indent );
	}

	if (logo->subjectLogo) {
		i2r_LogotypeInfo(logo->subjectLogo, "subjectLogo", out, indent);
	}

	if (logo->otherLogos) {
		BIO_printf(out, "%*sotherLogos - TODO\n", indent, "");
	}

	BIO_free(out);
}


static void add_logotype_ext(struct http_ctx *ctx, struct http_cert *hcert,
			     X509 *cert)
{
	ASN1_OBJECT *obj;
	int pos;
	X509_EXTENSION *ext;
	ASN1_OCTET_STRING *os;
	LogotypeExtn *logo;
	const unsigned char *data;
	int i, num;

	obj = OBJ_txt2obj("1.3.6.1.5.5.7.1.12", 0);
	if (obj == NULL)
		return;

	pos = X509_get_ext_by_OBJ(cert, obj, -1);
	if (pos < 0) {
		wpa_printf(MSG_INFO, "No logotype extension included");
		return;
	}

	wpa_printf(MSG_INFO, "Parsing logotype extension");
	ext = X509_get_ext(cert, pos);
	if (!ext) {
		wpa_printf(MSG_INFO, "Could not get logotype extension");
		return;
	}

	os = X509_EXTENSION_get_data(ext);
	if (os == NULL) {
		wpa_printf(MSG_INFO, "Could not get logotype extension data");
		return;
	}

	wpa_hexdump(MSG_DEBUG, "logotypeExtn",
		    ASN1_STRING_data(os), ASN1_STRING_length(os));

	data = ASN1_STRING_data(os);
	logo = d2i_LogotypeExtn(NULL, &data, ASN1_STRING_length(os));
	if (logo == NULL) {
		wpa_printf(MSG_INFO, "Failed to parse logotypeExtn");
		return;
	}

	if (wpa_debug_level < MSG_INFO)
		debug_print_logotypeext(logo);

	if (!logo->communityLogos) {
		wpa_printf(MSG_INFO, "No communityLogos included");
		LogotypeExtn_free(logo);
		return;
	}

	num = sk_LogotypeInfo_num(logo->communityLogos);
	for (i = 0; i < num; i++) {
		LogotypeInfo *info;
		info = sk_LogotypeInfo_value(logo->communityLogos, i);
		switch (info->type) {
		case 0:
			add_logo_direct(ctx, hcert, info->d.direct);
			break;
		case 1:
			add_logo_indirect(ctx, hcert, info->d.indirect);
			break;
		}
	}

	LogotypeExtn_free(logo);
}


static void parse_cert(struct http_ctx *ctx, struct http_cert *hcert,
		       X509 *cert, GENERAL_NAMES **names)
{
	os_memset(hcert, 0, sizeof(*hcert));

	*names = X509_get_ext_d2i(cert, NID_subject_alt_name, NULL, NULL);
	if (*names)
		add_alt_names(ctx, hcert, *names);

	add_logotype_ext(ctx, hcert, cert);
}


static void parse_cert_free(struct http_cert *hcert, GENERAL_NAMES *names)
{
	unsigned int i;

	for (i = 0; i < hcert->num_dnsname; i++)
		OPENSSL_free(hcert->dnsname[i]);
	os_free(hcert->dnsname);

	for (i = 0; i < hcert->num_othername; i++)
		os_free(hcert->othername[i].oid);
	os_free(hcert->othername);

	for (i = 0; i < hcert->num_logo; i++) {
		os_free(hcert->logo[i].alg_oid);
		os_free(hcert->logo[i].hash);
		os_free(hcert->logo[i].uri);
	}
	os_free(hcert->logo);

	sk_GENERAL_NAME_pop_free(names, GENERAL_NAME_free);
}


static int validate_server_cert(struct http_ctx *ctx, X509 *cert)
{
	GENERAL_NAMES *names;
	struct http_cert hcert;
	int ret;

	if (ctx->cert_cb == NULL) {
		wpa_printf(MSG_DEBUG, "%s: no cert_cb configured", __func__);
		return 0;
	}

	if (0) {
		BIO *out;
		out = BIO_new_fp(stdout, BIO_NOCLOSE);
		X509_print_ex(out, cert, XN_FLAG_COMPAT, X509_FLAG_COMPAT);
		BIO_free(out);
	}

	parse_cert(ctx, &hcert, cert, &names);
	ret = ctx->cert_cb(ctx->cert_cb_ctx, &hcert);
	parse_cert_free(&hcert, names);

	return ret;
}


void http_parse_x509_certificate(struct http_ctx *ctx, const char *fname)
{
	BIO *in, *out;
	X509 *cert;
	GENERAL_NAMES *names;
	struct http_cert hcert;
	unsigned int i;

	in = BIO_new_file(fname, "r");
	if (in == NULL) {
		wpa_printf(MSG_ERROR, "Could not read '%s'", fname);
		return;
	}

	cert = d2i_X509_bio(in, NULL);
	BIO_free(in);

	if (cert == NULL) {
		wpa_printf(MSG_ERROR, "Could not parse certificate");
		return;
	}

	out = BIO_new_fp(stdout, BIO_NOCLOSE);
	if (out) {
		X509_print_ex(out, cert, XN_FLAG_COMPAT,
			      X509_FLAG_COMPAT);
		BIO_free(out);
	}

	wpa_printf(MSG_INFO, "Additional parsing information:");
	parse_cert(ctx, &hcert, cert, &names);
	for (i = 0; i < hcert.num_othername; i++) {
		if (os_strcmp(hcert.othername[i].oid,
			      "1.3.6.1.4.1.40808.1.1.1") == 0) {
			char *name = os_zalloc(hcert.othername[i].len + 1);
			if (name) {
				os_memcpy(name, hcert.othername[i].data,
					  hcert.othername[i].len);
				wpa_printf(MSG_INFO,
					   "id-wfa-hotspot-friendlyName: %s",
					   name);
				os_free(name);
			}
			wpa_hexdump_ascii(MSG_INFO,
					  "id-wfa-hotspot-friendlyName",
					  hcert.othername[i].data,
					  hcert.othername[i].len);
		} else {
			wpa_printf(MSG_INFO, "subjAltName[othername]: oid=%s",
				   hcert.othername[i].oid);
			wpa_hexdump_ascii(MSG_INFO, "unknown othername",
					  hcert.othername[i].data,
					  hcert.othername[i].len);
		}
	}
	parse_cert_free(&hcert, names);

	X509_free(cert);
}


static int curl_cb_ssl_verify(int preverify_ok, X509_STORE_CTX *x509_ctx)
{
	struct http_ctx *ctx;
	X509 *cert;
	int err, depth;
	char buf[256];
	X509_NAME *name;
	const char *err_str;
	SSL *ssl;
	SSL_CTX *ssl_ctx;

	ssl = X509_STORE_CTX_get_ex_data(x509_ctx,
					 SSL_get_ex_data_X509_STORE_CTX_idx());
	ssl_ctx = SSL_get_SSL_CTX(ssl);
	ctx = SSL_CTX_get_app_data(ssl_ctx);

	wpa_printf(MSG_DEBUG, "curl_cb_ssl_verify, preverify_ok: %d",
		   preverify_ok);

	err = X509_STORE_CTX_get_error(x509_ctx);
	err_str = X509_verify_cert_error_string(err);
	depth = X509_STORE_CTX_get_error_depth(x509_ctx);
	cert = X509_STORE_CTX_get_current_cert(x509_ctx);
	if (!cert) {
		wpa_printf(MSG_INFO, "No server certificate available");
		ctx->last_err = "No server certificate available";
		return 0;
	}

	if (depth == 0)
		ctx->peer_cert = cert;
	else if (depth == 1)
		ctx->peer_issuer = cert;
	else if (depth == 2)
		ctx->peer_issuer_issuer = cert;

	name = X509_get_subject_name(cert);
	X509_NAME_oneline(name, buf, sizeof(buf));
	wpa_printf(MSG_INFO, "Server certificate chain - depth=%d err=%d (%s) subject=%s",
		   depth, err, err_str, buf);
	debug_dump_cert("Server certificate chain - certificate", cert);

	if (depth == 0 && preverify_ok && validate_server_cert(ctx, cert) < 0)
		return 0;

#ifdef OPENSSL_IS_BORINGSSL
	if (depth == 0 && ctx->ocsp != NO_OCSP && preverify_ok) {
		enum ocsp_result res;

		res = check_ocsp_resp(ssl_ctx, ssl, cert, ctx->peer_issuer,
				      ctx->peer_issuer_issuer);
		if (res == OCSP_REVOKED) {
			preverify_ok = 0;
			wpa_printf(MSG_INFO, "OCSP: certificate revoked");
			if (err == X509_V_OK)
				X509_STORE_CTX_set_error(
					x509_ctx, X509_V_ERR_CERT_REVOKED);
		} else if (res != OCSP_GOOD && (ctx->ocsp == MANDATORY_OCSP)) {
			preverify_ok = 0;
			wpa_printf(MSG_INFO,
				   "OCSP: bad certificate status response");
		}
	}
#endif /* OPENSSL_IS_BORINGSSL */

	if (!preverify_ok)
		ctx->last_err = "TLS validation failed";

	return preverify_ok;
}


#ifdef HAVE_OCSP

static void ocsp_debug_print_resp(OCSP_RESPONSE *rsp)
{
	BIO *out;
	size_t rlen;
	char *txt;
	int res;

	out = BIO_new(BIO_s_mem());
	if (!out)
		return;

	OCSP_RESPONSE_print(out, rsp, 0);
	rlen = BIO_ctrl_pending(out);
	txt = os_malloc(rlen + 1);
	if (!txt) {
		BIO_free(out);
		return;
	}

	res = BIO_read(out, txt, rlen);
	if (res > 0) {
		txt[res] = '\0';
		wpa_printf(MSG_MSGDUMP, "OpenSSL: OCSP Response\n%s", txt);
	}
	os_free(txt);
	BIO_free(out);
}


static void tls_show_errors(const char *func, const char *txt)
{
	unsigned long err;

	wpa_printf(MSG_DEBUG, "OpenSSL: %s - %s %s",
		   func, txt, ERR_error_string(ERR_get_error(), NULL));

	while ((err = ERR_get_error())) {
		wpa_printf(MSG_DEBUG, "OpenSSL: pending error: %s",
			   ERR_error_string(err, NULL));
	}
}


static int ocsp_resp_cb(SSL *s, void *arg)
{
	struct http_ctx *ctx = arg;
	const unsigned char *p;
	int len, status, reason, res;
	OCSP_RESPONSE *rsp;
	OCSP_BASICRESP *basic;
	OCSP_CERTID *id;
	ASN1_GENERALIZEDTIME *produced_at, *this_update, *next_update;
	X509_STORE *store;
	STACK_OF(X509) *certs = NULL;

	len = SSL_get_tlsext_status_ocsp_resp(s, &p);
	if (!p) {
		wpa_printf(MSG_DEBUG, "OpenSSL: No OCSP response received");
		if (ctx->ocsp == MANDATORY_OCSP)
			ctx->last_err = "No OCSP response received";
		return (ctx->ocsp == MANDATORY_OCSP) ? 0 : 1;
	}

	wpa_hexdump(MSG_DEBUG, "OpenSSL: OCSP response", p, len);

	rsp = d2i_OCSP_RESPONSE(NULL, &p, len);
	if (!rsp) {
		wpa_printf(MSG_INFO, "OpenSSL: Failed to parse OCSP response");
		ctx->last_err = "Failed to parse OCSP response";
		return 0;
	}

	ocsp_debug_print_resp(rsp);

	status = OCSP_response_status(rsp);
	if (status != OCSP_RESPONSE_STATUS_SUCCESSFUL) {
		wpa_printf(MSG_INFO, "OpenSSL: OCSP responder error %d (%s)",
			   status, OCSP_response_status_str(status));
		ctx->last_err = "OCSP responder error";
		return 0;
	}

	basic = OCSP_response_get1_basic(rsp);
	if (!basic) {
		wpa_printf(MSG_INFO, "OpenSSL: Could not find BasicOCSPResponse");
		ctx->last_err = "Could not find BasicOCSPResponse";
		return 0;
	}

	store = SSL_CTX_get_cert_store(s->ctx);
	if (ctx->peer_issuer) {
		wpa_printf(MSG_DEBUG, "OpenSSL: Add issuer");
		debug_dump_cert("OpenSSL: Issuer certificate",
				ctx->peer_issuer);

		if (X509_STORE_add_cert(store, ctx->peer_issuer) != 1) {
			tls_show_errors(__func__,
					"OpenSSL: Could not add issuer to certificate store");
		}
		certs = sk_X509_new_null();
		if (certs) {
			X509 *cert;
			cert = X509_dup(ctx->peer_issuer);
			if (cert && !sk_X509_push(certs, cert)) {
				tls_show_errors(
					__func__,
					"OpenSSL: Could not add issuer to OCSP responder trust store");
				X509_free(cert);
				sk_X509_free(certs);
				certs = NULL;
			}
			if (certs && ctx->peer_issuer_issuer) {
				cert = X509_dup(ctx->peer_issuer_issuer);
				if (cert && !sk_X509_push(certs, cert)) {
					tls_show_errors(
						__func__,
						"OpenSSL: Could not add issuer's issuer to OCSP responder trust store");
					X509_free(cert);
				}
			}
		}
	}

	status = OCSP_basic_verify(basic, certs, store, OCSP_TRUSTOTHER);
	sk_X509_pop_free(certs, X509_free);
	if (status <= 0) {
		tls_show_errors(__func__,
				"OpenSSL: OCSP response failed verification");
		OCSP_BASICRESP_free(basic);
		OCSP_RESPONSE_free(rsp);
		ctx->last_err = "OCSP response failed verification";
		return 0;
	}

	wpa_printf(MSG_DEBUG, "OpenSSL: OCSP response verification succeeded");

	if (!ctx->peer_cert) {
		wpa_printf(MSG_DEBUG, "OpenSSL: Peer certificate not available for OCSP status check");
		OCSP_BASICRESP_free(basic);
		OCSP_RESPONSE_free(rsp);
		ctx->last_err = "Peer certificate not available for OCSP status check";
		return 0;
	}

	if (!ctx->peer_issuer) {
		wpa_printf(MSG_DEBUG, "OpenSSL: Peer issuer certificate not available for OCSP status check");
		OCSP_BASICRESP_free(basic);
		OCSP_RESPONSE_free(rsp);
		ctx->last_err = "Peer issuer certificate not available for OCSP status check";
		return 0;
	}

	id = OCSP_cert_to_id(EVP_sha256(), ctx->peer_cert, ctx->peer_issuer);
	if (!id) {
		wpa_printf(MSG_DEBUG,
			   "OpenSSL: Could not create OCSP certificate identifier (SHA256)");
		OCSP_BASICRESP_free(basic);
		OCSP_RESPONSE_free(rsp);
		ctx->last_err = "Could not create OCSP certificate identifier";
		return 0;
	}

	res = OCSP_resp_find_status(basic, id, &status, &reason, &produced_at,
				    &this_update, &next_update);
	if (!res) {
		id = OCSP_cert_to_id(NULL, ctx->peer_cert, ctx->peer_issuer);
		if (!id) {
			wpa_printf(MSG_DEBUG,
				   "OpenSSL: Could not create OCSP certificate identifier (SHA1)");
			OCSP_BASICRESP_free(basic);
			OCSP_RESPONSE_free(rsp);
			ctx->last_err =
				"Could not create OCSP certificate identifier";
			return 0;
		}

		res = OCSP_resp_find_status(basic, id, &status, &reason,
					    &produced_at, &this_update,
					    &next_update);
	}

	if (!res) {
		wpa_printf(MSG_INFO, "OpenSSL: Could not find current server certificate from OCSP response%s",
			   (ctx->ocsp == MANDATORY_OCSP) ? "" :
			   " (OCSP not required)");
		OCSP_CERTID_free(id);
		OCSP_BASICRESP_free(basic);
		OCSP_RESPONSE_free(rsp);
		if (ctx->ocsp == MANDATORY_OCSP)

			ctx->last_err = "Could not find current server certificate from OCSP response";
		return (ctx->ocsp == MANDATORY_OCSP) ? 0 : 1;
	}
	OCSP_CERTID_free(id);

	if (!OCSP_check_validity(this_update, next_update, 5 * 60, -1)) {
		tls_show_errors(__func__, "OpenSSL: OCSP status times invalid");
		OCSP_BASICRESP_free(basic);
		OCSP_RESPONSE_free(rsp);
		ctx->last_err = "OCSP status times invalid";
		return 0;
	}

	OCSP_BASICRESP_free(basic);
	OCSP_RESPONSE_free(rsp);

	wpa_printf(MSG_DEBUG, "OpenSSL: OCSP status for server certificate: %s",
		   OCSP_cert_status_str(status));

	if (status == V_OCSP_CERTSTATUS_GOOD)
		return 1;
	if (status == V_OCSP_CERTSTATUS_REVOKED) {
		ctx->last_err = "Server certificate has been revoked";
		return 0;
	}
	if (ctx->ocsp == MANDATORY_OCSP) {
		wpa_printf(MSG_DEBUG, "OpenSSL: OCSP status unknown, but OCSP required");
		ctx->last_err = "OCSP status unknown";
		return 0;
	}
	wpa_printf(MSG_DEBUG, "OpenSSL: OCSP status unknown, but OCSP was not required, so allow connection to continue");
	return 1;
}


static SSL_METHOD patch_ssl_method;
static const SSL_METHOD *real_ssl_method;

static int curl_patch_ssl_new(SSL *s)
{
	SSL_CTX *ssl = s->ctx;
	int ret;

	ssl->method = real_ssl_method;
	s->method = real_ssl_method;

	ret = s->method->ssl_new(s);
	SSL_set_tlsext_status_type(s, TLSEXT_STATUSTYPE_ocsp);

	return ret;
}

#endif /* HAVE_OCSP */


static CURLcode curl_cb_ssl(CURL *curl, void *sslctx, void *parm)
{
	struct http_ctx *ctx = parm;
	SSL_CTX *ssl = sslctx;

	wpa_printf(MSG_DEBUG, "curl_cb_ssl");
	SSL_CTX_set_app_data(ssl, ctx);
	SSL_CTX_set_verify(ssl, SSL_VERIFY_PEER, curl_cb_ssl_verify);

#ifdef HAVE_OCSP
	if (ctx->ocsp != NO_OCSP) {
		SSL_CTX_set_tlsext_status_cb(ssl, ocsp_resp_cb);
		SSL_CTX_set_tlsext_status_arg(ssl, ctx);

		/*
		 * Use a temporary SSL_METHOD to get a callback on SSL_new()
		 * from libcurl since there is no proper callback registration
		 * available for this.
		 */
		os_memset(&patch_ssl_method, 0, sizeof(patch_ssl_method));
		patch_ssl_method.ssl_new = curl_patch_ssl_new;
		real_ssl_method = ssl->method;
		ssl->method = &patch_ssl_method;
	}
#endif /* HAVE_OCSP */

	return CURLE_OK;
}

#endif /* EAP_TLS_OPENSSL */


static CURL * setup_curl_post(struct http_ctx *ctx, const char *address,
			      const char *ca_fname, const char *username,
			      const char *password, const char *client_cert,
			      const char *client_key)
{
	CURL *curl;
#ifdef EAP_TLS_OPENSSL
	const char *extra = " tls=openssl";
#else /* EAP_TLS_OPENSSL */
	const char *extra = "";
#endif /* EAP_TLS_OPENSSL */

	wpa_printf(MSG_DEBUG, "Start HTTP client: address=%s ca_fname=%s "
		   "username=%s%s", address, ca_fname, username, extra);

	curl = curl_easy_init();
	if (curl == NULL)
		return NULL;

	curl_easy_setopt(curl, CURLOPT_URL, address);
	curl_easy_setopt(curl, CURLOPT_POST, 1L);
	if (ca_fname) {
		curl_easy_setopt(curl, CURLOPT_CAINFO, ca_fname);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
#ifdef EAP_TLS_OPENSSL
		curl_easy_setopt(curl, CURLOPT_SSL_CTX_FUNCTION, curl_cb_ssl);
		curl_easy_setopt(curl, CURLOPT_SSL_CTX_DATA, ctx);
#ifdef OPENSSL_IS_BORINGSSL
		/* For now, using the CURLOPT_SSL_VERIFYSTATUS option only
		 * with BoringSSL since the OpenSSL specific callback hack to
		 * enable OCSP is not available with BoringSSL. The OCSP
		 * implementation within libcurl is not sufficient for the
		 * Hotspot 2.0 OSU needs, so cannot use this with OpenSSL.
		 */
		if (ctx->ocsp != NO_OCSP)
			curl_easy_setopt(curl, CURLOPT_SSL_VERIFYSTATUS, 1L);
#endif /* OPENSSL_IS_BORINGSSL */
#endif /* EAP_TLS_OPENSSL */
	} else {
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	}
	if (client_cert && client_key) {
		curl_easy_setopt(curl, CURLOPT_SSLCERT, client_cert);
		curl_easy_setopt(curl, CURLOPT_SSLKEY, client_key);
	}
	/* TODO: use curl_easy_getinfo() with CURLINFO_CERTINFO to fetch
	 * information about the server certificate */
	curl_easy_setopt(curl, CURLOPT_CERTINFO, 1L);
	curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, curl_cb_debug);
	curl_easy_setopt(curl, CURLOPT_DEBUGDATA, ctx);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_cb_write);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, ctx);
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
	if (username) {
		curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_ANYSAFE);
		curl_easy_setopt(curl, CURLOPT_USERNAME, username);
		curl_easy_setopt(curl, CURLOPT_PASSWORD, password);
	}

	return curl;
}


static int post_init_client(struct http_ctx *ctx, const char *address,
			    const char *ca_fname, const char *username,
			    const char *password, const char *client_cert,
			    const char *client_key)
{
	char *pos;
	int count;

	clone_str(&ctx->svc_address, address);
	clone_str(&ctx->svc_ca_fname, ca_fname);
	clone_str(&ctx->svc_username, username);
	clone_str(&ctx->svc_password, password);
	clone_str(&ctx->svc_client_cert, client_cert);
	clone_str(&ctx->svc_client_key, client_key);

	/*
	 * Workaround for Apache "Hostname 'FOO' provided via SNI and hostname
	 * 'foo' provided via HTTP are different.
	 */
	for (count = 0, pos = ctx->svc_address; count < 3 && pos && *pos;
	     pos++) {
		if (*pos == '/')
			count++;
		*pos = tolower(*pos);
	}

	ctx->curl = setup_curl_post(ctx, ctx->svc_address, ca_fname, username,
				    password, client_cert, client_key);
	if (ctx->curl == NULL)
		return -1;

	return 0;
}


int soap_init_client(struct http_ctx *ctx, const char *address,
		     const char *ca_fname, const char *username,
		     const char *password, const char *client_cert,
		     const char *client_key)
{
	if (post_init_client(ctx, address, ca_fname, username, password,
			     client_cert, client_key) < 0)
		return -1;

	ctx->curl_hdr = curl_slist_append(ctx->curl_hdr,
					  "Content-Type: application/soap+xml");
	ctx->curl_hdr = curl_slist_append(ctx->curl_hdr, "SOAPAction: ");
	ctx->curl_hdr = curl_slist_append(ctx->curl_hdr, "Expect:");
	curl_easy_setopt(ctx->curl, CURLOPT_HTTPHEADER, ctx->curl_hdr);

	return 0;
}


int soap_reinit_client(struct http_ctx *ctx)
{
	char *address = NULL;
	char *ca_fname = NULL;
	char *username = NULL;
	char *password = NULL;
	char *client_cert = NULL;
	char *client_key = NULL;
	int ret;

	clear_curl(ctx);

	clone_str(&address, ctx->svc_address);
	clone_str(&ca_fname, ctx->svc_ca_fname);
	clone_str(&username, ctx->svc_username);
	clone_str(&password, ctx->svc_password);
	clone_str(&client_cert, ctx->svc_client_cert);
	clone_str(&client_key, ctx->svc_client_key);

	ret = soap_init_client(ctx, address, ca_fname, username, password,
			       client_cert, client_key);
	os_free(address);
	os_free(ca_fname);
	str_clear_free(username);
	str_clear_free(password);
	os_free(client_cert);
	os_free(client_key);
	return ret;
}


static void free_curl_buf(struct http_ctx *ctx)
{
	os_free(ctx->curl_buf);
	ctx->curl_buf = NULL;
	ctx->curl_buf_len = 0;
}


xml_node_t * soap_send_receive(struct http_ctx *ctx, xml_node_t *node)
{
	char *str;
	xml_node_t *envelope, *ret, *resp, *n;
	CURLcode res;
	long http = 0;

	ctx->last_err = NULL;

	wpa_printf(MSG_DEBUG, "SOAP: Sending message");
	envelope = soap_build_envelope(ctx->xml, node);
	str = xml_node_to_str(ctx->xml, envelope);
	xml_node_free(ctx->xml, envelope);
	wpa_printf(MSG_MSGDUMP, "SOAP[%s]", str);

	curl_easy_setopt(ctx->curl, CURLOPT_POSTFIELDS, str);
	free_curl_buf(ctx);

	res = curl_easy_perform(ctx->curl);
	if (res != CURLE_OK) {
		if (!ctx->last_err)
			ctx->last_err = curl_easy_strerror(res);
		wpa_printf(MSG_ERROR, "curl_easy_perform() failed: %s",
			   ctx->last_err);
		os_free(str);
		free_curl_buf(ctx);
		return NULL;
	}
	os_free(str);

	curl_easy_getinfo(ctx->curl, CURLINFO_RESPONSE_CODE, &http);
	wpa_printf(MSG_DEBUG, "SOAP: Server response code %ld", http);
	if (http != 200) {
		ctx->last_err = "HTTP download failed";
		wpa_printf(MSG_INFO, "HTTP download failed - code %ld", http);
		free_curl_buf(ctx);
		return NULL;
	}

	if (ctx->curl_buf == NULL)
		return NULL;

	wpa_printf(MSG_MSGDUMP, "Server response:\n%s", ctx->curl_buf);
	resp = xml_node_from_buf(ctx->xml, ctx->curl_buf);
	free_curl_buf(ctx);
	if (resp == NULL) {
		wpa_printf(MSG_INFO, "Could not parse SOAP response");
		ctx->last_err = "Could not parse SOAP response";
		return NULL;
	}

	ret = soap_get_body(ctx->xml, resp);
	if (ret == NULL) {
		wpa_printf(MSG_INFO, "Could not get SOAP body");
		ctx->last_err = "Could not get SOAP body";
		return NULL;
	}

	wpa_printf(MSG_DEBUG, "SOAP body localname: '%s'",
		   xml_node_get_localname(ctx->xml, ret));
	n = xml_node_copy(ctx->xml, ret);
	xml_node_free(ctx->xml, resp);

	return n;
}


struct http_ctx * http_init_ctx(void *upper_ctx, struct xml_node_ctx *xml_ctx)
{
	struct http_ctx *ctx;

	ctx = os_zalloc(sizeof(*ctx));
	if (ctx == NULL)
		return NULL;
	ctx->ctx = upper_ctx;
	ctx->xml = xml_ctx;
	ctx->ocsp = OPTIONAL_OCSP;

	curl_global_init(CURL_GLOBAL_ALL);

	return ctx;
}


void http_ocsp_set(struct http_ctx *ctx, int val)
{
	if (val == 0)
		ctx->ocsp = NO_OCSP;
	else if (val == 1)
		ctx->ocsp = OPTIONAL_OCSP;
	if (val == 2)
		ctx->ocsp = MANDATORY_OCSP;
}


void http_deinit_ctx(struct http_ctx *ctx)
{
	clear_curl(ctx);
	os_free(ctx->curl_buf);
	curl_global_cleanup();

	os_free(ctx->svc_address);
	os_free(ctx->svc_ca_fname);
	str_clear_free(ctx->svc_username);
	str_clear_free(ctx->svc_password);
	os_free(ctx->svc_client_cert);
	os_free(ctx->svc_client_key);

	os_free(ctx);
}


int http_download_file(struct http_ctx *ctx, const char *url,
		       const char *fname, const char *ca_fname)
{
	CURL *curl;
	FILE *f;
	CURLcode res;
	long http = 0;

	ctx->last_err = NULL;

	wpa_printf(MSG_DEBUG, "curl: Download file from %s to %s (ca=%s)",
		   url, fname, ca_fname);
	curl = curl_easy_init();
	if (curl == NULL)
		return -1;

	f = fopen(fname, "wb");
	if (f == NULL) {
		curl_easy_cleanup(curl);
		return -1;
	}

	curl_easy_setopt(curl, CURLOPT_URL, url);
	if (ca_fname) {
		curl_easy_setopt(curl, CURLOPT_CAINFO, ca_fname);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
		curl_easy_setopt(curl, CURLOPT_CERTINFO, 1L);
	} else {
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	}
	curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, curl_cb_debug);
	curl_easy_setopt(curl, CURLOPT_DEBUGDATA, ctx);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		if (!ctx->last_err)
			ctx->last_err = curl_easy_strerror(res);
		wpa_printf(MSG_ERROR, "curl_easy_perform() failed: %s",
			   ctx->last_err);
		curl_easy_cleanup(curl);
		fclose(f);
		return -1;
	}

	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
	wpa_printf(MSG_DEBUG, "curl: Server response code %ld", http);
	if (http != 200) {
		ctx->last_err = "HTTP download failed";
		wpa_printf(MSG_INFO, "HTTP download failed - code %ld", http);
		curl_easy_cleanup(curl);
		fclose(f);
		return -1;
	}

	curl_easy_cleanup(curl);
	fclose(f);

	return 0;
}


char * http_post(struct http_ctx *ctx, const char *url, const char *data,
		 const char *content_type, const char *ext_hdr,
		 const char *ca_fname,
		 const char *username, const char *password,
		 const char *client_cert, const char *client_key,
		 size_t *resp_len)
{
	long http = 0;
	CURLcode res;
	char *ret;
	CURL *curl;
	struct curl_slist *curl_hdr = NULL;

	ctx->last_err = NULL;
	wpa_printf(MSG_DEBUG, "curl: HTTP POST to %s", url);
	curl = setup_curl_post(ctx, url, ca_fname, username, password,
			       client_cert, client_key);
	if (curl == NULL)
		return NULL;

	if (content_type) {
		char ct[200];
		snprintf(ct, sizeof(ct), "Content-Type: %s", content_type);
		curl_hdr = curl_slist_append(curl_hdr, ct);
	}
	if (ext_hdr)
		curl_hdr = curl_slist_append(curl_hdr, ext_hdr);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_hdr);

	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
	free_curl_buf(ctx);

	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		if (!ctx->last_err)
			ctx->last_err = curl_easy_strerror(res);
		wpa_printf(MSG_ERROR, "curl_easy_perform() failed: %s",
			   ctx->last_err);
		free_curl_buf(ctx);
		return NULL;
	}

	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
	wpa_printf(MSG_DEBUG, "curl: Server response code %ld", http);
	if (http != 200) {
		ctx->last_err = "HTTP POST failed";
		wpa_printf(MSG_INFO, "HTTP POST failed - code %ld", http);
		free_curl_buf(ctx);
		return NULL;
	}

	if (ctx->curl_buf == NULL)
		return NULL;

	ret = ctx->curl_buf;
	if (resp_len)
		*resp_len = ctx->curl_buf_len;
	ctx->curl_buf = NULL;
	ctx->curl_buf_len = 0;

	wpa_printf(MSG_MSGDUMP, "Server response:\n%s", ret);

	return ret;
}


void http_set_cert_cb(struct http_ctx *ctx,
		      int (*cb)(void *ctx, struct http_cert *cert),
		      void *cb_ctx)
{
	ctx->cert_cb = cb;
	ctx->cert_cb_ctx = cb_ctx;
}


const char * http_get_err(struct http_ctx *ctx)
{
	return ctx->last_err;
}
