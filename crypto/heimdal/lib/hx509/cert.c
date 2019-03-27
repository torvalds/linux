/*
 * Copyright (c) 2004 - 2007 Kungliga Tekniska Högskolan
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

#include "hx_locl.h"
#include "crypto-headers.h"
#include <rtbl.h>

/**
 * @page page_cert The basic certificate
 *
 * The basic hx509 cerificate object in hx509 is hx509_cert. The
 * hx509_cert object is representing one X509/PKIX certificate and
 * associated attributes; like private key, friendly name, etc.
 *
 * A hx509_cert object is usully found via the keyset interfaces (@ref
 * page_keyset), but its also possible to create a certificate
 * directly from a parsed object with hx509_cert_init() and
 * hx509_cert_init_data().
 *
 * See the library functions here: @ref hx509_cert
 */

struct hx509_verify_ctx_data {
    hx509_certs trust_anchors;
    int flags;
#define HX509_VERIFY_CTX_F_TIME_SET			1
#define HX509_VERIFY_CTX_F_ALLOW_PROXY_CERTIFICATE	2
#define HX509_VERIFY_CTX_F_REQUIRE_RFC3280		4
#define HX509_VERIFY_CTX_F_CHECK_TRUST_ANCHORS		8
#define HX509_VERIFY_CTX_F_NO_DEFAULT_ANCHORS		16
#define HX509_VERIFY_CTX_F_NO_BEST_BEFORE_CHECK		32
    time_t time_now;
    unsigned int max_depth;
#define HX509_VERIFY_MAX_DEPTH 30
    hx509_revoke_ctx revoke_ctx;
};

#define REQUIRE_RFC3280(ctx) ((ctx)->flags & HX509_VERIFY_CTX_F_REQUIRE_RFC3280)
#define CHECK_TA(ctx) ((ctx)->flags & HX509_VERIFY_CTX_F_CHECK_TRUST_ANCHORS)
#define ALLOW_DEF_TA(ctx) (((ctx)->flags & HX509_VERIFY_CTX_F_NO_DEFAULT_ANCHORS) == 0)

struct _hx509_cert_attrs {
    size_t len;
    hx509_cert_attribute *val;
};

struct hx509_cert_data {
    unsigned int ref;
    char *friendlyname;
    Certificate *data;
    hx509_private_key private_key;
    struct _hx509_cert_attrs attrs;
    hx509_name basename;
    _hx509_cert_release_func release;
    void *ctx;
};

typedef struct hx509_name_constraints {
    NameConstraints *val;
    size_t len;
} hx509_name_constraints;

#define GeneralSubtrees_SET(g,var) \
	(g)->len = (var)->len, (g)->val = (var)->val;

/**
 * Creates a hx509 context that most functions in the library
 * uses. The context is only allowed to be used by one thread at each
 * moment. Free the context with hx509_context_free().
 *
 * @param context Returns a pointer to new hx509 context.
 *
 * @return Returns an hx509 error code.
 *
 * @ingroup hx509
 */

int
hx509_context_init(hx509_context *context)
{
    *context = calloc(1, sizeof(**context));
    if (*context == NULL)
	return ENOMEM;

    _hx509_ks_null_register(*context);
    _hx509_ks_mem_register(*context);
    _hx509_ks_file_register(*context);
    _hx509_ks_pkcs12_register(*context);
    _hx509_ks_pkcs11_register(*context);
    _hx509_ks_dir_register(*context);
    _hx509_ks_keychain_register(*context);

    ENGINE_add_conf_module();
    OpenSSL_add_all_algorithms();

    (*context)->ocsp_time_diff = HX509_DEFAULT_OCSP_TIME_DIFF;

    initialize_hx_error_table_r(&(*context)->et_list);
    initialize_asn1_error_table_r(&(*context)->et_list);

#ifdef HX509_DEFAULT_ANCHORS
    (void)hx509_certs_init(*context, HX509_DEFAULT_ANCHORS, 0,
			   NULL, &(*context)->default_trust_anchors);
#endif

    return 0;
}

/**
 * Selects if the hx509_revoke_verify() function is going to require
 * the existans of a revokation method (OCSP, CRL) or not. Note that
 * hx509_verify_path(), hx509_cms_verify_signed(), and other function
 * call hx509_revoke_verify().
 *
 * @param context hx509 context to change the flag for.
 * @param flag zero, revokation method required, non zero missing
 * revokation method ok
 *
 * @ingroup hx509_verify
 */

void
hx509_context_set_missing_revoke(hx509_context context, int flag)
{
    if (flag)
	context->flags |= HX509_CTX_VERIFY_MISSING_OK;
    else
	context->flags &= ~HX509_CTX_VERIFY_MISSING_OK;
}

/**
 * Free the context allocated by hx509_context_init().
 *
 * @param context context to be freed.
 *
 * @ingroup hx509
 */

void
hx509_context_free(hx509_context *context)
{
    hx509_clear_error_string(*context);
    if ((*context)->ks_ops) {
	free((*context)->ks_ops);
	(*context)->ks_ops = NULL;
    }
    (*context)->ks_num_ops = 0;
    free_error_table ((*context)->et_list);
    if ((*context)->querystat)
	free((*context)->querystat);
    memset(*context, 0, sizeof(**context));
    free(*context);
    *context = NULL;
}

/*
 *
 */

Certificate *
_hx509_get_cert(hx509_cert cert)
{
    return cert->data;
}

/*
 *
 */

int
_hx509_cert_get_version(const Certificate *t)
{
    return t->tbsCertificate.version ? *t->tbsCertificate.version + 1 : 1;
}

/**
 * Allocate and init an hx509 certificate object from the decoded
 * certificate `c´.
 *
 * @param context A hx509 context.
 * @param c
 * @param cert
 *
 * @return Returns an hx509 error code.
 *
 * @ingroup hx509_cert
 */

int
hx509_cert_init(hx509_context context, const Certificate *c, hx509_cert *cert)
{
    int ret;

    *cert = malloc(sizeof(**cert));
    if (*cert == NULL)
	return ENOMEM;
    (*cert)->ref = 1;
    (*cert)->friendlyname = NULL;
    (*cert)->attrs.len = 0;
    (*cert)->attrs.val = NULL;
    (*cert)->private_key = NULL;
    (*cert)->basename = NULL;
    (*cert)->release = NULL;
    (*cert)->ctx = NULL;

    (*cert)->data = calloc(1, sizeof(*(*cert)->data));
    if ((*cert)->data == NULL) {
	free(*cert);
	return ENOMEM;
    }
    ret = copy_Certificate(c, (*cert)->data);
    if (ret) {
	free((*cert)->data);
	free(*cert);
	*cert = NULL;
    }
    return ret;
}

/**
 * Just like hx509_cert_init(), but instead of a decode certificate
 * takes an pointer and length to a memory region that contains a
 * DER/BER encoded certificate.
 *
 * If the memory region doesn't contain just the certificate and
 * nothing more the function will fail with
 * HX509_EXTRA_DATA_AFTER_STRUCTURE.
 *
 * @param context A hx509 context.
 * @param ptr pointer to memory region containing encoded certificate.
 * @param len length of memory region.
 * @param cert a return pointer to a hx509 certificate object, will
 * contain NULL on error.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_cert
 */

int
hx509_cert_init_data(hx509_context context,
		     const void *ptr,
		     size_t len,
		     hx509_cert *cert)
{
    Certificate t;
    size_t size;
    int ret;

    ret = decode_Certificate(ptr, len, &t, &size);
    if (ret) {
	hx509_set_error_string(context, 0, ret, "Failed to decode certificate");
	return ret;
    }
    if (size != len) {
	free_Certificate(&t);
	hx509_set_error_string(context, 0, HX509_EXTRA_DATA_AFTER_STRUCTURE,
			       "Extra data after certificate");
	return HX509_EXTRA_DATA_AFTER_STRUCTURE;
    }

    ret = hx509_cert_init(context, &t, cert);
    free_Certificate(&t);
    return ret;
}

void
_hx509_cert_set_release(hx509_cert cert,
			_hx509_cert_release_func release,
			void *ctx)
{
    cert->release = release;
    cert->ctx = ctx;
}


/* Doesn't make a copy of `private_key'. */

int
_hx509_cert_assign_key(hx509_cert cert, hx509_private_key private_key)
{
    if (cert->private_key)
	hx509_private_key_free(&cert->private_key);
    cert->private_key = _hx509_private_key_ref(private_key);
    return 0;
}

/**
 * Free reference to the hx509 certificate object, if the refcounter
 * reaches 0, the object if freed. Its allowed to pass in NULL.
 *
 * @param cert the cert to free.
 *
 * @ingroup hx509_cert
 */

void
hx509_cert_free(hx509_cert cert)
{
    size_t i;

    if (cert == NULL)
	return;

    if (cert->ref <= 0)
	_hx509_abort("cert refcount <= 0 on free");
    if (--cert->ref > 0)
	return;

    if (cert->release)
	(cert->release)(cert, cert->ctx);

    if (cert->private_key)
	hx509_private_key_free(&cert->private_key);

    free_Certificate(cert->data);
    free(cert->data);

    for (i = 0; i < cert->attrs.len; i++) {
	der_free_octet_string(&cert->attrs.val[i]->data);
	der_free_oid(&cert->attrs.val[i]->oid);
	free(cert->attrs.val[i]);
    }
    free(cert->attrs.val);
    free(cert->friendlyname);
    if (cert->basename)
	hx509_name_free(&cert->basename);
    memset(cert, 0, sizeof(*cert));
    free(cert);
}

/**
 * Add a reference to a hx509 certificate object.
 *
 * @param cert a pointer to an hx509 certificate object.
 *
 * @return the same object as is passed in.
 *
 * @ingroup hx509_cert
 */

hx509_cert
hx509_cert_ref(hx509_cert cert)
{
    if (cert == NULL)
	return NULL;
    if (cert->ref <= 0)
	_hx509_abort("cert refcount <= 0");
    cert->ref++;
    if (cert->ref == 0)
	_hx509_abort("cert refcount == 0");
    return cert;
}

/**
 * Allocate an verification context that is used fo control the
 * verification process.
 *
 * @param context A hx509 context.
 * @param ctx returns a pointer to a hx509_verify_ctx object.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_verify
 */

int
hx509_verify_init_ctx(hx509_context context, hx509_verify_ctx *ctx)
{
    hx509_verify_ctx c;

    c = calloc(1, sizeof(*c));
    if (c == NULL)
	return ENOMEM;

    c->max_depth = HX509_VERIFY_MAX_DEPTH;

    *ctx = c;

    return 0;
}

/**
 * Free an hx509 verification context.
 *
 * @param ctx the context to be freed.
 *
 * @ingroup hx509_verify
 */

void
hx509_verify_destroy_ctx(hx509_verify_ctx ctx)
{
    if (ctx) {
	hx509_certs_free(&ctx->trust_anchors);
	hx509_revoke_free(&ctx->revoke_ctx);
	memset(ctx, 0, sizeof(*ctx));
    }
    free(ctx);
}

/**
 * Set the trust anchors in the verification context, makes an
 * reference to the keyset, so the consumer can free the keyset
 * independent of the destruction of the verification context (ctx).
 * If there already is a keyset attached, it's released.
 *
 * @param ctx a verification context
 * @param set a keyset containing the trust anchors.
 *
 * @ingroup hx509_verify
 */

void
hx509_verify_attach_anchors(hx509_verify_ctx ctx, hx509_certs set)
{
    if (ctx->trust_anchors)
	hx509_certs_free(&ctx->trust_anchors);
    ctx->trust_anchors = hx509_certs_ref(set);
}

/**
 * Attach an revocation context to the verfication context, , makes an
 * reference to the revoke context, so the consumer can free the
 * revoke context independent of the destruction of the verification
 * context. If there is no revoke context, the verification process is
 * NOT going to check any verification status.
 *
 * @param ctx a verification context.
 * @param revoke_ctx a revoke context.
 *
 * @ingroup hx509_verify
 */

void
hx509_verify_attach_revoke(hx509_verify_ctx ctx, hx509_revoke_ctx revoke_ctx)
{
    if (ctx->revoke_ctx)
	hx509_revoke_free(&ctx->revoke_ctx);
    ctx->revoke_ctx = _hx509_revoke_ref(revoke_ctx);
}

