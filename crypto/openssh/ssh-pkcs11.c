/* $OpenBSD: ssh-pkcs11.c,v 1.26 2018/02/07 02:06:51 jsing Exp $ */
/*
 * Copyright (c) 2010 Markus Friedl.  All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "includes.h"

#ifdef ENABLE_PKCS11

#include <sys/types.h>
#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif
#include <stdarg.h>
#include <stdio.h>

#include <string.h>
#include <dlfcn.h>

#include "openbsd-compat/sys-queue.h"
#include "openbsd-compat/openssl-compat.h"

#include <openssl/x509.h>

#define CRYPTOKI_COMPAT
#include "pkcs11.h"

#include "log.h"
#include "misc.h"
#include "sshkey.h"
#include "ssh-pkcs11.h"
#include "xmalloc.h"

struct pkcs11_slotinfo {
	CK_TOKEN_INFO		token;
	CK_SESSION_HANDLE	session;
	int			logged_in;
};

struct pkcs11_provider {
	char			*name;
	void			*handle;
	CK_FUNCTION_LIST	*function_list;
	CK_INFO			info;
	CK_ULONG		nslots;
	CK_SLOT_ID		*slotlist;
	struct pkcs11_slotinfo	*slotinfo;
	int			valid;
	int			refcount;
	TAILQ_ENTRY(pkcs11_provider) next;
};

TAILQ_HEAD(, pkcs11_provider) pkcs11_providers;

struct pkcs11_key {
	struct pkcs11_provider	*provider;
	CK_ULONG		slotidx;
	int			(*orig_finish)(RSA *rsa);
	RSA_METHOD		*rsa_method;
	char			*keyid;
	int			keyid_len;
};

int pkcs11_interactive = 0;

int
pkcs11_init(int interactive)
{
	pkcs11_interactive = interactive;
	TAILQ_INIT(&pkcs11_providers);
	return (0);
}

/*
 * finalize a provider shared libarary, it's no longer usable.
 * however, there might still be keys referencing this provider,
 * so the actuall freeing of memory is handled by pkcs11_provider_unref().
 * this is called when a provider gets unregistered.
 */
static void
pkcs11_provider_finalize(struct pkcs11_provider *p)
{
	CK_RV rv;
	CK_ULONG i;

	debug("pkcs11_provider_finalize: %p refcount %d valid %d",
	    p, p->refcount, p->valid);
	if (!p->valid)
		return;
	for (i = 0; i < p->nslots; i++) {
		if (p->slotinfo[i].session &&
		    (rv = p->function_list->C_CloseSession(
		    p->slotinfo[i].session)) != CKR_OK)
			error("C_CloseSession failed: %lu", rv);
	}
	if ((rv = p->function_list->C_Finalize(NULL)) != CKR_OK)
		error("C_Finalize failed: %lu", rv);
	p->valid = 0;
	p->function_list = NULL;
	dlclose(p->handle);
}

/*
 * remove a reference to the provider.
 * called when a key gets destroyed or when the provider is unregistered.
 */
static void
pkcs11_provider_unref(struct pkcs11_provider *p)
{
	debug("pkcs11_provider_unref: %p refcount %d", p, p->refcount);
	if (--p->refcount <= 0) {
		if (p->valid)
			error("pkcs11_provider_unref: %p still valid", p);
		free(p->slotlist);
		free(p->slotinfo);
		free(p);
	}
}

/* unregister all providers, keys might still point to the providers */
void
pkcs11_terminate(void)
{
	struct pkcs11_provider *p;

	while ((p = TAILQ_FIRST(&pkcs11_providers)) != NULL) {
		TAILQ_REMOVE(&pkcs11_providers, p, next);
		pkcs11_provider_finalize(p);
		pkcs11_provider_unref(p);
	}
}

/* lookup provider by name */
static struct pkcs11_provider *
pkcs11_provider_lookup(char *provider_id)
{
	struct pkcs11_provider *p;

	TAILQ_FOREACH(p, &pkcs11_providers, next) {
		debug("check %p %s", p, p->name);
		if (!strcmp(provider_id, p->name))
			return (p);
	}
	return (NULL);
}

/* unregister provider by name */
int
pkcs11_del_provider(char *provider_id)
{
	struct pkcs11_provider *p;

	if ((p = pkcs11_provider_lookup(provider_id)) != NULL) {
		TAILQ_REMOVE(&pkcs11_providers, p, next);
		pkcs11_provider_finalize(p);
		pkcs11_provider_unref(p);
		return (0);
	}
	return (-1);
}

