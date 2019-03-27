/*
 * Copyright (c) 1997 - 2008 Kungliga Tekniska Högskolan
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
#include "hdb_locl.h"

#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif

/*! @mainpage Heimdal database backend library
 *
 * @section intro Introduction
 *
 * Heimdal libhdb library provides the backend support for Heimdal kdc
 * and kadmind. Its here where plugins for diffrent database engines
 * can be pluged in and extend support for here Heimdal get the
 * principal and policy data from.
 *
 * Example of Heimdal backend are:
 * - Berkeley DB 1.85
 * - Berkeley DB 3.0
 * - Berkeley DB 4.0
 * - New Berkeley DB
 * - LDAP
 *
 *
 * The project web page: http://www.h5l.org/
 *
 */

const int hdb_interface_version = HDB_INTERFACE_VERSION;

static struct hdb_method methods[] = {
#if HAVE_DB1 || HAVE_DB3
    { HDB_INTERFACE_VERSION, "db:",	hdb_db_create},
#endif
#if HAVE_DB1
    { HDB_INTERFACE_VERSION, "mit-db:",	hdb_mdb_create},
#endif
#if HAVE_NDBM
    { HDB_INTERFACE_VERSION, "ndbm:",	hdb_ndbm_create},
#endif
    { HDB_INTERFACE_VERSION, "keytab:",	hdb_keytab_create},
#if defined(OPENLDAP) && !defined(OPENLDAP_MODULE)
    { HDB_INTERFACE_VERSION, "ldap:",	hdb_ldap_create},
    { HDB_INTERFACE_VERSION, "ldapi:",	hdb_ldapi_create},
#endif
#ifdef HAVE_SQLITE3
    { HDB_INTERFACE_VERSION, "sqlite:", hdb_sqlite_create},
#endif
    {0, NULL,	NULL}
};

#if HAVE_DB1 || HAVE_DB3
static struct hdb_method dbmetod =
    { HDB_INTERFACE_VERSION, "", hdb_db_create };
#elif defined(HAVE_NDBM)
static struct hdb_method dbmetod =
    { HDB_INTERFACE_VERSION, "", hdb_ndbm_create };
#endif


krb5_error_code
hdb_next_enctype2key(krb5_context context,
		     const hdb_entry *e,
		     krb5_enctype enctype,
		     Key **key)
{
    Key *k;

    for (k = *key ? (*key) + 1 : e->keys.val;
	 k < e->keys.val + e->keys.len;
	 k++)
    {
	if(k->key.keytype == enctype){
	    *key = k;
	    return 0;
	}
    }
    krb5_set_error_message(context, KRB5_PROG_ETYPE_NOSUPP,
			   "No next enctype %d for hdb-entry",
			  (int)enctype);
    return KRB5_PROG_ETYPE_NOSUPP; /* XXX */
}

krb5_error_code
hdb_enctype2key(krb5_context context,
		hdb_entry *e,
		krb5_enctype enctype,
		Key **key)
{
    *key = NULL;
    return hdb_next_enctype2key(context, e, enctype, key);
}

void
hdb_free_key(Key *key)
{
    memset(key->key.keyvalue.data,
	   0,
	   key->key.keyvalue.length);
    free_Key(key);
    free(key);
}


krb5_error_code
hdb_lock(int fd, int operation)
{
    int i, code = 0;

    for(i = 0; i < 3; i++){
	code = flock(fd, (operation == HDB_RLOCK ? LOCK_SH : LOCK_EX) | LOCK_NB);
	if(code == 0 || errno != EWOULDBLOCK)
	    break;
	sleep(1);
    }
    if(code == 0)
	return 0;
    if(errno == EWOULDBLOCK)
	return HDB_ERR_DB_INUSE;
    return HDB_ERR_CANT_LOCK_DB;
}

krb5_error_code
hdb_unlock(int fd)
{
    int code;
    code = flock(fd, LOCK_UN);
    if(code)
	return 4711 /* XXX */;
    return 0;
}

void
hdb_free_entry(krb5_context context, hdb_entry_ex *ent)
{
    size_t i;

    if (ent->free_entry)
	(*ent->free_entry)(context, ent);

    for(i = 0; i < ent->entry.keys.len; ++i) {
	Key *k = &ent->entry.keys.val[i];

	memset (k->key.keyvalue.data, 0, k->key.keyvalue.length);
    }
    free_hdb_entry(&ent->entry);
}