/**
 * Set the clock time the the verification process is going to
 * use. Used to check certificate in the past and future time. If not
 * set the current time will be used.
 *
 * @param ctx a verification context.
 * @param t the time the verifiation is using.
 *
 *
 * @ingroup hx509_verify
 */

void
hx509_verify_set_time(hx509_verify_ctx ctx, time_t t)
{
    ctx->flags |= HX509_VERIFY_CTX_F_TIME_SET;
    ctx->time_now = t;
}

time_t
_hx509_verify_get_time(hx509_verify_ctx ctx)
{
    return ctx->time_now;
}

/**
 * Set the maximum depth of the certificate chain that the path
 * builder is going to try.
 *
 * @param ctx a verification context
 * @param max_depth maxium depth of the certificate chain, include
 * trust anchor.
 *
 * @ingroup hx509_verify
 */

void
hx509_verify_set_max_depth(hx509_verify_ctx ctx, unsigned int max_depth)
{
    ctx->max_depth = max_depth;
}

/**
 * Allow or deny the use of proxy certificates
 *
 * @param ctx a verification context
 * @param boolean if non zero, allow proxy certificates.
 *
 * @ingroup hx509_verify
 */

void
hx509_verify_set_proxy_certificate(hx509_verify_ctx ctx, int boolean)
{
    if (boolean)
	ctx->flags |= HX509_VERIFY_CTX_F_ALLOW_PROXY_CERTIFICATE;
    else
	ctx->flags &= ~HX509_VERIFY_CTX_F_ALLOW_PROXY_CERTIFICATE;
}

/**
 * Select strict RFC3280 verification of certificiates. This means
 * checking key usage on CA certificates, this will make version 1
 * certificiates unuseable.
 *
 * @param ctx a verification context
 * @param boolean if non zero, use strict verification.
 *
 * @ingroup hx509_verify
 */

void
hx509_verify_set_strict_rfc3280_verification(hx509_verify_ctx ctx, int boolean)
{
    if (boolean)
	ctx->flags |= HX509_VERIFY_CTX_F_REQUIRE_RFC3280;
    else
	ctx->flags &= ~HX509_VERIFY_CTX_F_REQUIRE_RFC3280;
}

/**
 * Allow using the operating system builtin trust anchors if no other
 * trust anchors are configured.
 *
 * @param ctx a verification context
 * @param boolean if non zero, useing the operating systems builtin
 * trust anchors.
 *
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_cert
 */

void
hx509_verify_ctx_f_allow_default_trustanchors(hx509_verify_ctx ctx, int boolean)
{
    if (boolean)
	ctx->flags &= ~HX509_VERIFY_CTX_F_NO_DEFAULT_ANCHORS;
    else
	ctx->flags |= HX509_VERIFY_CTX_F_NO_DEFAULT_ANCHORS;
}

void
hx509_verify_ctx_f_allow_best_before_signature_algs(hx509_context ctx,
						    int boolean)
{
    if (boolean)
	ctx->flags &= ~HX509_VERIFY_CTX_F_NO_BEST_BEFORE_CHECK;
    else
	ctx->flags |= HX509_VERIFY_CTX_F_NO_BEST_BEFORE_CHECK;
}

static const Extension *
find_extension(const Certificate *cert, const heim_oid *oid, size_t *idx)
{
    const TBSCertificate *c = &cert->tbsCertificate;

    if (c->version == NULL || *c->version < 2 || c->extensions == NULL)
	return NULL;

    for (;*idx < c->extensions->len; (*idx)++) {
	if (der_heim_oid_cmp(&c->extensions->val[*idx].extnID, oid) == 0)
	    return &c->extensions->val[(*idx)++];
    }
    return NULL;
}

static int
find_extension_auth_key_id(const Certificate *subject,
			   AuthorityKeyIdentifier *ai)
{
    const Extension *e;
    size_t size;
    size_t i = 0;

    memset(ai, 0, sizeof(*ai));

    e = find_extension(subject, &asn1_oid_id_x509_ce_authorityKeyIdentifier, &i);
    if (e == NULL)
	return HX509_EXTENSION_NOT_FOUND;

    return decode_AuthorityKeyIdentifier(e->extnValue.data,
					 e->extnValue.length,
					 ai, &size);
}

int
_hx509_find_extension_subject_key_id(const Certificate *issuer,
				     SubjectKeyIdentifier *si)
{
    const Extension *e;
    size_t size;
    size_t i = 0;

    memset(si, 0, sizeof(*si));

    e = find_extension(issuer, &asn1_oid_id_x509_ce_subjectKeyIdentifier, &i);
    if (e == NULL)
	return HX509_EXTENSION_NOT_FOUND;

    return decode_SubjectKeyIdentifier(e->extnValue.data,
				       e->extnValue.length,
				       si, &size);
}

static int
find_extension_name_constraints(const Certificate *subject,
				NameConstraints *nc)
{
    const Extension *e;
    size_t size;
    size_t i = 0;

    memset(nc, 0, sizeof(*nc));

    e = find_extension(subject, &asn1_oid_id_x509_ce_nameConstraints, &i);
    if (e == NULL)
	return HX509_EXTENSION_NOT_FOUND;

    return decode_NameConstraints(e->extnValue.data,
				  e->extnValue.length,
				  nc, &size);
}

static int
find_extension_subject_alt_name(const Certificate *cert, size_t *i,
				GeneralNames *sa)
{
    const Extension *e;
    size_t size;

    memset(sa, 0, sizeof(*sa));

    e = find_extension(cert, &asn1_oid_id_x509_ce_subjectAltName, i);
    if (e == NULL)
	return HX509_EXTENSION_NOT_FOUND;

    return decode_GeneralNames(e->extnValue.data,
			       e->extnValue.length,
			       sa, &size);
}

static int
find_extension_eku(const Certificate *cert, ExtKeyUsage *eku)
{
    const Extension *e;
    size_t size;
    size_t i = 0;

    memset(eku, 0, sizeof(*eku));

    e = find_extension(cert, &asn1_oid_id_x509_ce_extKeyUsage, &i);
    if (e == NULL)
	return HX509_EXTENSION_NOT_FOUND;

    return decode_ExtKeyUsage(e->extnValue.data,
			      e->extnValue.length,
			      eku, &size);
}

static int
add_to_list(hx509_octet_string_list *list, const heim_octet_string *entry)
{
    void *p;
    int ret;

    p = realloc(list->val, (list->len + 1) * sizeof(list->val[0]));
    if (p == NULL)
	return ENOMEM;
    list->val = p;
    ret = der_copy_octet_string(entry, &list->val[list->len]);
    if (ret)
	return ret;
    list->len++;
    return 0;
}

/**
 * Free a list of octet strings returned by another hx509 library
 * function.
 *
 * @param list list to be freed.
 *
 * @ingroup hx509_misc
 */

void
hx509_free_octet_string_list(hx509_octet_string_list *list)
{
    size_t i;
    for (i = 0; i < list->len; i++)
	der_free_octet_string(&list->val[i]);
    free(list->val);
    list->val = NULL;
    list->len = 0;
}

/**
 * Return a list of subjectAltNames specified by oid in the
 * certificate. On error the
 *
 * The returned list of octet string should be freed with
 * hx509_free_octet_string_list().
 *
 * @param context A hx509 context.
 * @param cert a hx509 certificate object.
 * @param oid an oid to for SubjectAltName.
 * @param list list of matching SubjectAltName.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_cert
 */

int
hx509_cert_find_subjectAltName_otherName(hx509_context context,
					 hx509_cert cert,
					 const heim_oid *oid,
					 hx509_octet_string_list *list)
{
    GeneralNames sa;
    int ret;
    size_t i, j;

    list->val = NULL;
    list->len = 0;

    i = 0;
    while (1) {
	ret = find_extension_subject_alt_name(_hx509_get_cert(cert), &i, &sa);
	i++;
	if (ret == HX509_EXTENSION_NOT_FOUND) {
	    return 0;
	} else if (ret != 0) {
	    hx509_set_error_string(context, 0, ret, "Error searching for SAN");
	    hx509_free_octet_string_list(list);
	    return ret;
	}

	for (j = 0; j < sa.len; j++) {
	    if (sa.val[j].element == choice_GeneralName_otherName &&
		der_heim_oid_cmp(&sa.val[j].u.otherName.type_id, oid) == 0)
	    {
		ret = add_to_list(list, &sa.val[j].u.otherName.value);
		if (ret) {
		    hx509_set_error_string(context, 0, ret,
					   "Error adding an exra SAN to "
					   "return list");
		    hx509_free_octet_string_list(list);
		    free_GeneralNames(&sa);
		    return ret;
		}
	    }
	}
	free_GeneralNames(&sa);
    }
}


static int
check_key_usage(hx509_context context, const Certificate *cert,
		unsigned flags, int req_present)
{
    const Extension *e;
    KeyUsage ku;
    size_t size;
    int ret;
    size_t i = 0;
    unsigned ku_flags;

    if (_hx509_cert_get_version(cert) < 3)
	return 0;

    e = find_extension(cert, &asn1_oid_id_x509_ce_keyUsage, &i);
    if (e == NULL) {
	if (req_present) {
	    hx509_set_error_string(context, 0, HX509_KU_CERT_MISSING,
				   "Required extension key "
				   "usage missing from certifiate");
	    return HX509_KU_CERT_MISSING;
	}
	return 0;
    }

    ret = decode_KeyUsage(e->extnValue.data, e->extnValue.length, &ku, &size);
    if (ret)
	return ret;
    ku_flags = KeyUsage2int(ku);
    if ((ku_flags & flags) != flags) {
	unsigned missing = (~ku_flags) & flags;
	char buf[256], *name;

	unparse_flags(missing, asn1_KeyUsage_units(), buf, sizeof(buf));
	_hx509_unparse_Name(&cert->tbsCertificate.subject, &name);
	hx509_set_error_string(context, 0, HX509_KU_CERT_MISSING,
			       "Key usage %s required but missing "
			       "from certifiate %s", buf, name);
	free(name);
	return HX509_KU_CERT_MISSING;
    }
    return 0;
}

/*
 * Return 0 on matching key usage 'flags' for 'cert', otherwise return
 * an error code. If 'req_present' the existance is required of the
 * KeyUsage extension.
 */

int
_hx509_check_key_usage(hx509_context context, hx509_cert cert,
		       unsigned flags, int req_present)
{
    return check_key_usage(context, _hx509_get_cert(cert), flags, req_present);
}

enum certtype { PROXY_CERT, EE_CERT, CA_CERT };

static int
check_basic_constraints(hx509_context context, const Certificate *cert,
			enum certtype type, size_t depth)
{
    BasicConstraints bc;
    const Extension *e;
    size_t size;
    int ret;
    size_t i = 0;

    if (_hx509_cert_get_version(cert) < 3)
	return 0;

    e = find_extension(cert, &asn1_oid_id_x509_ce_basicConstraints, &i);
    if (e == NULL) {
	switch(type) {
	case PROXY_CERT:
	case EE_CERT:
	    return 0;
	case CA_CERT: {
	    char *name;
	    ret = _hx509_unparse_Name(&cert->tbsCertificate.subject, &name);
	    assert(ret == 0);
	    hx509_set_error_string(context, 0, HX509_EXTENSION_NOT_FOUND,
				   "basicConstraints missing from "
				   "CA certifiacte %s", name);
	    free(name);
	    return HX509_EXTENSION_NOT_FOUND;
	}
	}
    }

    ret = decode_BasicConstraints(e->extnValue.data,
				  e->extnValue.length, &bc,
				  &size);
    if (ret)
	return ret;
    switch(type) {
    case PROXY_CERT:
	if (bc.cA != NULL && *bc.cA)
	    ret = HX509_PARENT_IS_CA;
	break;
    case EE_CERT:
	ret = 0;
	break;
    case CA_CERT:
	if (bc.cA == NULL || !*bc.cA)
	    ret = HX509_PARENT_NOT_CA;
	else if (bc.pathLenConstraint)
	    if (depth - 1 > *bc.pathLenConstraint)
		ret = HX509_CA_PATH_TOO_DEEP;
	break;
    }
    free_BasicConstraints(&bc);
    return ret;
}

