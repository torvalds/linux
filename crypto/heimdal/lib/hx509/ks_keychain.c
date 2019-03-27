/*
 * Copyright (c) 2007 Kungliga Tekniska Högskolan
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

#ifdef HAVE_FRAMEWORK_SECURITY

#include <Security/Security.h>

/* Missing function decls in pre Leopard */
#ifdef NEED_SECKEYGETCSPHANDLE_PROTO
OSStatus SecKeyGetCSPHandle(SecKeyRef, CSSM_CSP_HANDLE *);
OSStatus SecKeyGetCredentials(SecKeyRef, CSSM_ACL_AUTHORIZATION_TAG,
			      int, const CSSM_ACCESS_CREDENTIALS **);
#define kSecCredentialTypeDefault 0
#define CSSM_SIZE uint32_t
#endif


static int
getAttribute(SecKeychainItemRef itemRef, SecItemAttr item,
	     SecKeychainAttributeList **attrs)
{
    SecKeychainAttributeInfo attrInfo;
    UInt32 attrFormat = 0;
    OSStatus ret;

    *attrs = NULL;

    attrInfo.count = 1;
    attrInfo.tag = &item;
    attrInfo.format = &attrFormat;

    ret = SecKeychainItemCopyAttributesAndData(itemRef, &attrInfo, NULL,
					       attrs, NULL, NULL);
    if (ret)
	return EINVAL;
    return 0;
}


/*
 *
 */

struct kc_rsa {
    SecKeychainItemRef item;
    size_t keysize;
};


static int
kc_rsa_public_encrypt(int flen,
		      const unsigned char *from,
		      unsigned char *to,
		      RSA *rsa,
		      int padding)
{
    return -1;
}

static int
kc_rsa_public_decrypt(int flen,
		      const unsigned char *from,
		      unsigned char *to,
		      RSA *rsa,
		      int padding)
{
    return -1;
}


static int
kc_rsa_private_encrypt(int flen,
		       const unsigned char *from,
		       unsigned char *to,
		       RSA *rsa,
		       int padding)
{
    struct kc_rsa *kc = RSA_get_app_data(rsa);

    CSSM_RETURN cret;
    OSStatus ret;
    const CSSM_ACCESS_CREDENTIALS *creds;
    SecKeyRef privKeyRef = (SecKeyRef)kc->item;
    CSSM_CSP_HANDLE cspHandle;
    const CSSM_KEY *cssmKey;
    CSSM_CC_HANDLE sigHandle = 0;
    CSSM_DATA sig, in;
    int fret = 0;

    if (padding != RSA_PKCS1_PADDING)
	return -1;

    cret = SecKeyGetCSSMKey(privKeyRef, &cssmKey);
    if(cret) abort();

    cret = SecKeyGetCSPHandle(privKeyRef, &cspHandle);
    if(cret) abort();

    ret = SecKeyGetCredentials(privKeyRef, CSSM_ACL_AUTHORIZATION_SIGN,
			       kSecCredentialTypeDefault, &creds);
    if(ret) abort();

    ret = CSSM_CSP_CreateSignatureContext(cspHandle, CSSM_ALGID_RSA,
					  creds, cssmKey, &sigHandle);
    if(ret) abort();

    in.Data = (uint8 *)from;
    in.Length = flen;

    sig.Data = (uint8 *)to;
    sig.Length = kc->keysize;

    cret = CSSM_SignData(sigHandle, &in, 1, CSSM_ALGID_NONE, &sig);
    if(cret) {
	/* cssmErrorString(cret); */
	fret = -1;
    } else
	fret = sig.Length;

    if(sigHandle)
	CSSM_DeleteContext(sigHandle);

    return fret;
}

static int
kc_rsa_private_decrypt(int flen, const unsigned char *from, unsigned char *to,
		       RSA * rsa, int padding)
{
    struct kc_rsa *kc = RSA_get_app_data(rsa);

    CSSM_RETURN cret;
    OSStatus ret;
    const CSSM_ACCESS_CREDENTIALS *creds;
    SecKeyRef privKeyRef = (SecKeyRef)kc->item;
    CSSM_CSP_HANDLE cspHandle;
    const CSSM_KEY *cssmKey;
    CSSM_CC_HANDLE handle = 0;
    CSSM_DATA out, in, rem;
    int fret = 0;
    CSSM_SIZE outlen = 0;
    char remdata[1024];

    if (padding != RSA_PKCS1_PADDING)
	return -1;

    cret = SecKeyGetCSSMKey(privKeyRef, &cssmKey);
    if(cret) abort();

    cret = SecKeyGetCSPHandle(privKeyRef, &cspHandle);
    if(cret) abort();

    ret = SecKeyGetCredentials(privKeyRef, CSSM_ACL_AUTHORIZATION_DECRYPT,
			       kSecCredentialTypeDefault, &creds);
    if(ret) abort();


    ret = CSSM_CSP_CreateAsymmetricContext (cspHandle,
					    CSSM_ALGID_RSA,
					    creds,
					    cssmKey,
					    CSSM_PADDING_PKCS1,
					    &handle);
    if(ret) abort();

    in.Data = (uint8 *)from;
    in.Length = flen;

    out.Data = (uint8 *)to;
    out.Length = kc->keysize;

    rem.Data = (uint8 *)remdata;
    rem.Length = sizeof(remdata);

    cret = CSSM_DecryptData(handle, &in, 1, &out, 1, &outlen, &rem);
    if(cret) {
	/* cssmErrorString(cret); */
	fret = -1;
    } else
	fret = out.Length;

    if(handle)
	CSSM_DeleteContext(handle);

    return fret;
}

