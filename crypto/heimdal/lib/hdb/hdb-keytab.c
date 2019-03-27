/*
 * Copyright (c) 2009 Kungliga Tekniska Högskolan
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

#include "hdb_locl.h"
#include <assert.h>

typedef struct {
    char *path;
    krb5_keytab keytab;
} *hdb_keytab;

/*
 *
 */

static krb5_error_code
hkt_close(krb5_context context, HDB *db)
{
    hdb_keytab k = (hdb_keytab)db->hdb_db;
    krb5_error_code ret;

    assert(k->keytab);

    ret = krb5_kt_close(context, k->keytab);
    k->keytab = NULL;

    return ret;
}

static krb5_error_code
hkt_destroy(krb5_context context, HDB *db)
{
    hdb_keytab k = (hdb_keytab)db->hdb_db;
    krb5_error_code ret;

    ret = hdb_clear_master_key (context, db);

    free(k->path);
    free(k);

    free(db->hdb_name);
    free(db);
    return ret;
}

static krb5_error_code
hkt_lock(krb5_context context, HDB *db, int operation)
{
    return 0;
}

static krb5_error_code
hkt_unlock(krb5_context context, HDB *db)
{
    return 0;
}

static krb5_error_code
hkt_firstkey(krb5_context context, HDB *db,
	     unsigned flags, hdb_entry_ex *entry)
{
    return HDB_ERR_DB_INUSE;
}

static krb5_error_code
hkt_nextkey(krb5_context context, HDB * db, unsigned flags,
	     hdb_entry_ex * entry)
{
    return HDB_ERR_DB_INUSE;
}

static krb5_error_code
hkt_open(krb5_context context, HDB * db, int flags, mode_t mode)
{
    hdb_keytab k = (hdb_keytab)db->hdb_db;
    krb5_error_code ret;

    assert(k->keytab == NULL);

    ret = krb5_kt_resolve(context, k->path, &k->keytab);
    if (ret)
	return ret;

    return 0;
}

static krb5_error_code
hkt_fetch_kvno(krb5_context context, HDB * db, krb5_const_principal principal,
	       unsigned flags, krb5_kvno kvno, hdb_entry_ex * entry)
{
    hdb_keytab k = (hdb_keytab)db->hdb_db;
    krb5_error_code ret;
    krb5_keytab_entry ktentry;

    if (!(flags & HDB_F_KVNO_SPECIFIED)) {
	    /* Preserve previous behaviour if no kvno specified */
	    kvno = 0;
    }

    memset(&ktentry, 0, sizeof(ktentry));

    entry->entry.flags.server = 1;
    entry->entry.flags.forwardable = 1;
    entry->entry.flags.renewable = 1;

    /* Not recorded in the OD backend, make something up */
    ret = krb5_parse_name(context, "hdb/keytab@WELL-KNOWN:KEYTAB-BACKEND",
			  &entry->entry.created_by.principal);
    if (ret)
	goto out;

    /*
     * XXX really needs to try all enctypes and just not pick the
     * first one, even if that happens to be des3-cbc-sha1 (ie best
     * enctype) in the Apple case. A while loop over all known
     * enctypes should work.
     */

    ret = krb5_kt_get_entry(context, k->keytab, principal, kvno, 0, &ktentry);
    if (ret) {
	ret = HDB_ERR_NOENTRY;
	goto out;
    }

    ret = krb5_copy_principal(context, principal, &entry->entry.principal);
    if (ret)
	goto out;

    ret = _hdb_keytab2hdb_entry(context, &ktentry, entry);

 out:
    if (ret) {
	free_hdb_entry(&entry->entry);
	memset(&entry->entry, 0, sizeof(entry->entry));
    }
    krb5_kt_free_entry(context, &ktentry);

    return ret;
}

static krb5_error_code
hkt_store(krb5_context context, HDB * db, unsigned flags,
	  hdb_entry_ex * entry)
{
    return HDB_ERR_DB_INUSE;
}


krb5_error_code
hdb_keytab_create(krb5_context context, HDB ** db, const char *arg)
{
    hdb_keytab k;

    *db = calloc(1, sizeof(**db));
    if (*db == NULL) {
	krb5_set_error_message(context, ENOMEM, "malloc: out of memory");
	return ENOMEM;
    }
    memset(*db, 0, sizeof(**db));

    k = calloc(1, sizeof(*k));
    if (k == NULL) {
	free(*db);
	*db = NULL;
	krb5_set_error_message(context, ENOMEM, "malloc: out of memory");
	return ENOMEM;
    }

    k->path = strdup(arg);
    if (k->path == NULL) {
	free(k);
	free(*db);
	*db = NULL;
	krb5_set_error_message(context, ENOMEM, "malloc: out of memory");
	return ENOMEM;
    }


    (*db)->hdb_db = k;

    (*db)->hdb_master_key_set = 0;
    (*db)->hdb_openp = 0;
    (*db)->hdb_open = hkt_open;
    (*db)->hdb_close = hkt_close;
    (*db)->hdb_fetch_kvno = hkt_fetch_kvno;
    (*db)->hdb_store = hkt_store;
    (*db)->hdb_remove = NULL;
    (*db)->hdb_firstkey = hkt_firstkey;
    (*db)->hdb_nextkey = hkt_nextkey;
    (*db)->hdb_lock = hkt_lock;
    (*db)->hdb_unlock = hkt_unlock;
    (*db)->hdb_rename = NULL;
    (*db)->hdb__get = NULL;
    (*db)->hdb__put = NULL;
    (*db)->hdb__del = NULL;
    (*db)->hdb_destroy = hkt_destroy;

    return 0;
}
