/*
 * Copyright (c) 2004 - 2008 Kungliga Tekniska Högskolan
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
#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif

#ifdef HAVE_DLOPEN

#include "pkcs11.h"

struct p11_slot {
    int flags;
#define P11_SESSION		1
#define P11_SESSION_IN_USE	2
#define P11_LOGIN_REQ		4
#define P11_LOGIN_DONE		8
#define P11_TOKEN_PRESENT	16
    CK_SESSION_HANDLE session;
    CK_SLOT_ID id;
    CK_BBOOL token;
    char *name;
    hx509_certs certs;
    char *pin;
    struct {
	CK_MECHANISM_TYPE_PTR list;
	CK_ULONG num;
	CK_MECHANISM_INFO_PTR *infos;
    } mechs;
};

struct p11_module {
    void *dl_handle;
    CK_FUNCTION_LIST_PTR funcs;
    CK_ULONG num_slots;
    unsigned int ref;
    struct p11_slot *slot;
};

#define P11FUNC(module,f,args) (*(module)->funcs->C_##f)args

static int p11_get_session(hx509_context,
			   struct p11_module *,
			   struct p11_slot *,
			   hx509_lock,
			   CK_SESSION_HANDLE *);
static int p11_put_session(struct p11_module *,
			   struct p11_slot *,
			   CK_SESSION_HANDLE);
static void p11_release_module(struct p11_module *);

static int p11_list_keys(hx509_context,
			 struct p11_module *,
			 struct p11_slot *,
			 CK_SESSION_HANDLE,
			 hx509_lock,
			 hx509_certs *);

/*
 *
 */

struct p11_rsa {
    struct p11_module *p;
    struct p11_slot *slot;
    CK_OBJECT_HANDLE private_key;
    CK_OBJECT_HANDLE public_key;
};

static int
p11_rsa_public_encrypt(int flen,
		       const unsigned char *from,
		       unsigned char *to,
		       RSA *rsa,
		       int padding)
{
    return -1;
}

static int
p11_rsa_public_decrypt(int flen,
		       const unsigned char *from,
		       unsigned char *to,
		       RSA *rsa,
		       int padding)
{
    return -1;
}


static int
p11_rsa_private_encrypt(int flen,
			const unsigned char *from,
			unsigned char *to,
			RSA *rsa,
			int padding)
{
    struct p11_rsa *p11rsa = RSA_get_app_data(rsa);
    CK_OBJECT_HANDLE key = p11rsa->private_key;
    CK_SESSION_HANDLE session;
    CK_MECHANISM mechanism;
    CK_ULONG ck_sigsize;
    int ret;

    if (padding != RSA_PKCS1_PADDING)
	return -1;

    memset(&mechanism, 0, sizeof(mechanism));
    mechanism.mechanism = CKM_RSA_PKCS;

    ck_sigsize = RSA_size(rsa);

    ret = p11_get_session(NULL, p11rsa->p, p11rsa->slot, NULL, &session);
    if (ret)
	return -1;

    ret = P11FUNC(p11rsa->p, SignInit, (session, &mechanism, key));
    if (ret != CKR_OK) {
	p11_put_session(p11rsa->p, p11rsa->slot, session);
	return -1;
    }

    ret = P11FUNC(p11rsa->p, Sign,
		  (session, (CK_BYTE *)(intptr_t)from, flen, to, &ck_sigsize));
    p11_put_session(p11rsa->p, p11rsa->slot, session);
    if (ret != CKR_OK)
	return -1;

    return ck_sigsize;
}

static int
p11_rsa_private_decrypt(int flen, const unsigned char *from, unsigned char *to,
			RSA * rsa, int padding)
{
    struct p11_rsa *p11rsa = RSA_get_app_data(rsa);
    CK_OBJECT_HANDLE key = p11rsa->private_key;
    CK_SESSION_HANDLE session;
    CK_MECHANISM mechanism;
    CK_ULONG ck_sigsize;
    int ret;

    if (padding != RSA_PKCS1_PADDING)
	return -1;

    memset(&mechanism, 0, sizeof(mechanism));
    mechanism.mechanism = CKM_RSA_PKCS;

    ck_sigsize = RSA_size(rsa);

    ret = p11_get_session(NULL, p11rsa->p, p11rsa->slot, NULL, &session);
    if (ret)
	return -1;

    ret = P11FUNC(p11rsa->p, DecryptInit, (session, &mechanism, key));
    if (ret != CKR_OK) {
	p11_put_session(p11rsa->p, p11rsa->slot, session);
	return -1;
    }

    ret = P11FUNC(p11rsa->p, Decrypt,
		  (session, (CK_BYTE *)(intptr_t)from, flen, to, &ck_sigsize));
    p11_put_session(p11rsa->p, p11rsa->slot, session);
    if (ret != CKR_OK)
	return -1;

    return ck_sigsize;
}

