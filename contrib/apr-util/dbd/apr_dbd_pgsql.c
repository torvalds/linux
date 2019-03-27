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

#include "apu.h"

#if APU_HAVE_PGSQL

#include "apu_config.h"

#include <ctype.h>
#include <stdlib.h>

#ifdef HAVE_LIBPQ_FE_H
#include <libpq-fe.h>
#elif defined(HAVE_POSTGRESQL_LIBPQ_FE_H)
#include <postgresql/libpq-fe.h>
#endif

#include "apr_strings.h"
#include "apr_time.h"
#include "apr_buckets.h"

#include "apr_dbd_internal.h"

struct apr_dbd_transaction_t {
    int mode;
    int errnum;
    apr_dbd_t *handle;
};

struct apr_dbd_t {
    PGconn *conn;
    apr_dbd_transaction_t *trans;
};

struct apr_dbd_results_t {
    int random;
    PGconn *handle;
    PGresult *res;
    size_t ntuples;
    size_t sz;
    size_t index;
    apr_pool_t *pool;
};

struct apr_dbd_row_t {
    int n;
    apr_dbd_results_t *res;
};

struct apr_dbd_prepared_t {
    const char *name;
    int prepared;
    int nargs;
    int nvals;
    apr_dbd_type_e *types;
};

#define dbd_pgsql_is_success(x) (((x) == PGRES_EMPTY_QUERY) \
                                 || ((x) == PGRES_COMMAND_OK) \
                                 || ((x) == PGRES_TUPLES_OK))

static apr_status_t clear_result(void *data)
{
    PQclear(data);
    return APR_SUCCESS;
}

static int dbd_pgsql_select(apr_pool_t *pool, apr_dbd_t *sql,
                            apr_dbd_results_t **results,
                            const char *query, int seek)
{
    PGresult *res;
    int ret;
    if ( sql->trans && sql->trans->errnum ) {
        return sql->trans->errnum;
    }
    if (seek) { /* synchronous query */
        if (TXN_IGNORE_ERRORS(sql->trans)) {
            PGresult *res = PQexec(sql->conn, "SAVEPOINT APR_DBD_TXN_SP");
            if (res) {
                int ret = PQresultStatus(res);
                PQclear(res);
                if (!dbd_pgsql_is_success(ret)) {
                    sql->trans->errnum = ret;
                    return PGRES_FATAL_ERROR;
                }
            } else {
                return sql->trans->errnum = PGRES_FATAL_ERROR;
            }
        }
        res = PQexec(sql->conn, query);
        if (res) {
            ret = PQresultStatus(res);
            if (dbd_pgsql_is_success(ret)) {
                ret = 0;
            } else {
                PQclear(res);
            }
        } else {
            ret = PGRES_FATAL_ERROR;
        }
        if (ret != 0) {
            if (TXN_IGNORE_ERRORS(sql->trans)) {
                PGresult *res = PQexec(sql->conn,
                                       "ROLLBACK TO SAVEPOINT APR_DBD_TXN_SP");
                if (res) {
                    int ret = PQresultStatus(res);
                    PQclear(res);
                    if (!dbd_pgsql_is_success(ret)) {
                        sql->trans->errnum = ret;
                        return PGRES_FATAL_ERROR;
                    }
                } else {
                    return sql->trans->errnum = PGRES_FATAL_ERROR;
                }
            } else if (TXN_NOTICE_ERRORS(sql->trans)){
                sql->trans->errnum = ret;
            }
            return ret;
        } else {
            if (TXN_IGNORE_ERRORS(sql->trans)) {
                PGresult *res = PQexec(sql->conn,
                                       "RELEASE SAVEPOINT APR_DBD_TXN_SP");
                if (res) {
                    int ret = PQresultStatus(res);
                    PQclear(res);
                    if (!dbd_pgsql_is_success(ret)) {
                        sql->trans->errnum = ret;
                        return PGRES_FATAL_ERROR;
                    }
                } else {
                    return sql->trans->errnum = PGRES_FATAL_ERROR;
                }
            }
        }
        if (!*results) {
            *results = apr_pcalloc(pool, sizeof(apr_dbd_results_t));
        }
        (*results)->res = res;
        (*results)->ntuples = PQntuples(res);
        (*results)->sz = PQnfields(res);
        (*results)->random = seek;
        (*results)->pool = pool;
        apr_pool_cleanup_register(pool, res, clear_result,
                                  apr_pool_cleanup_null);
    }
    else {
        if (TXN_IGNORE_ERRORS(sql->trans)) {
            PGresult *res = PQexec(sql->conn, "SAVEPOINT APR_DBD_TXN_SP");
            if (res) {
                int ret = PQresultStatus(res);
                PQclear(res);
                if (!dbd_pgsql_is_success(ret)) {
                    sql->trans->errnum = ret;
                    return PGRES_FATAL_ERROR;
                }
            } else {
                return sql->trans->errnum = PGRES_FATAL_ERROR;
            }
        }
        if (PQsendQuery(sql->conn, query) == 0) {
            if (TXN_IGNORE_ERRORS(sql->trans)) {
                PGresult *res = PQexec(sql->conn,
                                       "ROLLBACK TO SAVEPOINT APR_DBD_TXN_SP");
                if (res) {
                    int ret = PQresultStatus(res);
                    PQclear(res);
                    if (!dbd_pgsql_is_success(ret)) {
                        sql->trans->errnum = ret;
                        return PGRES_FATAL_ERROR;
                    }
                } else {
                    return sql->trans->errnum = PGRES_FATAL_ERROR;
                }
            } else if (TXN_NOTICE_ERRORS(sql->trans)){
                sql->trans->errnum = 1;
            }
            return 1;
        } else {
            if (TXN_IGNORE_ERRORS(sql->trans)) {
                PGresult *res = PQexec(sql->conn,
                                       "RELEASE SAVEPOINT APR_DBD_TXN_SP");
                if (res) {
                    int ret = PQresultStatus(res);
                    PQclear(res);
                    if (!dbd_pgsql_is_success(ret)) {
                        sql->trans->errnum = ret;
                        return PGRES_FATAL_ERROR;
                    }
                } else {
                    return sql->trans->errnum = PGRES_FATAL_ERROR;
                }
            }
        }
        if (*results == NULL) {
            *results = apr_pcalloc(pool, sizeof(apr_dbd_results_t));
        }
        (*results)->random = seek;
        (*results)->handle = sql->conn;
        (*results)->pool = pool;
    }
    return 0;
}