/* openssl callback for freeing an RSA key */
static int
pkcs11_rsa_finish(RSA *rsa)
{
	struct pkcs11_key	*k11;
	int rv = -1;

	if ((k11 = RSA_get_app_data(rsa)) != NULL) {
		if (k11->orig_finish)
			rv = k11->orig_finish(rsa);
		if (k11->provider)
			pkcs11_provider_unref(k11->provider);
		RSA_meth_free(k11->rsa_method);
		free(k11->keyid);
		free(k11);
	}
	return (rv);
}

/* find a single 'obj' for given attributes */
static int
pkcs11_find(struct pkcs11_provider *p, CK_ULONG slotidx, CK_ATTRIBUTE *attr,
    CK_ULONG nattr, CK_OBJECT_HANDLE *obj)
{
	CK_FUNCTION_LIST	*f;
	CK_SESSION_HANDLE	session;
	CK_ULONG		nfound = 0;
	CK_RV			rv;
	int			ret = -1;

	f = p->function_list;
	session = p->slotinfo[slotidx].session;
	if ((rv = f->C_FindObjectsInit(session, attr, nattr)) != CKR_OK) {
		error("C_FindObjectsInit failed (nattr %lu): %lu", nattr, rv);
		return (-1);
	}
	if ((rv = f->C_FindObjects(session, obj, 1, &nfound)) != CKR_OK ||
	    nfound != 1) {
		debug("C_FindObjects failed (nfound %lu nattr %lu): %lu",
		    nfound, nattr, rv);
	} else
		ret = 0;
	if ((rv = f->C_FindObjectsFinal(session)) != CKR_OK)
		error("C_FindObjectsFinal failed: %lu", rv);
	return (ret);
}

/* openssl callback doing the actual signing operation */
static int
pkcs11_rsa_private_encrypt(int flen, const u_char *from, u_char *to, RSA *rsa,
    int padding)
{
	struct pkcs11_key	*k11;
	struct pkcs11_slotinfo	*si;
	CK_FUNCTION_LIST	*f;
	CK_OBJECT_HANDLE	obj;
	CK_ULONG		tlen = 0;
	CK_RV			rv;
	CK_OBJECT_CLASS	private_key_class = CKO_PRIVATE_KEY;
	CK_BBOOL		true_val = CK_TRUE;
	CK_MECHANISM		mech = {
		CKM_RSA_PKCS, NULL_PTR, 0
	};
	CK_ATTRIBUTE		key_filter[] = {
		{CKA_CLASS, NULL, sizeof(private_key_class) },
		{CKA_ID, NULL, 0},
		{CKA_SIGN, NULL, sizeof(true_val) }
	};
	char			*pin = NULL, prompt[1024];
	int			rval = -1;

	key_filter[0].pValue = &private_key_class;
	key_filter[2].pValue = &true_val;

	if ((k11 = RSA_get_app_data(rsa)) == NULL) {
		error("RSA_get_app_data failed for rsa %p", rsa);
		return (-1);
	}
	if (!k11->provider || !k11->provider->valid) {
		error("no pkcs11 (valid) provider for rsa %p", rsa);
		return (-1);
	}
	f = k11->provider->function_list;
	si = &k11->provider->slotinfo[k11->slotidx];
	if ((si->token.flags & CKF_LOGIN_REQUIRED) && !si->logged_in) {
		if (!pkcs11_interactive) {
			error("need pin entry%s", (si->token.flags &
			    CKF_PROTECTED_AUTHENTICATION_PATH) ?
			    " on reader keypad" : "");
			return (-1);
		}
		if (si->token.flags & CKF_PROTECTED_AUTHENTICATION_PATH)
			verbose("Deferring PIN entry to reader keypad.");
		else {
			snprintf(prompt, sizeof(prompt),
			    "Enter PIN for '%s': ", si->token.label);
			pin = read_passphrase(prompt, RP_ALLOW_EOF);
			if (pin == NULL)
				return (-1);	/* bail out */
		}
		rv = f->C_Login(si->session, CKU_USER, (u_char *)pin,
		    (pin != NULL) ? strlen(pin) : 0);
		if (pin != NULL) {
			explicit_bzero(pin, strlen(pin));
			free(pin);
		}
		if (rv != CKR_OK && rv != CKR_USER_ALREADY_LOGGED_IN) {
			error("C_Login failed: %lu", rv);
			return (-1);
		}
		si->logged_in = 1;
	}
	key_filter[1].pValue = k11->keyid;
	key_filter[1].ulValueLen = k11->keyid_len;
	/* try to find object w/CKA_SIGN first, retry w/o */
	if (pkcs11_find(k11->provider, k11->slotidx, key_filter, 3, &obj) < 0 &&
	    pkcs11_find(k11->provider, k11->slotidx, key_filter, 2, &obj) < 0) {
		error("cannot find private key");
	} else if ((rv = f->C_SignInit(si->session, &mech, obj)) != CKR_OK) {
		error("C_SignInit failed: %lu", rv);
	} else {
		/* XXX handle CKR_BUFFER_TOO_SMALL */
		tlen = RSA_size(rsa);
		rv = f->C_Sign(si->session, (CK_BYTE *)from, flen, to, &tlen);
		if (rv == CKR_OK) 
			rval = tlen;
		else 
			error("C_Sign failed: %lu", rv);
	}
	return (rval);
}

