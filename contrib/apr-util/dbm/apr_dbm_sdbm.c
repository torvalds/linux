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

#include "apr_strings.h"
#define APR_WANT_MEMFUNC
#define APR_WANT_STRFUNC
#include "apr_want.h"

#include "apu_config.h"
#include "apu.h"

#if APU_HAVE_SDBM

#include "apr_dbm_private.h"
#include "apr_sdbm.h"

#define APR_DBM_DBMODE_RO       (APR_FOPEN_READ | APR_FOPEN_BUFFERED)
#define APR_DBM_DBMODE_RW       (APR_FOPEN_READ | APR_FOPEN_WRITE)
#define APR_DBM_DBMODE_RWCREATE (APR_FOPEN_READ | APR_FOPEN_WRITE | APR_FOPEN_CREATE)
#define APR_DBM_DBMODE_RWTRUNC  (APR_FOPEN_READ | APR_FOPEN_WRITE | APR_FOPEN_CREATE | \
                                 APR_FOPEN_TRUNCATE)

static apr_status_t set_error(apr_dbm_t *dbm, apr_status_t dbm_said)
{
    dbm->errcode = dbm_said;

    if (dbm_said != APR_SUCCESS) {
        dbm->errmsg = apr_psprintf(dbm->pool, "%pm", &dbm_said);
    } else {
        dbm->errmsg = NULL;
    }

    return dbm_said;
}

/* --------------------------------------------------------------------------
**
** DEFINE THE VTABLE FUNCTIONS FOR SDBM
*/

static apr_status_t vt_sdbm_open(apr_dbm_t **pdb, const char *pathname,
                                 apr_int32_t mode, apr_fileperms_t perm,
                                 apr_pool_t *pool)
{
    apr_sdbm_t *file;
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

    {
        apr_status_t rv;

        rv = apr_sdbm_open(&file, pathname, dbmode, perm, pool);
        if (rv != APR_SUCCESS)
            return rv;
    }

    /* we have an open database... return it */
    *pdb = apr_pcalloc(pool, sizeof(**pdb));
    (*pdb)->pool = pool;
    (*pdb)->type = &apr_dbm_type_sdbm;
    (*pdb)->file = file;

    /* ### register a cleanup to close the DBM? */

    return APR_SUCCESS;
}

static void vt_sdbm_close(apr_dbm_t *dbm)
{
    apr_sdbm_close(dbm->file);
}

static apr_status_t vt_sdbm_fetch(apr_dbm_t *dbm, apr_datum_t key,
                                  apr_datum_t *pvalue)
{
    apr_status_t rv;
    apr_sdbm_datum_t kd, rd;

    kd.dptr = key.dptr;
    kd.dsize = (int)key.dsize;

    rv = apr_sdbm_fetch(dbm->file, &rd, kd);

    pvalue->dptr = rd.dptr;
    pvalue->dsize = rd.dsize;

    /* store the error info into DBM, and return a status code. Also, note
       that *pvalue should have been cleared on error. */
    return set_error(dbm, rv);
}

static apr_status_t vt_sdbm_store(apr_dbm_t *dbm, apr_datum_t key,
                                  apr_datum_t value)
{
    apr_status_t rv;
    apr_sdbm_datum_t kd, vd;

    kd.dptr = key.dptr;
    kd.dsize = (int)key.dsize;

    vd.dptr = value.dptr;
    vd.dsize = (int)value.dsize;

    rv = apr_sdbm_store(dbm->file, kd, vd, APR_SDBM_REPLACE);

    /* store any error info into DBM, and return a status code. */
    return set_error(dbm, rv);
}

static apr_status_t vt_sdbm_del(apr_dbm_t *dbm, apr_datum_t key)
{
    apr_status_t rv;
    apr_sdbm_datum_t kd;

    kd.dptr = key.dptr;
    kd.dsize = (int)key.dsize;

    rv = apr_sdbm_delete(dbm->file, kd);

    /* store any error info into DBM, and return a status code. */
    return set_error(dbm, rv);
}

static int vt_sdbm_exists(apr_dbm_t *dbm, apr_datum_t key)
{
    int exists;
    apr_sdbm_datum_t vd, kd;

    kd.dptr = key.dptr;
    kd.dsize = (int)key.dsize;

    if (apr_sdbm_fetch(dbm->file, &vd, kd) != APR_SUCCESS)
        exists = 0;
    else
        exists = vd.dptr != NULL;

    return exists;
}

static apr_status_t vt_sdbm_firstkey(apr_dbm_t *dbm, apr_datum_t *pkey)
{
    apr_status_t rv;
    apr_sdbm_datum_t rd;

    rv = apr_sdbm_firstkey(dbm->file, &rd);

    pkey->dptr = rd.dptr;
    pkey->dsize = rd.dsize;

    /* store any error info into DBM, and return a status code. */
    return set_error(dbm, rv);
}

static apr_status_t vt_sdbm_nextkey(apr_dbm_t *dbm, apr_datum_t *pkey)
{
    apr_sdbm_datum_t rd;

    (void)apr_sdbm_nextkey(dbm->file, &rd);

    pkey->dptr = rd.dptr;
    pkey->dsize = rd.dsize;

    /* store any error info into DBM, and return a status code. */
    return set_error(dbm, APR_SUCCESS);
}

static void vt_sdbm_freedatum(apr_dbm_t *dbm, apr_datum_t data)
{
}

static void vt_sdbm_usednames(apr_pool_t *pool, const char *pathname,
                              const char **used1, const char **used2)
{
    *used1 = apr_pstrcat(pool, pathname, APR_SDBM_DIRFEXT, NULL);
    *used2 = apr_pstrcat(pool, pathname, APR_SDBM_PAGFEXT, NULL);
}

APU_MODULE_DECLARE_DATA const apr_dbm_type_t apr_dbm_type_sdbm = {
    "sdbm",
    vt_sdbm_open,
    vt_sdbm_close,
    vt_sdbm_fetch,
    vt_sdbm_store,
    vt_sdbm_del,
    vt_sdbm_exists,
    vt_sdbm_firstkey,
    vt_sdbm_nextkey,
    vt_sdbm_freedatum,
    vt_sdbm_usednames
};

#endif /* APU_HAVE_SDBM */