int
_hx509_cert_is_parent_cmp(const Certificate *subject,
			  const Certificate *issuer,
			  int allow_self_signed)
{
    int diff;
    AuthorityKeyIdentifier ai;
    SubjectKeyIdentifier si;
    int ret_ai, ret_si, ret;

    ret = _hx509_name_cmp(&issuer->tbsCertificate.subject,
			  &subject->tbsCertificate.issuer,
			  &diff);
    if (ret)
	return ret;
    if (diff)
	return diff;

    memset(&ai, 0, sizeof(ai));
    memset(&si, 0, sizeof(si));

    /*
     * Try to find AuthorityKeyIdentifier, if it's not present in the
     * subject certificate nor the parent.
     */

    ret_ai = find_extension_auth_key_id(subject, &ai);
    if (ret_ai && ret_ai != HX509_EXTENSION_NOT_FOUND)
	return 1;
    ret_si = _hx509_find_extension_subject_key_id(issuer, &si);
    if (ret_si && ret_si != HX509_EXTENSION_NOT_FOUND)
	return -1;

    if (ret_si && ret_ai)
	goto out;
    if (ret_ai)
	goto out;
    if (ret_si) {
	if (allow_self_signed) {
	    diff = 0;
	    goto out;
	} else if (ai.keyIdentifier) {
	    diff = -1;
	    goto out;
	}
    }

    if (ai.keyIdentifier == NULL) {
	Name name;

	if (ai.authorityCertIssuer == NULL)
	    return -1;
	if (ai.authorityCertSerialNumber == NULL)
	    return -1;

	diff = der_heim_integer_cmp(ai.authorityCertSerialNumber,
				    &issuer->tbsCertificate.serialNumber);
	if (diff)
	    return diff;
	if (ai.authorityCertIssuer->len != 1)
	    return -1;
	if (ai.authorityCertIssuer->val[0].element != choice_GeneralName_directoryName)
	    return -1;

	name.element =
	    ai.authorityCertIssuer->val[0].u.directoryName.element;
	name.u.rdnSequence =
	    ai.authorityCertIssuer->val[0].u.directoryName.u.rdnSequence;

	ret = _hx509_name_cmp(&issuer->tbsCertificate.subject,
			      &name,
			      &diff);
	if (ret)
	    return ret;
	if (diff)
	    return diff;
	diff = 0;
    } else
	diff = der_heim_octet_string_cmp(ai.keyIdentifier, &si);
    if (diff)
	goto out;

 out:
    free_AuthorityKeyIdentifier(&ai);
    free_SubjectKeyIdentifier(&si);
    return diff;
}

static int
certificate_is_anchor(hx509_context context,
		      hx509_certs trust_anchors,
		      const hx509_cert cert)
{
    hx509_query q;
    hx509_cert c;
    int ret;

    if (trust_anchors == NULL)
	return 0;

    _hx509_query_clear(&q);

    q.match = HX509_QUERY_MATCH_CERTIFICATE;
    q.certificate = _hx509_get_cert(cert);

    ret = hx509_certs_find(context, trust_anchors, &q, &c);
    if (ret == 0)
	hx509_cert_free(c);
    return ret == 0;
}

static int
certificate_is_self_signed(hx509_context context,
			   const Certificate *cert,
			   int *self_signed)
{
    int ret, diff;
    ret = _hx509_name_cmp(&cert->tbsCertificate.subject,
			  &cert->tbsCertificate.issuer, &diff);
    *self_signed = (diff == 0);
    if (ret) {
	hx509_set_error_string(context, 0, ret,
			       "Failed to check if self signed");
    } else
	ret = _hx509_self_signed_valid(context, &cert->signatureAlgorithm);

    return ret;
}

/*
 * The subjectName is "null" when it's empty set of relative DBs.
 */

static int
subject_null_p(const Certificate *c)
{
    return c->tbsCertificate.subject.u.rdnSequence.len == 0;
}


static int
find_parent(hx509_context context,
	    time_t time_now,
	    hx509_certs trust_anchors,
	    hx509_path *path,
	    hx509_certs pool,
	    hx509_cert current,
	    hx509_cert *parent)
{
    AuthorityKeyIdentifier ai;
    hx509_query q;
    int ret;

    *parent = NULL;
    memset(&ai, 0, sizeof(ai));

    _hx509_query_clear(&q);

    if (!subject_null_p(current->data)) {
	q.match |= HX509_QUERY_FIND_ISSUER_CERT;
	q.subject = _hx509_get_cert(current);
    } else {
	ret = find_extension_auth_key_id(current->data, &ai);
	if (ret) {
	    hx509_set_error_string(context, 0, HX509_CERTIFICATE_MALFORMED,
				   "Subjectless certificate missing AuthKeyID");
	    return HX509_CERTIFICATE_MALFORMED;
	}

	if (ai.keyIdentifier == NULL) {
	    free_AuthorityKeyIdentifier(&ai);
	    hx509_set_error_string(context, 0, HX509_CERTIFICATE_MALFORMED,
				   "Subjectless certificate missing keyIdentifier "
				   "inside AuthKeyID");
	    return HX509_CERTIFICATE_MALFORMED;
	}

	q.subject_id = ai.keyIdentifier;
	q.match = HX509_QUERY_MATCH_SUBJECT_KEY_ID;
    }

    q.path = path;
    q.match |= HX509_QUERY_NO_MATCH_PATH;

    if (pool) {
	q.timenow = time_now;
	q.match |= HX509_QUERY_MATCH_TIME;

	ret = hx509_certs_find(context, pool, &q, parent);
	if (ret == 0) {
	    free_AuthorityKeyIdentifier(&ai);
	    return 0;
	}
	q.match &= ~HX509_QUERY_MATCH_TIME;
    }

    if (trust_anchors) {
	ret = hx509_certs_find(context, trust_anchors, &q, parent);
	if (ret == 0) {
	    free_AuthorityKeyIdentifier(&ai);
	    return ret;
	}
    }
    free_AuthorityKeyIdentifier(&ai);

    {
	hx509_name name;
	char *str;

	ret = hx509_cert_get_subject(current, &name);
	if (ret) {
	    hx509_clear_error_string(context);
	    return HX509_ISSUER_NOT_FOUND;
	}
	ret = hx509_name_to_string(name, &str);
	hx509_name_free(&name);
	if (ret) {
	    hx509_clear_error_string(context);
	    return HX509_ISSUER_NOT_FOUND;
	}

	hx509_set_error_string(context, 0, HX509_ISSUER_NOT_FOUND,
			       "Failed to find issuer for "
			       "certificate with subject: '%s'", str);
	free(str);
    }
    return HX509_ISSUER_NOT_FOUND;
}

/*
 *
 */

static int
is_proxy_cert(hx509_context context,
	      const Certificate *cert,
	      ProxyCertInfo *rinfo)
{
    ProxyCertInfo info;
    const Extension *e;
    size_t size;
    int ret;
    size_t i = 0;

    if (rinfo)
	memset(rinfo, 0, sizeof(*rinfo));

    e = find_extension(cert, &asn1_oid_id_pkix_pe_proxyCertInfo, &i);
    if (e == NULL) {
	hx509_clear_error_string(context);
	return HX509_EXTENSION_NOT_FOUND;
    }

    ret = decode_ProxyCertInfo(e->extnValue.data,
			       e->extnValue.length,
			       &info,
			       &size);
    if (ret) {
	hx509_clear_error_string(context);
	return ret;
    }
    if (size != e->extnValue.length) {
	free_ProxyCertInfo(&info);
	hx509_clear_error_string(context);
	return HX509_EXTRA_DATA_AFTER_STRUCTURE;
    }
    if (rinfo == NULL)
	free_ProxyCertInfo(&info);
    else
	*rinfo = info;

    return 0;
}

/*
 * Path operations are like MEMORY based keyset, but with exposed
 * internal so we can do easy searches.
 */

int
_hx509_path_append(hx509_context context, hx509_path *path, hx509_cert cert)
{
    hx509_cert *val;
    val = realloc(path->val, (path->len + 1) * sizeof(path->val[0]));
    if (val == NULL) {
	hx509_set_error_string(context, 0, ENOMEM, "out of memory");
	return ENOMEM;
    }

    path->val = val;
    path->val[path->len] = hx509_cert_ref(cert);
    path->len++;

    return 0;
}

void
_hx509_path_free(hx509_path *path)
{
    unsigned i;

    for (i = 0; i < path->len; i++)
	hx509_cert_free(path->val[i]);
    free(path->val);
    path->val = NULL;
    path->len = 0;
}

/*
 * Find path by looking up issuer for the top certificate and continue
 * until an anchor certificate is found or max limit is found. A
 * certificate never included twice in the path.
 *
 * If the trust anchors are not given, calculate optimistic path, just
 * follow the chain upward until we no longer find a parent or we hit
 * the max path limit. In this case, a failure will always be returned
 * depending on what error condition is hit first.
 *
 * The path includes a path from the top certificate to the anchor
 * certificate.
 *
 * The caller needs to free `path´ both on successful built path and
 * failure.
 */

int
_hx509_calculate_path(hx509_context context,
		      int flags,
		      time_t time_now,
		      hx509_certs anchors,
		      unsigned int max_depth,
		      hx509_cert cert,
		      hx509_certs pool,
		      hx509_path *path)
{
    hx509_cert parent, current;
    int ret;

    if (max_depth == 0)
	max_depth = HX509_VERIFY_MAX_DEPTH;

    ret = _hx509_path_append(context, path, cert);
    if (ret)
	return ret;

    current = hx509_cert_ref(cert);

    while (!certificate_is_anchor(context, anchors, current)) {

	ret = find_parent(context, time_now, anchors, path,
			  pool, current, &parent);
	hx509_cert_free(current);
	if (ret)
	    return ret;

	ret = _hx509_path_append(context, path, parent);
	if (ret)
	    return ret;
	current = parent;

	if (path->len > max_depth) {
	    hx509_cert_free(current);
	    hx509_set_error_string(context, 0, HX509_PATH_TOO_LONG,
				   "Path too long while bulding "
				   "certificate chain");
	    return HX509_PATH_TOO_LONG;
	}
    }

    if ((flags & HX509_CALCULATE_PATH_NO_ANCHOR) &&
	path->len > 0 &&
	certificate_is_anchor(context, anchors, path->val[path->len - 1]))
    {
	hx509_cert_free(path->val[path->len - 1]);
	path->len--;
    }

    hx509_cert_free(current);
    return 0;
}

int
_hx509_AlgorithmIdentifier_cmp(const AlgorithmIdentifier *p,
			       const AlgorithmIdentifier *q)
{
    int diff;
    diff = der_heim_oid_cmp(&p->algorithm, &q->algorithm);
    if (diff)
	return diff;
    if (p->parameters) {
	if (q->parameters)
	    return heim_any_cmp(p->parameters,
				q->parameters);
	else
	    return 1;
    } else {
	if (q->parameters)
	    return -1;
	else
	    return 0;
    }
}

int
_hx509_Certificate_cmp(const Certificate *p, const Certificate *q)
{
    int diff;
    diff = der_heim_bit_string_cmp(&p->signatureValue, &q->signatureValue);
    if (diff)
	return diff;
    diff = _hx509_AlgorithmIdentifier_cmp(&p->signatureAlgorithm,
					  &q->signatureAlgorithm);
    if (diff)
	return diff;
    diff = der_heim_octet_string_cmp(&p->tbsCertificate._save,
				     &q->tbsCertificate._save);
    return diff;
}

/**
 * Compare to hx509 certificate object, useful for sorting.
 *
 * @param p a hx509 certificate object.
 * @param q a hx509 certificate object.
 *
 * @return 0 the objects are the same, returns > 0 is p is "larger"
 * then q, < 0 if p is "smaller" then q.
 *
 * @ingroup hx509_cert
 */

int
hx509_cert_cmp(hx509_cert p, hx509_cert q)
{
    return _hx509_Certificate_cmp(p->data, q->data);
}

/**
 * Return the name of the issuer of the hx509 certificate.
 *
 * @param p a hx509 certificate object.
 * @param name a pointer to a hx509 name, should be freed by
 * hx509_name_free().
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_cert
 */

int
hx509_cert_get_issuer(hx509_cert p, hx509_name *name)
{
    return _hx509_name_from_Name(&p->data->tbsCertificate.issuer, name);
}

/**
 * Return the name of the subject of the hx509 certificate.
 *
 * @param p a hx509 certificate object.
 * @param name a pointer to a hx509 name, should be freed by
 * hx509_name_free(). See also hx509_cert_get_base_subject().
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_cert
 */