static int
p11_rsa_init(RSA *rsa)
{
    return 1;
}

static int
p11_rsa_finish(RSA *rsa)
{
    struct p11_rsa *p11rsa = RSA_get_app_data(rsa);
    p11_release_module(p11rsa->p);
    free(p11rsa);
    return 1;
}

static const RSA_METHOD *
get_p11_rsa_pkcs1_method(void)
{
    static const RSA_METHOD *p11_rsa_pkcs1_method;
    RSA_METHOD *new_method;

    if (p11_rsa_pkcs1_method != NULL)
	return p11_rsa_pkcs1_method;

    new_method = RSA_meth_new("hx509 PKCS11 PKCS#1 RSA", 0);
    if (new_method == NULL)
	return NULL;

    if (RSA_meth_set_pub_enc(new_method, p11_rsa_public_encrypt) != 1)
	goto out;

    if (RSA_meth_set_pub_dec(new_method, p11_rsa_public_decrypt) != 1)
	goto out;

    if (RSA_meth_set_priv_enc(new_method, p11_rsa_private_encrypt) != 1)
	goto out;

    if (RSA_meth_set_priv_dec(new_method, p11_rsa_private_decrypt) != 1)
	goto out;

    if (RSA_meth_set_init(new_method, p11_rsa_init) != 1)
	goto out;

    if (RSA_meth_set_finish(new_method, p11_rsa_finish) != 1)
	goto out;

    /*
     * This might overwrite a previously-created method if multiple
     * threads invoke this concurrently which will leak memory.
     */
    p11_rsa_pkcs1_method = new_method;
    return p11_rsa_pkcs1_method;
out:
    RSA_meth_free(new_method);
    return NULL;
}

/*
 *
 */

static int
p11_mech_info(hx509_context context,
	      struct p11_module *p,
	      struct p11_slot *slot,
	      int num)
{
    CK_ULONG i;
    int ret;

    ret = P11FUNC(p, GetMechanismList, (slot->id, NULL_PTR, &i));
    if (ret) {
	hx509_set_error_string(context, 0, HX509_PKCS11_NO_MECH,
			       "Failed to get mech list count for slot %d",
			       num);
	return HX509_PKCS11_NO_MECH;
    }
    if (i == 0) {
	hx509_set_error_string(context, 0, HX509_PKCS11_NO_MECH,
			       "no mech supported for slot %d", num);
	return HX509_PKCS11_NO_MECH;
    }
    slot->mechs.list = calloc(i, sizeof(slot->mechs.list[0]));
    if (slot->mechs.list == NULL) {
	hx509_set_error_string(context, 0, ENOMEM,
			       "out of memory");
	return ENOMEM;
    }
    slot->mechs.num = i;
    ret = P11FUNC(p, GetMechanismList, (slot->id, slot->mechs.list, &i));
    if (ret) {
	hx509_set_error_string(context, 0, HX509_PKCS11_NO_MECH,
			       "Failed to get mech list for slot %d",
			       num);
	return HX509_PKCS11_NO_MECH;
    }
    assert(i == slot->mechs.num);

    slot->mechs.infos = calloc(i, sizeof(*slot->mechs.infos));
    if (slot->mechs.list == NULL) {
	hx509_set_error_string(context, 0, ENOMEM,
			       "out of memory");
	return ENOMEM;
    }

    for (i = 0; i < slot->mechs.num; i++) {
	slot->mechs.infos[i] = calloc(1, sizeof(*(slot->mechs.infos[0])));
	if (slot->mechs.infos[i] == NULL) {
	    hx509_set_error_string(context, 0, ENOMEM,
				   "out of memory");
	    return ENOMEM;
	}
	ret = P11FUNC(p, GetMechanismInfo, (slot->id, slot->mechs.list[i],
					    slot->mechs.infos[i]));
	if (ret) {
	    hx509_set_error_string(context, 0, HX509_PKCS11_NO_MECH,
				   "Failed to get mech info for slot %d",
				   num);
	    return HX509_PKCS11_NO_MECH;
	}
    }

    return 0;
}

static int
p11_init_slot(hx509_context context,
	      struct p11_module *p,
	      hx509_lock lock,
	      CK_SLOT_ID id,
	      int num,
	      struct p11_slot *slot)
{
    CK_SESSION_HANDLE session;
    CK_SLOT_INFO slot_info;
    CK_TOKEN_INFO token_info;
    size_t i;
    int ret;