static int
kc_rsa_init(RSA *rsa)
{
    return 1;
}

static int
kc_rsa_finish(RSA *rsa)
{
    struct kc_rsa *kc_rsa = RSA_get_app_data(rsa);
    CFRelease(kc_rsa->item);
    memset(kc_rsa, 0, sizeof(*kc_rsa));
    free(kc_rsa);
    return 1;
}

static const RSA_METHOD kc_rsa_pkcs1_method = {
    "hx509 Keychain PKCS#1 RSA",
    kc_rsa_public_encrypt,
    kc_rsa_public_decrypt,
    kc_rsa_private_encrypt,
    kc_rsa_private_decrypt,
    NULL,
    NULL,
    kc_rsa_init,
    kc_rsa_finish,
    0,
    NULL,
    NULL,
    NULL
};

static int
set_private_key(hx509_context context,
		SecKeychainItemRef itemRef,
		hx509_cert cert)
{
    struct kc_rsa *kc;
    hx509_private_key key;
    RSA *rsa;
    int ret;

    ret = hx509_private_key_init(&key, NULL, NULL);
    if (ret)
	return ret;

    kc = calloc(1, sizeof(*kc));
    if (kc == NULL)
	_hx509_abort("out of memory");

    kc->item = itemRef;

    rsa = RSA_new();
    if (rsa == NULL)
	_hx509_abort("out of memory");

    /* Argh, fake modulus since OpenSSL API is on crack */
    {
	SecKeychainAttributeList *attrs = NULL;
	uint32_t size;
	void *data;

	rsa->n = BN_new();
	if (rsa->n == NULL) abort();

	ret = getAttribute(itemRef, kSecKeyKeySizeInBits, &attrs);
	if (ret) abort();

	size = *(uint32_t *)attrs->attr[0].data;
	SecKeychainItemFreeAttributesAndData(attrs, NULL);

	kc->keysize = (size + 7) / 8;

	data = malloc(kc->keysize);
	memset(data, 0xe0, kc->keysize);
	BN_bin2bn(data, kc->keysize, rsa->n);
	free(data);
    }
    rsa->e = NULL;

    RSA_set_method(rsa, &kc_rsa_pkcs1_method);
    ret = RSA_set_app_data(rsa, kc);
    if (ret != 1)
	_hx509_abort("RSA_set_app_data");

    hx509_private_key_assign_rsa(key, rsa);
    _hx509_cert_assign_key(cert, key);

    return 0;
}

/*
 *
 */

struct ks_keychain {
    int anchors;
    SecKeychainRef keychain;
};

static int
keychain_init(hx509_context context,
	      hx509_certs certs, void **data, int flags,
	      const char *residue, hx509_lock lock)
{
    struct ks_keychain *ctx;

    ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
	hx509_clear_error_string(context);
	return ENOMEM;
    }

    if (residue) {
	if (strcasecmp(residue, "system-anchors") == 0) {
	    ctx->anchors = 1;
	} else if (strncasecmp(residue, "FILE:", 5) == 0) {
	    OSStatus ret;

	    ret = SecKeychainOpen(residue + 5, &ctx->keychain);
	    if (ret != noErr) {
		hx509_set_error_string(context, 0, ENOENT,
				       "Failed to open %s", residue);
		return ENOENT;
	    }
	} else {
	    hx509_set_error_string(context, 0, ENOENT,
				   "Unknown subtype %s", residue);
	    return ENOENT;
	}
    }

    *data = ctx;
    return 0;
}

/*
 *
 */

static int
keychain_free(hx509_certs certs, void *data)
{
    struct ks_keychain *ctx = data;
    if (ctx->keychain)
	CFRelease(ctx->keychain);
    memset(ctx, 0, sizeof(*ctx));
    free(ctx);
    return 0;
}

/*
 *
 */

struct iter {
    hx509_certs certs;
    void *cursor;
    SecKeychainSearchRef searchRef;
};

