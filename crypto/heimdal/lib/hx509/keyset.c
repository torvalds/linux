/*
 * Copyright (c) 2004 - 2007 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Portions Copyright (c) 2009 Apple Inc. All rights reserved.
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

/**
 * @page page_keyset Certificate store operations
 *
 * Type of certificates store:
 * - MEMORY
 *   In memory based format. Doesnt support storing.
 * - FILE
 *   FILE supports raw DER certicates and PEM certicates. When PEM is
 *   used the file can contain may certificates and match private
 *   keys. Support storing the certificates. DER format only supports
 *   on certificate and no private key.
 * - PEM-FILE
 *   Same as FILE, defaulting to PEM encoded certificates.
 * - PEM-FILE
 *   Same as FILE, defaulting to DER encoded certificates.
 * - PKCS11
 * - PKCS12
 * - DIR
 * - KEYCHAIN
 *   Apple Mac OS X KeyChain backed keychain object.
 *
 * See the library functions here: @ref hx509_keyset
 */

struct hx509_certs_data {
    unsigned int ref;
    struct hx509_keyset_ops *ops;
    void *ops_data;
};

static struct hx509_keyset_ops *
_hx509_ks_type(hx509_context context, const char *type)
{
    int i;

    for (i = 0; i < context->ks_num_ops; i++)
	if (strcasecmp(type, context->ks_ops[i]->name) == 0)
	    return context->ks_ops[i];

    return NULL;
}

void
_hx509_ks_register(hx509_context context, struct hx509_keyset_ops *ops)
{
    struct hx509_keyset_ops **val;

    if (_hx509_ks_type(context, ops->name))
	return;

    val = realloc(context->ks_ops,
		  (context->ks_num_ops + 1) * sizeof(context->ks_ops[0]));
    if (val == NULL)
	return;
    val[context->ks_num_ops] = ops;
    context->ks_ops = val;
    context->ks_num_ops++;
}

/**
 * Open or creates a new hx509 certificate store.
 *
 * @param context A hx509 context
 * @param name name of the store, format is TYPE:type-specific-string,
 * if NULL is used the MEMORY store is used.
 * @param flags list of flags:
 * - HX509_CERTS_CREATE create a new keystore of the specific TYPE.
 * - HX509_CERTS_UNPROTECT_ALL fails if any private key failed to be extracted.
 * @param lock a lock that unlocks the certificates store, use NULL to
 * select no password/certifictes/prompt lock (see @ref page_lock).
 * @param certs return pointer, free with hx509_certs_free().
 *
 * @ingroup hx509_keyset
 */

int
hx509_certs_init(hx509_context context,
		 const char *name, int flags,
		 hx509_lock lock, hx509_certs *certs)
{
    struct hx509_keyset_ops *ops;
    const char *residue;
    hx509_certs c;
    char *type;
    int ret;

    *certs = NULL;

    residue = strchr(name, ':');
    if (residue) {
	type = malloc(residue - name + 1);
	if (type)
	    strlcpy(type, name, residue - name + 1);
	residue++;
	if (residue[0] == '\0')
	    residue = NULL;
    } else {
	type = strdup("MEMORY");
	residue = name;
    }
    if (type == NULL) {
	hx509_clear_error_string(context);
	return ENOMEM;
    }

    ops = _hx509_ks_type(context, type);
    if (ops == NULL) {
	hx509_set_error_string(context, 0, ENOENT,
			       "Keyset type %s is not supported", type);
	free(type);
	return ENOENT;
    }
    free(type);
    c = calloc(1, sizeof(*c));
    if (c == NULL) {
	hx509_clear_error_string(context);
	return ENOMEM;
    }
    c->ops = ops;
    c->ref = 1;

    ret = (*ops->init)(context, c, &c->ops_data, flags, residue, lock);
    if (ret) {
	free(c);
	return ret;
    }

    *certs = c;
    return 0;
}

