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

struct ks_pkcs12 {
    hx509_certs certs;
    char *fn;
};

typedef int (*collector_func)(hx509_context,
			      struct hx509_collector *,
			      const void *, size_t,
			      const PKCS12_Attributes *);

struct type {
    const heim_oid *oid;
    collector_func func;
};

static void
parse_pkcs12_type(hx509_context, struct hx509_collector *, const heim_oid *,
		  const void *, size_t, const PKCS12_Attributes *);


static const PKCS12_Attribute *
find_attribute(const PKCS12_Attributes *attrs, const heim_oid *oid)
{
    size_t i;
    if (attrs == NULL)
	return NULL;
    for (i = 0; i < attrs->len; i++)
	if (der_heim_oid_cmp(oid, &attrs->val[i].attrId) == 0)
	    return &attrs->val[i];
    return NULL;
}

static int
keyBag_parser(hx509_context context,
	      struct hx509_collector *c,
	      const void *data, size_t length,
	      const PKCS12_Attributes *attrs)
{
    const PKCS12_Attribute *attr;
    PKCS8PrivateKeyInfo ki;
    const heim_octet_string *os = NULL;
    int ret;

    attr = find_attribute(attrs, &asn1_oid_id_pkcs_9_at_localKeyId);
    if (attr)
	os = &attr->attrValues;

    ret = decode_PKCS8PrivateKeyInfo(data, length, &ki, NULL);
    if (ret)
	return ret;

    _hx509_collector_private_key_add(context,
				     c,
				     &ki.privateKeyAlgorithm,
				     NULL,
				     &ki.privateKey,
				     os);
    free_PKCS8PrivateKeyInfo(&ki);
    return 0;
}

static int
ShroudedKeyBag_parser(hx509_context context,
		      struct hx509_collector *c,
		      const void *data, size_t length,
		      const PKCS12_Attributes *attrs)
{
    PKCS8EncryptedPrivateKeyInfo pk;
    heim_octet_string content;
    int ret;

    memset(&pk, 0, sizeof(pk));

    ret = decode_PKCS8EncryptedPrivateKeyInfo(data, length, &pk, NULL);
    if (ret)
	return ret;

    ret = _hx509_pbe_decrypt(context,
			     _hx509_collector_get_lock(c),
			     &pk.encryptionAlgorithm,
			     &pk.encryptedData,
			     &content);
    free_PKCS8EncryptedPrivateKeyInfo(&pk);
    if (ret)
	return ret;

    ret = keyBag_parser(context, c, content.data, content.length, attrs);
    der_free_octet_string(&content);
    return ret;
}

static int
certBag_parser(hx509_context context,
	       struct hx509_collector *c,
	       const void *data, size_t length,
	       const PKCS12_Attributes *attrs)
{
    heim_octet_string os;
    hx509_cert cert;
    PKCS12_CertBag cb;
    int ret;

    ret = decode_PKCS12_CertBag(data, length, &cb, NULL);
    if (ret)
	return ret;

    if (der_heim_oid_cmp(&asn1_oid_id_pkcs_9_at_certTypes_x509, &cb.certType)) {
	free_PKCS12_CertBag(&cb);
	return 0;
    }

    ret = decode_PKCS12_OctetString(cb.certValue.data,
				    cb.certValue.length,
				    &os,
				    NULL);
    free_PKCS12_CertBag(&cb);
    if (ret)
	return ret;

    ret = hx509_cert_init_data(context, os.data, os.length, &cert);
    der_free_octet_string(&os);
    if (ret)
	return ret;

    ret = _hx509_collector_certs_add(context, c, cert);
    if (ret) {
	hx509_cert_free(cert);
	return ret;
    }

    {
	const PKCS12_Attribute *attr;
	const heim_oid *oids[] = {
	    &asn1_oid_id_pkcs_9_at_localKeyId, &asn1_oid_id_pkcs_9_at_friendlyName
	};
	size_t i;

	for  (i = 0; i < sizeof(oids)/sizeof(oids[0]); i++) {
	    const heim_oid *oid = oids[i];
	    attr = find_attribute(attrs, oid);
	    if (attr)
		_hx509_set_cert_attribute(context, cert, oid,
					  &attr->attrValues);
	}
    }

    hx509_cert_free(cert);

    return 0;
}

static int
parse_safe_content(hx509_context context,
		   struct hx509_collector *c,
		   const unsigned char *p, size_t len)
{
    PKCS12_SafeContents sc;
    int ret;
    size_t i;

    memset(&sc, 0, sizeof(sc));

    ret = decode_PKCS12_SafeContents(p, len, &sc, NULL);
    if (ret)
	return ret;

    for (i = 0; i < sc.len ; i++)
	parse_pkcs12_type(context,
			  c,
			  &sc.val[i].bagId,
			  sc.val[i].bagValue.data,
			  sc.val[i].bagValue.length,
			  sc.val[i].bagAttributes);

    free_PKCS12_SafeContents(&sc);
    return 0;
}

