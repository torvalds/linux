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

#include "apr.h"
#include "apr_dso.h"
#include "apr_hash.h"
#include "apr_errno.h"
#include "apr_pools.h"
#include "apr_strings.h"
#define APR_WANT_MEMFUNC
#define APR_WANT_STRFUNC
#include "apr_want.h"
#include "apr_general.h"
#include "apr_atomic.h"

#include "apu_config.h"
#include "apu.h"
#include "apu_internal.h"
#include "apu_version.h"
#include "apr_dbm_private.h"
#include "apu_select_dbm.h"
#include "apr_dbm.h"
#include "apr_dbm_private.h"

/* ### note: the setting of DBM_VTABLE will go away once we have multiple
   ### DBMs in here. 
   ### Well, that day is here.  So, do we remove DBM_VTABLE and the old
   ### API entirely?  Oh, what to do.  We need an APU_DEFAULT_DBM #define.
   ### Sounds like a job for autoconf. */

#if APU_USE_DB
#define DBM_VTABLE apr_dbm_type_db
#define DBM_NAME   "db"
#elif APU_USE_GDBM
#define DBM_VTABLE apr_dbm_type_gdbm
#define DBM_NAME   "gdbm"
#elif APU_USE_NDBM
#define DBM_VTABLE apr_dbm_type_ndbm
#define DBM_NAME   "ndbm"
#elif APU_USE_SDBM
#define DBM_VTABLE apr_dbm_type_sdbm
#define DBM_NAME   "sdbm"
#else /* Not in the USE_xDBM list above */
#error a DBM implementation was not specified
#endif

#if APU_DSO_BUILD

static apr_hash_t *drivers = NULL;
static apr_uint32_t initialised = 0, in_init = 1;

static apr_status_t dbm_term(void *ptr)
{
    /* set drivers to NULL so init can work again */
    drivers = NULL;

    /* Everything else we need is handled by cleanups registered
     * when we created mutexes and loaded DSOs
     */
    return APR_SUCCESS;
}

#endif /* APU_DSO_BUILD */

static apr_status_t dbm_open_type(apr_dbm_type_t const* * vtable,
                                  const char *type, 
                                  apr_pool_t *pool)
{
#if !APU_DSO_BUILD

    *vtable = NULL;
    if (!strcasecmp(type, "default"))     *vtable = &DBM_VTABLE;
#if APU_HAVE_DB
    else if (!strcasecmp(type, "db"))     *vtable = &apr_dbm_type_db;
#endif
    else if (*type && !strcasecmp(type + 1, "dbm")) {
#if APU_HAVE_GDBM
        if (*type == 'G' || *type == 'g') *vtable = &apr_dbm_type_gdbm;
#endif
#if APU_HAVE_NDBM
        if (*type == 'N' || *type == 'n') *vtable = &apr_dbm_type_ndbm;
#endif
#if APU_HAVE_SDBM
        if (*type == 'S' || *type == 's') *vtable = &apr_dbm_type_sdbm;
#endif
        /* avoid empty block */ ;
    }
    if (*vtable)
        return APR_SUCCESS;
    return APR_ENOTIMPL;

#else /* APU_DSO_BUILD */

    char modname[32];
    char symname[34];
    apr_dso_handle_sym_t symbol;
    apr_status_t rv;
    int usertype = 0;

    if (!strcasecmp(type, "default"))        type = DBM_NAME;
    else if (!strcasecmp(type, "db"))        type = "db";
    else if (*type && !strcasecmp(type + 1, "dbm")) {
        if      (*type == 'G' || *type == 'g') type = "gdbm"; 
        else if (*type == 'N' || *type == 'n') type = "ndbm"; 
        else if (*type == 'S' || *type == 's') type = "sdbm"; 
    }
    else usertype = 1;

    if (apr_atomic_inc32(&initialised)) {
        apr_atomic_set32(&initialised, 1); /* prevent wrap-around */

        while (apr_atomic_read32(&in_init)) /* wait until we get fully inited */
            ;
    }
    else {
        apr_pool_t *parent;

        /* Top level pool scope, need process-scope lifetime */
        for (parent = apr_pool_parent_get(pool);
             parent && parent != pool;
             parent = apr_pool_parent_get(pool))
            pool = parent;

        /* deprecate in 2.0 - permit implicit initialization */
        apu_dso_init(pool);

        drivers = apr_hash_make(pool);
        apr_hash_set(drivers, "sdbm", APR_HASH_KEY_STRING, &apr_dbm_type_sdbm);

        apr_pool_cleanup_register(pool, NULL, dbm_term,
                                  apr_pool_cleanup_null);

        apr_atomic_dec32(&in_init);
    }

    rv = apu_dso_mutex_lock();
    if (rv) {
        *vtable = NULL;
        return rv;
    }

    *vtable = apr_hash_get(drivers, type, APR_HASH_KEY_STRING);
    if (*vtable) {
        apu_dso_mutex_unlock();
        return APR_SUCCESS;
    }

    /* The driver DSO must have exactly the same lifetime as the
     * drivers hash table; ignore the passed-in pool */
    pool = apr_hash_pool_get(drivers);

#if defined(NETWARE)
    apr_snprintf(modname, sizeof(modname), "dbm%s.nlm", type);
#elif defined(WIN32) || defined (__CYGWIN__)
    apr_snprintf(modname, sizeof(modname),
                 "apr_dbm_%s-" APU_STRINGIFY(APU_MAJOR_VERSION) ".dll", type);
#else
    apr_snprintf(modname, sizeof(modname),
                 "apr_dbm_%s-" APU_STRINGIFY(APU_MAJOR_VERSION) ".so", type);
#endif
    apr_snprintf(symname, sizeof(symname), "apr_dbm_type_%s", type);

    rv = apu_dso_load(NULL, &symbol, modname, symname, pool);
    if (rv == APR_SUCCESS || rv == APR_EINIT) { /* previously loaded?!? */
        *vtable = symbol;
        if (usertype)
            type = apr_pstrdup(pool, type);
        apr_hash_set(drivers, type, APR_HASH_KEY_STRING, *vtable);
        rv = APR_SUCCESS;
    }
    else
        *vtable = NULL;

    apu_dso_mutex_unlock();
    return rv;

#endif /* APU_DSO_BUILD */
}