static const char *dbd_pgsql_get_name(const apr_dbd_results_t *res, int n)
{
    if (res->res) {
        if ((n>=0) && (PQnfields(res->res) > n)) {
            return PQfname(res->res,n);
        }
    }
    return NULL;
}

static int dbd_pgsql_get_row(apr_pool_t *pool, apr_dbd_results_t *res,
                             apr_dbd_row_t **rowp, int rownum)
{
    apr_dbd_row_t *row = *rowp;
    int sequential = ((rownum >= 0) && res->random) ? 0 : 1;

    if (row == NULL) {
        row = apr_palloc(pool, sizeof(apr_dbd_row_t));
        *rowp = row;
        row->res = res;
        if ( sequential ) {
            row->n = 0;
        }
        else {
            if (rownum > 0) {
                row->n = --rownum;
            }
            else {
                return -1; /* invalid row */
            }
        }
    }
    else {
        if ( sequential ) {
            ++row->n;
        }
        else {
            if (rownum > 0) {
                row->n = --rownum;
            }
            else {
                return -1; /* invalid row */
            }
        }
    }

    if (res->random) {
        if ((row->n >= 0) && (size_t)row->n >= res->ntuples) {
            *rowp = NULL;
            apr_pool_cleanup_run(res->pool, res->res, clear_result);
            res->res = NULL;
            return -1;
        }
    }
    else {
        if ((row->n >= 0) && (size_t)row->n >= res->ntuples) {
            /* no data; we have to fetch some */
            row->n -= res->ntuples;
            if (res->res != NULL) {
                PQclear(res->res);
            }
            res->res = PQgetResult(res->handle);
            if (res->res) {
                res->ntuples = PQntuples(res->res);
                while (res->ntuples == 0) {
                    /* if we got an empty result, clear it, wait a mo, try
                     * again */
                    PQclear(res->res);
                    apr_sleep(100000);        /* 0.1 secs */
                    res->res = PQgetResult(res->handle);
                    if (res->res) {
                        res->ntuples = PQntuples(res->res);
                    }
                    else {
                        return -1;
                    }
                }
                if (res->sz == 0) {
                    res->sz = PQnfields(res->res);
                }
            }
            else {
                return -1;
            }
        }
    }
    return 0;
}

static const char *dbd_pgsql_get_entry(const apr_dbd_row_t *row, int n)
{
    return PQgetvalue(row->res->res, row->n, n);
}