static int
pkcs11_rsa_private_decrypt(int flen, const u_char *from, u_char *to, RSA *rsa,
    int padding)
{
	return (-1);
}

/* redirect private key operations for rsa key to pkcs11 token */
static int
pkcs11_rsa_wrap(struct pkcs11_provider *provider, CK_ULONG slotidx,
    CK_ATTRIBUTE *keyid_attrib, RSA *rsa)
{
	struct pkcs11_key	*k11;
	const RSA_METHOD	*def = RSA_get_default_method();

	k11 = xcalloc(1, sizeof(*k11));
	k11->provider = provider;
	provider->refcount++;	/* provider referenced by RSA key */
	k11->slotidx = slotidx;
	/* identify key object on smartcard */
	k11->keyid_len = keyid_attrib->ulValueLen;
	if (k11->keyid_len > 0) {
		k11->keyid = xmalloc(k11->keyid_len);
		memcpy(k11->keyid, keyid_attrib->pValue, k11->keyid_len);
	}
	k11->rsa_method = RSA_meth_dup(def);
	if (k11->rsa_method == NULL)
		fatal("%s: RSA_meth_dup failed", __func__);
	k11->orig_finish = RSA_meth_get_finish(def);
	if (!RSA_meth_set1_name(k11->rsa_method, "pkcs11") ||
	    !RSA_meth_set_priv_enc(k11->rsa_method,
	    pkcs11_rsa_private_encrypt) ||
	    !RSA_meth_set_priv_dec(k11->rsa_method,
	    pkcs11_rsa_private_decrypt) ||
	    !RSA_meth_set_finish(k11->rsa_method, pkcs11_rsa_finish))
		fatal("%s: setup pkcs11 method failed", __func__);
	RSA_set_method(rsa, k11->rsa_method);
	RSA_set_app_data(rsa, k11);
	return (0);
}

/* remove trailing spaces */
static void
rmspace(u_char *buf, size_t len)
{
	size_t i;

	if (!len)
		return;
	for (i = len - 1;  i > 0; i--)
		if (i == len - 1 || buf[i] == ' ')
			buf[i] = '\0';
		else
			break;
}

/*
 * open a pkcs11 session and login if required.
 * if pin == NULL we delay login until key use
 */
static int
pkcs11_open_session(struct pkcs11_provider *p, CK_ULONG slotidx, char *pin)
{
	CK_RV			rv;
	CK_FUNCTION_LIST	*f;
	CK_SESSION_HANDLE	session;
	int			login_required;

	f = p->function_list;
	login_required = p->slotinfo[slotidx].token.flags & CKF_LOGIN_REQUIRED;
	if (pin && login_required && !strlen(pin)) {
		error("pin required");
		return (-1);
	}
	if ((rv = f->C_OpenSession(p->slotlist[slotidx], CKF_RW_SESSION|
	    CKF_SERIAL_SESSION, NULL, NULL, &session))
	    != CKR_OK) {
		error("C_OpenSession failed: %lu", rv);
		return (-1);
	}
	if (login_required && pin) {
		rv = f->C_Login(session, CKU_USER,
		    (u_char *)pin, strlen(pin));
		if (rv != CKR_OK && rv != CKR_USER_ALREADY_LOGGED_IN) {
			error("C_Login failed: %lu", rv);
			if ((rv = f->C_CloseSession(session)) != CKR_OK)
				error("C_CloseSession failed: %lu", rv);
			return (-1);
		}
		p->slotinfo[slotidx].logged_in = 1;
	}
	p->slotinfo[slotidx].session = session;
	return (0);
}

