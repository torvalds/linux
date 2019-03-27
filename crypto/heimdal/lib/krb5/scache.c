/*
 * Copyright (c) 2008 Kungliga Tekniska Högskolan
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

#ifdef HAVE_SCC

#include <sqlite3.h>

typedef struct krb5_scache {
    char *name;
    char *file;
    sqlite3 *db;

    sqlite_uint64 cid;

    sqlite3_stmt *icred;
    sqlite3_stmt *dcred;
    sqlite3_stmt *iprincipal;

    sqlite3_stmt *icache;
    sqlite3_stmt *ucachen;
    sqlite3_stmt *ucachep;
    sqlite3_stmt *dcache;
    sqlite3_stmt *scache;
    sqlite3_stmt *scache_name;
    sqlite3_stmt *umaster;

} krb5_scache;

#define	SCACHE(X)	((krb5_scache *)(X)->data.data)

#define SCACHE_DEF_NAME		"Default-cache"
#ifdef KRB5_USE_PATH_TOKENS
#define KRB5_SCACHE_DB	"%{TEMP}/krb5scc_%{uid}"
#else
#define KRB5_SCACHE_DB	"/tmp/krb5scc_%{uid}"
#endif
#define KRB5_SCACHE_NAME	"SCC:"  SCACHE_DEF_NAME ":" KRB5_SCACHE_DB

#define SCACHE_INVALID_CID	((sqlite_uint64)-1)

/*
 *
 */

#define SQL_CMASTER ""				\
	"CREATE TABLE master ("			\
        "oid INTEGER PRIMARY KEY,"		\
	"version INTEGER NOT NULL,"		\
	"defaultcache TEXT NOT NULL"		\
	")"

#define SQL_SETUP_MASTER \
	"INSERT INTO master (version,defaultcache) VALUES(2, \"" SCACHE_DEF_NAME "\")"
#define SQL_UMASTER "UPDATE master SET defaultcache=? WHERE version=2"

#define SQL_CCACHE ""				\
	"CREATE TABLE caches ("			\
        "oid INTEGER PRIMARY KEY,"		\
	"principal TEXT,"			\
	"name TEXT NOT NULL"			\
	")"

#define SQL_TCACHE ""						\
	"CREATE TRIGGER CacheDropCreds AFTER DELETE ON caches "	\
	"FOR EACH ROW BEGIN "					\
	"DELETE FROM credentials WHERE cid=old.oid;"		\
	"END"

#define SQL_ICACHE "INSERT INTO caches (name) VALUES(?)"
#define SQL_UCACHE_NAME "UPDATE caches SET name=? WHERE OID=?"
#define SQL_UCACHE_PRINCIPAL "UPDATE caches SET principal=? WHERE OID=?"
#define SQL_DCACHE "DELETE FROM caches WHERE OID=?"
#define SQL_SCACHE "SELECT principal,name FROM caches WHERE OID=?"
#define SQL_SCACHE_NAME "SELECT oid FROM caches WHERE NAME=?"

#define SQL_CCREDS ""				\
	"CREATE TABLE credentials ("		\
        "oid INTEGER PRIMARY KEY,"		\
	"cid INTEGER NOT NULL,"			\
	"kvno INTEGER NOT NULL,"		\
	"etype INTEGER NOT NULL,"		\
        "created_at INTEGER NOT NULL,"		\
	"cred BLOB NOT NULL"			\
	")"

#define SQL_TCRED ""							\
	"CREATE TRIGGER credDropPrincipal AFTER DELETE ON credentials "	\
	"FOR EACH ROW BEGIN "						\
	"DELETE FROM principals WHERE credential_id=old.oid;"		\
	"END"

#define SQL_ICRED "INSERT INTO credentials (cid, kvno, etype, cred, created_at) VALUES (?,?,?,?,?)"
#define SQL_DCRED "DELETE FROM credentials WHERE cid=?"

#define SQL_CPRINCIPALS ""			\
	"CREATE TABLE principals ("		\
        "oid INTEGER PRIMARY KEY,"		\
	"principal TEXT NOT NULL,"		\
	"type INTEGER NOT NULL,"		\
	"credential_id INTEGER NOT NULL"	\
	")"

#define SQL_IPRINCIPAL "INSERT INTO principals (principal, type, credential_id) VALUES (?,?,?)"

/*
 * sqlite destructors
 */

static void
free_data(void *data)
{
    free(data);
}

static void
free_krb5(void *str)
{
    krb5_xfree(str);
}

static void
scc_free(krb5_scache *s)
{
    if (s->file)
	free(s->file);
    if (s->name)
	free(s->name);

    if (s->icred)
	sqlite3_finalize(s->icred);
    if (s->dcred)
	sqlite3_finalize(s->dcred);
    if (s->iprincipal)
	sqlite3_finalize(s->iprincipal);
    if (s->icache)
	sqlite3_finalize(s->icache);
    if (s->ucachen)
	sqlite3_finalize(s->ucachen);
    if (s->ucachep)
	sqlite3_finalize(s->ucachep);
    if (s->dcache)
	sqlite3_finalize(s->dcache);
    if (s->scache)
	sqlite3_finalize(s->scache);
    if (s->scache_name)
	sqlite3_finalize(s->scache_name);
    if (s->umaster)
	sqlite3_finalize(s->umaster);

    if (s->db)
	sqlite3_close(s->db);
    free(s);
}