static apr_status_t dbd_pgsql_datum_get(const apr_dbd_row_t *row, int n,
                                        apr_dbd_type_e type, void *data)
{
    if (PQgetisnull(row->res->res, row->n, n)) {
        return APR_ENOENT;
    }

    switch (type) {
    case APR_DBD_TYPE_TINY:
        *(char*)data = atoi(PQgetvalue(row->res->res, row->n, n));
        break;
    case APR_DBD_TYPE_UTINY:
        *(unsigned char*)data = atoi(PQgetvalue(row->res->res, row->n, n));
        break;
    case APR_DBD_TYPE_SHORT:
        *(short*)data = atoi(PQgetvalue(row->res->res, row->n, n));
        break;
    case APR_DBD_TYPE_USHORT:
        *(unsigned short*)data = atoi(PQgetvalue(row->res->res, row->n, n));
        break;
    case APR_DBD_TYPE_INT:
        *(int*)data = atoi(PQgetvalue(row->res->res, row->n, n));
        break;
    case APR_DBD_TYPE_UINT:
        *(unsigned int*)data = atoi(PQgetvalue(row->res->res, row->n, n));
        break;
    case APR_DBD_TYPE_LONG:
        *(long*)data = atol(PQgetvalue(row->res->res, row->n, n));
        break;
    case APR_DBD_TYPE_ULONG:
        *(unsigned long*)data = atol(PQgetvalue(row->res->res, row->n, n));
        break;
    case APR_DBD_TYPE_LONGLONG:
        *(apr_int64_t*)data = apr_atoi64(PQgetvalue(row->res->res, row->n, n));
        break;
    case APR_DBD_TYPE_ULONGLONG:
        *(apr_uint64_t*)data = apr_atoi64(PQgetvalue(row->res->res, row->n, n));
        break;
    case APR_DBD_TYPE_FLOAT:
        *(float*)data = (float)atof(PQgetvalue(row->res->res, row->n, n));
        break;
    case APR_DBD_TYPE_DOUBLE:
        *(double*)data = atof(PQgetvalue(row->res->res, row->n, n));
        break;
    case APR_DBD_TYPE_STRING:
    case APR_DBD_TYPE_TEXT:
    case APR_DBD_TYPE_TIME:
    case APR_DBD_TYPE_DATE:
    case APR_DBD_TYPE_DATETIME:
    case APR_DBD_TYPE_TIMESTAMP:
    case APR_DBD_TYPE_ZTIMESTAMP:
        *(char**)data = PQgetvalue(row->res->res, row->n, n);
        break;
    case APR_DBD_TYPE_BLOB:
    case APR_DBD_TYPE_CLOB:
        {
        apr_bucket *e;
        apr_bucket_brigade *b = (apr_bucket_brigade*)data;

        e = apr_bucket_pool_create(PQgetvalue(row->res->res, row->n, n),
                                   PQgetlength(row->res->res, row->n, n),
                                   row->res->pool, b->bucket_alloc);
        APR_BRIGADE_INSERT_TAIL(b, e);
        }
        break;
    case APR_DBD_TYPE_NULL:
        *(void**)data = NULL;
        break;
    default:
        return APR_EGENERAL;
    }

    return APR_SUCCESS;
}

static const char *dbd_pgsql_error(apr_dbd_t *sql, int n)
{
    return PQerrorMessage(sql->conn);
}

static int dbd_pgsql_query(apr_dbd_t *sql, int *nrows, const char *query)
{
    PGresult *res;
    int ret;
    if (sql->trans && sql->trans->errnum) {
        return sql->trans->errnum;
    }

    if (TXN_IGNORE_ERRORS(sql->trans)) {
        PGresult *res = PQexec(sql->conn, "SAVEPOINT APR_DBD_TXN_SP");
        if (res) {
            int ret = PQresultStatus(res);
            PQclear(res);
            if (!dbd_pgsql_is_success(ret)) {
                sql->trans->errnum = ret;
                return PGRES_FATAL_ERROR;
            }
        } else {
            return sql->trans->errnum = PGRES_FATAL_ERROR;
        }
    }

    res = PQexec(sql->conn, query);
    if (res) {
        ret = PQresultStatus(res);
        if (dbd_pgsql_is_success(ret)) {
            /* ugh, making 0 return-success doesn't fit */
            ret = 0;
        }
        *nrows = atoi(PQcmdTuples(res));
        PQclear(res);
    }
    else {
        ret = PGRES_FATAL_ERROR;
    }
    
    if (ret != 0){
        if (TXN_IGNORE_ERRORS(sql->trans)) {
            PGresult *res = PQexec(sql->conn,
                                   "ROLLBACK TO SAVEPOINT APR_DBD_TXN_SP");
            if (res) {
                int ret = PQresultStatus(res);
                PQclear(res);
                if (!dbd_pgsql_is_success(ret)) {
                    sql->trans->errnum = ret;
                    return PGRES_FATAL_ERROR;
                }
            } else {
                sql->trans->errnum = ret;
                return PGRES_FATAL_ERROR;
            }
        } else if (TXN_NOTICE_ERRORS(sql->trans)){
            sql->trans->errnum = ret;
        }
    } else {
        if (TXN_IGNORE_ERRORS(sql->trans)) {
            PGresult *res = PQexec(sql->conn,
                                   "RELEASE SAVEPOINT APR_DBD_TXN_SP");
            if (res) {
                int ret = PQresultStatus(res);
                PQclear(res);
                if (!dbd_pgsql_is_success(ret)) {
                    sql->trans->errnum = ret;
                    return PGRES_FATAL_ERROR;
                }
            } else {
                sql->trans->errnum = ret;
                return PGRES_FATAL_ERROR;
            }
        }
    }

    return ret;
}

static const char *dbd_pgsql_escape(apr_pool_t *pool, const char *arg,
                                    apr_dbd_t *sql)
{
    size_t len = strlen(arg);
    char *ret = apr_palloc(pool, 2*len + 2);
    PQescapeStringConn(sql->conn, ret, arg, len, NULL);
    return ret;
}