static int
keychain_iter_start(hx509_context context,
		    hx509_certs certs, void *data, void **cursor)
{
    struct ks_keychain *ctx = data;
    struct iter *iter;

    iter = calloc(1, sizeof(*iter));
    if (iter == NULL) {
	hx509_set_error_string(context, 0, ENOMEM, "out of memory");
	return ENOMEM;
    }

    if (ctx->anchors) {
        CFArrayRef anchors;
	int ret;
	int i;

	ret = hx509_certs_init(context, "MEMORY:ks-file-create",
			       0, NULL, &iter->certs);
	if (ret) {
	    free(iter);
	    return ret;
	}

	ret = SecTrustCopyAnchorCertificates(&anchors);
	if (ret != 0) {
	    hx509_certs_free(&iter->certs);
	    free(iter);
	    hx509_set_error_string(context, 0, ENOMEM,
				   "Can't get trust anchors from Keychain");
	    return ENOMEM;
	}
	for (i = 0; i < CFArrayGetCount(anchors); i++) {
	    SecCertificateRef cr;
	    hx509_cert cert;
	    CSSM_DATA cssm;

	    cr = (SecCertificateRef)CFArrayGetValueAtIndex(anchors, i);

	    SecCertificateGetData(cr, &cssm);

	    ret = hx509_cert_init_data(context, cssm.Data, cssm.Length, &cert);
	    if (ret)
		continue;

	    ret = hx509_certs_add(context, iter->certs, cert);
	    hx509_cert_free(cert);
	}
	CFRelease(anchors);
    }

    if (iter->certs) {
	int ret;
	ret = hx509_certs_start_seq(context, iter->certs, &iter->cursor);
	if (ret) {
	    hx509_certs_free(&iter->certs);
	    free(iter);
	    return ret;
	}
    } else {
	OSStatus ret;

	ret = SecKeychainSearchCreateFromAttributes(ctx->keychain,
						    kSecCertificateItemClass,
						    NULL,
						    &iter->searchRef);
	if (ret) {
	    free(iter);
	    hx509_set_error_string(context, 0, ret,
				   "Failed to start search for attributes");
	    return ENOMEM;
	}
    }

    *cursor = iter;
    return 0;
}

/*
 *
 */

static int
keychain_iter(hx509_context context,
	      hx509_certs certs, void *data, void *cursor, hx509_cert *cert)
{
    SecKeychainAttributeList *attrs = NULL;
    SecKeychainAttributeInfo attrInfo;
    UInt32 attrFormat[1] = { 0 };
    SecKeychainItemRef itemRef;
    SecItemAttr item[1];
    struct iter *iter = cursor;
    OSStatus ret;
    UInt32 len;
    void *ptr = NULL;

    if (iter->certs)
	return hx509_certs_next_cert(context, iter->certs, iter->cursor, cert);

    *cert = NULL;

    ret = SecKeychainSearchCopyNext(iter->searchRef, &itemRef);
    if (ret == errSecItemNotFound)
	return 0;
    else if (ret != 0)
	return EINVAL;

    /*
     * Pick out certificate and matching "keyid"
     */

    item[0] = kSecPublicKeyHashItemAttr;

    attrInfo.count = 1;
    attrInfo.tag = item;
    attrInfo.format = attrFormat;

    ret = SecKeychainItemCopyAttributesAndData(itemRef, &attrInfo, NULL,
					       &attrs, &len, &ptr);
    if (ret)
	return EINVAL;

    ret = hx509_cert_init_data(context, ptr, len, cert);
    if (ret)
	goto out;

    /*
     * Find related private key if there is one by looking at
     * kSecPublicKeyHashItemAttr == kSecKeyLabel
     */
    {
	SecKeychainSearchRef search;
	SecKeychainAttribute attrKeyid;
	SecKeychainAttributeList attrList;

	attrKeyid.tag = kSecKeyLabel;
	attrKeyid.length = attrs->attr[0].length;
	attrKeyid.data = attrs->attr[0].data;

	attrList.count = 1;
	attrList.attr = &attrKeyid;

	ret = SecKeychainSearchCreateFromAttributes(NULL,
						    CSSM_DL_DB_RECORD_PRIVATE_KEY,
						    &attrList,
						    &search);
	if (ret) {
	    ret = 0;
	    goto out;
	}

	ret = SecKeychainSearchCopyNext(search, &itemRef);
	CFRelease(search);
	if (ret == errSecItemNotFound) {
	    ret = 0;
	    goto out;
	} else if (ret) {
	    ret = EINVAL;
	    goto out;
	}
	set_private_key(context, itemRef, *cert);
    }

out:
    SecKeychainItemFreeAttributesAndData(attrs, ptr);

    return ret;
}

/*
 *
 */

static int
keychain_iter_end(hx509_context context,
		  hx509_certs certs,
		  void *data,
		  void *cursor)
{
    struct iter *iter = cursor;

    if (iter->certs) {
	hx509_certs_end_seq(context, iter->certs, iter->cursor);
	hx509_certs_free(&iter->certs);
    } else {
	CFRelease(iter->searchRef);
    }

    memset(iter, 0, sizeof(*iter));
    free(iter);
    return 0;
}

/*
 *
 */

struct hx509_keyset_ops keyset_keychain = {
    "KEYCHAIN",
    0,
    keychain_init,
    NULL,
    keychain_free,
    NULL,
    NULL,
    keychain_iter_start,
    keychain_iter,
    keychain_iter_end
};

#endif /* HAVE_FRAMEWORK_SECURITY */

/*
 *
 */

void
_hx509_ks_keychain_register(hx509_context context)
{
#ifdef HAVE_FRAMEWORK_SECURITY
    _hx509_ks_register(context, &keyset_keychain);
#endif
}