APU_DECLARE(apr_status_t) apr_dbm_open_ex(apr_dbm_t **pdb, const char *type, 
                                          const char *pathname, 
                                          apr_int32_t mode,
                                          apr_fileperms_t perm,
                                          apr_pool_t *pool)
{
    apr_dbm_type_t const* vtable = NULL;
    apr_status_t rv = dbm_open_type(&vtable, type, pool);

    if (rv == APR_SUCCESS) {
        rv = (vtable->open)(pdb, pathname, mode, perm, pool);
    }
    return rv;
} 

APU_DECLARE(apr_status_t) apr_dbm_open(apr_dbm_t **pdb, const char *pathname, 
                                       apr_int32_t mode, apr_fileperms_t perm,
                                       apr_pool_t *pool)
{
    return apr_dbm_open_ex(pdb, DBM_NAME, pathname, mode, perm, pool);
}

APU_DECLARE(void) apr_dbm_close(apr_dbm_t *dbm)
{
    (*dbm->type->close)(dbm);
}

APU_DECLARE(apr_status_t) apr_dbm_fetch(apr_dbm_t *dbm, apr_datum_t key,
                                        apr_datum_t *pvalue)
{
    return (*dbm->type->fetch)(dbm, key, pvalue);
}

APU_DECLARE(apr_status_t) apr_dbm_store(apr_dbm_t *dbm, apr_datum_t key,
                                        apr_datum_t value)
{
    return (*dbm->type->store)(dbm, key, value);
}

APU_DECLARE(apr_status_t) apr_dbm_delete(apr_dbm_t *dbm, apr_datum_t key)
{
    return (*dbm->type->del)(dbm, key);
}

APU_DECLARE(int) apr_dbm_exists(apr_dbm_t *dbm, apr_datum_t key)
{
    return (*dbm->type->exists)(dbm, key);
}

APU_DECLARE(apr_status_t) apr_dbm_firstkey(apr_dbm_t *dbm, apr_datum_t *pkey)
{
    return (*dbm->type->firstkey)(dbm, pkey);
}

APU_DECLARE(apr_status_t) apr_dbm_nextkey(apr_dbm_t *dbm, apr_datum_t *pkey)
{
    return (*dbm->type->nextkey)(dbm, pkey);
}

APU_DECLARE(void) apr_dbm_freedatum(apr_dbm_t *dbm, apr_datum_t data)
{
    (*dbm->type->freedatum)(dbm, data);
}

APU_DECLARE(char *) apr_dbm_geterror(apr_dbm_t *dbm, int *errcode,
                                     char *errbuf, apr_size_t errbufsize)
{
    if (errcode != NULL)
        *errcode = dbm->errcode;

    /* assert: errbufsize > 0 */

    if (dbm->errmsg == NULL)
        *errbuf = '\0';
    else
        (void) apr_cpystrn(errbuf, dbm->errmsg, errbufsize);
    return errbuf;
}

APU_DECLARE(apr_status_t) apr_dbm_get_usednames_ex(apr_pool_t *p, 
                                                   const char *type, 
                                                   const char *pathname,
                                                   const char **used1,
                                                   const char **used2)
{
    apr_dbm_type_t const* vtable;
    apr_status_t rv = dbm_open_type(&vtable, type, p);

    if (rv == APR_SUCCESS) {
        (vtable->getusednames)(p, pathname, used1, used2);
        return APR_SUCCESS;
    }
    return rv;
} 

APU_DECLARE(void) apr_dbm_get_usednames(apr_pool_t *p,
                                        const char *pathname,
                                        const char **used1,
                                        const char **used2)
{
    apr_dbm_get_usednames_ex(p, DBM_NAME, pathname, used1, used2); 
}

/* Most DBM libraries take a POSIX mode for creating files.  Don't trust
 * the mode_t type, some platforms may not support it, int is safe.
 */
APU_DECLARE(int) apr_posix_perms2mode(apr_fileperms_t perm)
{
    int mode = 0;

    mode |= 0700 & (perm >> 2); /* User  is off-by-2 bits */
    mode |= 0070 & (perm >> 1); /* Group is off-by-1 bit */
    mode |= 0007 & (perm);      /* World maps 1 for 1 */
    return mode;
}