static int dbd_pgsql_prepare(apr_pool_t *pool, apr_dbd_t *sql,
                             const char *query, const char *label,
                             int nargs, int nvals, apr_dbd_type_e *types,
                             apr_dbd_prepared_t **statement)
{
    char *sqlcmd;
    char *sqlptr;
    size_t length, qlen;
    int i = 0;
    const char **args;
    size_t alen;
    int ret;
    PGresult *res;

    if (!*statement) {
        *statement = apr_palloc(pool, sizeof(apr_dbd_prepared_t));
    }
    (*statement)->nargs = nargs;
    (*statement)->nvals = nvals;
    (*statement)->types = types;

    args = apr_palloc(pool, nargs * sizeof(*args));

    qlen = strlen(query);
    length = qlen + 1;

    for (i = 0; i < nargs; i++) {
        switch (types[i]) {
        case APR_DBD_TYPE_TINY: 
        case APR_DBD_TYPE_UTINY: 
        case APR_DBD_TYPE_SHORT: 
        case APR_DBD_TYPE_USHORT:
            args[i] = "smallint";
            break;
        case APR_DBD_TYPE_INT: 
        case APR_DBD_TYPE_UINT:
            args[i] = "integer";
            break;
        case APR_DBD_TYPE_LONG: 
        case APR_DBD_TYPE_ULONG:   
        case APR_DBD_TYPE_LONGLONG: 
        case APR_DBD_TYPE_ULONGLONG:
            args[i] = "bigint";
            break;
        case APR_DBD_TYPE_FLOAT:
            args[i] = "real";
            break;
        case APR_DBD_TYPE_DOUBLE:
            args[i] = "double precision";
            break;
        case APR_DBD_TYPE_TEXT:
            args[i] = "text";
            break;
        case APR_DBD_TYPE_TIME:
            args[i] = "time";
            break;
        case APR_DBD_TYPE_DATE:
            args[i] = "date";
            break;
        case APR_DBD_TYPE_DATETIME:
        case APR_DBD_TYPE_TIMESTAMP:
            args[i] = "timestamp";
            break;
        case APR_DBD_TYPE_ZTIMESTAMP:
            args[i] = "timestamp with time zone";
            break;
        case APR_DBD_TYPE_BLOB:
        case APR_DBD_TYPE_CLOB:
            args[i] = "bytea";
            break;
        case APR_DBD_TYPE_NULL:
            args[i] = "varchar"; /* XXX Eh? */
            break;
        default:
            args[i] = "varchar";
            break;
        }
        length += 1 + strlen(args[i]);
    }

    if (!label) {
        /* don't really prepare; use in execParams instead */
        (*statement)->prepared = 0;
        (*statement)->name = apr_pstrdup(pool, query);
        return 0;
    }
    (*statement)->name = apr_pstrdup(pool, label);

    /* length of SQL query that prepares this statement */
    length = 8 + strlen(label) + 2 + 4 + length + 1;
    sqlcmd = apr_palloc(pool, length);
    sqlptr = sqlcmd;
    memcpy(sqlptr, "PREPARE ", 8);
    sqlptr += 8;
    length = strlen(label);
    memcpy(sqlptr, label, length);
    sqlptr += length;
    if (nargs > 0) {
        memcpy(sqlptr, " (",2);
        sqlptr += 2;
        for (i=0; i < nargs; ++i) {
            alen = strlen(args[i]);
            memcpy(sqlptr, args[i], alen);
            sqlptr += alen;
            *sqlptr++ = ',';
        }
        sqlptr[-1] =  ')';
    }
    memcpy(sqlptr, " AS ", 4);
    sqlptr += 4;
    memcpy(sqlptr, query, qlen);
    sqlptr += qlen;
    *sqlptr = 0;

    res = PQexec(sql->conn, sqlcmd);
    if ( res ) {
        ret = PQresultStatus(res);
        if (dbd_pgsql_is_success(ret)) {
            ret = 0;
        }
        /* Hmmm, do we do this here or register it on the pool? */
        PQclear(res);
    }
    else {
        ret = PGRES_FATAL_ERROR;
    }
    (*statement)->prepared = 1;

    return ret;
}

static int dbd_pgsql_pquery_internal(apr_pool_t *pool, apr_dbd_t *sql,
                                     int *nrows, apr_dbd_prepared_t *statement,
                                     const char **values,
                                     const int *len, const int *fmt)
{
    int ret;
    PGresult *res;

    if (TXN_IGNORE_ERRORS(sql->trans)) {
        PGresult *res = PQexec(sql->conn, "SAVEPOINT APR_DBD_TXN_SP");
        if (res) {
            int ret = PQresultStatus(res);
            PQclear(res);
            if (!dbd_pgsql_is_success(ret)) {
                sql->trans->errnum = ret;
                return PGRES_FATAL_ERROR;
            }
        } else {
            return sql->trans->errnum = PGRES_FATAL_ERROR;
        }
    }

    if (statement->prepared) {
        res = PQexecPrepared(sql->conn, statement->name, statement->nargs,
                             values, len, fmt, 0);
    }
    else {
        res = PQexecParams(sql->conn, statement->name, statement->nargs, 0,
                           values, len, fmt, 0);
    }
    if (res) {
        ret = PQresultStatus(res);
        if (dbd_pgsql_is_success(ret)) {
            ret = 0;
        }
        *nrows = atoi(PQcmdTuples(res));
        PQclear(res);
    }
    else {
        ret = PGRES_FATAL_ERROR;
    }

    if (ret != 0){
        if (TXN_IGNORE_ERRORS(sql->trans)) {
            PGresult *res = PQexec(sql->conn,
                                   "ROLLBACK TO SAVEPOINT APR_DBD_TXN_SP");
            if (res) {
                int ret = PQresultStatus(res);
                PQclear(res);
                if (!dbd_pgsql_is_success(ret)) {
                    sql->trans->errnum = ret;
                    return PGRES_FATAL_ERROR;
                }
            } else {
                sql->trans->errnum = ret;
                return PGRES_FATAL_ERROR;
            }
        } else if (TXN_NOTICE_ERRORS(sql->trans)){
            sql->trans->errnum = ret;
        }
    } else {
        if (TXN_IGNORE_ERRORS(sql->trans)) {
            PGresult *res = PQexec(sql->conn,
                                   "RELEASE SAVEPOINT APR_DBD_TXN_SP");
            if (res) {
                int ret = PQresultStatus(res);
                PQclear(res);
                if (!dbd_pgsql_is_success(ret)) {
                    sql->trans->errnum = ret;
                    return PGRES_FATAL_ERROR;
                }
            } else {
                sql->trans->errnum = ret;
                return PGRES_FATAL_ERROR;
            }
        }
    }

    return ret;
}

