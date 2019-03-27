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

#include <ctype.h>
#include <stdio.h>

#include "apu_config.h"
#include "apu.h"

#include "apr_pools.h"
#include "apr_dso.h"
#include "apr_strings.h"
#include "apr_hash.h"
#include "apr_thread_mutex.h"
#include "apr_lib.h"
#include "apr_atomic.h"

#include "apu_internal.h"
#include "apr_dbd_internal.h"
#include "apr_dbd.h"
#include "apu_version.h"

static apr_hash_t *drivers = NULL;
static apr_uint32_t initialised = 0, in_init = 1;

#define CLEANUP_CAST (apr_status_t (*)(void*))

#if APR_HAS_THREADS
/* deprecated, but required for existing providers.  Existing and new
 * providers should be refactored to use a provider-specific mutex so
 * that different providers do not block one another.
 * In APR 1.3 this is no longer used for dso module loading, and
 * apu_dso_mutex_[un]lock is used instead.
 * In APR 2.0 this should become entirely local to libaprutil-2.so and
 * no longer be exported.
 */
static apr_thread_mutex_t* mutex = NULL;
APU_DECLARE(apr_status_t) apr_dbd_mutex_lock()
{
    return apr_thread_mutex_lock(mutex);
}
APU_DECLARE(apr_status_t) apr_dbd_mutex_unlock()
{
    return apr_thread_mutex_unlock(mutex);
}
#else
APU_DECLARE(apr_status_t) apr_dbd_mutex_lock() {
    return APR_SUCCESS;
}
APU_DECLARE(apr_status_t) apr_dbd_mutex_unlock() {
    return APR_SUCCESS;
}
#endif

#if !APU_DSO_BUILD
#define DRIVER_LOAD(name,driver,pool) \
    {   \
        extern const apr_dbd_driver_t driver; \
        apr_hash_set(drivers,name,APR_HASH_KEY_STRING,&driver); \
        if (driver.init) {     \
            driver.init(pool); \
        }  \
    }
#endif

static apr_status_t apr_dbd_term(void *ptr)
{
    /* set drivers to NULL so init can work again */
    drivers = NULL;

    /* Everything else we need is handled by cleanups registered
     * when we created mutexes and loaded DSOs
     */
    return APR_SUCCESS;
}

APU_DECLARE(apr_status_t) apr_dbd_init(apr_pool_t *pool)
{
    apr_status_t ret = APR_SUCCESS;
    apr_pool_t *parent;

    if (apr_atomic_inc32(&initialised)) {
        apr_atomic_set32(&initialised, 1); /* prevent wrap-around */

        while (apr_atomic_read32(&in_init)) /* wait until we get fully inited */
            ;

        return APR_SUCCESS;
    }

    /* Top level pool scope, need process-scope lifetime */
    for (parent = apr_pool_parent_get(pool);
        parent && parent != pool;
        parent = apr_pool_parent_get(pool))
       pool = parent;
#if APU_DSO_BUILD
    /* deprecate in 2.0 - permit implicit initialization */
    apu_dso_init(pool);
#endif

    drivers = apr_hash_make(pool);

#if APR_HAS_THREADS
    ret = apr_thread_mutex_create(&mutex, APR_THREAD_MUTEX_DEFAULT, pool);
    /* This already registers a pool cleanup */
#endif

#if !APU_DSO_BUILD

    /* Load statically-linked drivers: */
#if APU_HAVE_MYSQL
    DRIVER_LOAD("mysql", apr_dbd_mysql_driver, pool);
#endif
#if APU_HAVE_PGSQL
    DRIVER_LOAD("pgsql", apr_dbd_pgsql_driver, pool);
#endif
#if APU_HAVE_SQLITE3
    DRIVER_LOAD("sqlite3", apr_dbd_sqlite3_driver, pool);
#endif
#if APU_HAVE_SQLITE2
    DRIVER_LOAD("sqlite2", apr_dbd_sqlite2_driver, pool);
#endif
#if APU_HAVE_ORACLE
    DRIVER_LOAD("oracle", apr_dbd_oracle_driver, pool);
#endif
#if APU_HAVE_FREETDS
    DRIVER_LOAD("freetds", apr_dbd_freetds_driver, pool);
#endif
#if APU_HAVE_ODBC
    DRIVER_LOAD("odbc", apr_dbd_odbc_driver, pool);
#endif
#if APU_HAVE_SOME_OTHER_BACKEND
    DRIVER_LOAD("firebird", apr_dbd_other_driver, pool);
#endif
#endif /* APU_DSO_BUILD */

    apr_pool_cleanup_register(pool, NULL, apr_dbd_term,
                              apr_pool_cleanup_null);

    apr_atomic_dec32(&in_init);

    return ret;
}

