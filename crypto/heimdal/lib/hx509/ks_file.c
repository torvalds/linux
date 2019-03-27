/*
 * Copyright (c) 2005 - 2007 Kungliga Tekniska Högskolan
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

typedef enum { USE_PEM, USE_DER } outformat;

struct ks_file {
    hx509_certs certs;
    char *fn;
    outformat format;
};

/*
 *
 */

static int
parse_certificate(hx509_context context, const char *fn,
		  struct hx509_collector *c,
		  const hx509_pem_header *headers,
		  const void *data, size_t len,
		  const AlgorithmIdentifier *ai)
{
    hx509_cert cert;
    int ret;

    ret = hx509_cert_init_data(context, data, len, &cert);
    if (ret)
	return ret;

    ret = _hx509_collector_certs_add(context, c, cert);
    hx509_cert_free(cert);
    return ret;
}

static int
try_decrypt(hx509_context context,
	    struct hx509_collector *collector,
	    const AlgorithmIdentifier *alg,
	    const EVP_CIPHER *c,
	    const void *ivdata,
	    const void *password,
	    size_t passwordlen,
	    const void *cipher,
	    size_t len)
{
    heim_octet_string clear;
    size_t keylen;
    void *key;
    int ret;

    keylen = EVP_CIPHER_key_length(c);

    key = malloc(keylen);
    if (key == NULL) {
	hx509_clear_error_string(context);
	return ENOMEM;
    }

    ret = EVP_BytesToKey(c, EVP_md5(), ivdata,
			 password, passwordlen,
			 1, key, NULL);
    if (ret <= 0) {
	hx509_set_error_string(context, 0, HX509_CRYPTO_INTERNAL_ERROR,
			       "Failed to do string2key for private key");
	return HX509_CRYPTO_INTERNAL_ERROR;
    }

    clear.data = malloc(len);
    if (clear.data == NULL) {
	hx509_set_error_string(context, 0, ENOMEM,
			       "Out of memory to decrypt for private key");
	ret = ENOMEM;
	goto out;
    }
    clear.length = len;

    {
	EVP_CIPHER_CTX *ctx;

	ctx = EVP_CIPHER_CTX_new();
	if (ctx == NULL) {
		hx509_set_error_string(context, 0, ENOMEM,
				       "Out of memory to decrypt for private key");
		ret = ENOMEM;
		goto out;
	}
	EVP_CipherInit_ex(ctx, c, NULL, key, ivdata, 0);
	EVP_Cipher(ctx, clear.data, cipher, len);
	EVP_CIPHER_CTX_free(ctx);
    }

    ret = _hx509_collector_private_key_add(context,
					   collector,
					   alg,
					   NULL,
					   &clear,
					   NULL);

    memset(clear.data, 0, clear.length);
out:
    free(clear.data);
    memset(key, 0, keylen);
    free(key);
    return ret;
}

static int
parse_pkcs8_private_key(hx509_context context, const char *fn,
			struct hx509_collector *c,
			const hx509_pem_header *headers,
			const void *data, size_t length,
			const AlgorithmIdentifier *ai)
{
    PKCS8PrivateKeyInfo ki;
    heim_octet_string keydata;

    int ret;

    ret = decode_PKCS8PrivateKeyInfo(data, length, &ki, NULL);
    if (ret)
	return ret;

    keydata.data = rk_UNCONST(data);
    keydata.length = length;

    ret = _hx509_collector_private_key_add(context,
					   c,
					   &ki.privateKeyAlgorithm,
					   NULL,
					   &ki.privateKey,
					   &keydata);
    free_PKCS8PrivateKeyInfo(&ki);
    return ret;
}