/**
 * Write the certificate store to stable storage.
 *
 * @param context A hx509 context.
 * @param certs a certificate store to store.
 * @param flags currently unused, use 0.
 * @param lock a lock that unlocks the certificates store, use NULL to
 * select no password/certifictes/prompt lock (see @ref page_lock).
 *
 * @return Returns an hx509 error code. HX509_UNSUPPORTED_OPERATION if
 * the certificate store doesn't support the store operation.
 *
 * @ingroup hx509_keyset
 */

int
hx509_certs_store(hx509_context context,
		  hx509_certs certs,
		  int flags,
		  hx509_lock lock)
{
    if (certs->ops->store == NULL) {
	hx509_set_error_string(context, 0, HX509_UNSUPPORTED_OPERATION,
			       "keystore if type %s doesn't support "
			       "store operation",
			       certs->ops->name);
	return HX509_UNSUPPORTED_OPERATION;
    }

    return (*certs->ops->store)(context, certs, certs->ops_data, flags, lock);
}


hx509_certs
hx509_certs_ref(hx509_certs certs)
{
    if (certs == NULL)
	return NULL;
    if (certs->ref == 0)
	_hx509_abort("certs refcount == 0 on ref");
    if (certs->ref == UINT_MAX)
	_hx509_abort("certs refcount == UINT_MAX on ref");
    certs->ref++;
    return certs;
}

/**
 * Free a certificate store.
 *
 * @param certs certificate store to free.
 *
 * @ingroup hx509_keyset
 */

void
hx509_certs_free(hx509_certs *certs)
{
    if (*certs) {
	if ((*certs)->ref == 0)
	    _hx509_abort("cert refcount == 0 on free");
	if (--(*certs)->ref > 0)
	    return;

	(*(*certs)->ops->free)(*certs, (*certs)->ops_data);
	free(*certs);
	*certs = NULL;
    }
}

/**
 * Start the integration
 *
 * @param context a hx509 context.
 * @param certs certificate store to iterate over
 * @param cursor cursor that will keep track of progress, free with
 * hx509_certs_end_seq().
 *
 * @return Returns an hx509 error code. HX509_UNSUPPORTED_OPERATION is
 * returned if the certificate store doesn't support the iteration
 * operation.
 *
 * @ingroup hx509_keyset
 */

int
hx509_certs_start_seq(hx509_context context,
		      hx509_certs certs,
		      hx509_cursor *cursor)
{
    int ret;

    if (certs->ops->iter_start == NULL) {
	hx509_set_error_string(context, 0, HX509_UNSUPPORTED_OPERATION,
			       "Keyset type %s doesn't support iteration",
			       certs->ops->name);
	return HX509_UNSUPPORTED_OPERATION;
    }

    ret = (*certs->ops->iter_start)(context, certs, certs->ops_data, cursor);
    if (ret)
	return ret;

    return 0;
}

/**
 * Get next ceritificate from the certificate keystore pointed out by
 * cursor.
 *
 * @param context a hx509 context.
 * @param certs certificate store to iterate over.
 * @param cursor cursor that keeps track of progress.
 * @param cert return certificate next in store, NULL if the store
 * contains no more certificates. Free with hx509_cert_free().
 *
 * @return Returns an hx509 error code.
 *
 * @ingroup hx509_keyset
 */

int
hx509_certs_next_cert(hx509_context context,
		      hx509_certs certs,
		      hx509_cursor cursor,
		      hx509_cert *cert)
{
    *cert = NULL;
    return (*certs->ops->iter)(context, certs, certs->ops_data, cursor, cert);
}

/**
 * End the iteration over certificates.
 *
 * @param context a hx509 context.
 * @param certs certificate store to iterate over.
 * @param cursor cursor that will keep track of progress, freed.
 *
 * @return Returns an hx509 error code.
 *
 * @ingroup hx509_keyset
 */

int
hx509_certs_end_seq(hx509_context context,
		    hx509_certs certs,
		    hx509_cursor cursor)
{
    (*certs->ops->iter_end)(context, certs, certs->ops_data, cursor);
    return 0;
}

