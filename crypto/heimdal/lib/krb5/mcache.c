/*
 * Copyright (c) 1997-2004 Kungliga Tekniska Högskolan
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

#include "krb5_locl.h"

typedef struct krb5_mcache {
    char *name;
    unsigned int refcnt;
    int dead;
    krb5_principal primary_principal;
    struct link {
	krb5_creds cred;
	struct link *next;
    } *creds;
    struct krb5_mcache *next;
    time_t mtime;
    krb5_deltat kdc_offset;
} krb5_mcache;

static HEIMDAL_MUTEX mcc_mutex = HEIMDAL_MUTEX_INITIALIZER;
static struct krb5_mcache *mcc_head;

#define	MCACHE(X)	((krb5_mcache *)(X)->data.data)

#define MISDEAD(X)	((X)->dead)

static const char* KRB5_CALLCONV
mcc_get_name(krb5_context context,
	     krb5_ccache id)
{
    return MCACHE(id)->name;
}

static krb5_mcache * KRB5_CALLCONV
mcc_alloc(const char *name)
{
    krb5_mcache *m, *m_c;
    int ret = 0;

    ALLOC(m, 1);
    if(m == NULL)
	return NULL;
    if(name == NULL)
	ret = asprintf(&m->name, "%p", m);
    else
	m->name = strdup(name);
    if(ret < 0 || m->name == NULL) {
	free(m);
	return NULL;
    }
    /* check for dups first */
    HEIMDAL_MUTEX_lock(&mcc_mutex);
    for (m_c = mcc_head; m_c != NULL; m_c = m_c->next)
	if (strcmp(m->name, m_c->name) == 0)
	    break;
    if (m_c) {
	free(m->name);
	free(m);
	HEIMDAL_MUTEX_unlock(&mcc_mutex);
	return NULL;
    }

    m->dead = 0;
    m->refcnt = 1;
    m->primary_principal = NULL;
    m->creds = NULL;
    m->mtime = time(NULL);
    m->kdc_offset = 0;
    m->next = mcc_head;
    mcc_head = m;
    HEIMDAL_MUTEX_unlock(&mcc_mutex);
    return m;
}

static krb5_error_code KRB5_CALLCONV
mcc_resolve(krb5_context context, krb5_ccache *id, const char *res)
{
    krb5_mcache *m;

    HEIMDAL_MUTEX_lock(&mcc_mutex);
    for (m = mcc_head; m != NULL; m = m->next)
	if (strcmp(m->name, res) == 0)
	    break;
    HEIMDAL_MUTEX_unlock(&mcc_mutex);

    if (m != NULL) {
	m->refcnt++;
	(*id)->data.data = m;
	(*id)->data.length = sizeof(*m);
	return 0;
    }

    m = mcc_alloc(res);
    if (m == NULL) {
	krb5_set_error_message(context, KRB5_CC_NOMEM,
			       N_("malloc: out of memory", ""));
	return KRB5_CC_NOMEM;
    }

    (*id)->data.data = m;
    (*id)->data.length = sizeof(*m);

    return 0;
}


static krb5_error_code KRB5_CALLCONV
mcc_gen_new(krb5_context context, krb5_ccache *id)
{
    krb5_mcache *m;

    m = mcc_alloc(NULL);

    if (m == NULL) {
	krb5_set_error_message(context, KRB5_CC_NOMEM,
			       N_("malloc: out of memory", ""));
	return KRB5_CC_NOMEM;
    }

    (*id)->data.data = m;
    (*id)->data.length = sizeof(*m);

    return 0;
}

static krb5_error_code KRB5_CALLCONV
mcc_initialize(krb5_context context,
	       krb5_ccache id,
	       krb5_principal primary_principal)
{
    krb5_mcache *m = MCACHE(id);
    m->dead = 0;
    m->mtime = time(NULL);
    return krb5_copy_principal (context,
				primary_principal,
				&m->primary_principal);
}

static int
mcc_close_internal(krb5_mcache *m)
{
    if (--m->refcnt != 0)
	return 0;

    if (MISDEAD(m)) {
	free (m->name);
	return 1;
    }
    return 0;
}

static krb5_error_code KRB5_CALLCONV
mcc_close(krb5_context context,
	  krb5_ccache id)
{
    if (mcc_close_internal(MCACHE(id)))
	krb5_data_free(&id->data);
    return 0;
}