int
hx509_cert_get_subject(hx509_cert p, hx509_name *name)
{
    return _hx509_name_from_Name(&p->data->tbsCertificate.subject, name);
}

/**
 * Return the name of the base subject of the hx509 certificate. If
 * the certiicate is a verified proxy certificate, the this function
 * return the base certificate (root of the proxy chain). If the proxy
 * certificate is not verified with the base certificate
 * HX509_PROXY_CERTIFICATE_NOT_CANONICALIZED is returned.
 *
 * @param context a hx509 context.
 * @param c a hx509 certificate object.
 * @param name a pointer to a hx509 name, should be freed by
 * hx509_name_free(). See also hx509_cert_get_subject().
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_cert
 */

int
hx509_cert_get_base_subject(hx509_context context, hx509_cert c,
			    hx509_name *name)
{
    if (c->basename)
	return hx509_name_copy(context, c->basename, name);
    if (is_proxy_cert(context, c->data, NULL) == 0) {
	int ret = HX509_PROXY_CERTIFICATE_NOT_CANONICALIZED;
	hx509_set_error_string(context, 0, ret,
			       "Proxy certificate have not been "
			       "canonicalize yet, no base name");
	return ret;
    }
    return _hx509_name_from_Name(&c->data->tbsCertificate.subject, name);
}

/**
 * Get serial number of the certificate.
 *
 * @param p a hx509 certificate object.
 * @param i serial number, should be freed ith der_free_heim_integer().
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_cert
 */

int
hx509_cert_get_serialnumber(hx509_cert p, heim_integer *i)
{
    return der_copy_heim_integer(&p->data->tbsCertificate.serialNumber, i);
}

/**
 * Get notBefore time of the certificate.
 *
 * @param p a hx509 certificate object.
 *
 * @return return not before time
 *
 * @ingroup hx509_cert
 */

time_t
hx509_cert_get_notBefore(hx509_cert p)
{
    return _hx509_Time2time_t(&p->data->tbsCertificate.validity.notBefore);
}

/**
 * Get notAfter time of the certificate.
 *
 * @param p a hx509 certificate object.
 *
 * @return return not after time.
 *
 * @ingroup hx509_cert
 */

time_t
hx509_cert_get_notAfter(hx509_cert p)
{
    return _hx509_Time2time_t(&p->data->tbsCertificate.validity.notAfter);
}

/**
 * Get the SubjectPublicKeyInfo structure from the hx509 certificate.
 *
 * @param context a hx509 context.
 * @param p a hx509 certificate object.
 * @param spki SubjectPublicKeyInfo, should be freed with
 * free_SubjectPublicKeyInfo().
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_cert
 */

int
hx509_cert_get_SPKI(hx509_context context, hx509_cert p, SubjectPublicKeyInfo *spki)
{
    int ret;

    ret = copy_SubjectPublicKeyInfo(&p->data->tbsCertificate.subjectPublicKeyInfo, spki);
    if (ret)
	hx509_set_error_string(context, 0, ret, "Failed to copy SPKI");
    return ret;
}

/**
 * Get the AlgorithmIdentifier from the hx509 certificate.
 *
 * @param context a hx509 context.
 * @param p a hx509 certificate object.
 * @param alg AlgorithmIdentifier, should be freed with
 *            free_AlgorithmIdentifier(). The algorithmidentifier is
 *            typicly rsaEncryption, or id-ecPublicKey, or some other
 *            public key mechanism.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_cert
 */

int
hx509_cert_get_SPKI_AlgorithmIdentifier(hx509_context context,
					hx509_cert p,
					AlgorithmIdentifier *alg)
{
    int ret;

    ret = copy_AlgorithmIdentifier(&p->data->tbsCertificate.subjectPublicKeyInfo.algorithm, alg);
    if (ret)
	hx509_set_error_string(context, 0, ret,
			       "Failed to copy SPKI AlgorithmIdentifier");
    return ret;
}

static int
get_x_unique_id(hx509_context context, const char *name,
		const heim_bit_string *cert, heim_bit_string *subject)
{
    int ret;

    if (cert == NULL) {
	ret = HX509_EXTENSION_NOT_FOUND;
	hx509_set_error_string(context, 0, ret, "%s unique id doesn't exists", name);
	return ret;
    }
    ret = der_copy_bit_string(cert, subject);
    if (ret) {
	hx509_set_error_string(context, 0, ret, "malloc out of memory", name);
	return ret;
    }
    return 0;
}

/**
 * Get a copy of the Issuer Unique ID
 *
 * @param context a hx509_context
 * @param p a hx509 certificate
 * @param issuer the issuer id returned, free with der_free_bit_string()
 *
 * @return An hx509 error code, see hx509_get_error_string(). The
 * error code HX509_EXTENSION_NOT_FOUND is returned if the certificate
 * doesn't have a issuerUniqueID
 *
 * @ingroup hx509_cert
 */

int
hx509_cert_get_issuer_unique_id(hx509_context context, hx509_cert p, heim_bit_string *issuer)
{
    return get_x_unique_id(context, "issuer", p->data->tbsCertificate.issuerUniqueID, issuer);
}

/**
 * Get a copy of the Subect Unique ID
 *
 * @param context a hx509_context
 * @param p a hx509 certificate
 * @param subject the subject id returned, free with der_free_bit_string()
 *
 * @return An hx509 error code, see hx509_get_error_string(). The
 * error code HX509_EXTENSION_NOT_FOUND is returned if the certificate
 * doesn't have a subjectUniqueID
 *
 * @ingroup hx509_cert
 */

int
hx509_cert_get_subject_unique_id(hx509_context context, hx509_cert p, heim_bit_string *subject)
{
    return get_x_unique_id(context, "subject", p->data->tbsCertificate.subjectUniqueID, subject);
}


hx509_private_key
_hx509_cert_private_key(hx509_cert p)
{
    return p->private_key;
}

int
hx509_cert_have_private_key(hx509_cert p)
{
    return p->private_key ? 1 : 0;
}


int
_hx509_cert_private_key_exportable(hx509_cert p)
{
    if (p->private_key == NULL)
	return 0;
    return _hx509_private_key_exportable(p->private_key);
}

int
_hx509_cert_private_decrypt(hx509_context context,
			    const heim_octet_string *ciphertext,
			    const heim_oid *encryption_oid,
			    hx509_cert p,
			    heim_octet_string *cleartext)
{
    cleartext->data = NULL;
    cleartext->length = 0;

    if (p->private_key == NULL) {
	hx509_set_error_string(context, 0, HX509_PRIVATE_KEY_MISSING,
			       "Private key missing");
	return HX509_PRIVATE_KEY_MISSING;
    }

    return hx509_private_key_private_decrypt(context,
					      ciphertext,
					      encryption_oid,
					      p->private_key,
					      cleartext);
}

int
hx509_cert_public_encrypt(hx509_context context,
			   const heim_octet_string *cleartext,
			   const hx509_cert p,
			   heim_oid *encryption_oid,
			   heim_octet_string *ciphertext)
{
    return _hx509_public_encrypt(context,
				 cleartext, p->data,
				 encryption_oid, ciphertext);
}

/*
 *
 */

time_t
_hx509_Time2time_t(const Time *t)
{
    switch(t->element) {
    case choice_Time_utcTime:
	return t->u.utcTime;
    case choice_Time_generalTime:
	return t->u.generalTime;
    }
    return 0;
}

/*
 *
 */

static int
init_name_constraints(hx509_name_constraints *nc)
{
    memset(nc, 0, sizeof(*nc));
    return 0;
}

static int
add_name_constraints(hx509_context context, const Certificate *c, int not_ca,
		     hx509_name_constraints *nc)
{
    NameConstraints tnc;
    int ret;

    ret = find_extension_name_constraints(c, &tnc);
    if (ret == HX509_EXTENSION_NOT_FOUND)
	return 0;
    else if (ret) {
	hx509_set_error_string(context, 0, ret, "Failed getting NameConstraints");
	return ret;
    } else if (not_ca) {
	ret = HX509_VERIFY_CONSTRAINTS;
	hx509_set_error_string(context, 0, ret, "Not a CA and "
			       "have NameConstraints");
    } else {
	NameConstraints *val;
	val = realloc(nc->val, sizeof(nc->val[0]) * (nc->len + 1));
	if (val == NULL) {
	    hx509_clear_error_string(context);
	    ret = ENOMEM;
	    goto out;
	}
	nc->val = val;
	ret = copy_NameConstraints(&tnc, &nc->val[nc->len]);
	if (ret) {
	    hx509_clear_error_string(context);
	    goto out;
	}
	nc->len += 1;
    }
out:
    free_NameConstraints(&tnc);
    return ret;
}

static int
match_RDN(const RelativeDistinguishedName *c,
	  const RelativeDistinguishedName *n)
{
    size_t i;

    if (c->len != n->len)
	return HX509_NAME_CONSTRAINT_ERROR;

    for (i = 0; i < n->len; i++) {
	int diff, ret;

	if (der_heim_oid_cmp(&c->val[i].type, &n->val[i].type) != 0)
	    return HX509_NAME_CONSTRAINT_ERROR;
	ret = _hx509_name_ds_cmp(&c->val[i].value, &n->val[i].value, &diff);
	if (ret)
	    return ret;
	if (diff != 0)
	    return HX509_NAME_CONSTRAINT_ERROR;
    }
    return 0;
}

static int
match_X501Name(const Name *c, const Name *n)
{
    size_t i;
    int ret;

    if (c->element != choice_Name_rdnSequence
	|| n->element != choice_Name_rdnSequence)
	return 0;
    if (c->u.rdnSequence.len > n->u.rdnSequence.len)
	return HX509_NAME_CONSTRAINT_ERROR;
    for (i = 0; i < c->u.rdnSequence.len; i++) {
	ret = match_RDN(&c->u.rdnSequence.val[i], &n->u.rdnSequence.val[i]);
	if (ret)
	    return ret;
    }
    return 0;
}


static int
match_general_name(const GeneralName *c, const GeneralName *n, int *match)
{
    /*
     * Name constraints only apply to the same name type, see RFC3280,
     * 4.2.1.11.
     */
    assert(c->element == n->element);

    switch(c->element) {
    case choice_GeneralName_otherName:
	if (der_heim_oid_cmp(&c->u.otherName.type_id,
			 &n->u.otherName.type_id) != 0)
	    return HX509_NAME_CONSTRAINT_ERROR;
	if (heim_any_cmp(&c->u.otherName.value,
			 &n->u.otherName.value) != 0)
	    return HX509_NAME_CONSTRAINT_ERROR;
	*match = 1;
	return 0;
    case choice_GeneralName_rfc822Name: {
	const char *s;
	size_t len1, len2;
	s = memchr(c->u.rfc822Name.data, '@', c->u.rfc822Name.length);
	if (s) {
	    if (der_printable_string_cmp(&c->u.rfc822Name, &n->u.rfc822Name) != 0)
		return HX509_NAME_CONSTRAINT_ERROR;
	} else {
	    s = memchr(n->u.rfc822Name.data, '@', n->u.rfc822Name.length);
	    if (s == NULL)
		return HX509_NAME_CONSTRAINT_ERROR;
	    len1 = c->u.rfc822Name.length;
	    len2 = n->u.rfc822Name.length -
		(s - ((char *)n->u.rfc822Name.data));
	    if (len1 > len2)
		return HX509_NAME_CONSTRAINT_ERROR;
	    if (memcmp(s + 1 + len2 - len1, c->u.rfc822Name.data, len1) != 0)
		return HX509_NAME_CONSTRAINT_ERROR;
	    if (len1 < len2 && s[len2 - len1 + 1] != '.')
		return HX509_NAME_CONSTRAINT_ERROR;
	}
	*match = 1;
	return 0;
    }
    case choice_GeneralName_dNSName: {
	size_t lenc, lenn;
	char *ptr;

	lenc = c->u.dNSName.length;
	lenn = n->u.dNSName.length;
	if (lenc > lenn)
	    return HX509_NAME_CONSTRAINT_ERROR;
	ptr = n->u.dNSName.data;
	if (memcmp(&ptr[lenn - lenc], c->u.dNSName.data, lenc) != 0)
	    return HX509_NAME_CONSTRAINT_ERROR;
	if (lenn != lenc && ptr[lenn - lenc - 1] != '.')
	    return HX509_NAME_CONSTRAINT_ERROR;
	*match = 1;
	return 0;
    }
    case choice_GeneralName_directoryName: {
	Name c_name, n_name;
	int ret;

	c_name._save.data = NULL;
	c_name._save.length = 0;
	c_name.element = c->u.directoryName.element;
	c_name.u.rdnSequence = c->u.directoryName.u.rdnSequence;

	n_name._save.data = NULL;
	n_name._save.length = 0;
	n_name.element = n->u.directoryName.element;
	n_name.u.rdnSequence = n->u.directoryName.u.rdnSequence;

	ret = match_X501Name(&c_name, &n_name);
	if (ret == 0)
	    *match = 1;
	return ret;
    }
    case choice_GeneralName_uniformResourceIdentifier:
    case choice_GeneralName_iPAddress:
    case choice_GeneralName_registeredID:
    default:
	return HX509_NAME_CONSTRAINT_ERROR;
    }
}