/**
 * Iterate over all certificates in a keystore and call an function
 * for each fo them.
 *
 * @param context a hx509 context.
 * @param certs certificate store to iterate over.
 * @param func function to call for each certificate. The function
 * should return non-zero to abort the iteration, that value is passed
 * back to the caller of hx509_certs_iter_f().
 * @param ctx context variable that will passed to the function.
 *
 * @return Returns an hx509 error code.
 *
 * @ingroup hx509_keyset
 */

int
hx509_certs_iter_f(hx509_context context,
		   hx509_certs certs,
		   int (*func)(hx509_context, void *, hx509_cert),
		   void *ctx)
{
    hx509_cursor cursor;
    hx509_cert c;
    int ret;

    ret = hx509_certs_start_seq(context, certs, &cursor);
    if (ret)
	return ret;

    while (1) {
	ret = hx509_certs_next_cert(context, certs, cursor, &c);
	if (ret)
	    break;
	if (c == NULL) {
	    ret = 0;
	    break;
	}
	ret = (*func)(context, ctx, c);
	hx509_cert_free(c);
	if (ret)
	    break;
    }

    hx509_certs_end_seq(context, certs, cursor);

    return ret;
}

/**
 * Iterate over all certificates in a keystore and call an function
 * for each fo them.
 *
 * @param context a hx509 context.
 * @param certs certificate store to iterate over.
 * @param func function to call for each certificate. The function
 * should return non-zero to abort the iteration, that value is passed
 * back to the caller of hx509_certs_iter().
 *
 * @return Returns an hx509 error code.
 *
 * @ingroup hx509_keyset
 */

#ifdef __BLOCKS__

static int
certs_iter(hx509_context context, void *ctx, hx509_cert cert)
{
    int (^func)(hx509_cert) = ctx;
    return func(cert);
}

/**
 * Iterate over all certificates in a keystore and call an block
 * for each fo them.
 *
 * @param context a hx509 context.
 * @param certs certificate store to iterate over.
 * @param func block to call for each certificate. The function
 * should return non-zero to abort the iteration, that value is passed
 * back to the caller of hx509_certs_iter().
 *
 * @return Returns an hx509 error code.
 *
 * @ingroup hx509_keyset
 */

int
hx509_certs_iter(hx509_context context,
		 hx509_certs certs,
		 int (^func)(hx509_cert))
{
    return hx509_certs_iter_f(context, certs, certs_iter, func);
}
#endif


/**
 * Function to use to hx509_certs_iter_f() as a function argument, the
 * ctx variable to hx509_certs_iter_f() should be a FILE file descriptor.
 *
 * @param context a hx509 context.
 * @param ctx used by hx509_certs_iter_f().
 * @param c a certificate
 *
 * @return Returns an hx509 error code.
 *
 * @ingroup hx509_keyset
 */

int
hx509_ci_print_names(hx509_context context, void *ctx, hx509_cert c)
{
    Certificate *cert;
    hx509_name n;
    char *s, *i;

    cert = _hx509_get_cert(c);

    _hx509_name_from_Name(&cert->tbsCertificate.subject, &n);
    hx509_name_to_string(n, &s);
    hx509_name_free(&n);
    _hx509_name_from_Name(&cert->tbsCertificate.issuer, &n);
    hx509_name_to_string(n, &i);
    hx509_name_free(&n);
    fprintf(ctx, "subject: %s\nissuer: %s\n", s, i);
    free(s);
    free(i);
    return 0;
}

/**
 * Add a certificate to the certificiate store.
 *
 * The receiving keyset certs will either increase reference counter
 * of the cert or make a deep copy, either way, the caller needs to
 * free the cert itself.
 *
 * @param context a hx509 context.
 * @param certs certificate store to add the certificate to.
 * @param cert certificate to add.
 *
 * @return Returns an hx509 error code.
 *
 * @ingroup hx509_keyset
 */

int
hx509_certs_add(hx509_context context, hx509_certs certs, hx509_cert cert)
{
    if (certs->ops->add == NULL) {
	hx509_set_error_string(context, 0, ENOENT,
			       "Keyset type %s doesn't support add operation",
			       certs->ops->name);
	return ENOENT;
    }

    return (*certs->ops->add)(context, certs, certs->ops_data, cert);
}