static krb5_error_code KRB5_CALLCONV
mcc_destroy(krb5_context context,
	    krb5_ccache id)
{
    krb5_mcache **n, *m = MCACHE(id);
    struct link *l;

    if (m->refcnt == 0)
	krb5_abortx(context, "mcc_destroy: refcnt already 0");

    if (!MISDEAD(m)) {
	/* if this is an active mcache, remove it from the linked
           list, and free all data */
	HEIMDAL_MUTEX_lock(&mcc_mutex);
	for(n = &mcc_head; n && *n; n = &(*n)->next) {
	    if(m == *n) {
		*n = m->next;
		break;
	    }
	}
	HEIMDAL_MUTEX_unlock(&mcc_mutex);
	if (m->primary_principal != NULL) {
	    krb5_free_principal (context, m->primary_principal);
	    m->primary_principal = NULL;
	}
	m->dead = 1;

	l = m->creds;
	while (l != NULL) {
	    struct link *old;

	    krb5_free_cred_contents (context, &l->cred);
	    old = l;
	    l = l->next;
	    free (old);
	}
	m->creds = NULL;
    }
    return 0;
}

static krb5_error_code KRB5_CALLCONV
mcc_store_cred(krb5_context context,
	       krb5_ccache id,
	       krb5_creds *creds)
{
    krb5_mcache *m = MCACHE(id);
    krb5_error_code ret;
    struct link *l;

    if (MISDEAD(m))
	return ENOENT;

    l = malloc (sizeof(*l));
    if (l == NULL) {
	krb5_set_error_message(context, KRB5_CC_NOMEM,
			       N_("malloc: out of memory", ""));
	return KRB5_CC_NOMEM;
    }
    l->next = m->creds;
    m->creds = l;
    memset (&l->cred, 0, sizeof(l->cred));
    ret = krb5_copy_creds_contents (context, creds, &l->cred);
    if (ret) {
	m->creds = l->next;
	free (l);
	return ret;
    }
    m->mtime = time(NULL);
    return 0;
}

static krb5_error_code KRB5_CALLCONV
mcc_get_principal(krb5_context context,
		  krb5_ccache id,
		  krb5_principal *principal)
{
    krb5_mcache *m = MCACHE(id);

    if (MISDEAD(m) || m->primary_principal == NULL)
	return ENOENT;
    return krb5_copy_principal (context,
				m->primary_principal,
				principal);
}

static krb5_error_code KRB5_CALLCONV
mcc_get_first (krb5_context context,
	       krb5_ccache id,
	       krb5_cc_cursor *cursor)
{
    krb5_mcache *m = MCACHE(id);

    if (MISDEAD(m))
	return ENOENT;

    *cursor = m->creds;
    return 0;
}

static krb5_error_code KRB5_CALLCONV
mcc_get_next (krb5_context context,
	      krb5_ccache id,
	      krb5_cc_cursor *cursor,
	      krb5_creds *creds)
{
    krb5_mcache *m = MCACHE(id);
    struct link *l;

    if (MISDEAD(m))
	return ENOENT;

    l = *cursor;
    if (l != NULL) {
	*cursor = l->next;
	return krb5_copy_creds_contents (context,
					 &l->cred,
					 creds);
    } else
	return KRB5_CC_END;
}

static krb5_error_code KRB5_CALLCONV
mcc_end_get (krb5_context context,
	     krb5_ccache id,
	     krb5_cc_cursor *cursor)
{
    return 0;
}

static krb5_error_code KRB5_CALLCONV
mcc_remove_cred(krb5_context context,
		 krb5_ccache id,
		 krb5_flags which,
		 krb5_creds *mcreds)
{
    krb5_mcache *m = MCACHE(id);
    struct link **q, *p;
    for(q = &m->creds, p = *q; p; p = *q) {
	if(krb5_compare_creds(context, which, mcreds, &p->cred)) {
	    *q = p->next;
	    krb5_free_cred_contents(context, &p->cred);
	    free(p);
	    m->mtime = time(NULL);
	} else
	    q = &p->next;
    }
    return 0;
}

static krb5_error_code KRB5_CALLCONV
mcc_set_flags(krb5_context context,
	      krb5_ccache id,
	      krb5_flags flags)
{
    return 0; /* XXX */
}

struct mcache_iter {
    krb5_mcache *cache;
};