    slot->certs = NULL;
    slot->id = id;

    ret = P11FUNC(p, GetSlotInfo, (slot->id, &slot_info));
    if (ret) {
	hx509_set_error_string(context, 0, HX509_PKCS11_TOKEN_CONFUSED,
			       "Failed to init PKCS11 slot %d",
			       num);
	return HX509_PKCS11_TOKEN_CONFUSED;
    }

    for (i = sizeof(slot_info.slotDescription) - 1; i > 0; i--) {
	char c = slot_info.slotDescription[i];
	if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\0')
	    continue;
	i++;
	break;
    }

    asprintf(&slot->name, "%.*s",
	     (int)i, slot_info.slotDescription);

    if ((slot_info.flags & CKF_TOKEN_PRESENT) == 0)
	return 0;

    ret = P11FUNC(p, GetTokenInfo, (slot->id, &token_info));
    if (ret) {
	hx509_set_error_string(context, 0, HX509_PKCS11_NO_TOKEN,
			       "Failed to init PKCS11 slot %d "
			       "with error 0x08x",
			       num, ret);
	return HX509_PKCS11_NO_TOKEN;
    }
    slot->flags |= P11_TOKEN_PRESENT;

    if (token_info.flags & CKF_LOGIN_REQUIRED)
	slot->flags |= P11_LOGIN_REQ;

    ret = p11_get_session(context, p, slot, lock, &session);
    if (ret)
	return ret;

    ret = p11_mech_info(context, p, slot, num);
    if (ret)
	goto out;

    ret = p11_list_keys(context, p, slot, session, lock, &slot->certs);
 out:
    p11_put_session(p, slot, session);

    return ret;
}

static int
p11_get_session(hx509_context context,
		struct p11_module *p,
		struct p11_slot *slot,
		hx509_lock lock,
		CK_SESSION_HANDLE *psession)
{
    CK_RV ret;

    if (slot->flags & P11_SESSION_IN_USE)
	_hx509_abort("slot already in session");

    if (slot->flags & P11_SESSION) {
	slot->flags |= P11_SESSION_IN_USE;
	*psession = slot->session;
	return 0;
    }

    ret = P11FUNC(p, OpenSession, (slot->id,
				   CKF_SERIAL_SESSION,
				   NULL,
				   NULL,
				   &slot->session));
    if (ret != CKR_OK) {
	if (context)
	    hx509_set_error_string(context, 0, HX509_PKCS11_OPEN_SESSION,
				   "Failed to OpenSession for slot id %d "
				   "with error: 0x%08x",
				   (int)slot->id, ret);
	return HX509_PKCS11_OPEN_SESSION;
    }

    slot->flags |= P11_SESSION;

    /*
     * If we have have to login, and haven't tried before and have a
     * prompter or known to work pin code.
     *
     * This code is very conversative and only uses the prompter in
     * the hx509_lock, the reason is that it's bad to try many
     * passwords on a pkcs11 token, it might lock up and have to be
     * unlocked by a administrator.
     *
     * XXX try harder to not use pin several times on the same card.
     */

    if (   (slot->flags & P11_LOGIN_REQ)
	&& (slot->flags & P11_LOGIN_DONE) == 0
	&& (lock || slot->pin))
    {
	hx509_prompt prompt;
	char pin[20];
	char *str;

	if (slot->pin == NULL) {

	    memset(&prompt, 0, sizeof(prompt));

	    asprintf(&str, "PIN code for %s: ", slot->name);
	    prompt.prompt = str;
	    prompt.type = HX509_PROMPT_TYPE_PASSWORD;
	    prompt.reply.data = pin;
	    prompt.reply.length = sizeof(pin);

	    ret = hx509_lock_prompt(lock, &prompt);
	    if (ret) {
		free(str);
		if (context)
		    hx509_set_error_string(context, 0, ret,
					   "Failed to get pin code for slot "
					   "id %d with error: %d",
					   (int)slot->id, ret);
		return ret;
	    }
	    free(str);
	} else {
	    strlcpy(pin, slot->pin, sizeof(pin));
	}

	ret = P11FUNC(p, Login, (slot->session, CKU_USER,
				 (unsigned char*)pin, strlen(pin)));
	if (ret != CKR_OK) {
	    if (context)
		hx509_set_error_string(context, 0, HX509_PKCS11_LOGIN,
				       "Failed to login on slot id %d "
				       "with error: 0x%08x",
				       (int)slot->id, ret);
	    return HX509_PKCS11_LOGIN;
	} else
	    slot->flags |= P11_LOGIN_DONE;

	if (slot->pin == NULL) {
	    slot->pin = strdup(pin);
	    if (slot->pin == NULL) {
		if (context)
		    hx509_set_error_string(context, 0, ENOMEM,
					   "out of memory");
		return ENOMEM;
	    }
	}
    } else
	slot->flags |= P11_LOGIN_DONE;

    slot->flags |= P11_SESSION_IN_USE;

    *psession = slot->session;

    return 0;
}