static void dbd_pgsql_bind(apr_dbd_prepared_t *statement,
                           const char **values,
                           const char **val, int *len, int *fmt)
{
    int i, j;

    for (i = 0, j = 0; i < statement->nargs; i++, j++) {
        if (values[j] == NULL) {
            val[i] = NULL;
        }
        else {
            switch (statement->types[i]) {
            case APR_DBD_TYPE_BLOB:
            case APR_DBD_TYPE_CLOB:
                val[i] = (char *)values[j];
                len[i] = atoi(values[++j]);
                fmt[i] = 1;

                /* skip table and column */
                j += 2;
                break;
            default:
                val[i] = values[j];
                break;
            }
        }
    }

    return;
}

static int dbd_pgsql_pquery(apr_pool_t *pool, apr_dbd_t *sql,
                            int *nrows, apr_dbd_prepared_t *statement,
                            const char **values)
{
    int *len, *fmt;
    const char **val;

    if (sql->trans && sql->trans->errnum) {
        return sql->trans->errnum;
    }

    val = apr_palloc(pool, sizeof(*val) * statement->nargs);
    len = apr_pcalloc(pool, sizeof(*len) * statement->nargs);
    fmt = apr_pcalloc(pool, sizeof(*fmt) * statement->nargs);

    dbd_pgsql_bind(statement, values, val, len, fmt);

    return dbd_pgsql_pquery_internal(pool, sql, nrows, statement,
                                     val, len, fmt);
}

static int dbd_pgsql_pvquery(apr_pool_t *pool, apr_dbd_t *sql,
                             int *nrows, apr_dbd_prepared_t *statement,
                             va_list args)
{
    const char **values;
    int i;

    if (sql->trans && sql->trans->errnum) {
        return sql->trans->errnum;
    }

    values = apr_palloc(pool, sizeof(*values) * statement->nvals);

    for (i = 0; i < statement->nvals; i++) {
        values[i] = va_arg(args, const char*);
    }

    return dbd_pgsql_pquery(pool, sql, nrows, statement, values);
}