#ifdef TRACEME
static void
trace(void* ptr, const char * str)
{
    printf("SQL: %s\n", str);
}
#endif

static krb5_error_code
prepare_stmt(krb5_context context, sqlite3 *db,
	     sqlite3_stmt **stmt, const char *str)
{
    int ret;

    ret = sqlite3_prepare_v2(db, str, -1, stmt, NULL);
    if (ret != SQLITE_OK) {
	krb5_set_error_message(context, ENOENT,
			       N_("Failed to prepare stmt %s: %s", ""),
			       str, sqlite3_errmsg(db));
	return ENOENT;
    }
    return 0;
}

static krb5_error_code
exec_stmt(krb5_context context, sqlite3 *db, const char *str,
	  krb5_error_code code)
{
    int ret;

    ret = sqlite3_exec(db, str, NULL, NULL, NULL);
    if (ret != SQLITE_OK && code) {
	krb5_set_error_message(context, code,
			       N_("scache execute %s: %s", ""), str,
			       sqlite3_errmsg(db));
	return code;
    }
    return 0;
}

static krb5_error_code
default_db(krb5_context context, sqlite3 **db)
{
    char *name;
    int ret;

    ret = _krb5_expand_default_cc_name(context, KRB5_SCACHE_DB, &name);
    if (ret)
	return ret;

    ret = sqlite3_open_v2(name, db, SQLITE_OPEN_READWRITE, NULL);
    free(name);
    if (ret != SQLITE_OK) {
	krb5_clear_error_message(context);
	return ENOENT;
    }

#ifdef TRACEME
    sqlite3_trace(*db, trace, NULL);
#endif

    return 0;
}

static krb5_error_code
get_def_name(krb5_context context, char **str)
{
    krb5_error_code ret;
    sqlite3_stmt *stmt;
    const char *name;
    sqlite3 *db;

    ret = default_db(context, &db);
    if (ret)
	return ret;

    ret = prepare_stmt(context, db, &stmt, "SELECT defaultcache FROM master");
    if (ret) {
	sqlite3_close(db);
	return ret;
    }

    ret = sqlite3_step(stmt);
    if (ret != SQLITE_ROW)
	goto out;

    if (sqlite3_column_type(stmt, 0) != SQLITE_TEXT)
	goto out;

    name = (const char *)sqlite3_column_text(stmt, 0);
    if (name == NULL)
	goto out;

    *str = strdup(name);
    if (*str == NULL)
	goto out;

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
out:
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    krb5_clear_error_message(context);
    return ENOENT;
}



static krb5_scache * KRB5_CALLCONV
scc_alloc(krb5_context context, const char *name)
{
    krb5_error_code ret;
    krb5_scache *s;

    ALLOC(s, 1);
    if(s == NULL)
	return NULL;

    s->cid = SCACHE_INVALID_CID;

    if (name) {
	char *file;

	if (*name == '\0') {
	    krb5_error_code ret;
	    ret = get_def_name(context, &s->name);
	    if (ret)
		s->name = strdup(SCACHE_DEF_NAME);
	} else
	    s->name = strdup(name);

	file = strrchr(s->name, ':');
	if (file) {
	    *file++ = '\0';
	    s->file = strdup(file);
	    ret = 0;
	} else {
	    ret = _krb5_expand_default_cc_name(context, KRB5_SCACHE_DB, &s->file);
	}
    } else {
	_krb5_expand_default_cc_name(context, KRB5_SCACHE_DB, &s->file);
	ret = asprintf(&s->name, "unique-%p", s);
    }
    if (ret < 0 || s->file == NULL || s->name == NULL) {
	scc_free(s);
	return NULL;
    }

    return s;
}

static krb5_error_code
open_database(krb5_context context, krb5_scache *s, int flags)
{
    int ret;

    ret = sqlite3_open_v2(s->file, &s->db, SQLITE_OPEN_READWRITE|flags, NULL);
    if (ret) {
	if (s->db) {
	    krb5_set_error_message(context, ENOENT,
				   N_("Error opening scache file %s: %s", ""),
				   s->file, sqlite3_errmsg(s->db));
	    sqlite3_close(s->db);
	    s->db = NULL;
	} else
	    krb5_set_error_message(context, ENOENT,
				   N_("malloc: out of memory", ""));
	return ENOENT;
    }
    return 0;
}

static krb5_error_code
create_cache(krb5_context context, krb5_scache *s)
{
    int ret;

    sqlite3_bind_text(s->icache, 1, s->name, -1, NULL);
    do {
	ret = sqlite3_step(s->icache);
    } while (ret == SQLITE_ROW);
    if (ret != SQLITE_DONE) {
	krb5_set_error_message(context, KRB5_CC_IO,
			       N_("Failed to add scache: %d", ""), ret);
	return KRB5_CC_IO;
    }
    sqlite3_reset(s->icache);

    s->cid = sqlite3_last_insert_rowid(s->db);

    return 0;
}