static int
p11_put_session(struct p11_module *p,
		struct p11_slot *slot,
		CK_SESSION_HANDLE session)
{
    if ((slot->flags & P11_SESSION_IN_USE) == 0)
	_hx509_abort("slot not in session");
    slot->flags &= ~P11_SESSION_IN_USE;

    return 0;
}

static int
iterate_entries(hx509_context context,
		struct p11_module *p, struct p11_slot *slot,
		CK_SESSION_HANDLE session,
		CK_ATTRIBUTE *search_data, int num_search_data,
		CK_ATTRIBUTE *query, int num_query,
		int (*func)(hx509_context,
			    struct p11_module *, struct p11_slot *,
			    CK_SESSION_HANDLE session,
			    CK_OBJECT_HANDLE object,
			    void *, CK_ATTRIBUTE *, int), void *ptr)
{
    CK_OBJECT_HANDLE object;
    CK_ULONG object_count;
    int ret, ret2, i;

    ret = P11FUNC(p, FindObjectsInit, (session, search_data, num_search_data));
    if (ret != CKR_OK) {
	return -1;
    }
    while (1) {
	ret = P11FUNC(p, FindObjects, (session, &object, 1, &object_count));
	if (ret != CKR_OK) {
	    return -1;
	}
	if (object_count == 0)
	    break;

	for (i = 0; i < num_query; i++)
	    query[i].pValue = NULL;

	ret = P11FUNC(p, GetAttributeValue,
		      (session, object, query, num_query));
	if (ret != CKR_OK) {
	    return -1;
	}
	for (i = 0; i < num_query; i++) {
	    query[i].pValue = malloc(query[i].ulValueLen);
	    if (query[i].pValue == NULL) {
		ret = ENOMEM;
		goto out;
	    }
	}
	ret = P11FUNC(p, GetAttributeValue,
		      (session, object, query, num_query));
	if (ret != CKR_OK) {
	    ret = -1;
	    goto out;
	}

	ret = (*func)(context, p, slot, session, object, ptr, query, num_query);
	if (ret)
	    goto out;

	for (i = 0; i < num_query; i++) {
	    if (query[i].pValue)
		free(query[i].pValue);
	    query[i].pValue = NULL;
	}
    }
 out:

    for (i = 0; i < num_query; i++) {
	if (query[i].pValue)
	    free(query[i].pValue);
	query[i].pValue = NULL;
    }

    ret2 = P11FUNC(p, FindObjectsFinal, (session));
    if (ret2 != CKR_OK) {
	return ret2;
    }

    return ret;
}

static BIGNUM *
getattr_bn(struct p11_module *p,
	   struct p11_slot *slot,
	   CK_SESSION_HANDLE session,
	   CK_OBJECT_HANDLE object,
	   unsigned int type)
{
    CK_ATTRIBUTE query;
    BIGNUM *bn;
    int ret;

    query.type = type;
    query.pValue = NULL;
    query.ulValueLen = 0;

    ret = P11FUNC(p, GetAttributeValue,
		  (session, object, &query, 1));
    if (ret != CKR_OK)
	return NULL;

    query.pValue = malloc(query.ulValueLen);

    ret = P11FUNC(p, GetAttributeValue,
		  (session, object, &query, 1));
    if (ret != CKR_OK) {
	free(query.pValue);
	return NULL;
    }
    bn = BN_bin2bn(query.pValue, query.ulValueLen, NULL);
    free(query.pValue);

    return bn;
}