static int dbd_pgsql_pselect_internal(apr_pool_t *pool, apr_dbd_t *sql,
                                      apr_dbd_results_t **results,
                                      apr_dbd_prepared_t *statement,
                                      int seek, const char **values,
                                      const int *len, const int *fmt)
{
    PGresult *res;
    int rv;
    int ret = 0;

    if (seek) { /* synchronous query */
        if (TXN_IGNORE_ERRORS(sql->trans)) {
            PGresult *res = PQexec(sql->conn, "SAVEPOINT APR_DBD_TXN_SP");
            if (res) {
                int ret = PQresultStatus(res);
                PQclear(res);
                if (!dbd_pgsql_is_success(ret)) {
                    sql->trans->errnum = ret;
                    return PGRES_FATAL_ERROR;
                }
            } else {
                sql->trans->errnum = ret;
                return PGRES_FATAL_ERROR;
            }
        }
        if (statement->prepared) {
            res = PQexecPrepared(sql->conn, statement->name, statement->nargs,
                                 values, len, fmt, 0);
        }
        else {
            res = PQexecParams(sql->conn, statement->name, statement->nargs, 0,
                               values, len, fmt, 0);
        }
        if (res) {
            ret = PQresultStatus(res);
            if (dbd_pgsql_is_success(ret)) {
                ret = 0;
            }
            else {
                PQclear(res);
            }
        }
        else {
            ret = PGRES_FATAL_ERROR;
        }
        if (ret != 0) {
            if (TXN_IGNORE_ERRORS(sql->trans)) {
                PGresult *res = PQexec(sql->conn,
                                       "ROLLBACK TO SAVEPOINT APR_DBD_TXN_SP");
                if (res) {
                    int ret = PQresultStatus(res);
                    PQclear(res);
                    if (!dbd_pgsql_is_success(ret)) {
                        sql->trans->errnum = ret;
                        return PGRES_FATAL_ERROR;
                    }
                } else {
                    sql->trans->errnum = ret;
                    return PGRES_FATAL_ERROR;
                }
            } else if (TXN_NOTICE_ERRORS(sql->trans)){
                sql->trans->errnum = ret;
            }
            return ret;
        } else {
            if (TXN_IGNORE_ERRORS(sql->trans)) {
                PGresult *res = PQexec(sql->conn,
                                       "RELEASE SAVEPOINT APR_DBD_TXN_SP");
                if (res) {
                    int ret = PQresultStatus(res);
                    PQclear(res);
                    if (!dbd_pgsql_is_success(ret)) {
                        sql->trans->errnum = ret;
                        return PGRES_FATAL_ERROR;
                    }
                } else {
                    sql->trans->errnum = ret;
                    return PGRES_FATAL_ERROR;
                }
            }
        }
        if (!*results) {
            *results = apr_pcalloc(pool, sizeof(apr_dbd_results_t));
        }
        (*results)->res = res;
        (*results)->ntuples = PQntuples(res);
        (*results)->sz = PQnfields(res);
        (*results)->random = seek;
        (*results)->pool = pool;
        apr_pool_cleanup_register(pool, res, clear_result,
                                  apr_pool_cleanup_null);
    }
    else {
        if (TXN_IGNORE_ERRORS(sql->trans)) {
            PGresult *res = PQexec(sql->conn, "SAVEPOINT APR_DBD_TXN_SP");
            if (res) {
                int ret = PQresultStatus(res);
                PQclear(res);
                if (!dbd_pgsql_is_success(ret)) {
                    sql->trans->errnum = ret;
                    return PGRES_FATAL_ERROR;
                }
            } else {
                sql->trans->errnum = ret;
                return PGRES_FATAL_ERROR;
            }
        }
        if (statement->prepared) {
            rv = PQsendQueryPrepared(sql->conn, statement->name,
                                     statement->nargs, values, len, fmt, 0);
        }
        else {
            rv = PQsendQueryParams(sql->conn, statement->name,
                                   statement->nargs, 0, values, len, fmt, 0);
        }
        if (rv == 0) {
            if (TXN_IGNORE_ERRORS(sql->trans)) {
                PGresult *res = PQexec(sql->conn,
                                       "ROLLBACK TO SAVEPOINT APR_DBD_TXN_SP");
                if (res) {
                    int ret = PQresultStatus(res);
                    PQclear(res);
                    if (!dbd_pgsql_is_success(ret)) {
                        sql->trans->errnum = ret;
                        return PGRES_FATAL_ERROR;
                    }
                } else {
                    sql->trans->errnum = ret;
                    return PGRES_FATAL_ERROR;
                }
            } else if (TXN_NOTICE_ERRORS(sql->trans)){
                sql->trans->errnum = 1;
            }
            return 1;
        } else {
            if (TXN_IGNORE_ERRORS(sql->trans)) {
                PGresult *res = PQexec(sql->conn,
                                       "RELEASE SAVEPOINT APR_DBD_TXN_SP");
                if (res) {
                    int ret = PQresultStatus(res);
                    PQclear(res);
                    if (!dbd_pgsql_is_success(ret)) {
                        sql->trans->errnum = ret;
                        return PGRES_FATAL_ERROR;
                    }
                } else {
                    sql->trans->errnum = ret;
                    return PGRES_FATAL_ERROR;
                }
            }
        }
        if (!*results) {
            *results = apr_pcalloc(pool, sizeof(apr_dbd_results_t));
        }
        (*results)->random = seek;
        (*results)->handle = sql->conn;
        (*results)->pool = pool;
    }

    return ret;
}

static int dbd_pgsql_pselect(apr_pool_t *pool, apr_dbd_t *sql,
                             apr_dbd_results_t **results,
                             apr_dbd_prepared_t *statement,
                             int seek, const char **values)
{
    int *len, *fmt;
    const char **val;

    if (sql->trans && sql->trans->errnum) {
        return sql->trans->errnum;
    }

    val = apr_palloc(pool, sizeof(*val) * statement->nargs);
    len = apr_pcalloc(pool, sizeof(*len) * statement->nargs);
    fmt = apr_pcalloc(pool, sizeof(*fmt) * statement->nargs);

    dbd_pgsql_bind(statement, values, val, len, fmt);

    return dbd_pgsql_pselect_internal(pool, sql, results, statement,
                                      seek, val, len, fmt);
}

static int dbd_pgsql_pvselect(apr_pool_t *pool, apr_dbd_t *sql,
                              apr_dbd_results_t **results,
                              apr_dbd_prepared_t *statement,
                              int seek, va_list args)
{
    const char **values;
    int i;

    if (sql->trans && sql->trans->errnum) {
        return sql->trans->errnum;
    }

    values = apr_palloc(pool, sizeof(*values) * statement->nvals);

    for (i = 0; i < statement->nvals; i++) {
        values[i] = va_arg(args, const char*);
    }

    return dbd_pgsql_pselect(pool, sql, results, statement, seek, values);
}