static int
safeContent_parser(hx509_context context,
		   struct hx509_collector *c,
		   const void *data, size_t length,
		   const PKCS12_Attributes *attrs)
{
    heim_octet_string os;
    int ret;

    ret = decode_PKCS12_OctetString(data, length, &os, NULL);
    if (ret)
	return ret;
    ret = parse_safe_content(context, c, os.data, os.length);
    der_free_octet_string(&os);
    return ret;
}

static int
encryptedData_parser(hx509_context context,
		     struct hx509_collector *c,
		     const void *data, size_t length,
		     const PKCS12_Attributes *attrs)
{
    heim_octet_string content;
    heim_oid contentType;
    int ret;

    memset(&contentType, 0, sizeof(contentType));

    ret = hx509_cms_decrypt_encrypted(context,
				      _hx509_collector_get_lock(c),
				      data, length,
				      &contentType,
				      &content);
    if (ret)
	return ret;

    if (der_heim_oid_cmp(&contentType, &asn1_oid_id_pkcs7_data) == 0)
	ret = parse_safe_content(context, c, content.data, content.length);

    der_free_octet_string(&content);
    der_free_oid(&contentType);
    return ret;
}

static int
envelopedData_parser(hx509_context context,
		     struct hx509_collector *c,
		     const void *data, size_t length,
		     const PKCS12_Attributes *attrs)
{
    heim_octet_string content;
    heim_oid contentType;
    hx509_lock lock;
    int ret;

    memset(&contentType, 0, sizeof(contentType));

    lock = _hx509_collector_get_lock(c);

    ret = hx509_cms_unenvelope(context,
			       _hx509_lock_unlock_certs(lock),
			       0,
			       data, length,
			       NULL,
			       0,
			       &contentType,
			       &content);
    if (ret) {
	hx509_set_error_string(context, HX509_ERROR_APPEND, ret,
			       "PKCS12 failed to unenvelope");
	return ret;
    }

    if (der_heim_oid_cmp(&contentType, &asn1_oid_id_pkcs7_data) == 0)
	ret = parse_safe_content(context, c, content.data, content.length);

    der_free_octet_string(&content);
    der_free_oid(&contentType);

    return ret;
}


struct type bagtypes[] = {
    { &asn1_oid_id_pkcs12_keyBag, keyBag_parser },
    { &asn1_oid_id_pkcs12_pkcs8ShroudedKeyBag, ShroudedKeyBag_parser },
    { &asn1_oid_id_pkcs12_certBag, certBag_parser },
    { &asn1_oid_id_pkcs7_data, safeContent_parser },
    { &asn1_oid_id_pkcs7_encryptedData, encryptedData_parser },
    { &asn1_oid_id_pkcs7_envelopedData, envelopedData_parser }
};

static void
parse_pkcs12_type(hx509_context context,
		  struct hx509_collector *c,
		  const heim_oid *oid,
		  const void *data, size_t length,
		  const PKCS12_Attributes *attrs)
{
    size_t i;

    for (i = 0; i < sizeof(bagtypes)/sizeof(bagtypes[0]); i++)
	if (der_heim_oid_cmp(bagtypes[i].oid, oid) == 0)
	    (*bagtypes[i].func)(context, c, data, length, attrs);
}