static int
collect_private_key(hx509_context context,
		    struct p11_module *p, struct p11_slot *slot,
		    CK_SESSION_HANDLE session,
		    CK_OBJECT_HANDLE object,
		    void *ptr, CK_ATTRIBUTE *query, int num_query)
{
    struct hx509_collector *collector = ptr;
    hx509_private_key key;
    heim_octet_string localKeyId;
    int ret;
    const RSA_METHOD *meth;
    BIGNUM *n, *e;
    RSA *rsa;
    struct p11_rsa *p11rsa;

    localKeyId.data = query[0].pValue;
    localKeyId.length = query[0].ulValueLen;

    ret = hx509_private_key_init(&key, NULL, NULL);
    if (ret)
	return ret;

    rsa = RSA_new();
    if (rsa == NULL)
	_hx509_abort("out of memory");

    /*
     * The exponent and modulus should always be present according to
     * the pkcs11 specification, but some smartcards leaves it out,
     * let ignore any failure to fetch it.
     */
    n = getattr_bn(p, slot, session, object, CKA_MODULUS);
    e = getattr_bn(p, slot, session, object, CKA_PUBLIC_EXPONENT);
    if (RSA_set0_key(rsa, n, e, NULL) != 1) {
	BN_free(n);
	BN_free(e);
	RSA_free(rsa);
	hx509_private_key_free(&key);
	return EINVAL;
    }

    p11rsa = calloc(1, sizeof(*p11rsa));
    if (p11rsa == NULL)
	_hx509_abort("out of memory");

    p11rsa->p = p;
    p11rsa->slot = slot;
    p11rsa->private_key = object;

    if (p->ref == 0)
	_hx509_abort("pkcs11 ref == 0 on alloc");
    p->ref++;
    if (p->ref == UINT_MAX)
	_hx509_abort("pkcs11 ref == UINT_MAX on alloc");

    meth = get_p11_rsa_pkcs1_method();
    if (meth == NULL)
	_hx509_abort("failed to create RSA method");
    RSA_set_method(rsa, meth);
    ret = RSA_set_app_data(rsa, p11rsa);
    if (ret != 1)
	_hx509_abort("RSA_set_app_data");

    hx509_private_key_assign_rsa(key, rsa);

    ret = _hx509_collector_private_key_add(context,
					   collector,
					   hx509_signature_rsa(),
					   key,
					   NULL,
					   &localKeyId);

    if (ret) {
	hx509_private_key_free(&key);
	return ret;
    }
    return 0;
}

static void
p11_cert_release(hx509_cert cert, void *ctx)
{
    struct p11_module *p = ctx;
    p11_release_module(p);
}


static int
collect_cert(hx509_context context,
	     struct p11_module *p, struct p11_slot *slot,
	     CK_SESSION_HANDLE session,
	     CK_OBJECT_HANDLE object,
	     void *ptr, CK_ATTRIBUTE *query, int num_query)
{
    struct hx509_collector *collector = ptr;
    hx509_cert cert;
    int ret;

    if ((CK_LONG)query[0].ulValueLen == -1 ||
	(CK_LONG)query[1].ulValueLen == -1)
    {
	return 0;
    }

    ret = hx509_cert_init_data(context, query[1].pValue,
			       query[1].ulValueLen, &cert);
    if (ret)
	return ret;

    if (p->ref == 0)
	_hx509_abort("pkcs11 ref == 0 on alloc");
    p->ref++;
    if (p->ref == UINT_MAX)
	_hx509_abort("pkcs11 ref to high");

    _hx509_cert_set_release(cert, p11_cert_release, p);

    {
	heim_octet_string data;

	data.data = query[0].pValue;
	data.length = query[0].ulValueLen;

	_hx509_set_cert_attribute(context,
				  cert,
				  &asn1_oid_id_pkcs_9_at_localKeyId,
				  &data);
    }

    if ((CK_LONG)query[2].ulValueLen != -1) {
	char *str;

	asprintf(&str, "%.*s",
		 (int)query[2].ulValueLen, (char *)query[2].pValue);
	if (str) {
	    hx509_cert_set_friendly_name(cert, str);
	    free(str);
	}
    }

    ret = _hx509_collector_certs_add(context, collector, cert);
    hx509_cert_free(cert);

    return ret;
}


static int
p11_list_keys(hx509_context context,
	      struct p11_module *p,
	      struct p11_slot *slot,
	      CK_SESSION_HANDLE session,
	      hx509_lock lock,
	      hx509_certs *certs)
{
    struct hx509_collector *collector;
    CK_OBJECT_CLASS key_class;
    CK_ATTRIBUTE search_data[] = {
	{CKA_CLASS, NULL, 0},
    };
    CK_ATTRIBUTE query_data[3] = {
	{CKA_ID, NULL, 0},
	{CKA_VALUE, NULL, 0},
	{CKA_LABEL, NULL, 0}
    };
    int ret;

    search_data[0].pValue = &key_class;
    search_data[0].ulValueLen = sizeof(key_class);

    if (lock == NULL)
	lock = _hx509_empty_lock;

    ret = _hx509_collector_alloc(context, lock, &collector);
    if (ret)
	return ret;

    key_class = CKO_PRIVATE_KEY;
    ret = iterate_entries(context, p, slot, session,
			  search_data, 1,
			  query_data, 1,
			  collect_private_key, collector);
    if (ret)
	goto out;

    key_class = CKO_CERTIFICATE;
    ret = iterate_entries(context, p, slot, session,
			  search_data, 1,
			  query_data, 3,
			  collect_cert, collector);
    if (ret)
	goto out;

    ret = _hx509_collector_collect_certs(context, collector, &slot->certs);

out:
    _hx509_collector_free(collector);

    return ret;
}