static void dbd_pgsql_bbind(apr_pool_t *pool, apr_dbd_prepared_t * statement,
                            const void **values,
                            const char **val, int *len, int *fmt)
{
    int i, j;
    apr_dbd_type_e type;

    for (i = 0, j = 0; i < statement->nargs; i++, j++) {
        type = (values[j] == NULL ? APR_DBD_TYPE_NULL : statement->types[i]);

        switch (type) {
        case APR_DBD_TYPE_TINY:
            val[i] = apr_itoa(pool, *(char*)values[j]);
            break;
        case APR_DBD_TYPE_UTINY:
            val[i] = apr_itoa(pool, *(unsigned char*)values[j]);
            break;
        case APR_DBD_TYPE_SHORT:
            val[i] = apr_itoa(pool, *(short*)values[j]);
            break;
        case APR_DBD_TYPE_USHORT:
            val[i] = apr_itoa(pool, *(unsigned short*)values[j]);
            break;
        case APR_DBD_TYPE_INT:
            val[i] = apr_itoa(pool, *(int*)values[j]);
            break;
        case APR_DBD_TYPE_UINT:
            val[i] = apr_itoa(pool, *(unsigned int*)values[j]);
            break;
        case APR_DBD_TYPE_LONG:
            val[i] = apr_ltoa(pool, *(long*)values[j]);
            break;
        case APR_DBD_TYPE_ULONG:
            val[i] = apr_ltoa(pool, *(unsigned long*)values[j]);
            break;
        case APR_DBD_TYPE_LONGLONG:
            val[i] = apr_psprintf(pool, "%" APR_INT64_T_FMT,
                                  *(apr_int64_t*)values[j]);
            break;
        case APR_DBD_TYPE_ULONGLONG:
            val[i] = apr_psprintf(pool, "%" APR_UINT64_T_FMT,
                                  *(apr_uint64_t*)values[j]);
            break;
        case APR_DBD_TYPE_FLOAT:
            val[i] = apr_psprintf(pool, "%f", *(float*)values[j]);
            break;
        case APR_DBD_TYPE_DOUBLE:
            val[i] = apr_psprintf(pool, "%lf", *(double*)values[j]);
            break;
        case APR_DBD_TYPE_STRING:
        case APR_DBD_TYPE_TEXT:
        case APR_DBD_TYPE_TIME:
        case APR_DBD_TYPE_DATE:
        case APR_DBD_TYPE_DATETIME:
        case APR_DBD_TYPE_TIMESTAMP:
        case APR_DBD_TYPE_ZTIMESTAMP:
            val[i] = values[j];
            break;
        case APR_DBD_TYPE_BLOB:
        case APR_DBD_TYPE_CLOB:
            val[i] = (char*)values[j];
            len[i] = *(apr_size_t*)values[++j];
            fmt[i] = 1;

            /* skip table and column */
            j += 2;
            break;
        case APR_DBD_TYPE_NULL:
        default:
            val[i] = NULL;
            break;
        }
    }

    return;
}

static int dbd_pgsql_pbquery(apr_pool_t * pool, apr_dbd_t * sql,
                             int *nrows, apr_dbd_prepared_t * statement,
                             const void **values)
{
    int *len, *fmt;
    const char **val;

    if (sql->trans && sql->trans->errnum) {
        return sql->trans->errnum;
    }

    val = apr_palloc(pool, sizeof(*val) * statement->nargs);
    len = apr_pcalloc(pool, sizeof(*len) * statement->nargs);
    fmt = apr_pcalloc(pool, sizeof(*fmt) * statement->nargs);

    dbd_pgsql_bbind(pool, statement, values, val, len, fmt);

    return dbd_pgsql_pquery_internal(pool, sql, nrows, statement,
                                     val, len, fmt);
}

static int dbd_pgsql_pvbquery(apr_pool_t * pool, apr_dbd_t * sql,
                              int *nrows, apr_dbd_prepared_t * statement,
                              va_list args)
{
    const void **values;
    int i;

    if (sql->trans && sql->trans->errnum) {
        return sql->trans->errnum;
    }

    values = apr_palloc(pool, sizeof(*values) * statement->nvals);

    for (i = 0; i < statement->nvals; i++) {
        values[i] = va_arg(args, const void*);
    }

    return dbd_pgsql_pbquery(pool, sql, nrows, statement, values);
}

static int dbd_pgsql_pbselect(apr_pool_t * pool, apr_dbd_t * sql,
                              apr_dbd_results_t ** results,
                              apr_dbd_prepared_t * statement,
                              int seek, const void **values)
{
    int *len, *fmt;
    const char **val;

    if (sql->trans && sql->trans->errnum) {
        return sql->trans->errnum;
    }

    val = apr_palloc(pool, sizeof(*val) * statement->nargs);
    len = apr_pcalloc(pool, sizeof(*len) * statement->nargs);
    fmt = apr_pcalloc(pool, sizeof(*fmt) * statement->nargs);

    dbd_pgsql_bbind(pool, statement, values, val, len, fmt);

    return dbd_pgsql_pselect_internal(pool, sql, results, statement,
                                      seek, val, len, fmt);
}

static int dbd_pgsql_pvbselect(apr_pool_t * pool, apr_dbd_t * sql,
                               apr_dbd_results_t ** results,
                               apr_dbd_prepared_t * statement, int seek,
                               va_list args)
{
    const void **values;
    int i;

    if (sql->trans && sql->trans->errnum) {
        return sql->trans->errnum;
    }

    values = apr_palloc(pool, sizeof(*values) * statement->nvals);

    for (i = 0; i < statement->nvals; i++) {
        values[i] = va_arg(args, const void*);
    }

    return dbd_pgsql_pbselect(pool, sql, results, statement, seek, values);
}

