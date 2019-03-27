/*
 * Copyright (c) 1997 - 2001 Kungliga Tekniska Högskolan
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

#include "krb5_locl.h"

/* memory operations -------------------------------------------- */

struct mkt_data {
    krb5_keytab_entry *entries;
    int num_entries;
    char *name;
    int refcount;
    struct mkt_data *next;
};

/* this mutex protects mkt_head, ->refcount, and ->next
 * content is not protected (name is static and need no protection)
 */
static HEIMDAL_MUTEX mkt_mutex = HEIMDAL_MUTEX_INITIALIZER;
static struct mkt_data *mkt_head;


static krb5_error_code KRB5_CALLCONV
mkt_resolve(krb5_context context, const char *name, krb5_keytab id)
{
    struct mkt_data *d;

    HEIMDAL_MUTEX_lock(&mkt_mutex);

    for (d = mkt_head; d != NULL; d = d->next)
	if (strcmp(d->name, name) == 0)
	    break;
    if (d) {
	if (d->refcount < 1)
	    krb5_abortx(context, "Double close on memory keytab, "
			"refcount < 1 %d", d->refcount);
	d->refcount++;
	id->data = d;
	HEIMDAL_MUTEX_unlock(&mkt_mutex);
	return 0;
    }

    d = calloc(1, sizeof(*d));
    if(d == NULL) {
	HEIMDAL_MUTEX_unlock(&mkt_mutex);
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    d->name = strdup(name);
    if (d->name == NULL) {
	HEIMDAL_MUTEX_unlock(&mkt_mutex);
	free(d);
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    d->entries = NULL;
    d->num_entries = 0;
    d->refcount = 1;
    d->next = mkt_head;
    mkt_head = d;
    HEIMDAL_MUTEX_unlock(&mkt_mutex);
    id->data = d;
    return 0;
}

static krb5_error_code KRB5_CALLCONV
mkt_close(krb5_context context, krb5_keytab id)
{
    struct mkt_data *d = id->data, **dp;
    int i;

    HEIMDAL_MUTEX_lock(&mkt_mutex);
    if (d->refcount < 1)
	krb5_abortx(context,
		    "krb5 internal error, memory keytab refcount < 1 on close");

    if (--d->refcount > 0) {
	HEIMDAL_MUTEX_unlock(&mkt_mutex);
	return 0;
    }
    for (dp = &mkt_head; *dp != NULL; dp = &(*dp)->next) {
	if (*dp == d) {
	    *dp = d->next;
	    break;
	}
    }
    HEIMDAL_MUTEX_unlock(&mkt_mutex);

    free(d->name);
    for(i = 0; i < d->num_entries; i++)
	krb5_kt_free_entry(context, &d->entries[i]);
    free(d->entries);
    free(d);
    return 0;
}

static krb5_error_code KRB5_CALLCONV
mkt_get_name(krb5_context context,
	     krb5_keytab id,
	     char *name,
	     size_t namesize)
{
    struct mkt_data *d = id->data;
    strlcpy(name, d->name, namesize);
    return 0;
}

static krb5_error_code KRB5_CALLCONV
mkt_start_seq_get(krb5_context context,
		  krb5_keytab id,
		  krb5_kt_cursor *c)
{
    /* XXX */
    c->fd = 0;
    return 0;
}

static krb5_error_code KRB5_CALLCONV
mkt_next_entry(krb5_context context,
	       krb5_keytab id,
	       krb5_keytab_entry *entry,
	       krb5_kt_cursor *c)
{
    struct mkt_data *d = id->data;
    if(c->fd >= d->num_entries)
	return KRB5_KT_END;
    return krb5_kt_copy_entry_contents(context, &d->entries[c->fd++], entry);
}

static krb5_error_code KRB5_CALLCONV
mkt_end_seq_get(krb5_context context,
		krb5_keytab id,
		krb5_kt_cursor *cursor)
{
    return 0;
}

static krb5_error_code KRB5_CALLCONV
mkt_add_entry(krb5_context context,
	      krb5_keytab id,
	      krb5_keytab_entry *entry)
{
    struct mkt_data *d = id->data;
    krb5_keytab_entry *tmp;
    tmp = realloc(d->entries, (d->num_entries + 1) * sizeof(*d->entries));
    if(tmp == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    d->entries = tmp;
    return krb5_kt_copy_entry_contents(context, entry,
				       &d->entries[d->num_entries++]);
}

static krb5_error_code KRB5_CALLCONV
mkt_remove_entry(krb5_context context,
		 krb5_keytab id,
		 krb5_keytab_entry *entry)
{
    struct mkt_data *d = id->data;
    krb5_keytab_entry *e, *end;
    int found = 0;

    if (d->num_entries == 0) {
	krb5_clear_error_message(context);
        return KRB5_KT_NOTFOUND;
    }

    /* do this backwards to minimize copying */
    for(end = d->entries + d->num_entries, e = end - 1; e >= d->entries; e--) {
	if(krb5_kt_compare(context, e, entry->principal,
			   entry->vno, entry->keyblock.keytype)) {
	    krb5_kt_free_entry(context, e);
	    memmove(e, e + 1, (end - e - 1) * sizeof(*e));
	    memset(end - 1, 0, sizeof(*end));
	    d->num_entries--;
	    end--;
	    found = 1;
	}
    }
    if (!found) {
	krb5_clear_error_message (context);
	return KRB5_KT_NOTFOUND;
    }
    e = realloc(d->entries, d->num_entries * sizeof(*d->entries));
    if(e != NULL || d->num_entries == 0)
	d->entries = e;
    return 0;
}

const krb5_kt_ops krb5_mkt_ops = {
    "MEMORY",
    mkt_resolve,
    mkt_get_name,
    mkt_close,
    NULL, /* destroy */
    NULL, /* get */
    mkt_start_seq_get,
    mkt_next_entry,
    mkt_end_seq_get,
    mkt_add_entry,
    mkt_remove_entry
};