/*
 * lookup public keys for token in slot identified by slotidx,
 * add 'wrapped' public keys to the 'keysp' array and increment nkeys.
 * keysp points to an (possibly empty) array with *nkeys keys.
 */
static int pkcs11_fetch_keys_filter(struct pkcs11_provider *, CK_ULONG,
    CK_ATTRIBUTE [], CK_ATTRIBUTE [3], struct sshkey ***, int *)
	__attribute__((__bounded__(__minbytes__,4, 3 * sizeof(CK_ATTRIBUTE))));

static int
pkcs11_fetch_keys(struct pkcs11_provider *p, CK_ULONG slotidx,
    struct sshkey ***keysp, int *nkeys)
{
	CK_OBJECT_CLASS	pubkey_class = CKO_PUBLIC_KEY;
	CK_OBJECT_CLASS	cert_class = CKO_CERTIFICATE;
	CK_ATTRIBUTE		pubkey_filter[] = {
		{ CKA_CLASS, NULL, sizeof(pubkey_class) }
	};
	CK_ATTRIBUTE		cert_filter[] = {
		{ CKA_CLASS, NULL, sizeof(cert_class) }
	};
	CK_ATTRIBUTE		pubkey_attribs[] = {
		{ CKA_ID, NULL, 0 },
		{ CKA_MODULUS, NULL, 0 },
		{ CKA_PUBLIC_EXPONENT, NULL, 0 }
	};
	CK_ATTRIBUTE		cert_attribs[] = {
		{ CKA_ID, NULL, 0 },
		{ CKA_SUBJECT, NULL, 0 },
		{ CKA_VALUE, NULL, 0 }
	};
	pubkey_filter[0].pValue = &pubkey_class;
	cert_filter[0].pValue = &cert_class;

	if (pkcs11_fetch_keys_filter(p, slotidx, pubkey_filter, pubkey_attribs,
	    keysp, nkeys) < 0 ||
	    pkcs11_fetch_keys_filter(p, slotidx, cert_filter, cert_attribs,
	    keysp, nkeys) < 0)
		return (-1);
	return (0);
}

static int
pkcs11_key_included(struct sshkey ***keysp, int *nkeys, struct sshkey *key)
{
	int i;

	for (i = 0; i < *nkeys; i++)
		if (sshkey_equal(key, (*keysp)[i]))
			return (1);
	return (0);
}

static int
have_rsa_key(const RSA *rsa)
{
	const BIGNUM *rsa_n, *rsa_e;

	RSA_get0_key(rsa, &rsa_n, &rsa_e, NULL);
	return rsa_n != NULL && rsa_e != NULL;
}

static int
pkcs11_fetch_keys_filter(struct pkcs11_provider *p, CK_ULONG slotidx,
    CK_ATTRIBUTE filter[], CK_ATTRIBUTE attribs[3],
    struct sshkey ***keysp, int *nkeys)
{
	struct sshkey		*key;
	RSA			*rsa;
	X509 			*x509;
	EVP_PKEY		*evp;
	int			i;
	const u_char		*cp;
	CK_RV			rv;
	CK_OBJECT_HANDLE	obj;
	CK_ULONG		nfound;
	CK_SESSION_HANDLE	session;
	CK_FUNCTION_LIST	*f;

