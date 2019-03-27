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
#include "apu_config.h"

/* COMPILE_STUBS: compile stubs for unimplemented functions.
 *
 * This is required to compile in /trunk/, but can be
 * undefined to compile a driver for httpd-2.2 and other
 * APR-1.2 applications
 */
#define COMPILE_STUBS

#if APU_HAVE_FREETDS

#include <ctype.h>
#include <stdlib.h>

#include "apr_strings.h"
#include "apr_lib.h"

#include "apr_pools.h"
#include "apr_dbd_internal.h"

#ifdef HAVE_FREETDS_SYBDB_H
#include <freetds/sybdb.h>
#endif
#ifdef HAVE_SYBDB_H
#include <sybdb.h>
#endif

#include <stdio.h>
#include <sys/types.h>
#include <regex.h>

/* This probably needs to change for different applications */
#define MAX_COL_LEN 256

typedef struct freetds_cell_t {
    int type;
    DBINT len;
    BYTE *data;
} freetds_cell_t;

struct apr_dbd_transaction_t {
    int mode;
    int errnum;
    apr_dbd_t *handle;
};

struct apr_dbd_t {
    DBPROCESS *proc;
    apr_dbd_transaction_t *trans;
    apr_pool_t *pool;
    const char *params;
    RETCODE err;
};

struct apr_dbd_results_t {
    int random;
    size_t ntuples;
    size_t sz;
    apr_pool_t *pool;
    DBPROCESS *proc;
};

struct apr_dbd_row_t {
    apr_dbd_results_t *res;
    BYTE buf[MAX_COL_LEN];
};

struct apr_dbd_prepared_t {
    int nargs;
    regex_t **taint;
    int *sz;
    char *fmt;
};

#define dbd_freetds_is_success(x) (x == SUCCEED)

static int labelnum = 0; /* FIXME */
static regex_t dbd_freetds_find_arg;

/* execute a query that doesn't return a result set, mop up,
 * and return and APR-flavoured status
 */
static RETCODE freetds_exec(DBPROCESS *proc, const char *query,
                            int want_results, int *nrows)
{
    /* TBD */
    RETCODE rv = dbcmd(proc, query);
    if (rv != SUCCEED) {
        return rv;
    }
    rv = dbsqlexec(proc);
    if (rv != SUCCEED) {
        return rv;
    }
    if (!want_results) {
        while (dbresults(proc) != NO_MORE_RESULTS) {
            ++*nrows;
        }
    }
    return SUCCEED;
}
static apr_status_t clear_result(void *data)
{
    /* clear cursor */
    return (dbcanquery((DBPROCESS*)data) == SUCCEED)
            ? APR_SUCCESS
            : APR_EGENERAL;
}