static int
match_alt_name(const GeneralName *n, const Certificate *c,
	       int *same, int *match)
{
    GeneralNames sa;
    int ret;
    size_t i, j;

    i = 0;
    do {
	ret = find_extension_subject_alt_name(c, &i, &sa);
	if (ret == HX509_EXTENSION_NOT_FOUND) {
	    ret = 0;
	    break;
	} else if (ret != 0)
	    break;

	for (j = 0; j < sa.len; j++) {
	    if (n->element == sa.val[j].element) {
		*same = 1;
		ret = match_general_name(n, &sa.val[j], match);
	    }
	}
	free_GeneralNames(&sa);
    } while (1);
    return ret;
}


static int
match_tree(const GeneralSubtrees *t, const Certificate *c, int *match)
{
    int name, alt_name, same;
    unsigned int i;
    int ret = 0;

    name = alt_name = same = *match = 0;
    for (i = 0; i < t->len; i++) {
	if (t->val[i].minimum && t->val[i].maximum)
	    return HX509_RANGE;

	/*
	 * If the constraint apply to directoryNames, test is with
	 * subjectName of the certificate if the certificate have a
	 * non-null (empty) subjectName.
	 */

	if (t->val[i].base.element == choice_GeneralName_directoryName
	    && !subject_null_p(c))
	{
	    GeneralName certname;

	    memset(&certname, 0, sizeof(certname));
	    certname.element = choice_GeneralName_directoryName;
	    certname.u.directoryName.element =
		c->tbsCertificate.subject.element;
	    certname.u.directoryName.u.rdnSequence =
		c->tbsCertificate.subject.u.rdnSequence;

	    ret = match_general_name(&t->val[i].base, &certname, &name);
	}

	/* Handle subjectAltNames, this is icky since they
	 * restrictions only apply if the subjectAltName is of the
	 * same type. So if there have been a match of type, require
	 * altname to be set.
	 */
	ret = match_alt_name(&t->val[i].base, c, &same, &alt_name);
    }
    if (name && (!same || alt_name))
	*match = 1;
    return ret;
}

static int
check_name_constraints(hx509_context context,
		       const hx509_name_constraints *nc,
		       const Certificate *c)
{
    int match, ret;
    size_t i;

    for (i = 0 ; i < nc->len; i++) {
	GeneralSubtrees gs;

	if (nc->val[i].permittedSubtrees) {
	    GeneralSubtrees_SET(&gs, nc->val[i].permittedSubtrees);
	    ret = match_tree(&gs, c, &match);
	    if (ret) {
		hx509_clear_error_string(context);
		return ret;
	    }
	    /* allow null subjectNames, they wont matches anything */
	    if (match == 0 && !subject_null_p(c)) {
		hx509_set_error_string(context, 0, HX509_VERIFY_CONSTRAINTS,
				       "Error verify constraints, "
				       "certificate didn't match any "
				       "permitted subtree");
		return HX509_VERIFY_CONSTRAINTS;
	    }
	}
	if (nc->val[i].excludedSubtrees) {
	    GeneralSubtrees_SET(&gs, nc->val[i].excludedSubtrees);
	    ret = match_tree(&gs, c, &match);
	    if (ret) {
		hx509_clear_error_string(context);
		return ret;
	    }
	    if (match) {
		hx509_set_error_string(context, 0, HX509_VERIFY_CONSTRAINTS,
				       "Error verify constraints, "
				       "certificate included in excluded "
				       "subtree");
		return HX509_VERIFY_CONSTRAINTS;
	    }
	}
    }
    return 0;
}

static void
free_name_constraints(hx509_name_constraints *nc)
{
    size_t i;

    for (i = 0 ; i < nc->len; i++)
	free_NameConstraints(&nc->val[i]);
    free(nc->val);
}

/**
 * Build and verify the path for the certificate to the trust anchor
 * specified in the verify context. The path is constructed from the
 * certificate, the pool and the trust anchors.
 *
 * @param context A hx509 context.
 * @param ctx A hx509 verification context.
 * @param cert the certificate to build the path from.
 * @param pool A keyset of certificates to build the chain from.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_verify
 */

int
hx509_verify_path(hx509_context context,
		  hx509_verify_ctx ctx,
		  hx509_cert cert,
		  hx509_certs pool)
{
    hx509_name_constraints nc;
    hx509_path path;
    int ret, proxy_cert_depth, selfsigned_depth, diff;
    size_t i, k;
    enum certtype type;
    Name proxy_issuer;
    hx509_certs anchors = NULL;

    memset(&proxy_issuer, 0, sizeof(proxy_issuer));

    ret = init_name_constraints(&nc);
    if (ret)
	return ret;

    path.val = NULL;
    path.len = 0;

    if ((ctx->flags & HX509_VERIFY_CTX_F_TIME_SET) == 0)
	ctx->time_now = time(NULL);

    /*
     *
     */
    if (ctx->trust_anchors)
	anchors = hx509_certs_ref(ctx->trust_anchors);
    else if (context->default_trust_anchors && ALLOW_DEF_TA(ctx))
	anchors = hx509_certs_ref(context->default_trust_anchors);
    else {
	ret = hx509_certs_init(context, "MEMORY:no-TA", 0, NULL, &anchors);
	if (ret)
	    goto out;
    }

    /*
     * Calculate the path from the certificate user presented to the
     * to an anchor.
     */
    ret = _hx509_calculate_path(context, 0, ctx->time_now,
				anchors, ctx->max_depth,
				cert, pool, &path);
    if (ret)
	goto out;

    /*
     * Check CA and proxy certificate chain from the top of the
     * certificate chain. Also check certificate is valid with respect
     * to the current time.
     *
     */

    proxy_cert_depth = 0;
    selfsigned_depth = 0;

    if (ctx->flags & HX509_VERIFY_CTX_F_ALLOW_PROXY_CERTIFICATE)
	type = PROXY_CERT;
    else
	type = EE_CERT;

    for (i = 0; i < path.len; i++) {
	Certificate *c;
	time_t t;

	c = _hx509_get_cert(path.val[i]);

	/*
	 * Lets do some basic check on issuer like
	 * keyUsage.keyCertSign and basicConstraints.cA bit depending
	 * on what type of certificate this is.
	 */

	switch (type) {
	case CA_CERT:

	    /* XXX make constants for keyusage */
	    ret = check_key_usage(context, c, 1 << 5,
				  REQUIRE_RFC3280(ctx) ? TRUE : FALSE);
	    if (ret) {
		hx509_set_error_string(context, HX509_ERROR_APPEND, ret,
				       "Key usage missing from CA certificate");
		goto out;
	    }

	    /* self signed cert doesn't add to path length */
	    if (i + 1 != path.len) {
		int selfsigned;

		ret = certificate_is_self_signed(context, c, &selfsigned);
		if (ret)
		    goto out;
		if (selfsigned)
		    selfsigned_depth++;
	    }

	    break;
	case PROXY_CERT: {
	    ProxyCertInfo info;

	    if (is_proxy_cert(context, c, &info) == 0) {
		size_t j;

		if (info.pCPathLenConstraint != NULL &&
		    *info.pCPathLenConstraint < i)
		{
		    free_ProxyCertInfo(&info);
		    ret = HX509_PATH_TOO_LONG;
		    hx509_set_error_string(context, 0, ret,
					   "Proxy certificate chain "
					   "longer then allowed");
		    goto out;
		}
		/* XXX MUST check info.proxyPolicy */
		free_ProxyCertInfo(&info);

		j = 0;
		if (find_extension(c, &asn1_oid_id_x509_ce_subjectAltName, &j)) {
		    ret = HX509_PROXY_CERT_INVALID;
		    hx509_set_error_string(context, 0, ret,
					   "Proxy certificate have explicity "
					   "forbidden subjectAltName");
		    goto out;
		}

		j = 0;
		if (find_extension(c, &asn1_oid_id_x509_ce_issuerAltName, &j)) {
		    ret = HX509_PROXY_CERT_INVALID;
		    hx509_set_error_string(context, 0, ret,
					   "Proxy certificate have explicity "
					   "forbidden issuerAltName");
		    goto out;
		}

		/*
		 * The subject name of the proxy certificate should be
		 * CN=XXX,<proxy issuer>, prune of CN and check if its
		 * the same over the whole chain of proxy certs and
		 * then check with the EE cert when we get to it.
		 */

		if (proxy_cert_depth) {
		    ret = _hx509_name_cmp(&proxy_issuer, &c->tbsCertificate.subject, &diff);
		    if (ret) {
			hx509_set_error_string(context, 0, ret, "Out of memory");
			goto out;
		    }
		    if (diff) {
			ret = HX509_PROXY_CERT_NAME_WRONG;
			hx509_set_error_string(context, 0, ret,
					       "Base proxy name not right");
			goto out;
		    }
		}

		free_Name(&proxy_issuer);

		ret = copy_Name(&c->tbsCertificate.subject, &proxy_issuer);
		if (ret) {
		    hx509_clear_error_string(context);
		    goto out;
		}

		j = proxy_issuer.u.rdnSequence.len;
		if (proxy_issuer.u.rdnSequence.len < 2
		    || proxy_issuer.u.rdnSequence.val[j - 1].len > 1
		    || der_heim_oid_cmp(&proxy_issuer.u.rdnSequence.val[j - 1].val[0].type,
					&asn1_oid_id_at_commonName))
		{
		    ret = HX509_PROXY_CERT_NAME_WRONG;
		    hx509_set_error_string(context, 0, ret,
					   "Proxy name too short or "
					   "does not have Common name "
					   "at the top");
		    goto out;
		}

		free_RelativeDistinguishedName(&proxy_issuer.u.rdnSequence.val[j - 1]);
		proxy_issuer.u.rdnSequence.len -= 1;

		ret = _hx509_name_cmp(&proxy_issuer, &c->tbsCertificate.issuer, &diff);
		if (ret) {
		    hx509_set_error_string(context, 0, ret, "Out of memory");
		    goto out;
		}
		if (diff != 0) {
		    ret = HX509_PROXY_CERT_NAME_WRONG;
		    hx509_set_error_string(context, 0, ret,
					   "Proxy issuer name not as expected");
		    goto out;
		}

		break;
	    } else {
		/*
		 * Now we are done with the proxy certificates, this
		 * cert was an EE cert and we we will fall though to
		 * EE checking below.
		 */
		type = EE_CERT;
		/* FALLTHOUGH */
	    }
	}
	case EE_CERT:
	    /*
	     * If there where any proxy certificates in the chain
	     * (proxy_cert_depth > 0), check that the proxy issuer
	     * matched proxy certificates "base" subject.
	     */
	    if (proxy_cert_depth) {

		ret = _hx509_name_cmp(&proxy_issuer,
				      &c->tbsCertificate.subject, &diff);
		if (ret) {
		    hx509_set_error_string(context, 0, ret, "out of memory");
		    goto out;
		}
		if (diff) {
		    ret = HX509_PROXY_CERT_NAME_WRONG;
		    hx509_clear_error_string(context);
		    goto out;
		}
		if (cert->basename)
		    hx509_name_free(&cert->basename);

		ret = _hx509_name_from_Name(&proxy_issuer, &cert->basename);
		if (ret) {
		    hx509_clear_error_string(context);
		    goto out;
		}
	    }

	    break;
	}

	ret = check_basic_constraints(context, c, type,
				      i - proxy_cert_depth - selfsigned_depth);
	if (ret)
	    goto out;

	/*
	 * Don't check the trust anchors expiration time since they
	 * are transported out of band, from RFC3820.
	 */
	if (i + 1 != path.len || CHECK_TA(ctx)) {

	    t = _hx509_Time2time_t(&c->tbsCertificate.validity.notBefore);
	    if (t > ctx->time_now) {
		ret = HX509_CERT_USED_BEFORE_TIME;
		hx509_clear_error_string(context);
		goto out;
	    }
	    t = _hx509_Time2time_t(&c->tbsCertificate.validity.notAfter);
	    if (t < ctx->time_now) {
		ret = HX509_CERT_USED_AFTER_TIME;
		hx509_clear_error_string(context);
		goto out;
	    }
	}

	if (type == EE_CERT)
	    type = CA_CERT;
	else if (type == PROXY_CERT)
	    proxy_cert_depth++;
    }

    /*
     * Verify constraints, do this backward so path constraints are
     * checked in the right order.
     */

    for (ret = 0, k = path.len; k > 0; k--) {
	Certificate *c;
	int selfsigned;
	i = k - 1;

	c = _hx509_get_cert(path.val[i]);

	ret = certificate_is_self_signed(context, c, &selfsigned);
	if (ret)
	    goto out;

	/* verify name constraints, not for selfsigned and anchor */
	if (!selfsigned || i + 1 != path.len) {
	    ret = check_name_constraints(context, &nc, c);
	    if (ret) {
		goto out;
	    }
	}
	ret = add_name_constraints(context, c, i == 0, &nc);
	if (ret)
	    goto out;

	/* XXX verify all other silly constraints */

    }

    /*
     * Verify that no certificates has been revoked.
     */

    if (ctx->revoke_ctx) {
	hx509_certs certs;

	ret = hx509_certs_init(context, "MEMORY:revoke-certs", 0,
			       NULL, &certs);
	if (ret)
	    goto out;

	for (i = 0; i < path.len; i++) {
	    ret = hx509_certs_add(context, certs, path.val[i]);
	    if (ret) {
		hx509_certs_free(&certs);
		goto out;
	    }
	}
	ret = hx509_certs_merge(context, certs, pool);
	if (ret) {
	    hx509_certs_free(&certs);
	    goto out;
	}

	for (i = 0; i < path.len - 1; i++) {
	    size_t parent = (i < path.len - 1) ? i + 1 : i;

	    ret = hx509_revoke_verify(context,
				      ctx->revoke_ctx,
				      certs,
				      ctx->time_now,
				      path.val[i],
				      path.val[parent]);
	    if (ret) {
		hx509_certs_free(&certs);
		goto out;
	    }
	}
	hx509_certs_free(&certs);
    }

    /*
     * Verify signatures, do this backward so public key working
     * parameter is passed up from the anchor up though the chain.
     */

    for (k = path.len; k > 0; k--) {
	hx509_cert signer;
	Certificate *c;
	i = k - 1;

	c = _hx509_get_cert(path.val[i]);

	/* is last in chain (trust anchor) */
	if (i + 1 == path.len) {
	    int selfsigned;

	    signer = path.val[i];

	    ret = certificate_is_self_signed(context, signer->data, &selfsigned);
	    if (ret)
		goto out;

	    /* if trust anchor is not self signed, don't check sig */
	    if (!selfsigned)
		continue;
	} else {
	    /* take next certificate in chain */
	    signer = path.val[i + 1];
	}

	/* verify signatureValue */
	ret = _hx509_verify_signature_bitstring(context,
						signer,
						&c->signatureAlgorithm,
						&c->tbsCertificate._save,
						&c->signatureValue);
	if (ret) {
	    hx509_set_error_string(context, HX509_ERROR_APPEND, ret,
				   "Failed to verify signature of certificate");
	    goto out;
	}
	/*
	 * Verify that the sigature algorithm "best-before" date is
	 * before the creation date of the certificate, do this for
	 * trust anchors too, since any trust anchor that is created
	 * after a algorithm is known to be bad deserved to be invalid.
	 *
	 * Skip the leaf certificate for now...
	 */

	if (i != 0 && (ctx->flags & HX509_VERIFY_CTX_F_NO_BEST_BEFORE_CHECK) == 0) {
	    time_t notBefore =
		_hx509_Time2time_t(&c->tbsCertificate.validity.notBefore);
	    ret = _hx509_signature_best_before(context,
					       &c->signatureAlgorithm,
					       notBefore);
	    if (ret)
		goto out;
	}
    }

out:
    hx509_certs_free(&anchors);
    free_Name(&proxy_issuer);
    free_name_constraints(&nc);
    _hx509_path_free(&path);

    return ret;
}