static int
p11_init(hx509_context context,
	 hx509_certs certs, void **data, int flags,
	 const char *residue, hx509_lock lock)
{
    CK_C_GetFunctionList getFuncs;
    struct p11_module *p;
    char *list, *str;
    int ret;

    *data = NULL;

    list = strdup(residue);
    if (list == NULL)
	return ENOMEM;

    p = calloc(1, sizeof(*p));
    if (p == NULL) {
	free(list);
	return ENOMEM;
    }

    p->ref = 1;

    str = strchr(list, ',');
    if (str)
	*str++ = '\0';
    while (str) {
	char *strnext;
	strnext = strchr(str, ',');
	if (strnext)
	    *strnext++ = '\0';
#if 0
	if (strncasecmp(str, "slot=", 5) == 0)
	    p->selected_slot = atoi(str + 5);
#endif
	str = strnext;
    }

    p->dl_handle = dlopen(list, RTLD_NOW);
    free(list);
    if (p->dl_handle == NULL) {
	ret = HX509_PKCS11_LOAD;
	hx509_set_error_string(context, 0, ret,
			       "Failed to open %s: %s", list, dlerror());
	goto out;
    }

    getFuncs = (CK_C_GetFunctionList) dlsym(p->dl_handle, "C_GetFunctionList");
    if (getFuncs == NULL) {
	ret = HX509_PKCS11_LOAD;
	hx509_set_error_string(context, 0, ret,
			       "C_GetFunctionList missing in %s: %s",
			       list, dlerror());
	goto out;
    }

    ret = (*getFuncs)(&p->funcs);
    if (ret) {
	ret = HX509_PKCS11_LOAD;
	hx509_set_error_string(context, 0, ret,
			       "C_GetFunctionList failed in %s", list);
	goto out;
    }

    ret = P11FUNC(p, Initialize, (NULL_PTR));
    if (ret != CKR_OK) {
	ret = HX509_PKCS11_TOKEN_CONFUSED;
	hx509_set_error_string(context, 0, ret,
			       "Failed initialize the PKCS11 module");
	goto out;
    }

    ret = P11FUNC(p, GetSlotList, (FALSE, NULL, &p->num_slots));
    if (ret) {
	ret = HX509_PKCS11_TOKEN_CONFUSED;
	hx509_set_error_string(context, 0, ret,
			       "Failed to get number of PKCS11 slots");
	goto out;
    }

   if (p->num_slots == 0) {
	ret = HX509_PKCS11_NO_SLOT;
	hx509_set_error_string(context, 0, ret,
			       "Selected PKCS11 module have no slots");
	goto out;
   }


    {
	CK_SLOT_ID_PTR slot_ids;
	int num_tokens = 0;
	size_t i;

	slot_ids = malloc(p->num_slots * sizeof(*slot_ids));
	if (slot_ids == NULL) {
	    hx509_clear_error_string(context);
	    ret = ENOMEM;
	    goto out;
	}

	ret = P11FUNC(p, GetSlotList, (FALSE, slot_ids, &p->num_slots));
	if (ret) {
	    free(slot_ids);
	    hx509_set_error_string(context, 0, HX509_PKCS11_TOKEN_CONFUSED,
				   "Failed getting slot-list from "
				   "PKCS11 module");
	    ret = HX509_PKCS11_TOKEN_CONFUSED;
	    goto out;
	}

	p->slot = calloc(p->num_slots, sizeof(p->slot[0]));
	if (p->slot == NULL) {
	    free(slot_ids);
	    hx509_set_error_string(context, 0, ENOMEM,
				   "Failed to get memory for slot-list");
	    ret = ENOMEM;
	    goto out;
	}

	for (i = 0; i < p->num_slots; i++) {
	    ret = p11_init_slot(context, p, lock, slot_ids[i], i, &p->slot[i]);
	    if (ret)
		break;
	    if (p->slot[i].flags & P11_TOKEN_PRESENT)
		num_tokens++;
	}
	free(slot_ids);
	if (ret)
	    goto out;
	if (num_tokens == 0) {
	    ret = HX509_PKCS11_NO_TOKEN;
	    goto out;
	}
    }

    *data = p;

    return 0;
 out:
    p11_release_module(p);
    return ret;
}