/**
 * Find a certificate matching the query.
 *
 * @param context a hx509 context.
 * @param certs certificate store to search.
 * @param q query allocated with @ref hx509_query functions.
 * @param r return certificate (or NULL on error), should be freed
 * with hx509_cert_free().
 *
 * @return Returns an hx509 error code.
 *
 * @ingroup hx509_keyset
 */

int
hx509_certs_find(hx509_context context,
		 hx509_certs certs,
		 const hx509_query *q,
		 hx509_cert *r)
{
    hx509_cursor cursor;
    hx509_cert c;
    int ret;

    *r = NULL;

    _hx509_query_statistic(context, 0, q);

    if (certs->ops->query)
	return (*certs->ops->query)(context, certs, certs->ops_data, q, r);

    ret = hx509_certs_start_seq(context, certs, &cursor);
    if (ret)
	return ret;

    c = NULL;
    while (1) {
	ret = hx509_certs_next_cert(context, certs, cursor, &c);
	if (ret)
	    break;
	if (c == NULL)
	    break;
	if (_hx509_query_match_cert(context, q, c)) {
	    *r = c;
	    break;
	}
	hx509_cert_free(c);
    }

    hx509_certs_end_seq(context, certs, cursor);
    if (ret)
	return ret;
    /**
     * Return HX509_CERT_NOT_FOUND if no certificate in certs matched
     * the query.
     */
    if (c == NULL) {
	hx509_clear_error_string(context);
	return HX509_CERT_NOT_FOUND;
    }

    return 0;
}

/**
 * Filter certificate matching the query.
 *
 * @param context a hx509 context.
 * @param certs certificate store to search.
 * @param q query allocated with @ref hx509_query functions.
 * @param result the filtered certificate store, caller must free with
 *        hx509_certs_free().
 *
 * @return Returns an hx509 error code.
 *
 * @ingroup hx509_keyset
 */

int
hx509_certs_filter(hx509_context context,
		   hx509_certs certs,
		   const hx509_query *q,
		   hx509_certs *result)
{
    hx509_cursor cursor;
    hx509_cert c;
    int ret, found = 0;

    _hx509_query_statistic(context, 0, q);

    ret = hx509_certs_init(context, "MEMORY:filter-certs", 0,
			   NULL, result);
    if (ret)
	return ret;

    ret = hx509_certs_start_seq(context, certs, &cursor);
    if (ret) {
	hx509_certs_free(result);
	return ret;
    }

    c = NULL;
    while (1) {
	ret = hx509_certs_next_cert(context, certs, cursor, &c);
	if (ret)
	    break;
	if (c == NULL)
	    break;
	if (_hx509_query_match_cert(context, q, c)) {
	    hx509_certs_add(context, *result, c);
	    found = 1;
	}
	hx509_cert_free(c);
    }

    hx509_certs_end_seq(context, certs, cursor);
    if (ret) {
	hx509_certs_free(result);
	return ret;
    }

    /**
     * Return HX509_CERT_NOT_FOUND if no certificate in certs matched
     * the query.
     */
    if (!found) {
	hx509_certs_free(result);
	hx509_clear_error_string(context);
	return HX509_CERT_NOT_FOUND;
    }

    return 0;
}


static int
certs_merge_func(hx509_context context, void *ctx, hx509_cert c)
{
    return hx509_certs_add(context, (hx509_certs)ctx, c);
}

/**
 * Merge a certificate store into another. The from store is keep
 * intact.
 *
 * @param context a hx509 context.
 * @param to the store to merge into.
 * @param from the store to copy the object from.
 *
 * @return Returns an hx509 error code.
 *
 * @ingroup hx509_keyset
 */

int
hx509_certs_merge(hx509_context context, hx509_certs to, hx509_certs from)
{
    if (from == NULL)
	return 0;
    return hx509_certs_iter_f(context, from, certs_merge_func, to);
}