/**
 * Verify a signature made using the private key of an certificate.
 *
 * @param context A hx509 context.
 * @param signer the certificate that made the signature.
 * @param alg algorthm that was used to sign the data.
 * @param data the data that was signed.
 * @param sig the sigature to verify.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_crypto
 */

int
hx509_verify_signature(hx509_context context,
		       const hx509_cert signer,
		       const AlgorithmIdentifier *alg,
		       const heim_octet_string *data,
		       const heim_octet_string *sig)
{
    return _hx509_verify_signature(context, signer, alg, data, sig);
}

int
_hx509_verify_signature_bitstring(hx509_context context,
				  const hx509_cert signer,
				  const AlgorithmIdentifier *alg,
				  const heim_octet_string *data,
				  const heim_bit_string *sig)
{
    heim_octet_string os;

    if (sig->length & 7) {
	hx509_set_error_string(context, 0, HX509_CRYPTO_SIG_INVALID_FORMAT,
			       "signature not multiple of 8 bits");
	return HX509_CRYPTO_SIG_INVALID_FORMAT;
    }

    os.data = sig->data;
    os.length = sig->length / 8;

    return _hx509_verify_signature(context, signer, alg, data, &os);
}



/**
 * Verify that the certificate is allowed to be used for the hostname
 * and address.
 *
 * @param context A hx509 context.
 * @param cert the certificate to match with
 * @param flags Flags to modify the behavior:
 * - HX509_VHN_F_ALLOW_NO_MATCH no match is ok
 * @param type type of hostname:
 * - HX509_HN_HOSTNAME for plain hostname.
 * - HX509_HN_DNSSRV for DNS SRV names.
 * @param hostname the hostname to check
 * @param sa address of the host
 * @param sa_size length of address
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_cert
 */

int
hx509_verify_hostname(hx509_context context,
		      const hx509_cert cert,
		      int flags,
		      hx509_hostname_type type,
		      const char *hostname,
		      const struct sockaddr *sa,
		      /* XXX krb5_socklen_t */ int sa_size)
{
    GeneralNames san;
    const Name *name;
    int ret;
    size_t i, j, k;

    if (sa && sa_size <= 0)
	return EINVAL;

    memset(&san, 0, sizeof(san));

    i = 0;
    do {
	ret = find_extension_subject_alt_name(cert->data, &i, &san);
	if (ret == HX509_EXTENSION_NOT_FOUND)
	    break;
	else if (ret != 0)
	    return HX509_PARSING_NAME_FAILED;

	for (j = 0; j < san.len; j++) {
	    switch (san.val[j].element) {
	    case choice_GeneralName_dNSName: {
		heim_printable_string hn;
		hn.data = rk_UNCONST(hostname);
		hn.length = strlen(hostname);

		if (der_printable_string_cmp(&san.val[j].u.dNSName, &hn) == 0) {
		    free_GeneralNames(&san);
		    return 0;
		}
		break;
	    }
	    default:
		break;
	    }
	}
	free_GeneralNames(&san);
    } while (1);

    name = &cert->data->tbsCertificate.subject;

    /* Find first CN= in the name, and try to match the hostname on that */
    for (ret = 0, k = name->u.rdnSequence.len; ret == 0 && k > 0; k--) {
	i = k - 1;
	for (j = 0; ret == 0 && j < name->u.rdnSequence.val[i].len; j++) {
	    AttributeTypeAndValue *n = &name->u.rdnSequence.val[i].val[j];

	    if (der_heim_oid_cmp(&n->type, &asn1_oid_id_at_commonName) == 0) {
		DirectoryString *ds = &n->value;
		switch (ds->element) {
		case choice_DirectoryString_printableString: {
		    heim_printable_string hn;
		    hn.data = rk_UNCONST(hostname);
		    hn.length = strlen(hostname);

		    if (der_printable_string_cmp(&ds->u.printableString, &hn) == 0)
			return 0;
		    break;
		}
		case choice_DirectoryString_ia5String: {
		    heim_ia5_string hn;
		    hn.data = rk_UNCONST(hostname);
		    hn.length = strlen(hostname);

		    if (der_ia5_string_cmp(&ds->u.ia5String, &hn) == 0)
			return 0;
		    break;
		}
		case choice_DirectoryString_utf8String:
		    if (strcasecmp(ds->u.utf8String, hostname) == 0)
			return 0;
		default:
		    break;
		}
		ret = HX509_NAME_CONSTRAINT_ERROR;
	    }
	}
    }

    if ((flags & HX509_VHN_F_ALLOW_NO_MATCH) == 0)
	ret = HX509_NAME_CONSTRAINT_ERROR;

    return ret;
}

int
_hx509_set_cert_attribute(hx509_context context,
			  hx509_cert cert,
			  const heim_oid *oid,
			  const heim_octet_string *attr)
{
    hx509_cert_attribute a;
    void *d;

    if (hx509_cert_get_attribute(cert, oid) != NULL)
	return 0;

    d = realloc(cert->attrs.val,
		sizeof(cert->attrs.val[0]) * (cert->attrs.len + 1));
    if (d == NULL) {
	hx509_clear_error_string(context);
	return ENOMEM;
    }
    cert->attrs.val = d;

    a = malloc(sizeof(*a));
    if (a == NULL)
	return ENOMEM;

    der_copy_octet_string(attr, &a->data);
    der_copy_oid(oid, &a->oid);

    cert->attrs.val[cert->attrs.len] = a;
    cert->attrs.len++;

    return 0;
}

/**
 * Get an external attribute for the certificate, examples are
 * friendly name and id.
 *
 * @param cert hx509 certificate object to search
 * @param oid an oid to search for.
 *
 * @return an hx509_cert_attribute, only valid as long as the
 * certificate is referenced.
 *
 * @ingroup hx509_cert
 */

hx509_cert_attribute
hx509_cert_get_attribute(hx509_cert cert, const heim_oid *oid)
{
    size_t i;
    for (i = 0; i < cert->attrs.len; i++)
	if (der_heim_oid_cmp(oid, &cert->attrs.val[i]->oid) == 0)
	    return cert->attrs.val[i];
    return NULL;
}

/**
 * Set the friendly name on the certificate.
 *
 * @param cert The certificate to set the friendly name on
 * @param name Friendly name.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_cert
 */

int
hx509_cert_set_friendly_name(hx509_cert cert, const char *name)
{
    if (cert->friendlyname)
	free(cert->friendlyname);
    cert->friendlyname = strdup(name);
    if (cert->friendlyname == NULL)
	return ENOMEM;
    return 0;
}

/**
 * Get friendly name of the certificate.
 *
 * @param cert cert to get the friendly name from.
 *
 * @return an friendly name or NULL if there is. The friendly name is
 * only valid as long as the certificate is referenced.
 *
 * @ingroup hx509_cert
 */

const char *
hx509_cert_get_friendly_name(hx509_cert cert)
{
    hx509_cert_attribute a;
    PKCS9_friendlyName n;
    size_t sz;
    int ret;
    size_t i;

    if (cert->friendlyname)
	return cert->friendlyname;

    a = hx509_cert_get_attribute(cert, &asn1_oid_id_pkcs_9_at_friendlyName);
    if (a == NULL) {
	hx509_name name;

	ret = hx509_cert_get_subject(cert, &name);
	if (ret)
	    return NULL;
	ret = hx509_name_to_string(name, &cert->friendlyname);
	hx509_name_free(&name);
	if (ret)
	    return NULL;
	return cert->friendlyname;
    }

    ret = decode_PKCS9_friendlyName(a->data.data, a->data.length, &n, &sz);
    if (ret)
	return NULL;

    if (n.len != 1) {
	free_PKCS9_friendlyName(&n);
	return NULL;
    }

    cert->friendlyname = malloc(n.val[0].length + 1);
    if (cert->friendlyname == NULL) {
	free_PKCS9_friendlyName(&n);
	return NULL;
    }

    for (i = 0; i < n.val[0].length; i++) {
	if (n.val[0].data[i] <= 0xff)
	    cert->friendlyname[i] = n.val[0].data[i] & 0xff;
	else
	    cert->friendlyname[i] = 'X';
    }
    cert->friendlyname[i] = '\0';
    free_PKCS9_friendlyName(&n);

    return cert->friendlyname;
}