static int
p12_init(hx509_context context,
	 hx509_certs certs, void **data, int flags,
	 const char *residue, hx509_lock lock)
{
    struct ks_pkcs12 *p12;
    size_t len;
    void *buf;
    PKCS12_PFX pfx;
    PKCS12_AuthenticatedSafe as;
    int ret;
    size_t i;
    struct hx509_collector *c;

    *data = NULL;

    if (lock == NULL)
	lock = _hx509_empty_lock;

    ret = _hx509_collector_alloc(context, lock, &c);
    if (ret)
	return ret;

    p12 = calloc(1, sizeof(*p12));
    if (p12 == NULL) {
	ret = ENOMEM;
	hx509_set_error_string(context, 0, ret, "out of memory");
	goto out;
    }

    p12->fn = strdup(residue);
    if (p12->fn == NULL) {
	ret = ENOMEM;
	hx509_set_error_string(context, 0, ret, "out of memory");
	goto out;
    }

    if (flags & HX509_CERTS_CREATE) {
	ret = hx509_certs_init(context, "MEMORY:ks-file-create",
			       0, lock, &p12->certs);
	if (ret == 0)
	    *data = p12;
	goto out;
    }

    ret = rk_undumpdata(residue, &buf, &len);
    if (ret) {
	hx509_clear_error_string(context);
	goto out;
    }

    ret = decode_PKCS12_PFX(buf, len, &pfx, NULL);
    rk_xfree(buf);
    if (ret) {
	hx509_set_error_string(context, 0, ret,
			       "Failed to decode the PFX in %s", residue);
	goto out;
    }

    if (der_heim_oid_cmp(&pfx.authSafe.contentType, &asn1_oid_id_pkcs7_data) != 0) {
	free_PKCS12_PFX(&pfx);
	ret = EINVAL;
	hx509_set_error_string(context, 0, ret,
			       "PKCS PFX isn't a pkcs7-data container");
	goto out;
    }

    if (pfx.authSafe.content == NULL) {
	free_PKCS12_PFX(&pfx);
	ret = EINVAL;
	hx509_set_error_string(context, 0, ret,
			       "PKCS PFX missing data");
	goto out;
    }

    {
	heim_octet_string asdata;

	ret = decode_PKCS12_OctetString(pfx.authSafe.content->data,
					pfx.authSafe.content->length,
					&asdata,
					NULL);
	free_PKCS12_PFX(&pfx);
	if (ret) {
	    hx509_clear_error_string(context);
	    goto out;
	}
	ret = decode_PKCS12_AuthenticatedSafe(asdata.data,
					      asdata.length,
					      &as,
					      NULL);
	der_free_octet_string(&asdata);
	if (ret) {
	    hx509_clear_error_string(context);
	    goto out;
	}
    }

    for (i = 0; i < as.len; i++)
	parse_pkcs12_type(context,
			  c,
			  &as.val[i].contentType,
			  as.val[i].content->data,
			  as.val[i].content->length,
			  NULL);

    free_PKCS12_AuthenticatedSafe(&as);

    ret = _hx509_collector_collect_certs(context, c, &p12->certs);
    if (ret == 0)
	*data = p12;

out:
    _hx509_collector_free(c);

    if (ret && p12) {
	if (p12->fn)
	    free(p12->fn);
	if (p12->certs)
	    hx509_certs_free(&p12->certs);
	free(p12);
    }

    return ret;
}

static int
addBag(hx509_context context,
       PKCS12_AuthenticatedSafe *as,
       const heim_oid *oid,
       void *data,
       size_t length)
{
    void *ptr;
    int ret;

    ptr = realloc(as->val, sizeof(as->val[0]) * (as->len + 1));
    if (ptr == NULL) {
	hx509_set_error_string(context, 0, ENOMEM, "out of memory");
	return ENOMEM;
    }
    as->val = ptr;

    ret = der_copy_oid(oid, &as->val[as->len].contentType);
    if (ret) {
	hx509_set_error_string(context, 0, ret, "out of memory");
	return ret;
    }

    as->val[as->len].content = calloc(1, sizeof(*as->val[0].content));
    if (as->val[as->len].content == NULL) {
	der_free_oid(&as->val[as->len].contentType);
	hx509_set_error_string(context, 0, ENOMEM, "malloc out of memory");
	return ENOMEM;
    }

    as->val[as->len].content->data = data;
    as->val[as->len].content->length = length;

    as->len++;

    return 0;
}

static int
store_func(hx509_context context, void *ctx, hx509_cert c)
{
    PKCS12_AuthenticatedSafe *as = ctx;
    PKCS12_OctetString os;
    PKCS12_CertBag cb;
    size_t size;
    int ret;

    memset(&os, 0, sizeof(os));
    memset(&cb, 0, sizeof(cb));

    os.data = NULL;
    os.length = 0;

    ret = hx509_cert_binary(context, c, &os);
    if (ret)
	return ret;

    ASN1_MALLOC_ENCODE(PKCS12_OctetString,
		       cb.certValue.data,cb.certValue.length,
		       &os, &size, ret);
    free(os.data);
    if (ret)
	goto out;
    ret = der_copy_oid(&asn1_oid_id_pkcs_9_at_certTypes_x509, &cb.certType);
    if (ret) {
	free_PKCS12_CertBag(&cb);
	goto out;
    }
    ASN1_MALLOC_ENCODE(PKCS12_CertBag, os.data, os.length,
		       &cb, &size, ret);
    free_PKCS12_CertBag(&cb);
    if (ret)
	goto out;

    ret = addBag(context, as, &asn1_oid_id_pkcs12_certBag, os.data, os.length);

    if (_hx509_cert_private_key_exportable(c)) {
	hx509_private_key key = _hx509_cert_private_key(c);
	PKCS8PrivateKeyInfo pki;

	memset(&pki, 0, sizeof(pki));

	ret = der_parse_hex_heim_integer("00", &pki.version);
	if (ret)
	    return ret;
	ret = _hx509_private_key_oid(context, key,
				     &pki.privateKeyAlgorithm.algorithm);
	if (ret) {
	    free_PKCS8PrivateKeyInfo(&pki);
	    return ret;
	}
	ret = _hx509_private_key_export(context,
					_hx509_cert_private_key(c),
					HX509_KEY_FORMAT_DER,
					&pki.privateKey);
	if (ret) {
	    free_PKCS8PrivateKeyInfo(&pki);
	    return ret;
	}
	/* set attribute, asn1_oid_id_pkcs_9_at_localKeyId */

	ASN1_MALLOC_ENCODE(PKCS8PrivateKeyInfo, os.data, os.length,
			   &pki, &size, ret);
	free_PKCS8PrivateKeyInfo(&pki);
	if (ret)
	    return ret;

	ret = addBag(context, as, &asn1_oid_id_pkcs12_keyBag, os.data, os.length);
	if (ret)
	    return ret;
    }

out:
    return ret;
}