static int
parse_pem_private_key(hx509_context context, const char *fn,
		      struct hx509_collector *c,
		      const hx509_pem_header *headers,
		      const void *data, size_t len,
		      const AlgorithmIdentifier *ai)
{
    int ret = 0;
    const char *enc;

    enc = hx509_pem_find_header(headers, "Proc-Type");
    if (enc) {
	const char *dek;
	char *type, *iv;
	ssize_t ssize, size;
	void *ivdata;
	const EVP_CIPHER *cipher;
	const struct _hx509_password *pw;
	hx509_lock lock;
	int decrypted = 0;
	size_t i;

	lock = _hx509_collector_get_lock(c);
	if (lock == NULL) {
	    hx509_set_error_string(context, 0, HX509_ALG_NOT_SUPP,
				   "Failed to get password for "
				   "password protected file %s", fn);
	    return HX509_ALG_NOT_SUPP;
	}

	if (strcmp(enc, "4,ENCRYPTED") != 0) {
	    hx509_set_error_string(context, 0, HX509_PARSING_KEY_FAILED,
				   "Private key encrypted in unknown method %s "
				   "in file",
				   enc, fn);
	    hx509_clear_error_string(context);
	    return HX509_PARSING_KEY_FAILED;
	}

	dek = hx509_pem_find_header(headers, "DEK-Info");
	if (dek == NULL) {
	    hx509_set_error_string(context, 0, HX509_PARSING_KEY_FAILED,
				   "Encrypted private key missing DEK-Info");
	    return HX509_PARSING_KEY_FAILED;
	}

	type = strdup(dek);
	if (type == NULL) {
	    hx509_clear_error_string(context);
	    return ENOMEM;
	}

	iv = strchr(type, ',');
	if (iv == NULL) {
	    free(type);
	    hx509_set_error_string(context, 0, HX509_PARSING_KEY_FAILED,
				   "IV missing");
	    return HX509_PARSING_KEY_FAILED;
	}

	*iv++ = '\0';

	size = strlen(iv);
	ivdata = malloc(size);
	if (ivdata == NULL) {
	    hx509_clear_error_string(context);
	    free(type);
	    return ENOMEM;
	}

	cipher = EVP_get_cipherbyname(type);
	if (cipher == NULL) {
	    free(ivdata);
	    hx509_set_error_string(context, 0, HX509_ALG_NOT_SUPP,
				   "Private key encrypted with "
				   "unsupported cipher: %s",
				   type);
	    free(type);
	    return HX509_ALG_NOT_SUPP;
	}

#define PKCS5_SALT_LEN 8

	ssize = hex_decode(iv, ivdata, size);
	free(type);
	type = NULL;
	iv = NULL;

	if (ssize < 0 || ssize < PKCS5_SALT_LEN || ssize < EVP_CIPHER_iv_length(cipher)) {
	    free(ivdata);
	    hx509_set_error_string(context, 0, HX509_PARSING_KEY_FAILED,
				   "Salt have wrong length in "
				   "private key file");
	    return HX509_PARSING_KEY_FAILED;
	}

	pw = _hx509_lock_get_passwords(lock);
	if (pw != NULL) {
	    const void *password;
	    size_t passwordlen;

	    for (i = 0; i < pw->len; i++) {
		password = pw->val[i];
		passwordlen = strlen(password);

		ret = try_decrypt(context, c, ai, cipher, ivdata,
				  password, passwordlen, data, len);
		if (ret == 0) {
		    decrypted = 1;
		    break;
		}
	    }
	}
	if (!decrypted) {
	    hx509_prompt prompt;
	    char password[128];

	    memset(&prompt, 0, sizeof(prompt));

	    prompt.prompt = "Password for keyfile: ";
	    prompt.type = HX509_PROMPT_TYPE_PASSWORD;
	    prompt.reply.data = password;
	    prompt.reply.length = sizeof(password);

	    ret = hx509_lock_prompt(lock, &prompt);
	    if (ret == 0)
		ret = try_decrypt(context, c, ai, cipher, ivdata, password,
				  strlen(password), data, len);
	    /* XXX add password to lock password collection ? */
	    memset(password, 0, sizeof(password));
	}
	free(ivdata);

    } else {
	heim_octet_string keydata;

	keydata.data = rk_UNCONST(data);
	keydata.length = len;

	ret = _hx509_collector_private_key_add(context, c, ai, NULL,
					       &keydata, NULL);
    }

    return ret;
}


struct pem_formats {
    const char *name;
    int (*func)(hx509_context, const char *, struct hx509_collector *,
		const hx509_pem_header *, const void *, size_t,
		const AlgorithmIdentifier *);
    const AlgorithmIdentifier *(*ai)(void);
} formats[] = {
    { "CERTIFICATE", parse_certificate, NULL },
    { "PRIVATE KEY", parse_pkcs8_private_key, NULL },
    { "RSA PRIVATE KEY", parse_pem_private_key, hx509_signature_rsa },
    { "EC PRIVATE KEY", parse_pem_private_key, hx509_signature_ecPublicKey }
};


struct pem_ctx {
    int flags;
    struct hx509_collector *c;
};