	f = p->function_list;
	session = p->slotinfo[slotidx].session;
	/* setup a filter the looks for public keys */
	if ((rv = f->C_FindObjectsInit(session, filter, 1)) != CKR_OK) {
		error("C_FindObjectsInit failed: %lu", rv);
		return (-1);
	}
	while (1) {
		/* XXX 3 attributes in attribs[] */
		for (i = 0; i < 3; i++) {
			attribs[i].pValue = NULL;
			attribs[i].ulValueLen = 0;
		}
		if ((rv = f->C_FindObjects(session, &obj, 1, &nfound)) != CKR_OK
		    || nfound == 0)
			break;
		/* found a key, so figure out size of the attributes */
		if ((rv = f->C_GetAttributeValue(session, obj, attribs, 3))
		    != CKR_OK) {
			error("C_GetAttributeValue failed: %lu", rv);
			continue;
		}
		/*
		 * Allow CKA_ID (always first attribute) to be empty, but
		 * ensure that none of the others are zero length.
		 * XXX assumes CKA_ID is always first.
		 */
		if (attribs[1].ulValueLen == 0 ||
		    attribs[2].ulValueLen == 0) {
			continue;
		}
		/* allocate buffers for attributes */
		for (i = 0; i < 3; i++) {
			if (attribs[i].ulValueLen > 0) {
				attribs[i].pValue = xmalloc(
				    attribs[i].ulValueLen);
			}
		}

		/*
		 * retrieve ID, modulus and public exponent of RSA key,
		 * or ID, subject and value for certificates.
		 */
		rsa = NULL;
		if ((rv = f->C_GetAttributeValue(session, obj, attribs, 3))
		    != CKR_OK) {
			error("C_GetAttributeValue failed: %lu", rv);
		} else if (attribs[1].type == CKA_MODULUS ) {
			if ((rsa = RSA_new()) == NULL) {
				error("RSA_new failed");
			} else {
				BIGNUM *rsa_n, *rsa_e;

				rsa_n = BN_bin2bn(attribs[1].pValue,
				    attribs[1].ulValueLen, NULL);
				rsa_e = BN_bin2bn(attribs[2].pValue,
				    attribs[2].ulValueLen, NULL);
				if (rsa_n != NULL && rsa_e != NULL) {
					if (!RSA_set0_key(rsa,
					    rsa_n, rsa_e, NULL))
						fatal("%s: set key", __func__);
					rsa_n = rsa_e = NULL; /* transferred */
				}
				BN_free(rsa_n);
				BN_free(rsa_e);
			}
		} else {
			cp = attribs[2].pValue;
			if ((x509 = X509_new()) == NULL) {
				error("X509_new failed");
			} else if (d2i_X509(&x509, &cp, attribs[2].ulValueLen)
			    == NULL) {
				error("d2i_X509 failed");
			} else if ((evp = X509_get_pubkey(x509)) == NULL ||
			    EVP_PKEY_base_id(evp) != EVP_PKEY_RSA ||
			    EVP_PKEY_get0_RSA(evp) == NULL) {
				debug("X509_get_pubkey failed or no rsa");
			} else if ((rsa = RSAPublicKey_dup(
			    EVP_PKEY_get0_RSA(evp))) == NULL) {
				error("RSAPublicKey_dup");
			}
			X509_free(x509);
		}
		if (rsa && have_rsa_key(rsa) &&
		    pkcs11_rsa_wrap(p, slotidx, &attribs[0], rsa) == 0) {
			if ((key = sshkey_new(KEY_UNSPEC)) == NULL)
				fatal("sshkey_new failed");
			key->rsa = rsa;
			key->type = KEY_RSA;
			key->flags |= SSHKEY_FLAG_EXT;
			if (pkcs11_key_included(keysp, nkeys, key)) {
				sshkey_free(key);
			} else {
				/* expand key array and add key */
				*keysp = xrecallocarray(*keysp, *nkeys,
				    *nkeys + 1, sizeof(struct sshkey *));
				(*keysp)[*nkeys] = key;
				*nkeys = *nkeys + 1;
				debug("have %d keys", *nkeys);
			}
		} else if (rsa) {
			RSA_free(rsa);
		}
		for (i = 0; i < 3; i++)
			free(attribs[i].pValue);
	}
	if ((rv = f->C_FindObjectsFinal(session)) != CKR_OK)
		error("C_FindObjectsFinal failed: %lu", rv);
	return (0);
}