/**
 * Same a hx509_certs_merge() but use a lock and name to describe the
 * from source.
 *
 * @param context a hx509 context.
 * @param to the store to merge into.
 * @param lock a lock that unlocks the certificates store, use NULL to
 * select no password/certifictes/prompt lock (see @ref page_lock).
 * @param name name of the source store
 *
 * @return Returns an hx509 error code.
 *
 * @ingroup hx509_keyset
 */

int
hx509_certs_append(hx509_context context,
		   hx509_certs to,
		   hx509_lock lock,
		   const char *name)
{
    hx509_certs s;
    int ret;

    ret = hx509_certs_init(context, name, 0, lock, &s);
    if (ret)
	return ret;
    ret = hx509_certs_merge(context, to, s);
    hx509_certs_free(&s);
    return ret;
}

/**
 * Get one random certificate from the certificate store.
 *
 * @param context a hx509 context.
 * @param certs a certificate store to get the certificate from.
 * @param c return certificate, should be freed with hx509_cert_free().
 *
 * @return Returns an hx509 error code.
 *
 * @ingroup hx509_keyset
 */

int
hx509_get_one_cert(hx509_context context, hx509_certs certs, hx509_cert *c)
{
    hx509_cursor cursor;
    int ret;

    *c = NULL;

    ret = hx509_certs_start_seq(context, certs, &cursor);
    if (ret)
	return ret;

    ret = hx509_certs_next_cert(context, certs, cursor, c);
    if (ret)
	return ret;

    hx509_certs_end_seq(context, certs, cursor);
    return 0;
}

static int
certs_info_stdio(void *ctx, const char *str)
{
    FILE *f = ctx;
    fprintf(f, "%s\n", str);
    return 0;
}

/**
 * Print some info about the certificate store.
 *
 * @param context a hx509 context.
 * @param certs certificate store to print information about.
 * @param func function that will get each line of the information, if
 * NULL is used the data is printed on a FILE descriptor that should
 * be passed in ctx, if ctx also is NULL, stdout is used.
 * @param ctx parameter to func.
 *
 * @return Returns an hx509 error code.
 *
 * @ingroup hx509_keyset
 */

int
hx509_certs_info(hx509_context context,
		 hx509_certs certs,
		 int (*func)(void *, const char *),
		 void *ctx)
{
    if (func == NULL) {
	func = certs_info_stdio;
	if (ctx == NULL)
	    ctx = stdout;
    }
    if (certs->ops->printinfo == NULL) {
	(*func)(ctx, "No info function for certs");
	return 0;
    }
    return (*certs->ops->printinfo)(context, certs, certs->ops_data,
				    func, ctx);
}

void
_hx509_pi_printf(int (*func)(void *, const char *), void *ctx,
		 const char *fmt, ...)
{
    va_list ap;
    char *str;

    va_start(ap, fmt);
    vasprintf(&str, fmt, ap);
    va_end(ap);
    if (str == NULL)
	return;
    (*func)(ctx, str);
    free(str);
}

int
_hx509_certs_keys_get(hx509_context context,
		      hx509_certs certs,
		      hx509_private_key **keys)
{
    if (certs->ops->getkeys == NULL) {
	*keys = NULL;
	return 0;
    }
    return (*certs->ops->getkeys)(context, certs, certs->ops_data, keys);
}

int
_hx509_certs_keys_add(hx509_context context,
		      hx509_certs certs,
		      hx509_private_key key)
{
    if (certs->ops->addkey == NULL) {
	hx509_set_error_string(context, 0, EINVAL,
			       "keystore if type %s doesn't support "
			       "key add operation",
			       certs->ops->name);
	return EINVAL;
    }
    return (*certs->ops->addkey)(context, certs, certs->ops_data, key);
}


void
_hx509_certs_keys_free(hx509_context context,
		       hx509_private_key *keys)
{
    int i;
    for (i = 0; keys[i]; i++)
	hx509_private_key_free(&keys[i]);
    free(keys);
}