static int
pem_func(hx509_context context, const char *type,
	 const hx509_pem_header *header,
	 const void *data, size_t len, void *ctx)
{
    struct pem_ctx *pem_ctx = (struct pem_ctx*)ctx;
    int ret = 0;
    size_t j;

    for (j = 0; j < sizeof(formats)/sizeof(formats[0]); j++) {
	const char *q = formats[j].name;
	if (strcasecmp(type, q) == 0) {
	    const AlgorithmIdentifier *ai = NULL;
	    if (formats[j].ai != NULL)
		ai = (*formats[j].ai)();

	    ret = (*formats[j].func)(context, NULL, pem_ctx->c,
				     header, data, len, ai);
	    if (ret && (pem_ctx->flags & HX509_CERTS_UNPROTECT_ALL)) {
		hx509_set_error_string(context, HX509_ERROR_APPEND, ret,
				       "Failed parseing PEM format %s", type);
		return ret;
	    }
	    break;
	}
    }
    if (j == sizeof(formats)/sizeof(formats[0])) {
	ret = HX509_UNSUPPORTED_OPERATION;
	hx509_set_error_string(context, 0, ret,
			       "Found no matching PEM format for %s", type);
	return ret;
    }
    return 0;
}

/*
 *
 */

static int
file_init_common(hx509_context context,
		 hx509_certs certs, void **data, int flags,
		 const char *residue, hx509_lock lock, outformat format)
{
    char *p, *pnext;
    struct ks_file *ksf = NULL;
    hx509_private_key *keys = NULL;
    int ret;
    struct pem_ctx pem_ctx;

    pem_ctx.flags = flags;
    pem_ctx.c = NULL;

    *data = NULL;

    if (lock == NULL)
	lock = _hx509_empty_lock;

    ksf = calloc(1, sizeof(*ksf));
    if (ksf == NULL) {
	hx509_clear_error_string(context);
	return ENOMEM;
    }
    ksf->format = format;

    ksf->fn = strdup(residue);
    if (ksf->fn == NULL) {
	hx509_clear_error_string(context);
	ret = ENOMEM;
	goto out;
    }

    /*
     * XXX this is broken, the function should parse the file before
     * overwriting it
     */

    if (flags & HX509_CERTS_CREATE) {
	ret = hx509_certs_init(context, "MEMORY:ks-file-create",
			       0, lock, &ksf->certs);
	if (ret)
	    goto out;
	*data = ksf;
	return 0;
    }

    ret = _hx509_collector_alloc(context, lock, &pem_ctx.c);
    if (ret)
	goto out;

    for (p = ksf->fn; p != NULL; p = pnext) {
	FILE *f;

	pnext = strchr(p, ',');
	if (pnext)
	    *pnext++ = '\0';


	if ((f = fopen(p, "r")) == NULL) {
	    ret = ENOENT;
	    hx509_set_error_string(context, 0, ret,
				   "Failed to open PEM file \"%s\": %s",
				   p, strerror(errno));
	    goto out;
	}
	rk_cloexec_file(f);

	ret = hx509_pem_read(context, f, pem_func, &pem_ctx);
	fclose(f);
	if (ret != 0 && ret != HX509_PARSING_KEY_FAILED)
	    goto out;
	else if (ret == HX509_PARSING_KEY_FAILED) {
	    size_t length;
	    void *ptr;
	    size_t i;

	    ret = rk_undumpdata(p, &ptr, &length);
	    if (ret) {
		hx509_clear_error_string(context);
		goto out;
	    }

	    for (i = 0; i < sizeof(formats)/sizeof(formats[0]); i++) {
		const AlgorithmIdentifier *ai = NULL;
		if (formats[i].ai != NULL)
		    ai = (*formats[i].ai)();

		ret = (*formats[i].func)(context, p, pem_ctx.c, NULL, ptr, length, ai);
		if (ret == 0)
		    break;
	    }
	    rk_xfree(ptr);
	    if (ret) {
		hx509_clear_error_string(context);
		goto out;
	    }
	}
    }

    ret = _hx509_collector_collect_certs(context, pem_ctx.c, &ksf->certs);
    if (ret)
	goto out;

    ret = _hx509_collector_collect_private_keys(context, pem_ctx.c, &keys);
    if (ret == 0) {
	int i;

	for (i = 0; keys[i]; i++)
	    _hx509_certs_keys_add(context, ksf->certs, keys[i]);
	_hx509_certs_keys_free(context, keys);
    }

out:
    if (ret == 0)
	*data = ksf;
    else {
	if (ksf->fn)
	    free(ksf->fn);
	free(ksf);
    }
    if (pem_ctx.c)
	_hx509_collector_free(pem_ctx.c);

    return ret;
}

static int
file_init_pem(hx509_context context,
	      hx509_certs certs, void **data, int flags,
	      const char *residue, hx509_lock lock)
{
    return file_init_common(context, certs, data, flags, residue, lock, USE_PEM);
}