static int dbd_freetds_select(apr_pool_t *pool, apr_dbd_t *sql,
                              apr_dbd_results_t **results,
                              const char *query, int seek)
{
    apr_dbd_results_t *res;
    if (sql->trans && (sql->trans->errnum != SUCCEED)) {
        return 1;
    }
    /* the core of this is
     * dbcmd(proc, query);
     * dbsqlexec(proc);
     * while (dbnextrow(dbproc) != NO_MORE_ROWS) {
     *     do things
     * }
     *
     * Ignore seek
     */

    sql->err = freetds_exec(sql->proc, query, 1, NULL);
    if (!dbd_freetds_is_success(sql->err)) {
        if (sql->trans) {
            sql->trans->errnum = sql->err;
        }
        return 1;
    }

    sql->err = dbresults(sql->proc);
    if (sql->err != SUCCEED) {
        if (sql->trans) {
            sql->trans->errnum = sql->err;
        }
        return 1;
    }

    if (!*results) {
        *results = apr_pcalloc(pool, sizeof(apr_dbd_results_t));
    }
    res = *results;
    res->proc = sql->proc;
    res->random = seek;
    res->pool = pool;
    res->ntuples = dblastrow(sql->proc);
    res->sz = dbnumcols(sql->proc);
    apr_pool_cleanup_register(pool, sql->proc, clear_result,
                              apr_pool_cleanup_null);

#if 0
    /* Now we have a result set.  We need to bind to its vars */
    res->vars = apr_palloc(pool, res->sz * sizeof(freetds_cell_t*));
    for (i=1; i <= res->sz; ++i) {
        freetds_cell_t *cell = &res->vars[i-1];
        cell->type = dbcoltype(sql->proc, i);
        cell->len = dbcollen(sql->proc, i);
        cell->data = apr_palloc(pool, cell->len);
        sql->err = dbbind(sql->proc, i, /*cell->type */ STRINGBIND, cell->len, cell->data);
        if (sql->err != SUCCEED) {
            fprintf(stderr, "dbbind error: %d, %d, %d", i, cell->type, cell->len);
        }
        if ((sql->err != SUCCEED) && (sql->trans != NULL)) {
            sql->trans->errnum = sql->err;
        }
    }
#endif
    return (sql->err == SUCCEED) ? 0 : 1;
}
static const char *dbd_untaint(apr_pool_t *pool, regex_t *rx, const char *val)
{
    regmatch_t match[1];
    if (rx == NULL) {
        /* no untaint expression */
        return val;
    }
    if (regexec(rx, val, 1, match, 0) == 0) {
        return apr_pstrndup(pool, val+match[0].rm_so,
                            match[0].rm_eo - match[0].rm_so);
    }
    return "";
}
static const char *dbd_statement(apr_pool_t *pool,
                                 apr_dbd_prepared_t *stmt,
                                 int nargs, const char **args)
{
    int i;
    int len;
    const char *var;
    char *ret;
    const char *p_in;
    char *p_out;
    char *q;
   
    /* compute upper bound on length (since untaint shrinks) */
    len  = strlen(stmt->fmt) +1;
    for (i=0; i<nargs; ++i) {
        len += strlen(args[i]) - 2;
    }
    i = 0;
    p_in = stmt->fmt;
    p_out = ret = apr_palloc(pool, len);
    /* FIXME silly bug - this'll catch %%s */
    while (q = strstr(p_in, "%s"), q != NULL) {
        len = q-p_in;
        strncpy(p_out, p_in, len);
        p_in += len;
        p_out += len;
        var = dbd_untaint(pool, stmt->taint[i], args[i]);
        len = strlen(var);
        strncpy(p_out, var, len);
        p_in += 2;
        p_out += len;
        ++i;
    }
    strcpy(p_out, p_in);
    return ret;
}
static int dbd_freetds_pselect(apr_pool_t *pool, apr_dbd_t *sql,
                               apr_dbd_results_t **results,
                               apr_dbd_prepared_t *statement,
                               int seek, const char **values)
{
    const char *query = dbd_statement(pool, statement,
                                      statement->nargs, values);
    return dbd_freetds_select(pool, sql, results, query, seek);
}
static int dbd_freetds_pvselect(apr_pool_t *pool, apr_dbd_t *sql,
                                apr_dbd_results_t **results,
                                apr_dbd_prepared_t *statement,
                                int seek, va_list args)
{
    const char **values;
    int i;

    if (sql->trans && sql->trans->errnum) {
        return sql->trans->errnum;
    }

    values = apr_palloc(pool, sizeof(*values) * statement->nargs);

    for (i = 0; i < statement->nargs; i++) {
        values[i] = va_arg(args, const char*);
    }

    return dbd_freetds_pselect(pool, sql, results, statement, seek, values);
}
static int dbd_freetds_query(apr_dbd_t *sql, int *nrows, const char *query);
static int dbd_freetds_pquery(apr_pool_t *pool, apr_dbd_t *sql,
                              int *nrows, apr_dbd_prepared_t *statement,
                              const char **values)
{
    const char *query = dbd_statement(pool, statement,
                                      statement->nargs, values);
    return dbd_freetds_query(sql, nrows, query);
}
static int dbd_freetds_pvquery(apr_pool_t *pool, apr_dbd_t *sql, int *nrows,
                               apr_dbd_prepared_t *statement, va_list args)
{
    const char **values;
    int i;

    if (sql->trans && sql->trans->errnum) {
        return sql->trans->errnum;
    }

    values = apr_palloc(pool, sizeof(*values) * statement->nargs);

    for (i = 0; i < statement->nargs; i++) {
        values[i] = va_arg(args, const char*);
    }
    return dbd_freetds_pquery(pool, sql, nrows, statement, values);
}

