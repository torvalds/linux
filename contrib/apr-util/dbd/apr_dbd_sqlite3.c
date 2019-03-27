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

#if APU_HAVE_SQLITE3

#include <ctype.h>
#include <stdlib.h>

#include <sqlite3.h>

#include "apr_strings.h"
#include "apr_time.h"
#include "apr_buckets.h"

#include "apr_dbd_internal.h"

#define MAX_RETRY_COUNT 15
#define MAX_RETRY_SLEEP 100000

struct apr_dbd_transaction_t {
    int mode;
    int errnum;
    apr_dbd_t *handle;
};

struct apr_dbd_t {
    sqlite3 *conn;
    apr_dbd_transaction_t *trans;
    apr_pool_t *pool;
    apr_dbd_prepared_t *prep;
};

typedef struct {
    char *name;
    char *value;
    int size;
    int type;
} apr_dbd_column_t;

struct apr_dbd_row_t {
    apr_dbd_results_t *res;
    apr_dbd_column_t **columns;
    apr_dbd_row_t *next_row;
    int columnCount;
    int rownum;
};

struct apr_dbd_results_t {
    int random;
    sqlite3 *handle;
    sqlite3_stmt *stmt;
    apr_dbd_row_t *next_row;
    size_t sz;
    int tuples;
    char **col_names;
    apr_pool_t *pool;
};

struct apr_dbd_prepared_t {
    sqlite3_stmt *stmt;
    apr_dbd_prepared_t *next;
    int nargs;
    int nvals;
    apr_dbd_type_e *types;
};

#define dbd_sqlite3_is_success(x) (((x) == SQLITE_DONE) || ((x) == SQLITE_OK))

static int dbd_sqlite3_select_internal(apr_pool_t *pool,
                                       apr_dbd_t *sql,
                                       apr_dbd_results_t **results,
                                       sqlite3_stmt *stmt, int seek)
{
    int ret, retry_count = 0, column_count;
    size_t i, num_tuples = 0;
    int increment = 0;
    apr_dbd_row_t *row = NULL;
    apr_dbd_row_t *lastrow = NULL;
    apr_dbd_column_t *column;
    char *hold = NULL;

    column_count = sqlite3_column_count(stmt);
    if (!*results) {
        *results = apr_pcalloc(pool, sizeof(apr_dbd_results_t));
    }
    (*results)->stmt = stmt;
    (*results)->sz = column_count;
    (*results)->random = seek;
    (*results)->next_row = 0;
    (*results)->tuples = 0;
    (*results)->col_names = apr_pcalloc(pool, column_count * sizeof(char *));
    (*results)->pool = pool;
    do {
        ret = sqlite3_step(stmt);
        if (ret == SQLITE_BUSY) {
            if (retry_count++ > MAX_RETRY_COUNT) {
                ret = SQLITE_ERROR;
            } else {
                apr_dbd_mutex_unlock();
                apr_sleep(MAX_RETRY_SLEEP);
                apr_dbd_mutex_lock();
            }
        } else if (ret == SQLITE_ROW) {
            int length;
            row = apr_palloc(pool, sizeof(apr_dbd_row_t));
            row->res = *results;
            increment = sizeof(apr_dbd_column_t *);
            length = increment * (*results)->sz;
            row->columns = apr_palloc(pool, length);
            row->columnCount = column_count;
            for (i = 0; i < (*results)->sz; i++) {
                column = apr_palloc(pool, sizeof(apr_dbd_column_t));
                row->columns[i] = column;
                /* copy column name once only */
                if ((*results)->col_names[i] == NULL) {
                    (*results)->col_names[i] =
                        apr_pstrdup(pool, sqlite3_column_name(stmt, i));
                }
                column->name = (*results)->col_names[i];
                column->size = sqlite3_column_bytes(stmt, i);
                column->type = sqlite3_column_type(stmt, i);
                column->value = NULL;
                switch (column->type) {
                case SQLITE_FLOAT:
                case SQLITE_INTEGER:
                case SQLITE_TEXT:
                    hold = (char *) sqlite3_column_text(stmt, i);
                    if (hold) {
                        column->value = apr_pstrmemdup(pool, hold,
                                                       column->size);
                    }
                    break;
                case SQLITE_BLOB:
                    hold = (char *) sqlite3_column_blob(stmt, i);
                    if (hold) {
                        column->value = apr_pstrmemdup(pool, hold,
                                                       column->size);
                    }
                    break;
                case SQLITE_NULL:
                    break;
                }
            }
            row->rownum = num_tuples++;
            row->next_row = 0;
            (*results)->tuples = num_tuples;
            if ((*results)->next_row == 0) {
                (*results)->next_row = row;
            }
            if (lastrow != 0) {
                lastrow->next_row = row;
            }
            lastrow = row;
        }
    } while (ret == SQLITE_ROW || ret == SQLITE_BUSY);

    if (dbd_sqlite3_is_success(ret)) {
        ret = 0;
    }
    return ret;
}