static krb5_error_code KRB5_CALLCONV
mcc_get_cache_first(krb5_context context, krb5_cc_cursor *cursor)
{
    struct mcache_iter *iter;

    iter = calloc(1, sizeof(*iter));
    if (iter == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }

    HEIMDAL_MUTEX_lock(&mcc_mutex);
    iter->cache = mcc_head;
    if (iter->cache)
	iter->cache->refcnt++;
    HEIMDAL_MUTEX_unlock(&mcc_mutex);

    *cursor = iter;
    return 0;
}

static krb5_error_code KRB5_CALLCONV
mcc_get_cache_next(krb5_context context, krb5_cc_cursor cursor, krb5_ccache *id)
{
    struct mcache_iter *iter = cursor;
    krb5_error_code ret;
    krb5_mcache *m;

    if (iter->cache == NULL)
	return KRB5_CC_END;

    HEIMDAL_MUTEX_lock(&mcc_mutex);
    m = iter->cache;
    if (m->next)
	m->next->refcnt++;
    iter->cache = m->next;
    HEIMDAL_MUTEX_unlock(&mcc_mutex);

    ret = _krb5_cc_allocate(context, &krb5_mcc_ops, id);
    if (ret)
	return ret;

    (*id)->data.data = m;
    (*id)->data.length = sizeof(*m);

    return 0;
}

static krb5_error_code KRB5_CALLCONV
mcc_end_cache_get(krb5_context context, krb5_cc_cursor cursor)
{
    struct mcache_iter *iter = cursor;

    if (iter->cache)
	mcc_close_internal(iter->cache);
    iter->cache = NULL;
    free(iter);
    return 0;
}

static krb5_error_code KRB5_CALLCONV
mcc_move(krb5_context context, krb5_ccache from, krb5_ccache to)
{
    krb5_mcache *mfrom = MCACHE(from), *mto = MCACHE(to);
    struct link *creds;
    krb5_principal principal;
    krb5_mcache **n;

    HEIMDAL_MUTEX_lock(&mcc_mutex);

    /* drop the from cache from the linked list to avoid lookups */
    for(n = &mcc_head; n && *n; n = &(*n)->next) {
	if(mfrom == *n) {
	    *n = mfrom->next;
	    break;
	}
    }

    /* swap creds */
    creds = mto->creds;
    mto->creds = mfrom->creds;
    mfrom->creds = creds;
    /* swap principal */
    principal = mto->primary_principal;
    mto->primary_principal = mfrom->primary_principal;
    mfrom->primary_principal = principal;

    mto->mtime = mfrom->mtime = time(NULL);

    HEIMDAL_MUTEX_unlock(&mcc_mutex);
    mcc_destroy(context, from);

    return 0;
}

static krb5_error_code KRB5_CALLCONV
mcc_default_name(krb5_context context, char **str)
{
    *str = strdup("MEMORY:");
    if (*str == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    return 0;
}

static krb5_error_code KRB5_CALLCONV
mcc_lastchange(krb5_context context, krb5_ccache id, krb5_timestamp *mtime)
{
    *mtime = MCACHE(id)->mtime;
    return 0;
}

static krb5_error_code KRB5_CALLCONV
mcc_set_kdc_offset(krb5_context context, krb5_ccache id, krb5_deltat kdc_offset)
{
    krb5_mcache *m = MCACHE(id);
    m->kdc_offset = kdc_offset;
    return 0;
}

static krb5_error_code KRB5_CALLCONV
mcc_get_kdc_offset(krb5_context context, krb5_ccache id, krb5_deltat *kdc_offset)
{
    krb5_mcache *m = MCACHE(id);
    *kdc_offset = m->kdc_offset;
    return 0;
}


/**
 * Variable containing the MEMORY based credential cache implemention.
 *
 * @ingroup krb5_ccache
 */

KRB5_LIB_VARIABLE const krb5_cc_ops krb5_mcc_ops = {
    KRB5_CC_OPS_VERSION,
    "MEMORY",
    mcc_get_name,
    mcc_resolve,
    mcc_gen_new,
    mcc_initialize,
    mcc_destroy,
    mcc_close,
    mcc_store_cred,
    NULL, /* mcc_retrieve */
    mcc_get_principal,
    mcc_get_first,
    mcc_get_next,
    mcc_end_get,
    mcc_remove_cred,
    mcc_set_flags,
    NULL,
    mcc_get_cache_first,
    mcc_get_cache_next,
    mcc_end_cache_get,
    mcc_move,
    mcc_default_name,
    NULL,
    mcc_lastchange,
    mcc_set_kdc_offset,
    mcc_get_kdc_offset
};