/* register a new provider, fails if provider already exists */
int
pkcs11_add_provider(char *provider_id, char *pin, struct sshkey ***keyp)
{
	int nkeys, need_finalize = 0;
	struct pkcs11_provider *p = NULL;
	void *handle = NULL;
	CK_RV (*getfunctionlist)(CK_FUNCTION_LIST **);
	CK_RV rv;
	CK_FUNCTION_LIST *f = NULL;
	CK_TOKEN_INFO *token;
	CK_ULONG i;

	*keyp = NULL;
	if (pkcs11_provider_lookup(provider_id) != NULL) {
		debug("%s: provider already registered: %s",
		    __func__, provider_id);
		goto fail;
	}
	/* open shared pkcs11-libarary */
	if ((handle = dlopen(provider_id, RTLD_NOW)) == NULL) {
		error("dlopen %s failed: %s", provider_id, dlerror());
		goto fail;
	}
	if ((getfunctionlist = dlsym(handle, "C_GetFunctionList")) == NULL) {
		error("dlsym(C_GetFunctionList) failed: %s", dlerror());
		goto fail;
	}
	p = xcalloc(1, sizeof(*p));
	p->name = xstrdup(provider_id);
	p->handle = handle;
	/* setup the pkcs11 callbacks */
	if ((rv = (*getfunctionlist)(&f)) != CKR_OK) {
		error("C_GetFunctionList for provider %s failed: %lu",
		    provider_id, rv);
		goto fail;
	}
	p->function_list = f;
	if ((rv = f->C_Initialize(NULL)) != CKR_OK) {
		error("C_Initialize for provider %s failed: %lu",
		    provider_id, rv);
		goto fail;
	}
	need_finalize = 1;
	if ((rv = f->C_GetInfo(&p->info)) != CKR_OK) {
		error("C_GetInfo for provider %s failed: %lu",
		    provider_id, rv);
		goto fail;
	}
	rmspace(p->info.manufacturerID, sizeof(p->info.manufacturerID));
	rmspace(p->info.libraryDescription, sizeof(p->info.libraryDescription));
	debug("provider %s: manufacturerID <%s> cryptokiVersion %d.%d"
	    " libraryDescription <%s> libraryVersion %d.%d",
	    provider_id,
	    p->info.manufacturerID,
	    p->info.cryptokiVersion.major,
	    p->info.cryptokiVersion.minor,
	    p->info.libraryDescription,
	    p->info.libraryVersion.major,
	    p->info.libraryVersion.minor);
	if ((rv = f->C_GetSlotList(CK_TRUE, NULL, &p->nslots)) != CKR_OK) {
		error("C_GetSlotList failed: %lu", rv);
		goto fail;
	}
	if (p->nslots == 0) {
		debug("%s: provider %s returned no slots", __func__,
		    provider_id);
		goto fail;
	}
	p->slotlist = xcalloc(p->nslots, sizeof(CK_SLOT_ID));
	if ((rv = f->C_GetSlotList(CK_TRUE, p->slotlist, &p->nslots))
	    != CKR_OK) {
		error("C_GetSlotList for provider %s failed: %lu",
		    provider_id, rv);
		goto fail;
	}
	p->slotinfo = xcalloc(p->nslots, sizeof(struct pkcs11_slotinfo));
	p->valid = 1;
	nkeys = 0;
	for (i = 0; i < p->nslots; i++) {
		token = &p->slotinfo[i].token;
		if ((rv = f->C_GetTokenInfo(p->slotlist[i], token))
		    != CKR_OK) {
			error("C_GetTokenInfo for provider %s slot %lu "
			    "failed: %lu", provider_id, (unsigned long)i, rv);
			continue;
		}
		if ((token->flags & CKF_TOKEN_INITIALIZED) == 0) {
			debug2("%s: ignoring uninitialised token in "
			    "provider %s slot %lu", __func__,
			    provider_id, (unsigned long)i);
			continue;
		}
		rmspace(token->label, sizeof(token->label));
		rmspace(token->manufacturerID, sizeof(token->manufacturerID));
		rmspace(token->model, sizeof(token->model));
		rmspace(token->serialNumber, sizeof(token->serialNumber));
		debug("provider %s slot %lu: label <%s> manufacturerID <%s> "
		    "model <%s> serial <%s> flags 0x%lx",
		    provider_id, (unsigned long)i,
		    token->label, token->manufacturerID, token->model,
		    token->serialNumber, token->flags);
		/* open session, login with pin and retrieve public keys */
		if (pkcs11_open_session(p, i, pin) == 0)
			pkcs11_fetch_keys(p, i, keyp, &nkeys);
	}
	if (nkeys > 0) {
		TAILQ_INSERT_TAIL(&pkcs11_providers, p, next);
		p->refcount++;	/* add to provider list */
		return (nkeys);
	}
	debug("%s: provider %s returned no keys", __func__, provider_id);
	/* don't add the provider, since it does not have any keys */
fail:
	if (need_finalize && (rv = f->C_Finalize(NULL)) != CKR_OK)
		error("C_Finalize for provider %s failed: %lu",
		    provider_id, rv);
	if (p) {
		free(p->slotlist);
		free(p->slotinfo);
		free(p);
	}
	if (handle)
		dlclose(handle);
	return (-1);
}

#else

int
pkcs11_init(int interactive)
{
	return (0);
}

void
pkcs11_terminate(void)
{
	return;
}

#endif /* ENABLE_PKCS11 */