static int dbd_freetds_get_row(apr_pool_t *pool, apr_dbd_results_t *res,
                               apr_dbd_row_t **rowp, int rownum)
{
    RETCODE rv = 0;
    apr_dbd_row_t *row = *rowp;
    int sequential = ((rownum >= 0) && res->random) ? 0 : 1;

    if (row == NULL) {
        row = apr_palloc(pool, sizeof(apr_dbd_row_t));
        *rowp = row;
        row->res = res;
    }
    /*
    else {
        if ( sequential ) {
            ++row->n;
        }
        else {
            row->n = rownum;
        }
    }
    */
    if (sequential) {
        rv = dbnextrow(res->proc);
    }
    else {
        rv = (rownum >= 0) ? dbgetrow(res->proc, rownum) : NO_MORE_ROWS;
    }
    switch (rv) {
    case SUCCEED: return 0;
    case REG_ROW: return 0;
    case NO_MORE_ROWS:
        apr_pool_cleanup_run(res->pool, res->proc, clear_result);
        *rowp = NULL;
        return -1;
    case FAIL: return 1;
    case BUF_FULL: return 2; /* FIXME */
    default: return 3;
    }

    return 0;
}

static const char *dbd_freetds_get_entry(const apr_dbd_row_t *row, int n)
{
    /* FIXME: support different data types */
    /* this fails - bind gets some vars but not others
    return (const char*)row->res->vars[n].data;
     */
    DBPROCESS* proc = row->res->proc;
    BYTE *ptr = dbdata(proc, n+1);
    int t = dbcoltype(proc, n+1);
    int l = dbcollen(proc, n+1);
    if (dbwillconvert(t, SYBCHAR)) {
      dbconvert(proc, t, ptr, l, SYBCHAR, (BYTE *)row->buf, -1);
      return (const char*)row->buf;
    }
    return (char*)ptr;
}

static const char *dbd_freetds_error(apr_dbd_t *sql, int n)
{
    /* XXX this doesn't seem to exist in the API ??? */
    return apr_psprintf(sql->pool, "Error %d", sql->err);
}

static int dbd_freetds_query(apr_dbd_t *sql, int *nrows, const char *query)
{
    if (sql->trans && sql->trans->errnum) {
        return sql->trans->errnum;
    }
    *nrows = 0;
    sql->err = freetds_exec(sql->proc, query, 0, nrows);

    if (sql->err != SUCCEED) {
        if (sql->trans) {
            sql->trans->errnum = sql->err;
        }
        return 1;
    }
    return 0;
}

static const char *dbd_freetds_escape(apr_pool_t *pool, const char *arg,
                                      apr_dbd_t *sql)
{
    return arg;
}

static apr_status_t freetds_regfree(void *rx)
{
    regfree((regex_t*)rx);
    return APR_SUCCESS;
}
static int recurse_args(apr_pool_t *pool, int n, const char *query,
                        apr_dbd_prepared_t *stmt, int offs)
{

    /* we only support %s arguments for now */
    int ret;
    char arg[256];
    regmatch_t matches[3];
    if (regexec(&dbd_freetds_find_arg, query, 3, matches, 0) != 0) {
        /* No more args */
        stmt->nargs = n;
        stmt->taint = apr_palloc(pool, n*sizeof(regex_t*));
        stmt->sz = apr_palloc(pool, n*sizeof(int));
        ret = 0;
    }
    else {
        int i;
        int sz = 0;
        int len = matches[1].rm_eo - matches[1].rm_so - 2;
        if (len > 255) {
            return 9999;
        }

        ret = recurse_args(pool, n+1, query+matches[0].rm_eo,
                           stmt, offs+matches[0].rm_eo);

        memmove(stmt->fmt + offs + matches[1].rm_so,
                stmt->fmt + offs + matches[0].rm_eo-1,
                strlen(stmt->fmt+offs+matches[0].rm_eo)+2);

        /* compile untaint to a regex if found */
        if (matches[1].rm_so == -1) {
            stmt->taint[n] = NULL;
        }
        else {
            strncpy(arg, query+matches[1].rm_so+1,
                    matches[1].rm_eo - matches[1].rm_so - 2);
            arg[matches[1].rm_eo - matches[1].rm_so - 2] = '\0';
            stmt->taint[n] = apr_palloc(pool, sizeof(regex_t));
            if (regcomp(stmt->taint[n], arg, REG_ICASE|REG_EXTENDED) != 0) {
                ++ret;
            }
            else {
                apr_pool_cleanup_register(pool, stmt->taint[n], freetds_regfree,
                                          apr_pool_cleanup_null);
            }
        }

        /* record length if specified */
        for (i=matches[2].rm_so; i<matches[2].rm_eo; ++i) {
            sz = 10*sz + (query[i]-'\0');
        }
    }
    return ret;
}

