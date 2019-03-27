/*
 * Copyright (c) 2002 Kungliga Tekniska Högskolan
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

#include <config.h>

#include "ndbm_wrap.h"
#if defined(HAVE_DBHEADER)
#include <db.h>
#elif defined(HAVE_DB5_DB_H)
#include <db5/db.h>
#elif defined(HAVE_DB4_DB_H)
#include <db4/db.h>
#elif defined(HAVE_DB3_DB_H)
#include <db3/db.h>
#else
#include <db.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

/* XXX undefine open so this works on Solaris with large file support */
#undef open

#define DBT2DATUM(DBT, DATUM) do { (DATUM)->dptr = (DBT)->data; (DATUM)->dsize = (DBT)->size; } while(0)
#define DATUM2DBT(DATUM, DBT) do { (DBT)->data = (DATUM)->dptr; (DBT)->size = (DATUM)->dsize; } while(0)
#define RETURN(X) return ((X) == 0) ? 0 : -1

#ifdef HAVE_DB3
static DBC *cursor;
#endif

#define D(X) ((DB*)(X))

ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
dbm_close (DBM *db)
{
#ifdef HAVE_DB3
    D(db)->close(D(db), 0);
    cursor = NULL;
#else
    D(db)->close(D(db));
#endif
}

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
dbm_delete (DBM *db, datum dkey)
{
    DBT key;
    DATUM2DBT(&dkey, &key);
#ifdef HAVE_DB3
    RETURN(D(db)->del(D(db), NULL, &key, 0));
#else
    RETURN(D(db)->del(D(db), &key, 0));
#endif
}

datum
dbm_fetch (DBM *db, datum dkey)
{
    datum dvalue;
    DBT key, value;
    DATUM2DBT(&dkey, &key);
    if(D(db)->get(D(db),
#ifdef HAVE_DB3
	       NULL,
#endif
	       &key, &value, 0) != 0) {
	dvalue.dptr = NULL;
	dvalue.dsize = 0;
    }
    else
	DBT2DATUM(&value, &dvalue);

    return dvalue;
}

static datum
dbm_get (DB *db, int flags)
{
    DBT key, value;
    datum datum;
#ifdef HAVE_DB3
    if(cursor == NULL)
	db->cursor(db, NULL, &cursor, 0);
    if(cursor->c_get(cursor, &key, &value, flags) != 0) {
	datum.dptr = NULL;
	datum.dsize = 0;
    } else
	DBT2DATUM(&value, &datum);
#else
    db->seq(db, &key, &value, flags);
    DBT2DATUM(&value, &datum);
#endif
    return datum;
}

#ifndef DB_FIRST
#define DB_FIRST	R_FIRST
#define DB_NEXT		R_NEXT
#define DB_NOOVERWRITE	R_NOOVERWRITE
#define DB_KEYEXIST	1
#endif

ROKEN_LIB_FUNCTION datum ROKEN_LIB_CALL
dbm_firstkey (DBM *db)
{
    return dbm_get(D(db), DB_FIRST);
}

ROKEN_LIB_FUNCTION datum ROKEN_LIB_CALL
dbm_nextkey (DBM *db)
{
    return dbm_get(D(db), DB_NEXT);
}

ROKEN_LIB_FUNCTION DBM* ROKEN_LIB_CALL
dbm_open (const char *file, int flags, mode_t mode)
{
#ifdef HAVE_DB3
    int myflags = 0;
#endif
    DB *db;
    char *fn = malloc(strlen(file) + 4);
    if(fn == NULL)
	return NULL;
    strcpy(fn, file);
    strcat(fn, ".db");
#ifdef HAVE_DB3
    if (flags & O_CREAT)
	myflags |= DB_CREATE;

    if (flags & O_EXCL)
	myflags |= DB_EXCL;

    if (flags & O_RDONLY)
	myflags |= DB_RDONLY;

    if (flags & O_TRUNC)
	myflags |= DB_TRUNCATE;
    if(db_create(&db, NULL, 0) != 0) {
	free(fn);
	return NULL;
    }

#if (DB_VERSION_MAJOR > 3) && (DB_VERSION_MINOR > 0)
    if(db->open(db, NULL, fn, NULL, DB_BTREE, myflags, mode) != 0) {
#else
    if(db->open(db, fn, NULL, DB_BTREE, myflags, mode) != 0) {
#endif
	free(fn);
	db->close(db, 0);
	return NULL;
    }
#else
    db = dbopen(fn, flags, mode, DB_BTREE, NULL);
#endif
    free(fn);
    return (DBM*)db;
}

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
dbm_store (DBM *db, datum dkey, datum dvalue, int flags)
{
    int ret;
    DBT key, value;
    int myflags = 0;
    if((flags & DBM_REPLACE) == 0)
	myflags |= DB_NOOVERWRITE;
    DATUM2DBT(&dkey, &key);
    DATUM2DBT(&dvalue, &value);
    ret = D(db)->put(D(db),
#ifdef HAVE_DB3
		     NULL,
#endif
&key, &value, myflags);
    if(ret == DB_KEYEXIST)
	return 1;
    RETURN(ret);
}

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
dbm_error (DBM *db)
{
    return 0;
}

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
dbm_clearerr (DBM *db)
{
    return 0;
}