static krb5_error_code
make_database(krb5_context context, krb5_scache *s)
{
    int created_file = 0;
    int ret;

    if (s->db)
	return 0;

    ret = open_database(context, s, 0);
    if (ret) {
	mode_t oldumask = umask(077);
	ret = open_database(context, s, SQLITE_OPEN_CREATE);
	umask(oldumask);
	if (ret) goto out;

	created_file = 1;

	ret = exec_stmt(context, s->db, SQL_CMASTER, KRB5_CC_IO);
	if (ret) goto out;
	ret = exec_stmt(context, s->db, SQL_CCACHE, KRB5_CC_IO);
	if (ret) goto out;
	ret = exec_stmt(context, s->db, SQL_CCREDS, KRB5_CC_IO);
	if (ret) goto out;
	ret = exec_stmt(context, s->db, SQL_CPRINCIPALS, KRB5_CC_IO);
	if (ret) goto out;
	ret = exec_stmt(context, s->db, SQL_SETUP_MASTER, KRB5_CC_IO);
	if (ret) goto out;

	ret = exec_stmt(context, s->db, SQL_TCACHE, KRB5_CC_IO);
	if (ret) goto out;
	ret = exec_stmt(context, s->db, SQL_TCRED, KRB5_CC_IO);
	if (ret) goto out;
    }

#ifdef TRACEME
    sqlite3_trace(s->db, trace, NULL);
#endif

    ret = prepare_stmt(context, s->db, &s->icred, SQL_ICRED);
    if (ret) goto out;
    ret = prepare_stmt(context, s->db, &s->dcred, SQL_DCRED);
    if (ret) goto out;
    ret = prepare_stmt(context, s->db, &s->iprincipal, SQL_IPRINCIPAL);
    if (ret) goto out;
    ret = prepare_stmt(context, s->db, &s->icache, SQL_ICACHE);
    if (ret) goto out;
    ret = prepare_stmt(context, s->db, &s->ucachen, SQL_UCACHE_NAME);
    if (ret) goto out;
    ret = prepare_stmt(context, s->db, &s->ucachep, SQL_UCACHE_PRINCIPAL);
    if (ret) goto out;
    ret = prepare_stmt(context, s->db, &s->dcache, SQL_DCACHE);
    if (ret) goto out;
    ret = prepare_stmt(context, s->db, &s->scache, SQL_SCACHE);
    if (ret) goto out;
    ret = prepare_stmt(context, s->db, &s->scache_name, SQL_SCACHE_NAME);
    if (ret) goto out;
    ret = prepare_stmt(context, s->db, &s->umaster, SQL_UMASTER);
    if (ret) goto out;

    return 0;

out:
    if (s->db)
	sqlite3_close(s->db);
    if (created_file)
	unlink(s->file);

    return ret;
}

static krb5_error_code
bind_principal(krb5_context context,
	       sqlite3 *db,
	       sqlite3_stmt *stmt,
	       int col,
	       krb5_const_principal principal)
{
    krb5_error_code ret;
    char *str;

    ret = krb5_unparse_name(context, principal, &str);
    if (ret)
	return ret;

    ret = sqlite3_bind_text(stmt, col, str, -1, free_krb5);
    if (ret != SQLITE_OK) {
	krb5_xfree(str);
	krb5_set_error_message(context, ENOMEM,
			       N_("scache bind principal: %s", ""),
			       sqlite3_errmsg(db));
	return ENOMEM;
    }
    return 0;
}

/*
 *
 */

static const char* KRB5_CALLCONV
scc_get_name(krb5_context context,
	     krb5_ccache id)
{
    return SCACHE(id)->name;
}

static krb5_error_code KRB5_CALLCONV
scc_resolve(krb5_context context, krb5_ccache *id, const char *res)
{
    krb5_scache *s;
    int ret;

    s = scc_alloc(context, res);
    if (s == NULL) {
	krb5_set_error_message(context, KRB5_CC_NOMEM,
			       N_("malloc: out of memory", ""));
	return KRB5_CC_NOMEM;
    }

    ret = make_database(context, s);
    if (ret) {
	scc_free(s);
	return ret;
    }

    ret = sqlite3_bind_text(s->scache_name, 1, s->name, -1, NULL);
    if (ret != SQLITE_OK) {
	krb5_set_error_message(context, ENOMEM,
			       "bind name: %s", sqlite3_errmsg(s->db));
	scc_free(s);
	return ENOMEM;
    }

    if (sqlite3_step(s->scache_name) == SQLITE_ROW) {

	if (sqlite3_column_type(s->scache_name, 0) != SQLITE_INTEGER) {
	    sqlite3_reset(s->scache_name);
	    krb5_set_error_message(context, KRB5_CC_END,
				   N_("Cache name of wrong type "
				      "for scache %s", ""),
				   s->name);
	    scc_free(s);
	    return KRB5_CC_END;
	}

	s->cid = sqlite3_column_int(s->scache_name, 0);
    } else {
	s->cid = SCACHE_INVALID_CID;
    }
    sqlite3_reset(s->scache_name);

    (*id)->data.data = s;
    (*id)->data.length = sizeof(*s);

    return 0;
}