static int dbd_sqlite3_select(apr_pool_t *pool, apr_dbd_t *sql,
                              apr_dbd_results_t **results, const char *query,
                              int seek)
{
    sqlite3_stmt *stmt = NULL;
    const char *tail = NULL;
    int ret;

    if (sql->trans && sql->trans->errnum) {
        return sql->trans->errnum;
    }

    apr_dbd_mutex_lock();

    ret = sqlite3_prepare(sql->conn, query, strlen(query), &stmt, &tail);
    if (dbd_sqlite3_is_success(ret)) {
        ret = dbd_sqlite3_select_internal(pool, sql, results, stmt, seek);
    }
    sqlite3_finalize(stmt);

    apr_dbd_mutex_unlock();

    if (TXN_NOTICE_ERRORS(sql->trans)) {
        sql->trans->errnum = ret;
    }
    return ret;
}

static const char *dbd_sqlite3_get_name(const apr_dbd_results_t *res, int n)
{
    if ((n < 0) || ((size_t)n >= res->sz)) {
        return NULL;
    }

    return res->col_names[n];
}

static int dbd_sqlite3_get_row(apr_pool_t *pool, apr_dbd_results_t *res,
                               apr_dbd_row_t **rowp, int rownum)
{
    int i = 0;

    if (rownum == -1) {
        *rowp = res->next_row;
        if (*rowp == 0)
            return -1;
        res->next_row = (*rowp)->next_row;
        return 0;
    }
    if (rownum > res->tuples) {
        return -1;
    }
    rownum--;
    *rowp = res->next_row;
    for (; *rowp != 0; i++, *rowp = (*rowp)->next_row) {
        if (i == rownum) {
            return 0;
        }
    }

    return -1;

}

static const char *dbd_sqlite3_get_entry(const apr_dbd_row_t *row, int n)
{
    apr_dbd_column_t *column;
    const char *value;
    if ((n < 0) || (n >= row->columnCount)) {
        return NULL;
    }
    column = row->columns[n];
    value = column->value;
    return value;
}