void
_hx509_query_clear(hx509_query *q)
{
    memset(q, 0, sizeof(*q));
}

/**
 * Allocate an query controller. Free using hx509_query_free().
 *
 * @param context A hx509 context.
 * @param q return pointer to a hx509_query.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_cert
 */

int
hx509_query_alloc(hx509_context context, hx509_query **q)
{
    *q = calloc(1, sizeof(**q));
    if (*q == NULL)
	return ENOMEM;
    return 0;
}


/**
 * Set match options for the hx509 query controller.
 *
 * @param q query controller.
 * @param option options to control the query controller.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_cert
 */

void
hx509_query_match_option(hx509_query *q, hx509_query_option option)
{
    switch(option) {
    case HX509_QUERY_OPTION_PRIVATE_KEY:
	q->match |= HX509_QUERY_PRIVATE_KEY;
	break;
    case HX509_QUERY_OPTION_KU_ENCIPHERMENT:
	q->match |= HX509_QUERY_KU_ENCIPHERMENT;
	break;
    case HX509_QUERY_OPTION_KU_DIGITALSIGNATURE:
	q->match |= HX509_QUERY_KU_DIGITALSIGNATURE;
	break;
    case HX509_QUERY_OPTION_KU_KEYCERTSIGN:
	q->match |= HX509_QUERY_KU_KEYCERTSIGN;
	break;
    case HX509_QUERY_OPTION_END:
    default:
	break;
    }
}

/**
 * Set the issuer and serial number of match in the query
 * controller. The function make copies of the isser and serial number.
 *
 * @param q a hx509 query controller
 * @param issuer issuer to search for
 * @param serialNumber the serialNumber of the issuer.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_cert
 */

int
hx509_query_match_issuer_serial(hx509_query *q,
				const Name *issuer,
				const heim_integer *serialNumber)
{
    int ret;
    if (q->serial) {
	der_free_heim_integer(q->serial);
	free(q->serial);
    }
    q->serial = malloc(sizeof(*q->serial));
    if (q->serial == NULL)
	return ENOMEM;
    ret = der_copy_heim_integer(serialNumber, q->serial);
    if (ret) {
	free(q->serial);
	q->serial = NULL;
	return ret;
    }
    if (q->issuer_name) {
	free_Name(q->issuer_name);
	free(q->issuer_name);
    }
    q->issuer_name = malloc(sizeof(*q->issuer_name));
    if (q->issuer_name == NULL)
	return ENOMEM;
    ret = copy_Name(issuer, q->issuer_name);
    if (ret) {
	free(q->issuer_name);
	q->issuer_name = NULL;
	return ret;
    }
    q->match |= HX509_QUERY_MATCH_SERIALNUMBER|HX509_QUERY_MATCH_ISSUER_NAME;
    return 0;
}

/**
 * Set the query controller to match on a friendly name
 *
 * @param q a hx509 query controller.
 * @param name a friendly name to match on
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_cert
 */

int
hx509_query_match_friendly_name(hx509_query *q, const char *name)
{
    if (q->friendlyname)
	free(q->friendlyname);
    q->friendlyname = strdup(name);
    if (q->friendlyname == NULL)
	return ENOMEM;
    q->match |= HX509_QUERY_MATCH_FRIENDLY_NAME;
    return 0;
}

/**
 * Set the query controller to require an one specific EKU (extended
 * key usage). Any previous EKU matching is overwitten. If NULL is
 * passed in as the eku, the EKU requirement is reset.
 *
 * @param q a hx509 query controller.
 * @param eku an EKU to match on.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_cert
 */

int
hx509_query_match_eku(hx509_query *q, const heim_oid *eku)
{
    int ret;

    if (eku == NULL) {
	if (q->eku) {
	    der_free_oid(q->eku);
	    free(q->eku);
	    q->eku = NULL;
	}
	q->match &= ~HX509_QUERY_MATCH_EKU;
    } else {
	if (q->eku) {
	    der_free_oid(q->eku);
	} else {
	    q->eku = calloc(1, sizeof(*q->eku));
	    if (q->eku == NULL)
		return ENOMEM;
	}
	ret = der_copy_oid(eku, q->eku);
	if (ret) {
	    free(q->eku);
	    q->eku = NULL;
	    return ret;
	}
	q->match |= HX509_QUERY_MATCH_EKU;
    }
    return 0;
}

int
hx509_query_match_expr(hx509_context context, hx509_query *q, const char *expr)
{
    if (q->expr) {
	_hx509_expr_free(q->expr);
	q->expr = NULL;
    }

    if (expr == NULL) {
	q->match &= ~HX509_QUERY_MATCH_EXPR;
    } else {
	q->expr = _hx509_expr_parse(expr);
	if (q->expr)
	    q->match |= HX509_QUERY_MATCH_EXPR;
    }

    return 0;
}

/**
 * Set the query controller to match using a specific match function.
 *
 * @param q a hx509 query controller.
 * @param func function to use for matching, if the argument is NULL,
 * the match function is removed.
 * @param ctx context passed to the function.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_cert
 */

int
hx509_query_match_cmp_func(hx509_query *q,
			   int (*func)(hx509_context, hx509_cert, void *),
			   void *ctx)
{
    if (func)
	q->match |= HX509_QUERY_MATCH_FUNCTION;
    else
	q->match &= ~HX509_QUERY_MATCH_FUNCTION;
    q->cmp_func = func;
    q->cmp_func_ctx = ctx;
    return 0;
}

/**
 * Free the query controller.
 *
 * @param context A hx509 context.
 * @param q a pointer to the query controller.
 *
 * @ingroup hx509_cert
 */

void
hx509_query_free(hx509_context context, hx509_query *q)
{
    if (q == NULL)
	return;

    if (q->serial) {
	der_free_heim_integer(q->serial);
	free(q->serial);
    }
    if (q->issuer_name) {
	free_Name(q->issuer_name);
	free(q->issuer_name);
    }
    if (q->eku) {
	der_free_oid(q->eku);
	free(q->eku);
    }
    if (q->friendlyname)
	free(q->friendlyname);
    if (q->expr)
	_hx509_expr_free(q->expr);

    memset(q, 0, sizeof(*q));
    free(q);
}

int
_hx509_query_match_cert(hx509_context context, const hx509_query *q, hx509_cert cert)
{
    Certificate *c = _hx509_get_cert(cert);
    int ret, diff;

    _hx509_query_statistic(context, 1, q);

    if ((q->match & HX509_QUERY_FIND_ISSUER_CERT) &&
	_hx509_cert_is_parent_cmp(q->subject, c, 0) != 0)
	return 0;

    if ((q->match & HX509_QUERY_MATCH_CERTIFICATE) &&
	_hx509_Certificate_cmp(q->certificate, c) != 0)
	return 0;

    if ((q->match & HX509_QUERY_MATCH_SERIALNUMBER)
	&& der_heim_integer_cmp(&c->tbsCertificate.serialNumber, q->serial) != 0)
	return 0;

    if (q->match & HX509_QUERY_MATCH_ISSUER_NAME) {
	ret = _hx509_name_cmp(&c->tbsCertificate.issuer, q->issuer_name, &diff);
	if (ret || diff)
	    return 0;
    }

    if (q->match & HX509_QUERY_MATCH_SUBJECT_NAME) {
	ret = _hx509_name_cmp(&c->tbsCertificate.subject, q->subject_name, &diff);
	if (ret || diff)
	    return 0;
    }

    if (q->match & HX509_QUERY_MATCH_SUBJECT_KEY_ID) {
	SubjectKeyIdentifier si;

	ret = _hx509_find_extension_subject_key_id(c, &si);
	if (ret == 0) {
	    if (der_heim_octet_string_cmp(&si, q->subject_id) != 0)
		ret = 1;
	    free_SubjectKeyIdentifier(&si);
	}
	if (ret)
	    return 0;
    }
    if ((q->match & HX509_QUERY_MATCH_ISSUER_ID))
	return 0;
    if ((q->match & HX509_QUERY_PRIVATE_KEY) &&
	_hx509_cert_private_key(cert) == NULL)
	return 0;

    {
	unsigned ku = 0;
	if (q->match & HX509_QUERY_KU_DIGITALSIGNATURE)
	    ku |= (1 << 0);
	if (q->match & HX509_QUERY_KU_NONREPUDIATION)
	    ku |= (1 << 1);
	if (q->match & HX509_QUERY_KU_ENCIPHERMENT)
	    ku |= (1 << 2);
	if (q->match & HX509_QUERY_KU_DATAENCIPHERMENT)
	    ku |= (1 << 3);
	if (q->match & HX509_QUERY_KU_KEYAGREEMENT)
	    ku |= (1 << 4);
	if (q->match & HX509_QUERY_KU_KEYCERTSIGN)
	    ku |= (1 << 5);
	if (q->match & HX509_QUERY_KU_CRLSIGN)
	    ku |= (1 << 6);
	if (ku && check_key_usage(context, c, ku, TRUE))
	    return 0;
    }
    if ((q->match & HX509_QUERY_ANCHOR))
	return 0;

    if (q->match & HX509_QUERY_MATCH_LOCAL_KEY_ID) {
	hx509_cert_attribute a;

	a = hx509_cert_get_attribute(cert, &asn1_oid_id_pkcs_9_at_localKeyId);
	if (a == NULL)
	    return 0;
	if (der_heim_octet_string_cmp(&a->data, q->local_key_id) != 0)
	    return 0;
    }

    if (q->match & HX509_QUERY_NO_MATCH_PATH) {
	size_t i;

	for (i = 0; i < q->path->len; i++)
	    if (hx509_cert_cmp(q->path->val[i], cert) == 0)
		return 0;
    }
    if (q->match & HX509_QUERY_MATCH_FRIENDLY_NAME) {
	const char *name = hx509_cert_get_friendly_name(cert);
	if (name == NULL)
	    return 0;
	if (strcasecmp(q->friendlyname, name) != 0)
	    return 0;
    }
    if (q->match & HX509_QUERY_MATCH_FUNCTION) {
	ret = (*q->cmp_func)(context, cert, q->cmp_func_ctx);
	if (ret != 0)
	    return 0;
    }

    if (q->match & HX509_QUERY_MATCH_KEY_HASH_SHA1) {
	heim_octet_string os;

	os.data = c->tbsCertificate.subjectPublicKeyInfo.subjectPublicKey.data;
	os.length =
	    c->tbsCertificate.subjectPublicKeyInfo.subjectPublicKey.length / 8;

	ret = _hx509_verify_signature(context,
				      NULL,
				      hx509_signature_sha1(),
				      &os,
				      q->keyhash_sha1);
	if (ret != 0)
	    return 0;
    }

    if (q->match & HX509_QUERY_MATCH_TIME) {
	time_t t;
	t = _hx509_Time2time_t(&c->tbsCertificate.validity.notBefore);
	if (t > q->timenow)
	    return 0;
	t = _hx509_Time2time_t(&c->tbsCertificate.validity.notAfter);
	if (t < q->timenow)
	    return 0;
    }

    /* If an EKU is required, check the cert for it. */
    if ((q->match & HX509_QUERY_MATCH_EKU) &&
	hx509_cert_check_eku(context, cert, q->eku, 0))
	return 0;

    if ((q->match & HX509_QUERY_MATCH_EXPR)) {
	hx509_env env = NULL;

	ret = _hx509_cert_to_env(context, cert, &env);
	if (ret)
	    return 0;

	ret = _hx509_expr_eval(context, env, q->expr);
	hx509_env_free(&env);
	if (ret == 0)
	    return 0;
    }

    if (q->match & ~HX509_QUERY_MASK)
	return 0;

    return 1;
}

/**
 * Set a statistic file for the query statistics.
 *
 * @param context A hx509 context.
 * @param fn statistics file name
 *
 * @ingroup hx509_cert
 */

