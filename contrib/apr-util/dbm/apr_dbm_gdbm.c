/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "apu_config.h"
#include "apu.h"
#include "apr_strings.h"

#if APR_HAVE_STDLIB_H
#include <stdlib.h>     /* for free() */
#endif

#if APU_HAVE_GDBM
#include "apr_dbm_private.h"

#include <gdbm.h>

#define APR_DBM_DBMODE_RO       GDBM_READER
#define APR_DBM_DBMODE_RW       GDBM_WRITER
#define APR_DBM_DBMODE_RWCREATE GDBM_WRCREAT
#define APR_DBM_DBMODE_RWTRUNC  GDBM_NEWDB

/* map a GDBM error to an apr_status_t */
static apr_status_t g2s(int gerr)
{
    if (gerr == -1) {
        /* ### need to fix this */
        return APR_EGENERAL;
    }

    return APR_SUCCESS;
}

static apr_status_t datum_cleanup(void *dptr)
{
    if (dptr)
        free(dptr);

    return APR_SUCCESS;
}

static apr_status_t set_error(apr_dbm_t *dbm, apr_status_t dbm_said)
{
    apr_status_t rv = APR_SUCCESS;

    /* ### ignore whatever the DBM said (dbm_said); ask it explicitly */

    if ((dbm->errcode = gdbm_errno) == GDBM_NO_ERROR) {
        dbm->errmsg = NULL;
    }
    else {
        dbm->errmsg = gdbm_strerror(gdbm_errno);
        rv = APR_EGENERAL;        /* ### need something better */
    }

    /* captured it. clear it now. */
    gdbm_errno = GDBM_NO_ERROR;

    return rv;
}

/* --------------------------------------------------------------------------
**
** DEFINE THE VTABLE FUNCTIONS FOR GDBM
*/

static apr_status_t vt_gdbm_open(apr_dbm_t **pdb, const char *pathname,
                                 apr_int32_t mode, apr_fileperms_t perm,
                                 apr_pool_t *pool)
{
    GDBM_FILE file;
    int dbmode;

    *pdb = NULL;

    switch (mode) {
    case APR_DBM_READONLY:
        dbmode = APR_DBM_DBMODE_RO;
        break;
    case APR_DBM_READWRITE:
        dbmode = APR_DBM_DBMODE_RW;
        break;
    case APR_DBM_RWCREATE:
        dbmode = APR_DBM_DBMODE_RWCREATE;
        break;
    case APR_DBM_RWTRUNC:
        dbmode = APR_DBM_DBMODE_RWTRUNC;
        break;
    default:
        return APR_EINVAL;
    }

    /* Note: stupid cast to get rid of "const" on the pathname */
    file = gdbm_open((char *) pathname, 0, dbmode, apr_posix_perms2mode(perm),
                     NULL);

    if (file == NULL)
        return APR_EGENERAL;      /* ### need a better error */

    /* we have an open database... return it */
    *pdb = apr_pcalloc(pool, sizeof(**pdb));
    (*pdb)->pool = pool;
    (*pdb)->type = &apr_dbm_type_gdbm;
    (*pdb)->file = file;

    /* ### register a cleanup to close the DBM? */

    return APR_SUCCESS;
}

static void vt_gdbm_close(apr_dbm_t *dbm)
{
    gdbm_close(dbm->file);
}

static apr_status_t vt_gdbm_fetch(apr_dbm_t *dbm, apr_datum_t key,
                                  apr_datum_t *pvalue)
{
    datum kd, rd;

    kd.dptr = key.dptr;
    kd.dsize = key.dsize;

    rd = gdbm_fetch(dbm->file, kd);

    pvalue->dptr = rd.dptr;
    pvalue->dsize = rd.dsize;

    if (pvalue->dptr)
        apr_pool_cleanup_register(dbm->pool, pvalue->dptr, datum_cleanup,
                                  apr_pool_cleanup_null);

    /* store the error info into DBM, and return a status code. Also, note
       that *pvalue should have been cleared on error. */
    return set_error(dbm, APR_SUCCESS);
}

static apr_status_t vt_gdbm_store(apr_dbm_t *dbm, apr_datum_t key,
                                  apr_datum_t value)
{
    int rc;
    datum kd, vd;

    kd.dptr = key.dptr;
    kd.dsize = key.dsize;

    vd.dptr = value.dptr;
    vd.dsize = value.dsize;

    rc = gdbm_store(dbm->file, kd, vd, GDBM_REPLACE);

    /* store any error info into DBM, and return a status code. */
    return set_error(dbm, g2s(rc));
}

static apr_status_t vt_gdbm_del(apr_dbm_t *dbm, apr_datum_t key)
{
    int rc;
    datum kd;

    kd.dptr = key.dptr;
    kd.dsize = key.dsize;

    rc = gdbm_delete(dbm->file, kd);

    /* store any error info into DBM, and return a status code. */
    return set_error(dbm, g2s(rc));
}

static int vt_gdbm_exists(apr_dbm_t *dbm, apr_datum_t key)
{
    datum kd;

    kd.dptr = key.dptr;
    kd.dsize = key.dsize;

    return gdbm_exists(dbm->file, kd) != 0;
}

static apr_status_t vt_gdbm_firstkey(apr_dbm_t *dbm, apr_datum_t *pkey)
{
    datum rd;

    rd = gdbm_firstkey(dbm->file);

    pkey->dptr = rd.dptr;
    pkey->dsize = rd.dsize;

    if (pkey->dptr)
        apr_pool_cleanup_register(dbm->pool, pkey->dptr, datum_cleanup,
                                  apr_pool_cleanup_null);

    /* store any error info into DBM, and return a status code. */
    return set_error(dbm, APR_SUCCESS);
}

static apr_status_t vt_gdbm_nextkey(apr_dbm_t *dbm, apr_datum_t *pkey)
{
    datum kd, rd;

    kd.dptr = pkey->dptr;
    kd.dsize = pkey->dsize;

    rd = gdbm_nextkey(dbm->file, kd);

    pkey->dptr = rd.dptr;
    pkey->dsize = rd.dsize;

    if (pkey->dptr)
        apr_pool_cleanup_register(dbm->pool, pkey->dptr, datum_cleanup,
                                  apr_pool_cleanup_null);

    /* store any error info into DBM, and return a status code. */
    return set_error(dbm, APR_SUCCESS);
}

static void vt_gdbm_freedatum(apr_dbm_t *dbm, apr_datum_t data)
{
    (void) apr_pool_cleanup_run(dbm->pool, data.dptr, datum_cleanup);
}

static void vt_gdbm_usednames(apr_pool_t *pool, const char *pathname,
                              const char **used1, const char **used2)
{
    *used1 = apr_pstrdup(pool, pathname);
    *used2 = NULL;
}

APU_MODULE_DECLARE_DATA const apr_dbm_type_t apr_dbm_type_gdbm = {
    "gdbm",
    vt_gdbm_open,
    vt_gdbm_close,
    vt_gdbm_fetch,
    vt_gdbm_store,
    vt_gdbm_del,
    vt_gdbm_exists,
    vt_gdbm_firstkey,
    vt_gdbm_nextkey,
    vt_gdbm_freedatum,
    vt_gdbm_usednames
};

#endif /* APU_HAVE_GDBM */