static int dbd_pgsql_start_transaction(apr_pool_t *pool, apr_dbd_t *handle,
                                       apr_dbd_transaction_t **trans)
{
    int ret = 0;
    PGresult *res;

    /* XXX handle recursive transactions here */

    res = PQexec(handle->conn, "BEGIN TRANSACTION");
    if (res) {
        ret = PQresultStatus(res);
        if (dbd_pgsql_is_success(ret)) {
            ret = 0;
            if (!*trans) {
                *trans = apr_pcalloc(pool, sizeof(apr_dbd_transaction_t));
            }
        }
        PQclear(res);
        (*trans)->handle = handle;
        handle->trans = *trans;
    }
    else {
        ret = PGRES_FATAL_ERROR;
    }
    return ret;
}

static int dbd_pgsql_end_transaction(apr_dbd_transaction_t *trans)
{
    PGresult *res;
    int ret = -1;                /* no transaction is an error cond */
    if (trans) {
        /* rollback on error or explicit rollback request */
        if (trans->errnum || TXN_DO_ROLLBACK(trans)) {
            trans->errnum = 0;
            res = PQexec(trans->handle->conn, "ROLLBACK");
        }
        else {
            res = PQexec(trans->handle->conn, "COMMIT");
        }
        if (res) {
            ret = PQresultStatus(res);
            if (dbd_pgsql_is_success(ret)) {
                ret = 0;
            }
            PQclear(res);
        }
        else {
            ret = PGRES_FATAL_ERROR;
        }
        trans->handle->trans = NULL;
    }
    return ret;
}

static int dbd_pgsql_transaction_mode_get(apr_dbd_transaction_t *trans)
{
    if (!trans)
        return APR_DBD_TRANSACTION_COMMIT;

    return trans->mode;
}

static int dbd_pgsql_transaction_mode_set(apr_dbd_transaction_t *trans,
                                          int mode)
{
    if (!trans)
        return APR_DBD_TRANSACTION_COMMIT;

    return trans->mode = (mode & TXN_MODE_BITS);
}

static void null_notice_receiver(void *arg, const PGresult *res)
{
    /* nothing */
}

static void null_notice_processor(void *arg, const char *message)
{
    /* nothing */
}

static apr_dbd_t *dbd_pgsql_open(apr_pool_t *pool, const char *params,
                                 const char **error)
{
    apr_dbd_t *sql;
    
    PGconn *conn = PQconnectdb(params);

    /* if there's an error in the connect string or something we get
     * back a * bogus connection object, and things like PQreset are
     * liable to segfault, so just close it out now.  it would be nice
     * if we could give an indication of why we failed to connect... */
    if (PQstatus(conn) != CONNECTION_OK) {
        if (error) {
            *error = apr_pstrdup(pool, PQerrorMessage(conn));
        }
        PQfinish(conn);
        return NULL;
    }

    PQsetNoticeReceiver(conn, null_notice_receiver, NULL);
    PQsetNoticeProcessor(conn, null_notice_processor, NULL);

    sql = apr_pcalloc (pool, sizeof (*sql));

    sql->conn = conn;

    return sql;
}

static apr_status_t dbd_pgsql_close(apr_dbd_t *handle)
{
    PQfinish(handle->conn);
    return APR_SUCCESS;
}

static apr_status_t dbd_pgsql_check_conn(apr_pool_t *pool,
                                         apr_dbd_t *handle)
{
    if (PQstatus(handle->conn) != CONNECTION_OK) {
        PQreset(handle->conn);
        if (PQstatus(handle->conn) != CONNECTION_OK) {
            return APR_EGENERAL;
        }
    }
    return APR_SUCCESS;
}

static int dbd_pgsql_select_db(apr_pool_t *pool, apr_dbd_t *handle,
                               const char *name)
{
    return APR_ENOTIMPL;
}

static void *dbd_pgsql_native(apr_dbd_t *handle)
{
    return handle->conn;
}

static int dbd_pgsql_num_cols(apr_dbd_results_t* res)
{
    return res->sz;
}

static int dbd_pgsql_num_tuples(apr_dbd_results_t* res)
{
    if (res->random) {
        return res->ntuples;
    }
    else {
        return -1;
    }
}

APU_MODULE_DECLARE_DATA const apr_dbd_driver_t apr_dbd_pgsql_driver = {
    "pgsql",
    NULL,
    dbd_pgsql_native,
    dbd_pgsql_open,
    dbd_pgsql_check_conn,
    dbd_pgsql_close,
    dbd_pgsql_select_db,
    dbd_pgsql_start_transaction,
    dbd_pgsql_end_transaction,
    dbd_pgsql_query,
    dbd_pgsql_select,
    dbd_pgsql_num_cols,
    dbd_pgsql_num_tuples,
    dbd_pgsql_get_row,
    dbd_pgsql_get_entry,
    dbd_pgsql_error,
    dbd_pgsql_escape,
    dbd_pgsql_prepare,
    dbd_pgsql_pvquery,
    dbd_pgsql_pvselect,
    dbd_pgsql_pquery,
    dbd_pgsql_pselect,
    dbd_pgsql_get_name,
    dbd_pgsql_transaction_mode_get,
    dbd_pgsql_transaction_mode_set,
    "$%d",
    dbd_pgsql_pvbquery,
    dbd_pgsql_pvbselect,
    dbd_pgsql_pbquery,
    dbd_pgsql_pbselect,
    dbd_pgsql_datum_get
};
#endif