static void
p11_release_module(struct p11_module *p)
{
    size_t i;

    if (p->ref == 0)
	_hx509_abort("pkcs11 ref to low");
    if (--p->ref > 0)
	return;

    for (i = 0; i < p->num_slots; i++) {
	if (p->slot[i].flags & P11_SESSION_IN_USE)
	    _hx509_abort("pkcs11 module release while session in use");
	if (p->slot[i].flags & P11_SESSION) {
	    P11FUNC(p, CloseSession, (p->slot[i].session));
	}

	if (p->slot[i].name)
	    free(p->slot[i].name);
	if (p->slot[i].pin) {
	    memset(p->slot[i].pin, 0, strlen(p->slot[i].pin));
	    free(p->slot[i].pin);
	}
	if (p->slot[i].mechs.num) {
	    free(p->slot[i].mechs.list);

	    if (p->slot[i].mechs.infos) {
		size_t j;

		for (j = 0 ; j < p->slot[i].mechs.num ; j++)
		    free(p->slot[i].mechs.infos[j]);
		free(p->slot[i].mechs.infos);
	    }
	}
    }
    free(p->slot);

    if (p->funcs)
	P11FUNC(p, Finalize, (NULL));

    if (p->dl_handle)
	dlclose(p->dl_handle);

    memset(p, 0, sizeof(*p));
    free(p);
}

static int
p11_free(hx509_certs certs, void *data)
{
    struct p11_module *p = data;
    size_t i;

    for (i = 0; i < p->num_slots; i++) {
	if (p->slot[i].certs)
	    hx509_certs_free(&p->slot[i].certs);
    }
    p11_release_module(p);
    return 0;
}

struct p11_cursor {
    hx509_certs certs;
    void *cursor;
};

static int
p11_iter_start(hx509_context context,
	       hx509_certs certs, void *data, void **cursor)
{
    struct p11_module *p = data;
    struct p11_cursor *c;
    int ret;
    size_t i;

    c = malloc(sizeof(*c));
    if (c == NULL) {
	hx509_clear_error_string(context);
	return ENOMEM;
    }
    ret = hx509_certs_init(context, "MEMORY:pkcs11-iter", 0, NULL, &c->certs);
    if (ret) {
	free(c);
	return ret;
    }

    for (i = 0 ; i < p->num_slots; i++) {
	if (p->slot[i].certs == NULL)
	    continue;
	ret = hx509_certs_merge(context, c->certs, p->slot[i].certs);
	if (ret) {
	    hx509_certs_free(&c->certs);
	    free(c);
	    return ret;
	}
    }

    ret = hx509_certs_start_seq(context, c->certs, &c->cursor);
    if (ret) {
	hx509_certs_free(&c->certs);
	free(c);
	return 0;
    }
    *cursor = c;

    return 0;
}

static int
p11_iter(hx509_context context,
	 hx509_certs certs, void *data, void *cursor, hx509_cert *cert)
{
    struct p11_cursor *c = cursor;
    return hx509_certs_next_cert(context, c->certs, c->cursor, cert);
}

static int
p11_iter_end(hx509_context context,
	     hx509_certs certs, void *data, void *cursor)
{
    struct p11_cursor *c = cursor;
    int ret;
    ret = hx509_certs_end_seq(context, c->certs, c->cursor);
    hx509_certs_free(&c->certs);
    free(c);
    return ret;
}

#define MECHFLAG(x) { "unknown-flag-" #x, x }
static struct units mechflags[] = {
	MECHFLAG(0x80000000),
	MECHFLAG(0x40000000),
	MECHFLAG(0x20000000),
	MECHFLAG(0x10000000),
	MECHFLAG(0x08000000),
	MECHFLAG(0x04000000),
	{"ec-compress",		0x2000000 },
	{"ec-uncompress",	0x1000000 },
	{"ec-namedcurve",	0x0800000 },
	{"ec-ecparameters",	0x0400000 },
	{"ec-f-2m",		0x0200000 },
	{"ec-f-p",		0x0100000 },
	{"derive",		0x0080000 },
	{"unwrap",		0x0040000 },
	{"wrap",		0x0020000 },
	{"genereate-key-pair",	0x0010000 },
	{"generate",		0x0008000 },
	{"verify-recover",	0x0004000 },
	{"verify",		0x0002000 },
	{"sign-recover",	0x0001000 },
	{"sign",		0x0000800 },
	{"digest",		0x0000400 },
	{"decrypt",		0x0000200 },
	{"encrypt",		0x0000100 },
	MECHFLAG(0x00080),
	MECHFLAG(0x00040),
	MECHFLAG(0x00020),
	MECHFLAG(0x00010),
	MECHFLAG(0x00008),
	MECHFLAG(0x00004),
	MECHFLAG(0x00002),
	{"hw",			0x0000001 },
	{ NULL,			0x0000000 }
};
#undef MECHFLAG

