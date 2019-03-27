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

#if APU_HAVE_MYSQL

#include "apu_version.h"
#include "apu_config.h"

#include <ctype.h>
#include <stdlib.h>

#if defined(HAVE_MYSQL_MYSQL_H)
#if defined(HAVE_MYSQL_MY_GLOBAL_H)
#include <mysql/my_global.h>
#if defined(HAVE_MYSQL_MY_SYS_H)
#include <mysql/my_sys.h>
#endif
#endif
#include <mysql/mysql.h>
#include <mysql/errmsg.h>
#else /* !defined(HAVE_MYSQL_MYSQL_H) */
#if defined(HAVE_MY_GLOBAL_H) 
#include <my_global.h>
#if defined(HAVE_MY_SYS_H)
#include <my_sys.h>
#endif
#endif
#include <mysql.h>
#include <errmsg.h>
#endif

#include "apr_strings.h"
#include "apr_lib.h"
#include "apr_buckets.h"

#include "apr_dbd_internal.h"

/* default maximum field size 1 MB */
#define FIELDSIZE 1048575

struct apr_dbd_prepared_t {
    MYSQL_STMT* stmt;
    int nargs;
    int nvals;
    apr_dbd_type_e *types;
};

struct apr_dbd_transaction_t {
    int mode;
    int errnum;
    apr_dbd_t *handle;
};

struct apr_dbd_t {
    MYSQL* conn ;
    apr_dbd_transaction_t* trans ;
    unsigned long fldsz;
};

struct apr_dbd_results_t {
    int random;
    MYSQL_RES *res;
    MYSQL_STMT *statement;
    MYSQL_BIND *bind;
    apr_pool_t *pool;
};
struct apr_dbd_row_t {
    MYSQL_ROW row;
    apr_dbd_results_t *res;
    unsigned long *len;
};

/* MySQL specific bucket for BLOB types */
typedef struct apr_bucket_lob apr_bucket_lob;
/**
 * A bucket referring to a MySQL BLOB
 */
struct apr_bucket_lob {
    /** Number of buckets using this memory */
    apr_bucket_refcount  refcount;
    /** The row this bucket refers to */
    const apr_dbd_row_t *row;
    /** The column this bucket refers to */
    int col;
    /** The pool into which any needed structures should
     *  be created while reading from this bucket */
    apr_pool_t *readpool;
};

static void lob_bucket_destroy(void *data);
static apr_status_t lob_bucket_read(apr_bucket *e, const char **str,
                                    apr_size_t *len, apr_read_type_e block);
static apr_bucket *apr_bucket_lob_make(apr_bucket *b,
                                       const apr_dbd_row_t *row, int col,
                                       apr_off_t offset, apr_size_t len,
                                       apr_pool_t *p);
static apr_bucket *apr_bucket_lob_create(const apr_dbd_row_t *row, int col,
                                         apr_off_t offset,
                                         apr_size_t len, apr_pool_t *p,
                                         apr_bucket_alloc_t *list);
static int dbd_mysql_num_cols(apr_dbd_results_t *res);

static const apr_bucket_type_t apr_bucket_type_lob = {
    "LOB", 5, APR_BUCKET_DATA,
    lob_bucket_destroy,
    lob_bucket_read,
    apr_bucket_setaside_notimpl,
    apr_bucket_shared_split,
    apr_bucket_shared_copy
};

static void lob_bucket_destroy(void *data)
{
    apr_bucket_lob *f = data;

    if (apr_bucket_shared_destroy(f)) {
        /* no need to destroy database objects here; it will get
         * done automatically when the pool gets cleaned up */
        apr_bucket_free(f);
    }
}

static apr_status_t lob_bucket_read(apr_bucket *e, const char **str,
                                    apr_size_t *len, apr_read_type_e block)
{
    apr_bucket_lob *a = e->data;
    const apr_dbd_row_t *row = a->row;
    apr_dbd_results_t *res = row->res;
    int col = a->col;
    apr_bucket *b = NULL;
    int rv;
    apr_size_t blength = e->length;  /* bytes remaining in file past offset */
    apr_off_t boffset = e->start;
    MYSQL_BIND *bind = &res->bind[col];

    *str = NULL;  /* in case we die prematurely */

    /* fetch from offset if not at the beginning */
    if (boffset > 0) {
        rv = mysql_stmt_fetch_column(res->statement, bind, col,
                                     (unsigned long) boffset);
        if (rv != 0) {
            return APR_EGENERAL;
        }
    }
    blength -= blength > bind->buffer_length ? bind->buffer_length : blength;
    *len = e->length - blength;
    *str = bind->buffer;

    /* allocate new buffer, since we used this one for the bucket */
    bind->buffer = apr_palloc(res->pool, bind->buffer_length);

    /*
     * Change the current bucket to refer to what we read,
     * even if we read nothing because we hit EOF.
     */
    apr_bucket_pool_make(e, *str, *len, res->pool);

    /* If we have more to read from the field, then create another bucket */
    if (blength > 0) {
        /* for efficiency, we can just build a new apr_bucket struct
         * to wrap around the existing LOB bucket */
        b = apr_bucket_alloc(sizeof(*b), e->list);
        b->start  = boffset + *len;
        b->length = blength;
        b->data   = a;
        b->type   = &apr_bucket_type_lob;
        b->free   = apr_bucket_free;
        b->list   = e->list;
        APR_BUCKET_INSERT_AFTER(e, b);
    }
    else {
        lob_bucket_destroy(a);
    }

    return APR_SUCCESS;
}