static int dbd_freetds_prepare(apr_pool_t *pool, apr_dbd_t *sql,
                             const char *query, const char *label,
                             int nargs, int nvals, apr_dbd_type_e *types,
                             apr_dbd_prepared_t **statement)
{
    apr_dbd_prepared_t *stmt;

    if (label == NULL) {
        label = apr_psprintf(pool, "%d", labelnum++);
    }

    if (!*statement) {
        *statement = apr_palloc(pool, sizeof(apr_dbd_prepared_t));
    }
    stmt = *statement;

#if 0
    /* count args */
    stmt->fmt = apr_pstrdup(pool, query);
    stmt->fmt = recurse_args(pool, 0, query, stmt, stmt->fmt);

    /* overestimate by a byte or two to simplify */
    len = strlen("CREATE PROC apr.")
            + strlen(label)
            + stmt->nargs * strlen(" @arg1 varchar(len1),")
            + strlen(" AS begin ")
            + strlen(stmt->fmt)
            + strlen(" end "); /* extra byte for terminator */

    pquery = apr_pcalloc(pool, len);
    sprintf(pquery, "CREATE PROC apr.%s", label);
    for (i=0; i<stmt->nargs; ++i) {
        sprintf(pquery+strlen(pquery), " @arg%d varchar(%d)", i, stmt->sz[i]);
        if (i < stmt->nargs-1) {
            pquery[strlen(pquery)] = ',';
        }
    }
    strcat(pquery, " AS BEGIN ");
    strcat(pquery, stmt->fmt);
    strcat(pquery, " END");

    return (freetds_exec(sql->proc, pquery, 0, &i) == SUCCEED) ? 0 : 1;
#else
    stmt->fmt = apr_pstrdup(pool, query);
    return recurse_args(pool, 0, query, stmt, 0);
#endif

}

static int dbd_freetds_start_transaction(apr_pool_t *pool, apr_dbd_t *handle,
                                         apr_dbd_transaction_t **trans)
{
    int dummy;

    /* XXX handle recursive transactions here */

    handle->err = freetds_exec(handle->proc, "BEGIN TRANSACTION", 0, &dummy);

    if (dbd_freetds_is_success(handle->err)) {
        if (!*trans) {
            *trans = apr_pcalloc(pool, sizeof(apr_dbd_transaction_t));
        }
        (*trans)->handle = handle;
        handle->trans = *trans;
        return 0;
    }

    return 1;
}

static int dbd_freetds_end_transaction(apr_dbd_transaction_t *trans)
{
    int dummy;
    if (trans) {
        /* rollback on error or explicit rollback request */
        if (trans->errnum) {
            trans->errnum = 0;
            trans->handle->err = freetds_exec(trans->handle->proc,
                                              "ROLLBACK", 0, &dummy);
        }
        else {
            trans->handle->err = freetds_exec(trans->handle->proc,
                                              "COMMIT", 0, &dummy);
        }
        trans->handle->trans = NULL;
    }
    return (trans->handle->err == SUCCEED) ? 0 : 1;
}