krb5_error_code
hdb_foreach(krb5_context context,
	    HDB *db,
	    unsigned flags,
	    hdb_foreach_func_t func,
	    void *data)
{
    krb5_error_code ret;
    hdb_entry_ex entry;
    ret = db->hdb_firstkey(context, db, flags, &entry);
    if (ret == 0)
	krb5_clear_error_message(context);
    while(ret == 0){
	ret = (*func)(context, db, &entry, data);
	hdb_free_entry(context, &entry);
	if(ret == 0)
	    ret = db->hdb_nextkey(context, db, flags, &entry);
    }
    if(ret == HDB_ERR_NOENTRY)
	ret = 0;
    return ret;
}

krb5_error_code
hdb_check_db_format(krb5_context context, HDB *db)
{
    krb5_data tag;
    krb5_data version;
    krb5_error_code ret, ret2;
    unsigned ver;
    int foo;

    ret = db->hdb_lock(context, db, HDB_RLOCK);
    if (ret)
	return ret;

    tag.data = (void *)(intptr_t)HDB_DB_FORMAT_ENTRY;
    tag.length = strlen(tag.data);
    ret = (*db->hdb__get)(context, db, tag, &version);
    ret2 = db->hdb_unlock(context, db);
    if(ret)
	return ret;
    if (ret2)
	return ret2;
    foo = sscanf(version.data, "%u", &ver);
    krb5_data_free (&version);
    if (foo != 1)
	return HDB_ERR_BADVERSION;
    if(ver != HDB_DB_FORMAT)
	return HDB_ERR_BADVERSION;
    return 0;
}

krb5_error_code
hdb_init_db(krb5_context context, HDB *db)
{
    krb5_error_code ret, ret2;
    krb5_data tag;
    krb5_data version;
    char ver[32];

    ret = hdb_check_db_format(context, db);
    if(ret != HDB_ERR_NOENTRY)
	return ret;

    ret = db->hdb_lock(context, db, HDB_WLOCK);
    if (ret)
	return ret;

    tag.data = (void *)(intptr_t)HDB_DB_FORMAT_ENTRY;
    tag.length = strlen(tag.data);
    snprintf(ver, sizeof(ver), "%u", HDB_DB_FORMAT);
    version.data = ver;
    version.length = strlen(version.data) + 1; /* zero terminated */
    ret = (*db->hdb__put)(context, db, 0, tag, version);
    ret2 = db->hdb_unlock(context, db);
    if (ret) {
	if (ret2)
	    krb5_clear_error_message(context);
	return ret;
    }
    return ret2;
}

#ifdef HAVE_DLOPEN

 /*
 * Load a dynamic backend from /usr/heimdal/lib/hdb_NAME.so,
 * looking for the hdb_NAME_create symbol.
 */

static const struct hdb_method *
find_dynamic_method (krb5_context context,
		     const char *filename,
		     const char **rest)
{
    static struct hdb_method method;
    struct hdb_so_method *mso;
    char *prefix, *path, *symbol;
    const char *p;
    void *dl;
    size_t len;

    p = strchr(filename, ':');

    /* if no prefix, don't know what module to load, just ignore it */
    if (p == NULL)
	return NULL;

    len = p - filename;
    *rest = filename + len + 1;

    prefix = malloc(len + 1);
    if (prefix == NULL)
	krb5_errx(context, 1, "out of memory");
    strlcpy(prefix, filename, len + 1);

    if (asprintf(&path, LIBDIR "/hdb_%s.so", prefix) == -1)
	krb5_errx(context, 1, "out of memory");

#ifndef RTLD_NOW
#define RTLD_NOW 0
#endif
#ifndef RTLD_GLOBAL
#define RTLD_GLOBAL 0
#endif

    dl = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
    if (dl == NULL) {
	krb5_warnx(context, "error trying to load dynamic module %s: %s\n",
		   path, dlerror());
	free(prefix);
	free(path);
	return NULL;
    }

    if (asprintf(&symbol, "hdb_%s_interface", prefix) == -1)
	krb5_errx(context, 1, "out of memory");

    mso = (struct hdb_so_method *) dlsym(dl, symbol);
    if (mso == NULL) {
	krb5_warnx(context, "error finding symbol %s in %s: %s\n",
		   symbol, path, dlerror());
	dlclose(dl);
	free(symbol);
	free(prefix);
	free(path);
	return NULL;
    }
    free(path);
    free(symbol);

    if (mso->version != HDB_INTERFACE_VERSION) {
	krb5_warnx(context,
		   "error wrong version in shared module %s "
		   "version: %d should have been %d\n",
		   prefix, mso->version, HDB_INTERFACE_VERSION);
	dlclose(dl);
	free(prefix);
	return NULL;
    }

    if (mso->create == NULL) {
	krb5_errx(context, 1,
		  "no entry point function in shared mod %s ",
		   prefix);
	dlclose(dl);
	free(prefix);
	return NULL;
    }

    method.create = mso->create;
    method.prefix = prefix;

    return &method;
}
#endif /* HAVE_DLOPEN */