static int
file_init_der(hx509_context context,
	      hx509_certs certs, void **data, int flags,
	      const char *residue, hx509_lock lock)
{
    return file_init_common(context, certs, data, flags, residue, lock, USE_DER);
}

static int
file_free(hx509_certs certs, void *data)
{
    struct ks_file *ksf = data;
    hx509_certs_free(&ksf->certs);
    free(ksf->fn);
    free(ksf);
    return 0;
}

struct store_ctx {
    FILE *f;
    outformat format;
};

static int
store_func(hx509_context context, void *ctx, hx509_cert c)
{
    struct store_ctx *sc = ctx;
    heim_octet_string data;
    int ret;

    ret = hx509_cert_binary(context, c, &data);
    if (ret)
	return ret;

    switch (sc->format) {
    case USE_DER:
	fwrite(data.data, data.length, 1, sc->f);
	free(data.data);
	break;
    case USE_PEM:
	hx509_pem_write(context, "CERTIFICATE", NULL, sc->f,
			data.data, data.length);
	free(data.data);
	if (_hx509_cert_private_key_exportable(c)) {
	    hx509_private_key key = _hx509_cert_private_key(c);
	    ret = _hx509_private_key_export(context, key,
					    HX509_KEY_FORMAT_DER, &data);
	    if (ret)
		break;
	    hx509_pem_write(context, _hx509_private_pem_name(key), NULL, sc->f,
			    data.data, data.length);
	    free(data.data);
	}
	break;
    }

    return 0;
}

static int
file_store(hx509_context context,
	   hx509_certs certs, void *data, int flags, hx509_lock lock)
{
    struct ks_file *ksf = data;
    struct store_ctx sc;
    int ret;

    sc.f = fopen(ksf->fn, "w");
    if (sc.f == NULL) {
	hx509_set_error_string(context, 0, ENOENT,
			       "Failed to open file %s for writing");
	return ENOENT;
    }
    rk_cloexec_file(sc.f);
    sc.format = ksf->format;

    ret = hx509_certs_iter_f(context, ksf->certs, store_func, &sc);
    fclose(sc.f);
    return ret;
}

static int
file_add(hx509_context context, hx509_certs certs, void *data, hx509_cert c)
{
    struct ks_file *ksf = data;
    return hx509_certs_add(context, ksf->certs, c);
}

static int
file_iter_start(hx509_context context,
		hx509_certs certs, void *data, void **cursor)
{
    struct ks_file *ksf = data;
    return hx509_certs_start_seq(context, ksf->certs, cursor);
}

static int
file_iter(hx509_context context,
	  hx509_certs certs, void *data, void *iter, hx509_cert *cert)
{
    struct ks_file *ksf = data;
    return hx509_certs_next_cert(context, ksf->certs, iter, cert);
}

static int
file_iter_end(hx509_context context,
	      hx509_certs certs,
	      void *data,
	      void *cursor)
{
    struct ks_file *ksf = data;
    return hx509_certs_end_seq(context, ksf->certs, cursor);
}

static int
file_getkeys(hx509_context context,
	     hx509_certs certs,
	     void *data,
	     hx509_private_key **keys)
{
    struct ks_file *ksf = data;
    return _hx509_certs_keys_get(context, ksf->certs, keys);
}

static int
file_addkey(hx509_context context,
	     hx509_certs certs,
	     void *data,
	     hx509_private_key key)
{
    struct ks_file *ksf = data;
    return _hx509_certs_keys_add(context, ksf->certs, key);
}

static struct hx509_keyset_ops keyset_file = {
    "FILE",
    0,
    file_init_pem,
    file_store,
    file_free,
    file_add,
    NULL,
    file_iter_start,
    file_iter,
    file_iter_end,
    NULL,
    file_getkeys,
    file_addkey
};

static struct hx509_keyset_ops keyset_pemfile = {
    "PEM-FILE",
    0,
    file_init_pem,
    file_store,
    file_free,
    file_add,
    NULL,
    file_iter_start,
    file_iter,
    file_iter_end,
    NULL,
    file_getkeys,
    file_addkey
};

static struct hx509_keyset_ops keyset_derfile = {
    "DER-FILE",
    0,
    file_init_der,
    file_store,
    file_free,
    file_add,
    NULL,
    file_iter_start,
    file_iter,
    file_iter_end,
    NULL,
    file_getkeys,
    file_addkey
};


void
_hx509_ks_file_register(hx509_context context)
{
    _hx509_ks_register(context, &keyset_file);
    _hx509_ks_register(context, &keyset_pemfile);
    _hx509_ks_register(context, &keyset_derfile);
}