APU_DECLARE(apr_status_t) apr_dbd_get_driver(apr_pool_t *pool, const char *name,
                                             const apr_dbd_driver_t **driver)
{
#if APU_DSO_BUILD
    char modname[32];
    char symname[34];
    apr_dso_handle_sym_t symbol;
#endif
    apr_status_t rv;

#if APU_DSO_BUILD
    rv = apu_dso_mutex_lock();
    if (rv) {
        return rv;
    }
#endif
    *driver = apr_hash_get(drivers, name, APR_HASH_KEY_STRING);
    if (*driver) {
#if APU_DSO_BUILD
        apu_dso_mutex_unlock();
#endif
        return APR_SUCCESS;
    }

#if APU_DSO_BUILD
    /* The driver DSO must have exactly the same lifetime as the
     * drivers hash table; ignore the passed-in pool */
    pool = apr_hash_pool_get(drivers);

#if defined(NETWARE)
    apr_snprintf(modname, sizeof(modname), "dbd%s.nlm", name);
#elif defined(WIN32) || defined(__CYGWIN__)
    apr_snprintf(modname, sizeof(modname),
                 "apr_dbd_%s-" APU_STRINGIFY(APU_MAJOR_VERSION) ".dll", name);
#else
    apr_snprintf(modname, sizeof(modname),
                 "apr_dbd_%s-" APU_STRINGIFY(APU_MAJOR_VERSION) ".so", name);
#endif
    apr_snprintf(symname, sizeof(symname), "apr_dbd_%s_driver", name);
    rv = apu_dso_load(NULL, &symbol, modname, symname, pool);
    if (rv == APR_SUCCESS || rv == APR_EINIT) { /* previously loaded?!? */
        *driver = symbol;
        name = apr_pstrdup(pool, name);
        apr_hash_set(drivers, name, APR_HASH_KEY_STRING, *driver);
        rv = APR_SUCCESS;
        if ((*driver)->init) {
            (*driver)->init(pool);
        }
    }
    apu_dso_mutex_unlock();

#else /* not builtin and !APU_DSO_BUILD => not implemented */
    rv = APR_ENOTIMPL;
#endif

    return rv;
}

APU_DECLARE(apr_status_t) apr_dbd_open_ex(const apr_dbd_driver_t *driver,
                                          apr_pool_t *pool, const char *params,
                                          apr_dbd_t **handle,
                                          const char **error)
{
    apr_status_t rv;
    *handle = (driver->open)(pool, params, error);
    if (*handle == NULL) {
        return APR_EGENERAL;
    }
    rv = apr_dbd_check_conn(driver, pool, *handle);
    if ((rv != APR_SUCCESS) && (rv != APR_ENOTIMPL)) {
        /* XXX: rv is APR error code, but apr_dbd_error() takes int! */
        if (error) {
            *error = apr_dbd_error(driver, *handle, rv);
        }
        apr_dbd_close(driver, *handle);
        return APR_EGENERAL;
    }
    return APR_SUCCESS;
}

APU_DECLARE(apr_status_t) apr_dbd_open(const apr_dbd_driver_t *driver,
                                       apr_pool_t *pool, const char *params,
                                       apr_dbd_t **handle)
{
    return apr_dbd_open_ex(driver,pool,params,handle,NULL);
}