static apr_status_t dbd_sqlite3_datum_get(const apr_dbd_row_t *row, int n,
                                          apr_dbd_type_e type, void *data)
{
    if ((n < 0) || ((size_t)n >= row->res->sz)) {
      return APR_EGENERAL;
    }

    if (row->columns[n]->type == SQLITE_NULL) {
        return APR_ENOENT;
    }

    switch (type) {
    case APR_DBD_TYPE_TINY:
        *(char*)data = atoi(row->columns[n]->value);
        break;
    case APR_DBD_TYPE_UTINY:
        *(unsigned char*)data = atoi(row->columns[n]->value);
        break;
    case APR_DBD_TYPE_SHORT:
        *(short*)data = atoi(row->columns[n]->value);
        break;
    case APR_DBD_TYPE_USHORT:
        *(unsigned short*)data = atoi(row->columns[n]->value);
        break;
    case APR_DBD_TYPE_INT:
        *(int*)data = atoi(row->columns[n]->value);
        break;
    case APR_DBD_TYPE_UINT:
        *(unsigned int*)data = atoi(row->columns[n]->value);
        break;
    case APR_DBD_TYPE_LONG:
        *(long*)data = atol(row->columns[n]->value);
        break;
    case APR_DBD_TYPE_ULONG:
        *(unsigned long*)data = atol(row->columns[n]->value);
        break;
    case APR_DBD_TYPE_LONGLONG:
        *(apr_int64_t*)data = apr_atoi64(row->columns[n]->value);
        break;
    case APR_DBD_TYPE_ULONGLONG:
        *(apr_uint64_t*)data = apr_atoi64(row->columns[n]->value);
        break;
    case APR_DBD_TYPE_FLOAT:
        *(float*)data = (float)atof(row->columns[n]->value);
        break;
    case APR_DBD_TYPE_DOUBLE:
        *(double*)data = atof(row->columns[n]->value);
        break;
    case APR_DBD_TYPE_STRING:
    case APR_DBD_TYPE_TEXT:
    case APR_DBD_TYPE_TIME:
    case APR_DBD_TYPE_DATE:
    case APR_DBD_TYPE_DATETIME:
    case APR_DBD_TYPE_TIMESTAMP:
    case APR_DBD_TYPE_ZTIMESTAMP:
        *(char**)data = row->columns[n]->value;
        break;
    case APR_DBD_TYPE_BLOB:
    case APR_DBD_TYPE_CLOB:
        {
        apr_bucket *e;
        apr_bucket_brigade *b = (apr_bucket_brigade*)data;

        e = apr_bucket_pool_create(row->columns[n]->value,
                                   row->columns[n]->size,
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

static const char *dbd_sqlite3_error(apr_dbd_t *sql, int n)
{
    return sqlite3_errmsg(sql->conn);
}

static int dbd_sqlite3_query_internal(apr_dbd_t *sql, sqlite3_stmt *stmt,
                                      int *nrows)
{
    int ret = -1, retry_count = 0;

    while(retry_count++ <= MAX_RETRY_COUNT) {
        ret = sqlite3_step(stmt);
        if (ret != SQLITE_BUSY)
            break;

        apr_dbd_mutex_unlock();
        apr_sleep(MAX_RETRY_SLEEP);
        apr_dbd_mutex_lock();
    }

    *nrows = sqlite3_changes(sql->conn);

    if (dbd_sqlite3_is_success(ret)) {
        ret = 0;
    }
    return ret;
}

static int dbd_sqlite3_query(apr_dbd_t *sql, int *nrows, const char *query)
{
    sqlite3_stmt *stmt = NULL;
    const char *tail = NULL;
    int ret = -1, length = 0;

    if (sql->trans && sql->trans->errnum) {
        return sql->trans->errnum;
    }

    length = strlen(query);
    apr_dbd_mutex_lock();

    do {
        ret = sqlite3_prepare(sql->conn, query, length, &stmt, &tail);
        if (ret != SQLITE_OK) {
            sqlite3_finalize(stmt);
            break;
        }

        ret = dbd_sqlite3_query_internal(sql, stmt, nrows);

        sqlite3_finalize(stmt);
        length -= (tail - query);
        query = tail;
    } while (length > 0);

    apr_dbd_mutex_unlock();

    if (TXN_NOTICE_ERRORS(sql->trans)) {
        sql->trans->errnum = ret;
    }
    return ret;
}

static apr_status_t free_mem(void *data)
{
    sqlite3_free(data);
    return APR_SUCCESS;
}

static const char *dbd_sqlite3_escape(apr_pool_t *pool, const char *arg,
                                      apr_dbd_t *sql)
{
    char *ret = sqlite3_mprintf("%q", arg);
    apr_pool_cleanup_register(pool, ret, free_mem,
                              apr_pool_cleanup_null);
    return ret;
}

static int dbd_sqlite3_prepare(apr_pool_t *pool, apr_dbd_t *sql,
                               const char *query, const char *label,
                               int nargs, int nvals, apr_dbd_type_e *types,
                               apr_dbd_prepared_t **statement)
{
    sqlite3_stmt *stmt;
    const char *tail = NULL;
    int ret;

    apr_dbd_mutex_lock();

    ret = sqlite3_prepare(sql->conn, query, strlen(query), &stmt, &tail);
    if (ret == SQLITE_OK) {
        apr_dbd_prepared_t *prep; 

        prep = apr_pcalloc(sql->pool, sizeof(*prep));
        prep->stmt = stmt;
        prep->next = sql->prep;
        prep->nargs = nargs;
        prep->nvals = nvals;
        prep->types = types;

        /* link new statement to the handle */
        sql->prep = prep;

        *statement = prep;
    } else {
        sqlite3_finalize(stmt);
    }
   
    apr_dbd_mutex_unlock();

    return ret;
}

static void dbd_sqlite3_bind(apr_dbd_prepared_t *statement, const char **values)
{
    sqlite3_stmt *stmt = statement->stmt;
    int i, j;

    for (i = 0, j = 0; i < statement->nargs; i++, j++) {
        if (values[j] == NULL) {
            sqlite3_bind_null(stmt, i + 1);
        }
        else {
            switch (statement->types[i]) {
            case APR_DBD_TYPE_BLOB:
            case APR_DBD_TYPE_CLOB:
                {
                char *data = (char *)values[j];
                int size = atoi((char*)values[++j]);

                /* skip table and column */
                j += 2;

                sqlite3_bind_blob(stmt, i + 1, data, size, SQLITE_STATIC);
                }
                break;
            default:
                sqlite3_bind_text(stmt, i + 1, values[j],
                                  strlen(values[j]), SQLITE_STATIC);
                break;
            }
        }
    }

    return;
}

static int dbd_sqlite3_pquery(apr_pool_t *pool, apr_dbd_t *sql,
                              int *nrows, apr_dbd_prepared_t *statement,
                              const char **values)
{
    sqlite3_stmt *stmt = statement->stmt;
    int ret = -1;

    if (sql->trans && sql->trans->errnum) {
        return sql->trans->errnum;
    }

    apr_dbd_mutex_lock();

    ret = sqlite3_reset(stmt);
    if (ret == SQLITE_OK) {
        dbd_sqlite3_bind(statement, values);

        ret = dbd_sqlite3_query_internal(sql, stmt, nrows);

        sqlite3_reset(stmt);
    }

    apr_dbd_mutex_unlock();

    if (TXN_NOTICE_ERRORS(sql->trans)) {
        sql->trans->errnum = ret;
    }
    return ret;
}

static int dbd_sqlite3_pvquery(apr_pool_t *pool, apr_dbd_t *sql, int *nrows,
                               apr_dbd_prepared_t *statement, va_list args)
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

    return dbd_sqlite3_pquery(pool, sql, nrows, statement, values);
}

static int dbd_sqlite3_pselect(apr_pool_t *pool, apr_dbd_t *sql,
                               apr_dbd_results_t **results,
                               apr_dbd_prepared_t *statement, int seek,
                               const char **values)
{
    sqlite3_stmt *stmt = statement->stmt;
    int ret;

    if (sql->trans && sql->trans->errnum) {
        return sql->trans->errnum;
    }

    apr_dbd_mutex_lock();

    ret = sqlite3_reset(stmt);
    if (ret == SQLITE_OK) {
        dbd_sqlite3_bind(statement, values);

        ret = dbd_sqlite3_select_internal(pool, sql, results, stmt, seek);

        sqlite3_reset(stmt);
    }

    apr_dbd_mutex_unlock();

    if (TXN_NOTICE_ERRORS(sql->trans)) {
        sql->trans->errnum = ret;
    }
    return ret;
}

static int dbd_sqlite3_pvselect(apr_pool_t *pool, apr_dbd_t *sql,
                                apr_dbd_results_t **results,
                                apr_dbd_prepared_t *statement, int seek,
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

    return dbd_sqlite3_pselect(pool, sql, results, statement, seek, values);
}

static void dbd_sqlite3_bbind(apr_dbd_prepared_t * statement,
                              const void **values)
{
    sqlite3_stmt *stmt = statement->stmt;
    int i, j;
    apr_dbd_type_e type;

    for (i = 0, j = 0; i < statement->nargs; i++, j++) {
        type = (values[j] == NULL ? APR_DBD_TYPE_NULL : statement->types[i]);

        switch (type) {
        case APR_DBD_TYPE_TINY:
            sqlite3_bind_int(stmt, i + 1, *(char*)values[j]);
            break;
        case APR_DBD_TYPE_UTINY:
            sqlite3_bind_int(stmt, i + 1, *(unsigned char*)values[j]);
            break;
        case APR_DBD_TYPE_SHORT:
            sqlite3_bind_int(stmt, i + 1, *(short*)values[j]);
            break;
        case APR_DBD_TYPE_USHORT:
            sqlite3_bind_int(stmt, i + 1, *(unsigned short*)values[j]);
            break;
        case APR_DBD_TYPE_INT:
            sqlite3_bind_int(stmt, i + 1, *(int*)values[j]);
            break;
        case APR_DBD_TYPE_UINT:
            sqlite3_bind_int(stmt, i + 1, *(unsigned int*)values[j]);
            break;
        case APR_DBD_TYPE_LONG:
            sqlite3_bind_int64(stmt, i + 1, *(long*)values[j]);
            break;
        case APR_DBD_TYPE_ULONG:
            sqlite3_bind_int64(stmt, i + 1, *(unsigned long*)values[j]);
            break;
        case APR_DBD_TYPE_LONGLONG:
            sqlite3_bind_int64(stmt, i + 1, *(apr_int64_t*)values[j]);
            break;
        case APR_DBD_TYPE_ULONGLONG:
            sqlite3_bind_int64(stmt, i + 1, *(apr_uint64_t*)values[j]);
            break;
        case APR_DBD_TYPE_FLOAT:
            sqlite3_bind_double(stmt, i + 1, *(float*)values[j]);
            break;
        case APR_DBD_TYPE_DOUBLE:
            sqlite3_bind_double(stmt, i + 1, *(double*)values[j]);
            break;
        case APR_DBD_TYPE_STRING:
        case APR_DBD_TYPE_TEXT:
        case APR_DBD_TYPE_TIME:
        case APR_DBD_TYPE_DATE:
        case APR_DBD_TYPE_DATETIME:
        case APR_DBD_TYPE_TIMESTAMP:
        case APR_DBD_TYPE_ZTIMESTAMP:
            sqlite3_bind_text(stmt, i + 1, values[j], strlen(values[j]),
                              SQLITE_STATIC);
            break;
        case APR_DBD_TYPE_BLOB:
        case APR_DBD_TYPE_CLOB:
            {
            char *data = (char*)values[j];
            apr_size_t size = *(apr_size_t*)values[++j];

            sqlite3_bind_blob(stmt, i + 1, data, size, SQLITE_STATIC);

            /* skip table and column */
            j += 2;
            }
            break;
        case APR_DBD_TYPE_NULL:
        default:
            sqlite3_bind_null(stmt, i + 1);
            break;
        }
    }

    return;
}

static int dbd_sqlite3_pbquery(apr_pool_t * pool, apr_dbd_t * sql,
                               int *nrows, apr_dbd_prepared_t * statement,
                               const void **values)
{
    sqlite3_stmt *stmt = statement->stmt;
    int ret = -1;

    if (sql->trans && sql->trans->errnum) {
        return sql->trans->errnum;
    }

    apr_dbd_mutex_lock();

    ret = sqlite3_reset(stmt);
    if (ret == SQLITE_OK) {
        dbd_sqlite3_bbind(statement, values);

        ret = dbd_sqlite3_query_internal(sql, stmt, nrows);

        sqlite3_reset(stmt);
    }

    apr_dbd_mutex_unlock();

    if (TXN_NOTICE_ERRORS(sql->trans)) {
        sql->trans->errnum = ret;
    }
    return ret;
}

static int dbd_sqlite3_pvbquery(apr_pool_t * pool, apr_dbd_t * sql,
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

    return dbd_sqlite3_pbquery(pool, sql, nrows, statement, values);
}

static int dbd_sqlite3_pbselect(apr_pool_t * pool, apr_dbd_t * sql,
                                apr_dbd_results_t ** results,
                                apr_dbd_prepared_t * statement,
                                int seek, const void **values)
{
    sqlite3_stmt *stmt = statement->stmt;
    int ret;

    if (sql->trans && sql->trans->errnum) {
        return sql->trans->errnum;
    }

    apr_dbd_mutex_lock();

    ret = sqlite3_reset(stmt);
    if (ret == SQLITE_OK) {
        dbd_sqlite3_bbind(statement, values);

        ret = dbd_sqlite3_select_internal(pool, sql, results, stmt, seek);

        sqlite3_reset(stmt);
    }

    apr_dbd_mutex_unlock();

    if (TXN_NOTICE_ERRORS(sql->trans)) {
        sql->trans->errnum = ret;
    }
    return ret;
}

static int dbd_sqlite3_pvbselect(apr_pool_t * pool, apr_dbd_t * sql,
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

    return dbd_sqlite3_pbselect(pool, sql, results, statement, seek, values);
}

static int dbd_sqlite3_start_transaction(apr_pool_t *pool,
                                         apr_dbd_t *handle,
                                         apr_dbd_transaction_t **trans)
{
    int ret = 0;
    int nrows = 0;

    ret = dbd_sqlite3_query(handle, &nrows, "BEGIN IMMEDIATE");
    if (!*trans) {
        *trans = apr_pcalloc(pool, sizeof(apr_dbd_transaction_t));
        (*trans)->handle = handle;
        handle->trans = *trans;
    }

    return ret;
}

static int dbd_sqlite3_end_transaction(apr_dbd_transaction_t *trans)
{
    int ret = -1; /* ending transaction that was never started is an error */
    int nrows = 0;

    if (trans) {
        /* rollback on error or explicit rollback request */
        if (trans->errnum || TXN_DO_ROLLBACK(trans)) {
            trans->errnum = 0;
            ret = dbd_sqlite3_query(trans->handle, &nrows, "ROLLBACK");
        } else {
            ret = dbd_sqlite3_query(trans->handle, &nrows, "COMMIT");
        }
        trans->handle->trans = NULL;
    }

    return ret;
}

static int dbd_sqlite3_transaction_mode_get(apr_dbd_transaction_t *trans)
{
    if (!trans)
        return APR_DBD_TRANSACTION_COMMIT;

    return trans->mode;
}

static int dbd_sqlite3_transaction_mode_set(apr_dbd_transaction_t *trans,
                                            int mode)
{
    if (!trans)
        return APR_DBD_TRANSACTION_COMMIT;

    return trans->mode = (mode & TXN_MODE_BITS);
}

static apr_dbd_t *dbd_sqlite3_open(apr_pool_t *pool, const char *params,
                                   const char **error)
{
    apr_dbd_t *sql = NULL;
    sqlite3 *conn = NULL;
    int sqlres;
    if (!params)
        return NULL;
    sqlres = sqlite3_open(params, &conn);
    if (sqlres != SQLITE_OK) {
        if (error) {
            *error = apr_pstrdup(pool, sqlite3_errmsg(conn));
        }
        sqlite3_close(conn);
        return NULL;
    }
    /* should we register rand or power functions to the sqlite VM? */
    sql = apr_pcalloc(pool, sizeof(*sql));
    sql->conn = conn;
    sql->pool = pool;
    sql->trans = NULL;

    return sql;
}

static apr_status_t dbd_sqlite3_close(apr_dbd_t *handle)
{
    apr_dbd_prepared_t *prep = handle->prep;

    /* finalize all prepared statements, or we'll get SQLITE_BUSY on close */
    while (prep) {
        sqlite3_finalize(prep->stmt);
        prep = prep->next;
    }

    sqlite3_close(handle->conn);
    return APR_SUCCESS;
}

static apr_status_t dbd_sqlite3_check_conn(apr_pool_t *pool,
                                           apr_dbd_t *handle)
{
    return (handle->conn != NULL) ? APR_SUCCESS : APR_EGENERAL;
}

static int dbd_sqlite3_select_db(apr_pool_t *pool, apr_dbd_t *handle,
                                 const char *name)
{
    return APR_ENOTIMPL;
}

static void *dbd_sqlite3_native(apr_dbd_t *handle)
{
    return handle->conn;
}

static int dbd_sqlite3_num_cols(apr_dbd_results_t *res)
{
    return res->sz;
}

static int dbd_sqlite3_num_tuples(apr_dbd_results_t *res)
{
    return res->tuples;
}

APU_MODULE_DECLARE_DATA const apr_dbd_driver_t apr_dbd_sqlite3_driver = {
    "sqlite3",
    NULL,
    dbd_sqlite3_native,
    dbd_sqlite3_open,
    dbd_sqlite3_check_conn,
    dbd_sqlite3_close,
    dbd_sqlite3_select_db,
    dbd_sqlite3_start_transaction,
    dbd_sqlite3_end_transaction,
    dbd_sqlite3_query,
    dbd_sqlite3_select,
    dbd_sqlite3_num_cols,
    dbd_sqlite3_num_tuples,
    dbd_sqlite3_get_row,
    dbd_sqlite3_get_entry,
    dbd_sqlite3_error,
    dbd_sqlite3_escape,
    dbd_sqlite3_prepare,
    dbd_sqlite3_pvquery,
    dbd_sqlite3_pvselect,
    dbd_sqlite3_pquery,
    dbd_sqlite3_pselect,
    dbd_sqlite3_get_name,
    dbd_sqlite3_transaction_mode_get,
    dbd_sqlite3_transaction_mode_set,
    "?",
    dbd_sqlite3_pvbquery,
    dbd_sqlite3_pvbselect,
    dbd_sqlite3_pbquery,
    dbd_sqlite3_pbselect,
    dbd_sqlite3_datum_get
};
#endif
