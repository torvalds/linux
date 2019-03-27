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

#include "hdb_locl.h"

#if HAVE_NDBM

#if defined(HAVE_GDBM_NDBM_H)
#include <gdbm/ndbm.h>
#define WRITE_SUPPORT 1
#elif defined(HAVE_NDBM_H)
#include <ndbm.h>
#elif defined(HAVE_DBM_H)
#define WRITE_SUPPORT 1
#include <dbm.h>
#endif

struct ndbm_db {
    DBM *db;
    int lock_fd;
};

static krb5_error_code
NDBM_destroy(krb5_context context, HDB *db)
{
    hdb_clear_master_key (context, db);
    free(db->hdb_name);
    free(db);
    return 0;
}

static krb5_error_code
NDBM_lock(krb5_context context, HDB *db, int operation)
{
    struct ndbm_db *d = db->hdb_db;
    return hdb_lock(d->lock_fd, operation);
}

static krb5_error_code
NDBM_unlock(krb5_context context, HDB *db)
{
    struct ndbm_db *d = db->hdb_db;
    return hdb_unlock(d->lock_fd);
}

static krb5_error_code
NDBM_seq(krb5_context context, HDB *db,
	 unsigned flags, hdb_entry_ex *entry, int first)

{
    struct ndbm_db *d = (struct ndbm_db *)db->hdb_db;
    datum key, value;
    krb5_data key_data, data;
    krb5_error_code ret = 0;

    if(first)
	key = dbm_firstkey(d->db);
    else
	key = dbm_nextkey(d->db);
    if(key.dptr == NULL)
	return HDB_ERR_NOENTRY;
    key_data.data = key.dptr;
    key_data.length = key.dsize;
    ret = db->hdb_lock(context, db, HDB_RLOCK);
    if(ret) return ret;
    value = dbm_fetch(d->db, key);
    db->hdb_unlock(context, db);
    data.data = value.dptr;
    data.length = value.dsize;
    memset(entry, 0, sizeof(*entry));
    if(hdb_value2entry(context, &data, &entry->entry))
	return NDBM_seq(context, db, flags, entry, 0);
    if (db->hdb_master_key_set && (flags & HDB_F_DECRYPT)) {
	ret = hdb_unseal_keys (context, db, &entry->entry);
	if (ret)
	    hdb_free_entry (context, entry);
    }
    if (ret == 0 && entry->entry.principal == NULL) {
	entry->entry.principal = malloc (sizeof(*entry->entry.principal));
	if (entry->entry.principal == NULL) {
	    hdb_free_entry (context, entry);
	    ret = ENOMEM;
	    krb5_set_error_message(context, ret, "malloc: out of memory");
	} else {
	    hdb_key2principal (context, &key_data, entry->entry.principal);
	}
    }
    return ret;
}


static krb5_error_code
NDBM_firstkey(krb5_context context, HDB *db,unsigned flags,hdb_entry_ex *entry)
{
    return NDBM_seq(context, db, flags, entry, 1);
}


static krb5_error_code
NDBM_nextkey(krb5_context context, HDB *db, unsigned flags,hdb_entry_ex *entry)
{
    return NDBM_seq(context, db, flags, entry, 0);
}

static krb5_error_code
open_lock_file(krb5_context context, const char *db_name, int *fd)
{
    char *lock_file;

    /* lock old and new databases */
    asprintf(&lock_file, "%s.lock", db_name);
    if(lock_file == NULL) {
	krb5_set_error_message(context, ENOMEM, "malloc: out of memory");
	return ENOMEM;
    }

    *fd = open(lock_file, O_RDWR | O_CREAT, 0600);
    free(lock_file);
    if(*fd < 0) {
	int ret = errno;
	krb5_set_error_message(context, ret, "open(%s): %s", lock_file,
			       strerror(ret));
	return ret;
    }
    return 0;
}


static krb5_error_code
NDBM_rename(krb5_context context, HDB *db, const char *new_name)
{
    int ret;
    char *old_dir, *old_pag, *new_dir, *new_pag;
    int old_lock_fd, new_lock_fd;

    /* lock old and new databases */
    ret = open_lock_file(context, db->hdb_name, &old_lock_fd);
    if (ret)
	return ret;

    ret = hdb_lock(old_lock_fd, HDB_WLOCK);
    if(ret) {
	close(old_lock_fd);
	return ret;
    }

    ret = open_lock_file(context, new_name, &new_lock_fd);
    if (ret) {
	hdb_unlock(old_lock_fd);
	close(old_lock_fd);
	return ret;
    }

    ret = hdb_lock(new_lock_fd, HDB_WLOCK);
    if(ret) {
	hdb_unlock(old_lock_fd);
	close(old_lock_fd);
	close(new_lock_fd);
	return ret;
    }

    asprintf(&old_dir, "%s.dir", db->hdb_name);
    asprintf(&old_pag, "%s.pag", db->hdb_name);
    asprintf(&new_dir, "%s.dir", new_name);
    asprintf(&new_pag, "%s.pag", new_name);

    ret = rename(old_dir, new_dir) || rename(old_pag, new_pag);
    if (ret) {
	ret = errno;
	if (ret == 0)
	    ret = EPERM;
	krb5_set_error_message(context, ret, "rename: %s", strerror(ret));
    }

    free(old_dir);
    free(old_pag);
    free(new_dir);
    free(new_pag);

    hdb_unlock(new_lock_fd);
    hdb_unlock(old_lock_fd);
    close(new_lock_fd);
    close(old_lock_fd);

    if(ret)
	return ret;

    free(db->hdb_name);
    db->hdb_name = strdup(new_name);
    return 0;
}