static int
p11_printinfo(hx509_context context,
	      hx509_certs certs,
	      void *data,
	      int (*func)(void *, const char *),
	      void *ctx)
{
    struct p11_module *p = data;
    size_t i, j;

    _hx509_pi_printf(func, ctx, "pkcs11 driver with %d slot%s",
		     p->num_slots, p->num_slots > 1 ? "s" : "");

    for (i = 0; i < p->num_slots; i++) {
	struct p11_slot *s = &p->slot[i];

	_hx509_pi_printf(func, ctx, "slot %d: id: %d name: %s flags: %08x",
			 i, (int)s->id, s->name, s->flags);

	_hx509_pi_printf(func, ctx, "number of supported mechanisms: %lu",
			 (unsigned long)s->mechs.num);
	for (j = 0; j < s->mechs.num; j++) {
	    const char *mechname = "unknown";
	    char flags[256], unknownname[40];
#define MECHNAME(s,n) case s: mechname = n; break
	    switch(s->mechs.list[j]) {
		MECHNAME(CKM_RSA_PKCS_KEY_PAIR_GEN, "rsa-pkcs-key-pair-gen");
		MECHNAME(CKM_RSA_PKCS, "rsa-pkcs");
		MECHNAME(CKM_RSA_X_509, "rsa-x-509");
		MECHNAME(CKM_MD5_RSA_PKCS, "md5-rsa-pkcs");
		MECHNAME(CKM_SHA1_RSA_PKCS, "sha1-rsa-pkcs");
		MECHNAME(CKM_SHA256_RSA_PKCS, "sha256-rsa-pkcs");
		MECHNAME(CKM_SHA384_RSA_PKCS, "sha384-rsa-pkcs");
		MECHNAME(CKM_SHA512_RSA_PKCS, "sha512-rsa-pkcs");
		MECHNAME(CKM_RIPEMD160_RSA_PKCS, "ripemd160-rsa-pkcs");
		MECHNAME(CKM_RSA_PKCS_OAEP, "rsa-pkcs-oaep");
		MECHNAME(CKM_SHA512_HMAC, "sha512-hmac");
		MECHNAME(CKM_SHA512, "sha512");
		MECHNAME(CKM_SHA384_HMAC, "sha384-hmac");
		MECHNAME(CKM_SHA384, "sha384");
		MECHNAME(CKM_SHA256_HMAC, "sha256-hmac");
		MECHNAME(CKM_SHA256, "sha256");
		MECHNAME(CKM_SHA_1, "sha1");
		MECHNAME(CKM_MD5, "md5");
		MECHNAME(CKM_RIPEMD160, "ripemd-160");
		MECHNAME(CKM_DES_ECB, "des-ecb");
		MECHNAME(CKM_DES_CBC, "des-cbc");
		MECHNAME(CKM_AES_ECB, "aes-ecb");
		MECHNAME(CKM_AES_CBC, "aes-cbc");
		MECHNAME(CKM_DH_PKCS_PARAMETER_GEN, "dh-pkcs-parameter-gen");
	    default:
		snprintf(unknownname, sizeof(unknownname),
			 "unknown-mech-%lu",
			 (unsigned long)s->mechs.list[j]);
		mechname = unknownname;
		break;
	    }
#undef MECHNAME
	    unparse_flags(s->mechs.infos[j]->flags, mechflags,
			  flags, sizeof(flags));

	    _hx509_pi_printf(func, ctx, "  %s: %s", mechname, flags);
	}
    }

    return 0;
}

static struct hx509_keyset_ops keyset_pkcs11 = {
    "PKCS11",
    0,
    p11_init,
    NULL,
    p11_free,
    NULL,
    NULL,
    p11_iter_start,
    p11_iter,
    p11_iter_end,
    p11_printinfo
};

#endif /* HAVE_DLOPEN */

void
_hx509_ks_pkcs11_register(hx509_context context)
{
#ifdef HAVE_DLOPEN
    _hx509_ks_register(context, &keyset_pkcs11);
#endif
}