static apr_bucket *apr_bucket_lob_make(apr_bucket *b,
                                       const apr_dbd_row_t *row, int col,
                                       apr_off_t offset, apr_size_t len,
                                       apr_pool_t *p)
{
    apr_bucket_lob *f;

    f = apr_bucket_alloc(sizeof(*f), b->list);
    f->row = row;
    f->col = col;
    f->readpool = p;

    b = apr_bucket_shared_make(b, f, offset, len);
    b->type = &apr_bucket_type_lob;

    return b;
}

static apr_bucket *apr_bucket_lob_create(const apr_dbd_row_t *row, int col,
                                         apr_off_t offset,
                                         apr_size_t len, apr_pool_t *p,
                                         apr_bucket_alloc_t *list)
{
    apr_bucket *b = apr_bucket_alloc(sizeof(*b), list);

    APR_BUCKET_INIT(b);
    b->free = apr_bucket_free;
    b->list = list;
    return apr_bucket_lob_make(b, row, col, offset, len, p);
}

static apr_status_t free_result(void *data)
{
    mysql_free_result(data);
    return APR_SUCCESS;
}

static int dbd_mysql_select(apr_pool_t *pool, apr_dbd_t *sql,
                            apr_dbd_results_t **results,
                            const char *query, int seek)
{
    int sz;
    int ret;
    if (sql->trans && sql->trans->errnum) {
        return sql->trans->errnum;
    }
    ret = mysql_query(sql->conn, query);
    if (!ret) {
        if (sz = mysql_field_count(sql->conn), sz > 0) {
            if (!*results) {
                *results = apr_palloc(pool, sizeof(apr_dbd_results_t));
            }
            (*results)->random = seek;
            (*results)->statement = NULL;
            (*results)->pool = pool;
            if (seek) {
                (*results)->res = mysql_store_result(sql->conn);
            }
            else {
                (*results)->res = mysql_use_result(sql->conn);
            }
            apr_pool_cleanup_register(pool, (*results)->res,
                                      free_result,apr_pool_cleanup_null);
        }
    } else {
        ret = mysql_errno(sql->conn);
    }

    if (TXN_NOTICE_ERRORS(sql->trans)) {
        sql->trans->errnum = ret;
    }
    return ret;
}

static const char *dbd_mysql_get_name(const apr_dbd_results_t *res, int n)
{
    if ((n < 0) || (n >= (int) mysql_num_fields(res->res))) {
        return NULL;
    }

    return mysql_fetch_fields(res->res)[n].name;
}

static int dbd_mysql_get_row(apr_pool_t *pool, apr_dbd_results_t *res,
                             apr_dbd_row_t **row, int rownum)
{
    MYSQL_ROW r = NULL;
    int ret = 0;

    if (res->statement) {
        if (res->random) {
            if (rownum > 0) {
                mysql_stmt_data_seek(res->statement, (my_ulonglong) --rownum);
            }
            else {
                return -1; /* invalid row */
            }
        }
        ret = mysql_stmt_fetch(res->statement);
        switch (ret) {
        case 1:
            ret = mysql_stmt_errno(res->statement);
            break;
        case MYSQL_NO_DATA:
            ret = -1;
            break;
        default:
            ret = 0; /* bad luck - get_entry will deal with this */
            break;
        }
    }
    else {
        if (res->random) {
            if (rownum > 0) {
                mysql_data_seek(res->res, (my_ulonglong) --rownum);
            }
            else {
                return -1; /* invalid row */
            }
        }
        r = mysql_fetch_row(res->res);
        if (r == NULL) {
            ret = -1;
        }
    }
    if (ret == 0) {
        if (!*row) {
            *row = apr_palloc(pool, sizeof(apr_dbd_row_t));
        }
        (*row)->row = r;
        (*row)->res = res;
        (*row)->len = mysql_fetch_lengths(res->res);
    }
    else {
        apr_pool_cleanup_run(res->pool, res->res, free_result);
    }
    return ret;
}
#if 0
/* An improved API that was proposed but not followed up */
static int dbd_mysql_get_entry(const apr_dbd_row_t *row, int n,
                               apr_dbd_datum_t *val)
{
    MYSQL_BIND *bind;
    if (dbd_mysql_num_cols(row->res) <= n) {
    	return NULL;
    }
    if (row->res->statement) {
        bind = &row->res->bind[n];
        if (mysql_stmt_fetch_column(row->res->statement, bind, n, 0) != 0) {
            val->type = APR_DBD_VALUE_NULL;
            return -1;
        }
        if (*bind->is_null) {
            val->type = APR_DBD_VALUE_NULL;
            return -1;
        }
        else {
            val->type = APR_DBD_VALUE_STRING;
            val->value.stringval = bind->buffer;
        }
    }
    else {
        val->type = APR_DBD_VALUE_STRING;
        val->value.stringval = row->row[n];
    }
    return 0;
}
#else