static krb5_error_code KRB5_CALLCONV
scc_gen_new(krb5_context context, krb5_ccache *id)
{
    krb5_scache *s;

    s = scc_alloc(context, NULL);

    if (s == NULL) {
	krb5_set_error_message(context, KRB5_CC_NOMEM,
			       N_("malloc: out of memory", ""));
	return KRB5_CC_NOMEM;
    }

    (*id)->data.data = s;
    (*id)->data.length = sizeof(*s);

    return 0;
}

static krb5_error_code KRB5_CALLCONV
scc_initialize(krb5_context context,
	       krb5_ccache id,
	       krb5_principal primary_principal)
{
    krb5_scache *s = SCACHE(id);
    krb5_error_code ret;

    ret = make_database(context, s);
    if (ret)
	return ret;

    ret = exec_stmt(context, s->db, "BEGIN IMMEDIATE TRANSACTION", KRB5_CC_IO);
    if (ret) return ret;

    if (s->cid == SCACHE_INVALID_CID) {
	ret = create_cache(context, s);
	if (ret)
	    goto rollback;
    } else {
	sqlite3_bind_int(s->dcred, 1, s->cid);
	do {
	    ret = sqlite3_step(s->dcred);
	} while (ret == SQLITE_ROW);
	sqlite3_reset(s->dcred);
	if (ret != SQLITE_DONE) {
	    ret = KRB5_CC_IO;
	    krb5_set_error_message(context, ret,
				   N_("Failed to delete old "
				      "credentials: %s", ""),
				   sqlite3_errmsg(s->db));
	    goto rollback;
	}
    }

    ret = bind_principal(context, s->db, s->ucachep, 1, primary_principal);
    if (ret)
	goto rollback;
    sqlite3_bind_int(s->ucachep, 2, s->cid);

    do {
	ret = sqlite3_step(s->ucachep);
    } while (ret == SQLITE_ROW);
    sqlite3_reset(s->ucachep);
    if (ret != SQLITE_DONE) {
	ret = KRB5_CC_IO;
	krb5_set_error_message(context, ret,
			       N_("Failed to bind principal to cache %s", ""),
			       sqlite3_errmsg(s->db));
	goto rollback;
    }

    ret = exec_stmt(context, s->db, "COMMIT", KRB5_CC_IO);
    if (ret) return ret;

    return 0;

rollback:
    exec_stmt(context, s->db, "ROLLBACK", 0);

    return ret;

}

static krb5_error_code KRB5_CALLCONV
scc_close(krb5_context context,
	  krb5_ccache id)
{
    scc_free(SCACHE(id));
    return 0;
}

static krb5_error_code KRB5_CALLCONV
scc_destroy(krb5_context context,
	    krb5_ccache id)
{
    krb5_scache *s = SCACHE(id);
    int ret;

    if (s->cid == SCACHE_INVALID_CID)
	return 0;

    sqlite3_bind_int(s->dcache, 1, s->cid);
    do {
	ret = sqlite3_step(s->dcache);
    } while (ret == SQLITE_ROW);
    sqlite3_reset(s->dcache);
    if (ret != SQLITE_DONE) {
	krb5_set_error_message(context, KRB5_CC_IO,
			       N_("Failed to destroy cache %s: %s", ""),
			       s->name, sqlite3_errmsg(s->db));
	return KRB5_CC_IO;
    }
    return 0;
}

static krb5_error_code
encode_creds(krb5_context context, krb5_creds *creds, krb5_data *data)
{
    krb5_error_code ret;
    krb5_storage *sp;

    sp = krb5_storage_emem();
    if (sp == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }

    ret = krb5_store_creds(sp, creds);
    if (ret) {
	krb5_set_error_message(context, ret,
			       N_("Failed to store credential in scache", ""));
	krb5_storage_free(sp);
	return ret;
    }

    ret = krb5_storage_to_data(sp, data);
    krb5_storage_free(sp);
    if (ret)
	krb5_set_error_message(context, ret,
			       N_("Failed to encode credential in scache", ""));
    return ret;
}

static krb5_error_code
decode_creds(krb5_context context, const void *data, size_t length,
	     krb5_creds *creds)
{
    krb5_error_code ret;
    krb5_storage *sp;

    sp = krb5_storage_from_readonly_mem(data, length);
    if (sp == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }

    ret = krb5_ret_creds(sp, creds);
    krb5_storage_free(sp);
    if (ret) {
	krb5_set_error_message(context, ret,
			       N_("Failed to read credential in scache", ""));
	return ret;
    }
    return 0;
}