static int
p12_store(hx509_context context,
	  hx509_certs certs, void *data, int flags, hx509_lock lock)
{
    struct ks_pkcs12 *p12 = data;
    PKCS12_PFX pfx;
    PKCS12_AuthenticatedSafe as;
    PKCS12_OctetString asdata;
    size_t size;
    int ret;

    memset(&as, 0, sizeof(as));
    memset(&pfx, 0, sizeof(pfx));

    ret = hx509_certs_iter_f(context, p12->certs, store_func, &as);
    if (ret)
	goto out;

    ASN1_MALLOC_ENCODE(PKCS12_AuthenticatedSafe, asdata.data, asdata.length,
		       &as, &size, ret);
    free_PKCS12_AuthenticatedSafe(&as);
    if (ret)
	return ret;

    ret = der_parse_hex_heim_integer("03", &pfx.version);
    if (ret) {
	free(asdata.data);
	goto out;
    }

    pfx.authSafe.content = calloc(1, sizeof(*pfx.authSafe.content));

    ASN1_MALLOC_ENCODE(PKCS12_OctetString,
		       pfx.authSafe.content->data,
		       pfx.authSafe.content->length,
		       &asdata, &size, ret);
    free(asdata.data);
    if (ret)
	goto out;

    ret = der_copy_oid(&asn1_oid_id_pkcs7_data, &pfx.authSafe.contentType);
    if (ret)
	goto out;

    ASN1_MALLOC_ENCODE(PKCS12_PFX, asdata.data, asdata.length,
		       &pfx, &size, ret);
    if (ret)
	goto out;

#if 0
    const struct _hx509_password *pw;

    pw = _hx509_lock_get_passwords(lock);
    if (pw != NULL) {
	pfx.macData = calloc(1, sizeof(*pfx.macData));
	if (pfx.macData == NULL) {
	    ret = ENOMEM;
	    hx509_set_error_string(context, 0, ret, "malloc out of memory");
	    return ret;
	}
	if (pfx.macData == NULL) {
	    free(asdata.data);
	    goto out;
	}
    }
    ret = calculate_hash(&aspath, pw, pfx.macData);
#endif

    rk_dumpdata(p12->fn, asdata.data, asdata.length);
    free(asdata.data);

out:
    free_PKCS12_AuthenticatedSafe(&as);
    free_PKCS12_PFX(&pfx);

    return ret;
}


static int
p12_free(hx509_certs certs, void *data)
{
    struct ks_pkcs12 *p12 = data;
    hx509_certs_free(&p12->certs);
    free(p12->fn);
    free(p12);
    return 0;
}

static int
p12_add(hx509_context context, hx509_certs certs, void *data, hx509_cert c)
{
    struct ks_pkcs12 *p12 = data;
    return hx509_certs_add(context, p12->certs, c);
}

static int
p12_iter_start(hx509_context context,
	       hx509_certs certs,
	       void *data,
	       void **cursor)
{
    struct ks_pkcs12 *p12 = data;
    return hx509_certs_start_seq(context, p12->certs, cursor);
}

static int
p12_iter(hx509_context context,
	 hx509_certs certs,
	 void *data,
	 void *cursor,
	 hx509_cert *cert)
{
    struct ks_pkcs12 *p12 = data;
    return hx509_certs_next_cert(context, p12->certs, cursor, cert);
}

static int
p12_iter_end(hx509_context context,
	     hx509_certs certs,
	     void *data,
	     void *cursor)
{
    struct ks_pkcs12 *p12 = data;
    return hx509_certs_end_seq(context, p12->certs, cursor);
}

static struct hx509_keyset_ops keyset_pkcs12 = {
    "PKCS12",
    0,
    p12_init,
    p12_store,
    p12_free,
    p12_add,
    NULL,
    p12_iter_start,
    p12_iter,
    p12_iter_end
};

void
_hx509_ks_pkcs12_register(hx509_context context)
{
    _hx509_ks_register(context, &keyset_pkcs12);
}