static const char *dbd_mysql_get_entry(const apr_dbd_row_t *row, int n)
{
    MYSQL_BIND *bind;
    if (dbd_mysql_num_cols(row->res) <= n) {
    	return NULL;
    }
    if (row->res->statement) {
        bind = &row->res->bind[n];
        if (mysql_stmt_fetch_column(row->res->statement, bind, n, 0) != 0) {
            return NULL;
        }
        if (*bind->is_null) {
            return NULL;
        }
        else {
            return bind->buffer;
        }
    }
    else {
        return row->row[n];
    }
    return NULL;
}
#endif

static apr_status_t dbd_mysql_datum_get(const apr_dbd_row_t *row, int n,
                                        apr_dbd_type_e type, void *data)
{
    if (row->res->statement) {
        MYSQL_BIND *bind = &row->res->bind[n];
        unsigned long len = *bind->length;

        if (mysql_stmt_fetch_column(row->res->statement, bind, n, 0) != 0) {
            return APR_EGENERAL;
        }

        if (*bind->is_null) {
            return APR_ENOENT;
        }

        switch (type) {
        case APR_DBD_TYPE_TINY:
            *(char*)data = atoi(bind->buffer);
            break;
        case APR_DBD_TYPE_UTINY:
            *(unsigned char*)data = atoi(bind->buffer);
            break;
        case APR_DBD_TYPE_SHORT:
            *(short*)data = atoi(bind->buffer);
            break;
        case APR_DBD_TYPE_USHORT:
            *(unsigned short*)data = atoi(bind->buffer);
            break;
        case APR_DBD_TYPE_INT:
            *(int*)data = atoi(bind->buffer);
            break;
        case APR_DBD_TYPE_UINT:
            *(unsigned int*)data = atoi(bind->buffer);
            break;
        case APR_DBD_TYPE_LONG:
            *(long*)data = atol(bind->buffer);
            break;
        case APR_DBD_TYPE_ULONG:
            *(unsigned long*)data = atol(bind->buffer);
            break;
        case APR_DBD_TYPE_LONGLONG:
            *(apr_int64_t*)data = apr_atoi64(bind->buffer);
            break;
        case APR_DBD_TYPE_ULONGLONG:
            *(apr_uint64_t*)data = apr_atoi64(bind->buffer);
            break;
        case APR_DBD_TYPE_FLOAT:
            *(float*)data = (float) atof(bind->buffer);
            break;
        case APR_DBD_TYPE_DOUBLE:
            *(double*)data = atof(bind->buffer);
            break;
        case APR_DBD_TYPE_STRING:
        case APR_DBD_TYPE_TEXT:
        case APR_DBD_TYPE_TIME:
        case APR_DBD_TYPE_DATE:
        case APR_DBD_TYPE_DATETIME:
        case APR_DBD_TYPE_TIMESTAMP:
        case APR_DBD_TYPE_ZTIMESTAMP:
            *((char*)bind->buffer+bind->buffer_length-1) = '\0';
            *(char**)data = bind->buffer;
            break;
        case APR_DBD_TYPE_BLOB:
        case APR_DBD_TYPE_CLOB:
            {
            apr_bucket *e;
            apr_bucket_brigade *b = (apr_bucket_brigade*)data;

            e = apr_bucket_lob_create(row, n, 0, len,
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
    }
    else {
        if (row->row[n] == NULL) {
            return APR_ENOENT;
        }

        switch (type) {
        case APR_DBD_TYPE_TINY:
            *(char*)data = atoi(row->row[n]);
            break;
        case APR_DBD_TYPE_UTINY:
            *(unsigned char*)data = atoi(row->row[n]);
            break;
        case APR_DBD_TYPE_SHORT:
            *(short*)data = atoi(row->row[n]);
            break;
        case APR_DBD_TYPE_USHORT:
            *(unsigned short*)data = atoi(row->row[n]);
            break;
        case APR_DBD_TYPE_INT:
            *(int*)data = atoi(row->row[n]);
            break;
        case APR_DBD_TYPE_UINT:
            *(unsigned int*)data = atoi(row->row[n]);
            break;
        case APR_DBD_TYPE_LONG:
            *(long*)data = atol(row->row[n]);
            break;
        case APR_DBD_TYPE_ULONG:
            *(unsigned long*)data = atol(row->row[n]);
            break;
        case APR_DBD_TYPE_LONGLONG:
            *(apr_int64_t*)data = apr_atoi64(row->row[n]);
            break;
        case APR_DBD_TYPE_ULONGLONG:
            *(apr_uint64_t*)data = apr_atoi64(row->row[n]);
            break;
        case APR_DBD_TYPE_FLOAT:
            *(float*)data = (float) atof(row->row[n]);
            break;
        case APR_DBD_TYPE_DOUBLE:
            *(double*)data = atof(row->row[n]);
            break;
        case APR_DBD_TYPE_STRING:
        case APR_DBD_TYPE_TEXT:
        case APR_DBD_TYPE_TIME:
        case APR_DBD_TYPE_DATE:
        case APR_DBD_TYPE_DATETIME:
        case APR_DBD_TYPE_TIMESTAMP:
        case APR_DBD_TYPE_ZTIMESTAMP:
            *(char**)data = row->row[n];
            break;
        case APR_DBD_TYPE_BLOB:
        case APR_DBD_TYPE_CLOB:
            {
            apr_bucket *e;
            apr_bucket_brigade *b = (apr_bucket_brigade*)data;

            e = apr_bucket_pool_create(row->row[n], row->len[n],
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
    }
    return 0;
}

static const char *dbd_mysql_error(apr_dbd_t *sql, int n)
{
    return mysql_error(sql->conn);
}

static int dbd_mysql_query(apr_dbd_t *sql, int *nrows, const char *query)
{
    int ret;
    if (sql->trans && sql->trans->errnum) {
        return sql->trans->errnum;
    }
    ret = mysql_query(sql->conn, query);
    if (ret != 0) {
        ret = mysql_errno(sql->conn);
    }
    *nrows = (int) mysql_affected_rows(sql->conn);
    if (TXN_NOTICE_ERRORS(sql->trans)) {
        sql->trans->errnum = ret;
    }
    return ret;
}

static const char *dbd_mysql_escape(apr_pool_t *pool, const char *arg,
                                    apr_dbd_t *sql)
{
    unsigned long len = strlen(arg);
    char *ret = apr_palloc(pool, 2*len + 1);
    mysql_real_escape_string(sql->conn, ret, arg, len);
    return ret;
}

static apr_status_t stmt_close(void *data)
{
    mysql_stmt_close(data);
    return APR_SUCCESS;
}

static int dbd_mysql_prepare(apr_pool_t *pool, apr_dbd_t *sql,
                             const char *query, const char *label,
                             int nargs, int nvals, apr_dbd_type_e *types,
                             apr_dbd_prepared_t **statement)
{
    /* Translate from apr_dbd to native query format */
    int ret;

    if (!*statement) {
        *statement = apr_palloc(pool, sizeof(apr_dbd_prepared_t));
    }
    (*statement)->stmt = mysql_stmt_init(sql->conn);

    if ((*statement)->stmt) {
        apr_pool_cleanup_register(pool, (*statement)->stmt,
                                  stmt_close, apr_pool_cleanup_null);
        ret = mysql_stmt_prepare((*statement)->stmt, query, strlen(query));

        if (ret != 0) {
            ret = mysql_stmt_errno((*statement)->stmt);
        }

        (*statement)->nargs = nargs;
        (*statement)->nvals = nvals;
        (*statement)->types = types;

        return ret;
    }

    return CR_OUT_OF_MEMORY;
}

static void dbd_mysql_bind(apr_dbd_prepared_t *statement,
                           const char **values, MYSQL_BIND *bind)
{
    int i, j;

    for (i = 0, j = 0; i < statement->nargs; i++, j++) {
        bind[i].length = &bind[i].buffer_length;
        bind[i].is_unsigned = 0;
        bind[i].is_null = NULL;

        if (values[j] == NULL) {
            bind[i].buffer_type = MYSQL_TYPE_NULL;
        }
        else {
            switch (statement->types[i]) {
            case APR_DBD_TYPE_BLOB:
            case APR_DBD_TYPE_CLOB:
                bind[i].buffer_type = MYSQL_TYPE_LONG_BLOB;
                bind[i].buffer = (void*)values[j];
                bind[i].buffer_length = atol(values[++j]);

                /* skip table and column */
                j += 2;
                break;
            default:
                bind[i].buffer_type = MYSQL_TYPE_VAR_STRING;
                bind[i].buffer = (void*)values[j];
                bind[i].buffer_length = strlen(values[j]);
                break;
            }
        }
    }

    return;
}

static int dbd_mysql_pquery_internal(apr_pool_t *pool, apr_dbd_t *sql,
                                     int *nrows, apr_dbd_prepared_t *statement,
                                     MYSQL_BIND *bind)
{
    int ret;

    ret = mysql_stmt_bind_param(statement->stmt, bind);
    if (ret != 0) {
        *nrows = 0;
        ret = mysql_stmt_errno(statement->stmt);
    }
    else {
        ret = mysql_stmt_execute(statement->stmt);
        if (ret != 0) {
            ret = mysql_stmt_errno(statement->stmt);
        }
        *nrows = (int) mysql_stmt_affected_rows(statement->stmt);
    }

    return ret;
}

static int dbd_mysql_pquery(apr_pool_t *pool, apr_dbd_t *sql,
                            int *nrows, apr_dbd_prepared_t *statement,
                            const char **values)
{
    MYSQL_BIND *bind;
    int ret;

    if (sql->trans && sql->trans->errnum) {
        return sql->trans->errnum;
    }

    bind = apr_palloc(pool, statement->nargs * sizeof(MYSQL_BIND));

    dbd_mysql_bind(statement, values, bind);

    ret = dbd_mysql_pquery_internal(pool, sql, nrows, statement, bind);

    if (TXN_NOTICE_ERRORS(sql->trans)) {
        sql->trans->errnum = ret;
    }
    return ret;
}

static int dbd_mysql_pvquery(apr_pool_t *pool, apr_dbd_t *sql, int *nrows,
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

    return dbd_mysql_pquery(pool, sql, nrows, statement, values);
}

static int dbd_mysql_pselect_internal(apr_pool_t *pool, apr_dbd_t *sql,
                                      apr_dbd_results_t **res,
                                      apr_dbd_prepared_t *statement,
                                      int random, MYSQL_BIND *bind)
{
    int nfields, i;
    my_bool *is_nullr;
#if MYSQL_VERSION_ID >= 50000
    my_bool *error;
#endif
    int ret;
    unsigned long *length, maxlen;

    ret = mysql_stmt_bind_param(statement->stmt, bind);
    if (ret == 0) {
        ret = mysql_stmt_execute(statement->stmt);
        if (!ret) {
            if (!*res) {
                *res = apr_pcalloc(pool, sizeof(apr_dbd_results_t));
            }
            (*res)->random = random;
            (*res)->statement = statement->stmt;
            (*res)->res = mysql_stmt_result_metadata(statement->stmt);
            (*res)->pool = pool;
            apr_pool_cleanup_register(pool, (*res)->res,
                                      free_result, apr_pool_cleanup_null);
            nfields = mysql_num_fields((*res)->res);
            if (!(*res)->bind) {
                (*res)->bind = apr_palloc(pool, nfields*sizeof(MYSQL_BIND));
                length = apr_pcalloc(pool, nfields*sizeof(unsigned long));
#if MYSQL_VERSION_ID >= 50000
                error = apr_palloc(pool, nfields*sizeof(my_bool));
#endif
                is_nullr = apr_pcalloc(pool, nfields*sizeof(my_bool));
                for ( i = 0; i < nfields; ++i ) {
                    maxlen = ((*res)->res->fields[i].length < sql->fldsz ?
                              (*res)->res->fields[i].length : sql->fldsz) + 1;
                    if ((*res)->res->fields[i].type == MYSQL_TYPE_BLOB) {
                        (*res)->bind[i].buffer_type = MYSQL_TYPE_LONG_BLOB;
                    }
                    else {
                        (*res)->bind[i].buffer_type = MYSQL_TYPE_VAR_STRING;
                    }
                    (*res)->bind[i].buffer_length = maxlen;
                    (*res)->bind[i].length = &length[i];
                    (*res)->bind[i].buffer = apr_palloc(pool, maxlen);
                    (*res)->bind[i].is_null = is_nullr+i;
#if MYSQL_VERSION_ID >= 50000
                    (*res)->bind[i].error = error+i;
#endif
                }
            }
            ret = mysql_stmt_bind_result(statement->stmt, (*res)->bind);
            if (!ret) {
                ret = mysql_stmt_store_result(statement->stmt);
            }
        }
    }
    if (ret != 0) {
        ret = mysql_stmt_errno(statement->stmt);
    }

    return ret;
}

static int dbd_mysql_pselect(apr_pool_t *pool, apr_dbd_t *sql,
                             apr_dbd_results_t **res,
                             apr_dbd_prepared_t *statement, int random,
                             const char **args)
{
    int ret;
    MYSQL_BIND *bind;

    if (sql->trans && sql->trans->errnum) {
        return sql->trans->errnum;
    }

    bind = apr_palloc(pool, statement->nargs * sizeof(MYSQL_BIND));

    dbd_mysql_bind(statement, args, bind);

    ret = dbd_mysql_pselect_internal(pool, sql,  res, statement, random, bind);

    if (TXN_NOTICE_ERRORS(sql->trans)) {
        sql->trans->errnum = ret;
    }
    return ret;
}

static int dbd_mysql_pvselect(apr_pool_t *pool, apr_dbd_t *sql,
                              apr_dbd_results_t **res,
                              apr_dbd_prepared_t *statement, int random,
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

    return dbd_mysql_pselect(pool, sql, res, statement, random, values);
}

static void dbd_mysql_bbind(apr_pool_t *pool, apr_dbd_prepared_t *statement,
                            const void **values, MYSQL_BIND *bind)
{
    void *arg;
    int i, j;
    apr_dbd_type_e type;

    for (i = 0, j = 0; i < statement->nargs; i++, j++) {
        arg = (void *)values[j];

        bind[i].length = &bind[i].buffer_length;
        bind[i].is_null = NULL;

        type = (arg == NULL ? APR_DBD_TYPE_NULL : statement->types[i]);
        switch (type) {
        case APR_DBD_TYPE_TINY:
            bind[i].buffer = arg;
            bind[i].buffer_type = MYSQL_TYPE_TINY;
            bind[i].is_unsigned = 0;
            break;
        case APR_DBD_TYPE_UTINY:
            bind[i].buffer = arg;
            bind[i].buffer_type = MYSQL_TYPE_TINY;
            bind[i].is_unsigned = 1;
            break;
        case APR_DBD_TYPE_SHORT:
            bind[i].buffer = arg;
            bind[i].buffer_type = MYSQL_TYPE_SHORT;
            bind[i].is_unsigned = 0;
            break;
        case APR_DBD_TYPE_USHORT:
            bind[i].buffer = arg;
            bind[i].buffer_type = MYSQL_TYPE_SHORT;
            bind[i].is_unsigned = 1;
            break;
        case APR_DBD_TYPE_INT:
            bind[i].buffer = arg;
            bind[i].buffer_type = MYSQL_TYPE_LONG;
            bind[i].is_unsigned = 0;
            break;
        case APR_DBD_TYPE_UINT:
            bind[i].buffer = arg;
            bind[i].buffer_type = MYSQL_TYPE_LONG;
            bind[i].is_unsigned = 1;
            break;
        case APR_DBD_TYPE_LONG:
            if (sizeof(int) == sizeof(long)) {
                bind[i].buffer = arg;
            }
            else {
                bind[i].buffer = apr_palloc(pool, sizeof(int));
                *(int*)bind[i].buffer = *(long*)arg;
            }
            bind[i].buffer_type = MYSQL_TYPE_LONG;
            bind[i].is_unsigned = 0;
            break;
        case APR_DBD_TYPE_ULONG:
            if (sizeof(unsigned int) == sizeof(unsigned long)) {
                bind[i].buffer = arg;
            }
            else {
                bind[i].buffer = apr_palloc(pool, sizeof(unsigned int));
                *(unsigned int*)bind[i].buffer = *(unsigned long*)arg;
            }
            bind[i].buffer_type = MYSQL_TYPE_LONG;
            bind[i].is_unsigned = 1;
            break;
        case APR_DBD_TYPE_LONGLONG:
            if (sizeof(my_ulonglong) == sizeof(apr_int64_t)) {
                bind[i].buffer = arg;
                bind[i].buffer_type = MYSQL_TYPE_LONGLONG;
            }
            else { /* have to downsize, long long is not portable */
                bind[i].buffer = apr_palloc(pool, sizeof(long));
                *(long*)bind[i].buffer = (long) *(apr_int64_t*)arg;
                bind[i].buffer_type = MYSQL_TYPE_LONG;
            }
            bind[i].is_unsigned = 0;
            break;
        case APR_DBD_TYPE_ULONGLONG:
            if (sizeof(my_ulonglong) == sizeof(apr_uint64_t)) {
                bind[i].buffer = arg;
                bind[i].buffer_type = MYSQL_TYPE_LONGLONG;
            }
            else { /* have to downsize, long long is not portable */
                bind[i].buffer = apr_palloc(pool, sizeof(long));
                *(unsigned long*)bind[i].buffer =
                    (unsigned long) *(apr_uint64_t*)arg;
                bind[i].buffer_type = MYSQL_TYPE_LONG;
            }
            bind[i].is_unsigned = 1;
            break;
        case APR_DBD_TYPE_FLOAT:
            bind[i].buffer = arg;
            bind[i].buffer_type = MYSQL_TYPE_FLOAT;
            bind[i].is_unsigned = 0;
            break;
        case APR_DBD_TYPE_DOUBLE:
            bind[i].buffer = arg;
            bind[i].buffer_type = MYSQL_TYPE_DOUBLE;
            bind[i].is_unsigned = 0;
            break;
        case APR_DBD_TYPE_STRING:
        case APR_DBD_TYPE_TEXT:
        case APR_DBD_TYPE_TIME:
        case APR_DBD_TYPE_DATE:
        case APR_DBD_TYPE_DATETIME:
        case APR_DBD_TYPE_TIMESTAMP:
        case APR_DBD_TYPE_ZTIMESTAMP:
            bind[i].buffer = arg;
            bind[i].buffer_type = MYSQL_TYPE_VAR_STRING;
            bind[i].is_unsigned = 0;
            bind[i].buffer_length = strlen((const char *)arg);
            break;
        case APR_DBD_TYPE_BLOB:
        case APR_DBD_TYPE_CLOB:
            bind[i].buffer = (void *)arg;
            bind[i].buffer_type = MYSQL_TYPE_LONG_BLOB;
            bind[i].is_unsigned = 0;
            bind[i].buffer_length = *(apr_size_t*)values[++j];

            /* skip table and column */
            j += 2;
            break;
        case APR_DBD_TYPE_NULL:
        default:
            bind[i].buffer_type = MYSQL_TYPE_NULL;
            break;
        }
    }

    return;
}

static int dbd_mysql_pbquery(apr_pool_t *pool, apr_dbd_t *sql,
                             int *nrows, apr_dbd_prepared_t *statement,
                             const void **values)
{
    MYSQL_BIND *bind;
    int ret;

    if (sql->trans && sql->trans->errnum) {
        return sql->trans->errnum;
    }

    bind = apr_palloc(pool, statement->nargs * sizeof(MYSQL_BIND));

    dbd_mysql_bbind(pool, statement, values, bind);

    ret = dbd_mysql_pquery_internal(pool, sql, nrows, statement, bind);

    if (TXN_NOTICE_ERRORS(sql->trans)) {
        sql->trans->errnum = ret;
    }
    return ret;
}

static int dbd_mysql_pvbquery(apr_pool_t *pool, apr_dbd_t *sql, int *nrows,
                              apr_dbd_prepared_t *statement, va_list args)
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

    return dbd_mysql_pbquery(pool, sql, nrows, statement, values);
}

static int dbd_mysql_pbselect(apr_pool_t *pool, apr_dbd_t *sql,
                              apr_dbd_results_t **res,
                              apr_dbd_prepared_t *statement, int random,
                              const void **args)
{
    int ret;
    MYSQL_BIND *bind;

    if (sql->trans && sql->trans->errnum) {
        return sql->trans->errnum;
    }

    bind = apr_palloc(pool, statement->nargs * sizeof(MYSQL_BIND));

    dbd_mysql_bbind(pool, statement, args, bind);

    ret = dbd_mysql_pselect_internal(pool, sql,  res, statement, random, bind);

    if (TXN_NOTICE_ERRORS(sql->trans)) {
        sql->trans->errnum = ret;
    }
    return ret;
}

static int dbd_mysql_pvbselect(apr_pool_t *pool, apr_dbd_t *sql,
                               apr_dbd_results_t **res,
                               apr_dbd_prepared_t *statement, int random,
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

    return dbd_mysql_pbselect(pool, sql, res, statement, random, values);
}

static int dbd_mysql_end_transaction(apr_dbd_transaction_t *trans)
{
    int ret = -1;
    if (trans) {
        /* rollback on error or explicit rollback request */
        if (trans->errnum || TXN_DO_ROLLBACK(trans)) {
            trans->errnum = 0;
            ret = mysql_rollback(trans->handle->conn);
        }
        else {
            ret = mysql_commit(trans->handle->conn);
        }
        ret |= mysql_autocommit(trans->handle->conn, 1);
        trans->handle->trans = NULL;
    }
    return ret;
}
/* Whether or not transactions work depends on whether the
 * underlying DB supports them within MySQL.  Unfortunately
 * it fails silently with the default InnoDB.
 */

static int dbd_mysql_transaction(apr_pool_t *pool, apr_dbd_t *handle,
                                 apr_dbd_transaction_t **trans)
{
    /* Don't try recursive transactions here */
    if (handle->trans) {
        dbd_mysql_end_transaction(handle->trans) ;
    }
    if (!*trans) {
        *trans = apr_pcalloc(pool, sizeof(apr_dbd_transaction_t));
    }
    (*trans)->errnum = mysql_autocommit(handle->conn, 0);
    (*trans)->handle = handle;
    handle->trans = *trans;
    return (*trans)->errnum;
}

static int dbd_mysql_transaction_mode_get(apr_dbd_transaction_t *trans)
{
    if (!trans)
        return APR_DBD_TRANSACTION_COMMIT;

    return trans->mode;
}

static int dbd_mysql_transaction_mode_set(apr_dbd_transaction_t *trans,
                                          int mode)
{
    if (!trans)
        return APR_DBD_TRANSACTION_COMMIT;

    return trans->mode = (mode & TXN_MODE_BITS);
}

static apr_dbd_t *dbd_mysql_open(apr_pool_t *pool, const char *params,
                                 const char **error)
{
    static const char *const delims = " \r\n\t;|,";
    const char *ptr;
    int i;
    const char *key;
    size_t klen;
    const char *value;
    size_t vlen;
#if MYSQL_VERSION_ID >= 50013
    my_bool do_reconnect = 1;
#endif
    MYSQL *real_conn;
    unsigned long flags = 0;

    struct {
        const char *field;
        const char *value;
    } fields[] = {
        {"host", NULL},
        {"user", NULL},
        {"pass", NULL},
        {"dbname", NULL},
        {"port", NULL},
        {"sock", NULL},
        {"flags", NULL},
        {"fldsz", NULL},
        {"group", NULL},
        {"reconnect", NULL},
        {NULL, NULL}
    };
    unsigned int port = 0;
    apr_dbd_t *sql = apr_pcalloc(pool, sizeof(apr_dbd_t));
    sql->fldsz = FIELDSIZE;
    sql->conn = mysql_init(sql->conn);
    if ( sql->conn == NULL ) {
        return NULL;
    }
    for (ptr = strchr(params, '='); ptr; ptr = strchr(ptr, '=')) {
        /* don't dereference memory that may not belong to us */
        if (ptr == params) {
            ++ptr;
            continue;
        }
        for (key = ptr-1; apr_isspace(*key); --key);
        klen = 0;
        while (apr_isalpha(*key)) {
            /* don't parse backwards off the start of the string */
            if (key == params) {
                --key;
                ++klen;
                break;
            }
            --key;
            ++klen;
        }
        ++key;
        for (value = ptr+1; apr_isspace(*value); ++value);
        vlen = strcspn(value, delims);
        for (i = 0; fields[i].field != NULL; i++) {
            if (!strncasecmp(fields[i].field, key, klen)) {
                fields[i].value = apr_pstrndup(pool, value, vlen);
                break;
            }
        }
        ptr = value+vlen;
    }
    if (fields[4].value != NULL) {
        port = atoi(fields[4].value);
    }
    if (fields[6].value != NULL &&
        !strcmp(fields[6].value, "CLIENT_FOUND_ROWS")) {
        flags |= CLIENT_FOUND_ROWS; /* only option we know */
    }
    if (fields[7].value != NULL) {
        sql->fldsz = atol(fields[7].value);
    }
    if (fields[8].value != NULL) {
         mysql_options(sql->conn, MYSQL_READ_DEFAULT_GROUP, fields[8].value);
    }
#if MYSQL_VERSION_ID >= 50013
    if (fields[9].value != NULL) {
         do_reconnect = atoi(fields[9].value) ? 1 : 0;
    }
#endif

#if MYSQL_VERSION_ID >= 50013
    /* the MySQL manual says this should be BEFORE mysql_real_connect */
    mysql_options(sql->conn, MYSQL_OPT_RECONNECT, &do_reconnect);
#endif

    real_conn = mysql_real_connect(sql->conn, fields[0].value,
                                   fields[1].value, fields[2].value,
                                   fields[3].value, port,
                                   fields[5].value, flags);

    if(real_conn == NULL) {
        if (error) {
            *error = apr_pstrdup(pool, mysql_error(sql->conn));
        }
        mysql_close(sql->conn);
        return NULL;
    }

#if MYSQL_VERSION_ID >= 50013
    /* Some say this should be AFTER mysql_real_connect */
    mysql_options(sql->conn, MYSQL_OPT_RECONNECT, &do_reconnect);
#endif

    return sql;
}

static apr_status_t dbd_mysql_close(apr_dbd_t *handle)
{
    mysql_close(handle->conn);
    return APR_SUCCESS;
}

static apr_status_t dbd_mysql_check_conn(apr_pool_t *pool,
                                         apr_dbd_t *handle)
{
    return mysql_ping(handle->conn) ? APR_EGENERAL : APR_SUCCESS;
}

static int dbd_mysql_select_db(apr_pool_t *pool, apr_dbd_t* handle,
                               const char* name)
{
    return mysql_select_db(handle->conn, name);
}

static void *dbd_mysql_native(apr_dbd_t *handle)
{
    return handle->conn;
}

static int dbd_mysql_num_cols(apr_dbd_results_t *res)
{
    if (res->statement) {
        return mysql_stmt_field_count(res->statement);
    }
    else {
        return mysql_num_fields(res->res);
    }
}

static int dbd_mysql_num_tuples(apr_dbd_results_t *res)
{
    if (res->random) {
        if (res->statement) {
            return (int) mysql_stmt_num_rows(res->statement);
        }
        else {
            return (int) mysql_num_rows(res->res);
        }
    }
    else {
        return -1;
    }
}

static apr_status_t thread_end(void *data)
{
    mysql_thread_end();
    return APR_SUCCESS;
}

static void dbd_mysql_init(apr_pool_t *pool)
{
    my_init();
    mysql_thread_init();

    /* FIXME: this is a guess; find out what it really does */
    apr_pool_cleanup_register(pool, NULL, thread_end, apr_pool_cleanup_null);
}
APU_MODULE_DECLARE_DATA const apr_dbd_driver_t apr_dbd_mysql_driver = {
    "mysql",
    dbd_mysql_init,
    dbd_mysql_native,
    dbd_mysql_open,
    dbd_mysql_check_conn,
    dbd_mysql_close,
    dbd_mysql_select_db,
    dbd_mysql_transaction,
    dbd_mysql_end_transaction,
    dbd_mysql_query,
    dbd_mysql_select,
    dbd_mysql_num_cols,
    dbd_mysql_num_tuples,
    dbd_mysql_get_row,
    dbd_mysql_get_entry,
    dbd_mysql_error,
    dbd_mysql_escape,
    dbd_mysql_prepare,
    dbd_mysql_pvquery,
    dbd_mysql_pvselect,
    dbd_mysql_pquery,
    dbd_mysql_pselect,
    dbd_mysql_get_name,
    dbd_mysql_transaction_mode_get,
    dbd_mysql_transaction_mode_set,
    "?",
    dbd_mysql_pvbquery,
    dbd_mysql_pvbselect,
    dbd_mysql_pbquery,
    dbd_mysql_pbselect,
    dbd_mysql_datum_get
};

#endif