void
hx509_query_statistic_file(hx509_context context, const char *fn)
{
    if (context->querystat)
	free(context->querystat);
    context->querystat = strdup(fn);
}

void
_hx509_query_statistic(hx509_context context, int type, const hx509_query *q)
{
    FILE *f;
    if (context->querystat == NULL)
	return;
    f = fopen(context->querystat, "a");
    if (f == NULL)
	return;
    rk_cloexec_file(f);
    fprintf(f, "%d %d\n", type, q->match);
    fclose(f);
}

static const char *statname[] = {
    "find issuer cert",
    "match serialnumber",
    "match issuer name",
    "match subject name",
    "match subject key id",
    "match issuer id",
    "private key",
    "ku encipherment",
    "ku digitalsignature",
    "ku keycertsign",
    "ku crlsign",
    "ku nonrepudiation",
    "ku keyagreement",
    "ku dataencipherment",
    "anchor",
    "match certificate",
    "match local key id",
    "no match path",
    "match friendly name",
    "match function",
    "match key hash sha1",
    "match time"
};

struct stat_el {
    unsigned long stats;
    unsigned int index;
};


static int
stat_sort(const void *a, const void *b)
{
    const struct stat_el *ae = a;
    const struct stat_el *be = b;
    return be->stats - ae->stats;
}

/**
 * Unparse the statistics file and print the result on a FILE descriptor.
 *
 * @param context A hx509 context.
 * @param printtype tyep to print
 * @param out the FILE to write the data on.
 *
 * @ingroup hx509_cert
 */

void
hx509_query_unparse_stats(hx509_context context, int printtype, FILE *out)
{
    rtbl_t t;
    FILE *f;
    int type, mask, num;
    size_t i;
    unsigned long multiqueries = 0, totalqueries = 0;
    struct stat_el stats[32];

    if (context->querystat == NULL)
	return;
    f = fopen(context->querystat, "r");
    if (f == NULL) {
	fprintf(out, "No statistic file %s: %s.\n",
		context->querystat, strerror(errno));
	return;
    }
    rk_cloexec_file(f);

    for (i = 0; i < sizeof(stats)/sizeof(stats[0]); i++) {
	stats[i].index = i;
	stats[i].stats = 0;
    }

    while (fscanf(f, "%d %d\n", &type, &mask) == 2) {
	if (type != printtype)
	    continue;
	num = i = 0;
	while (mask && i < sizeof(stats)/sizeof(stats[0])) {
	    if (mask & 1) {
		stats[i].stats++;
		num++;
	    }
	    mask = mask >>1 ;
	    i++;
	}
	if (num > 1)
	    multiqueries++;
	totalqueries++;
    }
    fclose(f);

    qsort(stats, sizeof(stats)/sizeof(stats[0]), sizeof(stats[0]), stat_sort);

    t = rtbl_create();
    if (t == NULL)
	errx(1, "out of memory");

    rtbl_set_separator (t, "  ");

    rtbl_add_column_by_id (t, 0, "Name", 0);
    rtbl_add_column_by_id (t, 1, "Counter", 0);


    for (i = 0; i < sizeof(stats)/sizeof(stats[0]); i++) {
	char str[10];

	if (stats[i].index < sizeof(statname)/sizeof(statname[0]))
	    rtbl_add_column_entry_by_id (t, 0, statname[stats[i].index]);
	else {
	    snprintf(str, sizeof(str), "%d", stats[i].index);
	    rtbl_add_column_entry_by_id (t, 0, str);
	}
	snprintf(str, sizeof(str), "%lu", stats[i].stats);
	rtbl_add_column_entry_by_id (t, 1, str);
    }

    rtbl_format(t, out);
    rtbl_destroy(t);

    fprintf(out, "\nQueries: multi %lu total %lu\n",
	    multiqueries, totalqueries);
}

/**
 * Check the extended key usage on the hx509 certificate.
 *
 * @param context A hx509 context.
 * @param cert A hx509 context.
 * @param eku the EKU to check for
 * @param allow_any_eku if the any EKU is set, allow that to be a
 * substitute.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_cert
 */

int
hx509_cert_check_eku(hx509_context context, hx509_cert cert,
		     const heim_oid *eku, int allow_any_eku)
{
    ExtKeyUsage e;
    int ret;
    size_t i;

    ret = find_extension_eku(_hx509_get_cert(cert), &e);
    if (ret) {
	hx509_clear_error_string(context);
	return ret;
    }

    for (i = 0; i < e.len; i++) {
	if (der_heim_oid_cmp(eku, &e.val[i]) == 0) {
	    free_ExtKeyUsage(&e);
	    return 0;
	}
	if (allow_any_eku) {
#if 0
	    if (der_heim_oid_cmp(id_any_eku, &e.val[i]) == 0) {
		free_ExtKeyUsage(&e);
		return 0;
	    }
#endif
	}
    }
    free_ExtKeyUsage(&e);
    hx509_clear_error_string(context);
    return HX509_CERTIFICATE_MISSING_EKU;
}

int
_hx509_cert_get_keyusage(hx509_context context,
			 hx509_cert c,
			 KeyUsage *ku)
{
    Certificate *cert;
    const Extension *e;
    size_t size;
    int ret;
    size_t i = 0;

    memset(ku, 0, sizeof(*ku));

    cert = _hx509_get_cert(c);

    if (_hx509_cert_get_version(cert) < 3)
	return 0;

    e = find_extension(cert, &asn1_oid_id_x509_ce_keyUsage, &i);
    if (e == NULL)
	return HX509_KU_CERT_MISSING;

    ret = decode_KeyUsage(e->extnValue.data, e->extnValue.length, ku, &size);
    if (ret)
	return ret;
    return 0;
}

int
_hx509_cert_get_eku(hx509_context context,
		    hx509_cert cert,
		    ExtKeyUsage *e)
{
    int ret;

    memset(e, 0, sizeof(*e));

    ret = find_extension_eku(_hx509_get_cert(cert), e);
    if (ret && ret != HX509_EXTENSION_NOT_FOUND) {
	hx509_clear_error_string(context);
	return ret;
    }
    return 0;
}

/**
 * Encodes the hx509 certificate as a DER encode binary.
 *
 * @param context A hx509 context.
 * @param c the certificate to encode.
 * @param os the encode certificate, set to NULL, 0 on case of
 * error. Free the os->data with hx509_xfree().
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_cert
 */

int
hx509_cert_binary(hx509_context context, hx509_cert c, heim_octet_string *os)
{
    size_t size;
    int ret;

    os->data = NULL;
    os->length = 0;

    ASN1_MALLOC_ENCODE(Certificate, os->data, os->length,
		       _hx509_get_cert(c), &size, ret);
    if (ret) {
	os->data = NULL;
	os->length = 0;
	return ret;
    }
    if (os->length != size)
	_hx509_abort("internal ASN.1 encoder error");

    return ret;
}

/*
 * Last to avoid lost __attribute__s due to #undef.
 */

#undef __attribute__
#define __attribute__(X)

void
_hx509_abort(const char *fmt, ...)
     __attribute__ ((noreturn, format (printf, 1, 2)))
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
    abort();
}

/**
 * Free a data element allocated in the library.
 *
 * @param ptr data to be freed.
 *
 * @ingroup hx509_misc
 */

void
hx509_xfree(void *ptr)
{
    free(ptr);
}

/**
 *
 */

int
_hx509_cert_to_env(hx509_context context, hx509_cert cert, hx509_env *env)
{
    ExtKeyUsage eku;
    hx509_name name;
    char *buf;
    int ret;
    hx509_env envcert = NULL;

    *env = NULL;

    /* version */
    asprintf(&buf, "%d", _hx509_cert_get_version(_hx509_get_cert(cert)));
    ret = hx509_env_add(context, &envcert, "version", buf);
    free(buf);
    if (ret)
	goto out;

    /* subject */
    ret = hx509_cert_get_subject(cert, &name);
    if (ret)
	goto out;

    ret = hx509_name_to_string(name, &buf);
    if (ret) {
	hx509_name_free(&name);
	goto out;
    }

    ret = hx509_env_add(context, &envcert, "subject", buf);
    hx509_name_free(&name);
    if (ret)
	goto out;

    /* issuer */
    ret = hx509_cert_get_issuer(cert, &name);
    if (ret)
	goto out;

    ret = hx509_name_to_string(name, &buf);
    hx509_name_free(&name);
    if (ret)
	goto out;

    ret = hx509_env_add(context, &envcert, "issuer", buf);
    hx509_xfree(buf);
    if (ret)
	goto out;

    /* eku */

    ret = _hx509_cert_get_eku(context, cert, &eku);
    if (ret == HX509_EXTENSION_NOT_FOUND)
	;
    else if (ret != 0)
	goto out;
    else {
	size_t i;
	hx509_env enveku = NULL;

	for (i = 0; i < eku.len; i++) {

	    ret = der_print_heim_oid(&eku.val[i], '.', &buf);
	    if (ret) {
		free_ExtKeyUsage(&eku);
		hx509_env_free(&enveku);
		goto out;
	    }
	    ret = hx509_env_add(context, &enveku, buf, "oid-name-here");
	    free(buf);
	    if (ret) {
		free_ExtKeyUsage(&eku);
		hx509_env_free(&enveku);
		goto out;
	    }
	}
	free_ExtKeyUsage(&eku);

	ret = hx509_env_add_binding(context, &envcert, "eku", enveku);
	if (ret) {
	    hx509_env_free(&enveku);
	    goto out;
	}
    }

    {
	Certificate *c = _hx509_get_cert(cert);
        heim_octet_string os, sig;
	hx509_env envhash = NULL;

	os.data = c->tbsCertificate.subjectPublicKeyInfo.subjectPublicKey.data;
	os.length =
	  c->tbsCertificate.subjectPublicKeyInfo.subjectPublicKey.length / 8;

	ret = _hx509_create_signature(context,
				      NULL,
				      hx509_signature_sha1(),
				      &os,
				      NULL,
				      &sig);
	if (ret != 0)
	    goto out;

	ret = hex_encode(sig.data, sig.length, &buf);
	der_free_octet_string(&sig);
	if (ret < 0) {
	    ret = ENOMEM;
	    hx509_set_error_string(context, 0, ret,
				   "Out of memory");
	    goto out;
	}

	ret = hx509_env_add(context, &envhash, "sha1", buf);
	free(buf);
	if (ret)
	    goto out;

	ret = hx509_env_add_binding(context, &envcert, "hash", envhash);
	if (ret) {
	  hx509_env_free(&envhash);
	  goto out;
	}
    }

    ret = hx509_env_add_binding(context, env, "certificate", envcert);
    if (ret)
	goto out;

    return 0;

out:
    hx509_env_free(&envcert);
    return ret;
}

/**
 * Print a simple representation of a certificate
 *
 * @param context A hx509 context, can be NULL
 * @param cert certificate to print
 * @param out the stdio output stream, if NULL, stdout is used
 *
 * @return An hx509 error code
 *
 * @ingroup hx509_cert
 */

int
hx509_print_cert(hx509_context context, hx509_cert cert, FILE *out)
{
    hx509_name name;
    char *str;
    int ret;

    if (out == NULL)
	out = stderr;

    ret = hx509_cert_get_issuer(cert, &name);
    if (ret)
	return ret;
    hx509_name_to_string(name, &str);
    hx509_name_free(&name);
    fprintf(out, "    issuer:  \"%s\"\n", str);
    free(str);

    ret = hx509_cert_get_subject(cert, &name);
    if (ret)
	return ret;
    hx509_name_to_string(name, &str);
    hx509_name_free(&name);
    fprintf(out, "    subject: \"%s\"\n", str);
    free(str);

    {
	heim_integer serialNumber;

	ret = hx509_cert_get_serialnumber(cert, &serialNumber);
	if (ret)
	    return ret;
	ret = der_print_hex_heim_integer(&serialNumber, &str);
	if (ret)
	    return ret;
	der_free_heim_integer(&serialNumber);
	fprintf(out, "    serial: %s\n", str);
	free(str);
    }

    printf("    keyusage: ");
    ret = hx509_cert_keyusage_print(context, cert, &str);
    if (ret == 0) {
	fprintf(out, "%s\n", str);
	free(str);
    } else
	fprintf(out, "no");

    return 0;
}