static krb5_error_code KRB5_CALLCONV
scc_store_cred(krb5_context context,
	       krb5_ccache id,
	       krb5_creds *creds)
{
    sqlite_uint64 credid;
    krb5_scache *s = SCACHE(id);
    krb5_error_code ret;
    krb5_data data;

    ret = make_database(context, s);
    if (ret)
	return ret;

    ret = encode_creds(context, creds, &data);
    if (ret)
	return ret;

    sqlite3_bind_int(s->icred, 1, s->cid);
    {
	krb5_enctype etype = 0;
	int kvno = 0;
	Ticket t;
	size_t len;

	ret = decode_Ticket(creds->ticket.data,
			    creds->ticket.length, &t, &len);
	if (ret == 0) {
	    if(t.enc_part.kvno)
		kvno = *t.enc_part.kvno;

	    etype = t.enc_part.etype;

	    free_Ticket(&t);
	}

	sqlite3_bind_int(s->icred, 2, kvno);
	sqlite3_bind_int(s->icred, 3, etype);

    }

    sqlite3_bind_blob(s->icred, 4, data.data, data.length, free_data);
    sqlite3_bind_int(s->icred, 5, time(NULL));

    ret = exec_stmt(context, s->db, "BEGIN IMMEDIATE TRANSACTION", KRB5_CC_IO);
    if (ret) return ret;

    do {
	ret = sqlite3_step(s->icred);
    } while (ret == SQLITE_ROW);
    sqlite3_reset(s->icred);
    if (ret != SQLITE_DONE) {
	ret = KRB5_CC_IO;
	krb5_set_error_message(context, ret,
			       N_("Failed to add credential: %s", ""),
			       sqlite3_errmsg(s->db));
	goto rollback;
    }

    credid = sqlite3_last_insert_rowid(s->db);

    {
	bind_principal(context, s->db, s->iprincipal, 1, creds->server);
	sqlite3_bind_int(s->iprincipal, 2, 1);
	sqlite3_bind_int(s->iprincipal, 3, credid);

	do {
	    ret = sqlite3_step(s->iprincipal);
	} while (ret == SQLITE_ROW);
	sqlite3_reset(s->iprincipal);
	if (ret != SQLITE_DONE) {
	    ret = KRB5_CC_IO;
	    krb5_set_error_message(context, ret,
				   N_("Failed to add principal: %s", ""),
				   sqlite3_errmsg(s->db));
	    goto rollback;
	}
    }

    {
	bind_principal(context, s->db, s->iprincipal, 1, creds->client);
	sqlite3_bind_int(s->iprincipal, 2, 0);
	sqlite3_bind_int(s->iprincipal, 3, credid);

	do {
	    ret = sqlite3_step(s->iprincipal);
	} while (ret == SQLITE_ROW);
	sqlite3_reset(s->iprincipal);
	if (ret != SQLITE_DONE) {
	    ret = KRB5_CC_IO;
	    krb5_set_error_message(context, ret,
				   N_("Failed to add principal: %s", ""),
				   sqlite3_errmsg(s->db));
	    goto rollback;
	}
    }

    ret = exec_stmt(context, s->db, "COMMIT", KRB5_CC_IO);
    if (ret) return ret;

    return 0;

rollback:
    exec_stmt(context, s->db, "ROLLBACK", 0);

    return ret;
}

static krb5_error_code KRB5_CALLCONV
scc_get_principal(krb5_context context,
		  krb5_ccache id,
		  krb5_principal *principal)
{
    krb5_scache *s = SCACHE(id);
    krb5_error_code ret;
    const char *str;

    *principal = NULL;

    ret = make_database(context, s);
    if (ret)
	return ret;

    sqlite3_bind_int(s->scache, 1, s->cid);

    if (sqlite3_step(s->scache) != SQLITE_ROW) {
	sqlite3_reset(s->scache);
	krb5_set_error_message(context, KRB5_CC_END,
			       N_("No principal for cache SCC:%s:%s", ""),
			       s->name, s->file);
	return KRB5_CC_END;
    }

    if (sqlite3_column_type(s->scache, 0) != SQLITE_TEXT) {
	sqlite3_reset(s->scache);
	krb5_set_error_message(context, KRB5_CC_END,
			       N_("Principal data of wrong type "
				  "for SCC:%s:%s", ""),
			       s->name, s->file);
	return KRB5_CC_END;
    }

    str = (const char *)sqlite3_column_text(s->scache, 0);
    if (str == NULL) {
	sqlite3_reset(s->scache);
	krb5_set_error_message(context, KRB5_CC_END,
			       N_("Principal not set for SCC:%s:%s", ""),
			       s->name, s->file);
	return KRB5_CC_END;
    }

    ret = krb5_parse_name(context, str, principal);

    sqlite3_reset(s->scache);

    return ret;
}

struct cred_ctx {
    char *drop;
    sqlite3_stmt *stmt;
    sqlite3_stmt *credstmt;
};