static DBPROCESS *freetds_open(apr_pool_t *pool, const char *params,
                               const char **error)
{
    char *server = NULL;
    DBPROCESS *process;
    LOGINREC *login;
    static const char *delims = " \r\n\t;|,";
    char *ptr;
    char *key;
    char *value;
    int vlen;
    int klen;
    char *buf;
    char *databaseName = NULL;

    /* FIXME - this uses malloc */
    /* FIXME - pass error message back to the caller in case of failure */
    login = dblogin();
    if (login == NULL) {
        return NULL;
    }
    /* now set login properties */
    for (ptr = strchr(params, '='); ptr; ptr = strchr(ptr, '=')) {
        /* don't dereference memory that may not belong to us */
        if (ptr == params) {
            ++ptr;
            continue;
        }
        for (key = ptr-1; apr_isspace(*key); --key);
        klen = 0;
        while (apr_isalpha(*key)) {
            --key;
            ++klen;
        }
        ++key;
        for (value = ptr+1; apr_isspace(*value); ++value);

        vlen = strcspn(value, delims);
        buf = apr_pstrndup(pool, value, vlen);        /* NULL-terminated copy */

        if (!strncasecmp(key, "username", klen)) {
            DBSETLUSER(login, buf);
        }
        else if (!strncasecmp(key, "password", klen)) {
            DBSETLPWD(login, buf);
        }
        else if (!strncasecmp(key, "appname", klen)) {
            DBSETLAPP(login, buf);
        }
        else if (!strncasecmp(key, "dbname", klen)) {
            databaseName = buf;
        }
        else if (!strncasecmp(key, "host", klen)) {
            DBSETLHOST(login, buf);
        }
        else if (!strncasecmp(key, "charset", klen)) {
            DBSETLCHARSET(login, buf);
        }
        else if (!strncasecmp(key, "lang", klen)) {
            DBSETLNATLANG(login, buf);
        }
        else if (!strncasecmp(key, "server", klen)) {
            server = buf;
        }
        else {
            /* unknown param */
        }
        ptr = value+vlen;
    }

    process = dbopen(login, server);

    if (process != NULL && databaseName != NULL)
    {
        dbuse(process, databaseName);
    }
 
    dbloginfree(login);
    if (process == NULL) {
        return NULL;
    }

    return process;
}
static apr_dbd_t *dbd_freetds_open(apr_pool_t *pool, const char *params,
                                   const char **error)
{
    apr_dbd_t *sql;
    /* FIXME - pass error message back to the caller in case of failure */
    DBPROCESS *process = freetds_open(pool, params, error);
    if (process == NULL) {
        return NULL;
    }
    sql = apr_pcalloc(pool, sizeof (apr_dbd_t));
    sql->pool = pool;
    sql->proc = process;
    sql->params = params;
    return sql;
}

static apr_status_t dbd_freetds_close(apr_dbd_t *handle)
{
    dbclose(handle->proc);
    return APR_SUCCESS;
}

static apr_status_t dbd_freetds_check_conn(apr_pool_t *pool,
                                           apr_dbd_t *handle)
{
    if (dbdead(handle->proc)) {
        /* try again */
        dbclose(handle->proc);
        handle->proc = freetds_open(handle->pool, handle->params, NULL);
        if (!handle->proc || dbdead(handle->proc)) {
            return APR_EGENERAL;
        }
    }
    /* clear it, in case this is called in error handling */
    dbcancel(handle->proc);
    return APR_SUCCESS;
}

static int dbd_freetds_select_db(apr_pool_t *pool, apr_dbd_t *handle,
                               const char *name)
{
    /* ouch, it's declared int.  But we can use APR 0/nonzero */
    return (dbuse(handle->proc, (char*)name) == SUCCEED) ? APR_SUCCESS : APR_EGENERAL;
}

static void *dbd_freetds_native(apr_dbd_t *handle)
{
    return handle->proc;
}

static int dbd_freetds_num_cols(apr_dbd_results_t* res)
{
    return res->sz;
}

static int dbd_freetds_num_tuples(apr_dbd_results_t* res)
{
    if (res->random) {
        return res->ntuples;
    }
    else {
        return -1;
    }
}