static krb5_error_code
NDBM__get(krb5_context context, HDB *db, krb5_data key, krb5_data *reply)
{
    struct ndbm_db *d = (struct ndbm_db *)db->hdb_db;
    datum k, v;
    int code;

    k.dptr  = key.data;
    k.dsize = key.length;
    code = db->hdb_lock(context, db, HDB_RLOCK);
    if(code)
	return code;
    v = dbm_fetch(d->db, k);
    db->hdb_unlock(context, db);
    if(v.dptr == NULL)
	return HDB_ERR_NOENTRY;

    krb5_data_copy(reply, v.dptr, v.dsize);
    return 0;
}

static krb5_error_code
NDBM__put(krb5_context context, HDB *db, int replace,
	krb5_data key, krb5_data value)
{
#ifdef WRITE_SUPPORT
    struct ndbm_db *d = (struct ndbm_db *)db->hdb_db;
    datum k, v;
    int code;

    k.dptr  = key.data;
    k.dsize = key.length;
    v.dptr  = value.data;
    v.dsize = value.length;

    code = db->hdb_lock(context, db, HDB_WLOCK);
    if(code)
	return code;
    code = dbm_store(d->db, k, v, replace ? DBM_REPLACE : DBM_INSERT);
    db->hdb_unlock(context, db);
    if(code == 1)
	return HDB_ERR_EXISTS;
    if (code < 0)
	return code;
    return 0;
#else
    return HDB_ERR_NO_WRITE_SUPPORT;
#endif
}

static krb5_error_code
NDBM__del(krb5_context context, HDB *db, krb5_data key)
{
    struct ndbm_db *d = (struct ndbm_db *)db->hdb_db;
    datum k;
    int code;
    krb5_error_code ret;

    k.dptr = key.data;
    k.dsize = key.length;
    ret = db->hdb_lock(context, db, HDB_WLOCK);
    if(ret) return ret;
    code = dbm_delete(d->db, k);
    db->hdb_unlock(context, db);
    if(code < 0)
	return errno;
    return 0;
}


static krb5_error_code
NDBM_close(krb5_context context, HDB *db)
{
    struct ndbm_db *d = db->hdb_db;
    dbm_close(d->db);
    close(d->lock_fd);
    free(d);
    return 0;
}

static krb5_error_code
NDBM_open(krb5_context context, HDB *db, int flags, mode_t mode)
{
    krb5_error_code ret;
    struct ndbm_db *d = malloc(sizeof(*d));

    if(d == NULL) {
	krb5_set_error_message(context, ENOMEM, "malloc: out of memory");
	return ENOMEM;
    }

    d->db = dbm_open((char*)db->hdb_name, flags, mode);
    if(d->db == NULL){
	ret = errno;
	free(d);
	krb5_set_error_message(context, ret, "dbm_open(%s): %s", db->hdb_name,
			       strerror(ret));
	return ret;
    }

    ret = open_lock_file(context, db->hdb_name, &d->lock_fd);
    if (ret) {
	ret = errno;
	dbm_close(d->db);
	free(d);
	krb5_set_error_message(context, ret, "open(lock file): %s",
			       strerror(ret));
	return ret;
    }

    db->hdb_db = d;
    if((flags & O_ACCMODE) == O_RDONLY)
	ret = hdb_check_db_format(context, db);
    else
	ret = hdb_init_db(context, db);
    if(ret == HDB_ERR_NOENTRY)
	return 0;
    if (ret) {
	NDBM_close(context, db);
	krb5_set_error_message(context, ret, "hdb_open: failed %s database %s",
			       (flags & O_ACCMODE) == O_RDONLY ?
			       "checking format of" : "initialize",
			       db->hdb_name);
    }
    return ret;
}

krb5_error_code
hdb_ndbm_create(krb5_context context, HDB **db,
		const char *filename)
{
    *db = calloc(1, sizeof(**db));
    if (*db == NULL) {
	krb5_set_error_message(context, ENOMEM, "malloc: out of memory");
	return ENOMEM;
    }

    (*db)->hdb_db = NULL;
    (*db)->hdb_name = strdup(filename);
    if ((*db)->hdb_name == NULL) {
	free(*db);
	*db = NULL;
	krb5_set_error_message(context, ENOMEM, "malloc: out of memory");
	return ENOMEM;
    }
    (*db)->hdb_master_key_set = 0;
    (*db)->hdb_openp = 0;
    (*db)->hdb_capability_flags = HDB_CAP_F_HANDLE_ENTERPRISE_PRINCIPAL;
    (*db)->hdb_open = NDBM_open;
    (*db)->hdb_close = NDBM_close;
    (*db)->hdb_fetch_kvno = _hdb_fetch_kvno;
    (*db)->hdb_store = _hdb_store;
    (*db)->hdb_remove = _hdb_remove;
    (*db)->hdb_firstkey = NDBM_firstkey;
    (*db)->hdb_nextkey= NDBM_nextkey;
    (*db)->hdb_lock = NDBM_lock;
    (*db)->hdb_unlock = NDBM_unlock;
    (*db)->hdb_rename = NDBM_rename;
    (*db)->hdb__get = NDBM__get;
    (*db)->hdb__put = NDBM__put;
    (*db)->hdb__del = NDBM__del;
    (*db)->hdb_destroy = NDBM_destroy;
    return 0;
}

#endif /* HAVE_NDBM */