static krb5_error_code KRB5_CALLCONV
scc_get_first (krb5_context context,
	       krb5_ccache id,
	       krb5_cc_cursor *cursor)
{
    krb5_scache *s = SCACHE(id);
    krb5_error_code ret;
    struct cred_ctx *ctx;
    char *str = NULL, *name = NULL;

    *cursor = NULL;

    ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }

    ret = make_database(context, s);
    if (ret) {
	free(ctx);
	return ret;
    }

    if (s->cid == SCACHE_INVALID_CID) {
	krb5_set_error_message(context, KRB5_CC_END,
			       N_("Iterating a invalid scache %s", ""),
			       s->name);
	free(ctx);
	return KRB5_CC_END;
    }

    ret = asprintf(&name, "credIteration%pPid%d",
                   ctx, (int)getpid());
    if (ret < 0 || name == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	free(ctx);
	return ENOMEM;
    }

    ret = asprintf(&ctx->drop, "DROP TABLE %s", name);
    if (ret < 0 || ctx->drop == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	free(name);
	free(ctx);
	return ENOMEM;
    }

    ret = asprintf(&str, "CREATE TEMPORARY TABLE %s "
	     "AS SELECT oid,created_at FROM credentials WHERE cid = %lu",
	     name, (unsigned long)s->cid);
    if (ret < 0 || str == NULL) {
	free(ctx->drop);
	free(name);
	free(ctx);
	return ENOMEM;
    }

    ret = exec_stmt(context, s->db, str, KRB5_CC_IO);
    free(str);
    str = NULL;
    if (ret) {
	free(ctx->drop);
	free(name);
	free(ctx);
	return ret;
    }

    ret = asprintf(&str, "SELECT oid FROM %s ORDER BY created_at", name);
    if (ret < 0 || str == NULL) {
	exec_stmt(context, s->db, ctx->drop, 0);
	free(ctx->drop);
	free(name);
	free(ctx);
	return ret;
    }

    ret = prepare_stmt(context, s->db, &ctx->stmt, str);
    free(str);
    str = NULL;
    free(name);
    if (ret) {
	exec_stmt(context, s->db, ctx->drop, 0);
	free(ctx->drop);
	free(ctx);
	return ret;
    }

    ret = prepare_stmt(context, s->db, &ctx->credstmt,
		       "SELECT cred FROM credentials WHERE oid = ?");
    if (ret) {
	sqlite3_finalize(ctx->stmt);
	exec_stmt(context, s->db, ctx->drop, 0);
	free(ctx->drop);
	free(ctx);
	return ret;
    }

    *cursor = ctx;

    return 0;
}

static krb5_error_code KRB5_CALLCONV
scc_get_next (krb5_context context,
	      krb5_ccache id,
	      krb5_cc_cursor *cursor,
	      krb5_creds *creds)
{
    struct cred_ctx *ctx = *cursor;
    krb5_scache *s = SCACHE(id);
    krb5_error_code ret;
    sqlite_uint64 oid;
    const void *data = NULL;
    size_t len = 0;

next:
    ret = sqlite3_step(ctx->stmt);
    if (ret == SQLITE_DONE) {
	krb5_clear_error_message(context);
        return KRB5_CC_END;
    } else if (ret != SQLITE_ROW) {
	krb5_set_error_message(context, KRB5_CC_IO,
			       N_("scache Database failed: %s", ""),
			       sqlite3_errmsg(s->db));
        return KRB5_CC_IO;
    }

    oid = sqlite3_column_int64(ctx->stmt, 0);

    /* read cred from credentials table */

    sqlite3_bind_int(ctx->credstmt, 1, oid);

    ret = sqlite3_step(ctx->credstmt);
    if (ret != SQLITE_ROW) {
	sqlite3_reset(ctx->credstmt);
	goto next;
    }

    if (sqlite3_column_type(ctx->credstmt, 0) != SQLITE_BLOB) {
	krb5_set_error_message(context, KRB5_CC_END,
			       N_("credential of wrong type for SCC:%s:%s", ""),
			       s->name, s->file);
	sqlite3_reset(ctx->credstmt);
	return KRB5_CC_END;
    }

    data = sqlite3_column_blob(ctx->credstmt, 0);
    len = sqlite3_column_bytes(ctx->credstmt, 0);

    ret = decode_creds(context, data, len, creds);
    sqlite3_reset(ctx->credstmt);
    return ret;
}

static krb5_error_code KRB5_CALLCONV
scc_end_get (krb5_context context,
	     krb5_ccache id,
	     krb5_cc_cursor *cursor)
{
    struct cred_ctx *ctx = *cursor;
    krb5_scache *s = SCACHE(id);

    sqlite3_finalize(ctx->stmt);
    sqlite3_finalize(ctx->credstmt);

    exec_stmt(context, s->db, ctx->drop, 0);

    free(ctx->drop);
    free(ctx);

    return 0;
}

