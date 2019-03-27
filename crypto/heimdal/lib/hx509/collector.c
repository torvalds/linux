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

struct private_key {
    AlgorithmIdentifier alg;
    hx509_private_key private_key;
    heim_octet_string localKeyId;
};

struct hx509_collector {
    hx509_lock lock;
    hx509_certs unenvelop_certs;
    hx509_certs certs;
    struct {
	struct private_key **data;
	size_t len;
    } val;
};


int
_hx509_collector_alloc(hx509_context context, hx509_lock lock, struct hx509_collector **collector)
{
    struct hx509_collector *c;
    int ret;

    *collector = NULL;

    c = calloc(1, sizeof(*c));
    if (c == NULL) {
	hx509_set_error_string(context, 0, ENOMEM, "out of memory");
	return ENOMEM;
    }
    c->lock = lock;

    ret = hx509_certs_init(context, "MEMORY:collector-unenvelop-cert",
			   0,NULL, &c->unenvelop_certs);
    if (ret) {
	free(c);
	return ret;
    }
    c->val.data = NULL;
    c->val.len = 0;
    ret = hx509_certs_init(context, "MEMORY:collector-tmp-store",
			   0, NULL, &c->certs);
    if (ret) {
	hx509_certs_free(&c->unenvelop_certs);
	free(c);
	return ret;
    }

    *collector = c;
    return 0;
}

hx509_lock
_hx509_collector_get_lock(struct hx509_collector *c)
{
    return c->lock;
}


int
_hx509_collector_certs_add(hx509_context context,
			   struct hx509_collector *c,
			   hx509_cert cert)
{
    return hx509_certs_add(context, c->certs, cert);
}

static void
free_private_key(struct private_key *key)
{
    free_AlgorithmIdentifier(&key->alg);
    if (key->private_key)
	hx509_private_key_free(&key->private_key);
    der_free_octet_string(&key->localKeyId);
    free(key);
}

int
_hx509_collector_private_key_add(hx509_context context,
				 struct hx509_collector *c,
				 const AlgorithmIdentifier *alg,
				 hx509_private_key private_key,
				 const heim_octet_string *key_data,
				 const heim_octet_string *localKeyId)
{
    struct private_key *key;
    void *d;
    int ret;

    key = calloc(1, sizeof(*key));
    if (key == NULL)
	return ENOMEM;

    d = realloc(c->val.data, (c->val.len + 1) * sizeof(c->val.data[0]));
    if (d == NULL) {
	free(key);
	hx509_set_error_string(context, 0, ENOMEM, "Out of memory");
	return ENOMEM;
    }
    c->val.data = d;

    ret = copy_AlgorithmIdentifier(alg, &key->alg);
    if (ret) {
	hx509_set_error_string(context, 0, ret, "Failed to copy "
			       "AlgorithmIdentifier");
	goto out;
    }
    if (private_key) {
	key->private_key = private_key;
    } else {
	ret = hx509_parse_private_key(context, alg,
				       key_data->data, key_data->length,
				       HX509_KEY_FORMAT_DER,
				       &key->private_key);
	if (ret)
	    goto out;
    }
    if (localKeyId) {
	ret = der_copy_octet_string(localKeyId, &key->localKeyId);
	if (ret) {
	    hx509_set_error_string(context, 0, ret,
				   "Failed to copy localKeyId");
	    goto out;
	}
    } else
	memset(&key->localKeyId, 0, sizeof(key->localKeyId));

    c->val.data[c->val.len] = key;
    c->val.len++;

out:
    if (ret)
	free_private_key(key);

    return ret;
}

static int
match_localkeyid(hx509_context context,
		 struct private_key *value,
		 hx509_certs certs)
{
    hx509_cert cert;
    hx509_query q;
    int ret;

    if (value->localKeyId.length == 0) {
	hx509_set_error_string(context, 0, HX509_LOCAL_ATTRIBUTE_MISSING,
			       "No local key attribute on private key");
	return HX509_LOCAL_ATTRIBUTE_MISSING;
    }

    _hx509_query_clear(&q);
    q.match |= HX509_QUERY_MATCH_LOCAL_KEY_ID;

    q.local_key_id = &value->localKeyId;

    ret = hx509_certs_find(context, certs, &q, &cert);
    if (ret == 0) {

	if (value->private_key)
	    _hx509_cert_assign_key(cert, value->private_key);
	hx509_cert_free(cert);
    }
    return ret;
}

static int
match_keys(hx509_context context, struct private_key *value, hx509_certs certs)
{
    hx509_cursor cursor;
    hx509_cert c;
    int ret, found = HX509_CERT_NOT_FOUND;

    if (value->private_key == NULL) {
	hx509_set_error_string(context, 0, HX509_PRIVATE_KEY_MISSING,
			       "No private key to compare with");
	return HX509_PRIVATE_KEY_MISSING;
    }

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
	if (_hx509_cert_private_key(c)) {
	    hx509_cert_free(c);
	    continue;
	}

	ret = _hx509_match_keys(c, value->private_key);
	if (ret) {
	    _hx509_cert_assign_key(c, value->private_key);
	    hx509_cert_free(c);
	    found = 0;
	    break;
	}
	hx509_cert_free(c);
    }

    hx509_certs_end_seq(context, certs, cursor);

    if (found)
	hx509_clear_error_string(context);

    return found;
}

int
_hx509_collector_collect_certs(hx509_context context,
			       struct hx509_collector *c,
			       hx509_certs *ret_certs)
{
    hx509_certs certs;
    int ret;
    size_t i;

    *ret_certs = NULL;

    ret = hx509_certs_init(context, "MEMORY:collector-store", 0, NULL, &certs);
    if (ret)
	return ret;

    ret = hx509_certs_merge(context, certs, c->certs);
    if (ret) {
	hx509_certs_free(&certs);
	return ret;
    }

    for (i = 0; i < c->val.len; i++) {
	ret = match_localkeyid(context, c->val.data[i], certs);
	if (ret == 0)
	    continue;
	ret = match_keys(context, c->val.data[i], certs);
	if (ret == 0)
	    continue;
    }

    *ret_certs = certs;

    return 0;
}

int
_hx509_collector_collect_private_keys(hx509_context context,
				      struct hx509_collector *c,
				      hx509_private_key **keys)
{
    size_t i, nkeys;

    *keys = NULL;

    for (i = 0, nkeys = 0; i < c->val.len; i++)
	if (c->val.data[i]->private_key)
	    nkeys++;

    *keys = calloc(nkeys + 1, sizeof(**keys));
    if (*keys == NULL) {
	hx509_set_error_string(context, 0, ENOMEM, "malloc - out of memory");
	return ENOMEM;
    }

    for (i = 0, nkeys = 0; i < c->val.len; i++) {
 	if (c->val.data[i]->private_key) {
	    (*keys)[nkeys++] = c->val.data[i]->private_key;
	    c->val.data[i]->private_key = NULL;
	}
    }
    (*keys)[nkeys] = NULL;

    return 0;
}


void
_hx509_collector_free(struct hx509_collector *c)
{
    size_t i;

    if (c->unenvelop_certs)
	hx509_certs_free(&c->unenvelop_certs);
    if (c->certs)
	hx509_certs_free(&c->certs);
    for (i = 0; i < c->val.len; i++)
	free_private_key(c->val.data[i]);
    if (c->val.data)
	free(c->val.data);
    free(c);
}