/*
 * find the relevant method for `filename', returning a pointer to the
 * rest in `rest'.
 * return NULL if there's no such method.
 */

static const struct hdb_method *
find_method (const char *filename, const char **rest)
{
    const struct hdb_method *h;

    for (h = methods; h->prefix != NULL; ++h) {
	if (strncmp (filename, h->prefix, strlen(h->prefix)) == 0) {
	    *rest = filename + strlen(h->prefix);
	    return h;
	}
    }
#if defined(HAVE_DB1) || defined(HAVE_DB3) || defined(HAVE_NDBM)
    if (strncmp(filename, "/", 1) == 0
	|| strncmp(filename, "./", 2) == 0
	|| strncmp(filename, "../", 3) == 0)
    {
	*rest = filename;
	return &dbmetod;
    }
#endif

    return NULL;
}

krb5_error_code
hdb_list_builtin(krb5_context context, char **list)
{
    const struct hdb_method *h;
    size_t len = 0;
    char *buf = NULL;

    for (h = methods; h->prefix != NULL; ++h) {
	if (h->prefix[0] == '\0')
	    continue;
	len += strlen(h->prefix) + 2;
    }

    len += 1;
    buf = malloc(len);
    if (buf == NULL) {
	krb5_set_error_message(context, ENOMEM, "malloc: out of memory");
	return ENOMEM;
    }
    buf[0] = '\0';

    for (h = methods; h->prefix != NULL; ++h) {
	if (h != methods)
	    strlcat(buf, ", ", len);
	strlcat(buf, h->prefix, len);
    }
    *list = buf;
    return 0;
}

krb5_error_code
_hdb_keytab2hdb_entry(krb5_context context,
		      const krb5_keytab_entry *ktentry,
		      hdb_entry_ex *entry)
{
    entry->entry.kvno = ktentry->vno;
    entry->entry.created_by.time = ktentry->timestamp;

    entry->entry.keys.val = calloc(1, sizeof(entry->entry.keys.val[0]));
    if (entry->entry.keys.val == NULL)
	return ENOMEM;
    entry->entry.keys.len = 1;

    entry->entry.keys.val[0].mkvno = NULL;
    entry->entry.keys.val[0].salt = NULL;

    return krb5_copy_keyblock_contents(context,
				       &ktentry->keyblock,
				       &entry->entry.keys.val[0].key);
}

/**
 * Create a handle for a Kerberos database
 *
 * Create a handle for a Kerberos database backend specified by a
 * filename.  Doesn't create a file if its doesn't exists, you have to
 * use O_CREAT to tell the backend to create the file.
 */

krb5_error_code
hdb_create(krb5_context context, HDB **db, const char *filename)
{
    const struct hdb_method *h;
    const char *residual;
    krb5_error_code ret;
    struct krb5_plugin *list = NULL, *e;

    if(filename == NULL)
	filename = HDB_DEFAULT_DB;
    krb5_add_et_list(context, initialize_hdb_error_table_r);
    h = find_method (filename, &residual);

    if (h == NULL) {
	    ret = _krb5_plugin_find(context, PLUGIN_TYPE_DATA, "hdb", &list);
	    if(ret == 0 && list != NULL) {
		    for (e = list; e != NULL; e = _krb5_plugin_get_next(e)) {
			    h = _krb5_plugin_get_symbol(e);
			    if (strncmp (filename, h->prefix, strlen(h->prefix)) == 0
				&& h->interface_version == HDB_INTERFACE_VERSION) {
				    residual = filename + strlen(h->prefix);
				    break;
			    }
		    }
		    if (e == NULL) {
			    h = NULL;
			    _krb5_plugin_free(list);
		    }
	    }
    }

#ifdef HAVE_DLOPEN
    if (h == NULL)
	h = find_dynamic_method (context, filename, &residual);
#endif
    if (h == NULL)
	krb5_errx(context, 1, "No database support for %s", filename);
    return (*h->create)(context, db, residual);
}