static krb5_error_code KRB5_CALLCONV
scc_remove_cred(krb5_context context,
		 krb5_ccache id,
		 krb5_flags which,
		 krb5_creds *mcreds)
{
    krb5_scache *s = SCACHE(id);
    krb5_error_code ret;
    sqlite3_stmt *stmt;
    sqlite_uint64 credid = 0;
    const void *data = NULL;
    size_t len = 0;

    ret = make_database(context, s);
    if (ret)
	return ret;

    ret = prepare_stmt(context, s->db, &stmt,
		       "SELECT cred,oid FROM credentials "
		       "WHERE cid = ?");
    if (ret)
	return ret;

    sqlite3_bind_int(stmt, 1, s->cid);

    /* find credential... */
    while (1) {
	krb5_creds creds;

	ret = sqlite3_step(stmt);
	if (ret == SQLITE_DONE) {
	    ret = 0;
	    break;
	} else if (ret != SQLITE_ROW) {
	    ret = KRB5_CC_IO;
	    krb5_set_error_message(context, ret,
				   N_("scache Database failed: %s", ""),
				   sqlite3_errmsg(s->db));
	    break;
	}

	if (sqlite3_column_type(stmt, 0) != SQLITE_BLOB) {
	    ret = KRB5_CC_END;
	    krb5_set_error_message(context, ret,
				   N_("Credential of wrong type "
				      "for SCC:%s:%s", ""),
				   s->name, s->file);
	    break;
	}

	data = sqlite3_column_blob(stmt, 0);
	len = sqlite3_column_bytes(stmt, 0);

	ret = decode_creds(context, data, len, &creds);
	if (ret)
	    break;

	ret = krb5_compare_creds(context, which, mcreds, &creds);
	krb5_free_cred_contents(context, &creds);
	if (ret) {
	    credid = sqlite3_column_int64(stmt, 1);
	    ret = 0;
	    break;
	}
    }

    sqlite3_finalize(stmt);

    if (id) {
	ret = prepare_stmt(context, s->db, &stmt,
			   "DELETE FROM credentials WHERE oid=?");
	if (ret)
	    return ret;
	sqlite3_bind_int(stmt, 1, credid);

	do {
	    ret = sqlite3_step(stmt);
	} while (ret == SQLITE_ROW);
	sqlite3_finalize(stmt);
	if (ret != SQLITE_DONE) {
	    ret = KRB5_CC_IO;
	    krb5_set_error_message(context, ret,
				   N_("failed to delete scache credental", ""));
	} else
	    ret = 0;
    }

    return ret;
}

static krb5_error_code KRB5_CALLCONV
scc_set_flags(krb5_context context,
	      krb5_ccache id,
	      krb5_flags flags)
{
    return 0; /* XXX */
}

struct cache_iter {
    char *drop;
    sqlite3 *db;
    sqlite3_stmt *stmt;
};

static krb5_error_code KRB5_CALLCONV
scc_get_cache_first(krb5_context context, krb5_cc_cursor *cursor)
{
    struct cache_iter *ctx;
    krb5_error_code ret;
    char *name = NULL, *str = NULL;

    *cursor = NULL;

    ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }

    ret = default_db(context, &ctx->db);
    if (ctx->db == NULL) {
	free(ctx);
	return ret;
    }

    ret = asprintf(&name, "cacheIteration%pPid%d",
                   ctx, (int)getpid());
    if (ret < 0 || name == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	sqlite3_close(ctx->db);
	free(ctx);
	return ENOMEM;
    }

    ret = asprintf(&ctx->drop, "DROP TABLE %s", name);
    if (ret < 0 || ctx->drop == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	sqlite3_close(ctx->db);
	free(name);
	free(ctx);
	return ENOMEM;
    }

    ret = asprintf(&str, "CREATE TEMPORARY TABLE %s AS SELECT name FROM caches",
	     name);
    if (ret < 0 || str == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	sqlite3_close(ctx->db);
	free(name);
	free(ctx->drop);
	free(ctx);
	return ENOMEM;
    }

    ret = exec_stmt(context, ctx->db, str, KRB5_CC_IO);
    free(str);
    str = NULL;
    if (ret) {
	sqlite3_close(ctx->db);
	free(name);
	free(ctx->drop);
	free(ctx);
	return ret;
    }

    ret = asprintf(&str, "SELECT name FROM %s", name);
    free(name);
    if (ret < 0 || str == NULL) {
	exec_stmt(context, ctx->db, ctx->drop, 0);
	sqlite3_close(ctx->db);
	free(name);
	free(ctx->drop);
	free(ctx);
	return ENOMEM;
    }

    ret = prepare_stmt(context, ctx->db, &ctx->stmt, str);
    free(str);
    if (ret) {
	exec_stmt(context, ctx->db, ctx->drop, 0);
	sqlite3_close(ctx->db);
	free(ctx->drop);
	free(ctx);
	return ret;
    }

    *cursor = ctx;

    return 0;
}

static krb5_error_code KRB5_CALLCONV
scc_get_cache_next(krb5_context context,
		   krb5_cc_cursor cursor,
		   krb5_ccache *id)
{
    struct cache_iter *ctx = cursor;
    krb5_error_code ret;
    const char *name;

again:
    ret = sqlite3_step(ctx->stmt);
    if (ret == SQLITE_DONE) {
	krb5_clear_error_message(context);
        return KRB5_CC_END;
    } else if (ret != SQLITE_ROW) {
	krb5_set_error_message(context, KRB5_CC_IO,
			       N_("Database failed: %s", ""),
			       sqlite3_errmsg(ctx->db));
        return KRB5_CC_IO;
    }

    if (sqlite3_column_type(ctx->stmt, 0) != SQLITE_TEXT)
	goto again;

    name = (const char *)sqlite3_column_text(ctx->stmt, 0);
    if (name == NULL)
	goto again;

    ret = _krb5_cc_allocate(context, &krb5_scc_ops, id);
    if (ret)
	return ret;

    return scc_resolve(context, id, name);
}