APU_DECLARE(int) apr_dbd_transaction_start(const apr_dbd_driver_t *driver,
                                           apr_pool_t *pool, apr_dbd_t *handle,
                                           apr_dbd_transaction_t **trans)
{
    int ret = driver->start_transaction(pool, handle, trans);
    if (*trans) {
        apr_pool_cleanup_register(pool, *trans,
                                  CLEANUP_CAST driver->end_transaction,
                                  apr_pool_cleanup_null);
    }
    return ret;
}

APU_DECLARE(int) apr_dbd_transaction_end(const apr_dbd_driver_t *driver,
                                         apr_pool_t *pool,
                                         apr_dbd_transaction_t *trans)
{
    apr_pool_cleanup_kill(pool, trans, CLEANUP_CAST driver->end_transaction);
    return driver->end_transaction(trans);
}

APU_DECLARE(int) apr_dbd_transaction_mode_get(const apr_dbd_driver_t *driver,
                                              apr_dbd_transaction_t *trans)
{
    return driver->transaction_mode_get(trans);
}

APU_DECLARE(int) apr_dbd_transaction_mode_set(const apr_dbd_driver_t *driver,
                                              apr_dbd_transaction_t *trans,
                                              int mode)
{
    return driver->transaction_mode_set(trans, mode);
}

APU_DECLARE(apr_status_t) apr_dbd_close(const apr_dbd_driver_t *driver,
                                        apr_dbd_t *handle)
{
    return driver->close(handle);
}

APU_DECLARE(const char*) apr_dbd_name(const apr_dbd_driver_t *driver)
{
    return driver->name;
}

APU_DECLARE(void*) apr_dbd_native_handle(const apr_dbd_driver_t *driver,
                                         apr_dbd_t *handle)
{
    return driver->native_handle(handle);
}

APU_DECLARE(int) apr_dbd_check_conn(const apr_dbd_driver_t *driver,
                                    apr_pool_t *pool,
                                    apr_dbd_t *handle)
{
    return driver->check_conn(pool, handle);
}

APU_DECLARE(int) apr_dbd_set_dbname(const apr_dbd_driver_t *driver,
                                    apr_pool_t *pool,
                                    apr_dbd_t *handle, const char *name)
{
    return driver->set_dbname(pool,handle,name);
}

APU_DECLARE(int) apr_dbd_query(const apr_dbd_driver_t *driver,
                               apr_dbd_t *handle,
                               int *nrows, const char *statement)
{
    return driver->query(handle,nrows,statement);
}

APU_DECLARE(int) apr_dbd_select(const apr_dbd_driver_t *driver,
                                apr_pool_t *pool,
                                apr_dbd_t *handle, apr_dbd_results_t **res,
                                const char *statement, int random)
{
    return driver->select(pool,handle,res,statement,random);
}

APU_DECLARE(int) apr_dbd_num_cols(const apr_dbd_driver_t *driver,
                                  apr_dbd_results_t *res)
{
    return driver->num_cols(res);
}

APU_DECLARE(int) apr_dbd_num_tuples(const apr_dbd_driver_t *driver,
                                    apr_dbd_results_t *res)
{
    return driver->num_tuples(res);
}

APU_DECLARE(int) apr_dbd_get_row(const apr_dbd_driver_t *driver,
                                 apr_pool_t *pool,
                                 apr_dbd_results_t *res, apr_dbd_row_t **row,
                                 int rownum)
{
    return driver->get_row(pool,res,row,rownum);
}

APU_DECLARE(const char*) apr_dbd_get_entry(const apr_dbd_driver_t *driver,
                                           apr_dbd_row_t *row, int col)
{
    return driver->get_entry(row,col);
}

APU_DECLARE(const char*) apr_dbd_get_name(const apr_dbd_driver_t *driver,
                                          apr_dbd_results_t *res, int col)
{
    return driver->get_name(res,col);
}

APU_DECLARE(const char*) apr_dbd_error(const apr_dbd_driver_t *driver,
                                       apr_dbd_t *handle, int errnum)
{
    return driver->error(handle,errnum);
}