static apr_status_t freetds_term(void *dummy)
{
    dbexit();
    regfree(&dbd_freetds_find_arg);
    return APR_SUCCESS;
}
static int freetds_err_handler(DBPROCESS *dbproc, int severity, int dberr,
                               int oserr, char *dberrstr, char *oserrstr)
{
    return INT_CANCEL; /* never exit */
}
static void dbd_freetds_init(apr_pool_t *pool)
{
    int rv = regcomp(&dbd_freetds_find_arg,
                     "%(\\{[^}]*\\})?([0-9]*)[A-Za-z]", REG_EXTENDED);
    if (rv != 0) {
        char errmsg[256];
        regerror(rv, &dbd_freetds_find_arg, errmsg, 256);
        fprintf(stderr, "regcomp failed: %s\n", errmsg);
    }
    dbinit();
    dberrhandle(freetds_err_handler);
    apr_pool_cleanup_register(pool, NULL, freetds_term, apr_pool_cleanup_null);
}

#ifdef COMPILE_STUBS
/* get_name is the only one of these that is implemented */
static const char *dbd_freetds_get_name(const apr_dbd_results_t *res, int n)
{
    return (const char*) dbcolname(res->proc, n+1); /* numbering starts at 1 */
}

/* These are stubs: transaction modes not implemented here */
#define DBD_NOTIMPL APR_ENOTIMPL;
static int dbd_freetds_transaction_mode_get(apr_dbd_transaction_t *trans)
{
    return trans ? trans->mode : APR_DBD_TRANSACTION_COMMIT;
}

static int dbd_freetds_transaction_mode_set(apr_dbd_transaction_t *trans,
                                            int mode)
{
    if (trans) {
        trans->mode = mode & TXN_MODE_BITS;
        return trans->mode;
    }
    return APR_DBD_TRANSACTION_COMMIT;
}
static int dbd_freetds_pvbquery(apr_pool_t *pool, apr_dbd_t *sql, int *nrows,
                                apr_dbd_prepared_t *statement, va_list args)
{
    return DBD_NOTIMPL;
}
static int dbd_freetds_pbquery(apr_pool_t *pool, apr_dbd_t *sql, int *nrows,
                               apr_dbd_prepared_t * statement,
                               const void **values)
{
    return DBD_NOTIMPL;
}

static int dbd_freetds_pvbselect(apr_pool_t *pool, apr_dbd_t *sql,
                                 apr_dbd_results_t **results,
                                 apr_dbd_prepared_t *statement,
                                 int seek, va_list args)
{
    return DBD_NOTIMPL;
}
static int dbd_freetds_pbselect(apr_pool_t *pool, apr_dbd_t *sql,
                                apr_dbd_results_t **results,
                                apr_dbd_prepared_t *statement,
                                int seek, const void **values)
{
    return DBD_NOTIMPL;
}
static apr_status_t dbd_freetds_datum_get(const apr_dbd_row_t *row, int n,
                                          apr_dbd_type_e type, void *data)
{
    return APR_ENOTIMPL;
}
#endif

APU_MODULE_DECLARE_DATA const apr_dbd_driver_t apr_dbd_freetds_driver = {
    "freetds",
    dbd_freetds_init,
    dbd_freetds_native,
    dbd_freetds_open,
    dbd_freetds_check_conn,
    dbd_freetds_close,
    dbd_freetds_select_db,
    dbd_freetds_start_transaction,
    dbd_freetds_end_transaction,
    dbd_freetds_query,
    dbd_freetds_select,
    dbd_freetds_num_cols,
    dbd_freetds_num_tuples,
    dbd_freetds_get_row,
    dbd_freetds_get_entry,
    dbd_freetds_error,
    dbd_freetds_escape,
    dbd_freetds_prepare,
    dbd_freetds_pvquery,
    dbd_freetds_pvselect,
    dbd_freetds_pquery,
    dbd_freetds_pselect,
    /* this is only implemented to support httpd/2.2 standard usage,
     * as in the original DBD implementation.  Everything else is NOTIMPL.
     */
#ifdef COMPILE_STUBS
    dbd_freetds_get_name,
    dbd_freetds_transaction_mode_get,
    dbd_freetds_transaction_mode_set,
    "",
    dbd_freetds_pvbquery,
    dbd_freetds_pvbselect,
    dbd_freetds_pbquery,
    dbd_freetds_pbselect,
    dbd_freetds_datum_get
#endif
};
#endif