static krb5_error_code KRB5_CALLCONV
scc_end_cache_get(krb5_context context, krb5_cc_cursor cursor)
{
    struct cache_iter *ctx = cursor;

    exec_stmt(context, ctx->db, ctx->drop, 0);
    sqlite3_finalize(ctx->stmt);
    sqlite3_close(ctx->db);
    free(ctx->drop);
    free(ctx);
    return 0;
}

static krb5_error_code KRB5_CALLCONV
scc_move(krb5_context context, krb5_ccache from, krb5_ccache to)
{
    krb5_scache *sfrom = SCACHE(from);
    krb5_scache *sto = SCACHE(to);
    krb5_error_code ret;

    if (strcmp(sfrom->file, sto->file) != 0) {
	krb5_set_error_message(context, KRB5_CC_BADNAME,
			       N_("Can't handle cross database "
				  "credential move: %s -> %s", ""),
			       sfrom->file, sto->file);
	return KRB5_CC_BADNAME;
    }

    ret = make_database(context, sfrom);
    if (ret)
	return ret;

    ret = exec_stmt(context, sfrom->db,
		    "BEGIN IMMEDIATE TRANSACTION", KRB5_CC_IO);
    if (ret) return ret;

    if (sto->cid != SCACHE_INVALID_CID) {
	/* drop old cache entry */

	sqlite3_bind_int(sfrom->dcache, 1, sto->cid);
	do {
	    ret = sqlite3_step(sfrom->dcache);
	} while (ret == SQLITE_ROW);
	sqlite3_reset(sfrom->dcache);
	if (ret != SQLITE_DONE) {
	    krb5_set_error_message(context, KRB5_CC_IO,
				   N_("Failed to delete old cache: %d", ""),
				   (int)ret);
	    goto rollback;
	}
    }

    sqlite3_bind_text(sfrom->ucachen, 1, sto->name, -1, NULL);
    sqlite3_bind_int(sfrom->ucachen, 2, sfrom->cid);

    do {
	ret = sqlite3_step(sfrom->ucachen);
    } while (ret == SQLITE_ROW);
    sqlite3_reset(sfrom->ucachen);
    if (ret != SQLITE_DONE) {
	krb5_set_error_message(context, KRB5_CC_IO,
			       N_("Failed to update new cache: %d", ""),
			       (int)ret);
	goto rollback;
    }

    sto->cid = sfrom->cid;

    ret = exec_stmt(context, sfrom->db, "COMMIT", KRB5_CC_IO);
    if (ret) return ret;

    scc_free(sfrom);

    return 0;

rollback:
    exec_stmt(context, sfrom->db, "ROLLBACK", 0);
    scc_free(sfrom);

    return KRB5_CC_IO;
}

static krb5_error_code KRB5_CALLCONV
scc_get_default_name(krb5_context context, char **str)
{
    krb5_error_code ret;
    char *name;

    *str = NULL;

    ret = get_def_name(context, &name);
    if (ret)
	return _krb5_expand_default_cc_name(context, KRB5_SCACHE_NAME, str);

    ret = asprintf(str, "SCC:%s", name);
    free(name);
    if (ret < 0 || *str == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    return 0;
}

static krb5_error_code KRB5_CALLCONV
scc_set_default(krb5_context context, krb5_ccache id)
{
    krb5_scache *s = SCACHE(id);
    krb5_error_code ret;

    if (s->cid == SCACHE_INVALID_CID) {
	krb5_set_error_message(context, KRB5_CC_IO,
			       N_("Trying to set a invalid cache "
				  "as default %s", ""),
			       s->name);
	return KRB5_CC_IO;
    }

    ret = sqlite3_bind_text(s->umaster, 1, s->name, -1, NULL);
    if (ret) {
	sqlite3_reset(s->umaster);
	krb5_set_error_message(context, KRB5_CC_IO,
			       N_("Failed to set name of default cache", ""));
	return KRB5_CC_IO;
    }

    do {
	ret = sqlite3_step(s->umaster);
    } while (ret == SQLITE_ROW);
    sqlite3_reset(s->umaster);
    if (ret != SQLITE_DONE) {
	krb5_set_error_message(context, KRB5_CC_IO,
			       N_("Failed to update default cache", ""));
	return KRB5_CC_IO;
    }

    return 0;
}

/**
 * Variable containing the SCC based credential cache implemention.
 *
 * @ingroup krb5_ccache
 */

KRB5_LIB_VARIABLE const krb5_cc_ops krb5_scc_ops = {
    KRB5_CC_OPS_VERSION,
    "SCC",
    scc_get_name,
    scc_resolve,
    scc_gen_new,
    scc_initialize,
    scc_destroy,
    scc_close,
    scc_store_cred,
    NULL, /* scc_retrieve */
    scc_get_principal,
    scc_get_first,
    scc_get_next,
    scc_end_get,
    scc_remove_cred,
    scc_set_flags,
    NULL,
    scc_get_cache_first,
    scc_get_cache_next,
    scc_end_cache_get,
    scc_move,
    scc_get_default_name,
    scc_set_default
};

#endif