APU_DECLARE(const char*) apr_dbd_escape(const apr_dbd_driver_t *driver,
                                        apr_pool_t *pool, const char *string,
                                        apr_dbd_t *handle)
{
    return driver->escape(pool,string,handle);
}

APU_DECLARE(int) apr_dbd_prepare(const apr_dbd_driver_t *driver,
                                 apr_pool_t *pool,
                                 apr_dbd_t *handle, const char *query,
                                 const char *label,
                                 apr_dbd_prepared_t **statement)
{
    size_t qlen;
    int i, nargs = 0, nvals = 0;
    char *p, *pq;
    const char *q;
    apr_dbd_type_e *t;

    if (!driver->pformat) {
        return APR_ENOTIMPL;
    }

    /* find the number of parameters in the query */
    for (q = query; *q; q++) {
        if (q[0] == '%') {
            if (apr_isalpha(q[1])) {
                nargs++;
            } else if (q[1] == '%') {
                q++;
            }
        }
    }
    nvals = nargs;

    qlen = strlen(query) +
           nargs * (strlen(driver->pformat) + sizeof(nargs) * 3 + 2) + 1;
    pq = apr_palloc(pool, qlen);
    t = apr_pcalloc(pool, sizeof(*t) * nargs);

    for (p = pq, q = query, i = 0; *q; q++) {
        if (q[0] == '%') {
            if (apr_isalpha(q[1])) {
                switch (q[1]) {
                case 'd': t[i] = APR_DBD_TYPE_INT;   break;
                case 'u': t[i] = APR_DBD_TYPE_UINT;  break;
                case 'f': t[i] = APR_DBD_TYPE_FLOAT; break;
                case 'h':
                    switch (q[2]) {
                    case 'h':
                        switch (q[3]){
                        case 'd': t[i] = APR_DBD_TYPE_TINY;  q += 2; break;
                        case 'u': t[i] = APR_DBD_TYPE_UTINY; q += 2; break;
                        }
                        break;
                    case 'd': t[i] = APR_DBD_TYPE_SHORT;  q++; break;
                    case 'u': t[i] = APR_DBD_TYPE_USHORT; q++; break;
                    }
                    break;
                case 'l':
                    switch (q[2]) {
                    case 'l':
                        switch (q[3]){
                        case 'd': t[i] = APR_DBD_TYPE_LONGLONG;  q += 2; break;
                        case 'u': t[i] = APR_DBD_TYPE_ULONGLONG; q += 2; break;
                        }
                        break;
                    case 'd': t[i] = APR_DBD_TYPE_LONG;   q++; break;
                    case 'u': t[i] = APR_DBD_TYPE_ULONG;  q++; break;
                    case 'f': t[i] = APR_DBD_TYPE_DOUBLE; q++; break;
                    }
                    break;
                case 'p':
                    if (q[2] == 'D') {
                        switch (q[3]) {
                        case 't': t[i] = APR_DBD_TYPE_TEXT;       q += 2; break;
                        case 'i': t[i] = APR_DBD_TYPE_TIME;       q += 2; break;
                        case 'd': t[i] = APR_DBD_TYPE_DATE;       q += 2; break;
                        case 'a': t[i] = APR_DBD_TYPE_DATETIME;   q += 2; break;
                        case 's': t[i] = APR_DBD_TYPE_TIMESTAMP;  q += 2; break;
                        case 'z': t[i] = APR_DBD_TYPE_ZTIMESTAMP; q += 2; break;
                        case 'b': t[i] = APR_DBD_TYPE_BLOB;       q += 2; break;
                        case 'c': t[i] = APR_DBD_TYPE_CLOB;       q += 2; break;
                        case 'n': t[i] = APR_DBD_TYPE_NULL;       q += 2; break;
                        }
                    }
                    break;
                }
                q++;

                switch (t[i]) {
                case APR_DBD_TYPE_NONE: /* by default, we expect strings */
                    t[i] = APR_DBD_TYPE_STRING;
                    break;
                case APR_DBD_TYPE_BLOB:
                case APR_DBD_TYPE_CLOB: /* three (3) more values passed in */
                    nvals += 3;
                    break;
                default:
                    break;
                }

                /* insert database specific parameter reference */
                p += apr_snprintf(p, qlen - (p - pq), driver->pformat, ++i);
            } else if (q[1] == '%') { /* reduce %% to % */
                *p++ = *q++;
            } else {
                *p++ = *q;
            }
        } else {
            *p++ = *q;
        }
    }
    *p = '\0';

    return driver->prepare(pool,handle,pq,label,nargs,nvals,t,statement);
}

APU_DECLARE(int) apr_dbd_pquery(const apr_dbd_driver_t *driver,
                                apr_pool_t *pool,
                                apr_dbd_t *handle, int *nrows,
                                apr_dbd_prepared_t *statement,
                                int nargs, const char **args)
{
    return driver->pquery(pool,handle,nrows,statement,args);
}

APU_DECLARE(int) apr_dbd_pselect(const apr_dbd_driver_t *driver,
                                 apr_pool_t *pool,
                                 apr_dbd_t *handle, apr_dbd_results_t **res,
                                 apr_dbd_prepared_t *statement, int random,
                                 int nargs, const char **args)
{
    return driver->pselect(pool,handle,res,statement,random,args);
}

APU_DECLARE_NONSTD(int) apr_dbd_pvquery(const apr_dbd_driver_t *driver,
                                        apr_pool_t *pool,
                                        apr_dbd_t *handle, int *nrows,
                                        apr_dbd_prepared_t *statement, ...)
{
    int ret;
    va_list args;
    va_start(args, statement);
    ret = driver->pvquery(pool,handle,nrows,statement,args);
    va_end(args);
    return ret;
}

APU_DECLARE_NONSTD(int) apr_dbd_pvselect(const apr_dbd_driver_t *driver,
                                         apr_pool_t *pool, apr_dbd_t *handle,
                                         apr_dbd_results_t **res,
                                         apr_dbd_prepared_t *statement,
                                         int random, ...)
{
    int ret;
    va_list args;
    va_start(args, random);
    ret = driver->pvselect(pool,handle,res,statement,random,args);
    va_end(args);
    return ret;
}

APU_DECLARE(int) apr_dbd_pbquery(const apr_dbd_driver_t *driver,
                                 apr_pool_t *pool,
                                 apr_dbd_t *handle, int *nrows,
                                 apr_dbd_prepared_t *statement,
                                 const void **args)
{
    return driver->pbquery(pool,handle,nrows,statement,args);
}

APU_DECLARE(int) apr_dbd_pbselect(const apr_dbd_driver_t *driver,
                                  apr_pool_t *pool,
                                  apr_dbd_t *handle, apr_dbd_results_t **res,
                                  apr_dbd_prepared_t *statement, int random,
                                  const void **args)
{
    return driver->pbselect(pool,handle,res,statement,random,args);
}

APU_DECLARE_NONSTD(int) apr_dbd_pvbquery(const apr_dbd_driver_t *driver,
                                         apr_pool_t *pool,
                                         apr_dbd_t *handle, int *nrows,
                                         apr_dbd_prepared_t *statement, ...)
{
    int ret;
    va_list args;
    va_start(args, statement);
    ret = driver->pvbquery(pool,handle,nrows,statement,args);
    va_end(args);
    return ret;
}

APU_DECLARE_NONSTD(int) apr_dbd_pvbselect(const apr_dbd_driver_t *driver,
                                          apr_pool_t *pool, apr_dbd_t *handle,
                                          apr_dbd_results_t **res,
                                          apr_dbd_prepared_t *statement,
                                          int random, ...)
{
    int ret;
    va_list args;
    va_start(args, random);
    ret = driver->pvbselect(pool,handle,res,statement,random,args);
    va_end(args);
    return ret;
}

APU_DECLARE(apr_status_t) apr_dbd_datum_get(const apr_dbd_driver_t *driver,
                                            apr_dbd_row_t *row, int col,
                                            apr_dbd_type_e type, void *data)
{
    return driver->datum_get(row,col,type,data);
}
